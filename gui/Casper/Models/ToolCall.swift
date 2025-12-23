import Foundation

/// Available tool types
enum ToolType: String, CaseIterable, Codable, Identifiable {
    case bash = "Bash"
    case read = "Read"
    case write = "Write"
    case edit = "Edit"
    case glob = "Glob"
    case grep = "Grep"

    var id: String { rawValue }

    var displayName: String { rawValue }

    var icon: String {
        switch self {
        case .bash: return "terminal"
        case .read: return "doc.text"
        case .write: return "doc.badge.plus"
        case .edit: return "pencil"
        case .glob: return "folder.badge.questionmark"
        case .grep: return "magnifyingglass"
        }
    }

    var description: String {
        switch self {
        case .bash:
            return "Execute shell commands"
        case .read:
            return "Read file contents"
        case .write:
            return "Create or overwrite a file"
        case .edit:
            return "Modify specific parts of a file"
        case .glob:
            return "Find files matching a pattern"
        case .grep:
            return "Search file contents with regex"
        }
    }

    var requiredParameters: [String] {
        switch self {
        case .bash: return ["command"]
        case .read: return ["file_path"]
        case .write: return ["file_path", "content"]
        case .edit: return ["file_path", "old_string", "new_string"]
        case .glob: return ["pattern"]
        case .grep: return ["pattern"]
        }
    }

    var optionalParameters: [String] {
        switch self {
        case .bash: return ["timeout", "description"]
        case .read: return ["offset", "limit"]
        case .write: return []
        case .edit: return ["replace_all"]
        case .glob: return ["path"]
        case .grep: return ["path", "glob", "type", "output_mode", "-i", "-n", "-A", "-B", "-C"]
        }
    }
}

/// Represents a parsed tool call from AI response
struct ToolCall: Identifiable, Equatable, Hashable {
    let id: UUID
    let name: String
    let parameters: [String: String]

    init(id: UUID = UUID(), name: String, parameters: [String: String]) {
        self.id = id
        self.name = name
        self.parameters = parameters
    }

    var toolType: ToolType? {
        ToolType(rawValue: name)
    }

    var displayDescription: String {
        switch toolType {
        case .bash:
            return parameters["command"] ?? "Execute command"
        case .read:
            return parameters["file_path"] ?? "Read file"
        case .write:
            if let path = parameters["file_path"] {
                return "Write to \(path)"
            }
            return "Write file"
        case .edit:
            if let path = parameters["file_path"] {
                return "Edit \(path)"
            }
            return "Edit file"
        case .glob:
            return parameters["pattern"] ?? "Search files"
        case .grep:
            return parameters["pattern"] ?? "Search content"
        case nil:
            return name
        }
    }

    /// Validate that all required parameters are present
    func validate() -> [String] {
        guard let toolType = toolType else {
            return ["Unknown tool type: \(name)"]
        }

        var errors: [String] = []
        for param in toolType.requiredParameters {
            if parameters[param] == nil || parameters[param]?.isEmpty == true {
                errors.append("Missing required parameter: \(param)")
            }
        }
        return errors
    }

    /// Check if this tool is safe to auto-approve
    func isSafeForAutoApprove(allowedCommands: Set<String>) -> Bool {
        switch toolType {
        case .read, .glob, .grep:
            return true // Read-only operations are safe
        case .bash:
            guard let command = parameters["command"] else { return false }
            let baseCommand = command.split(separator: " ").first.map(String.init) ?? command
            return allowedCommands.contains(baseCommand)
        case .write, .edit:
            return false // Write operations need confirmation
        case nil:
            return false
        }
    }
}

/// Status of a tool execution
enum ToolExecutionStatus: Equatable {
    case pending
    case awaitingConfirmation
    case approved
    case denied
    case running
    case completed
    case failed(String)
}

/// Pending tool execution awaiting user confirmation
struct PendingToolExecution: Identifiable {
    let id: UUID
    let toolCall: ToolCall
    var status: ToolExecutionStatus
    var preview: String?

    init(toolCall: ToolCall) {
        self.id = toolCall.id
        self.toolCall = toolCall
        self.status = .pending
        self.preview = nil
    }
}
