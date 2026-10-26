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

    // ---- Path-safety checks for untrusted relative paths (archive entries,
    // ---- package-relative names, preset names) -------------------------

    // True only if `relativePath` is safe to join onto a trusted root.
    // Rejects: empty input, leading '/' or '\' (including UNC), drive
    // letters ("C:"), any "." or ".." segment, control characters,
    // < > : " | ? *, segments ending in '.' or ' ', and Windows reserved
    // device names (CON, PRN, AUX, NUL, COM1-9, LPT1-9, CLOCK$, CONIN$,
    // CONOUT$ -- with or without an extension).
    static bool IsSafeRelativePath(const juce::String& relativePath);

    // Why IsSafeRelativePath() said no, for logs/UI. Returns "OK" if safe.
    static juce::String GetSafeRelativePathRejectionReason(const juce::String& relativePath);

    // True if `file`, or any ancestor of it, is a symlink / junction /
    // other reparse point. If `stopAtRoot` is given, only components BELOW
    // that root are examined (the root itself and everything above it are
    // trusted). That matters on Windows: Documents is often redirected
    // through OneDrive or a junction, and walking all the way to the
    // drive root would flag every write as unsafe. Always pass the
    // validated destination root when checking a write target.
    static bool HasSymlinkAncestor(const juce::File& file, const juce::File& stopAtRoot = {});

private:
    static const juce::Array<juce::File>& GetBlockedRoots();
};
