#include "FSTInspector.h"

namespace
{
    // FLhd "format" values for preset files. See the note in FSTInspector.h.
    // FLhd "format" values. Observed/assumed (PyFLP's FileFormat): 0 = project,
    // 0x20 = channel state, 0x40 = mixer insert state; any other non-zero value
    // is treated as a plugin state. Only these two need to be exact; if a real
    // file disagrees, fix the two constants.
    constexpr juce::uint16 kFormatProject = 0;
    constexpr juce::uint16 kFormatChannel = 0x20;
    constexpr juce::uint16 kFormatInsert  = 0x40;

    constexpr size_t kHeaderBytes = 22;   // FLhd(4)+size(4)+fmt(2)+chan(2)+ppq(2)+FLdt(4)+len(4)

    bool le32 (const juce::uint8* p, size_t avail, size_t off, juce::uint32& out)
    {
        if (off + 4 > avail) return false;
        out = (juce::uint32) p[off] | ((juce::uint32) p[off + 1] << 8)
            | ((juce::uint32) p[off + 2] << 16) | ((juce::uint32) p[off + 3] << 24);
        return true;
    }
    bool le64 (const juce::uint8* p, size_t avail, size_t off, juce::uint64& out)
    {
        if (off + 8 > avail) return false;
        out = 0;
        for (int i = 0; i < 8; ++i) out |= (juce::uint64) p[off + i] << (8 * i);
        return true;
    }

    // Walks the wrapper payload's record framing: u32 outer-count (8/10/12),
    // then records of { u32 event, u64 size, payload }. Calls fn(event, ptr, size);
    // fn returns true to stop. Fully bounds-checked.
    template <typename Fn>
    void walkWrapperRecords (const juce::uint8* p, size_t n, Fn fn)
    {
        juce::uint32 first = 0;
        if (! le32 (p, n, 0, first)) return;
        if (first != 8 && first != 10 && first != 12) return;
        size_t cursor = 4;
        while (cursor + 12 <= n)
        {
            juce::uint32 event = 0; juce::uint64 size = 0;
            if (! le32 (p, n, cursor, event) || ! le64 (p, n, cursor + 4, size)) return;
            cursor += 12;
            if (size > (juce::uint64) (n - cursor)) return;
            if (fn (event, p + cursor, (size_t) size)) return;
            cursor += (size_t) size;
        }
    }

    juce::uint32 hostedPluginId (const juce::uint8* p, size_t n)
    {
        juce::uint32 id = 0;
        walkWrapperRecords (p, n, [&] (juce::uint32 ev, const juce::uint8* d, size_t sz)
        {
            if (ev != 51) return false;
            if (sz == 4) le32 (d, 4, 0, id);
            return true;
        });
        return id;
    }

    juce::String hostedPluginName (const juce::uint8* p, size_t n)
    {
        juce::String name;
        walkWrapperRecords (p, n, [&] (juce::uint32 ev, const juce::uint8* d, size_t sz)
        {
            if (ev != 54 || sz == 0 || sz > 512) return false;
            // NUL-terminated ASCII. Reject control characters.
            size_t len = sz;
            while (len > 0 && d[len - 1] == 0) --len;
            for (size_t i = 0; i < len; ++i)
                if (d[i] < 0x20 || d[i] == 0x7F) return true;     // reject, stop
            name = juce::String::fromUTF8 (reinterpret_cast<const char*> (d), (int) len);
            return true;
        });
        return name;
    }

    // Length of an event's payload as Event::read will consume it, or -1 if the
    // varint is malformed/truncated. Mirrors the framing in FL::Event::read
    // (including the id-172 3-byte exception) WITHOUT allocating.
    juce::int64 peekPayloadSize (const juce::uint8* d, size_t avail, size_t pos, juce::uint8 id, size_t& headerLen)
    {
        headerLen = 0;
        if (id < 64)  return 1;
        if (id < 128) return 2;
        if (id < 192)
        {
            // Event 172 has been seen with a 3-byte payload; recognise it by its
            // observed framing rather than by version.
            if (id == 172 && pos + 4 <= avail && d[pos] == 0x01 && d[pos + 1] == 0x01
                && d[pos + 2] == 0x00 && d[pos + 3] == 0xC0) return 3;
            return 4;
        }
        juce::uint64 v = 0; int shift = 0;
        while (true)
        {
            if (pos + headerLen >= avail || shift > 28) return -1;
            juce::uint8 b = d[pos + headerLen++];
            v |= (juce::uint64) (b & 0x7F) << shift;
            shift += 7;
            if (! (b & 0x80)) break;
        }
        return (juce::int64) v;
    }

