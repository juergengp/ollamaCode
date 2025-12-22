#ifndef OLEG_OLLAMA_CLIENT_H
#define OLEG_OLLAMA_CLIENT_H

#include <string>
#include <vector>
#include <functional>
#include <curl/curl.h>
#include "json.hpp"

using json = nlohmann::json;

namespace oleg {

struct OllamaResponse {
    std::string response;
    int eval_count;
    long long total_duration;
    bool done;
    std::string error;

    bool isSuccess() const { return error.empty(); }
};

// Model creation result
struct CreateModelResult {
    bool success;
    std::string error;
    std::string status;
};

// Model information result
struct ShowModelResult {
    bool success;
    std::string modelfile;
    std::string template_text;
    std::string system;
    std::string license;
    json parameters;
    json details;
    std::string error;
};

// Progress callback types
using ProgressCallback = std::function<void(const std::string& status, int64_t completed, int64_t total)>;
using StatusCallback = std::function<void(const std::string& status)>;

class OllamaClient {
public:
    OllamaClient(const std::string& host = "http://localhost:11434");
    ~OllamaClient();

    // Test connection to Ollama
    bool testConnection();

    // List available models
    std::vector<std::string> listModels();

    // Generate completion (legacy)
    OllamaResponse generate(
        const std::string& model,
        const std::string& prompt,
        double temperature = 0.7,
        int max_tokens = 4096
    );

    // Chat completion with messages
    OllamaResponse chat(
        const std::string& model,
        const json& messages,
        double temperature = 0.7,
        int max_tokens = 4096
    );

    // ===== Model Management APIs =====

    // Create a model from Modelfile content (POST /api/create)
    CreateModelResult createModel(
        const std::string& name,
        const std::string& modelfile,
        StatusCallback progress_callback = nullptr
    );

    // Show model information (POST /api/show)
    ShowModelResult showModel(const std::string& model_name);

    // Copy/clone a model (POST /api/copy)
    bool copyModel(const std::string& source, const std::string& destination);

    // Delete a model (DELETE /api/delete)
    bool deleteModel(const std::string& model_name);

    // Pull a model from Ollama library (POST /api/pull)
    bool pullModel(
        const std::string& model_name,
        ProgressCallback progress_callback = nullptr
    );

    // Push a model to Ollama library (POST /api/push)
    bool pushModel(
        const std::string& model_name,
        ProgressCallback progress_callback = nullptr
    );

    // Get host URL
    std::string getHost() const { return host_; }

    // Set host URL
    void setHost(const std::string& host) { host_ = host; }

private:
    std::string host_;
    CURL* curl_;

    // HTTP helpers
    std::string httpPost(const std::string& endpoint, const std::string& payload);
    std::string httpGet(const std::string& endpoint);
    bool httpDelete(const std::string& endpoint, const std::string& payload);

    // Streaming HTTP for progress callbacks
    bool httpPostStreaming(
        const std::string& endpoint,
        const std::string& payload,
        std::function<void(const std::string&)> line_callback
    );

    static size_t writeCallback(void* contents, size_t size, size_t nmemb, std::string* userp);
    static size_t streamCallback(void* contents, size_t size, size_t nmemb, void* userp);
};

} // namespace oleg

#endif // OLEG_OLLAMA_CLIENT_H
