// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_SIGNIN_DICE_TAB_HELPER_H_
#define CHROME_BROWSER_SIGNIN_DICE_TAB_HELPER_H_

#include "components/signin/public/base/signin_metrics.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"

namespace content {
class NavigationHandle;
}

// Tab helper used for DICE to tag signin tabs. Signin tabs can be reused.
class DiceTabHelper : public content::WebContentsUserData<DiceTabHelper>,
                      public content::WebContentsObserver {
 public:
  DiceTabHelper(const DiceTabHelper&) = delete;
  DiceTabHelper& operator=(const DiceTabHelper&) = delete;

  ~DiceTabHelper() override;

  signin_metrics::AccessPoint signin_access_point() const {
    return signin_access_point_;
  }

  signin_metrics::PromoAction signin_promo_action() const {
    return signin_promo_action_;
  }

  signin_metrics::Reason signin_reason() const { return signin_reason_; }

  const GURL& redirect_url() const { return redirect_url_; }

  const GURL& signin_url() const { return signin_url_; }

  // Initializes the DiceTabHelper for a new signin flow. Must be called once
  // per signin flow happening in the tab, when the signin URL is being loaded.
  void InitializeSigninFlow(const GURL& signin_url,
                            signin_metrics::AccessPoint access_point,
                            signin_metrics::Reason reason,
                            signin_metrics::PromoAction promo_action,
                            const GURL& redirect_url);

  // Returns true if this the tab is a re-usable chrome sign-in page (the signin
  // page is loading or loaded in the tab).
  // Returns false if the user or the page has navigated away from |signin_url|.
  bool IsChromeSigninPage() const;

  // Returns true if a signin flow was initialized with the reason
  // kSigninPrimaryAccount and is not yet complete.
  // Note that there is not guarantee that the flow would ever finish, and in
  // some rare cases it is possible that a "non-sync" signin happens while this
  // is true (if the user aborts the flow and then re-uses the same tab for a
  // normal web signin).
  bool IsSyncSigninInProgress() const;

  // Called to notify that the sync signin is complete.
  void OnSyncSigninFlowComplete();

 private:
  friend class content::WebContentsUserData<DiceTabHelper>;
  explicit DiceTabHelper(content::WebContents* web_contents);

  // kStarted: a Sync signin flow was started and not completed.
  // kNotStarted: there is no sync signin flow in progress.
  enum class SyncSigninFlowStatus { kNotStarted, kStarted };

  // content::WebContentsObserver:
  void DidStartNavigation(
      content::NavigationHandle* navigation_handle) override;
  void DidFinishNavigation(
      content::NavigationHandle* navigation_handle) override;

  // Returns true if this is a navigation to the signin URL.
  bool IsSigninPageNavigation(
      content::NavigationHandle* navigation_handle) const;

  GURL redirect_url_;
  GURL signin_url_;
  signin_metrics::AccessPoint signin_access_point_ =
      signin_metrics::AccessPoint::ACCESS_POINT_UNKNOWN;
  signin_metrics::PromoAction signin_promo_action_ =
      signin_metrics::PromoAction::PROMO_ACTION_NO_SIGNIN_PROMO;
  signin_metrics::Reason signin_reason_ =
      signin_metrics::Reason::kUnknownReason;
  bool is_chrome_signin_page_ = false;
  bool signin_page_load_recorded_ = false;
  SyncSigninFlowStatus sync_signin_flow_status_ =
      SyncSigninFlowStatus::kNotStarted;

  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

#endif  // CHROME_BROWSER_SIGNIN_DICE_TAB_HELPER_H_
