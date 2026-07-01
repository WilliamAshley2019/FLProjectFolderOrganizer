#pragma once
#include <JuceHeader.h>
#include "VersionDetector.h"
#include "ZIPHandler.h"
#include "ProjectDatabase.h"
#include "RecycleBinManager.h"
#include "SafeFileOperations.h"

class FLPScanner : public juce::Thread
{
public:
    // A file found during scanning that we deliberately did NOT touch,
    // along with why. Surfaced to the UI as a separate "Protected
    // Files" review list requiring explicit confirmation before any
    // move/delete is attempted on it.
    struct ProtectedFileEntry
    {
        juce::String path;
        juce::String reason;
    };

    FLPScanner();
    ~FLPScanner() override;

    void Scan(const juce::File& sourceDir, const juce::File& destDir,
        bool includeZips, bool scanSubfolders, bool dryRun, bool deleteOriginalsAfterCopy = false);
    void StopScanning();

    std::function<void(const juce::String&)> onLog;
    std::function<void(float)> onProgress;
    std::function<void(const ProjectDatabase::ProjectEntry&)> onProjectFound;
    std::function<void(const juce::String&, const juce::String&)> onDuplicateFound;
    std::function<void(const ProtectedFileEntry&)> onProtectedFileFound;

    const std::vector<ProjectDatabase::ProjectEntry>& GetResults() const { return results; }
    const std::vector<ProtectedFileEntry>& GetProtectedFiles() const { return protectedFiles; }
    int GetTotalScanned() const { return totalScanned; }
    int GetTotalDuplicates() const { return duplicateCount; }

private:
    void run() override;
    void ProcessFLP(const juce::File& flpFile, const juce::File& sourceDir);
    void ProcessZIP(const juce::File& zipFile, const juce::File& sourceDir);
    juce::String CalculateFileHash(const juce::File& file);
    juce::String CalculateMemoryHash(const juce::MemoryBlock& data);
    void CheckForDuplicates(const ProjectDatabase::ProjectEntry& entry);
    void OrganizeProject(const ProjectDatabase::ProjectEntry& entry, const juce::File& destDir);
    juce::File GetUniquePath(const juce::File& basePath);
    bool VerifyCopy(const juce::File& source, const juce::File& dest);

    // Recursive folder walk that NEVER descends into directories flagged
    // by SafeFileOperations::IsBlockedDirectory -- this is what actually
    // keeps a scan started at "C:\" from ever touching C:\Windows,
    // Program Files, etc. Matches files against the given extension
    // list (case-insensitive, without the dot, e.g. {"flp", "zip"}).
    void FindFilesSkippingBlockedDirs(const juce::File& dir, bool recurse,
        const juce::StringArray& extensions, juce::Array<juce::File>& results);

    std::vector<ProtectedFileEntry> protectedFiles;

    std::atomic<bool> stopScanning { false };
    bool includeZips = true;
    bool scanSubfolders = true;
    bool isDryRun = true;
    bool deleteOriginals = false;
    juce::File sourceDirectory;
    juce::File destinationDirectory;

    std::vector<ProjectDatabase::ProjectEntry> results;
    std::map<juce::String, std::vector<juce::String>> hashMap; // hash -> paths
    int totalScanned = 0;
    int duplicateCount = 0;

    ProjectDatabase database;
    bool dbInitialized = false;
    RecycleBinManager recycleBin;

    void sendLog(const juce::String& msg);
    void sendProgress(float progress);
};