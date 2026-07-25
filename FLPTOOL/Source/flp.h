#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>

#include <vector>
#include <memory>
#include <unordered_map>
#include <optional>
#include <functional>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace FL
{

    // =============================================================================
    // 1.  Event IDs – complete list from PyFLP, Kaitai, FlpEvents.h, and organiser
    // =============================================================================
    enum class EventID : uint8_t
    {
        // ----- BYTE range (0–63) -----
        IsEnabled = 0,
        NoteOn = 1,
        VolByte = 2,
        PanByte = 3,
        MIDIChan = 4,
        MIDINote = 5,
        MIDIPatch = 6,
        MIDIBank = 7,
        LoopActive = 9,
        ShowInfo = 10,
        Shuffle = 11,
        MainVol = 12,
        StretchByte = 13,
        Pitchable = 14,
        Zipped = 15,
        DelayFlags = 16,
        PatLength = 17,
        BlockLength = 18,
        UseLoopPoints = 19,
        LoopType = 20,
        ChanType = 21,
        MixSliceNum = 22,
        PanLaw = 23,
        Unknown24 = 24,
        Looped = 26,
        Licensed = 28,
        Unknown28 = 28,
        PlayTruncatedNotes = 30,
        IsLocked = 32,
        TimeSigNumerator = 33,
        TimeSigDenominator = 34,
        Unknown37 = 37,

        // ----- WORD range (64–127) -----
        NewChan = 64,   // Channel New
        NewPat = 65,   // Pattern New
        TempoCoarse = 66,   // Deprecated
        CurrentPatNum = 67,
        PatData = 68,
        FX = 69,
        FadeStereo = 70,
        CutOff = 71,
        DotVol = 72,
        DotPan = 73,
        PreAmp = 74,
        Decay = 75,
        Attack = 76,
        DotNote = 77,
        DotPitch = 78,
        DotMix = 79,
        MainPitch = 80,
        RandChan = 81,
        MixChan = 82,
        Resonance = 83,
        LoopBar = 84,
        StereoDelay = 85,
        FX3 = 86,
        DotReso = 87,
        DotCutOff = 88,
        ShiftDelay = 89,
        LoopEndBar = 90,
        Dot = 91,
        DotShift = 92,
        TempoFine = 93,   // Deprecated
        Children = 94,
        InsertIcon = 95,
        Swing = 97,
        SlotIID = 98,
        ArrangementNew = 99,   // New arrangement
        CurrentlySelected = 100,  // selected arrangement

        // ----- DWORD range (128–191) -----
        Color = 128,
        PlayListItem = 129,  // Deprecated? Actually PLAYLIST is 233
        Echo = 130,
        FXSine = 131,
        CutCutBy = 132,
        WindowHeight = 133,
        RootNote = 134,  // <-- Added for completeness
        MiddleNote = 135,
        Reserved = 136,
        MainResoCutOff = 137,
        DelayReso = 138,
        Reverb = 139,
        IntStretch = 140,
        SSNote = 141,
        FineTune = 142,
        SamplerFlags = 143,
        LayerFlags = 144,
        GroupNum = 145,
        CurCategory = 146,
        InsertOut = 147,
        MarkerPosition = 148,
        InsertColor = 149,
        PatternColor = 150,
        VerBuild = 151,  // build number from version string
        LoopPos = 152,
        AUSampleRate = 153,
        InsertIn = 154,
        PluginIcon = 155,
        Tempo = 156,
        Pattern157 = 157,
        Pattern158 = 158,
        VersionBuild = 159,
        PatternChanIID = 160,
        Unknown161 = 161,
        Unknown162 = 162,
        Unknown163 = 163,
        PatternSteps = 164,

        // ----- TEXT range (192–207) -----
        Undef = 192,
        Text = 192,
        PatName = 193,
        Title = 194,
        Comment = 195,
        SampleFileName = 196,
        URL = 197,
        CommentRTF = 198,
        Version = 199,
        Licensee = 200,
        PluginFactory = 201,
        DataPath = 202,
        PluginName = 203,
        Unknown204 = 204,
        MarkerText = 205,
        Genre = 206,
        Author = 207,
        MIDICtrls = 208,   // Actually DATA+0? We'll see
        Delay = 209,
        TS404Params = 210,
        DelayLine = 211,
        NewPlugin = 212,
        PluginParams = 213,
        ChanParams = 215,
        InitCtrls = 216,
        PLSelection = 217,
        EnvelopeLFO = 218,
        Levels = 219,
        Unknown220 = 220,
        Polyphony = 221,
        Unknown222 = 222,
        PatternCtrls = 223,
        PatternNotes = 224,
        MixerBlob = 225,
        MIDIController = 226,
        RemoteController = 227,
        Tracking = 228,
        LevelAdjusts = 229,
        Unknown230 = 230,
        CategoryName = 231,
        Unknown232 = 232,
        Playlist = 233,
        Automation = 234,
        InsertRouting = 235,
        InsertData = 236,
        Timestamp = 237,
        TrackInfo = 238,
        TrackName = 239,
        Unknown240 = 240,
        ArrangementName = 241,
    };

    // =============================================================================
    // 2.  Enums from Kaitai and PyFLP
    // =============================================================================
    enum class FilterType : uint32_t {
        FastLP = 0,
        LP = 1,
        BP = 2,
        HP = 3,
        BS = 4,
        LPx2 = 5,
        SVFLP = 6,
        SVFLPx2 = 7
    };

    enum class StretchMode : int32_t {
        Stretch = -1,
        Resample = 0,
        E3Generic = 1,
        E3Mono = 2,
        SliceStretch = 3,
        SliceMap = 4,
        Auto = 5,
        E2Generic = 6,
        E2Transient = 7,
        E2Mono = 8,
        E2Speech = 9
    };

    enum class ArpDirection : uint32_t {
        Off = 0,
        Up = 1,
        Down = 2,
        Bounce = 3,
        Sticky = 4,
        Random = 5
    };

    enum class DeclickMode : uint8_t {
        OutOnly = 0,
        NoBleed = 1,
        Transient = 2,
        Generic = 3,
        Smooth = 4,
        XFade = 5
    };

    enum class LFOShape : int32_t {
        Sine = 0,
        Triangle = 1,
        Pulse = 2
    };

    enum class TrackMotion : uint32_t {
        Stay = 0,
        OneShot = 1,
        MarchWrap = 2,
        MarchStay = 3,
        MarchStop = 4,
        Random = 5,
        ExclusiveRandom = 6
    };

    enum class TrackPress : uint32_t {
        Retrig = 0,
        HoldStop = 1,
        HoldMotion = 2,
        Latch = 3
    };

    enum class TrackSync : uint32_t {
        Off = 0,
        QuarterBeat = 1,
        HalfBeat = 2,
        Beat = 3,
        TwoBeats = 4,
        FourBeats = 5,
        Auto = 6
    };

    enum class PanLaw : uint8_t {
        Circular = 0,
        Triangular = 2
    };

    enum class ChannelType : uint8_t {
        Sampler = 0,
        Native = 2,
        Layer = 3,
        Instrument = 4,
        Automation = 5
    };

    enum class AutomationCurveType : int32_t {
        Linear = 0x00,
        DoubleCurve = 0x01,
        SingleCurve = 0x02,
        Stairs = 0x03,
        SmoothStairs = 0x04,
        HalfSine = 0x05,
        HoldPulse = 0x06,
        Wave = 0x07,
        FlatAnchor = 0x08,
        SingleCurve2 = 0x09,
        SingleCurve3 = 0x0A,
        DoubleCurve2 = 0x0B,
        DoubleCurve3 = 0x0C
    };

    // =============================================================================
    // 3.  Version helper
    // =============================================================================
    struct FLVersion
    {
        int major, minor, patch, build = 0;
        bool operator>=(const FLVersion& other) const;
        juce::String toString() const {
            return juce::String(major) + "." + juce::String(minor) + "." + juce::String(patch) + "." + juce::String(build);
        }
    };

    // =============================================================================
    // 4.  Event base classes
    // =============================================================================

    // Abstract base
    class Event
    {
    public:
        Event(EventID id) : m_id(id) {}
        virtual ~Event() = default;
        EventID id() const { return m_id; }

        virtual void write(juce::OutputStream& out) const = 0;
        virtual std::unique_ptr<Event> clone() const = 0;

        static std::unique_ptr<Event> read(juce::InputStream& in, EventID id, const FLVersion& version);

    protected:
        EventID m_id;
    };

    // ----- Fixed-size event classes -----
    class U8Event final : public Event {
    public:
        explicit U8Event(EventID eid, uint8_t v) : Event(eid), value(v) {}
        void write(juce::OutputStream& out) const override;
        std::unique_ptr<Event> clone() const override { return std::make_unique<U8Event>(id(), value); }
        uint8_t value;
    };

    class BoolEvent final : public Event {
    public:
        explicit BoolEvent(EventID eid, bool v) : Event(eid), value(v) {}
        void write(juce::OutputStream& out) const override;
        std::unique_ptr<Event> clone() const override { return std::make_unique<BoolEvent>(id(), value); }
        bool value;
    };

    class I16Event final : public Event {
    public:
        explicit I16Event(EventID eid, int16_t v) : Event(eid), value(v) {}
        void write(juce::OutputStream& out) const override;
        std::unique_ptr<Event> clone() const override { return std::make_unique<I16Event>(id(), value); }
        int16_t value;
    };

    class U16Event final : public Event {
    public:
        explicit U16Event(EventID eid, uint16_t v) : Event(eid), value(v) {}
        void write(juce::OutputStream& out) const override;
        std::unique_ptr<Event> clone() const override { return std::make_unique<U16Event>(id(), value); }
        uint16_t value;
    };

    class I32Event final : public Event {
    public:
        explicit I32Event(EventID eid, int32_t v) : Event(eid), value(v) {}
        void write(juce::OutputStream& out) const override;
        std::unique_ptr<Event> clone() const override { return std::make_unique<I32Event>(id(), value); }
        int32_t value;
    };

    class U32Event final : public Event {
    public:
        explicit U32Event(EventID eid, uint32_t v) : Event(eid), value(v) {}
        void write(juce::OutputStream& out) const override;
        std::unique_ptr<Event> clone() const override { return std::make_unique<U32Event>(id(), value); }
        uint32_t value;
    };

    class F32Event final : public Event {
    public:
        explicit F32Event(EventID eid, float v) : Event(eid), value(v) {}
        void write(juce::OutputStream& out) const override;
        std::unique_ptr<Event> clone() const override { return std::make_unique<F32Event>(id(), value); }
        float value;
    };

    class ColorEvent final : public Event {
    public:
        explicit ColorEvent(EventID eid, const juce::Colour& c) : Event(eid), value(c) {}
        void write(juce::OutputStream& out) const override;
        std::unique_ptr<Event> clone() const override { return std::make_unique<ColorEvent>(id(), value); }
        juce::Colour value;
    };

    class AsciiEvent final : public Event {
    public:
        explicit AsciiEvent(EventID eid, const juce::String& s) : Event(eid), value(s) {}
        void write(juce::OutputStream& out) const override;
        std::unique_ptr<Event> clone() const override { return std::make_unique<AsciiEvent>(id(), value); }
        juce::String value;
    };

    class UnicodeEvent final : public Event {
    public:
        explicit UnicodeEvent(EventID eid, const juce::String& s) : Event(eid), value(s) {}
        void write(juce::OutputStream& out) const override;
        std::unique_ptr<Event> clone() const override { return std::make_unique<UnicodeEvent>(id(), value); }
        juce::String value;
    };

    // ----- Structured event base (for events with fixed-layout payload) -----
    class StructEvent : public Event {
    public:
        StructEvent(EventID id) : Event(id) {}
        virtual void parse(juce::InputStream& in, size_t size) = 0;
        virtual void writeFields(juce::OutputStream& out) const = 0;
        void write(juce::OutputStream& out) const final;
    };

    // ----- List event base (for arrays of structs) -----
    class ListEvent : public Event {
    public:
        ListEvent(EventID id) : Event(id) {}
        virtual void parse(juce::InputStream& in, size_t size) = 0;
        virtual void writeItems(juce::OutputStream& out) const = 0;
        void write(juce::OutputStream& out) const final;
    };

    // ----- Unknown data event (fallback) -----
    class UnknownDataEvent final : public Event {
    public:
        UnknownDataEvent(EventID eid, const uint8_t* data, size_t size) : Event(eid), m_data(data, size) {}
        void write(juce::OutputStream& out) const override;
        std::unique_ptr<Event> clone() const override { return std::make_unique<UnknownDataEvent>(id(), static_cast<const uint8_t*>(m_data.getData()), m_data.getSize()); }
        juce::MemoryBlock m_data;
    };

    // =============================================================================
    // 5.  Specific structured events
    // =============================================================================

    // ---- TrackInfoEvent (event 238) ----
    struct TrackInfoEvent final : public StructEvent {
        TrackInfoEvent() : StructEvent(EventID::TrackInfo) {}
        void parse(juce::InputStream& in, size_t size) override;
        void writeFields(juce::OutputStream& out) const override;
        std::unique_ptr<Event> clone() const override { return std::make_unique<TrackInfoEvent>(*this); }

        struct Fields {
            uint32_t iid;
            juce::Colour color;
            uint32_t icon;
            bool enabled;
            float height;       // 0.0–1.0 (percentage)
            int32_t lockedHeight;
            bool contentLocked;
            uint32_t motion;    // TrackMotion
            uint32_t press;     // TrackPress
            uint32_t triggerSync; // TrackSync
            bool queued;
            bool tolerant;
            uint32_t positionSync; // TrackSync
            bool grouped;
            bool locked;
        } fields;
    };

    // ---- PlaylistEvent (event 233) ----
    struct PlaylistItem {
        uint32_t position;
        uint16_t patternBase; // always 20480
        uint16_t itemIndex;
        uint32_t length;
        uint16_t trackRvidx;  // reversed track index
        uint16_t group;
        uint16_t itemFlags;
        float startOffset;
        float endOffset;
    };

    class PlaylistEvent final : public ListEvent {
    public:
        PlaylistEvent() : ListEvent(EventID::Playlist) {}
        void parse(juce::InputStream& in, size_t size) override;
        void writeItems(juce::OutputStream& out) const override;
        std::unique_ptr<Event> clone() const override { return std::make_unique<PlaylistEvent>(*this); }

        int getItemSize() const { return m_itemSize; }
        void setItemSize(int size) { m_itemSize = size; }

        std::vector<PlaylistItem> items;
    private:
        int m_itemSize = 80; // default for FL25
        static int detectItemSize(const uint8_t* data, size_t totalSize, int offset);
    };

    // ---- LevelsEvent (event 219) ----
    struct LevelsEvent final : public StructEvent {
        LevelsEvent() : StructEvent(EventID::Levels) {}
        void parse(juce::InputStream& in, size_t size) override;
        void writeFields(juce::OutputStream& out) const override;
        std::unique_ptr<Event> clone() const override { return std::make_unique<LevelsEvent>(*this); }

        struct Fields {
            std::optional<uint32_t> pan;      // 0–12800, centre=6400
            std::optional<uint32_t> volume;   // 0–12800
            std::optional<int32_t> pitchShift; // -4800..4800 cents
            std::optional<uint32_t> filterModX;
            std::optional<uint32_t> filterModY;
            std::optional<uint32_t> filterType; // FilterType
        } fields;
    };

    // ---- ChannelBlobEvent (event 215) – raw payload with accessors ----
    class ChannelBlobEvent final : public StructEvent {
    public:
        ChannelBlobEvent() : StructEvent(EventID::ChanParams) {}
        void parse(juce::InputStream& in, size_t size) override;
        void writeFields(juce::OutputStream& out) const override;
        std::unique_ptr<Event> clone() const override { return std::make_unique<ChannelBlobEvent>(*this); }

        juce::MemoryBlock rawData;

        // Accessors for common fields (offsets from Kaitai)
        bool getNoDC() const;
        void setNoDC(bool val);
        uint8_t getDelayFlags() const;
        void setDelayFlags(uint8_t flags);
        bool getUseMainPitch() const;
        void setUseMainPitch(bool val);
        uint32_t getArpDirection() const;
        void setArpDirection(uint32_t dir);
        // ... add more as needed
    };

    // ---- RemoteControllerEvent (event 227) – automation channel routing ----
    struct RemoteControllerEvent final : public StructEvent {
        RemoteControllerEvent() : StructEvent(EventID::RemoteController) {}
        void parse(juce::InputStream& in, size_t size) override;
        void writeFields(juce::OutputStream& out) const override;
        std::unique_ptr<Event> clone() const override { return std::make_unique<RemoteControllerEvent>(*this); }

        struct Fields {
            uint16_t unknown1;   // observed 0
            uint32_t trackId;    // channel IID or 0
            uint16_t unknown2;   // observed 0
            uint16_t paramId;    // 5 = tempo, 0 = volume, 2 = pitch
            uint16_t destId;     // 0x4000 = master, 0x2000-0x3FFF = mixer track
            uint32_t unknown3;   // observed 0
            uint32_t unknown4;   // observed 0
        } fields;
    };

    // ---- AutomationEvent (event 234) – automation points ----
   // ---- AutomationEvent (event 234) – automation points ----
// Legacy point structure (kept for backward compatibility)
    struct AutomationPoint {
        double beatIncrement; // delta from previous point in beats
        double value;         // raw automation value (0..1 usually)
        float tension;        // 0..1
        uint8_t unknown3[3];  // usually 0
        uint8_t direction;    // 0 or 1
    };

    class AutomationEvent final : public ListEvent {
    public:
        AutomationEvent() : ListEvent(EventID::Automation) {}
        void parse(juce::InputStream& in, size_t size) override;
        void writeItems(juce::OutputStream& out) const override;
        std::unique_ptr<Event> clone() const override { return std::make_unique<AutomationEvent>(*this); }

        // ============================================================
        // Complete Record structure - CONFIRMED against 7 real test files
        // (2-point, 4-point, 7-point, and FL11-vs-FL26 tension-isolated
        // pairs), covering position, value, tension, and 3 of the real
        // curve-type values.
        // ============================================================
        //
        // IMPORTANT - this is NOT a flat per-point struct on disk. Each
        // 24-byte slot in the file straddles TWO points:
        //   - bytes  8-15 (position) and 16-23 (value) belong to point (k+1)
        //   - bytes  0-3  (tension, float) and byte 4 (curveType) belong
        //     to point k itself - i.e. one slot LATER than that point's
        //     own position/value.
        // This means a clip's real point data always needs one MORE
        // 24-byte slot than its point count: the last point's own
        // position/value live in slot (P-1), but its tension/curveType
        // live in slot P, whose position/value bytes are unused (and read
        // as NaN under the naive fixed-stride read that's how this was
        // first noticed). parse() below reads that extra slot for tension/
        // curveType only and does not treat it as an extra point.
        //
        // controlCode only carries real meaning on the very first slot
        // (marks the start point) - preserved verbatim via startMarkerBytes
        // rather than reinterpreted, since we don't know what its second
        // int32 (values 2/4/7 seen across test files) means yet.
        //
        // curveType confirmed values so far: 0 = Single Curve, 1 = Double
        // Curve, 5 = Pulse. Others are still the ORIGINAL reverse-engineering
        // doc's guesses and are very possibly wrong (that doc's guessed
        // Single Curve=0x02 and Linear=0x00 were both wrong) - treat
        // anything not in {0,1,5} as unconfirmed.
        //
        // tension confirmed as a signed float, -1.0 to +1.0 (-100% to
        // +100%), isolated via a single-variable before/after file pair.
        // Sign is presumably which side of the segment the curve bulges
        // toward; exact interpolation math using it is NOT confirmed,
        // only its storage location/range.
        //
        // Pulse's additional "step count" parameter (confirmed to exist -
        // e.g. "4 steps" vs "14 steps" on two real Pulse segments) has NOT
        // been located yet; it isn't in this record at all as far as we've
        // found. Rendered as a plain Hold for now (see AutomationCurveView).
        struct Record {
            int32_t controlCode;   // only meaningful on records[0]; opaque elsewhere, see startMarkerBytes
            int32_t curveType;     // confirmed: 0=Single Curve, 1=Double Curve, 5=Pulse; others unconfirmed
            double position;       // delta in BEATS from the previous point (not absolute, not PPQ ticks)
            double value;          // normalized 0.0-1.0 fraction of the destination parameter's own range
            float tension = 0.0f;  // signed, -1.0..+1.0; meaningless on records[0] (start has no incoming curve)
        };

        // Get number of steps/subdivisions
        int getStepCount() const {
            if (records.size() < 2) return 0;
            return (int)records.size() - 1;
        }

        // Get curve type for a specific segment (0 = first segment)
        int getCurveTypeForSegment(int segmentIndex) const {
            if (segmentIndex < 0 || segmentIndex >= (int)records.size() - 1) return 0;
            return records[segmentIndex + 1].curveType;
        }

        // Set curve type for a specific segment (0 = first segment)
        void setCurveTypeForSegment(int segmentIndex, int curveType) {
            if (segmentIndex < 0 || segmentIndex >= (int)records.size() - 1) return;
            records[segmentIndex + 1].curveType = curveType;
        }

        // Set curve type for all interior segments
        void setCurveTypeForAllSegments(int curveType) {
            for (size_t i = 1; i < records.size(); ++i) {
                records[i].curveType = curveType;
            }
        }

        // Get human-readable curve type name. Only 0/1/5 are confirmed
        // against real files (see the comment on Record) - everything
        // else here is the ORIGINAL reverse-engineering doc's guess and
        // should be treated as unverified; that doc's guesses for 0x00
        // and 0x02 were both wrong.
        static juce::String getCurveTypeName(int curveType) {
            switch (curveType) {
            case 0: return "Single Curve";       // CONFIRMED
            case 1: return "Double Curve";       // CONFIRMED
            case 5: return "Pulse";              // CONFIRMED (step count not yet decoded)
            case 0x03: return "Stairs (unconfirmed)";
            case 0x04: return "Smooth Stairs (unconfirmed)";
            case 0x06: return "Hold (unconfirmed)";
            case 0x07: return "Wave (unconfirmed)";
            case 0x08: return "Flat Anchor (unconfirmed)";
            default: return "Unknown (" + juce::String(curveType) + ")";
            }
        }

        // Get all curve type names for UI dropdowns. Only the first 3 are
        // confirmed - see getCurveTypeName.
        static std::vector<std::pair<int, juce::String>> getCurveTypeList() {
            return {
                {0, "Single Curve"},
                {1, "Double Curve"},
                {5, "Pulse"},
                {0x03, "Stairs (unconfirmed)"},
                {0x04, "Smooth Stairs (unconfirmed)"},
                {0x06, "Hold (unconfirmed)"},
                {0x07, "Wave (unconfirmed)"},
                {0x08, "Flat Anchor (unconfirmed)"}
            };
        }

        // Legacy points (kept for backward compatibility)
        std::vector<AutomationPoint> points;

        // NEW: Complete records (replaces points for modern FLP files)
        std::vector<Record> records;

        // Real-file testing (see comment on parse()) found that dividing
        // (payloadSize - HEADER_SIZE) by POINT_SIZE overclaims: only the
        // first couple of "records" for a genuine 2-point clip decoded to
        // sane values: bytes past that point failed basic sanity checks
        // (curveType outside 0x00-0x0C, controlCode not one of 0/3/-1) and
        // are something else entirely - almost certainly the "PluginSettings
        // (binary blob)" mentioned in the original reverse-engineering notes,
        // not more Records. Preserved verbatim and rewritten unchanged so
        // Save Project As doesn't corrupt files we don't fully understand
        // yet. Still needs a file with 3+ real points to confirm where the
        // real cutoff actually is.
        juce::MemoryBlock trailingData;

        // Slot 0's bytes 0-7, verbatim. Consistently starts with controlCode
        // 3 (marking the start point) but its second int32 varies across
        // real files (2, 4, 7 seen) with unknown meaning - preserved as-is
        // rather than guessed at, so edits never corrupt it.
        juce::MemoryBlock startMarkerBytes;

        // The 13-byte header, captured verbatim at parse time. writeItems()
        // used to hardcode a *different*, longer (16-byte) fixed pattern
        // that doesn't match what real files on disk actually contain -
        // that mismatch alone would desync every byte after this event on
        // Save Project As. Round-tripping the real bytes sidesteps needing
        // to fully decode this header's meaning for now.
        juce::MemoryBlock headerBytes;

    private:
        static constexpr size_t POINT_SIZE = 24; // 8+8+4+3+1
        static constexpr size_t HEADER_SIZE = 13; // 13-byte header before records
    };

    // ---- MixerBlobEvent (event 225) ----
    struct MixerParam {
        uint8_t id;          // param ID (slot_on, slot_mix, vol, pan, etc.)
        uint8_t _u1;
        uint16_t channelData; // packed insert/slot index
        int32_t msg;
    };
    class MixerBlobEvent final : public ListEvent {
    public:
        MixerBlobEvent() : ListEvent(EventID::MixerBlob) {}
        void parse(juce::InputStream& in, size_t size) override;
        void writeItems(juce::OutputStream& out) const override;
        std::unique_ptr<Event> clone() const override { return std::make_unique<MixerBlobEvent>(*this); }
        std::vector<MixerParam> params;
    };

    // ---- WrapperEvent (event 212) ----
    struct WrapperEvent final : public StructEvent {
        WrapperEvent() : StructEvent(EventID::NewPlugin) {}
        void parse(juce::InputStream& in, size_t size) override;
        void writeFields(juce::OutputStream& out) const override;
        std::unique_ptr<Event> clone() const override { return std::make_unique<WrapperEvent>(*this); }

        struct Fields {
            uint16_t flags;    // bitfield (visible, detached, demo, etc.)
            uint8_t page;      // 0=editor, 1=settings, 3=sample, 4=envlfo, 5=misc
            uint32_t width;
            uint32_t height;
        } fields;
    };

    // ---- PatternNotesEvent (event 224) ----
    struct Note {
        uint32_t position;
        uint16_t flags;      // bit 3 = slide
        uint16_t channelIID;
        uint32_t length;
        uint16_t key;        // 0..131 (C0..B10)
        uint16_t group;
        uint8_t finePitch;   // 0..240, default 120
        uint8_t release;     // 0..128, default 64
        uint8_t midiChannel; // 0..15 color, +128 for MIDI drag
        uint8_t pan;         // 0..128, default 64
        uint8_t velocity;    // 0..128, default 100
        uint8_t modX;        // default 128
        uint8_t modY;        // default 128
    };
    class PatternNotesEvent final : public ListEvent {
    public:
        PatternNotesEvent() : ListEvent(EventID::PatternNotes) {}
        void parse(juce::InputStream& in, size_t size) override;
        void writeItems(juce::OutputStream& out) const override;
        std::unique_ptr<Event> clone() const override { return std::make_unique<PatternNotesEvent>(*this); }
        std::vector<Note> notes;
    };

    // ---- PatternCtrlsEvent (event 223) ----
    struct Controller {
        uint32_t position;
        uint8_t channelIID;
        uint8_t flags;
        float value;
    };
    class PatternCtrlsEvent final : public ListEvent {
    public:
        PatternCtrlsEvent() : ListEvent(EventID::PatternCtrls) {}
        void parse(juce::InputStream& in, size_t size) override;
        void writeItems(juce::OutputStream& out) const override;
        std::unique_ptr<Event> clone() const override { return std::make_unique<PatternCtrlsEvent>(*this); }
        std::vector<Controller> controllers;
    };

    // ---- InsertDataEvent (event 236) ----
    struct InsertDataEvent final : public StructEvent {
        InsertDataEvent() : StructEvent(EventID::InsertData) {}
        void parse(juce::InputStream& in, size_t size) override;
        void writeFields(juce::OutputStream& out) const override;
        std::unique_ptr<Event> clone() const override { return std::make_unique<InsertDataEvent>(*this); }
        uint32_t flags = 0; // bitfield
    };

    // ---- InsertRoutingEvent (event 235) ----
    class InsertRoutingEvent final : public ListEvent {
    public:
        InsertRoutingEvent() : ListEvent(EventID::InsertRouting) {}
        void parse(juce::InputStream& in, size_t size) override;
        void writeItems(juce::OutputStream& out) const override;
        std::unique_ptr<Event> clone() const override { return std::make_unique<InsertRoutingEvent>(*this); }
        std::vector<bool> routes;
    };

    // ---- TimestampEvent (event 237) ----
    struct TimestampEvent final : public StructEvent {
        TimestampEvent() : StructEvent(EventID::Timestamp) {}
        void parse(juce::InputStream& in, size_t size) override;
        void writeFields(juce::OutputStream& out) const override;
        std::unique_ptr<Event> clone() const override { return std::make_unique<TimestampEvent>(*this); }
        double createdOn = 0.0;   // days since 1899-12-30
        double timeSpent = 0.0;   // days
    };

    // =============================================================================
    // 6.  EventTree – container with mutable views
    // =============================================================================

    class EventTree
    {
    public:
        EventTree() = default;
        EventTree(const EventTree&) = delete;
        EventTree& operator=(const EventTree&) = delete;
        EventTree(EventTree&&) noexcept = default;
        EventTree& operator=(EventTree&&) noexcept = default;

        void addEvent(std::unique_ptr<Event> event);
        void removeEvent(EventID id, int index = 0);
        std::vector<Event*> getEvents(EventID id) const;
        Event* firstEvent(EventID id) const;
        bool hasEvent(EventID id) const;
        size_t size() const { return m_events.size(); }

        // Grouping methods (like PyFLP)
        std::vector<EventTree> divide(EventID separator, const std::vector<EventID>& allowed) const;
        std::vector<EventTree> separate(EventID id) const;
        EventTree subtree(std::function<bool(const Event*)> predicate) const;

        // Serialise all events in order
        void writeAll(juce::OutputStream& out) const;

    private:
        std::vector<std::unique_ptr<Event>> m_events;
    };

    // =============================================================================
    // 7.  High-level models
    // =============================================================================

    // Forward declarations
    class Project;
    class Channel;
    class Pattern;
    class Arrangement;
    class Track;
    class Mixer;
    class Insert;
    class Slot;

    // ---- Channel ----
    class Channel
    {
    public:
        virtual ~Channel() = default;
        static std::unique_ptr<Channel> create(EventTree& tree, const FLVersion& version);

        // Identity
        int getIID() const;
        juce::String getName() const;
        void setName(const juce::String& name);
        juce::String getInternalName() const;
        ChannelType getType() const;
        void setType(ChannelType type);

        // Color
        juce::Colour getColor() const;
        void setColor(const juce::Colour& c);

        // State
        bool isEnabled() const;
        void setEnabled(bool enabled);
        bool isZipped() const;
        void setZipped(bool zipped);
        bool isLocked() const;
        void setLocked(bool locked);

        // Volume & Pan
        std::optional<int> getVolume() const;
        void setVolume(int vol);
        std::optional<int> getPan() const;
        void setPan(int pan);
        std::optional<int> getPitchShift() const;
        void setPitchShift(int cents);

        // Filter
        std::optional<int> getFilterCutoff() const;
        void setFilterCutoff(int value);
        std::optional<int> getFilterResonance() const;
        void setFilterResonance(int value);
        std::optional<int> getFilterType() const;
        void setFilterType(int type);

        // Group (display group index)
        int getGroupIndex() const;
        void setGroupIndex(int group);

        // ===== PATCHES FOR flphelper.cpp =====
        // Sample path accessors (Required by Sample Scanner & Batch Processor)
        juce::String getSamplePath() const;
        void setSamplePath(const juce::String& path);

        // EventTree accessors (Required by Plugin Inspector, Stats Generator, Automation Editor)
        const EventTree& getEventTree() const { return m_tree; }
        EventTree& getMutableTree() { return m_tree; }

    protected:
        Channel(EventTree& tree, const FLVersion& version) : m_tree(tree), m_version(version) {}
        EventTree& m_tree;
        FLVersion m_version;

        LevelsEvent* getLevelsEvent() const;
        void ensureLevelsEvent();
    };

    // ---- Pattern ----
    class Pattern
    {
    public:
        Pattern(EventTree& tree, const FLVersion& version) : m_tree(tree), m_version(version) {}
        int getIID() const;
        juce::String getName() const;
        void setName(const juce::String& name);
        juce::Colour getColor() const;
        void setColor(const juce::Colour& c);
        std::vector<Note> getNotes() const;
        void setNotes(const std::vector<Note>& notes);
        std::vector<Controller> getControllers() const;
        void setControllers(const std::vector<Controller>& ctrls);
        int getLength() const; // in PPQ ticks

    private:
        EventTree& m_tree;
        FLVersion m_version;
    };

    // ---- Track ----
    class Track
    {
    public:
        Track(EventTree& tree, const FLVersion& version) : m_tree(tree), m_version(version) {}
        juce::String getName() const;
        void setName(const juce::String& name);
        int getIID() const;
        bool isMuted() const;
        void setMuted(bool muted);
        juce::Colour getColor() const;
        void setColor(const juce::Colour& c);
        uint32_t getMotion() const;
        void setMotion(uint32_t motion);
        uint32_t getPress() const;
        void setPress(uint32_t press);
        uint32_t getTriggerSync() const;
        void setTriggerSync(uint32_t sync);
        bool isQueued() const;
        void setQueued(bool queued);
        bool isTolerant() const;
        void setTolerant(bool tolerant);
        uint32_t getPositionSync() const;
        void setPositionSync(uint32_t sync);
        bool isGrouped() const;
        void setGrouped(bool grouped);
        bool isLocked() const;
        void setLocked(bool locked);
        float getHeight() const;
        void setHeight(float height);
        int32_t getLockedHeight() const;
        void setLockedHeight(int32_t h);
        bool getContentLocked() const;
        void setContentLocked(bool locked);

    private:
        EventTree& m_tree;
        FLVersion m_version;
        TrackInfoEvent* getTrackInfoEvent() const;
    };

    // ---- Arrangement ----
    class Arrangement
    {
    public:
        Arrangement(EventTree& tree, const FLVersion& version, const Project* project, int arrangementIndex)
            : m_tree(tree), m_version(version), m_project(project), m_arrangementIndex(arrangementIndex) {}
        int getIID() const;
        juce::String getName() const;
        void setName(const juce::String& name);
        // Track subtrees are cached on the owning Project, not here, so
        // Arrangement is safe to use as a temporary (e.g.
        // project->getArrangement(0).getTracks() works correctly) - only
        // Project itself needs to outlive the returned Track objects.
        std::vector<Track> getTracks() const;
        std::vector<PlaylistItem> getPlaylistItems() const;
        void setPlaylistItems(const std::vector<PlaylistItem>& items);
        // Time markers not fully implemented

    private:
        EventTree& m_tree;
        FLVersion m_version;
        const Project* m_project;
        int m_arrangementIndex;
    };

    // ---- Mixer ----
    class Slot
    {
    public:
        Slot(EventTree& tree, const FLVersion& version, int index = 0) : m_tree(tree), m_version(version), m_index(index) {}
        int getIndex() const { return m_index; }
        juce::String getName() const;
        void setName(const juce::String& name);
        bool isEnabled() const;
        void setEnabled(bool enabled);
        juce::Colour getColor() const;
        void setColor(const juce::Colour& c);
        // Plugin data not implemented
    private:
        EventTree& m_tree;
        FLVersion m_version;
        int m_index;
    };

    class Insert
    {
    public:
        Insert(EventTree& tree, const FLVersion& version, const Project* project, int index = 0)
            : m_tree(tree), m_version(version), m_project(project), m_index(index) {}
        int getIID() const { return m_index; } // inserts have no explicit IID event; index of appearance is used
        juce::String getName() const;
        void setName(const juce::String& name);
        juce::Colour getColor() const;
        void setColor(const juce::Colour& c);
        // Slot subtrees are cached on the owning Project - see the note on
        // Arrangement above; the same reasoning applies here.
        std::vector<Slot> getSlots() const;
        bool isEnabled() const;
        void setEnabled(bool enabled);
        // more insert properties
    private:
        EventTree& m_tree;
        FLVersion m_version;
        const Project* m_project;
        int m_index;
    };

    class Mixer
    {
    public:
        Mixer(EventTree& tree, const FLVersion& version, const Project* project)
            : m_tree(tree), m_version(version), m_project(project) {}
        // Insert subtrees are cached on the owning Project - see the note on
        // Arrangement above; the same reasoning applies here.
        std::vector<Insert> getInserts() const;
        bool getAPDC() const;
        void setAPDC(bool on);
    private:
        EventTree& m_tree;
        FLVersion m_version;
        const Project* m_project;
    };

    // ---- Project ----
    class Project
    {
    public:
        static std::unique_ptr<Project> load(const juce::File& file, juce::String* errorOut = nullptr);
        void save(const juce::File& file) const;

        // Metadata
        struct Metadata {
            juce::String title;
            juce::String author;
            juce::String genre;
            juce::String comments;
            juce::String webUrl;
            bool showInfoOnStart = true;
        };
        struct UserState {
            bool playbackSong = false;
            bool shuffle = false;
            uint16_t pattern = 0;
        };
        Metadata getMetadata() const;
        UserState getUserState() const;

        // Version and global settings
        FLVersion getVersion() const { return m_version; }
        int getPPQ() const { return m_ppq; }
        double getTempo() const;
        void setTempo(double bpm);
        int getChannelCount() const;
        int getMainPitch() const;
        void setMainPitch(int cents);

        // Access to models
        std::vector<Channel*> getChannels() const; // raw pointers, owned by EventTree
        std::vector<Pattern> getPatterns() const;
        Arrangement getArrangement(int index = 0) const;
        Mixer getMixer() const;

        // Utilities
        std::vector<RemoteControllerEvent*> getAutomationChannels() const;
        std::vector<AutomationPoint> getTempoAutomationPoints() const;

        // ===== PATCHES FOR flphelper.cpp =====
        // Metadata setter (Required by Batch Processor)
        void setMetadata(const Metadata& md);
        // Mutable tree accessor (Required by Cleaner to actually delete unused events)
        EventTree& getMutableEventTree() { return *m_eventTree; }

    private:
        Project() = default;
        std::unique_ptr<EventTree> m_eventTree;
        FLVersion m_version;
        int m_ppq = 96;
        Metadata m_metadata;
        UserState m_userState;

        // Lazy caches. Project must stay alive as long as anything returned
        // from getChannels()/getPatterns()/getArrangement()/getMixer() (or
        // objects derived from them, like Track/Insert/Slot) is in use - see
        // NOTE comments on those methods.
        mutable std::vector<EventTree> m_channelTreeCache;
        mutable std::vector<std::unique_ptr<Channel>> m_channelObjCache;
        mutable bool m_channelsLoaded = false;

        mutable std::vector<EventTree> m_patternTreeCache;
        mutable bool m_patternsLoaded = false;

        mutable std::vector<EventTree> m_arrangementTreeCache;
        mutable bool m_arrangementsLoaded = false;

        mutable EventTree m_mixerTreeCache;
        mutable bool m_mixerLoaded = false;

        mutable EventTree m_emptyTree; // stable fallback for out-of-range arrangement lookups

        // Track/Insert/Slot sub-caches, owned here rather than by Arrangement/
        // Mixer/Insert themselves. Those are lightweight view objects very
        // naturally used as temporaries (e.g. project->getArrangement(0).getTracks()
        // reads perfectly reasonably) - if they owned their own sub-caches,
        // that pattern would silently return dangling references the moment
        // the temporary is destroyed. Owning the caches here instead means
        // any number of temporary Arrangement/Mixer/Insert objects can be
        // constructed and discarded freely; only Project itself needs to
        // outlive the Track/Insert/Slot objects derived from it.
    public:
        std::vector<EventTree>& getOrBuildTrackCache(int arrangementIndex) const;
        std::vector<EventTree>& getOrBuildInsertCache() const;
        std::vector<EventTree>& getOrBuildSlotCache(int insertIndex) const;
    private:
        mutable std::vector<std::vector<EventTree>> m_trackTreeCachePerArrangement;

        mutable std::vector<EventTree> m_insertTreeCache;

        mutable std::vector<std::vector<EventTree>> m_slotTreeCachePerInsert;
    };

} // namespace FL