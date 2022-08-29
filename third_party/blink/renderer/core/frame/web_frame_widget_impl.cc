/*
 * Copyright (C) 2014 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "third_party/blink/renderer/core/frame/web_frame_widget_impl.h"

#include <memory>
#include <utility>

#include "base/auto_reset.h"
#include "base/callback_helpers.h"
#include "base/debug/crash_logging.h"
#include "base/debug/dump_without_crashing.h"
#include "base/metrics/histogram_macros.h"
#include "base/task/single_thread_task_runner.h"
#include "base/time/time.h"
#include "build/build_config.h"
#include "cc/animation/animation_host.h"
#include "cc/base/features.h"
#include "cc/trees/compositor_commit_data.h"
#include "cc/trees/layer_tree_host.h"
#include "cc/trees/swap_promise.h"
#include "cc/trees/ukm_manager.h"
#include "third_party/blink/public/common/features.h"
#include "third_party/blink/public/mojom/frame/intrinsic_sizing_info.mojom-blink.h"
#include "third_party/blink/public/mojom/input/input_handler.mojom-blink.h"
#include "third_party/blink/public/mojom/input/touch_event.mojom-blink.h"
#include "third_party/blink/public/platform/platform.h"
#include "third_party/blink/public/web/web_autofill_client.h"
#include "third_party/blink/public/web/web_local_frame.h"
#include "third_party/blink/public/web/web_local_frame_client.h"
#include "third_party/blink/public/web/web_non_composited_widget_client.h"
#include "third_party/blink/public/web/web_performance.h"
#include "third_party/blink/public/web/web_plugin.h"
#include "third_party/blink/public/web/web_settings.h"
#include "third_party/blink/public/web/web_view_client.h"
#include "third_party/blink/renderer/core/content_capture/content_capture_manager.h"
#include "third_party/blink/renderer/core/core_initializer.h"
#include "third_party/blink/renderer/core/dom/element.h"
#include "third_party/blink/renderer/core/dom/layout_tree_builder_traversal.h"
#include "third_party/blink/renderer/core/editing/editing_utilities.h"
#include "third_party/blink/renderer/core/editing/ephemeral_range.h"
#include "third_party/blink/renderer/core/editing/frame_selection.h"
#include "third_party/blink/renderer/core/editing/ime/edit_context.h"
#include "third_party/blink/renderer/core/editing/ime/input_method_controller.h"
#include "third_party/blink/renderer/core/editing/ime/stylus_writing_gesture.h"
#include "third_party/blink/renderer/core/editing/selection_template.h"
#include "third_party/blink/renderer/core/events/current_input_event.h"
#include "third_party/blink/renderer/core/events/pointer_event_factory.h"
#include "third_party/blink/renderer/core/events/web_input_event_conversion.h"
#include "third_party/blink/renderer/core/events/wheel_event.h"
#include "third_party/blink/renderer/core/exported/web_dev_tools_agent_impl.h"
#include "third_party/blink/renderer/core/exported/web_plugin_container_impl.h"
#include "third_party/blink/renderer/core/exported/web_settings_impl.h"
#include "third_party/blink/renderer/core/exported/web_view_impl.h"
#include "third_party/blink/renderer/core/frame/local_dom_window.h"
#include "third_party/blink/renderer/core/frame/local_frame_ukm_aggregator.h"
#include "third_party/blink/renderer/core/frame/local_frame_view.h"
#include "third_party/blink/renderer/core/frame/remote_frame_client.h"
#include "third_party/blink/renderer/core/frame/screen.h"
#include "third_party/blink/renderer/core/frame/screen_metrics_emulator.h"
#include "third_party/blink/renderer/core/frame/settings.h"
#include "third_party/blink/renderer/core/frame/visual_viewport.h"
#include "third_party/blink/renderer/core/frame/web_feature.h"
#include "third_party/blink/renderer/core/frame/web_local_frame_impl.h"
#include "third_party/blink/renderer/core/html/fenced_frame/document_fenced_frames.h"
#include "third_party/blink/renderer/core/html/fenced_frame/html_fenced_frame_element.h"
#include "third_party/blink/renderer/core/html/forms/text_control_element.h"
#include "third_party/blink/renderer/core/html/html_frame_owner_element.h"
#include "third_party/blink/renderer/core/html/html_plugin_element.h"
#include "third_party/blink/renderer/core/html/portal/document_portals.h"
#include "third_party/blink/renderer/core/html/portal/portal_contents.h"
#include "third_party/blink/renderer/core/input/context_menu_allowed_scope.h"
#include "third_party/blink/renderer/core/input/event_handler.h"
#include "third_party/blink/renderer/core/input/touch_action_util.h"
#include "third_party/blink/renderer/core/layout/hit_test_location.h"
#include "third_party/blink/renderer/core/layout/hit_test_request.h"
#include "third_party/blink/renderer/core/layout/layout_box.h"
#include "third_party/blink/renderer/core/layout/layout_embedded_content.h"
#include "third_party/blink/renderer/core/layout/layout_object.h"
#include "third_party/blink/renderer/core/layout/layout_shift_tracker.h"
#include "third_party/blink/renderer/core/loader/document_loader.h"
#include "third_party/blink/renderer/core/loader/interactive_detector.h"
#include "third_party/blink/renderer/core/page/context_menu_controller.h"
#include "third_party/blink/renderer/core/page/drag_actions.h"
#include "third_party/blink/renderer/core/page/drag_controller.h"
#include "third_party/blink/renderer/core/page/drag_data.h"
#include "third_party/blink/renderer/core/page/focus_controller.h"
#include "third_party/blink/renderer/core/page/link_highlight.h"
#include "third_party/blink/renderer/core/page/page.h"
#include "third_party/blink/renderer/core/page/page_animator.h"
#include "third_party/blink/renderer/core/page/pointer_lock_controller.h"
#include "third_party/blink/renderer/core/page/scrolling/fragment_anchor.h"
#include "third_party/blink/renderer/core/page/validation_message_client.h"
#include "third_party/blink/renderer/core/page/viewport_description.h"
#include "third_party/blink/renderer/core/paint/first_meaningful_paint_detector.h"
#include "third_party/blink/renderer/core/paint/paint_timing_detector.h"
#include "third_party/blink/renderer/core/probe/core_probes.h"
#include "third_party/blink/renderer/core/scroll/scroll_into_view_util.h"
#include "third_party/blink/renderer/core/scroll/scrollbar_theme.h"
#include "third_party/blink/renderer/core/timing/dom_window_performance.h"
#include "third_party/blink/renderer/core/timing/window_performance.h"
#include "third_party/blink/renderer/platform/graphics/animation_worklet_mutator_dispatcher_impl.h"
#include "third_party/blink/renderer/platform/graphics/compositor_mutator_client.h"
#include "third_party/blink/renderer/platform/graphics/paint_worklet_paint_dispatcher.h"
#include "third_party/blink/renderer/platform/instrumentation/use_counter.h"
#include "third_party/blink/renderer/platform/keyboard_codes.h"
#include "third_party/blink/renderer/platform/scheduler/public/post_cross_thread_task.h"
#include "third_party/blink/renderer/platform/widget/input/main_thread_event_queue.h"
#include "third_party/blink/renderer/platform/widget/input/widget_input_handler_manager.h"
#include "third_party/blink/renderer/platform/widget/widget_base.h"
#include "third_party/blink/renderer/platform/wtf/cross_thread_copier_base.h"
#include "third_party/blink/renderer/platform/wtf/cross_thread_functional.h"
#include "ui/base/dragdrop/mojom/drag_drop_types.mojom-blink.h"
#include "ui/gfx/geometry/point_conversions.h"

#if BUILDFLAG(IS_MAC)
#include "third_party/blink/renderer/core/editing/substring_util.h"
#include "third_party/blink/renderer/platform/fonts/mac/attributed_string_type_converter.h"
#include "ui/base/mojom/attributed_string.mojom-blink.h"
#include "ui/gfx/geometry/point.h"
#endif

namespace WTF {

template <>
struct CrossThreadCopier<blink::WebFrameWidgetImpl::PromiseCallbacks>
    : public CrossThreadCopierByValuePassThrough<
          blink::WebFrameWidgetImpl::PromiseCallbacks> {
  STATIC_ONLY(CrossThreadCopier);
};

}  // namespace WTF

namespace blink {

namespace {

using ::ui::mojom::blink::DragOperation;

void ForEachLocalFrameControlledByWidget(
    LocalFrame* frame,
    const base::RepeatingCallback<void(WebLocalFrameImpl*)>& callback) {
  callback.Run(WebLocalFrameImpl::FromFrame(frame));
  for (Frame* child = frame->FirstChild(); child;
       child = child->NextSibling()) {
    if (child->IsLocalFrame()) {
      ForEachLocalFrameControlledByWidget(DynamicTo<LocalFrame>(child),
                                          callback);
    }
  }
}

// Iterate the remote children that will be controlled by the widget. Skip over
// any RemoteFrames have have another LocalFrame root as their parent.
void ForEachRemoteFrameChildrenControlledByWidget(
    Frame* frame,
    const base::RepeatingCallback<void(RemoteFrame*)>& callback) {
  for (Frame* child = frame->Tree().FirstChild(); child;
       child = child->Tree().NextSibling()) {
    if (auto* remote_frame = DynamicTo<RemoteFrame>(child)) {
      callback.Run(remote_frame);
      ForEachRemoteFrameChildrenControlledByWidget(remote_frame, callback);
    } else if (auto* local_frame = DynamicTo<LocalFrame>(child)) {
      // If iteration arrives at a local root then don't descend as it will be
      // controlled by another widget.
      if (!local_frame->IsLocalRoot()) {
        ForEachRemoteFrameChildrenControlledByWidget(local_frame, callback);
      }
    }
  }

  if (auto* local_frame = DynamicTo<LocalFrame>(frame)) {
    if (Document* document = local_frame->GetDocument()) {
      // Iterate on any portals owned by a local frame.
      if (auto* portals = DocumentPortals::Get(*document)) {
        for (PortalContents* portal : portals->GetPortals()) {
          if (RemoteFrame* remote_frame = portal->GetFrame())
            callback.Run(remote_frame);
        }
      }
      // Iterate on any fenced frames owned by a local frame.
      if (auto* fenced_frames = DocumentFencedFrames::Get(*document)) {
        for (HTMLFencedFrameElement* fenced_frame :
             fenced_frames->GetFencedFrames()) {
          callback.Run(To<RemoteFrame>(fenced_frame->ContentFrame()));
        }
      }
    }
  }
}

viz::FrameSinkId GetRemoteFrameSinkId(const HitTestResult& result) {
  Node* node = result.InnerNode();
  auto* frame_owner = DynamicTo<HTMLFrameOwnerElement>(node);
  if (!frame_owner || !frame_owner->ContentFrame() ||
      !frame_owner->ContentFrame()->IsRemoteFrame())
    return viz::FrameSinkId();

  RemoteFrame* remote_frame = To<RemoteFrame>(frame_owner->ContentFrame());
  if (remote_frame->IsIgnoredForHitTest())
    return viz::FrameSinkId();
  LayoutObject* object = node->GetLayoutObject();
  DCHECK(object);
  if (!object->IsBox())
    return viz::FrameSinkId();

  LayoutPoint local_point(ToRoundedPoint(result.LocalPoint()));
  if (!To<LayoutBox>(object)->ComputedCSSContentBoxRect().Contains(local_point))
    return viz::FrameSinkId();

  return remote_frame->GetFrameSinkId();
}

bool IsElementNotNullAndEditable(Element* element) {
  if (!element)
    return false;

  if (IsEditable(*element))
    return true;

  auto* text_control = ToTextControlOrNull(element);
  if (text_control && !text_control->IsDisabledOrReadOnly())
    return true;

  if (EqualIgnoringASCIICase(element->FastGetAttribute(html_names::kRoleAttr),
                             "textbox")) {
    return true;
  }

  return false;
}

}  // namespace

// WebFrameWidget ------------------------------------------------------------

bool WebFrameWidgetImpl::ignore_input_events_ = false;

WebFrameWidgetImpl::WebFrameWidgetImpl(
    base::PassKey<WebLocalFrame>,
    CrossVariantMojoAssociatedRemote<mojom::blink::FrameWidgetHostInterfaceBase>
        frame_widget_host,
    CrossVariantMojoAssociatedReceiver<mojom::blink::FrameWidgetInterfaceBase>
        frame_widget,
    CrossVariantMojoAssociatedRemote<mojom::blink::WidgetHostInterfaceBase>
        widget_host,
    CrossVariantMojoAssociatedReceiver<mojom::blink::WidgetInterfaceBase>
        widget,
    scoped_refptr<base::SingleThreadTaskRunner> task_runner,
    const viz::FrameSinkId& frame_sink_id,
    bool hidden,
    bool never_composited,
    bool is_for_child_local_root,
    bool is_for_nested_main_frame,
    bool is_for_scalable_page)
    : widget_base_(std::make_unique<WidgetBase>(
          /*widget_base_client=*/this,
          std::move(widget_host),
          std::move(widget),
          task_runner,
          hidden,
          never_composited,
          /*is_embedded=*/is_for_child_local_root || is_for_nested_main_frame,
          is_for_scalable_page)),
      frame_sink_id_(frame_sink_id),
      is_for_child_local_root_(is_for_child_local_root),
      is_for_scalable_page_(is_for_scalable_page) {
  DCHECK(task_runner);
  if (is_for_nested_main_frame)
    main_data().is_for_nested_main_frame = is_for_nested_main_frame;
  frame_widget_host_.Bind(std::move(frame_widget_host), task_runner);
  receiver_.Bind(std::move(frame_widget), task_runner);
}

WebFrameWidgetImpl::~WebFrameWidgetImpl() {
  // Ensure that Close is called and we aren't releasing |widget_base_| in the
  // destructor.
  // TODO(crbug.com/1139104): This CHECK can be changed to a DCHECK once
  // the issue is solved.
  CHECK(!widget_base_);
}

void WebFrameWidgetImpl::BindLocalRoot(WebLocalFrame& local_root) {
  local_root_ = To<WebLocalFrameImpl>(local_root);
}

bool WebFrameWidgetImpl::ForTopMostMainFrame() const {
  return ForMainFrame() && !main_data().is_for_nested_main_frame;
}

void WebFrameWidgetImpl::SetIsNestedMainFrameWidget(bool is_nested) {
  main_data().is_for_nested_main_frame = is_nested;
}

void WebFrameWidgetImpl::Close() {
  // TODO(bokan): This seems wrong since the page may have other still-active
  // frame widgets. See also: https://crbug.com/1344531.
  GetPage()->WillStopCompositing();

  if (ForMainFrame()) {
    // Closing the WebFrameWidgetImpl happens in response to the local main
    // frame being detached from the Page/WebViewImpl.
    View()->SetMainFrameViewWidget(nullptr);
  }

  mutator_dispatcher_ = nullptr;
  local_root_ = nullptr;
  widget_base_->Shutdown();
  widget_base_.reset();
  // These WeakPtrs must be invalidated for WidgetInputHandlerManager at the
  // same time as the WidgetBase is.
  input_handler_weak_ptr_factory_.InvalidateWeakPtrs();
  receiver_.reset();
  input_target_receiver_.reset();
}

WebLocalFrame* WebFrameWidgetImpl::LocalRoot() const {
  return local_root_;
}

gfx::Rect WebFrameWidgetImpl::ComputeBlockBound(
    const gfx::Point& point_in_root_frame,
    bool ignore_clipping) const {
  HitTestLocation location(local_root_->GetFrameView()->ConvertFromRootFrame(
      PhysicalOffset(point_in_root_frame)));
  HitTestRequest::HitTestRequestType hit_type =
      HitTestRequest::kReadOnly | HitTestRequest::kActive |
      (ignore_clipping ? HitTestRequest::kIgnoreClipping : 0);
  HitTestResult result =
      local_root_->GetFrame()->GetEventHandler().HitTestResultAtLocation(
          location, hit_type);
  result.SetToShadowHostIfInRestrictedShadowRoot();

  Node* node = result.InnerNodeOrImageMapImage();
  if (!node)
    return gfx::Rect();

  // Find the block type node based on the hit node.
  // FIXME: This wants to walk flat tree with
  // LayoutTreeBuilderTraversal::parent().
  while (node &&
         (!node->GetLayoutObject() || node->GetLayoutObject()->IsInline()))
    node = LayoutTreeBuilderTraversal::Parent(*node);

  // Return the bounding box in the root frame's coordinate space.
  if (node) {
    gfx::Rect absolute_rect =
        node->GetLayoutObject()->AbsoluteBoundingBoxRect();
    LocalFrame* frame = node->GetDocument().GetFrame();
    return frame->View()->ConvertToRootFrame(absolute_rect);
  }
  return gfx::Rect();
}

void WebFrameWidgetImpl::DragTargetDragEnter(
    const WebDragData& web_drag_data,
    const gfx::PointF& point_in_viewport,
    const gfx::PointF& screen_point,
    DragOperationsMask operations_allowed,
    uint32_t key_modifiers,
    DragTargetDragEnterCallback callback) {
  DCHECK(!current_drag_data_);

  current_drag_data_ = DataObject::Create(web_drag_data);
  operations_allowed_ = operations_allowed;

  DragOperation operation = DragTargetDragEnterOrOver(
      point_in_viewport, screen_point, kDragEnter, key_modifiers);
  std::move(callback).Run(operation);
}

void WebFrameWidgetImpl::DragTargetDragOver(
    const gfx::PointF& point_in_viewport,
    const gfx::PointF& screen_point,
    DragOperationsMask operations_allowed,
    uint32_t key_modifiers,
    DragTargetDragOverCallback callback) {
  operations_allowed_ = operations_allowed;

  DragOperation operation = DragTargetDragEnterOrOver(
      point_in_viewport, screen_point, kDragOver, key_modifiers);
  std::move(callback).Run(operation);
}

void WebFrameWidgetImpl::DragTargetDragLeave(
    const gfx::PointF& point_in_viewport,
    const gfx::PointF& screen_point) {
  DCHECK(current_drag_data_);

  // TODO(paulmeyer): It shouldn't be possible for |current_drag_data_| to be
  // null here, but this is somehow happening (rarely). This suggests that in
  // some cases drag-leave is happening before drag-enter, which should be
  // impossible. This needs to be investigated further. Once fixed, the extra
  // check for |!current_drag_data_| should be removed. (crbug.com/671152)
  if (IgnoreInputEvents() || !current_drag_data_) {
    CancelDrag();
    return;
  }

  gfx::PointF point_in_root_frame(ViewportToRootFrame(point_in_viewport));
  DragData drag_data(current_drag_data_.Get(), point_in_root_frame,
                     screen_point, operations_allowed_);

  GetPage()->GetDragController().DragExited(&drag_data,
                                            *local_root_->GetFrame());

  // FIXME: why is the drag scroll timer not stopped here?

  drag_operation_ = DragOperation::kNone;
  current_drag_data_ = nullptr;
}

void WebFrameWidgetImpl::DragTargetDrop(const WebDragData& web_drag_data,
                                        const gfx::PointF& point_in_viewport,
                                        const gfx::PointF& screen_point,
                                        uint32_t key_modifiers,
                                        base::OnceClosure callback) {
  base::ScopedClosureRunner calllback_runner(std::move(callback));
  gfx::PointF point_in_root_frame(ViewportToRootFrame(point_in_viewport));

  DCHECK(current_drag_data_);
  current_drag_data_ = DataObject::Create(web_drag_data);

  // If this webview transitions from the "drop accepting" state to the "not
  // accepting" state, then our IPC message reply indicating that may be in-
  // flight, or else delayed by javascript processing in this webview.  If a
  // drop happens before our IPC reply has reached the browser process, then
  // the browser forwards the drop to this webview.  So only allow a drop to
  // proceed if our webview drag_operation_ state is not DragOperation::kNone.

  if (drag_operation_ == DragOperation::kNone) {
    // IPC RACE CONDITION: do not allow this drop.
    DragTargetDragLeave(point_in_viewport, screen_point);
    return;
  }

  if (!IgnoreInputEvents()) {
    current_drag_data_->SetModifiers(key_modifiers);
    DragData drag_data(current_drag_data_.Get(), point_in_root_frame,
                       screen_point, operations_allowed_);

    GetPage()->GetDragController().PerformDrag(&drag_data,
                                               *local_root_->GetFrame());
  }
  drag_operation_ = DragOperation::kNone;
  current_drag_data_ = nullptr;
}

void WebFrameWidgetImpl::DragSourceEndedAt(const gfx::PointF& point_in_viewport,
                                           const gfx::PointF& screen_point,
                                           DragOperation operation,
                                           base::OnceClosure callback) {
  base::ScopedClosureRunner calllback_runner(std::move(callback));
  if (!local_root_) {
    // We should figure out why |local_root_| could be nullptr
    // (https://crbug.com/792345).
    return;
  }

  if (IgnoreInputEvents()) {
    CancelDrag();
    return;
  }
  gfx::PointF point_in_root_frame =
      GetPage()->GetVisualViewport().ViewportToRootFrame(point_in_viewport);

  WebMouseEvent fake_mouse_move(
      WebInputEvent::Type::kMouseMove, point_in_root_frame, screen_point,
      WebPointerProperties::Button::kLeft, 0, WebInputEvent::kNoModifiers,
      base::TimeTicks::Now());
  fake_mouse_move.SetFrameScale(1);
  local_root_->GetFrame()->GetEventHandler().DragSourceEndedAt(fake_mouse_move,
                                                               operation);
}

void WebFrameWidgetImpl::DragSourceSystemDragEnded() {
  CancelDrag();
}

gfx::Rect WebFrameWidgetImpl::GetAbsoluteCaretBounds() {
  LocalFrame* local_frame = GetPage()->GetFocusController().FocusedFrame();
  if (local_frame) {
    auto& selection = local_frame->Selection();
    if (selection.GetSelectionInDOMTree().IsCaret())
      return selection.AbsoluteCaretBounds();
  }
  return gfx::Rect();
}

