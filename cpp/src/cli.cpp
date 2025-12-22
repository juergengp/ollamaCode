#include "cli.h"
#include "utils.h"
#include "command_menu.h"
#include "agent.h"
#include "task_suggester.h"
#include <iostream>
#include <sstream>
#include <cstring>
#include <chrono>
#include <unistd.h>
#include <termios.h>

#ifdef HAVE_READLINE
#include <readline/readline.h>
#include <readline/history.h>
#endif

namespace oleg {

CLI::CLI()
    : agentModeEnabled_(true)  // Enable agent mode by default
    , temperature_override_(-1.0)
    , auto_approve_override_(false)
    , unsafe_mode_override_(false)
{
    config_ = std::make_unique<Config>();
    config_->initialize();

    client_ = std::make_unique<OllamaClient>(config_->getOllamaHost());
    parser_ = std::make_unique<ToolParser>();
    executor_ = std::make_unique<ToolExecutor>(*config_);
    command_menu_ = std::make_unique<CommandMenu>();
    mcp_client_ = std::make_unique<MCPClient>();
    task_suggester_ = std::make_unique<TaskSuggester>();

    // Initialize license manager
    license_manager_ = std::make_unique<LicenseManager>();
    license_manager_->initialize();

    // Initialize model manager
    model_manager_ = std::make_unique<ModelManager>(*client_, *config_, license_manager_.get());

    // Initialize prompt database
    prompt_db_ = std::make_unique<PromptDatabase>();
    prompt_db_->initialize();
    prompt_db_->setLicenseManager(license_manager_.get());

    // Initialize with general agent
    currentAgent_ = AgentRegistry::getAgent(AgentType::General);

    // Set confirmation callback
    executor_->setConfirmCallback([this](const std::string& tool_name, const std::string& description) {
        return confirmToolExecution(tool_name, description);
    });

    // Connect MCP client to executor
    executor_->setMCPClient(mcp_client_.get());
}

CLI::~CLI() = default;

bool CLI::parseArgs(int argc, char* argv[]) {
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];

        if (arg == "-h" || arg == "--help") {
            printHelp();
            return false;
        } else if (arg == "-v" || arg == "--version") {
            std::cout << "OlEg version 2.3.1 (C++)" << std::endl;
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
        } else if (arg == "--mcp") {
            config_->setMCPEnabled(true);
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

    std::cout << utils::terminal::BLUE << "Interactive CLI for Ollama - Version 2.3.1 (C++)" << utils::terminal::RESET << "\n";
    std::cout << utils::terminal::YELLOW << "Type '/help' for commands, '/exit' to quit" << utils::terminal::RESET << "\n\n";
}

void CLI::printHelp() {
    std::cout << R"(Usage: oleg [OPTIONS] [PROMPT]

Interactive CLI for Ollama with Claude Code-like tool calling

OPTIONS:
    -m, --model MODEL       Use specific model
    -t, --temperature NUM   Set temperature (0.0-2.0)
    -a, --auto-approve      Auto-approve all tool executions
    --unsafe                Disable safe mode (allow all commands)
    --mcp                   Enable MCP servers on startup
    -v, --version           Show version
    -h, --help              Show this help

INTERACTIVE COMMANDS (use /command):
    /help                   Show available commands
    /models                 List available models
    /model                  Interactive model selector
    /use MODEL              Switch to different model
    /host URL               Set Ollama host (e.g., http://192.168.1.100:11434)
    /temp NUM               Set temperature
    /safe [on|off]          Toggle safe mode
    /auto [on|off]          Toggle auto-approve
    /mcp                    Show MCP status and tools
    /mcp on                 Enable MCP and connect servers
    /mcp off                Disable MCP and disconnect servers
    /mcp servers            List configured MCP servers
    /mcp tools              List available MCP tools
    /clear                  Clear screen
    /config                 Show configuration
    /exit, /quit            Exit oleg

AGENT COMMANDS:
    /agent                  Show current agent status
    /agent on               Enable agent selection mode
    /agent off              Disable agent selection mode
    /explore                Switch to explorer agent (read-only)
    /code                   Switch to coder agent (code changes)
    /run                    Switch to runner agent (commands)
    /plan                   Switch to planner agent (planning)
    /general                Switch to general agent (all tools)
    /search, /web           Switch to searcher agent (web search)
    /db, /database          Switch to database agent (SQL queries)
    /learn, /memory, /rag   Switch to learner agent (RAG knowledge)

MODEL MANAGEMENT:
    /model create           Interactive custom model creation
    /model show <name>      Display model details
    /model copy <src> <dst> Clone/copy a model
    /model delete <name>    Delete a model
    /model pull <name>      Download model from Ollama
    /model push <name>      Upload model to Ollama.ai

PROMPT DATABASE:
    /prompt                 Interactive prompt selector
    /prompt add             Add new prompt
    /prompt edit <name>     Edit existing prompt
    /prompt delete <name>   Delete prompt
    /prompt list            List all prompts
    /prompt search <query>  Search prompts
    /prompt export <file>   Export prompts to JSON
    /prompt import <file>   Import prompts from JSON

LICENSE:
    /license                Show license status
    /license activate <key> Activate license key
    /license deactivate     Remove license

EXAMPLES:
    oleg                              # Start interactive mode
    oleg "List all Python files"      # Single prompt with tools
    oleg -m llama3 "Hello"            # Use specific model
    oleg -a "Build the project"       # Auto-approve all tools
    oleg --mcp "Search the web"       # Use MCP tools

MCP SERVERS:
    Configure MCP servers in ~/.config/oleg/mcp_servers.json
    See docs/MCP_SETUP.md for configuration examples

WHAT'S NEW in v2.3.0:
    - Custom LLM builder with Modelfile support
    - Model management: create, copy, delete, pull, push
    - Prompt database with categories and search
    - Activation key system for premium features
    - Import/export prompts as JSON

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
    std::cout << "  MCP Enabled:  " << (config_->getMCPEnabled() ? std::string(utils::terminal::GREEN) + "true" : "false") << utils::terminal::RESET << "\n";
    std::cout << "  Agent Mode:   " << (agentModeEnabled_ ? std::string(utils::terminal::GREEN) + "enabled" : "disabled") << utils::terminal::RESET << "\n";
    std::cout << "  Current Agent:" << utils::terminal::GREEN << " " << currentAgent_.getDisplayName() << utils::terminal::RESET << "\n";

    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd))) {
        std::cout << "  Working Dir:  " << cwd << "\n";
    }
    std::cout << "\n";
}

