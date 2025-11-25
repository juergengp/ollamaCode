#ifndef OLLAMACODE_OLLAMA_CLIENT_H
#define OLLAMACODE_OLLAMA_CLIENT_H

#include <string>
#include <vector>
#include <curl/curl.h>
#include "json.hpp"

using json = nlohmann::json;

namespace ollamacode {

struct OllamaResponse {
    std::string response;
    int eval_count;
    long long total_duration;
    bool done;
    std::string error;

    bool isSuccess() const { return error.empty(); }
};

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

    // Stream completion (for future)
    // void streamGenerate(...);

private:
    std::string host_;
    CURL* curl_;

    // HTTP helpers
    std::string httpPost(const std::string& endpoint, const std::string& payload);
    std::string httpGet(const std::string& endpoint);

    static size_t writeCallback(void* contents, size_t size, size_t nmemb, std::string* userp);
};

} // namespace ollamacode

#endif // OLLAMACODE_OLLAMA_CLIENT_H
