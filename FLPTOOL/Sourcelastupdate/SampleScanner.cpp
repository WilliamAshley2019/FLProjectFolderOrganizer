#include "SampleScanner.h"

// Reuse the same BCrypt SHA256 approach as FLPScanner, duplicated here
// rather than shared via a header to keep each scanner's translation
// unit self-contained (mirrors how FLPScanner.cpp does its own local
// static helpers).
#pragma comment(lib, "bcrypt.lib")
#include <windows.h>
#include <bcrypt.h>

static juce::String sha256FileSample(const juce::File& file, const std::atomic<bool>* abortFlag)
{
    BCRYPT_ALG_HANDLE hAlg  = nullptr;
    BCRYPT_HASH_HANDLE hHash = nullptr;

    if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0)
        return {};

    DWORD hashObjSize = 0, dummy = 0;
    BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH, (PBYTE)&hashObjSize, sizeof(DWORD), &dummy, 0);

    juce::HeapBlock<BYTE> hashObj(hashObjSize);
    if (BCryptCreateHash(hAlg, &hHash, hashObj, hashObjSize, nullptr, 0, 0) != 0)
    {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return {};
    }

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
        return {};

    static const char* hex = "0123456789abcdef";
    juce::String result;
    for (int i = 0; i < 32; ++i)
    {
        result += hex[(digest[i] >> 4) & 0xF];
        result += hex[ digest[i]       & 0xF];
    }
    return result;
}

SampleScanner::SampleScanner() : juce::Thread("SampleScanner") {}

SampleScanner::~SampleScanner()
{
    stopScanning = true;
    waitForThreadToExit(5000);
}

void SampleScanner::Scan(const juce::File& sourceDir, const juce::File& destDir,
    const juce::StringArray& extensionsToScan, bool scanSubfoldersFlag,
    bool dryRun, bool deleteOriginalsAfterCopy)
{
    sourceDirectory      = sourceDir;
    destinationDirectory = destDir;
    extensions           = extensionsToScan.isEmpty()
        ? juce::StringArray{ "wav", "mp3", "flac", "ogg" }
        : extensionsToScan;
    scanSubfolders  = scanSubfoldersFlag;
    isDryRun        = dryRun;
    deleteOriginals = deleteOriginalsAfterCopy;

    results.clear();
    hashMap.clear();
    protectedFiles.clear();
    totalScanned   = 0;
    duplicateCount = 0;
    stopScanning   = false;

    startThread();
}

void SampleScanner::StopScanning()
{
    stopScanning = true;
}

void SampleScanner::FindFilesSkippingBlockedDirs(const juce::File& dir, bool recurse,
    const juce::StringArray& exts, juce::Array<juce::File>& out)
{
    if (stopScanning.load())
        return;

    if (SafeFileOperations::IsBlockedDirectory(dir))
        return;

    for (const auto& item : juce::RangedDirectoryIterator(dir, false, "*",
        juce::File::findFilesAndDirectories))
    {
        if (stopScanning.load())
            return;

        juce::File f = item.getFile();

        if (f.isDirectory())
        {
            if (recurse)
                FindFilesSkippingBlockedDirs(f, true, exts, out);
            continue;
        }

        for (const auto& ext : exts)
        {
            if (f.getFileExtension().trimCharactersAtStart(".").equalsIgnoreCase(ext))
            {
                out.add(f);
                break;
            }
        }
    }
}

void SampleScanner::run()
{
    sendLog("Starting sample scan of: " + sourceDirectory.getFullPathName());

    if (SafeFileOperations::IsBlockedDirectory(sourceDirectory))
    {
        sendLog("REFUSED: The selected source folder is itself a protected system directory. "
            "Please choose a different folder.");
        sendProgress(1.0f);
        return;
    }

    juce::Array<juce::File> files;
    FindFilesSkippingBlockedDirs(sourceDirectory, scanSubfolders, extensions, files);

    int total = files.size();
    int processed = 0;

    for (const auto& file : files)
    {
        if (stopScanning) break;
        ProcessSample(file);
        sendProgress(total > 0 ? (float)(++processed) / (float)total : 1.0f);
    }

    sendLog("Sample scan complete. Found " + juce::String(totalScanned) + " samples.");
    sendLog("Duplicates found: " + juce::String(duplicateCount));
    sendProgress(1.0f);
}

