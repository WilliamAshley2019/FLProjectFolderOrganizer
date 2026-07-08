#include "flp.h"

namespace FL
{

    // =============================================================================
    // 2.  Version helper
    // =============================================================================
    bool FLVersion::operator>=(const FLVersion& other) const
    {
        if (major != other.major) return major > other.major;
        if (minor != other.minor) return minor > other.minor;
        if (patch != other.patch) return patch > other.patch;
        return build >= other.build;
    }

    // =============================================================================
    // 3.  Varint helpers
    // =============================================================================
    static int readVarInt(juce::InputStream& in)
    {
        int result = 0;
        uint8_t byte;
        do {
            if (in.read(&byte, 1) != 1) throw std::runtime_error("Unexpected EOF");
            result = (result << 7) | (byte & 0x7F);
        } while (byte & 0x80);
        return result;
    }

    static void writeVarInt(juce::OutputStream& out, int value)
    {
        uint8_t byte = value & 0x7F;
        while (value > 0x7F) {
            out.writeByte(byte | 0x80);
            value >>= 7;
            byte = value & 0x7F;
        }
        out.writeByte(byte);
    }

    // =============================================================================
    // 4.  Event implementations
    // =============================================================================

    void U8Event::write(juce::OutputStream& out) const
    {
        out.writeByte(static_cast<uint8_t>(m_id));
        out.writeByte(value);
    }

    void BoolEvent::write(juce::OutputStream& out) const
    {
        out.writeByte(static_cast<uint8_t>(m_id));
        out.writeByte(value ? 1 : 0);
    }

    void I16Event::write(juce::OutputStream& out) const
    {
        out.writeByte(static_cast<uint8_t>(m_id));
        uint16_t le = juce::ByteOrder::swapIfBigEndian((uint16_t)value);
        out.write(&le, 2);
    }

    void U16Event::write(juce::OutputStream& out) const
    {
        out.writeByte(static_cast<uint8_t>(m_id));
        uint16_t le = juce::ByteOrder::swapIfBigEndian(value);
        out.write(&le, 2);
    }

    void I32Event::write(juce::OutputStream& out) const
    {
        out.writeByte(static_cast<uint8_t>(m_id));
        uint32_t le = juce::ByteOrder::swapIfBigEndian((uint32_t)value);
        out.write(&le, 4);
    }

    void U32Event::write(juce::OutputStream& out) const
    {
        out.writeByte(static_cast<uint8_t>(m_id));
        uint32_t le = juce::ByteOrder::swapIfBigEndian(value);
        out.write(&le, 4);
    }

    void F32Event::write(juce::OutputStream& out) const
    {
        out.writeByte(static_cast<uint8_t>(m_id));
        uint32_t le = juce::ByteOrder::swapIfBigEndian(juce::ByteOrder::floatToRawLEBytes(value));
        out.write(&le, 4);
    }

    void ColorEvent::write(juce::OutputStream& out) const
    {
        out.writeByte(static_cast<uint8_t>(m_id));
        uint32_t rgba = (uint32_t)(value.getRed() * 255) << 0 |
            (uint32_t)(value.getGreen() * 255) << 8 |
            (uint32_t)(value.getBlue() * 255) << 16 |
            (uint32_t)(value.getAlpha() * 255) << 24;
        uint32_t le = juce::ByteOrder::swapIfBigEndian(rgba);
        out.write(&le, 4);
    }

    void AsciiEvent::write(juce::OutputStream& out) const
    {
        out.writeByte(static_cast<uint8_t>(m_id));
        juce::MemoryOutputStream mos;
        mos << value << '\0';
        juce::String ascii = mos.getData();
        writeVarInt(out, ascii.length());
        out.write(ascii.toRawUTF8(), ascii.length());
    }

    void UnicodeEvent::write(juce::OutputStream& out) const
    {
        out.writeByte(static_cast<uint8_t>(m_id));
        juce::String utf16 = value;
        juce::MemoryOutputStream mos;
        mos.writeUTF16(utf16, true);
        juce::MemoryBlock mb = mos.getMemoryBlock();
        writeVarInt(out, (int)mb.getSize());
        out.write(mb.getData(), mb.getSize());
    }

    void StructEvent::write(juce::OutputStream& out) const
    {
        out.writeByte(static_cast<uint8_t>(m_id));
        juce::MemoryOutputStream mos;
        writeFields(mos);
        juce::MemoryBlock mb = mos.getMemoryBlock();
        if (static_cast<uint8_t>(m_id) >= 192) {
            writeVarInt(out, (int)mb.getSize());
        }
        out.write(mb.getData(), mb.getSize());
    }

    void ListEvent::write(juce::OutputStream& out) const
    {
        out.writeByte(static_cast<uint8_t>(m_id));
        juce::MemoryOutputStream mos;
        writeItems(mos);
        juce::MemoryBlock mb = mos.getMemoryBlock();
        if (static_cast<uint8_t>(m_id) >= 192) {
            writeVarInt(out, (int)mb.getSize());
        }
        out.write(mb.getData(), mb.getSize());
    }

    void UnknownDataEvent::write(juce::OutputStream& out) const
    {
        out.writeByte(static_cast<uint8_t>(m_id));
        if (static_cast<uint8_t>(m_id) >= 192) {
            writeVarInt(out, (int)m_data.getSize());
        }
        out.write(m_data.getData(), m_data.getSize());
    }