void WebFrameWidgetImpl::OnStartStylusWriting(
    OnStartStylusWritingCallback callback) {
  // Focus the stylus writable element for current touch sequence as we have
  // detected writing has started.
  LocalFrame* frame = GetPage()->GetFocusController().FocusedFrame();
  if (!frame) {
    std::move(callback).Run(absl::nullopt, absl::nullopt);
    return;
  }

  Element* stylus_writable_element =
      frame->GetEventHandler().CurrentTouchDownElement();
  if (!stylus_writable_element) {
    std::move(callback).Run(absl::nullopt, absl::nullopt);
    return;
  }

  if (auto* text_control = EnclosingTextControl(stylus_writable_element)) {
    text_control->Focus();
  } else if (auto* html_element =
                 DynamicTo<HTMLElement>(stylus_writable_element)) {
    html_element->Focus();
  }
  Element* focused_element = FocusedElement();
  // Since the element can change after it gets focused, we just verify if
  // the focused element is editable to continue writing.
  if (IsElementNotNullAndEditable(focused_element)) {
    std::move(callback).Run(
        focused_element->BoundsInViewport(),
        frame->View()->FrameToViewport(GetAbsoluteCaretBounds()));
    return;
  }

  std::move(callback).Run(absl::nullopt, absl::nullopt);
}

void WebFrameWidgetImpl::HandleStylusWritingGestureAction(
    mojom::blink::StylusWritingGestureDataPtr gesture_data) {
  LocalFrame* focused_frame = FocusedLocalFrameInWidget();
  if (!focused_frame)
    return;
  StylusWritingGesture::ApplyGesture(focused_frame, std::move(gesture_data));
}

void WebFrameWidgetImpl::SetBackgroundOpaque(bool opaque) {
  View()->SetBaseBackgroundColorOverrideTransparent(!opaque);
}

void WebFrameWidgetImpl::SetTextDirection(base::i18n::TextDirection direction) {
  LocalFrame* focusedFrame = FocusedLocalFrameInWidget();
  if (focusedFrame)
    focusedFrame->SetTextDirection(direction);
}

void WebFrameWidgetImpl::SetInheritedEffectiveTouchActionForSubFrame(
    TouchAction touch_action) {
  DCHECK(ForSubframe());
  LocalRootImpl()->GetFrame()->SetInheritedEffectiveTouchAction(touch_action);
}

void WebFrameWidgetImpl::UpdateRenderThrottlingStatusForSubFrame(
    bool is_throttled,
    bool subtree_throttled,
    bool display_locked) {
  DCHECK(ForSubframe());
  // TODO(szager,vmpstr): The parent render process currently rolls up
  // display_locked into the value of subtree throttled here; display_locked
  // should be maintained as a separate bit and transmitted between render
  // processes.
  LocalRootImpl()->GetFrameView()->UpdateRenderThrottlingStatus(
      is_throttled, subtree_throttled, display_locked, /*recurse=*/true);
}

#if BUILDFLAG(IS_MAC)
void WebFrameWidgetImpl::GetStringAtPoint(const gfx::Point& point_in_local_root,
                                          GetStringAtPointCallback callback) {
  gfx::Point baseline_point;
  ui::mojom::blink::AttributedStringPtr attributed_string = nullptr;
  NSAttributedString* string = SubstringUtil::AttributedWordAtPoint(
      this, point_in_local_root, baseline_point);
  if (string)
    attributed_string = ui::mojom::blink::AttributedString::From(string);

  std::move(callback).Run(std::move(attributed_string), baseline_point);
}
#endif

void WebFrameWidgetImpl::BindWidgetCompositor(
    mojo::PendingReceiver<mojom::blink::WidgetCompositor> receiver) {
  widget_base_->BindWidgetCompositor(std::move(receiver));
}

void WebFrameWidgetImpl::BindInputTargetClient(
    mojo::PendingReceiver<viz::mojom::blink::InputTargetClient> receiver) {
  DCHECK(!input_target_receiver_.is_bound());
  TaskType priority = TaskType::kInternalDefault;
  if (base::FeatureList::IsEnabled(
          blink::features::kInputTargetClientHighPriority)) {
    priority = TaskType::kInternalInputBlocking;
  }
  input_target_receiver_.Bind(std::move(receiver),
                              local_root_->GetTaskRunner(priority));
}

void WebFrameWidgetImpl::FrameSinkIdAt(const gfx::PointF& point,
                                       const uint64_t trace_id,
                                       FrameSinkIdAtCallback callback) {
  TRACE_EVENT_WITH_FLOW1("viz,benchmark", "Event.Pipeline",
                         TRACE_ID_GLOBAL(trace_id),
                         TRACE_EVENT_FLAG_FLOW_IN | TRACE_EVENT_FLAG_FLOW_OUT,
                         "step", "FrameSinkIdAt");

  gfx::PointF local_point;
  viz::FrameSinkId id = GetFrameSinkIdAtPoint(point, &local_point);
  std::move(callback).Run(id, local_point);
}

viz::FrameSinkId WebFrameWidgetImpl::GetFrameSinkIdAtPoint(
    const gfx::PointF& point_in_dips,
    gfx::PointF* local_point_in_dips) {
  HitTestResult result =
      CoreHitTestResultAt(widget_base_->DIPsToBlinkSpace(point_in_dips));

  Node* result_node = result.InnerNode();
  *local_point_in_dips = gfx::PointF(point_in_dips);

  // TODO(crbug.com/797828): When the node is null the caller may
  // need to do extra checks. Like maybe update the layout and then
  // call the hit-testing API. Either way it might be better to have
  // a DCHECK for the node rather than a null check here.
  if (!result_node) {
    return frame_sink_id_;
  }

  viz::FrameSinkId remote_frame_sink_id = GetRemoteFrameSinkId(result);
  if (remote_frame_sink_id.is_valid()) {
    gfx::PointF local_point(result.LocalPoint());
    LayoutObject* object = result_node->GetLayoutObject();
    if (auto* box = DynamicTo<LayoutBox>(object))
      local_point -= gfx::Vector2dF(box->PhysicalContentBoxOffset());

    *local_point_in_dips = widget_base_->BlinkSpaceToDIPs(local_point);
    return remote_frame_sink_id;
  }

  // Return the FrameSinkId for the current widget if the point did not hit
  // test to a remote frame, or the point is outside of the remote frame's
  // content box, or the remote frame doesn't have a valid FrameSinkId yet.
  return frame_sink_id_;
}

gfx::RectF WebFrameWidgetImpl::BlinkSpaceToDIPs(const gfx::RectF& rect) {
  return widget_base_->BlinkSpaceToDIPs(rect);
}

gfx::Rect WebFrameWidgetImpl::BlinkSpaceToEnclosedDIPs(const gfx::Rect& rect) {
  return widget_base_->BlinkSpaceToEnclosedDIPs(rect);
}

gfx::Size WebFrameWidgetImpl::BlinkSpaceToFlooredDIPs(const gfx::Size& size) {
  return widget_base_->BlinkSpaceToFlooredDIPs(size);
}

gfx::RectF WebFrameWidgetImpl::DIPsToBlinkSpace(const gfx::RectF& rect) {
  return widget_base_->DIPsToBlinkSpace(rect);
}

gfx::PointF WebFrameWidgetImpl::DIPsToBlinkSpace(const gfx::PointF& point) {
  return widget_base_->DIPsToBlinkSpace(point);
}

gfx::Point WebFrameWidgetImpl::DIPsToRoundedBlinkSpace(
    const gfx::Point& point) {
  return widget_base_->DIPsToRoundedBlinkSpace(point);
}

float WebFrameWidgetImpl::DIPsToBlinkSpace(float scalar) {
  return widget_base_->DIPsToBlinkSpace(scalar);
}

gfx::Size WebFrameWidgetImpl::DIPsToCeiledBlinkSpace(const gfx::Size& size) {
  return widget_base_->DIPsToCeiledBlinkSpace(size);
}

void WebFrameWidgetImpl::SetActive(bool active) {
  View()->SetIsActive(active);
}

WebInputEventResult WebFrameWidgetImpl::HandleKeyEvent(
    const WebKeyboardEvent& event) {
  DCHECK((event.GetType() == WebInputEvent::Type::kRawKeyDown) ||
         (event.GetType() == WebInputEvent::Type::kKeyDown) ||
         (event.GetType() == WebInputEvent::Type::kKeyUp));

  // Please refer to the comments explaining the m_suppressNextKeypressEvent
  // member.
  // The m_suppressNextKeypressEvent is set if the KeyDown is handled by
  // Webkit. A keyDown event is typically associated with a keyPress(char)
  // event and a keyUp event. We reset this flag here as this is a new keyDown
  // event.
  suppress_next_keypress_event_ = false;

  // If there is a popup open, it should be the one processing the event,
  // not the page.
  scoped_refptr<WebPagePopupImpl> page_popup = View()->GetPagePopup();
  if (page_popup) {
    page_popup->HandleKeyEvent(event);
    if (event.GetType() == WebInputEvent::Type::kRawKeyDown) {
      suppress_next_keypress_event_ = true;
    }
    return WebInputEventResult::kHandledSystem;
  }

  auto* frame = DynamicTo<LocalFrame>(FocusedCoreFrame());
  if (!frame)
    return WebInputEventResult::kNotHandled;

  WebInputEventResult result = frame->GetEventHandler().KeyEvent(event);
  // EventHandler may have detached the frame.
  if (!LocalRootImpl())
    return result;

  if (result != WebInputEventResult::kNotHandled) {
    if (WebInputEvent::Type::kRawKeyDown == event.GetType()) {
      // Suppress the next keypress event unless the focused node is a plugin
      // node.  (Flash needs these keypress events to handle non-US keyboards.)
      Element* element = FocusedElement();
      if (element && element->GetLayoutObject() &&
          element->GetLayoutObject()->IsEmbeddedObject()) {
        if (event.windows_key_code == VKEY_TAB) {
          // If the plugin supports keyboard focus then we should not send a tab
          // keypress event.
          WebPluginContainerImpl* plugin_view =
              To<LayoutEmbeddedContent>(element->GetLayoutObject())->Plugin();
          if (plugin_view && plugin_view->SupportsKeyboardFocus()) {
            suppress_next_keypress_event_ = true;
          }
        }
      } else {
        suppress_next_keypress_event_ = true;
      }
    }
    return result;
  }

#if !BUILDFLAG(IS_MAC)
  const WebInputEvent::Type kContextMenuKeyTriggeringEventType =
#if BUILDFLAG(IS_WIN)
      WebInputEvent::Type::kKeyUp;
#else
      WebInputEvent::Type::kRawKeyDown;
#endif
  const WebInputEvent::Type kShiftF10TriggeringEventType =
      WebInputEvent::Type::kRawKeyDown;

  bool is_unmodified_menu_key =
      !(event.GetModifiers() & WebInputEvent::kInputModifiers) &&
      event.windows_key_code == VKEY_APPS;
  bool is_shift_f10 = (event.GetModifiers() & WebInputEvent::kInputModifiers) ==
                          WebInputEvent::kShiftKey &&
                      event.windows_key_code == VKEY_F10;
  if ((is_unmodified_menu_key &&
       event.GetType() == kContextMenuKeyTriggeringEventType) ||
      (is_shift_f10 && event.GetType() == kShiftF10TriggeringEventType)) {
    View()->SendContextMenuEvent();
    return WebInputEventResult::kHandledSystem;
  }
#endif  // !BUILDFLAG(IS_MAC)

  return WebInputEventResult::kNotHandled;
}

void WebFrameWidgetImpl::HandleMouseDown(LocalFrame& local_root,
                                         const WebMouseEvent& event) {
  WebViewImpl* view_impl = View();
  // If there is a popup open, close it as the user is clicking on the page
  // (outside of the popup). We also save it so we can prevent a click on an
  // element from immediately reopening the same popup.
  scoped_refptr<WebPagePopupImpl> page_popup;
  if (event.button == WebMouseEvent::Button::kLeft) {
    page_popup = view_impl->GetPagePopup();
    view_impl->CancelPagePopup();
  }

  // Take capture on a mouse down on a plugin so we can send it mouse events.
  // If the hit node is a plugin but a scrollbar is over it don't start mouse
  // capture because it will interfere with the scrollbar receiving events.
  PhysicalOffset point(LayoutUnit(event.PositionInWidget().x()),
                       LayoutUnit(event.PositionInWidget().y()));
  if (event.button == WebMouseEvent::Button::kLeft) {
    HitTestLocation location(
        LocalRootImpl()->GetFrameView()->ConvertFromRootFrame(point));
    HitTestResult result(
        LocalRootImpl()->GetFrame()->GetEventHandler().HitTestResultAtLocation(
            location));
    result.SetToShadowHostIfInRestrictedShadowRoot();
    Node* hit_node = result.InnerNode();
    auto* html_element = DynamicTo<HTMLElement>(hit_node);
    if (!result.GetScrollbar() && hit_node && hit_node->GetLayoutObject() &&
        hit_node->GetLayoutObject()->IsEmbeddedObject() && html_element &&
        html_element->IsPluginElement()) {
      mouse_capture_element_ = To<HTMLPlugInElement>(hit_node);
      SetMouseCapture(true);
      TRACE_EVENT_NESTABLE_ASYNC_BEGIN0("input", "capturing mouse",
                                        TRACE_ID_LOCAL(this));
    }
  }

  WidgetEventHandler::HandleMouseDown(local_root, event);
  // WidgetEventHandler may have detached the frame.
  if (!LocalRootImpl())
    return;

  if (view_impl->GetPagePopup() && page_popup &&
      view_impl->GetPagePopup()->HasSamePopupClient(page_popup.get())) {
    // That click triggered a page popup that is the same as the one we just
    // closed.  It needs to be closed.
    view_impl->CancelPagePopup();
  }

  // Dispatch the contextmenu event regardless of if the click was swallowed.
  if (!GetPage()->GetSettings().GetShowContextMenuOnMouseUp()) {
#if BUILDFLAG(IS_MAC)
    if (event.button == WebMouseEvent::Button::kRight ||
        (event.button == WebMouseEvent::Button::kLeft &&
         event.GetModifiers() & WebMouseEvent::kControlKey))
      MouseContextMenu(event);
#else
    if (event.button == WebMouseEvent::Button::kRight)
      MouseContextMenu(event);
#endif
  }
}

void WebFrameWidgetImpl::HandleMouseLeave(LocalFrame& local_root,
                                          const WebMouseEvent& event) {
  View()->SetMouseOverURL(WebURL());
  WidgetEventHandler::HandleMouseLeave(local_root, event);
  // WidgetEventHandler may have detached the frame.
}

void WebFrameWidgetImpl::MouseContextMenu(const WebMouseEvent& event) {
  GetPage()->GetContextMenuController().ClearContextMenu();

  WebMouseEvent transformed_event =
      TransformWebMouseEvent(LocalRootImpl()->GetFrameView(), event);
  transformed_event.menu_source_type = kMenuSourceMouse;
  transformed_event.id = PointerEventFactory::kMouseId;

  // Find the right target frame. See issue 1186900.
  HitTestResult result =
      HitTestResultForRootFramePos(transformed_event.PositionInRootFrame());
  Frame* target_frame;
  if (result.InnerNodeOrImageMapImage())
    target_frame = result.InnerNodeOrImageMapImage()->GetDocument().GetFrame();
  else
    target_frame = GetPage()->GetFocusController().FocusedOrMainFrame();

  // This will need to be changed to a nullptr check when focus control
  // is refactored, at which point focusedOrMainFrame will never return a
  // RemoteFrame.
  // See https://crbug.com/341918.
  LocalFrame* target_local_frame = DynamicTo<LocalFrame>(target_frame);
  if (!target_local_frame)
    return;

  {
    ContextMenuAllowedScope scope;
    target_local_frame->GetEventHandler().SendContextMenuEvent(
        transformed_event);
  }
  // Actually showing the context menu is handled by the ContextMenuClient
  // implementation...
}

WebInputEventResult WebFrameWidgetImpl::HandleMouseUp(
    LocalFrame& local_root,
    const WebMouseEvent& event) {
  WebInputEventResult result =
      WidgetEventHandler::HandleMouseUp(local_root, event);
  // WidgetEventHandler may have detached the frame.
  if (!LocalRootImpl())
    return result;

  if (GetPage()->GetSettings().GetShowContextMenuOnMouseUp()) {
    // Dispatch the contextmenu event regardless of if the click was swallowed.
    // On Mac/Linux, we handle it on mouse down, not up.
    if (event.button == WebMouseEvent::Button::kRight)
      MouseContextMenu(event);
  }
  return result;
}

WebInputEventResult WebFrameWidgetImpl::HandleGestureEvent(
    const WebGestureEvent& event) {
  WebInputEventResult event_result = WebInputEventResult::kNotHandled;

  // Fling events are not sent to the renderer.
  CHECK(event.GetType() != WebInputEvent::Type::kGestureFlingStart);
  CHECK(event.GetType() != WebInputEvent::Type::kGestureFlingCancel);

  WebViewImpl* web_view = View();

  LocalFrame* frame = LocalRootImpl()->GetFrame();
  WebGestureEvent scaled_event = TransformWebGestureEvent(frame->View(), event);

  // Special handling for double tap and scroll events as we don't want to
  // hit test for them.
  switch (event.GetType()) {
    case WebInputEvent::Type::kGestureDoubleTap:
      if (web_view->SettingsImpl()->DoubleTapToZoomEnabled() &&
          web_view->MinimumPageScaleFactor() !=
              web_view->MaximumPageScaleFactor()) {
        gfx::Point pos_in_local_frame_root =
            gfx::ToFlooredPoint(scaled_event.PositionInRootFrame());
        auto block_bounds = ComputeBlockBound(pos_in_local_frame_root, false);

        if (ForMainFrame()) {
          web_view->AnimateDoubleTapZoom(pos_in_local_frame_root, block_bounds);
        } else {
          // This sends the tap point and bounds to the main frame renderer via
          // the browser, where their coordinates will be transformed into the
          // main frame's coordinate space.
          GetAssociatedFrameWidgetHost()->AnimateDoubleTapZoomInMainFrame(
              pos_in_local_frame_root, block_bounds);
        }
      }
      event_result = WebInputEventResult::kHandledSystem;
      DidHandleGestureEvent(event);
      return event_result;
    case WebInputEvent::Type::kGestureScrollBegin:
    case WebInputEvent::Type::kGestureScrollEnd:
    case WebInputEvent::Type::kGestureScrollUpdate:
      // If we are getting any scroll toss close any page popup that is open.
      web_view->CancelPagePopup();

      // Scrolling-related gesture events invoke EventHandler recursively for
      // each frame down the chain, doing a single-frame hit-test per frame.
      // This matches handleWheelEvent.  Perhaps we could simplify things by
      // rewriting scroll handling to work inner frame out, and then unify with
      // other gesture events.
      event_result =
          frame->GetEventHandler().HandleGestureScrollEvent(scaled_event);
      DidHandleGestureEvent(event);
      return event_result;
    default:
      break;
  }

  // Hit test across all frames and do touch adjustment as necessary for the
  // event type.
  GestureEventWithHitTestResults targeted_event =
      frame->GetEventHandler().TargetGestureEvent(scaled_event);

  // Link highlight animations are only for the main frame.
  // TODO(bokan): This isn't intentional, see https://crbug.com/1344531.
  if (ForMainFrame()) {
    // Handle link highlighting outside the main switch to avoid getting lost in
    // the complicated set of cases handled below.
    switch (scaled_event.GetType()) {
      case WebInputEvent::Type::kGestureShowPress:
        // Queue a highlight animation, then hand off to regular handler.
        web_view->EnableTapHighlightAtPoint(targeted_event);
        break;
      case WebInputEvent::Type::kGestureShortPress:
      case WebInputEvent::Type::kGestureLongPress:
      case WebInputEvent::Type::kGestureTapCancel:
      case WebInputEvent::Type::kGestureTap:
        GetPage()->GetLinkHighlight().UpdateOpacityAndRequestAnimation();
        break;
      default:
        break;
    }
  }

  switch (scaled_event.GetType()) {
    case WebInputEvent::Type::kGestureTap: {
      {
        ContextMenuAllowedScope scope;
        event_result =
            frame->GetEventHandler().HandleGestureEvent(targeted_event);
      }

      if (web_view->GetPagePopup() && last_hidden_page_popup_ &&
          web_view->GetPagePopup()->HasSamePopupClient(
              last_hidden_page_popup_.get())) {
        // The tap triggered a page popup that is the same as the one we just
        // closed. It needs to be closed.
        web_view->CancelPagePopup();
      }
      // Don't have this value persist outside of a single tap gesture, plus
      // we're done with it now.
      last_hidden_page_popup_ = nullptr;
      break;
    }
    case WebInputEvent::Type::kGestureTwoFingerTap:
    case WebInputEvent::Type::kGestureLongPress:
    case WebInputEvent::Type::kGestureLongTap:
      if (scaled_event.GetType() == WebInputEvent::Type::kGestureLongTap) {
        if (LocalFrame* inner_frame =
                targeted_event.GetHitTestResult().InnerNodeFrame()) {
          if (!inner_frame->GetEventHandler().LongTapShouldInvokeContextMenu())
            break;
        } else if (!frame->GetEventHandler().LongTapShouldInvokeContextMenu()) {
          break;
        }
      }

      GetPage()->GetContextMenuController().ClearContextMenu();
      {
        ContextMenuAllowedScope scope;
        event_result =
            frame->GetEventHandler().HandleGestureEvent(targeted_event);
      }

      break;
    case WebInputEvent::Type::kGestureTapDown:
      // Touch pinch zoom and scroll on the page (outside of a popup) must hide
      // the popup. In case of a touch scroll or pinch zoom, this function is
      // called with GestureTapDown rather than a GSB/GSU/GSE or GPB/GPU/GPE.
      // When we close a popup because of a GestureTapDown, we also save it so
      // we can prevent the following GestureTap from immediately reopening the
      // same popup.
      // This value should not persist outside of a gesture, so is cleared by
      // GestureTap (where it is used) and by GestureCancel.
      last_hidden_page_popup_ = web_view->GetPagePopup();
      web_view->CancelPagePopup();
      event_result =
          frame->GetEventHandler().HandleGestureEvent(targeted_event);
      break;
    case WebInputEvent::Type::kGestureTapCancel:
      // Don't have this value persist outside of a single tap gesture.
      last_hidden_page_popup_ = nullptr;
      event_result =
          frame->GetEventHandler().HandleGestureEvent(targeted_event);
      break;
    case WebInputEvent::Type::kGestureShowPress:
    case WebInputEvent::Type::kGestureTapUnconfirmed:
    case WebInputEvent::Type::kGestureShortPress:
      event_result =
          frame->GetEventHandler().HandleGestureEvent(targeted_event);
      break;
    default:
      NOTREACHED();
  }
  DidHandleGestureEvent(event);
  return event_result;
}

