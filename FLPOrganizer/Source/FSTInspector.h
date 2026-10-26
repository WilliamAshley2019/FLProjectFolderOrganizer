#pragma once
#include <JuceHeader.h>
#include "flp.h"

// Read-only inspector for FL Studio .fst preset files (plugin state, channel
// state, mixer-insert state). Never loads a plugin and never trusts a length
// field beyond the first kMaxPrefixBytes of the file.
//
// Identity rules (ported from FL Dropper's fst.py):
//  * The OUTER PluginName event is authoritative.
//  * Only when the outer name is "Fruity Wrapper" / "Fruit Wrapper" (or absent)
//    is the PluginParams (213) payload consulted for the hosted VST2 ID/name.
//  * Mixer-insert presets short-circuit: their identity is "Mixer preset".
//
// FLhd "format" handling: 0 = project (rejected), 0x20 = channel state,
// 0x40 = mixer insert, anything else = plugin state. The two exact constants live
// at the top of FSTInspector.cpp; confirm them against a few real .fst files.
class FSTInspector
{
public:
    struct Metadata
    {
        juce::String pluginName;       // outer PluginName, or "Mixer preset"/"Channel preset"
        juce::String pluginFactory;    // outer PluginFactory (internal name)
        juce::String flVersion;        // from the Version event, if present
        juce::String encoding;         // "utf-16-le" or "ascii" (derived from FLVersion)
        juce::String reason;           // non-empty => not usable; pluginName unreliable

        juce::String hostedPluginName; // wrapper only: VST2 name from the payload
        juce::uint32 vst2Id = 0;       // wrapper only: VST2 ID (0 = not found)

        juce::String displayName;      // PluginName event: the (renameable) channel label, not an identity
        juce::String pluginKind;       // "generator"/"effect" from the wrapper flags (mixer = "effect")
        juce::String fstType;          // "plugin" / "channel" / "mixer"

        bool ok() const { return reason.isEmpty(); }
        // The name to look up in the plugin database.
        juce::String effectiveName() const
        { return hostedPluginName.isNotEmpty() ? hostedPluginName : pluginName; }
        // vst2Id as the 4-character code FL/VST2 plugins use (e.g. "SawW"), or {}.
        juce::String vst2IdString() const;
    };

    // fileSize enables the "declared FLdt length == file size" cross-check;
    // pass 0 for loose in-memory buffers.
    static Metadata inspectBytes (const juce::uint8* data, size_t size, juce::int64 fileSize = 0);
    static Metadata inspectFile (const juce::File& f);

    static bool isWrapperName (const juce::String& name);
    static constexpr int kMaxPrefixBytes = 64 * 1024;
};
