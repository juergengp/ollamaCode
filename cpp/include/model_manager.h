#ifndef CASPER_MODEL_MANAGER_H
#define CASPER_MODEL_MANAGER_H

#include <string>
#include <vector>
#include <map>
#include <functional>
#include "ollama_client.h"

namespace casper {

// Forward declarations
class Config;
class LicenseManager;

// Model information structure
struct ModelInfo {
    std::string name;
    std::string base_model;
    std::string system_prompt;
    std::string template_text;
    std::string license;
    std::map<std::string, std::string> parameters;
    std::string modelfile;
    int64_t size;
    std::string created_at;
    std::string description;
};

// Modelfile builder for creating custom models
struct ModelfileBuilder {
    std::string from;           // Base model (required)
    std::string system;         // System prompt
    std::string template_text;  // Custom template
    std::map<std::string, std::string> parameters;  // temperature, top_p, etc.
    std::string adapter;        // LoRA adapter path
    std::string license;        // License text

    // Build the Modelfile content string
    std::string build() const;

    // Validate the builder has required fields
    bool isValid() const;

    // Reset to defaults
    void clear();
};

// Progress callback for long operations
using ModelProgressCallback = std::function<void(const std::string& status, int64_t completed, int64_t total)>;

class ModelManager {
public:
    ModelManager(OllamaClient& client, Config& config, LicenseManager* license = nullptr);
    ~ModelManager();

    // Set license manager (for feature gating)
    void setLicenseManager(LicenseManager* license);

    // ===== Model Creation =====

    // Create model from Modelfile content
    bool createModel(const std::string& name, const std::string& modelfile,
                     StatusCallback progress = nullptr);

    // Create model from builder
    bool createModel(const std::string& name, const ModelfileBuilder& builder,
                     StatusCallback progress = nullptr);

    // Interactive model creation wizard
    ModelfileBuilder interactiveModelBuilder();

    // ===== Model Information =====

    // Get detailed model info
    ModelInfo getModelInfo(const std::string& model_name);

    // List all available models
    std::vector<std::string> listModels();

    // List custom models (tracked locally)
    std::vector<std::string> listCustomModels();

    // ===== Model Manipulation =====

    // Copy/clone a model
    bool copyModel(const std::string& source, const std::string& destination);

    // Delete a model
    bool deleteModel(const std::string& model_name);

    // Edit model (recreate with modified Modelfile)
    bool editModel(const std::string& model_name, const ModelfileBuilder& new_builder);

    // ===== Download/Upload =====

    // Pull model from Ollama library
    bool pullModel(const std::string& model_name, ModelProgressCallback progress = nullptr);

    // Push model to Ollama library (requires ollama.ai account)
    bool pushModel(const std::string& model_name, ModelProgressCallback progress = nullptr);

    // ===== Local Tracking =====

    // Save custom model info to local database
    bool saveCustomModel(const std::string& name, const ModelfileBuilder& builder,
                         const std::string& description = "");

    // Get custom model info from local database
    ModelfileBuilder getCustomModelBuilder(const std::string& name);

    // Delete custom model record (not the actual model)
    bool deleteCustomModelRecord(const std::string& name);

    // ===== Utilities =====

    // Print model info to console
    void printModelInfo(const std::string& model_name);

    // Print progress bar for pull/push operations
    static void printProgress(const std::string& status, int64_t completed, int64_t total);

    // Get available parameters with descriptions
    static std::map<std::string, std::string> getAvailableParameters();

private:
    OllamaClient& client_;
    Config& config_;
    LicenseManager* license_;
    void* db_;  // sqlite3*

    // Database helpers
    void initializeDatabase();
    void createTables();

    // License checks
    bool checkLicense(const std::string& operation);
};

} // namespace casper

#endif // CASPER_MODEL_MANAGER_H
