#pragma once
#include <JuceHeader.h>

// Bounded VST2 .fxp identity reader. Reads only the fixed header; never touches
// plugin state. Two real-world length quirks are tolerated (see .cpp).
class FXPInspector
{
public:
    struct Metadata
    {
        juce::String pluginName;   // from the verified table, else empty
        juce::String pluginKind;   // "generator"/"effect"/{}
        juce::String pluginId;     // raw 4 chars, file order
        juce::String presetName;
        juce::String reason;       // non-empty => rejected or unrecognised
        bool ok() const { return pluginId.isNotEmpty() && (reason.isEmpty() || reason == kUnrecognised); }
        // ID as a big-endian integer (the form FST wrapper payloads store byte-reversed).
        juce::uint32 idValue() const;
    };
    static constexpr const char* kUnrecognised = "Unrecognized FXP plugin ID";
    static constexpr int kHeaderBytes = 60;

    static Metadata inspectBytes (const juce::uint8* data, size_t size, juce::int64 fileSize);
    static Metadata inspectFile (const juce::File& f);
};
