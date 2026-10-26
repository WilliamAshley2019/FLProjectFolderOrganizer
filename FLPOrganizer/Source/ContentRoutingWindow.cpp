#include "ContentRoutingWindow.h"
#include "InstalledPluginLookup.h"

using namespace RoutingConfig;

// ─── worker threads ─────────────────────────────────────────────────────────

class ContentRoutingPanel::ScanThread : public juce::Thread
{
public:
    ScanThread (ContentRoutingPanel& o, juce::StringArray p, const FileTransfer::Limits& l)
        : juce::Thread ("RoutingScan"), owner (o), paths (std::move (p)), limits (l), safe (&o) {}
    void run() override
    {
        InstalledPluginLookup::getInstance().invalidateFstIndex();
        std::vector<FileTransfer::Package> found;
        for (auto& path : paths)
        {
            if (threadShouldExit() || owner.cancelFlag.load()) break;
            FileTransfer::Package pkg; juce::String err;
            auto name = juce::File (path).getFileName();
            if (! FileTransfer::scan (juce::File (path), limits, owner.cancelFlag, &InstalledPluginLookup::getInstance(), pkg, err))
            { post ("Skipped " + name + ": " + err); continue; }
            for (auto& g : ContentRouter::fstPackageGroups (pkg))
            {
                for (auto& kv : g.info) if (kv.second.error.isNotEmpty() && g.plannedFiles().contains (kv.first))
                    post ("Note: " + kv.first + ": " + kv.second.error);
                found.push_back (std::move (g));
            }
        }
        found = ContentRouter::groupLooseScripts (std::move (found), &owner.cancelFlag);
        for (auto& pkg : found)
            for (auto& note : pkg.scriptNotes)
                post ("Script note (" + pkg.folderName() + "): " + note);
        auto s = safe;
        auto* fp = new std::vector<FileTransfer::Package> (std::move (found));
        juce::MessageManager::callAsync ([s, fp]
        {
            std::unique_ptr<std::vector<FileTransfer::Package>> own (fp);
            if (s != nullptr) { s->appendPackages (std::move (*own)); s->setBusy (false); }
        });
    }
private:
    void post (const juce::String& m) { auto s = safe; juce::MessageManager::callAsync ([s, m] { if (s != nullptr) s->log (m); }); }
    ContentRoutingPanel& owner; juce::StringArray paths; FileTransfer::Limits limits;
    juce::Component::SafePointer<ContentRoutingPanel> safe;
};

class ContentRoutingPanel::ImportThread : public juce::Thread
{
public:
    ImportThread (ContentRoutingPanel& o, std::vector<FileTransfer::Job> j, const FileTransfer::Limits& l, bool alwaysReplace)
        : juce::Thread ("RoutingImport"), owner (o), jobs (std::move (j)), limits (l), replaceAll (alwaysReplace), safe (&o) {}
    void run() override
    {
        auto conflict = [this] (const juce::File& existing) -> juce::String
        {
            if (replaceAll) return "replace";
            return owner.askConflict (existing);
        };
        auto progress = [this] (const juce::String& m)
        {
            const auto now = juce::Time::getMillisecondCounter();
            if (now - lastPost < 120) return;
            lastPost = now; auto s = safe;
            juce::MessageManager::callAsync ([s, m] { if (s != nullptr) s->log ("  " + m); });
        };
        auto result = FileTransfer::runJobs (jobs, limits, owner.cancelFlag, conflict, progress);
        auto s = safe;
        juce::MessageManager::callAsync ([s, result]
        {
            if (s == nullptr) return;
            for (auto& d : result.details) s->log (d);
            s->log ("Import finished: " + juce::String (result.copied) + " copied, " + juce::String (result.skipped) + " skipped, "
                    + juce::String (result.failed) + " failed" + (result.cancelled ? ", " + juce::String (result.cancelled) + " cancelled" : juce::String()) + ".");
            s->setBusy (false);
        });
    }
private:
    ContentRoutingPanel& owner; std::vector<FileTransfer::Job> jobs; FileTransfer::Limits limits; bool replaceAll;
    juce::Component::SafePointer<ContentRoutingPanel> safe; juce::uint32 lastPost = 0;
};

