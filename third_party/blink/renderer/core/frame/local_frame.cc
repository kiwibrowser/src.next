/*
 * Copyright (C) 1998, 1999 Torben Weis <weis@kde.org>
 *                     1999 Lars Knoll <knoll@kde.org>
 *                     1999 Antti Koivisto <koivisto@kde.org>
 *                     2000 Simon Hausmann <hausmann@kde.org>
 *                     2000 Stefan Schimanski <1Stein@gmx.de>
 *                     2001 George Staikos <staikos@kde.org>
 * Copyright (C) 2004, 2005, 2006, 2007, 2008, 2009, 2010, 2011 Apple Inc. All
 * rights reserved.
 * Copyright (C) 2005 Alexey Proskuryakov <ap@nypop.com>
 * Copyright (C) 2008 Nokia Corporation and/or its subsidiary(-ies)
 * Copyright (C) 2008 Eric Seidel <eric@webkit.org>
 * Copyright (C) 2008 Google Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include "third_party/blink/renderer/core/frame/local_frame.h"

#include <limits>
#include <memory>
#include <utility>

#include "base/metrics/histogram_functions.h"
#include "base/unguessable_token.h"
#include "base/values.h"
#include "build/build_config.h"
#include "mojo/public/cpp/bindings/self_owned_receiver.h"
#include "mojo/public/cpp/system/message_pipe.h"
#include "services/data_decoder/public/mojom/resource_snapshot_for_web_bundle.mojom-blink.h"
#include "services/network/public/cpp/features.h"
#include "services/network/public/mojom/content_security_policy.mojom-blink.h"
#include "services/network/public/mojom/source_location.mojom-blink.h"
#include "skia/public/mojom/skcolor.mojom-blink.h"
#include "third_party/blink/public/common/associated_interfaces/associated_interface_provider.h"
#include "third_party/blink/public/common/browser_interface_broker_proxy.h"
#include "third_party/blink/public/common/chrome_debug_urls.h"
#include "third_party/blink/public/common/features.h"
#include "third_party/blink/public/common/input/web_input_event_attribution.h"
#include "third_party/blink/public/common/thread_safe_browser_interface_broker_proxy.h"
#include "third_party/blink/public/mojom/blob/blob_url_store.mojom-blink.h"
#include "third_party/blink/public/mojom/devtools/inspector_issue.mojom-blink.h"
#include "third_party/blink/public/mojom/favicon/favicon_url.mojom-blink.h"
#include "third_party/blink/public/mojom/fetch/fetch_api_request.mojom-blink.h"
#include "third_party/blink/public/mojom/frame/blocked_navigation_types.mojom-blink.h"
#include "third_party/blink/public/mojom/frame/frame.mojom-blink.h"
#include "third_party/blink/public/mojom/frame/frame_owner_properties.mojom-blink.h"
#include "third_party/blink/public/mojom/frame/lifecycle.mojom-blink.h"
#include "third_party/blink/public/mojom/frame/media_player_action.mojom-blink.h"
#include "third_party/blink/public/mojom/frame/reporting_observer.mojom-blink.h"
#include "third_party/blink/public/mojom/frame/user_activation_notification_type.mojom-shared.h"
#include "third_party/blink/public/mojom/scroll/scrollbar_mode.mojom-blink.h"
#include "third_party/blink/public/platform/interface_registry.h"
#include "third_party/blink/public/platform/platform.h"
#include "third_party/blink/public/platform/scheduler/web_resource_loading_task_runner_handle.h"
#include "third_party/blink/public/platform/url_conversion.h"
#include "third_party/blink/public/platform/web_content_settings_client.h"
#include "third_party/blink/public/platform/web_string.h"
#include "third_party/blink/public/platform/web_url_request.h"
#include "third_party/blink/public/platform/web_vector.h"
#include "third_party/blink/public/web/web_content_capture_client.h"
#include "third_party/blink/public/web/web_frame.h"
#include "third_party/blink/public/web/web_local_frame_client.h"
#include "third_party/blink/renderer/bindings/core/v8/script_controller.h"
#include "third_party/blink/renderer/bindings/core/v8/source_location.h"
#include "third_party/blink/renderer/bindings/core/v8/v8_binding_for_core.h"
#include "third_party/blink/renderer/core/clipboard/system_clipboard.h"
#include "third_party/blink/renderer/core/content_capture/content_capture_manager.h"
#include "third_party/blink/renderer/core/core_export.h"
#include "third_party/blink/renderer/core/core_initializer.h"
#include "third_party/blink/renderer/core/core_probe_sink.h"
#include "third_party/blink/renderer/core/css/background_color_paint_image_generator.h"
#include "third_party/blink/renderer/core/css/box_shadow_paint_image_generator.h"
#include "third_party/blink/renderer/core/css/clip_path_paint_image_generator.h"
#include "third_party/blink/renderer/core/css/document_style_environment_variables.h"
#include "third_party/blink/renderer/core/css/style_change_reason.h"
#include "third_party/blink/renderer/core/dom/child_frame_disconnector.h"
#include "third_party/blink/renderer/core/dom/document_init.h"
#include "third_party/blink/renderer/core/dom/document_parser.h"
#include "third_party/blink/renderer/core/dom/document_type.h"
#include "third_party/blink/renderer/core/dom/dom_node_ids.h"
#include "third_party/blink/renderer/core/dom/events/event.h"
#include "third_party/blink/renderer/core/dom/ignore_opens_during_unload_count_incrementer.h"
#include "third_party/blink/renderer/core/editing/editing_utilities.h"
#include "third_party/blink/renderer/core/editing/editor.h"
#include "third_party/blink/renderer/core/editing/frame_selection.h"
#include "third_party/blink/renderer/core/editing/ime/input_method_controller.h"
#include "third_party/blink/renderer/core/editing/serializers/create_markup_options.h"
#include "third_party/blink/renderer/core/editing/serializers/serialization.h"
#include "third_party/blink/renderer/core/editing/spellcheck/spell_checker.h"
#include "third_party/blink/renderer/core/editing/suggestion/text_suggestion_controller.h"
#include "third_party/blink/renderer/core/editing/surrounding_text.h"
#include "third_party/blink/renderer/core/editing/visible_position.h"
#include "third_party/blink/renderer/core/event_type_names.h"
#include "third_party/blink/renderer/core/execution_context/security_context.h"
#include "third_party/blink/renderer/core/execution_context/window_agent.h"
#include "third_party/blink/renderer/core/exported/web_plugin_container_impl.h"
#include "third_party/blink/renderer/core/fileapi/public_url_manager.h"
#include "third_party/blink/renderer/core/fragment_directive/text_fragment_handler.h"
#include "third_party/blink/renderer/core/frame/ad_tracker.h"
#include "third_party/blink/renderer/core/frame/attribution_src_loader.h"
#include "third_party/blink/renderer/core/frame/csp/content_security_policy.h"
#include "third_party/blink/renderer/core/frame/event_handler_registry.h"
#include "third_party/blink/renderer/core/frame/frame_console.h"
#include "third_party/blink/renderer/core/frame/frame_overlay.h"
#include "third_party/blink/renderer/core/frame/frame_serializer.h"
#include "third_party/blink/renderer/core/frame/frame_serializer_delegate_impl.h"
#include "third_party/blink/renderer/core/frame/local_dom_window.h"
#include "third_party/blink/renderer/core/frame/local_frame_client.h"
#include "third_party/blink/renderer/core/frame/local_frame_mojo_handler.h"
#include "third_party/blink/renderer/core/frame/local_frame_view.h"
#include "third_party/blink/renderer/core/frame/page_scale_constraints_set.h"
#include "third_party/blink/renderer/core/frame/pausable_script_executor.h"
#include "third_party/blink/renderer/core/frame/performance_monitor.h"
#include "third_party/blink/renderer/core/frame/picture_in_picture_controller.h"
#include "third_party/blink/renderer/core/frame/remote_frame.h"
#include "third_party/blink/renderer/core/frame/remote_frame_owner.h"
#include "third_party/blink/renderer/core/frame/report.h"
#include "third_party/blink/renderer/core/frame/reporting_context.h"
#include "third_party/blink/renderer/core/frame/root_frame_viewport.h"
#include "third_party/blink/renderer/core/frame/settings.h"
#include "third_party/blink/renderer/core/frame/smart_clip.h"
#include "third_party/blink/renderer/core/frame/user_activation.h"
#include "third_party/blink/renderer/core/frame/virtual_keyboard_overlay_changed_observer.h"
#include "third_party/blink/renderer/core/frame/visual_viewport.h"
#include "third_party/blink/renderer/core/frame/web_frame_widget_impl.h"
#include "third_party/blink/renderer/core/frame/web_local_frame_impl.h"
#include "third_party/blink/renderer/core/fullscreen/scoped_allow_fullscreen.h"
#include "third_party/blink/renderer/core/html/html_frame_element_base.h"
#include "third_party/blink/renderer/core/html/html_link_element.h"
#include "third_party/blink/renderer/core/html/html_object_element.h"
#include "third_party/blink/renderer/core/html/html_plugin_element.h"
#include "third_party/blink/renderer/core/html/media/html_media_element.h"
#include "third_party/blink/renderer/core/html/media/html_video_element.h"
#include "third_party/blink/renderer/core/html/plugin_document.h"
#include "third_party/blink/renderer/core/input/event_handler.h"
#include "third_party/blink/renderer/core/inspector/console_message.h"
#include "third_party/blink/renderer/core/inspector/inspector_issue_reporter.h"
#include "third_party/blink/renderer/core/inspector/inspector_issue_storage.h"
#include "third_party/blink/renderer/core/inspector/inspector_task_runner.h"
#include "third_party/blink/renderer/core/inspector/inspector_trace_events.h"
#include "third_party/blink/renderer/core/intersection_observer/intersection_observer_controller.h"
#include "third_party/blink/renderer/core/layout/hit_test_result.h"
#include "third_party/blink/renderer/core/layout/layout_view.h"
#include "third_party/blink/renderer/core/layout/text_autosizer.h"
#include "third_party/blink/renderer/core/loader/document_loader.h"
#include "third_party/blink/renderer/core/loader/frame_load_request.h"
#include "third_party/blink/renderer/core/loader/idleness_detector.h"
#include "third_party/blink/renderer/core/loader/prerender_handle.h"
#include "third_party/blink/renderer/core/messaging/message_port.h"
#include "third_party/blink/renderer/core/mobile_metrics/mobile_friendliness_checker.h"
#include "third_party/blink/renderer/core/page/chrome_client.h"
#include "third_party/blink/renderer/core/page/drag_controller.h"
#include "third_party/blink/renderer/core/page/focus_controller.h"
#include "third_party/blink/renderer/core/page/page.h"
#include "third_party/blink/renderer/core/page/page_animator.h"
#include "third_party/blink/renderer/core/page/plugin_data.h"
#include "third_party/blink/renderer/core/page/plugin_script_forbidden_scope.h"
#include "third_party/blink/renderer/core/page/pointer_lock_controller.h"
#include "third_party/blink/renderer/core/page/scrolling/scrolling_coordinator.h"
#include "third_party/blink/renderer/core/paint/object_painter.h"
#include "third_party/blink/renderer/core/paint/paint_auto_dark_mode.h"
#include "third_party/blink/renderer/core/paint/paint_layer_scrollable_area.h"
#include "third_party/blink/renderer/core/probe/core_probes.h"
#include "third_party/blink/renderer/core/script/classic_script.h"
#include "third_party/blink/renderer/core/scroll/smooth_scroll_sequencer.h"
#include "third_party/blink/renderer/core/svg/svg_document_extensions.h"
#include "third_party/blink/renderer/platform/back_forward_cache_utils.h"
#include "third_party/blink/renderer/platform/bindings/script_forbidden_scope.h"
#include "third_party/blink/renderer/platform/bindings/v8_binding.h"
#include "third_party/blink/renderer/platform/bindings/v8_per_isolate_data.h"
#include "third_party/blink/renderer/platform/blob/blob_data.h"
#include "third_party/blink/renderer/platform/graphics/paint/drawing_recorder.h"
#include "third_party/blink/renderer/platform/graphics/paint/paint_canvas.h"
#include "third_party/blink/renderer/platform/graphics/paint/paint_controller.h"
#include "third_party/blink/renderer/platform/graphics/paint/scoped_paint_chunk_properties.h"
#include "third_party/blink/renderer/platform/heap/collection_support/heap_vector.h"
#include "third_party/blink/renderer/platform/heap/garbage_collected.h"
#include "third_party/blink/renderer/platform/heap/persistent.h"
#include "third_party/blink/renderer/platform/instrumentation/instance_counters.h"
#include "third_party/blink/renderer/platform/instrumentation/resource_coordinator/document_resource_coordinator.h"
#include "third_party/blink/renderer/platform/instrumentation/tracing/trace_event.h"
#include "third_party/blink/renderer/platform/instrumentation/use_counter.h"
#include "third_party/blink/renderer/platform/json/json_values.h"
#include "third_party/blink/renderer/platform/loader/fetch/resource_fetcher.h"
#include "third_party/blink/renderer/platform/loader/fetch/resource_request.h"
#include "third_party/blink/renderer/platform/mhtml/serialized_resource.h"
#include "third_party/blink/renderer/platform/network/network_utils.h"
#include "third_party/blink/renderer/platform/runtime_enabled_features.h"
#include "third_party/blink/renderer/platform/scheduler/public/frame_scheduler.h"
#include "third_party/blink/renderer/platform/weborigin/scheme_registry.h"
#include "third_party/blink/renderer/platform/wtf/casting.h"
#include "third_party/blink/renderer/platform/wtf/std_lib_extras.h"
#include "third_party/blink/renderer/platform/wtf/vector.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/point_conversions.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/geometry/transform.h"

#if BUILDFLAG(IS_MAC)
#include "third_party/blink/renderer/core/editing/ephemeral_range.h"
#include "third_party/blink/renderer/core/editing/substring_util.h"
#include "third_party/blink/renderer/platform/fonts/mac/attributed_string_type_converter.h"
#include "ui/base/mojom/attributed_string.mojom-blink.h"
#include "ui/gfx/range/range.h"
#endif

#if !BUILDFLAG(IS_ANDROID)
#include "third_party/blink/renderer/core/frame/window_controls_overlay_changed_delegate.h"
#endif

namespace blink {

namespace {

// Maintain a global (statically-allocated) hash map indexed by the the result
// of hashing the |frame_token| passed on creation of a LocalFrame object.
using LocalFramesByTokenMap = HeapHashMap<uint64_t, WeakMember<LocalFrame>>;
static LocalFramesByTokenMap& GetLocalFramesMap() {
  DEFINE_STATIC_LOCAL(Persistent<LocalFramesByTokenMap>, map,
                      (MakeGarbageCollected<LocalFramesByTokenMap>()));
  return *map;
}

// Maximum number of burst download requests allowed.
const int kBurstDownloadLimit = 10;

inline float ParentPageZoomFactor(LocalFrame* frame) {
  auto* parent_local_frame = DynamicTo<LocalFrame>(frame->Tree().Parent());
  return parent_local_frame ? parent_local_frame->PageZoomFactor() : 1;
}

inline float ParentTextZoomFactor(LocalFrame* frame) {
  auto* parent_local_frame = DynamicTo<LocalFrame>(frame->Tree().Parent());
  return parent_local_frame ? parent_local_frame->TextZoomFactor() : 1;
}

// Convert a data url to a message pipe handle that corresponds to a remote
// blob, so that it can be passed across processes.
mojo::PendingRemote<mojom::blink::Blob> DataURLToBlob(const String& data_url) {
  auto blob_data = std::make_unique<BlobData>();
  StringUTF8Adaptor data_url_utf8(data_url);
  blob_data->AppendBytes(data_url_utf8.data(), data_url_utf8.size());
  scoped_refptr<BlobDataHandle> blob_data_handle =
      BlobDataHandle::Create(std::move(blob_data), data_url_utf8.size());
  return blob_data_handle->CloneBlobRemote();
}

RemoteFrame* SourceFrameForOptionalToken(
    const absl::optional<RemoteFrameToken>& source_frame_token) {
  if (!source_frame_token)
    return nullptr;
  return RemoteFrame::FromFrameToken(source_frame_token.value());
}

void SetViewportSegmentVariablesForRect(StyleEnvironmentVariables& vars,
                                        gfx::Rect segment_rect,
                                        unsigned first_dimension,
                                        unsigned second_dimension) {
  vars.SetVariable(UADefinedTwoDimensionalVariable::kViewportSegmentTop,
                   first_dimension, second_dimension,
                   StyleEnvironmentVariables::FormatPx(segment_rect.y()));
  vars.SetVariable(UADefinedTwoDimensionalVariable::kViewportSegmentRight,
                   first_dimension, second_dimension,
                   StyleEnvironmentVariables::FormatPx(segment_rect.right()));
  vars.SetVariable(UADefinedTwoDimensionalVariable::kViewportSegmentBottom,
                   first_dimension, second_dimension,
                   StyleEnvironmentVariables::FormatPx(segment_rect.bottom()));
  vars.SetVariable(UADefinedTwoDimensionalVariable::kViewportSegmentLeft,
                   first_dimension, second_dimension,
                   StyleEnvironmentVariables::FormatPx(segment_rect.x()));
  vars.SetVariable(UADefinedTwoDimensionalVariable::kViewportSegmentWidth,
                   first_dimension, second_dimension,
                   StyleEnvironmentVariables::FormatPx(segment_rect.width()));
  vars.SetVariable(UADefinedTwoDimensionalVariable::kViewportSegmentHeight,
                   first_dimension, second_dimension,
                   StyleEnvironmentVariables::FormatPx(segment_rect.height()));
}

}  // namespace

template class CORE_TEMPLATE_EXPORT Supplement<LocalFrame>;

// static
LocalFrame* LocalFrame::FromFrameToken(const LocalFrameToken& frame_token) {
  LocalFramesByTokenMap& local_frames_map = GetLocalFramesMap();
  auto it = local_frames_map.find(LocalFrameToken::Hasher()(frame_token));
  return it == local_frames_map.end() ? nullptr : it->value.Get();
}

void LocalFrame::Init(Frame* opener,
                      std::unique_ptr<PolicyContainer> policy_container) {
  if (!policy_container)
    policy_container = PolicyContainer::CreateEmpty();

  CoreInitializer::GetInstance().InitLocalFrame(*this);

  GetInterfaceRegistry()->AddInterface(WTF::BindRepeating(
      &LocalFrame::BindTextFragmentReceiver, WrapWeakPersistent(this)));
  DCHECK(!mojo_handler_);
  mojo_handler_ = MakeGarbageCollected<LocalFrameMojoHandler>(*this);

  SetOpenerDoNotNotify(opener);
  loader_.Init(std::move(policy_container));
}

void LocalFrame::SetView(LocalFrameView* view) {
  DCHECK(!view_ || view_ != view);
  DCHECK(!GetDocument() || !GetDocument()->IsActive());
  if (view_)
    view_->WillBeRemovedFromFrame();
  view_ = view;
}

void LocalFrame::CreateView(const gfx::Size& viewport_size,
                            const Color& background_color) {
  DCHECK(this);
  DCHECK(GetPage());

  bool is_local_root = IsLocalRoot();

  if (is_local_root && View())
    View()->SetParentVisible(false);

  SetView(nullptr);

  LocalFrameView* frame_view = nullptr;
  if (is_local_root) {
    frame_view = MakeGarbageCollected<LocalFrameView>(*this, viewport_size);

    // The layout size is set by WebViewImpl to support @viewport
    frame_view->SetLayoutSizeFixedToFrameSize(false);
  } else {
    frame_view = MakeGarbageCollected<LocalFrameView>(*this);
  }

  SetView(frame_view);

  frame_view->UpdateBaseBackgroundColorRecursively(background_color);

  if (is_local_root)
    frame_view->SetParentVisible(true);

  // FIXME: Not clear what the right thing for OOPI is here.
  if (OwnerLayoutObject()) {
    HTMLFrameOwnerElement* owner = DeprecatedLocalOwner();
    DCHECK(owner);
    // FIXME: OOPI might lead to us temporarily lying to a frame and telling it
    // that it's owned by a FrameOwner that knows nothing about it. If we're
    // lying to this frame, don't let it clobber the existing
    // EmbeddedContentView.
    if (owner->ContentFrame() == this)
      owner->SetEmbeddedContentView(frame_view);
  }

  if (Owner()) {
    View()->SetCanHaveScrollbars(Owner()->ScrollbarMode() !=
                                 mojom::blink::ScrollbarMode::kAlwaysOff);
  }
}

LocalFrame::~LocalFrame() {
  // Verify that the LocalFrameView has been cleared as part of detaching
  // the frame owner.
  DCHECK(!view_);
  DCHECK(!frame_color_overlay_);
  if (IsAdSubframe())
    InstanceCounters::DecrementCounter(InstanceCounters::kAdSubframeCounter);
}

void LocalFrame::Trace(Visitor* visitor) const {
  visitor->Trace(ad_tracker_);
  visitor->Trace(attribution_src_loader_);
  visitor->Trace(probe_sink_);
  visitor->Trace(performance_monitor_);
  visitor->Trace(idleness_detector_);
  visitor->Trace(inspector_issue_reporter_);
  visitor->Trace(inspector_trace_events_);
  visitor->Trace(loader_);
  visitor->Trace(view_);
  visitor->Trace(dom_window_);
  visitor->Trace(page_popup_owner_);
  visitor->Trace(editor_);
  visitor->Trace(selection_);
  visitor->Trace(event_handler_);
  visitor->Trace(console_);
  visitor->Trace(smooth_scroll_sequencer_);
  visitor->Trace(content_capture_manager_);
  visitor->Trace(system_clipboard_);
  visitor->Trace(virtual_keyboard_overlay_changed_observers_);
  visitor->Trace(pause_handle_receivers_);
  visitor->Trace(frame_color_overlay_);
  visitor->Trace(mojo_handler_);
  visitor->Trace(text_fragment_handler_);
  visitor->Trace(saved_scroll_offsets_);
  visitor->Trace(background_color_paint_image_generator_);
  visitor->Trace(box_shadow_paint_image_generator_);
  visitor->Trace(clip_path_paint_image_generator_);
#if !BUILDFLAG(IS_ANDROID)
  visitor->Trace(window_controls_overlay_changed_delegate_);
#endif
  Frame::Trace(visitor);
  Supplementable<LocalFrame>::Trace(visitor);
}

bool LocalFrame::IsLocalRoot() const {
  if (!Tree().Parent())
    return true;

  return Tree().Parent()->IsRemoteFrame();
}

void LocalFrame::Navigate(FrameLoadRequest& request,
                          WebFrameLoadType frame_load_type) {
  if (HTMLFrameOwnerElement* element = DeprecatedLocalOwner())
    element->CancelPendingLazyLoad();

  if (!navigation_rate_limiter().CanProceed())
    return;

  TRACE_EVENT2("navigation", "LocalFrame::Navigate", "url",
               request.GetResourceRequest().Url().GetString().Utf8(),
               "load_type", static_cast<int>(frame_load_type));

  if (request.ClientRedirectReason() != ClientNavigationReason::kNone)
    probe::FrameScheduledNavigation(this, request.GetResourceRequest().Url(),
                                    base::TimeDelta(),
                                    request.ClientRedirectReason());

  if (NavigationShouldReplaceCurrentHistoryEntry(request, frame_load_type))
    frame_load_type = WebFrameLoadType::kReplaceCurrentItem;

  const ClientNavigationReason client_redirect_reason =
      request.ClientRedirectReason();
  loader_.StartNavigation(request, frame_load_type);

  if (client_redirect_reason != ClientNavigationReason::kNone)
    probe::FrameClearedScheduledNavigation(this);
}

bool LocalFrame::NavigationShouldReplaceCurrentHistoryEntry(
    const FrameLoadRequest& request,
    WebFrameLoadType frame_load_type) {
  // Non-user navigation before the page has finished firing onload should not
  // create a new back/forward item. The spec only explicitly mentions this in
  // the context of navigating an iframe.
  if (request.ClientRedirectReason() != ClientNavigationReason::kNone &&
      !GetDocument()->LoadEventFinished() &&
      !HasTransientUserActivation(this) &&
      request.ClientRedirectReason() != ClientNavigationReason::kAnchorClick)
    return true;
  return frame_load_type == WebFrameLoadType::kStandard &&
         ShouldMaintainTrivialSessionHistory();

  // TODO(http://crbug.com/1197384): We may want to assert that
  // WebFrameLoadType is never kStandard in prerendered pages/portals before
  // commit. DCHECK can be in FrameLoader::CommitNavigation or somewhere
  // similar.
}

bool LocalFrame::ShouldMaintainTrivialSessionHistory() const {
  // This should be kept in sync with
  // NavigationControllerImpl::ShouldMaintainTrivialSessionHistory.
  //
  // TODO(mcnee): Similarly, we need to restrict orphaned contexts.
  return GetPage()->InsidePortal() || GetDocument()->IsPrerendering() ||
         IsInFencedFrameTree();
}

bool LocalFrame::DetachImpl(FrameDetachType type) {
  // !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
  // BEGIN REENTRANCY SAFE BLOCK
  // Starting here, the code must be safe against reentrancy. Dispatching
  // events, et cetera can run Javascript, which can reenter Detach().
  //
  // Most cleanup code should *not* be in inside the reentrancy safe block.
  // !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

  if (IsProvisional()) {
    Frame* provisional_owner = GetProvisionalOwnerFrame();
    // Having multiple provisional frames somehow associated with the same frame
    // to potentially replace is a logic error.
    DCHECK_EQ(provisional_owner->ProvisionalFrame(), this);
    provisional_owner->SetProvisionalFrame(nullptr);
  }

  PluginScriptForbiddenScope forbid_plugin_destructor_scripting;
  // In a kSwap detach, if we have a navigation going, its moved to the frame
  // being swapped in, so we don't need to notify the client about the
  // navigation stopping here. That will be up to the provisional frame being
  // swapped in, which knows the actual state of the navigation.
  loader_.StopAllLoaders(/*abort_client=*/type == FrameDetachType::kRemove);
  // Don't allow any new child frames to load in this frame: attaching a new
  // child frame during or after detaching children results in an attached
  // frame on a detached DOM tree, which is bad.
  SubframeLoadingDisabler disabler(*GetDocument());
  // https://html.spec.whatwg.org/C/browsing-the-web.html#unload-a-document
  // The ignore-opens-during-unload counter of a Document must be incremented
  // both when unloading itself and when unloading its descendants.
  IgnoreOpensDuringUnloadCountIncrementer ignore_opens_during_unload(
      GetDocument());

  loader_.DispatchUnloadEventAndFillOldDocumentInfoIfNeeded(
      type == FrameDetachType::kSwap);
  if (evict_cached_session_storage_on_freeze_or_unload_) {
    // Evicts the cached data of Session Storage to avoid reusing old data in
    // the cache after the session storage has been modified by another renderer
    // process.
    CoreInitializer::GetInstance().EvictSessionStorageCachedData(
        GetDocument()->GetPage());
  }
  if (!Client())
    return false;

  if (!DetachChildren())
    return false;

  // Detach() needs to be called after detachChildren(), because
  // detachChildren() will trigger the unload event handlers of any child
  // frames, and those event handlers might start a new subresource load in this
  // frame which should be stopped by Detach.
  loader_.Detach();
  DomWindow()->FrameDestroyed();

  // Verify here that any LocalFrameView has been detached by now.
  if (view_ && view_->IsAttached()) {
    DCHECK(DeprecatedLocalOwner());
    DCHECK(DeprecatedLocalOwner()->OwnedEmbeddedContentView());
    DCHECK_EQ(view_, DeprecatedLocalOwner()->OwnedEmbeddedContentView());
  }
  DCHECK(!view_ || !view_->IsAttached());

  // This is the earliest that scripting can be disabled:
  // - FrameLoader::Detach() can fire XHR abort events
  // - Document::Shutdown() can dispose plugins which can run script.
  ScriptForbiddenScope forbid_script;
  if (!Client())
    return false;

  // !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
  // END REENTRANCY SAFE BLOCK
  // Past this point, no script should be executed. If this method was
  // reentered, then a check for a null Client() above should have already
  // returned false.
  // !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
  DCHECK(!IsDetached());

  if (frame_color_overlay_)
    frame_color_overlay_.Release()->Destroy();

  if (IsLocalRoot()) {
    performance_monitor_->Shutdown();
    if (ad_tracker_)
      ad_tracker_->Shutdown();
    // Unregister only if this is LocalRoot because the paint_image_generator_
    // was created on LocalRoot.
    if (background_color_paint_image_generator_)
      background_color_paint_image_generator_->Shutdown();
    if (box_shadow_paint_image_generator_)
      box_shadow_paint_image_generator_->Shutdown();
    if (clip_path_paint_image_generator_)
      clip_path_paint_image_generator_->Shutdown();
  }
  idleness_detector_->Shutdown();
  if (inspector_issue_reporter_)
    probe_sink_->RemoveInspectorIssueReporter(inspector_issue_reporter_);
  if (inspector_trace_events_)
    probe_sink_->RemoveInspectorTraceEvents(inspector_trace_events_);
  inspector_task_runner_->Dispose();

  if (content_capture_manager_) {
    content_capture_manager_->Shutdown();
    content_capture_manager_ = nullptr;
  }

  if (text_fragment_handler_)
    text_fragment_handler_->DidDetachDocumentOrFrame();

  DCHECK(!view_->IsAttached());
  Client()->WillBeDetached();

  // TODO(crbug.com/729196): Trace why LocalFrameView::DetachFromLayout crashes.
  CHECK(!view_->IsAttached());
  SetView(nullptr);

  GetEventHandlerRegistry().DidRemoveAllEventHandlers(*DomWindow());

  probe::FrameDetachedFromParent(this, type);

  supplements_.clear();
  frame_scheduler_.reset();
  mojo_handler_->DidDetachFrame();
  WeakIdentifierMap<LocalFrame>::NotifyObjectDestroyed(this);

  return true;
}

