/*
 * Copyright (C) 2006, 2007, 2008, 2009, 2010, 2011 Apple Inc. All rights
 * reserved.
 * Copyright (C) 2008 Nokia Corporation and/or its subsidiary(-ies)
 * Copyright (C) 2008, 2009 Torch Mobile Inc. All rights reserved.
 * (http://www.torchmobile.com/)
 * Copyright (C) 2008 Alp Toker <alp@atoker.com>
 * Copyright (C) Research In Motion Limited 2009. All rights reserved.
 * Copyright (C) 2011 Kris Jordan <krisjordan@gmail.com>
 * Copyright (C) 2011 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 * 3.  Neither the name of Apple Computer, Inc. ("Apple") nor the names of
 *     its contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE AND ITS CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL APPLE OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "third_party/blink/renderer/core/loader/frame_loader.h"

#include <memory>
#include <utility>

#include "base/auto_reset.h"
#include "base/trace_event/typed_macros.h"
#include "base/unguessable_token.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "services/metrics/public/cpp/ukm_builders.h"
#include "services/metrics/public/cpp/ukm_recorder.h"
#include "services/network/public/cpp/features.h"
#include "services/network/public/cpp/web_sandbox_flags.h"
#include "services/network/public/mojom/web_sandbox_flags.mojom-blink.h"
#include "third_party/blink/public/common/features.h"
#include "third_party/blink/public/common/user_agent/user_agent_metadata.h"
#include "third_party/blink/public/mojom/fetch/fetch_api_request.mojom-blink.h"
#include "third_party/blink/public/mojom/loader/request_context_frame_type.mojom-blink.h"
#include "third_party/blink/public/platform/modules/service_worker/web_service_worker_network_provider.h"
#include "third_party/blink/public/platform/platform.h"
#include "third_party/blink/public/platform/task_type.h"
#include "third_party/blink/public/platform/web_content_settings_client.h"
#include "third_party/blink/public/platform/web_url_request.h"
#include "third_party/blink/public/web/web_frame_load_type.h"
#include "third_party/blink/public/web/web_history_item.h"
#include "third_party/blink/public/web/web_navigation_params.h"
#include "third_party/blink/renderer/bindings/core/v8/script_controller.h"
#include "third_party/blink/renderer/bindings/core/v8/serialization/serialized_script_value.h"
#include "third_party/blink/renderer/core/dom/document_init.h"
#include "third_party/blink/renderer/core/dom/element.h"
#include "third_party/blink/renderer/core/dom/ignore_opens_during_unload_count_incrementer.h"
#include "third_party/blink/renderer/core/events/page_transition_event.h"
#include "third_party/blink/renderer/core/exported/web_plugin_container_impl.h"
#include "third_party/blink/renderer/core/fragment_directive/text_fragment_anchor.h"
#include "third_party/blink/renderer/core/frame/csp/content_security_policy.h"
#include "third_party/blink/renderer/core/frame/csp/csp_source.h"
#include "third_party/blink/renderer/core/frame/frame_console.h"
#include "third_party/blink/renderer/core/frame/local_dom_window.h"
#include "third_party/blink/renderer/core/frame/local_frame.h"
#include "third_party/blink/renderer/core/frame/local_frame_client.h"
#include "third_party/blink/renderer/core/frame/local_frame_view.h"
#include "third_party/blink/renderer/core/frame/policy_container.h"
#include "third_party/blink/renderer/core/frame/settings.h"
#include "third_party/blink/renderer/core/frame/visual_viewport.h"
#include "third_party/blink/renderer/core/frame/web_local_frame_impl.h"
#include "third_party/blink/renderer/core/html/forms/html_form_element.h"
#include "third_party/blink/renderer/core/html/html_frame_owner_element.h"
#include "third_party/blink/renderer/core/html_names.h"
#include "third_party/blink/renderer/core/inspector/console_message.h"
#include "third_party/blink/renderer/core/inspector/identifiers_factory.h"
#include "third_party/blink/renderer/core/loader/document_load_timing.h"
#include "third_party/blink/renderer/core/loader/document_loader.h"
#include "third_party/blink/renderer/core/loader/form_submission.h"
#include "third_party/blink/renderer/core/loader/frame_load_request.h"
#include "third_party/blink/renderer/core/loader/frame_loader_types.h"
#include "third_party/blink/renderer/core/loader/mixed_content_checker.h"
#include "third_party/blink/renderer/core/loader/progress_tracker.h"
#include "third_party/blink/renderer/core/navigation_api/navigation_api.h"
#include "third_party/blink/renderer/core/page/chrome_client.h"
#include "third_party/blink/renderer/core/page/frame_tree.h"
#include "third_party/blink/renderer/core/page/page.h"
#include "third_party/blink/renderer/core/page/plugin_data.h"
#include "third_party/blink/renderer/core/page/plugin_script_forbidden_scope.h"
#include "third_party/blink/renderer/core/page/scrolling/fragment_anchor.h"
#include "third_party/blink/renderer/core/page/scrolling/scrolling_coordinator.h"
#include "third_party/blink/renderer/core/page/viewport_description.h"
#include "third_party/blink/renderer/core/paint/paint_layer_scrollable_area.h"
#include "third_party/blink/renderer/core/probe/core_probes.h"
#include "third_party/blink/renderer/core/scroll/scroll_animator_base.h"
#include "third_party/blink/renderer/core/svg/graphics/svg_image.h"
#include "third_party/blink/renderer/core/xml/parser/xml_document_parser.h"
#include "third_party/blink/renderer/platform/bindings/dom_wrapper_world.h"
#include "third_party/blink/renderer/platform/bindings/microtask.h"
#include "third_party/blink/renderer/platform/bindings/script_forbidden_scope.h"
#include "third_party/blink/renderer/platform/bindings/v8_dom_activity_logger.h"
#include "third_party/blink/renderer/platform/exported/wrapped_resource_request.h"
#include "third_party/blink/renderer/platform/heap/garbage_collected.h"
#include "third_party/blink/renderer/platform/instrumentation/instance_counters.h"
#include "third_party/blink/renderer/platform/instrumentation/tracing/trace_event.h"
#include "third_party/blink/renderer/platform/loader/fetch/resource_fetcher.h"
#include "third_party/blink/renderer/platform/loader/fetch/resource_fetcher_properties.h"
#include "third_party/blink/renderer/platform/loader/fetch/resource_request.h"
#include "third_party/blink/renderer/platform/mhtml/archive_resource.h"
#include "third_party/blink/renderer/platform/mhtml/mhtml_archive.h"
#include "third_party/blink/renderer/platform/network/http_parsers.h"
#include "third_party/blink/renderer/platform/network/mime/mime_type_registry.h"
#include "third_party/blink/renderer/platform/network/network_utils.h"
#include "third_party/blink/renderer/platform/scheduler/public/frame_scheduler.h"
#include "third_party/blink/renderer/platform/weborigin/security_origin.h"
#include "third_party/blink/renderer/platform/weborigin/security_policy.h"
#include "third_party/blink/renderer/platform/wtf/text/string_utf8_adaptor.h"
#include "third_party/blink/renderer/platform/wtf/text/wtf_string.h"

namespace blink {

bool IsBackForwardLoadType(WebFrameLoadType type) {
  return type == WebFrameLoadType::kBackForward;
}

bool IsReloadLoadType(WebFrameLoadType type) {
  return type == WebFrameLoadType::kReload ||
         type == WebFrameLoadType::kReloadBypassingCache;
}

bool FrameLoader::NeedsHistoryItemRestore(WebFrameLoadType type) {
  return type == WebFrameLoadType::kBackForward || IsReloadLoadType(type);
}

ResourceRequest FrameLoader::ResourceRequestForReload(
    WebFrameLoadType frame_load_type,
    ClientRedirectPolicy client_redirect_policy) {
  DCHECK(IsReloadLoadType(frame_load_type));
  const auto cache_mode =
      frame_load_type == WebFrameLoadType::kReloadBypassingCache
          ? mojom::FetchCacheMode::kBypassCache
          : mojom::FetchCacheMode::kValidateCache;
  if (!document_loader_ || !document_loader_->GetHistoryItem())
    return ResourceRequest();

  ResourceRequest request =
      document_loader_->GetHistoryItem()->GenerateResourceRequest(cache_mode);

  // ClientRedirectPolicy is an indication that this load was triggered by some
  // direct interaction with the page. If this reload is not a client redirect,
  // we should reuse the referrer from the original load of the current
  // document. If this reload is a client redirect (e.g., location.reload()), it
  // was initiated by something in the current document and should therefore
  // show the current document's url as the referrer.
  if (client_redirect_policy == ClientRedirectPolicy::kClientRedirect) {
    LocalDOMWindow* window = frame_->DomWindow();
    Referrer referrer = SecurityPolicy::GenerateReferrer(
        window->GetReferrerPolicy(), window->Url(), window->OutgoingReferrer());
    request.SetReferrerString(referrer.referrer);
    request.SetReferrerPolicy(referrer.referrer_policy);
  }

  request.SetSkipServiceWorker(frame_load_type ==
                               WebFrameLoadType::kReloadBypassingCache);
  return request;
}

FrameLoader::ScopedOldDocumentInfoForCommitCapturer*
    FrameLoader::ScopedOldDocumentInfoForCommitCapturer::current_capturer_ =
        nullptr;

FrameLoader::ScopedOldDocumentInfoForCommitCapturer::
    ~ScopedOldDocumentInfoForCommitCapturer() {
  current_capturer_ = previous_capturer_;
}

FrameLoader::FrameLoader(LocalFrame* frame)
    : frame_(frame),
      progress_tracker_(MakeGarbageCollected<ProgressTracker>(frame)),
      dispatching_did_clear_window_object_in_main_world_(false),
      virtual_time_pauser_(
          frame_->GetFrameScheduler()->CreateWebScopedVirtualTimePauser(
              "FrameLoader",
              WebScopedVirtualTimePauser::VirtualTaskDuration::kInstant)) {
  DCHECK(frame_);

  TRACE_EVENT_OBJECT_CREATED_WITH_ID("loading", "FrameLoader", this);
  TakeObjectSnapshot();
}

WebFrameLoadType FrameLoader::HandleInitialEmptyDocumentReplacementIfNeeded(
    const KURL& url,
    WebFrameLoadType frame_load_type) {
  // Converts navigations from the initial empty document to do replacement if
  // needed.
  if (features::IsInitialNavigationEntryEnabled()) {
    // When we have initial NavigationEntries, just checking the original load
    // type and IsOnInitialEmptyDocument() should be enough. Note that we don't
    // convert reloads or history navigations (so only
    // kStandard navigations can get converted to do replacement).
    if (frame_load_type == WebFrameLoadType::kStandard &&
        IsOnInitialEmptyDocument()) {
      frame_load_type = WebFrameLoadType::kReplaceCurrentItem;
    }
    return frame_load_type;
  }

  if (frame_load_type == WebFrameLoadType::kStandard ||
      frame_load_type == WebFrameLoadType::kReplaceCurrentItem) {
    if (frame_->Tree().Parent() && IsOnInitialEmptyDocument()) {
      // Subframe navigations from the initial empty document should always do
      // replacement.
      return WebFrameLoadType::kReplaceCurrentItem;
    }
    if (!frame_->Tree().Parent() && !Client()->BackForwardLength()) {
      // For main frames, currently only empty-URL navigations will be converted
      // to do replacement. Note that this will cause the navigation to be
      // ignored in the browser side, so no NavigationEntry will be added.
      // TODO(https://crbug.com/1215096, https://crbug.com/524208): Make the
      // main frame case follow the behavior of subframes (always replace when
      // navigating from the initial empty document), and that a NavigationEntry
      // will always be created.
      if (Opener() && url.IsEmpty())
        return WebFrameLoadType::kReplaceCurrentItem;
      return WebFrameLoadType::kStandard;
    }
  }
  return frame_load_type;
}

FrameLoader::~FrameLoader() {
  DCHECK_EQ(state_, State::kDetached);
}

void FrameLoader::Trace(Visitor* visitor) const {
  visitor->Trace(frame_);
  visitor->Trace(progress_tracker_);
  visitor->Trace(document_loader_);
}

void FrameLoader::Init(std::unique_ptr<PolicyContainer> policy_container) {
  DCHECK(policy_container);
  ScriptForbiddenScope forbid_scripts;

  // Load the initial empty document:
  auto navigation_params = std::make_unique<WebNavigationParams>();
  navigation_params->url = KURL(g_empty_string);
  navigation_params->frame_policy =
      frame_->Owner() ? frame_->Owner()->GetFramePolicy() : FramePolicy();

  DocumentLoader* new_document_loader = MakeGarbageCollected<DocumentLoader>(
      frame_, kWebNavigationTypeOther, std::move(navigation_params),
      std::move(policy_container), nullptr /* extra_data */);

  CommitDocumentLoader(new_document_loader, nullptr,
                       CommitReason::kInitialization);

  frame_->GetDocument()->CancelParsing();

  // Suppress finish notifications for initial empty documents, since they don't
  // generate start notifications.
  document_loader_->SetSentDidFinishLoad();
  // Ensure that the frame sees the correct page lifecycle state.
  frame_->OnPageLifecycleStateUpdated();

  TakeObjectSnapshot();

  state_ = State::kInitialized;
}

