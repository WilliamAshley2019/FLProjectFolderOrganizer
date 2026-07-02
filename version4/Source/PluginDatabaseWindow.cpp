#include "PluginDatabaseWindow.h"

// ============================================================================
// TableModel
// ============================================================================

int PluginDatabaseComponent::TableModel::getNumRows()
{
    return (int)rows.size();
}

void PluginDatabaseComponent::TableModel::paintRowBackground(
    juce::Graphics& g, int rowNumber, int /*width*/, int /*height*/, bool rowIsSelected)
{
    auto bg = rowIsSelected ? juce::Colour(0xFF2A2A3A) :
        (rowNumber % 2 ? juce::Colour(0xFF222222) : juce::Colour(0xFF1A1A1A));
    g.fillAll(bg);
}

void PluginDatabaseComponent::TableModel::paintCell(
    juce::Graphics& g, int rowNumber, int columnId, int width, int height, bool rowIsSelected)
{
    if (rowNumber >= (int)rows.size())
        return;

    const auto& entry = rows[(size_t)rowNumber];
    juce::String text;

    switch (columnId)
    {
        case 1: text = entry.name; break;
        case 2: text = (entry.type == PluginDatabaseManager::PluginType::Generator) ? "Generator" : "Effect"; break;
        case 3: text = entry.source == PluginDatabaseManager::EntrySource::Installed ? "Installed" : "Favorites"; break;
        case 4: text = entry.GetPrimaryFormat(); break;
        case 5: text = entry.GetPrimaryVendor().isNotEmpty() ? entry.GetPrimaryVendor() : "--"; break;
        case 6: text = entry.GetPrimaryCategory().isNotEmpty() ? entry.GetPrimaryCategory() : "--"; break;
        default: break;
    }

    g.setColour(rowIsSelected ? juce::Colours::white : juce::Colours::lightgrey);
    g.setFont(juce::Font(juce::FontOptions(13.0f)));
    g.drawText(text, 4, 0, width - 8, height, juce::Justification::centredLeft);
}

void PluginDatabaseComponent::TableModel::cellClicked(
    int rowNumber, int /*columnId*/, const juce::MouseEvent& event)
{
    if (event.mods.isPopupMenu() && rowNumber >= 0 && rowNumber < (int)rows.size())
        owner.showRowContextMenu(rowNumber, event.getScreenPosition());
}

// ============================================================================
// PluginDatabaseComponent
// ============================================================================

PluginDatabaseComponent::PluginDatabaseComponent()
{
    setSize(1050, 650);

    addAndMakeVisible(titleLabel);
    titleLabel.setText("Plugin Database", juce::dontSendNotification);
    titleLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    titleLabel.setFont(juce::Font(juce::FontOptions(20.0f, juce::Font::bold)));

    addAndMakeVisible(statusLabel);
    statusLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);

    addAndMakeVisible(sourceFilterCombo);
    sourceFilterCombo.addItem("All", 1);
    sourceFilterCombo.addItem("Installed only", 2);
    sourceFilterCombo.addItem("Favorites only", 3);
    sourceFilterCombo.setSelectedId(1, juce::dontSendNotification);
    sourceFilterCombo.onChange = [this] { applySourceFilter(); };

    addAndMakeVisible(rescanBtn);
    rescanBtn.setButtonText("Rescan");
    rescanBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFFFF5C00)); // phosphor orange
    rescanBtn.onClick = [this] { refreshFromDisk(); };

    addAndMakeVisible(table);
    tableModel = std::make_unique<TableModel>(*this);
    table.setModel(tableModel.get());
    table.getHeader().addColumn("Plugin Name", 1, 220);
    table.getHeader().addColumn("Type", 2, 80);
    table.getHeader().addColumn("Source", 3, 90);
    table.getHeader().addColumn("Format", 4, 80);
    table.getHeader().addColumn("Vendor", 5, 180);
    table.getHeader().addColumn("Category", 6, 200);

    refreshFromDisk();
}

