#include "RecycleBinManager.h"

RecycleBinManager::RecycleBinManager()
{
    recycleBinRoot = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
        .getChildFile("WAM")
        .getChildFile("FLProjectOrganizer")
        .getChildFile("RecycleBin");
}

juce::File RecycleBinManager::GetRecycleBinRoot()
{
    if (!recycleBinRoot.exists())
        recycleBinRoot.createDirectory();

    return recycleBinRoot;
}

bool RecycleBinManager::SoftDelete(const juce::File& file)
{
    if (!file.existsAsFile() && !file.isDirectory())
        return false;

    auto binRoot = GetRecycleBinRoot();

    // Build a collision-safe destination name:
    // YYYYMMDD_HHMMSS__originalname.ext
    auto now = juce::Time::getCurrentTime();
    auto stamp = now.formatted("%Y%m%d_%H%M%S");

    juce::File dest = binRoot.getChildFile(stamp + "__" + file.getFileName());

    // Extremely unlikely, but guard against same-second collisions
    int suffix = 2;
    while (dest.exists())
    {
        dest = binRoot.getChildFile(stamp + "__" + juce::String(suffix) + "__" + file.getFileName());
        ++suffix;
    }

    bool moved = file.moveFileTo(dest);
    if (!moved)
        return false;

    // Write a sidecar file recording where this came from, so a future
    // "restore" feature can put it back without guesswork.
    juce::File sidecar = dest.getParentDirectory()
        .getChildFile(dest.getFileName() + ".origin.txt");

    sidecar.replaceWithText(
        "OriginalPath=" + file.getFullPathName() + "\n" +
        "DeletedAt="    + now.toISO8601(true)     + "\n"
    );

    return true;
}

bool RecycleBinManager::HasSufficientSpace(const juce::File& targetPath,
    int64_t requiredBytes,
    double safetyMarginPercent)
{
    // getVolumeFreeSpace operates on whatever drive targetPath resolves to,
    // even if targetPath itself doesn't exist yet (e.g. a destination
    // subfolder we're about to create).
    juce::File probePath = targetPath;
    while (!probePath.exists() && probePath.getParentDirectory() != probePath)
        probePath = probePath.getParentDirectory();

    int64_t freeBytes = probePath.getBytesFreeOnVolume();
    if (freeBytes <= 0)
        return false; // couldn't determine free space; fail safe

    double margin = (double)requiredBytes * (safetyMarginPercent / 100.0);
    int64_t neededWithMargin = requiredBytes + (int64_t)margin;

    return freeBytes >= neededWithMargin;
}

juce::String RecycleBinManager::GetFreeSpaceDescription(const juce::File& targetPath)
{
    juce::File probePath = targetPath;
    while (!probePath.exists() && probePath.getParentDirectory() != probePath)
        probePath = probePath.getParentDirectory();

    int64_t freeBytes = probePath.getBytesFreeOnVolume();
    if (freeBytes <= 0)
        return "Unknown free space";

    double gb = (double)freeBytes / (1024.0 * 1024.0 * 1024.0);
    juce::String driveLabel = probePath.getFullPathName().substring(0, 2); // e.g. "C:"

    return juce::String(gb, 1) + " GB free on " + driveLabel + "\\";
}
