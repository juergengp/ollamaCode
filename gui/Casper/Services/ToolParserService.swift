import Foundation

/// Service for parsing tool calls from AI responses
/// Supports both the custom XML format and Claude's antml format
class ToolParserService {
    static let shared = ToolParserService()

    private init() {}

    /// Parse result containing tool calls and remaining text
    struct ParseResult {
        let toolCalls: [ToolCall]
        let textContent: String
        let hasToolCalls: Bool

        var isEmpty: Bool {
            toolCalls.isEmpty && textContent.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty
        }
    }

    // MARK: - Main Parsing

    /// Parse AI response for tool calls
    func parse(_ response: String) -> ParseResult {
        var toolCalls: [ToolCall] = []
        var textContent = response

        // Try parsing Claude's antml format first
        let antmlCalls = parseAntmlFormat(response)
        if !antmlCalls.isEmpty {
            toolCalls.append(contentsOf: antmlCalls)
            textContent = removeAntmlBlocks(from: response)
        }

        // Also try the legacy format
        let legacyCalls = parseLegacyFormat(response)
        if !legacyCalls.isEmpty {
            toolCalls.append(contentsOf: legacyCalls)
            textContent = removeLegacyBlocks(from: textContent)
        }

        // Clean up text content
        textContent = textContent.trimmingCharacters(in: .whitespacesAndNewlines)

        return ParseResult(
            toolCalls: toolCalls,
            textContent: textContent,
            hasToolCalls: !toolCalls.isEmpty
        )
    }

    // MARK: - Claude antml Format Parsing

    private func parseAntmlFormat(_ text: String) -> [ToolCall] {
        var toolCalls: [ToolCall] = []

        // Pattern for function_calls block
        let blockPattern = "<function_calls>([\\s\\S]*?)</function_calls>"
        guard let blockRegex = try? NSRegularExpression(pattern: blockPattern, options: []) else {
            return []
        }

        let range = NSRange(text.startIndex..., in: text)
        let blockMatches = blockRegex.matches(in: text, options: [], range: range)

        for blockMatch in blockMatches {
            guard let blockRange = Range(blockMatch.range(at: 1), in: text) else { continue }
            let blockContent = String(text[blockRange])

            // Parse invoke elements within the block
            let invokePattern = "<invoke name=\"([^\"]+)\"[^>]*>([\\s\\S]*?)</invoke>"
            guard let invokeRegex = try? NSRegularExpression(pattern: invokePattern, options: []) else {
                continue
            }

            let invokeRange = NSRange(blockContent.startIndex..., in: blockContent)
            let invokeMatches = invokeRegex.matches(in: blockContent, options: [], range: invokeRange)

            for invokeMatch in invokeMatches {
                guard let nameRange = Range(invokeMatch.range(at: 1), in: blockContent),
                      let paramsRange = Range(invokeMatch.range(at: 2), in: blockContent) else {
                    continue
                }

                let toolName = String(blockContent[nameRange])
                let paramsContent = String(blockContent[paramsRange])
                let parameters = parseParameters(paramsContent)

                toolCalls.append(ToolCall(name: toolName, parameters: parameters))
            }
        }

        return toolCalls
    }

    private func removeAntmlBlocks(from text: String) -> String {
        let pattern = "<function_calls>[\\s\\S]*?</function_calls>"
        guard let regex = try? NSRegularExpression(pattern: pattern, options: []) else {
            return text
        }
        let range = NSRange(text.startIndex..., in: text)
        return regex.stringByReplacingMatches(in: text, options: [], range: range, withTemplate: "")
    }

    // MARK: - Legacy Format Parsing

    private func parseLegacyFormat(_ text: String) -> [ToolCall] {
        var toolCalls: [ToolCall] = []

        // Pattern for tool_calls block
        let blockPattern = "<tool_calls>([\\s\\S]*?)</tool_calls>"
        guard let blockRegex = try? NSRegularExpression(pattern: blockPattern, options: []) else {
            return []
        }

        let range = NSRange(text.startIndex..., in: text)
        let blockMatches = blockRegex.matches(in: text, options: [], range: range)

        for blockMatch in blockMatches {
            guard let blockRange = Range(blockMatch.range(at: 1), in: text) else { continue }
            let blockContent = String(text[blockRange])

            // Parse individual tool_call elements
            let toolPattern = "<tool_call>([\\s\\S]*?)</tool_call>"
            guard let toolRegex = try? NSRegularExpression(pattern: toolPattern, options: []) else {
                continue
            }

            let toolRange = NSRange(blockContent.startIndex..., in: blockContent)
            let toolMatches = toolRegex.matches(in: blockContent, options: [], range: toolRange)

            for toolMatch in toolMatches {
                guard let contentRange = Range(toolMatch.range(at: 1), in: blockContent) else {
                    continue
                }

                let toolContent = String(blockContent[contentRange])

                // Extract tool name
                let namePattern = "<tool_name>([^<]+)</tool_name>"
                guard let nameRegex = try? NSRegularExpression(pattern: namePattern, options: []),
                      let nameMatch = nameRegex.firstMatch(in: toolContent, options: [], range: NSRange(toolContent.startIndex..., in: toolContent)),
                      let nameRange = Range(nameMatch.range(at: 1), in: toolContent) else {
                    continue
                }

                let toolName = String(toolContent[nameRange])
                let parameters = parseLegacyParameters(toolContent)

                toolCalls.append(ToolCall(name: toolName, parameters: parameters))
            }
        }

        return toolCalls
    }