WebInputEventResult WebFrameWidgetImpl::HandleMouseWheel(
    LocalFrame& frame,
    const WebMouseWheelEvent& event) {
  View()->CancelPagePopup();
  return WidgetEventHandler::HandleMouseWheel(frame, event);
  // WidgetEventHandler may have detached the frame.
}

WebInputEventResult WebFrameWidgetImpl::HandleCharEvent(
    const WebKeyboardEvent& event) {
  DCHECK_EQ(event.GetType(), WebInputEvent::Type::kChar);

  // Please refer to the comments explaining the m_suppressNextKeypressEvent
  // member.  The m_suppressNextKeypressEvent is set if the KeyDown is
  // handled by Webkit. A keyDown event is typically associated with a
  // keyPress(char) event and a keyUp event. We reset this flag here as it
  // only applies to the current keyPress event.
  bool suppress = suppress_next_keypress_event_;
  suppress_next_keypress_event_ = false;

  // If there is a popup open, it should be the one processing the event,
  // not the page.
  scoped_refptr<WebPagePopupImpl> page_popup = View()->GetPagePopup();
  if (page_popup)
    return page_popup->HandleKeyEvent(event);

  LocalFrame* frame = To<LocalFrame>(FocusedCoreFrame());
  if (!frame) {
    return suppress ? WebInputEventResult::kHandledSuppressed
                    : WebInputEventResult::kNotHandled;
  }

  EventHandler& handler = frame->GetEventHandler();

  if (!event.IsCharacterKey())
    return WebInputEventResult::kHandledSuppressed;

  // Accesskeys are triggered by char events and can't be suppressed.
  // It is unclear whether a keypress should be dispatched as well
  // crbug.com/563507
  if (handler.HandleAccessKey(event))
    return WebInputEventResult::kHandledSystem;

  // Safari 3.1 does not pass off windows system key messages (WM_SYSCHAR) to
  // the eventHandler::keyEvent. We mimic this behavior on all platforms since
  // for now we are converting other platform's key events to windows key
  // events.
  if (event.is_system_key)
    return WebInputEventResult::kNotHandled;

  if (suppress)
    return WebInputEventResult::kHandledSuppressed;

  WebInputEventResult result = handler.KeyEvent(event);
  if (result != WebInputEventResult::kNotHandled)
    return result;

  return WebInputEventResult::kNotHandled;
}

void WebFrameWidgetImpl::CancelDrag() {
  // It's possible for this to be called while we're not doing a drag if
  // it's from a previous page that got unloaded.
  if (!doing_drag_and_drop_)
    return;
  GetPage()->GetDragController().DragEnded();
  doing_drag_and_drop_ = false;
}

void WebFrameWidgetImpl::StartDragging(const WebDragData& drag_data,
                                       DragOperationsMask operations_allowed,
                                       const SkBitmap& drag_image,
                                       const gfx::Point& drag_image_offset) {
  doing_drag_and_drop_ = true;
  if (drag_and_drop_disabled_) {
    DragSourceSystemDragEnded();
    return;
  }

  gfx::Point offset_in_dips =
      widget_base_->BlinkSpaceToFlooredDIPs(drag_image_offset);
  GetAssociatedFrameWidgetHost()->StartDragging(
      drag_data, operations_allowed, drag_image,
      gfx::Vector2d(offset_in_dips.x(), offset_in_dips.y()),
      possible_drag_event_info_.Clone());
}

DragOperation WebFrameWidgetImpl::DragTargetDragEnterOrOver(
    const gfx::PointF& point_in_viewport,
    const gfx::PointF& screen_point,
    DragAction drag_action,
    uint32_t key_modifiers) {
  DCHECK(current_drag_data_);
  // TODO(paulmeyer): It shouldn't be possible for |m_currentDragData| to be
  // null here, but this is somehow happening (rarely). This suggests that in
  // some cases drag-over is happening before drag-enter, which should be
  // impossible. This needs to be investigated further. Once fixed, the extra
  // check for |!m_currentDragData| should be removed. (crbug.com/671504)
  if (IgnoreInputEvents() || !current_drag_data_) {
    CancelDrag();
    return DragOperation::kNone;
  }

  gfx::PointF point_in_root_frame = ViewportToRootFrame(point_in_viewport);

  current_drag_data_->SetModifiers(key_modifiers);
  DragData drag_data(current_drag_data_.Get(), point_in_root_frame,
                     screen_point, operations_allowed_);

  DragOperation drag_operation =
      GetPage()->GetDragController().DragEnteredOrUpdated(
          &drag_data, *local_root_->GetFrame());

  // Mask the drag operation against the drag source's allowed
  // operations.
  if (!(static_cast<int>(drag_operation) &
        drag_data.DraggingSourceOperationMask()))
    drag_operation = DragOperation::kNone;

  drag_operation_ = drag_operation;

  return drag_operation_;
}

void WebFrameWidgetImpl::SendOverscrollEventFromImplSide(
    const gfx::Vector2dF& overscroll_delta,
    cc::ElementId scroll_latched_element_id) {
  if (!RuntimeEnabledFeatures::OverscrollCustomizationEnabled())
    return;

  Node* target_node = View()->FindNodeFromScrollableCompositorElementId(
      scroll_latched_element_id);
  if (target_node) {
    target_node->GetDocument().EnqueueOverscrollEventForNode(
        target_node, overscroll_delta.x(), overscroll_delta.y());
  }
}

void WebFrameWidgetImpl::SendScrollEndEventFromImplSide(
    cc::ElementId scroll_latched_element_id) {
  if (!RuntimeEnabledFeatures::ScrollEndEventsEnabled())
    return;

  Node* target_node = View()->FindNodeFromScrollableCompositorElementId(
      scroll_latched_element_id);
  if (target_node)
    target_node->GetDocument().EnqueueScrollEndEventForNode(target_node);
}

void WebFrameWidgetImpl::UpdateCompositorScrollState(
    const cc::CompositorCommitData& commit_data) {
  if (WebDevToolsAgentImpl* devtools = LocalRootImpl()->DevToolsAgentImpl())
    devtools->SetPageIsScrolling(commit_data.is_scroll_active);

  RecordManipulationTypeCounts(commit_data.manipulation_info);

  if (commit_data.scroll_latched_element_id == cc::ElementId())
    return;

  if (!commit_data.overscroll_delta.IsZero()) {
    SendOverscrollEventFromImplSide(commit_data.overscroll_delta,
                                    commit_data.scroll_latched_element_id);
  }

  // TODO(bokan): If a scroll ended and a new one began in the same Blink frame
  // (e.g. during a long running main thread task), this will erroneously
  // dispatch the scroll end to the latter (still-scrolling) element.
  // https://crbug.com/1116780.
  if (commit_data.scroll_gesture_did_end)
    SendScrollEndEventFromImplSide(commit_data.scroll_latched_element_id);
}

WebInputMethodController*
WebFrameWidgetImpl::GetActiveWebInputMethodController() const {
  WebLocalFrameImpl* local_frame =
      WebLocalFrameImpl::FromFrame(FocusedLocalFrameInWidget());
  return local_frame ? local_frame->GetInputMethodController() : nullptr;
}

void WebFrameWidgetImpl::DisableDragAndDrop() {
  drag_and_drop_disabled_ = true;
}

gfx::PointF WebFrameWidgetImpl::ViewportToRootFrame(
    const gfx::PointF& point_in_viewport) const {
  return GetPage()->GetVisualViewport().ViewportToRootFrame(point_in_viewport);
}

WebViewImpl* WebFrameWidgetImpl::View() const {
  return local_root_->ViewImpl();
}

Page* WebFrameWidgetImpl::GetPage() const {
  return View()->GetPage();
}

mojom::blink::FrameWidgetHost*
WebFrameWidgetImpl::GetAssociatedFrameWidgetHost() const {
  return frame_widget_host_.get();
}

void WebFrameWidgetImpl::RequestDecode(
    const PaintImage& image,
    base::OnceCallback<void(bool)> callback) {
  widget_base_->LayerTreeHost()->QueueImageDecode(image, std::move(callback));
}

void WebFrameWidgetImpl::Trace(Visitor* visitor) const {
  visitor->Trace(local_root_);
  visitor->Trace(current_drag_data_);
  visitor->Trace(frame_widget_host_);
  visitor->Trace(receiver_);
  visitor->Trace(input_target_receiver_);
  visitor->Trace(mouse_capture_element_);
  visitor->Trace(device_emulator_);
}

void WebFrameWidgetImpl::SetNeedsRecalculateRasterScales() {
  if (!View()->does_composite())
    return;
  widget_base_->LayerTreeHost()->SetNeedsRecalculateRasterScales();
}

void WebFrameWidgetImpl::SetBackgroundColor(SkColor color) {
  if (!View()->does_composite())
    return;
  // TODO(crbug/1308932): Remove FromColor and make all SkColor4f.
  widget_base_->LayerTreeHost()->set_background_color(
      SkColor4f::FromColor(color));
}

void WebFrameWidgetImpl::SetOverscrollBehavior(
    const cc::OverscrollBehavior& overscroll_behavior) {
  if (!View()->does_composite())
    return;
  widget_base_->LayerTreeHost()->SetOverscrollBehavior(overscroll_behavior);
}

void WebFrameWidgetImpl::SetPrefersReducedMotion(bool prefers_reduced_motion) {
  if (!View()->does_composite())
    return;
  widget_base_->LayerTreeHost()->SetPrefersReducedMotion(
      prefers_reduced_motion);
}

void WebFrameWidgetImpl::StartPageScaleAnimation(const gfx::Point& destination,
                                                 bool use_anchor,
                                                 float new_page_scale,
                                                 base::TimeDelta duration) {
  widget_base_->LayerTreeHost()->StartPageScaleAnimation(
      destination, use_anchor, new_page_scale, duration);
}

void WebFrameWidgetImpl::RequestBeginMainFrameNotExpected(bool request) {
  if (!View()->does_composite())
    return;
  widget_base_->LayerTreeHost()->RequestBeginMainFrameNotExpected(request);
}

void WebFrameWidgetImpl::DidCommitAndDrawCompositorFrame() {
  ForEachLocalFrameControlledByWidget(
      local_root_->GetFrame(),
      WTF::BindRepeating([](WebLocalFrameImpl* local_frame) {
        local_frame->Client()->DidCommitAndDrawCompositorFrame();
      }));
}

void WebFrameWidgetImpl::DidObserveFirstScrollDelay(
    base::TimeDelta first_scroll_delay,
    base::TimeTicks first_scroll_timestamp) {
  if (!local_root_ || !(local_root_->GetFrame()) ||
      !(local_root_->GetFrame()->GetDocument())) {
    return;
  }
  InteractiveDetector* interactive_detector =
      InteractiveDetector::From(*(local_root_->GetFrame()->GetDocument()));
  if (interactive_detector) {
    interactive_detector->DidObserveFirstScrollDelay(first_scroll_delay,
                                                     first_scroll_timestamp);
  }
}

std::unique_ptr<cc::LayerTreeFrameSink>
WebFrameWidgetImpl::AllocateNewLayerTreeFrameSink() {
  return nullptr;
}

void WebFrameWidgetImpl::DidBeginMainFrame() {
  LocalFrame* root_frame = LocalRootImpl()->GetFrame();
  DCHECK(root_frame);

  if (LocalFrameView* frame_view = root_frame->View())
    frame_view->RunPostLifecycleSteps();
  if (Page* page = root_frame->GetPage())
    page->Animator().PostAnimate();
}

void WebFrameWidgetImpl::UpdateLifecycle(WebLifecycleUpdate requested_update,
                                         DocumentUpdateReason reason) {
  TRACE_EVENT0("blink", "WebFrameWidgetImpl::UpdateLifecycle");
  if (!LocalRootImpl())
    return;

  GetPage()->UpdateLifecycle(*LocalRootImpl()->GetFrame(), requested_update,
                             reason);
  if (requested_update != WebLifecycleUpdate::kAll)
    return;

  View()->UpdatePagePopup();

  // Meaningful layout events and background colors only apply to main frames.
  if (ForMainFrame()) {
    MainFrameData& data = main_data();

    // There is no background color for non-composited WebViews (eg
    // printing).
    if (View()->does_composite()) {
      SkColor background_color = View()->BackgroundColor();
      SetBackgroundColor(background_color);
      if (background_color != data.last_background_color) {
        LocalRootImpl()->GetFrame()->DidChangeBackgroundColor(
            background_color, false /* color_adjust */);
        data.last_background_color = background_color;
      }
    }

    if (LocalFrameView* view = LocalRootImpl()->GetFrameView()) {
      LocalFrame* frame = LocalRootImpl()->GetFrame();

      if (data.should_dispatch_first_visually_non_empty_layout &&
          view->IsVisuallyNonEmpty()) {
        data.should_dispatch_first_visually_non_empty_layout = false;
        // TODO(esprehn): Move users of this callback to something
        // better, the heuristic for "visually non-empty" is bad.
        DidMeaningfulLayout(WebMeaningfulLayout::kVisuallyNonEmpty);
      }

      if (data.should_dispatch_first_layout_after_finished_parsing &&
          frame->GetDocument()->HasFinishedParsing()) {
        data.should_dispatch_first_layout_after_finished_parsing = false;
        DidMeaningfulLayout(WebMeaningfulLayout::kFinishedParsing);
      }

      if (data.should_dispatch_first_layout_after_finished_loading &&
          frame->GetDocument()->IsLoadCompleted()) {
        data.should_dispatch_first_layout_after_finished_loading = false;
        DidMeaningfulLayout(WebMeaningfulLayout::kFinishedLoading);
      }
    }
  }
}

void WebFrameWidgetImpl::OnDeferCommitsChanged(
    bool defer_status,
    cc::PaintHoldingReason reason,
    absl::optional<cc::PaintHoldingCommitTrigger> trigger) {
  GetPage()->GetChromeClient().OnDeferCommitsChanged(defer_status, reason,
                                                     trigger);
}

void WebFrameWidgetImpl::DidCompletePageScaleAnimation() {
  // Page scale animations only happen on the main frame.
  DCHECK(ForMainFrame());
  if (auto* focused_frame = View()->FocusedFrame()) {
    if (focused_frame->AutofillClient())
      focused_frame->AutofillClient()->DidCompleteFocusChangeInFrame();
  }

  if (page_scale_animation_for_testing_callback_)
    std::move(page_scale_animation_for_testing_callback_).Run();
}

void WebFrameWidgetImpl::ScheduleAnimation() {
  if (!View()->does_composite()) {
    non_composited_client_->ScheduleNonCompositedAnimation();
    return;
  }
  widget_base_->LayerTreeHost()->SetNeedsAnimate();
}

void WebFrameWidgetImpl::FocusChanged(mojom::blink::FocusState focus_state) {
  // TODO(crbug.com/689777): FocusChange events are only sent to the MainFrame
  // these maybe should goto the local root so that the rest of input messages
  // sent to those are preserved in order.
  DCHECK(ForMainFrame());
  View()->SetIsActive(focus_state == mojom::blink::FocusState::kFocused ||
                      focus_state ==
                          mojom::blink::FocusState::kNotFocusedAndActive);
  View()->SetPageFocus(focus_state == mojom::blink::FocusState::kFocused);
}

bool WebFrameWidgetImpl::ShouldAckSyntheticInputImmediately() {
  // TODO(bokan): The RequestPresentation API appears not to function in VR. As
  // a short term workaround for https://crbug.com/940063, ACK input
  // immediately rather than using RequestPresentation.
  if (GetPage()->GetSettings().GetImmersiveModeEnabled())
    return true;
  return false;
}

void WebFrameWidgetImpl::UpdateVisualProperties(
    const VisualProperties& visual_properties) {
  SetZoomLevel(visual_properties.zoom_level);

  // TODO(danakj): In order to synchronize updates between local roots, the
  // display mode should be propagated to RenderFrameProxies and down through
  // their RenderWidgetHosts to child WebFrameWidgetImpl via the
  // VisualProperties waterfall, instead of coming to each WebFrameWidgetImpl
  // independently.
  // https://developer.mozilla.org/en-US/docs/Web/CSS/@media/display-mode
  SetDisplayMode(visual_properties.display_mode);

  if (ForMainFrame()) {
    SetAutoResizeMode(
        visual_properties.auto_resize_enabled,
        visual_properties.min_size_for_auto_resize,
        visual_properties.max_size_for_auto_resize,
        visual_properties.screen_infos.current().device_scale_factor);
  }

  bool capture_sequence_number_changed =
      visual_properties.capture_sequence_number !=
      last_capture_sequence_number_;
  if (capture_sequence_number_changed) {
    last_capture_sequence_number_ = visual_properties.capture_sequence_number;

    // Send the capture sequence number to RemoteFrames that are below the
    // local root for this widget.
    ForEachRemoteFrameControlledByWidget(WTF::BindRepeating(
        [](uint32_t capture_sequence_number, RemoteFrame* remote_frame) {
          remote_frame->UpdateCaptureSequenceNumber(capture_sequence_number);
        },
        visual_properties.capture_sequence_number));
  }

  if (!View()->AutoResizeMode()) {
    // This needs to run before ApplyVisualPropertiesSizing below,
    // which updates the current set of screen_infos from visual properties.
    if (DidChangeFullscreenState(visual_properties)) {
      is_fullscreen_granted_ = visual_properties.is_fullscreen_granted;
      if (is_fullscreen_granted_)
        View()->DidEnterFullscreen();
      else
        View()->DidExitFullscreen();
    }
  }

  gfx::Size old_visible_viewport_size_in_dips =
      widget_base_->VisibleViewportSizeInDIPs();
  ApplyVisualPropertiesSizing(visual_properties);

  if (old_visible_viewport_size_in_dips !=
      widget_base_->VisibleViewportSizeInDIPs()) {
    ForEachLocalFrameControlledByWidget(
        local_root_->GetFrame(),
        WTF::BindRepeating([](WebLocalFrameImpl* local_frame) {
          local_frame->ResetHasScrolledFocusedEditableIntoView();
        }));

    // Propagate changes down to child local root RenderWidgets and
    // BrowserPlugins in other frame trees/processes.
    ForEachRemoteFrameControlledByWidget(WTF::BindRepeating(
        [](const gfx::Size& visible_viewport_size, RemoteFrame* remote_frame) {
          remote_frame->DidChangeVisibleViewportSize(visible_viewport_size);
        },
        widget_base_->VisibleViewportSizeInDIPs()));
  }

  // All non-top-level Widgets (child local-root frames, Portals, GuestViews,
  // etc.) propagate and consume the page scale factor as "external", meaning
  // that it comes from the top level widget's page scale.
  if (!ForTopMostMainFrame()) {
    // The main frame controls the page scale factor, from blink. For other
    // frame widgets, the page scale from pinch zoom and compositing scale is
    // received from its parent as part of the visual properties here. While
    // blink doesn't need to know this page scale factor outside the main frame,
    // the compositor does in order to produce its output at the correct scale.
    float combined_scale_factor = visual_properties.page_scale_factor *
                                  visual_properties.compositing_scale_factor;
    widget_base_->LayerTreeHost()->SetExternalPageScaleFactor(
        combined_scale_factor, visual_properties.is_pinch_gesture_active);

    NotifyPageScaleFactorChanged(visual_properties.page_scale_factor,
                                 visual_properties.is_pinch_gesture_active);

    NotifyCompositingScaleFactorChanged(
        visual_properties.compositing_scale_factor);
  } else {
    // Ensure the external scale factor in top-level widgets is reset as it may
    // be leftover from when a widget was nested and was promoted to top level
    // (e.g. portal activation).
    widget_base_->LayerTreeHost()->SetExternalPageScaleFactor(
        1.f,
        /*is_pinch_gesture_active=*/false);
  }

  // TODO(crbug.com/939118): This code path where scroll_focused_node_into_view
  // is set is used only for WebView, crbug 939118 tracks fixing webviews to
  // not use scroll_focused_node_into_view.
  if (visual_properties.scroll_focused_node_into_view)
    ScrollFocusedEditableElementIntoView();
}

void WebFrameWidgetImpl::ApplyVisualPropertiesSizing(
    const VisualProperties& visual_properties) {
  gfx::Rect new_compositor_viewport_pixel_rect =
      visual_properties.compositor_viewport_pixel_rect;
  if (ForMainFrame()) {
    if (size_ !=
        widget_base_->DIPsToCeiledBlinkSpace(visual_properties.new_size)) {
      // Only hide popups when the size changes. Eg https://crbug.com/761908.
      View()->CancelPagePopup();
    }

    if (auto* device_emulator = DeviceEmulator()) {
      device_emulator->UpdateVisualProperties(visual_properties);
      return;
    }

    if (AutoResizeMode()) {
      new_compositor_viewport_pixel_rect = gfx::Rect(gfx::ScaleToCeiledSize(
          widget_base_->BlinkSpaceToFlooredDIPs(size_.value_or(gfx::Size())),
          visual_properties.screen_infos.current().device_scale_factor));
    }
  }

  SetWindowSegments(visual_properties.root_widget_window_segments);

  widget_base_->UpdateSurfaceAndScreenInfo(
      visual_properties.local_surface_id.value_or(viz::LocalSurfaceId()),
      new_compositor_viewport_pixel_rect, visual_properties.screen_infos);

  // Store this even when auto-resizing, it is the size of the full viewport
  // used for clipping, and this value is propagated down the Widget
  // hierarchy via the VisualProperties waterfall.
  widget_base_->SetVisibleViewportSizeInDIPs(
      visual_properties.visible_viewport_size);

  if (ForMainFrame()) {
    if (!AutoResizeMode()) {
      size_ = widget_base_->DIPsToCeiledBlinkSpace(visual_properties.new_size);

      View()->ResizeWithBrowserControls(
          size_.value(),
          widget_base_->DIPsToCeiledBlinkSpace(
              widget_base_->VisibleViewportSizeInDIPs()),
          visual_properties.browser_controls_params);
    }

#if !BUILDFLAG(IS_ANDROID)
    LocalRootImpl()->GetFrame()->UpdateWindowControlsOverlay(
        visual_properties.window_controls_overlay_rect);
#endif

  } else {
    // Widgets in a WebView's frame tree without a local main frame
    // set the size of the WebView to be the |visible_viewport_size|, in order
    // to limit compositing in (out of process) child frames to what is visible.
    //
    // Note that child frames in the same process/WebView frame tree as the
    // main frame do not do this in order to not clobber the source of truth in
    // the main frame.
    if (!View()->MainFrameImpl()) {
      View()->Resize(widget_base_->DIPsToCeiledBlinkSpace(
          widget_base_->VisibleViewportSizeInDIPs()));
    }

    Resize(widget_base_->DIPsToCeiledBlinkSpace(visual_properties.new_size));
  }
}

