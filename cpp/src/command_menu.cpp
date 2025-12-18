#include "command_menu.h"
#include "utils.h"
#include <iostream>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <algorithm>
#include <cstring>

namespace ollamacode {

// Key codes
namespace keys {
    const int ESCAPE = 27;
    const int ENTER = 10;
    const int BACKSPACE = 127;
    const int TAB = 9;
    const int ARROW_UP = 1000;
    const int ARROW_DOWN = 1001;
    const int ARROW_LEFT = 1002;
    const int ARROW_RIGHT = 1003;
}

CommandMenu::CommandMenu()
    : orig_termios_(new struct termios)
    , raw_mode_enabled_(false)
    , menu_lines_rendered_(0)
{
    // Define available commands with descriptions
    commands_ = {
        {"help",    "Show available commands",           "/help"},
        {"models",  "List available Ollama models",      "/models"},
        {"model",   "Interactive model selector",        "/model"},
        {"use",     "Switch to a different model",       "/use MODEL"},
        {"temp",    "Set temperature (0.0-2.0)",         "/temp NUM"},
        {"safe",    "Toggle safe mode",                  "/safe [on|off]"},
        {"auto",    "Toggle auto-approve for tools",     "/auto [on|off]"},
        {"config",  "Show current configuration",        "/config"},
        {"clear",   "Clear the screen",                  "/clear"},
        {"exit",    "Exit ollamacode",                   "/exit"},
        {"quit",    "Exit ollamacode",                   "/quit"},
    };
}

CommandMenu::~CommandMenu() {
    if (raw_mode_enabled_) {
        disableRawMode();
    }
    delete orig_termios_;
}

void CommandMenu::enableRawMode() {
    if (raw_mode_enabled_) return;

    tcgetattr(STDIN_FILENO, orig_termios_);

    struct termios raw = *orig_termios_;
    // Disable canonical mode, echo, and signals
    raw.c_lflag &= ~(ICANON | ECHO | ISIG);
    // Set minimum chars and timeout
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
    raw_mode_enabled_ = true;
}

void CommandMenu::disableRawMode() {
    if (!raw_mode_enabled_) return;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, orig_termios_);
    raw_mode_enabled_ = false;
}

int CommandMenu::readKey() {
    char c;
    if (read(STDIN_FILENO, &c, 1) != 1) {
        return -1;
    }

    // Handle escape sequences for arrow keys
    if (c == 27) {  // Escape
        char seq[3];
        // Try to read more characters (arrow key sequence)
        // Set non-blocking temporarily
        struct termios temp;
        tcgetattr(STDIN_FILENO, &temp);
        temp.c_cc[VMIN] = 0;
        temp.c_cc[VTIME] = 0;
        tcsetattr(STDIN_FILENO, TCSANOW, &temp);

        int nread = read(STDIN_FILENO, &seq[0], 1);
        if (nread == 0) {
            // Restore blocking mode
            temp.c_cc[VMIN] = 1;
            tcsetattr(STDIN_FILENO, TCSANOW, &temp);
            return keys::ESCAPE;  // Just escape key
        }

        if (seq[0] == '[') {
            read(STDIN_FILENO, &seq[1], 1);
            // Restore blocking mode
            temp.c_cc[VMIN] = 1;
            tcsetattr(STDIN_FILENO, TCSANOW, &temp);

            switch (seq[1]) {
                case 'A': return keys::ARROW_UP;
                case 'B': return keys::ARROW_DOWN;
                case 'C': return keys::ARROW_RIGHT;
                case 'D': return keys::ARROW_LEFT;
            }
        }

        // Restore blocking mode
        temp.c_cc[VMIN] = 1;
        tcsetattr(STDIN_FILENO, TCSANOW, &temp);
        return keys::ESCAPE;
    }

    return static_cast<int>(c);
}

void CommandMenu::clearLines(int count) {
    for (int i = 0; i < count; i++) {
        std::cout << "\033[2K";  // Clear line
        if (i < count - 1) {
            std::cout << "\033[1A";  // Move up
        }
    }
    std::cout << "\r";  // Return to beginning of line
    std::cout.flush();
}

void CommandMenu::moveCursorUp(int lines) {
    if (lines > 0) {
        std::cout << "\033[" << lines << "A";
        std::cout.flush();
    }
}