// Agent helper methods
void CLI::switchAgent(AgentType type) {
    currentAgent_ = AgentRegistry::getAgent(type);
    std::cout << utils::terminal::GREEN << "Switched to " << currentAgent_.getDisplayName()
              << utils::terminal::RESET << "\n";
    std::cout << utils::terminal::YELLOW << "  " << currentAgent_.description
              << utils::terminal::RESET << "\n";

    if (!currentAgent_.allowedTools.empty()) {
        std::cout << utils::terminal::CYAN << "  Tools: ";
        for (const auto& tool : currentAgent_.allowedTools) {
            std::cout << tool << " ";
        }
        std::cout << utils::terminal::RESET << "\n";
    }
    std::cout << "\n";
}

void CLI::printAgentStatus() {
    std::cout << utils::terminal::CYAN << utils::terminal::BOLD << "Agent Status:" << utils::terminal::RESET << "\n";
    std::cout << "  Mode:    " << (agentModeEnabled_ ? std::string(utils::terminal::GREEN) + "enabled" : "disabled")
              << utils::terminal::RESET << "\n";
    std::cout << "  Current: " << utils::terminal::GREEN << currentAgent_.getDisplayName()
              << utils::terminal::RESET << "\n";
    std::cout << "           " << currentAgent_.description << "\n";

    if (!currentAgent_.allowedTools.empty()) {
        std::cout << "  Tools:   ";
        for (const auto& tool : currentAgent_.allowedTools) {
            std::cout << tool << " ";
        }
        std::cout << "\n";
    } else {
        std::cout << "  Tools:   all available\n";
    }

    std::cout << "\n" << utils::terminal::CYAN << "Available Agents:" << utils::terminal::RESET << "\n";
    auto agents = AgentRegistry::getAllAgents();
    for (const auto& agent : agents) {
        std::cout << "  " << agent.getDisplayName() << " - " << agent.description << "\n";
    }
    std::cout << "\n";
}

void CLI::handleAgentCommand(const std::string& cmd) {
    if (cmd == "agent" || cmd == "agent status") {
        printAgentStatus();
    } else if (cmd == "agent on") {
        agentModeEnabled_ = true;
        utils::terminal::printSuccess("Agent selection mode enabled");
    } else if (cmd == "agent off") {
        agentModeEnabled_ = false;
        utils::terminal::printSuccess("Agent selection mode disabled");
    } else if (cmd == "explore") {
        switchAgent(AgentType::Explorer);
    } else if (cmd == "code") {
        switchAgent(AgentType::Coder);
    } else if (cmd == "run") {
        switchAgent(AgentType::Runner);
    } else if (cmd == "plan") {
        switchAgent(AgentType::Planner);
    } else if (cmd == "general") {
        switchAgent(AgentType::General);
    } else if (cmd == "search" || cmd == "web") {
        switchAgent(AgentType::Searcher);
    } else if (cmd == "db" || cmd == "database") {
        switchAgent(AgentType::Database);
    } else if (cmd == "learn" || cmd == "memory" || cmd == "rag") {
        switchAgent(AgentType::Learner);
    } else {
        utils::terminal::printError("Unknown agent command. Try: /agent, /explore, /code, /run, /plan, /general, /search, /db, /learn");
    }
}

void CLI::processWithAgentSelection(const std::string& input) {
    // Analyze the task and get suggestions
    auto suggestions = task_suggester_->analyzeTask(input);

    // If only one suggestion and it's general agent, skip the menu
    if (suggestions.size() == 1 && suggestions[0].agentType == AgentType::General) {
        executeAgentTask(suggestions[0], input);
        return;
    }

    // Show agent selection menu
    auto result = task_suggester_->showAgentSelectionMenu(suggestions, input);

    if (result.cancelled) {
        std::cout << utils::terminal::YELLOW << "Cancelled." << utils::terminal::RESET << "\n\n";
        return;
    }

    // Handle custom input
    if (!result.customInput.empty()) {
        // Re-analyze with custom input
        processWithAgentSelection(result.customInput);
        return;
    }

    // Execute all tasks in sequence
    if (result.executeAll && !result.selectedTasks.empty()) {
        executeAllAgentTasks(result.selectedTasks, input);
        return;
    }

    // Execute single selected task
    if (!result.selectedTasks.empty()) {
        executeAgentTask(result.selectedTasks[0], input);
    } else {
        // Execute with selected agent type
        TaskSuggestion task;
        task.agentType = result.selectedAgent;
        task.taskDescription = input;
        executeAgentTask(task, input);
    }
}