bool WebFrameWidgetImpl::DidChangeFullscreenState(
    const VisualProperties& visual_properties) const {
  if (visual_properties.is_fullscreen_granted != is_fullscreen_granted_)
    return true;
  // If changing fullscreen from one display to another, the fullscreen
  // granted state will not change, but we still need to resolve promises
  // by considering this a change.
  return visual_properties.is_fullscreen_granted &&
         widget_base_->screen_infos().current().display_id !=
             visual_properties.screen_infos.current().display_id;
}

int WebFrameWidgetImpl::GetLayerTreeId() {
  if (!View()->does_composite())
    return 0;
  return widget_base_->LayerTreeHost()->GetId();
}

const cc::LayerTreeSettings& WebFrameWidgetImpl::GetLayerTreeSettings() {
  return widget_base_->LayerTreeHost()->GetSettings();
}

void WebFrameWidgetImpl::UpdateBrowserControlsState(
    cc::BrowserControlsState constraints,
    cc::BrowserControlsState current,
    bool animate) {
  DCHECK(View()->does_composite());
  widget_base_->LayerTreeHost()->UpdateBrowserControlsState(constraints,
                                                            current, animate);
}

void WebFrameWidgetImpl::SetHaveScrollEventHandlers(bool has_handlers) {
  widget_base_->LayerTreeHost()->SetHaveScrollEventHandlers(has_handlers);
}

void WebFrameWidgetImpl::SetEventListenerProperties(
    cc::EventListenerClass listener_class,
    cc::EventListenerProperties listener_properties) {
  widget_base_->LayerTreeHost()->SetEventListenerProperties(
      listener_class, listener_properties);

  if (listener_class == cc::EventListenerClass::kTouchStartOrMove ||
      listener_class == cc::EventListenerClass::kTouchEndOrCancel) {
    bool has_touch_handlers =
        EventListenerProperties(cc::EventListenerClass::kTouchStartOrMove) !=
            cc::EventListenerProperties::kNone ||
        EventListenerProperties(cc::EventListenerClass::kTouchEndOrCancel) !=
            cc::EventListenerProperties::kNone;
    if (!has_touch_handlers_ || *has_touch_handlers_ != has_touch_handlers) {
      has_touch_handlers_ = has_touch_handlers;

      widget_base_->WidgetScheduler()->SetHasTouchHandler(has_touch_handlers);
      // Set touch event consumers based on whether there are touch event
      // handlers or the page has hit testable scrollbars.
      auto touch_event_consumers = mojom::blink::TouchEventConsumers::New(
          has_touch_handlers, GetPage()->GetScrollbarTheme().AllowsHitTest());
      frame_widget_host_->SetHasTouchEventConsumers(
          std::move(touch_event_consumers));
    }
  } else if (listener_class == cc::EventListenerClass::kPointerRawUpdate) {
    SetHasPointerRawUpdateEventHandlers(listener_properties !=
                                        cc::EventListenerProperties::kNone);
  }
}

cc::EventListenerProperties WebFrameWidgetImpl::EventListenerProperties(
    cc::EventListenerClass listener_class) const {
  return widget_base_->LayerTreeHost()->event_listener_properties(
      listener_class);
}

mojom::blink::DisplayMode WebFrameWidgetImpl::DisplayMode() const {
  return display_mode_;
}

const WebVector<gfx::Rect>& WebFrameWidgetImpl::WindowSegments() const {
  return window_segments_;
}

bool WebFrameWidgetImpl::StartDeferringCommits(base::TimeDelta timeout,
                                               cc::PaintHoldingReason reason) {
  if (!View()->does_composite())
    return false;
  return widget_base_->LayerTreeHost()->StartDeferringCommits(timeout, reason);
}

void WebFrameWidgetImpl::StopDeferringCommits(
    cc::PaintHoldingCommitTrigger triggger) {
  if (!View()->does_composite())
    return;
  widget_base_->LayerTreeHost()->StopDeferringCommits(triggger);
}

std::unique_ptr<cc::ScopedDeferMainFrameUpdate>
WebFrameWidgetImpl::DeferMainFrameUpdate() {
  return widget_base_->LayerTreeHost()->DeferMainFrameUpdate();
}

void WebFrameWidgetImpl::SetBrowserControlsShownRatio(float top_ratio,
                                                      float bottom_ratio) {
  widget_base_->LayerTreeHost()->SetBrowserControlsShownRatio(top_ratio,
                                                              bottom_ratio);
}

void WebFrameWidgetImpl::SetBrowserControlsParams(
    cc::BrowserControlsParams params) {
  widget_base_->LayerTreeHost()->SetBrowserControlsParams(params);
}

void WebFrameWidgetImpl::SynchronouslyCompositeForTesting(
    base::TimeTicks frame_time) {
  widget_base_->LayerTreeHost()->CompositeForTest(frame_time, false);
}

void WebFrameWidgetImpl::SetDeviceColorSpaceForTesting(
    const gfx::ColorSpace& color_space) {
  DCHECK(ForMainFrame());
  // We are changing the device color space from the renderer, so allocate a
  // new viz::LocalSurfaceId to avoid surface invariants violations in tests.
  widget_base_->LayerTreeHost()->RequestNewLocalSurfaceId();

  display::ScreenInfos screen_infos = widget_base_->screen_infos();
  for (display::ScreenInfo& screen_info : screen_infos.screen_infos)
    screen_info.display_color_spaces = gfx::DisplayColorSpaces(color_space);
  widget_base_->UpdateScreenInfo(screen_infos);
}

// TODO(665924): Remove direct dispatches of mouse events from
// PointerLockController, instead passing them through EventHandler.
void WebFrameWidgetImpl::PointerLockMouseEvent(
    const WebCoalescedInputEvent& coalesced_event) {
  const WebInputEvent& input_event = coalesced_event.Event();
  const WebMouseEvent& mouse_event =
      static_cast<const WebMouseEvent&>(input_event);
  WebMouseEvent transformed_event =
      TransformWebMouseEvent(local_root_->GetFrameView(), mouse_event);

  AtomicString event_type;
  switch (input_event.GetType()) {
    case WebInputEvent::Type::kMouseDown:
      event_type = event_type_names::kMousedown;
      if (!GetPage() || !GetPage()->GetPointerLockController().GetElement())
        break;
      LocalFrame::NotifyUserActivation(
          GetPage()
              ->GetPointerLockController()
              .GetElement()
              ->GetDocument()
              .GetFrame(),
          mojom::blink::UserActivationNotificationType::kInteraction);
      break;
    case WebInputEvent::Type::kMouseUp:
      event_type = event_type_names::kMouseup;
      break;
    case WebInputEvent::Type::kMouseMove:
      event_type = event_type_names::kMousemove;
      break;
    case WebInputEvent::Type::kMouseEnter:
    case WebInputEvent::Type::kMouseLeave:
    case WebInputEvent::Type::kContextMenu:
      // These should not be normally dispatched but may be due to timing
      // because pointer lost messaging happens on separate mojo channel.
      return;
    default:
      NOTREACHED() << input_event.GetType();
  }

  if (GetPage()) {
    GetPage()->GetPointerLockController().DispatchLockedMouseEvent(
        transformed_event,
        TransformWebMouseEventVector(
            local_root_->GetFrameView(),
            coalesced_event.GetCoalescedEventsPointers()),
        TransformWebMouseEventVector(
            local_root_->GetFrameView(),
            coalesced_event.GetPredictedEventsPointers()),
        event_type);
  }
}
bool WebFrameWidgetImpl::IsPointerLocked() {
  if (GetPage()) {
    return GetPage()->GetPointerLockController().IsPointerLocked();
  }
  return false;
}

void WebFrameWidgetImpl::ShowContextMenu(
    ui::mojom::blink::MenuSourceType source_type,
    const gfx::Point& location) {
  host_context_menu_location_ = location;

  if (!GetPage())
    return;
  GetPage()->GetContextMenuController().ClearContextMenu();
  {
    ContextMenuAllowedScope scope;
    if (LocalFrame* focused_frame =
            GetPage()->GetFocusController().FocusedFrame()) {
      focused_frame->GetEventHandler().ShowNonLocatedContextMenu(
          nullptr, static_cast<blink::WebMenuSourceType>(source_type));
    }
  }
  host_context_menu_location_.reset();
}

void WebFrameWidgetImpl::SetViewportIntersection(
    mojom::blink::ViewportIntersectionStatePtr intersection_state,
    const absl::optional<VisualProperties>& visual_properties) {
  // Remote viewports are only applicable to local frames with remote ancestors.
  // TODO(https://crbug.com/1148960): Should this deal with portals?
  DCHECK(ForSubframe() || !LocalRootImpl()->GetFrame()->IsOutermostMainFrame());

  if (visual_properties.has_value())
    UpdateVisualProperties(visual_properties.value());
  ApplyViewportIntersection(std::move(intersection_state));
}

void WebFrameWidgetImpl::ApplyViewportIntersectionForTesting(
    mojom::blink::ViewportIntersectionStatePtr intersection_state) {
  ApplyViewportIntersection(std::move(intersection_state));
}

void WebFrameWidgetImpl::ApplyViewportIntersection(
    mojom::blink::ViewportIntersectionStatePtr intersection_state) {
  if (ForSubframe()) {
    // This information is propagated to LTH to define the region for filling
    // the on-screen text content.
    // TODO(khushalsagar) : This needs to also be done for main frames which are
    // embedded pages (see Frame::IsOutermostMainFrame()).
    child_data().compositor_visible_rect =
        intersection_state->compositor_visible_rect;
    widget_base_->LayerTreeHost()->SetVisualDeviceViewportIntersectionRect(
        intersection_state->compositor_visible_rect);
  }
  LocalRootImpl()->GetFrame()->SetViewportIntersectionFromParent(
      *intersection_state);
}

void WebFrameWidgetImpl::EnableDeviceEmulation(
    const DeviceEmulationParams& parameters) {
  // Device Emaulation is only supported for the main frame.
  DCHECK(ForMainFrame());
  if (!device_emulator_) {
    gfx::Size size_in_dips = widget_base_->BlinkSpaceToFlooredDIPs(Size());

    device_emulator_ = MakeGarbageCollected<ScreenMetricsEmulator>(
        this, widget_base_->screen_infos(), size_in_dips,
        widget_base_->VisibleViewportSizeInDIPs(),
        widget_base_->WidgetScreenRect(), widget_base_->WindowScreenRect());
  }
  device_emulator_->ChangeEmulationParams(parameters);
}

void WebFrameWidgetImpl::DisableDeviceEmulation() {
  if (!device_emulator_)
    return;
  device_emulator_->DisableAndApply();
  device_emulator_ = nullptr;
}

void WebFrameWidgetImpl::SetIsInertForSubFrame(bool inert) {
  DCHECK(ForSubframe());
  LocalRootImpl()->GetFrame()->SetIsInert(inert);
}

absl::optional<gfx::Point>
WebFrameWidgetImpl::GetAndResetContextMenuLocation() {
  return std::move(host_context_menu_location_);
}

void WebFrameWidgetImpl::SetZoomLevel(double zoom_level) {
  // Override the zoom level with the testing one if necessary.
  if (zoom_level_for_testing_ != -INFINITY)
    zoom_level = zoom_level_for_testing_;

  // Set the layout shift exclusion window for the zoom level change.
  if (View()->ZoomLevel() != zoom_level)
    NotifyZoomLevelChanged(LocalRootImpl()->GetFrame());

  View()->SetZoomLevel(zoom_level);

  // Part of the UpdateVisualProperties dance we send the zoom level to
  // RemoteFrames that are below the local root for this widget.
  ForEachRemoteFrameControlledByWidget(WTF::BindRepeating(
      [](double zoom_level, RemoteFrame* remote_frame) {
        remote_frame->ZoomLevelChanged(zoom_level);
      },
      zoom_level));
}

void WebFrameWidgetImpl::SetAutoResizeMode(bool auto_resize,
                                           const gfx::Size& min_window_size,
                                           const gfx::Size& max_window_size,
                                           float device_scale_factor) {
  // Auto resize only applies to main frames.
  DCHECK(ForMainFrame());

  if (auto_resize) {
    View()->EnableAutoResizeMode(
        gfx::ScaleToCeiledSize(min_window_size, device_scale_factor),
        gfx::ScaleToCeiledSize(max_window_size, device_scale_factor));
  } else if (AutoResizeMode()) {
    View()->DisableAutoResizeMode();
  }
}

void WebFrameWidgetImpl::DidAutoResize(const gfx::Size& size) {
  DCHECK(ForMainFrame());
  gfx::Size size_in_dips = widget_base_->BlinkSpaceToFlooredDIPs(size);
  size_ = size;

  // TODO(ccameron): Note that this destroys any information differentiating
  // |size| from the compositor's viewport size.
  gfx::Rect size_with_dsf = gfx::Rect(gfx::ScaleToCeiledSize(
      gfx::Rect(size_in_dips).size(),
      widget_base_->GetScreenInfo().device_scale_factor));
  widget_base_->LayerTreeHost()->RequestNewLocalSurfaceId();
  widget_base_->UpdateCompositorViewportRect(size_with_dsf);
}

LocalFrame* WebFrameWidgetImpl::FocusedLocalFrameInWidget() const {
  if (!local_root_) {
    // WebFrameWidget is created in the call to CreateFrame. The corresponding
    // RenderWidget, however, might not swap in right away (InstallNewDocument()
    // will lead to it swapping in). During this interval local_root_ is nullptr
    // (see https://crbug.com/792345).
    return nullptr;
  }

  LocalFrame* frame = GetPage()->GetFocusController().FocusedFrame();
  return (frame && frame->LocalFrameRoot() == local_root_->GetFrame())
             ? frame
             : nullptr;
}

WebLocalFrameImpl* WebFrameWidgetImpl::FocusedWebLocalFrameInWidget() const {
  return WebLocalFrameImpl::FromFrame(FocusedLocalFrameInWidget());
}

bool WebFrameWidgetImpl::ScrollFocusedEditableElementIntoView() {
  Element* element = FocusedElement();
  if (!element)
    return false;

  EditContext* edit_context = element->GetDocument()
                                  .GetFrame()
                                  ->GetInputMethodController()
                                  .GetActiveEditContext();

  if (!WebElement(element).IsEditable() && !edit_context)
    return false;

  element->GetDocument().UpdateStyleAndLayout(DocumentUpdateReason::kSelection);

  if (!element->GetLayoutObject())
    return false;

  // The page scale animation started by ZoomAndScrollToFocusedEditableRect
  // will scroll only the visual and layout viewports. Call ScrollRectToVisible
  // first to ensure the editable is visible within the document (i.e. scroll
  // it into view in any subscrollers). By setting `for_focused_editable`,
  // ScrollRectToVisible will stop bubbling when it reaches the layout viewport
  // so that can be animated by the PageScaleAnimation.
  mojom::blink::ScrollIntoViewParamsPtr params =
      ScrollAlignment::CreateScrollIntoViewParams(
          ScrollAlignment::CenterIfNeeded(), ScrollAlignment::CenterIfNeeded(),
          mojom::blink::ScrollType::kProgrammatic,
          /*make_visible_in_visual_viewport=*/false,
          mojom::blink::ScrollBehavior::kInstant);
  params->for_focused_editable = mojom::blink::FocusedEditableParams::New();
  params->for_focused_editable->relative_location = gfx::Vector2dF();
  params->for_focused_editable->size = gfx::SizeF();

  // When deciding whether to zoom in on a focused text box, we should
  // decide not to zoom in if the user won't be able to zoom out. e.g if the
  // textbox is within a touch-action: none container the user can't zoom
  // back out.
  TouchAction action = touch_action_util::ComputeEffectiveTouchAction(*element);
  params->for_focused_editable->can_zoom =
      static_cast<int>(action) & static_cast<int>(TouchAction::kPinchZoom);

  PhysicalRect absolute_element_bounds;
  PhysicalRect absolute_caret_bounds;

  if (edit_context) {
    gfx::Rect control_bounds_in_physical_pixels;
    gfx::Rect selection_bounds_in_physical_pixels;
    edit_context->GetLayoutBounds(&control_bounds_in_physical_pixels,
                                  &selection_bounds_in_physical_pixels);

    absolute_element_bounds = PhysicalRect(control_bounds_in_physical_pixels);
    absolute_caret_bounds = PhysicalRect(selection_bounds_in_physical_pixels);
  } else {
    absolute_element_bounds =
        PhysicalRect(element->GetLayoutObject()->AbsoluteBoundingBoxRect());
    absolute_caret_bounds = PhysicalRect(
        element->GetDocument().GetFrame()->Selection().ComputeRectToScroll(
            kRevealExtent));
  }

  gfx::Vector2dF editable_offset_from_caret(absolute_element_bounds.offset -
                                            absolute_caret_bounds.offset);
  gfx::SizeF editable_size(absolute_element_bounds.size);

  params->for_focused_editable->relative_location = editable_offset_from_caret;
  params->for_focused_editable->size = editable_size;

  scroll_into_view_util::ScrollRectToVisible(
      *element->GetLayoutObject(), absolute_caret_bounds, std::move(params));

  return true;
}

void WebFrameWidgetImpl::ResetMeaningfulLayoutStateForMainFrame() {
  MainFrameData& data = main_data();
  data.should_dispatch_first_visually_non_empty_layout = true;
  data.should_dispatch_first_layout_after_finished_parsing = true;
  data.should_dispatch_first_layout_after_finished_loading = true;
  data.last_background_color.reset();
}

void WebFrameWidgetImpl::InitializeCompositing(
    scheduler::WebAgentGroupScheduler& agent_group_scheduler,
    const display::ScreenInfos& screen_infos,
    const cc::LayerTreeSettings* settings) {
  DCHECK(View()->does_composite());
  DCHECK(!non_composited_client_);  // Assure only one initialize is called.
  widget_base_->InitializeCompositing(
      *GetPage()->GetPageScheduler(), screen_infos, settings,
      input_handler_weak_ptr_factory_.GetWeakPtr());

  // TODO(bokan): This seems wrong. Page may host multiple FrameWidgets so this
  // will call DidInitializeCompositing once per FrameWidget. It probably makes
  // sense to move LinkHighlight from Page to WidgetBase so initialization is
  // per-widget. See also: https://crbug.com/1344531.
  GetPage()->DidInitializeCompositing(*AnimationHost());
}

void WebFrameWidgetImpl::InitializeNonCompositing(
    WebNonCompositedWidgetClient* client) {
  DCHECK(!non_composited_client_);
  DCHECK(client);
  DCHECK(!View()->does_composite());
  widget_base_->InitializeNonCompositing();
  non_composited_client_ = client;
}

void WebFrameWidgetImpl::SetCompositorVisible(bool visible) {
  widget_base_->SetCompositorVisible(visible);
}

gfx::Size WebFrameWidgetImpl::Size() {
  return size_.value_or(gfx::Size());
}

void WebFrameWidgetImpl::Resize(const gfx::Size& new_size) {
  if (size_ && *size_ == new_size)
    return;

  if (ForMainFrame()) {
    size_ = new_size;
    View()->Resize(new_size);
    return;
  }

  if (child_data().did_suspend_parsing) {
    child_data().did_suspend_parsing = false;
    LocalRootImpl()->GetFrame()->Loader().GetDocumentLoader()->ResumeParser();
  }

  LocalFrameView* view = LocalRootImpl()->GetFrameView();
  DCHECK(view);

  size_ = new_size;

  view->SetLayoutSize(*size_);
  view->Resize(*size_);
}

void WebFrameWidgetImpl::BeginMainFrame(base::TimeTicks last_frame_time) {
  TRACE_EVENT1("blink", "WebFrameWidgetImpl::BeginMainFrame", "frameTime",
               last_frame_time);
  DCHECK(!last_frame_time.is_null());
  CHECK(LocalRootImpl());

  // Dirty bit on MouseEventManager is not cleared in OOPIFs after scroll
  // or layout changes. Ensure the hover state is recomputed if necessary.
  LocalRootImpl()
      ->GetFrame()
      ->GetEventHandler()
      .RecomputeMouseHoverStateIfNeeded();

  ForEachLocalFrameControlledByWidget(
      LocalRootImpl()->GetFrame(),
      WTF::BindRepeating([](WebLocalFrameImpl* local_frame) {
        if (LocalFrameView* view = local_frame->GetFrameView()) {
          if (FragmentAnchor* anchor = view->GetFragmentAnchor())
            anchor->PerformScriptableActions();
        }
      }));

  absl::optional<LocalFrameUkmAggregator::ScopedUkmHierarchicalTimer> ukm_timer;
  if (WidgetBase::ShouldRecordBeginMainFrameMetrics()) {
    ukm_timer.emplace(LocalRootImpl()
                          ->GetFrame()
                          ->View()
                          ->EnsureUkmAggregator()
                          .GetScopedTimer(LocalFrameUkmAggregator::kAnimate));
  }

  GetPage()->Animate(last_frame_time);
  // Animate can cause the local frame to detach.
  if (!LocalRootImpl())
    return;

  GetPage()->GetValidationMessageClient().LayoutOverlay();
}

void WebFrameWidgetImpl::BeginCommitCompositorFrame() {
  commit_compositor_frame_start_time_.emplace(base::TimeTicks::Now());
  probe::LayerTreePainted(LocalRootImpl()->GetFrame());
  if (ForTopMostMainFrame()) {
    Document* doc = local_root_->GetFrame()->GetDocument();
    if (doc->GetSettings()->GetViewportMetaEnabled() &&
        !LayerTreeHost()->IsMobileOptimized()) {
      UseCounter::Count(doc, WebFeature::kTapDelayEnabled);
    }
  }
  if (ForMainFrame()) {
    View()->DidCommitCompositorFrameForLocalMainFrame();
    View()->UpdatePreferredSize();
    if (!View()->MainFrameImpl()) {
      // Trying to track down why the view's idea of the main frame varies
      // from LocalRootImpl's.
      // TODO(https://crbug.com/1139104): Remove this.
      std::string reason = View()->GetNullFrameReasonForBug1139104();
      DCHECK(false) << reason;
      SCOPED_CRASH_KEY_STRING32("Crbug1139104", "NullFrameReason", reason);
      base::debug::DumpWithoutCrashing();
    }
  }
}

