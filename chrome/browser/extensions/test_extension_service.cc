// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/test_extension_service.h"

#include "chrome/browser/extensions/crx_installer.h"
#include "testing/gtest/include/gtest/gtest.h"

using extensions::Extension;

TestExtensionService::~TestExtensionService() {}

extensions::PendingExtensionManager*
TestExtensionService::pending_extension_manager() {
  ADD_FAILURE();
  return NULL;
}

extensions::CorruptedExtensionReinstaller*
TestExtensionService::corrupted_extension_reinstaller() {
  ADD_FAILURE();
  return nullptr;
}

bool TestExtensionService::UpdateExtension(
    const extensions::CRXFileInfo& file,
    bool file_ownership_passed,
    extensions::CrxInstaller** out_crx_installer) {
  ADD_FAILURE();
  return false;
}

const Extension* TestExtensionService::GetPendingExtensionUpdate(
    const std::string& id) const {
  ADD_FAILURE();
  return NULL;
}

bool TestExtensionService::FinishDelayedInstallationIfReady(
    const std::string& extension_id,
    bool install_immediately) {
  ADD_FAILURE();
  return false;
}

bool TestExtensionService::IsExtensionEnabled(
    const std::string& extension_id) const {
  ADD_FAILURE();
  return false;
}

void TestExtensionService::CheckManagementPolicy() {
  ADD_FAILURE();
}

void TestExtensionService::CheckForUpdatesSoon() {
  ADD_FAILURE();
}

void TestExtensionService::AddExtension(const Extension* extension) {
  ADD_FAILURE();
}

void TestExtensionService::AddComponentExtension(const Extension* extension) {
  ADD_FAILURE();
}

void TestExtensionService::UnloadExtension(
    const std::string& extension_id,
    extensions::UnloadedExtensionReason reason) {
  ADD_FAILURE();
}

void TestExtensionService::RemoveComponentExtension(
    const std::string& extension_id) {
  ADD_FAILURE();
}

bool TestExtensionService::UserCanDisableInstalledExtension(
    const std::string& extension_id) {
  ADD_FAILURE();
  return false;
}

void TestExtensionService::ReinstallProviderExtensions() {
  ADD_FAILURE();
}
