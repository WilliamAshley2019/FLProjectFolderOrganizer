#include "AutomationToolsWindow.h"

namespace
{
    // Peeks at a channel's automation point count without going through
    // AutomationEditor::applyToChannel (which is built around mutating in
    // place via a callback, not just reading).
    size_t getAutomationPointCount(FL::Channel* ch)
    {
        if (ch == nullptr) return 0;
        auto* ev = ch->getMutableTree().firstEvent(FL::EventID::Automation);
        auto* autoEv = dynamic_cast<FL::AutomationEvent*>(ev);
        return autoEv ? autoEv->points.size() : 0;
    }
}

AutomationToolsComponent::AutomationToolsComponent(PluginProcessor& processor)
    : processorRef(processor)
{
    setSize(650, 550);

    addAndMakeVisible(titleLabel);
    titleLabel.setText("Automation Tools", juce::dontSendNotification);
    titleLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    titleLabel.setFont(juce::Font(juce::FontOptions(20.0f, juce::Font::bold)));

    addAndMakeVisible(channelLabel);
    channelLabel.setText("Channel:", juce::dontSendNotification);
    channelLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);

    addAndMakeVisible(channelSelector);
    channelSelector.onChange = [this]
    {
        int idx = channelSelector.getSelectedItemIndex();
        if (idx < 0 || idx >= channelIidsForSelector.size()) return;
        int iid = channelIidsForSelector[idx];
        processorRef.withProjectLock([&](FL::Project* p) -> int
        {
            if (p == nullptr) return 0;
            for (auto* ch : p->getChannels())
                if (ch->getIID() == iid)
                    pointCountLabel.setText(juce::String((int) getAutomationPointCount(ch)) + " point(s)", juce::dontSendNotification);
            return 0;
        });
    };

    addAndMakeVisible(pointCountLabel);
    pointCountLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);

    addAndMakeVisible(factorLabel);
    factorLabel.setText("Factor / window size / tolerance (used by the button you press):", juce::dontSendNotification);
    factorLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    factorLabel.setFont(juce::Font(juce::FontOptions(12.0f)));

    addAndMakeVisible(factorEditor);
    factorEditor.setText("1.0");
    factorEditor.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xFF2A2A2A));
    factorEditor.setColour(juce::TextEditor::textColourId, juce::Colours::white);
    factorEditor.setInputRestrictions(10, "0123456789.");

    addAndMakeVisible(scaleBtn);
    scaleBtn.setButtonText("Scale Values");
    scaleBtn.onClick = [this] { applyScale(); };

    addAndMakeVisible(invertBtn);
    invertBtn.setButtonText("Invert");
    invertBtn.onClick = [this] { applyInvert(); };

    addAndMakeVisible(smoothBtn);
    smoothBtn.setButtonText("Smooth (window size)");
    smoothBtn.onClick = [this] { applySmooth(); };

    addAndMakeVisible(removeRedundantBtn);
    removeRedundantBtn.setButtonText("Remove Redundant (tolerance)");
    removeRedundantBtn.onClick = [this] { applyRemoveRedundant(); };

    addAndMakeVisible(saveAsBtn);
    saveAsBtn.setButtonText("Save Project As...");
    saveAsBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFFFF5C00));
    saveAsBtn.onClick = [this] { saveAs(); };

    addAndMakeVisible(statusLabel);
    statusLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    statusLabel.setText("Changes are held in memory until you Save Project As.", juce::dontSendNotification);

    addAndMakeVisible(logBox);
    logBox.setMultiLine(true);
    logBox.setReadOnly(true);
    logBox.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xFF111111));
    logBox.setColour(juce::TextEditor::textColourId, juce::Colours::lightgrey);
    logBox.setFont(juce::Font(juce::FontOptions(12.0f)));

    refreshChannelList();
}

AutomationToolsComponent::~AutomationToolsComponent() {}

void AutomationToolsComponent::resized()
{
    auto bounds = getLocalBounds().reduced(15);

    titleLabel.setBounds(bounds.removeFromTop(28));
    bounds.removeFromTop(10);

    auto chanRow = bounds.removeFromTop(26);
    channelLabel.setBounds(chanRow.removeFromLeft(80));
    channelSelector.setBounds(chanRow);
    bounds.removeFromTop(6);

    pointCountLabel.setBounds(bounds.removeFromTop(20));
    bounds.removeFromTop(12);

    factorLabel.setBounds(bounds.removeFromTop(18));
    factorEditor.setBounds(bounds.removeFromTop(26).removeFromLeft(150));
    bounds.removeFromTop(10);

    auto btnRow1 = bounds.removeFromTop(32);
    scaleBtn.setBounds(btnRow1.removeFromLeft(150));
    btnRow1.removeFromLeft(8);
    invertBtn.setBounds(btnRow1.removeFromLeft(100));
    bounds.removeFromTop(6);

    auto btnRow2 = bounds.removeFromTop(32);
    smoothBtn.setBounds(btnRow2.removeFromLeft(180));
    btnRow2.removeFromLeft(8);
    removeRedundantBtn.setBounds(btnRow2.removeFromLeft(220));
    bounds.removeFromTop(12);

    saveAsBtn.setBounds(bounds.removeFromTop(34).removeFromLeft(180));
    bounds.removeFromTop(8);

    statusLabel.setBounds(bounds.removeFromTop(20));
    bounds.removeFromTop(6);

    logBox.setBounds(bounds);
}

