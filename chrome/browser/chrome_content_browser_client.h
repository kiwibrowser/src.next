// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROME_CONTENT_BROWSER_CLIENT_H_
#define CHROME_BROWSER_CHROME_CONTENT_BROWSER_CLIENT_H_

#include <stddef.h>

#include <memory>
#include <set>
#include <string>
#include <vector>

#include "base/callback.h"
#include "base/containers/flat_map.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "base/values.h"
#include "build/build_config.h"
#include "build/chromeos_buildflags.h"
#include "chrome/browser/startup_data.h"
#include "components/embedder_support/user_agent_utils.h"
#include "components/file_access/scoped_file_access.h"
#include "components/safe_browsing/content/browser/web_api_handshake_checker.h"
#include "content/public/browser/child_process_security_policy.h"
#include "content/public/browser/content_browser_client.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/alternative_error_page_override_info.mojom-forward.h"
#include "extensions/buildflags/buildflags.h"
#include "media/media_buildflags.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "services/metrics/public/cpp/ukm_source_id.h"

class ChromeContentBrowserClientParts;
class PrefRegistrySimple;
class ScopedKeepAlive;

namespace base {
class CommandLine;
}  // namespace base

namespace blink {
namespace mojom {
class WindowFeatures;
class WebUsbService;
}  // namespace mojom
namespace web_pref {
struct WebPreferences;
}  // namespace web_pref
class StorageKey;
class URLLoaderThrottle;
}  // namespace blink

namespace content {
class BrowserContext;
class QuotaPermissionContext;
class RenderFrameHost;
enum class SmsFetchFailureType;
struct ServiceWorkerVersionBaseInfo;
}  // namespace content

namespace net {
class IsolationInfo;
class SiteForCookies;
}  // namespace net

namespace permissions {
class BluetoothDelegateImpl;
}  // namespace permissions

namespace safe_browsing {
class RealTimeUrlLookupServiceBase;
class SafeBrowsingService;
class UrlCheckerDelegate;
}  // namespace safe_browsing

namespace sandbox {
class SeatbeltExecClient;
}  // namespace sandbox

namespace ui {
class NativeTheme;
}  // namespace ui

namespace url {
class Origin;
}  // namespace url

namespace user_prefs {
class PrefRegistrySyncable;
}  // namespace user_prefs

namespace version_info {
enum class Channel;
}  // namespace version_info

class ChromeHidDelegate;
class ChromeSerialDelegate;
class ChromeWebAuthenticationDelegate;

#if BUILDFLAG(ENABLE_VR)
namespace vr {
class ChromeXrIntegrationClient;
}
#endif

class ChromeContentBrowserClient : public content::ContentBrowserClient {
 public:
  ChromeContentBrowserClient();

  ChromeContentBrowserClient(const ChromeContentBrowserClient&) = delete;
  ChromeContentBrowserClient& operator=(const ChromeContentBrowserClient&) =
      delete;

  ~ChromeContentBrowserClient() override;

  // TODO(https://crbug.com/787567): This file is about calls from content/ out
  // to chrome/ to get values or notify about events, but both of these
  // functions are from chrome/ to chrome/ and don't involve content/ at all.
  // That suggests they belong somewhere else at the chrome/ layer.
  static void RegisterLocalStatePrefs(PrefRegistrySimple* registry);
  static void RegisterProfilePrefs(user_prefs::PrefRegistrySyncable* registry);

  // Notification that the application locale has changed. This allows us to
  // update our I/O thread cache of this value.
  static void SetApplicationLocale(const std::string& locale);

