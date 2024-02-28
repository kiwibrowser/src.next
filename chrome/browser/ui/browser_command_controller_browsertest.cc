// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/browser_command_controller.h"

#include "base/command_line.h"
#include "base/containers/contains.h"
#include "base/run_loop.h"
#include "base/strings/string_piece.h"
#include "base/test/scoped_feature_list.h"
#include "base/test/task_environment.h"
#include "build/build_config.h"
#include "build/chromeos_buildflags.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_window.h"
#include "chrome/browser/search_engines/template_url_service_factory.h"
#include "chrome/browser/sessions/tab_restore_service_factory.h"
#include "chrome/browser/sessions/tab_restore_service_load_waiter.h"
#include "chrome/browser/signin/identity_manager_factory.h"
#include "chrome/browser/translate/translate_test_utils.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/profiles/profile_picker.h"
#include "chrome/browser/ui/profiles/profile_ui_test_utils.h"
#include "chrome/browser/ui/startup/startup_browser_creator.h"
#include "chrome/browser/ui/tab_modal_confirm_dialog_browsertest.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/search_engines/template_url_service.h"
#include "components/sessions/core/tab_restore_service.h"
#include "components/sessions/core/tab_restore_service_observer.h"
#include "components/signin/public/identity_manager/identity_manager.h"
#include "components/signin/public/identity_manager/identity_test_environment.h"
#include "components/signin/public/identity_manager/identity_test_utils.h"
#include "components/translate/core/browser/language_state.h"
#include "components/translate/core/browser/translate_manager.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/test_utils.h"
#include "net/base/network_change_notifier.h"
#include "ui/base/ui_base_features.h"

#if BUILDFLAG(IS_CHROMEOS_ASH)
#include "ash/constants/ash_switches.h"
#include "chrome/browser/ui/chromeos/window_pin_util.h"
#include "ui/aura/window.h"
#endif

namespace chrome {

class BrowserCommandControllerBrowserTest : public InProcessBrowserTest {
 public:
  BrowserCommandControllerBrowserTest() {}

  BrowserCommandControllerBrowserTest(
      const BrowserCommandControllerBrowserTest&) = delete;
  BrowserCommandControllerBrowserTest& operator=(
      const BrowserCommandControllerBrowserTest&) = delete;

  ~BrowserCommandControllerBrowserTest() override = default;

  void SetUpCommandLine(base::CommandLine* command_line) override {
#if BUILDFLAG(IS_CHROMEOS_ASH)
    command_line->AppendSwitch(
        ash::switches::kIgnoreUserProfileMappingForTests);
#endif
  }
};

// Test case for menus that only appear after Chrome Refresh.
class BrowserCommandControllerBrowserTestRefreshOnly
    : public BrowserCommandControllerBrowserTest {
 public:
  BrowserCommandControllerBrowserTestRefreshOnly() {
    scoped_feature_list_.InitWithFeatures({features::kChromeRefresh2023}, {});
  }
  BrowserCommandControllerBrowserTestRefreshOnly(
      const BrowserCommandControllerBrowserTestRefreshOnly&) = delete;
  BrowserCommandControllerBrowserTestRefreshOnly& operator=(
      const BrowserCommandControllerBrowserTestRefreshOnly&) = delete;

  ~BrowserCommandControllerBrowserTestRefreshOnly() override = default;

 protected:
  void LoadAndWaitForLanguage(base::StringPiece relative_url) {
    ASSERT_TRUE(embedded_test_server()->Start());

    GURL url = embedded_test_server()->GetURL(relative_url);
    ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), url));

    ChromeTranslateClient* chrome_translate_client =
        ChromeTranslateClient::FromWebContents(
            browser()->tab_strip_model()->GetActiveWebContents());

    std::unique_ptr<translate::TranslateWaiter> translate_waiter =
        translate::CreateTranslateWaiter(
            browser()->tab_strip_model()->GetActiveWebContents(),
            translate::TranslateWaiter::WaitEvent::kLanguageDetermined);

    while (
        chrome_translate_client->GetLanguageState().source_language().empty()) {
      translate_waiter->Wait();
    }
    translate::TranslateManager::SetIgnoreMissingKeyForTesting(true);
    net::NetworkChangeNotifier::CreateMockIfNeeded();
    browser()->command_controller()->TabStateChanged();
  }

 private:
  base::test::ScopedFeatureList scoped_feature_list_;
};

