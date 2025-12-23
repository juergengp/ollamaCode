import Foundation

/// Result of a tool execution
struct ToolResult: Identifiable, Equatable {
    let id: UUID
    let toolCallId: UUID
    let success: Bool
    let output: String
    let error: String?
    let exitCode: Int?
    let executionTime: TimeInterval?
    let timestamp: Date

    init(
        id: UUID = UUID(),
        toolCallId: UUID,
        success: Bool,
        output: String,
        error: String? = nil,
        exitCode: Int? = nil,
        executionTime: TimeInterval? = nil,
        timestamp: Date = Date()
    ) {
        self.id = id
        self.toolCallId = toolCallId
        self.success = success
        self.output = output
        self.error = error
        self.exitCode = exitCode
        self.executionTime = executionTime
        self.timestamp = timestamp
    }

    /// Truncated output for display
    var displayOutput: String {
        let maxLength = 2000
        if output.count > maxLength {
            return String(output.prefix(maxLength)) + "\n... (truncated)"
        }
        return output
    }

    /// Format result for AI consumption
    func formatForAI() -> String {
        var result = ""

        if success {
            result = output
        } else {
            result = "Error: \(error ?? "Unknown error")"
            if let code = exitCode {
                result += " (exit code: \(code))"
            }
            if !output.isEmpty {
                result += "\nOutput: \(output)"
            }
        }

        // Truncate if too long
        let maxLength = 10000
        if result.count > maxLength {
            result = String(result.prefix(maxLength)) + "\n... (output truncated)"
        }

        return result
    }

    /// Create a success result
    static func success(for toolCall: ToolCall, output: String, exitCode: Int? = nil, executionTime: TimeInterval? = nil) -> ToolResult {
        ToolResult(
            toolCallId: toolCall.id,
            success: true,
            output: output,
            exitCode: exitCode,
            executionTime: executionTime
        )
    }

    /// Create a failure result
    static func failure(for toolCall: ToolCall, error: String, output: String = "", exitCode: Int? = nil) -> ToolResult {
        ToolResult(
            toolCallId: toolCall.id,
            success: false,
            output: output,
            error: error,
            exitCode: exitCode
        )
    }

    /// Create a denied result (user rejected execution)
    static func denied(for toolCall: ToolCall) -> ToolResult {
        ToolResult(
            toolCallId: toolCall.id,
            success: false,
            output: "",
            error: "Tool execution denied by user"
        )
    }
}

/// Aggregated results from multiple tool executions
struct ToolExecutionBatch: Identifiable {
    let id: UUID
    let toolCalls: [ToolCall]
    var results: [ToolResult]
    var startTime: Date
    var endTime: Date?
    var iteration: Int

    init(toolCalls: [ToolCall], iteration: Int = 1) {
        self.id = UUID()
        self.toolCalls = toolCalls
        self.results = []
        self.startTime = Date()
        self.endTime = nil
        self.iteration = iteration
    }

    var isComplete: Bool {
        results.count == toolCalls.count
    }

    var allSucceeded: Bool {
        results.allSatisfy { $0.success }
    }

    mutating func addResult(_ result: ToolResult) {
        results.append(result)
        if isComplete {
            endTime = Date()
        }
    }

    /// Format all results for AI consumption
    func formatForAI() -> String {
        var output = "Tool execution results:\n\n"

        for (index, toolCall) in toolCalls.enumerated() {
            output += "[\(toolCall.name)]: "

            if let result = results.first(where: { $0.toolCallId == toolCall.id }) {
                output += result.formatForAI()
            } else {
                output += "(no result)"
            }

            if index < toolCalls.count - 1 {
                output += "\n\n---\n\n"
            }
        }

        return output
    }
}
