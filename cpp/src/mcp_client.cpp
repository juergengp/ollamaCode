#include "mcp_client.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>
#include <cstring>

namespace oleg {

// ============================================================================
// MCPServerConnection Implementation
// ============================================================================

MCPServerConnection::MCPServerConnection(const MCPServerConfig& config)
    : config_(config)
    , connected_(false)
    , stdin_fd_(-1)
    , stdout_fd_(-1)
    , server_pid_(-1)
    , request_id_(0)
{
}

MCPServerConnection::~MCPServerConnection() {
    disconnect();
}

bool MCPServerConnection::connect() {
    if (connected_) {
        return true;
    }

    if (config_.transport == "http") {
        // HTTP transport - just mark as connected, actual connection happens per-request
        connected_ = true;
        return true;
    }

    // Stdio transport - spawn the server process
    int stdin_pipe[2];
    int stdout_pipe[2];

    if (pipe(stdin_pipe) == -1 || pipe(stdout_pipe) == -1) {
        std::cerr << "Failed to create pipes: " << strerror(errno) << std::endl;
        return false;
    }

    pid_t pid = fork();

    if (pid == -1) {
        std::cerr << "Failed to fork: " << strerror(errno) << std::endl;
        close(stdin_pipe[0]);
        close(stdin_pipe[1]);
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        return false;
    }

    if (pid == 0) {
        // Child process
        close(stdin_pipe[1]);  // Close write end of stdin pipe
        close(stdout_pipe[0]); // Close read end of stdout pipe

        dup2(stdin_pipe[0], STDIN_FILENO);
        dup2(stdout_pipe[1], STDOUT_FILENO);

        close(stdin_pipe[0]);
        close(stdout_pipe[1]);

        // Set environment variables
        for (const auto& [key, value] : config_.env) {
            setenv(key.c_str(), value.c_str(), 1);
        }

        // Build argv
        std::vector<char*> argv;
        argv.push_back(const_cast<char*>(config_.command.c_str()));
        for (const auto& arg : config_.args) {
            argv.push_back(const_cast<char*>(arg.c_str()));
        }
        argv.push_back(nullptr);

        execvp(config_.command.c_str(), argv.data());

        // If we get here, exec failed
        std::cerr << "Failed to exec " << config_.command << ": " << strerror(errno) << std::endl;
        _exit(1);
    }

    // Parent process
    close(stdin_pipe[0]);  // Close read end of stdin pipe
    close(stdout_pipe[1]); // Close write end of stdout pipe

    stdin_fd_ = stdin_pipe[1];
    stdout_fd_ = stdout_pipe[0];
    server_pid_ = pid;

    // Set stdout to non-blocking for reading
    int flags = fcntl(stdout_fd_, F_GETFL, 0);
    fcntl(stdout_fd_, F_SETFL, flags | O_NONBLOCK);

    connected_ = true;

    // Send initialize request
    try {
        json result = initialize();
        if (result.contains("error")) {
            std::cerr << "MCP initialization failed: " << result["error"].dump() << std::endl;
            disconnect();
            return false;
        }
    } catch (const std::exception& e) {
        std::cerr << "MCP initialization exception: " << e.what() << std::endl;
        disconnect();
        return false;
    }

    return true;
}

void MCPServerConnection::disconnect() {
    if (!connected_) {
        return;
    }

    if (stdin_fd_ >= 0) {
        close(stdin_fd_);
        stdin_fd_ = -1;
    }

    if (stdout_fd_ >= 0) {
        close(stdout_fd_);
        stdout_fd_ = -1;
    }

    if (server_pid_ > 0) {
        kill(server_pid_, SIGTERM);
        int status;
        waitpid(server_pid_, &status, WNOHANG);
        server_pid_ = -1;
    }

    connected_ = false;
}

bool MCPServerConnection::isConnected() const {
    return connected_;
}

void MCPServerConnection::writeMessage(const json& message) {
    std::string content = message.dump();
    std::string header = "Content-Length: " + std::to_string(content.size()) + "\r\n\r\n";
    std::string full_message = header + content;

    ssize_t written = write(stdin_fd_, full_message.c_str(), full_message.size());
    if (written < 0) {
        throw std::runtime_error("Failed to write to MCP server: " + std::string(strerror(errno)));
    }
}

json MCPServerConnection::readResponse() {
    // Read headers first
    std::string buffer;
    char c;
    int content_length = -1;

    // Set to blocking mode for reading
    int flags = fcntl(stdout_fd_, F_GETFL, 0);
    fcntl(stdout_fd_, F_SETFL, flags & ~O_NONBLOCK);

    // Read until we find the header delimiter
    while (true) {
        ssize_t n = read(stdout_fd_, &c, 1);
        if (n <= 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                usleep(10000); // 10ms
                continue;
            }
            throw std::runtime_error("Failed to read from MCP server");
        }
        buffer += c;

        // Check for header end
        if (buffer.size() >= 4 && buffer.substr(buffer.size() - 4) == "\r\n\r\n") {
            // Parse Content-Length
            size_t pos = buffer.find("Content-Length:");
            if (pos != std::string::npos) {
                size_t end = buffer.find("\r\n", pos);
                std::string length_str = buffer.substr(pos + 15, end - pos - 15);
                content_length = std::stoi(length_str);
            }
            break;
        }
    }

