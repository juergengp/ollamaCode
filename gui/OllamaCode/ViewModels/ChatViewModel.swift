import Foundation
import SwiftUI
import Combine

/// Main view model for chat functionality
@MainActor
class ChatViewModel: ObservableObject {
    // MARK: - Published Properties

    @Published var messages: [Message] = []
    @Published var inputText: String = ""
    @Published var isLoading: Bool = false
    @Published var isStreaming: Bool = false
    @Published var streamingContent: String = ""
    @Published var error: String?
    @Published var currentAgent: AgentType = .general

    @Published var pendingToolExecutions: [PendingToolExecution] = []
    @Published var showToolConfirmation: Bool = false
    @Published var currentIteration: Int = 0

    // MARK: - Private Properties

    private let ollamaService = OllamaService.shared
    private let configService = ConfigService.shared
    private let toolParser = ToolParserService.shared
    private let toolExecutor = ToolExecutorService.shared

    private let maxIterations = 10
    private var cancellables = Set<AnyCancellable>()

    // MARK: - Initialization

    init() {
        setupNotifications()
    }

    private func setupNotifications() {
        NotificationCenter.default.publisher(for: .clearConversation)
            .sink { [weak self] _ in
                self?.clearConversation()
            }
            .store(in: &cancellables)

        NotificationCenter.default.publisher(for: .newConversation)
            .sink { [weak self] _ in
                self?.newConversation()
            }
            .store(in: &cancellables)
    }

    // MARK: - Public Methods

    /// Send a message and get AI response
    func sendMessage() async {
        let text = inputText.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !text.isEmpty else { return }

        inputText = ""
        error = nil
        currentIteration = 0

        // Add user message
        let userMessage = Message.user(text)
        messages.append(userMessage)

        await processConversation()
    }

    /// Process the current conversation (handles tool execution iterations)
    func processConversation() async {
        currentIteration += 1

        guard currentIteration <= maxIterations else {
            error = "Maximum tool iterations (\(maxIterations)) reached"
            return
        }

        isLoading = true
        defer { isLoading = false }

        do {
            // Build messages for API
            let chatMessages = buildChatMessages()

            // Call Ollama
            let response = try await ollamaService.chat(
                model: configService.model,
                messages: chatMessages,
                temperature: configService.temperature,
                maxTokens: configService.maxTokens
            )

            let responseText = response.message.content

            // Parse for tool calls
            let parseResult = toolParser.parse(responseText)

            if parseResult.hasToolCalls {
                // Add assistant message with text content (if any)
                var assistantMessage = Message.assistant(parseResult.textContent)
                assistantMessage.toolCalls = parseResult.toolCalls
                messages.append(assistantMessage)

                // Handle tool execution
                await handleToolCalls(parseResult.toolCalls)
            } else {
                // No tool calls, just add the response
                messages.append(Message.assistant(responseText))
            }

        } catch {
            self.error = error.localizedDescription
        }
    }

    /// Handle tool calls - show confirmation UI
    private func handleToolCalls(_ toolCalls: [ToolCall]) async {
        let agent = Agent(type: currentAgent)
        var executions: [PendingToolExecution] = []

        for toolCall in toolCalls {
            var execution = PendingToolExecution(toolCall: toolCall)

            // Check if tool is allowed for current agent
            if let toolType = toolCall.toolType, !agent.allowedTools.contains(toolType) {
                execution.status = .failed("Tool not allowed for \(agent.displayName) agent")
                executions.append(execution)
                continue
            }

            // Check for auto-approve
            if configService.autoApprove || toolCall.isSafeForAutoApprove(allowedCommands: configService.allowedCommands) {
                execution.status = .approved
            } else {
                execution.status = .awaitingConfirmation
            }

            executions.append(execution)
        }

        pendingToolExecutions = executions

        // Check if we need user confirmation
        let needsConfirmation = executions.contains { $0.status == .awaitingConfirmation }

        if needsConfirmation {
            showToolConfirmation = true
            // Wait for user to approve/deny - handled by UI
        } else {
            // All auto-approved, execute immediately
            await executeApprovedTools()
        }
    }

    /// Execute all approved tools
    func executeApprovedTools() async {
        showToolConfirmation = false

        var results: [ToolResult] = []

        for i in pendingToolExecutions.indices {
            switch pendingToolExecutions[i].status {
            case .approved:
                pendingToolExecutions[i].status = .running
                let result = await toolExecutor.execute(pendingToolExecutions[i].toolCall)
                results.append(result)
                pendingToolExecutions[i].status = result.success ? .completed : .failed(result.error ?? "Unknown error")

            case .denied:
                let result = ToolResult.denied(for: pendingToolExecutions[i].toolCall)
                results.append(result)

            case .failed(let error):
                let result = ToolResult.failure(for: pendingToolExecutions[i].toolCall, error: error)
                results.append(result)

            default:
                break
            }
        }

        // Add tool results to conversation
        if !results.isEmpty {
            let batch = ToolExecutionBatch(toolCalls: pendingToolExecutions.map(\.toolCall), iteration: currentIteration)
            let formattedResults = formatToolResults(results)

            // Add tool result message
            var toolMessage = Message(role: .tool, content: formattedResults)
            toolMessage.toolResults = results
            messages.append(toolMessage)

            // Continue conversation for AI to process results
            pendingToolExecutions = []
            await processConversation()
        }

        pendingToolExecutions = []
    }

