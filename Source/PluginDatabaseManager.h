#pragma once
#include <JuceHeader.h>
#include "RecycleBinManager.h"

// Manages FL Studio's "Plugin database", which has TWO distinct parts
// that both live under:
//   Documents\Image-Line\FL Studio\Presets\Plugin database\
//
// 1. Generators\ and Effects\ (top-level, NOT under Installed\) --
//    small, user-curated "Favorites" trees organized into folders by
//    category (e.g. "Drum", "Delay reverb"). Each entry is a trio of
//    sibling files: Name.fst / Name.nfo / Name.png. This part has no
//    central index -- it's discovered by walking the folder tree.
//
// 2. Installed\Effects\ and Installed\Generators\ -- the FULL scanned
//    plugin list (hundreds to thousands of entries), organized by
//    plugin format (VST, VST3, CLAP) rather than category. Critically,
//    this has a real structured index: Installed\.Plugins.ini, an
//    INI-style file with one [section] per plugin, e.g.:
//      [Effects\VST\Average]
//      ps_name=Average
//      ps_presetfilename=%FLPluginDBPath%\Installed\Effects\VST\Average.fst
//      ps_files=1
//      ps_file_filename_0=C:\...\Average64.dll
//      ps_file_vendorname_0=airwindows
//      ps_file_category_0=Effect
//    This is the AUTHORITATIVE source for vendor/category/format data.
//    A single .Plugins.ini section can list more than one ps_file_*_N
//    record (e.g. a plugin shipped as both a VST3 and a legacy VST DLL)
//    -- each becomes one PluginFileRecord.
//
// An earlier version of this class tried to extract vendor names by
// scanning raw bytes in the .fst file itself (a heuristic ported from a
// third-party script). Against real-world files that proved unreliable
// -- it was reading unrelated binary patch data and returning garbage.
// That approach has been removed entirely. Vendor/category data now
// comes only from .Plugins.ini, and is left blank (not guessed) when no
// ini entry is found for a Favorites-tree entry.
class PluginDatabaseManager
{
public:
    enum class PluginType { Generator, Effect, Unknown };
    enum class EntrySource { Installed, Favorites };

    // One ps_file_*_N record within a .Plugins.ini section. A section
    // normally has just one (ps_files=1), but can have more for plugins
    // shipped in multiple formats under the same display name.
    struct PluginFileRecord
    {
        juce::String name;
        juce::String filePathRaw;   // as stored, may contain %FLPath% etc.
        juce::String vendorName;
        juce::String category;      // e.g. "Instrument|Synth" or "Effect"
        juce::String formatType;    // VST / VST3 / CLAP / Fruity (native), derived from section path
        int plugClass = -1;
        int bitSize   = -1;
        juce::String arch;
        juce::String guid;
        juce::String idString;
    };

    struct PluginEntry
    {
        juce::String name;          // ps_name for Installed; .fst base filename for Favorites
        juce::String sectionPath;   // raw .Plugins.ini section name, e.g. "Effects\VST\Average" (Installed only)
        juce::String category;      // folder path for Favorites entries, e.g. "Drum"
        PluginType   type = PluginType::Unknown;
        EntrySource  source = EntrySource::Favorites;

        juce::File fstFile;
        juce::File nfoFile;   // may not exist
        juce::File imageFile; // .png or .bmp, may not exist

        std::vector<PluginFileRecord> fileRecords; // empty for Favorites entries without an ini match

        // Convenience accessors for table display -- pulls from the
        // first file record if one exists (the common case), otherwise
        // returns an empty string rather than guessing.
        juce::String GetPrimaryVendor() const
        {
            return fileRecords.empty() ? juce::String() : fileRecords.front().vendorName;
        }
        juce::String GetPrimaryCategory() const
        {
            return fileRecords.empty() ? category : fileRecords.front().category;
        }
        juce::String GetPrimaryFormat() const
        {
            return fileRecords.empty() ? juce::String() : fileRecords.front().formatType;
        }
    };

    PluginDatabaseManager() = default;

    // Standard location: Documents\Image-Line\FL Studio\Presets\Plugin database
    static juce::File GetDefaultDatabaseRoot();

    // Scans both the Installed\ (.Plugins.ini-driven) and the top-level
    // Favorites (folder-walk) trees and returns every plugin entry
    // found, tagged with its EntrySource. databaseRoot defaults to
    // GetDefaultDatabaseRoot() if not given.
    std::vector<PluginEntry> ScanDatabase(const juce::File& databaseRoot = {});

    // --- Favorites tree only ---
    // Moves the entry's full file trio (.fst/.nfo/.png) to the opposite
    // type's tree (Generator <-> Effect). ONLY supported for
    // EntrySource::Favorites entries -- Installed entries are
    // intentionally read-only here, since they're indexed by
    // .Plugins.ini and incorrectly editing that file risks breaking FL
    // Studio's own plugin scanner. Returns false (and does nothing) if
    // called on an Installed entry.
    bool ReclassifyEntry(PluginEntry& entry, PluginType newType, const juce::File& databaseRoot = {});

    // Soft-deletes the entry's files via the recycle bin. For Favorites
    // entries this removes the .fst/.nfo/.png trio. Installed entries
    // are intentionally NOT deletable from this UI -- removing entries
    // from .Plugins.ini or the underlying plugin file is out of scope
    // for the reasons described above. Returns false for Installed
    // entries without attempting anything.
    bool DeleteEntry(const PluginEntry& entry, RecycleBinManager& recycleBin);

    static juce::String ReadNfoRawText(const PluginEntry& entry);
    static bool WriteNfoRawText(const PluginEntry& entry, const juce::String& newText);

private:
    void ScanFavoritesFolder(const juce::File& typeRoot, PluginType type, std::vector<PluginEntry>& results);
    void ScanInstalledIni(const juce::File& databaseRoot, std::vector<PluginEntry>& results);

    // Parses one .Plugins.ini file into raw [section] -> {key: value} maps.
    struct IniSection
    {
        juce::String name;
        juce::StringPairArray values;
    };
    static std::vector<IniSection> ParseIniSections(const juce::File& iniFile);

    // Replaces known %TOKEN% placeholders. Currently only
    // %FLPluginDBPath% is substituted (with databaseRoot) since that's
    // the one we can resolve with certainty from this class alone.
    // %FLPath% (the FL Studio install folder) is deliberately left
    // un-substituted -- guessing which of potentially several installed
    // versions generated a given ini entry risks showing a confidently
    // wrong path, which is worse than showing the raw token.
    static juce::String SubstituteKnownTokens(const juce::String& rawPath, const juce::File& databaseRoot);
};
