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
        return autoEv ? autoEv->records.size() : 0;
    }

    // Get the curve type of the first automation segment
    int getCurveType(FL::Channel* ch)
    {
        if (ch == nullptr) return 0;
        auto* ev = ch->getMutableTree().firstEvent(FL::EventID::Automation);
        auto* autoEv = dynamic_cast<FL::AutomationEvent*>(ev);
        if (autoEv && autoEv->records.size() > 1)
            return autoEv->records[1].curveType; // First interior point's curve type
        return 0;
    }

    // Check if a channel has automation data
    bool hasAutomationData(FL::Channel* ch)
    {
        if (ch == nullptr) return false;
        auto* ev = ch->getMutableTree().firstEvent(FL::EventID::Automation);
        auto* autoEv = dynamic_cast<FL::AutomationEvent*>(ev);
        return autoEv && !autoEv->records.empty();
    }
}

// Shared plumbing for operations.
// Declared/defined BEFORE the methods that call it, otherwise the calls below
// produce "applyToSelected: identifier not found".
template <typename ModifierFn>
static void applyToSelected(FLProjectOrganizerProcessor& processorRef, juce::ComboBox& channelSelector,
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
                    ? ("Applied. Now " + juce::String((int)getAutomationPointCount(ch)) + " point(s).")
                    : "This channel has no Automation event to modify.");
                return 0;
            }
            report("Channel not found (project may have been reloaded).");
            return 0;
        });
}

AutomationToolsComponent::AutomationToolsComponent(FLProjectOrganizerProcessor& processor)
    : processorRef(processor)
{
    setSize(700, 760);

    addAndMakeVisible(curveView);

    addAndMakeVisible(titleLabel);
    titleLabel.setText("Automation Tools", juce::dontSendNotification);
    titleLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    titleLabel.setFont(juce::Font(juce::FontOptions(20.0f, juce::Font::bold)));

    addAndMakeVisible(channelLabel);
    channelLabel.setText("Channel:", juce::dontSendNotification);
    channelLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);

    addAndMakeVisible(channelSelector);
    channelSelector.onChange = [this] { refreshPointInfo(); };

    addAndMakeVisible(pointCountLabel);
    pointCountLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);

    addAndMakeVisible(curveTypeLabel);
    curveTypeLabel.setText("Curve Type:", juce::dontSendNotification);
    curveTypeLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);

    // The dropdown only chooses; "Apply Curve Type" below commits it.
    // (It used to modify the project on every selection change, so merely
    // browsing the list rewrote the automation.)
    addAndMakeVisible(curveTypeSelector);

    // Populate curve types
    auto curveTypes = getCurveTypeList();
    for (const auto& pair : curveTypes)
        curveTypeSelector.addItem(pair.second, pair.first + 1);

    addAndMakeVisible(factorLabel);
    factorLabel.setText("Factor / window size / subdivisions:", juce::dontSendNotification);
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

    addAndMakeVisible(applyCurveTypeBtn);
    applyCurveTypeBtn.setButtonText("Apply Curve Type");
    applyCurveTypeBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF44AAFF));
    applyCurveTypeBtn.onClick = [this]
        {
            auto curveTypes = getCurveTypeList();
            int idx = curveTypeSelector.getSelectedItemIndex();
            if (idx >= 0 && idx < (int)curveTypes.size())
                applyCurveType(curveTypes[idx].first);
        };

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

    auto infoRow = bounds.removeFromTop(22);
    pointCountLabel.setBounds(infoRow.removeFromLeft(200));
    curveTypeLabel.setBounds(infoRow.removeFromLeft(100));
    curveTypeSelector.setBounds(infoRow);
    bounds.removeFromTop(10);

    curveView.setBounds(bounds.removeFromTop(160));
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
    bounds.removeFromTop(6);

    auto btnRow3 = bounds.removeFromTop(32);
    applyCurveTypeBtn.setBounds(btnRow3.removeFromLeft(180));
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
                if (!hasAutomationData(ch)) continue;

                size_t points = getAutomationPointCount(ch);
                int curveType = getCurveType(ch);
                juce::String label = "IID " + juce::String(ch->getIID()) + ": "
                    + (ch->getName().isNotEmpty() ? ch->getName() : juce::String("(unnamed)"))
                    + " - " + juce::String((int)points) + " point(s)"
                    + " - " + getCurveTypeName(curveType);
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

    refreshPointInfo();
}

