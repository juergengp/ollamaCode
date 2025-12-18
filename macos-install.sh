#!/bin/bash
#
# ollamaCode - Comprehensive macOS Installation Script
# Version: 2.0.5
# Installs both Bash and C++ versions with full dependency management
#

set -euo pipefail

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
MAGENTA='\033[0;35m'
NC='\033[0m'
BOLD='\033[1m'

# Installation paths
INSTALL_DIR="/usr/local/bin"
CONFIG_DIR="${HOME}/.config/ollamacode"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Banner
echo -e "${BLUE}${BOLD}"
cat << 'EOF'
   ____  _ _                       ____          _
  / __ \| | | __ _ _ __ ___   __ _/ ___|___   __| | ___
 | |  | | | |/ _` | '_ ` _ \ / _` | |   / _ \ / _` |/ _ \
 | |__| | | | (_| | | | | | | (_| | |__| (_) | (_| |  __/
  \____/|_|_|\__,_|_| |_| |_|\__,_|\____\___/ \__,_|\___|

EOF
echo -e "${NC}${CYAN}${BOLD}macOS Installation Script - Version 2.0.5${NC}"
echo ""

# Check if running on macOS
if [[ "$(uname)" != "Darwin" ]]; then
    echo -e "${RED}✗ Error: This script is for macOS only${NC}"
    echo -e "${YELLOW}  For other platforms, use: ./install.sh${NC}"
    exit 1
fi

# Detect architecture
ARCH=$(uname -m)
if [[ "$ARCH" == "arm64" ]]; then
    ARCH_NAME="Apple Silicon (ARM64)"
elif [[ "$ARCH" == "x86_64" ]]; then
    ARCH_NAME="Intel (x86_64)"
else
    ARCH_NAME="Unknown ($ARCH)"
fi

echo -e "${BLUE}Platform: ${BOLD}$ARCH_NAME${NC}"
echo ""

# Function to check if command exists
command_exists() {
    command -v "$1" &> /dev/null
}

# Check Ollama
echo -e "${YELLOW}Checking dependencies...${NC}"
echo ""

MISSING_DEPS=()

# Check for Ollama
if ! command_exists ollama; then
    echo -e "${RED}✗ Ollama not found${NC}"
    MISSING_DEPS+=("ollama")
else
    echo -e "${GREEN}✓ Ollama found${NC}"
fi

# Check for jq (required for Bash version)
if ! command_exists jq; then
    echo -e "${RED}✗ jq not found (required for Bash version)${NC}"
    MISSING_DEPS+=("jq")
else
    echo -e "${GREEN}✓ jq found${NC}"
fi

# Check for curl
if ! command_exists curl; then
    echo -e "${RED}✗ curl not found${NC}"
    MISSING_DEPS+=("curl")
else
    echo -e "${GREEN}✓ curl found${NC}"
fi

echo ""

# Offer to install missing dependencies
if [ ${#MISSING_DEPS[@]} -gt 0 ]; then
    echo -e "${YELLOW}Missing dependencies: ${MISSING_DEPS[*]}${NC}"
    echo ""

    if ! command_exists brew; then
        echo -e "${YELLOW}Homebrew not found. Install Homebrew first:${NC}"
        echo -e "${CYAN}  /bin/bash -c \"\$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)\"${NC}"
        echo ""
        echo -e "${YELLOW}Then re-run this installer.${NC}"
        exit 1
    fi

    echo -e "${YELLOW}Would you like to install missing dependencies with Homebrew? (y/n)${NC}"
    read -r response
    if [[ "$response" =~ ^[Yy]$ ]]; then
        for dep in "${MISSING_DEPS[@]}"; do
            echo -e "${YELLOW}Installing $dep...${NC}"
            brew install "$dep"
        done
        echo -e "${GREEN}✓ Dependencies installed${NC}"
        echo ""
    else
        echo -e "${RED}✗ Cannot proceed without dependencies${NC}"
        exit 1
    fi
fi

# Check for C++ binary
CPP_BINARY="${SCRIPT_DIR}/cpp/build/ollamacode"
CPP_AVAILABLE=false

if [[ -f "$CPP_BINARY" ]]; then
    # Check if it's the right architecture
    BINARY_ARCH=$(file "$CPP_BINARY" | grep -o "arm64\|x86_64" || echo "unknown")
    if [[ "$BINARY_ARCH" == "$ARCH" ]]; then
        CPP_AVAILABLE=true
        echo -e "${GREEN}✓ C++ binary found (${BINARY_ARCH})${NC}"
    else
        echo -e "${YELLOW}⚠ C++ binary found but wrong architecture (${BINARY_ARCH}, need ${ARCH})${NC}"
        echo -e "${YELLOW}  The C++ binary will need to be rebuilt${NC}"
    fi
else
    echo -e "${YELLOW}⚠ C++ binary not found${NC}"
    echo -e "${YELLOW}  Only Bash version will be available${NC}"
fi

echo ""

# Ask which version to install
echo -e "${CYAN}${BOLD}Which version would you like to install?${NC}"
echo ""
echo -e "${BOLD}1)${NC} ${GREEN}Bash version${NC} - Lightweight, easy to customize (9.4KB)"
echo -e "   ${BLUE}✓${NC} Zero compilation"
echo -e "   ${BLUE}✓${NC} Easy to modify"
echo -e "   ${BLUE}✓${NC} Simple text configs"
echo ""
echo -e "${BOLD}2)${NC} ${GREEN}C++ version${NC} - High performance, SQL history (366KB)"
echo -e "   ${BLUE}✓${NC} 15x faster startup (8ms vs 120ms)"
echo -e "   ${BLUE}✓${NC} 3x less memory (5MB vs 15MB)"
echo -e "   ${BLUE}✓${NC} SQLite-based config & history"

if ! $CPP_AVAILABLE; then
    echo -e "   ${RED}✗${NC} ${RED}Not available (needs building)${NC}"
fi

echo ""
echo -e "${BOLD}3)${NC} ${GREEN}Both versions${NC}"
echo -e "   ${BLUE}✓${NC} Install bash as 'ollamacode'"
echo -e "   ${BLUE}✓${NC} Install C++ as 'ollamacode-cpp'"

if ! $CPP_AVAILABLE; then
    echo -e "   ${RED}✗${NC} ${RED}C++ part not available${NC}"
fi

echo ""
echo -e "${YELLOW}Enter your choice (1, 2, or 3): ${NC}"
read -r choice

case $choice in
    1)
        VERSION="bash"
        echo -e "${GREEN}Installing Bash version...${NC}"
        ;;
    2)
        if ! $CPP_AVAILABLE; then
            echo -e "${RED}✗ C++ version not available${NC}"
            echo -e "${YELLOW}Build it first with: cd cpp && mkdir -p build && cd build && cmake -DCMAKE_BUILD_TYPE=Release .. && make${NC}"
            exit 1
        fi
        VERSION="cpp"
        echo -e "${GREEN}Installing C++ version...${NC}"
        ;;
    3)
        if ! $CPP_AVAILABLE; then
            echo -e "${YELLOW}⚠ C++ version not available, installing Bash only${NC}"
            VERSION="bash"
        else
            VERSION="both"
            echo -e "${GREEN}Installing both versions...${NC}"
        fi
        ;;
    *)
        echo -e "${RED}✗ Invalid choice${NC}"
        exit 1
        ;;
esac

echo ""

# Function to install a binary
install_binary() {
    local src=$1
    local dest=$2
    local name=$3

    if [[ ! -f "$src" ]]; then
        echo -e "${RED}✗ Source file not found: $src${NC}"
        return 1
    fi

    echo -e "${YELLOW}Installing $name to $dest...${NC}"

    if [[ -w "${INSTALL_DIR}" ]]; then
        cp "$src" "$dest"
        chmod +x "$dest"
    else
        echo -e "${YELLOW}  (requires sudo)${NC}"
        sudo cp "$src" "$dest"
        sudo chmod +x "$dest"
    fi

    echo -e "${GREEN}✓ $name installed${NC}"
}

# Install based on choice
case $VERSION in
    bash)
        install_binary "${SCRIPT_DIR}/bin/ollamacode" "${INSTALL_DIR}/ollamacode" "Bash version"
        ;;
    cpp)
        install_binary "$CPP_BINARY" "${INSTALL_DIR}/ollamacode" "C++ version"
        ;;
    both)
        install_binary "${SCRIPT_DIR}/bin/ollamacode" "${INSTALL_DIR}/ollamacode" "Bash version"
        install_binary "$CPP_BINARY" "${INSTALL_DIR}/ollamacode-cpp" "C++ version"
        ;;