bool LocalFrame::DetachDocument() {
  return Loader().DetachDocument();
}

void LocalFrame::CheckCompleted() {
  GetDocument()->CheckCompleted();
}

BackgroundColorPaintImageGenerator*
LocalFrame::GetBackgroundColorPaintImageGenerator() {
  // There is no compositor thread in certain testing environment, and we should
  // not composite background color animation in those cases.
  if (!Thread::CompositorThread())
    return nullptr;
  LocalFrame& local_root = LocalFrameRoot();
  // One background color paint worklet per root frame.
  if (!local_root.background_color_paint_image_generator_) {
    local_root.background_color_paint_image_generator_ =
        BackgroundColorPaintImageGenerator::Create(local_root);
  }
  return local_root.background_color_paint_image_generator_.Get();
}

BoxShadowPaintImageGenerator* LocalFrame::GetBoxShadowPaintImageGenerator() {
  // There is no compositor thread in certain testing environment, and we should
  // not composite background color animation in those cases.
  if (!Thread::CompositorThread())
    return nullptr;
  LocalFrame& local_root = LocalFrameRoot();
  // One box shadow paint worklet per root frame.
  if (!local_root.box_shadow_paint_image_generator_) {
    local_root.box_shadow_paint_image_generator_ =
        BoxShadowPaintImageGenerator::Create(local_root);
  }
  return local_root.box_shadow_paint_image_generator_.Get();
}

ClipPathPaintImageGenerator* LocalFrame::GetClipPathPaintImageGenerator() {
  LocalFrame& local_root = LocalFrameRoot();
  // One clip path paint worklet per root frame.
  if (!local_root.clip_path_paint_image_generator_) {
    local_root.clip_path_paint_image_generator_ =
        ClipPathPaintImageGenerator::Create(local_root);
  }
  return local_root.clip_path_paint_image_generator_.Get();
}

const SecurityContext* LocalFrame::GetSecurityContext() const {
  return DomWindow() ? &DomWindow()->GetSecurityContext() : nullptr;
}

// Provides a string description of the Frame as either its URL or origin if
// remote.
static String FrameDescription(const Frame& frame) {
  // URLs aren't available for RemoteFrames, so the error message uses their
  // origin instead.
  const LocalFrame* local_frame = DynamicTo<LocalFrame>(&frame);
  return local_frame
             ? "with URL '" + local_frame->GetDocument()->Url().GetString() +
                   "'"
             : "with origin '" +
                   frame.GetSecurityContext()->GetSecurityOrigin()->ToString() +
                   "'";
}

void LocalFrame::PrintNavigationErrorMessage(const Frame& target_frame,
                                             const String& reason) {
  String message = "Unsafe attempt to initiate navigation for frame " +
                   FrameDescription(target_frame) + " from frame with URL '" +
                   GetDocument()->Url().GetString() + "'. " + reason + "\n";

  DomWindow()->PrintErrorMessage(message);
}

void LocalFrame::PrintNavigationWarning(const String& message) {
  console_->AddMessage(MakeGarbageCollected<ConsoleMessage>(
      mojom::ConsoleMessageSource::kJavaScript,
      mojom::ConsoleMessageLevel::kWarning, message));
}

bool LocalFrame::ShouldClose() {
  // TODO(dcheng): This should be fixed to dispatch beforeunload events to
  // both local and remote frames.
  return loader_.ShouldClose();
}

bool LocalFrame::DetachChildren() {
  DCHECK(GetDocument());
  ChildFrameDisconnector(*GetDocument()).Disconnect();
  return !!Client();
}