  // content::ContentBrowserClient:
  std::unique_ptr<content::BrowserMainParts> CreateBrowserMainParts(
      bool is_integration_test) override;
  void PostAfterStartupTask(
      const base::Location& from_here,
      const scoped_refptr<base::SequencedTaskRunner>& task_runner,
      base::OnceClosure task) override;
  bool IsBrowserStartupComplete() override;
  void SetBrowserStartupIsCompleteForTesting() override;
  bool IsShuttingDown() override;
  content::StoragePartitionConfig GetStoragePartitionConfigForSite(
      content::BrowserContext* browser_context,
      const GURL& site) override;
  std::unique_ptr<content::WebContentsViewDelegate> GetWebContentsViewDelegate(
      content::WebContents* web_contents) override;
  void RenderProcessWillLaunch(content::RenderProcessHost* host) override;
  bool AllowGpuLaunchRetryOnIOThread() override;
  GURL GetEffectiveURL(content::BrowserContext* browser_context,
                       const GURL& url) override;
  bool ShouldCompareEffectiveURLsForSiteInstanceSelection(
      content::BrowserContext* browser_context,
      content::SiteInstance* candidate_site_instance,
      bool is_main_frame,
      const GURL& candidate_url,
      const GURL& destination_url) override;
  bool ShouldUseMobileFlingCurve() override;
  bool ShouldUseProcessPerSite(content::BrowserContext* browser_context,
                               const GURL& site_url) override;
  bool ShouldUseSpareRenderProcessHost(content::BrowserContext* browser_context,
                                       const GURL& site_url) override;
  bool DoesSiteRequireDedicatedProcess(content::BrowserContext* browser_context,
                                       const GURL& effective_site_url) override;
  bool DoesWebUISchemeRequireProcessLock(base::StringPiece scheme) override;
  bool ShouldTreatURLSchemeAsFirstPartyWhenTopLevel(
      base::StringPiece scheme,
      bool is_embedded_origin_secure) override;
  bool ShouldIgnoreSameSiteCookieRestrictionsWhenTopLevel(
      base::StringPiece scheme,
      bool is_embedded_origin_secure) override;
  std::string GetSiteDisplayNameForCdmProcess(
      content::BrowserContext* browser_context,
      const GURL& site_url) override;
  void OverrideURLLoaderFactoryParams(
      content::BrowserContext* browser_context,
      const url::Origin& origin,
      bool is_for_isolated_world,
      network::mojom::URLLoaderFactoryParams* factory_params) override;
  void GetAdditionalWebUISchemes(
      std::vector<std::string>* additional_schemes) override;
  void GetAdditionalViewSourceSchemes(
      std::vector<std::string>* additional_schemes) override;
  network::mojom::IPAddressSpace DetermineAddressSpaceFromURL(
      const GURL& url) override;
  bool LogWebUIUrl(const GURL& web_ui_url) override;
  bool IsWebUIAllowedToMakeNetworkRequests(const url::Origin& origin) override;
  bool IsHandledURL(const GURL& url) override;
  bool HasCustomSchemeHandler(content::BrowserContext* browser_context,
                              const std::string& scheme) override;
  bool CanCommitURL(content::RenderProcessHost* process_host,
                    const GURL& url) override;
  void OverrideNavigationParams(
      content::SiteInstance* site_instance,
      ui::PageTransition* transition,
      bool* is_renderer_initiated,
      content::Referrer* referrer,
      absl::optional<url::Origin>* initiator_origin) override;
  bool ShouldStayInParentProcessForNTP(
      const GURL& url,
      content::SiteInstance* parent_site_instance) override;
  bool IsSuitableHost(content::RenderProcessHost* process_host,
                      const GURL& site_url) override;
  bool MayReuseHost(content::RenderProcessHost* process_host) override;
  size_t GetProcessCountToIgnoreForLimit() override;
  blink::ParsedPermissionsPolicy GetPermissionsPolicyForIsolatedApp(
      content::BrowserContext* browser_context,
      const url::Origin& app_origin) override;
  bool ShouldTryToUseExistingProcessHost(
      content::BrowserContext* browser_context,
      const GURL& url) override;
  bool ShouldEmbeddedFramesTryToReuseExistingProcess(
      content::RenderFrameHost* outermost_main_frame) override;
  void SiteInstanceGotProcess(content::SiteInstance* site_instance) override;
  void SiteInstanceDeleting(content::SiteInstance* site_instance) override;
  bool ShouldSwapBrowsingInstancesForNavigation(
      content::SiteInstance* site_instance,
      const GURL& current_effective_url,
      const GURL& destination_effective_url) override;
  bool ShouldIsolateErrorPage(bool in_main_frame) override;
  bool ShouldAssignSiteForURL(const GURL& url) override;
  std::vector<url::Origin> GetOriginsRequiringDedicatedProcess() override;
  bool ShouldEnableStrictSiteIsolation() override;
  bool ShouldDisableSiteIsolation(
      content::SiteIsolationMode site_isolation_mode) override;
  std::vector<std::string> GetAdditionalSiteIsolationModes() override;
  void PersistIsolatedOrigin(
      content::BrowserContext* context,
      const url::Origin& origin,
      content::ChildProcessSecurityPolicy::IsolatedOriginSource source)
      override;
  bool ShouldUrlUseApplicationIsolationLevel(
      content::BrowserContext* browser_context,
      const GURL& url) override;
  bool IsIsolatedAppsDeveloperModeAllowed(
      content::BrowserContext* context) override;
  bool IsGetDisplayMediaSetSelectAllScreensAllowed(
      content::BrowserContext* context,
      const url::Origin& origin) override;
  bool IsFileAccessAllowed(const base::FilePath& path,
                           const base::FilePath& absolute_path,
                           const base::FilePath& profile_path) override;
  void AppendExtraCommandLineSwitches(base::CommandLine* command_line,
                                      int child_process_id) override;
  std::string GetApplicationClientGUIDForQuarantineCheck() override;
  download::QuarantineConnectionCallback GetQuarantineConnectionCallback()
      override;
  std::string GetApplicationLocale() override;
  std::string GetAcceptLangs(content::BrowserContext* context) override;
  gfx::ImageSkia GetDefaultFavicon() override;
  bool IsDataSaverEnabled(content::BrowserContext* context) override;
  void UpdateRendererPreferencesForWorker(
      content::BrowserContext* browser_context,
      blink::RendererPreferences* out_prefs) override;
  content::AllowServiceWorkerResult AllowServiceWorker(
      const GURL& scope,
      const net::SiteForCookies& site_for_cookies,
      const absl::optional<url::Origin>& top_frame_origin,
      const GURL& script_url,
      content::BrowserContext* context) override;
  void UpdateEnabledBlinkRuntimeFeaturesInIsolatedWorker(
      content::BrowserContext* context,
      const GURL& script_url,
      std::vector<std::string>& out_forced_enabled_runtime_features) override;
  bool AllowSharedWorker(const GURL& worker_url,
                         const net::SiteForCookies& site_for_cookies,
                         const absl::optional<url::Origin>& top_frame_origin,
                         const std::string& name,
                         const blink::StorageKey& storage_key,
                         content::BrowserContext* context,
                         int render_process_id,
                         int render_frame_id) override;
  bool DoesSchemeAllowCrossOriginSharedWorker(
      const std::string& scheme) override;
  bool AllowSignedExchange(content::BrowserContext* browser_context) override;
  void RequestFilesAccess(
      const std::vector<base::FilePath>& files,
      const GURL& destination_url,
      base::OnceCallback<void(file_access::ScopedFileAccess)>
          continuation_callback) override;
  void AllowWorkerFileSystem(
      const GURL& url,
      content::BrowserContext* browser_context,
      const std::vector<content::GlobalRenderFrameHostId>& render_frames,
      base::OnceCallback<void(bool)> callback) override;
  bool AllowWorkerIndexedDB(const GURL& url,
                            content::BrowserContext* browser_context,
                            const std::vector<content::GlobalRenderFrameHostId>&
                                render_frames) override;
  bool AllowWorkerCacheStorage(
      const GURL& url,
      content::BrowserContext* browser_context,
      const std::vector<content::GlobalRenderFrameHostId>& render_frames)
      override;
  bool AllowWorkerWebLocks(const GURL& url,
                           content::BrowserContext* browser_context,
                           const std::vector<content::GlobalRenderFrameHostId>&
                               render_frames) override;
  AllowWebBluetoothResult AllowWebBluetooth(
      content::BrowserContext* browser_context,
      const url::Origin& requesting_origin,
      const url::Origin& embedding_origin) override;
  std::string GetWebBluetoothBlocklist() override;
  bool IsInterestGroupAPIAllowed(content::RenderFrameHost* render_frame_host,
                                 InterestGroupApiOperation operation,
                                 const url::Origin& top_frame_origin,
                                 const url::Origin& api_origin) override;
  bool IsConversionMeasurementOperationAllowed(
      content::BrowserContext* browser_context,
      ConversionMeasurementOperation operation,
      const url::Origin* impression_origin,
      const url::Origin* conversion_origin,
      const url::Origin* reporting_origin) override;
  bool IsSharedStorageAllowed(content::BrowserContext* browser_context,
                              const url::Origin& top_frame_origin,
                              const url::Origin& accessing_origin) override;
#if BUILDFLAG(IS_CHROMEOS)
  void OnTrustAnchorUsed(content::BrowserContext* browser_context) override;
#endif
  bool CanSendSCTAuditingReport(
      content::BrowserContext* browser_context) override;
  void OnNewSCTAuditingReportSent(
      content::BrowserContext* browser_context) override;
  scoped_refptr<network::SharedURLLoaderFactory>
  GetSystemSharedURLLoaderFactory() override;
  network::mojom::NetworkContext* GetSystemNetworkContext() override;
  std::string GetGeolocationApiKey() override;
  device::GeolocationManager* GetGeolocationManager() override;

#if BUILDFLAG(IS_ANDROID)
  bool ShouldUseGmsCoreGeolocationProvider() override;
#endif
  scoped_refptr<content::QuotaPermissionContext> CreateQuotaPermissionContext()
      override;
  content::GeneratedCodeCacheSettings GetGeneratedCodeCacheSettings(
      content::BrowserContext* context) override;
  void AllowCertificateError(
      content::WebContents* web_contents,
      int cert_error,
      const net::SSLInfo& ssl_info,
      const GURL& request_url,
      bool is_primary_main_frame_request,
      bool strict_enforcement,
      base::OnceCallback<void(content::CertificateRequestResultType)> callback)
      override;
#if !BUILDFLAG(IS_ANDROID)
  bool ShouldDenyRequestOnCertificateError(const GURL main_page_url) override;
#endif
  base::OnceClosure SelectClientCertificate(
      content::WebContents* web_contents,
      net::SSLCertRequestInfo* cert_request_info,
      net::ClientCertIdentityList client_certs,
      std::unique_ptr<content::ClientCertificateDelegate> delegate) override;
  content::MediaObserver* GetMediaObserver() override;
  content::FeatureObserverClient* GetFeatureObserverClient() override;
  bool CanCreateWindow(content::RenderFrameHost* opener,
                       const GURL& opener_url,
                       const GURL& opener_top_level_frame_url,
                       const url::Origin& source_origin,
                       content::mojom::WindowContainerType container_type,
                       const GURL& target_url,
                       const content::Referrer& referrer,
                       const std::string& frame_name,
                       WindowOpenDisposition disposition,
                       const blink::mojom::WindowFeatures& features,
                       bool user_gesture,
                       bool opener_suppressed,
                       bool* no_javascript_access) override;
  content::SpeechRecognitionManagerDelegate*
  CreateSpeechRecognitionManagerDelegate() override;
#if BUILDFLAG(IS_CHROMEOS_ASH)
  content::TtsControllerDelegate* GetTtsControllerDelegate() override;
#endif
  void MaybeOverrideManifest(content::RenderFrameHost* render_frame_host,
                             blink::mojom::ManifestPtr& manifest) override;
  content::TtsPlatform* GetTtsPlatform() override;
  void OverrideWebkitPrefs(content::WebContents* web_contents,
                           blink::web_pref::WebPreferences* prefs) override;
  bool OverrideWebPreferencesAfterNavigation(
      content::WebContents* web_contents,
      blink::web_pref::WebPreferences* prefs) override;
  void BrowserURLHandlerCreated(content::BrowserURLHandler* handler) override;
  base::FilePath GetDefaultDownloadDirectory() override;
  std::string GetDefaultDownloadName() override;
  base::FilePath GetFontLookupTableCacheDir() override;
  base::FilePath GetShaderDiskCacheDirectory() override;
  base::FilePath GetGrShaderDiskCacheDirectory() override;
  base::FilePath GetNetLogDefaultDirectory() override;
  base::FilePath GetFirstPartySetsDirectory() override;
  void DidCreatePpapiPlugin(content::BrowserPpapiHost* browser_host) override;
  content::BrowserPpapiHost* GetExternalBrowserPpapiHost(
      int plugin_process_id) override;
  bool AllowPepperSocketAPI(
      content::BrowserContext* browser_context,
      const GURL& url,
      bool private_api,
      const content::SocketPermissionRequest* params) override;
  bool IsPepperVpnProviderAPIAllowed(content::BrowserContext* browser_context,
                                     const GURL& url) override;
  std::unique_ptr<content::VpnServiceProxy> GetVpnServiceProxy(
      content::BrowserContext* browser_context) override;
  std::unique_ptr<ui::SelectFilePolicy> CreateSelectFilePolicy(
      content::WebContents* web_contents) override;
  void GetAdditionalAllowedSchemesForFileSystem(
      std::vector<std::string>* additional_schemes) override;
  void GetSchemesBypassingSecureContextCheckAllowlist(
      std::set<std::string>* schemes) override;
  void GetURLRequestAutoMountHandlers(
      std::vector<storage::URLRequestAutoMountHandler>* handlers) override;
  void GetAdditionalFileSystemBackends(
      content::BrowserContext* browser_context,
      const base::FilePath& storage_partition_path,
      std::vector<std::unique_ptr<storage::FileSystemBackend>>*
          additional_backends) override;
  std::unique_ptr<content::DevToolsManagerDelegate>
  CreateDevToolsManagerDelegate() override;
  void UpdateDevToolsBackgroundServiceExpiration(
      content::BrowserContext* browser_context,
      int service,
      base::Time expiration_time) override;
  base::flat_map<int, base::Time> GetDevToolsBackgroundServiceExpirations(
      content::BrowserContext* browser_context) override;
  content::TracingDelegate* GetTracingDelegate() override;
  bool IsPluginAllowedToCallRequestOSFileHandle(
      content::BrowserContext* browser_context,
      const GURL& url) override;
  bool IsPluginAllowedToUseDevChannelAPIs(
      content::BrowserContext* browser_context,
      const GURL& url) override;
  void OverridePageVisibilityState(
      content::RenderFrameHost* render_frame_host,
      content::PageVisibilityState* visibility_state) override;
#if BUILDFLAG(IS_POSIX) && !BUILDFLAG(IS_MAC)
  void GetAdditionalMappedFilesForChildProcess(
      const base::CommandLine& command_line,
      int child_process_id,
      content::PosixFileDescriptorInfo* mappings) override;
#endif  // BUILDFLAG(IS_POSIX) && !BUILDFLAG(IS_MAC)
#if BUILDFLAG(IS_WIN)
  bool PreSpawnChild(sandbox::TargetPolicy* policy,
                     sandbox::mojom::Sandbox sandbox_type,
                     ChildSpawnFlags flags) override;
  std::wstring GetAppContainerSidForSandboxType(
      sandbox::mojom::Sandbox sandbox_type,
      AppContainerFlags flags) override;
  bool IsRendererAppContainerDisabled() override;
  std::wstring GetLPACCapabilityNameForNetworkService() override;
  bool IsUtilityCetCompatible(const std::string& utility_sub_type) override;
  bool IsRendererCodeIntegrityEnabled() override;
  void SessionEnding() override;
  bool ShouldEnableAudioProcessHighPriority() override;
#endif
  void ExposeInterfacesToRenderer(
      service_manager::BinderRegistry* registry,
      blink::AssociatedInterfaceRegistry* associated_registry,
      content::RenderProcessHost* render_process_host) override;
  void BindMediaServiceReceiver(content::RenderFrameHost* render_frame_host,
                                mojo::GenericPendingReceiver receiver) override;
  void RegisterBrowserInterfaceBindersForFrame(
      content::RenderFrameHost* render_frame_host,
      mojo::BinderMapWithContext<content::RenderFrameHost*>* map) override;
  void RegisterWebUIInterfaceBrokers(
      content::WebUIBrowserInterfaceBrokerRegistry& registry) override;
  void RegisterMojoBinderPoliciesForSameOriginPrerendering(
      content::MojoBinderPolicyMap& policy_map) override;
  void RegisterBrowserInterfaceBindersForServiceWorker(
      content::BrowserContext* browser_context,
      mojo::BinderMapWithContext<const content::ServiceWorkerVersionBaseInfo&>*
          map) override;
  void RegisterAssociatedInterfaceBindersForRenderFrameHost(
      content::RenderFrameHost& render_frame_host,
      blink::AssociatedInterfaceRegistry& associated_registry) override;
  void BindGpuHostReceiver(mojo::GenericPendingReceiver receiver) override;
  void BindUtilityHostReceiver(mojo::GenericPendingReceiver receiver) override;
  void BindHostReceiverForRenderer(
      content::RenderProcessHost* render_process_host,
      mojo::GenericPendingReceiver receiver) override;
  void OpenURL(
      content::SiteInstance* site_instance,
      const content::OpenURLParams& params,
      base::OnceCallback<void(content::WebContents*)> callback) override;
  content::ControllerPresentationServiceDelegate*
  GetControllerPresentationServiceDelegate(
      content::WebContents* web_contents) override;
  content::ReceiverPresentationServiceDelegate*
  GetReceiverPresentationServiceDelegate(
      content::WebContents* web_contents) override;
  std::vector<std::unique_ptr<content::NavigationThrottle>>
  CreateThrottlesForNavigation(content::NavigationHandle* handle) override;
  std::vector<std::unique_ptr<content::CommitDeferringCondition>>
  CreateCommitDeferringConditionsForNavigation(
      content::NavigationHandle* navigation_handle,
      content::CommitDeferringCondition::NavigationType type) override;
  std::unique_ptr<content::NavigationUIData> GetNavigationUIData(
      content::NavigationHandle* navigation_handle) override;
  std::unique_ptr<media::ScreenEnumerator> CreateScreenEnumerator()
      const override;
#if BUILDFLAG(ENABLE_MEDIA_REMOTING)
  void CreateMediaRemoter(
      content::RenderFrameHost* render_frame_host,
      mojo::PendingRemote<media::mojom::RemotingSource> source,
      mojo::PendingReceiver<media::mojom::Remoter> receiver) final;
#endif  // BUILDFLAG(ENABLE_MEDIA_REMOTING)
  base::FilePath GetLoggingFileName(
      const base::CommandLine& command_line) override;
  std::vector<std::unique_ptr<blink::URLLoaderThrottle>>
  CreateURLLoaderThrottles(
      const network::ResourceRequest& request,
      content::BrowserContext* browser_context,
      const base::RepeatingCallback<content::WebContents*()>& wc_getter,
      content::NavigationUIData* navigation_ui_data,
      int frame_tree_node_id) override;
  void RegisterNonNetworkNavigationURLLoaderFactories(
      int frame_tree_node_id,
      ukm::SourceIdObj ukm_source_id,
      NonNetworkURLLoaderFactoryMap* factories) override;
  void RegisterNonNetworkWorkerMainResourceURLLoaderFactories(
      content::BrowserContext* browser_context,
      NonNetworkURLLoaderFactoryMap* factories) override;
  void RegisterNonNetworkServiceWorkerUpdateURLLoaderFactories(
      content::BrowserContext* browser_context,
      NonNetworkURLLoaderFactoryMap* factories) override;
  void RegisterNonNetworkSubresourceURLLoaderFactories(
      int render_process_id,
      int render_frame_id,
      const absl::optional<url::Origin>& request_initiator_origin,
      NonNetworkURLLoaderFactoryMap* factories) override;
  bool WillCreateURLLoaderFactory(
      content::BrowserContext* browser_context,
      content::RenderFrameHost* frame,
      int render_process_id,
      URLLoaderFactoryType type,
      const url::Origin& request_initiator,
      absl::optional<int64_t> navigation_id,
      ukm::SourceIdObj ukm_source_id,
      mojo::PendingReceiver<network::mojom::URLLoaderFactory>* factory_receiver,
      mojo::PendingRemote<network::mojom::TrustedURLLoaderHeaderClient>*
          header_client,
      bool* bypass_redirect_checks,
      bool* disable_secure_dns,
      network::mojom::URLLoaderFactoryOverridePtr* factory_override) override;
  std::vector<std::unique_ptr<content::URLLoaderRequestInterceptor>>
  WillCreateURLLoaderRequestInterceptors(
      content::NavigationUIData* navigation_ui_data,
      int frame_tree_node_id,
      const scoped_refptr<network::SharedURLLoaderFactory>&
          network_loader_factory) override;
  content::ContentBrowserClient::URLLoaderRequestHandler
  CreateURLLoaderHandlerForServiceWorkerNavigationPreload(
      int frame_tree_node_id,
      const network::ResourceRequest& resource_request) override;
  bool WillInterceptWebSocket(content::RenderFrameHost* frame) override;
  void CreateWebSocket(
      content::RenderFrameHost* frame,
      WebSocketFactory factory,
      const GURL& url,
      const net::SiteForCookies& site_for_cookies,
      const absl::optional<std::string>& user_agent,
      mojo::PendingRemote<network::mojom::WebSocketHandshakeClient>
          handshake_client) override;
  void WillCreateWebTransport(
      int process_id,
      int frame_routing_id,
      const GURL& url,
      const url::Origin& initiator_origin,
      mojo::PendingRemote<network::mojom::WebTransportHandshakeClient>
          handshake_client,
      WillCreateWebTransportCallback callback) override;

