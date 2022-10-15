// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "extensions/browser/extension_navigation_throttle.h"

#include <string>

#include "components/guest_view/browser/guest_view_base.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/storage_partition_config.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/url_constants.h"
#include "extensions/browser/app_window/app_window.h"
#include "extensions/browser/app_window/app_window_registry.h"
#include "extensions/browser/extension_host.h"
#include "extensions/browser/extension_host_registry.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/extensions_browser_client.h"
#include "extensions/browser/guest_view/app_view/app_view_guest.h"
#include "extensions/browser/guest_view/mime_handler_view/mime_handler_view_embedder.h"
#include "extensions/browser/guest_view/web_view/web_view_guest.h"
#include "extensions/browser/url_request_util.h"
#include "extensions/browser/view_type_utils.h"
#include "extensions/common/constants.h"
#include "extensions/common/extension.h"
#include "extensions/common/extension_set.h"
#include "extensions/common/identifiability_metrics.h"
#include "extensions/common/manifest_handlers/icons_handler.h"
#include "extensions/common/manifest_handlers/web_accessible_resources_info.h"
#include "extensions/common/manifest_handlers/webview_info.h"
#include "extensions/common/mojom/view_type.mojom.h"
#include "extensions/common/permissions/api_permission.h"
#include "extensions/common/permissions/permissions_data.h"
#include "services/metrics/public/cpp/ukm_source_id.h"
#include "services/network/public/cpp/web_sandbox_flags.h"
#include "ui/base/page_transition_types.h"

namespace extensions {

namespace {

// Whether a navigation to the |platform_app| resource should be blocked in the
// given |web_contents|.
bool ShouldBlockNavigationToPlatformAppResource(
    const Extension* platform_app,
    content::NavigationHandle& navigation_handle) {
  content::WebContents* web_contents = navigation_handle.GetWebContents();
  mojom::ViewType view_type = GetViewType(web_contents);
  DCHECK_NE(mojom::ViewType::kInvalid, view_type);

  // Navigation to platform app's background page.
  if (view_type == mojom::ViewType::kExtensionBackgroundPage)
    return false;

  // Navigation within an extension dialog, e.g. this is used by ChromeOS file
  // manager.
  if (view_type == mojom::ViewType::kExtensionDialog)
    return false;

  // Navigation within an app window. The app window must belong to the
  // |platform_app|.
  if (view_type == mojom::ViewType::kAppWindow) {
    AppWindowRegistry* registry =
        AppWindowRegistry::Get(web_contents->GetBrowserContext());
    DCHECK(registry);
    AppWindow* app_window = registry->GetAppWindowForWebContents(web_contents);
    DCHECK(app_window);
    return app_window->extension_id() != platform_app->id();
  }

  // Navigation within a guest web contents.
  if (view_type == mojom::ViewType::kExtensionGuest) {
    // Navigating within a PDF viewer extension (see crbug.com/1252154). This
    // exemption is only for the PDF resource. The initial navigation to the PDF
    // loads the PDF viewer extension, which would have already passed the
    // checks in this navigation throttle.
    if (navigation_handle.IsPdf()) {
      const url::Origin& initiator_origin =
          navigation_handle.GetInitiatorOrigin().value();
      CHECK_EQ(initiator_origin.scheme(), kExtensionScheme);
      CHECK_EQ(initiator_origin.host(), extension_misc::kPdfExtensionId);
      return false;
    }

    // Platform apps can be embedded by other platform apps using an <appview>
    // tag.
    AppViewGuest* app_view = AppViewGuest::FromWebContents(web_contents);
    if (app_view)
      return false;

    // Webviews owned by the platform app can embed platform app resources via
    // "accessible_resources".
    WebViewGuest* web_view_guest = WebViewGuest::FromWebContents(web_contents);
    if (web_view_guest)
      return web_view_guest->owner_host() != platform_app->id();

    // Otherwise, it's a guest view that's neither a webview nor an appview
    // (such as an extensionoptions view). Disallow.
    return true;
  }

  DCHECK(view_type == mojom::ViewType::kBackgroundContents ||
         view_type == mojom::ViewType::kComponent ||
         view_type == mojom::ViewType::kExtensionPopup ||
         view_type == mojom::ViewType::kTabContents ||
         view_type == mojom::ViewType::kOffscreenDocument)
      << "Unhandled view type: " << view_type;

  return true;
}

}  // namespace

ExtensionNavigationThrottle::ExtensionNavigationThrottle(
    content::NavigationHandle* navigation_handle)
    : content::NavigationThrottle(navigation_handle) {}

ExtensionNavigationThrottle::~ExtensionNavigationThrottle() {}

content::NavigationThrottle::ThrottleCheckResult
ExtensionNavigationThrottle::WillStartOrRedirectRequest() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  content::WebContents* web_contents = navigation_handle()->GetWebContents();
  content::BrowserContext* browser_context = web_contents->GetBrowserContext();