void LocalFrame::DidAttachDocument() {
  Document* document = GetDocument();
  DCHECK(document);
  GetEditor().Clear();
  // Clearing the event handler clears many events, but notably can ensure that
  // for a drag started on an element in a frame that was moved (likely via
  // appendChild()), the drag source will detach and stop firing drag events
  // even after the frame reattaches.
  GetEventHandler().Clear();
  Selection().DidAttachDocument(document);
  notified_color_scheme_ = false;

#if !BUILDFLAG(IS_ANDROID)
  // For PWAs with display_override "window-controls-overlay", titlebar area
  // rect bounds sent from the browser need to persist on navigation to keep the
  // UI consistent. The titlebar area rect values are set in |LocalFrame| before
  // the new document is attached. The css environment variables are needed to
  // be set for the new document.
  if (is_window_controls_overlay_visible_) {
    DocumentStyleEnvironmentVariables& vars =
        GetDocument()->GetStyleEngine().EnsureEnvironmentVariables();
    DCHECK(!vars.ResolveVariable(
        StyleEnvironmentVariables::GetVariableName(
            UADefinedVariable::kTitlebarAreaX, document->GetExecutionContext()),
        {}, false /* record_metrics */));
    SetTitlebarAreaDocumentStyleEnvironmentVariables();
  }
#endif
}

void LocalFrame::OnFirstPaint(bool text_painted, bool image_painted) {
  if (notified_color_scheme_)
    return;

  if (text_painted || image_painted) {
    // Infer the document's color scheme according to the background color, this
    // approach assumes that the background won't be changed after the first
    // text or image is painted, otherwise, the document will have a jarring
    // flash which should be avoid by most pages.
    double h, s, l;
    View()->DocumentBackgroundColor().GetHSL(h, s, l);
    GetLocalFrameHostRemote().DidInferColorScheme(
        l < 0.5 ? mojom::blink::PreferredColorScheme::kDark
                : mojom::blink::PreferredColorScheme::kLight);
    notified_color_scheme_ = true;
  }
}

bool LocalFrame::CanAccessEvent(
    const WebInputEventAttribution& attribution) const {
  switch (attribution.type()) {
    case WebInputEventAttribution::kTargetedFrame: {
      auto* frame_document = GetDocument();
      if (!frame_document)
        return false;

      Document* target_document = nullptr;
      if (auto* page = frame_document->GetPage()) {
        auto& pointer_lock_controller = page->GetPointerLockController();
        if (auto* element = pointer_lock_controller.GetElement()) {
          // If a pointer lock is held, we can expect all events to be
          // dispatched to the frame containing the locked element.
          target_document = &element->GetDocument();
        } else if (cc::ElementId element_id = attribution.target_frame_id()) {
          DOMNodeId target_document_id =
              DOMNodeIdFromCompositorElementId(element_id);
          target_document =
              DynamicTo<Document>(DOMNodeIds::NodeForId(target_document_id));
        }
      }

      if (!target_document || !target_document->domWindow())
        return false;

      return GetSecurityContext()->GetSecurityOrigin()->CanAccess(
          target_document->domWindow()->GetSecurityOrigin());
    }
    case WebInputEventAttribution::kFocusedFrame:
      return GetPage() ? GetPage()->GetFocusController().FocusedFrame() == this
                       : false;
    case WebInputEventAttribution::kUnknown:
      return false;
  }
}

bool LocalFrame::IsTransientAllowFullscreenActive() const {
  return transient_allow_fullscreen_.IsActive();
}

void LocalFrame::Reload(WebFrameLoadType load_type) {
  DCHECK(IsReloadLoadType(load_type));
  if (!loader_.GetDocumentLoader()->GetHistoryItem())
    return;
  TRACE_EVENT1("navigation", "LocalFrame::Reload", "load_type",
               static_cast<int>(load_type));

  FrameLoadRequest request(
      DomWindow(), loader_.ResourceRequestForReload(
                       load_type, ClientRedirectPolicy::kClientRedirect));
  request.SetClientRedirectReason(ClientNavigationReason::kReload);
  probe::FrameScheduledNavigation(this, request.GetResourceRequest().Url(),
                                  base::TimeDelta(),
                                  ClientNavigationReason::kReload);
  loader_.StartNavigation(request, load_type);
  probe::FrameClearedScheduledNavigation(this);
}

LocalWindowProxy* LocalFrame::WindowProxy(DOMWrapperWorld& world) {
  return To<LocalWindowProxy>(Frame::GetWindowProxy(world));
}

LocalWindowProxy* LocalFrame::WindowProxyMaybeUninitialized(
    DOMWrapperWorld& world) {
  return To<LocalWindowProxy>(Frame::GetWindowProxyMaybeUninitialized(world));
}

LocalDOMWindow* LocalFrame::DomWindow() {
  return To<LocalDOMWindow>(dom_window_.Get());
}

const LocalDOMWindow* LocalFrame::DomWindow() const {
  return To<LocalDOMWindow>(dom_window_.Get());
}

void LocalFrame::SetDOMWindow(LocalDOMWindow* dom_window) {
  DCHECK(dom_window);
  if (DomWindow()) {
    DomWindow()->Reset();
    // SystemClipboard uses HeapMojo wrappers. HeapMojo
    // wrappers uses LocalDOMWindow (ExecutionContext) to reset the mojo
    // objects when the ExecutionContext was destroyed. So when new
    // LocalDOMWindow was set, we need to create new SystemClipboard.
    system_clipboard_ = nullptr;
  }
  GetWindowProxyManager()->ClearForNavigation();
  dom_window_ = dom_window;
  dom_window->Initialize();
}

Document* LocalFrame::GetDocument() const {
  return DomWindow() ? DomWindow()->document() : nullptr;
}

void LocalFrame::SetPagePopupOwner(Element& owner) {
  page_popup_owner_ = &owner;
}

LayoutView* LocalFrame::ContentLayoutObject() const {
  return GetDocument() ? GetDocument()->GetLayoutView() : nullptr;
}

void LocalFrame::DidChangeVisibilityState() {
  if (GetDocument())
    GetDocument()->DidChangeVisibilityState();

  Frame::DidChangeVisibilityState();
}

bool LocalFrame::IsCaretBrowsingEnabled() const {
  return GetSettings() ? GetSettings()->GetCaretBrowsingEnabled() : false;
}

void LocalFrame::HookBackForwardCacheEviction() {
  TRACE_EVENT0("blink", "LocalFrame::HookBackForwardCacheEviction");
  // Register a callback dispatched when JavaScript is executed on the frame.
  // The callback evicts the frame. If a frame is frozen by BackForwardCache,
  // the frame must not be mutated e.g., by JavaScript execution, then the
  // frame must be evicted in such cases.
  DCHECK(RuntimeEnabledFeatures::BackForwardCacheEnabled());
  static_cast<LocalWindowProxyManager*>(GetWindowProxyManager())
      ->SetAbortScriptExecution(
          [](v8::Isolate* isolate, v8::Local<v8::Context> context) {
            ScriptState* script_state = ScriptState::From(context);
            LocalDOMWindow* window = LocalDOMWindow::From(script_state);
            DCHECK(window);
            LocalFrame* frame = window->GetFrame();
            if (frame) {
              frame->EvictFromBackForwardCache(
                  mojom::blink::RendererEvictionReason::kJavaScriptExecution);
            }
          });
}

void LocalFrame::RemoveBackForwardCacheEviction() {
  TRACE_EVENT0("blink", "LocalFrame::RemoveBackForwardCacheEviction");
  DCHECK(RuntimeEnabledFeatures::BackForwardCacheEnabled());
  static_cast<LocalWindowProxyManager*>(GetWindowProxyManager())
      ->SetAbortScriptExecution(nullptr);

  // The page is being restored, and from this point eviction should not happen
  // for any reason. Change the deferring state from |kBufferIncoming| to
  // |kStrict| so that network related eviction cannot happen.
  GetDocument()->Fetcher()->SetDefersLoading(LoaderFreezeMode::kStrict);
}

void LocalFrame::SetTextDirection(base::i18n::TextDirection direction) {
  // The Editor::SetBaseWritingDirection() function checks if we can change
  // the text direction of the selected node and updates its DOM "dir"
  // attribute and its CSS "direction" property.
  // So, we just call the function as Safari does.
  Editor& editor = GetEditor();
  if (!editor.CanEdit())
    return;

  switch (direction) {
    case base::i18n::TextDirection::UNKNOWN_DIRECTION:
      editor.SetBaseWritingDirection(
          mojo_base::mojom::blink::TextDirection::UNKNOWN_DIRECTION);
      break;

    case base::i18n::TextDirection::LEFT_TO_RIGHT:
      editor.SetBaseWritingDirection(
          mojo_base::mojom::blink::TextDirection::LEFT_TO_RIGHT);
      break;

    case base::i18n::TextDirection::RIGHT_TO_LEFT:
      editor.SetBaseWritingDirection(
          mojo_base::mojom::blink::TextDirection::RIGHT_TO_LEFT);
      break;

    default:
      NOTIMPLEMENTED();
      break;
  }
}

void LocalFrame::SetIsInert(bool inert) {
  if (is_inert_ == inert)
    return;
  is_inert_ = inert;

  // Propagate inert to child frames
  for (Frame* child = Tree().FirstChild(); child;
       child = child->Tree().NextSibling()) {
    child->UpdateInertIfPossible();
  }

  // Nodes all over the accessibility tree can change inertness which means they
  // must be added or removed from the tree. The most foolproof way is to clear
  // the entire tree and rebuild it, though a more clever way is probably
  // possible.
  if (Document* document = GetDocument())
    document->ClearAXObjectCache();
}

void LocalFrame::SetInheritedEffectiveTouchAction(TouchAction touch_action) {
  if (inherited_effective_touch_action_ == touch_action)
    return;
  inherited_effective_touch_action_ = touch_action;
  GetDocument()->GetStyleEngine().MarkAllElementsForStyleRecalc(
      StyleChangeReasonForTracing::Create(
          style_change_reason::kInheritedStyleChangeFromParentFrame));
}

bool LocalFrame::BubbleLogicalScrollInParentFrame(
    mojom::blink::ScrollDirection direction,
    ui::ScrollGranularity granularity) {
  bool is_embedded_main_frame = IsMainFrame() && !IsOutermostMainFrame();
  if (is_embedded_main_frame || IsA<RemoteFrame>(Parent())) {
    GetLocalFrameHostRemote().BubbleLogicalScrollInParentFrame(direction,
                                                               granularity);
    return false;
  } else if (auto* local_parent = DynamicTo<LocalFrame>(Parent())) {
    return local_parent->BubbleLogicalScrollFromChildFrame(direction,
                                                           granularity, this);
  }

  DCHECK(IsOutermostMainFrame());
  return false;
}

bool LocalFrame::BubbleLogicalScrollFromChildFrame(
    mojom::blink::ScrollDirection direction,
    ui::ScrollGranularity granularity,
    Frame* child) {
  FrameOwner* owner = child->Owner();
  auto* owner_element = DynamicTo<HTMLFrameOwnerElement>(owner);
  DCHECK(owner_element);

  return GetEventHandler().BubblingScroll(direction, granularity,
                                          owner_element);
}

mojom::blink::SuddenTerminationDisablerType
SuddenTerminationDisablerTypeForEventType(const AtomicString& event_type) {
  if (event_type == event_type_names::kUnload) {
    return mojom::blink::SuddenTerminationDisablerType::kUnloadHandler;
  }
  if (event_type == event_type_names::kBeforeunload) {
    return mojom::blink::SuddenTerminationDisablerType::kBeforeUnloadHandler;
  }
  if (event_type == event_type_names::kPagehide) {
    return mojom::blink::SuddenTerminationDisablerType::kPageHideHandler;
  }
  if (event_type == event_type_names::kVisibilitychange) {
    return mojom::blink::SuddenTerminationDisablerType::
        kVisibilityChangeHandler;
  }
  NOTREACHED();
  return mojom::blink::SuddenTerminationDisablerType::kUnloadHandler;
}

int NumberOfSuddenTerminationEventListeners(const EventTarget& event_target,
                                            const AtomicString& event_type) {
  if (event_type != event_type_names::kVisibilitychange)
    return event_target.NumberOfEventListeners(event_type);
  // For visibilitychange, we need to count the number of event listeners that
  // are registered on the document and the window, as the event is initially
  // dispatched on the document but might bubble up to the window.
  // The other events (beforeunload, unload, pagehide) are dispatched on the
  // window and won't bubble up anywhere, so we don't need to check for
  // listeners the document for those events.
  int total_listeners_count = event_target.NumberOfEventListeners(event_type);
  if (auto* dom_window = event_target.ToLocalDOMWindow()) {
    // |event_target| is the window, so get the count for listeners registered
    // on the document.
    total_listeners_count +=
        dom_window->document()->NumberOfEventListeners(event_type);
  } else {
    auto* node = const_cast<EventTarget*>(&event_target)->ToNode();
    DCHECK(node);
    DCHECK(node->IsDocumentNode());
    DCHECK(node->GetDocument().domWindow());
    // |event_target| is the document, so get the count for listeners registered
    // on the window.
    total_listeners_count +=
        node->GetDocument().domWindow()->NumberOfEventListeners(event_type);
  }
  return total_listeners_count;
}

void LocalFrame::UpdateSuddenTerminationStatus(
    bool added_listener,
    mojom::blink::SuddenTerminationDisablerType disabler_type) {
  Platform::Current()->SuddenTerminationChanged(!added_listener);
  GetLocalFrameHostRemote().SuddenTerminationDisablerChanged(added_listener,
                                                             disabler_type);
}

void LocalFrame::AddedSuddenTerminationDisablerListener(
    const EventTarget& event_target,
    const AtomicString& event_type) {
  if (NumberOfSuddenTerminationEventListeners(event_target, event_type) == 1) {
    // The first handler of this type was added.
    UpdateSuddenTerminationStatus(
        true, SuddenTerminationDisablerTypeForEventType(event_type));
  }
}

void LocalFrame::RemovedSuddenTerminationDisablerListener(
    const EventTarget& event_target,
    const AtomicString& event_type) {
  if (NumberOfSuddenTerminationEventListeners(event_target, event_type) == 0) {
    // The last handler of this type was removed.
    UpdateSuddenTerminationStatus(
        false, SuddenTerminationDisablerTypeForEventType(event_type));
  }
}

void LocalFrame::DidFocus() {
  GetLocalFrameHostRemote().DidFocusFrame();
}

void LocalFrame::DidChangeThemeColor(bool update_theme_color_cache) {
  if (Tree().Parent())
    return;

  if (update_theme_color_cache)
    GetDocument()->UpdateThemeColorCache();

  absl::optional<Color> color = GetDocument()->ThemeColor();
  absl::optional<SkColor> sk_color;
  if (color)
    sk_color = color->Rgb();

  GetLocalFrameHostRemote().DidChangeThemeColor(sk_color);
}

void LocalFrame::DidChangeBackgroundColor(SkColor background_color,
                                          bool color_adjust) {
  DCHECK(!Tree().Parent());
  GetLocalFrameHostRemote().DidChangeBackgroundColor(background_color,
                                                     color_adjust);
}

LocalFrame& LocalFrame::LocalFrameRoot() const {
  const LocalFrame* cur_frame = this;
  while (cur_frame && IsA<LocalFrame>(cur_frame->Tree().Parent()))
    cur_frame = To<LocalFrame>(cur_frame->Tree().Parent());

  return const_cast<LocalFrame&>(*cur_frame);
}

scoped_refptr<InspectorTaskRunner> LocalFrame::GetInspectorTaskRunner() {
  return inspector_task_runner_;
}

void LocalFrame::StartPrinting(const gfx::SizeF& page_size,
                               const gfx::SizeF& original_page_size,
                               float maximum_shrink_ratio) {
  DCHECK(!saved_scroll_offsets_);
  SetPrinting(true, page_size, original_page_size, maximum_shrink_ratio);
}

void LocalFrame::EndPrinting() {
  RestoreScrollOffsets();
  SetPrinting(false, gfx::SizeF(), gfx::SizeF(), 0);
}

