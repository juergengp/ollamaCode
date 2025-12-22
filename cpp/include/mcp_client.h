#ifndef OLEG_MCP_CLIENT_H
#define OLEG_MCP_CLIENT_H

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <functional>
#include "json.hpp"
#include "config.h"  // For MCPServerConfig

using json = nlohmann::json;

namespace oleg {

// MCP Tool definition
struct MCPTool {
    std::string name;
    std::string description;
    json inputSchema;  // JSON Schema for parameters
    std::string serverName;  // Which server provides this tool
};

// MCP Resource definition
struct MCPResource {
    std::string uri;
    std::string name;
    std::string description;
    std::string mimeType;
    std::string serverName;
};

// MCP Prompt definition
struct MCPPrompt {
    std::string name;
    std::string description;
    std::vector<json> arguments;
    std::string serverName;
};

// MCPServerConfig is defined in config.h

// Result from MCP tool call
struct MCPToolResult {
    bool success;
    std::string content;
    std::string error;
    bool isError;
};

// MCP Server connection
class MCPServerConnection {
public:
    MCPServerConnection(const MCPServerConfig& config);
    ~MCPServerConnection();

    // Connection management
    bool connect();
    void disconnect();
    bool isConnected() const;

    // MCP Protocol methods
    json initialize();
    std::vector<MCPTool> listTools();
    std::vector<MCPResource> listResources();
    std::vector<MCPPrompt> listPrompts();

    MCPToolResult callTool(const std::string& name, const json& arguments);
    std::string readResource(const std::string& uri);
    std::string getPrompt(const std::string& name, const json& arguments);

    // Server info
    std::string getName() const { return config_.name; }
    const MCPServerConfig& getConfig() const { return config_; }

private:
    MCPServerConfig config_;
    bool connected_;
    int stdin_fd_;   // Pipe to server stdin
    int stdout_fd_;  // Pipe from server stdout
    pid_t server_pid_;
    int request_id_;

    // JSON-RPC helpers
    json sendRequest(const std::string& method, const json& params = json::object());
    json readResponse();
    void writeMessage(const json& message);
    json parseMessage(const std::string& data);
};

// Main MCP Client - manages multiple server connections
class MCPClient {
public:
    MCPClient();
    ~MCPClient();

    // Server management
    bool addServer(const MCPServerConfig& config);
    bool removeServer(const std::string& name);
    bool enableServer(const std::string& name);
    bool disableServer(const std::string& name);

    // Connect/disconnect
    bool connectAll();
    bool connectServer(const std::string& name);
    void disconnectAll();
    void disconnectServer(const std::string& name);

    // Get all available tools from all connected servers
    std::vector<MCPTool> getAllTools();
    std::vector<MCPResource> getAllResources();
    std::vector<MCPPrompt> getAllPrompts();

    // Call a tool (finds the right server automatically)
    MCPToolResult callTool(const std::string& toolName, const json& arguments);

    // Read a resource
    std::string readResource(const std::string& uri);

    // Get prompt
    std::string getPrompt(const std::string& name, const json& arguments);

    // Server info
    std::vector<MCPServerConfig> getServerConfigs() const;
    std::vector<std::string> getConnectedServers() const;
    bool isServerConnected(const std::string& name) const;

    // Generate tool definitions for Ollama's tool calling format
    json generateToolDefinitions();

    // Load/save server configurations
    bool loadConfig(const std::string& configPath);
    bool saveConfig(const std::string& configPath);

    // Status callback for connection events
    using StatusCallback = std::function<void(const std::string& server, const std::string& status)>;
    void setStatusCallback(StatusCallback callback) { status_callback_ = callback; }

private:
    std::map<std::string, std::unique_ptr<MCPServerConnection>> connections_;
    std::map<std::string, MCPServerConfig> server_configs_;
    std::map<std::string, std::vector<MCPTool>> tool_cache_;
    StatusCallback status_callback_;

    void notifyStatus(const std::string& server, const std::string& status);
};

} // namespace oleg

#endif // OLEG_MCP_CLIENT_H
