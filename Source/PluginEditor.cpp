#include "PluginEditor.h"

// ============================================================================
// Result Table Model Implementation
// ============================================================================

int FLProjectOrganizerEditor::ResultTableModel::getNumRows()
{
    return (int)displayRows.size();
}

void FLProjectOrganizerEditor::ResultTableModel::paintRowBackground(
    juce::Graphics& g, int rowNumber, int width, int height, bool rowIsSelected)
{
    auto bg = rowIsSelected ? juce::Colour(0xFF2A2A3A) :
        (rowNumber % 2 ? juce::Colour(0xFF222222) : juce::Colour(0xFF1A1A1A));
    g.fillAll(bg);
}

void FLProjectOrganizerEditor::ResultTableModel::paintCell(
    juce::Graphics& g, int rowNumber, int columnId,
    int width, int height, bool rowIsSelected)
{
    if (rowNumber >= (int)displayRows.size())
        return;

    const auto& entry = displayRows[rowNumber];
    juce::String text;

    switch (columnId)
    {
    case 1: text = entry.fileName; break;
    case 2: text = entry.version; break;
    case 3: text = entry.groupName; break;
    case 4: text = juce::String(entry.fileSize / 1024) + " KB"; break;
    case 5: text = entry.isZipped ? "ZIP" : "FLP"; break;
    case 6:
    {
        juce::Time time(entry.lastModified);
        text = time.toString(true, true);
        break;
    }
    default: break;
    }

    g.setColour(rowIsSelected ? juce::Colours::white : juce::Colours::lightgrey);
    g.setFont(juce::Font(juce::FontOptions(14.0f)));
    g.drawText(text, 4, 0, width - 8, height, juce::Justification::centredLeft);
}

void FLProjectOrganizerEditor::ResultTableModel::sortOrderChanged(int newSortColumnId, bool isForwards)
{
    sortColumn = newSortColumnId;
    sortForwards = isForwards;

    std::sort(displayRows.begin(), displayRows.end(),
        [this, newSortColumnId, isForwards](const ProjectDatabase::ProjectEntry& a,
            const ProjectDatabase::ProjectEntry& b)
        {
            int result = 0;
            switch (newSortColumnId)
            {
            case 1: result = a.fileName.compareNatural(b.fileName); break;
            case 2: result = a.version.compareNatural(b.version); break;
            case 3: result = a.groupName.compareNatural(b.groupName); break;
            case 4: result = (a.fileSize < b.fileSize) ? -1 : (a.fileSize > b.fileSize) ? 1 : 0; break;
            case 5: result = a.isZipped ? -1 : 1; break;
            case 6: result = (a.lastModified < b.lastModified) ? -1 : (a.lastModified > b.lastModified) ? 1 : 0; break;
            default: result = 0;
            }
            return isForwards ? (result < 0) : (result > 0);
        });

    parent.updateResultTable();
}

// ============================================================================
// Editor Implementation
// ============================================================================

