#pragma once
#include <JuceHeader.h>
#include "FLPScanner.h"
#include "RecycleBinManager.h"

// Review window for files the scanner encountered but refused to
// automatically move/delete because SafeFileOperations flagged them as
// protected (read-only, system-marked, or inside a blocked directory --
// though the latter should never actually appear here, since blocked
// directories are skipped during scanning entirely and never produce
// entries in the first place).
//
// Every item requires an explicit checkbox before any action -- there
// is deliberately no "select all and organize automatically" shortcut
// baked into the scan flow; the user must open this window and
// consciously check items they want acted on.
class ProtectedFilesComponent : public juce::Component
{
public:
    ProtectedFilesComponent();
    ~ProtectedFilesComponent() override;

    void resized() override;
    void SetEntries(const std::vector<FLPScanner::ProtectedFileEntry>& entries);

private:
    class TableModel : public juce::TableListBoxModel
    {
    public:
        TableModel(ProtectedFilesComponent& ownerIn) : owner(ownerIn) {}

        int getNumRows() override;
        void paintRowBackground(juce::Graphics&, int rowNumber, int width, int height, bool rowIsSelected) override;
        void paintCell(juce::Graphics&, int rowNumber, int columnId, int width, int height, bool rowIsSelected) override;
        juce::Component* refreshComponentForCell(int rowNumber, int columnId, bool isRowSelected,
            juce::Component* existingComponentToUpdate) override;

        struct Row
        {
            juce::String path;
            juce::String reason;
            bool checked = false;
        };
        std::vector<Row> rows;

    private:
        ProtectedFilesComponent& owner;
    };

    void selectAll(bool checked);
    void deleteCheckedItems();

    juce::Label titleLabel;
    juce::Label statusLabel;
    juce::TextButton selectAllBtn, selectNoneBtn, deleteCheckedBtn;
    juce::TableListBox table;
    std::unique_ptr<TableModel> tableModel;
    RecycleBinManager recycleBin;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ProtectedFilesComponent)
};

class ProtectedFilesWindow : public juce::DialogWindow
{
public:
    ProtectedFilesWindow();
    void closeButtonPressed() override;
    void SetEntries(const std::vector<FLPScanner::ProtectedFileEntry>& entries);

private:
    ProtectedFilesComponent* comp = nullptr;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ProtectedFilesWindow)
};
