#ifndef OLLAMACODE_TASK_SUGGESTER_H
#define OLLAMACODE_TASK_SUGGESTER_H

#include "agent.h"
#include "tool_parser.h"
#include <string>
#include <vector>
#include <functional>

namespace ollamacode {

// Callback for status updates
using StatusCallback = std::function<void(const std::string&)>;

class TaskSuggester {
public:
    TaskSuggester();

    // Analyze user input and suggest agents/tasks
    std::vector<TaskSuggestion> analyzeTask(const std::string& userInput);

    // Show interactive selection menu for agent suggestions
    AgentSelectionResult showAgentSelectionMenu(
        const std::vector<TaskSuggestion>& suggestions,
        const std::string& originalInput
    );

    // Show interactive selection menu for tool execution
    ToolSelectionResult showToolSelectionMenu(
        const std::vector<ToolCall>& toolCalls,
        bool autoApprove = false
    );

    // Set status callback
    void setStatusCallback(StatusCallback callback);

private:
    StatusCallback statusCallback_;

    // Pattern matching for task analysis
    bool containsExplorePatterns(const std::string& input);
    bool containsCodePatterns(const std::string& input);
    bool containsRunPatterns(const std::string& input);
    bool containsPlanPatterns(const std::string& input);
    bool containsSearchPatterns(const std::string& input);
    bool containsDatabasePatterns(const std::string& input);
    bool containsLearnerPatterns(const std::string& input);

    // Menu helpers
    std::string readSingleKey();
    void printSelectionMenu(
        const std::vector<std::string>& options,
        int selectedIndex,
        const std::string& title
    );
};

} // namespace ollamacode

#endif // OLLAMACODE_TASK_SUGGESTER_H
