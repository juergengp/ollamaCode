#include "license.h"
#include "utils.h"
#include <sqlite3.h>
#include <iostream>

// Helper macro to cast db_
#define DB() (reinterpret_cast<sqlite3*>(db_))
#include <sstream>
#include <iomanip>
#include <ctime>
#include <fstream>
#include <algorithm>
#include <cctype>

#ifdef __APPLE__
#include <IOKit/IOKitLib.h>
#include <CoreFoundation/CoreFoundation.h>
#endif

namespace ollamacode {

LicenseManager::LicenseManager()
    : db_(nullptr)
    , initialized_(false)
{
    license_info_.tier = LicenseTier::Free;
    license_info_.is_valid = false;
    license_info_.is_expired = false;
    license_info_.days_remaining = -1;
}

LicenseManager::~LicenseManager() {
    if (db_) {
        sqlite3_close(DB());
    }
}

bool LicenseManager::initialize(const std::string& db_path) {
    if (initialized_) return true;

    // Determine database path
    if (db_path.empty()) {
        const char* home = getenv("HOME");
        if (!home) {
            std::cerr << "Cannot determine home directory" << std::endl;
            return false;
        }
        db_path_ = std::string(home) + "/.config/ollamacode/config.db";
    } else {
        db_path_ = db_path;
    }

    // Open database
    sqlite3* db_ptr = nullptr;
    int rc = sqlite3_open(db_path_.c_str(), &db_ptr);
    db_ = db_ptr;
    if (rc != SQLITE_OK) {
        std::cerr << "Cannot open license database: " << sqlite3_errmsg(DB()) << std::endl;
        return false;
    }

    createTables();
    loadLicense();
    validateLicense();

    initialized_ = true;
    return true;
}

void LicenseManager::createTables() {
    const char* sql = R"(
        CREATE TABLE IF NOT EXISTS license (
            id INTEGER PRIMARY KEY CHECK (id = 1),
            license_key TEXT,
            hardware_id TEXT,
            activated_at TEXT,
            expires_at TEXT,
            tier TEXT DEFAULT 'free',
            last_validated TEXT
        );
    )";

    char* errmsg = nullptr;
    int rc = sqlite3_exec(DB(), sql, nullptr, nullptr, &errmsg);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to create license table: " << errmsg << std::endl;
        sqlite3_free(errmsg);
    }
}

void LicenseManager::loadLicense() {
    const char* sql = "SELECT license_key, hardware_id, activated_at, expires_at, tier FROM license WHERE id = 1";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(DB(), sql, -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* key = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            const char* hwid = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            const char* activated = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
            const char* expires = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
            const char* tier = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));

            license_info_.key = key ? key : "";
            license_info_.hardware_id = hwid ? hwid : "";
            license_info_.activated_at = activated ? activated : "";
            license_info_.expires_at = expires ? expires : "";

            // Parse tier
            std::string tier_str = tier ? tier : "free";
            if (tier_str == "basic") {
                license_info_.tier = LicenseTier::Basic;
            } else if (tier_str == "pro") {
                license_info_.tier = LicenseTier::Pro;
            } else if (tier_str == "enterprise") {
                license_info_.tier = LicenseTier::Enterprise;
            } else {
                license_info_.tier = LicenseTier::Free;
            }
        }
        sqlite3_finalize(stmt);
    }
}

void LicenseManager::saveLicense() {
    // Convert tier to string
    std::string tier_str;
    switch (license_info_.tier) {
        case LicenseTier::Basic: tier_str = "basic"; break;
        case LicenseTier::Pro: tier_str = "pro"; break;
        case LicenseTier::Enterprise: tier_str = "enterprise"; break;
        default: tier_str = "free"; break;
    }

    const char* sql = R"(
        INSERT OR REPLACE INTO license (id, license_key, hardware_id, activated_at, expires_at, tier, last_validated)
        VALUES (1, ?, ?, ?, ?, ?, ?)
    )";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(DB(), sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, license_info_.key.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, license_info_.hardware_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, license_info_.activated_at.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, license_info_.expires_at.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 5, tier_str.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 6, getCurrentTimestamp().c_str(), -1, SQLITE_TRANSIENT);

        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
}

