// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/memory/raw_ptr.h"

#import "chrome/browser/app_controller_mac.h"

#include <dispatch/dispatch.h>
#include <stddef.h>

#include <memory>
#include <vector>

#include "base/auto_reset.h"
#include "base/bind.h"
#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/mac/foundation_util.h"
#include "base/mac/mac_util.h"
#include "base/metrics/histogram_macros.h"
#include "base/run_loop.h"
#include "base/scoped_multi_source_observation.h"
#include "base/scoped_observation.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/sys_string_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/thread_pool.h"
#include "base/threading/scoped_blocking_call.h"
#include "base/threading/thread_restrictions.h"
#include "build/branding_buildflags.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/app/notification_metrics.h"
#include "chrome/browser/apps/app_shim/app_shim_termination_manager.h"
#include "chrome/browser/apps/platform_apps/app_window_registry_util.h"
#include "chrome/browser/browser_features.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/browser_process_platform_part.h"
#include "chrome/browser/command_updater_impl.h"
#include "chrome/browser/download/download_core_service.h"
#include "chrome/browser/download/download_core_service_factory.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/first_run/first_run.h"
#include "chrome/browser/lifetime/application_lifetime.h"
#include "chrome/browser/lifetime/browser_shutdown.h"
#include "chrome/browser/mac/auth_session_request.h"
#include "chrome/browser/mac/key_window_notifier.h"
#include "chrome/browser/mac/mac_startup_profiler.h"
#include "chrome/browser/policy/chrome_browser_policy_connector.h"
#include "chrome/browser/prefs/incognito_mode_prefs.h"
#include "chrome/browser/profiles/keep_alive/profile_keep_alive_types.h"
#include "chrome/browser/profiles/profile_attributes_entry.h"
#include "chrome/browser/profiles/profile_attributes_storage.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/profiles/profile_manager_observer.h"
#include "chrome/browser/profiles/profile_observer.h"
#include "chrome/browser/profiles/profiles_state.h"
#include "chrome/browser/sessions/session_restore.h"
#include "chrome/browser/sessions/session_service.h"
#include "chrome/browser/sessions/session_service_factory.h"
#include "chrome/browser/sessions/tab_restore_service_factory.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_command_controller.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_dialogs.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/browser/ui/browser_live_tab_context.h"
#include "chrome/browser/ui/browser_mac.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/chrome_pages.h"
#import "chrome/browser/ui/cocoa/apps/app_shim_menu_controller_mac.h"
#include "chrome/browser/ui/cocoa/apps/quit_with_apps_controller_mac.h"
#import "chrome/browser/ui/cocoa/bookmarks/bookmark_menu_bridge.h"
#import "chrome/browser/ui/cocoa/confirm_quit.h"
#import "chrome/browser/ui/cocoa/confirm_quit_panel_controller.h"
#include "chrome/browser/ui/cocoa/handoff_observer.h"
#import "chrome/browser/ui/cocoa/history_menu_bridge.h"
#include "chrome/browser/ui/cocoa/last_active_browser_cocoa.h"
#import "chrome/browser/ui/cocoa/profiles/profile_menu_controller.h"
#import "chrome/browser/ui/cocoa/share_menu_controller.h"
#import "chrome/browser/ui/cocoa/tab_menu_bridge.h"
#include "chrome/browser/ui/extensions/application_launch.h"
#include "chrome/browser/ui/profile_picker.h"
#include "chrome/browser/ui/startup/startup_browser_creator.h"
#include "chrome/browser/ui/startup/startup_browser_creator_impl.h"
#include "chrome/browser/ui/startup/startup_tab.h"
#include "chrome/browser/ui/startup/startup_types.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/web_applications/os_integration/web_app_shortcut_mac.h"
#include "chrome/browser/web_applications/web_app_helpers.h"
#include "chrome/common/chrome_features.h"
#include "chrome/common/chrome_paths_internal.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/extensions/extension_constants.h"
#include "chrome/common/mac/app_mode_common.h"
#include "chrome/common/pref_names.h"
#include "chrome/common/url_constants.h"
#include "chrome/common/webui_url_constants.h"
#include "chrome/grit/chromium_strings.h"
#include "chrome/grit/generated_resources.h"
#include "components/enterprise/browser/controller/chrome_browser_cloud_management_controller.h"
#include "components/handoff/handoff_manager.h"
#include "components/handoff/handoff_utility.h"
#include "components/keep_alive_registry/keep_alive_types.h"
#include "components/keep_alive_registry/scoped_keep_alive.h"
#include "components/prefs/pref_service.h"
#include "components/sessions/core/tab_restore_service.h"
#include "content/public/browser/download_manager.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/extension_system.h"
#include "net/base/filename_util.h"
#include "net/base/mac/url_conversions.h"
#import "ui/base/cocoa/nsmenu_additions.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/l10n/l10n_util_mac.h"
#include "ui/native_theme/native_theme_mac.h"
#include "ui/native_theme/native_theme_observer.h"
#include "url/gurl.h"

