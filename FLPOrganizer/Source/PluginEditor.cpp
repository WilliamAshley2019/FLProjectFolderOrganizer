#include "PluginEditor.h"
#include "SampleUsageIndexer.h"
#include "InstalledPluginLookup.h"
#include "RecycleBinManager.h"
#include "ContentRoutingWindow.h"

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
    if (rowIsSelected)
    {
        g.setColour(flo::ui::col::accent.withAlpha(0.20f));
        g.fillRect(0, 0, width, height);
        g.setColour(flo::ui::col::accent);
        g.fillRect(0, 2, 3, height - 4);
    }
    else if (rowNumber % 2)
    {
        g.setColour(juce::Colours::white.withAlpha(0.025f));
        g.fillRect(0, 0, width, height);
    }
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

    g.setColour(rowIsSelected ? flo::ui::col::text : flo::ui::col::textDim.brighter(0.25f));
    g.setFont(flo::ui::sans(13.0f));
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

void FLProjectOrganizerEditor::ResultTableModel::cellClicked(
    int rowNumber, int /*columnId*/, const juce::MouseEvent& event)
{
    if (event.mods.isPopupMenu() && rowNumber >= 0 && rowNumber < (int)displayRows.size())
        parent.showRowContextMenu(rowNumber, event.getScreenPosition());
}

// ============================================================================
// Editor Implementation
// ============================================================================

FLProjectOrganizerEditor::FLProjectOrganizerEditor(FLProjectOrganizerProcessor& p)
    : AudioProcessorEditor(&p), processorRef(p)
{
    lnf = std::make_unique<FLPLookAndFeel>();
    setLookAndFeel(lnf.get());
    addAndMakeVisible(chassis);          // brushed-metal backdrop, behind every other child
    addAndMakeVisible(lcd);
    setSize(1100, 800);

    // Title
    addAndMakeVisible(titleLabel);
    titleLabel.setText("FL Project Organizer", juce::dontSendNotification);
    titleLabel.setText("FL PROJECT ORGANIZER", juce::dontSendNotification);
    titleLabel.setFont(flo::ui::caps(22.0f));
    titleLabel.setColour(juce::Label::textColourId, flo::ui::col::text);

    addAndMakeVisible(versionLabel);
    versionLabel.setText("v1.0.4", juce::dontSendNotification);
    versionLabel.setFont(flo::ui::caps(11.0f));
    versionLabel.setColour(juce::Label::textColourId, flo::ui::col::accent);

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

    addAndMakeVisible(pluginDbBtn);
    pluginDbBtn.setButtonText("Plugin Database");
    pluginDbBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFFFF5C00)); // phosphor orange
    // PluginDatabaseWindow deletes itself in closeButtonPressed() -- standard
    // JUCE idiom for a non-modal, self-owned top-level window launched from
    // a button click.
    pluginDbBtn.onClick = [this] { new PluginDatabaseWindow(); };

    addAndMakeVisible(protectedFilesBtn);
    protectedFilesBtn.setButtonText("Protected Files");
    protectedFilesBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFFFF4444));
    protectedFilesBtn.onClick = [this]
    {
        auto* win = new ProtectedFilesWindow();
        win->SetEntries(protectedFilesFound);
    };

    addAndMakeVisible(sampleScannerBtn);
    sampleScannerBtn.setButtonText("Sample Scanner");
    sampleScannerBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFFFF5C00));
    sampleScannerBtn.onClick = [this] { new SampleScannerWindow(); };

    addAndMakeVisible(indexSamplesBtn);
    indexSamplesBtn.setButtonText("Index Samples");
    indexSamplesBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFFFF5C00));
    indexSamplesBtn.onClick = [this] { startSampleIndexing(); };

    addAndMakeVisible(findSampleUsersBtn);
    findSampleUsersBtn.setButtonText("Find Sample Users...");
    findSampleUsersBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFFFF5C00));
    findSampleUsersBtn.onClick = [this] { promptFindSampleUsers(); };

    addAndMakeVisible(importContentBtn);
    importContentBtn.setButtonText("Import Content...");
    importContentBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF2BFF6B));
    importContentBtn.onClick = [this] { new ContentRoutingWindow(); };   // deletes itself on close

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
    // Stored under Documents\WAM\FLProjectOrganizer\ -- same root the
    // RecycleBinManager and FLPScanner use, since this is third-party
    // tooling rather than Image-Line's own FL Studio data tree.
    auto dbFile = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
        .getChildFile("WAM")
        .getChildFile("FLProjectOrganizer")
        .getChildFile("projects.db");
    dbFile.getParentDirectory().createDirectory();

    databaseFile = dbFile;
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

    // Phase 3: discover installed FL Studio versions for the right-click
    // "Open With" / "Open Install Folder" context menu.
    refreshInstalledVersions();

    // Parse FL's plugin database once, off the message thread, so the first
    // project inspection doesn't stall.
    InstalledPluginLookup::getInstance().warmUp();

    startTimerHz(30);
}