// ============================================================================
// Key Format: OLMA-TIER-HWID-EXPIRY-CHECK
// Example: OLMA-PRO1-A1B2-2512-X9Y8
// ============================================================================

bool LicenseManager::validateKeyFormat(const std::string& key) const {
    // Expected format: XXXX-XXXX-XXXX-XXXX-XXXX (5 groups of 4 chars)
    if (key.length() != 24) return false;

    for (size_t i = 0; i < key.length(); i++) {
        if ((i + 1) % 5 == 0) {
            if (key[i] != '-') return false;
        } else {
            if (!std::isalnum(key[i])) return false;
        }
    }

    // Check prefix
    return key.substr(0, 4) == "OLMA";
}

bool LicenseManager::validateKeyChecksum(const std::string& key) const {
    if (key.length() < 4) return false;

    // Extract checksum (last 4 chars)
    std::string check = key.substr(key.length() - 4);

    // Compute expected checksum from rest of key
    std::string data = key.substr(0, key.length() - 5);  // Remove -CHECK
    std::string expected = computeChecksum(data);

    return check == expected;
}

std::string LicenseManager::computeChecksum(const std::string& data) const {
    uint32_t crc = crc32(data);
    std::stringstream ss;
    ss << std::hex << std::uppercase << std::setfill('0') << std::setw(4) << (crc & 0xFFFF);
    return ss.str().substr(0, 4);
}

uint32_t LicenseManager::crc32(const std::string& data) const {
    uint32_t crc = 0xFFFFFFFF;
    for (char c : data) {
        crc ^= static_cast<uint8_t>(c);
        for (int i = 0; i < 8; i++) {
            crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
        }
    }
    return ~crc;
}

LicenseTier LicenseManager::extractTierFromKey(const std::string& key) const {
    if (key.length() < 9) return LicenseTier::Free;

    // Tier is in second group (positions 5-8)
    std::string tier_code = key.substr(5, 4);

    if (tier_code == "ENTR") return LicenseTier::Enterprise;
    if (tier_code == "PRO1") return LicenseTier::Pro;
    if (tier_code == "BASI") return LicenseTier::Basic;
    return LicenseTier::Free;
}

std::string LicenseManager::extractExpirationFromKey(const std::string& key) const {
    if (key.length() < 19) return "";

    // Expiry is in fourth group (positions 15-18): YYMM
    std::string expiry_code = key.substr(15, 4);

    if (expiry_code == "9999") {
        return "9999-12-31";  // Perpetual
    }

    // Parse YYMM
    try {
        int yy = std::stoi(expiry_code.substr(0, 2));
        int mm = std::stoi(expiry_code.substr(2, 2));

        // Convert to full date (last day of month)
        int year = 2000 + yy;
        int days_in_month[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
        if (mm >= 1 && mm <= 12) {
            int day = days_in_month[mm - 1];
            // Leap year check for February
            if (mm == 2 && (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0))) {
                day = 29;
            }

            std::stringstream ss;
            ss << year << "-" << std::setfill('0') << std::setw(2) << mm
               << "-" << std::setfill('0') << std::setw(2) << day;
            return ss.str();
        }
    } catch (...) {
        // Invalid format
    }

    return "";
}

bool LicenseManager::isHardwareBound(const std::string& key) const {
    if (key.length() < 14) return false;

    // HWID is in third group (positions 10-13)
    std::string hwid_code = key.substr(10, 4);

    return hwid_code != "0000";  // 0000 = floating license
}

// ============================================================================
// Hardware ID Generation
// ============================================================================

