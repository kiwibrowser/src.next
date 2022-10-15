// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/active_tab_permission_granter.h"

#include <set>
#include <vector>

#include "base/bind.h"
#include "base/callback_helpers.h"
#include "base/containers/contains.h"
#include "base/feature_list.h"
#include "chrome/browser/extensions/extension_action_runner.h"
#include "chrome/browser/extensions/extension_util.h"
#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/web_contents.h"
#include "extensions/browser/extension_util.h"
#include "extensions/browser/network_permissions_updater.h"
#include "extensions/browser/process_manager.h"
#include "extensions/browser/renderer_startup_helper.h"
#include "extensions/common/extension.h"
#include "extensions/common/extension_messages.h"
#include "extensions/common/manifest_handlers/incognito_info.h"
#include "extensions/common/mojom/renderer.mojom.h"
#include "extensions/common/permissions/permission_set.h"
#include "extensions/common/permissions/permissions_data.h"
#include "extensions/common/url_pattern_set.h"
#include "extensions/common/user_script.h"
#include "url/gurl.h"

namespace extensions {

namespace {

using RendererMessageFunction =
    base::RepeatingCallback<void(bool, content::RenderProcessHost*)>;

void UpdateTabSpecificPermissions(const std::string& extension_id,
                                  const extensions::URLPatternSet& new_hosts,
                                  int tab_id,
                                  bool update_origin_allowlist,
                                  content::RenderProcessHost* process) {
  mojom::Renderer* renderer =
      RendererStartupHelperFactory::GetForBrowserContext(
          process->GetBrowserContext())
          ->GetRenderer(process);
  if (renderer) {
    renderer->UpdateTabSpecificPermissions(extension_id, new_hosts.Clone(),
                                           tab_id, update_origin_allowlist);
  }
}

void ClearTabSpecificPermissions(const std::vector<std::string>& extension_ids,
                                 int tab_id,
                                 bool update_origin_allowlist,
                                 content::RenderProcessHost* process) {
  mojom::Renderer* renderer =
      RendererStartupHelperFactory::GetForBrowserContext(
          process->GetBrowserContext())
          ->GetRenderer(process);
  if (renderer) {
    renderer->ClearTabSpecificPermissions(extension_ids, tab_id,
                                          update_origin_allowlist);
  }
}

// Sends a message exactly once to each render process host owning one of the
// given |frame_hosts| and |tab_process|. If |tab_process| doesn't own any of
// the |frame_hosts|, it will not be signaled to update its origin allowlist.
void SendRendererMessageToProcesses(
    const std::set<content::RenderFrameHost*>& frame_hosts,
    content::RenderProcessHost* tab_process,
    const RendererMessageFunction& renderer_message) {
  std::set<content::RenderProcessHost*> sent_to_hosts;
  for (content::RenderFrameHost* frame_host : frame_hosts) {
    content::RenderProcessHost* process_host = frame_host->GetProcess();
    if (!base::Contains(sent_to_hosts, process_host)) {
      // Extension processes have to update the origin allowlists.
      renderer_message.Run(true, process_host);
      sent_to_hosts.insert(frame_host->GetProcess());
    }
  }
  // If the tab wasn't one of those processes already updated (it likely
  // wasn't), update it. Tabs don't need to update the origin allowlist.
  if (!base::Contains(sent_to_hosts, tab_process))
    renderer_message.Run(false, tab_process);
}

void SetCorsOriginAccessList(content::BrowserContext* browser_context,
                             const Extension& extension,
                             base::OnceClosure closure) {
  // To limit how far the new permissions reach, we only apply them to the
  // ActiveTab's context for split-mode extensions.  OTOH, spanning-mode
  // extensions need to get the new permissions in all profiles (e.g. if the
  // ActiveTab is in an incognito window, than the [single/only/spanning]
  // background page in the regular profile also needs to get the new
  // permissions).
  NetworkPermissionsUpdater::ContextSet context_set =
      IncognitoInfo::IsSplitMode(&extension)
          ? NetworkPermissionsUpdater::ContextSet::kCurrentContextOnly
          : NetworkPermissionsUpdater::ContextSet::kAllRelatedContexts;
  NetworkPermissionsUpdater::UpdateExtension(*browser_context, extension,
                                             context_set, std::move(closure));
}

}  // namespace

ActiveTabPermissionGranter::ActiveTabPermissionGranter(
    content::WebContents* web_contents,
    int tab_id,
    Profile* profile)
    : content::WebContentsObserver(web_contents), tab_id_(tab_id) {
  extension_registry_observation_.Observe(ExtensionRegistry::Get(profile));
}

ActiveTabPermissionGranter::~ActiveTabPermissionGranter() {}

void ActiveTabPermissionGranter::GrantIfRequested(const Extension* extension) {
  if (granted_extensions_.Contains(extension->id()))
    return;

  APIPermissionSet new_apis;
  URLPatternSet new_hosts;

  const PermissionsData* permissions_data = extension->permissions_data();

  const GURL& url =
      web_contents()->GetPrimaryMainFrame()->GetLastCommittedOrigin().GetURL();

  // If the extension requested the host permission to |url| but had it
  // withheld, we grant it active tab-style permissions, even if it doesn't have
  // the activeTab permission in the manifest. This is necessary for the
  // runtime host permissions feature to work.
  content::BrowserContext* browser_context =
      web_contents()->GetBrowserContext();
  if (permissions_data->HasAPIPermission(mojom::APIPermissionID::kActiveTab) ||
      permissions_data->withheld_permissions().effective_hosts().MatchesURL(
          url)) {
    // Gate activeTab for file urls on extensions having explicit access to file
    // urls.
    int valid_schemes = UserScript::ValidUserScriptSchemes();
    if (!util::AllowFileAccess(extension->id(), browser_context)) {
      valid_schemes &= ~URLPattern::SCHEME_FILE;
    }
    new_hosts.AddOrigin(valid_schemes, url);
    new_apis.insert(mojom::APIPermissionID::kTab);

    if (permissions_data->HasAPIPermission(
            mojom::APIPermissionID::kDeclarativeNetRequest) ||
        permissions_data->HasAPIPermission(
            mojom::APIPermissionID::kDeclarativeNetRequestWithHostAccess)) {
      new_apis.insert(mojom::APIPermissionID::kDeclarativeNetRequestFeedback);
    }
  }

  if (permissions_data->HasAPIPermission(mojom::APIPermissionID::kTabCapture))
    new_apis.insert(mojom::APIPermissionID::kTabCaptureForTab);

  if (!new_apis.empty() || !new_hosts.is_empty()) {
    granted_extensions_.Insert(extension);
    PermissionSet new_permissions(std::move(new_apis), ManifestPermissionSet(),
                                  new_hosts.Clone(), new_hosts.Clone());
    permissions_data->UpdateTabSpecificPermissions(tab_id_, new_permissions);
    SetCorsOriginAccessList(browser_context, *extension, base::DoNothing());

    if (web_contents()->GetController().GetVisibleEntry()) {
      // We update all extension render views with the new tab permissions, and
      // also the tab itself.
      RendererMessageFunction update_message =
          base::BindRepeating(&UpdateTabSpecificPermissions, extension->id(),
                              new_hosts.Clone(), tab_id_);
      ProcessManager* process_manager = ProcessManager::Get(browser_context);
      SendRendererMessageToProcesses(
          process_manager->GetRenderFrameHostsForExtension(extension->id()),
          web_contents()->GetPrimaryMainFrame()->GetProcess(), update_message);

      // If more things ever need to know about this, we should consider making
      // an observer class.
      // It's important that this comes after the Mojo message is sent to the
      // renderer, so that any tasks executing in the renderer occur after it
      // has the updated permissions.
      ExtensionActionRunner::GetForWebContents(web_contents())
          ->OnActiveTabPermissionGranted(extension);
    }
  }
}

void ActiveTabPermissionGranter::RevokeForTesting() {
  ClearActiveExtensionsAndNotify();
}

void ActiveTabPermissionGranter::DidFinishNavigation(
    content::NavigationHandle* navigation_handle) {
  // Important: sub-frames don't get granted!
  if (!navigation_handle->IsInPrimaryMainFrame() ||
      !navigation_handle->HasCommitted() ||
      navigation_handle->IsSameDocument()) {
    return;
  }

  // Only clear the granted permissions for cross-origin navigations.
  if (navigation_handle->IsSameOrigin())
    return;

  ClearActiveExtensionsAndNotify();
}

void ActiveTabPermissionGranter::WebContentsDestroyed() {
  ClearActiveExtensionsAndNotify();
}

void ActiveTabPermissionGranter::OnExtensionUnloaded(
    content::BrowserContext* browser_context,
    const Extension* extension,
    UnloadedExtensionReason reason) {
  // Note: don't need to clear the permissions (nor tell the renderer about it)
  // because it's being unloaded anyway.
  granted_extensions_.Remove(extension->id());
}

void ActiveTabPermissionGranter::ClearActiveExtensionsAndNotify() {
  if (granted_extensions_.is_empty())
    return;

  std::set<content::RenderFrameHost*> frame_hosts;
  std::vector<std::string> extension_ids;
  content::BrowserContext* browser_context =
      web_contents()->GetBrowserContext();
  ProcessManager* process_manager = ProcessManager::Get(browser_context);
  for (const scoped_refptr<const Extension>& extension : granted_extensions_) {
    extension->permissions_data()->ClearTabSpecificPermissions(tab_id_);
    SetCorsOriginAccessList(browser_context, *extension, base::DoNothing());

    extension_ids.push_back(extension->id());
    std::set<content::RenderFrameHost*> extension_frame_hosts =
        process_manager->GetRenderFrameHostsForExtension(extension->id());
    frame_hosts.insert(extension_frame_hosts.begin(),
                       extension_frame_hosts.end());
  }

  RendererMessageFunction clear_message =
      base::BindRepeating(&ClearTabSpecificPermissions, extension_ids, tab_id_);
  SendRendererMessageToProcesses(
      frame_hosts, web_contents()->GetPrimaryMainFrame()->GetProcess(),
      clear_message);

  granted_extensions_.Clear();
}

}  // namespace extensions