void CLI::executeAgentTask(const TaskSuggestion& task, const std::string& originalInput) {
    // Switch to the appropriate agent
    Agent previousAgent = currentAgent_;
    currentAgent_ = AgentRegistry::getAgent(task.agentType);

    std::cout << utils::terminal::CYAN << utils::terminal::BOLD
              << currentAgent_.getDisplayName() << " working..."
              << utils::terminal::RESET << "\n\n";

    auto start = std::chrono::steady_clock::now();

    json messages = buildMessages(originalInput);

    std::string model = model_override_.empty() ? config_->getModel() : model_override_;

    // Use agent's temperature if specified
    double temp;
    if (currentAgent_.temperatureOverride >= 0) {
        temp = currentAgent_.temperatureOverride;
    } else {
        temp = temperature_override_ < 0 ? config_->getTemperature() : temperature_override_;
    }

    auto response = client_->chat(model, messages, temp, config_->getMaxTokens());

    if (!response.isSuccess()) {
        utils::terminal::printError("Failed to get AI response: " + response.error);
        currentAgent_ = previousAgent;
        return;
    }

    processResponseWithMessages(messages, response.response);

    auto end = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - start);

    std::cout << "\n" << utils::terminal::MAGENTA << "───────────────────────────────────────" << utils::terminal::RESET << "\n";
    std::cout << utils::terminal::MAGENTA << "⏱ " << currentAgent_.getDisplayName()
              << " completed in " << duration.count() << "s" << utils::terminal::RESET << "\n\n";

    // Restore previous agent
    currentAgent_ = previousAgent;
}

void CLI::executeAllAgentTasks(const std::vector<TaskSuggestion>& tasks, const std::string& originalInput) {
    std::cout << utils::terminal::CYAN << utils::terminal::BOLD
              << "Executing " << tasks.size() << " tasks in sequence..."
              << utils::terminal::RESET << "\n\n";

    auto totalStart = std::chrono::steady_clock::now();

    for (size_t i = 0; i < tasks.size(); i++) {
        const auto& task = tasks[i];
        Agent agent = AgentRegistry::getAgent(task.agentType);

        std::cout << utils::terminal::CYAN << "[" << (i + 1) << "/" << tasks.size() << "] "
                  << agent.getDisplayName() << ": " << task.reasoning
                  << utils::terminal::RESET << "\n";

        executeAgentTask(task, originalInput);
    }

    auto totalEnd = std::chrono::steady_clock::now();
    auto totalDuration = std::chrono::duration_cast<std::chrono::seconds>(totalEnd - totalStart);

    std::cout << utils::terminal::GREEN << utils::terminal::BOLD
              << "All tasks completed in " << totalDuration.count() << "s"
              << utils::terminal::RESET << "\n\n";
}

// MCP Methods
void CLI::initializeMCP() {
    if (!config_->getMCPEnabled()) {
        return;
    }

    // Set status callback
    mcp_client_->setStatusCallback([](const std::string& server, const std::string& status) {
        std::cout << utils::terminal::CYAN << "  [MCP] " << server << ": " << status << utils::terminal::RESET << "\n";
    });

    // Load server configurations from config
    auto servers = config_->getMCPServers();
    for (const auto& server : servers) {
        oleg::MCPServerConfig mcp_config;
        mcp_config.name = server.name;
        mcp_config.command = server.command;
        mcp_config.args = server.args;
        mcp_config.env = server.env;
        mcp_config.enabled = server.enabled;
        mcp_config.transport = server.transport;
        mcp_config.url = server.url;
        mcp_client_->addServer(mcp_config);
    }

    // Connect to enabled servers
    std::cout << utils::terminal::CYAN << utils::terminal::BOLD << "Connecting to MCP servers..." << utils::terminal::RESET << "\n";
    mcp_client_->connectAll();
    std::cout << "\n";
}

void CLI::printMCPStatus() {
    std::cout << utils::terminal::CYAN << utils::terminal::BOLD << "MCP Status:" << utils::terminal::RESET << "\n";
    std::cout << "  Enabled: " << (config_->getMCPEnabled() ? std::string(utils::terminal::GREEN) + "yes" : "no") << utils::terminal::RESET << "\n";

    auto servers = config_->getMCPServers();
    if (servers.empty()) {
        std::cout << "  Servers: " << utils::terminal::YELLOW << "none configured" << utils::terminal::RESET << "\n";
        std::cout << "\n  Configure servers in: " << Config::getMCPConfigPath() << "\n";
    } else {
        std::cout << "  Servers:\n";
        for (const auto& server : servers) {
            bool connected = mcp_client_->isServerConnected(server.name);
            std::string status = connected ? std::string(utils::terminal::GREEN) + "connected" : std::string(utils::terminal::YELLOW) + "disconnected";
            std::string enabled = server.enabled ? "" : " (disabled)";
            std::cout << "    - " << server.name << ": " << status << utils::terminal::RESET << enabled << "\n";
        }
    }

    auto tools = mcp_client_->getAllTools();
    std::cout << "  Available Tools: " << tools.size() << "\n";
    std::cout << "\n";
}

void CLI::printMCPTools() {
    auto tools = mcp_client_->getAllTools();

    if (tools.empty()) {
        std::cout << utils::terminal::YELLOW << "No MCP tools available." << utils::terminal::RESET << "\n";
        std::cout << "Make sure MCP is enabled (/mcp on) and servers are configured.\n\n";
        return;
    }

    std::cout << utils::terminal::CYAN << utils::terminal::BOLD << "Available MCP Tools:" << utils::terminal::RESET << "\n\n";

    std::string currentServer;
    for (const auto& tool : tools) {
        if (tool.serverName != currentServer) {
            currentServer = tool.serverName;
            std::cout << utils::terminal::MAGENTA << "  [" << currentServer << "]" << utils::terminal::RESET << "\n";
        }
        std::cout << "    " << utils::terminal::GREEN << tool.name << utils::terminal::RESET;
        if (!tool.description.empty()) {
            std::cout << " - " << tool.description;
        }
        std::cout << "\n";
    }
    std::cout << "\n";
}

