// MiniAraHost.h - a tiny, JUCE-free ARA *host* model (document / source / modification / region).
// Pure ARA C API + std. This is the piece FL Studio does not have: it plays the role of the DAW
// side of ARA so that an ARA plug-in loaded *inside* our wrapper believes it has a real host.
#pragma once
#include "ARA_API/ARAInterface.h"
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace wam
{
struct SourceAudio
{
    std::vector<std::vector<float>> channels;   // planar, immutable once published
    double sampleRate = 44100.0;
    ARA::ARASampleCount numSamples() const { return channels.empty() ? 0 : (ARA::ARASampleCount) channels[0].size(); }
    double seconds() const { return sampleRate > 0 ? (double) numSamples() / sampleRate : 0.0; }
};

class MiniAraHost
{
public:
    struct Listener
    {
        virtual ~Listener() = default;
        virtual void araAnalysisProgress (ARA::ARAAnalysisProgressState, float) {}
        virtual void araPlugInChangedContent() {}
    };

    MiniAraHost();
    ~MiniAraHost();

    // Init ARA on the plug-in's factory and create the ARA document controller.
    bool attach (const ARA::ARAFactory* factory);
    void detach();
    bool isAttached() const noexcept { return inst_ != nullptr; }
    ARA::ARADocumentControllerRef controllerRef() const noexcept { return inst_->documentControllerRef; }
    void setListener (Listener* l) noexcept { listener_ = l; }

    // ---- multi-source / multi-region graph (one region sequence = one FL playlist track) ----
    struct SourceDef { std::string id, name; std::shared_ptr<const SourceAudio> audio; };
    struct RegionDef { std::string id, name; int source = 0; double startSec = 0, durSec = 0, offsetSec = 0; };
    struct GraphDef  { std::string sequenceName = "FL Track"; std::vector<SourceDef> sources; std::vector<RegionDef> regions; };

    // Plug-in renderers must NOT be active while this runs (caller's job).
    bool buildGraph (const GraphDef& def);
    // Convenience: one source, one region at startSec (capture / single-file import path).
    bool buildSingle (std::shared_ptr<const SourceAudio> audio, double startSec, const std::string& name);
    void destroyGraph();
    bool hasGraph() const noexcept { return ! regions_.empty(); }
    std::vector<ARA::ARAPlaybackRegionRef> playbackRegionRefs() const;
    ARA::ARARegionSequenceRef regionSequenceRef() const noexcept { return sequence_; }
    double graphStartSec() const noexcept;
    double graphEndSec() const noexcept;

    // Musical context: fed from the outer host's transport (AudioPlayHead). Any thread.
    void setTempo (double bpm, int num, int den);
    void flushTempoIfChanged();          // message thread: pushes a content-changed edit to the plug-in
    void poll();                         // message thread: ARA notifyModelUpdates (plug-in -> host changes)

    // Persistency (opaque plug-in data: analysis + user edits)
    bool storeArchive (std::vector<uint8_t>& out);
    bool restoreArchive (const uint8_t* data, size_t size);

private:
    struct Detail;
    friend struct Detail;
    struct EditCycle { MiniAraHost& h; explicit EditCycle (MiniAraHost& x); ~EditCycle(); };

    const ARA::ARAFactory* factory_ = nullptr;
    const ARA::ARADocumentControllerInstance* inst_ = nullptr;
    const ARA::ARADocumentControllerInterface* dc_ = nullptr;
    Listener* listener_ = nullptr;

    struct HSource { MiniAraHost* host; SourceDef def; std::string modID; ARA::ARAAudioSourceRef ref = nullptr; ARA::ARAAudioModificationRef modRef = nullptr; };
    struct HRegion { MiniAraHost* host; RegionDef def; ARA::ARAPlaybackRegionRef ref = nullptr; };
    std::vector<std::unique_ptr<HSource>> sources_;
    std::vector<std::unique_ptr<HRegion>> regions_;
    ARA::ARAMusicalContextRef context_ = nullptr;
    ARA::ARARegionSequenceRef sequence_ = nullptr;
    int tagContext_ = 0, tagSequence_ = 0;

    std::mutex tempoMutex_;
    double bpm_ = 120.0, pushedBpm_ = 120.0;
    int num_ = 4, den_ = 4, pushedNum_ = 4, pushedDen_ = 4;
};
} // namespace wam
