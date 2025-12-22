#include "tool_executor.h"
#include "config.h"
#include "mcp_client.h"
#include "search_client.h"
#include "db_client.h"
#include "rag_engine.h"
#include "utils.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <array>

namespace oleg {

// Helper function to count lines in a string
static int countLines(const std::string& str) {
    if (str.empty()) return 0;
    int count = 1;
    for (char c : str) {
        if (c == '\n') count++;
    }
    // Don't count trailing newline as extra line
    if (!str.empty() && str.back() == '\n') count--;
    return count;
}

// Helper function to split string into lines
static std::vector<std::string> splitLines(const std::string& str) {
    std::vector<std::string> lines;
    std::istringstream iss(str);
    std::string line;
    while (std::getline(iss, line)) {
        lines.push_back(line);
    }
    return lines;
}

ToolExecutor::ToolExecutor(Config& config)
    : config_(config)
    , confirm_callback_(nullptr)
    , mcp_client_(nullptr)
    , search_client_(nullptr)
    , db_client_(nullptr)
    , rag_engine_(nullptr)
{
}

void ToolExecutor::setMCPClient(MCPClient* client) {
    mcp_client_ = client;
}

void ToolExecutor::setSearchClient(SearchClient* client) {
    search_client_ = client;
}

void ToolExecutor::setDBClient(DBClient* client) {
    db_client_ = client;
}

void ToolExecutor::setRAGEngine(RAGEngine* engine) {
    rag_engine_ = engine;
}

bool ToolExecutor::isMCPTool(const std::string& tool_name) const {
    // MCP tools are prefixed with "serverName__toolName"
    return tool_name.find("__") != std::string::npos;
}

void ToolExecutor::setConfirmCallback(ConfirmCallback callback) {
    confirm_callback_ = callback;
}

bool ToolExecutor::isCommandSafe(const std::string& command) {
    if (!config_.getSafeMode()) {
        return true;
    }

    return config_.isCommandAllowed(command);
}

bool ToolExecutor::requestConfirmation(const std::string& tool_name, const std::string& description) {
    if (config_.getAutoApprove()) {
        return true;
    }

    if (confirm_callback_) {
        return confirm_callback_(tool_name, description);
    }

    // Default confirmation
    std::cout << utils::terminal::YELLOW << "Execute " << tool_name << "? (y/n): " << utils::terminal::RESET;
    char response;
    std::cin >> response;
    std::cin.ignore(); // Clear newline
    return (response == 'y' || response == 'Y');
}

std::string ToolExecutor::executeCommand(const std::string& command, int& exit_code) {
    std::array<char, 128> buffer;
    std::string result;

    FILE* pipe = popen((command + " 2>&1").c_str(), "r");
    if (!pipe) {
        exit_code = -1;
        return "Failed to execute command";
    }

    while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
        result += buffer.data();
    }

    exit_code = pclose(pipe);
    exit_code = WEXITSTATUS(exit_code);

    return result;
}

ToolResult ToolExecutor::executeBash(const ToolCall& tool_call) {
    ToolResult result;

    auto it = tool_call.parameters.find("command");
    if (it == tool_call.parameters.end()) {
        result.success = false;
        result.error = "Missing 'command' parameter";
        return result;
    }

    std::string command = it->second;
    std::string description = "Execute command";

    auto desc_it = tool_call.parameters.find("description");
    if (desc_it != tool_call.parameters.end()) {
        description = desc_it->second;
    }

    utils::terminal::printInfo("[Tool: Bash]");
    std::cout << utils::terminal::CYAN << "Description: " << description << utils::terminal::RESET << "\n";
    std::cout << utils::terminal::MAGENTA << "Command: " << command << utils::terminal::RESET << "\n\n";

    // Safety check
    if (!isCommandSafe(command)) {
        result.success = false;
        result.error = "Command not allowed in safe mode";
        utils::terminal::printError(result.error);
        return result;
    }

    // Confirmation
    if (!requestConfirmation("Bash", description)) {
        result.success = false;
        result.error = "Cancelled by user";
        utils::terminal::printError("Cancelled");
        return result;
    }

    // Execute
    utils::terminal::printInfo("Executing...");
    result.output = executeCommand(command, result.exit_code);
    result.success = (result.exit_code == 0);

    if (result.success) {
        utils::terminal::printSuccess("Success");
    } else {
        utils::terminal::printError("Failed (exit code: " + std::to_string(result.exit_code) + ")");
    }

    std::cout << "\n=== Output ===\n" << result.output << "==============\n\n";

    return result;
}

