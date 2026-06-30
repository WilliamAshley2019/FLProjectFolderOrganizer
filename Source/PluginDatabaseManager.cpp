#include "PluginDatabaseManager.h"

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
            entry.sectionPath = section.name;
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

    bool allOk = true;

    if (entry.fstFile.existsAsFile())
        allOk = recycleBin.SoftDelete(entry.fstFile) && allOk;

    if (entry.nfoFile.existsAsFile())
        allOk = recycleBin.SoftDelete(entry.nfoFile) && allOk;

    if (entry.imageFile.existsAsFile())
        allOk = recycleBin.SoftDelete(entry.imageFile) && allOk;

    return allOk;
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
