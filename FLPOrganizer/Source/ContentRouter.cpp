#include "ContentRouter.h"
#include "PluginDestinations.h"
#include <set>

namespace ContentRouter
{
namespace
{
    juce::String ext (const juce::String& p) { return p.fromLastOccurrenceOf (".", true, false).toLowerCase(); }
    bool generic (const juce::String& n) { return n.isEmpty() || n.equalsIgnoreCase ("fruity wrapper") || n.equalsIgnoreCase ("fruit wrapper"); }
    bool isNativeExt (const juce::String& e) { return PluginDestinations::isNativePresetExtension (e); }

    bool isPrimary (const juce::String& e)
    {
        return juce::StringArray { ".flp", ".pyscript", ".zgeproj", ".mid", ".midi", ".fsc", ".fxp", ".h2p", ".chenv", ".fst" }.contains (e)
            || isNativeExt (e);
    }
    bool containsExt (const std::vector<const Destination*>&, const juce::String&) { return false; }
}

bool needsPluginName (const Package& p)
{
    auto e = ext (p.routePath());
    return juce::StringArray { ".fst", ".fxp", ".h2p" }.contains (e) && generic (p.pluginName);
}

std::vector<Package> fstPackageGroups (const Package& pkg, bool split)
{
    bool anyIdentified = false, hasFlp = false;
    for (auto& f : pkg.files) { if (ext (f) == ".flp") hasFlp = true; }
    for (auto& kv : pkg.info) if (kv.second.pluginName.isNotEmpty() || kv.second.fstType.isNotEmpty() || kv.second.error.isNotEmpty()) anyIdentified = true;
    if (! split || pkg.hasScripts || ! anyIdentified || hasFlp) return { pkg };

    // Files that carry a plugin identity vs. everything else.
    juce::StringArray presetFiles;
    for (auto& f : pkg.files) if (pkg.info.count (f) != 0) presetFiles.add (f);

    juce::StringArray companions, remaining;
    for (auto& f : pkg.files)
    {
        if (presetFiles.contains (f)) continue;
        (isPrimary (ext (f)) ? remaining : companions).add (f);
    }

    struct Key { juce::String name, kind, type, unique; bool operator< (const Key& o) const
        { return std::tie (name, kind, type, unique) < std::tie (o.name, o.kind, o.type, o.unique); } };
    std::map<Key, juce::StringArray> groups;
    std::map<Key, FileTransfer::FileInfo> infoOf;
    for (auto& f : presetFiles)
    {
        auto& fi = pkg.info.at (f);
        const bool gen = generic (fi.pluginName);
        Key k { fi.pluginName.toLowerCase(),
                (gen || fi.pluginName.equalsIgnoreCase ("patcher")) ? fi.pluginKind : juce::String(),
                fi.fstType.isNotEmpty() ? fi.fstType
                    : ((isNativeExt (ext (f)) || ext (f) == ".fxp" || ext (f) == ".h2p") ? juce::String ("plugin") : juce::String()),
                gen ? f : juce::String() };         // unidentified files each get their own group
        groups[k].add (f);
        infoOf[k] = fi;
    }

    std::vector<Package> result;
    for (auto& [k, paths] : groups)
    {
        Package g = pkg;
        auto& fi = infoOf[k];
        g.hasSelection = true;
        g.selectedFiles = paths; g.selectedFiles.addArray (companions);
        g.pluginName = fi.pluginName; g.pluginKind = k.kind; g.fstType = k.type;
        g.displayName = fi.displayName; g.hostedName = fi.hostedName; g.databaseName = fi.databaseName;
        auto title = fi.pluginName.isNotEmpty() ? fi.pluginName : juce::String ("Unidentified FST presets");
        if (k.kind.isNotEmpty()) title = fi.pluginName + " (" + k.kind.substring (0, 1).toUpperCase() + k.kind.substring (1) + ")";
        if (k.type == "channel") title << " - Channel state";
        g.groupName = title;
        g.isFstGroup = true;
        result.push_back (std::move (g));
    }
    if (! remaining.isEmpty())
    {
        Package g = pkg;
        g.hasSelection = true;
        g.selectedFiles = remaining; g.selectedFiles.addArray (companions);
        g.groupName = "Other content";
        result.push_back (std::move (g));
    }
    return result;
}

juce::String pluginDestinationId (const juce::String& pluginName)
{
    if (pluginName.isEmpty()) return {};
    if (pluginName.equalsIgnoreCase ("zgameeditor visualizer")) return "visualizer";
    for (auto& p : PluginDestinations::kPluginPresets)
        if (pluginName.equalsIgnoreCase (p.name)) return "preset-" + juce::String (p.id);
    return {};
}

bool matchesPlugin (const Destination& d, const juce::String& pluginName, const juce::String& pluginKind, const juce::String& fstType)
{
    if (fstType == "channel" && pluginName == "Channel preset") return d.id == "channel-presets";
    if (d.id == "channel-presets") return false;
    if (fstType == "mixer") return d.id == "mixer";
    if (d.id == "mixer") return false;

    auto tag = d.extraString ("plugin_name");
    auto destKind = d.extraString ("plugin_kind");
    if (pluginKind.isNotEmpty() && destKind.isNotEmpty() && destKind != pluginKind) return false;

    if (pluginName.removeCharacters (" ").equalsIgnoreCase ("3xosc") && fstType == "channel") return d.id == "channel-3x-osc";
    if (d.id == "channel-3x-osc") return false;

    if (pluginName.equalsIgnoreCase ("patcher"))
    {
        juce::String idKind = d.id == "preset-patcher" ? "effect" : d.id == "preset-patcher-generator" ? "generator" : juce::String();
        const bool identity = idKind.isNotEmpty() || tag.equalsIgnoreCase ("patcher")
            || juce::StringArray { "patcher", "patcher (effect)", "patcher (generator)" }.contains (d.name.toLowerCase());
        auto kind = destKind.isNotEmpty() ? destKind : idKind;
        if (kind.isEmpty())
        {
            auto parts = juce::StringArray::fromTokens (d.path.replaceCharacter ('\\', '/').toLowerCase(), "/", "");
            kind = parts.contains ("generators") ? "generator" : parts.contains ("effects") ? "effect" : juce::String();
        }
        return identity && (pluginKind.isEmpty() || kind == pluginKind);
    }
    return d.id == pluginDestinationId (pluginName) || (tag.isNotEmpty() && tag.equalsIgnoreCase (pluginName))
        || d.name.equalsIgnoreCase (pluginName);
}

std::vector<const Destination*> compatible (const Config& c, const juce::String& relPath, bool automatic,
                                            const juce::String& pluginName, const juce::String& pluginKind, const juce::String& fstType)
{
    std::vector<const Destination*> choices;
    for (auto& d : c.destinations)
        if (d.enabled && d.accepts (relPath) && (! automatic || (d.automatic && ! d.extensions.isEmpty())))
            choices.push_back (&d);

    auto filter = [&] (auto pred) { std::vector<const Destination*> r; for (auto* d : choices) if (pred (*d)) r.push_back (d); return r; };
    const auto e = ext (relPath);

    if (automatic && e == ".fst" && fstType == "mixer") return filter ([] (const Destination& d) { return d.id == "mixer"; });
    if (automatic && e == ".fst")
    {
        choices = filter ([&] (const Destination& d)
        { return d.id != "mixer" && (d.id != "channel-presets" || (fstType == "channel" && pluginName == "Channel preset")); });
        if (pluginName.isNotEmpty())
            return filter ([&] (const Destination& d) { return matchesPlugin (d, pluginName, pluginKind, fstType); });
    }
    if (automatic && (e == ".fxp" || e == ".h2p"))
        return filter ([&] (const Destination& d) { return pluginName.isNotEmpty() && matchesPlugin (d, pluginName, pluginKind); });
    if (automatic && e == ".chenv") return filter ([] (const Destination& d) { return d.id == "channel-envelopes"; });
    if (automatic && (e == ".fsc" || e == ".mid" || e == ".midi")) return filter ([] (const Destination& d) { return d.id == "scores"; });
    if (automatic && isNativeExt (e))
        return filter ([&] (const Destination& d) { return matchesPlugin (d, PluginDestinations::nativePluginForExtension (e), "generator"); });
    return choices;
}

std::vector<const Destination*> packageChoices (const Config& c, const Package& p, bool automatic)
{
    if (p.hasScripts)
    {
        // Script packages route by the type their imports reveal; never guess when it is undecided.
        std::vector<const Destination*> out;
        for (auto& d : c.destinations)
        {
            if (! d.enabled || (automatic && ! d.automatic)) continue;
            const bool isScriptDest = d.id == "piano" || d.id == "edison" || d.id == "hardware";
            if (automatic)
            {
                if (p.scriptKind.isNotEmpty() && ! (p.scriptKind == "hardware" && p.hardwareNested)
                    && (d.id == p.scriptKind || d.extraString ("script_type") == p.scriptKind))
                    out.push_back (&d);
            }
            else
            {
                if (p.scriptKind.isNotEmpty() && isScriptDest && d.id != p.scriptKind) continue;
                bool accepts = d.extensions.isEmpty();
                for (auto& f : p.plannedFiles()) if (d.accepts (f)) { accepts = true; break; }
                if (accepts) out.push_back (&d);
            }
        }
        return out;
    }

    std::set<juce::String> primary;
    for (auto& f : p.plannedFiles()) if (isPrimary (ext (f))) primary.insert (ext (f));

    auto onlyIn = [&] (std::initializer_list<const char*> allowed)
    { for (auto& e : primary) { bool ok = false; for (auto* a : allowed) if (e == a) ok = true; if (! ok) return false; } return ! primary.empty(); };

    if (automatic && onlyIn ({ ".chenv" })) return compatible (c, "envelope.chenv", true);
    if (automatic && onlyIn ({ ".fsc", ".mid", ".midi" })) return compatible (c, "score.fsc", true);
    return compatible (c, p.routePath(), automatic, p.pluginName, p.pluginKind, p.fstType);
}

std::vector<Package> groupLooseScripts (std::vector<Package> packages, const std::atomic<bool>* cancel)
{
    std::map<juce::String, std::vector<size_t>> byParent;
    for (size_t i = 0; i < packages.size(); ++i)
    {
        auto& p = packages[i];
        if (p.source.existsAsFile() && ! p.isZip && (p.source.hasFileExtension (".py") || p.source.hasFileExtension (".pyscript")))
            byParent[p.source.getParentDirectory().getFullPathName()].push_back (i);
    }

    std::set<size_t> merged;
    std::vector<Package> combinedPackages;
    for (auto& [parent, idx] : byParent)
    {
        if (idx.size() < 2) continue;
        Package combined;
        combined.source = juce::File (parent);
        for (auto i : idx)
        {
            combined.files.add (packages[i].source.getFileName());
            merged.insert (i);
        }
        combined.hasSelection = true;
        combined.selectedFiles = combined.files;
        FileTransfer::inspectScriptFiles (combined, cancel);

        juce::StringArray entries;
        for (auto& f : combined.files)
            if (f.endsWithIgnoreCase (".pyscript") || f.startsWithIgnoreCase ("device_")) entries.add (f);
        if (entries.size() == 1)
        {
            auto stem = juce::File::createFileWithoutCheckingPath ("/" + entries[0]).getFileNameWithoutExtension();
            if (stem.startsWithIgnoreCase ("device_")) stem = stem.substring (7);
            combined.destinationSubfolder = stem.isEmpty() ? juce::String ("Scripts") : stem;
        }
        combinedPackages.push_back (std::move (combined));
    }

    std::vector<Package> result;
    for (size_t i = 0; i < packages.size(); ++i)
        if (merged.count (i) == 0) result.push_back (std::move (packages[i]));
    for (auto& c : combinedPackages) result.push_back (std::move (c));
    return result;
}

bool packageMatches (const Config& c, const juce::StringArray& paths, const Destination& dest, juce::StringArray* bad)
{
    if (dest.extensions.isEmpty()) return true;
    std::set<juce::String> recognised;
    for (auto& d : c.destinations) for (auto& e : d.extensions) recognised.insert (e);
    recognised.erase (".zip");

    juce::StringArray present;
    for (auto& p : paths) if (isPrimary (ext (p))) present.add (p);
    if (present.isEmpty()) for (auto& p : paths) if (recognised.count (ext (p))) present.add (p);

    juce::StringArray offenders;
    for (auto& p : present) if (! dest.accepts (p)) offenders.add (p);
    if (bad) *bad = offenders;
    return ! present.isEmpty() && offenders.isEmpty();
}
}