ToolResult ToolExecutor::executeRead(const ToolCall& tool_call) {
    ToolResult result;

    // Try multiple parameter names (fallback for different model outputs)
    std::string file_path;
    std::vector<std::string> path_aliases = {"file_path", "path", "filename", "file", "content"};

    for (const auto& alias : path_aliases) {
        auto it = tool_call.parameters.find(alias);
        if (it != tool_call.parameters.end() && !it->second.empty()) {
            file_path = it->second;
            break;
        }
    }

    if (file_path.empty()) {
        result.success = false;
        result.error = "Missing 'file_path' parameter. Received parameters: ";
        for (const auto& p : tool_call.parameters) {
            result.error += "[" + p.first + "=" + p.second.substr(0, 50) + "] ";
        }
        if (tool_call.parameters.empty()) {
            result.error += "(none)";
        }
        return result;
    }

    utils::terminal::printInfo("[Tool: Read]");
    std::cout << utils::terminal::CYAN << "File: " << file_path << utils::terminal::RESET << "\n\n";

    if (!utils::fileExists(file_path)) {
        result.success = false;
        result.error = "File not found: " + file_path;
        utils::terminal::printError(result.error);
        return result;
    }

    result.output = utils::readFile(file_path);
    if (result.output.empty()) {
        result.success = false;
        result.error = "Failed to read file or file is empty";
        utils::terminal::printError(result.error);
        return result;
    }

    result.success = true;
    result.exit_code = 0;

    std::cout << "=== File Contents ===\n" << result.output << "\n====================\n\n";

    return result;
}

ToolResult ToolExecutor::executeWrite(const ToolCall& tool_call) {
    ToolResult result;

    // Try multiple parameter names for file path
    std::string file_path;
    std::vector<std::string> path_aliases = {"file_path", "path", "filename", "file"};
    for (const auto& alias : path_aliases) {
        auto it = tool_call.parameters.find(alias);
        if (it != tool_call.parameters.end() && !it->second.empty()) {
            file_path = it->second;
            break;
        }
    }

    // Try multiple parameter names for content
    std::string content;
    std::vector<std::string> content_aliases = {"content", "text", "data", "body"};
    for (const auto& alias : content_aliases) {
        auto it = tool_call.parameters.find(alias);
        if (it != tool_call.parameters.end()) {
            content = it->second;
            break;
        }
    }

    if (file_path.empty() || content.empty()) {
        result.success = false;
        result.error = "Missing 'file_path' or 'content' parameter. Received: ";
        for (const auto& p : tool_call.parameters) {
            result.error += "[" + p.first + "] ";
        }
        return result;
    }

    utils::terminal::printInfo("[Tool: Write]");
    std::cout << utils::terminal::CYAN << "File: " << file_path << utils::terminal::RESET << "\n\n";

    // Create parent directory if needed
    std::string dir = utils::getDirname(file_path);
    if (!utils::dirExists(dir)) {
        if (!utils::createDir(dir)) {
            result.success = false;
            result.error = "Failed to create directory: " + dir;
            utils::terminal::printError(result.error);
            return result;
        }
    }

    // Confirm if file exists
    if (utils::fileExists(file_path)) {
        if (!requestConfirmation("Write", "File exists. Overwrite?")) {
            result.success = false;
            result.error = "Cancelled by user";
            utils::terminal::printError("Cancelled");
            return result;
        }
    }

    // Check if this is a new file or overwrite
    bool isNewFile = !utils::fileExists(file_path);
    std::string oldContent;
    int oldLineCount = 0;
    if (!isNewFile) {
        oldContent = utils::readFile(file_path);
        oldLineCount = countLines(oldContent);
    }

    int newLineCount = countLines(content);

    // Show what will happen
    if (isNewFile) {
        std::cout << utils::terminal::GREEN << "Creating new file with "
                  << newLineCount << " lines" << utils::terminal::RESET << "\n\n";
    } else {
        int linesDiff = newLineCount - oldLineCount;
        std::cout << utils::terminal::CYAN << "Overwriting file:"
                  << utils::terminal::RESET << "\n";
        std::cout << utils::terminal::RED << "  Old: " << oldLineCount << " lines"
                  << utils::terminal::RESET << "\n";
        std::cout << utils::terminal::GREEN << "  New: " << newLineCount << " lines"
                  << utils::terminal::RESET << "\n";
        if (linesDiff > 0) {
            std::cout << utils::terminal::GREEN << "  Net: +" << linesDiff << " lines"
                      << utils::terminal::RESET << "\n\n";
        } else if (linesDiff < 0) {
            std::cout << utils::terminal::RED << "  Net: " << linesDiff << " lines"
                      << utils::terminal::RESET << "\n\n";
        } else {
            std::cout << utils::terminal::YELLOW << "  Net: 0 lines (same size)"
                      << utils::terminal::RESET << "\n\n";
        }
    }

    // Write file
    if (!utils::writeFile(file_path, content)) {
        result.success = false;
        result.error = "Failed to write file";
        utils::terminal::printError(result.error);
        return result;
    }

    result.success = true;
    result.exit_code = 0;

    if (isNewFile) {
        result.output = "Created new file with " + std::to_string(newLineCount) + " lines";
        utils::terminal::printSuccess("File created");
        std::cout << utils::terminal::GREEN << "  +" << newLineCount << " lines"
                  << utils::terminal::RESET << "\n\n";
    } else {
        int linesDiff = newLineCount - oldLineCount;
        std::ostringstream output_msg;
        output_msg << "File overwritten (";
        if (linesDiff >= 0) {
            output_msg << "+" << linesDiff;
        } else {
            output_msg << linesDiff;
        }
        output_msg << " lines)";
        result.output = output_msg.str();
        utils::terminal::printSuccess("File written");
        std::cout << utils::terminal::CYAN << "  " << newLineCount << " lines total"
                  << utils::terminal::RESET << "\n\n";
    }

    return result;
}

