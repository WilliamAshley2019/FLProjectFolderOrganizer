#include "SampleScannerWindow.h"

int SampleScannerComponent::TableModel::getNumRows()
{
    return (int)rows.size();
}

void SampleScannerComponent::TableModel::paintRowBackground(
    juce::Graphics& g, int rowNumber, int, int, bool rowIsSelected)
{
    auto bg = rowIsSelected ? juce::Colour(0xFF2A2A3A) :
        (rowNumber % 2 ? juce::Colour(0xFF222222) : juce::Colour(0xFF1A1A1A));
    g.fillAll(bg);
}

void SampleScannerComponent::TableModel::paintCell(
    juce::Graphics& g, int rowNumber, int columnId, int width, int height, bool)
{
    if (rowNumber >= (int)rows.size())
        return;

    const auto& e = rows[(size_t)rowNumber];
    juce::String text;
    switch (columnId)
    {
        case 1: text = e.fileName; break;
        case 2: text = e.extension.toUpperCase(); break;
        case 3: text = juce::String(e.fileSize / 1024) + " KB"; break;
        case 4: text = juce::Time(e.lastModified).toString(true, true); break;
        default: break;
    }

    g.setColour(juce::Colours::lightgrey);
    g.setFont(juce::Font(juce::FontOptions(13.0f)));
    g.drawText(text, 4, 0, width - 8, height, juce::Justification::centredLeft);
}

SampleScannerComponent::SampleScannerComponent()
{
    setSize(1000, 700);

    addAndMakeVisible(titleLabel);
    titleLabel.setText("Sample Scanner (.wav / .mp3 / .flac / .ogg)", juce::dontSendNotification);
    titleLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    titleLabel.setFont(juce::Font(juce::FontOptions(20.0f, juce::Font::bold)));

    addAndMakeVisible(sourceLabel);
    sourceLabel.setText("Source Folder:", juce::dontSendNotification);
    sourceLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible(sourcePathEditor);
    sourcePathEditor.setReadOnly(true);
    sourcePathEditor.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xFF2A2A2A));
    sourcePathEditor.setColour(juce::TextEditor::textColourId, juce::Colours::white);
    addAndMakeVisible(browseSourceBtn);
    browseSourceBtn.setButtonText("Browse...");
    browseSourceBtn.onClick = [this] { browseSource(); };

    addAndMakeVisible(destLabel);
    destLabel.setText("Destination Folder:", juce::dontSendNotification);
    destLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible(destPathEditor);
    destPathEditor.setReadOnly(true);
    destPathEditor.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xFF2A2A2A));
    destPathEditor.setColour(juce::TextEditor::textColourId, juce::Colours::white);
    addAndMakeVisible(browseDestBtn);
    browseDestBtn.setButtonText("Browse...");
    browseDestBtn.onClick = [this] { browseDest(); };

    auto setupToggle = [this](juce::ToggleButton& t, const juce::String& text, bool defaultOn)
    {
        addAndMakeVisible(t);
        t.setButtonText(text);
        t.setToggleState(defaultOn, juce::dontSendNotification);
        t.setColour(juce::ToggleButton::textColourId, juce::Colours::lightgrey);
    };
    setupToggle(wavToggle, "WAV", true);
    setupToggle(mp3Toggle, "MP3", true);
    setupToggle(flacToggle, "FLAC", true);
    setupToggle(oggToggle, "OGG", true);
    setupToggle(scanSubfoldersToggle, "Scan Subfolders", true);
    setupToggle(dryRunToggle, "Dry Run (Preview Only)", true);
    setupToggle(deleteOriginalsToggle, "Delete Originals After Copy", false);

    addAndMakeVisible(scanBtn);
    scanBtn.setButtonText("Scan");
    scanBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFFFF5C00));
    scanBtn.onClick = [this] { startScan(); };

    addAndMakeVisible(stopBtn);
    stopBtn.setButtonText("Stop");
    stopBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFFFF4444));
    stopBtn.onClick = [this] { stopScan(); };
    stopBtn.setEnabled(false);

    addAndMakeVisible(statusLabel);
    statusLabel.setText("Ready", juce::dontSendNotification);
    statusLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);

    addAndMakeVisible(progressBar);

    addAndMakeVisible(table);
    tableModel = std::make_unique<TableModel>();
    table.setModel(tableModel.get());
    table.getHeader().addColumn("File Name", 1, 300);
    table.getHeader().addColumn("Type", 2, 80);
    table.getHeader().addColumn("Size", 3, 100);
    table.getHeader().addColumn("Modified", 4, 180);

    addAndMakeVisible(logBox);
    logBox.setMultiLine(true);
    logBox.setReadOnly(true);
    logBox.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xFF111111));
    logBox.setColour(juce::TextEditor::textColourId, juce::Colours::lightgrey);
    logBox.setFont(juce::Font(juce::FontOptions(12.0f)));

    startTimerHz(30);
}

