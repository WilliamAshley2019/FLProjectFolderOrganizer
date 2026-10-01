// flp2sidecar - console exporter built on YOUR flp.h/flp.cpp parser.
//   flp2sidecar MyProject.flp [--fl "C:\Program Files\Image-Line\FL Studio 2025"] [--no-probe]
// Writes MyProject.flp.arabridge.json next to the project.
#include "FLPSidecarExporter.h"
#include <iostream>

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    if (argc < 2) { std::cerr << "usage: flp2sidecar <project.flp> [--fl <FL install dir>] [--no-probe]\n"; return 2; }

    juce::File flp = juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]);
    FLPSidecar::Options opt;
    for (int i = 2; i < argc; ++i)
    {
        juce::String a (argv[i]);
        if (a == "--fl" && i + 1 < argc) opt.flInstallDir = juce::File (argv[++i]);
        else if (a == "--no-probe") opt.includeProbe = false;
    }

    juce::String err;
    auto project = FL::Project::load (flp, &err);
    if (! project) { std::cerr << "Could not parse " << flp.getFullPathName() << ": " << err << "\n"; return 1; }

    auto r = FLPSidecar::exportSidecar (*project, flp, opt);
    if (! r.ok) { std::cerr << "Export failed: " << r.error << "\n"; return 1; }

    auto out = FLPSidecar::sidecarFileFor (flp);
    out.replaceWithText (r.json);
    std::cout << "Wrote " << out.getFullPathName() << "\n  sources: " << r.sources << " (missing files: " << r.missingFiles << ")"
              << "\n  audio regions: " << r.regions
              << "\n  skipped: " << r.skippedPatternClips << " pattern clips, " << r.skippedAutomationClips << " automation clips\n";
    return 0;
}