void AutomationToolsComponent::refreshChannelList()
{
    channelSelector.clear();
    channelIidsForSelector.clear();

    processorRef.withProjectLock([&](FL::Project* p) -> int
    {
        if (p == nullptr)
        {
            setStatus("No project loaded.");
            return 0;
        }
        int itemId = 1;
        for (auto* ch : p->getChannels())
        {
            size_t points = getAutomationPointCount(ch);
            if (points == 0) continue; // only list channels that actually have automation data
            juce::String label = "IID " + juce::String(ch->getIID()) + ": "
                + (ch->getName().isNotEmpty() ? ch->getName() : juce::String("(unnamed)"))
                + " - " + juce::String((int) points) + " point(s)";
            channelSelector.addItem(label, itemId);
            channelIidsForSelector.add(ch->getIID());
            ++itemId;
        }
        return 0;
    });

    if (channelSelector.getNumItems() == 0)
        channelSelector.setTextWhenNoChoicesAvailable("No channels with automation data found.");
    else
        channelSelector.setSelectedItemIndex(0);
}

void AutomationToolsComponent::setStatus(const juce::String& msg)
{
    statusLabel.setText(msg, juce::dontSendNotification);
    logBox.moveCaretToEnd();
    logBox.insertTextAtCaret("[" + juce::Time::getCurrentTime().toString(true, true) + "] " + msg + "\n");
}

// Shared plumbing for the four operations below: find the selected channel,
// hand its automation points to a modifier, report how many points remain.
template <typename ModifierFn>
static void applyToSelected(PluginProcessor& processorRef, juce::ComboBox& channelSelector,
    juce::Array<int>& channelIidsForSelector, ModifierFn&& modifier,
    std::function<void(const juce::String&)> report)
{
    int idx = channelSelector.getSelectedItemIndex();
    if (idx < 0 || idx >= channelIidsForSelector.size())
    {
        report("Select a channel first.");
        return;
    }
    int iid = channelIidsForSelector[idx];

    processorRef.withProjectLock([&](FL::Project* p) -> int
    {
        if (p == nullptr) { report("No project loaded."); return 0; }
        for (auto* ch : p->getChannels())
        {
            if (ch->getIID() != iid) continue;
            bool applied = FL::AutomationEditor::applyToChannel(*ch, modifier);
            report(applied
                ? ("Applied. Now " + juce::String((int) getAutomationPointCount(ch)) + " point(s).")
                : "This channel has no Automation event to modify.");
            return 0;
        }
        report("Channel not found (project may have been reloaded).");
        return 0;
    });
}

void AutomationToolsComponent::applyScale()
{
    double factor = factorEditor.getText().getDoubleValue();
    if (factor <= 0.0) { setStatus("Enter a positive scale factor first."); return; }
    applyToSelected(processorRef, channelSelector, channelIidsForSelector,
        [factor](std::vector<FL::AutomationPoint>& pts) { FL::AutomationEditor::scalePoints(pts, factor); },
        [this](const juce::String& msg) { setStatus("Scale x" + factorEditor.getText() + ": " + msg); });
}

void AutomationToolsComponent::applyInvert()
{
    applyToSelected(processorRef, channelSelector, channelIidsForSelector,
        [](std::vector<FL::AutomationPoint>& pts) { FL::AutomationEditor::invertPoints(pts); },
        [this](const juce::String& msg) { setStatus("Invert: " + msg); });
}

void AutomationToolsComponent::applySmooth()
{
    int window = factorEditor.getText().getIntValue();
    if (window < 2) { setStatus("Enter a window size of at least 2 first."); return; }
    applyToSelected(processorRef, channelSelector, channelIidsForSelector,
        [window](std::vector<FL::AutomationPoint>& pts) { FL::AutomationEditor::smoothPoints(pts, window); },
        [this, window](const juce::String& msg) { setStatus("Smooth (window " + juce::String(window) + "): " + msg); });
}

void AutomationToolsComponent::applyRemoveRedundant()
{
    double tolerance = factorEditor.getText().getDoubleValue();
    if (tolerance <= 0.0) tolerance = 0.001;
    applyToSelected(processorRef, channelSelector, channelIidsForSelector,
        [tolerance](std::vector<FL::AutomationPoint>& pts) { FL::AutomationEditor::removeRedundantPoints(pts, tolerance); },
        [this, tolerance](const juce::String& msg) { setStatus("Remove redundant (tol " + juce::String(tolerance) + "): " + msg); });
}

void AutomationToolsComponent::saveAs()
{
    saveChooser = std::make_unique<juce::FileChooser>(
        "Save modified project as...",
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("modified.flp"),
        "*.flp");

    constexpr auto flags = juce::FileBrowserComponent::saveMode
                          | juce::FileBrowserComponent::warnAboutOverwriting;

    saveChooser->launchAsync(flags, [this](const juce::FileChooser& chooser)
    {
        auto file = chooser.getResult();
        if (file == juce::File{}) return;

        bool ok = processorRef.saveProjectAs(file);
        setStatus(ok ? ("Saved to " + file.getFullPathName()) : "Save failed.");
    });
}

AutomationToolsWindow::AutomationToolsWindow(PluginProcessor& processor)
    : DialogWindow("Automation Tools", juce::Colour(0xFF181818), true, true)
{
    setUsingNativeTitleBar(true);
    setContentOwned(new AutomationToolsComponent(processor), true);
    centreWithSize(650, 550);
    setResizable(true, true);
    setVisible(true);
}

void AutomationToolsWindow::closeButtonPressed()
{
    setVisible(false);
    delete this;
}