std::string LicenseManager::generateHardwareId() const {
    std::string hw_data;

#ifdef __APPLE__
    // macOS: Use IOPlatformUUID
    io_service_t platformExpert = IOServiceGetMatchingService(
        kIOMainPortDefault,
        IOServiceMatching("IOPlatformExpertDevice")
    );

    if (platformExpert) {
        CFTypeRef serialNumberAsCFString = IORegistryEntryCreateCFProperty(
            platformExpert,
            CFSTR("IOPlatformUUID"),
            kCFAllocatorDefault, 0
        );

        if (serialNumberAsCFString) {
            char buffer[512];
            if (CFStringGetCString(
                    static_cast<CFStringRef>(serialNumberAsCFString),
                    buffer, sizeof(buffer), kCFStringEncodingUTF8)) {
                hw_data = buffer;
            }
            CFRelease(serialNumberAsCFString);
        }
        IOObjectRelease(platformExpert);
    }
#else
    // Linux: Use machine-id
    std::ifstream file("/etc/machine-id");
    if (file.is_open()) {
        std::getline(file, hw_data);
        file.close();
    }
#endif

    if (hw_data.empty()) {
        hw_data = "unknown";
    }

    // Hash and truncate to 16 chars
    uint32_t hash1 = crc32(hw_data);
    uint32_t hash2 = crc32(hw_data + "salt");

    std::stringstream ss;
    ss << std::hex << std::uppercase << std::setfill('0')
       << std::setw(8) << hash1 << std::setw(8) << hash2;

    return ss.str().substr(0, 16);
}

bool LicenseManager::verifyHardwareBinding(const std::string& stored_hw_id) const {
    std::string current_hw_id = generateHardwareId();

    // Allow exact match or partial match (first 8 chars) for minor changes
    if (current_hw_id == stored_hw_id) {
        return true;
    }

    // Fuzzy match: at least 8 chars must match
    if (stored_hw_id.length() >= 8 && current_hw_id.length() >= 8) {
        return stored_hw_id.substr(0, 8) == current_hw_id.substr(0, 8);
    }

    return false;
}

// ============================================================================
// Time Helpers
// ============================================================================

std::string LicenseManager::getCurrentTimestamp() const {
    time_t now = time(nullptr);
    struct tm* tm_info = localtime(&now);

    char buffer[32];
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", tm_info);
    return std::string(buffer);
}

bool LicenseManager::isExpired(const std::string& expires_at) const {
    if (expires_at.empty() || expires_at.substr(0, 4) == "9999") {
        return false;  // Perpetual
    }

    // Parse date
    struct tm exp_tm = {};
    if (sscanf(expires_at.c_str(), "%d-%d-%d", &exp_tm.tm_year, &exp_tm.tm_mon, &exp_tm.tm_mday) != 3) {
        return true;  // Invalid date = expired
    }
    exp_tm.tm_year -= 1900;
    exp_tm.tm_mon -= 1;
    exp_tm.tm_hour = 23;
    exp_tm.tm_min = 59;
    exp_tm.tm_sec = 59;

    time_t exp_time = mktime(&exp_tm);
    time_t now = time(nullptr);

    return now > exp_time;
}

int LicenseManager::daysUntilExpiration(const std::string& expires_at) const {
    if (expires_at.empty() || expires_at.substr(0, 4) == "9999") {
        return -1;  // Perpetual
    }

    struct tm exp_tm = {};
    if (sscanf(expires_at.c_str(), "%d-%d-%d", &exp_tm.tm_year, &exp_tm.tm_mon, &exp_tm.tm_mday) != 3) {
        return 0;
    }
    exp_tm.tm_year -= 1900;
    exp_tm.tm_mon -= 1;

    time_t exp_time = mktime(&exp_tm);
    time_t now = time(nullptr);

    double diff = difftime(exp_time, now);
    return static_cast<int>(diff / (60 * 60 * 24));
}

// ============================================================================
// Activation & Validation
// ============================================================================

