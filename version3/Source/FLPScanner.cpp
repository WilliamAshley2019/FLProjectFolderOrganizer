#include "FLPScanner.h"
#include "RecycleBinManager.h"

// ─── SHA256 via Windows CNG (BCrypt) ────────────────────────────────────────
// Available on all Windows versions since Vista. No extra libs needed in MSVC
// beyond bcrypt.lib, which we link below with a pragma.
#pragma comment(lib, "bcrypt.lib")
#include <windows.h>
#include <bcrypt.h>

static juce::String sha256File(const juce::File& file, const std::atomic<bool>* abortFlag = nullptr)
{
    BCRYPT_ALG_HANDLE hAlg  = nullptr;
    BCRYPT_HASH_HANDLE hHash = nullptr;
    juce::String result;

    if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0)
        return {};

    DWORD hashObjSize = 0, dummy = 0;
    BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH,
        (PBYTE)&hashObjSize, sizeof(DWORD), &dummy, 0);

    juce::HeapBlock<BYTE> hashObj(hashObjSize);
    if (BCryptCreateHash(hAlg, &hHash, hashObj, hashObjSize, nullptr, 0, 0) != 0)
    {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return {};
    }

    // Feed file in 64KB chunks. Checking abortFlag between chunks means a
    // huge file (multi-GB sample, etc.) can be interrupted within ~64KB
    // of work instead of forcing the Stop button to wait for the entire
    // file to finish hashing.
    bool wasAborted = false;
    juce::FileInputStream fis(file);
    if (fis.openedOk())
    {
        constexpr int kChunk = 65536;
        juce::HeapBlock<BYTE> buf(kChunk);
        while (!fis.isExhausted())
        {
            if (abortFlag != nullptr && abortFlag->load())
            {
                wasAborted = true;
                break;
            }

            int n = fis.read(buf, kChunk);
            if (n <= 0) break;
            BCryptHashData(hHash, buf, (ULONG)n, 0);
        }
    }

    BYTE digest[32] = {};
    BCryptFinishHash(hHash, digest, 32, 0);
    BCryptDestroyHash(hHash);
    BCryptCloseAlgorithmProvider(hAlg, 0);

    if (wasAborted)
        return {}; // caller treats an empty hash as "incomplete, skip duplicate check"

    static const char* hex = "0123456789abcdef";
    for (int i = 0; i < 32; ++i)
    {
        result += hex[(digest[i] >> 4) & 0xF];
        result += hex[ digest[i]       & 0xF];
    }
    return result;
}

static juce::String sha256Block(const juce::MemoryBlock& data)
{
    BCRYPT_ALG_HANDLE  hAlg  = nullptr;
    BCRYPT_HASH_HANDLE hHash = nullptr;
    juce::String result;

    if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0)
        return {};

    DWORD hashObjSize = 0, dummy = 0;
    BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH,
        (PBYTE)&hashObjSize, sizeof(DWORD), &dummy, 0);

    juce::HeapBlock<BYTE> hashObj(hashObjSize);
    BCryptCreateHash(hAlg, &hHash, hashObj, hashObjSize, nullptr, 0, 0);
    BCryptHashData(hHash, (PBYTE)data.getData(), (ULONG)data.getSize(), 0);

    BYTE digest[32] = {};
    BCryptFinishHash(hHash, digest, 32, 0);
    BCryptDestroyHash(hHash);
    BCryptCloseAlgorithmProvider(hAlg, 0);

    static const char* hex = "0123456789abcdef";
    for (int i = 0; i < 32; ++i)
    {
        result += hex[(digest[i] >> 4) & 0xF];
        result += hex[ digest[i]       & 0xF];
    }
    return result;
}
// ────────────────────────────────────────────────────────────────────────────

FLPScanner::FLPScanner() : juce::Thread("FLPScanner")
{
    // Database lives under the WAM (William Ashley Music) data tree rather
    // than roaming AppData or FL Studio's own folder structure, since this
    // is a third-party tool, not Image-Line software.
    auto dbFile = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
        .getChildFile("WAM")
        .getChildFile("FLProjectOrganizer")
        .getChildFile("projects.db");
    dbFile.getParentDirectory().createDirectory();

    dbInitialized = database.Initialize(dbFile);
    if (dbInitialized)
        sendLog("Database initialized at: " + dbFile.getFullPathName());
    else
        sendLog("Failed to initialize database!");
}

FLPScanner::~FLPScanner()
{
    stopScanning = true;
    waitForThreadToExit(5000);
}

