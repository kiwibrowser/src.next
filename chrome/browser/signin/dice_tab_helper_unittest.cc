// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/signin/dice_tab_helper.h"

#include "base/test/metrics/histogram_tester.h"
#include "base/test/metrics/user_action_tester.h"
#include "chrome/test/base/chrome_render_view_host_test_harness.h"
#include "chrome/test/base/testing_profile.h"
#include "components/signin/public/base/signin_metrics.h"
#include "content/public/common/content_features.h"
#include "content/public/test/back_forward_cache_util.h"
#include "content/public/test/navigation_simulator.h"
#include "content/public/test/prerender_test_util.h"
#include "content/public/test/test_renderer_host.h"
#include "content/public/test/web_contents_tester.h"
#include "google_apis/gaia/gaia_urls.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/blink/public/common/features.h"

class DiceTabHelperTest : public ChromeRenderViewHostTestHarness {
 public:
  DiceTabHelperTest() {
    signin_url_ = GaiaUrls::GetInstance()->signin_chrome_sync_dice();
    feature_list_.InitWithFeaturesAndParameters(
        {{features::kBackForwardCache, {}},
         {features::kBackForwardCacheMemoryControls, {}}},
        {});
  }

  // Does a navigation to Gaia and initializes the tab helper.
  void InitializeDiceTabHelper(DiceTabHelper* helper,
                               signin_metrics::AccessPoint access_point,
                               signin_metrics::Reason reason) {
    // Load the signin page.
    std::unique_ptr<content::NavigationSimulator> simulator =
        content::NavigationSimulator::CreateRendererInitiated(signin_url_,
                                                              main_rfh());
    simulator->Start();
    helper->InitializeSigninFlow(
        signin_url_, access_point, reason,
        signin_metrics::PromoAction::PROMO_ACTION_NO_SIGNIN_PROMO,
        GURL::EmptyGURL());
    EXPECT_TRUE(helper->IsChromeSigninPage());
    simulator->Commit();
  }

  GURL signin_url_;
  base::test::ScopedFeatureList feature_list_;
};

// Tests DiceTabHelper intialization.
TEST_F(DiceTabHelperTest, Initialization) {
  DiceTabHelper::CreateForWebContents(web_contents());
  DiceTabHelper* dice_tab_helper =
      DiceTabHelper::FromWebContents(web_contents());

  // Check default state.
  EXPECT_EQ(signin_metrics::AccessPoint::ACCESS_POINT_UNKNOWN,
            dice_tab_helper->signin_access_point());
  EXPECT_EQ(signin_metrics::Reason::kUnknownReason,
            dice_tab_helper->signin_reason());
  EXPECT_FALSE(dice_tab_helper->IsChromeSigninPage());

  // Initialize the signin flow.
  signin_metrics::AccessPoint access_point =
      signin_metrics::AccessPoint::ACCESS_POINT_BOOKMARK_BUBBLE;
  signin_metrics::Reason reason = signin_metrics::Reason::kSigninPrimaryAccount;
  InitializeDiceTabHelper(dice_tab_helper, access_point, reason);
  EXPECT_EQ(access_point, dice_tab_helper->signin_access_point());
  EXPECT_EQ(reason, dice_tab_helper->signin_reason());
  EXPECT_TRUE(dice_tab_helper->IsChromeSigninPage());
}

