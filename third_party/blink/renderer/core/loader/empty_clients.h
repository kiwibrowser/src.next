/*
 * Copyright (C) 2006 Eric Seidel (eric@webkit.org)
 * Copyright (C) 2008, 2009, 2010, 2011, 2012 Apple Inc. All rights reserved.
 * Copyright (C) 2010 Nokia Corporation and/or its subsidiary(-ies).
 * Copyright (C) 2012 Samsung Electronics. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE COMPUTER, INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE COMPUTER, INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_LOADER_EMPTY_CLIENTS_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_LOADER_EMPTY_CLIENTS_H_

#include <memory>

#include "base/notreached.h"
#include "cc/paint/paint_canvas.h"
#include "cc/trees/paint_holding_reason.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "third_party/blink/public/common/associated_interfaces/associated_interface_provider.h"
#include "third_party/blink/public/common/browser_interface_broker_proxy.h"
#include "third_party/blink/public/common/input/web_menu_source_type.h"
#include "third_party/blink/public/common/user_agent/user_agent_metadata.h"
#include "third_party/blink/public/mojom/frame/viewport_intersection_state.mojom-blink.h"
#include "third_party/blink/public/mojom/input/focus_type.mojom-blink-forward.h"
#include "third_party/blink/public/platform/web_spell_check_panel_host_client.h"
#include "third_party/blink/public/platform/web_url_loader.h"
#include "third_party/blink/public/platform/web_url_loader_factory.h"
#include "third_party/blink/renderer/core/core_export.h"
#include "third_party/blink/renderer/core/frame/local_frame_client.h"
#include "third_party/blink/renderer/core/frame/remote_frame_client.h"
#include "third_party/blink/renderer/core/page/chrome_client.h"
#include "third_party/blink/renderer/core/page/page.h"
#include "third_party/blink/renderer/platform/cursors.h"
#include "third_party/blink/renderer/platform/exported/wrapped_resource_request.h"
#include "third_party/blink/renderer/platform/graphics/touch_action.h"
#include "third_party/blink/renderer/platform/loader/fetch/resource_error.h"
#include "third_party/blink/renderer/platform/wtf/allocator/allocator.h"
#include "third_party/blink/renderer/platform/wtf/forward.h"
#include "ui/base/cursor/cursor.h"
#include "ui/display/screen_info.h"
#include "ui/display/screen_infos.h"
#include "ui/gfx/geometry/point_f.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/rect_f.h"
#include "v8/include/v8.h"

/*
 This file holds empty Client stubs for use by WebCore.

 Viewless element needs to create a dummy Page->LocalFrame->FrameView tree for
 use in parsing or executing JavaScript. This tree depends heavily on Clients
 (usually provided by WebKit classes).

 This file was first created for SVGImage as it had no way to access the current
 Page (nor should it, since Images are not tied to a page). See
 http://bugs.webkit.org/show_bug.cgi?id=5971 for the original discussion about
 this file.

 Ideally, whenever you change a Client class, you should add a stub here.
 Brittle, yes. Unfortunate, yes. Hopefully temporary.
*/

namespace ui {
class Cursor;
}

namespace blink {

class CORE_EXPORT EmptyChromeClient : public ChromeClient {
 public:
  EmptyChromeClient() = default;
  ~EmptyChromeClient() override = default;

