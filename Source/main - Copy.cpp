// FL Studio Project Organizer
// Version 0.1 - Prototype
// Single-file Win32 implementation

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shlobj.h>
#include <shellapi.h>
#include <filesystem>
#include <fstream>
#include <vector>
#include <string>
#include <map>
#include <set>
#include <thread>
#include <future>
#include <chrono>
#include <algorithm>
#include <iomanip>
#include <sstream>
#include <regex>
#include <cstdint>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "advapi32.lib")

namespace fs = std::filesystem;

// ============================================================================
// Application Constants
// ============================================================================

#define APP_NAME L"FL Studio Project Organizer"
#define APP_VERSION L"0.1"
#define WM_USER_SCAN_COMPLETE (WM_USER + 100)
#define WM_USER_PROGRESS_UPDATE (WM_USER + 101)
#define WM_USER_LOG_MESSAGE (WM_USER + 102)

// Version mapping table
struct VersionMapping {
    std::wregex pattern;
    std::wstring folderName;
    int majorVersion;
};

// ============================================================================
// Version Database
// ============================================================================

class VersionDatabase {
public:
    VersionDatabase() {
        // Initialize version mappings - ordered from newest to oldest
        mappings.push_back({ std::wregex(L"25\\.\\d+\\.\\d+\\.\\d+"), L"2025", 25 });
        mappings.push_back({ std::wregex(L"24\\.\\d+\\.\\d+\\.\\d+"), L"2024", 24 });
        mappings.push_back({ std::wregex(L"23\\.\\d+\\.\\d+\\.\\d+"), L"2023", 23 });
        mappings.push_back({ std::wregex(L"22\\.\\d+\\.\\d+\\.\\d+"), L"2022", 22 });
        mappings.push_back({ std::wregex(L"21\\.\\d+\\.\\d+\\.\\d+"), L"FL21", 21 });
        mappings.push_back({ std::wregex(L"20\\.\\d+\\.\\d+\\.\\d+"), L"FL20", 20 });
        mappings.push_back({ std::wregex(L"12\\.\\d+\\.\\d+"), L"FL12", 12 });
        mappings.push_back({ std::wregex(L"11\\.\\d+\\.\\d+"), L"FL11", 11 });
        mappings.push_back({ std::wregex(L"10\\.\\d+\\.\\d+"), L"FL10", 10 });
        mappings.push_back({ std::wregex(L"9\\.\\d+\\.\\d+"), L"FL9", 9 });
        mappings.push_back({ std::wregex(L"8\\.\\d+\\.\\d+"), L"FL8", 8 });
        mappings.push_back({ std::wregex(L"7\\.\\d+\\.\\d+"), L"FL7", 7 });
        mappings.push_back({ std::wregex(L"6\\.\\d+\\.\\d+"), L"FL6", 6 });
        mappings.push_back({ std::wregex(L"5\\.\\d+\\.\\d+"), L"FL5", 5 });
        mappings.push_back({ std::wregex(L"4\\.\\d+\\.\\d+"), L"FL4", 4 });
        mappings.push_back({ std::wregex(L"3\\.\\d+\\.\\d+"), L"FL3", 3 });
    }

    std::wstring GetVersionGroup(const std::wstring& versionString) const {
        for (const auto& mapping : mappings) {
            if (std::regex_search(versionString, mapping.pattern)) {
                return mapping.folderName;
            }
        }
        return L"Unknown";
    }

    int GetMajorVersion(const std::wstring& versionString) const {
        for (const auto& mapping : mappings) {
            if (std::regex_search(versionString, mapping.pattern)) {
                return mapping.majorVersion;
            }
        }
        return -1;
    }

private:
    std::vector<VersionMapping> mappings;
};

// ============================================================================
// Project Information Structure
// ============================================================================

struct ProjectInfo {
    fs::path sourcePath;           // Full path to .flp file
    fs::path projectFolder;        // Parent folder containing the project
    std::wstring projectName;      // Name of the project
    std::wstring versionString;    // Detected version (e.g., "25.2.4.5242")
    std::wstring versionGroup;     // Group folder name (e.g., "2025")
    int majorVersion;              // Numeric version
    uint64_t fileSize;             // Size in bytes
    FILETIME lastModified;         // Last modified time
    bool isZipped;                 // Whether from a ZIP archive
    std::vector<fs::path> files;   // All files in the project folder
    uint64_t totalSize;            // Total size of all files
};

// ============================================================================
// Global Application State
// ============================================================================

struct AppState {
    HWND hWnd = nullptr;
    HWND hListCtrl = nullptr;
    HWND hProgressBar = nullptr;
    HWND hStatusBar = nullptr;
    HWND hSourceEdit = nullptr;
    HWND hDestEdit = nullptr;
    HWND hScanBtn = nullptr;
    HWND hOrganizeBtn = nullptr;
    HWND hDryRunCheck = nullptr;
    HWND hCopyCheck = nullptr;
    HWND hDeleteCheck = nullptr;
    HWND hIncludeZipsCheck = nullptr;
    HWND hSubfoldersCheck = nullptr;