LocalFrameClient* FrameLoader::Client() const {
  return frame_->Client();
}

ClientRedirectPolicy CalculateClientRedirectPolicy(
    ClientNavigationReason client_navigation_reason,
    WebFrameLoadType frame_load_type,
    bool is_on_initial_empty_document) {
  if (is_on_initial_empty_document ||
      client_navigation_reason == ClientNavigationReason::kNone ||
      client_navigation_reason == ClientNavigationReason::kFormSubmissionGet ||
      client_navigation_reason == ClientNavigationReason::kFormSubmissionPost ||
      client_navigation_reason == ClientNavigationReason::kAnchorClick) {
    // Navigations away from the initial empty document and some types of
    // navigations like form submission shouldn't be considered as client
    // redirects, because they're not actually caused by a script redirecting to
    // a different URL.
    return ClientRedirectPolicy::kNotClientRedirect;
  }
  // If the ClientRedirectReason is kFrameNavigation, only treat as a client
  // redirect if the WebFrameLoadType is kReplaceCurrentItem. If this check is
  // not applied, an anchor location change is classified as client redirect
  // and an incorrect redirect chain is formed. On deleting one entry of this
  // redirect chain, the whole chain gets deleted. This result in
  // deletion of multiple items on deleting one item in history.
  // https://crbug.com/1138096
  if (client_navigation_reason == ClientNavigationReason::kFrameNavigation &&
      frame_load_type != WebFrameLoadType::kReplaceCurrentItem)
    return ClientRedirectPolicy::kNotClientRedirect;
  return ClientRedirectPolicy::kClientRedirect;
}

void FrameLoader::SetDefersLoading(LoaderFreezeMode mode) {
  if (frame_->GetDocument())
    frame_->GetDocument()->Fetcher()->SetDefersLoading(mode);
  if (document_loader_)
    document_loader_->SetDefersLoading(mode);
}

void FrameLoader::SaveScrollAnchor() {
  if (!document_loader_ || !document_loader_->GetHistoryItem() ||
      !frame_->View())
    return;

  // Shouldn't clobber anything if we might still restore later.
  if (NeedsHistoryItemRestore(document_loader_->LoadType()) &&
      !document_loader_->GetInitialScrollState().was_scrolled_by_user)
    return;

  HistoryItem* history_item = document_loader_->GetHistoryItem();
  if (ScrollableArea* layout_scrollable_area =
          frame_->View()->LayoutViewport()) {
    ScrollAnchor* scroll_anchor = layout_scrollable_area->GetScrollAnchor();
    DCHECK(scroll_anchor);

    const SerializedAnchor& serialized_anchor =
        scroll_anchor->GetSerializedAnchor();
    if (serialized_anchor.IsValid()) {
      history_item->SetScrollAnchorData(
          {serialized_anchor.selector,
           gfx::PointF(serialized_anchor.relative_offset.X(),
                       serialized_anchor.relative_offset.Y()),
           serialized_anchor.simhash});
    }
  }
}

void FrameLoader::SaveScrollState() {
  if (!document_loader_ || !document_loader_->GetHistoryItem() ||
      !frame_->View())
    return;

  // Shouldn't clobber anything if we might still restore later.
  if (NeedsHistoryItemRestore(document_loader_->LoadType()) &&
      !document_loader_->GetInitialScrollState().was_scrolled_by_user)
    return;

  HistoryItem* history_item = document_loader_->GetHistoryItem();
  // For performance reasons, we don't save scroll anchors as often as we save
  // scroll offsets. In order to avoid keeping around a stale anchor, we clear
  // it when the saved scroll offset changes.
  history_item->SetScrollAnchorData(ScrollAnchorData());
  if (ScrollableArea* layout_scrollable_area = frame_->View()->LayoutViewport())
    history_item->SetScrollOffset(layout_scrollable_area->GetScrollOffset());

  VisualViewport& visual_viewport = frame_->GetPage()->GetVisualViewport();
  if (frame_->IsMainFrame() && visual_viewport.IsActiveViewport()) {
    history_item->SetVisualViewportScrollOffset(
        visual_viewport.VisibleRect().OffsetFromOrigin());
    history_item->SetPageScaleFactor(visual_viewport.Scale());
  }

  Client()->DidUpdateCurrentHistoryItem();
}

void FrameLoader::DispatchUnloadEventAndFillOldDocumentInfoIfNeeded(
    bool will_commit_new_document_in_this_frame) {
  FrameNavigationDisabler navigation_disabler(*frame_);
  SaveScrollState();

  if (SVGImage::IsInSVGImage(frame_->GetDocument()))
    return;

  // Only fill in the info of the unloading document if it is needed for a new
  // document committing in this frame (either due to frame swap or committing
  // a new document in the same FrameLoader). This avoids overwriting the info
  // saved of a parent frame that's already saved in
  // ScopedOldDocumentInfoForCommitCapturer when a child frame is being
  // destroyed due to the parent frame committing. In that case, only the parent
  // frame needs should fill in the info.
  OldDocumentInfoForCommit* old_document_info =
      ScopedOldDocumentInfoForCommitCapturer::CurrentInfo();
  if (!old_document_info || !will_commit_new_document_in_this_frame) {
    frame_->GetDocument()->DispatchUnloadEvents(nullptr);
    return;
  }
  old_document_info->history_item = GetDocumentLoader()->GetHistoryItem();

  frame_->GetDocument()->DispatchUnloadEvents(
      &old_document_info->unload_timing_info);
}

void FrameLoader::DidExplicitOpen() {
  probe::DidOpenDocument(frame_, GetDocumentLoader());
  if (initial_empty_document_status_ ==
      InitialEmptyDocumentStatus::kInitialOrSynchronousAboutBlank) {
    initial_empty_document_status_ = InitialEmptyDocumentStatus::
        kInitialOrSynchronousAboutBlankButExplicitlyOpened;
  }

  // Only model a document.open() as part of a navigation if its parent is not
  // done or in the process of completing.
  if (Frame* parent = frame_->Tree().Parent()) {
    auto* parent_local_frame = DynamicTo<LocalFrame>(parent);
    if ((parent_local_frame &&
         parent_local_frame->GetDocument()->LoadEventStillNeeded()) ||
        (parent->IsRemoteFrame() && parent->IsLoading())) {
      progress_tracker_->ProgressStarted();
    }
  }
}