namespace {

// How long we allow a workspace change notification to wait to be
// associated with a dock activation. The animation lasts 250ms. See
// applicationShouldHandleReopen:hasVisibleWindows:.
static const int kWorkspaceChangeTimeoutMs = 500;

// True while AppController is calling chrome::NewEmptyWindow(). We need a
// global flag here, analogue to StartupBrowserCreator::InProcessStartup()
// because otherwise the SessionService will try to restore sessions when we
// make a new window while there are no other active windows.
bool g_is_opening_new_window = false;

// Stores the pending web auth requests (typically while the profile is being
// loaded) until they are passed to the AuthSessionRequest class.
NSMutableDictionary* GetPendingWebAuthRequests() API_AVAILABLE(macos(10.15)) {
  static NSMutableDictionary* g_pending_requests =
      [[NSMutableDictionary alloc] init];
  return g_pending_requests;
}

// Open the urls in the last used browser from a regular profile.
void OpenUrlsInBrowserWithProfile(const std::vector<GURL>& urls,
                                  Profile* profile);

// Starts a web authentication session request.
void BeginHandlingWebAuthenticationSessionRequestWithProfile(
    ASWebAuthenticationSessionRequest* request,
    Profile* profile) API_AVAILABLE(macos(10.15)) {
  NSUUID* key = [request UUID];
  if (![GetPendingWebAuthRequests() objectForKey:key])
    return;  // The request has been canceled, do not start the session.
  [GetPendingWebAuthRequests() removeObjectForKey:key];
  // If there is no safe profile, |profile| is nullptr, and the session will
  // fail immediately.
  AuthSessionRequest::StartNewAuthSession(request, profile);
}

// Activates a browser window having the given profile (the last one active) if
// possible and returns a pointer to the activate |Browser| or NULL if this was
// not possible. If the last active browser is minimized (in particular, if
// there are only minimized windows), it will unminimize it.
Browser* ActivateBrowser(Profile* profile) {
  Browser* browser = chrome::FindLastActiveWithProfile(
      profile->IsGuestSession()
          ? profile->GetPrimaryOTRProfile(/*create_if_needed=*/true)
          : profile);
  if (browser)
    browser->window()->Activate();
  return browser;
}

// Creates an empty browser window with the given profile and returns a pointer
// to the new |Browser|.
Browser* CreateBrowser(Profile* profile) {
  {
    base::AutoReset<bool> auto_reset_in_run(&g_is_opening_new_window, true);
    chrome::NewEmptyWindow(profile);
  }

  Browser* browser = chrome::GetLastActiveBrowser();
  CHECK(browser);
  return browser;
}

// Activates a browser window having the given profile (the last one active) if
// possible or creates an empty one if necessary. Returns a pointer to the
// activated/new |Browser|.
Browser* ActivateOrCreateBrowser(Profile* profile) {
  if (Browser* browser = ActivateBrowser(profile))
    return browser;
  return CreateBrowser(profile);
}

CFStringRef BaseBundleID_CFString() {
  return base::mac::NSToCFCast(
      base::SysUTF8ToNSString(base::mac::BaseBundleID()));
}

// Record the location of the application bundle (containing the main framework)
// from which Chromium was loaded. This is used by app mode shims to find
// Chromium.
void RecordLastRunAppBundlePath() {
  // Going up three levels from |chrome::GetVersionedDirectory()| gives the
  // real, user-visible app bundle directory. (The alternatives give either the
  // framework's path or the initial app's path, which may be an app mode shim
  // or a unit test.)
  base::ScopedBlockingCall scoped_blocking_call(FROM_HERE,
                                                base::BlockingType::MAY_BLOCK);

  // Go up five levels from the versioned sub-directory of the framework, which
  // is at C.app/Contents/Frameworks/C.framework/Versions/V.
  base::FilePath app_bundle_path = chrome::GetFrameworkBundlePath()
                                       .DirName()
                                       .DirName()
                                       .DirName()
                                       .DirName()
                                       .DirName();
  base::ScopedCFTypeRef<CFStringRef> app_bundle_path_cfstring(
      base::SysUTF8ToCFStringRef(app_bundle_path.value()));
  CFPreferencesSetAppValue(
      base::mac::NSToCFCast(app_mode::kLastRunAppBundlePathPrefsKey),
      app_bundle_path_cfstring, BaseBundleID_CFString());
}

bool IsProfileSignedOut(Profile* profile) {
  ProfileAttributesEntry* entry =
      g_browser_process->profile_manager()
          ->GetProfileAttributesStorage()
          .GetProfileAttributesWithPath(profile->GetPath());
  return entry && entry->IsSigninRequired();
}

void ConfigureNSAppForKioskMode() {
  NSApp.presentationOptions =
      NSApplicationPresentationHideDock | NSApplicationPresentationHideMenuBar |
      NSApplicationPresentationDisableProcessSwitching |
      NSApplicationPresentationDisableSessionTermination |
      NSApplicationPresentationDisableForceQuit |
      NSApplicationPresentationFullScreen;
}

// Returns the list of gfx::NativeWindows for all browser windows (excluding
// apps).
std::set<gfx::NativeWindow> GetBrowserNativeWindows() {
  std::set<gfx::NativeWindow> result;
  for (auto* browser : *BrowserList::GetInstance()) {
    if (!browser)
      continue;
    // When focusing Chrome, don't focus any browser windows associated with
    // an app.
    // https://crbug.com/960904
    if (browser->is_type_app())
      continue;
    result.insert(browser->window()->GetNativeWindow());
  }
  return result;
}

void FocusWindowSetOnCurrentSpace(const std::set<gfx::NativeWindow>& windows) {
  // This callback runs before AppKit picks its own window to
  // deminiaturize, so we get to pick one from the right set. Limit to
  // the windows on the current workspace. Otherwise we jump spaces
  // haphazardly.
  //
  // Also consider both visible and hidden windows; this call races
  // with the system unhiding the application. http://crbug.com/368238
  //
  // NOTE: If this is called in the
  // applicationShouldHandleReopen:hasVisibleWindows: hook when
  // clicking the dock icon, and that caused macOS to begin switch
  // spaces, isOnActiveSpace gives the answer for the PREVIOUS
  // space. This means that we actually raise and focus the wrong
  // space's windows, leaving the new key window off-screen. To detect
  // this, check if the key window is on the active space prior to
  // calling.
  //
  // Also, if we decide to deminiaturize a window during a space switch,
  // that can switch spaces and then switch back. Fortunately, this only
  // happens if, say, space 1 contains an app, space 2 contains a
  // miniaturized browser. We click the icon, macOS switches to space 1,
  // we deminiaturize the browser, and that triggers switching back.
  //
  // TODO(davidben): To limit those cases, consider preferentially
  // deminiaturizing a window on the current space.
  NSWindow* frontmost_window = nil;
  NSWindow* frontmost_miniaturized_window = nil;
  bool all_miniaturized = true;
  for (NSWindow* win in [[NSApp orderedWindows] reverseObjectEnumerator]) {
    if (windows.find(win) == windows.end())
      continue;
    if ([win isMiniaturized]) {
      frontmost_miniaturized_window = win;
    } else if ([win isVisible]) {
      all_miniaturized = false;
      if ([win isOnActiveSpace]) {
        // Raise the old |frontmost_window| (if any). The topmost |win| will be
        // raised with makeKeyAndOrderFront: below.
        [frontmost_window orderFront:nil];
        frontmost_window = win;
      }
    }
  }
  if (all_miniaturized && frontmost_miniaturized_window) {
    DCHECK(!frontmost_window);
    // Note the call to makeKeyAndOrderFront: will deminiaturize the window.
    frontmost_window = frontmost_miniaturized_window;
  }

  if (frontmost_window) {
    [frontmost_window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
  }
}

// Returns the profile path to be used at startup.
base::FilePath GetStartupProfilePathMac() {
  // This profile path is used to open URLs passed in application:openURLs: and
  // should not default to Guest when the profile picker is shown.
  // TODO(https://crbug.com/1155158): Remove the ignore_profile_picker parameter
  // once the picker supports opening URLs.
  StartupProfilePathInfo profile_path_info = GetStartupProfilePath(
      /*cur_dir=*/base::FilePath(), *base::CommandLine::ForCurrentProcess(),
      /*ignore_profile_picker=*/true);
  DCHECK_EQ(profile_path_info.mode, StartupProfileMode::kBrowserWindow);
  return profile_path_info.path;
}

// Open the urls in the last used browser. Loads the profile asynchronously if
// needed.
void OpenUrlsInBrowser(const std::vector<GURL>& urls) {
  app_controller_mac::RunInLastProfileSafely(
      base::BindOnce(&OpenUrlsInBrowserWithProfile, urls),
      app_controller_mac::kShowProfilePickerOnFailure);
}

}  // namespace

// This is extracted as a standalone function in order
// to be friend with base::ScopedAllowBlocking.
Profile* GetLastProfileMac() {
  ProfileManager* profile_manager = g_browser_process->profile_manager();
  if (!profile_manager)
    return nullptr;

  base::FilePath profile_path = GetStartupProfilePathMac();
  // ProfileManager::GetProfile() is blocking if the profile was not loaded yet.
  // TODO(https://crbug.com/1176734): Change this code to return nullptr when
  // the profile is not loaded, and update all callers to handle this case.
  base::ScopedAllowBlocking allow_blocking;
  return profile_manager->GetProfile(profile_path);
}

@interface AppController () <HandoffObserverDelegate>
- (void)initMenuState;
- (void)initProfileMenu;
- (void)updateConfirmToQuitPrefMenuItem:(NSMenuItem*)item;
- (void)registerServicesMenuTypesTo:(NSApplication*)app;
- (void)activeSpaceDidChange:(NSNotification*)inNotification;
- (void)checkForAnyKeyWindows;
- (BOOL)userWillWaitForInProgressDownloads:(int)downloadCount;
- (BOOL)shouldQuitWithInProgressDownloads;
- (void)profileWasRemoved:(const base::FilePath&)profilePath
             forIncognito:(bool)isIncognito;

// This class cannot open urls until startup has finished. The urls that cannot
// be opened are cached in |startupUrls_|. This method must be called exactly
// once after startup has completed. It opens the urls in |startupUrls_|, and
// clears |startupUrls_|.
- (void)openStartupUrls;

// Opens a tab for each GURL in |urls|. If there is exactly one tab open before
// this method is called, and that tab is the NTP, then this method closes the
// NTP after all the |urls| have been opened.
- (void)openUrlsReplacingNTP:(const std::vector<GURL>&)urls;

// Returns |YES| if |webContents| can be sent to another device via Handoff.
- (BOOL)isHandoffEligible:(content::WebContents*)webContents;

// This method passes |handoffURL| and |handoffTitle| to |handoffManager_|.
// This is a separate method (vs. being inlined into `updateHandoffManager`
// below) so that it can be swizzled in tests.
- (void)updateHandoffManagerWithURL:(const GURL&)handoffURL
                              title:(const std::u16string&)handoffTitle;

// Lazily creates the Handoff Manager. Updates the state of the Handoff
// Manager. This method is idempotent. This should be called:
// - During initialization.
// - When the current tab navigates to a new URL.
// - When the active browser changes.
// - When the active browser's active tab switches.
// |webContents| should be the new, active WebContents.
- (void)updateHandoffManager:(content::WebContents*)webContents;

// Return false if Chrome startup is paused by dialog and AppController is
// called without any initialized Profile.
- (BOOL)isProfileReady;
@end

class AppControllerProfileObserver : public ProfileAttributesStorage::Observer,
                                     public ProfileManagerObserver,
                                     public ProfileObserver {
 public:
  AppControllerProfileObserver(
      ProfileManager* profile_manager, AppController* app_controller)
      : profile_manager_(profile_manager),
        app_controller_(app_controller) {
    DCHECK(profile_manager_);
    DCHECK(app_controller_);
    // Listen to ProfileObserver and ProfileManagerObserver, if either one of
    // kDestroyProfileOnBrowserClose or kUpdateHistoryEntryPointsInIncognito
    // are enabled.
    if (ObserveRegularProfiles() || ObserveOTRProfiles()) {
      profile_manager_observer_.Observe(profile_manager_.get());
      for (Profile* profile : profile_manager_->GetLoadedProfiles()) {
        profile_observers_.AddObservation(profile);
        Profile* otr_profile =
            profile->GetPrimaryOTRProfile(/*create_if_needed=*/false);
        if (otr_profile && ObserveOTRProfiles())
          profile_observers_.AddObservation(otr_profile);
      }
    }
    storage_observer_.Observe(&profile_manager_->GetProfileAttributesStorage());
  }

  AppControllerProfileObserver(const AppControllerProfileObserver&) = delete;
  AppControllerProfileObserver& operator=(const AppControllerProfileObserver&) =
      delete;

  ~AppControllerProfileObserver() override = default;

 private:
  // ProfileAttributesStorage::Observer implementation:

  // `ProfileAttributesStorage::Observer::OnProfileAdded()` must be explicitly
  // defined even if it's empty, because of the competing overload
  // `ProfileManager::Observer::OnProfileAdded()`.
  void OnProfileAdded(const base::FilePath& profile_path) override {}

  void OnProfileWasRemoved(const base::FilePath& profile_path,
                           const std::u16string& profile_name) override {
    // When a profile is deleted we need to notify the AppController,
    // so it can correctly update its pointer to the last used profile.
    [app_controller_ profileWasRemoved:profile_path forIncognito:false];
  }

  // ProfileManager::Observer implementation:
  void OnProfileAdded(Profile* profile) override {
    if (!ObserveRegularProfiles() && !ObserveOTRProfiles())
      return;
    profile_observers_.AddObservation(profile);
  }

  // ProfileObserver implementation:
  void OnProfileWillBeDestroyed(Profile* profile) override {
    profile_observers_.RemoveObservation(profile);

    bool is_profile_observed = profile->IsOffTheRecord()
                                   ? ObserveOTRProfiles()
                                   : ObserveRegularProfiles();

    // If the profile is not observed, then no need to call rest.
    if (!is_profile_observed)
      return;

    [app_controller_ profileWasRemoved:profile->GetPath()
                          forIncognito:profile->IsOffTheRecord()];
  }

  void OnOffTheRecordProfileCreated(Profile* off_the_record) override {
    // If off_the_record profiles are not observed, then do not add observer.
    if (!ObserveOTRProfiles()) {
      return;
    }
    profile_observers_.AddObservation(off_the_record);
  }

  static bool ObserveRegularProfiles() {
    return base::FeatureList::IsEnabled(
        features::kDestroyProfileOnBrowserClose);
  }

  static bool ObserveOTRProfiles() {
    return base::FeatureList::IsEnabled(
        features::kUpdateHistoryEntryPointsInIncognito);
  }

  base::ScopedMultiSourceObservation<Profile, ProfileObserver>
      profile_observers_{this};
  base::ScopedObservation<ProfileAttributesStorage,
                          ProfileAttributesStorage::Observer>
      storage_observer_{this};
  base::ScopedObservation<ProfileManager, ProfileManagerObserver>
      profile_manager_observer_{this};

  const raw_ptr<ProfileManager> profile_manager_;
  AppController* const app_controller_;  // Weak; owns us.
};

class AppControllerNativeThemeObserver : public ui::NativeThemeObserver {
 public:
  AppControllerNativeThemeObserver(AppController* app_controller)
      : app_controller_(app_controller) {
    native_theme_observation_.Observe(
        ui::NativeThemeMac::GetInstanceForNativeUi());
  }

  // NativeThemeObserver:
  void OnNativeThemeUpdated(ui::NativeTheme* observed_theme) override {
    [app_controller_ nativeThemeDidChange];
  }

