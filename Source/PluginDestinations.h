#pragma once
#include <JuceHeader.h>

// Known plugin-preset destinations and FXP identity tables.
// Entries describe where FL Studio looks for each plugin's presets, relative to
// the user-data folder. Extend only with locations you have verified on a real
// install; a wrong entry misroutes files.
namespace PluginDestinations
{
    struct PluginPreset { const char* id; const char* name; const char* category; const char* folder; };
    inline const PluginPreset kPluginPresets[] = {
        { "autogun", "Autogun", "Generators", "Autogun" },
        { "directwave", "DirectWave", "Generators", "DirectWave" },
        { "fpc", "FPC", "Generators", "FPC" },
        { "harmless", "Harmless", "Generators", "Harmless" },
        { "harmor", "Harmor", "Generators", "Harmor" },
        { "kepler-exo", "Kepler EXO", "Generators", "Kepler Exo" },
        { "minisynth", "MiniSynth", "Generators", "MiniSynth" },
        { "morphine", "Morphine", "Generators", "Morphine" },
        { "ogun", "Ogun", "Generators", "Ogun" },
        { "patcher", "Patcher (Effect)", "Effects", "Patcher" },
        { "patcher-generator", "Patcher (Generator)", "Generators", "Patcher" },
        { "poizone", "PoiZone", "Generators", "PoiZone" },
        { "sawer", "Sawer", "Generators", "Sawer" },
        { "sytrus", "Sytrus", "Generators", "Sytrus" },
        { "toxic-biohazard", "Toxic Biohazard", "Generators", "Toxic Biohazard" },
        { "transistor-bass", "Transistor Bass", "Generators", "Transistor Bass" },
        { "wave-candy", "Wave Candy", "Effects", "Wave Candy" },
    };

    struct ChannelPreset { const char* id; const char* name; const char* path; };
    inline const ChannelPreset kChannelPresets[] = {
        { "3x-osc", "3x Osc channel states", "Data/Patches/Channel presets/3x Osc" },
    };

    // Extensions each plugin's presets may use (default: .fst only).
    inline juce::StringArray extensionsFor (const juce::String& id)
    {
        if (id == "morphine")        return { ".fst", ".mrp" };
        if (id == "toxic-biohazard") return { ".fst", ".txd", ".tbio" };
        if (id == "poizone")         return { ".fst", ".fxp" };
        if (id == "sawer")           return { ".fst", ".sawer" };
        return { ".fst" };
    }

    // Native (non-.fst/.fxp) preset extension -> plugin display name.
    inline juce::String nativePluginForExtension (const juce::String& ext)
    {
        auto e = ext.toLowerCase();
        for (auto& p : kPluginPresets)
            for (auto& x : extensionsFor (p.id))
                if (x != ".fst" && x != ".fxp" && x == e) return p.name;
        return {};
    }
    inline bool isNativePresetExtension (const juce::String& ext) { return nativePluginForExtension (ext).isNotEmpty(); }

    // VST2 IDs verified from real files (as 4 ASCII chars, file order).
    struct KnownFxp { const char* id; const char* name; const char* kind; };
    inline const KnownFxp kKnownFxp[] = { { "SawW", "PoiZone", "generator" } };

    // Alternate IDs seen in the wild that map to the registered one.
    struct FxpAlias { const char* from; const char* to; };
    inline const FxpAlias kFxpAliases[] = { { "H4M9", "g7U0" } };   // Albino3 preset vs. installed FST

    inline const KnownFxp* findKnownFxp (const juce::String& id4)
    { for (auto& k : kKnownFxp) if (id4 == k.id) return &k; return nullptr; }
    inline juce::String aliasFor (const juce::String& id4)
    { for (auto& a : kFxpAliases) if (id4 == a.from) return a.to; return {}; }

    constexpr int kCatalogRevision = 17;
}
