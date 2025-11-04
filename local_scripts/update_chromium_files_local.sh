#!/bin/bash
# Local script to update Chromium files from upstream
# This replaces the GitHub Actions workflow for updating Chromium version

set -e  # Exit on error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}=== Update Chromium Files from Upstream (Local) ===${NC}"

# Check if all version arguments are provided
if [ -z "$1" ] || [ -z "$2" ] || [ -z "$3" ] || [ -z "$4" ]; then
    echo -e "${RED}Usage: $0 <major> <minor> <build> <patch>${NC}"
    echo -e "${YELLOW}Example: $0 93 0 4577 25${NC}"
    
    # Show current version if available
    if [ -f "CHROMIUM_VERSION" ]; then
        echo -e "${YELLOW}Current version:${NC}"
        cat CHROMIUM_VERSION
    fi
    exit 1
fi

NEW_CHROMIUM_MAJOR="$1"
NEW_CHROMIUM_MINOR="$2"
NEW_CHROMIUM_BUILD="$3"
NEW_CHROMIUM_PATCH="$4"

NEW_VERSION="${NEW_CHROMIUM_MAJOR}.${NEW_CHROMIUM_MINOR}.${NEW_CHROMIUM_BUILD}.${NEW_CHROMIUM_PATCH}"
echo -e "${YELLOW}Target Chromium version: ${NEW_VERSION}${NC}"

# Set up git user if not configured
if [ -z "$(git config user.email)" ]; then
    echo -e "${YELLOW}Configuring git user...${NC}"
    git config user.email "local@build"
    git config user.name "Local Builder"
fi

# Disable git pager
git config --global core.pager 'cat'

# Load current version
OLD_VERSION="unknown"
if [ -f "CHROMIUM_VERSION" ]; then
    source CHROMIUM_VERSION
    export $(cut -d= -f1 CHROMIUM_VERSION | grep -vF '#')
    OLD_VERSION="${CHROMIUM_MAJOR}.${CHROMIUM_MINOR}.${CHROMIUM_BUILD}.${CHROMIUM_PATCH}"
fi

echo -e "${YELLOW}Current Chromium version: ${OLD_VERSION}${NC}"

# Switch to chromium branch
CURRENT_BRANCH=$(git rev-parse --abbrev-ref HEAD)
echo -e "${GREEN}Switching to chromium branch...${NC}"
git checkout chromium

# List source files
echo -e "${GREEN}Listing source files...${NC}"
find . -type f | sed 's#\./#/#g' | grep -vFi '.gitignore' > /tmp/source_files.txt

# Clone Chromium repository
echo -e "${GREEN}Cloning Chromium repository (this may take a while)...${NC}"
rm -rf /tmp/chromium_new
git clone --filter=tree:0 --no-checkout https://github.com/chromium/chromium.git /tmp/chromium_new

# Set up sparse checkout
echo -e "${GREEN}Setting up sparse checkout...${NC}"
cd /tmp/chromium_new
cat /tmp/source_files.txt | git sparse-checkout set --stdin

# Checkout the new version
echo -e "${GREEN}Checking out version ${NEW_VERSION}...${NC}"
if ! git read-tree -mu "${NEW_VERSION}"; then
    echo -e "${RED}Error: Failed to checkout version ${NEW_VERSION}${NC}"
    echo -e "${YELLOW}Please verify that the version tag exists${NC}"
    cd -
    git checkout "$CURRENT_BRANCH"
    exit 1
fi

cd -

# Copy files
echo -e "${GREEN}Copying files to local chromium branch...${NC}"
rsync -avz --progress --exclude=".git" /tmp/chromium_new/* .

# Add files to git
echo -e "${GREEN}Adding files to git...${NC}"
git add .

# Update CHROMIUM_VERSION file
echo -e "${GREEN}Updating CHROMIUM_VERSION file...${NC}"
cat > CHROMIUM_VERSION <<EOF
CHROMIUM_MAJOR=${NEW_CHROMIUM_MAJOR}
CHROMIUM_MINOR=${NEW_CHROMIUM_MINOR}
CHROMIUM_BUILD=${NEW_CHROMIUM_BUILD}
CHROMIUM_PATCH=${NEW_CHROMIUM_PATCH}
EOF

git add CHROMIUM_VERSION

# Commit changes
echo -e "${GREEN}Committing changes...${NC}"
git commit -m "[Chromium] Update version ${OLD_VERSION} to ${NEW_VERSION}"

echo -e "${GREEN}=== Update successful! ===${NC}"
echo -e "${YELLOW}Review the changes and push with: git push origin chromium${NC}"

# Cleanup
rm -rf /tmp/chromium_new

# Return to original branch
git checkout "$CURRENT_BRANCH"
