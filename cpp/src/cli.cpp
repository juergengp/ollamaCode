#include "cli.h"
#include "utils.h"
#include <iostream>
#include <sstream>
#include <cstring>
#include <chrono>
#include <unistd.h>

#ifdef HAVE_READLINE
#include <readline/readline.h>
#include <readline/history.h>
#endif

namespace ollamacode {

CLI::CLI()
    : temperature_override_(-1.0)
    , auto_approve_override_(false)
    , unsafe_mode_override_(false)
{
    config_ = std::make_unique<Config>();
    config_->initialize();

    client_ = std::make_unique<OllamaClient>(config_->getOllamaHost());
    parser_ = std::make_unique<ToolParser>();
    executor_ = std::make_unique<ToolExecutor>(*config_);

    // Set confirmation callback
    executor_->setConfirmCallback([this](const std::string& tool_name, const std::string& description) {
        return confirmToolExecution(tool_name, description);
    });
}

CLI::~CLI() = default;

bool CLI::parseArgs(int argc, char* argv[]) {
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];

        if (arg == "-h" || arg == "--help") {
            printHelp();
            return false;
        } else if (arg == "-v" || arg == "--version") {
            std::cout << "ollamaCode version 2.0.0 (C++)" << std::endl;
            return false;
        } else if (arg == "-m" || arg == "--model") {
            if (i + 1 < argc) {
                model_override_ = argv[++i];
            }
        } else if (arg == "-t" || arg == "--temperature") {
            if (i + 1 < argc) {
                temperature_override_ = std::stod(argv[++i]);
            }
        } else if (arg == "-a" || arg == "--auto-approve") {
            auto_approve_override_ = true;
            config_->setAutoApprove(true);
        } else if (arg == "--unsafe") {
            unsafe_mode_override_ = true;
            config_->setSafeMode(false);
        } else {
            // Collect remaining args as prompt
            for (int j = i; j < argc; j++) {
                if (!direct_prompt_.empty()) direct_prompt_ += " ";
                direct_prompt_ += argv[j];
            }
            break;
        }
    }

    return true;
}

void CLI::printBanner() {
    std::cout << utils::terminal::CYAN << utils::terminal::BOLD;
    std::cout << R"(
   ____  _ _                       ____          _
  / __ \| | | __ _ _ __ ___   __ _/ ___|___   __| | ___
 | |  | | | |/ _` | '_ ` _ \ / _` | |   / _ \ / _` |/ _ \
 | |__| | | | (_| | | | | | | (_| | |__| (_) | (_| |  __/
  \____/|_|_|\__,_|_| |_| |_|\__,_|\____\___/ \__,_|\___|

)" << utils::terminal::RESET;

    std::cout << utils::terminal::BLUE << "Interactive CLI for Ollama - Version 2.0.0 (C++)" << utils::terminal::RESET << "\n";
    std::cout << utils::terminal::YELLOW << "Type 'help' for commands, 'exit' to quit" << utils::terminal::RESET << "\n\n";
}

void CLI::printHelp() {
    std::cout << R"(Usage: ollamacode [OPTIONS] [PROMPT]

Interactive CLI for Ollama with Claude Code-like tool calling

OPTIONS:
    -m, --model MODEL       Use specific model
    -t, --temperature NUM   Set temperature (0.0-2.0)
    -a, --auto-approve      Auto-approve all tool executions
    --unsafe                Disable safe mode (allow all commands)
    -v, --version           Show version
    -h, --help              Show this help

INTERACTIVE COMMANDS:
    help                    Show available commands
    models                  List available models
    use MODEL               Switch to different model
    temp NUM                Set temperature
    safe [on|off]           Toggle safe mode
    auto [on|off]           Toggle auto-approve
    clear                   Clear screen
    config                  Show configuration
    exit, quit              Exit ollamacode

EXAMPLES:
    ollamacode                              # Start interactive mode
    ollamacode "List all Python files"      # Single prompt with tools
    ollamacode -m llama3 "Hello"            # Use specific model
    ollamacode -a "Build the project"       # Auto-approve all tools

)";
}

void CLI::printConfig() {
    std::cout << utils::terminal::CYAN << utils::terminal::BOLD << "Current Configuration:" << utils::terminal::RESET << "\n";
    std::cout << "  Model:        " << utils::terminal::GREEN << config_->getModel() << utils::terminal::RESET << "\n";
    std::cout << "  Host:         " << config_->getOllamaHost() << "\n";
    std::cout << "  Temperature:  " << config_->getTemperature() << "\n";
    std::cout << "  Max Tokens:   " << config_->getMaxTokens() << "\n";
    std::cout << "  Safe Mode:    " << (config_->getSafeMode() ? "true" : "false") << "\n";
    std::cout << "  Auto Approve: " << (config_->getAutoApprove() ? "true" : "false") << "\n";

    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd))) {
        std::cout << "  Working Dir:  " << cwd << "\n";
    }
    std::cout << "\n";
}

