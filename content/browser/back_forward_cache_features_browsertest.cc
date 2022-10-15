// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/back_forward_cache_browsertest.h"

#include "base/containers/contains.h"
#include "base/threading/platform_thread.h"
#include "base/time/time.h"
#include "build/build_config.h"
#include "build/chromeos_buildflags.h"
#include "content/browser/generic_sensor/sensor_provider_proxy_impl.h"
#include "content/browser/presentation/presentation_test_utils.h"
#include "content/browser/renderer_host/back_forward_cache_disable.h"
#include "content/browser/web_contents/web_contents_impl.h"
#include "content/browser/worker_host/dedicated_worker_hosts_for_document.h"
#include "content/public/browser/media_session.h"
#include "content/public/test/back_forward_cache_util.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/content_browser_test.h"
#include "content/public/test/content_browser_test_utils.h"
#include "content/public/test/media_start_stop_observer.h"
#include "content/public/test/test_utils.h"
#include "content/public/test/web_transport_simple_test_server.h"
#include "content/shell/browser/shell.h"
#include "device/bluetooth/bluetooth_adapter_factory.h"
#include "device/bluetooth/test/mock_bluetooth_adapter.h"
#include "net/test/embedded_test_server/controllable_http_response.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/spawned_test_server/spawned_test_server.h"
#include "net/test/test_data_directory.h"
#include "services/device/public/cpp/test/fake_sensor_and_provider.h"
#include "services/device/public/cpp/test/scoped_geolocation_overrider.h"
#include "services/device/public/mojom/vibration_manager.mojom.h"
#include "services/service_manager/public/cpp/interface_provider.h"
#include "third_party/blink/public/common/features.h"
#include "third_party/blink/public/mojom/app_banner/app_banner.mojom.h"
#include "ui/base/idle/idle_time_provider.h"
#include "ui/base/test/idle_test_utils.h"

// This file contains back-/forward-cache tests for web-platform features and
// APIs. It was forked from
// https://source.chromium.org/chromium/chromium/src/+/main:content/browser/back_forward_cache_browsertest.cc;drc=1288c1bd6a81785cd85b965d61820a7cd87a0e9c
//
// When adding tests for new features please also add WPTs. See
// third_party/blink/web_tests/external/wpt/html/browsers/browsing-the-web/back-forward-cache/README.md

using testing::_;
using testing::Each;
using testing::ElementsAre;
using testing::Not;
using testing::UnorderedElementsAreArray;

namespace content {

using NotRestoredReason = BackForwardCacheMetrics::NotRestoredReason;

class BackForwardCacheDedicatedWorkerFlagBrowserTest
    : public BackForwardCacheBrowserTest,
      public testing::WithParamInterface<bool> {
 public:
  void SetUpCommandLine(base::CommandLine* command_line) override {
    EnableFeatureAndSetParams(blink::features::kBackForwardCacheDedicatedWorker,
                              "", "");
    if (IsDedicatedWorkerEnabled()) {
      EnableFeatureAndSetParams(
          blink::features::kBackForwardCacheDedicatedWorker, "", "");
    } else {
      DisableFeature(blink::features::kBackForwardCacheDedicatedWorker);
    }
    BackForwardCacheBrowserTest::SetUpCommandLine(command_line);
  }

  bool IsDedicatedWorkerEnabled() { return GetParam(); }
};

INSTANTIATE_TEST_SUITE_P(All,
                         BackForwardCacheDedicatedWorkerFlagBrowserTest,
                         testing::Bool());

IN_PROC_BROWSER_TEST_P(BackForwardCacheDedicatedWorkerFlagBrowserTest,
                       PageWithDedicatedWorkerCachedOrNot) {
  ASSERT_TRUE(embedded_test_server()->Start());

  ASSERT_TRUE(NavigateToURL(
      shell(),
      embedded_test_server()->GetURL(
          "a.com", "/back_forward_cache/page_with_dedicated_worker.html")));
  ASSERT_EQ(42, EvalJs(current_frame_host(), "window.receivedMessagePromise"));
  RenderFrameHostWrapper rfh(current_frame_host());

  // Navigate away.
  ASSERT_TRUE(NavigateToURL(
      shell(), embedded_test_server()->GetURL("b.com", "/title1.html")));

  // Go back
  ASSERT_TRUE(HistoryGoBack(web_contents()));

  // Check the outcome.
  if (IsDedicatedWorkerEnabled()) {
    EXPECT_EQ(rfh.get(), current_frame_host());
    ExpectRestored(FROM_HERE);
  } else {
    // The page with the dedicated worker should be deleted (not cached).
    ASSERT_TRUE(rfh.WaitUntilRenderFrameDeleted());
    ExpectNotRestored({NotRestoredReason::kBlocklistedFeatures},
                      {blink::scheduler::WebSchedulerTrackedFeature::
                           kDedicatedWorkerOrWorklet},
                      {}, {}, {}, FROM_HERE);
  }
}

// The bool parameter is used for switching PlzDedicatedWorker.
class BackForwardCacheWithDedicatedWorkerBrowserTest
    : public BackForwardCacheBrowserTest,
      public testing::WithParamInterface<bool> {
 public:
  BackForwardCacheWithDedicatedWorkerBrowserTest() { server_.Start(); }

  void SetUpCommandLine(base::CommandLine* command_line) override {
    EnableFeatureAndSetParams(blink::features::kBackForwardCacheDedicatedWorker,
                              "", "");
    if (IsPlzDedicatedWorkerEnabled())
      EnableFeatureAndSetParams(blink::features::kPlzDedicatedWorker, "", "");
    BackForwardCacheBrowserTest::SetUpCommandLine(command_line);

    server_.SetUpCommandLine(command_line);
  }

  bool IsPlzDedicatedWorkerEnabled() { return GetParam(); }

  int port() const { return server_.server_address().port(); }

  int CountWorkerClients(RenderFrameHostImpl* rfh) {
    return EvalJs(rfh, JsReplace(R"(
      new Promise(async (resolve) => {
        const resp = await fetch('/service_worker/count_worker_clients');
        resolve(parseInt(await resp.text(), 10));
      });
    )"))
        .ExtractInt();
  }

 private:
  WebTransportSimpleTestServer server_;
};

INSTANTIATE_TEST_SUITE_P(All,
                         BackForwardCacheWithDedicatedWorkerBrowserTest,
                         testing::Bool());

// Confirms that a page using a dedicated worker is cached.
IN_PROC_BROWSER_TEST_P(BackForwardCacheWithDedicatedWorkerBrowserTest,
                       CacheWithDedicatedWorker) {
  CreateHttpsServer();
  ASSERT_TRUE(https_server()->Start());

  EXPECT_TRUE(NavigateToURL(
      shell(),
      https_server()->GetURL(
          "a.test", "/back_forward_cache/page_with_dedicated_worker.html")));
  EXPECT_EQ(42, EvalJs(current_frame_host(), "window.receivedMessagePromise"));

  // Navigate away.
  EXPECT_TRUE(
      NavigateToURL(shell(), https_server()->GetURL("b.test", "/title1.html")));

  // Go back to the original page.
  ASSERT_TRUE(HistoryGoBack(web_contents()));
  ExpectRestored(FROM_HERE);
}

// Confirms that an active page using a dedicated worker that calls
// importScripts won't trigger an eviction IPC, causing the page to reload.
// Regression test for https://crbug.com/1305041.
IN_PROC_BROWSER_TEST_P(
    BackForwardCacheWithDedicatedWorkerBrowserTest,
    PageWithDedicatedWorkerAndImportScriptsWontTriggerReload) {
  CreateHttpsServer();
  ASSERT_TRUE(https_server()->Start());

  EXPECT_TRUE(NavigateToURL(
      shell(), https_server()->GetURL(
                   "a.test",
                   "/back_forward_cache/"
                   "page_with_dedicated_worker_and_importscripts.html")));
  // Wait until the importScripts() call finished running.
  EXPECT_EQ(42, EvalJs(current_frame_host(), "window.receivedMessagePromise"));

  // If the importScripts() call triggered an eviction, a reload will be
  // triggered due to the "evict after docment is restored" will be hit, as the
  // page is not in back/forward cache.
  EXPECT_FALSE(
      web_contents()->GetPrimaryFrameTree().root()->navigation_request());
}

// Confirms that a page using a dedicated worker with WebTransport is not
// cached.
// TODO(crbug.com/1299018): Flakes on Linux.
#if BUILDFLAG(IS_LINUX)
#define MAYBE_DoNotCacheWithDedicatedWorkerWithWebTransport \
  DISABLED_DoNotCacheWithDedicatedWorkerWithWebTransport
#else
#define MAYBE_DoNotCacheWithDedicatedWorkerWithWebTransport \
  DoNotCacheWithDedicatedWorkerWithWebTransport
#endif
IN_PROC_BROWSER_TEST_P(BackForwardCacheWithDedicatedWorkerBrowserTest,
                       MAYBE_DoNotCacheWithDedicatedWorkerWithWebTransport) {
  CreateHttpsServer();
  ASSERT_TRUE(https_server()->Start());

  EXPECT_TRUE(NavigateToURL(
      shell(), https_server()->GetURL(
                   "a.test",
                   "/back_forward_cache/"
                   "page_with_dedicated_worker_and_webtransport.html")));
  // Open a WebTransport.
  EXPECT_EQ("opened",
            EvalJs(current_frame_host(),
                   JsReplace("window.testOpenWebTransport($1);", port())));
  RenderFrameDeletedObserver delete_observer_rfh(current_frame_host());

  // Navigate away.
  EXPECT_TRUE(
      NavigateToURL(shell(), https_server()->GetURL("b.test", "/title1.html")));
  delete_observer_rfh.WaitUntilDeleted();

  // Go back to the original page. The page was not cached as the worker used
  // WebTransport.
  ASSERT_TRUE(HistoryGoBack(web_contents()));
  ExpectNotRestored(
      {NotRestoredReason::kBlocklistedFeatures},
      {blink::scheduler::WebSchedulerTrackedFeature::kWebTransport}, {}, {}, {},
      FROM_HERE);
}

// Confirms that a page using a dedicated worker with a closed WebTransport is
// cached as WebTransport is not a sticky feature.
IN_PROC_BROWSER_TEST_P(BackForwardCacheWithDedicatedWorkerBrowserTest,
                       CacheWithDedicatedWorkerWithWebTransportClosed) {
  CreateHttpsServer();
  ASSERT_TRUE(https_server()->Start());

  EXPECT_TRUE(NavigateToURL(
      shell(), https_server()->GetURL(
                   "a.test",
                   "/back_forward_cache/"
                   "page_with_dedicated_worker_and_webtransport.html")));
  // Open and close a WebTransport.
  EXPECT_EQ("opened",
            EvalJs(current_frame_host(),
                   JsReplace("window.testOpenWebTransport($1);", port())));
  EXPECT_EQ("closed",
            EvalJs(current_frame_host(), "window.testCloseWebTransport();"));

  // Navigate away.
  EXPECT_TRUE(
      NavigateToURL(shell(), https_server()->GetURL("b.test", "/title1.html")));

  // Go back to the original page. The page was cached. Even though WebTransport
  // is used once, the page is eligible for back-forward cache as the feature is
  // not sticky.
  ASSERT_TRUE(HistoryGoBack(web_contents()));
  ExpectRestored(FROM_HERE);
}

// TODO(https://crbug.com/1299018): Flaky on Linux.
#if BUILDFLAG(IS_LINUX)
#define MAYBE_DoNotCacheWithDedicatedWorkerWithWebTransportAndDocumentWithBroadcastChannel \
  DISABLED_DoNotCacheWithDedicatedWorkerWithWebTransportAndDocumentWithBroadcastChannel
#else
#define MAYBE_DoNotCacheWithDedicatedWorkerWithWebTransportAndDocumentWithBroadcastChannel \
  DoNotCacheWithDedicatedWorkerWithWebTransportAndDocumentWithBroadcastChannel
#endif
IN_PROC_BROWSER_TEST_P(
    BackForwardCacheWithDedicatedWorkerBrowserTest,
    MAYBE_DoNotCacheWithDedicatedWorkerWithWebTransportAndDocumentWithBroadcastChannel) {
  CreateHttpsServer();
  ASSERT_TRUE(https_server()->Start());

  EXPECT_TRUE(NavigateToURL(
      shell(), https_server()->GetURL(
                   "a.test",
                   "/back_forward_cache/"
                   "page_with_dedicated_worker_and_webtransport.html")));

  // Open a WebTransport in the dedicated worker.
  EXPECT_EQ("opened",
            EvalJs(current_frame_host(),
                   JsReplace("window.testOpenWebTransport($1);", port())));
  // testOpenWebTransport sends the IPC (BackForwardCacheController.
  // DidChangeBackForwardCacheDisablingFeatures) from a renderer. Run a script
  // to wait for the IPC reaching to the browser.
  EXPECT_EQ(42, EvalJs(current_frame_host(), "42;"));
  EXPECT_TRUE(
      DedicatedWorkerHostsForDocument::GetOrCreateForCurrentDocument(
          current_frame_host())
          ->GetBackForwardCacheDisablingFeatures()
          .HasAll(
              {blink::scheduler::WebSchedulerTrackedFeature::kWebTransport}));

  // Use a broadcast channel in the frame.
  EXPECT_TRUE(ExecJs(current_frame_host(),
                     "window.foo = new BroadcastChannel('foo');"));
  RenderFrameDeletedObserver delete_observer_rfh(current_frame_host());

  // Navigate away.
  EXPECT_TRUE(
      NavigateToURL(shell(), https_server()->GetURL("b.test", "/title1.html")));
  delete_observer_rfh.WaitUntilDeleted();

  // Go back to the original page. The page was not cached due to WebTransport
  // and a broadcast channel, which came from the dedicated worker and the frame
  // respectively. Confirm both are recorded.
  ASSERT_TRUE(HistoryGoBack(web_contents()));
  ExpectNotRestored(
      {NotRestoredReason::kBlocklistedFeatures},
      {blink::scheduler::WebSchedulerTrackedFeature::kWebTransport,
       blink::scheduler::WebSchedulerTrackedFeature::kBroadcastChannel},
      {}, {}, {}, FROM_HERE);
}

IN_PROC_BROWSER_TEST_P(
    BackForwardCacheWithDedicatedWorkerBrowserTest,
    DoNotCacheWithDedicatedWorkerWithClosedWebTransportAndDocumentWithBroadcastChannel) {
  CreateHttpsServer();
  ASSERT_TRUE(https_server()->Start());

  EXPECT_TRUE(NavigateToURL(
      shell(), https_server()->GetURL(
                   "a.test",
                   "/back_forward_cache/"
                   "page_with_dedicated_worker_and_webtransport.html")));

  // Open and close a WebTransport in the dedicated worker.
  EXPECT_EQ("opened",
            EvalJs(current_frame_host(),
                   JsReplace("window.testOpenWebTransport($1);", port())));
  // testOpenWebTransport sends the IPC (BackForwardCacheController.
  // DidChangeBackForwardCacheDisablingFeatures) from a renderer. Run a script
  // to wait for the IPC reaching to the browser.
  EXPECT_EQ(42, EvalJs(current_frame_host(), "42;"));
  EXPECT_TRUE(
      DedicatedWorkerHostsForDocument::GetOrCreateForCurrentDocument(
          current_frame_host())
          ->GetBackForwardCacheDisablingFeatures()
          .HasAll(
              {blink::scheduler::WebSchedulerTrackedFeature::kWebTransport}));

  EXPECT_EQ("closed",
            EvalJs(current_frame_host(),
                   JsReplace("window.testCloseWebTransport($1);", port())));
  // testOpenWebTransport sends the IPC (BackForwardCacheController.
  // DidChangeBackForwardCacheDisablingFeatures) from a renderer. Run a script
  // to wait for the IPC reaching to the browser.
  EXPECT_EQ(42, EvalJs(current_frame_host(), "42;"));
  EXPECT_TRUE(DedicatedWorkerHostsForDocument::GetOrCreateForCurrentDocument(
                  current_frame_host())
                  ->GetBackForwardCacheDisablingFeatures()
                  .Empty());

  // Use a broadcast channel in the frame.
  EXPECT_TRUE(ExecJs(current_frame_host(),
                     "window.foo = new BroadcastChannel('foo');"));
  RenderFrameDeletedObserver delete_observer_rfh(current_frame_host());

  // Navigate away.
  EXPECT_TRUE(
      NavigateToURL(shell(), https_server()->GetURL("b.test", "/title1.html")));
  delete_observer_rfh.WaitUntilDeleted();

  // Go back to the original page. The page was not cached due to a broadcast
  // channel, which came from the frame. WebTransport was used once in the
  // dedicated worker but was closed, then this doesn't affect the cache usage.
  ASSERT_TRUE(HistoryGoBack(web_contents()));
  ExpectNotRestored(
      {NotRestoredReason::kBlocklistedFeatures},
      {blink::scheduler::WebSchedulerTrackedFeature::kBroadcastChannel}, {}, {},
      {}, FROM_HERE);
}

// Tests the case when the page starts fetching in a dedicated worker, goes to
// BFcache, and then a redirection happens. The cached page should evicted in
// this case.
IN_PROC_BROWSER_TEST_P(BackForwardCacheWithDedicatedWorkerBrowserTest,
                       FetchRedirectedWhileStoring) {
  CreateHttpsServer();

  net::test_server::ControllableHttpResponse fetch1_response(https_server(),
                                                             "/fetch1");
  net::test_server::ControllableHttpResponse fetch2_response(https_server(),
                                                             "/fetch2");
  ASSERT_TRUE(https_server()->Start());

  GURL url_a(https_server()->GetURL("a.test", "/title1.html"));
  GURL url_b(https_server()->GetURL("b.test", "/title1.html"));

  // Navigate to A.
  EXPECT_TRUE(NavigateToURL(shell(), url_a));
  RenderFrameHostImpl* rfh_a = current_frame_host();
  RenderFrameDeletedObserver delete_observer_rfh_a(rfh_a);

  // Trigger a fetch in a dedicated worker.
  std::string worker_script =
      JsReplace(R"(
    fetch($1);
  )",
                https_server()->GetURL("a.test", "/fetch1"));
  EXPECT_TRUE(ExecJs(rfh_a, JsReplace(R"(
    const blob = new Blob([$1]);
    const blobURL = URL.createObjectURL(blob);
    const worker = new Worker(blobURL);
  )",
                                      worker_script)));

  fetch1_response.WaitForRequest();

  // Navigate to B.
  PageLifecycleStateManagerTestDelegate delegate(
      rfh_a->render_view_host()->GetPageLifecycleStateManager());
  EXPECT_TRUE(NavigateToURL(shell(), url_b));
  delegate.WaitForInBackForwardCacheAck();

  // Page A is initially stored in the back-forward cache.
  EXPECT_TRUE(rfh_a->IsInBackForwardCache());

  // Respond the fetch with a redirect.
  fetch1_response.Send(
      "HTTP/1.1 302 Moved Temporarily\r\n"
      "Location: /fetch2\r\n\r\n");
  fetch1_response.Done();

  // Ensure that the request to /fetch2 was never sent (because the page is
  // immediately evicted) by checking after 3 seconds.
  base::RunLoop loop1;
  base::OneShotTimer timer1;
  timer1.Start(FROM_HERE, base::Seconds(3), loop1.QuitClosure());
  loop1.Run();
  EXPECT_EQ(nullptr, fetch2_response.http_request());

  // Page A should be evicted from the back-forward cache.
  delete_observer_rfh_a.WaitUntilDeleted();

  // Go back to A.
  web_contents()->GetController().GoBack();
  EXPECT_TRUE(WaitForLoadStop(shell()->web_contents()));
  ExpectNotRestored({NotRestoredReason::kNetworkRequestRedirected}, {}, {}, {},
                    {}, FROM_HERE);
}

// Tests the case when the page starts fetching in a nested dedicated worker,
// goes to BFcache, and then a redirection happens. The cached page should
// evicted in this case.
IN_PROC_BROWSER_TEST_P(BackForwardCacheWithDedicatedWorkerBrowserTest,
                       FetchRedirectedWhileStoring_Nested) {
  CreateHttpsServer();

  net::test_server::ControllableHttpResponse fetch1_response(https_server(),
                                                             "/fetch1");
  net::test_server::ControllableHttpResponse fetch2_response(https_server(),
                                                             "/fetch2");

  ASSERT_TRUE(https_server()->Start());

  GURL url_a(https_server()->GetURL("a.test", "/title1.html"));
  GURL url_b(https_server()->GetURL("b.test", "/title1.html"));

  // Navigate to A.
  EXPECT_TRUE(NavigateToURL(shell(), url_a));
  RenderFrameHostImpl* rfh_a = current_frame_host();
  RenderFrameDeletedObserver delete_observer_rfh_a(rfh_a);

  // Trigger a fetch in a nested dedicated worker.
  std::string child_worker_script =
      JsReplace(R"(
    fetch($1);
  )",
                https_server()->GetURL("a.test", "/fetch1"));
  std::string parent_worker_script = JsReplace(R"(
    const blob = new Blob([$1]);
    const blobURL = URL.createObjectURL(blob);
    const worker = new Worker(blobURL);
  )",
                                               child_worker_script);
  EXPECT_TRUE(ExecJs(rfh_a, JsReplace(R"(
    const blob = new Blob([$1]);
    const blobURL = URL.createObjectURL(blob);
    const worker = new Worker(blobURL);
    worker.onmessage = () => { resolve(); }
  )",
                                      parent_worker_script)));

  fetch1_response.WaitForRequest();

  // Navigate to B.
  PageLifecycleStateManagerTestDelegate delegate(
      rfh_a->render_view_host()->GetPageLifecycleStateManager());
  EXPECT_TRUE(NavigateToURL(shell(), url_b));
  delegate.WaitForInBackForwardCacheAck();

  // Page A is initially stored in the back-forward cache.
  EXPECT_TRUE(rfh_a->IsInBackForwardCache());

  // Respond the fetch with a redirect.
  fetch1_response.Send(
      "HTTP/1.1 302 Moved Temporarily\r\n"
      "Location: /fetch2\r\n\r\n");
  fetch1_response.Done();

  // Ensure that the request to /fetch2 was never sent (because the page is
  // immediately evicted) by checking after 3 seconds.
  base::RunLoop loop2;
  base::OneShotTimer timer2;
  timer2.Start(FROM_HERE, base::Seconds(3), loop2.QuitClosure());
  loop2.Run();
  EXPECT_EQ(nullptr, fetch2_response.http_request());

  // Page A should be evicted from the back-forward cache.
  delete_observer_rfh_a.WaitUntilDeleted();

  // Go back to A.
  web_contents()->GetController().GoBack();
  EXPECT_TRUE(WaitForLoadStop(shell()->web_contents()));
  ExpectNotRestored({NotRestoredReason::kNetworkRequestRedirected}, {}, {}, {},
                    {}, FROM_HERE);
}