  bool WillCreateRestrictedCookieManager(
      network::mojom::RestrictedCookieManagerRole role,
      content::BrowserContext* browser_context,
      const url::Origin& origin,
      const net::IsolationInfo& isolation_info,
      bool is_service_worker,
      int process_id,
      int routing_id,
      mojo::PendingReceiver<network::mojom::RestrictedCookieManager>* receiver)
      override;
  void OnNetworkServiceCreated(
      network::mojom::NetworkService* network_service) override;
  void ConfigureNetworkContextParams(
      content::BrowserContext* context,
      bool in_memory,
      const base::FilePath& relative_partition_path,
      network::mojom::NetworkContextParams* network_context_params,
      cert_verifier::mojom::CertVerifierCreationParams*
          cert_verifier_creation_params) override;
  std::vector<base::FilePath> GetNetworkContextsParentDirectory() override;
  base::Value::Dict GetNetLogConstants() override;
  bool AllowRenderingMhtmlOverHttp(
      content::NavigationUIData* navigation_ui_data) override;
  bool ShouldForceDownloadResource(const GURL& url,
                                   const std::string& mime_type) override;
  void CreateWebUsbService(
      content::RenderFrameHost* render_frame_host,
      mojo::PendingReceiver<blink::mojom::WebUsbService> receiver) override;
  content::BluetoothDelegate* GetBluetoothDelegate() override;
#if !BUILDFLAG(IS_ANDROID)
  void CreateDeviceInfoService(
      content::RenderFrameHost* render_frame_host,
      mojo::PendingReceiver<blink::mojom::DeviceAPIService> receiver) override;
  void CreateManagedConfigurationService(
      content::RenderFrameHost* render_frame_host,
      mojo::PendingReceiver<blink::mojom::ManagedConfigurationService> receiver)
      override;
  content::SerialDelegate* GetSerialDelegate() override;
  content::HidDelegate* GetHidDelegate() override;
  content::WebAuthenticationDelegate* GetWebAuthenticationDelegate() override;
  std::unique_ptr<content::AuthenticatorRequestClientDelegate>
  GetWebAuthenticationRequestDelegate(
      content::RenderFrameHost* render_frame_host) override;
#endif
  bool ShowPaymentHandlerWindow(
      content::BrowserContext* browser_context,
      const GURL& url,
      base::OnceCallback<void(bool, int, int)> callback) override;
  std::unique_ptr<net::ClientCertStore> CreateClientCertStore(
      content::BrowserContext* browser_context) override;
  std::unique_ptr<content::LoginDelegate> CreateLoginDelegate(
      const net::AuthChallengeInfo& auth_info,
      content::WebContents* web_contents,
      const content::GlobalRequestID& request_id,
      bool is_request_for_primary_main_frame,
      const GURL& url,
      scoped_refptr<net::HttpResponseHeaders> response_headers,
      bool first_auth_attempt,
      LoginAuthRequiredCallback auth_required_callback) override;
  bool HandleExternalProtocol(
      const GURL& url,
      content::WebContents::Getter web_contents_getter,
      int frame_tree_node_id,
      content::NavigationUIData* navigation_data,
      bool is_primary_main_frame,
      bool is_in_fenced_frame_tree,
      network::mojom::WebSandboxFlags sandbox_flags,
      ui::PageTransition page_transition,
      bool has_user_gesture,
      const absl::optional<url::Origin>& initiating_origin,
      content::RenderFrameHost* initiator_document,
      mojo::PendingRemote<network::mojom::URLLoaderFactory>* out_factory)
      override;
  std::unique_ptr<content::VideoOverlayWindow>
  CreateWindowForVideoPictureInPicture(
      content::VideoPictureInPictureWindowController* controller) override;
  std::unique_ptr<content::DocumentOverlayWindow>
  CreateWindowForDocumentPictureInPicture(
      content::DocumentPictureInPictureWindowController* controller) override;
  void RegisterRendererPreferenceWatcher(
      content::BrowserContext* browser_context,
      mojo::PendingRemote<blink::mojom::RendererPreferenceWatcher> watcher)
      override;
  bool CanAcceptUntrustedExchangesIfNeeded() override;
  void OnNetworkServiceDataUseUpdate(
      content::GlobalRenderFrameHostId render_frame_host_id,
      int32_t network_traffic_annotation_id_hash,
      int64_t recv_bytes,
      int64_t sent_bytes) override;
  base::FilePath GetSandboxedStorageServiceDataDirectory() override;
  bool ShouldSandboxAudioService() override;
  bool ShouldSandboxNetworkService() override;

