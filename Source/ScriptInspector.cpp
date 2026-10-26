#include "ScriptInspector.h"
#include <set>

namespace ScriptInspector
{
namespace
{
    // ── source cleaning ────────────────────────────────────────────────────
    // Blank out comments and string literals (keeping newlines) so that import-looking
    // text inside docstrings or strings is never mistaken for a statement.
    juce::String stripCommentsAndStrings (const juce::String& src)
    {
        juce::String out;
        out.preallocateBytes ((size_t) src.getNumBytesAsUTF8() + 8);
        const int n = src.length();
        int i = 0;
        while (i < n)
        {
            const auto c = src[i];
            if (c == '#')
            {
                while (i < n && src[i] != '\n') ++i;
                continue;
            }
            if (c == '"' || c == '\'')
            {
                const bool triple = i + 2 < n && src[i + 1] == c && src[i + 2] == c;
                i += triple ? 3 : 1;
                while (i < n)
                {
                    if (src[i] == '\\') { i += 2; continue; }
                    if (triple) { if (src[i] == c && i + 2 < n + 0 && src[i + 1] == c && src[i + 2] == c) { i += 3; break; } }
                    else if (src[i] == c || src[i] == '\n') { if (src[i] == c) ++i; break; }
                    if (src[i] == '\n') out += "\n";
                    ++i;
                }
                out += " ";
                continue;
            }
            out += juce::String::charToString (c);
            ++i;
        }
        return out;
    }

    // Joins physical lines into logical lines (backslash continuation and open brackets).
    juce::StringArray logicalLines (const juce::String& cleaned)
    {
        juce::StringArray lines;
        juce::String cur;
        int depth = 0;
        const int n = cleaned.length();
        for (int i = 0; i < n; ++i)
        {
            const auto c = cleaned[i];
            if (c == '\\' && i + 1 < n && (cleaned[i + 1] == '\n' || (cleaned[i + 1] == '\r')))
            {
                ++i;
                if (cleaned[i] == '\r' && i + 1 < n && cleaned[i + 1] == '\n') ++i;
                cur += " ";
                continue;
            }
            if (c == '(' || c == '[' || c == '{') ++depth;
            else if (c == ')' || c == ']' || c == '}') depth = juce::jmax (0, depth - 1);

            if ((c == '\n' || c == '\r') && depth == 0)
            {
                lines.add (cur);
                cur.clear();
                continue;
            }
            cur += juce::String::charToString ((c == '\n' || c == '\r') ? (juce::juce_wchar) ' ' : (juce::juce_wchar) c);
        }
        if (cur.isNotEmpty()) lines.add (cur);
        return lines;
    }

    bool startsWithWord (const juce::String& s, const char* word)
    {
        const int len = (int) std::strlen (word);
        return s.startsWith (word) && (s.length() == len || ! (juce::CharacterFunctions::isLetterOrDigit (s[len]) || s[len] == '_'));
    }

    // Splits a statement list on top-level ';'. Strings are already blanked.
    juce::StringArray splitStatements (const juce::String& line)
    {
        juce::StringArray out;
        juce::String cur;
        int depth = 0;
        for (auto c : line)
        {
            if (c == '(' || c == '[' || c == '{') ++depth;
            else if (c == ')' || c == ']' || c == '}') depth = juce::jmax (0, depth - 1);
            if (c == ';' && depth == 0) { out.add (cur.trim()); cur.clear(); }
            else cur += juce::String::charToString (c);
        }
        out.add (cur.trim());
        out.removeEmptyStrings();
        return out;
    }

    // "try: import x" / "if y: from a import b" -> the part after the block's colon.
    juce::String afterBlockColon (const juce::String& stmt)
    {
        static const char* blockWords[] = { "if", "elif", "else", "try", "except", "finally", "with", "for", "while" };
        bool isBlock = false;
        for (auto* w : blockWords) if (startsWithWord (stmt, w)) { isBlock = true; break; }
        if (! isBlock) return stmt;
        int depth = 0;
        for (int i = 0; i < stmt.length(); ++i)
        {
            const auto c = stmt[i];
            if (c == '(' || c == '[' || c == '{') ++depth;
            else if (c == ')' || c == ']' || c == '}') depth = juce::jmax (0, depth - 1);
            else if (c == ':' && depth == 0) return stmt.substring (i + 1).trim();
        }
        return {};
    }

