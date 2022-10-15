// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/extensions/application_launch.h"

#include <memory>
#include <string>
#include <utility>

#include "apps/launcher.h"
#include "base/bind.h"
#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/memory/raw_ptr.h"
#include "base/metrics/histogram_macros.h"
#include "base/time/time.h"
#include "build/build_config.h"
#include "build/chromeos_buildflags.h"
#include "chrome/browser/app_mode/app_mode_utils.h"
#include "chrome/browser/apps/app_service/app_launch_params.h"
#include "chrome/browser/apps/app_service/launch_utils.h"
#include "chrome/browser/apps/platform_apps/platform_app_launch.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/extensions/extension_util.h"
#include "chrome/browser/extensions/launch_util.h"
#include "chrome/browser/extensions/tab_helper.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/browser/ui/browser_navigator.h"
#include "chrome/browser/ui/browser_navigator_params.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/extensions/extension_enable_flow.h"
#include "chrome/browser/ui/extensions/extension_enable_flow_delegate.h"
#include "chrome/browser/ui/tabs/tab_strip_user_gesture_details.h"
#include "chrome/browser/ui/web_applications/web_app_launch_utils.h"
#include "chrome/browser/web_applications/web_app_helpers.h"
#include "chrome/common/extensions/manifest_handlers/app_launch_info.h"
#include "chrome/common/url_constants.h"
#include "content/public/browser/web_contents.h"
#include "extensions/browser/extension_prefs.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/extension_system.h"
#include "extensions/common/constants.h"
#include "extensions/common/extension.h"
#include "extensions/common/features/feature.h"
#include "extensions/common/features/feature_provider.h"
#include "extensions/common/manifest_handlers/options_page_info.h"
#include "third_party/abseil-cpp/absl/types/optional.h"
#include "third_party/blink/public/common/features.h"
#include "ui/base/window_open_disposition.h"
#include "ui/display/scoped_display_for_new_windows.h"
#include "ui/gfx/geometry/rect.h"

#if BUILDFLAG(IS_MAC)
#include "chrome/browser/ui/browser_commands_mac.h"
#endif

using content::WebContents;
using extensions::Extension;
using extensions::ExtensionPrefs;
using extensions::ExtensionRegistry;
using extensions::ExtensionService;

namespace {

// Attempts to launch an app, prompting the user to enable it if necessary.
// This class manages its own lifetime.
class EnableViaDialogFlow : public ExtensionEnableFlowDelegate {
 public:
  EnableViaDialogFlow(ExtensionService* service,
                      ExtensionRegistry* registry,
                      Profile* profile,
                      const std::string& extension_id,
                      base::OnceClosure callback)
      : service_(service),
        registry_(registry),
        profile_(profile),
        extension_id_(extension_id),
        callback_(std::move(callback)) {}

  EnableViaDialogFlow(const EnableViaDialogFlow&) = delete;
  EnableViaDialogFlow& operator=(const EnableViaDialogFlow&) = delete;

  ~EnableViaDialogFlow() override {}

  void Run() {
    DCHECK(!service_->IsExtensionEnabled(extension_id_));
    flow_ =
        std::make_unique<ExtensionEnableFlow>(profile_, extension_id_, this);
    flow_->Start();
  }

 private:
  // ExtensionEnableFlowDelegate overrides.
  void ExtensionEnableFlowFinished() override {
    const Extension* extension =
        registry_->GetExtensionById(extension_id_, ExtensionRegistry::ENABLED);
    if (!extension)
      return;
    std::move(callback_).Run();
    delete this;
  }

  void ExtensionEnableFlowAborted(bool user_initiated) override { delete this; }

