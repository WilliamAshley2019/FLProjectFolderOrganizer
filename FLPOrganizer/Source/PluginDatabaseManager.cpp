#include "PluginDatabaseManager.h"
#include "SafeFileOperations.h"

#if JUCE_WINDOWS
 #include <windows.h>
 #pragma comment(lib, "version.lib")
#endif

juce::File PluginDatabaseManager::GetDefaultDatabaseRoot()
{
    return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
        .getChildFile("Image-Line")
        .getChildFile("FL Studio")
        .getChildFile("Presets")
        .getChildFile("Plugin database");
}

std::vector<PluginDatabaseManager::PluginEntry> PluginDatabaseManager::ScanDatabase(const juce::File& databaseRootIn)
{
    std::vector<PluginEntry> results;

    juce::File root = databaseRootIn.exists() ? databaseRootIn : GetDefaultDatabaseRoot();
    if (!root.isDirectory())
        return results;

    // Installed\ -- the big, .Plugins.ini-indexed tree. Parsed first
    // since it's the authoritative vendor/category source.
    ScanInstalledIni(root, results);

    // Top-level Generators\ and Effects\ -- the small, user-curated
    // Favorites trees, discovered by walking folders directly since
    // there's no central index for these.
    ScanFavoritesFolder(root.getChildFile("Generators"), PluginType::Generator, results);
    ScanFavoritesFolder(root.getChildFile("Effects"),    PluginType::Effect,    results);

    return results;
}

// ─── Installed\.Plugins.ini parsing ─────────────────────────────────────────

void PluginDatabaseManager::ScanInstalledIni(const juce::File& root, std::vector<PluginEntry>& results)
{
    juce::File installedRoot = root.getChildFile("Installed");
    if (!installedRoot.isDirectory())
        return;

    // Layout can vary: a single shared .Plugins.ini directly under
    // Installed\, or one per type folder (Installed\Effects\.Plugins.ini
    // and Installed\Generators\.Plugins.ini). We check both rather than
    // assuming one layout and silently finding nothing if it's the other.
    juce::Array<juce::File> candidateIniFiles;
    candidateIniFiles.add(installedRoot.getChildFile(".Plugins.ini"));
    candidateIniFiles.add(installedRoot.getChildFile("Effects").getChildFile(".Plugins.ini"));
    candidateIniFiles.add(installedRoot.getChildFile("Generators").getChildFile(".Plugins.ini"));

    for (const auto& iniFile : candidateIniFiles)
    {
        if (!iniFile.existsAsFile())
            continue;

        auto sections = ParseIniSections(iniFile);

        for (const auto& section : sections)
        {
            // Section names look like "Effects\VST\Average" or
            // "Generators\VST3\KORG MS-20 V". First path component
            // tells us Generator vs Effect; second is the format folder
            // (VST/VST3/CLAP/Fruity/etc).
            juce::StringArray pathParts;
            pathParts.addTokens(section.name, "\\", "");
            pathParts.removeEmptyStrings();

            if (pathParts.isEmpty())
                continue;

            PluginEntry entry;
            entry.sectionPath  = section.name;
            entry.sourceIniFile = iniFile;
            entry.source       = EntrySource::Installed;

            if (pathParts[0].equalsIgnoreCase("Effects"))
                entry.type = PluginType::Effect;
            else if (pathParts[0].equalsIgnoreCase("Generators"))
                entry.type = PluginType::Generator;
            else
                entry.type = PluginType::Unknown;

            juce::String formatFolder = pathParts.size() > 1 ? pathParts[1] : juce::String();

            entry.name = section.values.getValue("ps_name", juce::String());
            if (entry.name.isEmpty())
                entry.name = pathParts.size() > 0 ? pathParts[pathParts.size() - 1] : section.name;

            juce::String presetPathRaw = section.values.getValue("ps_presetfilename", juce::String());
            if (presetPathRaw.isNotEmpty())
            {
                juce::String resolved = SubstituteKnownTokens(presetPathRaw, root);
                entry.fstFile = juce::File(resolved);

                // .nfo/.NFO sidecar, if present, shares the .fst's base name.
                juce::File nfoLower = entry.fstFile.withFileExtension(".nfo");
                juce::File nfoUpper = entry.fstFile.withFileExtension(".NFO");
                if (nfoLower.existsAsFile())
                    entry.nfoFile = nfoLower;
                else if (nfoUpper.existsAsFile())
                    entry.nfoFile = nfoUpper;
            }

            int fileCount = section.values.getValue("ps_files", "0").getIntValue();
            for (int i = 0; i < fileCount; ++i)
            {
                juce::String suffix = "_" + juce::String(i);

                PluginFileRecord rec;
                rec.name        = section.values.getValue("ps_file_name" + suffix, juce::String());
                rec.filePathRaw = section.values.getValue("ps_file_filename" + suffix, juce::String());
                rec.vendorName  = section.values.getValue("ps_file_vendorname" + suffix, juce::String());
                rec.category    = section.values.getValue("ps_file_category" + suffix, juce::String());
                rec.plugClass   = section.values.getValue("ps_file_plugclass" + suffix, "-1").getIntValue();
                rec.bitSize     = section.values.getValue("ps_file_bitsize" + suffix, "-1").getIntValue();
                rec.arch        = section.values.getValue("ps_file_arch" + suffix, juce::String());
                rec.guid        = section.values.getValue("ps_file_guid" + suffix, juce::String());
                rec.idString    = section.values.getValue("ps_file_idstring" + suffix, juce::String());
                rec.formatType  = formatFolder;

                // Resolve the underlying plugin binary path and read its
                // PE version resource. %FLPath% isn't substituted (see
                // SubstituteKnownTokens notes), so this only succeeds
                // when ps_file_filename_N is already an absolute path
                // (which the sample data shows is the common case for
                // vendor plugin DLLs/VST3/CLAP files, just not for
                // Image-Line's own Fruity-native plugins under %FLPath%).
                if (rec.filePathRaw.isNotEmpty() && !rec.filePathRaw.contains("%"))
                    rec.versionString = GetBinaryFileVersion(juce::File(rec.filePathRaw));

                entry.fileRecords.push_back(rec);
            }

            results.push_back(entry);
        }
    }
}

