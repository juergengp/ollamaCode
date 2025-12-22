#include "prompt_db.h"
#include "license.h"
#include "utils.h"
#include "json.hpp"
#include <sqlite3.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <ctime>
#include <termios.h>
#include <unistd.h>

using json = nlohmann::json;

// Helper macro to cast db_
#define DB() (reinterpret_cast<sqlite3*>(db_))

namespace oleg {

PromptDatabase::PromptDatabase()
    : db_(nullptr)
    , license_(nullptr)
    , initialized_(false)
{
}

PromptDatabase::~PromptDatabase() {
    if (db_) {
        sqlite3_close(DB());
    }
}

bool PromptDatabase::initialize(const std::string& db_path) {
    if (initialized_) return true;

    // Determine database path
    if (db_path.empty()) {
        const char* home = getenv("HOME");
        if (!home) {
            std::cerr << "Cannot determine home directory" << std::endl;
            return false;
        }
        db_path_ = std::string(home) + "/.config/oleg/config.db";
    } else {
        db_path_ = db_path;
    }

    // Open database
    sqlite3* db_ptr = nullptr;
    int rc = sqlite3_open(db_path_.c_str(), &db_ptr);
    db_ = db_ptr;
    if (rc != SQLITE_OK) {
        std::cerr << "Cannot open prompt database: " << sqlite3_errmsg(DB()) << std::endl;
        return false;
    }

    createTables();
    insertDefaultCategories();

    initialized_ = true;
    return true;
}

void PromptDatabase::setLicenseManager(LicenseManager* license) {
    license_ = license;
}

void PromptDatabase::createTables() {
    const char* sql = R"(
        CREATE TABLE IF NOT EXISTS prompts (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT NOT NULL UNIQUE,
            content TEXT NOT NULL,
            description TEXT,
            category TEXT DEFAULT 'general',
            tags TEXT,
            created_at TEXT NOT NULL,
            updated_at TEXT NOT NULL,
            usage_count INTEGER DEFAULT 0,
            is_favorite INTEGER DEFAULT 0
        );
        CREATE INDEX IF NOT EXISTS idx_prompts_category ON prompts(category);
        CREATE INDEX IF NOT EXISTS idx_prompts_name ON prompts(name);
        CREATE INDEX IF NOT EXISTS idx_prompts_favorite ON prompts(is_favorite);

        CREATE TABLE IF NOT EXISTS prompt_categories (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT NOT NULL UNIQUE,
            description TEXT,
            color TEXT
        );
    )";

    char* errmsg = nullptr;
    int rc = sqlite3_exec(DB(), sql, nullptr, nullptr, &errmsg);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to create prompt tables: " << errmsg << std::endl;
        sqlite3_free(errmsg);
    }
}

void PromptDatabase::insertDefaultCategories() {
    // Check if categories exist
    sqlite3_stmt* stmt;
    const char* check_sql = "SELECT COUNT(*) FROM prompt_categories";
    if (sqlite3_prepare_v2(DB(), check_sql, -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW && sqlite3_column_int(stmt, 0) > 0) {
            sqlite3_finalize(stmt);
            return;  // Categories already exist
        }
        sqlite3_finalize(stmt);
    }

    // Insert default categories
    const char* sql = R"(
        INSERT OR IGNORE INTO prompt_categories (name, description, color) VALUES
        ('general', 'General purpose prompts', '#808080'),
        ('coding', 'Programming and development prompts', '#00FF00'),
        ('writing', 'Writing and content creation prompts', '#0000FF'),
        ('analysis', 'Data analysis and research prompts', '#FF00FF'),
        ('creative', 'Creative and brainstorming prompts', '#FFFF00'),
        ('system', 'System and assistant role prompts', '#FF0000')
    )";

    char* errmsg = nullptr;
    sqlite3_exec(DB(), sql, nullptr, nullptr, &errmsg);
    if (errmsg) sqlite3_free(errmsg);
}

std::string PromptDatabase::getCurrentTimestamp() {
    time_t now = time(nullptr);
    struct tm* tm_info = localtime(&now);
    char buffer[32];
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", tm_info);
    return std::string(buffer);
}

