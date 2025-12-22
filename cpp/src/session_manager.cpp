#include "session_manager.h"
#include "utils.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <random>
#include <algorithm>

namespace oleg {

// Message implementation
json Message::toJson() const {
    return json{
        {"role", role},
        {"content", content},
        {"timestamp", timestamp}
    };
}

Message Message::fromJson(const json& j) {
    Message msg;
    msg.role = j.value("role", "");
    msg.content = j.value("content", "");
    msg.timestamp = j.value("timestamp", "");
    return msg;
}

// ToolExecution implementation
json ToolExecution::toJson() const {
    return json{
        {"tool_name", tool_name},
        {"parameters", parameters},
        {"output", output},
        {"exit_code", exit_code},
        {"timestamp", timestamp}
    };
}

ToolExecution ToolExecution::fromJson(const json& j) {
    ToolExecution te;
    te.tool_name = j.value("tool_name", "");
    te.parameters = j.value("parameters", json::object());
    te.output = j.value("output", "");
    te.exit_code = j.value("exit_code", 0);
    te.timestamp = j.value("timestamp", "");
    return te;
}

// FileModification implementation
json FileModification::toJson() const {
    return json{
        {"file_path", file_path},
        {"operation", operation},
        {"timestamp", timestamp}
    };
}

FileModification FileModification::fromJson(const json& j) {
    FileModification fm;
    fm.file_path = j.value("file_path", "");
    fm.operation = j.value("operation", "");
    fm.timestamp = j.value("timestamp", "");
    return fm;
}

// Session implementation
json Session::toJson() const {
    json messages_json = json::array();
    for (const auto& msg : messages) {
        messages_json.push_back(msg.toJson());
    }

    json tools_json = json::array();
    for (const auto& tool : tool_executions) {
        tools_json.push_back(tool.toJson());
    }

    json files_json = json::array();
    for (const auto& file : file_modifications) {
        files_json.push_back(file.toJson());
    }

    return json{
        {"session_id", session_id},
        {"created_at", created_at},
        {"updated_at", updated_at},
        {"model", model},
        {"messages", messages_json},
        {"tool_executions", tools_json},
        {"file_modifications", files_json},
        {"working_directory", working_directory},
        {"summary", summary},
        {"is_active", is_active}
    };
}

Session Session::fromJson(const json& j) {
    Session session;
    session.session_id = j.value("session_id", "");
    session.created_at = j.value("created_at", "");
    session.updated_at = j.value("updated_at", "");
    session.model = j.value("model", "");
    session.working_directory = j.value("working_directory", "");
    session.summary = j.value("summary", "");
    session.is_active = j.value("is_active", false);

    // Load messages
    if (j.contains("messages")) {
        for (const auto& msg_json : j["messages"]) {
            session.messages.push_back(Message::fromJson(msg_json));
        }
    }

    // Load tool executions
    if (j.contains("tool_executions")) {
        for (const auto& tool_json : j["tool_executions"]) {
            session.tool_executions.push_back(ToolExecution::fromJson(tool_json));
        }
    }

    // Load file modifications
    if (j.contains("file_modifications")) {
        for (const auto& file_json : j["file_modifications"]) {
            session.file_modifications.push_back(FileModification::fromJson(file_json));
        }
    }

    return session;
}

// SessionManager implementation
SessionManager::SessionManager()
    : db_(nullptr)
{
}

SessionManager::~SessionManager() {
    if (current_session_ && current_session_->is_active) {
        saveSession();
        closeSession();
    }

    if (db_) {
        sqlite3_close(db_);
    }
}

bool SessionManager::initialize(const std::string& db_path) {
    if (db_path.empty()) {
        db_path_ = getSessionDbPath();
    } else {
        db_path_ = db_path;
    }

    // Create sessions directory
    std::string sessions_dir = getSessionsDir();
    if (!utils::dirExists(sessions_dir)) {
        if (!utils::createDir(sessions_dir)) {
            std::cerr << "Failed to create sessions directory: " << sessions_dir << std::endl;
            return false;
        }
    }

    initializeDatabase();
    return db_ != nullptr;
}

void SessionManager::initializeDatabase() {
    int rc = sqlite3_open(db_path_.c_str(), &db_);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to open session database: " << sqlite3_errmsg(db_) << std::endl;
        db_ = nullptr;
        return;
    }

