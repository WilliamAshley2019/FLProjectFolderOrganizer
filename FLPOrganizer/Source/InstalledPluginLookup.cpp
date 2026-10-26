#include "InstalledPluginLookup.h"
#include <set>
#include "UserDataLocator.h"
#include "PluginDestinations.h"

InstalledPluginLookup& InstalledPluginLookup::getInstance()
{
    static InstalledPluginLookup inst;
    return inst;
}

void InstalledPluginLookup::parseIniFile (const juce::File& iniFile, std::vector<Entry>& out)
{
    juce::StringArray lines;
    iniFile.readLines (lines);

    juce::String section;
    Entry cur;
    bool haveEntry = false;

    auto flush = [&]
    {
        if (haveEntry && cur.name.isNotEmpty())
        {
            cur.iniSection = section;
            if (section.containsIgnoreCase ("\\VST3\\"))      cur.formatType = "VST3";
            else if (section.containsIgnoreCase ("\\VST\\"))  cur.formatType = "VST";
            else if (section.containsIgnoreCase ("\\CLAP\\")) cur.formatType = "CLAP";
            out.push_back (cur);
        }
        cur = {};
        haveEntry = false;
    };

    for (const auto& raw : lines)
    {
        auto line = raw.trim();
        if (line.isEmpty()) continue;
        if (line.startsWithChar ('[') && line.endsWithChar (']'))
        {
            flush();
            section = line.substring (1, line.length() - 1);
            continue;
        }
        auto eq = line.indexOfChar ('=');
        if (eq <= 0) continue;
        auto key = line.substring (0, eq).trim();
        auto value = line.substring (eq + 1).trim();
        if (key == "ps_name")                       { cur.name = value; haveEntry = true; }
        else if (key == "ps_file_vendorname_0")     cur.vendorName = value;
        else if (key == "ps_file_category_0")       cur.category = value;
    }
    flush();
}

void InstalledPluginLookup::load (const juce::File& rootIn)
{
    juce::File root = rootIn;
    if (root == juce::File() || ! root.isDirectory())
        root = UserDataLocator::getPluginDatabaseRoot();

    std::unordered_map<juce::String, Entry> fresh;
    if (root != juce::File() && root.isDirectory())
    {
        juce::Array<juce::File> inis;
        // Versions differ on one consolidated .Plugins.ini vs split Effects/Generators.
        root.getChildFile ("Installed").findChildFiles (inis, juce::File::findFiles, true, "*.Plugins.ini");
        std::vector<Entry> parsed;
        for (auto& f : inis) parseIniFile (f, parsed);
        for (auto& e : parsed) fresh[e.name.toLowerCase()] = e;   // later files win
    }

    const juce::ScopedLock sl (mutex);
    entries = std::move (fresh);
    rootUsed = root;
    loaded = true;
    loading = false;
}

void InstalledPluginLookup::reload (const juce::File& root)
{
    { const juce::ScopedLock sl (mutex); loading = true; }
    load (root);
}

void InstalledPluginLookup::warmUp()
{
    {
        const juce::ScopedLock sl (mutex);
        if (loaded || loading) return;
        loading = true;
    }
    juce::Thread::launch ([this] { load ({}); });
}

void InstalledPluginLookup::ensureLoaded() const
{
    bool needLoad = false, wait = false;
    {
        const juce::ScopedLock sl (mutex);
        if (loaded) return;
        if (loading) wait = true; else needLoad = true;
    }
    if (needLoad) const_cast<InstalledPluginLookup*> (this)->reload();
    else if (wait)
    {   // a warm-up is running; poll briefly rather than parse twice
        for (int i = 0; i < 400; ++i)       // <= ~4 s
        {
            juce::Thread::sleep (10);
            const juce::ScopedLock sl (mutex);
            if (loaded) return;
        }
    }
}

std::optional<InstalledPluginLookup::Entry> InstalledPluginLookup::lookup (const juce::String& pluginName) const
{
    if (pluginName.isEmpty()) return std::nullopt;
    ensureLoaded();
    const juce::ScopedLock sl (mutex);
    auto it = entries.find (pluginName.toLowerCase());
    if (it == entries.end()) return std::nullopt;
    return it->second;
}