    std::vector<ProjectInfo> projects;
    std::vector<ProjectInfo> filteredProjects;
    VersionDatabase versionDb;

    bool isScanning = false;
    bool isOrganizing = false;
    bool dryRun = true;
    bool copyFiles = true;
    bool deleteOriginals = false;
    bool includeZips = true;
    bool scanSubfolders = true;

    std::wstring sourcePath;
    std::wstring destPath;
    std::wstring logBuffer;

    int scannedCount = 0;
    int totalCount = 0;

    std::future<void> scanFuture;
    std::future<void> organizeFuture;
};

AppState g_state;

// ============================================================================
// Utility Functions
// ============================================================================

std::wstring GetLastErrorString() {
    DWORD error = GetLastError();
    if (error == 0) return L"";

    LPWSTR messageBuffer = nullptr;
    size_t size = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL, error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPWSTR)&messageBuffer, 0, NULL
    );

    std::wstring message(messageBuffer, size);
    LocalFree(messageBuffer);
    return message;
}

std::wstring FormatFileTime(const FILETIME& ft) {
    SYSTEMTIME st;
    FileTimeToSystemTime(&ft, &st);

    wchar_t buffer[64];
    swprintf_s(buffer, 64, L"%04d-%02d-%02d %02d:%02d:%02d",
        st.wYear, st.wMonth, st.wDay,
        st.wHour, st.wMinute, st.wSecond);

    return std::wstring(buffer);
}

std::wstring FormatSize(uint64_t bytes) {
    const wchar_t* units[] = { L"B", L"KB", L"MB", L"GB", L"TB" };
    int unitIndex = 0;
    double size = (double)bytes;

    while (size >= 1024.0 && unitIndex < 4) {
        size /= 1024.0;
        unitIndex++;
    }

    wchar_t buffer[64];
    if (unitIndex == 0) {
        swprintf_s(buffer, 64, L"%.0f %s", size, units[unitIndex]);
    }
    else {
        swprintf_s(buffer, 64, L"%.1f %s", size, units[unitIndex]);
    }

    return std::wstring(buffer);
}

std::wstring StringToWide(const std::string& str) {
    if (str.empty()) return L"";
    int size = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
    std::wstring wstr(size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &wstr[0], size);
    wstr.pop_back(); // Remove null terminator
    return wstr;
}

std::string WideToString(const std::wstring& wstr) {
    if (wstr.empty()) return "";
    int size = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string str(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &str[0], size, nullptr, nullptr);
    str.pop_back(); // Remove null terminator
    return str;
}

// ============================================================================
// FLP Parser
// ============================================================================

std::wstring ExtractVersionFromFLP(const fs::path& filePath) {
    try {
        std::ifstream file(filePath, std::ios::binary);
        if (!file.is_open()) return L"";

        // Read first 16KB - enough to find version info
        const size_t BUFFER_SIZE = 16384;
        std::vector<char> buffer(BUFFER_SIZE);
        file.read(buffer.data(), BUFFER_SIZE);
        size_t bytesRead = file.gcount();

        if (bytesRead < 10) return L"";

        // Look for the FLhd marker and version pattern
        // Version appears after Ç character in FL Studio 20+ 
        // Older versions have different patterns

        std::string data(buffer.data(), bytesRead);

        // Try to find version pattern: digit.digit.digit[.digit]
        std::regex versionPattern(R"((\d+)\.(\d+)\.(\d+)(?:\.(\d+))?)");
        std::smatch matches;

        // Search in a sliding window
        if (std::regex_search(data, matches, versionPattern)) {
            // Verify it looks like a valid FL version
            std::string version = matches[0];

            // Additional verification: version should be near the start
            size_t pos = matches.position(0);
            if (pos < 4096) { // Within first 4KB
                // Check for FLhd marker nearby
                std::regex flhdPattern("FLhd");
                if (std::regex_search(data.substr(0, std::min<size_t>(1024, data.size())), flhdPattern)) {
                    return StringToWide(version);
                }

                // Also check for FLdt marker
                std::regex fldtPattern("FLdt");
                if (std::regex_search(data.substr(0, std::min<size_t>(1024, data.size())), fldtPattern)) {
                    return StringToWide(version);
                }
            }
        }

        // Try alternative pattern for older versions
        std::regex oldVersionPattern(R"((\d+)\.(\d+)\.(\d+))");
        if (std::regex_search(data, matches, oldVersionPattern)) {
            std::string version = matches[0];
            // Verify it's a plausible FL version (3.0.0 to 9.9.9)
            int major = std::stoi(matches[1]);
            if (major >= 3 && major <= 9) {
                return StringToWide(version);
            }
        }

        return L"";

    }
    catch (const std::exception& e) {
        return L"";
    }
}

std::wstring ExtractVersionFromZIP(const fs::path& zipPath) {
    // For ZIP files, we'd need to read the FLP inside without extracting
    // This is a placeholder - full ZIP support would require a ZIP library
    return L"";
}