  // Prevent background extension contexts from being navigated away.
  // See crbug.com/1130083.
  if (navigation_handle()->IsInPrimaryMainFrame()) {
    ExtensionHostRegistry* host_registry =
        ExtensionHostRegistry::Get(browser_context);
    DCHECK(host_registry);
    ExtensionHost* host = host_registry->GetExtensionHostForPrimaryMainFrame(
        web_contents->GetPrimaryMainFrame());

    // Navigation throttles don't intercept same document navigations, hence we
    // can ignore that case.
    DCHECK(!navigation_handle()->IsSameDocument());

    if (host && host->initial_url() != navigation_handle()->GetURL() &&
        !host->ShouldAllowNavigations()) {
      return content::NavigationThrottle::CANCEL;
    }
  }

  // Some checks below will need to know whether this navigation is in a
  // <webview> guest.
  guest_view::GuestViewBase* guest =
      guest_view::GuestViewBase::FromWebContents(web_contents);

  // Is this navigation targeting an extension resource?
  ExtensionRegistry* registry = ExtensionRegistry::Get(browser_context);
  const GURL& url = navigation_handle()->GetURL();
  bool url_has_extension_scheme = url.SchemeIs(kExtensionScheme);
  url::Origin target_origin = url::Origin::Create(url);
  const Extension* target_extension = nullptr;
  if (url_has_extension_scheme) {
    // "chrome-extension://" URL.
    target_extension = registry->enabled_extensions().GetExtensionOrAppByURL(
        url, true /*include_guid*/);
  } else if (target_origin.scheme() == kExtensionScheme) {
    // "blob:chrome-extension://" or "filesystem:chrome-extension://" URL.
    DCHECK(url.SchemeIsFileSystem() || url.SchemeIsBlob());
    target_extension =
        registry->enabled_extensions().GetByID(target_origin.host());
  } else {
    // If this navigation is in a guest, check if the URL maps to the Chrome
    // Web Store hosted app. If so, block the navigation to avoid a renderer
    // kill later, see https://crbug.com/1197674.
    if (guest) {
      const Extension* hosted_app =
          registry->enabled_extensions().GetHostedAppByURL(url);
      if (hosted_app && hosted_app->id() == kWebStoreAppId)
        return content::NavigationThrottle::BLOCK_REQUEST;
    }

    // Otherwise, the navigation is not to a chrome-extension resource, and
    // there is no need to perform any more checks; it's outside of the purview
    // of this throttle.
    return content::NavigationThrottle::PROCEED;
  }

  ukm::SourceIdObj source_id = ukm::SourceIdObj::FromInt64(
      navigation_handle()->GetNextPageUkmSourceId());

  // If the navigation is to an unknown or disabled extension, block it.
  if (!target_extension) {
    RecordExtensionResourceAccessResult(
        source_id, url, ExtensionResourceAccessResult::kFailure);
    // TODO(nick): This yields an unsatisfying error page; use a different error
    // code once that's supported. https://crbug.com/649869
    return content::NavigationThrottle::BLOCK_REQUEST;
  }