  void LogWebFeatureForCurrentPage(content::RenderFrameHost* render_frame_host,
                                   blink::mojom::WebFeature feature) override;

  std::string GetProduct() override;
  std::string GetUserAgent() override;
  std::string GetUserAgentBasedOnPolicy(
      content::BrowserContext* context) override;
  std::string GetFullUserAgent() override;
  std::string GetReducedUserAgent() override;
  blink::UserAgentMetadata GetUserAgentMetadata() override;

  absl::optional<gfx::ImageSkia> GetProductLogo() override;

  bool IsBuiltinComponent(content::BrowserContext* browser_context,
                          const url::Origin& origin) override;

  bool ShouldBlockRendererDebugURL(const GURL& url,
                                   content::BrowserContext* context) override;

  ui::AXMode GetAXModeForBrowserContext(
      content::BrowserContext* browser_context) override;

#if BUILDFLAG(IS_ANDROID)
  ContentBrowserClient::WideColorGamutHeuristic GetWideColorGamutHeuristic()
      override;
#endif

  base::flat_set<std::string> GetPluginMimeTypesWithExternalHandlers(
      content::BrowserContext* browser_context) override;

  void AugmentNavigationDownloadPolicy(
      content::RenderFrameHost* frame_host,
      bool user_gesture,
      blink::NavigationDownloadPolicy* download_policy) override;

