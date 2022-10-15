// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef EXTENSIONS_BROWSER_CONTENT_SCRIPT_TRACKER_H_
#define EXTENSIONS_BROWSER_CONTENT_SCRIPT_TRACKER_H_

#include "base/types/pass_key.h"
#include "extensions/common/extension_id.h"
#include "extensions/common/mojom/host_id.mojom-forward.h"
#include "url/gurl.h"

struct HostID;

namespace content {
class NavigationHandle;
class RenderFrameHost;
class RenderProcessHost;
}  // namespace content

namespace extensions {

class Extension;
class ExtensionWebContentsObserver;
class UserScriptLoader;
class RequestContentScript;
class ScriptExecutor;

// Class for
// 1) observing when a content script gets injected into a process,
// 2) checking if a content script was ever injected into a given process.
//
// WARNING: False positives might happen.  This class is primarily meant to help
// make security decisions.  This focus means that it is known and
// working-as-intended that false positives might happen - in some scenarios the
// tracker might report that a content script was injected, when it actually
// wasn't (e.g. because the tracker might not have access to all the
// renderer-side information used to decide whether to run a content script).
//
// WARNING: This class ignores cases that don't currently need IPC verification:
// - CSS content scripts (only JavaScript content scripts are tracked)
// - WebUI content scripts (only content scripts injected by extensions are
//   tracked)
//
// This class may only be used on the UI thread.
class ContentScriptTracker {
 public:
  // Only static methods.
  ContentScriptTracker() = delete;

  // Answers whether the `process` has ever in the past run a content script
  // from an extension with the given `extension_id`.
  static bool DidProcessRunContentScriptFromExtension(
      const content::RenderProcessHost& process,
      const ExtensionId& extension_id);

  // Returns all the IDs for extensions that have ever in the past run a content
  // script in `process`.
  static ExtensionIdSet GetExtensionsThatRanScriptsInProcess(
      const content::RenderProcessHost& process);

  // The few methods below are called by ExtensionWebContentsObserver to notify
  // ContentScriptTracker about various events.  The methods correspond directly
  // to methods of content::WebContentsObserver with the same names.
  static void ReadyToCommitNavigation(
      base::PassKey<ExtensionWebContentsObserver> pass_key,
      content::NavigationHandle* navigation);
  static void DidFinishNavigation(
      base::PassKey<ExtensionWebContentsObserver> pass_key,
      content::NavigationHandle* navigation);
  static void RenderFrameCreated(
      base::PassKey<ExtensionWebContentsObserver> pass_key,
      content::RenderFrameHost* frame);
  static void RenderFrameDeleted(
      base::PassKey<ExtensionWebContentsObserver> pass_key,
      content::RenderFrameHost* frame);

  // Called before ExtensionMsg_ExecuteCode is sent to a renderer process
  // (typically when handling chrome.tabs.executeScript or a similar API call).
  //
  // The caller needs to ensure that if `host_id.type() == HostID::EXTENSIONS`,
  // then the extension with the given `host_id` exists and is enabled.
  static void WillExecuteCode(base::PassKey<ScriptExecutor> pass_key,
                              content::RenderFrameHost* frame,
                              const mojom::HostID& host_id);

  // Called before `extensions::mojom::LocalFrame::ExecuteDeclarativeScript` is
  // invoked in a renderer process (e.g. when handling RequestContentScript
  // action of the `chrome.declarativeContent` API).
  static void WillExecuteCode(base::PassKey<RequestContentScript> pass_key,
                              content::RenderFrameHost* frame,
                              const Extension& extension);

  // Called before the given renderer `process` is notified about new content
  // scripts.
  static void WillUpdateContentScriptsInRenderer(
      base::PassKey<UserScriptLoader> pass_key,
      const mojom::HostID& host_id,
      content::RenderProcessHost& process);

 private:
  using PassKey = base::PassKey<ContentScriptTracker>;

  // See the doc comment of DoContentScriptsMatch in the .cc file.
  friend class ContentScriptMatchingBrowserTest;
  static bool DoContentScriptsMatchForTesting(const Extension& extension,
                                              content::RenderFrameHost* frame,
                                              const GURL& url);
};

}  // namespace extensions

#endif  // EXTENSIONS_BROWSER_CONTENT_SCRIPT_TRACKER_H_
