#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "BatchProcessWindow.h"
#include "AutomationToolsWindow.h"

PluginEditor::PluginEditor (PluginProcessor& p)
    : AudioProcessorEditor (&p), processorRef (p)
{
    // Set a nice, modern size for the utility window
    setSize (800, 600);
    setResizable(true, true);
    setResizeLimits(600, 400, 1200, 900);

    // Title
    titleLabel.setText("FL Studio Project Inspector", juce::dontSendNotification);
    titleLabel.setFont(juce::FontOptions(24.0f, juce::Font::bold));
    titleLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(titleLabel);

    // Load Button
    loadButton.onClick = [this] { loadButtonClicked(); };
    addAndMakeVisible(loadButton);

    // Export-to-MIDI Button
    exportMidiButton.onClick = [this] { exportMidiButtonClicked(); };
    addAndMakeVisible(exportMidiButton);

    // Export-to-JSON Button
    exportJsonButton.onClick = [this] { exportJsonButtonClicked(); };
    addAndMakeVisible(exportJsonButton);

    // Compare Button
    compareButton.onClick = [this] { compareButtonClicked(); };
    addAndMakeVisible(compareButton);

    // Batch Process Button
    batchProcessButton.onClick = [this] { batchProcessButtonClicked(); };
    addAndMakeVisible(batchProcessButton);

    // Automation Tools Button
    automationToolsButton.onClick = [this] { automationToolsButtonClicked(); };
    addAndMakeVisible(automationToolsButton);

    // Tool Selector
    toolSelector.addItem("Project Statistics", ToolID::Stats + 1);
    toolSelector.addItem("Plugin Usage", ToolID::Plugins + 1);
    toolSelector.addItem("Missing Samples", ToolID::Samples + 1);
    toolSelector.addItem("Arrangement Layout", ToolID::Arrangement + 1);
    toolSelector.addItem("Cleanup Report", ToolID::Cleanup + 1);
    toolSelector.addItem("Comparison", ToolID::Comparison + 1);
    toolSelector.setSelectedId(ToolID::Stats + 1);
    toolSelector.onChange = [this] { toolSelectorChanged(); };
    addAndMakeVisible(toolSelector);

    // Output Display (Read-only text area)
    outputDisplay.setMultiLine(true);
    outputDisplay.setReadOnly(true);
    outputDisplay.setScrollbarsShown(true);
    outputDisplay.setCaretVisible(false);
    outputDisplay.setPopupMenuEnabled(true);
    outputDisplay.setFont(juce::FontOptions(14.0f));
    outputDisplay.setText("Click 'Load FLP Project...' to begin.", juce::dontSendNotification);
    addAndMakeVisible(outputDisplay);

    // Status Label
    statusLabel.setText("Status: Idle", juce::dontSendNotification);
    statusLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(statusLabel);
}

PluginEditor::~PluginEditor() {}

void PluginEditor::paint (juce::Graphics& g)
{
    // Dark modern theme
    g.fillAll (juce::Colour::fromRGB(30, 30, 35));
    
    // Draw a subtle border around the text area
    g.setColour (juce::Colours::darkgrey);
    g.drawRect (outputDisplay.getBounds(), 1);
}

void PluginEditor::resized()
{
    auto bounds = getLocalBounds().reduced(15);
    
    // Top bar: Title and Load Button
    auto topBar = bounds.removeFromTop(40);
    titleLabel.setBounds(topBar.removeFromLeft(topBar.getWidth() - 160));
    loadButton.setBounds(topBar.removeFromRight(150));
    
    bounds.removeFromTop(10); // Spacer

    // Tool selector + compare/export buttons share a row
    auto toolBar = bounds.removeFromTop(30);
    exportMidiButton.setBounds(toolBar.removeFromRight(190));
    toolBar.removeFromRight(8);
    compareButton.setBounds(toolBar.removeFromRight(130));
    toolBar.removeFromRight(8);
    toolSelector.setBounds(toolBar);

    bounds.removeFromTop(8); // Spacer

    // Second action row: dialog-launching tools (batch processing, automation editing)
    auto toolBar2 = bounds.removeFromTop(30);
    automationToolsButton.setBounds(toolBar2.removeFromLeft(160));
    toolBar2.removeFromLeft(8);
    batchProcessButton.setBounds(toolBar2.removeFromLeft(160));
    toolBar2.removeFromLeft(8);
    exportJsonButton.setBounds(toolBar2.removeFromLeft(190));

    bounds.removeFromTop(10); // Spacer

    // Status bar at the bottom
    auto bottomBar = bounds.removeFromBottom(25);
    statusLabel.setBounds(bottomBar);
    
    bounds.removeFromBottom(10); // Spacer

    // Main output display takes the rest
    outputDisplay.setBounds(bounds);
}