void FrameLoader::FinishedParsing() {
  if (state_ == State::kUninitialized)
    return;

  progress_tracker_->FinishedParsing();

  frame_->GetLocalFrameHostRemote().DidDispatchDOMContentLoadedEvent();

  if (Client()) {
    ScriptForbiddenScope forbid_scripts;
    Client()->DispatchDidDispatchDOMContentLoadedEvent();
  }

  if (Client()) {
    Client()->RunScriptsAtDocumentReady(
        document_loader_ ? document_loader_->IsCommittedButEmpty() : true);
  }

  if (frame_->View()) {
    ProcessFragment(frame_->GetDocument()->Url(), document_loader_->LoadType(),
                    kNavigationToDifferentDocument);
  }

  frame_->GetDocument()->CheckCompleted();
}

// TODO(dgozman): we are calling this method too often, hoping that it
// does not do anything when navigation is in progress, or when loading
// has finished already. We should call it at the right times.
void FrameLoader::DidFinishNavigation(NavigationFinishState state) {
  if (document_loader_) {
    // Only declare the whole frame finished if the committed navigation is done
    // and there is no provisional navigation in progress.
    // The navigation API may prevent a navigation from completing while waiting
    // for a JS-provided promise to resolve, so check it as well.
    if (!document_loader_->SentDidFinishLoad() || HasProvisionalNavigation())
      return;
    if (auto* navigation_api =
            NavigationApi::navigation(*frame_->DomWindow())) {
      if (navigation_api->HasNonDroppedOngoingNavigation())
        return;
    }
  }

  // This code in this block is meant to prepare a document for display, but
  // this code may also run when swapping out a provisional frame. In that case,
  // skip the display work.
  if (frame_->IsLoading() && !frame_->IsProvisional()) {
    progress_tracker_->ProgressCompleted();
    // Retry restoring scroll offset since finishing loading disables content
    // size clamping.
    RestoreScrollPositionAndViewState();
    if (document_loader_)
      document_loader_->SetLoadType(WebFrameLoadType::kStandard);
    frame_->FinishedLoading(state);
  }

  // When a subframe finishes loading, the parent should check if *all*
  // subframes have finished loading (which may mean that the parent can declare
  // that the parent itself has finished loading).  This local-subframe-focused
  // code has a remote-subframe equivalent in
  // WebRemoteFrameImpl::DidStopLoading.
  Frame* parent = frame_->Tree().Parent();
  if (parent)
    parent->CheckCompleted();
}

Frame* FrameLoader::Opener() {
  return frame_->Opener();
}

void FrameLoader::SetOpener(LocalFrame* opener) {
  // If the frame is already detached, the opener has already been cleared.
  frame_->SetOpener(opener);
}

bool FrameLoader::AllowPlugins() {
  // With Oilpan, a FrameLoader might be accessed after the Page has been
  // detached. FrameClient will not be accessible, so bail early.
  if (!Client())
    return false;
  Settings* settings = frame_->GetSettings();
  return settings && settings->GetPluginsEnabled();
}

void FrameLoader::DetachDocumentLoader(Member<DocumentLoader>& loader,
                                       bool flush_microtask_queue) {
  if (!loader)
    return;

  FrameNavigationDisabler navigation_disabler(*frame_);
  loader->DetachFromFrame(flush_microtask_queue);
  loader = nullptr;
}

void FrameLoader::ProcessScrollForSameDocumentNavigation(
    const KURL& url,
    WebFrameLoadType frame_load_type,
    absl::optional<HistoryItem::ViewState> view_state,
    mojom::blink::ScrollRestorationType scroll_restoration_type) {
  if (view_state) {
    RestoreScrollPositionAndViewState(frame_load_type, *view_state,
                                      scroll_restoration_type);
  }

  // We need to scroll to the fragment whether or not a hash change occurred,
  // since the user might have scrolled since the previous navigation.
  ProcessFragment(url, frame_load_type, kNavigationWithinSameDocument);

  TakeObjectSnapshot();
}

bool FrameLoader::AllowRequestForThisFrame(const FrameLoadRequest& request) {
  // If no origin Document* was specified, skip remaining security checks and
  // assume the caller has fully initialized the FrameLoadRequest.
  if (!request.GetOriginWindow())
    return true;

  const KURL& url = request.GetResourceRequest().Url();
  if (url.ProtocolIsJavaScript()) {
    // Check the CSP of the caller (the "source browsing context") if required,
    // as per https://html.spec.whatwg.org/C/#javascript-protocol.
    bool javascript_url_is_allowed =
        request.GetOriginWindow()
            ->GetContentSecurityPolicyForWorld(request.JavascriptWorld().get())
            ->AllowInline(ContentSecurityPolicy::InlineType::kNavigation,
                          frame_->DeprecatedLocalOwner(), url.GetString(),
                          String() /* nonce */,
                          request.GetOriginWindow()->Url(),
                          OrdinalNumber::First());

    if (!javascript_url_is_allowed)
      return false;

    if (frame_->Owner() && ((frame_->Owner()->GetFramePolicy().sandbox_flags &
                             network::mojom::blink::WebSandboxFlags::kOrigin) !=
                            network::mojom::blink::WebSandboxFlags::kNone)) {
      return false;
    }
  }

  if (!request.CanDisplay(url)) {
    request.GetOriginWindow()->AddConsoleMessage(
        MakeGarbageCollected<ConsoleMessage>(
            mojom::ConsoleMessageSource::kSecurity,
            mojom::ConsoleMessageLevel::kError,
            "Not allowed to load local resource: " + url.ElidedString()));
    return false;
  }
  return true;
}

static WebNavigationType DetermineNavigationType(
    WebFrameLoadType frame_load_type,
    bool is_form_submission,
    bool have_event) {
  bool is_reload = IsReloadLoadType(frame_load_type);
  bool is_back_forward = IsBackForwardLoadType(frame_load_type);
  if (is_form_submission) {
    return (is_reload || is_back_forward) ? kWebNavigationTypeFormResubmitted
                                          : kWebNavigationTypeFormSubmitted;
  }
  if (have_event)
    return kWebNavigationTypeLinkClicked;
  if (is_reload)
    return kWebNavigationTypeReload;
  if (is_back_forward)
    return kWebNavigationTypeBackForward;
  return kWebNavigationTypeOther;
}

static mojom::blink::RequestContextType
DetermineRequestContextFromNavigationType(
    const WebNavigationType navigation_type) {
  switch (navigation_type) {
    case kWebNavigationTypeLinkClicked:
      return mojom::blink::RequestContextType::HYPERLINK;

    case kWebNavigationTypeOther:
      return mojom::blink::RequestContextType::LOCATION;

    case kWebNavigationTypeFormResubmitted:
    case kWebNavigationTypeFormSubmitted:
      return mojom::blink::RequestContextType::FORM;

    case kWebNavigationTypeBackForward:
    case kWebNavigationTypeReload:
      return mojom::blink::RequestContextType::INTERNAL;
  }
  NOTREACHED();
  return mojom::blink::RequestContextType::HYPERLINK;
}

static network::mojom::RequestDestination
DetermineRequestDestinationFromNavigationType(
    const WebNavigationType navigation_type) {
  switch (navigation_type) {
    case kWebNavigationTypeLinkClicked:
    case kWebNavigationTypeOther:
    case kWebNavigationTypeFormResubmitted:
    case kWebNavigationTypeFormSubmitted:
      return network::mojom::RequestDestination::kDocument;
    case kWebNavigationTypeBackForward:
    case kWebNavigationTypeReload:
      return network::mojom::RequestDestination::kEmpty;
  }
  NOTREACHED();
  return network::mojom::RequestDestination::kDocument;
}

