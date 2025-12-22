#include "task_suggester.h"
#include "utils.h"
#include <iostream>
#include <sstream>
#include <algorithm>
#include <termios.h>
#include <unistd.h>
#include <regex>

namespace ollamacode {

TaskSuggester::TaskSuggester() : statusCallback_(nullptr) {}

void TaskSuggester::setStatusCallback(StatusCallback callback) {
    statusCallback_ = callback;
}

bool TaskSuggester::containsExplorePatterns(const std::string& input) {
    std::string lower = input;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    std::vector<std::string> patterns = {
        "find", "search", "where", "look for", "locate",
        "what is", "what are", "how does", "how do",
        "explain", "understand", "show me", "list",
        "which files", "what files", "explore", "analyze"
    };

    for (const auto& pattern : patterns) {
        if (lower.find(pattern) != std::string::npos) {
            return true;
        }
    }
    return false;
}

bool TaskSuggester::containsCodePatterns(const std::string& input) {
    std::string lower = input;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    std::vector<std::string> patterns = {
        "add", "create", "write", "implement", "fix",
        "change", "modify", "update", "edit", "refactor",
        "remove", "delete", "rename", "move", "replace"
    };

    for (const auto& pattern : patterns) {
        if (lower.find(pattern) != std::string::npos) {
            return true;
        }
    }
    return false;
}

bool TaskSuggester::containsRunPatterns(const std::string& input) {
    std::string lower = input;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    std::vector<std::string> patterns = {
        "run", "execute", "test", "build", "compile",
        "install", "start", "stop", "restart", "deploy",
        "npm", "yarn", "make", "cmake", "cargo", "go build"
    };

    for (const auto& pattern : patterns) {
        if (lower.find(pattern) != std::string::npos) {
            return true;
        }
    }
    return false;
}

bool TaskSuggester::containsPlanPatterns(const std::string& input) {
    std::string lower = input;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    std::vector<std::string> patterns = {
        "plan", "design", "architect", "strategy",
        "how should", "what's the best way", "approach",
        "step by step", "roadmap", "outline"
    };

    for (const auto& pattern : patterns) {
        if (lower.find(pattern) != std::string::npos) {
            return true;
        }
    }
    return false;
}

bool TaskSuggester::containsSearchPatterns(const std::string& input) {
    std::string lower = input;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    std::vector<std::string> patterns = {
        "search the web", "google", "look up online", "web search",
        "fetch url", "download page", "scrape", "crawl",
        "find online", "search internet", "research online",
        "duckduckgo", "brave search", "http://", "https://"
    };

    for (const auto& pattern : patterns) {
        if (lower.find(pattern) != std::string::npos) {
            return true;
        }
    }
    return false;
}

bool TaskSuggester::containsDatabasePatterns(const std::string& input) {
    std::string lower = input;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    std::vector<std::string> patterns = {
        "database", "sql", "query", "select from", "insert into",
        "table", "schema", "postgresql", "postgres", "mysql",
        "sqlite", "db connect", "db query", "rows", "columns"
    };

    for (const auto& pattern : patterns) {
        if (lower.find(pattern) != std::string::npos) {
            return true;
        }
    }
    return false;
}

bool TaskSuggester::containsLearnerPatterns(const std::string& input) {
    std::string lower = input;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    std::vector<std::string> patterns = {
        "learn", "remember", "memorize", "index", "vector",
        "rag", "embed", "knowledge base", "context", "forget",
        "teach", "train on", "study", "recall", "memory"
    };

    for (const auto& pattern : patterns) {
        if (lower.find(pattern) != std::string::npos) {
            return true;
        }
    }
    return false;
}

std::vector<TaskSuggestion> TaskSuggester::analyzeTask(const std::string& userInput) {
    std::vector<TaskSuggestion> suggestions;

    // Analyze patterns and create suggestions
    bool hasExplore = containsExplorePatterns(userInput);
    bool hasCode = containsCodePatterns(userInput);
    bool hasRun = containsRunPatterns(userInput);
    bool hasPlan = containsPlanPatterns(userInput);
    bool hasSearch = containsSearchPatterns(userInput);
    bool hasDatabase = containsDatabasePatterns(userInput);
    bool hasLearner = containsLearnerPatterns(userInput);

    int priority = 1;

    // Web search tasks
    if (hasSearch) {
        TaskSuggestion searchSuggestion;
        searchSuggestion.agentType = AgentType::Searcher;
        searchSuggestion.taskDescription = userInput;
        searchSuggestion.reasoning = "Search the web and gather information";
        searchSuggestion.priority = priority++;
        suggestions.push_back(searchSuggestion);
    }

    // Database tasks
    if (hasDatabase) {
        TaskSuggestion dbSuggestion;
        dbSuggestion.agentType = AgentType::Database;
        dbSuggestion.taskDescription = userInput;
        dbSuggestion.reasoning = "Query and analyze database";
        dbSuggestion.priority = priority++;
        suggestions.push_back(dbSuggestion);
    }

    // Learning/RAG tasks
    if (hasLearner) {
        TaskSuggestion learnSuggestion;
        learnSuggestion.agentType = AgentType::Learner;
        learnSuggestion.taskDescription = userInput;
        learnSuggestion.reasoning = "Learn from documents or retrieve knowledge";
        learnSuggestion.priority = priority++;
        suggestions.push_back(learnSuggestion);
    }

    // Complex tasks might need planning first
    if (hasPlan || (hasCode && hasExplore)) {
        TaskSuggestion planSuggestion;
        planSuggestion.agentType = AgentType::Planner;
        planSuggestion.taskDescription = "Create a plan for: " + userInput;
        planSuggestion.reasoning = "Complex task - planning first helps break it down";
        planSuggestion.priority = priority++;
        suggestions.push_back(planSuggestion);
    }

    // Code changes often need exploration first
    if (hasCode && !hasPlan) {
        if (hasExplore || userInput.length() > 50) {
            TaskSuggestion exploreSuggestion;
            exploreSuggestion.agentType = AgentType::Explorer;
            exploreSuggestion.taskDescription = "Find relevant files for: " + userInput;
            exploreSuggestion.reasoning = "Explore codebase first to understand structure";
            exploreSuggestion.priority = priority++;
            suggestions.push_back(exploreSuggestion);
        }

        TaskSuggestion codeSuggestion;
        codeSuggestion.agentType = AgentType::Coder;
        codeSuggestion.taskDescription = userInput;
        codeSuggestion.reasoning = "Make code changes";
        codeSuggestion.priority = priority++;
        suggestions.push_back(codeSuggestion);
    }

    // Pure exploration
    if (hasExplore && !hasCode) {
        TaskSuggestion exploreSuggestion;
        exploreSuggestion.agentType = AgentType::Explorer;
        exploreSuggestion.taskDescription = userInput;
        exploreSuggestion.reasoning = "Read-only exploration of codebase";
        exploreSuggestion.priority = priority++;
        suggestions.push_back(exploreSuggestion);
    }

    // Running commands/tests
    if (hasRun) {
        TaskSuggestion runSuggestion;
        runSuggestion.agentType = AgentType::Runner;
        runSuggestion.taskDescription = userInput;
        runSuggestion.reasoning = "Execute commands or run tests";
        runSuggestion.priority = priority++;
        suggestions.push_back(runSuggestion);
    }

    // Default to general agent if no specific patterns matched
    if (suggestions.empty()) {
        TaskSuggestion generalSuggestion;
        generalSuggestion.agentType = AgentType::General;
        generalSuggestion.taskDescription = userInput;
        generalSuggestion.reasoning = "General task with all tools available";
        generalSuggestion.priority = 1;
        suggestions.push_back(generalSuggestion);
    }

    return suggestions;
}

std::string TaskSuggester::readSingleKey() {
    struct termios oldt, newt;
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);
    newt.c_cc[VMIN] = 1;
    newt.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);

    std::string result;
    char c;
    if (read(STDIN_FILENO, &c, 1) == 1) {
        result += c;
        // Check for escape sequences (arrow keys)
        if (c == 27) {  // ESC
            char seq[2];
            if (read(STDIN_FILENO, &seq[0], 1) == 1 && seq[0] == '[') {
                if (read(STDIN_FILENO, &seq[1], 1) == 1) {
                    result = std::string("\x1b[") + seq[1];
                }
            }
        }
    }

    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    return result;
}

