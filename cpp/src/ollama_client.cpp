#include "ollama_client.h"
#include "utils.h"
#include <iostream>
#include <sstream>

namespace oleg {

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

    // Reset curl handle to clear any previous state
    curl_easy_reset(curl_);

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

    // Reset curl handle to clear any previous POST state
    curl_easy_reset(curl_);

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

// ============================================================================
// HTTP Delete helper
// ============================================================================

bool OllamaClient::httpDelete(const std::string& endpoint, const std::string& payload) {
    std::string response;
    std::string url = host_ + endpoint;

    curl_easy_reset(curl_);
    curl_easy_setopt(curl_, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl_, CURLOPT_CUSTOMREQUEST, "DELETE");
    curl_easy_setopt(curl_, CURLOPT_POSTFIELDS, payload.c_str());
    curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl_, CURLOPT_TIMEOUT, 60L);

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    curl_easy_setopt(curl_, CURLOPT_HTTPHEADER, headers);

    CURLcode res = curl_easy_perform(curl_);
    curl_slist_free_all(headers);

    if (res != CURLE_OK) {
        std::cerr << "Delete request failed: " << curl_easy_strerror(res) << std::endl;
        return false;
    }

    long http_code = 0;
    curl_easy_getinfo(curl_, CURLINFO_RESPONSE_CODE, &http_code);
    return http_code >= 200 && http_code < 300;
}

// ============================================================================
// Streaming HTTP helper with line callback
// ============================================================================

struct StreamContext {
    std::function<void(const std::string&)> callback;
    std::string buffer;
};

size_t OllamaClient::streamCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t totalSize = size * nmemb;
    StreamContext* ctx = static_cast<StreamContext*>(userp);

    ctx->buffer.append(static_cast<char*>(contents), totalSize);

    // Process complete lines (JSON objects are newline-delimited)
    size_t pos;
    while ((pos = ctx->buffer.find('\n')) != std::string::npos) {
        std::string line = ctx->buffer.substr(0, pos);
        ctx->buffer.erase(0, pos + 1);

        if (!line.empty() && ctx->callback) {
            ctx->callback(line);
        }
    }

    return totalSize;
}

bool OllamaClient::httpPostStreaming(
    const std::string& endpoint,
    const std::string& payload,
    std::function<void(const std::string&)> line_callback)
{
    std::string url = host_ + endpoint;
    StreamContext ctx;
    ctx.callback = line_callback;

    curl_easy_reset(curl_);
    curl_easy_setopt(curl_, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl_, CURLOPT_POST, 1L);
    curl_easy_setopt(curl_, CURLOPT_POSTFIELDS, payload.c_str());
    curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, streamCallback);
    curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &ctx);
    curl_easy_setopt(curl_, CURLOPT_TIMEOUT, 3600L);  // 1 hour for large downloads

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    curl_easy_setopt(curl_, CURLOPT_HTTPHEADER, headers);

    CURLcode res = curl_easy_perform(curl_);
    curl_slist_free_all(headers);

    // Process any remaining data in buffer
    if (!ctx.buffer.empty() && ctx.callback) {
        ctx.callback(ctx.buffer);
    }

    if (res != CURLE_OK) {
        std::cerr << "Streaming request failed: " << curl_easy_strerror(res) << std::endl;
        return false;
    }

    return true;
}

// ============================================================================
// Model Management APIs
// ============================================================================

CreateModelResult OllamaClient::createModel(
    const std::string& name,
    const std::string& modelfile,
    StatusCallback progress_callback)
{
    CreateModelResult result;
    result.success = false;

    try {
        json payload = {
            {"name", name},
            {"modelfile", modelfile},
            {"stream", progress_callback != nullptr}
        };

        if (progress_callback) {
            // Streaming mode
            bool ok = httpPostStreaming("/api/create", payload.dump(),
                [&result, &progress_callback](const std::string& line) {
                    try {
                        json j = json::parse(line);
                        if (j.contains("status")) {
                            result.status = j["status"].get<std::string>();
                            progress_callback(result.status);
                        }
                        if (j.contains("error")) {
                            result.error = j["error"].get<std::string>();
                        }
                    } catch (...) {
                        // Ignore parse errors for incomplete JSON
                    }
                });
            result.success = ok && result.error.empty();
        } else {
            // Non-streaming mode
            std::string response = httpPost("/api/create", payload.dump());
            json j = json::parse(response);

            if (j.contains("error")) {
                result.error = j["error"].get<std::string>();
            } else {
                result.success = true;
                if (j.contains("status")) {
                    result.status = j["status"].get<std::string>();
                }
            }
        }
    } catch (const std::exception& e) {
        result.error = std::string("Create model failed: ") + e.what();
    }

    return result;
}

