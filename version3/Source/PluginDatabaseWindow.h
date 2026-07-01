#pragma once
#include <JuceHeader.h>
#include "PluginDatabaseManager.h"
#include "RecycleBinManager.h"

// Standalone window for Phase 4 -- the Plugin Database viewer/editor.
// Kept as a separate DialogWindow rather than folded into the main
// editor's single-window layout, so this feature is additive and
// doesn't require restructuring the existing UI into tabs.
class PluginDatabaseComponent : public juce::Component
{
public:
    PluginDatabaseComponent();
    ~PluginDatabaseComponent() override;

    void resized() override;

    void refreshFromDisk();

private:
    class TableModel : public juce::TableListBoxModel
    {
    public:
        TableModel(PluginDatabaseComponent& ownerIn) : owner(ownerIn) {}

        int getNumRows() override;
        void paintRowBackground(juce::Graphics&, int rowNumber, int width, int height, bool rowIsSelected) override;
        void paintCell(juce::Graphics&, int rowNumber, int columnId, int width, int height, bool rowIsSelected) override;
        void cellClicked(int rowNumber, int columnId, const juce::MouseEvent& event) override;

        std::vector<PluginDatabaseManager::PluginEntry> rows;

    private:
        PluginDatabaseComponent& owner;
    };

    void showRowContextMenu(int rowNumber, juce::Point<int> screenPosition);
    void showNfoEditor(const PluginDatabaseManager::PluginEntry& entry);

    PluginDatabaseManager databaseManager;
    RecycleBinManager recycleBin;

    juce::Label titleLabel;
    juce::Label statusLabel;
    juce::TextButton rescanBtn;
    juce::ComboBox sourceFilterCombo;
    juce::TableListBox table;
    std::unique_ptr<TableModel> tableModel;

    std::vector<PluginDatabaseManager::PluginEntry> allRows; // unfiltered, from last scan
    void applySourceFilter();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginDatabaseComponent)
};

// Thin DialogWindow wrapper so the main editor can launch this with one call.
class PluginDatabaseWindow : public juce::DialogWindow
{
public:
    PluginDatabaseWindow();
    void closeButtonPressed() override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginDatabaseWindow)
};