FLProjectOrganizerEditor::FLProjectOrganizerEditor(FLProjectOrganizerProcessor& p)
    : AudioProcessorEditor(&p), processorRef(p)  // Removed progressBar from initializer
{
    lnf = std::make_unique<FLPLookAndFeel>();
    setLookAndFeel(lnf.get());
    setSize(1100, 800);

    // Title
    addAndMakeVisible(titleLabel);
    titleLabel.setText("FL Studio Project Organizer", juce::dontSendNotification);
    titleLabel.setFont(juce::Font(juce::FontOptions(28.0f, juce::Font::bold)));
    titleLabel.setColour(juce::Label::textColourId, juce::Colours::white);

    addAndMakeVisible(versionLabel);
    versionLabel.setText("v1.0.0", juce::dontSendNotification);
    versionLabel.setFont(juce::Font(juce::FontOptions(14.0f)));
    versionLabel.setColour(juce::Label::textColourId, juce::Colours::orange);

    // Source path
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

    // Destination path
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

    // Options group
    addAndMakeVisible(optionsGroup);
    optionsGroup.setText("Options");
    optionsGroup.setColour(juce::GroupComponent::textColourId, juce::Colours::lightgrey);

    addAndMakeVisible(includeZipsToggle);
    includeZipsToggle.setButtonText("Include ZIP Archives");
    includeZipsToggle.setToggleState(true, juce::dontSendNotification);
    includeZipsToggle.setColour(juce::ToggleButton::textColourId, juce::Colours::lightgrey);

    addAndMakeVisible(scanSubfoldersToggle);
    scanSubfoldersToggle.setButtonText("Scan Subfolders");
    scanSubfoldersToggle.setToggleState(true, juce::dontSendNotification);
    scanSubfoldersToggle.setColour(juce::ToggleButton::textColourId, juce::Colours::lightgrey);

    addAndMakeVisible(dryRunToggle);
    dryRunToggle.setButtonText("Dry Run (Preview Only)");
    dryRunToggle.setToggleState(true, juce::dontSendNotification);
    dryRunToggle.setColour(juce::ToggleButton::textColourId, juce::Colours::lightgrey);

    addAndMakeVisible(deleteOriginalsToggle);
    deleteOriginalsToggle.setButtonText("Delete Originals After Copy");
    deleteOriginalsToggle.setToggleState(false, juce::dontSendNotification);
    deleteOriginalsToggle.setColour(juce::ToggleButton::textColourId, juce::Colours::lightgrey);

    addAndMakeVisible(verifyCopiesToggle);
    verifyCopiesToggle.setButtonText("Verify Copies (Hash Check)");
    verifyCopiesToggle.setToggleState(true, juce::dontSendNotification);
    verifyCopiesToggle.setColour(juce::ToggleButton::textColourId, juce::Colours::lightgrey);

    // Action buttons
    addAndMakeVisible(scanBtn);
    scanBtn.setButtonText("Scan");
    scanBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF4488FF));
    scanBtn.onClick = [this] { startScan(); };

    addAndMakeVisible(organizeBtn);
    organizeBtn.setButtonText("Organize");
    organizeBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFFFF8800));
    organizeBtn.onClick = [this] { startOrganize(); };

    addAndMakeVisible(stopBtn);
    stopBtn.setButtonText("Stop");
    stopBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFFFF4444));
    stopBtn.onClick = [this] { stopOperation(); };
    stopBtn.setEnabled(false);

    addAndMakeVisible(exportCSVBtn);
    exportCSVBtn.setButtonText("Export CSV");
    exportCSVBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF44AA44));
    exportCSVBtn.onClick = [this] { exportCSV(); };

    addAndMakeVisible(undoBtn);
    undoBtn.setButtonText("Undo Last");
    undoBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFFAA44FF));
    undoBtn.onClick = [this] { undoLastOperation(); };

    // Status and progress
    addAndMakeVisible(statusLabel);
    statusLabel.setText("Ready", juce::dontSendNotification);
    statusLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);

    addAndMakeVisible(progressBar);

    // Results table
    addAndMakeVisible(resultTable);
    tableModel = std::make_unique<ResultTableModel>(*this);
    resultTable.setModel(tableModel.get());
    resultTable.getHeader().addColumn("Project", 1, 200);
    resultTable.getHeader().addColumn("Version", 2, 150);
    resultTable.getHeader().addColumn("Group", 3, 100);
    resultTable.getHeader().addColumn("Size", 4, 100);
    resultTable.getHeader().addColumn("Type", 5, 80);
    resultTable.getHeader().addColumn("Modified", 6, 180);
    resultTable.getHeader().setSortColumnId(1, true);

    // Log box
    addAndMakeVisible(logBox);
    logBox.setMultiLine(true);
    logBox.setReadOnly(true);
    logBox.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xFF111111));
    logBox.setColour(juce::TextEditor::textColourId, juce::Colours::lightgrey);
    logBox.setFont(juce::Font(juce::FontOptions(12.0f)));

    // Initialize database
    auto dbFile = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("FLProjectOrganizer/projects.db");
    dbFile.getParentDirectory().createDirectory();

    dbInitialized = database.Initialize(dbFile);
    if (dbInitialized)
        sendLog("Database initialized: " + dbFile.getFullPathName());
    else
        sendLog("Failed to initialize database!");

    // Set default paths
    auto docsPath = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
        .getChildFile("Image-Line/FL Studio/Projects");

    if (docsPath.exists())
    {
        sourcePathEditor.setText(docsPath.getFullPathName());
        destPathEditor.setText(docsPath.getFullPathName());
    }

    // Load existing projects from database
    refreshDatabaseView();

    startTimerHz(30);
}

FLProjectOrganizerEditor::~FLProjectOrganizerEditor()
{
    setLookAndFeel(nullptr);
    if (scanner)
        scanner->StopScanning();
}

void FLProjectOrganizerEditor::paint(juce::Graphics& g)
{
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
}

