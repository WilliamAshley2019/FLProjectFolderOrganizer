#pragma once
#include <JuceHeader.h>
#include "ProjectDatabase.h"

// Builds the sample_usage index: for each scanned .flp, which sample files its
// channels reference. Answers "which projects use kick_07.wav?" and is the data
// source for the dependency manifest export.
class SampleUsageIndexer
{
public:
    explicit SampleUsageIndexer (ProjectDatabase& database) : db (database) {}

    // Parse one project and replace its rows. Returns the number of sample
    // references stored, or -1 on parse failure (error filled if given).
    // Only FLP-format files are accepted (FLhd format 0).
    int indexProject (const juce::File& flpFile, juce::String* error = nullptr);

    // Index everything in the database not yet indexed (or all, if force).
    // progress(i, total, fileName) may be null. Return false from shouldCancel to stop.
    struct Result { int indexed = 0, failed = 0, skipped = 0, references = 0; };
    Result indexAll (bool force,
                     std::function<void (int, int, const juce::String&)> progress = {},
                     std::function<bool()> shouldCancel = {});

    // JSON dependency manifest for one project (samples + existence on this machine).
    // Reads the stored index; run indexProject first if the project is new.
    juce::var buildDependencyManifest (const juce::File& flpFile);
    bool exportDependencyManifest (const juce::File& flpFile, const juce::File& jsonOut);

private:
    ProjectDatabase& db;
};
