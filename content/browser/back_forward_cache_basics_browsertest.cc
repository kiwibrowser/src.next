// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/back_forward_cache_browsertest.h"

#include "content/browser/web_contents/web_contents_impl.h"
#include "content/public/browser/site_isolation_policy.h"
#include "content/public/common/url_constants.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/content_browser_test_utils.h"
#include "content/public/test/navigation_handle_observer.h"
#include "content/public/test/test_navigation_observer.h"
#include "content/public/test/url_loader_interceptor.h"
#include "content/shell/browser/shell.h"
#include "net/test/embedded_test_server/controllable_http_response.h"
#include "third_party/blink/public/common/frame/event_page_show_persisted.h"

// This file contains back/forward-cache tests that test basic functionality,
// e.g. navigation, different responses and document structures.
// Almost everything in here could have been written as a JS-only WPT. It was
// forked from
// https://source.chromium.org/chromium/chromium/src/+/main:content/browser/back_forward_cache_browsertest.cc;drc=804bb57be3441b6291c11e34d8f901e2b1c0b430
//
// When adding tests here consider adding a WPT intead. See
// third_party/blink/web_tests/external/wpt/html/browsers/browsing-the-web/back-forward-cache/README.md

using testing::_;
using testing::Each;
using testing::ElementsAre;
using testing::Not;
using testing::UnorderedElementsAreArray;

