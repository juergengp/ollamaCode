import Foundation

/// Service for executing tool calls
/// Handles Bash, Read, Write, Edit, Glob, and Grep operations
actor ToolExecutorService {
    static let shared = ToolExecutorService()

    private let fileManager = FileManager.default
    private let maxOutputSize = 50000 // 50KB max output

    private init() {}

    // MARK: - Tool Execution

    /// Execute a tool call and return the result
    func execute(_ toolCall: ToolCall) async -> ToolResult {
        let startTime = Date()

        guard let toolType = toolCall.toolType else {
            return .failure(for: toolCall, error: "Unknown tool type: \(toolCall.name)")
        }

        do {
            let output: String
            var exitCode: Int? = nil

            switch toolType {
            case .bash:
                let result = try await executeBash(toolCall)
                output = result.output
                exitCode = result.exitCode
            case .read:
                output = try executeRead(toolCall)
            case .write:
                output = try executeWrite(toolCall)
            case .edit:
                output = try executeEdit(toolCall)
            case .glob:
                output = try executeGlob(toolCall)
            case .grep:
                output = try await executeGrep(toolCall)
            }

            let executionTime = Date().timeIntervalSince(startTime)
            return .success(for: toolCall, output: output, exitCode: exitCode, executionTime: executionTime)

        } catch let error as ToolError {
            return .failure(for: toolCall, error: error.localizedDescription)
        } catch {
            return .failure(for: toolCall, error: error.localizedDescription)
        }
    }

    // MARK: - Bash Execution

    struct BashResult {
        let output: String
        let exitCode: Int
    }

    private func executeBash(_ toolCall: ToolCall) async throws -> BashResult {
        guard let command = toolCall.parameters["command"] else {
            throw ToolError.missingParameter("command")
        }

        let timeoutMs = Int(toolCall.parameters["timeout"] ?? "") ?? 120000
        let timeout = TimeInterval(timeoutMs) / 1000.0

        return try await withCheckedThrowingContinuation { continuation in
            DispatchQueue.global().async {
                let process = Process()
                let outputPipe = Pipe()
                let errorPipe = Pipe()

                process.executableURL = URL(fileURLWithPath: "/bin/bash")
                process.arguments = ["-c", command]
                process.standardOutput = outputPipe
                process.standardError = errorPipe
                process.currentDirectoryURL = URL(fileURLWithPath: FileManager.default.currentDirectoryPath)

                // Set up environment
                var environment = ProcessInfo.processInfo.environment
                environment["TERM"] = "dumb"
                process.environment = environment

                var outputData = Data()
                var errorData = Data()

                outputPipe.fileHandleForReading.readabilityHandler = { handle in
                    let data = handle.availableData
                    if !data.isEmpty {
                        outputData.append(data)
                    }
                }

                errorPipe.fileHandleForReading.readabilityHandler = { handle in
                    let data = handle.availableData
                    if !data.isEmpty {
                        errorData.append(data)
                    }
                }

                do {
                    try process.run()

                    // Set up timeout
                    let timeoutWorkItem = DispatchWorkItem {
                        if process.isRunning {
                            process.terminate()
                        }
                    }
                    DispatchQueue.global().asyncAfter(deadline: .now() + timeout, execute: timeoutWorkItem)

                    process.waitUntilExit()
                    timeoutWorkItem.cancel()

                    // Clean up handlers
                    outputPipe.fileHandleForReading.readabilityHandler = nil
                    errorPipe.fileHandleForReading.readabilityHandler = nil

                    // Read any remaining data
                    outputData.append(outputPipe.fileHandleForReading.readDataToEndOfFile())
                    errorData.append(errorPipe.fileHandleForReading.readDataToEndOfFile())

                    var output = String(data: outputData, encoding: .utf8) ?? ""
                    let errorOutput = String(data: errorData, encoding: .utf8) ?? ""

                    if !errorOutput.isEmpty {
                        output += (output.isEmpty ? "" : "\n") + errorOutput
                    }

                    // Truncate if too long
                    if output.count > self.maxOutputSize {
                        output = String(output.prefix(self.maxOutputSize)) + "\n... (output truncated)"
                    }

                    continuation.resume(returning: BashResult(output: output, exitCode: Int(process.terminationStatus)))

                } catch {
                    continuation.resume(throwing: ToolError.executionFailed(error.localizedDescription))
                }
            }
        }
    }

    // MARK: - Read Execution

    private func executeRead(_ toolCall: ToolCall) throws -> String {
        guard let filePath = toolCall.parameters["file_path"] else {
            throw ToolError.missingParameter("file_path")
        }

        let expandedPath = NSString(string: filePath).expandingTildeInPath

        guard fileManager.fileExists(atPath: expandedPath) else {
            throw ToolError.fileNotFound(filePath)
        }

        guard fileManager.isReadableFile(atPath: expandedPath) else {
            throw ToolError.permissionDenied(filePath)
        }

        let content = try String(contentsOfFile: expandedPath, encoding: .utf8)
        let lines = content.components(separatedBy: .newlines)

        let offset = Int(toolCall.parameters["offset"] ?? "") ?? 0
        let limit = Int(toolCall.parameters["limit"] ?? "") ?? 2000

        // Apply offset and limit
        let startIndex = max(0, offset)
        let endIndex = min(lines.count, startIndex + limit)

        guard startIndex < lines.count else {
            return "(File has \(lines.count) lines, requested offset \(offset) is beyond end)"
        }

        let selectedLines = Array(lines[startIndex..<endIndex])

        // Format with line numbers
        var output = ""
        for (index, line) in selectedLines.enumerated() {
            let lineNumber = startIndex + index + 1
            let truncatedLine = line.count > 2000 ? String(line.prefix(2000)) + "..." : line
            output += String(format: "%6d\t%@\n", lineNumber, truncatedLine)
        }

        if endIndex < lines.count {
            output += "\n... (\(lines.count - endIndex) more lines)"
        }

        return output
    }

    // MARK: - Write Execution

    private func executeWrite(_ toolCall: ToolCall) throws -> String {
        guard let filePath = toolCall.parameters["file_path"] else {
            throw ToolError.missingParameter("file_path")
        }

        guard let content = toolCall.parameters["content"] else {
            throw ToolError.missingParameter("content")
        }

        let expandedPath = NSString(string: filePath).expandingTildeInPath
        let fileURL = URL(fileURLWithPath: expandedPath)

        // Create parent directories if needed
        let parentDir = fileURL.deletingLastPathComponent()
        if !fileManager.fileExists(atPath: parentDir.path) {
            try fileManager.createDirectory(at: parentDir, withIntermediateDirectories: true)
        }

        try content.write(toFile: expandedPath, atomically: true, encoding: .utf8)

        let lineCount = content.components(separatedBy: .newlines).count
        return "Successfully wrote \(content.count) bytes (\(lineCount) lines) to \(filePath)"
    }

    // MARK: - Edit Execution

    private func executeEdit(_ toolCall: ToolCall) throws -> String {
        guard let filePath = toolCall.parameters["file_path"] else {
            throw ToolError.missingParameter("file_path")
        }

        guard let oldString = toolCall.parameters["old_string"] else {
            throw ToolError.missingParameter("old_string")
        }

        guard let newString = toolCall.parameters["new_string"] else {
            throw ToolError.missingParameter("new_string")
        }

        let expandedPath = NSString(string: filePath).expandingTildeInPath

        guard fileManager.fileExists(atPath: expandedPath) else {
            throw ToolError.fileNotFound(filePath)
        }

        var content = try String(contentsOfFile: expandedPath, encoding: .utf8)

        let replaceAll = toolCall.parameters["replace_all"]?.lowercased() == "true"

        let occurrences = content.components(separatedBy: oldString).count - 1

        if occurrences == 0 {
            throw ToolError.editFailed("String not found in file: \(oldString.prefix(50))...")
        }

        if occurrences > 1 && !replaceAll {
            throw ToolError.editFailed("String found \(occurrences) times. Use replace_all=true to replace all, or provide more context to make the match unique.")
        }

        if replaceAll {
            content = content.replacingOccurrences(of: oldString, with: newString)
        } else {
            if let range = content.range(of: oldString) {
                content.replaceSubrange(range, with: newString)
            }
        }

        try content.write(toFile: expandedPath, atomically: true, encoding: .utf8)

        let replacedCount = replaceAll ? occurrences : 1
        return "Successfully replaced \(replacedCount) occurrence(s) in \(filePath)"
    }

    // MARK: - Glob Execution

    private func executeGlob(_ toolCall: ToolCall) throws -> String {
        guard let pattern = toolCall.parameters["pattern"] else {
            throw ToolError.missingParameter("pattern")
        }

        let basePath = toolCall.parameters["path"] ?? fileManager.currentDirectoryPath
        let expandedBase = NSString(string: basePath).expandingTildeInPath

        var matches: [String] = []
        let maxResults = 100

        // Simple glob implementation
        if let enumerator = fileManager.enumerator(atPath: expandedBase) {
            while let file = enumerator.nextObject() as? String {
                if matches.count >= maxResults { break }

                if matchesGlobPattern(file, pattern: pattern) {
                    matches.append(file)
                }
            }
        }

        if matches.isEmpty {
            return "No files found matching pattern: \(pattern)"
        }

        matches.sort()
        var output = "Found \(matches.count) file(s):\n"
        for match in matches {
            output += "\(match)\n"
        }

        if matches.count >= maxResults {
            output += "\n... (results limited to \(maxResults))"
        }

        return output
    }

    private func matchesGlobPattern(_ path: String, pattern: String) -> Bool {
        // Convert glob pattern to regex
        var regexPattern = pattern
            .replacingOccurrences(of: ".", with: "\\.")
            .replacingOccurrences(of: "**", with: "<<<DOUBLESTAR>>>")
            .replacingOccurrences(of: "*", with: "[^/]*")
            .replacingOccurrences(of: "<<<DOUBLESTAR>>>", with: ".*")
            .replacingOccurrences(of: "?", with: ".")

        regexPattern = "^" + regexPattern + "$"

        guard let regex = try? NSRegularExpression(pattern: regexPattern, options: []) else {
            return false
        }

        let range = NSRange(path.startIndex..., in: path)
        return regex.firstMatch(in: path, options: [], range: range) != nil
    }

    // MARK: - Grep Execution

    private func executeGrep(_ toolCall: ToolCall) async throws -> String {
        guard let pattern = toolCall.parameters["pattern"] else {
            throw ToolError.missingParameter("pattern")
        }

        let basePath = toolCall.parameters["path"] ?? fileManager.currentDirectoryPath
        let expandedBase = NSString(string: basePath).expandingTildeInPath
        let fileGlob = toolCall.parameters["glob"]
        let fileType = toolCall.parameters["type"]

        guard let regex = try? NSRegularExpression(pattern: pattern, options: []) else {
            throw ToolError.invalidPattern(pattern)
        }

        var matches: [(file: String, line: Int, content: String)] = []
        let maxMatches = 50

        // Search files
        if let enumerator = fileManager.enumerator(atPath: expandedBase) {
            while let file = enumerator.nextObject() as? String {
                if matches.count >= maxMatches { break }

                let fullPath = (expandedBase as NSString).appendingPathComponent(file)

                // Skip directories
                var isDir: ObjCBool = false
                guard fileManager.fileExists(atPath: fullPath, isDirectory: &isDir), !isDir.boolValue else {
                    continue
                }

                // Apply file type filter
                if let fileType = fileType {
                    let ext = (file as NSString).pathExtension
                    if ext != fileType { continue }
                }

                // Apply glob filter
                if let glob = fileGlob {
                    if !matchesGlobPattern(file, pattern: glob) { continue }
                }

                // Search in file
                guard let content = try? String(contentsOfFile: fullPath, encoding: .utf8) else {
                    continue
                }

                let lines = content.components(separatedBy: .newlines)
                for (index, line) in lines.enumerated() {
                    if matches.count >= maxMatches { break }

                    let range = NSRange(line.startIndex..., in: line)
                    if regex.firstMatch(in: line, options: [], range: range) != nil {
                        matches.append((file: file, line: index + 1, content: line))
                    }
                }
            }
        }

        if matches.isEmpty {
            return "No matches found for pattern: \(pattern)"
        }

        var output = "Found \(matches.count) match(es):\n\n"
        for match in matches {
            let truncatedContent = match.content.count > 200 ? String(match.content.prefix(200)) + "..." : match.content
            output += "\(match.file):\(match.line): \(truncatedContent)\n"
        }

        if matches.count >= maxMatches {
            output += "\n... (results limited to \(maxMatches))"
        }

        return output
    }
}

// MARK: - Tool Errors

enum ToolError: LocalizedError {
    case missingParameter(String)
    case fileNotFound(String)
    case permissionDenied(String)
    case executionFailed(String)
    case editFailed(String)
    case invalidPattern(String)

    var errorDescription: String? {
        switch self {
        case .missingParameter(let param):
            return "Missing required parameter: \(param)"
        case .fileNotFound(let path):
            return "File not found: \(path)"
        case .permissionDenied(let path):
            return "Permission denied: \(path)"
        case .executionFailed(let message):
            return "Execution failed: \(message)"
        case .editFailed(let message):
            return "Edit failed: \(message)"
        case .invalidPattern(let pattern):
            return "Invalid regex pattern: \(pattern)"
        }
    }
}
