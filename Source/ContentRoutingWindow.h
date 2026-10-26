#pragma once
#include <JuceHeader.h>
#include "ConfigManager.h"
#include "ContentRouter.h"
#include "FileTransfer.h"
#include "OrganizerTheme.h"

// Drop FL content (presets, FXP/H2P, scores, projects, samples, folders or ZIPs) here;
// each item is identified and routed to the folder FL Studio expects, with conflict
// prompts and recycle-bin safety. Destinations come from routing.json.
class ContentRoutingPanel : public juce::Component,
                            public juce::FileDragAndDropTarget,
                            private juce::ListBoxModel
{
public:
    ContentRoutingPanel();
    ~ContentRoutingPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
    void filesDropped (const juce::StringArray& files, int, int) override;

private:
    struct Item
    {
        FileTransfer::Package pkg;
        juce::String destId = "auto";      // "auto" or a destination id
        juce::String status;
    };

    // ListBoxModel
    int getNumRows() override { return (int) items.size(); }
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    void selectedRowsChanged (int) override { refreshChoices(); }
    void deleteKeyPressed (int row) override { removeRow (row); }

    class ScanThread; class ImportThread;

    void addPaths (const juce::StringArray&);
    void appendPackages (std::vector<FileTransfer::Package>);
    void removeRow (int row);
    void refreshChoices();
    const RoutingConfig::Destination* effectiveDestination (const Item&, juce::String* why = nullptr) const;
    void startImport();
    void cancelWork();
    void saveSettings();
    void setBusy (bool);
    void log (const juce::String&);
    void chooseRoot();
    static juce::File targetFor (const FileTransfer::Package&, const juce::File& base);
    juce::String askConflict (const juce::File& existing);   // called from the worker thread

    RoutingConfig::Config config;
    juce::File configFile;
    std::vector<Item> items;
    std::vector<juce::String> choiceIds;       // destination ids behind destChoice items 2..N

    flo::ui::Chassis chassis;
    juce::ListBox list { {}, this };
    juce::Label destLabel { {}, "Destination" }, rootLabel { {}, "FL user data" };
    juce::ComboBox destChoice;
    juce::TextEditor rootEditor, logBox;
    juce::TextButton addFilesBtn { "Add Files..." }, addFolderBtn { "Add Folder..." }, clearBtn { "Clear" },
                     importBtn { "Import" }, cancelBtn { "Cancel" }, rootBtn { "Browse..." }, saveBtn { "Save Settings" };
    juce::ToggleButton replaceToggle { "Replace duplicates without asking (old file goes to the recycle bin)" };
    double progressValue = -1.0;
    juce::ProgressBar progress { progressValue };

    std::unique_ptr<juce::Thread> worker;
    std::atomic<bool> cancelFlag { false };
    std::unique_ptr<juce::FileChooser> chooser;
    bool busy = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ContentRoutingPanel)
};

class ContentRoutingWindow : public juce::DialogWindow
{
public:
    ContentRoutingWindow();
    void closeButtonPressed() override;
private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ContentRoutingWindow)
};