namespace content {

using NotRestoredReason = BackForwardCacheMetrics::NotRestoredReason;
using NotRestoredReasons =
    BackForwardCacheCanStoreDocumentResult::NotRestoredReasons;

// Navigate from A to B and go back.
IN_PROC_BROWSER_TEST_F(BackForwardCacheBrowserTest, Basic) {
  ASSERT_TRUE(embedded_test_server()->Start());
  GURL url_a(embedded_test_server()->GetURL("a.com", "/title1.html"));
  GURL url_b(embedded_test_server()->GetURL("b.com", "/title1.html"));
  url::Origin origin_a = url::Origin::Create(url_a);
  url::Origin origin_b = url::Origin::Create(url_b);

  // 1) Navigate to A.
  EXPECT_TRUE(NavigateToURL(shell(), url_a));
  RenderFrameHostImpl* rfh_a = current_frame_host();
  RenderFrameDeletedObserver delete_observer_rfh_a(rfh_a);

  // 2) Navigate to B.
  EXPECT_TRUE(NavigateToURL(shell(), url_b));
  RenderFrameHostImpl* rfh_b = current_frame_host();
  RenderFrameDeletedObserver delete_observer_rfh_b(rfh_b);
  EXPECT_FALSE(delete_observer_rfh_a.deleted());
  EXPECT_TRUE(rfh_a->IsInBackForwardCache());
  EXPECT_EQ(rfh_a->GetVisibilityState(), PageVisibilityState::kHidden);
  EXPECT_EQ(origin_a, rfh_a->GetLastCommittedOrigin());
  EXPECT_EQ(origin_b, rfh_b->GetLastCommittedOrigin());
  EXPECT_FALSE(rfh_b->IsInBackForwardCache());
  EXPECT_EQ(rfh_b->GetVisibilityState(), PageVisibilityState::kVisible);

  // 3) Go back to A.
  ASSERT_TRUE(HistoryGoBack(web_contents()));
  EXPECT_FALSE(delete_observer_rfh_a.deleted());
  EXPECT_FALSE(delete_observer_rfh_b.deleted());
  EXPECT_EQ(origin_a, rfh_a->GetLastCommittedOrigin());
  EXPECT_EQ(origin_b, rfh_b->GetLastCommittedOrigin());
  EXPECT_EQ(rfh_a, current_frame_host());
  EXPECT_FALSE(rfh_a->IsInBackForwardCache());
  EXPECT_EQ(rfh_a->GetVisibilityState(), PageVisibilityState::kVisible);
  EXPECT_TRUE(rfh_b->IsInBackForwardCache());
  EXPECT_EQ(rfh_b->GetVisibilityState(), PageVisibilityState::kHidden);

  ExpectRestored(FROM_HERE);
}

// Navigate from A to B and go back.
IN_PROC_BROWSER_TEST_F(BackForwardCacheBrowserTest, BasicDocumentInitiated) {
  ASSERT_TRUE(embedded_test_server()->Start());
  GURL url_a(embedded_test_server()->GetURL("a.com", "/title1.html"));
  GURL url_b(embedded_test_server()->GetURL("b.com", "/title1.html"));

  // 1) Navigate to A.
  EXPECT_TRUE(NavigateToURL(shell(), url_a));
  RenderFrameHostImpl* rfh_a = current_frame_host();
  RenderFrameDeletedObserver delete_observer_rfh_a(rfh_a);

  // 2) Navigate to B.
  ASSERT_TRUE(NavigateToURLFromRenderer(shell(), url_b));
  RenderFrameHostImpl* rfh_b = current_frame_host();
  RenderFrameDeletedObserver delete_observer_rfh_b(rfh_b);
  EXPECT_FALSE(delete_observer_rfh_a.deleted());
  EXPECT_TRUE(rfh_a->IsInBackForwardCache());
  EXPECT_FALSE(rfh_b->IsInBackForwardCache());

  // The two pages are using different BrowsingInstances.
  EXPECT_FALSE(rfh_a->GetSiteInstance()->IsRelatedSiteInstance(
      rfh_b->GetSiteInstance()));

  // 3) Go back to A.
  EXPECT_TRUE(ExecJs(shell(), "history.back();"));
  EXPECT_TRUE(WaitForLoadStop(shell()->web_contents()));
  EXPECT_FALSE(delete_observer_rfh_a.deleted());
  EXPECT_FALSE(delete_observer_rfh_b.deleted());
  EXPECT_EQ(rfh_a, current_frame_host());
  EXPECT_FALSE(rfh_a->IsInBackForwardCache());
  EXPECT_TRUE(rfh_b->IsInBackForwardCache());

  ExpectRestored(FROM_HERE);
}

// Navigate from back and forward repeatedly.
IN_PROC_BROWSER_TEST_F(BackForwardCacheBrowserTest,
                       NavigateBackForwardRepeatedly) {
  // Do not check for unexpected messages because the input task queue is not
  // currently frozen, causing flakes in this test: crbug.com/1099395.
  DoNotFailForUnexpectedMessagesWhileCached();
  ASSERT_TRUE(embedded_test_server()->Start());
  GURL url_a(embedded_test_server()->GetURL("a.com", "/title1.html"));
  GURL url_b(embedded_test_server()->GetURL("b.com", "/title1.html"));

  // 1) Navigate to A.
  EXPECT_TRUE(NavigateToURL(shell(), url_a));
  RenderFrameHostImpl* rfh_a = current_frame_host();
  RenderFrameDeletedObserver delete_observer_rfh_a(rfh_a);

  // 2) Navigate to B.
  EXPECT_TRUE(NavigateToURL(shell(), url_b));
  RenderFrameHostImpl* rfh_b = current_frame_host();
  RenderFrameDeletedObserver delete_observer_rfh_b(rfh_b);
  EXPECT_TRUE(rfh_a->IsInBackForwardCache());
  EXPECT_FALSE(rfh_b->IsInBackForwardCache());

  // 3) Go back to A.
  ASSERT_TRUE(HistoryGoBack(web_contents()));

  EXPECT_EQ(rfh_a, current_frame_host());
  EXPECT_FALSE(rfh_a->IsInBackForwardCache());
  EXPECT_TRUE(rfh_b->IsInBackForwardCache());

  ExpectRestored(FROM_HERE);

  // 4) Go forward to B.
  ASSERT_TRUE(HistoryGoForward(web_contents()));

  EXPECT_EQ(rfh_b, current_frame_host());
  EXPECT_TRUE(rfh_a->IsInBackForwardCache());
  EXPECT_FALSE(rfh_b->IsInBackForwardCache());

  ExpectRestored(FROM_HERE);

  // 5) Go back to A.
  ASSERT_TRUE(HistoryGoBack(web_contents()));

  EXPECT_EQ(rfh_a, current_frame_host());
  EXPECT_FALSE(rfh_a->IsInBackForwardCache());
  EXPECT_TRUE(rfh_b->IsInBackForwardCache());

  ExpectRestored(FROM_HERE);

  // 6) Go forward to B.
  ASSERT_TRUE(HistoryGoForward(web_contents()));

  EXPECT_EQ(rfh_b, current_frame_host());
  EXPECT_TRUE(rfh_a->IsInBackForwardCache());
  EXPECT_FALSE(rfh_b->IsInBackForwardCache());

  EXPECT_FALSE(delete_observer_rfh_a.deleted());
  EXPECT_FALSE(delete_observer_rfh_b.deleted());

  ExpectRestored(FROM_HERE);
}

// The current page can't enter the BackForwardCache if another page can script
// it. This can happen when one document opens a popup using window.open() for
// instance. It prevents the BackForwardCache from being used.
IN_PROC_BROWSER_TEST_F(BackForwardCacheBrowserTest, WindowOpen) {
  // This test assumes cross-site navigation staying in the same
  // BrowsingInstance to use a different SiteInstance. Otherwise, it will
  // timeout at step 2).
  if (!SiteIsolationPolicy::UseDedicatedProcessesForAllSites())
    return;

  ASSERT_TRUE(embedded_test_server()->Start());
  GURL url_a(embedded_test_server()->GetURL("a.com", "/title1.html"));
  GURL url_b(embedded_test_server()->GetURL("b.com", "/title1.html"));

  // 1) Navigate to A and open a popup.
  ASSERT_TRUE(NavigateToURL(shell(), url_a));
  RenderFrameHostImplWrapper rfh_a(current_frame_host());
  EXPECT_EQ(1u, rfh_a->GetSiteInstance()->GetRelatedActiveContentsCount());
  Shell* popup = OpenPopup(rfh_a.get(), url_a, "");
  EXPECT_EQ(2u, rfh_a->GetSiteInstance()->GetRelatedActiveContentsCount());
  rfh_a->GetBackForwardCacheMetrics()->SetObserverForTesting(this);

  // 2) Navigate to B. The previous document can't enter the BackForwardCache,
  // because of the popup.
  ASSERT_TRUE(NavigateToURLFromRenderer(rfh_a.get(), url_b));
  ASSERT_TRUE(rfh_a.WaitUntilRenderFrameDeleted());
  RenderFrameHostImplWrapper rfh_b(current_frame_host());
  EXPECT_EQ(2u, rfh_b->GetSiteInstance()->GetRelatedActiveContentsCount());

  // 3) Go back to A. The previous document can't enter the BackForwardCache,
  // because of the popup.
  ASSERT_TRUE(ExecJs(rfh_b.get(), "history.back();"));
  ASSERT_TRUE(rfh_b.WaitUntilRenderFrameDeleted());

  ExpectNotRestored({NotRestoredReason::kRelatedActiveContentsExist,
                     NotRestoredReason::kBrowsingInstanceNotSwapped},
                    {},
                    {ShouldSwapBrowsingInstance::kNo_HasRelatedActiveContents},
                    {}, {}, FROM_HERE);
  // Make sure that the tree result also has the same reasons.
  EXPECT_THAT(
      GetTreeResult()->GetDocumentResult(),
      MatchesDocumentResult(
          NotRestoredReasons(NotRestoredReason::kRelatedActiveContentsExist,
                             NotRestoredReason::kBrowsingInstanceNotSwapped),
          BlockListedFeatures()));

  // 4) Make the popup drop the window.opener connection. It happens when the
  //    user does an omnibox-initiated navigation, which happens in a new
  //    BrowsingInstance.
  RenderFrameHostImplWrapper rfh_a_new(current_frame_host());
  EXPECT_EQ(2u, rfh_a_new->GetSiteInstance()->GetRelatedActiveContentsCount());
  ASSERT_TRUE(NavigateToURL(popup, url_b));
  EXPECT_EQ(1u, rfh_a_new->GetSiteInstance()->GetRelatedActiveContentsCount());

  // 5) Navigate to B again. As the scripting relationship with the popup is
  // now severed, the current page (|rfh_a_new|) can enter back-forward cache.
  ASSERT_TRUE(NavigateToURLFromRenderer(rfh_a_new.get(), url_b));
  EXPECT_FALSE(rfh_a_new.IsRenderFrameDeleted());
  EXPECT_TRUE(rfh_a_new->IsInBackForwardCache());

  // 6) Go back to A. The current document can finally enter the
  // BackForwardCache, because it is alone in its BrowsingInstance and has never
  // been related to any other document.
  RenderFrameHostImplWrapper rfh_b_new(current_frame_host());
  ASSERT_TRUE(ExecJs(rfh_b_new.get(), "history.back();"));
  ASSERT_TRUE(WaitForLoadStop(web_contents()));
  EXPECT_FALSE(rfh_b_new.IsRenderFrameDeleted());
  EXPECT_TRUE(rfh_b_new->IsInBackForwardCache());
}

// A popup will prevent a page from entering BFCache. Test that after closing a
// popup, the page is not stopped from entering. This tries to close the popup
// at the last moment.
IN_PROC_BROWSER_TEST_F(BackForwardCacheBrowserTest, WindowOpenThenClose) {
  net::test_server::ControllableHttpResponse response(embedded_test_server(),
                                                      "/title2.html");
  ASSERT_TRUE(embedded_test_server()->Start());
  GURL url_a(embedded_test_server()->GetURL("a.test", "/title1.html"));
  GURL url_b(embedded_test_server()->GetURL("b.test", "/title2.html"));

  // Navigate to A.
  ASSERT_TRUE(NavigateToURL(shell(), url_a));
  RenderFrameHostImplWrapper rfh_a(current_frame_host());
  EXPECT_EQ(1u, rfh_a->GetSiteInstance()->GetRelatedActiveContentsCount());

  // Open a popup.
  Shell* popup = OpenPopup(rfh_a.get(), url_a, "");
  EXPECT_EQ(2u, rfh_a->GetSiteInstance()->GetRelatedActiveContentsCount());

  // Start navigating to B, the response will be delayed.
  TestNavigationObserver observer(web_contents());
  shell()->LoadURL(url_b);

  // When the request is received, close the popup.
  response.WaitForRequest();
  RenderFrameHostImplWrapper rfh_popup(
      popup->web_contents()->GetPrimaryMainFrame());
  ASSERT_TRUE(ExecJs(rfh_popup.get(), "window.close();"));
  ASSERT_TRUE(rfh_popup.WaitUntilRenderFrameDeleted());

  EXPECT_EQ(1u, rfh_a->GetSiteInstance()->GetRelatedActiveContentsCount());

  // Send the response.
  response.Send(net::HTTP_OK, "text/html", "foo");
  response.Done();
  observer.Wait();

  // A is in BFCache.
  EXPECT_EQ(0u, rfh_a->GetSiteInstance()->GetRelatedActiveContentsCount());
  ASSERT_TRUE(rfh_a->IsInBackForwardCache());

  // Go back.
  web_contents()->GetController().GoBack();
  ASSERT_TRUE(WaitForLoadStop(shell()->web_contents()));

  // A is restored from BFCache.
  EXPECT_FALSE(rfh_a.IsRenderFrameDeleted());
  ExpectRestored(FROM_HERE);
}

// Navigate from A(B) to C and go back.
IN_PROC_BROWSER_TEST_F(BackForwardCacheBrowserTest, BasicIframe) {
  ASSERT_TRUE(embedded_test_server()->Start());
  GURL url_a(embedded_test_server()->GetURL(
      "a.com", "/cross_site_iframe_factory.html?a(b)"));
  GURL url_c(embedded_test_server()->GetURL("c.com", "/title1.html"));

  // 1) Navigate to A(B).
  EXPECT_TRUE(NavigateToURL(shell(), url_a));
  RenderFrameHostImpl* rfh_a = current_frame_host();
  RenderFrameHostImpl* rfh_b = rfh_a->child_at(0)->current_frame_host();
  RenderFrameDeletedObserver delete_observer_rfh_a(rfh_a);
  RenderFrameDeletedObserver delete_observer_rfh_b(rfh_b);

  // 2) Navigate to C.
  EXPECT_TRUE(NavigateToURL(shell(), url_c));
  RenderFrameHostImpl* rfh_c = current_frame_host();
  RenderFrameDeletedObserver delete_observer_rfh_c(rfh_c);
  EXPECT_FALSE(delete_observer_rfh_a.deleted());
  EXPECT_FALSE(delete_observer_rfh_b.deleted());
  EXPECT_TRUE(rfh_a->IsInBackForwardCache());
  EXPECT_TRUE(rfh_b->IsInBackForwardCache());
  EXPECT_FALSE(rfh_c->IsInBackForwardCache());

  // 3) Go back to A(B).
  ASSERT_TRUE(HistoryGoBack(web_contents()));
  EXPECT_FALSE(delete_observer_rfh_a.deleted());
  EXPECT_FALSE(delete_observer_rfh_b.deleted());
  EXPECT_FALSE(delete_observer_rfh_c.deleted());
  EXPECT_EQ(rfh_a, current_frame_host());
  EXPECT_FALSE(rfh_a->IsInBackForwardCache());
  EXPECT_FALSE(rfh_b->IsInBackForwardCache());
  EXPECT_TRUE(rfh_c->IsInBackForwardCache());

  ExpectRestored(FROM_HERE);
}

// Similar to BackForwardCacheBrowserTest.SubframeSurviveCache*
// Test case: a1(b2) -> c3 -> a1(b2)
IN_PROC_BROWSER_TEST_F(BackForwardCacheBrowserTest, SubframeSurviveCache1) {
  ASSERT_TRUE(embedded_test_server()->Start());
  GURL url_a(embedded_test_server()->GetURL(
      "a.com", "/cross_site_iframe_factory.html?a(b)"));
  GURL url_c(embedded_test_server()->GetURL("c.com", "/title1.html"));

  std::vector<RenderFrameDeletedObserver*> rfh_observer;

  // 1) Navigate to a1(b2).
  EXPECT_TRUE(NavigateToURL(shell(), url_a));
  RenderFrameHostImpl* a1 = current_frame_host();
  RenderFrameHostImpl* b2 = a1->child_at(0)->current_frame_host();
  RenderFrameDeletedObserver a1_observer(a1), b2_observer(b2);
  rfh_observer.insert(rfh_observer.end(), {&a1_observer, &b2_observer});
  EXPECT_TRUE(ExecJs(b2, "window.alive = 'I am alive';"));

  // 2) Navigate to c3.
  EXPECT_TRUE(NavigateToURL(shell(), url_c));
  RenderFrameHostImpl* c3 = current_frame_host();
  RenderFrameDeletedObserver c3_observer(c3);
  rfh_observer.push_back(&c3_observer);
  ASSERT_THAT(rfh_observer, Each(Not(Deleted())));
  EXPECT_THAT(Elements({a1, b2}), Each(InBackForwardCache()));
  EXPECT_THAT(c3, Not(InBackForwardCache()));

  // 3) Go back to a1(b2).
  ASSERT_TRUE(HistoryGoBack(web_contents()));
  ASSERT_THAT(rfh_observer, Each(Not(Deleted())));
  EXPECT_THAT(Elements({a1, b2}), Each(Not(InBackForwardCache())));
  EXPECT_THAT(c3, InBackForwardCache());

  // Even after a new IPC round trip with the renderer, b2 must still be alive.
  EXPECT_EQ("I am alive", EvalJs(b2, "window.alive"));
  EXPECT_FALSE(b2_observer.deleted());

  ExpectRestored(FROM_HERE);
}

// Similar to BackForwardCacheBrowserTest.SubframeSurviveCache*
// Test case: a1(b2) -> b3 -> a1(b2).
IN_PROC_BROWSER_TEST_F(BackForwardCacheBrowserTest, SubframeSurviveCache2) {
  ASSERT_TRUE(embedded_test_server()->Start());
  GURL url_a(embedded_test_server()->GetURL(
      "a.com", "/cross_site_iframe_factory.html?a(b)"));
  GURL url_b(embedded_test_server()->GetURL("b.com", "/title1.html"));

  std::vector<RenderFrameDeletedObserver*> rfh_observer;

  // 1) Navigate to a1(b2).
  EXPECT_TRUE(NavigateToURL(shell(), url_a));
  RenderFrameHostImpl* a1 = current_frame_host();
  RenderFrameHostImpl* b2 = a1->child_at(0)->current_frame_host();
  RenderFrameDeletedObserver a1_observer(a1), b2_observer(b2);
  rfh_observer.insert(rfh_observer.end(), {&a1_observer, &b2_observer});
  EXPECT_TRUE(ExecJs(b2, "window.alive = 'I am alive';"));

  // 2) Navigate to b3.
  EXPECT_TRUE(NavigateToURL(shell(), url_b));
  RenderFrameHostImpl* b3 = current_frame_host();
  RenderFrameDeletedObserver b3_observer(b3);
  rfh_observer.push_back(&b3_observer);
  ASSERT_THAT(rfh_observer, Each(Not(Deleted())));
  EXPECT_THAT(Elements({a1, b2}), Each(InBackForwardCache()));
  EXPECT_THAT(b3, Not(InBackForwardCache()));

  // 3) Go back to a1(b2).
  ASSERT_TRUE(HistoryGoBack(web_contents()));
  ASSERT_THAT(rfh_observer, Each(Not(Deleted())));
  EXPECT_EQ(a1, current_frame_host());
  EXPECT_THAT(Elements({a1, b2}), Each(Not(InBackForwardCache())));
  EXPECT_THAT(b3, InBackForwardCache());

  // Even after a new IPC round trip with the renderer, b2 must still be alive.
  EXPECT_EQ("I am alive", EvalJs(b2, "window.alive"));
  EXPECT_FALSE(b2_observer.deleted());

  ExpectRestored(FROM_HERE);
}

// Similar to BackForwardCacheBrowserTest.tSubframeSurviveCache*
// Test case: a1(b2) -> b3(a4) -> a1(b2) -> b3(a4)
IN_PROC_BROWSER_TEST_F(BackForwardCacheBrowserTest, SubframeSurviveCache3) {
  ASSERT_TRUE(embedded_test_server()->Start());
  GURL url_a(embedded_test_server()->GetURL(
      "a.com", "/cross_site_iframe_factory.html?a(b)"));
  GURL url_b(embedded_test_server()->GetURL(
      "b.com", "/cross_site_iframe_factory.html?b(a)"));

  std::vector<RenderFrameDeletedObserver*> rfh_observer;

  // 1) Navigate to a1(b2).
  EXPECT_TRUE(NavigateToURL(shell(), url_a));
  RenderFrameHostImpl* a1 = current_frame_host();
  RenderFrameHostImpl* b2 = a1->child_at(0)->current_frame_host();
  RenderFrameDeletedObserver a1_observer(a1), b2_observer(b2);
  rfh_observer.insert(rfh_observer.end(), {&a1_observer, &b2_observer});
  EXPECT_TRUE(ExecJs(b2, "window.alive = 'I am alive';"));

  // 2) Navigate to b3(a4)
  EXPECT_TRUE(NavigateToURL(shell(), url_b));
  RenderFrameHostImpl* b3 = current_frame_host();
  RenderFrameHostImpl* a4 = b3->child_at(0)->current_frame_host();
  RenderFrameDeletedObserver b3_observer(b3), a4_observer(a4);
  rfh_observer.insert(rfh_observer.end(), {&b3_observer, &a4_observer});
  ASSERT_THAT(rfh_observer, Each(Not(Deleted())));
  EXPECT_THAT(Elements({a1, b2}), Each(InBackForwardCache()));
  EXPECT_THAT(Elements({b3, a4}), Each(Not(InBackForwardCache())));
  EXPECT_TRUE(ExecJs(a4, "window.alive = 'I am alive';"));

  // 3) Go back to a1(b2).
  ASSERT_TRUE(HistoryGoBack(web_contents()));
  ASSERT_THAT(rfh_observer, Each(Not(Deleted())));
  EXPECT_EQ(a1, current_frame_host());
  EXPECT_THAT(Elements({a1, b2}), Each(Not(InBackForwardCache())));
  EXPECT_THAT(Elements({b3, a4}), Each(InBackForwardCache()));

  // Even after a new IPC round trip with the renderer, b2 must still be alive.
  EXPECT_EQ("I am alive", EvalJs(b2, "window.alive"));
  EXPECT_FALSE(b2_observer.deleted());

  ExpectRestored(FROM_HERE);

  // 4) Go forward to b3(a4).
  ASSERT_TRUE(HistoryGoForward(web_contents()));
  ASSERT_THAT(rfh_observer, Each(Not(Deleted())));
  EXPECT_EQ(b3, current_frame_host());
  EXPECT_THAT(Elements({a1, b2}), Each(InBackForwardCache()));
  EXPECT_THAT(Elements({b3, a4}), Each(Not(InBackForwardCache())));

  // Even after a new IPC round trip with the renderer, a4 must still be alive.
  EXPECT_EQ("I am alive", EvalJs(a4, "window.alive"));
  EXPECT_FALSE(a4_observer.deleted());

  ExpectRestored(FROM_HERE);
}

// Similar to BackForwardCacheBrowserTest.SubframeSurviveCache*
// Test case: a1(b2) -> b3 -> a4 -> b5 -> a1(b2).
IN_PROC_BROWSER_TEST_F(HighCacheSizeBackForwardCacheBrowserTest,
                       SubframeSurviveCache4) {
  ASSERT_TRUE(embedded_test_server()->Start());
  GURL url_ab(embedded_test_server()->GetURL(
      "a.com", "/cross_site_iframe_factory.html?a(b)"));
  GURL url_a(embedded_test_server()->GetURL("a.com", "/title1.html"));
  GURL url_b(embedded_test_server()->GetURL("b.com", "/title1.html"));

  std::vector<RenderFrameDeletedObserver*> rfh_observer;

  // 1) Navigate to a1(b2).
  EXPECT_TRUE(NavigateToURL(shell(), url_ab));
  RenderFrameHostImpl* a1 = current_frame_host();
  RenderFrameHostImpl* b2 = a1->child_at(0)->current_frame_host();
  RenderFrameDeletedObserver a1_observer(a1), b2_observer(b2);
  rfh_observer.insert(rfh_observer.end(), {&a1_observer, &b2_observer});
  EXPECT_TRUE(ExecJs(b2, "window.alive = 'I am alive';"));

  // 2) Navigate to b3.
  EXPECT_TRUE(NavigateToURL(shell(), url_b));
  RenderFrameHostImpl* b3 = current_frame_host();
  RenderFrameDeletedObserver b3_observer(b3);
  rfh_observer.push_back(&b3_observer);
  ASSERT_THAT(rfh_observer, Each(Not(Deleted())));
  EXPECT_THAT(Elements({a1, b2}), Each(InBackForwardCache()));
  EXPECT_THAT(b3, Not(InBackForwardCache()));

  // 3) Navigate to a4.
  EXPECT_TRUE(NavigateToURL(shell(), url_a));
  RenderFrameHostImpl* a4 = current_frame_host();
  RenderFrameDeletedObserver a4_observer(a4);
  rfh_observer.push_back(&a4_observer);
  ASSERT_THAT(rfh_observer, Each(Not(Deleted())));

  // 4) Navigate to b5
  EXPECT_TRUE(NavigateToURL(shell(), url_b));
  RenderFrameHostImpl* b5 = current_frame_host();
  RenderFrameDeletedObserver b5_observer(b5);
  rfh_observer.push_back(&b5_observer);
  ASSERT_THAT(rfh_observer, Each(Not(Deleted())));
  EXPECT_THAT(Elements({a1, b2, b3, a4}), Each(InBackForwardCache()));
  EXPECT_THAT(b5, Not(InBackForwardCache()));

  // 3) Go back to a1(b2).
  ASSERT_TRUE(HistoryGoToOffset(web_contents(), -3));
  EXPECT_EQ(a1, current_frame_host());
  ASSERT_THAT(rfh_observer, Each(Not(Deleted())));
  EXPECT_THAT(Elements({b3, a4, b5}), Each(InBackForwardCache()));
  EXPECT_THAT(Elements({a1, b2}), Each(Not(InBackForwardCache())));

  // Even after a new IPC round trip with the renderer, b2 must still be alive.
  EXPECT_EQ("I am alive", EvalJs(b2, "window.alive"));
  EXPECT_FALSE(b2_observer.deleted());
}

// Check that unload event handlers are not dispatched when the page goes
// into BackForwardCache.
IN_PROC_BROWSER_TEST_F(BackForwardCacheBrowserTest,
                       ConfirmUnloadEventNotFired) {
  // This test is only enabled for Android, as pages with unload handlers are
  // only eligible for bfcache on Android.
  if (!IsUnloadAllowedToEnterBackForwardCache())
    return;
  ASSERT_TRUE(embedded_test_server()->Start());
  GURL url_a(embedded_test_server()->GetURL("a.com", "/title1.html"));
  GURL url_b(embedded_test_server()->GetURL("b.com", "/title1.html"));

  // 1) Navigate to A.
  EXPECT_TRUE(NavigateToURL(shell(), url_a));
  RenderFrameHostImpl* rfh_a = current_frame_host();
  RenderFrameDeletedObserver delete_observer_rfh_a(rfh_a);

  // 2) Set unload handler and check the title.
  EXPECT_TRUE(ExecJs(rfh_a,
                     "document.title = 'loaded!';"
                     "window.addEventListener('unload', () => {"
                     "  document.title = 'unloaded!';"
                     "});"));
  {
    std::u16string title_when_loaded = u"loaded!";
    TitleWatcher title_watcher(web_contents(), title_when_loaded);
    EXPECT_EQ(title_watcher.WaitAndGetTitle(), title_when_loaded);
  }

  // 3) Navigate to B.
  EXPECT_TRUE(NavigateToURL(shell(), url_b));
  RenderFrameHostImpl* rfh_b = current_frame_host();
  RenderFrameDeletedObserver delete_observer_rfh_b(rfh_b);
  EXPECT_FALSE(delete_observer_rfh_a.deleted());
  EXPECT_TRUE(rfh_a->IsInBackForwardCache());
  EXPECT_FALSE(rfh_b->IsInBackForwardCache());

  // 4) Go back to A and check the title again.
  ASSERT_TRUE(HistoryGoBack(web_contents()));
  EXPECT_FALSE(delete_observer_rfh_a.deleted());
  EXPECT_FALSE(delete_observer_rfh_b.deleted());
  EXPECT_EQ(rfh_a, current_frame_host());
  EXPECT_TRUE(rfh_b->IsInBackForwardCache());
  {
    std::u16string title_when_loaded = u"loaded!";
    TitleWatcher title_watcher(web_contents(), title_when_loaded);
    EXPECT_EQ(title_watcher.WaitAndGetTitle(), title_when_loaded);
  }
}

IN_PROC_BROWSER_TEST_F(BackForwardCacheBrowserTest,
                       DoesNotCacheIfMainFrameStillLoading) {
  net::test_server::ControllableHttpResponse response(embedded_test_server(),
                                                      "/main_document");
  ASSERT_TRUE(embedded_test_server()->Start());

  // 1) Navigate to a page that doesn't finish loading.
  GURL url(embedded_test_server()->GetURL("a.com", "/main_document"));
  TestNavigationManager navigation_manager(shell()->web_contents(), url);
  shell()->LoadURL(url);

  // The navigation starts.
  EXPECT_TRUE(navigation_manager.WaitForRequestStart());
  navigation_manager.ResumeNavigation();

  // The server sends the first part of the response and waits.
  response.WaitForRequest();
  response.Send(
      "HTTP/1.1 200 OK\r\n"
      "Content-Type: text/html; charset=utf-8\r\n"
      "\r\n"
      "<html><body> ... ");

  // The navigation finishes while the body is still loading.
  navigation_manager.WaitForNavigationFinished();
  RenderFrameDeletedObserver delete_observer_rfh_a(current_frame_host());

  // 2) Navigate away.
  shell()->LoadURL(embedded_test_server()->GetURL("b.com", "/title1.html"));

  // The page was still loading when we navigated away, so it shouldn't have
  // been cached.
  delete_observer_rfh_a.WaitUntilDeleted();

  // 3) Go back.
  web_contents()->GetController().GoBack();
  EXPECT_FALSE(WaitForLoadStop(shell()->web_contents()));
  ExpectNotRestored({NotRestoredReason::kLoading}, {}, {}, {}, {}, FROM_HERE);
}

IN_PROC_BROWSER_TEST_F(BackForwardCacheBrowserTest,
                       DoesNotCacheLoadingSubframe) {
  net::test_server::ControllableHttpResponse response(embedded_test_server(),
                                                      "/controlled");
  ASSERT_TRUE(embedded_test_server()->Start());

  // 1) Navigate to a page with an iframe that loads forever.
  GURL url(embedded_test_server()->GetURL(
      "a.com", "/back_forward_cache/controllable_subframe.html"));
  TestNavigationManager navigation_manager(shell()->web_contents(), url);
  shell()->LoadURL(url);

  // The navigation finishes while the iframe is still loading.
  navigation_manager.WaitForNavigationFinished();

  // Wait for the iframe request to arrive, and leave it hanging with no
  // response.
  response.WaitForRequest();

  RenderFrameHostImpl* rfh_a = current_frame_host();
  RenderFrameDeletedObserver delete_observer_rfh_a(rfh_a);

  // 2) Navigate away.
  shell()->LoadURL(embedded_test_server()->GetURL("b.com", "/title1.html"));
  EXPECT_TRUE(WaitForLoadStop(shell()->web_contents()));

  // The page should not have been added to cache, since it had a subframe that
  // was still loading at the time it was navigated away from.
  delete_observer_rfh_a.WaitUntilDeleted();

  // 3) Go back.
  ASSERT_TRUE(HistoryGoBack(web_contents()));
  ExpectNotRestored(
      {
          NotRestoredReason::kLoading,
          NotRestoredReason::kSubframeIsNavigating,
      },
      {}, {}, {}, {}, FROM_HERE);
}

IN_PROC_BROWSER_TEST_F(BackForwardCacheBrowserTest,
                       DoesNotCacheLoadingSubframeOfSubframe) {
  net::test_server::ControllableHttpResponse response(embedded_test_server(),
                                                      "/controlled");
  ASSERT_TRUE(embedded_test_server()->Start());

  // 1) Navigate to a page with an iframe that contains yet another iframe, that
  // hangs while loading.
  GURL url(embedded_test_server()->GetURL(
      "a.com", "/back_forward_cache/controllable_subframe_of_subframe.html"));
  TestNavigationManager navigation_manager(shell()->web_contents(), url);
  shell()->LoadURL(url);

  // The navigation finishes while the iframe within an iframe is still loading.
  navigation_manager.WaitForNavigationFinished();

  // Wait for the innermost iframe request to arrive, and leave it hanging with
  // no response.
  response.WaitForRequest();

  RenderFrameHostImpl* rfh_a = current_frame_host();
  RenderFrameDeletedObserver delete_rfh_a(rfh_a);

  // 2) Navigate away.
  shell()->LoadURL(embedded_test_server()->GetURL("b.com", "/title1.html"));
  EXPECT_TRUE(WaitForLoadStop(shell()->web_contents()));

  // The page should not have been added to the cache, since it had an iframe
  // that was still loading at the time it was navigated away from.
  delete_rfh_a.WaitUntilDeleted();

  // 3) Go back.
  ASSERT_TRUE(HistoryGoBack(web_contents()));
  ExpectNotRestored(
      {
          NotRestoredReason::kLoading,
          NotRestoredReason::kSubframeIsNavigating,
      },
      {}, {}, {}, {}, FROM_HERE);
}

IN_PROC_BROWSER_TEST_F(BackForwardCacheBrowserTest, DoesNotCacheIfHttpError) {
  ASSERT_TRUE(embedded_test_server()->Start());

  GURL error_url(embedded_test_server()->GetURL("a.com", "/page404.html"));
  GURL url(embedded_test_server()->GetURL("b.com", "/title1.html"));

  // Navigate to an error page.
  EXPECT_TRUE(NavigateToURL(shell(), error_url));
  EXPECT_EQ(net::HTTP_NOT_FOUND, current_frame_host()->last_http_status_code());
  RenderFrameDeletedObserver delete_rfh_a(current_frame_host());

  // Navigate away.
  EXPECT_TRUE(NavigateToURL(shell(), url));

  // The page did not return 200 (OK), so it shouldn't have been cached.
  delete_rfh_a.WaitUntilDeleted();

  // Go back.
  ASSERT_TRUE(HistoryGoBack(web_contents()));
  ExpectNotRestored({NotRestoredReason::kHTTPStatusNotOK}, {}, {}, {}, {},
                    FROM_HERE);
}

IN_PROC_BROWSER_TEST_F(BackForwardCacheBrowserTest,
                       DoesNotCacheIfPageUnreachable) {
  ASSERT_TRUE(embedded_test_server()->Start());

  GURL error_url(embedded_test_server()->GetURL("a.com", "/empty.html"));
  GURL url(embedded_test_server()->GetURL("b.com", "/title1.html"));

  std::unique_ptr<URLLoaderInterceptor> url_interceptor =
      URLLoaderInterceptor::SetupRequestFailForURL(error_url,
                                                   net::ERR_DNS_TIMED_OUT);

  // Start with a successful navigation to a document.
  EXPECT_TRUE(NavigateToURL(shell(), url));
  EXPECT_EQ(net::HTTP_OK, current_frame_host()->last_http_status_code());

  // Navigate to an error page.
  NavigationHandleObserver observer(shell()->web_contents(), error_url);
  EXPECT_FALSE(NavigateToURL(shell(), error_url));
  EXPECT_TRUE(observer.is_error());
  EXPECT_EQ(net::ERR_DNS_TIMED_OUT, observer.net_error_code());
  EXPECT_EQ(GURL(kUnreachableWebDataURL), shell()
                                              ->web_contents()
                                              ->GetPrimaryMainFrame()
                                              ->GetSiteInstance()
                                              ->GetSiteURL());
  EXPECT_EQ(net::OK, current_frame_host()->last_http_status_code());

  RenderFrameDeletedObserver delete_rfh_a(current_frame_host());

  // Navigate away.
  EXPECT_TRUE(NavigateToURL(shell(), url));

  // The page had a networking error, so it shouldn't have been cached.
  delete_rfh_a.WaitUntilDeleted();

  // Go back.
  web_contents()->GetController().GoBack();
  EXPECT_FALSE(WaitForLoadStop(shell()->web_contents()));
  ExpectNotRestored(
      {NotRestoredReason::kHTTPStatusNotOK, NotRestoredReason::kNoResponseHead,
       NotRestoredReason::kErrorDocument},
      {}, {}, {}, {}, FROM_HERE);
}

// Tests the events are fired when going back from the cache.
IN_PROC_BROWSER_TEST_F(BackForwardCacheBrowserTest, Events) {
  ASSERT_TRUE(embedded_test_server()->Start());
  GURL url_a(embedded_test_server()->GetURL(
      "a.com", "/back_forward_cache/record_events.html"));
  GURL url_b(embedded_test_server()->GetURL(
      "b.com", "/back_forward_cache/record_events.html"));

  // 1) Navigate to A.
  EXPECT_TRUE(NavigateToURL(shell(), url_a));
  RenderFrameHostImplWrapper rfh_a(current_frame_host());

  // At A, a page-show event is recorded for the first loading.
  MatchEventList(rfh_a.get(), ListValueOf("window.pageshow"));

  constexpr char kEventPageShowPersisted[] = "Event.PageShow.Persisted";

  content::FetchHistogramsFromChildProcesses();
  EXPECT_THAT(
      histogram_tester().GetAllSamples(kEventPageShowPersisted),
      testing::UnorderedElementsAre(base::Bucket(
          static_cast<int>(blink::EventPageShowPersisted::kNoInRenderer), 1)));

  // 2) Navigate to B.
  EXPECT_TRUE(NavigateToURL(shell(), url_b));
  RenderFrameHostImplWrapper rfh_b(current_frame_host());

  EXPECT_FALSE(rfh_a.IsRenderFrameDeleted());
  EXPECT_FALSE(rfh_b.IsRenderFrameDeleted());
  EXPECT_TRUE(rfh_a->IsInBackForwardCache());
  EXPECT_FALSE(rfh_b->IsInBackForwardCache());
  // TODO(yuzus): Post message to the frozen page, and make sure that the
  // messages arrive after the page visibility events, not before them.

  // As |rfh_a| is in back-forward cache, we cannot get the event list of A.
  // At B, a page-show event is recorded for the first loading.
  MatchEventList(rfh_b.get(), ListValueOf("window.pageshow"));
  content::FetchHistogramsFromChildProcesses();
  EXPECT_THAT(
      histogram_tester().GetAllSamples(kEventPageShowPersisted),
      testing::UnorderedElementsAre(base::Bucket(
          static_cast<int>(blink::EventPageShowPersisted::kNoInRenderer), 2)));

  // 3) Go back to A. Confirm that expected events are fired.
  ASSERT_TRUE(HistoryGoBack(web_contents()));
  EXPECT_FALSE(rfh_a.IsRenderFrameDeleted());
  EXPECT_FALSE(rfh_b.IsRenderFrameDeleted());
  EXPECT_EQ(rfh_a.get(), current_frame_host());
  // visibilitychange events are added twice per each because it is fired for
  // both window and document.
  MatchEventList(
      rfh_a.get(),
      ListValueOf("window.pageshow", "window.pagehide.persisted",
                  "document.visibilitychange", "window.visibilitychange",
                  "document.freeze", "document.resume",
                  "document.visibilitychange", "window.visibilitychange",
                  "window.pageshow.persisted"));

  content::FetchHistogramsFromChildProcesses();
  EXPECT_THAT(
      histogram_tester().GetAllSamples(kEventPageShowPersisted),
      testing::UnorderedElementsAre(
          base::Bucket(
              static_cast<int>(blink::EventPageShowPersisted::kNoInRenderer),
              2),
          base::Bucket(
              static_cast<int>(blink::EventPageShowPersisted::kYesInBrowser),
              1),
          base::Bucket(
              static_cast<int>(blink::EventPageShowPersisted::kYesInRenderer),
              1),
          base::Bucket(
              static_cast<int>(
                  blink::EventPageShowPersisted::kBrowserYesInRenderer),
              1),
          base::Bucket(
              static_cast<int>(
                  blink::EventPageShowPersisted::kBrowserYesInRendererWithPage),
              1),
          base::Bucket(
              static_cast<int>(blink::EventPageShowPersisted::kYesInBrowserAck),
              1),
          base::Bucket(
              static_cast<int>(
                  blink::EventPageShowPersisted::
                      kYesInBrowser_BackForwardCache_WillCommitNavigationToCachedEntry),
              1),
          base::Bucket(
              static_cast<int>(
                  blink::EventPageShowPersisted::
                      kYesInBrowser_BackForwardCache_RestoreEntry_Attempt),
              1),
          base::Bucket(
              static_cast<int>(
                  blink::EventPageShowPersisted::
                      kYesInBrowser_BackForwardCache_RestoreEntry_Succeed),
              1),
          base::Bucket(
              static_cast<int>(
                  blink::EventPageShowPersisted::
                      kYesInBrowser_RenderFrameHostManager_CommitPending),
              1)));
}

// Tests the events are fired for subframes when going back from the cache.
// Test case: a(b) -> c -> a(b)
IN_PROC_BROWSER_TEST_F(BackForwardCacheBrowserTest, EventsForSubframes) {
  ASSERT_TRUE(embedded_test_server()->Start());
  GURL url_a(embedded_test_server()->GetURL(
      "a.com", "/cross_site_iframe_factory.html?a(b)"));
  GURL url_c(embedded_test_server()->GetURL("c.com", "/title1.html"));

  // 1) Navigate to A(B).
  EXPECT_TRUE(NavigateToURL(shell(), url_a));
  RenderFrameHostImpl* rfh_a = current_frame_host();
  RenderFrameHostImpl* rfh_b = rfh_a->child_at(0)->current_frame_host();
  RenderFrameDeletedObserver delete_observer_rfh_a(rfh_a);
  RenderFrameDeletedObserver delete_observer_rfh_b(rfh_b);
  StartRecordingEvents(rfh_a);
  StartRecordingEvents(rfh_b);

  // 2) Navigate to C.
  EXPECT_TRUE(NavigateToURL(shell(), url_c));
  RenderFrameHostImpl* rfh_c = current_frame_host();
  RenderFrameDeletedObserver delete_observer_rfh_c(rfh_c);
  EXPECT_FALSE(delete_observer_rfh_a.deleted());
  EXPECT_FALSE(delete_observer_rfh_b.deleted());
  EXPECT_TRUE(rfh_a->IsInBackForwardCache());
  EXPECT_TRUE(rfh_b->IsInBackForwardCache());
  EXPECT_FALSE(rfh_c->IsInBackForwardCache());
  // TODO(yuzus): Post message to the frozen page, and make sure that the
  // messages arrive after the page visibility events, not before them.

  // 3) Go back to A(B). Confirm that expected events are fired on the subframe.
  ASSERT_TRUE(HistoryGoBack(web_contents()));
  EXPECT_FALSE(delete_observer_rfh_a.deleted());
  EXPECT_FALSE(delete_observer_rfh_b.deleted());
  EXPECT_FALSE(delete_observer_rfh_c.deleted());
  EXPECT_EQ(rfh_a, current_frame_host());
  EXPECT_FALSE(rfh_a->IsInBackForwardCache());
  EXPECT_FALSE(rfh_b->IsInBackForwardCache());
  EXPECT_TRUE(rfh_c->IsInBackForwardCache());
  // visibilitychange events are added twice per each because it is fired for
  // both window and document.
  MatchEventList(
      rfh_a,
      ListValueOf("window.pagehide.persisted", "document.visibilitychange",
                  "window.visibilitychange", "document.freeze",
                  "document.resume", "document.visibilitychange",
                  "window.visibilitychange", "window.pageshow.persisted"));
  MatchEventList(
      rfh_b,
      ListValueOf("window.pagehide.persisted", "document.visibilitychange",
                  "window.visibilitychange", "document.freeze",
                  "document.resume", "document.visibilitychange",
                  "window.visibilitychange", "window.pageshow.persisted"));
}

// Tests the events are fired when going back from the cache.
// Same as: BackForwardCacheBrowserTest.Events, but with a document-initiated
// navigation. This is a regression test for https://crbug.com/1000324
IN_PROC_BROWSER_TEST_F(BackForwardCacheBrowserTest,
                       EventsAfterDocumentInitiatedNavigation) {
  ASSERT_TRUE(embedded_test_server()->Start());
  GURL url_a(embedded_test_server()->GetURL("a.com", "/title1.html"));
  GURL url_b(embedded_test_server()->GetURL("b.com", "/title1.html"));

  // 1) Navigate to A.
  EXPECT_TRUE(NavigateToURL(shell(), url_a));
  RenderFrameHostImpl* rfh_a = current_frame_host();
  RenderFrameDeletedObserver delete_observer_rfh_a(rfh_a);
  StartRecordingEvents(rfh_a);

  // 2) Navigate to B.
  ASSERT_TRUE(NavigateToURLFromRenderer(shell(), url_b));
  RenderFrameHostImpl* rfh_b = current_frame_host();
  RenderFrameDeletedObserver delete_observer_rfh_b(rfh_b);

  EXPECT_FALSE(delete_observer_rfh_a.deleted());
  EXPECT_FALSE(delete_observer_rfh_b.deleted());
  EXPECT_TRUE(rfh_a->IsInBackForwardCache());
  EXPECT_FALSE(rfh_b->IsInBackForwardCache());
  // TODO(yuzus): Post message to the frozen page, and make sure that the
  // messages arrive after the page visibility events, not before them.

  // 3) Go back to A. Confirm that expected events are fired.
  ASSERT_TRUE(HistoryGoBack(web_contents()));
  EXPECT_FALSE(delete_observer_rfh_a.deleted());
  EXPECT_FALSE(delete_observer_rfh_b.deleted());
  EXPECT_EQ(rfh_a, current_frame_host());
  // visibilitychange events are added twice per each because it is fired for
  // both window and document.
  MatchEventList(
      rfh_a,
      ListValueOf("window.pagehide.persisted", "document.visibilitychange",
                  "window.visibilitychange", "document.freeze",
                  "document.resume", "document.visibilitychange",
                  "window.visibilitychange", "window.pageshow.persisted"));
}

// Track the events dispatched when a page is deemed ineligible for back-forward
// cache after we've dispatched the 'pagehide' event with persisted set to true.
IN_PROC_BROWSER_TEST_F(BackForwardCacheBrowserTest,
                       EventsForPageIneligibleAfterPagehidePersisted) {
  ASSERT_TRUE(CreateHttpsServer()->Start());
  GURL url_1(https_server()->GetURL("a.com", "/title1.html"));
  GURL url_2(https_server()->GetURL("a.com", "/title2.html"));

  // 1) Navigate to |url_1|.
  EXPECT_TRUE(NavigateToURL(shell(), url_1));
  RenderFrameHostImpl* rfh_1 = current_frame_host();
  RenderFrameDeletedObserver delete_observer_rfh_1(rfh_1);
  // 2) Use BroadcastChannel (a non-sticky blocklisted feature), so that we
  // would still do a RFH swap on same-site navigation and fire the 'pagehide'
  // event during commit of the new page with 'persisted' set to true, but the
  // page will not be eligible for back-forward cache after commit.
  EXPECT_TRUE(ExecJs(rfh_1, "window.foo = new BroadcastChannel('foo');"));

  EXPECT_TRUE(ExecJs(rfh_1, R"(
    window.onpagehide = (e) => {
      console.log("onagepagehide", e.persisted);
      localStorage.setItem('pagehide_persisted',
        e.persisted ? 'true' : 'false');
    }
    document.onvisibilitychange = () => {
      localStorage.setItem('visibilitychange',
        document.visibilityState);
    }
  )"));

  // 3) Navigate to |url_2|.
  EXPECT_TRUE(NavigateToURL(shell(), url_2));
  // |rfh_1| will not get into the back-forward cache and eventually get deleted
  // because it uses a blocklisted feature.
  delete_observer_rfh_1.WaitUntilDeleted();

  EXPECT_EQ("true",
            GetLocalStorage(current_frame_host(), "pagehide_persisted"));
  EXPECT_EQ("hidden",
            GetLocalStorage(current_frame_host(), "visibilitychange"));
}

// Track the events dispatched when a page is deemed ineligible for back-forward
// cache before we've dispatched the pagehide event on it.
IN_PROC_BROWSER_TEST_F(BackForwardCacheBrowserTest,
                       EventsForPageIneligibleBeforePagehide) {
  ASSERT_TRUE(CreateHttpsServer()->Start());
  GURL url_1(https_server()->GetURL("a.com", "/title1.html"));
  GURL url_2(https_server()->GetURL("b.com", "/title2.html"));

  // 1) Navigate to |url_1|.
  EXPECT_TRUE(NavigateToURL(shell(), url_1));
  RenderFrameHostImpl* rfh_1 = current_frame_host();
  RenderFrameDeletedObserver delete_observer_rfh_1(rfh_1);
  // 2) Use a dummy sticky blocklisted feature, so that the page is known to be
  // ineligible for bfcache at commit time, before we dispatch pagehide event.
  rfh_1->UseDummyStickyBackForwardCacheDisablingFeatureForTesting();

  EXPECT_TRUE(ExecJs(rfh_1, R"(
    window.onpagehide = (e) => {
      if (!e.persisted) {
        window.domAutomationController.send('pagehide.not_persisted');
      }
    }
    document.onvisibilitychange = () => {
      if (document.visibilityState == 'hidden') {
        window.domAutomationController.send('visibilitychange.hidden');
      }
    }
  )"));

  DOMMessageQueue dom_message_queue(shell()->web_contents());
  // 3) Navigate to |url_2|.
  EXPECT_TRUE(NavigateToURL(shell(), url_2));
  // |rfh_1| will not get into the back-forward cache and eventually get deleted
  // because it uses a blocklisted feature.
  delete_observer_rfh_1.WaitUntilDeleted();

  // "pagehide", "visibilitychange", and "unload" events will be dispatched.
  int num_messages_received = 0;
  std::string expected_messages[] = {"\"pagehide.not_persisted\"",
                                     "\"visibilitychange.hidden\""};
  std::string message;
  while (dom_message_queue.PopMessage(&message)) {
    EXPECT_EQ(expected_messages[num_messages_received], message);
    num_messages_received++;
  }
  EXPECT_EQ(num_messages_received, 2);
}

enum class TestFrameType {
  kMainFrame,
  kSubFrame,
};

enum class StickinessType {
  kSticky,
  kNonSticky,
};

class BackForwardCacheBrowserTestWithVaryingFrameAndFeatureStickinessType
    : public BackForwardCacheBrowserTest,
      public ::testing::WithParamInterface<
          testing::tuple<TestFrameType, StickinessType>> {};

INSTANTIATE_TEST_SUITE_P(
    All,
    BackForwardCacheBrowserTestWithVaryingFrameAndFeatureStickinessType,
    ::testing::Combine(::testing::Values(TestFrameType::kMainFrame,
                                         TestFrameType::kSubFrame),
                       ::testing::Values(StickinessType::kSticky,
                                         StickinessType::kNonSticky)));

// Test pagehide's persisted value and whether the page can be BFCached when a
// sticky/non-sticky feature is used on the mainframe/subframe.
// TODO(crbug.com/1277888): flaky.
IN_PROC_BROWSER_TEST_P(
    BackForwardCacheBrowserTestWithVaryingFrameAndFeatureStickinessType,
    DISABLED_TestPagehidePersistedValue) {
  ASSERT_TRUE(embedded_test_server()->Start());
  GURL url_a(embedded_test_server()->GetURL(
      "a.com", "/cross_site_iframe_factory.html?a(b)"));
  GURL url_b(embedded_test_server()->GetURL("b.com", "/title1.html"));

  // 1) Navigate to A(B).
  EXPECT_TRUE(NavigateToURL(shell(), url_a));
  TestFrameType parameter_frame = std::get<0>(GetParam());
  StickinessType use_sticky_feature = std::get<1>(GetParam());

  // Depending on the parameter, pick the mainframe or subframe to add a
  // blocking feature.
  RenderFrameHostImplWrapper rfh_with_blocking_feature(
      parameter_frame == TestFrameType::kSubFrame
          ? current_frame_host()->child_at(0)->current_frame_host()
          : current_frame_host());

  // 2) Mark the document as using a feature that's either sticky or non-sticky,
  // depending on the test parameter.
  if (use_sticky_feature == StickinessType::kSticky) {
    rfh_with_blocking_feature.get()
        ->UseDummyStickyBackForwardCacheDisablingFeatureForTesting();
  } else {
    EXPECT_TRUE(ExecJs(rfh_with_blocking_feature.get(),
                       "window.foo = new BroadcastChannel('foo');"));
  }

  // 3) Install the pagehide handler in A to know pagehide.persisted status
  // after navigating to B.
  EXPECT_TRUE(ExecJs(current_frame_host(), R"(
    window.onpagehide = (e) => {
      localStorage.setItem('pagehide_persisted',
        e.persisted ? 'true' : 'false');
    }
  )"));

  // 4) Navigate to B.
  EXPECT_TRUE(NavigateToURL(shell(), url_b));

  // 5) Go back to the previous page.
  ASSERT_TRUE(HistoryGoBack(web_contents()));

  // 6) If the page is using a sticky feature at pagehide, it can never be put
  // into BFCache no matter what pagehide does, so pagehide's persisted is
  // false. Meanwhile, if the page is using a non-sticky feature at pagehide, it
  // can still be put into BFCache if the pagehide event removes the feature's
  // usage, so pagehide's persisted is true, since the page might still get into
  // BFCache.
  EXPECT_EQ(use_sticky_feature == StickinessType::kSticky ? "false" : "true",
            GetLocalStorage(current_frame_host(), "pagehide_persisted"));

  // 7) Confirm that the page was not restored from the BFCache in both the
  // sticky and non-sticky cases.
  blink::scheduler::WebSchedulerTrackedFeature expected_feature =
      (use_sticky_feature == StickinessType::kSticky)
          ? blink::scheduler::WebSchedulerTrackedFeature::kDummy
          : blink::scheduler::WebSchedulerTrackedFeature::kBroadcastChannel;
  ExpectNotRestored({NotRestoredReason::kBlocklistedFeatures},
                    {expected_feature}, {}, {}, {}, FROM_HERE);
}

IN_PROC_BROWSER_TEST_F(HighCacheSizeBackForwardCacheBrowserTest,
                       CanCacheMultiplesPagesOnSameDomain) {
  ASSERT_TRUE(embedded_test_server()->Start());
  GURL url_a1(embedded_test_server()->GetURL("a.com", "/title1.html"));
  GURL url_b2(embedded_test_server()->GetURL("b.com", "/title1.html"));
  GURL url_a3(embedded_test_server()->GetURL("a.com", "/title2.html"));
  GURL url_b4(embedded_test_server()->GetURL("b.com", "/title2.html"));

  // 1) Navigate to A1.
  EXPECT_TRUE(NavigateToURL(shell(), url_a1));
  RenderFrameHostImpl* rfh_a1 = current_frame_host();

  // 2) Navigate to B2.
  EXPECT_TRUE(NavigateToURL(shell(), url_b2));
  RenderFrameHostImpl* rfh_b2 = current_frame_host();
  EXPECT_TRUE(rfh_a1->IsInBackForwardCache());

  // 3) Navigate to A3.
  EXPECT_TRUE(NavigateToURL(shell(), url_a3));
  RenderFrameHostImpl* rfh_a3 = current_frame_host();
  EXPECT_TRUE(rfh_a1->IsInBackForwardCache());
  EXPECT_TRUE(rfh_b2->IsInBackForwardCache());
  // A1 and A3 shouldn't be treated as the same site instance.
  EXPECT_NE(rfh_a1->GetSiteInstance(), rfh_a3->GetSiteInstance());

  // 4) Navigate to B4.
  // Make sure we can store A1 and A3 in the cache at the same time.
  EXPECT_TRUE(NavigateToURL(shell(), url_b4));
  RenderFrameHostImpl* rfh_b4 = current_frame_host();
  EXPECT_TRUE(rfh_a1->IsInBackForwardCache());
  EXPECT_TRUE(rfh_b2->IsInBackForwardCache());
  EXPECT_TRUE(rfh_a3->IsInBackForwardCache());

  // 5) Go back to A3.
  // Make sure we can restore A3, while A1 remains in the cache.
  ASSERT_TRUE(HistoryGoBack(web_contents()));
  EXPECT_TRUE(rfh_a1->IsInBackForwardCache());
  EXPECT_TRUE(rfh_b2->IsInBackForwardCache());
  EXPECT_TRUE(rfh_b4->IsInBackForwardCache());
  EXPECT_EQ(rfh_a3, current_frame_host());
  // B2 and B4 shouldn't be treated as the same site instance.
  EXPECT_NE(rfh_b2->GetSiteInstance(), rfh_b4->GetSiteInstance());

  // 6) Do a history navigation back to A1.
  // Make sure we can restore A1, while coming from A3.
  ASSERT_TRUE(HistoryGoToIndex(web_contents(), 0));
  EXPECT_TRUE(rfh_b2->IsInBackForwardCache());
  EXPECT_TRUE(rfh_b4->IsInBackForwardCache());
  EXPECT_TRUE(rfh_a3->IsInBackForwardCache());
  EXPECT_EQ(rfh_a1, current_frame_host());
}

IN_PROC_BROWSER_TEST_F(BackForwardCacheBrowserTest, Encoding) {
  ASSERT_TRUE(embedded_test_server()->Start());
  GURL url_a(embedded_test_server()->GetURL(
      "a.com", "/back_forward_cache/charset_windows-1250.html"));
  GURL url_b(embedded_test_server()->GetURL(
      "b.com", "/back_forward_cache/charset_utf-8.html"));
  url::Origin origin_a = url::Origin::Create(url_a);
  url::Origin origin_b = url::Origin::Create(url_b);

  EXPECT_TRUE(NavigateToURL(shell(), url_a));
  RenderFrameHostImpl* rfh_a = current_frame_host();
  EXPECT_EQ(web_contents()->GetEncoding(), "windows-1250");

  EXPECT_TRUE(NavigateToURL(shell(), url_b));
  EXPECT_TRUE(rfh_a->IsInBackForwardCache());
  EXPECT_EQ(web_contents()->GetEncoding(), "UTF-8");

  ASSERT_TRUE(HistoryGoBack(web_contents()));
  EXPECT_EQ(web_contents()->GetEncoding(), "windows-1250");
}

IN_PROC_BROWSER_TEST_F(BackForwardCacheBrowserTest,
                       DoesNotCacheCrossSiteHttpPost) {
  SetupCrossSiteRedirector(embedded_test_server());
  ASSERT_TRUE(embedded_test_server()->Start());

  // Note we do a cross-site post because same-site navigations of any kind
  // aren't cached currently.
  GURL form_url(embedded_test_server()->GetURL(
      "a.com", "/form_that_posts_cross_site.html"));
  GURL redirect_target_url(embedded_test_server()->GetURL("x.com", "/echoall"));
  GURL url_b(embedded_test_server()->GetURL("b.com", "/title1.html"));

  // Navigate to the page with form that posts via 307 redirection to
  // |redirect_target_url| (cross-site from |form_url|).
  EXPECT_TRUE(NavigateToURL(shell(), form_url));

  // Submit the form.
  TestNavigationObserver form_post_observer(shell()->web_contents(), 1);
  EXPECT_TRUE(ExecJs(shell(), "document.getElementById('text-form').submit()"));
  form_post_observer.Wait();

  // Verify that we arrived at the expected, redirected location.
  EXPECT_EQ(redirect_target_url,
            shell()->web_contents()->GetLastCommittedURL());
  RenderFrameDeletedObserver delete_observer_rfh(current_frame_host());

  // Navigate away. |redirect_target_url|'s page should not be cached.
  EXPECT_TRUE(NavigateToURL(shell(), url_b));
  delete_observer_rfh.WaitUntilDeleted();
}

// On windows, the expected value is off by ~20ms. In order to get the
// feature out to canary, the test is disabled for WIN.
// TODO(crbug.com/1022191): Fix this for Win.
// TODO(crbug.com/1211428): Flaky on other platforms.
// Make sure we are exposing the duration between back navigation's
// navigationStart and the page's original navigationStart through pageshow
// event's timeStamp, and that we aren't modifying
// performance.timing.navigationStart.
IN_PROC_BROWSER_TEST_F(BackForwardCacheBrowserTest, DISABLED_NavigationStart) {
  ASSERT_TRUE(embedded_test_server()->Start());
  GURL url_a(embedded_test_server()->GetURL(
      "a.com", "/back_forward_cache/record_navigation_start_time_stamp.html"));
  GURL url_b(embedded_test_server()->GetURL("b.com", "/title1.html"));

  // 1) Navigate to A.
  EXPECT_TRUE(NavigateToURL(shell(), url_a));
  RenderFrameHostImpl* rfh_a = current_frame_host();
  RenderFrameDeletedObserver delete_observer_rfh_a(rfh_a);

  double initial_page_show_time_stamp =
      EvalJs(shell(), "window.initialPageShowTimeStamp").ExtractDouble();
  EXPECT_DOUBLE_EQ(
      initial_page_show_time_stamp,
      EvalJs(shell(), "window.latestPageShowTimeStamp").ExtractDouble());
  double initial_navigation_start =
      EvalJs(shell(), "window.initialNavigationStart").ExtractDouble();

  // 2) Navigate to B. A should be in the back forward cache.
  EXPECT_TRUE(NavigateToURL(shell(), url_b));
  EXPECT_FALSE(delete_observer_rfh_a.deleted());
  EXPECT_TRUE(rfh_a->IsInBackForwardCache());

  // 3) Navigate back and expect everything to be restored.
  NavigationHandleObserver observer(web_contents(), url_a);
  base::TimeTicks time_before_navigation = base::TimeTicks::Now();
  double js_time_before_navigation =
      EvalJs(shell(), "performance.now()").ExtractDouble();
  ASSERT_TRUE(HistoryGoBack(web_contents()));
  base::TimeTicks time_after_navigation = base::TimeTicks::Now();
  double js_time_after_navigation =
      EvalJs(shell(), "performance.now()").ExtractDouble();

  // The navigation start time should be between the time we saved just before
  // calling GoBack() and the time we saved just after calling GoBack().
  base::TimeTicks back_navigation_start = observer.navigation_start();
  EXPECT_LT(time_before_navigation, back_navigation_start);
  EXPECT_GT(time_after_navigation, back_navigation_start);

  // Check JS values. window.initialNavigationStart should not change.
  EXPECT_DOUBLE_EQ(
      initial_navigation_start,
      EvalJs(shell(), "window.initialNavigationStart").ExtractDouble());
  // performance.timing.navigationStart should not change.
  EXPECT_DOUBLE_EQ(
      initial_navigation_start,
      EvalJs(shell(), "performance.timing.navigationStart").ExtractDouble());
  // window.initialPageShowTimeStamp should not change.
  EXPECT_DOUBLE_EQ(
      initial_page_show_time_stamp,
      EvalJs(shell(), "window.initialPageShowTimeStamp").ExtractDouble());
  // window.latestPageShowTimeStamp should be updated with the timestamp of the
  // last pageshow event, which occurs after the page is restored. This should
  // be greater than the initial pageshow event's timestamp.
  double latest_page_show_time_stamp =
      EvalJs(shell(), "window.latestPageShowTimeStamp").ExtractDouble();
  EXPECT_LT(initial_page_show_time_stamp, latest_page_show_time_stamp);

  // |latest_page_show_time_stamp| should be the duration between initial
  // navigation start and |back_navigation_start|. Note that since
  // performance.timing.navigationStart returns a 64-bit integer instead of
  // double, we might be losing somewhere between 0 to 1 milliseconds of
  // precision, hence the usage of EXPECT_NEAR.
  EXPECT_NEAR(
      (back_navigation_start - base::TimeTicks::UnixEpoch()).InMillisecondsF(),
      latest_page_show_time_stamp + initial_navigation_start, 1.0);
  // Expect that the back navigation start value calculated from the JS results
  // are between time taken before & after navigation, just like
  // |before_navigation_start|.
  EXPECT_LT(js_time_before_navigation, latest_page_show_time_stamp);
  EXPECT_GT(js_time_after_navigation, latest_page_show_time_stamp);
}

IN_PROC_BROWSER_TEST_F(BackForwardCacheBrowserTest,
                       CanUseCacheWhenNavigatingAwayToErrorPage) {
  ASSERT_TRUE(embedded_test_server()->Start());

  GURL url_a(embedded_test_server()->GetURL("a.com", "/title1.html"));
  GURL error_url(embedded_test_server()->GetURL("b.com", "/empty.html"));
  auto url_interceptor = URLLoaderInterceptor::SetupRequestFailForURL(
      error_url, net::ERR_DNS_TIMED_OUT);

  // 1) Navigate to A.
  EXPECT_TRUE(NavigateToURL(shell(), url_a));
  RenderFrameHostImpl* rfh_a = current_frame_host();

  // 2) Navigate to an error page and expect the old page to be stored in
  // bfcache.
  EXPECT_FALSE(NavigateToURL(shell(), error_url));
  EXPECT_TRUE(rfh_a->IsInBackForwardCache());

  // 3) Navigate back and expect the page to be restored from bfcache.
  ASSERT_TRUE(HistoryGoBack(web_contents()));
}

// RenderFrameHostImpl::coep_reporter() must be preserved when doing a back
// navigation using the BackForwardCache.
// Regression test for https://crbug.com/1102285.
IN_PROC_BROWSER_TEST_F(BackForwardCacheBrowserTest, CoepReporter) {
  ASSERT_TRUE(CreateHttpsServer()->Start());
  GURL url_a(https_server()->GetURL("a.com",
                                    "/set-header?"
                                    "Cross-Origin-Embedder-Policy-Report-Only: "
                                    "require-corp; report-to%3d\"a\""));
  GURL url_b(https_server()->GetURL("b.com", "/title1.html"));

  // Navigate to a document that set RenderFrameHostImpl::coep_reporter().
  EXPECT_TRUE(NavigateToURL(shell(), url_a));
  RenderFrameHostImpl* rfh_a = current_frame_host();
  EXPECT_TRUE(rfh_a->coep_reporter());

  // Navigate away and back using the BackForwardCache. The
  // RenderFrameHostImpl::coep_reporter() must still be there.
  RenderFrameDeletedObserver delete_observer_rfh_a(rfh_a);
  EXPECT_TRUE(NavigateToURL(shell(), url_b));
  ASSERT_TRUE(HistoryGoBack(web_contents()));
  EXPECT_FALSE(delete_observer_rfh_a.deleted());
  EXPECT_EQ(rfh_a, current_frame_host());

  EXPECT_TRUE(rfh_a->coep_reporter());
}

// RenderFrameHostImpl::coop_reporter() must be preserved when doing a back
// navigation using the BackForwardCache.
// Regression test for https://crbug.com/1102285.
IN_PROC_BROWSER_TEST_F(BackForwardCacheBrowserTest, CoopReporter) {
  ASSERT_TRUE(CreateHttpsServer()->Start());
  GURL url_a(https_server()->GetURL("a.com",
                                    "/set-header?"
                                    "Cross-Origin-Opener-Policy-Report-Only: "
                                    "same-origin; report-to%3d\"a\""));
  GURL url_b(https_server()->GetURL("b.com", "/title1.html"));

  // Navigate to a document that set RenderFrameHostImpl::coop_reporter().
  EXPECT_TRUE(NavigateToURL(shell(), url_a));
  RenderFrameHostImpl* rfh_a = current_frame_host();
  EXPECT_TRUE(rfh_a->coop_access_report_manager()->coop_reporter());

  // Navigate away and back using the BackForwardCache. The
  // RenderFrameHostImpl::coop_reporter() must still be there.
  RenderFrameDeletedObserver delete_observer_rfh_a(rfh_a);
  EXPECT_TRUE(NavigateToURL(shell(), url_b));
  ASSERT_TRUE(HistoryGoBack(web_contents()));
  EXPECT_FALSE(delete_observer_rfh_a.deleted());
  EXPECT_EQ(rfh_a, current_frame_host());

  EXPECT_TRUE(rfh_a->coop_access_report_manager()->coop_reporter());
}

// RenderFrameHostImpl::cross_origin_embedder_policy() must be preserved when
// doing a back navigation using the BackForwardCache.
// Regression test for https://crbug.com/1021846.
IN_PROC_BROWSER_TEST_F(BackForwardCacheBrowserTest, Coep) {
  ASSERT_TRUE(CreateHttpsServer()->Start());
  GURL url_a(https_server()->GetURL(
      "a.com", "/set-header?Cross-Origin-Embedder-Policy: require-corp"));
  GURL url_b(https_server()->GetURL("b.com", "/title1.html"));

  // Navigate to a document that sets COEP.
  network::CrossOriginEmbedderPolicy coep;
  coep.value = network::mojom::CrossOriginEmbedderPolicyValue::kRequireCorp;
  EXPECT_TRUE(NavigateToURL(shell(), url_a));
  RenderFrameHostImpl* rfh_a = current_frame_host();
  EXPECT_EQ(coep, rfh_a->cross_origin_embedder_policy());

  // Navigate away and back using the BackForwardCache.
  // RenderFrameHostImpl::cross_origin_embedder_policy() should return the same
  // result.
  RenderFrameDeletedObserver delete_observer_rfh_a(rfh_a);
  EXPECT_TRUE(NavigateToURL(shell(), url_b));
  ASSERT_TRUE(HistoryGoBack(web_contents()));
  EXPECT_FALSE(delete_observer_rfh_a.deleted());
  EXPECT_EQ(rfh_a, current_frame_host());

  EXPECT_EQ(coep, rfh_a->cross_origin_embedder_policy());
}

// Tests that pagehide and visibilitychange handlers of the old RFH are run for
// bfcached pages.
IN_PROC_BROWSER_TEST_F(BackForwardCacheBrowserTest,
                       PagehideAndVisibilitychangeRuns) {
  ASSERT_TRUE(embedded_test_server()->Start());
  GURL url_1(embedded_test_server()->GetURL("a.com", "/title1.html"));
  GURL url_2(embedded_test_server()->GetURL("b.com", "/title2.html"));
  GURL url_3(embedded_test_server()->GetURL("a.com", "/title2.html"));
  WebContentsImpl* web_contents =
      static_cast<WebContentsImpl*>(shell()->web_contents());

  // 1) Navigate to |url_1|.
  EXPECT_TRUE(NavigateToURL(shell(), url_1));
  RenderFrameHostImpl* main_frame_1 = web_contents->GetPrimaryMainFrame();

  // Create a pagehide handler that sets item "pagehide_storage" and a
  // visibilitychange handler that sets item "visibilitychange_storage" in
  // localStorage.
  EXPECT_TRUE(ExecJs(main_frame_1,
                     R"(
    localStorage.setItem('pagehide_storage', 'not_dispatched');
    var dispatched_pagehide = false;
    window.onpagehide = function(e) {
      if (dispatched_pagehide) {
        // We shouldn't dispatch pagehide more than once.
        localStorage.setItem('pagehide_storage', 'dispatched_more_than_once');
      } else if (!e.persisted) {
        localStorage.setItem('pagehide_storage', 'wrong_persisted');
      } else {
        localStorage.setItem('pagehide_storage', 'dispatched_once');
      }
      dispatched_pagehide = true;
    }
    localStorage.setItem('visibilitychange_storage', 'not_dispatched');
    var dispatched_visibilitychange = false;
    document.onvisibilitychange = function(e) {
      if (dispatched_visibilitychange) {
        // We shouldn't dispatch visibilitychange more than once.
        localStorage.setItem('visibilitychange_storage',
          'dispatched_more_than_once');
      } else if (document.visibilityState != 'hidden') {
        // We should dispatch the event when the visibilityState is 'hidden'.
        localStorage.setItem('visibilitychange_storage', 'not_hidden');
      } else {
        localStorage.setItem('visibilitychange_storage', 'dispatched_once');
      }
      dispatched_visibilitychange = true;
    }
  )"));

  // 2) Navigate cross-site to |url_2|. We need to navigate cross-site to make
  // sure we won't run pagehide and visibilitychange during new page's commit,
  // which is tested in ProactivelySwapBrowsingInstancesSameSiteTest.
  EXPECT_TRUE(NavigateToURL(shell(), url_2));

  // |main_frame_1| should be in the back-forward cache.
  EXPECT_TRUE(main_frame_1->IsInBackForwardCache());

  // 3) Navigate to |url_3| which is same-origin with |url_1|, so we can check
  // the localStorage values.
  EXPECT_TRUE(NavigateToURL(shell(), url_3));
  RenderFrameHostImpl* main_frame_3 = web_contents->GetPrimaryMainFrame();

  // Check that the value for 'pagehide_storage' and 'visibilitychange_storage'
  // are set correctly.
  EXPECT_EQ("dispatched_once",
            GetLocalStorage(main_frame_3, "pagehide_storage"));
  EXPECT_EQ("dispatched_once",
            GetLocalStorage(main_frame_3, "visibilitychange_storage"));
}

// Tests that the history value saved in the renderer is updated correctly when
// a page gets restored from the back-forward cache through browser-initiated
// navigation.
IN_PROC_BROWSER_TEST_F(BackForwardCacheBrowserTest,
                       RendererHistory_BrowserInitiated) {
  ASSERT_TRUE(embedded_test_server()->Start());
  GURL url1(embedded_test_server()->GetURL(
      "a.com", "/cross_site_iframe_factory.html?a(b)"));
  GURL url2(embedded_test_server()->GetURL("a.com", "/title1.html"));

  // 1) Go to |url1|, then |url2|. Both pages should have script to save the
  // history.length value when getting restored from the back-forward cache.
  EXPECT_TRUE(NavigateToURL(shell(), url1));
  FrameTreeNode* root = web_contents()->GetPrimaryFrameTree().root();
  FrameTreeNode* subframe = root->child_at(0);

  std::string restore_time_length_saver_script =
      "var resumeLength = -1;"
      "var pageshowLength = -1;"
      "document.onresume = () => {"
      "  resumeLength = history.length;"
      "};"
      "window.onpageshow  = () => {"
      "  pageshowLength = history.length;"
      "};";
  EXPECT_TRUE(ExecJs(root, restore_time_length_saver_script));
  EXPECT_TRUE(ExecJs(subframe, restore_time_length_saver_script));
  // We should have one history entry.
  EXPECT_EQ(EvalJs(root, "history.length").ExtractInt(), 1);
  EXPECT_EQ(EvalJs(subframe, "history.length").ExtractInt(), 1);

  EXPECT_TRUE(NavigateToURL(shell(), url2));
  EXPECT_TRUE(ExecJs(root, restore_time_length_saver_script));
  // We should have two history entries.
  EXPECT_EQ(EvalJs(root, "history.length").ExtractInt(), 2);

  // 2) Go back to |url1|, browser-initiated.
  ASSERT_TRUE(HistoryGoBack(web_contents()));
  EXPECT_EQ(web_contents()->GetLastCommittedURL(), url1);

  // We should still have two history entries, and recorded the correct length
  // when the 'resume' and 'pageshow' events were dispatched.
  EXPECT_EQ(EvalJs(root, "history.length").ExtractInt(), 2);
  EXPECT_EQ(EvalJs(root, "resumeLength").ExtractInt(), 2);
  EXPECT_EQ(EvalJs(root, "pageshowLength").ExtractInt(), 2);
  EXPECT_EQ(EvalJs(subframe, "history.length").ExtractInt(), 2);
  EXPECT_EQ(EvalJs(subframe, "resumeLength").ExtractInt(), 2);
  EXPECT_EQ(EvalJs(subframe, "pageshowLength").ExtractInt(), 2);

  // 3) Go forward to |url2|, browser-initiated.
  ASSERT_TRUE(HistoryGoForward(web_contents()));
  EXPECT_EQ(web_contents()->GetLastCommittedURL(), url2);

  // We should still have two history entries, and recorded the correct length
  // when the 'resume' and 'pageshow' events were dispatched.
  EXPECT_EQ(EvalJs(root, "history.length").ExtractInt(), 2);
  EXPECT_EQ(EvalJs(root, "resumeLength").ExtractInt(), 2);
  EXPECT_EQ(EvalJs(root, "pageshowLength").ExtractInt(), 2);
}

// Tests that the history value saved in the renderer is updated correctly when
// a page gets restored from the back-forward cache through renderer-initiated
// navigation.
IN_PROC_BROWSER_TEST_F(BackForwardCacheBrowserTest,
                       RendererHistory_RendererInitiated) {
  ASSERT_TRUE(embedded_test_server()->Start());

  GURL url1(embedded_test_server()->GetURL(
      "a.com", "/cross_site_iframe_factory.html?a(b)"));
  GURL url2(embedded_test_server()->GetURL("a.com", "/title1.html"));

  // 1) Go to |url1|, then |url2|. Both pages should have script to save the
  // history.length value when getting restored from the back-forward cache.
  EXPECT_TRUE(NavigateToURL(shell(), url1));
  FrameTreeNode* root = web_contents()->GetPrimaryFrameTree().root();
  FrameTreeNode* subframe = root->child_at(0);

  std::string restore_time_length_saver_script =
      "var resumeLength = -1;"
      "var pageshowLength = -1;"
      "document.onresume = () => {"
      "  resumeLength = history.length;"
      "};"
      "window.onpageshow  = () => {"
      "  pageshowLength = history.length;"
      "};";
  EXPECT_TRUE(ExecJs(root, restore_time_length_saver_script));
  EXPECT_TRUE(ExecJs(subframe, restore_time_length_saver_script));
  // We should have one history entry.
  EXPECT_EQ(EvalJs(root, "history.length").ExtractInt(), 1);
  EXPECT_EQ(EvalJs(subframe, "history.length").ExtractInt(), 1);

  EXPECT_TRUE(NavigateToURL(shell(), url2));
  EXPECT_TRUE(ExecJs(root, restore_time_length_saver_script));
  // We should have two history entries.
  EXPECT_EQ(EvalJs(root, "history.length").ExtractInt(), 2);

  // 2) Go back to |url1|, renderer-initiated.
  EXPECT_TRUE(ExecJs(root, "history.back()"));
  EXPECT_TRUE(WaitForLoadStop(shell()->web_contents()));
  EXPECT_EQ(web_contents()->GetLastCommittedURL(), url1);

  // We should still have two history entries, and recorded the correct length
  // when the 'resume' and 'pageshow' events were dispatched.
  EXPECT_EQ(EvalJs(root, "history.length").ExtractInt(), 2);
  EXPECT_EQ(EvalJs(root, "resumeLength").ExtractInt(), 2);
  EXPECT_EQ(EvalJs(root, "pageshowLength").ExtractInt(), 2);
  EXPECT_EQ(EvalJs(subframe, "history.length").ExtractInt(), 2);
  EXPECT_EQ(EvalJs(subframe, "resumeLength").ExtractInt(), 2);
  EXPECT_EQ(EvalJs(subframe, "pageshowLength").ExtractInt(), 2);

  // 3) Go forward to |url2|, renderer-initiated.
  EXPECT_TRUE(ExecJs(root, "history.forward()"));
  EXPECT_TRUE(WaitForLoadStop(shell()->web_contents()));

  EXPECT_EQ(web_contents()->GetLastCommittedURL(), url2);

  // We should still have two history entries, and recorded the correct length
  // when the 'resume' and 'pageshow' events were dispatched.
  EXPECT_EQ(EvalJs(root, "history.length").ExtractInt(), 2);
  EXPECT_EQ(EvalJs(root, "resumeLength").ExtractInt(), 2);
  EXPECT_EQ(EvalJs(root, "pageshowLength").ExtractInt(), 2);
}

// Check that an eligible page is cached when navigating to about:blank.
IN_PROC_BROWSER_TEST_F(BackForwardCacheBrowserTest,
                       NavigatingToAboutBlankDoesNotPreventCaching) {
  ASSERT_TRUE(embedded_test_server()->Start());

  // 1) Navigate to a.com,
  GURL url_a(embedded_test_server()->GetURL("a.com", "/empty.html"));
  EXPECT_TRUE(NavigateToURL(shell(), url_a));

  // 2) Navigate to about:blank.
  GURL blank_url(url::kAboutBlankURL);
  EXPECT_TRUE(NavigateToURL(shell(), blank_url));

  // 3) Navigate back to a.com.
  ASSERT_TRUE(HistoryGoBack(web_contents()));

  ExpectRestored(FROM_HERE);
}

// Check that the response 204 No Content doesn't affect back-forward cache.
IN_PROC_BROWSER_TEST_F(BackForwardCacheBrowserTest, NoContent) {
  ASSERT_TRUE(embedded_test_server()->Start());
  NavigationControllerImpl& controller = web_contents()->GetController();

  // 1) Navigate to a.com.
  GURL url_a(embedded_test_server()->GetURL("a.com", "/empty.html"));
  EXPECT_TRUE(NavigateToURL(shell(), url_a));
  EXPECT_EQ(1, controller.GetEntryCount());
  EXPECT_EQ(url_a, controller.GetLastCommittedEntry()->GetURL());

  // 2) Navigate to b.com
  GURL url_b(embedded_test_server()->GetURL("b.com", "/empty.html"));
  EXPECT_TRUE(NavigateToURL(shell(), url_b));
  EXPECT_EQ(2, controller.GetEntryCount());
  EXPECT_EQ(url_b, controller.GetLastCommittedEntry()->GetURL());

  // 3) Navigate to c.com with 204 No Content, then the URL will still be b.com.
  GURL url_c(embedded_test_server()->GetURL("c.com", "/echo?status=204"));
  EXPECT_TRUE(NavigateToURL(shell(), url_c, url_b));
  EXPECT_EQ(2, controller.GetEntryCount());
  EXPECT_EQ(url_b, controller.GetLastCommittedEntry()->GetURL());

  // 4) Navigate back to a.com.
  ASSERT_TRUE(HistoryGoBack(web_contents()));
  EXPECT_EQ(2, controller.GetEntryCount());
  EXPECT_EQ(url_a, controller.GetLastCommittedEntry()->GetURL());

  ExpectRestored(FROM_HERE);
}

// A testing subclass that limits the cache size to 1 for ease of testing
// evictions.
class CacheSizeOneBackForwardCacheBrowserTest
    : public BackForwardCacheBrowserTest {
 protected:
  void SetUpCommandLine(base::CommandLine* command_line) override {
    EnableFeatureAndSetParams(features::kBackForwardCache, "cache_size",
                              base::NumberToString(1));
    BackForwardCacheBrowserTest::SetUpCommandLine(command_line);
  }
};

IN_PROC_BROWSER_TEST_F(CacheSizeOneBackForwardCacheBrowserTest,
                       ReplacedNavigationEntry) {
  // Set the bfcache value to 1 to ensure that the test fails if a page
  // that replaces the current history entry is stored in back-forward cache.
  ASSERT_TRUE(embedded_test_server()->Start());
  GURL url_a(embedded_test_server()->GetURL("a.test", "/title1.html"));
  GURL url_b(embedded_test_server()->GetURL("b.test", "/title1.html"));
  GURL url_c(embedded_test_server()->GetURL("c.test", "/title1.html"));

  // 1) Navigate to A.
  EXPECT_TRUE(NavigateToURL(shell(), url_a));
  RenderFrameHostImplWrapper rfh_a(current_frame_host());

  // 2) Navigate to B.
  EXPECT_TRUE(NavigateToURL(shell(), url_b));
  RenderFrameHostImplWrapper rfh_b(current_frame_host());
  EXPECT_FALSE(rfh_a.IsRenderFrameDeleted());
  EXPECT_TRUE(rfh_a->IsInBackForwardCache());
  EXPECT_FALSE(rfh_b->IsInBackForwardCache());

  // 3) Navigate to a new page by replacing the location. The old page can't
  // be navigated back to and we should not store it in the back-forward
  // cache.
  EXPECT_TRUE(
      ExecJs(shell(), JsReplace("window.location.replace($1);", url_c)));
  EXPECT_TRUE(WaitForLoadStop(shell()->web_contents()));
  RenderFrameHostImplWrapper rfh_c(current_frame_host());

  // 4) Confirm A is still in BackForwardCache and it wasn't evicted due to the
  // cache size limit, which would happen if we tried to store a new page in the
  // cache in the previous step.
  EXPECT_FALSE(rfh_a.IsRenderFrameDeleted());
  EXPECT_TRUE(rfh_a->IsInBackForwardCache());

  // 5) Confirm that navigating backwards goes back to A.
  ASSERT_TRUE(HistoryGoBack(shell()->web_contents()));
  EXPECT_EQ(rfh_a.get(), current_frame_host());
  EXPECT_FALSE(rfh_a->IsInBackForwardCache());
  EXPECT_EQ(rfh_a->GetVisibilityState(), PageVisibilityState::kVisible);

  // Go forward again, should return to C
  ASSERT_TRUE(HistoryGoForward(shell()->web_contents()));
  EXPECT_EQ(rfh_c.get(), current_frame_host());
  EXPECT_EQ(rfh_c->GetVisibilityState(), PageVisibilityState::kVisible);
}

}  // namespace content
