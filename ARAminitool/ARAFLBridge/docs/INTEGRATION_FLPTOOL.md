# Adding "Export ARA Bridge Sidecar" to your existing FLP tool

Your `PluginProcessor` never remembers *which file* it loaded, and the sidecar needs the path (hash, relative paths,
output name). Three small edits:

**PluginProcessor.h** - add members/methods
```cpp
juce::File loadedFlpFile;                                   // set in loadFLPFile
juce::String exportBridgeSidecar (const juce::File& flInstallDir = {});
```

**PluginProcessor.cpp**
```cpp
// in loadFLPFile(), after a successful load:
if (project != nullptr) loadedFlpFile = file;

#include "FLPSidecar/FLPSidecarExporter.h"
juce::String PluginProcessor::exportBridgeSidecar (const juce::File& flDir)
{
    juce::ScopedLock lock (projectLock);
    if (! project || ! loadedFlpFile.existsAsFile()) return "Load a project first.";
    FLPSidecar::Options opt; opt.flInstallDir = flDir;
    auto r = FLPSidecar::exportSidecar (*project, loadedFlpFile, opt);
    if (! r.ok) return "Export failed: " + r.error;
    auto out = FLPSidecar::sidecarFileFor (loadedFlpFile);
    if (! out.replaceWithText (r.json)) return "Could not write " + out.getFullPathName();
    return "Wrote " + out.getFullPathName() + "\n" + juce::String (r.regions) + " audio region(s), " + juce::String (r.sources)
         + " source(s), " + juce::String (r.missingFiles) + " missing file(s)\nSkipped: " + juce::String (r.skippedPatternClips)
         + " pattern clips, " + juce::String (r.skippedAutomationClips) + " automation clips";
}
```

**PluginEditor** - mirror your existing `exportJsonButtonClicked`: one `TextButton exportSidecarButton`, whose handler calls
`outputDisplay.setText (processorRef.exportBridgeSidecar (flDirFromFLInstallationScanner));`
(`FLInstallationScanner::ScanForInstallations().front().installFolder` gives the FL dir for `%FLStudioFactoryData%`.)

Projucer: add `juce_cryptography` to the module list (the exporter uses `juce::SHA256` for the stale-FLP check).
Add `Source/FLPSidecar/FLPSidecarExporter.{h,cpp}` to the project.
