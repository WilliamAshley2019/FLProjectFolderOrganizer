#include "MiniAraHost.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

using namespace ARA;

namespace wam
{
static ARAAssertFunction gAssert = [] (ARAAssertCategory, const void*, const char* diag)
{ std::fprintf (stderr, "[ARA assert] %s\n", diag ? diag : ""); };

struct MiniAraHost::Detail
{
    struct AudioReader { std::shared_ptr<const SourceAudio> a; bool use64; };
    static HSource* src (void* p) { return reinterpret_cast<HSource*> (p); }
    struct ContentReader { ARAContentType type; std::vector<ARAContentTempoEntry> tempo; std::vector<ARAContentBarSignature> bars; };
    struct Reader { const uint8_t* data; size_t size; };

    static MiniAraHost* me (void* p) { return reinterpret_cast<MiniAraHost*> (p); }

    // ---- audio access: the plug-in pulls samples from our captured/loaded buffer ----
    static ARAAudioReaderHostRef ARA_CALL createAudioReader (ARAAudioAccessControllerHostRef, ARAAudioSourceHostRef s, ARABool use64)
    { return reinterpret_cast<ARAAudioReaderHostRef> (new AudioReader { src (s)->def.audio, use64 != kARAFalse }); }

    static ARABool ARA_CALL readAudioSamples (ARAAudioAccessControllerHostRef, ARAAudioReaderHostRef r, ARASamplePosition pos,
                                              ARASampleCount n, void* const buffers[])
    {
        auto* rd = reinterpret_cast<AudioReader*> (r);
        const auto& ch = rd->a->channels;
        for (size_t c = 0; c < ch.size(); ++c)
            for (ARASampleCount i = 0; i < n; ++i)
            {
                const ARASamplePosition p = pos + i;
                const float v = (p >= 0 && p < (ARASamplePosition) ch[c].size()) ? ch[c][(size_t) p] : 0.0f;
                if (rd->use64) static_cast<double*> (buffers[c])[i] = v; else static_cast<float*> (buffers[c])[i] = v;
            }
        return kARATrue;
    }
    static void ARA_CALL destroyAudioReader (ARAAudioAccessControllerHostRef, ARAAudioReaderHostRef r)
    { delete reinterpret_cast<AudioReader*> (r); }

    // ---- archiving: opaque plug-in bytes <-> std::vector ----
    static ARASize ARA_CALL getArchiveSize (ARAArchivingControllerHostRef, ARAArchiveReaderHostRef r)
    { return reinterpret_cast<Reader*> (r)->size; }
    static ARABool ARA_CALL readBytes (ARAArchivingControllerHostRef, ARAArchiveReaderHostRef r, ARASize pos, ARASize len, ARAByte buf[])
    {
        auto* rd = reinterpret_cast<Reader*> (r);
        if (pos + len > rd->size) return kARAFalse;
        std::memcpy (buf, rd->data + pos, len);
        return kARATrue;
    }
    static ARABool ARA_CALL writeBytes (ARAArchivingControllerHostRef, ARAArchiveWriterHostRef w, ARASize pos, ARASize len, const ARAByte buf[])
    {
        auto* v = reinterpret_cast<std::vector<uint8_t>*> (w);
        if (v->size() < pos + len) v->resize (pos + len);
        std::memcpy (v->data() + pos, buf, len);
        return kARATrue;
    }
    static void ARA_CALL archProgress (ARAArchivingControllerHostRef, float) {}
    static ARAPersistentID ARA_CALL archiveID (ARAArchivingControllerHostRef c, ARAArchiveReaderHostRef)
    { return me (c)->factory_->documentArchiveID; }