void AutomationToolsComponent::refreshPointInfo()
{
    int idx = channelSelector.getSelectedItemIndex();
    if (idx < 0 || idx >= channelIidsForSelector.size()) { curveView.clear(); return; }
    int iid = channelIidsForSelector[idx];

    processorRef.withProjectLock([&](FL::Project* p) -> int
        {
            if (p == nullptr) return 0;
            for (auto* ch : p->getChannels())
            {
                if (ch->getIID() != iid) continue;
                pointCountLabel.setText(juce::String((int)getAutomationPointCount(ch)) + " point(s)", juce::dontSendNotification);

                int curveType = getCurveType(ch);
                curveTypeSelector.setSelectedId(curveType + 1, juce::dontSendNotification);
                return 0;
            }
            return 0;
        });

    refreshCurveView();
}

void AutomationToolsComponent::refreshCurveView()
{
    int idx = channelSelector.getSelectedItemIndex();
    if (idx < 0 || idx >= channelIidsForSelector.size()) { curveView.clear(); return; }
    int iid = channelIidsForSelector[idx];

    processorRef.withProjectLock([&](FL::Project* p) -> int
        {
            if (p == nullptr) { curveView.clear(); return 0; }
            for (auto* ch : p->getChannels())
            {
                if (ch->getIID() != iid) continue;
                auto* ev = ch->getMutableTree().firstEvent(FL::EventID::Automation);
                if (auto* autoEv = dynamic_cast<FL::AutomationEvent*>(ev))
                    curveView.setPoints(autoEv->records);
                else
                    curveView.clear();

                // Cross-reference the Playlist: a clip can be drawn longer
                // than the automation data it contains (FL just holds the
                // last value flat for the remainder), and that length
                // lives in FL::PlaylistItem::length, not in the automation
                // event itself. itemIndex < patternBase means itemIndex IS
                // a channel IID directly (audio/automation clip, not a
                // pattern) - see the comment on this same check elsewhere
                // in flphelper.cpp. Only checks arrangement 0 for now.
                double clipEndInBeats = 0.0;
                int ppq = p->getPPQ();
                for (const auto& item : p->getArrangement(0).getPlaylistItems())
                {
                    if (item.itemIndex >= item.patternBase) continue; // pattern clip, not this channel
                    if (item.itemIndex != iid) continue;
                    if (ppq > 0)
                        clipEndInBeats = std::max(clipEndInBeats, (double)item.length / (double)ppq);
                }
                curveView.setClipLength(clipEndInBeats);
                return 0;
            }
            curveView.clear();
            return 0;
        });
}

void AutomationToolsComponent::setStatus(const juce::String& msg)
{
    statusLabel.setText(msg, juce::dontSendNotification);
    logBox.moveCaretToEnd();
    logBox.insertTextAtCaret("[" + juce::Time::getCurrentTime().toString(true, true) + "] " + msg + "\n");
}

// The curve-type table lives in one place -- FL::AutomationEvent -- and is
// the one checked against real files (0=Single, 1=Double, 2=Hold, 3=Stairs,
// 4=Smooth Stairs, 5=Pulse, 6=Wave, 9=Half Sine). The old local copy here
// disagreed with it, so e.g. choosing "Half Sine" wrote byte 0x05 (Pulse).
juce::String AutomationToolsComponent::getCurveTypeName(int curveType) const
{
    return FL::AutomationEvent::getCurveTypeName(curveType);
}

std::vector<std::pair<int, juce::String>> AutomationToolsComponent::getCurveTypeList() const
{
    return FL::AutomationEvent::getCurveTypeList();
}

void AutomationToolsComponent::applyScale()
{
    double factor = factorEditor.getText().getDoubleValue();
    if (factor <= 0.0) { setStatus("Enter a positive scale factor first."); return; }
    applyToSelected(processorRef, channelSelector, channelIidsForSelector,
        [factor](std::vector<FL::AutomationEvent::Record>& pts) { FL::AutomationEditor::scalePoints(pts, factor); },
        [this](const juce::String& msg) { setStatus("Scale x" + factorEditor.getText() + ": " + msg); refreshPointInfo(); });
}