void CLI::handleMCPCommand(const std::string& cmd) {
    if (cmd == "mcp" || cmd == "mcp status") {
        printMCPStatus();
    } else if (cmd == "mcp on") {
        config_->setMCPEnabled(true);
        utils::terminal::printSuccess("MCP enabled");
        initializeMCP();
    } else if (cmd == "mcp off") {
        config_->setMCPEnabled(false);
        mcp_client_->disconnectAll();
        utils::terminal::printSuccess("MCP disabled");
    } else if (cmd == "mcp servers") {
        auto servers = config_->getMCPServers();
        if (servers.empty()) {
            std::cout << utils::terminal::YELLOW << "No MCP servers configured." << utils::terminal::RESET << "\n";
            std::cout << "Add servers to: " << Config::getMCPConfigPath() << "\n\n";
        } else {
            std::cout << utils::terminal::CYAN << utils::terminal::BOLD << "Configured MCP Servers:" << utils::terminal::RESET << "\n\n";
            for (const auto& server : servers) {
                std::cout << "  " << utils::terminal::GREEN << server.name << utils::terminal::RESET << "\n";
                std::cout << "    Command:   " << server.command << "\n";
                if (!server.args.empty()) {
                    std::cout << "    Args:      ";
                    for (const auto& arg : server.args) {
                        std::cout << arg << " ";
                    }
                    std::cout << "\n";
                }
                std::cout << "    Enabled:   " << (server.enabled ? "yes" : "no") << "\n";
                std::cout << "    Transport: " << server.transport << "\n";
                std::cout << "\n";
            }
        }
    } else if (cmd == "mcp tools") {
        printMCPTools();
    } else {
        utils::terminal::printError("Unknown MCP command. Try: /mcp, /mcp on, /mcp off, /mcp servers, /mcp tools");
    }
}

std::string CLI::getMCPToolsPrompt() {
    auto tools = mcp_client_->getAllTools();
    if (tools.empty()) {
        return "";
    }

    std::ostringstream prompt;
    prompt << "\n\n## MCP Tools (External Services)\n\n";
    prompt << "You also have access to these MCP (Model Context Protocol) tools from external servers:\n\n";

    for (const auto& tool : tools) {
        prompt << "**" << tool.serverName << "__" << tool.name << "** - " << tool.description << "\n";
        if (!tool.inputSchema.empty() && tool.inputSchema.contains("properties")) {
            prompt << "  Parameters:\n";
            for (const auto& [name, schema] : tool.inputSchema["properties"].items()) {
                prompt << "    - " << name;
                if (schema.contains("description")) {
                    prompt << ": " << schema["description"].get<std::string>();
                }
                prompt << "\n";
            }
        }
        prompt << "\n";
    }

    prompt << "To call an MCP tool, use the format: serverName__toolName\n";
    prompt << "Example:\n";
    prompt << "<function_calls>\n";
    prompt << "<invoke name=\"filesystem__read_file\">\n";
    prompt << "<parameter name=\"path\">/path/to/file</parameter>\n";
    prompt << "</invoke>\n";
    prompt << "</function_calls>\n";

    return prompt.str();
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

void CLI::selectModel() {
    auto models = client_->listModels();
    if (models.empty()) {
        utils::terminal::printWarning("No models found. Make sure Ollama is running.");
        return;
    }

    std::string currentModel = model_override_.empty() ? config_->getModel() : model_override_;

    std::cout << utils::terminal::CYAN << utils::terminal::BOLD << "Select a model:" << utils::terminal::RESET << "\n\n";

    for (size_t i = 0; i < models.size(); i++) {
        bool isCurrent = (models[i] == currentModel);
        if (isCurrent) {
            std::cout << utils::terminal::GREEN << "  [" << (i + 1) << "] " << models[i] << " (current)" << utils::terminal::RESET << "\n";
        } else {
            std::cout << "  [" << (i + 1) << "] " << models[i] << "\n";
        }
    }

    std::cout << "\n" << utils::terminal::BOLD << "Enter number (or 'q' to cancel): " << utils::terminal::RESET;

    std::string input;
#ifdef HAVE_READLINE
    char* line = readline("");
    if (!line) return;
    input = line;
    free(line);
#else
    if (!std::getline(std::cin, input)) return;
#endif

    input = utils::trim(input);
    if (input.empty() || input == "q" || input == "Q") {
        std::cout << "Cancelled.\n\n";
        return;
    }

    try {
        int choice = std::stoi(input);
        if (choice < 1 || choice > static_cast<int>(models.size())) {
            utils::terminal::printError("Invalid selection. Please enter a number between 1 and " + std::to_string(models.size()));
            return;
        }

        std::string selectedModel = models[choice - 1];
        config_->setModel(selectedModel);
        model_override_.clear();  // Clear override so config model is used
        utils::terminal::printSuccess("Switched to model: " + selectedModel);
        std::cout << "\n";
    } catch (const std::exception&) {
        utils::terminal::printError("Invalid input. Please enter a number.");
    }
}

std::string CLI::getToolFormatPrompt() {
    return R"(
## Tool Usage Format

Use this EXACT XML format to call tools:

<function_calls>
<invoke name="TOOL_NAME">
<parameter name="param1">value1</parameter>
<parameter name="param2">value2</parameter>
</invoke>
</function_calls>

## Examples

User: "List my files"
Response:
<function_calls>
<invoke name="Bash">
<parameter name="command">ls -la</parameter>
<parameter name="description">List all files in current directory</parameter>
</invoke>
</function_calls>

User: "Read main.cpp"
Response:
<function_calls>
<invoke name="Read">
<parameter name="file_path">main.cpp</parameter>
</invoke>
</function_calls>

IMPORTANT RULES:
1. When a task requires tools, output the <function_calls> XML IMMEDIATELY without any preamble
2. You can add a brief explanation AFTER the tool call XML, not before
3. Never describe what tool you would use - just use it
4. After receiving tool results, provide a helpful summary to the user
)";
}