    private func removeLegacyBlocks(from text: String) -> String {
        let pattern = "<tool_calls>[\\s\\S]*?</tool_calls>"
        guard let regex = try? NSRegularExpression(pattern: pattern, options: []) else {
            return text
        }
        let range = NSRange(text.startIndex..., in: text)
        return regex.stringByReplacingMatches(in: text, options: [], range: range, withTemplate: "")
    }

    // MARK: - Parameter Parsing

    private func parseParameters(_ content: String) -> [String: String] {
        var parameters: [String: String] = [:]

        // Pattern: <parameter name="key">value</parameter>
        let pattern = "<parameter name=\"([^\"]+)\">([\\s\\S]*?)</parameter>"
        guard let regex = try? NSRegularExpression(pattern: pattern, options: []) else {
            return parameters
        }

        let range = NSRange(content.startIndex..., in: content)
        let matches = regex.matches(in: content, options: [], range: range)

        for match in matches {
            guard let keyRange = Range(match.range(at: 1), in: content),
                  let valueRange = Range(match.range(at: 2), in: content) else {
                continue
            }

            let key = String(content[keyRange])
            let value = String(content[valueRange]).trimmingCharacters(in: .whitespacesAndNewlines)
            parameters[key] = value
        }

        return parameters
    }

    private func parseLegacyParameters(_ content: String) -> [String: String] {
        var parameters: [String: String] = [:]

        // Pattern: <param_name>value</param_name>
        // Common parameters: command, file_path, content, old_string, new_string, pattern, path
        let paramNames = ["command", "file_path", "content", "old_string", "new_string",
                          "pattern", "path", "timeout", "description", "offset", "limit",
                          "replace_all", "glob", "type", "output_mode"]

        for paramName in paramNames {
            let pattern = "<\(paramName)>([\\s\\S]*?)</\(paramName)>"
            guard let regex = try? NSRegularExpression(pattern: pattern, options: []),
                  let match = regex.firstMatch(in: content, options: [], range: NSRange(content.startIndex..., in: content)),
                  let valueRange = Range(match.range(at: 1), in: content) else {
                continue
            }

            let value = String(content[valueRange]).trimmingCharacters(in: .whitespacesAndNewlines)
            parameters[paramName] = value
        }

        return parameters
    }

    // MARK: - Tool Format Generation

    /// Generate the tool format documentation for system prompt
    func generateToolFormatPrompt(tools: Set<ToolType>) -> String {
        var prompt = """

        ## Available Tools

        You can use the following tools by including XML-formatted tool calls in your response.

        Format:
        <function_calls>
        <invoke name="ToolName">
        <parameter name="param1">value1</parameter>
        <parameter name="param2">value2</parameter>
        </invoke>
        </function_calls>

        Available tools:

        """

        for tool in tools.sorted(by: { $0.rawValue < $1.rawValue }) {
            prompt += generateToolDocumentation(tool)
        }

        prompt += """

        ## Important Guidelines

        - Only use tools when necessary
        - Read files before editing them
        - Use the appropriate tool for each task
        - Handle errors gracefully
        - Explain what you're doing before using tools

        """

        return prompt
    }

    private func generateToolDocumentation(_ tool: ToolType) -> String {
        switch tool {
        case .bash:
            return """

            ### Bash
            Execute shell commands.

            Parameters:
            - command (required): The command to execute
            - timeout (optional): Timeout in milliseconds
            - description (optional): Description of what the command does

            Example:
            <invoke name="Bash">
            <parameter name="command">ls -la</parameter>
            <parameter name="description">List files in current directory</parameter>
            </invoke>

            """

        case .read:
            return """

            ### Read
            Read file contents.

            Parameters:
            - file_path (required): Absolute path to the file
            - offset (optional): Line number to start from
            - limit (optional): Number of lines to read

            Example:
            <invoke name="Read">
            <parameter name="file_path">/path/to/file.txt</parameter>
            </invoke>

            """

        case .write:
            return """

            ### Write
            Create or overwrite a file.

            Parameters:
            - file_path (required): Absolute path to the file
            - content (required): Content to write

            Example:
            <invoke name="Write">
            <parameter name="file_path">/path/to/file.txt</parameter>
            <parameter name="content">File content here</parameter>
            </invoke>

            """

        case .edit:
            return """

            ### Edit
            Modify specific parts of a file.

            Parameters:
            - file_path (required): Absolute path to the file
            - old_string (required): Text to find and replace
            - new_string (required): Replacement text
            - replace_all (optional): Replace all occurrences (default: false)

            Example:
            <invoke name="Edit">
            <parameter name="file_path">/path/to/file.txt</parameter>
            <parameter name="old_string">old text</parameter>
            <parameter name="new_string">new text</parameter>
            </invoke>

            """

        case .glob:
            return """

            ### Glob
            Find files matching a pattern.

            Parameters:
            - pattern (required): Glob pattern (e.g., "**/*.swift")
            - path (optional): Directory to search in

            Example:
            <invoke name="Glob">
            <parameter name="pattern">**/*.swift</parameter>
            </invoke>

            """

        case .grep:
            return """

            ### Grep
            Search file contents with regex.

            Parameters:
            - pattern (required): Regex pattern to search for
            - path (optional): File or directory to search
            - glob (optional): File pattern filter
            - type (optional): File type (e.g., "swift", "py")

            Example:
            <invoke name="Grep">
            <parameter name="pattern">func.*init</parameter>
            <parameter name="type">swift</parameter>
            </invoke>

            """
        }
    }
}