    createTables();
}

void SessionManager::createTables() {
    if (!db_) return;

    const char* create_sessions = R"(
        CREATE TABLE IF NOT EXISTS sessions (
            session_id TEXT PRIMARY KEY,
            created_at TEXT NOT NULL,
            updated_at TEXT NOT NULL,
            model TEXT NOT NULL,
            working_directory TEXT,
            summary TEXT,
            is_active INTEGER DEFAULT 1
        );
    )";

    const char* create_messages = R"(
        CREATE TABLE IF NOT EXISTS messages (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            session_id TEXT NOT NULL,
            role TEXT NOT NULL,
            content TEXT NOT NULL,
            timestamp TEXT NOT NULL,
            FOREIGN KEY (session_id) REFERENCES sessions(session_id) ON DELETE CASCADE
        );
    )";

    const char* create_tool_executions = R"(
        CREATE TABLE IF NOT EXISTS tool_executions (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            session_id TEXT NOT NULL,
            tool_name TEXT NOT NULL,
            parameters TEXT,
            output TEXT,
            exit_code INTEGER,
            timestamp TEXT NOT NULL,
            FOREIGN KEY (session_id) REFERENCES sessions(session_id) ON DELETE CASCADE
        );
    )";

    const char* create_file_modifications = R"(
        CREATE TABLE IF NOT EXISTS file_modifications (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            session_id TEXT NOT NULL,
            file_path TEXT NOT NULL,
            operation TEXT NOT NULL,
            timestamp TEXT NOT NULL,
            FOREIGN KEY (session_id) REFERENCES sessions(session_id) ON DELETE CASCADE
        );
    )";

    const char* create_indices = R"(
        CREATE INDEX IF NOT EXISTS idx_messages_session ON messages(session_id);
        CREATE INDEX IF NOT EXISTS idx_tools_session ON tool_executions(session_id);
        CREATE INDEX IF NOT EXISTS idx_files_session ON file_modifications(session_id);
        CREATE INDEX IF NOT EXISTS idx_sessions_active ON sessions(is_active);
    )";

    char* err_msg = nullptr;

    sqlite3_exec(db_, create_sessions, nullptr, nullptr, &err_msg);
    if (err_msg) {
        std::cerr << "SQL error: " << err_msg << std::endl;
        sqlite3_free(err_msg);
        err_msg = nullptr;
    }

    sqlite3_exec(db_, create_messages, nullptr, nullptr, &err_msg);
    if (err_msg) {
        std::cerr << "SQL error: " << err_msg << std::endl;
        sqlite3_free(err_msg);
        err_msg = nullptr;
    }

    sqlite3_exec(db_, create_tool_executions, nullptr, nullptr, &err_msg);
    if (err_msg) {
        std::cerr << "SQL error: " << err_msg << std::endl;
        sqlite3_free(err_msg);
        err_msg = nullptr;
    }

    sqlite3_exec(db_, create_file_modifications, nullptr, nullptr, &err_msg);
    if (err_msg) {
        std::cerr << "SQL error: " << err_msg << std::endl;
        sqlite3_free(err_msg);
        err_msg = nullptr;
    }

    sqlite3_exec(db_, create_indices, nullptr, nullptr, &err_msg);
    if (err_msg) {
        std::cerr << "SQL error: " << err_msg << std::endl;
        sqlite3_free(err_msg);
    }
}

std::string SessionManager::generateSessionId() const {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);

    std::stringstream ss;
    ss << "session_" << std::put_time(std::localtime(&time), "%Y%m%d_%H%M%S");

    // Add random suffix
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(1000, 9999);
    ss << "_" << dis(gen);

    return ss.str();
}

std::string SessionManager::getCurrentTimestamp() const {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);

    std::stringstream ss;
    ss << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

std::string SessionManager::createSession(const std::string& model, const std::string& working_dir) {
    current_session_ = std::make_unique<Session>();
    current_session_->session_id = generateSessionId();
    current_session_->created_at = getCurrentTimestamp();
    current_session_->updated_at = current_session_->created_at;
    current_session_->model = model;
    current_session_->working_directory = working_dir;
    current_session_->is_active = true;

    saveSessionToDb();
    return current_session_->session_id;
}

bool SessionManager::loadSession(const std::string& session_id) {
    return loadSessionFromDb(session_id);
}