void CLI::printModels() {
    std::cout << utils::terminal::CYAN << utils::terminal::BOLD << "Available Models:" << utils::terminal::RESET << "\n";

    auto models = client_->listModels();
    if (models.empty()) {
        utils::terminal::printWarning("No models found. Make sure Ollama is running.");
    } else {
        for (const auto& model : models) {
            std::cout << "  " << model << "\n";
        }
    }
    std::cout << "\n";
}

std::string CLI::getSystemPrompt() {
    return R"(You are an AI coding assistant with tool access. When asked to perform actions, use the appropriate tools.

## Available Tools

**Bash** - Execute shell commands
  - command: The shell command to run
  - description: What the command does

**Read** - Read file contents
  - file_path: Path to file

**Write** - Write/create files
  - file_path: Path to file
  - content: File content

**Edit** - Edit existing files
  - file_path: Path to file
  - old_string: Text to replace
  - new_string: Replacement text

**Glob** - Find files by pattern
  - pattern: File pattern (e.g., "*.py")
  - path: Directory to search (optional)

**Grep** - Search in files
  - pattern: Text to find
  - path: Where to search (optional)
  - output_mode: "content" or "files_with_matches" (optional)

## Tool Usage Format

<tool_calls>
<tool_call>
<tool_name>Bash</tool_name>
<parameters>
<command>ls -la</command>
<description>List files</description>
</parameters>
</tool_call>
</tool_calls>

Multiple tools can be called by adding more <tool_call> blocks within <tool_calls>.

When the user asks you to do something, think about what tools you need, explain your plan briefly, then execute the tools. After tools run, you'll receive the results and can provide your final answer.
)";
}

json CLI::buildMessages(const std::string& user_message) {
    json messages = json::array();

    // System message with tool definitions
    messages.push_back({
        {"role", "system"},
        {"content", getSystemPrompt()}
    });

    // User message with environment context
    std::ostringstream userContent;
    userContent << user_message << "\n\nEnvironment:\n";

    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd))) {
        userContent << "- Working Directory: " << cwd << "\n";
    }

    char* user = getenv("USER");
    if (user) {
        userContent << "- User: " << user << "\n";
    }

    userContent << "- Date: " << utils::getCurrentTimestamp();

    messages.push_back({
        {"role", "user"},
        {"content", userContent.str()}
    });

    return messages;
}

// Legacy method for backward compatibility
std::string CLI::buildContext(const std::string& user_message) {
    std::ostringstream context;

    context << getSystemPrompt() << "\n\n";
    context << "Current Environment:\n";

    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd))) {
        context << "- Working Directory: " << cwd << "\n";
    }

    char* user = getenv("USER");
    if (user) {
        context << "- User: " << user << "\n";
    }

    context << "- Date: " << utils::getCurrentTimestamp() << "\n";
    context << "- OS: " << "Linux" << "\n\n"; // Could use uname

    context << "User Request: " << user_message;

    return context.str();
}