void FrameLoader::StartNavigation(FrameLoadRequest& request,
                                  WebFrameLoadType frame_load_type) {
  CHECK(!IsBackForwardLoadType(frame_load_type));
  DCHECK(request.GetTriggeringEventInfo() !=
         mojom::blink::TriggeringEventInfo::kUnknown);
  DCHECK(frame_->GetDocument());
  if (HTMLFrameOwnerElement* element = frame_->DeprecatedLocalOwner())
    element->CancelPendingLazyLoad();

  ResourceRequest& resource_request = request.GetResourceRequest();
  const KURL& url = resource_request.Url();
  LocalDOMWindow* origin_window = request.GetOriginWindow();

  TRACE_EVENT2("navigation", "FrameLoader::StartNavigation", "url",
               url.GetString().Utf8(), "load_type",
               static_cast<int>(frame_load_type));

  resource_request.SetHasUserGesture(
      LocalFrame::HasTransientUserActivation(frame_));

  if (!AllowRequestForThisFrame(request))
    return;

  // Block renderer-initiated loads of filesystem: URLs.
  if (url.ProtocolIs("filesystem") &&
      !base::FeatureList::IsEnabled(features::kFileSystemUrlNavigation)) {
    frame_->GetDocument()->AddConsoleMessage(
        MakeGarbageCollected<ConsoleMessage>(
            mojom::blink::ConsoleMessageSource::kSecurity,
            mojom::blink::ConsoleMessageLevel::kError,
            "Not allowed to navigate to " + url.Protocol() +
                " URL: " + url.ElidedString()));
    return;
  }

  // Block renderer-initiated loads of data: and filesystem: URLs in the top
  // frame (unless they are reload requests).
  //
  // If the mime type of the data URL is supported, the URL will
  // eventually be rendered, so block it here. Otherwise, the load might be
  // handled by a plugin or end up as a download, so allow it to let the
  // embedder figure out what to do with it. Navigations to filesystem URLs are
  // always blocked here.
  if (frame_->IsMainFrame() && origin_window &&
      request.ClientRedirectReason() != ClientNavigationReason::kReload &&
      !frame_->Client()->AllowContentInitiatedDataUrlNavigations(
          origin_window->Url()) &&
      (url.ProtocolIs("filesystem") ||
       (url.ProtocolIsData() &&
        network_utils::IsDataURLMimeTypeSupported(url)))) {
    frame_->GetDocument()->AddConsoleMessage(
        MakeGarbageCollected<ConsoleMessage>(
            mojom::blink::ConsoleMessageSource::kSecurity,
            mojom::blink::ConsoleMessageLevel::kError,
            "Not allowed to navigate top frame to " + url.Protocol() +
                " URL: " + url.ElidedString()));
    return;
  }

  // TODO(dgozman): merge page dismissal check and FrameNavigationDisabler.
  if (!frame_->IsNavigationAllowed() ||
      frame_->GetDocument()->PageDismissalEventBeingDispatched() !=
          Document::kNoDismissal) {
    return;
  }

  if (url.ProtocolIs("filesystem")) {
    document_loader_->CountUse(
        mojom::blink::WebFeature::kFileSystemUrlNavigation);
  }

  frame_load_type = HandleInitialEmptyDocumentReplacementIfNeeded(
      resource_request.Url(), frame_load_type);

  bool same_document_navigation =
      request.GetNavigationPolicy() == kNavigationPolicyCurrentTab &&
      ShouldPerformFragmentNavigation(
          request.Form(), resource_request.HttpMethod(), frame_load_type, url);

  // Perform same document navigation.
  if (same_document_navigation) {
    DCHECK(origin_window);
    document_loader_->CommitSameDocumentNavigation(
        url, frame_load_type, nullptr,
        CalculateClientRedirectPolicy(request.ClientRedirectReason(),
                                      frame_load_type,
                                      IsOnInitialEmptyDocument()),
        resource_request.HasUserGesture(), origin_window->GetSecurityOrigin(),
        /*is_synchronously_committed=*/true, request.GetTriggeringEventInfo(),
        false /* is_browser_initiated */);
    return;
  }

  // If we're navigating and there's still a text fragment permission token on
  // the document loader, it means this navigation didn't try to invoke a text
  // fragment. In this case, we want to propagate this to the next document to
  // allow text-fragments across client-side redirects.
  bool text_fragment_token = GetDocumentLoader()->ConsumeTextFragmentToken();

  resource_request.SetHasTextFragmentToken(text_fragment_token);

  WebNavigationType navigation_type = DetermineNavigationType(
      frame_load_type, resource_request.HttpBody() || request.Form(),
      request.GetTriggeringEventInfo() !=
          mojom::blink::TriggeringEventInfo::kNotFromEvent);
  mojom::blink::RequestContextType request_context_type =
      DetermineRequestContextFromNavigationType(navigation_type);

  // TODO(lyf): handle `frame` context type. https://crbug.com/1019716
  if (mojom::blink::RequestContextType::LOCATION == request_context_type &&
      !frame_->IsMainFrame()) {
    request_context_type = mojom::blink::RequestContextType::IFRAME;
  }
  resource_request.SetRequestContext(request_context_type);
  resource_request.SetRequestDestination(
      DetermineRequestDestinationFromNavigationType(navigation_type));
  request.SetFrameType(frame_->IsMainFrame()
                           ? mojom::RequestContextFrameType::kTopLevel
                           : mojom::RequestContextFrameType::kNested);

  // TODO(arthursonzogni): 'frame-src' check is disabled on the
  // renderer side, but is enforced on the browser side.
  // See http://crbug.com/692595 for understanding why it
  // can't be enforced on both sides instead.

  // 'form-action' check in the frame that is navigating is disabled on the
  // renderer side, but is enforced on the browser side instead.
  // N.B. check in the frame that initiates the navigation stills occurs in
  // blink and is not enforced on the browser-side.
  // TODO(arthursonzogni) The 'form-action' check should be fully disabled
  // in blink, except when the form submission doesn't trigger a navigation
  // (i.e. javascript urls). Please see https://crbug.com/701749.

  // Report-only CSP headers are checked in browser.
  const FetchClientSettingsObject* fetch_client_settings_object = nullptr;
  if (origin_window) {
    fetch_client_settings_object = &origin_window->Fetcher()
                                        ->GetProperties()
                                        .GetFetchClientSettingsObject();
  }
  ModifyRequestForCSP(resource_request, fetch_client_settings_object,
                      origin_window, request.GetFrameType());

  DCHECK(Client()->HasWebView());
  // Check for non-escaped new lines in the url.
  if (url.PotentiallyDanglingMarkup() && url.ProtocolIsInHTTPFamily()) {
    Deprecation::CountDeprecation(
        origin_window, WebFeature::kCanRequestURLHTTPContainingNewline);
    return;
  }

  if (url.ProtocolIsJavaScript()) {
    if (!origin_window ||
        origin_window->CanExecuteScripts(kAboutToExecuteScript)) {
      frame_->GetDocument()->ProcessJavaScriptUrl(url,
                                                  request.JavascriptWorld());
    }
    return;
  }

  if (auto* navigation_api = NavigationApi::navigation(*frame_->DomWindow())) {
    if (request.GetNavigationPolicy() == kNavigationPolicyCurrentTab &&
        (!origin_window || origin_window->GetSecurityOrigin()->CanAccess(
                               frame_->DomWindow()->GetSecurityOrigin()))) {
      NavigationApi::DispatchParams params(
          url, NavigateEventType::kCrossDocument, frame_load_type);
      params.form = request.Form();
      if (request.GetTriggeringEventInfo() ==
          mojom::blink::TriggeringEventInfo::kFromTrustedEvent) {
        params.involvement = UserNavigationInvolvement::kActivation;
      }
      if (navigation_api->DispatchNavigateEvent(params) !=
          NavigationApi::DispatchResult::kContinue) {
        return;
      }
    }
  }

  if (frame_->IsMainFrame())
    LocalFrame::ConsumeTransientUserActivation(frame_);

  // The main resource request gets logged here, because V8DOMActivityLogger
  // is looked up based on the current v8::Context. When the request actually
  // begins, the v8::Context may no longer be on the stack.
  if (V8DOMActivityLogger* activity_logger =
          V8DOMActivityLogger::CurrentActivityLoggerIfIsolatedWorld()) {
    if (!DocumentLoader::WillLoadUrlAsEmpty(url)) {
      Vector<String> argv;
      argv.push_back("Main resource");
      argv.push_back(url.GetString());
      activity_logger->LogEvent("blinkRequestResource", argv.size(),
                                argv.data());
    }
  }

  if (request.ClientRedirectReason() != ClientNavigationReason::kNone) {
    probe::FrameRequestedNavigation(frame_, frame_, url,
                                    request.ClientRedirectReason(),
                                    request.GetNavigationPolicy());
  }

  // TODO(crbug.com/896041): Instead of just bypassing the CSP for navigations
  // from isolated world, ideally we should enforce the isolated world CSP by
  // plumbing the correct CSP to the browser.
  using CSPDisposition = network::mojom::CSPDisposition;
  CSPDisposition should_check_main_world_csp =
      ContentSecurityPolicy::ShouldBypassMainWorldDeprecated(
          request.JavascriptWorld().get())
          ? CSPDisposition::DO_NOT_CHECK
          : CSPDisposition::CHECK;

  // If this is a subframe load to a uuid-in-package: URL, allow loading from a
  // Web Bundle attached to the parent document.
  if (url.Protocol() == "uuid-in-package") {
    auto* parent_local_frame = DynamicTo<LocalFrame>(frame_->Tree().Parent());
    if (parent_local_frame &&
        parent_local_frame->DomWindow() == origin_window &&
        origin_window->Fetcher()) {
      origin_window->Fetcher()->AttachWebBundleTokenIfNeeded(resource_request);
    }
  }

  Client()->BeginNavigation(
      resource_request, request.GetFrameType(), origin_window,
      nullptr /* document_loader */, navigation_type,
      request.GetNavigationPolicy(), frame_load_type,
      CalculateClientRedirectPolicy(
          request.ClientRedirectReason(), frame_load_type,
          IsOnInitialEmptyDocument()) == ClientRedirectPolicy::kClientRedirect,
      request.IsUnfencedTopNavigation(), request.GetTriggeringEventInfo(),
      request.Form(), should_check_main_world_csp, request.GetBlobURLToken(),
      request.GetInputStartTime(), request.HrefTranslate().GetString(),
      request.Impression(), request.GetInitiatorFrameToken(),
      request.TakeSourceLocation(),
      request.TakeInitiatorPolicyContainerKeepAliveHandle());
}

