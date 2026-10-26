#include "ProjectDatabase.h"

// sqlite3 amalgamation - add sqlite3.h and sqlite3.c to your Projucer file list.
// sqlite3.c must be compiled as C (not C++), which Projucer handles automatically
// when you add it as a plain .c file. Both files sit in the Source folder.
extern "C" {
#include "sqlite3.h"
}

ProjectDatabase::ProjectDatabase()  {}
ProjectDatabase::~ProjectDatabase()
{
    if (db != nullptr)
    {
        sqlite3_close(db);
        db = nullptr;
    }
}

bool ProjectDatabase::Initialize(const juce::File& dbFile)
{
    auto pathUTF8 = dbFile.getFullPathName().toUTF8();
    int rc = sqlite3_open(pathUTF8, &db);
    if (rc != SQLITE_OK)
    {
        db = nullptr;
        return false;
    }

    ExecuteSQL("PRAGMA foreign_keys = ON;");
    ExecuteSQL("PRAGMA journal_mode = WAL;");
    return CreateTables();
}

bool ProjectDatabase::CreateTables()
{
    const char* createProjects = R"(
        CREATE TABLE IF NOT EXISTS projects (
            id            INTEGER PRIMARY KEY AUTOINCREMENT,
            path          TEXT UNIQUE NOT NULL,
            file_name     TEXT,
            version       TEXT,
            group_name    TEXT,
            hash          TEXT,
            file_size     INTEGER,
            last_modified INTEGER,
            is_zipped     INTEGER DEFAULT 0,
            duplicate_of  TEXT,
            created_at    DATETIME DEFAULT CURRENT_TIMESTAMP,
            updated_at    DATETIME DEFAULT CURRENT_TIMESTAMP
        )
    )";

    const char* createTransactions = R"(
        CREATE TABLE IF NOT EXISTS transactions (
            id          INTEGER PRIMARY KEY AUTOINCREMENT,
            timestamp   DATETIME DEFAULT CURRENT_TIMESTAMP,
            action      TEXT NOT NULL,
            source_path TEXT NOT NULL,
            dest_path   TEXT,
            success     INTEGER DEFAULT 0,
            details     TEXT
        )
    )";

    if (!ExecuteSQL(createProjects))    return false;
    ExecuteSQL("CREATE INDEX IF NOT EXISTS idx_version ON projects(version)");
    ExecuteSQL("CREATE INDEX IF NOT EXISTS idx_group   ON projects(group_name)");
    ExecuteSQL("CREATE INDEX IF NOT EXISTS idx_hash    ON projects(hash)");
    if (!ExecuteSQL(createTransactions)) return false;
    // Added later: tracks transactions already undone. Fails harmlessly if the column exists.
    ExecuteSQL("ALTER TABLE transactions ADD COLUMN undone INTEGER DEFAULT 0");

    // sample_usage rows die with their project (foreign_keys is ON).
    const char* createSampleUsage = R"(
        CREATE TABLE IF NOT EXISTS sample_usage (
            id           INTEGER PRIMARY KEY AUTOINCREMENT,
            project_path TEXT NOT NULL REFERENCES projects(path) ON DELETE CASCADE ON UPDATE CASCADE,
            channel_name TEXT,
            sample_path  TEXT NOT NULL,
            sample_key   TEXT NOT NULL
        )
    )";
    if (!ExecuteSQL(createSampleUsage)) return false;
    ExecuteSQL("CREATE INDEX IF NOT EXISTS idx_usage_key     ON sample_usage(sample_key)");
    ExecuteSQL("CREATE INDEX IF NOT EXISTS idx_usage_project ON sample_usage(project_path)");
    // Marker table so IsProjectIndexed can tell "indexed, no samples" from "never indexed".
    ExecuteSQL("CREATE TABLE IF NOT EXISTS sample_usage_indexed (project_path TEXT PRIMARY KEY REFERENCES projects(path) ON DELETE CASCADE ON UPDATE CASCADE)");
    return true;
}

bool ProjectDatabase::ExecuteSQL(const juce::String& sql)
{
    if (db == nullptr) return false;
    char* errMsg = nullptr;
    int rc = sqlite3_exec(db, sql.toUTF8(), nullptr, nullptr, &errMsg);
    if (errMsg) sqlite3_free(errMsg);
    return rc == SQLITE_OK;
}

// ─── helpers ────────────────────────────────────────────────────────────────

bool ProjectDatabase::bindText(sqlite3_stmt* stmt, int col, const juce::String& val)
{
    auto utf = val.toUTF8();
    return sqlite3_bind_text(stmt, col, utf, -1, SQLITE_TRANSIENT) == SQLITE_OK;
}
bool ProjectDatabase::bindInt64(sqlite3_stmt* stmt, int col, int64_t val)
{
    return sqlite3_bind_int64(stmt, col, val) == SQLITE_OK;
}
bool ProjectDatabase::bindInt(sqlite3_stmt* stmt, int col, int val)
{
    return sqlite3_bind_int(stmt, col, val) == SQLITE_OK;
}

