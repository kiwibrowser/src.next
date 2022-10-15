/*
 * Copyright (C) 1998, 1999 Torben Weis <weis@kde.org>
 *                     1999 Lars Knoll <knoll@kde.org>
 *                     1999 Antti Koivisto <koivisto@kde.org>
 *                     2000 Dirk Mueller <mueller@kde.org>
 * Copyright (C) 2004, 2005, 2006, 2007, 2008 Apple Inc. All rights reserved.
 *           (C) 2006 Graham Dennis (graham.dennis@gmail.com)
 *           (C) 2006 Alexey Proskuryakov (ap@nypop.com)
 * Copyright (C) 2009 Google Inc. All rights reserved.
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

#include "third_party/blink/renderer/core/frame/local_frame_view.h"

#include <algorithm>
#include <memory>
#include <utility>

#include "base/auto_reset.h"
#include "base/callback.h"
#include "base/feature_list.h"
#include "base/functional/function_ref.h"
#include "base/memory/ptr_util.h"
#include "base/metrics/field_trial_params.h"
#include "base/numerics/safe_conversions.h"
#include "base/timer/lap_timer.h"
#include "base/trace_event/typed_macros.h"
#include "cc/animation/animation_host.h"
#include "cc/animation/animation_timeline.h"
#include "cc/base/features.h"
#include "cc/document_transition/document_transition_request.h"
#include "cc/input/main_thread_scrolling_reason.h"
#include "cc/layers/picture_layer.h"
#include "cc/tiles/frame_viewer_instrumentation.h"
#include "cc/trees/layer_tree_host.h"
#include "components/paint_preview/common/paint_preview_tracker.h"
#include "third_party/blink/public/common/features.h"
#include "third_party/blink/public/mojom/frame/frame.mojom-blink.h"
#include "third_party/blink/public/mojom/frame/remote_frame.mojom-blink.h"
#include "third_party/blink/public/mojom/scroll/scroll_into_view_params.mojom-blink.h"
#include "third_party/blink/public/mojom/scroll/scrollbar_mode.mojom-blink.h"
#include "third_party/blink/public/platform/task_type.h"
#include "third_party/blink/renderer/bindings/core/v8/capture_source_location.h"
#include "third_party/blink/renderer/bindings/core/v8/v8_binding_for_core.h"
#include "third_party/blink/renderer/bindings/core/v8/v8_scroll_into_view_options.h"
#include "third_party/blink/renderer/core/accessibility/ax_object_cache.h"
#include "third_party/blink/renderer/core/animation/document_animations.h"
#include "third_party/blink/renderer/core/css/font_face_set_document.h"
#include "third_party/blink/renderer/core/css/post_style_update_scope.h"
#include "third_party/blink/renderer/core/css/style_change_reason.h"
#include "third_party/blink/renderer/core/display_lock/display_lock_document_state.h"
#include "third_party/blink/renderer/core/display_lock/display_lock_utilities.h"
#include "third_party/blink/renderer/core/document_transition/document_transition_supplement.h"
#include "third_party/blink/renderer/core/dom/static_node_list.h"
#include "third_party/blink/renderer/core/editing/drag_caret.h"
#include "third_party/blink/renderer/core/editing/frame_selection.h"
#include "third_party/blink/renderer/core/editing/markers/document_marker_controller.h"
#include "third_party/blink/renderer/core/events/error_event.h"
#include "third_party/blink/renderer/core/exported/web_plugin_container_impl.h"
#include "third_party/blink/renderer/core/fragment_directive/fragment_directive_utils.h"
#include "third_party/blink/renderer/core/fragment_directive/text_fragment_handler.h"
#include "third_party/blink/renderer/core/frame/browser_controls.h"
#include "third_party/blink/renderer/core/frame/find_in_page.h"
#include "third_party/blink/renderer/core/frame/frame_overlay.h"
#include "third_party/blink/renderer/core/frame/frame_view_auto_size_info.h"
#include "third_party/blink/renderer/core/frame/local_dom_window.h"
#include "third_party/blink/renderer/core/frame/local_frame.h"
#include "third_party/blink/renderer/core/frame/local_frame_client.h"
#include "third_party/blink/renderer/core/frame/local_frame_ukm_aggregator.h"
#include "third_party/blink/renderer/core/frame/location.h"
#include "third_party/blink/renderer/core/frame/page_scale_constraints_set.h"
#include "third_party/blink/renderer/core/frame/remote_frame.h"
#include "third_party/blink/renderer/core/frame/remote_frame_view.h"
#include "third_party/blink/renderer/core/frame/root_frame_viewport.h"
#include "third_party/blink/renderer/core/frame/settings.h"
#include "third_party/blink/renderer/core/frame/visual_viewport.h"
#include "third_party/blink/renderer/core/frame/web_local_frame_impl.h"
#include "third_party/blink/renderer/core/fullscreen/fullscreen.h"
#include "third_party/blink/renderer/core/highlight/highlight_registry.h"
#include "third_party/blink/renderer/core/html/fenced_frame/document_fenced_frames.h"
#include "third_party/blink/renderer/core/html/fenced_frame/html_fenced_frame_element.h"
#include "third_party/blink/renderer/core/html/forms/text_control_element.h"
#include "third_party/blink/renderer/core/html/html_frame_element.h"
#include "third_party/blink/renderer/core/html/html_plugin_element.h"
#include "third_party/blink/renderer/core/html/media/html_video_element.h"
#include "third_party/blink/renderer/core/html/parser/text_resource_decoder.h"
#include "third_party/blink/renderer/core/html/portal/document_portals.h"
#include "third_party/blink/renderer/core/html/portal/html_portal_element.h"
#include "third_party/blink/renderer/core/html/portal/portal_contents.h"
#include "third_party/blink/renderer/core/html_names.h"
#include "third_party/blink/renderer/core/input/event_handler.h"
#include "third_party/blink/renderer/core/inspector/inspector_trace_events.h"
#include "third_party/blink/renderer/core/intersection_observer/intersection_observation.h"
#include "third_party/blink/renderer/core/intersection_observer/intersection_observer_controller.h"
#include "third_party/blink/renderer/core/layout/adjust_for_absolute_zoom.h"
#include "third_party/blink/renderer/core/layout/deferred_shaping.h"
#include "third_party/blink/renderer/core/layout/deferred_shaping_controller.h"
#include "third_party/blink/renderer/core/layout/geometry/transform_state.h"
#include "third_party/blink/renderer/core/layout/geometry/writing_mode_converter.h"
#include "third_party/blink/renderer/core/layout/layout_counter.h"
#include "third_party/blink/renderer/core/layout/layout_embedded_content.h"
#include "third_party/blink/renderer/core/layout/layout_embedded_object.h"
#include "third_party/blink/renderer/core/layout/layout_shift_tracker.h"
#include "third_party/blink/renderer/core/layout/layout_view.h"
#include "third_party/blink/renderer/core/layout/ng/legacy_layout_tree_walking.h"
#include "third_party/blink/renderer/core/layout/ng/ng_block_node.h"
#include "third_party/blink/renderer/core/layout/ng/ng_physical_box_fragment.h"
#include "third_party/blink/renderer/core/layout/style_retain_scope.h"
#include "third_party/blink/renderer/core/layout/svg/layout_svg_root.h"
#include "third_party/blink/renderer/core/layout/text_autosizer.h"
#include "third_party/blink/renderer/core/layout/traced_layout_object.h"
#include "third_party/blink/renderer/core/loader/document_loader.h"
#include "third_party/blink/renderer/core/loader/frame_loader.h"
#include "third_party/blink/renderer/core/media_type_names.h"
#include "third_party/blink/renderer/core/mobile_metrics/mobile_friendliness_checker.h"
#include "third_party/blink/renderer/core/page/autoscroll_controller.h"
#include "third_party/blink/renderer/core/page/chrome_client.h"
#include "third_party/blink/renderer/core/page/focus_controller.h"
#include "third_party/blink/renderer/core/page/frame_tree.h"
#include "third_party/blink/renderer/core/page/link_highlight.h"
#include "third_party/blink/renderer/core/page/page.h"
#include "third_party/blink/renderer/core/page/page_animator.h"
#include "third_party/blink/renderer/core/page/scrolling/fragment_anchor.h"
#include "third_party/blink/renderer/core/page/scrolling/scrolling_coordinator.h"
#include "third_party/blink/renderer/core/page/scrolling/snap_coordinator.h"
#include "third_party/blink/renderer/core/page/scrolling/top_document_root_scroller_controller.h"
#include "third_party/blink/renderer/core/page/spatial_navigation_controller.h"
#include "third_party/blink/renderer/core/page/validation_message_client.h"
#include "third_party/blink/renderer/core/paint/block_paint_invalidator.h"
#include "third_party/blink/renderer/core/paint/cull_rect_updater.h"
#include "third_party/blink/renderer/core/paint/frame_painter.h"
#include "third_party/blink/renderer/core/paint/old_cull_rect_updater.h"
#include "third_party/blink/renderer/core/paint/paint_layer.h"
#include "third_party/blink/renderer/core/paint/paint_layer_painter.h"
#include "third_party/blink/renderer/core/paint/paint_layer_scrollable_area.h"
#include "third_party/blink/renderer/core/paint/paint_timing.h"
#include "third_party/blink/renderer/core/paint/paint_timing_detector.h"
#include "third_party/blink/renderer/core/paint/pre_paint_tree_walk.h"
#include "third_party/blink/renderer/core/probe/core_probes.h"
#include "third_party/blink/renderer/core/resize_observer/resize_observer_controller.h"
#include "third_party/blink/renderer/core/scroll/scroll_alignment.h"
#include "third_party/blink/renderer/core/scroll/scroll_animator_base.h"
#include "third_party/blink/renderer/core/scroll/smooth_scroll_sequencer.h"
#include "third_party/blink/renderer/core/style/computed_style.h"
#include "third_party/blink/renderer/core/svg/svg_document_extensions.h"
#include "third_party/blink/renderer/core/svg/svg_svg_element.h"
#include "third_party/blink/renderer/platform/bindings/script_forbidden_scope.h"
#include "third_party/blink/renderer/platform/bindings/v8_per_isolate_data.h"
#include "third_party/blink/renderer/platform/fonts/font_cache.h"
#include "third_party/blink/renderer/platform/fonts/font_performance.h"
#include "third_party/blink/renderer/platform/geometry/layout_rect.h"
#include "third_party/blink/renderer/platform/graphics/compositing/paint_artifact_compositor.h"
#include "third_party/blink/renderer/platform/graphics/dark_mode_settings_builder.h"
#include "third_party/blink/renderer/platform/graphics/graphics_context.h"
#include "third_party/blink/renderer/platform/graphics/paint/cull_rect.h"
#include "third_party/blink/renderer/platform/graphics/paint/drawing_recorder.h"
#include "third_party/blink/renderer/platform/graphics/paint/paint_controller.h"
#include "third_party/blink/renderer/platform/instrumentation/histogram.h"
#include "third_party/blink/renderer/platform/instrumentation/resource_coordinator/document_resource_coordinator.h"
#include "third_party/blink/renderer/platform/instrumentation/tracing/trace_event.h"
#include "third_party/blink/renderer/platform/instrumentation/tracing/traced_value.h"
#include "third_party/blink/renderer/platform/language.h"
#include "third_party/blink/renderer/platform/loader/fetch/resource_fetcher.h"
#include "third_party/blink/renderer/platform/runtime_enabled_features.h"
#include "third_party/blink/renderer/platform/scheduler/public/frame_scheduler.h"
#include "third_party/blink/renderer/platform/web_test_support.h"
#include "third_party/blink/renderer/platform/widget/frame_widget.h"
#include "third_party/blink/renderer/platform/wtf/std_lib_extras.h"
#include "third_party/blink/renderer/platform/wtf/wtf_size_t.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/base/cursor/cursor.h"
#include "ui/base/cursor/mojom/cursor_type.mojom-blink.h"
#include "ui/gfx/geometry/point_conversions.h"
#include "ui/gfx/geometry/quad_f.h"
#include "ui/gfx/geometry/rect_f.h"

// Used to check for dirty layouts violating document lifecycle rules.
// If arg evaluates to true, the program will continue. If arg evaluates to
// false, program will crash if DCHECK_IS_ON() or return false from the current
// function.
#define CHECK_FOR_DIRTY_LAYOUT(arg) \
  do {                              \
    if (!(arg)) {                   \
      NOTREACHED();                 \
      return false;                 \
    }                               \
  } while (false)

namespace blink {
namespace {

// Logs a UseCounter for the size of the cursor that will be set. This will be
// used for compatibility analysis to determine whether the maximum size can be
// reduced.
void LogCursorSizeCounter(LocalFrame* frame, const ui::Cursor& cursor) {
  DCHECK(frame);
  SkBitmap bitmap = cursor.custom_bitmap();
  if (cursor.type() != ui::mojom::blink::CursorType::kCustom || bitmap.isNull())
    return;
  // Should not overflow, this calculation is done elsewhere when determining
  // whether the cursor exceeds its maximum size (see event_handler.cc).
  auto scaled_size =
      gfx::ScaleToFlooredSize(gfx::Size(bitmap.width(), bitmap.height()),
                              1 / cursor.image_scale_factor());
  if (scaled_size.width() > 64 || scaled_size.height() > 64) {
    UseCounter::Count(frame->GetDocument(), WebFeature::kCursorImageGT64x64);
  } else if (scaled_size.width() > 32 || scaled_size.height() > 32) {
    UseCounter::Count(frame->GetDocument(), WebFeature::kCursorImageGT32x32);
  } else {
    UseCounter::Count(frame->GetDocument(), WebFeature::kCursorImageLE32x32);
  }
}

gfx::QuadF GetQuadForTimelinePaintEvent(const scoped_refptr<cc::Layer>& layer) {
  gfx::RectF rect(layer->update_rect());
  if (layer->transform_tree_index() != -1)
    layer->ScreenSpaceTransform().TransformRect(&rect);
  return gfx::QuadF(rect);
}

// Default value for how long we want to delay the
// compositor commit beyond the start of document lifecycle updates to avoid
// flash between navigations. The delay should be small enough so that it won't
// confuse users expecting a new page to appear after navigation and the omnibar
// has updated the url display.
constexpr int kCommitDelayDefaultInMs = 500;  // 30 frames @ 60hz

}  // namespace

// The maximum number of updatePlugins iterations that should be done before
// returning.
static const unsigned kMaxUpdatePluginsIterations = 2;

LocalFrameView::LocalFrameView(LocalFrame& frame)
    : LocalFrameView(frame, gfx::Rect()) {
  Show();
}

LocalFrameView::LocalFrameView(LocalFrame& frame, const gfx::Size& initial_size)
    : LocalFrameView(frame, gfx::Rect(gfx::Point(), initial_size)) {
  SetLayoutSizeInternal(initial_size);
  Show();
}

LocalFrameView::LocalFrameView(LocalFrame& frame, gfx::Rect frame_rect)
    : FrameView(frame_rect),
      frame_(frame),
      can_have_scrollbars_(true),
      has_pending_layout_(false),
      layout_scheduling_enabled_(true),
      layout_count_for_testing_(0),
      lifecycle_update_count_for_testing_(0),
      // We want plugin updates to happen in FIFO order with loading tasks.
      update_plugins_timer_(frame.GetTaskRunner(TaskType::kInternalLoading),
                            this,
                            &LocalFrameView::UpdatePluginsTimerFired),
      first_layout_(true),
      base_background_color_(Color::kWhite),
      media_type_(media_type_names::kScreen),
      visually_non_empty_character_count_(0),
      visually_non_empty_pixel_count_(0),
      is_visually_non_empty_(false),
      layout_size_fixed_to_frame_size_(true),
      needs_update_geometries_(false),
      root_layer_did_scroll_(false),
      frame_timing_requests_dirty_(true),
      // The compositor throttles the main frame using deferred begin main frame
      // updates. We can't throttle it here or it seems the root compositor
      // doesn't get setup properly.
      lifecycle_updates_throttled_(!GetFrame().IsMainFrame()),
      target_state_(DocumentLifecycle::kUninitialized),
      suppress_adjust_view_size_(false),
      intersection_observation_state_(kNotNeeded),
      needs_focus_on_fragment_(false),
      main_thread_scrolling_reasons_(0),
      forced_layout_stack_depth_(0),
      forced_layout_start_time_(base::TimeTicks()),
      paint_frame_count_(0),
      unique_id_(NewUniqueObjectId()),
      layout_shift_tracker_(MakeGarbageCollected<LayoutShiftTracker>(this)),
      paint_timing_detector_(MakeGarbageCollected<PaintTimingDetector>(this)),
      mobile_friendliness_checker_(MobileFriendlinessChecker::Create(*this))
#if DCHECK_IS_ON()
      ,
      is_updating_descendant_dependent_flags_(false),
      is_updating_layout_(false)
#endif
{
  // Propagate the marginwidth/height and scrolling modes to the view.
  if (frame_->Owner() && frame_->Owner()->ScrollbarMode() ==
                             mojom::blink::ScrollbarMode::kAlwaysOff)
    SetCanHaveScrollbars(false);
}

LocalFrameView::~LocalFrameView() {
#if DCHECK_IS_ON()
  DCHECK(has_been_disposed_);
#endif
}

void LocalFrameView::Trace(Visitor* visitor) const {
  visitor->Trace(part_update_set_);
  visitor->Trace(frame_);
  visitor->Trace(update_plugins_timer_);
  visitor->Trace(layout_subtree_root_list_);
  visitor->Trace(orthogonal_writing_mode_root_list_);
  visitor->Trace(fragment_anchor_);
  visitor->Trace(scroll_anchoring_scrollable_areas_);
  visitor->Trace(animating_scrollable_areas_);
  visitor->Trace(user_scrollable_areas_);
  visitor->Trace(fixed_position_objects_);
  visitor->Trace(background_attachment_fixed_objects_);
  visitor->Trace(auto_size_info_);
  visitor->Trace(plugins_);
  visitor->Trace(scrollbars_);
  visitor->Trace(viewport_scrollable_area_);
  visitor->Trace(anchoring_adjustment_queue_);
  visitor->Trace(scroll_event_queue_);
  visitor->Trace(layout_shift_tracker_);
  visitor->Trace(paint_timing_detector_);
  visitor->Trace(mobile_friendliness_checker_);
  visitor->Trace(lifecycle_observers_);
  visitor->Trace(fullscreen_video_elements_);
  visitor->Trace(pending_transform_updates_);
}

void LocalFrameView::ForAllChildViewsAndPlugins(
    base::FunctionRef<void(EmbeddedContentView&)> function) {
  for (Frame* child = frame_->Tree().FirstChild(); child;
       child = child->Tree().NextSibling()) {
    if (child->View())
      function(*child->View());
  }

  for (const auto& plugin : plugins_) {
    function(*plugin);
  }

  if (Document* document = frame_->GetDocument()) {
    if (DocumentPortals* portals = DocumentPortals::Get(*document)) {
      for (PortalContents* portal : portals->GetPortals()) {
        if (Frame* frame = portal->GetFrame())
          function(*frame->View());
      }
    }
    if (DocumentFencedFrames* fenced_frames =
            DocumentFencedFrames::Get(*document)) {
      for (HTMLFencedFrameElement* fenced_frame :
           fenced_frames->GetFencedFrames()) {
        if (Frame* frame = fenced_frame->ContentFrame())
          function(*frame->View());
      }
    }
  }
}

void LocalFrameView::ForAllChildLocalFrameViews(
    base::FunctionRef<void(LocalFrameView&)> function) {
  for (Frame* child = frame_->Tree().FirstChild(); child;
       child = child->Tree().NextSibling()) {
    auto* child_local_frame = DynamicTo<LocalFrame>(child);
    if (!child_local_frame)
      continue;
    if (LocalFrameView* child_view = child_local_frame->View())
      function(*child_view);
  }
}

// Note: if this logic is updated, `ForAllThrottledLocalFrameViews()` may
// need to be updated as well.
void LocalFrameView::ForAllNonThrottledLocalFrameViews(
    base::FunctionRef<void(LocalFrameView&)> function,
    TraversalOrder order) {
  if (ShouldThrottleRendering())
    return;

  if (order == kPreOrder)
    function(*this);

  ForAllChildLocalFrameViews([&function, order](LocalFrameView& child_view) {
    child_view.ForAllNonThrottledLocalFrameViews(function, order);
  });

  if (order == kPostOrder)
    function(*this);
}

// Note: if this logic is updated, `ForAllNonThrottledLocalFrameViews()` may
// need to be updated as well.
void LocalFrameView::ForAllThrottledLocalFrameViews(
    base::FunctionRef<void(LocalFrameView&)> function) {
  if (ShouldThrottleRendering())
    function(*this);

  ForAllChildLocalFrameViews([&function](LocalFrameView& child_view) {
    child_view.ForAllThrottledLocalFrameViews(function);
  });
}

void LocalFrameView::ForAllRemoteFrameViews(
    base::FunctionRef<void(RemoteFrameView&)> function) {
  for (Frame* child = frame_->Tree().FirstChild(); child;
       child = child->Tree().NextSibling()) {
    if (child->IsLocalFrame()) {
      To<LocalFrame>(child)->View()->ForAllRemoteFrameViews(function);
    } else {
      DCHECK(child->IsRemoteFrame());
      if (RemoteFrameView* view = To<RemoteFrame>(child)->View())
        function(*view);
    }
  }
  if (Document* document = frame_->GetDocument()) {
    if (DocumentPortals* portals = DocumentPortals::Get(*document)) {
      for (PortalContents* portal : portals->GetPortals()) {
        if (RemoteFrame* frame = portal->GetFrame()) {
          if (RemoteFrameView* view = frame->View())
            function(*view);
        }
      }
    }
    if (DocumentFencedFrames* fenced_frames =
            DocumentFencedFrames::Get(*document)) {
      for (HTMLFencedFrameElement* fenced_frame :
           fenced_frames->GetFencedFrames()) {
        if (RemoteFrame* frame =
                To<RemoteFrame>(fenced_frame->ContentFrame())) {
          if (RemoteFrameView* view = frame->View())
            function(*view);
        }
      }
    }
  }
}

void LocalFrameView::Dispose() {
  CHECK(!IsInPerformLayout());

  // TODO(dcheng): It's wrong that the frame can be detached before the
  // LocalFrameView. Figure out what's going on and fix LocalFrameView to be
  // disposed with the correct timing.

  // We need to clear the RootFrameViewport's animator since it gets called
  // from non-GC'd objects and RootFrameViewport will still have a pointer to
  // this class.
  if (viewport_scrollable_area_) {
    DCHECK(frame_->IsMainFrame());
    DCHECK(frame_->GetPage());

    viewport_scrollable_area_->ClearScrollableArea();
    viewport_scrollable_area_.Clear();
    frame_->GetPage()->GlobalRootScrollerController().Reset();
  }

  // If we have scheduled plugins to be updated, cancel it. They will still be
  // notified before they are destroyed.
  if (update_plugins_timer_.IsActive())
    update_plugins_timer_.Stop();
  part_update_set_.clear();

  // These are LayoutObjects whose layout has been deferred to a subsequent
  // lifecycle update. Not gonna happen.
  layout_subtree_root_list_.Clear();

  // TODO(szager): LayoutObjects are supposed to remove themselves from these
  // tracking groups when they update style or are destroyed, but sometimes they
  // are missed. It would be good to understand how/why that happens, but in the
  // mean time, it's not safe to keep pointers around to defunct LayoutObjects.
  orthogonal_writing_mode_root_list_.Clear();
  fixed_position_objects_.Clear();
  background_attachment_fixed_objects_.clear();

  // Destroy |m_autoSizeInfo| as early as possible, to avoid dereferencing
  // partially destroyed |this| via |m_autoSizeInfo->m_frameView|.
  auto_size_info_.Clear();

  // FIXME: Do we need to do something here for OOPI?
  HTMLFrameOwnerElement* owner_element = frame_->DeprecatedLocalOwner();
  // TODO(dcheng): It seems buggy that we can have an owner element that points
  // to another EmbeddedContentView. This can happen when a plugin element loads
  // a frame (EmbeddedContentView A of type LocalFrameView) and then loads a
  // plugin (EmbeddedContentView B of type WebPluginContainerImpl). In this
  // case, the frame's view is A and the frame element's
  // OwnedEmbeddedContentView is B. See https://crbug.com/673170 for an example.
  if (owner_element && owner_element->OwnedEmbeddedContentView() == this)
    owner_element->SetEmbeddedContentView(nullptr);

  ukm_aggregator_.reset();
  layout_shift_tracker_->Dispose();

#if DCHECK_IS_ON()
  has_been_disposed_ = true;
#endif
}

void LocalFrameView::InvalidateAllCustomScrollbarsOnActiveChanged() {
  bool uses_window_inactive_selector =
      frame_->GetDocument()->GetStyleEngine().UsesWindowInactiveSelector();

  ForAllChildLocalFrameViews([](LocalFrameView& frame_view) {
    frame_view.InvalidateAllCustomScrollbarsOnActiveChanged();
  });

  for (const auto& scrollbar : scrollbars_) {
    if (uses_window_inactive_selector && scrollbar->IsCustomScrollbar())
      scrollbar->StyleChanged();
  }
}

void LocalFrameView::UsesOverlayScrollbarsChanged() {
  if (!user_scrollable_areas_)
    return;
  for (const auto& scrollable_area : *user_scrollable_areas_) {
    if (scrollable_area->ScrollsOverflow() || scrollable_area->HasScrollbar()) {
      scrollable_area->RemoveScrollbarsForReconstruction();
      if (auto* layout_box = scrollable_area->GetLayoutBox()) {
        layout_box->SetNeedsLayout(
            layout_invalidation_reason::kScrollbarChanged);
      }
    }
  }
}

bool LocalFrameView::DidFirstLayout() const {
  return !first_layout_;
}

bool LocalFrameView::LifecycleUpdatesActive() const {
  return !lifecycle_updates_throttled_;
}

void LocalFrameView::SetLifecycleUpdatesThrottledForTesting(bool throttled) {
  lifecycle_updates_throttled_ = throttled;
}

void LocalFrameView::FrameRectsChanged(const gfx::Rect& old_rect) {
  const bool width_changed = Size().width() != old_rect.width();
  const bool height_changed = Size().height() != old_rect.height();

  PropagateFrameRects();

  if (FrameRect() != old_rect) {
    if (auto* layout_view = GetLayoutView())
      layout_view->SetShouldCheckForPaintInvalidation();
  }

  if (width_changed || height_changed) {
    ViewportSizeChanged(width_changed, height_changed);
    if (frame_->IsMainFrame())
      frame_->GetPage()->GetVisualViewport().MainFrameDidChangeSize();
    GetFrame().Loader().RestoreScrollPositionAndViewState();
  }
}

Page* LocalFrameView::GetPage() const {
  return GetFrame().GetPage();
}

LayoutView* LocalFrameView::GetLayoutView() const {
  return GetFrame().ContentLayoutObject();
}

ScrollingCoordinator* LocalFrameView::GetScrollingCoordinator() const {
  Page* p = GetPage();
  return p ? p->GetScrollingCoordinator() : nullptr;
}

cc::AnimationHost* LocalFrameView::GetCompositorAnimationHost() const {
  if (!GetChromeClient())
    return nullptr;

  return GetChromeClient()->GetCompositorAnimationHost(*frame_);
}

cc::AnimationTimeline* LocalFrameView::GetScrollAnimationTimeline() const {
  if (!GetChromeClient())
    return nullptr;

  return GetChromeClient()->GetScrollAnimationTimeline(*frame_);
}

void LocalFrameView::SetLayoutOverflowSize(const gfx::Size& size) {
  if (size == layout_overflow_size_)
    return;

  layout_overflow_size_ = size;

  Page* page = GetFrame().GetPage();
  if (!page)
    return;
  page->GetChromeClient().ContentsSizeChanged(frame_.Get(), size);
}

void LocalFrameView::AdjustViewSize() {
  if (suppress_adjust_view_size_)
    return;

  LayoutView* layout_view = GetLayoutView();
  if (!layout_view)
    return;

  DCHECK_EQ(frame_->View(), this);
  SetLayoutOverflowSize(ToPixelSnappedRect(layout_view->DocumentRect()).size());
}

void LocalFrameView::CountObjectsNeedingLayout(unsigned& needs_layout_objects,
                                               unsigned& total_objects,
                                               bool& is_subtree) {
  needs_layout_objects = 0;
  total_objects = 0;
  is_subtree = IsSubtreeLayout();
  if (is_subtree) {
    layout_subtree_root_list_.CountObjectsNeedingLayout(needs_layout_objects,
                                                        total_objects);
  } else {
    LayoutSubtreeRootList::CountObjectsNeedingLayoutInRoot(
        GetLayoutView(), needs_layout_objects, total_objects);
  }
}

bool LocalFrameView::LayoutFromRootObject(LayoutObject& root) {
  if (!root.NeedsLayout())
    return false;

  if (auto* locked_ancestor =
          DisplayLockUtilities::LockedAncestorPreventingLayout(root)) {
    // Note that since we're preventing the layout on a layout root, we have to
    // mark its ancestor chain for layout. The reason for this is that we will
    // clear the layout roots whether or not we have finished laying them out,
    // so the fact that this root still needs layout will be lost if we don't
    // mark its container chain.
    //
    // Also, since we know that this root has a layout-blocking ancestor, the
    // layout bit propagation will stop there.
    //
    // TODO(vmpstr): Note that an alternative to this approach is to keep `root`
    // as a layout root in `layout_subtree_root_list_`. It would mean that we
    // will keep it in the list while the display-lock prevents layout. We need
    // to investigate which of these approaches is better.
    root.MarkContainerChainForLayout();
    return false;
  }

  LayoutState layout_state(root);
  if (scroll_anchoring_scrollable_areas_) {
    for (auto& scrollable_area : *scroll_anchoring_scrollable_areas_) {
      if (scrollable_area->GetScrollAnchor() &&
          scrollable_area->ShouldPerformScrollAnchoring())
        scrollable_area->GetScrollAnchor()->NotifyBeforeLayout();
    }
  }

  To<LayoutBox>(root).LayoutSubtreeRoot();
  return true;
}

#define PERFORM_LAYOUT_TRACE_CATEGORIES \
  "blink,benchmark,rail," TRACE_DISABLED_BY_DEFAULT("blink.debug.layout")

void LocalFrameView::PerformLayout() {
  ScriptForbiddenScope forbid_script;

  has_pending_layout_ = false;

  // TODO(crbug.com/460956): The notion of a single root for layout is no
  // longer applicable. Remove or update this code.
  LayoutObject* root_for_this_layout = GetLayoutView();

  FontCachePurgePreventer font_cache_purge_preventer;
  StyleRetainScope style_retain_scope;
  bool in_subtree_layout = false;
  base::AutoReset<bool> change_scheduling_enabled(&layout_scheduling_enabled_,
                                                  false);
  // If the layout view was marked as needing layout after we added items in
  // the subtree roots we need to clear the roots and do the layout from the
  // layoutView.
  if (GetLayoutView()->NeedsLayout())
    ClearLayoutSubtreeRootsAndMarkContainingBlocks();
  GetLayoutView()->ClearHitTestCache();

  in_subtree_layout = IsSubtreeLayout();

  // TODO(crbug.com/460956): The notion of a single root for layout is no
  // longer applicable. Remove or update this code.
  if (in_subtree_layout)
    root_for_this_layout = layout_subtree_root_list_.RandomRoot();

  if (!root_for_this_layout) {
    // FIXME: Do we need to set m_size here?
    NOTREACHED();
    return;
  }

  Document* document = GetFrame().GetDocument();
  if (!in_subtree_layout) {
    ClearLayoutSubtreeRootsAndMarkContainingBlocks();
    Node* body = document->body();
    if (body && body->GetLayoutObject()) {
      if (IsA<HTMLFrameSetElement>(*body)) {
        body->GetLayoutObject()->SetChildNeedsLayout();
      } else if (IsA<HTMLBodyElement>(*body)) {
        if (!first_layout_ && size_.Height() != GetLayoutSize().height() &&
            body->GetLayoutObject()->EnclosingBox()->StretchesToViewport())
          body->GetLayoutObject()->SetChildNeedsLayout();
      }
    }

    if (first_layout_) {
      first_layout_ = false;

      mojom::blink::ScrollbarMode h_mode;
      mojom::blink::ScrollbarMode v_mode;
      GetLayoutView()->CalculateScrollbarModes(h_mode, v_mode);
      if (v_mode == mojom::blink::ScrollbarMode::kAuto) {
        if (auto* scrollable_area = GetLayoutView()->GetScrollableArea())
          scrollable_area->ForceVerticalScrollbarForFirstLayout();
      }
    }

    LayoutSize old_size = size_;

    size_ = LayoutSize(GetLayoutSize());

    if (old_size != size_ && !first_layout_) {
      LayoutBox* root_layout_object =
          document->documentElement()
              ? document->documentElement()->GetLayoutBox()
              : nullptr;
      LayoutBox* body_layout_object = root_layout_object && document->body()
                                          ? document->body()->GetLayoutBox()
                                          : nullptr;
      if (body_layout_object && body_layout_object->StretchesToViewport())
        body_layout_object->SetChildNeedsLayout();
      else if (root_layout_object && root_layout_object->StretchesToViewport())
        root_layout_object->SetChildNeedsLayout();
    }
  }

  TRACE_EVENT_OBJECT_SNAPSHOT_WITH_ID(
      TRACE_DISABLED_BY_DEFAULT("blink.debug.layout.trees"), "LayoutTree", this,
      TracedLayoutObject::Create(*GetLayoutView(), false));

  gfx::Size old_size(Size());

  DCHECK(in_subtree_layout || layout_subtree_root_list_.IsEmpty());

  double contents_height_before_layout =
      GetLayoutView()->DocumentRect().Height();
  TRACE_EVENT_BEGIN1(
      PERFORM_LAYOUT_TRACE_CATEGORIES, "LocalFrameView::performLayout",
      "contentsHeightBeforeLayout", contents_height_before_layout);

  if (in_subtree_layout && HasOrthogonalWritingModeRoots()) {
    // If we're going to lay out from each subtree root, rather than once from
    // LayoutView, we need to merge the depth-ordered orthogonal writing mode
    // root list into the depth-ordered list of subtrees scheduled for
    // layout. Otherwise, during layout of one such subtree, we'd risk skipping
    // over a subtree of objects needing layout.
    DCHECK(!layout_subtree_root_list_.IsEmpty());
    ScheduleOrthogonalWritingModeRootsForLayout();
  }

  DCHECK(!IsInPerformLayout());
  Lifecycle().AdvanceTo(DocumentLifecycle::kInPerformLayout);

  // performLayout is the actual guts of layout().
  // FIXME: The 300 other lines in layout() probably belong in other helper
  // functions so that a single human could understand what layout() is actually
  // doing.

  {
    // TODO(szager): Remove this after diagnosing crash.
    DocumentLifecycle::CheckNoTransitionScope check_no_transition(Lifecycle());
    if (in_subtree_layout) {
      // This map will be used to avoid rebuilding several times the fragment
      // tree spine of a common ancestor.
      HeapHashMap<Member<const LayoutBlock>, unsigned> fragment_tree_spines;
      for (auto& root : layout_subtree_root_list_.Unordered()) {
        const LayoutBlock* cb = root->ContainingNGBlock();
        if (cb && cb->PhysicalFragmentCount()) {
          auto add_result = fragment_tree_spines.insert(cb, 0);
          ++add_result.stored_value->value;
        }
      }
      for (auto& root : layout_subtree_root_list_.Ordered()) {
        bool should_rebuild_fragments = false;
        LayoutObject& root_layout_object = *root;
        LayoutBlock* cb = root->ContainingNGBlock();
        if (cb) {
          auto it = fragment_tree_spines.find(cb);
          DCHECK(it == fragment_tree_spines.end() || it->value > 0);
          // Ensure fragment-tree consistency just after all the cb's
          // descendants have completed their subtree layout.
          should_rebuild_fragments =
              it != fragment_tree_spines.end() && --it->value == 0;
        }

        if (!LayoutFromRootObject(*root))
          continue;

        if (should_rebuild_fragments)
          cb->RebuildFragmentTreeSpine();

        // We need to ensure that we mark up all layoutObjects up to the
        // LayoutView for paint invalidation. This simplifies our code as we
        // just always do a full tree walk.
        if (LayoutObject* container = root_layout_object.Container())
          container->SetShouldCheckForPaintInvalidation();
      }
      layout_subtree_root_list_.Clear();
#if DCHECK_IS_ON()
      // Ensure fragment-tree consistency after a subtree layout.
      for (const auto& p : fragment_tree_spines) {
        p.key->AssertFragmentTree();
        DCHECK_EQ(p.value, 0u);
      }
#endif
      fragment_tree_spines.clear();
    } else {
      if (HasOrthogonalWritingModeRoots())
        LayoutOrthogonalWritingModeRoots();

      DeferredShapingController& ds_controller =
          GetLayoutView()->GetDeferredShapingController();
      bool default_allow_deferred_shaping =
          ds_controller.DefaultAllowDeferredShaping() &&
          RuntimeEnabledFeatures::DeferredShapingEnabled() &&
          !frame_->PagePopupOwner() && !auto_size_info_ &&
          !GetScrollableArea()->HasPendingHistoryRestoreScrollOffset();
      if (!default_allow_deferred_shaping)
        ds_controller.DisallowDeferredShaping();
      using PassKey = base::PassKey<LocalFrameView>;
      ds_controller.SetAllowDeferredShaping(
          PassKey(), default_allow_deferred_shaping && !document->Printing());
      DeferredShapingViewportScope viewport_scope(*GetLayoutView());
      GetLayoutView()->UpdateLayout();
      ds_controller.SetAllowDeferredShaping(PassKey(), false);
    }
  }

  document->Fetcher()->UpdateAllImageResourcePriorities();

  Lifecycle().AdvanceTo(DocumentLifecycle::kAfterPerformLayout);

  TRACE_EVENT_END0(PERFORM_LAYOUT_TRACE_CATEGORIES,
                   "LocalFrameView::performLayout");
  FirstMeaningfulPaintDetector::From(*document)
      .MarkNextPaintAsMeaningfulIfNeeded(
          layout_object_counter_, contents_height_before_layout,
          GetLayoutView()->DocumentRect().Height(), Height());

  gfx::Size new_size(Size());
  if (old_size != new_size) {
    MarkFixedPositionObjectsForLayout(old_size.width() != new_size.width(),
                                      old_size.height() != new_size.height());
  }

  if (frame_->IsMainFrame()) {
    if (auto* text_autosizer = document->GetTextAutosizer()) {
      if (text_autosizer->HasLayoutInlineSizeChanged())
        text_autosizer->UpdatePageInfoInAllFrames(frame_);
    }
  }
#if EXPENSIVE_DCHECKS_ARE_ON()
  DCHECK(!Lifecycle().LifecyclePostponed() && !ShouldThrottleRendering());
  document->AssertLayoutTreeUpdatedAfterLayout();
#endif
}

void LocalFrameView::UpdateLayout() {
  // We should never layout a Document which is not in a LocalFrame.
  DCHECK(frame_);
  DCHECK_EQ(frame_->View(), this);
  DCHECK(frame_->GetPage());

  Lifecycle().EnsureStateAtMost(DocumentLifecycle::kStyleClean);

  absl::optional<RuntimeCallTimerScope> rcs_scope;
  probe::UpdateLayout probe(GetFrame().GetDocument());
  HeapVector<LayoutObjectWithDepth> layout_roots;

  ENTER_EMBEDDER_STATE(V8PerIsolateData::MainThreadIsolate(), &GetFrame(),
                       BlinkState::LAYOUT);
  TRACE_EVENT_BEGIN0("blink,benchmark", "LocalFrameView::layout");
  if (UNLIKELY(RuntimeEnabledFeatures::BlinkRuntimeCallStatsEnabled())) {
    rcs_scope.emplace(
        RuntimeCallStats::From(V8PerIsolateData::MainThreadIsolate()),
        RuntimeCallStats::CounterId::kUpdateLayout);
  }
  layout_roots = layout_subtree_root_list_.Ordered();
  if (layout_roots.IsEmpty())
    layout_roots.push_back(LayoutObjectWithDepth(GetLayoutView()));
  TRACE_EVENT_BEGIN1("devtools.timeline", "Layout", "beginData",
                     [&](perfetto::TracedValue context) {
                       inspector_layout_event::BeginData(std::move(context),
                                                         this);
                     });

  PerformLayout();
  Lifecycle().AdvanceTo(DocumentLifecycle::kLayoutClean);

  TRACE_EVENT_END0("blink,benchmark", "LocalFrameView::layout");

  TRACE_EVENT_END1("devtools.timeline", "Layout", "endData",
                   [&](perfetto::TracedValue context) {
                     inspector_layout_event::EndData(std::move(context),
                                                     layout_roots);
                   });
  probe::DidChangeViewport(frame_.Get());
}

void LocalFrameView::WillStartForcedLayout() {
  // UpdateLayout is re-entrant for auto-sizing and plugins. So keep
  // track of stack depth to include all the time in the top-level call.
  forced_layout_stack_depth_++;
  if (forced_layout_stack_depth_ > 1)
    return;
  forced_layout_start_time_ = base::TimeTicks::Now();
}

void LocalFrameView::DidFinishForcedLayout(DocumentUpdateReason reason) {
  CHECK_GT(forced_layout_stack_depth_, (unsigned)0);
  forced_layout_stack_depth_--;
  if (!forced_layout_stack_depth_ && base::TimeTicks::IsHighResolution()) {
    LocalFrameUkmAggregator& aggregator = EnsureUkmAggregator();
    aggregator.RecordForcedLayoutSample(reason, forced_layout_start_time_,
                                        base::TimeTicks::Now());
  }
}

void LocalFrameView::MarkFirstEligibleToPaint() {
  if (frame_ && frame_->GetDocument()) {
    PaintTiming& timing = PaintTiming::From(*frame_->GetDocument());
    timing.MarkFirstEligibleToPaint();
  }
}

void LocalFrameView::MarkIneligibleToPaint() {
  if (frame_ && frame_->GetDocument()) {
    PaintTiming& timing = PaintTiming::From(*frame_->GetDocument());
    timing.MarkIneligibleToPaint();
  }
}

void LocalFrameView::SetNeedsPaintPropertyUpdate() {
  if (auto* layout_view = GetLayoutView())
    layout_view->SetNeedsPaintPropertyUpdate();
}

gfx::SizeF LocalFrameView::ViewportSizeForViewportUnits() const {
  return LargeViewportSizeForViewportUnits();
}

gfx::SizeF LocalFrameView::SmallViewportSizeForViewportUnits() const {
  float zoom = 1;
  if (!frame_->GetDocument() || !frame_->GetDocument()->Printing())
    zoom = GetFrame().PageZoomFactor();

  auto* layout_view = GetLayoutView();
  if (!layout_view)
    return gfx::SizeF();

  gfx::SizeF layout_size;
  layout_size.set_width(layout_view->ViewWidth(kIncludeScrollbars) / zoom);
  layout_size.set_height(layout_view->ViewHeight(kIncludeScrollbars) / zoom);

  return layout_size;
}

gfx::SizeF LocalFrameView::LargeViewportSizeForViewportUnits() const {
  auto* layout_view = GetLayoutView();
  if (!layout_view)
    return gfx::SizeF();

  gfx::SizeF layout_size = SmallViewportSizeForViewportUnits();

  BrowserControls& browser_controls = frame_->GetPage()->GetBrowserControls();
  if (browser_controls.PermittedState() != cc::BrowserControlsState::kHidden) {
    // We use the layoutSize rather than frameRect to calculate viewport units
    // so that we get correct results on mobile where the page is laid out into
    // a rect that may be larger than the viewport (e.g. the 980px fallback
    // width for desktop pages). Since the layout height is statically set to
    // be the viewport with browser controls showing, we add the browser
    // controls height, compensating for page scale as well, since we want to
    // use the viewport with browser controls hidden for vh (to match Safari).
    int viewport_width = frame_->GetPage()->GetVisualViewport().Size().width();
    // TODO(bokan): IsOutermostMainFrame may need to be reevaluated for
    // portals.
    if (frame_->IsOutermostMainFrame() && layout_size.width() &&
        viewport_width) {
      float page_scale_at_layout_width = viewport_width / layout_size.width();
      layout_size.Enlarge(0, (browser_controls.TotalHeight() -
                              browser_controls.TotalMinHeight()) /
                                 page_scale_at_layout_width);
    }
  }

  return layout_size;
}

gfx::SizeF LocalFrameView::ViewportSizeForMediaQueries() const {
  gfx::SizeF viewport_size(layout_size_);
  if (!frame_->GetDocument() || !frame_->GetDocument()->Printing())
    viewport_size.Scale(1 / GetFrame().PageZoomFactor());
  return viewport_size;
}

gfx::SizeF LocalFrameView::DynamicViewportSizeForViewportUnits() const {
  BrowserControls& browser_controls = frame_->GetPage()->GetBrowserControls();
  return browser_controls.ShrinkViewport()
             ? SmallViewportSizeForViewportUnits()
             : LargeViewportSizeForViewportUnits();
}

DocumentLifecycle& LocalFrameView::Lifecycle() const {
  DCHECK(frame_);
  DCHECK(frame_->GetDocument());
  return frame_->GetDocument()->Lifecycle();
}

bool LocalFrameView::InPostLifecycleSteps() const {
  return GetFrame().LocalFrameRoot().View()->in_post_lifecycle_steps_;
}

void LocalFrameView::RunPostLifecycleSteps() {
  base::AutoReset<bool> in_post_lifecycle_steps(&in_post_lifecycle_steps_,
                                                true);
  AllowThrottlingScope allow_throttling(*this);
  RunIntersectionObserverSteps();
  if (mobile_friendliness_checker_)
    mobile_friendliness_checker_->MaybeRecompute();

  ForAllRemoteFrameViews([](RemoteFrameView& frame_view) {
    frame_view.UpdateCompositingScaleFactor();
  });
}

void LocalFrameView::RunIntersectionObserverSteps() {
#if DCHECK_IS_ON()
  bool was_dirty = NeedsLayout();
#endif
  if ((intersection_observation_state_ < kRequired &&
       ShouldThrottleRendering()) ||
      Lifecycle().LifecyclePostponed() || !frame_->GetDocument()->IsActive()) {
    return;
  }

  if (frame_->IsOutermostMainFrame()) {
    EnsureOverlayInterstitialAdDetector().MaybeFireDetection(frame_.Get());
    EnsureStickyAdDetector().MaybeFireDetection(frame_.Get());

    // Report the main frame's document intersection with itself.
    LayoutObject* layout_object = GetLayoutView();
    gfx::Rect main_frame_dimensions(ToRoundedSize(
        To<LayoutBox>(layout_object)->PhysicalLayoutOverflowRect().size));
    GetFrame().Client()->OnMainFrameIntersectionChanged(main_frame_dimensions);
    GetFrame().Client()->OnMainFrameViewportRectangleChanged(
        gfx::Rect(frame_->GetOutermostMainFrameScrollPosition(),
                  frame_->GetOutermostMainFrameSize()));
  }

  TRACE_EVENT0("blink,benchmark",
               "LocalFrameView::UpdateViewportIntersectionsForSubtree");
  SCOPED_UMA_AND_UKM_TIMER(EnsureUkmAggregator(),
                           LocalFrameUkmAggregator::kIntersectionObservation);

  // Populating monotonic_time may be expensive, and may be unnecessary, so
  // allow it to be populated on demand.
  absl::optional<base::TimeTicks> monotonic_time;
  bool needs_occlusion_tracking =
      UpdateViewportIntersectionsForSubtree(0, monotonic_time);
  if (FrameOwner* owner = frame_->Owner())
    owner->SetNeedsOcclusionTracking(needs_occlusion_tracking);
#if DCHECK_IS_ON()
  DCHECK(was_dirty || !NeedsLayout());
#endif
  DeliverSynchronousIntersectionObservations();
}

void LocalFrameView::ForceUpdateViewportIntersections() {
  // IntersectionObserver targets in this frame (and its frame tree) need to
  // update; but we can't wait for a lifecycle update to run them, because a
  // hidden frame won't run lifecycle updates. Force layout and run them now.
  DisallowThrottlingScope disallow_throttling(*this);
  UpdateLifecycleToPrePaintClean(
      DocumentUpdateReason::kIntersectionObservation);
  absl::optional<base::TimeTicks> monotonic_time;
  UpdateViewportIntersectionsForSubtree(
      IntersectionObservation::kImplicitRootObserversNeedUpdate |
          IntersectionObservation::kIgnoreDelay,
      monotonic_time);
}

LayoutSVGRoot* LocalFrameView::EmbeddedReplacedContent() const {
  auto* layout_view = GetLayoutView();
  if (!layout_view)
    return nullptr;

  LayoutObject* first_child = layout_view->FirstChild();
  if (!first_child || !first_child->IsBox())
    return nullptr;

  // Currently only embedded SVG documents participate in the size-negotiation
  // logic.
  return DynamicTo<LayoutSVGRoot>(first_child);
}

bool LocalFrameView::GetIntrinsicSizingInfo(
    IntrinsicSizingInfo& intrinsic_sizing_info) const {
  if (LayoutSVGRoot* content_layout_object = EmbeddedReplacedContent()) {
    content_layout_object->UnscaledIntrinsicSizingInfo(intrinsic_sizing_info);
    return true;
  }
  return false;
}

bool LocalFrameView::HasIntrinsicSizingInfo() const {
  return EmbeddedReplacedContent();
}

void LocalFrameView::UpdateGeometry() {
  LayoutEmbeddedContent* layout = GetLayoutEmbeddedContent();
  if (!layout)
    return;

  PhysicalRect new_frame = layout->ReplacedContentRect();
#if DCHECK_IS_ON()
  if (new_frame.Width() != LayoutUnit::Max().RawValue() &&
      new_frame.Height() != LayoutUnit::Max().RawValue())
    DCHECK(!new_frame.size.HasFraction());
#endif
  bool bounds_will_change = PhysicalSize(Size()) != new_frame.size;

  // If frame bounds are changing mark the view for layout. Also check the
  // frame's page to make sure that the frame isn't in the process of being
  // destroyed. If iframe scrollbars needs reconstruction from native to custom
  // scrollbar, then also we need to layout the frameview.
  if (bounds_will_change)
    SetNeedsLayout();

  layout->UpdateGeometry(*this);
}

void LocalFrameView::AddPartToUpdate(LayoutEmbeddedObject& object) {
  // This is typically called during layout to ensure we update plugins.
  // However, if layout is blocked (e.g. by content-visibility), we can add the
  // part to update during layout tree attachment (which is a part of style
  // recalc).
  DCHECK(IsInPerformLayout() ||
         (DisplayLockUtilities::LockedAncestorPreventingLayout(object) &&
          frame_->GetDocument()->InStyleRecalc()));

  // Tell the DOM element that it needs a Plugin update.
  Node* node = object.GetNode();
  DCHECK(node);
  if (IsA<HTMLObjectElement>(*node) || IsA<HTMLEmbedElement>(*node))
    To<HTMLPlugInElement>(node)->SetNeedsPluginUpdate(true);

  part_update_set_.insert(&object);
}

void LocalFrameView::SetMediaType(const AtomicString& media_type) {
  DCHECK(frame_->GetDocument());
  media_type_ = media_type;
  frame_->GetDocument()->MediaQueryAffectingValueChanged(
      MediaValueChange::kOther);
}

AtomicString LocalFrameView::MediaType() const {
  // See if we have an override type.
  if (frame_->GetSettings() &&
      !frame_->GetSettings()->GetMediaTypeOverride().IsEmpty())
    return AtomicString(frame_->GetSettings()->GetMediaTypeOverride());
  return media_type_;
}

void LocalFrameView::AdjustMediaTypeForPrinting(bool printing) {
  if (printing) {
    if (media_type_when_not_printing_.IsNull())
      media_type_when_not_printing_ = MediaType();
    SetMediaType(media_type_names::kPrint);
  } else {
    if (!media_type_when_not_printing_.IsNull())
      SetMediaType(media_type_when_not_printing_);
    media_type_when_not_printing_ = g_null_atom;
  }
}

void LocalFrameView::AddBackgroundAttachmentFixedObject(LayoutObject* object) {
  DCHECK(!background_attachment_fixed_objects_.Contains(object));
  background_attachment_fixed_objects_.insert(object);

  // Ensure main thread scrolling reasons of the ancestor scroll nodes are
  // recomputed. The object's own scroll properties are not affected.
  object->ForceAllAncestorsNeedPaintPropertyUpdate();
}

void LocalFrameView::RemoveBackgroundAttachmentFixedObject(
    LayoutObject* object) {
  background_attachment_fixed_objects_.erase(object);

  // Ensure main thread scrolling reasons of the ancestor scroll nodes are
  // recomputed. The object's own scroll properties are not affected.
  object->ForceAllAncestorsNeedPaintPropertyUpdate();
}

bool LocalFrameView::RequiresMainThreadScrollingForBackgroundAttachmentFixed()
    const {
  if (background_attachment_fixed_objects_.IsEmpty())
    return false;
  if (background_attachment_fixed_objects_.size() > 1)
    return true;

  const auto* object = To<LayoutBoxModelObject>(
      background_attachment_fixed_objects_.begin()->Get());
  // We should not add such object in the set.
  DCHECK(!object->BackgroundTransfersToView());
  // If the background is viewport background and it paints onto the border box
  // space only, then it doesn't need main thread scrolling.
  if (IsA<LayoutView>(object) &&
      object->GetBackgroundPaintLocation() == kBackgroundPaintInBorderBoxSpace)
    return false;
  return true;
}

void LocalFrameView::AddFixedPositionObject(LayoutObject& object) {
  if (!fixed_position_objects_)
    fixed_position_objects_ = MakeGarbageCollected<ObjectSet>();
  fixed_position_objects_->insert(&object);
}

void LocalFrameView::RemoveFixedPositionObject(LayoutObject& object) {
  if (fixed_position_objects_)
    fixed_position_objects_->erase(&object);
}

void LocalFrameView::ViewportSizeChanged(bool width_changed,
                                         bool height_changed) {
  DCHECK(width_changed || height_changed);
  DCHECK(frame_->GetPage());
  if (frame_->GetDocument() &&
      frame_->GetDocument()->Lifecycle().LifecyclePostponed())
    return;

  if (frame_->IsOutermostMainFrame())
    layout_shift_tracker_->NotifyViewportSizeChanged();

  auto* layout_view = GetLayoutView();
  if (layout_view) {
    // If this is the outermost main frame, we might have got here by
    // hiding/showing the top controls. In that case, layout won't be
    // triggered, so we need to clamp the scroll offset here.
    // TODO(bokan): IsOutermostMainFrame may need to be reevaluated for
    // portals.
    if (GetFrame().IsOutermostMainFrame()) {
      layout_view->Layer()->UpdateSize();
      if (auto* scrollable_area = layout_view->GetScrollableArea())
        scrollable_area->ClampScrollOffsetAfterOverflowChange();
    }

    layout_view->Layer()->SetNeedsCompositingInputsUpdate();
  }

  if (GetFrame().GetDocument())
    GetFrame().GetDocument()->GetRootScrollerController().DidResizeFrameView();

  // Change of viewport size after browser controls showing/hiding may affect
  // painting of the background.
  if (layout_view && frame_->IsMainFrame() &&
      frame_->GetPage()->GetBrowserControls().TotalHeight())
    layout_view->SetShouldCheckForPaintInvalidation();

  if (GetFrame().GetDocument() && !IsInPerformLayout())
    MarkFixedPositionObjectsForLayout(width_changed, height_changed);

  if (GetPaintTimingDetector().Visualizer())
    GetPaintTimingDetector().Visualizer()->OnViewportChanged();
}

void LocalFrameView::MarkFixedPositionObjectsForLayout(bool width_changed,
                                                       bool height_changed) {
  if (!HasFixedPositionObjects() || !(width_changed || height_changed))
    return;

  for (const auto& layout_object : *fixed_position_objects_) {
    const ComputedStyle& style = layout_object->StyleRef();
    if (width_changed) {
      if (style.Width().IsFixed() &&
          (style.Left().IsAuto() || style.Right().IsAuto())) {
        layout_object->SetNeedsPositionedMovementLayout();
      } else {
        layout_object->SetNeedsLayoutAndFullPaintInvalidation(
            layout_invalidation_reason::kSizeChanged);
      }
    }
    if (height_changed) {
      if (style.Height().IsFixed() &&
          (style.Top().IsAuto() || style.Bottom().IsAuto())) {
        layout_object->SetNeedsPositionedMovementLayout();
      } else {
        layout_object->SetNeedsLayoutAndFullPaintInvalidation(
            layout_invalidation_reason::kSizeChanged);
      }
    }
  }
}

void LocalFrameView::DynamicViewportUnitsChanged() {
  if (GetFrame().GetDocument())
    GetFrame().GetDocument()->DynamicViewportUnitsChanged();
}

bool LocalFrameView::ShouldSetCursor() const {
  Page* page = GetFrame().GetPage();
  return page && page->IsPageVisible() &&
         !frame_->GetEventHandler().IsMousePositionUnknown() &&
         page->GetFocusController().IsActive();
}

void LocalFrameView::InvalidateBackgroundAttachmentFixedDescendantsOnScroll(
    const LayoutObject& scrolled_object) {
  for (const auto& layout_object : background_attachment_fixed_objects_) {
    if (scrolled_object != GetLayoutView() &&
        !layout_object->IsDescendantOf(&scrolled_object))
      continue;
    // An object needs to repaint the background on scroll when it has
    // background-attachment:fixed unless the object is the LayoutView and the
    // background is not painted on the scrolling contents.
    if (layout_object == GetLayoutView() &&
        !(GetLayoutView()->GetBackgroundPaintLocation() &
          kBackgroundPaintInContentsSpace))
      continue;
    layout_object->SetBackgroundNeedsFullPaintInvalidation();
  }
}

HitTestResult LocalFrameView::HitTestWithThrottlingAllowed(
    const HitTestLocation& location,
    HitTestRequest::HitTestRequestType request_type) const {
  AllowThrottlingScope allow_throttling(*this);
  return GetFrame().GetEventHandler().HitTestResultAtLocation(location,
                                                              request_type);
}

void LocalFrameView::ProcessUrlFragment(const KURL& url,
                                        bool same_document_navigation,
                                        bool should_scroll) {
  // We want to create the anchor even if we don't need to scroll. This ensures
  // all the side effects like setting CSS :target are correctly set.
  FragmentAnchor* anchor =
      FragmentAnchor::TryCreate(url, *frame_, should_scroll);

  if (anchor) {
    fragment_anchor_ = anchor;
    fragment_anchor_->Installed();
    // Post-load, same-document navigations need to schedule a frame in which
    // the fragment anchor will be invoked. It will be done after layout as
    // part of the lifecycle.
    if (same_document_navigation)
      ScheduleAnimation();
    if (const auto* layout_view = GetLayoutView()) {
      layout_view->GetDeferredShapingController().ReshapeAllDeferred(
          ReshapeReason::kFragmentAnchor);
    }
  }
}

void LocalFrameView::SetLayoutSize(const gfx::Size& size) {
  DCHECK(!LayoutSizeFixedToFrameSize());
  if (frame_->GetDocument() &&
      frame_->GetDocument()->Lifecycle().LifecyclePostponed())
    return;

  SetLayoutSizeInternal(size);
}

void LocalFrameView::SetLayoutSizeFixedToFrameSize(bool is_fixed) {
  if (layout_size_fixed_to_frame_size_ == is_fixed)
    return;

  layout_size_fixed_to_frame_size_ = is_fixed;
  if (is_fixed)
    SetLayoutSizeInternal(Size());
}

ChromeClient* LocalFrameView::GetChromeClient() const {
  Page* page = GetFrame().GetPage();
  if (!page)
    return nullptr;
  return &page->GetChromeClient();
}

void LocalFrameView::HandleLoadCompleted() {
  // Once loading has completed, allow autoSize one last opportunity to
  // reduce the size of the frame.
  if (auto_size_info_)
    UpdateStyleAndLayout();
}

void LocalFrameView::ClearLayoutSubtreeRoot(const LayoutObject& root) {
  layout_subtree_root_list_.Remove(const_cast<LayoutObject&>(root));
}

void LocalFrameView::ClearLayoutSubtreeRootsAndMarkContainingBlocks() {
  layout_subtree_root_list_.ClearAndMarkContainingBlocksForLayout();
}

void LocalFrameView::AddOrthogonalWritingModeRoot(LayoutBox& root) {
  DCHECK(!root.IsLayoutCustomScrollbarPart());
  orthogonal_writing_mode_root_list_.Add(root);
}

void LocalFrameView::RemoveOrthogonalWritingModeRoot(LayoutBox& root) {
  orthogonal_writing_mode_root_list_.Remove(root);
}

bool LocalFrameView::HasOrthogonalWritingModeRoots() const {
  return !orthogonal_writing_mode_root_list_.IsEmpty();
}

static inline void RemoveFloatingObjectsForSubtreeRoot(LayoutObject& root) {
  // TODO(kojii): Under certain conditions, moveChildTo() defers
  // removeFloatingObjects() until the containing block layouts. For
  // instance, when descendants of the moving child is floating,
  // removeChildNode() does not clear them. In such cases, at this
  // point, FloatingObjects may contain old or even deleted objects.
  // Dealing this in markAllDescendantsWithFloatsForLayout() could
  // solve, but since that is likely to suffer the performance and
  // since the containing block of orthogonal writing mode roots
  // having floats is very rare, prefer to re-create
  // FloatingObjects.
  if (LayoutBlock* cb = root.ContainingBlock()) {
    auto* child_block_flow = DynamicTo<LayoutBlockFlow>(cb);
    if ((cb->NormalChildNeedsLayout() || cb->SelfNeedsLayout()) &&
        child_block_flow) {
      child_block_flow->RemoveFloatingObjectsFromDescendants();
    }
  }
}

static bool PrepareOrthogonalWritingModeRootForLayout(LayoutObject& root) {
  DCHECK(To<LayoutBox>(root).IsOrthogonalWritingModeRoot());
  if (!root.NeedsLayout() || root.IsOutOfFlowPositioned() ||
      root.IsColumnSpanAll() || root.StyleRef().LogicalHeight().IsSpecified() ||
      To<LayoutBox>(root).IsGridItem() || root.IsTablePart() ||
      root.IsLayoutFlowThread())
    return false;

  if (RuntimeEnabledFeatures::LayoutNGEnabled()) {
    // Do not pre-layout objects that are fully managed by LayoutNG; it is not
    // necessary and may lead to double layouts. We do need to pre-layout
    // objects whose containing block is a legacy object so that it can
    // properly compute its intrinsic size.
    if (IsManagedByLayoutNG(root))
      return false;

    // If the root is legacy but has |CachedLayoutResult|, its parent is NG,
    // which called |RunLegacyLayout()|. This parent not only needs to run
    // pre-layout, but also clearing |NeedsLayout()| without updating
    // |CachedLayoutResult| is harmful.
    if (const auto* box = DynamicTo<LayoutBox>(root)) {
      if (box->GetCachedLayoutResult())
        return false;
    }
  }

  RemoveFloatingObjectsForSubtreeRoot(root);
  return true;
}

void LocalFrameView::LayoutOrthogonalWritingModeRoots() {
  for (auto& root : orthogonal_writing_mode_root_list_.Ordered()) {
    if (PrepareOrthogonalWritingModeRootForLayout(*root))
      LayoutFromRootObject(*root);
  }
}

void LocalFrameView::ScheduleOrthogonalWritingModeRootsForLayout() {
  for (auto& root : orthogonal_writing_mode_root_list_.Ordered()) {
    if (PrepareOrthogonalWritingModeRootForLayout(*root))
      layout_subtree_root_list_.Add(*root);
  }
}

void LocalFrameView::MarkOrthogonalWritingModeRootsForLayout() {
  for (auto& root : orthogonal_writing_mode_root_list_.Ordered()) {
    // OOF-positioned objects don't depend on the ICB size.
    if (root->NeedsLayout() || root->IsOutOfFlowPositioned())
      continue;

    root->SetNeedsLayoutAndIntrinsicWidthsRecalc(
        layout_invalidation_reason::kSizeChanged);
  }
}

bool LocalFrameView::CheckLayoutInvalidationIsAllowed() const {
#if DCHECK_IS_ON()
  if (allows_layout_invalidation_after_layout_clean_)
    return true;

  // If we are updating all lifecycle phases beyond LayoutClean, we don't expect
  // dirty layout after LayoutClean.
  CHECK_FOR_DIRTY_LAYOUT(Lifecycle().GetState() <
                         DocumentLifecycle::kLayoutClean);

#endif
  return true;
}

bool LocalFrameView::RunPostLayoutIntersectionObserverSteps() {
  DCHECK(frame_->IsLocalRoot());
  DCHECK(Lifecycle().GetState() >= DocumentLifecycle::kPrePaintClean);

  absl::optional<base::TimeTicks> monotonic_time;
  ComputePostLayoutIntersections(0, monotonic_time);

  bool needs_more_lifecycle_steps = false;
  ForAllNonThrottledLocalFrameViews(
      [&needs_more_lifecycle_steps](LocalFrameView& frame_view) {
        if (auto* controller = frame_view.GetFrame()
                                   .GetDocument()
                                   ->GetIntersectionObserverController()) {
          controller->DeliverNotifications(
              IntersectionObserver::kDeliverDuringPostLayoutSteps);
        }
        // If the lifecycle state changed as a result of the notifications, we
        // should run the lifecycle again.
        needs_more_lifecycle_steps |= frame_view.Lifecycle().GetState() <
                                      DocumentLifecycle::kPrePaintClean;
      });

  return needs_more_lifecycle_steps;
}

void LocalFrameView::ComputePostLayoutIntersections(
    unsigned parent_flags,
    absl::optional<base::TimeTicks>& monotonic_time) {
  if (ShouldThrottleRendering())
    return;

  unsigned flags = GetIntersectionObservationFlags(parent_flags) |
                   IntersectionObservation::kPostLayoutDeliveryOnly;

  if (auto* controller =
          GetFrame().GetDocument()->GetIntersectionObserverController()) {
    controller->ComputeIntersections(flags, EnsureUkmAggregator(),
                                     monotonic_time);
  }

  for (Frame* child = frame_->Tree().FirstChild(); child;
       child = child->Tree().NextSibling()) {
    auto* child_local_frame = DynamicTo<LocalFrame>(child);
    if (!child_local_frame)
      continue;
    if (LocalFrameView* child_view = child_local_frame->View())
      child_view->ComputePostLayoutIntersections(flags, monotonic_time);
  }
}

void LocalFrameView::ScheduleRelayout() {
  DCHECK(frame_->View() == this);

  if (!layout_scheduling_enabled_)
    return;
  // TODO(crbug.com/590856): It's still broken when we choose not to crash when
  // the check fails.
  if (!CheckLayoutInvalidationIsAllowed())
    return;
  if (!NeedsLayout())
    return;
  if (!frame_->GetDocument()->ShouldScheduleLayout())
    return;
  DEVTOOLS_TIMELINE_TRACE_EVENT_INSTANT_WITH_CATEGORIES(
      TRACE_DISABLED_BY_DEFAULT("devtools.timeline"), "InvalidateLayout",
      inspector_invalidate_layout_event::Data, frame_.Get(),
      GetLayoutView()->OwnerNodeId());

  ClearLayoutSubtreeRootsAndMarkContainingBlocks();

  if (has_pending_layout_)
    return;
  has_pending_layout_ = true;

  if (!ShouldThrottleRendering())
    GetPage()->Animator().ScheduleVisualUpdate(frame_.Get());
}

void LocalFrameView::ScheduleRelayoutOfSubtree(LayoutObject* relayout_root) {
  DCHECK(frame_->View() == this);
  DCHECK(relayout_root->IsBox());

  // TODO(crbug.com/590856): It's still broken when we choose not to crash when
  // the check fails.
  if (!CheckLayoutInvalidationIsAllowed())
    return;

  // FIXME: Should this call shouldScheduleLayout instead?
  if (!frame_->GetDocument()->IsActive())
    return;

  LayoutView* layout_view = GetLayoutView();
  if (layout_view && layout_view->NeedsLayout()) {
    if (relayout_root)
      relayout_root->MarkContainerChainForLayout(false);
    return;
  }

  if (relayout_root == layout_view)
    layout_subtree_root_list_.ClearAndMarkContainingBlocksForLayout();
  else
    layout_subtree_root_list_.Add(*relayout_root);

  if (layout_scheduling_enabled_) {
    has_pending_layout_ = true;

    if (!ShouldThrottleRendering())
      GetPage()->Animator().ScheduleVisualUpdate(frame_.Get());

    if (GetPage()->Animator().IsServicingAnimations())
      Lifecycle().EnsureStateAtMost(DocumentLifecycle::kStyleClean);
  }
  DEVTOOLS_TIMELINE_TRACE_EVENT_INSTANT_WITH_CATEGORIES(
      TRACE_DISABLED_BY_DEFAULT("devtools.timeline"), "InvalidateLayout",
      inspector_invalidate_layout_event::Data, frame_.Get(),
      relayout_root->OwnerNodeId());
}

bool LocalFrameView::LayoutPending() const {
  // FIXME: This should check Document::lifecycle instead.
  return has_pending_layout_;
}

bool LocalFrameView::IsInPerformLayout() const {
  return Lifecycle().GetState() == DocumentLifecycle::kInPerformLayout;
}

bool LocalFrameView::NeedsLayout() const {
  // This can return true in cases where the document does not have a body yet.
  // Document::shouldScheduleLayout takes care of preventing us from scheduling
  // layout in that case.

  auto* layout_view = GetLayoutView();
  return LayoutPending() || (layout_view && layout_view->NeedsLayout()) ||
         IsSubtreeLayout();
}

NOINLINE bool LocalFrameView::CheckDoesNotNeedLayout() const {
  CHECK_FOR_DIRTY_LAYOUT(!LayoutPending());
  CHECK_FOR_DIRTY_LAYOUT(!GetLayoutView() || !GetLayoutView()->NeedsLayout());
  CHECK_FOR_DIRTY_LAYOUT(!IsSubtreeLayout());
  return true;
}

void LocalFrameView::SetNeedsLayout() {
  auto* layout_view = GetLayoutView();
  if (!layout_view)
    return;
  // TODO(crbug.com/590856): It's still broken if we choose not to crash when
  // the check fails.
  if (!CheckLayoutInvalidationIsAllowed())
    return;
  layout_view->SetNeedsLayout(layout_invalidation_reason::kUnknown);
}

bool LocalFrameView::ShouldUseColorAdjustBackground() const {
  return use_color_adjust_background_ == UseColorAdjustBackground::kYes ||
         (use_color_adjust_background_ ==
              UseColorAdjustBackground::kIfBaseNotTransparent &&
          base_background_color_ != Color::kTransparent);
}

Color LocalFrameView::BaseBackgroundColor() const {
  if (ShouldUseColorAdjustBackground()) {
    DCHECK(frame_->GetDocument());
    return frame_->GetDocument()->GetStyleEngine().ColorAdjustBackgroundColor();
  }
  return base_background_color_;
}

void LocalFrameView::SetBaseBackgroundColor(const Color& background_color) {
  if (base_background_color_ == background_color)
    return;

  base_background_color_ = background_color;

  if (auto* layout_view = GetLayoutView())
    layout_view->SetBackgroundNeedsFullPaintInvalidation();

  if (!ShouldThrottleRendering())
    GetPage()->Animator().ScheduleVisualUpdate(frame_.Get());
}

void LocalFrameView::SetUseColorAdjustBackground(UseColorAdjustBackground use,
                                                 bool color_scheme_changed) {
  if (use_color_adjust_background_ == use && !color_scheme_changed)
    return;

  if (!frame_->GetDocument())
    return;

  use_color_adjust_background_ = use;

  if (GetFrame().IsMainFrame() && ShouldUseColorAdjustBackground()) {
    // Pass the dark color-scheme background to the browser process to paint a
    // dark background in the browser tab while rendering is blocked in order to
    // avoid flashing the white background in between loading documents. If we
    // perform a navigation within the same renderer process, we keep the
    // content background from the previous page while rendering is blocked in
    // the new page, but for cross process navigations we would paint the
    // default background (typically white) while the rendering is blocked.
    GetFrame().DidChangeBackgroundColor(
        BaseBackgroundColor().ToSkColorDeprecated(), true /* color_adjust */);
  }

  if (auto* layout_view = GetLayoutView())
    layout_view->SetBackgroundNeedsFullPaintInvalidation();
}

