#pragma once
#include <JuceHeader.h>
#include <set>

// Content-routing configuration: where each kind of FL content should be copied.
// Stored as JSON; unknown keys are preserved on save so newer/older builds do
// not destroy each other's settings.
namespace RoutingConfig
{
    struct Destination
    {
        juce::String id, name;
        juce::StringArray extensions;           // lower-case with leading '.', empty = accepts anything
        juce::String root = "absolute";         // absolute | user_data | shared_data | fl_install
        juce::String path;
        bool enabled = true, automatic = true;
        juce::NamedValueSet extra;              // unknown keys (e.g. plugin_name, plugin_kind)

        bool accepts (const juce::File& f) const { return extensions.isEmpty() || extensions.contains (f.getFileExtension().toLowerCase()); }
        bool accepts (const juce::String& relPath) const { return extensions.isEmpty() || extensions.contains (juce::File::createFileWithoutCheckingPath ("/" + relPath).getFileExtension().toLowerCase()); }
        juce::String extraString (const juce::String& key) const { return extra.getWithDefault (key, {}).toString(); }
    };

    struct Config
    {
        juce::String userData, sharedData, flInstall;     // roots
        std::vector<Destination> destinations;
        juce::String lastSelection = "auto";
        int zipEntries = 10000;
        juce::int64 zipBytes = 2LL * 1024 * 1024 * 1024;
        int zipRatio = 200;
        bool alwaysReplaceDuplicates = false;
        int catalogRevision = 0;
        juce::NamedValueSet extra;

        juce::String& rootRef (const juce::String& name);
        const Destination* find (const juce::String& id) const;
    };

    // %APPDATA%\WAM\FLProjectOrganizer\routing.json (per-user; not beside the exe).
    juce::File defaultConfigPath();

    Config makeDefault();
    // Fills userData from UserDataLocator when blank and discoverable.
    void autoFillRoots (Config&);

    bool parse (const juce::var& json, Config& out, juce::String& error);
    juce::var toVar (const Config&);
    bool load (const juce::File& file, Config& out, juce::String& error);     // missing file => defaults
    bool save (const Config&, const juce::File& file, juce::String& error);   // validates, writes atomically

    // Resolves a destination to (root folder, target folder). Fails with a readable message
    // if the root is unset or the relative path is unsafe.
    bool resolve (const Config&, const Destination&, juce::File& base, juce::File& target, juce::String& error);
}