// ─── panel ──────────────────────────────────────────────────────────────────

ContentRoutingPanel::ContentRoutingPanel()
{
    configFile = RoutingConfig::defaultConfigPath();
    juce::String err;
    if (! RoutingConfig::load (configFile, config, err))
    {
        config = RoutingConfig::makeDefault();
        RoutingConfig::autoFillRoots (config);
        juce::MessageManager::callAsync ([this, err] { log (err + "  Using defaults for this session; the file was not modified."); });
    }

    addAndMakeVisible (chassis);
    for (auto* c : std::initializer_list<juce::Component*> { &list, &destLabel, &rootLabel, &destChoice, &rootEditor, &logBox,
                                                              &addFilesBtn, &addFolderBtn, &clearBtn, &importBtn, &cancelBtn,
                                                              &rootBtn, &saveBtn, &replaceToggle, &progress })
        addAndMakeVisible (*c);

    list.setRowHeight (28);
    list.setOutlineThickness (0);
    for (auto* l : { &destLabel, &rootLabel }) { l->setFont (flo::ui::caps (10.f)); l->setColour (juce::Label::textColourId, flo::ui::col::slateText); }
    rootEditor.setReadOnly (true);
    rootEditor.setText (config.userData, false);
    logBox.setMultiLine (true); logBox.setReadOnly (true);
    logBox.setFont (juce::FontOptions (12.f));

    importBtn.setColour (juce::TextButton::buttonColourId, flo::ui::col::accent);
    addFilesBtn.setColour (juce::TextButton::buttonColourId, flo::ui::col::cyan);
    addFolderBtn.setColour (juce::TextButton::buttonColourId, flo::ui::col::cyan);
    clearBtn.setColour (juce::TextButton::buttonColourId, flo::ui::col::textDim);
    cancelBtn.setColour (juce::TextButton::buttonColourId, flo::ui::col::ledRed);
    saveBtn.setColour (juce::TextButton::buttonColourId, flo::ui::col::ledGreen);
    rootBtn.setColour (juce::TextButton::buttonColourId, flo::ui::col::textDim);
    replaceToggle.setToggleState (config.alwaysReplaceDuplicates, juce::dontSendNotification);
    cancelBtn.setEnabled (false);

    addFilesBtn.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Choose FL content", juce::File(), "*");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::canSelectMultipleItems,
                              [this] (const juce::FileChooser& fc)
                              { juce::StringArray p; for (auto& f : fc.getResults()) p.add (f.getFullPathName()); addPaths (p); });
    };
    addFolderBtn.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Choose a folder of FL content", juce::File());
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                              [this] (const juce::FileChooser& fc) { if (fc.getResult().isDirectory()) addPaths ({ fc.getResult().getFullPathName() }); });
    };
    clearBtn.onClick = [this] { if (! busy) { items.clear(); list.updateContent(); refreshChoices(); repaint(); } };
    importBtn.onClick = [this] { startImport(); };
    cancelBtn.onClick = [this] { cancelWork(); };
    rootBtn.onClick = [this] { chooseRoot(); };
    saveBtn.onClick = [this] { saveSettings(); };
    replaceToggle.onClick = [this] { config.alwaysReplaceDuplicates = replaceToggle.getToggleState(); };
    destChoice.onChange = [this]
    {
        const int row = list.getSelectedRow();
        if (row < 0 || row >= (int) items.size()) return;
        auto* item = &items[(size_t) row];
        const int id = destChoice.getSelectedId();
        item->destId = (id <= 1 || id - 2 >= (int) choiceIds.size()) ? juce::String ("auto") : choiceIds[(size_t) (id - 2)];
        list.repaintRow (row);
    };

    if (config.userData.isEmpty()) log ("FL user data folder not found automatically. Use Browse... to set it, then Save Settings.");
    else log ("FL user data: " + config.userData);
    log ("Drop files, folders or ZIP archives onto this window, or use Add Files / Add Folder.");
}

