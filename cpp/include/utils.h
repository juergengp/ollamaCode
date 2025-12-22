#ifndef OLEG_UTILS_H
#define OLEG_UTILS_H

#include <string>
#include <vector>

namespace oleg {
namespace utils {

// String utilities
std::string trim(const std::string& str);
std::vector<std::string> split(const std::string& str, char delimiter);
bool startsWith(const std::string& str, const std::string& prefix);
bool endsWith(const std::string& str, const std::string& suffix);
std::string toLower(const std::string& str);

// File utilities
bool fileExists(const std::string& path);
bool dirExists(const std::string& path);
bool createDir(const std::string& path);
std::string readFile(const std::string& path);
bool writeFile(const std::string& path, const std::string& content);

// Path utilities
std::string getHomeDir();
std::string joinPath(const std::string& p1, const std::string& p2);
std::string getBasename(const std::string& path);
std::string getDirname(const std::string& path);

// System utilities
std::string getUsername();
std::string getOsName();

// Time utilities
std::string getCurrentTimestamp();
long long getCurrentMillis();

// Terminal utilities
namespace terminal {
    // ANSI colors
    extern const char* RED;
    extern const char* GREEN;
    extern const char* YELLOW;
    extern const char* BLUE;
    extern const char* MAGENTA;
    extern const char* CYAN;
    extern const char* BOLD;
    extern const char* RESET;

    // Terminal control
    void clearScreen();
    int getTerminalWidth();

    // Colored output
    void printColor(const std::string& text, const char* color);
    void printError(const std::string& text);
    void printSuccess(const std::string& text);
    void printWarning(const std::string& text);
    void printInfo(const std::string& text);
}

} // namespace utils
} // namespace oleg

#endif // OLEG_UTILS_H
