#!/bin/bash
#
# Build RPM package for ollamaCode
#

set -euo pipefail

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m'

VERSION="2.1.0"
RELEASE="1"
NAME="ollamacode"

echo -e "${BLUE}Building ollamaCode RPM package${NC}"
echo ""

# Check for rpmbuild
if ! command -v rpmbuild &> /dev/null; then
    echo -e "${YELLOW}Installing rpm-build tools...${NC}"
    sudo dnf install -y rpm-build rpmdevtools
fi

# Setup RPM build environment
echo -e "${YELLOW}Setting up RPM build environment...${NC}"
rpmdev-setuptree

# Get script directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Create source tarball
echo -e "${YELLOW}Creating source tarball...${NC}"
TARBALL_DIR="${HOME}/rpmbuild/SOURCES"
PACKAGE_DIR="${NAME}-${VERSION}"

mkdir -p "${TARBALL_DIR}"
mkdir -p "/tmp/${PACKAGE_DIR}"

# Copy files to package directory
cp -r "${SCRIPT_DIR}/bin" "/tmp/${PACKAGE_DIR}/"
cp -r "${SCRIPT_DIR}/lib" "/tmp/${PACKAGE_DIR}/"
mkdir -p "/tmp/${PACKAGE_DIR}/docs"
cp "${SCRIPT_DIR}/README.md" "/tmp/${PACKAGE_DIR}/docs/"
echo "MIT License" > "/tmp/${PACKAGE_DIR}/docs/LICENSE"

# Create man page
cat > "/tmp/${PACKAGE_DIR}/docs/ollamacode.1" << 'EOF'
.TH OLLAMACODE 1 "October 2025" "Version 1.0.0" "User Commands"
.SH NAME
ollamacode \- Interactive CLI for Ollama
.SH SYNOPSIS
.B ollamacode
[\fIOPTIONS\fR] [\fIPROMPT\fR]
.SH DESCRIPTION
ollamaCode is an interactive command-line interface for Ollama, providing a rich CLI experience for interacting with local LLM models.
.SH OPTIONS
.TP
.B \-m, \-\-model \fIMODEL\fR
Use specific model
.TP
.B \-t, \-\-temperature \fINUM\fR
Set temperature (0.0-2.0)
.TP
.B \-s, \-\-system \fIPROMPT\fR
Set system prompt
.TP
.B \-f, \-\-file \fIFILE\fR
Read prompt from file
.TP
.B \-l, \-\-list
List available models
.TP
.B \-h, \-\-help
Show help message
.SH AUTHOR
Written by Core.at
.SH REPORTING BUGS
Report bugs to: support@core.at
EOF

# Create tarball
cd /tmp
tar czf "${TARBALL_DIR}/${NAME}-${VERSION}.tar.gz" "${PACKAGE_DIR}"
rm -rf "/tmp/${PACKAGE_DIR}"

echo -e "${GREEN}✓ Source tarball created${NC}"

# Copy spec file
echo -e "${YELLOW}Copying spec file...${NC}"
cp "${SCRIPT_DIR}/rpm/ollamacode.spec" "${HOME}/rpmbuild/SPECS/"

# Build RPM
echo -e "${YELLOW}Building RPM package...${NC}"
cd "${HOME}/rpmbuild/SPECS"
rpmbuild -ba ollamacode.spec

if [[ $? -eq 0 ]]; then
    echo ""
    echo -e "${GREEN}✓ RPM package built successfully!${NC}"
    echo ""
    echo -e "${BLUE}Packages created:${NC}"
    find "${HOME}/rpmbuild/RPMS" -name "${NAME}*.rpm" -exec ls -lh {} \;
    find "${HOME}/rpmbuild/SRPMS" -name "${NAME}*.rpm" -exec ls -lh {} \;
    echo ""
    echo -e "${BLUE}To install:${NC}"
    echo -e "  ${CYAN}sudo rpm -ivh ${HOME}/rpmbuild/RPMS/noarch/${NAME}-${VERSION}-${RELEASE}.*.noarch.rpm${NC}"
else
    echo -e "${RED}✗ RPM build failed${NC}"
    exit 1
fi