bool InstalledPluginLookup::isDatabaseAvailable() const { ensureLoaded(); const juce::ScopedLock sl (mutex); return ! entries.empty(); }
size_t InstalledPluginLookup::size() const              { ensureLoaded(); const juce::ScopedLock sl (mutex); return entries.size(); }
juce::File InstalledPluginLookup::getRootUsed() const   { const juce::ScopedLock sl (mutex); return rootUsed; }

std::vector<InstalledPluginLookup::Entry> InstalledPluginLookup::getAll() const
{
    ensureLoaded();
    const juce::ScopedLock sl (mutex);
    std::vector<Entry> v; v.reserve (entries.size());
    for (auto& kv : entries) v.push_back (kv.second);
    return v;
}


// ─── FST identity index ─────────────────────────────────────────────────────

juce::String InstalledPluginLookup::productKey (const juce::String& name)
{
    // u-he's VST3 factory name is "Diva(x64)" while its H2P says "Diva": drop only a
    // terminal architecture tag.
    auto s = name.trim();
    for (auto* tag : { "(x64)", "(x86)" })
    {
        if (s.endsWithIgnoreCase (tag)) { s = s.dropLastCharacters ((int) std::strlen (tag)).trim(); break; }
    }
    return s.toLowerCase();
}

juce::String InstalledPluginLookup::databaseNameFor (const juce::File& fst)
{
    auto nfo = fst.withFileExtension (".nfo");
    if (nfo.existsAsFile() && ! nfo.isSymbolicLink() && nfo.getSize() <= 64 * 1024)
    {
        juce::StringArray lines;
        nfo.readLines (lines);
        for (auto& l : lines)
        {
            auto eq = l.indexOfChar ('=');
            if (eq > 0 && l.substring (0, eq).trim().equalsIgnoreCase ("ps_name"))
            {
                auto v = l.substring (eq + 1).trim();
                bool printable = v.isNotEmpty() && v.length() <= 256;
                for (auto ch : v) if (! juce::CharacterFunctions::isPrintable (ch)) printable = false;
                if (printable) return v;
            }
        }
    }
    return fst.getFileNameWithoutExtension();
}

void InstalledPluginLookup::invalidateFstIndex()
{
    const juce::ScopedLock sl (fstMutex);
    fstBuilt = false;
    fstById.clear(); fstByProduct.clear(); fstNative.clear();
}

bool InstalledPluginLookup::buildFstIndex (const std::atomic<bool>* cancel)
{
    // caller holds fstMutex
    if (fstBuilt) return true;
    fstById.clear(); fstByProduct.clear(); fstNative.clear();

    auto installed = UserDataLocator::getInstalledPluginsFolder();
    if (installed != juce::File() && installed.isDirectory())
    {
        for (auto [category, kind] : { std::pair<const char*, const char*> { "Generators", "generator" },
                                       std::pair<const char*, const char*> { "Effects", "effect" } })
        {
            auto base = installed.getChildFile (category);
            if (! base.isDirectory() || base.isSymbolicLink()) continue;

            // Manual walk so links are never followed.
            std::vector<juce::File> stack { base };
            while (! stack.empty())
            {
                auto dir = stack.back(); stack.pop_back();
                for (const auto& entry : juce::RangedDirectoryIterator (dir, false, "*", juce::File::findFilesAndDirectories))
                {
                    if (cancel != nullptr && cancel->load()) return false;
                    auto f = entry.getFile();
                    if (f.isSymbolicLink()) continue;
                    if (entry.isDirectory()) { stack.push_back (f); continue; }
                    if (! f.hasFileExtension (".fst")) continue;

                    auto meta = FSTInspector::inspectFile (f);
                    if (! meta.ok() || meta.fstType != "plugin" || meta.pluginName.isEmpty()) continue;
                    FstEntry e { f, meta, kind };
                    if (meta.hostedPluginName.isNotEmpty())
                    {
                        fstByProduct[productKey (meta.hostedPluginName)].push_back (e);
                        if (meta.vst2Id != 0) fstById[meta.vst2Id].push_back (e);
                    }
                    else if (! FSTInspector::isWrapperName (meta.pluginName))
                        fstNative[meta.pluginName.toLowerCase()].push_back (e);
                }
            }
        }
    }
    fstBuilt = true;
    return true;
}

