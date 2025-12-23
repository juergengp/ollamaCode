import SwiftUI

/// Main application view with sidebar and chat area
struct MainView: View {
    @EnvironmentObject var appState: AppState
    @StateObject private var chatViewModel = ChatViewModel()
    @State private var showingSidebar = true
    @State private var selectedSessionId: UUID?

    var body: some View {
        NavigationSplitView(columnVisibility: .constant(.all)) {
            SidebarView(selectedSessionId: $selectedSessionId)
                .navigationSplitViewColumnWidth(min: 200, ideal: 250, max: 300)
        } detail: {
            ChatView(viewModel: chatViewModel)
                .navigationTitle(navigationTitle)
                .toolbar {
                    ToolbarItemGroup(placement: .primaryAction) {
                        toolbarContent
                    }
                }
        }
        .sheet(isPresented: $chatViewModel.showToolConfirmation) {
            ToolConfirmationSheet(viewModel: chatViewModel)
        }
        .alert("Error", isPresented: .constant(chatViewModel.error != nil)) {
            Button("OK") {
                chatViewModel.error = nil
            }
        } message: {
            if let error = chatViewModel.error {
                Text(error)
            }
        }
        .environmentObject(chatViewModel)
    }

    private var navigationTitle: String {
        "\(appState.currentAgent.emoji) \(appState.currentAgent.displayName)"
    }

    @ViewBuilder
    private var toolbarContent: some View {
        // Connection status and model
        HStack(spacing: 6) {
            Circle()
                .fill(appState.isConnected ? Color.green : Color.red)
                .frame(width: 8, height: 8)

            Menu {
                ForEach(appState.availableModels, id: \.self) { model in
                    Button(model) {
                        appState.selectModel(model)
                    }
                }

                Divider()

                Button("Refresh Models") {
                    Task {
                        await appState.refreshModels()
                    }
                }
            } label: {
                HStack(spacing: 4) {
                    Image(systemName: "cpu")
                        .font(.caption)
                    Text(appState.currentModel)
                        .font(.system(.body, design: .rounded))
                        .fontWeight(.medium)
                    Image(systemName: "chevron.down")
                        .font(.caption2)
                }
                .padding(.horizontal, 8)
                .padding(.vertical, 4)
                .background(Color.accentColor.opacity(0.1))
                .cornerRadius(6)
            }
            .buttonStyle(.plain)
        }

        Divider()

        // Agent selector
        Menu {
            ForEach(AgentType.allCases) { agent in
                Button {
                    appState.selectAgent(agent)
                    chatViewModel.selectAgent(agent)
                } label: {
                    Label(agent.displayName, systemImage: agent.icon)
                }
            }
        } label: {
            Label("Agent", systemImage: appState.currentAgent.icon)
        }

        // Clear button
        Button {
            chatViewModel.clearConversation()
        } label: {
            Label("Clear", systemImage: "trash")
        }
        .keyboardShortcut("k", modifiers: .command)
    }
}

/// Sidebar with session list
struct SidebarView: View {
    @Binding var selectedSessionId: UUID?
    @State private var sessions: [Session] = []

    var body: some View {
        List(selection: $selectedSessionId) {
            Section("Conversations") {
                if sessions.isEmpty {
                    Text("No saved conversations")
                        .foregroundColor(.secondary)
                        .italic()
                } else {
                    ForEach(sessions) { session in
                        SessionRowView(session: session)
                            .tag(session.id)
                    }
                }
            }
        }
        .listStyle(.sidebar)
        .navigationTitle("OllamaCode")
        .toolbar {
            ToolbarItem(placement: .primaryAction) {
                Button {
                    // New conversation
                    NotificationCenter.default.post(name: .newConversation, object: nil)
                } label: {
                    Label("New", systemImage: "plus")
                }
            }
        }
    }
}

/// Session row in sidebar
struct SessionRowView: View {
    let session: Session

    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            Text(session.name)
                .font(.headline)
                .lineLimit(1)

            HStack {
                Text(session.agentType.capitalized)
                    .font(.caption2)
                    .padding(.horizontal, 6)
                    .padding(.vertical, 2)
                    .background(Color.accentColor.opacity(0.2))
                    .cornerRadius(4)

                Spacer()

                Text(formatDate(session.updatedAt))
                    .font(.caption2)
                    .foregroundColor(.secondary)
            }
        }
        .padding(.vertical, 4)
    }

    private func formatDate(_ date: Date) -> String {
        let formatter = RelativeDateTimeFormatter()
        formatter.unitsStyle = .abbreviated
        return formatter.localizedString(for: date, relativeTo: Date())
    }
}

