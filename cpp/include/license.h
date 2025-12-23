#ifndef CASPER_LICENSE_H
#define CASPER_LICENSE_H

#include <string>
#include <vector>
#include <cstdint>

namespace casper {

// License tiers
enum class LicenseTier {
    Free,       // Basic features only
    Basic,      // + Prompt database (50 prompts)
    Pro,        // + Custom models, export/import (500 prompts)
    Enterprise  // All features, unlimited
};

// Features that can be gated
enum class Feature {
    // Free features (always available)
    BasicChat,
    ModelSwitch,
    SafeMode,
    ModelPull,
    ModelDelete,
    ModelShow,

    // Basic tier
    PromptDatabase,
    PromptCategories,
    PromptFavorites,

    // Pro tier
    CustomModelCreation,
    ModelPush,
    ModelCopy,
    PromptExport,
    PromptImport,
    UnlimitedPrompts
};

// License information
struct LicenseInfo {
    std::string key;
    std::string hardware_id;
    LicenseTier tier;
    std::string activated_at;
    std::string expires_at;
    bool is_valid;
    bool is_expired;
    std::string error_message;
    int days_remaining;  // -1 for perpetual
};

class LicenseManager {
public:
    LicenseManager();
    ~LicenseManager();

    // Initialize with database path
    bool initialize(const std::string& db_path = "");

    // Activation
    bool activateKey(const std::string& key);
    bool deactivateKey();
    bool validateLicense();

    // Status
    LicenseInfo getLicenseInfo() const;
    bool isActivated() const;
    LicenseTier getTier() const;
    std::string getTierName() const;

    // Feature checking
    bool hasFeature(Feature feature) const;
    bool canUsePromptDatabase() const;
    bool canCreateCustomModels() const;
    bool canPushModels() const;
    bool canExportPrompts() const;
    bool canImportPrompts() const;
    int getMaxPrompts() const;  // -1 for unlimited

    // Hardware ID
    std::string getHardwareId() const;

    // UI helpers
    void showLicenseStatus() const;
    void showUpgradeMessage(Feature feature) const;
    std::string getUpgradeUrl() const;

private:
    void* db_;  // sqlite3*
    std::string db_path_;
    LicenseInfo license_info_;
    bool initialized_;

    // Database
    void createTables();
    void loadLicense();
    void saveLicense();

    // Key validation
    bool validateKeyFormat(const std::string& key) const;
    bool validateKeyChecksum(const std::string& key) const;
    LicenseTier extractTierFromKey(const std::string& key) const;
    std::string extractExpirationFromKey(const std::string& key) const;
    bool isHardwareBound(const std::string& key) const;

    // Hardware fingerprinting
    std::string generateHardwareId() const;
    bool verifyHardwareBinding(const std::string& stored_hw_id) const;

    // Cryptographic helpers
    std::string computeChecksum(const std::string& data) const;
    uint32_t crc32(const std::string& data) const;

    // Time helpers
    std::string getCurrentTimestamp() const;
    bool isExpired(const std::string& expires_at) const;
    int daysUntilExpiration(const std::string& expires_at) const;
};

} // namespace casper

#endif // CASPER_LICENSE_H
