// FLPSidecarExporter.h - turns a parsed FL::Project (your flp.h) into a "bridge sidecar" JSON
// that the ARA FL Bridge plug-in loads. Everything we KNOW goes in typed fields; everything we
// are NOT sure about goes in "raw" blocks so the bridge (and you) can calibrate against real FL behaviour.
#pragma once
#include "flp.h"

namespace FLPSidecar
{
struct Options
{
    juce::File flInstallDir;         // only used to resolve %FLStudioFactoryData% stock samples
    bool includeProbe = true;        // write the "probe" section (raw values for reverse-engineering)
    int  arrangementIndex = 0;
};

struct Result
{
    bool ok = false;
    juce::String error;
    juce::String json;
    int sources = 0, regions = 0, skippedPatternClips = 0, skippedAutomationClips = 0, missingFiles = 0;
};

// flpFile is the on-disk path of the project (used for hash + relative paths + sidecar name).
Result exportSidecar (const FL::Project& project, const juce::File& flpFile, const Options& options = {});

// MyProject.flp -> MyProject.flp.arabridge.json
juce::File sidecarFileFor (const juce::File& flpFile);
}