std::string PromptDatabase::tagsToJson(const std::vector<std::string>& tags) {
    json j = tags;
    return j.dump();
}

std::vector<std::string> PromptDatabase::jsonToTags(const std::string& json_str) {
    std::vector<std::string> tags;
    if (json_str.empty()) return tags;

    try {
        json j = json::parse(json_str);
        if (j.is_array()) {
            for (const auto& item : j) {
                if (item.is_string()) {
                    tags.push_back(item.get<std::string>());
                }
            }
        }
    } catch (...) {
        // Ignore parse errors
    }

    return tags;
}

bool PromptDatabase::checkLicense(const std::string& operation) {
    if (!license_) return true;

    if (!license_->canUsePromptDatabase()) {
        license_->showUpgradeMessage(Feature::PromptDatabase);
        return false;
    }

    if (operation == "export" && !license_->canExportPrompts()) {
        license_->showUpgradeMessage(Feature::PromptExport);
        return false;
    }

    if (operation == "import" && !license_->canImportPrompts()) {
        license_->showUpgradeMessage(Feature::PromptImport);
        return false;
    }

    return true;
}

bool PromptDatabase::checkPromptLimit() {
    if (!license_) return true;

    int max_prompts = license_->getMaxPrompts();
    if (max_prompts < 0) return true;  // Unlimited

    int current_count = getPromptCount();
    if (current_count >= max_prompts) {
        std::cout << utils::terminal::YELLOW
                  << "Prompt limit reached (" << current_count << "/" << max_prompts << "). "
                  << "Upgrade to add more prompts.\n"
                  << utils::terminal::RESET;
        return false;
    }

    return true;
}

Prompt PromptDatabase::promptFromRow(void* stmt_ptr) {
    sqlite3_stmt* stmt = reinterpret_cast<sqlite3_stmt*>(stmt_ptr);
    Prompt p;
    p.id = sqlite3_column_int64(stmt, 0);

    const char* name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
    const char* content = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
    const char* description = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
    const char* category = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
    const char* tags = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
    const char* created = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
    const char* updated = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));

    p.name = name ? name : "";
    p.content = content ? content : "";
    p.description = description ? description : "";
    p.category = category ? category : "general";
    p.tags = jsonToTags(tags ? tags : "");
    p.created_at = created ? created : "";
    p.updated_at = updated ? updated : "";
    p.usage_count = sqlite3_column_int(stmt, 8);
    p.is_favorite = sqlite3_column_int(stmt, 9) != 0;

    return p;
}

PromptCategory PromptDatabase::categoryFromRow(void* stmt_ptr) {
    sqlite3_stmt* stmt = reinterpret_cast<sqlite3_stmt*>(stmt_ptr);
    PromptCategory c;
    c.id = sqlite3_column_int64(stmt, 0);

    const char* name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
    const char* description = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
    const char* color = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));

    c.name = name ? name : "";
    c.description = description ? description : "";
    c.color = color ? color : "#808080";

    return c;
}

// ============================================================================
// Prompt CRUD
// ============================================================================

int64_t PromptDatabase::addPrompt(const Prompt& prompt) {
    if (!checkLicense("add")) return -1;
    if (!checkPromptLimit()) return -1;

    const char* sql = R"(
        INSERT INTO prompts (name, content, description, category, tags, created_at, updated_at)
        VALUES (?, ?, ?, ?, ?, ?, ?)
    )";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(DB(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return -1;
    }

    std::string timestamp = getCurrentTimestamp();
    std::string tags_json = tagsToJson(prompt.tags);

    sqlite3_bind_text(stmt, 1, prompt.name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, prompt.content.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, prompt.description.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, prompt.category.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, tags_json.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, timestamp.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 7, timestamp.c_str(), -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(stmt);
    int64_t id = (rc == SQLITE_DONE) ? sqlite3_last_insert_rowid(DB()) : -1;
    sqlite3_finalize(stmt);

    return id;
}