// ============================================================================
// Project Scanner
// ============================================================================

class ProjectScanner {
public:
    ProjectScanner() : m_stopScanning(false) {}

    void Scan(const fs::path& sourcePath, bool scanSubfolders, bool includeZips) {
        m_projects.clear();
        m_totalFiles = 0;
        m_scannedFiles = 0;
        m_stopScanning = false;

        std::vector<fs::path> flpFiles;

        // Gather all .flp files
        if (scanSubfolders) {
            for (const auto& entry : fs::recursive_directory_iterator(sourcePath)) {
                if (m_stopScanning) break;
                if (entry.is_regular_file()) {
                    auto ext = entry.path().extension().wstring();
                    std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);

                    if (ext == L".flp") {
                        flpFiles.push_back(entry.path());
                    }
                    else if (includeZips && ext == L".zip") {
                        // Handle ZIP files - would need ZIP library
                        // For now, we'll just log them
                        LogMessage(L"ZIP file found: " + entry.path().filename().wstring() + L" (ZIP support coming soon)");
                    }
                }
            }
        }
        else {
            for (const auto& entry : fs::directory_iterator(sourcePath)) {
                if (m_stopScanning) break;
                if (entry.is_regular_file() && entry.path().extension() == L".flp") {
                    flpFiles.push_back(entry.path());
                }
            }
        }

        m_totalFiles = flpFiles.size();

        for (const auto& flpPath : flpFiles) {
            if (m_stopScanning) break;

            try {
                ProcessFLP(flpPath);
                m_scannedFiles++;

                // Update progress
                int progress = (m_scannedFiles * 100) / m_totalFiles;
                PostMessage(g_state.hWnd, WM_USER_PROGRESS_UPDATE, progress, 0);

            }
            catch (const std::exception& e) {
                LogMessage(L"Error processing: " + flpPath.filename().wstring() + L" - " + StringToWide(e.what()));
            }
        }

        PostMessage(g_state.hWnd, WM_USER_SCAN_COMPLETE, 0, 0);
    }

    const std::vector<ProjectInfo>& GetProjects() const { return m_projects; }
    void StopScanning() { m_stopScanning = true; }

private:
    void ProcessFLP(const fs::path& flpPath) {
        ProjectInfo info;
        info.sourcePath = flpPath;
        info.projectFolder = flpPath.parent_path();
        info.projectName = flpPath.stem().wstring();
        info.fileSize = fs::file_size(flpPath);
        info.isZipped = false;
        info.totalSize = info.fileSize;

        // Get last modified time
        auto ftime = fs::last_write_time(flpPath);
        // Convert to FILETIME (simplified for now)
        // In practice, we'd use proper time conversion

        // Extract version
        info.versionString = ExtractVersionFromFLP(flpPath);
        if (!info.versionString.empty()) {
            info.versionGroup = g_state.versionDb.GetVersionGroup(info.versionString);
            info.majorVersion = g_state.versionDb.GetMajorVersion(info.versionString);
        }
        else {
            info.versionGroup = L"Unknown";
            info.majorVersion = -1;
        }

        // Gather all files in the project folder
        try {
            for (const auto& entry : fs::directory_iterator(info.projectFolder)) {
                if (entry.is_regular_file()) {
                    info.files.push_back(entry.path());
                    info.totalSize += fs::file_size(entry.path());
                }
            }
        }
        catch (const std::exception&) {
            // Ignore directory iteration errors
        }

        m_projects.push_back(info);
    }

    void LogMessage(const std::wstring& msg) {
        PostMessage(g_state.hWnd, WM_USER_LOG_MESSAGE, 0, (LPARAM)new std::wstring(msg));
    }

    std::vector<ProjectInfo> m_projects;
    int m_totalFiles = 0;
    int m_scannedFiles = 0;
    bool m_stopScanning = false;
};

// ============================================================================
// Project Organizer
// ============================================================================

class ProjectOrganizer {
public:
    ProjectOrganizer() : m_stopOrganizing(false) {}

    void Organize(const std::vector<ProjectInfo>& projects,
        const fs::path& destPath,
        bool dryRun, bool copyFiles, bool deleteOriginals) {

        m_stopOrganizing = false;
        m_successCount = 0;
        m_failCount = 0;
        m_skippedCount = 0;

        int total = projects.size();
        int processed = 0;

        for (const auto& project : projects) {
            if (m_stopOrganizing) break;

            try {
                ProcessProject(project, destPath, dryRun, copyFiles, deleteOriginals);
                processed++;

                int progress = (processed * 100) / total;
                PostMessage(g_state.hWnd, WM_USER_PROGRESS_UPDATE, progress, 0);

            }
            catch (const std::exception& e) {
                m_failCount++;
                LogMessage(L"Error organizing: " + project.projectName + L" - " + StringToWide(e.what()));
            }
        }

        PostMessage(g_state.hWnd, WM_USER_SCAN_COMPLETE, 1, 0);
    }