bool SessionManager::saveSession() {
    if (!current_session_) return false;

    current_session_->updated_at = getCurrentTimestamp();
    return saveSessionToDb();
}

bool SessionManager::closeSession() {
    if (!current_session_) return false;

    current_session_->is_active = false;
    saveSession();
    current_session_.reset();
    return true;
}

void SessionManager::addUserMessage(const std::string& content) {
    if (!current_session_) return;

    Message msg;
    msg.role = "user";
    msg.content = content;
    msg.timestamp = getCurrentTimestamp();

    current_session_->messages.push_back(msg);
    saveSession();
}

void SessionManager::addAssistantMessage(const std::string& content) {
    if (!current_session_) return;

    Message msg;
    msg.role = "assistant";
    msg.content = content;
    msg.timestamp = getCurrentTimestamp();

    current_session_->messages.push_back(msg);
    saveSession();
}

void SessionManager::addToolMessage(const std::string& tool_name, const std::string& content) {
    if (!current_session_) return;

    Message msg;
    msg.role = "tool";
    msg.content = "[" + tool_name + "] " + content;
    msg.timestamp = getCurrentTimestamp();

    current_session_->messages.push_back(msg);
    saveSession();
}

void SessionManager::recordToolExecution(const std::string& tool_name,
                                        const json& parameters,
                                        const std::string& output,
                                        int exit_code) {
    if (!current_session_) return;

    ToolExecution te;
    te.tool_name = tool_name;
    te.parameters = parameters;
    te.output = output;
    te.exit_code = exit_code;
    te.timestamp = getCurrentTimestamp();

    current_session_->tool_executions.push_back(te);
    saveSession();
}

void SessionManager::recordFileModification(const std::string& file_path,
                                           const std::string& operation) {
    if (!current_session_) return;

    FileModification fm;
    fm.file_path = file_path;
    fm.operation = operation;
    fm.timestamp = getCurrentTimestamp();

    current_session_->file_modifications.push_back(fm);
    saveSession();
}

std::vector<Message> SessionManager::getConversationContext(int max_messages) const {
    if (!current_session_) return {};

    std::vector<Message> context;
    int start = std::max(0, static_cast<int>(current_session_->messages.size()) - max_messages);

    for (size_t i = start; i < current_session_->messages.size(); ++i) {
        context.push_back(current_session_->messages[i]);
    }

    return context;
}

bool SessionManager::saveSessionToDb() {
    if (!db_ || !current_session_) return false;

    // Save session metadata
    const char* sql = R"(
        INSERT OR REPLACE INTO sessions
        (session_id, created_at, updated_at, model, working_directory, summary, is_active)
        VALUES (?, ?, ?, ?, ?, ?, ?)
    )";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_text(stmt, 1, current_session_->session_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, current_session_->created_at.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, current_session_->updated_at.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, current_session_->model.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, current_session_->working_directory.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, current_session_->summary.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 7, current_session_->is_active ? 1 : 0);

    bool success = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);

    if (success) {
        saveMessages();
        saveToolExecutions();
        saveFileModifications();
    }

    return success;
}

bool SessionManager::loadSessionFromDb(const std::string& session_id) {
    if (!db_) return false;

    const char* sql = "SELECT created_at, updated_at, model, working_directory, summary, is_active FROM sessions WHERE session_id = ?";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_text(stmt, 1, session_id.c_str(), -1, SQLITE_TRANSIENT);

    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        current_session_ = std::make_unique<Session>();
        current_session_->session_id = session_id;
        current_session_->created_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        current_session_->updated_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        current_session_->model = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        current_session_->working_directory = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        current_session_->summary = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        current_session_->is_active = sqlite3_column_int(stmt, 5) == 1;
        found = true;
    }

    sqlite3_finalize(stmt);

    if (found) {
        loadMessages(session_id);
        loadToolExecutions(session_id);
        loadFileModifications(session_id);
    }

    return found;
}