PluginDatabaseComponent::~PluginDatabaseComponent() = default;

void PluginDatabaseComponent::resized()
{
    auto bounds = getLocalBounds().reduced(15);

    auto header = bounds.removeFromTop(40);
    titleLabel.setBounds(header.removeFromLeft(300));
    rescanBtn.setBounds(header.removeFromRight(100));
    header.removeFromRight(10);
    sourceFilterCombo.setBounds(header.removeFromRight(150));

    bounds.removeFromTop(5);
    statusLabel.setBounds(bounds.removeFromTop(20));

    bounds.removeFromTop(10);
    table.setBounds(bounds);
}

void PluginDatabaseComponent::refreshFromDisk()
{
    auto root = PluginDatabaseManager::GetDefaultDatabaseRoot();

    if (!root.isDirectory())
    {
        statusLabel.setText("Plugin database folder not found at: " + root.getFullPathName(),
            juce::dontSendNotification);
        allRows.clear();
        tableModel->rows.clear();
        table.updateContent();
        return;
    }

    allRows = databaseManager.ScanDatabase(root);
    applySourceFilter();
}

void PluginDatabaseComponent::applySourceFilter()
{
    int selected = sourceFilterCombo.getSelectedId();

    tableModel->rows.clear();
    for (const auto& entry : allRows)
    {
        bool include = true;
        if (selected == 2)
            include = (entry.source == PluginDatabaseManager::EntrySource::Installed);
        else if (selected == 3)
            include = (entry.source == PluginDatabaseManager::EntrySource::Favorites);

        if (include)
            tableModel->rows.push_back(entry);
    }

    table.updateContent();

    int installedCount = 0, favoritesCount = 0;
    for (const auto& entry : allRows)
    {
        if (entry.source == PluginDatabaseManager::EntrySource::Installed) ++installedCount;
        else ++favoritesCount;
    }

    statusLabel.setText(juce::String((int)tableModel->rows.size()) + " shown ("
        + juce::String(installedCount) + " Installed, " + juce::String(favoritesCount) + " Favorites total)",
        juce::dontSendNotification);
}