std::vector<PluginDatabaseManager::IniSection> PluginDatabaseManager::ParseIniSections(const juce::File& iniFile)
{
    std::vector<IniSection> sections;

    juce::String content = iniFile.loadFileAsString();
    if (content.isEmpty())
        return sections;

    auto lines = juce::StringArray::fromLines(content);

    // Track the current section by index rather than a pointer/reference,
    // since std::vector reallocates on push_back and a held pointer would
    // dangle the moment a second section is added.
    int currentIndex = -1;

    for (auto rawLine : lines)
    {
        auto line = rawLine.trim();
        if (line.isEmpty() || line.startsWith(";") || line.startsWith("#"))
            continue;

        if (line.startsWith("[") && line.endsWith("]"))
        {
            IniSection newSection;
            newSection.name = line.substring(1, line.length() - 1);
            sections.push_back(newSection);
            currentIndex = (int)sections.size() - 1;
            continue;
        }

        if (currentIndex < 0)
            continue; // stray key-value before any [section] -- ignore

        int eqPos = line.indexOfChar('=');
        if (eqPos < 0)
            continue;

        juce::String key   = line.substring(0, eqPos).trim();
        juce::String value = line.substring(eqPos + 1).trim();
        sections[(size_t)currentIndex].values.set(key, value);
    }

    return sections;
}

juce::String PluginDatabaseManager::SubstituteKnownTokens(const juce::String& rawPath, const juce::File& databaseRoot)
{
    return rawPath.replace("%FLPluginDBPath%", databaseRoot.getFullPathName());
    // %FLPath% intentionally left as-is -- see header comment.
}

// ─── Favorites tree (folder-walk) ───────────────────────────────────────────

