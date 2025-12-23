import Foundation

/// Service for communicating with Ollama API
/// Supports both local and remote Ollama instances
actor OllamaService {
    static let shared = OllamaService()

    private let session: URLSession
    private let timeout: TimeInterval = 300 // 5 minutes for long responses

    private init() {
        let config = URLSessionConfiguration.default
        config.timeoutIntervalForRequest = timeout
        config.timeoutIntervalForResource = timeout
        self.session = URLSession(configuration: config)
    }

    // MARK: - Configuration

    @MainActor
    var baseURL: String {
        ConfigService.shared.ollamaHost
    }

    // MARK: - Connection Test

    func testConnection() async -> Bool {
        let host = await baseURL
        guard let url = URL(string: "\(host)/api/tags") else {
            return false
        }

        do {
            let (_, response) = try await session.data(from: url)
            guard let httpResponse = response as? HTTPURLResponse else {
                return false
            }
            return httpResponse.statusCode == 200
        } catch {
            return false
        }
    }

    // MARK: - List Models

    func listModels() async throws -> [OllamaModel] {
        let host = await baseURL
        guard let url = URL(string: "\(host)/api/tags") else {
            throw OllamaError.invalidURL
        }

        let (data, response) = try await session.data(from: url)

        guard let httpResponse = response as? HTTPURLResponse,
              httpResponse.statusCode == 200 else {
            throw OllamaError.serverError
        }

        let result = try JSONDecoder().decode(ModelsResponse.self, from: data)
        return result.models
    }

    // MARK: - Chat Completion

    func chat(
        model: String,
        messages: [ChatMessage],
        temperature: Double = 0.7,
        maxTokens: Int = 4096,
        stream: Bool = false
    ) async throws -> ChatResponse {
        let host = await baseURL
        guard let url = URL(string: "\(host)/api/chat") else {
            throw OllamaError.invalidURL
        }

        var request = URLRequest(url: url)
        request.httpMethod = "POST"
        request.setValue("application/json", forHTTPHeaderField: "Content-Type")

        let body = ChatRequest(
            model: model,
            messages: messages,
            stream: stream,
            options: ChatOptions(temperature: temperature, num_predict: maxTokens)
        )

        request.httpBody = try JSONEncoder().encode(body)

        let (data, response) = try await session.data(for: request)

        guard let httpResponse = response as? HTTPURLResponse else {
            throw OllamaError.serverError
        }

        guard httpResponse.statusCode == 200 else {
            let errorMessage = String(data: data, encoding: .utf8) ?? "Unknown error"
            throw OllamaError.apiError(httpResponse.statusCode, errorMessage)
        }

        return try JSONDecoder().decode(ChatResponse.self, from: data)
    }

    // MARK: - Streaming Chat

    nonisolated func streamChat(
        model: String,
        messages: [ChatMessage],
        temperature: Double = 0.7,
        maxTokens: Int = 4096
    ) -> AsyncThrowingStream<StreamChunk, Error> {
        AsyncThrowingStream { continuation in
            Task {
                do {
                    let host = await self.baseURL
                    guard let url = URL(string: "\(host)/api/chat") else {
                        continuation.finish(throwing: OllamaError.invalidURL)
                        return
                    }

                    var request = URLRequest(url: url)
                    request.httpMethod = "POST"
                    request.setValue("application/json", forHTTPHeaderField: "Content-Type")

                    let body = ChatRequest(
                        model: model,
                        messages: messages,
                        stream: true,
                        options: ChatOptions(temperature: temperature, num_predict: maxTokens)
                    )

                    request.httpBody = try JSONEncoder().encode(body)

                    let (bytes, response) = try await self.session.bytes(for: request)

                    guard let httpResponse = response as? HTTPURLResponse,
                          httpResponse.statusCode == 200 else {
                        continuation.finish(throwing: OllamaError.serverError)
                        return
                    }

                    for try await line in bytes.lines {
                        if let data = line.data(using: .utf8),
                           let chunk = try? JSONDecoder().decode(StreamChunk.self, from: data) {
                            continuation.yield(chunk)
                            if chunk.done {
                                break
                            }
                        }
                    }

                    continuation.finish()
                } catch {
                    continuation.finish(throwing: error)
                }
            }
        }
    }

    // MARK: - Generate (Legacy API)

    func generate(
        model: String,
        prompt: String,
        system: String? = nil,
        temperature: Double = 0.7,
        maxTokens: Int = 4096
    ) async throws -> GenerateResponse {
        let host = await baseURL
        guard let url = URL(string: "\(host)/api/generate") else {
            throw OllamaError.invalidURL
        }

        var request = URLRequest(url: url)
        request.httpMethod = "POST"
        request.setValue("application/json", forHTTPHeaderField: "Content-Type")

        let body = GenerateRequest(
            model: model,
            prompt: prompt,
            system: system,
            stream: false,
            options: ChatOptions(temperature: temperature, num_predict: maxTokens)
        )

        request.httpBody = try JSONEncoder().encode(body)

        let (data, response) = try await session.data(for: request)

        guard let httpResponse = response as? HTTPURLResponse,
              httpResponse.statusCode == 200 else {
            throw OllamaError.serverError
        }

        return try JSONDecoder().decode(GenerateResponse.self, from: data)
    }
}

