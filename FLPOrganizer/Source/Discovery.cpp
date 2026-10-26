#include "Discovery.h"
#include <map>
#include <set>
#include <regex>

#if JUCE_WINDOWS
 #include <windows.h>
#endif

namespace Discovery
{
bool looksLikeInstall (const juce::File& f)
{
    return f.isDirectory() && f.getChildFile ("FL64.exe").existsAsFile() && f.getChildFile ("Plugins").isDirectory();
}

bool looksLikeUserDataRoot (const juce::File& f)
{
    return f.isDirectory() && f.getChildFile ("Settings").isDirectory()
        && (f.getChildFile ("Presets").isDirectory() || f.getChildFile ("Projects").isDirectory());
}

namespace
{
    // "Shared data" is an Image-Line parent; our routes use its "FL Studio" child.
    juce::File dataRoot (const juce::String& raw)
    {
        const juce::String expanded = raw.trim();
        if (! juce::File::isAbsolutePath (expanded)) return {};
        juce::File base (expanded);
        for (auto candidate : { base.getChildFile ("FL Studio"), base })
            if (looksLikeUserDataRoot (candidate)) return candidate;
        return {};
    }

    bool sameFolder (const juce::File& a, const juce::File& b)
    { return a.getFullPathName().equalsIgnoreCase (b.getFullPathName()); }

   #if JUCE_WINDOWS
    juce::String expandEnv (const juce::String& s)
    {
        wchar_t buf[4096] {};
        const DWORD n = ExpandEnvironmentStringsW (s.toWideCharPointer(), buf, (DWORD) std::size (buf));
        return (n > 0 && n < std::size (buf)) ? juce::String (buf) : s;
    }

    bool readString (HKEY hive, const juce::String& subKey, REGSAM view, const juce::String& valueName, juce::String& out)
    {
        HKEY key = nullptr;
        if (RegOpenKeyExW (hive, subKey.toWideCharPointer(), 0, KEY_READ | view, &key) != ERROR_SUCCESS) return false;
        DWORD type = 0, size = 0;
        bool ok = false;
        if (RegQueryValueExW (key, valueName.toWideCharPointer(), nullptr, &type, nullptr, &size) == ERROR_SUCCESS
            && (type == REG_SZ || type == REG_EXPAND_SZ) && size > 0 && size < 65536)
        {
            std::wstring buf (size / sizeof (wchar_t) + 1, L'\0');
            DWORD got = size;
            if (RegQueryValueExW (key, valueName.toWideCharPointer(), nullptr, &type,
                                  reinterpret_cast<LPBYTE> (buf.data()), &got) == ERROR_SUCCESS)
            {
                out = juce::String (buf.c_str());
                if (type == REG_EXPAND_SZ) out = expandEnv (out);
                ok = true;
            }
        }
        RegCloseKey (key);
        return ok;
    }

