// SidecarModel.h - bridge-side reader for the JSON written by FLPSidecarExporter (no dependency on flp.h).
#pragma once
#include <juce_core/juce_core.h>
#include <juce_cryptography/juce_cryptography.h>

struct SidecarModel
{
    struct Source  { juce::String id, name, resolvedPath, relativePath; bool exists = false; };
    struct Sequence{ juce::String id, name; int index = 0; bool muted = false; };
    struct Region  { juce::String id, channelName; int source = -1, sequence = 0;
                     juce::int64 posTicks = 0, lenTicks = 0; double startSec = 0, durSec = 0, rawStartOffset = 0, rawEndOffset = 0; };

    juce::File file;
    juce::String projectFile, sha256, offsetUnits;
    int ppq = 96; double tempo = 120.0;
    std::vector<Source> sources; std::vector<Sequence> sequences; std::vector<Region> regions;

    static bool load (const juce::File& f, SidecarModel& out, juce::String& err)
    {
        auto v = juce::JSON::parse (f);
        if (! v.isObject() || v.getProperty ("schema", {}).toString() != "arabridge.sidecar") { err = "Not an arabridge sidecar."; return false; }
        out = {};
        out.file = f;
        auto p = v.getProperty ("project", {});
        out.projectFile = p.getProperty ("file", {}).toString();
        out.sha256 = p.getProperty ("sha256", {}).toString();
        out.ppq = (int) p.getProperty ("ppq", 96);
        out.tempo = (double) p.getProperty ("tempo", 120.0);
        out.offsetUnits = v.getProperty ("calibration", {}).getProperty ("offsetUnits", "unknown").toString();
        if (auto* a = v.getProperty ("sources", {}).getArray())
            for (auto& s : *a)
                out.sources.push_back ({ s["persistentID"].toString(), s["name"].toString(), s["resolvedPath"].toString(),
                                         s["relativePath"].toString(), (bool) s["exists"] });
        if (auto* a = v.getProperty ("sequences", {}).getArray())
            for (auto& s : *a) out.sequences.push_back ({ s["persistentID"].toString(), s["name"].toString(), (int) s["index"], (bool) s["muted"] });
        if (auto* a = v.getProperty ("regions", {}).getArray())
            for (auto& r : *a)
                out.regions.push_back ({ r["persistentID"].toString(), r["channelName"].toString(), (int) r["sourceIndex"], (int) r["sequenceIndex"],
                                         (juce::int64) r["positionTicks"], (juce::int64) r["lengthTicks"], (double) r["startSeconds"],
                                         (double) r["durationSeconds"], (double) r["rawStartOffset"], (double) r["rawEndOffset"] });
        return true;
    }

    // Find the audio file: absolute path first, then relative to the sidecar (project moved between machines).
    juce::File resolveSource (const Source& s) const
    {
        juce::File f (s.resolvedPath);
        if (f.existsAsFile()) return f;
        f = file.getParentDirectory().getChildFile (s.relativePath);
        return f;
    }

    // Is the FLP next to the sidecar still the one this was exported from?
    bool isStale() const
    {
        auto flp = file.getSiblingFile (projectFile);
        return flp.existsAsFile() && ! sha256.isEmpty() && juce::SHA256 (flp).toHexString() != sha256;
    }
};