bool LocalFrameView::ShouldPaintBaseBackgroundColor() const {
  return ShouldUseColorAdjustBackground() ||
         frame_->GetDocument()->IsInMainFrame();
}

void LocalFrameView::UpdateBaseBackgroundColorRecursively(
    const Color& base_background_color) {
  ForAllNonThrottledLocalFrameViews(
      [base_background_color](LocalFrameView& frame_view) {
        frame_view.SetBaseBackgroundColor(base_background_color);
      });
}

void LocalFrameView::InvokeFragmentAnchor() {
  if (!fragment_anchor_)
    return;

  if (!fragment_anchor_->Invoke())
    fragment_anchor_ = nullptr;
}

void LocalFrameView::ClearFragmentAnchor() {
  fragment_anchor_ = nullptr;
}

bool LocalFrameView::UpdatePlugins() {
  // This is always called from UpdatePluginsTimerFired.
  // update_plugins_timer should only be scheduled if we have FrameViews to
  // update. Thus I believe we can stop checking isEmpty here, and just ASSERT
  // isEmpty:
  // FIXME: This assert has been temporarily removed due to
  // https://crbug.com/430344
  if (part_update_set_.IsEmpty())
    return true;

  // Need to swap because script will run inside the below loop and invalidate
  // the iterator.
  EmbeddedObjectSet objects;
  objects.swap(part_update_set_);

  for (const auto& embedded_object : objects) {
    LayoutEmbeddedObject& object = *embedded_object;

#if DCHECK_IS_ON()
    if (object.is_destroyed_)
      continue;
#endif

    auto* element = To<HTMLPlugInElement>(object.GetNode());

    // The object may have already been destroyed (thus node cleared).
    if (!element)
      continue;

    // No need to update if it's already crashed or known to be missing.
    if (object.ShowsUnavailablePluginIndicator())
      continue;

    if (element->NeedsPluginUpdate() && element->GetLayoutObject())
      element->UpdatePlugin();
    if (EmbeddedContentView* view = element->OwnedEmbeddedContentView())
      view->UpdateGeometry();

    // Prevent plugins from causing infinite updates of themselves.
    // FIXME: Do we really need to prevent this?
    part_update_set_.erase(&object);
  }

  return part_update_set_.IsEmpty();
}