 private:
  base::ScopedObservation<ui::NativeTheme, ui::NativeThemeObserver>
      native_theme_observation_{this};
  AppController* const app_controller_;  // Weak; owns us.
};

@implementation AppController

@synthesize startupComplete = _startupComplete;

- (instancetype)init {
  if (self = [super init]) {
    // -[NSMenu cr_menuItemForKeyEquivalentEvent:] lives in /content, but
    // we need to execute special update code before the search begins.
    // Setting this block gives us the hook we need.
    [NSMenu cr_setMenuItemForKeyEquivalentEventPreSearchBlock:^{
      // We avoid calling -[NSMenuDelegate menuNeedsUpdate:] on each submenu's
      // delegate as that can be slow. Instead, we update the relevant
      // NSMenuItems if [NSApp delegate] is an instance of AppController. See
      // https://crbug.com/851260#c4 .
      [base::mac::ObjCCast<AppController>([NSApp delegate])
          updateMenuItemKeyEquivalents];
    }];
  }
  return self;
}

- (void)dealloc {
  [[_closeTabMenuItem menu] setDelegate:nil];
  [super dealloc];
}

// This method is called very early in application startup (ie, before
// the profile is loaded or any preferences have been registered). Defer any
// user-data initialization until -applicationDidFinishLaunching:.
- (void)mainMenuCreated {
  MacStartupProfiler::GetInstance()->Profile(
      MacStartupProfiler::AWAKE_FROM_NIB);
  NSNotificationCenter* notificationCenter =
      [NSNotificationCenter defaultCenter];
  [notificationCenter
      addObserver:self
         selector:@selector(windowDidResignKey:)
             name:NSWindowDidResignKeyNotification
           object:nil];
  [notificationCenter
      addObserver:self
         selector:@selector(windowDidBecomeMain:)
             name:NSWindowDidBecomeMainNotification
           object:nil];

  // Register for space change notifications.
  [[[NSWorkspace sharedWorkspace] notificationCenter]
    addObserver:self
       selector:@selector(activeSpaceDidChange:)
           name:NSWorkspaceActiveSpaceDidChangeNotification
         object:nil];

  [[[NSWorkspace sharedWorkspace] notificationCenter]
      addObserver:self
         selector:@selector(willPowerOff:)
             name:NSWorkspaceWillPowerOffNotification
           object:nil];

  NSMenu* fileMenu = [[[NSApp mainMenu] itemWithTag:IDC_FILE_MENU] submenu];
  _closeTabMenuItem = [fileMenu itemWithTag:IDC_CLOSE_TAB];
  DCHECK(_closeTabMenuItem);
  _closeWindowMenuItem = [fileMenu itemWithTag:IDC_CLOSE_WINDOW];
  DCHECK(_closeWindowMenuItem);

  // Set up the command updater for when there are no windows open
  [self initMenuState];

  // Initialize the Profile menu.
  [self initProfileMenu];
}

- (void)unregisterEventHandlers {
  [[NSNotificationCenter defaultCenter] removeObserver:self];
  [[[NSWorkspace sharedWorkspace] notificationCenter] removeObserver:self];
}

// (NSApplicationDelegate protocol) This is the Apple-approved place to override
// the default handlers.
- (void)applicationWillFinishLaunching:(NSNotification*)notification {
  MacStartupProfiler::GetInstance()->Profile(
      MacStartupProfiler::WILL_FINISH_LAUNCHING);

  NSWindow.allowsAutomaticWindowTabbing = NO;

  [self initShareMenu];
}

- (BOOL)tryToTerminateApplication:(NSApplication*)app {
  // Reset this now that we've received the call to terminate.
  BOOL isPoweringOff = _isPoweringOff;
  _isPoweringOff = NO;

  // Check for in-process downloads, and prompt the user if they really want
  // to quit (and thus cancel downloads). Only check if we're not already
  // shutting down, else the user might be prompted multiple times if the
  // download isn't stopped before terminate is called again.
  if (!browser_shutdown::IsTryingToQuit() &&
      ![self shouldQuitWithInProgressDownloads])
    return NO;

  // TODO(viettrungluu): Remove Apple Event handlers here? (It's safe to leave
  // them in, but I'm not sure about UX; we'd also want to disable other things
  // though.) http://crbug.com/40861

  // Check for active apps. If quitting is prevented, only close browsers and
  // sessions.
  if (!browser_shutdown::IsTryingToQuit() && !isPoweringOff &&
      _quitWithAppsController.get() && !_quitWithAppsController->ShouldQuit()) {

    chrome::OnClosingAllBrowsers(true);
    // This will close all browser sessions.
    chrome::CloseAllBrowsers();

    // At this point, the user has already chosen to cancel downloads. If we
    // were to shut down as usual, the downloads would be cancelled in
    // DownloadCoreService::Shutdown().
    DownloadCoreService::CancelAllDownloads();

    return NO;
  }

  size_t num_browsers = chrome::GetTotalBrowserCount();

  // Initiate a shutdown (via chrome::CloseAllBrowsersAndQuit()) if we aren't
  // already shutting down.
  if (!browser_shutdown::IsTryingToQuit()) {
    chrome::OnClosingAllBrowsers(true);
    chrome::CloseAllBrowsersAndQuit();
  }

  return num_browsers == 0 ? YES : NO;
}

- (void)stopTryingToTerminateApplication:(NSApplication*)app {
  if (browser_shutdown::IsTryingToQuit()) {
    // Reset the "trying to quit" state, so that closing all browser windows
    // will no longer lead to termination.
    browser_shutdown::SetTryingToQuit(false);

    // TODO(viettrungluu): Were we to remove Apple Event handlers above, we
    // would have to reinstall them here. http://crbug.com/40861
  }
}

- (BOOL)runConfirmQuitPanel {
  // If there are no windows, quit immediately.
  if (BrowserList::GetInstance()->empty() &&
      !AppWindowRegistryUtil::IsAppWindowVisibleInAnyProfile(0)) {
    return YES;
  }

  // Check if the preference is turned on.
  const PrefService* prefs = g_browser_process->local_state();
  if (!prefs->GetBoolean(prefs::kConfirmToQuitEnabled)) {
    confirm_quit::RecordHistogram(confirm_quit::kNoConfirm);
    return YES;
  }

  // Run only for keyboard-initiated quits.
  if ([[NSApp currentEvent] type] != NSEventTypeKeyDown)
    return NSTerminateNow;

  return [[ConfirmQuitPanelController sharedController]
      runModalLoopForApplication:NSApp];
}

// Called when the app is shutting down. Clean-up as appropriate.
- (void)applicationWillTerminate:(NSNotification*)aNotification {
  // There better be no browser windows left at this point.
  CHECK_EQ(0u, chrome::GetTotalBrowserCount());

  // Tell BrowserList not to keep the browser process alive. Once all the
  // browsers get dealloc'd, it will stop the RunLoop and fall back into main().
  _keep_alive.reset();

  // Reset local state watching, as this object outlives the prefs system.
  _localPrefRegistrar.RemoveAll();

  // It's safe to delete |_lastProfile| now.
  [self setLastProfile:nullptr];

  _profileAttributesStorageObserver.reset();
  [self unregisterEventHandlers];
  _appShimMenuController.reset();
  _profileBookmarkMenuBridgeMap.clear();
}

- (void)didEndMainMessageLoop {
  if (!_lastProfile) {
    // If only the profile picker is open and closed again, there is no profile
    // loaded when main message loop ends and we cannot load it from disk now.
    return;
  }
  DCHECK_EQ(0u, chrome::GetBrowserCount(_lastProfile));
  if (!chrome::GetBrowserCount(_lastProfile)) {
    // As we're shutting down, we need to nuke the TabRestoreService, which
    // will start the shutdown of the NavigationControllers and allow for
    // proper shutdown. If we don't do this, Chrome won't shut down cleanly,
    // and may end up crashing when some thread tries to use the IO thread (or
    // another thread) that is no longer valid.
    TabRestoreServiceFactory::ResetForProfile(_lastProfile);
  }
}

// If the window has a tab controller, make "close window" be cmd-shift-w,
// otherwise leave it as the normal cmd-w. Capitalization of the key equivalent
// affects whether the shift modifier is used.
- (void)adjustCloseWindowMenuItemKeyEquivalent:(BOOL)enableCloseTabShortcut {
  [_closeWindowMenuItem setKeyEquivalent:(enableCloseTabShortcut ? @"W" :
                                                                   @"w")];
  [_closeWindowMenuItem
      setKeyEquivalentModifierMask:NSEventModifierFlagCommand];
}

// If the window has a tab controller, make "close tab" take over cmd-w,
// otherwise it shouldn't have any key-equivalent because it should be disabled.
- (void)adjustCloseTabMenuItemKeyEquivalent:(BOOL)enableCloseTabShortcut {
  if (enableCloseTabShortcut) {
    [_closeTabMenuItem setKeyEquivalent:@"w"];
    [_closeTabMenuItem setKeyEquivalentModifierMask:NSEventModifierFlagCommand];
  } else {
    [_closeTabMenuItem setKeyEquivalent:@""];
    [_closeTabMenuItem setKeyEquivalentModifierMask:0];
  }
}

// See if the focused window window has tabs, and adjust the key equivalents for
// Close Tab/Close Window accordingly.
- (void)menuNeedsUpdate:(NSMenu*)menu {
  DCHECK(menu == [_closeTabMenuItem menu]);
  [self updateMenuItemKeyEquivalents];
}

- (void)windowDidResignKey:(NSNotification*)notify {
  // If a window is closed, this notification is fired but |[NSApp keyWindow]|
  // returns nil regardless of whether any suitable candidates for the key
  // window remain. It seems that the new key window for the app is not set
  // until after this notification is fired, so a check is performed after the
  // run loop is allowed to spin.
  [self performSelector:@selector(checkForAnyKeyWindows)
             withObject:nil
             afterDelay:0.0];
}

- (void)windowDidBecomeMain:(NSNotification*)notify {
  Browser* browser = chrome::FindBrowserWithWindow([notify object]);
  if (!browser)
    return;

  if (browser->is_type_normal()) {
    _tabMenuBridge = std::make_unique<TabMenuBridge>(
        browser->tab_strip_model(),
        [[NSApp mainMenu] itemWithTag:IDC_TAB_MENU]);
    _tabMenuBridge->BuildMenu();
  } else {
    _tabMenuBridge.reset();
  }

  Profile* profile = browser->profile();
  // If kUpdateHistoryEntryPointsInIncognito is not enabled, always pass
  // original profile.
  if (!base::FeatureList::IsEnabled(
          features::kUpdateHistoryEntryPointsInIncognito)) {
    profile = profile->GetOriginalProfile();
  }
  [self setLastProfile:profile];
  _lastActiveColorProvider = browser->window()->GetColorProvider();
}

- (void)activeSpaceDidChange:(NSNotification*)notify {
  if (_reopenTime.is_null() ||
      ![NSApp isActive] ||
      (base::TimeTicks::Now() - _reopenTime).InMilliseconds() >
      kWorkspaceChangeTimeoutMs) {
    return;
  }

  // The last applicationShouldHandleReopen:hasVisibleWindows: call
  // happened during a space change. Now that the change has
  // completed, raise browser windows.
  _reopenTime = base::TimeTicks();
  std::set<gfx::NativeWindow> browserWindows = GetBrowserNativeWindows();
  if (!browserWindows.empty())
    FocusWindowSetOnCurrentSpace(browserWindows);
}

// Called when shutting down or logging out.
- (void)willPowerOff:(NSNotification*)notify {
  // Don't attempt any shutdown here. Cocoa will shortly call
  // -[BrowserCrApplication terminate:].
  _isPoweringOff = YES;
}

- (void)checkForAnyKeyWindows {
  if ([NSApp keyWindow])
    return;

  g_browser_process->platform_part()->key_window_notifier().NotifyNoKeyWindow();
}

// If the auto-update interval is not set, make it 5 hours.
// Placed here for 2 reasons:
// 1) Same spot as other Pref stuff
// 2) Try and be friendly by keeping this after app launch
- (void)setUpdateCheckInterval {
#if BUILDFLAG(GOOGLE_CHROME_BRANDING)
  CFStringRef app = CFSTR("com.google.Keystone.Agent");
  CFStringRef checkInterval = CFSTR("checkInterval");
  CFPropertyListRef plist = CFPreferencesCopyAppValue(checkInterval, app);
  if (!plist) {
    const float fiveHoursInSeconds = 5.0 * 60.0 * 60.0;
    NSNumber* value = [NSNumber numberWithFloat:fiveHoursInSeconds];
    CFPreferencesSetAppValue(checkInterval, value, app);
    CFPreferencesAppSynchronize(app);
  }
#endif
}

- (void)openStartupUrls {
  DCHECK(_startupComplete);
  [self openUrlsReplacingNTP:_startupUrls];
  _startupUrls.clear();
}

- (void)openUrlsReplacingNTP:(const std::vector<GURL>&)urls {
  if (urls.empty())
    return;

  // On Mac, the URLs are passed in via Cocoa, not command line. The Chrome
  // NSApplication is created in MainMessageLoop, and then the shortcut urls
  // are passed in via Apple events. At this point, the first browser is
  // already loaded in PreMainMessageLoop. If we initialize NSApplication
  // before PreMainMessageLoop to capture shortcut URL events, it may cause
  // more problems because it relies on things created in PreMainMessageLoop
  // and may break existing message loop design.

  // If the browser hasn't started yet, just queue up the URLs.
  if (!_startupComplete) {
    _startupUrls.insert(_startupUrls.end(), urls.begin(), urls.end());
    return;
  }

  OpenUrlsInBrowser(urls);
}

// This is called after profiles have been loaded and preferences registered.
// It is safe to access the default profile here.
- (void)applicationDidFinishLaunching:(NSNotification*)notify {
  // Check if Chrome got launched by clicking on a notification.
  if ([notify userInfo][NSApplicationLaunchUserNotificationKey])
    LogLaunchedViaNotificationAction(NotificationActionSource::kBrowser);

  if (g_browser_process->browser_policy_connector()
          ->chrome_browser_cloud_management_controller()
          ->IsEnterpriseStartupDialogShowing()) {
    // As Chrome is not ready when the Enterprise startup dialog is being shown.
    // Store the notification as it will be reposted when the dialog is closed.
    return;
  }

  MacStartupProfiler::GetInstance()->Profile(
      MacStartupProfiler::DID_FINISH_LAUNCHING);
  MacStartupProfiler::GetInstance()->RecordMetrics();

  // Notify BrowserList to keep the application running so it doesn't go away
  // when all the browser windows get closed.
  _keep_alive = std::make_unique<ScopedKeepAlive>(
      KeepAliveOrigin::APP_CONTROLLER, KeepAliveRestartOption::DISABLED);

  [self setUpdateCheckInterval];

  // Start managing the menu for app windows. This needs to be done here because
  // main menu item titles are not yet initialized in awakeFromNib.
  [self initAppShimMenuController];

  // If enabled, keep Chrome alive when apps are open instead of quitting all
  // apps.
  _quitWithAppsController = new QuitWithAppsController();

  // Dynamically update shortcuts for "Close Window" and "Close Tab" menu items.
  [[_closeTabMenuItem menu] setDelegate:self];

  // Instantiate the ProfileAttributesStorage observer so that we can get
  // notified when a profile is deleted.
  _profileAttributesStorageObserver =
      std::make_unique<AppControllerProfileObserver>(
          g_browser_process->profile_manager(), self);

  // Observe native theme change (e.g. light and dark mode).
  _nativeThemeObserver =
      std::make_unique<AppControllerNativeThemeObserver>(self);

  // Record the path to the (browser) app bundle; this is used by the app mode
  // shim.
  if (base::mac::AmIBundled()) {
    base::ThreadPool::PostTask(
        FROM_HERE,
        {base::MayBlock(), base::TaskPriority::BEST_EFFORT,
         base::TaskShutdownBehavior::CONTINUE_ON_SHUTDOWN},
        base::BindOnce(&RecordLastRunAppBundlePath));
  }

  // Makes "Services" menu items available.
  [self registerServicesMenuTypesTo:[notify object]];

  _startupComplete = YES;

  if (base::CommandLine::ForCurrentProcess()->HasSwitch(switches::kKioskMode))
    ConfigureNSAppForKioskMode();

  Browser* browser = chrome::FindLastActive();
  content::WebContents* activeWebContents = nullptr;
  _lastActiveColorProvider = nullptr;
  if (browser) {
    activeWebContents = browser->tab_strip_model()->GetActiveWebContents();
    _lastActiveColorProvider = browser->window()->GetColorProvider();
  }
  [self updateHandoffManager:activeWebContents];
  [self openStartupUrls];

  PrefService* localState = g_browser_process->local_state();
  if (localState) {
    _localPrefRegistrar.Init(localState);
    _localPrefRegistrar.Add(
        prefs::kAllowFileSelectionDialogs,
        base::BindRepeating(
            [](CommandUpdater* commandUpdater) {
              bool enabled = g_browser_process->local_state()->GetBoolean(
                  prefs::kAllowFileSelectionDialogs);
              commandUpdater->UpdateCommandEnabled(IDC_OPEN_FILE, enabled);
            },
            _menuState.get()));
  }

  _handoff_observer = std::make_unique<HandoffObserver>(self);

  if (@available(macOS 10.15, *)) {
    ASWebAuthenticationSessionWebBrowserSessionManager.sharedManager
        .sessionHandler = self;
  }
}

// Helper function for populating and displaying the in progress downloads at
// exit alert panel.
- (BOOL)userWillWaitForInProgressDownloads:(int)downloadCount {
  NSString* titleText = nil;
  NSString* explanationText = nil;
  NSString* waitTitle = nil;
  NSString* exitTitle = nil;

  // Set the dialog text based on whether or not there are multiple downloads.
  // Dialog text: warning and explanation.
  titleText = l10n_util::GetPluralNSStringF(IDS_ABANDON_DOWNLOAD_DIALOG_TITLE,
                                            downloadCount);
  explanationText =
      l10n_util::GetNSString(IDS_ABANDON_DOWNLOAD_DIALOG_BROWSER_MESSAGE);
  // "Cancel download and exit" button text.
  exitTitle = l10n_util::GetNSString(IDS_ABANDON_DOWNLOAD_DIALOG_EXIT_BUTTON);

  // "Wait for download" button text.
  waitTitle =
      l10n_util::GetNSString(IDS_ABANDON_DOWNLOAD_DIALOG_CONTINUE_BUTTON);

  base::scoped_nsobject<NSAlert> alert([[NSAlert alloc] init]);
  [alert setMessageText:titleText];
  [alert setInformativeText:explanationText];
  [alert addButtonWithTitle:waitTitle];
  [alert addButtonWithTitle:exitTitle];

  // 'waitButton' is the default choice.
  int choice = [alert runModal];
  return choice == NSAlertFirstButtonReturn ? YES : NO;
}

// Check all profiles for in progress downloads, and if we find any, prompt the
// user to see if we should continue to exit (and thus cancel the downloads), or
// if we should wait.
- (BOOL)shouldQuitWithInProgressDownloads {
  ProfileManager* profile_manager = g_browser_process->profile_manager();
  if (!profile_manager)
    return YES;

  std::vector<Profile*> profiles(profile_manager->GetLoadedProfiles());

  std::vector<Profile*> added_profiles;
  for (Profile* profile : profiles) {
    for (Profile* otr : profile->GetAllOffTheRecordProfiles())
      added_profiles.push_back(otr);
  }
  profiles.insert(profiles.end(), added_profiles.begin(), added_profiles.end());

  for (Profile* profile : profiles) {
    DownloadCoreService* download_core_service =
        DownloadCoreServiceFactory::GetForBrowserContext(profile);
    content::DownloadManager* download_manager =
        (download_core_service->HasCreatedDownloadManager()
             ? profile->GetDownloadManager()
             : nullptr);
    if (download_manager &&
        download_manager->NonMaliciousInProgressCount() > 0) {
      int downloadCount = download_manager->NonMaliciousInProgressCount();
      if ([self userWillWaitForInProgressDownloads:downloadCount]) {
        // Create a new browser window (if necessary) and navigate to the
        // downloads page if the user chooses to wait.
        Browser* browser = chrome::FindBrowserWithProfile(profile);
        if (!browser) {
          browser = Browser::Create(Browser::CreateParams(profile, true));
          browser->window()->Show();
        }
        DCHECK(browser);
        chrome::ShowDownloads(browser);
        return NO;
      }

      // User wants to exit.
      return YES;
    }
  }

  // No profiles or active downloads found, okay to exit.
  return YES;
}

// Called to determine if we should enable the "restore tab" menu item.
// Checks with the TabRestoreService to see if there's anything there to
// restore and returns YES if so.
//
// In the zero-profile state, use the value from when the last profile was
// still loaded (if ever).
- (BOOL)canRestoreTab {
  Profile* lastProfile = [self lastProfileIfLoaded];
  if (!lastProfile)
    return _tabRestoreWasEnabled;
  sessions::TabRestoreService* service =
      TabRestoreServiceFactory::GetForProfile(lastProfile);
  return service && !service->entries().empty();
}

// Called from the AppControllerProfileObserver every time a profile is deleted.
- (void)profileWasRemoved:(const base::FilePath&)profilePath
             forIncognito:(bool)isOffTheRecord {
  // If the lastProfile has been deleted, the profile manager has
  // already loaded a new one, so the pointer needs to be updated;
  // otherwise we will try to start up a browser window with a pointer
  // to the old profile.
  //
  // In a browser test, the application is not brought to the front, so
  // |_lastProfile| might be null.
  if (!_lastProfile || (profilePath == _lastProfile->GetPath() &&
                        isOffTheRecord == _lastProfile->IsOffTheRecord())) {
    Profile* last_used_profile = nullptr;
    auto* profile_manager = g_browser_process->profile_manager();
    if (profile_manager) {
      // |profile_manager| is null in browser tests during shutdown.
      last_used_profile = profile_manager->GetLastUsedProfileIfLoaded();
    }
    [self setLastProfile:last_used_profile];
  }

  _profileBookmarkMenuBridgeMap.erase(profilePath);
}

// Returns true if there is a modal window (either window- or application-
// modal) blocking the active browser. Note that tab modal dialogs (HTTP auth
// sheets) will not count as blocking the browser. But things like open/save
// dialogs that are window modal will block the browser.
- (BOOL)keyWindowIsModal {
  if ([NSApp modalWindow])
    return YES;

  Browser* browser = chrome::GetLastActiveBrowser();
  return browser && [[browser->window()->GetNativeWindow().GetNativeNSWindow()
                            attachedSheet] isKindOfClass:[NSWindow class]];
}

- (BOOL)canOpenNewBrowser {
  Profile* unsafeLastProfile = [self lastProfileIfLoaded];
  // If the profile is not loaded, try to load it. If it's not usable, the
  // profile picker will be open instead.
  if (!unsafeLastProfile)
    return YES;
  return [self safeProfileForNewWindows:unsafeLastProfile] ? YES : NO;
}

// Validates menu items in the dock (always) and in the menu bar (if there is no
// browser).
- (BOOL)validateUserInterfaceItem:(id<NSValidatedUserInterfaceItem>)item {
  SEL action = [item action];
  BOOL enable = NO;
  // Whether the profile is loaded and opening a new browser window is allowed.
  BOOL canOpenNewBrowser = [self canOpenNewBrowser];
  BOOL hasLoadedProfile = [self lastProfileIfLoaded] ? YES : NO;
  // Commands from dock are always handled by commandFromDock:, but commands
  // from the menu bar are only handled by commandDispatch: if there is no key
  // window.
  if (action == @selector(commandDispatch:) ||
      action == @selector(commandFromDock:)) {
    NSInteger tag = [item tag];
    if (_menuState &&  // NULL in tests.
        _menuState->SupportsCommand(tag)) {
      switch (tag) {
        // The File Menu commands are not automatically disabled by Cocoa when a
        // dialog sheet obscures the browser window, so we disable several of
        // them here.  We don't need to include IDC_CLOSE_WINDOW, because
        // app_controller is only activated when there are no key windows (see
        // function comment).
        case IDC_RESTORE_TAB:
          enable = ![self keyWindowIsModal] && [self canRestoreTab];
          break;
        // Profile-level items that affect how the profile's UI looks should
        // only be available while there is a Profile opened.
        case IDC_SHOW_FULL_URLS:
          enable = hasLoadedProfile;
          break;
        // Browser-level items that open in new tabs or perform an action in a
        // current tab should not open if there's a window- or app-modal dialog.
        case IDC_OPEN_FILE:
        case IDC_NEW_TAB:
        case IDC_FOCUS_LOCATION:
        case IDC_FOCUS_SEARCH:
        case IDC_SHOW_HISTORY:
        case IDC_SHOW_BOOKMARK_MANAGER:
        case IDC_CLEAR_BROWSING_DATA:
        case IDC_SHOW_DOWNLOADS:
        case IDC_IMPORT_SETTINGS:
        case IDC_MANAGE_EXTENSIONS:
        case IDC_HELP_PAGE_VIA_MENU:
        case IDC_OPTIONS:
          enable = canOpenNewBrowser && ![self keyWindowIsModal];
          break;
        // Browser-level items that open in new windows: allow the user to open
        // a new window even if there's a window-modal dialog.
        case IDC_NEW_WINDOW:
          enable = canOpenNewBrowser;
          break;
        case IDC_TASK_MANAGER:
          enable = YES;
          break;
        case IDC_NEW_INCOGNITO_WINDOW:
          enable = _menuState->IsCommandEnabled(tag) ? canOpenNewBrowser : NO;
          break;
        default:
          enable = _menuState->IsCommandEnabled(tag) ?
                   ![self keyWindowIsModal] : NO;
          break;
      }
    }

    // "Show as tab" should only appear when the current window is a popup.
    // Since |validateUserInterfaceItem:| is called only when there are no
    // key windows, we should just hide this.
    // This is handled outside of the switch statement because we want to hide
    // this regardless if the command is supported or not.
    if (tag == IDC_SHOW_AS_TAB) {
      NSMenuItem* menuItem = base::mac::ObjCCast<NSMenuItem>(item);
      [menuItem setHidden:YES];
    }
  } else if (action == @selector(terminate:)) {
    enable = YES;
  } else if (action == @selector(showPreferences:)) {
    enable = canOpenNewBrowser;
  } else if (action == @selector(orderFrontStandardAboutPanel:)) {
    enable = canOpenNewBrowser;
  } else if (action == @selector(toggleConfirmToQuit:)) {
    [self updateConfirmToQuitPrefMenuItem:static_cast<NSMenuItem*>(item)];
    enable = [self shouldEnableConfirmToQuitPrefMenuItem];
  }
  return enable;
}

- (void)commandDispatch:(id)sender {
  // Drop commands received after shutdown was initiated.
  if (g_browser_process->IsShuttingDown())
    return;

  // Handle the case where we're dispatching a command from a sender that's in a
  // browser window. This means that the command came from a background window
  // and is getting here because the foreground window is not a browser window.
  if ([sender respondsToSelector:@selector(window)]) {
    id delegate = [[sender window] windowController];
    if ([delegate respondsToSelector:@selector(commandDispatch:)]) {
      [delegate commandDispatch:sender];
      return;
    }
  }

  // If not between -applicationDidFinishLaunching: and
  // -applicationWillTerminate:, ignore. This can happen when events are sitting
  // in the event queue while the browser is shutting down.
  if (!_keep_alive)
    return;

  Profile* unsafeLastProfile = [self lastProfileIfLoaded];
  Profile* lastProfile = [self safeProfileForNewWindows:unsafeLastProfile];
  // Ignore commands during session restore's browser creation.  It uses a
  // nested run loop and commands dispatched during this operation cause
  // havoc.
  if (lastProfile && SessionRestore::IsRestoring(lastProfile) &&
      base::RunLoop::IsNestedOnCurrentThread()) {
    return;
  }

  NSInteger tag = [sender tag];
  // The task manager can be shown without profile.
  if (tag == IDC_TASK_MANAGER) {
    chrome::OpenTaskManager(nullptr);
    return;
  }

  if (unsafeLastProfile && !lastProfile) {
    // The profile is disallowed by policy (locked or guest mode disabled).
    ProfilePicker::Show(ProfilePicker::Params::FromEntryPoint(
        ProfilePicker::EntryPoint::kProfileLocked));
    return;
  }

  // Asynchronously load profile first if needed.
  app_controller_mac::RunInLastProfileSafely(
      base::BindOnce(base::RetainBlock(^(Profile* profile) {
        [self executeCommand:sender withProfile:profile];
      })),
      app_controller_mac::kShowProfilePickerOnFailure);
}

- (void)executeCommand:(id)sender withProfile:(Profile*)profile {
  if (!profile) {
    // Couldn't load the Profile. RunInSafeProfileHelper will show the
    // ProfilePicker instead.
    return;
  }

  NSInteger tag = [sender tag];

  switch (tag) {
    case IDC_NEW_TAB:
      // Create a new tab in an existing browser window (which we activate) if
      // possible.
      if (Browser* browser = ActivateBrowser(profile)) {
        chrome::ExecuteCommand(browser, IDC_NEW_TAB);
        break;
      }
      [[fallthrough]];  // To create new window.
    case IDC_NEW_WINDOW:
      CreateBrowser(profile);
      break;
    case IDC_FOCUS_LOCATION:
      chrome::ExecuteCommand(ActivateOrCreateBrowser(profile),
                             IDC_FOCUS_LOCATION);
      break;
    case IDC_FOCUS_SEARCH:
      chrome::ExecuteCommand(ActivateOrCreateBrowser(profile),
                             IDC_FOCUS_SEARCH);
      break;
    case IDC_NEW_INCOGNITO_WINDOW:
      CreateBrowser(profile->GetPrimaryOTRProfile(/*create_if_needed=*/true));
      break;
    case IDC_RESTORE_TAB:
      app_controller_mac::TabRestorer::RestoreMostRecent(profile);
      break;
    case IDC_OPEN_FILE:
      chrome::ExecuteCommand(CreateBrowser(profile), IDC_OPEN_FILE);
      break;
    case IDC_CLEAR_BROWSING_DATA: {
      // There may not be a browser open, so use the default profile.
      if (Browser* browser = ActivateBrowser(profile)) {
        chrome::ShowClearBrowsingDataDialog(browser);
      } else {
        chrome::OpenClearBrowsingDataDialogWindow(profile);
      }
      break;
    }
    case IDC_IMPORT_SETTINGS: {
      if (Browser* browser = ActivateBrowser(profile)) {
        chrome::ShowImportDialog(browser);
      } else {
        chrome::OpenImportSettingsDialogWindow(profile);
      }
      break;
    }
    case IDC_SHOW_BOOKMARK_MANAGER:
      if (Browser* browser = ActivateBrowser(profile)) {
        chrome::ShowBookmarkManager(browser);
      } else {
        // No browser window, so create one for the bookmark manager tab.
        chrome::OpenBookmarkManagerWindow(profile);
      }
      break;
    case IDC_SHOW_HISTORY:
      if (Browser* browser = ActivateBrowser(profile))
        chrome::ShowHistory(browser);
      else
        chrome::OpenHistoryWindow(profile);
      break;
    case IDC_SHOW_DOWNLOADS:
      if (Browser* browser = ActivateBrowser(profile))
        chrome::ShowDownloads(browser);
      else
        chrome::OpenDownloadsWindow(profile);
      break;
    case IDC_MANAGE_EXTENSIONS:
      if (Browser* browser = ActivateBrowser(profile))
        chrome::ShowExtensions(browser, std::string());
      else
        chrome::OpenExtensionsWindow(profile);
      break;
    case IDC_HELP_PAGE_VIA_MENU:
      if (Browser* browser = ActivateBrowser(profile))
        chrome::ShowHelp(browser, chrome::HELP_SOURCE_MENU);
      else
        chrome::OpenHelpWindow(profile, chrome::HELP_SOURCE_MENU);
      break;
    case IDC_OPTIONS:
      [self showPreferences:sender];
      break;
  }
}

// Same as |-commandDispatch:|, but executes commands using a disposition
// determined by the key flags. This will get called in the case where the
// frontmost window is not a browser window, and the user has command-clicked
// a button in a background browser window whose action is
// |-commandDispatchUsingKeyModifiers:|
- (void)commandDispatchUsingKeyModifiers:(id)sender {
  DCHECK(sender);
  if ([sender respondsToSelector:@selector(window)]) {
    id delegate = [[sender window] windowController];
    if ([delegate respondsToSelector:
            @selector(commandDispatchUsingKeyModifiers:)]) {
      [delegate commandDispatchUsingKeyModifiers:sender];
    }
  }
}

// NSApplication delegate method called when someone clicks on the dock icon.
// To match standard mac behavior, we should open a new window if there are no
// browser windows.
- (BOOL)applicationShouldHandleReopen:(NSApplication*)theApplication
                    hasVisibleWindows:(BOOL)hasVisibleWindows {
  // If the browser is currently trying to quit, don't do anything and return NO
  // to prevent AppKit from doing anything.
  if (browser_shutdown::IsTryingToQuit())
    return NO;

  // Bring all browser windows to the front. Specifically, this brings them in
  // front of any app windows. FocusWindowSet will also unminimize the most
  // recently minimized window if no windows in the set are visible.
  // If there are any, return here. Otherwise, the windows are panels or
  // notifications so we still need to open a new window.
  if (hasVisibleWindows) {
    std::set<gfx::NativeWindow> browserWindows = GetBrowserNativeWindows();
    if (!browserWindows.empty()) {
      NSWindow* keyWindow = [NSApp keyWindow];
      if (keyWindow && ![keyWindow isOnActiveSpace]) {
        // The key window is not on the active space. We must be mid-animation
        // for a space transition triggered by the dock. Delay the call to
        // |ui::FocusWindowSet| until the transition completes. Otherwise, the
        // wrong space's windows get raised, resulting in an off-screen key
        // window. It does not work to |ui::FocusWindowSet| twice, once here
        // and once in |activeSpaceDidChange:|, as that appears to break when
        // the omnibox is focused.
        //
        // This check relies on OS X setting the key window to a window on the
        // target space before calling this method.
        //
        // See http://crbug.com/309656.
        _reopenTime = base::TimeTicks::Now();
      } else {
        FocusWindowSetOnCurrentSpace(browserWindows);
      }
      // Return NO; we've done (or soon will do) the deminiaturize, so
      // AppKit shouldn't do anything.
      return NO;
    }
  }

  // If launched as a hidden login item (due to installation of a persistent app
  // or by the user, for example in System Preferences->Accounts->Login Items),
  // allow session to be restored first time the user clicks on a Dock icon.
  // Normally, it'd just open a new empty page.
  {
    static BOOL doneOnce = NO;
    BOOL attemptRestore =
        apps::AppShimTerminationManager::Get()->ShouldRestoreSession() ||
        (!doneOnce && base::mac::WasLaunchedAsHiddenLoginItem());
    doneOnce = YES;
    if (attemptRestore) {
      Profile* lastProfile = [self lastProfile];
      if (!lastProfile || IsProfileSignedOut(lastProfile)) {
        // There is no session to be restored without a valid profile or a
        // profile that is locked and requires signin. Return NO to do nothing.
        return NO;
      }
      SessionService* sessionService =
          SessionServiceFactory::GetForProfileForSessionRestore(lastProfile);
      if (sessionService &&
          sessionService->RestoreIfNecessary(StartupTabs(),
                                             /* restore_apps */ false))
        return NO;
    }
  }

  // Otherwise open a new window.
  // If the last profile was locked, we have to open the User Manager, as the
  // profile requires authentication. Similarly, because guest mode and system
  // profile are implemented as forced incognito, we can't open a new guest
  // browser either, so we have to show the User Manager as well.
  Profile* lastProfile = [self lastProfile];
  if (!lastProfile) {
    // Without a profile there's nothing that can be done, but still return NO
    // to AppKit as there's nothing that it can do either.
    return NO;
  }
  if (lastProfile->IsGuestSession() || IsProfileSignedOut(lastProfile) ||
      lastProfile->IsSystemProfile()) {
    ProfilePicker::Show(ProfilePicker::Params::FromEntryPoint(
        ProfilePicker::EntryPoint::kProfileLocked));
  } else if (ProfilePicker::ShouldShowAtLaunch()) {
    ProfilePicker::Show(ProfilePicker::Params::FromEntryPoint(
        ProfilePicker::EntryPoint::kNewSessionOnExistingProcess));
  } else {
    CreateBrowser(lastProfile);
  }

  // We've handled the reopen event, so return NO to tell AppKit not
  // to do anything.
  return NO;
}

- (void)initMenuState {
  _menuState = std::make_unique<CommandUpdaterImpl>(nullptr);
  _menuState->UpdateCommandEnabled(IDC_NEW_TAB, true);
  _menuState->UpdateCommandEnabled(IDC_NEW_WINDOW, true);
  _menuState->UpdateCommandEnabled(IDC_NEW_INCOGNITO_WINDOW, true);
  _menuState->UpdateCommandEnabled(IDC_OPEN_FILE, true);
  _menuState->UpdateCommandEnabled(IDC_CLEAR_BROWSING_DATA, true);
  _menuState->UpdateCommandEnabled(IDC_RESTORE_TAB, false);
  _menuState->UpdateCommandEnabled(IDC_FOCUS_LOCATION, true);
  _menuState->UpdateCommandEnabled(IDC_FOCUS_SEARCH, true);
  _menuState->UpdateCommandEnabled(IDC_SHOW_BOOKMARK_MANAGER, true);
  _menuState->UpdateCommandEnabled(IDC_SHOW_HISTORY, true);
  _menuState->UpdateCommandEnabled(IDC_SHOW_DOWNLOADS, true);
  _menuState->UpdateCommandEnabled(IDC_MANAGE_EXTENSIONS, true);
  _menuState->UpdateCommandEnabled(IDC_HELP_PAGE_VIA_MENU, true);
  _menuState->UpdateCommandEnabled(IDC_IMPORT_SETTINGS, true);
#if BUILDFLAG(GOOGLE_CHROME_BRANDING)
  _menuState->UpdateCommandEnabled(IDC_FEEDBACK, true);
#endif
  _menuState->UpdateCommandEnabled(IDC_TASK_MANAGER, true);
}

// Conditionally adds the Profile menu to the main menu bar.
- (void)initProfileMenu {
  NSMenu* mainMenu = [NSApp mainMenu];
  NSMenuItem* profileMenu = [mainMenu itemWithTag:IDC_PROFILE_MAIN_MENU];

  if (!profiles::IsMultipleProfilesEnabled()) {
    [mainMenu removeItem:profileMenu];
    return;
  }

  // The controller will unhide the menu if necessary.
  [profileMenu setHidden:YES];

  _profileMenuController.reset(
      [[ProfileMenuController alloc] initWithMainMenuItem:profileMenu]);
}

- (void)initShareMenu {
  _shareMenuController.reset([[ShareMenuController alloc] init]);
  NSMenu* mainMenu = [NSApp mainMenu];
  NSMenu* fileMenu = [[mainMenu itemWithTag:IDC_FILE_MENU] submenu];
  NSString* shareMenuTitle = l10n_util::GetNSString(IDS_SHARE_MAC);
  NSMenuItem* shareMenuItem = [fileMenu itemWithTitle:shareMenuTitle];
  base::scoped_nsobject<NSMenu> shareSubmenu(
      [[NSMenu alloc] initWithTitle:shareMenuTitle]);
  [shareSubmenu setDelegate:_shareMenuController];
  [shareMenuItem setSubmenu:shareSubmenu];
}

// The Confirm to Quit preference is atypical in that the preference lives in
// the app menu right above the Quit menu item. This method will refresh the
// display of that item depending on the preference state.
- (void)updateConfirmToQuitPrefMenuItem:(NSMenuItem*)item {
  // Format the string so that the correct key equivalent is displayed.
  NSString* acceleratorString = [ConfirmQuitPanelController keyCommandString];
  NSString* title = l10n_util::GetNSStringF(IDS_CONFIRM_TO_QUIT_OPTION,
      base::SysNSStringToUTF16(acceleratorString));
  [item setTitle:title];

  const PrefService* prefService = g_browser_process->local_state();
  bool enabled = prefService->GetBoolean(prefs::kConfirmToQuitEnabled);
  [item setState:enabled ? NSOnState : NSOffState];
}

- (BOOL)shouldEnableConfirmToQuitPrefMenuItem {
  const PrefService* prefService = g_browser_process->local_state();
  return !prefService->FindPreference(prefs::kConfirmToQuitEnabled)
              ->IsManaged();
}

- (void)registerServicesMenuTypesTo:(NSApplication*)app {
  // Note that RenderWidgetHostViewCocoa implements NSServicesRequests which
  // handles requests from services.
  NSArray* types = @[ base::mac::CFToNSCast(kUTTypeUTF8PlainText) ];
  [app registerServicesMenuSendTypes:types returnTypes:types];
}

// Returns null if the profile is not loaded in memory.
- (Profile*)lastProfileIfLoaded {
  // Return the profile of the last-used Browser, if available.
  if (_lastProfile)
    return _lastProfile;

  if (![self isProfileReady])
    return nullptr;

  ProfileManager* profile_manager = g_browser_process->profile_manager();
  if (!profile_manager)
    return nullptr;

  // GetProfileByPath() returns nullptr if the profile is not loaded.
  return profile_manager->GetProfileByPath(GetStartupProfilePathMac());
}

// Returns null if Chrome is not ready or there is no ProfileManager.
// DEPRECATED: use lastProfileIfLoaded instead.
// TODO(https://crbug.com/1176734): May be blocking, migrate all callers to
// |-lastProfileIfLoaded|.
- (Profile*)lastProfile {
  Profile* lastLoadedProfile = [self lastProfileIfLoaded];
  if (lastLoadedProfile)
    return lastLoadedProfile;

  if (![self isProfileReady])
    return nullptr;

  return GetLastProfileMac();
}

- (Profile*)safeProfileForNewWindows:(Profile*)profile {
  if (!profile)
    return nullptr;

  DCHECK(!profile->IsSystemProfile());
  if (profile->IsGuestSession() && !profiles::IsGuestModeEnabled())
    return nullptr;

  if (IsProfileSignedOut(profile))
    return nullptr;  // Profile is locked.

  return ProfileManager::MaybeForceOffTheRecordMode(profile);
}

- (void)application:(NSApplication*)sender openURLs:(NSArray<NSURL*>*)urls {
  std::vector<GURL> gurlVector;
  for (NSURL* url in urls)
    gurlVector.push_back(net::GURLWithNSURL(url));

  if (!gurlVector.empty())
    [self openUrlsReplacingNTP:gurlVector];
}

// Show the preferences window, or bring it to the front if it's already
// visible.
- (IBAction)showPreferences:(id)sender {
  // Asynchronously load profile first if needed.
  app_controller_mac::RunInLastProfileSafely(
      base::BindOnce(base::RetainBlock(^(Profile* profile) {
        [self showPreferencesForProfile:profile];
      })),
      app_controller_mac::kShowProfilePickerOnFailure);
}

- (IBAction)showPreferencesForProfile:(Profile*)profile {
  if (!profile) {
    // Failed to load profile, show Profile Picker instead.
    return;
  }
  // Re-use an existing browser, or create a new one.
  if (Browser* browser = ActivateBrowser(profile))
    chrome::ShowSettings(browser);
  else
    chrome::OpenOptionsWindow(profile);
}

- (IBAction)orderFrontStandardAboutPanel:(id)sender {
  // Asynchronously load profile first if needed.
  app_controller_mac::RunInLastProfileSafely(
      base::BindOnce(base::RetainBlock(^(Profile* profile) {
        [self orderFrontStandardAboutPanelForProfile:profile];
      })),
      app_controller_mac::kShowProfilePickerOnFailure);
}

- (IBAction)orderFrontStandardAboutPanelForProfile:(Profile*)profile {
  if (!profile) {
    // Failed to load profile, show Profile Picker instead.
    return;
  }
  // Re-use an existing browser, or create a new one.
  if (Browser* browser = ActivateBrowser(profile))
    chrome::ShowAboutChrome(browser);
  else
    chrome::OpenAboutWindow(profile);
}

- (IBAction)toggleConfirmToQuit:(id)sender {
  PrefService* prefService = g_browser_process->local_state();
  bool enabled = prefService->GetBoolean(prefs::kConfirmToQuitEnabled);
  prefService->SetBoolean(prefs::kConfirmToQuitEnabled, !enabled);
}

// Explicitly bring to the foreground when creating new windows from the dock.
- (void)commandFromDock:(id)sender {
  [NSApp activateIgnoringOtherApps:YES];
  [self commandDispatch:sender];
}

- (NSMenu*)applicationDockMenu:(NSApplication*)sender {
  NSMenu* dockMenu = [[[NSMenu alloc] initWithTitle: @""] autorelease];

  BOOL profilesAdded = [_profileMenuController insertItemsIntoMenu:dockMenu
                                                          atOffset:0
                                                          fromDock:YES];
  if (profilesAdded)
    [dockMenu addItem:[NSMenuItem separatorItem]];

  NSString* titleStr = l10n_util::GetNSStringWithFixup(IDS_NEW_WINDOW_MAC);
  base::scoped_nsobject<NSMenuItem> item(
      [[NSMenuItem alloc] initWithTitle:titleStr
                                 action:@selector(commandFromDock:)
                          keyEquivalent:@""]);
  [item setTarget:self];
  [item setTag:IDC_NEW_WINDOW];
  [item setEnabled:[self validateUserInterfaceItem:item]];
  [dockMenu addItem:item];

  Profile* profile = [self lastProfileIfLoaded];

  // Buttons below require the profile to be loaded. In particular, if the
  // profile picker is shown at startup, these buttons won't be added until the
  // user picks a profile.
  if (!profile)
    return dockMenu;

  if (IncognitoModePrefs::GetAvailability(profile->GetPrefs()) !=
      IncognitoModePrefs::Availability::kDisabled) {
    titleStr = l10n_util::GetNSStringWithFixup(IDS_NEW_INCOGNITO_WINDOW_MAC);
    item.reset(
        [[NSMenuItem alloc] initWithTitle:titleStr
                                   action:@selector(commandFromDock:)
                            keyEquivalent:@""]);
    [item setTarget:self];
    [item setTag:IDC_NEW_INCOGNITO_WINDOW];
    [item setEnabled:[self validateUserInterfaceItem:item]];
    [dockMenu addItem:item];
  }

  return dockMenu;
}

- (const std::vector<GURL>&)startupUrls {
  return _startupUrls;
}

- (BookmarkMenuBridge*)bookmarkMenuBridge {
  return _bookmarkMenuBridge;
}

- (HistoryMenuBridge*)historyMenuBridge {
  return _historyMenuBridge.get();
}

- (TabMenuBridge*)tabMenuBridge {
  return _tabMenuBridge.get();
}

- (void)initAppShimMenuController {
  if (!_appShimMenuController)
    _appShimMenuController.reset([[AppShimMenuController alloc] init]);
}

- (void)setLastProfile:(Profile*)profile {
  if (profile == _lastProfile)
    return;

  // If _lastProfile becomes null, remember the last state of Cmd+Shift+T so the
  // command can continue working (or stay disabled). This is primarily meant
  // for the zero-profile state.
  _tabRestoreWasEnabled = profile == nullptr && [self canRestoreTab];

  // Before tearing down the menu controller bridges, return the history menu to
  // its initial state.
  if (_historyMenuBridge)
    _historyMenuBridge->ResetMenu();
  _historyMenuBridge.reset();

  _profilePrefRegistrar.reset();

  // Rebuild the menus with the new profile. The bookmarks submenu is cached to
  // avoid slowdowns when switching between profiles with large numbers of
  // bookmarks. Before caching, store whether it is hidden, make the menu item
  // visible, and restore its original hidden state after resetting the submenu.
  // This works around an apparent AppKit bug where setting a *different* NSMenu
  // submenu on a *hidden* menu item forces the item to become visible.
  // See https://crbug.com/497813 for more details.
  NSMenuItem* bookmarkItem = [[NSApp mainMenu] itemWithTag:IDC_BOOKMARKS_MENU];
  BOOL hidden = [bookmarkItem isHidden];
  [bookmarkItem setHidden:NO];
  _bookmarkMenuBridge = nullptr;

  _lastProfile = profile;

  if (_lastProfile == nullptr)
    return;

  auto& entry = _profileBookmarkMenuBridgeMap[profile->GetPath()];
  if (!entry) {
    // This creates a deep copy, but only the first 3 items in the root menu
    // are really wanted. This can probably be optimized, but lazy-loading of
    // the menu should reduce the impact in most flows.
    base::scoped_nsobject<NSMenu> submenu([[bookmarkItem submenu] copy]);
    [submenu setDelegate:nil];  // The delegate is also copied. Remove it.

    entry = std::make_unique<BookmarkMenuBridge>(profile, submenu);

    // Clear bookmarks from the old profile.
    entry->ClearBookmarkMenu();
  }
  _bookmarkMenuBridge = entry.get();

  // No need to |BuildMenu| here.  It is done lazily upon menu access.
  [bookmarkItem setSubmenu:_bookmarkMenuBridge->BookmarkMenu()];
  [bookmarkItem setHidden:hidden];

  _historyMenuBridge = std::make_unique<HistoryMenuBridge>(_lastProfile);
  _historyMenuBridge->BuildMenu();

  chrome::BrowserCommandController::
      UpdateSharedCommandsForIncognitoAvailability(
          _menuState.get(), _lastProfile);
  _profilePrefRegistrar = std::make_unique<PrefChangeRegistrar>();
  _profilePrefRegistrar->Init(_lastProfile->GetPrefs());
  _profilePrefRegistrar->Add(
      prefs::kIncognitoModeAvailability,
      base::BindRepeating(&chrome::BrowserCommandController::
                              UpdateSharedCommandsForIncognitoAvailability,
                          _menuState.get(), _lastProfile));
}

- (const ui::ColorProvider&)lastActiveColorProvider {
  DCHECK(_lastActiveColorProvider);
  return *_lastActiveColorProvider;
}

- (void)nativeThemeDidChange {
  // Some tests manually notify native theme change without setting
  // a profile for app controller, so `_lastProfile` will be nullptr.
  if (_lastProfile) {
    Browser* browser = chrome::FindBrowserWithProfile(_lastProfile);
    if (browser && browser->window())
      _lastActiveColorProvider = browser->window()->GetColorProvider();
  }
}

- (BOOL)windowHasBrowserTabs:(NSWindow*)window {
  if (!window) {
    return NO;
  }
  Browser* browser = chrome::FindBrowserWithWindow(window);
  return browser && browser->is_type_normal();
}

- (void)updateMenuItemKeyEquivalents {
  BOOL enableCloseTabShortcut = NO;

  id target = [NSApp targetForAction:@selector(performClose:)];

  // If `target` is a popover (likely the dictionary lookup popover) the
  // main window should handle the close menu item action.
  NSWindow* targetWindow = nil;
  if ([target isKindOfClass:[NSPopover class]]) {
    targetWindow =
        [[[base::mac::ObjCCast<NSPopover>(target) contentViewController] view]
            window];
  } else {
    targetWindow = base::mac::ObjCCast<NSWindow>(target);
  }

  if (targetWindow != nil) {
    // If `targetWindow` is a child (a popover or bubble) the parent should
    // handle the command.
    if ([targetWindow parentWindow] != nil) {
      targetWindow = [targetWindow parentWindow];
    }

    enableCloseTabShortcut = [self windowHasBrowserTabs:targetWindow];
  }

  [self adjustCloseWindowMenuItemKeyEquivalent:enableCloseTabShortcut];
  [self adjustCloseTabMenuItemKeyEquivalent:enableCloseTabShortcut];
}

- (BOOL)application:(NSApplication*)application
    willContinueUserActivityWithType:(NSString*)userActivityType {
  return [userActivityType isEqualToString:NSUserActivityTypeBrowsingWeb];
}

- (BOOL)application:(NSApplication*)application
    continueUserActivity:(NSUserActivity*)userActivity
      restorationHandler:
          (void (^)(NSArray<id<NSUserActivityRestoring>>*))restorationHandler
{
  if (![userActivity.activityType
          isEqualToString:NSUserActivityTypeBrowsingWeb]) {
    return NO;
  }

  NSString* originString = base::mac::ObjCCast<NSString>(
      (userActivity.userInfo)[handoff::kOriginKey]);
  handoff::Origin origin = handoff::OriginFromString(originString);
  UMA_HISTOGRAM_ENUMERATION(
      "OSX.Handoff.Origin", origin, handoff::ORIGIN_COUNT);

  NSURL* url = userActivity.webpageURL;
  if (!url)
    return NO;

  GURL gurl(base::SysNSStringToUTF8([url absoluteString]));
  std::vector<GURL> gurlVector;
  gurlVector.push_back(gurl);

  [self openUrlsReplacingNTP:gurlVector];
  return YES;
}

- (void)application:(NSApplication*)application
    didFailToContinueUserActivityWithType:(NSString*)userActivityType
                                    error:(NSError*)error {
}

#pragma mark - Handoff Manager

- (void)updateHandoffManagerWithURL:(const GURL&)handoffURL
                              title:(const std::u16string&)handoffTitle {
  [_handoffManager updateActiveURL:handoffURL];
  [_handoffManager updateActiveTitle:handoffTitle];
}

- (void)updateHandoffManager:(content::WebContents*)webContents {
  if (!_handoffManager)
    _handoffManager.reset([[HandoffManager alloc] init]);

  if ([self isHandoffEligible:webContents]) {
    [self updateHandoffManagerWithURL:webContents->GetVisibleURL()
                                title:webContents->GetTitle()];
  } else {
    [self updateHandoffManagerWithURL:GURL() title:std::u16string()];
  }
}

- (BOOL)isHandoffEligible:(content::WebContents*)webContents {
  if (!webContents)
    return NO;

  Profile* profile =
      Profile::FromBrowserContext(webContents->GetBrowserContext());
  if (!profile)
    return NO;

  // Handoff is not allowed from an incognito profile. To err on the safe side,
  // also disallow Handoff from a guest profile.
  return profile->IsRegularProfile();
}

- (BOOL)isProfileReady {
  return !g_browser_process->browser_policy_connector()
              ->chrome_browser_cloud_management_controller()
              ->IsEnterpriseStartupDialogShowing();
}

#pragma mark - HandoffObserverDelegate

- (void)handoffContentsChanged:(content::WebContents*)webContents {
  [self updateHandoffManager:webContents];
}

#pragma mark - ASWebAuthenticationSessionWebBrowserSessionHandling

// Note that both of these WebAuthenticationSession calls come in on a random
// worker thread, so it's important to hop to the main thread.

- (void)beginHandlingWebAuthenticationSessionRequest:
    (ASWebAuthenticationSessionRequest*)request API_AVAILABLE(macos(10.15)) {
  dispatch_async(dispatch_get_main_queue(), ^(void) {
    // Start tracking the pending request, so it's possible to cancel it before
    // the session actually starts.
    NSUUID* key = [request UUID];
    DCHECK(![GetPendingWebAuthRequests() objectForKey:key])
        << "Duplicate ASWebAuthenticationSessionRequest";
    [GetPendingWebAuthRequests() setObject:request forKey:key];
    app_controller_mac::RunInLastProfileSafely(
        base::BindOnce(&BeginHandlingWebAuthenticationSessionRequestWithProfile,
                       base::scoped_nsobject<ASWebAuthenticationSessionRequest>(
                           request, base::scoped_policy::RETAIN)),
        app_controller_mac::kShowProfilePickerOnFailure);
  });
}

- (void)cancelWebAuthenticationSessionRequest:
    (ASWebAuthenticationSessionRequest*)request API_AVAILABLE(macos(10.15)) {
  dispatch_async(dispatch_get_main_queue(), ^(void) {
    // Remove the pending request: for the case when the session is not started.
    [GetPendingWebAuthRequests() removeObjectForKey:[request UUID]];
    // Cancel the session: for the case when it was already started.
    AuthSessionRequest::CancelAuthSession(request);
  });
}

- (void)setCloseWindowMenuItemForTesting:(NSMenuItem*)menuItem {
  _closeWindowMenuItem = menuItem;
}

- (void)setCloseTabMenuItemForTesting:(NSMenuItem*)menuItem {
  _closeTabMenuItem = menuItem;
}

- (void)setLastProfileForTesting:(Profile*)profile {
  _lastProfile = profile;
  Browser* browser = chrome::FindLastActiveWithProfile(profile);
  _lastActiveColorProvider = browser->window()->GetColorProvider();
}

@end  // @implementation AppController

//---------------------------------------------------------------------------

namespace {

void UpdateProfileInUse(Profile* profile, Profile::CreateStatus status) {
  if (status == Profile::CREATE_STATUS_INITIALIZED) {
    AppController* controller =
        base::mac::ObjCCastStrict<AppController>([NSApp delegate]);
    [controller setLastProfile:profile];
  }
}

void OpenUrlsInBrowserWithProfile(const std::vector<GURL>& urls,
                                  Profile* profile) {
  if (!profile)
    return;  // No suitable profile to open the URLs, do nothing.
  Browser* browser = chrome::FindLastActiveWithProfile(profile);
  int startupIndex = TabStripModel::kNoTab;
  content::WebContents* startupContent = nullptr;
  if (browser && browser->tab_strip_model()->count() == 1) {
    // If there's only 1 tab and the tab is NTP, close this NTP tab and open all
    // startup urls in new tabs, because the omnibox will stay focused if we
    // load url in NTP tab.
    startupIndex = browser->tab_strip_model()->active_index();
    startupContent = browser->tab_strip_model()->GetActiveWebContents();
  } else if (!browser) {
    // if no browser window exists then create one with no tabs to be filled in.
    browser = Browser::Create(Browser::CreateParams(profile, true));
    browser->window()->Show();
  }

  // Various methods to open URLs that we get in a native fashion. We use
  // StartupBrowserCreator here because on the other platforms, URLs to open
  // come through the ProcessSingleton, and it calls StartupBrowserCreator. It's
  // best to bottleneck the openings through that for uniform handling.
  base::CommandLine dummy(base::CommandLine::NO_PROGRAM);
  chrome::startup::IsFirstRun first_run =
      first_run::IsChromeFirstRun() ? chrome::startup::IsFirstRun::kYes
                                    : chrome::startup::IsFirstRun::kNo;
  StartupBrowserCreatorImpl launch(base::FilePath(), dummy, first_run);
  launch.OpenURLsInBrowser(browser, chrome::startup::IsProcessStartup::kNo,
                           urls);

  // This NTP check should be replaced once https://crbug.com/624410 is fixed.
  if (startupIndex != TabStripModel::kNoTab &&
      (startupContent->GetVisibleURL() == chrome::kChromeUINewTabURL ||
       startupContent->GetVisibleURL() == chrome::kChromeUINewTabPageURL)) {
    browser->tab_strip_model()->CloseWebContentsAt(startupIndex,
                                                   TabStripModel::CLOSE_NONE);
  }
}

// Returns the profile to be used for new windows (or nullptr if it fails).
Profile* GetSafeProfile(Profile* loaded_profile, Profile::CreateStatus status) {
  switch (status) {
    case Profile::CREATE_STATUS_INITIALIZED:
      break;
    case Profile::CREATE_STATUS_CREATED:
      NOTREACHED() << "Should only be called when profile loading is complete";
      [[fallthrough]];
    case Profile::CREATE_STATUS_LOCAL_FAIL:
      return nullptr;
  }
  AppController* controller =
      base::mac::ObjCCastStrict<AppController>([NSApp delegate]);
  if (!controller)
    return nullptr;
  DCHECK(loaded_profile);
  return [controller safeProfileForNewWindows:loaded_profile];
}

// Called when the profile has been loaded for RunIn*ProfileSafely(). This
// profile may not be safe to use for new windows (due to policies).
void OnProfileLoaded(base::OnceCallback<void(Profile*)>& callback,
                     app_controller_mac::ProfileLoadFailureBehavior on_failure,
                     Profile* loaded_profile,
                     Profile::CreateStatus status) {
  if (status == Profile::CREATE_STATUS_CREATED)
    return;  // Profile loading is not complete, wait to be called again.
  Profile* safe_profile = GetSafeProfile(loaded_profile, status);
  if (!safe_profile) {
    switch (on_failure) {
      case app_controller_mac::kShowProfilePickerOnFailure:
        ProfilePicker::Show(ProfilePicker::Params::FromEntryPoint(
            ProfilePicker::EntryPoint::kUnableToCreateBrowser));
        break;

      case app_controller_mac::kIgnoreOnFailure:
        break;
    }
  }
  std::move(callback).Run(safe_profile);
}

}  // namespace

namespace app_controller_mac {

bool IsOpeningNewWindow() {
  return g_is_opening_new_window;
}

void CreateGuestProfileIfNeeded() {
  g_browser_process->profile_manager()->CreateProfileAsync(
      ProfileManager::GetGuestProfilePath(),
      base::BindRepeating(&UpdateProfileInUse));
}

void EnterpriseStartupDialogClosed() {
  AppController* controller =
      base::mac::ObjCCastStrict<AppController>([NSApp delegate]);
  if (controller != nil) {
    NSNotification* notify = [NSNotification
        notificationWithName:NSApplicationDidFinishLaunchingNotification
                      object:NSApp];
    [controller applicationDidFinishLaunching:notify];
  }
}

void RunInLastProfileSafely(base::OnceCallback<void(Profile*)> callback,
                            ProfileLoadFailureBehavior on_failure) {
  DCHECK(callback);
  AppController* controller =
      base::mac::ObjCCastStrict<AppController>([NSApp delegate]);
  if (!controller) {
    OnProfileLoaded(callback, on_failure, nullptr,
                    Profile::CREATE_STATUS_LOCAL_FAIL);
    return;
  }
  if (Profile* profile = [controller lastProfileIfLoaded]) {
    OnProfileLoaded(callback, on_failure, profile,
                    Profile::CREATE_STATUS_INITIALIZED);
    return;
  }
  // Pass the OnceCallback by reference because CreateProfileAsync() needs a
  // repeating callback. It will be called at most once.
  g_browser_process->profile_manager()->CreateProfileAsync(
      GetStartupProfilePathMac(),
      base::BindRepeating(&OnProfileLoaded, base::OwnedRef(std::move(callback)),
                          on_failure));
}

void RunInProfileSafely(const base::FilePath& profile_dir,
                        base::OnceCallback<void(Profile*)> callback,
                        ProfileLoadFailureBehavior on_failure) {
  DCHECK(callback);
  ProfileManager* profile_manager = g_browser_process->profile_manager();
  // `profile_manager` can be null in tests.
  if (!profile_manager) {
    OnProfileLoaded(callback, on_failure, nullptr,
                    Profile::CREATE_STATUS_LOCAL_FAIL);
    return;
  }
  if (Profile* profile = profile_manager->GetProfileByPath(profile_dir)) {
    OnProfileLoaded(callback, on_failure, profile,
                    Profile::CREATE_STATUS_INITIALIZED);
    return;
  }
  // Pass the OnceCallback by reference because CreateProfileAsync() needs a
  // repeating callback. It will be called at most once.
  g_browser_process->profile_manager()->CreateProfileAsync(
      profile_dir,
      base::BindRepeating(&OnProfileLoaded, base::OwnedRef(std::move(callback)),
                          on_failure));
}

// static
void TabRestorer::RestoreMostRecent(Profile* profile) {
  RestoreByID(profile, SessionID::InvalidValue());
}

// static
void TabRestorer::RestoreByID(Profile* profile, SessionID session_id) {
  DCHECK(profile);
  auto* service = TabRestoreServiceFactory::GetForProfile(profile);
  if (!service)
    return;
  if (service->IsLoaded()) {
    DoRestoreTab(profile, session_id);
  } else {
    // TabRestoreService isn't loaded. Tell it to load entries, and call
    // OpenWindowWithRestoredTabs() when it's done.
    std::ignore = new TabRestorer(profile, session_id);
    service->LoadTabsFromLastSession();
  }
}

// static
void TabRestorer::DoRestoreTab(Profile* profile, SessionID session_id) {
  DCHECK(profile);
  auto* service = TabRestoreServiceFactory::GetForProfile(profile);
  if (!service)
    return;
  Browser* browser = chrome::FindTabbedBrowser(profile, false);
  BrowserLiveTabContext* context =
      browser ? browser->live_tab_context() : nullptr;
  if (session_id.is_valid()) {
    service->RestoreEntryById(context, session_id,
                              WindowOpenDisposition::UNKNOWN);
  } else {
    service->RestoreMostRecentEntry(context);
  }
}

TabRestorer::TabRestorer(Profile* profile, SessionID session_id)
    : profile_(profile), session_id_(session_id) {
  auto* service = TabRestoreServiceFactory::GetForProfile(profile);
  DCHECK(service);
  observation_.Observe(service);
}

TabRestorer::~TabRestorer() = default;

void TabRestorer::TabRestoreServiceDestroyed(
    sessions::TabRestoreService* service) {
  delete this;
}

void TabRestorer::TabRestoreServiceLoaded(
    sessions::TabRestoreService* service) {
  observation_.Reset();
  DoRestoreTab(profile_, session_id_);
  delete this;
}

}  // namespace app_controller_mac
