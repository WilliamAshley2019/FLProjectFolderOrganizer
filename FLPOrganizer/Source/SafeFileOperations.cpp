#include "SafeFileOperations.h"

#if JUCE_WINDOWS
 #include <windows.h>
#endif

const juce::Array<juce::File>& SafeFileOperations::GetBlockedRoots()
{
    static juce::Array<juce::File> blocked = []
    {
        juce::Array<juce::File> roots;

        // Windows/System32 and the whole Windows tree -- the big one.
        roots.add(juce::File::getSpecialLocation(juce::File::windowsSystemDirectory));

        // Its parent is C:\Windows itself -- block the whole tree, not
        // just System32, since System, SysWOW64, WinSxS, etc. all live
        // alongside it and are equally dangerous to touch.
        auto windowsRoot = juce::File::getSpecialLocation(juce::File::windowsSystemDirectory)
            .getParentDirectory();
        if (windowsRoot.getFileName().equalsIgnoreCase("Windows"))
            roots.add(windowsRoot);

        // Program Files / Program Files (x86) -- installed application
        // binaries. We never want to move or delete anything living
        // here, even though FL Studio's own install folder is under
        // here too (that's read-only-scanned by FLInstallationScanner,
        // never move/delete territory).
        roots.add(juce::File::getSpecialLocation(juce::File::globalApplicationsDirectory));
        roots.add(juce::File::getSpecialLocation(juce::File::globalApplicationsDirectoryX86));

        // ProgramData -- shared app config/data, not user content.
        juce::File programData("C:\\ProgramData");
        if (programData.isDirectory())
            roots.add(programData);

        // System Volume Information, Recovery, $Recycle.Bin -- OS
        // reserved folders that appear at the root of every drive.
        // We can't enumerate every drive letter here generically, but
        // the directory-name check in IsBlockedDirectory below catches
        // these regardless of which drive they're on.

        return roots;
    }();

    return blocked;
}

bool SafeFileOperations::IsBlockedDirectory(const juce::File& file)
{
    // Name-based check first -- catches OS-reserved folder names on ANY
    // drive letter, not just C:\, since these reserved names appear at
    // the root of every fixed/removable drive Windows manages.
    static const juce::StringArray reservedNames = {
        "System Volume Information",
        "$Recycle.Bin",
        "Recovery",
        "Windows",
        "MSOCache"
    };

    // Walk up the full path checking every path component, not just the
    // immediate parent -- a file three folders deep inside
    // "D:\Whatever\Windows\..." (an unlikely but possible user-created
    // folder literally named "Windows" elsewhere) is a corner case we
    // deliberately accept some over-blocking risk on, since the cost of
    // a false positive (skip a legitimate folder) is far lower than the
    // cost of a false negative (touch a system folder).
    juce::File current = file;
    while (current.exists() || current.getParentDirectory() != current)
    {
        for (const auto& reserved : reservedNames)
        {
            if (current.getFileName().equalsIgnoreCase(reserved))
                return true;
        }

        auto parent = current.getParentDirectory();
        if (parent == current)
            break; // reached drive root
        current = parent;
    }

    // Explicit blocked roots (Windows, Program Files, ProgramData).
    for (const auto& blockedRoot : GetBlockedRoots())
    {
        if (file == blockedRoot || file.isAChildOf(blockedRoot))
            return true;
    }

    return false;
}

bool SafeFileOperations::IsProtectedFile(const juce::File& file)
{
    if (!file.existsAsFile())
        return false;

    // Read-only flag, and lack of write access as a broader catch-all
    // (covers cases where the read-only bit isn't set but permissions
    // still deny writing, e.g. certain OneDrive-synced or admin-owned
    // files).
    if (!file.hasWriteAccess())
        return true;

   #if JUCE_WINDOWS
    // Explicit FILE_ATTRIBUTE_SYSTEM / FILE_ATTRIBUTE_READONLY check via
    // Win32, since JUCE's hasWriteAccess() may not always reflect the
    // SYSTEM attribute specifically (a file can be writable by the
    // current user's permissions yet still be OS-marked as a system
    // file that shouldn't be moved).
    DWORD attrs = GetFileAttributesW(file.getFullPathName().toWideCharPointer());
    if (attrs != INVALID_FILE_ATTRIBUTES)
    {
        if ((attrs & FILE_ATTRIBUTE_SYSTEM) != 0)
            return true;
        if ((attrs & FILE_ATTRIBUTE_READONLY) != 0)
            return true;
    }
   #endif

    return false;
}

