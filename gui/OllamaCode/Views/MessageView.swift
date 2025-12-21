import SwiftUI

/// Individual message view
struct MessageView: View {
    let message: Message

    var body: some View {
        HStack(alignment: .top, spacing: 12) {
            // Avatar
            avatarView

            // Content
            VStack(alignment: .leading, spacing: 8) {
                // Header
                HStack {
                    Text(message.role.displayName)
                        .font(.headline)
                        .foregroundColor(roleColor)

                    Spacer()

                    Text(formatTime(message.timestamp))
                        .font(.caption2)
                        .foregroundColor(.secondary)
                }

                // Message content
                if !message.content.isEmpty {
                    MessageContentView(content: message.content, isStreaming: message.isStreaming)
                }

                // Tool calls
                if !message.toolCalls.isEmpty {
                    ToolCallsView(toolCalls: message.toolCalls)
                }

                // Tool results
                if !message.toolResults.isEmpty {
                    ToolResultsView(results: message.toolResults)
                }
            }

            if message.role == .user {
                Spacer(minLength: 60)
            }
        }
        .padding(.horizontal)
    }

    @ViewBuilder
    private var avatarView: some View {
        Circle()
            .fill(roleColor.opacity(0.2))
            .frame(width: 36, height: 36)
            .overlay {
                Image(systemName: avatarIcon)
                    .font(.system(size: 16))
                    .foregroundColor(roleColor)
            }
    }

    private var avatarIcon: String {
        switch message.role {
        case .user: return "person.fill"
        case .assistant: return "cpu"
        case .tool: return "wrench.fill"
        case .system: return "gear"
        }
    }

    private var roleColor: Color {
        switch message.role {
        case .user: return .blue
        case .assistant: return .green
        case .tool: return .orange
        case .system: return .gray
        }
    }

    private func formatTime(_ date: Date) -> String {
        let formatter = DateFormatter()
        formatter.timeStyle = .short
        return formatter.string(from: date)
    }
}

/// Message content with markdown support
struct MessageContentView: View {
    let content: String
    let isStreaming: Bool

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            // Split content into code blocks and text
            ForEach(parseContent(), id: \.self) { block in
                if block.hasPrefix("```") {
                    CodeBlockView(content: block)
                } else {
                    Text(block)
                        .textSelection(.enabled)
                }
            }

            if isStreaming {
                HStack(spacing: 4) {
                    ForEach(0..<3, id: \.self) { i in
                        Circle()
                            .fill(Color.secondary)
                            .frame(width: 6, height: 6)
                            .opacity(0.5)
                    }
                }
            }
        }
    }

    private func parseContent() -> [String] {
        // Simple parsing for code blocks
        var blocks: [String] = []
        var currentBlock = ""
        var inCodeBlock = false
        var codeBlockContent = ""

        for line in content.components(separatedBy: "\n") {
            if line.hasPrefix("```") {
                if inCodeBlock {
                    // End of code block
                    codeBlockContent += line
                    blocks.append(codeBlockContent)
                    codeBlockContent = ""
                    inCodeBlock = false
                } else {
                    // Start of code block
                    if !currentBlock.isEmpty {
                        blocks.append(currentBlock)
                        currentBlock = ""
                    }
                    codeBlockContent = line + "\n"
                    inCodeBlock = true
                }
            } else if inCodeBlock {
                codeBlockContent += line + "\n"
            } else {
                currentBlock += (currentBlock.isEmpty ? "" : "\n") + line
            }
        }

        if !currentBlock.isEmpty {
            blocks.append(currentBlock)
        }
        if !codeBlockContent.isEmpty {
            blocks.append(codeBlockContent)
        }

        return blocks
    }
}

/// Code block with syntax highlighting placeholder
struct CodeBlockView: View {
    let content: String
    @State private var isCopied = false

    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            // Header with language and copy button
            HStack {
                Text(extractLanguage())
                    .font(.caption)
                    .foregroundColor(.secondary)

                Spacer()

                Button {
                    copyToClipboard()
                } label: {
                    Image(systemName: isCopied ? "checkmark" : "doc.on.doc")
                        .font(.caption)
                }
                .buttonStyle(.plain)
                .foregroundColor(.secondary)
            }
            .padding(.horizontal, 12)
            .padding(.vertical, 6)
            .background(Color(nsColor: .controlBackgroundColor))

