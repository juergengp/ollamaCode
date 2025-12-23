#ifndef CASPER_COMMAND_MENU_H
#define CASPER_COMMAND_MENU_H

#include <string>
#include <vector>
#include <functional>
#include <termios.h>

namespace casper {

// Command definition with name and description
struct CommandDef {
    std::string name;        // Command without leading slash (e.g., "help")
    std::string description; // Brief description
    std::string usage;       // Usage hint (e.g., "/use MODEL")
};

// Result from command menu interaction
struct MenuResult {
    bool selected;           // true if user selected a command
    bool cancelled;          // true if user pressed Escape
    std::string command;     // The selected/typed command (with /)
};

class CommandMenu {
public:
    CommandMenu();
    ~CommandMenu();

    // Show the command dropdown menu starting from input
    // Returns the final input string (selected command or typed input)
    MenuResult show(const std::string& initial_input = "/");

    // Get all available commands
    const std::vector<CommandDef>& getCommands() const { return commands_; }

private:
    // Command definitions
    std::vector<CommandDef> commands_;

    // Terminal helpers
    void enableRawMode();
    void disableRawMode();
    int readKey();
    void clearLines(int count);
    void moveCursorUp(int lines);
    void saveCursor();
    void restoreCursor();
    void hideCursor();
    void showCursor();

    // Render the dropdown menu
    void renderMenu(const std::string& filter, int selected_index,
                   const std::vector<int>& matching_indices);

    // Filter commands based on input
    std::vector<int> filterCommands(const std::string& filter);

    // Store original terminal settings
    ::termios* orig_termios_;
    bool raw_mode_enabled_;
    int menu_lines_rendered_;
};

} // namespace casper

#endif // CASPER_COMMAND_MENU_H
