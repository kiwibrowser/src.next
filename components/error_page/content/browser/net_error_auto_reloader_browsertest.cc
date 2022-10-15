// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/error_page/content/browser/net_error_auto_reloader.h"

#include <memory>
#include <utility>

#include "base/memory/raw_ptr.h"
#include "base/test/bind.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/content_browser_test.h"
#include "content/public/test/content_browser_test_utils.h"
#include "content/public/test/fenced_frame_test_util.h"
#include "content/public/test/test_navigation_throttle.h"
#include "content/public/test/test_navigation_throttle_inserter.h"
#include "content/public/test/url_loader_interceptor.h"
#include "content/shell/browser/shell.h"
#include "content/shell/browser/shell_content_browser_client.h"
#include "net/base/net_errors.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/abseil-cpp/absl/types/optional.h"
#include "url/gurl.h"

namespace error_page {
namespace {

// Helper which intercepts all requests for a given URL and terminates them with
// a given net error code. Interception affects most browser requests globally
// (tests here are concerned only with main-frame navigation requests, which are
// covered) and persists from construction time until destruction time.
class NetErrorUrlInterceptor {
 public:
  NetErrorUrlInterceptor(GURL url, net::Error error)
      : url_(std::move(url)),
        interceptor_(base::BindLambdaForTesting(
            [this,
             error](content::URLLoaderInterceptor::RequestParams* params) {
              if (params->url_request.url != url_)
                return false;
              network::URLLoaderCompletionStatus status;
              status.error_code = error;
              params->client->OnComplete(status);
              return true;
            })) {}
  ~NetErrorUrlInterceptor() = default;

 private:
  const GURL url_;
  const content::URLLoaderInterceptor interceptor_;
};

// Helper to intercept all navigations with a failure using custom error page
// contents. As long as an instance of this class exists, navigations will land
// on its custom error page.
class CustomErrorPageThrottleInserter {
 public:
  CustomErrorPageThrottleInserter(content::WebContents* web_contents,
                                  net::Error error,
                                  std::string error_page_contents)
      : throttle_inserter_(
            web_contents,
            base::BindLambdaForTesting(
                [error, error_page_contents](content::NavigationHandle* handle)
                    -> std::unique_ptr<content::NavigationThrottle> {
                  auto throttle =
                      std::make_unique<content::TestNavigationThrottle>(handle);
                  throttle->SetResponse(
                      content::TestNavigationThrottle::WILL_START_REQUEST,
                      content::TestNavigationThrottle::SYNCHRONOUS,
                      content::NavigationThrottle::ThrottleCheckResult(
                          content::NavigationThrottle::CANCEL, error,
                          error_page_contents));
                  return throttle;
                })) {}
  ~CustomErrorPageThrottleInserter() = default;

