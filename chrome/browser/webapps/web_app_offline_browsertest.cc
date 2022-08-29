// Copyright 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "build/build_config.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/web_applications/test/web_app_browsertest_util.h"
#include "chrome/browser/web_applications/test/service_worker_registration_waiter.h"
#include "chrome/browser/web_applications/test/web_app_icon_waiter.h"
#include "chrome/browser/web_applications/test/web_app_test_utils.h"
#include "chrome/common/chrome_features.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "components/metrics/content/subprocess_metrics_provider.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_base.h"
#include "content/public/test/test_navigation_observer.h"
#include "content/public/test/url_loader_interceptor.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/blink/public/common/features.h"
#include "ui/base/ui_base_switches.h"
#include "ui/native_theme/native_theme.h"

#if BUILDFLAG(IS_WIN)
#include "base/win/windows_version.h"
#endif

#if BUILDFLAG(IS_CHROMEOS)
#include "chromeos/constants/chromeos_features.h"
#endif

#if BUILDFLAG(IS_CHROMEOS_ASH)
#include "ash/constants/ash_features.h"
#endif

namespace web_app {

enum class PageFlagParam {
  kWithDefaultPageFlag = 0,
  kWithoutDefaultPageFlag = 1,
  kMaxValue = kWithoutDefaultPageFlag
};

class WebAppOfflineTest : public InProcessBrowserTest {
 public:
  // Start a web app without a service worker and disconnect.
  void StartWebAppAndDisconnect(content::WebContents* web_contents,
                                base::StringPiece relative_url) {
    GURL target_url(embedded_test_server()->GetURL(relative_url));
    web_app::NavigateToURLAndWait(browser(), target_url);
    web_app::AppId app_id = web_app::test::InstallPwaForCurrentUrl(browser());
    WebAppIconWaiter(browser()->profile(), app_id).Wait();
    std::unique_ptr<content::URLLoaderInterceptor> interceptor =
        content::URLLoaderInterceptor::SetupRequestFailForURL(
            target_url, net::ERR_INTERNET_DISCONNECTED);

    content::TestNavigationObserver observer(web_contents, 1);
    web_contents->GetController().Reload(content::ReloadType::NORMAL, false);
    observer.Wait();
  }