ShowModelResult OllamaClient::showModel(const std::string& model_name) {
    ShowModelResult result;
    result.success = false;

    try {
        json payload = {{"name", model_name}};
        std::string response = httpPost("/api/show", payload.dump());
        json j = json::parse(response);

        if (j.contains("error")) {
            result.error = j["error"].get<std::string>();
            return result;
        }

        result.success = true;

        if (j.contains("modelfile")) {
            result.modelfile = j["modelfile"].get<std::string>();
        }
        if (j.contains("template")) {
            result.template_text = j["template"].get<std::string>();
        }
        if (j.contains("system")) {
            result.system = j["system"].get<std::string>();
        }
        if (j.contains("license")) {
            result.license = j["license"].get<std::string>();
        }
        if (j.contains("parameters")) {
            result.parameters = j["parameters"];
        }
        if (j.contains("details")) {
            result.details = j["details"];
        }

    } catch (const std::exception& e) {
        result.error = std::string("Show model failed: ") + e.what();
    }

    return result;
}

bool OllamaClient::copyModel(const std::string& source, const std::string& destination) {
    try {
        json payload = {
            {"source", source},
            {"destination", destination}
        };
        std::string response = httpPost("/api/copy", payload.dump());

        // Successful copy returns empty response or status
        if (response.empty()) {
            return true;
        }

        json j = json::parse(response);
        return !j.contains("error");

    } catch (const std::exception& e) {
        std::cerr << "Copy model failed: " << e.what() << std::endl;
        return false;
    }
}

bool OllamaClient::deleteModel(const std::string& model_name) {
    try {
        json payload = {{"name", model_name}};
        return httpDelete("/api/delete", payload.dump());
    } catch (const std::exception& e) {
        std::cerr << "Delete model failed: " << e.what() << std::endl;
        return false;
    }
}

bool OllamaClient::pullModel(
    const std::string& model_name,
    ProgressCallback progress_callback)
{
    try {
        json payload = {
            {"name", model_name},
            {"stream", progress_callback != nullptr}
        };

        bool has_error = false;
        std::string error_msg;

        bool ok = httpPostStreaming("/api/pull", payload.dump(),
            [&has_error, &error_msg, &progress_callback](const std::string& line) {
                try {
                    json j = json::parse(line);

                    if (j.contains("error")) {
                        has_error = true;
                        error_msg = j["error"].get<std::string>();
                        return;
                    }

                    if (progress_callback) {
                        std::string status = j.value("status", "");
                        int64_t completed = j.value("completed", 0LL);
                        int64_t total = j.value("total", 0LL);
                        progress_callback(status, completed, total);
                    }
                } catch (...) {
                    // Ignore parse errors
                }
            });

        if (has_error) {
            std::cerr << "Pull failed: " << error_msg << std::endl;
            return false;
        }

        return ok;

    } catch (const std::exception& e) {
        std::cerr << "Pull model failed: " << e.what() << std::endl;
        return false;
    }
}

bool OllamaClient::pushModel(
    const std::string& model_name,
    ProgressCallback progress_callback)
{
    try {
        json payload = {
            {"name", model_name},
            {"stream", progress_callback != nullptr}
        };

        bool has_error = false;
        std::string error_msg;

        bool ok = httpPostStreaming("/api/push", payload.dump(),
            [&has_error, &error_msg, &progress_callback](const std::string& line) {
                try {
                    json j = json::parse(line);

                    if (j.contains("error")) {
                        has_error = true;
                        error_msg = j["error"].get<std::string>();
                        return;
                    }

                    if (progress_callback) {
                        std::string status = j.value("status", "");
                        int64_t completed = j.value("completed", 0LL);
                        int64_t total = j.value("total", 0LL);
                        progress_callback(status, completed, total);
                    }
                } catch (...) {
                    // Ignore parse errors
                }
            });

        if (has_error) {
            std::cerr << "Push failed: " << error_msg << std::endl;
            return false;
        }

        return ok;

    } catch (const std::exception& e) {
        std::cerr << "Push model failed: " << e.what() << std::endl;
        return false;
    }
}

} // namespace oleg
