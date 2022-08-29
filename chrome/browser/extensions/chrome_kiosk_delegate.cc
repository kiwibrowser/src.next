// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/chrome_kiosk_delegate.h"

namespace extensions {

ChromeKioskDelegate::ChromeKioskDelegate() {}

ChromeKioskDelegate::~ChromeKioskDelegate() {}

bool ChromeKioskDelegate::IsAutoLaunchedKioskApp(const ExtensionId& id) const {
  return false;
}

}  // namespace extensions