std::string CLI::getDefaultSystemPrompt() {
    return R"(You are an AI coding assistant with tool access. You MUST use tools to complete tasks - do NOT just describe what you would do.

CRITICAL: When the user asks you to do something that requires a tool, you MUST output the XML tool call IMMEDIATELY. Do NOT explain what you're going to do. Do NOT say "I propose to..." or "Here's what I'll do...". Just output the tool call XML directly.

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
)";
}

std::string CLI::getSystemPrompt() {
    std::string basePrompt;

    // Use agent-specific prompt if available
    if (!currentAgent_.systemPrompt.empty()) {
        basePrompt = currentAgent_.systemPrompt;
    } else {
        basePrompt = getDefaultSystemPrompt();
    }

    // Add tool format instructions
    basePrompt += getToolFormatPrompt();

    // Append MCP tools if available
    std::string mcpPrompt = getMCPToolsPrompt();
    if (!mcpPrompt.empty()) {
        basePrompt += mcpPrompt;
    }

    return basePrompt;
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
        // Debug: show if tool call format was detected but parsing failed
        if (response.find("<function_calls>") != std::string::npos ||
            response.find("<tool_calls>") != std::string::npos) {
            utils::terminal::printWarning("Tool call block detected but no valid tools parsed");
            std::cout << utils::terminal::YELLOW << "Raw response snippet:\n"
                      << response.substr(0, std::min(size_t(500), response.length()))
                      << "..." << utils::terminal::RESET << "\n\n";
        }
        return; // No tools to execute
    }

    // Debug: show parsed tool calls
    std::cout << utils::terminal::BLUE << "Parsed " << toolCalls.size() << " tool call(s):" << utils::terminal::RESET << "\n";
    for (size_t i = 0; i < toolCalls.size(); i++) {
        std::cout << utils::terminal::BLUE << "  [" << (i+1) << "] " << toolCalls[i].name << " - params: ";
        for (const auto& p : toolCalls[i].parameters) {
            std::cout << p.first << "=\"" << p.second.substr(0, 30);
            if (p.second.length() > 30) std::cout << "...";
            std::cout << "\" ";
        }
        std::cout << utils::terminal::RESET << "\n";
    }
    std::cout << "\n";

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
        // Debug: show if tool call format was detected but parsing failed
        if (response.find("<function_calls>") != std::string::npos ||
            response.find("<tool_calls>") != std::string::npos) {
            utils::terminal::printWarning("Tool call block detected but no valid tools parsed");
            std::cout << utils::terminal::YELLOW << "Raw response snippet:\n"
                      << response.substr(0, std::min(size_t(500), response.length()))
                      << "..." << utils::terminal::RESET << "\n\n";
        }
        return; // No tools to execute
    }

    // Filter tool calls based on current agent's allowed tools
    std::vector<ToolCall> filteredToolCalls;
    std::vector<ToolCall> blockedToolCalls;

    for (const auto& tool : toolCalls) {
        if (currentAgent_.canUseTool(tool.name)) {
            filteredToolCalls.push_back(tool);
        } else {
            blockedToolCalls.push_back(tool);
        }
    }

    // Report blocked tools
    if (!blockedToolCalls.empty()) {
        std::cout << utils::terminal::YELLOW << "⚠ Blocked " << blockedToolCalls.size()
                  << " tool(s) not available to " << currentAgent_.name << " agent:"
                  << utils::terminal::RESET << "\n";
        for (const auto& tool : blockedToolCalls) {
            std::cout << utils::terminal::YELLOW << "  - " << tool.name << utils::terminal::RESET << "\n";
        }
        std::cout << "\n";
    }

    if (filteredToolCalls.empty()) {
        std::cout << utils::terminal::YELLOW << "No executable tools for current agent."
                  << utils::terminal::RESET << "\n\n";
        return;
    }

    // Show tool selection menu (unless auto-approve is enabled)
    ToolSelectionResult selection;
    if (!config_->getAutoApprove()) {
        selection = task_suggester_->showToolSelectionMenu(filteredToolCalls, false);

        if (selection.cancelled) {
            std::cout << utils::terminal::YELLOW << "Tool execution cancelled."
                      << utils::terminal::RESET << "\n\n";
            return;
        }

        if (selection.skipAll) {
            std::cout << utils::terminal::YELLOW << "Skipped all tools."
                      << utils::terminal::RESET << "\n\n";
            return;
        }

        // Handle custom input (user wants to modify the request)
        if (!selection.customInput.empty()) {
            // Add the custom request to messages and get new response
            messages.push_back({
                {"role", "user"},
                {"content", selection.customInput}
            });

            std::string model = model_override_.empty() ? config_->getModel() : model_override_;
            double temp = temperature_override_ < 0 ? config_->getTemperature() : temperature_override_;

            auto newResponse = client_->chat(model, messages, temp, config_->getMaxTokens());
            if (newResponse.isSuccess()) {
                processResponseWithMessages(messages, newResponse.response, iteration);
            }
            return;
        }
    } else {
        // Auto-approve mode
        selection.executeAll = true;
        for (size_t i = 0; i < filteredToolCalls.size(); i++) {
            selection.selectedIndices.push_back(i);
        }
    }

    // Execute selected tools
    std::vector<ToolCall> toolsToExecute;
    if (selection.executeAll) {
        toolsToExecute = filteredToolCalls;
    } else {
        for (size_t idx : selection.selectedIndices) {
            if (idx < filteredToolCalls.size()) {
                toolsToExecute.push_back(filteredToolCalls[idx]);
            }
        }
    }

    std::cout << utils::terminal::CYAN << utils::terminal::BOLD
              << "🔧 Executing " << toolsToExecute.size() << " tool(s)..."
              << utils::terminal::RESET << "\n\n";

    auto results = executor_->executeAll(toolsToExecute);

    // Build results summary for next AI iteration
    std::ostringstream resultsSummary;
    resultsSummary << "Tool execution results:\n\n";

    for (size_t i = 0; i < toolsToExecute.size(); i++) {
        resultsSummary << "Tool: " << toolsToExecute[i].name << "\n";
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
    // Don't submit empty prompts to Ollama
    std::string trimmedPrompt = utils::trim(prompt);
    if (trimmedPrompt.empty()) {
        return;
    }

    auto start = std::chrono::steady_clock::now();

    json messages = buildMessages(trimmedPrompt);

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

    // Strip leading slash
    if (!cmd.empty() && cmd[0] == '/') {
        cmd = cmd.substr(1);
    }

    if (cmd == "help") {
        printHelp();
    } else if (cmd == "models") {
        printModels();
    } else if (cmd == "model") {
        selectModel();
    } else if (cmd == "config") {
        printConfig();
    } else if (cmd == "clear") {
        utils::terminal::clearScreen();
        printBanner();
    } else if (utils::startsWith(cmd, "use ")) {
        std::string model = cmd.substr(4);
        config_->setModel(model);
        utils::terminal::printSuccess("Switched to model: " + model);
    } else if (utils::startsWith(cmd, "host ")) {
        std::string host = utils::trim(cmd.substr(5));
        if (host.empty()) {
            utils::terminal::printInfo("Current host: " + config_->getOllamaHost());
        } else {
            // Ensure host has http:// prefix
            if (host.find("://") == std::string::npos) {
                host = "http://" + host;
            }
            config_->setOllamaHost(host);
            // Recreate the client with the new host
            client_ = std::make_unique<OllamaClient>(host);
            utils::terminal::printSuccess("Ollama host set to: " + host);
            // Test connection
            auto models = client_->listModels();
            if (models.empty()) {
                utils::terminal::printWarning("Warning: Could not connect to " + host);
            } else {
                utils::terminal::printSuccess("Connected! " + std::to_string(models.size()) + " models available.");
            }
        }
    } else if (cmd == "host") {
        utils::terminal::printInfo("Current host: " + config_->getOllamaHost());
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
    } else if (utils::startsWith(cmd, "mcp")) {
        handleMCPCommand(cmd);
    } else if (utils::startsWith(cmd, "agent") || cmd == "explore" || cmd == "code" ||
               cmd == "run" || cmd == "plan" || cmd == "general" ||
               cmd == "search" || cmd == "web" || cmd == "db" || cmd == "database" ||
               cmd == "learn" || cmd == "memory" || cmd == "rag") {
        handleAgentCommand(cmd);
    } else if (utils::startsWith(cmd, "model ") || cmd == "model create" ||
               cmd == "model show" || cmd == "model copy" || cmd == "model delete" ||
               cmd == "model pull" || cmd == "model push" || cmd == "model edit") {
        handleModelCommand(cmd);
    } else if (utils::startsWith(cmd, "prompt") || cmd == "prompts") {
        handlePromptCommand(cmd);
    } else if (utils::startsWith(cmd, "license")) {
        handleLicenseCommand(cmd);
    } else {
        utils::terminal::printError("Unknown command. Type '/help' for available commands.");
    }
}