void LocalFrameView::UpdatePluginsTimerFired(TimerBase*) {
  DCHECK(!IsInPerformLayout());
  for (unsigned i = 0; i < kMaxUpdatePluginsIterations; ++i) {
    if (UpdatePlugins())
      return;
  }
}

void LocalFrameView::FlushAnyPendingPostLayoutTasks() {
  DCHECK(!IsInPerformLayout());
  if (update_plugins_timer_.IsActive()) {
    update_plugins_timer_.Stop();
    UpdatePluginsTimerFired(nullptr);
  }
}

void LocalFrameView::ScheduleUpdatePluginsIfNecessary() {
  DCHECK(!IsInPerformLayout());
  if (update_plugins_timer_.IsActive() || part_update_set_.IsEmpty())
    return;
  update_plugins_timer_.StartOneShot(base::TimeDelta(), FROM_HERE);
}

void LocalFrameView::PerformPostLayoutTasks(bool visual_viewport_size_changed) {
  // FIXME: We can reach here, even when the page is not active!
  // http/tests/inspector/elements/html-link-import.html and many other
  // tests hit that case.
  // We should DCHECK(isActive()); or at least return early if we can!

  // Always called before or after performLayout(), part of the highest-level
  // layout() call.
  DCHECK(!IsInPerformLayout());
  TRACE_EVENT0("blink,benchmark", "LocalFrameView::performPostLayoutTasks");

  frame_timing_requests_dirty_ = true;
  TRACE_EVENT_OBJECT_SNAPSHOT_WITH_ID(
      TRACE_DISABLED_BY_DEFAULT("blink.debug.layout.trees"), "LayoutTree", this,
      TracedLayoutObject::Create(*GetLayoutView(), true));
  layout_count_for_testing_++;
  Document* document = GetFrame().GetDocument();
  DCHECK(document);
  if (AXObjectCache* cache = document->ExistingAXObjectCache()) {
    const KURL& url = document->Url();
    if (url.IsValid() && !url.IsAboutBlankURL()) {
      // TODO(kschmi) move HandleLayoutComplete to the accessibility lifecycle
      // stage. crbug.com/1062122
      cache->HandleLayoutComplete(document);
    }
  }

  UpdateDocumentAnnotatedRegions();

  GetLayoutView()->Layer()->UpdateLayerPositionsAfterLayout();
  frame_->Selection().DidLayout();

  FontFaceSetDocument::DidLayout(*document);
  // Fire a fake a mouse move event to update hover state and mouse cursor, and
  // send the right mouse out/over events.
  // TODO(lanwei): we should check whether the mouse is inside the frame before
  // dirtying the hover state.
  frame_->LocalFrameRoot().GetEventHandler().MarkHoverStateDirty();

  UpdateGeometriesIfNeeded();

  // Plugins could have torn down the page inside updateGeometries().
  if (!GetLayoutView())
    return;

  ScheduleUpdatePluginsIfNecessary();
  if (visual_viewport_size_changed && !document->Printing())
    frame_->GetDocument()->EnqueueVisualViewportResizeEvent();

  GetLayoutView()->GetDeferredShapingController().PerformPostLayoutTask();
}

