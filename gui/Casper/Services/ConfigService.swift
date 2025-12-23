import Foundation
import SQLite

/// Configuration service that shares the SQLite database with the CLI version
/// Database location: ~/.config/ollamacode/config.db
@MainActor
class ConfigService: ObservableObject {
    static let shared = ConfigService()

    // MARK: - Published Properties

    @Published var model: String = "llama3"
    @Published var ollamaHost: String = "http://localhost:11434"
    @Published var temperature: Double = 0.7
    @Published var maxTokens: Int = 4096
    @Published var safeMode: Bool = true
    @Published var autoApprove: Bool = false
    @Published var mcpEnabled: Bool = false
    @Published var allowedCommands: Set<String> = []

    // MARK: - Private Properties

    private var db: Connection?
    private let configDir: URL
    private let dbPath: URL

    // SQLite table definitions
    private let configTable = Table("config")
    private let keyColumn = Expression<String>("key")
    private let valueColumn = Expression<String>("value")

    private let allowedCommandsTable = Table("allowed_commands")
    private let commandColumn = Expression<String>("command")

    // MARK: - Default allowed commands (same as CLI)

    private static let defaultAllowedCommands: Set<String> = [
        "ls", "cat", "head", "tail", "grep", "find", "git", "docker",
        "kubectl", "systemctl", "journalctl", "pwd", "whoami", "date",
        "echo", "which", "ps", "df", "du", "wc", "sort", "uniq", "tree"
    ]

    // MARK: - Initialization

    private init() {
        // Set up config directory path
        let homeDir = FileManager.default.homeDirectoryForCurrentUser
        configDir = homeDir.appendingPathComponent(".config/ollamacode")
        dbPath = configDir.appendingPathComponent("config.db")

        initializeDatabase()
        loadConfiguration()
    }

    // MARK: - Database Setup

    private func initializeDatabase() {
        do {
            // Create config directory if it doesn't exist
            try FileManager.default.createDirectory(at: configDir, withIntermediateDirectories: true)

            // Connect to database
            db = try Connection(dbPath.path)

            // Create tables if they don't exist
            try db?.run(configTable.create(ifNotExists: true) { t in
                t.column(keyColumn, primaryKey: true)
                t.column(valueColumn)
            })

            try db?.run(allowedCommandsTable.create(ifNotExists: true) { t in
                t.column(commandColumn, primaryKey: true)
            })

            // Initialize default allowed commands if table is empty
            let count = try db?.scalar(allowedCommandsTable.count) ?? 0
            if count == 0 {
                for command in Self.defaultAllowedCommands {
                    try db?.run(allowedCommandsTable.insert(or: .ignore, commandColumn <- command))
                }
            }

        } catch {
            print("Database initialization error: \(error)")
        }
    }

    // MARK: - Load Configuration

    private func loadConfiguration() {
        guard let db = db else { return }

        do {
            // Load config values
            for row in try db.prepare(configTable) {
                let key = row[keyColumn]
                let value = row[valueColumn]

                switch key {
                case "model":
                    model = value
                case "ollama_host":
                    ollamaHost = value
                case "temperature":
                    temperature = Double(value) ?? 0.7
                case "max_tokens":
                    maxTokens = Int(value) ?? 4096
                case "safe_mode":
                    safeMode = value == "true" || value == "1"
                case "auto_approve":
                    autoApprove = value == "true" || value == "1"
                case "mcp_enabled":
                    mcpEnabled = value == "true" || value == "1"
                default:
                    break
                }
            }

            // Load allowed commands
            allowedCommands.removeAll()
            for row in try db.prepare(allowedCommandsTable) {
                allowedCommands.insert(row[commandColumn])
            }

        } catch {
            print("Error loading configuration: \(error)")
        }
    }

    // MARK: - Save Configuration

    func save() {
        guard let db = db else { return }

        do {
            try saveValue(key: "model", value: model)
            try saveValue(key: "ollama_host", value: ollamaHost)
            try saveValue(key: "temperature", value: String(temperature))
            try saveValue(key: "max_tokens", value: String(maxTokens))
            try saveValue(key: "safe_mode", value: safeMode ? "true" : "false")
            try saveValue(key: "auto_approve", value: autoApprove ? "true" : "false")
            try saveValue(key: "mcp_enabled", value: mcpEnabled ? "true" : "false")
        } catch {
            print("Error saving configuration: \(error)")
        }
    }

    private func saveValue(key: String, value: String) throws {
        guard let db = db else { return }

        let query = configTable.filter(keyColumn == key)
        let count = try db.scalar(query.count)

        if count > 0 {
            try db.run(query.update(valueColumn <- value))
        } else {
            try db.run(configTable.insert(keyColumn <- key, valueColumn <- value))
        }
    }

    // MARK: - Allowed Commands Management

    func addAllowedCommand(_ command: String) {
        guard let db = db else { return }

        do {
            try db.run(allowedCommandsTable.insert(or: .ignore, commandColumn <- command))
            allowedCommands.insert(command)
        } catch {
            print("Error adding allowed command: \(error)")
        }
    }

    func removeAllowedCommand(_ command: String) {
        guard let db = db else { return }

        do {
            let query = allowedCommandsTable.filter(commandColumn == command)
            try db.run(query.delete())
            allowedCommands.remove(command)
        } catch {
            print("Error removing allowed command: \(error)")
        }
    }

    func isCommandAllowed(_ command: String) -> Bool {
        // Extract the base command (first word)
        let baseCommand = command.split(separator: " ").first.map(String.init) ?? command
        return allowedCommands.contains(baseCommand)
    }

    // MARK: - MCP Configuration

    var mcpConfigPath: URL {
        configDir.appendingPathComponent("mcp_servers.json")
    }

    func loadMCPServers() -> [String: MCPServerConfig] {
        guard FileManager.default.fileExists(atPath: mcpConfigPath.path) else {
            return [:]
        }

        do {
            let data = try Data(contentsOf: mcpConfigPath)
            let wrapper = try JSONDecoder().decode(MCPConfigWrapper.self, from: data)
            return wrapper.mcpServers
        } catch {
            print("Error loading MCP servers: \(error)")
            return [:]
        }
    }

    func saveMCPServers(_ servers: [String: MCPServerConfig]) {
        do {
            let wrapper = MCPConfigWrapper(mcpServers: servers)
            let encoder = JSONEncoder()
            encoder.outputFormatting = [.prettyPrinted, .sortedKeys]
            let data = try encoder.encode(wrapper)
            try data.write(to: mcpConfigPath)
        } catch {
            print("Error saving MCP servers: \(error)")
        }
    }
}

// MARK: - MCP Configuration Structures

struct MCPConfigWrapper: Codable {
    let mcpServers: [String: MCPServerConfig]
}

struct MCPServerConfig: Codable, Identifiable {
    var id: String { command + args.joined() }

    let command: String
    let args: [String]
    var env: [String: String]?
    var enabled: Bool
    var transport: String?

    enum CodingKeys: String, CodingKey {
        case command, args, env, enabled, transport
    }

    init(command: String, args: [String], env: [String: String]? = nil, enabled: Bool = true, transport: String? = "stdio") {
        self.command = command
        self.args = args
        self.env = env
        self.enabled = enabled
        self.transport = transport
    }
}