// ============================================================================
// Model Command Handlers
// ============================================================================

void CLI::handleModelCommand(const std::string& cmd) {
    if (cmd == "model create") {
        // Interactive model creation
        auto builder = model_manager_->interactiveModelBuilder();
        if (builder.isValid()) {
            std::cout << "Enter name for the new model: ";
            std::string name;
            std::getline(std::cin, name);
            if (!name.empty()) {
                model_manager_->createModel(name, builder, [](const std::string& status) {
                    std::cout << "\r" << status << std::flush;
                });
            }
        }
    } else if (utils::startsWith(cmd, "model show ")) {
        std::string model_name = utils::trim(cmd.substr(11));
        model_manager_->printModelInfo(model_name);
    } else if (utils::startsWith(cmd, "model copy ")) {
        // Parse: model copy <src> <dst>
        std::string args = utils::trim(cmd.substr(11));
        size_t space = args.find(' ');
        if (space != std::string::npos) {
            std::string src = args.substr(0, space);
            std::string dst = utils::trim(args.substr(space + 1));
            model_manager_->copyModel(src, dst);
        } else {
            utils::terminal::printError("Usage: /model copy <source> <destination>");
        }
    } else if (utils::startsWith(cmd, "model delete ")) {
        std::string model_name = utils::trim(cmd.substr(13));
        std::cout << "Delete model '" << model_name << "'? (y/n): ";
        std::string confirm;
        std::getline(std::cin, confirm);
        if (confirm == "y" || confirm == "Y") {
            model_manager_->deleteModel(model_name);
        }
    } else if (utils::startsWith(cmd, "model pull ")) {
        std::string model_name = utils::trim(cmd.substr(11));
        model_manager_->pullModel(model_name);
    } else if (utils::startsWith(cmd, "model push ")) {
        std::string model_name = utils::trim(cmd.substr(11));
        model_manager_->pushModel(model_name);
    } else if (utils::startsWith(cmd, "model edit ")) {
        std::string model_name = utils::trim(cmd.substr(11));
        auto builder = model_manager_->getCustomModelBuilder(model_name);
        if (builder.isValid()) {
            // Re-run wizard with existing values
            auto new_builder = model_manager_->interactiveModelBuilder();
            if (new_builder.isValid()) {
                model_manager_->editModel(model_name, new_builder);
            }
        } else {
            utils::terminal::printError("Custom model not found: " + model_name);
        }
    } else {
        utils::terminal::printInfo("Model Commands:");
        std::cout << "  /model create           - Interactive model creation\n";
        std::cout << "  /model show <name>      - Show model details\n";
        std::cout << "  /model copy <src> <dst> - Clone a model\n";
        std::cout << "  /model delete <name>    - Delete a model\n";
        std::cout << "  /model pull <name>      - Download from Ollama\n";
        std::cout << "  /model push <name>      - Upload to Ollama.ai\n";
        std::cout << "  /model edit <name>      - Edit model parameters\n";
    }
}

