#pragma once
#include <JuceHeader.h>

// Reads the product name from the header of a u-he .h2p preset. Only the
// line-oriented header is interpreted; comments are stripped first so prose
// inside them cannot masquerade as directives. Never reads preset state.
namespace H2PInspector
{
    constexpr int kHeaderBytes = 64 * 1024;
    // Returns the plugin name, or {} with `error` set.
    juce::String pluginName (const juce::uint8* data, size_t size, juce::String& error);
    juce::String pluginNameFromFile (const juce::File& f, juce::String& error);
}