static void FillStaticResponseIfNeeded(WebNavigationParams* params,
                                       LocalFrame* frame) {
  if (params->is_static_data)
    return;

  const KURL& url = params->url;
  // See WebNavigationParams for special case explanations.
  if (url.IsAboutSrcdocURL()) {
    if (params->body_loader)
      return;
    // TODO(wjmaclean): It seems some pathways don't go via the
    // RenderFrameImpl::BeginNavigation/CommitNavigation functions.
    // https://crbug.com/1290435.
    String srcdoc;
    HTMLFrameOwnerElement* owner_element = frame->DeprecatedLocalOwner();
    if (!IsA<HTMLIFrameElement>(owner_element) ||
        !owner_element->FastHasAttribute(html_names::kSrcdocAttr)) {
      // Cannot retrieve srcdoc content anymore (perhaps, the attribute was
      // cleared) - load empty instead.
    } else {
      srcdoc = owner_element->FastGetAttribute(html_names::kSrcdocAttr);
      DCHECK(!srcdoc.IsNull());
    }
    WebNavigationParams::FillStaticResponse(params, "text/html", "UTF-8",
                                            StringUTF8Adaptor(srcdoc));
    return;
  }

  MHTMLArchive* archive = nullptr;
  if (auto* parent = DynamicTo<LocalFrame>(frame->Tree().Parent()))
    archive = parent->Loader().GetDocumentLoader()->Archive();
  if (archive && !url.ProtocolIsData()) {
    // If we have an archive loaded in some ancestor frame, we should
    // retrieve document content from that archive. This is different from
    // loading an archive into this frame, which will be handled separately
    // once we load the body and parse it as an archive.
    params->body_loader.reset();
    ArchiveResource* archive_resource = archive->SubresourceForURL(url);
    if (archive_resource) {
      SharedBuffer* archive_data = archive_resource->Data();
      WebNavigationParams::FillStaticResponse(
          params, archive_resource->MimeType(),
          archive_resource->TextEncoding(),
          base::make_span(archive_data->Data(), archive_data->size()));
    } else {
      // The requested archive resource does not exist. In an ideal world, this
      // would commit as a failed navigation, but the browser doesn't know
      // anything about what resources are available in the archive. Just
      // synthesize an empty document so that something commits still.
      // TODO(https://crbug.com/1112965): remove these special cases by adding
      // an URLLoaderFactory implementation for MHTML archives.
      WebNavigationParams::FillStaticResponse(
          params, "text/html", "UTF-8",
          "<html><body>"
          "<!-- failed to find resource in MHTML archive -->"
          "</body></html>");
    }
  }

  // Checking whether a URL would load as empty (e.g. about:blank) must be done
  // after checking for content with the corresponding URL in the MHTML archive,
  // since MHTML archives can define custom content to load for about:blank...
  //
  // Note that no static response needs to be filled here; instead, this is
  // synthesised later by `DocumentLoader::InitializeEmptyResponse()`.
  if (DocumentLoader::WillLoadUrlAsEmpty(params->url))
    return;

  const String& mime_type = params->response.MimeType();
  if (MIMETypeRegistry::IsSupportedMIMEType(mime_type))
    return;

  PluginData* plugin_data = frame->GetPluginData();
  if (!mime_type.IsEmpty() && plugin_data &&
      plugin_data->SupportsMimeType(mime_type)) {
    return;
  }

  // Typically, PlzNavigate checks that the MIME type can be handled on the
  // browser side before sending it to the renderer. However, there are rare
  // scenarios where it's possible for the renderer to send a commit request
  // with a MIME type the renderer cannot handle:
  //
  // - (hypothetical) some sort of race between enabling/disabling plugins
  //   and when it's checked by the navigation URL loader / handled in the
  //   renderer.
  // - mobile emulation disables plugins on the renderer side, but the browser
  //   navigation code is not aware of this.
  //
  // Similar to the missing archive resource case above, synthesise a resource
  // to commit.
  //
  // WebNavigationParams::FillStaticResponse() fills the response of |params|
  // using |params|'s |url| which is the initial URL even after redirections. So
  // updates the URL to the current URL before calling FillStaticResponse().
  params->url = params->response.CurrentRequestUrl();
  WebNavigationParams::FillStaticResponse(
      params, "text/html", "UTF-8",
      "<html><body>"
      "<!-- no enabled plugin supports this MIME type -->"
      "</body></html>");
}

// The browser navigation code should never send a `CommitNavigation()` request
// that fails this check.
static void AssertCanNavigate(WebNavigationParams* params, LocalFrame* frame) {
  if (params->is_static_data)
    return;

  if (DocumentLoader::WillLoadUrlAsEmpty(params->url))
    return;

  int status_code = params->response.HttpStatusCode();
  // If the server sends 204 or 205, this means the server does not want to
  // replace the page contents. However, PlzNavigate should have handled it
  // browser-side and never sent a commit request to the renderer.
  if (status_code == 204 || status_code == 205)
    CHECK(false);

  // If the server attached a Content-Disposition indicating that the resource
  // is an attachment, this is actually a download. However, PlzNavigate should
  // have handled it browser-side and never sent a commit request to the
  // renderer.
  if (IsContentDispositionAttachment(
          params->response.HttpHeaderField(http_names::kContentDisposition))) {
    CHECK(false);
  }
}

void FrameLoader::CommitNavigation(
    std::unique_ptr<WebNavigationParams> navigation_params,
    std::unique_ptr<WebDocumentLoader::ExtraData> extra_data,
    CommitReason commit_reason) {
  DCHECK(document_loader_);
  DCHECK(frame_->GetDocument());
  DCHECK(Client()->HasWebView());

  if (!frame_->IsNavigationAllowed() ||
      frame_->GetDocument()->PageDismissalEventBeingDispatched() !=
          Document::kNoDismissal) {
    // Any of the checks above should not be necessary.
    // Unfortunately, in the case of sync IPCs like print() there might be
    // reentrancy and, for example, frame detach happening.
    // See fast/loader/detach-while-printing.html for a repro.
    // TODO(https://crbug.com/862088): we should probably ignore print()
    // call in this case instead.
    return;
  }

  // TODO(dgozman): figure out the better place for this check
  // to cancel lazy load both on start and commit. Perhaps
  // CancelProvisionalLoaderForNewNavigation() is a good one.
  HTMLFrameOwnerElement* frame_owner = frame_->DeprecatedLocalOwner();
  if (frame_owner)
    frame_owner->CancelPendingLazyLoad();

  // Note: we might actually classify this navigation as same document
  // right here in the following circumstances:
  // - the loader has already committed a navigation and notified the browser
  //   process which did not receive a message about that just yet;
  // - meanwhile, the browser process sent us a command to commit this new
  //   "cross-document" navigation, while it's actually same-document
  //   with regards to the last commit.
  // In this rare case, we intentionally proceed as cross-document.

  if (!CancelProvisionalLoaderForNewNavigation())
    return;

  if (auto* navigation_api = NavigationApi::navigation(*frame_->DomWindow())) {
    if (navigation_params->frame_load_type == WebFrameLoadType::kBackForward) {
      NavigationApi::DispatchParams params(navigation_params->url,
                                           NavigateEventType::kCrossDocument,
                                           WebFrameLoadType::kBackForward);
      if (navigation_params->is_browser_initiated)
        params.involvement = UserNavigationInvolvement::kBrowserUI;
      params.destination_item = navigation_params->history_item;
      auto result = navigation_api->DispatchNavigateEvent(params);
      DCHECK_EQ(result, NavigationApi::DispatchResult::kContinue);
      if (!document_loader_)
        return;
    }
  }

  FillStaticResponseIfNeeded(navigation_params.get(), frame_);
  AssertCanNavigate(navigation_params.get(), frame_);

  // If this is a javascript: URL or XSLT commit, we must copy the ExtraData
  // from the previous DocumentLoader to ensure the new DocumentLoader behaves
  // the same way as the previous one.
  if (commit_reason == CommitReason::kXSLT ||
      commit_reason == CommitReason::kJavascriptUrl) {
    DCHECK(!extra_data);
    extra_data = document_loader_->TakeExtraData();
  }

  // Fenced frame reporting metadata persists across same-origin navigations
  // initiated from inside the fenced frame. Embedder-initiated navigations
  // use a unique origin (in `FencedFrame::Navigate`), so the requestor is
  // always considered cross-origin by the check (in MPArch).
  bool is_requestor_same_origin =
      !navigation_params->requestor_origin.IsNull() &&
      navigation_params->requestor_origin.IsSameOriginWith(
          WebSecurityOrigin::Create(navigation_params->url));
  if (is_requestor_same_origin) {
    for (const WebNavigationParams::RedirectInfo& redirect :
         navigation_params->redirects) {
      is_requestor_same_origin &=
          navigation_params->requestor_origin.IsSameOriginWith(
              WebSecurityOrigin::Create(redirect.new_url));
    }
  }
  if (is_requestor_same_origin) {
    const mojom::blink::FencedFrameReportingPtr& old_fenced_frame_reporting =
        document_loader_->FencedFrameReporting();
    // TODO(crbug.com/1277593): In ShadowDOM self-urn navigations are allowed,
    // so we need to keep this check in the `if` condition for now.
    if (blink::features::IsFencedFramesMPArchBased()) {
      DCHECK(!navigation_params->fenced_frame_reporting);
    }
    if (!navigation_params->fenced_frame_reporting &&
        old_fenced_frame_reporting) {
      navigation_params->fenced_frame_reporting.emplace();
      for (const auto& [destination, event_type_url] :
           old_fenced_frame_reporting->metadata) {
        base::flat_map<WebString, WebURL> data;
        for (const auto& [event_type, url] : event_type_url) {
          data.emplace(event_type, url);
        }
        navigation_params->fenced_frame_reporting->metadata.emplace(
            destination, std::move(data));
      }
    }
  }

  // Create the OldDocumentInfoForCommit for the old document (that might be in
  // another FrameLoader) and save it in ScopedOldDocumentInfoForCommitCapturer,
  // so that the old document can access it and fill in the information as it
  // is being unloaded/swapped out.
  ScopedOldDocumentInfoForCommitCapturer scoped_old_document_info(
      MakeGarbageCollected<OldDocumentInfoForCommit>(
          SecurityOrigin::Create(navigation_params->url)));

  FrameSwapScope frame_swap_scope(frame_owner);
  {
    base::AutoReset<bool> scoped_committing(&committing_navigation_, true);

    progress_tracker_->ProgressStarted();
    // In DocumentLoader, the matching DidCommitLoad messages are only called
    // for kRegular commits. Skip them here, too, to ensure we match
    // start/commit message pairs.
    if (commit_reason == CommitReason::kRegular) {
      frame_->GetFrameScheduler()->DidStartProvisionalLoad();
      probe::DidStartProvisionalLoad(frame_);
    }

    DCHECK(Client()->HasWebView());

    // If `frame_` is provisional, `DetachDocument()` is largely a no-op other
    // than cleaning up the initial (and unused) empty document. Otherwise, this
    // unloads the previous Document and detaches subframes. If
    // `DetachDocument()` returns false, JS caused `frame_` to be removed, so
    // just return.
    const bool is_provisional = frame_->IsProvisional();
    // For an XSLT document, set SentDidFinishLoad now to prevent the
    // DocumentLoader from reporting an error when detaching the pre-XSLT
    // document.
    if (commit_reason == CommitReason::kXSLT && document_loader_)
      document_loader_->SetSentDidFinishLoad();
    if (!DetachDocument()) {
      DCHECK(!is_provisional);
      return;
    }

    // If the frame is provisional, swap it in now. However, if `SwapIn()`
    // returns false, JS caused `frame_` to be removed, so just return. In case
    // this triggers a local RenderFrame swap, it might trigger the unloading
    // of the old RenderFrame's document, updating the contents of the
    // OldDocumentInfoForCommit set in `scoped_old_document_info` above.
    // NOTE: it's important that SwapIn() happens before DetachDocument(),
    // because this ensures that the unload timing info generated by detaching
    // the provisional frame's document isn't the one that gets used.
    if (is_provisional && !frame_->SwapIn())
      return;
  }

  tls_version_warning_origins_.clear();

  if (!navigation_params->is_synchronous_commit_for_bug_778318 ||
      (!navigation_params->url.IsEmpty() &&
       !KURL(navigation_params->url).IsAboutBlankURL())) {
    // The new document is not the synchronously committed about:blank document,
    // so lose the initial empty document status.
    // Note 1: The actual initial empty document commit (with commit_reason set
    // to CommitReason::kInitialization) won't go through this path since it
    // immediately commits the DocumentLoader, so we only check for the
    // synchronous about:blank commit here.
    // Note 2: Even if the navigation is a synchronous one, it might be a
    // non-about:blank/empty URL commit that is accidentally got caught by the
    // synchronous about:blank path but can't easily be removed due to failing
    // tests/compatibility risk (e.g. about:mumble).
    // TODO(https://crbug.com/1215096): Tighten the conditions in
    // RenderFrameImpl::BeginNavigation() for a navigation to enter the
    // synchronous commit path to only accept about:blank or an empty URL which
    // defaults to about:blank, per the spec:
    // https://html.spec.whatwg.org/multipage/iframe-embed-object.html#the-iframe-element:about:blank
    DCHECK_NE(commit_reason, CommitReason::kInitialization);
    SetIsNotOnInitialEmptyDocument();
  }

  // TODO(dgozman): navigation type should probably be passed by the caller.
  // It seems incorrect to pass |false| for |have_event| and then use
  // determined navigation type to update resource request.
  WebNavigationType navigation_type = DetermineNavigationType(
      navigation_params->frame_load_type,
      !navigation_params->http_body.IsNull(), false /* have_event */);

  std::unique_ptr<PolicyContainer> policy_container;
  if (navigation_params->policy_container) {
    // Javascript and xslt documents should not change the PolicyContainer.
    DCHECK(commit_reason == CommitReason::kRegular);

    policy_container = PolicyContainer::CreateFromWebPolicyContainer(
        std::move(navigation_params->policy_container));
  }
  // TODO(dgozman): get rid of provisional document loader and most of the code
  // below. We should probably call DocumentLoader::CommitNavigation directly.
  DocumentLoader* new_document_loader = MakeGarbageCollected<DocumentLoader>(
      frame_, navigation_type, std::move(navigation_params),
      std::move(policy_container), std::move(extra_data));

  CommitDocumentLoader(
      new_document_loader,
      ScopedOldDocumentInfoForCommitCapturer::CurrentInfo()->history_item,
      commit_reason);

  RestoreScrollPositionAndViewState();

  TakeObjectSnapshot();
}

