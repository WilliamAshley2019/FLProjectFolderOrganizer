#pragma once
#include "PluginProcessor.h"

class ARAFLBridgeEditor : public juce::AudioProcessorEditor, private juce::ChangeListener
{
public:
    explicit ARAFLBridgeEditor (ARAFLBridgeProcessor&);
    ~ARAFLBridgeEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { refresh(); }
    void refresh();
    void showInnerUi();

    struct InnerWindow : juce::DocumentWindow
    {
        InnerWindow (juce::AudioProcessorEditor* e) : DocumentWindow ("Inner ARA plug-in", juce::Colours::black, closeButton)
        { setUsingNativeTitleBar (true); setContentOwned (e, true); setResizable (true, false); centreWithSize (getWidth(), getHeight()); setVisible (true); }
        void closeButtonPressed() override { setVisible (false); }
    };

    ARAFLBridgeProcessor& p_;
    juce::TextButton loadInner_ { "1. Load inner ARA VST3..." }, loadSidecar_ { "2a. Load FLP sidecar..." },
                     capture_ { "2b. Arm capture" }, loadFile_ { "2c. Import WAV at playhead..." }, showUi_ { "3. Show inner UI" };
    juce::ComboBox sequenceBox_;
    juce::Label warning_;
    juce::Label status_, stage_;
    juce::ProgressBar progress_;
    double progressValue_ = 0.0;
    bool updatingBox_ = false;
    std::unique_ptr<juce::FileChooser> chooser_;
    std::unique_ptr<InnerWindow> window_;
};