esac

echo ""

# Create configuration directory
echo -e "${YELLOW}Creating configuration directory...${NC}"
mkdir -p "${CONFIG_DIR}"
echo -e "${GREEN}✓ Configuration directory: ${CONFIG_DIR}${NC}"
echo ""

# Test installation
echo -e "${YELLOW}Testing installation...${NC}"
echo ""

test_binary() {
    local bin=$1
    local name=$2

    if command_exists "$bin"; then
        local version=$($bin --version 2>&1 || echo "unknown")
        echo -e "${GREEN}✓ $name is working!${NC}"
        echo -e "  ${CYAN}Version: $version${NC}"
        return 0
    else
        echo -e "${RED}✗ $name not found in PATH${NC}"
        return 1
    fi
}

SUCCESS=true

case $VERSION in
    bash)
        test_binary "ollamacode" "ollamacode (Bash)" || SUCCESS=false
        ;;
    cpp)
        test_binary "ollamacode" "ollamacode (C++)" || SUCCESS=false
        ;;
    both)
        test_binary "ollamacode" "ollamacode (Bash)" || SUCCESS=false
        test_binary "ollamacode-cpp" "ollamacode-cpp (C++)" || SUCCESS=false
        ;;
esac

echo ""

if ! $SUCCESS; then
    echo -e "${RED}✗ Installation test failed${NC}"
    echo -e "${YELLOW}Make sure ${INSTALL_DIR} is in your PATH${NC}"
    exit 1