void SampleScanner::ProcessSample(const juce::File& file)
{
    SampleEntry entry;
    entry.path         = file.getFullPathName();
    entry.fileName      = file.getFileName();
    entry.extension     = file.getFileExtension().trimCharactersAtStart(".").toLowerCase();
    entry.fileSize       = file.getSize();
    entry.lastModified  = file.getLastModificationTime().toMilliseconds();
    entry.hash          = CalculateFileHash(file);

    if (stopScanning.load())
        return;

    CheckForDuplicates(entry);

    results.push_back(entry);
    ++totalScanned;

    if (onSampleFound)
        onSampleFound(entry);

    if (!isDryRun && destinationDirectory.isDirectory())
        OrganizeSample(entry, destinationDirectory);
}

void SampleScanner::CheckForDuplicates(const SampleEntry& entry)
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

void SampleScanner::OrganizeSample(const SampleEntry& entry, const juce::File& destDir)
{
    juce::File sourceFile(entry.path);

    if (SafeFileOperations::RequiresProtectedFileConfirmation(sourceFile))
    {
        protectedFiles.push_back({ sourceFile.getFullPathName(),
            SafeFileOperations::GetProtectionReason(sourceFile) });
        if (onProtectedFileFound)
            onProtectedFileFound(protectedFiles.back());
        sendLog("SKIPPED (protected file): " + sourceFile.getFullPathName());
        return;
    }

    if (!RecycleBinManager::HasSufficientSpace(destDir, sourceFile.getSize()))
    {
        sendLog("SKIPPED (insufficient disk space): " + sourceFile.getFileName());
        return;
    }

    // Group by extension into subfolders: destDir\WAV\, destDir\MP3\, etc.
    juce::File typeFolder = destDir.getChildFile(entry.extension.toUpperCase());
    if (!typeFolder.exists())
        typeFolder.createDirectory();

    juce::File destFile = typeFolder.getChildFile(sourceFile.getFileName());
    if (destFile.exists())
    {
        // Same name already at destination -- only treat as a true
        // duplicate (skip copy) if the hash also matches. Otherwise
        // this is a different file that happens to share a name, so
        // give it a unique name rather than silently overwriting.
        juce::String existingHash = CalculateFileHash(destFile);
        if (existingHash == entry.hash)
        {
            sendLog("Already present (same hash), skipping: " + sourceFile.getFileName());
            return;
        }
        destFile = GetUniquePath(destFile);
    }

    if (sourceFile.copyFileTo(destFile))
    {
        sendLog("Copied: " + sourceFile.getFileName() + " -> " + destFile.getFullPathName());

        if (deleteOriginals)
        {
            if (SafeFileOperations::RequiresProtectedFileConfirmation(sourceFile))
                sendLog("REFUSED to delete protected file: " + sourceFile.getFullPathName());
            else if (recycleBin.SoftDelete(sourceFile))
                sendLog("Moved original to recycle bin: " + sourceFile.getFullPathName());
            else
                sendLog("WARNING: Could not move original to recycle bin: " + sourceFile.getFullPathName());
        }
    }
}

juce::File SampleScanner::GetUniquePath(const juce::File& basePath)
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

juce::String SampleScanner::CalculateFileHash(const juce::File& file)
{
    return sha256FileSample(file, &stopScanning);
}

void SampleScanner::sendLog(const juce::String& msg)
{
    if (onLog) onLog(msg);
}

void SampleScanner::sendProgress(float progress)
{
    if (onProgress) onProgress(progress);
}