void PluginDatabaseManager::ScanFavoritesFolder(const juce::File& typeRoot, PluginType type,
    std::vector<PluginEntry>& results)
{
    if (!typeRoot.isDirectory())
        return;

    juce::Array<juce::File> fstFiles;
    typeRoot.findChildFiles(fstFiles, juce::File::findFiles, true, "*.fst");

    for (const auto& fst : fstFiles)
    {
        PluginEntry entry;
        entry.name    = fst.getFileNameWithoutExtension();
        entry.fstFile = fst;
        entry.type    = type;
        entry.source  = EntrySource::Favorites;

        entry.category = fst.getParentDirectory().getRelativePathFrom(typeRoot);
        if (entry.category == ".")
            entry.category = "";

        juce::File nfo = fst.withFileExtension(".nfo");
        if (nfo.existsAsFile())
            entry.nfoFile = nfo;

        juce::File png = fst.withFileExtension(".png");
        juce::File bmp = fst.withFileExtension(".bmp");
        if (png.existsAsFile())
            entry.imageFile = png;
        else if (bmp.existsAsFile())
            entry.imageFile = bmp;

        // No vendor/category guessing here -- fileRecords stays empty
        // unless/until we cross-reference against the Installed ini data
        // by name. (Left as a future enhancement rather than guessed now.)

        results.push_back(entry);
    }
}

// ─── Mutating operations (Favorites only) ───────────────────────────────────

bool PluginDatabaseManager::ReclassifyEntry(PluginEntry& entry, PluginType newType, const juce::File& databaseRootIn)
{
    if (entry.source != EntrySource::Favorites)
        return false; // Installed entries are read-only -- see header notes

    if (entry.type == newType)
        return true;

    juce::File root = databaseRootIn.exists() ? databaseRootIn : GetDefaultDatabaseRoot();

    juce::String newTypeFolder = (newType == PluginType::Generator) ? "Generators" : "Effects";
    juce::File newTypeRoot = root.getChildFile(newTypeFolder);

    juce::File destFolder = entry.category.isNotEmpty()
        ? newTypeRoot.getChildFile(entry.category)
        : newTypeRoot;

    if (!destFolder.exists())
        destFolder.createDirectory();

    auto moveOne = [&](juce::File& sourceFile) -> bool
    {
        if (!sourceFile.existsAsFile())
            return true;

        juce::File dest = destFolder.getChildFile(sourceFile.getFileName());
        if (dest.exists())
            return false;

        bool ok = sourceFile.moveFileTo(dest);
        if (ok)
            sourceFile = dest;
        return ok;
    };

    bool fstOk = moveOne(entry.fstFile);
    bool nfoOk = moveOne(entry.nfoFile);
    bool imgOk = moveOne(entry.imageFile);

    if (fstOk && nfoOk && imgOk)
    {
        entry.type = newType;
        return true;
    }

    return false;
}

bool PluginDatabaseManager::DeleteEntry(const PluginEntry& entry, RecycleBinManager& recycleBin)
{
    if (entry.source != EntrySource::Favorites)
        return false; // Installed entries are intentionally not deletable from here

    auto safeSoftDelete = [&recycleBin](const juce::File& f) -> bool
    {
        if (!f.existsAsFile())
            return true;
        if (SafeFileOperations::RequiresProtectedFileConfirmation(f))
            return false; // refuse -- this should be unreachable in practice
                          // since plugin database entries never live under
                          // a blocked system directory, but we check anyway
        return recycleBin.SoftDelete(f);
    };

    bool allOk = true;
    allOk = safeSoftDelete(entry.fstFile)   && allOk;
    allOk = safeSoftDelete(entry.nfoFile)   && allOk;
    allOk = safeSoftDelete(entry.imageFile) && allOk;

    return allOk;
}

