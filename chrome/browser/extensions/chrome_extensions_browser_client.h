// Copyright 2013 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_CHROME_EXTENSIONS_BROWSER_CLIENT_H_
#define CHROME_BROWSER_EXTENSIONS_CHROME_EXTENSIONS_BROWSER_CLIENT_H_

#include <memory>
#include <string>
#include <vector>

#include "base/lazy_instance.h"
#include "base/memory/ref_counted.h"
#include "build/build_config.h"
#include "build/chromeos_buildflags.h"
#include "chrome/browser/extensions/user_script_listener.h"
#include "extensions/browser/extensions_browser_client.h"
#include "extensions/browser/kiosk/kiosk_delegate.h"
#include "extensions/common/mojom/view_type.mojom.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "services/network/public/mojom/fetch_api.mojom.h"

namespace base {
class CommandLine;
}

namespace content {
class BrowserContext;
}

namespace extensions {

class ChromeComponentExtensionResourceManager;
class ChromeExtensionsAPIClient;
class ChromeProcessManagerDelegate;
class ScopedExtensionUpdaterKeepAlive;

// Implementation of BrowserClient for Chrome, which includes
// knowledge of Profiles, BrowserContexts and incognito.
//
// NOTE: Methods that do not require knowledge of browser concepts should be
// implemented in ChromeExtensionsClient even if they are only used in the
// browser process (see chrome/common/extensions/chrome_extensions_client.h).
class ChromeExtensionsBrowserClient : public ExtensionsBrowserClient {
 public:
  ChromeExtensionsBrowserClient();

  ChromeExtensionsBrowserClient(const ChromeExtensionsBrowserClient&) = delete;
  ChromeExtensionsBrowserClient& operator=(
      const ChromeExtensionsBrowserClient&) = delete;

  ~ChromeExtensionsBrowserClient() override;

  // ExtensionsBrowserClient overrides:
  bool IsShuttingDown() override;
  bool AreExtensionsDisabled(const base::CommandLine& command_line,
                             content::BrowserContext* context) override;
  bool IsValidContext(content::BrowserContext* context) override;
  bool IsSameContext(content::BrowserContext* first,
                     content::BrowserContext* second) override;
  bool HasOffTheRecordContext(content::BrowserContext* context) override;
  content::BrowserContext* GetOffTheRecordContext(
      content::BrowserContext* context) override;
  content::BrowserContext* GetOriginalContext(
      content::BrowserContext* context) override;