bool LicenseManager::activateKey(const std::string& key) {
    // Normalize key (uppercase, trim)
    std::string normalized = key;
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), ::toupper);

    // Validate format
    if (!validateKeyFormat(normalized)) {
        license_info_.error_message = "Invalid key format. Expected: OLMA-XXXX-XXXX-XXXX-XXXX";
        return false;
    }

    // Validate checksum
    if (!validateKeyChecksum(normalized)) {
        license_info_.error_message = "Invalid key checksum";
        return false;
    }

    // Extract tier and expiration
    LicenseTier tier = extractTierFromKey(normalized);
    std::string expires = extractExpirationFromKey(normalized);

    // Check expiration
    if (isExpired(expires)) {
        license_info_.error_message = "License key has expired";
        return false;
    }

    // Check hardware binding
    std::string current_hw_id = generateHardwareId();
    if (isHardwareBound(normalized)) {
        // For hardware-bound keys, we store the binding on first use
        // In a real implementation, you'd verify against a server
    }

    // Activate
    license_info_.key = normalized;
    license_info_.tier = tier;
    license_info_.expires_at = expires;
    license_info_.hardware_id = current_hw_id;
    license_info_.activated_at = getCurrentTimestamp();
    license_info_.is_valid = true;
    license_info_.is_expired = false;
    license_info_.days_remaining = daysUntilExpiration(expires);
    license_info_.error_message.clear();

    saveLicense();

    return true;
}

bool LicenseManager::deactivateKey() {
    license_info_.key.clear();
    license_info_.tier = LicenseTier::Free;
    license_info_.expires_at.clear();
    license_info_.activated_at.clear();
    license_info_.is_valid = false;
    license_info_.is_expired = false;
    license_info_.days_remaining = -1;
    license_info_.error_message.clear();

    saveLicense();
    return true;
}

bool LicenseManager::validateLicense() {
    if (license_info_.key.empty()) {
        license_info_.tier = LicenseTier::Free;
        license_info_.is_valid = false;
        return false;
    }

    // Check expiration
    if (isExpired(license_info_.expires_at)) {
        license_info_.is_expired = true;
        license_info_.is_valid = false;
        license_info_.error_message = "License has expired";
        return false;
    }

    // Check hardware binding
    if (isHardwareBound(license_info_.key)) {
        if (!verifyHardwareBinding(license_info_.hardware_id)) {
            license_info_.is_valid = false;
            license_info_.error_message = "Hardware mismatch";
            return false;
        }
    }

    license_info_.is_valid = true;
    license_info_.is_expired = false;
    license_info_.days_remaining = daysUntilExpiration(license_info_.expires_at);

    return true;
}

// ============================================================================
// Status & Feature Checking
// ============================================================================

LicenseInfo LicenseManager::getLicenseInfo() const {
    return license_info_;
}

bool LicenseManager::isActivated() const {
    return license_info_.is_valid && !license_info_.key.empty();
}

LicenseTier LicenseManager::getTier() const {
    if (!license_info_.is_valid) {
        return LicenseTier::Free;
    }
    return license_info_.tier;
}

std::string LicenseManager::getTierName() const {
    switch (getTier()) {
        case LicenseTier::Enterprise: return "Enterprise";
        case LicenseTier::Pro: return "Pro";
        case LicenseTier::Basic: return "Basic";
        default: return "Free";
    }
}

bool LicenseManager::hasFeature(Feature feature) const {
    LicenseTier tier = getTier();

    switch (feature) {
        // Free features
        case Feature::BasicChat:
        case Feature::ModelSwitch:
        case Feature::SafeMode:
        case Feature::ModelPull:
        case Feature::ModelDelete:
        case Feature::ModelShow:
            return true;

        // Basic tier
        case Feature::PromptDatabase:
        case Feature::PromptCategories:
        case Feature::PromptFavorites:
            return tier >= LicenseTier::Basic;

        // Pro tier
        case Feature::CustomModelCreation:
        case Feature::ModelPush:
        case Feature::ModelCopy:
        case Feature::PromptExport:
        case Feature::PromptImport:
        case Feature::UnlimitedPrompts:
            return tier >= LicenseTier::Pro;

        default:
            return false;
    }
}

bool LicenseManager::canUsePromptDatabase() const {
    return hasFeature(Feature::PromptDatabase);
}