bool PluginDatabaseManager::WriteInstalledEntryFields(PluginEntry& entry,
    const juce::String& newVendor, const juce::String& newCategory, int newPlugClass)
{
    if (entry.source != EntrySource::Installed)
        return false;

    if (!entry.sourceIniFile.existsAsFile())
        return false;

    // Backup first, always -- a bad write to this file can make FL
    // Studio think plugins are missing/changed until it rescans.
    auto timestamp = juce::Time::getCurrentTime().formatted("%Y%m%d_%H%M%S");
    juce::File backup = entry.sourceIniFile.getSiblingFile(
        entry.sourceIniFile.getFileName() + ".backup_" + timestamp);

    if (!entry.sourceIniFile.copyFileTo(backup))
        return false; // refuse to edit if we couldn't secure a backup first

    juce::String content = entry.sourceIniFile.loadFileAsString();
    auto lines = juce::StringArray::fromLines(content);

    // Find the [section] line matching entry.sectionPath, then find and
    // rewrite (or insert) the three target keys only within that
    // section's line range. Everything else -- other sections, other
    // keys within this section, comments, blank lines -- passes through
    // untouched.
    juce::String targetHeader = "[" + entry.sectionPath + "]";
    int sectionStart = -1, sectionEnd = lines.size();

    for (int i = 0; i < lines.size(); ++i)
    {
        if (lines[i].trim() == targetHeader)
        {
            sectionStart = i;
            continue;
        }
        if (sectionStart >= 0 && i > sectionStart &&
            lines[i].trim().startsWith("[") && lines[i].trim().endsWith("]"))
        {
            sectionEnd = i;
            break;
        }
    }

    if (sectionStart < 0)
        return false; // section not found -- ini may have changed since scan; caller should rescan

    auto setOrInsertKey = [&](const juce::String& key, const juce::String& value)
    {
        for (int i = sectionStart + 1; i < sectionEnd; ++i)
        {
            if (lines[i].trim().startsWithIgnoreCase(key + "="))
            {
                lines.set(i, key + "=" + value);
                return;
            }
        }
        // Key not present in this section -- insert right after the header.
        lines.insert(sectionStart + 1, key + "=" + value);
        ++sectionEnd; // shift since we just grew the array
    };

    setOrInsertKey("ps_file_vendorname_0", newVendor);
    setOrInsertKey("ps_file_category_0", newCategory);
    setOrInsertKey("ps_file_plugclass_0", juce::String(newPlugClass));

    juce::String newContent = lines.joinIntoString("\n");

    if (!entry.sourceIniFile.replaceWithText(newContent))
        return false;

    if (!entry.fileRecords.empty())
    {
        entry.fileRecords[0].vendorName = newVendor;
        entry.fileRecords[0].category   = newCategory;
        entry.fileRecords[0].plugClass  = newPlugClass;
    }

    return true;
}

juce::String PluginDatabaseManager::GetBinaryFileVersion(const juce::File& binaryFile)
{
   #if JUCE_WINDOWS
    if (!binaryFile.existsAsFile())
        return {};

    auto pathW = binaryFile.getFullPathName().toWideCharPointer();

    DWORD handle = 0;
    DWORD size = GetFileVersionInfoSizeW(pathW, &handle);
    if (size == 0)
        return {}; // no version resource present -- common for some CLAP/third-party builds

    juce::HeapBlock<BYTE> buffer(size);
    if (!GetFileVersionInfoW(pathW, handle, size, buffer))
        return {};

    VS_FIXEDFILEINFO* info = nullptr;
    UINT infoLen = 0;
    if (!VerQueryValueW(buffer, L"\\", (LPVOID*)&info, &infoLen) || info == nullptr)
        return {};

    int major = HIWORD(info->dwFileVersionMS);
    int minor = LOWORD(info->dwFileVersionMS);
    int build = HIWORD(info->dwFileVersionLS);
    int rev   = LOWORD(info->dwFileVersionLS);

    return juce::String(major) + "." + juce::String(minor) + "." +
           juce::String(build) + "." + juce::String(rev);
   #else
    juce::ignoreUnused(binaryFile);
    return {};
   #endif
}

