#include "utils.h"
#include <algorithm>
#include <sstream>
#include <fstream>
#include <iostream>
#include <ctime>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <pwd.h>
#include <chrono>

namespace casper {
namespace utils {

// String utilities
std::string trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\n\r");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\n\r");
    return str.substr(first, (last - first + 1));
}

std::vector<std::string> split(const std::string& str, char delimiter) {
    std::vector<std::string> tokens;
    std::string token;
    std::istringstream tokenStream(str);
    while (std::getline(tokenStream, token, delimiter)) {
        tokens.push_back(token);
    }
    return tokens;
}

bool startsWith(const std::string& str, const std::string& prefix) {
    return str.size() >= prefix.size() &&
           str.compare(0, prefix.size(), prefix) == 0;
}

bool endsWith(const std::string& str, const std::string& suffix) {
    return str.size() >= suffix.size() &&
           str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string toLower(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(), ::tolower);
    return result;
}

// File utilities
bool fileExists(const std::string& path) {
    struct stat buffer;
    return (stat(path.c_str(), &buffer) == 0 && S_ISREG(buffer.st_mode));
}

bool dirExists(const std::string& path) {
    struct stat buffer;
    return (stat(path.c_str(), &buffer) == 0 && S_ISDIR(buffer.st_mode));
}

bool createDir(const std::string& path) {
    return mkdir(path.c_str(), 0755) == 0;
}

std::string readFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return "";
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

bool writeFile(const std::string& path, const std::string& content) {
    std::ofstream file(path);
    if (!file.is_open()) {
        return false;
    }
    file << content;
    return file.good();
}

// Path utilities
std::string getHomeDir() {
    const char* home = getenv("HOME");
    if (home) {
        return std::string(home);
    }
    struct passwd* pw = getpwuid(getuid());
    if (pw) {
        return std::string(pw->pw_dir);
    }
    return "/tmp";
}

std::string joinPath(const std::string& p1, const std::string& p2) {
    if (p1.empty()) return p2;
    if (p2.empty()) return p1;
    if (p1.back() == '/') return p1 + p2;
    return p1 + "/" + p2;
}

std::string getBasename(const std::string& path) {
    size_t pos = path.find_last_of('/');
    if (pos == std::string::npos) return path;
    return path.substr(pos + 1);
}

std::string getDirname(const std::string& path) {
    size_t pos = path.find_last_of('/');
    if (pos == std::string::npos) return ".";
    if (pos == 0) return "/";
    return path.substr(0, pos);
}

// System utilities
std::string getUsername() {
    const char* user = getenv("USER");
    if (user) {
        return std::string(user);
    }
    struct passwd* pw = getpwuid(getuid());
    if (pw) {
        return std::string(pw->pw_name);
    }
    return "unknown";
}

std::string getOsName() {
    #ifdef __linux__
    return "Linux";
    #elif __APPLE__
    return "macOS";
    #elif __unix__
    return "Unix";
    #else
    return "Unknown";
    #endif
}

std::string getLinuxDistro() {
    #ifdef __linux__
    // Try to read /etc/os-release
    std::ifstream osRelease("/etc/os-release");
    if (osRelease.is_open()) {
        std::string line;
        while (std::getline(osRelease, line)) {
            if (line.find("ID=") == 0) {
                std::string distro = line.substr(3);
                // Remove quotes if present
                if (!distro.empty() && distro[0] == '"') {
                    distro = distro.substr(1, distro.length() - 2);
                }
                return distro;
            }
        }
    }
    // Fallback: check for specific files
    if (fileExists("/etc/debian_version")) return "debian";
    if (fileExists("/etc/fedora-release")) return "fedora";
    if (fileExists("/etc/centos-release")) return "centos";
    if (fileExists("/etc/arch-release")) return "arch";
    if (fileExists("/etc/SuSE-release")) return "suse";
    return "unknown";
    #else
    return "not-linux";
    #endif
}

bool commandExists(const std::string& cmd) {
    std::string check = "command -v " + cmd + " > /dev/null 2>&1";
    return (system(check.c_str()) == 0);
}

bool isMacOS() {
    #ifdef __APPLE__
    return true;
    #else
    return false;
    #endif
}

bool isLinux() {
    #ifdef __linux__
    return true;
    #else
    return false;
    #endif
}

// Time utilities
std::string getCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    char buffer[100];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", std::localtime(&time));
    return std::string(buffer);
}

long long getCurrentMillis() {
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
}

// Terminal utilities
namespace terminal {

const char* RED = "\033[0;31m";
const char* GREEN = "\033[0;32m";
const char* YELLOW = "\033[1;33m";
const char* BLUE = "\033[0;34m";
const char* MAGENTA = "\033[0;35m";
const char* CYAN = "\033[0;36m";
const char* BOLD = "\033[1m";
const char* RESET = "\033[0m";

void clearScreen() {
    std::cout << "\033[2J\033[1;1H";
}

int getTerminalWidth() {
    // Simple default, could use ioctl for actual width
    return 80;
}

void printColor(const std::string& text, const char* color) {
    std::cout << color << text << RESET;
}

void printError(const std::string& text) {
    std::cerr << RED << "✗ " << text << RESET << std::endl;
}

void printSuccess(const std::string& text) {
    std::cout << GREEN << "✓ " << text << RESET << std::endl;
}

void printWarning(const std::string& text) {
    std::cout << YELLOW << "⚠ " << text << RESET << std::endl;
}

void printInfo(const std::string& text) {
    std::cout << CYAN << text << RESET << std::endl;
}

} // namespace terminal

} // namespace utils
} // namespace casper
