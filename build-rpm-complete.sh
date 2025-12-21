#!/bin/bash
#
# Build Complete RPM package for ollamaCode (Bash + C++ versions)
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

echo -e "${BLUE}╔════════════════════════════════════════════════════════╗${NC}"
echo -e "${BLUE}║   Building ollamaCode Complete RPM (Bash + C++)      ║${NC}"
echo -e "${BLUE}╚════════════════════════════════════════════════════════╝${NC}"
echo ""

# Check for required tools
echo -e "${YELLOW}Checking build dependencies...${NC}"

if ! command -v rpmbuild &> /dev/null; then
    echo -e "${YELLOW}Installing rpm-build tools...${NC}"
    sudo dnf install -y rpm-build rpmdevtools
fi

if ! command -v cmake &> /dev/null; then
    echo -e "${YELLOW}Installing CMake...${NC}"
    sudo dnf install -y cmake
fi

if ! command -v g++ &> /dev/null; then
    echo -e "${YELLOW}Installing C++ compiler...${NC}"
    sudo dnf install -y gcc-c++
fi

# Check for build dependencies
for pkg in libcurl-devel sqlite-devel readline-devel; do
    if ! rpm -q $pkg &> /dev/null; then
        echo -e "${YELLOW}Installing $pkg...${NC}"
        sudo dnf install -y $pkg
    fi
done

echo -e "${GREEN}✓ All build dependencies met${NC}"
echo ""

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

# Copy all project files
echo -e "${CYAN}  Copying Bash version...${NC}"
cp -r "${SCRIPT_DIR}/bin" "/tmp/${PACKAGE_DIR}/"
cp -r "${SCRIPT_DIR}/lib" "/tmp/${PACKAGE_DIR}/"

echo -e "${CYAN}  Copying C++ version...${NC}"
mkdir -p "/tmp/${PACKAGE_DIR}/cpp"
cp -r "${SCRIPT_DIR}/cpp/src" "/tmp/${PACKAGE_DIR}/cpp/"
cp -r "${SCRIPT_DIR}/cpp/include" "/tmp/${PACKAGE_DIR}/cpp/"
cp "${SCRIPT_DIR}/cpp/CMakeLists.txt" "/tmp/${PACKAGE_DIR}/cpp/"
cp "${SCRIPT_DIR}/cpp/build.sh" "/tmp/${PACKAGE_DIR}/cpp/" 2>/dev/null || true

echo -e "${CYAN}  Copying documentation...${NC}"
mkdir -p "/tmp/${PACKAGE_DIR}/docs"
cp "${SCRIPT_DIR}/README-v2.md" "/tmp/${PACKAGE_DIR}/"
cp "${SCRIPT_DIR}/SESSION_MANAGEMENT_IMPLEMENTATION.md" "/tmp/${PACKAGE_DIR}/" 2>/dev/null || true
cp "${SCRIPT_DIR}/CPP_SESSION_MANAGEMENT_COMPLETE.md" "/tmp/${PACKAGE_DIR}/" 2>/dev/null || true
cp "${SCRIPT_DIR}/cpp/SESSION_MANAGEMENT.md" "/tmp/${PACKAGE_DIR}/cpp/" 2>/dev/null || true
cp "${SCRIPT_DIR}/cpp/BUILD.md" "/tmp/${PACKAGE_DIR}/cpp/" 2>/dev/null || true
cp "${SCRIPT_DIR}/docs/QUICKSTART.md" "/tmp/${PACKAGE_DIR}/docs/" 2>/dev/null || true
cp "${SCRIPT_DIR}/docs/EXAMPLES.md" "/tmp/${PACKAGE_DIR}/docs/" 2>/dev/null || true

# Create tarball
echo -e "${CYAN}  Creating tarball...${NC}"
cd /tmp
tar czf "${TARBALL_DIR}/${NAME}-${VERSION}.tar.gz" "${PACKAGE_DIR}"
rm -rf "/tmp/${PACKAGE_DIR}"

echo -e "${GREEN}✓ Source tarball created${NC}"
echo ""

# Copy spec file
echo -e "${YELLOW}Copying spec file...${NC}"
cp "${SCRIPT_DIR}/rpm/ollamacode-complete.spec" "${HOME}/rpmbuild/SPECS/ollamacode.spec"
echo -e "${GREEN}✓ Spec file ready${NC}"
echo ""