bool SafeFileOperations::RequiresProtectedFileConfirmation(const juce::File& file)
{
    return IsBlockedDirectory(file) || IsProtectedFile(file);
}

juce::String SafeFileOperations::GetProtectionReason(const juce::File& file)
{
    if (IsBlockedDirectory(file))
        return "Inside a protected system directory (Windows, Program Files, or reserved OS folder)";

    if (IsProtectedFile(file))
        return "File is marked read-only or system-protected by the OS";

    return {};
}

// ============================================================================
// Untrusted relative-path checks
// ============================================================================

namespace
{
    bool isReservedDeviceName(const juce::String& segment)
    {
        static const juce::StringArray reserved = {
            "CON", "PRN", "AUX", "NUL", "CLOCK$", "CONIN$", "CONOUT$",
            "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
            "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"
        };

        // Windows ignores any extension and trailing spaces when deciding
        // whether a name is a device: "CON.txt" and "CON " are both CON.
        juce::String stem = segment.upToFirstOccurrenceOf(".", false, false).trimEnd();
        return reserved.contains(stem, true);
    }

    // Returns an empty string if the segment is fine, otherwise the reason.
    juce::String segmentProblem(const juce::String& part)
    {
        if (part.isEmpty())                       return "Empty path segment";
        if (part == "." || part == "..")          return "Contains '.' or '..' segment";
        if (part.endsWithChar('.') || part.endsWithChar(' '))
                                                  return "Segment ends with a dot or space";
        if (part.containsAnyOf("<>:\"|?*"))       return "Contains an invalid character";
        for (auto c : part)
            if (c < 32)                           return "Contains a control character";
        if (isReservedDeviceName(part))           return "Windows reserved device name";
        return {};
    }
}

juce::String SafeFileOperations::GetSafeRelativePathRejectionReason(const juce::String& relativePath)
{
    if (relativePath.isEmpty())
        return "Empty path";
    if (relativePath.startsWithChar('/') || relativePath.startsWithChar('\\'))
        return "Absolute or UNC path";
    if (relativePath.length() >= 2 && relativePath[1] == ':'
        && juce::CharacterFunctions::isLetter(relativePath[0]))
        return "Drive-qualified path";

    juce::StringArray parts;
    parts.addTokens(relativePath, "/\\", "");

    // Trailing separators (zip directory entries such as "kit/") produce
    // one empty final token; that is fine. Empty tokens anywhere else
    // ("a//b") are rejected so two spellings can't name the same file.
    if (parts.size() > 0 && parts[parts.size() - 1].isEmpty())
        parts.remove(parts.size() - 1);

    if (parts.isEmpty())
        return "Empty path";

    for (const auto& part : parts)
    {
        auto problem = segmentProblem(part);
        if (problem.isNotEmpty())
            return problem;
    }

    return "OK";
}

bool SafeFileOperations::IsSafeRelativePath(const juce::String& relativePath)
{
    return GetSafeRelativePathRejectionReason(relativePath) == "OK";
}

#if JUCE_WINDOWS
namespace
{
    // Only real links count. Cloud-sync placeholders (OneDrive etc.) also
    // carry FILE_ATTRIBUTE_REPARSE_POINT but are ordinary files/folders for
    // our purposes, so check the reparse tag instead of the attribute alone.
    bool isWindowsLinkReparsePoint(const juce::File& f)
    {
        WIN32_FIND_DATAW fd {};
        HANDLE h = FindFirstFileW(f.getFullPathName().toWideCharPointer(), &fd);
        if (h == INVALID_HANDLE_VALUE)
            return false;
        FindClose(h);

        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0)
            return false;

        return fd.dwReserved0 == IO_REPARSE_TAG_SYMLINK
            || fd.dwReserved0 == IO_REPARSE_TAG_MOUNT_POINT;
    }
}
#endif

bool SafeFileOperations::HasSymlinkAncestor(const juce::File& file, const juce::File& stopAtRoot)
{
    const bool haveRoot = (stopAtRoot != juce::File());

    // Fail closed: if a root was given and the file isn't actually inside
    // it, we can't vouch for the path.
    if (haveRoot && file != stopAtRoot && !file.isAChildOf(stopAtRoot))
        return true;

    juce::File current = file;
    for (;;)
    {
        if (haveRoot && current == stopAtRoot)
            return false;

        if (current.isSymbolicLink())
            return true;

       #if JUCE_WINDOWS
        if (isWindowsLinkReparsePoint(current))
            return true;
       #endif

        auto parent = current.getParentDirectory();
        if (parent == current)
            return false; // reached the filesystem root
        current = parent;
    }
}
