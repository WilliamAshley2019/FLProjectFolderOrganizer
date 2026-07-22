#pragma once
#include <JuceHeader.h>

// Centralizes "safe delete" behaviour for the whole app.
// Instead of permanently deleting files, moves them into a dedicated
// recycle bin folder under the app's WAM data directory, preserving
// the original path as metadata so items could be restored later.
class RecycleBinManager
{
public:
    RecycleBinManager();

    // Returns the root recycle bin folder, creating it if needed.
    juce::File GetRecycleBinRoot();

    // Moves a file into the recycle bin instead of deleting it.
    // Returns true on success. The file is placed under a folder named
    // with a timestamp + original filename to avoid collisions, and a
    // sidecar .origin.txt records the original full path for potential
    // future restore functionality.
    bool SoftDelete(const juce::File& file);

    // Checks whether the target drive has at least `requiredBytes` free,
    // with an additional safety margin (default 5%) so operations don't
    // fail right at the edge of available space.
    static bool HasSufficientSpace(const juce::File& targetPath,
        int64_t requiredBytes,
        double safetyMarginPercent = 5.0);

    // Returns a human-readable free space string for UI display,
    // e.g. "42.3 GB free on D:\"
    static juce::String GetFreeSpaceDescription(const juce::File& targetPath);

private:
    juce::File recycleBinRoot;
};