  content::BrowserContext* GetRedirectedContextInIncognito(
      content::BrowserContext* context,
      bool force_guest_profile,
      bool force_system_profile) override;
  content::BrowserContext* GetContextForRegularAndIncognito(
      content::BrowserContext* context,
      bool force_guest_profile,
      bool force_system_profile) override;
  content::BrowserContext* GetRegularProfile(
      content::BrowserContext* context,
      bool force_guest_profile,
      bool force_system_profile) override;

#if BUILDFLAG(IS_CHROMEOS_ASH)
  std::string GetUserIdHashFromContext(
      content::BrowserContext* context) override;
#endif
#if BUILDFLAG(IS_CHROMEOS_LACROS)
  bool IsFromMainProfile(content::BrowserContext* context) override;
#endif
  bool IsGuestSession(content::BrowserContext* context) const override;
  bool IsExtensionIncognitoEnabled(
      const std::string& extension_id,
      content::BrowserContext* context) const override;
  bool CanExtensionCrossIncognito(
      const Extension* extension,
      content::BrowserContext* context) const override;
  base::FilePath GetBundleResourcePath(
      const network::ResourceRequest& request,
      const base::FilePath& extension_resources_path,
      int* resource_id) const override;
  void LoadResourceFromResourceBundle(
      const network::ResourceRequest& request,
      mojo::PendingReceiver<network::mojom::URLLoader> loader,
      const base::FilePath& resource_relative_path,
      int resource_id,
      scoped_refptr<net::HttpResponseHeaders> headers,
      mojo::PendingRemote<network::mojom::URLLoaderClient> client) override;
  bool AllowCrossRendererResourceLoad(
      const network::ResourceRequest& request,
      network::mojom::RequestDestination destination,
      ui::PageTransition page_transition,
      int child_id,
      bool is_incognito,
      const Extension* extension,
      const ExtensionSet& extensions,
      const ProcessMap& process_map) override;
  PrefService* GetPrefServiceForContext(
      content::BrowserContext* context) override;
  void GetEarlyExtensionPrefsObservers(
      content::BrowserContext* context,
      std::vector<EarlyExtensionPrefsObserver*>* observers) const override;
  ProcessManagerDelegate* GetProcessManagerDelegate() const override;
  std::unique_ptr<ExtensionHostDelegate> CreateExtensionHostDelegate() override;
  bool DidVersionUpdate(content::BrowserContext* context) override;
  void PermitExternalProtocolHandler() override;
  bool IsInDemoMode() override;
  bool IsScreensaverInDemoMode(const std::string& app_id) override;
  bool IsRunningInForcedAppMode() override;
  bool IsAppModeForcedForApp(const ExtensionId& extension_id) override;
  bool IsLoggedInAsPublicAccount() override;
  ExtensionSystemProvider* GetExtensionSystemFactory() override;
  void RegisterBrowserInterfaceBindersForFrame(
      mojo::BinderMapWithContext<content::RenderFrameHost*>* binder_map,
      content::RenderFrameHost* render_frame_host,
      const Extension* extension) const override;
  std::unique_ptr<RuntimeAPIDelegate> CreateRuntimeAPIDelegate(
      content::BrowserContext* context) const override;
  const ComponentExtensionResourceManager*
  GetComponentExtensionResourceManager() override;
  void BroadcastEventToRenderers(
      events::HistogramValue histogram_value,
      const std::string& event_name,
      base::Value::List args,
      bool dispatch_to_off_the_record_profiles) override;
  ExtensionCache* GetExtensionCache() override;
  bool IsBackgroundUpdateAllowed() override;
  bool IsMinBrowserVersionSupported(const std::string& min_version) override;
  ExtensionWebContentsObserver* GetExtensionWebContentsObserver(
      content::WebContents* web_contents) override;
  void ReportError(content::BrowserContext* context,
                   std::unique_ptr<ExtensionError> error) override;
  void CleanUpWebView(content::BrowserContext* browser_context,
                      int embedder_process_id,
                      int view_instance_id) override;
  void ClearBackForwardCache() override;
  void AttachExtensionTaskManagerTag(content::WebContents* web_contents,
                                     mojom::ViewType view_type) override;
  scoped_refptr<update_client::UpdateClient> CreateUpdateClient(
      content::BrowserContext* context) override;
  std::unique_ptr<ScopedExtensionUpdaterKeepAlive> CreateUpdaterKeepAlive(
      content::BrowserContext* context) override;
  bool IsActivityLoggingEnabled(content::BrowserContext* context) override;
  void GetTabAndWindowIdForWebContents(content::WebContents* web_contents,
                                       int* tab_id,
                                       int* window_id) override;
  KioskDelegate* GetKioskDelegate() override;
  bool IsLockScreenContext(content::BrowserContext* context) override;
  std::string GetApplicationLocale() override;
  bool IsExtensionEnabled(const std::string& extension_id,
                          content::BrowserContext* context) const override;
  bool IsWebUIAllowedToMakeNetworkRequests(const url::Origin& origin) override;
  network::mojom::NetworkContext* GetSystemNetworkContext() override;
  UserScriptListener* GetUserScriptListener() override;
  void SignalContentScriptsLoaded(content::BrowserContext* context) override;
  std::string GetUserAgent() const override;
  bool ShouldSchemeBypassNavigationChecks(
      const std::string& scheme) const override;
  base::FilePath GetSaveFilePath(content::BrowserContext* context) override;
  void SetLastSaveFilePath(content::BrowserContext* context,
                           const base::FilePath& path) override;
  bool HasIsolatedStorage(const std::string& extension_id,
                          content::BrowserContext* context) override;
  bool IsScreenshotRestricted(
      content::WebContents* web_contents) const override;
  bool IsValidTabId(content::BrowserContext* context,
                    int tab_id) const override;
  bool IsExtensionTelemetryServiceEnabled(
      content::BrowserContext* context) const override;
  bool IsExtensionTelemetryRemoteHostContactedSignalEnabled() const override;
  void NotifyExtensionApiTabExecuteScript(
      content::BrowserContext* context,
      const ExtensionId& extension_id,
      const std::string& code) const override;
  void NotifyExtensionRemoteHostContacted(content::BrowserContext* context,
                                          const ExtensionId& extension_id,
                                          const GURL& url) const override;
  static void set_did_chrome_update_for_testing(bool did_update);
  bool IsUsbDeviceAllowedByPolicy(content::BrowserContext* context,
                                  const ExtensionId& extension_id,
                                  int vendor_id,
                                  int product_id) const override;
  void GetFavicon(content::BrowserContext* browser_context,
                  const Extension* extension,
                  const GURL& url,
                  base::CancelableTaskTracker* tracker,
                  base::OnceCallback<
                      void(scoped_refptr<base::RefCountedMemory> bitmap_data)>
                      callback) const override;
  std::vector<content::BrowserContext*> GetRelatedContextsForExtension(
      content::BrowserContext* browser_context,
      const Extension& extension) const override;
  void AddAdditionalAllowedHosts(
      const PermissionSet& desired_permissions,
      PermissionSet* granted_permissions) const override;

 private:
  friend struct base::LazyInstanceTraitsBase<ChromeExtensionsBrowserClient>;

  // Support for ProcessManager.
  std::unique_ptr<ChromeProcessManagerDelegate> process_manager_delegate_;

  // Client for API implementations.
  std::unique_ptr<ChromeExtensionsAPIClient> api_client_;

  std::unique_ptr<ChromeComponentExtensionResourceManager> resource_manager_;

  std::unique_ptr<ExtensionCache> extension_cache_;

  std::unique_ptr<KioskDelegate> kiosk_delegate_;

  UserScriptListener user_script_listener_;
};

}  // namespace extensions

#endif  // CHROME_BROWSER_EXTENSIONS_CHROME_EXTENSIONS_BROWSER_CLIENT_H_