    // ---- content access: tempo map + bar signatures, derived from FL's transport ----
    static bool isTimeline (ARAContentType t) { return t == kARAContentTypeTempoEntries || t == kARAContentTypeBarSignatures; }
    static ARABool ARA_CALL mcAvail (ARAContentAccessControllerHostRef, ARAMusicalContextHostRef, ARAContentType t) { return isTimeline (t); }
    static ARAContentGrade ARA_CALL mcGrade (ARAContentAccessControllerHostRef, ARAMusicalContextHostRef, ARAContentType)
    { return kARAContentGradeAdjusted; }   // the user set this tempo in FL, so it outranks "initial"
    static ARAContentReaderHostRef ARA_CALL mcReader (ARAContentAccessControllerHostRef c, ARAMusicalContextHostRef,
                                                      ARAContentType t, const ARAContentTimeRange*)
    {
        auto* h = me (c);
        auto* r = new ContentReader { t, {}, {} };
        std::lock_guard<std::mutex> l (h->tempoMutex_);
        if (t == kARAContentTypeTempoEntries)
        {
            const double qps = h->bpm_ / 60.0, span = 3600.0;      // constant tempo = two anchor points
            r->tempo = { { 0.0, 0.0 }, { span, span * qps } };
        }
        else
            r->bars = { { h->num_, h->den_, 0.0 } };
        return reinterpret_cast<ARAContentReaderHostRef> (r);
    }
    static ARABool ARA_CALL asAvail (ARAContentAccessControllerHostRef, ARAAudioSourceHostRef, ARAContentType) { return kARAFalse; }
    static ARAContentGrade ARA_CALL asGrade (ARAContentAccessControllerHostRef, ARAAudioSourceHostRef, ARAContentType) { return kARAContentGradeInitial; }
    static ARAContentReaderHostRef ARA_CALL asReader (ARAContentAccessControllerHostRef, ARAAudioSourceHostRef, ARAContentType, const ARAContentTimeRange*) { return nullptr; }
    static ARAInt32 ARA_CALL eventCount (ARAContentAccessControllerHostRef, ARAContentReaderHostRef r)
    { auto* c = reinterpret_cast<ContentReader*> (r); return (ARAInt32) (c->type == kARAContentTypeTempoEntries ? c->tempo.size() : c->bars.size()); }
    static const void* ARA_CALL eventData (ARAContentAccessControllerHostRef, ARAContentReaderHostRef r, ARAInt32 i)
    { auto* c = reinterpret_cast<ContentReader*> (r); return c->type == kARAContentTypeTempoEntries ? (const void*) &c->tempo[(size_t) i] : (const void*) &c->bars[(size_t) i]; }
    static void ARA_CALL destroyContentReader (ARAContentAccessControllerHostRef, ARAContentReaderHostRef r) { delete reinterpret_cast<ContentReader*> (r); }

    // ---- model updates (plug-in -> host) ----
    static void ARA_CALL analysisProgress (ARAModelUpdateControllerHostRef c, ARAAudioSourceHostRef, ARAAnalysisProgressState s, float v)
    { if (auto* l = me (c)->listener_) l->araAnalysisProgress (s, v); }
    static void ARA_CALL srcChanged (ARAModelUpdateControllerHostRef c, ARAAudioSourceHostRef, const ARAContentTimeRange*, ARAContentUpdateFlags)
    { if (auto* l = me (c)->listener_) l->araPlugInChangedContent(); }
    static void ARA_CALL modChanged (ARAModelUpdateControllerHostRef c, ARAAudioModificationHostRef, const ARAContentTimeRange*, ARAContentUpdateFlags)
    { if (auto* l = me (c)->listener_) l->araPlugInChangedContent(); }
    static void ARA_CALL regChanged (ARAModelUpdateControllerHostRef c, ARAPlaybackRegionHostRef, const ARAContentTimeRange*, ARAContentUpdateFlags)
    { if (auto* l = me (c)->listener_) l->araPlugInChangedContent(); }
    static void ARA_CALL docChanged (ARAModelUpdateControllerHostRef c)
    { if (auto* l = me (c)->listener_) l->araPlugInChangedContent(); }

    // ---- playback controller: FL exposes no transport-control API to plug-ins, so these are no-ops ----
    static void ARA_CALL pbStart (ARAPlaybackControllerHostRef) {}
    static void ARA_CALL pbStop (ARAPlaybackControllerHostRef) {}
    static void ARA_CALL pbPos (ARAPlaybackControllerHostRef, ARATimePosition) {}
    static void ARA_CALL pbCycle (ARAPlaybackControllerHostRef, ARATimePosition, ARATimeDuration) {}
    static void ARA_CALL pbCycleOn (ARAPlaybackControllerHostRef, ARABool) {}