// Verify that showing a constrained window disables find.
IN_PROC_BROWSER_TEST_F(BrowserCommandControllerBrowserTest, DisableFind) {
  EXPECT_TRUE(chrome::IsCommandEnabled(browser(), IDC_FIND));

  // Showing constrained window should disable find.
  content::WebContents* web_contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  auto delegate = std::make_unique<MockTabModalConfirmDialogDelegate>(
      web_contents, nullptr);
  MockTabModalConfirmDialogDelegate* delegate_ptr = delegate.get();
  TabModalConfirmDialog::Create(std::move(delegate), web_contents);
  EXPECT_FALSE(chrome::IsCommandEnabled(browser(), IDC_FIND));

  // Switching to a new (unblocked) tab should reenable it.
  AddBlankTabAndShow(browser());
  EXPECT_TRUE(chrome::IsCommandEnabled(browser(), IDC_FIND));

  // Switching back to the blocked tab should disable it again.
  browser()->tab_strip_model()->ActivateTabAt(0);
  EXPECT_FALSE(chrome::IsCommandEnabled(browser(), IDC_FIND));

  // Closing the constrained window should reenable it.
  delegate_ptr->Cancel();
  content::RunAllPendingInMessageLoop();
  EXPECT_TRUE(chrome::IsCommandEnabled(browser(), IDC_FIND));
}

IN_PROC_BROWSER_TEST_F(BrowserCommandControllerBrowserTest,
                       DisableCommandsInSingleTab) {
  EXPECT_FALSE(
      chrome::IsCommandEnabled(browser(), IDC_WINDOW_CLOSE_TABS_TO_RIGHT));
  EXPECT_FALSE(
      chrome::IsCommandEnabled(browser(), IDC_WINDOW_CLOSE_OTHER_TABS));
  EXPECT_FALSE(chrome::IsCommandEnabled(browser(), IDC_MOVE_TAB_TO_NEW_WINDOW));

  // Add a new tab.
  auto* tab_strip_model = browser()->tab_strip_model();
  AddBlankTabAndShow(browser());
  ASSERT_EQ(2, tab_strip_model->count());
  ASSERT_EQ(1, tab_strip_model->active_index());
  // Active previous tab.
  tab_strip_model->ActivateTabAt(0);
  ASSERT_EQ(2, tab_strip_model->count());
  ASSERT_EQ(0, tab_strip_model->active_index());

  EXPECT_TRUE(
      chrome::IsCommandEnabled(browser(), IDC_WINDOW_CLOSE_TABS_TO_RIGHT));
  EXPECT_TRUE(chrome::IsCommandEnabled(browser(), IDC_WINDOW_CLOSE_OTHER_TABS));
  EXPECT_TRUE(chrome::IsCommandEnabled(browser(), IDC_MOVE_TAB_TO_NEW_WINDOW));

  // Close the newly added tab.
  tab_strip_model->CloseWebContentsAt(1, TabCloseTypes::CLOSE_USER_GESTURE);
  ASSERT_EQ(1, tab_strip_model->count());

  EXPECT_FALSE(
      chrome::IsCommandEnabled(browser(), IDC_WINDOW_CLOSE_TABS_TO_RIGHT));
  EXPECT_FALSE(
      chrome::IsCommandEnabled(browser(), IDC_WINDOW_CLOSE_OTHER_TABS));
  EXPECT_FALSE(chrome::IsCommandEnabled(browser(), IDC_MOVE_TAB_TO_NEW_WINDOW));
}

#if !BUILDFLAG(IS_CHROMEOS_ASH)
IN_PROC_BROWSER_TEST_F(BrowserCommandControllerBrowserTest,
                       NewAvatarMenuEnabledInGuestMode) {
  EXPECT_EQ(1U, BrowserList::GetInstance()->size());

  Browser* browser = CreateGuestBrowser();
  EXPECT_TRUE(browser);

  const CommandUpdater* command_updater = browser->command_controller();
  EXPECT_TRUE(command_updater->IsCommandEnabled(IDC_SHOW_AVATAR_MENU));
}
#endif