void CLI::processResponse(const std::string& response, int iteration) {
    if (iteration > MAX_TOOL_ITERATIONS) {
        utils::terminal::printWarning("Maximum tool calling iterations reached");
        return;
    }

    // Extract and display response text
    std::string responseText = parser_->extractResponseText(response);
    if (!responseText.empty()) {
        std::cout << utils::terminal::GREEN << responseText << utils::terminal::RESET << "\n\n";
    }

    // Parse tool calls
    auto toolCalls = parser_->parseToolCalls(response);

    if (toolCalls.empty()) {
        return; // No tools to execute
    }

    // Execute tools
    std::cout << utils::terminal::CYAN << utils::terminal::BOLD
              << "🔧 Executing " << toolCalls.size() << " tool(s)..."
              << utils::terminal::RESET << "\n\n";

    auto results = executor_->executeAll(toolCalls);

    // Build results summary for next AI iteration
    std::ostringstream resultsSummary;
    resultsSummary << "Tool execution results:\n\n";

    for (size_t i = 0; i < toolCalls.size(); i++) {
        resultsSummary << "Tool: " << toolCalls[i].name << "\n";
        resultsSummary << "Exit Code: " << results[i].exit_code << "\n";
        resultsSummary << "Success: " << (results[i].success ? "true" : "false") << "\n";
        if (!results[i].error.empty()) {
            resultsSummary << "Error: " << results[i].error << "\n";
        }
        if (!results[i].output.empty()) {
            resultsSummary << "Output:\n" << results[i].output << "\n";
        }
        resultsSummary << "\n";
    }

    resultsSummary << "Based on these results, provide your analysis or next steps. Only use more tools if absolutely necessary.";

    // Send results back to AI
    std::cout << utils::terminal::CYAN << utils::terminal::BOLD
              << "📊 Tool execution completed. Processing results..."
              << utils::terminal::RESET << "\n\n";

    std::string model = model_override_.empty() ? config_->getModel() : model_override_;
    double temp = temperature_override_ < 0 ? config_->getTemperature() : temperature_override_;

    auto nextResponse = client_->generate(model, resultsSummary.str(), temp, config_->getMaxTokens());

    if (!nextResponse.isSuccess()) {
        utils::terminal::printError("Failed to get AI response: " + nextResponse.error);
        return;
    }

    // Recursively process next response
    processResponse(nextResponse.response, iteration + 1);
}

void CLI::processResponseWithMessages(json& messages, const std::string& response, int iteration) {
    if (iteration > MAX_TOOL_ITERATIONS) {
        utils::terminal::printWarning("Maximum tool calling iterations reached");
        return;
    }

    // Extract and display response text
    std::string responseText = parser_->extractResponseText(response);
    if (!responseText.empty()) {
        std::cout << utils::terminal::GREEN << responseText << utils::terminal::RESET << "\n\n";
    }

    // Parse tool calls
    auto toolCalls = parser_->parseToolCalls(response);

    if (toolCalls.empty()) {
        return; // No tools to execute
    }

    // Execute tools
    std::cout << utils::terminal::CYAN << utils::terminal::BOLD
              << "🔧 Executing " << toolCalls.size() << " tool(s)..."
              << utils::terminal::RESET << "\n\n";

    auto results = executor_->executeAll(toolCalls);

    // Build results summary for next AI iteration
    std::ostringstream resultsSummary;
    resultsSummary << "Tool execution results:\n\n";

    for (size_t i = 0; i < toolCalls.size(); i++) {
        resultsSummary << "Tool: " << toolCalls[i].name << "\n";
        resultsSummary << "Exit Code: " << results[i].exit_code << "\n";
        resultsSummary << "Success: " << (results[i].success ? "true" : "false") << "\n";
        if (!results[i].error.empty()) {
            resultsSummary << "Error: " << results[i].error << "\n";
        }
        if (!results[i].output.empty()) {
            resultsSummary << "Output:\n" << results[i].output << "\n";
        }
        resultsSummary << "\n";
    }

    resultsSummary << "Based on these results, provide your analysis or next steps. Only use more tools if absolutely necessary.";

    // Add assistant response to messages
    messages.push_back({
        {"role", "assistant"},
        {"content", response}
    });

    // Add tool results as user message
    messages.push_back({
        {"role", "user"},
        {"content", resultsSummary.str()}
    });

    // Send results back to AI
    std::cout << utils::terminal::CYAN << utils::terminal::BOLD
              << "📊 Tool execution completed. Processing results..."
              << utils::terminal::RESET << "\n\n";

    std::string model = model_override_.empty() ? config_->getModel() : model_override_;
    double temp = temperature_override_ < 0 ? config_->getTemperature() : temperature_override_;

    auto nextResponse = client_->chat(model, messages, temp, config_->getMaxTokens());

    if (!nextResponse.isSuccess()) {
        utils::terminal::printError("Failed to get AI response: " + nextResponse.error);
        return;
    }

    // Recursively process next response
    processResponseWithMessages(messages, nextResponse.response, iteration + 1);
}

void CLI::singlePromptMode(const std::string& prompt) {
    auto start = std::chrono::steady_clock::now();

    json messages = buildMessages(prompt);

    std::string model = model_override_.empty() ? config_->getModel() : model_override_;
    double temp = temperature_override_ < 0 ? config_->getTemperature() : temperature_override_;

    auto response = client_->chat(model, messages, temp, config_->getMaxTokens());

    if (!response.isSuccess()) {
        utils::terminal::printError("Failed to get AI response: " + response.error);
        return;
    }

    processResponseWithMessages(messages, response.response);

    auto end = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - start);

    std::cout << "\n" << utils::terminal::MAGENTA << "───────────────────────────────────────" << utils::terminal::RESET << "\n";
    std::cout << utils::terminal::MAGENTA << "⏱ Duration: " << duration.count() << "s" << utils::terminal::RESET << "\n";
}

