// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>

#include "base/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/run_loop.h"
#include "base/scoped_observation.h"
#include "base/test/bind.h"
#include "build/build_config.h"
#include "build/chromeos_buildflags.h"
#include "chrome/browser/signin/identity_manager_factory.h"
#include "chrome/browser/signin/reauth_result.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/signin_view_controller.h"
#include "chrome/browser/ui/signin_view_controller_delegate.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/webui/signin/enterprise_profile_welcome_ui.h"
#include "chrome/browser/ui/webui/signin/login_ui_service.h"
#include "chrome/browser/ui/webui/signin/login_ui_service_factory.h"
#include "chrome/browser/ui/webui/signin/login_ui_test_utils.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/interactive_test_utils.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/signin/public/base/consent_level.h"
#include "components/signin/public/base/signin_metrics.h"
#include "components/signin/public/identity_manager/identity_manager.h"
#include "components/signin/public/identity_manager/identity_test_utils.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/test_navigation_observer.h"
#include "content/public/test/test_utils.h"
#include "google_apis/gaia/core_account_id.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "third_party/abseil-cpp/absl/types/optional.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/events/keycodes/keyboard_codes.h"

namespace {

// Synchronously waits for the Sync confirmation to be closed.
class SyncConfirmationClosedObserver : public LoginUIService::Observer {
 public:
  explicit SyncConfirmationClosedObserver(Browser* browser)
      : browser_(browser) {
    DCHECK(browser_);
    login_ui_service_observation_.Observe(
        LoginUIServiceFactory::GetForProfile(browser_->profile()));
  }

  LoginUIService::SyncConfirmationUIClosedResult WaitForConfirmationClosed() {
    run_loop_.Run();
    return *result_;
  }

 private:
  // LoginUIService::Observer:
  void OnSyncConfirmationUIClosed(
      LoginUIService::SyncConfirmationUIClosedResult result) override {
    login_ui_service_observation_.Reset();
    result_ = result;
    browser_->signin_view_controller()->CloseModalSignin();
    run_loop_.Quit();
  }

  const raw_ptr<Browser> browser_;
  base::RunLoop run_loop_;
  base::ScopedObservation<LoginUIService, LoginUIService::Observer>
      login_ui_service_observation_{this};
  absl::optional<LoginUIService::SyncConfirmationUIClosedResult> result_;
};

}  // namespace

class SignInViewControllerBrowserTest : public InProcessBrowserTest {
 public:
  void SetUpOnMainThread() override {
    // Many hotkeys are defined by the main menu. The value of these hotkeys
    // depends on the focused window. We must focus the browser window. This is
    // also why this test must be an interactive_ui_test rather than a browser
    // test.
    ASSERT_TRUE(ui_test_utils::ShowAndFocusNativeWindow(
        browser()->window()->GetNativeWindow()));
  }

  signin::IdentityManager* GetIdentityManager() {
    return IdentityManagerFactory::GetForProfile(browser()->profile());
  }
};

#if !BUILDFLAG(IS_CHROMEOS_LACROS)
// DICE sign-in flow isn't applicable on Lacros.
IN_PROC_BROWSER_TEST_F(SignInViewControllerBrowserTest, Accelerators) {
  ASSERT_EQ(1, browser()->tab_strip_model()->count());
  browser()->signin_view_controller()->ShowSignin(
      profiles::BUBBLE_VIEW_MODE_GAIA_SIGNIN,
      signin_metrics::AccessPoint::ACCESS_POINT_SETTINGS);

  ui_test_utils::TabAddedWaiter wait_for_new_tab(browser());
// Press Ctrl/Cmd+T, which will open a new tab.
#if BUILDFLAG(IS_MAC)
  ASSERT_TRUE(ui_test_utils::SendKeyPressSync(
      browser(), ui::VKEY_T, /*control=*/false, /*shift=*/false, /*alt=*/false,
      /*command=*/true));
#else
  ASSERT_TRUE(ui_test_utils::SendKeyPressSync(
      browser(), ui::VKEY_T, /*control=*/true, /*shift=*/false, /*alt=*/false,
      /*command=*/false));
#endif

  wait_for_new_tab.Wait();

  EXPECT_EQ(2, browser()->tab_strip_model()->count());
}
#endif  // !BUILDFLAG(IS_CHROMEOS_LACROS)

// Tests that the confirm button is focused by default in the sync confirmation
// dialog.
IN_PROC_BROWSER_TEST_F(SignInViewControllerBrowserTest,
                       SyncConfirmationDefaultFocus) {
  signin::MakePrimaryAccountAvailable(GetIdentityManager(), "alice@gmail.com",
                                      signin::ConsentLevel::kSync);
  content::TestNavigationObserver content_observer(
      GURL("chrome://sync-confirmation/"));
  content_observer.StartWatchingNewWebContents();
  browser()->signin_view_controller()->ShowModalSyncConfirmationDialog();
  EXPECT_TRUE(browser()->signin_view_controller()->ShowsModalDialog());
  content_observer.Wait();

  SyncConfirmationClosedObserver sync_confirmation_observer(browser());
  ASSERT_TRUE(ui_test_utils::SendKeyPressSync(browser(), ui::VKEY_RETURN,
                                              /*control=*/false,
                                              /*shift=*/false, /*alt=*/false,
                                              /*command=*/false));

  LoginUIService::SyncConfirmationUIClosedResult result =
      sync_confirmation_observer.WaitForConfirmationClosed();
  EXPECT_EQ(result, LoginUIService::SYNC_WITH_DEFAULT_SETTINGS);
  EXPECT_FALSE(browser()->signin_view_controller()->ShowsModalDialog());
}

