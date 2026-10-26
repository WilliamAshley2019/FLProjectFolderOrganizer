#include "FileTransfer.h"
#include "FSTInspector.h"
#include "FXPInspector.h"
#include "H2PInspector.h"
#include "PluginDestinations.h"
#include "SafeFileOperations.h"
#include <set>
#if JUCE_WINDOWS
 #include <windows.h>
#endif

namespace FileTransfer
{
namespace
{
    struct Cancelled {};
    struct TransferError { juce::String message; };

    void checkCancel (const std::atomic<bool>* c) { if (c != nullptr && c->load()) throw Cancelled(); }
    juce::String ext (const juce::String& rel) { return rel.fromLastOccurrenceOf (".", true, false).toLowerCase(); }
    juce::String baseName (const juce::String& rel) { return rel.fromLastOccurrenceOf ("/", false, false); }

    bool isGenericWrapper (const juce::String& n) { return n.isEmpty() || FSTInspector::isWrapperName (n); }

    // Identity for one file, from its bytes (already bounded to a prefix) or from disk.
    void identifyFromBytes (const juce::String& rel, const juce::uint8* data, size_t n, juce::int64 fullSize,
                            InstalledPluginLookup* lookup, const std::atomic<bool>& cancel, FileInfo& fi)
    {
        const auto e = ext (rel);
        if (e == ".fst")
        {
            auto m = FSTInspector::inspectBytes (data, n, fullSize);
            fi.pluginName = m.pluginName; fi.pluginKind = m.pluginKind; fi.fstType = m.fstType;
            fi.displayName = m.displayName; fi.hostedName = m.hostedPluginName;
            if (! m.ok()) fi.error = m.reason;
        }
        else if (PluginDestinations::isNativePresetExtension (e))
        {
            fi.pluginName = PluginDestinations::nativePluginForExtension (e);
            fi.pluginKind = "generator";
        }
        else if (e == ".h2p")
        {
            juce::String err;
            auto name = H2PInspector::pluginName (data, n, err);
            InstalledPluginLookup::FstIdentity id;
            if (name.isEmpty()) fi.error = err;
            else if (lookup == nullptr) { fi.pluginName = name; fi.error = "Plugin database not available"; }
            else if (lookup->resolveName (name, id, err, &cancel))
            { fi.pluginName = id.meta.pluginName; fi.pluginKind = id.kind; fi.fstType = "plugin"; fi.hostedName = id.meta.hostedPluginName; fi.databaseName = id.databaseName; }
            else fi.error = err;
        }
        else if (e == ".fxp")
        {
            auto m = FXPInspector::inspectBytes (data, n, fullSize);
            if (m.pluginId.isEmpty()) { fi.error = m.reason; return; }
            fi.pluginName = m.pluginName; fi.pluginKind = m.pluginKind;
            if (lookup != nullptr)
            {
                InstalledPluginLookup::FstIdentity id; juce::String err;
                if (lookup->resolveFxp (m, id, err, &cancel))
                { fi.pluginName = id.meta.pluginName; fi.pluginKind = id.kind; fi.fstType = "plugin"; fi.hostedName = id.meta.hostedPluginName; fi.databaseName = id.databaseName; fi.error.clear(); }
                else if (fi.pluginName.isEmpty()) fi.error = err;
            }
            else if (fi.pluginName.isEmpty()) fi.error = m.reason;
        }
    }

    bool needsPrefix (const juce::String& rel)
    {
        auto e = ext (rel);
        return e == ".fst" || e == ".h2p" || e == ".fxp";
    }
    int prefixBytes (const juce::String& rel)
    {
        auto e = ext (rel);
        return e == ".fst" ? FSTInspector::kMaxPrefixBytes : e == ".h2p" ? H2PInspector::kHeaderBytes : FXPInspector::kHeaderBytes;
    }

