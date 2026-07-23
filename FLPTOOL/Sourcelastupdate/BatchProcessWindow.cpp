#include "BatchProcessWindow.h"

BatchProcessComponent::BatchProcessComponent()
{
    setSize(700, 620);

    addAndMakeVisible(titleLabel);
    titleLabel.setText("Batch Process .flp Files", juce::dontSendNotification);
    titleLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    titleLabel.setFont(juce::Font(juce::FontOptions(20.0f, juce::Font::bold)));

    addAndMakeVisible(inputFilesLabel);
    inputFilesLabel.setText("Input Files:", juce::dontSendNotification);
    inputFilesLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);

    addAndMakeVisible(inputFilesEditor);
    inputFilesEditor.setReadOnly(true);
    inputFilesEditor.setMultiLine(true);
    inputFilesEditor.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xFF2A2A2A));
    inputFilesEditor.setColour(juce::TextEditor::textColourId, juce::Colours::white);
    inputFilesEditor.setText("No files selected.");

    addAndMakeVisible(addFilesBtn);
    addFilesBtn.setButtonText("Add Files...");
    addFilesBtn.onClick = [this] { addInputFiles(); };

    addAndMakeVisible(clearFilesBtn);
    clearFilesBtn.setButtonText("Clear");
    clearFilesBtn.onClick = [this]
        {
            selectedFiles.clear();
            inputFilesEditor.setText("No files selected.");
        };

    addAndMakeVisible(outputDirLabel);
    outputDirLabel.setText("Output Folder:", juce::dontSendNotification);
    outputDirLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible(outputDirEditor);
    outputDirEditor.setReadOnly(true);
    outputDirEditor.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xFF2A2A2A));
    outputDirEditor.setColour(juce::TextEditor::textColourId, juce::Colours::white);
    addAndMakeVisible(browseOutputBtn);
    browseOutputBtn.setButtonText("Browse...");
    browseOutputBtn.onClick = [this] { browseOutputDir(); };

    addAndMakeVisible(tempoScaleLabel);
    tempoScaleLabel.setText("Tempo Scale Factor (1.0 = no change):", juce::dontSendNotification);
    tempoScaleLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible(tempoScaleEditor);
    tempoScaleEditor.setText("1.0");
    tempoScaleEditor.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xFF2A2A2A));
    tempoScaleEditor.setColour(juce::TextEditor::textColourId, juce::Colours::white);
    tempoScaleEditor.setInputRestrictions(8, "0123456789.");

    addAndMakeVisible(renameTracksToggle);
    renameTracksToggle.setButtonText("Rename tracks from pattern names");
    renameTracksToggle.setColour(juce::ToggleButton::textColourId, juce::Colours::lightgrey);

    addAndMakeVisible(makeSamplesRelativeToggle);
    makeSamplesRelativeToggle.setButtonText("Make sample paths relative to:");
    makeSamplesRelativeToggle.setColour(juce::ToggleButton::textColourId, juce::Colours::lightgrey);

    addAndMakeVisible(relativeBaseDirEditor);
    relativeBaseDirEditor.setReadOnly(true);
    relativeBaseDirEditor.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xFF2A2A2A));
    relativeBaseDirEditor.setColour(juce::TextEditor::textColourId, juce::Colours::white);
    addAndMakeVisible(browseRelativeBaseBtn);
    browseRelativeBaseBtn.setButtonText("Browse...");
    browseRelativeBaseBtn.onClick = [this] { browseRelativeBaseDir(); };

    addAndMakeVisible(watermarkTitleLabel);
    watermarkTitleLabel.setText("Watermark Title (optional):", juce::dontSendNotification);
    watermarkTitleLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible(watermarkTitleEditor);
    watermarkTitleEditor.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xFF2A2A2A));
    watermarkTitleEditor.setColour(juce::TextEditor::textColourId, juce::Colours::white);

    addAndMakeVisible(watermarkAuthorLabel);
    watermarkAuthorLabel.setText("Watermark Author (optional):", juce::dontSendNotification);
    watermarkAuthorLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible(watermarkAuthorEditor);
    watermarkAuthorEditor.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xFF2A2A2A));
    watermarkAuthorEditor.setColour(juce::TextEditor::textColourId, juce::Colours::white);

    addAndMakeVisible(runBtn);
    runBtn.setButtonText("Run Batch");
    runBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFFFF5C00));
    runBtn.onClick = [this] { startBatch(); };

    addAndMakeVisible(statusLabel);
    statusLabel.setText("Ready", juce::dontSendNotification);
    statusLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);

    addAndMakeVisible(progressBar);

    addAndMakeVisible(logBox);
    logBox.setMultiLine(true);
    logBox.setReadOnly(true);
    logBox.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xFF111111));
    logBox.setColour(juce::TextEditor::textColourId, juce::Colours::lightgrey);
    logBox.setFont(juce::Font(juce::FontOptions(12.0f)));

    startTimerHz(30);
}