    // =============================================================================
    // 5.  Event factory – using juce::ByteOrder for endian conversion
    // =============================================================================
    std::unique_ptr<Event> Event::read(juce::InputStream& in, EventID id, const FLVersion& version)
    {
        uint8_t idVal = static_cast<uint8_t>(id);
        size_t dataSize;
        if (idVal < 64) dataSize = 1;
        else if (idVal < 128) dataSize = 2;
        else if (idVal < 192) dataSize = 4;
        else {
            dataSize = readVarInt(in);
        }

        juce::HeapBlock<uint8_t> buffer(dataSize);
        if (in.read(buffer.get(), dataSize) != (int)dataSize)
            throw std::runtime_error("Failed to read event data");

        juce::MemoryInputStream memIn(buffer.get(), dataSize, false);

        switch (id) {
            // ----- Fixed-size events -----
        case EventID::IsEnabled:      return std::make_unique<BoolEvent>(buffer[0] != 0);
        case EventID::VolByte:        return std::make_unique<U8Event>(buffer[0]);
        case EventID::PanByte:        return std::make_unique<U8Event>(buffer[0]);
        case EventID::Zipped:         return std::make_unique<BoolEvent>(buffer[0] != 0);
        case EventID::LoopActive:     return std::make_unique<BoolEvent>(buffer[0] != 0);
        case EventID::Shuffle:        return std::make_unique<BoolEvent>(buffer[0] != 0);
        case EventID::ShowInfo:       return std::make_unique<BoolEvent>(buffer[0] != 0);
        case EventID::MainVol:        return std::make_unique<U8Event>(buffer[0]);
        case EventID::ChanType:       return std::make_unique<U8Event>(buffer[0]);
        case EventID::PanLaw:         return std::make_unique<U8Event>(buffer[0]);
        case EventID::IsLocked:       return std::make_unique<BoolEvent>(buffer[0] != 0);
        case EventID::TimeSigNumerator: return std::make_unique<U8Event>(buffer[0]);
        case EventID::TimeSigDenominator: return std::make_unique<U8Event>(buffer[0]);
        case EventID::Licensed:       return std::make_unique<BoolEvent>(buffer[0] != 0);
        case EventID::PlayTruncatedNotes: return std::make_unique<BoolEvent>(buffer[0] != 0);

        case EventID::NewChan: {
            uint16_t val = juce::ByteOrder::littleEndianUint16(buffer.get());
            return std::make_unique<U16Event>(val);
        }
        case EventID::NewPat: {
            uint16_t val = juce::ByteOrder::littleEndianUint16(buffer.get());
            return std::make_unique<U16Event>(val);
        }
        case EventID::TempoCoarse: {
            uint16_t val = juce::ByteOrder::littleEndianUint16(buffer.get());
            return std::make_unique<U16Event>(val);
        }
        case EventID::CurrentPatNum: {
            uint16_t val = juce::ByteOrder::littleEndianUint16(buffer.get());
            return std::make_unique<U16Event>(val);
        }
        case EventID::MainPitch: {
            int16_t val = (int16_t)juce::ByteOrder::littleEndianUint16(buffer.get());
            return std::make_unique<I16Event>(val);
        }
        case EventID::Resonance: {
            uint16_t val = juce::ByteOrder::littleEndianUint16(buffer.get());
            return std::make_unique<U16Event>(val);
        }
        case EventID::StereoDelay: {
            uint16_t val = juce::ByteOrder::littleEndianUint16(buffer.get());
            return std::make_unique<U16Event>(val);
        }
        case EventID::Swing: {
            uint16_t val = juce::ByteOrder::littleEndianUint16(buffer.get());
            return std::make_unique<U16Event>(val);
        }
        case EventID::Children: {
            uint16_t val = juce::ByteOrder::littleEndianUint16(buffer.get());
            return std::make_unique<U16Event>(val);
        }
        case EventID::InsertIcon: {
            int16_t val = (int16_t)juce::ByteOrder::littleEndianUint16(buffer.get());
            return std::make_unique<I16Event>(val);
        }
        case EventID::SlotIID: {
            int16_t val = (int16_t)juce::ByteOrder::littleEndianUint16(buffer.get());
            return std::make_unique<I16Event>(val);
        }
        case EventID::ArrangementNew: {
            uint16_t val = juce::ByteOrder::littleEndianUint16(buffer.get());
            return std::make_unique<U16Event>(val);
        }
        case EventID::CurrentlySelected: {
            uint16_t val = juce::ByteOrder::littleEndianUint16(buffer.get());
            return std::make_unique<U16Event>(val);
        }

        case EventID::Color:
        case EventID::PatternColor:
        case EventID::InsertColor:
            return std::make_unique<ColorEvent>(juce::Colour::fromRGBA(buffer[0], buffer[1], buffer[2], buffer[3]));

        case EventID::PluginIcon: {
            uint32_t val = juce::ByteOrder::littleEndianUint32(buffer.get());
            return std::make_unique<U32Event>(val);
        }
        case EventID::Tempo: {
            uint32_t val = juce::ByteOrder::littleEndianUint32(buffer.get());
            return std::make_unique<U32Event>(val);
        }
        case EventID::MarkerPosition: {
            uint32_t val = juce::ByteOrder::littleEndianUint32(buffer.get());
            return std::make_unique<U32Event>(val);
        }
        case EventID::WindowHeight: {
            uint32_t val = juce::ByteOrder::littleEndianUint32(buffer.get());
            return std::make_unique<U32Event>(val);
        }
        case EventID::GroupNum: {
            int32_t val = (int32_t)juce::ByteOrder::littleEndianUint32(buffer.get());
            return std::make_unique<I32Event>(val);
        }
        case EventID::SamplerFlags: {
            uint32_t val = juce::ByteOrder::littleEndianUint32(buffer.get());
            return std::make_unique<U32Event>(val);
        }
        case EventID::LayerFlags: {
            uint32_t val = juce::ByteOrder::littleEndianUint32(buffer.get());
            return std::make_unique<U32Event>(val);
        }
        case EventID::PatternSteps: {
            uint32_t val = juce::ByteOrder::littleEndianUint32(buffer.get());
            return std::make_unique<U32Event>(val);
        }
        case EventID::VerBuild: {
            uint32_t val = juce::ByteOrder::littleEndianUint32(buffer.get());
            return std::make_unique<U32Event>(val);
        }
        case EventID::VersionBuild: {
            uint32_t val = juce::ByteOrder::littleEndianUint32(buffer.get());
            return std::make_unique<U32Event>(val);
        }
        case EventID::IntStretch: {
            float val = juce::ByteOrder::littleEndianFloat(buffer.get());
            return std::make_unique<F32Event>(val);
        }

        case EventID::MarkerText:
        case EventID::PatName:
        case EventID::Title:
        case EventID::Comment:
        case EventID::URL:
        case EventID::CommentRTF:
        case EventID::Licensee:
        case EventID::DataPath:
        case EventID::Genre:
        case EventID::Author:
        case EventID::TrackName:
        case EventID::ArrangementName:
        case EventID::CategoryName:
        case EventID::PluginFactory:
        case EventID::PluginName:
        case EventID::SampleFileName:
        case EventID::Version:
            if (version >= FLVersion{ 11,5,0,0 })
                return std::make_unique<UnicodeEvent>(juce::String::fromUTF16(reinterpret_cast<const juce::uint16*>(buffer.get()), (int)dataSize / 2));
            else
                return std::make_unique<AsciiEvent>(juce::String::fromUTF8(buffer.get(), (int)dataSize));

            // ----- Structured events -----
        case EventID::TrackInfo: {
            auto ev = std::make_unique<TrackInfoEvent>();
            ev->parse(memIn, dataSize);
            return ev;
        }
        case EventID::Playlist: {
            auto ev = std::make_unique<PlaylistEvent>();
            ev->parse(memIn, dataSize);
            return ev;
        }
        case EventID::Levels: {
            auto ev = std::make_unique<LevelsEvent>();
            ev->parse(memIn, dataSize);
            return ev;
        }
        case EventID::ChanParams: {
            auto ev = std::make_unique<ChannelBlobEvent>();
            ev->parse(memIn, dataSize);
            return ev;
        }
        case EventID::RemoteController: {
            auto ev = std::make_unique<RemoteControllerEvent>();
            ev->parse(memIn, dataSize);
            return ev;
        }
        case EventID::Automation: {
            auto ev = std::make_unique<AutomationEvent>();
            ev->parse(memIn, dataSize);
            return ev;
        }
        case EventID::MixerBlob: {
            auto ev = std::make_unique<MixerBlobEvent>();
            ev->parse(memIn, dataSize);
            return ev;
        }
        case EventID::NewPlugin: {
            auto ev = std::make_unique<WrapperEvent>();
            ev->parse(memIn, dataSize);
            return ev;
        }
        case EventID::PatternNotes: {
            auto ev = std::make_unique<PatternNotesEvent>();
            ev->parse(memIn, dataSize);
            return ev;
        }
        case EventID::PatternCtrls: {
            auto ev = std::make_unique<PatternCtrlsEvent>();
            ev->parse(memIn, dataSize);
            return ev;
        }
        case EventID::InsertData: {
            auto ev = std::make_unique<InsertDataEvent>();
            ev->parse(memIn, dataSize);
            return ev;
        }
        case EventID::InsertRouting: {
            auto ev = std::make_unique<InsertRoutingEvent>();
            ev->parse(memIn, dataSize);
            return ev;
        }
        case EventID::Timestamp: {
            auto ev = std::make_unique<TimestampEvent>();
            ev->parse(memIn, dataSize);
            return ev;
        }
        default:
            return std::make_unique<UnknownDataEvent>(buffer.get(), dataSize);
        }
    }

    // =============================================================================
    // 6.  Specific event implementations – using juce::ByteOrder
    // =============================================================================

    void TrackInfoEvent::parse(juce::InputStream& in, size_t size)
    {
        auto& f = fields;
        if (size < 4) return;
        uint32_t v; in.read(&v, 4); f.iid = juce::ByteOrder::littleEndianUint32(&v);
        if (size < 8) return;
        uint8_t r, g, b, a; in.read(&r, 1); in.read(&g, 1); in.read(&b, 1); in.read(&a, 1);
        f.color = juce::Colour(r, g, b);
        if (size < 12) return;
        in.read(&v, 4); f.icon = juce::ByteOrder::littleEndianUint32(&v);
        if (size < 13) return;
        uint8_t en; in.read(&en, 1); f.enabled = (en != 0);
        if (size < 17) return;
        float fl; in.read(&fl, 4); f.height = juce::ByteOrder::littleEndianFloat(&fl);
        if (size < 21) return;
        int32_t lh; in.read(&lh, 4); f.lockedHeight = (int32_t)juce::ByteOrder::littleEndianUint32(&lh);
        if (size < 22) return;
        uint8_t cl; in.read(&cl, 1); f.contentLocked = (cl != 0);
        if (size < 26) return;
        in.read(&v, 4); f.motion = juce::ByteOrder::littleEndianUint32(&v);
        if (size < 30) return;
        in.read(&v, 4); f.press = juce::ByteOrder::littleEndianUint32(&v);
        if (size < 34) return;
        in.read(&v, 4); f.triggerSync = juce::ByteOrder::littleEndianUint32(&v);
        if (size < 38) return;
        uint32_t q; in.read(&q, 4); f.queued = (q != 0);
        if (size < 42) return;
        uint32_t tol; in.read(&tol, 4); f.tolerant = (tol != 0);
        if (size < 46) return;
        in.read(&v, 4); f.positionSync = juce::ByteOrder::littleEndianUint32(&v);
        if (size < 47) return;
        uint8_t gr; in.read(&gr, 1); f.grouped = (gr != 0);
        if (size < 48) return;
        uint8_t lk; in.read(&lk, 1); f.locked = (lk != 0);
    }

