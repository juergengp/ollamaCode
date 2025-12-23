import Foundation
import SQLite

/// Service for managing conversation sessions
@MainActor
class SessionService: ObservableObject {
    static let shared = SessionService()

    @Published var sessions: [Session] = []

    private var db: Connection?
    private let sessionsTable = Table("sessions")
    private let idColumn = Expression<String>("id")
    private let nameColumn = Expression<String>("name")
    private let agentTypeColumn = Expression<String>("agent_type")
    private let modelColumn = Expression<String>("model")
    private let createdAtColumn = Expression<String>("created_at")
    private let updatedAtColumn = Expression<String>("updated_at")
    private let messageCountColumn = Expression<Int>("message_count")
    private let previewColumn = Expression<String>("preview")
    private let dataColumn = Expression<String>("data")

    private let dateFormatter: ISO8601DateFormatter = {
        let formatter = ISO8601DateFormatter()
        formatter.formatOptions = [.withInternetDateTime, .withFractionalSeconds]
        return formatter
    }()

    private init() {
        initializeDatabase()
        loadSessions()
    }

    private func initializeDatabase() {
        let homeDir = FileManager.default.homeDirectoryForCurrentUser
        let configDir = homeDir.appendingPathComponent(".config/ollamacode")
        let dbPath = configDir.appendingPathComponent("config.db")

        do {
            try FileManager.default.createDirectory(at: configDir, withIntermediateDirectories: true)
            db = try Connection(dbPath.path)

            try db?.run(sessionsTable.create(ifNotExists: true) { t in
                t.column(idColumn, primaryKey: true)
                t.column(nameColumn)
                t.column(agentTypeColumn)
                t.column(modelColumn)
                t.column(createdAtColumn)
                t.column(updatedAtColumn)
                t.column(messageCountColumn)
                t.column(previewColumn)
                t.column(dataColumn)
            })
        } catch {
            print("Session database error: \(error)")
        }
    }

    // MARK: - Session Management

    func loadSessions() {
        guard let db = db else { return }

        do {
            sessions = try db.prepare(sessionsTable.order(updatedAtColumn.desc)).map { row in
                Session(
                    id: UUID(uuidString: row[idColumn]) ?? UUID(),
                    name: row[nameColumn],
                    agentType: row[agentTypeColumn],
                    model: row[modelColumn],
                    createdAt: dateFormatter.date(from: row[createdAtColumn]) ?? Date(),
                    updatedAt: dateFormatter.date(from: row[updatedAtColumn]) ?? Date(),
                    messageCount: row[messageCountColumn],
                    preview: row[previewColumn]
                )
            }
        } catch {
            print("Error loading sessions: \(error)")
        }
    }

    func saveSession(_ sessionData: SessionData) {
        guard let db = db else { return }

        do {
            let encoder = JSONEncoder()
            encoder.dateEncodingStrategy = .iso8601
            let jsonData = try encoder.encode(sessionData.messages)
            let jsonString = String(data: jsonData, encoding: .utf8) ?? "[]"

            let session = sessionData.session

            // Check if session exists
            let existingCount = try db.scalar(sessionsTable.filter(idColumn == session.id.uuidString).count)

            if existingCount > 0 {
                // Update existing
                try db.run(sessionsTable.filter(idColumn == session.id.uuidString).update(
                    nameColumn <- session.name,
                    agentTypeColumn <- session.agentType,
                    modelColumn <- session.model,
                    updatedAtColumn <- dateFormatter.string(from: Date()),
                    messageCountColumn <- session.messageCount,
                    previewColumn <- session.preview,
                    dataColumn <- jsonString
                ))
            } else {
                // Insert new
                try db.run(sessionsTable.insert(
                    idColumn <- session.id.uuidString,
                    nameColumn <- session.name,
                    agentTypeColumn <- session.agentType,
                    modelColumn <- session.model,
                    createdAtColumn <- dateFormatter.string(from: session.createdAt),
                    updatedAtColumn <- dateFormatter.string(from: session.updatedAt),
                    messageCountColumn <- session.messageCount,
                    previewColumn <- session.preview,
                    dataColumn <- jsonString
                ))
            }

            loadSessions()
        } catch {
            print("Error saving session: \(error)")
        }
    }

    func loadSession(_ id: UUID) -> SessionData? {
        guard let db = db else { return nil }

        do {
            guard let row = try db.pluck(sessionsTable.filter(idColumn == id.uuidString)) else {
                return nil
            }

            let session = Session(
                id: UUID(uuidString: row[idColumn]) ?? UUID(),
                name: row[nameColumn],
                agentType: row[agentTypeColumn],
                model: row[modelColumn],
                createdAt: dateFormatter.date(from: row[createdAtColumn]) ?? Date(),
                updatedAt: dateFormatter.date(from: row[updatedAtColumn]) ?? Date(),
                messageCount: row[messageCountColumn],
                preview: row[previewColumn]
            )

            let jsonString = row[dataColumn]
            guard let jsonData = jsonString.data(using: .utf8) else {
                return SessionData(session: session, messages: [])
            }

            let decoder = JSONDecoder()
            decoder.dateDecodingStrategy = .iso8601
            let conversationMessages = try decoder.decode([ConversationMessage].self, from: jsonData)
            let messages = conversationMessages.map { $0.toMessage() }

            return SessionData(session: session, messages: messages)
        } catch {
            print("Error loading session: \(error)")
            return nil
        }
    }

    func deleteSession(_ id: UUID) {
        guard let db = db else { return }

        do {
            try db.run(sessionsTable.filter(idColumn == id.uuidString).delete())
            loadSessions()
        } catch {
            print("Error deleting session: \(error)")
        }
    }

    func renameSession(_ id: UUID, to newName: String) {
        guard let db = db else { return }

        do {
            try db.run(sessionsTable.filter(idColumn == id.uuidString).update(
                nameColumn <- newName,
                updatedAtColumn <- dateFormatter.string(from: Date())
            ))
            loadSessions()
        } catch {
            print("Error renaming session: \(error)")
        }
    }

    // MARK: - Export

    func exportSession(_ id: UUID, format: SessionExportFormat) -> String? {
        guard let sessionData = loadSession(id) else { return nil }
        return SessionExporter.export(sessionData: sessionData, format: format)
    }

    func exportSessionToFile(_ id: UUID, format: SessionExportFormat) -> URL? {
        guard let content = exportSession(id, format: format) else { return nil }

        let session = sessions.first { $0.id == id }
        let fileName = (session?.name ?? "session")
            .replacingOccurrences(of: "/", with: "-")
            .replacingOccurrences(of: ":", with: "-")

        let tempDir = FileManager.default.temporaryDirectory
        let fileURL = tempDir.appendingPathComponent("\(fileName).\(format.fileExtension)")

        do {
            try content.write(to: fileURL, atomically: true, encoding: .utf8)
            return fileURL
        } catch {
            print("Error exporting session: \(error)")
            return nil
        }
    }

    // MARK: - Search

    func searchSessions(query: String) -> [Session] {
        guard !query.isEmpty else { return sessions }

        let lowercasedQuery = query.lowercased()
        return sessions.filter {
            $0.name.lowercased().contains(lowercasedQuery) ||
            $0.preview.lowercased().contains(lowercasedQuery)
        }
    }
}
