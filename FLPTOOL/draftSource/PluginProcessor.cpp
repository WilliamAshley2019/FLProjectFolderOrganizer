#include "PluginProcessor.h"
#include "PluginEditor.h"

PluginProcessor::PluginProcessor()
     : AudioProcessor (BusesProperties()
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
}

PluginProcessor::~PluginProcessor() {}

const juce::String PluginProcessor::getName() const { return JucePlugin_Name; }
bool PluginProcessor::acceptsMidi() const { return false; }
bool PluginProcessor::producesMidi() const { return false; }
double PluginProcessor::getTailLengthSeconds() const { return 0.0; }
int PluginProcessor::getNumPrograms() { return 1; }
int PluginProcessor::getCurrentProgram() { return 0; }
void PluginProcessor::setCurrentProgram (int) {}
const juce::String PluginProcessor::getProgramName (int) { return {}; }
void PluginProcessor::changeProgramName (int, const juce::String&) {}

void PluginProcessor::prepareToPlay (double, int) {}
void PluginProcessor::releaseResources() {}

bool PluginProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // This utility tool supports any layout, but primarily passes audio through
    return true; 
}

void PluginProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    // Transparent pass-through. This plugin is a utility GUI tool, not an audio effect.
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        // If you wanted to add a utility meter here, you could scan the buffer.
        // For now, we do nothing to the audio.
    }
}

bool PluginProcessor::hasEditor() const { return true; }
juce::AudioProcessorEditor* PluginProcessor::createEditor() { return new PluginEditor (*this); }

void PluginProcessor::getStateInformation (juce::MemoryBlock&) {}
void PluginProcessor::setStateInformation (const void*, int) {}

// =============================================================================
// FLP Utility Implementations
// =============================================================================
bool PluginProcessor::loadFLPFile(const juce::File& file)
{
    juce::ScopedLock lock(projectLock);
    project.reset(); // Clear previous
    
    if (!file.existsAsFile()) return false;
    
    project = FL::Project::load(file);
    return project != nullptr;
}

juce::String PluginProcessor::getStatsReport() const
{
    juce::ScopedLock lock(projectLock);
    if (!project) return "No project loaded.";
    
    FL::ProjectStats stats = FL::StatsGenerator::generate(*project);
    return stats.generateTextReport();
}

juce::String PluginProcessor::getPluginReport() const
{
    juce::ScopedLock lock(projectLock);
    if (!project) return "No project loaded.";
    
    FL::PluginInspector inspector(*project);
    auto plugins = inspector.getPluginSummary();
    
    juce::StringArray lines;
    lines.add("=== PLUGIN USAGE REPORT ===");
    for (const auto& p : plugins)
    {
        juce::String line = p.name + " (" + p.vendor + ")";
        if (p.isNative) line += " [Native]";
        if (p.isDemo) line += " [DEMO]";
        line += " - Instances: " + juce::String(p.instanceCount);
        lines.add(line);
    }
    return lines.joinIntoString("\n");
}

juce::String PluginProcessor::getSampleReport() const
{
    juce::ScopedLock lock(projectLock);
    if (!project) return "No project loaded.";
    
    FL::SampleScanner scanner(*project);
    scanner.scan();
    auto missing = scanner.getMissingFiles();
    
    juce::StringArray lines;
    lines.add("=== MISSING SAMPLES REPORT ===");
    if (missing.empty())
    {
        lines.add("All samples accounted for! No missing files.");
    }
    else
    {
        lines.add("Found " + juce::String(missing.size()) + " missing samples:");
        for (const auto& sf : missing)
            lines.add(" - " + sf.originalPath + " (Used by: " + sf.channelName + ")");
    }
    return lines.joinIntoString("\n");
}

juce::String PluginProcessor::getArrangementReport() const
{
    juce::ScopedLock lock(projectLock);
    if (!project) return "No project loaded.";
    
    FL::ArrangementDumper dumper(*project);
    return dumper.generateTextReport();
}