    void TrackInfoEvent::writeFields(juce::OutputStream& out) const
    {
        auto writeU32 = [&](uint32_t val) { uint32_t le = juce::ByteOrder::swapIfBigEndian(val); out.write(&le, 4); };
        auto writeI32 = [&](int32_t val) { uint32_t le = juce::ByteOrder::swapIfBigEndian((uint32_t)val); out.write(&le, 4); };
        auto writeFloat = [&](float val) { uint32_t le = juce::ByteOrder::swapIfBigEndian(juce::ByteOrder::floatToRawLEBytes(val)); out.write(&le, 4); };
        auto writeBool = [&](bool val) { out.writeByte(val ? 1 : 0); };

        writeU32(fields.iid);
        out.writeByte(fields.color.getRed());
        out.writeByte(fields.color.getGreen());
        out.writeByte(fields.color.getBlue());
        out.writeByte(fields.color.getAlpha());
        writeU32(fields.icon);
        writeBool(fields.enabled);
        writeFloat(fields.height);
        writeI32(fields.lockedHeight);
        writeBool(fields.contentLocked);
        writeU32(fields.motion);
        writeU32(fields.press);
        writeU32(fields.triggerSync);
        writeU32(fields.queued ? 1 : 0);
        writeU32(fields.tolerant ? 1 : 0);
        writeU32(fields.positionSync);
        writeBool(fields.grouped);
        writeBool(fields.locked);
    }

    int PlaylistEvent::detectItemSize(const uint8_t* data, size_t totalSize, int offset)
    {
        const int candidates[] = { 32, 60, 64, 80, 320 };
        for (int cand : candidates) {
            if (totalSize % cand != 0) continue;
            int n = totalSize / cand;
            bool ok = true;
            for (int i = 0; i < n; ++i) {
                uint16_t pb = juce::ByteOrder::littleEndianUint16(data + offset + i * cand + 4);
                if (pb != 20480) { ok = false; break; }
            }
            if (ok) return cand;
        }
        int best = 0;
        float bestRatio = 0.5f;
        for (int cand : candidates) {
            if (totalSize % cand != 0) continue;
            int n = totalSize / cand;
            int good = 0;
            for (int i = 0; i < n; ++i) {
                uint16_t pb = juce::ByteOrder::littleEndianUint16(data + offset + i * cand + 4);
                if (pb == 20480) ++good;
            }
            float ratio = (float)good / n;
            if (ratio > bestRatio) { bestRatio = ratio; best = cand; }
        }
        return best;
    }

    void PlaylistEvent::parse(juce::InputStream& in, size_t size)
    {
        juce::HeapBlock<uint8_t> buf(size);
        if (in.read(buf.get(), size) != (int)size)
            throw std::runtime_error("Failed to read Playlist data");
        int itemSize = detectItemSize(buf.get(), size, 0);
        if (itemSize == 0) { m_itemSize = 80; return; }
        m_itemSize = itemSize;
        int count = size / itemSize;
        items.clear();
        items.reserve(count);
        for (int i = 0; i < count; ++i) {
            int base = i * itemSize;
            PlaylistItem it;
            it.position = juce::ByteOrder::littleEndianUint32(buf.get() + base);
            it.patternBase = juce::ByteOrder::littleEndianUint16(buf.get() + base + 4);
            it.itemIndex = juce::ByteOrder::littleEndianUint16(buf.get() + base + 6);
            it.length = juce::ByteOrder::littleEndianUint32(buf.get() + base + 8);
            it.trackRvidx = juce::ByteOrder::littleEndianUint16(buf.get() + base + 12);
            it.group = juce::ByteOrder::littleEndianUint16(buf.get() + base + 14);
            it.itemFlags = juce::ByteOrder::littleEndianUint16(buf.get() + base + 18);
            it.startOffset = juce::ByteOrder::littleEndianFloat(buf.get() + base + 24);
            it.endOffset = juce::ByteOrder::littleEndianFloat(buf.get() + base + 28);
            items.push_back(it);
        }
    }

    void PlaylistEvent::writeItems(juce::OutputStream& out) const
    {
        for (const auto& it : items) {
            juce::MemoryOutputStream mos;
            uint32_t val = juce::ByteOrder::swapIfBigEndian(it.position); mos.write(&val, 4);
            uint16_t val16 = juce::ByteOrder::swapIfBigEndian(it.patternBase); mos.write(&val16, 2);
            val16 = juce::ByteOrder::swapIfBigEndian(it.itemIndex); mos.write(&val16, 2);
            val = juce::ByteOrder::swapIfBigEndian(it.length); mos.write(&val, 4);
            val16 = juce::ByteOrder::swapIfBigEndian(it.trackRvidx); mos.write(&val16, 2);
            val16 = juce::ByteOrder::swapIfBigEndian(it.group); mos.write(&val16, 2);
            val16 = 0x0078; mos.write(&val16, 2);
            val16 = juce::ByteOrder::swapIfBigEndian(it.itemFlags); mos.write(&val16, 2);
            uint32_t magic = 0x80806440; val = juce::ByteOrder::swapIfBigEndian(magic); mos.write(&val, 4);
            float f = juce::ByteOrder::swapIfBigEndian(it.startOffset); mos.write(&f, 4);
            f = juce::ByteOrder::swapIfBigEndian(it.endOffset); mos.write(&f, 4);
            size_t written = mos.getDataSize();
            if (written < (size_t)m_itemSize) {
                int padding = m_itemSize - (int)written;
                for (int i = 0; i < padding; ++i) mos.writeByte(0);
            }
            out.write(mos.getData(), mos.getDataSize());
        }
    }

    void LevelsEvent::parse(juce::InputStream& in, size_t size)
    {
        auto& f = fields;
        if (size >= 4) { uint32_t v; in.read(&v, 4); f.pan = juce::ByteOrder::littleEndianUint32(&v); }
        if (size >= 8) { uint32_t v; in.read(&v, 4); f.volume = juce::ByteOrder::littleEndianUint32(&v); }
        if (size >= 12) { int32_t v; in.read(&v, 4); f.pitchShift = (int32_t)juce::ByteOrder::littleEndianUint32(&v); }
        if (size >= 16) { uint32_t v; in.read(&v, 4); f.filterModX = juce::ByteOrder::littleEndianUint32(&v); }
        if (size >= 20) { uint32_t v; in.read(&v, 4); f.filterModY = juce::ByteOrder::littleEndianUint32(&v); }
        if (size >= 24) { uint32_t v; in.read(&v, 4); f.filterType = juce::ByteOrder::littleEndianUint32(&v); }
    }

    void LevelsEvent::writeFields(juce::OutputStream& out) const
    {
        auto writeOptU32 = [&](const std::optional<uint32_t>& opt) {
            if (opt) {
                uint32_t le = juce::ByteOrder::swapIfBigEndian(*opt);
                out.write(&le, 4);
            }
            else {
                uint32_t zero = 0; out.write(&zero, 4);
            }
            };
        auto writeOptI32 = [&](const std::optional<int32_t>& opt) {
            if (opt) {
                uint32_t le = juce::ByteOrder::swapIfBigEndian((uint32_t)*opt);
                out.write(&le, 4);
            }
            else {
                uint32_t zero = 0; out.write(&zero, 4);
            }
            };
        writeOptU32(fields.pan);
        writeOptU32(fields.volume);
        writeOptI32(fields.pitchShift);
        writeOptU32(fields.filterModX);
        writeOptU32(fields.filterModY);
        writeOptU32(fields.filterType);
    }

    void ChannelBlobEvent::parse(juce::InputStream& in, size_t size)
    {
        rawData.setSize(size);
        if (size > 0) {
            if (in.read(rawData.getData(), size) != (int)size)
                throw std::runtime_error("Failed to read ChannelBlob data");
        }
    }
    void ChannelBlobEvent::writeFields(juce::OutputStream& out) const
    {
        out.write(rawData.getData(), rawData.getSize());
    }

    bool ChannelBlobEvent::getNoDC() const {
        if (rawData.getSize() < 10) return false;
        return rawData[9] != 0;
    }
    void ChannelBlobEvent::setNoDC(bool val) {
        if (rawData.getSize() < 10) return;
        rawData[9] = val ? 1 : 0;
    }
    uint8_t ChannelBlobEvent::getDelayFlags() const {
        if (rawData.getSize() < 11) return 0;
        return rawData[10];
    }
    void ChannelBlobEvent::setDelayFlags(uint8_t flags) {
        if (rawData.getSize() < 11) return;
        rawData[10] = flags;
    }
    bool ChannelBlobEvent::getUseMainPitch() const {
        if (rawData.getSize() < 12) return false;
        return rawData[11] != 0;
    }
    void ChannelBlobEvent::setUseMainPitch(bool val) {
        if (rawData.getSize() < 12) return;
        rawData[11] = val ? 1 : 0;
    }
    uint32_t ChannelBlobEvent::getArpDirection() const {
        if (rawData.getSize() < 44) return 0;
        return juce::ByteOrder::littleEndianUint32((uint8_t*)rawData.getData() + 40);
    }
    void ChannelBlobEvent::setArpDirection(uint32_t dir) {
        if (rawData.getSize() < 44) return;
        uint32_t le = juce::ByteOrder::swapIfBigEndian(dir);
        memcpy((uint8_t*)rawData.getData() + 40, &le, 4);
    }