#if BUILDFLAG(IS_CHROMEOS_ASH)
IN_PROC_BROWSER_TEST_F(BrowserCommandControllerBrowserTest, LockedFullscreen) {
  CommandUpdaterImpl* command_updater =
      &browser()->command_controller()->command_updater_;
  // IDC_EXIT is always enabled in regular mode so it's a perfect candidate for
  // testing.
  EXPECT_TRUE(command_updater->IsCommandEnabled(IDC_EXIT));
  // Set locked fullscreen mode.
  PinWindow(browser()->window()->GetNativeWindow(), /*trusted=*/true);
  // Update the corresponding command_controller state.
  browser()->command_controller()->LockedFullscreenStateChanged();
  // Update some more states just to make sure the wrong commands don't get
  // enabled.
  browser()->command_controller()->TabStateChanged();
  browser()->command_controller()->FullscreenStateChanged();
  browser()->command_controller()->PrintingStateChanged();
  browser()->command_controller()->ExtensionStateChanged();
  // IDC_EXIT is not enabled in locked fullscreen.
  EXPECT_FALSE(command_updater->IsCommandEnabled(IDC_EXIT));

  constexpr int kAllowlistedIds[] = {IDC_CUT, IDC_COPY, IDC_PASTE};

  // Go through all the command ids and make sure all non-allowlisted commands
  // are disabled.
  for (int id : command_updater->GetAllIds()) {
    if (base::Contains(kAllowlistedIds, id)) {
      continue;
    }
    EXPECT_FALSE(command_updater->IsCommandEnabled(id));
  }

  // Verify the set of allowlisted commands.
  for (int id : kAllowlistedIds) {
    EXPECT_TRUE(command_updater->IsCommandEnabled(id));
  }

  // Exit locked fullscreen mode.
  UnpinWindow(browser()->window()->GetNativeWindow());
  // Update the corresponding command_controller state.
  browser()->command_controller()->LockedFullscreenStateChanged();
  // IDC_EXIT is enabled again.
  EXPECT_TRUE(command_updater->IsCommandEnabled(IDC_EXIT));
}
#endif

IN_PROC_BROWSER_TEST_F(BrowserCommandControllerBrowserTest,
                       TestTabRestoreServiceInitialized) {
  // Note: The command should start out as enabled as the default.
  // All the initialization happens before any test code executes,
  // so we can't validate it.

  // The TabRestoreService should get initialized (Loaded)
  // automatically upon launch.
  // Wait for robustness because InProcessBrowserTest::PreRunTestOnMainThread
  // does not flush the task scheduler.
  TabRestoreServiceLoadWaiter waiter(
      TabRestoreServiceFactory::GetForProfile(browser()->profile()));
  waiter.Wait();

  // After initialization, the command should become disabled because there's
  // nothing to restore.
  chrome::BrowserCommandController* commandController =
      browser()->command_controller();
  ASSERT_EQ(false, commandController->IsCommandEnabled(IDC_RESTORE_TAB));
}

IN_PROC_BROWSER_TEST_F(BrowserCommandControllerBrowserTest,
                       PRE_TestTabRestoreCommandEnabled) {
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), GURL("about:blank"), WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP);
  ASSERT_EQ(2, browser()->tab_strip_model()->count());
  EXPECT_EQ(1, browser()->tab_strip_model()->active_index());
  content::WebContents* tab_to_close =
      browser()->tab_strip_model()->GetActiveWebContents();
  content::WebContentsDestroyedWatcher destroyed_watcher(tab_to_close);
  browser()->tab_strip_model()->CloseSelectedTabs();
  destroyed_watcher.Wait();
}