void LocalFrame::SetPrinting(bool printing,
                             const gfx::SizeF& page_size,
                             const gfx::SizeF& original_page_size,
                             float maximum_shrink_ratio) {
  // In setting printing, we should not validate resources already cached for
  // the document.  See https://bugs.webkit.org/show_bug.cgi?id=43704
  ResourceCacheValidationSuppressor validation_suppressor(
      GetDocument()->Fetcher());

  GetDocument()->SetPrinting(printing ? Document::kPrinting
                                      : Document::kFinishingPrinting);
  View()->AdjustMediaTypeForPrinting(printing);

  if (!printing) {
    // Don't get stuck with the legacy engine when no longer printing.
    if (Element* root = GetDocument()->documentElement())
      root->ResetForceLegacyLayoutForPrinting();
  }

  if (TextAutosizer* text_autosizer = GetDocument()->GetTextAutosizer())
    text_autosizer->UpdatePageInfo();

  if (ShouldUsePrintingLayout()) {
    View()->ForceLayoutForPagination(page_size, original_page_size,
                                     maximum_shrink_ratio);
  } else {
    if (LayoutView* layout_view = View()->GetLayoutView()) {
      layout_view->SetIntrinsicLogicalWidthsDirty();
      layout_view->SetNeedsLayout(layout_invalidation_reason::kPrintingChanged);
      layout_view->InvalidatePaintForViewAndDescendants();
    }
    GetDocument()->UpdateStyleAndLayout(DocumentUpdateReason::kPrinting);
    View()->AdjustViewSize();
  }

  // Subframes of the one we're printing don't lay out to the page size.
  for (Frame* child = Tree().FirstChild(); child;
       child = child->Tree().NextSibling()) {
    if (auto* child_local_frame = DynamicTo<LocalFrame>(child)) {
      if (printing)
        child_local_frame->StartPrinting();
      else
        child_local_frame->EndPrinting();
    }
  }

  if (auto* layout_view = View()->GetLayoutView()) {
    layout_view->AddSubtreePaintPropertyUpdateReason(
        SubtreePaintPropertyUpdateReason::kPrinting);
  }

  if (!printing)
    GetDocument()->SetPrinting(Document::kNotPrinting);
}

bool LocalFrame::ShouldUsePrintingLayout() const {
  if (!GetDocument()->Printing())
    return false;

  // Only the top frame being printed should be fitted to page size.
  // Subframes should be constrained by parents only.
  // This function considers the following two kinds of frames as top frames:
  // -- frame with no parent;
  // -- frame's parent is not in printing mode.
  // For the second type, it is a bit complicated when its parent is a remote
  // frame. In such case, we can not check its document or other internal
  // status. However, if the parent is in printing mode, this frame's printing
  // must have started with |use_printing_layout| as false in print context.
  auto* parent = Tree().Parent();
  if (!parent)
    return true;
  auto* local_parent = DynamicTo<LocalFrame>(parent);
  return local_parent ? !local_parent->GetDocument()->Printing()
                      : Client()->UsePrintingLayout();
}

void LocalFrame::StartPaintPreview() {
  SetInvalidationForCapture(true);
}

void LocalFrame::EndPaintPreview() {
  SetInvalidationForCapture(false);
}

void LocalFrame::SetInvalidationForCapture(bool capturing) {
  if (!capturing)
    RestoreScrollOffsets();

  ResourceCacheValidationSuppressor validation_suppressor(
      GetDocument()->Fetcher());

  // Subframes of the captured content should be updated.
  for (Frame* child = Tree().FirstChild(); child;
       child = child->Tree().NextSibling()) {
    if (auto* child_local_frame = DynamicTo<LocalFrame>(child)) {
      child_local_frame->SetInvalidationForCapture(capturing);
    }
  }

  // Trigger a paint property update to ensure the unclipped behavior is
  // applied to the frame level scroller.
  if (auto* layout_view = View()->GetLayoutView()) {
    layout_view->SetNeedsPaintPropertyUpdate();
  }
}

void LocalFrame::EnsureSaveScrollOffset(Node& node) {
  const auto* scrollable_area = PaintLayerScrollableArea::FromNode(node);
  if (!scrollable_area)
    return;
  if (!saved_scroll_offsets_)
    saved_scroll_offsets_ = MakeGarbageCollected<SavedScrollOffsets>();
  // Retain the first scroll offset saved for each scrollable area.
  if (!saved_scroll_offsets_->Contains(&node))
    saved_scroll_offsets_->Set(&node, scrollable_area->GetScrollOffset());
}

void LocalFrame::RestoreScrollOffsets() {
  if (!saved_scroll_offsets_)
    return;

  // Restore scroll offsets unconditionally (i.e. without clamping) in case
  // layout or view sizes haven't been updated yet.
  for (auto& entry : *saved_scroll_offsets_) {
    auto* scrollable_area = PaintLayerScrollableArea::FromNode(*entry.key);
    if (!scrollable_area)
      continue;
    scrollable_area->SetScrollOffsetUnconditionally(
        entry.value, mojom::blink::ScrollType::kProgrammatic);
  }
  saved_scroll_offsets_ = nullptr;
}

gfx::SizeF LocalFrame::ResizePageRectsKeepingRatio(
    const gfx::SizeF& original_size,
    const gfx::SizeF& expected_size) const {
  auto* layout_object = ContentLayoutObject();
  if (!layout_object)
    return gfx::SizeF();

  bool is_horizontal = layout_object->StyleRef().IsHorizontalWritingMode();
  float width = original_size.width();
  float height = original_size.height();
  if (!is_horizontal)
    std::swap(width, height);
  DCHECK_GT(fabs(width), std::numeric_limits<float>::epsilon());
  float ratio = height / width;

  float result_width =
      floorf(is_horizontal ? expected_size.width() : expected_size.height());
  float result_height = floorf(result_width * ratio);
  if (!is_horizontal)
    std::swap(result_width, result_height);
  return gfx::SizeF(result_width, result_height);
}

void LocalFrame::SetPageZoomFactor(float factor) {
  SetPageAndTextZoomFactors(factor, text_zoom_factor_);
}

void LocalFrame::SetTextZoomFactor(float factor) {
  SetPageAndTextZoomFactors(page_zoom_factor_, factor);
}

void LocalFrame::SetPageAndTextZoomFactors(float page_zoom_factor,
                                           float text_zoom_factor) {
  if (page_zoom_factor_ == page_zoom_factor &&
      text_zoom_factor_ == text_zoom_factor)
    return;

  Page* page = GetPage();
  if (!page)
    return;

  Document* document = GetDocument();
  if (!document)
    return;

  // Respect SVGs zoomAndPan="disabled" property in standalone SVG documents.
  // FIXME: How to handle compound documents + zoomAndPan="disabled"? Needs SVG
  // WG clarification.
  if (document->IsSVGDocument()) {
    if (!document->AccessSVGExtensions().ZoomAndPanEnabled())
      return;
  }

  bool page_zoom_changed = (page_zoom_factor != page_zoom_factor_);

  page_zoom_factor_ = page_zoom_factor;
  text_zoom_factor_ = text_zoom_factor;

  for (Frame* child = Tree().FirstChild(); child;
       child = child->Tree().NextSibling()) {
    if (auto* child_local_frame = DynamicTo<LocalFrame>(child)) {
      child_local_frame->SetPageAndTextZoomFactors(page_zoom_factor_,
                                                   text_zoom_factor_);
    }
  }

  if (page_zoom_changed) {
#if !BUILDFLAG(IS_ANDROID)
    MaybeUpdateWindowControlsOverlayWithNewZoomLevel();
#endif
    document->LayoutViewportWasResized();
    document->MediaQueryAffectingValueChanged(MediaValueChange::kOther);
  }
  document->GetStyleEngine().MarkViewportStyleDirty();
  document->GetStyleEngine().MarkAllElementsForStyleRecalc(
      StyleChangeReasonForTracing::Create(style_change_reason::kZoom));
  if (View())
    View()->SetNeedsLayout();
}

void LocalFrame::MediaQueryAffectingValueChangedForLocalSubtree(
    MediaValueChange value) {
  GetDocument()->MediaQueryAffectingValueChanged(value);
  for (Frame* child = Tree().FirstChild(); child;
       child = child->Tree().NextSibling()) {
    if (auto* child_local_frame = DynamicTo<LocalFrame>(child))
      child_local_frame->MediaQueryAffectingValueChangedForLocalSubtree(value);
  }
}

void LocalFrame::WindowSegmentsChanged(
    const WebVector<gfx::Rect>& window_segments) {
  if (!RuntimeEnabledFeatures::CSSFoldablesEnabled())
    return;

  DCHECK(IsLocalRoot());

  // A change in the window segments requires re-evaluation of media queries
  // for the local frame subtree (the segments affect the
  // "horizontal-viewport-segments" and "vertical-viewport-segments" features).
  MediaQueryAffectingValueChangedForLocalSubtree(MediaValueChange::kOther);

  // Also need to update the environment variables related to window segments.
  UpdateViewportSegmentCSSEnvironmentVariables(window_segments);
}

void LocalFrame::UpdateViewportSegmentCSSEnvironmentVariables(
    const WebVector<gfx::Rect>& window_segments) {
  DCHECK(RuntimeEnabledFeatures::CSSFoldablesEnabled());

  // Update the variable values on the root instance so that documents that
  // are created after the values change automatically have the right values.
  StyleEnvironmentVariables& vars =
      StyleEnvironmentVariables::GetRootInstance();

  // Unset all variables, since they will be set as a whole by the code below.
  // Since the number and configurations of the segments can change, and
  // removing variables clears all values that have previously been set,
  // we will recalculate all the values on each change.
  const UADefinedTwoDimensionalVariable vars_to_remove[] = {
      UADefinedTwoDimensionalVariable::kViewportSegmentTop,
      UADefinedTwoDimensionalVariable::kViewportSegmentRight,
      UADefinedTwoDimensionalVariable::kViewportSegmentBottom,
      UADefinedTwoDimensionalVariable::kViewportSegmentLeft,
      UADefinedTwoDimensionalVariable::kViewportSegmentWidth,
      UADefinedTwoDimensionalVariable::kViewportSegmentHeight,
  };
  for (auto var : vars_to_remove) {
    vars.RemoveVariable(var);
  }

  // Per [css-env-1], only set the segment variables if there is more than one.
  if (window_segments.size() >= 2) {
    // Iterate the segments in row-major order, setting the segment variables
    // based on x and y index.
    int current_y_position = window_segments[0].y();
    unsigned x_index = 0;
    unsigned y_index = 0;
    SetViewportSegmentVariablesForRect(vars, window_segments[0], x_index,
                                       y_index);
    for (size_t i = 1; i < window_segments.size(); i++) {
      if (window_segments[i].y() == current_y_position) {
        x_index++;
        SetViewportSegmentVariablesForRect(vars, window_segments[i], x_index,
                                           y_index);
      } else {
        // If there is a different y value, this is the next row so increase
        // y index and start again from 0 for x.
        y_index++;
        x_index = 0;
        current_y_position = window_segments[i].y();
        SetViewportSegmentVariablesForRect(vars, window_segments[i], x_index,
                                           y_index);
      }
    }
  }
}

device::mojom::blink::DevicePostureType LocalFrame::GetDevicePosture() {
  return mojo_handler_->GetDevicePosture();
}

double LocalFrame::DevicePixelRatio() const {
  if (!page_)
    return 0;

  double ratio = page_->InspectorDeviceScaleFactorOverride();
  ratio *= PageZoomFactor();
  return ratio;
}

String LocalFrame::SelectedText() const {
  return Selection().SelectedText();
}

String LocalFrame::SelectedTextForClipboard() const {
  if (!GetDocument())
    return g_empty_string;
  DCHECK(!GetDocument()->NeedsLayoutTreeUpdate());
  return Selection().SelectedTextForClipboard();
}

void LocalFrame::TextSelectionChanged(const WTF::String& selection_text,
                                      uint32_t offset,
                                      const gfx::Range& range) const {
  GetLocalFrameHostRemote().TextSelectionChanged(selection_text, offset, range);
}

PositionWithAffinity LocalFrame::PositionForPoint(
    const PhysicalOffset& frame_point) {
  HitTestLocation location(frame_point);
  HitTestResult result = GetEventHandler().HitTestResultAtLocation(location);
  return result.GetPositionForInnerNodeOrImageMapImage();
}

Document* LocalFrame::DocumentAtPoint(
    const PhysicalOffset& point_in_root_frame) {
  if (!View())
    return nullptr;

  HitTestLocation location(View()->ConvertFromRootFrame(point_in_root_frame));

  if (!ContentLayoutObject())
    return nullptr;
  HitTestResult result = GetEventHandler().HitTestResultAtLocation(
      location, HitTestRequest::kReadOnly | HitTestRequest::kActive);
  return result.InnerNode() ? &result.InnerNode()->GetDocument() : nullptr;
}

void LocalFrame::RemoveSpellingMarkersUnderWords(const Vector<String>& words) {
  GetSpellChecker().RemoveSpellingMarkersUnderWords(words);
}

String LocalFrame::GetLayerTreeAsTextForTesting(unsigned flags) const {
  if (!ContentLayoutObject())
    return String();

  std::unique_ptr<JSONObject> layers;
  if (!(flags & kOutputAsLayerTree)) {
    layers = View()->CompositedLayersAsJSON(static_cast<LayerTreeFlags>(flags));
  }
  return layers ? layers->ToPrettyJSONString() : String();
}

bool LocalFrame::ShouldThrottleRendering() const {
  return View() && View()->ShouldThrottleRendering();
}

void LocalFrame::PortalStateChanged() {
  if (Document* document = GetDocument())
    document->ClearAXObjectCache();

  if (IsOutermostMainFrame()) {
    intersection_state_.occlusion_state =
        mojom::blink::FrameOcclusionState::kGuaranteedNotOccluded;
  } else {
    intersection_state_.occlusion_state =
        mojom::blink::FrameOcclusionState::kUnknown;
  }
}

LocalFrame::LocalFrame(LocalFrameClient* client,
                       Page& page,
                       FrameOwner* owner,
                       Frame* parent,
                       Frame* previous_sibling,
                       FrameInsertType insert_type,
                       const LocalFrameToken& frame_token,
                       WindowAgentFactory* inheriting_agent_factory,
                       InterfaceRegistry* interface_registry,
                       const base::TickClock* clock)
    : Frame(client,
            page,
            owner,
            parent,
            previous_sibling,
            insert_type,
            frame_token,
            client->GetDevToolsFrameToken(),
            MakeGarbageCollected<LocalWindowProxyManager>(*this),
            inheriting_agent_factory),
      frame_scheduler_(page.GetPageScheduler()->CreateFrameScheduler(
          this,
          client->GetFrameBlameContext(),
          /*TODO(crbug.com/1170350): Set for portals*/ IsInFencedFrameTree(),
          IsMainFrame() ? FrameScheduler::FrameType::kMainFrame
                        : FrameScheduler::FrameType::kSubframe)),
      loader_(this),
      editor_(MakeGarbageCollected<Editor>(*this)),
      selection_(MakeGarbageCollected<FrameSelection>(*this)),
      event_handler_(MakeGarbageCollected<EventHandler>(*this)),
      console_(MakeGarbageCollected<FrameConsole>(*this)),
      navigation_disable_count_(0),
      should_send_resource_timing_info_to_parent_(true),
      in_view_source_mode_(false),
      frozen_(false),
      paused_(false),
      hidden_(false),
      page_zoom_factor_(ParentPageZoomFactor(this)),
      text_zoom_factor_(ParentTextZoomFactor(this)),
      inspector_task_runner_(InspectorTaskRunner::Create(
          GetTaskRunner(TaskType::kInternalInspector))),
      interface_registry_(interface_registry
                              ? interface_registry
                              : InterfaceRegistry::GetEmptyInterfaceRegistry()),
      lifecycle_state_(mojom::FrameLifecycleState::kRunning) {
  auto frame_tracking_result =
      GetLocalFramesMap().insert(FrameToken::Hasher()(GetFrameToken()), this);
  CHECK(frame_tracking_result.stored_value) << "Inserting a duplicate item.";
  v8::Isolate* isolate = V8PerIsolateData::MainThreadIsolate();
  if (IsLocalRoot()) {
    probe_sink_ = MakeGarbageCollected<CoreProbeSink>();
    performance_monitor_ =
        MakeGarbageCollected<PerformanceMonitor>(this, isolate);
    inspector_issue_reporter_ = MakeGarbageCollected<InspectorIssueReporter>(
        &page.GetInspectorIssueStorage());
    probe_sink_->AddInspectorIssueReporter(inspector_issue_reporter_);
    inspector_trace_events_ = MakeGarbageCollected<InspectorTraceEvents>();
    probe_sink_->AddInspectorTraceEvents(inspector_trace_events_);
    if (RuntimeEnabledFeatures::AdTaggingEnabled()) {
      ad_tracker_ = MakeGarbageCollected<AdTracker>(this);
    }
  } else {
    // Inertness only needs to be updated if this frame might inherit the
    // inert state from a higher-level frame. If this is an OOPIF local root,
    // it will be updated later.
    UpdateInertIfPossible();
    UpdateInheritedEffectiveTouchActionIfPossible();
    probe_sink_ = LocalFrameRoot().probe_sink_;
    ad_tracker_ = LocalFrameRoot().ad_tracker_;
    performance_monitor_ = LocalFrameRoot().performance_monitor_;
  }
  idleness_detector_ = MakeGarbageCollected<IdlenessDetector>(this, clock);
  attribution_src_loader_ = MakeGarbageCollected<AttributionSrcLoader>(this);
  inspector_task_runner_->InitIsolate(isolate);

  if (IsOutermostMainFrame()) {
    intersection_state_.occlusion_state =
        mojom::blink::FrameOcclusionState::kGuaranteedNotOccluded;
  }

  DCHECK(ad_tracker_ ? RuntimeEnabledFeatures::AdTaggingEnabled()
                     : !RuntimeEnabledFeatures::AdTaggingEnabled());

  absl::optional<AdScriptIdentifier> ad_script_on_stack;
  // See SubresourceFilterAgent::Initialize for why we don't set this here for
  // fenced frames.
  is_subframe_created_by_ad_script_ =
      !IsMainFrame() && ad_tracker_ &&
      ad_tracker_->IsAdScriptInStack(AdTracker::StackType::kBottomAndTop,
                                     /*out_ad_script=*/&ad_script_on_stack);
  Initialize();

  probe::FrameAttachedToParent(this, ad_script_on_stack);
}

