// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef EXTENSIONS_BROWSER_EXTENSION_WEB_CONTENTS_OBSERVER_H_
#define EXTENSIONS_BROWSER_EXTENSION_WEB_CONTENTS_OBSERVER_H_

#include <map>
#include <string>

#include "base/compiler_specific.h"
#include "base/memory/raw_ptr.h"
#include "base/types/pass_key.h"
#include "components/sessions/core/session_id.h"
#include "content/public/browser/web_contents_observer.h"
#include "extensions/browser/extension_function_dispatcher.h"
#include "extensions/common/mojom/frame.mojom.h"
#include "mojo/public/cpp/bindings/associated_remote.h"

namespace content {
class BrowserContext;
class RenderFrameHost;
class WebContents;
}

namespace sessions {
class SessionTabHelper;
}

namespace extensions {
class Extension;
class ExtensionFrameHost;

// A web contents observer used for renderer and extension processes. Grants the
// renderer access to certain URL scheme patterns for extensions and notifies
// the renderer that the extension was loaded.
//
// Extension system embedders must create an instance for every extension
// WebContents. It must be a subclass so that creating an instance via
// content::WebContentsUserData::CreateForWebContents() provides an object of
// the correct type. For an example, see ChromeExtensionWebContentsObserver.
//
// This class is responsible for maintaining the registrations of extension
// frames with the ProcessManager. Only frames in an extension process are
// registered. If out-of-process frames are enabled, every frame hosts a
// chrome-extension: page. Otherwise non-extension frames may erroneously be
// registered, but only briefly until they are correctly classified. This is
// achieved using the following notifications:
// 1. RenderFrameCreated - registers all new frames in extension processes.
// 2. DidCommitProvisionalLoadForFrame - unregisters non-extension frames.
// 3. DidNavigateAnyFrame - registers extension frames if they had been
//    unregistered.
//
// Without OOPIF, non-extension frames created by the Chrome extension are also
// registered at RenderFrameCreated. When the non-extension page is committed,
// we detect that the unexpected URL and unregister the frame.
// With OOPIF only the first notification is sufficient in most cases, except
// for sandboxed frames with a unique origin.
class ExtensionWebContentsObserver
    : public content::WebContentsObserver,
      public ExtensionFunctionDispatcher::Delegate {
 public:
  ExtensionWebContentsObserver(const ExtensionWebContentsObserver&) = delete;
  ExtensionWebContentsObserver& operator=(const ExtensionWebContentsObserver&) =
      delete;

  // Returns the ExtensionWebContentsObserver for the given |web_contents|.
  static ExtensionWebContentsObserver* GetForWebContents(
      content::WebContents* web_contents);

  // Binds the LocalFrameHost interface to the ExtensionFrameHost associated
  // with the RenderFrameHost.
  static void BindLocalFrameHost(
      mojo::PendingAssociatedReceiver<mojom::LocalFrameHost> receiver,
      content::RenderFrameHost* rfh);

  // This must be called by clients directly after the EWCO has been created.
  void Initialize();

  ExtensionFunctionDispatcher* dispatcher() { return &dispatcher_; }

  // Returns the extension associated with the given |render_frame_host|, or
  // null if there is none.
  // If |verify_url| is false, only the SiteInstance is taken into account.
  // If |verify_url| is true, the frame's last committed URL is also used to
  // improve the classification of the frame.
  const Extension* GetExtensionFromFrame(
      content::RenderFrameHost* render_frame_host,
      bool verify_url) const;

  // Returns mojom::LocalFrame* corresponding |render_frame_host|. It emplaces
  // AssociatedRemote<mojom::LocalFrame> to |local_frame_map_| if the map
  // doesn't have it. Note that it could return nullptr if |render_frame_host|
  // is not live.
  mojom::LocalFrame* GetLocalFrame(content::RenderFrameHost* render_frame_host);

  // Tells the receiver to start listening to window ID changes from the
  // supplied SessionTabHelper. This method is public to allow the code that
  // installs new SessionTabHelpers to call it; that in turn is required because
  // SessionTabHelpers may be created after the corresponding
  // ExtensionWebContentsObserver has already been initialized.
  void ListenToWindowIdChangesFrom(sessions::SessionTabHelper* helper);

  ExtensionFrameHost* extension_frame_host_for_testing() {
    return extension_frame_host_.get();
  }

 protected:
  explicit ExtensionWebContentsObserver(content::WebContents* web_contents);
  ~ExtensionWebContentsObserver() override;

  bool initialized() const { return initialized_; }

  content::BrowserContext* browser_context() { return browser_context_; }

  // Initializes a new render frame. Subclasses should invoke this
  // implementation if extending. Note: this should be called for both extension
  // and non-extension frames.
  virtual void InitializeRenderFrame(
      content::RenderFrameHost* render_frame_host);

  // Creates ExtensionFrameHost which implements mojom::LocalFrameHost.
  virtual std::unique_ptr<ExtensionFrameHost> CreateExtensionFrameHost(
      content::WebContents* web_contents);

  // ExtensionFunctionDispatcher::Delegate overrides.
  content::WebContents* GetAssociatedWebContents() const override;

  // content::WebContentsObserver overrides.
  void RenderFrameCreated(content::RenderFrameHost* render_frame_host) override;
  void RenderFrameDeleted(content::RenderFrameHost* render_frame_host) override;
  void ReadyToCommitNavigation(
      content::NavigationHandle* navigation_handle) override;
  void DidFinishNavigation(
      content::NavigationHandle* navigation_handle) override;
  void MediaPictureInPictureChanged(bool is_picture_in_picture) override;

  // Per the documentation in WebContentsObserver, these two methods are invoked
  // when a Pepper plugin instance is attached/detached in the page DOM.
  void PepperInstanceCreated() override;
  void PepperInstanceDeleted() override;

  // Returns the extension id associated with the given |render_frame_host|, or
  // the empty string if there is none.
  std::string GetExtensionIdFromFrame(
      content::RenderFrameHost* render_frame_host) const;

 private:
  using PassKey = base::PassKey<ExtensionWebContentsObserver>;

  void OnWindowIdChanged(const SessionID& id);

  // The BrowserContext associated with the WebContents being observed.
  raw_ptr<content::BrowserContext> browser_context_;

  ExtensionFunctionDispatcher dispatcher_;

  // Whether this object has been initialized.
  bool initialized_;

  std::unique_ptr<ExtensionFrameHost> extension_frame_host_;

  base::CallbackListSubscription window_id_subscription_;

  // A map of render frame host to mojo remotes.
  std::map<content::RenderFrameHost*, mojo::AssociatedRemote<mojom::LocalFrame>>
      local_frame_map_;
};

}  // namespace extensions

#endif  // EXTENSIONS_BROWSER_EXTENSION_WEB_CONTENTS_OBSERVER_H_
