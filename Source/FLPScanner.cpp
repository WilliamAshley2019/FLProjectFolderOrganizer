#include "FLPScanner.h"

FLPScanner::FLPScanner() : juce::Thread("FLPScanner")
{
    auto dbFile = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("FLProjectOrganizer/projects.db");
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
    bool includeZipsFlag, bool scanSubfoldersFlag, bool dryRun)
{
    sourceDirectory      = sourceDir;
    destinationDirectory = destDir;
    includeZips          = includeZipsFlag;
    scanSubfolders       = scanSubfoldersFlag;
    isDryRun             = dryRun;

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
    // juce::SHA256 has a direct File constructor - simplest approach
    return juce::SHA256(file).toHexString();
}

juce::String FLPScanner::CalculateMemoryHash(const juce::MemoryBlock& data)
{
    return juce::SHA256(data).toHexString();
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
    try
    {
        juce::File sourceFile(entry.path);
        juce::File versionFolder = destDir.getChildFile(entry.groupName);
        juce::File destFile      = versionFolder.getChildFile(sourceFile.getFileName());

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
