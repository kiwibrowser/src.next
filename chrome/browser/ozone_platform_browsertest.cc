// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/test/base/in_process_browser_test.h"
#include "content/public/test/browser_test.h"
#include "ui/ozone/public/ozone_platform.h"

namespace ui {

// Configures the ozone platform so it would return an error early at the stage
// of initialisation.  In such event, the browser should gracefully exit.
// See https://crbug.com/1280138.
class OzonePlatformTest : public InProcessBrowserTest {
 public:
  OzonePlatformTest() {
    OzonePlatform::SetFailInitializeUIForTest(true);
    set_expected_exit_code(1);
  }

  ~OzonePlatformTest() override {
    OzonePlatform::SetFailInitializeUIForTest(false);
  }
};

IN_PROC_BROWSER_TEST_F(OzonePlatformTest, ExitsGracefullyOnPlatormInitFailure) {
  // This should never be hit.  The browser is expected to exit before entering
  // the test body.
  ASSERT_TRUE(false);
}

}  // namespace ui