void FLPScanner::Scan(const juce::File& sourceDir, const juce::File& destDir,
    bool includeZipsFlag, bool scanSubfoldersFlag, bool dryRun, bool deleteOriginalsAfterCopy)
{
    sourceDirectory      = sourceDir;
    destinationDirectory = destDir;
    includeZips          = includeZipsFlag;
    scanSubfolders       = scanSubfoldersFlag;
    isDryRun             = dryRun;
    deleteOriginals      = deleteOriginalsAfterCopy;

    results.clear();
    hashMap.clear();
    totalScanned  = 0;
    duplicateCount = 0;
    stopScanning  = false;

    startThread();
}

void FLPScanner::StopScanning()
{
    stopScanning = true;
}

void FLPScanner::run()
{
    sendLog("Starting scan of: " + sourceDirectory.getFullPathName());

    juce::Array<juce::File> flpFiles, zipFiles;

    sourceDirectory.findChildFiles(flpFiles, juce::File::findFiles, scanSubfolders, "*.flp");
    if (includeZips)
        sourceDirectory.findChildFiles(zipFiles, juce::File::findFiles, scanSubfolders, "*.zip");

    int total     = flpFiles.size() + zipFiles.size();
    int processed = 0;

    for (const auto& file : flpFiles)
    {
        if (stopScanning) break;
        ProcessFLP(file, sourceDirectory);
        sendProgress((float)(++processed) / (float)total);
    }

    for (const auto& file : zipFiles)
    {
        if (stopScanning) break;
        ProcessZIP(file, sourceDirectory);
        sendProgress((float)(++processed) / (float)total);
    }

    sendLog("Scan complete. Found "     + juce::String(totalScanned) + " projects.");
    sendLog("Duplicates found: " + juce::String(duplicateCount));
    sendProgress(1.0f);
}

void FLPScanner::ProcessFLP(const juce::File& flpFile, const juce::File& /*sourceDir*/)
{
    ProjectDatabase::ProjectEntry entry;
    entry.path         = flpFile.getFullPathName();
    entry.fileName     = flpFile.getFileNameWithoutExtension();
    entry.fileSize     = flpFile.getSize();
    entry.lastModified = flpFile.getLastModificationTime().toMilliseconds();
    entry.isZipped     = false;

    auto versionInfo = VersionDetector::DetectVersion(flpFile);
    entry.version   = versionInfo.isValid ? versionInfo.versionString : "Unknown";
    entry.groupName = versionInfo.isValid ? versionInfo.groupName     : "Unknown";

    entry.hash = CalculateFileHash(flpFile);

    // If the hash got aborted partway through (huge file + Stop pressed),
    // bail before touching the database or kicking off a copy/organize.
    if (stopScanning.load())
        return;

    CheckForDuplicates(entry);

    if (dbInitialized)
        database.AddProject(entry);

    results.push_back(entry);
    ++totalScanned;

    if (onProjectFound)
        onProjectFound(entry);

    if (!isDryRun && destinationDirectory.isDirectory())
        OrganizeProject(entry, destinationDirectory);
}

void FLPScanner::ProcessZIP(const juce::File& zipFile, const juce::File& /*sourceDir*/)
{
    if (!ZIPHandler::IsZIPFile(zipFile))
        return;

    juce::String version = ZIPHandler::ReadFLPFromZIP(zipFile);

    ProjectDatabase::ProjectEntry entry;
    entry.path         = zipFile.getFullPathName();
    entry.fileName     = zipFile.getFileNameWithoutExtension();
    entry.fileSize     = zipFile.getSize();
    entry.lastModified = zipFile.getLastModificationTime().toMilliseconds();
    entry.isZipped     = true;
    entry.version      = version.isNotEmpty() ? version : "Unknown";
    entry.groupName    = version.isNotEmpty() ? VersionDetector::GetVersionGroup(version) : "Unknown";
    entry.hash         = CalculateFileHash(zipFile);

    if (stopScanning.load())
        return;

    CheckForDuplicates(entry);

    if (dbInitialized)
        database.AddProject(entry);

    results.push_back(entry);
    ++totalScanned;

    if (onProjectFound)
        onProjectFound(entry);

    if (!isDryRun && destinationDirectory.isDirectory())
    {
        juce::File extractDest = destinationDirectory.getChildFile(entry.groupName)
                                                     .getChildFile(entry.fileName);

        if (ZIPHandler::ExtractZIPFile(zipFile, extractDest))
        {
            sendLog("Extracted: " + zipFile.getFileName() + " -> " + extractDest.getFullPathName());

            ProjectDatabase::TransactionRecord record;
            record.action     = "EXTRACT";
            record.sourcePath = zipFile.getFullPathName();
            record.destPath   = extractDest.getFullPathName();
            record.success    = true;
            database.AddTransaction(record);
        }
    }
}