void WebFrameWidgetImpl::EndCommitCompositorFrame(
    base::TimeTicks commit_start_time,
    base::TimeTicks commit_finish_time) {
  DCHECK(commit_compositor_frame_start_time_.has_value());
  LocalRootImpl()
      ->GetFrame()
      ->View()
      ->EnsureUkmAggregator()
      .RecordImplCompositorSample(commit_compositor_frame_start_time_.value(),
                                  commit_start_time, commit_finish_time);
  commit_compositor_frame_start_time_.reset();
}

void WebFrameWidgetImpl::ApplyViewportChanges(
    const ApplyViewportChangesArgs& args) {
  // Viewport changes only change the outermost main frame. Technically a
  // portal has a viewport but it cannot produce changes from the compositor
  // until activated so this should be correct for portals too.
  if (!LocalRootImpl()->GetFrame()->IsOutermostMainFrame())
    return;

  WebViewImpl* web_view = View();
  // TODO(https://crbug.com/1160652): Figure out if View is null.
  CHECK(widget_base_);
  CHECK(web_view);
  web_view->ApplyViewportChanges(args);
}

void WebFrameWidgetImpl::RecordManipulationTypeCounts(
    cc::ManipulationInfo info) {
  // Manipulation counts are only recorded for the main frame.
  if (!ForMainFrame())
    return;

  if ((info & cc::kManipulationInfoWheel) == cc::kManipulationInfoWheel) {
    UseCounter::Count(LocalRootImpl()->GetDocument(),
                      WebFeature::kScrollByWheel);
  }
  if ((info & cc::kManipulationInfoTouch) == cc::kManipulationInfoTouch) {
    UseCounter::Count(LocalRootImpl()->GetDocument(),
                      WebFeature::kScrollByTouch);
  }
  if ((info & cc::kManipulationInfoPinchZoom) ==
      cc::kManipulationInfoPinchZoom) {
    UseCounter::Count(LocalRootImpl()->GetDocument(), WebFeature::kPinchZoom);
  }
  if ((info & cc::kManipulationInfoPrecisionTouchPad) ==
      cc::kManipulationInfoPrecisionTouchPad) {
    UseCounter::Count(LocalRootImpl()->GetDocument(),
                      WebFeature::kScrollByPrecisionTouchPad);
  }
}

void WebFrameWidgetImpl::RecordDispatchRafAlignedInputTime(
    base::TimeTicks raf_aligned_input_start_time) {
  if (LocalRootImpl()) {
    LocalRootImpl()
        ->GetFrame()
        ->View()
        ->EnsureUkmAggregator()
        .RecordTimerSample(LocalFrameUkmAggregator::kHandleInputEvents,
                           raf_aligned_input_start_time,
                           base::TimeTicks::Now());
  }
}

void WebFrameWidgetImpl::SetSuppressFrameRequestsWorkaroundFor704763Only(
    bool suppress_frame_requests) {
  GetPage()->Animator().SetSuppressFrameRequestsWorkaroundFor704763Only(
      suppress_frame_requests);
}

void WebFrameWidgetImpl::CountDroppedPointerDownForEventTiming(unsigned count) {
  if (!local_root_ || !(local_root_->GetFrame()) ||
      !(local_root_->GetFrame()->DomWindow())) {
    return;
  }
  WindowPerformance* performance = DOMWindowPerformance::performance(
      *(local_root_->GetFrame()->DomWindow()));

  performance->eventCounts()->AddMultipleEvents(event_type_names::kPointerdown,
                                                count);
  // We only count dropped touchstart that can trigger pointerdown.
  performance->eventCounts()->AddMultipleEvents(event_type_names::kTouchstart,
                                                count);
  // TouchEnd will not be dropped. But in touch event model only touch starts
  // can set the target and after that the touch event always goes to that
  // target. So if a touchstart has been dropped, the following touchend will
  // not be dispatched. Meanwhile, the pointerup can be captured in the
  // pointer_event_manager.
  performance->eventCounts()->AddMultipleEvents(event_type_names::kTouchend,
                                                count);
}

std::unique_ptr<cc::BeginMainFrameMetrics>
WebFrameWidgetImpl::GetBeginMainFrameMetrics() {
  if (!LocalRootImpl())
    return nullptr;

  return LocalRootImpl()
      ->GetFrame()
      ->View()
      ->EnsureUkmAggregator()
      .GetBeginMainFrameMetrics();
}

std::unique_ptr<cc::WebVitalMetrics> WebFrameWidgetImpl::GetWebVitalMetrics() {
  if (!LocalRootImpl())
    return nullptr;

  // This class should be called at most once per commit.
  WebPerformance perf = LocalRootImpl()->Performance();
  auto metrics = std::make_unique<cc::WebVitalMetrics>();
  if (perf.FirstInputDelay().has_value()) {
    metrics->first_input_delay = perf.FirstInputDelay().value();
    metrics->has_fid = true;
  }

  base::TimeTicks start = perf.NavigationStartAsMonotonicTime();
  base::TimeTicks largest_contentful_paint =
      perf.LargestContentfulPaintAsMonotonicTime();
  if (largest_contentful_paint >= start) {
    metrics->largest_contentful_paint = largest_contentful_paint - start;
    metrics->has_lcp = true;
  }

  double layout_shift = LocalRootImpl()
                            ->GetFrame()
                            ->View()
                            ->GetLayoutShiftTracker()
                            .WeightedScore();
  if (layout_shift > 0.f) {
    metrics->layout_shift = layout_shift;
    metrics->has_cls = true;
  }

  if (!metrics->HasValue())
    return nullptr;

  return metrics;
}

void WebFrameWidgetImpl::BeginUpdateLayers() {
  if (LocalRootImpl())
    update_layers_start_time_.emplace(base::TimeTicks::Now());
}

void WebFrameWidgetImpl::EndUpdateLayers() {
  if (LocalRootImpl()) {
    DCHECK(update_layers_start_time_);
    LocalRootImpl()
        ->GetFrame()
        ->View()
        ->EnsureUkmAggregator()
        .RecordTimerSample(LocalFrameUkmAggregator::kUpdateLayers,
                           update_layers_start_time_.value(),
                           base::TimeTicks::Now());
    probe::LayerTreeDidChange(LocalRootImpl()->GetFrame());
  }
  update_layers_start_time_.reset();
}

void WebFrameWidgetImpl::RecordStartOfFrameMetrics() {
  if (!LocalRootImpl())
    return;

  LocalRootImpl()->GetFrame()->View()->EnsureUkmAggregator().BeginMainFrame();
}

void WebFrameWidgetImpl::RecordEndOfFrameMetrics(
    base::TimeTicks frame_begin_time,
    cc::ActiveFrameSequenceTrackers trackers) {
  if (!LocalRootImpl())
    return;

  LocalRootImpl()
      ->GetFrame()
      ->View()
      ->EnsureUkmAggregator()
      .RecordEndOfFrameMetrics(frame_begin_time, base::TimeTicks::Now(),
                               trackers);
}

void WebFrameWidgetImpl::WillHandleGestureEvent(const WebGestureEvent& event,
                                                bool* suppress) {
  possible_drag_event_info_.source = ui::mojom::blink::DragEventSource::kTouch;
  possible_drag_event_info_.location =
      gfx::ToFlooredPoint(event.PositionInScreen());

  bool handle_as_cursor_control = false;
  switch (event.GetType()) {
    case WebInputEvent::Type::kGestureScrollBegin: {
      if (event.data.scroll_begin.cursor_control) {
        swipe_to_move_cursor_activated_ = true;
        handle_as_cursor_control = true;
      }
      break;
    }
    case WebInputEvent::Type::kGestureScrollUpdate: {
      if (swipe_to_move_cursor_activated_)
        handle_as_cursor_control = true;
      break;
    }
    case WebInputEvent::Type::kGestureScrollEnd: {
      if (swipe_to_move_cursor_activated_) {
        swipe_to_move_cursor_activated_ = false;
        handle_as_cursor_control = true;
      }
      break;
    }
    default:
      break;
  }
  // TODO(crbug.com/1140106): Place cursor for scroll begin other than just move
  // cursor.
  if (handle_as_cursor_control) {
    WebLocalFrame* focused_frame = FocusedWebLocalFrameInWidget();
    if (focused_frame) {
      gfx::Point base(event.PositionInWidget().x(),
                      event.PositionInWidget().y());
      focused_frame->MoveCaretSelection(base);
    }
    *suppress = true;
  }
}

void WebFrameWidgetImpl::WillHandleMouseEvent(const WebMouseEvent& event) {
  possible_drag_event_info_.source = ui::mojom::blink::DragEventSource::kMouse;
  possible_drag_event_info_.location =
      gfx::Point(event.PositionInScreen().x(), event.PositionInScreen().y());
}

void WebFrameWidgetImpl::ObserveGestureEventAndResult(
    const WebGestureEvent& gesture_event,
    const gfx::Vector2dF& unused_delta,
    const cc::OverscrollBehavior& overscroll_behavior,
    bool event_processed) {
  if (!widget_base_->LayerTreeHost()->GetSettings().enable_elastic_overscroll)
    return;

  cc::InputHandlerScrollResult scroll_result;
  scroll_result.did_scroll = event_processed;
  scroll_result.did_overscroll_root = !unused_delta.IsZero();
  scroll_result.unused_scroll_delta = unused_delta;
  scroll_result.overscroll_behavior = overscroll_behavior;

  widget_base_->widget_input_handler_manager()->ObserveGestureEventOnMainThread(
      gesture_event, scroll_result);
}

void WebFrameWidgetImpl::DidHandleKeyEvent() {
  ClearEditCommands();
}

WebTextInputType WebFrameWidgetImpl::GetTextInputType() {
  if (ShouldDispatchImeEventsToPlugin()) {
    return GetFocusedPluginContainer()->GetPluginTextInputType();
  }

  WebInputMethodController* controller = GetActiveWebInputMethodController();
  if (!controller)
    return WebTextInputType::kWebTextInputTypeNone;
  return controller->TextInputType();
}

void WebFrameWidgetImpl::SetCursorVisibilityState(bool is_visible) {
  GetPage()->SetIsCursorVisible(is_visible);
}

void WebFrameWidgetImpl::ApplyViewportChangesForTesting(
    const ApplyViewportChangesArgs& args) {
  widget_base_->ApplyViewportChanges(args);
}

void WebFrameWidgetImpl::SetDisplayMode(mojom::blink::DisplayMode mode) {
  if (mode != display_mode_) {
    display_mode_ = mode;
    LocalFrame* frame = LocalRootImpl()->GetFrame();
    frame->MediaQueryAffectingValueChangedForLocalSubtree(
        MediaValueChange::kOther);
  }
}

void WebFrameWidgetImpl::SetWindowSegments(
    const std::vector<gfx::Rect>& window_segments_param) {
  WebVector<gfx::Rect> window_segments(window_segments_param);
  if (!window_segments_.Equals(window_segments)) {
    window_segments_ = window_segments;
    LocalFrame* frame = LocalRootImpl()->GetFrame();
    frame->WindowSegmentsChanged(window_segments_);

    ForEachRemoteFrameControlledByWidget(WTF::BindRepeating(
        [](const std::vector<gfx::Rect>& window_segments,
           RemoteFrame* remote_frame) {
          remote_frame->DidChangeRootWindowSegments(window_segments);
        },
        window_segments_param));
  }
}

void WebFrameWidgetImpl::SetCursor(const ui::Cursor& cursor) {
  widget_base_->SetCursor(cursor);
}

bool WebFrameWidgetImpl::HandlingInputEvent() {
  return widget_base_->input_handler().handling_input_event();
}

void WebFrameWidgetImpl::SetHandlingInputEvent(bool handling) {
  widget_base_->input_handler().set_handling_input_event(handling);
}

void WebFrameWidgetImpl::ProcessInputEventSynchronouslyForTesting(
    const WebCoalescedInputEvent& event,
    WidgetBaseInputHandler::HandledEventCallback callback) {
  widget_base_->input_handler().HandleInputEvent(event, nullptr,
                                                 std::move(callback));
}

void WebFrameWidgetImpl::ProcessInputEventSynchronouslyForTesting(
    const WebCoalescedInputEvent& event) {
  ProcessInputEventSynchronouslyForTesting(event, base::DoNothing());
}

WebInputEventResult WebFrameWidgetImpl::DispatchBufferedTouchEvents() {
  CHECK(LocalRootImpl());

  if (WebDevToolsAgentImpl* devtools = LocalRootImpl()->DevToolsAgentImpl())
    devtools->DispatchBufferedTouchEvents();

  return LocalRootImpl()
      ->GetFrame()
      ->GetEventHandler()
      .DispatchBufferedTouchEvents();
}

WebInputEventResult WebFrameWidgetImpl::HandleInputEvent(
    const WebCoalescedInputEvent& coalesced_event) {
  const WebInputEvent& input_event = coalesced_event.Event();
  TRACE_EVENT1("input,rail", "WebFrameWidgetImpl::HandleInputEvent", "type",
               WebInputEvent::GetName(input_event.GetType()));
  DCHECK(!WebInputEvent::IsTouchEventType(input_event.GetType()));
  CHECK(LocalRootImpl());

  // Only record metrics for the root frame.
  if (ForTopMostMainFrame()) {
    GetPage()->GetVisualViewport().StartTrackingPinchStats();
  }

  // If a drag-and-drop operation is in progress, ignore input events except
  // PointerCancel and GestureLongPress.
  if (doing_drag_and_drop_ &&
      input_event.GetType() != WebInputEvent::Type::kPointerCancel &&
      input_event.GetType() != WebInputEvent::Type::kGestureLongPress) {
    return WebInputEventResult::kHandledSuppressed;
  }

  // Don't handle events once we've started shutting down.
  if (!GetPage())
    return WebInputEventResult::kNotHandled;

  if (WebDevToolsAgentImpl* devtools = LocalRootImpl()->DevToolsAgentImpl()) {
    auto result = devtools->HandleInputEvent(input_event);
    if (result != WebInputEventResult::kNotHandled)
      return result;
  }

  // Report the event to be NOT processed by WebKit, so that the browser can
  // handle it appropriately.
  if (IgnoreInputEvents())
    return WebInputEventResult::kNotHandled;

  base::AutoReset<const WebInputEvent*> current_event_change(
      &CurrentInputEvent::current_input_event_, &input_event);
  UIEventWithKeyState::ClearNewTabModifierSetFromIsolatedWorld();

  if (GetPage()->GetPointerLockController().IsPointerLocked() &&
      WebInputEvent::IsMouseEventType(input_event.GetType())) {
    PointerLockMouseEvent(coalesced_event);
    return WebInputEventResult::kHandledSystem;
  }

  /// These metrics are only captured for the main frame.
  if (ForMainFrame()) {
    Document& main_frame_document = *LocalRootImpl()->GetFrame()->GetDocument();

    if (input_event.GetType() != WebInputEvent::Type::kMouseMove) {
      FirstMeaningfulPaintDetector::From(main_frame_document)
          .NotifyInputEvent();
    }

    if (input_event.GetType() != WebInputEvent::Type::kMouseMove &&
        input_event.GetType() != WebInputEvent::Type::kMouseEnter &&
        input_event.GetType() != WebInputEvent::Type::kMouseLeave) {
      InteractiveDetector* interactive_detector(
          InteractiveDetector::From(main_frame_document));
      if (interactive_detector) {
        interactive_detector->OnInvalidatingInputEvent(input_event.TimeStamp());
      }
    }
  }

  NotifyInputObservers(coalesced_event);

  // Notify the focus frame of the input. Note that the other frames are not
  // notified as input is only handled by the focused frame.
  Frame* frame = FocusedCoreFrame();
  if (auto* local_frame = DynamicTo<LocalFrame>(frame)) {
    if (auto* content_capture_manager =
            local_frame->LocalFrameRoot().GetOrResetContentCaptureManager()) {
      content_capture_manager->NotifyInputEvent(input_event.GetType(),
                                                *local_frame);
    }
  }

  // Skip the pointerrawupdate for mouse capture case.
  if (mouse_capture_element_ &&
      input_event.GetType() == WebInputEvent::Type::kPointerRawUpdate)
    return WebInputEventResult::kHandledSystem;

  if (mouse_capture_element_ &&
      WebInputEvent::IsMouseEventType(input_event.GetType()))
    return HandleCapturedMouseEvent(coalesced_event);

  // FIXME: This should take in the intended frame, not the local frame
  // root.
  return WidgetEventHandler::HandleInputEvent(coalesced_event,
                                              LocalRootImpl()->GetFrame());
}

WebInputEventResult WebFrameWidgetImpl::HandleCapturedMouseEvent(
    const WebCoalescedInputEvent& coalesced_event) {
  const WebInputEvent& input_event = coalesced_event.Event();
  TRACE_EVENT1("input", "captured mouse event", "type", input_event.GetType());
  // Save |mouse_capture_element_| since |MouseCaptureLost()| will clear it.
  HTMLPlugInElement* element = mouse_capture_element_;

  // Not all platforms call mouseCaptureLost() directly.
  if (input_event.GetType() == WebInputEvent::Type::kMouseUp) {
    SetMouseCapture(false);
    MouseCaptureLost();
  }

  AtomicString event_type;
  switch (input_event.GetType()) {
    case WebInputEvent::Type::kMouseEnter:
      event_type = event_type_names::kMouseover;
      break;
    case WebInputEvent::Type::kMouseMove:
      event_type = event_type_names::kMousemove;
      break;
    case WebInputEvent::Type::kPointerRawUpdate:
      // There will be no mouse event for rawupdate events.
      event_type = event_type_names::kPointerrawupdate;
      break;
    case WebInputEvent::Type::kMouseLeave:
      event_type = event_type_names::kMouseout;
      break;
    case WebInputEvent::Type::kMouseDown:
      event_type = event_type_names::kMousedown;
      LocalFrame::NotifyUserActivation(
          element->GetDocument().GetFrame(),
          mojom::blink::UserActivationNotificationType::kInteraction);
      break;
    case WebInputEvent::Type::kMouseUp:
      event_type = event_type_names::kMouseup;
      break;
    default:
      NOTREACHED();
  }

  WebMouseEvent transformed_event =
      TransformWebMouseEvent(LocalRootImpl()->GetFrameView(),
                             static_cast<const WebMouseEvent&>(input_event));
  if (LocalFrame* frame = element->GetDocument().GetFrame()) {
    frame->GetEventHandler().HandleTargetedMouseEvent(
        element, transformed_event, event_type,
        TransformWebMouseEventVector(
            LocalRootImpl()->GetFrameView(),
            coalesced_event.GetCoalescedEventsPointers()),
        TransformWebMouseEventVector(
            LocalRootImpl()->GetFrameView(),
            coalesced_event.GetPredictedEventsPointers()));
  }
  return WebInputEventResult::kHandledSystem;
}

void WebFrameWidgetImpl::UpdateTextInputState() {
  widget_base_->UpdateTextInputState();
}

void WebFrameWidgetImpl::UpdateSelectionBounds() {
  widget_base_->UpdateSelectionBounds();
}

void WebFrameWidgetImpl::ShowVirtualKeyboard() {
  widget_base_->ShowVirtualKeyboard();
}

void WebFrameWidgetImpl::FlushInputProcessedCallback() {
  widget_base_->FlushInputProcessedCallback();
}

void WebFrameWidgetImpl::CancelCompositionForPepper() {
  widget_base_->CancelCompositionForPepper();
}

void WebFrameWidgetImpl::RequestMouseLock(
    bool has_transient_user_activation,
    bool request_unadjusted_movement,
    mojom::blink::WidgetInputHandlerHost::RequestMouseLockCallback callback) {
  mojom::blink::WidgetInputHandlerHost* host =
      widget_base_->widget_input_handler_manager()->GetWidgetInputHandlerHost();

  // If we don't have a host just leave the callback uncalled. This simulates
  // the browser indefinitely postponing the mouse request which is valid.
  // Note that |callback| is not a mojo bound callback (until it is passed
  // into the mojo interface) and can be destructed without invoking the
  // callback. It does share the same signature as the mojo definition
  // for simplicity.
  if (host) {
    host->RequestMouseLock(has_transient_user_activation,
                           request_unadjusted_movement, std::move(callback));
  }
}

void WebFrameWidgetImpl::MouseCaptureLost() {
  TRACE_EVENT_NESTABLE_ASYNC_END0("input", "capturing mouse",
                                  TRACE_ID_LOCAL(this));
  mouse_capture_element_ = nullptr;
}

void WebFrameWidgetImpl::ApplyVisualProperties(
    const VisualProperties& visual_properties) {
  widget_base_->UpdateVisualProperties(visual_properties);
}

bool WebFrameWidgetImpl::IsFullscreenGranted() {
  return is_fullscreen_granted_;
}

bool WebFrameWidgetImpl::PinchGestureActiveInMainFrame() {
  return is_pinch_gesture_active_in_mainframe_;
}

float WebFrameWidgetImpl::PageScaleInMainFrame() {
  return page_scale_factor_in_mainframe_;
}

void WebFrameWidgetImpl::UpdateSurfaceAndScreenInfo(
    const viz::LocalSurfaceId& new_local_surface_id,
    const gfx::Rect& compositor_viewport_pixel_rect,
    const display::ScreenInfos& new_screen_infos) {
  widget_base_->UpdateSurfaceAndScreenInfo(
      new_local_surface_id, compositor_viewport_pixel_rect, new_screen_infos);
}

void WebFrameWidgetImpl::UpdateScreenInfo(
    const display::ScreenInfos& new_screen_infos) {
  widget_base_->UpdateScreenInfo(new_screen_infos);
}

void WebFrameWidgetImpl::UpdateSurfaceAndCompositorRect(
    const viz::LocalSurfaceId& new_local_surface_id,
    const gfx::Rect& compositor_viewport_pixel_rect) {
  widget_base_->UpdateSurfaceAndCompositorRect(new_local_surface_id,
                                               compositor_viewport_pixel_rect);
}

void WebFrameWidgetImpl::UpdateCompositorViewportRect(
    const gfx::Rect& compositor_viewport_pixel_rect) {
  widget_base_->UpdateCompositorViewportRect(compositor_viewport_pixel_rect);
}

const display::ScreenInfo& WebFrameWidgetImpl::GetScreenInfo() {
  return widget_base_->GetScreenInfo();
}

const display::ScreenInfos& WebFrameWidgetImpl::GetScreenInfos() {
  return widget_base_->screen_infos();
}

const display::ScreenInfo& WebFrameWidgetImpl::GetOriginalScreenInfo() {
  if (device_emulator_)
    return device_emulator_->GetOriginalScreenInfo();
  return widget_base_->GetScreenInfo();
}

