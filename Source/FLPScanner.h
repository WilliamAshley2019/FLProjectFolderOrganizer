#pragma once
#include <JuceHeader.h>
#include "VersionDetector.h"
#include "ZIPHandler.h"
#include "ProjectDatabase.h"
#include "RecycleBinManager.h"

class FLPScanner : public juce::Thread
{
public:
    FLPScanner();
    ~FLPScanner() override;

    void Scan(const juce::File& sourceDir, const juce::File& destDir,
        bool includeZips, bool scanSubfolders, bool dryRun, bool deleteOriginalsAfterCopy = false);
    void StopScanning();

    std::function<void(const juce::String&)> onLog;
    std::function<void(float)> onProgress;
    std::function<void(const ProjectDatabase::ProjectEntry&)> onProjectFound;
    std::function<void(const juce::String&, const juce::String&)> onDuplicateFound;

    const std::vector<ProjectDatabase::ProjectEntry>& GetResults() const { return results; }
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

    bool stopScanning = false;
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