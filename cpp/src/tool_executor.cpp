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

namespace casper {

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

// ============================================================================
// Network Tools Implementation
// ============================================================================

ToolResult ToolExecutor::executePing(const ToolCall& tool_call) {
    ToolResult result;

    auto host_it = tool_call.parameters.find("host");
    if (host_it == tool_call.parameters.end()) {
        result.success = false;
        result.error = "Missing 'host' parameter";
        return result;
    }

    std::string host = host_it->second;
    int count = 4;

    auto count_it = tool_call.parameters.find("count");
    if (count_it != tool_call.parameters.end()) {
        try {
            count = std::stoi(count_it->second);
            if (count > 20) count = 20;  // Limit max pings
        } catch (...) {}
    }

    utils::terminal::printInfo("[Tool: Ping]");
    std::cout << utils::terminal::CYAN << "Host: " << host << utils::terminal::RESET << "\n";
    std::cout << utils::terminal::CYAN << "Count: " << count << utils::terminal::RESET << "\n\n";

    if (!requestConfirmation("Ping", "Ping " + host + "?")) {
        result.success = false;
        result.error = "Cancelled by user";
        utils::terminal::printError("Cancelled");
        return result;
    }

    utils::terminal::printInfo("Pinging...");
    std::string command = "ping -c " + std::to_string(count) + " " + host;
    result.output = executeCommand(command, result.exit_code);
    result.success = (result.exit_code == 0);

    if (result.success) {
        utils::terminal::printSuccess("Ping complete");
    } else {
        utils::terminal::printError("Ping failed");
    }

    std::cout << "\n=== Output ===\n" << result.output << "==============\n\n";
    return result;
}

ToolResult ToolExecutor::executeTraceroute(const ToolCall& tool_call) {
    ToolResult result;

    auto host_it = tool_call.parameters.find("host");
    if (host_it == tool_call.parameters.end()) {
        result.success = false;
        result.error = "Missing 'host' parameter";
        return result;
    }

    std::string host = host_it->second;
    int max_hops = 30;

    auto hops_it = tool_call.parameters.find("max_hops");
    if (hops_it != tool_call.parameters.end()) {
        try {
            max_hops = std::stoi(hops_it->second);
            if (max_hops > 64) max_hops = 64;
        } catch (...) {}
    }

    utils::terminal::printInfo("[Tool: Traceroute]");
    std::cout << utils::terminal::CYAN << "Host: " << host << utils::terminal::RESET << "\n";
    std::cout << utils::terminal::CYAN << "Max hops: " << max_hops << utils::terminal::RESET << "\n\n";

    if (!requestConfirmation("Traceroute", "Trace route to " + host + "?")) {
        result.success = false;
        result.error = "Cancelled by user";
        utils::terminal::printError("Cancelled");
        return result;
    }

    utils::terminal::printInfo("Tracing route...");
    std::string command = "traceroute -m " + std::to_string(max_hops) + " " + host;
    result.output = executeCommand(command, result.exit_code);
    result.success = (result.exit_code == 0);

    if (result.success) {
        utils::terminal::printSuccess("Traceroute complete");
    } else {
        utils::terminal::printError("Traceroute failed");
    }

    std::cout << "\n=== Output ===\n" << result.output << "==============\n\n";
    return result;
}

ToolResult ToolExecutor::executeNmap(const ToolCall& tool_call) {
    ToolResult result;

    auto target_it = tool_call.parameters.find("target");
    if (target_it == tool_call.parameters.end()) {
        result.success = false;
        result.error = "Missing 'target' parameter";
        return result;
    }

    std::string target = target_it->second;
    std::string ports = "";
    std::string scan_type = "-sT";  // Default TCP connect scan

    auto ports_it = tool_call.parameters.find("ports");
    if (ports_it != tool_call.parameters.end()) {
        ports = "-p " + ports_it->second;
    }

    auto type_it = tool_call.parameters.find("scan_type");
    if (type_it != tool_call.parameters.end()) {
        std::string st = type_it->second;
        if (st == "syn" || st == "SYN") scan_type = "-sS";
        else if (st == "udp" || st == "UDP") scan_type = "-sU";
        else if (st == "ping") scan_type = "-sn";
        else if (st == "version") scan_type = "-sV";
        else if (st == "os") scan_type = "-O";
    }

    utils::terminal::printInfo("[Tool: Nmap]");
    std::cout << utils::terminal::CYAN << "Target: " << target << utils::terminal::RESET << "\n";
    std::cout << utils::terminal::CYAN << "Scan type: " << scan_type << utils::terminal::RESET << "\n";
    if (!ports.empty()) {
        std::cout << utils::terminal::CYAN << "Ports: " << ports << utils::terminal::RESET << "\n";
    }
    std::cout << "\n";

    if (!requestConfirmation("Nmap", "Scan " + target + "?")) {
        result.success = false;
        result.error = "Cancelled by user";
        utils::terminal::printError("Cancelled");
        return result;
    }

    utils::terminal::printInfo("Scanning (this may take a while)...");
    std::string command = "nmap " + scan_type + " " + ports + " " + target;
    result.output = executeCommand(command, result.exit_code);
    result.success = (result.exit_code == 0);

    if (result.success) {
        utils::terminal::printSuccess("Scan complete");
    } else {
        if (result.output.find("command not found") != std::string::npos) {
            result.error = "nmap not installed. Install with: brew install nmap";
        }
        utils::terminal::printError("Scan failed");
    }

    std::cout << "\n=== Output ===\n" << result.output << "==============\n\n";
    return result;
}

ToolResult ToolExecutor::executeDig(const ToolCall& tool_call) {
    ToolResult result;

    auto domain_it = tool_call.parameters.find("domain");
    if (domain_it == tool_call.parameters.end()) {
        result.success = false;
        result.error = "Missing 'domain' parameter";
        return result;
    }

    std::string domain = domain_it->second;
    std::string record_type = "A";

    auto type_it = tool_call.parameters.find("type");
    if (type_it != tool_call.parameters.end()) {
        record_type = type_it->second;
    }

    utils::terminal::printInfo("[Tool: Dig]");
    std::cout << utils::terminal::CYAN << "Domain: " << domain << utils::terminal::RESET << "\n";
    std::cout << utils::terminal::CYAN << "Record type: " << record_type << utils::terminal::RESET << "\n\n";

    if (!requestConfirmation("Dig", "DNS lookup for " + domain + "?")) {
        result.success = false;
        result.error = "Cancelled by user";
        utils::terminal::printError("Cancelled");
        return result;
    }

    utils::terminal::printInfo("Looking up...");
    std::string command = "dig " + domain + " " + record_type + " +short";
    result.output = executeCommand(command, result.exit_code);

    // Also get full output
    std::string full_command = "dig " + domain + " " + record_type;
    int full_exit;
    std::string full_output = executeCommand(full_command, full_exit);

    result.success = (result.exit_code == 0);

    if (result.success) {
        utils::terminal::printSuccess("DNS lookup complete");
    } else {
        utils::terminal::printError("DNS lookup failed");
    }

    std::cout << "\n=== Short Answer ===\n" << result.output << "\n=== Full Output ===\n" << full_output << "==============\n\n";
    result.output = "Short: " + result.output + "\nFull:\n" + full_output;
    return result;
}

ToolResult ToolExecutor::executeWhois(const ToolCall& tool_call) {
    ToolResult result;

    auto domain_it = tool_call.parameters.find("domain");
    if (domain_it == tool_call.parameters.end()) {
        result.success = false;
        result.error = "Missing 'domain' parameter";
        return result;
    }

    std::string domain = domain_it->second;

    utils::terminal::printInfo("[Tool: Whois]");
    std::cout << utils::terminal::CYAN << "Domain: " << domain << utils::terminal::RESET << "\n\n";

    if (!requestConfirmation("Whois", "WHOIS lookup for " + domain + "?")) {
        result.success = false;
        result.error = "Cancelled by user";
        utils::terminal::printError("Cancelled");
        return result;
    }

    utils::terminal::printInfo("Looking up...");
    std::string command = "whois " + domain;
    result.output = executeCommand(command, result.exit_code);
    result.success = (result.exit_code == 0);

    if (result.success) {
        utils::terminal::printSuccess("WHOIS lookup complete");
    } else {
        utils::terminal::printError("WHOIS lookup failed");
    }

    std::cout << "\n=== Output ===\n" << result.output << "==============\n\n";
    return result;
}

ToolResult ToolExecutor::executeNetstat(const ToolCall& tool_call) {
    ToolResult result;

    std::string flags = "-an";

    auto flags_it = tool_call.parameters.find("flags");
    if (flags_it != tool_call.parameters.end()) {
        flags = flags_it->second;
    }

    auto filter_it = tool_call.parameters.find("filter");
    std::string filter = "";
    if (filter_it != tool_call.parameters.end()) {
        filter = filter_it->second;
    }

    utils::terminal::printInfo("[Tool: Netstat]");
    std::cout << utils::terminal::CYAN << "Flags: " << flags << utils::terminal::RESET << "\n";
    if (!filter.empty()) {
        std::cout << utils::terminal::CYAN << "Filter: " << filter << utils::terminal::RESET << "\n";
    }
    std::cout << "\n";

    if (!requestConfirmation("Netstat", "Show network statistics?")) {
        result.success = false;
        result.error = "Cancelled by user";
        utils::terminal::printError("Cancelled");
        return result;
    }

    utils::terminal::printInfo("Getting network stats...");
    std::string command = "netstat " + flags;
    if (!filter.empty()) {
        command += " | grep -i '" + filter + "'";
    }
    command += " | head -100";

    result.output = executeCommand(command, result.exit_code);
    result.success = true;  // netstat always succeeds

    utils::terminal::printSuccess("Netstat complete");
    std::cout << "\n=== Output ===\n" << result.output << "==============\n\n";
    return result;
}

ToolResult ToolExecutor::executeCurl(const ToolCall& tool_call) {
    ToolResult result;

    auto url_it = tool_call.parameters.find("url");
    if (url_it == tool_call.parameters.end()) {
        result.success = false;
        result.error = "Missing 'url' parameter";
        return result;
    }

    std::string url = url_it->second;
    std::string method = "GET";
    std::string data = "";
    std::string headers = "";
    bool show_headers = false;

    auto method_it = tool_call.parameters.find("method");
    if (method_it != tool_call.parameters.end()) {
        method = method_it->second;
    }

    auto data_it = tool_call.parameters.find("data");
    if (data_it != tool_call.parameters.end()) {
        data = data_it->second;
    }

    auto headers_it = tool_call.parameters.find("headers");
    if (headers_it != tool_call.parameters.end()) {
        headers = headers_it->second;
    }

    auto show_headers_it = tool_call.parameters.find("show_headers");
    if (show_headers_it != tool_call.parameters.end()) {
        show_headers = (show_headers_it->second == "true" || show_headers_it->second == "1");
    }

    utils::terminal::printInfo("[Tool: Curl]");
    std::cout << utils::terminal::CYAN << "URL: " << url << utils::terminal::RESET << "\n";
    std::cout << utils::terminal::CYAN << "Method: " << method << utils::terminal::RESET << "\n";
    if (!data.empty()) {
        std::cout << utils::terminal::CYAN << "Data: " << data.substr(0, 100) << (data.length() > 100 ? "..." : "") << utils::terminal::RESET << "\n";
    }
    std::cout << "\n";

    if (!requestConfirmation("Curl", "Make HTTP request to " + url + "?")) {
        result.success = false;
        result.error = "Cancelled by user";
        utils::terminal::printError("Cancelled");
        return result;
    }

    utils::terminal::printInfo("Making request...");
    std::string command = "curl -s";
    if (show_headers) command += " -i";
    command += " -X " + method;
    if (!data.empty()) {
        command += " -d '" + data + "'";
    }
    if (!headers.empty()) {
        command += " -H '" + headers + "'";
    }
    command += " '" + url + "'";

    result.output = executeCommand(command, result.exit_code);
    result.success = (result.exit_code == 0);

    if (result.success) {
        utils::terminal::printSuccess("Request complete");
    } else {
        utils::terminal::printError("Request failed");
    }

    // Truncate very long output
    std::string display = result.output;
    if (display.length() > 5000) {
        display = display.substr(0, 5000) + "\n...(truncated)";
    }
    std::cout << "\n=== Output ===\n" << display << "\n==============\n\n";
    return result;
}

ToolResult ToolExecutor::executeSSH(const ToolCall& tool_call) {
    ToolResult result;

    auto host_it = tool_call.parameters.find("host");
    if (host_it == tool_call.parameters.end()) {
        result.success = false;
        result.error = "Missing 'host' parameter";
        return result;
    }

    std::string host = host_it->second;
    std::string user = "";
    std::string command_to_run = "";
    int port = 22;

    auto user_it = tool_call.parameters.find("user");
    if (user_it != tool_call.parameters.end()) {
        user = user_it->second;
    }

    auto cmd_it = tool_call.parameters.find("command");
    if (cmd_it != tool_call.parameters.end()) {
        command_to_run = cmd_it->second;
    }

    auto port_it = tool_call.parameters.find("port");
    if (port_it != tool_call.parameters.end()) {
        try {
            port = std::stoi(port_it->second);
        } catch (...) {}
    }

    utils::terminal::printInfo("[Tool: SSH]");
    std::cout << utils::terminal::CYAN << "Host: " << host << utils::terminal::RESET << "\n";
    if (!user.empty()) {
        std::cout << utils::terminal::CYAN << "User: " << user << utils::terminal::RESET << "\n";
    }
    std::cout << utils::terminal::CYAN << "Port: " << port << utils::terminal::RESET << "\n";
    if (!command_to_run.empty()) {
        std::cout << utils::terminal::CYAN << "Command: " << command_to_run << utils::terminal::RESET << "\n";
    }
    std::cout << "\n";

    if (command_to_run.empty()) {
        result.success = false;
        result.error = "Interactive SSH sessions not supported. Please provide a command to execute.";
        utils::terminal::printError(result.error);
        return result;
    }

    if (!requestConfirmation("SSH", "Execute command on " + host + "?")) {
        result.success = false;
        result.error = "Cancelled by user";
        utils::terminal::printError("Cancelled");
        return result;
    }

    utils::terminal::printInfo("Connecting...");
    std::string ssh_cmd = "ssh -o BatchMode=yes -o ConnectTimeout=10 -p " + std::to_string(port);
    if (!user.empty()) {
        ssh_cmd += " " + user + "@" + host;
    } else {
        ssh_cmd += " " + host;
    }
    ssh_cmd += " '" + command_to_run + "'";

    result.output = executeCommand(ssh_cmd, result.exit_code);
    result.success = (result.exit_code == 0);

    if (result.success) {
        utils::terminal::printSuccess("SSH command complete");
    } else {
        utils::terminal::printError("SSH command failed");
    }

    std::cout << "\n=== Output ===\n" << result.output << "==============\n\n";
    return result;
}

ToolResult ToolExecutor::executeTelnet(const ToolCall& tool_call) {
    ToolResult result;

    auto host_it = tool_call.parameters.find("host");
    if (host_it == tool_call.parameters.end()) {
        result.success = false;
        result.error = "Missing 'host' parameter";
        return result;
    }

    std::string host = host_it->second;
    int port = 23;

    auto port_it = tool_call.parameters.find("port");
    if (port_it != tool_call.parameters.end()) {
        try {
            port = std::stoi(port_it->second);
        } catch (...) {}
    }

    utils::terminal::printInfo("[Tool: Telnet]");
    std::cout << utils::terminal::CYAN << "Host: " << host << utils::terminal::RESET << "\n";
    std::cout << utils::terminal::CYAN << "Port: " << port << utils::terminal::RESET << "\n\n";

    if (!requestConfirmation("Telnet", "Test connection to " + host + ":" + std::to_string(port) + "?")) {
        result.success = false;
        result.error = "Cancelled by user";
        utils::terminal::printError("Cancelled");
        return result;
    }

    utils::terminal::printInfo("Testing connection...");
    // Use nc for non-interactive telnet testing
    std::string command = "nc -z -v -w 5 " + host + " " + std::to_string(port);
    result.output = executeCommand(command, result.exit_code);
    result.success = (result.exit_code == 0);

    if (result.success) {
        utils::terminal::printSuccess("Connection successful - port is open");
        result.output = "Port " + std::to_string(port) + " on " + host + " is open\n" + result.output;
    } else {
        utils::terminal::printError("Connection failed - port may be closed or filtered");
    }

    std::cout << "\n=== Output ===\n" << result.output << "==============\n\n";
    return result;
}

ToolResult ToolExecutor::executeNetcat(const ToolCall& tool_call) {
    ToolResult result;

    auto host_it = tool_call.parameters.find("host");
    if (host_it == tool_call.parameters.end()) {
        result.success = false;
        result.error = "Missing 'host' parameter";
        return result;
    }

    std::string host = host_it->second;
    int port = 0;
    std::string mode = "connect";
    std::string data = "";

    auto port_it = tool_call.parameters.find("port");
    if (port_it != tool_call.parameters.end()) {
        try {
            port = std::stoi(port_it->second);
        } catch (...) {}
    }

    auto mode_it = tool_call.parameters.find("mode");
    if (mode_it != tool_call.parameters.end()) {
        mode = mode_it->second;
    }

    auto data_it = tool_call.parameters.find("data");
    if (data_it != tool_call.parameters.end()) {
        data = data_it->second;
    }

    if (port == 0) {
        result.success = false;
        result.error = "Missing 'port' parameter";
        return result;
    }

    utils::terminal::printInfo("[Tool: Netcat]");
    std::cout << utils::terminal::CYAN << "Host: " << host << utils::terminal::RESET << "\n";
    std::cout << utils::terminal::CYAN << "Port: " << port << utils::terminal::RESET << "\n";
    std::cout << utils::terminal::CYAN << "Mode: " << mode << utils::terminal::RESET << "\n\n";

    if (!requestConfirmation("Netcat", "Connect to " + host + ":" + std::to_string(port) + "?")) {
        result.success = false;
        result.error = "Cancelled by user";
        utils::terminal::printError("Cancelled");
        return result;
    }

    utils::terminal::printInfo("Connecting...");
    std::string command;
    if (mode == "scan") {
        command = "nc -z -v -w 2 " + host + " " + std::to_string(port);
    } else if (!data.empty()) {
        command = "echo '" + data + "' | nc -w 5 " + host + " " + std::to_string(port);
    } else {
        command = "nc -z -v -w 5 " + host + " " + std::to_string(port);
    }

    result.output = executeCommand(command, result.exit_code);
    result.success = (result.exit_code == 0);

    if (result.success) {
        utils::terminal::printSuccess("Netcat complete");
    } else {
        utils::terminal::printError("Netcat failed");
    }

    std::cout << "\n=== Output ===\n" << result.output << "==============\n\n";
    return result;
}

ToolResult ToolExecutor::executeIfconfig(const ToolCall& tool_call) {
    ToolResult result;

    std::string interface = "";
    auto iface_it = tool_call.parameters.find("interface");
    if (iface_it != tool_call.parameters.end()) {
        interface = iface_it->second;
    }

    utils::terminal::printInfo("[Tool: Ifconfig]");
    if (!interface.empty()) {
        std::cout << utils::terminal::CYAN << "Interface: " << interface << utils::terminal::RESET << "\n";
    }
    std::cout << "\n";

    if (!requestConfirmation("Ifconfig", "Show network interfaces?")) {
        result.success = false;
        result.error = "Cancelled by user";
        utils::terminal::printError("Cancelled");
        return result;
    }

    utils::terminal::printInfo("Getting interface info...");
    std::string command = "ifconfig " + interface;
    result.output = executeCommand(command, result.exit_code);
    result.success = (result.exit_code == 0);

    if (result.success) {
        utils::terminal::printSuccess("Ifconfig complete");
    } else {
        utils::terminal::printError("Ifconfig failed");
    }

    std::cout << "\n=== Output ===\n" << result.output << "==============\n\n";
    return result;
}

ToolResult ToolExecutor::executeArp(const ToolCall& tool_call) {
    ToolResult result;

    std::string flags = "-a";
    auto flags_it = tool_call.parameters.find("flags");
    if (flags_it != tool_call.parameters.end()) {
        flags = flags_it->second;
    }

    utils::terminal::printInfo("[Tool: ARP]");
    std::cout << utils::terminal::CYAN << "Flags: " << flags << utils::terminal::RESET << "\n\n";

    if (!requestConfirmation("ARP", "Show ARP table?")) {
        result.success = false;
        result.error = "Cancelled by user";
        utils::terminal::printError("Cancelled");
        return result;
    }

    utils::terminal::printInfo("Getting ARP table...");
    std::string command = "arp " + flags;
    result.output = executeCommand(command, result.exit_code);
    result.success = (result.exit_code == 0);

    if (result.success) {
        utils::terminal::printSuccess("ARP complete");
    } else {
        utils::terminal::printError("ARP failed");
    }

    std::cout << "\n=== Output ===\n" << result.output << "==============\n\n";
    return result;
}

// ============================================================================
// Package Manager Tools Implementation
// ============================================================================

ToolResult ToolExecutor::executeBrew(const ToolCall& tool_call) {
    ToolResult result;

    auto action_it = tool_call.parameters.find("action");
    if (action_it == tool_call.parameters.end()) {
        result.success = false;
        result.error = "Missing 'action' parameter (install, uninstall, update, upgrade, search, info, list)";
        return result;
    }

    std::string action = action_it->second;
    std::string package = "";

    auto pkg_it = tool_call.parameters.find("package");
    if (pkg_it != tool_call.parameters.end()) {
        package = pkg_it->second;
    }

    // Validate action
    std::vector<std::string> valid_actions = {"install", "uninstall", "remove", "update", "upgrade", "search", "info", "list", "outdated", "cleanup"};
    bool valid = false;
    for (const auto& a : valid_actions) {
        if (action == a) { valid = true; break; }
    }
    if (!valid) {
        result.success = false;
        result.error = "Invalid action. Use: install, uninstall, update, upgrade, search, info, list, outdated, cleanup";
        return result;
    }

    // Some actions require a package name
    if ((action == "install" || action == "uninstall" || action == "remove" || action == "info") && package.empty()) {
        result.success = false;
        result.error = "Action '" + action + "' requires a package name";
        return result;
    }

    utils::terminal::printInfo("[Tool: Brew]");
    std::cout << utils::terminal::CYAN << "Action: " << action << utils::terminal::RESET << "\n";
    if (!package.empty()) {
        std::cout << utils::terminal::CYAN << "Package: " << package << utils::terminal::RESET << "\n";
    }
    std::cout << "\n";

    std::string desc = "brew " + action + (package.empty() ? "" : " " + package);
    if (!requestConfirmation("Brew", desc + "?")) {
        result.success = false;
        result.error = "Cancelled by user";
        utils::terminal::printError("Cancelled");
        return result;
    }

    utils::terminal::printInfo("Running brew...");
    std::string command = "brew " + action;
    if (!package.empty()) command += " " + package;

    result.output = executeCommand(command, result.exit_code);
    result.success = (result.exit_code == 0);

    if (result.success) {
        utils::terminal::printSuccess("Brew command complete");
    } else {
        utils::terminal::printError("Brew command failed");
    }

    std::cout << "\n=== Output ===\n" << result.output << "==============\n\n";
    return result;
}

ToolResult ToolExecutor::executePip(const ToolCall& tool_call) {
    ToolResult result;

    auto action_it = tool_call.parameters.find("action");
    if (action_it == tool_call.parameters.end()) {
        result.success = false;
        result.error = "Missing 'action' parameter (install, uninstall, list, show, search, freeze)";
        return result;
    }

    std::string action = action_it->second;
    std::string package = "";
    bool use_pip3 = true;

    auto pkg_it = tool_call.parameters.find("package");
    if (pkg_it != tool_call.parameters.end()) {
        package = pkg_it->second;
    }

    auto pip3_it = tool_call.parameters.find("pip3");
    if (pip3_it != tool_call.parameters.end()) {
        use_pip3 = (pip3_it->second != "false" && pip3_it->second != "0");
    }

    std::vector<std::string> valid_actions = {"install", "uninstall", "list", "show", "search", "freeze", "upgrade"};
    bool valid = false;
    for (const auto& a : valid_actions) {
        if (action == a) { valid = true; break; }
    }
    if (!valid) {
        result.success = false;
        result.error = "Invalid action. Use: install, uninstall, list, show, search, freeze, upgrade";
        return result;
    }

    if ((action == "install" || action == "uninstall" || action == "show" || action == "upgrade") && package.empty()) {
        result.success = false;
        result.error = "Action '" + action + "' requires a package name";
        return result;
    }

    utils::terminal::printInfo("[Tool: Pip]");
    std::cout << utils::terminal::CYAN << "Action: " << action << utils::terminal::RESET << "\n";
    if (!package.empty()) {
        std::cout << utils::terminal::CYAN << "Package: " << package << utils::terminal::RESET << "\n";
    }
    std::cout << "\n";

    std::string pip_cmd = use_pip3 ? "pip3" : "pip";
    std::string desc = pip_cmd + " " + action + (package.empty() ? "" : " " + package);
    if (!requestConfirmation("Pip", desc + "?")) {
        result.success = false;
        result.error = "Cancelled by user";
        utils::terminal::printError("Cancelled");
        return result;
    }

    utils::terminal::printInfo("Running pip...");
    std::string command = pip_cmd + " " + action;
    if (action == "upgrade") {
        command = pip_cmd + " install --upgrade " + package;
    } else if (!package.empty()) {
        command += " " + package;
    }

    result.output = executeCommand(command, result.exit_code);
    result.success = (result.exit_code == 0);

    if (result.success) {
        utils::terminal::printSuccess("Pip command complete");
    } else {
        utils::terminal::printError("Pip command failed");
    }

    std::cout << "\n=== Output ===\n" << result.output << "==============\n\n";
    return result;
}

ToolResult ToolExecutor::executeNpm(const ToolCall& tool_call) {
    ToolResult result;

    auto action_it = tool_call.parameters.find("action");
    if (action_it == tool_call.parameters.end()) {
        result.success = false;
        result.error = "Missing 'action' parameter (install, uninstall, update, list, search, info, init, run)";
        return result;
    }

    std::string action = action_it->second;
    std::string package = "";
    bool global = false;

    auto pkg_it = tool_call.parameters.find("package");
    if (pkg_it != tool_call.parameters.end()) {
        package = pkg_it->second;
    }

    auto global_it = tool_call.parameters.find("global");
    if (global_it != tool_call.parameters.end()) {
        global = (global_it->second == "true" || global_it->second == "1");
    }

    std::vector<std::string> valid_actions = {"install", "uninstall", "update", "list", "search", "info", "init", "run", "outdated", "audit"};
    bool valid = false;
    for (const auto& a : valid_actions) {
        if (action == a) { valid = true; break; }
    }
    if (!valid) {
        result.success = false;
        result.error = "Invalid action. Use: install, uninstall, update, list, search, info, init, run, outdated, audit";
        return result;
    }

    utils::terminal::printInfo("[Tool: Npm]");
    std::cout << utils::terminal::CYAN << "Action: " << action << utils::terminal::RESET << "\n";
    if (!package.empty()) {
        std::cout << utils::terminal::CYAN << "Package: " << package << utils::terminal::RESET << "\n";
    }
    if (global) {
        std::cout << utils::terminal::CYAN << "Global: yes" << utils::terminal::RESET << "\n";
    }
    std::cout << "\n";

    std::string desc = "npm " + action + (package.empty() ? "" : " " + package) + (global ? " (global)" : "");
    if (!requestConfirmation("Npm", desc + "?")) {
        result.success = false;
        result.error = "Cancelled by user";
        utils::terminal::printError("Cancelled");
        return result;
    }

    utils::terminal::printInfo("Running npm...");
    std::string command = "npm " + action;
    if (global) command += " -g";
    if (!package.empty()) command += " " + package;

    result.output = executeCommand(command, result.exit_code);
    result.success = (result.exit_code == 0);

    if (result.success) {
        utils::terminal::printSuccess("Npm command complete");
    } else {
        utils::terminal::printError("Npm command failed");
    }

    std::cout << "\n=== Output ===\n" << result.output << "==============\n\n";
    return result;
}

ToolResult ToolExecutor::executeApt(const ToolCall& tool_call) {
    ToolResult result;

    auto action_it = tool_call.parameters.find("action");
    if (action_it == tool_call.parameters.end()) {
        result.success = false;
        result.error = "Missing 'action' parameter (install, remove, update, upgrade, search, show, list)";
        return result;
    }

    std::string action = action_it->second;
    std::string package = "";

    auto pkg_it = tool_call.parameters.find("package");
    if (pkg_it != tool_call.parameters.end()) {
        package = pkg_it->second;
    }

    std::vector<std::string> valid_actions = {"install", "remove", "purge", "update", "upgrade", "full-upgrade", "search", "show", "list", "autoremove"};
    bool valid = false;
    for (const auto& a : valid_actions) {
        if (action == a) { valid = true; break; }
    }
    if (!valid) {
        result.success = false;
        result.error = "Invalid action. Use: install, remove, purge, update, upgrade, full-upgrade, search, show, list, autoremove";
        return result;
    }

    if ((action == "install" || action == "remove" || action == "purge" || action == "show") && package.empty()) {
        result.success = false;
        result.error = "Action '" + action + "' requires a package name";
        return result;
    }

    utils::terminal::printInfo("[Tool: Apt]");
    std::cout << utils::terminal::CYAN << "Action: " << action << utils::terminal::RESET << "\n";
    if (!package.empty()) {
        std::cout << utils::terminal::CYAN << "Package: " << package << utils::terminal::RESET << "\n";
    }
    std::cout << "\n";

    std::string desc = "apt " + action + (package.empty() ? "" : " " + package);
    if (!requestConfirmation("Apt", desc + "?")) {
        result.success = false;
        result.error = "Cancelled by user";
        utils::terminal::printError("Cancelled");
        return result;
    }

    utils::terminal::printInfo("Running apt...");
    std::string command = "sudo apt " + action + " -y";
    if (!package.empty()) command += " " + package;

    result.output = executeCommand(command, result.exit_code);
    result.success = (result.exit_code == 0);

    if (result.success) {
        utils::terminal::printSuccess("Apt command complete");
    } else {
        utils::terminal::printError("Apt command failed");
    }

    std::cout << "\n=== Output ===\n" << result.output << "==============\n\n";
    return result;
}

ToolResult ToolExecutor::executeDnf(const ToolCall& tool_call) {
    ToolResult result;

    auto action_it = tool_call.parameters.find("action");
    if (action_it == tool_call.parameters.end()) {
        result.success = false;
        result.error = "Missing 'action' parameter (install, remove, update, upgrade, search, info, list)";
        return result;
    }

    std::string action = action_it->second;
    std::string package = "";

    auto pkg_it = tool_call.parameters.find("package");
    if (pkg_it != tool_call.parameters.end()) {
        package = pkg_it->second;
    }

    std::vector<std::string> valid_actions = {"install", "remove", "update", "upgrade", "search", "info", "list", "check-update", "autoremove", "clean"};
    bool valid = false;
    for (const auto& a : valid_actions) {
        if (action == a) { valid = true; break; }
    }
    if (!valid) {
        result.success = false;
        result.error = "Invalid action. Use: install, remove, update, upgrade, search, info, list, check-update, autoremove, clean";
        return result;
    }

    if ((action == "install" || action == "remove" || action == "info") && package.empty()) {
        result.success = false;
        result.error = "Action '" + action + "' requires a package name";
        return result;
    }

    utils::terminal::printInfo("[Tool: Dnf]");
    std::cout << utils::terminal::CYAN << "Action: " << action << utils::terminal::RESET << "\n";
    if (!package.empty()) {
        std::cout << utils::terminal::CYAN << "Package: " << package << utils::terminal::RESET << "\n";
    }
    std::cout << "\n";

    std::string desc = "dnf " + action + (package.empty() ? "" : " " + package);
    if (!requestConfirmation("Dnf", desc + "?")) {
        result.success = false;
        result.error = "Cancelled by user";
        utils::terminal::printError("Cancelled");
        return result;
    }

    utils::terminal::printInfo("Running dnf...");
    std::string command = "sudo dnf " + action + " -y";
    if (!package.empty()) command += " " + package;

    result.output = executeCommand(command, result.exit_code);
    result.success = (result.exit_code == 0);

    if (result.success) {
        utils::terminal::printSuccess("Dnf command complete");
    } else {
        utils::terminal::printError("Dnf command failed");
    }

    std::cout << "\n=== Output ===\n" << result.output << "==============\n\n";
    return result;
}

ToolResult ToolExecutor::executeYum(const ToolCall& tool_call) {
    ToolResult result;

    auto action_it = tool_call.parameters.find("action");
    if (action_it == tool_call.parameters.end()) {
        result.success = false;
        result.error = "Missing 'action' parameter (install, remove, update, search, info, list)";
        return result;
    }

    std::string action = action_it->second;
    std::string package = "";

    auto pkg_it = tool_call.parameters.find("package");
    if (pkg_it != tool_call.parameters.end()) {
        package = pkg_it->second;
    }

    std::vector<std::string> valid_actions = {"install", "remove", "update", "search", "info", "list", "check-update", "clean"};
    bool valid = false;
    for (const auto& a : valid_actions) {
        if (action == a) { valid = true; break; }
    }
    if (!valid) {
        result.success = false;
        result.error = "Invalid action. Use: install, remove, update, search, info, list, check-update, clean";
        return result;
    }

    if ((action == "install" || action == "remove" || action == "info") && package.empty()) {
        result.success = false;
        result.error = "Action '" + action + "' requires a package name";
        return result;
    }

    utils::terminal::printInfo("[Tool: Yum]");
    std::cout << utils::terminal::CYAN << "Action: " << action << utils::terminal::RESET << "\n";
    if (!package.empty()) {
        std::cout << utils::terminal::CYAN << "Package: " << package << utils::terminal::RESET << "\n";
    }
    std::cout << "\n";

    std::string desc = "yum " + action + (package.empty() ? "" : " " + package);
    if (!requestConfirmation("Yum", desc + "?")) {
        result.success = false;
        result.error = "Cancelled by user";
        utils::terminal::printError("Cancelled");
        return result;
    }

    utils::terminal::printInfo("Running yum...");
    std::string command = "sudo yum " + action + " -y";
    if (!package.empty()) command += " " + package;

    result.output = executeCommand(command, result.exit_code);
    result.success = (result.exit_code == 0);

    if (result.success) {
        utils::terminal::printSuccess("Yum command complete");
    } else {
        utils::terminal::printError("Yum command failed");
    }

    std::cout << "\n=== Output ===\n" << result.output << "==============\n\n";
    return result;
}

ToolResult ToolExecutor::executePacman(const ToolCall& tool_call) {
    ToolResult result;

    auto action_it = tool_call.parameters.find("action");
    if (action_it == tool_call.parameters.end()) {
        result.success = false;
        result.error = "Missing 'action' parameter (install, remove, update, upgrade, search, info, list)";
        return result;
    }

    std::string action = action_it->second;
    std::string package = "";

    auto pkg_it = tool_call.parameters.find("package");
    if (pkg_it != tool_call.parameters.end()) {
        package = pkg_it->second;
    }

    utils::terminal::printInfo("[Tool: Pacman]");
    std::cout << utils::terminal::CYAN << "Action: " << action << utils::terminal::RESET << "\n";
    if (!package.empty()) {
        std::cout << utils::terminal::CYAN << "Package: " << package << utils::terminal::RESET << "\n";
    }
    std::cout << "\n";

    // Map actions to pacman flags
    std::string command;
    if (action == "install") {
        if (package.empty()) {
            result.success = false;
            result.error = "Install requires a package name";
            return result;
        }
        command = "sudo pacman -S --noconfirm " + package;
    } else if (action == "remove") {
        if (package.empty()) {
            result.success = false;
            result.error = "Remove requires a package name";
            return result;
        }
        command = "sudo pacman -R --noconfirm " + package;
    } else if (action == "update" || action == "upgrade") {
        command = "sudo pacman -Syu --noconfirm";
    } else if (action == "search") {
        command = "pacman -Ss " + package;
    } else if (action == "info") {
        command = "pacman -Si " + package;
    } else if (action == "list") {
        command = "pacman -Q";
    } else {
        result.success = false;
        result.error = "Invalid action. Use: install, remove, update, upgrade, search, info, list";
        return result;
    }

    std::string desc = "pacman " + action + (package.empty() ? "" : " " + package);
    if (!requestConfirmation("Pacman", desc + "?")) {
        result.success = false;
        result.error = "Cancelled by user";
        utils::terminal::printError("Cancelled");
        return result;
    }

    utils::terminal::printInfo("Running pacman...");
    result.output = executeCommand(command, result.exit_code);
    result.success = (result.exit_code == 0);

    if (result.success) {
        utils::terminal::printSuccess("Pacman command complete");
    } else {
        utils::terminal::printError("Pacman command failed");
    }

    std::cout << "\n=== Output ===\n" << result.output << "==============\n\n";
    return result;
}

ToolResult ToolExecutor::executeZypper(const ToolCall& tool_call) {
    ToolResult result;

    auto action_it = tool_call.parameters.find("action");
    if (action_it == tool_call.parameters.end()) {
        result.success = false;
        result.error = "Missing 'action' parameter (install, remove, update, search, info, list)";
        return result;
    }

    std::string action = action_it->second;
    std::string package = "";

    auto pkg_it = tool_call.parameters.find("package");
    if (pkg_it != tool_call.parameters.end()) {
        package = pkg_it->second;
    }

    std::vector<std::string> valid_actions = {"install", "remove", "update", "refresh", "search", "info", "list-updates", "dist-upgrade"};
    bool valid = false;
    for (const auto& a : valid_actions) {
        if (action == a) { valid = true; break; }
    }
    if (!valid) {
        result.success = false;
        result.error = "Invalid action. Use: install, remove, update, refresh, search, info, list-updates, dist-upgrade";
        return result;
    }

    if ((action == "install" || action == "remove" || action == "info") && package.empty()) {
        result.success = false;
        result.error = "Action '" + action + "' requires a package name";
        return result;
    }

    utils::terminal::printInfo("[Tool: Zypper]");
    std::cout << utils::terminal::CYAN << "Action: " << action << utils::terminal::RESET << "\n";
    if (!package.empty()) {
        std::cout << utils::terminal::CYAN << "Package: " << package << utils::terminal::RESET << "\n";
    }
    std::cout << "\n";

    std::string desc = "zypper " + action + (package.empty() ? "" : " " + package);
    if (!requestConfirmation("Zypper", desc + "?")) {
        result.success = false;
        result.error = "Cancelled by user";
        utils::terminal::printError("Cancelled");
        return result;
    }

    utils::terminal::printInfo("Running zypper...");
    std::string command = "sudo zypper --non-interactive " + action;
    if (!package.empty()) command += " " + package;

    result.output = executeCommand(command, result.exit_code);
    result.success = (result.exit_code == 0);

    if (result.success) {
        utils::terminal::printSuccess("Zypper command complete");
    } else {
        utils::terminal::printError("Zypper command failed");
    }

    std::cout << "\n=== Output ===\n" << result.output << "==============\n\n";
    return result;
}

// ============================================================================
// File Operation Tools Implementation
// ============================================================================

ToolResult ToolExecutor::executeTar(const ToolCall& tool_call) {
    ToolResult result;

    auto action_it = tool_call.parameters.find("action");
    if (action_it == tool_call.parameters.end()) {
        result.success = false;
        result.error = "Missing 'action' parameter (create, extract, list)";
        return result;
    }

    std::string action = action_it->second;
    std::string archive = "";
    std::string files = "";
    std::string compress = "auto";

    auto archive_it = tool_call.parameters.find("archive");
    if (archive_it != tool_call.parameters.end()) {
        archive = archive_it->second;
    }

    auto files_it = tool_call.parameters.find("files");
    if (files_it != tool_call.parameters.end()) {
        files = files_it->second;
    }

    auto compress_it = tool_call.parameters.find("compress");
    if (compress_it != tool_call.parameters.end()) {
        compress = compress_it->second;
    }

    if (archive.empty()) {
        result.success = false;
        result.error = "Missing 'archive' parameter";
        return result;
    }

    utils::terminal::printInfo("[Tool: Tar]");
    std::cout << utils::terminal::CYAN << "Action: " << action << utils::terminal::RESET << "\n";
    std::cout << utils::terminal::CYAN << "Archive: " << archive << utils::terminal::RESET << "\n";
    if (!files.empty()) {
        std::cout << utils::terminal::CYAN << "Files: " << files << utils::terminal::RESET << "\n";
    }
    std::cout << "\n";

    std::string command;
    std::string flags;

    // Determine compression from extension or parameter
    if (compress == "auto") {
        if (archive.find(".gz") != std::string::npos || archive.find(".tgz") != std::string::npos) compress = "gzip";
        else if (archive.find(".bz2") != std::string::npos) compress = "bzip2";
        else if (archive.find(".xz") != std::string::npos) compress = "xz";
        else compress = "none";
    }

    if (compress == "gzip") flags = "z";
    else if (compress == "bzip2") flags = "j";
    else if (compress == "xz") flags = "J";

    if (action == "create") {
        if (files.empty()) {
            result.success = false;
            result.error = "Create requires 'files' parameter";
            return result;
        }
        command = "tar -cv" + flags + "f " + archive + " " + files;
    } else if (action == "extract") {
        command = "tar -xv" + flags + "f " + archive;
    } else if (action == "list") {
        command = "tar -tv" + flags + "f " + archive;
    } else {
        result.success = false;
        result.error = "Invalid action. Use: create, extract, list";
        return result;
    }

    if (!requestConfirmation("Tar", command + "?")) {
        result.success = false;
        result.error = "Cancelled by user";
        utils::terminal::printError("Cancelled");
        return result;
    }

    utils::terminal::printInfo("Running tar...");
    result.output = executeCommand(command, result.exit_code);
    result.success = (result.exit_code == 0);

    if (result.success) {
        utils::terminal::printSuccess("Tar command complete");
    } else {
        utils::terminal::printError("Tar command failed");
    }

    std::cout << "\n=== Output ===\n" << result.output << "==============\n\n";
    return result;
}

ToolResult ToolExecutor::executeZip(const ToolCall& tool_call) {
    ToolResult result;

    auto archive_it = tool_call.parameters.find("archive");
    auto files_it = tool_call.parameters.find("files");

    if (archive_it == tool_call.parameters.end()) {
        result.success = false;
        result.error = "Missing 'archive' parameter";
        return result;
    }

    std::string archive = archive_it->second;
    std::string files = files_it != tool_call.parameters.end() ? files_it->second : "";
    bool recursive = true;

    auto recursive_it = tool_call.parameters.find("recursive");
    if (recursive_it != tool_call.parameters.end()) {
        recursive = (recursive_it->second != "false" && recursive_it->second != "0");
    }

    utils::terminal::printInfo("[Tool: Zip]");
    std::cout << utils::terminal::CYAN << "Archive: " << archive << utils::terminal::RESET << "\n";
    if (!files.empty()) {
        std::cout << utils::terminal::CYAN << "Files: " << files << utils::terminal::RESET << "\n";
    }
    std::cout << "\n";

    if (files.empty()) {
        result.success = false;
        result.error = "Missing 'files' parameter";
        return result;
    }

    std::string command = "zip";
    if (recursive) command += " -r";
    command += " " + archive + " " + files;

    if (!requestConfirmation("Zip", "Create " + archive + "?")) {
        result.success = false;
        result.error = "Cancelled by user";
        utils::terminal::printError("Cancelled");
        return result;
    }

    utils::terminal::printInfo("Creating zip...");
    result.output = executeCommand(command, result.exit_code);
    result.success = (result.exit_code == 0);

    if (result.success) {
        utils::terminal::printSuccess("Zip complete");
    } else {
        utils::terminal::printError("Zip failed");
    }

    std::cout << "\n=== Output ===\n" << result.output << "==============\n\n";
    return result;
}

ToolResult ToolExecutor::executeUnzip(const ToolCall& tool_call) {
    ToolResult result;

    auto archive_it = tool_call.parameters.find("archive");
    if (archive_it == tool_call.parameters.end()) {
        result.success = false;
        result.error = "Missing 'archive' parameter";
        return result;
    }

    std::string archive = archive_it->second;
    std::string dest = "";
    bool list_only = false;

    auto dest_it = tool_call.parameters.find("destination");
    if (dest_it != tool_call.parameters.end()) {
        dest = dest_it->second;
    }

    auto list_it = tool_call.parameters.find("list");
    if (list_it != tool_call.parameters.end()) {
        list_only = (list_it->second == "true" || list_it->second == "1");
    }

    utils::terminal::printInfo("[Tool: Unzip]");
    std::cout << utils::terminal::CYAN << "Archive: " << archive << utils::terminal::RESET << "\n";
    if (!dest.empty()) {
        std::cout << utils::terminal::CYAN << "Destination: " << dest << utils::terminal::RESET << "\n";
    }
    std::cout << "\n";

    std::string command;
    if (list_only) {
        command = "unzip -l " + archive;
    } else {
        command = "unzip -o " + archive;
        if (!dest.empty()) command += " -d " + dest;
    }

    if (!requestConfirmation("Unzip", list_only ? "List " + archive + "?" : "Extract " + archive + "?")) {
        result.success = false;
        result.error = "Cancelled by user";
        utils::terminal::printError("Cancelled");
        return result;
    }

    utils::terminal::printInfo(list_only ? "Listing..." : "Extracting...");
    result.output = executeCommand(command, result.exit_code);
    result.success = (result.exit_code == 0);

    if (result.success) {
        utils::terminal::printSuccess("Unzip complete");
    } else {
        utils::terminal::printError("Unzip failed");
    }

    std::cout << "\n=== Output ===\n" << result.output << "==============\n\n";
    return result;
}

ToolResult ToolExecutor::executeGzip(const ToolCall& tool_call) {
    ToolResult result;

    auto file_it = tool_call.parameters.find("file");
    if (file_it == tool_call.parameters.end()) {
        result.success = false;
        result.error = "Missing 'file' parameter";
        return result;
    }

    std::string file = file_it->second;
    bool decompress = false;
    bool keep = false;

    auto decompress_it = tool_call.parameters.find("decompress");
    if (decompress_it != tool_call.parameters.end()) {
        decompress = (decompress_it->second == "true" || decompress_it->second == "1");
    }

    auto keep_it = tool_call.parameters.find("keep");
    if (keep_it != tool_call.parameters.end()) {
        keep = (keep_it->second == "true" || keep_it->second == "1");
    }

    utils::terminal::printInfo("[Tool: Gzip]");
    std::cout << utils::terminal::CYAN << "File: " << file << utils::terminal::RESET << "\n";
    std::cout << utils::terminal::CYAN << "Mode: " << (decompress ? "decompress" : "compress") << utils::terminal::RESET << "\n";
    std::cout << "\n";

    std::string command = decompress ? "gunzip" : "gzip";
    if (keep) command += " -k";
    command += " " + file;

    if (!requestConfirmation("Gzip", (decompress ? "Decompress " : "Compress ") + file + "?")) {
        result.success = false;
        result.error = "Cancelled by user";
        utils::terminal::printError("Cancelled");
        return result;
    }

    utils::terminal::printInfo(decompress ? "Decompressing..." : "Compressing...");
    result.output = executeCommand(command, result.exit_code);
    result.success = (result.exit_code == 0);

    if (result.success) {
        utils::terminal::printSuccess("Gzip complete");
    } else {
        utils::terminal::printError("Gzip failed");
    }

    std::cout << "\n=== Output ===\n" << result.output << "==============\n\n";
    return result;
}

ToolResult ToolExecutor::executeRsync(const ToolCall& tool_call) {
    ToolResult result;

    auto source_it = tool_call.parameters.find("source");
    auto dest_it = tool_call.parameters.find("destination");

    if (source_it == tool_call.parameters.end() || dest_it == tool_call.parameters.end()) {
        result.success = false;
        result.error = "Missing 'source' or 'destination' parameter";
        return result;
    }

    std::string source = source_it->second;
    std::string dest = dest_it->second;
    std::string flags = "-avz";
    bool delete_extra = false;

    auto flags_it = tool_call.parameters.find("flags");
    if (flags_it != tool_call.parameters.end()) {
        flags = flags_it->second;
    }

    auto delete_it = tool_call.parameters.find("delete");
    if (delete_it != tool_call.parameters.end()) {
        delete_extra = (delete_it->second == "true" || delete_it->second == "1");
    }

    utils::terminal::printInfo("[Tool: Rsync]");
    std::cout << utils::terminal::CYAN << "Source: " << source << utils::terminal::RESET << "\n";
    std::cout << utils::terminal::CYAN << "Destination: " << dest << utils::terminal::RESET << "\n";
    std::cout << utils::terminal::CYAN << "Flags: " << flags << utils::terminal::RESET << "\n";
    std::cout << "\n";

    std::string command = "rsync " + flags;
    if (delete_extra) command += " --delete";
    command += " " + source + " " + dest;

    if (!requestConfirmation("Rsync", "Sync " + source + " to " + dest + "?")) {
        result.success = false;
        result.error = "Cancelled by user";
        utils::terminal::printError("Cancelled");
        return result;
    }

    utils::terminal::printInfo("Syncing...");
    result.output = executeCommand(command, result.exit_code);
    result.success = (result.exit_code == 0);

    if (result.success) {
        utils::terminal::printSuccess("Rsync complete");
    } else {
        utils::terminal::printError("Rsync failed");
    }

    std::cout << "\n=== Output ===\n" << result.output << "==============\n\n";
    return result;
}

ToolResult ToolExecutor::executeScp(const ToolCall& tool_call) {
    ToolResult result;

    auto source_it = tool_call.parameters.find("source");
    auto dest_it = tool_call.parameters.find("destination");

    if (source_it == tool_call.parameters.end() || dest_it == tool_call.parameters.end()) {
        result.success = false;
        result.error = "Missing 'source' or 'destination' parameter";
        return result;
    }

    std::string source = source_it->second;
    std::string dest = dest_it->second;
    bool recursive = false;
    int port = 22;

    auto recursive_it = tool_call.parameters.find("recursive");
    if (recursive_it != tool_call.parameters.end()) {
        recursive = (recursive_it->second == "true" || recursive_it->second == "1");
    }

    auto port_it = tool_call.parameters.find("port");
    if (port_it != tool_call.parameters.end()) {
        try {
            port = std::stoi(port_it->second);
        } catch (...) {}
    }

    utils::terminal::printInfo("[Tool: Scp]");
    std::cout << utils::terminal::CYAN << "Source: " << source << utils::terminal::RESET << "\n";
    std::cout << utils::terminal::CYAN << "Destination: " << dest << utils::terminal::RESET << "\n";
    std::cout << "\n";

    std::string command = "scp";
    if (recursive) command += " -r";
    if (port != 22) command += " -P " + std::to_string(port);
    command += " " + source + " " + dest;

    if (!requestConfirmation("Scp", "Copy " + source + " to " + dest + "?")) {
        result.success = false;
        result.error = "Cancelled by user";
        utils::terminal::printError("Cancelled");
        return result;
    }

    utils::terminal::printInfo("Copying...");
    result.output = executeCommand(command, result.exit_code);
    result.success = (result.exit_code == 0);

    if (result.success) {
        utils::terminal::printSuccess("Scp complete");
    } else {
        utils::terminal::printError("Scp failed");
    }

    std::cout << "\n=== Output ===\n" << result.output << "==============\n\n";
    return result;
}

ToolResult ToolExecutor::executeCp(const ToolCall& tool_call) {
    ToolResult result;

    auto source_it = tool_call.parameters.find("source");
    auto dest_it = tool_call.parameters.find("destination");

    if (source_it == tool_call.parameters.end() || dest_it == tool_call.parameters.end()) {
        result.success = false;
        result.error = "Missing 'source' or 'destination' parameter";
        return result;
    }

    std::string source = source_it->second;
    std::string dest = dest_it->second;
    bool recursive = false;

    auto recursive_it = tool_call.parameters.find("recursive");
    if (recursive_it != tool_call.parameters.end()) {
        recursive = (recursive_it->second == "true" || recursive_it->second == "1");
    }

    utils::terminal::printInfo("[Tool: Cp]");
    std::cout << utils::terminal::CYAN << "Source: " << source << utils::terminal::RESET << "\n";
    std::cout << utils::terminal::CYAN << "Destination: " << dest << utils::terminal::RESET << "\n";
    std::cout << "\n";

    std::string command = "cp -v";
    if (recursive) command += " -r";
    command += " " + source + " " + dest;

    if (!requestConfirmation("Cp", "Copy " + source + " to " + dest + "?")) {
        result.success = false;
        result.error = "Cancelled by user";
        utils::terminal::printError("Cancelled");
        return result;
    }

    utils::terminal::printInfo("Copying...");
    result.output = executeCommand(command, result.exit_code);
    result.success = (result.exit_code == 0);

    if (result.success) {
        utils::terminal::printSuccess("Copy complete");
    } else {
        utils::terminal::printError("Copy failed");
    }

    std::cout << "\n=== Output ===\n" << result.output << "==============\n\n";
    return result;
}

ToolResult ToolExecutor::executeMv(const ToolCall& tool_call) {
    ToolResult result;

    auto source_it = tool_call.parameters.find("source");
    auto dest_it = tool_call.parameters.find("destination");

    if (source_it == tool_call.parameters.end() || dest_it == tool_call.parameters.end()) {
        result.success = false;
        result.error = "Missing 'source' or 'destination' parameter";
        return result;
    }

    std::string source = source_it->second;
    std::string dest = dest_it->second;

    utils::terminal::printInfo("[Tool: Mv]");
    std::cout << utils::terminal::CYAN << "Source: " << source << utils::terminal::RESET << "\n";
    std::cout << utils::terminal::CYAN << "Destination: " << dest << utils::terminal::RESET << "\n";
    std::cout << "\n";

    std::string command = "mv -v " + source + " " + dest;

    if (!requestConfirmation("Mv", "Move " + source + " to " + dest + "?")) {
        result.success = false;
        result.error = "Cancelled by user";
        utils::terminal::printError("Cancelled");
        return result;
    }

    utils::terminal::printInfo("Moving...");
    result.output = executeCommand(command, result.exit_code);
    result.success = (result.exit_code == 0);

    if (result.success) {
        utils::terminal::printSuccess("Move complete");
    } else {
        utils::terminal::printError("Move failed");
    }

    std::cout << "\n=== Output ===\n" << result.output << "==============\n\n";
    return result;
}

ToolResult ToolExecutor::executeRm(const ToolCall& tool_call) {
    ToolResult result;

    auto path_it = tool_call.parameters.find("path");
    if (path_it == tool_call.parameters.end()) {
        result.success = false;
        result.error = "Missing 'path' parameter";
        return result;
    }

    std::string path = path_it->second;
    bool recursive = false;
    bool force = false;

    auto recursive_it = tool_call.parameters.find("recursive");
    if (recursive_it != tool_call.parameters.end()) {
        recursive = (recursive_it->second == "true" || recursive_it->second == "1");
    }

    auto force_it = tool_call.parameters.find("force");
    if (force_it != tool_call.parameters.end()) {
        force = (force_it->second == "true" || force_it->second == "1");
    }

    utils::terminal::printInfo("[Tool: Rm]");
    std::cout << utils::terminal::YELLOW << "WARNING: This will delete files!" << utils::terminal::RESET << "\n";
    std::cout << utils::terminal::CYAN << "Path: " << path << utils::terminal::RESET << "\n";
    std::cout << utils::terminal::CYAN << "Recursive: " << (recursive ? "yes" : "no") << utils::terminal::RESET << "\n";
    std::cout << "\n";

    std::string command = "rm -v";
    if (recursive) command += " -r";
    if (force) command += " -f";
    command += " " + path;

    if (!requestConfirmation("Rm", "DELETE " + path + "?")) {
        result.success = false;
        result.error = "Cancelled by user";
        utils::terminal::printError("Cancelled");
        return result;
    }

    utils::terminal::printInfo("Deleting...");
    result.output = executeCommand(command, result.exit_code);
    result.success = (result.exit_code == 0);

    if (result.success) {
        utils::terminal::printSuccess("Delete complete");
    } else {
        utils::terminal::printError("Delete failed");
    }

    std::cout << "\n=== Output ===\n" << result.output << "==============\n\n";
    return result;
}

ToolResult ToolExecutor::executeMkdir(const ToolCall& tool_call) {
    ToolResult result;

    auto path_it = tool_call.parameters.find("path");
    if (path_it == tool_call.parameters.end()) {
        result.success = false;
        result.error = "Missing 'path' parameter";
        return result;
    }

    std::string path = path_it->second;
    bool parents = true;

    auto parents_it = tool_call.parameters.find("parents");
    if (parents_it != tool_call.parameters.end()) {
        parents = (parents_it->second != "false" && parents_it->second != "0");
    }

    utils::terminal::printInfo("[Tool: Mkdir]");
    std::cout << utils::terminal::CYAN << "Path: " << path << utils::terminal::RESET << "\n";
    std::cout << "\n";

    std::string command = "mkdir -v";
    if (parents) command += " -p";
    command += " " + path;

    if (!requestConfirmation("Mkdir", "Create directory " + path + "?")) {
        result.success = false;
        result.error = "Cancelled by user";
        utils::terminal::printError("Cancelled");
        return result;
    }

    utils::terminal::printInfo("Creating directory...");
    result.output = executeCommand(command, result.exit_code);
    result.success = (result.exit_code == 0);

    if (result.success) {
        utils::terminal::printSuccess("Directory created");
    } else {
        utils::terminal::printError("Failed to create directory");
    }

    std::cout << "\n=== Output ===\n" << result.output << "==============\n\n";
    return result;
}

ToolResult ToolExecutor::executeChmod(const ToolCall& tool_call) {
    ToolResult result;

    auto path_it = tool_call.parameters.find("path");
    auto mode_it = tool_call.parameters.find("mode");

    if (path_it == tool_call.parameters.end() || mode_it == tool_call.parameters.end()) {
        result.success = false;
        result.error = "Missing 'path' or 'mode' parameter";
        return result;
    }

    std::string path = path_it->second;
    std::string mode = mode_it->second;
    bool recursive = false;

    auto recursive_it = tool_call.parameters.find("recursive");
    if (recursive_it != tool_call.parameters.end()) {
        recursive = (recursive_it->second == "true" || recursive_it->second == "1");
    }

    utils::terminal::printInfo("[Tool: Chmod]");
    std::cout << utils::terminal::CYAN << "Path: " << path << utils::terminal::RESET << "\n";
    std::cout << utils::terminal::CYAN << "Mode: " << mode << utils::terminal::RESET << "\n";
    std::cout << "\n";

    std::string command = "chmod -v";
    if (recursive) command += " -R";
    command += " " + mode + " " + path;

    if (!requestConfirmation("Chmod", "Change permissions of " + path + " to " + mode + "?")) {
        result.success = false;
        result.error = "Cancelled by user";
        utils::terminal::printError("Cancelled");
        return result;
    }

    utils::terminal::printInfo("Changing permissions...");
    result.output = executeCommand(command, result.exit_code);
    result.success = (result.exit_code == 0);

    if (result.success) {
        utils::terminal::printSuccess("Permissions changed");
    } else {
        utils::terminal::printError("Failed to change permissions");
    }

    std::cout << "\n=== Output ===\n" << result.output << "==============\n\n";
    return result;
}

ToolResult ToolExecutor::executeChown(const ToolCall& tool_call) {
    ToolResult result;

    auto path_it = tool_call.parameters.find("path");
    auto owner_it = tool_call.parameters.find("owner");

    if (path_it == tool_call.parameters.end() || owner_it == tool_call.parameters.end()) {
        result.success = false;
        result.error = "Missing 'path' or 'owner' parameter";
        return result;
    }

    std::string path = path_it->second;
    std::string owner = owner_it->second;
    bool recursive = false;

    auto recursive_it = tool_call.parameters.find("recursive");
    if (recursive_it != tool_call.parameters.end()) {
        recursive = (recursive_it->second == "true" || recursive_it->second == "1");
    }

    utils::terminal::printInfo("[Tool: Chown]");
    std::cout << utils::terminal::CYAN << "Path: " << path << utils::terminal::RESET << "\n";
    std::cout << utils::terminal::CYAN << "Owner: " << owner << utils::terminal::RESET << "\n";
    std::cout << "\n";

    std::string command = "sudo chown -v";
    if (recursive) command += " -R";
    command += " " + owner + " " + path;

    if (!requestConfirmation("Chown", "Change owner of " + path + " to " + owner + "?")) {
        result.success = false;
        result.error = "Cancelled by user";
        utils::terminal::printError("Cancelled");
        return result;
    }

    utils::terminal::printInfo("Changing ownership...");
    result.output = executeCommand(command, result.exit_code);
    result.success = (result.exit_code == 0);

    if (result.success) {
        utils::terminal::printSuccess("Ownership changed");
    } else {
        utils::terminal::printError("Failed to change ownership");
    }

    std::cout << "\n=== Output ===\n" << result.output << "==============\n\n";
    return result;
}

ToolResult ToolExecutor::executeDf(const ToolCall& tool_call) {
    ToolResult result;

    std::string path = "";
    bool human = true;

    auto path_it = tool_call.parameters.find("path");
    if (path_it != tool_call.parameters.end()) {
        path = path_it->second;
    }

    auto human_it = tool_call.parameters.find("human");
    if (human_it != tool_call.parameters.end()) {
        human = (human_it->second != "false" && human_it->second != "0");
    }

    utils::terminal::printInfo("[Tool: Df]");
    if (!path.empty()) {
        std::cout << utils::terminal::CYAN << "Path: " << path << utils::terminal::RESET << "\n";
    }
    std::cout << "\n";

    std::string command = "df";
    if (human) command += " -h";
    if (!path.empty()) command += " " + path;

    if (!requestConfirmation("Df", "Show disk space?")) {
        result.success = false;
        result.error = "Cancelled by user";
        utils::terminal::printError("Cancelled");
        return result;
    }

    utils::terminal::printInfo("Getting disk space...");
    result.output = executeCommand(command, result.exit_code);
    result.success = (result.exit_code == 0);

    if (result.success) {
        utils::terminal::printSuccess("Df complete");
    } else {
        utils::terminal::printError("Df failed");
    }

    std::cout << "\n=== Output ===\n" << result.output << "==============\n\n";
    return result;
}

ToolResult ToolExecutor::executeDu(const ToolCall& tool_call) {
    ToolResult result;

    std::string path = ".";
    bool human = true;
    bool summary = false;
    int max_depth = -1;

    auto path_it = tool_call.parameters.find("path");
    if (path_it != tool_call.parameters.end()) {
        path = path_it->second;
    }

    auto human_it = tool_call.parameters.find("human");
    if (human_it != tool_call.parameters.end()) {
        human = (human_it->second != "false" && human_it->second != "0");
    }

    auto summary_it = tool_call.parameters.find("summary");
    if (summary_it != tool_call.parameters.end()) {
        summary = (summary_it->second == "true" || summary_it->second == "1");
    }

    auto depth_it = tool_call.parameters.find("max_depth");
    if (depth_it != tool_call.parameters.end()) {
        try {
            max_depth = std::stoi(depth_it->second);
        } catch (...) {}
    }

    utils::terminal::printInfo("[Tool: Du]");
    std::cout << utils::terminal::CYAN << "Path: " << path << utils::terminal::RESET << "\n";
    std::cout << "\n";

    std::string command = "du";
    if (human) command += " -h";
    if (summary) command += " -s";
    else if (max_depth >= 0) command += " -d " + std::to_string(max_depth);
    command += " " + path;

    if (!requestConfirmation("Du", "Show disk usage for " + path + "?")) {
        result.success = false;
        result.error = "Cancelled by user";
        utils::terminal::printError("Cancelled");
        return result;
    }

    utils::terminal::printInfo("Calculating disk usage...");
    result.output = executeCommand(command, result.exit_code);
    result.success = (result.exit_code == 0);

    if (result.success) {
        utils::terminal::printSuccess("Du complete");
    } else {
        utils::terminal::printError("Du failed");
    }

    std::cout << "\n=== Output ===\n" << result.output << "==============\n\n";
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
    // Network tools
    else if (tool_call.name == "Ping") {
        return executePing(tool_call);
    } else if (tool_call.name == "Traceroute") {
        return executeTraceroute(tool_call);
    } else if (tool_call.name == "Nmap") {
        return executeNmap(tool_call);
    } else if (tool_call.name == "Dig") {
        return executeDig(tool_call);
    } else if (tool_call.name == "Whois") {
        return executeWhois(tool_call);
    } else if (tool_call.name == "Netstat") {
        return executeNetstat(tool_call);
    } else if (tool_call.name == "Curl") {
        return executeCurl(tool_call);
    } else if (tool_call.name == "SSH") {
        return executeSSH(tool_call);
    } else if (tool_call.name == "Telnet") {
        return executeTelnet(tool_call);
    } else if (tool_call.name == "Netcat") {
        return executeNetcat(tool_call);
    } else if (tool_call.name == "Ifconfig") {
        return executeIfconfig(tool_call);
    } else if (tool_call.name == "ARP") {
        return executeArp(tool_call);
    }
    // Package manager tools
    else if (tool_call.name == "Brew") {
        return executeBrew(tool_call);
    } else if (tool_call.name == "Pip") {
        return executePip(tool_call);
    } else if (tool_call.name == "Npm") {
        return executeNpm(tool_call);
    } else if (tool_call.name == "Apt") {
        return executeApt(tool_call);
    } else if (tool_call.name == "Dnf") {
        return executeDnf(tool_call);
    } else if (tool_call.name == "Yum") {
        return executeYum(tool_call);
    } else if (tool_call.name == "Pacman") {
        return executePacman(tool_call);
    } else if (tool_call.name == "Zypper") {
        return executeZypper(tool_call);
    }
    // File operation tools
    else if (tool_call.name == "Tar") {
        return executeTar(tool_call);
    } else if (tool_call.name == "Zip") {
        return executeZip(tool_call);
    } else if (tool_call.name == "Unzip") {
        return executeUnzip(tool_call);
    } else if (tool_call.name == "Gzip") {
        return executeGzip(tool_call);
    } else if (tool_call.name == "Rsync") {
        return executeRsync(tool_call);
    } else if (tool_call.name == "Scp") {
        return executeScp(tool_call);
    } else if (tool_call.name == "Cp") {
        return executeCp(tool_call);
    } else if (tool_call.name == "Mv") {
        return executeMv(tool_call);
    } else if (tool_call.name == "Rm") {
        return executeRm(tool_call);
    } else if (tool_call.name == "Mkdir") {
        return executeMkdir(tool_call);
    } else if (tool_call.name == "Chmod") {
        return executeChmod(tool_call);
    } else if (tool_call.name == "Chown") {
        return executeChown(tool_call);
    } else if (tool_call.name == "Df") {
        return executeDf(tool_call);
    } else if (tool_call.name == "Du") {
        return executeDu(tool_call);
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

} // namespace casper
