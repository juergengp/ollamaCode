#include "model_manager.h"
#include "config.h"
#include "license.h"
#include "utils.h"
#include <sqlite3.h>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <algorithm>

// Helper macro to cast db_
#define DB() (reinterpret_cast<sqlite3*>(db_))

namespace oleg {

// ============================================================================
// ModelfileBuilder Implementation
// ============================================================================

std::string ModelfileBuilder::build() const {
    std::stringstream ss;

    // FROM is required
    if (!from.empty()) {
        ss << "FROM " << from << "\n";
    }

    // System prompt
    if (!system.empty()) {
        // Handle multi-line system prompts
        if (system.find('\n') != std::string::npos) {
            ss << "SYSTEM \"\"\"" << system << "\"\"\"\n";
        } else {
            ss << "SYSTEM " << system << "\n";
        }
    }

    // Template
    if (!template_text.empty()) {
        ss << "TEMPLATE \"\"\"" << template_text << "\"\"\"\n";
    }

    // Parameters
    for (const auto& [key, value] : parameters) {
        ss << "PARAMETER " << key << " " << value << "\n";
    }

    // Adapter (LoRA)
    if (!adapter.empty()) {
        ss << "ADAPTER " << adapter << "\n";
    }

    // License
    if (!license.empty()) {
        ss << "LICENSE \"\"\"" << license << "\"\"\"\n";
    }

    return ss.str();
}

bool ModelfileBuilder::isValid() const {
    return !from.empty();
}

void ModelfileBuilder::clear() {
    from.clear();
    system.clear();
    template_text.clear();
    parameters.clear();
    adapter.clear();
    license.clear();
}

// ============================================================================
// ModelManager Implementation
// ============================================================================

ModelManager::ModelManager(OllamaClient& client, Config& config, LicenseManager* license)
    : client_(client)
    , config_(config)
    , license_(license)
    , db_(nullptr)
{
    initializeDatabase();
}

ModelManager::~ModelManager() {
    if (db_) {
        sqlite3_close(DB());
    }
}

void ModelManager::setLicenseManager(LicenseManager* license) {
    license_ = license;
}

void ModelManager::initializeDatabase() {
    const char* home = getenv("HOME");
    if (!home) return;

    std::string db_path = std::string(home) + "/.config/oleg/config.db";

    sqlite3* db_ptr = nullptr;
    int rc = sqlite3_open(db_path.c_str(), &db_ptr);
    db_ = db_ptr;
    if (rc != SQLITE_OK) {
        std::cerr << "Cannot open model database: " << sqlite3_errmsg(DB()) << std::endl;
        db_ = nullptr;
        return;
    }

    createTables();
}

void ModelManager::createTables() {
    if (!db_) return;

    const char* sql = R"(
        CREATE TABLE IF NOT EXISTS custom_models (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT NOT NULL UNIQUE,
            base_model TEXT NOT NULL,
            system_prompt TEXT,
            modelfile TEXT NOT NULL,
            parameters TEXT,
            created_at TEXT NOT NULL,
            updated_at TEXT NOT NULL,
            description TEXT
        );
    )";

    char* errmsg = nullptr;
    int rc = sqlite3_exec(DB(), sql, nullptr, nullptr, &errmsg);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to create custom_models table: " << errmsg << std::endl;
        sqlite3_free(errmsg);
    }
}

bool ModelManager::checkLicense(const std::string& operation) {
    if (!license_) return true;  // No license manager = allow all

    if (operation == "create" || operation == "edit") {
        if (!license_->canCreateCustomModels()) {
            license_->showUpgradeMessage(Feature::CustomModelCreation);
            return false;
        }
    } else if (operation == "push") {
        if (!license_->canPushModels()) {
            license_->showUpgradeMessage(Feature::ModelPush);
            return false;
        }
    } else if (operation == "copy") {
        if (!license_->hasFeature(Feature::ModelCopy)) {
            license_->showUpgradeMessage(Feature::ModelCopy);
            return false;
        }
    }

    return true;
}

// ============================================================================
// Model Creation
// ============================================================================

bool ModelManager::createModel(const std::string& name, const std::string& modelfile,
                                StatusCallback progress) {
    if (!checkLicense("create")) return false;

    std::cout << utils::terminal::CYAN << "Creating model '" << name << "'..."
              << utils::terminal::RESET << "\n";

    auto result = client_.createModel(name, modelfile, progress);

    if (result.success) {
        std::cout << utils::terminal::GREEN << "Model '" << name << "' created successfully!"
                  << utils::terminal::RESET << "\n";
    } else {
        std::cout << utils::terminal::RED << "Failed to create model: " << result.error
                  << utils::terminal::RESET << "\n";
    }

    return result.success;
}

