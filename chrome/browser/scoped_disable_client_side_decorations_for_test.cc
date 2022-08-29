// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/scoped_disable_client_side_decorations_for_test.h"

#if defined(USE_OZONE)
#include "ui/ozone/public/ozone_platform.h"
#endif

namespace ui {

ScopedDisableClientSideDecorationsForTest::
    ScopedDisableClientSideDecorationsForTest() {
#if defined(USE_OZONE)
  if (auto* platform_utils = OzonePlatform::GetInstance()->GetPlatformUtils()) {
    disabled_csd_ = platform_utils->DisableClientSideDecorationsForTest();
  }
#endif
}

ScopedDisableClientSideDecorationsForTest::
    ~ScopedDisableClientSideDecorationsForTest() = default;

}  // namespace ui