bool FrameLoader::WillStartNavigation(const WebNavigationInfo& info) {
  if (!CancelProvisionalLoaderForNewNavigation())
    return false;

  progress_tracker_->ProgressStarted();
  client_navigation_ = std::make_unique<ClientNavigationState>();
  client_navigation_->url = info.url_request.Url();
  frame_->GetFrameScheduler()->DidStartProvisionalLoad();
  probe::DidStartProvisionalLoad(frame_);
  virtual_time_pauser_.PauseVirtualTime();
  TakeObjectSnapshot();
  return true;
}

void FrameLoader::StopAllLoaders(bool abort_client) {
  if (!frame_->IsNavigationAllowed() ||
      frame_->GetDocument()->PageDismissalEventBeingDispatched() !=
          Document::kNoDismissal) {
    return;
  }

  // This method could be called from within this method, e.g. through plugin
  // detach. Avoid infinite recursion by disabling navigations.
  FrameNavigationDisabler navigation_disabler(*frame_);

  for (Frame* child = frame_->Tree().FirstChild(); child;
       child = child->Tree().NextSibling()) {
    if (auto* child_local_frame = DynamicTo<LocalFrame>(child))
      child_local_frame->Loader().StopAllLoaders(abort_client);
  }

  frame_->GetDocument()->CancelParsing();
  if (auto* navigation_api = NavigationApi::navigation(*frame_->DomWindow()))
    navigation_api->InformAboutCanceledNavigation();
  if (document_loader_)
    document_loader_->StopLoading();
  if (abort_client)
    CancelClientNavigation();
  else
    ClearClientNavigation();
  frame_->CancelFormSubmission();
  DidFinishNavigation(FrameLoader::NavigationFinishState::kSuccess);

  TakeObjectSnapshot();
}

void FrameLoader::DidAccessInitialDocument() {
  if (frame_->IsMainFrame() && !has_accessed_initial_document_) {
    has_accessed_initial_document_ = true;
    // Forbid script execution to prevent re-entering V8, since this is called
    // from a binding security check.
    ScriptForbiddenScope forbid_scripts;
    frame_->GetPage()->GetChromeClient().DidAccessInitialMainDocument();
  }
}

bool FrameLoader::DetachDocument() {
  DCHECK(frame_->GetDocument());
  DCHECK(document_loader_);

  PluginScriptForbiddenScope forbid_plugin_destructor_scripting;
  ClientNavigationState* client_navigation = client_navigation_.get();

  // Don't allow this frame to navigate anymore. This line is needed for
  // navigation triggered from children's unload handlers. Blocking navigations
  // triggered from this frame's unload handler is already covered in
  // DispatchUnloadEventAndFillOldDocumentInfoIfNeeded().
  FrameNavigationDisabler navigation_disabler(*frame_);
  // Don't allow any new child frames to load in this frame: attaching a new
  // child frame during or after detaching children results in an attached frame
  // on a detached DOM tree, which is bad.
  SubframeLoadingDisabler disabler(frame_->GetDocument());
  // https://html.spec.whatwg.org/C/browsing-the-web.html#unload-a-document
  // The ignore-opens-during-unload counter of a Document must be incremented
  // both when unloading itself and when unloading its descendants.
  IgnoreOpensDuringUnloadCountIncrementer ignore_opens_during_unload(
      frame_->GetDocument());
  DispatchUnloadEventAndFillOldDocumentInfoIfNeeded(
      true /* will_commit_new_document_in_this_frame */);
  frame_->DetachChildren();
  // The previous calls to DispatchUnloadEventAndFillOldDocumentInfoIfNeeded()
  // and detachChildren() can execute arbitrary script via things like unload
  // events. If the executed script causes the current frame to be detached, we
  // need to abandon the current load.
  if (!frame_->Client())
    return false;
  // FrameNavigationDisabler should prevent another load from starting.
  DCHECK_EQ(client_navigation_.get(), client_navigation);
  // Detaching the document loader will abort XHRs that haven't completed, which
  // can trigger event listeners for 'abort'. These event listeners might call
  // window.stop(), which will in turn detach the provisional document loader.
  // At this point, the provisional document loader should not detach, because
  // then the FrameLoader would not have any attached DocumentLoaders. This is
  // guaranteed by FrameNavigationDisabler above.
  DetachDocumentLoader(document_loader_, true);
  // 'abort' listeners can also detach the frame.
  if (!frame_->Client())
    return false;
  // FrameNavigationDisabler should prevent another load from starting.
  DCHECK_EQ(client_navigation_.get(), client_navigation);

  // No more events will be dispatched so detach the Document.
  // TODO(dcheng): Why is this a conditional check?
  // TODO(yoav): Should we also be nullifying domWindow's document (or
  // domWindow) since the doc is now detached?
  frame_->GetDocument()->Shutdown();
  document_loader_ = nullptr;

  return true;
}

void FrameLoader::CommitDocumentLoader(DocumentLoader* document_loader,
                                       HistoryItem* previous_history_item,
                                       CommitReason commit_reason) {
  TRACE_EVENT("blink", "FrameLoader::CommitDocumentLoader");
  document_loader_ = document_loader;
  CHECK(document_loader_);

  document_loader_->SetCommitReason(commit_reason);

  virtual_time_pauser_.PauseVirtualTime();
  document_loader_->StartLoading();
  virtual_time_pauser_.UnpauseVirtualTime();

  if (commit_reason != CommitReason::kInitialization) {
    // Following the call to StartLoading, the DocumentLoader state has taken
    // into account all redirects that happened during navigation. Its
    // HistoryItem can be properly updated for the commit, using the HistoryItem
    // of the previous Document.
    document_loader_->SetHistoryItemStateForCommit(
        previous_history_item, document_loader_->LoadType(),
        DocumentLoader::HistoryNavigationType::kDifferentDocument,
        commit_reason);
  }

  // Update the DocumentLoadTiming with the timings from the previous document
  // unload event.
  if (OldDocumentInfoForCommit* old_document_info =
          ScopedOldDocumentInfoForCommitCapturer::CurrentInfo()) {
    if (old_document_info->unload_timing_info.unload_timing.has_value()) {
      document_loader_->GetTiming().SetCanRequestFromPreviousDocument(
          old_document_info->unload_timing_info.unload_timing->can_request);
      document_loader_->GetTiming().MarkUnloadEventStart(
          old_document_info->unload_timing_info.unload_timing
              ->unload_event_start);
      document_loader_->GetTiming().MarkUnloadEventEnd(
          old_document_info->unload_timing_info.unload_timing
              ->unload_event_end);
      document_loader_->GetTiming().MarkCommitNavigationEnd();
    }
  }

  TakeObjectSnapshot();

  Client()->TransitionToCommittedForNewPage();

  document_loader_->CommitNavigation();
}

void FrameLoader::RestoreScrollPositionAndViewState() {
  if (!frame_->GetPage() || !GetDocumentLoader() ||
      !GetDocumentLoader()->GetHistoryItem() ||
      !GetDocumentLoader()->GetHistoryItem()->GetViewState() ||
      !GetDocumentLoader()->NavigationScrollAllowed()) {
    return;
  }
  RestoreScrollPositionAndViewState(
      GetDocumentLoader()->LoadType(),
      *GetDocumentLoader()->GetHistoryItem()->GetViewState(),
      GetDocumentLoader()->GetHistoryItem()->ScrollRestorationType());
}