bool PluginDatabaseManager::ReclassifyInstalledEntry(PluginEntry& entry, PluginType newType,
    const juce::File& databaseRootIn)
{
    if (entry.source != EntrySource::Installed)
        return false;

    if (entry.type == newType)
        return true;

    if (!entry.sourceIniFile.existsAsFile())
        return false;

    juce::File root = databaseRootIn.exists() ? databaseRootIn : GetDefaultDatabaseRoot();

    // Backup first, same discipline as WriteInstalledEntryFields.
    auto timestamp = juce::Time::getCurrentTime().formatted("%Y%m%d_%H%M%S");
    juce::File backup = entry.sourceIniFile.getSiblingFile(
        entry.sourceIniFile.getFileName() + ".backup_" + timestamp);
    if (!entry.sourceIniFile.copyFileTo(backup))
        return false;

    // Compute the new section path: swap the first path component
    // (Effects/Generators) and keep everything after it (format folder
    // + name) unchanged.
    juce::StringArray pathParts;
    pathParts.addTokens(entry.sectionPath, "\\", "");
    pathParts.removeEmptyStrings();
    if (pathParts.isEmpty())
        return false;

    juce::String newTypeWord = (newType == PluginType::Generator) ? "Generators" : "Effects";
    pathParts.set(0, newTypeWord);
    juce::String newSectionPath = pathParts.joinIntoString("\\");

    juce::String content = entry.sourceIniFile.loadFileAsString();
    auto lines = juce::StringArray::fromLines(content);

    juce::String oldHeader = "[" + entry.sectionPath + "]";
    juce::String newHeader = "[" + newSectionPath + "]";

    int sectionStart = -1, sectionEnd = lines.size();
    for (int i = 0; i < lines.size(); ++i)
    {
        if (lines[i].trim() == oldHeader)
        {
            sectionStart = i;
            lines.set(i, newHeader);
            continue;
        }
        if (sectionStart >= 0 && i > sectionStart &&
            lines[i].trim().startsWith("[") && lines[i].trim().endsWith("]"))
        {
            sectionEnd = i;
            break;
        }
    }

    if (sectionStart < 0)
        return false;

    // Move the physical .fst/.nfo files to the new type folder before
    // rewriting path-bearing keys, so we know the move succeeded before
    // committing the ini text change.
    juce::File oldFstFile = entry.fstFile;
    juce::File newTypeRoot = root.getChildFile("Installed").getChildFile(newTypeWord);
    juce::File newFolder = pathParts.size() > 2
        ? newTypeRoot.getChildFile(pathParts[1]) // format subfolder, e.g. VST3
        : newTypeRoot;

    if (!newFolder.exists())
        newFolder.createDirectory();

    juce::File newFstFile = newFolder.getChildFile(oldFstFile.getFileName());
    juce::File newNfoFile = entry.nfoFile.existsAsFile()
        ? newFolder.getChildFile(entry.nfoFile.getFileName())
        : juce::File();

    if (oldFstFile.existsAsFile())
    {
        if (newFstFile.exists() || !oldFstFile.moveFileTo(newFstFile))
            return false; // don't proceed with ini edit if the physical move failed
    }
    if (entry.nfoFile.existsAsFile() && newNfoFile != juce::File())
    {
        if (!newNfoFile.exists())
            entry.nfoFile.moveFileTo(newNfoFile); // best-effort; .nfo absence isn't fatal
    }

    // Now update the path-bearing keys within the section to match.
    auto setOrInsertKey = [&](const juce::String& key, const juce::String& value)
    {
        for (int i = sectionStart + 1; i < sectionEnd; ++i)
        {
            if (lines[i].trim().startsWithIgnoreCase(key + "="))
            {
                lines.set(i, key + "=" + value);
                return;
            }
        }
        lines.insert(sectionStart + 1, key + "=" + value);
        ++sectionEnd;
    };

    juce::String newPresetPath = "%FLPluginDBPath%\\Installed\\" + newSectionPath + ".fst";
    setOrInsertKey("ps_presetfilename", newPresetPath);
    setOrInsertKey("ps_file_category_0", newType == PluginType::Generator ? "Instrument" : "Effect");

    if (!entry.sourceIniFile.replaceWithText(lines.joinIntoString("\n")))
        return false;

    entry.type        = newType;
    entry.sectionPath = newSectionPath;
    entry.fstFile      = newFstFile;
    if (newNfoFile != juce::File() && newNfoFile.existsAsFile())
        entry.nfoFile = newNfoFile;
    if (!entry.fileRecords.empty())
        entry.fileRecords[0].category = (newType == PluginType::Generator) ? "Instrument" : "Effect";

    return true;
}