 private:
  const content::TestNavigationThrottleInserter throttle_inserter_;
};

// Helper to intercept and defer the first navigation initiated after
// construction. Allows a test to wait for both request start and deferral, as
// well as request completion after cancellation.
class DeferNextNavigationThrottleInserter
    : public content::WebContentsObserver {
 public:
  class DeferringThrottle : public content::NavigationThrottle {
   public:
    explicit DeferringThrottle(content::NavigationHandle* handle,
                               base::OnceClosure callback)
        : NavigationThrottle(handle), callback_(std::move(callback)) {}

    ~DeferringThrottle() override = default;

    void Cancel() { CancelDeferredNavigation(CANCEL); }

    // content::NavigationThrottle:
    ThrottleCheckResult WillStartRequest() override {
      std::move(callback_).Run();
      return DEFER;
    }
    const char* GetNameForLogging() override { return "DeferringThrottle"; }

   private:
    base::OnceClosure callback_;
  };

  explicit DeferNextNavigationThrottleInserter(
      content::WebContents* web_contents)
      : content::WebContentsObserver(web_contents),
        throttle_inserter_(
            web_contents,
            base::BindRepeating(
                &DeferNextNavigationThrottleInserter::MaybeCreateThrottle,
                base::Unretained(this))) {}
  ~DeferNextNavigationThrottleInserter() override = default;

  void WaitForNextNavigationToBeDeferred() { defer_wait_loop_.Run(); }

  void CancelAndWaitForNavigationToFinish() {
    throttle_->Cancel();
    finish_wait_loop_.Run();
  }

  // content::WebContentsObserver:
  void DidFinishNavigation(content::NavigationHandle* handle) override {
    DCHECK(throttle_);
    if (handle == throttle_->navigation_handle())
      finish_wait_loop_.Quit();
  }

 private:
  std::unique_ptr<content::NavigationThrottle> MaybeCreateThrottle(
      content::NavigationHandle* handle) {
    if (throttle_)
      return nullptr;

    auto throttle = std::make_unique<DeferringThrottle>(
        handle, defer_wait_loop_.QuitClosure());
    throttle_ = throttle.get();
    return throttle;
  }

  const content::TestNavigationThrottleInserter throttle_inserter_;
  raw_ptr<DeferringThrottle> throttle_ = nullptr;
  base::RunLoop defer_wait_loop_;
  base::RunLoop finish_wait_loop_;
};

base::TimeDelta GetDelayForReloadCount(size_t count) {
  return NetErrorAutoReloader::GetNextReloadDelayForTesting(count);
}

class NetErrorAutoReloaderBrowserTest : public content::ContentBrowserTest {
 public:
  void SetUpOnMainThread() override {
    ASSERT_TRUE(embedded_test_server()->Start());
    error_page::NetErrorAutoReloader::CreateForWebContents(
        shell()->web_contents());

    // Start online by default in all tests.
    GetAutoReloader()->DisableConnectionChangeObservationForTesting();
    GetAutoReloader()->OnConnectionChanged(
        network::mojom::ConnectionType::CONNECTION_WIFI);

    content::ShellContentBrowserClient::Get()
        ->set_create_throttles_for_navigation_callback(base::BindRepeating(
            [](content::NavigationHandle* handle)
                -> std::vector<std::unique_ptr<content::NavigationThrottle>> {
              std::vector<std::unique_ptr<content::NavigationThrottle>>
                  throttles;
              auto throttle =
                  NetErrorAutoReloader::MaybeCreateThrottleFor(handle);
              if (throttle)
                throttles.push_back(std::move(throttle));
              return throttles;
            }));
  }

  NetErrorAutoReloader* GetAutoReloader() {
    return error_page::NetErrorAutoReloader::FromWebContents(
        shell()->web_contents());
  }

  // Returns the time-delay of the currently scheduled auto-reload task, if one
  // is scheduled. If no auto-reload is scheduled, this returns null.
  absl::optional<base::TimeDelta> GetCurrentAutoReloadDelay() {
    const absl::optional<base::OneShotTimer>& timer =
        GetAutoReloader()->next_reload_timer_for_testing();
    if (!timer)
      return absl::nullopt;
    return timer->GetCurrentDelay();
  }

  // Forces the currently scheduled auto-reload task to execute immediately.
  // This allows tests to force normal forward-progress of the delayed reload
  // logic without waiting a long time or painfully messing around with mock
  // time in browser tests.
  void ForceScheduledAutoReloadNow() {
    absl::optional<base::OneShotTimer>& timer =
        GetAutoReloader()->next_reload_timer_for_testing();
    if (timer && timer->IsRunning())
      timer->FireNow();
  }

  GURL GetTestUrl() { return embedded_test_server()->GetURL("/empty.html"); }

  // Helper used by all tests to perform navigations, whether successful or
  // intercepted for simulated failure. Note that this asynchronously initiates
  // the navigation and then waits only for the *navigation* to finish; this is
  // in contrast to common test utilities which wait for loading to finish. It
  // matters because most of NetErrorAutoReloader's interesting behavior is
  // triggered at navigation completion and tests may want to observe the
  // immediate side effects, such as the scheduling of an auto-reload timer.
  //
  // Return true if the navigation was successful, or false if it failed.
  [[nodiscard]] bool NavigateMainFrame(const GURL& url) {
    content::TestNavigationManager navigation(shell()->web_contents(), url);
    shell()->web_contents()->GetController().LoadURL(
        url, content::Referrer(), ui::PAGE_TRANSITION_TYPED,
        /*extra_headers=*/std::string());
    navigation.WaitForNavigationFinished();
    return navigation.was_successful();
  }

  void SimulateNetworkGoingOnline() {
    GetAutoReloader()->OnConnectionChanged(
        network::mojom::ConnectionType::CONNECTION_WIFI);
  }

