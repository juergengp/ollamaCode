#!/bin/bash
#
# Casper - macOS Installation Script
# Version: 2.1.0
# Installs the C++ version with full feature support
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
CONFIG_DIR="${HOME}/.config/casper"
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
echo -e "${NC}${CYAN}${BOLD}macOS Installation Script - Version 2.1.0${NC}"
echo ""

# Check if running on macOS
if [[ "$(uname)" != "Darwin" ]]; then
    echo -e "${RED}Error: This script is for macOS only${NC}"
    exit 1
fi

# Detect architecture
ARCH=$(uname -m)
if [[ "$ARCH" == "arm64" ]]; then
    ARCH_NAME="Apple Silicon (ARM64)"
    BINARY_NAME="casper-arm64"
elif [[ "$ARCH" == "x86_64" ]]; then
    ARCH_NAME="Intel (x86_64)"
    BINARY_NAME="casper-x86_64"
else
    echo -e "${RED}Error: Unsupported architecture: $ARCH${NC}"
    exit 1
fi

echo -e "${BLUE}Platform: ${BOLD}$ARCH_NAME${NC}"
echo ""

# Function to check if command exists
command_exists() {
    command -v "$1" &> /dev/null
}

# Check dependencies
echo -e "${YELLOW}Checking dependencies...${NC}"
echo ""

# Check for Ollama
if ! command_exists ollama; then
    echo -e "${RED}Ollama not found${NC}"
    echo -e "${YELLOW}Install Ollama from: https://ollama.ai${NC}"
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

echo ""

# Find binary
BINARY_PATH="${SCRIPT_DIR}/bin/${BINARY_NAME}"

if [[ ! -f "$BINARY_PATH" ]]; then
    # Try the generic binary
    BINARY_PATH="${SCRIPT_DIR}/bin/casper"
fi

if [[ ! -f "$BINARY_PATH" ]]; then
    # Try build directory
    BINARY_PATH="${SCRIPT_DIR}/cpp/build/casper"
fi

if [[ ! -f "$BINARY_PATH" ]]; then
    echo -e "${RED}Error: casper binary not found${NC}"
    echo -e "${YELLOW}Build it first with:${NC}"
    echo -e "  cd cpp && mkdir -p build && cd build && cmake .. && make"
    exit 1
fi

echo -e "${GREEN}Binary found: ${BINARY_PATH}${NC}"
echo ""

# Install binary
echo -e "${YELLOW}Installing casper...${NC}"

if [[ -w "${INSTALL_DIR}" ]]; then
    cp "$BINARY_PATH" "${INSTALL_DIR}/casper"
    chmod +x "${INSTALL_DIR}/casper"
else
    echo -e "${YELLOW}(requires sudo)${NC}"
    sudo cp "$BINARY_PATH" "${INSTALL_DIR}/casper"
    sudo chmod +x "${INSTALL_DIR}/casper"
fi

echo -e "${GREEN}Installed to ${INSTALL_DIR}/casper${NC}"
echo ""

# Create configuration directory
mkdir -p "${CONFIG_DIR}"
echo -e "${GREEN}Config directory: ${CONFIG_DIR}${NC}"
echo ""

# Test installation
echo -e "${YELLOW}Testing installation...${NC}"
if command_exists casper; then
    VERSION=$(casper --version 2>&1 || echo "unknown")
    echo -e "${GREEN}casper is working!${NC}"
    echo -e "  ${CYAN}${VERSION}${NC}"
else
    echo -e "${RED}Installation failed - casper not found in PATH${NC}"
    exit 1
fi

echo ""

# Check if Ollama is running
if ! curl -s http://localhost:11434/api/tags &> /dev/null; then
    echo -e "${YELLOW}Ollama is not running${NC}"
    echo -e "${YELLOW}Start it with: ollama serve${NC}"
    echo ""
fi

# Installation complete
echo -e "${GREEN}${BOLD}Installation Complete!${NC}"
echo ""
echo -e "${CYAN}${BOLD}Quick Start:${NC}"
echo -e "  ${BOLD}casper${NC}                  # Start interactive mode"
echo -e "  ${BOLD}casper --help${NC}           # Show help"
echo -e "  ${BOLD}casper --mcp${NC}            # Start with MCP servers"
echo ""
echo -e "${CYAN}${BOLD}Key Commands:${NC}"
echo -e "  ${BOLD}/model${NC}                      # Interactive model selector"
echo -e "  ${BOLD}/host URL${NC}                   # Connect to remote Ollama"
echo -e "  ${BOLD}/mcp on${NC}                     # Enable MCP servers"
echo -e "  ${BOLD}/explore, /code, /run${NC}       # Switch agent modes"
echo ""
echo -e "${CYAN}${BOLD}Next Steps:${NC}"
echo -e "  1. Pull a model: ${BOLD}ollama pull llama3.1${NC}"
echo -e "  2. Start casper: ${BOLD}casper${NC}"
echo ""
