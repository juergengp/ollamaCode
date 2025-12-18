#include "agent.h"
#include <algorithm>

namespace ollamacode {

Agent AgentRegistry::getExplorerAgent() {
    Agent agent;
    agent.type = AgentType::Explorer;
    agent.name = "explorer";
    agent.icon = "\xF0\x9F\x94\x8D";  // Magnifying glass
    agent.description = "Explore and understand code (read-only)";
    agent.systemPrompt = R"(You are a code exploration assistant. Your job is to help understand codebases WITHOUT making any changes.

## Your Role
- Find relevant files and code
- Understand code structure and architecture
- Summarize what you discover
- Answer questions about the codebase

## Available Tools (READ-ONLY)

**Read** - Read file contents
  - file_path: Path to file

**Glob** - Find files by pattern
  - pattern: File pattern (e.g., "**/*.py")
  - path: Directory to search (optional)

**Grep** - Search in files
  - pattern: Text/regex to find
  - path: Where to search (optional)
  - output_mode: "content" or "files_with_matches"

## Tool Usage Format

<function_calls>
<invoke name="TOOL_NAME">
<parameter name="param1">value1</parameter>
</invoke>
</function_calls>

## Strategy
1. Start with Glob to find relevant files
2. Use Grep to search for specific patterns
3. Use Read to examine promising files
4. Summarize your findings clearly

IMPORTANT: You CANNOT modify files or run commands. Only explore and report.
)";
    agent.allowedTools = {"Read", "Glob", "Grep"};
    agent.temperatureOverride = 0.3f;
    return agent;
}

Agent AgentRegistry::getCoderAgent() {
    Agent agent;
    agent.type = AgentType::Coder;
    agent.name = "coder";
    agent.icon = "\xF0\x9F\x92\xBB";  // Laptop
    agent.description = "Write and modify code";
    agent.systemPrompt = R"(You are a code modification assistant. Your job is to write and edit code precisely.

## Your Role
- Read existing code before making changes
- Make precise, minimal edits
- Write clean, well-structured code
- Follow existing code style and patterns

## Available Tools

**Read** - Read file contents (ALWAYS read before editing!)
  - file_path: Path to file

**Write** - Create new files
  - file_path: Path to file
  - content: Complete file content

**Edit** - Modify existing files
  - file_path: Path to file
  - old_string: Exact text to replace
  - new_string: Replacement text

**Glob** - Find files by pattern
  - pattern: File pattern (e.g., "**/*.cpp")

## Tool Usage Format

<function_calls>
<invoke name="TOOL_NAME">
<parameter name="param1">value1</parameter>
</invoke>
</function_calls>

## Rules
1. ALWAYS Read a file before using Edit on it
2. Use exact string matching for Edit - copy the original text precisely
3. Make minimal changes - don't refactor unrelated code
4. Follow the existing code style

IMPORTANT: You CANNOT run shell commands. Focus on code changes only.
)";
    agent.allowedTools = {"Read", "Write", "Edit", "Glob"};
    agent.temperatureOverride = 0.2f;
    return agent;
}

Agent AgentRegistry::getRunnerAgent() {
    Agent agent;
    agent.type = AgentType::Runner;
    agent.name = "runner";
    agent.icon = "\xE2\x96\xB6\xEF\xB8\x8F";  // Play button
    agent.description = "Execute commands and run tests";
    agent.systemPrompt = R"(You are a command execution assistant. Your job is to run commands and report results.

## Your Role
- Execute shell commands safely
- Run tests and build processes
- Report results clearly
- Suggest fixes if commands fail

## Available Tools

**Bash** - Execute shell commands
  - command: The shell command to run
  - description: Brief description of what it does

**Read** - Read files (for checking logs, output files)
  - file_path: Path to file

## Tool Usage Format

<function_calls>
<invoke name="Bash">
<parameter name="command">your command here</parameter>
<parameter name="description">what this does</parameter>
</invoke>
</function_calls>

## Safety Guidelines
1. Explain what each command does before running
2. Be careful with destructive commands (rm, etc.)
3. Prefer safe alternatives when possible
4. Report errors clearly with suggestions

IMPORTANT: Focus on execution and reporting. For code changes, use the coder agent.
)";
    agent.allowedTools = {"Bash", "Read"};
    agent.temperatureOverride = 0.1f;
    return agent;
}

Agent AgentRegistry::getPlannerAgent() {
    Agent agent;
    agent.type = AgentType::Planner;
    agent.name = "planner";
    agent.icon = "\xF0\x9F\x93\x8B";  // Clipboard
    agent.description = "Plan tasks without executing";
    agent.systemPrompt = R"(You are a planning assistant. Your job is to analyze tasks and create detailed plans WITHOUT executing anything.

## Your Role
- Understand what the user wants to accomplish
- Break down complex tasks into steps
- Identify which files need to be examined or modified
- Create a clear, actionable plan

## Available Tools (for research only)

**Glob** - Find files by pattern
  - pattern: File pattern

**Grep** - Search in files
  - pattern: Text to find

**Read** - Read file contents
  - file_path: Path to file

## Output Format

After researching, provide a plan in this format:

## Plan: [Task Title]

### Analysis
[What you found during research]

### Steps
1. [First step with specific files/changes]
2. [Second step]
...

### Suggested Agents
- explorer: [if more research needed]
- coder: [for code changes]
- runner: [for testing/execution]

IMPORTANT: Do NOT make any changes. Only research and plan.
)";
    agent.allowedTools = {"Read", "Glob", "Grep"};
    agent.temperatureOverride = 0.4f;
    return agent;
}

Agent AgentRegistry::getGeneralAgent() {
    Agent agent;
    agent.type = AgentType::General;
    agent.name = "general";
    agent.icon = "\xF0\x9F\xA4\x96";  // Robot
    agent.description = "Full assistant with all tools";
    agent.systemPrompt = "";  // Uses default system prompt
    agent.allowedTools = {};  // Empty = all tools allowed
    agent.temperatureOverride = -1.0f;
    return agent;
}

Agent AgentRegistry::getAgent(AgentType type) {
    switch (type) {
        case AgentType::Explorer: return getExplorerAgent();
        case AgentType::Coder: return getCoderAgent();
        case AgentType::Runner: return getRunnerAgent();
        case AgentType::Planner: return getPlannerAgent();
        default: return getGeneralAgent();
    }
}

std::vector<Agent> AgentRegistry::getAllAgents() {
    return {
        getGeneralAgent(),
        getExplorerAgent(),
        getCoderAgent(),
        getRunnerAgent(),
        getPlannerAgent()
    };
}

AgentType AgentRegistry::parseAgentName(const std::string& name) {
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    if (lower == "explorer" || lower == "explore") return AgentType::Explorer;
    if (lower == "coder" || lower == "code") return AgentType::Coder;
    if (lower == "runner" || lower == "run") return AgentType::Runner;
    if (lower == "planner" || lower == "plan") return AgentType::Planner;
    return AgentType::General;
}

} // namespace ollamacode