bool InstalledPluginLookup::finishResolve (const FstEntry& e, FstIdentity& out, juce::String& error)
{
    // Re-read so routing uses the file's current metadata, as the original tool did.
    auto fresh = FSTInspector::inspectFile (e.file);
    if (! fresh.ok()) { error = "Matched plugin FST could not be read: " + e.file.getFullPathName(); return false; }
    out.meta = fresh;
    out.meta.fstType = "plugin";
    out.file = e.file;
    out.kind = e.kind;
    out.databaseName = databaseNameFor (e.file);
    return true;
}

bool InstalledPluginLookup::resolveFxp (const FXPInspector::Metadata& fxp, FstIdentity& out, juce::String& error,
                                        const std::atomic<bool>* cancel)
{
    if (fxp.pluginId.isEmpty()) { error = fxp.reason.isNotEmpty() ? fxp.reason : "Invalid FXP header"; return false; }

    const juce::ScopedLock sl (fstMutex);
    if (! buildFstIndex (cancel)) { error = "Cancelled"; return false; }

    auto fmtId = [] (juce::uint32 v) { return "0x" + juce::String::toHexString ((int) v).toUpperCase().paddedLeft ('0', 8); };

    // FST payloads store the ID byte-reversed relative to the FXP's ASCII order.
    auto toKey = [] (const juce::String& id4)
    {
        return ((juce::uint32) (juce::uint8) id4[0] << 24) | ((juce::uint32) (juce::uint8) id4[1] << 16)
             | ((juce::uint32) (juce::uint8) id4[2] << 8) | (juce::uint32) (juce::uint8) id4[3];
    };

    juce::uint32 ident = toKey (fxp.pluginId);
    auto it = fstById.find (ident);
    if (it == fstById.end())
    {
        auto alias = PluginDestinations::aliasFor (fxp.pluginId);
        if (alias.isNotEmpty()) { ident = toKey (alias); it = fstById.find (ident); }
    }

    if (it == fstById.end() || it->second.empty())
    {
        // Native FL plugins have no VST ID: only bridge IDs already verified in the
        // catalogue, and only to the matching native FST.
        if (auto* known = PluginDestinations::findKnownFxp (fxp.pluginId))
        {
            auto n = fstNative.find (juce::String (known->name).toLowerCase());
            if (n != fstNative.end())
                for (auto& e : n->second)
                    if (e.kind == known->kind)
                    {
                        if (! finishResolve (e, out, error)) return false;
                        out.meta.pluginKind = known->kind;
                        return true;
                    }
        }
        error = "No installed-plugin FST matches FXP ID " + fmtId (ident)
              + ". Scan the plugin in FL Studio's Plugin Manager and try again.";
        return false;
    }

    std::set<std::pair<juce::String, juce::String>> identities;
    for (auto& e : it->second) identities.insert ({ e.meta.hostedPluginName.toLowerCase(), e.kind });
    if (identities.size() != 1)
    { error = "Multiple installed plugins match FXP ID " + fmtId (ident) + "; cannot determine a unique plugin."; return false; }

    if (! finishResolve (it->second.front(), out, error)) return false;
    if (out.meta.vst2Id != ident) { error = "Matched plugin FST no longer carries that ID: " + out.file.getFullPathName(); return false; }
    out.meta.pluginKind = out.kind;
    return true;
}

bool InstalledPluginLookup::resolveName (const juce::String& productName, FstIdentity& out, juce::String& error,
                                         const std::atomic<bool>* cancel)
{
    const juce::ScopedLock sl (fstMutex);
    if (! buildFstIndex (cancel)) { error = "Cancelled"; return false; }

    auto it = fstByProduct.find (productKey (productName));
    if (it == fstByProduct.end() || it->second.empty())
    {
        error = "No installed-plugin FST matches '" + productName + "'. Scan the plugin in FL Studio's Plugin Manager and try again.";
        return false;
    }
    std::set<std::pair<juce::String, juce::String>> identities;
    for (auto& e : it->second) identities.insert ({ productKey (e.meta.hostedPluginName), e.kind });
    if (identities.size() != 1) { error = "Conflicting installed-plugin FST matches for '" + productName + "'."; return false; }

    if (! finishResolve (it->second.front(), out, error)) return false;
    if (productKey (out.meta.hostedPluginName) != productKey (productName))
    { error = "Matched plugin FST could not be verified: " + out.file.getFullPathName(); return false; }
    out.meta.pluginKind = out.kind;
    return true;
}