void FLProjectOrganizerEditor::resized()
{
    auto bounds = getLocalBounds().reduced(20);

    // Title
    auto header = bounds.removeFromTop(50);
    titleLabel.setBounds(header.removeFromLeft(350));
    versionLabel.setBounds(header.removeFromLeft(80).withY(header.getY() + 10));

    bounds.removeFromTop(10);

    // Source/Destination
    auto sourceRow = bounds.removeFromTop(30);
    sourceLabel.setBounds(sourceRow.removeFromLeft(120));
    sourcePathEditor.setBounds(sourceRow.removeFromLeft(sourceRow.getWidth() - 100));
    browseSourceBtn.setBounds(sourceRow.removeFromRight(90));

    bounds.removeFromTop(5);

    auto destRow = bounds.removeFromTop(30);
    destLabel.setBounds(destRow.removeFromLeft(120));
    destPathEditor.setBounds(destRow.removeFromLeft(destRow.getWidth() - 100));
    browseDestBtn.setBounds(destRow.removeFromRight(90));

    bounds.removeFromTop(10);

    // Options
    auto optionsRow = bounds.removeFromTop(80);
    optionsGroup.setBounds(optionsRow);
    optionsRow = optionsRow.reduced(10, 20);

    auto toggleWidth = 160;
    includeZipsToggle.setBounds(optionsRow.removeFromLeft(toggleWidth));
    optionsRow.removeFromLeft(10);
    scanSubfoldersToggle.setBounds(optionsRow.removeFromLeft(toggleWidth));
    optionsRow.removeFromLeft(10);
    dryRunToggle.setBounds(optionsRow.removeFromLeft(toggleWidth));
    optionsRow.removeFromLeft(10);
    deleteOriginalsToggle.setBounds(optionsRow.removeFromLeft(toggleWidth));
    optionsRow.removeFromLeft(10);
    verifyCopiesToggle.setBounds(optionsRow.removeFromLeft(toggleWidth));

    bounds.removeFromTop(10);

    // Action buttons
    auto buttonRow = bounds.removeFromTop(40);
    scanBtn.setBounds(buttonRow.removeFromLeft(100));
    buttonRow.removeFromLeft(10);
    organizeBtn.setBounds(buttonRow.removeFromLeft(100));
    buttonRow.removeFromLeft(10);
    stopBtn.setBounds(buttonRow.removeFromLeft(80));
    buttonRow.removeFromLeft(20);
    exportCSVBtn.setBounds(buttonRow.removeFromLeft(100));
    buttonRow.removeFromLeft(10);
    undoBtn.setBounds(buttonRow.removeFromLeft(100));
    buttonRow.removeFromLeft(50);
    statusLabel.setBounds(buttonRow.removeFromLeft(200));
    progressBar.setBounds(buttonRow.removeFromLeft(buttonRow.getWidth()));

    bounds.removeFromTop(10);

    // Results table and log
    auto split = bounds;
    auto tableArea = split.removeFromTop(split.getHeight() * 0.6f);
    resultTable.setBounds(tableArea);

    split.removeFromTop(5);
    logBox.setBounds(split);
}

void FLProjectOrganizerEditor::timerCallback()
{
    // Process pending log messages
    juce::ScopedLock sl(logLock);
    if (!pendingLogs.isEmpty())
    {
        for (const auto& msg : pendingLogs)
        {
            logBox.moveCaretToEnd();
            logBox.insertTextAtCaret(msg + "\n");
        }
        pendingLogs.clear();
        logBox.moveCaretToEnd();
    }

    // Progress bar updates automatically via currentProgress reference
}

// ============================================================================
// Browse Source (Async & Safe - JUCE 8 Compliant)
// ============================================================================
void FLProjectOrganizerEditor::browseSource()
{
    // Initialize the unique pointer so it stays alive during the async dialog
    sourceChooser = std::make_unique<juce::FileChooser>("Select Source Folder", juce::File(), "*");

    int flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories;

    sourceChooser->launchAsync(flags, [this](const juce::FileChooser& chooser)
        {
            auto result = chooser.getResult();
            if (result.isDirectory())
            {
                sourcePathEditor.setText(result.getFullPathName());
                refreshDatabaseView();
            }
        });
}

// ============================================================================
// Browse Destination (Async & Safe - JUCE 8 Compliant)
// ============================================================================
void FLProjectOrganizerEditor::browseDest()
{
    destChooser = std::make_unique<juce::FileChooser>("Select Destination Folder", juce::File(), "*");

    int flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories;

    destChooser->launchAsync(flags, [this](const juce::FileChooser& chooser)
        {
            auto result = chooser.getResult();
            if (result.isDirectory())
            {
                destPathEditor.setText(result.getFullPathName());
            }
        });
}

