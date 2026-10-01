#include "PluginEditor.h"

ARAFLBridgeEditor::ARAFLBridgeEditor (ARAFLBridgeProcessor& p) : AudioProcessorEditor (&p), p_ (p), progress_ (progressValue_)
{
    for (auto* b : { &loadInner_, &loadSidecar_, &capture_, &loadFile_, &showUi_ }) addAndMakeVisible (*b);
    addAndMakeVisible (sequenceBox_); addAndMakeVisible (warning_);
    warning_.setColour (juce::Label::textColourId, juce::Colours::orange);
    addAndMakeVisible (status_); addAndMakeVisible (stage_); addAndMakeVisible (progress_);
    status_.setJustificationType (juce::Justification::topLeft);

    loadInner_.onClick = [this]
    {
        chooser_ = std::make_unique<juce::FileChooser> ("Pick an ARA-capable VST3", juce::File(), "*.vst3");
        chooser_->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectDirectories,
                               [this] (const juce::FileChooser& c) { if (c.getResult().exists()) p_.loadInnerPlugin (c.getResult()); });
    };
    loadSidecar_.onClick = [this]
    {
        chooser_ = std::make_unique<juce::FileChooser> ("Pick an FLP sidecar", juce::File(), "*.arabridge.json");
        chooser_->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                               [this] (const juce::FileChooser& c) { if (c.getResult().existsAsFile()) p_.loadSidecar (c.getResult()); });
    };
    sequenceBox_.onChange = [this] { if (! updatingBox_) p_.setSequence (sequenceBox_.getSelectedId() - 1); };
    capture_.onClick = [this] { p_.beginCapture(); };
    loadFile_.onClick = [this]
    {
        chooser_ = std::make_unique<juce::FileChooser> ("Pick audio", juce::File(), "*.wav;*.aif;*.aiff;*.flac");
        chooser_->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                               [this] (const juce::FileChooser& c) { if (c.getResult().existsAsFile()) p_.loadAudioFile (c.getResult(), true); });
    };
    showUi_.onClick = [this] { showInnerUi(); };

    p_.addChangeListener (this);
    setSize (480, 400);
    refresh();
}

ARAFLBridgeEditor::~ARAFLBridgeEditor() { p_.removeChangeListener (this); window_.reset(); }

void ARAFLBridgeEditor::showInnerUi()
{
    if (window_ == nullptr)
        if (auto* e = p_.createInnerEditor()) window_ = std::make_unique<InnerWindow> (e);
    if (window_) { window_->setVisible (true); window_->toFront (true); }
}

void ARAFLBridgeEditor::refresh()
{
    using S = ARAFLBridgeProcessor::Stage;
    const auto s = p_.getStage();
    stage_.setText (juce::String (s == S::NoInner ? "No inner plug-in" : s == S::InnerReady ? "Ready" : s == S::Capturing ? "Capturing" : "Bridged")
                    + "   |   FL tempo " + juce::String (p_.getTempo(), 2) + " BPM", juce::dontSendNotification);
    status_.setText (p_.getStatus(), juce::dontSendNotification);
    progressValue_ = p_.getAnalysisProgress();
    if (p_.hasSidecar() && sequenceBox_.getNumItems() != (int) p_.getSidecar().sequences.size())
    {
        updatingBox_ = true; sequenceBox_.clear();
        for (const auto& q : p_.getSidecar().sequences) sequenceBox_.addItem (juce::String (q.index + 1) + ": " + q.name, q.index + 1);
        sequenceBox_.setSelectedId (p_.getSequence() + 1, juce::dontSendNotification); updatingBox_ = false;
    }
    warning_.setText (p_.getWarning(), juce::dontSendNotification);
    loadSidecar_.setEnabled (true);
    capture_.setEnabled (s != S::NoInner); loadFile_.setEnabled (s != S::NoInner); showUi_.setEnabled (p_.hasInner());
}

void ARAFLBridgeEditor::paint (juce::Graphics& g) { g.fillAll (juce::Colour (0xff15171c)); g.setColour (juce::Colours::white); g.setFont (16.0f);
                                                   g.drawText ("ARA FL Bridge  (AlphaAudio experiment)", 12, 6, getWidth() - 24, 24, juce::Justification::left); }

void ARAFLBridgeEditor::resized()
{
    auto r = getLocalBounds().reduced (12); r.removeFromTop (30);
    loadInner_.setBounds (r.removeFromTop (30)); r.removeFromTop (6);
    loadSidecar_.setBounds (r.removeFromTop (30)); r.removeFromTop (6);
    sequenceBox_.setBounds (r.removeFromTop (26)); r.removeFromTop (6);
    capture_.setBounds (r.removeFromTop (30)); r.removeFromTop (6);
    loadFile_.setBounds (r.removeFromTop (30)); r.removeFromTop (6);
    showUi_.setBounds (r.removeFromTop (30)); r.removeFromTop (10);
    warning_.setBounds (r.removeFromTop (34));
    stage_.setBounds (r.removeFromTop (22));
    progress_.setBounds (r.removeFromTop (18)); r.removeFromTop (6);
    status_.setBounds (r);
}