ProjectDatabase::ProjectEntry ProjectDatabase::rowToEntry(sqlite3_stmt* stmt)
{
    ProjectEntry e;
    // columns: 0=id,1=path,2=file_name,3=version,4=group_name,5=hash,
    //          6=file_size,7=last_modified,8=is_zipped,9=duplicate_of
    auto getText = [&](int col) -> juce::String {
        const char* t = (const char*) sqlite3_column_text(stmt, col);
        return t ? juce::String::fromUTF8(t) : juce::String();
    };
    e.path         = getText(1);
    e.fileName     = getText(2);
    e.version      = getText(3);
    e.groupName    = getText(4);
    e.hash         = getText(5);
    e.fileSize     = sqlite3_column_int64(stmt, 6);
    e.lastModified = sqlite3_column_int64(stmt, 7);
    e.isZipped     = sqlite3_column_int(stmt, 8) != 0;
    e.duplicateOf  = getText(9);
    return e;
}

ProjectDatabase::TransactionRecord ProjectDatabase::rowToTransaction(sqlite3_stmt* stmt)
{
    TransactionRecord r;
    auto getText = [&](int col) -> juce::String {
        const char* t = (const char*) sqlite3_column_text(stmt, col);
        return t ? juce::String::fromUTF8(t) : juce::String();
    };
    r.id         = sqlite3_column_int64(stmt, 0);
    r.timestamp  = getText(1);
    r.action     = getText(2);
    r.sourcePath = getText(3);
    r.destPath   = getText(4);
    r.success    = sqlite3_column_int(stmt, 5) != 0;
    r.details    = getText(6);
    return r;
}

// ─── CRUD ───────────────────────────────────────────────────────────────────

bool ProjectDatabase::AddProject(const ProjectEntry& e)
{
    if (db == nullptr) return false;

    const char* sql = R"(
        INSERT OR REPLACE INTO projects
        (path,file_name,version,group_name,hash,file_size,last_modified,is_zipped,duplicate_of,updated_at)
        VALUES (?,?,?,?,?,?,?,?,?,CURRENT_TIMESTAMP)
    )";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;

    bindText (stmt, 1, e.path);
    bindText (stmt, 2, e.fileName);
    bindText (stmt, 3, e.version);
    bindText (stmt, 4, e.groupName);
    bindText (stmt, 5, e.hash);
    bindInt64(stmt, 6, e.fileSize);
    bindInt64(stmt, 7, e.lastModified);
    bindInt  (stmt, 8, e.isZipped ? 1 : 0);
    bindText (stmt, 9, e.duplicateOf);

    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

bool ProjectDatabase::UpdateProject(const ProjectEntry& e)
{
    return AddProject(e); // INSERT OR REPLACE handles upsert
}

bool ProjectDatabase::DeleteProject(const juce::String& path)
{
    if (db == nullptr) return false;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, "DELETE FROM projects WHERE path=?", -1, &stmt, nullptr) != SQLITE_OK)
        return false;
    bindText(stmt, 1, path);
    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

bool ProjectDatabase::ProjectExists(const juce::String& path)
{
    if (db == nullptr) return false;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM projects WHERE path=?", -1, &stmt, nullptr) != SQLITE_OK)
        return false;
    bindText(stmt, 1, path);
    bool exists = false;
    if (sqlite3_step(stmt) == SQLITE_ROW)
        exists = sqlite3_column_int(stmt, 0) > 0;
    sqlite3_finalize(stmt);
    return exists;
}

std::vector<ProjectDatabase::ProjectEntry> ProjectDatabase::GetAllProjects()
{
    std::vector<ProjectEntry> results;
    if (db == nullptr) return results;

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db,
        "SELECT * FROM projects ORDER BY group_name, file_name",
        -1, &stmt, nullptr) != SQLITE_OK)
        return results;

    while (sqlite3_step(stmt) == SQLITE_ROW)
        results.push_back(rowToEntry(stmt));

    sqlite3_finalize(stmt);
    return results;
}

std::vector<ProjectDatabase::ProjectEntry> ProjectDatabase::FindDuplicates()
{
    std::vector<ProjectEntry> results;
    if (db == nullptr) return results;

    const char* sql = R"(
        SELECT * FROM projects
        WHERE hash IN (SELECT hash FROM projects GROUP BY hash HAVING COUNT(*) > 1)
        ORDER BY hash, path
    )";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        return results;

    while (sqlite3_step(stmt) == SQLITE_ROW)
        results.push_back(rowToEntry(stmt));

    sqlite3_finalize(stmt);
    return results;
}

