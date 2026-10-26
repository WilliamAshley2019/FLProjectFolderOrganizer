#pragma once
#include <JuceHeader.h>
#include <atomic>

// Read-only discovery of FL Studio folders on Windows. Saved paths always win; these are
// only suggestions. Nothing here writes the registry or follows links.
namespace Discovery
{
    struct Candidate
    {
        juce::File path;
        juce::String source;      // human-readable origin, e.g. "HKCU\Software\Image-Line\Shared\Paths · Shared data · 64-bit"
        juce::File sharedRoot;    // the Image-Line parent when path is its "FL Studio" child (user data only)
    };

    enum class Kind { UserData, FlInstall };

    // Registry (HKCU/HKLM, 64- and 32-bit views): Shared\Paths first, then each existing
    // "FL Studio <n>" version key. Empty on other platforms.
    std::vector<Candidate> registryCandidates (Kind kind);

    // Conventional "<Program Files>\Image-Line\FL Studio*" folders that look like installs.
    std::vector<juce::File> installCandidates();

    // Registry + conventional installs, de-duplicated, newest-looking folder name first.
    std::vector<Candidate> installationSuggestions();

    // Registered user-data roots, else <Documents>\Image-Line[\FL Studio] folders that exist.
    std::vector<juce::File> userSuggestions (bool includeRegistry = true);

    // A folder is a usable install when it contains FL64.exe and a Plugins folder.
    bool looksLikeInstall (const juce::File&);
    // A folder is a usable user-data root when it has Settings plus Presets or Projects.
    bool looksLikeUserDataRoot (const juce::File&);

    struct PluginFolderSearch
    {
        std::map<juce::String, std::vector<Candidate>> matches;   // wanted name -> folders found
        juce::StringArray notes;
    };

    // Finds existing folders whose name equals one of `names` (case-insensitive), searching user data,
    // then shared data, then the install folder, without following links. Bounded by entry count and depth.
    PluginFolderSearch findPluginFolders (const std::map<juce::String, juce::String>& roots,   // "user_data"/"shared_data"/"fl_install"
                                          const juce::StringArray& names, const std::atomic<bool>& cancel,
                                          int maxEntries = 50000, int maxDepth = 10);
}