// ============================================================================
// Export CSV (Async, Safe, and properly closed - JUCE 8 Compliant)
// ============================================================================
void FLProjectOrganizerEditor::exportCSV()
{
    auto projects = database.GetAllProjects();
    if (projects.empty())
    {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon,
            "Export", "No projects to export.");
        return;
    }

    auto defaultFile = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
        .getChildFile("fl_projects_export.csv");

    csvChooser = std::make_unique<juce::FileChooser>("Save CSV File", defaultFile, "*.csv");

    // Flags for saving: saveMode, select files, and warn if overwriting
    int flags = juce::FileBrowserComponent::saveMode
        | juce::FileBrowserComponent::canSelectFiles
        | juce::FileBrowserComponent::warnAboutOverwriting;

    csvChooser->launchAsync(flags, [this, projects](const juce::FileChooser& chooser)
        {
            auto file = chooser.getResult();

            // If file is empty, the user clicked "Cancel"
            if (file == juce::File())
                return;

            // Delete existing file if it exists (the warning handled the prompt)
            if (file.existsAsFile())
                file.deleteFile();

            juce::FileOutputStream fos(file);

            if (fos.openedOk())
            {
                // Write CSV Header - JUCE 8.0.12 requires 4 parameters
                fos.writeText("Project Name,Version,Group,Size (Bytes),Type,Modified,Hash\n", false, false, "\n");

                // Write CSV Data
                for (const auto& entry : projects)
                {
                    // Escape quotes in names for CSV safety
                    juce::String name = entry.fileName.replace("\"", "\"\"");
                    juce::String version = entry.version.replace("\"", "\"\"");
                    juce::String group = entry.groupName.replace("\"", "\"\"");
                    juce::String hash = entry.hash.replace("\"", "\"\"");

                    juce::String line = "\"" + name + "\",\"" + version + "\",\"" + group + "\","
                        + juce::String(entry.fileSize) + ","
                        + (entry.isZipped ? "\"ZIP\"" : "\"FLP\"") + ","
                        + "\"" + juce::Time(entry.lastModified).toString(true, true) + "\","
                        + "\"" + hash + "\"\n";

                    fos.writeText(line, false, false, "\n");
                }

                fos.flush();

                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon,
                    "Export Complete",
                    "Successfully exported " + juce::String(projects.size()) + " projects to:\n" + file.getFullPathName());
            }
            else
            {
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                    "Error", "Could not save file. Check folder permissions.");
            }
        });
}

// ============================================================================
// Start Scan
// ============================================================================
void FLProjectOrganizerEditor::startScan()
{
    juce::File source(sourcePathEditor.getText());
    if (!source.isDirectory())
    {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
            "Error", "Please select a valid source folder.");
        return;
    }

    scanner = std::make_unique<FLPScanner>();

    scanner->onLog = [this](const juce::String& msg) { sendLog(msg); };
    scanner->onProgress = [this](float p) { currentProgress = (double)p; };
    scanner->onProjectFound = [this](const ProjectDatabase::ProjectEntry& entry)
        {
            juce::MessageManager::callAsync([this] { refreshDatabaseView(); });
        };
    scanner->onDuplicateFound = [this](const juce::String& file1, const juce::String& file2)
        {
            sendLog("DUPLICATE: " + file1 + " == " + file2);
        };

    scanner->Scan(source, juce::File(destPathEditor.getText()),
        includeZipsToggle.getToggleState(),
        scanSubfoldersToggle.getToggleState(),
        dryRunToggle.getToggleState());

    updateUIState();
}