ToolResult ToolExecutor::executeEdit(const ToolCall& tool_call) {
    ToolResult result;

    // Try multiple parameter names for file path
    std::string file_path;
    std::vector<std::string> path_aliases = {"file_path", "path", "filename", "file"};
    for (const auto& alias : path_aliases) {
        auto it = tool_call.parameters.find(alias);
        if (it != tool_call.parameters.end() && !it->second.empty()) {
            file_path = it->second;
            break;
        }
    }

    // Try multiple parameter names for old string
    std::string old_string;
    std::vector<std::string> old_aliases = {"old_string", "old", "search", "find", "original"};
    for (const auto& alias : old_aliases) {
        auto it = tool_call.parameters.find(alias);
        if (it != tool_call.parameters.end()) {
            old_string = it->second;
            break;
        }
    }

    // Try multiple parameter names for new string
    std::string new_string;
    std::vector<std::string> new_aliases = {"new_string", "new", "replace", "replacement"};
    for (const auto& alias : new_aliases) {
        auto it = tool_call.parameters.find(alias);
        if (it != tool_call.parameters.end()) {
            new_string = it->second;
            break;
        }
    }

    if (file_path.empty() || old_string.empty()) {
        result.success = false;
        result.error = "Missing required parameters (file_path, old_string). Received: ";
        for (const auto& p : tool_call.parameters) {
            result.error += "[" + p.first + "] ";
        }
        return result;
    }

    utils::terminal::printInfo("[Tool: Edit]");
    std::cout << utils::terminal::CYAN << "File: " << file_path << utils::terminal::RESET << "\n\n";

    if (!utils::fileExists(file_path)) {
        result.success = false;
        result.error = "File not found: " + file_path;
        utils::terminal::printError(result.error);
        return result;
    }

    // Read file
    std::string content = utils::readFile(file_path);
    if (content.empty()) {
        result.success = false;
        result.error = "Failed to read file";
        utils::terminal::printError(result.error);
        return result;
    }

    // Check if old_string exists
    size_t match_pos = content.find(old_string);
    if (match_pos == std::string::npos) {
        result.success = false;
        result.error = "String not found in file";
        utils::terminal::printError(result.error);
        return result;
    }

    // Count occurrences
    size_t count = 0;
    size_t pos = 0;
    while ((pos = content.find(old_string, pos)) != std::string::npos) {
        count++;
        pos += old_string.length();
    }

    // Calculate line counts for diff display
    int old_lines = countLines(old_string);
    int new_lines = countLines(new_string);
    int lines_removed = old_lines;
    int lines_added = new_lines;

    // Show diff-style output
    std::cout << utils::terminal::CYAN << "Changes (" << count << " occurrence(s)):"
              << utils::terminal::RESET << "\n\n";

    // Show removed lines (red)
    std::cout << utils::terminal::RED << "--- Removed:" << utils::terminal::RESET << "\n";
    auto oldLines = splitLines(old_string);
    for (const auto& line : oldLines) {
        std::cout << utils::terminal::RED << "- " << line << utils::terminal::RESET << "\n";
    }

    std::cout << "\n";

    // Show added lines (green)
    std::cout << utils::terminal::GREEN << "+++ Added:" << utils::terminal::RESET << "\n";
    auto newLines = splitLines(new_string);
    for (const auto& line : newLines) {
        std::cout << utils::terminal::GREEN << "+ " << line << utils::terminal::RESET << "\n";
    }

    std::cout << "\n";

    // Show summary
    std::cout << utils::terminal::YELLOW << "Summary: "
              << utils::terminal::RED << "-" << lines_removed << " lines"
              << utils::terminal::RESET << " / "
              << utils::terminal::GREEN << "+" << lines_added << " lines"
              << utils::terminal::RESET << "\n\n";

    // Confirm
    if (!requestConfirmation("Edit", "Apply changes?")) {
        result.success = false;
        result.error = "Cancelled by user";
        utils::terminal::printError("Cancelled");
        return result;
    }

    // Create backup
    std::string backup_path = file_path + ".bak";
    utils::writeFile(backup_path, content);

    // Perform replacement
    pos = 0;
    while ((pos = content.find(old_string, pos)) != std::string::npos) {
        content.replace(pos, old_string.length(), new_string);
        pos += new_string.length();
    }

    // Write modified content
    if (!utils::writeFile(file_path, content)) {
        result.success = false;
        result.error = "Failed to write file";
        utils::terminal::printError(result.error);
        return result;
    }

    result.success = true;
    result.exit_code = 0;

    std::ostringstream output_msg;
    output_msg << "File edited successfully ("
               << utils::terminal::RED << "-" << lines_removed
               << utils::terminal::RESET << "/"
               << utils::terminal::GREEN << "+" << lines_added
               << utils::terminal::RESET << " lines)";
    result.output = output_msg.str();

    utils::terminal::printSuccess("Edit complete");
    std::cout << utils::terminal::CYAN << "  " << count << " replacement(s) made"
              << utils::terminal::RESET << "\n";
    std::cout << utils::terminal::RED << "  -" << lines_removed << " lines removed"
              << utils::terminal::RESET << "\n";
    std::cout << utils::terminal::GREEN << "  +" << lines_added << " lines added"
              << utils::terminal::RESET << "\n";
    std::cout << utils::terminal::CYAN << "  Backup: " << backup_path
              << utils::terminal::RESET << "\n\n";

    return result;
}

