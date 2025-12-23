#ifndef CASPER_AGENT_H
#define CASPER_AGENT_H

#include <string>
#include <vector>
#include <unordered_set>
#include <functional>

namespace casper {

enum class AgentType {
    General,    // Default - all tools
    Explorer,   // Read-only exploration
    Coder,      // Code modification
    Runner,     // Command execution
    Planner,    // Planning without execution
    Searcher,   // Web search and research
    Database,   // Database queries and analysis
    Learner,    // RAG learning and retrieval
    Network     // Network diagnostics and tools
};

struct Agent {
    AgentType type;
    std::string name;
    std::string icon;
    std::string description;
    std::string systemPrompt;
    std::unordered_set<std::string> allowedTools;
    float temperatureOverride;  // -1 = use default

    bool canUseTool(const std::string& tool) const {
        return allowedTools.empty() || allowedTools.count(tool) > 0;
    }

    std::string getDisplayName() const {
        return icon + " " + name;
    }
};

struct TaskSuggestion {
    AgentType agentType;
    std::string taskDescription;
    std::string reasoning;
    int priority;  // 1 = highest
};

// Selection menu result
struct AgentSelectionResult {
    bool cancelled;
    bool executeAll;
    AgentType selectedAgent;
    std::string customInput;
    std::vector<TaskSuggestion> selectedTasks;
};

// Tool execution selection
struct ToolSelectionResult {
    bool cancelled;
    bool executeAll;
    bool skipAll;
    std::vector<size_t> selectedIndices;
    std::string customInput;
};

class AgentRegistry {
public:
    static Agent getAgent(AgentType type);
    static std::vector<Agent> getAllAgents();
    static AgentType parseAgentName(const std::string& name);

private:
    static Agent getExplorerAgent();
    static Agent getCoderAgent();
    static Agent getRunnerAgent();
    static Agent getPlannerAgent();
    static Agent getGeneralAgent();
    static Agent getSearcherAgent();
    static Agent getDatabaseAgent();
    static Agent getLearnerAgent();
    static Agent getNetworkAgent();
};

} // namespace casper

#endif // CASPER_AGENT_H