/// Tool confirmation sheet
struct ToolConfirmationSheet: View {
    @ObservedObject var viewModel: ChatViewModel
    @Environment(\.dismiss) var dismiss

    var body: some View {
        VStack(spacing: 0) {
            // Header
            HStack {
                Image(systemName: "wrench.and.screwdriver")
                    .font(.title2)
                Text("Tool Execution Request")
                    .font(.headline)
                Spacer()
            }
            .padding()
            .background(Color(nsColor: .windowBackgroundColor))

            Divider()

            // Tool list
            ScrollView {
                LazyVStack(spacing: 12) {
                    ForEach(viewModel.pendingToolExecutions) { execution in
                        ToolExecutionRowView(
                            execution: execution,
                            onApprove: { viewModel.approveTool(execution.id) },
                            onDeny: { viewModel.denyTool(execution.id) }
                        )
                    }
                }
                .padding()
            }

            Divider()

            // Action buttons
            HStack {
                Button("Deny All") {
                    viewModel.denyAllTools()
                    Task {
                        await viewModel.executeApprovedTools()
                    }
                    dismiss()
                }
                .keyboardShortcut(.escape, modifiers: [])

                Spacer()

                Button("Approve All") {
                    viewModel.approveAllTools()
                    Task {
                        await viewModel.executeApprovedTools()
                    }
                    dismiss()
                }
                .keyboardShortcut(.return, modifiers: .command)
                .buttonStyle(.borderedProminent)
            }
            .padding()
        }
        .frame(minWidth: 500, minHeight: 400)
    }
}

/// Individual tool execution row
struct ToolExecutionRowView: View {
    let execution: PendingToolExecution
    let onApprove: () -> Void
    let onDeny: () -> Void

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            HStack {
                if let toolType = execution.toolCall.toolType {
                    Image(systemName: toolType.icon)
                        .foregroundColor(.accentColor)
                }
                Text(execution.toolCall.name)
                    .font(.headline)

                Spacer()

                statusBadge
            }

            // Parameters
            VStack(alignment: .leading, spacing: 4) {
                ForEach(Array(execution.toolCall.parameters.keys.sorted()), id: \.self) { key in
                    HStack(alignment: .top) {
                        Text(key + ":")
                            .font(.caption)
                            .foregroundColor(.secondary)
                            .frame(width: 80, alignment: .trailing)

                        Text(execution.toolCall.parameters[key] ?? "")
                            .font(.system(.caption, design: .monospaced))
                            .lineLimit(3)
                    }
                }
            }
            .padding(8)
            .background(Color(nsColor: .textBackgroundColor))
            .cornerRadius(6)

            if execution.status == .awaitingConfirmation {
                HStack {
                    Spacer()
                    Button("Deny", action: onDeny)
                    Button("Approve", action: onApprove)
                        .buttonStyle(.borderedProminent)
                }
            }
        }
        .padding()
        .background(Color(nsColor: .controlBackgroundColor))
        .cornerRadius(8)
    }

    @ViewBuilder
    private var statusBadge: some View {
        switch execution.status {
        case .pending:
            Text("Pending")
                .statusBadgeStyle(color: .gray)
        case .awaitingConfirmation:
            Text("Awaiting")
                .statusBadgeStyle(color: .orange)
        case .approved:
            Text("Approved")
                .statusBadgeStyle(color: .green)
        case .denied:
            Text("Denied")
                .statusBadgeStyle(color: .red)
        case .running:
            HStack(spacing: 4) {
                ProgressView()
                    .scaleEffect(0.5)
                Text("Running")
            }
            .statusBadgeStyle(color: .blue)
        case .completed:
            Text("Done")
                .statusBadgeStyle(color: .green)
        case .failed(let error):
            Text("Failed")
                .statusBadgeStyle(color: .red)
        }
    }
}

extension View {
    func statusBadgeStyle(color: Color) -> some View {
        self.font(.caption2)
            .padding(.horizontal, 8)
            .padding(.vertical, 4)
            .background(color.opacity(0.2))
            .foregroundColor(color)
            .cornerRadius(4)
    }
}

#Preview {
    MainView()
        .environmentObject(AppState())
}