// ============================================================================
// Start Organize
// ============================================================================
void FLProjectOrganizerEditor::startOrganize()
{
    if (scanner && scanner->isThreadRunning())
        return;

    juce::File source(sourcePathEditor.getText());
    juce::File dest(destPathEditor.getText());

    if (!source.isDirectory())
    {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
            "Error", "Please select a valid source folder.");
        return;
    }

    if (!dest.isDirectory())
    {
        juce::AlertWindow::showAsync(
            juce::MessageBoxOptions()
                .withIconType(juce::MessageBoxIconType::QuestionIcon)
                .withTitle("Create Destination?")
                .withMessage("Destination folder doesn't exist. Create it?")
                .withButton("Create")
                .withButton("Cancel"),
            [this, source, dest](int result) mutable
            {
                if (result == 0)  // 0 = Cancel (second button)
                    return;
                dest.createDirectory();

                scanner = std::make_unique<FLPScanner>();
                scanner->onLog = [this](const juce::String& msg) { sendLog(msg); };
                scanner->onProgress = [this](float p) { currentProgress = (double)p; };
                scanner->onProjectFound = [this](const ProjectDatabase::ProjectEntry&)
                {
                    juce::MessageManager::callAsync([this] { refreshDatabaseView(); });
                };
                scanner->Scan(source, dest,
                    includeZipsToggle.getToggleState(),
                    scanSubfoldersToggle.getToggleState(),
                    dryRunToggle.getToggleState());
                updateUIState();
            });
        return;
    }

    auto launchOrganize = [this, source, dest]()
    {
        scanner = std::make_unique<FLPScanner>();
        scanner->onLog = [this](const juce::String& msg) { sendLog(msg); };
        scanner->onProgress = [this](float p) { currentProgress = (double)p; };
        scanner->onProjectFound = [this](const ProjectDatabase::ProjectEntry&)
        {
            juce::MessageManager::callAsync([this] { refreshDatabaseView(); });
        };
        scanner->Scan(source, dest,
            includeZipsToggle.getToggleState(),
            scanSubfoldersToggle.getToggleState(),
            dryRunToggle.getToggleState());
        updateUIState();
    };

    if (deleteOriginalsToggle.getToggleState())
    {
        juce::AlertWindow::showAsync(
            juce::MessageBoxOptions()
                .withIconType(juce::MessageBoxIconType::WarningIcon)
                .withTitle("Confirm Delete")
                .withMessage("WARNING: You are about to delete original files after copying.\n"
                             "This operation cannot be undone.\n\nContinue?")
                .withButton("Delete")
                .withButton("Cancel"),
            [launchOrganize](int result)
            {
                if (result == 1)  // 1 = Delete (first button)
                    launchOrganize();
            });
        return;
    }

    launchOrganize();
}

// ============================================================================
// Stop Operation
// ============================================================================
void FLProjectOrganizerEditor::stopOperation()
{
    if (scanner)
        scanner->StopScanning();

    currentProgress = 0;
    updateUIState();
    sendLog("Operation stopped by user.");
}

// ============================================================================
// Update UI State
// ============================================================================
void FLProjectOrganizerEditor::updateUIState()
{
    bool isRunning = scanner && scanner->isThreadRunning();
    isScanning = isRunning;

    scanBtn.setEnabled(!isRunning);
    organizeBtn.setEnabled(!isRunning);
    stopBtn.setEnabled(isRunning);
    browseSourceBtn.setEnabled(!isRunning);
    browseDestBtn.setEnabled(!isRunning);

    if (isRunning)
    {
        scanBtn.setButtonText("Scanning...");
        organizeBtn.setButtonText("Organizing...");
        statusLabel.setText("Working...", juce::dontSendNotification);
    }
    else
    {
        scanBtn.setButtonText("Scan");
        organizeBtn.setButtonText("Organize");
        statusLabel.setText("Ready", juce::dontSendNotification);
    }
}

// ============================================================================
// Refresh Database View
// ============================================================================
void FLProjectOrganizerEditor::refreshDatabaseView()
{
    auto projects = database.GetAllProjects();

    // Update the table model
    if (tableModel)
    {
        tableModel->displayRows = projects;
        resultTable.updateContent();
    }

    statusLabel.setText(juce::String(projects.size()) + " projects in database", juce::dontSendNotification);
}

// ============================================================================
// Update Result Table
// ============================================================================
void FLProjectOrganizerEditor::updateResultTable()
{
    resultTable.updateContent();
}

// ============================================================================
// Send Log Message
// ============================================================================
void FLProjectOrganizerEditor::sendLog(const juce::String& msg)
{
    juce::ScopedLock sl(logLock);
    pendingLogs.add("[" + juce::Time::getCurrentTime().toString(true, true) + "] " + msg);
}

// ============================================================================
// Undo Last Operation
// ============================================================================
void FLProjectOrganizerEditor::undoLastOperation()
{
    auto transactions = database.GetTransactions();
    if (transactions.empty())
    {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon,
            "Undo", "No operations to undo.");
        return;
    }

    auto& last = transactions.back();
    if (database.UndoTransaction(last.id))
    {
        sendLog("Undid: " + last.action + " (" + last.sourcePath + ")");
        refreshDatabaseView();
    }
    else
    {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
            "Undo Failed", "Could not undo the last operation.");
    }
}