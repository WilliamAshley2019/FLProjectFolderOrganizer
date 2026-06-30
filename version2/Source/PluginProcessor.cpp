#include "PluginProcessor.h"
#include "PluginEditor.h"

FLProjectOrganizerProcessor::FLProjectOrganizerProcessor()
    : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true))
{
}

FLProjectOrganizerProcessor::~FLProjectOrganizerProcessor() {}

juce::AudioProcessorEditor* FLProjectOrganizerProcessor::createEditor()
{
    return new FLProjectOrganizerEditor(*this);
}

// This creates new instances of the plugin
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new FLProjectOrganizerProcessor();
}