bool PromptDatabase::updatePrompt(const Prompt& prompt) {
    if (!checkLicense("update")) return false;

    const char* sql = R"(
        UPDATE prompts SET content = ?, description = ?, category = ?, tags = ?, updated_at = ?
        WHERE id = ?
    )";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(DB(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    std::string timestamp = getCurrentTimestamp();
    std::string tags_json = tagsToJson(prompt.tags);

    sqlite3_bind_text(stmt, 1, prompt.content.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, prompt.description.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, prompt.category.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, tags_json.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, timestamp.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 6, prompt.id);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return rc == SQLITE_DONE;
}

bool PromptDatabase::deletePrompt(int64_t id) {
    if (!checkLicense("delete")) return false;

    const char* sql = "DELETE FROM prompts WHERE id = ?";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(DB(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_int64(stmt, 1, id);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return rc == SQLITE_DONE;
}

bool PromptDatabase::deletePromptByName(const std::string& name) {
    if (!checkLicense("delete")) return false;

    const char* sql = "DELETE FROM prompts WHERE name = ?";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(DB(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return rc == SQLITE_DONE;
}

// ============================================================================
// Prompt Retrieval
// ============================================================================

Prompt PromptDatabase::getPrompt(int64_t id) {
    Prompt p;

    const char* sql = "SELECT * FROM prompts WHERE id = ?";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(DB(), sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, id);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            p = promptFromRow(stmt);
        }
        sqlite3_finalize(stmt);
    }

    return p;
}

Prompt PromptDatabase::getPromptByName(const std::string& name) {
    Prompt p;

    const char* sql = "SELECT * FROM prompts WHERE name = ?";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(DB(), sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            p = promptFromRow(stmt);
        }
        sqlite3_finalize(stmt);
    }

    return p;
}

std::vector<Prompt> PromptDatabase::getAllPrompts() {
    std::vector<Prompt> prompts;

    const char* sql = "SELECT * FROM prompts ORDER BY name";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(DB(), sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            prompts.push_back(promptFromRow(stmt));
        }
        sqlite3_finalize(stmt);
    }

    return prompts;
}

std::vector<Prompt> PromptDatabase::getPromptsByCategory(const std::string& category) {
    std::vector<Prompt> prompts;

    const char* sql = "SELECT * FROM prompts WHERE category = ? ORDER BY name";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(DB(), sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, category.c_str(), -1, SQLITE_TRANSIENT);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            prompts.push_back(promptFromRow(stmt));
        }
        sqlite3_finalize(stmt);
    }

    return prompts;
}

std::vector<Prompt> PromptDatabase::searchPrompts(const std::string& query) {
    std::vector<Prompt> prompts;

    const char* sql = R"(
        SELECT * FROM prompts
        WHERE name LIKE ? OR content LIKE ? OR description LIKE ?
        ORDER BY name
    )";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(DB(), sql, -1, &stmt, nullptr) == SQLITE_OK) {
        std::string pattern = "%" + query + "%";
        sqlite3_bind_text(stmt, 1, pattern.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, pattern.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, pattern.c_str(), -1, SQLITE_TRANSIENT);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            prompts.push_back(promptFromRow(stmt));
        }
        sqlite3_finalize(stmt);
    }

    return prompts;
}

std::vector<Prompt> PromptDatabase::getFavorites() {
    std::vector<Prompt> prompts;

    const char* sql = "SELECT * FROM prompts WHERE is_favorite = 1 ORDER BY name";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(DB(), sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            prompts.push_back(promptFromRow(stmt));
        }
        sqlite3_finalize(stmt);
    }

    return prompts;
}

std::vector<Prompt> PromptDatabase::getRecentlyUsed(int limit) {
    std::vector<Prompt> prompts;

    const char* sql = "SELECT * FROM prompts WHERE usage_count > 0 ORDER BY updated_at DESC LIMIT ?";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(DB(), sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, limit);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            prompts.push_back(promptFromRow(stmt));
        }
        sqlite3_finalize(stmt);
    }

    return prompts;
}

