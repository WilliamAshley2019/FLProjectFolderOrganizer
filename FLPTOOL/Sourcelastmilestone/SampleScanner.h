#pragma once
#include <JuceHeader.h>
#include "SafeFileOperations.h"
#include "RecycleBinManager.h"

// Scans for audio sample files (.wav, .mp3, .flac, .ogg) -- a separate,
// simpler tool than FLPScanner, following the same safety patterns
// (blocked-directory-aware recursive walk, protected-file guard on any
// move/delete, atomic stop flag checked frequently, hashing via
// SHA-256 for duplicate detection).
//
// Unlike FLPScanner, this doesn't attempt version detection or
// FL-version-based folder organization -- samples don't have a
// meaningful "version" the way .flp project files do. Organizing here
// just means "move everything found into one destination folder,"
// optionally deduplicating by hash so re-running a scan doesn't create
// duplicate copies.
class SampleScanner : public juce::Thread
{
public:
    struct SampleEntry
    {
        juce::String path;
        juce::String fileName;
        juce::String extension; // "wav", "mp3", "flac", "ogg"
        juce::String hash;
        int64_t fileSize = 0;
        int64_t lastModified = 0;
    };

    struct ProtectedFileEntry
    {
        juce::String path;
        juce::String reason;
    };

    SampleScanner();
    ~SampleScanner() override;

    // extensionsToScan: which of wav/mp3/flac/ogg to include. Empty
    // means all four.
    void Scan(const juce::File& sourceDir, const juce::File& destDir,
        const juce::StringArray& extensionsToScan, bool scanSubfolders,
        bool dryRun, bool deleteOriginalsAfterCopy = false);
    void StopScanning();

    std::function<void(const juce::String&)> onLog;
    std::function<void(float)> onProgress;
    std::function<void(const SampleEntry&)> onSampleFound;
    std::function<void(const juce::String&, const juce::String&)> onDuplicateFound;
    std::function<void(const ProtectedFileEntry&)> onProtectedFileFound;

    const std::vector<SampleEntry>& GetResults() const { return results; }
    int GetTotalScanned() const { return totalScanned; }
    int GetTotalDuplicates() const { return duplicateCount; }

private:
    void run() override;
    void ProcessSample(const juce::File& file);
    void OrganizeSample(const SampleEntry& entry, const juce::File& destDir);
    void CheckForDuplicates(const SampleEntry& entry);
    juce::File GetUniquePath(const juce::File& basePath);
    juce::String CalculateFileHash(const juce::File& file);

    void FindFilesSkippingBlockedDirs(const juce::File& dir, bool recurse,
        const juce::StringArray& extensions, juce::Array<juce::File>& results);

    std::vector<ProtectedFileEntry> protectedFiles;

    std::atomic<bool> stopScanning { false };
    juce::StringArray extensions;
    bool scanSubfolders = true;
    bool isDryRun = true;
    bool deleteOriginals = false;
    juce::File sourceDirectory;
    juce::File destinationDirectory;

    std::vector<SampleEntry> results;
    std::map<juce::String, std::vector<juce::String>> hashMap;
    int totalScanned = 0;
    int duplicateCount = 0;

    RecycleBinManager recycleBin;

    void sendLog(const juce::String& msg);
    void sendProgress(float progress);
};
