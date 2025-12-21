import Foundation

/// Represents a message in the conversation
struct Message: Identifiable, Equatable {
    let id: UUID
    let role: MessageRole
    let content: String
    let timestamp: Date
    var toolCalls: [ToolCall]
    var toolResults: [ToolResult]
    var isStreaming: Bool

    init(
        id: UUID = UUID(),
        role: MessageRole,
        content: String,
        timestamp: Date = Date(),
        toolCalls: [ToolCall] = [],
        toolResults: [ToolResult] = [],
        isStreaming: Bool = false
    ) {
        self.id = id
        self.role = role
        self.content = content
        self.timestamp = timestamp
        self.toolCalls = toolCalls
        self.toolResults = toolResults
        self.isStreaming = isStreaming
    }

    /// Convert to Ollama ChatMessage format
    func toChatMessage() -> ChatMessage {
        ChatMessage(role: role.rawValue, content: content)
    }

    /// Create a user message
    static func user(_ content: String) -> Message {
        Message(role: .user, content: content)
    }

    /// Create an assistant message
    static func assistant(_ content: String, isStreaming: Bool = false) -> Message {
        Message(role: .assistant, content: content, isStreaming: isStreaming)
    }

    /// Create a system message
    static func system(_ content: String) -> Message {
        Message(role: .system, content: content)
    }

    /// Create a tool result message
    static func toolResult(_ content: String, for toolCall: ToolCall) -> Message {
        var message = Message(role: .tool, content: content)
        message.toolResults = [ToolResult(toolCallId: toolCall.id, success: true, output: content)]
        return message
    }
}

/// Message role in conversation
enum MessageRole: String, Codable {
    case system
    case user
    case assistant
    case tool

    var displayName: String {
        switch self {
        case .system: return "System"
        case .user: return "You"
        case .assistant: return "Assistant"
        case .tool: return "Tool"
        }
    }
}

/// Conversation containing messages and metadata
struct Conversation: Identifiable, Codable {
    let id: UUID
    var title: String
    var messages: [ConversationMessage]
    var createdAt: Date
    var updatedAt: Date
    var agentType: String

    init(
        id: UUID = UUID(),
        title: String = "New Conversation",
        messages: [ConversationMessage] = [],
        agentType: String = "general"
    ) {
        self.id = id
        self.title = title
        self.messages = messages
        self.createdAt = Date()
        self.updatedAt = Date()
        self.agentType = agentType
    }
}

/// Serializable message for storage
struct ConversationMessage: Codable, Identifiable {
    let id: UUID
    let role: String
    let content: String
    let timestamp: Date
    var toolCalls: [SerializedToolCall]?

    init(from message: Message) {
        self.id = message.id
        self.role = message.role.rawValue
        self.content = message.content
        self.timestamp = message.timestamp
        self.toolCalls = message.toolCalls.isEmpty ? nil : message.toolCalls.map { SerializedToolCall(from: $0) }
    }

    func toMessage() -> Message {
        let toolCalls = self.toolCalls?.map { $0.toToolCall() } ?? []
        return Message(
            id: id,
            role: MessageRole(rawValue: role) ?? .user,
            content: content,
            timestamp: timestamp,
            toolCalls: toolCalls
        )
    }
}

struct SerializedToolCall: Codable {
    let id: UUID
    let name: String
    let parameters: [String: String]

    init(from toolCall: ToolCall) {
        self.id = toolCall.id
        self.name = toolCall.name
        self.parameters = toolCall.parameters
    }

    func toToolCall() -> ToolCall {
        ToolCall(id: id, name: name, parameters: parameters)
    }
}