bool LicenseManager::canCreateCustomModels() const {
    return hasFeature(Feature::CustomModelCreation);
}

bool LicenseManager::canPushModels() const {
    return hasFeature(Feature::ModelPush);
}

bool LicenseManager::canExportPrompts() const {
    return hasFeature(Feature::PromptExport);
}

bool LicenseManager::canImportPrompts() const {
    return hasFeature(Feature::PromptImport);
}

int LicenseManager::getMaxPrompts() const {
    switch (getTier()) {
        case LicenseTier::Enterprise: return -1;  // Unlimited
        case LicenseTier::Pro: return 500;
        case LicenseTier::Basic: return 50;
        default: return 0;  // No prompts for free tier
    }
}

std::string LicenseManager::getHardwareId() const {
    return generateHardwareId();
}

// ============================================================================
// UI Helpers
// ============================================================================

void LicenseManager::showLicenseStatus() const {
    std::cout << "\n";
    std::cout << utils::terminal::BOLD << "License Status" << utils::terminal::RESET << "\n";
    std::cout << std::string(40, '-') << "\n";

    std::cout << "Tier:        " << utils::terminal::GREEN << getTierName()
              << utils::terminal::RESET << "\n";

    if (isActivated()) {
        std::cout << "Status:      " << utils::terminal::GREEN << "Active"
                  << utils::terminal::RESET << "\n";

        if (license_info_.days_remaining >= 0) {
            std::cout << "Expires in:  " << license_info_.days_remaining << " days\n";
        } else {
            std::cout << "Expires:     Never (Perpetual)\n";
        }

        std::cout << "Activated:   " << license_info_.activated_at << "\n";
    } else {
        std::cout << "Status:      " << utils::terminal::YELLOW << "Not activated"
                  << utils::terminal::RESET << "\n";

        if (!license_info_.error_message.empty()) {
            std::cout << "Error:       " << utils::terminal::RED
                      << license_info_.error_message << utils::terminal::RESET << "\n";
        }
    }

    std::cout << "\n";
    std::cout << "Features:\n";
    std::cout << "  " << (hasFeature(Feature::PromptDatabase) ? "[Y]" : "[-]")
              << " Prompt Database\n";
    std::cout << "  " << (hasFeature(Feature::CustomModelCreation) ? "[Y]" : "[-]")
              << " Custom Model Creation\n";
    std::cout << "  " << (hasFeature(Feature::ModelPush) ? "[Y]" : "[-]")
              << " Model Push to Ollama.ai\n";
    std::cout << "  " << (hasFeature(Feature::PromptExport) ? "[Y]" : "[-]")
              << " Prompt Export/Import\n";

    int max_prompts = getMaxPrompts();
    if (max_prompts < 0) {
        std::cout << "  Max Prompts: Unlimited\n";
    } else {
        std::cout << "  Max Prompts: " << max_prompts << "\n";
    }

    std::cout << "\n";
}

void LicenseManager::showUpgradeMessage(Feature feature) const {
    std::string feature_name;
    std::string required_tier;

    switch (feature) {
        case Feature::PromptDatabase:
        case Feature::PromptCategories:
        case Feature::PromptFavorites:
            feature_name = "Prompt Database";
            required_tier = "Basic";
            break;

        case Feature::CustomModelCreation:
            feature_name = "Custom Model Creation";
            required_tier = "Pro";
            break;

        case Feature::ModelPush:
            feature_name = "Model Push";
            required_tier = "Pro";
            break;

        case Feature::PromptExport:
        case Feature::PromptImport:
            feature_name = "Prompt Export/Import";
            required_tier = "Pro";
            break;

        default:
            feature_name = "This feature";
            required_tier = "a higher tier";
    }

    std::cout << utils::terminal::YELLOW << "\n"
              << feature_name << " requires " << required_tier << " license.\n"
              << "Use '/license activate <key>' to activate your license.\n"
              << utils::terminal::RESET;
}

std::string LicenseManager::getUpgradeUrl() const {
    return "https://github.com/juergengp/ollamaCode#license";
}

} // namespace ollamacode
