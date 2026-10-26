#pragma once
#include <JuceHeader.h>
#include <optional>
#include <unordered_map>
#include <atomic>
#include "FSTInspector.h"
#include "FXPInspector.h"

// Process-wide, thread-safe, read-only view of FL Studio's installed-plugin
// database (Installed\*.Plugins.ini). Replaces the per-instance parsing that
// FL::PluginDBLookup used to do every time a PluginInspector was constructed.
//
// Typical use:
//     InstalledPluginLookup::getInstance().warmUp();      // at startup, returns at once
//     ...
//     if (auto e = InstalledPluginLookup::getInstance().lookup ("Serum")) ...
//
// lookup() never blocks on a warm-up in progress for longer than the parse itself
// and returns nullopt ("unknown") when FL's database is absent. It never throws.
class InstalledPluginLookup
{
public:
    struct Entry
    {
        juce::String name;        // ps_name as FL shows it
        juce::String vendorName;
        juce::String category;
        juce::String formatType;  // "VST", "VST3", "CLAP", or {}
        juce::String iniSection;  // raw section header, for diagnostics
    };

    static InstalledPluginLookup& getInstance();

    // Kick off a background load; safe to call repeatedly (no-op if loaded/loading).
    void warmUp();
    // Synchronous (re)load. root = {} uses UserDataLocator.
    void reload (const juce::File& databaseRoot = {});

    std::optional<Entry> lookup (const juce::String& pluginName) const;
    bool isDatabaseAvailable() const;
    size_t size() const;
    std::vector<Entry> getAll() const;
    juce::File getRootUsed() const;

    // ---- FST-based identity resolution (for routing .fxp / .h2p presets) ----
    // Built lazily by reading every verified .fst under Installed\\Generators and
    // Installed\\Effects (bounded prefix reads only). Pass `cancel` to abort a long build.
    struct FstIdentity
    {
        FSTInspector::Metadata meta;   // outer/hosted identity of the database FST
        juce::File file;
        juce::String kind;             // "generator" / "effect" (from the folder it lives in)
        juce::String databaseName;     // Plugin Manager name (.nfo ps_name), else file stem
    };
    // Resolve an FXP header to an installed plugin. On failure returns false and fills `error`.
    bool resolveFxp (const FXPInspector::Metadata& fxp, FstIdentity& out, juce::String& error,
                     const std::atomic<bool>* cancel = nullptr);
    // Resolve a native preset's declared product name (e.g. from an H2P header).
    bool resolveName (const juce::String& productName, FstIdentity& out, juce::String& error,
                      const std::atomic<bool>* cancel = nullptr);
    void invalidateFstIndex();         // call at the start of each import batch

    // Parser exposed for tests / other callers (appends into `out`).
    static void parseIniFile (const juce::File& iniFile, std::vector<Entry>& out);

private:
    InstalledPluginLookup() = default;
    void ensureLoaded() const;
    void load (const juce::File& root);

    mutable juce::CriticalSection mutex;
    std::unordered_map<juce::String, Entry> entries;   // key: lower-case name
    juce::File rootUsed;
    // FST index (separate lock: building can take a while)
    struct FstEntry { juce::File file; FSTInspector::Metadata meta; juce::String kind; };
    juce::CriticalSection fstMutex;
    bool fstBuilt = false;
    std::unordered_map<juce::uint32, std::vector<FstEntry>> fstById;
    std::unordered_map<juce::String, std::vector<FstEntry>> fstByProduct;   // hosted names, product key
    std::unordered_map<juce::String, std::vector<FstEntry>> fstNative;      // outer names (non-wrapper)
    bool buildFstIndex (const std::atomic<bool>* cancel);
    static juce::String productKey (const juce::String& name);
    static juce::String databaseNameFor (const juce::File& fst);
    bool finishResolve (const FstEntry& e, FstIdentity& out, juce::String& error);
    mutable bool loaded = false;
    bool loading = false;
};
