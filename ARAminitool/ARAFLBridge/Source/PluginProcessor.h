#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "MiniAraHost.h"
#include "SidecarModel.h"
#include "ARA_API/ARAVST3.h"

// Bridge plug-in: a normal (non-ARA) VST3 effect that FL Studio can load. Inside it runs a mini ARA host
// and an "inner" ARA plug-in (e.g. Melodyne, SpectraLayers, or any ARA-capable VST3).
class ARAFLBridgeProcessor : public juce::AudioProcessor,
                             public juce::ChangeBroadcaster,
                             private wam::MiniAraHost::Listener,
                             private juce::Timer
{
public:
    enum class Stage { NoInner, InnerReady, Capturing, Bridged };

    ARAFLBridgeProcessor();
    ~ARAFLBridgeProcessor() override;

    // --- message-thread API used by the editor ---
    void loadInnerPlugin (const juce::File& vst3File);
    void beginCapture();                                   // bridge method A: stream capture (record from FL mixer insert)
    void loadAudioFile (const juce::File& f, bool placeAtPlayhead, double startOverrideSec = -1.0);
    // bridge method D: FLP sidecar (real playlist regions + real audio files from the exporter)
    void loadSidecar (const juce::File& sidecar);
    void setSequence (int index);                         // which FL playlist track this instance represents
    const SidecarModel& getSidecar() const noexcept { return sidecar_; }
    int getSequence() const noexcept { return sequence_; }
    bool hasSidecar() const noexcept { return sidecarLoaded_; }
    juce::String getWarning() const { return warning_; } // bridge method B: file import
    Stage getStage() const noexcept { return stage_; }
    juce::String getStatus() const { return status_; }
    float getAnalysisProgress() const noexcept { return analysis_; }
    double getTempo() const noexcept { return bpm_.load(); }
    juce::AudioProcessorEditor* createInnerEditor();
    bool hasInner() const noexcept { return inner_ != nullptr; }

    // --- juce::AudioProcessor ---
    void prepareToPlay (double sr, int bs) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& l) const override
    { return l.getMainInputChannelSet() == juce::AudioChannelSet::stereo() && l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo(); }
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "ARA FL Bridge"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

private:
    struct FwdPlayHead : juce::AudioPlayHead
    {   // gives the inner plug-in the *outer* host's (FL's) transport - this is what makes ARA timing line up
        juce::AudioPlayHead* outer = nullptr;
        juce::Optional<PositionInfo> getPosition() const override { return outer ? outer->getPosition() : juce::nullopt; }
    };

    void timerCallback() override;
    void araAnalysisProgress (ARA::ARAAnalysisProgressState, float v) override;
    void araPlugInChangedContent() override { sendChangeMessage(); }

    void attachInner (std::unique_ptr<juce::AudioPluginInstance>, const juce::File&);
    void teardownInner();
    bool publishSource (std::shared_ptr<const wam::SourceAudio>, double startSec, const juce::File& backing);
    bool publishGraph (const wam::MiniAraHost::GraphDef&, const juce::String& label);
    bool buildFromSidecar();
    void finishCapture();
    void setStatus (const juce::String& s) { status_ = s; sendChangeMessage(); }
    static juce::File bridgeFolder();

    // inner plug-in + ARA binding
    juce::VST3PluginFormat vst3_;
    std::unique_ptr<juce::AudioPluginInstance> inner_;
    ARA::IPlugInEntryPoint2* entry_ = nullptr;
    const ARA::ARAPlugInExtensionInstance* ext_ = nullptr;
    wam::MiniAraHost host_;
    juce::File innerFile_, backingWav_;
    SidecarModel sidecar_; bool sidecarLoaded_ = false; int sequence_ = 0; juce::String warning_;
    double sourceStart_ = 0.0;
    FwdPlayHead fwd_;
    juce::SpinLock innerLock_;
    bool innerPrepared_ = false, graphLive_ = false;

    // capture (audio thread writes, message thread finalises)
    juce::AudioBuffer<float> capture_;
    std::atomic<int> capWritten_ { 0 };
    std::atomic<bool> capturing_ { false }, capDone_ { false }, capStarted_ { false };
    std::atomic<double> capStart_ { 0.0 };

    // transport snapshot from FL
    std::atomic<double> bpm_ { 120.0 };
    std::atomic<bool> playing_ { false };

    double sr_ = 44100.0; int bs_ = 512;
    Stage stage_ = Stage::NoInner;
    juce::String status_ { "Load an ARA-capable VST3 to begin." };
    float analysis_ = 0.0f;
    std::unique_ptr<juce::XmlElement> pending_;   // state waiting for the inner plug-in to finish loading

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ARAFLBridgeProcessor)
};
