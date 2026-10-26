#pragma once
#include <JuceHeader.h>

// Finds the FL Studio *user data* folder (the one containing Presets, Projects,
// Audio, ...). It is not always <Documents>\Image-Line\FL Studio: users relocate
// it from FL's file settings, and OneDrive redirects Documents.
//
// Candidates are tried in order and the first one that actually looks like an FL
// user-data folder (has a "Presets" or "Projects" subfolder) wins:
//   1. explicit override (set from ConfigManager / a settings UI)
//   2. registry  Software\Image-Line\Shared\Paths, value "Shared data" (via Discovery),
//      then the per-version "FL Studio <n>" keys
//   3. <userDocumentsDirectory>\Image-Line\FL Studio   (resolves OneDrive redirection)
class UserDataLocator
{
public:
    static void setOverride (const juce::File& dir);
    static juce::File getOverride();

    // Returns {} if nothing plausible was found.
    static juce::File findUserDataFolder();

    static juce::File getPluginDatabaseRoot();   // <user data>\Presets\Plugin database
    static juce::File getInstalledPluginsFolder(); // ...\Plugin database\Installed

    // True if the folder has the shape of an FL user-data folder.
    static bool looksLikeUserData (const juce::File& dir);
};
