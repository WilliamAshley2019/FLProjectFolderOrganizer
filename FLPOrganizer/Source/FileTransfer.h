#pragma once
#include <JuceHeader.h>
#include <atomic>
#include <functional>
#include <map>
#include "InstalledPluginLookup.h"

// Inspect, validate and copy FL content (loose files, folders or ZIP archives)
// into destination folders, with conflict handling that never silently destroys
// a file: replaced files go to the OS recycle bin first.
namespace FileTransfer
{
    struct Limits { int entries = 10000; juce::int64 bytes = 2LL * 1024 * 1024 * 1024; int ratio = 200; };

    // Per-file identity gathered during scan (keys are package-relative, '/'-separated).
    struct FileInfo
    {
        juce::String pluginName, pluginKind, fstType, displayName, hostedName, databaseName, error;
    };

    struct Package
    {
        juce::File source;
        juce::StringArray files;                 // relative paths of all files
        juce::StringArray directories;           // relative paths of all folders
        juce::StringArray skipped;               // things not imported, with reasons
        bool isZip = false;
        std::map<juce::String, FileInfo> info;

        // Identity for single-file packages and for FST groups.
        juce::String pluginName, pluginKind, fstType, displayName, hostedName, databaseName;
        juce::String groupName;
        bool isFstGroup = false;

        bool hasSelection = false;               // true => selectedFiles is authoritative
        juce::StringArray selectedFiles;
        juce::String zipRootFolder;              // single top-level folder inside a ZIP, if any
        juce::String destinationSubfolder;

        const juce::StringArray& plannedFiles() const { return hasSelection ? selectedFiles : files; }
        juce::String folderName() const { return destinationSubfolder.isNotEmpty() ? destinationSubfolder
                                                : (isZip ? source.getFileNameWithoutExtension() : source.getFileName()); }
        // A representative file name used to pick destinations for this package.
        juce::String routePath() const;
        juce::String pluginLabel() const;
    };

    struct Job { Package package; juce::File root, target; bool keepZip = false; };
    struct Result { int copied = 0, skipped = 0, cancelled = 0, failed = 0; juce::StringArray details; };

    // Returns "replace", "skip", "keep" (copy as "name (2).ext") or "cancel".
    using ConflictFn = std::function<juce::String (const juce::File& existing)>;
    using ProgressFn = std::function<void (const juce::String&)>;

    // Scan a file / folder / ZIP. `lookup` may be null (then FXP/H2P stay unidentified).
    bool scan (const juce::File& source, const Limits&, const std::atomic<bool>& cancel,
               InstalledPluginLookup* lookup, Package& out, juce::String& error);

    // Validates the ZIP without extracting. Returns "" when safe, else the reason.
    juce::String validateZip (const juce::File& zip, const Limits&, juce::StringArray* filesOut = nullptr,
                              juce::StringArray* dirsOut = nullptr, const std::atomic<bool>* cancel = nullptr);

    Result runJobs (std::vector<Job>& jobs, const Limits&, const std::atomic<bool>& cancel,
                    const ConflictFn&, const ProgressFn&);
}
