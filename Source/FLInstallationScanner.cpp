#include "FLInstallationScanner.h"
#include "Discovery.h"

#if JUCE_WINDOWS
 #include <windows.h>
 #include <tlhelp32.h>
#endif

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

    // Installs outside Program Files (custom drive/folder) are found via the registry.
    for (const auto& candidate : Discovery::registryCandidates(Discovery::Kind::FlInstall))
    {
        const auto folder = candidate.path;
        const bool known = std::any_of(results.begin(), results.end(), [&](const Installation& i)
            { return i.installFolder.getFullPathName().equalsIgnoreCase(folder.getFullPathName()); });
        if (known)
            continue;

        Installation install;
        install.folderName     = folder.getFileName();
        install.installFolder  = folder;
        install.executablePath = folder.getChildFile("FL64.exe");
        install.versionLabel   = folder.getFileName();
        install.majorVersion   = ExtractMajorVersionFromFolderName(install.folderName);
        install.registryConfirmed = true;
        if (install.executablePath.existsAsFile())
            results.push_back(install);
    }

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

#if JUCE_WINDOWS
namespace
{
    using LogFn = std::function<void(const juce::String&)>;

    // pids of running processes whose image path is exactly `exe` (case-insensitive)
    std::vector<DWORD> pidsRunningExe(const juce::File& exe)
    {
        std::vector<DWORD> pids;
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap == INVALID_HANDLE_VALUE)
            return pids;