SampleScannerComponent::~SampleScannerComponent()
{
    if (scanner)
        scanner->StopScanning();
}

void SampleScannerComponent::resized()
{
    auto bounds = getLocalBounds().reduced(15);

    titleLabel.setBounds(bounds.removeFromTop(30));
    bounds.removeFromTop(8);

    auto sourceRow = bounds.removeFromTop(28);
    sourceLabel.setBounds(sourceRow.removeFromLeft(120));
    sourcePathEditor.setBounds(sourceRow.removeFromLeft(sourceRow.getWidth() - 100));
    browseSourceBtn.setBounds(sourceRow.removeFromRight(90));
    bounds.removeFromTop(5);

    auto destRow = bounds.removeFromTop(28);
    destLabel.setBounds(destRow.removeFromLeft(120));
    destPathEditor.setBounds(destRow.removeFromLeft(destRow.getWidth() - 100));
    browseDestBtn.setBounds(destRow.removeFromRight(90));
    bounds.removeFromTop(8);

    auto toggleRow = bounds.removeFromTop(26);
    int tw = 90;
    wavToggle.setBounds(toggleRow.removeFromLeft(tw));
    mp3Toggle.setBounds(toggleRow.removeFromLeft(tw));
    flacToggle.setBounds(toggleRow.removeFromLeft(tw));
    oggToggle.setBounds(toggleRow.removeFromLeft(tw));
    bounds.removeFromTop(4);

    auto toggleRow2 = bounds.removeFromTop(26);
    scanSubfoldersToggle.setBounds(toggleRow2.removeFromLeft(160));
    dryRunToggle.setBounds(toggleRow2.removeFromLeft(200));
    deleteOriginalsToggle.setBounds(toggleRow2.removeFromLeft(220));
    bounds.removeFromTop(8);

    auto btnRow = bounds.removeFromTop(36);
    scanBtn.setBounds(btnRow.removeFromLeft(100));
    btnRow.removeFromLeft(10);
    stopBtn.setBounds(btnRow.removeFromLeft(80));
    btnRow.removeFromLeft(20);
    statusLabel.setBounds(btnRow.removeFromLeft(200));
    progressBar.setBounds(btnRow.removeFromLeft(btnRow.getWidth()));
    bounds.removeFromTop(8);

    auto tableArea = bounds.removeFromTop((int)(bounds.getHeight() * 0.6f));
    table.setBounds(tableArea);
    bounds.removeFromTop(5);
    logBox.setBounds(bounds);
}

void SampleScannerComponent::timerCallback()
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

    if (scanner && !scanner->isThreadRunning() && stopBtn.isEnabled())
    {
        stopBtn.setEnabled(false);
        scanBtn.setEnabled(true);
        statusLabel.setText("Done -- " + juce::String((int)tableModel->rows.size()) + " samples found",
            juce::dontSendNotification);
    }
}

