#pragma once
#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "flp.h"
#include <vector>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <algorithm>
#include <numeric>
#include <cmath>
#include <optional>

namespace FL
{
    // =========================================================================
    // 0. Plugin database lookup (read-only, best-effort)
    // =========================================================================
    // Thin facade over the shared InstalledPluginLookup singleton (which parses
    // FL's Installed\*.Plugins.ini once per process, off-thread if warmUp() was
    // called at startup). Kept so existing callers compile unchanged.
    class PluginDBLookup {
    public:
        struct Entry {
            juce::String vendorName;
            juce::String category;
            juce::String formatType; // "VST", "VST3", "CLAP", or "" if not derivable
        };
        // databaseRoot is honoured only if it names an existing directory
        // (reloads the shared lookup from there); otherwise the shared instance is used as-is.
        explicit PluginDBLookup(const juce::File& databaseRoot = {});
        std::optional<Entry> lookup(const juce::String& pluginName) const;
        bool isDatabaseAvailable() const;
    };

    // =========================================================================
    // 1. Plugin Usage Analyzer
    // =========================================================================
    struct PluginInfo {
        juce::String name, vendor;
        ChannelType type;
        int instanceCount = 0;
        bool isDemo = false, isNative = false;
    };

    class PluginInspector {
    public:
        explicit PluginInspector(const Project& p) : project(p) {}
        std::vector<PluginInfo> getPluginSummary() const;
    private:
        const Project& project;
        PluginDBLookup pluginDB; // best-effort; degrades to naming-convention heuristics if unavailable
    };

    // =========================================================================
    // 2. Sample File Collector
    // =========================================================================
    struct SampleFile {
        juce::String originalPath, resolvedPath, channelName;
        bool exists = false, isStock = false;
    };

    class SampleScanner {
    public:
        explicit SampleScanner(const Project& p) : project(p) {}
        std::vector<SampleFile> scan(const juce::File& flInstallDir = {});
        std::vector<SampleFile> getMissingFiles() const;
        int copySamplesTo(const juce::File& targetDir, bool copyStock = false);
    private:
        const Project& project;
        std::vector<SampleFile> files;
        juce::String resolvePath(const juce::String& raw, const juce::File& flDir) const;
    };

    // =========================================================================
    // 3. Arrangement Visualiser
    // =========================================================================
    struct ArrangementTrack {
        int index;
        juce::String name;
        juce::Colour color;
        bool isMuted;
        std::vector<PlaylistItem> clips;
    };

    class ArrangementDumper {
    public:
        explicit ArrangementDumper(const Project& p) : project(p) {}
        std::vector<ArrangementTrack> getTracks() const;
        juce::String generateTextReport() const;
    private:
        const Project& project;
        static juce::String formatTime(uint32_t ticks, int ppq);
    };

    // =========================================================================
    // 4. Project Comparer
    // =========================================================================
    struct DiffEntry {
        juce::String category, itemName, property, oldValue, newValue;
    };

    class Comparer {
    public:
        static std::vector<DiffEntry> compare(const Project& a, const Project& b);
    private:
        static void compareChannels(const Project& a, const Project& b, std::vector<DiffEntry>& diffs);
    };

    // =========================================================================
    // 5. Cleanup / Optimisation Tool
    // =========================================================================
    struct CleanupReport {
        int removedPatterns = 0, removedChannels = 0;
        juce::String toString() const {
            return "Removed " + juce::String(removedPatterns) + " patterns, " + 
                   juce::String(removedChannels) + " channels.";
        }
    };

    class Cleaner {
    public:
        static void removeUnusedPatterns(Project& project, CleanupReport& report);
        static void removeUnusedChannels(Project& project, CleanupReport& report);
        static void runAll(Project& project, CleanupReport& report);
    private:
        static std::unordered_set<int> getReferencedPatternIIDs(const Project& project);
        static std::unordered_set<int> getReferencedChannelIIDs(const Project& project);
    };

    // =========================================================================
    // 6. Batch Processor
    // =========================================================================
    class BatchProcessor {
    public:
        struct Options {
            bool renameTracksFromPatterns = false;
            double tempoScaleFactor = 1.0;
            bool makeSamplesRelative = false;
            juce::File baseDirectoryForRelative; 
            juce::String watermarkTitle, watermarkAuthor; 
        };

