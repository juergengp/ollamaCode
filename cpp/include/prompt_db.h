#ifndef OLEG_PROMPT_DB_H
#define OLEG_PROMPT_DB_H

#include <string>
#include <vector>
#include <cstdint>

namespace oleg {

// Forward declaration
class LicenseManager;

// Prompt structure
struct Prompt {
    int64_t id;
    std::string name;
    std::string content;
    std::string description;
    std::string category;
    std::vector<std::string> tags;
    std::string created_at;
    std::string updated_at;
    int usage_count;
    bool is_favorite;

    Prompt() : id(0), usage_count(0), is_favorite(false) {}
};

// Prompt category
struct PromptCategory {
    int64_t id;
    std::string name;
    std::string description;
    std::string color;  // Hex color for UI

    PromptCategory() : id(0) {}
};

class PromptDatabase {
public:
    PromptDatabase();
    ~PromptDatabase();

    // Initialize database
    bool initialize(const std::string& db_path = "");

    // Set license manager for feature gating
    void setLicenseManager(LicenseManager* license);

    // ===== Prompt CRUD =====

    // Add a new prompt
    int64_t addPrompt(const Prompt& prompt);

    // Update existing prompt
    bool updatePrompt(const Prompt& prompt);

    // Delete prompt by ID
    bool deletePrompt(int64_t id);

    // Delete prompt by name
    bool deletePromptByName(const std::string& name);

    // ===== Prompt Retrieval =====

    // Get prompt by ID
    Prompt getPrompt(int64_t id);

    // Get prompt by name
    Prompt getPromptByName(const std::string& name);

    // Get all prompts
    std::vector<Prompt> getAllPrompts();

    // Get prompts by category
    std::vector<Prompt> getPromptsByCategory(const std::string& category);

    // Search prompts by name/content/description
    std::vector<Prompt> searchPrompts(const std::string& query);

    // Get favorite prompts
    std::vector<Prompt> getFavorites();

    // Get recently used prompts
    std::vector<Prompt> getRecentlyUsed(int limit = 10);

    // Get most used prompts
    std::vector<Prompt> getMostUsed(int limit = 10);

    // ===== Categories =====

    // Add category
    int64_t addCategory(const PromptCategory& category);

    // Delete category
    bool deleteCategory(const std::string& name);

    // Get all categories
    std::vector<PromptCategory> getCategories();

    // Get category by name
    PromptCategory getCategory(const std::string& name);

    // ===== Usage Tracking =====

    // Increment usage count
    void incrementUsageCount(int64_t prompt_id);

    // Toggle favorite status
    bool toggleFavorite(int64_t prompt_id);

    // ===== Import/Export =====

    // Export prompts to JSON file
    bool exportToJson(const std::string& file_path);

    // Import prompts from JSON file
    bool importFromJson(const std::string& file_path);

    // Export prompts to Markdown file
    bool exportToMarkdown(const std::string& file_path);

    // ===== Interactive UI =====

    // Show interactive prompt selector
    std::string showPromptSelector();

    // Show prompt add dialog
    Prompt showAddPromptDialog();

    // Show prompt edit dialog
    bool showEditPromptDialog(const std::string& name);

    // ===== Statistics =====

    // Get total prompt count
    int getPromptCount();

    // Get prompts per category count
    std::vector<std::pair<std::string, int>> getPromptsPerCategory();

private:
    void* db_;  // sqlite3*
    std::string db_path_;
    LicenseManager* license_;
    bool initialized_;

    // Database helpers
    void createTables();
    void insertDefaultCategories();
    Prompt promptFromRow(void* stmt);  // sqlite3_stmt*
    PromptCategory categoryFromRow(void* stmt);  // sqlite3_stmt*
    std::string getCurrentTimestamp();

    // Tags helpers
    std::string tagsToJson(const std::vector<std::string>& tags);
    std::vector<std::string> jsonToTags(const std::string& json_str);

    // License check
    bool checkLicense(const std::string& operation);
    bool checkPromptLimit();
};

} // namespace oleg

#endif // OLEG_PROMPT_DB_H