bool ModelManager::createModel(const std::string& name, const ModelfileBuilder& builder,
                                StatusCallback progress) {
    if (!builder.isValid()) {
        std::cout << utils::terminal::RED << "Invalid Modelfile: FROM is required"
                  << utils::terminal::RESET << "\n";
        return false;
    }

    std::string modelfile = builder.build();

    // Save to local database
    saveCustomModel(name, builder);

    return createModel(name, modelfile, progress);
}

ModelfileBuilder ModelManager::interactiveModelBuilder() {
    ModelfileBuilder builder;

    std::cout << "\n" << utils::terminal::BOLD << "Create Custom Model"
              << utils::terminal::RESET << "\n";
    std::cout << std::string(40, '-') << "\n\n";

    // Base model (required)
    std::cout << "Base model (e.g., llama3, codellama, mistral): ";
    std::getline(std::cin, builder.from);
    if (builder.from.empty()) {
        std::cout << utils::terminal::RED << "Base model is required."
                  << utils::terminal::RESET << "\n";
        return builder;
    }

    // System prompt
    std::cout << "\nSystem prompt (press Enter twice to finish, or leave empty):\n";
    std::string line;
    std::stringstream system_ss;
    bool first_line = true;
    while (std::getline(std::cin, line)) {
        if (line.empty()) {
            if (!first_line) break;
        }
        if (!first_line) system_ss << "\n";
        system_ss << line;
        first_line = false;
    }
    builder.system = system_ss.str();

    // Temperature
    std::cout << "\nTemperature (0.0-2.0, default 0.7, press Enter to skip): ";
    std::getline(std::cin, line);
    if (!line.empty()) {
        builder.parameters["temperature"] = line;
    }

    // Top P
    std::cout << "Top P (0.0-1.0, default 0.9, press Enter to skip): ";
    std::getline(std::cin, line);
    if (!line.empty()) {
        builder.parameters["top_p"] = line;
    }

    // Top K
    std::cout << "Top K (1-100, default 40, press Enter to skip): ";
    std::getline(std::cin, line);
    if (!line.empty()) {
        builder.parameters["top_k"] = line;
    }

    // Context length
    std::cout << "Context length (default 4096, press Enter to skip): ";
    std::getline(std::cin, line);
    if (!line.empty()) {
        builder.parameters["num_ctx"] = line;
    }

    std::cout << "\n" << utils::terminal::GREEN << "Model configuration complete!"
              << utils::terminal::RESET << "\n";

    return builder;
}

// ============================================================================
// Model Information
// ============================================================================

ModelInfo ModelManager::getModelInfo(const std::string& model_name) {
    ModelInfo info;
    info.name = model_name;

    auto result = client_.showModel(model_name);

    if (result.success) {
        info.modelfile = result.modelfile;
        info.template_text = result.template_text;
        info.system_prompt = result.system;
        info.license = result.license;

        // Parse parameters from JSON
        if (!result.parameters.is_null()) {
            for (auto& [key, value] : result.parameters.items()) {
                if (value.is_string()) {
                    info.parameters[key] = value.get<std::string>();
                } else {
                    info.parameters[key] = value.dump();
                }
            }
        }

        // Parse details
        if (!result.details.is_null()) {
            if (result.details.contains("parent_model")) {
                info.base_model = result.details["parent_model"].get<std::string>();
            }
            if (result.details.contains("parameter_size")) {
                info.description = result.details["parameter_size"].get<std::string>();
            }
        }
    }

    return info;
}

std::vector<std::string> ModelManager::listModels() {
    return client_.listModels();
}

std::vector<std::string> ModelManager::listCustomModels() {
    std::vector<std::string> models;

    if (!db_) return models;

    const char* sql = "SELECT name FROM custom_models ORDER BY updated_at DESC";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(DB(), sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            if (name) models.push_back(name);
        }
        sqlite3_finalize(stmt);
    }

    return models;
}

// ============================================================================
// Model Manipulation
// ============================================================================