void TaskSuggester::printSelectionMenu(
    const std::vector<std::string>& options,
    int selectedIndex,
    const std::string& title
) {
    // Move cursor up to overwrite previous menu
    std::cout << "\033[" << (options.size() + 3) << "A";
    std::cout << "\033[J";  // Clear from cursor to end

    std::cout << utils::terminal::CYAN << utils::terminal::BOLD
              << title << utils::terminal::RESET << "\n\n";

    for (size_t i = 0; i < options.size(); i++) {
        if (static_cast<int>(i) == selectedIndex) {
            std::cout << utils::terminal::GREEN << utils::terminal::BOLD
                      << " > " << options[i] << utils::terminal::RESET << "\n";
        } else {
            std::cout << "   " << options[i] << "\n";
        }
    }

    std::cout << "\n" << utils::terminal::YELLOW
              << "(Use arrow keys to select, Enter to confirm, Esc to cancel)"
              << utils::terminal::RESET << std::flush;
}

AgentSelectionResult TaskSuggester::showAgentSelectionMenu(
    const std::vector<TaskSuggestion>& suggestions,
    const std::string& originalInput
) {
    AgentSelectionResult result;
    result.cancelled = false;
    result.executeAll = false;
    result.selectedAgent = AgentType::General;

    if (suggestions.empty()) {
        result.selectedAgent = AgentType::General;
        return result;
    }

    // Build menu options
    std::vector<std::string> options;
    std::vector<AgentType> agentTypes;

    for (const auto& suggestion : suggestions) {
        Agent agent = AgentRegistry::getAgent(suggestion.agentType);
        std::ostringstream opt;
        opt << agent.getDisplayName() << " - " << suggestion.reasoning;
        options.push_back(opt.str());
        agentTypes.push_back(suggestion.agentType);
    }

    // Add special options
    if (suggestions.size() > 1) {
        options.push_back("\xF0\x9F\x9A\x80 Execute all suggested tasks in sequence");
        agentTypes.push_back(AgentType::General);  // Placeholder
    }
    options.push_back("\xF0\x9F\xA4\x96 Use general agent (all tools)");
    agentTypes.push_back(AgentType::General);
    options.push_back("\xE2\x9C\x8F\xEF\xB8\x8F  Enter custom instruction");
    agentTypes.push_back(AgentType::General);  // Placeholder

    int selectedIndex = 0;
    int executeAllIndex = suggestions.size() > 1 ? static_cast<int>(suggestions.size()) : -1;
    int generalIndex = suggestions.size() > 1 ? static_cast<int>(suggestions.size() + 1) : static_cast<int>(suggestions.size());
    int customIndex = static_cast<int>(options.size() - 1);

    // Print initial menu
    std::cout << "\n";
    for (size_t i = 0; i < options.size() + 3; i++) std::cout << "\n";

    printSelectionMenu(options, selectedIndex, "Select an approach:");

    while (true) {
        std::string key = readSingleKey();

        if (key == "\x1b[A") {  // Up arrow
            selectedIndex = (selectedIndex - 1 + options.size()) % options.size();
            printSelectionMenu(options, selectedIndex, "Select an approach:");
        } else if (key == "\x1b[B") {  // Down arrow
            selectedIndex = (selectedIndex + 1) % options.size();
            printSelectionMenu(options, selectedIndex, "Select an approach:");
        } else if (key == "\n" || key == "\r") {  // Enter
            std::cout << "\n\n";

            if (executeAllIndex >= 0 && selectedIndex == executeAllIndex) {
                result.executeAll = true;
                result.selectedTasks = suggestions;
                return result;
            } else if (selectedIndex == customIndex) {
                // Custom input
                std::cout << utils::terminal::CYAN << "Enter your instruction: "
                          << utils::terminal::RESET;
                std::string customInput;
                std::getline(std::cin, customInput);
                result.customInput = customInput;
                result.selectedAgent = AgentType::General;
                return result;
            } else if (selectedIndex == generalIndex) {
                result.selectedAgent = AgentType::General;
                return result;
            } else if (selectedIndex < static_cast<int>(suggestions.size())) {
                result.selectedAgent = suggestions[selectedIndex].agentType;
                result.selectedTasks.push_back(suggestions[selectedIndex]);
                return result;
            }
        } else if (key[0] == 27 || key[0] == 'q' || key[0] == 'Q') {  // Escape or q
            std::cout << "\n\n";
            result.cancelled = true;
            return result;
        } else if (key[0] >= '1' && key[0] <= '9') {
            // Number key selection
            int idx = key[0] - '1';
            if (idx < static_cast<int>(options.size())) {
                selectedIndex = idx;
                printSelectionMenu(options, selectedIndex, "Select an approach:");
            }
        }
    }
}