FrameScheduler* LocalFrame::GetFrameScheduler() {
  return frame_scheduler_.get();
}

EventHandlerRegistry& LocalFrame::GetEventHandlerRegistry() const {
  return event_handler_->GetEventHandlerRegistry();
}

scoped_refptr<base::SingleThreadTaskRunner> LocalFrame::GetTaskRunner(
    TaskType type) {
  DCHECK(IsMainThread());
  return frame_scheduler_->GetTaskRunner(type);
}

void LocalFrame::ScheduleVisualUpdateUnlessThrottled() {
  if (ShouldThrottleRendering())
    return;
  GetPage()->Animator().ScheduleVisualUpdate(this);
}

static bool CanAccessAncestor(const SecurityOrigin& active_security_origin,
                              const Frame* target_frame) {
  // targetFrame can be 0 when we're trying to navigate a top-level frame
  // that has a 0 opener.
  if (!target_frame)
    return false;

  const bool is_local_active_origin = active_security_origin.IsLocal();
  for (const Frame* ancestor_frame = target_frame; ancestor_frame;
       ancestor_frame = ancestor_frame->Tree().Parent()) {
    const SecurityOrigin* ancestor_security_origin =
        ancestor_frame->GetSecurityContext()->GetSecurityOrigin();
    if (active_security_origin.CanAccess(ancestor_security_origin))
      return true;

    // Allow file URL descendant navigation even when
    // allowFileAccessFromFileURLs is false.
    // FIXME: It's a bit strange to special-case local origins here. Should we
    // be doing something more general instead?
    if (is_local_active_origin && ancestor_security_origin->IsLocal())
      return true;
  }

  return false;
}

// `initiating_frame` - The frame that CanNavigate was initially requested for.
// `source_frame` - The frame that is currently being tested to see if it can
//                  navigate `target_frame`.
// `target_frame` - The frame to be navigated.
// `destination_url` - The URL to navigate to on `target_frame`.
static bool CanNavigateHelper(LocalFrame& initiating_frame,
                              const Frame& source_frame,
                              const Frame& target_frame,
                              const KURL& destination_url) {
  // The only time the helper is called with a different `initiating_frame` from
  // its `source_frame` is to recursively check if ancestors can navigate the
  // top frame.
  DCHECK(&initiating_frame == &source_frame ||
         target_frame ==
             initiating_frame.Tree().Top(FrameTreeBoundary::kIgnoreFence));

  // Only report navigation blocking on the initial call to CanNavigateHelper,
  // not the recursive calls.
  bool should_report = &initiating_frame == &source_frame;

  if (&target_frame == &source_frame)
    return true;

  // Navigating window.opener cross origin, without user activation. See
  // https://crbug.com/813643.
  if (should_report && source_frame.Opener() == target_frame &&
      !source_frame.HasTransientUserActivation() &&
      !target_frame.GetSecurityContext()->GetSecurityOrigin()->CanAccess(
          SecurityOrigin::Create(destination_url).get())) {
    UseCounter::Count(initiating_frame.GetDocument(),
                      WebFeature::kOpenerNavigationWithoutGesture);
  }

  const bool target_escapes_fenced_frame =
      source_frame.IsInFencedFrameTree() &&
      (source_frame.Tree().Top(FrameTreeBoundary::kFenced) !=
       target_frame.Tree().Top(FrameTreeBoundary::kFenced));

  // If the target frame is outside the fenced frame, the only way that should
  // be possible is through the '_unfencedTop' reserved frame name.
  if (target_escapes_fenced_frame) {
    CHECK(target_frame == source_frame.Tree().Top());
  }

  if (destination_url.ProtocolIsJavaScript() &&
      (!source_frame.GetSecurityContext()->GetSecurityOrigin()->CanAccess(
           target_frame.GetSecurityContext()->GetSecurityOrigin()) ||
       target_escapes_fenced_frame)) {
    if (should_report) {
      initiating_frame.PrintNavigationErrorMessage(
          target_frame,
          target_escapes_fenced_frame
              ? "The frame attempting navigation must be in the same fenced "
                "frame tree as the target if navigating to a javascript: url"
              : "The frame attempting navigation must be same-origin with the "
                "target if navigating to a javascript: url");
    }
    return false;
  }

  if (source_frame.GetSecurityContext()->IsSandboxed(
          network::mojom::blink::WebSandboxFlags::kNavigation)) {
    // 'allow-top-navigation' and 'allow-top-navigation-by-user-activation'
    // allow the outermost frame navigations. They don't allow root fenced frame
    // navigations from the descendant frames.
    const bool target_is_outermost_frame =
        target_frame.IsMainFrame() &&
        !target_frame.GetPage()->IsMainFrameFencedFrameRoot();

    if (!target_frame.Tree().IsDescendantOf(&source_frame) &&
        !target_is_outermost_frame) {
      if (should_report) {
        initiating_frame.PrintNavigationErrorMessage(
            target_frame,
            source_frame.IsInFencedFrameTree()
                ? "The frame attempting navigation is in a fenced frame tree, "
                  "and is therefore disallowed from navigating its ancestors."
                : "The frame attempting navigation is sandboxed, and is "
                  "therefore "
                  "disallowed from navigating its ancestors.");
      }
      return false;
    }

    // Sandboxed frames can also navigate popups, if the
    // 'allow-sandbox-escape-via-popup' flag is specified, or if
    // 'allow-popups' flag is specified and the popup's opener is the frame.
    if (target_is_outermost_frame &&
        target_frame !=
            source_frame.Tree().Top(FrameTreeBoundary::kIgnoreFence) &&
        source_frame.GetSecurityContext()->IsSandboxed(
            network::mojom::blink::WebSandboxFlags::
                kPropagatesToAuxiliaryBrowsingContexts) &&
        (source_frame.GetSecurityContext()->IsSandboxed(
             network::mojom::blink::WebSandboxFlags::kPopups) ||
         target_frame.Opener() != &source_frame)) {
      if (should_report) {
        initiating_frame.PrintNavigationErrorMessage(
            target_frame,
            "The frame attempting navigation is sandboxed and is trying "
            "to navigate a popup, but is not the popup's opener and is not "
            "set to propagate sandboxing to popups.");
      }
      return false;
    }

    // Top navigation is forbidden in sandboxed frames unless opted-in, and only
    // then if the ancestor chain allowed to navigate the top frame.
    // Note: We don't check root fenced frames for kTop* flags since the kTop*
    // flags imply the actual top-level page.
    if ((target_frame ==
         source_frame.Tree().Top(FrameTreeBoundary::kIgnoreFence)) &&
        !target_frame.GetPage()->IsMainFrameFencedFrameRoot()) {
      if (source_frame.GetSecurityContext()->IsSandboxed(
              network::mojom::blink::WebSandboxFlags::kTopNavigation) &&
          source_frame.GetSecurityContext()->IsSandboxed(
              network::mojom::blink::WebSandboxFlags::
                  kTopNavigationByUserActivation)) {
        if (should_report) {
          initiating_frame.PrintNavigationErrorMessage(
              target_frame,
              "The frame attempting navigation of the top-level window is "
              "sandboxed, but the flag of 'allow-top-navigation' or "
              "'allow-top-navigation-by-user-activation' is not set.");
        }
        return false;
      }

      // With only 'allow-top-navigation-by-user-activation' (but not
      // 'allow-top-navigation'), top navigation requires a user gesture.
      if (source_frame.GetSecurityContext()->IsSandboxed(
              network::mojom::blink::WebSandboxFlags::kTopNavigation) &&
          !source_frame.GetSecurityContext()->IsSandboxed(
              network::mojom::blink::WebSandboxFlags::
                  kTopNavigationByUserActivation)) {
        // If there is no user activation, fail.
        if (!source_frame.HasTransientUserActivation()) {
          if (should_report) {
            initiating_frame.GetLocalFrameHostRemote().DidBlockNavigation(
                destination_url, initiating_frame.GetDocument()->Url(),
                mojom::NavigationBlockedReason::
                    kRedirectWithNoUserGestureSandbox);
            initiating_frame.PrintNavigationErrorMessage(
                target_frame,
                "The frame attempting navigation of the top-level window is "
                "sandboxed with the 'allow-top-navigation-by-user-activation' "
                "flag, but has no user activation (aka gesture). See "
                "https://www.chromestatus.com/feature/5629582019395584.");
          }
          return false;
        }

        // If we are in a fenced frame and there is user activation, then we
        // know the navigation is allowed. Fenced frames do not propagate
        // user activation into their ancestors outside of the fence, but we
        // want to pretend that they do; upon recursing it would pass the check
        // below for whether the source frame has sticky activation.
        if (target_escapes_fenced_frame) {
          return true;
        }
      }

      // With only 'allow-top-navigation':
      // If the nearest non-sandboxed ancestor frame is not allowed to navigate,
      // then this sandboxed frame can't either. This prevents a cross-origin
      // frame from embedding a sandboxed iframe with kTopNavigate from
      // navigating the top frame. See (crbug.com/1145553)
      if (Frame* parent_frame = source_frame.Tree().Parent()) {
        bool parent_can_navigate = CanNavigateHelper(
            initiating_frame, *parent_frame, target_frame, destination_url);
        if (!parent_can_navigate) {
          if (should_report) {
            String message =
                "The frame attempting navigation of the top-level window is "
                "sandboxed and is not allowed to navigate since its ancestor "
                "frame " +
                FrameDescription(*parent_frame) +
                " is unable to navigate the top frame.\n";
            initiating_frame.PrintNavigationErrorMessage(target_frame, message);
          }
          return false;
        }
      }
      return true;
    }
  }

  DCHECK(source_frame.GetSecurityContext()->GetSecurityOrigin());
  const SecurityOrigin& origin =
      *source_frame.GetSecurityContext()->GetSecurityOrigin();

  // This is the normal case. A document can navigate its decendant frames,
  // or, more generally, a document can navigate a frame if the document is
  // in the same origin as any of that frame's ancestors (in the frame
  // hierarchy).
  //
  // See http://www.adambarth.com/papers/2008/barth-jackson-mitchell.pdf for
  // historical information about this security check.
  if (CanAccessAncestor(origin, &target_frame))
    return true;

  // Top-level frames are easier to navigate than other frames because they
  // display their URLs in the address bar (in most browsers). However, there
  // are still some restrictions on navigation to avoid nuisance attacks.
  // Specifically, a document can navigate a top-level frame if that frame
  // opened the document or if the document is the same-origin with any of
  // the top-level frame's opener's ancestors (in the frame hierarchy).
  //
  // In both of these cases, the document performing the navigation is in
  // some way related to the frame being navigate (e.g., by the "opener"
  // and/or "parent" relation). Requiring some sort of relation prevents a
  // document from navigating arbitrary, unrelated top-level frames.
  if (!target_frame.Tree().Parent()) {
    if (target_frame == source_frame.Opener())
      return true;
    if (CanAccessAncestor(origin, target_frame.Opener()))
      return true;
  }

  if (target_frame == source_frame.Tree().Top()) {
    // A frame navigating its top may blocked if the document initiating
    // the navigation has never received a user gesture and the navigation
    // isn't same-origin with the target.
    if (source_frame.HasStickyUserActivation() ||
        target_frame.GetSecurityContext()->GetSecurityOrigin()->CanAccess(
            SecurityOrigin::Create(destination_url).get())) {
      return true;
    }

    String target_domain = network_utils::GetDomainAndRegistry(
        target_frame.GetSecurityContext()->GetSecurityOrigin()->Domain(),
        network_utils::kIncludePrivateRegistries);
    String destination_domain = network_utils::GetDomainAndRegistry(
        destination_url.Host(), network_utils::kIncludePrivateRegistries);
    if (!target_domain.IsEmpty() && !destination_domain.IsEmpty() &&
        target_domain == destination_domain &&
        (target_frame.GetSecurityContext()->GetSecurityOrigin()->Protocol() ==
             destination_url.Protocol() ||
         !base::FeatureList::IsEnabled(
             features::kBlockCrossOriginTopNavigationToDiffentScheme))) {
      return true;
    }

    // We skip this check for recursive calls on remote frames, in which case
    // we're less permissive.
    if (const LocalFrame* local_frame = DynamicTo<LocalFrame>(&source_frame)) {
      if (auto* settings_client =
              local_frame->Client()->GetContentSettingsClient()) {
        if (settings_client->AllowPopupsAndRedirects(false /* default_value*/))
          return true;
      }
    }

    if (should_report) {
      initiating_frame.PrintNavigationErrorMessage(
          target_frame,
          "The frame attempting navigation is targeting its top-level window, "
          "but is neither same-origin with its target nor has it received a "
          "user gesture. See "
          "https://www.chromestatus.com/feature/5851021045661696.");
      initiating_frame.GetLocalFrameHostRemote().DidBlockNavigation(
          destination_url, initiating_frame.GetDocument()->Url(),
          mojom::NavigationBlockedReason::kRedirectWithNoUserGesture);
    }

  } else {
    if (should_report) {
      initiating_frame.PrintNavigationErrorMessage(
          target_frame,
          "The frame attempting navigation is neither same-origin with the "
          "target, nor is it the target's parent or opener.");
    }
  }
  return false;
}

bool LocalFrame::CanNavigate(const Frame& target_frame,
                             const KURL& destination_url) {
  return CanNavigateHelper(*this, *this, target_frame, destination_url);
}

ContentCaptureManager* LocalFrame::GetOrResetContentCaptureManager() {
  DCHECK(Client());
  if (!IsLocalRoot())
    return nullptr;

  // WebContentCaptureClient is set on each navigation and it could become null
  // because the url is in disallowed list, so ContentCaptureManager
  // is created or released as needed to save the resources.
  // It is a little bit odd that ContentCaptureManager is created or released on
  // demand, and that this is something that could be improved with an explicit
  // signal for creating / destroying content capture managers.
  if (auto* content_capture_client = Client()->GetWebContentCaptureClient()) {
    if (!content_capture_manager_) {
      content_capture_manager_ =
          MakeGarbageCollected<ContentCaptureManager>(*this);
    }
  } else if (content_capture_manager_) {
    content_capture_manager_->Shutdown();
    content_capture_manager_ = nullptr;
  }
  return content_capture_manager_;
}

BrowserInterfaceBrokerProxy& LocalFrame::GetBrowserInterfaceBroker() {
  DCHECK(Client());
  return Client()->GetBrowserInterfaceBroker();
}

AssociatedInterfaceProvider*
LocalFrame::GetRemoteNavigationAssociatedInterfaces() {
  DCHECK(Client());
  return Client()->GetRemoteNavigationAssociatedInterfaces();
}

LocalFrameClient* LocalFrame::Client() const {
  return static_cast<LocalFrameClient*>(Frame::Client());
}

FrameWidget* LocalFrame::GetWidgetForLocalRoot() {
  WebLocalFrameImpl* web_frame = WebLocalFrameImpl::FromFrame(this);
  if (!web_frame)
    return nullptr;
  // This WebFrameWidgetImpl upcasts to a FrameWidget which is the interface
  // exposed to Blink core.
  return web_frame->LocalRootFrameWidget();
}

WebContentSettingsClient* LocalFrame::GetContentSettingsClient() {
  return Client() ? Client()->GetContentSettingsClient() : nullptr;
}

PluginData* LocalFrame::GetPluginData() const {
  if (!Loader().AllowPlugins())
    return nullptr;
  return GetPage()->GetPluginData();
}

void LocalFrame::SetAdTrackerForTesting(AdTracker* ad_tracker) {
  if (ad_tracker_)
    ad_tracker_->Shutdown();
  ad_tracker_ = ad_tracker;
}

DEFINE_WEAK_IDENTIFIER_MAP(LocalFrame)

FrameNavigationDisabler::FrameNavigationDisabler(LocalFrame& frame)
    : frame_(&frame) {
  frame_->DisableNavigation();
}

FrameNavigationDisabler::~FrameNavigationDisabler() {
  frame_->EnableNavigation();
}

namespace {

bool IsScopedFrameBlamerEnabled() {
  // Must match the category used in content::FrameBlameContext.
  static const auto* enabled =
      TRACE_EVENT_API_GET_CATEGORY_GROUP_ENABLED("blink");
  return *enabled;
}

}  // namespace