float LocalFrameView::InputEventsScaleFactor() const {
  float page_scale = frame_->GetPage()->GetVisualViewport().Scale();
  return page_scale *
         frame_->GetPage()->GetChromeClient().InputEventsScaleForEmulation();
}

void LocalFrameView::NotifyPageThatContentAreaWillPaint() const {
  Page* page = frame_->GetPage();
  if (!page)
    return;

  if (!user_scrollable_areas_)
    return;

  for (const auto& scrollable_area : *user_scrollable_areas_) {
    if (!scrollable_area->ScrollbarsCanBeActive())
      continue;

    scrollable_area->ContentAreaWillPaint();
  }
}

void LocalFrameView::UpdateDocumentAnnotatedRegions() const {
  Document* document = frame_->GetDocument();
  if (!document->HasAnnotatedRegions())
    return;
  Vector<AnnotatedRegionValue> new_regions;
  CollectAnnotatedRegions(*(document->GetLayoutBox()), new_regions);
  if (new_regions == document->AnnotatedRegions())
    return;
  document->SetAnnotatedRegions(new_regions);

  DCHECK(frame_->Client());
  frame_->Client()->AnnotatedRegionsChanged();
}

void LocalFrameView::DidAttachDocument() {
  Page* page = frame_->GetPage();
  DCHECK(page);

  VisualViewport& visual_viewport = page->GetVisualViewport();

  if (frame_->IsMainFrame() && visual_viewport.IsActiveViewport()) {
    // If this frame is provisional it's not yet the Page's main frame. In that
    // case avoid creating a root scroller as it has Page-global effects; it
    // will be initialized when the frame becomes the Page's main frame.
    if (!frame_->IsProvisional())
      InitializeRootScroller();
  }

  if (frame_->IsMainFrame()) {
    // Allow for commits to be deferred because this is a new document.
    have_deferred_main_frame_commits_ = false;
  }
}

void LocalFrameView::InitializeRootScroller() {
  Page* page = frame_->GetPage();
  DCHECK(page);

  DCHECK_EQ(frame_, page->MainFrame());
  DCHECK(frame_->GetDocument());
  DCHECK(frame_->GetDocument()->IsActive());

  VisualViewport& visual_viewport = frame_->GetPage()->GetVisualViewport();
  DCHECK(visual_viewport.IsActiveViewport());

  ScrollableArea* layout_viewport = LayoutViewport();
  DCHECK(layout_viewport);

  // This method may be called multiple times during loading. If the root
  // scroller is already initialized this call will be a no-op.
  if (viewport_scrollable_area_)
    return;

  auto* root_frame_viewport = MakeGarbageCollected<RootFrameViewport>(
      visual_viewport, *layout_viewport);
  viewport_scrollable_area_ = root_frame_viewport;

  DCHECK(frame_->GetDocument());
  page->GlobalRootScrollerController().InitializeViewportScrollCallback(
      *root_frame_viewport, *frame_->GetDocument());
}

Color LocalFrameView::DocumentBackgroundColor() {
  // The LayoutView's background color is set in
  // StyleResolver::PropagateStyleToViewport(). Blend this with the base
  // background color of the LocalFrameView. This should match the color drawn
  // by ViewPainter::paintBoxDecorationBackground.
  Color result = BaseBackgroundColor();

  bool blend_with_base = true;
  LayoutObject* background_source = GetLayoutView();

  // If we have a fullscreen element grab the fullscreen color from the
  // backdrop.
  if (Document* doc = frame_->GetDocument()) {
    if (Element* element = Fullscreen::FullscreenElementFrom(*doc)) {
      if (LayoutObject* layout_object =
              element->PseudoElementLayoutObject(kPseudoIdBackdrop)) {
        background_source = layout_object;
      }
      if (doc->IsXrOverlay()) {
        // Use the fullscreened element's background directly. Don't bother
        // blending with the backdrop since that's transparent.
        blend_with_base = false;
        if (LayoutObject* layout_object = element->GetLayoutObject())
          background_source = layout_object;
      }
    }
  }

  if (!background_source)
    return result;

  Color doc_bg =
      background_source->ResolveColor(GetCSSPropertyBackgroundColor());
  if (background_source->StyleRef().ColorSchemeForced()) {
    // TODO(https://crbug.com/1351544): The DarkModeFilter operate on SkColor4f,
    // and DocumentBackgroundColor should return an SkColor4f.
    doc_bg = Color::FromSkColor(EnsureDarkModeFilter().InvertColorIfNeeded(
        doc_bg.ToSkColorDeprecated(),
        DarkModeFilter::ElementRole::kBackground));
  }
  if (blend_with_base)
    return result.Blend(doc_bg);
  return doc_bg;
}

void LocalFrameView::WillBeRemovedFromFrame() {
  if (paint_artifact_compositor_)
    paint_artifact_compositor_->WillBeRemovedFromFrame();

  if (Settings* settings = frame_->GetSettings()) {
    DCHECK(frame_->GetPage());
    if (settings->GetSpatialNavigationEnabled()) {
      frame_->GetPage()->GetSpatialNavigationController().DidDetachFrameView(
          *this);
    }
  }
}

bool LocalFrameView::IsUpdatingLifecycle() const {
  LocalFrameView* root_view = GetFrame().LocalFrameRoot().View();
  DCHECK(root_view);
  return root_view->target_state_ != DocumentLifecycle::kUninitialized;
}

LocalFrameView* LocalFrameView::ParentFrameView() const {
  if (!IsAttached())
    return nullptr;

  Frame* parent_frame = frame_->Tree().Parent();
  if (auto* parent_local_frame = DynamicTo<LocalFrame>(parent_frame))
    return parent_local_frame->View();

  return nullptr;
}

LayoutEmbeddedContent* LocalFrameView::GetLayoutEmbeddedContent() const {
  return frame_->OwnerLayoutObject();
}

bool LocalFrameView::LoadAllLazyLoadedIframes() {
  bool result = false;
  ForAllChildViewsAndPlugins([&](EmbeddedContentView& view) {
    if (auto* embed = view.GetLayoutEmbeddedContent()) {
      if (auto* node = embed->GetNode()) {
        if (auto* frame_owner = DynamicTo<HTMLFrameOwnerElement>(node)) {
          result = result || frame_owner->LoadImmediatelyIfLazy();
        }
      }
    }
  });
  return result;
}

void LocalFrameView::UpdateGeometriesIfNeeded() {
  if (!needs_update_geometries_)
    return;
  needs_update_geometries_ = false;
  HeapVector<Member<EmbeddedContentView>> views;
  ForAllChildViewsAndPlugins(
      [&](EmbeddedContentView& view) { views.push_back(view); });

  for (const auto& view : views) {
    // Script or plugins could detach the frame so abort processing if that
    // happens.
    if (!GetLayoutView())
      break;

    view->UpdateGeometry();
  }
  // Explicitly free the backing store to avoid memory regressions.
  // TODO(bikineev): Revisit after young generation is there.
  views.clear();
}

bool LocalFrameView::UpdateAllLifecyclePhases(DocumentUpdateReason reason) {
  AllowThrottlingScope allow_throttling(*this);
  bool updated = GetFrame().LocalFrameRoot().View()->UpdateLifecyclePhases(
      DocumentLifecycle::kPaintClean, reason);

#if DCHECK_IS_ON()
  if (updated) {
    // This function should return true iff all non-throttled frames are in the
    // kPaintClean lifecycle state.
    ForAllNonThrottledLocalFrameViews([](LocalFrameView& frame_view) {
      DCHECK_EQ(frame_view.Lifecycle().GetState(),
                DocumentLifecycle::kPaintClean);
    });

    // A required intersection observation should run throttled frames to
    // kLayoutClean.
    ForAllThrottledLocalFrameViews([](LocalFrameView& frame_view) {
      DCHECK(frame_view.intersection_observation_state_ != kRequired ||
             frame_view.IsDisplayLocked() ||
             frame_view.Lifecycle().GetState() >=
                 DocumentLifecycle::kLayoutClean);
    });
  }
#endif

  return updated;
}

bool LocalFrameView::UpdateAllLifecyclePhasesForTest() {
  bool result = UpdateAllLifecyclePhases(DocumentUpdateReason::kTest);
  RunPostLifecycleSteps();
  return result;
}

bool LocalFrameView::UpdateLifecycleToPrePaintClean(
    DocumentUpdateReason reason) {
  return GetFrame().LocalFrameRoot().View()->UpdateLifecyclePhases(
      DocumentLifecycle::kPrePaintClean, reason);
}

bool LocalFrameView::UpdateLifecycleToCompositingInputsClean(
    DocumentUpdateReason reason) {
  return GetFrame().LocalFrameRoot().View()->UpdateLifecyclePhases(
      DocumentLifecycle::kCompositingInputsClean, reason);
}

bool LocalFrameView::UpdateAllLifecyclePhasesExceptPaint(
    DocumentUpdateReason reason) {
  return GetFrame().LocalFrameRoot().View()->UpdateLifecyclePhases(
      DocumentLifecycle::kPrePaintClean, reason);
}

void LocalFrameView::UpdateLifecyclePhasesForPrinting() {
  auto* local_frame_view_root = GetFrame().LocalFrameRoot().View();
  local_frame_view_root->UpdateLifecyclePhases(
      DocumentLifecycle::kPrePaintClean, DocumentUpdateReason::kPrinting);

  if (local_frame_view_root != this && !IsAttached()) {
    // We are printing a detached frame which is not reached above. Make sure
    // the frame is ready for painting.
    UpdateLifecyclePhases(DocumentLifecycle::kPrePaintClean,
                          DocumentUpdateReason::kPrinting);
  }
}

bool LocalFrameView::UpdateLifecycleToLayoutClean(DocumentUpdateReason reason) {
  return GetFrame().LocalFrameRoot().View()->UpdateLifecyclePhases(
      DocumentLifecycle::kLayoutClean, reason);
}

void LocalFrameView::ScheduleVisualUpdateForPaintInvalidationIfNeeded() {
  LocalFrame& local_frame_root = GetFrame().LocalFrameRoot();
  // We need a full lifecycle update to clear pending paint invalidations.
  if (local_frame_root.View()->target_state_ < DocumentLifecycle::kPaintClean ||
      Lifecycle().GetState() >= DocumentLifecycle::kPrePaintClean) {
    // Schedule visual update to process the paint invalidation in the next
    // cycle.
    local_frame_root.ScheduleVisualUpdateUnlessThrottled();
  }
  // Otherwise the paint invalidation will be handled in the pre-paint and paint
  // phase of this full lifecycle update.
}

bool LocalFrameView::NotifyResizeObservers(
    DocumentLifecycle::LifecycleState target_state) {
  // Return true if lifecycles need to be re-run
  TRACE_EVENT0("blink,benchmark", "LocalFrameView::NotifyResizeObservers");

  if (target_state < DocumentLifecycle::kPaintClean)
    return false;

  // Controller exists only if ResizeObserver was created.
  ResizeObserverController* resize_controller =
      ResizeObserverController::FromIfExists(*GetFrame().DomWindow());
  if (!resize_controller)
    return false;

  DCHECK(Lifecycle().GetState() >= DocumentLifecycle::kPrePaintClean);

  size_t min_depth = resize_controller->GatherObservations();

  if (min_depth != ResizeObserverController::kDepthBottom) {
    resize_controller->DeliverObservations();
  } else {
    // Observation depth limit reached
    if (resize_controller->SkippedObservations() &&
        !resize_controller->IsLoopLimitErrorDispatched()) {
      resize_controller->ClearObservations();
      ErrorEvent* error = ErrorEvent::Create(
          "ResizeObserver loop limit exceeded",
          CaptureSourceLocation(frame_->DomWindow()), nullptr);
      // We're using |SanitizeScriptErrors::kDoNotSanitize| as the error is made
      // by blink itself.
      // TODO(yhirano): Reconsider this.
      frame_->DomWindow()->DispatchErrorEvent(
          error, SanitizeScriptErrors::kDoNotSanitize);
      // Ensure notifications will get delivered in next cycle.
      ScheduleAnimation();
      resize_controller->SetLoopLimitErrorDispatched(true);
    }
    if (Lifecycle().GetState() >= DocumentLifecycle::kPrePaintClean)
      return false;
  }

  // Lifecycle needs to be run again because Resize Observer affected layout
  return true;
}

bool LocalFrameView::LocalFrameTreeAllowsThrottling() const {
  if (LocalFrameView* root_view = GetFrame().LocalFrameRoot().View())
    return root_view->allow_throttling_;
  return false;
}

bool LocalFrameView::LocalFrameTreeForcesThrottling() const {
  if (LocalFrameView* root_view = GetFrame().LocalFrameRoot().View())
    return root_view->force_throttling_;
  return false;
}

void LocalFrameView::PrepareForLifecycleUpdateRecursive() {
  // We will run lifecycle phases for LocalFrameViews that are unthrottled; or
  // are throttled but require IntersectionObserver steps to run.
  if (!ShouldThrottleRendering() ||
      intersection_observation_state_ == kRequired) {
    Lifecycle().EnsureStateAtMost(DocumentLifecycle::kVisualUpdatePending);
    ForAllChildLocalFrameViews([](LocalFrameView& child) {
      child.PrepareForLifecycleUpdateRecursive();
    });
  }
}

// TODO(leviw): We don't assert lifecycle information from documents in child
// WebPluginContainerImpls.
bool LocalFrameView::UpdateLifecyclePhases(
    DocumentLifecycle::LifecycleState target_state,
    DocumentUpdateReason reason) {
  // If the lifecycle is postponed, which can happen if the inspector requests
  // it, then we shouldn't update any lifecycle phases.
  if (UNLIKELY(frame_->GetDocument() &&
               frame_->GetDocument()->Lifecycle().LifecyclePostponed())) {
    return false;
  }

  // Prevent reentrance.
  // TODO(vmpstr): Should we just have a DCHECK instead here?
  if (UNLIKELY(IsUpdatingLifecycle())) {
    NOTREACHED()
        << "LocalFrameView::updateLifecyclePhasesInternal() reentrance";
    return false;
  }

  // This must be called from the root frame, or a detached frame for printing,
  // since it recurses down, not up. Otherwise the lifecycles of the frames
  // might be out of sync.
  DCHECK(frame_->IsLocalRoot() || !IsAttached());

  DCHECK(LocalFrameTreeAllowsThrottling() ||
         (target_state < DocumentLifecycle::kPaintClean));

  // Only the following target states are supported.
  DCHECK(target_state == DocumentLifecycle::kLayoutClean ||
         target_state == DocumentLifecycle::kAccessibilityClean ||
         target_state == DocumentLifecycle::kCompositingInputsClean ||
         target_state == DocumentLifecycle::kPrePaintClean ||
         target_state == DocumentLifecycle::kPaintClean);
  lifecycle_update_count_for_testing_++;

  // If the document is not active then it is either not yet initialized, or it
  // is stopping. In either case, we can't reach one of the supported target
  // states.
  if (!frame_->GetDocument()->IsActive())
    return false;

  // If we're throttling and we aren't required to run the IntersectionObserver
  // steps, then we don't need to update lifecycle phases. The throttling status
  // will get updated in RunPostLifecycleSteps().
  if (ShouldThrottleRendering() &&
      intersection_observation_state_ < kRequired) {
    return Lifecycle().GetState() == target_state;
  }

  PrepareForLifecycleUpdateRecursive();

  // This is used to guard against reentrance. It is also used in conjunction
  // with the current lifecycle state to determine which phases are yet to run
  // in this cycle. Note that this may change the return value of
  // ShouldThrottleRendering(), hence it cannot be moved before the preceeding
  // code, which relies on the prior value of ShouldThrottleRendering().
  base::AutoReset<DocumentLifecycle::LifecycleState> target_state_scope(
      &target_state_, target_state);

  lifecycle_data_.start_time = base::TimeTicks::Now();
  ++lifecycle_data_.count;

  if (target_state == DocumentLifecycle::kPaintClean) {
    {
      TRACE_EVENT0("blink", "LocalFrameView::WillStartLifecycleUpdate");

      ForAllNonThrottledLocalFrameViews([](LocalFrameView& frame_view) {
        auto lifecycle_observers = frame_view.lifecycle_observers_;
        for (auto& observer : lifecycle_observers)
          observer->WillStartLifecycleUpdate(frame_view);
      });
    }

    {
      TRACE_EVENT0(
          "blink",
          "LocalFrameView::UpdateLifecyclePhases - start of lifecycle tasks");
      ForAllNonThrottledLocalFrameViews([](LocalFrameView& frame_view) {
        WTF::Vector<base::OnceClosure> tasks;
        frame_view.start_of_lifecycle_tasks_.swap(tasks);
        for (auto& task : tasks)
          std::move(task).Run();
      });
    }
  }

  absl::optional<base::AutoReset<bool>> force_debug_info;
  if (reason == DocumentUpdateReason::kTest)
    force_debug_info.emplace(&layer_debug_info_enabled_, true);

  // Run the lifecycle updates.
  UpdateLifecyclePhasesInternal(target_state);

  if (target_state == DocumentLifecycle::kPaintClean) {
    TRACE_EVENT0("blink", "LocalFrameView::DidFinishLifecycleUpdate");

    ForAllNonThrottledLocalFrameViews([](LocalFrameView& frame_view) {
      auto lifecycle_observers = frame_view.lifecycle_observers_;
      for (auto& observer : lifecycle_observers)
        observer->DidFinishLifecycleUpdate(frame_view);
    });
  }

  // Hit testing metrics include the entire time processing a document update
  // in preparation for a hit test.
  if (reason == DocumentUpdateReason::kHitTest) {
    LocalFrameUkmAggregator& aggregator = EnsureUkmAggregator();
    aggregator.RecordTimerSample(
        static_cast<size_t>(LocalFrameUkmAggregator::kHitTestDocumentUpdate),
        lifecycle_data_.start_time, base::TimeTicks::Now());
  }

  return Lifecycle().GetState() == target_state;
}

void LocalFrameView::UpdateLifecyclePhasesInternal(
    DocumentLifecycle::LifecycleState target_state) {
  // TODO(https://crbug.com/1196853): Switch to ScriptForbiddenScope once
  // failures are fixed.
  BlinkLifecycleScopeWillBeScriptForbidden forbid_script;

  // RunScrollTimelineSteps must not run more than once.
  bool should_run_scroll_timeline_steps = true;

  // CSS Toggle steps must not run more than once.
  bool should_run_css_toggle_steps = true;

  // Run style, layout, compositing and prepaint lifecycle phases and deliver
  // resize observations if required. Resize observer callbacks/delegates have
  // the potential to dirty layout (until loop limit is reached) and therefore
  // the above lifecycle phases need to be re-run until the limit is reached
  // or no layout is pending.
  // Note that after ResizeObserver has settled, we also run intersection
  // observations that need to be delievered in post-layout. This process can
  // also dirty layout, which will run this loop again.

  // A LocalFrameView can be unthrottled at this point, but become throttled as
  // it advances through lifecycle stages. If that happens, it will prevent
  // subsequent passes through the loop from updating the newly-throttled views.
  // To avoid that, we lock in the set of unthrottled views before entering the
  // loop.
  HeapVector<Member<LocalFrameView>> unthrottled_frame_views;
  ForAllNonThrottledLocalFrameViews(
      [&unthrottled_frame_views](LocalFrameView& frame_view) {
        unthrottled_frame_views.push_back(&frame_view);
      });

  while (true) {
    for (LocalFrameView* frame_view : unthrottled_frame_views) {
      // RunResizeObserverSteps may run arbitrary script, which can cause a
      // frame to become detached.
      if (frame_view->GetFrame().IsAttached()) {
        frame_view->Lifecycle().EnsureStateAtMost(
            DocumentLifecycle::kVisualUpdatePending);
      }
    }
    bool run_more_lifecycle_phases =
        RunStyleAndLayoutLifecyclePhases(target_state);

    if (RuntimeEnabledFeatures::CSSTogglesEnabled() &&
        should_run_css_toggle_steps) {
      should_run_css_toggle_steps = false;
      bool needs_to_repeat_lifecycle = RunCSSToggleSteps();
      if (needs_to_repeat_lifecycle)
        continue;
    }

    if (!run_more_lifecycle_phases)
      return;
    DCHECK(Lifecycle().GetState() >= DocumentLifecycle::kLayoutClean);

    if (!GetLayoutView())
      return;

    {
      // We need scoping braces here because this
      // DisallowLayoutInvalidationScope is meant to be in effect during
      // pre-paint, but not during ResizeObserver.
#if DCHECK_IS_ON()
      DisallowLayoutInvalidationScope disallow_layout_invalidation(this);
#endif

      DCHECK_GE(target_state, DocumentLifecycle::kAccessibilityClean);
      run_more_lifecycle_phases = RunAccessibilityLifecyclePhase(target_state);
      DCHECK(ShouldThrottleRendering() || !ExistingAXObjectCache() ||
             Lifecycle().GetState() == DocumentLifecycle::kAccessibilityClean);
      if (!run_more_lifecycle_phases)
        return;

      DEVTOOLS_TIMELINE_TRACE_EVENT_INSTANT_WITH_CATEGORIES(
          TRACE_DISABLED_BY_DEFAULT("devtools.timeline"), "SetLayerTreeId",
          inspector_set_layer_tree_id::Data, frame_.Get());
      // The Compositing Inputs lifecycle phase should be integrated into the
      // PrePaint lifecycle phase in the future. The difference between these
      // two stages is not relevant to web developers, so include them both
      // under PrePaint.
      DEVTOOLS_TIMELINE_TRACE_EVENT("PrePaint", inspector_pre_paint_event::Data,
                                    frame_.Get());
      run_more_lifecycle_phases =
          RunCompositingInputsLifecyclePhase(target_state);
      if (!run_more_lifecycle_phases)
        return;

      run_more_lifecycle_phases = RunPrePaintLifecyclePhase(target_state);
      DCHECK(ShouldThrottleRendering() ||
             Lifecycle().GetState() >= DocumentLifecycle::kPrePaintClean);
      if (ShouldThrottleRendering() || !run_more_lifecycle_phases)
        return;
    }

    // Some features may require several passes over style and layout
    // within the same lifecycle update.
    bool needs_to_repeat_lifecycle = false;

    // ScrollTimelines may be associated with a source that never had a
    // a chance to get a layout box at the time style was calculated; when
    // this situation happens, RunScrollTimelineSteps will re-snapshot all
    // affected timelines and dirty style for associated effect targets.
    //
    // https://github.com/w3c/csswg-drafts/issues/5261
    if (RuntimeEnabledFeatures::CSSScrollTimelineEnabled() &&
        should_run_scroll_timeline_steps) {
      should_run_scroll_timeline_steps = false;
      needs_to_repeat_lifecycle = RunScrollTimelineSteps();
      if (needs_to_repeat_lifecycle)
        continue;
    }

    // ResizeObserver and post-layout IntersectionObserver observation
    // deliveries may dirty style and layout. RunResizeObserverSteps will return
    // true if any observer ran that may have dirtied style or layout;
    // RunPostLayoutIntersectionObserverSteps will return true if any
    // observations led to content-visibility intersection changing visibility
    // state synchronously (which happens on the first intersection
    // observeration of a context).
    {
      ScriptForbiddenScope::AllowUserAgentScript allow_script;
      needs_to_repeat_lifecycle = RunResizeObserverSteps(target_state);
    }
    // Only run the rest of the steps here if resize observer is done.
    if (needs_to_repeat_lifecycle)
      continue;

    // DocumentTransition mutates the tree and mirrors post layout transform for
    // shared elements to UA created elements. This may dirty style/layout
    // requiring another lifecycle update.
    needs_to_repeat_lifecycle = RunDocumentTransitionSteps(target_state);
    if (needs_to_repeat_lifecycle)
      continue;

    {
      ScriptForbiddenScope::AllowUserAgentScript allow_script;
      needs_to_repeat_lifecycle = RunPostLayoutIntersectionObserverSteps();
    }
    if (!needs_to_repeat_lifecycle)
      break;
  }

  // Once we exit the ResizeObserver / IntersectionObserver loop above, we need
  // to clear the resize observer limits so that next time we run this, we can
  // deliver more observations.
  ClearResizeObserverLimit();

  // Layout invalidation scope was disabled for resize observer
  // re-enable it for subsequent steps
#if DCHECK_IS_ON()
  DisallowLayoutInvalidationScope disallow_layout_invalidation(this);
#endif

  // This needs to be done prior to paint: it will update the cc::Layer bounds
  // for the remote frame views, which will be wrapped during paint in
  // ForeignLayerDisplayItem's whose visual rect is set at construction based
  // on cc::Layer bounds.
  ForAllRemoteFrameViews(
      [](RemoteFrameView& frame_view) { frame_view.UpdateCompositingRect(); });

  DCHECK_EQ(target_state, DocumentLifecycle::kPaintClean);
  RunPaintLifecyclePhase(PaintBenchmarkMode::kNormal);
  DCHECK(ShouldThrottleRendering() || AnyFrameIsPrintingOrPaintingPreview() ||
         Lifecycle().GetState() == DocumentLifecycle::kPaintClean);
}