    static const ARAAudioAccessControllerInterface audioIf;
    static const ARAArchivingControllerInterface archIf;
    static const ARAContentAccessControllerInterface contentIf;
    static const ARAModelUpdateControllerInterface updateIf;
    static const ARAPlaybackControllerInterface playbackIf;
};

const ARAAudioAccessControllerInterface MiniAraHost::Detail::audioIf {
    ARA_IMPLEMENTED_STRUCT_SIZE (ARAAudioAccessControllerInterface, destroyAudioReader), createAudioReader, readAudioSamples, destroyAudioReader };
const ARAArchivingControllerInterface MiniAraHost::Detail::archIf {
    ARA_IMPLEMENTED_STRUCT_SIZE (ARAArchivingControllerInterface, getDocumentArchiveID), getArchiveSize, readBytes, writeBytes, archProgress, archProgress, archiveID };
const ARAContentAccessControllerInterface MiniAraHost::Detail::contentIf {
    ARA_IMPLEMENTED_STRUCT_SIZE (ARAContentAccessControllerInterface, destroyContentReader), mcAvail, mcGrade, mcReader,
    asAvail, asGrade, asReader, eventCount, eventData, destroyContentReader };
const ARAModelUpdateControllerInterface MiniAraHost::Detail::updateIf {
    ARA_IMPLEMENTED_STRUCT_SIZE (ARAModelUpdateControllerInterface, notifyDocumentDataChanged), analysisProgress, srcChanged, modChanged, regChanged, docChanged };
const ARAPlaybackControllerInterface MiniAraHost::Detail::playbackIf {
    ARA_IMPLEMENTED_STRUCT_SIZE (ARAPlaybackControllerInterface, requestEnableCycle), pbStart, pbStop, pbPos, pbCycle, pbCycleOn };

MiniAraHost::EditCycle::EditCycle (MiniAraHost& x) : h (x) { h.dc_->beginEditing (h.inst_->documentControllerRef); }
MiniAraHost::EditCycle::~EditCycle() { h.dc_->endEditing (h.inst_->documentControllerRef); }

MiniAraHost::MiniAraHost() = default;
MiniAraHost::~MiniAraHost() { detach(); }

bool MiniAraHost::attach (const ARAFactory* f)
{
    if (f == nullptr || inst_ != nullptr) return false;
    factory_ = f;

    ARAInterfaceConfiguration cfg {};
    cfg.structSize = kARAInterfaceConfigurationMinSize;
    cfg.desiredApiGeneration = std::min<ARAAPIGeneration> (f->highestSupportedApiGeneration, kARAAPIGeneration_2_0_Final);
    if (cfg.desiredApiGeneration < f->lowestSupportedApiGeneration) return false;
    cfg.assertFunctionAddress = &gAssert;
    f->initializeARAWithConfiguration (&cfg);

    ARADocumentControllerHostInstance hi {};
    hi.structSize = kARADocumentControllerHostInstanceMinSize;
    hi.audioAccessControllerHostRef   = reinterpret_cast<ARAAudioAccessControllerHostRef> (this);
    hi.audioAccessControllerInterface = &Detail::audioIf;
    hi.archivingControllerHostRef     = reinterpret_cast<ARAArchivingControllerHostRef> (this);
    hi.archivingControllerInterface   = &Detail::archIf;
    hi.contentAccessControllerHostRef = reinterpret_cast<ARAContentAccessControllerHostRef> (this);
    hi.contentAccessControllerInterface = &Detail::contentIf;
    hi.modelUpdateControllerHostRef   = reinterpret_cast<ARAModelUpdateControllerHostRef> (this);
    hi.modelUpdateControllerInterface = &Detail::updateIf;
    hi.playbackControllerHostRef      = reinterpret_cast<ARAPlaybackControllerHostRef> (this);
    hi.playbackControllerInterface    = &Detail::playbackIf;

    ARADocumentProperties dp {};
    dp.structSize = kARADocumentPropertiesMinSize;
    dp.name = "FL Studio Project (via ARA bridge)";

    inst_ = f->createDocumentControllerWithDocument (&hi, &dp);
    if (inst_ == nullptr) { f->uninitializeARA(); factory_ = nullptr; return false; }
    dc_ = inst_->documentControllerInterface;
    return true;
}

void MiniAraHost::detach()
{
    if (inst_ == nullptr) return;
    destroyGraph();
    dc_->destroyDocumentController (inst_->documentControllerRef);
    factory_->uninitializeARA();
    inst_ = nullptr; dc_ = nullptr; factory_ = nullptr;
}

bool MiniAraHost::buildGraph (const GraphDef& def)
{
    if (! isAttached() || def.sources.empty() || def.regions.empty()) return false;
    destroyGraph();
    const auto ref = inst_->documentControllerRef;
    {
        EditCycle edit (*this);

        ARAMusicalContextProperties mp {};
        mp.structSize = ARA_IMPLEMENTED_STRUCT_SIZE (ARAMusicalContextProperties, color);
        mp.name = "FL Studio Timeline";
        context_ = dc_->createMusicalContext (ref, reinterpret_cast<ARAMusicalContextHostRef> (&tagContext_), &mp);

        ARARegionSequenceProperties sp {};
        sp.structSize = ARA_IMPLEMENTED_STRUCT_SIZE (ARARegionSequenceProperties, color);
        sp.name = def.sequenceName.c_str();
        sp.musicalContextRef = context_;
        sequence_ = dc_->createRegionSequence (ref, reinterpret_cast<ARARegionSequenceHostRef> (&tagSequence_), &sp);

        for (const auto& sd : def.sources)
        {
            auto hs = std::make_unique<HSource>();
            hs->host = this; hs->def = sd; hs->modID = "mod." + sd.id;

            ARAAudioSourceProperties ap {};
            ap.structSize = ARA_IMPLEMENTED_STRUCT_SIZE (ARAAudioSourceProperties, channelArrangement);
            ap.name = hs->def.name.c_str();
            ap.persistentID = hs->def.id.c_str();
            ap.sampleCount = sd.audio->numSamples();
            ap.sampleRate = sd.audio->sampleRate;
            ap.channelCount = (ARAChannelCount) sd.audio->channels.size();
            ap.merits64BitSamples = kARAFalse;
            ap.channelArrangementDataType = kARAChannelArrangementUndefined;   // mono/stereo need no arrangement blob
            ap.channelArrangement = nullptr;
            hs->ref = dc_->createAudioSource (ref, reinterpret_cast<ARAAudioSourceHostRef> (hs.get()), &ap);

            ARAAudioModificationProperties mod {};
            mod.structSize = ARA_IMPLEMENTED_STRUCT_SIZE (ARAAudioModificationProperties, persistentID);
            mod.name = hs->def.name.c_str();
            mod.persistentID = hs->modID.c_str();
            hs->modRef = dc_->createAudioModification (ref, hs->ref, reinterpret_cast<ARAAudioModificationHostRef> (hs.get()), &mod);
            sources_.push_back (std::move (hs));
        }

        for (const auto& rd : def.regions)
        {
            if (rd.source < 0 || rd.source >= (int) sources_.size()) continue;
            auto* hs = sources_[(size_t) rd.source].get();
            const double srcSec = hs->def.audio->seconds();
            const double avail = std::max (0.0, srcSec - rd.offsetSec);
            const double dur = std::min (rd.durSec > 0 ? rd.durSec : avail, avail);
            if (dur <= 0) continue;

            auto hr = std::make_unique<HRegion>();
            hr->host = this; hr->def = rd;
            ARAPlaybackRegionProperties rp {};
            rp.structSize = ARA_IMPLEMENTED_STRUCT_SIZE (ARAPlaybackRegionProperties, color);
            rp.transformationFlags = kARAPlaybackTransformationNoChanges;
            rp.startInModificationTime = rd.offsetSec;
            rp.durationInModificationTime = dur;
            rp.startInPlaybackTime = rd.startSec;
            rp.durationInPlaybackTime = dur;
            rp.regionSequenceRef = sequence_;
            rp.name = hr->def.name.c_str();
            hr->ref = dc_->createPlaybackRegion (ref, hs->modRef, reinterpret_cast<ARAPlaybackRegionHostRef> (hr.get()), &rp);
            regions_.push_back (std::move (hr));
        }
    }
    for (auto& s : sources_)   // controller operation, legal outside the edit cycle
        dc_->enableAudioSourceSamplesAccess (ref, s->ref, kARATrue);
    return ! regions_.empty();
}

bool MiniAraHost::buildSingle (std::shared_ptr<const SourceAudio> audio, double startSec, const std::string& name)
{
    GraphDef d;
    d.sequenceName = "FL Mixer Track";
    d.sources.push_back ({ "wam-bridge-src-1", name, audio });
    d.regions.push_back ({ "wam-bridge-reg-1", name, 0, startSec, audio->seconds(), 0.0 });
    return buildGraph (d);
}

void MiniAraHost::destroyGraph()
{
    if (! isAttached() || (sources_.empty() && context_ == nullptr)) return;
    const auto ref = inst_->documentControllerRef;
    for (auto& s : sources_) dc_->enableAudioSourceSamplesAccess (ref, s->ref, kARAFalse);
    {
        EditCycle edit (*this);
        for (auto& r : regions_)  dc_->destroyPlaybackRegion (ref, r->ref);
        for (auto& s : sources_)  dc_->destroyAudioModification (ref, s->modRef);
        for (auto& s : sources_)  dc_->destroyAudioSource (ref, s->ref);
        if (sequence_) dc_->destroyRegionSequence (ref, sequence_);
        if (context_)  dc_->destroyMusicalContext (ref, context_);
    }
    regions_.clear(); sources_.clear(); sequence_ = nullptr; context_ = nullptr;
}

std::vector<ARAPlaybackRegionRef> MiniAraHost::playbackRegionRefs() const
{
    std::vector<ARAPlaybackRegionRef> v;
    for (auto& r : regions_) v.push_back (r->ref);
    return v;
}
double MiniAraHost::graphStartSec() const noexcept { double s = 1e300; for (auto& r : regions_) s = std::min (s, r->def.startSec); return regions_.empty() ? 0.0 : s; }
double MiniAraHost::graphEndSec() const noexcept { double e = 0; for (auto& r : regions_) e = std::max (e, r->def.startSec + r->def.durSec); return e; }

void MiniAraHost::setTempo (double bpm, int num, int den)
{
    std::lock_guard<std::mutex> l (tempoMutex_);
    if (bpm > 0) bpm_ = bpm;
    if (num > 0 && den > 0) { num_ = num; den_ = den; }
}

void MiniAraHost::flushTempoIfChanged()
{
    if (context_ == nullptr) return;
    {
        std::lock_guard<std::mutex> l (tempoMutex_);
        if (std::abs (bpm_ - pushedBpm_) < 0.01 && num_ == pushedNum_ && den_ == pushedDen_) return;
        pushedBpm_ = bpm_; pushedNum_ = num_; pushedDen_ = den_;
    }
    EditCycle edit (*this);   // nullptr range + EverythingChanged = "re-read the whole tempo map"
    dc_->updateMusicalContextContent (inst_->documentControllerRef, context_, nullptr, kARAContentUpdateEverythingChanged);
}

void MiniAraHost::poll() { if (isAttached()) dc_->notifyModelUpdates (inst_->documentControllerRef); }

bool MiniAraHost::storeArchive (std::vector<uint8_t>& out)
{
    if (! hasGraph()) return false;
    out.clear();
    std::vector<ARAAudioSourceRef> sr; std::vector<ARAAudioModificationRef> mr;
    for (auto& s : sources_) { sr.push_back (s->ref); mr.push_back (s->modRef); }
    ARAStoreObjectsFilter f {};
    f.structSize = kARAStoreObjectsFilterMinSize;
    f.documentData = kARATrue;
    f.audioSourceRefsCount = sr.size();       f.audioSourceRefs = sr.data();
    f.audioModificationRefsCount = mr.size(); f.audioModificationRefs = mr.data();
    return dc_->storeObjectsToArchive (inst_->documentControllerRef, reinterpret_cast<ARAArchiveWriterHostRef> (&out), &f) != kARAFalse;
}

bool MiniAraHost::restoreArchive (const uint8_t* data, size_t size)
{
    if (! hasGraph() || size == 0) return false;
    Detail::Reader rd { data, size };
    std::vector<ARAPersistentID> srcIDs, modIDs;
    for (auto& s : sources_) { srcIDs.push_back (s->def.id.c_str()); modIDs.push_back (s->modID.c_str()); }
    ARARestoreObjectsFilter f {};
    f.structSize = kARARestoreObjectsFilterMinSize;
    f.documentData = kARATrue;
    f.audioSourceIDsCount = srcIDs.size();       f.audioSourceArchiveIDs = srcIDs.data();
    f.audioModificationIDsCount = modIDs.size(); f.audioModificationArchiveIDs = modIDs.data();
    EditCycle edit (*this);
    return dc_->restoreObjectsFromArchive (inst_->documentControllerRef, reinterpret_cast<ARAArchiveReaderHostRef> (&rd), &f) != kARAFalse;
}
} // namespace wam