    void identifyFile (const juce::File& f, const juce::String& rel, InstalledPluginLookup* lookup,
                       const std::atomic<bool>& cancel, Package& p)
    {
        if (! needsPrefix (rel) && ! PluginDestinations::isNativePresetExtension (ext (rel))) return;
        FileInfo fi;
        if (needsPrefix (rel))
        {
            juce::FileInputStream in (f);
            if (! in.openedOk()) { fi.error = "Cannot read file"; p.info[rel] = fi; return; }
            const int want = prefixBytes (rel);
            juce::HeapBlock<juce::uint8> buf ((size_t) want);
            const int got = in.read (buf.get(), want);
            identifyFromBytes (rel, buf.get(), (size_t) juce::jmax (0, got), in.getTotalLength(), lookup, cancel, fi);
        }
        else identifyFromBytes (rel, nullptr, 0, 0, lookup, cancel, fi);
        p.info[rel] = fi;
    }
}

juce::String Package::routePath() const
{
    auto has = [&] (const char* e) { for (auto& f : plannedFiles()) if (ext (f) == e) return true; return false; };
    if (isFstGroup)
    {
        if (has (".h2p") && ! has (".fst")) return "preset.h2p";
        if (has (".fxp") && ! has (".fst")) return "preset.fxp";
        return "preset.fst";
    }
    return source.getFileName();
}

juce::String Package::pluginLabel() const
{
    auto label = pluginName;
    if (pluginName == "Patcher" && pluginKind.isNotEmpty()) label << " (" << pluginKind.substring (0, 1).toUpperCase() << pluginKind.substring (1) << ")";
    if (label.isNotEmpty() && fstType == "channel") label << " (Channel state)";
    return label;
}

// ─── ZIP validation ─────────────────────────────────────────────────────────

juce::String validateZip (const juce::File& zipFile, const Limits& lim, juce::StringArray* filesOut,
                          juce::StringArray* dirsOut, const std::atomic<bool>* cancel)
{
    if (SafeFileOperations::HasSymlinkAncestor (zipFile)) return "Links/reparse points are not allowed in the archive's path.";
    juce::ZipFile zip (zipFile);
    const int n = zip.getNumEntries();
    if (n <= 0) return "Archive is empty or unreadable.";
    if (n > lim.entries) return "ZIP exceeds entry-count limit.";

    std::map<juce::String, bool> kinds;    // lower-cased relative path -> isDirectory
    juce::int64 expanded = 0;
    juce::StringArray files, dirs;
    for (int i = 0; i < n; ++i)
    {
        checkCancel (cancel);
        auto* e = zip.getEntry (i);
        if (e == nullptr) return "Unreadable ZIP entry.";
        auto name = e->filename.replaceCharacter ('\\', '/');
        const bool isDir = name.endsWithChar ('/');
        auto reason = SafeFileOperations::GetSafeRelativePathRejectionReason (name);
        if (reason != "OK") return "Unsafe entry '" + name + "': " + reason;
        auto rel = name.trimCharactersAtEnd ("/");
        auto key = rel.toLowerCase();
        if (kinds.count (key)) return "ZIP has colliding names: " + rel;
        kinds[key] = isDir;
        if (e->isSymbolicLink) return "ZIP contains a link entry: " + rel;
        expanded += e->uncompressedSize;
        if (expanded > lim.bytes) return "ZIP exceeds the expanded-size limit.";
        (isDir ? dirs : files).add (rel);
    }
    for (auto& kv : kinds)
    {
        juce::String parent = kv.first;
        while (parent.contains ("/"))
        {
            parent = parent.upToLastOccurrenceOf ("/", false, false);
            auto it = kinds.find (parent);
            if (it != kinds.end() && ! it->second) return "ZIP file/directory collision: " + kv.first;
        }
    }
    // The API exposes no per-entry compressed size, so judge the whole archive,
    // and only once it is big enough for a bomb to matter.
    const auto archiveSize = juce::jmax<juce::int64> (1, zipFile.getSize());
    if (expanded > 64LL * 1024 * 1024 && expanded / archiveSize > lim.ratio) return "ZIP exceeds the compression-ratio limit.";

    if (filesOut) *filesOut = files;
    if (dirsOut) *dirsOut = dirs;
    return {};
}

// ─── scan ───────────────────────────────────────────────────────────────────

bool scan (const juce::File& source, const Limits& lim, const std::atomic<bool>& cancel,
           InstalledPluginLookup* lookup, Package& out, juce::String& error)
{
    try
    {
        checkCancel (&cancel);
        if (SafeFileOperations::HasSymlinkAncestor (source)) { error = "Links/reparse points are not allowed: " + source.getFullPathName(); return false; }
        Package p; p.source = source;

        if (source.existsAsFile() && source.hasFileExtension (".zip"))
        {
            p.isZip = true;
            auto bad = validateZip (source, lim, &p.files, &p.directories, &cancel);
            if (bad.isNotEmpty()) { error = bad; return false; }

            juce::ZipFile zip (source);
            juce::int64 metaBytes = 0;
            for (int i = 0; i < zip.getNumEntries(); ++i)
            {
                checkCancel (&cancel);
                auto* e = zip.getEntry (i);
                auto rel = e->filename.replaceCharacter ('\\', '/').trimCharactersAtEnd ("/");
                if (e->filename.endsWithChar ('/') || ! (needsPrefix (rel) || PluginDestinations::isNativePresetExtension (ext (rel)))) continue;
                FileInfo fi;
                if (needsPrefix (rel))
                {
                    std::unique_ptr<juce::InputStream> s (zip.createStreamForEntry (i));
                    if (s == nullptr) { fi.error = "Cannot read entry"; p.info[rel] = fi; continue; }
                    const int want = (int) juce::jmin<juce::int64> (prefixBytes (rel), e->uncompressedSize);
                    juce::HeapBlock<juce::uint8> buf ((size_t) juce::jmax (1, want));
                    const int got = s->read (buf.get(), want);
                    metaBytes += got;
                    if (metaBytes > lim.bytes) { error = "ZIP metadata scan exceeded the byte limit."; return false; }
                    identifyFromBytes (rel, buf.get(), (size_t) juce::jmax (0, got), e->uncompressedSize, lookup, cancel, fi);
                }
                else identifyFromBytes (rel, nullptr, 0, 0, lookup, cancel, fi);
                p.info[rel] = fi;
            }
            std::set<juce::String> tops;
            bool allNested = true;
            for (auto& f : p.files) { tops.insert (f.upToFirstOccurrenceOf ("/", false, false)); if (! f.contains ("/")) allNested = false; }
            for (auto& d : p.directories) tops.insert (d.upToFirstOccurrenceOf ("/", false, false));
            if (tops.size() == 1 && allNested && ! p.files.isEmpty()) p.zipRootFolder = *tops.begin();
            out = std::move (p);
            return true;
        }

        if (source.existsAsFile())
        {
            auto name = source.getFileName();
            if (SafeFileOperations::GetSafeRelativePathRejectionReason (name) != "OK") { error = "Unsafe file name: " + name; return false; }
            p.files.add (name);
            identifyFile (source, name, lookup, cancel, p);
            if (auto it = p.info.find (name); it != p.info.end())
            {
                auto& fi = it->second;
                p.pluginName = fi.pluginName; p.pluginKind = fi.pluginKind; p.fstType = fi.fstType;
                p.displayName = fi.displayName; p.hostedName = fi.hostedName; p.databaseName = fi.databaseName;
            }
            out = std::move (p);
            return true;
        }

        if (! source.isDirectory()) { error = "Source is not a regular file or folder: " + source.getFullPathName(); return false; }

        std::vector<juce::File> stack { source };
        while (! stack.empty())
        {
            auto dir = stack.back(); stack.pop_back();
            for (const auto& entry : juce::RangedDirectoryIterator (dir, false, "*", juce::File::findFilesAndDirectories))
            {
                checkCancel (&cancel);
                auto f = entry.getFile();
                auto rel = f.getRelativePathFrom (source).replaceCharacter ('\\', '/');
                if (f.isSymbolicLink()) { p.skipped.add (rel + ": link skipped"); continue; }
                if (SafeFileOperations::GetSafeRelativePathRejectionReason (rel) != "OK")
                { p.skipped.add (rel + ": unsafe Windows file name skipped"); continue; }
                if (entry.isDirectory()) { p.directories.add (rel); stack.push_back (f); }
                else
                {
                    p.files.add (rel);
                    identifyFile (f, rel, lookup, cancel, p);
                }
                if (p.files.size() > lim.entries) { error = "Folder exceeds the entry-count limit."; return false; }
            }
        }
        p.files.sort (true); p.directories.sort (true);
        out = std::move (p);
        return true;
    }
    catch (const Cancelled&) { error = "Cancelled"; return false; }
    catch (const TransferError& e) { error = e.message; return false; }
}

// ─── copy ───────────────────────────────────────────────────────────────────

namespace
{
    // Atomic, no-clobber commit. Returns false if the target now exists.
    bool commitNoClobber (const juce::File& staged, const juce::File& target)
    {
       #if JUCE_WINDOWS
        return MoveFileExW (staged.getFullPathName().toWideCharPointer(), target.getFullPathName().toWideCharPointer(), 0) != 0;
       #else
        if (target.exists()) return false;
        return staged.moveFileTo (target);
       #endif
    }

