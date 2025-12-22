#include "embeddings.h"
#include "json.hpp"
#include <curl/curl.h>
#include <cmath>
#include <algorithm>
#include <sstream>
#include <cctype>
#include <iostream>

using json = nlohmann::json;

namespace ollamacode {

// CURL write callback
static size_t writeCallback(void* contents, size_t size, size_t nmemb, std::string* userp) {
    size_t total_size = size * nmemb;
    userp->append(static_cast<char*>(contents), total_size);
    return total_size;
}

// ============================================================================
// OllamaEmbeddingProvider Implementation
// ============================================================================

OllamaEmbeddingProvider::OllamaEmbeddingProvider(const std::string& host, const std::string& model)
    : host_(host)
    , model_(model)
    , dimensions_(0) {
}

void OllamaEmbeddingProvider::setHost(const std::string& host) {
    host_ = host;
}

void OllamaEmbeddingProvider::setModel(const std::string& model) {
    model_ = model;
    dimensions_ = 0;  // Reset to detect on next embed
}

void OllamaEmbeddingProvider::detectDimensions(const Embedding& emb) {
    if (!emb.empty() && dimensions_ == 0) {
        dimensions_ = static_cast<int>(emb.size());
    }
}

bool OllamaEmbeddingProvider::testConnection() {
    CURL* curl = curl_easy_init();
    if (!curl) return false;

    std::string url = host_ + "/api/tags";
    std::string response;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    return res == CURLE_OK;
}

std::vector<std::string> OllamaEmbeddingProvider::listModels() {
    std::vector<std::string> models;

    CURL* curl = curl_easy_init();
    if (!curl) return models;

    std::string url = host_ + "/api/tags";
    std::string response;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) return models;

    try {
        json data = json::parse(response);
        if (data.contains("models")) {
            for (const auto& model : data["models"]) {
                std::string name = model.value("name", "");
                // Filter for embedding-capable models
                if (name.find("embed") != std::string::npos ||
                    name.find("nomic") != std::string::npos ||
                    name.find("mxbai") != std::string::npos) {
                    models.push_back(name);
                }
            }
        }
    } catch (...) {
        // Ignore parse errors
    }

    return models;
}

EmbeddingResult OllamaEmbeddingProvider::embed(const std::string& text) {
    EmbeddingResult result;
    result.success = false;
    result.dimensions = 0;

    CURL* curl = curl_easy_init();
    if (!curl) {
        result.error = "Failed to initialize CURL";
        return result;
    }

    std::string url = host_ + "/api/embeddings";
    std::string response;

    json request;
    request["model"] = model_;
    request["prompt"] = text;

    std::string payload = request.dump();

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        result.error = curl_easy_strerror(res);
        return result;
    }

    try {
        json data = json::parse(response);

        if (data.contains("error")) {
            result.error = data["error"].get<std::string>();
            return result;
        }

        if (data.contains("embedding")) {
            result.embedding = data["embedding"].get<std::vector<float>>();
            detectDimensions(result.embedding);
            result.dimensions = static_cast<int>(result.embedding.size());
            result.success = true;
        } else {
            result.error = "No embedding in response";
        }
    } catch (const std::exception& e) {
        result.error = std::string("Parse error: ") + e.what();
    }

    return result;
}

BatchEmbeddingResult OllamaEmbeddingProvider::embedBatch(const std::vector<std::string>& texts) {
    BatchEmbeddingResult result;
    result.success = true;
    result.dimensions = 0;

    for (const auto& text : texts) {
        auto single = embed(text);
        if (!single.success) {
            result.success = false;
            result.error = single.error;
            return result;
        }
        result.embeddings.push_back(single.embedding);
        result.dimensions = single.dimensions;
    }

    return result;
}

// ============================================================================
// LocalEmbeddingProvider Implementation
// ============================================================================

LocalEmbeddingProvider::LocalEmbeddingProvider() : dimensions_(256) {
}

std::vector<std::string> LocalEmbeddingProvider::tokenize(const std::string& text) {
    std::vector<std::string> tokens;
    std::string current;

    for (char c : text) {
        if (std::isalnum(c)) {
            current += std::tolower(c);
        } else if (!current.empty()) {
            if (current.length() >= 2) {  // Skip very short tokens
                tokens.push_back(current);
            }
            current.clear();
        }
    }

    if (!current.empty() && current.length() >= 2) {
        tokens.push_back(current);
    }

    return tokens;
}

uint32_t LocalEmbeddingProvider::hash(const std::string& token) {
    // Simple FNV-1a hash
    uint32_t h = 2166136261u;
    for (char c : token) {
        h ^= static_cast<uint32_t>(c);
        h *= 16777619u;
    }
    return h;
}

