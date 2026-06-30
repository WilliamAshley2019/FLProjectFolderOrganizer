#include "FLInstallationScanner.h"

std::vector<FLInstallationScanner::Installation> FLInstallationScanner::ScanForInstallations()
{
    std::vector<Installation> results;

    // Check both the standard 64-bit Program Files and the x86 folder,
    // since older FL Studio versions (pre-64-bit builds) installed there.
    juce::Array<juce::File> rootsToCheck;

    rootsToCheck.add(juce::File::getSpecialLocation(juce::File::globalApplicationsDirectory)
        .getChildFile("Image-Line"));

    // Some systems also have a separate x86 Program Files
    juce::File programFilesX86 = juce::File::getSpecialLocation(juce::File::globalApplicationsDirectoryX86)
        .getChildFile("Image-Line");
    if (programFilesX86.isDirectory())
        rootsToCheck.add(programFilesX86);

    for (const auto& root : rootsToCheck)
        ScanProgramFilesRoot(root, results);

    // Sort newest-first so the UI can show the most relevant version up top.
    std::sort(results.begin(), results.end(),
        [](const Installation& a, const Installation& b)
        {
            return a.majorVersion > b.majorVersion;
        });

    return results;
}

void FLInstallationScanner::ScanProgramFilesRoot(const juce::File& root, std::vector<Installation>& results)
{
    if (!root.isDirectory())
        return;

    juce::Array<juce::File> subfolders;
    root.findChildFiles(subfolders, juce::File::findDirectories, false);

    for (const auto& folder : subfolders)
    {
        juce::String name = folder.getFileName();

        // Match "FL Studio 21", "FL Studio 12", "FruityLoops 3", etc.
        // We don't hard-code an exhaustive name list since Image-Line's
        // folder naming has shifted over the years -- instead we look
        // for the FL64.exe / FL.exe executable, which is the reliable
        // signal that this is an actual FL Studio install rather than
        // some other Image-Line product folder (Sytrus, Harmor, etc.
        // also live under Image-Line\).
        juce::File exe64 = folder.getChildFile("FL64.exe");
        juce::File exe32 = folder.getChildFile("FL.exe");

        juce::File foundExe;
        if (exe64.existsAsFile())
            foundExe = exe64;
        else if (exe32.existsAsFile())
            foundExe = exe32;
        else
            continue; // not an FL Studio install folder

        Installation install;
        install.folderName     = name;
        install.installFolder  = folder;
        install.executablePath = foundExe;
        install.versionLabel   = name; // e.g. "FL Studio 21"
        install.majorVersion   = ExtractMajorVersionFromFolderName(name);

        ConfirmViaRegistry(install);

        results.push_back(install);
    }
}

int FLInstallationScanner::ExtractMajorVersionFromFolderName(const juce::String& folderName)
{
    // Folder names look like "FL Studio 21", "FL Studio 12", "FL Studio 2024"
    // Pull the trailing number out.
    juce::String digits;
    for (int i = folderName.length() - 1; i >= 0; --i)
    {
        auto c = folderName[i];
        if (juce::CharacterFunctions::isDigit(c))
            digits = juce::String::charToString(c) + digits;
        else if (digits.isNotEmpty())
            break; // stop once we've collected a contiguous trailing number
    }

    return digits.isNotEmpty() ? digits.getIntValue() : -1;
}

bool FLInstallationScanner::ConfirmViaRegistry(Installation& install)
{
    // Secondary confirmation only -- we look for *any* Image-Line
    // registration key presence, which tells us Image-Line software has
    // been activated on this machine. This isn't a precise per-version
    // check (we don't have verified documentation of how Image-Line
    // structures per-version install registry keys), so a missing match
    // here does NOT mean the installation is invalid -- it just means we
    // couldn't independently confirm it. The filesystem check (finding
    // FL64.exe) remains the authoritative signal.
   #if JUCE_WINDOWS
    bool confirmed = juce::WindowsRegistry::keyExists(
        "HKEY_CURRENT_USER\\Software\\Image-Line\\Registrations");
    install.registryConfirmed = confirmed;
    return confirmed;
   #else
    install.registryConfirmed = false;
    return false;
   #endif
}

bool FLInstallationScanner::LaunchInstallation(const Installation& install, const juce::File& projectToOpen)
{
    if (!install.executablePath.existsAsFile())
        return false;

    juce::StringArray args;
    args.add(install.executablePath.getFullPathName());

    if (projectToOpen.existsAsFile())
        args.add(projectToOpen.getFullPathName());

    juce::ChildProcess process;
    return process.start(args);
}

bool FLInstallationScanner::OpenInstallFolderInExplorer(const Installation& install)
{
    if (!install.installFolder.isDirectory())
        return false;

    install.installFolder.startAsProcess();
    return true;
}

const FLInstallationScanner::Installation* FLInstallationScanner::FindBestMatch(
    const std::vector<Installation>& installations, int projectMajorVersion)
{
    if (installations.empty())
        return nullptr;

    const Installation* bestMatch = nullptr;

    // FL Studio is backwards-compatible (newer can open older) but not
    // forwards-compatible (older cannot open newer). So: find the
    // *oldest* installed version that is still >= the project's version
    // -- that's the most "native" match. If nothing qualifies (project
    // is newer than anything installed), fall back to the newest
    // available install as a best-effort attempt.
    for (const auto& install : installations)
    {
        if (install.majorVersion < 0)
            continue;

        if (install.majorVersion >= projectMajorVersion)
        {
            if (bestMatch == nullptr || install.majorVersion < bestMatch->majorVersion)
                bestMatch = &install;
        }
    }

    if (bestMatch != nullptr)
        return bestMatch;

    // Fallback: newest installed version (results are already sorted
    // newest-first by ScanForInstallations).
    return &installations.front();
}
