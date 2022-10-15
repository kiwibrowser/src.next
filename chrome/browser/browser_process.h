// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This interface is for managing the global services of the application. Each
// service is lazily created when requested the first time. The service getters
// will return NULL if the service is not available, so callers must check for
// this condition.

#ifndef CHROME_BROWSER_BROWSER_PROCESS_H_
#define CHROME_BROWSER_BROWSER_PROCESS_H_

#include <stdint.h>

#include <memory>
#include <string>

#include "base/callback_forward.h"
#include "base/memory/scoped_refptr.h"
#include "build/build_config.h"
#include "build/chromeos_buildflags.h"
#include "chrome/common/buildflags.h"
#include "media/media_buildflags.h"

class BackgroundModeManager;
class BrowserProcessPlatformPart;
class BuildState;
class DownloadRequestLimiter;
class DownloadStatusUpdater;
class GpuModeManager;
class HidPolicyAllowedDevices;
class IconManager;
class MediaFileSystemRegistry;
class NotificationPlatformBridge;
class NotificationUIManager;
class PrefService;
class ProfileManager;
class SerialPolicyAllowedPorts;
class StartupData;
class StatusTray;
class SystemNetworkContextManager;
class WebRtcLogUploader;

#if !BUILDFLAG(IS_ANDROID)
class IntranetRedirectDetector;
#endif

namespace breadcrumbs {
class BreadcrumbPersistentStorageManager;
}

namespace network {
class NetworkQualityTracker;
class SharedURLLoaderFactory;
}

namespace safe_browsing {
class SafeBrowsingService;
}

namespace subresource_filter {
class RulesetService;
}

namespace variations {
class VariationsService;
}

namespace component_updater {
class ComponentUpdateService;
}

namespace extensions {
class EventRouterForwarder;
}

namespace gcm {
class GCMDriver;
}

namespace metrics {
class MetricsService;
}

namespace metrics_services_manager {
class MetricsServicesManager;
}

namespace network_time {
class NetworkTimeTracker;
}

namespace policy {
class ChromeBrowserPolicyConnector;
class PolicyService;
}

namespace printing {
class BackgroundPrintingManager;
class PrintJobManager;
class PrintPreviewDialogController;
}

namespace resource_coordinator {
class ResourceCoordinatorParts;
class TabManager;
}

// NOT THREAD SAFE, call only from the main thread.
// These functions shouldn't return NULL unless otherwise noted.
class BrowserProcess {
 public:
  BrowserProcess();

  BrowserProcess(const BrowserProcess&) = delete;
  BrowserProcess& operator=(const BrowserProcess&) = delete;

  virtual ~BrowserProcess();

  // Invoked when the user is logging out/shutting down. When logging off we may
  // not have enough time to do a normal shutdown. This method is invoked prior
  // to normal shutdown and saves any state that must be saved before system
  // shutdown.
  virtual void EndSession() = 0;

  // Ensures |local_state()| was flushed to disk and then posts |reply| back on
  // the current sequence.
  virtual void FlushLocalStateAndReply(base::OnceClosure reply) = 0;

  // Gets the manager for the various metrics-related services, constructing it
  // if necessary.
  virtual metrics_services_manager::MetricsServicesManager*
  GetMetricsServicesManager() = 0;

  // Services: any of these getters may return NULL
  virtual metrics::MetricsService* metrics_service() = 0;
  virtual ProfileManager* profile_manager() = 0;
  virtual PrefService* local_state() = 0;
  virtual scoped_refptr<network::SharedURLLoaderFactory>
  shared_url_loader_factory() = 0;
  virtual variations::VariationsService* variations_service() = 0;

  virtual BrowserProcessPlatformPart* platform_part() = 0;

  virtual extensions::EventRouterForwarder*
      extension_event_router_forwarder() = 0;

  // Returns the manager for desktop notifications.
  // TODO(miguelg) This is in the process of being deprecated in favour of
  // NotificationPlatformBridge + NotificationDisplayService
  virtual NotificationUIManager* notification_ui_manager() = 0;
  virtual NotificationPlatformBridge* notification_platform_bridge() = 0;

  // Replacement for IOThread. It owns and manages the
  // NetworkContext which will use the network service when the network service
  // is enabled. When the network service is not enabled, its NetworkContext is
  // backed by the IOThread's URLRequestContext.
  virtual SystemNetworkContextManager* system_network_context_manager() = 0;

  // Returns a NetworkQualityTracker that can be used to subscribe for
  // network quality change events.
  virtual network::NetworkQualityTracker* network_quality_tracker() = 0;

  // Starts and manages the policy system.
  virtual policy::ChromeBrowserPolicyConnector* browser_policy_connector() = 0;

