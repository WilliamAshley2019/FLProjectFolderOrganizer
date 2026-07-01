#include "PluginDatabaseManager.h"
#include "SafeFileOperations.h"

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
