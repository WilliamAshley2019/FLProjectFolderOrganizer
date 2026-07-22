#pragma once
#include <JuceHeader.h>
#include "flphelper.h"

// Runs FL::BatchProcessor::processBatch on a background thread so a large
// batch of files doesn't freeze the UI. Mirrors the same threading pattern
// used elsewhere in this codebase (a Thread subclass with onLog/onProgress
// callbacks marshalled back to the message thread via MessageManager::callAsync).
class BatchProcessRunner : public juce::Thread
{
public:
    BatchProcessRunner() : juce::Thread("BatchProcessRunner") {}
    ~BatchProcessRunner() override { stopThread(5000); }

    void start(const juce::Array<juce::File>& inputFilesIn,
        const juce::File& outputDirIn,
        const FL::BatchProcessor::Options& optionsIn)
    {
        inputFiles = inputFilesIn;
        outputDir = outputDirIn;
        options = optionsIn;
        startThread();
    }

    std::function<void(const juce::String&)> onLog;
    std::function<void(float)> onProgress;
    std::function<void(int, int)> onComplete; // successCount, total

private:
    void run() override
    {
        int total = inputFiles.size();
        auto successCount = FL::BatchProcessor::processBatch(inputFiles, outputDir, options,
            [this, total](int index, int, const juce::String& name)
            {
                if (threadShouldExit()) return;
                juce::MessageManager::callAsync([this, index, total, name]
                {
                    if (onLog) onLog("[" + juce::String(index + 1) + "/" + juce::String(total) + "] " + name);
                    if (onProgress) onProgress(total > 0 ? (float)index / (float)total : 1.0f);
                });
            });

        juce::MessageManager::callAsync([this, successCount, total]
        {
            if (onProgress) onProgress(1.0f);
            if (onComplete) onComplete(successCount, total);
        });
    }

    juce::Array<juce::File> inputFiles;
    juce::File outputDir;
    FL::BatchProcessor::Options options;
};

class BatchProcessComponent : public juce::Component, private juce::Timer
{
public:
    BatchProcessComponent();
    ~BatchProcessComponent() override;

    void resized() override;
    void timerCallback() override;

private:
    void addInputFiles();
    void browseOutputDir();
    void browseRelativeBaseDir();
    void startBatch();
    void sendLog(const juce::String& msg);

    juce::Label titleLabel;

    juce::Label inputFilesLabel;
    juce::TextEditor inputFilesEditor; // read-only summary of selected files
    juce::TextButton addFilesBtn, clearFilesBtn;
    juce::Array<juce::File> selectedFiles;

    juce::Label outputDirLabel;
    juce::TextEditor outputDirEditor;
    juce::TextButton browseOutputBtn;

    juce::Label tempoScaleLabel;
    juce::TextEditor tempoScaleEditor { "Tempo Scale Factor" };

    juce::ToggleButton renameTracksToggle;

    juce::ToggleButton makeSamplesRelativeToggle;
    juce::Label relativeBaseDirLabel;
    juce::TextEditor relativeBaseDirEditor;
    juce::TextButton browseRelativeBaseBtn;

    juce::Label watermarkTitleLabel, watermarkAuthorLabel;
    juce::TextEditor watermarkTitleEditor, watermarkAuthorEditor;

    juce::TextButton runBtn;
    juce::Label statusLabel;
    double currentProgress = 0.0;
    juce::ProgressBar progressBar { currentProgress };

    juce::TextEditor logBox;
    juce::CriticalSection logLock;
    juce::StringArray pendingLogs;

    std::unique_ptr<BatchProcessRunner> runner;
    std::unique_ptr<juce::FileChooser> inputFilesChooser, outputDirChooser, relativeBaseChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BatchProcessComponent)
};

class BatchProcessWindow : public juce::DialogWindow
{
public:
    BatchProcessWindow();
    void closeButtonPressed() override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BatchProcessWindow)
};