// Tests the case when the page starts fetching in a dedicated worker, goes to
// BFcache, and then the response amount reaches the threshold. The cached page
// should evicted in this case.
IN_PROC_BROWSER_TEST_P(
    BackForwardCacheWithDedicatedWorkerBrowserTest,
    FetchStillLoading_ResponseStartedWhileFrozen_ExceedsPerProcessBytesLimit) {
  CreateHttpsServer();

  net::test_server::ControllableHttpResponse image_response(https_server(),
                                                            "/image.png");
  ASSERT_TRUE(https_server()->Start());

  // Navigate to a page.
  EXPECT_TRUE(
      NavigateToURL(shell(), https_server()->GetURL("a.test", "/title1.html")));
  RenderFrameHostImpl* rfh_a = current_frame_host();

  // Trigger a fetch in a dedicated worker.
  std::string worker_script =
      JsReplace(R"(
    fetch($1);
  )",
                https_server()->GetURL("a.test", "/image.png"));
  EXPECT_TRUE(ExecJs(rfh_a, JsReplace(R"(
    const blob = new Blob([$1]);
    const blobURL = URL.createObjectURL(blob);
    const worker = new Worker(blobURL);
  )",
                                      worker_script)));

  // Wait for the image request, but don't send anything yet.
  image_response.WaitForRequest();

  // Navigate away.
  PageLifecycleStateManagerTestDelegate delegate(
      rfh_a->render_view_host()->GetPageLifecycleStateManager());
  EXPECT_TRUE(
      NavigateToURL(shell(), https_server()->GetURL("b.test", "/title2.html")));
  delegate.WaitForInBackForwardCacheAck();

  // The worker was still loading when we navigated away, but it's still
  // eligible for back-forward cache.
  EXPECT_TRUE(rfh_a->IsInBackForwardCache());

  RenderFrameDeletedObserver delete_observer_rfh_a(rfh_a);
  // Start sending the image response while in the back-forward cache.
  image_response.Send(net::HTTP_OK, "image/png");
  std::string body(kMaxBufferedBytesPerProcess + 1, '*');
  image_response.Send(body);
  image_response.Done();
  delete_observer_rfh_a.WaitUntilDeleted();

  // Go back to the first page. We should not restore the page from the
  // back-forward cache.
  web_contents()->GetController().GoBack();
  EXPECT_TRUE(WaitForLoadStop(shell()->web_contents()));
  ExpectNotRestored({NotRestoredReason::kNetworkExceedsBufferLimit}, {}, {}, {},
                    {}, FROM_HERE);
}

// Tests the case when the page starts fetching in a nested dedicated worker,
// goes to BFcache, and then the response amount reaches the threshold. The
// cached page should evicted in this case.
IN_PROC_BROWSER_TEST_P(
    BackForwardCacheWithDedicatedWorkerBrowserTest,
    FetchStillLoading_ResponseStartedWhileFrozen_ExceedsPerProcessBytesLimit_Nested) {
  CreateHttpsServer();

  net::test_server::ControllableHttpResponse image_response(https_server(),
                                                            "/image.png");
  ASSERT_TRUE(https_server()->Start());

  // Navigate to a page.
  EXPECT_TRUE(
      NavigateToURL(shell(), https_server()->GetURL("a.test", "/title1.html")));
  RenderFrameHostImpl* rfh_a = current_frame_host();

  // Trigger a fetch in a nested dedicated worker.
  std::string child_worker_script =
      JsReplace(R"(
    fetch($1);
  )",
                https_server()->GetURL("a.test", "/image.png"));
  std::string parent_worker_script = JsReplace(R"(
    const blob = new Blob([$1]);
    const blobURL = URL.createObjectURL(blob);
    const worker = new Worker(blobURL);
  )",
                                               child_worker_script);
  EXPECT_TRUE(ExecJs(rfh_a, JsReplace(R"(
    const blob = new Blob([$1]);
    const blobURL = URL.createObjectURL(blob);
    const worker = new Worker(blobURL);
  )",
                                      parent_worker_script)));

  // Wait for the image request, but don't send anything yet.
  image_response.WaitForRequest();

  // Navigate away.
  PageLifecycleStateManagerTestDelegate delegate(
      rfh_a->render_view_host()->GetPageLifecycleStateManager());
  EXPECT_TRUE(
      NavigateToURL(shell(), https_server()->GetURL("b.test", "/title2.html")));
  delegate.WaitForInBackForwardCacheAck();
  // The worker was still loading when we navigated away, but it's still
  // eligible for back-forward cache.
  EXPECT_TRUE(rfh_a->IsInBackForwardCache());

  RenderFrameDeletedObserver delete_observer_rfh_a(rfh_a);
  // Start sending the image response while in the back-forward cache.
  image_response.Send(net::HTTP_OK, "image/png");
  std::string body(kMaxBufferedBytesPerProcess + 1, '*');
  image_response.Send(body);
  image_response.Done();
  delete_observer_rfh_a.WaitUntilDeleted();

  // Go back to the first page. We should not restore the page from the
  // back-forward cache.
  web_contents()->GetController().GoBack();
  EXPECT_TRUE(WaitForLoadStop(shell()->web_contents()));
  ExpectNotRestored({NotRestoredReason::kNetworkExceedsBufferLimit}, {}, {}, {},
                    {}, FROM_HERE);
}

// Tests the case when fetching started in a dedicated worker and the header was
// received before the page is frozen, but parts of the response body is
// received when the page is frozen.
IN_PROC_BROWSER_TEST_P(BackForwardCacheWithDedicatedWorkerBrowserTest,
                       PageWithDrainedDatapipeRequestsForFetchShouldBeEvicted) {
  CreateHttpsServer();

  net::test_server::ControllableHttpResponse fetch_response(https_server(),
                                                            "/fetch");

  ASSERT_TRUE(https_server()->Start());

  GURL url_a(https_server()->GetURL("a.test", "/title1.html"));
  GURL url_b(https_server()->GetURL("b.test", "/title1.html"));

  // Navigate to A.
  EXPECT_TRUE(NavigateToURL(shell(), url_a));
  RenderFrameHostImplWrapper rfh_a(current_frame_host());
  RenderFrameDeletedObserver delete_observer_rfh_a(rfh_a.get());

  // Call fetch in a dedicated worker before navigating away.
  std::string worker_script =
      JsReplace("fetch($1)", https_server()->GetURL("a.test", "/fetch"));
  EXPECT_TRUE(ExecJs(rfh_a.get(), JsReplace(R"(
    const blob = new Blob([$1]);
    const blobURL = URL.createObjectURL(blob);
    const worker = new Worker(blobURL);
  )",
                                            worker_script)));
  // Send response header and a piece of the body. This receiving the response
  // doesn't end (i.e. Done is not called) before navigating away. In this case,
  // the page will be evicted when the page is frozen.
  fetch_response.WaitForRequest();
  fetch_response.Send(net::HTTP_OK, "text/plain");
  fetch_response.Send("body");

  // Navigate to B.
  EXPECT_TRUE(NavigateToURL(shell(), url_b));

  delete_observer_rfh_a.WaitUntilDeleted();

  // Go back to A. kNetworkRequestDatapipeDrainedAsBytesConsumer is recorded
  // since receiving the response body started but this didn't end before the
  // navigation to B.
  web_contents()->GetController().GoBack();
  EXPECT_TRUE(WaitForLoadStop(shell()->web_contents()));
  ExpectNotRestored(
      {NotRestoredReason::kNetworkRequestDatapipeDrainedAsBytesConsumer}, {},
      {}, {}, {}, FROM_HERE);
}

// Tests the case when fetching started in a nested dedicated worker and the
// header was received before the page is frozen, but parts of the response body
// is received when the page is frozen.
IN_PROC_BROWSER_TEST_P(
    BackForwardCacheWithDedicatedWorkerBrowserTest,
    PageWithDrainedDatapipeRequestsForFetchShouldBeEvicted_Nested) {
  CreateHttpsServer();

  net::test_server::ControllableHttpResponse fetch_response(https_server(),
                                                            "/fetch");

  ASSERT_TRUE(https_server()->Start());

  GURL url_a(https_server()->GetURL("a.test", "/title1.html"));
  GURL url_b(https_server()->GetURL("b.test", "/title1.html"));

  // Navigate to A.
  EXPECT_TRUE(NavigateToURL(shell(), url_a));
  RenderFrameHostImplWrapper rfh_a(current_frame_host());
  RenderFrameDeletedObserver delete_observer_rfh_a(rfh_a.get());

  // Call fetch in a nested dedicated worker before navigating away.
  std::string child_worker_script =
      JsReplace("fetch($1)", https_server()->GetURL("a.test", "/fetch"));
  std::string parent_worker_script = JsReplace(R"(
    const blob = new Blob([$1]);
    const blobURL = URL.createObjectURL(blob);
    const worker = new Worker(blobURL);
  )",
                                               child_worker_script);
  EXPECT_TRUE(ExecJs(rfh_a.get(), JsReplace(R"(
    const blob = new Blob([$1]);
    const blobURL = URL.createObjectURL(blob);
    const worker = new Worker(blobURL);
  )",
                                            parent_worker_script)));
  // Send response header and a piece of the body. This receiving the response
  // doesn't end (i.e. Done is not called) before navigating away. In this case,
  // the page will be evicted when the page is frozen.
  fetch_response.WaitForRequest();
  fetch_response.Send(net::HTTP_OK, "text/plain");
  fetch_response.Send("body");

  // Navigate to B.
  EXPECT_TRUE(NavigateToURL(shell(), url_b));

  delete_observer_rfh_a.WaitUntilDeleted();

  // Go back to A. kNetworkRequestDatapipeDrainedAsBytesConsumer is recorded
  // since receiving the response body started but this didn't end before the
  // navigation to B.
  web_contents()->GetController().GoBack();
  EXPECT_TRUE(WaitForLoadStop(shell()->web_contents()));
  ExpectNotRestored(
      {NotRestoredReason::kNetworkRequestDatapipeDrainedAsBytesConsumer}, {},
      {}, {}, {}, FROM_HERE);
}

// Tests the case when fetch started in a dedicated worker, but the response
// never ends after the page is frozen. This should result in an eviction due to
// timeout.
IN_PROC_BROWSER_TEST_P(BackForwardCacheWithDedicatedWorkerBrowserTest,
                       ImageStillLoading_ResponseStartedWhileFrozen_Timeout) {
  CreateHttpsServer();

  net::test_server::ControllableHttpResponse image_response(https_server(),
                                                            "/image.png");
  ASSERT_TRUE(https_server()->Start());

  GURL url_a(https_server()->GetURL("a.test", "/title1.html"));
  GURL url_b(https_server()->GetURL("b.test", "/title1.html"));

  // Navigate to A.
  EXPECT_TRUE(NavigateToURL(shell(), url_a));
  RenderFrameHostImplWrapper rfh_a(current_frame_host());

  // Call fetch in a dedicated worker before navigating away.
  std::string worker_script =
      JsReplace(R"(
    fetch($1);
  )",
                https_server()->GetURL("a.test", "/image.png"));
  EXPECT_TRUE(ExecJs(rfh_a.get(), JsReplace(R"(
    const blob = new Blob([$1]);
    const blobURL = URL.createObjectURL(blob);
    const worker = new Worker(blobURL);
  )",
                                            worker_script)));

  // Wait for the image request, but don't send anything yet.
  image_response.WaitForRequest();

  // Navigate away.
  PageLifecycleStateManagerTestDelegate delegate(
      rfh_a->render_view_host()->GetPageLifecycleStateManager());
  EXPECT_TRUE(NavigateToURL(shell(), url_b));
  delegate.WaitForInBackForwardCacheAck();
  // The page was still loading when we navigated away, but it's still eligible
  // for back-forward cache.
  EXPECT_TRUE(rfh_a->IsInBackForwardCache());

  RenderFrameDeletedObserver delete_observer(rfh_a.get());
  // Start sending the image response while in the back-forward cache, but never
  // finish the request. Eventually the page will get deleted due to network
  // request timeout.
  image_response.Send(net::HTTP_OK, "image/png");
  delete_observer.WaitUntilDeleted();

  // 3) Go back to the first page. We should not restore the page from the
  // back-forward cache.
  ASSERT_TRUE(HistoryGoBack(web_contents()));
  ExpectNotRestored({NotRestoredReason::kNetworkRequestTimeout}, {}, {}, {}, {},
                    FROM_HERE);
}

// Tests the case when fetch started in a nested dedicated worker, but the
// response never ends after the page is frozen. This should result in an
// eviction due to timeout.
IN_PROC_BROWSER_TEST_P(
    BackForwardCacheWithDedicatedWorkerBrowserTest,
    ImageStillLoading_ResponseStartedWhileFrozen_Timeout_Nested) {
  CreateHttpsServer();

  net::test_server::ControllableHttpResponse image_response(https_server(),
                                                            "/image.png");
  ASSERT_TRUE(https_server()->Start());

  GURL url_a(https_server()->GetURL("a.test", "/title1.html"));
  GURL url_b(https_server()->GetURL("b.test", "/title1.html"));

  // Navigate to A.
  EXPECT_TRUE(NavigateToURL(shell(), url_a));
  RenderFrameHostImplWrapper rfh_a(current_frame_host());

  // Call fetch in a dedicated worker before navigating away.
  std::string child_worker_script =
      JsReplace(R"(
    fetch($1);
  )",
                https_server()->GetURL("a.test", "/image.png"));
  std::string parent_worker_script = JsReplace(R"(
    const blob = new Blob([$1]);
    const blobURL = URL.createObjectURL(blob);
    const worker = new Worker(blobURL);
  )",
                                               child_worker_script);
  EXPECT_TRUE(ExecJs(rfh_a.get(), JsReplace(R"(
    const blob = new Blob([$1]);
    const blobURL = URL.createObjectURL(blob);
    const worker = new Worker(blobURL);
  )",
                                            parent_worker_script)));

  // Wait for the image request, but don't send anything yet.
  image_response.WaitForRequest();

  // Navigate away.
  PageLifecycleStateManagerTestDelegate delegate(
      rfh_a->render_view_host()->GetPageLifecycleStateManager());
  EXPECT_TRUE(NavigateToURL(shell(), url_b));
  delegate.WaitForInBackForwardCacheAck();
  // The page was still loading when we navigated away, but it's still eligible
  // for back-forward cache.
  EXPECT_TRUE(rfh_a->IsInBackForwardCache());

  RenderFrameDeletedObserver delete_observer(rfh_a.get());
  // Start sending the image response while in the back-forward cache, but never
  // finish the request. Eventually the page will get deleted due to network
  // request timeout.
  image_response.Send(net::HTTP_OK, "image/png");
  delete_observer.WaitUntilDeleted();

  // 3) Go back to the first page. We should not restore the page from the
  // back-forward cache.
  ASSERT_TRUE(HistoryGoBack(web_contents()));
  ExpectNotRestored({NotRestoredReason::kNetworkRequestTimeout}, {}, {}, {}, {},
                    FROM_HERE);
}

// Tests that dedicated workers in back/forward cache are not visible to a
// service worker.
IN_PROC_BROWSER_TEST_P(BackForwardCacheWithDedicatedWorkerBrowserTest,
                       ServiceWorkerClientMatchAll) {
  CreateHttpsServer();
  ASSERT_TRUE(https_server()->Start());

  GURL url_a1(https_server()->GetURL(
      "a.test", "/service_worker/create_service_worker.html"));
  GURL url_a2(https_server()->GetURL("a.test", "/service_worker/empty.html"));

  // Navigate to A.
  EXPECT_TRUE(NavigateToURL(shell(), url_a1));
  EXPECT_EQ(
      "DONE",
      EvalJs(current_frame_host(),
             "register('/service_worker/fetch_event_worker_clients.js');"));

  // Reload the page to enable fetch to be hooked by the service worker.
  web_contents()->GetController().Reload(content::ReloadType::NORMAL, false);
  EXPECT_TRUE(WaitForLoadStop(web_contents()));
  RenderFrameHostImplWrapper rfh_a(current_frame_host());

  // Confirm there is no worker client.
  EXPECT_EQ(0, CountWorkerClients(rfh_a.get()));

  // Call fetch in a dedicated worker. If the PlzDedicatedWorker is enabled, the
  // number of worker clients should be 1. If PlzDedicatedWorker is disabled,
  // worker clients are not supported, so the number should be 0.
  int expected_number = IsPlzDedicatedWorkerEnabled() ? 1 : 0;
  std::string dedicated_worker_script = JsReplace(
      R"(
    (async() => {
      const response = await fetch($1);
      postMessage(await response.text());
    })();
  )",
      https_server()->GetURL("a.test", "/service_worker/count_worker_clients"));
  EXPECT_EQ(base::NumberToString(expected_number),
            EvalJs(rfh_a.get(), JsReplace(R"(
    new Promise(async (resolve) => {
      const blobURL = URL.createObjectURL(new Blob([$1]));
      const dedicatedWorker = new Worker(blobURL);
      dedicatedWorker.addEventListener('message', e => {
        resolve(e.data);
      });
    });
  )",
                                          dedicated_worker_script)));

  // Navigate away.
  EXPECT_TRUE(NavigateToURL(shell(), url_a2));
  EXPECT_TRUE(rfh_a->IsInBackForwardCache());

  // Confirm that the worker in back/forward cache is invisible from the service
  // worker.
  EXPECT_EQ(0, CountWorkerClients(current_frame_host()));

  // Restore from the back/forward cache.
  ASSERT_TRUE(HistoryGoBack(web_contents()));
  EXPECT_EQ(expected_number, CountWorkerClients(current_frame_host()));
}

// Tests that dedicated workers, including a nested dedicated workers, in
// back/forward cache are not visible to a service worker.
IN_PROC_BROWSER_TEST_P(BackForwardCacheWithDedicatedWorkerBrowserTest,
                       ServiceWorkerClientMatchAll_Nested) {
  CreateHttpsServer();
  ASSERT_TRUE(https_server()->Start());

  GURL url_a1(https_server()->GetURL(
      "a.test", "/service_worker/create_service_worker.html"));
  GURL url_a2(https_server()->GetURL("a.test", "/service_worker/empty.html"));

  // Navigate to A.
  EXPECT_TRUE(NavigateToURL(shell(), url_a1));
  EXPECT_EQ(
      "DONE",
      EvalJs(current_frame_host(),
             "register('/service_worker/fetch_event_worker_clients.js');"));

  // Reload the page to enable fetch to be hooked by the service worker.
  web_contents()->GetController().Reload(content::ReloadType::NORMAL, false);
  EXPECT_TRUE(WaitForLoadStop(web_contents()));
  RenderFrameHostImplWrapper rfh_a(current_frame_host());

  // Confirm there is no worker client.
  EXPECT_EQ(0, CountWorkerClients(rfh_a.get()));

  // Call fetch in a dedicated worker. If the PlzDedicatedWorker is enabled, the
  // number of worker clients should be 2. If PlzDedicatedWorker is disabled,
  // worker clients are not supported, so the number should be 0.
  int expected_number = IsPlzDedicatedWorkerEnabled() ? 2 : 0;
  std::string child_worker_script = JsReplace(
      R"(
    (async() => {
      const response = await fetch($1);
      postMessage(await response.text());
    })();
  )",
      https_server()->GetURL("a.test", "/service_worker/count_worker_clients"));
  std::string parent_worker_script = JsReplace(
      R"(
    const blobURL = URL.createObjectURL(new Blob([$1]));
    const dedicatedWorker = new Worker(blobURL);
    dedicatedWorker.addEventListener('message', e => {
      postMessage(e.data);
    });
  )",
      child_worker_script);
  EXPECT_EQ(base::NumberToString(expected_number),
            EvalJs(rfh_a.get(), JsReplace(R"(
    new Promise(async (resolve) => {
      const blobURL = URL.createObjectURL(new Blob([$1]));
      const dedicatedWorker = new Worker(blobURL);
      dedicatedWorker.addEventListener('message', e => {
        resolve(e.data);
      });
    });
  )",
                                          parent_worker_script)));

  // Navigate away.
  EXPECT_TRUE(NavigateToURL(shell(), url_a2));
  EXPECT_TRUE(rfh_a->IsInBackForwardCache());

  // Confirm that the worker in back/forward cache is invisible from the service
  // worker.
  EXPECT_EQ(0, CountWorkerClients(current_frame_host()));

  // Restore from the back/forward cache.
  ASSERT_TRUE(HistoryGoBack(web_contents()));
  EXPECT_EQ(expected_number, CountWorkerClients(current_frame_host()));
}