    /// Approve a specific tool execution
    func approveTool(_ id: UUID) {
        if let index = pendingToolExecutions.firstIndex(where: { $0.id == id }) {
            pendingToolExecutions[index].status = .approved
        }
    }

    /// Deny a specific tool execution
    func denyTool(_ id: UUID) {
        if let index = pendingToolExecutions.firstIndex(where: { $0.id == id }) {
            pendingToolExecutions[index].status = .denied
        }
    }

    /// Approve all pending tools
    func approveAllTools() {
        for i in pendingToolExecutions.indices {
            if pendingToolExecutions[i].status == .awaitingConfirmation {
                pendingToolExecutions[i].status = .approved
            }
        }
    }

    /// Deny all pending tools
    func denyAllTools() {
        for i in pendingToolExecutions.indices {
            if pendingToolExecutions[i].status == .awaitingConfirmation {
                pendingToolExecutions[i].status = .denied
            }
        }
    }

    /// Clear the current conversation
    func clearConversation() {
        messages = []
        pendingToolExecutions = []
        showToolConfirmation = false
        error = nil
        currentIteration = 0
    }

    /// Start a new conversation
    func newConversation() {
        clearConversation()
    }

    /// Change the current agent
    func selectAgent(_ agent: AgentType) {
        currentAgent = agent
    }

    // MARK: - Private Helpers

    private func buildChatMessages() -> [ChatMessage] {
        var chatMessages: [ChatMessage] = []

        // Build system prompt
        let agent = Agent(type: currentAgent)
        let systemPrompt = buildSystemPrompt(agent: agent)
        chatMessages.append(.system(systemPrompt))

        // Add conversation messages
        for message in messages {
            switch message.role {
            case .user:
                chatMessages.append(.user(message.content))
            case .assistant:
                chatMessages.append(.assistant(message.content))
            case .tool:
                // Tool results are sent as user messages
                chatMessages.append(.user("Tool execution results:\n\(message.content)"))
            case .system:
                // Skip system messages in history
                break
            }
        }

        return chatMessages
    }

    private func buildSystemPrompt(agent: Agent) -> String {
        var prompt = agent.systemPrompt
        prompt += toolParser.generateToolFormatPrompt(tools: agent.allowedTools)

        // Add current working directory
        prompt += "\n\nCurrent working directory: \(FileManager.default.currentDirectoryPath)"

        return prompt
    }

    private func formatToolResults(_ results: [ToolResult]) -> String {
        var output = ""

        for (index, result) in results.enumerated() {
            if index > 0 { output += "\n\n---\n\n" }

            if let toolCall = pendingToolExecutions.first(where: { $0.toolCall.id == result.toolCallId })?.toolCall {
                output += "[\(toolCall.name)]:\n"
            }

            output += result.formatForAI()
        }

        return output
    }
}

// MARK: - Streaming Support (Optional Enhancement)

extension ChatViewModel {
    /// Send message with streaming response
    func sendMessageStreaming() async {
        let text = inputText.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !text.isEmpty else { return }

        inputText = ""
        error = nil
        currentIteration = 0

        // Add user message
        messages.append(Message.user(text))

        isStreaming = true
        streamingContent = ""

        // Add placeholder for streaming response
        var streamingMessage = Message.assistant("", isStreaming: true)
        messages.append(streamingMessage)

        do {
            let chatMessages = buildChatMessages()

            for try await chunk in ollamaService.streamChat(
                model: configService.model,
                messages: chatMessages,
                temperature: configService.temperature,
                maxTokens: configService.maxTokens
            ) {
                if let content = chunk.message?.content {
                    streamingContent += content

                    // Update the last message
                    if let lastIndex = messages.indices.last {
                        messages[lastIndex] = Message.assistant(streamingContent, isStreaming: !chunk.done)
                    }
                }

                if chunk.done {
                    break
                }
            }

            isStreaming = false

            // Parse final response for tool calls
            let parseResult = toolParser.parse(streamingContent)

            if parseResult.hasToolCalls {
                // Update message with parsed content and tool calls
                if let lastIndex = messages.indices.last {
                    var updatedMessage = Message.assistant(parseResult.textContent)
                    updatedMessage.toolCalls = parseResult.toolCalls
                    messages[lastIndex] = updatedMessage
                }

                await handleToolCalls(parseResult.toolCalls)
            }

            streamingContent = ""

        } catch {
            isStreaming = false
            self.error = error.localizedDescription
        }
    }
}