    bool underRoot (const juce::File& root, const juce::File& target)
    { return target == root || target.isAChildOf (root); }

    void validateTarget (const juce::File& root, const juce::File& target)
    {
        if (! root.isAbsolutePath (root.getFullPathName()) || ! root.isDirectory())
            throw TransferError { "Configured root must be an existing folder: " + root.getFullPathName() };
        if (! underRoot (root, target)) throw TransferError { "Destination escapes the configured root: " + target.getFullPathName() };
        if (SafeFileOperations::HasSymlinkAncestor (target, root))
            throw TransferError { "Links/reparse points are not allowed in the destination: " + target.getFullPathName() };
    }

    juce::File findCaseInsensitive (const juce::File& target)
    {
        auto parent = target.getParentDirectory();
        if (! parent.isDirectory()) return {};
        for (const auto& e : juce::RangedDirectoryIterator (parent, false, "*", juce::File::findFilesAndDirectories))
            if (e.getFile().getFileName().equalsIgnoreCase (target.getFileName())) return e.getFile();
        return {};
    }

    // returns "copied" or "skipped"
    juce::String copyFile (const juce::File& source, juce::File target, const juce::File& root,
                           const std::atomic<bool>& cancel, const ConflictFn& conflict, const ProgressFn& progress)
    {
        if (SafeFileOperations::HasSymlinkAncestor (source)) throw TransferError { "Source path contains a link: " + source.getFullPathName() };
        if (! source.existsAsFile()) throw TransferError { "Source is no longer a regular file: " + source.getFullPathName() };
        validateTarget (root, target);
        target.getParentDirectory().createDirectory();
        validateTarget (root, target);

        auto staged = target.getParentDirectory().getNonexistentChildFile (".flo-" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt()), ".tmp", false);
        struct Cleanup { juce::File f; ~Cleanup() { f.deleteFile(); } } cleanup { staged };