ToolResult ToolExecutor::executeGlob(const ToolCall& tool_call) {
    ToolResult result;

    auto pattern_it = tool_call.parameters.find("pattern");
    if (pattern_it == tool_call.parameters.end()) {
        result.success = false;
        result.error = "Missing 'pattern' parameter";
        return result;
    }

    std::string pattern = pattern_it->second;
    std::string path = ".";

    auto path_it = tool_call.parameters.find("path");
    if (path_it != tool_call.parameters.end()) {
        path = path_it->second;
    }

    utils::terminal::printInfo("[Tool: Glob]");
    std::cout << utils::terminal::CYAN << "Pattern: " << pattern << utils::terminal::RESET << "\n";
    std::cout << utils::terminal::CYAN << "Path: " << path << utils::terminal::RESET << "\n\n";

    // Use find command
    std::string command = "find " + path + " -name '" + pattern + "' -type f 2>/dev/null | head -100";
    result.output = executeCommand(command, result.exit_code);
    result.success = true;

    std::cout << "=== Matching Files ===\n" << result.output << "=====================\n\n";

    return result;
}

ToolResult ToolExecutor::executeGrep(const ToolCall& tool_call) {
    ToolResult result;

    auto pattern_it = tool_call.parameters.find("pattern");
    if (pattern_it == tool_call.parameters.end()) {
        result.success = false;
        result.error = "Missing 'pattern' parameter";
        return result;
    }

    std::string pattern = pattern_it->second;
    std::string path = ".";
    std::string output_mode = "files_with_matches";

    auto path_it = tool_call.parameters.find("path");
    if (path_it != tool_call.parameters.end()) {
        path = path_it->second;
    }

    auto mode_it = tool_call.parameters.find("output_mode");
    if (mode_it != tool_call.parameters.end()) {
        output_mode = mode_it->second;
    }

    utils::terminal::printInfo("[Tool: Grep]");
    std::cout << utils::terminal::CYAN << "Pattern: " << pattern << utils::terminal::RESET << "\n";
    std::cout << utils::terminal::CYAN << "Path: " << path << utils::terminal::RESET << "\n";
    std::cout << utils::terminal::CYAN << "Mode: " << output_mode << utils::terminal::RESET << "\n\n";

    // Build grep command
    std::string command;
    if (output_mode == "content") {
        command = "grep -r -n '" + pattern + "' " + path + " 2>/dev/null | head -100";
    } else {
        command = "grep -r -l '" + pattern + "' " + path + " 2>/dev/null | head -100";
    }

    result.output = executeCommand(command, result.exit_code);
    result.success = true;

    std::cout << "=== Search Results ===\n" << result.output << "=====================\n\n";

    return result;
}

ToolResult ToolExecutor::executeMCPTool(const ToolCall& tool_call) {
    ToolResult result;

    if (!mcp_client_) {
        result.success = false;
        result.error = "MCP client not initialized";
        utils::terminal::printError(result.error);
        return result;
    }

    // Parse tool name: "serverName__toolName"
    size_t pos = tool_call.name.find("__");
    if (pos == std::string::npos) {
        result.success = false;
        result.error = "Invalid MCP tool name format";
        utils::terminal::printError(result.error);
        return result;
    }

    std::string server_name = tool_call.name.substr(0, pos);
    std::string tool_name = tool_call.name.substr(pos + 2);

    utils::terminal::printInfo("[MCP Tool: " + server_name + "/" + tool_name + "]");

    // Convert parameters to JSON
    json arguments;
    for (const auto& [key, value] : tool_call.parameters) {
        // Try to parse as JSON, fallback to string
        try {
            arguments[key] = json::parse(value);
        } catch (...) {
            arguments[key] = value;
        }
    }

    std::cout << utils::terminal::CYAN << "Arguments: " << arguments.dump(2) << utils::terminal::RESET << "\n\n";

    // Confirm execution
    if (!requestConfirmation("MCP:" + tool_name, "Execute MCP tool?")) {
        result.success = false;
        result.error = "Cancelled by user";
        utils::terminal::printError("Cancelled");
        return result;
    }

    // Execute via MCP client
    utils::terminal::printInfo("Executing MCP tool...");
    MCPToolResult mcp_result = mcp_client_->callTool(tool_name, arguments);

    result.success = mcp_result.success;
    result.output = mcp_result.content;
    result.error = mcp_result.error;
    result.exit_code = mcp_result.isError ? 1 : 0;

    if (result.success) {
        utils::terminal::printSuccess("MCP tool executed successfully");
    } else {
        utils::terminal::printError("MCP tool failed: " + result.error);
    }

    std::cout << "\n=== MCP Output ===\n" << result.output << "\n==================\n\n";

    return result;
}