ScopedFrameBlamer::ScopedFrameBlamer(LocalFrame* frame)
    : frame_(IsScopedFrameBlamerEnabled() ? frame : nullptr) {
  if (LIKELY(!frame_))
    return;
  LocalFrameClient* client = frame_->Client();
  if (!client)
    return;
  if (BlameContext* context = client->GetFrameBlameContext())
    context->Enter();
}

void ScopedFrameBlamer::LeaveContext() {
  LocalFrameClient* client = frame_->Client();
  if (!client)
    return;
  if (BlameContext* context = client->GetFrameBlameContext())
    context->Leave();
}

LocalFrame::LazyLoadImageSetting LocalFrame::GetLazyLoadImageSetting() const {
  DCHECK(GetSettings());
  if (!RuntimeEnabledFeatures::LazyImageLoadingEnabled() ||
      !GetSettings()->GetLazyLoadEnabled()) {
    return LocalFrame::LazyLoadImageSetting::kDisabled;
  }

  // Disable explicit and automatic lazyload for backgrounded pages including
  // NoStatePrefetch and Prerender.
  if (!GetDocument()->IsPageVisible()) {
    return LocalFrame::LazyLoadImageSetting::kDisabled;
  }

  return LocalFrame::LazyLoadImageSetting::kEnabledExplicit;
}

WebURLLoaderFactory* LocalFrame::GetURLLoaderFactory() {
  if (!url_loader_factory_)
    url_loader_factory_ = Client()->CreateURLLoaderFactory();
  return url_loader_factory_.get();
}

WebPluginContainerImpl* LocalFrame::GetWebPluginContainer(Node* node) const {
  if (auto* plugin_document = DynamicTo<PluginDocument>(GetDocument())) {
    return plugin_document->GetPluginView();
  }
  if (!node) {
    DCHECK(GetDocument());
    node = GetDocument()->FocusedElement();
  }

  if (node) {
    return node->GetWebPluginContainer();
  }
  return nullptr;
}

void LocalFrame::WasHidden() {
  if (hidden_)
    return;
  hidden_ = true;

  if (auto* content_capture_manager = GetOrResetContentCaptureManager()) {
    content_capture_manager->OnFrameWasHidden();
  }

  // An iframe may get a "was hidden" notification before it has been attached
  // to the frame tree; in that case, skip further processing.
  if (!Owner() || IsProvisional())
    return;

  // Mark intersections as dirty, so that child frames will reevaluate their
  // render throttling status on the next lifecycle update.
  LocalFrameView* frame_view = View();
  if (frame_view)
    frame_view->SetIntersectionObservationState(LocalFrameView::kDesired);

  // If we are tracking occlusion for this frame, and it was not previously
  // known to be occluded, then we need to force "not visible" notifications to
  // be sent, since it's unknown whether this frame will run lifecycle updates.

  // Frame was already occluded, nothing more to do.
  if (intersection_state_.occlusion_state ==
      mojom::blink::FrameOcclusionState::kPossiblyOccluded) {
    return;
  }

  Document* document = GetDocument();
  if (frame_view && document && document->IsActive()) {
    if (auto* controller = GetDocument()->GetIntersectionObserverController()) {
      if (controller->NeedsOcclusionTracking()) {
        View()->ForceUpdateViewportIntersections();
      }
    }
  }
}

void LocalFrame::WasShown() {
  if (!hidden_)
    return;
  hidden_ = false;
  if (LocalFrameView* frame_view = View())
    frame_view->ScheduleAnimation();

  if (auto* content_capture_manager = GetOrResetContentCaptureManager()) {
    content_capture_manager->OnFrameWasShown();
  }
}

bool LocalFrame::ClipsContent() const {
  // A paint preview shouldn't clip to the viewport. Each frame paints to a
  // separate canvas in full to allow scrolling.
  if (GetDocument()->GetPaintPreviewState() != Document::kNotPaintingPreview)
    return false;

  if (ShouldUsePrintingLayout())
    return false;

  if (IsOutermostMainFrame())
    return GetSettings()->GetMainFrameClipsContent();
  // By default clip to viewport.
  return true;
}

void LocalFrame::SetViewportIntersectionFromParent(
    const mojom::blink::ViewportIntersectionState& intersection_state) {
  DCHECK(IsLocalRoot());
  DCHECK(!IsOutermostMainFrame());
  // Notify the render frame observers when the main frame intersection or the
  // transform changes.
  if (intersection_state_.main_frame_intersection !=
          intersection_state.main_frame_intersection ||
      intersection_state_.main_frame_transform !=
          intersection_state.main_frame_transform) {
    gfx::RectF transform_rect =
        gfx::RectF(gfx::Rect(intersection_state.main_frame_intersection));

    intersection_state.main_frame_transform.TransformRect(&transform_rect);
    gfx::Rect rect = ToEnclosingRect(
        gfx::RectF(transform_rect.x(), transform_rect.y(),
                   transform_rect.width(), transform_rect.height()));

    // Return <0, 0, 0, 0> if there is no area.
    if (rect.IsEmpty())
      rect.set_origin(gfx::Point(0, 0));
    Client()->OnMainFrameIntersectionChanged(rect);
  }

  // We only schedule an update if the viewport intersection or occlusion state
  // has changed; neither the viewport offset nor the compositing bounds will
  // affect IntersectionObserver.
  bool needs_update =
      intersection_state_.viewport_intersection !=
          intersection_state.viewport_intersection ||
      intersection_state_.occlusion_state != intersection_state.occlusion_state;
  intersection_state_ = intersection_state;
  if (needs_update) {
    if (LocalFrameView* frame_view = View()) {
      frame_view->SetIntersectionObservationState(LocalFrameView::kRequired);
      frame_view->ScheduleAnimation();
    }
  }
}

gfx::Size LocalFrame::GetMainFrameViewportSize() const {
  LocalFrame& local_root = LocalFrameRoot();
  return local_root.IsMainFrame()
             ? local_root.View()
                   ->GetScrollableArea()
                   ->VisibleContentRect()
                   .size()
             : local_root.intersection_state_.main_frame_viewport_size;
}

gfx::Point LocalFrame::GetMainFrameScrollPosition() const {
  LocalFrame& local_root = LocalFrameRoot();
  return local_root.IsMainFrame()
             ? gfx::ToFlooredPoint(
                   local_root.View()->LayoutViewport()->ScrollPosition())
             : local_root.intersection_state_.main_frame_scroll_position;
}

void LocalFrame::SetOpener(Frame* opener_frame) {
  // Only a local frame should be able to update another frame's opener.
  DCHECK(!opener_frame || opener_frame->IsLocalFrame());

  auto* web_frame = WebFrame::FromCoreFrame(this);
  if (web_frame && Opener() != opener_frame) {
    GetLocalFrameHostRemote().DidChangeOpener(
        opener_frame
            ? absl::optional<blink::LocalFrameToken>(
                  opener_frame->GetFrameToken().GetAs<LocalFrameToken>())
            : absl::nullopt);
  }
  SetOpenerDoNotNotify(opener_frame);
}

mojom::blink::FrameOcclusionState LocalFrame::GetOcclusionState() const {
  if (hidden_)
    return mojom::blink::FrameOcclusionState::kPossiblyOccluded;
  if (IsLocalRoot())
    return intersection_state_.occlusion_state;
  return LocalFrameRoot().GetOcclusionState();
}

bool LocalFrame::NeedsOcclusionTracking() const {
  if (Document* document = GetDocument()) {
    if (IntersectionObserverController* controller =
            document->GetIntersectionObserverController()) {
      return controller->NeedsOcclusionTracking();
    }
  }
  return false;
}

void LocalFrame::ForceSynchronousDocumentInstall(
    const AtomicString& mime_type,
    scoped_refptr<const SharedBuffer> data) {
  CHECK(GetDocument()->IsInitialEmptyDocument());
  DCHECK(!Client()->IsLocalFrameClientImpl());
  DCHECK(GetPage());

  // Any Document requires Shutdown() before detach, even the initial empty
  // document.
  GetDocument()->Shutdown();
  DomWindow()->ClearForReuse();

  Document* document = DomWindow()->InstallNewDocument(
      DocumentInit::Create()
          .WithWindow(DomWindow(), nullptr)
          .WithTypeFrom(mime_type)
          .ForPrerendering(GetPage()->IsPrerendering()));
  DCHECK_EQ(document, GetDocument());
  DocumentParser* parser = document->OpenForNavigation(
      kForceSynchronousParsing, mime_type, AtomicString("UTF-8"));
  for (const auto& segment : *data)
    parser->AppendBytes(segment.data(), segment.size());
  parser->Finish();

  // Upon loading of SVGImages, log PageVisits in UseCounter if we did not
  // replace the document in `parser->Finish()`, which may happen when XSLT
  // finishes processing.
  // Do not track PageVisits for inspector, web page popups, and validation
  // message overlays (the other callers of this method).
  if (document == GetDocument() && document->IsSVGDocument())
    loader_.GetDocumentLoader()->GetUseCounter().DidCommitLoad(this);
}

bool LocalFrame::IsProvisional() const {
  // Calling this after the frame is marked as completely detached is a bug, as
  // this state can no longer be accurately calculated.
  CHECK(!IsDetached());

  if (IsMainFrame()) {
    return GetPage()->MainFrame() != this;
  }

  DCHECK(Owner());
  return Owner()->ContentFrame() != this;
}

bool LocalFrame::IsAdSubframe() const {
  return ad_evidence_ && ad_evidence_->IndicatesAdSubframe();
}

bool LocalFrame::IsAdRoot() const {
  return IsAdSubframe() && !ad_evidence_->parent_is_ad();
}

void LocalFrame::SetAdEvidence(const blink::FrameAdEvidence& ad_evidence) {
  DCHECK(!IsMainFrame() || IsInFencedFrameTree());
  DCHECK(ad_evidence.is_complete());

  // Once set, `is_subframe_created_by_ad_script_` should not be unset.
  DCHECK(!is_subframe_created_by_ad_script_ ||
         ad_evidence.created_by_ad_script() ==
             blink::mojom::FrameCreationStackEvidence::kCreatedByAdScript);
  is_subframe_created_by_ad_script_ =
      ad_evidence.created_by_ad_script() ==
      blink::mojom::FrameCreationStackEvidence::kCreatedByAdScript;

  if (ad_evidence_.has_value()) {
    // Check that replacing with the new ad evidence doesn't violate invariants.
    // The parent frame's ad status should not change as it can only change due
    // to a cross-document commit, which would remove this child frame.
    DCHECK_EQ(ad_evidence_->parent_is_ad(), ad_evidence.parent_is_ad());

    // The most restrictive filter list result cannot become less restrictive,
    // by definition.
    DCHECK_LE(ad_evidence_->most_restrictive_filter_list_result(),
              ad_evidence.most_restrictive_filter_list_result());
  }

  bool was_ad_subframe = IsAdSubframe();
  bool is_ad_subframe = ad_evidence.IndicatesAdSubframe();
  ad_evidence_ = ad_evidence;

  if (was_ad_subframe == is_ad_subframe)
    return;

  if (auto* document = GetDocument()) {
    // TODO(fdoray): It is possible for the document not to be installed when
    // this method is called. Consider inheriting frame bit in the graph instead
    // of sending an IPC.
    auto* document_resource_coordinator = document->GetResourceCoordinator();
    if (document_resource_coordinator)
      document_resource_coordinator->SetIsAdFrame(is_ad_subframe);
  }

  UpdateAdHighlight();
  frame_scheduler_->SetIsAdFrame(is_ad_subframe);

  if (is_ad_subframe) {
    UseCounter::Count(DomWindow(), WebFeature::kAdFrameDetected);
    InstanceCounters::IncrementCounter(InstanceCounters::kAdSubframeCounter);
  } else {
    InstanceCounters::DecrementCounter(InstanceCounters::kAdSubframeCounter);
  }
}

bool LocalFrame::IsAdScriptInStack() const {
  return ad_tracker_ &&
         ad_tracker_->IsAdScriptInStack(AdTracker::StackType::kBottomAndTop);
}

void LocalFrame::UpdateAdHighlight() {
  if (IsMainFrame() && !IsInFencedFrameTree())
    return;

  // TODO(bokan): Fenced frames may need some work to propagate the ad
  // highlighting setting to the inner tree.
  if (IsAdRoot() && GetPage()->GetSettings().GetHighlightAds())
    SetSubframeColorOverlay(SkColorSetARGB(128, 255, 0, 0));
  else
    SetSubframeColorOverlay(Color::kTransparent);
}

void LocalFrame::PauseSubresourceLoading(
    mojo::PendingReceiver<blink::mojom::blink::PauseSubresourceLoadingHandle>
        receiver) {
  auto handle = GetFrameScheduler()->GetPauseSubresourceLoadingHandle();
  if (!handle)
    return;
  pause_handle_receivers_.Add(std::move(handle), std::move(receiver),
                              GetTaskRunner(blink::TaskType::kInternalDefault));
}

void LocalFrame::ResumeSubresourceLoading() {
  pause_handle_receivers_.Clear();
}

void LocalFrame::AnimateSnapFling(base::TimeTicks monotonic_time) {
  GetEventHandler().AnimateSnapFling(monotonic_time);
}

SmoothScrollSequencer& LocalFrame::GetSmoothScrollSequencer() {
  if (!IsLocalRoot())
    return LocalFrameRoot().GetSmoothScrollSequencer();
  if (!smooth_scroll_sequencer_)
    smooth_scroll_sequencer_ = MakeGarbageCollected<SmoothScrollSequencer>();
  return *smooth_scroll_sequencer_;
}

ukm::UkmRecorder* LocalFrame::GetUkmRecorder() {
  Document* document = GetDocument();
  if (!document)
    return nullptr;
  return document->UkmRecorder();
}

int64_t LocalFrame::GetUkmSourceId() {
  Document* document = GetDocument();
  if (!document)
    return ukm::kInvalidSourceId;
  return document->UkmSourceID();
}

void LocalFrame::UpdateTaskTime(base::TimeDelta time) {
  Client()->DidChangeCpuTiming(time);
}

void LocalFrame::UpdateBackForwardCacheDisablingFeatures(
    uint64_t features_mask) {
  GetBackForwardCacheControllerHostRemote()
      .DidChangeBackForwardCacheDisablingFeatures(features_mask);
}

const base::UnguessableToken& LocalFrame::GetAgentClusterId() const {
  if (const LocalDOMWindow* window = DomWindow()) {
    return window->GetAgentClusterID();
  }
  return base::UnguessableToken::Null();
}

mojom::blink::ReportingServiceProxy* LocalFrame::GetReportingService() {
  return mojo_handler_->ReportingService();
}

// static
void LocalFrame::NotifyUserActivation(
    LocalFrame* frame,
    mojom::blink::UserActivationNotificationType notification_type,
    bool need_browser_verification) {
  if (frame) {
    frame->NotifyUserActivation(notification_type, need_browser_verification);
  }
}

// static
bool LocalFrame::HasTransientUserActivation(LocalFrame* frame) {
  return frame ? frame->Frame::HasTransientUserActivation() : false;
}

// static
bool LocalFrame::ConsumeTransientUserActivation(
    LocalFrame* frame,
    UserActivationUpdateSource update_source) {
  return frame ? frame->ConsumeTransientUserActivation(update_source) : false;
}

void LocalFrame::NotifyUserActivation(
    mojom::blink::UserActivationNotificationType notification_type,
    bool need_browser_verification) {
  mojom::blink::UserActivationUpdateType update_type =
      need_browser_verification
          ? mojom::blink::UserActivationUpdateType::
                kNotifyActivationPendingBrowserVerification
          : mojom::blink::UserActivationUpdateType::kNotifyActivation;

  GetLocalFrameHostRemote().UpdateUserActivationState(update_type,
                                                      notification_type);
  Client()->NotifyUserActivation();
  NotifyUserActivationInFrameTree(notification_type);
  DomWindow()->closewatcher_stack()->DidReceiveUserActivation();
}

bool LocalFrame::ConsumeTransientUserActivation(
    UserActivationUpdateSource update_source) {
  if (update_source == UserActivationUpdateSource::kRenderer) {
    GetLocalFrameHostRemote().UpdateUserActivationState(
        mojom::blink::UserActivationUpdateType::kConsumeTransientActivation,
        mojom::blink::UserActivationNotificationType::kNone);
  }
  return ConsumeTransientUserActivationInFrameTree();
}

namespace {

class FrameColorOverlay final : public FrameOverlay::Delegate {
 public:
  explicit FrameColorOverlay(LocalFrame* frame, SkColor color)
      : color_(color), frame_(frame) {}