void PluginDatabaseComponent::showRowContextMenu(int rowNumber, juce::Point<int> screenPosition)
{
    auto& entry = tableModel->rows[(size_t)rowNumber];
    bool isFavorites = (entry.source == PluginDatabaseManager::EntrySource::Favorites);

    juce::PopupMenu menu;

    // Non-actionable path display at the top, per request -- shows
    // exactly where this entry's .fst lives on disk.
    menu.addItem(99, ".fst: " + entry.fstFile.getFullPathName(), false);
    if (!entry.fileRecords.empty() && entry.fileRecords.front().filePathRaw.isNotEmpty())
        menu.addItem(98, "Plugin file: " + entry.fileRecords.front().filePathRaw, false);
    menu.addSeparator();

    menu.addItem(1, "Open Containing Folder");

    if (isFavorites)
    {
        menu.addItem(2, entry.type == PluginDatabaseManager::PluginType::Generator
            ? "Reclassify as Effect"
            : "Reclassify as Generator");
        menu.addItem(3, "View / Edit .nfo Text", entry.nfoFile.existsAsFile());
        menu.addSeparator();
        menu.addItem(4, "Delete (move to Recycle Bin)");
    }
    else
    {
        menu.addItem(6, "Edit Entry (vendor / category / type)");
        menu.addItem(3, "View .nfo Text", entry.nfoFile.existsAsFile());
    }

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(
        juce::Rectangle<int>(screenPosition, screenPosition)),
        [this, rowNumber](int result)
        {
            if (result == 0 || rowNumber < 0 || rowNumber >= (int)tableModel->rows.size())
                return;

            auto& entry = tableModel->rows[(size_t)rowNumber];

            if (result == 1)
            {
                entry.fstFile.getParentDirectory().startAsProcess();
            }
            else if (result == 2)
            {
                auto newType = (entry.type == PluginDatabaseManager::PluginType::Generator)
                    ? PluginDatabaseManager::PluginType::Effect
                    : PluginDatabaseManager::PluginType::Generator;

                if (databaseManager.ReclassifyEntry(entry, newType))
                {
                    statusLabel.setText("Reclassified " + entry.name, juce::dontSendNotification);
                    refreshFromDisk();
                }
                else
                {
                    juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                        "Reclassify Failed",
                        "Could not move all files for " + entry.name +
                        ". A file with the same name may already exist at the destination, "
                        "or a file may be locked. Rescanning to show current state.");
                    refreshFromDisk();
                }
            }
            else if (result == 3)
            {
                showNfoEditor(entry);
            }
            else if (result == 6)
            {
                showEntryEditor(entry);
            }
            else if (result == 4)
            {
                juce::AlertWindow::showAsync(
                    juce::MessageBoxOptions()
                        .withIconType(juce::MessageBoxIconType::WarningIcon)
                        .withTitle("Confirm Delete")
                        .withMessage("Move " + entry.name + " (.fst/.nfo/.png) to the recycle bin?")
                        .withButton("Move to Recycle Bin")
                        .withButton("Cancel"),
                    [this, entry](int confirmResult)
                    {
                        if (confirmResult != 1)
                            return;

                        if (databaseManager.DeleteEntry(entry, recycleBin))
                        {
                            statusLabel.setText("Deleted " + entry.name, juce::dontSendNotification);
                            refreshFromDisk();
                        }
                        else
                        {
                            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                "Delete Failed", "Could not move all files to the recycle bin.");
                        }
                    });
            }
        });
}

void PluginDatabaseComponent::showNfoEditor(const PluginDatabaseManager::PluginEntry& entry)
{
    bool readOnly = (entry.source == PluginDatabaseManager::EntrySource::Installed);
    new NfoEditorWindow(entry, readOnly);
}

// ============================================================================
// PluginDatabaseWindow
// ============================================================================

void PluginDatabaseComponent::showEntryEditor(const PluginDatabaseManager::PluginEntry& entry)
{
    new PluginEntryEditorWindow(databaseManager, entry, [this]
    {
        refreshFromDisk();
    });
}

// ============================================================================
// PluginEntryEditorComponent
// ============================================================================