  // Start a PWA with a service worker and disconnect.
  void StartPwaAndDisconnect(content::WebContents* web_contents,
                             base::StringPiece relative_url) {
    GURL target_url(embedded_test_server()->GetURL(relative_url));
    web_app::ServiceWorkerRegistrationWaiter registration_waiter(
        browser()->profile(), target_url);
    web_app::NavigateToURLAndWait(browser(), target_url);
    registration_waiter.AwaitRegistration();
    web_app::AppId app_id = web_app::test::InstallPwaForCurrentUrl(browser());
    WebAppIconWaiter(browser()->profile(), app_id).Wait();
    std::unique_ptr<content::URLLoaderInterceptor> interceptor =
        content::URLLoaderInterceptor::SetupRequestFailForURL(
            target_url, net::ERR_INTERNET_DISCONNECTED);

    content::TestNavigationObserver observer(web_contents, 1);
    web_contents->GetController().Reload(content::ReloadType::NORMAL, false);
    observer.Wait();
  }
};

class WebAppOfflinePageTest
    : public WebAppOfflineTest,
      public ::testing::WithParamInterface<PageFlagParam> {
 public:
  WebAppOfflinePageTest() {
    if (GetParam() == PageFlagParam::kWithDefaultPageFlag) {
      feature_list_.InitAndEnableFeature(
          features::kDesktopPWAsDefaultOfflinePage);
    } else {
      feature_list_.InitAndDisableFeature(
          features::kDesktopPWAsDefaultOfflinePage);
    }
  }

  void ExpectUniqueSample(net::Error error, int samples) {
    // Expect that the histogram has been updated.
    content::FetchHistogramsFromChildProcesses();
    metrics::SubprocessMetricsProvider::MergeHistogramDeltasForTesting();
    histogram_tester_.ExpectUniqueSample(
        "Net.ErrorPageCounts.WebAppAlternativeErrorPage", -error, samples);
  }

 private:
  base::test::ScopedFeatureList feature_list_;
  base::HistogramTester histogram_tester_;
};

// When a web app with a manifest and no service worker is offline it should
// display the default offline page rather than the dino.
// When the exact same conditions are applied with the feature flag disabled
// expect that the default offline page is not shown.
IN_PROC_BROWSER_TEST_P(WebAppOfflinePageTest, WebAppOfflinePageIsDisplayed) {
  ASSERT_TRUE(embedded_test_server()->Start());
  content::WebContents* web_contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  ExpectUniqueSample(net::ERR_INTERNET_DISCONNECTED, 0);
  StartWebAppAndDisconnect(web_contents, "/banners/no-sw-with-colors.html");

  if (GetParam() == PageFlagParam::kWithDefaultPageFlag) {
    ExpectUniqueSample(net::ERR_INTERNET_DISCONNECTED, 1);
    // Expect that the default offline page is showing.
    EXPECT_TRUE(
        EvalJs(web_contents,
               "document.getElementById('default-web-app-msg') !== null")
            .ExtractBool());
  } else {
    ExpectUniqueSample(net::ERR_INTERNET_DISCONNECTED, 0);
    // Expect that the default offline page is not showing.
    EXPECT_TRUE(
        EvalJs(web_contents,
               "document.getElementById('default-web-app-msg') === null")
            .ExtractBool());
  }
}

// When a web app with a manifest and service worker that doesn't handle being
// offline it should display the default offline page rather than the dino.
IN_PROC_BROWSER_TEST_P(WebAppOfflinePageTest,
                       WebAppOfflineWithEmptyServiceWorker) {
  ASSERT_TRUE(embedded_test_server()->Start());
  content::WebContents* web_contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  ExpectUniqueSample(net::ERR_INTERNET_DISCONNECTED, 0);
  StartPwaAndDisconnect(web_contents, "/banners/background-color.html");

  if (GetParam() == PageFlagParam::kWithDefaultPageFlag) {
    ExpectUniqueSample(net::ERR_INTERNET_DISCONNECTED, 1);
    // Expect that the default offline page is showing.
    EXPECT_TRUE(
        EvalJs(web_contents,
               "document.getElementById('default-web-app-msg') !== null")
            .ExtractBool());
  } else {
    ExpectUniqueSample(net::ERR_INTERNET_DISCONNECTED, 0);
    // Expect that the default offline page is not showing.
    EXPECT_TRUE(
        EvalJs(web_contents,
               "document.getElementById('default-web-app-msg') === null")
            .ExtractBool());
  }
}

// When a web app with a manifest and service worker that handles being offline
// it should not display the default offline page.
IN_PROC_BROWSER_TEST_P(WebAppOfflinePageTest, WebAppOfflineWithServiceWorker) {
  ASSERT_TRUE(embedded_test_server()->Start());
  content::WebContents* web_contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  ExpectUniqueSample(net::ERR_INTERNET_DISCONNECTED, 0);
  StartPwaAndDisconnect(web_contents, "/banners/theme-color.html");

  ExpectUniqueSample(net::ERR_INTERNET_DISCONNECTED, 0);
  // Expect that the default offline page is not showing.
  EXPECT_TRUE(EvalJs(web_contents,
                     "document.getElementById('default-web-app-msg') === null")
                  .ExtractBool());
}

// Default offline page icon test.
IN_PROC_BROWSER_TEST_P(WebAppOfflinePageTest, WebAppOfflinePageIconShowing) {
  ASSERT_TRUE(embedded_test_server()->Start());
  content::WebContents* web_contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  StartWebAppAndDisconnect(web_contents, "/favicon/title2_with_favicon.html");

  if (GetParam() == PageFlagParam::kWithDefaultPageFlag) {
    // Expect that the icon on default offline page is showing.
    EXPECT_TRUE(
        EvalJs(web_contents,
               "document.getElementById('default-web-app-msg') !== null")
            .ExtractBool());
    EXPECT_EQ(EvalJs(web_contents, "document.getElementById('icon').src")
                  .ExtractString(),
              "data:image/"
              "png;base64,"
              "iVBORw0KGgoAAAANSUhEUgAAABAAAAAQCAIAAACQkWg2AAAAYElEQVQ4jd2SKxLA"
              "MAhEXzKdweFyp97/"
              "DHVxOFQrGjr9iCY2qAX2LYbEujNSecg9GeD9gEPtYCpsJ2Ag4CCxs49wKM2Qm1Xj"
              "DqEraEyuLMjIo/+tpeXdGYMSQt9AmuCXDpHoFE1lEw9DAAAAAElFTkSuQmCC");
  } else {
    // Expect that the default offline page is not showing.
    EXPECT_TRUE(
        EvalJs(web_contents,
               "document.getElementById('default-web-app-msg') === null")
            .ExtractBool());
  }
}

INSTANTIATE_TEST_SUITE_P(
    All,
    WebAppOfflinePageTest,
    ::testing::Values(PageFlagParam::kWithDefaultPageFlag,
                      PageFlagParam::kWithoutDefaultPageFlag));

class WebAppOfflineDarkModeTest
    : public WebAppOfflineTest,
      public testing::WithParamInterface<blink::mojom::PreferredColorScheme> {
 public:
  WebAppOfflineDarkModeTest() {
    std::vector<base::Feature> disabled_features;
#if BUILDFLAG(IS_CHROMEOS)
    disabled_features.push_back(chromeos::features::kDarkLightMode);
#endif

#if BUILDFLAG(IS_CHROMEOS_ASH)
    disabled_features.push_back(ash::features::kNotificationsRefresh);
#endif

    feature_list_.InitWithFeatures({features::kDesktopPWAsDefaultOfflinePage,
                                    blink::features::kWebAppEnableDarkMode},
                                   {disabled_features});
  }

  void SetUp() override {
#if BUILDFLAG(IS_WIN)
    if (base::win::GetVersion() < base::win::Version::WIN10) {
      GTEST_SKIP();
    } else {
      InProcessBrowserTest::SetUp();
    }
#elif BUILDFLAG(IS_MAC)
    // TODO(crbug.com/1298658): Get this test suite working.
    GTEST_SKIP();
#else
    InProcessBrowserTest::SetUp();
#endif  // BUILDFLAG(IS_MAC)
  }

 protected:
  void SetUpCommandLine(base::CommandLine* command_line) override {
    // ShellContentBrowserClient::OverrideWebkitPrefs() overrides the
    // prefers-color-scheme according to switches::kForceDarkMode
    // command line.
    if (GetParam() == blink::mojom::PreferredColorScheme::kDark)
      command_line->AppendSwitch(switches::kForceDarkMode);
  }

 private:
  base::test::ScopedFeatureList feature_list_;
};

// Testing offline page in dark mode for a web app with a manifest and no
// service worker.
IN_PROC_BROWSER_TEST_P(WebAppOfflineDarkModeTest,
                       WebAppOfflineDarkModeNoServiceWorker) {
  ASSERT_TRUE(embedded_test_server()->Start());

  // ui::NativeTheme::GetInstanceForNativeUi()->set_use_dark_colors(true);
  content::WebContents* web_contents =
      browser()->tab_strip_model()->GetActiveWebContents();

  StartWebAppAndDisconnect(
      web_contents, "/web_apps/get_manifest.html?color_scheme_dark.json");

  // Expect that the default offline page is showing with dark mode colors.
  if (GetParam() == blink::mojom::PreferredColorScheme::kDark) {
    EXPECT_TRUE(
        EvalJs(web_contents,
               "window.matchMedia('(prefers-color-scheme: dark)').matches")
            .ExtractBool());
    EXPECT_EQ(
        EvalJs(web_contents,
               "window.getComputedStyle(document.querySelector('h2')).color")
            .ExtractString(),
        "rgb(255, 0, 0)");
    EXPECT_EQ(EvalJs(web_contents,
                     "window.getComputedStyle(document.querySelector('body'))."
                     "backgroundColor")
                  .ExtractString(),
              "rgb(255, 0, 0)");
  } else {
    EXPECT_TRUE(
        EvalJs(web_contents,
               "window.matchMedia('(prefers-color-scheme: light)').matches")
            .ExtractBool());
    EXPECT_EQ(
        EvalJs(web_contents,
               "window.getComputedStyle(document.querySelector('h2')).color")
            .ExtractString(),
        "rgb(0, 0, 255)");
    EXPECT_EQ(EvalJs(web_contents,
                     "window.getComputedStyle(document.querySelector('body'))."
                     "backgroundColor")
                  .ExtractString(),
              "rgb(0, 0, 255)");
  }
}

// Testing offline page in dark mode for a web app with a manifest and service
// worker that does not handle offline error.
// TODO(1295430): Flaky on both Linux and Windows CI bots
IN_PROC_BROWSER_TEST_P(WebAppOfflineDarkModeTest,
                       WebAppOfflineDarkModeEmptyServiceWorker) {
  ASSERT_TRUE(embedded_test_server()->Start());

  content::WebContents* web_contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  StartPwaAndDisconnect(
      web_contents,
      "/banners/manifest_test_page_empty_fetch_handler.html?manifest=../"
      "web_apps/color_scheme_dark.json");
  if (GetParam() == blink::mojom::PreferredColorScheme::kDark) {
    // Expect that the default offline page is showing with dark mode colors.
    EXPECT_TRUE(
        EvalJs(web_contents,
               "window.matchMedia('(prefers-color-scheme: dark)').matches")
            .ExtractBool());
    EXPECT_EQ(
        EvalJs(web_contents,
               "window.getComputedStyle(document.querySelector('h2')).color")
            .ExtractString(),
        "rgb(255, 0, 0)");
    EXPECT_EQ(EvalJs(web_contents,
                     "window.getComputedStyle(document.querySelector('body'))."
                     "backgroundColor")
                  .ExtractString(),
              "rgb(255, 0, 0)");
  } else {
    // Expect that the default offline page is showing with light mode colors.
    EXPECT_TRUE(
        EvalJs(web_contents,
               "window.matchMedia('(prefers-color-scheme: light)').matches")
            .ExtractBool());
    EXPECT_EQ(
        EvalJs(web_contents,
               "window.getComputedStyle(document.querySelector('h2')).color")
            .ExtractString(),
        "rgb(0, 0, 255)");
    EXPECT_EQ(EvalJs(web_contents,
                     "window.getComputedStyle(document.querySelector('body'))."
                     "backgroundColor")
                  .ExtractString(),
              "rgb(0, 0, 255)");
  }
}

// Testing offline page in dark mode for a web app with a manifest that has not
// provided dark mode colors.
IN_PROC_BROWSER_TEST_P(WebAppOfflineDarkModeTest,
                       WebAppOfflineNoDarkModeColorsProvided) {
  ASSERT_TRUE(embedded_test_server()->Start());

  content::WebContents* web_contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  StartWebAppAndDisconnect(web_contents, "/banners/no-sw-with-colors.html");

  if (GetParam() == blink::mojom::PreferredColorScheme::kDark) {
    // Expect that the default offline page is showing with dark mode colors.
    EXPECT_TRUE(
        EvalJs(web_contents,
               "window.matchMedia('(prefers-color-scheme: dark)').matches")
            .ExtractBool());
  } else {
    // Expect that the default offline page is showing with light mode colors.
    EXPECT_TRUE(
        EvalJs(web_contents,
               "window.matchMedia('(prefers-color-scheme: light)').matches")
            .ExtractBool());
  }
  EXPECT_EQ(
      EvalJs(web_contents,
             "window.getComputedStyle(document.querySelector('h2')).color")
          .ExtractString(),
      "rgb(0, 255, 0)");
  EXPECT_EQ(EvalJs(web_contents,
                   "window.getComputedStyle(document.querySelector('body'))."
                   "backgroundColor")
                .ExtractString(),
            "rgb(255, 255, 0)");
}

INSTANTIATE_TEST_SUITE_P(
    /* no prefix */,
    WebAppOfflineDarkModeTest,
    ::testing::Values(blink::mojom::PreferredColorScheme::kDark,
                      blink::mojom::PreferredColorScheme::kLight));
}  // namespace web_app