IN_PROC_BROWSER_TEST_F(BrowserCommandControllerBrowserTest,
                       TestTabRestoreCommandEnabled) {
  // The TabRestoreService should get initialized (Loaded)
  // automatically upon launch.
  // Wait for robustness because InProcessBrowserTest::PreRunTestOnMainThread
  // does not flush the task scheduler.
  TabRestoreServiceLoadWaiter waiter(
      TabRestoreServiceFactory::GetForProfile(browser()->profile()));
  waiter.Wait();

  // After initialization, the command should remain enabled because there's
  // one tab to restore.
  chrome::BrowserCommandController* commandController =
      browser()->command_controller();
  ASSERT_EQ(true, commandController->IsCommandEnabled(IDC_RESTORE_TAB));
}

IN_PROC_BROWSER_TEST_F(BrowserCommandControllerBrowserTest,
                       OpenDisabledForAppBrowser) {
  auto params = Browser::CreateParams::CreateForApp(
      "abcdefghaghpphfffooibmlghaeopach", true /* trusted_source */,
      gfx::Rect(), /* window_bounts */
      browser()->profile(), true /* user_gesture */);
  Browser* browser = Browser::Create(params);

  chrome::BrowserCommandController* commandController =
      browser->command_controller();
  ASSERT_EQ(false, commandController->IsCommandEnabled(IDC_OPEN_FILE));
}

IN_PROC_BROWSER_TEST_F(BrowserCommandControllerBrowserTest,
                       OpenDisabledForAppPopupBrowser) {
  auto params = Browser::CreateParams::CreateForAppPopup(
      "abcdefghaghpphfffooibmlghaeopach", true /* trusted_source */,
      gfx::Rect(), /* window_bounts */
      browser()->profile(), true /* user_gesture */);
  Browser* browser = Browser::Create(params);

  chrome::BrowserCommandController* commandController =
      browser->command_controller();
  ASSERT_EQ(false, commandController->IsCommandEnabled(IDC_OPEN_FILE));
}

IN_PROC_BROWSER_TEST_F(BrowserCommandControllerBrowserTest,
                       OpenDisabledForDevToolsBrowser) {
  auto params = Browser::CreateParams::CreateForDevTools(browser()->profile());
  Browser* browser = Browser::Create(params);

  chrome::BrowserCommandController* commandController =
      browser->command_controller();
  ASSERT_EQ(false, commandController->IsCommandEnabled(IDC_OPEN_FILE));
}

#if !BUILDFLAG(IS_CHROMEOS_ASH)
IN_PROC_BROWSER_TEST_F(BrowserCommandControllerBrowserTestRefreshOnly,
                       ExecuteProfileMenuCustomizeChrome) {
  EXPECT_TRUE(chrome::ExecuteCommand(browser(), IDC_CUSTOMIZE_CHROME));
  content::WebContents* web_contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  content::WaitForLoadStop(web_contents);
  EXPECT_EQ(web_contents->GetURL().possibly_invalid_spec(),
            "chrome://settings/manageProfile");
}

IN_PROC_BROWSER_TEST_F(BrowserCommandControllerBrowserTestRefreshOnly,
                       ExecuteProfileMenuManageGoogleAccount) {
  signin::IdentityManager* identity_manager =
      IdentityManagerFactory::GetForProfile(browser()->profile());
  CoreAccountInfo account_info = signin::SetPrimaryAccount(
      identity_manager, "user@example.com", signin::ConsentLevel::kSignin);
  chrome::UpdateCommandEnabled(browser(), IDC_MANAGE_GOOGLE_ACCOUNT, true);
  EXPECT_TRUE(chrome::ExecuteCommand(browser(), IDC_MANAGE_GOOGLE_ACCOUNT));
}

IN_PROC_BROWSER_TEST_F(BrowserCommandControllerBrowserTestRefreshOnly,
                       ExecuteProfileMenuCloseProfile) {
  EXPECT_TRUE(chrome::ExecuteCommand(browser(), IDC_CLOSE_PROFILE));
}

IN_PROC_BROWSER_TEST_F(BrowserCommandControllerBrowserTestRefreshOnly,
                       ExecuteShowSyncSettings) {
  EXPECT_TRUE(chrome::ExecuteCommand(browser(), IDC_SHOW_SYNC_SETTINGS));
  content::WebContents* web_contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  content::WaitForLoadStop(web_contents);
  EXPECT_EQ(web_contents->GetURL().possibly_invalid_spec(),
            "chrome://settings/syncSetup");
}