    void RemoteControllerEvent::parse(juce::InputStream& in, size_t size)
    {
        if (size < 22) return;
        in.read(&fields.unknown1, 2);
        fields.unknown1 = juce::ByteOrder::littleEndianUint16(&fields.unknown1);
        in.read(&fields.trackId, 4);
        fields.trackId = juce::ByteOrder::littleEndianUint32(&fields.trackId);
        in.read(&fields.unknown2, 2);
        fields.unknown2 = juce::ByteOrder::littleEndianUint16(&fields.unknown2);
        in.read(&fields.paramId, 2);
        fields.paramId = juce::ByteOrder::littleEndianUint16(&fields.paramId);
        in.read(&fields.destId, 2);
        fields.destId = juce::ByteOrder::littleEndianUint16(&fields.destId);
        in.read(&fields.unknown3, 4);
        fields.unknown3 = juce::ByteOrder::littleEndianUint32(&fields.unknown3);
        in.read(&fields.unknown4, 4);
        fields.unknown4 = juce::ByteOrder::littleEndianUint32(&fields.unknown4);
    }
    void RemoteControllerEvent::writeFields(juce::OutputStream& out) const
    {
        auto writeU16 = [&](uint16_t val) { uint16_t le = juce::ByteOrder::swapIfBigEndian(val); out.write(&le, 2); };
        auto writeU32 = [&](uint32_t val) { uint32_t le = juce::ByteOrder::swapIfBigEndian(val); out.write(&le, 4); };
        writeU16(fields.unknown1);
        writeU32(fields.trackId);
        writeU16(fields.unknown2);
        writeU16(fields.paramId);
        writeU16(fields.destId);
        writeU32(fields.unknown3);
        writeU32(fields.unknown4);
    }

    void AutomationEvent::parse(juce::InputStream& in, size_t size)
    {
        juce::HeapBlock<uint8_t> header(20);
        if (in.read(header.get(), 20) != 20) throw std::runtime_error("Failed to read automation header");
        uint32_t numPoints;
        if (in.read(&numPoints, 4) != 4) throw std::runtime_error("Failed to read num points");
        numPoints = juce::ByteOrder::littleEndianUint32(&numPoints);
        points.clear();
        points.reserve(numPoints);
        for (uint32_t i = 0; i < numPoints; ++i) {
            AutomationPoint p;
            if (in.read(&p.beatIncrement, 8) != 8) throw std::runtime_error("Failed to read beat increment");
            p.beatIncrement = juce::ByteOrder::littleEndianDouble(&p.beatIncrement);
            if (in.read(&p.value, 8) != 8) throw std::runtime_error("Failed to read value");
            p.value = juce::ByteOrder::littleEndianDouble(&p.value);
            if (in.read(&p.tension, 4) != 4) throw std::runtime_error("Failed to read tension");
            p.tension = juce::ByteOrder::littleEndianFloat(&p.tension);
            if (in.read(p.unknown3, 3) != 3) throw std::runtime_error("Failed to read unknown3");
            if (in.read(&p.direction, 1) != 1) throw std::runtime_error("Failed to read direction");
            points.push_back(p);
        }
    }
    void AutomationEvent::writeItems(juce::OutputStream& out) const
    {
        for (int i = 0; i < 20; ++i) out.writeByte(0);
        uint32_t num = (uint32_t)points.size();
        uint32_t le = juce::ByteOrder::swapIfBigEndian(num);
        out.write(&le, 4);
        for (const auto& p : points) {
            double inc = juce::ByteOrder::swapIfBigEndian(p.beatIncrement);
            out.write(&inc, 8);
            double val = juce::ByteOrder::swapIfBigEndian(p.value);
            out.write(&val, 8);
            float tens = juce::ByteOrder::swapIfBigEndian(p.tension);
            out.write(&tens, 4);
            out.write(p.unknown3, 3);
            out.writeByte(p.direction);
        }
    }

    void MixerBlobEvent::parse(juce::InputStream& in, size_t size)
    {
        params.clear();
        while (size >= 12) {
            MixerParam p;
            uint32_t u4; in.read(&u4, 4);
            in.read(&p.id, 1);
            in.read(&p._u1, 1);
            in.read(&p.channelData, 2);
            p.channelData = juce::ByteOrder::littleEndianUint16(&p.channelData);
            in.read(&p.msg, 4);
            p.msg = (int32_t)juce::ByteOrder::littleEndianUint32(&p.msg);
            params.push_back(p);
            size -= 12;
        }
    }
    void MixerBlobEvent::writeItems(juce::OutputStream& out) const
    {
        for (const auto& p : params) {
            uint32_t u4 = 0; out.write(&u4, 4);
            out.writeByte(p.id);
            out.writeByte(p._u1);
            uint16_t cd = juce::ByteOrder::swapIfBigEndian(p.channelData);
            out.write(&cd, 2);
            uint32_t msg = juce::ByteOrder::swapIfBigEndian((uint32_t)p.msg);
            out.write(&msg, 4);
        }
    }

    void WrapperEvent::parse(juce::InputStream& in, size_t size)
    {
        if (size < 21) return;
        in.skipBytes(16);
        in.read(&fields.flags, 2);
        fields.flags = juce::ByteOrder::littleEndianUint16(&fields.flags);
        in.skipBytes(2);
        in.read(&fields.page, 1);
        in.skipBytes(23);
        if (size >= 21 + 4 + 4) {
            in.read(&fields.width, 4);
            fields.width = juce::ByteOrder::littleEndianUint32(&fields.width);
            in.read(&fields.height, 4);
            fields.height = juce::ByteOrder::littleEndianUint32(&fields.height);
        }
    }
    void WrapperEvent::writeFields(juce::OutputStream& out) const
    {
        for (int i = 0; i < 16; ++i) out.writeByte(0);
        uint16_t fl = juce::ByteOrder::swapIfBigEndian(fields.flags);
        out.write(&fl, 2);
        uint16_t zero16 = 0; out.write(&zero16, 2);
        out.writeByte(fields.page);
        for (int i = 0; i < 23; ++i) out.writeByte(0);
        uint32_t w = juce::ByteOrder::swapIfBigEndian(fields.width);
        out.write(&w, 4);
        uint32_t h = juce::ByteOrder::swapIfBigEndian(fields.height);
        out.write(&h, 4);
    }

    void PatternNotesEvent::parse(juce::InputStream& in, size_t size)
    {
        notes.clear();
        while (size >= 24) {
            Note n;
            in.read(&n.position, 4); n.position = juce::ByteOrder::littleEndianUint32(&n.position);
            in.read(&n.flags, 2); n.flags = juce::ByteOrder::littleEndianUint16(&n.flags);
            in.read(&n.channelIID, 2); n.channelIID = juce::ByteOrder::littleEndianUint16(&n.channelIID);
            in.read(&n.length, 4); n.length = juce::ByteOrder::littleEndianUint32(&n.length);
            in.read(&n.key, 2); n.key = juce::ByteOrder::littleEndianUint16(&n.key);
            in.read(&n.group, 2); n.group = juce::ByteOrder::littleEndianUint16(&n.group);
            in.read(&n.finePitch, 1);
            in.skipBytes(1);
            in.read(&n.release, 1);
            in.read(&n.midiChannel, 1);
            in.read(&n.pan, 1);
            in.read(&n.velocity, 1);
            in.read(&n.modX, 1);
            in.read(&n.modY, 1);
            notes.push_back(n);
            size -= 24;
        }
    }
    void PatternNotesEvent::writeItems(juce::OutputStream& out) const
    {
        for (const auto& n : notes) {
            uint32_t pos = juce::ByteOrder::swapIfBigEndian(n.position); out.write(&pos, 4);
            uint16_t flags = juce::ByteOrder::swapIfBigEndian(n.flags); out.write(&flags, 2);
            uint16_t ciid = juce::ByteOrder::swapIfBigEndian(n.channelIID); out.write(&ciid, 2);
            uint32_t len = juce::ByteOrder::swapIfBigEndian(n.length); out.write(&len, 4);
            uint16_t key = juce::ByteOrder::swapIfBigEndian(n.key); out.write(&key, 2);
            uint16_t grp = juce::ByteOrder::swapIfBigEndian(n.group); out.write(&grp, 2);
            out.writeByte(n.finePitch);
            out.writeByte(0);
            out.writeByte(n.release);
            out.writeByte(n.midiChannel);
            out.writeByte(n.pan);
            out.writeByte(n.velocity);
            out.writeByte(n.modX);
            out.writeByte(n.modY);
        }
    }

