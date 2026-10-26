#include "ConfigManager.h"
#include "PluginDestinations.h"
#include "SafeFileOperations.h"
#include "UserDataLocator.h"

namespace RoutingConfig
{
using namespace PluginDestinations;

juce::String& Config::rootRef (const juce::String& name)
{ return name == "user_data" ? userData : name == "shared_data" ? sharedData : flInstall; }

const Destination* Config::find (const juce::String& id) const
{ for (auto& d : destinations) if (d.id == id) return &d; return nullptr; }

juce::File defaultConfigPath()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("WAM").getChildFile ("FLProjectOrganizer").getChildFile ("routing.json");
}

static Destination mk (const char* id, const char* name, juce::StringArray ext, const char* root = "absolute", juce::String path = {})
{
    Destination d; d.id = id; d.name = name; d.extensions = std::move (ext); d.root = root; d.path = std::move (path);
    return d;
}

static std::vector<Destination> pluginDests()
{
    std::vector<Destination> v;
    for (auto& p : kPluginPresets)
        v.push_back (mk (("preset-" + juce::String (p.id)).toRawUTF8(), p.name, extensionsFor (p.id), "user_data",
                         "Presets/Plugin presets/" + juce::String (p.category) + "/" + p.folder));
    return v;
}
static std::vector<Destination> channelDests()
{
    std::vector<Destination> v;
    for (auto& c : kChannelPresets)
        v.push_back (mk (("channel-" + juce::String (c.id)).toRawUTF8(), c.name, { ".fst" }, "fl_install", c.path));
    return v;
}

Config makeDefault()
{
    Config c;
    c.destinations = {
        mk ("piano",   "Piano Roll scripts",       { ".pyscript", ".py" }, "user_data", "Settings/Piano roll scripts"),
        mk ("edison",  "Edison audio scripts",     { ".pyscript", ".py" }, "user_data", "Settings/Audio scripts"),
        mk ("hardware","MIDI hardware scripts",    { ".py" },              "user_data", "Settings/Hardware"),
        mk ("zge",     "ZGameEditor objects (.zgeproj)", { ".zgeproj" },   "shared_data", "ZGameEditor Visualizer/Effects"),
        mk ("visualizer", "Visualizer plugin presets", { ".fst" },         "user_data", "Presets/Plugin presets/Effects/ZGameEditor Visualizer"),
        mk ("channel-presets", "Channel presets",  { ".fst" },             "user_data", "Presets/Channel presets"),
        mk ("channel-envelopes", "Channel envelopes", { ".chenv" },        "user_data", "Presets/Channel envelopes"),
        mk ("mixer",   "Mixer presets",            { ".fst" },             "user_data", "Presets/Mixer presets"),
        mk ("generator", "Generator presets",      { ".fst" },             "user_data", "Presets/Plugin presets/Generators"),
        mk ("effect",  "Effect presets",           { ".fst" },             "user_data", "Presets/Plugin presets/Effects"),
        mk ("samples", "Samples (choose an organization folder)", { ".wav", ".aif", ".aiff", ".mp3", ".ogg", ".flac" }),
        mk ("scores",  "Scores",                   { ".fsc", ".mid", ".midi" }, "user_data", "Presets/Scores"),
        mk ("projects", "FL Studio projects (choose a folder)", { ".flp", ".zip" }),
    };
    for (auto& d : pluginDests())  c.destinations.push_back (d);
    for (auto& d : channelDests()) c.destinations.push_back (d);
    c.catalogRevision = kCatalogRevision;
    return c;
}

void autoFillRoots (Config& c)
{
    if (c.userData.isEmpty())
    {
        auto f = UserDataLocator::findUserDataFolder();
        if (f != juce::File()) c.userData = f.getFullPathName();
    }
}

static bool validRelative (const juce::String& p)
{
    return p.isEmpty() || SafeFileOperations::IsSafeRelativePath (p);
}