FLProjectOrganizerEditor::~FLProjectOrganizerEditor()
{
    if (sampleIndexThread != nullptr)
        sampleIndexThread->stopThread(10000);
    setLookAndFeel(nullptr);
    if (scanner)
        scanner->StopScanning();
}

void FLProjectOrganizerEditor::paint(juce::Graphics& g)
{
    g.fillAll(flo::ui::col::bg);
}

void FLProjectOrganizerEditor::resized()
{
    chassis.setBounds(getLocalBounds());
    auto bounds = getLocalBounds().reduced(20);

    // Header: engraved title on the left, LCD on the right
    auto header = bounds.removeFromTop(62);
    lcd.setBounds(header.removeFromRight(juce::jmin(460, header.getWidth() / 2)));
    header.removeFromRight(16);
    titleLabel.setBounds(header.removeFromTop(36));
    versionLabel.setBounds(header.removeFromTop(18));

    bounds.removeFromTop(10);

    // Source / destination plate
    auto pathPlate = bounds.removeFromTop(30 + 5 + 30 + 20);
    auto pathArea = pathPlate.reduced(12, 10);
    auto sourceRow = pathArea.removeFromTop(30);
    sourceLabel.setBounds(sourceRow.removeFromLeft(130));
    browseSourceBtn.setBounds(sourceRow.removeFromRight(90));
    sourceRow.removeFromRight(8);
    sourcePathEditor.setBounds(sourceRow);
    pathArea.removeFromTop(5);
    auto destRow = pathArea.removeFromTop(30);
    destLabel.setBounds(destRow.removeFromLeft(130));
    browseDestBtn.setBounds(destRow.removeFromRight(90));
    destRow.removeFromRight(8);
    destPathEditor.setBounds(destRow);

    bounds.removeFromTop(10);

    // Options
    auto optionsRow = bounds.removeFromTop(80);
    optionsGroup.setBounds(optionsRow);
    optionsRow = optionsRow.reduced(10, 24);

    auto toggleWidth = 160;
    includeZipsToggle.setBounds(optionsRow.removeFromLeft(toggleWidth));
    optionsRow.removeFromLeft(10);
    scanSubfoldersToggle.setBounds(optionsRow.removeFromLeft(toggleWidth));
    optionsRow.removeFromLeft(10);
    dryRunToggle.setBounds(optionsRow.removeFromLeft(toggleWidth));
    optionsRow.removeFromLeft(10);
    deleteOriginalsToggle.setBounds(optionsRow.removeFromLeft(toggleWidth + 20));
    optionsRow.removeFromLeft(10);
    verifyCopiesToggle.setBounds(optionsRow.removeFromLeft(toggleWidth + 20));

    bounds.removeFromTop(10);

    // Action row on one plate
    auto actionPlate = bounds.removeFromTop(40 + 8 + 36 + 20);
    auto actionArea = actionPlate.reduced(12, 10);
    auto buttonRow = actionArea.removeFromTop(40);
    scanBtn.setBounds(buttonRow.removeFromLeft(100));
    buttonRow.removeFromLeft(10);
    organizeBtn.setBounds(buttonRow.removeFromLeft(100));
    buttonRow.removeFromLeft(10);
    stopBtn.setBounds(buttonRow.removeFromLeft(80));
    buttonRow.removeFromLeft(20);
    exportCSVBtn.setBounds(buttonRow.removeFromLeft(100));
    buttonRow.removeFromLeft(10);
    undoBtn.setBounds(buttonRow.removeFromLeft(100));
    buttonRow.removeFromLeft(20);
    statusLabel.setBounds(buttonRow.removeFromLeft(200));
    progressBar.setBounds(buttonRow.reduced(0, 6));

    actionArea.removeFromTop(8);

    // Tool row
    auto toolRow = actionArea.removeFromTop(36);
    pluginDbBtn.setBounds(toolRow.removeFromLeft(130));
    toolRow.removeFromLeft(8);
    protectedFilesBtn.setBounds(toolRow.removeFromLeft(150));
    toolRow.removeFromLeft(8);
    sampleScannerBtn.setBounds(toolRow.removeFromLeft(140));
    toolRow.removeFromLeft(8);
    indexSamplesBtn.setBounds(toolRow.removeFromLeft(120));
    toolRow.removeFromLeft(8);
    findSampleUsersBtn.setBounds(toolRow.removeFromLeft(160));
    toolRow.removeFromLeft(8);
    importContentBtn.setBounds(toolRow.removeFromLeft(150));

    bounds.removeFromTop(10);

    // Results table and log, each in a glass well
    auto split = bounds;
    auto tableArea = split.removeFromTop((int)(split.getHeight() * 0.6f));
    resultTable.setBounds(tableArea.reduced(2));

    split.removeFromTop(8);
    logBox.setBounds(split.reduced(2));

    chassis.plates = { pathPlate.toFloat(), actionPlate.toFloat() };
    chassis.wells = { tableArea.toFloat(), split.toFloat() };
    chassis.headers.clear();
    chassis.repaint();
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
    scanner->onProtectedFileFound = [this](const FLPScanner::ProtectedFileEntry& e)
        {
            juce::MessageManager::callAsync([this, e]
            {
                protectedFilesFound.push_back(e);
                protectedFilesBtn.setButtonText("Protected Files (" +
                    juce::String((int)protectedFilesFound.size()) + ")");
            });
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
                scanner->onProtectedFileFound = [this](const FLPScanner::ProtectedFileEntry& e)
                {
                    juce::MessageManager::callAsync([this, e]
                    {
                        protectedFilesFound.push_back(e);
                        protectedFilesBtn.setButtonText("Protected Files (" +
                            juce::String((int)protectedFilesFound.size()) + ")");
                    });
                };
                scanner->Scan(source, dest,
                    includeZipsToggle.getToggleState(),
                    scanSubfoldersToggle.getToggleState(),
                    dryRunToggle.getToggleState(),
                    deleteOriginalsToggle.getToggleState());
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
        scanner->onProtectedFileFound = [this](const FLPScanner::ProtectedFileEntry& e)
        {
            juce::MessageManager::callAsync([this, e]
            {
                protectedFilesFound.push_back(e);
                protectedFilesBtn.setButtonText("Protected Files (" +
                    juce::String((int)protectedFilesFound.size()) + ")");
            });
        };
        scanner->Scan(source, dest,
            includeZipsToggle.getToggleState(),
            scanSubfoldersToggle.getToggleState(),
            dryRunToggle.getToggleState(),
            deleteOriginalsToggle.getToggleState());
        updateUIState();
    };

    if (deleteOriginalsToggle.getToggleState())
    {
        // First confirmation: plain warning.
        juce::AlertWindow::showAsync(
            juce::MessageBoxOptions()
                .withIconType(juce::MessageBoxIconType::WarningIcon)
                .withTitle("Confirm Delete")
                .withMessage("WARNING: You are about to move original files to the\n"
                             "FL Organizer recycle bin after copying.\n\n"
                             "Files are NOT permanently deleted -- they go to:\n" +
                             RecycleBinManager().GetRecycleBinRoot().getFullPathName() +
                             "\n\nContinue?")
                .withButton("Continue")
                .withButton("Cancel"),
            [this, launchOrganize](int result)
            {
                if (result != 1)  // 1 = Continue (first button)
                    return;

                // Second confirmation: type-to-confirm, the strongest guard
                // against an accidental click wiping out a project folder.
                auto* typeConfirmWindow = new juce::AlertWindow(
                    "Final Confirmation Required",
                    "Type DELETE below to confirm moving originals to the recycle bin.\n"
                    "This is your last chance to back out.",
                    juce::MessageBoxIconType::WarningIcon);

                typeConfirmWindow->addTextEditor("confirmText", "", "Type DELETE here:");
                typeConfirmWindow->addButton("Confirm", 1, juce::KeyPress(juce::KeyPress::returnKey));
                typeConfirmWindow->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

                typeConfirmWindow->enterModalState(true,
                    juce::ModalCallbackFunction::create(
                        [typeConfirmWindow, launchOrganize](int result2)
                        {
                            auto typed = typeConfirmWindow->getTextEditorContents("confirmText");
                            if (result2 == 1 && typed.trim().equalsIgnoreCase("DELETE"))
                                launchOrganize();
                            // typeConfirmWindow is owned by the modal manager and
                            // cleaned up automatically after the callback runs.
                        }),
                    true);
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

    int zipped = 0;
    for (auto& p : projects) zipped += p.isZipped ? 1 : 0;
    updateLcd((int)projects.size(), zipped);
}

void FLProjectOrganizerEditor::updateLcd(int projectCount, int zippedCount)
{
    lcd.set({ { "PROJECTS", juce::String(juce::jmin(projectCount, 99999)).paddedLeft('0', 4), 1.4f },
              { "IN ZIPS", juce::String(juce::jmin(zippedCount, 9999)).paddedLeft('0', 3), 1.0f },
              { "FL INSTALLS", juce::String(juce::jmin((int)installedVersions.size(), 99)).paddedLeft('0', 2), 1.0f } });
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

    auto& last = transactions.front();   // GetTransactions() is newest-first
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

// ============================================================================
// Refresh Installed FL Studio Versions (Phase 3)
// ============================================================================
void FLProjectOrganizerEditor::refreshInstalledVersions()
{
    installedVersions = installScanner.ScanForInstallations();
    updateLcd((int)tableModel->displayRows.size(), 0);

    if (installedVersions.empty())
    {
        sendLog("No FL Studio installations found on this system.");
        return;
    }

    sendLog("Found " + juce::String((int)installedVersions.size()) + " FL Studio installation(s):");
    for (const auto& v : installedVersions)
        sendLog("  - " + v.versionLabel + " (major v" + juce::String(v.majorVersion) + ") at " +
            v.installFolder.getFullPathName() + " [exe: " + v.executablePath.getFileName() + "]" +
            (v.registryConfirmed ? " [registry confirmed]" : " [not registry confirmed]"));
}

// ============================================================================
// Right-Click Context Menu for Result Table Rows (Phase 3)
// ============================================================================
void FLProjectOrganizerEditor::showRowContextMenu(int rowNumber, juce::Point<int> screenPosition)
{
    if (rowNumber < 0 || rowNumber >= (int)tableModel->displayRows.size())
        return;

    const auto& entry = tableModel->displayRows[(size_t)rowNumber];
    juce::File projectFile(entry.path);

    juce::PopupMenu menu;

    // Non-actionable info rows -- shows exactly where this file lives
    // and its hash, which isn't shown as a table column but is computed
    // and stored for every scanned project.
    menu.addItem(97, "Path: " + entry.path, false);
    if (entry.hash.isNotEmpty())
        menu.addItem(96, "SHA-256: " + entry.hash.substring(0, 16) + "...", false);
    menu.addSeparator();

    menu.addItem(1, "Open Containing Folder");

    // Best-match FL Studio version, if any installs were found
    const FLInstallationScanner::Installation* bestMatch = nullptr;
    if (!installedVersions.empty())
    {
        int projectMajor = entry.groupName.startsWith("FL")
            ? entry.groupName.substring(2).getIntValue()
            : entry.version.upToFirstOccurrenceOf(".", false, false).getIntValue();

        bestMatch = FLInstallationScanner::FindBestMatch(installedVersions, projectMajor);

        if (bestMatch != nullptr)
            menu.addItem(2, "Open with " + bestMatch->versionLabel +
                "  (" + bestMatch->installFolder.getFullPathName() + ")");

        if (installedVersions.size() > 1)
        {
            juce::PopupMenu otherVersions;
            int itemId = 100;
            for (const auto& v : installedVersions)
            {
                if (&v != bestMatch)
                    otherVersions.addItem(itemId++, v.versionLabel +
                        "  (" + v.installFolder.getFullPathName() + ")");
            }
            if (otherVersions.getNumItems() > 0)
                menu.addSubMenu("Open with...", otherVersions);
        }
    }
    else
    {
        menu.addItem(3, "No FL Studio installations found (click to rescan)");
    }

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(
        juce::Rectangle<int>(screenPosition, screenPosition)),
        [this, projectFile, bestMatch](int result)
        {
            if (result == 0)
                return;

            if (result == 1)
            {
                projectFile.getParentDirectory().startAsProcess();
            }
            else if (result == 2 && bestMatch != nullptr)
            {
                sendLog("Attempting to launch: " + bestMatch->executablePath.getFullPathName());
                if (!FLInstallationScanner::LaunchInstallation(*bestMatch, projectFile))
                    sendLog("Failed to launch " + bestMatch->versionLabel +
                        " -- executable not found or failed to start at: " +
                        bestMatch->executablePath.getFullPathName());
                else
                    sendLog("Launched " + bestMatch->versionLabel + " with " + projectFile.getFileName());
            }
            else if (result == 3)
            {
                refreshInstalledVersions();
            }
            else if (result >= 100)
            {
                int index = result - 100;
                int seen = 0;
                for (const auto& v : installedVersions)
                {
                    if (&v == bestMatch) continue;
                    if (seen == index)
                    {
                        sendLog("Attempting to launch: " + v.executablePath.getFullPathName());
                        if (!FLInstallationScanner::LaunchInstallation(v, projectFile))
                            sendLog("Failed to launch " + v.versionLabel +
                                " -- executable not found or failed to start at: " +
                                v.executablePath.getFullPathName());
                        else
                            sendLog("Launched " + v.versionLabel + " with " + projectFile.getFileName());
                        break;
                    }
                    ++seen;
                }
            }
        });
}

// ============================================================================
// Sample usage index
// ============================================================================
namespace
{
    // Runs on its own thread with its OWN sqlite connection to the same WAL
    // database, so it never interleaves transactions with the UI thread's handle.
    class SampleIndexThread : public juce::Thread
    {
    public:
        SampleIndexThread(const juce::File& dbFile, std::function<void(const juce::String&)> logFn,
                          std::function<void()> doneFn)
            : juce::Thread("SampleIndexThread"), file(dbFile), log(std::move(logFn)), done(std::move(doneFn)) {}

        void run() override
        {
            ProjectDatabase localDb;
            if (!localDb.Initialize(file)) { post("Sample index: could not open database."); return finish(); }
            SampleUsageIndexer indexer(localDb);
            int lastReported = -10;
            auto r = indexer.indexAll(false,
                [&](int i, int total, const juce::String& name)
                {
                    if (i - lastReported >= 25 || i == total) { lastReported = i; post("Indexing samples " + juce::String(i) + "/" + juce::String(total) + "  " + name); }
                },
                [this] { return threadShouldExit(); });
            post("Sample index done: " + juce::String(r.indexed) + " indexed (" + juce::String(r.references)
                 + " references), " + juce::String(r.skipped) + " skipped, " + juce::String(r.failed) + " failed.");
            finish();
        }
    private:
        void post(const juce::String& m) { auto l = log; juce::MessageManager::callAsync([l, m] { l(m); }); }
        void finish() { auto d = done; juce::MessageManager::callAsync([d] { d(); }); }
        juce::File file;
        std::function<void(const juce::String&)> log;
        std::function<void()> done;
    };
}

void FLProjectOrganizerEditor::startSampleIndexing()
{
    if (!dbInitialized) return;
    if (sampleIndexThread != nullptr)
    {
        if (sampleIndexThread->isThreadRunning()) return;
        sampleIndexThread.reset();
    }
    indexSamplesBtn.setEnabled(false);
    sendLog("Sample index started...");
    juce::Component::SafePointer<FLProjectOrganizerEditor> safe(this);
    sampleIndexThread.reset(new SampleIndexThread(databaseFile,
        [safe](const juce::String& m) { if (safe != nullptr) safe->sendLog(m); },
        [safe] { if (safe != nullptr) safe->indexSamplesBtn.setEnabled(true); }));
    sampleIndexThread->startThread();
}

void FLProjectOrganizerEditor::promptFindSampleUsers()
{
    if (!dbInitialized) return;
    auto* w = new juce::AlertWindow("Find Sample Users",
        "Enter part of a sample's file name or path (e.g. kick_07.wav):", juce::MessageBoxIconType::QuestionIcon);
    w->addTextEditor("needle", "", "Sample:");
    w->addButton("Search", 1, juce::KeyPress(juce::KeyPress::returnKey));
    w->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    juce::Component::SafePointer<FLProjectOrganizerEditor> safe(this);
    w->enterModalState(true, juce::ModalCallbackFunction::create([safe, w](int result)
    {
        auto needle = w->getTextEditorContents("needle").trim();
        std::unique_ptr<juce::AlertWindow> owner(w);     // modal state ended; we own the window now
        if (result != 1 || needle.isEmpty() || safe == nullptr) return;

        auto rows = safe->database.FindSampleUsers(needle, true);
        juce::String msg;
        if (rows.empty())
            msg = "No indexed project uses a sample matching \"" + needle + "\".\n\n"
                  "If projects were added recently, press Index Samples first.";
        else
        {
            juce::StringArray lines;
            for (int i = 0; i < juce::jmin((int)rows.size(), 40); ++i)
                lines.add(juce::File(rows[(size_t)i].projectPath).getFileName() + "  -  " + rows[(size_t)i].channelName);
            msg = juce::String((int)rows.size()) + " reference(s):\n\n" + lines.joinIntoString("\n");
            if (rows.size() > 40) msg += "\n... and " + juce::String((int)rows.size() - 40) + " more";
        }
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon, "Sample Users", msg);
    }), false);
}