// ============================================================================
// Search Tools Implementation
// ============================================================================

ToolResult ToolExecutor::executeWebSearch(const ToolCall& tool_call) {
    ToolResult result;

    if (!search_client_) {
        result.success = false;
        result.error = "Search client not initialized";
        utils::terminal::printError(result.error);
        return result;
    }

    auto query_it = tool_call.parameters.find("query");
    if (query_it == tool_call.parameters.end()) {
        result.success = false;
        result.error = "Missing 'query' parameter";
        return result;
    }

    std::string query = query_it->second;
    int max_results = 10;

    auto max_it = tool_call.parameters.find("max_results");
    if (max_it != tool_call.parameters.end()) {
        try {
            max_results = std::stoi(max_it->second);
        } catch (...) {}
    }

    utils::terminal::printInfo("[Tool: WebSearch]");
    std::cout << utils::terminal::CYAN << "Query: " << query << utils::terminal::RESET << "\n";
    std::cout << utils::terminal::CYAN << "Max results: " << max_results << utils::terminal::RESET << "\n\n";

    // Confirmation
    if (!requestConfirmation("WebSearch", "Search the web?")) {
        result.success = false;
        result.error = "Cancelled by user";
        utils::terminal::printError("Cancelled");
        return result;
    }

    utils::terminal::printInfo("Searching...");
    auto search_results = search_client_->search(query, max_results);

    std::stringstream ss;
    ss << "Search Results for: " << query << "\n\n";

    for (size_t i = 0; i < search_results.size(); i++) {
        const auto& res = search_results[i];
        ss << "[" << (i + 1) << "] " << res.title << "\n";
        ss << "    URL: " << res.url << "\n";
        ss << "    " << res.snippet << "\n\n";
    }

    if (search_results.empty()) {
        ss << "No results found.\n";
    }

    result.output = ss.str();
    result.success = true;
    result.exit_code = 0;

    utils::terminal::printSuccess("Search complete");
    std::cout << "\n=== Results ===\n" << result.output << "===============\n\n";

    return result;
}

ToolResult ToolExecutor::executeWebFetch(const ToolCall& tool_call) {
    ToolResult result;

    if (!search_client_) {
        result.success = false;
        result.error = "Search client not initialized";
        utils::terminal::printError(result.error);
        return result;
    }

    auto url_it = tool_call.parameters.find("url");
    if (url_it == tool_call.parameters.end()) {
        result.success = false;
        result.error = "Missing 'url' parameter";
        return result;
    }

    std::string url = url_it->second;
    bool extract_links = false;

    auto links_it = tool_call.parameters.find("extract_links");
    if (links_it != tool_call.parameters.end()) {
        extract_links = (links_it->second == "true" || links_it->second == "1");
    }

    utils::terminal::printInfo("[Tool: WebFetch]");
    std::cout << utils::terminal::CYAN << "URL: " << url << utils::terminal::RESET << "\n\n";

    // Confirmation
    if (!requestConfirmation("WebFetch", "Fetch web page?")) {
        result.success = false;
        result.error = "Cancelled by user";
        utils::terminal::printError("Cancelled");
        return result;
    }

    utils::terminal::printInfo("Fetching...");
    auto page = search_client_->fetchPage(url);

    if (!page.success) {
        result.success = false;
        result.error = "Failed to fetch page: " + page.error;
        utils::terminal::printError(result.error);
        return result;
    }

    std::stringstream ss;
    ss << "=== Page: " << page.title << " ===\n\n";
    ss << page.content << "\n";

    if (extract_links && !page.links.empty()) {
        ss << "\n=== Links ===\n";
        for (const auto& link : page.links) {
            ss << "- " << link << "\n";
        }
    }

    result.output = ss.str();
    result.success = true;
    result.exit_code = 0;

    utils::terminal::printSuccess("Fetch complete");
    std::cout << "\n=== Content ===\n";
    // Truncate output for display
    if (page.content.length() > 2000) {
        std::cout << page.content.substr(0, 2000) << "\n...(truncated)\n";
    } else {
        std::cout << page.content << "\n";
    }
    std::cout << "===============\n\n";

    return result;
}

// ============================================================================
// Database Tools Implementation
// ============================================================================

ToolResult ToolExecutor::executeDBConnect(const ToolCall& tool_call) {
    ToolResult result;

    if (!db_client_) {
        result.success = false;
        result.error = "Database client not initialized";
        utils::terminal::printError(result.error);
        return result;
    }

    auto type_it = tool_call.parameters.find("type");
    auto conn_it = tool_call.parameters.find("connection");

    if (type_it == tool_call.parameters.end() || conn_it == tool_call.parameters.end()) {
        result.success = false;
        result.error = "Missing 'type' or 'connection' parameter";
        return result;
    }

    std::string db_type = type_it->second;
    std::string connection = conn_it->second;

    utils::terminal::printInfo("[Tool: DBConnect]");
    std::cout << utils::terminal::CYAN << "Type: " << db_type << utils::terminal::RESET << "\n";
    std::cout << utils::terminal::CYAN << "Connection: " << connection << utils::terminal::RESET << "\n\n";

    // Confirmation
    if (!requestConfirmation("DBConnect", "Connect to database?")) {
        result.success = false;
        result.error = "Cancelled by user";
        utils::terminal::printError("Cancelled");
        return result;
    }

    utils::terminal::printInfo("Connecting...");

    if (db_client_->connect(db_type, connection)) {
        result.success = true;
        result.output = "Connected to " + db_type + " database successfully";
        utils::terminal::printSuccess(result.output);
    } else {
        result.success = false;
        result.error = "Failed to connect to database";
        utils::terminal::printError(result.error);
    }

    return result;
}