bool PluginDatabaseManager::DeleteEntryAndPlugin(const PluginEntry& entry, RecycleBinManager& recycleBin)
{
    bool allOk = true;

    // Database-side files first (.fst/.nfo/.png) -- these are always
    // safe locations (never under a blocked directory), so no
    // SafeFileOperations check needed here, matching DeleteEntry.
    if (entry.fstFile.existsAsFile())
        allOk = recycleBin.SoftDelete(entry.fstFile) && allOk;
    if (entry.nfoFile.existsAsFile())
        allOk = recycleBin.SoftDelete(entry.nfoFile) && allOk;
    if (entry.imageFile.existsAsFile())
        allOk = recycleBin.SoftDelete(entry.imageFile) && allOk;

    // The actual plugin binary -- this IS allowed to be under Program
    // Files, by design, for this one specific precision-targeted
    // action. See header comment.
    if (!entry.fileRecords.empty())
    {
        juce::String rawPath = entry.fileRecords.front().filePathRaw;
        if (rawPath.isNotEmpty() && !rawPath.contains("%"))
        {
            juce::File pluginFile(rawPath);
            if (pluginFile.exists())
            {
                // VST3 bundles can be a single file OR a folder on
                // Windows depending on how the vendor packaged it --
                // handle both. moveToTrash-style soft delete via
                // recycle bin either way.
                if (pluginFile.isDirectory())
                    allOk = recycleBin.SoftDelete(pluginFile) && allOk;
                else
                    allOk = recycleBin.SoftDelete(pluginFile) && allOk;
            }
        }
    }

    return allOk;
}

bool PluginDatabaseManager::ExportToCSV(const std::vector<PluginEntry>& entries, const juce::File& csvFile)
{
    juce::String csv = "Source,Type,Name,Vendor,Category,PlugClass,Version,SectionPath,FstPath\n";

    auto escape = [](const juce::String& s) -> juce::String
    {
        if (s.containsAnyOf(",\"\n"))
            return "\"" + s.replace("\"", "\"\"") + "\"";
        return s;
    };

    for (const auto& e : entries)
    {
        juce::String source = (e.source == EntrySource::Installed) ? "Installed" : "Favorites";
        juce::String type   = (e.type == PluginType::Generator) ? "Generator" :
                               (e.type == PluginType::Effect)   ? "Effect" : "Unknown";

        csv += escape(source) + "," + escape(type) + "," + escape(e.name) + "," +
               escape(e.GetPrimaryVendor()) + "," + escape(e.GetPrimaryCategory()) + "," +
               juce::String(e.fileRecords.empty() ? -1 : e.fileRecords[0].plugClass) + "," +
               escape(e.fileRecords.empty() ? juce::String() : e.fileRecords[0].versionString) + "," +
               escape(e.sectionPath) + "," +
               escape(e.fstFile.getFullPathName()) + "\n";
    }

    return csvFile.replaceWithText(csv);
}

