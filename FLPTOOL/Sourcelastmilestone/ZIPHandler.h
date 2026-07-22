#pragma once
#include <JuceHeader.h>

class ZIPHandler
{
public:
    struct ZIPEntry
    {
        juce::String    filename;
        juce::MemoryBlock data;
        size_t          uncompressedSize = 0;
        juce::Time      lastModTime;             // mapped from ZipEntry::fileTime
    };

    static bool IsZIPFile(const juce::File& file);
    static std::vector<ZIPEntry> ReadZIPContents(const juce::File& zipFile);
    static juce::String ReadFLPFromZIP(const juce::File& zipFile);
    static bool ExtractZIPFile(const juce::File& zipFile, const juce::File& destFolder);
    static bool CreateZIPFromFolder(const juce::File& folder, const juce::File& zipFile);
};
