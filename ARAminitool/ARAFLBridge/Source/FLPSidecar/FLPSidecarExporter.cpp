#include "FLPSidecarExporter.h"
#include <map>
#include <juce_cryptography/juce_cryptography.h>   // SHA256 / MD5 live here in JUCE 8

namespace FLPSidecar
{
static juce::var obj (std::initializer_list<std::pair<const char*, juce::var>> kv)
{
    auto* o = new juce::DynamicObject();
    for (auto& p : kv) o->setProperty (p.first, p.second);
    return juce::var (o);
}

static juce::String resolveStock (const juce::String& raw, const juce::File& flDir)
{
    if (raw.contains ("%FLStudioFactoryData%") && flDir.isDirectory())
        return raw.replace ("%FLStudioFactoryData%", flDir.getChildFile ("Data").getFullPathName());
    return raw;
}

juce::File sidecarFileFor (const juce::File& flp) { return flp.getSiblingFile (flp.getFileName() + ".arabridge.json"); }

Result exportSidecar (const FL::Project& project, const juce::File& flpFile, const Options& opt)
{
    Result res;
    const int ppq = project.getPPQ();
    if (ppq <= 0) { res.error = "Project PPQ is 0."; return res; }
    const double tempo = project.getTempo();               // constant: tempo automation is NOT yet decoded (see "knowledge" notes)
    const double secPerTick = 60.0 / (tempo * (double) ppq);

    // ---- channel table: IID -> channel; only Sampler channels with a sample path become sources ----
    std::map<int, FL::Channel*> channelByIid;
    for (auto* ch : project.getChannels()) if (ch) channelByIid[ch->getIID()] = ch;

    juce::Array<juce::var> sources, regions, sequences, skipped;
    std::map<juce::String, int> sourceIndexByRaw;      // raw FLP path -> index in `sources`
    std::map<int, int> sourceIndexByChannel;           // channel IID -> source index

    auto sourceFor = [&] (FL::Channel* ch) -> int
    {
        const juce::String raw = ch->getSamplePath();
        if (raw.isEmpty()) return -1;
        if (auto it = sourceIndexByRaw.find (raw); it != sourceIndexByRaw.end()) return it->second;

        const auto resolved = resolveStock (raw, opt.flInstallDir);
        juce::File f = juce::File::isAbsolutePath (resolved) ? juce::File (resolved)
                                                              : flpFile.getParentDirectory().getChildFile (resolved);

        const int idx = sources.size();
        // Persistent ID derives from the RAW path, so it survives moving the audio between machines.
        const auto pid = "src." + juce::String::toHexString (raw.hashCode64());
        if (! f.existsAsFile()) ++res.missingFiles;
        sources.add (obj ({ { "persistentID", pid },
                            { "name", f.getFileNameWithoutExtension() },
                            { "rawPath", raw },
                            { "resolvedPath", f.getFullPathName() },
                            { "relativePath", f.getRelativePathFrom (flpFile.getParentDirectory()) },
                            { "exists", f.existsAsFile() } }));
        sourceIndexByRaw[raw] = idx;
        return idx;
    };

    // ---- tracks -> region sequences ----
    auto arr = project.getArrangement (opt.arrangementIndex);
    auto tracks = arr.getTracks();
    const int nTracks = (int) tracks.size();
    for (int t = 0; t < nTracks; ++t)
        sequences.add (obj ({ { "persistentID", "seq.arr" + juce::String (opt.arrangementIndex) + ".trk" + juce::String (t) },
                              { "index", t },
                              { "name", tracks[(size_t) t].getName() },
                              { "muted", tracks[(size_t) t].isMuted() } }));

    // ---- playlist items -> regions (audio clips only) ----
    const auto items = arr.getPlaylistItems();
    for (size_t i = 0; i < items.size(); ++i)
    {
        const auto& it = items[i];
        const bool isPattern = it.itemIndex >= it.patternBase;     // same rule as ArrangementDumper
        const int trackIndex = nTracks - 1 - (int) it.trackRvidx;  // trackRvidx is reversed (same as ArrangementDumper)
        if (trackIndex < 0 || trackIndex >= nTracks) continue;

        FL::Channel* ch = nullptr;
        if (! isPattern) { auto f = channelByIid.find ((int) it.itemIndex); if (f != channelByIid.end()) ch = f->second; }

        juce::String why;
        if (isPattern) { ++res.skippedPatternClips; why = "pattern clip (no audio; bounce in FL first)"; }
        else if (ch == nullptr) why = "no channel with this IID";
        else if (ch->getType() == FL::ChannelType::Automation) { ++res.skippedAutomationClips; why = "automation clip"; }
        else if (ch->getSamplePath().isEmpty()) why = "channel has no sample path (synth/instrument or embedded audio)";

        if (why.isNotEmpty())
        {
            skipped.add (obj ({ { "item", (int) i }, { "reason", why } }));
            continue;
        }

        const int srcIdx = sourceFor (ch);
        regions.add (obj ({
            { "persistentID", "reg.arr" + juce::String (opt.arrangementIndex) + ".item" + juce::String ((int) i) },
            { "sourceIndex", srcIdx },
            { "sequenceIndex", trackIndex },
            { "channelIID", (int) it.itemIndex },
            { "channelName", ch->getName() },
            { "positionTicks", (juce::int64) it.position },
            { "lengthTicks", (juce::int64) it.length },
            // derived with the CONSTANT project tempo; the bridge re-derives from ticks if the tempo map changes
            { "startSeconds", (double) it.position * secPerTick },
            { "durationSeconds", (double) it.length * secPerTick },
            // RAW, UNCALIBRATED: units of these two floats are not yet confirmed (see knowledge.md). Bridge ignores them
            // unless "offsetUnits" in the top-level calibration block says how to interpret them.
            { "rawStartOffset", (double) it.startOffset },
            { "rawEndOffset", (double) it.endOffset }
        }));
    }

    // ---- probe: raw playlist data for reverse-engineering (itemFlags, group, offsets, etc.) ----
    juce::Array<juce::var> probe;
    if (opt.includeProbe)
        for (size_t i = 0; i < items.size(); ++i)
        {
            const auto& it = items[i];
            probe.add (obj ({ { "item", (int) i }, { "position", (juce::int64) it.position }, { "patternBase", (int) it.patternBase },
                              { "itemIndex", (int) it.itemIndex }, { "length", (juce::int64) it.length }, { "trackRvidx", (int) it.trackRvidx },
                              { "group", (int) it.group }, { "itemFlags", (int) it.itemFlags },
                              { "startOffset", (double) it.startOffset }, { "endOffset", (double) it.endOffset } }));
        }

    const auto v = project.getVersion();
    auto root = obj ({
        { "schema", "arabridge.sidecar" }, { "version", 1 },
        { "project", obj ({ { "file", flpFile.getFileName() },
                            { "sha256", juce::SHA256 (flpFile).toHexString() },
                            { "ppq", ppq }, { "tempo", tempo },
                            { "timeSigNumerator", 4 }, { "timeSigDenominator", 4 } }) },   // TimeSig events carry no position yet -> 4/4 assumed
        { "calibration", obj ({ { "offsetUnits", "unknown" } }) },
        { "sources", sources }, { "sequences", sequences }, { "regions", regions },
        { "skipped", skipped }, { "probe", probe }
    });
    juce::ignoreUnused (v);

    res.json = juce::JSON::toString (root, false);
    res.sources = sources.size(); res.regions = regions.size(); res.ok = true;
    return res;
}
}
