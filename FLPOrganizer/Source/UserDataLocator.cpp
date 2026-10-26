#include "UserDataLocator.h"

namespace
{
    juce::File& overrideDir() { static juce::File f; return f; }
    juce::CriticalSection& lock() { static juce::CriticalSection cs; return cs; }
}

void UserDataLocator::setOverride (const juce::File& dir)
{
    const juce::ScopedLock sl (lock());
    overrideDir() = dir;
}
juce::File UserDataLocator::getOverride()
{
    const juce::ScopedLock sl (lock());
    return overrideDir();
}

bool UserDataLocator::looksLikeUserData (const juce::File& dir)
{
    return dir.isDirectory()
        && (dir.getChildFile ("Presets").isDirectory() || dir.getChildFile ("Projects").isDirectory());
}

juce::File UserDataLocator::findUserDataFolder()
{
    if (auto o = getOverride(); looksLikeUserData (o)) return o;

   #if JUCE_WINDOWS
    for (auto* key : { "HKEY_CURRENT_USER\\Software\\Image-Line\\Shared\\Paths" })
    {
        // JUCE's registry helper can read named values, but not enumerate them,
        // so probe the value names Image-Line has been seen to use.
        for (auto* valueName : { "FL Studio", "FL Studio 20", "FL Studio 21", "Data", "User data" })
        {
            auto v = juce::WindowsRegistry::getValue (juce::String (key) + "\\" + valueName);
            if (v.isNotEmpty())
            {
                juce::File f (v);
                if (looksLikeUserData (f)) return f;
            }
        }
    }
   #endif

    auto docs = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                    .getChildFile ("Image-Line").getChildFile ("FL Studio");
    if (looksLikeUserData (docs)) return docs;
    return {};
}

juce::File UserDataLocator::getPluginDatabaseRoot()
{
    auto root = findUserDataFolder();
    if (root == juce::File()) return {};
    auto db = root.getChildFile ("Presets").getChildFile ("Plugin database");
    return db.isDirectory() ? db : juce::File();
}

juce::File UserDataLocator::getInstalledPluginsFolder()
{
    auto db = getPluginDatabaseRoot();
    return db == juce::File() ? juce::File() : db.getChildFile ("Installed");
}