ContentRoutingPanel::~ContentRoutingPanel()
{
    cancelFlag = true;
    if (worker != nullptr) worker->stopThread (15000);
}

void ContentRoutingPanel::log (const juce::String& m)
{
    logBox.moveCaretToEnd();
    logBox.insertTextAtCaret (m + "\n");
}

void ContentRoutingPanel::setBusy (bool b)
{
    busy = b;
    for (auto* c : { &addFilesBtn, &addFolderBtn, &clearBtn, &importBtn, &saveBtn, &rootBtn })
        c->setEnabled (! b);
    cancelBtn.setEnabled (b);
    progressValue = b ? -1.0 : 0.0;
    if (! b && worker != nullptr) { /* joined lazily on next start/destruction */ }
    repaint();
}

void ContentRoutingPanel::paint (juce::Graphics&) {}

void ContentRoutingPanel::resized()
{
    auto b = getLocalBounds();
    chassis.setBounds (b);
    auto area = b.reduced (14);

    auto top = area.removeFromTop (30);
    rootLabel.setBounds (top.removeFromLeft (110));
    rootBtn.setBounds (top.removeFromRight (100)); top.removeFromRight (6);
    saveBtn.setBounds (top.removeFromRight (130)); top.removeFromRight (6);
    rootEditor.setBounds (top);
    area.removeFromTop (10);

    auto btnRow = area.removeFromBottom (34);
    importBtn.setBounds (btnRow.removeFromLeft (110)); btnRow.removeFromLeft (8);
    cancelBtn.setBounds (btnRow.removeFromLeft (90)); btnRow.removeFromLeft (14);
    progress.setBounds (btnRow);
    area.removeFromBottom (8);

    auto logArea = area.removeFromBottom (juce::jmax (90, area.getHeight() / 3));
    logBox.setBounds (logArea.reduced (2));
    area.removeFromBottom (8);

    auto toggleRow = area.removeFromBottom (24);
    replaceToggle.setBounds (toggleRow);
    area.removeFromBottom (6);

    auto choiceRow = area.removeFromBottom (30);
    destLabel.setBounds (choiceRow.removeFromLeft (110));
    destChoice.setBounds (choiceRow);
    area.removeFromBottom (8);

    auto head = area.removeFromTop (30);
    addFilesBtn.setBounds (head.removeFromLeft (120)); head.removeFromLeft (8);
    addFolderBtn.setBounds (head.removeFromLeft (130)); head.removeFromLeft (8);
    clearBtn.setBounds (head.removeFromLeft (80));
    area.removeFromTop (8);
    list.setBounds (area.reduced (2));

    chassis.plates = { b.toFloat().reduced (6.f) };
    chassis.wells = { area.toFloat(), logArea.toFloat() };
    chassis.repaint();
}

void ContentRoutingPanel::filesDropped (const juce::StringArray& files, int, int) { addPaths (files); }

void ContentRoutingPanel::addPaths (const juce::StringArray& paths)
{
    if (busy || paths.isEmpty()) return;
    setBusy (true);
    cancelFlag = false;
    if (worker != nullptr) { worker->stopThread (5000); worker.reset(); }
    FileTransfer::Limits lim { config.zipEntries, config.zipBytes, config.zipRatio };
    worker.reset (new ScanThread (*this, paths, lim));
    worker->startThread();
    log ("Scanning " + juce::String (paths.size()) + " item(s)...");
}

void ContentRoutingPanel::appendPackages (std::vector<FileTransfer::Package> pk)
{
    for (auto& p : pk)
    {
        Item it; it.pkg = std::move (p);
        items.push_back (std::move (it));
    }
    list.updateContent();
    if (! items.empty() && list.getSelectedRow() < 0) list.selectRow (0);
    log (juce::String ((int) pk.size()) + " package(s) added.");
    refreshChoices();
    repaint();
}

