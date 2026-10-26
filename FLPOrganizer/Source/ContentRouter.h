#pragma once
#include <JuceHeader.h>
#include "ConfigManager.h"
#include "FileTransfer.h"

// Decides which configured destinations can receive a package, and splits mixed
// preset packages into one group per plugin so each lands in its own folder.
namespace ContentRouter
{
    using RoutingConfig::Config;
    using RoutingConfig::Destination;
    using FileTransfer::Package;

    // True when a preset package has no usable plugin identity (user must choose).
    bool needsPluginName (const Package&);

    // Separate FSTs by outer plugin, keeping package-relative paths. Project-style
    // packages (containing an .flp) are returned unsplit.
    std::vector<Package> fstPackageGroups (const Package&, bool split = true);

    juce::String pluginDestinationId (const juce::String& pluginName);
    bool matchesPlugin (const Destination&, const juce::String& pluginName,
                        const juce::String& pluginKind = {}, const juce::String& fstType = {});

    // Destinations that accept `relPath`; when automatic, only auto-enabled,
    // extension-bearing ones that agree with the identity.
    std::vector<const Destination*> compatible (const Config&, const juce::String& relPath, bool automatic = false,
                                                const juce::String& pluginName = {}, const juce::String& pluginKind = {},
                                                const juce::String& fstType = {});

    std::vector<const Destination*> packageChoices (const Config&, const Package&, bool automatic = true);

    // Every recognised file in the package must be accepted by `dest`; unknown
    // companion assets (readmes, images) travel along. `bad` lists offenders.
    bool packageMatches (const Config&, const juce::StringArray& paths, const Destination& dest, juce::StringArray* bad = nullptr);
}
