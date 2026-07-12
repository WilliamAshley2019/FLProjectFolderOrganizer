#include "ProtectedFilesWindow.h"

// ============================================================================
// TableModel
// ============================================================================

int ProtectedFilesComponent::TableModel::getNumRows()
{
    return (int)rows.size();
}

void ProtectedFilesComponent::TableModel::paintRowBackground(
    juce::Graphics& g, int rowNumber, int /*width*/, int /*height*/, bool rowIsSelected)
{
    auto bg = rowIsSelected ? juce::Colour(0xFF2A2A3A) :
        (rowNumber % 2 ? juce::Colour(0xFF222222) : juce::Colour(0xFF1A1A1A));
    g.fillAll(bg);
}

void ProtectedFilesComponent::TableModel::paintCell(
    juce::Graphics& g, int rowNumber, int columnId, int width, int height, bool /*rowIsSelected*/)
{
    if (rowNumber >= (int)rows.size())
        return;

    if (columnId == 1)
        return; // checkbox column, handled by refreshComponentForCell

    const auto& row = rows[(size_t)rowNumber];
    juce::String text = (columnId == 2) ? row.path : row.reason;

    g.setColour(juce::Colours::lightgrey);
    g.setFont(juce::Font(juce::FontOptions(13.0f)));
    g.drawText(text, 4, 0, width - 8, height, juce::Justification::centredLeft);
}

juce::Component* ProtectedFilesComponent::TableModel::refreshComponentForCell(
    int rowNumber, int columnId, bool /*isRowSelected*/, juce::Component* existingComponentToUpdate)
{
    if (columnId != 1)
    {
        delete existingComponentToUpdate;
        return nullptr;
    }

    auto* toggle = dynamic_cast<juce::ToggleButton*>(existingComponentToUpdate);
    if (toggle == nullptr)
    {
        delete existingComponentToUpdate;
        toggle = new juce::ToggleButton();
    }

    if (rowNumber >= 0 && rowNumber < (int)rows.size())
    {
        toggle->setToggleState(rows[(size_t)rowNumber].checked, juce::dontSendNotification);
        toggle->onClick = [this, rowNumber, toggle]
        {
            if (rowNumber >= 0 && rowNumber < (int)rows.size())
                rows[(size_t)rowNumber].checked = toggle->getToggleState();
        };
    }

    return toggle;
}

// ============================================================================
// ProtectedFilesComponent
// ============================================================================

ProtectedFilesComponent::ProtectedFilesComponent()
{
    setSize(850, 500);

    addAndMakeVisible(titleLabel);
    titleLabel.setText("Protected Files -- Review Required", juce::dontSendNotification);
    titleLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    titleLabel.setFont(juce::Font(juce::FontOptions(18.0f, juce::Font::bold)));

    addAndMakeVisible(statusLabel);
    statusLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    statusLabel.setText("These files were found during scanning but were NOT automatically moved or "
        "deleted because they are marked read-only, system-protected, or otherwise flagged unsafe. "
        "Check the items you want to move to the recycle bin, then click \"Delete Checked\".",
        juce::dontSendNotification);
    statusLabel.setJustificationType(juce::Justification::topLeft);

    addAndMakeVisible(selectAllBtn);
    selectAllBtn.setButtonText("Select All");
    selectAllBtn.onClick = [this] { selectAll(true); };

    addAndMakeVisible(selectNoneBtn);
    selectNoneBtn.setButtonText("Select None");
    selectNoneBtn.onClick = [this] { selectAll(false); };

    addAndMakeVisible(deleteCheckedBtn);
    deleteCheckedBtn.setButtonText("Delete Checked (to Recycle Bin)");
    deleteCheckedBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFFFF4444));
    deleteCheckedBtn.onClick = [this] { deleteCheckedItems(); };

    addAndMakeVisible(table);
    tableModel = std::make_unique<TableModel>(*this);
    table.setModel(tableModel.get());
    table.getHeader().addColumn("", 1, 40);
    table.getHeader().addColumn("Path", 2, 480);
    table.getHeader().addColumn("Reason", 3, 260);
}

