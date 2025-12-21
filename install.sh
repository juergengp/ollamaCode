#!/bin/bash
#
# ollamaCode - Universal Installation Script
# Version: 2.1.0
# Supports: macOS, Linux (Fedora, RHEL, CentOS, Debian, Ubuntu)
#

set -euo pipefail

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m'
BOLD='\033[1m'

# Installation paths
INSTALL_DIR="/usr/local/bin"
CONFIG_DIR="${HOME}/.config/ollamacode"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo -e "${BLUE}${BOLD}"
cat << 'EOF'
   ____  _ _                       ____          _
  / __ \| | | __ _ _ __ ___   __ _/ ___|___   __| | ___
 | |  | | | |/ _` | '_ ` _ \ / _` | |   / _ \ / _` |/ _ \
 | |__| | | | (_| | | | | | | (_| | |__| (_) | (_| |  __/
  \____/|_|_|\__,_|_| |_| |_|\__,_|\____\___/ \__,_|\___|

EOF
echo -e "${NC}${CYAN}${BOLD}Universal Installation Script - Version 2.1.0${NC}"
echo ""

# Detect OS and architecture
detect_platform() {
    OS="$(uname -s)"
    ARCH="$(uname -m)"

    case "${OS}" in
        Darwin)
            PLATFORM="macos"
            if [[ "$ARCH" == "arm64" ]]; then
                BINARY_NAME="ollamacode-arm64"
            else
                BINARY_NAME="ollamacode-x86_64"
            fi
            ;;
        Linux)
            PLATFORM="linux"
            BINARY_NAME="ollamacode-linux-${ARCH}"
            ;;
        *)
            echo -e "${RED}Error: Unsupported OS: ${OS}${NC}"
            exit 1
            ;;
    esac

    echo -e "${GREEN}Platform: ${PLATFORM} (${ARCH})${NC}"
}

# Check if command exists
command_exists() {
    command -v "$1" &> /dev/null
}

# Check dependencies
check_dependencies() {
    echo -e "${YELLOW}Checking dependencies...${NC}"

    # Check for Ollama
    if ! command_exists ollama; then
        echo -e "${RED}Ollama not found${NC}"
        echo -e "${YELLOW}Install Ollama from: https://ollama.ai${NC}"
        echo -e "${YELLOW}  curl -fsSL https://ollama.ai/install.sh | sh${NC}"
        exit 1
    else
        echo -e "${GREEN}Ollama found${NC}"
    fi

    # Check for curl
    if ! command_exists curl; then
        echo -e "${RED}curl not found${NC}"
        exit 1
    else
        echo -e "${GREEN}curl found${NC}"
    fi
}

# Find binary
find_binary() {
    BINARY_PATH=""

    # Try architecture-specific binary first
    if [[ -f "${SCRIPT_DIR}/bin/${BINARY_NAME}" ]]; then
        BINARY_PATH="${SCRIPT_DIR}/bin/${BINARY_NAME}"
    # Try generic binary
    elif [[ -f "${SCRIPT_DIR}/bin/ollamacode" ]]; then
        BINARY_PATH="${SCRIPT_DIR}/bin/ollamacode"
    # Try build directory
    elif [[ -f "${SCRIPT_DIR}/cpp/build/ollamacode" ]]; then
        BINARY_PATH="${SCRIPT_DIR}/cpp/build/ollamacode"
    fi

    if [[ -z "$BINARY_PATH" ]]; then
        echo -e "${RED}Error: ollamacode binary not found${NC}"
        echo -e "${YELLOW}Build it first with:${NC}"
        echo -e "  cd cpp && mkdir -p build && cd build && cmake .. && make"
        exit 1
    fi

    echo -e "${GREEN}Binary found: ${BINARY_PATH}${NC}"
}

# Install binary
install_binary() {
    echo -e "${YELLOW}Installing ollamacode...${NC}"

    if [[ -w "${INSTALL_DIR}" ]]; then
        cp "$BINARY_PATH" "${INSTALL_DIR}/ollamacode"
        chmod +x "${INSTALL_DIR}/ollamacode"
    else
        echo -e "${YELLOW}(requires sudo)${NC}"
        sudo cp "$BINARY_PATH" "${INSTALL_DIR}/ollamacode"
        sudo chmod +x "${INSTALL_DIR}/ollamacode"
    fi

    echo -e "${GREEN}Installed to ${INSTALL_DIR}/ollamacode${NC}"
}

# Create config directory
create_config() {
    mkdir -p "${CONFIG_DIR}"
    echo -e "${GREEN}Config directory: ${CONFIG_DIR}${NC}"
}

# Test installation
test_installation() {
    echo -e "${YELLOW}Testing installation...${NC}"

    if command_exists ollamacode; then
        VERSION=$(ollamacode --version 2>&1 || echo "unknown")
        echo -e "${GREEN}ollamacode is working!${NC}"
        echo -e "  ${CYAN}${VERSION}${NC}"
    else
        echo -e "${RED}Installation failed - ollamacode not found in PATH${NC}"
        exit 1
    fi
}

# Main installation
main() {
    detect_platform
    echo ""
    check_dependencies
    echo ""
    find_binary
    echo ""
    install_binary
    echo ""
    create_config
    echo ""
    test_installation
    echo ""

    # Check if Ollama is running
    if ! curl -s http://localhost:11434/api/tags &> /dev/null; then
        echo -e "${YELLOW}Ollama is not running${NC}"
        echo -e "${YELLOW}Start it with: ollama serve${NC}"
        echo ""
    fi

    echo -e "${GREEN}${BOLD}Installation Complete!${NC}"
    echo ""
    echo -e "${CYAN}${BOLD}Quick Start:${NC}"
    echo -e "  ${BOLD}ollamacode${NC}                  # Start interactive mode"
    echo -e "  ${BOLD}ollamacode --help${NC}           # Show help"
    echo -e "  ${BOLD}ollamacode --mcp${NC}            # Start with MCP servers"
    echo ""
    echo -e "${CYAN}${BOLD}Key Commands:${NC}"
    echo -e "  ${BOLD}/model${NC}                      # Interactive model selector"
    echo -e "  ${BOLD}/host URL${NC}                   # Connect to remote Ollama"
    echo -e "  ${BOLD}/mcp on${NC}                     # Enable MCP servers"
    echo -e "  ${BOLD}/explore, /code, /run${NC}       # Switch agent modes"
    echo ""
    echo -e "${CYAN}${BOLD}Next Steps:${NC}"
    echo -e "  1. Pull a model: ${BOLD}ollama pull llama3.1${NC}"
    echo -e "  2. Start ollamacode: ${BOLD}ollamacode${NC}"
    echo ""
}

main