    void PatternCtrlsEvent::parse(juce::InputStream& in, size_t size)
    {
        controllers.clear();
        while (size >= 4 + 1 + 1 + 4) {
            Controller c;
            in.read(&c.position, 4); c.position = juce::ByteOrder::littleEndianUint32(&c.position);
            in.skipBytes(2);
            in.read(&c.channelIID, 1);
            in.read(&c.flags, 1);
            in.read(&c.value, 4); c.value = juce::ByteOrder::littleEndianFloat(&c.value);
            controllers.push_back(c);
            size -= 12;
        }
    }
    void PatternCtrlsEvent::writeItems(juce::OutputStream& out) const
    {
        for (const auto& c : controllers) {
            uint32_t pos = juce::ByteOrder::swapIfBigEndian(c.position); out.write(&pos, 4);
            uint16_t zero16 = 0; out.write(&zero16, 2);
            out.writeByte(c.channelIID);
            out.writeByte(c.flags);
            float val = juce::ByteOrder::swapIfBigEndian(c.value); out.write(&val, 4);
        }
    }

    void InsertDataEvent::parse(juce::InputStream& in, size_t size)
    {
        if (size < 8) return;
        in.skipBytes(4);
        in.read(&flags, 4); flags = juce::ByteOrder::littleEndianUint32(&flags);
    }
    void InsertDataEvent::writeFields(juce::OutputStream& out) const
    {
        uint32_t u4 = 0; out.write(&u4, 4);
        uint32_t f = juce::ByteOrder::swapIfBigEndian(flags);
        out.write(&f, 4);
    }

    void InsertRoutingEvent::parse(juce::InputStream& in, size_t size)
    {
        routes.clear();
        for (size_t i = 0; i < size; ++i) {
            uint8_t b; in.read(&b, 1); routes.push_back(b != 0);
        }
    }
    void InsertRoutingEvent::writeItems(juce::OutputStream& out) const
    {
        for (bool r : routes) out.writeByte(r ? 1 : 0);
    }

    void TimestampEvent::parse(juce::InputStream& in, size_t size)
    {
        if (size < 16) return;
        in.read(&createdOn, 8); createdOn = juce::ByteOrder::littleEndianDouble(&createdOn);
        in.read(&timeSpent, 8); timeSpent = juce::ByteOrder::littleEndianDouble(&timeSpent);
    }
    void TimestampEvent::writeFields(juce::OutputStream& out) const
    {
        double co = juce::ByteOrder::swapIfBigEndian(createdOn);
        out.write(&co, 8);
        double ts = juce::ByteOrder::swapIfBigEndian(timeSpent);
        out.write(&ts, 8);
    }

    // =============================================================================
    // 7.  EventTree methods (unchanged)
    // =============================================================================
    void EventTree::addEvent(std::unique_ptr<Event> event)
    {
        m_events.push_back(std::move(event));
    }
    void EventTree::removeEvent(EventID id, int index)
    {
        auto it = std::find_if(m_events.begin(), m_events.end(),
            [id, &index](const auto& e) { if (e->id() == id && index-- == 0) return true; return false; });
        if (it != m_events.end())
            m_events.erase(it);
    }
    std::vector<Event*> EventTree::getEvents(EventID id) const
    {
        std::vector<Event*> result;
        for (auto& e : m_events)
            if (e->id() == id) result.push_back(e.get());
        return result;
    }
    Event* EventTree::firstEvent(EventID id) const
    {
        for (auto& e : m_events)
            if (e->id() == id) return e.get();
        return nullptr;
    }
    bool EventTree::hasEvent(EventID id) const
    {
        return firstEvent(id) != nullptr;
    }
    void EventTree::writeAll(juce::OutputStream& out) const
    {
        for (auto& e : m_events)
            e->write(out);
    }

    // ---- Grouping methods ----
    std::vector<EventTree> EventTree::divide(EventID separator, const std::vector<EventID>& allowed) const
    {
        std::vector<EventTree> result;
        std::vector<std::unique_ptr<Event>> current;
        for (auto& e : m_events) {
            if (e->id() == separator) {
                if (!current.empty()) {
                    EventTree subtree;
                    subtree.m_events = std::move(current);
                    result.push_back(std::move(subtree));
                    current.clear();
                }
            }
            else if (std::find(allowed.begin(), allowed.end(), e->id()) != allowed.end()) {
                current.push_back(std::unique_ptr<Event>(e->clone()));
            }
        }
        if (!current.empty()) {
            EventTree subtree;
            subtree.m_events = std::move(current);
            result.push_back(std::move(subtree));
        }
        return result;
    }

    std::vector<EventTree> EventTree::separate(EventID id) const
    {
        std::vector<EventTree> result;
        for (auto& e : m_events) {
            if (e->id() == id) {
                EventTree subtree;
                subtree.addEvent(std::unique_ptr<Event>(e->clone()));
                result.push_back(std::move(subtree));
            }
        }
        return result;
    }

    EventTree EventTree::subtree(std::function<bool(const Event*)> predicate) const
    {
        EventTree sub;
        for (auto& e : m_events) {
            if (predicate(e.get()))
                sub.addEvent(std::unique_ptr<Event>(e->clone()));
        }
        return sub;
    }

    // =============================================================================
    // 8.  High-level model implementations (unchanged)
    // =============================================================================

    // ---- Channel ----
    int Channel::getIID() const
    {
        auto ev = m_tree.firstEvent(EventID::NewChan);
        if (ev && dynamic_cast<U16Event*>(ev))
            return static_cast<U16Event*>(ev)->value;
        return -1;
    }

    juce::String Channel::getName() const
    {
        auto ev = m_tree.firstEvent(EventID::PluginName);
        if (ev) {
            if (auto* a = dynamic_cast<AsciiEvent*>(ev)) return a->value;
            if (auto* u = dynamic_cast<UnicodeEvent*>(ev)) return u->value;
        }
        return {};
    }
    void Channel::setName(const juce::String& name)
    {
        m_tree.removeEvent(EventID::PluginName);
        std::unique_ptr<Event> ev;
        if (m_version >= FLVersion{ 11,5,0,0 })
            ev = std::make_unique<UnicodeEvent>(name);
        else
            ev = std::make_unique<AsciiEvent>(name);
        m_tree.addEvent(std::move(ev));
    }

    juce::String Channel::getInternalName() const
    {
        auto ev = m_tree.firstEvent(EventID::PluginFactory);
        if (ev) {
            if (auto* a = dynamic_cast<AsciiEvent*>(ev)) return a->value;
            if (auto* u = dynamic_cast<UnicodeEvent*>(ev)) return u->value;
        }
        return {};
    }

    ChannelType Channel::getType() const
    {
        auto ev = m_tree.firstEvent(EventID::ChanType);
        if (ev && dynamic_cast<U8Event*>(ev))
            return static_cast<ChannelType>(static_cast<U8Event*>(ev)->value);
        return ChannelType::Sampler;
    }
    void Channel::setType(ChannelType type)
    {
        m_tree.removeEvent(EventID::ChanType);
        m_tree.addEvent(std::make_unique<U8Event>((uint8_t)type));
    }

    juce::Colour Channel::getColor() const
    {
        auto ev = m_tree.firstEvent(EventID::Color);
        if (ev && dynamic_cast<ColorEvent*>(ev))
            return static_cast<ColorEvent*>(ev)->value;
        return juce::Colours::grey;
    }
    void Channel::setColor(const juce::Colour& c)
    {
        m_tree.removeEvent(EventID::Color);
        m_tree.addEvent(std::make_unique<ColorEvent>(c));
    }

    bool Channel::isEnabled() const
    {
        auto ev = m_tree.firstEvent(EventID::IsEnabled);
        if (ev && dynamic_cast<BoolEvent*>(ev))
            return static_cast<BoolEvent*>(ev)->value;
        return true;
    }
    void Channel::setEnabled(bool enabled)
    {
        m_tree.removeEvent(EventID::IsEnabled);
        m_tree.addEvent(std::make_unique<BoolEvent>(enabled));
    }

