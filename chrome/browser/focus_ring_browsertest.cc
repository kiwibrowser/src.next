// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/files/file_util.h"
#include "base/path_service.h"
#include "build/build_config.h"
#include "build/chromeos_buildflags.h"
#include "cc/test/pixel_comparator.h"
#include "chrome/browser/focus_ring_browsertest_mac.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/interactive_test_utils.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/common/content_switches.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "ui/base/ui_base_features.h"
#include "ui/base/ui_base_switches.h"

// TODO(crbug.com/958242): Move the baselines to skia gold for easier
//   rebaselining when all platforms are supported

// To rebaseline this test on all platforms:
// 1. Run a CQ+1 dry run.
// 2. Click the failing bots for android, windows, mac, and linux.
// 3. Find the failing interactive_ui_tests step.
// 4. Click the "Deterministic failure" link for the failing test case.
// 5. Copy the "Actual pixels" data url and paste into browser.
// 6. Save the image into your chromium checkout in
//    chrome/test/data/focus_rings.

#if BUILDFLAG(IS_MAC)
// Mac has subtle rendering differences between different versions of MacOS, so
// we account for them with these fuzzy pixel comparators. These two comparators
// are used in different tests in order to keep the matching somewhat strict.
const cc::FuzzyPixelComparator mac_strict_comparator(
    /* discard_alpha */ true,
    /* error_pixels_percentage_limit */ 3.f,
    /* small_error_pixels_percentage_limit */ 0.f,
    /* avg_abs_error_limit */ 20.f,
    /* max_abs_error_limit */ 49.f,
    /* small_error_threshold */ 0);
const cc::FuzzyPixelComparator mac_loose_comparator(
    /* discard_alpha */ true,
    /* error_pixels_percentage_limit */ 8.7f,
    /* small_error_pixels_percentage_limit */ 0.f,
    /* avg_abs_error_limit */ 20.f,
    /* max_abs_error_limit */ 43.f,
    /* small_error_threshold */ 0);
#endif

class FocusRingBrowserTest : public InProcessBrowserTest {
 public:
  void SetUp() override {
    EnablePixelOutput(/*force_device_scale_factor=*/1.f);
    InProcessBrowserTest::SetUp();
  }

  void SetUpCommandLine(base::CommandLine* command_line) override {
    // The --disable-lcd-text flag helps text render more similarly on
    // different bots and platform.
    command_line->AppendSwitch(switches::kDisableLCDText);

    // This is required to allow dark mode to be used on some platforms.
    command_line->AppendSwitch(switches::kForceDarkMode);
  }

  void RunTest(const std::string& screenshot_filename,
               const std::string& body_html,
               int screenshot_width,
               int screenshot_height,
               const cc::PixelComparator& comparator) {
    base::ScopedAllowBlockingForTesting allow_blocking;

    base::FilePath dir_test_data;
    ASSERT_TRUE(base::PathService::Get(chrome::DIR_TEST_DATA, &dir_test_data));

    std::string platform_suffix;
#if BUILDFLAG(IS_MAC)
    platform_suffix = "_mac";
#elif BUILDFLAG(IS_WIN)
    platform_suffix = "_win";
#elif BUILDFLAG(IS_LINUX)
    platform_suffix = "_linux";
#elif BUILDFLAG(IS_CHROMEOS_ASH)
    platform_suffix = "_chromeos";
#endif

    base::FilePath golden_filepath =
        dir_test_data.AppendASCII("focus_rings")
            .AppendASCII(screenshot_filename + ".png");

    base::FilePath golden_filepath_platform =
        golden_filepath.InsertBeforeExtensionASCII(platform_suffix);
    if (base::PathExists(golden_filepath_platform)) {
      golden_filepath = golden_filepath_platform;
    }

    content::WebContents* web_contents =
        browser()->tab_strip_model()->GetActiveWebContents();
    ASSERT_TRUE(content::NavigateToURL(
        web_contents, GURL("data:text/html,<!DOCTYPE html>" + body_html)));
    ASSERT_TRUE(ui_test_utils::BringBrowserWindowToFront(browser()));

    EXPECT_TRUE(CompareWebContentsOutputToReference(
        web_contents, golden_filepath,
        gfx::Size(screenshot_width, screenshot_height), comparator));
  }
};