        {
            juce::FileInputStream in (source);
            juce::FileOutputStream out (staged);
            if (! in.openedOk() || ! out.openedOk()) throw TransferError { "Could not open for copy: " + source.getFullPathName() };
            out.setPosition (0); out.truncate();
            juce::HeapBlock<char> buf (1 << 20);
            for (;;)
            {
                checkCancel (&cancel);
                const int n = in.read (buf.get(), 1 << 20);
                if (n <= 0) break;
                if (! out.write (buf.get(), (size_t) n)) throw TransferError { "Write failed (disk full?): " + staged.getFullPathName() };
                progress ("Copying " + source.getFileName());
            }
            out.flush();
            if (out.getStatus().failed()) throw TransferError { "Write failed: " + staged.getFullPathName() };
        }
        staged.setLastModificationTime (source.getLastModificationTime());

        for (int attempt = 0; attempt < 50; ++attempt)
        {
            checkCancel (&cancel);
            validateTarget (root, target);
            auto existing = findCaseInsensitive (target);
            if (existing != juce::File())
            {
                if (existing.isSymbolicLink() || existing.isDirectory()) throw TransferError { "Destination is a link or folder: " + existing.getFullPathName() };
                if (existing == source) throw TransferError { "Source and destination are the same file." };

                auto fingerprint = std::make_pair (existing.getSize(), existing.getLastModificationTime().toMilliseconds());
                auto action = conflict (existing);
                checkCancel (&cancel);
                if (action == "cancel") throw Cancelled();
                if (action == "skip") return "skipped";
                if (action == "keep")
                {
                    auto stem = target.getFileNameWithoutExtension(), x = target.getFileExtension();
                    for (int i = 2; findCaseInsensitive (target) != juce::File(); ++i)
                        target = target.getSiblingFile (stem + " (" + juce::String (i) + ")" + x);
                    continue;
                }
                if (action != "replace") throw TransferError { "Unknown conflict action: " + action };
                validateTarget (root, existing);
                if (! existing.exists() || fingerprint != std::make_pair (existing.getSize(), existing.getLastModificationTime().toMilliseconds()))
                    continue;                                   // it changed while the user was deciding: ask again
                // Recoverable: the old file goes to the recycle bin, never straight to oblivion.
                if (! existing.moveToTrash()) throw TransferError { "Could not move the existing file to the recycle bin: " + existing.getFullPathName() };
            }
            if (commitNoClobber (staged, target)) return "copied";
        }
        throw TransferError { "Destination kept changing; giving up on " + target.getFullPathName() };
    }