BatchProcessComponent::~BatchProcessComponent() {}

void BatchProcessComponent::resized()
{
    auto bounds = getLocalBounds().reduced(15);

    titleLabel.setBounds(bounds.removeFromTop(28));
    bounds.removeFromTop(8);

    inputFilesLabel.setBounds(bounds.removeFromTop(18));
    auto inputRow = bounds.removeFromTop(70);
    inputFilesEditor.setBounds(inputRow.removeFromLeft(inputRow.getWidth() - 110));
    inputRow.removeFromLeft(8);
    addFilesBtn.setBounds(inputRow.removeFromTop(30));
    inputRow.removeFromTop(6);
    clearFilesBtn.setBounds(inputRow.removeFromTop(30));
    bounds.removeFromTop(8);

    auto outRow = bounds.removeFromTop(26);
    outputDirLabel.setBounds(outRow.removeFromLeft(120));
    outputDirEditor.setBounds(outRow.removeFromLeft(outRow.getWidth() - 100));
    browseOutputBtn.setBounds(outRow.removeFromRight(90));
    bounds.removeFromTop(10);

    auto tempoRow = bounds.removeFromTop(24);
    tempoScaleLabel.setBounds(tempoRow.removeFromLeft(280));
    tempoScaleEditor.setBounds(tempoRow.removeFromLeft(80));
    bounds.removeFromTop(8);

    renameTracksToggle.setBounds(bounds.removeFromTop(24));
    bounds.removeFromTop(4);

    makeSamplesRelativeToggle.setBounds(bounds.removeFromTop(24));
    auto relRow = bounds.removeFromTop(26);
    relativeBaseDirEditor.setBounds(relRow.removeFromLeft(relRow.getWidth() - 100));
    browseRelativeBaseBtn.setBounds(relRow.removeFromRight(90));
    bounds.removeFromTop(10);

    auto wmTitleRow = bounds.removeFromTop(24);
    watermarkTitleLabel.setBounds(wmTitleRow.removeFromLeft(200));
    watermarkTitleEditor.setBounds(wmTitleRow);
    bounds.removeFromTop(6);

    auto wmAuthorRow = bounds.removeFromTop(24);
    watermarkAuthorLabel.setBounds(wmAuthorRow.removeFromLeft(200));
    watermarkAuthorEditor.setBounds(wmAuthorRow);
    bounds.removeFromTop(10);

    auto runRow = bounds.removeFromTop(36);
    runBtn.setBounds(runRow.removeFromLeft(120));
    runRow.removeFromLeft(15);
    statusLabel.setBounds(runRow.removeFromLeft(200));
    progressBar.setBounds(runRow);
    bounds.removeFromTop(8);

    logBox.setBounds(bounds);
}

void BatchProcessComponent::timerCallback()
{
    juce::ScopedLock sl(logLock);
    if (!pendingLogs.isEmpty())
    {
        for (const auto& msg : pendingLogs)
        {
            logBox.moveCaretToEnd();
            logBox.insertTextAtCaret(msg + "\n");
        }
        pendingLogs.clear();
    }
}