std::vector<Prompt> PromptDatabase::getMostUsed(int limit) {
    std::vector<Prompt> prompts;

    const char* sql = "SELECT * FROM prompts ORDER BY usage_count DESC LIMIT ?";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(DB(), sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, limit);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            prompts.push_back(promptFromRow(stmt));
        }
        sqlite3_finalize(stmt);
    }

    return prompts;
}

// ============================================================================
// Categories
// ============================================================================

int64_t PromptDatabase::addCategory(const PromptCategory& category) {
    const char* sql = "INSERT INTO prompt_categories (name, description, color) VALUES (?, ?, ?)";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(DB(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return -1;
    }

    sqlite3_bind_text(stmt, 1, category.name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, category.description.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, category.color.c_str(), -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(stmt);
    int64_t id = (rc == SQLITE_DONE) ? sqlite3_last_insert_rowid(DB()) : -1;
    sqlite3_finalize(stmt);

    return id;
}

bool PromptDatabase::deleteCategory(const std::string& name) {
    const char* sql = "DELETE FROM prompt_categories WHERE name = ?";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(DB(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return rc == SQLITE_DONE;
}

std::vector<PromptCategory> PromptDatabase::getCategories() {
    std::vector<PromptCategory> categories;

    const char* sql = "SELECT * FROM prompt_categories ORDER BY name";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(DB(), sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            categories.push_back(categoryFromRow(stmt));
        }
        sqlite3_finalize(stmt);
    }

    return categories;
}

PromptCategory PromptDatabase::getCategory(const std::string& name) {
    PromptCategory c;

    const char* sql = "SELECT * FROM prompt_categories WHERE name = ?";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(DB(), sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            c = categoryFromRow(stmt);
        }
        sqlite3_finalize(stmt);
    }

    return c;
}

// ============================================================================
// Usage Tracking
// ============================================================================

void PromptDatabase::incrementUsageCount(int64_t prompt_id) {
    const char* sql = "UPDATE prompts SET usage_count = usage_count + 1, updated_at = ? WHERE id = ?";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(DB(), sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, getCurrentTimestamp().c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 2, prompt_id);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
}

bool PromptDatabase::toggleFavorite(int64_t prompt_id) {
    const char* sql = "UPDATE prompts SET is_favorite = NOT is_favorite WHERE id = ?";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(DB(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_int64(stmt, 1, prompt_id);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return rc == SQLITE_DONE;
}

// ============================================================================
// Import/Export
// ============================================================================

bool PromptDatabase::exportToJson(const std::string& file_path) {
    if (!checkLicense("export")) return false;

    auto prompts = getAllPrompts();

    json j;
    j["version"] = "1.0";
    j["exported_at"] = getCurrentTimestamp();
    j["prompts"] = json::array();

    for (const auto& p : prompts) {
        json prompt_json;
        prompt_json["name"] = p.name;
        prompt_json["content"] = p.content;
        prompt_json["description"] = p.description;
        prompt_json["category"] = p.category;
        prompt_json["tags"] = p.tags;
        prompt_json["is_favorite"] = p.is_favorite;
        j["prompts"].push_back(prompt_json);
    }

    std::ofstream file(file_path);
    if (!file.is_open()) {
        std::cout << utils::terminal::RED << "Cannot open file: " << file_path
                  << utils::terminal::RESET << "\n";
        return false;
    }

    file << j.dump(2);
    file.close();

    std::cout << utils::terminal::GREEN << "Exported " << prompts.size()
              << " prompts to " << file_path << utils::terminal::RESET << "\n";

    return true;
}

bool PromptDatabase::importFromJson(const std::string& file_path) {
    if (!checkLicense("import")) return false;

    std::ifstream file(file_path);
    if (!file.is_open()) {
        std::cout << utils::terminal::RED << "Cannot open file: " << file_path
                  << utils::terminal::RESET << "\n";
        return false;
    }

    json j;
    try {
        file >> j;
    } catch (const std::exception& e) {
        std::cout << utils::terminal::RED << "Invalid JSON: " << e.what()
                  << utils::terminal::RESET << "\n";
        return false;
    }
    file.close();

    if (!j.contains("prompts") || !j["prompts"].is_array()) {
        std::cout << utils::terminal::RED << "Invalid format: missing 'prompts' array"
                  << utils::terminal::RESET << "\n";
        return false;
    }

    int imported = 0;
    int skipped = 0;

    for (const auto& item : j["prompts"]) {
        Prompt p;
        p.name = item.value("name", "");
        p.content = item.value("content", "");
        p.description = item.value("description", "");
        p.category = item.value("category", "general");
        p.is_favorite = item.value("is_favorite", false);

        if (item.contains("tags") && item["tags"].is_array()) {
            for (const auto& tag : item["tags"]) {
                if (tag.is_string()) {
                    p.tags.push_back(tag.get<std::string>());
                }
            }
        }

        if (p.name.empty() || p.content.empty()) {
            skipped++;
            continue;
        }

        // Check if prompt already exists
        auto existing = getPromptByName(p.name);
        if (existing.id > 0) {
            skipped++;
            continue;
        }

        if (addPrompt(p) > 0) {
            imported++;
        } else {
            skipped++;
        }
    }

    std::cout << utils::terminal::GREEN << "Imported " << imported << " prompts";
    if (skipped > 0) {
        std::cout << " (" << skipped << " skipped)";
    }
    std::cout << utils::terminal::RESET << "\n";

    return imported > 0;
}

bool PromptDatabase::exportToMarkdown(const std::string& file_path) {
    if (!checkLicense("export")) return false;

    auto prompts = getAllPrompts();
    auto categories = getCategories();

    std::ofstream file(file_path);
    if (!file.is_open()) {
        std::cout << utils::terminal::RED << "Cannot open file: " << file_path
                  << utils::terminal::RESET << "\n";
        return false;
    }

    file << "# Prompt Library\n\n";
    file << "Exported: " << getCurrentTimestamp() << "\n\n";

    // Group by category
    for (const auto& cat : categories) {
        auto cat_prompts = getPromptsByCategory(cat.name);
        if (cat_prompts.empty()) continue;

        file << "## " << cat.name << "\n\n";

        for (const auto& p : cat_prompts) {
            file << "### " << p.name;
            if (p.is_favorite) file << " ⭐";
            file << "\n\n";

            if (!p.description.empty()) {
                file << "*" << p.description << "*\n\n";
            }

            file << "```\n" << p.content << "\n```\n\n";
        }
    }

    file.close();

    std::cout << utils::terminal::GREEN << "Exported to " << file_path
              << utils::terminal::RESET << "\n";

    return true;
}

// ============================================================================
// Interactive UI
// ============================================================================

std::string PromptDatabase::showPromptSelector() {
    if (!checkLicense("select")) return "";

    auto prompts = getAllPrompts();
    if (prompts.empty()) {
        std::cout << utils::terminal::YELLOW << "No prompts found. Use '/prompt add' to create one."
                  << utils::terminal::RESET << "\n";
        return "";
    }

    std::cout << "\n" << utils::terminal::BOLD << "Select Prompt"
              << utils::terminal::RESET << "\n";
    std::cout << std::string(40, '-') << "\n\n";

    // Display prompts with numbers
    for (size_t i = 0; i < prompts.size(); i++) {
        const auto& p = prompts[i];
        std::cout << "  [" << (i + 1) << "] ";

        if (p.is_favorite) {
            std::cout << utils::terminal::YELLOW << "⭐ " << utils::terminal::RESET;
        }

        std::cout << utils::terminal::GREEN << p.name << utils::terminal::RESET;
        std::cout << " (" << p.category << ")";

        if (!p.description.empty()) {
            std::string desc = p.description;
            if (desc.length() > 40) desc = desc.substr(0, 40) + "...";
            std::cout << " - " << desc;
        }
        std::cout << "\n";
    }

    std::cout << "\nEnter number (or 'q' to cancel): ";
    std::string input;
    std::getline(std::cin, input);

    if (input == "q" || input.empty()) {
        return "";
    }

    try {
        int choice = std::stoi(input);
        if (choice >= 1 && choice <= static_cast<int>(prompts.size())) {
            const auto& selected = prompts[choice - 1];
            incrementUsageCount(selected.id);
            return selected.content;
        }
    } catch (...) {
        // Invalid input
    }

    std::cout << utils::terminal::RED << "Invalid selection"
              << utils::terminal::RESET << "\n";
    return "";
}

Prompt PromptDatabase::showAddPromptDialog() {
    Prompt p;

    std::cout << "\n" << utils::terminal::BOLD << "Add New Prompt"
              << utils::terminal::RESET << "\n";
    std::cout << std::string(40, '-') << "\n\n";

    // Name
    std::cout << "Name: ";
    std::getline(std::cin, p.name);
    if (p.name.empty()) {
        std::cout << utils::terminal::RED << "Name is required"
                  << utils::terminal::RESET << "\n";
        return p;
    }

    // Description
    std::cout << "Description (optional): ";
    std::getline(std::cin, p.description);

    // Category
    auto categories = getCategories();
    std::cout << "\nCategories: ";
    for (size_t i = 0; i < categories.size(); i++) {
        std::cout << categories[i].name;
        if (i < categories.size() - 1) std::cout << ", ";
    }
    std::cout << "\nCategory [general]: ";
    std::getline(std::cin, p.category);
    if (p.category.empty()) p.category = "general";

    // Content
    std::cout << "\nPrompt content (press Enter twice to finish):\n";
    std::string line;
    std::stringstream content_ss;
    bool first_line = true;
    while (std::getline(std::cin, line)) {
        if (line.empty() && !first_line) break;
        if (!first_line) content_ss << "\n";
        content_ss << line;
        first_line = false;
    }
    p.content = content_ss.str();

    if (p.content.empty()) {
        std::cout << utils::terminal::RED << "Content is required"
                  << utils::terminal::RESET << "\n";
        return Prompt();
    }

    return p;
}

bool PromptDatabase::showEditPromptDialog(const std::string& name) {
    auto p = getPromptByName(name);
    if (p.id == 0) {
        std::cout << utils::terminal::RED << "Prompt not found: " << name
                  << utils::terminal::RESET << "\n";
        return false;
    }

    std::cout << "\n" << utils::terminal::BOLD << "Edit Prompt: " << name
              << utils::terminal::RESET << "\n";
    std::cout << std::string(40, '-') << "\n";
    std::cout << "(Press Enter to keep current value)\n\n";

    // Description
    std::cout << "Description [" << p.description << "]: ";
    std::string input;
    std::getline(std::cin, input);
    if (!input.empty()) p.description = input;

    // Category
    std::cout << "Category [" << p.category << "]: ";
    std::getline(std::cin, input);
    if (!input.empty()) p.category = input;

    // Content
    std::cout << "\nCurrent content:\n" << utils::terminal::CYAN
              << p.content << utils::terminal::RESET << "\n";
    std::cout << "\nNew content (Enter twice to finish, empty to keep current):\n";

    std::stringstream content_ss;
    bool first_line = true;
    bool has_content = false;
    while (std::getline(std::cin, input)) {
        if (input.empty()) {
            if (first_line || !has_content) break;
            break;
        }
        if (!first_line) content_ss << "\n";
        content_ss << input;
        first_line = false;
        has_content = true;
    }

    if (has_content) {
        p.content = content_ss.str();
    }

    return updatePrompt(p);
}

// ============================================================================
// Statistics
// ============================================================================

int PromptDatabase::getPromptCount() {
    int count = 0;

    const char* sql = "SELECT COUNT(*) FROM prompts";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(DB(), sql, -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            count = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }

    return count;
}

std::vector<std::pair<std::string, int>> PromptDatabase::getPromptsPerCategory() {
    std::vector<std::pair<std::string, int>> result;

    const char* sql = "SELECT category, COUNT(*) FROM prompts GROUP BY category ORDER BY category";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(DB(), sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* cat = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            int count = sqlite3_column_int(stmt, 1);
            result.push_back({cat ? cat : "unknown", count});
        }
        sqlite3_finalize(stmt);
    }

    return result;
}

} // namespace oleg
