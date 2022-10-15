// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef EXTENSIONS_BROWSER_EXTENSION_HOST_H_
#define EXTENSIONS_BROWSER_EXTENSION_HOST_H_

#include <stdint.h>

#include <memory>
#include <string>
#include <unordered_map>

#include "base/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"
#include "base/timer/elapsed_timer.h"
#include "content/public/browser/media_stream_request.h"
#include "content/public/browser/web_contents_delegate.h"
#include "content/public/browser/web_contents_observer.h"
#include "extensions/browser/deferred_start_render_host.h"
#include "extensions/browser/extension_function_dispatcher.h"
#include "extensions/browser/extension_registry_observer.h"
#include "extensions/common/mojom/view_type.mojom.h"
#include "extensions/common/stack_frame.h"

namespace content {
class BrowserContext;
class RenderProcessHost;
class SiteInstance;
}

namespace extensions {
class Extension;
class ExtensionHostDelegate;
class ExtensionHostObserver;
class ExtensionHostQueue;

// This class is the browser component of an extension component's page.
// It handles setting up the renderer process, if needed, with special
// privileges available to extensions.  It may have a view to be shown in the
// browser UI, or it may be hidden.
//
// If you are adding code that only affects visible extension views (and not
// invisible background pages) you should add it to ExtensionViewHost.
class ExtensionHost : public DeferredStartRenderHost,
                      public content::WebContentsDelegate,
                      public content::WebContentsObserver,
                      public ExtensionFunctionDispatcher::Delegate,
                      public ExtensionRegistryObserver {
 public:
  using CloseHandler = base::OnceCallback<void(ExtensionHost*)>;

  ExtensionHost(const Extension* extension,
                content::SiteInstance* site_instance,
                const GURL& url,
                mojom::ViewType host_type);

  ExtensionHost(const ExtensionHost&) = delete;
  ExtensionHost& operator=(const ExtensionHost&) = delete;

  ~ExtensionHost() override;

  // This may be null if the extension has been or is being unloaded.
  const Extension* extension() const { return extension_; }

  const std::string& extension_id() const { return extension_id_; }
  content::WebContents* host_contents() const { return host_contents_.get(); }
  content::RenderFrameHost* main_frame_host() const { return main_frame_host_; }
  content::RenderProcessHost* render_process_host() const;
  bool has_loaded_once() const { return has_loaded_once_; }
  const GURL& initial_url() const { return initial_url_; }
  bool document_element_available() const {
    return document_element_available_;
  }

  content::BrowserContext* browser_context() { return browser_context_; }

  mojom::ViewType extension_host_type() const { return extension_host_type_; }

  // Sets the callback responsible for closing the ExtensionHost in response to
  // a WebContents::CloseContents() call (which is triggered from e.g.
  // calling `window.close()`). This is done separately from the constructor as
  // some callsites create an ExtensionHost prior to the object that is
  // responsible for later closing it, but must be done before `CloseContents()`
  // can be called.
  void SetCloseHandler(CloseHandler close_handler);

  // Returns the last committed URL of the associated WebContents.
  const GURL& GetLastCommittedURL() const;

  // Returns true if the renderer main frame exists.
  bool IsRendererLive() const;

  // Prepares to initializes our RenderFrameHost by creating the main frame and
  // navigating `host_contents_` to the initial url. This happens delayed to
  // avoid locking the UI.
  void CreateRendererSoon();

  // Closes this host (results in [possibly asynchronous] deletion).
  void Close();

  // Typical observer interface.
  void AddObserver(ExtensionHostObserver* observer);
  void RemoveObserver(ExtensionHostObserver* observer);

  // Called when an event is dispatched to the event page associated with this
  // ExtensionHost.
  void OnBackgroundEventDispatched(const std::string& event_name, int event_id);

  // Called by the ProcessManager when a network request is started by the
  // extension corresponding to this ExtensionHost.
  void OnNetworkRequestStarted(uint64_t request_id);

  // Called by the ProcessManager when a previously started network request is
  // finished.
  void OnNetworkRequestDone(uint64_t request_id);

  // Returns true if the ExtensionHost is allowed to be navigated.
  bool ShouldAllowNavigations() const;

  // content::WebContentsObserver:
  bool OnMessageReceived(const IPC::Message& message,
                         content::RenderFrameHost* host) override;
  void RenderFrameCreated(content::RenderFrameHost* frame_host) override;
  void RenderFrameHostChanged(content::RenderFrameHost* old_host,
                              content::RenderFrameHost* new_host) override;
  void PrimaryMainFrameRenderProcessGone(
      base::TerminationStatus status) override;
  void PrimaryMainDocumentElementAvailable() override;
  void DidStopLoading() override;

  // content::WebContentsDelegate:
  content::JavaScriptDialogManager* GetJavaScriptDialogManager(
      content::WebContents* source) override;
  void AddNewContents(content::WebContents* source,
                      std::unique_ptr<content::WebContents> new_contents,
                      const GURL& target_url,
                      WindowOpenDisposition disposition,
                      const blink::mojom::WindowFeatures& window_features,
                      bool user_gesture,
                      bool* was_blocked) override;
  void CloseContents(content::WebContents* contents) override;
  void RequestMediaAccessPermission(
      content::WebContents* web_contents,
      const content::MediaStreamRequest& request,
      content::MediaResponseCallback callback) override;
  bool CheckMediaAccessPermission(content::RenderFrameHost* render_frame_host,
                                  const GURL& security_origin,
                                  blink::mojom::MediaStreamType type) override;
  bool IsNeverComposited(content::WebContents* web_contents) override;
  content::PictureInPictureResult EnterPictureInPicture(
      content::WebContents* web_contents) override;
  void ExitPictureInPicture() override;
  std::string GetTitleForMediaControls(
      content::WebContents* web_contents) override;

  // ExtensionRegistryObserver:
  void OnExtensionReady(content::BrowserContext* browser_context,
                        const Extension* extension) override;
  void OnExtensionUnloaded(content::BrowserContext* browser_context,
                           const Extension* extension,
                           UnloadedExtensionReason reason) override;

 protected:
  // Called each time this ExtensionHost completes a load finishes loading,
  // before any stop-loading notifications or observer methods are called.
  virtual void OnDidStopFirstLoad();

  // Navigates to the initial page.
  virtual void LoadInitialURL();

  // Returns true if we're hosting a background page.
  virtual bool IsBackgroundPage() const;

 private:
  // DeferredStartRenderHost:
  void CreateRendererNow() override;

  // Message handlers.
  void OnEventAck(int event_id);
  void OnIncrementLazyKeepaliveCount();
  void OnDecrementLazyKeepaliveCount();

  void MaybeNotifyRenderProcessReady();
  void NotifyRenderProcessReady();

  // Records UMA for load events.
  void RecordStopLoadingUMA();

  // Delegate for functionality that cannot exist in the extensions module.
  std::unique_ptr<ExtensionHostDelegate> delegate_;

  // The extension that we're hosting in this view.
  raw_ptr<const Extension> extension_;

  // Id of extension that we're hosting in this view.
  const std::string extension_id_;

  // The browser context that this host is tied to.
  raw_ptr<content::BrowserContext> browser_context_;

  // The host for our HTML content.
  std::unique_ptr<content::WebContents> host_contents_;

  // A pointer to the current or speculative main frame in `host_contents_`. We
  // can't access this frame through the `host_contents_` directly as it does
  // not expose the speculative main frame. While navigating to a still-loading
  // speculative main frame, we want to send messages to it rather than the
  // current frame.
  raw_ptr<content::RenderFrameHost> main_frame_host_;

  // Whether CreateRendererNow was called before the extension was ready.
  bool is_renderer_creation_pending_ = false;

  // Whether ExtensionHostCreated() event has been fired, since
  // RenderFrameCreated is triggered by every main frame that is created,
  // including during a cross-site navigation which uses a new main frame.
  bool has_creation_notification_already_fired_ = false;

  // Whether the ExtensionHost has finished loading some content at least once.
  // There may be subsequent loads - such as reloads and navigations - and this
  // will not affect its value (it will remain true).
  bool has_loaded_once_ = false;

  // True if the main frame has finished parsing.
  bool document_element_available_ = false;

  // The original URL of the page being hosted.
  GURL initial_url_;

  // Messages sent out to the renderer that have not been acknowledged yet.
  // Maps event ID to event name.
  std::unordered_map<int, std::string> unacked_messages_;

  // The type of view being hosted.
  mojom::ViewType extension_host_type_;

  // Measures how long since the ExtensionHost object was created. This can be
  // used to measure the responsiveness of UI. For example, it's important to
  // keep this as low as possible for popups. Contrast this to |load_start_|,
  // for which a low value does not necessarily mean a responsive UI, as
  // ExtensionHosts may sit in an ExtensionHostQueue for a long time.
  base::ElapsedTimer create_start_;

  // Measures how long since the initial URL started loading. This timer is
  // started only once the ExtensionHost has exited the ExtensionHostQueue.
  std::unique_ptr<base::ElapsedTimer> load_start_;

  CloseHandler close_handler_;
  // Whether the close handler has been previously invoked.
  bool called_close_handler_ = false;

  base::ObserverList<ExtensionHostObserver>::Unchecked observer_list_;

  base::WeakPtrFactory<ExtensionHost> weak_ptr_factory_{this};
};

}  // namespace extensions

#endif  // EXTENSIONS_BROWSER_EXTENSION_HOST_H_
