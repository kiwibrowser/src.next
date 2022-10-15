/*
   Copyright (C) 1997 Martin Jones (mjones@kde.org)
             (C) 1998 Waldo Bastian (bastian@kde.org)
             (C) 1998, 1999 Torben Weis (weis@kde.org)
             (C) 1999 Lars Knoll (knoll@kde.org)
             (C) 1999 Antti Koivisto (koivisto@kde.org)
   Copyright (C) 2004, 2005, 2006, 2007, 2008, 2009 Apple Inc. All rights
   reserved.

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public
   License as published by the Free Software Foundation; either
   version 2 of the License, or (at your option) any later version.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public License
   along with this library; see the file COPYING.LIB.  If not, write to
   the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301, USA.
*/

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_FRAME_LOCAL_FRAME_VIEW_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_FRAME_LOCAL_FRAME_VIEW_H_

#include <memory>

#include "base/auto_reset.h"
#include "base/callback_forward.h"
#include "base/dcheck_is_on.h"
#include "base/functional/function_ref.h"
#include "base/gtest_prod_util.h"
#include "base/time/time.h"
#include "third_party/blink/public/common/metrics/document_update_reason.h"
#include "third_party/blink/public/mojom/frame/lifecycle.mojom-blink-forward.h"
#include "third_party/blink/public/mojom/frame/viewport_intersection_state.mojom-blink-forward.h"
#include "third_party/blink/public/mojom/scroll/scroll_into_view_params.mojom-blink-forward.h"
#include "third_party/blink/renderer/core/core_export.h"
#include "third_party/blink/renderer/core/document_transition/document_transition_request.h"
#include "third_party/blink/renderer/core/dom/document_lifecycle.h"
#include "third_party/blink/renderer/core/frame/frame_view.h"
#include "third_party/blink/renderer/core/frame/layout_subtree_root_list.h"
#include "third_party/blink/renderer/core/frame/overlay_interstitial_ad_detector.h"
#include "third_party/blink/renderer/core/frame/sticky_ad_detector.h"
#include "third_party/blink/renderer/core/layout/depth_ordered_layout_object_list.h"
#include "third_party/blink/renderer/core/layout/hit_test_result.h"
#include "third_party/blink/renderer/core/paint/layout_object_counter.h"
#include "third_party/blink/renderer/platform/geometry/layout_size.h"
#include "third_party/blink/renderer/platform/graphics/color.h"
#include "third_party/blink/renderer/platform/graphics/compositor_element_id.h"
#include "third_party/blink/renderer/platform/graphics/paint/cull_rect.h"
#include "third_party/blink/renderer/platform/graphics/paint_invalidation_reason.h"
#include "third_party/blink/renderer/platform/graphics/subtree_paint_property_update_reason.h"
#include "third_party/blink/renderer/platform/runtime_enabled_features.h"
#include "third_party/blink/renderer/platform/scheduler/public/post_cancellable_task.h"
#include "third_party/blink/renderer/platform/timer.h"
#include "third_party/blink/renderer/platform/wtf/allocator/allocator.h"
#include "third_party/blink/renderer/platform/wtf/casting.h"
#include "ui/gfx/geometry/rect.h"

template <typename T>
class sk_sp;

namespace cc {
class AnimationHost;
class AnimationTimeline;
class Layer;
class PaintOpBuffer;
enum class PaintHoldingCommitTrigger;
using PaintRecord = PaintOpBuffer;
struct PaintBenchmarkResult;
}

namespace gfx {
class SizeF;
}

namespace ui {
class Cursor;
}

