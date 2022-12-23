// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/chrome_extension_web_contents_observer.h"

#include <memory>
#include <string>

#include "base/command_line.h"
#include "base/metrics/field_trial.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/extensions/chrome_extension_frame_host.h"
#include "chrome/browser/extensions/error_console/error_console.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/extensions/window_controller.h"
#include "chrome/browser/metrics/chrome_metrics_service_accessor.h"
#include "chrome/common/url_constants.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/child_process_security_policy.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/common/content_switches.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/extension_system.h"
#include "extensions/common/constants.h"
#include "extensions/common/extension_messages.h"
#include "extensions/common/extension_urls.h"
#include "extensions/common/switches.h"
#include "third_party/blink/public/common/chrome_debug_urls.h"

namespace extensions {

ChromeExtensionWebContentsObserver::ChromeExtensionWebContentsObserver(
    content::WebContents* web_contents)
    : ExtensionWebContentsObserver(web_contents),
      content::WebContentsUserData<ChromeExtensionWebContentsObserver>(
          *web_contents) {}

ChromeExtensionWebContentsObserver::~ChromeExtensionWebContentsObserver() {}

// static
void ChromeExtensionWebContentsObserver::CreateForWebContents(
    content::WebContents* web_contents) {
  content::WebContentsUserData<
      ChromeExtensionWebContentsObserver>::CreateForWebContents(web_contents);

  // Initialize this instance if necessary.
  FromWebContents(web_contents)->Initialize();
}

std::unique_ptr<ExtensionFrameHost>
ChromeExtensionWebContentsObserver::CreateExtensionFrameHost(
    content::WebContents* web_contents) {
  return std::make_unique<ChromeExtensionFrameHost>(web_contents);
}

void ChromeExtensionWebContentsObserver::RenderFrameCreated(
    content::RenderFrameHost* render_frame_host) {
  DCHECK(initialized());
  ReloadIfTerminated(render_frame_host);
  ExtensionWebContentsObserver::RenderFrameCreated(render_frame_host);

  // This logic should match
  // ChromeContentBrowserClient::RegisterNonNetworkSubresourceURLLoaderFactories.
  const Extension* extension = GetExtensionFromFrame(render_frame_host, false);
  if (!extension)
    return;

  int process_id = render_frame_host->GetProcess()->GetID();
  auto* policy = content::ChildProcessSecurityPolicy::GetInstance();

  // Components of chrome that are implemented as extensions or platform apps
  // are allowed to use chrome://resources/ and chrome://theme/ URLs.
  if ((extension->is_extension() || extension->is_platform_app()) &&
      Manifest::IsComponentLocation(extension->location())) {
    policy->GrantRequestOrigin(
        process_id, url::Origin::Create(GURL(blink::kChromeUIResourcesURL)));
    policy->GrantRequestOrigin(
        process_id, url::Origin::Create(GURL(chrome::kChromeUIThemeURL)));
  }

  // Extensions, legacy packaged apps, and component platform apps are allowed
  // to use chrome://favicon/ and chrome://extension-icon/ URLs. Hosted apps are
  // not allowed because they are served via web servers (and are generally
  // never given access to Chrome APIs).
  if (extension->is_extension() ||
      extension->is_legacy_packaged_app() ||
      (extension->is_platform_app() &&
       Manifest::IsComponentLocation(extension->location()))) {
    policy->GrantRequestOrigin(
        process_id, url::Origin::Create(GURL(chrome::kChromeUIFaviconURL)));
    policy->GrantRequestOrigin(
        process_id,
        url::Origin::Create(GURL(chrome::kChromeUIExtensionIconURL)));
  }
}

bool ChromeExtensionWebContentsObserver::OnMessageReceived(
    const IPC::Message& message,
    content::RenderFrameHost* render_frame_host) {
  DCHECK(initialized());
  if (ExtensionWebContentsObserver::OnMessageReceived(message,
                                                      render_frame_host)) {
    return true;
  }

  bool handled = true;
  IPC_BEGIN_MESSAGE_MAP_WITH_PARAM(ChromeExtensionWebContentsObserver, message,
                                   render_frame_host)
    IPC_MESSAGE_HANDLER(ExtensionHostMsg_DetailedConsoleMessageAdded,
                        OnDetailedConsoleMessageAdded)
    IPC_MESSAGE_UNHANDLED(handled = false)
  IPC_END_MESSAGE_MAP()
  return handled;
}

void ChromeExtensionWebContentsObserver::OnDetailedConsoleMessageAdded(
    content::RenderFrameHost* render_frame_host,
    const std::u16string& message,
    const std::u16string& source,
    const StackTrace& stack_trace,
    int32_t severity_level) {
  DCHECK(initialized());
  if (!IsSourceFromAnExtension(source))
    return;

  std::string extension_id = GetExtensionIdFromFrame(render_frame_host);
  if (extension_id.empty())
    extension_id = GURL(source).host();

  ErrorConsole::Get(browser_context())
      ->ReportError(std::unique_ptr<ExtensionError>(new RuntimeError(
          extension_id, browser_context()->IsOffTheRecord(), source, message,
          stack_trace, web_contents()->GetLastCommittedURL(),
          static_cast<logging::LogSeverity>(severity_level),
          render_frame_host->GetRoutingID(),
          render_frame_host->GetProcess()->GetID())));
}

void ChromeExtensionWebContentsObserver::InitializeRenderFrame(
    content::RenderFrameHost* render_frame_host) {
  DCHECK(initialized());
  ExtensionWebContentsObserver::InitializeRenderFrame(render_frame_host);
  WindowController* controller = dispatcher()->GetExtensionWindowController();
  if (controller) {
    GetLocalFrame(render_frame_host)
        ->UpdateBrowserWindowId(controller->GetWindowId());
  }
}

void ChromeExtensionWebContentsObserver::ReloadIfTerminated(
    content::RenderFrameHost* render_frame_host) {
  DCHECK(initialized());
  std::string extension_id = GetExtensionIdFromFrame(render_frame_host);
  if (extension_id.empty())
    return;

  ExtensionRegistry* registry = ExtensionRegistry::Get(browser_context());

  // Reload the extension if it has crashed.
  // TODO(yoz): This reload doesn't happen synchronously for unpacked
  //            extensions. It seems to be fast enough, but there is a race.
  //            We should delay loading until the extension has reloaded.
  if (registry->GetExtensionById(extension_id, ExtensionRegistry::TERMINATED)) {
    ExtensionSystem::Get(browser_context())->
        extension_service()->ReloadExtension(extension_id);
  }
}

WEB_CONTENTS_USER_DATA_KEY_IMPL(ChromeExtensionWebContentsObserver);

}  // namespace extensions