void BatchProcessComponent::addInputFiles()
{
    inputFilesChooser = std::make_unique<juce::FileChooser>(
        "Select .flp files to process", juce::File(), "*.flp");

    inputFilesChooser->launchAsync(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectMultipleItems,
        [this](const juce::FileChooser& chooser)
        {
            for (auto& f : chooser.getResults())
                if (f.existsAsFile() && !selectedFiles.contains(f))
                    selectedFiles.add(f);

            juce::StringArray names;
            for (auto& f : selectedFiles) names.add(f.getFileName());
            inputFilesEditor.setText(names.isEmpty() ? "No files selected." : names.joinIntoString("\n"));
        });
}

void BatchProcessComponent::browseOutputDir()
{
    outputDirChooser = std::make_unique<juce::FileChooser>("Select output folder", juce::File(), "*");
    outputDirChooser->launchAsync(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
        [this](const juce::FileChooser& chooser)
        {
            auto result = chooser.getResult();
            if (result.isDirectory())
                outputDirEditor.setText(result.getFullPathName());
        });
}

void BatchProcessComponent::browseRelativeBaseDir()
{
    relativeBaseChooser = std::make_unique<juce::FileChooser>("Select base folder for relative sample paths", juce::File(), "*");
    relativeBaseChooser->launchAsync(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
        [this](const juce::FileChooser& chooser)
        {
            auto result = chooser.getResult();
            if (result.isDirectory())
                relativeBaseDirEditor.setText(result.getFullPathName());
        });
}

void BatchProcessComponent::sendLog(const juce::String& msg)
{
    juce::ScopedLock sl(logLock);
    pendingLogs.add("[" + juce::Time::getCurrentTime().toString(true, true) + "] " + msg);
}

void BatchProcessComponent::startBatch()
{
    if (selectedFiles.isEmpty())
    {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
            "Error", "Add at least one .flp file to process.");
        return;
    }

    juce::File outDir(outputDirEditor.getText());
    if (!outDir.getFullPathName().isNotEmpty() || outputDirEditor.getText().isEmpty())
    {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
            "Error", "Select an output folder.");
        return;
    }

    FL::BatchProcessor::Options options;
    options.tempoScaleFactor = juce::jmax(0.01, tempoScaleEditor.getText().getDoubleValue());
    options.renameTracksFromPatterns = renameTracksToggle.getToggleState();
    options.makeSamplesRelative = makeSamplesRelativeToggle.getToggleState();
    options.baseDirectoryForRelative = juce::File(relativeBaseDirEditor.getText());
    options.watermarkTitle = watermarkTitleEditor.getText();
    options.watermarkAuthor = watermarkAuthorEditor.getText();

    if (options.makeSamplesRelative && !options.baseDirectoryForRelative.isDirectory())
    {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
            "Error", "\"Make sample paths relative\" is checked but no valid base folder is set.");
        return;
    }

    runBtn.setEnabled(false);
    statusLabel.setText("Running...", juce::dontSendNotification);
    sendLog("Starting batch of " + juce::String(selectedFiles.size()) + " file(s) -> " + outDir.getFullPathName());

    runner = std::make_unique<BatchProcessRunner>();
    runner->onLog = [this](const juce::String& msg) { sendLog(msg); };
    runner->onProgress = [this](float p) { currentProgress = (double)p; };
    runner->onComplete = [this](int successCount, int total)
        {
            sendLog("Done - " + juce::String(successCount) + "/" + juce::String(total) + " file(s) processed successfully.");
            statusLabel.setText("Complete: " + juce::String(successCount) + "/" + juce::String(total), juce::dontSendNotification);
            runBtn.setEnabled(true);
        };
    runner->start(selectedFiles, outDir, options);
}

BatchProcessWindow::BatchProcessWindow()
    : DialogWindow("Batch Process", juce::Colour(0xFF181818), true, true)
{
    setUsingNativeTitleBar(true);
    setContentOwned(new BatchProcessComponent(), true);
    centreWithSize(700, 620);
    setResizable(true, true);
    setVisible(true);
}

void BatchProcessWindow::closeButtonPressed()
{
    setVisible(false);
    delete this;
}