std::vector<ProjectDatabase::ProjectEntry> ProjectDatabase::FindByVersion(const juce::String& version)
{
    std::vector<ProjectEntry> results;
    if (db == nullptr) return results;

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db,
        "SELECT * FROM projects WHERE version=? ORDER BY file_name",
        -1, &stmt, nullptr) != SQLITE_OK)
        return results;

    bindText(stmt, 1, version);
    while (sqlite3_step(stmt) == SQLITE_ROW)
        results.push_back(rowToEntry(stmt));

    sqlite3_finalize(stmt);
    return results;
}

std::vector<ProjectDatabase::ProjectEntry> ProjectDatabase::FindByGroup(const juce::String& group)
{
    std::vector<ProjectEntry> results;
    if (db == nullptr) return results;

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db,
        "SELECT * FROM projects WHERE group_name=? ORDER BY file_name",
        -1, &stmt, nullptr) != SQLITE_OK)
        return results;

    bindText(stmt, 1, group);
    while (sqlite3_step(stmt) == SQLITE_ROW)
        results.push_back(rowToEntry(stmt));

    sqlite3_finalize(stmt);
    return results;
}

bool ProjectDatabase::AddTransaction(const TransactionRecord& r)
{
    if (db == nullptr) return false;

    const char* sql = R"(
        INSERT INTO transactions (action,source_path,dest_path,success,details)
        VALUES (?,?,?,?,?)
    )";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        return false;

    bindText(stmt, 1, r.action);
    bindText(stmt, 2, r.sourcePath);
    bindText(stmt, 3, r.destPath);
    bindInt (stmt, 4, r.success ? 1 : 0);
    bindText(stmt, 5, r.details);

    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

std::vector<ProjectDatabase::TransactionRecord> ProjectDatabase::GetTransactions()
{
    std::vector<TransactionRecord> results;
    if (db == nullptr) return results;

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db,
        "SELECT id,timestamp,action,source_path,dest_path,success,details FROM transactions WHERE COALESCE(undone,0)=0 ORDER BY id DESC LIMIT 100",
        -1, &stmt, nullptr) != SQLITE_OK)
        return results;

    while (sqlite3_step(stmt) == SQLITE_ROW)
        results.push_back(rowToTransaction(stmt));

    sqlite3_finalize(stmt);
    return results;
}

bool ProjectDatabase::UndoTransaction(int64_t id)
{
    if (db == nullptr) return false;

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db,
        "SELECT action,source_path,dest_path FROM transactions WHERE id=?",
        -1, &stmt, nullptr) != SQLITE_OK)
        return false;

    bindInt64(stmt, 1, id);
    if (sqlite3_step(stmt) != SQLITE_ROW)
    {
        sqlite3_finalize(stmt);
        return false;
    }

    auto getText = [&](int col) -> juce::String {
        const char* t = (const char*) sqlite3_column_text(stmt, col);
        return t ? juce::String::fromUTF8(t) : juce::String();
    };

    juce::String action     = getText(0);
    juce::String sourcePath = getText(1);
    juce::String destPath   = getText(2);
    sqlite3_finalize(stmt);

    if (action == "MOVE" || action == "COPY")
    {
        juce::File source(sourcePath);
        juce::File dest(destPath);

        // Never overwrite a file that has since reappeared at the original path.
        if (dest.existsAsFile() && !source.existsAsFile())
        {
            source.getParentDirectory().createDirectory();
            if (dest.moveFileTo(source))
            {
                sqlite3_stmt* mark = nullptr;
                if (sqlite3_prepare_v2(db, "UPDATE transactions SET undone=1 WHERE id=?", -1, &mark, nullptr) == SQLITE_OK)
                {
                    bindInt64(mark, 1, id);
                    sqlite3_step(mark);
                    sqlite3_finalize(mark);
                }
                return true;
            }
        }
    }

    return false; // DELETE cannot be undone without a backup
}


// ─── Sample usage index ─────────────────────────────────────────────────────

juce::String ProjectDatabase::NormaliseSampleKey(const juce::String& path)
{
    return path.trim().replaceCharacter('/', '\\').toLowerCase();
}