    if (content_length < 0) {
        throw std::runtime_error("Invalid MCP response: no Content-Length header");
    }

    // Read the content
    std::string content(content_length, '\0');
    size_t total_read = 0;
    while (total_read < static_cast<size_t>(content_length)) {
        ssize_t n = read(stdout_fd_, &content[total_read], content_length - total_read);
        if (n <= 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                usleep(10000);
                continue;
            }
            throw std::runtime_error("Failed to read content from MCP server");
        }
        total_read += n;
    }

    // Set back to non-blocking
    fcntl(stdout_fd_, F_SETFL, flags | O_NONBLOCK);

    return json::parse(content);
}

json MCPServerConnection::sendRequest(const std::string& method, const json& params) {
    json request = {
        {"jsonrpc", "2.0"},
        {"id", ++request_id_},
        {"method", method}
    };

    if (!params.empty()) {
        request["params"] = params;
    }

    writeMessage(request);
    json response = readResponse();

    if (response.contains("error")) {
        return response;
    }

    return response.value("result", json::object());
}

json MCPServerConnection::initialize() {
    json params = {
        {"protocolVersion", "2024-11-05"},
        {"capabilities", {
            {"roots", {{"listChanged", true}}},
            {"sampling", json::object()}
        }},
        {"clientInfo", {
            {"name", "OlEg"},
            {"version", "2.0.1"}
        }}
    };

    json result = sendRequest("initialize", params);

    // Send initialized notification
    json notification = {
        {"jsonrpc", "2.0"},
        {"method", "notifications/initialized"}
    };
    writeMessage(notification);

    return result;
}