bool LocalFrameView::RunScrollTimelineSteps() {
  // TODO(crbug.com/1329159): Determine if the source for a view timeline has
  // changed, which may in turn require a fresh style/layout cycle.

  DCHECK_GE(Lifecycle().GetState(), DocumentLifecycle::kPrePaintClean);
  bool re_run_lifecycles = false;
  ForAllNonThrottledLocalFrameViews([&re_run_lifecycles](
                                        LocalFrameView& frame_view) {
    frame_view.GetFrame()
        .GetDocument()
        ->GetDocumentAnimations()
        .ValidateTimelines();
    re_run_lifecycles |=
        (frame_view.Lifecycle().GetState() < DocumentLifecycle::kPrePaintClean);
  });
  return re_run_lifecycles;
}

bool LocalFrameView::RunCSSToggleSteps() {
  bool re_run_lifecycles = false;
  ForAllNonThrottledLocalFrameViews([&re_run_lifecycles](
                                        LocalFrameView& frame_view) {
    re_run_lifecycles |=
        frame_view.GetFrame().GetDocument()->SetNeedsStyleRecalcForToggles();
  });
  return re_run_lifecycles;
}

bool LocalFrameView::RunDocumentTransitionSteps(
    DocumentLifecycle::LifecycleState target_state) {
  DCHECK(frame_ && frame_->GetDocument());

  if (target_state != DocumentLifecycle::kPaintClean)
    return false;

  auto* document_transition_supplement =
      DocumentTransitionSupplement::FromIfExists(*frame_->GetDocument());
  if (!document_transition_supplement)
    return false;

  document_transition_supplement->GetTransition()->RunPostPrePaintSteps();
  return Lifecycle().GetState() < DocumentLifecycle::kPrePaintClean;
}

bool LocalFrameView::RunResizeObserverSteps(
    DocumentLifecycle::LifecycleState target_state) {
  bool re_run_lifecycles = false;
  if (target_state == DocumentLifecycle::kPaintClean) {
    ForAllNonThrottledLocalFrameViews(
        [&re_run_lifecycles](LocalFrameView& frame_view) {
          bool result =
              frame_view.NotifyResizeObservers(DocumentLifecycle::kPaintClean);
          re_run_lifecycles = re_run_lifecycles || result;
        });
  }
  return re_run_lifecycles;
}

void LocalFrameView::ClearResizeObserverLimit() {
  ForAllNonThrottledLocalFrameViews([](LocalFrameView& frame_view) {
    ResizeObserverController* resize_controller =
        ResizeObserverController::From(*frame_view.frame_->DomWindow());
    resize_controller->ClearMinDepth();
    resize_controller->SetLoopLimitErrorDispatched(false);
  });
}

bool LocalFrameView::RunStyleAndLayoutLifecyclePhases(
    DocumentLifecycle::LifecycleState target_state) {
  TRACE_EVENT0("blink,benchmark",
               "LocalFrameView::RunStyleAndLayoutLifecyclePhases");
  UpdateStyleAndLayoutIfNeededRecursive();
  DCHECK(ShouldThrottleRendering() ||
         Lifecycle().GetState() >= DocumentLifecycle::kLayoutClean);
  if (Lifecycle().GetState() < DocumentLifecycle::kLayoutClean)
    return false;

  // PerformRootScrollerSelection can dirty layout if an effective root
  // scroller is changed so make sure we get back to LayoutClean.
  if (frame_->GetDocument()
          ->GetRootScrollerController()
          .PerformRootScrollerSelection() &&
      RuntimeEnabledFeatures::ImplicitRootScrollerEnabled()) {
    UpdateStyleAndLayoutIfNeededRecursive();
  }

  if (target_state == DocumentLifecycle::kLayoutClean)
    return false;

  // Now we can run post layout steps in preparation for further phases.
  ForAllNonThrottledLocalFrameViews([](LocalFrameView& frame_view) {
    frame_view.PerformScrollAnchoringAdjustments();
  });

  frame_->GetDocument()->PerformScrollSnappingTasks();

  EnqueueScrollEvents();

  frame_->GetPage()->GetValidationMessageClient().LayoutOverlay();

  if (target_state == DocumentLifecycle::kPaintClean) {
    ForAllNonThrottledLocalFrameViews([](LocalFrameView& frame_view) {
      frame_view.NotifyFrameRectsChangedIfNeeded();
    });
  }

  return Lifecycle().GetState() >= DocumentLifecycle::kLayoutClean;
}

bool LocalFrameView::RunCompositingInputsLifecyclePhase(
    DocumentLifecycle::LifecycleState target_state) {
  TRACE_EVENT0("blink,benchmark",
               "LocalFrameView::RunCompositingInputsLifecyclePhase");
  auto* layout_view = GetLayoutView();
  DCHECK(layout_view);

  SCOPED_UMA_AND_UKM_TIMER(EnsureUkmAggregator(),
                           LocalFrameUkmAggregator::kCompositingInputs);
  // TODO(pdr): This descendant dependent treewalk should be integrated into
  // the prepaint tree walk.
  {
#if DCHECK_IS_ON()
    SetIsUpdatingDescendantDependentFlags(true);
#endif
    ForAllNonThrottledLocalFrameViews([](LocalFrameView& frame_view) {
      frame_view.GetLayoutView()->Layer()->UpdateDescendantDependentFlags();
      frame_view.GetLayoutView()->CommitPendingSelection();
    });
#if DCHECK_IS_ON()
    SetIsUpdatingDescendantDependentFlags(false);
#endif
  }

  ForAllNonThrottledLocalFrameViews([](LocalFrameView& frame_view) {
    frame_view.Lifecycle().AdvanceTo(
        DocumentLifecycle::kCompositingInputsClean);
  });

  return target_state > DocumentLifecycle::kCompositingInputsClean;
}

bool LocalFrameView::RunPrePaintLifecyclePhase(
    DocumentLifecycle::LifecycleState target_state) {
  TRACE_EVENT0("blink,benchmark", "LocalFrameView::RunPrePaintLifecyclePhase");

  ForAllNonThrottledLocalFrameViews(
      [](LocalFrameView& frame_view) {
        frame_view.Lifecycle().AdvanceTo(DocumentLifecycle::kInPrePaint);

        // Validate all HighlightMarkers of all non-throttled LocalFrameViews
        // before paint phase so the nodes affected by markers removed/added are
        // invalidated and then painted during this lifecycle.
        if (LocalDOMWindow* window = frame_view.GetFrame().DomWindow()) {
          if (HighlightRegistry* highlight_registry =
                  window->Supplementable<LocalDOMWindow>::RequireSupplement<
                      HighlightRegistry>()) {
            highlight_registry->ValidateHighlightMarkers();
          }
        }

        // Propagate dirty bits in the frame into the parent frame so that
        // pre-paint reaches into this frame.
        if (LayoutView* layout_view = frame_view.GetLayoutView()) {
          if (auto* owner = frame_view.GetFrame().OwnerLayoutObject()) {
            if (layout_view->NeedsPaintPropertyUpdate() ||
                layout_view->DescendantNeedsPaintPropertyUpdate()) {
              owner->SetDescendantNeedsPaintPropertyUpdate();
            }
            if (layout_view->ShouldCheckForPaintInvalidation()) {
              owner->SetShouldCheckForPaintInvalidation();
            }
            if (layout_view->EffectiveAllowedTouchActionChanged() ||
                layout_view->DescendantEffectiveAllowedTouchActionChanged()) {
              owner->MarkDescendantEffectiveAllowedTouchActionChanged();
            }
            if (layout_view->BlockingWheelEventHandlerChanged() ||
                layout_view->DescendantBlockingWheelEventHandlerChanged()) {
              owner->MarkDescendantBlockingWheelEventHandlerChanged();
            }
            if (!RuntimeEnabledFeatures::ScrollUpdateOptimizationsEnabled()) {
              // Issue additional invalidations for the old cull rect updater.
              if (layout_view->Layer()->NeedsCullRectUpdate() ||
                  layout_view->Layer()->DescendantNeedsCullRectUpdate()) {
                layout_view->Layer()
                    ->MarkCompositingContainerChainForNeedsCullRectUpdate();
              }
            }
          }
        }
      },
      // Use post-order to ensure correct flag propagation for nested frames.
      kPostOrder);

  {
    SCOPED_UMA_AND_UKM_TIMER(EnsureUkmAggregator(),
                             LocalFrameUkmAggregator::kPrePaint);

    GetPage()->GetLinkHighlight().UpdateBeforePrePaint();
    PrePaintTreeWalk().WalkTree(*this);
    GetPage()->GetLinkHighlight().UpdateAfterPrePaint();

    frame_->GetPage()->GetValidationMessageClient().UpdatePrePaint();
    ForAllNonThrottledLocalFrameViews([](LocalFrameView& view) {
      view.frame_->UpdateFrameColorOverlayPrePaint();
    });
    if (auto* web_local_frame_impl = WebLocalFrameImpl::FromFrame(frame_))
      web_local_frame_impl->UpdateDevToolsOverlaysPrePaint();
  }

  ForAllNonThrottledLocalFrameViews([](LocalFrameView& frame_view) {
    frame_view.Lifecycle().AdvanceTo(DocumentLifecycle::kPrePaintClean);
  });

  return target_state > DocumentLifecycle::kPrePaintClean;
}

bool LocalFrameView::AnyFrameIsPrintingOrPaintingPreview() {
  bool any_frame_is_printing_or_painting_preview = false;
  ForAllNonThrottledLocalFrameViews(
      [&any_frame_is_printing_or_painting_preview](LocalFrameView& frame_view) {
        if (frame_view.GetFrame().GetDocument()->IsPrintingOrPaintingPreview())
          any_frame_is_printing_or_painting_preview = true;
      });
  return any_frame_is_printing_or_painting_preview;
}

void LocalFrameView::RunPaintLifecyclePhase(PaintBenchmarkMode benchmark_mode) {
  DCHECK(ScriptForbiddenScope::WillBeScriptForbidden());
  DCHECK(LocalFrameTreeAllowsThrottling());
  TRACE_EVENT0("blink,benchmark", "LocalFrameView::RunPaintLifecyclePhase");
  // While printing or capturing a paint preview of a document, the paint walk
  // is done into a special canvas. There is no point doing a normal paint step
  // (or animations update) when in this mode.
  if (AnyFrameIsPrintingOrPaintingPreview())
    return;

  bool needed_update;
  {
    PaintControllerCycleScope cycle_scope(PaintDebugInfoEnabled());
    bool repainted = PaintTree(benchmark_mode, cycle_scope);

    if (paint_artifact_compositor_ &&
        benchmark_mode ==
            PaintBenchmarkMode::kForcePaintArtifactCompositorUpdate) {
      paint_artifact_compositor_->SetNeedsUpdate(
          PaintArtifactCompositorUpdateReason::kLocalFrameViewBenchmarking);
    }
    needed_update = !paint_artifact_compositor_ ||
                    paint_artifact_compositor_->NeedsUpdate();
    PushPaintArtifactToCompositor(repainted);
  }

  size_t total_animations_count = 0;
  ForAllNonThrottledLocalFrameViews(
      [this, &needed_update,
       &total_animations_count](LocalFrameView& frame_view) {
        if (auto* scrollable_area = frame_view.GetScrollableArea())
          scrollable_area->UpdateCompositorScrollAnimations();
        if (const auto* animating_scrollable_areas =
                frame_view.AnimatingScrollableAreas()) {
          for (PaintLayerScrollableArea* area : *animating_scrollable_areas)
            area->UpdateCompositorScrollAnimations();
        }
        frame_view.GetPage()->GetLinkHighlight().UpdateAfterPaint(
            paint_artifact_compositor_.get());
        Document& document = frame_view.GetLayoutView()->GetDocument();
        {
          // Updating animations can notify ready promises which could mutate
          // the DOM. We should delay these until we have finished the lifecycle
          // update. https://crbug.com/1196781
          ScriptForbiddenScope forbid_script;
          document.GetDocumentAnimations().UpdateAnimations(
              DocumentLifecycle::kPaintClean, paint_artifact_compositor_.get(),
              needed_update);
        }
        total_animations_count +=
            document.GetDocumentAnimations().GetAnimationsCount();
      });

  // If this is a throttled local root, then we shouldn't run animation steps
  // below, because the cc animation data structures might not even exist.
  if (frame_->IsLocalRoot() && ShouldThrottleRendering())
    return;

  if (auto* animation_host = GetCompositorAnimationHost()) {
    animation_host->SetAnimationCounts(total_animations_count);
  }

  // Initialize animation properties in the newly created paint property
  // nodes according to the current animation state. This is mainly for
  // the running composited animations which didn't change state during
  // above UpdateAnimations() but associated with new paint property nodes.
  if (needed_update) {
    auto* root_layer = RootCcLayer();
    if (root_layer && root_layer->layer_tree_host()) {
      root_layer->layer_tree_host()->mutator_host()->InitClientAnimationState();
    }
  }

  if (paint_artifact_compositor_)
    paint_artifact_compositor_->ClearPropertyTreeChangedState();

  if (GetPage())
    GetPage()->Animator().ReportFrameAnimations(GetCompositorAnimationHost());
}

bool LocalFrameView::RunAccessibilityLifecyclePhase(
    DocumentLifecycle::LifecycleState target_state) {
  TRACE_EVENT0("blink,benchmark",
               "LocalFrameView::RunAccessibilityLifecyclePhase");

  SCOPED_UMA_AND_UKM_TIMER(EnsureUkmAggregator(),
                           LocalFrameUkmAggregator::kAccessibility);

  // Reduce redundant ancestor chain walking for display lock computations.
  auto display_lock_memoization_scope =
      DisplayLockUtilities::CreateLockCheckMemoizationScope();

  ForAllNonThrottledLocalFrameViews([](LocalFrameView& frame_view) {
    if (AXObjectCache* cache = frame_view.ExistingAXObjectCache()) {
      frame_view.Lifecycle().AdvanceTo(DocumentLifecycle::kInAccessibility);
      cache->ProcessDeferredAccessibilityEvents(
          *frame_view.GetFrame().GetDocument());
      frame_view.Lifecycle().AdvanceTo(DocumentLifecycle::kAccessibilityClean);
    }
  });

  return target_state > DocumentLifecycle::kAccessibilityClean;
}

void LocalFrameView::EnqueueScrollAnchoringAdjustment(
    ScrollableArea* scrollable_area) {
  anchoring_adjustment_queue_.insert(scrollable_area);
}

void LocalFrameView::DequeueScrollAnchoringAdjustment(
    ScrollableArea* scrollable_area) {
  anchoring_adjustment_queue_.erase(scrollable_area);
}

void LocalFrameView::SetNeedsEnqueueScrollEvent(
    PaintLayerScrollableArea* scrollable_area) {
  scroll_event_queue_.insert(scrollable_area);
  GetPage()->Animator().ScheduleVisualUpdate(frame_.Get());
}

void LocalFrameView::PerformScrollAnchoringAdjustments() {
  // Adjust() will cause a scroll which could end up causing a layout and
  // reentering this method. Copy and clear the queue so we don't modify it
  // during iteration.
  AnchoringAdjustmentQueue queue_copy = anchoring_adjustment_queue_;
  anchoring_adjustment_queue_.clear();

  for (const WeakMember<ScrollableArea>& scroller : queue_copy) {
    if (scroller) {
      DCHECK(scroller->GetScrollAnchor());
      scroller->GetScrollAnchor()->Adjust();
    }
  }
}

void LocalFrameView::EnqueueScrollEvents() {
  ForAllNonThrottledLocalFrameViews([](LocalFrameView& frame_view) {
    for (const WeakMember<PaintLayerScrollableArea>& scroller :
         frame_view.scroll_event_queue_) {
      if (scroller)
        scroller->EnqueueScrollEventIfNeeded();
    }
    frame_view.scroll_event_queue_.clear();
  });
}

bool LocalFrameView::PaintTree(PaintBenchmarkMode benchmark_mode,
                               PaintControllerCycleScope& cycle_scope) {
  SCOPED_UMA_AND_UKM_TIMER(EnsureUkmAggregator(),
                           LocalFrameUkmAggregator::kPaint);

  DCHECK(GetFrame().IsLocalRoot());

  absl::optional<MobileFriendlinessChecker::PaintScope> mf_scope;
  if (mobile_friendliness_checker_)
    mf_scope.emplace(*mobile_friendliness_checker_);

  auto* layout_view = GetLayoutView();
  DCHECK(layout_view);

  if (RuntimeEnabledFeatures::ScrollUpdateOptimizationsEnabled())
    CullRectUpdater(*layout_view->Layer()).Update();
  else
    OldCullRectUpdater(*layout_view->Layer()).Update();

  bool debug_info_newly_enabled =
      UpdateLayerDebugInfoEnabled() && PaintDebugInfoEnabled();

  paint_frame_count_++;
  ForAllNonThrottledLocalFrameViews(
      [debug_info_newly_enabled](LocalFrameView& frame_view) {
        frame_view.MarkFirstEligibleToPaint();
        frame_view.Lifecycle().AdvanceTo(DocumentLifecycle::kInPaint);
        // Propagate child frame PaintLayer NeedsRepaint flag into the owner
        // frame.
        if (auto* frame_layout_view = frame_view.GetLayoutView()) {
          if (auto* owner = frame_view.GetFrame().OwnerLayoutObject()) {
            PaintLayer* frame_root_layer = frame_layout_view->Layer();
            DCHECK(frame_root_layer);
            DCHECK(owner->Layer());
            if (frame_root_layer->SelfOrDescendantNeedsRepaint())
              owner->Layer()->SetDescendantNeedsRepaint();
          }
          // If debug info was just enabled, then the paint cache won't have any
          // debug info; we need to force a full repaint to generate it.
          if (debug_info_newly_enabled)
            frame_layout_view->InvalidatePaintForViewAndDescendants();
        }
      },
      // Use post-order to ensure correct flag propagation for nested frames.
      kPostOrder);

  ForAllThrottledLocalFrameViews(
      [](LocalFrameView& frame_view) { frame_view.MarkIneligibleToPaint(); });

  bool repainted = false;
  bool needs_clear_repaint_flags = false;

  // TODO(paint-dev): We should be able to get rid of AddController entirely
  // after non-CAP code is removed. The call to EnsurePaintController() will
  // need to be moved up the call stack.
  EnsurePaintController();
  cycle_scope.AddController(*paint_controller_);

  PaintChunkSubset previous_chunks(paint_controller_->GetPaintArtifactShared());

  PaintController::ScopedBenchmarkMode scoped_benchmark(*paint_controller_,
                                                        benchmark_mode);

  if (paint_controller_->ShouldForcePaintForBenchmark() ||
      GetLayoutView()->Layer()->SelfOrDescendantNeedsRepaint() ||
      visual_viewport_or_overlay_needs_repaint_) {
    GraphicsContext graphics_context(*paint_controller_);

    // Draw the WebXR DOM overlay if present.
    if (PaintLayer* full_screen_layer = GetXROverlayLayer()) {
      PaintLayerPainter(*full_screen_layer).Paint(graphics_context);
    } else {
      PaintFrame(graphics_context);

      GetPage()->GetValidationMessageClient().PaintOverlay(graphics_context);
      ForAllNonThrottledLocalFrameViews(
          [&graphics_context](LocalFrameView& view) {
            view.frame_->PaintFrameColorOverlay(graphics_context);
          });

      // Devtools overlays query the inspected page's paint data so this
      // update needs to be after other paintings.
      if (auto* web_local_frame_impl = WebLocalFrameImpl::FromFrame(frame_))
        web_local_frame_impl->PaintDevToolsOverlays(graphics_context);

      if (frame_->IsMainFrame())
        GetPage()->GetVisualViewport().Paint(graphics_context);
    }

    // Link highlights paint after all other paintings.
    GetPage()->GetLinkHighlight().Paint(graphics_context);

    paint_controller_->CommitNewDisplayItems();

    repainted = true;
    if (paint_artifact_compositor_) {
      paint_artifact_compositor_->SetNeedsFullUpdateAfterPaintIfNeeded(
          previous_chunks, paint_controller_->GetPaintArtifactShared());
    }
  }

  visual_viewport_or_overlay_needs_repaint_ = false;

  needs_clear_repaint_flags |= repainted;
  ForAllNonThrottledLocalFrameViews(
      [needs_clear_repaint_flags](LocalFrameView& frame_view) {
        frame_view.Lifecycle().AdvanceTo(DocumentLifecycle::kPaintClean);
        if (needs_clear_repaint_flags) {
          if (auto* layout_view = frame_view.GetLayoutView())
            layout_view->Layer()->ClearNeedsRepaintRecursively();
        }
        frame_view.GetPaintTimingDetector().NotifyPaintFinished();
      });

  return repainted;
}

cc::Layer* LocalFrameView::RootCcLayer() {
  return paint_artifact_compositor_ ? paint_artifact_compositor_->RootLayer()
                                    : nullptr;
}

const cc::Layer* LocalFrameView::RootCcLayer() const {
  return const_cast<LocalFrameView*>(this)->RootCcLayer();
}

void LocalFrameView::CreatePaintTimelineEvents() {
  if (const cc::Layer* root_layer = paint_artifact_compositor_->RootLayer()) {
    for (const auto& layer : root_layer->children()) {
      if (!layer->update_rect().IsEmpty()) {
        DEVTOOLS_TIMELINE_TRACE_EVENT_INSTANT_WITH_CATEGORIES(
            "devtools.timeline,rail", "Paint", inspector_paint_event::Data,
            &GetFrame(), /*layout_object=*/nullptr,
            GetQuadForTimelinePaintEvent(layer), layer->id());
      }
    }
  }
}

void LocalFrameView::PushPaintArtifactToCompositor(bool repainted) {
  TRACE_EVENT0("blink", "LocalFrameView::pushPaintArtifactToCompositor");
  if (!frame_->GetSettings()->GetAcceleratedCompositingEnabled()) {
    if (paint_artifact_compositor_) {
      paint_artifact_compositor_->WillBeRemovedFromFrame();
      paint_artifact_compositor_ = nullptr;
    }
    return;
  }

  Page* page = GetFrame().GetPage();
  if (!page)
    return;

  if (!paint_artifact_compositor_) {
    paint_artifact_compositor_ = std::make_unique<PaintArtifactCompositor>(
        page->GetScrollingCoordinator()->GetWeakPtr());
    page->GetChromeClient().AttachRootLayer(
        paint_artifact_compositor_->RootLayer(), &GetFrame());
  }

  paint_artifact_compositor_->SetPrefersLCDText(
      !page->GetSettings().GetPreferCompositingToLCDTextEnabled());

  SCOPED_UMA_AND_UKM_TIMER(EnsureUkmAggregator(),
                           LocalFrameUkmAggregator::kCompositingCommit);

  // Skip updating property trees, pushing cc::Layers, and issuing raster
  // invalidations if possible.
  if (!paint_artifact_compositor_->NeedsUpdate()) {
    if (repainted) {
      paint_artifact_compositor_->UpdateRepaintedLayers(
          paint_controller_->GetPaintArtifactShared());
      CreatePaintTimelineEvents();
    }
    // TODO(pdr): Should we clear the property tree state change bits (
    // |PaintArtifactCompositor::ClearPropertyTreeChangedState|)?
    return;
  }

  paint_artifact_compositor_->SetLayerDebugInfoEnabled(
      layer_debug_info_enabled_);

  PaintArtifactCompositor::ViewportProperties viewport_properties;
  if (const auto& viewport = page->GetVisualViewport();
      GetFrame().IsMainFrame() && viewport.IsActiveViewport()) {
    viewport_properties.overscroll_elasticity_effect =
        viewport.GetOverscrollElasticityEffectNode();
    viewport_properties.overscroll_elasticity_transform =
        viewport.GetOverscrollElasticityTransformNode();
    viewport_properties.page_scale = viewport.GetPageScaleNode();

    if (const auto* root_scroller =
            GetPage()->GlobalRootScrollerController().GlobalRootScroller()) {
      if (const auto* layout_object = root_scroller->GetLayoutObject()) {
        if (const auto* paint_properties =
                layout_object->FirstFragment().PaintProperties()) {
          if (paint_properties->Scroll()) {
            viewport_properties.outer_clip = paint_properties->OverflowClip();
            viewport_properties.outer_scroll_translation =
                paint_properties->ScrollTranslation();
            viewport_properties.inner_scroll_translation =
                viewport.GetScrollTranslationNode();
          }
        }
      }
    }
  }

  Vector<const TransformPaintPropertyNode*> scroll_translation_nodes;
  if (base::FeatureList::IsEnabled(::features::kScrollUnification)) {
    ForAllNonThrottledLocalFrameViews(
        [&scroll_translation_nodes](LocalFrameView& frame_view) {
          frame_view.GetUserScrollTranslationNodes(scroll_translation_nodes);
        });
  }

  WTF::Vector<std::unique_ptr<DocumentTransitionRequest>>
      document_transition_requests;
  // TODO(vmpstr): We should make this work for subframes as well.
  AppendDocumentTransitionRequests(document_transition_requests);

  paint_artifact_compositor_->Update(
      paint_controller_->GetPaintArtifactShared(), viewport_properties,
      scroll_translation_nodes, std::move(document_transition_requests));

  CreatePaintTimelineEvents();
}