void FrameLoader::RestoreScrollPositionAndViewState(
    WebFrameLoadType load_type,
    const HistoryItem::ViewState& view_state,
    mojom::blink::ScrollRestorationType scroll_restoration_type) {
  LocalFrameView* view = frame_->View();
  if (!view || !view->LayoutViewport() || !frame_->IsAttached() ||
      frame_->GetDocument()->IsInitialEmptyDocument()) {
    return;
  }
  if (!NeedsHistoryItemRestore(load_type))
    return;

  view->LayoutViewport()->SetPendingHistoryRestoreScrollOffset(
      view_state,
      scroll_restoration_type != mojom::blink::ScrollRestorationType::kManual);
  view->GetScrollableArea()->SetPendingHistoryRestoreScrollOffset(
      view_state,
      scroll_restoration_type != mojom::blink::ScrollRestorationType::kManual);

  view->ScheduleAnimation();
}

String FrameLoader::ApplyUserAgentOverrideAndLog(
    const String& user_agent) const {
  String user_agent_override;
  probe::ApplyUserAgentOverride(probe::ToCoreProbeSink(frame_->GetDocument()),
                                &user_agent_override);

  if (Client()->UserAgentOverride().IsEmpty() &&
      user_agent_override.IsEmpty()) {
    return user_agent;
  }

  if (user_agent_override.IsEmpty()) {
    user_agent_override = user_agent;
  }

  if (base::FeatureList::IsEnabled(
          blink::features::kUserAgentOverrideExperiment)) {
    String ua_original = Platform::Current()->UserAgent();

    auto it = user_agent_override.Find(ua_original);
    UserAgentOverride::UserAgentOverrideHistogram histogram =
        UserAgentOverride::UserAgentOverrideHistogram::UserAgentOverriden;
    if (it == 0) {
      histogram = UserAgentOverride::UserAgentOverrideHistogram::
          UserAgentOverrideSuffix;
    } else if (it != kNotFound) {
      histogram = UserAgentOverride::UserAgentOverrideHistogram::
          UserAgentOverrideSubstring;
    }

    if (document_loader_) {
      document_loader_->GetUseCounter().CountUserAgentOverride(histogram,
                                                               frame_.Get());
    }
  }

  return user_agent_override;
}

String FrameLoader::UserAgent() const {
  return ApplyUserAgentOverrideAndLog(Client()->UserAgent());
}

String FrameLoader::FullUserAgent() const {
  return ApplyUserAgentOverrideAndLog(Client()->FullUserAgent());
}

String FrameLoader::ReducedUserAgent() const {
  return ApplyUserAgentOverrideAndLog(Client()->ReducedUserAgent());
}

absl::optional<blink::UserAgentMetadata> FrameLoader::UserAgentMetadata()
    const {
  return Client()->UserAgentMetadata();
}

void FrameLoader::Detach() {
  frame_->GetDocument()->CancelParsing();
  DetachDocumentLoader(document_loader_);
  ClearClientNavigation();
  committing_navigation_ = false;
  DidFinishNavigation(FrameLoader::NavigationFinishState::kSuccess);

  if (progress_tracker_) {
    progress_tracker_->Dispose();
    progress_tracker_.Clear();
  }

  TRACE_EVENT_OBJECT_DELETED_WITH_ID("loading", "FrameLoader", this);
  state_ = State::kDetached;
  virtual_time_pauser_.UnpauseVirtualTime();
}

bool FrameLoader::ShouldPerformFragmentNavigation(bool is_form_submission,
                                                  const String& http_method,
                                                  WebFrameLoadType load_type,
                                                  const KURL& url) {
  // We don't do this if we are submitting a form with method other than "GET",
  // explicitly reloading, currently displaying a frameset, or if the URL does
  // not have a fragment.
  return EqualIgnoringASCIICase(http_method, http_names::kGET) &&
         !IsReloadLoadType(load_type) &&
         load_type != WebFrameLoadType::kBackForward &&
         url.HasFragmentIdentifier() &&
         // For provisional LocalFrame, there is no real document loaded and
         // the initial empty document should not be considered, so there is
         // no way to get a same-document load in this case.
         !frame_->IsProvisional() &&
         EqualIgnoringFragmentIdentifier(frame_->GetDocument()->Url(), url)
         // We don't want to just scroll if a link from within a frameset is
         // trying to reload the frameset into _top.
         && !frame_->GetDocument()->IsFrameSet();
}

void FrameLoader::ProcessFragment(const KURL& url,
                                  WebFrameLoadType frame_load_type,
                                  LoadStartType load_start_type) {
  LocalFrameView* view = frame_->View();
  if (!view)
    return;

  const bool is_same_document_navigation =
      load_start_type == kNavigationWithinSameDocument;

  // Pages can opt-in to manual scroll restoration so the page will handle
  // restoring the past scroll offset during a history navigation. In these
  // cases we assume the scroll was restored from history (by the page).
  const bool uses_manual_scroll_restoration =
      frame_load_type == WebFrameLoadType::kBackForward &&
      GetDocumentLoader()->GetHistoryItem() &&
      GetDocumentLoader()->GetHistoryItem()->ScrollRestorationType() ==
          mojom::blink::ScrollRestorationType::kManual;

  // If we restored a scroll position from history, we shouldn't clobber it
  // with the fragment.
  const bool will_restore_scroll_from_history =
      GetDocumentLoader()->GetInitialScrollState().did_restore_from_history ||
      uses_manual_scroll_restoration;

  // Scrolling at load can be blocked by document policy. This policy applies
  // only to cross-document navigations.
  const bool blocked_by_policy =
      !is_same_document_navigation &&
      !GetDocumentLoader()->NavigationScrollAllowed();

  // We should avoid scrolling the fragment if it would clobber a history
  // restored scroll state but still allow it on same document navigations
  // after (i.e. if we navigate back and restore the scroll position, the user
  // should still be able to click on a same-document fragment link and have it
  // jump to the anchor).
  const bool is_same_document_non_history_nav =
      is_same_document_navigation && !IsBackForwardLoadType(frame_load_type);

  const bool block_fragment_scroll =
      blocked_by_policy ||
      (will_restore_scroll_from_history && !is_same_document_non_history_nav);

  view->ProcessUrlFragment(url, is_same_document_navigation,
                           !block_fragment_scroll);
}

bool FrameLoader::ShouldClose(bool is_reload) {
  Page* page = frame_->GetPage();
  if (!page || !page->GetChromeClient().CanOpenBeforeUnloadConfirmPanel())
    return true;

  HeapVector<Member<LocalFrame>> descendant_frames;
  for (Frame* child = frame_->Tree().FirstChild(); child;
       child = child->Tree().TraverseNext(frame_)) {
    // FIXME: There is not yet any way to dispatch events to out-of-process
    // frames.
    if (auto* child_local_frame = DynamicTo<LocalFrame>(child))
      descendant_frames.push_back(child_local_frame);
  }

  {
    FrameNavigationDisabler navigation_disabler(*frame_);
    bool did_allow_navigation = false;

    // https://html.spec.whatwg.org/C/browsing-the-web.html#prompt-to-unload-a-document

    // First deal with this frame.
    IgnoreOpensDuringUnloadCountIncrementer ignore_opens_during_unload(
        frame_->GetDocument());
    if (!frame_->GetDocument()->DispatchBeforeUnloadEvent(
            &page->GetChromeClient(), is_reload, did_allow_navigation)) {
      if (auto* navigation_api =
              NavigationApi::navigation(*frame_->DomWindow()))
        navigation_api->InformAboutCanceledNavigation();
      return false;
    }

    // Then deal with descendent frames.
    for (Member<LocalFrame>& descendant_frame : descendant_frames) {
      if (!descendant_frame->Tree().IsDescendantOf(frame_))
        continue;

      // There is some confusion in the spec around what counters should be
      // incremented for a descendant browsing context:
      // https://github.com/whatwg/html/issues/3899
      //
      // Here for implementation ease, we use the current spec behavior, which
      // is to increment only the counter of the Document on which this is
      // called, and that of the Document we are firing the beforeunload event
      // on -- not any intermediate Documents that may be the parent of the
      // frame being unloaded but is not root Document.
      IgnoreOpensDuringUnloadCountIncrementer
          ignore_opens_during_unload_descendant(
              descendant_frame->GetDocument());
      if (!descendant_frame->GetDocument()->DispatchBeforeUnloadEvent(
              &page->GetChromeClient(), is_reload, did_allow_navigation)) {
        if (auto* navigation_api =
                NavigationApi::navigation(*frame_->DomWindow()))
          navigation_api->InformAboutCanceledNavigation();
        return false;
      }
    }
  }

  // Now that none of the unloading frames canceled the BeforeUnload, tell each
  // of them so they can advance to the appropriate load state.
  frame_->GetDocument()->BeforeUnloadDoneWillUnload();
  for (Member<LocalFrame>& descendant_frame : descendant_frames) {
    if (!descendant_frame->Tree().IsDescendantOf(frame_))
      continue;
    descendant_frame->GetDocument()->BeforeUnloadDoneWillUnload();
  }

  return true;
}

void FrameLoader::DidDropNavigation() {
  if (!client_navigation_)
    return;
  // TODO(dgozman): should we ClearClientNavigation instead and not
  // notify the client in response to its own call?
  CancelClientNavigation(CancelNavigationReason::kDropped);
  DidFinishNavigation(FrameLoader::NavigationFinishState::kSuccess);

  // Forcibly instantiate WindowProxy for initial frame document.
  // This is only required when frame navigation is aborted, e.g. due to
  // mixed content.
  // TODO(lushnikov): this should be done in Init for initial empty doc, but
  // that breaks extensions abusing SetForceMainWorldInitialization setting
  // and relying on the number of created window proxies.
  Settings* settings = frame_->GetSettings();
  if (settings && settings->GetForceMainWorldInitialization()) {
    // Forcibly instantiate WindowProxy.
    frame_->DomWindow()->GetScriptController().WindowProxy(
        DOMWrapperWorld::MainWorld());
  }
}

