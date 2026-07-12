#include "ZIPHandler.h"
#include "VersionDetector.h"

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

        if (ze->filename.containsIgnoreCase(".flp"))
        {
            std::unique_ptr<juce::InputStream> stream(zip.createStreamForEntry(i));
            if (stream != nullptr)
            {
                juce::MemoryBlock data;
                stream->readIntoMemoryBlock(data);

                auto info = VersionDetector::DetectVersionFromData(data);
                if (info.isValid)
                    return info.versionString;
            }
        }
    }

    return {};
}

bool ZIPHandler::ExtractZIPFile(const juce::File& zipFile, const juce::File& destFolder)
{
    juce::ZipFile zip(zipFile);
    if (zip.getNumEntries() == 0)
        return false;

    if (!destFolder.exists())
        destFolder.createDirectory();

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