juce::String PluginDatabaseManager::ImportFromCSV(const juce::File& csvFile,
    std::vector<PluginEntry>& entries, const juce::File& databaseRootIn)
{
    if (!csvFile.existsAsFile())
        return "CSV file not found.";

    juce::File root = databaseRootIn.exists() ? databaseRootIn : GetDefaultDatabaseRoot();

    auto lines = juce::StringArray::fromLines(csvFile.loadFileAsString());
    if (lines.isEmpty())
        return "CSV file is empty.";

    // Simple quote-aware CSV field split -- handles quoted fields
    // containing commas or escaped quotes, which is all we need for
    // our own export format.
    auto splitCsvLine = [](const juce::String& line) -> juce::StringArray
    {
        juce::StringArray fields;
        juce::String current;
        bool inQuotes = false;

        for (int i = 0; i < line.length(); ++i)
        {
            juce::juce_wchar c = line[i];
            if (inQuotes)
            {
                if (c == '"')
                {
                    if (i + 1 < line.length() && line[i + 1] == '"')
                    {
                        current += '"';
                        ++i;
                    }
                    else inQuotes = false;
                }
                else current += c;
            }
            else
            {
                if (c == '"') inQuotes = true;
                else if (c == ',') { fields.add(current); current.clear(); }
                else current += c;
            }
        }
        fields.add(current);
        return fields;
    };

    int updatedCount = 0, unchangedCount = 0, notFoundCount = 0, failedCount = 0;

    for (int lineIdx = 1; lineIdx < lines.size(); ++lineIdx) // skip header row
    {
        if (lines[lineIdx].trim().isEmpty())
            continue;

        auto fields = splitCsvLine(lines[lineIdx]);
        if (fields.size() < 9)
            continue;

        juce::String source      = fields[0];
        juce::String typeStr     = fields[1];
        juce::String csvVendor   = fields[3];
        juce::String csvCategory = fields[4];
        int csvPlugClass         = fields[5].getIntValue();
        juce::String sectionPath = fields[7];
        juce::String fstPath     = fields[8];

        PluginType newType = typeStr.equalsIgnoreCase("Generator") ? PluginType::Generator :
                             typeStr.equalsIgnoreCase("Effect")    ? PluginType::Effect :
                             PluginType::Unknown;

        // Find matching entry by SectionPath (Installed) or FstPath (Favorites).
        PluginEntry* match = nullptr;
        for (auto& e : entries)
        {
            if (source.equalsIgnoreCase("Installed") && e.source == EntrySource::Installed &&
                e.sectionPath == sectionPath)
            {
                match = &e;
                break;
            }
            if (source.equalsIgnoreCase("Favorites") && e.source == EntrySource::Favorites &&
                e.fstFile.getFullPathName() == fstPath)
            {
                match = &e;
                break;
            }
        }

        if (match == nullptr)
        {
            ++notFoundCount;
            continue;
        }

        bool anyChange = false;

        if (newType != PluginType::Unknown && newType != match->type)
        {
            bool ok = (match->source == EntrySource::Installed)
                ? ReclassifyInstalledEntry(*match, newType, root)
                : ReclassifyEntry(*match, newType, root);
            if (ok) anyChange = true;
            else { ++failedCount; continue; }
        }

        if (match->source == EntrySource::Installed && !match->fileRecords.empty())
        {
            bool vendorChanged   = csvVendor   != match->fileRecords[0].vendorName;
            bool categoryChanged = csvCategory != match->fileRecords[0].category;
            bool plugClassChanged = csvPlugClass != match->fileRecords[0].plugClass;

            if (vendorChanged || categoryChanged || plugClassChanged)
            {
                if (WriteInstalledEntryFields(*match, csvVendor, csvCategory, csvPlugClass))
                    anyChange = true;
                else
                    ++failedCount;
            }
        }

        if (anyChange) ++updatedCount;
        else ++unchangedCount;
    }

    return "CSV import complete: " + juce::String(updatedCount) + " updated, " +
           juce::String(unchangedCount) + " unchanged, " +
           juce::String(notFoundCount) + " not found, " +
           juce::String(failedCount) + " failed.";
}

juce::String PluginDatabaseManager::ReadNfoRawText(const PluginEntry& entry)
{
    if (!entry.nfoFile.existsAsFile())
        return {};

    return entry.nfoFile.loadFileAsString();
}

bool PluginDatabaseManager::WriteNfoRawText(const PluginEntry& entry, const juce::String& newText)
{
    if (entry.nfoFile == juce::File())
        return false;

    return entry.nfoFile.replaceWithText(newText);
}