ProtectedFilesComponent::~ProtectedFilesComponent() = default;

void ProtectedFilesComponent::resized()
{
    auto bounds = getLocalBounds().reduced(15);

    titleLabel.setBounds(bounds.removeFromTop(28));
    bounds.removeFromTop(5);
    statusLabel.setBounds(bounds.removeFromTop(50));
    bounds.removeFromTop(10);

    auto buttonRow = bounds.removeFromTop(32);
    selectAllBtn.setBounds(buttonRow.removeFromLeft(100));
    buttonRow.removeFromLeft(10);
    selectNoneBtn.setBounds(buttonRow.removeFromLeft(100));
    buttonRow.removeFromLeft(10);
    deleteCheckedBtn.setBounds(buttonRow.removeFromLeft(220));

    bounds.removeFromTop(10);
    table.setBounds(bounds);
}

void ProtectedFilesComponent::SetEntries(const std::vector<FLPScanner::ProtectedFileEntry>& entries)
{
    tableModel->rows.clear();
    for (const auto& e : entries)
        tableModel->rows.push_back({ e.path, e.reason, false });

    table.updateContent();
}

void ProtectedFilesComponent::selectAll(bool checked)
{
    for (auto& row : tableModel->rows)
        row.checked = checked;
    table.updateContent();
}

void ProtectedFilesComponent::deleteCheckedItems()
{
    juce::Array<int> checkedIndices;
    for (int i = 0; i < (int)tableModel->rows.size(); ++i)
        if (tableModel->rows[(size_t)i].checked)
            checkedIndices.add(i);

    if (checkedIndices.isEmpty())
    {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon,
            "Nothing Selected", "Check at least one item before deleting.");
        return;
    }

    juce::AlertWindow::showAsync(
        juce::MessageBoxOptions()
            .withIconType(juce::MessageBoxIconType::WarningIcon)
            .withTitle("Confirm Delete")
            .withMessage("Move " + juce::String(checkedIndices.size()) +
                " checked file(s) to the recycle bin?\n\n"
                "These files were flagged as protected -- please make sure you really "
                "intend to remove them.")
            .withButton("Move to Recycle Bin")
            .withButton("Cancel"),
        [this, checkedIndices](int result)
        {
            if (result != 1)
                return;

            int successCount = 0, failCount = 0;
            for (int idx : checkedIndices)
            {
                juce::File f(tableModel->rows[(size_t)idx].path);
                if (recycleBin.SoftDelete(f))
                    ++successCount;
                else
                    ++failCount;
            }

            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon,
                "Delete Complete",
                juce::String(successCount) + " file(s) moved to recycle bin. " +
                (failCount > 0 ? juce::String(failCount) + " failed." : juce::String()));

            std::vector<TableModel::Row> remaining;
            for (int i = 0; i < (int)tableModel->rows.size(); ++i)
            {
                bool wasChecked = checkedIndices.contains(i);
                juce::File f(tableModel->rows[(size_t)i].path);
                if (!(wasChecked && !f.existsAsFile()))
                    remaining.push_back(tableModel->rows[(size_t)i]);
            }
            tableModel->rows = remaining;
            table.updateContent();
        });
}

// ============================================================================
// ProtectedFilesWindow
// ============================================================================

ProtectedFilesWindow::ProtectedFilesWindow()
    : DialogWindow("Protected Files", juce::Colour(0xFF181818), true, true)
{
    setUsingNativeTitleBar(true);
    auto* c = new ProtectedFilesComponent();
    comp = c;
    setContentOwned(c, true);
    centreWithSize(850, 500);
    setResizable(true, true);
    setVisible(true);
}

void ProtectedFilesWindow::closeButtonPressed()
{
    setVisible(false);
    delete this;
}

void ProtectedFilesWindow::SetEntries(const std::vector<FLPScanner::ProtectedFileEntry>& entries)
{
    if (comp != nullptr)
        comp->SetEntries(entries);
}
