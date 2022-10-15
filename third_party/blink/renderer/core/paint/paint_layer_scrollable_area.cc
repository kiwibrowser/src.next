/*
 * Copyright (C) 2006, 2007, 2008, 2009, 2010, 2011, 2012 Apple Inc. All rights
 * reserved.
 *
 * Portions are Copyright (C) 1998 Netscape Communications Corporation.
 *
 * Other contributors:
 *   Robert O'Callahan <roc+@cs.cmu.edu>
 *   David Baron <dbaron@dbaron.org>
 *   Christian Biesinger <cbiesinger@gmail.com>
 *   Randall Jesup <rjesup@wgate.com>
 *   Roland Mainz <roland.mainz@informatik.med.uni-giessen.de>
 *   Josh Soref <timeless@mac.com>
 *   Boris Zbarsky <bzbarsky@mit.edu>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA
 *
 * Alternatively, the contents of this file may be used under the terms
 * of either the Mozilla Public License Version 1.1, found at
 * http://www.mozilla.org/MPL/ (the "MPL") or the GNU General Public
 * License Version 2.0, found at http://www.fsf.org/copyleft/gpl.html
 * (the "GPL"), in which case the provisions of the MPL or the GPL are
 * applicable instead of those above.  If you wish to allow use of your
 * version of this file only under the terms of one of those two
 * licenses (the MPL or the GPL) and not to allow others to use your
 * version of this file under the LGPL, indicate your decision by
 * deletingthe provisions above and replace them with the notice and
 * other provisions required by the MPL or the GPL, as the case may be.
 * If you do not delete the provisions above, a recipient may use your
 * version of this file under any of the LGPL, the MPL or the GPL.
 */

#include "third_party/blink/renderer/core/paint/paint_layer_scrollable_area.h"

#include <utility>

#include "base/numerics/checked_math.h"
#include "base/task/single_thread_task_runner.h"
#include "cc/animation/animation_timeline.h"
#include "cc/input/main_thread_scrolling_reason.h"
#include "cc/input/snap_selection_strategy.h"
#include "cc/layers/picture_layer.h"
#include "third_party/blink/public/common/features.h"
#include "third_party/blink/public/mojom/scroll/scroll_into_view_params.mojom-blink.h"
#include "third_party/blink/public/mojom/scroll/scrollbar_mode.mojom-blink.h"
#include "third_party/blink/public/platform/platform.h"
#include "third_party/blink/public/platform/task_type.h"
#include "third_party/blink/renderer/core/accessibility/ax_object_cache.h"
#include "third_party/blink/renderer/core/animation/scroll_timeline.h"
#include "third_party/blink/renderer/core/content_capture/content_capture_manager.h"
#include "third_party/blink/renderer/core/css/style_request.h"
#include "third_party/blink/renderer/core/dom/dom_node_ids.h"
#include "third_party/blink/renderer/core/dom/node.h"
#include "third_party/blink/renderer/core/dom/shadow_root.h"
#include "third_party/blink/renderer/core/editing/frame_selection.h"
#include "third_party/blink/renderer/core/editing/markers/document_marker_controller.h"
#include "third_party/blink/renderer/core/frame/local_frame.h"
#include "third_party/blink/renderer/core/frame/local_frame_client.h"
#include "third_party/blink/renderer/core/frame/local_frame_view.h"
#include "third_party/blink/renderer/core/frame/page_scale_constraints_set.h"
#include "third_party/blink/renderer/core/frame/root_frame_viewport.h"
#include "third_party/blink/renderer/core/frame/settings.h"
#include "third_party/blink/renderer/core/frame/visual_viewport.h"
#include "third_party/blink/renderer/core/fullscreen/fullscreen.h"
#include "third_party/blink/renderer/core/html/forms/text_control_element.h"
#include "third_party/blink/renderer/core/html/html_frame_owner_element.h"
#include "third_party/blink/renderer/core/input/event_handler.h"
#include "third_party/blink/renderer/core/inspector/inspector_trace_events.h"
#include "third_party/blink/renderer/core/layout/custom_scrollbar.h"
#include "third_party/blink/renderer/core/layout/layout_custom_scrollbar_part.h"
#include "third_party/blink/renderer/core/layout/layout_embedded_content.h"
#include "third_party/blink/renderer/core/layout/layout_flexible_box.h"
#include "third_party/blink/renderer/core/layout/layout_theme.h"
#include "third_party/blink/renderer/core/layout/layout_view.h"
#include "third_party/blink/renderer/core/layout/ng/legacy_layout_tree_walking.h"
#include "third_party/blink/renderer/core/loader/document_loader.h"
#include "third_party/blink/renderer/core/page/chrome_client.h"
#include "third_party/blink/renderer/core/page/focus_controller.h"
#include "third_party/blink/renderer/core/page/page.h"
#include "third_party/blink/renderer/core/page/scrolling/fragment_anchor.h"
#include "third_party/blink/renderer/core/page/scrolling/root_scroller_controller.h"
#include "third_party/blink/renderer/core/page/scrolling/scrolling_coordinator.h"
#include "third_party/blink/renderer/core/page/scrolling/snap_coordinator.h"
#include "third_party/blink/renderer/core/page/scrolling/sticky_position_scrolling_constraints.h"
#include "third_party/blink/renderer/core/page/scrolling/top_document_root_scroller_controller.h"
#include "third_party/blink/renderer/core/paint/compositing/compositing_reason_finder.h"
#include "third_party/blink/renderer/core/paint/object_paint_invalidator.h"
#include "third_party/blink/renderer/core/paint/paint_invalidator.h"
#include "third_party/blink/renderer/core/paint/paint_layer.h"
#include "third_party/blink/renderer/core/paint/paint_layer_fragment.h"
#include "third_party/blink/renderer/core/scroll/scroll_alignment.h"
#include "third_party/blink/renderer/core/scroll/scroll_animator_base.h"
#include "third_party/blink/renderer/core/scroll/scrollbar_theme.h"
#include "third_party/blink/renderer/core/scroll/smooth_scroll_sequencer.h"
#include "third_party/blink/renderer/platform/graphics/paint/drawing_recorder.h"
#include "third_party/blink/renderer/platform/heap/garbage_collected.h"
#include "ui/base/ui_base_features.h"
#include "ui/gfx/geometry/point_conversions.h"

