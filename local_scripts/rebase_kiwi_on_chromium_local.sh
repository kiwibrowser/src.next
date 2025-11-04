#!/bin/bash
# Local rebase script for Kiwi Browser
# This replaces the GitHub Actions workflow for rebasing kiwi on top of chromium

set -e  # Exit on error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}=== Rebase Kiwi on top of Chromium (Local) ===${NC}"

# Set up git user if not configured
if [ -z "$(git config user.email)" ]; then
    echo -e "${YELLOW}Configuring git user...${NC}"
    git config user.email "local@build"
    git config user.name "Local Builder"
fi

# Check if we have a chromium branch
if ! git show-ref --verify --quiet refs/heads/chromium; then
    echo -e "${RED}Error: chromium branch not found${NC}"
    exit 1
fi

# Save current branch
CURRENT_BRANCH=$(git rev-parse --abbrev-ref HEAD)
echo -e "${YELLOW}Current branch: ${CURRENT_BRANCH}${NC}"

# Fetch latest changes
echo -e "${GREEN}Fetching latest changes...${NC}"
git fetch origin

# Switch to chromium branch
echo -e "${GREEN}Switching to chromium branch...${NC}"
git checkout chromium

# Pull latest chromium changes
echo -e "${GREEN}Pulling latest chromium changes...${NC}"
git pull origin chromium

# Switch to kiwi branch
echo -e "${GREEN}Switching to kiwi branch...${NC}"
git checkout kiwi

# Pull latest kiwi changes
echo -e "${GREEN}Pulling latest kiwi changes...${NC}"
git pull origin kiwi

# Perform the rebase
echo -e "${GREEN}Rebasing kiwi on top of chromium...${NC}"
if git rebase chromium --committer-date-is-author-date; then
    echo -e "${GREEN}=== Rebase successful! ===${NC}"
    echo -e "${YELLOW}To push changes, run: git push origin kiwi --force${NC}"
else
    echo -e "${RED}=== Rebase failed with conflicts ===${NC}"
    echo -e "${YELLOW}Resolve conflicts and run: git rebase --continue${NC}"
    exit 1
fi

# Return to original branch if it wasn't kiwi
if [ "$CURRENT_BRANCH" != "kiwi" ]; then
    git checkout "$CURRENT_BRANCH"
fi