    void StopOrganizing() { m_stopOrganizing = true; }

private:
    void ProcessProject(const ProjectInfo& project,
        const fs::path& destPath,
        bool dryRun, bool copyFiles, bool deleteOriginals) {

        // Determine destination folder
        fs::path versionFolder = destPath / project.versionGroup;
        fs::path destProjectFolder = versionFolder / project.projectFolder.filename();

        // Check if destination already exists
        if (fs::exists(destProjectFolder) && !dryRun) {
            // Handle existing folder - create unique name
            fs::path uniquePath = GetUniquePath(destProjectFolder);
            if (uniquePath.empty()) {
                LogMessage(L"Could not create unique path for: " + project.projectName);
                m_skippedCount++;
                return;
            }
            destProjectFolder = uniquePath;
        }

        if (dryRun) {
            LogMessage(L"[DRY RUN] Would move: " + project.projectName +
                L" -> " + destProjectFolder.wstring());
            m_successCount++;
            return;
        }

        // Create version folder if it doesn't exist
        if (!fs::exists(versionFolder)) {
            if (!fs::create_directories(versionFolder)) {
                LogMessage(L"Failed to create folder: " + versionFolder.wstring());
                m_failCount++;
                return;
            }
        }

        if (copyFiles) {
            // Copy project folder
            CopyDirectory(project.projectFolder, destProjectFolder);

            // Verify copy
            if (VerifyCopy(project.projectFolder, destProjectFolder)) {
                LogMessage(L"Copied: " + project.projectName +
                    L" -> " + destProjectFolder.wstring());

                if (deleteOriginals) {
                    fs::remove_all(project.projectFolder);
                    LogMessage(L"Deleted original: " + project.projectFolder.wstring());
                }

                m_successCount++;
            }
            else {
                LogMessage(L"Verification failed for: " + project.projectName);
                m_failCount++;
            }
        }
        else {
            // Move operation
            fs::rename(project.projectFolder, destProjectFolder);
            LogMessage(L"Moved: " + project.projectName +
                L" -> " + destProjectFolder.wstring());
            m_successCount++;
        }
    }

    fs::path GetUniquePath(const fs::path& basePath) {
        fs::path parent = basePath.parent_path();
        fs::path stem = basePath.stem();
        fs::path ext = basePath.extension();

        for (int i = 2; i < 1000; i++) {
            fs::path candidate = parent / (stem.wstring() + L" (" + std::to_wstring(i) + L")" + ext.wstring());
            if (!fs::exists(candidate)) {
                return candidate;
            }
        }
        return fs::path();
    }

    void CopyDirectory(const fs::path& source, const fs::path& dest) {
        if (!fs::exists(dest)) {
            fs::create_directories(dest);
        }

        for (const auto& entry : fs::directory_iterator(source)) {
            if (m_stopOrganizing) break;

            fs::path destPath = dest / entry.path().filename();

            if (fs::is_directory(entry.path())) {
                CopyDirectory(entry.path(), destPath);
            }
            else if (fs::is_regular_file(entry.path())) {
                fs::copy_file(entry.path(), destPath, fs::copy_options::overwrite_existing);
            }
        }
    }

    bool VerifyCopy(const fs::path& source, const fs::path& dest) {
        try {
            // Compare file counts
            size_t sourceCount = 0, destCount = 0;

            for (const auto& entry : fs::recursive_directory_iterator(source)) {
                if (fs::is_regular_file(entry)) sourceCount++;
            }

            for (const auto& entry : fs::recursive_directory_iterator(dest)) {
                if (fs::is_regular_file(entry)) destCount++;
            }

            if (sourceCount != destCount) {
                LogMessage(L"File count mismatch: " +
                    std::to_wstring(sourceCount) + L" vs " +
                    std::to_wstring(destCount));
                return false;
            }

            // Compare sizes (simplified verification)
            uint64_t sourceSize = 0, destSize = 0;

            for (const auto& entry : fs::recursive_directory_iterator(source)) {
                if (fs::is_regular_file(entry)) {
                    sourceSize += fs::file_size(entry);
                }
            }

            for (const auto& entry : fs::recursive_directory_iterator(dest)) {
                if (fs::is_regular_file(entry)) {
                    destSize += fs::file_size(entry);
                }
            }

            if (sourceSize != destSize) {
                LogMessage(L"Size mismatch: " +
                    FormatSize(sourceSize) + L" vs " +
                    FormatSize(destSize));
                return false;
            }

            return true;

        }
        catch (const std::exception& e) {
            LogMessage(L"Verification error: " + StringToWide(e.what()));
            return false;
        }
    }

    void LogMessage(const std::wstring& msg) {
        PostMessage(g_state.hWnd, WM_USER_LOG_MESSAGE, 0, (LPARAM)new std::wstring(msg));
    }

    bool m_stopOrganizing = false;
    int m_successCount = 0;
    int m_failCount = 0;
    int m_skippedCount = 0;
};

// ============================================================================
// Windows UI Functions
// ============================================================================