  std::vector<blink::mojom::EpochTopicPtr> GetBrowsingTopicsForJsApi(
      const url::Origin& context_origin,
      content::RenderFrameHost* main_frame) override;

  bool IsBluetoothScanningBlocked(content::BrowserContext* browser_context,
                                  const url::Origin& requesting_origin,
                                  const url::Origin& embedding_origin) override;

  void BlockBluetoothScanning(content::BrowserContext* browser_context,
                              const url::Origin& requesting_origin,
                              const url::Origin& embedding_origin) override;

  bool ArePersistentMediaDeviceIDsAllowed(
      content::BrowserContext* browser_context,
      const GURL& scope,
      const net::SiteForCookies& site_for_cookies,
      const absl::optional<url::Origin>& top_frame_origin) override;

#if !BUILDFLAG(IS_ANDROID)
  base::OnceClosure FetchRemoteSms(
      content::WebContents* web_contents,
      const std::vector<url::Origin>& origin_list,
      base::OnceCallback<void(absl::optional<std::vector<url::Origin>>,
                              absl::optional<std::string>,
                              absl::optional<content::SmsFetchFailureType>)>
          callback) override;
#endif

  bool IsClipboardPasteAllowed(
      content::RenderFrameHost* render_frame_host) override;

  void IsClipboardPasteContentAllowed(
      content::WebContents* web_contents,
      const GURL& url,
      const ui::ClipboardFormatType& data_type,
      const std::string& data,
      IsClipboardPasteContentAllowedCallback callback) override;