ToolResult ToolExecutor::executeDBQuery(const ToolCall& tool_call) {
    ToolResult result;

    if (!db_client_) {
        result.success = false;
        result.error = "Database client not initialized";
        utils::terminal::printError(result.error);
        return result;
    }

    if (!db_client_->isConnected()) {
        result.success = false;
        result.error = "Not connected to any database";
        utils::terminal::printError(result.error);
        return result;
    }

    auto query_it = tool_call.parameters.find("query");
    if (query_it == tool_call.parameters.end()) {
        result.success = false;
        result.error = "Missing 'query' parameter";
        return result;
    }

    std::string query = query_it->second;

    utils::terminal::printInfo("[Tool: DBQuery]");
    std::cout << utils::terminal::CYAN << "Query: " << query << utils::terminal::RESET << "\n\n";

    // Confirmation
    if (!requestConfirmation("DBQuery", "Execute query?")) {
        result.success = false;
        result.error = "Cancelled by user";
        utils::terminal::printError("Cancelled");
        return result;
    }

    utils::terminal::printInfo("Executing query...");
    auto query_result = db_client_->query(query);

    if (!query_result.success) {
        result.success = false;
        result.error = "Query failed: " + query_result.error;
        utils::terminal::printError(result.error);
        return result;
    }

    // Format results as table
    std::stringstream ss;
    if (!query_result.columns.empty()) {
        // Header
        for (const auto& col : query_result.columns) {
            ss << col << "\t";
        }
        ss << "\n";

        // Separator
        for (size_t i = 0; i < query_result.columns.size(); i++) {
            ss << "--------\t";
        }
        ss << "\n";

        // Rows
        for (const auto& row : query_result.rows) {
            for (const auto& col : query_result.columns) {
                auto it = row.find(col);
                if (it != row.end()) {
                    ss << it->second << "\t";
                } else {
                    ss << "(null)\t";
                }
            }
            ss << "\n";
        }

        ss << "\n" << query_result.rows.size() << " row(s) returned\n";
    } else {
        ss << "Query executed successfully. Rows affected: " << query_result.affected_rows << "\n";
    }

    result.output = ss.str();
    result.success = true;
    result.exit_code = 0;

    utils::terminal::printSuccess("Query complete");
    std::cout << "\n=== Results ===\n" << result.output << "===============\n\n";

    return result;
}

ToolResult ToolExecutor::executeDBExecute(const ToolCall& tool_call) {
    ToolResult result;

    if (!db_client_) {
        result.success = false;
        result.error = "Database client not initialized";
        utils::terminal::printError(result.error);
        return result;
    }

    if (!db_client_->isConnected()) {
        result.success = false;
        result.error = "Not connected to any database";
        utils::terminal::printError(result.error);
        return result;
    }

    // Check if writes are allowed
    if (!config_.getDBAllowWrite()) {
        result.success = false;
        result.error = "Database writes are disabled in settings";
        utils::terminal::printError(result.error);
        return result;
    }

    auto query_it = tool_call.parameters.find("query");
    if (query_it == tool_call.parameters.end()) {
        result.success = false;
        result.error = "Missing 'query' parameter";
        return result;
    }

    std::string query = query_it->second;

    utils::terminal::printInfo("[Tool: DBExecute]");
    std::cout << utils::terminal::YELLOW << "WARNING: This will modify the database!" << utils::terminal::RESET << "\n";
    std::cout << utils::terminal::CYAN << "Query: " << query << utils::terminal::RESET << "\n\n";

    // Always require confirmation for write operations
    if (!requestConfirmation("DBExecute", "Execute WRITE query?")) {
        result.success = false;
        result.error = "Cancelled by user";
        utils::terminal::printError("Cancelled");
        return result;
    }

    utils::terminal::printInfo("Executing...");
    auto query_result = db_client_->execute(query);

    if (!query_result.success) {
        result.success = false;
        result.error = "Execute failed: " + query_result.error;
        utils::terminal::printError(result.error);
        return result;
    }

    result.output = "Query executed. Rows affected: " + std::to_string(query_result.affected_rows);
    result.success = true;
    result.exit_code = 0;

    utils::terminal::printSuccess(result.output);

    return result;
}