  // ChromeClient implementation.
  WebViewImpl* GetWebView() const override { return nullptr; }
  void ChromeDestroyed() override {}
  void SetWindowRect(const gfx::Rect&, LocalFrame&) override {}
  gfx::Rect RootWindowRect(LocalFrame&) override { return gfx::Rect(); }
  void DidAccessInitialMainDocument() override {}
  void FocusPage() override {}
  void DidFocusPage() override {}
  bool CanTakeFocus(mojom::blink::FocusType) override { return false; }
  void TakeFocus(mojom::blink::FocusType) override {}
  void Show(LocalFrame& frame,
            LocalFrame& opener_frame,
            NavigationPolicy navigation_policy,
            const mojom::blink::WindowFeatures& window_features,
            bool consumed_user_gesture) override {}
  void DidOverscroll(const gfx::Vector2dF&,
                     const gfx::Vector2dF&,
                     const gfx::PointF&,
                     const gfx::Vector2dF&) override {}
  void SetOverscrollBehavior(LocalFrame& frame,
                             const cc::OverscrollBehavior&) override {}
  void BeginLifecycleUpdates(LocalFrame& main_frame) override {}
  void RegisterForCommitObservation(CommitObserver*) override {}
  void UnregisterFromCommitObservation(CommitObserver*) override {}
  void WillCommitCompositorFrame() override {}
  std::unique_ptr<cc::ScopedPauseRendering> PauseRendering(
      LocalFrame&) override;
  bool StartDeferringCommits(LocalFrame& main_frame,
                             base::TimeDelta timeout,
                             cc::PaintHoldingReason reason) override;
  void StopDeferringCommits(LocalFrame& main_frame,
                            cc::PaintHoldingCommitTrigger) override {}
  void StartDragging(LocalFrame*,
                     const WebDragData&,
                     DragOperationsMask,
                     const SkBitmap& drag_image,
                     const gfx::Vector2d& cursor_offset,
                     const gfx::Rect& drag_obj_rect) override {}
  bool AcceptsLoadDrops() const override { return true; }
  bool ShouldReportDetailedMessageForSourceAndSeverity(
      LocalFrame&,
      mojom::blink::ConsoleMessageLevel,
      const String&) override {
    return false;
  }
  void AddMessageToConsole(LocalFrame*,
                           mojom::ConsoleMessageSource,
                           mojom::ConsoleMessageLevel,
                           const String&,
                           unsigned,
                           const String&,
                           const String&) override {}
  bool CanOpenBeforeUnloadConfirmPanel() override { return false; }
  bool OpenBeforeUnloadConfirmPanelDelegate(LocalFrame*, bool) override {
    return true;
  }
  void CloseWindowSoon() override {}
  Page* CreateWindowDelegate(LocalFrame*,
                             const FrameLoadRequest&,
                             const AtomicString&,
                             const WebWindowFeatures&,
                             network::mojom::blink::WebSandboxFlags,
                             const SessionStorageNamespaceId&,
                             bool& consumed_user_gesture) override {
    return nullptr;
  }
  bool OpenJavaScriptAlertDelegate(LocalFrame*, const String&) override {
    return false;
  }
  bool OpenJavaScriptConfirmDelegate(LocalFrame*, const String&) override {
    return false;
  }
  bool OpenJavaScriptPromptDelegate(LocalFrame*,
                                    const String&,
                                    const String&,
                                    String&) override {
    return false;
  }
  bool HasOpenedPopup() const override { return false; }
  PopupMenu* OpenPopupMenu(LocalFrame&, HTMLSelectElement&) override;
  PagePopup* OpenPagePopup(PagePopupClient*) override { return nullptr; }
  void ClosePagePopup(PagePopup*) override {}
  DOMWindow* PagePopupWindowForTesting() const override { return nullptr; }

  bool TabsToLinks() override { return false; }

  void InvalidateContainer() override {}
  void ScheduleAnimation(const LocalFrameView*,
                         base::TimeDelta delay) override {}
  gfx::Rect ViewportToScreen(const gfx::Rect& r,
                             const LocalFrameView*) const override {
    return r;
  }
  float WindowToViewportScalar(LocalFrame*, const float s) const override {
    return s;
  }
  const display::ScreenInfo& GetScreenInfo(LocalFrame&) const override {
    return empty_screen_infos_.current();
  }
  const display::ScreenInfos& GetScreenInfos(LocalFrame&) const override {
    return empty_screen_infos_;
  }
  void ContentsSizeChanged(LocalFrame*, const gfx::Size&) const override {}
  void ShowMouseOverURL(const HitTestResult&) override {}
  void UpdateTooltipUnderCursor(LocalFrame&,
                                const String&,
                                TextDirection) override {}
  void UpdateTooltipFromKeyboard(LocalFrame&,
                                 const String&,
                                 TextDirection,
                                 const gfx::Rect&) override {}
  void ClearKeyboardTriggeredTooltip(LocalFrame&) override {}
  void PrintDelegate(LocalFrame*) override {}
  ColorChooser* OpenColorChooser(LocalFrame*,
                                 ColorChooserClient*,
                                 const Color&) override;
  DateTimeChooser* OpenDateTimeChooser(
      LocalFrame* frame,
      DateTimeChooserClient*,
      const DateTimeChooserParameters&) override;
  void OpenTextDataListChooser(HTMLInputElement&) override;
  void OpenFileChooser(LocalFrame*, scoped_refptr<FileChooser>) override;
  void SetCursor(const ui::Cursor&, LocalFrame* local_root) override {}
  void SetCursorOverridden(bool) override {}
  ui::Cursor LastSetCursorForTesting() const override {
    return PointerCursor();
  }
  void AttachRootLayer(scoped_refptr<cc::Layer>,
                       LocalFrame* local_root) override;
  cc::AnimationHost* GetCompositorAnimationHost(LocalFrame&) const override {
    return nullptr;
  }
  cc::AnimationTimeline* GetScrollAnimationTimeline(
      LocalFrame&) const override {
    return nullptr;
  }
  void SetEventListenerProperties(LocalFrame*,
                                  cc::EventListenerClass,
                                  cc::EventListenerProperties) override {}
  void SetHasScrollEventHandlers(LocalFrame*, bool) override {}
  void SetNeedsLowLatencyInput(LocalFrame*, bool) override {}
  void SetNeedsUnbufferedInputForDebugger(LocalFrame*, bool) override {}
  void RequestUnbufferedInputEvents(LocalFrame*) override {}
  void SetTouchAction(LocalFrame*, TouchAction) override {}
  void SetPanAction(LocalFrame*, mojom::blink::PanAction pan_action) override {}
  void DidAssociateFormControlsAfterLoad(LocalFrame*) override {}
  String AcceptLanguages() override;
  void RegisterPopupOpeningObserver(PopupOpeningObserver*) override {}
  void UnregisterPopupOpeningObserver(PopupOpeningObserver*) override {}
  void NotifyPopupOpeningObservers() const override {}