bool SessionManager::saveMessages() {
    if (!db_ || !current_session_) return false;

    // Delete old messages for this session
    const char* delete_sql = "DELETE FROM messages WHERE session_id = ?";
    sqlite3_stmt* stmt;

    if (sqlite3_prepare_v2(db_, delete_sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, current_session_->session_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    // Insert all messages
    const char* insert_sql = "INSERT INTO messages (session_id, role, content, timestamp) VALUES (?, ?, ?, ?)";

    for (const auto& msg : current_session_->messages) {
        if (sqlite3_prepare_v2(db_, insert_sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, current_session_->session_id.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, msg.role.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 3, msg.content.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 4, msg.timestamp.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    }

    return true;
}

bool SessionManager::saveToolExecutions() {
    if (!db_ || !current_session_) return false;

    // Delete old tool executions for this session
    const char* delete_sql = "DELETE FROM tool_executions WHERE session_id = ?";
    sqlite3_stmt* stmt;

    if (sqlite3_prepare_v2(db_, delete_sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, current_session_->session_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    // Insert all tool executions
    const char* insert_sql = "INSERT INTO tool_executions (session_id, tool_name, parameters, output, exit_code, timestamp) VALUES (?, ?, ?, ?, ?, ?)";

    for (const auto& te : current_session_->tool_executions) {
        if (sqlite3_prepare_v2(db_, insert_sql, -1, &stmt, nullptr) == SQLITE_OK) {
            std::string params_str = te.parameters.dump();
            sqlite3_bind_text(stmt, 1, current_session_->session_id.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, te.tool_name.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 3, params_str.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 4, te.output.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(stmt, 5, te.exit_code);
            sqlite3_bind_text(stmt, 6, te.timestamp.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    }

    return true;
}

bool SessionManager::saveFileModifications() {
    if (!db_ || !current_session_) return false;

    // Delete old file modifications for this session
    const char* delete_sql = "DELETE FROM file_modifications WHERE session_id = ?";
    sqlite3_stmt* stmt;

    if (sqlite3_prepare_v2(db_, delete_sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, current_session_->session_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    // Insert all file modifications
    const char* insert_sql = "INSERT INTO file_modifications (session_id, file_path, operation, timestamp) VALUES (?, ?, ?, ?)";

    for (const auto& fm : current_session_->file_modifications) {
        if (sqlite3_prepare_v2(db_, insert_sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, current_session_->session_id.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, fm.file_path.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 3, fm.operation.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 4, fm.timestamp.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    }

    return true;
}

bool SessionManager::loadMessages(const std::string& session_id) {
    if (!db_ || !current_session_) return false;

    const char* sql = "SELECT role, content, timestamp FROM messages WHERE session_id = ? ORDER BY id";
    sqlite3_stmt* stmt;

    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_text(stmt, 1, session_id.c_str(), -1, SQLITE_TRANSIENT);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Message msg;
        msg.role = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        msg.content = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        msg.timestamp = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        current_session_->messages.push_back(msg);
    }

    sqlite3_finalize(stmt);
    return true;
}

bool SessionManager::loadToolExecutions(const std::string& session_id) {
    if (!db_ || !current_session_) return false;

    const char* sql = "SELECT tool_name, parameters, output, exit_code, timestamp FROM tool_executions WHERE session_id = ? ORDER BY id";
    sqlite3_stmt* stmt;

    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_text(stmt, 1, session_id.c_str(), -1, SQLITE_TRANSIENT);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        ToolExecution te;
        te.tool_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));

        std::string params_str = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        te.parameters = json::parse(params_str, nullptr, false);

        te.output = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        te.exit_code = sqlite3_column_int(stmt, 3);
        te.timestamp = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        current_session_->tool_executions.push_back(te);
    }

    sqlite3_finalize(stmt);
    return true;
}

bool SessionManager::loadFileModifications(const std::string& session_id) {
    if (!db_ || !current_session_) return false;

    const char* sql = "SELECT file_path, operation, timestamp FROM file_modifications WHERE session_id = ? ORDER BY id";
    sqlite3_stmt* stmt;

    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_text(stmt, 1, session_id.c_str(), -1, SQLITE_TRANSIENT);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        FileModification fm;
        fm.file_path = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        fm.operation = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        fm.timestamp = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        current_session_->file_modifications.push_back(fm);
    }

    sqlite3_finalize(stmt);
    return true;
}

std::vector<std::string> SessionManager::listSessions() const {
    if (!db_) return {};

    std::vector<std::string> sessions;
    const char* sql = "SELECT session_id FROM sessions ORDER BY created_at DESC";
    sqlite3_stmt* stmt;

    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            sessions.push_back(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
        }
        sqlite3_finalize(stmt);
    }

    return sessions;
}

std::vector<std::string> SessionManager::listActiveSessions() const {
    if (!db_) return {};

    std::vector<std::string> sessions;
    const char* sql = "SELECT session_id FROM sessions WHERE is_active = 1 ORDER BY updated_at DESC";
    sqlite3_stmt* stmt;

    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            sessions.push_back(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
        }
        sqlite3_finalize(stmt);
    }

    return sessions;
}

std::string SessionManager::getLastActiveSession() const {
    if (!db_) return "";

    const char* sql = "SELECT session_id FROM sessions WHERE is_active = 1 ORDER BY updated_at DESC LIMIT 1";
    sqlite3_stmt* stmt;
    std::string session_id;

    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            session_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        }
        sqlite3_finalize(stmt);
    }

    return session_id;
}

bool SessionManager::deleteSession(const std::string& session_id) {
    if (!db_) return false;

    const char* sql = "DELETE FROM sessions WHERE session_id = ?";
    sqlite3_stmt* stmt;

    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, session_id.c_str(), -1, SQLITE_TRANSIENT);
        bool success = (sqlite3_step(stmt) == SQLITE_DONE);
        sqlite3_finalize(stmt);
        return success;
    }

    return false;
}

void SessionManager::generateSessionSummary(const std::string& summary) {
    if (!current_session_) return;
    current_session_->summary = summary;
    saveSession();
}

std::string SessionManager::getSessionSummary() const {
    if (!current_session_) return "";
    return current_session_->summary;
}

int SessionManager::getMessageCount() const {
    if (!current_session_) return 0;
    return static_cast<int>(current_session_->messages.size());
}

int SessionManager::getToolExecutionCount() const {
    if (!current_session_) return 0;
    return static_cast<int>(current_session_->tool_executions.size());
}

int SessionManager::getFileModificationCount() const {
    if (!current_session_) return 0;
    return static_cast<int>(current_session_->file_modifications.size());
}

std::vector<std::string> SessionManager::getModifiedFiles() const {
    if (!current_session_) return {};

    std::vector<std::string> files;
    for (const auto& fm : current_session_->file_modifications) {
        if (std::find(files.begin(), files.end(), fm.file_path) == files.end()) {
            files.push_back(fm.file_path);
        }
    }
    return files;
}

std::vector<std::string> SessionManager::getExecutedTools() const {
    if (!current_session_) return {};

    std::vector<std::string> tools;
    for (const auto& te : current_session_->tool_executions) {
        if (std::find(tools.begin(), tools.end(), te.tool_name) == tools.end()) {
            tools.push_back(te.tool_name);
        }
    }
    return tools;
}

bool SessionManager::exportSessionToJson(const std::string& file_path) const {
    if (!current_session_) return false;

    try {
        json session_json = current_session_->toJson();
        std::ofstream out(file_path);
        out << session_json.dump(2);
        out.close();
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Failed to export session to JSON: " << e.what() << std::endl;
        return false;
    }
}

bool SessionManager::exportSessionToMarkdown(const std::string& file_path) const {
    if (!current_session_) return false;

    try {
        std::ofstream out(file_path);

        out << "# Session: " << current_session_->session_id << "\n\n";
        out << "**Created:** " << current_session_->created_at << "\n";
        out << "**Updated:** " << current_session_->updated_at << "\n";
        out << "**Model:** " << current_session_->model << "\n";
        out << "**Working Directory:** " << current_session_->working_directory << "\n\n";

        if (!current_session_->summary.empty()) {
            out << "## Summary\n\n" << current_session_->summary << "\n\n";
        }

        out << "## Statistics\n\n";
        out << "- Messages: " << current_session_->messages.size() << "\n";
        out << "- Tool Executions: " << current_session_->tool_executions.size() << "\n";
        out << "- File Modifications: " << current_session_->file_modifications.size() << "\n\n";

        if (!current_session_->messages.empty()) {
            out << "## Conversation\n\n";
            for (const auto& msg : current_session_->messages) {
                out << "### " << msg.role << " (" << msg.timestamp << ")\n\n";
                out << msg.content << "\n\n";
            }
        }

        if (!current_session_->tool_executions.empty()) {
            out << "## Tool Executions\n\n";
            for (const auto& te : current_session_->tool_executions) {
                out << "### " << te.tool_name << " (" << te.timestamp << ")\n\n";
                out << "**Parameters:**\n```json\n" << te.parameters.dump(2) << "\n```\n\n";
                out << "**Output:**\n```\n" << te.output << "\n```\n\n";
                out << "**Exit Code:** " << te.exit_code << "\n\n";
            }
        }

        if (!current_session_->file_modifications.empty()) {
            out << "## File Modifications\n\n";
            for (const auto& fm : current_session_->file_modifications) {
                out << "- `" << fm.file_path << "` - " << fm.operation << " (" << fm.timestamp << ")\n";
            }
            out << "\n";
        }

        out.close();
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Failed to export session to Markdown: " << e.what() << std::endl;
        return false;
    }
}

bool SessionManager::generateTodoMd(const std::string& output_path) const {
    if (!current_session_) return false;

    std::string path = output_path.empty() ?
        utils::joinPath(current_session_->working_directory, "TODO.md") :
        output_path;

    try {
        std::ofstream out(path);

        out << "# TODO List\n\n";
        out << "**Generated from session:** " << current_session_->session_id << "\n";
        out << "**Date:** " << getCurrentTimestamp() << "\n\n";

        out << "## Tasks\n\n";

        // Extract potential TODOs from conversation
        for (const auto& msg : current_session_->messages) {
            if (msg.role == "assistant" &&
                (msg.content.find("TODO") != std::string::npos ||
                 msg.content.find("task") != std::string::npos ||
                 msg.content.find("next step") != std::string::npos)) {
                out << "- [ ] Extract from conversation: " << msg.timestamp << "\n";
            }
        }

        out << "\n## Modified Files\n\n";
        auto files = getModifiedFiles();
        for (const auto& file : files) {
            out << "- `" << file << "`\n";
        }

        out << "\n## Tools Used\n\n";
        auto tools = getExecutedTools();
        for (const auto& tool : tools) {
            out << "- " << tool << "\n";
        }

        out.close();
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Failed to generate TODO.md: " << e.what() << std::endl;
        return false;
    }
}

bool SessionManager::generateDecisionsMd(const std::string& output_path) const {
    if (!current_session_) return false;

    std::string path = output_path.empty() ?
        utils::joinPath(current_session_->working_directory, "DECISIONS.md") :
        output_path;

    try {
        std::ofstream out(path);

        out << "# Technical Decisions\n\n";
        out << "**Session:** " << current_session_->session_id << "\n";
        out << "**Date:** " << current_session_->created_at << "\n\n";

        out << "## Context\n\n";
        out << "**Model:** " << current_session_->model << "\n";
        out << "**Working Directory:** " << current_session_->working_directory << "\n\n";

        if (!current_session_->summary.empty()) {
            out << "## Summary\n\n" << current_session_->summary << "\n\n";
        }

        out << "## Key Actions\n\n";

        out << "### Files Modified (" << current_session_->file_modifications.size() << ")\n\n";
        for (const auto& fm : current_session_->file_modifications) {
            out << "- **" << fm.operation << "**: `" << fm.file_path << "` (" << fm.timestamp << ")\n";
        }

        out << "\n### Tools Executed (" << current_session_->tool_executions.size() << ")\n\n";
        for (const auto& te : current_session_->tool_executions) {
            out << "- **" << te.tool_name << "** (" << te.timestamp << ")\n";
            if (te.exit_code != 0) {
                out << "  - ⚠️ Failed with exit code " << te.exit_code << "\n";
            }
        }

        out.close();
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Failed to generate DECISIONS.md: " << e.what() << std::endl;
        return false;
    }
}

bool SessionManager::generateSessionReport(const std::string& output_path) const {
    if (!current_session_) return false;

    std::string path = output_path.empty() ?
        utils::joinPath(current_session_->working_directory, "SESSION_REPORT.md") :
        output_path;

    return exportSessionToMarkdown(path);
}

std::string SessionManager::getSessionsDir() {
    return utils::joinPath(utils::joinPath(utils::getHomeDir(), ".config/oleg"), "sessions");
}

std::string SessionManager::getSessionDbPath() {
    return utils::joinPath(getSessionsDir(), "sessions.db");
}

} // namespace oleg
