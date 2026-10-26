#include "FXPInspector.h"
#include "PluginDestinations.h"

namespace
{
    juce::uint32 be32 (const juce::uint8* p, size_t off)
    { return ((juce::uint32) p[off] << 24) | ((juce::uint32) p[off + 1] << 16) | ((juce::uint32) p[off + 2] << 8) | p[off + 3]; }
    juce::String latin1 (const juce::uint8* p, size_t n) { juce::String s; for (size_t i = 0; i < n; ++i) s += (juce::juce_wchar) p[i]; return s; }
}

juce::uint32 FXPInspector::Metadata::idValue() const
{
    if (pluginId.length() != 4) return 0;
    return ((juce::uint32) (juce::uint8) pluginId[0] << 24) | ((juce::uint32) (juce::uint8) pluginId[1] << 16)
         | ((juce::uint32) (juce::uint8) pluginId[2] << 8) | (juce::uint32) (juce::uint8) pluginId[3];
}

FXPInspector::Metadata FXPInspector::inspectBytes (const juce::uint8* d, size_t n, juce::int64 fileSizeIn)
{
    Metadata m;
    const juce::int64 size = fileSizeIn > 0 ? fileSizeIn : (juce::int64) n;
    if (n < 56 || size < 56 || std::memcmp (d, "CcnK", 4) != 0)
    { m.reason = "Missing or truncated FXP header"; return m; }

    const bool isChunk = std::memcmp (d + 8, "FPCh", 4) == 0;
    const bool isParams = std::memcmp (d + 8, "FxCk", 4) == 0;
    if (! isChunk && ! isParams) { m.reason = "Not a single-program FXP format"; return m; }

    const auto id4 = latin1 (d + 16, 4);
    const auto* known = PluginDestinations::findKnownFxp (id4);
    const juce::int64 declared = be32 (d, 4);

    // Standard FXP: declared = size - 8. Observed deviations:
    //  * a verified-plugin file whose declared length counts its own size field (size - 4);
    //  * a chunk file with declared == 0 whose ID has a registered alias.
    const bool omittedSize = declared == 0 && isChunk && PluginDestinations::aliasFor (id4).isNotEmpty();
    if (declared != size - 8 && ! (known != nullptr && declared == size - 4) && ! omittedSize)
    { m.reason = "FXP declared length does not match file size"; return m; }

    if (be32 (d, 12) != 1) { m.reason = "Unsupported FXP format version"; return m; }

    if (isChunk)
    {
        if (n < 60 || size < 60 || (juce::int64) be32 (d, 56) != size - 60)
        { m.reason = "FXP chunk length does not match file size"; return m; }
    }
    else if (56 + 4 * (juce::int64) be32 (d, 24) != size)
    { m.reason = "FXP parameter count does not match file size"; return m; }

    size_t len = 0; while (len < 28 && d[28 + len] != 0) ++len;
    m.presetName = latin1 (d + 28, len);
    m.pluginId = id4;
    if (known != nullptr) { m.pluginName = known->name; m.pluginKind = known->kind; }
    else m.reason = kUnrecognised;
    return m;
}

FXPInspector::Metadata FXPInspector::inspectFile (const juce::File& f)
{
    Metadata m;
    juce::FileInputStream in (f);
    if (! in.openedOk()) { m.reason = "Cannot read FXP file"; return m; }
    juce::uint8 buf[kHeaderBytes] {};
    const auto got = in.read (buf, kHeaderBytes);
    return inspectBytes (buf, (size_t) juce::jmax (0, got), in.getTotalLength());
}
