#include "ZIPHandler.h"
#include "VersionDetector.h"
#include "SafeFileOperations.h"
#include <set>

namespace
{
    // Hard limits for archives we are about to extract. Chosen to be far
    // above any real FL Studio project/sample-pack ZIP while still stopping
    // a hostile archive from filling the disk or hanging the app.
    constexpr int       kMaxEntries            = 20000;
    constexpr juce::int64 kMaxTotalUncompressed = 4LL * 1024 * 1024 * 1024;   // 4 GB
    constexpr juce::int64 kRatioCheckThreshold  = 256LL * 1024 * 1024;        // only judge ratio above 256 MB
    constexpr juce::int64 kMaxOverallRatio      = 200;                        // uncompressed : archive size
}

bool ZIPHandler::IsZIPFile(const juce::File& file)
{
    if (!file.existsAsFile())
        return false;

    juce::FileInputStream fis(file);
    if (!fis.openedOk())
        return false;

    uint8_t magic[2] = {};
    if (fis.read(magic, 2) != 2)
        return false;

    return (magic[0] == 0x50 && magic[1] == 0x4B);
}

std::vector<ZIPHandler::ZIPEntry> ZIPHandler::ReadZIPContents(const juce::File& zipFile)
{
    std::vector<ZIPEntry> entries;

    juce::ZipFile zip(zipFile);
    int numEntries = zip.getNumEntries();
    if (numEntries == 0)
        return entries;

    for (int i = 0; i < numEntries; ++i)
    {
        const auto* ze = zip.getEntry(i);
        if (ze == nullptr)
            continue;

        ZIPEntry entry;
        entry.filename          = ze->filename;
        entry.uncompressedSize  = (size_t) ze->uncompressedSize;
        entry.lastModTime       = ze->fileTime;          // ZipEntry::fileTime in JUCE 8

        // Read contents via createStreamForEntry
        if (ze->uncompressedSize > 0 && ze->uncompressedSize < 100LL * 1024 * 1024)
        {
            std::unique_ptr<juce::InputStream> stream (zip.createStreamForEntry(i));
            if (stream != nullptr)
                stream->readIntoMemoryBlock(entry.data, (juce::int64) ze->uncompressedSize);
        }

        entries.push_back(std::move(entry));
    }

    return entries;
}

juce::String ZIPHandler::ReadFLPFromZIP(const juce::File& zipFile)
{
    juce::ZipFile zip(zipFile);
    int numEntries = zip.getNumEntries();

    for (int i = 0; i < numEntries; ++i)
    {
        const auto* ze = zip.getEntry(i);
        if (ze == nullptr)
            continue;

        // Match the extension, not any name that merely contains ".flp"
        // (so "song.flp.bak" or "my.flp backup/readme.txt" don't qualify).
        if (ze->filename.endsWithIgnoreCase(".flp"))
        {
            std::unique_ptr<juce::InputStream> stream(zip.createStreamForEntry(i));
            if (stream != nullptr)
            {
                // Same 16 KB window VersionDetector::DetectVersion uses for
                // loose files -- never inflate a whole entry just to read a
                // version string (a zip bomb would otherwise land here).
                juce::MemoryBlock data;
                stream->readIntoMemoryBlock(data, 16384);

                auto info = VersionDetector::DetectVersionFromData(data);
                if (info.isValid)
                    return info.versionString;
            }
        }
    }

    return {};
}

juce::String ZIPHandler::ValidateZIP(const juce::File& zipFile)
{
    if (!zipFile.existsAsFile())
        return "Archive not found";

    juce::ZipFile zip(zipFile);
    const int numEntries = zip.getNumEntries();

    if (numEntries == 0)
        return "Archive is empty or unreadable";
    if (numEntries > kMaxEntries)
        return "Archive has too many entries (" + juce::String(numEntries) + ")";

    std::set<juce::String> seenLowerNames;
    juce::int64 totalUncompressed = 0;

    for (int i = 0; i < numEntries; ++i)
    {
        const auto* ze = zip.getEntry(i);
        if (ze == nullptr)
            continue;

        if (!SafeFileOperations::IsSafeRelativePath(ze->filename))
            return "Unsafe entry name [" + ze->filename + "]: "
                 + SafeFileOperations::GetSafeRelativePathRejectionReason(ze->filename);

        // Symlink entries can redirect later writes outside the target.
        if (ze->isSymbolicLink)
            return "Archive contains a symbolic link entry [" + ze->filename + "]";

        // "A.wav" and "a.WAV" are the same file on Windows.
        const auto key = ze->filename.replaceCharacter('\\', '/').toLowerCase();
        if (!seenLowerNames.insert(key).second)
            return "Duplicate or case-colliding entry [" + ze->filename + "]";

        if (ze->uncompressedSize < 0)
            return "Entry [" + ze->filename + "] reports a negative size";

        totalUncompressed += ze->uncompressedSize;
        if (totalUncompressed > kMaxTotalUncompressed)
            return "Archive would expand beyond the " + juce::String(kMaxTotalUncompressed / (1024 * 1024 * 1024))
                 + " GB limit";
    }

    // juce::ZipFile::ZipEntry doesn't expose per-entry compressed sizes, so
    // the ratio is judged on the whole archive, and only once it is big
    // enough to matter.
    const juce::int64 archiveSize = zipFile.getSize();
    if (totalUncompressed > kRatioCheckThreshold && archiveSize > 0
        && totalUncompressed / archiveSize > kMaxOverallRatio)
        return "Archive compression ratio is implausibly high (possible zip bomb)";

    return {};
}

bool ZIPHandler::ExtractZIPFile(const juce::File& zipFile, const juce::File& destFolder)
{
    // Refuse the whole archive on any unsafe entry rather than extracting a
    // partial tree. Without this a hostile archive can use "..", drive
    // letters, device names, or case-colliding names to write outside
    // destFolder or clobber files.
    auto problem = ValidateZIP(zipFile);
    if (problem.isNotEmpty())
    {
        DBG("ZIPHandler: refusing " << zipFile.getFullPathName() << ": " << problem);
        return false;
    }

    juce::ZipFile zip(zipFile);

    if (!destFolder.exists())
        destFolder.createDirectory();

    // Don't extract through a symlinked/junctioned folder below the
    // destination root's own parent chain.
    if (SafeFileOperations::HasSymlinkAncestor(destFolder, destFolder.getParentDirectory()))
    {
        DBG("ZIPHandler: destination is a link: " << destFolder.getFullPathName());
        return false;
    }

    // uncompressTo handles all entries and subdirectories automatically
    auto result = zip.uncompressTo(destFolder, true);
    return result.wasOk();
}

bool ZIPHandler::CreateZIPFromFolder(const juce::File& folder, const juce::File& zipFile)
{
    juce::ZipFile::Builder builder;

    juce::Array<juce::File> files;
    folder.findChildFiles(files, juce::File::findFiles, true);

    for (const auto& file : files)
    {
        auto relativePath = file.getRelativePathFrom(folder);
        builder.addFile(file, 6, relativePath);   // compression level 6
    }

    juce::FileOutputStream fos(zipFile);
    if (!fos.openedOk())
        return false;

    return builder.writeToStream(fos, nullptr);
}
