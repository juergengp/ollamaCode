import SwiftUI

/// Settings view for application preferences
struct SettingsView: View {
    @EnvironmentObject var appState: AppState

    var body: some View {
        TabView {
            GeneralSettingsView()
                .tabItem {
                    Label("General", systemImage: "gear")
                }

            ModelSettingsView()
                .tabItem {
                    Label("Model", systemImage: "cpu")
                }

            SafetySettingsView()
                .tabItem {
                    Label("Safety", systemImage: "shield")
                }

            MCPSettingsView()
                .tabItem {
                    Label("MCP", systemImage: "link")
                }
        }
        .frame(width: 500, height: 400)
    }
}

/// General settings tab
struct GeneralSettingsView: View {
    @ObservedObject private var config = ConfigService.shared
    @State private var ollamaHost: String = ""

    var body: some View {
        Form {
            Section {
                TextField("Ollama Host", text: $ollamaHost)
                    .textFieldStyle(.roundedBorder)
                    .onAppear {
                        ollamaHost = config.ollamaHost
                    }
                    .onChange(of: ollamaHost) { newValue in
                        config.ollamaHost = newValue
                    }

                Text("Default: http://localhost:11434")
                    .font(.caption)
                    .foregroundColor(.secondary)
            } header: {
                Text("Connection")
            }

            Section {
                HStack {
                    Text("Temperature")
                    Spacer()
                    Text(String(format: "%.1f", config.temperature))
                        .foregroundColor(.secondary)
                }

                Slider(value: $config.temperature, in: 0...2, step: 0.1)
                    .onChange(of: config.temperature) { _ in
                        config.save()
                    }

                Text("Lower values produce more focused responses, higher values more creative.")
                    .font(.caption)
                    .foregroundColor(.secondary)
            } header: {
                Text("Response Settings")
            }

            Section {
                Stepper(value: $config.maxTokens, in: 256...32768, step: 256) {
                    HStack {
                        Text("Max Tokens")
                        Spacer()
                        Text("\(config.maxTokens)")
                            .foregroundColor(.secondary)
                    }
                }
                .onChange(of: config.maxTokens) { _ in
                    config.save()
                }

                Text("Maximum length of AI responses.")
                    .font(.caption)
                    .foregroundColor(.secondary)
            }
        }
        .formStyle(.grouped)
        .padding()
    }
}

/// Model settings tab
struct ModelSettingsView: View {
    @ObservedObject private var config = ConfigService.shared
    @State private var availableModels: [OllamaModel] = []
    @State private var isLoading = false
    @State private var error: String?

    var body: some View {
        Form {
            Section {
                Picker("Current Model", selection: $config.model) {
                    if availableModels.isEmpty {
                        Text(config.model).tag(config.model)
                    } else {
                        ForEach(availableModels) { model in
                            Text(model.name).tag(model.name)
                        }
                    }
                }
                .onChange(of: config.model) { _ in
                    config.save()
                }

                Button("Refresh Models") {
                    Task {
                        await loadModels()
                    }
                }
                .disabled(isLoading)

                if isLoading {
                    ProgressView()
                        .scaleEffect(0.8)
                }

                if let error = error {
                    Text(error)
                        .font(.caption)
                        .foregroundColor(.red)
                }
            } header: {
                Text("Available Models")
            }

            if !availableModels.isEmpty {
                Section {
                    ForEach(availableModels) { model in
                        HStack {
                            VStack(alignment: .leading) {
                                Text(model.displayName)
                                    .font(.headline)
                                if let tag = model.tag {
                                    Text(tag)
                                        .font(.caption)
                                        .foregroundColor(.secondary)
                                }
                            }

                            Spacer()

                            if let size = model.size {
                                Text(formatSize(size))
                                    .font(.caption)
                                    .foregroundColor(.secondary)
                            }

                            if model.name == config.model {
                                Image(systemName: "checkmark.circle.fill")
                                    .foregroundColor(.green)
                            }
                        }
                        .contentShape(Rectangle())
                        .onTapGesture {
                            config.model = model.name
                            config.save()
                        }
                    }
                } header: {
                    Text("Installed Models")
                }
            }
        }
        .formStyle(.grouped)
        .padding()
        .onAppear {
            Task {
                await loadModels()
            }
        }
    }

    private func loadModels() async {
        isLoading = true
        error = nil

        do {
            availableModels = try await OllamaService.shared.listModels()
        } catch {
            self.error = error.localizedDescription
        }

        isLoading = false
    }

    private func formatSize(_ bytes: Int64) -> String {
        let formatter = ByteCountFormatter()
        formatter.countStyle = .file
        return formatter.string(fromByteCount: bytes)
    }
}

/// Safety settings tab
struct SafetySettingsView: View {
    @ObservedObject private var config = ConfigService.shared
    @State private var newCommand: String = ""

