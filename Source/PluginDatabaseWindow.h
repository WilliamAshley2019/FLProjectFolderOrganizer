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
    void showEntryEditor(const PluginDatabaseManager::PluginEntry& entry);

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

// Dedicated editable form for a single Installed-tree plugin database
// entry -- built as a proper Component with labeled fields rather than
// an AlertWindow popup, since AlertWindow's cramped layout doesn't
// display or edit multi-field data well. Lets the user fix a
// misclassified plugin (vendor/category/plugclass) directly in
// .Plugins.ini, with an automatic backup taken before every write.
class PluginEntryEditorComponent : public juce::Component
{
public:
    PluginEntryEditorComponent(PluginDatabaseManager& managerIn,
        PluginDatabaseManager::PluginEntry entryIn,
        std::function<void()> onSavedCallback);

    void resized() override;
    void SetCloseRequestedCallback(std::function<void()> cb) { closeRequested = std::move(cb); }

private:
    void doSave();

    PluginDatabaseManager& manager;
    PluginDatabaseManager::PluginEntry entry;
    std::function<void()> onSaved;
    std::function<void()> closeRequested; // set by owning DialogWindow

    juce::Label titleLabel;
    juce::Label pathLabel;
    juce::Label warningLabel;

    juce::Label nameLabel, vendorLabel, categoryLabel, plugClassLabel;
    juce::Label nameValue; // read-only, ps_name isn't part of the fields we write back
    juce::TextEditor vendorEditor, categoryEditor, plugClassEditor;

    juce::TextButton saveBtn, cancelBtn;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginEntryEditorComponent)
};

class PluginEntryEditorWindow : public juce::DialogWindow
{
public:
    PluginEntryEditorWindow(PluginDatabaseManager& manager,
        const PluginDatabaseManager::PluginEntry& entry,
        std::function<void()> onSaved);
    void closeButtonPressed() override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginEntryEditorWindow)
};

// Properly sized, properly laid-out text editor for .nfo raw content --
// replaces the earlier AlertWindow-based popup, which displayed
// multi-line text poorly. Read-only for Installed entries, editable for
// Favorites entries.
class NfoEditorComponent : public juce::Component
{
public:
    NfoEditorComponent(PluginDatabaseManager::PluginEntry entryIn, bool readOnlyIn);
    void resized() override;
    void SetCloseRequestedCallback(std::function<void()> cb) { closeRequested = std::move(cb); }

private:
    PluginDatabaseManager::PluginEntry entry;
    bool readOnly;
    juce::Label titleLabel;
    juce::TextEditor textEditor;
    juce::TextButton saveBtn, closeBtn;
    std::function<void()> closeRequested;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NfoEditorComponent)
};

class NfoEditorWindow : public juce::DialogWindow
{
public:
    NfoEditorWindow(const PluginDatabaseManager::PluginEntry& entry, bool readOnly);
    void closeButtonPressed() override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NfoEditorWindow)
};