ToolResult ToolExecutor::executeDBSchema(const ToolCall& tool_call) {
    ToolResult result;

    if (!db_client_) {
        result.success = false;
        result.error = "Database client not initialized";
        utils::terminal::printError(result.error);
        return result;
    }

    if (!db_client_->isConnected()) {
        result.success = false;
        result.error = "Not connected to any database";
        utils::terminal::printError(result.error);
        return result;
    }

    std::string table;
    auto table_it = tool_call.parameters.find("table");
    if (table_it != tool_call.parameters.end()) {
        table = table_it->second;
    }

    utils::terminal::printInfo("[Tool: DBSchema]");
    if (!table.empty()) {
        std::cout << utils::terminal::CYAN << "Table: " << table << utils::terminal::RESET << "\n\n";
    }

    auto tables = db_client_->getSchema();

    if (tables.empty()) {
        result.success = false;
        result.error = "Could not retrieve schema";
        utils::terminal::printError(result.error);
        return result;
    }

    // Format schema output
    std::stringstream ss;
    for (const auto& tbl : tables) {
        // If specific table requested, only show that one
        if (!table.empty() && tbl.name != table) continue;

        ss << "Table: " << tbl.name << "\n";
        for (const auto& col : tbl.columns) {
            ss << "  " << col.name << " " << col.type;
            if (col.primary_key) ss << " PRIMARY KEY";
            if (col.nullable) ss << " NULL";
            else ss << " NOT NULL";
            if (!col.default_value.empty()) ss << " DEFAULT " << col.default_value;
            ss << "\n";
        }
        ss << "\n";
    }

    result.output = ss.str();
    result.success = true;
    result.exit_code = 0;

    utils::terminal::printSuccess("Schema retrieved");
    std::cout << "\n=== Schema ===\n" << result.output << "==============\n\n";

    return result;
}

// ============================================================================
// RAG Tools Implementation
// ============================================================================

ToolResult ToolExecutor::executeLearn(const ToolCall& tool_call) {
    ToolResult result;

    if (!rag_engine_) {
        result.success = false;
        result.error = "RAG engine not initialized";
        utils::terminal::printError(result.error);
        return result;
    }

    if (!rag_engine_->isInitialized()) {
        result.success = false;
        result.error = "RAG engine not initialized - check vector database settings";
        utils::terminal::printError(result.error);
        return result;
    }

    auto source_it = tool_call.parameters.find("source");
    if (source_it == tool_call.parameters.end()) {
        result.success = false;
        result.error = "Missing 'source' parameter";
        return result;
    }

    std::string source = source_it->second;
    std::string pattern = "*";
    std::string content;

    auto pattern_it = tool_call.parameters.find("pattern");
    if (pattern_it != tool_call.parameters.end()) {
        pattern = pattern_it->second;
    }

    auto content_it = tool_call.parameters.find("content");
    if (content_it != tool_call.parameters.end()) {
        content = content_it->second;
    }

    utils::terminal::printInfo("[Tool: Learn]");
    std::cout << utils::terminal::CYAN << "Source: " << source << utils::terminal::RESET << "\n";
    if (!pattern.empty() && pattern != "*") {
        std::cout << utils::terminal::CYAN << "Pattern: " << pattern << utils::terminal::RESET << "\n";
    }
    std::cout << "\n";

    // Confirmation
    if (!requestConfirmation("Learn", "Index content into vector database?")) {
        result.success = false;
        result.error = "Cancelled by user";
        utils::terminal::printError("Cancelled");
        return result;
    }

    utils::terminal::printInfo("Learning...");

    LearnResult learn_result;

    if (source == "text" && !content.empty()) {
        learn_result = rag_engine_->learnText(content, "text_input");
    } else if (source.find("http://") == 0 || source.find("https://") == 0) {
        learn_result = rag_engine_->learnUrl(source);
    } else if (utils::dirExists(source)) {
        learn_result = rag_engine_->learnDirectory(source, pattern);
    } else if (utils::fileExists(source)) {
        learn_result = rag_engine_->learnFile(source);
    } else {
        result.success = false;
        result.error = "Source not found: " + source;
        utils::terminal::printError(result.error);
        return result;
    }

    if (!learn_result.success) {
        result.success = false;
        result.error = "Learn failed: " + learn_result.error;
        utils::terminal::printError(result.error);
        return result;
    }

    std::stringstream ss;
    ss << "Learned from: " << source << "\n";
    ss << "Documents indexed: " << learn_result.documents_added << "\n";
    ss << "Chunks created: " << learn_result.chunks_created << "\n";

    result.output = ss.str();
    result.success = true;
    result.exit_code = 0;

    utils::terminal::printSuccess("Learning complete");
    std::cout << "\n" << result.output << "\n";

    return result;
}

