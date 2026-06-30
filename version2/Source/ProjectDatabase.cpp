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
        "SELECT * FROM transactions ORDER BY id DESC LIMIT 100",
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

        if (dest.existsAsFile())
            return dest.moveFileTo(source);
    }

    return false; // DELETE cannot be undone without a backup
}