  bool IsClipboardCopyAllowed(content::BrowserContext* browser_context,
                              const GURL& url,
                              size_t data_size_in_bytes,
                              std::u16string& replacement_data) override;

#if BUILDFLAG(ENABLE_VR)
  content::XrIntegrationClient* GetXrIntegrationClient() override;
#endif

  void BindBrowserControlInterface(mojo::ScopedMessagePipeHandle pipe) override;
  bool ShouldInheritCrossOriginEmbedderPolicyImplicitly(
      const GURL& url) override;
  bool ShouldAllowInsecurePrivateNetworkRequests(
      content::BrowserContext* browser_context,
      const url::Origin& origin) override;
  bool IsJitDisabledForSite(content::BrowserContext* browser_context,
                            const GURL& site_url) override;
  ukm::UkmService* GetUkmService() override;

  void OnKeepaliveRequestStarted(
      content::BrowserContext* browser_context) override;
  void OnKeepaliveRequestFinished() override;

#if BUILDFLAG(IS_MAC)
  bool SetupEmbedderSandboxParameters(
      sandbox::mojom::Sandbox sandbox_type,
      sandbox::SeatbeltExecClient* client) override;
#endif  // BUILDFLAG(IS_MAC)

  void GetHyphenationDictionary(
      base::OnceCallback<void(const base::FilePath&)>) override;
  bool HasErrorPage(int http_status_code) override;