bool parse (const juce::var& json, Config& out, juce::String& err)
{
    auto* root = json.getDynamicObject();
    if (root == nullptr) { err = "Configuration must be a JSON object."; return false; }

    Config c;
    const int version = (int) root->getProperty ("version");
    if (version != 1 && root->hasProperty ("version")) { err = "Unsupported configuration version: " + juce::String (version); return false; }

    if (auto* roots = root->getProperty ("roots").getDynamicObject())
        for (auto& nv : roots->getProperties())
        {
            if (! nv.value.isString()) { err = "Roots must contain folder strings."; return false; }
            auto v = nv.value.toString();
            if (v.isNotEmpty() && ! juce::File::isAbsolutePath (v)) { err = "Root must be absolute: " + v; return false; }
            auto k = nv.name.toString();
            if (k == "user_data") c.userData = v; else if (k == "shared_data") c.sharedData = v; else if (k == "fl_install") c.flInstall = v;
        }

    std::set<juce::String> ids;
    auto* dests = root->getProperty ("destinations").getArray();
    if (root->hasProperty ("destinations") && dests == nullptr) { err = "Destinations must be a list."; return false; }
    if (dests != nullptr)
        for (auto& item : *dests)
        {
            auto* o = item.getDynamicObject();
            if (o == nullptr) { err = "Every destination must be an object."; return false; }
            Destination d;
            d.id = o->getProperty ("id").toString();
            d.name = o->getProperty ("name").toString();
            d.root = o->hasProperty ("root") ? o->getProperty ("root").toString() : "absolute";
            d.path = o->getProperty ("path").toString();
            if (o->hasProperty ("enabled"))   { if (! o->getProperty ("enabled").isBool())   { err = "Enabled and automatic must be booleans."; return false; } d.enabled = (bool) o->getProperty ("enabled"); }
            if (o->hasProperty ("automatic")) { if (! o->getProperty ("automatic").isBool()) { err = "Enabled and automatic must be booleans."; return false; } d.automatic = (bool) o->getProperty ("automatic"); }
            if (d.id.isEmpty() || d.id == "auto" || ! ids.insert (d.id).second) { err = "Destination IDs must be unique, nonempty, and not 'auto'."; return false; }
            if (d.name.trim().isEmpty()) { err = "Destination names cannot be empty."; return false; }

            std::set<juce::String> exts;
            if (auto* ea = o->getProperty ("extensions").getArray())
                for (auto& e : *ea)
                {
                    auto s = e.toString().trim().toLowerCase().trimCharactersAtStart (".");
                    if (s.isEmpty() || s.containsAnyOf ("/\\:*? ")) { err = "Invalid extensions for " + d.name + "."; return false; }
                    exts.insert ("." + s);
                }
            for (auto& s : exts) d.extensions.add (s);
            if (d.extensions.isEmpty()) d.automatic = false;

            if (! juce::StringArray { "absolute", "user_data", "shared_data", "fl_install" }.contains (d.root))
            { err = "Invalid root for " + d.name + "."; return false; }
            if (d.root != "absolute" && ! validRelative (d.path)) { err = "Destination path must stay beneath its root: " + d.name; return false; }
            if (d.root == "absolute" && d.path.isNotEmpty() && ! juce::File::isAbsolutePath (d.path))
            { err = "Absolute destination requires an absolute path: " + d.name; return false; }

            for (auto& nv : o->getProperties())
            {
                auto k = nv.name.toString();
                if (! juce::StringArray { "id", "name", "extensions", "root", "path", "enabled", "automatic" }.contains (k))
                    d.extra.set (nv.name, nv.value);
            }
            c.destinations.push_back (std::move (d));
        }

    if (auto* z = root->getProperty ("zip_limits").getDynamicObject())
    {
        auto g = [&] (const char* k, juce::int64 def) { return z->hasProperty (k) ? (juce::int64) z->getProperty (k) : def; };
        c.zipEntries = (int) g ("entries", c.zipEntries);
        c.zipBytes = g ("bytes", c.zipBytes);
        c.zipRatio = (int) g ("ratio", c.zipRatio);
        if (c.zipEntries <= 0 || c.zipBytes <= 0 || c.zipRatio <= 0) { err = "ZIP limits must be positive integers."; return false; }
    }
    if (root->hasProperty ("last_selection")) c.lastSelection = root->getProperty ("last_selection").toString();
    if (root->hasProperty ("always_replace_duplicates"))
    {
        if (! root->getProperty ("always_replace_duplicates").isBool()) { err = "Always replace duplicates must be a boolean."; return false; }
        c.alwaysReplaceDuplicates = (bool) root->getProperty ("always_replace_duplicates");
    }
    c.catalogRevision = (int) root->getProperty ("catalog_revision");
    for (auto& nv : root->getProperties())
        if (! juce::StringArray { "version", "roots", "destinations", "last_selection", "zip_limits",
                                  "always_replace_duplicates", "catalog_revision" }.contains (nv.name.toString()))
            c.extra.set (nv.name, nv.value);

    out = std::move (c);
    return true;
}