  raw_ptr<ExtensionService> service_;
  raw_ptr<ExtensionRegistry> registry_;
  raw_ptr<Profile> profile_;
  std::string extension_id_;
  base::OnceClosure callback_;
  std::unique_ptr<ExtensionEnableFlow> flow_;
};

const Extension* GetExtension(Profile* profile,
                              const apps::AppLaunchParams& params) {
  if (params.app_id.empty())
    return nullptr;
  ExtensionRegistry* registry = ExtensionRegistry::Get(profile);
  return registry->GetExtensionById(
      params.app_id, ExtensionRegistry::ENABLED | ExtensionRegistry::DISABLED |
                         ExtensionRegistry::TERMINATED);
}

bool IsAllowedToOverrideURL(const extensions::Extension* extension,
                            const GURL& override_url) {
  if (extension->web_extent().MatchesURL(override_url))
    return true;

  if (override_url.DeprecatedGetOriginAsURL() == extension->url())
    return true;

  return false;
}

// Get the launch URL for a given extension, with optional override/fallback.
// |override_url|, if non-empty, will be preferred over the extension's
// launch url.
GURL UrlForExtension(const extensions::Extension* extension,
                     Profile* profile,
                     const apps::AppLaunchParams& params) {
  if (!extension)
    return params.override_url;

  GURL url;
  if (!params.override_url.is_empty()) {
    DCHECK(IsAllowedToOverrideURL(extension, params.override_url));
    url = params.override_url;
  } else {
    url = extensions::AppLaunchInfo::GetFullLaunchURL(extension);
  }

  // For extensions lacking launch urls, determine a reasonable fallback.
  if (!url.is_valid()) {
    url = extensions::OptionsPageInfo::GetOptionsPage(extension);
    if (!url.is_valid())
      url = GURL(chrome::kChromeUIExtensionsURL);
  }

  return url;
}

ui::WindowShowState DetermineWindowShowState(Profile* profile,
                                             apps::LaunchContainer container,
                                             const Extension* extension) {
  if (!extension || container != apps::LaunchContainer::kLaunchContainerWindow)
    return ui::SHOW_STATE_DEFAULT;

  if (chrome::IsRunningInForcedAppMode())
    return ui::SHOW_STATE_FULLSCREEN;

#if BUILDFLAG(IS_CHROMEOS_ASH)
  // In ash, LAUNCH_TYPE_FULLSCREEN launches in a maximized app window and
  // LAUNCH_TYPE_WINDOW launches in a default app window.
  extensions::LaunchType launch_type =
      extensions::GetLaunchType(ExtensionPrefs::Get(profile), extension);
  if (launch_type == extensions::LAUNCH_TYPE_FULLSCREEN)
    return ui::SHOW_STATE_MAXIMIZED;
  else if (launch_type == extensions::LAUNCH_TYPE_WINDOW)
    return ui::SHOW_STATE_DEFAULT;
#endif

  return ui::SHOW_STATE_DEFAULT;
}

WebContents* OpenApplicationTab(Profile* profile,
                                const apps::AppLaunchParams& launch_params,
                                const GURL& url) {
  const Extension* extension = GetExtension(profile, launch_params);
  CHECK(extension);
  WindowOpenDisposition disposition = launch_params.disposition;

  Browser* browser =
      chrome::FindTabbedBrowser(profile, false, launch_params.display_id);
  WebContents* contents = nullptr;
  if (browser) {
    // For existing browser, ensure its window is shown and activated.
    browser->window()->Show();
    browser->window()->Activate();
  } else {
    // No browser for this profile, need to open a new one.
    if (Browser::GetCreationStatusForProfile(profile) !=
        Browser::CreationStatus::kOk) {
      return contents;
    }

    // TODO(erg): AppLaunchParams should pass user_gesture from the extension
    // system to here.
    browser = Browser::Create(
        Browser::CreateParams(Browser::TYPE_NORMAL, profile, true));
    browser->window()->Show();
    // There's no current tab in this browser window, so add a new one.
    disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
  }

  extensions::LaunchType launch_type =
      extensions::GetLaunchType(ExtensionPrefs::Get(profile), extension);
  UMA_HISTOGRAM_ENUMERATION("Extensions.AppTabLaunchType", launch_type, 100);

  int add_type = AddTabTypes::ADD_ACTIVE;
  if (launch_type == extensions::LAUNCH_TYPE_PINNED)
    add_type |= AddTabTypes::ADD_PINNED;

  ui::PageTransition transition = ui::PAGE_TRANSITION_AUTO_BOOKMARK;
  NavigateParams params(browser, url, transition);
  params.tabstrip_add_types = add_type;
  params.disposition = disposition;

  if (disposition == WindowOpenDisposition::CURRENT_TAB) {
    WebContents* existing_tab =
        browser->tab_strip_model()->GetActiveWebContents();
    TabStripModel* model = browser->tab_strip_model();
    int tab_index = model->GetIndexOfWebContents(existing_tab);

    existing_tab->OpenURL(content::OpenURLParams(
        url,
        content::Referrer::SanitizeForRequest(
            url, content::Referrer(existing_tab->GetURL(),
                                   network::mojom::ReferrerPolicy::kDefault)),
        disposition, transition, false));
    // Reset existing_tab as OpenURL() may have clobbered it.
    existing_tab = browser->tab_strip_model()->GetActiveWebContents();
    if (params.tabstrip_add_types & AddTabTypes::ADD_PINNED) {
      model->SetTabPinned(tab_index, true);
      // Pinning may have moved the tab.
      tab_index = model->GetIndexOfWebContents(existing_tab);
    }
    if (params.tabstrip_add_types & AddTabTypes::ADD_ACTIVE) {
      model->ActivateTabAt(
          tab_index, TabStripUserGestureDetails(
                         TabStripUserGestureDetails::GestureType::kOther));
    }

    contents = existing_tab;
  } else {
    Navigate(&params);
    contents = params.navigated_or_inserted_contents;
  }

#if BUILDFLAG(IS_CHROMEOS_ASH)
  // In ash, LAUNCH_FULLSCREEN launches in the OpenApplicationWindow function
  // i.e. it should not reach here.
  DCHECK(launch_type != extensions::LAUNCH_TYPE_FULLSCREEN);
#else
  // TODO(skerner):  If we are already in full screen mode, and the user set the
  // app to open as a regular or pinned tab, what should happen? Today we open
  // the tab, but stay in full screen mode.  Should we leave full screen mode in
  // this case?
  if (launch_type == extensions::LAUNCH_TYPE_FULLSCREEN &&
      !browser->window()->IsFullscreen()) {
    chrome::ToggleFullscreenMode(browser);
  }
#endif  // BUILDFLAG(IS_CHROMEOS_ASH)
  return contents;
}

WebContents* OpenEnabledApplication(Profile* profile,
                                    apps::AppLaunchParams&& params) {
  const Extension* extension = GetExtension(profile, params);
  if (!extension)
    return nullptr;

#if BUILDFLAG(IS_CHROMEOS_LACROS)
  if (!profile->IsMainProfile())
    return nullptr;
#endif

  WebContents* tab = nullptr;
  ExtensionPrefs* prefs = ExtensionPrefs::Get(profile);
  prefs->SetActiveBit(extension->id(), true);

  if (CanLaunchViaEvent(extension)) {
    // When launching an app with a command line, there might be a file path to
    // work with that command line, so
    // LaunchPlatformAppWithCommandLineAndLaunchId should be called to handle
    // the command line. If |launch_files| is set without |command_line|, that
    // means launching the app with files, so call
    // LaunchPlatformAppWithFile{Handler,Paths} to forward |launch_files| to the
    // app.
    if (params.command_line.GetArgs().empty() && !params.launch_files.empty()) {
      if (params.intent && params.intent->activity_name) {
        apps::LaunchPlatformAppWithFileHandler(
            profile, extension, params.intent->activity_name.value(),
            params.launch_files);
      } else {
        apps::LaunchPlatformAppWithFilePaths(profile, extension,
                                             params.launch_files);
      }
      return nullptr;
    }

    apps::LaunchPlatformAppWithCommandLineAndLaunchId(
        profile, extension, params.launch_id, params.command_line,
        params.current_directory,
        apps::GetAppLaunchSource(params.launch_source));
    return nullptr;
  }

  UMA_HISTOGRAM_ENUMERATION("Extensions.HostedAppLaunchContainer",
                            params.container);

  GURL url = UrlForExtension(extension, profile, params);

  // Record v1 app launch. Platform app launch is recorded when dispatching
  // the onLaunched event.
  prefs->SetLastLaunchTime(extension->id(), base::Time::Now());

  switch (params.container) {
    case apps::LaunchContainer::kLaunchContainerNone: {
      NOTREACHED();
      break;
    }
    // Panels are deprecated. Launch a normal window instead.
    case apps::LaunchContainer::kLaunchContainerPanelDeprecated:
    case apps::LaunchContainer::kLaunchContainerWindow:
      tab = OpenApplicationWindow(profile, params, url);
      break;
    case apps::LaunchContainer::kLaunchContainerTab: {
      tab = OpenApplicationTab(profile, params, url);
      break;
    }
    default:
      NOTREACHED();
      break;
  }

  return tab;
}

}  // namespace