# Build RPM
echo -e "${YELLOW}Building RPM package...${NC}"
echo -e "${CYAN}  This will compile the C++ version, may take a minute...${NC}"
echo ""

cd "${HOME}/rpmbuild/SPECS"
rpmbuild -ba ollamacode.spec 2>&1 | tee /tmp/rpmbuild.log

if [[ ${PIPESTATUS[0]} -eq 0 ]]; then
    echo ""
    echo -e "${GREEN}╔════════════════════════════════════════════════════════╗${NC}"
    echo -e "${GREEN}║          RPM Package Built Successfully!              ║${NC}"
    echo -e "${GREEN}╚════════════════════════════════════════════════════════╝${NC}"
    echo ""

    echo -e "${BLUE}📦 Binary RPM:${NC}"
    find "${HOME}/rpmbuild/RPMS" -name "${NAME}*.rpm" -exec ls -lh {} \;
    echo ""

    echo -e "${BLUE}📦 Source RPM:${NC}"
    find "${HOME}/rpmbuild/SRPMS" -name "${NAME}*.rpm" -exec ls -lh {} \;
    echo ""

    RPM_PATH=$(find "${HOME}/rpmbuild/RPMS" -name "${NAME}-${VERSION}-${RELEASE}.*.rpm" | head -1)

    echo -e "${BLUE}═══════════════════════════════════════════════════════${NC}"
    echo -e "${BLUE}Installation Instructions:${NC}"
    echo -e "${BLUE}═══════════════════════════════════════════════════════${NC}"
    echo ""
    echo -e "${CYAN}Local installation:${NC}"
    echo -e "  sudo rpm -ivh $RPM_PATH"
    echo ""
    echo -e "${CYAN}Upgrade existing installation:${NC}"
    echo -e "  sudo rpm -Uvh $RPM_PATH"
    echo ""
    echo -e "${CYAN}Copy to other servers:${NC}"
    echo -e "  scp $RPM_PATH user@server:/tmp/"
    echo -e "  ssh user@server 'sudo rpm -ivh /tmp/${NAME}-${VERSION}-${RELEASE}.*.rpm'"
    echo ""
    echo -e "${BLUE}═══════════════════════════════════════════════════════${NC}"
    echo -e "${BLUE}What Gets Installed:${NC}"
    echo -e "${BLUE}═══════════════════════════════════════════════════════${NC}"
    echo ""
    echo -e "${GREEN}Binaries:${NC}"
    echo -e "  /usr/bin/ollamacode          (C++ version, default)"
    echo -e "  /usr/bin/ollamacode-bash     (Bash version)"
    echo ""
    echo -e "${GREEN}Libraries:${NC}"
    echo -e "  /usr/local/lib/ollamacode/   (Bash libraries)"
    echo ""
    echo -e "${GREEN}Documentation:${NC}"
    echo -e "  /usr/share/doc/ollamacode/"
    echo ""
    echo -e "${GREEN}Man Page:${NC}"
    echo -e "  man ollamacode"
    echo ""
    echo -e "${GREEN}Config (per-user):${NC}"
    echo -e "  ~/.config/ollamacode/config     (Bash)"
    echo -e "  ~/.config/ollamacode/config.db  (C++)"
    echo -e "  ~/.config/ollamacode/sessions/  (C++ sessions)"
    echo ""
    echo -e "${BLUE}═══════════════════════════════════════════════════════${NC}"
    echo -e "${BLUE}Quick Start After Install:${NC}"
    echo -e "${BLUE}═══════════════════════════════════════════════════════${NC}"
    echo ""
    echo -e "  ${CYAN}ollamacode${NC}              # Start C++ version (with session mgmt)"
    echo -e "  ${CYAN}ollamacode --resume${NC}     # Resume last session"
    echo -e "  ${CYAN}ollamacode-bash${NC}         # Start Bash version"
    echo -e "  ${CYAN}man ollamacode${NC}          # Read manual"
    echo ""
else
    echo ""
    echo -e "${RED}╔════════════════════════════════════════════════════════╗${NC}"
    echo -e "${RED}║              RPM Build Failed!                        ║${NC}"
    echo -e "${RED}╚════════════════════════════════════════════════════════╝${NC}"
    echo ""
    echo -e "${YELLOW}Check the build log:${NC}"
    echo -e "  less /tmp/rpmbuild.log"
    echo ""
    exit 1
fi