    juce::StringArray commaItems (juce::String list)
    {
        list = list.replace ("(", " ").replace (")", " ");
        juce::StringArray items;
        items.addTokens (list, ",", "");
        juce::StringArray out;
        for (auto& it : items)
        {
            auto t = it.trim();
            if (t.isEmpty()) continue;
            // "name as alias" -> name
            juce::StringArray words;
            words.addTokens (t, " \t", "");
            words.removeEmptyStrings();
            if (words.size() >= 1) out.add (words[0]);
        }
        return out;
    }

    bool validName (const juce::String& s)
    {
        if (s.isEmpty()) return false;
        for (auto c : s) if (! (juce::CharacterFunctions::isLetterOrDigit (c) || c == '_' || c == '.')) return false;
        return true;
    }

    void parseStatement (juce::String stmt, std::vector<Import>& imports)
    {
        stmt = afterBlockColon (stmt);
        if (startsWithWord (stmt, "import"))
        {
            for (auto& name : commaItems (stmt.substring (6)))
                if (validName (name)) imports.push_back ({ name, 0 });
        }
        else if (startsWithWord (stmt, "from"))
        {
            auto rest = stmt.substring (4).trim();
            const int at = rest.indexOf (" import");
            if (at < 0) return;
            auto modulePart = rest.substring (0, at).trim();
            auto namesPart = rest.substring (at + 7).trim();

            int level = 0;
            while (level < modulePart.length() && modulePart[level] == '.') ++level;
            auto module = modulePart.substring (level).trim();
            if (module.isNotEmpty() && ! validName (module)) return;

            imports.push_back ({ module, level });
            for (auto& name : commaItems (namesPart))
            {
                if (name == "*" || ! validName (name)) continue;
                imports.push_back ({ module.isEmpty() ? name : module + "." + name, level });
            }
        }
    }

    // ── path helpers (package-relative, '/'-separated, case-insensitive like Windows) ──
    juce::String key (const juce::String& p) { return p.toLowerCase(); }
    juce::String parentOf (const juce::String& p) { return p.contains ("/") ? p.upToLastOccurrenceOf ("/", false, false) : juce::String(); }
    juce::String join (const juce::String& a, const juce::String& b) { return a.isEmpty() ? b : a + "/" + b; }
    juce::String fileName (const juce::String& p) { return p.fromLastOccurrenceOf ("/", false, false); }
    juce::String suffixOf (const juce::String& p)
    {
        auto n = fileName (p);
        const int dot = n.lastIndexOfChar ('.');
        return dot > 0 ? n.substring (dot).toLowerCase() : juce::String();
    }