juce::String FLPScanner::CalculateFileHash(const juce::File& file)
{
    return sha256File(file, &stopScanning);
}

juce::String FLPScanner::CalculateMemoryHash(const juce::MemoryBlock& data)
{
    return sha256Block(data);
}

void FLPScanner::CheckForDuplicates(const ProjectDatabase::ProjectEntry& entry)
{
    if (entry.hash.isEmpty())
        return;

    auto it = hashMap.find(entry.hash);
    if (it != hashMap.end())
    {
        ++duplicateCount;
        for (const auto& path : it->second)
            if (onDuplicateFound)
                onDuplicateFound(entry.path, path);
        it->second.push_back(entry.path);
    }
    else
    {
        hashMap[entry.hash] = { entry.path };
    }
}

void FLPScanner::OrganizeProject(const ProjectDatabase::ProjectEntry& entry, const juce::File& destDir)
{
    // Note: juce::File::copyFileTo() is a single blocking OS call with no
    // interruption hook, so a Stop press can't abort mid-copy of one huge
    // file -- but this check stops the queue between files, which covers
    // the common case of many files queued after Stop was pressed.
    if (stopScanning.load())
        return;

    try
    {
        juce::File sourceFile(entry.path);
        juce::File versionFolder = destDir.getChildFile(entry.groupName);
        juce::File destFile      = versionFolder.getChildFile(sourceFile.getFileName());

        // Check there's enough room on the destination drive before doing
        // anything. Fails loudly rather than partway through a copy.
        if (!RecycleBinManager::HasSufficientSpace(destDir, sourceFile.getSize()))
        {
            sendLog("SKIPPED (insufficient disk space): " + sourceFile.getFileName() +
                " -- " + RecycleBinManager::GetFreeSpaceDescription(destDir));

            ProjectDatabase::TransactionRecord record;
            record.action     = "COPY";
            record.sourcePath = sourceFile.getFullPathName();
            record.destPath   = destFile.getFullPathName();
            record.success    = false;
            record.details    = "Insufficient disk space";
            database.AddTransaction(record);
            return;
        }

        if (!versionFolder.exists())
            versionFolder.createDirectory();

        if (destFile.exists())
            destFile = GetUniquePath(destFile);

        if (sourceFile.copyFileTo(destFile))
        {
            sendLog("Copied: " + sourceFile.getFileName() + " -> " + destFile.getFullPathName());

            ProjectDatabase::TransactionRecord record;
            record.action     = "COPY";
            record.sourcePath = sourceFile.getFullPathName();
            record.destPath   = destFile.getFullPathName();
            record.success    = true;
            database.AddTransaction(record);

            // Only remove the original if explicitly requested, and route
            // through the recycle bin rather than a hard delete. The UI is
            // responsible for double-confirming this with the user before
            // deleteOriginals is ever set true.
            if (deleteOriginals)
            {
                if (recycleBin.SoftDelete(sourceFile))
                {
                    sendLog("Moved original to recycle bin: " + sourceFile.getFullPathName());

                    ProjectDatabase::TransactionRecord delRecord;
                    delRecord.action     = "SOFT_DELETE";
                    delRecord.sourcePath = sourceFile.getFullPathName();
                    delRecord.destPath   = recycleBin.GetRecycleBinRoot().getFullPathName();
                    delRecord.success    = true;
                    database.AddTransaction(delRecord);
                }
                else
                {
                    sendLog("WARNING: Could not move original to recycle bin: " + sourceFile.getFullPathName());
                }
            }
        }
    }
    catch (const std::exception& e)
    {
        sendLog("Error organizing: " + juce::String(e.what()));
    }
}

juce::File FLPScanner::GetUniquePath(const juce::File& basePath)
{
    juce::File parent = basePath.getParentDirectory();
    juce::String stem = basePath.getFileNameWithoutExtension();
    juce::String ext  = basePath.getFileExtension();

    for (int i = 2; i < 1000; ++i)
    {
        juce::File candidate = parent.getChildFile(stem + " (" + juce::String(i) + ")" + ext);
        if (!candidate.exists())
            return candidate;
    }
    return basePath;
}

bool FLPScanner::VerifyCopy(const juce::File& source, const juce::File& dest)
{
    if (!source.existsAsFile() || !dest.existsAsFile())
        return false;
    if (source.getSize() != dest.getSize())
        return false;
    return CalculateFileHash(source) == CalculateFileHash(dest);
}

void FLPScanner::sendLog(const juce::String& msg)
{
    if (onLog) onLog(msg);
}

void FLPScanner::sendProgress(float progress)
{
    if (onProgress) onProgress(progress);
}