    void extractZip (const Package& p, const juce::File& stage, const Limits& lim, const std::atomic<bool>& cancel, const ProgressFn& progress)
    {
        juce::StringArray files;
        auto bad = validateZip (p.source, lim, &files, nullptr, &cancel);      // revalidate right before extraction
        if (bad.isNotEmpty()) throw TransferError { bad };
        if (files != p.files) throw TransferError { "Archive changed after its import plan was shown. Please import again." };

        juce::ZipFile zip (p.source);
        juce::int64 total = 0;
        for (int i = 0; i < zip.getNumEntries(); ++i)
        {
            checkCancel (&cancel);
            auto* e = zip.getEntry (i);
            auto name = e->filename.replaceCharacter ('\\', '/');
            auto rel = name.trimCharactersAtEnd ("/");
            auto dest = stage.getChildFile (rel);
            if (name.endsWithChar ('/')) { dest.createDirectory(); continue; }
            dest.getParentDirectory().createDirectory();
            progress ("Extracting " + rel);
            std::unique_ptr<juce::InputStream> in (zip.createStreamForEntry (i));
            if (in == nullptr || dest.exists()) throw TransferError { "Cannot extract " + rel };
            juce::FileOutputStream out (dest);
            if (! out.openedOk()) throw TransferError { "Cannot create " + dest.getFullPathName() };
            juce::HeapBlock<char> buf (1 << 20);
            juce::int64 written = 0;
            for (;;)
            {
                checkCancel (&cancel);
                const int n = in->read (buf.get(), 1 << 20);
                if (n <= 0) break;
                total += n; written += n;
                if (total > lim.bytes || written > e->uncompressedSize) throw TransferError { "ZIP exceeded its declared size while extracting." };
                out.write (buf.get(), (size_t) n);
            }
        }
    }
}