    bool Channel::isZipped() const
    {
        auto ev = m_tree.firstEvent(EventID::Zipped);
        if (ev && dynamic_cast<BoolEvent*>(ev))
            return static_cast<BoolEvent*>(ev)->value;
        return false;
    }
    void Channel::setZipped(bool zipped)
    {
        m_tree.removeEvent(EventID::Zipped);
        m_tree.addEvent(std::make_unique<BoolEvent>(zipped));
    }

    bool Channel::isLocked() const
    {
        auto ev = m_tree.firstEvent(EventID::IsLocked);
        if (ev && dynamic_cast<BoolEvent*>(ev))
            return static_cast<BoolEvent*>(ev)->value;
        return false;
    }
    void Channel::setLocked(bool locked)
    {
        m_tree.removeEvent(EventID::IsLocked);
        m_tree.addEvent(std::make_unique<BoolEvent>(locked));
    }

    LevelsEvent* Channel::getLevelsEvent() const
    {
        auto ev = m_tree.firstEvent(EventID::Levels);
        return dynamic_cast<LevelsEvent*>(ev);
    }
    void Channel::ensureLevelsEvent()
    {
        if (!m_tree.hasEvent(EventID::Levels))
            m_tree.addEvent(std::make_unique<LevelsEvent>());
    }

    std::optional<int> Channel::getVolume() const
    {
        auto* levels = getLevelsEvent();
        if (levels && levels->fields.volume.has_value())
            return (int)*levels->fields.volume;
        auto evWord = m_tree.firstEvent(EventID::VolByte);
        if (evWord && dynamic_cast<U8Event*>(evWord))
            return static_cast<U8Event*>(evWord)->value;
        return std::nullopt;
    }
    void Channel::setVolume(int vol)
    {
        auto* levels = getLevelsEvent();
        if (levels) {
            levels->fields.volume = (uint32_t)vol;
            return;
        }
        m_tree.removeEvent(EventID::VolByte);
        m_tree.addEvent(std::make_unique<U16Event>((uint16_t)vol));
    }

    std::optional<int> Channel::getPan() const
    {
        auto* levels = getLevelsEvent();
        if (levels && levels->fields.pan.has_value())
            return (int)*levels->fields.pan;
        auto ev = m_tree.firstEvent(EventID::PanByte);
        if (ev && dynamic_cast<U8Event*>(ev))
            return static_cast<U8Event*>(ev)->value;
        return std::nullopt;
    }
    void Channel::setPan(int pan)
    {
        auto* levels = getLevelsEvent();
        if (levels) {
            levels->fields.pan = (uint32_t)pan;
            return;
        }
        m_tree.removeEvent(EventID::PanByte);
        m_tree.addEvent(std::make_unique<U16Event>((uint16_t)pan));
    }

    std::optional<int> Channel::getPitchShift() const
    {
        auto* levels = getLevelsEvent();
        if (levels && levels->fields.pitchShift.has_value())
            return (int)*levels->fields.pitchShift;
        return std::nullopt;
    }
    void Channel::setPitchShift(int cents)
    {
        ensureLevelsEvent();
        auto* levels = getLevelsEvent();
        if (levels) levels->fields.pitchShift = cents;
    }

    std::optional<int> Channel::getFilterCutoff() const
    {
        auto* levels = getLevelsEvent();
        if (levels && levels->fields.filterModX.has_value())
            return (int)*levels->fields.filterModX;
        return std::nullopt;
    }
    void Channel::setFilterCutoff(int value)
    {
        ensureLevelsEvent();
        auto* levels = getLevelsEvent();
        if (levels) levels->fields.filterModX = (uint32_t)value;
    }

    std::optional<int> Channel::getFilterResonance() const
    {
        auto* levels = getLevelsEvent();
        if (levels && levels->fields.filterModY.has_value())
            return (int)*levels->fields.filterModY;
        return std::nullopt;
    }
    void Channel::setFilterResonance(int value)
    {
        ensureLevelsEvent();
        auto* levels = getLevelsEvent();
        if (levels) levels->fields.filterModY = (uint32_t)value;
    }

    std::optional<int> Channel::getFilterType() const
    {
        auto* levels = getLevelsEvent();
        if (levels && levels->fields.filterType.has_value())
            return (int)*levels->fields.filterType;
        return std::nullopt;
    }
    void Channel::setFilterType(int type)
    {
        ensureLevelsEvent();
        auto* levels = getLevelsEvent();
        if (levels) levels->fields.filterType = (uint32_t)type;
    }

    int Channel::getGroupIndex() const
    {
        auto ev = m_tree.firstEvent(EventID::GroupNum);
        if (ev && dynamic_cast<I32Event*>(ev))
            return static_cast<I32Event*>(ev)->value;
        return 0;
    }
    void Channel::setGroupIndex(int group)
    {
        m_tree.removeEvent(EventID::GroupNum);
        m_tree.addEvent(std::make_unique<I32Event>(group));
    }

    std::unique_ptr<Channel> Channel::create(EventTree& tree, const FLVersion& version)
    {
        return std::make_unique<Channel>(tree, version);
    }

    // ---- Pattern ----
    int Pattern::getIID() const
    {
        auto ev = m_tree.firstEvent(EventID::NewPat);
        if (ev && dynamic_cast<U16Event*>(ev))
            return static_cast<U16Event*>(ev)->value;
        return -1;
    }
    juce::String Pattern::getName() const
    {
        auto ev = m_tree.firstEvent(EventID::PatName);
        if (ev) {
            if (auto* a = dynamic_cast<AsciiEvent*>(ev)) return a->value;
            if (auto* u = dynamic_cast<UnicodeEvent*>(ev)) return u->value;
        }
        return {};
    }
    void Pattern::setName(const juce::String& name)
    {
        m_tree.removeEvent(EventID::PatName);
        std::unique_ptr<Event> ev;
        if (m_version >= FLVersion{ 11,5,0,0 })
            ev = std::make_unique<UnicodeEvent>(name);
        else
            ev = std::make_unique<AsciiEvent>(name);
        m_tree.addEvent(std::move(ev));
    }
    juce::Colour Pattern::getColor() const
    {
        auto ev = m_tree.firstEvent(EventID::PatternColor);
        if (ev && dynamic_cast<ColorEvent*>(ev))
            return static_cast<ColorEvent*>(ev)->value;
        return juce::Colours::grey;
    }
    void Pattern::setColor(const juce::Colour& c)
    {
        m_tree.removeEvent(EventID::PatternColor);
        m_tree.addEvent(std::make_unique<ColorEvent>(c));
    }

    std::vector<Note> Pattern::getNotes() const
    {
        auto ev = m_tree.firstEvent(EventID::PatternNotes);
        if (auto* pn = dynamic_cast<PatternNotesEvent*>(ev))
            return pn->notes;
        return {};
    }
    void Pattern::setNotes(const std::vector<Note>& notes)
    {
        m_tree.removeEvent(EventID::PatternNotes);
        auto ev = std::make_unique<PatternNotesEvent>();
        ev->notes = notes;
        m_tree.addEvent(std::move(ev));
    }

    std::vector<Controller> Pattern::getControllers() const
    {
        auto ev = m_tree.firstEvent(EventID::PatternCtrls);
        if (auto* pc = dynamic_cast<PatternCtrlsEvent*>(ev))
            return pc->controllers;
        return {};
    }
    void Pattern::setControllers(const std::vector<Controller>& ctrls)
    {
        m_tree.removeEvent(EventID::PatternCtrls);
        auto ev = std::make_unique<PatternCtrlsEvent>();
        ev->controllers = ctrls;
        m_tree.addEvent(std::move(ev));
    }

    int Pattern::getLength() const
    {
        auto ev = m_tree.firstEvent(EventID::PatternSteps);
        if (ev && dynamic_cast<U32Event*>(ev))
            return static_cast<U32Event*>(ev)->value;
        return 0;
    }

    // ---- Track ----
    TrackInfoEvent* Track::getTrackInfoEvent() const
    {
        auto ev = m_tree.firstEvent(EventID::TrackInfo);
        return dynamic_cast<TrackInfoEvent*>(ev);
    }

    juce::String Track::getName() const
    {
        auto ev = m_tree.firstEvent(EventID::TrackName);
        if (ev) {
            if (auto* a = dynamic_cast<AsciiEvent*>(ev)) return a->value;
            if (auto* u = dynamic_cast<UnicodeEvent*>(ev)) return u->value;
        }
        return {};
    }
    void Track::setName(const juce::String& name)
    {
        m_tree.removeEvent(EventID::TrackName);
        std::unique_ptr<Event> ev;
        if (m_version >= FLVersion{ 11,5,0,0 })
            ev = std::make_unique<UnicodeEvent>(name);
        else
            ev = std::make_unique<AsciiEvent>(name);
        m_tree.addEvent(std::move(ev));
    }

