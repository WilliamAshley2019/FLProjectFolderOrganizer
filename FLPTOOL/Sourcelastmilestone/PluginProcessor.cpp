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

bool PluginProcessor::isBusesLayoutSupported (const BusesLayout& /*layouts*/) const
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
    lastLoadError.clear();
    
    if (!file.existsAsFile()) { lastLoadError = "File does not exist."; return false; }
    
    try
    {
        project = FL::Project::load(file, &lastLoadError);
    }
    catch (const std::exception& e)
    {
        project.reset();
        lastLoadError = juce::String("Unexpected exception: ") + e.what();
    }
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

juce::String PluginProcessor::getCleanupReport() const
{
    juce::ScopedLock lock(projectLock);
    if (!project) return "No project loaded.";

    // Cleaner counts unused patterns/channels but doesn't remove anything
    // from the loaded project (see the note in Cleaner::removeUnusedPatterns) -
    // this is a read-only report, safe to run on a const-accessed project.
    FL::CleanupReport report;
    FL::Cleaner::runAll(*project, report);

    juce::StringArray lines;
    lines.add("=== CLEANUP REPORT (informational only - nothing has been removed) ===");
    lines.add(report.toString());
    lines.add("");
    lines.add(report.removedPatterns > 0 || report.removedChannels > 0
        ? "These patterns/channels aren't referenced anywhere else in the project."
        : "Nothing found - every pattern and channel is referenced somewhere.");

    // "Referenced" for patterns is determined entirely from the modern
    // Playlist blob event. Very old files use a legacy per-clip format
    // (PlayListItem, id 129) this parser doesn't read yet - on those files
    // every pattern looks unreferenced even if it's genuinely used, since
    // we simply can't see the playlist at all. Flag that explicitly rather
    // than let a misleadingly large "unused" count go unexplained.
    auto arr = project->getArrangement(0);
    if (arr.getPlaylistItems().empty() && !project->getPatterns().empty())
    {
        lines.add("");
        lines.add("NOTE: This project's playlist appears empty or unreadable - if this is an "
            "older FL Studio file, it may be using the legacy per-clip playlist format this "
            "parser doesn't support yet (see README). If so, the pattern-unused count above "
            "is unreliable: it may be flagging patterns that are actually used in the playlist, "
            "just in a format we can't currently see.");
    }

    return lines.joinIntoString("\n");
}

juce::String PluginProcessor::compareWithFile(const juce::File& otherFile)
{
    juce::ScopedLock lock(projectLock);
    if (!project) return "Load a project first before comparing.";
    if (!otherFile.existsAsFile()) return "File not found.";

    std::unique_ptr<FL::Project> otherProject;
    try
    {
        otherProject = FL::Project::load(otherFile);
    }
    catch (const std::exception&)
    {
        otherProject.reset();
    }
    if (!otherProject) return "Could not parse: " + otherFile.getFileName();

    auto diffs = FL::Comparer::compare(*project, *otherProject);

    juce::StringArray lines;
    lines.add("=== COMPARISON: current project vs. " + otherFile.getFileName() + " ===");
    if (diffs.empty())
    {
        lines.add("No differences found in the categories currently compared (tempo, channel presence).");
    }
    else
    {
        for (const auto& d : diffs)
            lines.add(d.category + " \"" + d.itemName + "\" - " + d.property + ": "
                + d.oldValue + " -> " + d.newValue);
    }
    return lines.joinIntoString("\n");
}

bool PluginProcessor::saveProjectAs(const juce::File& destFile)
{
    juce::ScopedLock lock(projectLock);
    if (!project) return false;
    try
    {
        project->save(destFile);
        return true;
    }
    catch (const std::exception&)
    {
        return false;
    }
}

juce::String PluginProcessor::exportFullDataAsJson(const juce::File& outputJsonFile)
{
    juce::ScopedLock lock(projectLock);
    if (!project) return "No project loaded.";

    auto json = FL::ProjectJsonExporter::exportToJson(*project);
    if (!outputJsonFile.replaceWithText(json))
        return "Failed to write JSON file - check that the destination folder is writable.";

    return "Exported full project data (" + juce::String(json.length()) + " characters) to:\n"
         + outputJsonFile.getFullPathName();
}

juce::String PluginProcessor::exportPatternsToMidi(const juce::File& outputMidiFile)
{
    juce::ScopedLock lock(projectLock);
    if (!project) return "No project loaded.";

    bool ok = FL::MidiBridge::exportProjectToMidi(*project, outputMidiFile);
    if (!ok)
        return "Failed to write MIDI file - check that the destination folder is writable.";

    auto patterns = project->getPatterns();
    int totalNotes = 0;
    for (auto& p : patterns) totalNotes += (int) p.getNotes().size();

    return "Exported " + juce::String(patterns.size()) + " pattern(s), "
         + juce::String(totalNotes) + " note(s) total, to:\n" + outputMidiFile.getFullPathName();
}

// =============================================================================
// Required JUCE plugin entry point.
// Every VST3/Standalone/etc. wrapper JUCE builds calls this exact function to
// create the one AudioProcessor instance for the plugin. Without it, the
// wrapper object files (include_juce_audio_plugin_client_VST3.obj,
// include_juce_audio_plugin_client_Standalone.obj, etc.) fail to link.
// =============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PluginProcessor();
}