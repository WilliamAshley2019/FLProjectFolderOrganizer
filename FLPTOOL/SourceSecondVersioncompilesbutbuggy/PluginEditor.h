#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

class PluginEditor : public juce::AudioProcessorEditor
{
public:
    PluginEditor(PluginProcessor&);
    ~PluginEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    PluginProcessor& processorRef;

    // ADD THIS: Keeps the FileChooser alive during the async operation
    std::unique_ptr<juce::FileChooser> fileChooser;

    // UI Components
    juce::TextButton loadButton{ "Load FLP Project..." };
    juce::Label titleLabel;
    juce::ComboBox toolSelector;
    juce::TextEditor outputDisplay;
    juce::Label statusLabel;

    // Enum for tool selection
    enum ToolID { Stats = 0, Plugins, Samples, Arrangement };

    // Callbacks
    void loadButtonClicked();
    void toolSelectorChanged();
    void refreshOutputDisplay();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginEditor)
};