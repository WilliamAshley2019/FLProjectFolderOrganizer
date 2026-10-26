#include "UserDataLocator.h"
#include "Discovery.h"

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

    // Registered location (Shared\Paths "Shared data", then per-version keys); relocated or
    // OneDrive-redirected folders show up here rather than under Documents.
    for (auto& c : Discovery::registryCandidates (Discovery::Kind::UserData))
        if (looksLikeUserData (c.path)) return c.path;

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