const display::ScreenInfos& WebFrameWidgetImpl::GetOriginalScreenInfos() {
  if (device_emulator_)
    return device_emulator_->original_screen_infos();
  return widget_base_->screen_infos();
}

gfx::Rect WebFrameWidgetImpl::WindowRect() {
  return widget_base_->WindowRect();
}

gfx::Rect WebFrameWidgetImpl::ViewRect() {
  return widget_base_->ViewRect();
}

void WebFrameWidgetImpl::SetScreenRects(const gfx::Rect& widget_screen_rect,
                                        const gfx::Rect& window_screen_rect) {
  widget_base_->SetScreenRects(widget_screen_rect, window_screen_rect);
}

gfx::Size WebFrameWidgetImpl::VisibleViewportSizeInDIPs() {
  return widget_base_->VisibleViewportSizeInDIPs();
}

void WebFrameWidgetImpl::SetPendingWindowRect(
    const gfx::Rect& window_screen_rect) {
  widget_base_->SetPendingWindowRect(window_screen_rect);
}

void WebFrameWidgetImpl::AckPendingWindowRect() {
  widget_base_->AckPendingWindowRect();
}

bool WebFrameWidgetImpl::IsHidden() const {
  return widget_base_->is_hidden();
}

WebString WebFrameWidgetImpl::GetLastToolTipTextForTesting() const {
  return GetPage()->GetChromeClient().GetLastToolTipTextForTesting();
}

float WebFrameWidgetImpl::GetEmulatorScale() {
  if (device_emulator_)
    return device_emulator_->scale();
  return 1.0f;
}

void WebFrameWidgetImpl::IntrinsicSizingInfoChanged(
    mojom::blink::IntrinsicSizingInfoPtr sizing_info) {
  DCHECK(ForSubframe());
  GetAssociatedFrameWidgetHost()->IntrinsicSizingInfoChanged(
      std::move(sizing_info));
}

void WebFrameWidgetImpl::AutoscrollStart(const gfx::PointF& position) {
  GetAssociatedFrameWidgetHost()->AutoscrollStart(std::move(position));
}

void WebFrameWidgetImpl::AutoscrollFling(const gfx::Vector2dF& velocity) {
  GetAssociatedFrameWidgetHost()->AutoscrollFling(std::move(velocity));
}

void WebFrameWidgetImpl::AutoscrollEnd() {
  GetAssociatedFrameWidgetHost()->AutoscrollEnd();
}

void WebFrameWidgetImpl::DidMeaningfulLayout(WebMeaningfulLayout layout_type) {
  if (layout_type == blink::WebMeaningfulLayout::kVisuallyNonEmpty) {
    NotifyPresentationTime(
        WTF::Bind(&WebFrameWidgetImpl::PresentationCallbackForMeaningfulLayout,
                  WrapWeakPersistent(this)));
  }

  ForEachLocalFrameControlledByWidget(
      local_root_->GetFrame(),
      WTF::BindRepeating(
          [](WebMeaningfulLayout layout_type, WebLocalFrameImpl* local_frame) {
            local_frame->Client()->DidMeaningfulLayout(layout_type);
          },
          layout_type));
}

void WebFrameWidgetImpl::PresentationCallbackForMeaningfulLayout(
    base::TimeTicks) {
  // |local_root_| may be null if the widget has shut down between when this
  // callback was requested and when it was resolved by the compositor.
  if (local_root_)
    local_root_->ViewImpl()->DidFirstVisuallyNonEmptyPaint();

  if (widget_base_)
    widget_base_->DidFirstVisuallyNonEmptyPaint();
}

void WebFrameWidgetImpl::RequestAnimationAfterDelay(
    const base::TimeDelta& delay) {
  widget_base_->RequestAnimationAfterDelay(delay);
}

void WebFrameWidgetImpl::SetRootLayer(scoped_refptr<cc::Layer> layer) {
  if (!View()->does_composite()) {
    DCHECK(ForMainFrame());
    DCHECK(!layer);
    return;
  }

  // Set up some initial state before we are setting the layer.
  if (ForSubframe() && layer) {
    // Child local roots will always have a transparent background color.
    widget_base_->LayerTreeHost()->set_background_color(SkColors::kTransparent);
    // Pass the limits even though this is for subframes, as the limits will
    // be needed in setting the raster scale.
    SetPageScaleStateAndLimits(1.f, false /* is_pinch_gesture_active */,
                               View()->MinimumPageScaleFactor(),
                               View()->MaximumPageScaleFactor());
  }

  bool root_layer_exists = !!layer;
  widget_base_->LayerTreeHost()->SetRootLayer(std::move(layer));

  // Notify the WebView that we did set a layer.
  if (ForMainFrame()) {
    View()->DidChangeRootLayer(root_layer_exists);
  }
}

base::WeakPtr<AnimationWorkletMutatorDispatcherImpl>
WebFrameWidgetImpl::EnsureCompositorMutatorDispatcher(
    scoped_refptr<base::SingleThreadTaskRunner> mutator_task_runner) {
  if (!mutator_task_runner_) {
    mutator_task_runner_ = std::move(mutator_task_runner);
    widget_base_->LayerTreeHost()->SetLayerTreeMutator(
        AnimationWorkletMutatorDispatcherImpl::CreateCompositorThreadClient(
            mutator_dispatcher_, mutator_task_runner_));
  }

  DCHECK(mutator_task_runner_);
  return mutator_dispatcher_;
}

HitTestResult WebFrameWidgetImpl::CoreHitTestResultAt(
    const gfx::PointF& point_in_viewport) {
  LocalFrameView* view = LocalRootImpl()->GetFrameView();
  gfx::PointF point_in_root_frame(view->ViewportToFrame(point_in_viewport));
  return HitTestResultForRootFramePos(point_in_root_frame);
}

cc::AnimationHost* WebFrameWidgetImpl::AnimationHost() const {
  return widget_base_->AnimationHost();
}

cc::AnimationTimeline* WebFrameWidgetImpl::ScrollAnimationTimeline() const {
  return widget_base_->ScrollAnimationTimeline();
}

base::WeakPtr<PaintWorkletPaintDispatcher>
WebFrameWidgetImpl::EnsureCompositorPaintDispatcher(
    scoped_refptr<base::SingleThreadTaskRunner>* paint_task_runner) {
  // We check paint_task_runner_ not paint_dispatcher_ because the dispatcher is
  // a base::WeakPtr that should only be used on the compositor thread.
  if (!paint_task_runner_) {
    widget_base_->LayerTreeHost()->SetPaintWorkletLayerPainter(
        PaintWorkletPaintDispatcher::CreateCompositorThreadPainter(
            &paint_dispatcher_));
    paint_task_runner_ = Thread::CompositorThread()->GetTaskRunner();
  }
  DCHECK(paint_task_runner_);
  *paint_task_runner = paint_task_runner_;
  return paint_dispatcher_;
}

void WebFrameWidgetImpl::SetDelegatedInkMetadata(
    std::unique_ptr<gfx::DelegatedInkMetadata> metadata) {
  widget_base_->LayerTreeHost()->SetDelegatedInkMetadata(std::move(metadata));
}

// Enables measuring and reporting both presentation times and swap times in
// swap promises.
class ReportTimeSwapPromise : public cc::SwapPromise {
 public:
  ReportTimeSwapPromise(WebFrameWidgetImpl::PromiseCallbacks callbacks,
                        scoped_refptr<base::SingleThreadTaskRunner> task_runner,
                        WebFrameWidgetImpl* widget)
      : promise_callbacks_(std::move(callbacks)),
        task_runner_(std::move(task_runner)),
        widget_(widget) {}

  ReportTimeSwapPromise(const ReportTimeSwapPromise&) = delete;
  ReportTimeSwapPromise& operator=(const ReportTimeSwapPromise&) = delete;

  ~ReportTimeSwapPromise() override = default;

  void DidActivate() override {}

  void WillSwap(viz::CompositorFrameMetadata* metadata) override {
    DCHECK_GT(metadata->frame_token, 0u);
    // The interval between the current swap and its presentation time is
    // reported in UMA (see corresponding code in DidSwap() below).
    frame_token_ = metadata->frame_token;
  }

  void DidSwap() override {
    DCHECK_GT(frame_token_, 0u);
    PostCrossThreadTask(
        *task_runner_, FROM_HERE,
        CrossThreadBindOnce(&RunCallbackAfterSwap, widget_,
                            base::TimeTicks::Now(),
                            std::move(promise_callbacks_), frame_token_));
  }

  DidNotSwapAction DidNotSwap(DidNotSwapReason reason) override {
    if (base::FeatureList::IsEnabled(
            features::kReportFCPOnlyOnSuccessfulCommit)) {
      if (reason != DidNotSwapReason::SWAP_FAILS &&
          reason != DidNotSwapReason::COMMIT_NO_UPDATE) {
        return DidNotSwapAction::KEEP_ACTIVE;
      }
    }

    DidNotSwapAction action = DidNotSwapAction::BREAK_PROMISE;
    WebFrameWidgetImpl::PromiseCallbacks promise_callbacks_on_failure = {
        .swap_time_callback = std::move(promise_callbacks_.swap_time_callback),
        .presentation_time_callback =
            std::move(promise_callbacks_.presentation_time_callback)};

#if BUILDFLAG(IS_MAC)
    if (reason == DidNotSwapReason::COMMIT_FAILS &&
        promise_callbacks_.core_animation_error_code_callback) {
      action = DidNotSwapAction::KEEP_ACTIVE;
    } else {
      promise_callbacks_on_failure.core_animation_error_code_callback =
          std::move(promise_callbacks_.core_animation_error_code_callback);
    }
#endif

    if (!promise_callbacks_on_failure.IsEmpty()) {
      ReportSwapAndPresentationFailureOnTaskRunner(
          task_runner_, std::move(promise_callbacks_on_failure),
          base::TimeTicks::Now());
    }
    return action;
  }

  int64_t GetTraceId() const override { return 0; }

 private:
  static void RunCallbackAfterSwap(
      WebFrameWidgetImpl* widget,
      base::TimeTicks swap_time,
      WebFrameWidgetImpl::PromiseCallbacks callbacks,
      int frame_token) {
    // If the widget was collected or the widget wasn't collected yet, but
    // it was closed don't schedule a presentation callback.
    if (widget && widget->widget_base_) {
      widget->widget_base_->AddPresentationCallback(
          frame_token,
          WTF::Bind(&RunCallbackAfterPresentation,
                    std::move(callbacks.presentation_time_callback),
                    swap_time));
      ReportTime(std::move(callbacks.swap_time_callback), swap_time);

#if BUILDFLAG(IS_MAC)
      if (callbacks.core_animation_error_code_callback) {
        widget->widget_base_->AddCoreAnimationErrorCodeCallback(
            frame_token,
            std::move(callbacks.core_animation_error_code_callback));
      }
#endif
    } else {
      ReportTime(std::move(callbacks.swap_time_callback), swap_time);
      ReportTime(std::move(callbacks.presentation_time_callback), swap_time);
#if BUILDFLAG(IS_MAC)
      ReportErrorCode(std::move(callbacks.core_animation_error_code_callback),
                      gfx::kCALayerUnknownNoWidget);
#endif
    }
  }

  static void RunCallbackAfterPresentation(
      base::OnceCallback<void(base::TimeTicks)> presentation_time_callback,
      base::TimeTicks swap_time,
      base::TimeTicks presentation_time) {
    DCHECK(!swap_time.is_null());
    bool presentation_time_is_valid =
        !presentation_time.is_null() && (presentation_time > swap_time);
    UMA_HISTOGRAM_BOOLEAN("PageLoad.Internal.Renderer.PresentationTime.Valid",
                          presentation_time_is_valid);
    if (presentation_time_is_valid) {
      // This measures from 1ms to 10seconds.
      UMA_HISTOGRAM_TIMES(
          "PageLoad.Internal.Renderer.PresentationTime.DeltaFromSwapTime",
          presentation_time - swap_time);
    }
    ReportTime(std::move(presentation_time_callback),
               presentation_time_is_valid ? presentation_time : swap_time);
  }

  static void ReportTime(base::OnceCallback<void(base::TimeTicks)> callback,
                         base::TimeTicks time) {
    if (callback)
      std::move(callback).Run(time);
  }

#if BUILDFLAG(IS_MAC)
  static void ReportErrorCode(
      base::OnceCallback<void(gfx::CALayerResult)> callback,
      gfx::CALayerResult error_code) {
    if (callback)
      std::move(callback).Run(error_code);
  }
#endif

  static void ReportSwapAndPresentationFailureOnTaskRunner(
      scoped_refptr<base::SingleThreadTaskRunner> task_runner,
      WebFrameWidgetImpl::PromiseCallbacks callbacks,
      base::TimeTicks failure_time) {
    if (!task_runner->BelongsToCurrentThread()) {
      PostCrossThreadTask(
          *task_runner, FROM_HERE,
          CrossThreadBindOnce(&ReportSwapAndPresentationFailureOnTaskRunner,
                              task_runner, std::move(callbacks), failure_time));
      return;
    }

    ReportTime(std::move(callbacks.swap_time_callback), failure_time);
    ReportTime(std::move(callbacks.presentation_time_callback), failure_time);
#if BUILDFLAG(IS_MAC)
    ReportErrorCode(std::move(callbacks.core_animation_error_code_callback),
                    gfx::kCALayerUnknownDidNotSwap);
#endif
  }

  WebFrameWidgetImpl::PromiseCallbacks promise_callbacks_;

  scoped_refptr<base::SingleThreadTaskRunner> task_runner_;
  CrossThreadWeakPersistent<WebFrameWidgetImpl> widget_;
  uint32_t frame_token_ = 0;
};

void WebFrameWidgetImpl::NotifySwapAndPresentationTimeForTesting(
    PromiseCallbacks callbacks) {
  NotifySwapAndPresentationTime(std::move(callbacks));
}

void WebFrameWidgetImpl::NotifyPresentationTimeInBlink(
    base::OnceCallback<void(base::TimeTicks)> presentation_callback) {
  NotifySwapAndPresentationTime(
      {.presentation_time_callback = std::move(presentation_callback)});
}

void WebFrameWidgetImpl::NotifyPresentationTime(
    base::OnceCallback<void(base::TimeTicks)> presentation_callback) {
  NotifySwapAndPresentationTime(
      {.presentation_time_callback = std::move(presentation_callback)});
}

#if BUILDFLAG(IS_MAC)
void WebFrameWidgetImpl::NotifyCoreAnimationErrorCode(
    base::OnceCallback<void(gfx::CALayerResult)>
        core_animation_error_code_callback) {
  NotifySwapAndPresentationTime(
      {.core_animation_error_code_callback =
           std::move(core_animation_error_code_callback)});
}
#endif

void WebFrameWidgetImpl::NotifySwapAndPresentationTime(
    PromiseCallbacks callbacks) {
  if (!View()->does_composite())
    return;

  widget_base_->LayerTreeHost()->QueueSwapPromise(
      std::make_unique<ReportTimeSwapPromise>(std::move(callbacks),
                                              widget_base_->LayerTreeHost()
                                                  ->GetTaskRunnerProvider()
                                                  ->MainThreadTaskRunner(),
                                              this));
}

void WebFrameWidgetImpl::WaitForDebuggerWhenShown() {
  local_root_->WaitForDebuggerWhenShown();
}

void WebFrameWidgetImpl::SetTextZoomFactor(float text_zoom_factor) {
  local_root_->GetFrame()->SetTextZoomFactor(text_zoom_factor);
}

float WebFrameWidgetImpl::TextZoomFactor() {
  return local_root_->GetFrame()->TextZoomFactor();
}

void WebFrameWidgetImpl::SetMainFrameOverlayColor(SkColor color) {
  DCHECK(!local_root_->Parent());
  local_root_->GetFrame()->SetMainFrameColorOverlay(color);
}

void WebFrameWidgetImpl::AddEditCommandForNextKeyEvent(const WebString& name,
                                                       const WebString& value) {
  edit_commands_.push_back(mojom::blink::EditCommand::New(name, value));
}

bool WebFrameWidgetImpl::HandleCurrentKeyboardEvent() {
  bool did_execute_command = false;
  WebLocalFrame* frame = FocusedWebLocalFrameInWidget();
  if (!frame)
    frame = local_root_;
  for (const auto& command : edit_commands_) {
    // In gtk and cocoa, it's possible to bind multiple edit commands to one
    // key (but it's the exception). Once one edit command is not executed, it
    // seems safest to not execute the rest.
    if (!frame->ExecuteCommand(command->name, command->value))
      break;
    did_execute_command = true;
  }

  return did_execute_command;
}

void WebFrameWidgetImpl::ClearEditCommands() {
  edit_commands_ = Vector<mojom::blink::EditCommandPtr>();
}

WebTextInputInfo WebFrameWidgetImpl::TextInputInfo() {
  WebInputMethodController* controller = GetActiveWebInputMethodController();
  if (!controller)
    return WebTextInputInfo();
  return controller->TextInputInfo();
}

ui::mojom::blink::VirtualKeyboardVisibilityRequest
WebFrameWidgetImpl::GetLastVirtualKeyboardVisibilityRequest() {
  WebInputMethodController* controller = GetActiveWebInputMethodController();
  if (!controller)
    return ui::mojom::blink::VirtualKeyboardVisibilityRequest::NONE;
  return controller->GetLastVirtualKeyboardVisibilityRequest();
}

bool WebFrameWidgetImpl::ShouldSuppressKeyboardForFocusedElement() {
  WebLocalFrame* focused_frame = FocusedWebLocalFrameInWidget();
  if (!focused_frame)
    return false;
  return focused_frame->ShouldSuppressKeyboardForFocusedElement();
}

void WebFrameWidgetImpl::GetEditContextBoundsInWindow(
    absl::optional<gfx::Rect>* edit_context_control_bounds,
    absl::optional<gfx::Rect>* edit_context_selection_bounds) {
  WebInputMethodController* controller = GetActiveWebInputMethodController();
  if (!controller)
    return;
  gfx::Rect control_bounds;
  gfx::Rect selection_bounds;
  controller->GetLayoutBounds(&control_bounds, &selection_bounds);
  *edit_context_control_bounds =
      widget_base_->BlinkSpaceToEnclosedDIPs(control_bounds);
  if (controller->IsEditContextActive()) {
    *edit_context_selection_bounds =
        widget_base_->BlinkSpaceToEnclosedDIPs(selection_bounds);
  }
}

int32_t WebFrameWidgetImpl::ComputeWebTextInputNextPreviousFlags() {
  WebInputMethodController* controller = GetActiveWebInputMethodController();
  if (!controller)
    return 0;
  return controller->ComputeWebTextInputNextPreviousFlags();
}

void WebFrameWidgetImpl::ResetVirtualKeyboardVisibilityRequest() {
  WebInputMethodController* controller = GetActiveWebInputMethodController();
  if (!controller)
    return;
  controller->SetVirtualKeyboardVisibilityRequest(
      ui::mojom::blink::VirtualKeyboardVisibilityRequest::NONE);
  ;
}

bool WebFrameWidgetImpl::GetSelectionBoundsInWindow(
    gfx::Rect* focus,
    gfx::Rect* anchor,
    gfx::Rect* bounding_box,
    base::i18n::TextDirection* focus_dir,
    base::i18n::TextDirection* anchor_dir,
    bool* is_anchor_first) {
  if (ShouldDispatchImeEventsToPlugin()) {
    // TODO(kinaba) http://crbug.com/101101
    // Current Pepper IME API does not handle selection bounds. So we simply
    // use the caret position as an empty range for now. It will be updated
    // after Pepper API equips features related to surrounding text retrieval.
    gfx::Rect pepper_caret_in_dips = widget_base_->BlinkSpaceToEnclosedDIPs(
        GetFocusedPluginContainer()->GetPluginCaretBounds());
    if (pepper_caret_in_dips == *focus && pepper_caret_in_dips == *anchor)
      return false;
    *focus = pepper_caret_in_dips;
    *anchor = *focus;
    return true;
  }
  gfx::Rect focus_root_frame;
  gfx::Rect anchor_root_frame;
  gfx::Rect bounding_box_root_frame;
  CalculateSelectionBounds(focus_root_frame, anchor_root_frame,
                           &bounding_box_root_frame);
  gfx::Rect focus_rect_in_dips =
      widget_base_->BlinkSpaceToEnclosedDIPs(gfx::Rect(focus_root_frame));
  gfx::Rect anchor_rect_in_dips =
      widget_base_->BlinkSpaceToEnclosedDIPs(gfx::Rect(anchor_root_frame));
  gfx::Rect bounding_box_in_dips = widget_base_->BlinkSpaceToEnclosedDIPs(
      gfx::Rect(bounding_box_root_frame));

  // if the bounds are the same return false.
  if (focus_rect_in_dips == *focus && anchor_rect_in_dips == *anchor)
    return false;
  *focus = focus_rect_in_dips;
  *anchor = anchor_rect_in_dips;
  *bounding_box = bounding_box_in_dips;

  WebLocalFrame* focused_frame = FocusedWebLocalFrameInWidget();
  if (!focused_frame)
    return true;
  focused_frame->SelectionTextDirection(*focus_dir, *anchor_dir);
  *is_anchor_first = focused_frame->IsSelectionAnchorFirst();
  return true;
}

void WebFrameWidgetImpl::ClearTextInputState() {
  widget_base_->ClearTextInputState();
}

bool WebFrameWidgetImpl::IsPasting() {
  return widget_base_->is_pasting();
}

bool WebFrameWidgetImpl::HandlingSelectRange() {
  return widget_base_->handling_select_range();
}

void WebFrameWidgetImpl::SetFocus(bool focus) {
  widget_base_->SetFocus(
      focus ? mojom::blink::FocusState::kFocused
            : View()->IsActive()
                  ? mojom::blink::FocusState::kNotFocusedAndActive
                  : mojom::blink::FocusState::kNotFocusedAndNotActive);
}

bool WebFrameWidgetImpl::HasFocus() {
  return widget_base_->has_focus();
}

void WebFrameWidgetImpl::UpdateTooltipUnderCursor(const String& tooltip_text,
                                                  TextDirection dir) {
  widget_base_->UpdateTooltipUnderCursor(tooltip_text, dir);
}

void WebFrameWidgetImpl::UpdateTooltipFromKeyboard(const String& tooltip_text,
                                                   TextDirection dir,
                                                   const gfx::Rect& bounds) {
  widget_base_->UpdateTooltipFromKeyboard(tooltip_text, dir, bounds);
}

void WebFrameWidgetImpl::ClearKeyboardTriggeredTooltip() {
  widget_base_->ClearKeyboardTriggeredTooltip();
}