void SampleScannerComponent::browseSource()
{
    sourceChooser = std::make_unique<juce::FileChooser>("Select Source Folder", juce::File(), "*");
    int chooserFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories;
    sourceChooser->launchAsync(chooserFlags, [this](const juce::FileChooser& chooser)
    {
        auto result = chooser.getResult();
        if (result.isDirectory())
            sourcePathEditor.setText(result.getFullPathName());
    });
}

void SampleScannerComponent::browseDest()
{
    destChooser = std::make_unique<juce::FileChooser>("Select Destination Folder", juce::File(), "*");
    int chooserFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories;
    destChooser->launchAsync(chooserFlags, [this](const juce::FileChooser& chooser)
    {
        auto result = chooser.getResult();
        if (result.isDirectory())
            destPathEditor.setText(result.getFullPathName());
    });
}

void SampleScannerComponent::startScan()
{
    juce::File source(sourcePathEditor.getText());
    if (!source.isDirectory())
    {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
            "Error", "Please select a valid source folder.");
        return;
    }

    juce::StringArray exts;
    if (wavToggle.getToggleState())  exts.add("wav");
    if (mp3Toggle.getToggleState())  exts.add("mp3");
    if (flacToggle.getToggleState()) exts.add("flac");
    if (oggToggle.getToggleState())  exts.add("ogg");

    if (exts.isEmpty())
    {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
            "Error", "Select at least one file type to scan for.");
        return;
    }

    auto launch = [this, source, exts]()
    {
        tableModel->rows.clear();
        table.updateContent();

        scanner = std::make_unique<SampleScanner>();
        scanner->onLog = [this](const juce::String& msg) { sendLog(msg); };
        scanner->onProgress = [this](float p) { currentProgress = (double)p; };
        scanner->onSampleFound = [this](const SampleScanner::SampleEntry& e)
        {
            juce::MessageManager::callAsync([this, e]
            {
                tableModel->rows.push_back(e);
                table.updateContent();
            });
        };
        scanner->onDuplicateFound = [this](const juce::String& f1, const juce::String& f2)
        {
            sendLog("DUPLICATE: " + f1 + " == " + f2);
        };

        scanner->Scan(source, juce::File(destPathEditor.getText()), exts,
            scanSubfoldersToggle.getToggleState(),
            dryRunToggle.getToggleState(),
            deleteOriginalsToggle.getToggleState());

        scanBtn.setEnabled(false);
        stopBtn.setEnabled(true);
        statusLabel.setText("Scanning...", juce::dontSendNotification);
    };

    if (deleteOriginalsToggle.getToggleState())
    {
        juce::AlertWindow::showAsync(
            juce::MessageBoxOptions()
                .withIconType(juce::MessageBoxIconType::WarningIcon)
                .withTitle("Confirm Delete")
                .withMessage("Originals will be moved to the recycle bin after copying. Continue?")
                .withButton("Continue")
                .withButton("Cancel"),
            [launch](int result)
            {
                if (result == 1)
                    launch();
            });
    }
    else
    {
        launch();
    }
}

void SampleScannerComponent::stopScan()
{
    if (scanner)
        scanner->StopScanning();
    sendLog("Stop requested.");
}

void SampleScannerComponent::sendLog(const juce::String& msg)
{
    juce::ScopedLock sl(logLock);
    pendingLogs.add("[" + juce::Time::getCurrentTime().toString(true, true) + "] " + msg);
}

SampleScannerWindow::SampleScannerWindow()
    : DialogWindow("Sample Scanner", juce::Colour(0xFF181818), true, true)
{
    setUsingNativeTitleBar(true);
    setContentOwned(new SampleScannerComponent(), true);
    centreWithSize(1000, 700);
    setResizable(true, true);
    setVisible(true);
}

void SampleScannerWindow::closeButtonPressed()
{
    setVisible(false);
    delete this;
}