void ContentRoutingPanel::removeRow (int row)
{
    if (busy || row < 0 || row >= (int) items.size()) return;
    items.erase (items.begin() + row);
    list.updateContent(); refreshChoices();
}

const Destination* ContentRoutingPanel::effectiveDestination (const Item& it, juce::String* why) const
{
    if (it.destId != "auto")
    {
        auto* d = config.find (it.destId);
        if (d == nullptr && why) *why = "Unknown destination";
        return d;
    }
    auto choices = ContentRouter::packageChoices (config, it.pkg, true);
    if (choices.size() == 1) return choices.front();
    if (why) *why = choices.empty() ? (ContentRouter::needsPluginName (it.pkg) ? "plugin not identified - choose a destination"
                                                                                : "no matching destination - choose one")
                                    : "several destinations match - choose one";
    return nullptr;
}

void ContentRoutingPanel::refreshChoices()
{
    destChoice.clear (juce::dontSendNotification);
    choiceIds.clear();
    destChoice.addItem ("Automatic", 1);
    const int row = list.getSelectedRow();
    if (row >= 0 && row < (int) items.size())
    {
        auto& it = items[(size_t) row];
        int id = 2; int sel = 1;
        for (auto* d : ContentRouter::packageChoices (config, it.pkg, false))
        {
            destChoice.addItem (d->name, id);
            choiceIds.push_back (d->id);
            if (d->id == it.destId) sel = id;
            ++id;
        }
        destChoice.setSelectedId (sel, juce::dontSendNotification);
    }
    else destChoice.setSelectedId (1, juce::dontSendNotification);
}

void ContentRoutingPanel::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected)
{
    if (row < 0 || row >= (int) items.size()) return;
    auto& it = items[(size_t) row];
    if (selected) { g.setColour (flo::ui::col::accent.withAlpha (0.16f)); g.fillRoundedRectangle (3.f, 1.f, (float) w - 6.f, (float) h - 2.f, 4.f);
                    g.setColour (flo::ui::col::accent); g.fillRect (3.f, 6.f, 2.f, (float) h - 12.f); }

    juce::String why;
    auto* d = effectiveDestination (it, &why);
    auto title = it.pkg.groupName.isNotEmpty() ? it.pkg.groupName : it.pkg.source.getFileName();
    if (it.pkg.groupName.isNotEmpty()) title << "  (" << it.pkg.source.getFileName() << ")";
    flo::ui::led (g, { 12.f, (float) h * 0.5f - 4.f, 8.f, 8.f }, d ? flo::ui::col::ledGreen : flo::ui::col::amber, true);
    g.setFont (flo::ui::sans (13.f));
    g.setColour (selected ? flo::ui::col::text : flo::ui::col::textDim.brighter (0.2f));
    g.drawText (title + "  [" + juce::String (it.pkg.plannedFiles().size()) + " file(s)]", 28, 0, (int) (w * 0.55f), h, juce::Justification::centredLeft, true);
    g.setFont (flo::ui::sans (12.f));
    g.setColour (d ? flo::ui::col::amber : flo::ui::col::textDim);
    g.drawText (d ? "->  " + d->name : why, (int) (w * 0.58f), 0, (int) (w * 0.40f), h, juce::Justification::centredLeft, true);
}

juce::File ContentRoutingPanel::targetFor (const FileTransfer::Package& p, const juce::File& base)
{
    if (p.isZip) return p.zipRootFolder.isNotEmpty() ? base : base.getChildFile (p.folderName());
    if (p.source.isDirectory()) return base.getChildFile (p.folderName());
    return base;                                                   // a loose file lands directly in the folder
}