  void SimulateNetworkGoingOffline() {
    GetAutoReloader()->OnConnectionChanged(
        network::mojom::ConnectionType::CONNECTION_NONE);
  }
};

// A successful navigation results in no auto-reload being scheduled.
IN_PROC_BROWSER_TEST_F(NetErrorAutoReloaderBrowserTest, NoError) {
  EXPECT_TRUE(NavigateMainFrame(GetTestUrl()));
  EXPECT_EQ(absl::nullopt, GetCurrentAutoReloadDelay());
}

// A normal error page triggers a scheduled reload.
IN_PROC_BROWSER_TEST_F(NetErrorAutoReloaderBrowserTest, ErrorSchedulesReload) {
  NetErrorUrlInterceptor interceptor(GetTestUrl(), net::ERR_CONNECTION_RESET);
  EXPECT_FALSE(NavigateMainFrame(GetTestUrl()));
  EXPECT_EQ(GetDelayForReloadCount(0), GetCurrentAutoReloadDelay());
}

// A successful auto-reload operation will behave like any successful navigation
// and not schedule subsequent reloads.
IN_PROC_BROWSER_TEST_F(NetErrorAutoReloaderBrowserTest, ErrorRecovery) {
  auto interceptor = std::make_unique<NetErrorUrlInterceptor>(
      GetTestUrl(), net::ERR_CONNECTION_RESET);
  EXPECT_FALSE(NavigateMainFrame(GetTestUrl()));
  EXPECT_EQ(GetDelayForReloadCount(0), GetCurrentAutoReloadDelay());
  interceptor.reset();

  // Force the scheduled auto-reload once interception is cancelled, and observe
  // a successful navigation.
  content::TestNavigationManager navigation(shell()->web_contents(),
                                            GetTestUrl());
  ForceScheduledAutoReloadNow();
  navigation.WaitForNavigationFinished();
  EXPECT_TRUE(navigation.was_successful());

  // No new auto-reload scheduled.
  EXPECT_EQ(absl::nullopt, GetCurrentAutoReloadDelay());
}

// An auto-reload that fails in the same way as the original navigation will
// result in another reload being scheduled with an increased delay.
IN_PROC_BROWSER_TEST_F(NetErrorAutoReloaderBrowserTest, ReloadDelayBackoff) {
  auto interceptor = std::make_unique<NetErrorUrlInterceptor>(
      GetTestUrl(), net::ERR_CONNECTION_RESET);
  EXPECT_FALSE(NavigateMainFrame(GetTestUrl()));
  EXPECT_EQ(GetDelayForReloadCount(0), GetCurrentAutoReloadDelay());

  // Force the scheduled auto-reload to run while still intercepting the
  // navigation request and forcing it to fail with the same error. Observe that
  // the navigation fails again and that a new auto-reload task has been
  // scheduled with an appropriately increased delay. Note that these auto-
  // reload navigations are also expected not to commit, since we suppress
  // committing a new error page navigation when it corresponds to the same
  // error code as the currently committed error page.
  {
    content::TestNavigationManager navigation(shell()->web_contents(),
                                              GetTestUrl());
    ForceScheduledAutoReloadNow();
    navigation.WaitForNavigationFinished();
    EXPECT_FALSE(navigation.was_committed());
    EXPECT_EQ(GetDelayForReloadCount(1), GetCurrentAutoReloadDelay());
  }

  // Do that one more time, for good measure. Note that we expect the scheduled
  // auto-reload delay to change again here, still with no new commit.
  {
    content::TestNavigationManager navigation(shell()->web_contents(),
                                              GetTestUrl());
    ForceScheduledAutoReloadNow();
    navigation.WaitForNavigationFinished();
    EXPECT_FALSE(navigation.was_committed());
    EXPECT_EQ(GetDelayForReloadCount(2), GetCurrentAutoReloadDelay());
    interceptor.reset();
  }

  // Finally, let the next reload succeed and verify successful navigation with
  // no subsequent auto-reload scheduling.
  {
    content::TestNavigationManager navigation(shell()->web_contents(),
                                              GetTestUrl());
    ForceScheduledAutoReloadNow();
    navigation.WaitForNavigationFinished();
    EXPECT_TRUE(navigation.was_successful());
  }
}

// If an auto-reload results in a different network error, it's treated as a new
// navigation and the auto-reload delay backoff is reset.
IN_PROC_BROWSER_TEST_F(NetErrorAutoReloaderBrowserTest,
                       ResetOnAutoReloadWithNewError) {
  {
    NetErrorUrlInterceptor interceptor(GetTestUrl(), net::ERR_CONNECTION_RESET);
    EXPECT_FALSE(NavigateMainFrame(GetTestUrl()));
    EXPECT_EQ(GetDelayForReloadCount(0), GetCurrentAutoReloadDelay());
  }

  // Force the scheduled auto-reload to run while still intercepting the
  // navigation request and forcing it to fail with a new error. Observe that
  // the navigation fails again but that the new auto-reload task has been
  // scheduled as if it was the first failure, since the error code is
  // different.
  {
    NetErrorUrlInterceptor interceptor(GetTestUrl(), net::ERR_ACCESS_DENIED);
    content::TestNavigationManager navigation(shell()->web_contents(),
                                              GetTestUrl());
    ForceScheduledAutoReloadNow();
    navigation.WaitForNavigationFinished();
    EXPECT_TRUE(navigation.was_committed());
    EXPECT_FALSE(navigation.was_successful());
    EXPECT_EQ(GetDelayForReloadCount(0), GetCurrentAutoReloadDelay());
  }
}

// An explicitly stopped navigation from an error page does not trigger
// auto-reload to restart.
IN_PROC_BROWSER_TEST_F(NetErrorAutoReloaderBrowserTest, StopCancelsAutoReload) {
  NetErrorUrlInterceptor interceptor(GetTestUrl(), net::ERR_CONNECTION_RESET);
  EXPECT_FALSE(NavigateMainFrame(GetTestUrl()));
  EXPECT_EQ(GetDelayForReloadCount(0), GetCurrentAutoReloadDelay());

  // Start the navigation and simulate stoppage via user action (i.e.
  // `WebContents::Stop`). This should cancel the previously scheduled
  // auto-reload timer without scheduling a new one.
  content::TestNavigationManager navigation(shell()->web_contents(),
                                            GetTestUrl());
  shell()->web_contents()->GetController().LoadURL(
      GetTestUrl(), content::Referrer(), ui::PAGE_TRANSITION_TYPED,
      /*extra_headers=*/std::string());
  EXPECT_TRUE(navigation.WaitForRequestStart());
  shell()->web_contents()->Stop();
  EXPECT_EQ(absl::nullopt, GetCurrentAutoReloadDelay());
}

// Various specific types of network-layer errors do not trigger auto-reload.
IN_PROC_BROWSER_TEST_F(NetErrorAutoReloaderBrowserTest,
                       NoAutoReloadOnUnsupportedNetworkErrors) {
  {
    NetErrorUrlInterceptor interceptor(GetTestUrl(),
                                       net::ERR_UNKNOWN_URL_SCHEME);
    EXPECT_FALSE(NavigateMainFrame(GetTestUrl()));
    EXPECT_EQ(absl::nullopt, GetCurrentAutoReloadDelay());
  }
  {
    NetErrorUrlInterceptor interceptor(GetTestUrl(),
                                       net::ERR_BAD_SSL_CLIENT_AUTH_CERT);
    EXPECT_FALSE(NavigateMainFrame(GetTestUrl()));
    EXPECT_EQ(absl::nullopt, GetCurrentAutoReloadDelay());
  }
  {
    NetErrorUrlInterceptor interceptor(GetTestUrl(), net::ERR_CERT_INVALID);
    EXPECT_FALSE(NavigateMainFrame(GetTestUrl()));
    EXPECT_EQ(absl::nullopt, GetCurrentAutoReloadDelay());
  }
  {
    NetErrorUrlInterceptor interceptor(GetTestUrl(),
                                       net::ERR_SSL_PROTOCOL_ERROR);
    EXPECT_FALSE(NavigateMainFrame(GetTestUrl()));
    EXPECT_EQ(absl::nullopt, GetCurrentAutoReloadDelay());
  }
  {
    NetErrorUrlInterceptor interceptor(GetTestUrl(),
                                       net::ERR_BLOCKED_BY_CLIENT);
    EXPECT_FALSE(NavigateMainFrame(GetTestUrl()));
    EXPECT_EQ(absl::nullopt, GetCurrentAutoReloadDelay());
  }
  {
    NetErrorUrlInterceptor interceptor(GetTestUrl(),
                                       net::ERR_BLOCKED_BY_ADMINISTRATOR);
    EXPECT_FALSE(NavigateMainFrame(GetTestUrl()));
    EXPECT_EQ(absl::nullopt, GetCurrentAutoReloadDelay());
  }
  {
    NetErrorUrlInterceptor interceptor(GetTestUrl(),
                                       net::ERR_INVALID_AUTH_CREDENTIALS);
    EXPECT_FALSE(NavigateMainFrame(GetTestUrl()));
    EXPECT_EQ(absl::nullopt, GetCurrentAutoReloadDelay());
  }
}

// Only HTTP and HTTPS navigation error pages activate auto-reload.
IN_PROC_BROWSER_TEST_F(NetErrorAutoReloaderBrowserTest,
                       NoAutoReloadWithoutHttpOrHttps) {
  {
    const GURL kTestDataUrl{"data://whatever"};
    NetErrorUrlInterceptor interceptor(kTestDataUrl, net::ERR_ACCESS_DENIED);
    EXPECT_FALSE(NavigateMainFrame(kTestDataUrl));
    EXPECT_EQ(absl::nullopt, GetCurrentAutoReloadDelay());
  }

  {
    const GURL kTestFileUrl{"file://whatever"};
    NetErrorUrlInterceptor interceptor(kTestFileUrl, net::ERR_ACCESS_DENIED);
    EXPECT_FALSE(NavigateMainFrame(kTestFileUrl));
    EXPECT_EQ(absl::nullopt, GetCurrentAutoReloadDelay());
  }
}

// Starting a new navigation cancels any pending auto-reload.
IN_PROC_BROWSER_TEST_F(NetErrorAutoReloaderBrowserTest,
                       NavigationCancelsAutoReload) {
  // Force an error to initiate auto-reload.
  auto interceptor = std::make_unique<NetErrorUrlInterceptor>(
      GetTestUrl(), net::ERR_CONNECTION_RESET);
  EXPECT_FALSE(NavigateMainFrame(GetTestUrl()));
  EXPECT_EQ(GetDelayForReloadCount(0), GetCurrentAutoReloadDelay());
  interceptor.reset();

  // Start a new navigation before the reload task can run. Reload should be
  // cancelled. Note that we wait only for the request to start and be deferred
  // here before verifying the auto-reload cancellation.
  DeferNextNavigationThrottleInserter deferrer(shell()->web_contents());
  shell()->web_contents()->GetController().LoadURL(
      GetTestUrl(), content::Referrer(), ui::PAGE_TRANSITION_TYPED,
      /*extra_headers=*/std::string());
  deferrer.WaitForNextNavigationToBeDeferred();
  EXPECT_EQ(absl::nullopt, GetCurrentAutoReloadDelay());

  // Now cancel the deferred navigation and observe that auto-reload for the
  // error page is rescheduled.
  deferrer.CancelAndWaitForNavigationToFinish();
  EXPECT_EQ(GetDelayForReloadCount(0), GetCurrentAutoReloadDelay());
}

// An error page while offline does not trigger auto-reload.
IN_PROC_BROWSER_TEST_F(NetErrorAutoReloaderBrowserTest,
                       NoAutoReloadWhileOffline) {
  SimulateNetworkGoingOffline();

  // This would normally schedule an auto-reload, but we're offline.
  NetErrorUrlInterceptor interceptor(GetTestUrl(), net::ERR_CONNECTION_RESET);
  EXPECT_FALSE(NavigateMainFrame(GetTestUrl()));
  EXPECT_EQ(absl::nullopt, GetCurrentAutoReloadDelay());
}

// If the browser comes online while sitting at an error page that supports
// auto-reload, a new auto-reload task should be scheduled.
IN_PROC_BROWSER_TEST_F(NetErrorAutoReloaderBrowserTest,
                       AutoReloadWhenBrowserComesOnline) {
  SimulateNetworkGoingOffline();

  // This would normally schedule an auto-reload, but we're offline.
  NetErrorUrlInterceptor interceptor(GetTestUrl(), net::ERR_CONNECTION_RESET);
  EXPECT_FALSE(NavigateMainFrame(GetTestUrl()));
  EXPECT_EQ(absl::nullopt, GetCurrentAutoReloadDelay());

  SimulateNetworkGoingOnline();
  EXPECT_EQ(GetDelayForReloadCount(0), GetCurrentAutoReloadDelay());
}

// If the browser comes online while sitting at non-error page, auto-reload is
// not scheduled.
IN_PROC_BROWSER_TEST_F(NetErrorAutoReloaderBrowserTest,
                       NoAutoReloadOnNonErrorPageWhenBrowserComesOnline) {
  EXPECT_TRUE(NavigateMainFrame(GetTestUrl()));
  EXPECT_EQ(absl::nullopt, GetCurrentAutoReloadDelay());

  SimulateNetworkGoingOffline();
  EXPECT_EQ(absl::nullopt, GetCurrentAutoReloadDelay());

  SimulateNetworkGoingOnline();
  EXPECT_EQ(absl::nullopt, GetCurrentAutoReloadDelay());
}

// Auto-reload is not scheduled when the WebContents are hidden.
IN_PROC_BROWSER_TEST_F(NetErrorAutoReloaderBrowserTest,
                       NoAutoReloadWhenContentsHidden) {
  NetErrorUrlInterceptor interceptor(GetTestUrl(), net::ERR_CONNECTION_RESET);
  EXPECT_FALSE(NavigateMainFrame(GetTestUrl()));
  EXPECT_EQ(GetDelayForReloadCount(0), GetCurrentAutoReloadDelay());

  // Hiding the contents cancels the scheduled auto-reload.
  shell()->web_contents()->WasHidden();
  EXPECT_EQ(absl::nullopt, GetCurrentAutoReloadDelay());
}

// If the WebContents becomes visible while sitting at an error page that
// supports auto-reload, a new auto-reload task should be scheduled.
IN_PROC_BROWSER_TEST_F(NetErrorAutoReloaderBrowserTest,
                       AutoReloadWhenContentsBecomeVisible) {
  NetErrorUrlInterceptor interceptor(GetTestUrl(), net::ERR_CONNECTION_RESET);
  EXPECT_FALSE(NavigateMainFrame(GetTestUrl()));
  EXPECT_EQ(GetDelayForReloadCount(0), GetCurrentAutoReloadDelay());

  // Hiding the contents cancels the scheduled auto-reload.
  shell()->web_contents()->WasHidden();
  EXPECT_EQ(absl::nullopt, GetCurrentAutoReloadDelay());

  // Becoming visible again reschedules auto-reload.
  shell()->web_contents()->WasShown();
  EXPECT_EQ(GetDelayForReloadCount(0), GetCurrentAutoReloadDelay());
}

// If the WebContents becomes visible while sitting at non-error page,
// auto-reload is not scheduled.
IN_PROC_BROWSER_TEST_F(NetErrorAutoReloaderBrowserTest,
                       NoAutoReloadOnNonErrorPageWhenContentsBecomeVisible) {
  EXPECT_TRUE(NavigateMainFrame(GetTestUrl()));
  EXPECT_EQ(absl::nullopt, GetCurrentAutoReloadDelay());

  shell()->web_contents()->WasHidden();
  EXPECT_EQ(absl::nullopt, GetCurrentAutoReloadDelay());

  shell()->web_contents()->WasShown();
  EXPECT_EQ(absl::nullopt, GetCurrentAutoReloadDelay());
}

class NetErrorAutoReloaderFencedFrameBrowserTest
    : public NetErrorAutoReloaderBrowserTest {
 public:
  ~NetErrorAutoReloaderFencedFrameBrowserTest() override = default;

  content::test::FencedFrameTestHelper& fenced_frame_test_helper() {
    return fenced_frame_helper_;
  }

 private:
  content::test::FencedFrameTestHelper fenced_frame_helper_;
};

IN_PROC_BROWSER_TEST_F(NetErrorAutoReloaderFencedFrameBrowserTest,
                       NoAutoReloadOnFencedFrames) {
  const GURL main_url = embedded_test_server()->GetURL("/title1.html");
  EXPECT_TRUE(NavigateToURL(shell(), main_url));

  const GURL fenced_frame_url = embedded_test_server()->GetURL("/title2.html");
  content::RenderFrameHost* fenced_frame_host =
      fenced_frame_test_helper().CreateFencedFrame(
          shell()->web_contents()->GetPrimaryMainFrame(), fenced_frame_url,
          net::ERR_BLOCKED_BY_RESPONSE);

  // The fenced frame navigation failed since it doesn't have the
  // Supports-Loading-Mode HTTP response header "fenced-frame".
  EXPECT_TRUE(fenced_frame_host->GetLastCommittedOrigin().opaque());
  EXPECT_TRUE(fenced_frame_host->IsErrorDocument());
  EXPECT_EQ(absl::nullopt, GetCurrentAutoReloadDelay());
}

}  // namespace
}  // namespace error_page
