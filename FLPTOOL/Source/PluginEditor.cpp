#include "PluginProcessor.h"
#include "PluginEditor.h"

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

    // Tool Selector
    toolSelector.addItem("Project Statistics", ToolID::Stats + 1);
    toolSelector.addItem("Plugin Usage", ToolID::Plugins + 1);
    toolSelector.addItem("Missing Samples", ToolID::Samples + 1);
    toolSelector.addItem("Arrangement Layout", ToolID::Arrangement + 1);
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
    titleLabel.setBounds(topBar.removeFromLeft(topBar.getWidth() - 180));
    loadButton.setBounds(topBar.removeFromRight(160));
    
    bounds.removeFromTop(10); // Spacer

    // Tool selector
    auto toolBar = bounds.removeFromTop(30);
    toolSelector.setBounds(toolBar);

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
            statusLabel.setText("Status: Failed to load file. Check console.", juce::dontSendNotification);
            outputDisplay.setText("Error: Could not parse the FLP file. It may be corrupted or an unsupported version.");
        }
    });
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
        default:
            report = "Unknown tool selected.";
    }

    outputDisplay.setText(report);
}