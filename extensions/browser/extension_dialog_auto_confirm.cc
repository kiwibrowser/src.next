// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "extensions/browser/extension_dialog_auto_confirm.h"

#include <cstring>
#include <utility>

#include "base/check.h"

namespace extensions {

namespace {
ScopedTestDialogAutoConfirm::AutoConfirm g_extension_dialog_auto_confirm_value =
    ScopedTestDialogAutoConfirm::NONE;
int g_extension_dialog_option_to_select = 0;
// Since |g_extension_dialog_justification| is global, type char[] is used
// instead of std::string to ensure trivial destruction. Note that its size is
// currently hard-coded as it's only used for testing purposes.
char g_extension_dialog_justification[20];
}

ScopedTestDialogAutoConfirm::ScopedTestDialogAutoConfirm(
    ScopedTestDialogAutoConfirm::AutoConfirm override_confirm_value)
    : old_auto_confirm_value_(
          std::exchange(g_extension_dialog_auto_confirm_value,
                        override_confirm_value)),
      // Assign a default value if unspecified.
      old_option_to_select_(0) {}

ScopedTestDialogAutoConfirm::ScopedTestDialogAutoConfirm(
    ScopedTestDialogAutoConfirm::AutoConfirm override_confirm_value,
    int override_option_to_select)
    : old_auto_confirm_value_(
          std::exchange(g_extension_dialog_auto_confirm_value,
                        override_confirm_value)),
      old_option_to_select_(std::exchange(g_extension_dialog_option_to_select,
                                          override_option_to_select)) {}

ScopedTestDialogAutoConfirm::~ScopedTestDialogAutoConfirm() {
  g_extension_dialog_auto_confirm_value = old_auto_confirm_value_;
  g_extension_dialog_option_to_select = old_option_to_select_;
  std::strcpy(g_extension_dialog_justification, old_justification_.c_str());
}

// static
ScopedTestDialogAutoConfirm::AutoConfirm
ScopedTestDialogAutoConfirm::GetAutoConfirmValue() {
  return g_extension_dialog_auto_confirm_value;
}

// static
int ScopedTestDialogAutoConfirm::GetOptionSelected() {
  return g_extension_dialog_option_to_select;
}

// static
std::string ScopedTestDialogAutoConfirm::GetJustification() {
  return g_extension_dialog_justification;
}

void ScopedTestDialogAutoConfirm::set_justification(
    const std::string& justification) {
  DCHECK(sizeof(g_extension_dialog_justification) > justification.length());
  old_justification_.assign(g_extension_dialog_justification);
  std::strcpy(g_extension_dialog_justification, justification.c_str());
}

}  // namespace extensions