void WebFrameWidgetImpl::DidOverscroll(
    const gfx::Vector2dF& overscroll_delta,
    const gfx::Vector2dF& accumulated_overscroll,
    const gfx::PointF& position,
    const gfx::Vector2dF& velocity) {
#if BUILDFLAG(IS_MAC)
  // On OSX the user can disable the elastic overscroll effect. If that's the
  // case, don't forward the overscroll notification.
  if (!widget_base_->LayerTreeHost()->GetSettings().enable_elastic_overscroll)
    return;
#endif

  cc::OverscrollBehavior overscroll_behavior =
      widget_base_->LayerTreeHost()->overscroll_behavior();
  if (!widget_base_->input_handler().DidOverscrollFromBlink(
          overscroll_delta, accumulated_overscroll, position, velocity,
          overscroll_behavior))
    return;

  // If we're currently handling an event, stash the overscroll data such that
  // it can be bundled in the event ack.
  if (mojom::blink::WidgetInputHandlerHost* host =
          widget_base_->widget_input_handler_manager()
              ->GetWidgetInputHandlerHost()) {
    host->DidOverscroll(mojom::blink::DidOverscrollParams::New(
        accumulated_overscroll, overscroll_delta, velocity, position,
        overscroll_behavior));
  }
}

void WebFrameWidgetImpl::InjectGestureScrollEvent(
    blink::WebGestureDevice device,
    const gfx::Vector2dF& delta,
    ui::ScrollGranularity granularity,
    cc::ElementId scrollable_area_element_id,
    blink::WebInputEvent::Type injected_type) {
  if (base::FeatureList::IsEnabled(::features::kScrollUnification)) {
    // create a GestureScroll Event and post it to the compositor thread
    // TODO(crbug.com/1126098) use original input event's timestamp.
    // TODO(crbug.com/1082590) ensure continuity in scroll metrics collection
    base::TimeTicks now = base::TimeTicks::Now();
    std::unique_ptr<WebGestureEvent> gesture_event =
        WebGestureEvent::GenerateInjectedScrollGesture(
            injected_type, now, device, gfx::PointF(0, 0), delta, granularity);
    if (injected_type == WebInputEvent::Type::kGestureScrollBegin) {
      gesture_event->data.scroll_begin.scrollable_area_element_id =
          scrollable_area_element_id.GetStableId();
      gesture_event->data.scroll_begin.main_thread_hit_tested = true;
    }

    // Notifies TestWebFrameWidget of the injected event. Does nothing outside
    // of unit tests. This would happen in WidgetBase::QueueSyntheticEvent if
    // scroll unification were not enabled.
    WillQueueSyntheticEvent(
        WebCoalescedInputEvent(*gesture_event, ui::LatencyInfo()));

    widget_base_->widget_input_handler_manager()
        ->DispatchScrollGestureToCompositor(std::move(gesture_event));
  } else {
    widget_base_->input_handler().InjectGestureScrollEvent(
        device, delta, granularity, scrollable_area_element_id, injected_type);
  }
}

void WebFrameWidgetImpl::DidChangeCursor(const ui::Cursor& cursor) {
  widget_base_->SetCursor(cursor);
}

bool WebFrameWidgetImpl::SetComposition(
    const String& text,
    const Vector<ui::ImeTextSpan>& ime_text_spans,
    const gfx::Range& replacement_range,
    int selection_start,
    int selection_end) {
  WebInputMethodController* controller = GetActiveWebInputMethodController();
  if (!controller)
    return false;

  return controller->SetComposition(
      text, ime_text_spans,
      replacement_range.IsValid()
          ? WebRange(base::checked_cast<int>(replacement_range.start()),
                     base::checked_cast<int>(replacement_range.length()))
          : WebRange(),
      selection_start, selection_end);
}

void WebFrameWidgetImpl::CommitText(
    const String& text,
    const Vector<ui::ImeTextSpan>& ime_text_spans,
    const gfx::Range& replacement_range,
    int relative_cursor_pos) {
  WebInputMethodController* controller = GetActiveWebInputMethodController();
  if (!controller)
    return;
  controller->CommitText(
      text, ime_text_spans,
      replacement_range.IsValid()
          ? WebRange(base::checked_cast<int>(replacement_range.start()),
                     base::checked_cast<int>(replacement_range.length()))
          : WebRange(),
      relative_cursor_pos);
}

void WebFrameWidgetImpl::FinishComposingText(bool keep_selection) {
  WebInputMethodController* controller = GetActiveWebInputMethodController();
  if (!controller)
    return;
  controller->FinishComposingText(
      keep_selection ? WebInputMethodController::kKeepSelection
                     : WebInputMethodController::kDoNotKeepSelection);
}

bool WebFrameWidgetImpl::IsProvisional() {
  return LocalRoot()->IsProvisional();
}

uint64_t WebFrameWidgetImpl::GetScrollableContainerIdAt(
    const gfx::PointF& point) {
  return HitTestResultAt(point).GetScrollableContainerId();
}

bool WebFrameWidgetImpl::ShouldHandleImeEvents() {
  if (ForMainFrame()) {
    return HasFocus();
  } else {
    // TODO(ekaramad): main frame widget returns true only if it has focus.
    // We track page focus in all WebViews on the page but the WebFrameWidgets
    // corresponding to child local roots do not get the update. For now, this
    // method returns true when the WebFrameWidget is for a child local frame,
    // i.e., IME events will be processed regardless of page focus. We should
    // revisit this after page focus for OOPIFs has been fully resolved
    // (https://crbug.com/689777).
    return LocalRootImpl();
  }
}

void WebFrameWidgetImpl::SetEditCommandsForNextKeyEvent(
    Vector<mojom::blink::EditCommandPtr> edit_commands) {
  edit_commands_ = std::move(edit_commands);
}

void WebFrameWidgetImpl::FocusChangeComplete() {
  blink::WebLocalFrame* focused = LocalRoot()->View()->FocusedFrame();

  if (focused && focused->AutofillClient())
    focused->AutofillClient()->DidCompleteFocusChangeInFrame();
}

void WebFrameWidgetImpl::ShowVirtualKeyboardOnElementFocus() {
  widget_base_->ShowVirtualKeyboardOnElementFocus();
}

void WebFrameWidgetImpl::ProcessTouchAction(WebTouchAction touch_action) {
  widget_base_->ProcessTouchAction(touch_action);
}

void WebFrameWidgetImpl::DidHandleGestureEvent(const WebGestureEvent& event) {
#if BUILDFLAG(IS_ANDROID) || defined(USE_AURA)
  if (event.GetType() == WebInputEvent::Type::kGestureTap) {
    widget_base_->ShowVirtualKeyboard();
  } else if (event.GetType() == WebInputEvent::Type::kGestureLongPress) {
    WebInputMethodController* controller = GetActiveWebInputMethodController();
    if (!controller || controller->TextInputInfo().value.IsEmpty())
      widget_base_->UpdateTextInputState();
    else
      widget_base_->ShowVirtualKeyboard();
  }
#endif
}

void WebFrameWidgetImpl::SetHasPointerRawUpdateEventHandlers(
    bool has_handlers) {
  widget_base_->widget_input_handler_manager()
      ->input_event_queue()
      ->HasPointerRawUpdateEventHandlers(has_handlers);
}

void WebFrameWidgetImpl::SetNeedsLowLatencyInput(bool needs_low_latency) {
  widget_base_->widget_input_handler_manager()
      ->input_event_queue()
      ->SetNeedsLowLatency(needs_low_latency);
}

void WebFrameWidgetImpl::RequestUnbufferedInputEvents() {
  widget_base_->widget_input_handler_manager()
      ->input_event_queue()
      ->RequestUnbufferedInputEvents();
}

void WebFrameWidgetImpl::SetNeedsUnbufferedInputForDebugger(bool unbuffered) {
  widget_base_->widget_input_handler_manager()
      ->input_event_queue()
      ->SetNeedsUnbufferedInputForDebugger(unbuffered);
}

void WebFrameWidgetImpl::DidNavigate() {
  // The input handler wants to know about navigation so that it can
  // suppress input until the newly navigated page has a committed frame.
  // It also resets the state for UMA reporting of input arrival with respect
  // to document lifecycle.
  if (!widget_base_->widget_input_handler_manager())
    return;
  widget_base_->widget_input_handler_manager()->DidNavigate();
}

void WebFrameWidgetImpl::SetMouseCapture(bool capture) {
  if (mojom::blink::WidgetInputHandlerHost* host =
          widget_base_->widget_input_handler_manager()
              ->GetWidgetInputHandlerHost()) {
    host->SetMouseCapture(capture);
  }
}

gfx::Range WebFrameWidgetImpl::CompositionRange() {
  WebLocalFrame* focused_frame = FocusedWebLocalFrameInWidget();
  if (!focused_frame || ShouldDispatchImeEventsToPlugin())
    return gfx::Range::InvalidRange();

  blink::WebInputMethodController* controller =
      focused_frame->GetInputMethodController();
  WebRange web_range = controller->CompositionRange();
  if (web_range.IsNull())
    return gfx::Range::InvalidRange();
  return gfx::Range(web_range.StartOffset(), web_range.EndOffset());
}

void WebFrameWidgetImpl::GetCompositionCharacterBoundsInWindow(
    Vector<gfx::Rect>* bounds_in_dips) {
  WebLocalFrame* focused_frame = FocusedWebLocalFrameInWidget();
  if (!focused_frame || ShouldDispatchImeEventsToPlugin())
    return;
  blink::WebInputMethodController* controller =
      focused_frame->GetInputMethodController();
  blink::WebVector<gfx::Rect> bounds_from_blink;
  if (!controller->GetCompositionCharacterBounds(bounds_from_blink))
    return;

  for (auto& rect : bounds_from_blink) {
    bounds_in_dips->push_back(widget_base_->BlinkSpaceToEnclosedDIPs(rect));
  }
}

void WebFrameWidgetImpl::AddImeTextSpansToExistingText(
    uint32_t start,
    uint32_t end,
    const Vector<ui::ImeTextSpan>& ime_text_spans) {
  WebLocalFrame* focused_frame = FocusedWebLocalFrameInWidget();
  if (!focused_frame)
    return;
  focused_frame->AddImeTextSpansToExistingText(ime_text_spans, start, end);
}

Vector<ui::mojom::blink::ImeTextSpanInfoPtr>
WebFrameWidgetImpl::GetImeTextSpansInfo(
    const WebVector<ui::ImeTextSpan>& ime_text_spans) {
  auto* focused_frame = FocusedWebLocalFrameInWidget();
  if (!focused_frame)
    return Vector<ui::mojom::blink::ImeTextSpanInfoPtr>();

  Vector<ui::mojom::blink::ImeTextSpanInfoPtr> ime_text_spans_info;

  for (const auto& ime_text_span : ime_text_spans) {
    gfx::Rect rect;
    unsigned length = ime_text_span.end_offset - ime_text_span.start_offset;
    focused_frame->FirstRectForCharacterRange(ime_text_span.start_offset,
                                              length, rect);

    ime_text_spans_info.push_back(ui::mojom::blink::ImeTextSpanInfo::New(
        ime_text_span, widget_base_->BlinkSpaceToEnclosedDIPs(rect)));
  }
  return ime_text_spans_info;
}

void WebFrameWidgetImpl::ClearImeTextSpansByType(uint32_t start,
                                                 uint32_t end,
                                                 ui::ImeTextSpan::Type type) {
  WebLocalFrame* focused_frame = FocusedWebLocalFrameInWidget();
  if (!focused_frame)
    return;
  focused_frame->ClearImeTextSpansByType(type, start, end);
}

void WebFrameWidgetImpl::SetCompositionFromExistingText(
    int32_t start,
    int32_t end,
    const Vector<ui::ImeTextSpan>& ime_text_spans) {
  WebLocalFrame* focused_frame = FocusedWebLocalFrameInWidget();
  if (!focused_frame)
    return;
  focused_frame->SetCompositionFromExistingText(start, end, ime_text_spans);
}

void WebFrameWidgetImpl::ExtendSelectionAndDelete(int32_t before,
                                                  int32_t after) {
  WebLocalFrame* focused_frame = FocusedWebLocalFrameInWidget();
  if (!focused_frame)
    return;
  focused_frame->ExtendSelectionAndDelete(before, after);
}

void WebFrameWidgetImpl::DeleteSurroundingText(int32_t before, int32_t after) {
  WebLocalFrame* focused_frame = FocusedWebLocalFrameInWidget();
  if (!focused_frame)
    return;
  focused_frame->DeleteSurroundingText(before, after);
}

void WebFrameWidgetImpl::DeleteSurroundingTextInCodePoints(int32_t before,
                                                           int32_t after) {
  WebLocalFrame* focused_frame = FocusedWebLocalFrameInWidget();
  if (!focused_frame)
    return;
  focused_frame->DeleteSurroundingTextInCodePoints(before, after);
}

void WebFrameWidgetImpl::SetEditableSelectionOffsets(int32_t start,
                                                     int32_t end) {
  WebLocalFrame* focused_frame = FocusedWebLocalFrameInWidget();
  if (!focused_frame)
    return;
  focused_frame->SetEditableSelectionOffsets(start, end);
}

void WebFrameWidgetImpl::ExecuteEditCommand(const String& command,
                                            const String& value) {
  WebLocalFrame* focused_frame = FocusedWebLocalFrameInWidget();
  if (!focused_frame)
    return;
  focused_frame->ExecuteCommand(command, value);
}

void WebFrameWidgetImpl::Undo() {
  WebLocalFrame* focused_frame = FocusedWebLocalFrameInWidget();
  if (!focused_frame)
    return;
  focused_frame->ExecuteCommand(WebString::FromLatin1("Undo"));
}

void WebFrameWidgetImpl::Redo() {
  WebLocalFrame* focused_frame = FocusedWebLocalFrameInWidget();
  if (!focused_frame)
    return;
  focused_frame->ExecuteCommand(WebString::FromLatin1("Redo"));
}

void WebFrameWidgetImpl::Cut() {
  WebLocalFrame* focused_frame = FocusedWebLocalFrameInWidget();
  if (!focused_frame)
    return;
  focused_frame->ExecuteCommand(WebString::FromLatin1("Cut"));
}

void WebFrameWidgetImpl::Copy() {
  WebLocalFrame* focused_frame = FocusedWebLocalFrameInWidget();
  if (!focused_frame)
    return;
  focused_frame->ExecuteCommand(WebString::FromLatin1("Copy"));
}

void WebFrameWidgetImpl::CopyToFindPboard() {
  WebLocalFrame* focused_frame = FocusedWebLocalFrameInWidget();
  if (!focused_frame)
    return;
  To<WebLocalFrameImpl>(focused_frame)->CopyToFindPboard();
}

void WebFrameWidgetImpl::Paste() {
  WebLocalFrame* focused_frame = FocusedWebLocalFrameInWidget();
  if (!focused_frame)
    return;
  focused_frame->ExecuteCommand(WebString::FromLatin1("Paste"));
}

void WebFrameWidgetImpl::PasteAndMatchStyle() {
  WebLocalFrame* focused_frame = FocusedWebLocalFrameInWidget();
  if (!focused_frame)
    return;
  focused_frame->ExecuteCommand(WebString::FromLatin1("PasteAndMatchStyle"));
}

void WebFrameWidgetImpl::Delete() {
  WebLocalFrame* focused_frame = FocusedWebLocalFrameInWidget();
  if (!focused_frame)
    return;
  focused_frame->ExecuteCommand(WebString::FromLatin1("Delete"));
}

void WebFrameWidgetImpl::SelectAll() {
  WebLocalFrame* focused_frame = FocusedWebLocalFrameInWidget();
  if (!focused_frame)
    return;
  focused_frame->ExecuteCommand(WebString::FromLatin1("SelectAll"));
}

void WebFrameWidgetImpl::CollapseSelection() {
  WebLocalFrame* focused_frame = FocusedWebLocalFrameInWidget();
  if (!focused_frame)
    return;
  const blink::WebRange& range =
      focused_frame->GetInputMethodController()->GetSelectionOffsets();
  if (range.IsNull())
    return;

  focused_frame->SelectRange(blink::WebRange(range.EndOffset(), 0),
                             blink::WebLocalFrame::kHideSelectionHandle,
                             mojom::blink::SelectionMenuBehavior::kHide);
}

void WebFrameWidgetImpl::Replace(const String& word) {
  auto* focused_frame = FocusedWebLocalFrameInWidget();
  if (!focused_frame)
    return;
  if (!focused_frame->HasSelection()) {
    focused_frame->SelectAroundCaret(mojom::blink::SelectionGranularity::kWord,
                                     /*should_show_handle=*/false,
                                     /*should_show_context_menu=*/false);
  }
  focused_frame->ReplaceSelection(word);
  // If the resulting selection is not actually a change in selection, we do not
  // need to explicitly notify about the selection change.
  focused_frame->Client()->SyncSelectionIfRequired(
      blink::SyncCondition::kNotForced);
}

void WebFrameWidgetImpl::ReplaceMisspelling(const String& word) {
  WebLocalFrame* focused_frame = FocusedWebLocalFrameInWidget();
  if (!focused_frame)
    return;
  if (!focused_frame->HasSelection())
    return;
  focused_frame->ReplaceMisspelledRange(word);
}

void WebFrameWidgetImpl::SelectRange(const gfx::Point& base_in_dips,
                                     const gfx::Point& extent_in_dips) {
  WebLocalFrame* focused_frame = FocusedWebLocalFrameInWidget();
  if (!focused_frame)
    return;
  focused_frame->SelectRange(
      widget_base_->DIPsToRoundedBlinkSpace(base_in_dips),
      widget_base_->DIPsToRoundedBlinkSpace(extent_in_dips));
}

void WebFrameWidgetImpl::AdjustSelectionByCharacterOffset(
    int32_t start,
    int32_t end,
    mojom::blink::SelectionMenuBehavior selection_menu_behavior) {
  WebLocalFrame* focused_frame = FocusedWebLocalFrameInWidget();
  if (!focused_frame)
    return;
  blink::WebRange range =
      focused_frame->GetInputMethodController()->GetSelectionOffsets();
  if (range.IsNull())
    return;

  // Sanity checks to disallow empty and out of range selections.
  if (start - end > range.length() || range.StartOffset() + start < 0)
    return;

  // A negative adjust amount moves the selection towards the beginning of
  // the document, a positive amount moves the selection towards the end of
  // the document.
  focused_frame->SelectRange(blink::WebRange(range.StartOffset() + start,
                                             range.length() + end - start),
                             blink::WebLocalFrame::kPreserveHandleVisibility,
                             selection_menu_behavior);
}

void WebFrameWidgetImpl::MoveRangeSelectionExtent(
    const gfx::Point& extent_in_dips) {
  WebLocalFrame* focused_frame = FocusedWebLocalFrameInWidget();
  if (!focused_frame)
    return;
  focused_frame->MoveRangeSelectionExtent(
      widget_base_->DIPsToRoundedBlinkSpace(extent_in_dips));
}

void WebFrameWidgetImpl::ScrollFocusedEditableNodeIntoView() {
  WebLocalFrameImpl* local_frame = FocusedWebLocalFrameInWidget();
  if (!local_frame)
    return;

  // OnSynchronizeVisualProperties does not call DidChangeVisibleViewport
  // on OOPIFs. Since we are starting a new scroll operation now, call
  // DidChangeVisibleViewport to ensure that we don't assume the element
  // is already in view and ignore the scroll.
  local_frame->ResetHasScrolledFocusedEditableIntoView();
  local_frame->ScrollFocusedEditableElementIntoView();
}

void WebFrameWidgetImpl::WaitForPageScaleAnimationForTesting(
    WaitForPageScaleAnimationForTestingCallback callback) {
  DCHECK(ForMainFrame());
  DCHECK(LocalRootImpl()->GetFrame()->IsOutermostMainFrame());
  page_scale_animation_for_testing_callback_ = std::move(callback);
}

void WebFrameWidgetImpl::ZoomToFindInPageRect(
    const gfx::Rect& rect_in_root_frame) {
  if (ForMainFrame()) {
    View()->ZoomToFindInPageRect(rect_in_root_frame);
  } else {
    GetAssociatedFrameWidgetHost()->ZoomToFindInPageRectInMainFrame(
        rect_in_root_frame);
  }
}

void WebFrameWidgetImpl::MoveCaret(const gfx::Point& point_in_dips) {
  WebLocalFrame* focused_frame = FocusedWebLocalFrameInWidget();
  if (!focused_frame)
    return;
  focused_frame->MoveCaretSelection(
      widget_base_->DIPsToRoundedBlinkSpace(point_in_dips));
}

#if BUILDFLAG(IS_ANDROID)
void WebFrameWidgetImpl::SelectAroundCaret(
    mojom::blink::SelectionGranularity granularity,
    bool should_show_handle,
    bool should_show_context_menu,
    SelectAroundCaretCallback callback) {
  auto* focused_frame = FocusedWebLocalFrameInWidget();
  if (!focused_frame) {
    std::move(callback).Run(std::move(nullptr));
    return;
  }

  int extended_start_adjust = 0;
  int extended_end_adjust = 0;
  int word_start_adjust = 0;
  int word_end_adjust = 0;
  blink::WebRange initial_range = focused_frame->SelectionRange();
  SetHandlingInputEvent(true);

  if (initial_range.IsNull()) {
    std::move(callback).Run(std::move(nullptr));
    return;
  }

  // If the requested granularity is not word, still calculate the hypothetical
  // word selection offsets. This is needed for contextual search to support
  // legacy semantics for the word that was tapped.
  blink::WebRange word_range;
  if (granularity != mojom::blink::SelectionGranularity::kWord) {
    word_range = focused_frame->GetWordSelectionRangeAroundCaret();
  }

  // Select around the caret at the specified |granularity|.
  if (!focused_frame->SelectAroundCaret(granularity, should_show_handle,
                                        should_show_context_menu)) {
    std::move(callback).Run(std::move(nullptr));
    return;
  }

  blink::WebRange extended_range = focused_frame->SelectionRange();
  DCHECK(!extended_range.IsNull());
  extended_start_adjust =
      extended_range.StartOffset() - initial_range.StartOffset();
  extended_end_adjust = extended_range.EndOffset() - initial_range.EndOffset();

  if (granularity == mojom::blink::SelectionGranularity::kWord) {
    // Since the requested granularity was word, simply set the word offset
    // to be the same as the extended offset values.
    word_start_adjust = extended_start_adjust;
    word_end_adjust = extended_end_adjust;
  } else {
    // Calculate the word offset compared to the initial selection (caret).
    DCHECK(!word_range.IsNull());
    word_start_adjust = word_range.StartOffset() - initial_range.StartOffset();
    word_end_adjust = word_range.EndOffset() - initial_range.EndOffset();
  }

  SetHandlingInputEvent(false);
  auto result = mojom::blink::SelectAroundCaretResult::New();
  result->extended_start_adjust = extended_start_adjust;
  result->extended_end_adjust = extended_end_adjust;
  result->word_start_adjust = word_start_adjust;
  result->word_end_adjust = word_end_adjust;
  std::move(callback).Run(std::move(result));
}
#endif

