#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

// Lets the user pick a channel with automation data and apply
// FL::AutomationEditor operations to it (scale/invert/smooth/remove
// redundant points), then save the result to a new .flp file.
//
// Operates directly on the PluginProcessor's live loaded project via
// withProjectLock() - changes are held in memory until "Save Project As..."
// is clicked, matching how AutomationEditor::applyToChannel mutates the
// EventTree in place rather than returning a copy.
class AutomationToolsComponent : public juce::Component
{
public:
    explicit AutomationToolsComponent(PluginProcessor& processor);
    ~AutomationToolsComponent() override;

    void resized() override;

private:
    void refreshChannelList();
    void applyScale();
    void applyInvert();
    void applySmooth();
    void applyRemoveRedundant();
    void saveAs();
    void setStatus(const juce::String& msg);

    PluginProcessor& processorRef;

    juce::Label titleLabel;
    juce::Label channelLabel;
    juce::ComboBox channelSelector; // indexed by channel IID, stored in itemData via getSelectedId()-1 lookup
    juce::Array<int> channelIidsForSelector; // parallel to combo box item order

    juce::Label pointCountLabel;

    juce::Label factorLabel;
    juce::TextEditor factorEditor; // scale factor / smoothing window size / tolerance, reused per-operation

    juce::TextButton scaleBtn, invertBtn, smoothBtn, removeRedundantBtn;
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