void PluginEditor::loadButtonClicked()
{
    fileChooser = std::make_unique<juce::FileChooser>(
        "Select an FL Studio Project...",
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
        "*.flp");

    constexpr auto flags = juce::FileBrowserComponent::openMode
                          | juce::FileBrowserComponent::canSelectFiles;

    fileChooser->launchAsync(flags, [this](const juce::FileChooser& chooser)
    {
        auto file = chooser.getResult();
        if (!file.existsAsFile())
        {
            statusLabel.setText("Status: Cancelled.", juce::dontSendNotification);
            return;
        }

        statusLabel.setText("Status: Loading " + file.getFileName() + "...", juce::dontSendNotification);

        // In a production app, you might want to do this on a background thread
        // to avoid blocking the UI thread for massive projects.
        bool success = processorRef.loadFLPFile(file);

        if (success)
        {
            statusLabel.setText("Status: Loaded successfully. (" + file.getFileName() + ")", juce::dontSendNotification);
            refreshOutputDisplay();
        }
        else
        {
            statusLabel.setText("Status: Failed to load file.", juce::dontSendNotification);
            outputDisplay.setText("Error: Could not parse the FLP file.\n\n" + processorRef.getLastLoadError());
        }
    });
}

void PluginEditor::exportMidiButtonClicked()
{
    if (!processorRef.isProjectLoaded())
    {
        statusLabel.setText("Status: Load a project first.", juce::dontSendNotification);
        return;
    }

    midiSaveChooser = std::make_unique<juce::FileChooser>(
        "Export patterns to MIDI file...",
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("patterns.mid"),
        "*.mid");

    constexpr auto flags = juce::FileBrowserComponent::saveMode
                          | juce::FileBrowserComponent::warnAboutOverwriting;

    midiSaveChooser->launchAsync(flags, [this](const juce::FileChooser& chooser)
    {
        auto file = chooser.getResult();
        if (file == juce::File{})
        {
            statusLabel.setText("Status: Export cancelled.", juce::dontSendNotification);
            return;
        }

        auto result = processorRef.exportPatternsToMidi(file);
        statusLabel.setText("Status: " + result.upToFirstOccurrenceOf("\n", false, false), juce::dontSendNotification);
        outputDisplay.setText(result);
    });
}

void PluginEditor::compareButtonClicked()
{
    if (!processorRef.isProjectLoaded())
    {
        statusLabel.setText("Status: Load a project first.", juce::dontSendNotification);
        return;
    }

    compareChooser = std::make_unique<juce::FileChooser>(
        "Select a project to compare against...",
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
        "*.flp");

    constexpr auto flags = juce::FileBrowserComponent::openMode
                          | juce::FileBrowserComponent::canSelectFiles;

    compareChooser->launchAsync(flags, [this](const juce::FileChooser& chooser)
    {
        auto file = chooser.getResult();
        if (!file.existsAsFile())
            return;

        statusLabel.setText("Status: Comparing with " + file.getFileName() + "...", juce::dontSendNotification);
        lastComparisonReport = processorRef.compareWithFile(file);
        toolSelector.setSelectedId(ToolID::Comparison + 1); // switches tool + triggers refreshOutputDisplay via onChange
        statusLabel.setText("Status: Comparison complete.", juce::dontSendNotification);
    });
}

void PluginEditor::exportJsonButtonClicked()
{
    if (!processorRef.isProjectLoaded())
    {
        statusLabel.setText("Status: Load a project first.", juce::dontSendNotification);
        return;
    }

    jsonSaveChooser = std::make_unique<juce::FileChooser>(
        "Export full project data to JSON...",
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("project_data.json"),
        "*.json");

    constexpr auto flags = juce::FileBrowserComponent::saveMode
                          | juce::FileBrowserComponent::warnAboutOverwriting;

    jsonSaveChooser->launchAsync(flags, [this](const juce::FileChooser& chooser)
    {
        auto file = chooser.getResult();
        if (file == juce::File{})
        {
            statusLabel.setText("Status: Export cancelled.", juce::dontSendNotification);
            return;
        }

        auto result = processorRef.exportFullDataAsJson(file);
        statusLabel.setText("Status: " + result.upToFirstOccurrenceOf("\n", false, false), juce::dontSendNotification);
        outputDisplay.setText(result);
    });
}

void PluginEditor::batchProcessButtonClicked()
{
    new BatchProcessWindow(); // self-deleting on close, matches the DialogWindow pattern used throughout this codebase
}

void PluginEditor::automationToolsButtonClicked()
{
    if (!processorRef.isProjectLoaded())
    {
        statusLabel.setText("Status: Load a project first.", juce::dontSendNotification);
        return;
    }
    new AutomationToolsWindow(processorRef); // self-deleting on close
}

void PluginEditor::toolSelectorChanged()
{
    refreshOutputDisplay();
}

void PluginEditor::refreshOutputDisplay()
{
    if (!processorRef.isProjectLoaded())
    {
        outputDisplay.setText("No project loaded.");
        return;
    }

    juce::String report;
    int selectedTool = toolSelector.getSelectedId() - 1;

    switch (selectedTool)
    {
        case ToolID::Stats:
            report = processorRef.getStatsReport();
            break;
        case ToolID::Plugins:
            report = processorRef.getPluginReport();
            break;
        case ToolID::Samples:
            report = processorRef.getSampleReport();
            break;
        case ToolID::Arrangement:
            report = processorRef.getArrangementReport();
            break;
        case ToolID::Cleanup:
            report = processorRef.getCleanupReport();
            break;
        case ToolID::Comparison:
            report = lastComparisonReport;
            break;
        default:
            report = "Unknown tool selected.";
    }

    outputDisplay.setText(report);
}