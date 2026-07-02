#pragma once
#include <JuceHeader.h>
#include "SampleScanner.h"

class SampleScannerComponent : public juce::Component, private juce::Timer
{
public:
    SampleScannerComponent();
    ~SampleScannerComponent() override;

    void resized() override;
    void timerCallback() override;

private:
    class TableModel : public juce::TableListBoxModel
    {
    public:
        int getNumRows() override;
        void paintRowBackground(juce::Graphics&, int rowNumber, int width, int height, bool rowIsSelected) override;
        void paintCell(juce::Graphics&, int rowNumber, int columnId, int width, int height, bool rowIsSelected) override;

        std::vector<SampleScanner::SampleEntry> rows;
    };

    void browseSource();
    void browseDest();
    void startScan();
    void stopScan();
    void sendLog(const juce::String& msg);

    juce::Label titleLabel;
    juce::Label sourceLabel, destLabel;
    juce::TextEditor sourcePathEditor, destPathEditor;
    juce::TextButton browseSourceBtn, browseDestBtn;

    juce::ToggleButton wavToggle, mp3Toggle, flacToggle, oggToggle;
    juce::ToggleButton scanSubfoldersToggle, dryRunToggle, deleteOriginalsToggle;

    juce::TextButton scanBtn, stopBtn;
    juce::Label statusLabel;
    double currentProgress = 0.0;
    juce::ProgressBar progressBar { currentProgress };

    juce::TableListBox table;
    std::unique_ptr<TableModel> tableModel;

    juce::TextEditor logBox;
    juce::CriticalSection logLock;
    juce::StringArray pendingLogs;

    std::unique_ptr<SampleScanner> scanner;
    std::unique_ptr<juce::FileChooser> sourceChooser, destChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SampleScannerComponent)
};

class SampleScannerWindow : public juce::DialogWindow
{
public:
    SampleScannerWindow();
    void closeButtonPressed() override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SampleScannerWindow)
};