  // Hosted apps don't have any associated resources outside of icons, so
  // block any requests to URLs in their extension origin.
  if (target_extension->is_hosted_app()) {
    base::StringPiece resource_root_relative_path =
        url.path_piece().empty() ? base::StringPiece()
                                 : url.path_piece().substr(1);
    if (!IconsInfo::GetIcons(target_extension)
             .ContainsPath(resource_root_relative_path)) {
      RecordExtensionResourceAccessResult(
          source_id, url, ExtensionResourceAccessResult::kFailure);
      return content::NavigationThrottle::BLOCK_REQUEST;
    }
  }

  // Block all navigations to blob: or filesystem: URLs with extension
  // origin from non-extension processes.  See https://crbug.com/645028 and
  // https://crbug.com/836858.
  bool current_frame_is_extension_process =
      !!registry->enabled_extensions().GetExtensionOrAppByURL(
          navigation_handle()->GetStartingSiteInstance()->GetSiteURL());

  if (!url_has_extension_scheme && !current_frame_is_extension_process) {
    // Relax this restriction for apps that use <webview>.  See
    // https://crbug.com/652077.
    bool has_webview_permission =
        target_extension->permissions_data()->HasAPIPermission(
            mojom::APIPermissionID::kWebView);
    if (!has_webview_permission) {
      RecordExtensionResourceAccessResult(
          source_id, url, ExtensionResourceAccessResult::kCancel);
      return content::NavigationThrottle::CANCEL;
    }
  }

  if (url_has_extension_scheme && guest) {
    // Check whether the guest is allowed to load the extension URL. This is
    // usually allowed only for the guest's owner extension resources, and only
    // if those resources are marked as webview-accessible. This check is
    // needed for both navigations and subresources. The code below handles
    // navigations, and url_request_util::AllowCrossRendererResourceLoad()
    // handles subresources.
    const std::string& owner_extension_id = guest->owner_host();
    const Extension* owner_extension =
        registry->enabled_extensions().GetByID(owner_extension_id);

    content::StoragePartitionConfig storage_partition_config =
        content::StoragePartitionConfig::CreateDefault(browser_context);
    bool is_guest = navigation_handle()->GetStartingSiteInstance()->IsGuest();
    if (is_guest) {
      storage_partition_config = navigation_handle()
                                     ->GetStartingSiteInstance()
                                     ->GetStoragePartitionConfig();
    }
    CHECK_EQ(is_guest,
             navigation_handle()->GetStartingSiteInstance()->IsGuest());

    bool allowed = true;
    url_request_util::AllowCrossRendererResourceLoadHelper(
        is_guest, target_extension, owner_extension,
        storage_partition_config.partition_name(), url.path(),
        navigation_handle()->GetPageTransition(), &allowed);
    if (!allowed) {
      RecordExtensionResourceAccessResult(
          source_id, url, ExtensionResourceAccessResult::kFailure);
      return content::NavigationThrottle::BLOCK_REQUEST;
    }
  }

  if (target_extension->is_platform_app() &&
      ShouldBlockNavigationToPlatformAppResource(target_extension,
                                                 *navigation_handle())) {
    RecordExtensionResourceAccessResult(
        source_id, url, ExtensionResourceAccessResult::kFailure);
    return content::NavigationThrottle::BLOCK_REQUEST;
  }

  // A browser-initiated navigation is always considered trusted, and thus
  // allowed.
  if (!navigation_handle()->IsRendererInitiated())
    return content::NavigationThrottle::PROCEED;

  // A renderer-initiated request without an initiator origin is a history
  // traversal to an entry that was originally loaded in a browser-initiated
  // navigation. Those are trusted, too.
  if (!navigation_handle()->GetInitiatorOrigin().has_value())
    return content::NavigationThrottle::PROCEED;

  const url::Origin& initiator_origin =
      navigation_handle()->GetInitiatorOrigin().value();

  // Navigations from chrome://, devtools:// or chrome-search:// pages need to
  // be allowed, even if the target |url| is not web-accessible.  See also:
  // - https://crbug.com/662602
  // - similar checks in extensions::ResourceRequestPolicy::CanRequestResource
  if (initiator_origin.scheme() == content::kChromeUIScheme ||
      initiator_origin.scheme() == content::kChromeDevToolsScheme ||
      ExtensionsBrowserClient::Get()->ShouldSchemeBypassNavigationChecks(
          initiator_origin.scheme())) {
    return content::NavigationThrottle::PROCEED;
  }

