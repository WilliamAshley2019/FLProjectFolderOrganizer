#pragma once
#include <JuceHeader.h>

// Forward-declare sqlite3 so we don't need the header in the .h file
struct sqlite3;
struct sqlite3_stmt;

class ProjectDatabase
{
public:
    struct ProjectEntry
    {
        juce::String path;
        juce::String version;
        juce::String groupName;
        juce::String hash;
        juce::String fileName;
        int64_t fileSize     = 0;
        int64_t lastModified = 0;
        bool    isZipped     = false;
        juce::String duplicateOf;
    };

    struct TransactionRecord
    {
        int64_t      id = 0;
        juce::String timestamp;
        juce::String action;
        juce::String sourcePath;
        juce::String destPath;
        bool         success = false;
        juce::String details;
    };

    struct SampleUsageRow
    {
        juce::String projectPath;
        juce::String channelName;
        juce::String samplePath;      // exactly as stored in the FLP
        juce::String sampleKey;       // normalised for matching (see NormaliseSampleKey)
    };

    ProjectDatabase();
    ~ProjectDatabase();

    bool Initialize(const juce::File& dbFile);
    bool AddProject(const ProjectEntry& entry);
    bool UpdateProject(const ProjectEntry& entry);
    bool DeleteProject(const juce::String& path);
    bool ProjectExists(const juce::String& path);
    std::vector<ProjectEntry>     GetAllProjects();
    std::vector<ProjectEntry>     FindDuplicates();
    std::vector<ProjectEntry>     FindByVersion(const juce::String& version);
    std::vector<ProjectEntry>     FindByGroup(const juce::String& group);
    std::vector<TransactionRecord> GetTransactions();
    bool AddTransaction(const TransactionRecord& record);
    bool UndoTransaction(int64_t id);

    // ---- Sample usage index (which projects reference which samples) ----
    // Lower-cases, converts / to \\ and trims, so lookups ignore case/slash style.
    static juce::String NormaliseSampleKey(const juce::String& path);
    // Atomically replaces every row for `projectPath`. Rows' projectPath is ignored.
    bool ReplaceSampleUsage(const juce::String& projectPath, const std::vector<SampleUsageRow>& rows);
    bool DeleteSampleUsage(const juce::String& projectPath);
    // Projects whose samples match: exact key, or (if substring) key LIKE %needle%.
    std::vector<SampleUsageRow> FindSampleUsers(const juce::String& samplePathOrFragment, bool substring);
    std::vector<SampleUsageRow> GetSampleUsage(const juce::String& projectPath);
    bool IsProjectIndexed(const juce::String& projectPath);

private:
    sqlite3* db = nullptr;

    bool ExecuteSQL(const juce::String& sql);
    bool CreateTables();

    // Bind helpers
    bool bindText(sqlite3_stmt* stmt, int col, const juce::String& val);
    bool bindInt64(sqlite3_stmt* stmt, int col, int64_t val);
    bool bindInt(sqlite3_stmt* stmt, int col, int val);

    ProjectEntry  rowToEntry(sqlite3_stmt* stmt);
    TransactionRecord rowToTransaction(sqlite3_stmt* stmt);
};