            // Code content
            ScrollView(.horizontal, showsIndicators: false) {
                Text(extractCode())
                    .font(.system(.body, design: .monospaced))
                    .textSelection(.enabled)
                    .padding(12)
            }
            .background(Color(nsColor: .textBackgroundColor))
        }
        .cornerRadius(8)
        .overlay(
            RoundedRectangle(cornerRadius: 8)
                .stroke(Color.secondary.opacity(0.2), lineWidth: 1)
        )
    }

    private func extractLanguage() -> String {
        let firstLine = content.components(separatedBy: "\n").first ?? ""
        let lang = firstLine.replacingOccurrences(of: "```", with: "").trimmingCharacters(in: .whitespaces)
        return lang.isEmpty ? "code" : lang
    }

    private func extractCode() -> String {
        var lines = content.components(separatedBy: "\n")
        if lines.first?.hasPrefix("```") == true {
            lines.removeFirst()
        }
        if lines.last?.hasPrefix("```") == true {
            lines.removeLast()
        }
        return lines.joined(separator: "\n")
    }

    private func copyToClipboard() {
        let code = extractCode()
        NSPasteboard.general.clearContents()
        NSPasteboard.general.setString(code, forType: .string)

        isCopied = true
        DispatchQueue.main.asyncAfter(deadline: .now() + 2) {
            isCopied = false
        }
    }
}

/// Tool calls display
struct ToolCallsView: View {
    let toolCalls: [ToolCall]
    @State private var expandedIds: Set<UUID> = []

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            Text("Tool Calls")
                .font(.caption)
                .foregroundColor(.secondary)
                .textCase(.uppercase)

            ForEach(toolCalls) { toolCall in
                ToolCallCardView(
                    toolCall: toolCall,
                    isExpanded: expandedIds.contains(toolCall.id)
                ) {
                    if expandedIds.contains(toolCall.id) {
                        expandedIds.remove(toolCall.id)
                    } else {
                        expandedIds.insert(toolCall.id)
                    }
                }
            }
        }
        .padding(12)
        .background(Color.orange.opacity(0.1))
        .cornerRadius(8)
    }
}

/// Individual tool call card
struct ToolCallCardView: View {
    let toolCall: ToolCall
    let isExpanded: Bool
    let onToggle: () -> Void

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            Button(action: onToggle) {
                HStack {
                    if let toolType = toolCall.toolType {
                        Image(systemName: toolType.icon)
                            .foregroundColor(.orange)
                    }

                    Text(toolCall.name)
                        .font(.subheadline.weight(.medium))

                    Spacer()

                    Image(systemName: isExpanded ? "chevron.up" : "chevron.down")
                        .font(.caption)
                        .foregroundColor(.secondary)
                }
            }
            .buttonStyle(.plain)

            if isExpanded {
                VStack(alignment: .leading, spacing: 4) {
                    ForEach(Array(toolCall.parameters.keys.sorted()), id: \.self) { key in
                        HStack(alignment: .top, spacing: 8) {
                            Text(key)
                                .font(.caption)
                                .foregroundColor(.secondary)
                                .frame(width: 80, alignment: .trailing)

                            Text(toolCall.parameters[key] ?? "")
                                .font(.system(.caption, design: .monospaced))
                                .textSelection(.enabled)
                        }
                    }
                }
                .padding(8)
                .background(Color(nsColor: .textBackgroundColor))
                .cornerRadius(6)
            }
        }
    }
}

/// Tool results display
struct ToolResultsView: View {
    let results: [ToolResult]
    @State private var expandedIds: Set<UUID> = []

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            Text("Results")
                .font(.caption)
                .foregroundColor(.secondary)
                .textCase(.uppercase)

            ForEach(results) { result in
                ToolResultCardView(
                    result: result,
                    isExpanded: expandedIds.contains(result.id)
                ) {
                    if expandedIds.contains(result.id) {
                        expandedIds.remove(result.id)
                    } else {
                        expandedIds.insert(result.id)
                    }
                }
            }
        }
        .padding(12)
        .background(Color.green.opacity(0.1))
        .cornerRadius(8)
    }
}

/// Individual tool result card
struct ToolResultCardView: View {
    let result: ToolResult
    let isExpanded: Bool
    let onToggle: () -> Void

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            Button(action: onToggle) {
                HStack {
                    Image(systemName: result.success ? "checkmark.circle.fill" : "xmark.circle.fill")
                        .foregroundColor(result.success ? .green : .red)

                    Text(result.success ? "Success" : "Failed")
                        .font(.subheadline.weight(.medium))

                    if let time = result.executionTime {
                        Text(String(format: "%.2fs", time))
                            .font(.caption)
                            .foregroundColor(.secondary)
                    }

                    Spacer()

                    Image(systemName: isExpanded ? "chevron.up" : "chevron.down")
                        .font(.caption)
                        .foregroundColor(.secondary)
                }
            }
            .buttonStyle(.plain)

            if isExpanded {
                ScrollView {
                    Text(result.displayOutput)
                        .font(.system(.caption, design: .monospaced))
                        .textSelection(.enabled)
                        .frame(maxWidth: .infinity, alignment: .leading)
                }
                .frame(maxHeight: 200)
                .padding(8)
                .background(Color(nsColor: .textBackgroundColor))
                .cornerRadius(6)
            }
        }
    }
}

#Preview {
    VStack(spacing: 20) {
        MessageView(message: .user("Hello, can you help me find all Swift files?"))
        MessageView(message: .assistant("I'll help you find all Swift files in the project."))
    }
    .padding()
}
