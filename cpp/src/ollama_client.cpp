#include "ollama_client.h"
#include "utils.h"
#include <iostream>
#include <sstream>

namespace ollamacode {

OllamaClient::OllamaClient(const std::string& host)
    : host_(host)
    , curl_(nullptr)
{
    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl_ = curl_easy_init();
    if (!curl_) {
        throw std::runtime_error("Failed to initialize curl");
    }
}

OllamaClient::~OllamaClient() {
    if (curl_) {
        curl_easy_cleanup(curl_);
    }
    curl_global_cleanup();
}

size_t OllamaClient::writeCallback(void* contents, size_t size, size_t nmemb, std::string* userp) {
    size_t totalSize = size * nmemb;
    userp->append((char*)contents, totalSize);
    return totalSize;
}

std::string OllamaClient::httpPost(const std::string& endpoint, const std::string& payload) {
    std::string response;
    std::string url = host_ + endpoint;

    curl_easy_setopt(curl_, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl_, CURLOPT_POST, 1L);
    curl_easy_setopt(curl_, CURLOPT_POSTFIELDS, payload.c_str());
    curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl_, CURLOPT_TIMEOUT, 300L); // 5 minutes timeout

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    curl_easy_setopt(curl_, CURLOPT_HTTPHEADER, headers);

    CURLcode res = curl_easy_perform(curl_);

    curl_slist_free_all(headers);

    if (res != CURLE_OK) {
        throw std::runtime_error(std::string("curl_easy_perform() failed: ") + curl_easy_strerror(res));
    }

    return response;
}

std::string OllamaClient::httpGet(const std::string& endpoint) {
    std::string response;
    std::string url = host_ + endpoint;

    curl_easy_setopt(curl_, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl_, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl_, CURLOPT_TIMEOUT, 30L);

    CURLcode res = curl_easy_perform(curl_);

    if (res != CURLE_OK) {
        throw std::runtime_error(std::string("curl_easy_perform() failed: ") + curl_easy_strerror(res));
    }

    return response;
}

bool OllamaClient::testConnection() {
    try {
        std::string response = httpGet("/api/tags");
        return !response.empty();
    } catch (const std::exception& e) {
        std::cerr << "Connection test failed: " << e.what() << std::endl;
        return false;
    }
}

std::vector<std::string> OllamaClient::listModels() {
    std::vector<std::string> models;

    try {
        std::string response = httpGet("/api/tags");
        json j = json::parse(response);

        if (j.contains("models") && j["models"].is_array()) {
            for (const auto& model : j["models"]) {
                if (model.contains("name")) {
                    models.push_back(model["name"].get<std::string>());
                }
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Failed to list models: " << e.what() << std::endl;
    }

    return models;
}

OllamaResponse OllamaClient::generate(
    const std::string& model,
    const std::string& prompt,
    double temperature,
    int max_tokens)
{
    OllamaResponse response;

    try {
        // Build JSON payload
        json payload = {
            {"model", model},
            {"prompt", prompt},
            {"stream", false},
            {"options", {
                {"temperature", temperature},
                {"num_predict", max_tokens}
            }}
        };

        std::string jsonPayload = payload.dump();

        // Send request
        std::string responseStr = httpPost("/api/generate", jsonPayload);

        // Parse response
        json j = json::parse(responseStr);

        if (j.contains("response")) {
            response.response = j["response"].get<std::string>();
        }

        if (j.contains("eval_count")) {
            response.eval_count = j["eval_count"].get<int>();
        } else {
            response.eval_count = 0;
        }

        if (j.contains("total_duration")) {
            response.total_duration = j["total_duration"].get<long long>();
        } else {
            response.total_duration = 0;
        }

        if (j.contains("done")) {
            response.done = j["done"].get<bool>();
        } else {
            response.done = true;
        }

    } catch (const std::exception& e) {
        response.error = std::string("Generation failed: ") + e.what();
        response.done = true;
    }

    return response;
}

OllamaResponse OllamaClient::chat(
    const std::string& model,
    const json& messages,
    double temperature,
    int max_tokens)
{
    OllamaResponse response;

    try {
        // Build JSON payload
        json payload = {
            {"model", model},
            {"messages", messages},
            {"stream", false},
            {"options", {
                {"temperature", temperature},
                {"num_predict", max_tokens}
            }}
        };

        std::string jsonPayload = payload.dump();

        // Send request to chat endpoint
        std::string responseStr = httpPost("/api/chat", jsonPayload);

        // Parse response
        json j = json::parse(responseStr);

        // Check for error
        if (j.contains("error")) {
            response.error = j["error"].get<std::string>();
            response.done = true;
            return response;
        }

        // Extract message content
        if (j.contains("message") && j["message"].contains("content")) {
            response.response = j["message"]["content"].get<std::string>();
        }

        if (j.contains("eval_count")) {
            response.eval_count = j["eval_count"].get<int>();
        } else {
            response.eval_count = 0;
        }

        if (j.contains("total_duration")) {
            response.total_duration = j["total_duration"].get<long long>();
        } else {
            response.total_duration = 0;
        }

        if (j.contains("done")) {
            response.done = j["done"].get<bool>();
        } else {
            response.done = true;
        }

    } catch (const std::exception& e) {
        response.error = std::string("Chat failed: ") + e.what();
        response.done = true;
    }

    return response;
}

} // namespace ollamacode