// Tests that dedicated workers in back/forward cache are not visible to a
// service worker. This works correctly even if a dedicated worker is not loaded
// completely when the page is put into back/forward cache,
IN_PROC_BROWSER_TEST_P(BackForwardCacheWithDedicatedWorkerBrowserTest,
                       ServiceWorkerClientMatchAll_LoadWorkerAfterRestoring) {
  CreateHttpsServer();

  // Prepare a controllable HTTP response for a dedicated worker. Use
  // /service_worker path to match with the service worker's scope.
  net::test_server::ControllableHttpResponse dedicated_worker_response(
      https_server(),
      "/service_worker/dedicated_worker_using_service_worker.js");

  ASSERT_TRUE(https_server()->Start());

  GURL url_a1(https_server()->GetURL(
      "a.test", "/service_worker/create_service_worker.html"));
  GURL url_a2(https_server()->GetURL("a.test", "/service_worker/empty.html"));

  // Navigate to A.
  EXPECT_TRUE(NavigateToURL(shell(), url_a1));
  EXPECT_EQ(
      "DONE",
      EvalJs(current_frame_host(),
             "register('/service_worker/fetch_event_worker_clients.js');"));

  // Reload the page to enable fetch to be hooked by the service worker.
  web_contents()->GetController().Reload(content::ReloadType::NORMAL, false);
  EXPECT_TRUE(WaitForLoadStop(web_contents()));
  RenderFrameHostImplWrapper rfh_a(current_frame_host());

  // Confirm there is no worker client.
  EXPECT_EQ(0, CountWorkerClients(rfh_a.get()));

  // Start to requet a worker URL.
  EXPECT_TRUE(ExecJs(rfh_a.get(), R"(
    window.dedicatedWorkerUsingServiceWorker = new Worker(
        '/service_worker/dedicated_worker_using_service_worker.js');
  )"));

  dedicated_worker_response.WaitForRequest();

  // Navigate away.
  EXPECT_TRUE(NavigateToURL(shell(), url_a2));
  EXPECT_TRUE(rfh_a->IsInBackForwardCache());

  // Return the dedicated worker script.
  dedicated_worker_response.Send(net::HTTP_OK, "text/javascript");
  dedicated_worker_response.Send(R"(
    onmessage = e => {
      postMessage(e.data);
    };
  )");
  dedicated_worker_response.Done();

  // Confirm that the worker in back/forward cache is invisible from the service
  // worker.
  EXPECT_EQ(0, CountWorkerClients(current_frame_host()));

  // Restore from the back/forward cache. Now the number of client is 1.
  ASSERT_TRUE(HistoryGoBack(web_contents()));
  ExpectRestored(FROM_HERE);

  // Confirm that the dedicated worker is completely loaded.
  EXPECT_EQ("foo", EvalJs(current_frame_host(), JsReplace(R"(
    new Promise(async (resolve) => {
      window.dedicatedWorkerUsingServiceWorker.onmessage = e => {
        resolve(e.data);
      };
      window.dedicatedWorkerUsingServiceWorker.postMessage("foo");
    });
  )")));

  // If the PlzDedicatedWorker is enabled, the number of worker clients should
  // be 1. If PlzDedicatedWorker is disabled, worker clients are not supported,
  // so the number should be 0.
  EXPECT_EQ(IsPlzDedicatedWorkerEnabled() ? 1 : 0,
            CountWorkerClients(current_frame_host()));
}

// TODO(https://crbug.com/154571): Shared workers are not available on Android.
#if BUILDFLAG(IS_ANDROID)
#define MAYBE_PageWithSharedWorkerNotCached \
  DISABLED_PageWithSharedWorkerNotCached
#else
#define MAYBE_PageWithSharedWorkerNotCached PageWithSharedWorkerNotCached
#endif
IN_PROC_BROWSER_TEST_F(BackForwardCacheBrowserTest,
                       MAYBE_PageWithSharedWorkerNotCached) {
  ASSERT_TRUE(embedded_test_server()->Start());

  EXPECT_TRUE(NavigateToURL(
      shell(),
      embedded_test_server()->GetURL(
          "a.com", "/back_forward_cache/page_with_shared_worker.html")));
  RenderFrameDeletedObserver delete_observer_rfh_a(current_frame_host());

  // Navigate away.
  EXPECT_TRUE(NavigateToURL(
      shell(), embedded_test_server()->GetURL("b.com", "/title1.html")));

  // The page with the unsupported feature should be deleted (not cached).
  delete_observer_rfh_a.WaitUntilDeleted();

  // Go back.
  ASSERT_TRUE(HistoryGoBack(web_contents()));
  ExpectNotRestored(
      {NotRestoredReason::kBlocklistedFeatures},
      {blink::scheduler::WebSchedulerTrackedFeature::kSharedWorker}, {}, {}, {},
      FROM_HERE);
}

IN_PROC_BROWSER_TEST_F(BackForwardCacheBrowserTest,
                       AllowedFeaturesForSubframesDoNotEvict) {
  // The main purpose of this test is to check that when a state of a subframe
  // is updated, CanStoreDocument is still called for the main frame - otherwise
  // we would always evict the document, even when the feature is allowed as
  // CanStoreDocument always returns false for non-main frames.

  ASSERT_TRUE(embedded_test_server()->Start());
  GURL url_a(embedded_test_server()->GetURL(
      "a.com", "/cross_site_iframe_factory.html?a(b)"));
  GURL url_c(embedded_test_server()->GetURL("c.com", "/title1.html"));

  // 1) Navigate to A.
  ASSERT_TRUE(NavigateToURL(shell(), url_a));
  RenderFrameHostImpl* rfh_a = current_frame_host();
  RenderFrameHostImpl* rfh_b = rfh_a->child_at(0)->current_frame_host();
  RenderFrameDeletedObserver delete_observer_rfh_b(rfh_b);

  // 2) Navigate to C.
  ASSERT_TRUE(NavigateToURL(shell(), url_c));

  // 3) No-op feature update on a subframe while in cache, should be no-op.
  ASSERT_FALSE(delete_observer_rfh_b.deleted());
  rfh_b->DidChangeBackForwardCacheDisablingFeatures(0);

  // 4) Go back.
  ASSERT_TRUE(HistoryGoBack(web_contents()));
  EXPECT_EQ(current_frame_host(), rfh_a);

  ExpectRestored(FROM_HERE);
}

IN_PROC_BROWSER_TEST_F(BackForwardCacheBrowserTest,
                       DoesNotCacheIfRecordingAudio) {
  ASSERT_TRUE(embedded_test_server()->Start());

  BackForwardCacheDisabledTester tester;

  // Navigate to an empty page.
  GURL url(embedded_test_server()->GetURL("/title1.html"));
  EXPECT_TRUE(NavigateToURL(shell(), url));
  int process_id = current_frame_host()->GetProcess()->GetID();
  int routing_id = current_frame_host()->GetRoutingID();

  // Request for audio recording.
  EXPECT_EQ("success", EvalJs(current_frame_host(), R"(
    new Promise(resolve => {
      navigator.mediaDevices.getUserMedia({audio: true})
        .then(m => { window.keepaliveMedia = m; resolve("success"); })
        .catch(() => { resolve("error"); });
    });
  )"));

  RenderFrameDeletedObserver deleted(current_frame_host());

  // 2) Navigate away.
  shell()->LoadURL(embedded_test_server()->GetURL("b.com", "/title1.html"));

  // The page was still recording audio when we navigated away, so it shouldn't
  // have been cached.
  deleted.WaitUntilDeleted();

  // 3) Go back. Note that the reason for kWasGrantedMediaAccess occurs after
  // MediaDevicesDispatcherHost is called, hence, both are reasons for the page
  // not being restored.
  ASSERT_TRUE(HistoryGoBack(web_contents()));
  auto reason = BackForwardCacheDisable::DisabledReason(
      BackForwardCacheDisable::DisabledReasonId::kMediaDevicesDispatcherHost);
  ExpectNotRestored({NotRestoredReason::kWasGrantedMediaAccess,
                     NotRestoredReason::kDisableForRenderFrameHostCalled},
                    {}, {}, {reason}, {}, FROM_HERE);
  EXPECT_TRUE(
      tester.IsDisabledForFrameWithReason(process_id, routing_id, reason));
}

IN_PROC_BROWSER_TEST_F(BackForwardCacheBrowserTest,
                       DoesNotCacheIfSubframeRecordingAudio) {
  ASSERT_TRUE(embedded_test_server()->Start());

  BackForwardCacheDisabledTester tester;

  // Navigate to a page with an iframe.
  GURL url(embedded_test_server()->GetURL("/page_with_iframe.html"));
  EXPECT_TRUE(NavigateToURL(shell(), url));
  RenderFrameHostImpl* rfh = current_frame_host();
  int process_id =
      rfh->child_at(0)->current_frame_host()->GetProcess()->GetID();
  int routing_id = rfh->child_at(0)->current_frame_host()->GetRoutingID();

  // Request for audio recording from the subframe.
  EXPECT_EQ("success", EvalJs(rfh->child_at(0)->current_frame_host(), R"(
    new Promise(resolve => {
      navigator.mediaDevices.getUserMedia({audio: true})
        .then(m => { resolve("success"); })
        .catch(() => { resolve("error"); });
    });
  )"));

  RenderFrameDeletedObserver deleted(current_frame_host());

  // 2) Navigate away.
  shell()->LoadURL(embedded_test_server()->GetURL("b.com", "/title1.html"));

  // The page was still recording audio when we navigated away, so it shouldn't
  // have been cached.
  deleted.WaitUntilDeleted();

  // 3) Go back. Note that the reason for kWasGrantedMediaAccess occurs after
  // MediaDevicesDispatcherHost is called, hence, both are reasons for the page
  // not being restored.
  ASSERT_TRUE(HistoryGoBack(web_contents()));
  auto reason = BackForwardCacheDisable::DisabledReason(
      BackForwardCacheDisable::DisabledReasonId::kMediaDevicesDispatcherHost);

  ExpectNotRestored({NotRestoredReason::kWasGrantedMediaAccess,
                     NotRestoredReason::kDisableForRenderFrameHostCalled},
                    {}, {}, {reason}, {}, FROM_HERE);
  EXPECT_TRUE(
      tester.IsDisabledForFrameWithReason(process_id, routing_id, reason));
}

IN_PROC_BROWSER_TEST_F(BackForwardCacheBrowserTest,
                       DoesNotCacheIfMediaDeviceSubscribed) {
  ASSERT_TRUE(embedded_test_server()->Start());

  BackForwardCacheDisabledTester tester;

  // Navigate to a page with an iframe.
  GURL url(embedded_test_server()->GetURL("/page_with_iframe.html"));
  EXPECT_TRUE(NavigateToURL(shell(), url));
  RenderFrameHostImpl* rfh = current_frame_host();
  int process_id =
      rfh->child_at(0)->current_frame_host()->GetProcess()->GetID();
  int routing_id = rfh->child_at(0)->current_frame_host()->GetRoutingID();

  EXPECT_EQ("success", EvalJs(rfh->child_at(0)->current_frame_host(), R"(
    new Promise(resolve => {
      navigator.mediaDevices.addEventListener(
          'devicechange', function(event){});
      resolve("success");
    });
  )"));

  RenderFrameDeletedObserver deleted(current_frame_host());

  // 2) Navigate away.
  shell()->LoadURL(embedded_test_server()->GetURL("b.com", "/title1.html"));

  // The page was subscribed to media devices when we navigated away, so it
  // shouldn't have been cached.
  deleted.WaitUntilDeleted();

  // 3) Go back.
  ASSERT_TRUE(HistoryGoBack(web_contents()));
  auto reason = BackForwardCacheDisable::DisabledReason(
      BackForwardCacheDisable::DisabledReasonId::kMediaDevicesDispatcherHost);
  ExpectNotRestored({NotRestoredReason::kDisableForRenderFrameHostCalled}, {},
                    {}, {reason}, {}, FROM_HERE);
  EXPECT_TRUE(
      tester.IsDisabledForFrameWithReason(process_id, routing_id, reason));
}

IN_PROC_BROWSER_TEST_F(BackForwardCacheBrowserTest, CacheIfWebGL) {
  ASSERT_TRUE(embedded_test_server()->Start());

  // 1) Navigate to a page with WebGL usage
  GURL url(embedded_test_server()->GetURL(
      "example.com", "/back_forward_cache/page_with_webgl.html"));
  EXPECT_TRUE(NavigateToURL(shell(), url));

  // 2) Navigate away.
  EXPECT_TRUE(NavigateToURL(
      shell(), embedded_test_server()->GetURL("b.com", "/title1.html")));

  // The page had an active WebGL context when we navigated away,
  // but it should be cached.

  // 3) Go back.
  ASSERT_TRUE(HistoryGoBack(web_contents()));
  ExpectRestored(FROM_HERE);
}

// Since blink::mojom::HidService binder is not added in
// content/browser/browser_interface_binders.cc for Android, this test is not
// applicable for this OS.
#if !BUILDFLAG(IS_ANDROID)
IN_PROC_BROWSER_TEST_F(BackForwardCacheBrowserTest, DoesNotCacheIfWebHID) {
  ASSERT_TRUE(embedded_test_server()->Start());

  // 1) Navigate to an empty page.
  GURL url(embedded_test_server()->GetURL("/title1.html"));
  EXPECT_TRUE(NavigateToURL(shell(), url));

  // Request for HID devices.
  EXPECT_EQ("success", EvalJs(current_frame_host(), R"(
    new Promise(resolve => {
      navigator.hid.getDevices()
        .then(m => { resolve("success"); })
        .catch(() => { resolve("error"); });
    });
  )"));

  RenderFrameDeletedObserver deleted(current_frame_host());

  // 2) Navigate away.
  shell()->LoadURL(embedded_test_server()->GetURL("b.com", "/title1.html"));

  // The page uses WebHID so it should be deleted.
  deleted.WaitUntilDeleted();

  // 3) Go back.
  ASSERT_TRUE(HistoryGoBack(web_contents()));
  ExpectNotRestored({NotRestoredReason::kBlocklistedFeatures},
                    {blink::scheduler::WebSchedulerTrackedFeature::kWebHID}, {},
                    {}, {}, FROM_HERE);
}
#endif  // !BUILDFLAG(IS_ANDROID)

IN_PROC_BROWSER_TEST_F(BackForwardCacheBrowserTest,
                       WakeLockReleasedUponEnteringBfcache) {
  ASSERT_TRUE(CreateHttpsServer()->Start());

  // 1) Navigate to a page with WakeLock usage.
  GURL url(https_server()->GetURL(
      "a.test", "/back_forward_cache/page_with_wakelock.html"));
  EXPECT_TRUE(NavigateToURL(shell(), url));

  RenderFrameHostImpl* rfh_a = current_frame_host();
  // Acquire WakeLock.
  EXPECT_EQ("DONE", EvalJs(rfh_a, "acquireWakeLock()"));
  // Make sure that WakeLock is not released yet.
  EXPECT_FALSE(EvalJs(rfh_a, "wakeLockIsReleased()").ExtractBool());

  // 2) Navigate away.
  shell()->LoadURL(https_server()->GetURL("b.test", "/title1.html"));
  EXPECT_TRUE(WaitForLoadStop(shell()->web_contents()));
  EXPECT_TRUE(rfh_a->IsInBackForwardCache());

  // 3) Go back to the page with WakeLock, restored from BackForwardCache.
  ASSERT_TRUE(HistoryGoBack(web_contents()));
  EXPECT_EQ(current_frame_host(), rfh_a);
  EXPECT_TRUE(EvalJs(rfh_a, "wakeLockIsReleased()").ExtractBool());
  ExpectRestored(FROM_HERE);
}

IN_PROC_BROWSER_TEST_F(BackForwardCacheBrowserTest, CacheWithWebFileSystem) {
  ASSERT_TRUE(embedded_test_server()->Start());

  // 1) Navigate to a page with WebFileSystem usage.
  GURL url(embedded_test_server()->GetURL("a.test", "/title1.html"));
  EXPECT_TRUE(NavigateToURL(shell(), url));
  RenderFrameHostImpl* rfh_a = current_frame_host();
  // Writer a file 'file.txt' with a content 'foo'.
  EXPECT_EQ("success", EvalJs(rfh_a, R"(
      new Promise((resolve, reject) => {
        window.webkitRequestFileSystem(
          window.TEMPORARY,
          1024 * 1024,
          (fs) => {
            fs.root.getFile('file.txt', {create: true}, (entry) => {
              entry.createWriter((writer) => {
                writer.onwriteend = () => {
                  resolve('success');
                };
                writer.onerror = reject;
                var blob = new Blob(['foo'], {type: 'text/plain'});
                writer.write(blob);
              }, reject);
            }, reject);
          }, reject);
        });
    )"));

  // 2) Navigate away.
  shell()->LoadURL(embedded_test_server()->GetURL("b.test", "/title1.html"));
  EXPECT_TRUE(WaitForLoadStop(shell()->web_contents()));

  // 3) Go back to the page with WebFileSystem.
  ASSERT_TRUE(HistoryGoBack(web_contents()));
  ExpectRestored(FROM_HERE);
  // Check the file content is reserved.
  EXPECT_EQ("foo", EvalJs(rfh_a, R"(
      new Promise((resolve, reject) => {
        window.webkitRequestFileSystem(
          window.TEMPORARY,
          1024 * 1024,
          (fs) => {
            fs.root.getFile('file.txt', {}, (entry) => {
              entry.file((file) => {
                const reader = new FileReader();
                reader.onloadend = (e) => {
                  resolve(e.target.result);
                };
                reader.readAsText(file);
              }, reject);
            }, reject);
          }, reject);
        });
    )"));
}

namespace {

class FakeIdleTimeProvider : public ui::IdleTimeProvider {
 public:
  FakeIdleTimeProvider() = default;
  ~FakeIdleTimeProvider() override = default;
  FakeIdleTimeProvider(const FakeIdleTimeProvider&) = delete;
  FakeIdleTimeProvider& operator=(const FakeIdleTimeProvider&) = delete;

  base::TimeDelta CalculateIdleTime() override { return base::Seconds(0); }

  bool CheckIdleStateIsLocked() override { return false; }
};

}  // namespace

IN_PROC_BROWSER_TEST_F(BackForwardCacheBrowserTest, DoesNotCacheIdleManager) {
  ASSERT_TRUE(embedded_test_server()->Start());

  // 1) Navigate to a page and start using the IdleManager class.
  GURL url(embedded_test_server()->GetURL("/title1.html"));
  EXPECT_TRUE(NavigateToURL(shell(), url));
  RenderFrameHostImpl* rfh_a = current_frame_host();
  RenderFrameDeletedObserver deleted(rfh_a);

  ui::test::ScopedIdleProviderForTest scoped_idle_provider(
      std::make_unique<FakeIdleTimeProvider>());

  EXPECT_TRUE(ExecJs(rfh_a, R"(
    new Promise(async resolve => {
      let idleDetector = new IdleDetector();
      idleDetector.start();
      resolve();
    });
  )"));

  // 2) Navigate away.
  shell()->LoadURL(embedded_test_server()->GetURL("b.com", "/title1.html"));

  // The page uses IdleManager so it should be deleted.
  deleted.WaitUntilDeleted();

  // 3) Go back and make sure the IdleManager page wasn't in the cache.
  ASSERT_TRUE(HistoryGoBack(web_contents()));
  ExpectNotRestored(
      {NotRestoredReason::kBlocklistedFeatures},
      {blink::scheduler::WebSchedulerTrackedFeature::kIdleManager}, {}, {}, {},
      FROM_HERE);
}

IN_PROC_BROWSER_TEST_F(BackForwardCacheBrowserTest, DoesNotCacheSMSService) {
  ASSERT_TRUE(embedded_test_server()->Start());

  // 1) Navigate to a page and start using the SMSService.
  GURL url(embedded_test_server()->GetURL("/title1.html"));
  EXPECT_TRUE(NavigateToURL(shell(), url));
  RenderFrameHostImpl* rfh_a = current_frame_host();
  RenderFrameDeletedObserver rfh_a_deleted(rfh_a);

  EXPECT_TRUE(ExecJs(rfh_a, R"(
    navigator.credentials.get({otp: {transport: ["sms"]}});
  )",
                     EXECUTE_SCRIPT_NO_RESOLVE_PROMISES));

  // 2) Navigate away.
  EXPECT_TRUE(NavigateToURL(
      shell(), embedded_test_server()->GetURL("b.com", "/title1.html")));

  // The page uses SMSService so it should be deleted.
  rfh_a_deleted.WaitUntilDeleted();

  // 3) Go back and make sure the SMSService page wasn't in the cache.
  ASSERT_TRUE(HistoryGoBack(web_contents()));

  // Note that on certain linux tests, there is occasionally a not restored
  // reason of kDisableForRenderFrameHostCalled. This is due to the javascript
  // navigator.credentials.get, which will call on authentication code for linux
  // but not other operating systems. The authenticator code explicitly invokes
  // kDisableForRenderFrameHostCalled. This causes flakiness if we check against
  // all not restored reasons. As a result, we only check for the blocklist
  // reason.
  ExpectBlocklistedFeature(
      blink::scheduler::WebSchedulerTrackedFeature::kWebOTPService, FROM_HERE);
}

IN_PROC_BROWSER_TEST_F(BackForwardCacheBrowserTest,
                       DoesNotCachePaymentManager) {
  ASSERT_TRUE(CreateHttpsServer()->Start());

  // 1) Navigate to a page which includes PaymentManager functionality. Note
  // that service workers are used, and therefore we use https server instead of
  // embedded_server()
  EXPECT_TRUE(NavigateToURL(
      shell(), https_server()->GetURL(
                   "a.test", "/payments/payment_app_invocation.html")));
  RenderFrameHostImpl* rfh_a = current_frame_host();
  RenderFrameDeletedObserver rfh_a_deleted(rfh_a);

  // Execute functionality that calls PaymentManager.
  EXPECT_TRUE(ExecJs(rfh_a, R"(
    new Promise(async resolve => {
      registerPaymentApp();
      resolve();
    });
  )"));

  // 2) Navigate away.
  EXPECT_TRUE(
      NavigateToURL(shell(), https_server()->GetURL("b.test", "/title1.html")));

  // The page uses PaymentManager so it should be deleted.
  rfh_a_deleted.WaitUntilDeleted();

  // 3) Go back.
  ASSERT_TRUE(HistoryGoBack(web_contents()));
  ExpectNotRestored(
      {NotRestoredReason::kBlocklistedFeatures},
      {blink::scheduler::WebSchedulerTrackedFeature::kPaymentManager}, {}, {},
      {}, FROM_HERE);

  // Note that on Mac10.10, there is occasionally blocklisting for network
  // requests (kOutstandingNetworkRequestOthers). This causes flakiness if we
  // check against all blocklisted features. As a result, we only check for the
  // blocklist we care about.
  base::HistogramBase::Sample sample = base::HistogramBase::Sample(
      blink::scheduler::WebSchedulerTrackedFeature::kPaymentManager);
  std::vector<base::Bucket> blocklist_values = histogram_tester().GetAllSamples(
      "BackForwardCache.HistoryNavigationOutcome."
      "BlocklistedFeature");
  EXPECT_TRUE(base::Contains(blocklist_values, sample, &base::Bucket::min));

  std::vector<base::Bucket> all_sites_blocklist_values =
      histogram_tester().GetAllSamples(
          "BackForwardCache.AllSites.HistoryNavigationOutcome."
          "BlocklistedFeature");

  EXPECT_TRUE(
      base::Contains(all_sites_blocklist_values, sample, &base::Bucket::min));
}

// Pages with acquired keyboard lock should not enter BackForwardCache.
IN_PROC_BROWSER_TEST_F(BackForwardCacheBrowserTest,
                       DoesNotCacheOnKeyboardLock) {
  ASSERT_TRUE(embedded_test_server()->Start());

  // 1) Navigate to a page and start using the Keyboard lock.
  GURL url(embedded_test_server()->GetURL("/title1.html"));
  EXPECT_TRUE(NavigateToURL(shell(), url));
  RenderFrameHostImpl* rfh_a = current_frame_host();
  RenderFrameDeletedObserver rfh_a_deleted(rfh_a);

  AcquireKeyboardLock(rfh_a);

  // 2) Navigate away.
  EXPECT_TRUE(NavigateToURL(
      shell(), embedded_test_server()->GetURL("b.com", "/title1.html")));

  // The page uses keyboard lock so it should be deleted.
  rfh_a_deleted.WaitUntilDeleted();

  // 3) Go back and make sure the keyboard lock page wasn't in the cache.
  ASSERT_TRUE(HistoryGoBack(web_contents()));
  ExpectNotRestored(
      {NotRestoredReason::kBlocklistedFeatures},
      {blink::scheduler::WebSchedulerTrackedFeature::kKeyboardLock}, {}, {}, {},
      FROM_HERE);
}

// If pages released keyboard lock, they can enter BackForwardCache. It will
// remain eligible for multiple restores.
IN_PROC_BROWSER_TEST_F(BackForwardCacheBrowserTest,
                       CacheIfKeyboardLockReleasedMultipleRestores) {
  ASSERT_TRUE(embedded_test_server()->Start());

  // 1) Navigate to a page and start using the Keyboard lock.
  GURL url(embedded_test_server()->GetURL("/title1.html"));
  EXPECT_TRUE(NavigateToURL(shell(), url));
  RenderFrameHostImplWrapper rfh_a(current_frame_host());
  RenderFrameDeletedObserver rfh_a_deleted(rfh_a.get());

  AcquireKeyboardLock(rfh_a.get());
  ReleaseKeyboardLock(rfh_a.get());

  // 2) Navigate away.
  EXPECT_TRUE(NavigateToURL(
      shell(), embedded_test_server()->GetURL("b.com", "/title1.html")));
  RenderFrameHostImplWrapper rfh_b(current_frame_host());

  // 3) Go back and page should be restored from BackForwardCache.
  ASSERT_TRUE(HistoryGoBack(web_contents()));
  ExpectRestored(FROM_HERE);

  // 4) Go forward and back, the page should be restored from BackForwardCache.
  ASSERT_TRUE(HistoryGoForward(web_contents()));
  EXPECT_EQ(rfh_b.get(), current_frame_host());
  ExpectRestored(FROM_HERE);

  ASSERT_TRUE(HistoryGoBack(web_contents()));
  EXPECT_EQ(rfh_a.get(), current_frame_host());
  ExpectRestored(FROM_HERE);
}

// If pages previously released the keyboard lock, but acquired it again, they
// cannot enter BackForwardCache.
IN_PROC_BROWSER_TEST_F(BackForwardCacheBrowserTest,
                       DoNotCacheIfKeyboardLockIsHeldAfterRelease) {
  ASSERT_TRUE(embedded_test_server()->Start());

  // 1) Navigate to a page and start using the Keyboard lock.
  GURL url(embedded_test_server()->GetURL("/title1.html"));
  EXPECT_TRUE(NavigateToURL(shell(), url));
  RenderFrameHostImplWrapper rfh_a(current_frame_host());
  RenderFrameDeletedObserver rfh_a_deleted(rfh_a.get());

  AcquireKeyboardLock(rfh_a.get());
  ReleaseKeyboardLock(rfh_a.get());
  AcquireKeyboardLock(rfh_a.get());

  // 2) Navigate away.
  EXPECT_TRUE(NavigateToURL(
      shell(), embedded_test_server()->GetURL("b.com", "/title1.html")));

  // The page uses keyboard lock so it should be deleted.
  rfh_a_deleted.WaitUntilDeleted();

  // 3) Go back and make sure the keyboard lock page wasn't in the cache.
  ASSERT_TRUE(HistoryGoBack(web_contents()));
  ExpectNotRestored(
      {NotRestoredReason::kBlocklistedFeatures},
      {blink::scheduler::WebSchedulerTrackedFeature::kKeyboardLock}, {}, {}, {},
      FROM_HERE);
}

// If pages released keyboard lock before navigation, they can enter
// BackForwardCache.
IN_PROC_BROWSER_TEST_F(BackForwardCacheBrowserTest,
                       CacheIfKeyboardLockReleased) {
  ASSERT_TRUE(embedded_test_server()->Start());

  // 1) Navigate to a page and start using the Keyboard lock.
  GURL url(embedded_test_server()->GetURL("/title1.html"));
  EXPECT_TRUE(NavigateToURL(shell(), url));
  RenderFrameHostImplWrapper rfh_a(current_frame_host());
  RenderFrameDeletedObserver rfh_a_deleted(rfh_a.get());

  AcquireKeyboardLock(rfh_a.get());
  ReleaseKeyboardLock(rfh_a.get());

  // 2) Navigate away.
  EXPECT_TRUE(NavigateToURL(
      shell(), embedded_test_server()->GetURL("b.com", "/title1.html")));

  // 3) Go back and page should be restored from BackForwardCache.
  ASSERT_TRUE(HistoryGoBack(web_contents()));
  ExpectRestored(FROM_HERE);
}

// If pages released keyboard lock during pagehide, they can enter
// BackForwardCache.
IN_PROC_BROWSER_TEST_F(BackForwardCacheBrowserTest,
                       CacheIfKeyboardLockReleasedInPagehide) {
  ASSERT_TRUE(embedded_test_server()->Start());

  // 1) Navigate to a page and start using the Keyboard lock.
  GURL url(embedded_test_server()->GetURL("/title1.html"));
  EXPECT_TRUE(NavigateToURL(shell(), url));
  RenderFrameHostImplWrapper rfh_a(current_frame_host());
  RenderFrameDeletedObserver rfh_a_deleted(rfh_a.get());

  AcquireKeyboardLock(rfh_a.get());
  // Register a pagehide handler to release keyboard lock.
  EXPECT_TRUE(ExecJs(rfh_a.get(), R"(
    window.onpagehide = function(e) {
      new Promise(resolve => {
      navigator.keyboard.unlock();
      resolve();
      });
    };
  )"));

  // 2) Navigate away.
  EXPECT_TRUE(NavigateToURL(
      shell(), embedded_test_server()->GetURL("b.com", "/title1.html")));

  // 3) Go back and page should be restored from BackForwardCache.
  ASSERT_TRUE(HistoryGoBack(web_contents()));
  ExpectRestored(FROM_HERE);
}

IN_PROC_BROWSER_TEST_F(BackForwardCacheBrowserTest,
                       DoesNotCacheWithDummyStickyFeature) {
  ASSERT_TRUE(embedded_test_server()->Start());

  // 1) Navigate to a page and start using the dummy sticky feature.
  GURL url(embedded_test_server()->GetURL("/title1.html"));
  EXPECT_TRUE(NavigateToURL(shell(), url));
  RenderFrameHostImplWrapper rfh_a(current_frame_host());
  RenderFrameDeletedObserver rfh_a_deleted(rfh_a.get());
  rfh_a->UseDummyStickyBackForwardCacheDisablingFeatureForTesting();

  // 2) Navigate away.
  EXPECT_TRUE(NavigateToURL(
      shell(), embedded_test_server()->GetURL("b.com", "/title1.html")));

  // The page uses the dummy sticky feature so it should be deleted.
  rfh_a_deleted.WaitUntilDeleted();

  // 3) Go back and make sure the dummy sticky feature page wasn't in the cache.
  ASSERT_TRUE(HistoryGoBack(web_contents()));
  ExpectNotRestored({NotRestoredReason::kBlocklistedFeatures},
                    {blink::scheduler::WebSchedulerTrackedFeature::kDummy}, {},
                    {}, {}, FROM_HERE);
}

// Tests which blocklisted features are tracked in the metrics when we used
// blocklisted features (sticky and non-sticky) and do a browser-initiated
// cross-site navigation.
IN_PROC_BROWSER_TEST_F(BackForwardCacheBrowserTest,
                       BlocklistedFeaturesTracking_CrossSite_BrowserInitiated) {
  ASSERT_TRUE(CreateHttpsServer()->Start());
  GURL url_a(https_server()->GetURL("a.test", "/title1.html"));
  GURL url_b(https_server()->GetURL("b.test", "/title2.html"));
  // 1) Navigate to a page.
  EXPECT_TRUE(NavigateToURL(shell(), url_a));
  EXPECT_TRUE(WaitForLoadStop(shell()->web_contents()));

  RenderFrameHostImpl* rfh_a = current_frame_host();
  scoped_refptr<SiteInstanceImpl> site_instance_a =
      static_cast<SiteInstanceImpl*>(rfh_a->GetSiteInstance());
  RenderFrameDeletedObserver rfh_a_deleted(rfh_a);

  // 2) Use BroadcastChannel (non-sticky) and a dummy sticky blocklisted
  // features.
  EXPECT_TRUE(ExecJs(rfh_a, "window.foo = new BroadcastChannel('foo');"));
  rfh_a->UseDummyStickyBackForwardCacheDisablingFeatureForTesting();

  // 3) Navigate cross-site, browser-initiated.
  EXPECT_TRUE(NavigateToURL(shell(), url_b));
  EXPECT_TRUE(WaitForLoadStop(shell()->web_contents()));

  // The previous page won't get into the back-forward cache because of the
  // blocklisted features. Because we used sticky blocklisted features, we will
  // not do a proactive BrowsingInstance swap, however the RFH will still change
  // and get deleted.
  rfh_a_deleted.WaitUntilDeleted();
  EXPECT_FALSE(site_instance_a->IsRelatedSiteInstance(
      web_contents()->GetPrimaryMainFrame()->GetSiteInstance()));

  // 4) Go back.
  ASSERT_TRUE(HistoryGoBack(web_contents()));

  // Only sticky features are recorded because they're tracked in
  // RenderFrameHostManager::UnloadOldFrame.
  ExpectNotRestored({NotRestoredReason::kBlocklistedFeatures},
                    {blink::scheduler::WebSchedulerTrackedFeature::kDummy}, {},
                    {}, {}, FROM_HERE);
}

// Tests which blocklisted features are tracked in the metrics when we used
// blocklisted features (sticky and non-sticky) and do a renderer-initiated
// cross-site navigation.
IN_PROC_BROWSER_TEST_F(
    BackForwardCacheBrowserTest,
    BlocklistedFeaturesTracking_CrossSite_RendererInitiated) {
  ASSERT_TRUE(CreateHttpsServer()->Start());
  GURL url_a(https_server()->GetURL("a.test", "/title1.html"));
  GURL url_b(https_server()->GetURL("b.test", "/title2.html"));

  // 1) Navigate to a page.
  EXPECT_TRUE(NavigateToURL(shell(), url_a));
  EXPECT_TRUE(WaitForLoadStop(shell()->web_contents()));

  RenderFrameHostImpl* rfh_a = current_frame_host();
  scoped_refptr<SiteInstanceImpl> site_instance_a =
      static_cast<SiteInstanceImpl*>(rfh_a->GetSiteInstance());

  // 2) Use BroadcastChannel (non-sticky) and Dummy sticky blocklisted
  // features.
  EXPECT_TRUE(ExecJs(rfh_a, "window.foo = new BroadcastChannel('foo');"));
  rfh_a->UseDummyStickyBackForwardCacheDisablingFeatureForTesting();

  // 3) Navigate cross-site, renderer-inititated.
  ASSERT_TRUE(NavigateToURLFromRenderer(shell(), url_b));
  // The previous page won't get into the back-forward cache because of the
  // blocklisted features. Because we used sticky blocklisted features, we will
  // not do a proactive BrowsingInstance swap.
  EXPECT_TRUE(site_instance_a->IsRelatedSiteInstance(
      web_contents()->GetPrimaryMainFrame()->GetSiteInstance()));

  // 4) Go back.
  ASSERT_TRUE(HistoryGoBack(web_contents()));

  if (AreStrictSiteInstancesEnabled()) {
    // Only sticky features are recorded because they're tracked in
    // RenderFrameHostManager::UnloadOldFrame.
    ExpectNotRestored(
        {NotRestoredReason::kRelatedActiveContentsExist,
         NotRestoredReason::kBlocklistedFeatures,
         NotRestoredReason::kBrowsingInstanceNotSwapped},
        {blink::scheduler::WebSchedulerTrackedFeature::kDummy},
        {ShouldSwapBrowsingInstance::kNo_NotNeededForBackForwardCache}, {}, {},
        FROM_HERE);

    ASSERT_TRUE(HistoryGoForward(web_contents()));

    ExpectBrowsingInstanceNotSwappedReason(
        ShouldSwapBrowsingInstance::kNo_AlreadyHasMatchingBrowsingInstance,
        FROM_HERE);

    ASSERT_TRUE(HistoryGoBack(web_contents()));

    ExpectBrowsingInstanceNotSwappedReason(
        ShouldSwapBrowsingInstance::kNo_AlreadyHasMatchingBrowsingInstance,
        FROM_HERE);
  } else {
    // Non-sticky reasons are not recorded here.
    ExpectNotRestored(
        {
            NotRestoredReason::kBlocklistedFeatures,
            NotRestoredReason::kBrowsingInstanceNotSwapped,
        },
        {blink::scheduler::WebSchedulerTrackedFeature::kDummy},
        {ShouldSwapBrowsingInstance::kNo_NotNeededForBackForwardCache}, {}, {},
        FROM_HERE);
  }
}

// Tests which blocklisted features are tracked in the metrics when we used
// blocklisted features (sticky and non-sticky) and do a same-site navigation.
IN_PROC_BROWSER_TEST_F(BackForwardCacheBrowserTest,
                       BlocklistedFeaturesTracking_SameSite) {
  ASSERT_TRUE(CreateHttpsServer()->Start());

  ASSERT_TRUE(CreateHttpsServer()->Start());
  GURL url_1(https_server()->GetURL("/title1.html"));
  GURL url_2(https_server()->GetURL("/title2.html"));

  // 1) Navigate to a page.
  EXPECT_TRUE(NavigateToURL(shell(), url_1));
  EXPECT_TRUE(WaitForLoadStop(shell()->web_contents()));

  RenderFrameHostImpl* rfh_1 = current_frame_host();
  scoped_refptr<SiteInstanceImpl> site_instance_1 =
      static_cast<SiteInstanceImpl*>(rfh_1->GetSiteInstance());
  rfh_1->GetBackForwardCacheMetrics()->SetObserverForTesting(this);

  // 2) Use BroadcastChannel (non-sticky) and dummy sticky blocklisted features.
  EXPECT_TRUE(ExecJs(rfh_1, "window.foo = new BroadcastChannel('foo');"));
  rfh_1->UseDummyStickyBackForwardCacheDisablingFeatureForTesting();

  // 3) Navigate same-site.
  EXPECT_TRUE(NavigateToURL(shell(), url_2));
  EXPECT_TRUE(WaitForLoadStop(shell()->web_contents()));

  // Because we used sticky blocklisted features, we will not do a proactive
  // BrowsingInstance swap.
  EXPECT_TRUE(site_instance_1->IsRelatedSiteInstance(
      web_contents()->GetPrimaryMainFrame()->GetSiteInstance()));

  // 4) Go back.
  ASSERT_TRUE(HistoryGoBack(web_contents()));

  // Non-sticky reasons are not recorded here.
  ExpectNotRestored(
      {
          NotRestoredReason::kBlocklistedFeatures,
          NotRestoredReason::kBrowsingInstanceNotSwapped,
      },
      {blink::scheduler::WebSchedulerTrackedFeature::kDummy},
      {ShouldSwapBrowsingInstance::kNo_NotNeededForBackForwardCache}, {}, {},
      FROM_HERE);
  // NotRestoredReason tree should match the flattened list.
  EXPECT_THAT(
      GetTreeResult()->GetDocumentResult(),
      MatchesDocumentResult(
          NotRestoredReasons(NotRestoredReason::kBlocklistedFeatures,
                             NotRestoredReason::kBrowsingInstanceNotSwapped),
          BlockListedFeatures(
              blink::scheduler::WebSchedulerTrackedFeature::kDummy)));
}

// Tests which blocklisted features are tracked in the metrics when we used a
// non-sticky blocklisted feature and do a browser-initiated cross-site
// navigation.
IN_PROC_BROWSER_TEST_F(
    BackForwardCacheBrowserTest,
    BlocklistedFeaturesTracking_CrossSite_BrowserInitiated_NonSticky) {
  ASSERT_TRUE(CreateHttpsServer()->Start());

  // 1) Navigate to an empty page.
  GURL url_a(https_server()->GetURL("a.test", "/title1.html"));
  GURL url_b(https_server()->GetURL("b.test", "/title2.html"));
  EXPECT_TRUE(NavigateToURL(shell(), url_a));
  EXPECT_TRUE(WaitForLoadStop(shell()->web_contents()));

  RenderFrameHostImpl* rfh_a = current_frame_host();
  // 2) Use BroadcastChannel (a non-sticky blocklisted feature).
  EXPECT_TRUE(ExecJs(rfh_a, "window.foo = new BroadcastChannel('foo');"));
  scoped_refptr<SiteInstanceImpl> site_instance_a =
      static_cast<SiteInstanceImpl*>(
          web_contents()->GetPrimaryMainFrame()->GetSiteInstance());

  // 3) Navigate cross-site, browser-initiated.
  // The previous page won't get into the back-forward cache because of the
  // blocklisted feature.
  EXPECT_TRUE(NavigateToURL(shell(), url_b));
  EXPECT_TRUE(WaitForLoadStop(shell()->web_contents()));
  // Because we only used non-sticky blocklisted features, we will still do a
  // proactive BrowsingInstance swap.
  EXPECT_FALSE(site_instance_a->IsRelatedSiteInstance(
      web_contents()->GetPrimaryMainFrame()->GetSiteInstance()));

  // 4) Go back.
  ASSERT_TRUE(HistoryGoBack(web_contents()));

  // Because the RenderFrameHostManager changed, the blocklisted features will
  // be tracked in RenderFrameHostManager::UnloadOldFrame.
  ExpectNotRestored(
      {NotRestoredReason::kBlocklistedFeatures},
      {blink::scheduler::WebSchedulerTrackedFeature::kBroadcastChannel}, {}, {},
      {}, FROM_HERE);
}

// Tests which blocklisted features are tracked in the metrics when we used a
// non-sticky blocklisted feature and do a renderer-initiated cross-site
// navigation.
IN_PROC_BROWSER_TEST_F(
    BackForwardCacheBrowserTest,
    BlocklistedFeaturesTracking_CrossSite_RendererInitiated_NonSticky) {
  ASSERT_TRUE(CreateHttpsServer()->Start());

  // 1) Navigate to an empty page.
  GURL url_a(https_server()->GetURL("a.test", "/title1.html"));
  GURL url_b(https_server()->GetURL("b.test", "/title1.html"));
  EXPECT_TRUE(NavigateToURL(shell(), url_a));
  EXPECT_TRUE(WaitForLoadStop(shell()->web_contents()));

  RenderFrameHostImpl* rfh_a = current_frame_host();
  // 2) Use BroadcastChannel (a non-sticky blocklisted feature).
  EXPECT_TRUE(ExecJs(rfh_a, "window.foo = new BroadcastChannel('foo');"));
  scoped_refptr<SiteInstanceImpl> site_instance_a =
      static_cast<SiteInstanceImpl*>(
          web_contents()->GetPrimaryMainFrame()->GetSiteInstance());

  // 3) Navigate cross-site, renderer-inititated.
  // The previous page won't get into the back-forward cache because of the
  // blocklisted feature.
  ASSERT_TRUE(NavigateToURLFromRenderer(shell(), url_b));
  // Because we only used non-sticky blocklisted features, we will still do a
  // proactive BrowsingInstance swap.
  EXPECT_FALSE(site_instance_a->IsRelatedSiteInstance(
      web_contents()->GetPrimaryMainFrame()->GetSiteInstance()));

  // 4) Go back.
  ASSERT_TRUE(HistoryGoBack(web_contents()));

  // Because the RenderFrameHostManager changed, the blocklisted features will
  // be tracked in RenderFrameHostManager::UnloadOldFrame.
  ExpectNotRestored(
      {NotRestoredReason::kBlocklistedFeatures},
      {blink::scheduler::WebSchedulerTrackedFeature::kBroadcastChannel}, {}, {},
      {}, FROM_HERE);
}

// Tests which blocklisted features are tracked in the metrics when we used a
// non-sticky blocklisted feature and do a same-site navigation.
IN_PROC_BROWSER_TEST_F(BackForwardCacheBrowserTest,
                       BlocklistedFeaturesTracking_SameSite_NonSticky) {
  ASSERT_TRUE(CreateHttpsServer()->Start());

  // 1) Navigate to an empty page.
  GURL url_1(https_server()->GetURL("/title1.html"));
  GURL url_2(https_server()->GetURL("/title2.html"));
  EXPECT_TRUE(NavigateToURL(shell(), url_1));
  EXPECT_TRUE(WaitForLoadStop(shell()->web_contents()));

  RenderFrameHostImpl* rfh_1 = current_frame_host();
  // 2) Use BroadcastChannel (a non-sticky blocklisted feature).
  EXPECT_TRUE(ExecJs(rfh_1, "window.foo = new BroadcastChannel('foo');"));
  scoped_refptr<SiteInstanceImpl> site_instance_1 =
      static_cast<SiteInstanceImpl*>(
          web_contents()->GetPrimaryMainFrame()->GetSiteInstance());

  // 3) Navigate same-site.
  // The previous page won't get into the back-forward cache because of the
  // blocklisted feature.
  EXPECT_TRUE(NavigateToURL(shell(), url_2));
  EXPECT_TRUE(WaitForLoadStop(shell()->web_contents()));
  // Because we only used non-sticky blocklisted features, we will still do a
  // proactive BrowsingInstance swap.
  EXPECT_FALSE(site_instance_1->IsRelatedSiteInstance(
      web_contents()->GetPrimaryMainFrame()->GetSiteInstance()));

  // 4) Go back.
  ASSERT_TRUE(HistoryGoBack(web_contents()));

  // Because the RenderFrameHostManager changed, the blocklisted features will
  // be tracked in RenderFrameHostManager::UnloadOldFrame.
  ExpectNotRestored(
      {NotRestoredReason::kBlocklistedFeatures},
      {blink::scheduler::WebSchedulerTrackedFeature::kBroadcastChannel}, {}, {},
      {}, FROM_HERE);
}

class MockAppBannerService : public blink::mojom::AppBannerService {
 public:
  MockAppBannerService() = default;

  MockAppBannerService(const MockAppBannerService&) = delete;
  MockAppBannerService& operator=(const MockAppBannerService&) = delete;

  ~MockAppBannerService() override = default;

  void Bind(mojo::ScopedMessagePipeHandle handle) {
    receiver_.Bind(mojo::PendingReceiver<blink::mojom::AppBannerService>(
        std::move(handle)));
  }

  mojo::Remote<blink::mojom::AppBannerController>& controller() {
    return controller_;
  }

  void OnBannerPromptRequested(bool) {}

  void SendBannerPromptRequest() {
    blink::mojom::AppBannerController* controller_ptr = controller_.get();
    base::OnceCallback<void(bool)> callback = base::BindOnce(
        &MockAppBannerService::OnBannerPromptRequested, base::Unretained(this));
    controller_ptr->BannerPromptRequest(
        receiver_.BindNewPipeAndPassRemote(),
        event_.BindNewPipeAndPassReceiver(), {"web"},
        base::BindOnce(&MockAppBannerService::OnBannerPromptReply,
                       base::Unretained(this), std::move(callback)));
  }

  void OnBannerPromptReply(base::OnceCallback<void(bool)> callback,
                           blink::mojom::AppBannerPromptReply reply) {
    std::move(callback).Run(reply ==
                            blink::mojom::AppBannerPromptReply::CANCEL);
  }

  // blink::mojom::AppBannerService:
  void DisplayAppBanner() override {}

 private:
  mojo::Receiver<blink::mojom::AppBannerService> receiver_{this};
  mojo::Remote<blink::mojom::AppBannerEvent> event_;
  mojo::Remote<blink::mojom::AppBannerController> controller_;
};

// The parameter to this test class is whether or not App Banner is supported
// for BFCache.
class AppBannerBackForwardCacheBrowserTest
    : public BackForwardCacheBrowserTest,
      public ::testing::WithParamInterface<bool> {
 public:
  AppBannerBackForwardCacheBrowserTest() {
    const base::Feature& feature = blink::features::kBackForwardCacheAppBanner;
    if (ShouldEnabledAppBannerCaching()) {
      EnableFeatureAndSetParams(feature, "", "");
    } else {
      DisableFeature(feature);
    }
  }

 protected:
  bool ShouldEnabledAppBannerCaching() { return GetParam(); }
};

// Disabled on Mac due to flakes; see https://crbug.com/1276864#c8.
#if BUILDFLAG(IS_MAC)
#define MAYBE_TestAppBannerCaching DISABLED_TestAppBannerCaching
#else
#define MAYBE_TestAppBannerCaching TestAppBannerCaching
#endif
IN_PROC_BROWSER_TEST_P(AppBannerBackForwardCacheBrowserTest,
                       MAYBE_TestAppBannerCaching) {
  ASSERT_TRUE(embedded_test_server()->Start());

  // 1) Navigate to A and request a PWA app banner.
  EXPECT_TRUE(NavigateToURL(
      shell(), embedded_test_server()->GetURL("a.com", "/title1.html")));

  // Connect the MockAppBannerService mojom to the renderer's frame.
  MockAppBannerService mock_app_banner_service;
  web_contents()->GetPrimaryMainFrame()->GetRemoteInterfaces()->GetInterface(
      mock_app_banner_service.controller().BindNewPipeAndPassReceiver());
  // Send the request to the renderer's frame.
  mock_app_banner_service.SendBannerPromptRequest();

  RenderFrameDeletedObserver delete_observer_rfh(current_frame_host());

  // 2) Navigate away. Page A requested a PWA app banner, and thus not cached.
  EXPECT_TRUE(NavigateToURL(
      shell(), embedded_test_server()->GetURL("b.com", "/title1.html")));
  if (!ShouldEnabledAppBannerCaching()) {
    ASSERT_TRUE(delete_observer_rfh.WaitUntilDeleted());
  }

  // 3) Go back to A.
  ASSERT_TRUE(HistoryGoBack(web_contents()));
  if (ShouldEnabledAppBannerCaching()) {
    ExpectRestored(FROM_HERE);
  } else {
    ExpectNotRestored(
        {NotRestoredReason::kBlocklistedFeatures},
        {blink::scheduler::WebSchedulerTrackedFeature::kAppBanner}, {}, {}, {},
        FROM_HERE);
  }
}

INSTANTIATE_TEST_SUITE_P(All,
                         AppBannerBackForwardCacheBrowserTest,
                         testing::Bool());

// TODO(crbug.com/1317431): WebSQL does not work on Fuchsia.
#if BUILDFLAG(IS_FUCHSIA)
#define MAYBE_DoesNotCacheIfWebDatabase DISABLED_DoesNotCacheIfWebDatabase
#else
#define MAYBE_DoesNotCacheIfWebDatabase DoesNotCacheIfWebDatabase
#endif
IN_PROC_BROWSER_TEST_F(BackForwardCacheBrowserTest,
                       MAYBE_DoesNotCacheIfWebDatabase) {
  ASSERT_TRUE(embedded_test_server()->Start());

  // 1) Navigate to a page with WebDatabase usage.
  GURL url(embedded_test_server()->GetURL("/simple_database.html"));
  EXPECT_TRUE(NavigateToURL(shell(), url));
  RenderFrameHostImpl* rfh_a = current_frame_host();
  RenderFrameDeletedObserver deleted(rfh_a);

  // 2) Navigate away.
  shell()->LoadURL(embedded_test_server()->GetURL("b.com", "/title1.html"));
  // The page uses WebDatabase so it should be deleted.
  deleted.WaitUntilDeleted();

  // 3) Go back to the page with WebDatabase.
  ASSERT_TRUE(HistoryGoBack(web_contents()));
  ExpectNotRestored(
      {NotRestoredReason::kBlocklistedFeatures},
      {blink::scheduler::WebSchedulerTrackedFeature::kWebDatabase}, {}, {}, {},
      FROM_HERE);
}

IN_PROC_BROWSER_TEST_F(BackForwardCacheBrowserTest,
                       DoesNotCacheIfOpenIndexedDBConnection) {
  ASSERT_TRUE(embedded_test_server()->Start());

  // 1) Navigate to A and use IndexedDB.
  EXPECT_TRUE(NavigateToURL(
      shell(), embedded_test_server()->GetURL(
                   "a.com", "/back_forward_cache/page_with_indexedDB.html")));
  RenderFrameHostImplWrapper rfh_a(current_frame_host());
  EXPECT_TRUE(ExecJs(rfh_a.get(), "setupIndexedDBConnection()"));
  RenderFrameDeletedObserver deleted(rfh_a.get());

  // 2) Navigate away.
  shell()->LoadURL(embedded_test_server()->GetURL("b.com", "/title1.html"));
  EXPECT_TRUE(NavigateToURL(
      shell(), embedded_test_server()->GetURL("b.com", "/title1.html")));
  // The page has an open IndexedDB connection so it should be deleted.
  deleted.WaitUntilDeleted();

  // 3) Go back to the page with IndexedDB.
  web_contents()->GetController().GoBack();
  EXPECT_TRUE(WaitForLoadStop(shell()->web_contents()));
  ExpectNotRestored(
      {NotRestoredReason::kBlocklistedFeatures},
      {blink::scheduler::WebSchedulerTrackedFeature::kIndexedDBConnection}, {},
      {}, {}, FROM_HERE);
}

IN_PROC_BROWSER_TEST_F(BackForwardCacheBrowserTest,
                       CacheIfIndexedDBConnectionClosedInPagehide) {
  ASSERT_TRUE(embedded_test_server()->Start());

  // 1) Navigate to A and use IndexedDB, and close the connection on pagehide.
  EXPECT_TRUE(NavigateToURL(
      shell(), embedded_test_server()->GetURL(
                   "a.com", "/back_forward_cache/page_with_indexedDB.html")));
  RenderFrameHostImplWrapper rfh_a(current_frame_host());
  EXPECT_TRUE(ExecJs(rfh_a.get(), "setupIndexedDBConnection()"));
  // This registers a pagehide handler to close the IDB connection. This should
  // remove the bfcache blocking.
  EXPECT_TRUE(
      ExecJs(rfh_a.get(), "registerPagehideToCloseIndexedDBConnection()"));

  // 2) Navigate away.
  EXPECT_TRUE(NavigateToURL(
      shell(), embedded_test_server()->GetURL("b.com", "/title1.html")));
  EXPECT_TRUE(rfh_a->IsInBackForwardCache());

  // 3) Go back to the page with IndexedDB. The connection is closed so it
  // should be restored from bfcache.
  web_contents()->GetController().GoBack();
  EXPECT_TRUE(WaitForLoadStop(shell()->web_contents()));
  ExpectRestored(FROM_HERE);
}

IN_PROC_BROWSER_TEST_F(BackForwardCacheBrowserTest,
                       DoNotCacheIfIndexedDBTransactionNotCommitted) {
  ASSERT_TRUE(embedded_test_server()->Start());

  // 1) Navigate to A and use IndexedDB.
  EXPECT_TRUE(NavigateToURL(
      shell(), embedded_test_server()->GetURL(
                   "a.com", "/back_forward_cache/page_with_indexedDB.html")));
  RenderFrameHostImplWrapper rfh_a(current_frame_host());
  EXPECT_TRUE(ExecJs(rfh_a.get(), "setupIndexedDBConnection()"));
  // This registers a pagehide handler to start a new transaction. This will
  // block bfcache because there is an inflight transaction.
  EXPECT_TRUE(ExecJs(rfh_a.get(), "registerPagehideToStartTransaction()"));

  // 2) Navigate away.
  EXPECT_TRUE(NavigateToURL(
      shell(), embedded_test_server()->GetURL("b.com", "/title1.html")));

  // 3) Go back to the page with IndexedDB.
  web_contents()->GetController().GoBack();
  EXPECT_TRUE(WaitForLoadStop(shell()->web_contents()));
  ExpectNotRestored({NotRestoredReason::kBlocklistedFeatures},
                    {blink::scheduler::WebSchedulerTrackedFeature::
                         kOutstandingIndexedDBTransaction},
                    {}, {}, {}, FROM_HERE);
}

IN_PROC_BROWSER_TEST_F(BackForwardCacheBrowserTest,
                       CacheIfIndexedDBConnectionTransactionCommit) {
  ASSERT_TRUE(embedded_test_server()->Start());

  // 1) Navigate to A and use IndexedDB.
  EXPECT_TRUE(NavigateToURL(
      shell(), embedded_test_server()->GetURL(
                   "a.com", "/back_forward_cache/page_with_indexedDB.html")));
  RenderFrameHostImplWrapper rfh_a(current_frame_host());
  EXPECT_TRUE(ExecJs(rfh_a.get(), "setupIndexedDBConnection()"));
  // This registers a pagehide handler to start and commit the IDB transactions.
  // Since the transactions are ended inside the handler, the page is no longer
  // blocked for inflight IDB transactions.
  EXPECT_TRUE(
      ExecJs(rfh_a.get(), "registerPagehideToStartAndCommitTransaction()"));

  // 2) Navigate away.
  EXPECT_TRUE(NavigateToURL(
      shell(), embedded_test_server()->GetURL("b.com", "/title1.html")));
  EXPECT_TRUE(rfh_a->IsInBackForwardCache());

  // 3) Go back to the page with IndexedDB.
  web_contents()->GetController().GoBack();
  EXPECT_TRUE(WaitForLoadStop(shell()->web_contents()));
  ExpectRestored(FROM_HERE);
}

IN_PROC_BROWSER_TEST_F(BackForwardCacheBrowserTest,
                       DoesNotCacheIfBroadcastChannelStillOpen) {
  ASSERT_TRUE(CreateHttpsServer()->Start());

  // 1) Navigate to an empty page.
  GURL url_a(https_server()->GetURL(
      "a.test", "/back_forward_cache/page_with_broadcastchannel.html"));
  GURL url_b(https_server()->GetURL("b.test", "/title1.html"));
  EXPECT_TRUE(NavigateToURL(shell(), url_a));
  EXPECT_TRUE(WaitForLoadStop(shell()->web_contents()));

  // 2) Use BroadcastChannel (a non-sticky blocklisted feature).
  RenderFrameHostImpl* rfh_a = current_frame_host();
  EXPECT_TRUE(ExecJs(rfh_a, "acquireBroadcastChannel();"));
  EXPECT_TRUE(ExecJs(rfh_a, "setShouldCloseChannelInPageHide(false);"));

  // 3) Navigate cross-site, browser-initiated.
  // The previous page won't get into the back-forward cache because of the
  // blocklisted feature.
  EXPECT_TRUE(NavigateToURL(shell(), url_b));
  EXPECT_TRUE(WaitForLoadStop(shell()->web_contents()));

  // 4) Go back.
  ASSERT_TRUE(HistoryGoBack(web_contents()));

  // Because the RenderFrameHostManager changed, the blocklisted features will
  // be tracked in RenderFrameHostManager::UnloadOldFrame.
  ExpectNotRestored(
      {NotRestoredReason::kBlocklistedFeatures},
      {blink::scheduler::WebSchedulerTrackedFeature::kBroadcastChannel}, {}, {},
      {}, FROM_HERE);
}

IN_PROC_BROWSER_TEST_F(BackForwardCacheBrowserTest,
                       CacheIfBroadcastChannelIsClosedInPagehide) {
  ASSERT_TRUE(CreateHttpsServer()->Start());

  // 1) Navigate to an empty page.
  GURL url_a(https_server()->GetURL(
      "a.test", "/back_forward_cache/page_with_broadcastchannel.html"));
  GURL url_b(https_server()->GetURL("b.test", "/title1.html"));
  EXPECT_TRUE(NavigateToURL(shell(), url_a));
  EXPECT_TRUE(WaitForLoadStop(shell()->web_contents()));

  RenderFrameHostImpl* rfh_a = current_frame_host();
  // 2) Use BroadcastChannel (a non-sticky blocklisted feature).
  EXPECT_TRUE(ExecJs(rfh_a, "acquireBroadcastChannel();"));
  EXPECT_TRUE(ExecJs(rfh_a, "setShouldCloseChannelInPageHide(true);"));

  // 3) Navigate cross-site, browser-initiated.
  // The previous page won't get into the back-forward cache because of the
  // blocklisted feature.
  EXPECT_TRUE(NavigateToURL(shell(), url_b));
  EXPECT_TRUE(WaitForLoadStop(shell()->web_contents()));

  // 4) Go back.
  ASSERT_TRUE(HistoryGoBack(web_contents()));
  ExpectRestored(FROM_HERE);
}

// Disabled on Android, since we have problems starting up the websocket test
// server in the host
#if BUILDFLAG(IS_ANDROID)
#define MAYBE_WebSocketCachedIfClosed DISABLED_WebSocketCachedIfClosed
#else
#define MAYBE_WebSocketCachedIfClosed WebSocketCachedIfClosed
#endif
// Pages with WebSocket should be cached if the connection is closed.
IN_PROC_BROWSER_TEST_F(BackForwardCacheBrowserTest,
                       MAYBE_WebSocketCachedIfClosed) {
  net::SpawnedTestServer ws_server(net::SpawnedTestServer::TYPE_WS,
                                   net::GetWebSocketTestDataDirectory());
  ASSERT_TRUE(ws_server.Start());

  ASSERT_TRUE(embedded_test_server()->Start());

  GURL url_a(embedded_test_server()->GetURL("a.com", "/title1.html"));
  GURL url_b(embedded_test_server()->GetURL("b.com", "/title1.html"));

  // 1) Navigate to A.
  ASSERT_TRUE(NavigateToURL(shell(), url_a));
  RenderFrameHostImplWrapper rfh_a(current_frame_host());

  // Open a WebSocket.
  const char script[] = R"(
      let socket;
      window.onpagehide = event => {
        socket.close();
      }
      new Promise(resolve => {
        socket = new WebSocket($1);
        socket.addEventListener('open', () => resolve());
      });)";
  ASSERT_TRUE(
      ExecJs(rfh_a.get(),
             JsReplace(script, ws_server.GetURL("echo-with-no-extension"))));

  // 2) Navigate to B.
  ASSERT_TRUE(NavigateToURL(shell(), url_b));
  EXPECT_TRUE(rfh_a->IsInBackForwardCache());

  // 3) Navigate back.
  ASSERT_TRUE(HistoryGoBack(web_contents()));
  ExpectRestored(FROM_HERE);
}

class WebTransportBackForwardCacheBrowserTest
    : public BackForwardCacheBrowserTest {
 public:
  WebTransportBackForwardCacheBrowserTest() { server_.Start(); }
  void SetUpCommandLine(base::CommandLine* command_line) override {
    BackForwardCacheBrowserTest::SetUpCommandLine(command_line);
    server_.SetUpCommandLine(command_line);
  }
  int port() const { return server_.server_address().port(); }

 private:
  WebTransportSimpleTestServer server_;
};

// Pages with active WebTransport should not be cached.
// TODO(yhirano): Update this test once
// https://github.com/w3c/webtransport/issues/326 is resolved.
IN_PROC_BROWSER_TEST_F(WebTransportBackForwardCacheBrowserTest,
                       ActiveWebTransportEvictsPage) {
  CreateHttpsServer();
  ASSERT_TRUE(https_server()->Start());

  GURL url_a(https_server()->GetURL("a.test", "/title1.html"));
  GURL url_b(https_server()->GetURL("b.test", "/title1.html"));

  // 1) Navigate to A.
  ASSERT_TRUE(NavigateToURL(shell(), url_a));
  RenderFrameHostImplWrapper rfh_a(current_frame_host());
  RenderFrameDeletedObserver delete_observer_rfh_a(rfh_a.get());

  // Establish a WebTransport session.
  const char script[] = R"(
      let transport = new WebTransport('https://localhost:$1/echo');
      )";
  ASSERT_TRUE(ExecJs(rfh_a.get(), JsReplace(script, port())));

  // 2) Navigate to B.
  ASSERT_TRUE(NavigateToURL(shell(), url_b));

  // Confirm A is evicted.
  delete_observer_rfh_a.WaitUntilDeleted();

  // 3) Go back.
  ASSERT_TRUE(HistoryGoBack(web_contents()));
  ExpectNotRestored(
      {NotRestoredReason::kBlocklistedFeatures},
      {blink::scheduler::WebSchedulerTrackedFeature::kWebTransport}, {}, {}, {},
      FROM_HERE);
}

// Pages with inactive WebTransport should be cached.
IN_PROC_BROWSER_TEST_F(WebTransportBackForwardCacheBrowserTest,
                       WebTransportCachedIfClosed) {
  CreateHttpsServer();
  ASSERT_TRUE(https_server()->Start());

  GURL url_a(https_server()->GetURL("a.test", "/title1.html"));
  GURL url_b(https_server()->GetURL("b.test", "/title1.html"));

  // 1) Navigate to A.
  ASSERT_TRUE(NavigateToURL(shell(), url_a));
  RenderFrameHostImplWrapper rfh_a(current_frame_host());

  // Establish a WebTransport session.
  const char script[] = R"(
      let transport;
      window.onpagehide = event => {
        transport.close();
      };
      transport = new WebTransport('https://localhost:$1/echo');
      )";
  ASSERT_TRUE(ExecJs(rfh_a.get(), JsReplace(script, port())));

  // 2) Navigate to B.
  ASSERT_TRUE(NavigateToURL(shell(), url_b));
  EXPECT_TRUE(rfh_a->IsInBackForwardCache());

  // 3) Navigate back.
  ASSERT_TRUE(HistoryGoBack(web_contents()));
  ExpectRestored(FROM_HERE);
}

// Disabled on Android, since we have problems starting up the websocket test
// server in the host
#if BUILDFLAG(IS_ANDROID)
#define MAYBE_WebSocketNotCached DISABLED_WebSocketNotCached
#else
#define MAYBE_WebSocketNotCached WebSocketNotCached
#endif
IN_PROC_BROWSER_TEST_F(BackForwardCacheBrowserTest, MAYBE_WebSocketNotCached) {
  net::SpawnedTestServer ws_server(net::SpawnedTestServer::TYPE_WS,
                                   net::GetWebSocketTestDataDirectory());
  ASSERT_TRUE(ws_server.Start());

  ASSERT_TRUE(embedded_test_server()->Start());

  GURL url_a(embedded_test_server()->GetURL("a.com", "/title1.html"));
  GURL url_b(embedded_test_server()->GetURL("b.com", "/title1.html"));

  // 1) Navigate to A.
  ASSERT_TRUE(NavigateToURL(shell(), url_a));
  RenderFrameHostImpl* rfh_a = current_frame_host();
  RenderFrameDeletedObserver delete_observer_rfh_a(rfh_a);

  // Open a WebSocket.
  const char script[] = R"(
      new Promise(resolve => {
        const socket = new WebSocket($1);
        socket.addEventListener('open', () => resolve());
      });)";
  ASSERT_TRUE(ExecJs(
      rfh_a, JsReplace(script, ws_server.GetURL("echo-with-no-extension"))));

  // 2) Navigate to B.
  ASSERT_TRUE(NavigateToURL(shell(), url_b));

  // Confirm A is evicted.
  delete_observer_rfh_a.WaitUntilDeleted();
}

namespace {

void RegisterServiceWorker(RenderFrameHostImpl* rfh) {
  EXPECT_EQ("success", EvalJs(rfh, R"(
    let controller_changed_promise = new Promise(resolve_controller_change => {
      navigator.serviceWorker.oncontrollerchange = resolve_controller_change;
    });

    new Promise(async resolve => {
      try {
        await navigator.serviceWorker.register(
          "./service-worker.js", {scope: "./"})
      } catch (e) {
        resolve("error: registration has failed");
      }

      await controller_changed_promise;

      if (navigator.serviceWorker.controller) {
        resolve("success");
      } else {
        resolve("error: not controlled by service worker");
      }
    });
  )"));
}

// Returns a unique script for each request, to test service worker update.
std::unique_ptr<net::test_server::HttpResponse> RequestHandlerForUpdateWorker(
    const net::test_server::HttpRequest& request) {
  if (request.relative_url != "/back_forward_cache/service-worker.js")
    return nullptr;
  static int counter = 0;
  auto http_response = std::make_unique<net::test_server::BasicHttpResponse>();
  http_response->set_code(net::HTTP_OK);
  const char script[] = R"(
    // counter = $1
    self.addEventListener('activate', function(event) {
      event.waitUntil(self.clients.claim());
    });
  )";
  http_response->set_content(JsReplace(script, counter++));
  http_response->set_content_type("text/javascript");
  http_response->AddCustomHeader("Cache-Control",
                                 "no-cache, no-store, must-revalidate");
  return http_response;
}

}  // namespace

class TestVibrationManager : public device::mojom::VibrationManager {
 public:
  TestVibrationManager() {
    OverrideVibrationManagerBinderForTesting(base::BindRepeating(
        &TestVibrationManager::BindVibrationManager, base::Unretained(this)));
  }

  ~TestVibrationManager() override {
    OverrideVibrationManagerBinderForTesting(base::NullCallback());
  }

  void BindVibrationManager(
      mojo::PendingReceiver<device::mojom::VibrationManager> receiver) {
    receiver_.Bind(std::move(receiver));
  }

  bool TriggerVibrate(RenderFrameHostImpl* rfh, int duration) {
    return EvalJs(rfh, JsReplace("navigator.vibrate($1)", duration))
        .ExtractBool();
  }

  bool TriggerShortVibrationSequence(RenderFrameHostImpl* rfh) {
    return EvalJs(rfh, "navigator.vibrate([10] * 1000)").ExtractBool();
  }

  bool WaitForCancel() {
    run_loop_.Run();
    return IsCancelled();
  }

  bool IsCancelled() { return cancelled_; }

 private:
  // device::mojom::VibrationManager:
  void Vibrate(int64_t milliseconds, VibrateCallback callback) override {
    cancelled_ = false;
    std::move(callback).Run();
  }

  void Cancel(CancelCallback callback) override {
    cancelled_ = true;
    std::move(callback).Run();
    run_loop_.Quit();
  }

  bool cancelled_ = false;
  base::RunLoop run_loop_;
  mojo::Receiver<device::mojom::VibrationManager> receiver_{this};
};

// Tests that vibration stops after the page enters bfcache.
IN_PROC_BROWSER_TEST_F(BackForwardCacheBrowserTest,
                       VibrationStopsAfterEnteringCache) {
  ASSERT_TRUE(embedded_test_server()->Start());
  TestVibrationManager vibration_manager;

  // 1) Navigate to a page with a long vibration.
  GURL url(embedded_test_server()->GetURL("a.com", "/title1.html"));
  EXPECT_TRUE(NavigateToURL(shell(), url));
  RenderFrameHostImpl* rfh_a = current_frame_host();
  ASSERT_TRUE(vibration_manager.TriggerVibrate(rfh_a, 10000));
  EXPECT_FALSE(vibration_manager.IsCancelled());

  // 2) Navigate away and expect the vibration to be canceled.
  EXPECT_TRUE(NavigateToURL(
      shell(), embedded_test_server()->GetURL("b.com", "/title1.html")));
  EXPECT_NE(current_frame_host(), rfh_a);
  EXPECT_TRUE(rfh_a->IsInBackForwardCache());
  EXPECT_TRUE(vibration_manager.WaitForCancel());

  // 3) Go back to A.
  ASSERT_TRUE(HistoryGoBack(web_contents()));
  ExpectRestored(FROM_HERE);
}

// Tests that the short vibration sequence on the page stops after it enters
// bfcache.
IN_PROC_BROWSER_TEST_F(BackForwardCacheBrowserTest,
                       ShortVibrationSequenceStopsAfterEnteringCache) {
  ASSERT_TRUE(embedded_test_server()->Start());
  TestVibrationManager vibration_manager;

  // 1) Navigate to a page with a long vibration.
  GURL url(embedded_test_server()->GetURL("a.com", "/title1.html"));
  EXPECT_TRUE(NavigateToURL(shell(), url));
  RenderFrameHostImpl* rfh_a = current_frame_host();
  ASSERT_TRUE(vibration_manager.TriggerShortVibrationSequence(rfh_a));
  EXPECT_FALSE(vibration_manager.IsCancelled());

  // 2) Navigate away and expect the vibration to be canceled.
  EXPECT_TRUE(NavigateToURL(
      shell(), embedded_test_server()->GetURL("b.com", "/title1.html")));
  EXPECT_NE(current_frame_host(), rfh_a);
  EXPECT_TRUE(rfh_a->IsInBackForwardCache());
  EXPECT_TRUE(vibration_manager.WaitForCancel());

  // 3) Go back to A.
  ASSERT_TRUE(HistoryGoBack(web_contents()));
  ExpectRestored(FROM_HERE);
}

IN_PROC_BROWSER_TEST_F(BackForwardCacheBrowserTest,
                       CachedPagesWithServiceWorkers) {
  CreateHttpsServer();
  SetupCrossSiteRedirector(https_server());
  ASSERT_TRUE(https_server()->Start());

  // 1) Navigate to A.
  EXPECT_TRUE(NavigateToURL(
      shell(),
      https_server()->GetURL("a.test", "/back_forward_cache/empty.html")));

  // Register a service worker.
  RegisterServiceWorker(current_frame_host());

  RenderFrameHostImpl* rfh_a = current_frame_host();
  RenderFrameDeletedObserver deleted(rfh_a);

  // 2) Navigate away.
  EXPECT_TRUE(
      NavigateToURL(shell(), https_server()->GetURL("b.test", "/title1.html")));

  EXPECT_FALSE(deleted.deleted());
  EXPECT_TRUE(rfh_a->IsInBackForwardCache());

  // 3) Go back to A. The navigation should be served from the cache.
  ASSERT_TRUE(HistoryGoBack(web_contents()));
  EXPECT_FALSE(deleted.deleted());
  EXPECT_EQ(rfh_a, current_frame_host());
}

IN_PROC_BROWSER_TEST_F(BackForwardCacheBrowserTest,
                       EvictIfCacheBlocksServiceWorkerVersionActivation) {
  CreateHttpsServer();
  https_server()->RegisterRequestHandler(
      base::BindRepeating(&RequestHandlerForUpdateWorker));
  SetupCrossSiteRedirector(https_server());
  ASSERT_TRUE(https_server()->Start());
  Shell* tab_x = shell();
  Shell* tab_y = CreateBrowser();
  // 1) Navigate to A in tab X.
  EXPECT_TRUE(NavigateToURL(
      tab_x,
      https_server()->GetURL("a.test", "/back_forward_cache/empty.html")));
  // 2) Register a service worker.
  RegisterServiceWorker(current_frame_host());

  RenderFrameHostImpl* rfh_a = current_frame_host();
  RenderFrameDeletedObserver deleted(rfh_a);
  // 3) Navigate away to B in tab X.
  EXPECT_TRUE(
      NavigateToURL(tab_x, https_server()->GetURL("b.test", "/title1.html")));
  EXPECT_FALSE(deleted.deleted());
  EXPECT_TRUE(rfh_a->IsInBackForwardCache());
  // 4) Navigate to A in tab Y.
  EXPECT_TRUE(NavigateToURL(
      tab_y,
      https_server()->GetURL("a.test", "/back_forward_cache/empty.html")));
  // 5) Close tab Y to activate a service worker version.
  // This should evict |rfh_a| from the cache.
  tab_y->Close();
  deleted.WaitUntilDeleted();
  // 6) Navigate to A in tab X.
  ASSERT_TRUE(HistoryGoBack(tab_x->web_contents()));
  ExpectNotRestored(
      {
          NotRestoredReason::kServiceWorkerVersionActivation,
      },
      {}, {}, {}, {}, FROM_HERE);
}

IN_PROC_BROWSER_TEST_F(BackForwardCacheBrowserTest,
                       EvictWithPostMessageToCachedClient) {
  net::EmbeddedTestServer https_server(net::EmbeddedTestServer::TYPE_HTTPS);
  https_server.RegisterRequestHandler(
      base::BindRepeating(&RequestHandlerForUpdateWorker));
  https_server.AddDefaultHandlers(GetTestDataFilePath());
  https_server.SetSSLConfig(net::EmbeddedTestServer::CERT_TEST_NAMES);
  SetupCrossSiteRedirector(&https_server);
  ASSERT_TRUE(https_server.Start());
  Shell* tab_to_execute_service_worker = shell();
  Shell* tab_to_be_bfcached = CreateBrowser();

  // Observe the new WebContents to trace the navigtion ID.
  WebContentsObserver::Observe(tab_to_be_bfcached->web_contents());

  // 1) Navigate to A in |tab_to_execute_service_worker|.
  EXPECT_TRUE(NavigateToURL(
      tab_to_execute_service_worker,
      https_server.GetURL(
          "a.test", "/back_forward_cache/service_worker_post_message.html")));

  // 2) Register a service worker.
  EXPECT_EQ("DONE", EvalJs(tab_to_execute_service_worker,
                           "register('service_worker_post_message.js')"));

  // 3) Navigate to A in |tab_to_be_bfcached|.
  EXPECT_TRUE(NavigateToURL(
      tab_to_be_bfcached,
      https_server.GetURL(
          "a.test", "/back_forward_cache/service_worker_post_message.html")));
  const std::string script_to_store =
      "executeCommandOnServiceWorker('StoreClients')";
  EXPECT_EQ("DONE", EvalJs(tab_to_execute_service_worker, script_to_store));
  RenderFrameHostImplWrapper rfh(
      tab_to_be_bfcached->web_contents()->GetPrimaryMainFrame());

  // 4) Navigate away to B in |tab_to_be_bfcached|.
  EXPECT_TRUE(NavigateToURL(tab_to_be_bfcached,
                            https_server.GetURL("b.test", "/title1.html")));
  EXPECT_FALSE(rfh.IsDestroyed());
  EXPECT_TRUE(rfh->IsInBackForwardCache());

  // 5) Trigger client.postMessage via |tab_to_execute_service_worker|. Cache in
  // |tab_to_be_bfcached| will be evicted.
  const std::string script_to_post_message =
      "executeCommandOnServiceWorker('PostMessageToStoredClients')";
  EXPECT_EQ("DONE",
            EvalJs(tab_to_execute_service_worker, script_to_post_message));
  ASSERT_TRUE(rfh.WaitUntilRenderFrameDeleted());

  // 6) Go back to A in |tab_to_be_bfcached|.
  ASSERT_TRUE(HistoryGoBack(tab_to_be_bfcached->web_contents()));
  ExpectNotRestored({NotRestoredReason::kServiceWorkerPostMessage}, {}, {}, {},
                    {}, FROM_HERE);
}

IN_PROC_BROWSER_TEST_F(BackForwardCacheBrowserTest, EvictOnServiceWorkerClaim) {
  net::EmbeddedTestServer https_server(net::EmbeddedTestServer::TYPE_HTTPS);
  https_server.RegisterRequestHandler(
      base::BindRepeating(&RequestHandlerForUpdateWorker));
  https_server.AddDefaultHandlers(GetTestDataFilePath());
  https_server.SetSSLConfig(net::EmbeddedTestServer::CERT_TEST_NAMES);
  SetupCrossSiteRedirector(&https_server);
  ASSERT_TRUE(https_server.Start());

  Shell* tab_to_be_bfcached = shell();
  Shell* tab_to_execute_service_worker = CreateBrowser();

  // 1) Navigate to A in |tab_to_be_bfcached|.
  EXPECT_TRUE(NavigateToURL(
      tab_to_be_bfcached,
      https_server.GetURL(
          "a.test", "/back_forward_cache/service_worker_registration.html")));
  RenderFrameHostImpl* rfh_a = current_frame_host();
  RenderFrameDeletedObserver deleted(rfh_a);

  // 2) Navigate away to B in |tab_to_be_bfcached|.
  EXPECT_TRUE(NavigateToURL(tab_to_be_bfcached,
                            https_server.GetURL("b.test", "/title1.html")));
  EXPECT_FALSE(deleted.deleted());
  EXPECT_TRUE(rfh_a->IsInBackForwardCache());

  // 3) Navigate to A in |tab_to_execute_service_worker|.
  EXPECT_TRUE(NavigateToURL(
      tab_to_execute_service_worker,
      https_server.GetURL(
          "a.test", "/back_forward_cache/service_worker_registration.html")));

  // 4) Register a service worker for |tab_to_execute_service_worker|.
  EXPECT_EQ("DONE", EvalJs(tab_to_execute_service_worker,
                           "register('service_worker_registration.js')"));

  // 5) The service worker calls clients.claim(). |rfh_a| would normally be
  //    claimed but because it's in bfcache, it is evicted from the cache.
  EXPECT_EQ("DONE", EvalJs(tab_to_execute_service_worker, "claim()"));

  // 6) Navigate to A in |tab_to_be_bfcached|.
  ASSERT_TRUE(HistoryGoBack(tab_to_be_bfcached->web_contents()));
  EXPECT_TRUE(WaitForLoadStop(shell()->web_contents()));
  EXPECT_TRUE(deleted.deleted());
  ExpectNotRestored({NotRestoredReason::kServiceWorkerClaim}, {}, {}, {}, {},
                    FROM_HERE);
}

IN_PROC_BROWSER_TEST_F(BackForwardCacheBrowserTest,
                       EvictOnServiceWorkerUnregistration) {
  net::EmbeddedTestServer https_server(net::EmbeddedTestServer::TYPE_HTTPS);
  https_server.RegisterRequestHandler(
      base::BindRepeating(&RequestHandlerForUpdateWorker));
  https_server.AddDefaultHandlers(GetTestDataFilePath());
  https_server.SetSSLConfig(net::EmbeddedTestServer::CERT_TEST_NAMES);
  SetupCrossSiteRedirector(&https_server);
  ASSERT_TRUE(https_server.Start());

  Shell* tab_to_be_bfcached = shell();
  Shell* tab_to_unregister_service_worker = CreateBrowser();

  // 1) Navigate to A in |tab_to_be_bfcached|. This tab will be controlled by a
  // service worker.
  EXPECT_TRUE(NavigateToURL(
      tab_to_be_bfcached,
      https_server.GetURL("a.test",
                          "/back_forward_cache/"
                          "service_worker_registration.html?to_be_bfcached")));

  // 2) Register a service worker for |tab_to_be_bfcached|, but with a narrow
  // scope with URL param. This is to prevent |tab_to_unregister_service_worker|
  // from being controlled by the service worker.
  EXPECT_EQ("DONE",
            EvalJs(tab_to_be_bfcached,
                   "register('service_worker_registration.js', "
                   "'service_worker_registration.html?to_be_bfcached')"));
  EXPECT_EQ("DONE", EvalJs(tab_to_be_bfcached, "claim()"));
  RenderFrameHostImpl* rfh_a = current_frame_host();
  RenderFrameDeletedObserver deleted(rfh_a);

  // 3) Navigate to A in |tab_to_unregister_service_worker|. This tab is not
  // controlled by the service worker.
  EXPECT_TRUE(NavigateToURL(
      tab_to_unregister_service_worker,
      https_server.GetURL(
          "a.test", "/back_forward_cache/service_worker_registration.html")));

  // 5) Navigate from A to B in |tab_to_be_bfcached|. Now |tab_to_be_bfcached|
  // should be in bfcache.
  EXPECT_TRUE(NavigateToURL(tab_to_be_bfcached,
                            https_server.GetURL("b.test", "/title1.html")));
  EXPECT_FALSE(deleted.deleted());
  EXPECT_TRUE(rfh_a->IsInBackForwardCache());

  // 6) The service worker gets unregistered. Now |tab_to_be_bfcached| should be
  // notified of the unregistration and evicted from bfcache.
  EXPECT_EQ(
      "DONE",
      EvalJs(tab_to_unregister_service_worker,
             "unregister('service_worker_registration.html?to_be_bfcached')"));

  // 7) Navigate back to A in |tab_to_be_bfcached|.
  ASSERT_TRUE(HistoryGoBack(tab_to_be_bfcached->web_contents()));
  EXPECT_TRUE(WaitForLoadStop(shell()->web_contents()));
  EXPECT_TRUE(deleted.deleted());
  ExpectNotRestored({NotRestoredReason::kServiceWorkerUnregistration}, {}, {},
                    {}, {}, FROM_HERE);
}

IN_PROC_BROWSER_TEST_F(BackForwardCacheBrowserTest, CachePagesWithBeacon) {
  constexpr char kKeepalivePath[] = "/keepalive";

  net::test_server::ControllableHttpResponse keepalive(embedded_test_server(),
                                                       kKeepalivePath);
  ASSERT_TRUE(embedded_test_server()->Start());

  GURL url_a(embedded_test_server()->GetURL("a.com", "/title1.html"));
  GURL url_ping(embedded_test_server()->GetURL("a.com", kKeepalivePath));

  // 1) Navigate to A.
  EXPECT_TRUE(NavigateToURL(shell(), url_a));
  RenderFrameHostImpl* rfh_a = current_frame_host();
  RenderFrameDeletedObserver delete_observer_rfh_a(rfh_a);

  EXPECT_TRUE(
      ExecJs(shell(), JsReplace(R"(navigator.sendBeacon($1, "");)", url_ping)));

  // 2) Navigate to B.
  GURL url_b(embedded_test_server()->GetURL("b.com", "/title1.html"));
  EXPECT_TRUE(NavigateToURL(shell(), url_b));

  // Ensure that the keepalive request is sent.
  keepalive.WaitForRequest();
  // Don't actually send the response.

  // Page A should be in the cache.
  EXPECT_FALSE(delete_observer_rfh_a.deleted());
  EXPECT_TRUE(rfh_a->IsInBackForwardCache());
}

class GeolocationBackForwardCacheBrowserTest
    : public BackForwardCacheBrowserTest {
 protected:
  GeolocationBackForwardCacheBrowserTest() : geo_override_(0.0, 0.0) {}

  device::ScopedGeolocationOverrider geo_override_;
};

// Test that a page which has queried geolocation in the past, but have no
// active geolocation query, can be bfcached.
IN_PROC_BROWSER_TEST_F(GeolocationBackForwardCacheBrowserTest,
                       CacheAfterGeolocationRequest) {
  ASSERT_TRUE(embedded_test_server()->Start());
  GURL url_a(embedded_test_server()->GetURL("/title1.html"));
  GURL url_b(embedded_test_server()->GetURL("b.com", "/title1.html"));

  // 1) Navigate to A.
  EXPECT_TRUE(NavigateToURL(shell(), url_a));
  RenderFrameHostImpl* rfh_a = current_frame_host();

  // Query current position, and wait for the query to complete.
  EXPECT_EQ("received", EvalJs(rfh_a, R"(
      new Promise(resolve => {
        navigator.geolocation.getCurrentPosition(() => resolve('received'));
      });
  )"));

  RenderFrameDeletedObserver deleted(rfh_a);

  // 2) Navigate away.
  EXPECT_TRUE(NavigateToURL(shell(), url_b));

  // The page has no inflight geolocation request when we navigated away,
  // so it should have been cached.
  EXPECT_FALSE(deleted.deleted());
  EXPECT_TRUE(rfh_a->IsInBackForwardCache());
}

// Test that a page which has an inflight geolocation query can be bfcached,
// and verify that the page does not observe any geolocation while the page
// was inside bfcache.
// The test is flaky on multiple platforms: crbug.com/1033270
IN_PROC_BROWSER_TEST_F(GeolocationBackForwardCacheBrowserTest,
                       DISABLED_CancelGeolocationRequestInFlight) {
  ASSERT_TRUE(embedded_test_server()->Start());
  GURL url_a(embedded_test_server()->GetURL("/title1.html"));
  GURL url_b(embedded_test_server()->GetURL("b.com", "/title1.html"));

  // 1) Navigate to A.
  EXPECT_TRUE(NavigateToURL(shell(), url_a));
  RenderFrameHostImpl* rfh_a = current_frame_host();

  // Continuously query current geolocation.
  EXPECT_TRUE(ExecJs(rfh_a, R"(
    window.longitude_log = [];
    window.err_log = [];
    window.wait_for_first_position = new Promise(resolve => {
      navigator.geolocation.watchPosition(
        pos => {
          window.longitude_log.push(pos.coords.longitude);
          resolve("resolved");
        },
        err => window.err_log.push(err)
      );
    })
  )"));
  geo_override_.UpdateLocation(0.0, 0.0);
  EXPECT_EQ("resolved", EvalJs(rfh_a, "window.wait_for_first_position"));

  // Pause resolving Geoposition queries to keep the request inflight.
  geo_override_.Pause();
  geo_override_.UpdateLocation(1.0, 1.0);
  EXPECT_EQ(1u, geo_override_.GetGeolocationInstanceCount());

  // 2) Navigate away.
  base::RunLoop loop_until_close;
  geo_override_.SetGeolocationCloseCallback(loop_until_close.QuitClosure());

  RenderFrameDeletedObserver deleted(rfh_a);
  EXPECT_TRUE(NavigateToURL(shell(), url_b));

  loop_until_close.Run();

  // The page has no inflight geolocation request when we navigated away,
  // so it should have been cached.
  EXPECT_FALSE(deleted.deleted());
  EXPECT_TRUE(rfh_a->IsInBackForwardCache());

  // Resume resolving Geoposition queries.
  geo_override_.Resume();

  // We update the location while the page is BFCached, but this location should
  // not be observed.
  geo_override_.UpdateLocation(2.0, 2.0);

  // 3) Navigate back to A.

  // The location when navigated back can be observed
  geo_override_.UpdateLocation(3.0, 3.0);

  ASSERT_TRUE(HistoryGoBack(web_contents()));
  EXPECT_EQ(rfh_a, current_frame_host());
  EXPECT_FALSE(rfh_a->IsInBackForwardCache());

  // Wait for an update after the user navigates back to A.
  EXPECT_EQ("resolved", EvalJs(rfh_a, R"(
    window.wait_for_position_after_resume = new Promise(resolve => {
      navigator.geolocation.watchPosition(
        pos => {
          window.longitude_log.push(pos.coords.longitude);
          resolve("resolved");
        },
        err => window.err_log.push(err)
      );
    })
  )"));

  EXPECT_LE(0, EvalJs(rfh_a, "longitude_log.indexOf(0.0)").ExtractInt())
      << "Geoposition before the page is put into BFCache should be visible";
  EXPECT_EQ(-1, EvalJs(rfh_a, "longitude_log.indexOf(1.0)").ExtractInt())
      << "Geoposition while the page is put into BFCache should be invisible";
  EXPECT_EQ(-1, EvalJs(rfh_a, "longitude_log.indexOf(2.0)").ExtractInt())
      << "Geoposition while the page is put into BFCache should be invisible";
  EXPECT_LT(0, EvalJs(rfh_a, "longitude_log.indexOf(3.0)").ExtractInt())
      << "Geoposition when the page is restored from BFCache should be visible";
  EXPECT_EQ(0, EvalJs(rfh_a, "err_log.length"))
      << "watchPosition API should have reported no errors";
}

class BluetoothForwardCacheBrowserTest : public BackForwardCacheBrowserTest {
 protected:
  BluetoothForwardCacheBrowserTest() = default;

  ~BluetoothForwardCacheBrowserTest() override = default;

  void SetUp() override {
    // Fake the BluetoothAdapter to say it's present.
    // Used in WebBluetooth test.
    adapter_ =
        base::MakeRefCounted<testing::NiceMock<device::MockBluetoothAdapter>>();
    device::BluetoothAdapterFactory::SetAdapterForTesting(adapter_);
#if BUILDFLAG(IS_CHROMEOS_ASH)
    // In CHROMEOS build, even when |adapter_| object is released at TearDown()
    // it causes the test to fail on exit with an error indicating |adapter_| is
    // leaked.
    testing::Mock::AllowLeak(adapter_.get());
#endif

    BackForwardCacheBrowserTest::SetUp();
  }

  void TearDown() override {
    testing::Mock::VerifyAndClearExpectations(adapter_.get());
    adapter_.reset();
    BackForwardCacheBrowserTest::TearDown();
  }

  scoped_refptr<device::MockBluetoothAdapter> adapter_;
};

IN_PROC_BROWSER_TEST_F(BluetoothForwardCacheBrowserTest, WebBluetooth) {
  // The test requires a mock Bluetooth adapter to perform a
  // WebBluetooth API call. To avoid conflicts with the default Bluetooth
  // adapter, e.g. Windows adapter, which is configured during Bluetooth
  // initialization, the mock adapter is configured in SetUp().

  // WebBluetooth requires HTTPS.
  ASSERT_TRUE(CreateHttpsServer()->Start());
  GURL url(https_server()->GetURL("a.test", "/back_forward_cache/empty.html"));

  ASSERT_TRUE(NavigateToURL(web_contents(), url));
  BackForwardCacheDisabledTester tester;

  EXPECT_EQ("device not found", EvalJs(current_frame_host(), R"(
    new Promise(resolve => {
      navigator.bluetooth.requestDevice({
        filters: [
          { services: [0x1802, 0x1803] },
        ]
      })
      .then(() => resolve("device found"))
      .catch(() => resolve("device not found"))
    });
  )"));
  auto reason = BackForwardCacheDisable::DisabledReason(
      BackForwardCacheDisable::DisabledReasonId::kWebBluetooth);
  EXPECT_TRUE(tester.IsDisabledForFrameWithReason(
      current_frame_host()->GetProcess()->GetID(),
      current_frame_host()->GetRoutingID(), reason));

  ASSERT_TRUE(NavigateToURL(web_contents(),
                            https_server()->GetURL("b.test", "/title1.html")));
  ASSERT_TRUE(HistoryGoBack(web_contents()));
  ExpectNotRestored({NotRestoredReason::kDisableForRenderFrameHostCalled}, {},
                    {}, {reason}, {}, FROM_HERE);
}

enum class SerialContext {
  kDocument,
  kWorker,
  kNestedWorker,
};

enum class SerialType {
  kSerial,
  kWebUsb,
};

class BackForwardCacheBrowserWebUsbTest
    : public BackForwardCacheBrowserTest,
      public ::testing::WithParamInterface<
          std::tuple<SerialContext, SerialType>> {
 public:
  std::string GetJsToUseSerial(SerialContext context, SerialType serial_type) {
    switch (serial_type) {
      case SerialType::kSerial:
        switch (context) {
          case SerialContext::kDocument:
            return R"(
              new Promise(async resolve => {
                let ports = await navigator.serial.getPorts();
                resolve("Found " + ports.length + " ports");
              });
            )";
          case SerialContext::kWorker:
            return R"(
              new Promise(async resolve => {
                const worker = new Worker(
                    "/back_forward_cache/serial/worker.js");
                worker.onmessage = message => resolve(message.data);
                worker.postMessage("Run");
              });
            )";
          case SerialContext::kNestedWorker:
            return R"(
              new Promise(async resolve => {
                const worker = new Worker(
                  "/back_forward_cache/serial/nested-worker.js");
                worker.onmessage = message => resolve(message.data);
                worker.postMessage("Run");
              });
            )";
        }
      case SerialType::kWebUsb:
        switch (context) {
          case SerialContext::kDocument:
            return R"(
              new Promise(async resolve => {
                let devices = await navigator.usb.getDevices();
                resolve("Found " + devices.length + " devices");
              });
            )";
          case SerialContext::kWorker:
            return R"(
              new Promise(async resolve => {
                const worker = new Worker(
                    "/back_forward_cache/webusb/worker.js");
                worker.onmessage = message => resolve(message.data);
                worker.postMessage("Run");
              });
            )";
          case SerialContext::kNestedWorker:
            return R"(
              new Promise(async resolve => {
                const worker = new Worker(
                  "/back_forward_cache/webusb/nested-worker.js");
                worker.onmessage = message => resolve(message.data);
                worker.postMessage("Run");
              });
            )";
        }
    }
  }
};

// Check the BackForwardCache is disabled when the WebUSB feature is used.
// TODO(https://crbug.com/1339720): Consider testing in a subframe. This will
// require adjustments to Permissions Policy.
IN_PROC_BROWSER_TEST_P(BackForwardCacheBrowserWebUsbTest, Serials) {
  // WebUSB requires HTTPS.
  ASSERT_TRUE(CreateHttpsServer()->Start());

  SerialContext context;
  SerialType serial_type;
  std::tie(context, serial_type) = GetParam();

  content::BackForwardCacheDisabledTester tester;
  GURL url(https_server()->GetURL(
      "a.test", "/cross_site_iframe_factory.html?a.test(a.test)"));

  ASSERT_TRUE(NavigateToURL(shell(), url));

  // Check that the frames we care about are cacheable.
  RenderFrameHostImplWrapper main_rfh(current_frame_host());
  RenderFrameHostImplWrapper sub_rfh(
      current_frame_host()->child_at(0)->current_frame_host());
  ASSERT_FALSE(main_rfh->IsBackForwardCacheDisabled());
  ASSERT_FALSE(sub_rfh->IsBackForwardCacheDisabled());

  // Execute script to use WebUSB.
  ASSERT_EQ(
      serial_type == SerialType::kSerial ? "Found 0 ports" : "Found 0 devices",
      content::EvalJs(main_rfh.get(), GetJsToUseSerial(context, serial_type)));

  // Verify that the correct frames are now uncacheable.
  EXPECT_TRUE(main_rfh->IsBackForwardCacheDisabled());
  EXPECT_FALSE(sub_rfh->IsBackForwardCacheDisabled());
  auto expected_reason =
      serial_type == SerialType::kSerial
          ? BackForwardCacheDisable::DisabledReasonId::kSerial
          : BackForwardCacheDisable::DisabledReasonId::kWebUSB;
  EXPECT_TRUE(tester.IsDisabledForFrameWithReason(
      main_rfh->GetProcess()->GetID(), main_rfh->GetRoutingID(),
      BackForwardCacheDisable::DisabledReason(expected_reason)));
}

INSTANTIATE_TEST_SUITE_P(
    All,
    BackForwardCacheBrowserWebUsbTest,
    testing::Combine(testing::Values(SerialContext::kDocument,
                                     SerialContext::kWorker,
                                     SerialContext::kNestedWorker),
                     testing::Values(SerialType::kWebUsb
#if !BUILDFLAG(IS_ANDROID)
                                     ,
                                     SerialType::kSerial
#endif  // !BUILDFLAG(IS_ANDROID)
                                     )));

// Check that an audio suspends when the page goes to the cache and can resume
// after restored.
IN_PROC_BROWSER_TEST_F(BackForwardCacheBrowserTest, AudioSuspendAndResume) {
  ASSERT_TRUE(embedded_test_server()->Start());
  GURL url_a(embedded_test_server()->GetURL("a.com", "/title1.html"));
  GURL url_b(embedded_test_server()->GetURL("b.com", "/title1.html"));

  // 1) Navigate to A.
  EXPECT_TRUE(NavigateToURL(shell(), url_a));
  RenderFrameHostImpl* rfh_a = current_frame_host();
  EXPECT_TRUE(ExecJs(rfh_a, R"(
    var audio = document.createElement('audio');
    document.body.appendChild(audio);

    audio.testObserverEvents = [];
    let event_list = [
      'canplaythrough',
      'pause',
      'play',
      'error',
    ];
    for (event_name of event_list) {
      let result = event_name;
      audio.addEventListener(event_name, event => {
        document.title = result;
        audio.testObserverEvents.push(result);
      });
    }

    audio.src = 'media/bear-opus.ogg';

    var timeOnFrozen = 0.0;
    audio.addEventListener('pause', () => {
      timeOnFrozen = audio.currentTime;
    });
  )"));

  // Load the media.
  {
    TitleWatcher title_watcher(shell()->web_contents(), u"canplaythrough");
    title_watcher.AlsoWaitForTitle(u"error");
    EXPECT_EQ(u"canplaythrough", title_watcher.WaitAndGetTitle());
  }

  EXPECT_TRUE(ExecJs(rfh_a, R"(
    new Promise(async resolve => {
      audio.play();
      while (audio.currentTime === 0)
        await new Promise(r => setTimeout(r, 1));
      resolve();
    });
  )"));

  // 2) Navigate to B.
  EXPECT_TRUE(NavigateToURL(shell(), url_b));
  EXPECT_TRUE(rfh_a->IsInBackForwardCache());

  // 3) Navigate back to A.
  ASSERT_TRUE(HistoryGoBack(web_contents()));
  EXPECT_EQ(rfh_a, current_frame_host());

  // Check that the media position is not changed when the page is in cache.
  double duration1 = EvalJs(rfh_a, "timeOnFrozen;").ExtractDouble();
  double duration2 = EvalJs(rfh_a, "audio.currentTime;").ExtractDouble();
  EXPECT_LE(0.0, duration2 - duration1);
  EXPECT_GT(0.01, duration2 - duration1);

  // Resume the media.
  EXPECT_TRUE(ExecJs(rfh_a, "audio.play();"));

  // Confirm that the media pauses automatically when going to the cache.
  // TODO(hajimehoshi): Confirm that this media automatically resumes if
  // autoplay attribute exists.
  EXPECT_EQ(ListValueOf("canplaythrough", "play", "pause", "play"),
            EvalJs(rfh_a, "audio.testObserverEvents"));
}

// Check that a video suspends when the page goes to the cache and can resume
// after restored.
IN_PROC_BROWSER_TEST_F(BackForwardCacheBrowserTest, VideoSuspendAndResume) {
  ASSERT_TRUE(embedded_test_server()->Start());
  GURL url_a(embedded_test_server()->GetURL("a.com", "/title1.html"));
  GURL url_b(embedded_test_server()->GetURL("b.com", "/title1.html"));

  // Navigate to A.
  EXPECT_TRUE(NavigateToURL(shell(), url_a));
  RenderFrameHostImpl* rfh_a = current_frame_host();
  EXPECT_TRUE(ExecJs(rfh_a, R"(
    var video = document.createElement('video');
    document.body.appendChild(video);

    video.testObserverEvents = [];
    let event_list = [
      'canplaythrough',
      'pause',
      'play',
      'error',
    ];
    for (event_name of event_list) {
      let result = event_name;
      video.addEventListener(event_name, event => {
        document.title = result;
        // Ignore 'canplaythrough' event as we can randomly get extra
        // 'canplaythrough' events after playing here.
        if (result != 'canplaythrough')
          video.testObserverEvents.push(result);
      });
    }

    video.src = 'media/bear.webm';

    // Android bots can be very slow and the video is only 1s long.
    // This gives the first part of the test time to run before reaching
    // the end of the video.
    video.playbackRate = 0.1;

    var timeOnPagehide;
    window.addEventListener('pagehide', () => {
      timeOnPagehide = video.currentTime;
    });
    var timeOnPageshow;
    window.addEventListener('pageshow', () => {
      timeOnPageshow = video.currentTime;
    });
  )"));

  // Load the media.
  {
    TitleWatcher title_watcher(shell()->web_contents(), u"canplaythrough");
    title_watcher.AlsoWaitForTitle(u"error");
    EXPECT_EQ(u"canplaythrough", title_watcher.WaitAndGetTitle());
  }

  EXPECT_TRUE(ExecJs(rfh_a, R"(
    new Promise(async resolve => {
      video.play();
      while (video.currentTime == 0)
        await new Promise(r => setTimeout(r, 1));
      resolve();
    });
  )"));

  // Navigate to B.
  EXPECT_TRUE(NavigateToURL(shell(), url_b));
  EXPECT_TRUE(rfh_a->IsInBackForwardCache());

  // Sleep for 1s so that playing in BFCache can be detected.
  base::PlatformThread::Sleep(base::Seconds(1));

  // Navigate back to A.
  ASSERT_TRUE(HistoryGoBack(web_contents()));
  EXPECT_EQ(rfh_a, current_frame_host());

  const double timeOnPagehide =
      EvalJs(rfh_a, "timeOnPagehide;").ExtractDouble();
  const double timeOnPageshow = EvalJs(rfh_a, "timeOnPageshow").ExtractDouble();

  // Make sure the video did not reach the end. If it did, our test is not
  // reliable.
  ASSERT_GT(1.0, timeOnPageshow);

  // Check that the duration of video played between pagehide and pageshow is
  // small. We waited for 1s so if it didn't stop in BFCache, it should be much
  // longer than this.
  const double playedDuration = timeOnPageshow - timeOnPagehide;
  EXPECT_LE(0.0, playedDuration);
  EXPECT_GT(0.02, playedDuration);

  // Resume the media.
  EXPECT_TRUE(ExecJs(rfh_a, R"(
      // Ensure that the video does not auto-pause when it completes as that
      // would add an unexpected pause event.
      video.loop = true;
      video.play();
    )"));

  // Confirm that the media pauses automatically when going to the cache.
  // TODO(hajimehoshi): Confirm that this media automatically resumes if
  // autoplay attribute exists.
  EXPECT_EQ(ListValueOf("play", "pause", "play"),
            EvalJs(rfh_a, "video.testObserverEvents"));
}

class SensorBackForwardCacheBrowserTest : public BackForwardCacheBrowserTest {
 protected:
  SensorBackForwardCacheBrowserTest() {
    SensorProviderProxyImpl::OverrideSensorProviderBinderForTesting(
        base::BindRepeating(
            &SensorBackForwardCacheBrowserTest::BindSensorProvider,
            base::Unretained(this)));
  }

  ~SensorBackForwardCacheBrowserTest() override {
    SensorProviderProxyImpl::OverrideSensorProviderBinderForTesting(
        base::NullCallback());
  }

  void SetUpOnMainThread() override {
    provider_ = std::make_unique<device::FakeSensorProvider>();
    provider_->SetAccelerometerData(1.0, 2.0, 3.0);

    BackForwardCacheBrowserTest::SetUpOnMainThread();
  }

  std::unique_ptr<device::FakeSensorProvider> provider_;

 private:
  void BindSensorProvider(
      mojo::PendingReceiver<device::mojom::SensorProvider> receiver) {
    provider_->Bind(std::move(receiver));
  }
};

IN_PROC_BROWSER_TEST_F(SensorBackForwardCacheBrowserTest,
                       AccelerometerNotCached) {
  ASSERT_TRUE(embedded_test_server()->Start());
  GURL url_a(embedded_test_server()->GetURL("/title1.html"));
  GURL url_b(embedded_test_server()->GetURL("b.com", "/title1.html"));

  // 1) Navigate to A.
  ASSERT_TRUE(NavigateToURL(shell(), url_a));
  RenderFrameHostImpl* rfh_a = current_frame_host();
  RenderFrameDeletedObserver delete_observer_rfh_a(rfh_a);

  EXPECT_TRUE(ExecJs(rfh_a, R"(
    new Promise(resolve => {
      const sensor = new Accelerometer();
      sensor.addEventListener('reading', () => { resolve(); });
      sensor.start();
    })
  )"));

  // 2) Navigate to B.
  ASSERT_TRUE(NavigateToURL(shell(), url_b));

  // - Page A should not be in the cache.
  delete_observer_rfh_a.WaitUntilDeleted();

  // 3) Go back.
  ASSERT_TRUE(HistoryGoBack(web_contents()));
  ExpectNotRestored({NotRestoredReason::kBlocklistedFeatures},
                    {blink::scheduler::WebSchedulerTrackedFeature::
                         kRequestedBackForwardCacheBlockedSensors},
                    {}, {}, {}, FROM_HERE);
}

IN_PROC_BROWSER_TEST_F(SensorBackForwardCacheBrowserTest, OrientationCached) {
  ASSERT_TRUE(embedded_test_server()->Start());
  GURL url_a(embedded_test_server()->GetURL("/title1.html"));
  GURL url_b(embedded_test_server()->GetURL("b.com", "/title1.html"));

  // 1) Navigate to A.
  ASSERT_TRUE(NavigateToURL(shell(), url_a));
  RenderFrameHostImpl* rfh_a = current_frame_host();
  RenderFrameDeletedObserver delete_observer_rfh_a(rfh_a);

  EXPECT_TRUE(ExecJs(rfh_a, R"(
    window.addEventListener("deviceorientation", () => {});
  )"));

  // 2) Navigate to B.
  ASSERT_TRUE(NavigateToURL(shell(), url_b));

  EXPECT_FALSE(delete_observer_rfh_a.deleted());
  EXPECT_THAT(rfh_a, InBackForwardCache());
}

// Tests that the orientation sensor's events are not delivered to a page in the
// back-forward cache.
//
// This sets some JS functions in the pages to enable the sensors, capture and
// validate the events. The a-page should only receive events with alpha=0, the
// b-page is allowed to receive any alpha value. The test captures 3 events in
// the a-page, then navigates to the b-page and changes the reading to have
// alpha=1. While on the b-page it captures 3 more events. If the a-page is
// still receiving events it should receive one or more of these. Finally it
// resets the reasing back to have alpha=0 and navigates back to the a-page and
// captures 3 more events and verifies that all events on the a-page have
// alpha=1.
IN_PROC_BROWSER_TEST_F(SensorBackForwardCacheBrowserTest,
                       SensorPausedWhileCached) {
  ASSERT_TRUE(CreateHttpsServer()->Start());
  GURL url_a(https_server()->GetURL("a.test", "/title1.html"));
  GURL url_b(https_server()->GetURL("b.test", "/title1.html"));

  provider_->SetRelativeOrientationSensorData(0, 0, 0);

  // JS to cause a page to listen to, capture and validate orientation events.
  const std::string sensor_js = R"(
    // Collects events that have happened so far.
    var events = [];
    // If set, will be called by handleEvent.
    var pendingResolve = null;

    // Handles one event, pushing it to |events| and calling |pendingResolve| if
    // set.
    function handleEvent(event) {
      events.push(event);
      if (pendingResolve !== null) {
        pendingResolve('event');
        pendingResolve = null;
      }
    }

    // Returns a promise that will resolve when the events array has at least
    // |eventCountMin| elements. Returns the number of elements.
    function waitForEventsPromise(eventCountMin) {
      if (events.length >= eventCountMin) {
        return Promise.resolve(events.length);
      }
      return new Promise(resolve => {
        pendingResolve = resolve;
      }).then(() => waitForEventsPromise(eventCountMin));
    }

    // Pretty print an orientation event.
    function eventToString(event) {
      return `${event.alpha} ${event.beta} ${event.gamma}`;
    }

    // Ensure that that |expectedAlpha| matches the alpha of all events.
    function validateEvents(expectedAlpha = null) {
      if (expectedAlpha !== null) {
        let count = 0;
        for (event of events) {
          count++;
          if (Math.abs(event.alpha - expectedAlpha) > 0.01) {
            return `fail - ${count}/${events.length}: ` +
                `${expectedAlpha} != ${event.alpha} (${eventToString(event)})`;
          }
        }
      }
      return 'pass';
    }

    window.addEventListener('deviceorientation', handleEvent);
  )";

  // 1) Navigate to A.
  ASSERT_TRUE(NavigateToURL(shell(), url_a));
  ASSERT_TRUE(WaitForLoadStop(shell()->web_contents()));
  RenderFrameHostImpl* rfh_a = current_frame_host();
  RenderFrameDeletedObserver delete_observer_rfh_a(rfh_a);

  ASSERT_TRUE(ExecJs(rfh_a, sensor_js));

  // Collect 3 orientation events.
  ASSERT_EQ(1, EvalJs(rfh_a, "waitForEventsPromise(1)"));
  provider_->UpdateRelativeOrientationSensorData(0, 0, 0.2);
  ASSERT_EQ(2, EvalJs(rfh_a, "waitForEventsPromise(2)"));
  provider_->UpdateRelativeOrientationSensorData(0, 0, 0.4);
  ASSERT_EQ(3, EvalJs(rfh_a, "waitForEventsPromise(3)"));
  // We should have 3 events with alpha=0.
  ASSERT_EQ("pass", EvalJs(rfh_a, "validateEvents(0)"));

  // 2) Navigate to B.
  ASSERT_TRUE(NavigateToURL(shell(), url_b));
  ASSERT_TRUE(WaitForLoadStop(shell()->web_contents()));
  RenderFrameHostImpl* rfh_b = current_frame_host();

  ASSERT_FALSE(delete_observer_rfh_a.deleted());
  ASSERT_THAT(rfh_a, InBackForwardCache());
  ASSERT_NE(rfh_a, rfh_b);

  ASSERT_TRUE(ExecJs(rfh_b, sensor_js));

  // Collect 3 orientation events.
  provider_->SetRelativeOrientationSensorData(1, 0, 0);
  ASSERT_EQ(1, EvalJs(rfh_b, "waitForEventsPromise(1)"));
  provider_->UpdateRelativeOrientationSensorData(1, 0, 0.2);
  ASSERT_EQ(2, EvalJs(rfh_b, "waitForEventsPromise(2)"));
  provider_->UpdateRelativeOrientationSensorData(1, 0, 0.4);
  ASSERT_EQ(3, EvalJs(rfh_b, "waitForEventsPromise(3)"));
  // We should have 3 events with alpha=1.
  ASSERT_EQ("pass", EvalJs(rfh_b, "validateEvents()"));

  // 3) Go back to A.
  provider_->UpdateRelativeOrientationSensorData(0, 0, 0);
  ASSERT_TRUE(HistoryGoBack(web_contents()));
  ASSERT_EQ(rfh_a, current_frame_host());

  // Collect 3 orientation events.
  provider_->UpdateRelativeOrientationSensorData(0, 0, 0);
  // There are 2 processes so, it's possible that more events crept in. So we
  // capture how many there are at this point and uses to wait for at least 3
  // more.
  int count = EvalJs(rfh_a, "waitForEventsPromise(4)").ExtractInt();
  provider_->UpdateRelativeOrientationSensorData(0, 0, 0.2);
  count++;
  ASSERT_EQ(count, EvalJs(rfh_a, base::StringPrintf("waitForEventsPromise(%d)",
                                                    count)));
  provider_->UpdateRelativeOrientationSensorData(0, 0, 0.4);
  count++;
  ASSERT_EQ(count, EvalJs(rfh_a, base::StringPrintf("waitForEventsPromise(%d)",
                                                    count)));

  // We should have the earlier 3 plus another 3 events with alpha=0.
  ASSERT_EQ("pass", EvalJs(rfh_a, "validateEvents(0)"));
}

// This tests that even if a page initializes WebRTC, tha page can be cached as
// long as it doesn't make a connection.
// On the Android test environments, the test might fail due to IP restrictions.
// See the discussion at http://crrev.com/c/2564926.
#if !BUILDFLAG(IS_ANDROID)

// TODO(https://crbug.com/1213145): The test is consistently failing on some Mac
// bots.
#if BUILDFLAG(IS_MAC)
#define MAYBE_TrivialRTCPeerConnectionCached \
  DISABLED_TrivialRTCPeerConnectionCached
#else
#define MAYBE_TrivialRTCPeerConnectionCached TrivialRTCPeerConnectionCached
#endif
IN_PROC_BROWSER_TEST_F(BackForwardCacheBrowserTest,
                       MAYBE_TrivialRTCPeerConnectionCached) {
  ASSERT_TRUE(CreateHttpsServer()->Start());

  GURL url_a(https_server()->GetURL("/title1.html"));
  GURL url_b(https_server()->GetURL("b.test", "/title1.html"));

  // 1) Navigate to A.
  ASSERT_TRUE(NavigateToURL(shell(), url_a));
  RenderFrameHostImpl* rfh_a = current_frame_host();

  // Create an RTCPeerConnection without starting a connection.
  EXPECT_TRUE(ExecJs(rfh_a, "const pc1 = new RTCPeerConnection()"));

  // 2) Navigate to B.
  ASSERT_TRUE(NavigateToURL(shell(), url_b));

  // 3) Go back.
  ASSERT_TRUE(HistoryGoBack(web_contents()));
  ExpectRestored(FROM_HERE);

  // RTCPeerConnection object, that is created before being put into the cache,
  // is still available.
  EXPECT_EQ("success", EvalJs(rfh_a, R"(
    new Promise(async resolve => {
      const pc1 = new RTCPeerConnection();
      const pc2 = new RTCPeerConnection();
      pc1.onicecandidate = e => {
        if (e.candidate)
          pc2.addIceCandidate(e.candidate);
      }
      pc2.onicecandidate = e => {
        if (e.candidate)
          pc1.addIceCandidate(e.candidate);
      }
      pc1.addTransceiver("audio");
      const connectionEstablished = new Promise((resolve, reject) => {
        pc1.oniceconnectionstatechange = () => {
          const state = pc1.iceConnectionState;
          switch (state) {
          case "connected":
          case "completed":
            resolve();
            break;
          case "failed":
          case "disconnected":
          case "closed":
            reject(state);
            break;
          }
        }
      });
      await pc1.setLocalDescription();
      await pc2.setRemoteDescription(pc1.localDescription);
      await pc2.setLocalDescription();
      await pc1.setRemoteDescription(pc2.localDescription);
      try {
        await connectionEstablished;
      } catch (e) {
        resolve("fail " + e);
        return;
      }
      resolve("success");
    });
  )"));
}
#endif  // !BUILDFLAG(IS_ANDROID)

// This tests that a page using WebRTC and creating actual connections cannot be
// cached.
// On the Android test environments, the test might fail due to IP restrictions.
// See the discussion at http://crrev.com/c/2564926.
#if !BUILDFLAG(IS_ANDROID)

// TODO(https://crbug.com/1213145): The test is consistently failing on some Mac
// bots.
#if BUILDFLAG(IS_MAC)
#define MAYBE_NonTrivialRTCPeerConnectionNotCached \
  DISABLED_NonTrivialRTCPeerConnectionNotCached
#else
#define MAYBE_NonTrivialRTCPeerConnectionNotCached \
  NonTrivialRTCPeerConnectionNotCached
#endif
IN_PROC_BROWSER_TEST_F(BackForwardCacheBrowserTest,
                       MAYBE_NonTrivialRTCPeerConnectionNotCached) {
  ASSERT_TRUE(CreateHttpsServer()->Start());

  GURL url_a(https_server()->GetURL("/title1.html"));
  GURL url_b(https_server()->GetURL("b.test", "/title1.html"));

  // 1) Navigate to A.
  ASSERT_TRUE(NavigateToURL(shell(), url_a));
  RenderFrameHostImpl* rfh_a = current_frame_host();
  RenderFrameDeletedObserver delete_observer_rfh_a(rfh_a);

  // Create an RTCPeerConnection with starting a connection.
  EXPECT_EQ("success", EvalJs(rfh_a, R"(
    new Promise(async resolve => {
      const pc1 = new RTCPeerConnection();
      const pc2 = new RTCPeerConnection();
      pc1.onicecandidate = e => {
        if (e.candidate)
          pc2.addIceCandidate(e.candidate);
      }
      pc2.onicecandidate = e => {
        if (e.candidate)
          pc1.addIceCandidate(e.candidate);
      }
      pc1.addTransceiver("audio");
      const connectionEstablished = new Promise(resolve => {
        pc1.oniceconnectionstatechange = () => {
          const state = pc1.iceConnectionState;
          switch (state) {
          case "connected":
          case "completed":
            resolve();
            break;
          case "failed":
          case "disconnected":
          case "closed":
            reject(state);
            break;
          }
        }
      });
      await pc1.setLocalDescription();
      await pc2.setRemoteDescription(pc1.localDescription);
      await pc2.setLocalDescription();
      await pc1.setRemoteDescription(pc2.localDescription);
      await connectionEstablished;
      try {
        await connectionEstablished;
      } catch (e) {
        resolve("fail " + e);
        return;
      }
      resolve("success");
    });
  )"));

  // 2) Navigate to B.
  ASSERT_TRUE(NavigateToURL(shell(), url_b));

  // - Page A should not be in the cache.
  delete_observer_rfh_a.WaitUntilDeleted();

  // 3) Go back.
  ASSERT_TRUE(HistoryGoBack(web_contents()));
  ExpectNotRestored({NotRestoredReason::kBlocklistedFeatures},
                    {blink::scheduler::WebSchedulerTrackedFeature::kWebRTC}, {},
                    {}, {}, FROM_HERE);
}
#endif  // !BUILDFLAG(IS_ANDROID)

IN_PROC_BROWSER_TEST_F(BackForwardCacheBrowserTest, WebLocksNotCached) {
  ASSERT_TRUE(embedded_test_server()->Start());
  GURL url_a(embedded_test_server()->GetURL("/title1.html"));
  GURL url_b(embedded_test_server()->GetURL("b.com", "/title1.html"));

  // 1) Navigate to A.
  ASSERT_TRUE(NavigateToURL(shell(), url_a));
  RenderFrameHostImpl* rfh_a = current_frame_host();
  RenderFrameDeletedObserver delete_observer_rfh_a(rfh_a);

  // Wait for the page to acquire a lock and ensure that it continues to do so.
  EXPECT_TRUE(ExecJs(rfh_a, R"(
    const never_resolved = new Promise(resolve => {});
    new Promise(continue_test => {
      navigator.locks.request('test', async () => {
        continue_test();
        await never_resolved;
      });
    })
  )"));

  // 2) Navigate to B.
  ASSERT_TRUE(NavigateToURL(shell(), url_b));

  // - Page A should not be in the cache.
  delete_observer_rfh_a.WaitUntilDeleted();

  // 3) Go back.
  ASSERT_TRUE(HistoryGoBack(web_contents()));
  ExpectNotRestored({NotRestoredReason::kBlocklistedFeatures},
                    {blink::scheduler::WebSchedulerTrackedFeature::kWebLocks},
                    {}, {}, {}, FROM_HERE);
}

IN_PROC_BROWSER_TEST_F(BackForwardCacheBrowserTest, WebMidiNotCached) {
  ASSERT_TRUE(embedded_test_server()->Start());
  GURL url_a(embedded_test_server()->GetURL("/title1.html"));
  GURL url_b(embedded_test_server()->GetURL("b.com", "/title1.html"));

  // 1) Navigate to A.
  ASSERT_TRUE(NavigateToURL(shell(), url_a));
  RenderFrameHostImpl* rfh_a = current_frame_host();
  RenderFrameDeletedObserver delete_observer_rfh_a(rfh_a);

  // Request access to MIDI. This should prevent the page from entering the
  // BackForwardCache.
  EXPECT_TRUE(ExecJs(rfh_a, "navigator.requestMIDIAccess()",
                     EXECUTE_SCRIPT_NO_RESOLVE_PROMISES));

  // 2) Navigate to B.
  ASSERT_TRUE(NavigateToURL(shell(), url_b));

  // - Page A should not be in the cache.
  delete_observer_rfh_a.WaitUntilDeleted();

  // 3) Go back.
  ASSERT_TRUE(HistoryGoBack(web_contents()));
  ExpectNotRestored(
      {NotRestoredReason::kBlocklistedFeatures},
      {blink::scheduler::WebSchedulerTrackedFeature::kRequestedMIDIPermission},
      {}, {}, {}, FROM_HERE);
}

// TODO(https://crbug.com/1286474): This test is flaking on some Android bots.
// TODO(crbug.com/1297406): Also flaky on Mac and Linux.
#if BUILDFLAG(IS_ANDROID) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)
#define MAYBE_PresentationConnectionClosed DISABLED_PresentationConnectionClosed
#else
#define MAYBE_PresentationConnectionClosed PresentationConnectionClosed
#endif  // BUILDFLAG(IS_ANDROID)
IN_PROC_BROWSER_TEST_F(BackForwardCacheBrowserTest,
                       MAYBE_PresentationConnectionClosed) {
  ASSERT_TRUE(CreateHttpsServer()->Start());
  GURL url_a(https_server()->GetURL(
      "a.test", "/back_forward_cache/presentation_controller.html"));

  // Navigate to A (presentation controller page).
  ASSERT_TRUE(NavigateToURL(shell(), url_a));
  auto* rfh_a = current_frame_host();
  // Start a presentation connection in A.
  MockPresentationServiceDelegate mock_presentation_service_delegate;
  auto& presentation_service = rfh_a->GetPresentationServiceForTesting();
  presentation_service.SetControllerDelegateForTesting(
      &mock_presentation_service_delegate);
  EXPECT_CALL(mock_presentation_service_delegate, StartPresentation(_, _, _));
  EXPECT_TRUE(ExecJs(rfh_a, "presentationRequest.start().then(setConnection)",
                     EXECUTE_SCRIPT_NO_RESOLVE_PROMISES));

  // Send a mock connection to the renderer.
  MockPresentationConnection mock_controller_connection;
  mojo::Receiver<PresentationConnection> controller_connection_receiver(
      &mock_controller_connection);
  mojo::Remote<PresentationConnection> receiver_connection;
  const std::string presentation_connection_id = "foo";
  presentation_service.OnStartPresentationSucceeded(
      presentation_service.start_presentation_request_id_,
      PresentationConnectionResult::New(
          blink::mojom::PresentationInfo::New(GURL("fake-url"),
                                              presentation_connection_id),
          controller_connection_receiver.BindNewPipeAndPassRemote(),
          receiver_connection.BindNewPipeAndPassReceiver()));

  // Navigate to B, make sure that the connection started in A is closed.
  GURL url_b(https_server()->GetURL("b.test", "/title1.html"));
  EXPECT_CALL(
      mock_controller_connection,
      DidClose(blink::mojom::PresentationConnectionCloseReason::WENT_AWAY));
  RenderFrameDeletedObserver delete_observer_rfh_a(rfh_a);
  ASSERT_TRUE(NavigateToURL(shell(), url_b));
  EXPECT_FALSE(delete_observer_rfh_a.deleted());
  EXPECT_TRUE(rfh_a->IsInBackForwardCache());

  // Navigate back to A. Ensure that connection state has been updated
  // accordingly.
  ASSERT_TRUE(HistoryGoBack(web_contents()));
  EXPECT_FALSE(rfh_a->IsInBackForwardCache());
  EXPECT_EQ(presentation_connection_id, EvalJs(rfh_a, "connection.id"));
  EXPECT_EQ("closed", EvalJs(rfh_a, "connection.state"));
  EXPECT_TRUE(EvalJs(rfh_a, "connectionClosed").ExtractBool());

  // Try to start another connection, should successfully reach the browser side
  // PresentationServiceDelegate.
  EXPECT_CALL(mock_presentation_service_delegate,
              ReconnectPresentation(_, presentation_connection_id, _, _));
  EXPECT_TRUE(ExecJs(rfh_a, "presentationRequest.reconnect(connection.id);",
                     EXECUTE_SCRIPT_NO_RESOLVE_PROMISES));
  base::RunLoop().RunUntilIdle();

  // Reset |presentation_service|'s controller delegate so that it won't try to
  // call Reset() on it on destruction time.
  presentation_service.OnDelegateDestroyed();
}

IN_PROC_BROWSER_TEST_F(BackForwardCacheBrowserTest,
                       DoesNotCacheIfSpeechRecognitionIsStarted) {
  ASSERT_TRUE(embedded_test_server()->Start());
  GURL url_a(embedded_test_server()->GetURL("a.com", "/title1.html"));
  GURL url_b(embedded_test_server()->GetURL("b.com", "/title1.html"));

  // 1) Navigate to url_a.
  EXPECT_TRUE(NavigateToURL(shell(), url_a));
  RenderFrameHostImpl* rfh_a = current_frame_host();
  RenderFrameDeletedObserver delete_observer_rfh_a(rfh_a);

  // 2) Start SpeechRecognition.
  EXPECT_TRUE(ExecJs(rfh_a, R"(
    new Promise(async resolve => {
    var r = new webkitSpeechRecognition();
    r.start();
    resolve();
    });
  )"));

  // 3) Navigate away.
  EXPECT_TRUE(NavigateToURL(shell(), url_b));

  // 4) The page uses SpeechRecognition so it should be deleted.
  delete_observer_rfh_a.WaitUntilDeleted();

  // 5) Go back to the page with SpeechRecognition.
  ASSERT_TRUE(HistoryGoBack(web_contents()));
  ExpectNotRestored(
      {NotRestoredReason::kBlocklistedFeatures},
      {blink::scheduler::WebSchedulerTrackedFeature::kSpeechRecognizer}, {}, {},
      {}, FROM_HERE);
}

IN_PROC_BROWSER_TEST_F(BackForwardCacheBrowserTest,
                       CanCacheIfSpeechRecognitionIsNotStarted) {
  ASSERT_TRUE(embedded_test_server()->Start());
  GURL url_a(embedded_test_server()->GetURL("a.com", "/title1.html"));
  GURL url_b(embedded_test_server()->GetURL("b.com", "/title1.html"));

  // 1) Navigate to url_a.
  EXPECT_TRUE(NavigateToURL(shell(), url_a));
  RenderFrameHostImpl* rfh_a = current_frame_host();
  RenderFrameDeletedObserver delete_observer_rfh_a(rfh_a);

  // 2) Initialise SpeechRecognition but don't start it yet.
  EXPECT_TRUE(ExecJs(rfh_a, R"(
    new Promise(async resolve => {
    var r = new webkitSpeechRecognition();
    resolve();
    });
  )"));

  // 3) Navigate away.
  EXPECT_TRUE(NavigateToURL(shell(), url_b));

  // 4) The page didn't start using SpeechRecognition so it shouldn't be deleted
  // and enter BackForwardCache.
  EXPECT_FALSE(delete_observer_rfh_a.deleted());
  EXPECT_TRUE(rfh_a->IsInBackForwardCache());

  // 5) Go back to the page with SpeechRecognition.
  ASSERT_TRUE(HistoryGoBack(web_contents()));
  EXPECT_EQ(rfh_a, current_frame_host());

  ExpectRestored(FROM_HERE);
}

// This test is not important for Chrome OS if TTS is called in content. For
// more details refer (content/browser/speech/tts_platform_impl.cc).
#if BUILDFLAG(IS_CHROMEOS)
#define MAYBE_DoesNotCacheIfUsingSpeechSynthesis \
  DISABLED_DoesNotCacheIfUsingSpeechSynthesis
#else
#define MAYBE_DoesNotCacheIfUsingSpeechSynthesis \
  DoesNotCacheIfUsingSpeechSynthesis
#endif  // BUILDFLAG(IS_CHROMEOS_ASH)
IN_PROC_BROWSER_TEST_F(BackForwardCacheBrowserTest,
                       MAYBE_DoesNotCacheIfUsingSpeechSynthesis) {
  ASSERT_TRUE(embedded_test_server()->Start());
  GURL url_a(embedded_test_server()->GetURL("a.com", "/title1.html"));
  GURL url_b(embedded_test_server()->GetURL("b.com", "/title1.html"));

  // 1) Navigate to a page and start using SpeechSynthesis.
  EXPECT_TRUE(NavigateToURL(shell(), url_a));
  RenderFrameHostImpl* rfh_a = current_frame_host();
  RenderFrameDeletedObserver rhf_a_deleted(rfh_a);

  EXPECT_TRUE(ExecJs(rfh_a, R"(
    new Promise(async resolve => {
    var u = new SpeechSynthesisUtterance(" ");
    speechSynthesis.speak(u);
    resolve();
    });
  )"));

  // 2) Navigate away.
  EXPECT_TRUE(NavigateToURL(shell(), url_b));

  // The page uses SpeechSynthesis so it should be deleted.
  rhf_a_deleted.WaitUntilDeleted();

  // 3) Go back to the page with SpeechSynthesis.
  ASSERT_TRUE(HistoryGoBack(web_contents()));
  ExpectNotRestored(
      {NotRestoredReason::kBlocklistedFeatures},
      {blink::scheduler::WebSchedulerTrackedFeature::kSpeechSynthesis}, {}, {},
      {}, FROM_HERE);
}

IN_PROC_BROWSER_TEST_F(BackForwardCacheBrowserTest,
                       DoesNotCacheIfRunFileChooserIsInvoked) {
  ASSERT_TRUE(embedded_test_server()->Start());
  GURL url_a(embedded_test_server()->GetURL("a.com", "/title1.html"));
  GURL url_b(embedded_test_server()->GetURL("b.com", "/title1.html"));

  // 1) Navigate to url_a and open file chooser.
  EXPECT_TRUE(NavigateToURL(shell(), url_a));
  RenderFrameHostImpl* rfh_a = current_frame_host();
  RenderFrameDeletedObserver deleted_rfh_a(rfh_a);
  content::BackForwardCacheDisabledTester tester;

  // 2) Bind FileChooser to RenderFrameHost.
  mojo::Remote<blink::mojom::FileChooser> chooser =
      FileChooserImpl::CreateBoundForTesting(rfh_a);

  auto quit_run_loop = [](base::OnceClosure callback,
                          blink::mojom::FileChooserResultPtr result) {
    std::move(callback).Run();
  };

  // 3) Run OpenFileChooser and wait till its run.
  base::RunLoop run_loop;
  chooser->OpenFileChooser(
      blink::mojom::FileChooserParams::New(),
      base::BindOnce(quit_run_loop, run_loop.QuitClosure()));
  run_loop.Run();

  // 4) rfh_a should be disabled for BackForwardCache after opening file
  // chooser.
  EXPECT_TRUE(rfh_a->IsBackForwardCacheDisabled());
  auto reason = BackForwardCacheDisable::DisabledReason(
      BackForwardCacheDisable::DisabledReasonId::kFileChooser);
  EXPECT_TRUE(tester.IsDisabledForFrameWithReason(
      rfh_a->GetProcess()->GetID(), rfh_a->GetRoutingID(), reason));

  // 5) Navigate to B having the file chooser open.
  EXPECT_TRUE(NavigateToURL(shell(), url_b));

  // The page uses FileChooser so it should be deleted.
  deleted_rfh_a.WaitUntilDeleted();

  // 6) Go back to the page with FileChooser.
  ASSERT_TRUE(HistoryGoBack(web_contents()));
  ExpectNotRestored({NotRestoredReason::kDisableForRenderFrameHostCalled}, {},
                    {}, {reason}, {}, FROM_HERE);
}

IN_PROC_BROWSER_TEST_F(BackForwardCacheBrowserTest, CacheWithMediaSession) {
  ASSERT_TRUE(embedded_test_server()->Start());

  // 1) Navigate to a page using MediaSession.
  EXPECT_TRUE(NavigateToURL(
      shell(), embedded_test_server()->GetURL("a.com", "/title1.html")));
  RenderFrameHostImplWrapper rfh_a(current_frame_host());
  EXPECT_TRUE(ExecJs(rfh_a.get(), R"(
    navigator.mediaSession.metadata = new MediaMetadata({
      artwork: [
        {src: "test_image.jpg", sizes: "1x1", type: "image/jpeg"},
        {src: "test_image.jpg", sizes: "10x10", type: "image/jpeg"}
      ]
    });
  )"));

  // 2) Navigate away.
  EXPECT_TRUE(NavigateToURL(
      shell(), embedded_test_server()->GetURL("b.com", "/title1.html")));
  EXPECT_TRUE(rfh_a->IsInBackForwardCache());

  // 3) Go back.
  ASSERT_TRUE(HistoryGoBack(web_contents()));
  EXPECT_EQ(rfh_a.get(), current_frame_host());
  ExpectRestored(FROM_HERE);
  // Check the media session state is reserved.
  EXPECT_EQ("10x10", EvalJs(rfh_a.get(), R"(
    navigator.mediaSession.metadata.artwork[1].sizes;
  )"));
}

class BackForwardCacheBrowserTestWithSupportedFeatures
    : public BackForwardCacheBrowserTest {
 protected:
  void SetUpCommandLine(base::CommandLine* command_line) override {
    EnableFeatureAndSetParams(features::kBackForwardCache, "supported_features",
                              "BroadcastChannel,KeyboardLock");
    BackForwardCacheBrowserTest::SetUpCommandLine(command_line);
  }
};

IN_PROC_BROWSER_TEST_F(BackForwardCacheBrowserTestWithSupportedFeatures,
                       CacheWithSpecifiedFeatures) {
  ASSERT_TRUE(CreateHttpsServer()->Start());

  GURL url_a(https_server()->GetURL("a.test", "/title1.html"));
  GURL url_b(https_server()->GetURL("b.test", "/title1.html"));

  // 1) Navigate to the page A with BroadcastChannel.
  EXPECT_TRUE(NavigateToURL(shell(), url_a));
  RenderFrameHostImpl* rfh_a = current_frame_host();
  RenderFrameDeletedObserver deleted(rfh_a);
  EXPECT_TRUE(ExecJs(rfh_a, "window.foo = new BroadcastChannel('foo');"));

  // 2) Navigate away.
  EXPECT_TRUE(NavigateToURL(shell(), url_b));
  EXPECT_FALSE(deleted.deleted());
  EXPECT_TRUE(rfh_a->IsInBackForwardCache());

  // 3) Go back to the page A
  ASSERT_TRUE(HistoryGoBack(web_contents()));
  EXPECT_EQ(rfh_a, current_frame_host());
  ExpectRestored(FROM_HERE);

  // 4) Use KeyboardLock.
  AcquireKeyboardLock(rfh_a);

  // 5) Navigate away again.
  EXPECT_TRUE(NavigateToURL(shell(), url_b));
  EXPECT_FALSE(deleted.deleted());
  EXPECT_TRUE(rfh_a->IsInBackForwardCache());

  // 6) Go back to the page A again.
  ASSERT_TRUE(HistoryGoBack(web_contents()));
  EXPECT_EQ(rfh_a, current_frame_host());
  ExpectRestored(FROM_HERE);
}

class BackForwardCacheBrowserTestWithNoSupportedFeatures
    : public BackForwardCacheBrowserTest {
 protected:
  void SetUpCommandLine(base::CommandLine* command_line) override {
    // Specify empty supported features explicitly.
    EnableFeatureAndSetParams(features::kBackForwardCache, "supported_features",
                              "");
    BackForwardCacheBrowserTest::SetUpCommandLine(command_line);
  }
};

IN_PROC_BROWSER_TEST_F(BackForwardCacheBrowserTestWithNoSupportedFeatures,
                       DontCache) {
  ASSERT_TRUE(CreateHttpsServer()->Start());

  GURL url_a(https_server()->GetURL("a.test", "/title1.html"));
  GURL url_b(https_server()->GetURL("b.test", "/title1.html"));

  // 1) Navigate to the page A with BroadcastChannel.
  EXPECT_TRUE(NavigateToURL(shell(), url_a));
  RenderFrameHostImpl* rfh_a1 = current_frame_host();
  RenderFrameDeletedObserver deleted_a1(rfh_a1);
  EXPECT_TRUE(ExecJs(rfh_a1, "window.foo = new BroadcastChannel('foo');"));

  // 2) Navigate away.
  EXPECT_TRUE(NavigateToURL(shell(), url_b));
  deleted_a1.WaitUntilDeleted();

  // 3) Go back to the page A
  ASSERT_TRUE(HistoryGoBack(web_contents()));
  ExpectNotRestored(
      {NotRestoredReason::kBlocklistedFeatures},
      {blink::scheduler::WebSchedulerTrackedFeature::kBroadcastChannel}, {}, {},
      {}, FROM_HERE);

  RenderFrameHostImpl* rfh_a2 = current_frame_host();
  RenderFrameDeletedObserver deleted_a2(rfh_a2);

  // 4) Use KeyboardLock.
  AcquireKeyboardLock(rfh_a2);

  // 5) Navigate away again.
  EXPECT_TRUE(NavigateToURL(shell(), url_b));
  deleted_a2.WaitUntilDeleted();

  // 6) Go back to the page A again.
  ASSERT_TRUE(HistoryGoBack(web_contents()));
  ExpectNotRestored(
      {NotRestoredReason::kBlocklistedFeatures},
      {blink::scheduler::WebSchedulerTrackedFeature::kKeyboardLock}, {}, {}, {},
      FROM_HERE);
}

class BackForwardCacheBrowserTestWithMediaSession
    : public BackForwardCacheBrowserTest {
 protected:
  void PlayVideoNavigateAndGoBack() {
    MediaSession* media_session = MediaSession::Get(shell()->web_contents());
    ASSERT_TRUE(media_session);

    content::MediaStartStopObserver start_observer(
        shell()->web_contents(), MediaStartStopObserver::Type::kStart);
    EXPECT_TRUE(ExecJs(current_frame_host(),
                       "document.querySelector('#long-video').play();"));
    start_observer.Wait();

    content::MediaStartStopObserver stop_observer(
        shell()->web_contents(), MediaStartStopObserver::Type::kStop);
    media_session->Suspend(MediaSession::SuspendType::kSystem);
    stop_observer.Wait();

    // Navigate away.
    EXPECT_TRUE(NavigateToURL(
        shell(), embedded_test_server()->GetURL("b.test", "/title1.html")));

    // Go back.
    ASSERT_TRUE(HistoryGoBack(web_contents()));
  }
};

IN_PROC_BROWSER_TEST_F(BackForwardCacheBrowserTestWithMediaSession,
                       CacheWhenMediaSessionPlaybackStateIsChanged) {
  ASSERT_TRUE(embedded_test_server()->Start());

  // 1) Navigate to a page.
  EXPECT_TRUE(NavigateToURL(
      shell(), embedded_test_server()->GetURL("a.test", "/title1.html")));

  // 2) Update the playback state change.
  EXPECT_TRUE(ExecJs(shell()->web_contents()->GetPrimaryMainFrame(), R"(
    navigator.mediaSession.playbackState = 'playing';
  )"));

  // 3) Navigate away.
  EXPECT_TRUE(NavigateToURL(
      shell(), embedded_test_server()->GetURL("b.com", "/title1.html")));

  // 4) Go back.
  ASSERT_TRUE(HistoryGoBack(web_contents()));

  // The page is restored since a MediaSession service is not used.
  ExpectRestored(FROM_HERE);
}

IN_PROC_BROWSER_TEST_F(BackForwardCacheBrowserTestWithMediaSession,
                       CacheWhenMediaSessionServiceIsNotUsed) {
  // There are sometimes unexpected messages from a renderer to the browser,
  // which caused test flakiness.
  // TODO(crbug.com/1253200): Fix the test flakiness.
  DoNotFailForUnexpectedMessagesWhileCached();

  ASSERT_TRUE(embedded_test_server()->Start());

  // 1) Navigate to a page using MediaSession.
  EXPECT_TRUE(NavigateToURL(
      shell(), embedded_test_server()->GetURL(
                   "a.test", "/media/session/media-session.html")));

  // Play the media once, but without setting any callbacks to the MediaSession.
  // In this case, a MediaSession service is not used.
  PlayVideoNavigateAndGoBack();

  // The page is restored since a MediaSession service is not used.
  ExpectRestored(FROM_HERE);
}

IN_PROC_BROWSER_TEST_F(BackForwardCacheBrowserTestWithMediaSession,
                       DontCacheWhenMediaSessionServiceIsUsed) {
  ASSERT_TRUE(embedded_test_server()->Start());

  // Navigate to a page using MediaSession.
  EXPECT_TRUE(NavigateToURL(
      shell(), embedded_test_server()->GetURL(
                   "a.test", "/media/session/media-session.html")));
  RenderFrameHostWrapper rfh_a(current_frame_host());
  // Register a callback explicitly to use a MediaSession service.
  EXPECT_TRUE(ExecJs(rfh_a.get(), R"(
    navigator.mediaSession.setActionHandler('play', () => {});
  )"));

  PlayVideoNavigateAndGoBack();

  // The page is not restored since a MediaSession service is used.
  auto reason = BackForwardCacheDisable::DisabledReason(
      BackForwardCacheDisable::DisabledReasonId::kMediaSessionService);
  ExpectNotRestored({NotRestoredReason::kDisableForRenderFrameHostCalled}, {},
                    {}, {reason}, {}, FROM_HERE);
}

}  // namespace content