bool ModelManager::copyModel(const std::string& source, const std::string& destination) {
    if (!checkLicense("copy")) return false;

    std::cout << utils::terminal::CYAN << "Copying '" << source << "' to '" << destination << "'..."
              << utils::terminal::RESET << "\n";

    bool success = client_.copyModel(source, destination);

    if (success) {
        std::cout << utils::terminal::GREEN << "Model copied successfully!"
                  << utils::terminal::RESET << "\n";
    } else {
        std::cout << utils::terminal::RED << "Failed to copy model"
                  << utils::terminal::RESET << "\n";
    }

    return success;
}

bool ModelManager::deleteModel(const std::string& model_name) {
    std::cout << utils::terminal::YELLOW << "Deleting model '" << model_name << "'..."
              << utils::terminal::RESET << "\n";

    bool success = client_.deleteModel(model_name);

    if (success) {
        // Also delete from local tracking
        deleteCustomModelRecord(model_name);
        std::cout << utils::terminal::GREEN << "Model deleted successfully!"
                  << utils::terminal::RESET << "\n";
    } else {
        std::cout << utils::terminal::RED << "Failed to delete model"
                  << utils::terminal::RESET << "\n";
    }

    return success;
}

bool ModelManager::editModel(const std::string& model_name, const ModelfileBuilder& new_builder) {
    if (!checkLicense("edit")) return false;

    // Recreate the model with the new configuration
    return createModel(model_name, new_builder);
}

// ============================================================================
// Download/Upload
// ============================================================================

bool ModelManager::pullModel(const std::string& model_name, ModelProgressCallback progress) {
    std::cout << utils::terminal::CYAN << "Pulling model '" << model_name << "'..."
              << utils::terminal::RESET << "\n";

    auto callback = [&progress](const std::string& status, int64_t completed, int64_t total) {
        printProgress(status, completed, total);
        if (progress) progress(status, completed, total);
    };

    bool success = client_.pullModel(model_name, callback);

    std::cout << "\n";  // New line after progress

    if (success) {
        std::cout << utils::terminal::GREEN << "Model '" << model_name << "' pulled successfully!"
                  << utils::terminal::RESET << "\n";
    } else {
        std::cout << utils::terminal::RED << "Failed to pull model"
                  << utils::terminal::RESET << "\n";
    }

    return success;
}

bool ModelManager::pushModel(const std::string& model_name, ModelProgressCallback progress) {
    if (!checkLicense("push")) return false;

    std::cout << utils::terminal::CYAN << "Pushing model '" << model_name << "'..."
              << utils::terminal::RESET << "\n";

    auto callback = [&progress](const std::string& status, int64_t completed, int64_t total) {
        printProgress(status, completed, total);
        if (progress) progress(status, completed, total);
    };

    bool success = client_.pushModel(model_name, callback);

    std::cout << "\n";  // New line after progress

    if (success) {
        std::cout << utils::terminal::GREEN << "Model '" << model_name << "' pushed successfully!"
                  << utils::terminal::RESET << "\n";
    } else {
        std::cout << utils::terminal::RED << "Failed to push model"
                  << utils::terminal::RESET << "\n";
    }

    return success;
}

// ============================================================================
// Local Tracking
// ============================================================================

bool ModelManager::saveCustomModel(const std::string& name, const ModelfileBuilder& builder,
                                    const std::string& description) {
    if (!db_) return false;

    // Serialize parameters to JSON
    json params_json;
    for (const auto& [key, value] : builder.parameters) {
        params_json[key] = value;
    }

    // Get current timestamp
    time_t now = time(nullptr);
    struct tm* tm_info = localtime(&now);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info);

    const char* sql = R"(
        INSERT OR REPLACE INTO custom_models
        (name, base_model, system_prompt, modelfile, parameters, created_at, updated_at, description)
        VALUES (?, ?, ?, ?, ?, COALESCE((SELECT created_at FROM custom_models WHERE name = ?), ?), ?, ?)
    )";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(DB(), sql, -1, &stmt, nullptr) == SQLITE_OK) {
        std::string modelfile = builder.build();
        std::string params_str = params_json.dump();

        sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, builder.from.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, builder.system.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, modelfile.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 5, params_str.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 6, name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 7, timestamp, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 8, timestamp, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 9, description.c_str(), -1, SQLITE_TRANSIENT);

        int rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        return rc == SQLITE_DONE;
    }

    return false;
}

