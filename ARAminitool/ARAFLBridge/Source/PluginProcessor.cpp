#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <pluginterfaces/vst/ivstcomponent.h>
#include <map>

// The ARA VST3 header only *declares* the IIDs; define them once in this binary.
namespace ARA { DEF_CLASS_IID (IMainFactory) DEF_CLASS_IID (IPlugInEntryPoint) DEF_CLASS_IID (IPlugInEntryPoint2) }

ARAFLBridgeProcessor::ARAFLBridgeProcessor()
    : AudioProcessor (BusesProperties().withInput ("In", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Out", juce::AudioChannelSet::stereo(), true))
{
    host_.setListener (this);
    startTimerHz (10);
}

ARAFLBridgeProcessor::~ARAFLBridgeProcessor() { stopTimer(); teardownInner(); }

juce::File ARAFLBridgeProcessor::bridgeFolder()
{
    auto d = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("AlphaAudio/ARABridge");
    d.createDirectory();
    return d;
}

void ARAFLBridgeProcessor::prepareToPlay (double sr, int bs)
{
    sr_ = sr; bs_ = bs;
    capture_.setSize (2, (int) (sr * 180.0), false, true, false);   // 3 minutes of capture, allocated up front
    const juce::SpinLock::ScopedLockType l (innerLock_);
    if (inner_ != nullptr && graphLive_)
    {
        inner_->setPlayConfigDetails (2, 2, sr, bs);
        inner_->prepareToPlay (sr, bs);
        innerPrepared_ = true;
        setLatencySamples (inner_->getLatencySamples());
    }
}

void ARAFLBridgeProcessor::processBlock (juce::AudioBuffer<float>& buf, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    auto* ph = getPlayHead();
    double timeSec = 0.0;
    if (ph != nullptr)
        if (auto pos = ph->getPosition())
        {
            playing_ = pos->getIsPlaying();
            if (auto t = pos->getTimeInSeconds()) timeSec = *t;
            if (auto b = pos->getBpm()) bpm_ = *b;
            if (auto ts = pos->getTimeSignature()) host_.setTempo (bpm_, ts->numerator, ts->denominator);
            else host_.setTempo (bpm_, 0, 0);
        }
    fwd_.outer = ph;

    if (capturing_.load())
    {
        if (playing_.load())
        {
            if (! capStarted_.exchange (true)) capStart_ = timeSec;
            const int w = capWritten_.load(), n = juce::jmin (buf.getNumSamples(), capture_.getNumSamples() - w);
            for (int c = 0; c < 2; ++c) capture_.copyFrom (c, w, buf, juce::jmin (c, buf.getNumChannels() - 1), 0, n);
            capWritten_ += n;
            if (n < buf.getNumSamples()) { capturing_ = false; capDone_ = true; }
        }
        else if (capStarted_.load()) { capturing_ = false; capDone_ = true; }   // transport stopped -> finish
        return;   // monitor the dry signal while recording
    }

    const juce::SpinLock::ScopedTryLockType l (innerLock_);
    if (l.isLocked() && innerPrepared_ && graphLive_ && inner_ != nullptr)
    {
        inner_->setPlayHead (&fwd_);
        inner_->processBlock (buf, midi);     // ARA playback renderer replaces the dry audio inside the region
    }
}

// ---------------------------------------------------------------------------------------------
// Bridge method C: host the inner plug-in and bind it to OUR ARA document controller
// ---------------------------------------------------------------------------------------------
void ARAFLBridgeProcessor::loadInnerPlugin (const juce::File& f)
{
    juce::OwnedArray<juce::PluginDescription> descs;
    vst3_.findAllTypesForFile (descs, f.getFullPathName());
    if (descs.isEmpty()) { setStatus ("No VST3 found in that file."); return; }
    setStatus ("Loading " + descs[0]->name + "...");
    vst3_.createPluginInstanceAsync (*descs[0], sr_, bs_,
        [this, f] (std::unique_ptr<juce::AudioPluginInstance> p, const juce::String& err)
        {
            if (p == nullptr) { setStatus ("Load failed: " + err); return; }
            attachInner (std::move (p), f);
        });
}

void ARAFLBridgeProcessor::attachInner (std::unique_ptr<juce::AudioPluginInstance> p, const juce::File& f)
{
    teardownInner();

    struct Visitor : juce::ExtensionsVisitor
    {
        Steinberg::Vst::IComponent* comp = nullptr;
        void visitVST3Client (const VST3Client& c) override { comp = c.getIComponentPtr(); }
    } v;
    p->getExtensions (v);

    ARA::IPlugInEntryPoint*  e1 = nullptr;   // getFactory() is on the v1 interface
    ARA::IPlugInEntryPoint2* e = nullptr;    // bindToDocumentControllerWithRoles() is on the v2 interface
    if (v.comp == nullptr
        || v.comp->queryInterface (ARA::IPlugInEntryPoint::iid,  (void**) &e1) != Steinberg::kResultOk || e1 == nullptr
        || v.comp->queryInterface (ARA::IPlugInEntryPoint2::iid, (void**) &e)  != Steinberg::kResultOk || e == nullptr)
    {
        if (e1) e1->release();
        setStatus (p->getName() + " does not expose ARA 2 (IPlugInEntryPoint/2).");
        return;
    }
    const ARA::ARAFactory* factory = e1->getFactory();
    e1->release();                           // factory pointer stays valid for the lifetime of the plug-in instance

    if (! host_.attach (factory))
    { e->release(); setStatus ("ARA factory refused initialisation."); return; }

    // Bind BEFORE the instance is activated/has state set (ARA rule). We claim all three roles:
    // playback renderer (audio out), editor renderer (preview while editing), editor view (UI).
    const auto roles = (ARA::ARAPlugInInstanceRoleFlags) (ARA::kARAPlaybackRendererRole | ARA::kARAEditorRendererRole | ARA::kARAEditorViewRole);
    ext_ = e->bindToDocumentControllerWithRoles (host_.controllerRef(), roles, roles);
    if (ext_ == nullptr) { host_.detach(); e->release(); setStatus ("bindToDocumentControllerWithRoles failed."); return; }

    entry_ = e;
    inner_ = std::move (p);
    innerFile_ = f;
    inner_->setPlayHead (&fwd_);
    stage_ = Stage::InnerReady;
    setStatus (inner_->getName() + " bound to the bridge. Capture audio or load a file.");

    if (pending_ != nullptr)   // restoring a saved project
    {
        auto st = std::move (pending_);
        juce::File sc (st->getStringAttribute ("sidecar"));
        juce::File wav (st->getStringAttribute ("wav"));
        sequence_ = st->getIntAttribute ("sequence");
        if (sc.existsAsFile()) loadSidecar (sc);
        else if (wav.existsAsFile()) loadAudioFile (wav, false, st->getDoubleAttribute ("start"));
        if (graphLive_)
        {
            juce::MemoryBlock arch, inn;
            arch.fromBase64Encoding (st->getStringAttribute ("ara"));
            inn.fromBase64Encoding (st->getStringAttribute ("inner"));
            if (arch.getSize() > 0) host_.restoreArchive ((const uint8_t*) arch.getData(), arch.getSize());
            if (inn.getSize() > 0) inner_->setStateInformation (inn.getData(), (int) inn.getSize());
        }
    }
}

// ---------------------------------------------------------------------------------------------
// Bridge methods A/B: turn FL-side audio into an ARA audio source + region
// ---------------------------------------------------------------------------------------------
void ARAFLBridgeProcessor::beginCapture()
{
    if (stage_ == Stage::NoInner) { setStatus ("Load an inner ARA plug-in first."); return; }
    capWritten_ = 0; capStarted_ = false; capDone_ = false; capturing_ = true;
    stage_ = Stage::Capturing;
    setStatus ("Armed: press play in FL. Recording stops when the transport stops.");
}

void ARAFLBridgeProcessor::finishCapture()
{
    capDone_ = false;
    const int n = capWritten_.load();
    if (n < (int) sr_ / 10) { stage_ = Stage::InnerReady; setStatus ("Capture too short."); return; }
    auto a = std::make_shared<wam::SourceAudio>();
    a->sampleRate = sr_;
    a->channels.resize (2);
    for (int c = 0; c < 2; ++c) a->channels[(size_t) c].assign (capture_.getReadPointer (c), capture_.getReadPointer (c) + n);

    auto wav = bridgeFolder().getChildFile ("capture_" + juce::String (juce::Time::currentTimeMillis()) + ".wav");
    if (auto out = wav.createOutputStream())
    {
        juce::WavAudioFormat fmt;
        std::unique_ptr<juce::AudioFormatWriter> w (fmt.createWriterFor (out.release(), sr_, 2, 32, {}, 0));
        if (w) { juce::AudioBuffer<float> tmp (2, n); for (int c = 0; c < 2; ++c) tmp.copyFrom (c, 0, capture_, c, 0, n); w->writeFromAudioSampleBuffer (tmp, 0, n); }
    }
    publishSource (a, capStart_.load(), wav);
}

void ARAFLBridgeProcessor::loadAudioFile (const juce::File& f, bool placeAtPlayhead, double startOverrideSec)
{
    if (stage_ == Stage::NoInner) { setStatus ("Load an inner ARA plug-in first."); return; }
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
    if (r == nullptr) { setStatus ("Could not read " + f.getFileName()); return; }
    auto a = std::make_shared<wam::SourceAudio>();
    a->sampleRate = r->sampleRate;
    const int ch = juce::jmin (2, (int) r->numChannels);
    juce::AudioBuffer<float> b (ch, (int) r->lengthInSamples);
    r->read (&b, 0, (int) r->lengthInSamples, 0, true, ch > 1);
    a->channels.resize ((size_t) ch);
    for (int c = 0; c < ch; ++c) a->channels[(size_t) c].assign (b.getReadPointer (c), b.getReadPointer (c) + b.getNumSamples());
    double start = 0.0;
    if (placeAtPlayhead && getPlayHead() != nullptr)
        if (auto pos = getPlayHead()->getPosition()) if (auto t = pos->getTimeInSeconds()) start = *t;
    if (startOverrideSec >= 0.0) start = startOverrideSec;
    publishSource (a, start, f);
}

bool ARAFLBridgeProcessor::publishSource (std::shared_ptr<const wam::SourceAudio> a, double startSec, const juce::File& backing)
{
    wam::MiniAraHost::GraphDef d;
    d.sequenceName = "FL Mixer Track";
    d.sources.push_back ({ "wam-bridge-src-1", backing.getFileNameWithoutExtension().toStdString(), a });
    d.regions.push_back ({ "wam-bridge-reg-1", backing.getFileNameWithoutExtension().toStdString(), 0, startSec, a->seconds(), 0.0 });
    backingWav_ = backing; sidecarLoaded_ = false; sourceStart_ = startSec;
    return publishGraph (d, backing.getFileName() + " @ " + juce::String (startSec, 2) + " s");
}

bool ARAFLBridgeProcessor::publishGraph (const wam::MiniAraHost::GraphDef& def, const juce::String& label)
{
    // ARA: playback-region assignment is only legal while the plug-in is NOT in render state.
    const juce::SpinLock::ScopedLockType l (innerLock_);
    innerPrepared_ = false;
    inner_->releaseResources();

    auto removeAll = [this]
    {
        for (auto r : host_.playbackRegionRefs())
        {
            if (ext_->playbackRendererInterface) ext_->playbackRendererInterface->removePlaybackRegion (ext_->playbackRendererRef, r);
            if (ext_->editorRendererInterface)   ext_->editorRendererInterface->removePlaybackRegion (ext_->editorRendererRef, r);
        }
    };
    if (graphLive_ && ext_ != nullptr) removeAll();
    graphLive_ = false;

    if (! host_.buildGraph (def)) { setStatus ("Failed to build ARA model graph (no usable regions)."); return false; }

    auto regions = host_.playbackRegionRefs();
    for (auto r : regions)
    {
        if (ext_->playbackRendererInterface) ext_->playbackRendererInterface->addPlaybackRegion (ext_->playbackRendererRef, r);
        if (ext_->editorRendererInterface)   ext_->editorRendererInterface->addPlaybackRegion (ext_->editorRendererRef, r);
    }
    if (ext_->editorViewInterface)       // tell the plug-in UI which regions are selected so e.g. Melodyne shows them
    {
        auto seq = host_.regionSequenceRef();
        ARA::ARAContentTimeRange tr { host_.graphStartSec(), host_.graphEndSec() - host_.graphStartSec() };
        ARA::ARAViewSelection sel {};
        sel.structSize = ARA::kARAViewSelectionMinSize;
        sel.playbackRegionRefsCount = regions.size(); sel.playbackRegionRefs = regions.data();
        sel.regionSequenceRefsCount = 1; sel.regionSequenceRefs = &seq;
        sel.timeRange = &tr;
        ext_->editorViewInterface->notifySelection (ext_->editorViewRef, &sel);
    }
    graphLive_ = true;

    inner_->setPlayConfigDetails (2, 2, sr_, bs_);
    inner_->prepareToPlay (sr_, bs_);
    innerPrepared_ = true;
    setLatencySamples (inner_->getLatencySamples());
    stage_ = Stage::Bridged; analysis_ = 0.0f;
    setStatus ("Bridged: " + label);
    return true;
}

// ---------------------------------------------------------------------------------------------
// Bridge method D: FLP sidecar -> real multi-region ARA graph for one playlist track
// ---------------------------------------------------------------------------------------------
void ARAFLBridgeProcessor::loadSidecar (const juce::File& f)
{
    juce::String err;
    SidecarModel m;
    if (! SidecarModel::load (f, m, err)) { setStatus ("Sidecar: " + err); return; }
    sidecar_ = std::move (m); sidecarLoaded_ = true;
    if (sequence_ >= (int) sidecar_.sequences.size()) sequence_ = 0;
    buildFromSidecar();
}

void ARAFLBridgeProcessor::setSequence (int i) { sequence_ = i; if (sidecarLoaded_) buildFromSidecar(); }

bool ARAFLBridgeProcessor::buildFromSidecar()
{
    if (stage_ == Stage::NoInner) { setStatus ("Sidecar loaded. Load an inner ARA plug-in to use it."); return false; }
    warning_.clear();

    juce::AudioFormatManager fm; fm.registerBasicFormats();
    wam::MiniAraHost::GraphDef d;
    d.sequenceName = sequence_ < (int) sidecar_.sequences.size() ? sidecar_.sequences[(size_t) sequence_].name.toStdString() : "FL Track";
    std::map<int, int> remap;                 // sidecar source index -> index in graph
    int missing = 0;

    for (const auto& r : sidecar_.regions)
    {
        if (r.sequence != sequence_ || r.source < 0 || r.source >= (int) sidecar_.sources.size()) continue;
        if (! remap.count (r.source))
        {
            const auto& s = sidecar_.sources[(size_t) r.source];
            std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (sidecar_.resolveSource (s)));
            if (rd == nullptr) { ++missing; remap[r.source] = -1; continue; }
            auto a = std::make_shared<wam::SourceAudio>();
            a->sampleRate = rd->sampleRate;
            const int ch = juce::jmin (2, (int) rd->numChannels);
            juce::AudioBuffer<float> b (ch, (int) rd->lengthInSamples);
            rd->read (&b, 0, (int) rd->lengthInSamples, 0, true, ch > 1);
            a->channels.resize ((size_t) ch);
            for (int c = 0; c < ch; ++c) a->channels[(size_t) c].assign (b.getReadPointer (c), b.getReadPointer (c) + b.getNumSamples());
            remap[r.source] = (int) d.sources.size();
            d.sources.push_back ({ s.id.toStdString(), s.name.toStdString(), a });
        }
        const int gi = remap[r.source];
        if (gi < 0) continue;
        // offsets stay 0 until the raw offset units are calibrated (see knowledge notes)
        d.regions.push_back ({ r.id.toStdString(), r.channelName.toStdString(), gi, r.startSec, r.durSec, 0.0 });
    }

    if (missing > 0) warning_ += juce::String (missing) + " source file(s) missing. ";
    if (sidecar_.isStale()) warning_ += "FLP changed since export: re-export the sidecar. ";
    if (sidecar_.offsetUnits == "unknown")
        for (const auto& r : sidecar_.regions)
            if (r.sequence == sequence_ && (r.rawStartOffset != 0.0 || r.rawEndOffset != 0.0)) { warning_ += "Some clips are trimmed in FL; trim offsets are not applied yet. "; break; }
    if (d.regions.empty()) { setStatus ("No audio regions on that track."); return false; }
    return publishGraph (d, juce::String ((int) d.regions.size()) + " region(s) from " + sidecar_.projectFile);
}

void ARAFLBridgeProcessor::teardownInner()
{
    const juce::SpinLock::ScopedLockType l (innerLock_);
    innerPrepared_ = false; graphLive_ = false;
    if (inner_) { inner_->releaseResources(); inner_->setPlayHead (nullptr); }
    if (host_.hasGraph() && ext_ != nullptr)
        for (auto r : host_.playbackRegionRefs())
        {
            if (ext_->playbackRendererInterface) ext_->playbackRendererInterface->removePlaybackRegion (ext_->playbackRendererRef, r);
            if (ext_->editorRendererInterface)   ext_->editorRendererInterface->removePlaybackRegion (ext_->editorRendererRef, r);
        }
    inner_.reset();            // plug-in first ...
    host_.detach();            // ... then the document controller (ARA allows either order; this one is simplest)
    if (entry_) { entry_->release(); entry_ = nullptr; }
    ext_ = nullptr;
    stage_ = Stage::NoInner;
}

juce::AudioProcessorEditor* ARAFLBridgeProcessor::createInnerEditor()
{ return inner_ != nullptr && inner_->hasEditor() ? inner_->createEditorIfNeeded() : nullptr; }

// ---------------------------------------------------------------------------------------------
void ARAFLBridgeProcessor::timerCallback()
{
    host_.poll();                 // ARA: host polls the plug-in for its own content changes
    host_.flushTempoIfChanged();  // FL tempo changed -> new musical-context edit cycle
    if (capDone_.load()) finishCapture();
    if (sidecarLoaded_ && std::abs (bpm_.load() - sidecar_.tempo) > 0.05)
        warning_ = "FL tempo (" + juce::String (bpm_.load(), 2) + ") differs from the sidecar (" + juce::String (sidecar_.tempo, 2) + "): region times are from the sidecar tempo.";
    sendChangeMessage();
}

void ARAFLBridgeProcessor::araAnalysisProgress (ARA::ARAAnalysisProgressState, float v)
{ analysis_ = v; }

void ARAFLBridgeProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    juce::XmlElement x ("ARAFLBridge");
    if (inner_ != nullptr)
    {
        x.setAttribute ("vst3", innerFile_.getFullPathName());
        x.setAttribute ("wav", backingWav_.getFullPathName());
        x.setAttribute ("start", sourceStart_);
        x.setAttribute ("sidecar", sidecarLoaded_ ? sidecar_.file.getFullPathName() : juce::String());
        x.setAttribute ("sequence", sequence_);
        if (graphLive_)
        {
            std::vector<uint8_t> arch;
            if (host_.storeArchive (arch)) x.setAttribute ("ara", juce::Base64::toBase64 (arch.data(), arch.size()));
            juce::MemoryBlock inn; inner_->getStateInformation (inn);
            x.setAttribute ("inner", inn.toBase64Encoding());
        }
    }
    copyXmlToBinary (x, dest);
}

void ARAFLBridgeProcessor::setStateInformation (const void* data, int size)
{
    if (auto x = getXmlFromBinary (data, size))
    {
        pending_ = std::make_unique<juce::XmlElement> (*x);
        juce::File f (x->getStringAttribute ("vst3"));
        if (f.exists()) loadInnerPlugin (f);
    }
}

juce::AudioProcessorEditor* ARAFLBridgeProcessor::createEditor() { return new ARAFLBridgeEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new ARAFLBridgeProcessor(); }