// MARK: - Error Types

enum OllamaError: LocalizedError {
    case invalidURL
    case serverError
    case apiError(Int, String)
    case decodingError

    var errorDescription: String? {
        switch self {
        case .invalidURL:
            return "Invalid Ollama server URL"
        case .serverError:
            return "Server error occurred"
        case .apiError(let code, let message):
            return "API error (\(code)): \(message)"
        case .decodingError:
            return "Failed to decode response"
        }
    }
}

// MARK: - API Models

struct ModelsResponse: Codable {
    let models: [OllamaModel]
}

struct OllamaModel: Codable, Identifiable, Hashable {
    var id: String { name }
    let name: String
    let modified_at: String?
    let size: Int64?
    let digest: String?

    var displayName: String {
        name.split(separator: ":").first.map(String.init) ?? name
    }

    var tag: String? {
        let parts = name.split(separator: ":")
        return parts.count > 1 ? String(parts[1]) : nil
    }
}

struct ChatRequest: Codable {
    let model: String
    let messages: [ChatMessage]
    let stream: Bool
    let options: ChatOptions?
}

struct ChatMessage: Codable {
    let role: String
    let content: String

    static func system(_ content: String) -> ChatMessage {
        ChatMessage(role: "system", content: content)
    }

    static func user(_ content: String) -> ChatMessage {
        ChatMessage(role: "user", content: content)
    }

    static func assistant(_ content: String) -> ChatMessage {
        ChatMessage(role: "assistant", content: content)
    }
}

struct ChatOptions: Codable {
    let temperature: Double?
    let num_predict: Int?
}

struct ChatResponse: Codable {
    let model: String
    let created_at: String
    let message: ResponseMessage
    let done: Bool
    let total_duration: Int64?
    let load_duration: Int64?
    let prompt_eval_count: Int?
    let prompt_eval_duration: Int64?
    let eval_count: Int?
    let eval_duration: Int64?
}

struct ResponseMessage: Codable {
    let role: String
    let content: String
}

struct StreamChunk: Codable {
    let model: String?
    let created_at: String?
    let message: ResponseMessage?
    let done: Bool
}

struct GenerateRequest: Codable {
    let model: String
    let prompt: String
    let system: String?
    let stream: Bool
    let options: ChatOptions?
}

struct GenerateResponse: Codable {
    let model: String
    let created_at: String
    let response: String
    let done: Bool
    let total_duration: Int64?
    let load_duration: Int64?
    let prompt_eval_count: Int?
    let eval_count: Int?
}
