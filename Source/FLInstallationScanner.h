#pragma once
#include <JuceHeader.h>

// Discovers FL Studio / FruityLoops installations on this machine.
//
// Primary discovery is filesystem-based: FL Studio installs to a
// predictable path (Program Files\Image-Line\FL Studio <version>\) with
// the main executable at FL64.exe (64-bit) or FL.exe (older/32-bit
// installs). This is the reliable signal -- the exact registry key
// layout Image-Line uses for install *paths* (as opposed to license
// registration, which lives under Image-Line\Registrations) isn't
// something we have verified documentation for, so we don't guess at it
// for primary discovery. The registry is only consulted as a secondary
// confirmation signal once a candidate install folder is found.
class FLInstallationScanner
{
public:
    struct Installation
    {
        juce::String versionLabel;   // e.g. "FL Studio 21", "FL Studio 12"
        juce::String folderName;     // raw folder name as found on disk
        juce::File   installFolder;  // full path to the install directory
        juce::File   executablePath; // full path to FL64.exe / FL.exe
        int          majorVersion = -1;
        bool         registryConfirmed = false; // true if a matching
                                                  // Image-Line registry
                                                  // entry was also found
    };

    FLInstallationScanner() = default;

    // Scans standard install locations across all fixed drives and
    // returns every FL Studio / FruityLoops installation found.
    // Synchronous -- this is a handful of directory listings, not a
    // full drive scan, so it's fast enough to call directly from the UI.
    std::vector<Installation> ScanForInstallations();

    // Launches FL Studio for a specific installation, optionally opening
    // a project file directly. Returns false if the executable can't be
    // found or launched.
    static bool LaunchInstallation(const Installation& install, const juce::File& projectToOpen = {});

    // Opens the install folder in Windows Explorer.
    static bool OpenInstallFolderInExplorer(const Installation& install);

    // Given a detected FLP major version number (from VersionDetector),
    // finds the best-matching installed FL Studio version to open it
    // with. FL Studio is backwards- but not forwards-compatible, so this
    // picks the closest installed version that is >= the project's major
    // version; if none qualifies, falls back to the newest installed
    // version (best-effort, may prompt an in-app upgrade dialog).
    static const Installation* FindBestMatch(const std::vector<Installation>& installations,
        int projectMajorVersion);

private:
    void ScanProgramFilesRoot(const juce::File& root, std::vector<Installation>& results);
    bool ConfirmViaRegistry(Installation& install);
    int  ExtractMajorVersionFromFolderName(const juce::String& folderName);
};
