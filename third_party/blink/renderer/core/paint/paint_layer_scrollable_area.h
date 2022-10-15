/*
 * Copyright (C) 2003, 2009, 2012 Apple Inc. All rights reserved.
 *
 * Portions are Copyright (C) 1998 Netscape Communications Corporation.
 *
 * Other contributors:
 *   Robert O'Callahan <roc+@cs.cmu.edu>
 *   David Baron <dbaron@dbaron.org>
 *   Christian Biesinger <cbiesinger@web.de>
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

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_PAINT_PAINT_LAYER_SCROLLABLE_AREA_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_PAINT_PAINT_LAYER_SCROLLABLE_AREA_H_

#include "base/check_op.h"
#include "third_party/blink/public/mojom/scroll/scroll_into_view_params.mojom-blink-forward.h"
#include "third_party/blink/renderer/core/core_export.h"
#include "third_party/blink/renderer/core/layout/scroll_anchor.h"
#include "third_party/blink/renderer/core/scroll/scrollable_area.h"
#include "third_party/blink/renderer/platform/graphics/overlay_scrollbar_clip_behavior.h"
#include "third_party/blink/renderer/platform/heap/collection_support/heap_hash_set.h"
#include "third_party/blink/renderer/platform/heap/collection_support/heap_linked_hash_set.h"
#include "third_party/blink/renderer/platform/heap/collection_support/heap_vector.h"
#include "third_party/blink/renderer/platform/heap/garbage_collected.h"
#include "third_party/blink/renderer/platform/heap/persistent.h"

namespace blink {

enum ResizerHitTestType { kResizerForPointer, kResizerForTouch };

class ComputedStyle;
class HitTestResult;
class LayoutBox;
class LayoutCustomScrollbarPart;
struct PaintInvalidatorContext;
class PaintLayer;
class ScrollingCoordinator;
class SubtreeLayoutScope;

struct CORE_EXPORT PaintLayerScrollableAreaRareData final
    : public GarbageCollected<PaintLayerScrollableAreaRareData> {
 public:
  PaintLayerScrollableAreaRareData();
  PaintLayerScrollableAreaRareData(const PaintLayerScrollableAreaRareData&) =
      delete;
  PaintLayerScrollableAreaRareData& operator=(
      const PaintLayerScrollableAreaRareData&) = delete;

  void Trace(Visitor* visitor) const;

  HeapLinkedHashSet<Member<PaintLayer>> sticky_layers_;
  HeapHashSet<Member<PaintLayer>> anchor_positioned_layers_;
  absl::optional<cc::SnapContainerData> snap_container_data_;
  bool snap_container_data_needs_update_ = true;
  bool needs_resnap_ = false;
  Vector<gfx::Rect> tickmarks_override_;
};

// PaintLayerScrollableArea represents the scrollable area of a LayoutBox.
//
// To be scrollable, an element requires ‘overflow’ != visible. Note that this
// doesn’t imply having scrollbars as you can always programmatically scroll
// when ‘overflow’ is hidden (using JavaScript's element.scrollTo or
// scrollLeft).
//
// The size and scroll origin of the scrollable area are based on layout
// dimensions. They are recomputed after layout in |UpdateScrollDimensions|.
//
// |UpdateScrollDimensions| also determines if scrollbars need to be allocated,
// destroyed or updated as a result of layout. This is based on the value of the
// 'overflow' property. Having non-overlay scrollbars automatically allocates a
// scrollcorner (|scroll_corner_|), which is used to style the intersection of
// the two scrollbars.
//
// Note that scrollbars are placed based on the LayoutBox's computed
// 'direction'. See https://webkit.org/b/54623 for some context.
//
// The ‘resize' property allocates a resizer (|resizer_|), which is overlaid on
// top of the scroll corner. It is used to resize an element using the mouse. A
// resizer can exist when there are no scrollbars.
//
// ***** OVERLAY OVERFLOW CONTROLS *****
// Overlay overflow controls are painted on top of the box's content, including
// overlay scrollbars and resizers (regardless of whether the scrollbars are
// overlaid). As such, they don't use any space in the box. Overlay overflow
// controls are painted by
// |PaintLayerPainter::PaintOverlayOverflowControlsForFragments| after all
// scrolling contents.
class CORE_EXPORT PaintLayerScrollableArea final
    : public GarbageCollected<PaintLayerScrollableArea>,
      public ScrollableArea {
  friend class Internals;

 private:
  class ScrollbarManager {
    DISALLOW_NEW();

    // Helper class to manage the life cycle of Scrollbar objects.  Some layout
    // containers (e.g., flexbox, table) run multi-pass layout on their
    // children, applying different constraints.  If a child has overflow:auto,
    // it may gain and lose scrollbars multiple times during multi-pass layout,
    // causing pointless allocation/deallocation thrashing, and potentially
    // leading to other problems (crbug.com/528940).

    // ScrollbarManager allows a ScrollableArea to delay the destruction of a
    // scrollbar that is no longer needed, until the end of multi-pass layout.
    // If the scrollbar is then re-added before multi-pass layout finishes, the
    // previously "deleted" scrollbar will be restored, rather than constructing
    // a new one.
   public:
    explicit ScrollbarManager(PaintLayerScrollableArea& scrollable_area)
        : scrollable_area_(scrollable_area),
          h_bar_is_attached_(0),
          v_bar_is_attached_(0) {}

    PaintLayerScrollableArea* ScrollableArea() const {
      return scrollable_area_.Get();
    }
    Scrollbar* HorizontalScrollbar() const {
      return h_bar_is_attached_ ? h_bar_.Get() : nullptr;
    }
    Scrollbar* VerticalScrollbar() const {
      return v_bar_is_attached_ ? v_bar_.Get() : nullptr;
    }
    bool HasHorizontalScrollbar() const { return HorizontalScrollbar(); }
    bool HasVerticalScrollbar() const { return VerticalScrollbar(); }

    void SetHasHorizontalScrollbar(bool has_scrollbar);
    void SetHasVerticalScrollbar(bool has_scrollbar);

    void DestroyDetachedScrollbars();
    void Dispose();

    void Trace(Visitor*) const;

   private:
    Scrollbar* CreateScrollbar(ScrollbarOrientation);
    void DestroyScrollbar(ScrollbarOrientation);

    Member<PaintLayerScrollableArea> scrollable_area_;

    // The scrollbars associated with scrollable_area_. Both can nullptr.
    Member<Scrollbar> h_bar_;
    Member<Scrollbar> v_bar_;

    unsigned h_bar_is_attached_ : 1;
    unsigned v_bar_is_attached_ : 1;
  };

 public:
  // If a PreventRelayoutScope object is alive, updateAfterLayout() will not
  // re-run box layout as a result of adding or removing scrollbars.
  // Instead, it will mark the PLSA as needing relayout of its box.
  // When the last PreventRelayoutScope object is popped off the stack,
  // box().setNeedsLayout(), and box().scrollbarsChanged() for LayoutBlock's,
  // will be called as appropriate for all marked PLSA's.
  class PreventRelayoutScope {
    STACK_ALLOCATED();

   public:
    explicit PreventRelayoutScope(SubtreeLayoutScope&);
    ~PreventRelayoutScope();

    static bool RelayoutIsPrevented() { return count_; }
    static void SetBoxNeedsLayout(PaintLayerScrollableArea&,
                                  bool had_horizontal_scrollbar,
                                  bool had_vertical_scrollbar);
    static bool RelayoutNeeded() { return count_ == 0 && relayout_needed_; }
    static void ResetRelayoutNeeded();

   private:
    static HeapVector<Member<PaintLayerScrollableArea>>& NeedsRelayoutList();

    static int count_;
    static SubtreeLayoutScope* layout_scope_;
    static bool relayout_needed_;
  };

  // If a FreezeScrollbarScope object is alive, updateAfterLayout() will not
  // recompute the existence of overflow:auto scrollbars.
  class FreezeScrollbarsScope {
    STACK_ALLOCATED();

   public:
    FreezeScrollbarsScope() { count_++; }
    ~FreezeScrollbarsScope() { count_--; }

    static bool ScrollbarsAreFrozen() { return count_; }

   private:
    static int count_;
  };

  // Possible root for scrollbar freezing. When established, the root box itself
  // may freeze individual scrollbars, while all descendants will freeze both
  // scrollbars.
  class FreezeScrollbarsRootScope {
    STACK_ALLOCATED();

   public:
    // Attempt to establish a scope for scrollbar freezing. If established, this
    // will freeze the selected scrollbars (if any) on the specified LayoutBox,
    // and freeze both scrollbars on all descendants. It will not be established
    // (i.e. creating the FreezeScrollbarsRootScope will have no effect) if a
    // FreezeScrollbarScope is already active, if the specified box isn't
    // scrollable, or if no scrollbars have been selected.
    FreezeScrollbarsRootScope(const LayoutBox&,
                              bool freeze_horizontal,
                              bool freeze_vertical);
    ~FreezeScrollbarsRootScope();

   private:
    PaintLayerScrollableArea* scrollable_area_;
    absl::optional<FreezeScrollbarsScope> freezer_;
  };

  // If a DelayScrollOffsetClampScope object is alive, updateAfterLayout() will
  // not clamp scroll offsets to ensure they are in the valid range.  When the
  // last DelayScrollOffsetClampScope object is destructed, all
  // PaintLayerScrollableArea's that delayed clamping their offsets will
  // immediately clamp them.
  class CORE_EXPORT DelayScrollOffsetClampScope {
    STACK_ALLOCATED();

   public:
    DelayScrollOffsetClampScope();
    ~DelayScrollOffsetClampScope();

    static bool ClampingIsDelayed() { return count_; }
    static void SetNeedsClamp(PaintLayerScrollableArea*);

   private:
    static void ClampScrollableAreas();
    static HeapVector<Member<PaintLayerScrollableArea>>& NeedsClampList();

    static int count_;
  };

  // FIXME: We should pass in the LayoutBox but this opens a window
  // for crashers during PaintLayer setup (see crbug.com/368062).
  explicit PaintLayerScrollableArea(PaintLayer&);
  ~PaintLayerScrollableArea() override;

  // Return the PaintLayerScrollableArea (if any) associated with the Node.
  static PaintLayerScrollableArea* FromNode(const Node&);

  void ForceVerticalScrollbarForFirstLayout() { SetHasVerticalScrollbar(true); }
  bool HasHorizontalScrollbar() const { return HorizontalScrollbar(); }
  bool HasVerticalScrollbar() const { return VerticalScrollbar(); }

  Scrollbar* HorizontalScrollbar() const override {
    return scrollbar_manager_.HorizontalScrollbar();
  }
  Scrollbar* VerticalScrollbar() const override {
    return scrollbar_manager_.VerticalScrollbar();
  }

  bool IsThrottled() const override;
  ChromeClient* GetChromeClient() const override;

  SmoothScrollSequencer* GetSmoothScrollSequencer() const override;

  void DidCompositorScroll(const gfx::PointF&) override;

  bool ShouldScrollOnMainThread() const override;
  bool IsActive() const override;
  bool IsScrollCornerVisible() const override;
  gfx::Rect ScrollCornerRect() const override;
  void SetScrollCornerNeedsPaintInvalidation() override;
  gfx::Rect ConvertFromScrollbarToContainingEmbeddedContentView(
      const Scrollbar&,
      const gfx::Rect&) const override;
  gfx::Point ConvertFromScrollbarToContainingEmbeddedContentView(
      const Scrollbar&,
      const gfx::Point&) const override;
  gfx::Point ConvertFromContainingEmbeddedContentViewToScrollbar(
      const Scrollbar&,
      const gfx::Point&) const override;
  gfx::Point ConvertFromRootFrame(const gfx::Point&) const override;
  gfx::Point ConvertFromRootFrameToVisualViewport(
      const gfx::Point&) const override;
  int ScrollSize(ScrollbarOrientation) const override;
  gfx::PointF ScrollOffsetToPosition(
      const ScrollOffset& offset) const override {
    return gfx::PointF(ScrollOrigin()) + offset;
  }
  ScrollOffset ScrollPositionToOffset(
      const gfx::PointF& position) const override {
    return ScrollOffset(position - gfx::PointF(ScrollOrigin()));
  }
  gfx::Vector2d ScrollOffsetInt() const override;
  ScrollOffset GetScrollOffset() const override;
  // Commits a final scroll offset for the frame, if it might have changed.
  // If it did change, enqueues a scroll event.
  void EnqueueScrollEventIfNeeded();
  gfx::Vector2d MinimumScrollOffsetInt() const override;
  gfx::Vector2d MaximumScrollOffsetInt() const override;
  gfx::Rect VisibleContentRect(
      IncludeScrollbarsInRect = kExcludeScrollbars) const override;
  PhysicalRect VisibleScrollSnapportRect(
      IncludeScrollbarsInRect = kExcludeScrollbars) const override;
  gfx::Size ContentsSize() const override;

  // Similar to |ContentsSize| but snapped considering |paint_offset| which can
  // have subpixel accumulation.
  gfx::Size PixelSnappedContentsSize(const PhysicalOffset& paint_offset) const;

  void ContentsResized() override;
  gfx::Point LastKnownMousePosition() const override;
  bool ScrollAnimatorEnabled() const override;
  bool ShouldSuspendScrollAnimations() const override;
  bool ScrollbarsCanBeActive() const override;
  void ScrollbarVisibilityChanged() override;
  void ScrollbarFrameRectChanged() override;
  void RegisterForAnimation() override;
  void DeregisterForAnimation() override;
  bool UserInputScrollable(ScrollbarOrientation) const override;
  bool ShouldPlaceVerticalScrollbarOnLeft() const override;
  int PageStep(ScrollbarOrientation) const override;
  mojom::blink::ScrollBehavior ScrollBehaviorStyle() const override;
  mojom::blink::ColorScheme UsedColorScheme() const override;
  cc::AnimationHost* GetCompositorAnimationHost() const override;
  cc::AnimationTimeline* GetCompositorAnimationTimeline() const override;
  bool HasTickmarks() const override;
  Vector<gfx::Rect> GetTickmarks() const override;

  void VisibleSizeChanged();

  // See renderer/core/layout/README.md for an explanation of scroll origin.
  gfx::Point ScrollOrigin() const { return scroll_origin_; }
  bool ScrollOriginChanged() const { return scroll_origin_changed_; }

  void ScrollToAbsolutePosition(const gfx::PointF& position,
                                mojom::blink::ScrollBehavior scroll_behavior =
                                    mojom::blink::ScrollBehavior::kInstant,
                                mojom::blink::ScrollType scroll_type =
                                    mojom::blink::ScrollType::kProgrammatic) {
    SetScrollOffset(ScrollOffset(position - gfx::PointF(ScrollOrigin())),
                    scroll_type, scroll_behavior);
  }

  // This will set the scroll position without clamping, and it will do all
  // post-update work even if the scroll position didn't change.
  void SetScrollOffsetUnconditionally(
      const ScrollOffset&,
      mojom::blink::ScrollType = mojom::blink::ScrollType::kProgrammatic);

  // TODO(szager): Actually run these after all of layout is finished.
  // Currently, they run at the end of box()'es layout (or after all flexbox
  // layout has finished) but while document layout is still happening.
  void UpdateAfterLayout();
  void ClampScrollOffsetAfterOverflowChange();

  void DidChangeGlobalRootScroller() override;

  void UpdateAfterStyleChange(const ComputedStyle* old_style);
  void UpdateAfterOverflowRecalc();

  bool HasScrollbar() const {
    return HasHorizontalScrollbar() || HasVerticalScrollbar();
  }
  // Overflow controls are scrollbars, scroll corners, and resizers. The
  // |scroll_corner_| and |resizer_| scrollbar parts are only created for
  // specific pseudo styles but there can still be a scroll corner control or
  // resize control without these custom styled scrollbar parts.
  bool HasOverflowControls() const;

  bool HasOverlayOverflowControls() const;
  bool NeedsScrollCorner() const;

  bool ShouldOverflowControlsPaintAsOverlay() const;

  bool HasOverflow() const {
    return HasHorizontalOverflow() || HasVerticalOverflow();
  }

  LayoutCustomScrollbarPart* ScrollCorner() const { return scroll_corner_; }

  void Resize(const gfx::Point& pos, const LayoutSize& old_offset);
  gfx::Vector2d OffsetFromResizeCorner(const gfx::Point& absolute_point) const;

  bool InResizeMode() const { return in_resize_mode_; }
  void SetInResizeMode(bool in_resize_mode) {
    in_resize_mode_ = in_resize_mode;
  }

  LayoutUnit ScrollWidth() const;
  LayoutUnit ScrollHeight() const;

  int VerticalScrollbarWidth(OverlayScrollbarClipBehavior =
                                 kIgnoreOverlayScrollbarSize) const override;
  int HorizontalScrollbarHeight(OverlayScrollbarClipBehavior =
                                    kIgnoreOverlayScrollbarSize) const override;

  void PositionOverflowControls();

  // Test if a pointer/touch position is in the resize control area.
  bool IsAbsolutePointInResizeControl(const gfx::Point& absolute_point,
                                      ResizerHitTestType) const;
  bool IsLocalPointInResizeControl(const gfx::Point& local_point,
                                   ResizerHitTestType) const;

  bool HitTestOverflowControls(HitTestResult&, const gfx::Point& local_point);

  // Returns the new offset, after scrolling, of the given rect in absolute
  // coordinates, clipped by the parent's client rect.
  PhysicalRect ScrollIntoView(
      const PhysicalRect&,
      const mojom::blink::ScrollIntoViewParamsPtr&) override;

  // Returns true if the scrollable area is user-scrollable, visible to hit
  // testing, and it does in fact overflow. This means this method will return
  // false for 'overflow: hidden' and 'pointer-events: none'.
  bool ScrollsOverflow() const { return scrolls_overflow_; }

  // Rectangle encompassing the scroll corner and resizer rect.
  gfx::Rect ScrollCornerAndResizerRect() const;

  // The difference between this function and NeedsCompositedScrolling() is
  // that this function returns the composited scrolling status based on paint
  // properties which are updated based on the latter.
  bool UsesCompositedScrolling() const override;

  void UpdateNeedsCompositedScrolling(
      bool force_prefer_compositing_to_lcd_text);
  bool NeedsCompositedScrolling() const { return needs_composited_scrolling_; }
#if DCHECK_IS_ON()
  void CheckNeedsCompositedScrollingIsUpToDate(
      bool force_prefer_compositing_to_lcd_text) {
    DCHECK_EQ(
        needs_composited_scrolling_,
        ComputeNeedsCompositedScrolling(force_prefer_compositing_to_lcd_text));
  }
#endif

  gfx::Rect ResizerCornerRect(ResizerHitTestType) const;

  PaintLayer* Layer() const override;

  LayoutCustomScrollbarPart* Resizer() const { return resizer_; }

  gfx::Rect RectForHorizontalScrollbar() const;
  gfx::Rect RectForVerticalScrollbar() const;

  bool ScheduleAnimation() override;
  bool ShouldPerformScrollAnchoring() const override;
  bool RestoreScrollAnchor(const SerializedAnchor&) override;
  ScrollAnchor* GetScrollAnchor() override { return &scroll_anchor_; }
  bool IsPaintLayerScrollableArea() const override { return true; }
  bool IsRootFrameLayoutViewport() const override;

  LayoutBox* GetLayoutBox() const override;

  gfx::QuadF LocalToVisibleContentQuad(const gfx::QuadF&,
                                       const LayoutObject*,
                                       unsigned = 0) const final;

  scoped_refptr<base::SingleThreadTaskRunner> GetTimerTaskRunner() const final;

  // Did DelayScrollOffsetClampScope prevent us from running
  // clampScrollOffsetsAfterLayout() in updateAfterLayout()?
  bool NeedsScrollOffsetClamp() const { return needs_scroll_offset_clamp_; }
  void SetNeedsScrollOffsetClamp(bool val) { needs_scroll_offset_clamp_ = val; }

  // Did PreventRelayoutScope prevent us from running re-layout due to
  // adding/subtracting scrollbars in updateAfterLayout()?
  bool NeedsRelayout() const { return needs_relayout_; }
  void SetNeedsRelayout(bool val) { needs_relayout_ = val; }

  // Were we laid out with a horizontal scrollbar at the time we were marked as
  // needing relayout by PreventRelayoutScope?
  bool HadHorizontalScrollbarBeforeRelayout() const {
    return had_horizontal_scrollbar_before_relayout_;
  }
  void SetHadHorizontalScrollbarBeforeRelayout(bool val) {
    had_horizontal_scrollbar_before_relayout_ = val;
  }

  // Were we laid out with a vertical scrollbar at the time we were marked as
  // needing relayout by PreventRelayoutScope?
  bool HadVerticalScrollbarBeforeRelayout() const {
    return had_vertical_scrollbar_before_relayout_;
  }
  void SetHadVerticalScrollbarBeforeRelayout(bool val) {
    had_vertical_scrollbar_before_relayout_ = val;
  }

  void AddStickyLayer(PaintLayer*);
  void RemoveStickyLayer(PaintLayer*);
  bool HasStickyLayer(PaintLayer* layer) const {
    return rare_data_ && rare_data_->sticky_layers_.Contains(layer);
  }
  void InvalidateAllStickyConstraints();
  void InvalidatePaintForStickyDescendants();

  // Returns true if the layer is not already added.
  bool AddAnchorPositionedLayer(PaintLayer*);
  void InvalidateAllAnchorPositionedLayers();
  void InvalidatePaintForAnchorPositionedLayers();

  uint32_t GetNonCompositedMainThreadScrollingReasons() {
    return non_composited_main_thread_scrolling_reasons_;
  }

  ScrollbarTheme& GetPageScrollbarTheme() const override;

  // Return the thickness of the existing scrollbar; or, if there is no
  // existing scrollbar, then calculate the thickness it would have if it
  // existed. Returns zero if the (real or hypothetical) scrollbar is an overlay
  // scrollbar, unless should_include_overlay_thickness has been specified.
  int HypotheticalScrollbarThickness(
      ScrollbarOrientation,
      bool should_include_overlay_thickness = false) const;

  void DidAddScrollbar(Scrollbar&, ScrollbarOrientation) override;
  void WillRemoveScrollbar(Scrollbar&, ScrollbarOrientation) override;

  void InvalidatePaintOfScrollControlsIfNeeded(const PaintInvalidatorContext&);

  void DidScrollWithScrollbar(ScrollbarPart,
                              ScrollbarOrientation,
                              WebInputEvent::Type) override;
  CompositorElementId GetScrollElementId() const override;

  bool VisualViewportSuppliesScrollbars() const override;

  bool HasHorizontalOverflow() const;
  bool HasVerticalOverflow() const;

  void Trace(Visitor*) const override;

  gfx::Rect ScrollingBackgroundVisualRect(
      const PhysicalOffset& paint_offset) const;
  const DisplayItemClient& GetScrollingBackgroundDisplayItemClient() const {
    return *scrolling_background_display_item_client_;
  }
  const DisplayItemClient& GetScrollCornerDisplayItemClient() const {
    return *scroll_corner_display_item_client_;
  }

  const cc::SnapContainerData* GetSnapContainerData() const override;
  void SetSnapContainerData(absl::optional<cc::SnapContainerData>) override;
  bool SetTargetSnapAreaElementIds(cc::TargetSnapAreaElementIds) override;
  bool SnapContainerDataNeedsUpdate() const override;
  void SetSnapContainerDataNeedsUpdate(bool) override;
  bool NeedsResnap() const override;
  void SetNeedsResnap(bool) override;

  absl::optional<gfx::PointF> GetSnapPositionAndSetTarget(
      const cc::SnapSelectionStrategy& strategy) override;

  void DisposeImpl() override;

  void SetPendingHistoryRestoreScrollOffset(
      const HistoryItem::ViewState& view_state,
      bool should_restore_scroll) override {
    if (!should_restore_scroll)
      return;
    pending_view_state_ = view_state;
  }

  void ApplyPendingHistoryRestoreScrollOffset() override;

  bool HasPendingHistoryRestoreScrollOffset() override {
    return !!pending_view_state_;
  }

  void SetTickmarksOverride(Vector<gfx::Rect> tickmarks);

  bool ShouldDirectlyCompositeScrollbar(const Scrollbar&) const;

  void EstablishScrollbarRoot(bool freeze_horizontal, bool freeze_vertical);
  void ClearScrollbarRoot();
  bool IsHorizontalScrollbarFrozen() const {
    if (is_scrollbar_freeze_root_)
      return is_horizontal_scrollbar_frozen_;
    return FreezeScrollbarsScope::ScrollbarsAreFrozen();
  }
  bool IsVerticalScrollbarFrozen() const {
    if (is_scrollbar_freeze_root_)
      return is_vertical_scrollbar_frozen_;
    return FreezeScrollbarsScope::ScrollbarsAreFrozen();
  }

  // Force scrollbars off for reconstruction.
  void RemoveScrollbarsForReconstruction();

  void DidUpdateCullRect() {
    last_cull_rect_update_scroll_offset_ = scroll_offset_;
  }
  ScrollOffset LastCullRectUpdateScrollOffset() const {
    return last_cull_rect_update_scroll_offset_;
  }

 private:
  // This also updates main thread scrolling reasons and the LayoutBox's
  // background paint location.
  bool ComputeNeedsCompositedScrolling(
      bool force_prefer_compositing_to_lcd_text);

  bool NeedsScrollbarReconstruction() const;

  void ResetScrollOriginChanged() { scroll_origin_changed_ = false; }
  void UpdateScrollOrigin();
  void UpdateScrollDimensions();
  void UpdateScrollbarEnabledState(bool is_horizontal_scrollbar_frozen = false,
                                   bool is_vertical_scrollbar_frozen = false);

  // Update the proportions used for thumb rect dimensions.
  void UpdateScrollbarProportions();

  void UpdateScrollOffset(const ScrollOffset&,
                          mojom::blink::ScrollType) override;
  void InvalidatePaintForScrollOffsetChange();

  int VerticalScrollbarStart() const;
  int HorizontalScrollbarStart() const;
  gfx::Vector2d ScrollbarOffset(const Scrollbar&) const;

  // If OverflowIndependent is specified, will only change current scrollbar
  // existence if the new style doesn't depend on overflow which requires
  // layout to be clean. It'd be nice if we could always determine existence at
  // one point, after layout. Unfortunately, it seems that parts of layout are
  // dependent on scrollbar existence in cases like |overflow:scroll|, removing
  // the post style pass causes breaks in tests e.g. forms web_tests. Thus, we
  // must do two scrollbar existence passes.
  enum ComputeScrollbarExistenceOption {
    kDependsOnOverflow,
    kOverflowIndependent
  };
  void ComputeScrollbarExistence(
      bool& needs_horizontal_scrollbar,
      bool& needs_vertical_scrollbar,
      ComputeScrollbarExistenceOption = kDependsOnOverflow) const;

  // If the content fits entirely in the area without auto scrollbars, returns
  // true to try to remove them. This is a heuristic and can be incorrect if the
  // content size depends on the scrollbar size (e.g., percentage sizing).
  bool TryRemovingAutoScrollbars(const bool& needs_horizontal_scrollbar,
                                 const bool& needs_vertical_scrollbar);

  // Returns true iff scrollbar existence changed.
  bool SetHasHorizontalScrollbar(bool has_scrollbar);
  bool SetHasVerticalScrollbar(bool has_scrollbar);

  void UpdateScrollCornerStyle();
  LayoutSize MinimumSizeForResizing(float zoom_factor);
  PhysicalRect LayoutContentRect(IncludeScrollbarsInRect) const;

  void UpdateResizerStyle(const ComputedStyle* old_style);

  void UpdateScrollableAreaSet();

  ScrollingCoordinator* GetScrollingCoordinator() const;

  PaintLayerScrollableAreaRareData* RareData() { return rare_data_; }
  const PaintLayerScrollableAreaRareData* RareData() const {
    return rare_data_;
  }

  PaintLayerScrollableAreaRareData& EnsureRareData() {
    if (!rare_data_)
      rare_data_ = MakeGarbageCollected<PaintLayerScrollableAreaRareData>();
    return *rare_data_;
  }

  gfx::Rect CornerRect() const;

  void ScrollControlWasSetNeedsPaintInvalidation() override;

  gfx::Size PixelSnappedBorderBoxSize() const;

  using BackgroundPaintLocation = uint8_t;
  bool ComputeNeedsCompositedScrollingInternal(
      BackgroundPaintLocation background_paint_location_if_composited,
      bool force_prefer_compositing_to_lcd_text);

  void InvalidatePaintOfScrollbarIfNeeded(
      const PaintInvalidatorContext&,
      bool needs_paint_invalidation,
      Scrollbar* scrollbar,
      bool& previously_was_overlay,
      bool& previously_was_directly_composited,
      gfx::Rect& visual_rect);

  // PaintLayer is destructed before PaintLayerScrollable area, during this
  // time before PaintLayerScrollableArea has been collected layer_ will
  // be set to nullptr by the Dispose method.
  Member<PaintLayer> layer_;

  // Keeps track of whether the layer is currently resizing, so events can cause
  // resizing to start and stop.
  unsigned in_resize_mode_ : 1;
  unsigned scrolls_overflow_ : 1;

  // True if we are in an overflow scrollbar relayout.
  unsigned in_overflow_relayout_ : 1;

  // True if a second overflow scrollbar relayout is permitted.
  unsigned allow_second_overflow_relayout_ : 1;

  // FIXME: once cc can handle composited scrolling with clip paths, we will
  // no longer need this bit.
  unsigned needs_composited_scrolling_ : 1;

  unsigned needs_scroll_offset_clamp_ : 1;
  unsigned needs_relayout_ : 1;
  unsigned had_horizontal_scrollbar_before_relayout_ : 1;
  unsigned had_vertical_scrollbar_before_relayout_ : 1;
  unsigned had_resizer_before_relayout_ : 1;
  unsigned scroll_origin_changed_ : 1;

  unsigned is_scrollbar_freeze_root_ : 1;
  unsigned is_horizontal_scrollbar_frozen_ : 1;
  unsigned is_vertical_scrollbar_frozen_ : 1;

  // There are 6 possible combinations of writing mode and direction. Scroll
  // origin will be non-zero in the x or y axis if there is any reversed
  // direction or writing-mode. The combinations are:
  // writing-mode / direction     scrollOrigin.x() set    scrollOrigin.y() set
  // horizontal-tb / ltr          NO                      NO
  // horizontal-tb / rtl          YES                     NO
  // vertical-lr / ltr            NO                      NO
  // vertical-lr / rtl            NO                      YES
  // vertical-rl / ltr            YES                     NO
  // vertical-rl / rtl            YES                     YES
  gfx::Point scroll_origin_;

  // The width/height of our scrolled area.
  // This is OverflowModel's layout overflow translated to physical
  // coordinates. See OverflowModel for the different overflow and
  // LayoutBoxModelObject for the coordinate systems.
  PhysicalRect overflow_rect_;

  // ScrollbarManager holds the Scrollbar instances.
  ScrollbarManager scrollbar_manager_;

  // This is the offset from the beginning of content flow.
  ScrollOffset scroll_offset_;

  // The last scroll offset that was committed during the main document
  // lifecycle.
  bool has_last_committed_scroll_offset_;
  ScrollOffset last_committed_scroll_offset_;

  // LayoutObject to hold our custom scroll corner.
  Member<LayoutCustomScrollbarPart> scroll_corner_;

  // LayoutObject to hold our custom resizer.
  Member<LayoutCustomScrollbarPart> resizer_;

  ScrollAnchor scroll_anchor_;

  Member<PaintLayerScrollableAreaRareData> rare_data_;

  // MainThreadScrollingReason due to the properties of the LayoutObject
  uint32_t non_composited_main_thread_scrolling_reasons_;

  // These are not bitfields because they need to be passed as references.
  bool horizontal_scrollbar_previously_was_overlay_ = false;
  bool vertical_scrollbar_previously_was_overlay_ = false;
  bool horizontal_scrollbar_previously_was_directly_composited_ = false;
  bool vertical_scrollbar_previously_was_directly_composited_ = false;
  gfx::Rect horizontal_scrollbar_visual_rect_;
  gfx::Rect vertical_scrollbar_visual_rect_;
  gfx::Rect scroll_corner_and_resizer_visual_rect_;

  ScrollOffset last_cull_rect_update_scroll_offset_;

  class ScrollingBackgroundDisplayItemClient final
      : public GarbageCollected<ScrollingBackgroundDisplayItemClient>,
        public DisplayItemClient {
   public:
    explicit ScrollingBackgroundDisplayItemClient(
        const PaintLayerScrollableArea& scrollable_area)
        : scrollable_area_(&scrollable_area) {}

    void Trace(Visitor* visitor) const override {
      visitor->Trace(scrollable_area_);
      DisplayItemClient::Trace(visitor);
    }

   private:
    String DebugName() const final;
    DOMNodeId OwnerNodeId() const final;

    Member<const PaintLayerScrollableArea> scrollable_area_;
  };

  class ScrollCornerDisplayItemClient final
      : public GarbageCollected<ScrollCornerDisplayItemClient>,
        public DisplayItemClient {
   public:
    explicit ScrollCornerDisplayItemClient(
        const PaintLayerScrollableArea& scrollable_area)
        : scrollable_area_(&scrollable_area) {}

    void Trace(Visitor* visitor) const override {
      visitor->Trace(scrollable_area_);
      DisplayItemClient::Trace(visitor);
    }

   private:
    String DebugName() const final;
    DOMNodeId OwnerNodeId() const final;

    Member<const PaintLayerScrollableArea> scrollable_area_;
  };

  Member<ScrollingBackgroundDisplayItemClient>
      scrolling_background_display_item_client_ =
          MakeGarbageCollected<ScrollingBackgroundDisplayItemClient>(*this);
  Member<ScrollCornerDisplayItemClient> scroll_corner_display_item_client_ =
      MakeGarbageCollected<ScrollCornerDisplayItemClient>(*this);
  absl::optional<HistoryItem::ViewState> pending_view_state_;
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_CORE_PAINT_PAINT_LAYER_SCROLLABLE_AREA_H_
