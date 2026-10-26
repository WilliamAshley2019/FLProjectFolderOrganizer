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
    // Returns an empty string if the archive is safe to extract, otherwise a
    // human-readable reason (unsafe entry name, symlink entry, duplicate or
    // case-colliding names, too many entries, size/ratio limits).
    // ExtractZIPFile() calls this itself; call it directly to show the reason.
    static juce::String ValidateZIP(const juce::File& zipFile);
    static bool ExtractZIPFile(const juce::File& zipFile, const juce::File& destFolder);
    static bool CreateZIPFromFolder(const juce::File& folder, const juce::File& zipFile);
};