 private:
  void PaintFrameOverlay(const FrameOverlay& frame_overlay,
                         GraphicsContext& graphics_context,
                         const gfx::Size&) const override {
    const auto* view = frame_->View();
    DCHECK(view);
    if (view->Width() == 0 || view->Height() == 0)
      return;
    ScopedPaintChunkProperties properties(
        graphics_context.GetPaintController(),
        view->GetLayoutView()->FirstFragment().LocalBorderBoxProperties(),
        frame_overlay, DisplayItem::kFrameOverlay);
    if (DrawingRecorder::UseCachedDrawingIfPossible(
            graphics_context, frame_overlay, DisplayItem::kFrameOverlay))
      return;
    DrawingRecorder recorder(graphics_context, frame_overlay,
                             DisplayItem::kFrameOverlay,
                             gfx::Rect(view->Size()));
    gfx::RectF rect(0, 0, view->Width(), view->Height());
    graphics_context.FillRect(
        rect, color_,
        PaintAutoDarkMode(view->GetLayoutView()->StyleRef(),
                          DarkModeFilter::ElementRole::kBackground));
  }

  SkColor color_;
  Persistent<LocalFrame> frame_;
};

}  // namespace

void LocalFrame::SetMainFrameColorOverlay(SkColor color) {
  DCHECK(IsMainFrame() && !IsInFencedFrameTree());
  SetFrameColorOverlay(color);
}

void LocalFrame::SetSubframeColorOverlay(SkColor color) {
  DCHECK(!IsMainFrame() || IsInFencedFrameTree());
  SetFrameColorOverlay(color);
}

void LocalFrame::SetFrameColorOverlay(SkColor color) {
  if (frame_color_overlay_)
    frame_color_overlay_.Release()->Destroy();

  if (color == Color::kTransparent)
    return;

  frame_color_overlay_ = MakeGarbageCollected<FrameOverlay>(
      this, std::make_unique<FrameColorOverlay>(this, color));
}

void LocalFrame::UpdateFrameColorOverlayPrePaint() {
  if (frame_color_overlay_)
    frame_color_overlay_->UpdatePrePaint();
}

void LocalFrame::PaintFrameColorOverlay(GraphicsContext& context) {
  if (frame_color_overlay_)
    frame_color_overlay_->Paint(context);
}

void LocalFrame::ForciblyPurgeV8Memory() {
  DomWindow()->NotifyContextDestroyed();

  WindowProxyManager* window_proxy_manager = GetWindowProxyManager();
  window_proxy_manager->ClearForV8MemoryPurge();
  Loader().StopAllLoaders(/*abort_client=*/true);
}

void LocalFrame::OnPageLifecycleStateUpdated() {
  if (frozen_ != GetPage()->Frozen()) {
    frozen_ = GetPage()->Frozen();
    if (frozen_) {
      DidFreeze();
    } else {
      DidResume();
    }
    // The event handlers might have detached the frame.
    if (!IsAttached())
      return;
  }
  SetContextPaused(GetPage()->Paused());

  mojom::blink::FrameLifecycleState frame_lifecycle_state =
      mojom::blink::FrameLifecycleState::kRunning;
  if (GetPage()->Paused()) {
    frame_lifecycle_state = mojom::blink::FrameLifecycleState::kPaused;
  } else if (GetPage()->Frozen()) {
    frame_lifecycle_state = mojom::blink::FrameLifecycleState::kFrozen;
  }

  DomWindow()->SetLifecycleState(frame_lifecycle_state);
}

void LocalFrame::SetContextPaused(bool is_paused) {
  TRACE_EVENT0("blink", "LocalFrame::SetContextPaused");
  if (is_paused == paused_)
    return;
  paused_ = is_paused;

  GetDocument()->Fetcher()->SetDefersLoading(GetLoaderFreezeMode());
  Loader().SetDefersLoading(GetLoaderFreezeMode());
  // TODO(altimin): Move this to PageScheduler level.
  GetFrameScheduler()->SetPaused(is_paused);
}

bool LocalFrame::SwapIn() {
  DCHECK(IsProvisional());
  WebLocalFrameClient* client = Client()->GetWebFrame()->Client();
  return client->SwapIn(WebFrame::FromCoreFrame(GetProvisionalOwnerFrame()));
}

void LocalFrame::LoadJavaScriptURL(const KURL& url) {
  // Protect privileged pages against bookmarklets and other JavaScript
  // manipulations.
  if (SchemeRegistry::ShouldTreatURLSchemeAsNotAllowingJavascriptURLs(
          GetDocument()->Url().Protocol()))
    return;

  // TODO(mustaq): This is called only through the user typing a javascript URL
  // into the omnibox.  See https://crbug.com/1082900
  NotifyUserActivation(
      mojom::blink::UserActivationNotificationType::kInteraction, false);
  DomWindow()->GetScriptController().ExecuteJavaScriptURL(
      url, network::mojom::CSPDisposition::DO_NOT_CHECK,
      &DOMWrapperWorld::MainWorld());
}

void LocalFrame::SetEvictCachedSessionStorageOnFreezeOrUnload() {
  DCHECK(RuntimeEnabledFeatures::Prerender2Enabled(
      GetDocument()->GetExecutionContext()));
  evict_cached_session_storage_on_freeze_or_unload_ = true;
}

LocalFrameToken LocalFrame::GetLocalFrameToken() const {
  return GetFrameToken().GetAs<LocalFrameToken>();
}

LoaderFreezeMode LocalFrame::GetLoaderFreezeMode() {
  if (GetPage()->GetPageScheduler()->IsInBackForwardCache() &&
      IsInflightNetworkRequestBackForwardCacheSupportEnabled()) {
    return LoaderFreezeMode::kBufferIncoming;
  }
  if (paused_ || frozen_)
    return LoaderFreezeMode::kStrict;
  return LoaderFreezeMode::kNone;
}

void LocalFrame::DidFreeze() {
  TRACE_EVENT0("blink", "LocalFrame::DidFreeze");
  DCHECK(IsAttached());
  GetDocument()->DispatchFreezeEvent();
  if (evict_cached_session_storage_on_freeze_or_unload_) {
    // Evicts the cached data of Session Storage to avoid reusing old data in
    // the cache after the session storage has been modified by another renderer
    // process.
    CoreInitializer::GetInstance().EvictSessionStorageCachedData(
        GetDocument()->GetPage());
  }
  // DispatchFreezeEvent dispatches JS events, which may detach |this|.
  if (!IsAttached())
    return;
  // TODO(fmeawad): Move the following logic to the page once we have a
  // PageResourceCoordinator in Blink. http://crbug.com/838415
  if (auto* document_resource_coordinator =
          GetDocument()->GetResourceCoordinator()) {
    document_resource_coordinator->SetLifecycleState(
        performance_manager::mojom::LifecycleState::kFrozen);
  }

  if (GetPage()->GetPageScheduler()->IsInBackForwardCache()) {
    DomWindow()->SetIsInBackForwardCache(true);
  }

  LoaderFreezeMode freeze_mode = GetLoaderFreezeMode();
  GetDocument()->Fetcher()->SetDefersLoading(freeze_mode);
  Loader().SetDefersLoading(freeze_mode);
}

void LocalFrame::DidResume() {
  TRACE_EVENT0("blink", "LocalFrame::DidResume");
  DCHECK(IsAttached());
  const base::TimeTicks resume_event_start = base::TimeTicks::Now();
  GetDocument()->DispatchEvent(*Event::Create(event_type_names::kResume));
  const base::TimeTicks resume_event_end = base::TimeTicks::Now();
  base::UmaHistogramMicrosecondsTimes("DocumentEventTiming.ResumeDuration",
                                      resume_event_end - resume_event_start);
  // TODO(fmeawad): Move the following logic to the page once we have a
  // PageResourceCoordinator in Blink
  if (auto* document_resource_coordinator =
          GetDocument()->GetResourceCoordinator()) {
    document_resource_coordinator->SetLifecycleState(
        performance_manager::mojom::LifecycleState::kRunning);
  }

  // TODO(yuzus): Figure out if we should call GetLoaderFreezeMode().
  GetDocument()->Fetcher()->SetDefersLoading(LoaderFreezeMode::kNone);
  Loader().SetDefersLoading(LoaderFreezeMode::kNone);

  DomWindow()->SetIsInBackForwardCache(false);

  // TODO(yuzus): Figure out where these calls should really belong.
  GetDocument()->DispatchHandleLoadStart();
  GetDocument()->DispatchHandleLoadOrLayoutComplete();
}

void LocalFrame::MaybeLogAdClickNavigation() {
  if (HasTransientUserActivation(this) && IsAdSubframe())
    UseCounter::Count(GetDocument(), WebFeature::kAdClickNavigation);
}

void LocalFrame::CountUseIfFeatureWouldBeBlockedByPermissionsPolicy(
    mojom::WebFeature blocked_cross_origin,
    mojom::WebFeature blocked_same_origin) {
  // Get the origin of the top-level document
  const SecurityOrigin* topOrigin =
      Tree().Top().GetSecurityContext()->GetSecurityOrigin();

  // Check if this frame is same-origin with the top-level or is in
  // a fenced frame tree.
  if (!GetSecurityContext()->GetSecurityOrigin()->CanAccess(topOrigin) ||
      IsInFencedFrameTree()) {
    // This frame is cross-origin with the top-level frame, and so would be
    // blocked without a permissions policy.
    UseCounter::Count(GetDocument(), blocked_cross_origin);
    return;
  }

  // Walk up the frame tree looking for any cross-origin embeds. Even if this
  // frame is same-origin with the top-level, if it is embedded by a cross-
  // origin frame (like A->B->A) it would be blocked without a permissions
  // policy.
  const Frame* f = this;
  while (!f->IsMainFrame()) {
    if (!f->GetSecurityContext()->GetSecurityOrigin()->CanAccess(topOrigin)) {
      UseCounter::Count(GetDocument(), blocked_same_origin);
      return;
    }
    f = f->Tree().Parent();
  }
}

void LocalFrame::FinishedLoading(FrameLoader::NavigationFinishState state) {
  DomWindow()->FinishedLoading(state);
}

void LocalFrame::UpdateFaviconURL() {
  if (!IsMainFrame())
    return;

  // The URL to the icon may be in the header. As such, only
  // ask the loader for the icon if it's finished loading.
  if (!GetDocument()->LoadEventFinished())
    return;

  int icon_types_mask =
      1 << static_cast<int>(mojom::blink::FaviconIconType::kFavicon) |
      1 << static_cast<int>(mojom::blink::FaviconIconType::kTouchIcon) |
      1 << static_cast<int>(
          mojom::blink::FaviconIconType::kTouchPrecomposedIcon);
  Vector<IconURL> icon_urls = GetDocument()->IconURLs(icon_types_mask);
  if (icon_urls.IsEmpty())
    return;

  Vector<mojom::blink::FaviconURLPtr> urls;
  urls.ReserveCapacity(icon_urls.size());
  for (const auto& icon_url : icon_urls) {
    urls.push_back(mojom::blink::FaviconURL::New(
        icon_url.icon_url_, icon_url.icon_type_, icon_url.sizes_));
  }
  DCHECK_EQ(icon_urls.size(), urls.size());

  GetLocalFrameHostRemote().UpdateFaviconURL(std::move(urls));

  if (GetPage())
    GetPage()->GetPageScheduler()->OnTitleOrFaviconUpdated();
}

void LocalFrame::SetIsCapturingMediaCallback(
    IsCapturingMediaCallback callback) {
  is_capturing_media_callback_ = std::move(callback);
}

bool LocalFrame::IsCapturingMedia() const {
  return is_capturing_media_callback_ ? is_capturing_media_callback_.Run()
                                      : false;
}

SystemClipboard* LocalFrame::GetSystemClipboard() {
  if (!system_clipboard_)
    system_clipboard_ = MakeGarbageCollected<SystemClipboard>(this);

  return system_clipboard_.Get();
}

void LocalFrame::WasAttachedAsLocalMainFrame() {
  mojo_handler_->WasAttachedAsLocalMainFrame();
}

void LocalFrame::EvictFromBackForwardCache(
    mojom::blink::RendererEvictionReason reason) {
  if (!GetPage()->GetPageScheduler()->IsInBackForwardCache())
    return;
  UMA_HISTOGRAM_ENUMERATION("BackForwardCache.Eviction.Renderer", reason);
  GetBackForwardCacheControllerHostRemote().EvictFromBackForwardCache(reason);
}

void LocalFrame::DidBufferLoadWhileInBackForwardCache(size_t num_bytes) {
  DomWindow()->DidBufferLoadWhileInBackForwardCache(num_bytes);
}

void LocalFrame::SetScaleFactor(float scale_factor) {
  DCHECK(IsMainFrame());

  const PageScaleConstraints& constraints =
      GetPage()->GetPageScaleConstraintsSet().FinalConstraints();
  scale_factor = constraints.ClampToConstraints(scale_factor);
  if (scale_factor == GetPage()->GetVisualViewport().Scale())
    return;
  GetPage()->GetVisualViewport().SetScale(scale_factor);
}

void LocalFrame::ClosePageForTesting() {
  mojo_handler_->ClosePageForTesting();
}

void LocalFrame::SetInitialFocus(bool reverse) {
  GetDocument()->ClearFocusedElement();
  GetPage()->GetFocusController().SetInitialFocus(
      reverse ? mojom::blink::FocusType::kBackward
              : mojom::blink::FocusType::kForward);
}

#if BUILDFLAG(IS_MAC)
void LocalFrame::GetCharacterIndexAtPoint(const gfx::Point& point) {
  HitTestLocation location(View()->ViewportToFrame(gfx::Point(point)));
  HitTestResult result = GetEventHandler().HitTestResultAtLocation(
      location, HitTestRequest::kReadOnly | HitTestRequest::kActive);
  uint32_t index =
      Selection().CharacterIndexForPoint(result.RoundedPointInInnerNodeFrame());
  mojo_handler_->TextInputHost().GotCharacterIndexAtPoint(index);
}
#endif

#if !BUILDFLAG(IS_ANDROID)
void LocalFrame::UpdateWindowControlsOverlay(
    const gfx::Rect& bounding_rect_in_dips) {
  if (!RuntimeEnabledFeatures::WebAppWindowControlsOverlayEnabled(
          GetDocument()->GetExecutionContext())) {
    return;
  }

  // The rect passed to us from content is in DIP screen space, relative to the
  // main frame, and needs to move to CSS space. This doesn't take the page's
  // zoom factor into account so we must scale by the inverse of the page zoom
  // in order to get correct CSS space coordinates. Note that when
  // use-zoom-for-dsf is enabled, WindowToViewportScalar will be the true device
  // scale factor, and PageZoomFactor will be the combination of the device
  // scale factor and the zoom percent of the page. It is preferable to compute
  // a rect that is slightly larger than one that would render smaller than the
  // window control overlay.
  LocalFrame& local_frame_root = LocalFrameRoot();
  const float window_to_viewport_factor =
      GetPage()->GetChromeClient().WindowToViewportScalar(&local_frame_root,
                                                          1.0f);
  const float zoom_factor = local_frame_root.PageZoomFactor();
  const float scale_factor = zoom_factor / window_to_viewport_factor;
  gfx::Rect window_controls_overlay_rect =
      gfx::ScaleToEnclosingRect(bounding_rect_in_dips, 1.0f / scale_factor);

  bool fire_event =
      (window_controls_overlay_rect != window_controls_overlay_rect_);
  is_window_controls_overlay_visible_ = !window_controls_overlay_rect.IsEmpty();
  window_controls_overlay_rect_ = window_controls_overlay_rect;
  window_controls_overlay_rect_in_dips_ = bounding_rect_in_dips;

  DocumentStyleEnvironmentVariables& vars =
      GetDocument()->GetStyleEngine().EnsureEnvironmentVariables();

  if (is_window_controls_overlay_visible_) {
    SetTitlebarAreaDocumentStyleEnvironmentVariables();
  } else {
    const UADefinedVariable vars_to_remove[] = {
        UADefinedVariable::kTitlebarAreaX,
        UADefinedVariable::kTitlebarAreaY,
        UADefinedVariable::kTitlebarAreaWidth,
        UADefinedVariable::kTitlebarAreaHeight,
    };
    for (auto var_to_remove : vars_to_remove) {
      vars.RemoveVariable(var_to_remove);
    }
  }

  if (fire_event && window_controls_overlay_changed_delegate_) {
    window_controls_overlay_changed_delegate_->WindowControlsOverlayChanged(
        window_controls_overlay_rect_);
  }
}

void LocalFrame::RegisterWindowControlsOverlayChangedDelegate(
    WindowControlsOverlayChangedDelegate* delegate) {
  window_controls_overlay_changed_delegate_ = delegate;
}
#endif

