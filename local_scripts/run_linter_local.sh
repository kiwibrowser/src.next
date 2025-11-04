#!/bin/bash
# Local linter script for Kiwi Browser
# This replaces the GitHub Actions workflow for running linters

set -e  # Exit on error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}=== Run Source Code Linter (Local) ===${NC}"

# Check if Docker is installed
if ! command -v docker &> /dev/null; then
    echo -e "${RED}Error: Docker is not installed${NC}"
    echo -e "${YELLOW}Please install Docker to run the linter${NC}"
    exit 1
fi

# Get the current branch
BRANCH_NAME=$(git rev-parse --abbrev-ref HEAD)
echo -e "${YELLOW}Current branch: ${BRANCH_NAME}${NC}"

# Get the repository root
REPO_ROOT=$(git rev-parse --show-toplevel)

echo -e "${GREEN}Running Super-Linter in Docker...${NC}"
echo -e "${YELLOW}This will validate changed files against linting rules${NC}"

# Run Super-Linter
docker run --rm \
  -e VALIDATE_ALL_CODEBASE=false \
  -e DEFAULT_BRANCH=kiwi \
  -e LINTER_RULES_PATH=/.github/linters/ \
  -e JAVA_FILE_NAME=google_checks.xml \
  -e RUN_LOCAL=true \
  -v "${REPO_ROOT}":/tmp/lint \
  ghcr.io/github/super-linter:slim-v4

echo -e "${GREEN}=== Linting complete! ===${NC}"
