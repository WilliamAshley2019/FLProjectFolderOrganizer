#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

class PluginEditor  : public juce::AudioProcessorEditor
{
public:
    PluginEditor (PluginProcessor&);
    ~PluginEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    PluginProcessor& processorRef;

    // UI Components
    juce::TextButton loadButton { "Load FLP Project..." };
    juce::TextButton exportMidiButton { "Export Patterns to MIDI..." };
    juce::TextButton exportJsonButton { "Export Full Data as JSON..." };
    juce::TextButton compareButton { "Compare with..." };
    juce::TextButton batchProcessButton { "Batch Process..." };
    juce::TextButton automationToolsButton { "Automation Tools..." };
    juce::Label titleLabel;
    juce::ComboBox toolSelector;
    juce::TextEditor outputDisplay;
    juce::Label statusLabel;

    // Must be kept alive for the duration of the async dialog - see loadButtonClicked()/exportMidiButtonClicked()/compareButtonClicked()
    std::unique_ptr<juce::FileChooser> fileChooser;
    std::unique_ptr<juce::FileChooser> midiSaveChooser;
    std::unique_ptr<juce::FileChooser> jsonSaveChooser;
    std::unique_ptr<juce::FileChooser> compareChooser;

    // Enum for tool selection
    enum ToolID { Stats = 0, Plugins, Samples, Arrangement, Cleanup, Comparison };

    // The last comparison result, shown when Comparison is selected. Cached
    // rather than recomputed on tool-switch since it's the result of a
    // user-triggered action (picking a second file), not a property of the
    // currently loaded project alone.
    juce::String lastComparisonReport { "Use \"Compare with...\" to diff the loaded project against another .flp file." };

    // Callbacks
    void loadButtonClicked();
    void exportMidiButtonClicked();
    void exportJsonButtonClicked();
    void compareButtonClicked();
    void batchProcessButtonClicked();
    void automationToolsButtonClicked();
    void toolSelectorChanged();
    void refreshOutputDisplay();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginEditor)
};