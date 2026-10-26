#pragma once
#include <JuceHeader.h>
#include <map>

// Reads FL Studio script packages as *data*. Nothing here imports, evaluates or runs
// script code: the source is scanned for import statements only, and imports that
// resolve to files supplied in the same package are followed so the package's script
// type (Piano Roll / Edison audio / MIDI hardware) can be decided from what it uses.
//
// This is a line-based scanner, not a full Python parser: it understands comments,
// string literals (including triple-quoted), line continuations, parenthesised
// multi-line imports, ';' separators and imports nested in blocks, but it does not
// report Python syntax errors. A script it cannot make sense of is reported as
// "Cannot parse script imports" only when it is oversized or not valid text.
namespace ScriptInspector
{
    constexpr int kMaxScriptBytes = 512 * 1024;

    struct Import { juce::String module; int level = 0; };   // level = number of leading dots

    struct ScriptInfo
    {
        std::vector<Import> imports;
        std::set<juce::String> kinds;      // "piano" and/or "edison"
        juce::String problem;
    };

    // `size` may be kMaxScriptBytes + 1 to signal "too large" without reading more.
    ScriptInfo inspectScript (const void* data, size_t size);

    struct Classification
    {
        juce::String kind;                 // "piano", "edison", "hardware" or "" when undecided
        juce::StringArray notes;
    };

    // Keys are package-relative, '/'-separated paths.
    Classification classifyScripts (const std::map<juce::String, ScriptInfo>& files);

    bool isScriptFile (const juce::String& relPath);   // .py or .pyscript
}