        PROCESSENTRY32W pe {};
        pe.dwSize = sizeof(pe);
        const auto wanted = exe.getFullPathName();
        if (Process32FirstW(snap, &pe))
        {
            do
            {
                if (juce::String(pe.szExeFile).equalsIgnoreCase(exe.getFileName()))
                {
                    if (HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID))
                    {
                        wchar_t buf[MAX_PATH * 2] {};
                        DWORD len = (DWORD) std::size(buf);
                        if (QueryFullProcessImageNameW(h, 0, buf, &len) && juce::String(buf).equalsIgnoreCase(wanted))
                            pids.push_back(pe.th32ProcessID);
                        CloseHandle(h);
                    }
                }
            } while (Process32NextW(snap, &pe));
        }
        CloseHandle(snap);
        return pids;
    }

    // Starts `exe` with an optional project argument, working directory = the exe's own folder.
    // Returns the new process handle (caller closes it) or nullptr.
    HANDLE startExe(const juce::File& exe, const juce::File& project, DWORD* pidOut = nullptr)
    {
        juce::String cmd = "\"" + exe.getFullPathName() + "\"";
        if (project.existsAsFile())
            cmd += " \"" + project.getFullPathName() + "\"";

        std::wstring cmdLine(cmd.toWideCharPointer());     // CreateProcessW may modify the buffer
        const auto dir = exe.getParentDirectory().getFullPathName();

        STARTUPINFOW si {};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi {};
        if (! CreateProcessW(exe.getFullPathName().toWideCharPointer(), cmdLine.data(), nullptr, nullptr, FALSE,
                             0, nullptr, dir.toWideCharPointer(), &si, &pi))
            return nullptr;

        if (pidOut != nullptr) *pidOut = pi.dwProcessId;
        CloseHandle(pi.hThread);
        return pi.hProcess;
    }

    struct WindowProbe { DWORD pid; juce::String title; };

    BOOL CALLBACK probeWindow(HWND hwnd, LPARAM lp)
    {
        auto* probe = reinterpret_cast<WindowProbe*>(lp);
        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        if (pid != probe->pid || ! IsWindowVisible(hwnd) || GetWindow(hwnd, GW_OWNER) != nullptr)
            return TRUE;
        wchar_t title[512] {};
        if (GetWindowTextW(hwnd, title, (int) std::size(title)) > 0)
        {
            juce::String t(title);
            if (t.containsIgnoreCase("FL Studio") || t.containsIgnoreCase("FruityLoops"))
            {
                probe->title = t;
                return FALSE;           // found the main window
            }
        }
        return TRUE;
    }

    // Watches a freshly launched FL. FL only picks up a command-line project once it has
    // finished starting, and sometimes starts empty; if its title bar still doesn't show the
    // project after it has settled, send the project once more (an already-running FL takes
    // it directly). Owns itself via DeletedAtShutdown so it can't outlive the message thread.
    class LaunchWatcher : private juce::Timer, public juce::DeletedAtShutdown
    {
    public:
        LaunchWatcher(juce::File exeIn, juce::File projectIn, HANDLE proc, DWORD pidIn, LogFn logIn)
            : exe(std::move(exeIn)), project(std::move(projectIn)), process(proc), pid(pidIn), log(std::move(logIn))
        {
            startTimer(500);
        }
        ~LaunchWatcher() override { stopTimer(); if (process) CloseHandle(process); }

    private:
        void say(const juce::String& m) { if (log) log(m); }

        void finish() { stopTimer(); juce::MessageManager::callAsync([this] { delete this; }); }

        void timerCallback() override
        {
            ticks++;

            if (process != nullptr && WaitForSingleObject(process, 0) == WAIT_OBJECT_0 && ! sawWindow)
            {
                say("The new FL process exited without opening a window. FL is probably single-instance and "
                    "handed the project to a copy that was already running (check which FL Studio opened it).");
                return finish();
            }

            WindowProbe probe { pid, {} };
            EnumWindows(probeWindow, reinterpret_cast<LPARAM>(&probe));

            if (probe.title.isNotEmpty())
            {
                if (! sawWindow) { sawWindow = true; windowSeenAtTick = ticks; }
                lastTitle = probe.title;
            }

            if (! sawWindow)
            {
                if (ticks > 180) { say("Gave up waiting for FL's window (90 s)."); finish(); }
                return;
            }

            if (ticks - windowSeenAtTick < 10)      // let FL finish starting (~5 s) before judging
                return;

            const bool loaded = project.getFileNameWithoutExtension().isNotEmpty()
                             && lastTitle.containsIgnoreCase(project.getFileNameWithoutExtension());
            if (loaded)
                say("FL is showing \"" + lastTitle + "\" - project loaded.");
            else if (HANDLE h = startExe(exe, project))
            {
                CloseHandle(h);
                say("FL started without the project (title: \"" + lastTitle + "\"); sent it again.");
            }
            finish();
        }

        juce::File exe, project;
        HANDLE process;
        DWORD pid;
        LogFn log;
        int ticks = 0, windowSeenAtTick = 0;
        bool sawWindow = false;
        juce::String lastTitle;
    };
}
#endif

bool FLInstallationScanner::LaunchInstallation(const Installation& install, const juce::File& projectToOpen,
                                               std::function<void(const juce::String&)> log)
{
    if (!install.executablePath.existsAsFile())
        return false;

   #if JUCE_WINDOWS
    const auto running = pidsRunningExe(install.executablePath);

    DWORD pid = 0;
    HANDLE proc = startExe(install.executablePath, projectToOpen, &pid);
    if (proc == nullptr)
        return false;

    if (! running.empty())
    {
        // The same exe is already open: the new process just forwards the file to it and exits.
        if (log) log(install.versionLabel + " is already running; sent it the project.");
        CloseHandle(proc);
        return true;
    }

    if (log)
        log("Started " + install.versionLabel + " (pid " + juce::String((int) pid)
            + "); waiting for it to finish loading before checking the project.");

    if (projectToOpen.existsAsFile())
        new LaunchWatcher(install.executablePath, projectToOpen, proc, pid, std::move(log));   // takes ownership of proc
    else
        CloseHandle(proc);
    return true;
   #else
    juce::String params;
    if (projectToOpen.existsAsFile())
        params = "\"" + projectToOpen.getFullPathName() + "\"";
    return install.executablePath.startAsProcess(params);
   #endif
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