  // This is the main interface for chromium components to retrieve policy
  // information from the policy system.
  virtual policy::PolicyService* policy_service() = 0;

  virtual IconManager* icon_manager() = 0;

  virtual GpuModeManager* gpu_mode_manager() = 0;

  virtual void CreateDevToolsProtocolHandler() = 0;

  virtual void CreateDevToolsAutoOpener() = 0;

  virtual bool IsShuttingDown() = 0;

  virtual printing::PrintJobManager* print_job_manager() = 0;
  virtual printing::PrintPreviewDialogController*
      print_preview_dialog_controller() = 0;
  virtual printing::BackgroundPrintingManager*
      background_printing_manager() = 0;

#if !BUILDFLAG(IS_ANDROID)
  virtual IntranetRedirectDetector* intranet_redirect_detector() = 0;
#endif

  // Returns the locale used by the application. It is the IETF language tag,
  // defined in BCP 47. The region subtag is not included when it adds no
  // distinguishing information to the language tag (e.g. both "en-US" and "fr"
  // are correct here).
  virtual const std::string& GetApplicationLocale() = 0;
  virtual void SetApplicationLocale(const std::string& actual_locale) = 0;

  virtual DownloadStatusUpdater* download_status_updater() = 0;
  virtual DownloadRequestLimiter* download_request_limiter() = 0;

  // Returns the object that manages background applications.
  virtual BackgroundModeManager* background_mode_manager() = 0;
#if BUILDFLAG(ENABLE_BACKGROUND_MODE)
  virtual void set_background_mode_manager_for_test(
      std::unique_ptr<BackgroundModeManager> manager) = 0;
#endif

  // Returns the StatusTray, which provides an API for displaying status icons
  // in the system status tray. Returns NULL if status icons are not supported
  // on this platform (or this is a unit test).
  virtual StatusTray* status_tray() = 0;

  // Returns the SafeBrowsing service.
  virtual safe_browsing::SafeBrowsingService* safe_browsing_service() = 0;

  // Returns the service providing versioned storage for rules used by the Safe
  // Browsing subresource filter.
  virtual subresource_filter::RulesetService*
  subresource_filter_ruleset_service() = 0;

  // Returns the StartupData which owns any pre-created objects in //chrome
  // before the full browser starts.
  virtual StartupData* startup_data() = 0;

// TODO(crbug.com/1052397): Revisit once build flag switch of lacros-chrome is
// complete.
#if BUILDFLAG(IS_WIN) || (BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS_LACROS))
  // This will start a timer that, if Chrome is in persistent mode, will check
  // whether an update is available, and if that's the case, restart the
  // browser. Note that restart code will strip some of the command line keys
  // and all loose values from the cl this instance of Chrome was launched with,
  // and add the command line key that will force Chrome to start in the
  // background mode. For the full list of "blacklisted" keys, refer to
  // |kSwitchesToRemoveOnAutorestart| array in browser_process_impl.cc.
  virtual void StartAutoupdateTimer() = 0;
#endif

  virtual component_updater::ComponentUpdateService* component_updater() = 0;

  virtual MediaFileSystemRegistry* media_file_system_registry() = 0;

  virtual WebRtcLogUploader* webrtc_log_uploader() = 0;

  virtual network_time::NetworkTimeTracker* network_time_tracker() = 0;

#if !BUILDFLAG(IS_ANDROID)
  // Avoid using this. Prefer using GCMProfileServiceFactory.
  virtual gcm::GCMDriver* gcm_driver() = 0;
#endif

  // Returns the tab manager. On non-supported platforms, this returns null.
  // TODO(sebmarchand): Update callers to
  // resource_coordinator_parts()->tab_manager() and remove this.
  virtual resource_coordinator::TabManager* GetTabManager() = 0;

  virtual resource_coordinator::ResourceCoordinatorParts*
  resource_coordinator_parts() = 0;

#if !BUILDFLAG(IS_ANDROID)
  // Returns the object which keeps track of serial port permissions configured
  // through the policy engine.
  virtual SerialPolicyAllowedPorts* serial_policy_allowed_ports() = 0;

  // Returns the object which keeps track of Human Interface Device (HID)
  // permissions configured through the policy engine.
  virtual HidPolicyAllowedDevices* hid_policy_allowed_devices() = 0;
#endif

  virtual BuildState* GetBuildState() = 0;

  // Returns the BreadcrumbPersistentStorageManager writing breadcrumbs to disk,
  // or nullptr if breadcrumbs logging is disabled.
  virtual breadcrumbs::BreadcrumbPersistentStorageManager*
  GetBreadcrumbPersistentStorageManager() = 0;
};

extern BrowserProcess* g_browser_process;

#endif  // CHROME_BROWSER_BROWSER_PROCESS_H_