TEST_F(DiceTabHelperTest, SigninPageStatus) {
  // The test assumes the previous page gets deleted after navigation and will
  // be recreated after navigation (which resets the signin page state). Disable
  // back/forward cache to ensure that it doesn't get preserved in the cache.
  content::DisableBackForwardCacheForTesting(
      web_contents(), content::BackForwardCache::TEST_REQUIRES_NO_CACHING);
  DiceTabHelper::CreateForWebContents(web_contents());
  DiceTabHelper* dice_tab_helper =
      DiceTabHelper::FromWebContents(web_contents());
  EXPECT_FALSE(dice_tab_helper->IsChromeSigninPage());

  // Load the signin page.
  signin_metrics::AccessPoint access_point =
      signin_metrics::AccessPoint::ACCESS_POINT_BOOKMARK_BUBBLE;
  signin_metrics::Reason reason = signin_metrics::Reason::kSigninPrimaryAccount;
  InitializeDiceTabHelper(dice_tab_helper, access_point, reason);
  EXPECT_TRUE(dice_tab_helper->IsChromeSigninPage());

  // Reloading the signin page does not interrupt the signin flow.
  content::NavigationSimulator::Reload(web_contents());
  EXPECT_TRUE(dice_tab_helper->IsChromeSigninPage());

  // Subframe navigation are ignored.
  std::unique_ptr<content::NavigationSimulator> simulator =
      content::NavigationSimulator::CreateRendererInitiated(
          signin_url_.Resolve("#baz"), main_rfh());
  simulator->CommitSameDocument();
  EXPECT_TRUE(dice_tab_helper->IsChromeSigninPage());

  // Navigation in subframe does not interrupt the signin flow.
  content::RenderFrameHostTester* render_frame_host_tester =
      content::RenderFrameHostTester::For(main_rfh());
  content::RenderFrameHost* sub_frame =
      render_frame_host_tester->AppendChild("subframe");
  content::NavigationSimulator::NavigateAndCommitFromDocument(signin_url_,
                                                              sub_frame);
  EXPECT_TRUE(dice_tab_helper->IsChromeSigninPage());

  // Navigating to a different page resets the page status.
  simulator = content::NavigationSimulator::CreateRendererInitiated(
      signin_url_.Resolve("/foo"), main_rfh());
  simulator->Start();
  EXPECT_FALSE(dice_tab_helper->IsChromeSigninPage());
  simulator->Commit();
  EXPECT_FALSE(dice_tab_helper->IsChromeSigninPage());

  // Go Back to the signin page
  content::NavigationSimulator::GoBack(web_contents());
  // IsChromeSigninPage() returns false after navigating away from the
  // signin page.
  EXPECT_FALSE(dice_tab_helper->IsChromeSigninPage());

  // Navigate away from the signin page
  content::NavigationSimulator::GoForward(web_contents());
  EXPECT_FALSE(dice_tab_helper->IsChromeSigninPage());
}

// Tests DiceTabHelper metrics.
TEST_F(DiceTabHelperTest, Metrics) {
  base::UserActionTester ua_tester;
  base::HistogramTester h_tester;
  DiceTabHelper::CreateForWebContents(web_contents());
  DiceTabHelper* dice_tab_helper =
      DiceTabHelper::FromWebContents(web_contents());

  // No metrics are logged when the Dice tab helper is created.
  EXPECT_EQ(0, ua_tester.GetActionCount("Signin_Signin_FromStartPage"));
  EXPECT_EQ(0, ua_tester.GetActionCount("Signin_SigninPage_Loading"));
  EXPECT_EQ(0, ua_tester.GetActionCount("Signin_SigninPage_Shown"));

  // Check metrics logged when the Dice tab helper is initialized.
  std::unique_ptr<content::NavigationSimulator> simulator =
      content::NavigationSimulator::CreateRendererInitiated(signin_url_,
                                                            main_rfh());
  simulator->Start();
  dice_tab_helper->InitializeSigninFlow(
      signin_url_, signin_metrics::AccessPoint::ACCESS_POINT_SETTINGS,
      signin_metrics::Reason::kSigninPrimaryAccount,
      signin_metrics::PromoAction::PROMO_ACTION_NEW_ACCOUNT_NO_EXISTING_ACCOUNT,
      GURL::EmptyGURL());
  EXPECT_EQ(1, ua_tester.GetActionCount("Signin_Signin_FromSettings"));
  EXPECT_EQ(1, ua_tester.GetActionCount("Signin_SigninPage_Loading"));
  EXPECT_EQ(0, ua_tester.GetActionCount("Signin_SigninPage_Shown"));
  h_tester.ExpectUniqueSample(
      "Signin.SigninStartedAccessPoint",
      signin_metrics::AccessPoint::ACCESS_POINT_SETTINGS, 1);
  h_tester.ExpectUniqueSample(
      "Signin.SigninStartedAccessPoint.NewAccountNoExistingAccount",
      signin_metrics::AccessPoint::ACCESS_POINT_SETTINGS, 1);

  // First call to did finish load does logs any Signin_SigninPage_Shown user
  // action.
  simulator->Commit();
  EXPECT_EQ(1, ua_tester.GetActionCount("Signin_SigninPage_Loading"));
  EXPECT_EQ(1, ua_tester.GetActionCount("Signin_SigninPage_Shown"));

  // Second call to did finish load does not log any metrics.
  dice_tab_helper->DidFinishLoad(main_rfh(), signin_url_);
  EXPECT_EQ(1, ua_tester.GetActionCount("Signin_SigninPage_Loading"));
  EXPECT_EQ(1, ua_tester.GetActionCount("Signin_SigninPage_Shown"));

  // Check metrics are logged again when the Dice tab helper is re-initialized.
  simulator = content::NavigationSimulator::CreateRendererInitiated(signin_url_,
                                                                    main_rfh());
  simulator->Start();
  dice_tab_helper->InitializeSigninFlow(
      signin_url_, signin_metrics::AccessPoint::ACCESS_POINT_SETTINGS,
      signin_metrics::Reason::kSigninPrimaryAccount,
      signin_metrics::PromoAction::PROMO_ACTION_WITH_DEFAULT,
      GURL::EmptyGURL());
  EXPECT_EQ(2, ua_tester.GetActionCount("Signin_Signin_FromSettings"));
  EXPECT_EQ(2, ua_tester.GetActionCount("Signin_SigninPage_Loading"));
  h_tester.ExpectUniqueSample(
      "Signin.SigninStartedAccessPoint",
      signin_metrics::AccessPoint::ACCESS_POINT_SETTINGS, 2);
  h_tester.ExpectUniqueSample(
      "Signin.SigninStartedAccessPoint.WithDefault",
      signin_metrics::AccessPoint::ACCESS_POINT_SETTINGS, 1);
}