void LocalFrameView::AppendDocumentTransitionRequests(
    WTF::Vector<std::unique_ptr<DocumentTransitionRequest>>& requests) {
  DCHECK(frame_ && frame_->GetDocument());
  auto* document_transition_supplement =
      DocumentTransitionSupplement::FromIfExists(*frame_->GetDocument());
  if (!document_transition_supplement)
    return;
  auto* document_transition = document_transition_supplement->GetTransition();
  auto pending_request = document_transition->TakePendingRequest();
  if (pending_request)
    requests.push_back(std::move(pending_request));
}

void LocalFrameView::VerifySharedElementsForDocumentTransition() {
  DCHECK(frame_ && frame_->GetDocument());
  auto* document_transition_supplement =
      DocumentTransitionSupplement::FromIfExists(*frame_->GetDocument());
  if (!document_transition_supplement)
    return;

  auto* document_transition = document_transition_supplement->GetTransition();
  document_transition->VerifySharedElements();
}

std::unique_ptr<JSONObject> LocalFrameView::CompositedLayersAsJSON(
    LayerTreeFlags flags) {
  auto* root_frame_view = GetFrame().LocalFrameRoot().View();
  if (root_frame_view->paint_artifact_compositor_)
    return root_frame_view->paint_artifact_compositor_->GetLayersAsJSON(flags);
  return std::make_unique<JSONObject>();
}

void LocalFrameView::UpdateStyleAndLayoutIfNeededRecursive() {
  if (ShouldThrottleRendering() || !frame_->GetDocument()->IsActive())
    return;

  TRACE_EVENT0("blink,benchmark",
               "LocalFrameView::updateStyleAndLayoutIfNeededRecursive");

  UpdateStyleAndLayout();

  // WebView plugins need to update regardless of whether the
  // LayoutEmbeddedObject that owns them needed layout.
  // TODO(schenney): This currently runs the entire lifecycle on plugin
  // WebViews. We should have a way to only run these other Documents to the
  // same lifecycle stage as this frame.
  for (const auto& plugin : plugins_) {
    plugin->UpdateAllLifecyclePhases();
  }
  CheckDoesNotNeedLayout();

  // FIXME: Calling layout() shouldn't trigger script execution or have any
  // observable effects on the frame tree but we're not quite there yet.
  HeapVector<Member<LocalFrameView>> frame_views;
  for (Frame* child = frame_->Tree().FirstChild(); child;
       child = child->Tree().NextSibling()) {
    auto* child_local_frame = DynamicTo<LocalFrame>(child);
    if (!child_local_frame)
      continue;
    if (LocalFrameView* view = child_local_frame->View())
      frame_views.push_back(view);
  }

  for (const auto& frame_view : frame_views)
    frame_view->UpdateStyleAndLayoutIfNeededRecursive();

  // These asserts ensure that parent frames are clean, when child frames
  // finished updating layout and style.
  // TODO(szager): this is the last call to CheckDoesNotNeedLayout during the
  // lifecycle code, but it can happen that NeedsLayout() becomes true after
  // this point, even while the document lifecycle proceeds to kLayoutClean
  // and beyond. Figure out how this happens, and do something sensible.
  CheckDoesNotNeedLayout();
#if DCHECK_IS_ON()
  frame_->GetDocument()->GetLayoutView()->AssertLaidOut();
  frame_->GetDocument()->GetLayoutView()->AssertFragmentTree();
#endif

  if (Lifecycle().GetState() < DocumentLifecycle::kLayoutClean)
    Lifecycle().AdvanceTo(DocumentLifecycle::kLayoutClean);

  // If we're restoring a scroll position from history, that takes precedence
  // over scrolling to the anchor in the URL.
  frame_->GetDocument()->ApplyScrollRestorationLogic();

  // Ensure that we become visually non-empty eventually.
  // TODO(esprehn): This should check isRenderingReady() instead.
  if (GetFrame().GetDocument()->HasFinishedParsing() &&
      !GetFrame().GetDocument()->IsInitialEmptyDocument())
    is_visually_non_empty_ = true;

  GetFrame().Selection().UpdateStyleAndLayoutIfNeeded();
  GetFrame().GetPage()->GetDragCaret().UpdateStyleAndLayoutIfNeeded();

  // If we're running the lifecycle with intent of painting, we need to
  // verify the shared element transitions, since any requests will be
  // propagated to the compositor.
  if (GetFrame().LocalFrameRoot().View()->target_state_ ==
      DocumentLifecycle::kPaintClean) {
    VerifySharedElementsForDocumentTransition();
  }
}

void LocalFrameView::UpdateStyleAndLayout() {
#if DCHECK_IS_ON()
  DCHECK(!is_updating_layout_);
  base::AutoReset<bool> is_updating_layout(&is_updating_layout_, true);
#endif

  if (IsInPerformLayout() || ShouldThrottleRendering() ||
      !frame_->GetDocument()->IsActive() || frame_->IsProvisional() ||
      Lifecycle().LifecyclePostponed()) {
    return;
  }

  VisualViewport& visual_viewport = frame_->GetPage()->GetVisualViewport();
  gfx::SizeF visual_viewport_size(visual_viewport.VisibleWidthCSSPx(),
                                  visual_viewport.VisibleHeightCSSPx());

  bool did_layout = false;
  {
    // Script is allowed during the initial style and layout as we will rerun
    // at least once more if anything was invalidated.
    ScriptForbiddenScope::AllowUserAgentScript allow_script;
    did_layout = UpdateStyleAndLayoutInternal();
  }

  // Second pass: run autosize until it stabilizes
  if (auto_size_info_) {
    while (auto_size_info_->AutoSizeIfNeeded())
      did_layout |= UpdateStyleAndLayoutInternal();
    auto_size_info_->Clear();
  }

  // Third pass: if layout hasn't stabilized, don't update layout viewport size
  // based on content size.
  if (NeedsLayout()) {
    base::AutoReset<bool> suppress(&suppress_adjust_view_size_, true);
    did_layout |= UpdateStyleAndLayoutInternal();
  }

#if DCHECK_IS_ON()
  if (!Lifecycle().LifecyclePostponed() && !ShouldThrottleRendering()) {
    DCHECK(!frame_->GetDocument()->NeedsLayoutTreeUpdate());
    CheckDoesNotNeedLayout();
    DCHECK(layout_subtree_root_list_.IsEmpty());
    if (did_layout)
      GetLayoutView()->AssertSubtreeIsLaidOut();
  }
#endif

  if (did_layout) {
    bool visual_viewport_size_changed = false;
    if (frame_->IsMainFrame()) {
      // Scrollbars changing state can cause a visual viewport size change.
      gfx::SizeF new_viewport_size(visual_viewport.VisibleWidthCSSPx(),
                                   visual_viewport.VisibleHeightCSSPx());
      visual_viewport_size_changed =
          (new_viewport_size != visual_viewport_size);
      DCHECK(!visual_viewport_size_changed ||
             visual_viewport.IsActiveViewport());
    }
    SetNeedsUpdateGeometries();
    PerformPostLayoutTasks(visual_viewport_size_changed);
    GetFrame().GetDocument()->LayoutUpdated();
  }
  UpdateGeometriesIfNeeded();
}

bool LocalFrameView::UpdateStyleAndLayoutInternal() {
  PostStyleUpdateScope post_style_update_scope(*frame_->GetDocument());

  {
    frame_->GetDocument()->UpdateStyleAndLayoutTreeForThisDocument();

    // Update style for all embedded SVG documents underneath this frame, so
    // that intrinsic size computation for any embedded objects has up-to-date
    // information before layout.
    ForAllChildLocalFrameViews([](LocalFrameView& view) {
      Document& document = *view.GetFrame().GetDocument();
      if (document.IsSVGDocument())
        document.UpdateStyleAndLayoutTreeForThisDocument();
    });
  }

  if (NeedsLayout()) {
    SCOPED_UMA_AND_UKM_TIMER(EnsureUkmAggregator(),
                             LocalFrameUkmAggregator::kLayout);
    UpdateLayout();
    return true;
  }
  return false;
}

void LocalFrameView::EnableAutoSizeMode(const gfx::Size& min_size,
                                        const gfx::Size& max_size) {
  if (!auto_size_info_)
    auto_size_info_ = MakeGarbageCollected<FrameViewAutoSizeInfo>(this);

  auto_size_info_->ConfigureAutoSizeMode(min_size, max_size);
  SetLayoutSizeFixedToFrameSize(true);
  SetNeedsLayout();
  ScheduleRelayout();
}

void LocalFrameView::DisableAutoSizeMode() {
  if (!auto_size_info_)
    return;

  SetLayoutSizeFixedToFrameSize(false);
  SetNeedsLayout();
  ScheduleRelayout();

  // Since autosize mode forces the scrollbar mode, change them to being auto.
  GetLayoutView()->SetAutosizeScrollbarModes(
      mojom::blink::ScrollbarMode::kAuto, mojom::blink::ScrollbarMode::kAuto);
  auto_size_info_.Clear();
}

void LocalFrameView::ForceLayoutForPagination(
    const gfx::SizeF& page_size,
    const gfx::SizeF& original_page_size,
    float maximum_shrink_factor) {
  // Dumping externalRepresentation(m_frame->layoutObject()).ascii() is a good
  // trick to see the state of things before and after the layout
  if (LayoutView* layout_view = GetLayoutView()) {
    float page_logical_width = layout_view->StyleRef().IsHorizontalWritingMode()
                                   ? page_size.width()
                                   : page_size.height();
    float page_logical_height =
        layout_view->StyleRef().IsHorizontalWritingMode() ? page_size.height()
                                                          : page_size.width();

    LayoutUnit floored_page_logical_width =
        static_cast<LayoutUnit>(page_logical_width);
    LayoutUnit floored_page_logical_height =
        static_cast<LayoutUnit>(page_logical_height);
    layout_view->SetLogicalWidth(floored_page_logical_width);
    layout_view->SetPageLogicalHeight(floored_page_logical_height);
    layout_view->SetNeedsLayoutAndIntrinsicWidthsRecalcAndFullPaintInvalidation(
        layout_invalidation_reason::kPrintingChanged);
    frame_->GetDocument()->UpdateStyleAndLayout(
        DocumentUpdateReason::kPrinting);

    // If we don't fit in the given page width, we'll lay out again. If we don't
    // fit in the page width when shrunk, we will lay out at maximum shrink and
    // clip extra content.
    // FIXME: We are assuming a shrink-to-fit printing implementation.  A
    // cropping implementation should not do this!
    bool horizontal_writing_mode =
        layout_view->StyleRef().IsHorizontalWritingMode();
    PhysicalRect document_rect(layout_view->DocumentRect());
    LayoutUnit doc_logical_width = horizontal_writing_mode
                                       ? document_rect.Width()
                                       : document_rect.Height();
    if (doc_logical_width > page_logical_width) {
      // ResizePageRectsKeepingRatio would truncate the expected page size,
      // while we want it rounded -- so make sure it's rounded here.
      gfx::SizeF expected_page_size(
          std::min<float>(document_rect.Width().Round(),
                          page_size.width() * maximum_shrink_factor),
          std::min<float>(document_rect.Height().Round(),
                          page_size.height() * maximum_shrink_factor));
      gfx::SizeF max_page_size = frame_->ResizePageRectsKeepingRatio(
          original_page_size, expected_page_size);
      page_logical_width = horizontal_writing_mode ? max_page_size.width()
                                                   : max_page_size.height();
      page_logical_height = horizontal_writing_mode ? max_page_size.height()
                                                    : max_page_size.width();

      floored_page_logical_width = static_cast<LayoutUnit>(page_logical_width);
      floored_page_logical_height =
          static_cast<LayoutUnit>(page_logical_height);
      layout_view->SetLogicalWidth(floored_page_logical_width);
      layout_view->SetPageLogicalHeight(floored_page_logical_height);
      layout_view
          ->SetNeedsLayoutAndIntrinsicWidthsRecalcAndFullPaintInvalidation(
              layout_invalidation_reason::kPrintingChanged);
      frame_->GetDocument()->UpdateStyleAndLayout(
          DocumentUpdateReason::kPrinting);

      WritingModeConverter converter(
          layout_view->StyleRef().GetWritingDirection(),
          PhysicalSize(layout_view->Size()));
      LogicalRect logical_rect =
          converter.ToLogical(layout_view->DocumentRect());
      LayoutUnit clipped_logical_left;
      if (!layout_view->StyleRef().IsLeftToRightDirection()) {
        clipped_logical_left =
            LayoutUnit(logical_rect.InlineEndOffset() - page_logical_width);
      }
      logical_rect.offset.inline_offset = clipped_logical_left;
      logical_rect.size.inline_size = LayoutUnit(page_logical_width);

      AdjustViewSize();
      UpdateStyleAndLayout();
      // This is how we clip in case we overflow again.
      layout_view->ClearLayoutOverflow();
      layout_view->AddLayoutOverflow(
          converter.ToPhysical(logical_rect)
              .ToLayoutFlippedRect(layout_view->StyleRef(),
                                   PhysicalSize(layout_view->Size())));
      return;
    }
  }

  if (TextAutosizer* text_autosizer = frame_->GetDocument()->GetTextAutosizer())
    text_autosizer->UpdatePageInfo();
  AdjustViewSize();
  UpdateStyleAndLayout();
}

gfx::Rect LocalFrameView::RootFrameToDocument(
    const gfx::Rect& rect_in_root_frame) {
  gfx::Point offset = RootFrameToDocument(rect_in_root_frame.origin());
  gfx::Rect local_rect = rect_in_root_frame;
  local_rect.set_origin(offset);
  return local_rect;
}

gfx::Point LocalFrameView::RootFrameToDocument(
    const gfx::Point& point_in_root_frame) {
  return gfx::ToFlooredPoint(
      RootFrameToDocument(gfx::PointF(point_in_root_frame)));
}

gfx::PointF LocalFrameView::RootFrameToDocument(
    const gfx::PointF& point_in_root_frame) {
  ScrollableArea* layout_viewport = LayoutViewport();
  if (!layout_viewport)
    return point_in_root_frame;

  gfx::PointF local_frame = ConvertFromRootFrame(point_in_root_frame);
  return local_frame + layout_viewport->GetScrollOffset();
}

gfx::Rect LocalFrameView::DocumentToFrame(
    const gfx::Rect& rect_in_document) const {
  gfx::Rect rect_in_frame = rect_in_document;
  rect_in_frame.set_origin(DocumentToFrame(rect_in_document.origin()));
  return rect_in_frame;
}

gfx::Point LocalFrameView::DocumentToFrame(
    const gfx::Point& point_in_document) const {
  return gfx::ToFlooredPoint(DocumentToFrame(gfx::PointF(point_in_document)));
}

gfx::PointF LocalFrameView::DocumentToFrame(
    const gfx::PointF& point_in_document) const {
  ScrollableArea* layout_viewport = LayoutViewport();
  if (!layout_viewport)
    return point_in_document;

  return point_in_document - layout_viewport->GetScrollOffset();
}

PhysicalOffset LocalFrameView::DocumentToFrame(
    const PhysicalOffset& offset_in_document) const {
  ScrollableArea* layout_viewport = LayoutViewport();
  if (!layout_viewport)
    return offset_in_document;

  return offset_in_document -
         PhysicalOffset::FromVector2dFRound(layout_viewport->GetScrollOffset());
}

PhysicalRect LocalFrameView::DocumentToFrame(
    const PhysicalRect& rect_in_document) const {
  return PhysicalRect(DocumentToFrame(rect_in_document.offset),
                      rect_in_document.size);
}

gfx::Point LocalFrameView::FrameToDocument(
    const gfx::Point& point_in_frame) const {
  return ToFlooredPoint(FrameToDocument(PhysicalOffset(point_in_frame)));
}

PhysicalOffset LocalFrameView::FrameToDocument(
    const PhysicalOffset& offset_in_frame) const {
  ScrollableArea* layout_viewport = LayoutViewport();
  if (!layout_viewport)
    return offset_in_frame;

  return offset_in_frame +
         PhysicalOffset::FromVector2dFRound(layout_viewport->GetScrollOffset());
}

gfx::Rect LocalFrameView::FrameToDocument(
    const gfx::Rect& rect_in_frame) const {
  return gfx::Rect(FrameToDocument(rect_in_frame.origin()),
                   rect_in_frame.size());
}

PhysicalRect LocalFrameView::FrameToDocument(
    const PhysicalRect& rect_in_frame) const {
  return PhysicalRect(FrameToDocument(rect_in_frame.offset),
                      rect_in_frame.size);
}

gfx::Rect LocalFrameView::ConvertToContainingEmbeddedContentView(
    const gfx::Rect& local_rect) const {
  if (ParentFrameView()) {
    auto* layout_object = GetLayoutEmbeddedContent();
    if (!layout_object)
      return local_rect;

    // Add borders and padding etc.
    gfx::Rect rect = layout_object->BorderBoxFromEmbeddedContent(local_rect);
    return ToPixelSnappedRect(
        layout_object->LocalToAbsoluteRect(PhysicalRect(rect)));
  }

  return local_rect;
}

gfx::Rect LocalFrameView::ConvertFromContainingEmbeddedContentView(
    const gfx::Rect& parent_rect) const {
  if (ParentFrameView()) {
    gfx::Rect local_rect = parent_rect;
    local_rect.Offset(-Location().OffsetFromOrigin());
    return local_rect;
  }
  return parent_rect;
}

PhysicalOffset LocalFrameView::ConvertToContainingEmbeddedContentView(
    const PhysicalOffset& local_offset) const {
  if (ParentFrameView()) {
    auto* layout_object = GetLayoutEmbeddedContent();
    if (!layout_object)
      return local_offset;

    PhysicalOffset point(local_offset);
    // Add borders and padding etc.
    point = layout_object->BorderBoxFromEmbeddedContent(point);
    return layout_object->LocalToAbsolutePoint(point);
  }

  return local_offset;
}

gfx::PointF LocalFrameView::ConvertToContainingEmbeddedContentView(
    const gfx::PointF& local_point) const {
  if (ParentFrameView()) {
    auto* layout_object = GetLayoutEmbeddedContent();
    if (!layout_object)
      return local_point;

    PhysicalOffset point = PhysicalOffset::FromPointFRound(local_point);
    // Add borders and padding etc.
    point = layout_object->BorderBoxFromEmbeddedContent(point);
    return static_cast<gfx::PointF>(layout_object->LocalToAbsolutePoint(point));
  }

  return local_point;
}

PhysicalOffset LocalFrameView::ConvertFromContainingEmbeddedContentView(
    const PhysicalOffset& parent_offset) const {
  return PhysicalOffset::FromPointFRound(
      ConvertFromContainingEmbeddedContentView(gfx::PointF(parent_offset)));
}

gfx::PointF LocalFrameView::ConvertFromContainingEmbeddedContentView(
    const gfx::PointF& parent_point) const {
  if (ParentFrameView()) {
    // Get our layoutObject in the parent view
    auto* layout_object = GetLayoutEmbeddedContent();
    if (!layout_object)
      return parent_point;

    gfx::PointF point = layout_object->AbsoluteToLocalPoint(parent_point);
    // Subtract borders and padding etc.
    point = layout_object->EmbeddedContentFromBorderBox(point);
    return point;
  }

  return parent_point;
}

gfx::Point LocalFrameView::ConvertToContainingEmbeddedContentView(
    const gfx::Point& local_point) const {
  return ToRoundedPoint(
      ConvertToContainingEmbeddedContentView(PhysicalOffset(local_point)));
}

void LocalFrameView::SetTracksRasterInvalidations(
    bool track_raster_invalidations) {
  if (!GetFrame().IsLocalRoot()) {
    GetFrame().LocalFrameRoot().View()->SetTracksRasterInvalidations(
        track_raster_invalidations);
    return;
  }
  if (track_raster_invalidations == is_tracking_raster_invalidations_)
    return;

  // Ensure the document is up-to-date before tracking invalidations.
  UpdateAllLifecyclePhasesForTest();

  is_tracking_raster_invalidations_ = track_raster_invalidations;
  if (paint_artifact_compositor_) {
    paint_artifact_compositor_->SetTracksRasterInvalidations(
        track_raster_invalidations);
  }

  TRACE_EVENT_INSTANT1(TRACE_DISABLED_BY_DEFAULT("blink.invalidation"),
                       "LocalFrameView::setTracksPaintInvalidations",
                       TRACE_EVENT_SCOPE_GLOBAL, "enabled",
                       track_raster_invalidations);
}

void LocalFrameView::ServiceScriptedAnimations(base::TimeTicks start_time) {
  bool can_throttle = CanThrottleRendering();
  // Disallow throttling in case any script needs to do a synchronous
  // lifecycle update in other frames which are throttled.
  DisallowThrottlingScope disallow_throttling(*this);
  Document* document = GetFrame().GetDocument();
  DCHECK(document);
  if (!can_throttle) {
    if (ScrollableArea* scrollable_area = GetScrollableArea()) {
      scrollable_area->ServiceScrollAnimations(
          start_time.since_origin().InSecondsF());
    }
    if (const ScrollableAreaSet* animating_scrollable_areas =
            AnimatingScrollableAreas()) {
      // Iterate over a copy, since ScrollableAreas may deregister
      // themselves during the iteration.
      HeapVector<Member<PaintLayerScrollableArea>>
          animating_scrollable_areas_copy;
      CopyToVector(*animating_scrollable_areas,
                   animating_scrollable_areas_copy);
      for (PaintLayerScrollableArea* scrollable_area :
           animating_scrollable_areas_copy) {
        scrollable_area->ServiceScrollAnimations(
            start_time.since_origin().InSecondsF());
      }
    }
    GetFrame().AnimateSnapFling(start_time);
    if (SVGDocumentExtensions::ServiceSmilOnAnimationFrame(*document))
      GetPage()->Animator().SetHasSmilAnimation();
    SVGDocumentExtensions::ServiceWebAnimationsOnAnimationFrame(*document);
    document->GetDocumentAnimations().UpdateAnimationTimingForAnimationFrame();
  }
  document->ServiceScriptedAnimations(start_time, can_throttle);
}

void LocalFrameView::ScheduleAnimation(base::TimeDelta delay,
                                       base::Location location) {
  TRACE_EVENT("cc", "LocalFrameView::ScheduleAnimation", "frame", GetFrame(),
              "delay", delay, "location", location);
  if (auto* client = GetChromeClient())
    client->ScheduleAnimation(this, delay);
}

void LocalFrameView::AddScrollAnchoringScrollableArea(
    PaintLayerScrollableArea* scrollable_area) {
  DCHECK(scrollable_area);
  if (!scroll_anchoring_scrollable_areas_) {
    scroll_anchoring_scrollable_areas_ =
        MakeGarbageCollected<ScrollableAreaSet>();
  }
  scroll_anchoring_scrollable_areas_->insert(scrollable_area);
}

void LocalFrameView::RemoveScrollAnchoringScrollableArea(
    PaintLayerScrollableArea* scrollable_area) {
  if (scroll_anchoring_scrollable_areas_)
    scroll_anchoring_scrollable_areas_->erase(scrollable_area);
}

void LocalFrameView::AddAnimatingScrollableArea(
    PaintLayerScrollableArea* scrollable_area) {
  DCHECK(scrollable_area);
  if (!animating_scrollable_areas_)
    animating_scrollable_areas_ = MakeGarbageCollected<ScrollableAreaSet>();
  animating_scrollable_areas_->insert(scrollable_area);
}

void LocalFrameView::RemoveAnimatingScrollableArea(
    PaintLayerScrollableArea* scrollable_area) {
  if (!animating_scrollable_areas_)
    return;
  animating_scrollable_areas_->erase(scrollable_area);
}

void LocalFrameView::AddUserScrollableArea(
    PaintLayerScrollableArea* scrollable_area) {
  DCHECK(scrollable_area);
  if (!user_scrollable_areas_)
    user_scrollable_areas_ = MakeGarbageCollected<ScrollableAreaSet>();
  user_scrollable_areas_->insert(scrollable_area);
}

void LocalFrameView::RemoveUserScrollableArea(
    PaintLayerScrollableArea* scrollable_area) {
  if (!user_scrollable_areas_)
    return;
  user_scrollable_areas_->erase(scrollable_area);
}

void LocalFrameView::AttachToLayout() {
  CHECK(!IsAttached());
  if (frame_->GetDocument())
    CHECK_NE(Lifecycle().GetState(), DocumentLifecycle::kStopping);
  SetAttached(true);
  LocalFrameView* parent_view = ParentFrameView();
  CHECK(parent_view);
  if (parent_view->IsVisible())
    SetParentVisible(true);
  UpdateRenderThrottlingStatus(IsHiddenForThrottling(),
                               parent_view->CanThrottleRendering(),
                               IsDisplayLocked());

  // This is to handle a special case: a display:none iframe may have a fully
  // populated layout tree if it contains an <embed>. In that case, we must
  // ensure that the embed's compositing layer is properly reattached.
  // crbug.com/749737 for context.
  if (auto* layout_view = GetLayoutView())
    layout_view->Layer()->SetNeedsCompositingInputsUpdate();

  // We may have updated paint properties in detached frame subtree for
  // printing (see UpdateLifecyclePhasesForPrinting()). The paint properties
  // may change after the frame is attached.
  if (auto* layout_view = GetLayoutView()) {
    layout_view->AddSubtreePaintPropertyUpdateReason(
        SubtreePaintPropertyUpdateReason::kPrinting);
  }
}