namespace blink {
class AXObjectCache;
class ChromeClient;
class DarkModeFilter;
class DocumentLifecycle;
class FragmentAnchor;
class Frame;
class FrameViewAutoSizeInfo;
class HTMLVideoElement;
class JSONObject;
class KURL;
class LayoutBox;
class LayoutEmbeddedObject;
class LayoutObject;
class LayoutShiftTracker;
class LayoutSVGRoot;
class LayoutView;
class LocalFrame;
class MobileFriendlinessChecker;
class Page;
class PaintArtifactCompositor;
class PaintController;
class PaintControllerCycleScope;
class PaintLayer;
class PaintLayerScrollableArea;
class PaintTimingDetector;
class RemoteFrameView;
class RootFrameViewport;
class ScrollableArea;
class Scrollbar;
class ScrollingCoordinator;
class TransformState;
class LocalFrameUkmAggregator;
class WebPluginContainerImpl;
struct AnnotatedRegionValue;
struct IntrinsicSizingInfo;
struct MobileFriendliness;
struct PhysicalOffset;
struct PhysicalRect;

enum class PaintBenchmarkMode;
enum class PaintArtifactCompositorUpdateReason;

typedef uint64_t DOMTimeStamp;
using LayerTreeFlags = unsigned;
using MainThreadScrollingReasons = uint32_t;

struct LifecycleData {
  LifecycleData() {}
  LifecycleData(base::TimeTicks start_time_arg, int count_arg)
      : start_time(start_time_arg), count(count_arg) {}
  base::TimeTicks start_time;
  // The number of lifecycles that have occcurred since the first one,
  // inclusive, on a given LocalFrameRoot.
  unsigned count = 0;
};

class CORE_EXPORT LocalFrameView final
    : public GarbageCollected<LocalFrameView>,
      public FrameView {
  friend class PaintControllerPaintTestBase;
  friend class Internals;

 public:
  class CORE_EXPORT LifecycleNotificationObserver
      : public GarbageCollectedMixin {
   public:
    // These are called when the lifecycle updates start/finish.
    virtual void WillStartLifecycleUpdate(const LocalFrameView&) {}
    virtual void DidFinishLifecycleUpdate(const LocalFrameView&) {}
  };

  explicit LocalFrameView(LocalFrame&);
  LocalFrameView(LocalFrame&, const gfx::Size& initial_size);
  ~LocalFrameView() override;

  LocalFrame& GetFrame() const {
    DCHECK(frame_);
    return *frame_;
  }

  Page* GetPage() const;

  LayoutView* GetLayoutView() const;

  void SetCanHaveScrollbars(bool can_have_scrollbars) {
    can_have_scrollbars_ = can_have_scrollbars;
  }
  bool CanHaveScrollbars() const { return can_have_scrollbars_; }
  bool VisualViewportSuppliesScrollbars();

  void SetLayoutOverflowSize(const gfx::Size&);

  bool DidFirstLayout() const;
  bool LifecycleUpdatesActive() const;
  void SetLifecycleUpdatesThrottledForTesting(bool throttled = true);
  void ScheduleRelayout();
  void ScheduleRelayoutOfSubtree(LayoutObject*);
  bool LayoutPending() const;
  bool IsInPerformLayout() const;

  // Methods to capture forced layout metrics.
  void WillStartForcedLayout();
  void DidFinishForcedLayout(DocumentUpdateReason);

  void ClearLayoutSubtreeRoot(const LayoutObject&);
  void AddOrthogonalWritingModeRoot(LayoutBox&);
  void RemoveOrthogonalWritingModeRoot(LayoutBox&);
  bool HasOrthogonalWritingModeRoots() const;
  void LayoutOrthogonalWritingModeRoots();
  void ScheduleOrthogonalWritingModeRootsForLayout();
  void MarkOrthogonalWritingModeRootsForLayout();

  // Returns true if commits will be deferred for first contentful paint.
  bool WillDoPaintHoldingForFCP() const;

  unsigned LayoutCountForTesting() const { return layout_count_for_testing_; }
  unsigned LifecycleUpdateCountForTesting() const {
    return lifecycle_update_count_for_testing_;
  }

  void CountObjectsNeedingLayout(unsigned& needs_layout_objects,
                                 unsigned& total_objects,
                                 bool& is_partial);

  bool NeedsLayout() const;
  bool CheckDoesNotNeedLayout() const;
  void SetNeedsLayout();

  void SetNeedsUpdateGeometries() { needs_update_geometries_ = true; }
  void UpdateGeometry() override;

  bool LoadAllLazyLoadedIframes();

  void UpdateStyleAndLayout();

  // Marks this frame, and ancestor frames, as needing one intersection
  // observervation. This overrides throttling for one frame, up to
  // kLayoutClean. The order of these enums is important - they must proceed
  // from "least required to most required".
  enum IntersectionObservationState {
    // The next painting frame does not need an intersection observation.
    kNotNeeded = 0,
    // The next painting frame needs an intersection observation.
    kDesired = 1,
    // The next painting frame must be generated up to intersection observation
    // (even if frame is throttled).
    kRequired = 2
  };

  // Sets the internal IntersectionObservationState to the max of the
  // current value and the provided one.
  void SetIntersectionObservationState(IntersectionObservationState);
  IntersectionObservationState GetIntersectionObservationStateForTesting()
      const {
    return intersection_observation_state_;
  }

  // Get the InstersectionObservation::ComputeFlags for target elements in this
  // view.
  unsigned GetIntersectionObservationFlags(unsigned parent_flags) const;

  void ForceUpdateViewportIntersections();

  void SetPaintArtifactCompositorNeedsUpdate(
      PaintArtifactCompositorUpdateReason);

  // Methods for getting/setting the size Blink should use to layout the
  // contents.
  gfx::Size GetLayoutSize() const { return layout_size_; }
  void SetLayoutSize(const gfx::Size&);

  // If this is set to false, the layout size will need to be explicitly set by
  // the owner.  E.g. WebViewImpl sets its mainFrame's layout size manually
  void SetLayoutSizeFixedToFrameSize(bool);
  bool LayoutSizeFixedToFrameSize() const {
    return layout_size_fixed_to_frame_size_;
  }

  bool GetIntrinsicSizingInfo(IntrinsicSizingInfo&) const override;
  bool HasIntrinsicSizingInfo() const override;

  void Dispose() override;
  void PropagateFrameRects() override;
  void InvalidateAllCustomScrollbarsOnActiveChanged();

  void UsesOverlayScrollbarsChanged();

  Color BaseBackgroundColor() const;
  void SetBaseBackgroundColor(const Color&);
  void UpdateBaseBackgroundColorRecursively(const Color&);

  enum class UseColorAdjustBackground {
    // Use the base background color set on this view.
    kNo,
    // Use the color-adjust background from StyleEngine instead of the base
    // background color.
    kYes,
    // Use the color-adjust background from StyleEngine, but only if the base
    // background is not transparent.
    kIfBaseNotTransparent,
  };

  void SetUseColorAdjustBackground(UseColorAdjustBackground use,
                                   bool color_scheme_changed);
  bool ShouldPaintBaseBackgroundColor() const;

  void AdjustViewSize();

  // Scale used to convert incoming input events.
  float InputEventsScaleFactor() const;

  void DidChangeScrollOffset();

  void ViewportSizeChanged(bool width_changed, bool height_changed);
  void MarkFixedPositionObjectsForLayout(bool width_changed,
                                         bool height_changed);
  void DynamicViewportUnitsChanged();

  AtomicString MediaType() const;
  void SetMediaType(const AtomicString&);
  void AdjustMediaTypeForPrinting(bool printing);

  typedef HeapHashSet<Member<LayoutObject>> ObjectSet;
  void AddFixedPositionObject(LayoutObject&);
  void RemoveFixedPositionObject(LayoutObject&);
  const ObjectSet* FixedPositionObjects() const {
    return fixed_position_objects_;
  }
  bool HasFixedPositionObjects() const {
    return fixed_position_objects_ && fixed_position_objects_->size() > 0;
  }

  // Objects with background-attachment:fixed.
  void AddBackgroundAttachmentFixedObject(LayoutObject*);
  void RemoveBackgroundAttachmentFixedObject(LayoutObject*);
  bool RequiresMainThreadScrollingForBackgroundAttachmentFixed() const;
  const ObjectSet& BackgroundAttachmentFixedObjects() const {
    return background_attachment_fixed_objects_;
  }
  void InvalidateBackgroundAttachmentFixedDescendantsOnScroll(
      const LayoutObject& scrolled_object);

  void HandleLoadCompleted();

  void UpdateDocumentAnnotatedRegions() const;

  void DidAttachDocument();

  void ClearRootScroller();
  void InitializeRootScroller();

  void AddPartToUpdate(LayoutEmbeddedObject&);

  Color DocumentBackgroundColor();

  // Called when this view is going to be removed from its owning
  // LocalFrame.
  void WillBeRemovedFromFrame();

  bool IsUpdatingLifecycle() const;

  // Run all needed lifecycle stages. After calling this method, all
  // non-throttled frames will be in the lifecycle state PaintClean.
  // AllowThrottlingScope is used to allow frame throttling. Throttled frames
  // will skip the lifecycle update and will not end up being PaintClean.
  // Set |reason| to indicate the reason for this update, for metrics purposes.
  // Returns whether the lifecycle was successfully updated to PaintClean.
  bool UpdateAllLifecyclePhases(DocumentUpdateReason reason);

  // Runs UpdateAllLifecyclePhases(DocumentUpdateReason::kTest) followed by
  // RunPostLifecycleSteps(), which is what a full BeginMainFrame() would do.
  bool UpdateAllLifecyclePhasesForTest();

  // Computes the style, layout, compositing and pre-paint lifecycle stages
  // if needed. Frame throttling is not enabled by default.
  // After calling this method, all frames will be in a lifecycle
  // state >= PrePaintClean, unless the frame was throttled (if frame
  // throttling, which is not allowed by default, is allowed by the caller) or
  // inactive.
  // Returns whether the lifecycle was successfully updated to the
  // desired state.
  bool UpdateAllLifecyclePhasesExceptPaint(DocumentUpdateReason reason);

  // Printing needs everything up-to-date except paint (which will be done
  // specially). We may also print a detached frame or a descendant of a
  // detached frame and need special handling of the frame.
  // Frame throttling is not allowed by default. Normally we don't want to
  // throttle frames for printing.
  void UpdateLifecyclePhasesForPrinting();

  // TODO(pdr): Remove this in favor of |UpdateAllLifecyclePhasesExceptPaint|.
  bool UpdateLifecycleToPrePaintClean(DocumentUpdateReason reason);

  // Computes the style, layout, and compositing inputs lifecycle stages if
  // needed. After calling this method, all frames will be in a lifecycle state
  // >= CompositingInputsClean, unless the frame was throttled (if frame
  // throttling, which is not allowed by default, is allowed by the caller) or
  // inactive.
  // Returns whether the lifecycle was successfully updated to the
  // desired state.
  bool UpdateLifecycleToCompositingInputsClean(DocumentUpdateReason reason);

  // Computes only the style and layout lifecycle stages.
  // After calling this method, all frames will be in a lifecycle
  // state >= LayoutClean, unless the frame was throttled (if frame throttling,
  // which is not allowed by default, is allowed by the caller) or inactive.
  // Returns whether the lifecycle was successfully updated to the
  // desired state.
  bool UpdateLifecycleToLayoutClean(DocumentUpdateReason reason);

  void SetTargetStateForTest(DocumentLifecycle::LifecycleState state) {
    target_state_ = state;
  }

  // This for doing work that needs to run synchronously at the end of lifecyle
  // updates, but needs to happen outside of the lifecycle code. It's OK to
  // schedule another animation frame here, but the layout tree should not be
  // invalidated.
  void RunPostLifecycleSteps();
  bool InPostLifecycleSteps() const;

  void ScheduleVisualUpdateForPaintInvalidationIfNeeded();

  // Perform a hit test on the frame with throttling allowed. Normally, a hit
  // test will do a synchronous lifecycle update to kPrePaintClean with
  // throttling disabled. This will do the same lifecycle update, but with
  // throttling enabled.
  HitTestResult HitTestWithThrottlingAllowed(
      const HitTestLocation&,
      HitTestRequest::HitTestRequestType) const;

  void IncrementLayoutObjectCount() { layout_object_counter_.Increment(); }
  void IncrementVisuallyNonEmptyCharacterCount(unsigned);
  void IncrementVisuallyNonEmptyPixelCount(const gfx::Size&);
  bool IsVisuallyNonEmpty() const { return is_visually_non_empty_; }
  void SetIsVisuallyNonEmpty() { is_visually_non_empty_ = true; }
  void EnableAutoSizeMode(const gfx::Size& min_size, const gfx::Size& max_size);
  void DisableAutoSizeMode();

  void ForceLayoutForPagination(const gfx::SizeF& page_size,
                                const gfx::SizeF& original_page_size,
                                float maximum_shrink_factor);

  // Updates the fragment anchor element based on URL's fragment identifier.
  // Updates corresponding ':target' CSS pseudo class on the anchor element.
  // If |should_scroll| is passed it can be used to prevent scrolling/focusing
  // while still performing all related side-effects like setting :target (used
  // for e.g. in history restoration to override the scroll offset). The scroll
  // offset is maintained during the frame loading process.
  void ProcessUrlFragment(const KURL&,
                          bool same_document_navigation,
                          bool should_scroll = true);
  FragmentAnchor* GetFragmentAnchor() { return fragment_anchor_; }
  void InvokeFragmentAnchor();
  void ClearFragmentAnchor();

  bool ShouldSetCursor() const;

  void SetCursor(const ui::Cursor&);

  // FIXME: Remove this method once plugin loading is decoupled from layout.
  void FlushAnyPendingPostLayoutTasks();

  // These methods are for testing.
  void SetTracksRasterInvalidations(bool);
  bool IsTrackingRasterInvalidations() const {
    return is_tracking_raster_invalidations_;
  }

  using ScrollableAreaSet = HeapHashSet<Member<PaintLayerScrollableArea>>;
  void AddScrollAnchoringScrollableArea(PaintLayerScrollableArea*);
  void RemoveScrollAnchoringScrollableArea(PaintLayerScrollableArea*);
  const ScrollableAreaSet* ScrollAnchoringScrollableAreas() const {
    return scroll_anchoring_scrollable_areas_.Get();
  }

  void AddAnimatingScrollableArea(PaintLayerScrollableArea*);
  void RemoveAnimatingScrollableArea(PaintLayerScrollableArea*);
  const ScrollableAreaSet* AnimatingScrollableAreas() const {
    return animating_scrollable_areas_.Get();
  }

  void AddUserScrollableArea(PaintLayerScrollableArea*);
  void RemoveUserScrollableArea(PaintLayerScrollableArea*);
  const ScrollableAreaSet* UserScrollableAreas() const {
    return user_scrollable_areas_.Get();
  }

  void ServiceScriptedAnimations(base::TimeTicks);

  void ScheduleAnimation(base::TimeDelta = base::TimeDelta(),
                         base::Location location = base::Location::Current());

  // FIXME: This should probably be renamed as the 'inSubtreeLayout' parameter
  // passed around the LocalFrameView layout methods can be true while this
  // returns false.
  bool IsSubtreeLayout() const { return !layout_subtree_root_list_.IsEmpty(); }

  // The window that hosts the LocalFrameView. The LocalFrameView will
  // communicate scrolls and repaints to the host window in the window's
  // coordinate space.
  ChromeClient* GetChromeClient() const;

  LocalFrameView* ParentFrameView() const override;
  LayoutEmbeddedContent* GetLayoutEmbeddedContent() const override;
  void AttachToLayout() override;
  void DetachFromLayout() override;
  using PluginSet = HeapHashSet<Member<WebPluginContainerImpl>>;
  const PluginSet& Plugins() const { return plugins_; }
  void AddPlugin(WebPluginContainerImpl*);
  void RemovePlugin(WebPluginContainerImpl*);
  // Custom scrollbars in PaintLayerScrollableArea need to be called with
  // StyleChanged whenever window focus is changed.
  void RemoveScrollbar(Scrollbar*);
  void AddScrollbar(Scrollbar*);

  // Indicates the root layer's scroll offset changed since the last frame
  void SetRootLayerDidScroll() { root_layer_did_scroll_ = true; }

  // Methods for converting between this frame and other coordinate spaces.
  // For definitions and an explanation of the varous spaces, please see:
  // http://www.chromium.org/developers/design-documents/blink-coordinate-spaces
  gfx::Rect ViewportToFrame(const gfx::Rect&) const;
  gfx::Rect FrameToViewport(const gfx::Rect&) const;
  gfx::Point FrameToViewport(const gfx::Point&) const;
  gfx::Point ViewportToFrame(const gfx::Point&) const;
  gfx::PointF ViewportToFrame(const gfx::PointF&) const;
  PhysicalOffset ViewportToFrame(const PhysicalOffset&) const;

  // FIXME: Some external callers expect to get back a rect that's positioned
  // in viewport space, but sized in CSS pixels. This is an artifact of the
  // old pinch-zoom path. These callers should be converted to expect a rect
  // fully in viewport space. crbug.com/459591.
  gfx::Point SoonToBeRemovedUnscaledViewportToContents(const gfx::Point&) const;

  // Functions for converting to screen coordinates.
  gfx::Rect FrameToScreen(const gfx::Rect&) const;

  // Converts from/to local frame coordinates to the root frame coordinates.
  gfx::Rect ConvertToRootFrame(const gfx::Rect&) const;
  gfx::Point ConvertToRootFrame(const gfx::Point&) const;
  PhysicalOffset ConvertToRootFrame(const PhysicalOffset&) const;
  gfx::PointF ConvertToRootFrame(const gfx::PointF&) const;
  PhysicalRect ConvertToRootFrame(const PhysicalRect&) const;
  gfx::Rect ConvertFromRootFrame(const gfx::Rect&) const;
  gfx::Point ConvertFromRootFrame(const gfx::Point&) const;
  gfx::PointF ConvertFromRootFrame(const gfx::PointF&) const;
  PhysicalOffset ConvertFromRootFrame(const PhysicalOffset&) const;

  gfx::Rect RootFrameToDocument(const gfx::Rect&);
  gfx::Point RootFrameToDocument(const gfx::Point&);
  gfx::PointF RootFrameToDocument(const gfx::PointF&);
  gfx::Point DocumentToFrame(const gfx::Point&) const;
  gfx::PointF DocumentToFrame(const gfx::PointF&) const;
  PhysicalOffset DocumentToFrame(const PhysicalOffset&) const;
  gfx::Rect DocumentToFrame(const gfx::Rect&) const;
  PhysicalRect DocumentToFrame(const PhysicalRect&) const;
  gfx::Point FrameToDocument(const gfx::Point&) const;
  PhysicalOffset FrameToDocument(const PhysicalOffset&) const;
  gfx::Rect FrameToDocument(const gfx::Rect&) const;
  PhysicalRect FrameToDocument(const PhysicalRect&) const;

  // Normally a LocalFrameView synchronously paints during full lifecycle
  // updates, into the local frame root's PaintController. However, in some
  // cases (e.g. when printing) we need to paint the frame view into a
  // PaintController other than the default one(s). The following functions are
  // provided for these cases. This frame view must be in PrePaintClean or
  // PaintClean state when these functions are called.
  // AllowThrottlingScope is used to allow frame throttling.
  void PaintOutsideOfLifecycleWithThrottlingAllowed(
      GraphicsContext&,
      PaintFlags,
      const CullRect& cull_rect = CullRect::Infinite());

  // Same as the above, but frame throttling is not allowed by default. If this
  // function is called without frame throttling allowed, the caller must have
  // just updated the document lifecycle to PrePaintClean or PaintClean without
  // frame throttling allowed.
  void PaintOutsideOfLifecycle(
      GraphicsContext&,
      PaintFlags,
      const CullRect& cull_rect = CullRect::Infinite());

  // For testing paint with a custom cull rect.
  void PaintForTest(const CullRect&);

  // Get the PaintRecord based on the cached paint artifact generated during
  // the last paint in lifecycle update.
  sk_sp<cc::PaintRecord> GetPaintRecord() const;

  void Show() override;
  void Hide() override;

  bool IsLocalFrameView() const override { return true; }
  bool ShouldReportMainFrameIntersection() const override { return true; }

  void Trace(Visitor*) const override;
  void NotifyPageThatContentAreaWillPaint() const;

  // Returns the scrollable area for the frame. For the root frame, this will
  // be the RootFrameViewport, which adds pinch-zoom semantics to scrolling.
  // For non-root frames, this will be the ScrollableArea of the LayoutView.
  ScrollableArea* GetScrollableArea();

  // Returns the ScrollableArea of the LayoutView, a.k.a. the layout viewport.
  // In the root frame, this is the "outer" viewport in the pinch-zoom dual
  // viewport model.  Callers that need awareness of both inner and outer
  // viewports should use GetScrollableArea() instead.
  PaintLayerScrollableArea* LayoutViewport() const;

  // If this is the main frame, this will return the RootFrameViewport used
  // to scroll the main frame. Otherwise returns nullptr. Unless you need a
  // unique method on RootFrameViewport, you should probably use
  // getScrollableArea.
  RootFrameViewport* GetRootFrameViewport();

  int ViewportWidth() const;

  int ViewportHeight() const;

  bool LocalFrameTreeAllowsThrottling() const;
  bool LocalFrameTreeForcesThrottling() const;

  // Returns true if this frame should not render or schedule visual updates.
  bool ShouldThrottleRendering() const;

  // Same as ShouldThrottleRendering, but with an AllowThrottlingScope in scope.
  bool ShouldThrottleRenderingForTest() const;

  bool CanThrottleRendering() const override;
  void UpdateRenderThrottlingStatus(bool hidden_for_throttling,
                                    bool subtree_throttled,
                                    bool display_locked,
                                    bool recurse = false) override;

  void BeginLifecycleUpdates();

  // Records a timestamp in PaintTiming when the frame is first not
  // render-throttled (since it last was throttled if applicable).
  void MarkFirstEligibleToPaint();

  // Resets the optional timestamp in PaintTiming to null to indicate
  // that the frame is now render-throttled, unless the frame already has
  // a first contentful paint. This is a necessary workaround, as when
  // constructing the frame, HTMLConstructionSite::InsertHTMLBodyElement
  // initiates a call via Document::WillInsertBody to begin lifecycle
  // updates, and hence |lifecycle_updates_throttled_| is set to false, which
  // can cause the frame to be briefly unthrottled and receive a paint
  // eligibility timestamp, even if the frame is throttled shortly thereafter
  // and not actually painted.
  void MarkIneligibleToPaint();

  // Shorthands of LayoutView's corresponding methods.
  void SetNeedsPaintPropertyUpdate();

  // Viewport size that should be used for viewport units (i.e. 'vh'/'vw').
  // May include the size of browser controls. See implementation for further
  // documentation.
  gfx::SizeF ViewportSizeForViewportUnits() const;
  // https://drafts.csswg.org/css-values-4/#small-viewport-size
  gfx::SizeF SmallViewportSizeForViewportUnits() const;
  // https://drafts.csswg.org/css-values-4/#large-viewport-size
  gfx::SizeF LargeViewportSizeForViewportUnits() const;
  // https://drafts.csswg.org/css-values-4/#dynamic-viewport-size
  gfx::SizeF DynamicViewportSizeForViewportUnits() const;

  // Initial containing block size for evaluating viewport-dependent media
  // queries.
  gfx::SizeF ViewportSizeForMediaQueries() const;

  void EnqueueScrollAnchoringAdjustment(ScrollableArea*);
  void DequeueScrollAnchoringAdjustment(ScrollableArea*);
  void PerformScrollAnchoringAdjustments();

  void SetNeedsEnqueueScrollEvent(PaintLayerScrollableArea*);

  std::unique_ptr<JSONObject> CompositedLayersAsJSON(LayerTreeFlags);

  String MainThreadScrollingReasonsAsText();
  // Main thread scrolling reasons including reasons from ancestors.
  MainThreadScrollingReasons GetMainThreadScrollingReasons() const;
  // Main thread scrolling reasons for this object only. For all reasons,
  // see: mainThreadScrollingReasons().
  MainThreadScrollingReasons MainThreadScrollingReasonsPerFrame() const;

  bool MapToVisualRectInRemoteRootFrame(PhysicalRect& rect,
                                        bool apply_overflow_clip = true);

  void MapLocalToRemoteMainFrame(TransformState&);

  void CrossOriginToNearestMainFrameChanged();
  void CrossOriginToParentFrameChanged();

  void SetVisualViewportOrOverlayNeedsRepaint();
  bool VisualViewportOrOverlayNeedsRepaintForTesting() const;

  LayoutUnit CaretWidth() const;

  size_t PaintFrameCount() const { return paint_frame_count_; }

  // Return the ScrollableArea in a FrameView with the given ElementId, if any.
  // This is not recursive and will only return ScrollableAreas owned by this
  // LocalFrameView (or possibly the LocalFrameView itself).
  ScrollableArea* ScrollableAreaWithElementId(const CompositorElementId&);

  // When the frame is a local root and not a main frame, any recursive
  // scrolling should continue in the parent process.
  void ScrollRectToVisibleInRemoteParent(const PhysicalRect&,
                                         mojom::blink::ScrollIntoViewParamsPtr);

  PaintArtifactCompositor* GetPaintArtifactCompositor() const;

  cc::Layer* RootCcLayer();
  const cc::Layer* RootCcLayer() const;

  cc::AnimationHost* GetCompositorAnimationHost() const;
  cc::AnimationTimeline* GetScrollAnimationTimeline() const;

  LayoutShiftTracker& GetLayoutShiftTracker() { return *layout_shift_tracker_; }
  PaintTimingDetector& GetPaintTimingDetector() const {
    return *paint_timing_detector_;
  }

  MobileFriendlinessChecker* GetMobileFriendlinessChecker() const {
    return mobile_friendliness_checker_;
  }
  void DidChangeMobileFriendliness(const MobileFriendliness& mf);

  // Returns the UKM aggregator for this frame's local root, creating it if
  // necessary.
  LocalFrameUkmAggregator& EnsureUkmAggregator();
  void ResetUkmAggregatorForTesting();

  // Report the First Contentful Paint signal to the LocalFrameView.
  // This causes Deferred Commits to be restarted and tells the UKM
  // aggregator that FCP has been reached.
  void OnFirstContentfulPaint();

#if DCHECK_IS_ON()
  void SetIsUpdatingDescendantDependentFlags(bool val) {
    is_updating_descendant_dependent_flags_ = val;
  }
  bool IsUpdatingDescendantDependentFlags() const {
    return is_updating_descendant_dependent_flags_;
  }
#endif

  void RegisterForLifecycleNotifications(LifecycleNotificationObserver*);
  void UnregisterFromLifecycleNotifications(LifecycleNotificationObserver*);

  // Enqueue tasks to be run at the start of the next lifecycle. These tasks
  // will run right after `WillStartLifecycleUpdate()` on the lifecycle
  // notification observers.
  void EnqueueStartOfLifecycleTask(base::OnceClosure);

  // For testing way to steal the start-of-lifecycle tasks.
  WTF::Vector<base::OnceClosure> TakeStartOfLifecycleTasksForTest() {
    return std::move(start_of_lifecycle_tasks_);
  }

  // Called when the "dominant visible" status has changed for a
  // HTMLVideoElement in the page. "dominant visible" means the element is
  // mostly filling the viewport.
  void NotifyVideoIsDominantVisibleStatus(HTMLVideoElement* element,
                                          bool is_dominant);

  bool HasDominantVideoElement() const;

  // Gets the xr overlay layer if present, or nullptr if there is none.
  PaintLayer* GetXROverlayLayer() const;

  void PropagateCullRectNeedsUpdateForFrames();

  void RunPaintBenchmark(int repeat_count, cc::PaintBenchmarkResult& result);

  PaintController& GetPaintControllerForTesting() {
    return EnsurePaintController();
  }

  bool PaintDebugInfoEnabled() const {
    return layer_debug_info_enabled_ ||
           RuntimeEnabledFeatures::PaintUnderInvalidationCheckingEnabled();
  }

  void AddPendingTransformUpdate(LayoutObject& object);
  bool RemovePendingTransformUpdate(const LayoutObject& object);
  void UpdateAllPendingTransforms();

 protected:
  void FrameRectsChanged(const gfx::Rect&) override;
  void SelfVisibleChanged() override;
  void ParentVisibleChanged() override;
  void NotifyFrameRectsChangedIfNeeded();
  void SetViewportIntersection(const mojom::blink::ViewportIntersectionState&
                                   intersection_state) override {}
  void VisibilityForThrottlingChanged() override;
  bool LifecycleUpdatesThrottled() const override {
    return lifecycle_updates_throttled_;
  }
  void VisibilityChanged(blink::mojom::FrameVisibility visibility) override;

  void EnqueueScrollEvents();

 private:
  LocalFrameView(LocalFrame&, gfx::Rect);

#if DCHECK_IS_ON()
  class DisallowLayoutInvalidationScope {
    STACK_ALLOCATED();

   public:
    explicit DisallowLayoutInvalidationScope(LocalFrameView* view);
    ~DisallowLayoutInvalidationScope();

   private:
    LocalFrameView* local_frame_view_;
  };
#endif

  // Throttling is disabled by default. Instantiating this class allows
  // throttling (e.g., during BeginMainFrame). If a script needs to run inside
  // this scope, DisallowThrottlingScope should be used to let the script
  // perform a synchronous layout if necessary.
  class CORE_EXPORT AllowThrottlingScope {
    STACK_ALLOCATED();

   public:
    explicit AllowThrottlingScope(const LocalFrameView&);
    AllowThrottlingScope(const AllowThrottlingScope&) = delete;
    AllowThrottlingScope& operator=(const AllowThrottlingScope&) = delete;
    ~AllowThrottlingScope() = default;

   private:
    base::AutoReset<bool> value_;
  };

  class DisallowThrottlingScope {
    STACK_ALLOCATED();

   public:
    explicit DisallowThrottlingScope(const LocalFrameView& frame_view);
    DisallowThrottlingScope(const DisallowThrottlingScope&) = delete;
    DisallowThrottlingScope& operator=(const DisallowThrottlingScope&) = delete;
    ~DisallowThrottlingScope() = default;

   private:
    base::AutoReset<bool> value_;
  };

  // The logic to determine whether a view can be render throttled is delicate,
  // but in some cases we want to unconditionally force all views in a local
  // frame tree to be throttled. Having ForceThrottlingScope on the stack will
  // do that; it supercedes any DisallowThrottlingScope on the stack.
  class ForceThrottlingScope {
    STACK_ALLOCATED();

   public:
    explicit ForceThrottlingScope(const LocalFrameView& frame_view);
    ForceThrottlingScope(const ForceThrottlingScope&) = delete;
    ForceThrottlingScope& operator=(const ForceThrottlingScope&) = delete;
    ~ForceThrottlingScope() = default;

   private:
    AllowThrottlingScope allow_scope_;
    base::AutoReset<bool> value_;
  };
  friend class AllowThrottlingScope;
  friend class DisallowThrottlingScope;
  friend class ForceThrottlingScope;

  PaintController& EnsurePaintController();

  // A paint preview is a copy of the visual contents of a webpage recorded as
  // a set of SkPictures. This sends an IPC to the browser to trigger a
  // recording of this frame as a separate SkPicture. An ID is added to the
  // canvas of |context| at |paint_offset| to track the correct position of
  // this frame relative to its parent. Returns true on successfully creating
  // a placeholder and sending an IPC to the browser.
  bool CapturePaintPreview(GraphicsContext& context,
                           const gfx::Vector2d& paint_offset) const;

  // EmbeddedContentView implementation
  void Paint(GraphicsContext&,
             PaintFlags,
             const CullRect&,
             const gfx::Vector2d&) const final;

  void PaintFrame(GraphicsContext&, PaintFlags = PaintFlag::kNoFlag) const;

  LayoutSVGRoot* EmbeddedReplacedContent() const;

  void PrepareForLifecycleUpdateRecursive();

  // Returns whether the lifecycle was successfully updated to the
  // target state.
  bool UpdateLifecyclePhases(DocumentLifecycle::LifecycleState target_state,
                             DocumentUpdateReason reason);
  // The internal version that does the work after the proper context and checks
  // have passed in the above function call.
  void UpdateLifecyclePhasesInternal(
      DocumentLifecycle::LifecycleState target_state);
  // Four lifecycle phases helper functions corresponding to StyleAndLayout,
  // Compositing, PrePaint, and Paint phases. If the return value is true, it
  // means further lifecycle phases need to be run. This is used to abort
  // earlier if we don't need to run future lifecycle phases.
  bool RunStyleAndLayoutLifecyclePhases(
      DocumentLifecycle::LifecycleState target_state);
  bool RunAccessibilityLifecyclePhase(
      DocumentLifecycle::LifecycleState target_state);
  bool RunCompositingInputsLifecyclePhase(
      DocumentLifecycle::LifecycleState target_state);
  bool RunPrePaintLifecyclePhase(
      DocumentLifecycle::LifecycleState target_state);
  void RunPaintLifecyclePhase(PaintBenchmarkMode);

  void UpdateStyleAndLayoutIfNeededRecursive();
  bool UpdateStyleAndLayoutInternal();
  void UpdateLayout();
  void PerformLayout();
  void PerformPostLayoutTasks(bool view_size_changed);

  bool PaintTree(PaintBenchmarkMode, PaintControllerCycleScope&);
  void PushPaintArtifactToCompositor(bool repainted);
  void CreatePaintTimelineEvents();

  void ClearLayoutSubtreeRootsAndMarkContainingBlocks();

  DocumentLifecycle& Lifecycle() const;

  void RunIntersectionObserverSteps();
  void RenderThrottlingStatusChanged();

  // Methods to do point conversion via layoutObjects, in order to take
  // transforms into account.
  gfx::Rect ConvertToContainingEmbeddedContentView(const gfx::Rect&) const;
  gfx::Point ConvertToContainingEmbeddedContentView(const gfx::Point&) const;
  PhysicalOffset ConvertToContainingEmbeddedContentView(
      const PhysicalOffset&) const;
  gfx::PointF ConvertToContainingEmbeddedContentView(const gfx::PointF&) const;
  gfx::Rect ConvertFromContainingEmbeddedContentView(const gfx::Rect&) const;
  gfx::Point ConvertFromContainingEmbeddedContentView(const gfx::Point&) const;
  PhysicalOffset ConvertFromContainingEmbeddedContentView(
      const PhysicalOffset&) const;
  gfx::PointF ConvertFromContainingEmbeddedContentView(
      const gfx::PointF&) const;

  void UpdateGeometriesIfNeeded();

  void ScheduleUpdatePluginsIfNecessary();
  void UpdatePluginsTimerFired(TimerBase*);
  bool UpdatePlugins();

  AXObjectCache* ExistingAXObjectCache() const;

  void SetLayoutSizeInternal(const gfx::Size&);

  ScrollingCoordinator* GetScrollingCoordinator() const;

  void CollectAnnotatedRegions(LayoutObject&,
                               Vector<AnnotatedRegionValue>&) const;

  void ForAllChildViewsAndPlugins(
      base::FunctionRef<void(EmbeddedContentView&)>);
  void ForAllChildLocalFrameViews(base::FunctionRef<void(LocalFrameView&)>);

  enum TraversalOrder { kPreOrder, kPostOrder };
  void ForAllNonThrottledLocalFrameViews(
      base::FunctionRef<void(LocalFrameView&)>,
      TraversalOrder = kPreOrder);
  void ForAllThrottledLocalFrameViews(base::FunctionRef<void(LocalFrameView&)>);

  void ForAllRemoteFrameViews(base::FunctionRef<void(RemoteFrameView&)>);

  bool UpdateViewportIntersectionsForSubtree(
      unsigned parent_flags,
      absl::optional<base::TimeTicks>& monotonic_time) override;
  void DeliverSynchronousIntersectionObservations();

  bool RunScrollTimelineSteps();

  bool RunCSSToggleSteps();

  bool NotifyResizeObservers(DocumentLifecycle::LifecycleState target_state);
  bool RunResizeObserverSteps(DocumentLifecycle::LifecycleState target_state);
  void ClearResizeObserverLimit();

  bool RunDocumentTransitionSteps(
      DocumentLifecycle::LifecycleState target_state);

  bool CheckLayoutInvalidationIsAllowed() const;

  // This runs the intersection observer steps for observations that need to
  // happen in post-layout. These results are also delivered (if needed) in the
  // same call. Returns true if the lifecycle should process style and layout
  // again before proceeding.
  bool RunPostLayoutIntersectionObserverSteps();
  // This is a recursive helper for determining intersection observations which
  // need to happen in post-layout.
  void ComputePostLayoutIntersections(
      unsigned parent_flags,
      absl::optional<base::TimeTicks>& monotonic_time);

  // Returns true if the root object was laid out. Returns false if the layout
  // was prevented (e.g. by ancestor display-lock) or not needed.
  bool LayoutFromRootObject(LayoutObject& root);

  // Returns true if the value of layer_debug_info_enabled_ changed.
  bool UpdateLayerDebugInfoEnabled();

  // Return the interstitial-ad detector for this frame, creating it if
  // necessary.
  OverlayInterstitialAdDetector& EnsureOverlayInterstitialAdDetector();

  void GetUserScrollTranslationNodes(
      Vector<const TransformPaintPropertyNode*>& scroll_translation_nodes);

  // Return the sticky-ad detector for this frame, creating it if necessary.
  StickyAdDetector& EnsureStickyAdDetector();

  // Returns true if we should paint the color adjust background from the
  // StyleEngine instead of the base background color.
  bool ShouldUseColorAdjustBackground() const;

  // Verifies the shared elements for the document transition on this view.
  void VerifySharedElementsForDocumentTransition();
  // Append document transition requests from this view into the given vector.
  void AppendDocumentTransitionRequests(
      WTF::Vector<std::unique_ptr<DocumentTransitionRequest>>&);

  bool AnyFrameIsPrintingOrPaintingPreview();

  DarkModeFilter& EnsureDarkModeFilter();

  LayoutSize size_;

  typedef HeapHashSet<Member<LayoutEmbeddedObject>> EmbeddedObjectSet;
  EmbeddedObjectSet part_update_set_;

  Member<LocalFrame> frame_;

  bool can_have_scrollbars_;
  bool in_post_lifecycle_steps_ = false;

  bool has_pending_layout_;
  LayoutSubtreeRootList layout_subtree_root_list_;
  DepthOrderedLayoutObjectList orthogonal_writing_mode_root_list_;

  bool layout_scheduling_enabled_;
  unsigned layout_count_for_testing_;
  unsigned lifecycle_update_count_for_testing_;
  HeapTaskRunnerTimer<LocalFrameView> update_plugins_timer_;

  bool first_layout_;
  UseColorAdjustBackground use_color_adjust_background_{
      UseColorAdjustBackground::kNo};
  Color base_background_color_;

  // Used for tracking the frame's size and replicating it to the browser
  // process when it changes.
  absl::optional<gfx::Size> frame_size_;

  AtomicString media_type_;
  AtomicString media_type_when_not_printing_;

  unsigned visually_non_empty_character_count_;
  uint64_t visually_non_empty_pixel_count_;
  bool is_visually_non_empty_;
  LayoutObjectCounter layout_object_counter_;

  Member<FragmentAnchor> fragment_anchor_;

  // Scrollable areas which overflow in the block flow direction.
  // Needed for calculating scroll anchoring.
  Member<ScrollableAreaSet> scroll_anchoring_scrollable_areas_;
  Member<ScrollableAreaSet> animating_scrollable_areas_;
  // Scrollable areas which are user-scrollable, whether they overflow or not.
  Member<ScrollableAreaSet> user_scrollable_areas_;
  Member<ObjectSet> fixed_position_objects_;
  ObjectSet background_attachment_fixed_objects_;
  Member<FrameViewAutoSizeInfo> auto_size_info_;

  gfx::Size layout_size_;
  bool layout_size_fixed_to_frame_size_;

  bool needs_update_geometries_;

#if DCHECK_IS_ON()
  // Verified when finalizing.
  bool has_been_disposed_ = false;
#endif

  PluginSet plugins_;
  HeapHashSet<Member<Scrollbar>> scrollbars_;

  // TODO(bokan): This is unneeded when root-layer-scrolls is turned on.
  // crbug.com/417782.
  gfx::Size layout_overflow_size_;

  bool root_layer_did_scroll_;

  // Mark if something has changed in the mapping from Frame to GraphicsLayer
  // and the Frame Timing regions should be recalculated.
  bool frame_timing_requests_dirty_;

  // Exists only on root frame.
  Member<RootFrameViewport> viewport_scrollable_area_;

  // Non-top-level frames a throttled until they are ready to run lifecycle
  // updates (after render-blocking resources have loaded).
  bool lifecycle_updates_throttled_;

  // Used by AllowThrottlingScope and DisallowThrottlingScope
  bool allow_throttling_ = false;
  // Used by ForceThrottlingScope
  bool force_throttling_ = false;

  // This is set on the local root frame view only.
  DocumentLifecycle::LifecycleState target_state_;

  using AnchoringAdjustmentQueue =
      HeapLinkedHashSet<WeakMember<ScrollableArea>>;
  AnchoringAdjustmentQueue anchoring_adjustment_queue_;

  HeapLinkedHashSet<WeakMember<PaintLayerScrollableArea>> scroll_event_queue_;

  bool suppress_adjust_view_size_;
#if DCHECK_IS_ON()
  // In DCHECK on builds, this is set to false when we're running lifecycle
  // phases past layout to ensure that phases after layout don't dirty layout.
  bool allows_layout_invalidation_after_layout_clean_ = true;
#endif
  IntersectionObservationState intersection_observation_state_;

  bool needs_focus_on_fragment_;

  // True if the frame has deferred commits at least once per document load.
  // We won't defer again for the same document. This is only meaningful for
  // main frames.
  bool have_deferred_main_frame_commits_ = false;

  bool visual_viewport_or_overlay_needs_repaint_ = false;

  // Whether to collect layer debug information for debugging, tracing,
  // inspection, etc. Applies to local root only.
  bool layer_debug_info_enabled_ = DCHECK_IS_ON();

  LifecycleData lifecycle_data_;

  // For testing.
  bool is_tracking_raster_invalidations_ = false;

  // Used by |PaintTree()| to collect the updated |PaintArtifact| which will be
  // passed to the compositor. It caches display items and subsequences across
  // frame updates and repaints.
  std::unique_ptr<PaintController> paint_controller_;
  std::unique_ptr<PaintArtifactCompositor> paint_artifact_compositor_;

  MainThreadScrollingReasons main_thread_scrolling_reasons_;

  scoped_refptr<LocalFrameUkmAggregator> ukm_aggregator_;
  unsigned forced_layout_stack_depth_;
  base::TimeTicks forced_layout_start_time_;

  // From the beginning of the document, how many frames have painted.
  size_t paint_frame_count_;

  UniqueObjectId unique_id_;
  Member<LayoutShiftTracker> layout_shift_tracker_;
  Member<PaintTimingDetector> paint_timing_detector_;

  // Non-null in the outermost main frame of an ordinary page only.
  Member<MobileFriendlinessChecker> mobile_friendliness_checker_;

  HeapHashSet<WeakMember<LifecycleNotificationObserver>> lifecycle_observers_;

  HeapHashSet<WeakMember<HTMLVideoElement>> fullscreen_video_elements_;

  std::unique_ptr<OverlayInterstitialAdDetector>
      overlay_interstitial_ad_detector_;

  std::unique_ptr<StickyAdDetector> sticky_ad_detector_;

  // These tasks will be run at the beginning of the next lifecycle.
  WTF::Vector<base::OnceClosure> start_of_lifecycle_tasks_;

  // Filter used for inverting the document background for forced darkening.
  std::unique_ptr<DarkModeFilter> dark_mode_filter_;

  Member<HeapHashSet<Member<LayoutObject>>> pending_transform_updates_;

#if DCHECK_IS_ON()
  bool is_updating_descendant_dependent_flags_;
  bool is_updating_layout_;
#endif

  FRIEND_TEST_ALL_PREFIXES(FrameThrottlingTest, ForAllThrottledLocalFrameViews);
};

inline void LocalFrameView::IncrementVisuallyNonEmptyCharacterCount(
    unsigned count) {
  if (is_visually_non_empty_)
    return;
  visually_non_empty_character_count_ += count;
  // Use a threshold value to prevent very small amounts of visible content from
  // triggering didMeaningfulLayout.  The first few hundred characters rarely
  // contain the interesting content of the page.
  static const unsigned kVisualCharacterThreshold = 200;
  if (visually_non_empty_character_count_ > kVisualCharacterThreshold)
    SetIsVisuallyNonEmpty();
}

inline void LocalFrameView::IncrementVisuallyNonEmptyPixelCount(
    const gfx::Size& size) {
  if (is_visually_non_empty_)
    return;
  visually_non_empty_pixel_count_ += size.Area64();
  // Use a threshold value to prevent very small amounts of visible content from
  // triggering didMeaningfulLayout.
  static const unsigned kVisualPixelThreshold = 32 * 32;
  if (visually_non_empty_pixel_count_ > kVisualPixelThreshold)
    SetIsVisuallyNonEmpty();
}

template <>
struct DowncastTraits<LocalFrameView> {
  static bool AllowFrom(const EmbeddedContentView& embedded_content_view) {
    return embedded_content_view.IsLocalFrameView();
  }
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_CORE_FRAME_LOCAL_FRAME_VIEW_H_