bool FrameLoader::CancelProvisionalLoaderForNewNavigation() {
  // This seems to correspond to step 9 of the specification:
  // "9. Abort the active document of browsingContext."
  // https://html.spec.whatwg.org/C/#navigate
  frame_->GetDocument()->Abort();
  // document.onreadystatechange can fire in Abort(), which can:
  // 1) Detach this frame.
  // 2) Stop the provisional DocumentLoader (i.e window.stop()).
  if (!frame_->GetPage())
    return false;

  // For client navigations, don't send failure callbacks when simply
  // replacing client navigation with a DocumentLoader.
  ClearClientNavigation();

  // Cancel pending form submissions so they don't take precedence over this.
  frame_->CancelFormSubmission();

  return true;
}

void FrameLoader::ClearClientNavigation() {
  if (!client_navigation_)
    return;
  client_navigation_.reset();
  probe::DidFailProvisionalLoad(frame_);
  virtual_time_pauser_.UnpauseVirtualTime();
}

void FrameLoader::CancelClientNavigation(CancelNavigationReason reason) {
  if (!client_navigation_)
    return;

  if (auto* navigation_api = NavigationApi::navigation(*frame_->DomWindow()))
    navigation_api->InformAboutCanceledNavigation(reason);

  ResourceError error = ResourceError::CancelledError(client_navigation_->url);
  ClearClientNavigation();
  if (WebPluginContainerImpl* plugin = frame_->GetWebPluginContainer())
    plugin->DidFailLoading(error);
  Client()->AbortClientNavigation();
}

void FrameLoader::DispatchDocumentElementAvailable() {
  ScriptForbiddenScope forbid_scripts;

  // Notify the browser about non-blank documents loading in the top frame.
  KURL url = frame_->GetDocument()->Url();
  if (url.IsValid() && !url.IsAboutBlankURL()) {
    if (frame_->IsMainFrame()) {
      // For now, don't remember plugin zoom values.  We don't want to mix them
      // with normal web content (i.e. a fixed layout plugin would usually want
      // them different).
      frame_->GetLocalFrameHostRemote().MainDocumentElementAvailable(
          frame_->GetDocument()->IsPluginDocument());
    }
  }

  Client()->DocumentElementAvailable();
}

void FrameLoader::RunScriptsAtDocumentElementAvailable() {
  Client()->RunScriptsAtDocumentElementAvailable();
  // The frame might be detached at this point.
}

void FrameLoader::DispatchDidClearDocumentOfWindowObject() {
  if (state_ == State::kUninitialized)
    return;

  Settings* settings = frame_->GetSettings();
  LocalDOMWindow* window = frame_->DomWindow();
  if (settings && settings->GetForceMainWorldInitialization()) {
    // Forcibly instantiate WindowProxy, even if script is disabled.
    window->GetScriptController().WindowProxy(DOMWrapperWorld::MainWorld());
  }
  probe::DidClearDocumentOfWindowObject(frame_);
  if (!window->CanExecuteScripts(kNotAboutToExecuteScript))
    return;

  if (dispatching_did_clear_window_object_in_main_world_)
    return;
  base::AutoReset<bool> in_did_clear_window_object(
      &dispatching_did_clear_window_object_in_main_world_, true);
  // We just cleared the document, not the entire window object, but for the
  // embedder that's close enough.
  Client()->DispatchDidClearWindowObjectInMainWorld();
}

void FrameLoader::DispatchDidClearWindowObjectInMainWorld() {
  if (!frame_->DomWindow()->CanExecuteScripts(kNotAboutToExecuteScript))
    return;

  if (dispatching_did_clear_window_object_in_main_world_)
    return;
  base::AutoReset<bool> in_did_clear_window_object(
      &dispatching_did_clear_window_object_in_main_world_, true);
  Client()->DispatchDidClearWindowObjectInMainWorld();
}

network::mojom::blink::WebSandboxFlags
FrameLoader::PendingEffectiveSandboxFlags() const {
  if (Frame* parent = frame_->Tree().Parent()) {
    return parent->GetSecurityContext()->GetSandboxFlags() |
           frame_->Owner()->GetFramePolicy().sandbox_flags;
  } else {
    return frame_->OpenerSandboxFlags();
  }
}

void FrameLoader::ModifyRequestForCSP(
    ResourceRequest& resource_request,
    const FetchClientSettingsObject* fetch_client_settings_object,
    LocalDOMWindow* window_for_logging,
    mojom::RequestContextFrameType frame_type) const {
  // Tack an 'Upgrade-Insecure-Requests' header to outgoing navigational
  // requests, as described in
  // https://w3c.github.io/webappsec-upgrade-insecure-requests/#feature-detect
  if (frame_type != mojom::RequestContextFrameType::kNone) {
    // Early return if the request has already been upgraded.
    if (!resource_request.HttpHeaderField(http_names::kUpgradeInsecureRequests)
             .IsNull()) {
      return;
    }

    resource_request.SetHttpHeaderField(http_names::kUpgradeInsecureRequests,
                                        "1");
  }

  MixedContentChecker::UpgradeInsecureRequest(
      resource_request, fetch_client_settings_object, window_for_logging,
      frame_type, frame_->GetContentSettingsClient());
}

void FrameLoader::ReportLegacyTLSVersion(const KURL& url,
                                         bool is_subresource,
                                         bool is_ad_resource) {
  document_loader_->GetUseCounter().Count(
      is_subresource
          ? WebFeature::kLegacyTLSVersionInSubresource
          : (frame_->IsOutermostMainFrame()
                 ? WebFeature::kLegacyTLSVersionInMainFrameResource
                 : WebFeature::kLegacyTLSVersionInSubframeMainResource),
      frame_.Get());

  // For non-main-frame loads, we have to use the main frame's document for
  // the UKM recorder and source ID.
  auto& root = frame_->LocalFrameRoot();
  ukm::builders::Net_LegacyTLSVersion(root.GetDocument()->UkmSourceID())
      .SetIsMainFrame(frame_->IsMainFrame())
      .SetIsSubresource(is_subresource)
      .SetIsAdResource(is_ad_resource)
      .Record(root.GetDocument()->UkmRecorder());

  String origin = SecurityOrigin::Create(url)->ToString();
  // To prevent log spam, only log the message once per origin.
  if (tls_version_warning_origins_.Contains(origin))
    return;

  // After |kMaxSecurityWarningMessages| warnings, stop printing messages to the
  // console. At exactly |kMaxSecurityWarningMessages| warnings, print a message
  // that additional resources on the page use legacy certificates without
  // specifying which exact resources. Before |kMaxSecurityWarningMessages|
  // messages, print the exact resource URL in the message to help the developer
  // pinpoint the problematic resources.
  const size_t kMaxSecurityWarningMessages = 10;
  size_t num_warnings = tls_version_warning_origins_.size();
  if (num_warnings > kMaxSecurityWarningMessages)
    return;

  String console_message;
  if (num_warnings == kMaxSecurityWarningMessages) {
    console_message =
        "Additional resources on this page were loaded with TLS 1.0 or TLS "
        "1.1, which are deprecated and will be disabled in the future. Once "
        "disabled, users will be prevented from loading these resources. "
        "Servers should enable TLS 1.2 or later. See "
        "https://www.chromestatus.com/feature/5654791610957824 for more "
        "information.";
  } else {
    console_message =
        "The connection used to load resources from " + origin +
        " used TLS 1.0 or TLS "
        "1.1, which are deprecated and will be disabled in the future. Once "
        "disabled, users will be prevented from loading these resources. The "
        "server should enable TLS 1.2 or later. See "
        "https://www.chromestatus.com/feature/5654791610957824 for more "
        "information.";
  }
  tls_version_warning_origins_.insert(origin);
  // To avoid spamming the console, use verbose message level for subframe
  // resources, and only use the warning level for main-frame resources.
  frame_->Console().AddMessage(MakeGarbageCollected<ConsoleMessage>(
      mojom::ConsoleMessageSource::kOther,
      frame_->IsOutermostMainFrame() ? mojom::ConsoleMessageLevel::kWarning
                                     : mojom::ConsoleMessageLevel::kVerbose,
      console_message));
}

void FrameLoader::WriteIntoTrace(perfetto::TracedValue context) const {
  auto dict = std::move(context).WriteDictionary();
  {
    auto frame_dict = dict.AddDictionary("frame");
    frame_dict.Add("id_ref", IdentifiersFactory::FrameId(frame_.Get()));
  }
  dict.Add("isLoadingMainFrame", frame_->IsMainFrame());
  dict.Add("isOutermostMainFrame", frame_->IsOutermostMainFrame());
  dict.Add("documentLoaderURL",
           document_loader_ ? document_loader_->Url().GetString() : String());
}

inline void FrameLoader::TakeObjectSnapshot() const {
  if (state_ == State::kDetached) {
    // We already logged TRACE_EVENT_OBJECT_DELETED_WITH_ID in detach().
    return;
  }
  TRACE_EVENT_OBJECT_SNAPSHOT_WITH_ID("loading", "FrameLoader", this, this);
}

mojo::PendingRemote<blink::mojom::CodeCacheHost>
FrameLoader::CreateWorkerCodeCacheHost() {
  if (!document_loader_)
    return mojo::NullRemote();
  return document_loader_->CreateWorkerCodeCacheHost();
}

FrameLoader::OldDocumentInfoForCommit::OldDocumentInfoForCommit(
    scoped_refptr<SecurityOrigin> new_document_origin)
    : unload_timing_info(
          UnloadEventTimingInfo(std::move(new_document_origin))) {}

void FrameLoader::OldDocumentInfoForCommit::Trace(Visitor* visitor) const {
  visitor->Trace(history_item);
}

}  // namespace blink
