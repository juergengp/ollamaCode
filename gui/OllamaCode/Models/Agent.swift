import Foundation
import SwiftUI

/// Agent types matching the CLI implementation
enum AgentType: String, CaseIterable, Codable, Identifiable {
    case explorer
    case coder
    case runner
    case planner
    case general

    var id: String { rawValue }

    var displayName: String {
        switch self {
        case .explorer: return "Explorer"
        case .coder: return "Coder"
        case .runner: return "Runner"
        case .planner: return "Planner"
        case .general: return "General"
        }
    }

    var icon: String {
        switch self {
        case .explorer: return "magnifyingglass"
        case .coder: return "chevron.left.forwardslash.chevron.right"
        case .runner: return "play.fill"
        case .planner: return "list.clipboard"
        case .general: return "cpu"
        }
    }

    var emoji: String {
        switch self {
        case .explorer: return "🔍"
        case .coder: return "💻"
        case .runner: return "▶️"
        case .planner: return "📋"
        case .general: return "🤖"
        }
    }

    var description: String {
        switch self {
        case .explorer:
            return "Read-only exploration of files and codebase. Uses Glob, Grep, and Read tools only."
        case .coder:
            return "Code modification specialist. Can read, write, edit, and search files."
        case .runner:
            return "Command execution focused. Runs bash commands and reads files."
        case .planner:
            return "Task planning and analysis. Read-only access for understanding and planning."
        case .general:
            return "Full access to all tools. Use when you need complete flexibility."
        }
    }

    var color: Color {
        switch self {
        case .explorer: return .blue
        case .coder: return .green
        case .runner: return .orange
        case .planner: return .purple
        case .general: return .gray
        }
    }

    var allowedTools: Set<ToolType> {
        switch self {
        case .explorer:
            return [.glob, .grep, .read]
        case .coder:
            return [.read, .write, .edit, .glob]
        case .runner:
            return [.bash, .read]
        case .planner:
            return [.glob, .grep, .read]
        case .general:
            return Set(ToolType.allCases)
        }
    }

    var suggestedTemperature: Double {
        switch self {
        case .explorer: return 0.3
        case .coder: return 0.5
        case .runner: return 0.3
        case .planner: return 0.7
        case .general: return 0.7
        }
    }

    var systemPrompt: String {
        switch self {
        case .explorer:
            return """
            You are an Explorer agent specialized in navigating and understanding codebases.
            Your role is to help users find files, search code, and understand project structure.

            You have access to these tools:
            - Glob: Find files matching patterns
            - Grep: Search file contents with regex
            - Read: Read file contents

            Focus on:
            - Finding relevant files quickly
            - Understanding code structure
            - Explaining what you find
            - Suggesting where to look next

            You cannot modify files - only read and search.
            """

        case .coder:
            return """
            You are a Coder agent specialized in writing and modifying code.
            Your role is to help users implement features, fix bugs, and improve code quality.

            You have access to these tools:
            - Read: Read file contents
            - Write: Create or overwrite files
            - Edit: Modify specific parts of files
            - Glob: Find files matching patterns

            Focus on:
            - Writing clean, maintainable code
            - Following existing code patterns
            - Making minimal, targeted changes
            - Explaining your changes

            Always read files before editing them.
            """

        case .runner:
            return """
            You are a Runner agent specialized in executing commands.
            Your role is to help users run builds, tests, and other shell commands.

            You have access to these tools:
            - Bash: Execute shell commands
            - Read: Read file contents

            Focus on:
            - Running commands safely
            - Interpreting command output
            - Suggesting next steps
            - Handling errors gracefully

            Be cautious with destructive commands.
            """

        case .planner:
            return """
            You are a Planner agent specialized in analyzing tasks and creating plans.
            Your role is to help users understand complex tasks and break them into steps.

            You have access to these tools:
            - Glob: Find files matching patterns
            - Grep: Search file contents with regex
            - Read: Read file contents

            Focus on:
            - Understanding the full scope of tasks
            - Breaking work into clear steps
            - Identifying dependencies
            - Considering edge cases

            You create plans but don't implement them directly.
            """

        case .general:
            return """
            You are a General agent with access to all available tools.
            You can help with any task including exploration, coding, running commands, and planning.

            You have access to all tools:
            - Bash: Execute shell commands
            - Read: Read file contents
            - Write: Create or overwrite files
            - Edit: Modify specific parts of files
            - Glob: Find files matching patterns
            - Grep: Search file contents with regex

            Adapt your approach based on what the user needs.
            Use the right tools for each task.
            """
        }
    }
}

/// Full agent definition with all properties
struct Agent: Identifiable {
    let type: AgentType
    var customInstructions: String?

    var id: String { type.rawValue }
    var displayName: String { type.displayName }
    var icon: String { type.icon }
    var emoji: String { type.emoji }
    var description: String { type.description }
    var color: Color { type.color }
    var allowedTools: Set<ToolType> { type.allowedTools }
    var suggestedTemperature: Double { type.suggestedTemperature }

    var systemPrompt: String {
        var prompt = type.systemPrompt
        if let custom = customInstructions, !custom.isEmpty {
            prompt += "\n\nAdditional Instructions:\n\(custom)"
        }
        return prompt
    }

    init(type: AgentType, customInstructions: String? = nil) {
        self.type = type
        self.customInstructions = customInstructions
    }
}
