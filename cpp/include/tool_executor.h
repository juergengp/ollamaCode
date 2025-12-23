#ifndef CASPER_TOOL_EXECUTOR_H
#define CASPER_TOOL_EXECUTOR_H

#include <string>
#include <functional>
#include <memory>
#include "tool_parser.h"

namespace casper {

class Config; // Forward declaration
class MCPClient; // Forward declaration
class SearchClient; // Forward declaration
class DBClient; // Forward declaration
class RAGEngine; // Forward declaration

struct ToolResult {
    bool success;
    int exit_code;
    std::string output;
    std::string error;
};

class ToolExecutor {
public:
    explicit ToolExecutor(Config& config);

    // Execute a single tool call
    ToolResult execute(const ToolCall& tool_call);

    // Execute multiple tool calls
    std::vector<ToolResult> executeAll(const std::vector<ToolCall>& tool_calls);

    // Set confirmation callback (for interactive mode)
    using ConfirmCallback = std::function<bool(const std::string&, const std::string&)>;
    void setConfirmCallback(ConfirmCallback callback);

    // MCP Client integration
    void setMCPClient(MCPClient* client);
    bool isMCPTool(const std::string& tool_name) const;

    // Search, Database, and RAG integration
    void setSearchClient(SearchClient* client);
    void setDBClient(DBClient* client);
    void setRAGEngine(RAGEngine* engine);

private:
    Config& config_;
    ConfirmCallback confirm_callback_;
    MCPClient* mcp_client_;
    SearchClient* search_client_;
    DBClient* db_client_;
    RAGEngine* rag_engine_;

    // Tool implementations
    ToolResult executeBash(const ToolCall& tool_call);
    ToolResult executeRead(const ToolCall& tool_call);
    ToolResult executeWrite(const ToolCall& tool_call);
    ToolResult executeEdit(const ToolCall& tool_call);
    ToolResult executeGlob(const ToolCall& tool_call);
    ToolResult executeGrep(const ToolCall& tool_call);
    ToolResult executeMCPTool(const ToolCall& tool_call);

    // Search tools
    ToolResult executeWebSearch(const ToolCall& tool_call);
    ToolResult executeWebFetch(const ToolCall& tool_call);

    // Database tools
    ToolResult executeDBConnect(const ToolCall& tool_call);
    ToolResult executeDBQuery(const ToolCall& tool_call);
    ToolResult executeDBExecute(const ToolCall& tool_call);
    ToolResult executeDBSchema(const ToolCall& tool_call);

    // RAG tools
    ToolResult executeLearn(const ToolCall& tool_call);
    ToolResult executeRemember(const ToolCall& tool_call);
    ToolResult executeForget(const ToolCall& tool_call);

    // Network tools
    ToolResult executePing(const ToolCall& tool_call);
    ToolResult executeTraceroute(const ToolCall& tool_call);
    ToolResult executeNmap(const ToolCall& tool_call);
    ToolResult executeDig(const ToolCall& tool_call);
    ToolResult executeWhois(const ToolCall& tool_call);
    ToolResult executeNetstat(const ToolCall& tool_call);
    ToolResult executeCurl(const ToolCall& tool_call);
    ToolResult executeSSH(const ToolCall& tool_call);
    ToolResult executeTelnet(const ToolCall& tool_call);
    ToolResult executeNetcat(const ToolCall& tool_call);
    ToolResult executeIfconfig(const ToolCall& tool_call);
    ToolResult executeArp(const ToolCall& tool_call);

    // Package manager tools
    ToolResult executeBrew(const ToolCall& tool_call);
    ToolResult executePip(const ToolCall& tool_call);
    ToolResult executeNpm(const ToolCall& tool_call);
    ToolResult executeApt(const ToolCall& tool_call);
    ToolResult executeDnf(const ToolCall& tool_call);
    ToolResult executeYum(const ToolCall& tool_call);
    ToolResult executePacman(const ToolCall& tool_call);
    ToolResult executeZypper(const ToolCall& tool_call);

    // File operation tools
    ToolResult executeTar(const ToolCall& tool_call);
    ToolResult executeZip(const ToolCall& tool_call);
    ToolResult executeUnzip(const ToolCall& tool_call);
    ToolResult executeGzip(const ToolCall& tool_call);
    ToolResult executeRsync(const ToolCall& tool_call);
    ToolResult executeScp(const ToolCall& tool_call);
    ToolResult executeCp(const ToolCall& tool_call);
    ToolResult executeMv(const ToolCall& tool_call);
    ToolResult executeRm(const ToolCall& tool_call);
    ToolResult executeMkdir(const ToolCall& tool_call);
    ToolResult executeChmod(const ToolCall& tool_call);
    ToolResult executeChown(const ToolCall& tool_call);
    ToolResult executeDf(const ToolCall& tool_call);
    ToolResult executeDu(const ToolCall& tool_call);

    // Helpers
    bool isCommandSafe(const std::string& command);
    bool requestConfirmation(const std::string& tool_name, const std::string& description);
    std::string executeCommand(const std::string& command, int& exit_code);
};

} // namespace casper

#endif // CASPER_TOOL_EXECUTOR_H