WebContents* OpenApplication(Profile* profile, apps::AppLaunchParams&& params) {
  return OpenEnabledApplication(profile, std::move(params));
}

Browser* CreateApplicationWindow(Profile* profile,
                                 const apps::AppLaunchParams& params,
                                 const GURL& url) {
  const Extension* const extension = GetExtension(profile, params);

  std::string app_name;
  if (!params.override_app_name.empty())
    app_name = params.override_app_name;
  else if (extension)
    app_name = web_app::GenerateApplicationNameFromAppId(extension->id());
  else
    app_name = web_app::GenerateApplicationNameFromURL(url);

  gfx::Rect initial_bounds;
  if (!params.override_bounds.IsEmpty()) {
    initial_bounds = params.override_bounds;
  } else if (extension) {
    initial_bounds.set_width(
        extensions::AppLaunchInfo::GetLaunchWidth(extension));
    initial_bounds.set_height(
        extensions::AppLaunchInfo::GetLaunchHeight(extension));
  }

  // Place new windows on the specified display.
  display::ScopedDisplayForNewWindows scoped_display(params.display_id);

  // TODO(erg): AppLaunchParams should pass through the user_gesture from the
  // extension system here.
  Browser::CreateParams browser_params(
      params.disposition == WindowOpenDisposition::NEW_POPUP
          ? Browser::CreateParams::CreateForAppPopup(app_name,
                                                     /*trusted_source=*/true,
                                                     initial_bounds, profile,
                                                     /*user_gesture=*/true)
          : Browser::CreateParams::CreateForApp(app_name,
                                                /*trusted_source=*/true,
                                                initial_bounds, profile,
                                                /*user_gesture=*/true));

  browser_params.initial_show_state =
      DetermineWindowShowState(profile, params.container, extension);

  return Browser::Create(browser_params);
}