// Tests that the confirm button is focused by default in the signin email
// confirmation dialog.
// TODO(http://crbug.com/1286855): Flaky on MacOS.
#if BUILDFLAG(IS_MAC)
#define MAYBE_EmailConfirmationDefaultFocus \
  DISABLED_EmailConfirmationDefaultFocus
#else
#define MAYBE_EmailConfirmationDefaultFocus EmailConfirmationDefaultFocus
#endif
IN_PROC_BROWSER_TEST_F(SignInViewControllerBrowserTest,
                       MAYBE_EmailConfirmationDefaultFocus) {
  content::TestNavigationObserver content_observer(
      GURL("chrome://signin-email-confirmation/"));
  content_observer.StartWatchingNewWebContents();
  base::RunLoop run_loop;
  SigninEmailConfirmationDialog::Action chosen_action;
  browser()->signin_view_controller()->ShowModalSigninEmailConfirmationDialog(
      "alice@gmail.com", "bob@gmail.com",
      base::BindLambdaForTesting(
          [&](SigninEmailConfirmationDialog::Action action) {
            chosen_action = action;
            run_loop.Quit();
          }));
  EXPECT_TRUE(browser()->signin_view_controller()->ShowsModalDialog());
  content_observer.Wait();

  ASSERT_TRUE(ui_test_utils::SendKeyPressSync(browser(), ui::VKEY_RETURN,
                                              /*control=*/false,
                                              /*shift=*/false, /*alt=*/false,
                                              /*command=*/false));
  run_loop.Run();
  EXPECT_EQ(chosen_action, SigninEmailConfirmationDialog::CREATE_NEW_USER);
  EXPECT_FALSE(browser()->signin_view_controller()->ShowsModalDialog());
}

// Tests that the confirm button is focused by default in the signin error
// dialog.
IN_PROC_BROWSER_TEST_F(SignInViewControllerBrowserTest,
                       ErrorDialogDefaultFocus) {
  content::TestNavigationObserver content_observer(
      GURL("chrome://signin-error/"));
  content_observer.StartWatchingNewWebContents();
  browser()->signin_view_controller()->ShowModalSigninErrorDialog();
  EXPECT_TRUE(browser()->signin_view_controller()->ShowsModalDialog());
  content_observer.Wait();

  content::WebContentsDestroyedWatcher dialog_destroyed_watcher(
      browser()
          ->signin_view_controller()
          ->GetModalDialogWebContentsForTesting());
  ASSERT_TRUE(ui_test_utils::SendKeyPressSync(browser(), ui::VKEY_RETURN,
                                              /*control=*/false,
                                              /*shift=*/false, /*alt=*/false,
                                              /*command=*/false));
  // Default action simply closes the dialog.
  dialog_destroyed_watcher.Wait();
  EXPECT_FALSE(browser()->signin_view_controller()->ShowsModalDialog());
}

// Tests that the confirm button is focused by default in the enterprise
// interception dialog.
IN_PROC_BROWSER_TEST_F(SignInViewControllerBrowserTest,
                       EnterpriseConfirmationDefaultFocus) {
  auto account_info = signin::MakePrimaryAccountAvailable(
      GetIdentityManager(), "alice@gmail.com", signin::ConsentLevel::kSync);
  content::TestNavigationObserver content_observer(
      GURL("chrome://enterprise-profile-welcome/"));
  content_observer.StartWatchingNewWebContents();
  signin::SigninChoice result;
  browser()->signin_view_controller()->ShowModalEnterpriseConfirmationDialog(
      account_info, /*force_new_profile=*/true, /*show_link_data_option=*/true,
      SK_ColorWHITE,
      base::BindOnce(
          [](Browser* browser, signin::SigninChoice* result,
             signin::SigninChoice choice) {
            browser->signin_view_controller()->CloseModalSignin();
            *result = choice;
          },
          browser(), &result));
  EXPECT_TRUE(browser()->signin_view_controller()->ShowsModalDialog());
  content_observer.Wait();

  content::WebContentsDestroyedWatcher dialog_destroyed_watcher(
      browser()
          ->signin_view_controller()
          ->GetModalDialogWebContentsForTesting());
  ASSERT_TRUE(ui_test_utils::SendKeyPressSync(browser(), ui::VKEY_RETURN,
                                              /*control=*/false,
                                              /*shift=*/false, /*alt=*/false,
                                              /*command=*/false));

  dialog_destroyed_watcher.Wait();
  EXPECT_EQ(result, signin::SigninChoice::SIGNIN_CHOICE_NEW_PROFILE);
  EXPECT_FALSE(browser()->signin_view_controller()->ShowsModalDialog());
}