ModelfileBuilder ModelManager::getCustomModelBuilder(const std::string& name) {
    ModelfileBuilder builder;

    if (!db_) return builder;

    const char* sql = "SELECT base_model, system_prompt, parameters FROM custom_models WHERE name = ?";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(DB(), sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);

        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* base_model = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            const char* system = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            const char* params = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));

            if (base_model) builder.from = base_model;
            if (system) builder.system = system;

            if (params) {
                try {
                    json params_json = json::parse(params);
                    for (auto& [key, value] : params_json.items()) {
                        if (value.is_string()) {
                            builder.parameters[key] = value.get<std::string>();
                        }
                    }
                } catch (...) {
                    // Ignore parse errors
                }
            }
        }
        sqlite3_finalize(stmt);
    }

    return builder;
}

bool ModelManager::deleteCustomModelRecord(const std::string& name) {
    if (!db_) return false;

    const char* sql = "DELETE FROM custom_models WHERE name = ?";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(DB(), sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
        int rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        return rc == SQLITE_DONE;
    }

    return false;
}

// ============================================================================
// Utilities
// ============================================================================

void ModelManager::printModelInfo(const std::string& model_name) {
    auto info = getModelInfo(model_name);

    std::cout << "\n" << utils::terminal::BOLD << "Model: " << info.name
              << utils::terminal::RESET << "\n";
    std::cout << std::string(50, '-') << "\n";

    if (!info.base_model.empty()) {
        std::cout << "Base Model:    " << info.base_model << "\n";
    }

    if (!info.description.empty()) {
        std::cout << "Size:          " << info.description << "\n";
    }

    if (!info.system_prompt.empty()) {
        std::cout << "\nSystem Prompt:\n" << utils::terminal::CYAN;
        // Truncate if too long
        if (info.system_prompt.length() > 500) {
            std::cout << info.system_prompt.substr(0, 500) << "...\n";
        } else {
            std::cout << info.system_prompt << "\n";
        }
        std::cout << utils::terminal::RESET;
    }

    if (!info.parameters.empty()) {
        std::cout << "\nParameters:\n";
        for (const auto& [key, value] : info.parameters) {
            std::cout << "  " << key << ": " << value << "\n";
        }
    }

    if (!info.template_text.empty()) {
        std::cout << "\nTemplate:\n" << utils::terminal::YELLOW;
        if (info.template_text.length() > 300) {
            std::cout << info.template_text.substr(0, 300) << "...\n";
        } else {
            std::cout << info.template_text << "\n";
        }
        std::cout << utils::terminal::RESET;
    }

    std::cout << "\n";
}

void ModelManager::printProgress(const std::string& status, int64_t completed, int64_t total) {
    if (total <= 0) {
        std::cout << "\r" << status << std::flush;
        return;
    }

    int percent = static_cast<int>((completed * 100) / total);
    int bar_width = 40;
    int filled = (percent * bar_width) / 100;

    std::cout << "\r[";
    for (int i = 0; i < bar_width; i++) {
        if (i < filled) std::cout << "=";
        else if (i == filled) std::cout << ">";
        else std::cout << " ";
    }
    std::cout << "] " << std::setw(3) << percent << "% " << status;

    // Size info
    if (completed > 0 && total > 0) {
        double completed_mb = completed / (1024.0 * 1024.0);
        double total_mb = total / (1024.0 * 1024.0);
        std::cout << std::fixed << std::setprecision(1)
                  << " (" << completed_mb << "/" << total_mb << " MB)";
    }

    std::cout << std::flush;
}

std::map<std::string, std::string> ModelManager::getAvailableParameters() {
    return {
        {"temperature", "Controls randomness (0.0-2.0, default 0.7)"},
        {"top_p", "Top-p sampling threshold (0.0-1.0, default 0.9)"},
        {"top_k", "Top-k sampling (1-100, default 40)"},
        {"num_ctx", "Context window size (default 4096)"},
        {"num_predict", "Max tokens to generate (-1 for unlimited)"},
        {"repeat_penalty", "Penalize repeated tokens (default 1.1)"},
        {"seed", "Random seed for reproducibility"},
        {"stop", "Stop sequences (comma-separated)"},
        {"mirostat", "Mirostat sampling (0=off, 1=v1, 2=v2)"},
        {"mirostat_tau", "Mirostat target entropy (default 5.0)"},
        {"mirostat_eta", "Mirostat learning rate (default 0.1)"},
        {"num_gpu", "Number of GPU layers to offload"},
        {"num_thread", "Number of CPU threads"}
    };
}

} // namespace oleg
