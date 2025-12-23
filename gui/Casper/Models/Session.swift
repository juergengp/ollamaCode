import Foundation

/// Represents a saved conversation session
struct Session: Identifiable, Codable {
    let id: UUID
    var name: String
    var agentType: String
    var model: String
    var createdAt: Date
    var updatedAt: Date
    var messageCount: Int
    var preview: String

    init(
        id: UUID = UUID(),
        name: String,
        agentType: String = "general",
        model: String = "llama3",
        createdAt: Date = Date(),
        updatedAt: Date = Date(),
        messageCount: Int = 0,
        preview: String = ""
    ) {
        self.id = id
        self.name = name
        self.agentType = agentType
        self.model = model
        self.createdAt = createdAt
        self.updatedAt = updatedAt
        self.messageCount = messageCount
        self.preview = preview
    }

    /// Generate a title from the first user message
    static func generateTitle(from message: String) -> String {
        let cleaned = message.trimmingCharacters(in: .whitespacesAndNewlines)
        let maxLength = 50

        if cleaned.count <= maxLength {
            return cleaned
        }

        // Try to cut at a word boundary
        let truncated = String(cleaned.prefix(maxLength))
        if let lastSpace = truncated.lastIndex(of: " ") {
            return String(truncated[..<lastSpace]) + "..."
        }

        return truncated + "..."
    }
}

/// Session with full message history
struct SessionData: Codable {
    let session: Session
    let messages: [ConversationMessage]

    init(session: Session, messages: [Message]) {
        self.session = session
        self.messages = messages.map { ConversationMessage(from: $0) }
    }

    func toMessages() -> [Message] {
        messages.map { $0.toMessage() }
    }
}

/// Export format for sessions
enum SessionExportFormat: String, CaseIterable {
    case json = "JSON"
    case markdown = "Markdown"
    case text = "Plain Text"

    var fileExtension: String {
        switch self {
        case .json: return "json"
        case .markdown: return "md"
        case .text: return "txt"
        }
    }

    var contentType: String {
        switch self {
        case .json: return "application/json"
        case .markdown: return "text/markdown"
        case .text: return "text/plain"
        }
    }
}

/// Session export utility
struct SessionExporter {
    static func export(sessionData: SessionData, format: SessionExportFormat) -> String {
        switch format {
        case .json:
            return exportJSON(sessionData)
        case .markdown:
            return exportMarkdown(sessionData)
        case .text:
            return exportText(sessionData)
        }
    }

    private static func exportJSON(_ data: SessionData) -> String {
        let encoder = JSONEncoder()
        encoder.outputFormatting = [.prettyPrinted, .sortedKeys]
        encoder.dateEncodingStrategy = .iso8601

        guard let jsonData = try? encoder.encode(data),
              let jsonString = String(data: jsonData, encoding: .utf8) else {
            return "{}"
        }

        return jsonString
    }

    private static func exportMarkdown(_ data: SessionData) -> String {
        var output = "# \(data.session.name)\n\n"
        output += "**Model:** \(data.session.model)\n"
        output += "**Agent:** \(data.session.agentType)\n"
        output += "**Created:** \(formatDate(data.session.createdAt))\n\n"
        output += "---\n\n"

        for message in data.messages {
            switch message.role {
            case "user":
                output += "## You\n\n\(message.content)\n\n"
            case "assistant":
                output += "## Assistant\n\n\(message.content)\n\n"

                if let toolCalls = message.toolCalls, !toolCalls.isEmpty {
                    output += "### Tool Calls\n\n"
                    for tool in toolCalls {
                        output += "- **\(tool.name)**\n"
                        for (key, value) in tool.parameters {
                            output += "  - \(key): `\(value)`\n"
                        }
                    }
                    output += "\n"
                }
            case "tool":
                output += "### Tool Result\n\n```\n\(message.content)\n```\n\n"
            default:
                output += "\(message.content)\n\n"
            }
        }

        return output
    }

    private static func exportText(_ data: SessionData) -> String {
        var output = "\(data.session.name)\n"
        output += String(repeating: "=", count: data.session.name.count) + "\n\n"
        output += "Model: \(data.session.model)\n"
        output += "Agent: \(data.session.agentType)\n"
        output += "Created: \(formatDate(data.session.createdAt))\n\n"
        output += String(repeating: "-", count: 40) + "\n\n"

        for message in data.messages {
            switch message.role {
            case "user":
                output += "[You]\n\(message.content)\n\n"
            case "assistant":
                output += "[Assistant]\n\(message.content)\n\n"
            case "tool":
                output += "[Tool Result]\n\(message.content)\n\n"
            default:
                output += "\(message.content)\n\n"
            }
        }

        return output
    }

    private static func formatDate(_ date: Date) -> String {
        let formatter = DateFormatter()
        formatter.dateStyle = .medium
        formatter.timeStyle = .short
        return formatter.string(from: date)
    }
}
