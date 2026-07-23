#include "VersionDetector.h"

VersionDetector::VersionInfo VersionDetector::DetectVersion(const juce::File& file)
{
    VersionInfo info;

    try
    {
        juce::FileInputStream fis(file);
        if (!fis.openedOk())
            return info;

        juce::MemoryBlock mb;
        fis.readIntoMemoryBlock(mb, 16384); // Read first 16KB

        info = DetectVersionFromData(mb);

        if (info.isValid)
        {
            info.groupName = GetVersionGroup(info.versionString);
            info.majorVersion = GetMajorVersion(info.versionString);
        }
    }
    catch (const std::exception&)
    {
        // Return invalid
    }

    return info;
}

VersionDetector::VersionInfo VersionDetector::DetectVersionFromData(const juce::MemoryBlock& data)
{
    VersionInfo info;

    // Convert binary data to string safely
    juce::String dataString;
    const char* raw = static_cast<const char*>(data.getData());
    size_t size = data.getSize();

    for (size_t i = 0; i < size; ++i)
    {
        // Replace nulls and control chars with spaces
        unsigned char c = static_cast<unsigned char>(raw[i]);
        dataString += (c < 32 || c > 126) ? ' ' : raw[i];
    }

    std::string stdData = dataString.toStdString();

    // Try modern FL version pattern: major.minor.patch.build
    std::regex modernPattern(R"((\d{2})\.(\d+)\.(\d+)\.(\d+))");
    std::smatch matches;

    if (std::regex_search(stdData, matches, modernPattern))
    {
        int major = std::stoi(matches[1]);
        if (major >= 20 && major <= 99)
        {
            info.versionString = juce::String(matches[0].str());
            info.isValid = true;
            return info;
        }
    }

    // Try older FL version pattern: major.minor.patch (FL2 through FL19,
    // e.g. FruityLoops 2's "2.0.0" string embedded right after the FLdt
    // chunk header). Gated at >=2 rather than >=1 -- FL1 had no embedded
    // version string at all, so a "1.x.x" match here would almost always
    // be a false positive from unrelated binary data, not a real version.
    std::regex oldPattern(R"((\d+)\.(\d+)\.(\d+))");
    if (std::regex_search(stdData, matches, oldPattern))
    {
        int major = std::stoi(matches[1]);
        if (major >= 2 && major <= 19)
        {
            info.versionString = juce::String(matches[0].str());
            info.isValid = true;
            return info;
        }
    }

    // Try looking for version near FLhd/FLdt markers (catches version
    // strings JUCE's regexes above might have missed due to odd spacing
    // in the chunk data).
    std::regex markerPattern(R"(FLh[d][^\d]*(\d+\.\d+\.\d+(?:\.\d+)?))");
    if (std::regex_search(stdData, matches, markerPattern))
    {
        info.versionString = juce::String(matches[1].str());
        info.isValid = true;
        return info;
    }

    // FruityLoops 1 fallback: these files carry the FLhd/FLdt chunk
    // markers but embed no version string whatsoever -- there was nothing
    // to parse in FL1's project format. If we can see the markers but
    // found no number above, classify it explicitly as FL1 rather than
    // letting it fall through to "Unknown".
    if (stdData.find("FLhd") != std::string::npos && stdData.find("FLdt") != std::string::npos)
    {
        info.versionString = "1.0.0";
        info.isValid = true;
        return info;
    }

    return info;
}

juce::String VersionDetector::GetVersionGroup(const juce::String& versionString)
{
    struct VersionMapping
    {
        juce::String pattern;
        juce::String group;
        int major;
    };

    std::vector<VersionMapping> mappings = {
        { "25.", "2025", 25 },
        { "24.", "2024", 24 },
        { "23.", "2023", 23 },
        { "22.", "2022", 22 },
        { "21.", "FL21", 21 },
        { "20.", "FL20", 20 },
        { "12.", "FL12", 12 },
        { "11.", "FL11", 11 },
        { "10.", "FL10", 10 },
        { "9.", "FL9", 9 },
        { "8.", "FL8", 8 },
        { "7.", "FL7", 7 },
        { "6.", "FL6", 6 },
        { "5.", "FL5", 5 },
        { "4.", "FL4", 4 },
        { "3.", "FL3", 3 },
        { "2.", "FL2", 2 },
        { "1.", "FL1", 1 }
    };

    for (const auto& mapping : mappings)
    {
        if (versionString.startsWith(mapping.pattern))
            return mapping.group;
    }

    return "Unknown";
}

int VersionDetector::GetMajorVersion(const juce::String& versionString)
{
    struct VersionMapping
    {
        juce::String pattern;
        int major;
    };

    std::vector<VersionMapping> mappings = {
        { "25.", 25 },
        { "24.", 24 },
        { "23.", 23 },
        { "22.", 22 },
        { "21.", 21 },
        { "20.", 20 },
        { "12.", 12 },
        { "11.", 11 },
        { "10.", 10 },
        { "9.", 9 },
        { "8.", 8 },
        { "7.", 7 },
        { "6.", 6 },
        { "5.", 5 },
        { "4.", 4 },
        { "3.", 3 },
        { "2.", 2 },
        { "1.", 1 }
    };

    for (const auto& mapping : mappings)
    {
        if (versionString.startsWith(mapping.pattern))
            return mapping.major;
    }

    return -1;
}