std::vector<MCPTool> MCPServerConnection::listTools() {
    std::vector<MCPTool> tools;

    try {
        json result = sendRequest("tools/list");

        if (result.contains("tools") && result["tools"].is_array()) {
            for (const auto& tool : result["tools"]) {
                MCPTool t;
                t.name = tool.value("name", "");
                t.description = tool.value("description", "");
                t.inputSchema = tool.value("inputSchema", json::object());
                t.serverName = config_.name;
                tools.push_back(t);
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Error listing tools from " << config_.name << ": " << e.what() << std::endl;
    }

    return tools;
}

std::vector<MCPResource> MCPServerConnection::listResources() {
    std::vector<MCPResource> resources;

    try {
        json result = sendRequest("resources/list");

        if (result.contains("resources") && result["resources"].is_array()) {
            for (const auto& resource : result["resources"]) {
                MCPResource r;
                r.uri = resource.value("uri", "");
                r.name = resource.value("name", "");
                r.description = resource.value("description", "");
                r.mimeType = resource.value("mimeType", "");
                r.serverName = config_.name;
                resources.push_back(r);
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Error listing resources from " << config_.name << ": " << e.what() << std::endl;
    }

    return resources;
}

std::vector<MCPPrompt> MCPServerConnection::listPrompts() {
    std::vector<MCPPrompt> prompts;

    try {
        json result = sendRequest("prompts/list");

        if (result.contains("prompts") && result["prompts"].is_array()) {
            for (const auto& prompt : result["prompts"]) {
                MCPPrompt p;
                p.name = prompt.value("name", "");
                p.description = prompt.value("description", "");
                if (prompt.contains("arguments") && prompt["arguments"].is_array()) {
                    p.arguments = prompt["arguments"].get<std::vector<json>>();
                }
                p.serverName = config_.name;
                prompts.push_back(p);
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Error listing prompts from " << config_.name << ": " << e.what() << std::endl;
    }

    return prompts;
}

MCPToolResult MCPServerConnection::callTool(const std::string& name, const json& arguments) {
    MCPToolResult result;
    result.success = false;
    result.isError = false;

    try {
        json params = {
            {"name", name},
            {"arguments", arguments}
        };

        json response = sendRequest("tools/call", params);

        if (response.contains("error")) {
            result.error = response["error"].dump();
            result.isError = true;
            return result;
        }

        if (response.contains("content") && response["content"].is_array()) {
            std::ostringstream oss;
            for (const auto& item : response["content"]) {
                if (item.value("type", "") == "text") {
                    oss << item.value("text", "");
                }
            }
            result.content = oss.str();
        }

        result.isError = response.value("isError", false);
        result.success = !result.isError;

    } catch (const std::exception& e) {
        result.error = e.what();
        result.isError = true;
    }

    return result;
}

std::string MCPServerConnection::readResource(const std::string& uri) {
    try {
        json params = {{"uri", uri}};
        json response = sendRequest("resources/read", params);

        if (response.contains("contents") && response["contents"].is_array()) {
            std::ostringstream oss;
            for (const auto& item : response["contents"]) {
                if (item.contains("text")) {
                    oss << item["text"].get<std::string>();
                }
            }
            return oss.str();
        }
    } catch (const std::exception& e) {
        return "Error reading resource: " + std::string(e.what());
    }

    return "";
}

std::string MCPServerConnection::getPrompt(const std::string& name, const json& arguments) {
    try {
        json params = {
            {"name", name},
            {"arguments", arguments}
        };

        json response = sendRequest("prompts/get", params);

        if (response.contains("messages") && response["messages"].is_array()) {
            std::ostringstream oss;
            for (const auto& msg : response["messages"]) {
                if (msg.contains("content")) {
                    if (msg["content"].is_string()) {
                        oss << msg["content"].get<std::string>() << "\n";
                    } else if (msg["content"].is_object() && msg["content"].contains("text")) {
                        oss << msg["content"]["text"].get<std::string>() << "\n";
                    }
                }
            }
            return oss.str();
        }
    } catch (const std::exception& e) {
        return "Error getting prompt: " + std::string(e.what());
    }

    return "";
}

// ============================================================================
// MCPClient Implementation
// ============================================================================

MCPClient::MCPClient() {
}

MCPClient::~MCPClient() {
    disconnectAll();
}

bool MCPClient::addServer(const MCPServerConfig& config) {
    if (server_configs_.count(config.name) > 0) {
        return false;  // Server already exists
    }

    server_configs_[config.name] = config;
    return true;
}

bool MCPClient::removeServer(const std::string& name) {
    disconnectServer(name);
    server_configs_.erase(name);
    return true;
}

bool MCPClient::enableServer(const std::string& name) {
    if (server_configs_.count(name) == 0) {
        return false;
    }
    server_configs_[name].enabled = true;
    return true;
}

bool MCPClient::disableServer(const std::string& name) {
    if (server_configs_.count(name) == 0) {
        return false;
    }
    server_configs_[name].enabled = false;
    disconnectServer(name);
    return true;
}

bool MCPClient::connectAll() {
    bool all_success = true;

    for (const auto& [name, config] : server_configs_) {
        if (config.enabled) {
            if (!connectServer(name)) {
                all_success = false;
            }
        }
    }

    return all_success;
}

bool MCPClient::connectServer(const std::string& name) {
    if (server_configs_.count(name) == 0) {
        notifyStatus(name, "error: server not configured");
        return false;
    }

    if (connections_.count(name) > 0 && connections_[name]->isConnected()) {
        return true;  // Already connected
    }

    notifyStatus(name, "connecting...");

    auto connection = std::make_unique<MCPServerConnection>(server_configs_[name]);
    if (!connection->connect()) {
        notifyStatus(name, "failed to connect");
        return false;
    }

    connections_[name] = std::move(connection);

    // Cache the tools
    tool_cache_[name] = connections_[name]->listTools();

    notifyStatus(name, "connected (" + std::to_string(tool_cache_[name].size()) + " tools)");
    return true;
}

void MCPClient::disconnectAll() {
    for (auto& [name, connection] : connections_) {
        connection->disconnect();
        notifyStatus(name, "disconnected");
    }
    connections_.clear();
    tool_cache_.clear();
}

void MCPClient::disconnectServer(const std::string& name) {
    if (connections_.count(name) > 0) {
        connections_[name]->disconnect();
        connections_.erase(name);
        tool_cache_.erase(name);
        notifyStatus(name, "disconnected");
    }
}

std::vector<MCPTool> MCPClient::getAllTools() {
    std::vector<MCPTool> all_tools;

    for (const auto& [name, tools] : tool_cache_) {
        all_tools.insert(all_tools.end(), tools.begin(), tools.end());
    }

    return all_tools;
}

std::vector<MCPResource> MCPClient::getAllResources() {
    std::vector<MCPResource> all_resources;

    for (const auto& [name, connection] : connections_) {
        auto resources = connection->listResources();
        all_resources.insert(all_resources.end(), resources.begin(), resources.end());
    }

    return all_resources;
}

std::vector<MCPPrompt> MCPClient::getAllPrompts() {
    std::vector<MCPPrompt> all_prompts;

    for (const auto& [name, connection] : connections_) {
        auto prompts = connection->listPrompts();
        all_prompts.insert(all_prompts.end(), prompts.begin(), prompts.end());
    }

    return all_prompts;
}

MCPToolResult MCPClient::callTool(const std::string& toolName, const json& arguments) {
    // Find which server provides this tool
    for (const auto& [serverName, tools] : tool_cache_) {
        for (const auto& tool : tools) {
            if (tool.name == toolName) {
                if (connections_.count(serverName) > 0) {
                    return connections_[serverName]->callTool(toolName, arguments);
                }
            }
        }
    }

    MCPToolResult result;
    result.success = false;
    result.isError = true;
    result.error = "Tool not found: " + toolName;
    return result;
}

std::string MCPClient::readResource(const std::string& uri) {
    // Try all connected servers
    for (const auto& [name, connection] : connections_) {
        std::string result = connection->readResource(uri);
        if (!result.empty() && result.find("Error") != 0) {
            return result;
        }
    }
    return "Resource not found: " + uri;
}

std::string MCPClient::getPrompt(const std::string& name, const json& arguments) {
    // Find which server provides this prompt
    for (const auto& [serverName, connection] : connections_) {
        auto prompts = connection->listPrompts();
        for (const auto& prompt : prompts) {
            if (prompt.name == name) {
                return connection->getPrompt(name, arguments);
            }
        }
    }
    return "Prompt not found: " + name;
}

std::vector<MCPServerConfig> MCPClient::getServerConfigs() const {
    std::vector<MCPServerConfig> configs;
    for (const auto& [name, config] : server_configs_) {
        configs.push_back(config);
    }
    return configs;
}

std::vector<std::string> MCPClient::getConnectedServers() const {
    std::vector<std::string> names;
    for (const auto& [name, connection] : connections_) {
        if (connection->isConnected()) {
            names.push_back(name);
        }
    }
    return names;
}

bool MCPClient::isServerConnected(const std::string& name) const {
    return connections_.count(name) > 0 && connections_.at(name)->isConnected();
}

json MCPClient::generateToolDefinitions() {
    json tools = json::array();

    for (const auto& tool : getAllTools()) {
        json tool_def = {
            {"type", "function"},
            {"function", {
                {"name", tool.serverName + "__" + tool.name},
                {"description", "[MCP:" + tool.serverName + "] " + tool.description},
                {"parameters", tool.inputSchema}
            }}
        };
        tools.push_back(tool_def);
    }

    return tools;
}

bool MCPClient::loadConfig(const std::string& configPath) {
    std::ifstream file(configPath);
    if (!file.is_open()) {
        return false;
    }

    try {
        json config = json::parse(file);

        if (config.contains("mcpServers") && config["mcpServers"].is_object()) {
            for (const auto& [name, server] : config["mcpServers"].items()) {
                MCPServerConfig sc;
                sc.name = name;
                sc.command = server.value("command", "");
                sc.enabled = server.value("enabled", true);
                sc.transport = server.value("transport", "stdio");
                sc.url = server.value("url", "");

                if (server.contains("args") && server["args"].is_array()) {
                    sc.args = server["args"].get<std::vector<std::string>>();
                }

                if (server.contains("env") && server["env"].is_object()) {
                    sc.env = server["env"].get<std::map<std::string, std::string>>();
                }

                addServer(sc);
            }
        }

        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error loading MCP config: " << e.what() << std::endl;
        return false;
    }
}

bool MCPClient::saveConfig(const std::string& configPath) {
    json config;
    config["mcpServers"] = json::object();

    for (const auto& [name, sc] : server_configs_) {
        json server;
        server["command"] = sc.command;
        server["args"] = sc.args;
        server["env"] = sc.env;
        server["enabled"] = sc.enabled;
        server["transport"] = sc.transport;
        if (!sc.url.empty()) {
            server["url"] = sc.url;
        }
        config["mcpServers"][name] = server;
    }

    std::ofstream file(configPath);
    if (!file.is_open()) {
        return false;
    }

    file << config.dump(2);
    return true;
}

void MCPClient::notifyStatus(const std::string& server, const std::string& status) {
    if (status_callback_) {
        status_callback_(server, status);
    }
}

} // namespace oleg
