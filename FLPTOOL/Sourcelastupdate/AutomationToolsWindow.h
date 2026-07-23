#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

// Lets the user pick a channel with automation data and apply
// FL::AutomationEditor operations to it (scale/invert/smooth/remove
// redundant points), then save the result to a new .flp file.
//
// Now includes full curve type support for all 13 automation curve types
// discovered through reverse-engineering FL Studio 2026.
class AutomationToolsComponent : public juce::Component
{
public:
    explicit AutomationToolsComponent(PluginProcessor& processor);
    ~AutomationToolsComponent() override;

    void resized() override;

private:
    void refreshChannelList();
    void refreshPointInfo();
    void applyScale();
    void applyInvert();
    void applySmooth();
    void applyRemoveRedundant();
    void applyCurveType(int curveType);
    void saveAs();
    void setStatus(const juce::String& msg);
    
    // Get curve type name from value
    juce::String getCurveTypeName(int curveType) const;
    // Get all curve types for the dropdown
    std::vector<std::pair<int, juce::String>> getCurveTypeList() const;

    PluginProcessor& processorRef;

    juce::Label titleLabel;
    juce::Label channelLabel;
    juce::ComboBox channelSelector;
    juce::Array<int> channelIidsForSelector;

    juce::Label pointCountLabel;
    juce::Label curveTypeLabel;
    juce::ComboBox curveTypeSelector;

    juce::Label factorLabel;
    juce::TextEditor factorEditor;

    juce::TextButton scaleBtn, invertBtn, smoothBtn, removeRedundantBtn;
    juce::TextButton applyCurveTypeBtn;
    juce::TextButton saveAsBtn;
    juce::Label statusLabel;
    juce::TextEditor logBox;

    std::unique_ptr<juce::FileChooser> saveChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AutomationToolsComponent)
};

class AutomationToolsWindow : public juce::DialogWindow
{
public:
    explicit AutomationToolsWindow(PluginProcessor& processor);
    void closeButtonPressed() override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AutomationToolsWindow)
};