ToolSelectionResult TaskSuggester::showToolSelectionMenu(
    const std::vector<ToolCall>& toolCalls,
    bool autoApprove
) {
    ToolSelectionResult result;
    result.cancelled = false;
    result.executeAll = false;
    result.skipAll = false;

    if (toolCalls.empty()) {
        return result;
    }

    // Auto-approve mode
    if (autoApprove) {
        result.executeAll = true;
        for (size_t i = 0; i < toolCalls.size(); i++) {
            result.selectedIndices.push_back(i);
        }
        return result;
    }

    // Build menu options
    std::vector<std::string> options;

    for (size_t i = 0; i < toolCalls.size(); i++) {
        const auto& tool = toolCalls[i];
        std::ostringstream opt;
        opt << "\xF0\x9F\x94\xA7 " << tool.name;

        // Add brief parameter info
        if (tool.parameters.count("command")) {
            std::string cmd = tool.parameters.at("command");
            if (cmd.length() > 40) cmd = cmd.substr(0, 37) + "...";
            opt << ": " << cmd;
        } else if (tool.parameters.count("file_path")) {
            opt << ": " << tool.parameters.at("file_path");
        } else if (tool.parameters.count("pattern")) {
            opt << ": " << tool.parameters.at("pattern");
        }

        options.push_back(opt.str());
    }

    // Add action options
    options.push_back("\xE2\x9C\x85 Execute all tools");
    options.push_back("\xE2\x9D\x8C Skip all tools");
    options.push_back("\xE2\x9C\x8F\xEF\xB8\x8F  Modify request");

    int selectedIndex = static_cast<int>(toolCalls.size());  // Default to "Execute all"
    int executeAllIndex = static_cast<int>(toolCalls.size());
    int skipAllIndex = static_cast<int>(toolCalls.size() + 1);
    int modifyIndex = static_cast<int>(toolCalls.size() + 2);

    // Print header
    std::cout << "\n" << utils::terminal::CYAN << utils::terminal::BOLD
              << "The AI wants to execute " << toolCalls.size() << " tool(s):"
              << utils::terminal::RESET << "\n\n";

    // Print tool details
    for (size_t i = 0; i < toolCalls.size(); i++) {
        const auto& tool = toolCalls[i];
        std::cout << utils::terminal::YELLOW << "  [" << (i + 1) << "] "
                  << tool.name << utils::terminal::RESET << "\n";

        for (const auto& param : tool.parameters) {
            std::string value = param.second;
            if (value.length() > 60) {
                value = value.substr(0, 57) + "...";
            }
            // Replace newlines for display
            std::replace(value.begin(), value.end(), '\n', ' ');
            std::cout << "      " << param.first << ": " << value << "\n";
        }
        std::cout << "\n";
    }

    // Print initial menu
    std::cout << "\n";
    for (size_t i = 0; i < 6; i++) std::cout << "\n";

    // Custom menu for tool selection
    auto printToolMenu = [&]() {
        std::cout << "\033[6A\033[J";

        std::cout << utils::terminal::CYAN << "What would you like to do?"
                  << utils::terminal::RESET << "\n\n";

        std::vector<std::string> actionOptions = {
            "\xE2\x9C\x85 Execute all tools",
            "\xE2\x9D\x8C Skip all tools",
            "\xE2\x9C\x8F\xEF\xB8\x8F  Modify request"
        };

        for (size_t i = 0; i < actionOptions.size(); i++) {
            int actualIndex = static_cast<int>(toolCalls.size() + i);
            if (actualIndex == selectedIndex) {
                std::cout << utils::terminal::GREEN << utils::terminal::BOLD
                          << " > " << actionOptions[i] << utils::terminal::RESET << "\n";
            } else {
                std::cout << "   " << actionOptions[i] << "\n";
            }
        }

        std::cout << "\n" << utils::terminal::YELLOW
                  << "(Enter to confirm, Esc to cancel)"
                  << utils::terminal::RESET << std::flush;
    };

    printToolMenu();

    while (true) {
        std::string key = readSingleKey();

        if (key == "\x1b[A") {  // Up arrow
            if (selectedIndex > executeAllIndex) {
                selectedIndex--;
                printToolMenu();
            }
        } else if (key == "\x1b[B") {  // Down arrow
            if (selectedIndex < modifyIndex) {
                selectedIndex++;
                printToolMenu();
            }
        } else if (key == "\n" || key == "\r") {  // Enter
            std::cout << "\n\n";

            if (selectedIndex == executeAllIndex) {
                result.executeAll = true;
                for (size_t i = 0; i < toolCalls.size(); i++) {
                    result.selectedIndices.push_back(i);
                }
                return result;
            } else if (selectedIndex == skipAllIndex) {
                result.skipAll = true;
                return result;
            } else if (selectedIndex == modifyIndex) {
                std::cout << utils::terminal::CYAN << "Enter modified instruction: "
                          << utils::terminal::RESET;
                std::string customInput;
                std::getline(std::cin, customInput);
                result.customInput = customInput;
                return result;
            }
        } else if (key[0] == 27 || key[0] == 'q') {  // Escape
            std::cout << "\n\n";
            result.cancelled = true;
            return result;
        } else if (key == "y" || key == "Y") {
            // Quick yes
            std::cout << "\n\n";
            result.executeAll = true;
            for (size_t i = 0; i < toolCalls.size(); i++) {
                result.selectedIndices.push_back(i);
            }
            return result;
        } else if (key == "n" || key == "N") {
            // Quick no
            std::cout << "\n\n";
            result.skipAll = true;
            return result;
        }
    }
}

} // namespace ollamacode