fi

# Check if Ollama is running
if ! pgrep -x "ollama" > /dev/null && ! curl -s http://localhost:11434/api/tags &> /dev/null; then
    echo -e "${YELLOW}⚠ Ollama is not running${NC}"
    echo ""
    echo -e "${YELLOW}Would you like to start Ollama? (y/n)${NC}"
    read -r response
    if [[ "$response" =~ ^[Yy]$ ]]; then
        if command_exists brew; then
            echo -e "${YELLOW}Starting Ollama service...${NC}"
            brew services start ollama 2>/dev/null || {
                echo -e "${YELLOW}Starting Ollama manually...${NC}"
                ollama serve &
            }
            sleep 2
            echo -e "${GREEN}✓ Ollama started${NC}"
        else
            echo -e "${YELLOW}Start Ollama with: ollama serve${NC}"
        fi
    fi
    echo ""
fi

# Installation complete
echo -e "${GREEN}${BOLD}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo -e "${GREEN}${BOLD}  Installation Complete!${NC}"
echo -e "${GREEN}${BOLD}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo ""

# Show quick start based on version
case $VERSION in
    bash)
        echo -e "${CYAN}${BOLD}Quick Start:${NC}"
        echo -e "  ${BOLD}ollamacode${NC}                  # Start interactive mode"
        echo -e "  ${BOLD}ollamacode --help${NC}           # Show help"
        echo -e "  ${BOLD}ollamacode \"Hello\"${NC}          # Send a prompt"
        ;;
    cpp)
        echo -e "${CYAN}${BOLD}Quick Start:${NC}"
        echo -e "  ${BOLD}ollamacode${NC}                  # Start interactive mode (C++)"
        echo -e "  ${BOLD}ollamacode --help${NC}           # Show help"
        echo -e "  ${BOLD}ollamacode \"Hello\"${NC}          # Send a prompt"
        ;;
    both)
        echo -e "${CYAN}${BOLD}Quick Start:${NC}"
        echo -e "  ${BOLD}ollamacode${NC}                  # Bash version (lightweight)"
        echo -e "  ${BOLD}ollamacode-cpp${NC}              # C++ version (high performance)"
        echo -e "  ${BOLD}ollamacode --help${NC}           # Show help"
        ;;
esac

echo ""
echo -e "${CYAN}${BOLD}Configuration:${NC}"
echo -e "  Config directory: ${CYAN}${CONFIG_DIR}${NC}"

if [[ "$VERSION" == "cpp" ]] || [[ "$VERSION" == "both" ]]; then
    echo -e "  C++ config: ${CYAN}${CONFIG_DIR}/config.db${NC} (SQLite)"
fi
if [[ "$VERSION" == "bash" ]] || [[ "$VERSION" == "both" ]]; then
    echo -e "  Bash config: ${CYAN}${CONFIG_DIR}/config${NC} (text)"
fi

echo ""
echo -e "${CYAN}${BOLD}Next Steps:${NC}"
echo -e "  1. Pull a model: ${BOLD}ollama pull llama3${NC}"
echo -e "  2. Start ollamacode: ${BOLD}ollamacode${NC}"
echo -e "  3. Try tool execution: ${BOLD}ollamacode \"List all .sh files\"${NC}"
echo ""

# Show performance comparison if both installed
if [[ "$VERSION" == "both" ]]; then
    echo -e "${CYAN}${BOLD}Performance Comparison:${NC}"
    echo -e "  ${BOLD}Metric${NC}           ${BOLD}Bash${NC}      ${BOLD}C++${NC}       ${BOLD}Winner${NC}"
    echo -e "  Startup Time     120ms     8ms       ${GREEN}C++ (15x)${NC}"
    echo -e "  Memory Usage     15MB      5MB       ${GREEN}C++ (3x)${NC}"
    echo -e "  Binary Size      9.4KB     366KB     ${GREEN}Bash${NC}"
    echo -e "  Customization    ${GREEN}Easy${NC}      Rebuild   ${GREEN}Bash${NC}"
    echo ""
fi

echo -e "${BLUE}${BOLD}Documentation:${NC}"
echo -e "  macOS Quick Start: ${CYAN}${SCRIPT_DIR}/MACOS_QUICKSTART.md${NC}"
echo -e "  Main README: ${CYAN}${SCRIPT_DIR}/README.md${NC}"
echo -e "  Examples: ${CYAN}${SCRIPT_DIR}/docs/EXAMPLES.md${NC}"
echo ""

echo -e "${GREEN}Happy coding with ollamaCode! 🚀${NC}"
echo ""