    int Track::getIID() const
    {
        auto* ti = getTrackInfoEvent();
        if (ti) return ti->fields.iid;
        return -1;
    }

    bool Track::isMuted() const
    {
        auto* ti = getTrackInfoEvent();
        if (ti) return !ti->fields.enabled;
        return false;
    }
    void Track::setMuted(bool muted)
    {
        auto* ti = getTrackInfoEvent();
        if (ti) ti->fields.enabled = !muted;
    }

    juce::Colour Track::getColor() const
    {
        auto* ti = getTrackInfoEvent();
        if (ti) return ti->fields.color;
        return juce::Colours::grey;
    }
    void Track::setColor(const juce::Colour& c)
    {
        auto* ti = getTrackInfoEvent();
        if (ti) ti->fields.color = c;
    }

    uint32_t Track::getMotion() const
    {
        auto* ti = getTrackInfoEvent();
        return ti ? ti->fields.motion : 0;
    }
    void Track::setMotion(uint32_t motion)
    {
        auto* ti = getTrackInfoEvent();
        if (ti) ti->fields.motion = motion;
    }
    uint32_t Track::getPress() const
    {
        auto* ti = getTrackInfoEvent();
        return ti ? ti->fields.press : 0;
    }
    void Track::setPress(uint32_t press)
    {
        auto* ti = getTrackInfoEvent();
        if (ti) ti->fields.press = press;
    }
    uint32_t Track::getTriggerSync() const
    {
        auto* ti = getTrackInfoEvent();
        return ti ? ti->fields.triggerSync : 0;
    }
    void Track::setTriggerSync(uint32_t sync)
    {
        auto* ti = getTrackInfoEvent();
        if (ti) ti->fields.triggerSync = sync;
    }
    bool Track::isQueued() const
    {
        auto* ti = getTrackInfoEvent();
        return ti ? ti->fields.queued : false;
    }
    void Track::setQueued(bool queued)
    {
        auto* ti = getTrackInfoEvent();
        if (ti) ti->fields.queued = queued;
    }
    bool Track::isTolerant() const
    {
        auto* ti = getTrackInfoEvent();
        return ti ? ti->fields.tolerant : false;
    }
    void Track::setTolerant(bool tolerant)
    {
        auto* ti = getTrackInfoEvent();
        if (ti) ti->fields.tolerant = tolerant;
    }
    uint32_t Track::getPositionSync() const
    {
        auto* ti = getTrackInfoEvent();
        return ti ? ti->fields.positionSync : 0;
    }
    void Track::setPositionSync(uint32_t sync)
    {
        auto* ti = getTrackInfoEvent();
        if (ti) ti->fields.positionSync = sync;
    }
    bool Track::isGrouped() const
    {
        auto* ti = getTrackInfoEvent();
        return ti ? ti->fields.grouped : false;
    }
    void Track::setGrouped(bool grouped)
    {
        auto* ti = getTrackInfoEvent();
        if (ti) ti->fields.grouped = grouped;
    }
    bool Track::isLocked() const
    {
        auto* ti = getTrackInfoEvent();
        return ti ? ti->fields.locked : false;
    }
    void Track::setLocked(bool locked)
    {
        auto* ti = getTrackInfoEvent();
        if (ti) ti->fields.locked = locked;
    }
    float Track::getHeight() const
    {
        auto* ti = getTrackInfoEvent();
        return ti ? ti->fields.height : 0.0f;
    }
    void Track::setHeight(float height)
    {
        auto* ti = getTrackInfoEvent();
        if (ti) ti->fields.height = height;
    }
    int32_t Track::getLockedHeight() const
    {
        auto* ti = getTrackInfoEvent();
        return ti ? ti->fields.lockedHeight : 0;
    }
    void Track::setLockedHeight(int32_t h)
    {
        auto* ti = getTrackInfoEvent();
        if (ti) ti->fields.lockedHeight = h;
    }
    bool Track::getContentLocked() const
    {
        auto* ti = getTrackInfoEvent();
        return ti ? ti->fields.contentLocked : false;
    }
    void Track::setContentLocked(bool locked)
    {
        auto* ti = getTrackInfoEvent();
        if (ti) ti->fields.contentLocked = locked;
    }

    // ---- Arrangement ----
    int Arrangement::getIID() const
    {
        auto ev = m_tree.firstEvent(EventID::ArrangementNew);
        if (ev && dynamic_cast<U16Event*>(ev))
            return static_cast<U16Event*>(ev)->value;
        return -1;
    }
    juce::String Arrangement::getName() const
    {
        auto ev = m_tree.firstEvent(EventID::ArrangementName);
        if (ev) {
            if (auto* a = dynamic_cast<AsciiEvent*>(ev)) return a->value;
            if (auto* u = dynamic_cast<UnicodeEvent*>(ev)) return u->value;
        }
        return {};
    }
    void Arrangement::setName(const juce::String& name)
    {
        m_tree.removeEvent(EventID::ArrangementName);
        std::unique_ptr<Event> ev;
        if (m_version >= FLVersion{ 11,5,0,0 })
            ev = std::make_unique<UnicodeEvent>(name);
        else
            ev = std::make_unique<AsciiEvent>(name);
        m_tree.addEvent(std::move(ev));
    }

    std::vector<Track> Arrangement::getTracks() const
    {
        std::vector<Track> tracks;
        auto trackInfoEvents = m_tree.getEvents(EventID::TrackInfo);
        for (auto* ev : trackInfoEvents) {
            EventTree sub;
            sub.addEvent(std::unique_ptr<Event>(ev->clone()));
            tracks.emplace_back(sub, m_version);
        }
        return tracks;
    }

    std::vector<PlaylistItem> Arrangement::getPlaylistItems() const
    {
        auto ev = m_tree.firstEvent(EventID::Playlist);
        if (auto* pl = dynamic_cast<PlaylistEvent*>(ev))
            return pl->items;
        return {};
    }
    void Arrangement::setPlaylistItems(const std::vector<PlaylistItem>& items)
    {
        m_tree.removeEvent(EventID::Playlist);
        auto ev = std::make_unique<PlaylistEvent>();
        ev->items = items;
        ev->setItemSize(80);
        m_tree.addEvent(std::move(ev));
    }

    // ---- Mixer (stubs) ----
    // (no implementation needed for flphelper)

    // ---- Project load/save ----
    std::unique_ptr<Project> Project::load(const juce::File& file)
    {
        juce::FileInputStream in(file);
        if (!in.openedOk()) return nullptr;

        char magic[4];
        if (in.read(magic, 4) != 4 || memcmp(magic, "FLhd", 4) != 0)
            return nullptr;
        uint32_t headerSize; in.read(&headerSize, 4);
        int16_t format; in.read(&format, 2);
        uint16_t numChannels; in.read(&numChannels, 2);
        uint16_t ppq; in.read(&ppq, 2);

        if (in.read(magic, 4) != 4 || memcmp(magic, "FLdt", 4) != 0)
            return nullptr;
        uint32_t dataSize; in.read(&dataSize, 4);

        auto project = std::make_unique<Project>();
        project->m_ppq = ppq;
        auto tree = std::make_unique<EventTree>();

        FLVersion version{ 0,0,0,0 };
        while (in.getPosition() < in.getTotalLength())
        {
            uint8_t idByte;
            if (in.read(&idByte, 1) != 1) break;
            EventID id = static_cast<EventID>(idByte);
            auto event = Event::read(in, id, version);
            if (!event) continue;

            if (id == EventID::Version) {
                if (auto* a = dynamic_cast<AsciiEvent*>(event.get())) {
                    auto parts = juce::StringArray::fromTokens(a->value, ".", "");
                    if (parts.size() >= 3) {
                        version.major = parts[0].getIntValue();
                        version.minor = parts[1].getIntValue();
                        version.patch = parts[2].getIntValue();
                        if (parts.size() >= 4) version.build = parts[3].getIntValue();
                    }
                }
            }
            tree->addEvent(std::move(event));
        }
        project->m_version = version;
        project->m_eventTree = std::move(tree);
        return project;
    }

    void Project::save(const juce::File& file) const
    {
        juce::FileOutputStream out(file);
        if (!out.openedOk()) return;

        out.write("FLhd", 4);
        uint32_t headerSize = 6; out.write(&headerSize, 4);
        int16_t format = 0; out.write(&format, 2);
        uint16_t numChannels = getChannelCount(); out.write(&numChannels, 2);
        uint16_t ppq = m_ppq; out.write(&ppq, 2);

        out.write("FLdt", 4);
        uint32_t dataSizePos = out.getPosition();
        uint32_t dummy = 0; out.write(&dummy, 4);

        m_eventTree->writeAll(out);

        uint32_t endPos = out.getPosition();
        uint32_t totalDataSize = endPos - dataSizePos - 4;
        out.setPosition(dataSizePos);
        out.write(&totalDataSize, 4);
        out.setPosition(endPos);
    }

