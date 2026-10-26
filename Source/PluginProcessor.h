#pragma once
#include <JuceHeader.h>
#include "flp.h"   // at the top of PluginProcessor.h, for FL::Project


class FLProjectOrganizerProcessor : public juce::AudioProcessor
{
public:
    FLProjectOrganizerProcessor();
    ~FLProjectOrganizerProcessor() override;
    // inside FLProjectOrganizerProcessor's public section:
    template <typename Fn>
    int withProjectLock(Fn&& fn)
    {
        const juce::ScopedLock sl(projectLock);
        return fn(projectPtr.get());
    }

    bool saveProjectAs(const juce::File& file);

    
    //==============================================================================
    void prepareToPlay(double sampleRate, int samplesPerBlock) override {}
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override { return true; }
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    //==============================================================================
    const juce::String getName() const override { return "FL Project Organizer"; }
    double getTailLengthSeconds() const override { return 0.0; }

    //==============================================================================
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int index) override {}
    const juce::String getProgramName(int index) override { return {}; }
    void changeProgramName(int index, const juce::String& newName) override {}

    //==============================================================================
    void getStateInformation(juce::MemoryBlock& destData) override {}
    void setStateInformation(const void* data, int sizeInBytes) override {}

    //==============================================================================
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }

private:
    // and in the private section:
    juce::CriticalSection projectLock;
    std::unique_ptr<FL::Project> projectPtr;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FLProjectOrganizerProcessor)
};