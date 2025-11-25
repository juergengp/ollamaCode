#ifndef OLLAMACODE_CLI_H
#define OLLAMACODE_CLI_H

#include <string>
#include <vector>
#include <memory>
#include "config.h"
#include "ollama_client.h"
#include "tool_parser.h"
#include "tool_executor.h"
#include "session_manager.h"

namespace ollamacode {

class CLI {
public:
    CLI();
    ~CLI();

    // Parse command line arguments
    bool parseArgs(int argc, char* argv[]);

    // Run the application
    int run();

private:
    // Interactive mode
    void interactiveMode();

    // Single prompt mode
    void singlePromptMode(const std::string& prompt);

    // Process AI response with tool calling
    void processResponse(const std::string& response, int iteration = 1);
    void processResponseWithMessages(json& messages, const std::string& response, int iteration = 1);

    // Build context for AI
    std::string buildContext(const std::string& user_message);
    json buildMessages(const std::string& user_message);

    // Get system prompt
    std::string getSystemPrompt();

    // UI helpers
    void printBanner();
    void printHelp();
    void printConfig();
    void printModels();

    // Command handlers
    void handleCommand(const std::string& input);

    // Confirmation callback for tools
    bool confirmToolExecution(const std::string& tool_name, const std::string& description);

    // Members
    std::unique_ptr<Config> config_;
    std::unique_ptr<OllamaClient> client_;
    std::unique_ptr<ToolParser> parser_;
    std::unique_ptr<ToolExecutor> executor_;
    std::unique_ptr<SessionManager> session_manager_;

    // Options from command line
    std::string direct_prompt_;
    std::string model_override_;
    double temperature_override_;
    bool auto_approve_override_;
    bool unsafe_mode_override_;
    bool resume_session_;
    std::string resume_session_id_;
    bool list_sessions_;
    bool export_session_;
    std::string export_format_;  // "json" or "markdown"

    static const int MAX_TOOL_ITERATIONS = 10;
};

} // namespace ollamacode

#endif // OLLAMACODE_CLI_H