Result runJobs (std::vector<Job>& jobs, const Limits& lim, const std::atomic<bool>& cancel,
                const ConflictFn& conflict, const ProgressFn& progress)
{
    Result r;
    int totalFiles = 0, processed = 0;
    for (auto& j : jobs) totalFiles += j.keepZip ? 1 : j.package.plannedFiles().size();

    std::map<juce::String, juce::File> stages;                 // zip path -> staging dir
    std::vector<std::unique_ptr<juce::TemporaryFile>> temps;
    std::set<juce::String> reportedSkips;

    for (auto& job : jobs)
    {
        auto& p = job.package;
        if (reportedSkips.insert (p.source.getFullPathName()).second)
        {
            r.skipped += p.skipped.size();
            r.details.addArray (p.skipped);
        }
        juce::StringArray paths;
        if (job.keepZip) paths.add (p.source.getFileName()); else paths = p.plannedFiles();

        try
        {
            checkCancel (&cancel);
            validateTarget (job.root, job.target);
            if (p.isFstGroup && job.keepZip) throw TransferError { "A routed preset group cannot keep the whole ZIP as one file." };
            if (! job.keepZip) for (auto& f : paths) if (! p.files.contains (f)) throw TransferError { "Selected files are not part of the validated package." };
            if (p.source.isDirectory() && (job.target == p.source || job.target.isAChildOf (p.source)))
                throw TransferError { "Destination is inside the source folder: " + job.target.getFullPathName() };

            juce::File sourceRoot = p.source;
            if (p.isZip && ! job.keepZip)
            {
                auto key = p.source.getFullPathName();
                if (stages.find (key) == stages.end())
                {
                    temps.push_back (std::make_unique<juce::TemporaryFile> (juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("flo-stage-" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt64()))));
                    auto dir = temps.back()->getFile();
                    dir.createDirectory();
                    extractZip (p, dir, lim, cancel, progress);
                    stages[key] = dir;
                }
                sourceRoot = stages[key];
            }

            // Recreate (empty) folders that belong to the plan.
            if (! job.keepZip && (p.source.isDirectory() || p.isZip))
            {
                juce::StringArray dirs;
                for (auto& d : p.directories)
                {
                    bool needed = ! p.hasSelection;
                    if (! needed) for (auto& f : paths) if (f.startsWith (d + "/")) { needed = true; break; }
                    if (needed) dirs.add (d);
                }
                job.target.createDirectory();
                for (auto& d : dirs)
                {
                    auto dd = job.target.getChildFile (d);
                    validateTarget (job.root, dd);
                    dd.createDirectory();
                }
            }

            for (auto& rel : paths)
            {
                checkCancel (&cancel);
                const bool fromTree = p.source.isDirectory() || (p.isZip && ! job.keepZip);
                auto src = fromTree ? sourceRoot.getChildFile (rel) : p.source;
                auto dst = job.target.getChildFile (rel);
                try
                {
                    auto state = copyFile (src, dst, job.root, cancel, conflict, progress);
                    if (state == "copied") ++r.copied; else ++r.skipped;
                    r.details.add ((state == "copied" ? "Copied: " : "Skipped: ") + src.getFullPathName() + "  ->  " + dst.getFullPathName());
                }
                catch (const TransferError& e)
                {
                    ++r.failed;
                    r.details.add ("Failed: " + src.getFullPathName() + "  ->  " + dst.getFullPathName() + ": " + e.message);
                }
                ++processed;
                progress (juce::String (processed) + "/" + juce::String (totalFiles) + " files processed");
            }
        }
        catch (const Cancelled&)
        {
            r.cancelled = totalFiles - processed;
            r.details.add ("Cancelled remaining files; copies already made were kept.");
            break;
        }
        catch (const TransferError& e)
        {
            const int remaining = juce::jmax (1, paths.size());
            r.failed += remaining; processed += remaining;
            r.details.add ("Failed package " + p.source.getFullPathName() + ": " + e.message);
        }
    }
    return r;
}
}