PluginEntryEditorComponent::PluginEntryEditorComponent(PluginDatabaseManager& managerIn,
    PluginDatabaseManager::PluginEntry entryIn, std::function<void()> onSavedCallback)
    : manager(managerIn), entry(std::move(entryIn)), onSaved(std::move(onSavedCallback))
{
    setSize(560, 400);
    addAndMakeVisible(titleLabel);
    titleLabel.setText("Edit Plugin Database Entry", juce::dontSendNotification);
    titleLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    titleLabel.setFont(juce::Font(juce::FontOptions(18.0f, juce::Font::bold)));

    addAndMakeVisible(pathLabel);
    pathLabel.setText(entry.sourceIniFile.getFullPathName(), juce::dontSendNotification);
    pathLabel.setColour(juce::Label::textColourId, juce::Colours::grey);
    pathLabel.setFont(juce::Font(juce::FontOptions(11.0f)));

    addAndMakeVisible(warningLabel);
    warningLabel.setText(
        "A timestamped backup of .Plugins.ini is created automatically before saving. "
        "FL Studio may need to rescan plugins to pick up changes. plugclass is FL's "
        "internal numeric type code -- its exact meaning per value is not independently "
        "documented, so it's shown as a raw number rather than a guessed label.",
        juce::dontSendNotification);
    warningLabel.setColour(juce::Label::textColourId, juce::Colour(0xFFFFAA00));
    warningLabel.setFont(juce::Font(juce::FontOptions(11.0f)));
    warningLabel.setJustificationType(juce::Justification::topLeft);

    auto setupRow = [this](juce::Label& lbl, const juce::String& text)
    {
        addAndMakeVisible(lbl);
        lbl.setText(text, juce::dontSendNotification);
        lbl.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    };

    setupRow(nameLabel, "Name:");
    addAndMakeVisible(nameValue);
    nameValue.setText(entry.name, juce::dontSendNotification);
    nameValue.setColour(juce::Label::textColourId, juce::Colours::white);

    juce::String currentVendor   = entry.fileRecords.empty() ? juce::String() : entry.fileRecords[0].vendorName;
    juce::String currentCategory = entry.fileRecords.empty() ? juce::String() : entry.fileRecords[0].category;
    int currentPlugClass         = entry.fileRecords.empty() ? -1 : entry.fileRecords[0].plugClass;

    setupRow(vendorLabel, "Vendor:");
    addAndMakeVisible(vendorEditor);
    vendorEditor.setText(currentVendor);

    setupRow(categoryLabel, "Category:");
    addAndMakeVisible(categoryEditor);
    categoryEditor.setText(currentCategory);

    setupRow(plugClassLabel, "Plug Class (raw code):");
    addAndMakeVisible(plugClassEditor);
    plugClassEditor.setText(juce::String(currentPlugClass));
    plugClassEditor.setInputRestrictions(6, "-0123456789");

    addAndMakeVisible(saveBtn);
    saveBtn.setButtonText("Save (writes to .Plugins.ini)");
    saveBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFFFF5C00));
    saveBtn.onClick = [this] { doSave(); };

    addAndMakeVisible(cancelBtn);
    cancelBtn.setButtonText("Cancel");
    cancelBtn.onClick = [this]
    {
        if (closeRequested)
            closeRequested();
    };
}

void PluginEntryEditorComponent::resized()
{
    auto bounds = getLocalBounds().reduced(15);

    titleLabel.setBounds(bounds.removeFromTop(26));
    bounds.removeFromTop(4);
    pathLabel.setBounds(bounds.removeFromTop(18));
    bounds.removeFromTop(10);
    warningLabel.setBounds(bounds.removeFromTop(60));
    bounds.removeFromTop(15);

    auto row = [&](juce::Label& lbl, juce::Component& field)
    {
        auto r = bounds.removeFromTop(30);
        lbl.setBounds(r.removeFromLeft(150));
        field.setBounds(r);
        bounds.removeFromTop(8);
    };

    row(nameLabel, nameValue);
    row(vendorLabel, vendorEditor);
    row(categoryLabel, categoryEditor);
    row(plugClassLabel, plugClassEditor);

    bounds.removeFromTop(15);
    auto btnRow = bounds.removeFromTop(36);
    saveBtn.setBounds(btnRow.removeFromLeft(220));
    btnRow.removeFromLeft(10);
    cancelBtn.setBounds(btnRow.removeFromLeft(100));
}

void PluginEntryEditorComponent::doSave()
{
    int plugClass = plugClassEditor.getText().getIntValue();

    if (manager.WriteInstalledEntryFields(entry, vendorEditor.getText(), categoryEditor.getText(), plugClass))
    {
        if (onSaved)
            onSaved();

        if (closeRequested)
            closeRequested();
    }
    else
    {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
            "Save Failed",
            "Could not write changes to .Plugins.ini. The section may no longer exist "
            "(try rescanning), or the backup copy could not be created.");
    }
}

// ============================================================================
// PluginEntryEditorWindow
// ============================================================================

