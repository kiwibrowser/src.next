#!/bin/bash
# Local script to import file from Chromium
# This replaces the GitHub Actions workflow for importing files from Chromium

set -e  # Exit on error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}=== Import File from Chromium (Local) ===${NC}"

# Check if path argument is provided
if [ -z "$1" ]; then
    echo -e "${RED}Usage: $0 <path-in-chromium-repo>${NC}"
    echo -e "${YELLOW}Example: $0 chrome/android/java/res/drawable-mdpi/ic_launcher.png${NC}"
    exit 1
fi

FILE_PATH="$1"

# Set up git user if not configured
if [ -z "$(git config user.email)" ]; then
    echo -e "${YELLOW}Configuring git user...${NC}"
    git config user.email "local@build"
    git config user.name "Local Builder"
fi

# Load Chromium version
if [ ! -f "CHROMIUM_VERSION" ]; then
    echo -e "${RED}Error: CHROMIUM_VERSION file not found${NC}"
    exit 1
fi

source CHROMIUM_VERSION
export $(cut -d= -f1 CHROMIUM_VERSION | grep -vF '#')

CHROMIUM_TAG="${CHROMIUM_MAJOR}.${CHROMIUM_MINOR}.${CHROMIUM_BUILD}.${CHROMIUM_PATCH}"
echo -e "${YELLOW}Chromium version: ${CHROMIUM_TAG}${NC}"

# Switch to chromium branch
CURRENT_BRANCH=$(git rev-parse --abbrev-ref HEAD)
echo -e "${GREEN}Switching to chromium branch...${NC}"
git checkout chromium

# Download the file
echo -e "${GREEN}Downloading file: ${FILE_PATH}${NC}"
FILE_URL="https://chromium.googlesource.com/chromium/src/+/refs/tags/${CHROMIUM_TAG}/${FILE_PATH}?format=TEXT"

wget "$FILE_URL" -O /tmp/file.txt

if [ ! -s /tmp/file.txt ]; then
    echo -e "${RED}Error: Failed to download file${NC}"
    git checkout "$CURRENT_BRANCH"
    exit 1
fi

# Create directory structure
mkdir -p "$(dirname "${FILE_PATH}")"

# Decode and save file
echo -e "${GREEN}Decoding and saving file...${NC}"
base64 --decode /tmp/file.txt > "${FILE_PATH}"

# Add file to git
git add "${FILE_PATH}"

# Import adjacent drawable/mipmap folders if it's an image
if [[ "$FILE_PATH" == *"drawable-"* ]] || [[ "$FILE_PATH" == *"mipmap-"* ]]; then
    echo -e "${GREEN}Importing adjacent resolution folders...${NC}"
    
    for image_folder in drawable mipmap; do
        for resolution_folder in mdpi hdpi xhdpi xxhdpi xxxhdpi; do
            echo -e "${YELLOW}Processing ${image_folder}-${resolution_folder}${NC}"
            
            adjacent_target_file=$(echo "$FILE_PATH" | sed -E "s#(\-mdpi|\-hdpi|\-xhdpi|\-xxhdpi|\-xxxhdpi)/#-${resolution_folder}/#g")
            
            rm -f /tmp/file.txt
            adj_url="https://chromium.googlesource.com/chromium/src/+/refs/tags/${CHROMIUM_TAG}/${adjacent_target_file}?format=TEXT"
            
            if wget "$adj_url" -O /tmp/file.txt 2>/dev/null && [ -s /tmp/file.txt ]; then
                mkdir -p "$(dirname "${adjacent_target_file}")"
                base64 --decode /tmp/file.txt > "${adjacent_target_file}"
                git add "${adjacent_target_file}"
                echo -e "${GREEN}  ✓ Imported ${adjacent_target_file}${NC}"
            fi
        done
    done
fi

# Commit the changes
echo -e "${GREEN}Committing changes...${NC}"
git commit --allow-empty -m "[Chromium] Importing ${FILE_PATH} from version ${CHROMIUM_TAG}"

echo -e "${GREEN}=== Import successful! ===${NC}"
echo -e "${YELLOW}To push changes, run: git push origin chromium${NC}"

# Return to original branch
git checkout "$CURRENT_BRANCH"