  // An extension can initiate navigations to any of its resources.
  if (initiator_origin == target_origin)
    return content::NavigationThrottle::PROCEED;

  // Cancel cross-origin-initiator navigations to blob: or filesystem: URLs.
  if (!url_has_extension_scheme) {
    RecordExtensionResourceAccessResult(source_id, url,
                                        ExtensionResourceAccessResult::kCancel);
    return content::NavigationThrottle::CANCEL;
  }

  // Cross-origin-initiator navigations require that the |url| is in the
  // manifest's "web_accessible_resources" section.
  if (!WebAccessibleResourcesInfo::IsResourceWebAccessible(
          target_extension, url.path(), initiator_origin)) {
    RecordExtensionResourceAccessResult(
        source_id, url, ExtensionResourceAccessResult::kFailure);
    return content::NavigationThrottle::BLOCK_REQUEST;
  }

  // A platform app may not be loaded in an <iframe> by another origin.
  //
  // In fact, platform apps may not have any cross-origin iframes at all;
  // for non-extension origins of |url| this is enforced by means of a
  // Content Security Policy. But CSP is incapable of blocking the
  // chrome-extension scheme. Thus, this case must be handled specially
  // here.
  // TODO(karandeepb): Investigate if this check can be removed.
  if (target_extension->is_platform_app()) {
    RecordExtensionResourceAccessResult(source_id, url,
                                        ExtensionResourceAccessResult::kCancel);
    return content::NavigationThrottle::CANCEL;
  }

  // A platform app may not load another extension in an <iframe>.
  const Extension* initiator_extension =
      registry->enabled_extensions().GetExtensionOrAppByURL(
          initiator_origin.GetURL());
  if (initiator_extension && initiator_extension->is_platform_app()) {
    RecordExtensionResourceAccessResult(
        source_id, url, ExtensionResourceAccessResult::kFailure);
    return content::NavigationThrottle::BLOCK_REQUEST;
  }

  return content::NavigationThrottle::PROCEED;
}

content::NavigationThrottle::ThrottleCheckResult
ExtensionNavigationThrottle::WillStartRequest() {
  return WillStartOrRedirectRequest();
}

content::NavigationThrottle::ThrottleCheckResult
ExtensionNavigationThrottle::WillRedirectRequest() {
  return WillStartOrRedirectRequest();
}

content::NavigationThrottle::ThrottleCheckResult
ExtensionNavigationThrottle::WillProcessResponse() {
  if ((navigation_handle()->SandboxFlagsToCommit() &
       network::mojom::WebSandboxFlags::kPlugins) ==
      network::mojom::WebSandboxFlags::kNone) {
    return PROCEED;
  }

  auto* mime_handler_view_embedder =
      MimeHandlerViewEmbedder::Get(navigation_handle()->GetFrameTreeNodeId());
  if (!mime_handler_view_embedder)
    return PROCEED;

  // If we have a MimeHandlerViewEmbedder, the frame might embed a resource. If
  // the frame is sandboxed, however, we shouldn't show the embedded resource.
  // Instead, we should notify the MimeHandlerViewEmbedder (so that it will
  // delete itself) and commit an error page.
  // TODO(https://crbug.com/1144913): Currently MimeHandlerViewEmbedder is
  // created by PluginResponseInterceptorURLLoaderThrottle before the sandbox
  // flags are ready. This means in some cases we will create it and delete it
  // soon after that here. We should move MimeHandlerViewEmbedder creation to a
  // NavigationThrottle instead and check the sandbox flags before creating, so
  // that we don't have to remove it soon after creation.
  mime_handler_view_embedder->OnFrameSandboxed();
  return ThrottleCheckResult(CANCEL, net::ERR_BLOCKED_BY_CLIENT);
}

const char* ExtensionNavigationThrottle::GetNameForLogging() {
  return "ExtensionNavigationThrottle";
}

}  // namespace extensions