void CommandMenu::saveCursor() {
    std::cout << "\033[s";
    std::cout.flush();
}

void CommandMenu::restoreCursor() {
    std::cout << "\033[u";
    std::cout.flush();
}

void CommandMenu::hideCursor() {
    std::cout << "\033[?25l";
    std::cout.flush();
}

void CommandMenu::showCursor() {
    std::cout << "\033[?25h";
    std::cout.flush();
}

std::vector<int> CommandMenu::filterCommands(const std::string& filter) {
    std::vector<int> matches;
    std::string lowerFilter = utils::toLower(filter);

    // Remove leading slash if present
    std::string searchTerm = lowerFilter;
    if (!searchTerm.empty() && searchTerm[0] == '/') {
        searchTerm = searchTerm.substr(1);
    }

    for (size_t i = 0; i < commands_.size(); i++) {
        std::string lowerName = utils::toLower(commands_[i].name);
        std::string lowerDesc = utils::toLower(commands_[i].description);

        // Match if command name starts with filter or description contains filter
        if (searchTerm.empty() ||
            lowerName.find(searchTerm) == 0 ||
            lowerDesc.find(searchTerm) != std::string::npos) {
            matches.push_back(i);
        }
    }

    return matches;
}

void CommandMenu::renderMenu(const std::string& filter, int selected_index,
                            const std::vector<int>& matching_indices) {
    // Calculate how many lines we'll need
    int lines_needed = 1;  // Input line
    if (matching_indices.empty()) {
        lines_needed = 2;  // Input + "no matches" message
    } else {
        const int max_visible = 8;
        int visible_count = std::min(static_cast<int>(matching_indices.size()), max_visible);
        lines_needed = visible_count + 1;  // items + input line
        if (matching_indices.size() > static_cast<size_t>(max_visible)) {
            lines_needed++;  // scroll indicator
        }
        lines_needed++;  // hint line
    }

    // Clear previous menu: first move to start of rendered area, then clear each line
    if (menu_lines_rendered_ > 0) {
        // We're currently at the input line (top of menu)
        // Clear each line from top to bottom without moving around
        for (int i = 0; i < menu_lines_rendered_; i++) {
            std::cout << "\033[2K";  // Clear current line
            if (i < menu_lines_rendered_ - 1) {
                std::cout << "\033[1B";  // Move down
            }
        }
        // Move back up to the start
        if (menu_lines_rendered_ > 1) {
            std::cout << "\033[" << (menu_lines_rendered_ - 1) << "A";
        }
        std::cout << "\r";
        std::cout.flush();
    } else {
        // First render: reserve space by printing newlines to trigger any needed scrolling
        // This ensures the terminal scrolls before we start positioning content
        for (int i = 0; i < lines_needed - 1; i++) {
            std::cout << "\n";
        }
        // Move back up to where we started
        if (lines_needed > 1) {
            std::cout << "\033[" << (lines_needed - 1) << "A";
        }
        std::cout << "\r";
        std::cout.flush();
    }

    // Print current input line
    std::cout << utils::terminal::BOLD << utils::terminal::CYAN
              << "You> " << utils::terminal::RESET << filter;

    // If no matches, show message
    if (matching_indices.empty()) {
        std::cout << "\n" << utils::terminal::YELLOW
                  << "  No matching commands" << utils::terminal::RESET;
        menu_lines_rendered_ = 2;
        std::cout << "\033[1A\033[" << (5 + filter.length()) << "G";  // Move back to input line
        std::cout.flush();
        return;
    }

    // Show dropdown box
    std::cout << "\n";

    // Limit displayed items
    const int max_visible = 8;
    int start_idx = 0;
    int visible_count = std::min(static_cast<int>(matching_indices.size()), max_visible);

    // Scroll if selected is past visible area
    if (selected_index >= start_idx + visible_count) {
        start_idx = selected_index - visible_count + 1;
    }
    if (selected_index < start_idx) {
        start_idx = selected_index;
    }

    // Render items
    for (int i = 0; i < visible_count; i++) {
        int cmd_idx = matching_indices[start_idx + i];
        const auto& cmd = commands_[cmd_idx];

        bool is_selected = (start_idx + i == selected_index);

        // Selection indicator
        if (is_selected) {
            std::cout << utils::terminal::CYAN << "▸ " << utils::terminal::RESET;
        } else {
            std::cout << "  ";
        }

        // Command name
        if (is_selected) {
            std::cout << utils::terminal::BOLD << utils::terminal::GREEN;
        } else {
            std::cout << utils::terminal::GREEN;
        }
        std::cout << "/" << cmd.name << utils::terminal::RESET;

        // Padding and description
        int name_len = cmd.name.length() + 1;  // +1 for slash
        int padding = 14 - name_len;
        for (int p = 0; p < padding; p++) std::cout << " ";

        std::cout << utils::terminal::YELLOW << cmd.description << utils::terminal::RESET;

        // Usage hint if different from name
        if (cmd.usage != "/" + cmd.name) {
            std::cout << utils::terminal::BLUE << "  " << cmd.usage << utils::terminal::RESET;
        }

        std::cout << "\n";
    }

    // Show scroll indicator if needed
    if (matching_indices.size() > static_cast<size_t>(max_visible)) {
        std::cout << utils::terminal::MAGENTA << "  ↑↓ "
                  << (start_idx + 1) << "-" << (start_idx + visible_count)
                  << " of " << matching_indices.size() << utils::terminal::RESET << "\n";
        menu_lines_rendered_ = visible_count + 3;  // input + items + scroll indicator
    } else {
        menu_lines_rendered_ = visible_count + 1;  // input + items
    }

    // Hint line
    std::cout << utils::terminal::BLUE
              << "  ↑↓ Navigate • Enter Select • Esc Cancel • Type to filter"
              << utils::terminal::RESET;
    menu_lines_rendered_++;

    // Move cursor back to input line
    std::cout << "\033[" << (menu_lines_rendered_ - 1) << "A";  // Move up
    std::cout << "\033[" << (5 + filter.length()) << "G";       // Move to end of input
    std::cout.flush();
}