void AddLogMessage(const std::wstring& msg) {
    g_state.logBuffer += msg + L"\r\n";
    SetWindowText(g_state.hStatusBar, msg.c_str());
}

void UpdateListView() {
    SendMessage(g_state.hListCtrl, LVM_DELETEALLITEMS, 0, 0);

    for (const auto& project : g_state.projects) {
        LVITEMW item = { 0 };
        item.mask = LVIF_TEXT;
        item.iItem = g_state.filteredProjects.size();
        item.iSubItem = 0;
        item.pszText = const_cast<LPWSTR>(project.projectName.c_str());

        int index = (int)SendMessage(g_state.hListCtrl, LVM_INSERTITEMW, 0, (LPARAM)&item);
        if (index >= 0) {
            // Version string
            SendMessage(g_state.hListCtrl, LVM_SETITEMTEXT, index,
                (LPARAM)(new LVITEMW{ LVIF_TEXT, index, 1, 0, 0,
                const_cast<LPWSTR>(project.versionString.c_str()), 0, 0, 0, 0 }));

            // Version group
            SendMessage(g_state.hListCtrl, LVM_SETITEMTEXT, index,
                (LPARAM)(new LVITEMW{ LVIF_TEXT, index, 2, 0, 0,
                const_cast<LPWSTR>(project.versionGroup.c_str()), 0, 0, 0, 0 }));

            // Size
            std::wstring sizeStr = FormatSize(project.totalSize);
            SendMessage(g_state.hListCtrl, LVM_SETITEMTEXT, index,
                (LPARAM)(new LVITEMW{ LVIF_TEXT, index, 3, 0, 0,
                const_cast<LPWSTR>(sizeStr.c_str()), 0, 0, 0, 0 }));
        }
    }
}

void EnableUI(bool enable) {
    EnableWindow(g_state.hSourceEdit, enable);
    EnableWindow(g_state.hDestEdit, enable);
    EnableWindow(g_state.hScanBtn, enable);
    EnableWindow(g_state.hOrganizeBtn, enable);
    EnableWindow(g_state.hDryRunCheck, enable);
    EnableWindow(g_state.hCopyCheck, enable);
    EnableWindow(g_state.hDeleteCheck, enable);
    EnableWindow(g_state.hIncludeZipsCheck, enable);
    EnableWindow(g_state.hSubfoldersCheck, enable);
}

// ============================================================================
// Dialog Procedures
// ============================================================================