  void RequestBeginMainFrameNotExpected(LocalFrame& frame,
                                        bool request) override {}
  int GetLayerTreeId(LocalFrame& frame) override { return 0; }
  void SetCursorForPlugin(const ui::Cursor&, LocalFrame*) override {}
  void InstallSupplements(LocalFrame&) override {}
  void OutermostMainFrameScrollOffsetChanged() const override {}

 private:
  const display::ScreenInfos empty_screen_infos_{display::ScreenInfo()};
};

class CORE_EXPORT EmptyLocalFrameClient : public LocalFrameClient {
 public:
  EmptyLocalFrameClient() = default;
  EmptyLocalFrameClient(const EmptyLocalFrameClient&) = delete;
  EmptyLocalFrameClient& operator=(const EmptyLocalFrameClient&) = delete;
  ~EmptyLocalFrameClient() override = default;

  bool HasWebView() const override { return true; }  // mainly for assertions

  bool InShadowTree() const override { return false; }

  void WillBeDetached() override {}
  void Detached(FrameDetachType) override {}

  void DispatchWillSendRequest(ResourceRequest&) override {}
  void DispatchDidLoadResourceFromMemoryCache(
      const ResourceRequest&,
      const ResourceResponse&) override {}

  void DispatchDidHandleOnloadEvents() override {}
  void DispatchDidReceiveTitle(const String&) override {}
  void DispatchDidCommitLoad(
      HistoryItem* item,
      WebHistoryCommitType commit_type,
      bool should_reset_browser_interface_broker,
      const blink::ParsedPermissionsPolicy& permissions_policy_header,
      const blink::DocumentPolicyFeatureState& document_policy_header)
      override {}
  void DispatchDidFailLoad(const ResourceError&,
                           WebHistoryCommitType) override {}
  void DispatchDidDispatchDOMContentLoadedEvent() override {}
  void DispatchDidFinishLoad() override {}

  void BeginNavigation(
      const ResourceRequest&,
      mojom::RequestContextFrameType,
      LocalDOMWindow*,
      DocumentLoader*,
      WebNavigationType,
      NavigationPolicy,
      WebFrameLoadType,
      bool,
      // TODO(crbug.com/1315802): Refactor _unfencedTop handling.
      bool,
      mojom::blink::TriggeringEventInfo,
      HTMLFormElement*,
      network::mojom::CSPDisposition,
      mojo::PendingRemote<mojom::blink::BlobURLToken>,
      base::TimeTicks,
      const String&,
      const absl::optional<Impression>&,
      const LocalFrameToken* initiator_frame_token,
      std::unique_ptr<SourceLocation>,
      mojo::PendingRemote<mojom::blink::PolicyContainerHostKeepAliveHandle>)
      override;

  void DispatchWillSendSubmitEvent(HTMLFormElement*) override;

  void DidStartLoading() override {}
  void DidStopLoading() override {}

  void DidCreateDocumentLoader(DocumentLoader*) override {}

  String UserAgentOverride() override { return ""; }
  String UserAgent() override { return ""; }
  String FullUserAgent() override { return ""; }
  String ReducedUserAgent() override { return ""; }
  absl::optional<blink::UserAgentMetadata> UserAgentMetadata() override {
    return blink::UserAgentMetadata();
  }

  String DoNotTrackValue() override { return String(); }

  void TransitionToCommittedForNewPage() override {}