// ============================================================================
// Prompt Command Handlers
// ============================================================================

void CLI::handlePromptCommand(const std::string& cmd) {
    if (cmd == "prompt" || cmd == "prompts") {
        // Show prompt selector
        std::string selected = prompt_db_->showPromptSelector();
        if (!selected.empty()) {
            utils::terminal::printSuccess("Selected prompt applied as context.");
            // The prompt content could be used as system message or context
        }
    } else if (cmd == "prompt add") {
        auto p = prompt_db_->showAddPromptDialog();
        if (!p.name.empty() && !p.content.empty()) {
            int64_t id = prompt_db_->addPrompt(p);
            if (id > 0) {
                utils::terminal::printSuccess("Prompt '" + p.name + "' added.");
            } else {
                utils::terminal::printError("Failed to add prompt.");
            }
        }
    } else if (utils::startsWith(cmd, "prompt edit ")) {
        std::string name = utils::trim(cmd.substr(12));
        if (prompt_db_->showEditPromptDialog(name)) {
            utils::terminal::printSuccess("Prompt updated.");
        }
    } else if (utils::startsWith(cmd, "prompt delete ")) {
        std::string name = utils::trim(cmd.substr(14));
        std::cout << "Delete prompt '" << name << "'? (y/n): ";
        std::string confirm;
        std::getline(std::cin, confirm);
        if (confirm == "y" || confirm == "Y") {
            if (prompt_db_->deletePromptByName(name)) {
                utils::terminal::printSuccess("Prompt deleted.");
            } else {
                utils::terminal::printError("Failed to delete prompt.");
            }
        }
    } else if (cmd == "prompt list" || utils::startsWith(cmd, "prompt list ")) {
        std::string category;
        if (utils::startsWith(cmd, "prompt list ")) {
            category = utils::trim(cmd.substr(12));
        }

        std::vector<Prompt> prompts;
        if (category.empty()) {
            prompts = prompt_db_->getAllPrompts();
        } else {
            prompts = prompt_db_->getPromptsByCategory(category);
        }

        if (prompts.empty()) {
            utils::terminal::printInfo("No prompts found.");
        } else {
            std::cout << "\n" << utils::terminal::BOLD << "Prompts" << utils::terminal::RESET << "\n";
            std::cout << std::string(50, '-') << "\n";
            for (const auto& p : prompts) {
                std::cout << "  ";
                if (p.is_favorite) std::cout << utils::terminal::YELLOW << "* " << utils::terminal::RESET;
                std::cout << utils::terminal::GREEN << p.name << utils::terminal::RESET;
                std::cout << " [" << p.category << "]";
                if (!p.description.empty()) {
                    std::string desc = p.description;
                    if (desc.length() > 30) desc = desc.substr(0, 30) + "...";
                    std::cout << " - " << desc;
                }
                std::cout << "\n";
            }
            std::cout << "\n";
        }
    } else if (utils::startsWith(cmd, "prompt search ")) {
        std::string query = utils::trim(cmd.substr(14));
        auto prompts = prompt_db_->searchPrompts(query);
        if (prompts.empty()) {
            utils::terminal::printInfo("No prompts matching '" + query + "'");
        } else {
            std::cout << "Found " << prompts.size() << " prompts:\n";
            for (const auto& p : prompts) {
                std::cout << "  - " << p.name << " [" << p.category << "]\n";
            }
        }
    } else if (utils::startsWith(cmd, "prompt use ")) {
        std::string name = utils::trim(cmd.substr(11));
        auto p = prompt_db_->getPromptByName(name);
        if (p.id > 0) {
            prompt_db_->incrementUsageCount(p.id);
            utils::terminal::printSuccess("Prompt '" + name + "' content:");
            std::cout << utils::terminal::CYAN << p.content << utils::terminal::RESET << "\n";
        } else {
            utils::terminal::printError("Prompt not found: " + name);
        }
    } else if (utils::startsWith(cmd, "prompt export ")) {
        std::string file_path = utils::trim(cmd.substr(14));
        if (file_path.empty()) {
            file_path = "prompts.json";
        }
        prompt_db_->exportToJson(file_path);
    } else if (utils::startsWith(cmd, "prompt import ")) {
        std::string file_path = utils::trim(cmd.substr(14));
        prompt_db_->importFromJson(file_path);
    } else if (utils::startsWith(cmd, "prompt favorite ")) {
        std::string name = utils::trim(cmd.substr(16));
        auto p = prompt_db_->getPromptByName(name);
        if (p.id > 0) {
            prompt_db_->toggleFavorite(p.id);
            utils::terminal::printSuccess("Favorite toggled for '" + name + "'");
        } else {
            utils::terminal::printError("Prompt not found: " + name);
        }
    } else if (cmd == "prompt categories") {
        auto categories = prompt_db_->getCategories();
        std::cout << "\nCategories:\n";
        for (const auto& c : categories) {
            std::cout << "  - " << c.name;
            if (!c.description.empty()) std::cout << " (" << c.description << ")";
            std::cout << "\n";
        }
    } else {
        utils::terminal::printInfo("Prompt Commands:");
        std::cout << "  /prompt                  - Interactive prompt selector\n";
        std::cout << "  /prompt add              - Add new prompt\n";
        std::cout << "  /prompt edit <name>      - Edit prompt\n";
        std::cout << "  /prompt delete <name>    - Delete prompt\n";
        std::cout << "  /prompt list [category]  - List prompts\n";
        std::cout << "  /prompt search <query>   - Search prompts\n";
        std::cout << "  /prompt use <name>       - Show prompt content\n";
        std::cout << "  /prompt export <file>    - Export to JSON\n";
        std::cout << "  /prompt import <file>    - Import from JSON\n";
        std::cout << "  /prompt favorite <name>  - Toggle favorite\n";
        std::cout << "  /prompt categories       - List categories\n";
    }
}