IN_PROC_BROWSER_TEST_F(BrowserCommandControllerBrowserTestRefreshOnly,
                       ExecuteProfileMenuOpenGuestProfile) {
  EXPECT_TRUE(chrome::ExecuteCommand(browser(), IDC_OPEN_GUEST_PROFILE));
  Browser* guest_browser = ui_test_utils::WaitForBrowserToOpen();
  ASSERT_TRUE(guest_browser);
  ASSERT_TRUE(guest_browser->profile()->IsGuestSession());
}

IN_PROC_BROWSER_TEST_F(BrowserCommandControllerBrowserTestRefreshOnly,
                       ExecuteTurnOnSync) {
  EXPECT_TRUE(chrome::ExecuteCommand(browser(), IDC_TURN_ON_SYNC));
}

IN_PROC_BROWSER_TEST_F(BrowserCommandControllerBrowserTestRefreshOnly,
                       ExecuteShowSigninWhenPaused) {
  signin::IdentityManager* identity_manager =
      IdentityManagerFactory::GetForProfile(browser()->profile());
  signin::MakePrimaryAccountAvailable(identity_manager, "user@example.com",
                                      signin::ConsentLevel::kSync);
  signin::SetRefreshTokenForPrimaryAccount(identity_manager);
  signin::SetInvalidRefreshTokenForPrimaryAccount(identity_manager);
  EXPECT_TRUE(chrome::ExecuteCommand(browser(), IDC_SHOW_SIGNIN_WHEN_PAUSED));
}

IN_PROC_BROWSER_TEST_F(BrowserCommandControllerBrowserTestRefreshOnly,
                       ExecuteProfileMenuAddNewProfile) {
  EXPECT_TRUE(chrome::ExecuteCommand(browser(), IDC_ADD_NEW_PROFILE));
  profiles::testing::WaitForPickerLoadStop(
      GURL("chrome://profile-picker/new-profile"));
  EXPECT_TRUE(ProfilePicker::IsOpen());
}

IN_PROC_BROWSER_TEST_F(BrowserCommandControllerBrowserTestRefreshOnly,
                       ExecuteProfileMenuManageChromeProfiles) {
  EXPECT_TRUE(chrome::ExecuteCommand(browser(), IDC_MANAGE_CHROME_PROFILES));
  profiles::testing::WaitForPickerWidgetCreated();
  EXPECT_TRUE(ProfilePicker::IsOpen());
}

#endif
IN_PROC_BROWSER_TEST_F(BrowserCommandControllerBrowserTestRefreshOnly,
                       ShowTranslateStatusChromePage) {
  ASSERT_TRUE(embedded_test_server()->Start());
  GURL url = GURL("chrome://new-tab-page/");
  translate::TranslateManager::SetIgnoreMissingKeyForTesting(true);
  net::NetworkChangeNotifier::CreateMockIfNeeded();
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), url));

  browser()->command_controller()->TabStateChanged();

  EXPECT_FALSE(
      browser()->command_controller()->IsCommandEnabled(IDC_SHOW_TRANSLATE));
}

IN_PROC_BROWSER_TEST_F(BrowserCommandControllerBrowserTestRefreshOnly,
                       ShowTranslateStatusEnglishPage) {
  LoadAndWaitForLanguage("/english_page.html");
  EXPECT_TRUE(
      browser()->command_controller()->IsCommandEnabled(IDC_SHOW_TRANSLATE));
}

IN_PROC_BROWSER_TEST_F(BrowserCommandControllerBrowserTestRefreshOnly,
                       ShowTranslateStatusFrenchPage) {
  LoadAndWaitForLanguage("/french_page.html");
  EXPECT_TRUE(
      browser()->command_controller()->IsCommandEnabled(IDC_SHOW_TRANSLATE));
}

IN_PROC_BROWSER_TEST_F(BrowserCommandControllerBrowserTestRefreshOnly,
                       ExecuteShowTranslateBubble) {
  LoadAndWaitForLanguage("/french_page.html");
  EXPECT_TRUE(chrome::ExecuteCommand(browser(), IDC_SHOW_TRANSLATE));
}

}  // namespace chrome
