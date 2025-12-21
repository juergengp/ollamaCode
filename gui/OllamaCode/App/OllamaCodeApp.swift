import SwiftUI

@main
struct OllamaCodeApp: App {
    @StateObject private var appState = AppState()

    var body: some Scene {
        WindowGroup {
            MainView()
                .environmentObject(appState)
                .frame(minWidth: 900, minHeight: 600)
        }
        .commands {
            CommandGroup(replacing: .newItem) {
                Button("New Conversation") {
                    appState.newConversation()
                }
                .keyboardShortcut("n", modifiers: .command)
            }

            CommandGroup(after: .newItem) {
                Divider()
                Button("Clear Conversation") {
                    appState.clearConversation()
                }
                .keyboardShortcut("k", modifiers: .command)
            }

            CommandMenu("Agent") {
                ForEach(AgentType.allCases, id: \.self) { agentType in
                    Button(agentType.displayName) {
                        appState.selectAgent(agentType)
                    }
                }
            }
        }

        Settings {
            SettingsView()
                .environmentObject(appState)
        }
    }
}

/// Global application state
@MainActor
class AppState: ObservableObject {
    @Published var currentAgent: AgentType = .general
    @Published var isConnected: Bool = false
    @Published var currentModel: String = "llama3"

    private let configService = ConfigService.shared
    private let ollamaService = OllamaService.shared

    init() {
        loadConfiguration()
        Task {
            await checkConnection()
        }
    }

    func loadConfiguration() {
        currentModel = configService.model
    }

    func checkConnection() async {
        isConnected = await ollamaService.testConnection()
    }

    func selectAgent(_ agent: AgentType) {
        currentAgent = agent
    }

    func newConversation() {
        NotificationCenter.default.post(name: .newConversation, object: nil)
    }

    func clearConversation() {
        NotificationCenter.default.post(name: .clearConversation, object: nil)
    }
}

extension Notification.Name {
    static let newConversation = Notification.Name("newConversation")
    static let clearConversation = Notification.Name("clearConversation")
}
