#ifndef CASPER_SESSION_MANAGER_H
#define CASPER_SESSION_MANAGER_H

#include <string>
#include <vector>
#include <memory>
#include <chrono>
#include <sqlite3.h>
#include "json.hpp"

namespace casper {

using json = nlohmann::json;

// Represents a single message in the conversation
struct Message {
    std::string role;        // "user", "assistant", "tool"
    std::string content;
    std::string timestamp;

    json toJson() const;
    static Message fromJson(const json& j);
};

// Represents a tool execution
struct ToolExecution {
    std::string tool_name;
    json parameters;
    std::string output;
    int exit_code;
    std::string timestamp;

    json toJson() const;
    static ToolExecution fromJson(const json& j);
};

// Represents a file modification
struct FileModification {
    std::string file_path;
    std::string operation;  // "read", "write", "edit"
    std::string timestamp;

    json toJson() const;
    static FileModification fromJson(const json& j);
};

// Represents a complete session
struct Session {
    std::string session_id;
    std::string created_at;
    std::string updated_at;
    std::string model;
    std::vector<Message> messages;
    std::vector<ToolExecution> tool_executions;
    std::vector<FileModification> file_modifications;
    std::string working_directory;
    std::string summary;
    bool is_active;

    json toJson() const;
    static Session fromJson(const json& j);
};

class SessionManager {
public:
    SessionManager();
    ~SessionManager();

    // Initialize session manager with database
    bool initialize(const std::string& db_path = "");

    // Session lifecycle
    std::string createSession(const std::string& model, const std::string& working_dir);
    bool loadSession(const std::string& session_id);
    bool saveSession();
    bool closeSession();

    // Get current session
    Session* getCurrentSession() { return current_session_.get(); }
    const Session* getCurrentSession() const { return current_session_.get(); }

    // Message management
    void addUserMessage(const std::string& content);
    void addAssistantMessage(const std::string& content);
    void addToolMessage(const std::string& tool_name, const std::string& content);

    // Tool execution tracking
    void recordToolExecution(const std::string& tool_name,
                           const json& parameters,
                           const std::string& output,
                           int exit_code);

    // File modification tracking
    void recordFileModification(const std::string& file_path,
                               const std::string& operation);

    // Get conversation context for AI (last N messages)
    std::vector<Message> getConversationContext(int max_messages = 10) const;

    // Session listing and management
    std::vector<std::string> listSessions() const;
    std::vector<std::string> listActiveSessions() const;
    std::string getLastActiveSession() const;
    bool deleteSession(const std::string& session_id);

    // Generate session summary using AI
    void generateSessionSummary(const std::string& summary);
    std::string getSessionSummary() const;

    // Auto-documentation generation
    bool generateTodoMd(const std::string& output_path = "") const;
    bool generateDecisionsMd(const std::string& output_path = "") const;
    bool generateSessionReport(const std::string& output_path = "") const;

    // Export session data
    bool exportSessionToJson(const std::string& file_path) const;
    bool exportSessionToMarkdown(const std::string& file_path) const;

    // Session statistics
    int getMessageCount() const;
    int getToolExecutionCount() const;
    int getFileModificationCount() const;
    std::vector<std::string> getModifiedFiles() const;
    std::vector<std::string> getExecutedTools() const;

    // Get paths
    static std::string getSessionsDir();
    static std::string getSessionDbPath();

private:
    void initializeDatabase();
    void createTables();
    std::string generateSessionId() const;
    std::string getCurrentTimestamp() const;

    // Database operations
    bool saveSessionToDb();
    bool loadSessionFromDb(const std::string& session_id);
    bool saveMessages();
    bool saveToolExecutions();
    bool saveFileModifications();
    bool loadMessages(const std::string& session_id);
    bool loadToolExecutions(const std::string& session_id);
    bool loadFileModifications(const std::string& session_id);

    sqlite3* db_;
    std::unique_ptr<Session> current_session_;
    std::string db_path_;
};

} // namespace casper

#endif // CASPER_SESSION_MANAGER_H