Embedding LocalEmbeddingProvider::hashEmbed(const std::string& text) {
    Embedding emb(dimensions_, 0.0f);

    auto tokens = tokenize(text);
    if (tokens.empty()) return emb;

    // Create TF (term frequency) based embedding
    for (const auto& token : tokens) {
        uint32_t h = hash(token);

        // Use multiple hash positions for better distribution
        for (int i = 0; i < 4; i++) {
            uint32_t pos = (h + i * 0x9E3779B9) % dimensions_;
            float sign = ((h >> (i * 8)) & 1) ? 1.0f : -1.0f;
            emb[pos] += sign;
        }

        // Also add character n-grams (2-grams and 3-grams)
        for (size_t j = 0; j + 1 < token.length(); j++) {
            std::string bigram = token.substr(j, 2);
            uint32_t bh = hash(bigram);
            uint32_t bpos = bh % dimensions_;
            emb[bpos] += 0.5f;
        }

        for (size_t j = 0; j + 2 < token.length(); j++) {
            std::string trigram = token.substr(j, 3);
            uint32_t th = hash(trigram);
            uint32_t tpos = th % dimensions_;
            emb[tpos] += 0.3f;
        }
    }

    // Normalize
    float norm = 0.0f;
    for (float v : emb) {
        norm += v * v;
    }
    norm = std::sqrt(norm);

    if (norm > 0) {
        for (float& v : emb) {
            v /= norm;
        }
    }

    return emb;
}

EmbeddingResult LocalEmbeddingProvider::embed(const std::string& text) {
    EmbeddingResult result;
    result.embedding = hashEmbed(text);
    result.dimensions = dimensions_;
    result.success = true;
    return result;
}

BatchEmbeddingResult LocalEmbeddingProvider::embedBatch(const std::vector<std::string>& texts) {
    BatchEmbeddingResult result;
    result.success = true;
    result.dimensions = dimensions_;

    for (const auto& text : texts) {
        result.embeddings.push_back(hashEmbed(text));
    }

    return result;
}

// ============================================================================
// EmbeddingClient Implementation
// ============================================================================

EmbeddingClient::EmbeddingClient() : current_provider_("ollama") {
    ollama_ = std::make_unique<OllamaEmbeddingProvider>();
    local_ = std::make_unique<LocalEmbeddingProvider>();
}

void EmbeddingClient::setProvider(const std::string& provider) {
    current_provider_ = provider;
}

void EmbeddingClient::setOllamaHost(const std::string& host) {
    ollama_->setHost(host);
}

void EmbeddingClient::setOllamaModel(const std::string& model) {
    ollama_->setModel(model);
}

std::string EmbeddingClient::getProvider() const {
    return current_provider_;
}

std::string EmbeddingClient::getModel() const {
    if (current_provider_ == "ollama") {
        return ollama_->getModel();
    }
    return local_->getModel();
}

int EmbeddingClient::getDimensions() const {
    if (current_provider_ == "ollama") {
        return ollama_->getDimensions();
    }
    return local_->getDimensions();
}

EmbeddingProvider* EmbeddingClient::getActiveProvider() {
    if (current_provider_ == "local") {
        return local_.get();
    }
    return ollama_.get();
}

EmbeddingResult EmbeddingClient::embed(const std::string& text) {
    auto result = getActiveProvider()->embed(text);

    // Fallback to local if Ollama fails
    if (!result.success && current_provider_ == "ollama") {
        std::cerr << "Ollama embedding failed, falling back to local: " << result.error << std::endl;
        return local_->embed(text);
    }

    return result;
}

BatchEmbeddingResult EmbeddingClient::embedBatch(const std::vector<std::string>& texts) {
    auto result = getActiveProvider()->embedBatch(texts);

    // Fallback to local if Ollama fails
    if (!result.success && current_provider_ == "ollama") {
        std::cerr << "Ollama embedding failed, falling back to local: " << result.error << std::endl;
        return local_->embedBatch(texts);
    }

    return result;
}

bool EmbeddingClient::isAvailable() {
    if (current_provider_ == "local") {
        return true;
    }
    return ollama_->testConnection();
}

float EmbeddingClient::cosineSimilarity(const Embedding& a, const Embedding& b) {
    if (a.size() != b.size() || a.empty()) return 0.0f;

    float dot = 0.0f;
    float norm_a = 0.0f;
    float norm_b = 0.0f;

    for (size_t i = 0; i < a.size(); i++) {
        dot += a[i] * b[i];
        norm_a += a[i] * a[i];
        norm_b += b[i] * b[i];
    }

    float denom = std::sqrt(norm_a) * std::sqrt(norm_b);
    if (denom == 0.0f) return 0.0f;

    return dot / denom;
}

float EmbeddingClient::dotProduct(const Embedding& a, const Embedding& b) {
    if (a.size() != b.size()) return 0.0f;

    float dot = 0.0f;
    for (size_t i = 0; i < a.size(); i++) {
        dot += a[i] * b[i];
    }
    return dot;
}

Embedding EmbeddingClient::normalize(const Embedding& emb) {
    Embedding result = emb;
    float norm = 0.0f;

    for (float v : emb) {
        norm += v * v;
    }
    norm = std::sqrt(norm);

    if (norm > 0) {
        for (float& v : result) {
            v /= norm;
        }
    }

    return result;
}

} // namespace ollamacode