        static int processBatch(const juce::Array<juce::File>& inputFiles,
                                const juce::File& outputDirectory,
                                const Options& options,
                                std::function<void(int, int, const juce::String&)> progressCallback = nullptr);
        static bool processProject(Project& project, const Options& options);
    private:
        static void applyTempoScale(Project& project, double factor);
        static void renameTracks(Project& project);
        static void relativizeSamples(Project& project, const juce::File& baseDir);
        static void applyWatermark(Project& project, const juce::String& title, const juce::String& author);
    };

    // =========================================================================
    // 7. MIDI Exporter / Importer
    // =========================================================================
    class MidiBridge {
    public:
        static bool exportPatternToMidi(const Pattern& pattern, int projectPPQ, double tempoBPM, const juce::File& outputFile, int format = 1);
        static bool exportProjectToMidi(const Project& project, const juce::File& outputFile);
        static std::vector<Note> importMidiToNotes(const juce::File& midiFile, int targetChannelIID, int projectPPQ);
        static bool importMidiToPattern(Pattern& pattern, const juce::File& midiFile, int projectPPQ, int targetChannelIID = 0);
    private:
        static constexpr int kStandardMidiPPQ = 480;
        static int scaleTicksToMidi(int flTicks, int flPPQ);
        static int scaleTicksToFl(int midiTicks, int flPPQ);
        static uint8_t flKeyToMidiKey(uint16_t flKey);
        static uint16_t midiKeyToFlKey(uint8_t midiKey);
    };

    // =========================================================================
    // 8. Automation Curve Editor
    // =========================================================================
    // IMPORTANT: operates on AutomationEvent::Record (position/value/curveType),
    // NOT the legacy AutomationPoint list. AutomationEvent::writeItems() only
    // ever serializes `records` back to disk - `points` is a read-only
    // backward-compat snapshot populated once at parse time and never written
    // back. Editing `points` (as this class used to) is a silent no-op: the
    // UI shows the edit applied, but Save Project As writes the untouched
    // original data. Record::position is an absolute PPQ tick (not a delta
    // like AutomationPoint::beatIncrement), which also makes redundant-point
    // removal simpler - no running-sum reconstruction needed.
    class AutomationEditor {
    public:
        static void scalePoints(std::vector<AutomationEvent::Record>& points, double factor);
        static void invertPoints(std::vector<AutomationEvent::Record>& points, double maxValue = 1.0);
        static void smoothPoints(std::vector<AutomationEvent::Record>& points, int windowSize = 3);
        static int removeRedundantPoints(std::vector<AutomationEvent::Record>& points, double tolerance = 0.001);
        static bool applyToChannel(Channel& channel, std::function<void(std::vector<AutomationEvent::Record>&)> modifier);
        static std::vector<AutomationEvent::Record> generateFadeCurve(double startValue, double endValue, double totalBeats, int numPoints, int curveType = 0);
    };

    // =========================================================================
    // 9. Project Statistics Dashboard
    // =========================================================================
    struct ProjectStats {
        int totalChannels = 0, samplerChannels = 0, audioClipChannels = 0, nativePluginChannels = 0, automationChannels = 0;
        int totalPatterns = 0, totalNotes = 0, totalPatternControllers = 0;
        int totalTracks = 0, totalPlaylistItems = 0, totalAutomationPoints = 0;
        uint32_t maxPlaylistTick = 0;
        bool hasTempoAutomation = false;
        double initialTempo = 0.0, averageTempo = 0.0, durationBeats = 0.0, durationSeconds = 0.0;
        juce::String generateTextReport() const;
    };

    class StatsGenerator {
    public:
        static ProjectStats generate(const Project& project);
    private:
        static void analyzeChannels(const Project& project, ProjectStats& stats);
        static void analyzePatterns(const Project& project, ProjectStats& stats);
        static void analyzeArrangement(const Project& project, ProjectStats& stats);
        static void analyzeTempoAndDuration(const Project& project, ProjectStats& stats);
    };

    // =========================================================================
    // 10. Full structured JSON export
    // =========================================================================
    // Dumps everything Project already parses (channels, patterns/notes,
    // arrangement/tracks/playlist, mixer/inserts/slots) into one JSON
    // document, for use in external tools/scripts without going through FL
    // Studio at all. Deliberately just a serialization of the existing data
    // model - no new parsing, so it's exactly as reliable (and exactly as
    // limited, e.g. legacy playlist format) as everything else here.
    class ProjectJsonExporter {
    public:
        static juce::String exportToJson(const Project& project);
    };
}