PluginEntryEditorWindow::PluginEntryEditorWindow(PluginDatabaseManager& manager,
    const PluginDatabaseManager::PluginEntry& entry, std::function<void()> onSaved)
    : DialogWindow("Edit: " + entry.name, juce::Colour(0xFF181818), true, true)
{
    setUsingNativeTitleBar(true);
    auto* c = new PluginEntryEditorComponent(manager, entry, std::move(onSaved));
    c->SetCloseRequestedCallback([this] { closeButtonPressed(); });
    setContentOwned(c, true);
    centreWithSize(560, 400);
    setResizable(false, false);
    setVisible(true);
}

void PluginEntryEditorWindow::closeButtonPressed()
{
    setVisible(false);
    delete this;
}

// ============================================================================
// NfoEditorComponent / Window
// ============================================================================

NfoEditorComponent::NfoEditorComponent(PluginDatabaseManager::PluginEntry entryIn, bool readOnlyIn)
    : entry(std::move(entryIn)), readOnly(readOnlyIn)
{
    setSize(600, 450);

    addAndMakeVisible(titleLabel);
    titleLabel.setText(
        (readOnly ? juce::String(".nfo (read-only): ") : juce::String(".nfo: ")) + entry.name,
        juce::dontSendNotification);
    titleLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    titleLabel.setFont(juce::Font(juce::FontOptions(16.0f, juce::Font::bold)));

    addAndMakeVisible(textEditor);
    textEditor.setMultiLine(true);
    textEditor.setReturnKeyStartsNewLine(true);
    textEditor.setReadOnly(readOnly);
    textEditor.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xFF111111));
    textEditor.setColour(juce::TextEditor::textColourId, juce::Colours::lightgrey);
    textEditor.setFont(juce::Font(juce::FontOptions(13.0f)));
    textEditor.setText(PluginDatabaseManager::ReadNfoRawText(entry));

    if (!readOnly)
    {
        addAndMakeVisible(saveBtn);
        saveBtn.setButtonText("Save");
        saveBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFFFF5C00));
        saveBtn.onClick = [this]
        {
            PluginDatabaseManager::WriteNfoRawText(entry, textEditor.getText());
            if (closeRequested)
                closeRequested();
        };
    }

    addAndMakeVisible(closeBtn);
    closeBtn.setButtonText(readOnly ? "Close" : "Cancel");
    closeBtn.onClick = [this]
    {
        if (closeRequested)
            closeRequested();
    };
}

void NfoEditorComponent::resized()
{
    auto bounds = getLocalBounds().reduced(15);

    titleLabel.setBounds(bounds.removeFromTop(24));
    bounds.removeFromTop(8);

    auto btnRow = bounds.removeFromBottom(36);
    if (!readOnly)
    {
        saveBtn.setBounds(btnRow.removeFromLeft(100));
        btnRow.removeFromLeft(10);
    }
    closeBtn.setBounds(btnRow.removeFromLeft(100));

    bounds.removeFromBottom(8);
    textEditor.setBounds(bounds);
}

NfoEditorWindow::NfoEditorWindow(const PluginDatabaseManager::PluginEntry& entry, bool readOnly)
    : DialogWindow(".nfo Editor", juce::Colour(0xFF181818), true, true)
{
    setUsingNativeTitleBar(true);
    auto* c = new NfoEditorComponent(entry, readOnly);
    c->SetCloseRequestedCallback([this] { closeButtonPressed(); });
    setContentOwned(c, true);
    centreWithSize(600, 450);
    setResizable(true, true);
    setVisible(true);
}

void NfoEditorWindow::closeButtonPressed()
{
    setVisible(false);
    delete this;
}

// ============================================================================
// PluginDatabaseWindow
// ============================================================================

PluginDatabaseWindow::PluginDatabaseWindow()
    : DialogWindow("Plugin Database", juce::Colour(0xFF181818), true, true)
{
    setUsingNativeTitleBar(true);
    setContentOwned(new PluginDatabaseComponent(), true);
    centreWithSize(1050, 650);
    setResizable(true, true);
    setVisible(true);
}

void PluginDatabaseWindow::closeButtonPressed()
{
    setVisible(false);
    delete this;
}
