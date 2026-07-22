#pragma once
#include <JuceHeader.h>
#include "flp.h"
#include "flphelper.h"
#include <utility>

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
    juce::String getLastLoadError() const { return lastLoadError; }
    bool isProjectLoaded() const { return project != nullptr; }
    
    juce::String getStatsReport() const;
    juce::String getPluginReport() const;
    juce::String getSampleReport() const;
    juce::String getArrangementReport() const;
    juce::String getCleanupReport() const;

    // Loads a second project purely for comparison purposes (doesn't
    // replace the main loaded project) and returns the diff report.
    juce::String compareWithFile(const juce::File& otherFile);

    // Exports every pattern in the loaded project to a single .mid file
    // (one MIDI track per pattern, tempo on the first track). Returns a
    // human-readable result message for display, since this is invoked
    // directly from a UI button rather than returning a report string.
    // Exports every channel/pattern/note/arrangement/mixer field this parser
    // already extracts to a single JSON file, for use in external tools
    // without going through FL Studio. Returns a result message for display.
    juce::String exportFullDataAsJson(const juce::File& outputJsonFile);

    juce::String exportPatternsToMidi(const juce::File& outputMidiFile);

    // Direct access for tool windows (AutomationToolsWindow) that need to
    // read/mutate the live project rather than just get a text report.
    // Caller must hold projectLock (via withProjectLock) while using the
    // returned pointer - it can be invalidated by a concurrent loadFLPFile().
    template <typename Fn>
    auto withProjectLock(Fn&& fn) -> decltype(fn(std::declval<FL::Project*>()))
    {
        juce::ScopedLock lock(projectLock);
        return fn(project.get());
    }

    bool saveProjectAs(const juce::File& destFile);

private:
    // The core FLP project instance
    std::unique_ptr<FL::Project> project;
    juce::String lastLoadError;
    
    // Thread safety for loading files
    juce::CriticalSection projectLock;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginProcessor)
};