  StartupData* startup_data() { return &startup_data_; }

  std::unique_ptr<content::IdentityRequestDialogController>
  CreateIdentityRequestDialogController() override;

#if !BUILDFLAG(IS_ANDROID)
  base::TimeDelta GetKeepaliveTimerTimeout(content::BrowserContext* context);
#endif  // !BUILDFLAG(IS_ANDROID)

  bool SuppressDifferentOriginSubframeJSDialogs(
      content::BrowserContext* browser_context) override;

  std::unique_ptr<content::SpeculationHostDelegate>
  CreateSpeculationHostDelegate(
      content::RenderFrameHost& render_frame_host) override;

  std::unique_ptr<content::PrefetchServiceDelegate>
  CreatePrefetchServiceDelegate(
      content::BrowserContext* browser_context) override;

  void OnWebContentsCreated(content::WebContents* web_contents) override;

  bool IsFindInPageDisabledForOrigin(const url::Origin& origin) override;
  bool WillProvidePublicFirstPartySets() override;
  base::Value::Dict GetFirstPartySetsOverrides() override;

  bool ShouldPreconnectNavigation(
      content::BrowserContext* browser_context) override;

  bool ShouldDisableOriginAgentClusterDefault(
      content::BrowserContext* browser_context) override;

  content::mojom::AlternativeErrorPageOverrideInfoPtr
  GetAlternativeErrorPageOverrideInfo(
      const GURL& url,
      content::RenderFrameHost* render_frame_host,
      content::BrowserContext* browser_context,
      int32_t error_code) override;

  bool OpenExternally(content::RenderFrameHost* opener,
                      const GURL& url,
                      WindowOpenDisposition disposition) override;