void WebFrameWidgetImpl::ForEachRemoteFrameControlledByWidget(
    const base::RepeatingCallback<void(RemoteFrame*)>& callback) {
  ForEachRemoteFrameChildrenControlledByWidget(local_root_->GetFrame(),
                                               callback);
}

void WebFrameWidgetImpl::CalculateSelectionBounds(
    gfx::Rect& anchor_root_frame,
    gfx::Rect& focus_root_frame,
    gfx::Rect* bounding_box_in_root_frame) {
  const LocalFrame* local_frame = FocusedLocalFrameInWidget();
  if (!local_frame)
    return;

  gfx::Rect anchor;
  gfx::Rect focus;
  auto& selection = local_frame->Selection();
  if (!selection.ComputeAbsoluteBounds(anchor, focus))
    return;

  // Apply the visual viewport for main frames this will apply the page scale.
  // For subframes it will just be a 1:1 transformation and the browser
  // will then apply later transformations to these rects.
  VisualViewport& visual_viewport = GetPage()->GetVisualViewport();
  anchor_root_frame = visual_viewport.RootFrameToViewport(
      local_frame->View()->ConvertToRootFrame(anchor));
  focus_root_frame = visual_viewport.RootFrameToViewport(
      local_frame->View()->ConvertToRootFrame(focus));

  // Calculate the bounding box of the selection area.
  if (bounding_box_in_root_frame) {
    const gfx::Rect bounding_box = ToEnclosingRect(
        CreateRange(selection.GetSelectionInDOMTree().ComputeRange())
            ->BoundingRect());
    *bounding_box_in_root_frame = visual_viewport.RootFrameToViewport(
        local_frame->View()->ConvertToRootFrame(bounding_box));
  }
}

const viz::LocalSurfaceId& WebFrameWidgetImpl::LocalSurfaceIdFromParent() {
  return widget_base_->local_surface_id_from_parent();
}

cc::LayerTreeHost* WebFrameWidgetImpl::LayerTreeHost() {
  return widget_base_->LayerTreeHost();
}

cc::LayerTreeHost* WebFrameWidgetImpl::LayerTreeHostForTesting() const {
  return widget_base_->LayerTreeHost();
}

ScreenMetricsEmulator* WebFrameWidgetImpl::DeviceEmulator() {
  return device_emulator_;
}

bool WebFrameWidgetImpl::AutoResizeMode() {
  return View()->AutoResizeMode();
}

void WebFrameWidgetImpl::SetScreenMetricsEmulationParameters(
    bool enabled,
    const DeviceEmulationParams& params) {
  if (enabled)
    View()->ActivateDevToolsTransform(params);
  else
    View()->DeactivateDevToolsTransform();
}

void WebFrameWidgetImpl::SetScreenInfoAndSize(
    const display::ScreenInfos& screen_infos,
    const gfx::Size& widget_size_in_dips,
    const gfx::Size& visible_viewport_size_in_dips) {
  // Emulation happens on regular main frames which don't use auto-resize mode.
  DCHECK(!AutoResizeMode());

  UpdateScreenInfo(screen_infos);
  widget_base_->SetVisibleViewportSizeInDIPs(visible_viewport_size_in_dips);
  Resize(widget_base_->DIPsToCeiledBlinkSpace(widget_size_in_dips));
}

float WebFrameWidgetImpl::GetCompositingScaleFactor() {
  return compositing_scale_factor_;
}

const cc::LayerTreeDebugState& WebFrameWidgetImpl::GetLayerTreeDebugState() {
  return widget_base_->LayerTreeHost()->GetDebugState();
}

void WebFrameWidgetImpl::SetLayerTreeDebugState(
    const cc::LayerTreeDebugState& state) {
  widget_base_->LayerTreeHost()->SetDebugState(state);
}

void WebFrameWidgetImpl::NotifyCompositingScaleFactorChanged(
    float compositing_scale_factor) {
  compositing_scale_factor_ = compositing_scale_factor;

  // Update the scale factor for remote frames which in turn depends on the
  // compositing scale factor set in the widget.
  ForEachRemoteFrameControlledByWidget(
      WTF::BindRepeating([](RemoteFrame* remote_frame) {
        if (remote_frame->View())
          remote_frame->View()->UpdateCompositingScaleFactor();
      }));
}

void WebFrameWidgetImpl::NotifyPageScaleFactorChanged(
    float page_scale_factor,
    bool is_pinch_gesture_active) {
  // Store the value to give to any new RemoteFrame that will be created as a
  // descendant of this widget.
  page_scale_factor_in_mainframe_ = page_scale_factor;
  is_pinch_gesture_active_in_mainframe_ = is_pinch_gesture_active;
  // Push the page scale factor down to any child RemoteFrames.
  // TODO(danakj): This ends up setting the page scale factor in the
  // RenderWidgetHost of the child WebFrameWidgetImpl, so that it can bounce
  // the value down to its WebFrameWidgetImpl. Since this is essentially a
  // global value per-page, we could instead store it once in the browser
  // (such as in RenderViewHost) and distribute it to each WebFrameWidgetImpl
  // from there.
  ForEachRemoteFrameControlledByWidget(WTF::BindRepeating(
      [](float page_scale_factor, bool is_pinch_gesture_active,
         RemoteFrame* remote_frame) {
        remote_frame->PageScaleFactorChanged(page_scale_factor,
                                             is_pinch_gesture_active);
      },
      page_scale_factor, is_pinch_gesture_active));
}

void WebFrameWidgetImpl::SetPageScaleStateAndLimits(
    float page_scale_factor,
    bool is_pinch_gesture_active,
    float minimum,
    float maximum) {
  widget_base_->LayerTreeHost()->SetPageScaleFactorAndLimits(page_scale_factor,
                                                             minimum, maximum);

  // Only propagate page scale from the main frame.
  if (ForMainFrame()) {
    // If page scale hasn't changed, then just return without notifying
    // the remote frames.
    if (page_scale_factor == page_scale_factor_in_mainframe_ &&
        is_pinch_gesture_active == is_pinch_gesture_active_in_mainframe_) {
      return;
    }

    NotifyPageScaleFactorChanged(page_scale_factor, is_pinch_gesture_active);
  }
}

void WebFrameWidgetImpl::UpdateViewportDescription(
    const ViewportDescription& viewport) {
  bool is_device_width = viewport.max_width.IsDeviceWidth();
  bool is_zoom_at_least_one = viewport.zoom >= 1.0 || viewport.min_zoom >= 1;
  widget_base_->LayerTreeHost()->UpdateViewportIsMobileOptimized(
      (is_device_width && is_zoom_at_least_one) ||
      (is_device_width && !viewport.zoom_is_explicit) ||
      (viewport.max_width.IsAuto() && is_zoom_at_least_one));
}

bool WebFrameWidgetImpl::UpdateScreenRects(
    const gfx::Rect& widget_screen_rect,
    const gfx::Rect& window_screen_rect) {
  if (!device_emulator_)
    return false;
  device_emulator_->OnUpdateScreenRects(widget_screen_rect, window_screen_rect);
  return true;
}

void WebFrameWidgetImpl::OrientationChanged() {
  local_root_->SendOrientationChangeEvent();
}

void WebFrameWidgetImpl::DidUpdateSurfaceAndScreen(
    const display::ScreenInfos& previous_original_screen_infos) {
  display::ScreenInfo screen_info = widget_base_->GetScreenInfo();
  View()->SetZoomFactorForDeviceScaleFactor(screen_info.device_scale_factor);

  if (ShouldAutoDetermineCompositingToLCDTextSetting()) {
    // This causes compositing state to be modified which dirties the
    // document lifecycle. Android Webview relies on the document
    // lifecycle being clean after the RenderWidget is initialized, in
    // order to send IPCs that query and change compositing state. So
    // WebFrameWidgetImpl::Resize() must come after this call, as it runs the
    // entire document lifecycle.
    View()->GetSettings()->SetPreferCompositingToLCDTextEnabled(
        widget_base_->ComputePreferCompositingToLCDText());
  }

  // When the device scale changes, the size and position of the popup would
  // need to be adjusted, which we can't do. Just close the popup, which is
  // also consistent with page zoom and resize behavior.
  display::ScreenInfos original_screen_infos = GetOriginalScreenInfos();
  if (previous_original_screen_infos.current().device_scale_factor !=
      original_screen_infos.current().device_scale_factor) {
    View()->CancelPagePopup();
  }

  const bool window_screen_has_changed =
      !Screen::AreWebExposedScreenPropertiesEqual(
          previous_original_screen_infos.current(),
          original_screen_infos.current());

  // Update Screens interface data before firing any events. The API is designed
  // to offer synchronous access to the most up-to-date cached screen
  // information when a change event is fired.  It is not required but it
  // is convenient to have all ScreenDetailed objects be up to date when any
  // window.screen events are fired as well.
  ForEachLocalFrameControlledByWidget(
      LocalRootImpl()->GetFrame(),
      WTF::BindRepeating(
          [](const display::ScreenInfos& original_screen_infos,
             bool window_screen_has_changed, WebLocalFrameImpl* local_frame) {
            auto* screen = local_frame->GetFrame()->DomWindow()->screen();
            screen->UpdateDisplayId(original_screen_infos.current().display_id);
            CoreInitializer::GetInstance().DidUpdateScreens(
                *local_frame->GetFrame(), original_screen_infos);
            if (window_screen_has_changed)
              screen->DispatchEvent(*Event::Create(event_type_names::kChange));
          },
          original_screen_infos, window_screen_has_changed));

  if (previous_original_screen_infos != original_screen_infos) {
    // Propagate changes down to child local root RenderWidgets and
    // BrowserPlugins in other frame trees/processes.
    ForEachRemoteFrameControlledByWidget(WTF::BindRepeating(
        [](const display::ScreenInfos& original_screen_infos,
           RemoteFrame* remote_frame) {
          remote_frame->DidChangeScreenInfos(original_screen_infos);
        },
        original_screen_infos));
  }
}

gfx::Rect WebFrameWidgetImpl::ViewportVisibleRect() {
  if (ForMainFrame()) {
    return widget_base_->CompositorViewportRect();
  } else {
    return child_data().compositor_visible_rect;
  }
}

absl::optional<display::mojom::blink::ScreenOrientation>
WebFrameWidgetImpl::ScreenOrientationOverride() {
  return View()->ScreenOrientationOverride();
}

void WebFrameWidgetImpl::WasHidden() {
  ForEachLocalFrameControlledByWidget(
      local_root_->GetFrame(),
      WTF::BindRepeating([](WebLocalFrameImpl* local_frame) {
        local_frame->Client()->WasHidden();
      }));
}

void WebFrameWidgetImpl::WasShown(bool was_evicted) {
  ForEachLocalFrameControlledByWidget(
      local_root_->GetFrame(),
      WTF::BindRepeating([](WebLocalFrameImpl* local_frame) {
        local_frame->Client()->WasShown();
      }));
  if (was_evicted) {
    ForEachRemoteFrameControlledByWidget(
        WTF::BindRepeating([](RemoteFrame* remote_frame) {
          // On eviction, the last SurfaceId is invalidated. We need to
          // allocate a new id.
          remote_frame->ResendVisualProperties();
        }));
  }
}

void WebFrameWidgetImpl::RunPaintBenchmark(int repeat_count,
                                           cc::PaintBenchmarkResult& result) {
  if (!ForMainFrame())
    return;
  if (auto* frame_view = LocalRootImpl()->GetFrameView())
    frame_view->RunPaintBenchmark(repeat_count, result);
}

void WebFrameWidgetImpl::NotifyInputObservers(
    const WebCoalescedInputEvent& coalesced_event) {
  LocalFrame* frame = FocusedLocalFrameInWidget();
  if (!frame)
    return;

  LocalFrameView* frame_view = frame->View();
  if (!frame_view)
    return;

  const WebInputEvent& input_event = coalesced_event.Event();
  auto& paint_timing_detector = frame_view->GetPaintTimingDetector();

  if (paint_timing_detector.NeedToNotifyInputOrScroll())
    paint_timing_detector.NotifyInputEvent(input_event.GetType());
}

Frame* WebFrameWidgetImpl::FocusedCoreFrame() const {
  return GetPage() ? GetPage()->GetFocusController().FocusedOrMainFrame()
                   : nullptr;
}

Element* WebFrameWidgetImpl::FocusedElement() const {
  LocalFrame* frame = GetPage()->GetFocusController().FocusedFrame();
  if (!frame)
    return nullptr;

  Document* document = frame->GetDocument();
  if (!document)
    return nullptr;

  return document->FocusedElement();
}

HitTestResult WebFrameWidgetImpl::HitTestResultForRootFramePos(
    const gfx::PointF& pos_in_root_frame) {
  gfx::PointF doc_point =
      LocalRootImpl()->GetFrame()->View()->ConvertFromRootFrame(
          pos_in_root_frame);
  HitTestLocation location(doc_point);
  HitTestResult result =
      LocalRootImpl()->GetFrame()->View()->HitTestWithThrottlingAllowed(
          location, HitTestRequest::kReadOnly | HitTestRequest::kActive);
  return result;
}

KURL WebFrameWidgetImpl::GetURLForDebugTrace() {
  WebFrame* main_frame = View()->MainFrame();
  if (main_frame->IsWebLocalFrame())
    return main_frame->ToWebLocalFrame()->GetDocument().Url();
  return {};
}

float WebFrameWidgetImpl::GetTestingDeviceScaleFactorOverride() {
  return device_scale_factor_for_testing_;
}

void WebFrameWidgetImpl::ReleaseMouseLockAndPointerCaptureForTesting() {
  GetPage()->GetPointerLockController().ExitPointerLock();
  MouseCaptureLost();
}

const viz::FrameSinkId& WebFrameWidgetImpl::GetFrameSinkId() {
  // It is valid to create a WebFrameWidget with an invalid frame sink id for
  // printing and placeholders. But if we go to use it, it should be valid.
  DCHECK(frame_sink_id_.is_valid());
  return frame_sink_id_;
}

WebHitTestResult WebFrameWidgetImpl::HitTestResultAt(const gfx::PointF& point) {
  return CoreHitTestResultAt(point);
}

void WebFrameWidgetImpl::SetZoomLevelForTesting(double zoom_level) {
  DCHECK(ForMainFrame());
  DCHECK_NE(zoom_level, -INFINITY);
  zoom_level_for_testing_ = zoom_level;
  SetZoomLevel(zoom_level);
}

void WebFrameWidgetImpl::ResetZoomLevelForTesting() {
  DCHECK(ForMainFrame());
  zoom_level_for_testing_ = -INFINITY;
  SetZoomLevel(0);
}

void WebFrameWidgetImpl::SetDeviceScaleFactorForTesting(float factor) {
  DCHECK(ForMainFrame());
  DCHECK_GE(factor, 0.f);

  // Stash the window size before we adjust the scale factor, as subsequent
  // calls to convert will use the new scale factor.
  gfx::Size size_in_dips = widget_base_->BlinkSpaceToFlooredDIPs(Size());
  device_scale_factor_for_testing_ = factor;

  // Receiving a 0 is used to reset between tests, it removes the override in
  // order to listen to the browser for the next test.
  if (!factor)
    return;

  // We are changing the device scale factor from the renderer, so allocate a
  // new viz::LocalSurfaceId to avoid surface invariants violations in tests.
  widget_base_->LayerTreeHost()->RequestNewLocalSurfaceId();

  display::ScreenInfos screen_infos = widget_base_->screen_infos();
  screen_infos.mutable_current().device_scale_factor = factor;
  gfx::Size size_with_dsf = gfx::ScaleToCeiledSize(size_in_dips, factor);
  widget_base_->UpdateCompositorViewportAndScreenInfo(gfx::Rect(size_with_dsf),
                                                      screen_infos);
  if (!AutoResizeMode()) {
    // This picks up the new device scale factor as
    // `UpdateCompositorViewportAndScreenInfo()` has applied a new value.
    Resize(widget_base_->DIPsToCeiledBlinkSpace(size_in_dips));
  }
}

FrameWidgetTestHelper*
WebFrameWidgetImpl::GetFrameWidgetTestHelperForTesting() {
  return nullptr;
}

void WebFrameWidgetImpl::SetMayThrottleIfUndrawnFrames(
    bool may_throttle_if_undrawn_frames) {
  if (!View()->does_composite())
    return;
  widget_base_->LayerTreeHost()->SetMayThrottleIfUndrawnFrames(
      may_throttle_if_undrawn_frames);
}

bool WebFrameWidgetImpl::GetMayThrottleIfUndrawnFramesForTesting() {
  return widget_base_->LayerTreeHost()
      ->GetMayThrottleIfUndrawnFramesForTesting();
}

WebPlugin* WebFrameWidgetImpl::GetFocusedPluginContainer() {
  LocalFrame* focused_frame = FocusedLocalFrameInWidget();
  if (!focused_frame)
    return nullptr;
  if (auto* container = focused_frame->GetWebPluginContainer())
    return container->Plugin();
  return nullptr;
}

bool WebFrameWidgetImpl::HasPendingPageScaleAnimation() {
  return LayerTreeHost()->HasPendingPageScaleAnimation();
}

void WebFrameWidgetImpl::SetSourceURLForCompositor(ukm::SourceId source_id,
                                                   const KURL& url) {
  LayerTreeHost()->SetSourceURL(source_id, GURL(url));
}

base::ReadOnlySharedMemoryRegion
WebFrameWidgetImpl::CreateSharedMemoryForSmoothnessUkm() {
  return LayerTreeHost()->CreateSharedMemoryForSmoothnessUkm();
}

bool WebFrameWidgetImpl::CanComposeInline() {
  if (auto* plugin = GetFocusedPluginContainer())
    return plugin->CanComposeInline();
  return true;
}

bool WebFrameWidgetImpl::ShouldDispatchImeEventsToPlugin() {
  if (auto* plugin = GetFocusedPluginContainer())
    return plugin->ShouldDispatchImeEventsToPlugin();
  return false;
}

void WebFrameWidgetImpl::ImeSetCompositionForPlugin(
    const String& text,
    const Vector<ui::ImeTextSpan>& ime_text_spans,
    const gfx::Range& replacement_range,
    int selection_start,
    int selection_end) {
  if (auto* plugin = GetFocusedPluginContainer()) {
    plugin->ImeSetCompositionForPlugin(
        text,
        std::vector<ui::ImeTextSpan>(ime_text_spans.begin(),
                                     ime_text_spans.end()),
        replacement_range, selection_start, selection_end);
  }
}

void WebFrameWidgetImpl::ImeCommitTextForPlugin(
    const String& text,
    const Vector<ui::ImeTextSpan>& ime_text_spans,
    const gfx::Range& replacement_range,
    int relative_cursor_pos) {
  if (auto* plugin = GetFocusedPluginContainer()) {
    plugin->ImeCommitTextForPlugin(
        text,
        std::vector<ui::ImeTextSpan>(ime_text_spans.begin(),
                                     ime_text_spans.end()),
        replacement_range, relative_cursor_pos);
  }
}

void WebFrameWidgetImpl::ImeFinishComposingTextForPlugin(bool keep_selection) {
  if (auto* plugin = GetFocusedPluginContainer())
    plugin->ImeFinishComposingTextForPlugin(keep_selection);
}

void WebFrameWidgetImpl::SetWindowRect(const gfx::Rect& requested_rect,
                                       const gfx::Rect& adjusted_rect) {
  DCHECK(ForMainFrame());
  SetPendingWindowRect(adjusted_rect);
  View()->SendWindowRectToMainFrameHost(
      requested_rect, WTF::Bind(&WebFrameWidgetImpl::AckPendingWindowRect,
                                WrapWeakPersistent(this)));
}

void WebFrameWidgetImpl::SetWindowRectSynchronouslyForTesting(
    const gfx::Rect& new_window_rect) {
  DCHECK(ForMainFrame());
  SetWindowRectSynchronously(new_window_rect);
}

void WebFrameWidgetImpl::SetWindowRectSynchronously(
    const gfx::Rect& new_window_rect) {
  // This method is only call in tests, and it applies the |new_window_rect| to
  // all three of:
  // a) widget size (in |size_|)
  // b) blink viewport (in |visible_viewport_size_|)
  // c) compositor viewport (in cc::LayerTreeHost)
  // Normally the browser controls these three things independently, but this is
  // used in tests to control the size from the renderer.

  // We are resizing the window from the renderer, so allocate a new
  // viz::LocalSurfaceId to avoid surface invariants violations in tests.
  widget_base_->LayerTreeHost()->RequestNewLocalSurfaceId();

  gfx::Rect compositor_viewport_pixel_rect(gfx::ScaleToCeiledSize(
      new_window_rect.size(),
      widget_base_->GetScreenInfo().device_scale_factor));
  widget_base_->UpdateSurfaceAndScreenInfo(
      widget_base_->local_surface_id_from_parent(),
      compositor_viewport_pixel_rect, widget_base_->screen_infos());

  Resize(new_window_rect.size());
  widget_base_->SetScreenRects(new_window_rect, new_window_rect);
}

void WebFrameWidgetImpl::DidCreateLocalRootView() {
  // If this WebWidget still hasn't received its size from the embedder, block
  // the parser. This is necessary, because the parser can cause layout to
  // happen, which needs to be done with the correct size.
  if (ForSubframe() && !size_) {
    child_data().did_suspend_parsing = true;
    LocalRootImpl()->GetFrame()->Loader().GetDocumentLoader()->BlockParser();
  }
}

bool WebFrameWidgetImpl::ShouldAutoDetermineCompositingToLCDTextSetting() {
  return true;
}

void WebFrameWidgetImpl::NotifyZoomLevelChanged(LocalFrame* root) {
  if (root) {
    Document* document = root->GetDocument();
    DCHECK(document);
    if (LocalFrameView* view = document->View())
      view->GetLayoutShiftTracker().NotifyZoomLevelChanged();
  }
}

}  // namespace blink