    juce::String textOf (const FL::Event* ev)
    {
        if (auto* a = dynamic_cast<const FL::AsciiEvent*> (ev))   return a->value;
        if (auto* u = dynamic_cast<const FL::UnicodeEvent*> (ev)) return u->value;
        return {};
    }
}

juce::String FSTInspector::Metadata::vst2IdString() const
{
    // FL stores the ID little-endian; the 4-char code reads most-significant byte first.
    if (vst2Id == 0) return {};
    char c[5] = { (char) ((vst2Id >> 24) & 0xFF), (char) ((vst2Id >> 16) & 0xFF),
                  (char) ((vst2Id >> 8) & 0xFF), (char) (vst2Id & 0xFF), 0 };
    for (int i = 0; i < 4; ++i)
        if (c[i] < 0x20 || c[i] > 0x7E) return {};
    return juce::String (c);
}

bool FSTInspector::isWrapperName (const juce::String& s)
{
    return s.equalsIgnoreCase ("Fruity Wrapper") || s.equalsIgnoreCase ("Fruit Wrapper");
}

FSTInspector::Metadata FSTInspector::inspectBytes (const juce::uint8* data, size_t size, juce::int64 fileSize)
{
    Metadata md;
    if (size < kHeaderBytes || std::memcmp (data, "FLhd", 4) != 0)
    { md.reason = "Missing FLhd header"; return md; }

    juce::uint32 headerSize = 0;
    le32 (data, size, 4, headerSize);
    if (headerSize != 6) { md.reason = "Unsupported header size"; return md; }

    const juce::uint16 format = (juce::uint16) (data[8] | (data[9] << 8));
    if (format == kFormatProject)
    { md.reason = "This is a project (FLhd format 0), not a preset"; return md; }

    if (std::memcmp (data + 14, "FLdt", 4) != 0) { md.reason = "Missing FLdt chunk"; return md; }

    juce::uint32 dataSize = 0;
    le32 (data, size, 18, dataSize);
    if (fileSize > 0 && (juce::int64) dataSize + (juce::int64) kHeaderBytes != fileSize)
    { md.reason = "Declared FLdt length does not match file size"; return md; }

    if (format == kFormatInsert)
    {
        md.pluginName = "Mixer preset";
        md.fstType    = "mixer";
        md.pluginKind = "effect";
        return md;
    }
    md.fstType = (format == kFormatChannel) ? "channel" : "plugin";

    const size_t end = juce::jmin (size, kHeaderBytes + (size_t) juce::jmin<juce::uint32> (dataSize, (juce::uint32) kMaxPrefixBytes));
    size_t pos = kHeaderBytes;
    FL::FLVersion version { 0, 0, 0, 0 };
    bool haveName = false;

    auto finish = [&]() -> Metadata
    {
        if (md.pluginName.isEmpty())
            md.reason = "No outer plugin name in metadata prefix";
        return md;
    };

    try
    {
        while (pos < end)
        {
            const juce::uint8 idByte = data[pos];
            size_t hdr = 0;
            const juce::int64 payload = peekPayloadSize (data, end, pos + 1, idByte, hdr);
            if (payload < 0 || (juce::uint64) payload > (juce::uint64) (end - (pos + 1 + hdr)))
            {
                // PluginParams may legitimately run past the prefix once we have its start.
                if (idByte == (juce::uint8) FL::EventID::PluginParams && payload >= 0
                    && (juce::uint64) payload <= (juce::uint64) dataSize)
                { /* handled below with a truncated view */ }
                else { md.reason = "Event length exceeds metadata prefix"; return md; }
            }
            const juce::uint8* payloadPtr = data + pos + 1 + hdr;
            const size_t avail = (size_t) juce::jmin<juce::int64> (payload, (juce::int64) (size - (pos + 1 + hdr)));
            const size_t eventEnd = pos + 1 + hdr + (size_t) payload;
            const auto id = (FL::EventID) idByte;

            if (id == FL::EventID::PluginParams)
            {
                if (haveName && ! isWrapperName (md.pluginName)) return md;        // identity settled
                if (! haveName) { md.reason = "Reached opaque plugin state before the outer plugin name"; return md; }
                md.vst2Id = hostedPluginId (payloadPtr, avail);
                md.hostedPluginName = hostedPluginName (payloadPtr, avail);
                if (md.hostedPluginName.isEmpty())
                    md.reason = "Hosted plugin name unavailable; the wrapper label is not a plugin identity";
                return md;
            }

            if (id == FL::EventID::Version || id == FL::EventID::PluginFactory || id == FL::EventID::PluginName)
            {
                juce::MemoryInputStream in (data, eventEnd, false);
                in.setPosition ((juce::int64) pos + 1);
                auto ev = FL::Event::read (in, id, version);
                auto text = ev ? textOf (ev.get()) : juce::String();

                if (id == FL::EventID::Version)
                {
                    md.flVersion = text;
                    auto parts = juce::StringArray::fromTokens (text, ".", "");
                    if (parts.size() >= 2)
                    {
                        version = { parts[0].getIntValue(), parts[1].getIntValue(),
                                    parts.size() > 2 ? parts[2].getIntValue() : 0,
                                    parts.size() > 3 ? parts[3].getIntValue() : 0 };
                        md.encoding = (version >= FL::FLVersion { 11, 5, 0, 0 }) ? "utf-16-le" : "ascii";
                    }
                }
                else if (id == FL::EventID::PluginFactory)
                {
                    if (haveName) return md;                      // never replace the first outer identity
                    if (text.isEmpty() && md.fstType == "channel") { md.pluginName = "Channel preset"; haveName = true; }
                    else
                    {
                        if (text.isEmpty() || text.length() > 256) { md.reason = "Invalid outer plugin name"; return md; }
                        for (auto ch : text)
                            if (ch < 32 || ch > 126) { md.reason = "Plugin name contains unsupported characters"; return md; }
                        md.pluginName = text; md.pluginFactory = text; haveName = true;
                    }
                }
                else if (haveName && text.isNotEmpty())           // PluginName: the user-visible channel label
                {
                    bool printable = true;
                    for (auto ch : text) if (! juce::CharacterFunctions::isPrintable (ch)) printable = false;
                    if (printable) md.displayName = text;
                }
            }
            else if (id == FL::EventID::NewPlugin && payload >= 18)
            {
                // Wrapper flags: uint16 at payload offset 16; Generator = 1 << 4.
                const juce::uint16 flags = (juce::uint16) (payloadPtr[16] | (payloadPtr[17] << 8));
                md.pluginKind = (flags & 0x10) ? "generator" : "effect";
            }
            pos = eventEnd;
        }
    }
    catch (const std::exception& e)
    {
        md.reason = juce::String ("Parse exception: ") + e.what();
        return md;
    }
    return finish();
}

FSTInspector::Metadata FSTInspector::inspectFile (const juce::File& f)
{
    Metadata m;
    juce::FileInputStream in (f);
    if (! in.openedOk()) { m.reason = "Cannot open file"; return m; }
    const auto total = in.getTotalLength();
    if (total <= 0) { m.reason = "Empty file"; return m; }
    const int n = (int) juce::jmin<juce::int64> (total, kMaxPrefixBytes);
    juce::HeapBlock<juce::uint8> buf ((size_t) n);
    if (in.read (buf.get(), n) != n) { m.reason = "Short read"; return m; }
    return inspectBytes (buf.get(), (size_t) n, total);
}