// TODO(crbug.com/1222757): Flaky on Mac.
#if BUILDFLAG(IS_MAC)
#define MAYBE_Checkbox DISABLED_Checkbox
#else
#define MAYBE_Checkbox Checkbox
#endif
IN_PROC_BROWSER_TEST_F(FocusRingBrowserTest, MAYBE_Checkbox) {
#if BUILDFLAG(IS_MAC)
  cc::FuzzyPixelComparator comparator = mac_strict_comparator;
#else
  cc::ExactPixelComparator comparator(/*discard_alpha=*/true);
#endif
  RunTest("focus_ring_browsertest_checkbox",
          "<input type=checkbox autofocus>"
          "<input type=checkbox>",
          /* screenshot_width */ 60,
          /* screenshot_height */ 40, comparator);
}

// TODO(crbug.com/1222757): Flaky on Mac.
#if BUILDFLAG(IS_MAC)
#define MAYBE_Radio DISABLED_Radio
#else
#define MAYBE_Radio Radio
#endif
IN_PROC_BROWSER_TEST_F(FocusRingBrowserTest, MAYBE_Radio) {
#if BUILDFLAG(IS_MAC)
  cc::FuzzyPixelComparator comparator = mac_loose_comparator;
#else
  cc::ExactPixelComparator comparator(/*discard_alpha=*/true);
#endif
  RunTest("focus_ring_browsertest_radio",
          "<input type=radio autofocus>"
          "<input type=radio>",
          /* screenshot_width */ 60,
          /* screenshot_height */ 40, comparator);
}

// TODO(crbug.com/1222757): Flaky on Mac.
#if BUILDFLAG(IS_MAC)
#define MAYBE_Button DISABLED_Button
#else
#define MAYBE_Button Button
#endif
IN_PROC_BROWSER_TEST_F(FocusRingBrowserTest, MAYBE_Button) {
#if BUILDFLAG(IS_MAC)
  cc::FuzzyPixelComparator comparator = mac_strict_comparator;
#else
  cc::ExactPixelComparator comparator(/*discard_alpha=*/true);
#endif
  RunTest("focus_ring_browsertest_button",
          "<button autofocus style=\"width:40px;height:20px;\"></button>"
          "<br>"
          "<br>"
          "<button style=\"width:40px;height:20px;\"></button>",
          /* screenshot_width */ 80,
          /* screenshot_height */ 80, comparator);
}

// TODO(crbug.com/1222757): Flaky on Mac.
#if BUILDFLAG(IS_MAC)
#define MAYBE_Anchor DISABLED_Anchor
#else
#define MAYBE_Anchor Anchor
#endif
IN_PROC_BROWSER_TEST_F(FocusRingBrowserTest, MAYBE_Anchor) {
#if BUILDFLAG(IS_MAC)
  cc::FuzzyPixelComparator comparator = mac_strict_comparator;
#else
  cc::ExactPixelComparator comparator(/*discard_alpha=*/true);
#endif
  RunTest("focus_ring_browsertest_anchor",
          "<div style='text-align: center; width: 80px;'>"
          "  <a href='foo' autofocus>---- ---<br>---</a>"
          "</div>"
          "<br>"
          "<div style='text-align: center; width: 80px;'>"
          "  <a href='foo'>---- ---<br>---</a>"
          "</div>",
          /* screenshot_width */ 90,
          /* screenshot_height */ 130, comparator);
}

// TODO(crbug.com/1222757): Flaky on Mac.
#if BUILDFLAG(IS_MAC)
#define MAYBE_DarkModeButton DISABLED_DarkModeButton
#else
#define MAYBE_DarkModeButton DarkModeButton
#endif
IN_PROC_BROWSER_TEST_F(FocusRingBrowserTest, MAYBE_DarkModeButton) {
#if BUILDFLAG(IS_MAC)
  if (!MacOSVersionSupportsDarkMode())
    return;
  cc::FuzzyPixelComparator comparator = mac_strict_comparator;
#else
  cc::ExactPixelComparator comparator(/*discard_alpha=*/true);
#endif
  RunTest("focus_ring_browsertest_dark_mode_button",
          "<meta name=\"color-scheme\" content=\"dark\">"
          "<button autofocus style=\"width:40px;height:20px;\"></button>"
          "<br>"
          "<br>"
          "<button style=\"width:40px;height:20px;\"></button>",
          /* screenshot_width */ 80,
          /* screenshot_height */ 80, comparator);
}