void AutomationToolsComponent::applyInvert()
{
    applyToSelected(processorRef, channelSelector, channelIidsForSelector,
        [](std::vector<FL::AutomationEvent::Record>& pts) { FL::AutomationEditor::invertPoints(pts); },
        [this](const juce::String& msg) { setStatus("Invert: " + msg); refreshPointInfo(); });
}

void AutomationToolsComponent::applySmooth()
{
    int window = factorEditor.getText().getIntValue();
    if (window < 2) { setStatus("Enter a window size of at least 2 first."); return; }
    applyToSelected(processorRef, channelSelector, channelIidsForSelector,
        [window](std::vector<FL::AutomationEvent::Record>& pts) { FL::AutomationEditor::smoothPoints(pts, window); },
        [this, window](const juce::String& msg) { setStatus("Smooth (window " + juce::String(window) + "): " + msg); refreshPointInfo(); });
}

void AutomationToolsComponent::applyRemoveRedundant()
{
    double tolerance = factorEditor.getText().getDoubleValue();
    if (tolerance <= 0.0) tolerance = 0.001;
    applyToSelected(processorRef, channelSelector, channelIidsForSelector,
        [tolerance](std::vector<FL::AutomationEvent::Record>& pts) { FL::AutomationEditor::removeRedundantPoints(pts, tolerance); },
        [this, tolerance](const juce::String& msg) { setStatus("Remove redundant (tol " + juce::String(tolerance) + "): " + msg); refreshPointInfo(); });
}

void AutomationToolsComponent::applyCurveType(int curveType)
{
    // Apply curve type to the selected channel
    int idx = channelSelector.getSelectedItemIndex();
    if (idx < 0 || idx >= channelIidsForSelector.size())
    {
        setStatus("Select a channel first.");
        return;
    }
    int iid = channelIidsForSelector[idx];

    processorRef.withProjectLock([&](FL::Project* p) -> int
        {
            if (p == nullptr) { setStatus("No project loaded."); return 0; }
            for (auto* ch : p->getChannels())
            {
                if (ch->getIID() != iid) continue;

                auto* ev = ch->getMutableTree().firstEvent(FL::EventID::Automation);
                auto* autoEv = dynamic_cast<FL::AutomationEvent*>(ev);
                if (autoEv && !autoEv->records.empty())
                {
                    // Apply curve type to all interior points (skip first endpoint)
                    for (size_t i = 1; i < autoEv->records.size(); ++i)
                    {
                        autoEv->records[i].curveType = curveType;
                    }
                    setStatus("Applied curve type: " + getCurveTypeName(curveType) +
                        " to " + juce::String(autoEv->records.size() - 1) + " segment(s).");
                    refreshPointInfo();
                }
                else
                {
                    setStatus("No automation data found on this channel.");
                }
                return 0;
            }
            setStatus("Channel not found.");
            return 0;
        });
}

void AutomationToolsComponent::saveAs()
{
    saveChooser = std::make_unique<juce::FileChooser>(
        "Save modified project as...",
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("modified.flp"),
        "*.flp");

    constexpr auto chooserFlags = juce::FileBrowserComponent::saveMode
        | juce::FileBrowserComponent::warnAboutOverwriting;

    saveChooser->launchAsync(chooserFlags, [this](const juce::FileChooser& chooser)
        {
            auto file = chooser.getResult();
            if (file == juce::File{}) return;

            bool ok = processorRef.saveProjectAs(file);
            setStatus(ok ? ("Saved to " + file.getFullPathName()) : "Save failed.");
        });
}

AutomationToolsWindow::AutomationToolsWindow(FLProjectOrganizerProcessor& processor)
    : DialogWindow("Automation Tools", juce::Colour(0xFF181818), true, true)
{
    setUsingNativeTitleBar(true);
    setContentOwned(new AutomationToolsComponent(processor), true);
    centreWithSize(700, 760);
    setResizable(true, true);
    setVisible(true);
}

void AutomationToolsWindow::closeButtonPressed()
{
    setVisible(false);
    delete this;
}