    var body: some View {
        Form {
            Section {
                Toggle("Safe Mode", isOn: $config.safeMode)
                    .onChange(of: config.safeMode) { _ in
                        config.save()
                    }

                Text("When enabled, only whitelisted commands can be executed automatically.")
                    .font(.caption)
                    .foregroundColor(.secondary)
            } header: {
                Text("Execution Safety")
            }

            Section {
                Toggle("Auto-Approve Read Operations", isOn: $config.autoApprove)
                    .onChange(of: config.autoApprove) { _ in
                        config.save()
                    }

                Text("Automatically approve safe read-only operations without confirmation.")
                    .font(.caption)
                    .foregroundColor(.secondary)
            }

            Section {
                HStack {
                    TextField("Add command", text: $newCommand)
                        .textFieldStyle(.roundedBorder)

                    Button("Add") {
                        if !newCommand.isEmpty {
                            config.addAllowedCommand(newCommand)
                            newCommand = ""
                        }
                    }
                    .disabled(newCommand.isEmpty)
                }

                ScrollView {
                    LazyVStack(alignment: .leading, spacing: 4) {
                        ForEach(Array(config.allowedCommands).sorted(), id: \.self) { command in
                            HStack {
                                Text(command)
                                    .font(.system(.body, design: .monospaced))

                                Spacer()

                                Button {
                                    config.removeAllowedCommand(command)
                                } label: {
                                    Image(systemName: "xmark.circle")
                                        .foregroundColor(.secondary)
                                }
                                .buttonStyle(.plain)
                            }
                            .padding(.vertical, 2)
                        }
                    }
                }
                .frame(height: 150)
            } header: {
                Text("Allowed Commands")
            }
        }
        .formStyle(.grouped)
        .padding()
    }
}

/// MCP settings tab
struct MCPSettingsView: View {
    @ObservedObject private var config = ConfigService.shared
    @State private var mcpServers: [String: MCPServerConfig] = [:]
    @State private var showAddServer = false

    var body: some View {
        Form {
            Section {
                Toggle("Enable MCP", isOn: $config.mcpEnabled)
                    .onChange(of: config.mcpEnabled) { _ in
                        config.save()
                    }

                Text("Model Context Protocol allows connecting to external tools and services.")
                    .font(.caption)
                    .foregroundColor(.secondary)
            } header: {
                Text("MCP Settings")
            }

            Section {
                if mcpServers.isEmpty {
                    Text("No MCP servers configured")
                        .foregroundColor(.secondary)
                        .italic()
                } else {
                    ForEach(Array(mcpServers.keys).sorted(), id: \.self) { name in
                        if let server = mcpServers[name] {
                            MCPServerRowView(name: name, server: server) {
                                mcpServers.removeValue(forKey: name)
                                config.saveMCPServers(mcpServers)
                            }
                        }
                    }
                }

                Button("Add Server") {
                    showAddServer = true
                }
            } header: {
                Text("Configured Servers")
            }
        }
        .formStyle(.grouped)
        .padding()
        .onAppear {
            mcpServers = config.loadMCPServers()
        }
        .sheet(isPresented: $showAddServer) {
            AddMCPServerView { name, server in
                mcpServers[name] = server
                config.saveMCPServers(mcpServers)
            }
        }
    }
}

/// MCP server row view
struct MCPServerRowView: View {
    let name: String
    let server: MCPServerConfig
    let onDelete: () -> Void

    var body: some View {
        HStack {
            VStack(alignment: .leading, spacing: 4) {
                Text(name)
                    .font(.headline)

                Text("\(server.command) \(server.args.joined(separator: " "))")
                    .font(.caption)
                    .foregroundColor(.secondary)
                    .lineLimit(1)
            }

            Spacer()

            Circle()
                .fill(server.enabled ? Color.green : Color.gray)
                .frame(width: 8, height: 8)

            Button {
                onDelete()
            } label: {
                Image(systemName: "trash")
                    .foregroundColor(.red)
            }
            .buttonStyle(.plain)
        }
    }
}

/// Add MCP server sheet
struct AddMCPServerView: View {
    @Environment(\.dismiss) var dismiss
    @State private var name: String = ""
    @State private var command: String = "npx"
    @State private var args: String = ""

    let onSave: (String, MCPServerConfig) -> Void

    var body: some View {
        VStack(spacing: 20) {
            Text("Add MCP Server")
                .font(.headline)

            Form {
                TextField("Name", text: $name)
                TextField("Command", text: $command)
                TextField("Arguments (space-separated)", text: $args)
            }

            HStack {
                Button("Cancel") {
                    dismiss()
                }

                Spacer()

                Button("Add") {
                    let argsArray = args.split(separator: " ").map(String.init)
                    let server = MCPServerConfig(command: command, args: argsArray)
                    onSave(name, server)
                    dismiss()
                }
                .buttonStyle(.borderedProminent)
                .disabled(name.isEmpty || command.isEmpty)
            }
        }
        .padding()
        .frame(width: 400)
    }
}

#Preview {
    SettingsView()
        .environmentObject(AppState())
}