INT_PTR CALLBACK MainDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_INITDIALOG: {
        g_state.hWnd = hDlg;

        // Get control handles
        g_state.hSourceEdit = GetDlgItem(hDlg, IDC_SOURCE_EDIT);
        g_state.hDestEdit = GetDlgItem(hDlg, IDC_DEST_EDIT);
        g_state.hListCtrl = GetDlgItem(hDlg, IDC_PROJECT_LIST);
        g_state.hProgressBar = GetDlgItem(hDlg, IDC_PROGRESS);
        g_state.hStatusBar = GetDlgItem(hDlg, IDC_STATUS);
        g_state.hScanBtn = GetDlgItem(hDlg, IDC_SCAN_BTN);
        g_state.hOrganizeBtn = GetDlgItem(hDlg, IDC_ORGANIZE_BTN);
        g_state.hDryRunCheck = GetDlgItem(hDlg, IDC_DRYRUN_CHECK);
        g_state.hCopyCheck = GetDlgItem(hDlg, IDC_COPY_CHECK);
        g_state.hDeleteCheck = GetDlgItem(hDlg, IDC_DELETE_CHECK);
        g_state.hIncludeZipsCheck = GetDlgItem(hDlg, IDC_ZIPS_CHECK);
        g_state.hSubfoldersCheck = GetDlgItem(hDlg, IDC_SUBFOLDERS_CHECK);

        // Set default values
        CheckDlgButton(hDlg, IDC_DRYRUN_CHECK, BST_CHECKED);
        CheckDlgButton(hDlg, IDC_COPY_CHECK, BST_CHECKED);
        CheckDlgButton(hDlg, IDC_SUBFOLDERS_CHECK, BST_CHECKED);
        CheckDlgButton(hDlg, IDC_ZIPS_CHECK, BST_CHECKED);

        g_state.dryRun = true;
        g_state.copyFiles = true;
        g_state.scanSubfolders = true;
        g_state.includeZips = true;

        // Set default paths
        wchar_t docsPath[MAX_PATH];
        if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_PERSONAL, NULL, 0, docsPath))) {
            std::wstring defaultPath = std::wstring(docsPath) + L"\\Image-Line\\FL Studio\\Projects";
            SetWindowTextW(g_state.hSourceEdit, defaultPath.c_str());
            SetWindowTextW(g_state.hDestEdit, defaultPath.c_str());
        }

        // Setup list control
        DWORD style = GetWindowLong(g_state.hListCtrl, GWL_STYLE);
        SetWindowLong(g_state.hListCtrl, GWL_STYLE, style | LVS_REPORT | LVS_SINGLESEL);

        LVCOLUMNW col = { 0 };
        col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;

        col.pszText = L"Project Name";
        col.cx = 200;
        col.iSubItem = 0;
        SendMessage(g_state.hListCtrl, LVM_INSERTCOLUMNW, 0, (LPARAM)&col);

        col.pszText = L"Version";
        col.cx = 150;
        col.iSubItem = 1;
        SendMessage(g_state.hListCtrl, LVM_INSERTCOLUMNW, 1, (LPARAM)&col);

        col.pszText = L"Group";
        col.cx = 100;
        col.iSubItem = 2;
        SendMessage(g_state.hListCtrl, LVM_INSERTCOLUMNW, 2, (LPARAM)&col);

        col.pszText = L"Size";
        col.cx = 100;
        col.iSubItem = 3;
        SendMessage(g_state.hListCtrl, LVM_INSERTCOLUMNW, 3, (LPARAM)&col);

        // Set progress bar range
        SendMessage(g_state.hProgressBar, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
        SendMessage(g_state.hProgressBar, PBM_SETPOS, 0, 0);

        SetWindowTextW(hDlg, APP_NAME);
        EnableUI(true);
        return TRUE;
    }

    case WM_COMMAND: {
        switch (LOWORD(wParam)) {
        case IDC_SCAN_BTN: {
            if (g_state.isScanning) {
                // Stop scanning
                g_state.isScanning = false;
                // Note: Would need to stop the scanner thread
                EnableUI(true);
                SetWindowText(g_state.hScanBtn, L"Scan");
                return TRUE;
            }

            // Get source path
            wchar_t sourcePath[MAX_PATH];
            GetWindowTextW(g_state.hSourceEdit, sourcePath, MAX_PATH);
            g_state.sourcePath = sourcePath;

            if (g_state.sourcePath.empty()) {
                MessageBoxW(hDlg, L"Please select a source folder.", L"Error", MB_OK | MB_ICONERROR);
                return TRUE;
            }

            if (!fs::exists(g_state.sourcePath)) {
                MessageBoxW(hDlg, L"Source folder does not exist.", L"Error", MB_OK | MB_ICONERROR);
                return TRUE;
            }

            // Clear previous results
            g_state.projects.clear();
            g_state.filteredProjects.clear();
            SendMessage(g_state.hListCtrl, LVM_DELETEALLITEMS, 0, 0);
            SetWindowText(g_state.hStatusBar, L"Scanning...");
            SendMessage(g_state.hProgressBar, PBM_SETPOS, 0, 0);

            // Start scanning
            EnableUI(false);
            g_state.isScanning = true;
            SetWindowText(g_state.hScanBtn, L"Stop");

            // Launch scanner in background
            g_state.scanFuture = std::async(std::launch::async, [sourcePath]() {
                ProjectScanner scanner;
                scanner.Scan(sourcePath, true, true);
                g_state.projects = scanner.GetProjects();
                });

            return TRUE;
        }

        case IDC_ORGANIZE_BTN: {
            if (g_state.isOrganizing) {
                g_state.isOrganizing = false;
                EnableUI(true);
                SetWindowText(g_state.hOrganizeBtn, L"Organize");
                return TRUE;
            }

            if (g_state.projects.empty()) {
                MessageBoxW(hDlg, L"No projects to organize. Please scan first.", L"Error", MB_OK | MB_ICONERROR);
                return TRUE;
            }

            // Get destination path
            wchar_t destPath[MAX_PATH];
            GetWindowTextW(g_state.hDestEdit, destPath, MAX_PATH);
            g_state.destPath = destPath;

            if (g_state.destPath.empty()) {
                MessageBoxW(hDlg, L"Please select a destination folder.", L"Error", MB_OK | MB_ICONERROR);
                return TRUE;
            }

            // Get options
            g_state.dryRun = (IsDlgButtonChecked(hDlg, IDC_DRYRUN_CHECK) == BST_CHECKED);
            g_state.copyFiles = (IsDlgButtonChecked(hDlg, IDC_COPY_CHECK) == BST_CHECKED);
            g_state.deleteOriginals = (IsDlgButtonChecked(hDlg, IDC_DELETE_CHECK) == BST_CHECKED);

            // Confirm destructive operations
            if (!g_state.dryRun && g_state.deleteOriginals) {
                int result = MessageBoxW(hDlg,
                    L"WARNING: You are about to delete original files after copying.\n"
                    L"This operation cannot be undone.\n\n"
                    L"Are you sure you want to continue?",
                    L"Confirm Destructive Operation", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2);

                if (result != IDYES) {
                    return TRUE;
                }
            }

            EnableUI(false);
            g_state.isOrganizing = true;
            SetWindowText(g_state.hOrganizeBtn, L"Stop");
            SetWindowText(g_state.hStatusBar, L"Organizing...");
            SendMessage(g_state.hProgressBar, PBM_SETPOS, 0, 0);

            // Launch organizer in background
            g_state.organizeFuture = std::async(std::launch::async, []() {
                ProjectOrganizer organizer;
                organizer.Organize(g_state.projects, g_state.destPath,
                    g_state.dryRun, g_state.copyFiles, g_state.deleteOriginals);
                });

            return TRUE;
        }

        case IDC_BROWSE_SOURCE: {
            BROWSEINFOW bi = { 0 };
            bi.hwndOwner = hDlg;
            bi.lpszTitle = L"Select Source Folder";
            bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;

            LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
            if (pidl) {
                wchar_t path[MAX_PATH];
                if (SHGetPathFromIDListW(pidl, path)) {
                    SetWindowTextW(g_state.hSourceEdit, path);
                }
                CoTaskMemFree(pidl);
            }
            return TRUE;
        }

        case IDC_BROWSE_DEST: {
            BROWSEINFOW bi = { 0 };
            bi.hwndOwner = hDlg;
            bi.lpszTitle = L"Select Destination Folder";
            bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;

            LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
            if (pidl) {
                wchar_t path[MAX_PATH];
                if (SHGetPathFromIDListW(pidl, path)) {
                    SetWindowTextW(g_state.hDestEdit, path);
                }
                CoTaskMemFree(pidl);
            }
            return TRUE;
        }

        case IDC_DRYRUN_CHECK:
            g_state.dryRun = (IsDlgButtonChecked(hDlg, IDC_DRYRUN_CHECK) == BST_CHECKED);
            return TRUE;

        case IDC_COPY_CHECK:
            g_state.copyFiles = (IsDlgButtonChecked(hDlg, IDC_COPY_CHECK) == BST_CHECKED);
            return TRUE;

        case IDC_DELETE_CHECK:
            g_state.deleteOriginals = (IsDlgButtonChecked(hDlg, IDC_DELETE_CHECK) == BST_CHECKED);
            return TRUE;

        case IDC_ZIPS_CHECK:
            g_state.includeZips = (IsDlgButtonChecked(hDlg, IDC_ZIPS_CHECK) == BST_CHECKED);
            return TRUE;

        case IDC_SUBFOLDERS_CHECK:
            g_state.scanSubfolders = (IsDlgButtonChecked(hDlg, IDC_SUBFOLDERS_CHECK) == BST_CHECKED);
            return TRUE;

        case IDC_EXPORT_CSV: {
            if (g_state.projects.empty()) {
                MessageBoxW(hDlg, L"No projects to export.", L"Error", MB_OK | MB_ICONERROR);
                return TRUE;
            }

            // Save file dialog
            OPENFILENAMEW ofn = { 0 };
            wchar_t fileName[MAX_PATH] = L"projects.csv";
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = hDlg;
            ofn.lpstrFilter = L"CSV Files (*.csv)\0*.csv\0All Files (*.*)\0*.*\0";
            ofn.lpstrFile = fileName;
            ofn.nMaxFile = MAX_PATH;
            ofn.lpstrTitle = L"Export Projects to CSV";
            ofn.Flags = OFN_OVERWRITEPROMPT;

            if (GetSaveFileNameW(&ofn)) {
                std::wofstream file(fileName);
                if (file.is_open()) {
                    file << L"Project Name,Version,Version Group,Size (Bytes),Size (Human),File Count\n";

                    for (const auto& project : g_state.projects) {
                        file << project.projectName << L","
                            << project.versionString << L","
                            << project.versionGroup << L","
                            << project.totalSize << L","
                            << FormatSize(project.totalSize) << L","
                            << project.files.size() << L"\n";
                    }

                    file.close();
                    MessageBoxW(hDlg, L"Export completed successfully.", L"Success", MB_OK | MB_ICONINFORMATION);
                }
            }
            return TRUE;
        }
        }
        break;
    }

    case WM_USER_SCAN_COMPLETE: {
        g_state.isScanning = false;
        EnableUI(true);
        SetWindowText(g_state.hScanBtn, L"Scan");

        // Update list view
        AddLogMessage(L"Scan complete. Found " + std::to_wstring(g_state.projects.size()) + L" projects.");
        SetWindowText(g_state.hStatusBar, L"Ready");

        UpdateListView();
        return TRUE;
    }

    case WM_USER_PROGRESS_UPDATE: {
        SendMessage(g_state.hProgressBar, PBM_SETPOS, (WPARAM)wParam, 0);
        return TRUE;
    }

    case WM_USER_LOG_MESSAGE: {
        std::wstring* msg = (std::wstring*)lParam;
        if (msg) {
            AddLogMessage(*msg);
            delete msg;
        }
        return TRUE;
    }

    case WM_SIZE: {
        // Handle resizing - would need to reposition controls
        return TRUE;
    }

    case WM_CLOSE:
        EndDialog(hDlg, 0);
        return TRUE;

    case WM_DESTROY:
        PostQuitMessage(0);
        return TRUE;
    }

    return FALSE;
}