bool ProjectDatabase::ReplaceSampleUsage(const juce::String& projectPath, const std::vector<SampleUsageRow>& rows)
{
    if (db == nullptr) return false;
    if (!ExecuteSQL("BEGIN IMMEDIATE")) return false;

    auto rollback = [&] { ExecuteSQL("ROLLBACK"); return false; };

    sqlite3_stmt* del = nullptr;
    if (sqlite3_prepare_v2(db, "DELETE FROM sample_usage WHERE project_path = ?", -1, &del, nullptr) != SQLITE_OK) return rollback();
    bindText(del, 1, projectPath);
    bool ok = sqlite3_step(del) == SQLITE_DONE;
    sqlite3_finalize(del);
    if (!ok) return rollback();

    sqlite3_stmt* ins = nullptr;
    if (sqlite3_prepare_v2(db,
        "INSERT INTO sample_usage(project_path, channel_name, sample_path, sample_key) VALUES(?,?,?,?)",
        -1, &ins, nullptr) != SQLITE_OK) return rollback();
    for (auto& r : rows)
    {
        sqlite3_reset(ins);
        bindText(ins, 1, projectPath);
        bindText(ins, 2, r.channelName);
        bindText(ins, 3, r.samplePath);
        bindText(ins, 4, NormaliseSampleKey(r.samplePath));
        if (sqlite3_step(ins) != SQLITE_DONE) { sqlite3_finalize(ins); return rollback(); }
    }
    sqlite3_finalize(ins);

    sqlite3_stmt* mark = nullptr;
    if (sqlite3_prepare_v2(db, "INSERT OR IGNORE INTO sample_usage_indexed(project_path) VALUES(?)", -1, &mark, nullptr) != SQLITE_OK) return rollback();
    bindText(mark, 1, projectPath);
    ok = sqlite3_step(mark) == SQLITE_DONE;
    sqlite3_finalize(mark);
    if (!ok) return rollback();

    return ExecuteSQL("COMMIT");
}

bool ProjectDatabase::DeleteSampleUsage(const juce::String& projectPath)
{
    if (db == nullptr) return false;
    bool ok = true;
    for (auto* sql : { "DELETE FROM sample_usage WHERE project_path = ?",
                       "DELETE FROM sample_usage_indexed WHERE project_path = ?" })
    {
        sqlite3_stmt* s = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &s, nullptr) != SQLITE_OK) { ok = false; continue; }
        bindText(s, 1, projectPath);
        ok = (sqlite3_step(s) == SQLITE_DONE) && ok;
        sqlite3_finalize(s);
    }
    return ok;
}

static std::vector<ProjectDatabase::SampleUsageRow> readUsageRows(sqlite3_stmt* s)
{
    std::vector<ProjectDatabase::SampleUsageRow> out;
    auto txt = [&](int c) { const char* t = (const char*) sqlite3_column_text(s, c); return t ? juce::String::fromUTF8(t) : juce::String(); };
    while (sqlite3_step(s) == SQLITE_ROW)
        out.push_back({ txt(0), txt(1), txt(2), txt(3) });
    return out;
}

std::vector<ProjectDatabase::SampleUsageRow> ProjectDatabase::FindSampleUsers(const juce::String& needle, bool substring)
{
    std::vector<SampleUsageRow> out;
    if (db == nullptr || needle.trim().isEmpty()) return out;
    sqlite3_stmt* s = nullptr;
    const char* sql = substring
        ? "SELECT project_path, channel_name, sample_path, sample_key FROM sample_usage WHERE sample_key LIKE ? ESCAPE '\\' ORDER BY project_path"
        : "SELECT project_path, channel_name, sample_path, sample_key FROM sample_usage WHERE sample_key = ? ORDER BY project_path";
    if (sqlite3_prepare_v2(db, sql, -1, &s, nullptr) != SQLITE_OK) return out;
    auto key = NormaliseSampleKey(needle);
    if (substring)
        key = "%" + key.replace("\\", "\\\\").replace("%", "\\%").replace("_", "\\_") + "%";
    bindText(s, 1, key);
    out = readUsageRows(s);
    sqlite3_finalize(s);
    return out;
}

std::vector<ProjectDatabase::SampleUsageRow> ProjectDatabase::GetSampleUsage(const juce::String& projectPath)
{
    std::vector<SampleUsageRow> out;
    if (db == nullptr) return out;
    sqlite3_stmt* s = nullptr;
    if (sqlite3_prepare_v2(db, "SELECT project_path, channel_name, sample_path, sample_key FROM sample_usage WHERE project_path = ? ORDER BY id", -1, &s, nullptr) != SQLITE_OK) return out;
    bindText(s, 1, projectPath);
    out = readUsageRows(s);
    sqlite3_finalize(s);
    return out;
}

bool ProjectDatabase::IsProjectIndexed(const juce::String& projectPath)
{
    if (db == nullptr) return false;
    sqlite3_stmt* s = nullptr;
    if (sqlite3_prepare_v2(db, "SELECT 1 FROM sample_usage_indexed WHERE project_path = ?", -1, &s, nullptr) != SQLITE_OK) return false;
    bindText(s, 1, projectPath);
    bool found = sqlite3_step(s) == SQLITE_ROW;
    sqlite3_finalize(s);
    return found;
}
