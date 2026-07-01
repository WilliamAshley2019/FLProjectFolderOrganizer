#pragma once
#include <JuceHeader.h>

// Centralized safety guard. Every delete and every move/organize
// operation anywhere in the app MUST check this before acting on a
// file, full stop. The goal is that no matter how a scan was started
// (even a mistaken "C:\" as the source folder), the app can never move,
// delete, or otherwise disturb Windows system files, files under
// Program Files, or files marked read-only/system by the OS.
class SafeFileOperations
{
public:
    // Returns true if this path is inside a hard-blocked system
    // directory tree. These directories are never entered during
    // scanning at all -- not just protected from deletion, but skipped
    // during the recursive folder walk in the first place, so scanning
    // "C:\" doesn't even descend into C:\Windows.
    static bool IsBlockedDirectory(const juce::File& file);

    // Returns true if the individual file itself is protected --
    // marked read-only or system by the OS, regardless of which folder
    // it's in. These files ARE shown to the user (e.g. if found inside
    // an otherwise-legitimate scan target), but routed to a separate
    // "Protected Files" review list rather than the normal result
    // table, and require explicit per-item or select-all confirmation
    // before any move/delete is attempted.
    static bool IsProtectedFile(const juce::File& file);

    // Combined check: true if this file should never be touched by an
    // automatic move/delete operation without the explicit protected-
    // file confirmation flow. This is what OrganizeProject/DeleteEntry/
    // etc. should call before acting.
    static bool RequiresProtectedFileConfirmation(const juce::File& file);

    // Human-readable reason, for logging/UI display, e.g.
    // "System-marked file" or "Inside protected directory: C:\Windows".
    static juce::String GetProtectionReason(const juce::File& file);

private:
    static const juce::Array<juce::File>& GetBlockedRoots();
};