void LocalFrameView::DetachFromLayout() {
  CHECK(IsAttached());
  SetParentVisible(false);
  SetAttached(false);

  // We may need update paint properties in detached frame subtree for printing.
  // See UpdateLifecyclePhasesForPrinting().
  if (auto* layout_view = GetLayoutView()) {
    layout_view->AddSubtreePaintPropertyUpdateReason(
        SubtreePaintPropertyUpdateReason::kPrinting);
  }
}

void LocalFrameView::AddPlugin(WebPluginContainerImpl* plugin) {
  DCHECK(!plugins_.Contains(plugin));
  plugins_.insert(plugin);
}

void LocalFrameView::RemovePlugin(WebPluginContainerImpl* plugin) {
  DCHECK(plugins_.Contains(plugin));
  plugins_.erase(plugin);
}

void LocalFrameView::RemoveScrollbar(Scrollbar* scrollbar) {
  DCHECK(scrollbars_.Contains(scrollbar));
  scrollbars_.erase(scrollbar);
}

void LocalFrameView::AddScrollbar(Scrollbar* scrollbar) {
  DCHECK(!scrollbars_.Contains(scrollbar));
  scrollbars_.insert(scrollbar);
}

bool LocalFrameView::VisualViewportSuppliesScrollbars() {
  // On desktop, we always use the layout viewport's scrollbars.
  if (!frame_->GetSettings() || !frame_->GetSettings()->GetViewportEnabled() ||
      !frame_->GetDocument() || !frame_->GetPage())
    return false;

  if (!LayoutViewport())
    return false;

  const TopDocumentRootScrollerController& controller =
      frame_->GetPage()->GlobalRootScrollerController();
  return controller.RootScrollerArea() == LayoutViewport();
}

AXObjectCache* LocalFrameView::ExistingAXObjectCache() const {
  if (GetFrame().GetDocument())
    return GetFrame().GetDocument()->ExistingAXObjectCache();
  return nullptr;
}

void LocalFrameView::SetCursor(const ui::Cursor& cursor) {
  Page* page = GetFrame().GetPage();
  if (!page || frame_->GetEventHandler().IsMousePositionUnknown())
    return;
  LogCursorSizeCounter(&GetFrame(), cursor);
  page->GetChromeClient().SetCursor(cursor, frame_);
}

void LocalFrameView::PropagateFrameRects() {
  TRACE_EVENT0("blink", "LocalFrameView::PropagateFrameRects");
  if (LayoutSizeFixedToFrameSize())
    SetLayoutSizeInternal(Size());

  ForAllChildViewsAndPlugins([](EmbeddedContentView& view) {
    auto* local_frame_view = DynamicTo<LocalFrameView>(view);
    if (!local_frame_view || !local_frame_view->ShouldThrottleRendering()) {
      view.PropagateFrameRects();
    }
  });

  // To limit the number of Mojo communications, only notify the browser when
  // the rect's size changes, not when the position changes. The size needs to
  // be replicated if the iframe goes out-of-process.
  gfx::Size frame_size = FrameRect().size();
  if (!frame_size_ || *frame_size_ != frame_size) {
    frame_size_ = frame_size;
    GetFrame().GetLocalFrameHostRemote().FrameSizeChanged(frame_size);
  }

  // It's possible for changing the frame rect to not generate a layout
  // or any other event tracked by accessibility, we've seen this with
  // Android WebView. Ensure that the root of the accessibility tree is
  // invalidated so that it gets the right bounding rect.
  if (AXObjectCache* cache = ExistingAXObjectCache())
    cache->HandleFrameRectsChanged(*GetFrame().GetDocument());
}

void LocalFrameView::SetLayoutSizeInternal(const gfx::Size& size) {
  if (layout_size_ == size)
    return;
  layout_size_ = size;
  SetNeedsLayout();
  Document* document = GetFrame().GetDocument();
  if (!document || !document->IsActive())
    return;
  document->LayoutViewportWasResized();
  if (frame_->IsMainFrame())
    TextAutosizer::UpdatePageInfoInAllFrames(frame_);
  if (auto* ds_controller = DeferredShapingController::From(*document))
    ds_controller->OnResizeFrame();
}

void LocalFrameView::DidChangeScrollOffset() {
  GetFrame().Client()->DidChangeScrollOffset();
  if (GetFrame().IsOutermostMainFrame()) {
    GetFrame()
        .GetPage()
        ->GetChromeClient()
        .OutermostMainFrameScrollOffsetChanged();
  }
}

ScrollableArea* LocalFrameView::ScrollableAreaWithElementId(
    const CompositorElementId& id) {
  // Check for the layout viewport, which may not be in user_scrollable_areas_
  // if it is styled overflow: hidden.  (Other overflow: hidden elements won't
  // have composited scrolling layers per crbug.com/784053, so we don't have to
  // worry about them.)
  ScrollableArea* viewport = LayoutViewport();
  if (id == viewport->GetScrollElementId())
    return viewport;

  if (user_scrollable_areas_) {
    // This requires iterating over all user-scrollable areas. We may want to
    // store a map of ElementId to ScrollableArea if this is an issue for
    // performance.
    for (ScrollableArea* scrollable_area : *user_scrollable_areas_) {
      if (id == scrollable_area->GetScrollElementId())
        return scrollable_area;
    }
  }
  return nullptr;
}

void LocalFrameView::ScrollRectToVisibleInRemoteParent(
    const PhysicalRect& rect_to_scroll,
    mojom::blink::ScrollIntoViewParamsPtr params) {
  DCHECK(GetFrame().IsLocalRoot());
  DCHECK(!GetFrame().IsOutermostMainFrame());

  // If the scroll doesn't cross origin boundaries then it must already have
  // been blocked for a scroll crossing an embedded frame tree boundary.
  DCHECK(params->cross_origin_boundaries ||
         (!GetFrame().IsMainFrame() || GetFrame().IsOutermostMainFrame()));

  DCHECK(params->cross_origin_boundaries ||
         GetFrame()
             .Tree()
             .Parent()
             ->GetSecurityContext()
             ->GetSecurityOrigin()
             ->CanAccess(GetFrame().GetSecurityContext()->GetSecurityOrigin()));
  PhysicalRect new_rect = ConvertToRootFrame(rect_to_scroll);
  GetFrame().GetLocalFrameHostRemote().ScrollRectToVisibleInParentFrame(
      gfx::RectF(new_rect), std::move(params));
}

void LocalFrameView::NotifyFrameRectsChangedIfNeeded() {
  if (root_layer_did_scroll_) {
    root_layer_did_scroll_ = false;
    PropagateFrameRects();
  }
}

PhysicalOffset LocalFrameView::ViewportToFrame(
    const PhysicalOffset& point_in_viewport) const {
  PhysicalOffset point_in_root_frame = PhysicalOffset::FromPointFRound(
      frame_->GetPage()->GetVisualViewport().ViewportToRootFrame(
          gfx::PointF(point_in_viewport)));
  return ConvertFromRootFrame(point_in_root_frame);
}

gfx::PointF LocalFrameView::ViewportToFrame(
    const gfx::PointF& point_in_viewport) const {
  gfx::PointF point_in_root_frame(
      frame_->GetPage()->GetVisualViewport().ViewportToRootFrame(
          point_in_viewport));
  return ConvertFromRootFrame(point_in_root_frame);
}

gfx::Rect LocalFrameView::ViewportToFrame(
    const gfx::Rect& rect_in_viewport) const {
  gfx::Rect rect_in_root_frame =
      frame_->GetPage()->GetVisualViewport().ViewportToRootFrame(
          rect_in_viewport);
  return ConvertFromRootFrame(rect_in_root_frame);
}

gfx::Point LocalFrameView::ViewportToFrame(
    const gfx::Point& point_in_viewport) const {
  return ToRoundedPoint(ViewportToFrame(PhysicalOffset(point_in_viewport)));
}

gfx::Rect LocalFrameView::FrameToViewport(
    const gfx::Rect& rect_in_frame) const {
  gfx::Rect rect_in_root_frame = ConvertToRootFrame(rect_in_frame);
  return frame_->GetPage()->GetVisualViewport().RootFrameToViewport(
      rect_in_root_frame);
}

gfx::Point LocalFrameView::FrameToViewport(
    const gfx::Point& point_in_frame) const {
  gfx::Point point_in_root_frame = ConvertToRootFrame(point_in_frame);
  return frame_->GetPage()->GetVisualViewport().RootFrameToViewport(
      point_in_root_frame);
}

gfx::Rect LocalFrameView::FrameToScreen(const gfx::Rect& rect) const {
  if (auto* client = GetChromeClient())
    return client->ViewportToScreen(FrameToViewport(rect), this);
  return gfx::Rect();
}

gfx::Point LocalFrameView::SoonToBeRemovedUnscaledViewportToContents(
    const gfx::Point& point_in_viewport) const {
  gfx::Point point_in_root_frame = gfx::ToFlooredPoint(
      frame_->GetPage()->GetVisualViewport().ViewportCSSPixelsToRootFrame(
          gfx::PointF(point_in_viewport)));
  return ConvertFromRootFrame(point_in_root_frame);
}

LocalFrameView::AllowThrottlingScope::AllowThrottlingScope(
    const LocalFrameView& frame_view)
    : value_(&frame_view.GetFrame().LocalFrameRoot().View()->allow_throttling_,
             true) {}

LocalFrameView::DisallowThrottlingScope::DisallowThrottlingScope(
    const LocalFrameView& frame_view)
    : value_(&frame_view.GetFrame().LocalFrameRoot().View()->allow_throttling_,
             false) {}

LocalFrameView::ForceThrottlingScope::ForceThrottlingScope(
    const LocalFrameView& frame_view)
    : allow_scope_(frame_view),
      value_(&frame_view.GetFrame().LocalFrameRoot().View()->force_throttling_,
             true) {}

PaintController& LocalFrameView::EnsurePaintController() {
  if (!paint_controller_)
    paint_controller_ = std::make_unique<PaintController>();
  return *paint_controller_;
}

bool LocalFrameView::CapturePaintPreview(
    GraphicsContext& context,
    const gfx::Vector2d& paint_offset) const {
  absl::optional<base::UnguessableToken> maybe_embedding_token =
      GetFrame().GetEmbeddingToken();

  // Avoid crashing if a local frame doesn't have an embedding token.
  // e.g. it was unloaded or hasn't finished loading (crbug/1103157).
  if (!maybe_embedding_token.has_value())
    return false;

  // Ensure a recording canvas is properly created.
  DrawingRecorder recorder(context, *GetFrame().OwnerLayoutObject(),
                           DisplayItem::kDocumentBackground);
  context.Save();
  context.Translate(paint_offset.x(), paint_offset.y());
  DCHECK(context.Canvas());

  auto* tracker = context.Canvas()->GetPaintPreviewTracker();
  DCHECK(tracker);  // |tracker| must exist or there is a bug upstream.

  // Create a placeholder ID that maps to an embedding token.
  context.Canvas()->recordCustomData(tracker->CreateContentForRemoteFrame(
      FrameRect(), maybe_embedding_token.value()));
  context.Restore();

  // Send a request to the browser to trigger a capture of the frame.
  GetFrame().GetLocalFrameHostRemote().CapturePaintPreviewOfSubframe(
      FrameRect(), tracker->Guid());
  return true;
}

void LocalFrameView::Paint(GraphicsContext& context,
                           PaintFlags paint_flags,
                           const CullRect& cull_rect,
                           const gfx::Vector2d& paint_offset) const {
  const auto* owner_layout_object = GetFrame().OwnerLayoutObject();
  absl::optional<Document::PaintPreviewScope> paint_preview;
  if (owner_layout_object &&
      owner_layout_object->GetDocument().GetPaintPreviewState() !=
          Document::kNotPaintingPreview) {
    paint_preview.emplace(
        *GetFrame().GetDocument(),
        owner_layout_object->GetDocument().GetPaintPreviewState());
    // When capturing a Paint Preview we want to capture scrollable embedded
    // content separately. Paint should stop here and ask the browser to
    // coordinate painting such frames as a separate task.
    if (LayoutViewport()->ScrollsOverflow()) {
      // If capture fails we should fallback to capturing inline if possible.
      if (CapturePaintPreview(context, paint_offset))
        return;
    }
  }

  if (!cull_rect.Rect().Intersects(FrameRect()))
    return;

  // |paint_offset| is not used because paint properties of the contents will
  // ensure the correct location.
  PaintFrame(context, paint_flags);
}

void LocalFrameView::PaintFrame(GraphicsContext& context,
                                PaintFlags paint_flags) const {
  FramePainter(*this).Paint(context, paint_flags);
}

static bool PaintOutsideOfLifecycleIsAllowed(GraphicsContext& context,
                                             const LocalFrameView& frame_view) {
  // A paint outside of lifecycle should not conflict about paint controller
  // caching with the default painting executed during lifecycle update,
  // otherwise the caller should either use a transient paint controller or
  // explicitly skip cache.
  if (context.GetPaintController().IsSkippingCache())
    return true;
  return false;
}

void LocalFrameView::PaintOutsideOfLifecycle(GraphicsContext& context,
                                             const PaintFlags paint_flags,
                                             const CullRect& cull_rect) {
  DCHECK(PaintOutsideOfLifecycleIsAllowed(context, *this));

  SCOPED_UMA_AND_UKM_TIMER(EnsureUkmAggregator(),
                           LocalFrameUkmAggregator::kPaint);

  ForAllNonThrottledLocalFrameViews([](LocalFrameView& frame_view) {
    frame_view.Lifecycle().AdvanceTo(DocumentLifecycle::kInPaint);
  });

  {
    OverriddenCullRectScope force_cull_rect(*GetLayoutView()->Layer(),
                                            cull_rect);
    OverriddenOldCullRectScope force_old_cull_rect(*GetLayoutView()->Layer(),
                                                   cull_rect);
    PaintControllerCycleScope cycle_scope(context.GetPaintController(),
                                          PaintDebugInfoEnabled());
    PaintFrame(context, paint_flags);
  }

  ForAllNonThrottledLocalFrameViews([](LocalFrameView& frame_view) {
    frame_view.Lifecycle().AdvanceTo(DocumentLifecycle::kPaintClean);
  });
}

void LocalFrameView::PaintOutsideOfLifecycleWithThrottlingAllowed(
    GraphicsContext& context,
    const PaintFlags paint_flags,
    const CullRect& cull_rect) {
  AllowThrottlingScope allow_throttling(*this);
  PaintOutsideOfLifecycle(context, paint_flags, cull_rect);
}

void LocalFrameView::PaintForTest(const CullRect& cull_rect) {
  AllowThrottlingScope allow_throttling(*this);
  Lifecycle().AdvanceTo(DocumentLifecycle::kInPaint);
  OverriddenCullRectScope force_cull_rect(*GetLayoutView()->Layer(), cull_rect);
  OverriddenOldCullRectScope force_old_cull_rect(*GetLayoutView()->Layer(),
                                                 cull_rect);
  PaintController& paint_controller = EnsurePaintController();
  if (GetLayoutView()->Layer()->SelfOrDescendantNeedsRepaint()) {
    PaintControllerCycleScope cycle_scope(paint_controller,
                                          PaintDebugInfoEnabled());
    GraphicsContext graphics_context(paint_controller);
    PaintFrame(graphics_context);
    paint_controller.CommitNewDisplayItems();
  }
  Lifecycle().AdvanceTo(DocumentLifecycle::kPaintClean);
}

sk_sp<PaintRecord> LocalFrameView::GetPaintRecord() const {
  DCHECK_EQ(DocumentLifecycle::kPaintClean, Lifecycle().GetState());
  DCHECK(frame_->IsLocalRoot());
  DCHECK(paint_controller_);
  return paint_controller_->GetPaintArtifact().GetPaintRecord(
      PropertyTreeState::Root());
}

gfx::Rect LocalFrameView::ConvertToRootFrame(
    const gfx::Rect& local_rect) const {
  if (LocalFrameView* parent = ParentFrameView()) {
    gfx::Rect parent_rect = ConvertToContainingEmbeddedContentView(local_rect);
    return parent->ConvertToRootFrame(parent_rect);
  }
  return local_rect;
}

gfx::Point LocalFrameView::ConvertToRootFrame(
    const gfx::Point& local_point) const {
  return ToRoundedPoint(ConvertToRootFrame(PhysicalOffset(local_point)));
}

PhysicalOffset LocalFrameView::ConvertToRootFrame(
    const PhysicalOffset& local_offset) const {
  if (LocalFrameView* parent = ParentFrameView()) {
    PhysicalOffset parent_offset =
        ConvertToContainingEmbeddedContentView(local_offset);
    return parent->ConvertToRootFrame(parent_offset);
  }
  return local_offset;
}

gfx::PointF LocalFrameView::ConvertToRootFrame(
    const gfx::PointF& local_point) const {
  if (LocalFrameView* parent = ParentFrameView()) {
    gfx::PointF parent_point =
        ConvertToContainingEmbeddedContentView(local_point);
    return parent->ConvertToRootFrame(parent_point);
  }
  return local_point;
}

PhysicalRect LocalFrameView::ConvertToRootFrame(
    const PhysicalRect& local_rect) const {
  if (LocalFrameView* parent = ParentFrameView()) {
    PhysicalOffset parent_offset =
        ConvertToContainingEmbeddedContentView(local_rect.offset);
    PhysicalRect parent_rect(parent_offset, local_rect.size);
    return parent->ConvertToRootFrame(parent_rect);
  }
  return local_rect;
}

gfx::Rect LocalFrameView::ConvertFromRootFrame(
    const gfx::Rect& rect_in_root_frame) const {
  if (LocalFrameView* parent = ParentFrameView()) {
    gfx::Rect parent_rect = parent->ConvertFromRootFrame(rect_in_root_frame);
    return ConvertFromContainingEmbeddedContentView(parent_rect);
  }
  return rect_in_root_frame;
}

gfx::Point LocalFrameView::ConvertFromRootFrame(
    const gfx::Point& point_in_root_frame) const {
  return ToRoundedPoint(
      ConvertFromRootFrame(PhysicalOffset(point_in_root_frame)));
}

PhysicalOffset LocalFrameView::ConvertFromRootFrame(
    const PhysicalOffset& offset_in_root_frame) const {
  if (LocalFrameView* parent = ParentFrameView()) {
    PhysicalOffset parent_point =
        parent->ConvertFromRootFrame(offset_in_root_frame);
    return ConvertFromContainingEmbeddedContentView(parent_point);
  }
  return offset_in_root_frame;
}

gfx::PointF LocalFrameView::ConvertFromRootFrame(
    const gfx::PointF& point_in_root_frame) const {
  if (LocalFrameView* parent = ParentFrameView()) {
    gfx::PointF parent_point =
        parent->ConvertFromRootFrame(point_in_root_frame);
    return ConvertFromContainingEmbeddedContentView(parent_point);
  }
  return point_in_root_frame;
}

void LocalFrameView::ParentVisibleChanged() {
  if (!IsSelfVisible())
    return;

  bool visible = IsParentVisible();
  ForAllChildViewsAndPlugins(
      [visible](EmbeddedContentView& embedded_content_view) {
        embedded_content_view.SetParentVisible(visible);
      });
}

void LocalFrameView::SelfVisibleChanged() {
  // FrameView visibility affects PLC::CanBeComposited, which in turn affects
  // compositing inputs.
  if (LayoutView* view = GetLayoutView())
    view->Layer()->SetNeedsCompositingInputsUpdate();
}

void LocalFrameView::Show() {
  if (!IsSelfVisible()) {
    SetSelfVisible(true);
    if (IsParentVisible()) {
      ForAllChildViewsAndPlugins(
          [](EmbeddedContentView& embedded_content_view) {
            embedded_content_view.SetParentVisible(true);
          });
    }
  }
}

void LocalFrameView::Hide() {
  if (IsSelfVisible()) {
    if (IsParentVisible()) {
      ForAllChildViewsAndPlugins(
          [](EmbeddedContentView& embedded_content_view) {
            embedded_content_view.SetParentVisible(false);
          });
    }
    SetSelfVisible(false);
  }
}

int LocalFrameView::ViewportWidth() const {
  int viewport_width = GetLayoutSize().width();
  return AdjustForAbsoluteZoom::AdjustInt(viewport_width, GetLayoutView());
}

int LocalFrameView::ViewportHeight() const {
  int viewport_height = GetLayoutSize().height();
  return AdjustForAbsoluteZoom::AdjustInt(viewport_height, GetLayoutView());
}

ScrollableArea* LocalFrameView::GetScrollableArea() {
  if (viewport_scrollable_area_)
    return viewport_scrollable_area_.Get();

  return LayoutViewport();
}

PaintLayerScrollableArea* LocalFrameView::LayoutViewport() const {
  auto* layout_view = GetLayoutView();
  return layout_view ? layout_view->GetScrollableArea() : nullptr;
}

RootFrameViewport* LocalFrameView::GetRootFrameViewport() {
  return viewport_scrollable_area_.Get();
}

void LocalFrameView::CollectAnnotatedRegions(
    LayoutObject& layout_object,
    Vector<AnnotatedRegionValue>& regions) const {
  // LayoutTexts don't have their own style, they just use their parent's style,
  // so we don't want to include them.
  if (layout_object.IsText())
    return;

  layout_object.AddAnnotatedRegions(regions);
  for (LayoutObject* curr = layout_object.SlowFirstChild(); curr;
       curr = curr->NextSibling())
    CollectAnnotatedRegions(*curr, regions);
}

bool LocalFrameView::UpdateViewportIntersectionsForSubtree(
    unsigned parent_flags,
    absl::optional<base::TimeTicks>& monotonic_time) {
  // TODO(dcheng): Since LocalFrameView tree updates are deferred, FrameViews
  // might still be in the LocalFrameView hierarchy even though the associated
  // Document is already detached. Investigate if this check and a similar check
  // in lifecycle updates are still needed when there are no more deferred
  // LocalFrameView updates: https://crbug.com/561683
  if (!GetFrame().GetDocument()->IsActive())
    return false;

  unsigned flags = GetIntersectionObservationFlags(parent_flags);
  bool needs_occlusion_tracking = false;

  if (!NeedsLayout() || IsDisplayLocked()) {
    // Notify javascript IntersectionObservers
    if (IntersectionObserverController* controller =
            GetFrame().GetDocument()->GetIntersectionObserverController()) {
      needs_occlusion_tracking |= controller->ComputeIntersections(
          flags, EnsureUkmAggregator(), monotonic_time);
    }
    intersection_observation_state_ = kNotNeeded;
  }

  {
    SCOPED_UMA_AND_UKM_TIMER(
        EnsureUkmAggregator(),
        LocalFrameUkmAggregator::kUpdateViewportIntersection);
    UpdateViewportIntersection(flags, needs_occlusion_tracking);
  }

  for (Frame* child = frame_->Tree().FirstChild(); child;
       child = child->Tree().NextSibling()) {
    needs_occlusion_tracking |=
        child->View()->UpdateViewportIntersectionsForSubtree(flags,
                                                             monotonic_time);
  }

  if (DocumentPortals* portals = DocumentPortals::Get(*frame_->GetDocument())) {
    for (PortalContents* portal : portals->GetPortals()) {
      if (Frame* frame = portal->GetFrame()) {
        needs_occlusion_tracking |=
            frame->View()->UpdateViewportIntersectionsForSubtree(
                flags, monotonic_time);
      }
    }
  }

  if (DocumentFencedFrames* fenced_frames =
          DocumentFencedFrames::Get(*frame_->GetDocument())) {
    for (HTMLFencedFrameElement* fenced_frame :
         fenced_frames->GetFencedFrames()) {
      if (Frame* frame = fenced_frame->ContentFrame()) {
        needs_occlusion_tracking |=
            frame->View()->UpdateViewportIntersectionsForSubtree(
                flags, monotonic_time);
      }
    }
  }
  return needs_occlusion_tracking;
}

void LocalFrameView::DeliverSynchronousIntersectionObservations() {
  if (IntersectionObserverController* controller =
          GetFrame().GetDocument()->GetIntersectionObserverController()) {
    controller->DeliverNotifications(
        IntersectionObserver::kDeliverDuringPostLifecycleSteps);
  }
  ForAllChildLocalFrameViews([](LocalFrameView& frame_view) {
    frame_view.DeliverSynchronousIntersectionObservations();
  });
}

void LocalFrameView::CrossOriginToNearestMainFrameChanged() {
  // If any of these conditions hold, then a change in cross-origin status does
  // not affect throttling.
  if (lifecycle_updates_throttled_ || IsSubtreeThrottled() ||
      IsDisplayLocked() || !IsHiddenForThrottling()) {
    return;
  }
  RenderThrottlingStatusChanged();
  // Immediately propagate changes to children.
  UpdateRenderThrottlingStatus(IsHiddenForThrottling(), IsSubtreeThrottled(),
                               IsDisplayLocked(), true);
}

void LocalFrameView::CrossOriginToParentFrameChanged() {
  if (LayoutView* layout_view = GetLayoutView()) {
    if (PaintLayer* root_layer = layout_view->Layer())
      root_layer->SetNeedsCompositingInputsUpdate();
  }
}

void LocalFrameView::VisibilityForThrottlingChanged() {
  if (FrameScheduler* frame_scheduler = frame_->GetFrameScheduler()) {
    // TODO(szager): Per crbug.com/994443, maybe this should be:
    //   SetFrameVisible(IsHiddenForThrottling() || IsSubtreeThrottled());
    frame_scheduler->SetFrameVisible(!IsHiddenForThrottling());
  }
}

void LocalFrameView::VisibilityChanged(
    blink::mojom::FrameVisibility visibility) {
  frame_->GetLocalFrameHostRemote().VisibilityChanged(visibility);
}