    // ---- Project getters ----
    Project::Metadata Project::getMetadata() const
    {
        Metadata md;
        auto getStr = [&](EventID id) -> juce::String {
            auto ev = m_eventTree->firstEvent(id);
            if (ev) {
                if (auto* a = dynamic_cast<AsciiEvent*>(ev)) return a->value;
                if (auto* u = dynamic_cast<UnicodeEvent*>(ev)) return u->value;
            }
            return {};
            };
        md.title = getStr(EventID::Title);
        md.author = getStr(EventID::Author);
        md.genre = getStr(EventID::Genre);
        md.comments = getStr(EventID::Comment);
        md.webUrl = getStr(EventID::URL);
        auto ev = m_eventTree->firstEvent(EventID::ShowInfo);
        if (ev && dynamic_cast<BoolEvent*>(ev))
            md.showInfoOnStart = static_cast<BoolEvent*>(ev)->value;
        return md;
    }

    Project::UserState Project::getUserState() const
    {
        UserState us;
        auto ev = m_eventTree->firstEvent(EventID::LoopActive);
        if (ev && dynamic_cast<BoolEvent*>(ev))
            us.playbackSong = static_cast<BoolEvent*>(ev)->value;
        ev = m_eventTree->firstEvent(EventID::Shuffle);
        if (ev && dynamic_cast<BoolEvent*>(ev))
            us.shuffle = static_cast<BoolEvent*>(ev)->value;
        ev = m_eventTree->firstEvent(EventID::CurrentPatNum);
        if (ev && dynamic_cast<U16Event*>(ev))
            us.pattern = static_cast<U16Event*>(ev)->value;
        return us;
    }

    double Project::getTempo() const
    {
        auto ev = m_eventTree->firstEvent(EventID::Tempo);
        if (ev && dynamic_cast<U32Event*>(ev))
            return static_cast<U32Event*>(ev)->value / 1000.0;
        return 120.0;
    }
    void Project::setTempo(double bpm)
    {
        m_eventTree->removeEvent(EventID::Tempo);
        m_eventTree->addEvent(std::make_unique<U32Event>((uint32_t)(bpm * 1000)));
    }

    int Project::getChannelCount() const
    {
        auto evs = m_eventTree->getEvents(EventID::NewChan);
        return (int)evs.size();
    }

    int Project::getMainPitch() const
    {
        auto ev = m_eventTree->firstEvent(EventID::MainPitch);
        if (ev && dynamic_cast<I16Event*>(ev))
            return static_cast<I16Event*>(ev)->value;
        return 0;
    }
    void Project::setMainPitch(int cents)
    {
        m_eventTree->removeEvent(EventID::MainPitch);
        m_eventTree->addEvent(std::make_unique<I16Event>((int16_t)cents));
    }

    std::vector<Channel*> Project::getChannels() const
    {
        std::vector<Channel*> channels;
        auto trees = m_eventTree->divide(EventID::NewChan, {
            EventID::IsEnabled, EventID::VolByte, EventID::PanByte, EventID::Zipped,
            EventID::ChanType, EventID::IsLocked, EventID::Color, EventID::PluginFactory,
            EventID::PluginName, EventID::NewPlugin, EventID::PluginParams,
            EventID::ChanParams, EventID::Levels, EventID::Polyphony, EventID::Tracking,
            EventID::LevelAdjusts, EventID::Automation, EventID::EnvelopeLFO,
            EventID::GroupNum, EventID::LayerFlags, EventID::SamplerFlags,
            EventID::FineTune, EventID::RootNote, EventID::CutCutBy, EventID::DelayReso,
            EventID::Reverb, EventID::Swing, EventID::Children,
            EventID::MIDIController, EventID::RemoteController
            });
        for (auto& tree : trees) {
            auto ch = Channel::create(tree, m_version);
            channels.push_back(ch.release());
        }
        return channels;
    }

    std::vector<Pattern> Project::getPatterns() const
    {
        std::vector<Pattern> patterns;
        auto trees = m_eventTree->divide(EventID::NewPat, {
            EventID::NewPat, EventID::PatName, EventID::PatternColor,
            EventID::PatternNotes, EventID::PatternCtrls, EventID::PatternSteps,
            EventID::Pattern157, EventID::Pattern158, EventID::PatternChanIID,
            EventID::Unknown161, EventID::Unknown162, EventID::Unknown163
            });
        for (auto& tree : trees) {
            patterns.emplace_back(tree, m_version);
        }
        return patterns;
    }

    Arrangement Project::getArrangement(int index) const
    {
        auto trees = m_eventTree->divide(EventID::ArrangementNew, {
            EventID::ArrangementNew, EventID::ArrangementName, EventID::Playlist,
            EventID::TrackInfo, EventID::TrackName, EventID::MarkerPosition,
            EventID::MarkerText, EventID::TimeSigNumerator, EventID::TimeSigDenominator
            });
        if (index < (int)trees.size())
            return Arrangement(trees[index], m_version);
        return Arrangement(EventTree(), m_version);
    }

    Mixer Project::getMixer() const
    {
        auto mixerTree = m_eventTree->subtree([](const Event* e) {
            auto id = e->id();
            return id == EventID::MixerBlob || id == EventID::InsertData ||
                id == EventID::InsertRouting || id == EventID::InsertIcon ||
                id == EventID::InsertColor || id == EventID::InsertIn ||
                id == EventID::InsertOut || id == EventID::InsertData ||
                id == EventID::SlotIID || id == EventID::NewPlugin ||
                id == EventID::PluginParams || id == EventID::PluginName ||
                id == EventID::PluginFactory || id == EventID::PluginIcon ||
                id == EventID::Color || id == EventID::IsEnabled;
            });
        return Mixer(mixerTree, m_version);
    }

    std::vector<RemoteControllerEvent*> Project::getAutomationChannels() const
    {
        std::vector<RemoteControllerEvent*> result;
        auto evs = m_eventTree->getEvents(EventID::RemoteController);
        for (auto* ev : evs) {
            if (auto* rce = dynamic_cast<RemoteControllerEvent*>(ev))
                result.push_back(rce);
        }
        return result;
    }

    std::vector<AutomationPoint> Project::getTempoAutomationPoints() const
    {
        std::vector<AutomationPoint> allPoints;
        auto autoEv = m_eventTree->firstEvent(EventID::Automation);
        if (auto* ae = dynamic_cast<AutomationEvent*>(autoEv)) {
            allPoints.insert(allPoints.end(), ae->points.begin(), ae->points.end());
        }
        return allPoints;
    }

    // =============================================================================
    // 9. Channel Sample Path & Tree Accessors
    // =============================================================================
    juce::String Channel::getSamplePath() const
    {
        auto ev = m_tree.firstEvent(EventID::SampleFileName);
        if (ev) {
            if (auto* a = dynamic_cast<AsciiEvent*>(ev)) return a->value;
            if (auto* u = dynamic_cast<UnicodeEvent*>(ev)) return u->value;
        }
        return {};
    }

    void Channel::setSamplePath(const juce::String& path)
    {
        m_tree.removeEvent(EventID::SampleFileName);
        std::unique_ptr<Event> ev;
        if (m_version >= FLVersion{ 11,5,0,0 })
            ev = std::make_unique<UnicodeEvent>(path);
        else
            ev = std::make_unique<AsciiEvent>(path);
        m_tree.addEvent(std::move(ev));
    }

    // =============================================================================
    // 10. Project Metadata & Mutable Tree
    // =============================================================================
    void Project::setMetadata(const Metadata& md)
    {
        auto setTextEvent = [&](EventID id, const juce::String& text) {
            m_eventTree->removeEvent(id);
            std::unique_ptr<Event> ev;
            if (m_version >= FLVersion{ 11,5,0,0 })
                ev = std::make_unique<UnicodeEvent>(text);
            else
                ev = std::make_unique<AsciiEvent>(text);
            m_eventTree->addEvent(std::move(ev));
            };

        setTextEvent(EventID::Title, md.title);
        setTextEvent(EventID::Author, md.author);
        setTextEvent(EventID::Genre, md.genre);
        setTextEvent(EventID::Comment, md.comments);
        setTextEvent(EventID::URL, md.webUrl);
        m_eventTree->removeEvent(EventID::ShowInfo);
        m_eventTree->addEvent(std::make_unique<BoolEvent>(md.showInfoOnStart));
        m_metadata = md;
    }

} // namespace FL