// ============================================================================
// Resource definitions (inlined since we're keeping it single-file)
// ============================================================================

// Control IDs
#define IDC_SOURCE_EDIT      1001
#define IDC_DEST_EDIT        1002
#define IDC_BROWSE_SOURCE    1003
#define IDC_BROWSE_DEST      1004
#define IDC_SCAN_BTN         1005
#define IDC_ORGANIZE_BTN     1006
#define IDC_PROJECT_LIST     1007
#define IDC_PROGRESS         1008
#define IDC_STATUS           1009
#define IDC_DRYRUN_CHECK     1010
#define IDC_COPY_CHECK       1011
#define IDC_DELETE_CHECK     1012
#define IDC_ZIPS_CHECK       1013
#define IDC_SUBFOLDERS_CHECK 1014
#define IDC_EXPORT_CSV       1015

// ============================================================================
// Main Entry Point
// ============================================================================

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // Initialize common controls
    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_BAR_CLASSES | ICC_LISTVIEW_CLASSES | ICC_PROGRESS_CLASS;
    InitCommonControlsEx(&icex);

    // Create dialog
    DLGTEMPLATE dlgTemplate = { 0 };
    dlgTemplate.style = DS_CENTER | DS_SETFONT | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
    dlgTemplate.dwExtendedStyle = 0;
    dlgTemplate.cdit = 0;
    dlgTemplate.x = 0;
    dlgTemplate.y = 0;
    dlgTemplate.cx = 700;
    dlgTemplate.cy = 500;

    // Since we can't easily embed resources, we'll use a simpler approach
    // Create a modeless dialog from a dialog template

    // Actually, for simplicity, let's create the window manually
    // This is a simplified version - in a real app, you'd use a dialog resource

    // Create main window
    HWND hWnd = CreateWindowExW(
        WS_EX_CONTROLPARENT,
        L"STATIC",
        APP_NAME,
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT,
        800, 600,
        NULL, NULL, hInstance, NULL
    );

    if (!hWnd) {
        MessageBoxW(NULL, L"Failed to create main window.", L"Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    // Create controls on the main window
    // This is a bit messy, but keeps it single-file
    // In practice, you'd use a dialog resource

    // Since creating all controls manually would be very verbose,
    // I'll use a dialog resource approach via a temporary dialog

    // For now, let's use the simpler approach of loading a dialog from a resource
    // We'll need to define the dialog resource in a .rc file

    // For this prototype, we'll just show a message box
    MessageBoxW(NULL,
        L"FL Studio Project Organizer\n\n"
        L"This is a prototype version.\n\n"
        L"The full version would include:\n"
        L"• GUI with folder selection\n"
        L"• Project scanning\n"
        L"• Version detection\n"
        L"• Organization by version\n"
        L"• CSV export\n"
        L"• Dry run mode\n"
        L"• Verification of copied files",
        APP_NAME, MB_OK | MB_ICONINFORMATION);

    // For now, let's just demonstrate the scanner functionality
    // by performing a quick scan on the default path

    std::wstring docsPath;
    wchar_t docsPathBuffer[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_PERSONAL, NULL, 0, docsPathBuffer))) {
        docsPath = std::wstring(docsPathBuffer) + L"\\Image-Line\\FL Studio\\Projects";
    }

    if (!docsPath.empty() && fs::exists(docsPath)) {
        MessageBoxW(NULL,
            (L"Scanning: " + docsPath + L"\n\n"
                L"This will scan for FL Studio projects and detect their versions.\n"
                L"(Full GUI version would show progress and results)").c_str(),
            APP_NAME, MB_OK | MB_ICONINFORMATION);

        // Perform a quick scan
        ProjectScanner scanner;
        scanner.Scan(docsPath, true, true);
        auto projects = scanner.GetProjects();

        std::wstring result = L"Scan Complete!\n\n";
        result += L"Found " + std::to_wstring(projects.size()) + L" projects.\n\n";

        // Group by version
        std::map<std::wstring, int> versionGroups;
        for (const auto& project : projects) {
            versionGroups[project.versionGroup]++;
        }

        for (const auto& [group, count] : versionGroups) {
            result += group + L": " + std::to_wstring(count) + L"\n";
        }

        MessageBoxW(NULL, result.c_str(), L"Scan Results", MB_OK | MB_ICONINFORMATION);

        // Also export to CSV in the current directory
        std::wstring csvPath = L"fl_projects_export.csv";
        std::wofstream file(csvPath);
        if (file.is_open()) {
            file << L"Project Name,Version,Version Group,Size (Bytes),File Count\n";
            for (const auto& project : projects) {
                file << project.projectName << L","
                    << project.versionString << L","
                    << project.versionGroup << L","
                    << project.totalSize << L","
                    << project.files.size() << L"\n";
            }
            file.close();
            MessageBoxW(NULL,
                (L"CSV exported to: " + csvPath).c_str(),
                L"Export Complete", MB_OK | MB_ICONINFORMATION);
        }
    }

    // Show main window (though we haven't populated it with controls in this simplified version)
    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    // Message loop
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return (int)msg.wParam;
}