 protected:
  static bool HandleWebUI(GURL* url, content::BrowserContext* browser_context);
  static bool HandleWebUIReverse(GURL* url,
                                 content::BrowserContext* browser_context);
  virtual const ui::NativeTheme* GetWebTheme() const;  // For testing.

  // Used by subclasses (e.g. implemented by downstream embedders) to add
  // their own extra part objects.
  void AddExtraPart(ChromeContentBrowserClientParts* part) {
    extra_parts_.push_back(part);
  }

 private:
  friend class DisableWebRtcEncryptionFlagTest;
  friend class InProcessBrowserTest;

  // Initializes `network_contexts_parent_directory_` and
  // `safe_browsing_service_` on the UI thread.
  void InitOnUIThread();

  // Copies disable WebRTC encryption switch depending on the channel.
  static void MaybeCopyDisableWebRtcEncryptionSwitch(
      base::CommandLine* to_command_line,
      const base::CommandLine& from_command_line,
      version_info::Channel channel);

  void FileSystemAccessed(
      const GURL& url,
      const std::vector<content::GlobalRenderFrameHostId>& render_frames,
      base::OnceCallback<void(bool)> callback,
      bool allow);

#if BUILDFLAG(ENABLE_EXTENSIONS)
  void GuestPermissionRequestHelper(
      const GURL& url,
      const std::vector<content::GlobalRenderFrameHostId>& render_frames,
      base::OnceCallback<void(bool)> callback,
      bool allow);
#endif

  // Returns the existing UrlCheckerDelegate object if it is already created.
  // Otherwise, creates a new one and returns it. Updates the
  // |allowlist_domains| in the UrlCheckerDelegate object before returning. It
  // returns nullptr if |safe_browsing_enabled_for_profile| is false, because it
  // should bypass safe browsing check when safe browsing is disabled. Set
  // |should_check_on_sb_disabled| to true if you still want to perform safe
  // browsing check when safe browsing is disabled(e.g. for enterprise real time
  // URL check).
  scoped_refptr<safe_browsing::UrlCheckerDelegate>
  GetSafeBrowsingUrlCheckerDelegate(
      bool safe_browsing_enabled_for_profile,
      bool should_check_on_sb_disabled,
      const std::vector<std::string>& allowlist_domains);

  // Returns a RealTimeUrlLookupServiceBase object used for real time URL check.
  // Returns an enterprise version if |is_enterprise_lookup_enabled| is true.
  // Returns a consumer version if |is_enterprise_lookup_enabled| is false and
  // |is_consumer_lookup_enabled| is true. Returns nullptr if both are false.
  safe_browsing::RealTimeUrlLookupServiceBase* GetUrlLookupService(
      content::BrowserContext* browser_context,
      bool is_enterprise_lookup_enabled,
      bool is_consumer_lookup_enabled);

  void SafeBrowsingWebApiHandshakeChecked(
      std::unique_ptr<safe_browsing::WebApiHandshakeChecker> checker,
      int process_id,
      int frame_routing_id,
      const GURL& url,
      const url::Origin& initiator_origin,
      mojo::PendingRemote<network::mojom::WebTransportHandshakeClient>
          handshake_client,
      WillCreateWebTransportCallback callback,
      safe_browsing::WebApiHandshakeChecker::CheckResult result);
  void MaybeInterceptWebTransport(
      int process_id,
      int frame_routing_id,
      const GURL& url,
      const url::Origin& initiator_origin,
      mojo::PendingRemote<network::mojom::WebTransportHandshakeClient>
          handshake_client,
      WillCreateWebTransportCallback callback);

#if !BUILDFLAG(IS_ANDROID)
  void OnKeepaliveTimerFired(
      std::unique_ptr<ScopedKeepAlive> keep_alive_handle);
#endif

  // Vector of additional ChromeContentBrowserClientParts.
  // Parts are deleted in the reverse order they are added.
  std::vector<ChromeContentBrowserClientParts*> extra_parts_;

  scoped_refptr<safe_browsing::SafeBrowsingService> safe_browsing_service_;
  scoped_refptr<safe_browsing::UrlCheckerDelegate>
      safe_browsing_url_checker_delegate_;

  StartupData startup_data_;

#if !BUILDFLAG(IS_ANDROID)
  std::unique_ptr<ChromeSerialDelegate> serial_delegate_;
  std::unique_ptr<ChromeHidDelegate> hid_delegate_;
  std::unique_ptr<ChromeWebAuthenticationDelegate> web_authentication_delegate_;
#endif
  std::unique_ptr<permissions::BluetoothDelegateImpl> bluetooth_delegate_;

#if BUILDFLAG(ENABLE_VR)
  std::unique_ptr<vr::ChromeXrIntegrationClient> xr_integration_client_;
#endif

  // Returned from GetNetworkContextsParentDirectory() but created on the UI
  // thread because it needs to access the Local State prefs.
  std::vector<base::FilePath> network_contexts_parent_directory_;

#if !BUILDFLAG(IS_ANDROID)
  uint64_t num_keepalive_requests_ = 0;
  base::OneShotTimer keepalive_timer_;
  base::TimeTicks keepalive_deadline_;
#endif

  base::WeakPtrFactory<ChromeContentBrowserClient> weak_factory_{this};
};

#endif  // CHROME_BROWSER_CHROME_CONTENT_BROWSER_CLIENT_H_