void LocalFrameView::RenderThrottlingStatusChanged() {
  TRACE_EVENT0("blink", "LocalFrameView::RenderThrottlingStatusChanged");
  DCHECK(!IsInPerformLayout());
  DCHECK(!frame_->GetDocument() || !frame_->GetDocument()->InStyleRecalc());

  // When a frame is throttled, we delete its previous painted output, so it
  // will need to be repainted, even if nothing else has changed.
  if (LayoutView* layout_view = GetLayoutView())
    layout_view->Layer()->SetNeedsRepaint();
  // The painted output of the frame may be included in a cached subsequence
  // associated with the embedding document, so invalidate the owner.
  if (auto* owner = GetFrame().OwnerLayoutObject()) {
    if (PaintLayer* owner_layer = owner->Layer())
      owner_layer->SetNeedsRepaint();
  }

  if (!CanThrottleRendering()) {
    // Start ticking animation frames again if necessary.
    if (GetPage())
      GetPage()->Animator().ScheduleVisualUpdate(frame_.Get());
    // Ensure we'll recompute viewport intersection for the frame subtree during
    // the scheduled visual update.
    SetIntersectionObservationState(kRequired);
  } else if (GetFrame().IsLocalRoot()) {
    // By this point, every frame in the local frame tree has become throttled,
    // so painting the tree should just clear the previous painted output.
    SCOPED_BLINK_UMA_HISTOGRAM_TIMER_HIGHRES(
        "Blink.RenderThrottling.PaintUpdateTime");
    DCHECK(!IsUpdatingLifecycle());
    ForceThrottlingScope force_throttling(*this);
    // TODO(https://crbug.com/1196853): Switch to ScriptForbiddenScope once
    // failures are fixed.
    BlinkLifecycleScopeWillBeScriptForbidden forbid_script;
    RunPaintLifecyclePhase(PaintBenchmarkMode::kNormal);
  }

#if DCHECK_IS_ON()
  // Make sure we never have an unthrottled frame inside a throttled one.
  LocalFrameView* parent = ParentFrameView();
  while (parent) {
    DCHECK(CanThrottleRendering() || !parent->CanThrottleRendering());
    parent = parent->ParentFrameView();
  }
#endif
}

void LocalFrameView::SetIntersectionObservationState(
    IntersectionObservationState state) {
  if (intersection_observation_state_ >= state)
    return;
  intersection_observation_state_ = state;

  // If an intersection observation is required, force all ancestors to update.
  // Otherwise, an update could stop at a throttled frame before reaching this.
  if (state == kRequired) {
    Frame* parent_frame = frame_->Tree().Parent();
    if (auto* parent_local_frame = DynamicTo<LocalFrame>(parent_frame)) {
      if (parent_local_frame->View())
        parent_local_frame->View()->SetIntersectionObservationState(kRequired);
    }
  }
}

void LocalFrameView::SetVisualViewportOrOverlayNeedsRepaint() {
  LocalFrameView* root = GetFrame().LocalFrameRoot().View();
  DCHECK(root);
  root->visual_viewport_or_overlay_needs_repaint_ = true;
}

bool LocalFrameView::VisualViewportOrOverlayNeedsRepaintForTesting() const {
  DCHECK(GetFrame().IsLocalRoot());
  return visual_viewport_or_overlay_needs_repaint_;
}

void LocalFrameView::SetPaintArtifactCompositorNeedsUpdate(
    PaintArtifactCompositorUpdateReason reason) {
  LocalFrameView* root = GetFrame().LocalFrameRoot().View();
  if (root && root->paint_artifact_compositor_)
    root->paint_artifact_compositor_->SetNeedsUpdate(reason);
}

PaintArtifactCompositor* LocalFrameView::GetPaintArtifactCompositor() const {
  LocalFrameView* root = GetFrame().LocalFrameRoot().View();
  return root ? root->paint_artifact_compositor_.get() : nullptr;
}

unsigned LocalFrameView::GetIntersectionObservationFlags(
    unsigned parent_flags) const {
  unsigned flags = 0;

  const LocalFrame& target_frame = GetFrame();
  const Frame& root_frame = target_frame.Tree().Top();
  if (&root_frame == &target_frame ||
      target_frame.GetSecurityContext()->GetSecurityOrigin()->CanAccess(
          root_frame.GetSecurityContext()->GetSecurityOrigin())) {
    flags |= IntersectionObservation::kReportImplicitRootBounds;
  }

  if (!target_frame.IsLocalRoot() && !target_frame.OwnerLayoutObject())
    flags |= IntersectionObservation::kAncestorFrameIsDetachedFromLayout;

  // Observers with explicit roots only need to be checked on the same frame,
  // since in this case target and root must be in the same document.
  if (intersection_observation_state_ != kNotNeeded) {
    flags |= (IntersectionObservation::kExplicitRootObserversNeedUpdate |
              IntersectionObservation::kImplicitRootObserversNeedUpdate);
  }

  // For observers with implicit roots, we need to check state on the whole
  // local frame tree, as passed down from the parent.
  flags |= (parent_flags &
            IntersectionObservation::kImplicitRootObserversNeedUpdate);

  // The kIgnoreDelay parameter is used to force computation in an OOPIF which
  // is hidden in the parent document, thus not running lifecycle updates. It
  // applies to the entire frame tree.
  flags |= (parent_flags & IntersectionObservation::kIgnoreDelay);

  return flags;
}

bool LocalFrameView::ShouldThrottleRendering() const {
  if (LocalFrameTreeForcesThrottling())
    return true;
  bool throttled_for_global_reasons = LocalFrameTreeAllowsThrottling() &&
                                      CanThrottleRendering() &&
                                      frame_->GetDocument();
  if (!throttled_for_global_reasons)
    return false;

  // If we're currently running a lifecycle update, and we are required to run
  // the IntersectionObserver steps at the end of the update, then there are two
  // courses of action, depending on whether this frame is display locked by its
  // parent frame:
  //
  //   - If it is NOT display locked, then we suppress throttling to force the
  // lifecycle update to proceed up to the state required to run
  // IntersectionObserver.
  //
  //   - If it IS display locked, then we still need IntersectionObserver to
  // run; but the display lock status will short-circuit the
  // IntersectionObserver algorithm and create degenerate "not intersecting"
  // notifications. Hence, we don't need to force lifecycle phases to run,
  // because IntersectionObserver will not need access to up-to-date
  // geometry. So there is no point in suppressing throttling here.
  auto* local_frame_root_view = GetFrame().LocalFrameRoot().View();
  if (local_frame_root_view->IsUpdatingLifecycle() &&
      intersection_observation_state_ == kRequired && !IsDisplayLocked()) {
    return Lifecycle().GetState() >= DocumentLifecycle::kPrePaintClean;
  }

  return true;
}

bool LocalFrameView::ShouldThrottleRenderingForTest() const {
  AllowThrottlingScope allow_throttling(*this);
  return ShouldThrottleRendering();
}

bool LocalFrameView::CanThrottleRendering() const {
  if (lifecycle_updates_throttled_ || IsSubtreeThrottled() ||
      IsDisplayLocked()) {
    return true;
  }
  // We only throttle hidden cross-origin frames. This is to avoid a situation
  // where an ancestor frame directly depends on the pipeline timing of a
  // descendant and breaks as a result of throttling. The rationale is that
  // cross-origin frames must already communicate with asynchronous messages,
  // so they should be able to tolerate some delay in receiving replies from a
  // throttled peer.
  return IsHiddenForThrottling() && frame_->IsCrossOriginToNearestMainFrame();
}

void LocalFrameView::UpdateRenderThrottlingStatus(bool hidden_for_throttling,
                                                  bool subtree_throttled,
                                                  bool display_locked,
                                                  bool recurse) {
  bool was_throttled = CanThrottleRendering();
  FrameView::UpdateRenderThrottlingStatus(
      hidden_for_throttling, subtree_throttled, display_locked, recurse);
  if (was_throttled != CanThrottleRendering())
    RenderThrottlingStatusChanged();
}

void LocalFrameView::BeginLifecycleUpdates() {
  lifecycle_updates_throttled_ = false;

  LayoutView* layout_view = GetLayoutView();
  bool layout_view_is_empty = layout_view && !layout_view->FirstChild();
  if (layout_view_is_empty && !DidFirstLayout() && !NeedsLayout()) {
    // Make sure a display:none iframe gets an initial layout pass.
    layout_view->SetNeedsLayout(layout_invalidation_reason::kAddedToLayout,
                                kMarkOnlyThis);
  }

  ScheduleAnimation();
  SetIntersectionObservationState(kRequired);

  // Do not report paint timing for the initially empty document.
  if (GetFrame().GetDocument()->IsInitialEmptyDocument())
    MarkIneligibleToPaint();

  // Non-main-frame lifecycle and commit deferral are controlled by their
  // main frame.
  if (!GetFrame().IsMainFrame())
    return;

  ChromeClient& chrome_client = GetFrame().GetPage()->GetChromeClient();

  // Determine if we want to defer commits to the compositor once lifecycle
  // updates start. Doing so allows us to update the page lifecycle but not
  // present the results to screen until we see first contentful paint is
  // available or until a timer expires.
  // This is enabled only when the document loading is regular HTML served
  // over HTTP/HTTPs. And only defer commits once. This method gets called
  // multiple times, and we do not want to defer a second time if we have
  // already done so once and resumed commits already.
  if (WillDoPaintHoldingForFCP()) {
    have_deferred_main_frame_commits_ = true;
    chrome_client.StartDeferringCommits(
        GetFrame(), base::Milliseconds(kCommitDelayDefaultInMs),
        cc::PaintHoldingReason::kFirstContentfulPaint);
  }

  chrome_client.BeginLifecycleUpdates(GetFrame());
}

bool LocalFrameView::WillDoPaintHoldingForFCP() const {
  Document* document = GetFrame().GetDocument();
  return document && document->DeferredCompositorCommitIsAllowed() &&
         !have_deferred_main_frame_commits_ && GetFrame().IsMainFrame();
}

MainThreadScrollingReasons LocalFrameView::MainThreadScrollingReasonsPerFrame()
    const {
  MainThreadScrollingReasons reasons =
      static_cast<MainThreadScrollingReasons>(0);

  if (ShouldThrottleRendering())
    return reasons;

  if (RequiresMainThreadScrollingForBackgroundAttachmentFixed()) {
    reasons |=
        cc::MainThreadScrollingReason::kHasBackgroundAttachmentFixedObjects;
  }
  return reasons;
}

MainThreadScrollingReasons LocalFrameView::GetMainThreadScrollingReasons()
    const {
  MainThreadScrollingReasons reasons =
      static_cast<MainThreadScrollingReasons>(0);

  if (!GetPage()->GetSettings().GetThreadedScrollingEnabled())
    reasons |= cc::MainThreadScrollingReason::kThreadedScrollingDisabled;

  if (!GetPage()->MainFrame()->IsLocalFrame())
    return reasons;

  // TODO(alexmos,kenrb): For OOPIF, local roots that are different from
  // the main frame can't be used in the calculation, since they use
  // different compositors with unrelated state, which breaks some of the
  // calculations below.
  if (&frame_->LocalFrameRoot() != GetPage()->MainFrame())
    return reasons;

  // Walk the tree to the root. Use the gathered reasons to determine
  // whether the target frame should be scrolled on main thread regardless
  // other subframes on the same page.
  for (Frame* frame = frame_; frame; frame = frame->Tree().Parent()) {
    auto* local_frame = DynamicTo<LocalFrame>(frame);
    if (!local_frame)
      continue;
    reasons |= local_frame->View()->MainThreadScrollingReasonsPerFrame();
  }

  DCHECK(
      !cc::MainThreadScrollingReason::HasNonCompositedScrollReasons(reasons));
  return reasons;
}

String LocalFrameView::MainThreadScrollingReasonsAsText() {
  MainThreadScrollingReasons reasons = 0;
  DCHECK(Lifecycle().GetState() >= DocumentLifecycle::kPrePaintClean);
  const auto* properties = GetLayoutView()->FirstFragment().PaintProperties();
  if (properties && properties->Scroll())
    reasons = properties->Scroll()->GetMainThreadScrollingReasons();
  return String(cc::MainThreadScrollingReason::AsText(reasons).c_str());
}

bool LocalFrameView::MapToVisualRectInRemoteRootFrame(
    PhysicalRect& rect,
    bool apply_overflow_clip) {
  DCHECK(frame_->IsLocalRoot());
  // This is the top-level frame, so no mapping necessary.
  if (frame_->IsOutermostMainFrame())
    return true;
  bool result = rect.InclusiveIntersect(PhysicalRect(
      apply_overflow_clip ? frame_->RemoteViewportIntersection()
                          : frame_->RemoteMainFrameIntersection()));
  if (result) {
    if (LayoutView* layout_view = GetLayoutView()) {
      rect = layout_view->LocalToAncestorRect(
          rect, nullptr,
          kTraverseDocumentBoundaries | kApplyRemoteMainFrameTransform);
    }
  }
  return result;
}

void LocalFrameView::MapLocalToRemoteMainFrame(
    TransformState& transform_state) {
  DCHECK(frame_->IsLocalRoot());
  // This is the top-level frame, so no mapping necessary.
  if (frame_->IsOutermostMainFrame())
    return;
  transform_state.ApplyTransform(
      TransformationMatrix(GetFrame().RemoteMainFrameTransform()),
      TransformState::kAccumulateTransform);
}

LayoutUnit LocalFrameView::CaretWidth() const {
  return LayoutUnit(std::max<float>(
      1.0f, GetChromeClient()->WindowToViewportScalar(&GetFrame(), 1.0f)));
}

void LocalFrameView::DidChangeMobileFriendliness(
    const blink::MobileFriendliness& mf) {
  GetFrame().Client()->DidChangeMobileFriendliness(mf);
}

LocalFrameUkmAggregator& LocalFrameView::EnsureUkmAggregator() {
  DCHECK(frame_->IsLocalRoot() || !ukm_aggregator_);
  LocalFrameView* local_root = frame_->LocalFrameRoot().View();
  if (!local_root->ukm_aggregator_) {
    local_root->ukm_aggregator_ = base::MakeRefCounted<LocalFrameUkmAggregator>(
        local_root->frame_->GetDocument()->UkmSourceID(),
        local_root->frame_->GetDocument()->UkmRecorder());
    if (base::FeatureList::IsEnabled(
            features::kThrottleIntersectionObserverUMA)) {
      local_root->ukm_aggregator_->SetIntersectionObserverSamplePeriod(10);
    }
  }
  return *local_root->ukm_aggregator_;
}

void LocalFrameView::ResetUkmAggregatorForTesting() {
  ukm_aggregator_.reset();
}

void LocalFrameView::OnFirstContentfulPaint() {
  if (frame_->IsMainFrame()) {
    // Restart commits that may have been deferred.
    GetPage()->GetChromeClient().StopDeferringCommits(
        *frame_, cc::PaintHoldingCommitTrigger::kFirstContentfulPaint);
    if (frame_->GetDocument()->ShouldMarkFontPerformance())
      FontPerformance::MarkFirstContentfulPaint();
  }

  if (frame_->IsLocalRoot())
    EnsureUkmAggregator().DidReachFirstContentfulPaint();
}

void LocalFrameView::RegisterForLifecycleNotifications(
    LifecycleNotificationObserver* observer) {
  lifecycle_observers_.insert(observer);
}

void LocalFrameView::UnregisterFromLifecycleNotifications(
    LifecycleNotificationObserver* observer) {
  lifecycle_observers_.erase(observer);
}

void LocalFrameView::EnqueueStartOfLifecycleTask(base::OnceClosure closure) {
  start_of_lifecycle_tasks_.push_back(std::move(closure));
}

void LocalFrameView::NotifyVideoIsDominantVisibleStatus(
    HTMLVideoElement* element,
    bool is_dominant) {
  if (is_dominant) {
    fullscreen_video_elements_.insert(element);
    return;
  }

  fullscreen_video_elements_.erase(element);
}

bool LocalFrameView::HasDominantVideoElement() const {
  return !fullscreen_video_elements_.IsEmpty();
}

#if DCHECK_IS_ON()
LocalFrameView::DisallowLayoutInvalidationScope::
    DisallowLayoutInvalidationScope(LocalFrameView* view)
    : local_frame_view_(view) {
  local_frame_view_->allows_layout_invalidation_after_layout_clean_ = false;
  local_frame_view_->ForAllChildLocalFrameViews([](LocalFrameView& frame_view) {
    if (!frame_view.ShouldThrottleRendering())
      frame_view.CheckDoesNotNeedLayout();
    frame_view.allows_layout_invalidation_after_layout_clean_ = false;
  });
}

LocalFrameView::DisallowLayoutInvalidationScope::
    ~DisallowLayoutInvalidationScope() {
  local_frame_view_->allows_layout_invalidation_after_layout_clean_ = true;
  local_frame_view_->ForAllChildLocalFrameViews([](LocalFrameView& frame_view) {
    if (!frame_view.ShouldThrottleRendering())
      frame_view.CheckDoesNotNeedLayout();
    frame_view.allows_layout_invalidation_after_layout_clean_ = true;
  });
}

#endif

bool LocalFrameView::UpdateLayerDebugInfoEnabled() {
  DCHECK(frame_->IsLocalRoot());
#if DCHECK_IS_ON()
  DCHECK(layer_debug_info_enabled_);
#else
  bool should_enable =
      cc::frame_viewer_instrumentation::IsTracingLayerTreeSnapshots() ||
      WebTestSupport::IsRunningWebTest() ||
      CoreProbeSink::HasAgentsGlobal(CoreProbeSink::kInspectorLayerTreeAgent);
  if (should_enable != layer_debug_info_enabled_) {
    layer_debug_info_enabled_ = should_enable;
    SetPaintArtifactCompositorNeedsUpdate(
        PaintArtifactCompositorUpdateReason::
            kLocalFrameViewUpdateLayerDebugInfo);
    return true;
  }
#endif
  return false;
}

OverlayInterstitialAdDetector&
LocalFrameView::EnsureOverlayInterstitialAdDetector() {
  if (!overlay_interstitial_ad_detector_) {
    overlay_interstitial_ad_detector_ =
        std::make_unique<OverlayInterstitialAdDetector>();
  }
  return *overlay_interstitial_ad_detector_.get();
}

void LocalFrameView::GetUserScrollTranslationNodes(
    Vector<const TransformPaintPropertyNode*>& scroll_translation_nodes) {
  const auto* scrollable_areas = UserScrollableAreas();
  if (!scrollable_areas)
    return;

  for (const auto& area : *scrollable_areas) {
    const auto* paint_properties =
        area->GetLayoutBox()->FirstFragment().PaintProperties();
    if (paint_properties && paint_properties->Scroll()) {
      scroll_translation_nodes.push_back(paint_properties->ScrollTranslation());
    }
  }
}

StickyAdDetector& LocalFrameView::EnsureStickyAdDetector() {
  if (!sticky_ad_detector_) {
    sticky_ad_detector_ = std::make_unique<StickyAdDetector>();
  }
  return *sticky_ad_detector_.get();
}

static PaintLayer* GetXrOverlayLayer(Document& document) {
  // immersive-ar DOM overlay mode is very similar to fullscreen video, using
  // the AR camera image instead of a video element as a background that's
  // separately composited in the browser. The fullscreened DOM content is shown
  // on top of that, same as HTML video controls.
  if (!document.IsXrOverlay())
    return nullptr;

  // When DOM overlay mode is active in iframe content, the parent frame's
  // document will also be marked as being in DOM overlay mode, with the iframe
  // element being in fullscreen mode. Find the innermost reachable fullscreen
  // element to use as the XR overlay layer. This is the overlay element for
  // same-process iframes, or an iframe element for OOPIF if the overlay element
  // is in another process.
  Document* content_document = &document;
  Element* fullscreen_element =
      Fullscreen::FullscreenElementFrom(*content_document);
  while (auto* frame_owner =
             DynamicTo<HTMLFrameOwnerElement>(fullscreen_element)) {
    content_document = frame_owner->contentDocument();
    if (!content_document) {
      // This is an OOPIF iframe, treat it as the fullscreen element.
      break;
    }
    fullscreen_element = Fullscreen::FullscreenElementFrom(*content_document);
  }

  if (!fullscreen_element)
    return nullptr;

  const auto* object = fullscreen_element->GetLayoutBoxModelObject();
  if (!object) {
    // Currently, only HTML fullscreen elements are supported for this mode,
    // not others such as SVG or MathML.
    DVLOG(1) << "no LayoutBoxModelObject for element " << fullscreen_element;
    return nullptr;
  }

  return object->Layer();
}

PaintLayer* LocalFrameView::GetXROverlayLayer() const {
  Document* doc = frame_->GetDocument();
  DCHECK(doc);

  // For WebXR DOM Overlay, the fullscreen overlay layer comes from either the
  // overlay element itself, or from an iframe element if the overlay element is
  // in an OOPIF. This layer is needed even for non-main-frame scenarios to
  // ensure the background remains transparent.
  if (doc->IsXrOverlay())
    return GetXrOverlayLayer(*doc);

  return nullptr;
}

void LocalFrameView::PropagateCullRectNeedsUpdateForFrames() {
  DCHECK(RuntimeEnabledFeatures::ScrollUpdateOptimizationsEnabled());
  ForAllNonThrottledLocalFrameViews(
      [](LocalFrameView& frame_view) {
        // Propagate child frame PaintLayer NeedsCullRectUpdate flag into the
        // owner frame.
        if (auto* frame_layout_view = frame_view.GetLayoutView()) {
          if (auto* owner = frame_view.GetFrame().OwnerLayoutObject()) {
            PaintLayer* frame_root_layer = frame_layout_view->Layer();
            DCHECK(frame_root_layer);
            DCHECK(owner->Layer());
            if (frame_root_layer->NeedsCullRectUpdate() ||
                frame_root_layer->DescendantNeedsCullRectUpdate()) {
              owner->Layer()->SetDescendantNeedsCullRectUpdate();
            }
          }
        }
      },
      // Use post-order to ensure correct flag propagation for nested frames.
      kPostOrder);
}

void LocalFrameView::RunPaintBenchmark(int repeat_count,
                                       cc::PaintBenchmarkResult& result) {
  DCHECK_EQ(Lifecycle().GetState(), DocumentLifecycle::kPaintClean);
  DCHECK(GetFrame().IsLocalRoot());
  AllowThrottlingScope allow_throttling(*this);

  auto run_benchmark = [&](PaintBenchmarkMode mode) -> double {
    constexpr int kTimeCheckInterval = 1;
    constexpr int kWarmupRuns = 0;
    constexpr base::TimeDelta kTimeLimit = base::Milliseconds(1);

    base::TimeDelta min_time = base::TimeDelta::Max();
    for (int i = 0; i < repeat_count; i++) {
      // Run for a minimum amount of time to avoid problems with timer
      // quantization when the time is very small.
      base::LapTimer timer(kWarmupRuns, kTimeLimit, kTimeCheckInterval);
      do {
        // TODO(https://crbug.com/1196853): Switch to ScriptForbiddenScope once
        // failures are fixed.
        BlinkLifecycleScopeWillBeScriptForbidden forbid_script;
        RunPaintLifecyclePhase(mode);
        timer.NextLap();
      } while (!timer.HasTimeLimitExpired());

      base::TimeDelta duration = timer.TimePerLap();
      if (duration < min_time)
        min_time = duration;
    }
    return min_time.InMillisecondsF();
  };

  result.record_time_ms = run_benchmark(PaintBenchmarkMode::kForcePaint);
  result.record_time_caching_disabled_ms =
      run_benchmark(PaintBenchmarkMode::kCachingDisabled);
  result.record_time_subsequence_caching_disabled_ms =
      run_benchmark(PaintBenchmarkMode::kSubsequenceCachingDisabled);
  result.raster_invalidation_and_convert_time_ms =
      run_benchmark(PaintBenchmarkMode::kForceRasterInvalidationAndConvert);
  result.paint_artifact_compositor_update_time_ms =
      run_benchmark(PaintBenchmarkMode::kForcePaintArtifactCompositorUpdate);

  result.painter_memory_usage = 0;
  if (paint_controller_) {
    result.painter_memory_usage +=
        paint_controller_->ApproximateUnsharedMemoryUsage();
  }
  if (paint_artifact_compositor_) {
    result.painter_memory_usage +=
        paint_artifact_compositor_->ApproximateUnsharedMemoryUsage();
  }
}

DarkModeFilter& LocalFrameView::EnsureDarkModeFilter() {
  if (!dark_mode_filter_) {
    dark_mode_filter_ =
        std::make_unique<DarkModeFilter>(GetCurrentDarkModeSettings());
  }
  return *dark_mode_filter_;
}

void LocalFrameView::AddPendingTransformUpdate(LayoutObject& object) {
  if (!pending_transform_updates_) {
    pending_transform_updates_ =
        MakeGarbageCollected<HeapHashSet<Member<LayoutObject>>>();
  }
  pending_transform_updates_->insert(&object);
}

bool LocalFrameView::RemovePendingTransformUpdate(const LayoutObject& object) {
  if (!pending_transform_updates_)
    return false;
  auto it =
      pending_transform_updates_->find(const_cast<LayoutObject*>(&object));
  if (it == pending_transform_updates_->end())
    return false;
  pending_transform_updates_->erase(it);
  return true;
}

void LocalFrameView::UpdateAllPendingTransforms() {
  DCHECK(GetFrame().IsLocalRoot() || !IsAttached());
  ForAllNonThrottledLocalFrameViews([](LocalFrameView& frame_view) {
    if (frame_view.pending_transform_updates_) {
      for (const LayoutObject* object :
           *frame_view.pending_transform_updates_) {
        PaintPropertyTreeBuilder::DirectlyUpdateTransformMatrix(*object);
      }
      frame_view.pending_transform_updates_->clear();
    }
  });
}

}  // namespace blink
