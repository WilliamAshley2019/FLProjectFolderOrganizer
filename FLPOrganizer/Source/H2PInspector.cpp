#include "H2PInspector.h"

namespace H2PInspector
{
static juce::String stripComments (const juce::String& in)
{
    juce::String out;
    int i = 0; const int n = in.length();
    while (i < n)
    {
        if (i + 1 < n && in[i] == '/' && in[i + 1] == '*')
        {
            const int end = in.indexOf (i + 2, "*/");
            if (end < 0) break;                    // unterminated: ignore everything after
            out << "\n"; i = end + 2;
        }
        else if (i + 1 < n && in[i] == '/' && in[i + 1] == '/')
        {
            while (i < n && in[i] != '\r' && in[i] != '\n') ++i;
        }
        else out << in[i++];
    }
    return out;
}

static bool isDigits (const juce::String& s) { return s.isNotEmpty() && s.containsOnly ("0123456789"); }

juce::String pluginName (const juce::uint8* data, size_t size, juce::String& error)
{
    juce::String text; for (size_t i = 0; i < size; ++i) text += (juce::juce_wchar) data[i];
    if (text.startsWith (juce::String::charToString (0xFEFF))) text = text.substring (1);
    text = stripComments (text);

    juce::StringArray names;
    bool haveVers = false, haveEndian = false;
    for (auto line : juce::StringArray::fromLines (text))
    {
        if (line.startsWith ("#AM=")) names.add (line.substring (4));
        else if (line.startsWith ("#Vers=") && isDigits (line.substring (6).trimEnd())) haveVers = true;
        else if (line.startsWith ("#Endian=") && (line.substring (8).trimEnd() == "little" || line.substring (8).trimEnd() == "big")) haveEndian = true;
    }
    if (names.size() != 1) { error = "H2P plugin identity (#AM) is missing or ambiguous."; return {}; }
    auto name = names[0].trim();
    if (name.isEmpty() || name.length() > 128) { error = "Invalid H2P plugin name."; return {}; }
    for (auto c : name) if (! juce::CharacterFunctions::isPrintable (c)) { error = "Invalid H2P plugin name."; return {}; }
    if (! haveVers || ! haveEndian) { error = "Missing or invalid H2P format header."; return {}; }
    return name;
}

juce::String pluginNameFromFile (const juce::File& f, juce::String& error)
{
    juce::FileInputStream in (f);
    if (! in.openedOk()) { error = "Cannot open H2P file"; return {}; }
    juce::HeapBlock<juce::uint8> buf ((size_t) kHeaderBytes);
    const auto n = in.read (buf.get(), kHeaderBytes);
    return pluginName (buf.get(), (size_t) juce::jmax (0, n), error);
}
}