MenuResult CommandMenu::show(const std::string& initial_input) {
    MenuResult result;
    result.selected = false;
    result.cancelled = false;
    result.command = "";

    std::string input = initial_input;
    int selected_index = 0;
    std::vector<int> matches = filterCommands(input);

    enableRawMode();
    hideCursor();

    // Initial render
    renderMenu(input, selected_index, matches);

    while (true) {
        int key = readKey();

        if (key == keys::ESCAPE) {
            // Cancel
            result.cancelled = true;
            break;
        }
        else if (key == keys::ENTER) {
            // Select current item or use typed input
            result.selected = true;
            if (!matches.empty() && selected_index < static_cast<int>(matches.size())) {
                result.command = "/" + commands_[matches[selected_index]].name;
            } else {
                result.command = input;
            }
            break;
        }
        else if (key == keys::TAB) {
            // Tab completion - fill in selected command
            if (!matches.empty() && selected_index < static_cast<int>(matches.size())) {
                input = "/" + commands_[matches[selected_index]].name;
                matches = filterCommands(input);
                selected_index = 0;
            }
        }
        else if (key == keys::ARROW_UP) {
            // Move selection up
            if (selected_index > 0) {
                selected_index--;
            } else if (!matches.empty()) {
                selected_index = matches.size() - 1;  // Wrap to bottom
            }
        }
        else if (key == keys::ARROW_DOWN) {
            // Move selection down
            if (selected_index < static_cast<int>(matches.size()) - 1) {
                selected_index++;
            } else {
                selected_index = 0;  // Wrap to top
            }
        }
        else if (key == keys::BACKSPACE) {
            // Delete character
            if (input.length() > 1) {  // Keep at least "/"
                input.pop_back();
                matches = filterCommands(input);
                selected_index = 0;
            } else if (input == "/") {
                // Backspace on just "/" cancels
                result.cancelled = true;
                break;
            }
        }
        else if (key >= 32 && key < 127) {
            // Regular character input
            input += static_cast<char>(key);
            matches = filterCommands(input);
            selected_index = 0;
        }

        renderMenu(input, selected_index, matches);
    }

    // Clear the menu
    showCursor();
    disableRawMode();

    // Clean up the display
    if (menu_lines_rendered_ > 0) {
        // Move to end of menu and clear
        std::cout << "\033[" << (menu_lines_rendered_ - 1) << "B";  // Move down
        clearLines(menu_lines_rendered_);
    }

    return result;
}

} // namespace ollamacode