WebContents* NavigateApplicationWindow(Browser* browser,
                                       const apps::AppLaunchParams& params,
                                       const GURL& url,
                                       WindowOpenDisposition disposition) {
  const Extension* const extension = GetExtension(browser->profile(), params);
  ui::PageTransition transition =
      (extension ? ui::PAGE_TRANSITION_AUTO_BOOKMARK
                 : ui::PAGE_TRANSITION_AUTO_TOPLEVEL);

  NavigateParams nav_params(browser, url, transition);
  nav_params.disposition = disposition;
  Navigate(&nav_params);

  WebContents* const web_contents = nav_params.navigated_or_inserted_contents;

  if (extension) {
    DCHECK(extension->is_app());
    extensions::TabHelper::FromWebContents(web_contents)
        ->SetExtensionApp(extension);
  }
  web_app::SetAppPrefsForWebContents(web_contents);

  return web_contents;
}

WebContents* OpenApplicationWindow(Profile* profile,
                                   const apps::AppLaunchParams& params,
                                   const GURL& url) {
  if (Browser::GetCreationStatusForProfile(profile) !=
      Browser::CreationStatus::kOk) {
    return nullptr;
  }

  Browser* browser = CreateApplicationWindow(profile, params, url);
  WebContents* web_contents = NavigateApplicationWindow(
      browser, params, url, WindowOpenDisposition::NEW_FOREGROUND_TAB);

  browser->window()->Show();
  return web_contents;
}

