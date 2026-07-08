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

    // =============================================================================
    // 3.  Version helper
    // =============================================================================
    struct FLVersion
    {
        int major, minor, patch, build = 0;
        bool operator>=(const FLVersion& other) const;
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
        explicit U8Event(uint8_t v) : Event(EventID::IsEnabled), value(v) {}
        void write(juce::OutputStream& out) const override;
        std::unique_ptr<Event> clone() const override { return std::make_unique<U8Event>(value); }
        uint8_t value;
    };

    class BoolEvent final : public Event {
    public:
        explicit BoolEvent(bool v) : Event(EventID::IsEnabled), value(v) {}
        void write(juce::OutputStream& out) const override;
        std::unique_ptr<Event> clone() const override { return std::make_unique<BoolEvent>(value); }
        bool value;
    };

    class I16Event final : public Event {
    public:
        explicit I16Event(int16_t v) : Event(EventID::MainPitch), value(v) {}
        void write(juce::OutputStream& out) const override;
        std::unique_ptr<Event> clone() const override { return std::make_unique<I16Event>(value); }
        int16_t value;
    };

    class U16Event final : public Event {
    public:
        explicit U16Event(uint16_t v) : Event(EventID::NewChan), value(v) {}
        void write(juce::OutputStream& out) const override;
        std::unique_ptr<Event> clone() const override { return std::make_unique<U16Event>(value); }
        uint16_t value;
    };

    class I32Event final : public Event {
    public:
        explicit I32Event(int32_t v) : Event(EventID::Tempo), value(v) {}
        void write(juce::OutputStream& out) const override;
        std::unique_ptr<Event> clone() const override { return std::make_unique<I32Event>(value); }
        int32_t value;
    };

    class U32Event final : public Event {
    public:
        explicit U32Event(uint32_t v) : Event(EventID::Tempo), value(v) {}
        void write(juce::OutputStream& out) const override;
        std::unique_ptr<Event> clone() const override { return std::make_unique<U32Event>(value); }
        uint32_t value;
    };

    class F32Event final : public Event {
    public:
        explicit F32Event(float v) : Event(EventID::IntStretch), value(v) {}
        void write(juce::OutputStream& out) const override;
        std::unique_ptr<Event> clone() const override { return std::make_unique<F32Event>(value); }
        float value;
    };

    class ColorEvent final : public Event {
    public:
        explicit ColorEvent(const juce::Colour& c) : Event(EventID::Color), value(c) {}
        void write(juce::OutputStream& out) const override;
        std::unique_ptr<Event> clone() const override { return std::make_unique<ColorEvent>(value); }
        juce::Colour value;
    };

    class AsciiEvent final : public Event {
    public:
        explicit AsciiEvent(const juce::String& s) : Event(EventID::Text), value(s) {}
        void write(juce::OutputStream& out) const override;
        std::unique_ptr<Event> clone() const override { return std::make_unique<AsciiEvent>(value); }
        juce::String value;
    };

    class UnicodeEvent final : public Event {
    public:
        explicit UnicodeEvent(const juce::String& s) : Event(EventID::Text), value(s) {}
        void write(juce::OutputStream& out) const override;
        std::unique_ptr<Event> clone() const override { return std::make_unique<UnicodeEvent>(value); }
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
        UnknownDataEvent(const uint8_t* data, size_t size) : Event(EventID::Text), m_data(data, size) {}
        void write(juce::OutputStream& out) const override;
        std::unique_ptr<Event> clone() const override { return std::make_unique<UnknownDataEvent>(m_data.getData(), m_data.getSize()); }
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

        std::vector<AutomationPoint> points;
    private:
        static constexpr size_t POINT_SIZE = 24; // 8+8+4+3+1
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
        std::vector<MixerParam> params;
    };

    // ---- WrapperEvent (event 212) ----
    struct WrapperEvent final : public StructEvent {
        WrapperEvent() : StructEvent(EventID::NewPlugin) {}
        void parse(juce::InputStream& in, size_t size) override;
        void writeFields(juce::OutputStream& out) const override;

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
        std::vector<Controller> controllers;
    };

    // ---- InsertDataEvent (event 236) ----
    struct InsertDataEvent final : public StructEvent {
        InsertDataEvent() : StructEvent(EventID::InsertData) {}
        void parse(juce::InputStream& in, size_t size) override;
        void writeFields(juce::OutputStream& out) const override;
        uint32_t flags; // bitfield
    };

    // ---- InsertRoutingEvent (event 235) ----
    class InsertRoutingEvent final : public ListEvent {
    public:
        InsertRoutingEvent() : ListEvent(EventID::InsertRouting) {}
        void parse(juce::InputStream& in, size_t size) override;
        void writeItems(juce::OutputStream& out) const override;
        std::vector<bool> routes;
    };

    // ---- TimestampEvent (event 237) ----
    struct TimestampEvent final : public StructEvent {
        TimestampEvent() : StructEvent(EventID::Timestamp) {}
        void parse(juce::InputStream& in, size_t size) override;
        void writeFields(juce::OutputStream& out) const override;
        double createdOn;   // days since 1899-12-30
        double timeSpent;   // days
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
        Arrangement(EventTree& tree, const FLVersion& version) : m_tree(tree), m_version(version) {}
        int getIID() const;
        juce::String getName() const;
        void setName(const juce::String& name);
        std::vector<Track> getTracks() const;
        std::vector<PlaylistItem> getPlaylistItems() const;
        void setPlaylistItems(const std::vector<PlaylistItem>& items);
        // Time markers not fully implemented

    private:
        EventTree& m_tree;
        FLVersion m_version;
    };

    // ---- Mixer ----
    class Slot
    {
    public:
        Slot(EventTree& tree, const FLVersion& version) : m_tree(tree), m_version(version) {}
        int getIndex() const;
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
    };

    class Insert
    {
    public:
        Insert(EventTree& tree, const FLVersion& version) : m_tree(tree), m_version(version) {}
        int getIID() const;
        juce::String getName() const;
        void setName(const juce::String& name);
        juce::Colour getColor() const;
        void setColor(const juce::Colour& c);
        std::vector<Slot> getSlots() const;
        bool isEnabled() const;
        void setEnabled(bool enabled);
        // more insert properties
    private:
        EventTree& m_tree;
        FLVersion m_version;
    };

    class Mixer
    {
    public:
        Mixer(EventTree& tree, const FLVersion& version) : m_tree(tree), m_version(version) {}
        std::vector<Insert> getInserts() const;
        bool getAPDC() const;
        void setAPDC(bool on);
    private:
        EventTree& m_tree;
        FLVersion m_version;
    };

    // ---- Project ----
    class Project
    {
    public:
        static std::unique_ptr<Project> load(const juce::File& file);
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
    };

} // namespace FL