juce::var toVar (const Config& c)
{
    auto* root = new juce::DynamicObject();
    for (auto& nv : c.extra) root->setProperty (nv.name, nv.value);
    root->setProperty ("version", 1);
    root->setProperty ("catalog_revision", c.catalogRevision);

    auto* roots = new juce::DynamicObject();
    roots->setProperty ("user_data", c.userData);
    roots->setProperty ("shared_data", c.sharedData);
    roots->setProperty ("fl_install", c.flInstall);
    root->setProperty ("roots", juce::var (roots));

    juce::Array<juce::var> ds;
    for (auto& d : c.destinations)
    {
        auto* o = new juce::DynamicObject();
        for (auto& nv : d.extra) o->setProperty (nv.name, nv.value);
        o->setProperty ("id", d.id); o->setProperty ("name", d.name);
        juce::Array<juce::var> ex; for (auto& e : d.extensions) ex.add (e);
        o->setProperty ("extensions", ex);
        o->setProperty ("root", d.root); o->setProperty ("path", d.path);
        o->setProperty ("enabled", d.enabled); o->setProperty ("automatic", d.automatic);
        ds.add (juce::var (o));
    }
    root->setProperty ("destinations", ds);
    root->setProperty ("last_selection", c.lastSelection);
    auto* z = new juce::DynamicObject();
    z->setProperty ("entries", c.zipEntries); z->setProperty ("bytes", c.zipBytes); z->setProperty ("ratio", c.zipRatio);
    root->setProperty ("zip_limits", juce::var (z));
    root->setProperty ("always_replace_duplicates", c.alwaysReplaceDuplicates);
    return juce::var (root);
}

// Adds catalogue entries introduced after the revision a saved file was written
// with, without touching anything the user customised or deleted since.
static void migrate (Config& c)
{
    if (c.catalogRevision >= kCatalogRevision) return;
    if (c.catalogRevision == 0)                    // pre-catalogue file: add the plugin routes once
    {
        for (auto& d : pluginDests())
            if (! c.find (d.id)) c.destinations.push_back (d);
    }
    else
    {
        // Later revisions only ever added these specific routes.
        for (auto id : { "preset-patcher-generator", "preset-fpc", "preset-morphine", "preset-toxic-biohazard",
                         "preset-sawer", "preset-poizone" })
            for (auto& d : pluginDests())
                if (d.id == id && ! c.find (id)) c.destinations.push_back (d);
        for (auto& d : channelDests())
            if (! c.find (d.id)) c.destinations.push_back (d);
        for (auto id : { "mixer", "channel-presets", "channel-envelopes", "scores" })
            for (auto& d : makeDefault().destinations)
                if (d.id == id && ! c.find (id)) c.destinations.push_back (d);
    }
    c.catalogRevision = kCatalogRevision;
}

bool load (const juce::File& file, Config& out, juce::String& err)
{
    if (! file.existsAsFile()) { out = makeDefault(); autoFillRoots (out); return true; }
    auto text = file.loadFileAsString();
    juce::var json;
    auto res = juce::JSON::parse (text, json);
    if (res.failed()) { err = "Cannot load " + file.getFullPathName() + ": " + res.getErrorMessage() + ". The original file has been left untouched."; return false; }
    Config c;
    if (! parse (json, c, err)) { err = "Cannot load " + file.getFullPathName() + ": " + err + ". The original file has been left untouched."; return false; }
    migrate (c);
    out = std::move (c);
    return true;
}

bool save (const Config& c, const juce::File& file, juce::String& err)
{
    Config check;
    if (! parse (toVar (c), check, err)) return false;           // never write something we could not read back
    file.getParentDirectory().createDirectory();
    auto tmp = file.getSiblingFile (file.getFileName() + ".tmp");
    if (! tmp.replaceWithText (juce::JSON::toString (toVar (check), false)))
    { err = "Could not write " + tmp.getFullPathName(); return false; }
    if (! tmp.moveFileTo (file)) { tmp.deleteFile(); err = "Could not replace " + file.getFullPathName(); return false; }
    return true;
}

bool resolve (const Config& c, const Destination& d, juce::File& base, juce::File& target, juce::String& err)
{
    if (d.root == "absolute")
    {
        if (d.path.isEmpty() || ! juce::File::isAbsolutePath (d.path))
        { err = "Choose an absolute folder for " + d.name + " in Settings."; return false; }
        base = target = juce::File (d.path);
        return true;
    }
    auto raw = d.root == "user_data" ? c.userData : d.root == "shared_data" ? c.sharedData : c.flInstall;
    if (raw.isEmpty()) { err = "Set the " + d.root.replaceCharacter ('_', ' ') + " folder in Settings."; return false; }
    if (! validRelative (d.path)) { err = "Unsafe destination path for " + d.name; return false; }
    base = juce::File (raw);
    target = d.path.isEmpty() ? base : base.getChildFile (d.path.replaceCharacter ('\\', '/'));
    return true;
}
}