void OpenApplicationWithReenablePrompt(Profile* profile,
                                       apps::AppLaunchParams&& params) {
  const Extension* extension = GetExtension(profile, params);
  if (!extension)
    return;

  ExtensionService* service =
      extensions::ExtensionSystem::Get(profile)->extension_service();
  ExtensionRegistry* registry = ExtensionRegistry::Get(profile);
  if (!service->IsExtensionEnabled(extension->id()) ||
      registry->GetExtensionById(extension->id(),
                                 ExtensionRegistry::TERMINATED)) {
    // TODO(pkotwicz): Figure out which window should be used as the parent for
    // the "enable application" dialog in Athena.
    (new EnableViaDialogFlow(
         service, registry, profile, extension->id(),
         base::BindOnce(base::IgnoreResult(OpenEnabledApplication), profile,
                        std::move(params))))
        ->Run();
    return;
  }

  OpenEnabledApplication(profile, std::move(params));
}

WebContents* OpenAppShortcutWindow(Profile* profile, const GURL& url) {
  apps::AppLaunchParams launch_params(
      std::string(),  // this is a URL app. No app id.
      apps::LaunchContainer::kLaunchContainerWindow,
      WindowOpenDisposition::NEW_WINDOW, apps::LaunchSource::kFromCommandLine);
  launch_params.override_url = url;

  WebContents* tab = OpenApplicationWindow(profile, launch_params, url);

  if (!tab)
    return nullptr;

  return tab;
}

bool CanLaunchViaEvent(const extensions::Extension* extension) {
  const extensions::Feature* feature =
      extensions::FeatureProvider::GetAPIFeature("app.runtime");
  return feature && feature->IsAvailableToExtension(extension).is_available();
}

void LaunchAppWithCallback(
    Profile* profile,
    const std::string& app_id,
    const base::CommandLine& command_line,
    const base::FilePath& current_directory,
    base::OnceCallback<void(Browser* browser, apps::LaunchContainer container)>
        callback) {
  apps::LaunchContainer container;
  if (apps::OpenExtensionApplicationWindow(profile, app_id, command_line,
                                           current_directory)) {
    container = apps::LaunchContainer::kLaunchContainerWindow;
  } else if (apps::OpenExtensionApplicationTab(profile, app_id)) {
    container = apps::LaunchContainer::kLaunchContainerTab;
  } else {
    // Open an empty browser window as the app_id is invalid.
    apps::CreateBrowserWithNewTabPage(profile);
    container = apps::LaunchContainer::kLaunchContainerNone;
  }
  std::move(callback).Run(BrowserList::GetInstance()->GetLastActive(),
                          container);
}

bool ShowBrowserForProfile(Profile* profile,
                           const apps::AppLaunchParams& params) {
  Browser* browser = chrome::FindTabbedBrowser(
      profile, /*match_original_profiles*/ false, params.display_id);
  if (browser) {
    // For existing browser, ensure its window is shown and activated.
    browser->window()->Show();
    browser->window()->Activate();
  } else {
    // No browser for this profile, need to open a new one.
    if (Browser::GetCreationStatusForProfile(profile) !=
        Browser::CreationStatus::kOk) {
      return false;
    }
    browser = Browser::Create(
        Browser::CreateParams(Browser::TYPE_NORMAL, profile, true));
    browser->window()->Show();
  }
  return true;
}