    std::vector<juce::String> subKeys (HKEY hive, const juce::String& subKey, REGSAM view)
    {
        std::vector<juce::String> names;
        HKEY key = nullptr;
        if (RegOpenKeyExW (hive, subKey.toWideCharPointer(), 0, KEY_READ | view, &key) != ERROR_SUCCESS) return names;
        for (DWORD i = 0;; ++i)
        {
            wchar_t name[256]; DWORD len = (DWORD) std::size (name);
            if (RegEnumKeyExW (key, i, name, &len, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) break;
            names.emplace_back (juce::String (name, (size_t) len));
        }
        RegCloseKey (key);
        return names;
    }
   #endif
}

std::vector<Candidate> registryCandidates (Kind kind)
{
    std::vector<Candidate> found;
   #if JUCE_WINDOWS
    struct Hive { const char* name; HKEY handle; };
    struct View { const char* name; REGSAM flag; };
    const Hive hives[] = { { "HKCU", HKEY_CURRENT_USER }, { "HKLM", HKEY_LOCAL_MACHINE } };
    const View views[] = { { "64-bit", KEY_WOW64_64KEY }, { "32-bit", KEY_WOW64_32KEY } };
    std::set<juce::String> seen;

    auto read = [&] (const Hive& h, const View& v, const juce::String& keyPath)
    {
        const juce::StringArray valueNames = kind == Kind::UserData ? juce::StringArray { "Shared data" }
                                                                    : juce::StringArray { "Install path", "FL Studio" };
        for (auto& valueName : valueNames)
        {
            juce::String value;
            if (! readString (h.handle, keyPath, v.flag, valueName, value)) continue;

            juce::File path, shared;
            if (kind == Kind::UserData)
            {
                path = dataRoot (value);
                if (path == juce::File()) continue;
                juce::File base (value.trim());
                if (sameFolder (path, base.getChildFile ("FL Studio"))) shared = base;
                else if (sameFolder (path, base) && path.getFileName().equalsIgnoreCase ("FL Studio")) shared = path.getParentDirectory();
            }
            else
            {
                juce::String raw = value.trim().unquoted().trim();
                if (! juce::File::isAbsolutePath (raw)) continue;
                path = juce::File (raw);
                if (valueName == "FL Studio")
                {
                    if (! path.getFileName().equalsIgnoreCase ("fl64.exe")) continue;
                    path = path.getParentDirectory();
                }
                if (! looksLikeInstall (path)) continue;
            }

            if (seen.insert (path.getFullPathName().toLowerCase()).second)
                found.push_back ({ path, juce::String (h.name) + "\\" + keyPath + " · " + valueName + " · " + v.name, shared });
        }
    };

    const juce::String sharedKey = "Software\\Image-Line\\Shared\\Paths";
    for (auto& h : hives) for (auto& v : views) read (h, v, sharedKey);
    if (! found.empty() && kind == Kind::UserData) return found;

    const juce::String parent = "Software\\Image-Line";
    static const std::regex versionKey ("FL Studio \\d+(\\.\\d+)*", std::regex::icase);
    for (auto& h : hives)
        for (auto& v : views)
        {
            std::vector<juce::String> versions;
            for (auto& n : subKeys (h.handle, parent, v.flag))
                if (std::regex_match (n.toStdString(), versionKey)) versions.push_back (n);
            std::sort (versions.begin(), versions.end(), [] (auto& a, auto& b) { return a.compareIgnoreCase (b) < 0; });

            for (auto& version : versions)
            {
                const auto versionPath = parent + "\\" + version;
                read (h, v, versionPath);
                for (auto& branch : subKeys (h.handle, versionPath, v.flag))       // only branches that really exist
                    if (branch.equalsIgnoreCase ("paths") || branch.equalsIgnoreCase ("general"))
                        read (h, v, versionPath + "\\" + branch);
            }
        }
   #else
    juce::ignoreUnused (kind);
   #endif
    return found;
}

std::vector<juce::File> installCandidates()
{
    std::vector<juce::File> bases;
    for (auto loc : { juce::File::globalApplicationsDirectory
                     #if JUCE_WINDOWS
                      , juce::File::globalApplicationsDirectoryX86
                     #endif
                    })
        bases.push_back (juce::File::getSpecialLocation (loc).getChildFile ("Image-Line"));

    std::vector<juce::File> out;
    std::set<juce::String> seen;
    for (auto& base : bases)
    {
        if (! base.isDirectory()) continue;
        for (auto& f : base.findChildFiles (juce::File::findDirectories, false, "FL Studio*"))
            if (looksLikeInstall (f) && seen.insert (f.getFullPathName().toLowerCase()).second)
                out.push_back (f);
    }
    std::sort (out.begin(), out.end(), [] (auto& a, auto& b) { return a.getFullPathName().compareIgnoreCase (b.getFullPathName()) < 0; });
    return out;
}

std::vector<Candidate> installationSuggestions()
{
    auto candidates = registryCandidates (Kind::FlInstall);
    for (auto& f : installCandidates())
        candidates.push_back ({ f, "Conventional installation suggestion", {} });

    std::vector<Candidate> unique;
    std::set<juce::String> seen;
    for (auto& c : candidates)
        if (seen.insert (c.path.getFullPathName().toLowerCase()).second) unique.push_back (c);

    // The folder name hints at the newest version; it is never proof of which one is active.
    auto versionOf = [] (const Candidate& c)
    {
        std::vector<int> v;
        std::smatch m;
        const auto name = c.path.getFileName().toStdString();
        static const std::regex re ("FL Studio[ -]*(\\d+(?:\\.\\d+)*)", std::regex::icase);
        if (std::regex_match (name, m, re))
        {
            juce::StringArray parts; parts.addTokens (juce::String (m[1].str()), ".", "");
            for (auto& p : parts) v.push_back (p.getIntValue());
        }
        return v;
    };
    std::stable_sort (unique.begin(), unique.end(), [&] (const Candidate& a, const Candidate& b)
    {
        const auto va = versionOf (a), vb = versionOf (b);
        if (va != vb) return va > vb;
        return a.path.getFullPathName().compareIgnoreCase (b.path.getFullPathName()) > 0;
    });
    return unique;
}

std::vector<juce::File> userSuggestions (bool includeRegistry)
{
    std::vector<juce::File> out;
    if (includeRegistry)
        for (auto& c : registryCandidates (Kind::UserData)) out.push_back (c.path);
    if (! out.empty()) return out;

    auto docs = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Image-Line");
    for (auto f : { docs.getChildFile ("FL Studio"), docs })
        if (f.isDirectory()) out.push_back (f);
    return out;
}

PluginFolderSearch findPluginFolders (const std::map<juce::String, juce::String>& roots, const juce::StringArray& names,
                                      const std::atomic<bool>& cancel, int maxEntries, int maxDepth)
{
    PluginFolderSearch result;
    std::map<juce::String, juce::String> wanted;                 // lower-case -> original
    for (auto& n : names) { wanted[n.toLowerCase()] = n; result.matches[n]; }

    std::set<juce::String> seen;
    const std::pair<const char*, const char*> kinds[] = {
        { "user_data", "User data (preferred)" }, { "shared_data", "Shared user data (preferred)" }, { "fl_install", "Installation folder" } };

    for (auto& [kind, label] : kinds)
    {
        auto it = roots.find (kind);
        if (it == roots.end() || it->second.isEmpty()) continue;
        juce::File base (it->second);
        if (! base.isDirectory()) { result.notes.add ("Folder does not exist: " + base.getFullPathName()); continue; }
        if (juce::String (kind) == "fl_install" && ! looksLikeInstall (base))
        { result.notes.add ("Not a verified FL Studio installation (needs FL64.exe and Plugins): " + base.getFullPathName()); continue; }
        if (base.isSymbolicLink()) { result.notes.add ("Link not followed: " + base.getFullPathName()); continue; }

        std::vector<std::pair<juce::File, int>> pending { { base, 0 } };
        int examined = 0;
        while (! pending.empty())
        {
            if (cancel.load()) { result.notes.add ("Cancelled."); return result; }
            auto [folder, depth] = pending.back(); pending.pop_back();
            if (! seen.insert (folder.getFullPathName().toLowerCase()).second) continue;

            if (auto w = wanted.find (folder.getFileName().toLowerCase()); w != wanted.end())
                result.matches[w->second].push_back ({ folder, label, {} });

            if (depth >= maxDepth) { result.notes.add ("Depth limit reached at " + folder.getFullPathName() + "; browse manually if needed."); continue; }

            for (const auto& entry : juce::RangedDirectoryIterator (folder, false, "*", juce::File::findDirectories))
            {
                if (cancel.load()) { result.notes.add ("Cancelled."); return result; }
                if (++examined > maxEntries) break;
                auto f = entry.getFile();
                if (! f.isSymbolicLink()) pending.push_back ({ f, depth + 1 });
            }
            if (examined > maxEntries)
            {
                result.notes.add ("Search limit reached beneath " + base.getFullPathName() + "; browse manually if needed.");
                break;
            }
        }
    }

    for (auto& [name, list] : result.matches)
        std::sort (list.begin(), list.end(), [] (const Candidate& a, const Candidate& b)
        {
            const bool ia = a.source == "Installation folder", ib = b.source == "Installation folder";
            if (ia != ib) return ! ia;
            return a.path.getFullPathName().compareIgnoreCase (b.path.getFullPathName()) < 0;
        });
    return result;
}
}