void ContentRoutingPanel::startImport()
{
    if (busy) return;
    if (items.empty()) { log ("Nothing to import."); return; }

    std::vector<FileTransfer::Job> jobs;
    for (auto& it : items)
    {
        juce::String why;
        auto* d = effectiveDestination (it, &why);
        auto name = it.pkg.groupName.isNotEmpty() ? it.pkg.groupName : it.pkg.source.getFileName();
        if (d == nullptr) { log ("Skipped " + name + ": " + why); continue; }

        juce::StringArray bad;
        if (! ContentRouter::packageMatches (config, it.pkg.plannedFiles(), *d, &bad))
        { log ("Skipped " + name + ": '" + d->name + "' does not accept " + (bad.isEmpty() ? juce::String ("these files") : bad.joinIntoString (", "))); continue; }

        juce::File base, target; juce::String err;
        if (! RoutingConfig::resolve (config, *d, base, target, err)) { log ("Skipped " + name + ": " + err); continue; }
        if (! base.isDirectory()) { log ("Skipped " + name + ": the destination root does not exist: " + base.getFullPathName()); continue; }

        FileTransfer::Job j;
        j.package = it.pkg; j.root = base; j.target = targetFor (it.pkg, target);
        jobs.push_back (std::move (j));
    }
    if (jobs.empty()) { log ("No importable items."); return; }

    setBusy (true);
    cancelFlag = false;
    if (worker != nullptr) { worker->stopThread (5000); worker.reset(); }
    FileTransfer::Limits lim { config.zipEntries, config.zipBytes, config.zipRatio };
    worker.reset (new ImportThread (*this, std::move (jobs), lim, config.alwaysReplaceDuplicates));
    worker->startThread();
    log ("Importing...");
}

void ContentRoutingPanel::cancelWork() { cancelFlag = true; log ("Cancelling..."); }

juce::String ContentRoutingPanel::askConflict (const juce::File& existing)
{
    // Runs on the worker thread: ask on the message thread and wait for the answer.
    struct Box { juce::WaitableEvent done; juce::String answer = "cancel"; };
    auto box = std::make_shared<Box>();
    juce::Component::SafePointer<ContentRoutingPanel> safe (this);
    juce::MessageManager::callAsync ([safe, box, existing]
    {
        if (safe == nullptr) { box->done.signal(); return; }
        auto* w = new juce::AlertWindow ("File already exists",
            existing.getFullPathName() + "\n\nReplace sends the old file to the recycle bin.", juce::MessageBoxIconType::QuestionIcon);
        w->addButton ("Replace", 1);
        w->addButton ("Keep both", 2);
        w->addButton ("Skip", 3);
        w->addButton ("Stop import", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        w->enterModalState (true, juce::ModalCallbackFunction::create ([box] (int r)
        {
            box->answer = r == 1 ? "replace" : r == 2 ? "keep" : r == 3 ? "skip" : "cancel";
            box->done.signal();
        }), true);
    });
    while (! box->done.wait (200))
        if (cancelFlag.load()) return "cancel";
    return box->answer;
}

void ContentRoutingPanel::chooseRoot()
{
    chooser = std::make_unique<juce::FileChooser> ("Select your FL Studio user data folder (contains Presets, Projects...)", juce::File (config.userData));
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
        [this] (const juce::FileChooser& fc)
        {
            auto f = fc.getResult();
            if (! f.isDirectory()) return;
            config.userData = f.getFullPathName();
            rootEditor.setText (config.userData, false);
            log ("FL user data set to " + config.userData + " (press Save Settings to keep it).");
            repaint(); list.repaint();
        });
}

void ContentRoutingPanel::saveSettings()
{
    juce::String err;
    config.alwaysReplaceDuplicates = replaceToggle.getToggleState();
    if (RoutingConfig::save (config, configFile, err)) log ("Settings saved to " + configFile.getFullPathName());
    else log ("Could not save settings: " + err);
}

// ─── window ─────────────────────────────────────────────────────────────────

ContentRoutingWindow::ContentRoutingWindow()
    : DialogWindow ("Import FL Content", flo::ui::col::bg, true, true)
{
    setUsingNativeTitleBar (true);
    setContentOwned (new ContentRoutingPanel(), true);
    centreWithSize (900, 680);
    setResizable (true, true);
    setVisible (true);
}

void ContentRoutingWindow::closeButtonPressed()
{
    setVisible (false);
    delete this;
}
