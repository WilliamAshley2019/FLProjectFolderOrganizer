#pragma once
#include <JuceHeader.h>
#include <regex>

class VersionDetector
{
public:
    struct VersionInfo
    {
        juce::String versionString;
        juce::String groupName;
        int majorVersion;
        bool isValid;

        VersionInfo() : majorVersion(-1), isValid(false) {}
    };

    static VersionInfo DetectVersion(const juce::File& file);
    static VersionInfo DetectVersionFromData(const juce::MemoryBlock& data);
    static juce::String GetVersionGroup(const juce::String& versionString);
    static int GetMajorVersion(const juce::String& versionString);

private:
    static juce::String CleanVersionString(const juce::String& raw);
    static bool IsPlausibleFLVersion(const juce::String& version, int major);
}; 
