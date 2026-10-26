#include "SampleUsageIndexer.h"
#include "flp.h"
#include <set>

int SampleUsageIndexer::indexProject (const juce::File& flpFile, juce::String* error)
{
    juce::String err;
    auto project = FL::Project::load (flpFile, &err, 1u << 0);
    if (project == nullptr)
    {
        if (error != nullptr) *error = err;
        return -1;
    }

    std::vector<ProjectDatabase::SampleUsageRow> rows;
    std::set<juce::String> seen;      // dedupe (channel, path) pairs
    for (auto* ch : project->getChannels())     // pointers valid while `project` lives
    {
        if (ch == nullptr) continue;
        auto path = ch->getSamplePath();
        if (path.isEmpty()) continue;
        auto name = ch->getName();
        if (! seen.insert (name + "\n" + path).second) continue;
        rows.push_back ({ flpFile.getFullPathName(), name, path, {} });
    }
    if (! db.ReplaceSampleUsage (flpFile.getFullPathName(), rows))
    {
        if (error != nullptr) *error = "Database write failed (is the project in the database?)";
        return -1;
    }
    return (int) rows.size();
}

SampleUsageIndexer::Result SampleUsageIndexer::indexAll (bool force,
        std::function<void (int, int, const juce::String&)> progress,
        std::function<bool()> shouldCancel)
{
    Result r;
    auto projects = db.GetAllProjects();
    const int total = (int) projects.size();
    int i = 0;
    for (auto& p : projects)
    {
        if (shouldCancel && shouldCancel()) break;
        if (progress) progress (i, total, p.fileName);
        ++i;
        juce::File f (p.path);
        if (p.isZipped || ! f.hasFileExtension (".flp") || ! f.existsAsFile()) { ++r.skipped; continue; }
        if (! force && db.IsProjectIndexed (p.path)) { ++r.skipped; continue; }
        int n = indexProject (f);
        if (n < 0) ++r.failed; else { ++r.indexed; r.references += n; }
    }
    if (progress) progress (total, total, "Done");
    return r;
}

juce::var SampleUsageIndexer::buildDependencyManifest (const juce::File& flpFile)
{
    auto* root = new juce::DynamicObject();
    root->setProperty ("project", flpFile.getFullPathName());
    root->setProperty ("generatedBy", "FLP Organizer");
    root->setProperty ("indexed", db.IsProjectIndexed (flpFile.getFullPathName()));

    juce::Array<juce::var> samples;
    int missing = 0;
    for (auto& row : db.GetSampleUsage (flpFile.getFullPathName()))
    {
        // FL macros like %FLStudioFactoryData% are not real paths; only test real ones.
        const bool isMacro = row.samplePath.startsWithChar ('%');
        const bool absolute = juce::File::isAbsolutePath (row.samplePath);
        bool exists = false;
        if (absolute) exists = juce::File (row.samplePath).existsAsFile();
        const bool checkable = absolute && ! isMacro;
        if (checkable && ! exists) ++missing;

        auto* o = new juce::DynamicObject();
        o->setProperty ("channel", row.channelName);
        o->setProperty ("path", row.samplePath);
        o->setProperty ("kind", isMacro ? "flStock" : (absolute ? "absolute" : "relative"));
        o->setProperty ("existsOnThisMachine", checkable ? juce::var (exists) : juce::var());
        samples.add (juce::var (o));
    }
    root->setProperty ("sampleCount", samples.size());
    root->setProperty ("missingCount", missing);
    root->setProperty ("samples", samples);
    return juce::var (root);
}

bool SampleUsageIndexer::exportDependencyManifest (const juce::File& flpFile, const juce::File& jsonOut)
{
    if (! db.IsProjectIndexed (flpFile.getFullPathName()))
    {
        if (indexProject (flpFile) < 0) return false;
    }
    return jsonOut.replaceWithText (juce::JSON::toString (buildDependencyManifest (flpFile), false));
}