// ============================================================================
// License Command Handlers
// ============================================================================

void CLI::handleLicenseCommand(const std::string& cmd) {
    if (cmd == "license") {
        license_manager_->showLicenseStatus();
    } else if (utils::startsWith(cmd, "license activate ")) {
        std::string key = utils::trim(cmd.substr(17));
        if (license_manager_->activateKey(key)) {
            utils::terminal::printSuccess("License activated successfully!");
            license_manager_->showLicenseStatus();
        } else {
            auto info = license_manager_->getLicenseInfo();
            utils::terminal::printError("Activation failed: " + info.error_message);
        }
    } else if (cmd == "license deactivate") {
        std::cout << "Deactivate license? (y/n): ";
        std::string confirm;
        std::getline(std::cin, confirm);
        if (confirm == "y" || confirm == "Y") {
            license_manager_->deactivateKey();
            utils::terminal::printSuccess("License deactivated.");
        }
    } else if (cmd == "license hwid") {
        std::cout << "Hardware ID: " << license_manager_->getHardwareId() << "\n";
    } else {
        utils::terminal::printInfo("License Commands:");
        std::cout << "  /license               - Show license status\n";
        std::cout << "  /license activate <key> - Activate license key\n";
        std::cout << "  /license deactivate    - Remove license\n";
        std::cout << "  /license hwid          - Show hardware ID\n";
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
        std::cout.flush();

        std::string input;

        // Check first character to see if user wants command menu
        // We need to read the first character without echo to detect "/"
        struct termios oldt, newt;
        tcgetattr(STDIN_FILENO, &oldt);
        newt = oldt;
        newt.c_lflag &= ~(ICANON | ECHO);
        newt.c_cc[VMIN] = 1;
        newt.c_cc[VTIME] = 0;
        tcsetattr(STDIN_FILENO, TCSANOW, &newt);

        char first_char;
        if (read(STDIN_FILENO, &first_char, 1) != 1) {
            tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
            break;
        }

        // Restore terminal for remaining input
        tcsetattr(STDIN_FILENO, TCSANOW, &oldt);

        if (first_char == '/') {
            // Show command menu
            // First, erase the prompt line to redraw with menu
            std::cout << "\r\033[K";  // Clear line
            std::cout.flush();

            auto result = command_menu_->show("/");

            if (result.cancelled) {
                // User cancelled, show prompt again
                std::cout << "\r\033[K";
                continue;
            }

            input = result.command;

            // Add to readline history if available
#ifdef HAVE_READLINE
            if (!input.empty()) {
                add_history(input.c_str());
            }
#endif
        } else if (first_char == '\n') {
            // Empty line
            continue;
        } else if (first_char == 4) {  // Ctrl+D
            std::cout << "\n";
            break;
        } else {
            // Regular input - echo first char and continue with readline or getline
            std::cout << first_char;
            std::cout.flush();

#ifdef HAVE_READLINE
            // Use readline for the rest
            char* line = readline("");
            if (!line) break;
            input = first_char + std::string(line);
            if (!input.empty()) {
                add_history(input.c_str());
            }
            free(line);
#else
            std::string rest;
            if (!std::getline(std::cin, rest)) break;
            input = first_char + rest;
#endif
        }

        input = utils::trim(input);
        if (input.empty()) continue;

        if (input == "/exit" || input == "/quit") {
            std::cout << utils::terminal::GREEN << "👋 Goodbye!" << utils::terminal::RESET << "\n";
            break;
        }

        // Check if it's a command (starts with /)
        if (!input.empty() && input[0] == '/') {
            handleCommand(input);
            continue;
        }

        // Process as AI prompt
        std::cout << "\n";

        // Use agent selection flow if enabled, otherwise direct execution
        if (agentModeEnabled_) {
            processWithAgentSelection(input);
        } else {
            // Direct execution with current agent
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
}

int CLI::run() {
    // Test Ollama connection
    if (!client_->testConnection()) {
        utils::terminal::printError("Failed to connect to Ollama at " + config_->getOllamaHost());
        utils::terminal::printWarning("Make sure Ollama is running with: ollama serve");
        return 1;
    }

    // Initialize MCP if enabled
    initializeMCP();

    // Single prompt mode or interactive mode
    if (!direct_prompt_.empty()) {
        singlePromptMode(direct_prompt_);
        return 0;
    }

    // Interactive mode
    interactiveMode();
    return 0;
}

} // namespace oleg
