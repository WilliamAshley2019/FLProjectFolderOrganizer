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