namespace blink {

namespace {

// Default value is set to 15 as the default
// minimum size used by firefox is 15x15.
static const int kDefaultMinimumWidthForResizing = 15;
static const int kDefaultMinimumHeightForResizing = 15;

}  // namespace

PaintLayerScrollableAreaRareData::PaintLayerScrollableAreaRareData() = default;

void PaintLayerScrollableAreaRareData::Trace(Visitor* visitor) const {
  visitor->Trace(sticky_layers_);
  visitor->Trace(anchor_positioned_layers_);
}

const int kResizerControlExpandRatioForTouch = 2;

PaintLayerScrollableArea::PaintLayerScrollableArea(PaintLayer& layer)
    : ScrollableArea(layer.GetLayoutBox()
                         ->GetDocument()
                         .GetPage()
                         ->GetAgentGroupScheduler()
                         .CompositorTaskRunner()),
      layer_(&layer),
      in_resize_mode_(false),
      scrolls_overflow_(false),
      in_overflow_relayout_(false),
      allow_second_overflow_relayout_(false),
      needs_composited_scrolling_(false),
      needs_scroll_offset_clamp_(false),
      needs_relayout_(false),
      had_horizontal_scrollbar_before_relayout_(false),
      had_vertical_scrollbar_before_relayout_(false),
      had_resizer_before_relayout_(false),
      scroll_origin_changed_(false),
      is_scrollbar_freeze_root_(false),
      is_horizontal_scrollbar_frozen_(false),
      is_vertical_scrollbar_frozen_(false),
      scrollbar_manager_(*this),
      has_last_committed_scroll_offset_(false),
      scroll_corner_(nullptr),
      resizer_(nullptr),
      scroll_anchor_(this),
      non_composited_main_thread_scrolling_reasons_(0) {
  if (auto* element = DynamicTo<Element>(GetLayoutBox()->GetNode())) {
    // We save and restore only the scrollOffset as the other scroll values are
    // recalculated.
    scroll_offset_ = element->SavedLayerScrollOffset();
    if (!scroll_offset_.IsZero())
      GetScrollAnimator().SetCurrentOffset(scroll_offset_);
    element->SetSavedLayerScrollOffset(ScrollOffset());
  }

  GetLayoutBox()->GetDocument().GetSnapCoordinator().AddSnapContainer(
      *GetLayoutBox());
}

PaintLayerScrollableArea::~PaintLayerScrollableArea() {
  CHECK(HasBeenDisposed());
}

PaintLayerScrollableArea* PaintLayerScrollableArea::FromNode(const Node& node) {
  const LayoutBox* box = node.GetLayoutBox();
  return box ? box->GetScrollableArea() : nullptr;
}

void PaintLayerScrollableArea::DidCompositorScroll(
    const gfx::PointF& position) {
  ScrollableArea::DidCompositorScroll(position);
  // This should be alive if it receives composited scroll callbacks.
  CHECK(!HasBeenDisposed());
}

void PaintLayerScrollableArea::DisposeImpl() {
  rare_data_.Clear();

  GetLayoutBox()->GetDocument().GetSnapCoordinator().RemoveSnapContainer(
      *GetLayoutBox());

  if (InResizeMode() && !GetLayoutBox()->DocumentBeingDestroyed()) {
    if (LocalFrame* frame = GetLayoutBox()->GetFrame())
      frame->GetEventHandler().ResizeScrollableAreaDestroyed();
  }

  if (LocalFrame* frame = GetLayoutBox()->GetFrame()) {
    if (LocalFrameView* frame_view = frame->View()) {
      frame_view->RemoveScrollAnchoringScrollableArea(this);
      frame_view->RemoveUserScrollableArea(this);
      frame_view->RemoveAnimatingScrollableArea(this);
    }
  }

  non_composited_main_thread_scrolling_reasons_ = 0;

  if (!GetLayoutBox()->DocumentBeingDestroyed()) {
    if (auto* element = DynamicTo<Element>(GetLayoutBox()->GetNode()))
      element->SetSavedLayerScrollOffset(scroll_offset_);
  }

  // Note: it is not safe to call ScrollAnchor::clear if the document is being
  // destroyed, because LayoutObjectChildList::removeChildNode skips the call to
  // willBeRemovedFromTree,
  // leaving the ScrollAnchor with a stale LayoutObject pointer.
  scroll_anchor_.Dispose();

  GetLayoutBox()
      ->GetDocument()
      .GetPage()
      ->GlobalRootScrollerController()
      .DidDisposeScrollableArea(*this);

  scrollbar_manager_.Dispose();

  if (scroll_corner_)
    scroll_corner_->Destroy();
  if (resizer_)
    resizer_->Destroy();

  ClearScrollableArea();

  if (SmoothScrollSequencer* sequencer = GetSmoothScrollSequencer())
    sequencer->DidDisposeScrollableArea(*this);

  RunScrollCompleteCallbacks();
  InvalidateScrollTimeline();

  layer_ = nullptr;
}

void PaintLayerScrollableArea::ApplyPendingHistoryRestoreScrollOffset() {
  if (!pending_view_state_)
    return;

  // TODO(pnoland): attempt to restore the anchor in more places than this.
  // Anchor-based restore should allow for earlier restoration.
  bool did_restore = RestoreScrollAnchor(
      {pending_view_state_->scroll_anchor_data_.selector_,
       LayoutPoint(pending_view_state_->scroll_anchor_data_.offset_),
       pending_view_state_->scroll_anchor_data_.simhash_});
  if (!did_restore) {
    SetScrollOffset(pending_view_state_->scroll_offset_,
                    mojom::blink::ScrollType::kProgrammatic,
                    mojom::blink::ScrollBehavior::kAuto);
  }

  pending_view_state_.reset();
}

void PaintLayerScrollableArea::SetTickmarksOverride(
    Vector<gfx::Rect> tickmarks) {
  EnsureRareData().tickmarks_override_ = std::move(tickmarks);
}

void PaintLayerScrollableArea::Trace(Visitor* visitor) const {
  visitor->Trace(scrollbar_manager_);
  visitor->Trace(scroll_corner_);
  visitor->Trace(resizer_);
  visitor->Trace(scroll_anchor_);
  visitor->Trace(scrolling_background_display_item_client_);
  visitor->Trace(scroll_corner_display_item_client_);
  visitor->Trace(layer_);
  visitor->Trace(rare_data_);
  ScrollableArea::Trace(visitor);
}

bool PaintLayerScrollableArea::IsThrottled() const {
  return GetLayoutBox()->GetFrame()->ShouldThrottleRendering();
}

ChromeClient* PaintLayerScrollableArea::GetChromeClient() const {
  if (HasBeenDisposed())
    return nullptr;
  if (Page* page = GetLayoutBox()->GetFrame()->GetPage())
    return &page->GetChromeClient();
  return nullptr;
}

SmoothScrollSequencer* PaintLayerScrollableArea::GetSmoothScrollSequencer()
    const {
  if (HasBeenDisposed())
    return nullptr;

  return &GetLayoutBox()->GetFrame()->GetSmoothScrollSequencer();
}

bool PaintLayerScrollableArea::IsActive() const {
  Page* page = GetLayoutBox()->GetFrame()->GetPage();
  return page && page->GetFocusController().IsActive();
}

bool PaintLayerScrollableArea::IsScrollCornerVisible() const {
  return !ScrollCornerRect().IsEmpty();
}

static int CornerStart(const LayoutBox& box,
                       int min_x,
                       int max_x,
                       int thickness) {
  if (box.ShouldPlaceBlockDirectionScrollbarOnLogicalLeft())
    return min_x + box.StyleRef().BorderLeftWidth().ToFloat();
  return max_x - thickness - box.StyleRef().BorderRightWidth().ToFloat();
}

gfx::Rect PaintLayerScrollableArea::CornerRect() const {
  int horizontal_thickness;
  int vertical_thickness;
  if (!VerticalScrollbar() && !HorizontalScrollbar()) {
    // We need to know the thickness of custom scrollbars even when they don't
    // exist in order to set the resizer square size properly.
    horizontal_thickness = GetPageScrollbarTheme().ScrollbarThickness(
        ScaleFromDIP(), EScrollbarWidth::kAuto);
    vertical_thickness = horizontal_thickness;
  } else if (VerticalScrollbar() && !HorizontalScrollbar()) {
    horizontal_thickness = VerticalScrollbar()->ScrollbarThickness();
    vertical_thickness = horizontal_thickness;
  } else if (HorizontalScrollbar() && !VerticalScrollbar()) {
    vertical_thickness = HorizontalScrollbar()->ScrollbarThickness();
    horizontal_thickness = vertical_thickness;
  } else {
    horizontal_thickness = VerticalScrollbar()->ScrollbarThickness();
    vertical_thickness = HorizontalScrollbar()->ScrollbarThickness();
  }
  gfx::Size border_box_size = PixelSnappedBorderBoxSize();
  return gfx::Rect(CornerStart(*GetLayoutBox(), 0, border_box_size.width(),
                               horizontal_thickness),
                   border_box_size.height() - vertical_thickness -
                       GetLayoutBox()->StyleRef().BorderBottomWidth().ToFloat(),
                   horizontal_thickness, vertical_thickness);
}

gfx::Rect PaintLayerScrollableArea::ScrollCornerRect() const {
  // We have a scrollbar corner when a scrollbar is visible and not filling the
  // entire length of the box.
  // This happens when:
  // (a) A resizer is present and at least one scrollbar is present
  // (b) Both scrollbars are present.
  bool has_horizontal_bar = HorizontalScrollbar();
  bool has_vertical_bar = VerticalScrollbar();
  bool has_resizer = GetLayoutBox()->CanResize();
  if ((has_horizontal_bar && has_vertical_bar) ||
      (has_resizer && (has_horizontal_bar || has_vertical_bar))) {
    return CornerRect();
  }
  return gfx::Rect();
}

void PaintLayerScrollableArea::SetScrollCornerNeedsPaintInvalidation() {
  ScrollableArea::SetScrollCornerNeedsPaintInvalidation();
}

gfx::Rect
PaintLayerScrollableArea::ConvertFromScrollbarToContainingEmbeddedContentView(
    const Scrollbar& scrollbar,
    const gfx::Rect& scrollbar_rect) const {
  LayoutView* view = GetLayoutBox()->View();
  if (!view)
    return scrollbar_rect;

  gfx::Rect rect = scrollbar_rect;
  rect.Offset(ScrollbarOffset(scrollbar));
  return ToPixelSnappedRect(
      GetLayoutBox()->LocalToAbsoluteRect(PhysicalRect(rect)));
}

gfx::Point
PaintLayerScrollableArea::ConvertFromScrollbarToContainingEmbeddedContentView(
    const Scrollbar& scrollbar,
    const gfx::Point& scrollbar_point) const {
  LayoutView* view = GetLayoutBox()->View();
  if (!view)
    return scrollbar_point;

  gfx::Point point = scrollbar_point + ScrollbarOffset(scrollbar);
  return ToRoundedPoint(
      GetLayoutBox()->LocalToAbsolutePoint(PhysicalOffset(point)));
}

gfx::Point
PaintLayerScrollableArea::ConvertFromContainingEmbeddedContentViewToScrollbar(
    const Scrollbar& scrollbar,
    const gfx::Point& parent_point) const {
  LayoutView* view = GetLayoutBox()->View();
  if (!view)
    return parent_point;

  gfx::Point point = ToRoundedPoint(
      GetLayoutBox()->AbsoluteToLocalPoint(PhysicalOffset(parent_point)));
  point -= ScrollbarOffset(scrollbar);
  return point;
}

gfx::Point PaintLayerScrollableArea::ConvertFromRootFrame(
    const gfx::Point& point_in_root_frame) const {
  LayoutView* view = GetLayoutBox()->View();
  if (!view)
    return point_in_root_frame;

  return view->GetFrameView()->ConvertFromRootFrame(point_in_root_frame);
}

gfx::Point PaintLayerScrollableArea::ConvertFromRootFrameToVisualViewport(
    const gfx::Point& point_in_root_frame) const {
  LocalFrameView* frame_view = GetLayoutBox()->GetFrameView();
  DCHECK(frame_view);
  const auto* page = frame_view->GetPage();
  const auto& viewport = page->GetVisualViewport();
  return viewport.RootFrameToViewport(point_in_root_frame);
}

int PaintLayerScrollableArea::ScrollSize(
    ScrollbarOrientation orientation) const {
  gfx::Vector2d scroll_dimensions =
      MaximumScrollOffsetInt() - MinimumScrollOffsetInt();
  return (orientation == kHorizontalScrollbar) ? scroll_dimensions.x()
                                               : scroll_dimensions.y();
}

void PaintLayerScrollableArea::UpdateScrollOffset(
    const ScrollOffset& new_offset,
    mojom::blink::ScrollType scroll_type) {
  if (HasBeenDisposed() || GetScrollOffset() == new_offset)
    return;

  TRACE_EVENT2("blink", "PaintLayerScrollableArea::UpdateScrollOffset", "x",
               new_offset.x(), "y", new_offset.y());
  TRACE_EVENT_INSTANT1("blink", "Type", TRACE_EVENT_SCOPE_THREAD, "type",
                       scroll_type);

  scroll_offset_ = new_offset;

  LocalFrame* frame = GetLayoutBox()->GetFrame();
  DCHECK(frame);

  LocalFrameView* frame_view = GetLayoutBox()->GetFrameView();
  bool is_root_layer = Layer()->IsRootLayer();

  DEVTOOLS_TIMELINE_TRACE_EVENT(
      "ScrollLayer", inspector_scroll_layer_event::Data, GetLayoutBox());

  // Update the positions of our child layers (if needed as only fixed layers
  // should be impacted by a scroll).
  if (!frame_view->IsInPerformLayout()) {
    // Update regions, scrolling may change the clip of a particular region.
    frame_view->UpdateDocumentAnnotatedRegions();

    // As a performance optimization, the scroll offset of the root layer is
    // not included in EmbeddedContentView's stored frame rect, so there is no
    // reason to mark the FrameView as needing a geometry update here.
    if (is_root_layer)
      frame_view->SetRootLayerDidScroll();
    else
      frame_view->SetNeedsUpdateGeometries();
  }

  if (auto* scrolling_coordinator = GetScrollingCoordinator()) {
    if (!scrolling_coordinator->UpdateCompositorScrollOffset(*frame, *this)) {
      GetLayoutBox()->GetFrameView()->SetPaintArtifactCompositorNeedsUpdate(
          PaintArtifactCompositorUpdateReason::
              kPaintLayerScrollableAreaUpdateScrollOffset);
    }
  }

  // The ScrollOffsetTranslation paint property depends on the scroll offset.
  // (see: PaintPropertyTreeBuilder::UpdateScrollAndScrollTranslation).
  GetLayoutBox()->SetNeedsPaintPropertyUpdatePreservingCachedRects();
  InvalidateScrollTimeline();

  if (scroll_type == mojom::blink::ScrollType::kUser ||
      scroll_type == mojom::blink::ScrollType::kCompositor) {
    Page* page = frame->GetPage();
    if (page)
      page->GetChromeClient().ClearToolTip(*frame);
  }

  InvalidatePaintForScrollOffsetChange();

  // Don't enqueue a scroll event yet for scroll reasons that are not about
  // explicit changes to scroll. Instead, only do so at the time of the next
  // lifecycle update, to avoid scroll events that are out of date or don't
  // result in an actual scroll that is visible to the user. These scroll events
  // will then be dispatched at the *subsequent* animation frame, because
  // they happen after layout and therefore the next opportunity to fire the
  // events is at the next lifecycle update (*).
  //
  // (*) https://html.spec.whatwg.org/C/#update-the-rendering steps
  if (scroll_type == mojom::blink::ScrollType::kClamping ||
      scroll_type == mojom::blink::ScrollType::kAnchoring) {
    if (GetLayoutBox()->GetNode())
      frame_view->SetNeedsEnqueueScrollEvent(this);
  } else {
    EnqueueScrollEventIfNeeded();
  }

  GetLayoutBox()->View()->ClearHitTestCache();

  // Inform the FrameLoader of the new scroll position, so it can be restored
  // when navigating back.
  if (is_root_layer) {
    frame_view->GetFrame().Loader().SaveScrollState();
    frame_view->DidChangeScrollOffset();
    if (scroll_type == mojom::blink::ScrollType::kCompositor ||
        scroll_type == mojom::blink::ScrollType::kUser) {
      if (DocumentLoader* document_loader = frame->Loader().GetDocumentLoader())
        document_loader->GetInitialScrollState().was_scrolled_by_user = true;
    }
  }

  if (FragmentAnchor* anchor = frame_view->GetFragmentAnchor())
    anchor->DidScroll(scroll_type);

  if (IsExplicitScrollType(scroll_type)) {
    if (scroll_type != mojom::blink::ScrollType::kCompositor)
      ShowNonMacOverlayScrollbars();
    GetScrollAnchor()->Clear();
  }
  if (ContentCaptureManager* manager = frame_view->GetFrame()
                                           .LocalFrameRoot()
                                           .GetOrResetContentCaptureManager()) {
    manager->OnScrollPositionChanged();
  }
  if (AXObjectCache* cache =
          GetLayoutBox()->GetDocument().ExistingAXObjectCache())
    cache->HandleScrollPositionChanged(GetLayoutBox());
}

void PaintLayerScrollableArea::InvalidatePaintForScrollOffsetChange() {
  InvalidatePaintForStickyDescendants();
  InvalidatePaintForAnchorPositionedLayers();

  auto* box = GetLayoutBox();
  auto* frame_view = box->GetFrameView();
  frame_view->InvalidateBackgroundAttachmentFixedDescendantsOnScroll(*box);

  // TODO(chrishtr): remove this slow path once crbug.com/906885 is fixed.
  // See also https://bugs.chromium.org/p/chromium/issues/detail?id=903287#c10.
  if (Layer()->EnclosingPaginationLayer())
    box->SetSubtreeShouldCheckForPaintInvalidation();

  if (!box->BackgroundNeedsFullPaintInvalidation()) {
    auto background_paint_location = box->GetBackgroundPaintLocation();
    bool background_paint_in_border_box =
        background_paint_location & kBackgroundPaintInBorderBoxSpace;
    bool background_paint_in_scrolling_contents =
        background_paint_location & kBackgroundPaintInContentsSpace;

    // Invalidate background on scroll if needed.
    // Fixed attachment background has been dealt with in
    // frame_view->InvalidateBackgroundAttachmentFixedDescendantsOnScroll().
    const auto& background_layers = box->StyleRef().BackgroundLayers();
    if (background_layers.AnyLayerHasLocalAttachmentImage() &&
        background_paint_in_border_box) {
      // Local-attachment background image scrolls, so needs invalidation if it
      // paints in non-scrolling space.
      box->SetBackgroundNeedsFullPaintInvalidation();
    } else if (background_layers.AnyLayerHasDefaultAttachmentImage() &&
               background_paint_in_scrolling_contents) {
      // Normal attachment background image doesn't scroll, so needs
      // invalidation if it paints in scrolling contents.
      box->SetBackgroundNeedsFullPaintInvalidation();
    } else if (background_layers.AnyLayerHasLocalAttachment() &&
               background_layers.AnyLayerUsesContentBox() &&
               background_paint_in_border_box &&
               (box->PaddingLeft() || box->PaddingTop() ||
                box->PaddingRight() || box->PaddingBottom())) {
      // Local attachment content box background needs invalidation if there is
      // padding because the content area can change on scroll (e.g. the top
      // padding can disappear when the box scrolls to the bottom).
      box->SetBackgroundNeedsFullPaintInvalidation();
    }
  }
}

gfx::Vector2d PaintLayerScrollableArea::ScrollOffsetInt() const {
  return gfx::ToFlooredVector2d(scroll_offset_);
}

ScrollOffset PaintLayerScrollableArea::GetScrollOffset() const {
  return scroll_offset_;
}

void PaintLayerScrollableArea::EnqueueScrollEventIfNeeded() {
  if (scroll_offset_ == last_committed_scroll_offset_ &&
      has_last_committed_scroll_offset_)
    return;
  last_committed_scroll_offset_ = scroll_offset_;
  has_last_committed_scroll_offset_ = true;
  if (HasBeenDisposed())
    return;
  // Schedule the scroll DOM event.
  if (auto* node = EventTargetNode())
    node->GetDocument().EnqueueScrollEventForNode(node);
}

gfx::Vector2d PaintLayerScrollableArea::MinimumScrollOffsetInt() const {
  return -ScrollOrigin().OffsetFromOrigin();
}

gfx::Vector2d PaintLayerScrollableArea::MaximumScrollOffsetInt() const {
  if (!GetLayoutBox() || !GetLayoutBox()->IsScrollContainer())
    return -ScrollOrigin().OffsetFromOrigin();

  gfx::Size content_size = ContentsSize();

  Page* page = GetLayoutBox()->GetDocument().GetPage();
  DCHECK(page);
  TopDocumentRootScrollerController& controller =
      page->GlobalRootScrollerController();

  // The global root scroller should be clipped by the top LocalFrameView rather
  // than it's overflow clipping box. This is to ensure that content exposed by
  // hiding the URL bar at the bottom of the screen is visible.
  gfx::Size visible_size;
  if (this == controller.RootScrollerArea()) {
    visible_size = controller.RootScrollerVisibleArea();
  } else {
    visible_size = ToPixelSnappedRect(GetLayoutBox()->OverflowClipRect(
                                          GetLayoutBox()->Location(),
                                          kIgnoreOverlayScrollbarSize))
                       .size();
  }

  // TODO(skobes): We should really ASSERT that contentSize >= visibleSize
  // when we are not the root layer, but we can't because contentSize is
  // based on stale layout overflow data (http://crbug.com/576933).
  content_size.SetToMax(visible_size);

  return -ScrollOrigin().OffsetFromOrigin() +
         gfx::Vector2d(content_size.width() - visible_size.width(),
                       content_size.height() - visible_size.height());
}

void PaintLayerScrollableArea::VisibleSizeChanged() {
  InvalidateScrollTimeline();
  ShowNonMacOverlayScrollbars();
}

PhysicalRect PaintLayerScrollableArea::LayoutContentRect(
    IncludeScrollbarsInRect scrollbar_inclusion) const {
  // LayoutContentRect is conceptually the same as the box's client rect.
  LayoutSize layer_size(Layer()->Size());
  LayoutUnit border_width = GetLayoutBox()->BorderWidth();
  LayoutUnit border_height = GetLayoutBox()->BorderHeight();
  NGPhysicalBoxStrut scrollbars;
  if (scrollbar_inclusion == kExcludeScrollbars)
    scrollbars = GetLayoutBox()->ComputeScrollbars();

  PhysicalSize size(
      layer_size.Width() - border_width - scrollbars.HorizontalSum(),
      layer_size.Height() - border_height - scrollbars.VerticalSum());
  size.ClampNegativeToZero();
  return PhysicalRect(PhysicalOffset::FromPointFRound(ScrollPosition()), size);
}

gfx::Rect PaintLayerScrollableArea::VisibleContentRect(
    IncludeScrollbarsInRect scrollbar_inclusion) const {
  PhysicalRect layout_content_rect(LayoutContentRect(scrollbar_inclusion));
  // TODO(szager): It's not clear that Floor() is the right thing to do here;
  // what is the correct behavior for fractional scroll offsets?
  return gfx::Rect(ToFlooredPoint(layout_content_rect.offset),
                   ToPixelSnappedSize(layout_content_rect.size.ToLayoutSize(),
                                      GetLayoutBox()->Location()));
}

PhysicalRect PaintLayerScrollableArea::VisibleScrollSnapportRect(
    IncludeScrollbarsInRect scrollbar_inclusion) const {
  const ComputedStyle* style = GetLayoutBox()->Style();
  PhysicalRect layout_content_rect(LayoutContentRect(scrollbar_inclusion));
  layout_content_rect.Move(PhysicalOffset(-ScrollOrigin().OffsetFromOrigin()));
  LayoutRectOutsets padding(MinimumValueForLength(style->ScrollPaddingTop(),
                                                  layout_content_rect.Height()),
                            MinimumValueForLength(style->ScrollPaddingRight(),
                                                  layout_content_rect.Width()),
                            MinimumValueForLength(style->ScrollPaddingBottom(),
                                                  layout_content_rect.Height()),
                            MinimumValueForLength(style->ScrollPaddingLeft(),
                                                  layout_content_rect.Width()));
  layout_content_rect.Contract(padding);
  return layout_content_rect;
}

gfx::Size PaintLayerScrollableArea::ContentsSize() const {
  PhysicalOffset offset(
      GetLayoutBox()->ClientLeft() + GetLayoutBox()->Location().X(),
      GetLayoutBox()->ClientTop() + GetLayoutBox()->Location().Y());
  // TODO(crbug.com/962299): The pixel snapping is incorrect in some cases.
  return PixelSnappedContentsSize(offset);
}

gfx::Size PaintLayerScrollableArea::PixelSnappedContentsSize(
    const PhysicalOffset& paint_offset) const {
  return ToPixelSnappedRect(PhysicalRect(paint_offset, overflow_rect_.size))
      .size();
}

void PaintLayerScrollableArea::ContentsResized() {
  ScrollableArea::ContentsResized();
  // Need to update the bounds of the scroll property.
  GetLayoutBox()->SetNeedsPaintPropertyUpdate();
  Layer()->SetNeedsCompositingInputsUpdate();
  InvalidateScrollTimeline();
}

gfx::Point PaintLayerScrollableArea::LastKnownMousePosition() const {
  return GetLayoutBox()->GetFrame()
             ? gfx::ToFlooredPoint(GetLayoutBox()
                                       ->GetFrame()
                                       ->GetEventHandler()
                                       .LastKnownMousePositionInRootFrame())
             : gfx::Point();
}

bool PaintLayerScrollableArea::ScrollAnimatorEnabled() const {
  if (HasBeenDisposed())
    return false;
  if (Settings* settings = GetLayoutBox()->GetFrame()->GetSettings())
    return settings->GetScrollAnimatorEnabled();
  return false;
}

bool PaintLayerScrollableArea::ShouldSuspendScrollAnimations() const {
  if (HasBeenDisposed())
    return true;
  LayoutView* view = GetLayoutBox()->View();
  if (!view)
    return true;
  return !GetLayoutBox()->GetDocument().LoadEventFinished();
}

void PaintLayerScrollableArea::ScrollbarVisibilityChanged() {
  UpdateScrollbarEnabledState();

  // Paint properties need to be updated, because clip rects
  // are affected by overlay scrollbars.
  layer_->GetLayoutObject().SetNeedsPaintPropertyUpdate();

  if (LayoutView* view = GetLayoutBox()->View())
    view->ClearHitTestCache();
}

void PaintLayerScrollableArea::ScrollbarFrameRectChanged() {
  // Size of non-overlay scrollbar affects overflow clip rect. size of overlay
  // scrollbar effects hit testing rect excluding overlay scrollbars.
  GetLayoutBox()->SetNeedsPaintPropertyUpdate();
}

bool PaintLayerScrollableArea::ScrollbarsCanBeActive() const {
  LayoutView* view = GetLayoutBox()->View();
  if (!view)
    return false;

  // TODO(szager): This conditional is weird and likely obsolete. Originally
  // added in commit eb0d49caaee2b275ff524d3945a74e8d9180eb7d.
  LocalFrameView* frame_view = view->GetFrameView();
  if (frame_view != frame_view->GetFrame().View())
    return false;

  return !!frame_view->GetFrame().GetDocument();
}

void PaintLayerScrollableArea::RegisterForAnimation() {
  if (HasBeenDisposed())
    return;
  if (LocalFrame* frame = GetLayoutBox()->GetFrame()) {
    if (LocalFrameView* frame_view = frame->View())
      frame_view->AddAnimatingScrollableArea(this);
  }
}

void PaintLayerScrollableArea::DeregisterForAnimation() {
  if (HasBeenDisposed())
    return;
  if (LocalFrame* frame = GetLayoutBox()->GetFrame()) {
    if (LocalFrameView* frame_view = frame->View())
      frame_view->RemoveAnimatingScrollableArea(this);
  }
}

bool PaintLayerScrollableArea::UserInputScrollable(
    ScrollbarOrientation orientation) const {
  if (orientation == kVerticalScrollbar &&
      GetLayoutBox()->GetDocument().IsVerticalScrollEnforced()) {
    return false;
  }

  if (GetLayoutBox()->IsIntrinsicallyScrollable(orientation))
    return true;

  if (IsA<LayoutView>(GetLayoutBox())) {
    Document& document = GetLayoutBox()->GetDocument();
    Element* fullscreen_element = Fullscreen::FullscreenElementFrom(document);
    if (fullscreen_element && fullscreen_element != document.documentElement())
      return false;

    mojom::blink::ScrollbarMode h_mode;
    mojom::blink::ScrollbarMode v_mode;
    To<LayoutView>(GetLayoutBox())->CalculateScrollbarModes(h_mode, v_mode);
    mojom::blink::ScrollbarMode mode =
        (orientation == kHorizontalScrollbar) ? h_mode : v_mode;
    return mode == mojom::blink::ScrollbarMode::kAuto ||
           mode == mojom::blink::ScrollbarMode::kAlwaysOn;
  }

  EOverflow overflow_style = (orientation == kHorizontalScrollbar)
                                 ? GetLayoutBox()->StyleRef().OverflowX()
                                 : GetLayoutBox()->StyleRef().OverflowY();
  return (overflow_style == EOverflow::kScroll ||
          overflow_style == EOverflow::kAuto ||
          overflow_style == EOverflow::kOverlay);
}

bool PaintLayerScrollableArea::ShouldPlaceVerticalScrollbarOnLeft() const {
  return GetLayoutBox()->ShouldPlaceBlockDirectionScrollbarOnLogicalLeft();
}

int PaintLayerScrollableArea::PageStep(ScrollbarOrientation orientation) const {
  // Paging scroll operations should take scroll-padding into account [1]. So we
  // use the snapport rect to calculate the page step instead of the visible
  // rect.
  // [1] https://drafts.csswg.org/css-scroll-snap/#scroll-padding
  gfx::Size snapport_size = VisibleScrollSnapportRect().PixelSnappedSize();
  int length = (orientation == kHorizontalScrollbar) ? snapport_size.width()
                                                     : snapport_size.height();
  int min_page_step = static_cast<float>(length) *
                      ScrollableArea::MinFractionToStepWhenPaging();
  int page_step = max(min_page_step, length - MaxOverlapBetweenPages());
  return max(page_step, 1);
}

bool PaintLayerScrollableArea::IsRootFrameLayoutViewport() const {
  LocalFrame* frame = GetLayoutBox()->GetFrame();
  if (!frame || !frame->View())
    return false;

  RootFrameViewport* root_frame_viewport =
      frame->View()->GetRootFrameViewport();
  if (!root_frame_viewport)
    return false;

  return &root_frame_viewport->LayoutViewport() == this;
}

LayoutBox* PaintLayerScrollableArea::GetLayoutBox() const {
  return layer_ ? layer_->GetLayoutBox() : nullptr;
}

PaintLayer* PaintLayerScrollableArea::Layer() const {
  return layer_;
}

LayoutUnit PaintLayerScrollableArea::ScrollWidth() const {
  return overflow_rect_.Width();
}

LayoutUnit PaintLayerScrollableArea::ScrollHeight() const {
  return overflow_rect_.Height();
}

void PaintLayerScrollableArea::UpdateScrollOrigin() {
  // This should do nothing prior to first layout; the if-clause will catch
  // that.
  if (overflow_rect_.IsEmpty())
    return;
  PhysicalRect scrollable_overflow = overflow_rect_;
  scrollable_overflow.Move(-PhysicalOffset(GetLayoutBox()->BorderLeft(),
                                           GetLayoutBox()->BorderTop()));
  gfx::Point new_origin = ToFlooredPoint(-scrollable_overflow.offset) +
                          GetLayoutBox()->OriginAdjustmentForScrollbars();
  if (new_origin != scroll_origin_) {
    scroll_origin_changed_ = true;
    // ScrollOrigin affects paint offsets of the scrolling contents.
    GetLayoutBox()->SetSubtreeShouldCheckForPaintInvalidation();
  }
  scroll_origin_ = new_origin;
}

void PaintLayerScrollableArea::UpdateScrollDimensions() {
  PhysicalRect new_overflow_rect = GetLayoutBox()->PhysicalLayoutOverflowRect();

  // The layout viewport can be larger than the document's layout overflow when
  // top controls are hidden.  Expand the overflow here to ensure that our
  // contents size >= visible size.
  new_overflow_rect.Unite(PhysicalRect(
      new_overflow_rect.offset, LayoutContentRect(kExcludeScrollbars).size));

  bool resized = overflow_rect_.size != new_overflow_rect.size;
  overflow_rect_ = new_overflow_rect;
  if (resized)
    ContentsResized();
  UpdateScrollOrigin();
}

void PaintLayerScrollableArea::UpdateScrollbarEnabledState(
    bool is_horizontal_scrollbar_frozen,
    bool is_vertical_scrollbar_frozen) {
  bool force_disable =
      GetPageScrollbarTheme().ShouldDisableInvisibleScrollbars() &&
      ScrollbarsHiddenIfOverlay();

  // Don't update the enabled state of a custom scrollbar if that scrollbar
  // is frozen. Otherwise re-running the style cascade with the change in
  // :disabled pseudo state matching for custom scrollbars can cause infinite
  // loops in layout.
  if (Scrollbar* horizontal_scrollbar = HorizontalScrollbar()) {
    if (!horizontal_scrollbar->IsCustomScrollbar() ||
        !is_horizontal_scrollbar_frozen) {
      horizontal_scrollbar->SetEnabled(HasHorizontalOverflow() &&
                                       !force_disable);
    }
  }

  if (Scrollbar* vertical_scrollbar = VerticalScrollbar()) {
    if (!vertical_scrollbar->IsCustomScrollbar() ||
        !is_vertical_scrollbar_frozen) {
      vertical_scrollbar->SetEnabled(HasVerticalOverflow() && !force_disable);
    }
  }
}

void PaintLayerScrollableArea::UpdateScrollbarProportions() {
  if (Scrollbar* horizontal_scrollbar = HorizontalScrollbar())
    horizontal_scrollbar->SetProportion(VisibleWidth(), ContentsSize().width());
  if (Scrollbar* vertical_scrollbar = VerticalScrollbar())
    vertical_scrollbar->SetProportion(VisibleHeight(), ContentsSize().height());
}

void PaintLayerScrollableArea::SetScrollOffsetUnconditionally(
    const ScrollOffset& offset,
    mojom::blink::ScrollType scroll_type) {
  CancelScrollAnimation();
  ScrollOffsetChanged(offset, scroll_type);
}

void PaintLayerScrollableArea::UpdateAfterLayout() {
  InvalidateAllStickyConstraints();
  InvalidateAllAnchorPositionedLayers();

  bool is_horizontal_scrollbar_frozen;
  bool is_vertical_scrollbar_frozen;
  if (in_overflow_relayout_ && !allow_second_overflow_relayout_) {
    is_horizontal_scrollbar_frozen = is_vertical_scrollbar_frozen = true;
  } else {
    is_horizontal_scrollbar_frozen = IsHorizontalScrollbarFrozen();
    is_vertical_scrollbar_frozen = IsVerticalScrollbarFrozen();
  }
  allow_second_overflow_relayout_ = false;

  if (NeedsScrollbarReconstruction()) {
    RemoveScrollbarsForReconstruction();
    // In case that DelayScrollOffsetClampScope prevented destruction of the
    // scrollbars.
    scrollbar_manager_.DestroyDetachedScrollbars();
  }

  UpdateScrollDimensions();

  bool has_resizer = GetLayoutBox()->CanResize();
  bool resizer_will_change = had_resizer_before_relayout_ != has_resizer;
  had_resizer_before_relayout_ = has_resizer;

  bool had_horizontal_scrollbar = HasHorizontalScrollbar();
  bool had_vertical_scrollbar = HasVerticalScrollbar();

  bool needs_horizontal_scrollbar;
  bool needs_vertical_scrollbar;
  ComputeScrollbarExistence(needs_horizontal_scrollbar,
                            needs_vertical_scrollbar);

  // Removing auto scrollbars is a heuristic and can be incorrect if the content
  // size depends on the scrollbar size (e.g., sized with percentages). Removing
  // scrollbars can require two additional layout passes so this is only done on
  // the first layout (!in_overflow_layout).
  if (!in_overflow_relayout_ && !is_horizontal_scrollbar_frozen &&
      !is_vertical_scrollbar_frozen &&
      TryRemovingAutoScrollbars(needs_horizontal_scrollbar,
                                needs_vertical_scrollbar)) {
    needs_horizontal_scrollbar = needs_vertical_scrollbar = false;
    allow_second_overflow_relayout_ = true;
  }

  bool horizontal_scrollbar_should_change =
      needs_horizontal_scrollbar != had_horizontal_scrollbar;
  bool vertical_scrollbar_should_change =
      needs_vertical_scrollbar != had_vertical_scrollbar;

  bool scrollbars_will_change =
      (horizontal_scrollbar_should_change && !is_horizontal_scrollbar_frozen) ||
      (vertical_scrollbar_should_change && !is_vertical_scrollbar_frozen);
  if (scrollbars_will_change) {
    SetHasHorizontalScrollbar(needs_horizontal_scrollbar);
    SetHasVerticalScrollbar(needs_vertical_scrollbar);

    // If we change scrollbars on the layout viewport, the visual viewport
    // needs to update paint properties to account for the correct
    // scrollbounds.
    if (LocalFrameView* frame_view = GetLayoutBox()->GetFrameView()) {
      VisualViewport& visual_viewport =
          GetLayoutBox()->GetFrame()->GetPage()->GetVisualViewport();
      if (this == frame_view->LayoutViewport() &&
          visual_viewport.IsActiveViewport()) {
        visual_viewport.SetNeedsPaintPropertyUpdate();
      }
    }

    UpdateScrollCornerStyle();

    Layer()->UpdateSelfPaintingLayer();

    // Force an update since we know the scrollbars have changed things.
    if (GetLayoutBox()->GetDocument().HasAnnotatedRegions())
      GetLayoutBox()->GetDocument().SetAnnotatedRegionsDirty(true);

    // Our proprietary overflow: overlay value doesn't trigger a layout.
    if (((horizontal_scrollbar_should_change &&
          GetLayoutBox()->StyleRef().OverflowX() != EOverflow::kOverlay) ||
         (vertical_scrollbar_should_change &&
          GetLayoutBox()->StyleRef().OverflowY() != EOverflow::kOverlay))) {
      if ((vertical_scrollbar_should_change &&
           GetLayoutBox()->IsHorizontalWritingMode()) ||
          (horizontal_scrollbar_should_change &&
           !GetLayoutBox()->IsHorizontalWritingMode())) {
        GetLayoutBox()->SetIntrinsicLogicalWidthsDirty();
      }
      if (IsManagedByLayoutNG(*GetLayoutBox())) {
        // If the box is managed by LayoutNG, don't go here. We don't want to
        // re-enter the NG layout algorithm for this box from here. Just update
        // the rectangles, in case scrollbars were added or removed. LayoutNG
        // has its own scrollbar change detection mechanism.
        UpdateScrollDimensions();
      } else {
        if (PreventRelayoutScope::RelayoutIsPrevented()) {
          // We're not doing re-layout right now, but we still want to
          // add the scrollbar to the logical width now, to facilitate parent
          // layout.
          GetLayoutBox()->UpdateLogicalWidth();
          PreventRelayoutScope::SetBoxNeedsLayout(
              *this, had_horizontal_scrollbar, had_vertical_scrollbar);
        } else {
          in_overflow_relayout_ = true;
          SubtreeLayoutScope layout_scope(*GetLayoutBox());
          layout_scope.SetNeedsLayout(
              GetLayoutBox(), layout_invalidation_reason::kScrollbarChanged);
          if (auto* block = DynamicTo<LayoutBlock>(GetLayoutBox())) {
            block->ScrollbarsChanged(horizontal_scrollbar_should_change,
                                     vertical_scrollbar_should_change);
            block->UpdateBlockLayout(true);
          } else {
            GetLayoutBox()->UpdateLayout();
          }
          in_overflow_relayout_ = false;
          scrollbar_manager_.DestroyDetachedScrollbars();
        }
        LayoutObject* parent = GetLayoutBox()->Parent();
        if (parent && parent->IsFlexibleBox()) {
          To<LayoutFlexibleBox>(parent)->ClearCachedMainSizeForChild(
              *GetLayoutBox());
        }
      }
    }
  } else if (!HasScrollbar() && resizer_will_change) {
    Layer()->DirtyStackingContextZOrderLists();
  }
  // The snap container data will be updated at the end of the layout update. If
  // the data changes, then this will try to re-snap.
  SetSnapContainerDataNeedsUpdate(true);
  {
    UpdateScrollbarEnabledState(is_horizontal_scrollbar_frozen,
                                is_vertical_scrollbar_frozen);

    UpdateScrollbarProportions();
  }

  ClampScrollOffsetAfterOverflowChange();

  if (!is_horizontal_scrollbar_frozen || !is_vertical_scrollbar_frozen)
    UpdateScrollableAreaSet();

  PositionOverflowControls();
}

void PaintLayerScrollableArea::ClampScrollOffsetAfterOverflowChange() {
  if (HasBeenDisposed())
    return;

  // If a vertical scrollbar was removed, the min/max scroll offsets may have
  // changed, so the scroll offsets needs to be clamped.  If the scroll offset
  // did not change, but the scroll origin *did* change, we still need to notify
  // the scrollbars to update their dimensions.

  if (DelayScrollOffsetClampScope::ClampingIsDelayed()) {
    DelayScrollOffsetClampScope::SetNeedsClamp(this);
    return;
  }

  const Document& document = GetLayoutBox()->GetDocument();
  if (document.IsPrintingOrPaintingPreview()) {
    // Scrollable elements may change size when generating layout for printing,
    // which may require them to change the scroll position in order to keep the
    // same content within view. In vertical-rl writing-mode, even the root
    // frame may be attempted scrolled, because a viewport size change may
    // affect scroll origin. Save all scroll offsets before clamping, so that
    // everything can be restored the way it was after printing.
    if (Node* node = EventTargetNode())
      document.GetFrame()->EnsureSaveScrollOffset(*node);
  }

  UpdateScrollDimensions();
  if (ScrollOriginChanged()) {
    SetScrollOffsetUnconditionally(ClampScrollOffset(GetScrollOffset()));
  } else {
    ScrollableArea::SetScrollOffset(GetScrollOffset(),
                                    mojom::blink::ScrollType::kClamping);
  }

  SetNeedsScrollOffsetClamp(false);
  ResetScrollOriginChanged();
  scrollbar_manager_.DestroyDetachedScrollbars();
}

void PaintLayerScrollableArea::DidChangeGlobalRootScroller() {
  // Being the global root scroller will affect clipping size due to browser
  // controls behavior so we need to update compositing based on updated clip
  // geometry.
  Layer()->SetNeedsCompositingInputsUpdate();
  GetLayoutBox()->SetNeedsPaintPropertyUpdate();

  // On Android, where the VisualViewport supplies scrollbars, we need to
  // remove the PLSA's scrollbars if we become the global root scroller.
  // In general, this would be problematic as that can cause layout but this
  // should only ever apply with overlay scrollbars.
  if (GetLayoutBox()->GetFrame()->GetSettings() &&
      GetLayoutBox()->GetFrame()->GetSettings()->GetViewportEnabled()) {
    bool needs_horizontal_scrollbar;
    bool needs_vertical_scrollbar;
    ComputeScrollbarExistence(needs_horizontal_scrollbar,
                              needs_vertical_scrollbar);
    SetHasHorizontalScrollbar(needs_horizontal_scrollbar);
    SetHasVerticalScrollbar(needs_vertical_scrollbar);
  }

  // Recalculate the snap container data since the scrolling behaviour for this
  // layout box changed (i.e. it either became the layout viewport or it
  // is no longer the layout viewport).
  SetSnapContainerDataNeedsUpdate(true);
}

bool PaintLayerScrollableArea::ShouldPerformScrollAnchoring() const {
  return scroll_anchor_.HasScroller() && GetLayoutBox() &&
         GetLayoutBox()->StyleRef().OverflowAnchor() !=
             EOverflowAnchor::kNone &&
         !GetLayoutBox()->GetDocument().FinishingOrIsPrinting();
}

bool PaintLayerScrollableArea::RestoreScrollAnchor(
    const SerializedAnchor& serialized_anchor) {
  return ShouldPerformScrollAnchoring() &&
         scroll_anchor_.RestoreAnchor(serialized_anchor);
}

gfx::QuadF PaintLayerScrollableArea::LocalToVisibleContentQuad(
    const gfx::QuadF& quad,
    const LayoutObject* local_object,
    MapCoordinatesFlags flags) const {
  LayoutBox* box = GetLayoutBox();
  if (!box)
    return quad;
  DCHECK(local_object);
  return local_object->LocalToAncestorQuad(quad, box, flags);
}

scoped_refptr<base::SingleThreadTaskRunner>
PaintLayerScrollableArea::GetTimerTaskRunner() const {
  return GetLayoutBox()->GetFrame()->GetTaskRunner(TaskType::kInternalDefault);
}

mojom::blink::ScrollBehavior PaintLayerScrollableArea::ScrollBehaviorStyle()
    const {
  return GetLayoutBox()->StyleRef().GetScrollBehavior();
}

mojom::blink::ColorScheme PaintLayerScrollableArea::UsedColorScheme() const {
  return GetLayoutBox()->StyleRef().UsedColorScheme();
}

bool PaintLayerScrollableArea::HasHorizontalOverflow() const {
  // TODO(szager): Make the algorithm for adding/subtracting overflow:auto
  // scrollbars memoryless (crbug.com/625300).  This client_width hack will
  // prevent the spurious horizontal scrollbar, but it can cause a converse
  // problem: it can leave a sliver of horizontal overflow hidden behind the
  // vertical scrollbar without creating a horizontal scrollbar.  This
  // converse problem seems to happen much less frequently in practice, so we
  // bias the logic towards preventing unwanted horizontal scrollbars, which
  // are more common and annoying.
  LayoutUnit client_width = LayoutContentRect(kIncludeScrollbars).Width() -
                            VerticalScrollbarWidth(kIgnoreOverlayScrollbarSize);
  if (NeedsRelayout() && !HadVerticalScrollbarBeforeRelayout())
    client_width += VerticalScrollbarWidth();
  LayoutUnit scroll_width(ScrollWidth());
  LayoutUnit box_x = GetLayoutBox()->Location().X();
  return SnapSizeToPixel(scroll_width, box_x) >
         SnapSizeToPixel(client_width, box_x);
}

bool PaintLayerScrollableArea::HasVerticalOverflow() const {
  LayoutUnit client_height =
      LayoutContentRect(kIncludeScrollbars).Height() -
      HorizontalScrollbarHeight(kIgnoreOverlayScrollbarSize);
  LayoutUnit scroll_height(ScrollHeight());
  LayoutUnit box_y = GetLayoutBox()->Location().Y();
  return SnapSizeToPixel(scroll_height, box_y) >
         SnapSizeToPixel(client_height, box_y);
}

// This function returns true if the given box requires overflow scrollbars (as
// opposed to the viewport scrollbars managed by VisualViewport).
static bool CanHaveOverflowScrollbars(const LayoutBox& box) {
  return box.GetDocument().ViewportDefiningElement() != box.GetNode();
}

void PaintLayerScrollableArea::UpdateAfterStyleChange(
    const ComputedStyle* old_style) {
  // Don't do this on first style recalc, before layout has ever happened.
  if (!overflow_rect_.size.IsZero())
    UpdateScrollableAreaSet();

  UpdateResizerStyle(old_style);

  // The scrollbar overlay color theme depends on styles such as the background
  // color and the used color scheme.
  RecalculateScrollbarOverlayColorTheme();

  if (NeedsScrollbarReconstruction()) {
    RemoveScrollbarsForReconstruction();
    return;
  }

  bool needs_horizontal_scrollbar;
  bool needs_vertical_scrollbar;
  ComputeScrollbarExistence(needs_horizontal_scrollbar,
                            needs_vertical_scrollbar, kOverflowIndependent);

  // Avoid some unnecessary computation if there were and will be no scrollbars.
  if (!HasScrollbar() && !needs_horizontal_scrollbar &&
      !needs_vertical_scrollbar)
    return;

  bool horizontal_scrollbar_changed =
      SetHasHorizontalScrollbar(needs_horizontal_scrollbar);
  bool vertical_scrollbar_changed =
      SetHasVerticalScrollbar(needs_vertical_scrollbar);

  auto* layout_block = DynamicTo<LayoutBlock>(GetLayoutBox());
  if (layout_block &&
      (horizontal_scrollbar_changed || vertical_scrollbar_changed)) {
    layout_block->ScrollbarsChanged(
        horizontal_scrollbar_changed, vertical_scrollbar_changed,
        LayoutBlock::ScrollbarChangeContext::kStyleChange);
  }

  if (HorizontalScrollbar())
    HorizontalScrollbar()->StyleChanged();
  if (VerticalScrollbar())
    VerticalScrollbar()->StyleChanged();

  UpdateScrollCornerStyle();

  if (!old_style || old_style->UsedColorScheme() != UsedColorScheme() ||
      old_style->ScrollbarWidth() !=
          GetLayoutBox()->StyleRef().ScrollbarWidth()) {
    SetScrollControlsNeedFullPaintInvalidation();
  }
}

void PaintLayerScrollableArea::UpdateAfterOverflowRecalc() {
  UpdateScrollDimensions();
  UpdateScrollbarProportions();
  UpdateScrollbarEnabledState();

  bool needs_horizontal_scrollbar;
  bool needs_vertical_scrollbar;
  ComputeScrollbarExistence(needs_horizontal_scrollbar,
                            needs_vertical_scrollbar);

  bool horizontal_scrollbar_should_change =
      needs_horizontal_scrollbar != HasHorizontalScrollbar();
  bool vertical_scrollbar_should_change =
      needs_vertical_scrollbar != HasVerticalScrollbar();

  if ((GetLayoutBox()->HasAutoHorizontalScrollbar() &&
       horizontal_scrollbar_should_change) ||
      (GetLayoutBox()->HasAutoVerticalScrollbar() &&
       vertical_scrollbar_should_change)) {
    GetLayoutBox()->SetNeedsLayoutAndFullPaintInvalidation(
        layout_invalidation_reason::kUnknown);
  }

  ClampScrollOffsetAfterOverflowChange();
  UpdateScrollableAreaSet();
}

gfx::Rect PaintLayerScrollableArea::RectForHorizontalScrollbar() const {
  if (!HasHorizontalScrollbar())
    return gfx::Rect();

  const gfx::Rect& scroll_corner = ScrollCornerRect();
  gfx::Size border_box_size = PixelSnappedBorderBoxSize();
  return gfx::Rect(
      HorizontalScrollbarStart(),
      border_box_size.height() - GetLayoutBox()->BorderBottom().ToInt() -
          HorizontalScrollbar()->ScrollbarThickness(),
      border_box_size.width() -
          (GetLayoutBox()->BorderLeft() + GetLayoutBox()->BorderRight())
              .ToInt() -
          scroll_corner.width(),
      HorizontalScrollbar()->ScrollbarThickness());
}

gfx::Rect PaintLayerScrollableArea::RectForVerticalScrollbar() const {
  if (!HasVerticalScrollbar())
    return gfx::Rect();

  const gfx::Rect& scroll_corner = ScrollCornerRect();
  return gfx::Rect(
      VerticalScrollbarStart(), GetLayoutBox()->BorderTop().ToInt(),
      VerticalScrollbar()->ScrollbarThickness(),
      PixelSnappedBorderBoxSize().height() -
          (GetLayoutBox()->BorderTop() + GetLayoutBox()->BorderBottom())
              .ToInt() -
          scroll_corner.height());
}

int PaintLayerScrollableArea::VerticalScrollbarStart() const {
  if (GetLayoutBox()->ShouldPlaceBlockDirectionScrollbarOnLogicalLeft())
    return GetLayoutBox()->BorderLeft().ToInt();
  return PixelSnappedBorderBoxSize().width() -
         GetLayoutBox()->BorderRight().ToInt() -
         VerticalScrollbar()->ScrollbarThickness();
}

int PaintLayerScrollableArea::HorizontalScrollbarStart() const {
  int x = GetLayoutBox()->BorderLeft().ToInt();
  if (GetLayoutBox()->ShouldPlaceBlockDirectionScrollbarOnLogicalLeft()) {
    x += HasVerticalScrollbar() ? VerticalScrollbar()->ScrollbarThickness()
                                : ResizerCornerRect(kResizerForPointer).width();
  }
  return x;
}

gfx::Vector2d PaintLayerScrollableArea::ScrollbarOffset(
    const Scrollbar& scrollbar) const {
  // TODO(szager): Factor out vertical offset calculation into other methods,
  // for symmetry with *ScrollbarStart methods for horizontal offset.
  if (&scrollbar == VerticalScrollbar()) {
    return gfx::Vector2d(VerticalScrollbarStart(),
                         GetLayoutBox()->BorderTop().ToInt());
  }

  if (&scrollbar == HorizontalScrollbar()) {
    return gfx::Vector2d(HorizontalScrollbarStart(),
                         GetLayoutBox()->BorderTop().ToInt() +
                             VisibleContentRect(kIncludeScrollbars).height() -
                             HorizontalScrollbar()->ScrollbarThickness());
  }

  NOTREACHED();
  return gfx::Vector2d();
}

static inline const LayoutObject& ScrollbarStyleSource(
    const LayoutBox& layout_box) {
  if (IsA<LayoutView>(layout_box)) {
    Document& doc = layout_box.GetDocument();

    // If scrollbar properties have been set on the root element, they will be
    // propagated to the viewport.
    Element* doc_element = doc.documentElement();
    if (doc_element && doc_element->GetLayoutObject() &&
        doc_element->GetLayoutObject()->StyleRef().ScrollbarWidth() !=
            EScrollbarWidth::kAuto)
      return *doc_element->GetLayoutObject();

    if (Settings* settings = doc.GetSettings()) {
      LocalFrame* frame = layout_box.GetFrame();
      DCHECK(frame);
      DCHECK(frame->GetPage());

      VisualViewport& viewport = frame->GetPage()->GetVisualViewport();
      if (!settings->GetAllowCustomScrollbarInMainFrame() &&
          frame->IsMainFrame() && viewport.IsActiveViewport()) {
        return layout_box;
      }
    }

    // Try the <body> element as a scrollbar source, but only if the body
    // can scroll.
    Element* body = doc.body();
    if (body && body->GetLayoutObject() && body->GetLayoutObject()->IsBox() &&
        body->GetLayoutObject()->StyleRef().HasCustomScrollbarStyle())
      return *body->GetLayoutObject();

    // If the <body> didn't have a custom style, then the root element might.
    if (doc_element && doc_element->GetLayoutObject() &&
        doc_element->GetLayoutObject()->StyleRef().HasCustomScrollbarStyle())
      return *doc_element->GetLayoutObject();
  } else if (!layout_box.GetNode() && layout_box.Parent()) {
    return *layout_box.Parent();
  }

  return layout_box;
}

int PaintLayerScrollableArea::HypotheticalScrollbarThickness(
    ScrollbarOrientation orientation,
    bool should_include_overlay_thickness) const {
  Scrollbar* scrollbar = orientation == kHorizontalScrollbar
                             ? HorizontalScrollbar()
                             : VerticalScrollbar();
  if (scrollbar)
    return scrollbar->ScrollbarThickness();

  const LayoutObject& style_source = ScrollbarStyleSource(*GetLayoutBox());
  if (style_source.StyleRef().HasCustomScrollbarStyle()) {
    return CustomScrollbar::HypotheticalScrollbarThickness(
        this, orientation, To<Element>(style_source.GetNode()));
  }

  ScrollbarTheme& theme = GetPageScrollbarTheme();
  if (theme.UsesOverlayScrollbars() && !should_include_overlay_thickness)
    return 0;
  return theme.ScrollbarThickness(ScaleFromDIP(),
                                  style_source.StyleRef().ScrollbarWidth());
}

bool PaintLayerScrollableArea::NeedsScrollbarReconstruction() const {
  if (!HasScrollbar())
    return false;

  const LayoutObject& style_source = ScrollbarStyleSource(*GetLayoutBox());
  bool needs_custom =
      style_source.IsBox() && style_source.StyleRef().HasCustomScrollbarStyle();

  Scrollbar* scrollbars[] = {HorizontalScrollbar(), VerticalScrollbar()};

  for (Scrollbar* scrollbar : scrollbars) {
    if (!scrollbar)
      continue;

    // We have a native scrollbar that should be custom, or vice versa.
    if (scrollbar->IsCustomScrollbar() != needs_custom)
      return true;

    // We have a scrollbar with a stale style source.
    if (scrollbar->StyleSource() &&
        scrollbar->StyleSource()->GetLayoutObject() != style_source) {
      return true;
    }

    if (needs_custom) {
      // Should use custom scrollbar and nothing should change.
      continue;
    }

    // Check if native scrollbar should change.
    Page* page = GetLayoutBox()->GetFrame()->LocalFrameRoot().GetPage();
    DCHECK(page);
    ScrollbarTheme* current_theme = &page->GetScrollbarTheme();

    if (current_theme != &scrollbar->GetTheme())
      return true;
  }
  return false;
}

void PaintLayerScrollableArea::ComputeScrollbarExistence(
    bool& needs_horizontal_scrollbar,
    bool& needs_vertical_scrollbar,
    ComputeScrollbarExistenceOption option) const {
  // Scrollbars may be hidden or provided by visual viewport or frame instead.
  DCHECK(GetLayoutBox()->GetFrame()->GetSettings());
  if (VisualViewportSuppliesScrollbars() ||
      !CanHaveOverflowScrollbars(*GetLayoutBox()) ||
      GetLayoutBox()->GetFrame()->GetSettings()->GetHideScrollbars() ||
      GetLayoutBox()->IsLayoutNGFieldset() ||
      GetLayoutBox()->IsLayoutNGFrameSet() ||
      GetLayoutBox()->StyleRef().ScrollbarWidth() == EScrollbarWidth::kNone) {
    needs_horizontal_scrollbar = false;
    needs_vertical_scrollbar = false;
    return;
  }

  mojom::blink::ScrollbarMode h_mode = mojom::blink::ScrollbarMode::kAuto;
  mojom::blink::ScrollbarMode v_mode = mojom::blink::ScrollbarMode::kAuto;

  // First, determine what behavior the scrollbars say they should have.
  {
    if (auto* layout_view = DynamicTo<LayoutView>(GetLayoutBox())) {
      // LayoutView is special as there's various quirks and settings that
      // style doesn't account for.
      layout_view->CalculateScrollbarModes(h_mode, v_mode);
    } else {
      auto overflow_x = GetLayoutBox()->StyleRef().OverflowX();
      if (overflow_x == EOverflow::kScroll) {
        h_mode = mojom::blink::ScrollbarMode::kAlwaysOn;
      } else if (overflow_x == EOverflow::kHidden ||
                 overflow_x == EOverflow::kVisible) {
        h_mode = mojom::blink::ScrollbarMode::kAlwaysOff;
      }

      auto overflow_y = GetLayoutBox()->StyleRef().OverflowY();
      if (overflow_y == EOverflow::kScroll) {
        v_mode = mojom::blink::ScrollbarMode::kAlwaysOn;
      } else if (overflow_y == EOverflow::kHidden ||
                 overflow_y == EOverflow::kVisible) {
        v_mode = mojom::blink::ScrollbarMode::kAlwaysOff;
      }
    }

    // Since overlay scrollbars (the fade-in/out kind, not overflow: overlay)
    // only appear when scrolling, we don't create them if there isn't overflow
    // to scroll. Thus, overlay scrollbars can't be "always on". i.e.
    // |overlay:scroll| behaves like |overlay:auto|.
    bool has_custom_scrollbar_style = ScrollbarStyleSource(*GetLayoutBox())
                                          .StyleRef()
                                          .HasCustomScrollbarStyle();
    bool will_be_overlay = GetPageScrollbarTheme().UsesOverlayScrollbars() &&
                           !has_custom_scrollbar_style;
    if (will_be_overlay) {
      if (h_mode == mojom::blink::ScrollbarMode::kAlwaysOn)
        h_mode = mojom::blink::ScrollbarMode::kAuto;
      if (v_mode == mojom::blink::ScrollbarMode::kAlwaysOn)
        v_mode = mojom::blink::ScrollbarMode::kAuto;
    }
  }

  // By default, don't make any changes.
  needs_horizontal_scrollbar = HasHorizontalScrollbar();
  needs_vertical_scrollbar = HasVerticalScrollbar();

  // If the behavior doesn't depend on overflow or any other information, we
  // can set it now.
  {
    if (h_mode == mojom::blink::ScrollbarMode::kAlwaysOn)
      needs_horizontal_scrollbar = true;
    else if (h_mode == mojom::blink::ScrollbarMode::kAlwaysOff)
      needs_horizontal_scrollbar = false;

    if (v_mode == mojom::blink::ScrollbarMode::kAlwaysOn)
      needs_vertical_scrollbar = true;
    else if (v_mode == mojom::blink::ScrollbarMode::kAlwaysOff)
      needs_vertical_scrollbar = false;
  }

  // If this is being performed before layout, we want to only update scrollbar
  // existence if its based on purely style based reasons.
  if (option == kOverflowIndependent)
    return;

  // If we have clean layout, we can make a decision on any scrollbars that
  // depend on overflow.
  {
    if (h_mode == mojom::blink::ScrollbarMode::kAuto) {
      // Don't add auto scrollbars if the box contents aren't visible.
      needs_horizontal_scrollbar =
          GetLayoutBox()->IsRooted() && HasHorizontalOverflow() &&
          VisibleContentRect(kIncludeScrollbars).height();
    }
    if (v_mode == mojom::blink::ScrollbarMode::kAuto) {
      needs_vertical_scrollbar = GetLayoutBox()->IsRooted() &&
                                 HasVerticalOverflow() &&
                                 VisibleContentRect(kIncludeScrollbars).width();
    }
  }
}

bool PaintLayerScrollableArea::TryRemovingAutoScrollbars(
    const bool& needs_horizontal_scrollbar,
    const bool& needs_vertical_scrollbar) {
  // If scrollbars are removed but the content size depends on the scrollbars,
  // additional layouts will be required to size the content. Therefore, only
  // remove auto scrollbars for the initial layout pass.
  DCHECK(!in_overflow_relayout_);

  if (!needs_horizontal_scrollbar && !needs_vertical_scrollbar)
    return false;

  if (auto* layout_view = DynamicTo<LayoutView>(GetLayoutBox())) {
    mojom::blink::ScrollbarMode h_mode;
    mojom::blink::ScrollbarMode v_mode;
    layout_view->CalculateScrollbarModes(h_mode, v_mode);
    if (h_mode != mojom::blink::ScrollbarMode::kAuto ||
        v_mode != mojom::blink::ScrollbarMode::kAuto)
      return false;

    gfx::Size visible_size_with_scrollbars =
        VisibleContentRect(kIncludeScrollbars).size();
    if (ScrollWidth() <= visible_size_with_scrollbars.width() &&
        ScrollHeight() <= visible_size_with_scrollbars.height()) {
      return true;
    }
  } else {
    if (!GetLayoutBox()->HasAutoVerticalScrollbar() ||
        !GetLayoutBox()->HasAutoHorizontalScrollbar())
      return false;

    PhysicalSize client_size_with_scrollbars =
        LayoutContentRect(kIncludeScrollbars).size;
    if (ScrollWidth() <= client_size_with_scrollbars.width &&
        ScrollHeight() <= client_size_with_scrollbars.height) {
      return true;
    }
  }

  return false;
}

void PaintLayerScrollableArea::RemoveScrollbarsForReconstruction() {
  if (!HasHorizontalScrollbar() && !HasVerticalScrollbar())
    return;
  if (HasHorizontalScrollbar()) {
    SetScrollbarNeedsPaintInvalidation(kHorizontalScrollbar);
    scrollbar_manager_.SetHasHorizontalScrollbar(false);
  }
  if (HasVerticalScrollbar()) {
    SetScrollbarNeedsPaintInvalidation(kVerticalScrollbar);
    scrollbar_manager_.SetHasVerticalScrollbar(false);
  }
  UpdateScrollCornerStyle();
  UpdateScrollOrigin();

  // Force an update since we know the scrollbars have changed things.
  if (GetLayoutBox()->GetDocument().HasAnnotatedRegions())
    GetLayoutBox()->GetDocument().SetAnnotatedRegionsDirty(true);
}

bool PaintLayerScrollableArea::SetHasHorizontalScrollbar(bool has_scrollbar) {
  if (IsHorizontalScrollbarFrozen())
    return false;

  if (has_scrollbar == HasHorizontalScrollbar())
    return false;

  // when scrollbar-width is "none", the scrollbar will not be displayed
  if (GetLayoutBox()->StyleRef().ScrollbarWidth() == EScrollbarWidth::kNone)
    return false;

  SetScrollbarNeedsPaintInvalidation(kHorizontalScrollbar);

  scrollbar_manager_.SetHasHorizontalScrollbar(has_scrollbar);

  UpdateScrollOrigin();

  // Destroying or creating one bar can cause our scrollbar corner to come and
  // go. We need to update the opposite scrollbar's style.
  if (HasHorizontalScrollbar())
    HorizontalScrollbar()->StyleChanged();
  if (HasVerticalScrollbar())
    VerticalScrollbar()->StyleChanged();

  SetScrollCornerNeedsPaintInvalidation();

  // Force an update since we know the scrollbars have changed things.
  if (GetLayoutBox()->GetDocument().HasAnnotatedRegions())
    GetLayoutBox()->GetDocument().SetAnnotatedRegionsDirty(true);
  return true;
}

bool PaintLayerScrollableArea::SetHasVerticalScrollbar(bool has_scrollbar) {
  if (IsVerticalScrollbarFrozen())
    return false;

  if (GetLayoutBox()->GetDocument().IsVerticalScrollEnforced()) {
    // When the policy is enforced the contents of document cannot be scrolled.
    // This would make rendering a scrollbar look strange
    // (https://crbug.com/898151).
    return false;
  }

  if (has_scrollbar == HasVerticalScrollbar())
    return false;

  // when scrollbar-width is "none", the scrollbar will not be displayed
  if (GetLayoutBox()->StyleRef().ScrollbarWidth() == EScrollbarWidth::kNone)
    return false;

  SetScrollbarNeedsPaintInvalidation(kVerticalScrollbar);

  scrollbar_manager_.SetHasVerticalScrollbar(has_scrollbar);

  UpdateScrollOrigin();

  // Destroying or creating one bar can cause our scrollbar corner to come and
  // go. We need to update the opposite scrollbar's style.
  if (HasHorizontalScrollbar())
    HorizontalScrollbar()->StyleChanged();
  if (HasVerticalScrollbar())
    VerticalScrollbar()->StyleChanged();

  SetScrollCornerNeedsPaintInvalidation();

  // Force an update since we know the scrollbars have changed things.
  if (GetLayoutBox()->GetDocument().HasAnnotatedRegions())
    GetLayoutBox()->GetDocument().SetAnnotatedRegionsDirty(true);
  return true;
}

int PaintLayerScrollableArea::VerticalScrollbarWidth(
    OverlayScrollbarClipBehavior overlay_scrollbar_clip_behavior) const {
  if (!HasVerticalScrollbar())
    return 0;
  if (overlay_scrollbar_clip_behavior == kIgnoreOverlayScrollbarSize &&
      GetLayoutBox()->StyleRef().OverflowY() == EOverflow::kOverlay) {
    return 0;
  }
  if ((overlay_scrollbar_clip_behavior == kIgnoreOverlayScrollbarSize ||
       !VerticalScrollbar()->ShouldParticipateInHitTesting()) &&
      VerticalScrollbar()->IsOverlayScrollbar()) {
    return 0;
  }
  return VerticalScrollbar()->ScrollbarThickness();
}

int PaintLayerScrollableArea::HorizontalScrollbarHeight(
    OverlayScrollbarClipBehavior overlay_scrollbar_clip_behavior) const {
  if (!HasHorizontalScrollbar())
    return 0;
  if (overlay_scrollbar_clip_behavior == kIgnoreOverlayScrollbarSize &&
      GetLayoutBox()->StyleRef().OverflowX() == EOverflow::kOverlay) {
    return 0;
  }
  if ((overlay_scrollbar_clip_behavior == kIgnoreOverlayScrollbarSize ||
       !HorizontalScrollbar()->ShouldParticipateInHitTesting()) &&
      HorizontalScrollbar()->IsOverlayScrollbar()) {
    return 0;
  }
  return HorizontalScrollbar()->ScrollbarThickness();
}

const cc::SnapContainerData* PaintLayerScrollableArea::GetSnapContainerData()
    const {
  return RareData() && RareData()->snap_container_data_
             ? &RareData()->snap_container_data_.value()
             : nullptr;
}

void PaintLayerScrollableArea::SetSnapContainerData(
    absl::optional<cc::SnapContainerData> data) {
  EnsureRareData().snap_container_data_ = data;
}

bool PaintLayerScrollableArea::SetTargetSnapAreaElementIds(
    cc::TargetSnapAreaElementIds snap_target_ids) {
  if (!RareData() || !RareData()->snap_container_data_)
    return false;
  if (RareData()->snap_container_data_.value().SetTargetSnapAreaElementIds(
          snap_target_ids)) {
    GetLayoutBox()->SetNeedsPaintPropertyUpdate();
    return true;
  }
  return false;
}

bool PaintLayerScrollableArea::SnapContainerDataNeedsUpdate() const {
  return RareData() ? RareData()->snap_container_data_needs_update_ : false;
}

void PaintLayerScrollableArea::SetSnapContainerDataNeedsUpdate(
    bool needs_update) {
  EnsureRareData().snap_container_data_needs_update_ = needs_update;
  if (!needs_update)
    return;
  GetLayoutBox()
      ->GetDocument()
      .GetSnapCoordinator()
      .SetAnySnapContainerDataNeedsUpdate(true);
}

bool PaintLayerScrollableArea::NeedsResnap() const {
  return RareData() ? RareData()->needs_resnap_ : false;
}

void PaintLayerScrollableArea::SetNeedsResnap(bool needs_resnap) {
  EnsureRareData().needs_resnap_ = needs_resnap;
}

absl::optional<gfx::PointF>
PaintLayerScrollableArea::GetSnapPositionAndSetTarget(
    const cc::SnapSelectionStrategy& strategy) {
  if (!RareData() || !RareData()->snap_container_data_)
    return absl::nullopt;

  cc::SnapContainerData& data = RareData()->snap_container_data_.value();
  if (!data.size())
    return absl::nullopt;

  // If the document has a focused element that is coincident with the snap
  // target, update the snap target to point to the focused element. This
  // ensures that we stay snapped to the focused element after a relayout.
  // TODO(crbug.com/1199911): If the focused element is not a snap target but
  // has an ancestor that is, perhaps the rule should be applied for the
  // ancestor element.
  CompositorElementId active_element_id = CompositorElementId();
  if (auto* active_element = GetDocument()->ActiveElement()) {
    active_element_id =
        CompositorElementIdFromDOMNodeId(DOMNodeIds::IdForNode(active_element));
  }

  cc::TargetSnapAreaElementIds snap_targets;
  gfx::PointF snap_position;
  absl::optional<gfx::PointF> snap_point;
  if (data.FindSnapPosition(strategy, &snap_position, &snap_targets,
                            active_element_id)) {
    snap_point = gfx::PointF(snap_position.x(), snap_position.y());
  }

  if (data.SetTargetSnapAreaElementIds(snap_targets))
    GetLayoutBox()->SetNeedsPaintPropertyUpdate();

  return snap_point;
}

bool PaintLayerScrollableArea::HasOverflowControls() const {
  // We do not need to check for ScrollCorner because it only exists iff there
  // are scrollbars, see: |ScrollCornerRect| and |UpdateScrollCornerStyle|.
  DCHECK(!ScrollCorner() || HasScrollbar());
  return HasScrollbar() || GetLayoutBox()->CanResize();
}

bool PaintLayerScrollableArea::HasOverlayOverflowControls() const {
  if (HasOverlayScrollbars())
    return true;
  if (!HasScrollbar() && GetLayoutBox()->CanResize())
    return true;
  if (GetLayoutBox()->StyleRef().OverflowX() == EOverflow::kOverlay ||
      GetLayoutBox()->StyleRef().OverflowY() == EOverflow::kOverlay)
    return true;
  return false;
}

bool PaintLayerScrollableArea::NeedsScrollCorner() const {
  // This is one of the differences between platform overlay scrollbars and
  // overflow:overlay scrollbars: the former don't need scroll corner, while
  // the latter do. HasOverlayScrollbars doesn't include overflow:overlay.
  return HasScrollbar() && !HasOverlayScrollbars();
}

bool PaintLayerScrollableArea::ShouldOverflowControlsPaintAsOverlay() const {
  if (HasOverlayOverflowControls())
    return true;

  // The global root scrollbars and corner also paint as overlay so that they
  // appear on top of all content within the viewport. This is important since
  // these scrollbar's transform state is
  // VisualViewport::TransformNodeForViewportScrollbars().
  return GetLayoutBox() && GetLayoutBox()->IsGlobalRootScroller();
}

void PaintLayerScrollableArea::PositionOverflowControls() {
  if (!HasOverflowControls())
    return;

  if (Scrollbar* vertical_scrollbar = VerticalScrollbar()) {
    vertical_scrollbar->SetFrameRect(RectForVerticalScrollbar());
    if (auto* custom_scrollbar = DynamicTo<CustomScrollbar>(vertical_scrollbar))
      custom_scrollbar->PositionScrollbarParts();
  }

  if (Scrollbar* horizontal_scrollbar = HorizontalScrollbar()) {
    horizontal_scrollbar->SetFrameRect(RectForHorizontalScrollbar());
    if (auto* custom_scrollbar =
            DynamicTo<CustomScrollbar>(horizontal_scrollbar))
      custom_scrollbar->PositionScrollbarParts();
  }

  if (scroll_corner_) {
    LayoutRect rect(ScrollCornerRect());
    scroll_corner_->SetFrameRect(rect);
    // TODO(crbug.com/1020913): This should be part of PaintPropertyTreeBuilder
    // when we support subpixel layout of overflow controls.
    scroll_corner_->GetMutableForPainting().FirstFragment().SetPaintOffset(
        PhysicalOffset(rect.Location()));
  }

  if (resizer_) {
    LayoutRect rect(ResizerCornerRect(kResizerForPointer));
    resizer_->SetFrameRect(rect);
    // TODO(crbug.com/1020913): This should be part of PaintPropertyTreeBuilder
    // when we support subpixel layout of overflow controls.
    resizer_->GetMutableForPainting().FirstFragment().SetPaintOffset(
        PhysicalOffset(rect.Location()));
  }
}

void PaintLayerScrollableArea::UpdateScrollCornerStyle() {
  if (!NeedsScrollCorner()) {
    if (scroll_corner_) {
      scroll_corner_->Destroy();
      scroll_corner_ = nullptr;
    }
    return;
  }
  const LayoutObject& style_source = ScrollbarStyleSource(*GetLayoutBox());
  scoped_refptr<ComputedStyle> corner =
      GetLayoutBox()->IsScrollContainer()
          ? style_source.GetUncachedPseudoElementStyle(
                StyleRequest(kPseudoIdScrollbarCorner, style_source.Style()))
          : scoped_refptr<ComputedStyle>(nullptr);
  if (corner) {
    if (!scroll_corner_) {
      scroll_corner_ = LayoutCustomScrollbarPart::CreateAnonymous(
          &GetLayoutBox()->GetDocument(), this);
    }
    scroll_corner_->SetStyle(std::move(corner));
  } else if (scroll_corner_) {
    scroll_corner_->Destroy();
    scroll_corner_ = nullptr;
  }
}

bool PaintLayerScrollableArea::HitTestOverflowControls(
    HitTestResult& result,
    const gfx::Point& local_point) {
  if (!HasOverflowControls())
    return false;

  gfx::Rect resize_control_rect;
  if (GetLayoutBox()->CanResize()) {
    resize_control_rect = ResizerCornerRect(kResizerForPointer);
    if (resize_control_rect.Contains(local_point))
      return true;
  }
  int resize_control_size = max(resize_control_rect.height(), 0);

  gfx::Rect visible_rect = VisibleContentRect(kIncludeScrollbars);

  if (HasVerticalScrollbar() &&
      VerticalScrollbar()->ShouldParticipateInHitTesting()) {
    LayoutRect v_bar_rect(VerticalScrollbarStart(),
                          GetLayoutBox()->BorderTop().ToInt(),
                          VerticalScrollbar()->ScrollbarThickness(),
                          visible_rect.height() -
                              (HasHorizontalScrollbar()
                                   ? HorizontalScrollbar()->ScrollbarThickness()
                                   : resize_control_size));
    if (v_bar_rect.Contains(LayoutPoint(local_point))) {
      result.SetScrollbar(VerticalScrollbar());
      return true;
    }
  }

  resize_control_size = max(resize_control_rect.width(), 0);
  if (HasHorizontalScrollbar() &&
      HorizontalScrollbar()->ShouldParticipateInHitTesting()) {
    // TODO(crbug.com/638981): Are the conversions to int intentional?
    int h_scrollbar_thickness = HorizontalScrollbar()->ScrollbarThickness();
    LayoutRect h_bar_rect(
        HorizontalScrollbarStart(),
        GetLayoutBox()->BorderTop().ToInt() + visible_rect.height() -
            h_scrollbar_thickness,
        visible_rect.width() - (HasVerticalScrollbar()
                                    ? VerticalScrollbar()->ScrollbarThickness()
                                    : resize_control_size),
        h_scrollbar_thickness);
    if (h_bar_rect.Contains(LayoutPoint(local_point))) {
      result.SetScrollbar(HorizontalScrollbar());
      return true;
    }
  }

  // FIXME: We should hit test the m_scrollCorner and pass it back through the
  // result.

  return false;
}

gfx::Rect PaintLayerScrollableArea::ResizerCornerRect(
    ResizerHitTestType resizer_hit_test_type) const {
  if (!GetLayoutBox()->CanResize())
    return gfx::Rect();
  gfx::Rect corner = CornerRect();

  if (resizer_hit_test_type == kResizerForTouch) {
    // We make the resizer virtually larger for touch hit testing. With the
    // expanding ratio k = ResizerControlExpandRatioForTouch, we first move
    // the resizer rect (of width w & height h), by (-w * (k-1), -h * (k-1)),
    // then expand the rect by new_w/h = w/h * k.
    corner.Offset(-corner.width() * (kResizerControlExpandRatioForTouch - 1),
                  -corner.height() * (kResizerControlExpandRatioForTouch - 1));
    corner.set_size(
        gfx::Size(corner.width() * kResizerControlExpandRatioForTouch,
                  corner.height() * kResizerControlExpandRatioForTouch));
  }

  return corner;
}

gfx::Rect PaintLayerScrollableArea::ScrollCornerAndResizerRect() const {
  gfx::Rect scroll_corner_and_resizer = ScrollCornerRect();
  if (scroll_corner_and_resizer.IsEmpty())
    return ResizerCornerRect(kResizerForPointer);
  return scroll_corner_and_resizer;
}

bool PaintLayerScrollableArea::IsAbsolutePointInResizeControl(
    const gfx::Point& absolute_point,
    ResizerHitTestType resizer_hit_test_type) const {
  if (GetLayoutBox()->StyleRef().Visibility() != EVisibility::kVisible ||
      !GetLayoutBox()->CanResize())
    return false;

  gfx::Point local_point = ToRoundedPoint(
      GetLayoutBox()->AbsoluteToLocalPoint(PhysicalOffset(absolute_point)));
  return ResizerCornerRect(resizer_hit_test_type).Contains(local_point);
}

bool PaintLayerScrollableArea::IsLocalPointInResizeControl(
    const gfx::Point& local_point,
    ResizerHitTestType resizer_hit_test_type) const {
  if (GetLayoutBox()->StyleRef().Visibility() != EVisibility::kVisible ||
      !GetLayoutBox()->CanResize())
    return false;

  return ResizerCornerRect(resizer_hit_test_type).Contains(local_point);
}

void PaintLayerScrollableArea::UpdateResizerStyle(
    const ComputedStyle* old_style) {
  // Change of resizer status affects HasOverlayOverflowControls(). Invalid
  // z-order lists to refresh overflow control painting order.
  bool had_resizer = old_style && old_style->HasResize();
  bool needs_resizer = GetLayoutBox()->CanResize();
  if (had_resizer != needs_resizer)
    layer_->DirtyStackingContextZOrderLists();

  if (!resizer_ && !needs_resizer)
    return;

  // Update custom resizer style.
  const LayoutObject& style_source = ScrollbarStyleSource(*GetLayoutBox());
  scoped_refptr<ComputedStyle> resizer =
      GetLayoutBox()->IsScrollContainer()
          ? style_source.GetUncachedPseudoElementStyle(
                StyleRequest(kPseudoIdResizer, style_source.Style()))
          : scoped_refptr<ComputedStyle>(nullptr);
  if (resizer) {
    if (!resizer_) {
      resizer_ = LayoutCustomScrollbarPart::CreateAnonymous(
          &GetLayoutBox()->GetDocument(), this);
    }
    resizer_->SetStyle(std::move(resizer));
  } else if (resizer_) {
    resizer_->Destroy();
    resizer_ = nullptr;
  }
}

void PaintLayerScrollableArea::AddStickyLayer(PaintLayer* layer) {
  UseCounter::Count(GetLayoutBox()->GetDocument(), WebFeature::kPositionSticky);
  EnsureRareData().sticky_layers_.insert(layer);
}

void PaintLayerScrollableArea::RemoveStickyLayer(PaintLayer* layer) {
  if (rare_data_)
    rare_data_->sticky_layers_.erase(layer);
}

void PaintLayerScrollableArea::InvalidateAllStickyConstraints() {
  // Don't clear StickyConstraints for each LayoutObject of each layer in
  // sticky_layers_ because sticky_layers_ may contain stale pointers.
  // LayoutBoxModelObject::UpdateStickyPositionConstraints() will check both
  // HasStickyLayer() of its containing scrollable area and its
  // StickyConstraints() to see if its sticky constraints need update.
  if (rare_data_)
    rare_data_->sticky_layers_.clear();
}

void PaintLayerScrollableArea::InvalidatePaintForStickyDescendants() {
  // If this is called during layout, sticky_layers_ may contain stale pointers.
  // Return because we'll InvalidateAllStickyConstraints(), and we'll
  // SetNeedsPaintPropertyUpdate() when updating sticky constraints.
  if (GetLayoutBox()->NeedsLayout())
    return;

  if (PaintLayerScrollableAreaRareData* d = RareData()) {
    for (PaintLayer* sticky_layer : d->sticky_layers_) {
      auto& object = sticky_layer->GetLayoutObject();
      object.SetNeedsPaintPropertyUpdate();
      DCHECK(object.StickyConstraints());
      object.StickyConstraints()->ComputeStickyOffset(ScrollPosition());
    }
  }
}

bool PaintLayerScrollableArea::AddAnchorPositionedLayer(PaintLayer* layer) {
  auto add_result = EnsureRareData().anchor_positioned_layers_.insert(layer);
  return add_result.is_new_entry;
}

void PaintLayerScrollableArea::InvalidateAllAnchorPositionedLayers() {
  if (rare_data_)
    rare_data_->anchor_positioned_layers_.clear();
}

void PaintLayerScrollableArea::InvalidatePaintForAnchorPositionedLayers() {
  // If this is called during layout, anchor_positioned_layers_ may contain
  // stale pointers. Return because we'll InvalidateAllAnchorPositionedLayers(),
  // and we'll SetNeedsPaintPropertyUpdate() when updating anchor positioned
  // layers.
  if (GetLayoutBox()->NeedsLayout())
    return;

  if (PaintLayerScrollableAreaRareData* d = RareData()) {
    for (PaintLayer* anchor_positioned_layer : d->anchor_positioned_layers_)
      anchor_positioned_layer->GetLayoutObject().SetNeedsPaintPropertyUpdate();
  }
}

gfx::Vector2d PaintLayerScrollableArea::OffsetFromResizeCorner(
    const gfx::Point& absolute_point) const {
  // Currently the resize corner is either the bottom right corner or the bottom
  // left corner.
  // FIXME: This assumes the location is 0, 0. Is this guaranteed to always be
  // the case?
  gfx::Size element_size = PixelSnappedBorderBoxSize();
  if (GetLayoutBox()->ShouldPlaceBlockDirectionScrollbarOnLogicalLeft())
    element_size.set_width(0);
  gfx::Point local_point = ToRoundedPoint(
      GetLayoutBox()->AbsoluteToLocalPoint(PhysicalOffset(absolute_point)));
  return gfx::Vector2d(local_point.x() - element_size.width(),
                       local_point.y() - element_size.height());
}

LayoutSize PaintLayerScrollableArea::MinimumSizeForResizing(float zoom_factor) {
  LayoutUnit min_width =
      MinimumValueForLength(GetLayoutBox()->StyleRef().MinWidth(),
                            GetLayoutBox()->ContainingBlock()->Size().Width());
  LayoutUnit min_height =
      MinimumValueForLength(GetLayoutBox()->StyleRef().MinHeight(),
                            GetLayoutBox()->ContainingBlock()->Size().Height());
  min_width = std::max(LayoutUnit(min_width / zoom_factor),
                       LayoutUnit(kDefaultMinimumWidthForResizing));
  min_height = std::max(LayoutUnit(min_height / zoom_factor),
                        LayoutUnit(kDefaultMinimumHeightForResizing));
  return LayoutSize(min_width, min_height);
}

void PaintLayerScrollableArea::Resize(const gfx::Point& pos,
                                      const LayoutSize& old_offset) {
  // FIXME: This should be possible on generated content but is not right now.
  if (!InResizeMode() || !GetLayoutBox()->CanResize() ||
      !GetLayoutBox()->GetNode())
    return;

  DCHECK(GetLayoutBox()->GetNode()->IsElementNode());
  auto* element = To<Element>(GetLayoutBox()->GetNode());

  Document& document = element->GetDocument();

  float zoom_factor = GetLayoutBox()->StyleRef().EffectiveZoom();

  gfx::Vector2d new_offset =
      OffsetFromResizeCorner(document.View()->ConvertFromRootFrame(pos));
  new_offset.set_x(new_offset.x() / zoom_factor);
  new_offset.set_y(new_offset.y() / zoom_factor);

  LayoutSize current_size = GetLayoutBox()->Size();
  current_size.Scale(1 / zoom_factor);

  LayoutSize adjusted_old_offset = old_offset * (1.f / zoom_factor);
  if (GetLayoutBox()->ShouldPlaceBlockDirectionScrollbarOnLogicalLeft()) {
    new_offset.set_x(-new_offset.x());
    adjusted_old_offset.SetWidth(-adjusted_old_offset.Width());
  }

  LayoutSize difference(
      (current_size + LayoutSize(new_offset) - adjusted_old_offset)
          .ExpandedTo(MinimumSizeForResizing(zoom_factor)) -
      current_size);

  bool is_box_sizing_border =
      GetLayoutBox()->StyleRef().BoxSizing() == EBoxSizing::kBorderBox;

  EResize resize = GetLayoutBox()->StyleRef().Resize(
      GetLayoutBox()->ContainingBlock()->StyleRef());
  if (resize != EResize::kVertical && difference.Width()) {
    LayoutUnit base_width =
        GetLayoutBox()->Size().Width() -
        (is_box_sizing_border ? LayoutUnit()
                              : GetLayoutBox()->BorderAndPaddingWidth());
    base_width = LayoutUnit(base_width / zoom_factor);
    element->SetInlineStyleProperty(CSSPropertyID::kWidth,
                                    RoundToInt(base_width + difference.Width()),
                                    CSSPrimitiveValue::UnitType::kPixels);
  }

  if (resize != EResize::kHorizontal && difference.Height()) {
    LayoutUnit base_height =
        GetLayoutBox()->Size().Height() -
        (is_box_sizing_border ? LayoutUnit()
                              : GetLayoutBox()->BorderAndPaddingHeight());
    base_height = LayoutUnit(base_height / zoom_factor);
    element->SetInlineStyleProperty(
        CSSPropertyID::kHeight, RoundToInt(base_height + difference.Height()),
        CSSPrimitiveValue::UnitType::kPixels);
  }

  document.UpdateStyleAndLayout(DocumentUpdateReason::kSizeChange);

  // FIXME: We should also autoscroll the window as necessary to
  // keep the point under the cursor in view.
}

PhysicalRect PaintLayerScrollableArea::ScrollIntoView(
    const PhysicalRect& absolute_rect,
    const mojom::blink::ScrollIntoViewParamsPtr& params) {
  // Ignore sticky position offsets for the purposes of scrolling elements into
  // view. See https://www.w3.org/TR/css-position-3/#stickypos-scroll for
  // details
  const MapCoordinatesFlags flag =
      (RuntimeEnabledFeatures::CSSPositionStickyStaticScrollPositionEnabled())
          ? kIgnoreStickyOffset
          : 0;

  PhysicalRect local_expose_rect =
      GetLayoutBox()->AbsoluteToLocalRect(absolute_rect, flag);
  PhysicalOffset border_origin_to_scroll_origin(-GetLayoutBox()->BorderLeft(),
                                                -GetLayoutBox()->BorderTop());
  // There might be scroll bar between border_origin and scroll_origin.
  gfx::Vector2d scroll_bar_adjustment =
      GetLayoutBox()->OriginAdjustmentForScrollbars();
  border_origin_to_scroll_origin.left -= scroll_bar_adjustment.x();
  border_origin_to_scroll_origin.top -= scroll_bar_adjustment.y();
  border_origin_to_scroll_origin +=
      PhysicalOffset::FromVector2dFFloor(GetScrollOffset());
  // Represent the rect in the container's scroll-origin coordinate.
  local_expose_rect.Move(border_origin_to_scroll_origin);
  PhysicalRect scroll_snapport_rect = VisibleScrollSnapportRect();

  ScrollOffset target_offset = ScrollAlignment::GetScrollOffsetToExpose(
      scroll_snapport_rect, local_expose_rect, *params->align_x.get(),
      *params->align_y.get(), GetScrollOffset());
  ScrollOffset new_scroll_offset(
      ClampScrollOffset(gfx::ToRoundedVector2d(target_offset)));

  ScrollOffset old_scroll_offset = GetScrollOffset();
  if (params->type == mojom::blink::ScrollType::kUser) {
    if (!UserInputScrollable(kHorizontalScrollbar))
      new_scroll_offset.set_x(old_scroll_offset.x());
    if (!UserInputScrollable(kVerticalScrollbar))
      new_scroll_offset.set_y(old_scroll_offset.y());
  }

  gfx::PointF end_point = ScrollOffsetToPosition(new_scroll_offset);
  std::unique_ptr<cc::SnapSelectionStrategy> strategy =
      cc::SnapSelectionStrategy::CreateForEndPosition(end_point, true, true);
  end_point = GetSnapPositionAndSetTarget(*strategy).value_or(end_point);
  new_scroll_offset = ScrollPositionToOffset(end_point);

  if (params->is_for_scroll_sequence) {
    DCHECK(params->type == mojom::blink::ScrollType::kProgrammatic ||
           params->type == mojom::blink::ScrollType::kUser);
    mojom::blink::ScrollBehavior behavior = DetermineScrollBehavior(
        params->behavior, GetLayoutBox()->StyleRef().GetScrollBehavior());
    GetSmoothScrollSequencer()->QueueAnimation(this, new_scroll_offset,
                                               behavior);
  } else {
    SetScrollOffset(new_scroll_offset, params->type,
                    mojom::blink::ScrollBehavior::kInstant);
  }

  ScrollOffset scroll_offset_difference = new_scroll_offset - old_scroll_offset;
  // The container hasn't performed the scroll yet if it's for scroll sequence.
  // To calculate the result from the scroll, we move the |local_expose_rect| to
  // the will-be-scrolled location.
  local_expose_rect.Move(
      -PhysicalOffset::FromVector2dFRound(scroll_offset_difference));

  // Represent the rects in the container's border-box coordinate.
  local_expose_rect.Move(-border_origin_to_scroll_origin);
  scroll_snapport_rect.Move(-border_origin_to_scroll_origin);
  PhysicalRect intersect =
      Intersection(scroll_snapport_rect, local_expose_rect);

  if (intersect.IsEmpty() && !scroll_snapport_rect.IsEmpty() &&
      !local_expose_rect.IsEmpty()) {
    return GetLayoutBox()->LocalToAbsoluteRect(local_expose_rect, flag);
  }
  intersect = GetLayoutBox()->LocalToAbsoluteRect(intersect, flag);

  return intersect;
}

void PaintLayerScrollableArea::UpdateScrollableAreaSet() {
  LocalFrame* frame = GetLayoutBox()->GetFrame();
  if (!frame)
    return;

  LocalFrameView* frame_view = frame->View();
  if (!frame_view)
    return;

  bool has_overflow =
      !GetLayoutBox()->Size().IsZero() &&
      ((HasHorizontalOverflow() && GetLayoutBox()->ScrollsOverflowX()) ||
       (HasVerticalOverflow() && GetLayoutBox()->ScrollsOverflowY()));

  bool overflows_in_block_direction = GetLayoutBox()->IsHorizontalWritingMode()
                                          ? HasVerticalOverflow()
                                          : HasHorizontalOverflow();

  if (overflows_in_block_direction) {
    DCHECK(CanHaveOverflowScrollbars(*GetLayoutBox()));
    frame_view->AddScrollAnchoringScrollableArea(this);
  } else {
    frame_view->RemoveScrollAnchoringScrollableArea(this);
  }

  bool is_visible_to_hit_test =
      GetLayoutBox()->StyleRef().VisibleToHitTesting();
  bool did_scroll_overflow = scrolls_overflow_;
  if (auto* layout_view = DynamicTo<LayoutView>(GetLayoutBox())) {
    mojom::blink::ScrollbarMode h_mode;
    mojom::blink::ScrollbarMode v_mode;
    layout_view->CalculateScrollbarModes(h_mode, v_mode);
    if (h_mode == mojom::blink::ScrollbarMode::kAlwaysOff &&
        v_mode == mojom::blink::ScrollbarMode::kAlwaysOff)
      has_overflow = false;
  }

  scrolls_overflow_ = has_overflow && is_visible_to_hit_test;
  if (did_scroll_overflow == ScrollsOverflow())
    return;

  // Change of scrolls_overflow may affect whether we create ScrollTranslation
  // which is referenced from ScrollDisplayItem. Invalidate scrollbars (but not
  // their parts) to repaint the display item.
  if (auto* scrollbar = HorizontalScrollbar())
    scrollbar->SetNeedsPaintInvalidation(kNoPart);
  if (auto* scrollbar = VerticalScrollbar())
    scrollbar->SetNeedsPaintInvalidation(kNoPart);

  if (RuntimeEnabledFeatures::ImplicitRootScrollerEnabled() &&
      scrolls_overflow_) {
    if (IsA<LayoutView>(GetLayoutBox())) {
      if (Element* owner = GetLayoutBox()->GetDocument().LocalOwner()) {
        owner->GetDocument().GetRootScrollerController().ConsiderForImplicit(
            *owner);
      }
    } else {
      // In some cases, the LayoutBox may not be associated with a Node (e.g.
      // <input> and <fieldset> can generate anonymous LayoutBoxes for their
      // scrollers). We don't care about those cases for root scroller so
      // simply avoid these. https://crbug.com/1125621.
      if (GetLayoutBox()->GetNode()) {
        GetLayoutBox()
            ->GetDocument()
            .GetRootScrollerController()
            .ConsiderForImplicit(*GetLayoutBox()->GetNode());
      }
    }
  }

  // The scroll and scroll offset properties depend on |scrollsOverflow| (see:
  // PaintPropertyTreeBuilder::updateScrollAndScrollTranslation).
  GetLayoutBox()->SetNeedsPaintPropertyUpdate();

  // Scroll hit test data depend on whether the box scrolls overflow.
  // They are painted in the background phase
  // (see: BoxPainter::PaintBoxDecorationBackground).
  GetLayoutBox()->SetBackgroundNeedsFullPaintInvalidation();

  if (scrolls_overflow_) {
    DCHECK(CanHaveOverflowScrollbars(*GetLayoutBox()));
    frame_view->AddUserScrollableArea(this);
  } else {
    frame_view->RemoveUserScrollableArea(this);
  }

  layer_->DidUpdateScrollsOverflow();
}

ScrollingCoordinator* PaintLayerScrollableArea::GetScrollingCoordinator()
    const {
  LocalFrame* frame = GetLayoutBox()->GetFrame();
  if (!frame)
    return nullptr;

  Page* page = frame->GetPage();
  if (!page)
    return nullptr;

  return page->GetScrollingCoordinator();
}

bool PaintLayerScrollableArea::ShouldScrollOnMainThread() const {
  if (HasBeenDisposed())
    return true;

  // TODO(crbug.com/985127, crbug.com/1015833): We should just use the main
  // thread scrolling reasons on the scroll node which should have all required
  // reasons. If it was not, we would have inconsistent results here and
  // ScrollNode::GetMainThreadScrollingReasons().
  if (LocalFrame* frame = GetLayoutBox()->GetFrame()) {
    if (frame->View()->GetMainThreadScrollingReasons())
      return true;
  }

  // Property tree state is not available until the PrePaint lifecycle stage.
  // PaintPropertyTreeBuilder needs to get the old status during PrePaint.
  DCHECK_GE(GetDocument()->Lifecycle().GetState(),
            DocumentLifecycle::kInPrePaint);
  const auto* properties = GetLayoutBox()->FirstFragment().PaintProperties();
  if (!properties || !properties->Scroll() ||
      properties->Scroll()->GetMainThreadScrollingReasons())
    return true;

  DCHECK(properties->ScrollTranslation());
  return !properties->ScrollTranslation()->HasDirectCompositingReasons();
}

static bool LayerNodeMayNeedCompositedScrolling(const PaintLayer* layer) {
  // Don't force composite scroll for select or text input elements.
  if (Node* node = layer->GetLayoutObject().GetNode()) {
    if (IsA<HTMLSelectElement>(node))
      return false;
    if (TextControlElement* text_control = EnclosingTextControl(node)) {
      if (IsA<HTMLInputElement>(text_control)) {
        return false;
      }
    }
  }
  return true;
}

bool PaintLayerScrollableArea::ComputeNeedsCompositedScrolling(
    bool force_prefer_compositing_to_lcd_text) {
  const auto* box = GetLayoutBox();
  non_composited_main_thread_scrolling_reasons_ = 0;
  auto new_background_paint_location =
      box->ComputeBackgroundPaintLocationIfComposited();
  bool needs_composited_scrolling = ComputeNeedsCompositedScrollingInternal(
      new_background_paint_location, force_prefer_compositing_to_lcd_text);
  if (!needs_composited_scrolling)
    new_background_paint_location = kBackgroundPaintInBorderBoxSpace;
  box->GetMutableForPainting().SetBackgroundPaintLocation(
      new_background_paint_location);

  return needs_composited_scrolling;
}

bool PaintLayerScrollableArea::ComputeNeedsCompositedScrollingInternal(
    BackgroundPaintLocation background_paint_location_if_composited,
    bool force_prefer_compositing_to_lcd_text) {
  DCHECK_EQ(background_paint_location_if_composited,
            GetLayoutBox()->ComputeBackgroundPaintLocationIfComposited());

  if (!Layer()->GetLayoutObject().GetFrameView()->IsVisible())
    return false;

  if (CompositingReasonFinder::RequiresCompositingForRootScroller(*layer_))
    return true;

  if (!layer_->ScrollsOverflow())
    return false;

  if (layer_->Size().IsEmpty())
    return false;

  const auto* box = GetLayoutBox();

  // Although trivial 3D transforms are not always a direct compositing reason
  // (see CompositingReasonFinder::RequiresCompositingFor3DTransform), we treat
  // them as one for composited scrolling. This is because of the amount of
  // content that depends on this optimization, and because of the long-term
  // desire to use composited scrolling whenever possible.
  if (box->HasTransformRelatedProperty() &&
      box->StyleRef().Has3DTransformOperation()) {
    return true;
  }

  if (!force_prefer_compositing_to_lcd_text &&
      (RuntimeEnabledFeatures::PreferNonCompositedScrollingEnabled() ||
       !LayerNodeMayNeedCompositedScrolling(layer_))) {
    return false;
  }

  bool needs_composited_scrolling = true;

  if (!force_prefer_compositing_to_lcd_text &&
      !box->GetDocument()
           .GetSettings()
           ->GetPreferCompositingToLCDTextEnabled()) {
    if (!box->TextIsKnownToBeOnOpaqueBackground()) {
      non_composited_main_thread_scrolling_reasons_ |=
          cc::MainThreadScrollingReason::kNotOpaqueForTextAndLCDText;
      needs_composited_scrolling = false;
    }
    if (!(background_paint_location_if_composited &
          kBackgroundPaintInContentsSpace) &&
        box->StyleRef().HasBackground()) {
      non_composited_main_thread_scrolling_reasons_ |= cc::
          MainThreadScrollingReason::kCantPaintScrollingBackgroundAndLCDText;
      needs_composited_scrolling = false;
    }
  }

  DCHECK(!(non_composited_main_thread_scrolling_reasons_ &
           ~cc::MainThreadScrollingReason::kNonCompositedReasons));

  if (!box->GetFrame()->Client()->GetWebFrame()) {
    // If there's no WebFrame, then there's no WebFrameWidget, and we can't do
    // threaded scrolling.  This currently only happens in a WebPagePopup.
    // (However, we still allow needs_composited_scrolling to be true in this
    // case, so that the scroller gets layerized.)
    non_composited_main_thread_scrolling_reasons_ |=
        cc::MainThreadScrollingReason::kPopupNoThreadedInput;
  }

  return needs_composited_scrolling;
}

bool PaintLayerScrollableArea::UsesCompositedScrolling() const {
  return GetLayoutBox()->UsesCompositedScrolling();
}

void PaintLayerScrollableArea::UpdateNeedsCompositedScrolling(
    bool force_prefer_compositing_to_lcd_text) {
  DCHECK_EQ(DocumentLifecycle::kInPrePaint,
            GetDocument()->Lifecycle().GetState());

  bool new_needs_composited_scrolling =
      ComputeNeedsCompositedScrolling(force_prefer_compositing_to_lcd_text);
  if (new_needs_composited_scrolling == needs_composited_scrolling_)
    return;

  needs_composited_scrolling_ = new_needs_composited_scrolling;
  GetLayoutBox()->SetShouldCheckForPaintInvalidation();
}

bool PaintLayerScrollableArea::VisualViewportSuppliesScrollbars() const {
  LocalFrame* frame = GetLayoutBox()->GetFrame();
  if (!frame || !frame->GetSettings())
    return false;

  // On desktop, we always use the layout viewport's scrollbars.
  if (!frame->GetSettings()->GetViewportEnabled())
    return false;

  const TopDocumentRootScrollerController& controller =
      GetLayoutBox()->GetDocument().GetPage()->GlobalRootScrollerController();
  return controller.RootScrollerArea() == this;
}

bool PaintLayerScrollableArea::ScheduleAnimation() {
  if (ChromeClient* client =
          GetLayoutBox()->GetFrameView()->GetChromeClient()) {
    client->ScheduleAnimation(GetLayoutBox()->GetFrameView());
    return true;
  }
  return false;
}

cc::AnimationHost* PaintLayerScrollableArea::GetCompositorAnimationHost()
    const {
  return layer_->GetLayoutObject().GetFrameView()->GetCompositorAnimationHost();
}

cc::AnimationTimeline*
PaintLayerScrollableArea::GetCompositorAnimationTimeline() const {
  return layer_->GetLayoutObject().GetFrameView()->GetScrollAnimationTimeline();
}

bool PaintLayerScrollableArea::HasTickmarks() const {
  if (RareData() && !RareData()->tickmarks_override_.IsEmpty())
    return true;
  return layer_->IsRootLayer() &&
         To<LayoutView>(GetLayoutBox())->HasTickmarks();
}

Vector<gfx::Rect> PaintLayerScrollableArea::GetTickmarks() const {
  if (RareData() && !RareData()->tickmarks_override_.IsEmpty())
    return RareData()->tickmarks_override_;
  if (layer_->IsRootLayer())
    return To<LayoutView>(GetLayoutBox())->GetTickmarks();
  return Vector<gfx::Rect>();
}

void PaintLayerScrollableArea::ScrollbarManager::SetHasHorizontalScrollbar(
    bool has_scrollbar) {
  if (has_scrollbar) {
    if (!h_bar_) {
      h_bar_ = CreateScrollbar(kHorizontalScrollbar);
      h_bar_is_attached_ = 1;
      if (!h_bar_->IsCustomScrollbar())
        ScrollableArea()->DidAddScrollbar(*h_bar_, kHorizontalScrollbar);
    } else {
      h_bar_is_attached_ = 1;
    }
  } else {
    h_bar_is_attached_ = 0;
    if (!DelayScrollOffsetClampScope::ClampingIsDelayed())
      DestroyScrollbar(kHorizontalScrollbar);
  }
}

void PaintLayerScrollableArea::ScrollbarManager::SetHasVerticalScrollbar(
    bool has_scrollbar) {
  if (has_scrollbar) {
    if (!v_bar_) {
      v_bar_ = CreateScrollbar(kVerticalScrollbar);
      v_bar_is_attached_ = 1;
      if (!v_bar_->IsCustomScrollbar())
        ScrollableArea()->DidAddScrollbar(*v_bar_, kVerticalScrollbar);
    } else {
      v_bar_is_attached_ = 1;
    }
  } else {
    v_bar_is_attached_ = 0;
    if (!DelayScrollOffsetClampScope::ClampingIsDelayed())
      DestroyScrollbar(kVerticalScrollbar);
  }
}

Scrollbar* PaintLayerScrollableArea::ScrollbarManager::CreateScrollbar(
    ScrollbarOrientation orientation) {
  DCHECK(orientation == kHorizontalScrollbar ? !h_bar_is_attached_
                                             : !v_bar_is_attached_);
  Scrollbar* scrollbar = nullptr;
  const LayoutObject& style_source =
      ScrollbarStyleSource(*ScrollableArea()->GetLayoutBox());
  if (style_source.StyleRef().HasCustomScrollbarStyle()) {
    DCHECK(style_source.GetNode() && style_source.GetNode()->IsElementNode());
    scrollbar = MakeGarbageCollected<CustomScrollbar>(
        ScrollableArea(), orientation, To<Element>(style_source.GetNode()));
  } else {
    Element* style_source_element = nullptr;
    style_source_element = DynamicTo<Element>(style_source.GetNode());
    scrollbar = MakeGarbageCollected<Scrollbar>(ScrollableArea(), orientation,
                                                style_source_element);
  }
  ScrollableArea()->GetLayoutBox()->GetDocument().View()->AddScrollbar(
      scrollbar);
  return scrollbar;
}

void PaintLayerScrollableArea::ScrollbarManager::DestroyScrollbar(
    ScrollbarOrientation orientation) {
  Member<Scrollbar>& scrollbar =
      orientation == kHorizontalScrollbar ? h_bar_ : v_bar_;
  DCHECK(orientation == kHorizontalScrollbar ? !h_bar_is_attached_
                                             : !v_bar_is_attached_);
  if (!scrollbar)
    return;

  ScrollableArea()->SetScrollbarNeedsPaintInvalidation(orientation);

  if (!scrollbar->IsCustomScrollbar())
    ScrollableArea()->WillRemoveScrollbar(*scrollbar, orientation);

  ScrollableArea()->GetLayoutBox()->GetDocument().View()->RemoveScrollbar(
      scrollbar);
  scrollbar->DisconnectFromScrollableArea();
  scrollbar = nullptr;
}

void PaintLayerScrollableArea::ScrollbarManager::DestroyDetachedScrollbars() {
  DCHECK(!h_bar_is_attached_ || h_bar_);
  DCHECK(!v_bar_is_attached_ || v_bar_);
  if (h_bar_ && !h_bar_is_attached_)
    DestroyScrollbar(kHorizontalScrollbar);
  if (v_bar_ && !v_bar_is_attached_)
    DestroyScrollbar(kVerticalScrollbar);
}

void PaintLayerScrollableArea::ScrollbarManager::Dispose() {
  h_bar_is_attached_ = v_bar_is_attached_ = 0;
  DestroyScrollbar(kHorizontalScrollbar);
  DestroyScrollbar(kVerticalScrollbar);
}

void PaintLayerScrollableArea::ScrollbarManager::Trace(
    blink::Visitor* visitor) const {
  visitor->Trace(scrollable_area_);
  visitor->Trace(h_bar_);
  visitor->Trace(v_bar_);
}

int PaintLayerScrollableArea::PreventRelayoutScope::count_ = 0;
SubtreeLayoutScope*
    PaintLayerScrollableArea::PreventRelayoutScope::layout_scope_ = nullptr;
bool PaintLayerScrollableArea::PreventRelayoutScope::relayout_needed_ = false;

PaintLayerScrollableArea::PreventRelayoutScope::PreventRelayoutScope(
    SubtreeLayoutScope& layout_scope) {
  if (!count_) {
    DCHECK(!layout_scope_);
    DCHECK(NeedsRelayoutList().IsEmpty());
    layout_scope_ = &layout_scope;
  }
  count_++;
}

PaintLayerScrollableArea::PreventRelayoutScope::~PreventRelayoutScope() {
  if (--count_ == 0) {
    if (relayout_needed_) {
      for (auto scrollable_area : NeedsRelayoutList()) {
        DCHECK(scrollable_area->NeedsRelayout());
        LayoutBox* box = scrollable_area->GetLayoutBox();
        layout_scope_->SetNeedsLayout(
            box, layout_invalidation_reason::kScrollbarChanged);
        if (auto* layout_block = DynamicTo<LayoutBlock>(box)) {
          bool horizontal_scrollbar_changed =
              scrollable_area->HasHorizontalScrollbar() !=
              scrollable_area->HadHorizontalScrollbarBeforeRelayout();
          bool vertical_scrollbar_changed =
              scrollable_area->HasVerticalScrollbar() !=
              scrollable_area->HadVerticalScrollbarBeforeRelayout();
          if (horizontal_scrollbar_changed || vertical_scrollbar_changed) {
            layout_block->ScrollbarsChanged(horizontal_scrollbar_changed,
                                            vertical_scrollbar_changed);
          }
        }
        scrollable_area->SetNeedsRelayout(false);
      }

      NeedsRelayoutList().clear();
    }
    layout_scope_ = nullptr;
  }
}

void PaintLayerScrollableArea::PreventRelayoutScope::SetBoxNeedsLayout(
    PaintLayerScrollableArea& scrollable_area,
    bool had_horizontal_scrollbar,
    bool had_vertical_scrollbar) {
  DCHECK(count_);
  DCHECK(layout_scope_);
  if (scrollable_area.NeedsRelayout())
    return;
  scrollable_area.SetNeedsRelayout(true);
  scrollable_area.SetHadHorizontalScrollbarBeforeRelayout(
      had_horizontal_scrollbar);
  scrollable_area.SetHadVerticalScrollbarBeforeRelayout(had_vertical_scrollbar);

  relayout_needed_ = true;
  NeedsRelayoutList().push_back(&scrollable_area);
}

void PaintLayerScrollableArea::PreventRelayoutScope::ResetRelayoutNeeded() {
  DCHECK_EQ(count_, 0);
  DCHECK(NeedsRelayoutList().IsEmpty());
  relayout_needed_ = false;
}

HeapVector<Member<PaintLayerScrollableArea>>&
PaintLayerScrollableArea::PreventRelayoutScope::NeedsRelayoutList() {
  DEFINE_STATIC_LOCAL(
      Persistent<HeapVector<Member<PaintLayerScrollableArea>>>,
      needs_relayout_list,
      (MakeGarbageCollected<HeapVector<Member<PaintLayerScrollableArea>>>()));
  return *needs_relayout_list;
}

int PaintLayerScrollableArea::FreezeScrollbarsScope::count_ = 0;

PaintLayerScrollableArea::FreezeScrollbarsRootScope::FreezeScrollbarsRootScope(
    const LayoutBox& box,
    bool freeze_horizontal,
    bool freeze_vertical)
    : scrollable_area_(box.GetScrollableArea()) {
  if (scrollable_area_ && !FreezeScrollbarsScope::ScrollbarsAreFrozen() &&
      (freeze_horizontal || freeze_vertical)) {
    scrollable_area_->EstablishScrollbarRoot(freeze_horizontal,
                                             freeze_vertical);
    freezer_.emplace();
  }
}

PaintLayerScrollableArea::FreezeScrollbarsRootScope::
    ~FreezeScrollbarsRootScope() {
  if (scrollable_area_)
    scrollable_area_->ClearScrollbarRoot();
}

int PaintLayerScrollableArea::DelayScrollOffsetClampScope::count_ = 0;

PaintLayerScrollableArea::DelayScrollOffsetClampScope::
    DelayScrollOffsetClampScope() {
  DCHECK(count_ > 0 || NeedsClampList().IsEmpty());
  count_++;
}

PaintLayerScrollableArea::DelayScrollOffsetClampScope::
    ~DelayScrollOffsetClampScope() {
  if (--count_ == 0)
    DelayScrollOffsetClampScope::ClampScrollableAreas();
}

void PaintLayerScrollableArea::DelayScrollOffsetClampScope::SetNeedsClamp(
    PaintLayerScrollableArea* scrollable_area) {
  if (!scrollable_area->NeedsScrollOffsetClamp()) {
    scrollable_area->SetNeedsScrollOffsetClamp(true);
    NeedsClampList().push_back(scrollable_area);
  }
}

void PaintLayerScrollableArea::DelayScrollOffsetClampScope::
    ClampScrollableAreas() {
  for (auto& scrollable_area : NeedsClampList())
    scrollable_area->ClampScrollOffsetAfterOverflowChange();
  NeedsClampList().clear();
}

HeapVector<Member<PaintLayerScrollableArea>>&
PaintLayerScrollableArea::DelayScrollOffsetClampScope::NeedsClampList() {
  DEFINE_STATIC_LOCAL(
      Persistent<HeapVector<Member<PaintLayerScrollableArea>>>,
      needs_clamp_list,
      (MakeGarbageCollected<HeapVector<Member<PaintLayerScrollableArea>>>()));
  return *needs_clamp_list;
}

ScrollbarTheme& PaintLayerScrollableArea::GetPageScrollbarTheme() const {
  // If PaintLayer is destructed before PaintLayerScrollable area, we can not
  // get the page scrollbar theme setting.
  DCHECK(!HasBeenDisposed());

  Page* page = GetLayoutBox()->GetFrame()->GetPage();
  DCHECK(page);

  return page->GetScrollbarTheme();
}

void PaintLayerScrollableArea::DidAddScrollbar(
    Scrollbar& scrollbar,
    ScrollbarOrientation orientation) {
  if (HasOverlayOverflowControls() ||
      layer_->NeedsReorderOverlayOverflowControls()) {
    // Z-order of existing or new recordered overflow controls is updated along
    // with the z-order lists.
    layer_->DirtyStackingContextZOrderLists();
  }
  ScrollableArea::DidAddScrollbar(scrollbar, orientation);
}

void PaintLayerScrollableArea::WillRemoveScrollbar(
    Scrollbar& scrollbar,
    ScrollbarOrientation orientation) {
  if (layer_->NeedsReorderOverlayOverflowControls()) {
    // Z-order of recordered overflow controls is updated along with the z-order
    // lists.
    layer_->DirtyStackingContextZOrderLists();
  }

  if (!scrollbar.IsCustomScrollbar()) {
    ObjectPaintInvalidator(*GetLayoutBox())
        .SlowSetPaintingLayerNeedsRepaintAndInvalidateDisplayItemClient(
            scrollbar, PaintInvalidationReason::kScrollControl);
  }

  ScrollableArea::WillRemoveScrollbar(scrollbar, orientation);
}

// Returns true if the scroll control is invalidated.
static bool ScrollControlNeedsPaintInvalidation(
    const gfx::Rect& new_visual_rect,
    const gfx::Rect& previous_visual_rect,
    bool needs_paint_invalidation) {
  if (new_visual_rect != previous_visual_rect)
    return true;
  if (previous_visual_rect.IsEmpty()) {
    DCHECK(new_visual_rect.IsEmpty());
    // Do not issue an empty invalidation.
    return false;
  }

  return needs_paint_invalidation;
}

bool PaintLayerScrollableArea::ShouldDirectlyCompositeScrollbar(
    const Scrollbar& scrollbar) const {
  // Don't composite non-scrollable scrollbars.
  if (!scrollbar.Maximum())
    return false;
  if (scrollbar.IsCustomScrollbar())
    return false;

  return NeedsCompositedScrolling();
}

void PaintLayerScrollableArea::EstablishScrollbarRoot(bool freeze_horizontal,
                                                      bool freeze_vertical) {
  DCHECK(!FreezeScrollbarsScope::ScrollbarsAreFrozen());
  is_scrollbar_freeze_root_ = true;
  is_horizontal_scrollbar_frozen_ = freeze_horizontal;
  is_vertical_scrollbar_frozen_ = freeze_vertical;
}

void PaintLayerScrollableArea::ClearScrollbarRoot() {
  is_scrollbar_freeze_root_ = false;
  is_horizontal_scrollbar_frozen_ = false;
  is_vertical_scrollbar_frozen_ = false;
}

void PaintLayerScrollableArea::InvalidatePaintOfScrollbarIfNeeded(
    const PaintInvalidatorContext& context,
    bool needs_paint_invalidation,
    Scrollbar* scrollbar,
    bool& previously_was_overlay,
    bool& previously_was_directly_composited,
    gfx::Rect& visual_rect) {
  bool is_overlay = scrollbar && scrollbar->IsOverlayScrollbar();

  gfx::Rect new_visual_rect;
  if (scrollbar) {
    new_visual_rect = scrollbar->FrameRect();
    // TODO(crbug.com/1020913): We should not round paint_offset but should
    // consider subpixel accumulation when painting scrollbars.
    new_visual_rect.Offset(
        ToRoundedVector2d(context.fragment_data->PaintOffset()));
  }

  // Invalidate the box's display item client if the box's padding box size is
  // affected by change of the non-overlay scrollbar width. We detect change of
  // visual rect size instead of change of scrollbar width, which may have some
  // false-positives (e.g. the scrollbar changed length but not width) but won't
  // invalidate more than expected because in the false-positive case the box
  // must have changed size and have been invalidated.
  gfx::Size new_scrollbar_used_space_in_box;
  if (!is_overlay)
    new_scrollbar_used_space_in_box = new_visual_rect.size();
  gfx::Size previous_scrollbar_used_space_in_box;
  if (!previously_was_overlay)
    previous_scrollbar_used_space_in_box = visual_rect.size();

  // The IsEmpty() check avoids invalidaiton in cases when the visual rect
  // changes from (0,0 0x0) to (0,0 0x100).
  if (!(new_scrollbar_used_space_in_box.IsEmpty() &&
        previous_scrollbar_used_space_in_box.IsEmpty()) &&
      new_scrollbar_used_space_in_box != previous_scrollbar_used_space_in_box) {
    context.painting_layer->SetNeedsRepaint();
    const auto& box = *GetLayoutBox();
    ObjectPaintInvalidator(box).InvalidateDisplayItemClient(
        box, PaintInvalidationReason::kGeometry);
  }

  previously_was_overlay = is_overlay;

  if (scrollbar) {
    bool directly_composited = ShouldDirectlyCompositeScrollbar(*scrollbar);
    if (directly_composited != previously_was_directly_composited) {
      needs_paint_invalidation = true;
      previously_was_directly_composited = directly_composited;
    } else if (directly_composited) {
      // Don't invalidate directly composited scrollbar if the change is only
      // inside of the scrollbar. ScrollbarDisplayItem will handle such change.
      needs_paint_invalidation = false;
    }
  }

  if (scrollbar &&
      ScrollControlNeedsPaintInvalidation(new_visual_rect, visual_rect,
                                          needs_paint_invalidation)) {
    context.painting_layer->SetNeedsRepaint();
    scrollbar->Invalidate(PaintInvalidationReason::kScrollControl);
    if (auto* custom_scrollbar = DynamicTo<CustomScrollbar>(scrollbar))
      custom_scrollbar->InvalidateDisplayItemClientsOfScrollbarParts();
  }

  visual_rect = new_visual_rect;
}

void PaintLayerScrollableArea::InvalidatePaintOfScrollControlsIfNeeded(
    const PaintInvalidatorContext& context) {
  if (context.subtree_flags & PaintInvalidatorContext::kSubtreeFullInvalidation)
    SetScrollControlsNeedFullPaintInvalidation();

  InvalidatePaintOfScrollbarIfNeeded(
      context, HorizontalScrollbarNeedsPaintInvalidation(),
      HorizontalScrollbar(), horizontal_scrollbar_previously_was_overlay_,
      horizontal_scrollbar_previously_was_directly_composited_,
      horizontal_scrollbar_visual_rect_);
  InvalidatePaintOfScrollbarIfNeeded(
      context, VerticalScrollbarNeedsPaintInvalidation(), VerticalScrollbar(),
      vertical_scrollbar_previously_was_overlay_,
      vertical_scrollbar_previously_was_directly_composited_,
      vertical_scrollbar_visual_rect_);

  gfx::Rect new_scroll_corner_and_resizer_visual_rect =
      ScrollCornerAndResizerRect();
  // TODO(crbug.com/1020913): We should not round paint_offset but should
  // consider subpixel accumulation when painting scrollbars.
  new_scroll_corner_and_resizer_visual_rect.Offset(
      ToRoundedVector2d(context.fragment_data->PaintOffset()));
  if (ScrollControlNeedsPaintInvalidation(
          new_scroll_corner_and_resizer_visual_rect,
          scroll_corner_and_resizer_visual_rect_,
          ScrollCornerNeedsPaintInvalidation())) {
    scroll_corner_and_resizer_visual_rect_ =
        new_scroll_corner_and_resizer_visual_rect;
    if (LayoutCustomScrollbarPart* scroll_corner = ScrollCorner()) {
      DCHECK(!scroll_corner->PaintingLayer());
      ObjectPaintInvalidator(*scroll_corner)
          .InvalidateDisplayItemClient(*scroll_corner,
                                       PaintInvalidationReason::kScrollControl);
    }
    if (LayoutCustomScrollbarPart* resizer = Resizer()) {
      DCHECK(!resizer->PaintingLayer());
      ObjectPaintInvalidator(*resizer).InvalidateDisplayItemClient(
          *resizer, PaintInvalidationReason::kScrollControl);
    }

    context.painting_layer->SetNeedsRepaint();
    ObjectPaintInvalidator(*GetLayoutBox())
        .InvalidateDisplayItemClient(GetScrollCornerDisplayItemClient(),
                                     PaintInvalidationReason::kGeometry);
  }

  ClearNeedsPaintInvalidationForScrollControls();
}

void PaintLayerScrollableArea::ScrollControlWasSetNeedsPaintInvalidation() {
  GetLayoutBox()->SetShouldCheckForPaintInvalidation();
}

void PaintLayerScrollableArea::DidScrollWithScrollbar(
    ScrollbarPart part,
    ScrollbarOrientation orientation,
    WebInputEvent::Type type) {
  WebFeature scrollbar_use_uma;
  switch (part) {
    case kBackButtonEndPart:
    case kForwardButtonStartPart:
      UseCounter::Count(
          GetLayoutBox()->GetDocument(),
          WebFeature::kScrollbarUseScrollbarButtonReversedDirection);
      U_FALLTHROUGH;
    case kBackButtonStartPart:
    case kForwardButtonEndPart:
      scrollbar_use_uma =
          (orientation == kVerticalScrollbar
               ? WebFeature::kScrollbarUseVerticalScrollbarButton
               : WebFeature::kScrollbarUseHorizontalScrollbarButton);
      break;
    case kThumbPart:
      if (orientation == kVerticalScrollbar) {
        scrollbar_use_uma =
            (WebInputEvent::IsMouseEventType(type)
                 ? WebFeature::kVerticalScrollbarThumbScrollingWithMouse
                 : WebFeature::kVerticalScrollbarThumbScrollingWithTouch);
      } else {
        scrollbar_use_uma =
            (WebInputEvent::IsMouseEventType(type)
                 ? WebFeature::kHorizontalScrollbarThumbScrollingWithMouse
                 : WebFeature::kHorizontalScrollbarThumbScrollingWithTouch);
      }
      break;
    case kBackTrackPart:
    case kForwardTrackPart:
      scrollbar_use_uma =
          (orientation == kVerticalScrollbar
               ? WebFeature::kScrollbarUseVerticalScrollbarTrack
               : WebFeature::kScrollbarUseHorizontalScrollbarTrack);
      break;
    default:
      return;
  }

  Document& document = GetLayoutBox()->GetDocument();

  UseCounter::Count(document, scrollbar_use_uma);
}

CompositorElementId PaintLayerScrollableArea::GetScrollElementId() const {
  return CompositorElementIdFromUniqueObjectId(
      GetLayoutBox()->UniqueId(), CompositorElementIdNamespace::kScroll);
}

gfx::Size PaintLayerScrollableArea::PixelSnappedBorderBoxSize() const {
  // TODO(crbug.com/1020913): We use this method during
  // PositionOverflowControls() even before the paint offset is updated.
  // This can be fixed only after we support subpixels in overflow control
  // geometry. For now we ensure correct pixel snapping of overflow controls by
  // calling PositionOverflowControls() again when paint offset is updated.
  return GetLayoutBox()->PixelSnappedBorderBoxSize(
      GetLayoutBox()->FirstFragment().PaintOffset());
}

gfx::Rect PaintLayerScrollableArea::ScrollingBackgroundVisualRect(
    const PhysicalOffset& paint_offset) const {
  const auto* box = GetLayoutBox();
  auto clip_rect = box->OverflowClipRect(paint_offset);
  auto overflow_clip_rect = ToPixelSnappedRect(clip_rect);
  auto scroll_size = PixelSnappedContentsSize(clip_rect.offset);
  // Ensure scrolling contents are at least as large as the scroll clip
  scroll_size.SetToMax(overflow_clip_rect.size());
  gfx::Rect result(overflow_clip_rect.origin(), scroll_size);

  // The HTML element of a document is special, in that it can have a transform,
  // but the bounds of the painted area of the element still extends beyond
  // its actual size to encompass the entire viewport canvas. This is
  // accomplished in ViewPainter by starting with a rect in viewport canvas
  // space that is equal to the size of the viewport canvas, then mapping it
  // into the local border box space of the HTML element, and painting a rect
  // equal to the bounding box of the result. We need to add in that mapped rect
  // in such cases.
  const Document& document = box->GetDocument();
  if (IsA<LayoutView>(box) &&
      (document.IsXMLDocument() || document.IsHTMLDocument())) {
    if (const auto* document_element = document.documentElement()) {
      if (const auto* document_element_object =
              document_element->GetLayoutObject()) {
        const auto& document_element_state =
            document_element_object->FirstFragment().LocalBorderBoxProperties();
        const auto& view_contents_state =
            box->FirstFragment().ContentsProperties();
        gfx::Rect result_in_view = result;
        GeometryMapper::SourceToDestinationRect(
            view_contents_state.Transform(), document_element_state.Transform(),
            result_in_view);
        result.Union(result_in_view);
      }
    }
  }

  return result;
}

String
PaintLayerScrollableArea::ScrollingBackgroundDisplayItemClient::DebugName()
    const {
  return "Scrolling background of " +
         scrollable_area_->GetLayoutBox()->DebugName();
}

DOMNodeId
PaintLayerScrollableArea::ScrollingBackgroundDisplayItemClient::OwnerNodeId()
    const {
  return static_cast<const DisplayItemClient*>(scrollable_area_->GetLayoutBox())
      ->OwnerNodeId();
}

String PaintLayerScrollableArea::ScrollCornerDisplayItemClient::DebugName()
    const {
  return "Scroll corner of " + scrollable_area_->GetLayoutBox()->DebugName();
}

DOMNodeId PaintLayerScrollableArea::ScrollCornerDisplayItemClient::OwnerNodeId()
    const {
  return static_cast<const DisplayItemClient*>(scrollable_area_->GetLayoutBox())
      ->OwnerNodeId();
}

}  // namespace blink