    const std::set<juce::String>& knownModules()
    {
        static const std::set<juce::String> m = [] {
            std::set<juce::String> s;
            // Modules FL Studio itself provides to scripts.
            for (auto* n : { "flpianoroll", "enveditor", "device", "midi", "channels", "mixer", "patterns", "playlist",
                             "transport", "ui", "general", "arrangement", "plugins", "utils", "screen", "launchMapPages",
                             "autogen" })
                s.insert (n);
            // Python standard library (top-level names).
            for (auto* n : { "__future__", "_thread", "abc", "aifc", "argparse", "array", "ast", "asyncio", "atexit", "base64",
                             "bdb", "binascii", "bisect", "builtins", "bz2", "calendar", "cmath", "cmd", "code", "codecs",
                             "collections", "colorsys", "concurrent", "configparser", "contextlib", "contextvars", "copy",
                             "copyreg", "csv", "ctypes", "dataclasses", "datetime", "dbm", "decimal", "difflib", "dis",
                             "doctest", "email", "encodings", "enum", "errno", "faulthandler", "fnmatch", "fractions",
                             "functools", "gc", "getopt", "getpass", "gettext", "glob", "graphlib", "gzip", "hashlib",
                             "heapq", "hmac", "html", "http", "imaplib", "importlib", "inspect", "io", "ipaddress",
                             "itertools", "json", "keyword", "linecache", "locale", "logging", "lzma", "math", "mimetypes",
                             "numbers", "operator", "os", "pathlib", "pickle", "pkgutil", "platform", "pprint", "queue",
                             "random", "re", "reprlib", "sched", "secrets", "select", "selectors", "shelve", "shlex",
                             "shutil", "signal", "site", "socket", "sqlite3", "ssl", "stat", "statistics", "string",
                             "struct", "subprocess", "sys", "sysconfig", "tempfile", "textwrap", "threading", "time",
                             "timeit", "tkinter", "token", "tokenize", "traceback", "types", "typing", "unicodedata",
                             "unittest", "urllib", "uuid", "warnings", "wave", "weakref", "webbrowser", "xml", "zipfile",
                             "zlib", "zoneinfo" })
                s.insert (n);
            return s;
        }();
        return m;
    }
}

bool isScriptFile (const juce::String& relPath)
{
    auto s = suffixOf (relPath);
    return s == ".py" || s == ".pyscript";
}

ScriptInfo inspectScript (const void* data, size_t size)
{
    ScriptInfo info;
    if (size > (size_t) kMaxScriptBytes) { info.problem = "Script exceeds metadata scan limit"; return info; }

    juce::MemoryBlock mb (data, size);
    auto* bytes = static_cast<const juce::uint8*> (mb.getData());
    size_t skip = (size >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF) ? 3 : 0;
    if (! juce::CharPointer_UTF8::isValidString (reinterpret_cast<const char*> (bytes) + skip, (int) (size - skip)))
    {
        info.problem = "Cannot parse script imports";
        return info;
    }
    juce::String text = juce::String::fromUTF8 (reinterpret_cast<const char*> (bytes) + skip, (int) (size - skip));

    for (auto& line : logicalLines (stripCommentsAndStrings (text)))
        for (auto& stmt : splitStatements (line.trim()))
            parseStatement (stmt, info.imports);

    for (auto& imp : info.imports)
    {
        if (imp.level != 0) continue;
        const auto root = imp.module.upToFirstOccurrenceOf (".", false, false);
        if (root == "flpianoroll") info.kinds.insert ("piano");
        else if (root == "enveditor") info.kinds.insert ("edison");
    }
    return info;
}

Classification classifyScripts (const std::map<juce::String, ScriptInfo>& filesIn)
{
    // case-insensitive lookup of supplied files
    std::map<juce::String, juce::String> byKey;                 // lower-case -> original
    std::map<juce::String, const ScriptInfo*> infoByKey;
    for (auto& [path, info] : filesIn) { byKey[key (path)] = path; infoByKey[key (path)] = &info; }

    std::vector<juce::String> entries;
    for (auto& [path, info] : filesIn)
    {
        const auto s = suffixOf (path);
        if (s == ".pyscript" || (s == ".py" && fileName (path).toLowerCase().startsWith ("device_")))
            entries.push_back (path);
    }

    Classification result;
    std::set<juce::String> detected;
    bool unresolved = false;

    for (auto& entry : entries)
    {
        std::set<juce::String> kinds;
        if (suffixOf (entry) == ".py") kinds.insert ("hardware");

        std::vector<juce::String> pending { entry };
        std::set<juce::String> visited;
        while (! pending.empty())
        {
            const auto path = pending.back(); pending.pop_back();
            if (! visited.insert (key (path)).second) continue;

            const auto& info = *infoByKey[key (path)];
            kinds.insert (info.kinds.begin(), info.kinds.end());
            if (info.problem.isNotEmpty())
            {
                result.notes.add (path + ": " + info.problem);
                unresolved = true;
            }

            for (auto& imp : info.imports)
            {
                juce::String base = parentOf (path);
                for (int up = 1; up < imp.level; ++up) base = parentOf (base);

                juce::String modulePath = imp.module.replaceCharacter ('.', '/');
                std::vector<juce::String> roots;
                roots.push_back (base);
                if (imp.level == 0 && base.isNotEmpty()) roots.push_back ({});    // package root as a second candidate

                bool found = false;
                if (imp.module.isNotEmpty())
                {
                    for (auto& root : roots)
                    {
                        for (auto& option : { join (root, modulePath + ".py"), join (join (root, modulePath), "__init__.py") })
                            if (auto it = byKey.find (key (option)); it != byKey.end())
                            {
                                pending.push_back (it->second);
                                found = true;
                            }
                    }
                }

                const auto topName = imp.module.upToFirstOccurrenceOf (".", false, false);
                if (! found && imp.module.isNotEmpty() && (imp.level > 0 || knownModules().count (topName) == 0))
                    result.notes.add (path + ": dependency " + juce::String::repeatedString (".", imp.level) + imp.module
                                      + " is not among supplied modules; verify it is available in FL Studio.");
            }
        }

        if (kinds.size() != 1)
        {
            unresolved = true;
            result.notes.add (entry + ": script type is missing or conflicting; choose a destination.");
        }
        detected.insert (kinds.begin(), kinds.end());
    }

    if (entries.empty())
        result.notes.add ("Python helpers have no supplied .pyscript or device_*.py entry point; choose a destination.");
    if (detected.size() > 1)
        result.notes.add ("This package contains different script types; keep it together only in a destination you choose, "
                          "or import each script folder separately.");
    result.notes.add ("Only supplied files are copied. Include all local Python helpers and resources; "
                      "external or dynamic dependencies cannot be verified.");

    if (detected.size() == 1 && ! unresolved)
        result.kind = *detected.begin();
    return result;
}
}