  bool NavigateBackForward(int offset) const override { return false; }
  void DidDispatchPingLoader(const KURL&) override {}
  void SelectorMatchChanged(const Vector<String>&,
                            const Vector<String>&) override {}
  LocalFrame* CreateFrame(const AtomicString&, HTMLFrameOwnerElement*) override;
  std::pair<RemoteFrame*, PortalToken> CreatePortal(
      HTMLPortalElement*,
      mojo::PendingAssociatedReceiver<mojom::blink::Portal>,
      mojo::PendingAssociatedRemote<mojom::blink::PortalClient>) override;
  RemoteFrame* AdoptPortal(HTMLPortalElement*) override;

  RemoteFrame* CreateFencedFrame(
      HTMLFencedFrameElement*,
      mojo::PendingAssociatedReceiver<mojom::blink::FencedFrameOwnerHost>,
      mojom::blink::FencedFrameMode) override;

  WebPluginContainerImpl* CreatePlugin(HTMLPlugInElement&,
                                       const KURL&,
                                       const Vector<String>&,
                                       const Vector<String>&,
                                       const String&,
                                       bool) override;
  std::unique_ptr<WebMediaPlayer> CreateWebMediaPlayer(
      HTMLMediaElement&,
      const WebMediaPlayerSource&,
      WebMediaPlayerClient*) override;
  WebRemotePlaybackClient* CreateWebRemotePlaybackClient(
      HTMLMediaElement&) override;

  void DidCommitDocumentReplacementNavigation(DocumentLoader*) override {}
  void DispatchDidClearWindowObjectInMainWorld() override {}
  void DocumentElementAvailable() override {}
  void RunScriptsAtDocumentElementAvailable() override {}
  void RunScriptsAtDocumentReady(bool) override {}
  void RunScriptsAtDocumentIdle() override {}

  void DidCreateScriptContext(v8::Local<v8::Context>,
                              int32_t world_id) override {}
  void WillReleaseScriptContext(v8::Local<v8::Context>,
                                int32_t world_id) override {}
  bool AllowScriptExtensions() override { return false; }

  BrowserInterfaceBrokerProxy& GetBrowserInterfaceBroker() override {
    return GetEmptyBrowserInterfaceBroker();
  }

  AssociatedInterfaceProvider* GetRemoteNavigationAssociatedInterfaces()
      override;

  WebSpellCheckPanelHostClient* SpellCheckPanelHostClient() const override {
    return nullptr;
  }

  std::unique_ptr<WebServiceWorkerProvider> CreateServiceWorkerProvider()
      override;
  WebContentSettingsClient* GetContentSettingsClient() override {
    return nullptr;
  }

  void SetTextCheckerClientForTesting(WebTextCheckClient*);
  WebTextCheckClient* GetTextCheckerClient() const override;

  std::unique_ptr<WebURLLoaderFactory> CreateURLLoaderFactory() override {
    // Most consumers of EmptyLocalFrameClient should not make network requests.
    // If an exception needs to be made (e.g. in test code), then the consumer
    // should define their own subclass of LocalFrameClient or
    // EmptyLocalFrameClient and override the CreateURLLoaderFactory method.
    // See also https://crbug.com/891872.
    // We use CHECK(false) instead of NOTREACHED() here to catch errors on
    // clusterfuzz and production.
    CHECK(false);
    return nullptr;
  }

  void AnnotatedRegionsChanged() override {}
  base::UnguessableToken GetDevToolsFrameToken() const override {
    return base::UnguessableToken::Create();
  }
  String evaluateInInspectorOverlayForTesting(const String& script) override {
    return g_empty_string;
  }

  Frame* FindFrame(const AtomicString& name) const override;

 protected:
  // Not owned
  WebTextCheckClient* text_check_client_;
};

class EmptySpellCheckPanelHostClient : public WebSpellCheckPanelHostClient {
  USING_FAST_MALLOC(EmptySpellCheckPanelHostClient);

 public:
  EmptySpellCheckPanelHostClient() = default;
  EmptySpellCheckPanelHostClient(const EmptySpellCheckPanelHostClient&) =
      delete;
  EmptySpellCheckPanelHostClient& operator=(
      const EmptySpellCheckPanelHostClient&) = delete;

  void ShowSpellingUI(bool) override {}
  bool IsShowingSpellingUI() override { return false; }
  void UpdateSpellingUIWithMisspelledWord(const WebString&) override {}
};

CORE_EXPORT ChromeClient& GetStaticEmptyChromeClientInstance();

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_CORE_LOADER_EMPTY_CLIENTS_H_
