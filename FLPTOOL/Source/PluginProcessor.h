#pragma once
#include <JuceHeader.h>
#include "flp.h"
#include "flphelper.h"

class PluginProcessor  : public juce::AudioProcessor
{
public:
    PluginProcessor();
    ~PluginProcessor() override;

    // =========================================================================
    // JUCE AudioProcessor Overrides
    // =========================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // =========================================================================
    // FLP Utility Methods (Called by the Editor)
    // =========================================================================
    bool loadFLPFile(const juce::File& file);
    bool isProjectLoaded() const { return project != nullptr; }
    
    juce::String getStatsReport() const;
    juce::String getPluginReport() const;
    juce::String getSampleReport() const;
    juce::String getArrangementReport() const;

private:
    // The core FLP project instance
    std::unique_ptr<FL::Project> project;
    
    // Thread safety for loading files
    juce::CriticalSection projectLock;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginProcessor)
};