HitTestResult LocalFrame::HitTestResultForVisualViewportPos(
    const gfx::Point& pos_in_viewport) {
  gfx::Point root_frame_point(
      GetPage()->GetVisualViewport().ViewportToRootFrame(pos_in_viewport));
  HitTestLocation location(View()->ConvertFromRootFrame(root_frame_point));
  HitTestResult result = GetEventHandler().HitTestResultAtLocation(
      location, HitTestRequest::kReadOnly | HitTestRequest::kActive);
  result.SetToShadowHostIfInRestrictedShadowRoot();
  return result;
}

void LocalFrame::DidChangeVisibleToHitTesting() {
  // LayoutEmbeddedContent does not propagate style updates to descendants.
  // Need to update the field manually.
  for (Frame* child = Tree().FirstChild(); child;
       child = child->Tree().NextSibling()) {
    child->UpdateVisibleToHitTesting();
  }

  // The transform property tree node depends on visibility.
  if (auto* view = View()->GetLayoutView()) {
    view->SetNeedsPaintPropertyUpdate();
  }
}

WebPrescientNetworking* LocalFrame::PrescientNetworking() {
  if (!prescient_networking_) {
    WebLocalFrameImpl* web_local_frame = WebLocalFrameImpl::FromFrame(this);
    // There is no valid WebLocalFrame, return a nullptr to ignore pre* hints.
    if (!web_local_frame)
      return nullptr;
    prescient_networking_ =
        web_local_frame->Client()->CreatePrescientNetworking();
  }
  return prescient_networking_.get();
}

void LocalFrame::SetPrescientNetworkingForTesting(
    std::unique_ptr<WebPrescientNetworking> prescient_networking) {
  prescient_networking_ = std::move(prescient_networking);
}

mojom::blink::LocalFrameHost& LocalFrame::GetLocalFrameHostRemote() const {
  return mojo_handler_->LocalFrameHostRemote();
}

mojom::blink::BackForwardCacheControllerHost&
LocalFrame::GetBackForwardCacheControllerHostRemote() {
  return mojo_handler_->BackForwardCacheControllerHostRemote();
}

void LocalFrame::NotifyUserActivation(
    mojom::blink::UserActivationNotificationType notification_type) {
  NotifyUserActivation(notification_type, false);
}

void LocalFrame::RegisterVirtualKeyboardOverlayChangedObserver(
    VirtualKeyboardOverlayChangedObserver* observer) {
  virtual_keyboard_overlay_changed_observers_.insert(observer);
}

void LocalFrame::NotifyVirtualKeyboardOverlayRectObservers(
    const gfx::Rect& rect) const {
  HeapVector<Member<VirtualKeyboardOverlayChangedObserver>, 32> observers;
  CopyToVector(virtual_keyboard_overlay_changed_observers_, observers);
  for (VirtualKeyboardOverlayChangedObserver* observer : observers)
    observer->VirtualKeyboardOverlayChanged(rect);
}

void LocalFrame::AddInspectorIssue(mojom::blink::InspectorIssueInfoPtr info) {
  if (GetPage()) {
    GetPage()->GetInspectorIssueStorage().AddInspectorIssue(DomWindow(),
                                                            std::move(info));
  }
}

void LocalFrame::CopyImageAtViewportPoint(const gfx::Point& viewport_point) {
  HitTestResult result = HitTestResultForVisualViewportPos(viewport_point);
  if (!IsA<HTMLCanvasElement>(result.InnerNodeOrImageMapImage()) &&
      result.AbsoluteImageURL().IsEmpty()) {
    // There isn't actually an image at these coordinates.  Might be because
    // the window scrolled while the context menu was open or because the page
    // changed itself between when we thought there was an image here and when
    // we actually tried to retrieve the image.
    //
    // FIXME: implement a cache of the most recent HitTestResult to avoid having
    //        to do two hit tests.
    return;
  }

  // TODO(editing-dev): The use of UpdateStyleAndLayout
  // needs to be audited.  See http://crbug.com/590369 for more details.
  GetDocument()->UpdateStyleAndLayout(DocumentUpdateReason::kEditing);

  GetEditor().CopyImage(result);
}

void LocalFrame::SaveImageAt(const gfx::Point& window_point) {
  gfx::Point viewport_position =
      GetWidgetForLocalRoot()->DIPsToRoundedBlinkSpace(window_point);
  Node* node = HitTestResultForVisualViewportPos(viewport_position)
                   .InnerNodeOrImageMapImage();
  if (!node || !(IsA<HTMLCanvasElement>(*node) || IsA<HTMLImageElement>(*node)))
    return;

  String url = To<Element>(*node).ImageSourceURL();
  if (!KURL(NullURL(), url).ProtocolIsData())
    return;

  auto params = mojom::blink::DownloadURLParams::New();
  params->is_context_menu_save = true;
  params->data_url_blob = DataURLToBlob(url);
  GetLocalFrameHostRemote().DownloadURL(std::move(params));
}

void LocalFrame::MediaPlayerActionAtViewportPoint(
    const gfx::Point& viewport_position,
    const blink::mojom::blink::MediaPlayerActionType type,
    bool enable) {
  HitTestResult result = HitTestResultForVisualViewportPos(viewport_position);
  Node* node = result.InnerNode();
  if (!IsA<HTMLVideoElement>(*node) && !IsA<HTMLAudioElement>(*node))
    return;

  auto* media_element = To<HTMLMediaElement>(node);
  switch (type) {
    case blink::mojom::blink::MediaPlayerActionType::kPlay:
      if (enable)
        media_element->Play();
      else
        media_element->pause();
      break;
    case blink::mojom::blink::MediaPlayerActionType::kMute:
      media_element->setMuted(enable);
      break;
    case blink::mojom::blink::MediaPlayerActionType::kLoop:
      media_element->SetLoop(enable);
      break;
    case blink::mojom::blink::MediaPlayerActionType::kControls:
      media_element->SetUserWantsControlsVisible(enable);
      break;
    case blink::mojom::blink::MediaPlayerActionType::kPictureInPicture:
      DCHECK(IsA<HTMLVideoElement>(media_element));
      if (enable) {
        PictureInPictureController::From(node->GetDocument())
            .EnterPictureInPicture(To<HTMLVideoElement>(media_element),
                                   /*promise=*/nullptr);
      } else {
        PictureInPictureController::From(node->GetDocument())
            .ExitPictureInPicture(To<HTMLVideoElement>(media_element), nullptr);
      }

      break;
  }
}

void LocalFrame::DownloadURL(
    const ResourceRequest& request,
    network::mojom::blink::RedirectMode cross_origin_redirect_behavior) {
  mojo::PendingRemote<mojom::blink::BlobURLToken> blob_url_token;
  if (request.Url().ProtocolIs("blob")) {
    DomWindow()->GetPublicURLManager().Resolve(
        request.Url(), blob_url_token.InitWithNewPipeAndPassReceiver());
  }

  DownloadURL(request, cross_origin_redirect_behavior,
              std::move(blob_url_token));
}

void LocalFrame::DownloadURL(
    const ResourceRequest& request,
    network::mojom::blink::RedirectMode cross_origin_redirect_behavior,
    mojo::PendingRemote<mojom::blink::BlobURLToken> blob_url_token) {
  if (ShouldThrottleDownload())
    return;

  auto params = mojom::blink::DownloadURLParams::New();
  const KURL& url = request.Url();
  // Pass data URL through blob.
  if (url.ProtocolIs("data")) {
    params->url = KURL();
    params->data_url_blob = DataURLToBlob(url.GetString());
  } else {
    params->url = url;
  }

  params->referrer = mojom::blink::Referrer::New();
  params->referrer->url = KURL(request.ReferrerString());
  params->referrer->policy = request.GetReferrerPolicy();
  params->initiator_origin = request.RequestorOrigin();
  if (request.GetSuggestedFilename().has_value())
    params->suggested_name = *request.GetSuggestedFilename();
  params->cross_origin_redirects = cross_origin_redirect_behavior;
  params->blob_url_token = std::move(blob_url_token);
  params->has_user_gesture = request.HasUserGesture();

  GetLocalFrameHostRemote().DownloadURL(std::move(params));
}

void LocalFrame::AdvanceFocusForIME(mojom::blink::FocusType focus_type) {
  auto* focused_frame = GetPage()->GetFocusController().FocusedFrame();
  if (focused_frame != this)
    return;

  DCHECK(GetDocument());
  Element* element = GetDocument()->FocusedElement();
  if (!element)
    return;

  Element* next_element =
      GetPage()->GetFocusController().NextFocusableElementForIME(element,
                                                                 focus_type);
  if (!next_element)
    return;

  next_element->scrollIntoViewIfNeeded(true /*centerIfNeeded*/);
  next_element->Focus();
}

void LocalFrame::PostMessageEvent(
    const absl::optional<RemoteFrameToken>& source_frame_token,
    const String& source_origin,
    const String& target_origin,
    BlinkTransferableMessage message) {
  TRACE_EVENT0("blink", "LocalFrame::PostMessageEvent");
  RemoteFrame* source_frame = SourceFrameForOptionalToken(source_frame_token);

  // We must pass in the target_origin to do the security check on this side,
  // since it may have changed since the original postMessage call was made.
  scoped_refptr<SecurityOrigin> target_security_origin;
  if (!target_origin.IsEmpty()) {
    target_security_origin = SecurityOrigin::CreateFromString(target_origin);
  }

  // Preparation of the MessageEvent.
  MessageEvent* message_event = MessageEvent::Create();
  DOMWindow* window = nullptr;
  if (source_frame)
    window = source_frame->DomWindow();
  MessagePortArray* ports = nullptr;
  if (GetDocument()) {
    ports = MessagePort::EntanglePorts(*GetDocument()->GetExecutionContext(),
                                       std::move(message.ports));
  }

  // The |message.user_activation| only conveys the sender |Frame|'s user
  // activation state to receiver JS.  This is never used for activating the
  // receiver (or any other) |Frame|.
  UserActivation* user_activation = nullptr;
  if (message.user_activation) {
    user_activation = MakeGarbageCollected<UserActivation>(
        message.user_activation->has_been_active,
        message.user_activation->was_active);
  }

  message_event->initMessageEvent(
      "message", false, false, std::move(message.message), source_origin,
      "" /*lastEventId*/, window, ports, user_activation,
      message.delegated_capability);

  // If the agent cluster id had a value it means this was locked when it
  // was serialized.
  if (message.locked_agent_cluster_id)
    message_event->LockToAgentCluster();

  // Finally dispatch the message to the DOM Window.
  DomWindow()->DispatchMessageEventWithOriginCheck(
      target_security_origin.get(), message_event,
      std::make_unique<SourceLocation>(String(), String(), 0, 0, nullptr),
      message.locked_agent_cluster_id ? message.locked_agent_cluster_id.value()
                                      : base::UnguessableToken());
}

bool LocalFrame::ShouldThrottleDownload() {
  const auto now = base::TimeTicks::Now();
  if (num_burst_download_requests_ == 0) {
    burst_download_start_time_ = now;
  } else if (num_burst_download_requests_ >= kBurstDownloadLimit) {
    static constexpr auto kBurstDownloadLimitResetInterval = base::Seconds(1);
    if (now - burst_download_start_time_ > kBurstDownloadLimitResetInterval) {
      num_burst_download_requests_ = 1;
      burst_download_start_time_ = now;
      return false;
    }
    return true;
  }

  num_burst_download_requests_++;
  return false;
}

#if BUILDFLAG(IS_MAC)
void LocalFrame::ResetTextInputHostForTesting() {
  mojo_handler_->ResetTextInputHostForTesting();
}

void LocalFrame::RebindTextInputHostForTesting() {
  mojo_handler_->RebindTextInputHostForTesting();
}
#endif

Frame* LocalFrame::GetProvisionalOwnerFrame() {
  DCHECK(IsProvisional());
  if (Owner()) {
    // Since `this` is a provisional frame, its owner's `ContentFrame()` will
    // be the old LocalFrame.
    return Owner()->ContentFrame();
  }
  return GetPage()->MainFrame();
}

namespace {

// TODO(editing-dev): We should move |CreateMarkupInRect()| to
// "core/editing/serializers/Serialization.cpp".
String CreateMarkupInRect(LocalFrame* frame,
                          const gfx::Point& start_point,
                          const gfx::Point& end_point) {
  VisiblePosition start_visible_position = CreateVisiblePosition(
      PositionForContentsPointRespectingEditingBoundary(start_point, frame));
  VisiblePosition end_visible_position = CreateVisiblePosition(
      PositionForContentsPointRespectingEditingBoundary(end_point, frame));

  Position start_position = start_visible_position.DeepEquivalent();
  Position end_position = end_visible_position.DeepEquivalent();

  // document() will return null if -webkit-user-select is set to none.
  if (!start_position.GetDocument() || !end_position.GetDocument())
    return String();

  const CreateMarkupOptions create_markup_options =
      CreateMarkupOptions::Builder()
          .SetShouldAnnotateForInterchange(true)
          .SetShouldResolveURLs(kResolveNonLocalURLs)
          .Build();
  if (start_position.CompareTo(end_position) <= 0) {
    return CreateMarkup(start_position, end_position, create_markup_options);
  }
  return CreateMarkup(end_position, start_position, create_markup_options);
}

}  // namespace

void LocalFrame::ExtractSmartClipDataInternal(const gfx::Rect& rect_in_viewport,
                                              String& clip_text,
                                              String& clip_html,
                                              gfx::Rect& clip_rect) {
  // TODO(mahesh.ma): Check clip_data even after use-zoom-for-dsf is enabled.
  SmartClipData clip_data = SmartClip(this).DataForRect(rect_in_viewport);
  clip_text = clip_data.ClipData();
  clip_rect = clip_data.RectInViewport();

  gfx::Point start_point(rect_in_viewport.x(), rect_in_viewport.y());
  gfx::Point end_point(rect_in_viewport.x() + rect_in_viewport.width(),
                       rect_in_viewport.y() + rect_in_viewport.height());
  clip_html = CreateMarkupInRect(this, View()->ViewportToFrame(start_point),
                                 View()->ViewportToFrame(end_point));
}

void LocalFrame::CreateTextFragmentHandler() {
  text_fragment_handler_ = MakeGarbageCollected<TextFragmentHandler>(this);
}

void LocalFrame::BindTextFragmentReceiver(
    mojo::PendingReceiver<mojom::blink::TextFragmentReceiver> receiver) {
  if (IsDetached())
    return;

  if (!text_fragment_handler_)
    CreateTextFragmentHandler();

  text_fragment_handler_->BindTextFragmentReceiver(std::move(receiver));
}

SpellChecker& LocalFrame::GetSpellChecker() const {
  DCHECK(DomWindow());
  return DomWindow()->GetSpellChecker();
}

InputMethodController& LocalFrame::GetInputMethodController() const {
  DCHECK(DomWindow());
  return DomWindow()->GetInputMethodController();
}

TextSuggestionController& LocalFrame::GetTextSuggestionController() const {
  DCHECK(DomWindow());
  return DomWindow()->GetTextSuggestionController();
}

void LocalFrame::WriteIntoTrace(perfetto::TracedValue ctx) const {
  perfetto::TracedDictionary dict = std::move(ctx).WriteDictionary();
  dict.Add("document", GetDocument());
  dict.Add("is_main_frame", IsMainFrame());
  dict.Add("is_outermost_main_frame", IsOutermostMainFrame());
  dict.Add("is_cross_origin_to_parent", IsCrossOriginToParentOrOuterDocument());
  dict.Add("is_cross_origin_to_outermost_main_frame",
           IsCrossOriginToOutermostMainFrame());
}

#if !BUILDFLAG(IS_ANDROID)
void LocalFrame::SetTitlebarAreaDocumentStyleEnvironmentVariables() const {
  DCHECK(is_window_controls_overlay_visible_);
  DocumentStyleEnvironmentVariables& vars =
      GetDocument()->GetStyleEngine().EnsureEnvironmentVariables();
  vars.SetVariable(
      UADefinedVariable::kTitlebarAreaX,
      StyleEnvironmentVariables::FormatPx(window_controls_overlay_rect_.x()));
  vars.SetVariable(
      UADefinedVariable::kTitlebarAreaY,
      StyleEnvironmentVariables::FormatPx(window_controls_overlay_rect_.y()));
  vars.SetVariable(UADefinedVariable::kTitlebarAreaWidth,
                   StyleEnvironmentVariables::FormatPx(
                       window_controls_overlay_rect_.width()));
  vars.SetVariable(UADefinedVariable::kTitlebarAreaHeight,
                   StyleEnvironmentVariables::FormatPx(
                       window_controls_overlay_rect_.height()));
}

void LocalFrame::MaybeUpdateWindowControlsOverlayWithNewZoomLevel() {
  // |window_controls_overlay_rect_| is only set for local root.
  if (!is_window_controls_overlay_visible_ || !IsLocalRoot())
    return;

  DCHECK(!window_controls_overlay_rect_in_dips_.IsEmpty());

  UpdateWindowControlsOverlay(window_controls_overlay_rect_in_dips_);
}
#endif  // !BUILDFLAG(IS_ANDROID)

}  // namespace blink
