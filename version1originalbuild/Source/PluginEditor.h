#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "FLPLookAndFeel.h"
#include "FLPScanner.h"
#include "ProjectDatabase.h"

class FLProjectOrganizerEditor : public juce::AudioProcessorEditor,
    private juce::Timer
{
public:
    FLProjectOrganizerEditor(FLProjectOrganizerProcessor&);
    ~FLProjectOrganizerEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;

private:
    // Table model
    class ResultTableModel : public juce::TableListBoxModel
    {
    public:
        ResultTableModel(FLProjectOrganizerEditor& editor) : parent(editor) {}

        int getNumRows() override;
        void paintRowBackground(juce::Graphics&, int rowNumber, int width, int height, bool rowIsSelected) override;
        void paintCell(juce::Graphics&, int rowNumber, int columnId, int width, int height, bool rowIsSelected) override;
        void sortOrderChanged(int newSortColumnId, bool isForwards) override;

        std::vector<ProjectDatabase::ProjectEntry> displayRows;

    private:
        FLProjectOrganizerEditor& parent;
        int sortColumn = 0;
        bool sortForwards = true;
    };

    // Main components
    FLProjectOrganizerProcessor& processorRef;
    std::unique_ptr<FLPLookAndFeel> lnf;

    // Header area
    juce::Label titleLabel;
    juce::Label versionLabel;

    // Source/Destination selection
    juce::Label sourceLabel, destLabel;
    juce::TextEditor sourcePathEditor, destPathEditor;
    juce::TextButton browseSourceBtn, browseDestBtn;

    // Options
    juce::GroupComponent optionsGroup;
    juce::ToggleButton includeZipsToggle;
    juce::ToggleButton scanSubfoldersToggle;
    juce::ToggleButton dryRunToggle;
    juce::ToggleButton deleteOriginalsToggle;
    juce::ToggleButton verifyCopiesToggle;

    // Action buttons
    juce::TextButton scanBtn, organizeBtn, stopBtn;
    juce::TextButton exportCSVBtn, undoBtn;

    // Results display
    juce::TableListBox resultTable;
    juce::Label statusLabel;
    double currentProgress = 0.0;
    juce::ProgressBar progressBar { currentProgress };

    // Log area
    juce::TextEditor logBox;

    // Scanner thread
    std::unique_ptr<FLPScanner> scanner;
    juce::CriticalSection logLock;
    juce::StringArray pendingLogs;

    // Database
    ProjectDatabase database;
    bool dbInitialized = false;

    // UI state
    bool isScanning = false;
    bool isOrganizing = false;

    // FileChoosers (must be member variables for async operations)
    std::unique_ptr<juce::FileChooser> sourceChooser;
    std::unique_ptr<juce::FileChooser> destChooser;
    std::unique_ptr<juce::FileChooser> csvChooser;

    // Methods
    void browseSource();
    void browseDest();
    void startScan();
    void startOrganize();
    void stopOperation();
    void updateUIState();
    void updateResultTable();
    void sendLog(const juce::String& msg);
    void refreshDatabaseView();
    void exportCSV();
    void undoLastOperation();

    std::unique_ptr<ResultTableModel> tableModel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FLProjectOrganizerEditor)
};