void CLI::handleCommand(const std::string& input) {
    std::string cmd = utils::trim(input);

    if (cmd == "help") {
        printHelp();
    } else if (cmd == "models") {
        printModels();
    } else if (cmd == "config") {
        printConfig();
    } else if (cmd == "clear") {
        utils::terminal::clearScreen();
        printBanner();
    } else if (utils::startsWith(cmd, "use ")) {
        std::string model = cmd.substr(4);
        config_->setModel(model);
        utils::terminal::printSuccess("Switched to model: " + model);
    } else if (utils::startsWith(cmd, "temp ")) {
        double temp = std::stod(cmd.substr(5));
        config_->setTemperature(temp);
        utils::terminal::printSuccess("Temperature set to: " + std::to_string(temp));
    } else if (cmd == "safe on") {
        config_->setSafeMode(true);
        utils::terminal::printSuccess("Safe mode enabled");
    } else if (cmd == "safe off") {
        config_->setSafeMode(false);
        utils::terminal::printWarning("Safe mode disabled");
    } else if (cmd == "auto on") {
        config_->setAutoApprove(true);
        utils::terminal::printWarning("Auto-approve enabled");
    } else if (cmd == "auto off") {
        config_->setAutoApprove(false);
        utils::terminal::printSuccess("Auto-approve disabled");
    } else {
        utils::terminal::printError("Unknown command. Type 'help' for available commands.");
    }
}

bool CLI::confirmToolExecution(const std::string& tool_name, const std::string& description) {
    return true; // Handled in tool_executor already
}

void CLI::interactiveMode() {
    printBanner();
    printConfig();

    while (true) {
        std::cout << utils::terminal::BOLD << utils::terminal::CYAN << "You> " << utils::terminal::RESET;

        std::string input;

#ifdef HAVE_READLINE
        char* line = readline("");
        if (!line) break;
        input = line;
        if (!input.empty()) {
            add_history(line);
        }
        free(line);
#else
        if (!std::getline(std::cin, input)) break;
#endif

        input = utils::trim(input);
        if (input.empty()) continue;

        if (input == "exit" || input == "quit") {
            std::cout << utils::terminal::GREEN << "👋 Goodbye!" << utils::terminal::RESET << "\n";
            break;
        }

        // Check if it's a command
        if (input == "help" || input == "models" || input == "config" || input == "clear" ||
            utils::startsWith(input, "use ") || utils::startsWith(input, "temp ") ||
            utils::startsWith(input, "safe ") || utils::startsWith(input, "auto ")) {
            handleCommand(input);
            continue;
        }

        // Process as AI prompt
        std::cout << "\n";
        auto start = std::chrono::steady_clock::now();

        json messages = buildMessages(input);

        std::string model = model_override_.empty() ? config_->getModel() : model_override_;
        double temp = temperature_override_ < 0 ? config_->getTemperature() : temperature_override_;

        std::cout << utils::terminal::BLUE << utils::terminal::BOLD << "🤔 Thinking..." << utils::terminal::RESET << "\n\n";

        auto response = client_->chat(model, messages, temp, config_->getMaxTokens());

        if (!response.isSuccess()) {
            utils::terminal::printError("Failed to get AI response: " + response.error);
            continue;
        }

        processResponseWithMessages(messages, response.response);

        auto end = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - start);

        std::cout << "\n" << utils::terminal::MAGENTA << "───────────────────────────────────────" << utils::terminal::RESET << "\n";
        std::cout << utils::terminal::MAGENTA << "⏱ Duration: " << duration.count() << "s" << utils::terminal::RESET << "\n\n";
    }
}

int CLI::run() {
    // Test Ollama connection
    if (!client_->testConnection()) {
        utils::terminal::printError("Failed to connect to Ollama at " + config_->getOllamaHost());
        utils::terminal::printWarning("Make sure Ollama is running with: ollama serve");
        return 1;
    }

    // Single prompt mode or interactive mode
    if (!direct_prompt_.empty()) {
        singlePromptMode(direct_prompt_);
        return 0;
    }

    // Interactive mode
    interactiveMode();
    return 0;
}

} // namespace ollamacode