TEST_F(DiceTabHelperTest, IsSyncSigninInProgress) {
  DiceTabHelper::CreateForWebContents(web_contents());
  DiceTabHelper* dice_tab_helper =
      DiceTabHelper::FromWebContents(web_contents());
  EXPECT_FALSE(dice_tab_helper->IsSyncSigninInProgress());

  // Non-sync signin.
  InitializeDiceTabHelper(dice_tab_helper,
                          signin_metrics::AccessPoint::ACCESS_POINT_EXTENSIONS,
                          signin_metrics::Reason::kAddSecondaryAccount);
  EXPECT_FALSE(dice_tab_helper->IsSyncSigninInProgress());

  // Sync signin
  InitializeDiceTabHelper(dice_tab_helper,
                          signin_metrics::AccessPoint::ACCESS_POINT_SETTINGS,
                          signin_metrics::Reason::kSigninPrimaryAccount);
  EXPECT_TRUE(dice_tab_helper->IsSyncSigninInProgress());
  dice_tab_helper->OnSyncSigninFlowComplete();
  EXPECT_FALSE(dice_tab_helper->IsSyncSigninInProgress());
}

class DiceTabHelperPrerenderTest : public DiceTabHelperTest {
 public:
  DiceTabHelperPrerenderTest() {
    feature_list_.InitWithFeatures(
        {blink::features::kPrerender2},
        // Disable the memory requirement of Prerender2 so the test can run on
        // any bot.
        {blink::features::kPrerender2MemoryControls});
  }

  ~DiceTabHelperPrerenderTest() override = default;

 private:
  base::test::ScopedFeatureList feature_list_;
};

TEST_F(DiceTabHelperPrerenderTest, SigninStatusAfterPrerendering) {
  content::test::ScopedPrerenderWebContentsDelegate web_contents_delegate(
      *web_contents());
  base::UserActionTester ua_tester;
  DiceTabHelper::CreateForWebContents(web_contents());
  DiceTabHelper* dice_tab_helper =
      DiceTabHelper::FromWebContents(web_contents());
  EXPECT_FALSE(dice_tab_helper->IsChromeSigninPage());
  EXPECT_EQ(0, ua_tester.GetActionCount("Signin_SigninPage_Shown"));

  // Sync signin
  InitializeDiceTabHelper(dice_tab_helper,
                          signin_metrics::AccessPoint::ACCESS_POINT_SETTINGS,
                          signin_metrics::Reason::kSigninPrimaryAccount);
  dice_tab_helper->OnSyncSigninFlowComplete();
  EXPECT_TRUE(dice_tab_helper->IsChromeSigninPage());
  EXPECT_EQ(1, ua_tester.GetActionCount("Signin_SigninPage_Shown"));

  // Starting prerendering a page doesn't navigate away from the signin page.
  content::WebContentsTester::For(web_contents())
      ->AddPrerenderAndCommitNavigation(signin_url_.Resolve("/foo/test.html"));
  EXPECT_TRUE(dice_tab_helper->IsChromeSigninPage());
  EXPECT_EQ(1, ua_tester.GetActionCount("Signin_SigninPage_Shown"));
}
