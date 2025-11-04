#!/bin/bash
# Local APK build script for Kiwi Browser
# This replaces the GitHub Actions workflow for building APK

set -e  # Exit on error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}=== Kiwi Browser Local APK Build ===${NC}"

# Get the current branch name
BRANCH_NAME=$(git rev-parse --abbrev-ref HEAD)
echo -e "${YELLOW}Current branch: ${BRANCH_NAME}${NC}"

# Set up build directory
BUILD_DIR="./out/Default"
APK_OUTPUT_DIR="./build_output"

mkdir -p "$APK_OUTPUT_DIR"

echo -e "${GREEN}Starting build process...${NC}"

# Check if build system is set up
if [ ! -f "build/config.gni" ]; then
    echo -e "${RED}Error: Build system not configured. Please run gn gen first.${NC}"
    exit 1
fi

# Build the APK
echo -e "${GREEN}Building APK (this may take a while)...${NC}"
autoninja -C "$BUILD_DIR" chrome_public_apk

# Copy the built APK to output directory
if [ -f "${BUILD_DIR}/apks/ChromePublic.apk" ]; then
    TIMESTAMP=$(date +%Y%m%d_%H%M%S)
    OUTPUT_APK="${APK_OUTPUT_DIR}/Kiwi_${BRANCH_NAME}_${TIMESTAMP}_arm64.apk"
    cp "${BUILD_DIR}/apks/ChromePublic.apk" "$OUTPUT_APK"
    
    echo -e "${GREEN}=== Build successful! ===${NC}"
    echo -e "${GREEN}APK location: ${OUTPUT_APK}${NC}"
    
    # Get APK size
    SIZE=$(du -h "$OUTPUT_APK" | cut -f1)
    echo -e "${YELLOW}APK size: ${SIZE}${NC}"
else
    echo -e "${RED}Error: APK not found at expected location${NC}"
    exit 1
fi