ToolResult ToolExecutor::executeRemember(const ToolCall& tool_call) {
    ToolResult result;

    if (!rag_engine_) {
        result.success = false;
        result.error = "RAG engine not initialized";
        utils::terminal::printError(result.error);
        return result;
    }

    if (!rag_engine_->isInitialized()) {
        result.success = false;
        result.error = "RAG engine not initialized - check vector database settings";
        utils::terminal::printError(result.error);
        return result;
    }

    auto query_it = tool_call.parameters.find("query");
    if (query_it == tool_call.parameters.end()) {
        result.success = false;
        result.error = "Missing 'query' parameter";
        return result;
    }

    std::string query = query_it->second;
    int max_results = 5;

    auto max_it = tool_call.parameters.find("max_results");
    if (max_it != tool_call.parameters.end()) {
        try {
            max_results = std::stoi(max_it->second);
        } catch (...) {}
    }

    utils::terminal::printInfo("[Tool: Remember]");
    std::cout << utils::terminal::CYAN << "Query: " << query << utils::terminal::RESET << "\n";
    std::cout << utils::terminal::CYAN << "Max results: " << max_results << utils::terminal::RESET << "\n\n";

    utils::terminal::printInfo("Searching memory...");

    auto context = rag_engine_->retrieve(query, max_results);

    if (context.results.empty()) {
        result.output = "No relevant context found in memory.";
        result.success = true;
        result.exit_code = 0;
        utils::terminal::printInfo(result.output);
        return result;
    }

    result.output = context.formatted_context;
    result.success = true;
    result.exit_code = 0;

    utils::terminal::printSuccess("Found " + std::to_string(context.results.size()) + " relevant chunks");
    std::cout << "\n" << result.output << "\n";

    return result;
}

ToolResult ToolExecutor::executeForget(const ToolCall& tool_call) {
    ToolResult result;

    if (!rag_engine_) {
        result.success = false;
        result.error = "RAG engine not initialized";
        utils::terminal::printError(result.error);
        return result;
    }

    if (!rag_engine_->isInitialized()) {
        result.success = false;
        result.error = "RAG engine not initialized - check vector database settings";
        utils::terminal::printError(result.error);
        return result;
    }

    auto source_it = tool_call.parameters.find("source");
    if (source_it == tool_call.parameters.end()) {
        result.success = false;
        result.error = "Missing 'source' parameter";
        return result;
    }

    std::string source = source_it->second;

    utils::terminal::printInfo("[Tool: Forget]");
    std::cout << utils::terminal::CYAN << "Source: " << source << utils::terminal::RESET << "\n\n";

    // Confirmation
    if (!requestConfirmation("Forget", "Remove content from vector database?")) {
        result.success = false;
        result.error = "Cancelled by user";
        utils::terminal::printError("Cancelled");
        return result;
    }

    utils::terminal::printInfo("Forgetting...");

    bool success;
    if (source == "*" || source == "all") {
        success = rag_engine_->forgetAll();
        result.output = "All content removed from vector database.";
    } else {
        success = rag_engine_->forget(source);
        result.output = "Removed content from: " + source;
    }

    if (!success) {
        result.success = false;
        result.error = "Failed to remove content";
        utils::terminal::printError(result.error);
        return result;
    }

    result.success = true;
    result.exit_code = 0;

    utils::terminal::printSuccess(result.output);

    return result;
}

ToolResult ToolExecutor::execute(const ToolCall& tool_call) {
    // Check if this is an MCP tool
    if (isMCPTool(tool_call.name)) {
        return executeMCPTool(tool_call);
    }

    if (tool_call.name == "Bash") {
        return executeBash(tool_call);
    } else if (tool_call.name == "Read") {
        return executeRead(tool_call);
    } else if (tool_call.name == "Write") {
        return executeWrite(tool_call);
    } else if (tool_call.name == "Edit") {
        return executeEdit(tool_call);
    } else if (tool_call.name == "Glob") {
        return executeGlob(tool_call);
    } else if (tool_call.name == "Grep") {
        return executeGrep(tool_call);
    }
    // Search tools
    else if (tool_call.name == "WebSearch") {
        return executeWebSearch(tool_call);
    } else if (tool_call.name == "WebFetch") {
        return executeWebFetch(tool_call);
    }
    // Database tools
    else if (tool_call.name == "DBConnect") {
        return executeDBConnect(tool_call);
    } else if (tool_call.name == "DBQuery") {
        return executeDBQuery(tool_call);
    } else if (tool_call.name == "DBExecute") {
        return executeDBExecute(tool_call);
    } else if (tool_call.name == "DBSchema") {
        return executeDBSchema(tool_call);
    }
    // RAG tools
    else if (tool_call.name == "Learn") {
        return executeLearn(tool_call);
    } else if (tool_call.name == "Remember") {
        return executeRemember(tool_call);
    } else if (tool_call.name == "Forget") {
        return executeForget(tool_call);
    }
    else {
        ToolResult result;
        result.success = false;
        result.error = "Unknown tool: " + tool_call.name;
        utils::terminal::printError(result.error);
        return result;
    }
}

std::vector<ToolResult> ToolExecutor::executeAll(const std::vector<ToolCall>& tool_calls) {
    std::vector<ToolResult> results;

    for (size_t i = 0; i < tool_calls.size(); i++) {
        std::cout << utils::terminal::MAGENTA << "═══════════════════════════════════════" << utils::terminal::RESET << "\n";
        std::cout << utils::terminal::MAGENTA << "Tool " << (i+1) << "/" << tool_calls.size() << ": " << tool_calls[i].name << utils::terminal::RESET << "\n";
        std::cout << utils::terminal::MAGENTA << "═══════════════════════════════════════" << utils::terminal::RESET << "\n\n";

        results.push_back(execute(tool_calls[i]));
    }

    return results;
}

} // namespace oleg
