/*
 * Copyright (C) 2003, 2009, 2012 Apple Inc. All rights reserved.
 * Copyright (C) 2013 Intel Corporation. All rights reserved.
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

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_PAINT_PAINT_LAYER_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_PAINT_PAINT_LAYER_H_

#include <memory>

#include "base/dcheck_is_on.h"
#include "base/gtest_prod_util.h"
#include "third_party/blink/renderer/core/core_export.h"
#include "third_party/blink/renderer/core/layout/layout_box.h"
#include "third_party/blink/renderer/core/layout/ng/geometry/ng_static_position.h"
#include "third_party/blink/renderer/core/paint/paint_layer_clipper.h"
#include "third_party/blink/renderer/core/paint/paint_layer_fragment.h"
#include "third_party/blink/renderer/core/paint/paint_layer_resource_info.h"
#include "third_party/blink/renderer/core/paint/paint_layer_stacking_node.h"
#include "third_party/blink/renderer/core/paint/paint_result.h"
#include "third_party/blink/renderer/platform/graphics/overlay_scrollbar_clip_behavior.h"
#include "third_party/blink/renderer/platform/wtf/allocator/allocator.h"

namespace blink {

class CompositorFilterOperations;
class ComputedStyle;
class FilterEffect;
class FilterOperations;
class HitTestResult;
class HitTestingTransformState;
class PaintLayerScrollableArea;
class TransformationMatrix;

enum IncludeSelfOrNot { kIncludeSelf, kExcludeSelf };

// Used in PaintLayerPaintOrderIterator.
enum PaintLayerIteration {
  kNegativeZOrderChildren = 1,
  // Normal flow children are not mandated by CSS 2.1 but are an artifact of
  // our implementation: we allocate PaintLayers for elements that
  // are not treated as stacking contexts and thus we need to walk them
  // during painting and hit-testing.
  kNormalFlowChildren = 1 << 1,
  kPositiveZOrderChildren = 1 << 2,

  kStackedChildren = kNegativeZOrderChildren | kPositiveZOrderChildren,
  kNormalFlowAndPositiveZOrderChildren =
      kNormalFlowChildren | kPositiveZOrderChildren,
  kAllChildren =
      kNegativeZOrderChildren | kNormalFlowChildren | kPositiveZOrderChildren
};

struct CORE_EXPORT PaintLayerRareData final
    : public GarbageCollected<PaintLayerRareData> {
 public:
  PaintLayerRareData();
  PaintLayerRareData(const PaintLayerRareData&) = delete;
  PaintLayerRareData& operator=(const PaintLayerRareData&) = delete;
  ~PaintLayerRareData();

  void Trace(Visitor* visitor) const;

  // The offset for an in-flow relative-positioned PaintLayer. This is not
  // set by any other style.
  PhysicalOffset offset_for_in_flow_rel_position;

  std::unique_ptr<TransformationMatrix> transform;

  // Pointer to the enclosing Layer that caused us to be paginated. It is 0 if
  // we are not paginated.
  //
  // See LayoutMultiColumnFlowThread and
  // https://sites.google.com/a/chromium.org/dev/developers/design-documents/multi-column-layout
  // for more information about the multicol implementation. It's important to
  // understand the difference between flow thread coordinates and visual
  // coordinates when working with multicol in Layer, since Layer is one of the
  // few places where we have to worry about the visual ones. Internally we try
  // to use flow-thread coordinates whenever possible.
  Member<PaintLayer> enclosing_pagination_layer;

  Member<PaintLayerResourceInfo> resource_info;
};

// PaintLayer is an old object that handles lots of unrelated operations.
//
// We want it to die at some point and be replaced by more focused objects,
// which would remove (or at least compartmentalize) a lot of complexity.
// See the STATUS OF PAINTLAYER section below.
//
// The class is central to painting and hit-testing. That's because it handles
// a lot of tasks (we included ones done by associated satellite objects for
// historical reasons):
// - Complex painting operations (opacity, clipping, filters, reflections, ...).
// - hardware acceleration (through PaintLayerCompositor).
// - scrolling (through PaintLayerScrollableArea).
// - some performance optimizations.
//
// The compositing code is also based on PaintLayer. The entry to it is the
// PaintLayerCompositor, which fills |composited_layer_mapping| for hardware
// accelerated layers.
//
// TODO(jchaffraix): Expand the documentation about hardware acceleration.
//
//
// ***** SELF-PAINTING LAYER *****
// One important concept about PaintLayer is "self-painting"
// (is_self_painting_layer_).
// PaintLayer started as the implementation of a stacking context. This meant
// that we had to use PaintLayer's painting order (the code is now in
// PaintLayerPainter and PaintLayerStackingNode) instead of the LayoutObject's
// children order. Over the years, as more operations were handled by
// PaintLayer, some LayoutObjects that were not stacking context needed to have
// a PaintLayer for bookkeeping reasons. One such example is the overflow hidden
// case that wanted hardware acceleration and thus had to allocate a PaintLayer
// to get it. However overflow hidden is something LayoutObject can paint
// without a PaintLayer, which includes a lot of painting overhead. Thus the
// self-painting flag was introduced. The flag is a band-aid solution done for
// performance reason only. It just brush over the underlying problem, which is
// that its design doesn't match the system's requirements anymore.
//
// Note that the self-painting flag determines how we paint a LayoutObject:
// - If the flag is true, the LayoutObject is painted through its PaintLayer,
//   which is required to apply complex paint operations. The paint order is
//   handled by PaintLayerPainter::paintChildren, where we look at children
//   PaintLayers.
// - If the flag is false, the LayoutObject is painted like normal children (ie
//   as if it didn't have a PaintLayer). The paint order is handled by
//   BlockPainter::paintChild that looks at children LayoutObjects.
// This means that the self-painting flag changes the painting order in a subtle
// way, which can potentially have visible consequences. Those bugs are called
// painting inversion as we invert the order of painting for 2 elements
// (painting one wrongly in front of the other).
// See https://crbug.com/370604 for an example.
//
//
// ***** STATUS OF PAINTLAYER *****
// We would like to remove this class in the future. The reasons for the removal
// are:
// - it has been a dumping ground for features for too long.
// - it is the wrong level of abstraction, bearing no correspondence to any CSS
//   concept.
//
// Its features need to be migrated to helper objects. This was started with the
// introduction of satellite objects: PaintLayer*. Those helper objects then
// need to be moved to the appropriate LayoutObject class, probably to a rare
// data field to avoid growing all the LayoutObjects.
//
// A good example of this is PaintLayerScrollableArea, which can only happen
// be instanciated for LayoutBoxes. With the current design, it's hard to know
// that by reading the code.
class CORE_EXPORT PaintLayer : public GarbageCollected<PaintLayer>,
                               public DisplayItemClient {
 public:
  explicit PaintLayer(LayoutBoxModelObject*);
  PaintLayer(const PaintLayer&) = delete;
  PaintLayer& operator=(const PaintLayer&) = delete;
  ~PaintLayer() override;

  void Destroy();

  // DisplayItemClient methods
  String DebugName() const final;
  DOMNodeId OwnerNodeId() const final;

  LayoutBoxModelObject& GetLayoutObject() const { return *layout_object_; }
  LayoutBox* GetLayoutBox() const {
    return DynamicTo<LayoutBox>(layout_object_.Get());
  }
  // Returns |GetLayoutBox()| if it exists and has fragments.
  const LayoutBox* GetLayoutBoxWithBlockFragments() const;

  PaintLayer* Parent() const { return parent_; }
  PaintLayer* PreviousSibling() const { return previous_; }
  PaintLayer* NextSibling() const { return next_; }
  PaintLayer* FirstChild() const { return first_; }
  PaintLayer* LastChild() const { return last_; }

  // TODO(wangxianzhu): Find a better name for it. 'paintContainer' might be
  // good but we can't use it for now because it conflicts with
  // PaintInfo::paintContainer.
  PaintLayer* CompositingContainer() const;
  PaintLayer* AncestorStackingContext() const;

  void AddChild(PaintLayer* new_child, PaintLayer* before_child = nullptr);
  void RemoveChild(PaintLayer*);

  void RemoveOnlyThisLayerAfterStyleChange(const ComputedStyle* old_style);
  void InsertOnlyThisLayerAfterStyleChange();

  void StyleDidChange(StyleDifference, const ComputedStyle* old_style);

  // FIXME: Many people call this function while it has out-of-date information.
  bool IsSelfPaintingLayer() const { return is_self_painting_layer_; }

  bool IsTransparent() const {
    return GetLayoutObject().StyleRef().HasOpacity() ||
           GetLayoutObject().StyleRef().HasBlendMode() ||
           GetLayoutObject().HasMask();
  }

  const PaintLayer* Root() const {
    const PaintLayer* curr = this;
    while (curr->Parent())
      curr = curr->Parent();
    return curr;
  }

  // The physical offset from this PaintLayer to its ContainingLayer.
  // Does not include any scroll offset of the ContainingLayer. Also does not
  // include offsets for positioned elements.
  const PhysicalOffset& LocationWithoutPositionOffset() const {
#if DCHECK_IS_ON()
    DCHECK(!needs_position_update_);
#endif
    return location_without_position_offset_;
  }

  // This is the scroll offset that's actually used to display to the screen.
  // It should only be used in paint/compositing type use cases (includes hit
  // testing, intersection observer). Most other cases should use the unsnapped
  // offset from LayoutBox (for layout) or the source offset from the
  // ScrollableArea.
  gfx::Vector2d PixelSnappedScrolledContentOffset() const;

  // FIXME: size() should DCHECK(!needs_position_update_) as well, but that
  // fails in some tests, for example, fast/repaint/clipped-relative.html.
  const LayoutSize& Size() const { return size_; }

#if DCHECK_IS_ON()
  bool NeedsPositionUpdate() const { return needs_position_update_; }
#endif

  bool IsRootLayer() const { return is_root_layer_; }

  bool UpdateSize();
  void UpdateSizeAndScrollingAfterLayout();

  void UpdateLayerPositionsAfterLayout();

  PaintLayer* EnclosingPaginationLayer() const {
    return rare_data_ ? rare_data_->enclosing_pagination_layer : nullptr;
  }

  void UpdateTransformationMatrix();

  bool HasVisibleContent() const {
    DCHECK(!needs_descendant_dependent_flags_update_);
    return has_visible_content_;
  }

  bool HasVisibleSelfPaintingDescendant() const {
    DCHECK(!needs_descendant_dependent_flags_update_);
    return has_visible_self_painting_descendant_;
  }

  void DirtyVisibleContentStatus();

  // Gets the ancestor layer that serves as the containing block (in the sense
  // of LayoutObject::container() instead of LayoutObject::containingBlock())
  // of this layer. Normally the parent layer is the containing layer, except
  // for out of flow positioned, floating and multicol spanner layers whose
  // containing layer might be an ancestor of the parent layer.
  // If |ancestor| is specified, |*skippedAncestor| will be set to true if
  // |ancestor| is found in the ancestry chain between this layer and the
  // containing block layer; if not found, it will be set to false. Either both
  // |ancestor| and |skippedAncestor| should be nullptr, or none of them should.
  PaintLayer* ContainingLayer(const PaintLayer* ancestor = nullptr,
                              bool* skipped_ancestor = nullptr) const;

  void ConvertToLayerCoords(const PaintLayer* ancestor_layer,
                            PhysicalOffset&) const;
  void ConvertToLayerCoords(const PaintLayer* ancestor_layer,
                            PhysicalRect&) const;

  // Does the same as convertToLayerCoords() when not in multicol. For multicol,
  // however, convertToLayerCoords() calculates the offset in flow-thread
  // coordinates (what the layout engine uses internally), while this method
  // calculates the visual coordinates; i.e. it figures out which column the
  // layer starts in and adds in the offset. See
  // http://www.chromium.org/developers/design-documents/multi-column-layout for
  // more info.
  PhysicalOffset VisualOffsetFromAncestor(
      const PaintLayer* ancestor_layer,
      PhysicalOffset offset = PhysicalOffset()) const;

  // Convert a bounding box from flow thread coordinates, relative to |this|, to
  // visual coordinates, relative to |ancestorLayer|.
  // See http://www.chromium.org/developers/design-documents/multi-column-layout
  // for more info on these coordinate types.  This method requires this layer
  // to be paginated; i.e. it must have an enclosingPaginationLayer().
  void ConvertFromFlowThreadToVisualBoundingBoxInAncestor(
      const PaintLayer* ancestor_layer,
      PhysicalRect&) const;

  // The hitTest() method looks for mouse events by walking layers that
  // intersect the point from front to back.
  // |hit_test_area| is the rect in the space of this PaintLayer's
  // LayoutObject to consider for hit testing.
  bool HitTest(const HitTestLocation& location,
               HitTestResult&,
               const PhysicalRect& hit_test_area);

  // Bounding box relative to some ancestor layer. Pass offsetFromRoot if known.
  PhysicalRect PhysicalBoundingBox(
      const PhysicalOffset& offset_from_root) const;
  PhysicalRect PhysicalBoundingBox(const PaintLayer* ancestor_layer) const;

  // Static position is set in parent's coordinate space.
  LayoutUnit StaticInlinePosition() const { return static_inline_position_; }
  LayoutUnit StaticBlockPosition() const { return static_block_position_; }

  void SetStaticInlinePosition(LayoutUnit position) {
    static_inline_position_ = position;
  }
  void SetStaticBlockPosition(LayoutUnit position) {
    static_block_position_ = position;
  }

  using InlineEdge = NGLogicalStaticPosition::InlineEdge;
  using BlockEdge = NGLogicalStaticPosition::BlockEdge;
  InlineEdge StaticInlineEdge() const {
    return static_cast<InlineEdge>(static_inline_edge_);
  }
  BlockEdge StaticBlockEdge() const {
    return static_cast<BlockEdge>(static_block_edge_);
  }

  void SetStaticPositionFromNG(const NGLogicalStaticPosition& position) {
    static_inline_position_ = position.offset.inline_offset;
    static_block_position_ = position.offset.block_offset;
    static_inline_edge_ = position.inline_edge;
    static_block_edge_ = position.block_edge;
  }

  NGLogicalStaticPosition GetStaticPosition() const {
    NGLogicalStaticPosition position;
    position.offset.inline_offset = static_inline_position_;
    position.offset.block_offset = static_block_position_;
    position.inline_edge = StaticInlineEdge();
    position.block_edge = StaticBlockEdge();
    return position;
  }

  // Note that this transform has the transform-origin baked in.
  TransformationMatrix* Transform() const {
    return rare_data_ ? rare_data_->transform.get() : nullptr;
  }

  // Returns *Transform(), or identity matrix if Transform() is nullptr.
  TransformationMatrix CurrentTransform() const;

  bool Preserves3D() const { return GetLayoutObject().Preserves3D(); }
  bool Has3DTransform() const {
    return rare_data_ && rare_data_->transform &&
           !rare_data_->transform->IsAffine();
  }

  // Returns |true| if any property that renders using filter operations is
  // used (including, but not limited to, 'filter' and 'box-reflect').
  bool HasFilterInducingProperty() const {
    return GetLayoutObject().HasFilterInducingProperty();
  }

  bool SupportsSubsequenceCaching() const;

  // If the input CompositorFilterOperation is not empty, it will be populated
  // only if |filter_on_effect_node_dirty_| is true or the reference box has
  // changed. Otherwise it will be populated unconditionally.
  // |filter_on_effect_node_dirty_| will be cleared.
  void UpdateCompositorFilterOperationsForFilter(
      CompositorFilterOperations& operations);
  void SetFilterOnEffectNodeDirty() { filter_on_effect_node_dirty_ = true; }

  // |backdrop_filter_bounds| represents the clipping bounds for the filtered
  // backdrop image only. This rect lives in the local transform space of the
  // containing EffectPaintPropertyNode. If the input CompositorFilterOperation
  // is not empty, it will be populated only if
  // |backdrop_filter_on_effect_node_dirty_| is true or the reference box has
  // changed. Otherwise it will be populated unconditionally.
  // |backdrop_filter_on_effect_node_dirty_| will be cleared.
  void UpdateCompositorFilterOperationsForBackdropFilter(
      CompositorFilterOperations& operations,
      gfx::RRectF& backdrop_filter_bounds);
  void SetBackdropFilterOnEffectNodeDirty() {
    backdrop_filter_on_effect_node_dirty_ = true;
  }

  void SetIsUnderSVGHiddenContainer(bool value) {
    is_under_svg_hidden_container_ = value;
  }
  bool IsUnderSVGHiddenContainer() const {
    return is_under_svg_hidden_container_;
  }

  bool PaintsWithFilters() const;

  // Maps "forward" to determine which pixels in a destination rect are
  // affected by pixels in the source rect.
  // See also FilterEffect::mapRect.
  gfx::RectF MapRectForFilter(const gfx::RectF&) const;

  // Calls the above, rounding outwards.
  PhysicalRect MapRectForFilter(const PhysicalRect&) const;

  bool HasFilterThatMovesPixels() const {
    return has_filter_that_moves_pixels_;
  }

  PaintLayerResourceInfo* ResourceInfo() const {
    return rare_data_ ? rare_data_->resource_info.Get() : nullptr;
  }
  PaintLayerResourceInfo& EnsureResourceInfo();

  // Filter reference box is the area over which the filter is computed, in the
  // local coordinate system of the effect node containing the filter.
  gfx::RectF FilterReferenceBox() const;
  gfx::RectF BackdropFilterReferenceBox() const;
  gfx::RRectF BackdropFilterBounds() const;

  void UpdateFilterReferenceBox();
  void UpdateFilters(const ComputedStyle* old_style,
                     const ComputedStyle& new_style);
  void UpdateBackdropFilters(const ComputedStyle* old_style,
                             const ComputedStyle& new_style);
  void UpdateClipPath(const ComputedStyle* old_style,
                      const ComputedStyle& new_style);

  Node* EnclosingNode() const;

  bool IsInTopLayer() const;

  // FIXME: This should probably return a ScrollableArea but a lot of internal
  // methods are mistakenly exposed.
  PaintLayerScrollableArea* GetScrollableArea() const {
    return scrollable_area_.Get();
  }

  enum class GeometryMapperOption {
    kUseGeometryMapper,
    kDoNotUseGeometryMapper
  };

  PaintLayerClipper Clipper(GeometryMapperOption) const;

  bool ScrollsOverflow() const;

  bool NeedsVisualOverflowRecalc() const {
    return needs_visual_overflow_recalc_;
  }
  void SetNeedsVisualOverflowRecalc();
  void SetNeedsCompositingInputsUpdate();
  void ScrollContainerStatusChanged();

  // Returns the nearest ancestor layer (in containing block hierarchy,
  // not including this layer) that is a scroll container. It's nullptr for
  // the root layer. If not null, the value of |is_fixed_to_view| will be set to
  // true if the result of this function is the root layer and the current layer
  // is fixed to the view due to fixed-position ancestors.
  const PaintLayer* ContainingScrollContainerLayer(
      bool* is_fixed_to_view = nullptr) const;

  bool HasFixedPositionDescendant() const {
    DCHECK(!needs_descendant_dependent_flags_update_);
    return has_fixed_position_descendant_;
  }
  bool HasNonContainedAbsolutePositionDescendant() const {
    DCHECK(!needs_descendant_dependent_flags_update_);
    return has_non_contained_absolute_position_descendant_;
  }
  bool HasSelfPaintingLayerDescendant() const {
    DCHECK(!needs_descendant_dependent_flags_update_);
    return has_self_painting_layer_descendant_;
  }

  // See
  // PaintLayerStackingNode::layer_to_overlay_overflow_controls_painting_after_.
  bool NeedsReorderOverlayOverflowControls() const {
    return needs_reorder_overlay_overflow_controls_;
  }

  // Returns true if there is a descendant with blend-mode that is
  // not contained within another enclosing stacking context other
  // than the stacking context blend-mode creates, or the stacking
  // context this PaintLayer might create. This is needed because
  // blend-mode content needs to blend with the containing stacking
  // context's painted output, but not the content in any grandparent
  // stacking contexts.
  bool HasNonIsolatedDescendantWithBlendMode() const;

  CompositingReasons GetCompositingReasons() const {
    // TODO(pdr): Remove this.
    return CompositingReason::kNone;
  }

  void UpdateDescendantDependentFlags();

  void UpdateSelfPaintingLayer();
  // This is O(depth) so avoid calling this in loops. Instead use optimizations
  // like those in PaintInvalidatorContext.
  PaintLayer* EnclosingSelfPaintingLayer();

  void DidUpdateScrollsOverflow();

  bool SelfNeedsRepaint() const { return self_needs_repaint_; }
  bool DescendantNeedsRepaint() const { return descendant_needs_repaint_; }
  bool SelfOrDescendantNeedsRepaint() const {
    return self_needs_repaint_ || descendant_needs_repaint_;
  }
  void SetNeedsRepaint();
  void SetDescendantNeedsRepaint();
  void ClearNeedsRepaintRecursively();

  bool NeedsCullRectUpdate() const { return needs_cull_rect_update_; }
  bool ForcesChildrenCullRectUpdate() const {
    return forces_children_cull_rect_update_;
  }
  bool DescendantNeedsCullRectUpdate() const {
    return descendant_needs_cull_rect_update_;
  }
  bool SelfOrDescendantNeedsCullRectUpdate() const {
    return needs_cull_rect_update_ || descendant_needs_cull_rect_update_;
  }
  void SetNeedsCullRectUpdate();
  void SetForcesChildrenCullRectUpdate();
  void MarkCompositingContainerChainForNeedsCullRectUpdate();
  void SetDescendantNeedsCullRectUpdate();
  void ClearNeedsCullRectUpdate() {
    needs_cull_rect_update_ = false;
    forces_children_cull_rect_update_ = false;
    descendant_needs_cull_rect_update_ = false;
  }

  // The paint result of this layer during the previous painting with
  // subsequence. A painting without subsequence [1] doesn't change this flag.
  // [1] See ShouldCreateSubsequence() in paint_layer_painter.cc for the cases
  // we use subsequence when painting a PaintLayer.
  PaintResult PreviousPaintResult() const {
    return static_cast<PaintResult>(previous_paint_result_);
  }
  void SetPreviousPaintResult(PaintResult result) {
    previous_paint_result_ = static_cast<unsigned>(result);
    DCHECK(previous_paint_result_ == static_cast<unsigned>(result));
  }

  // Used to skip PaintPhaseDescendantOutlinesOnly for layers that have never
  // had descendant outlines.  The flag is set during paint invalidation on a
  // self painting layer if any contained object has outline.
  // For more details, see core/paint/REAME.md#Empty paint phase optimization.
  bool NeedsPaintPhaseDescendantOutlines() const {
    return needs_paint_phase_descendant_outlines_;
  }
  void SetNeedsPaintPhaseDescendantOutlines() {
    DCHECK(IsSelfPaintingLayer());
    needs_paint_phase_descendant_outlines_ = true;
  }

  // Similar to above, but for PaintPhaseFloat.
  bool NeedsPaintPhaseFloat() const { return needs_paint_phase_float_; }
  void SetNeedsPaintPhaseFloat() {
    DCHECK(IsSelfPaintingLayer());
    needs_paint_phase_float_ = true;
  }

  bool Has3DTransformedDescendant() const {
    DCHECK(!needs_descendant_dependent_flags_update_);
    return has3d_transformed_descendant_;
  }

  // See
  // https://chromium.googlesource.com/chromium/src.git/+/master/third_party/blink/renderer/core/paint/README.md
  // for the definition of a replaced normal-flow stacking element.
  bool IsReplacedNormalFlowStacking() const;

#if DCHECK_IS_ON()
  bool IsInStackingParentZOrderLists() const;
  bool LayerListMutationAllowed() const { return layer_list_mutation_allowed_; }
#endif

  void DirtyStackingContextZOrderLists();

  PhysicalOffset OffsetForInFlowRelPosition() const {
    return rare_data_ ? rare_data_->offset_for_in_flow_rel_position
                      : PhysicalOffset();
  }

  bool KnownToClipSubtreeToPaddingBox() const;

  void Trace(Visitor*) const override;

 private:
  void Update3DTransformedDescendantStatus();

  // Bounding box in the coordinates of this layer.
  PhysicalRect LocalBoundingBox() const;

  void UpdateLayerPositionRecursive();
  void UpdateLayerPosition();

  void SetNextSibling(PaintLayer* next) { next_ = next; }
  void SetPreviousSibling(PaintLayer* prev) { previous_ = prev; }
  void SetFirstChild(PaintLayer* first) { first_ = first; }
  void SetLastChild(PaintLayer* last) { last_ = last; }

  void UpdateHasSelfPaintingLayerDescendant() const;

  void AppendSingleFragmentForHitTesting(
      PaintLayerFragments&,
      const PaintLayerFragment* container_fragment,
      ShouldRespectOverflowClipType) const;

  void CollectFragments(PaintLayerFragments&,
                        const PaintLayer* root_layer,
                        ShouldRespectOverflowClipType = kRespectOverflowClip,
                        const FragmentData* root_fragment = nullptr) const;

  struct HitTestRecursionData {
    STACK_ALLOCATED();

   public:
    const PhysicalRect& rect;
    // Whether location.Intersects(rect) returns true.
    const HitTestLocation& location;
    const HitTestLocation& original_location;
    const bool intersects_location;
    HitTestRecursionData(const PhysicalRect& rect_arg,
                         const HitTestLocation& location_arg,
                         const HitTestLocation& original_location_arg);
  };

  PaintLayer* HitTestLayer(const PaintLayer& transform_container,
                           const PaintLayerFragment* container_fragment,
                           HitTestResult&,
                           const HitTestRecursionData& recursion_data,
                           bool applied_transform = false,
                           HitTestingTransformState* = nullptr,
                           double* z_offset = nullptr,
                           bool overflow_controls_only = false);
  PaintLayer* HitTestLayerByApplyingTransform(
      const PaintLayer& transform_container,
      const PaintLayerFragment* container_fragment,
      const PaintLayerFragment& local_fragment,
      HitTestResult&,
      const HitTestRecursionData& recursion_data,
      HitTestingTransformState*,
      double* z_offset,
      bool overflow_controls_only,
      const PhysicalOffset& translation_offset = PhysicalOffset());
  PaintLayer* HitTestChildren(
      PaintLayerIteration,
      const PaintLayer& transform_container,
      const PaintLayerFragment* container_fragment,
      HitTestResult&,
      const HitTestRecursionData& recursion_data,
      HitTestingTransformState* container_transform_state,
      double* z_offset_for_descendants,
      double* z_offset,
      HitTestingTransformState* local_transform_state,
      bool depth_sort_descendants);

  HitTestingTransformState CreateLocalTransformState(
      const PaintLayer& transform_container,
      const FragmentData& container_fragment,
      const FragmentData& local_fragment,
      const HitTestRecursionData& recursion_data,
      const HitTestingTransformState* root_transform_state) const;

  bool HitTestFragmentWithPhase(HitTestResult&,
                                const NGPhysicalBoxFragment*,
                                const PhysicalOffset& fragment_offset,
                                const HitTestLocation&,
                                HitTestPhase phase) const;
  bool HitTestFragmentsWithPhase(const PaintLayerFragments&,
                                 HitTestResult&,
                                 const HitTestLocation&,
                                 HitTestPhase,
                                 bool& inside_clip_rect) const;
  bool HitTestForegroundForFragments(const PaintLayerFragments&,
                                     HitTestResult&,
                                     const HitTestLocation&,
                                     bool& inside_clip_rect) const;
  PaintLayer* HitTestTransformedLayerInFragments(
      const PaintLayer& transform_container,
      const PaintLayerFragment* container_fragment,
      HitTestResult&,
      const HitTestRecursionData&,
      HitTestingTransformState*,
      double* z_offset,
      bool overflow_controls_only,
      ShouldRespectOverflowClipType);
  bool HitTestClippedOutByClipPath(const PaintLayer& root_layer,
                                   const HitTestLocation&) const;

  bool ShouldBeSelfPaintingLayer() const;

  void UpdateStackingNode();

  FilterOperations FilterOperationsIncludingReflection() const;

  bool RequiresScrollableArea() const;
  void UpdateScrollableArea();

  // Indicates whether the descendant-dependent tree walk bit should also
  // be set.
  enum DescendantDependentFlagsUpdateFlag {
    kNeedsDescendantDependentUpdate,
    kDoesNotNeedDescendantDependentUpdate
  };

  // Marks the ancestor chain for paint property update, and if
  // the flag is set, the descendant-dependent tree walk as well.
  void MarkAncestorChainForFlagsUpdate(
      DescendantDependentFlagsUpdateFlag = kNeedsDescendantDependentUpdate);

  // For transform updates we use a fast path that will not change
  // NeedsPaintPropertyUpdate, but still need to set
  // NeedsDescendantDependentFlagsUpdate to true, and will use this function.
  void SetNeedsDescendantDependentFlagsUpdate();

  void UpdateTransform(const ComputedStyle* old_style,
                       const ComputedStyle& new_style);

  void UpdatePaginationRecursive(bool needs_pagination_update = false);
  void ClearPaginationRecursive();

  void MarkCompositingContainerChainForNeedsRepaint();

  PaintLayerRareData& EnsureRareData() {
    if (!rare_data_)
      rare_data_ = MakeGarbageCollected<PaintLayerRareData>();
    return *rare_data_;
  }

  void MergeNeedsPaintPhaseFlagsFrom(const PaintLayer& layer) {
    needs_paint_phase_descendant_outlines_ |=
        layer.needs_paint_phase_descendant_outlines_;
    needs_paint_phase_float_ |= layer.needs_paint_phase_float_;
  }

  void ExpandRectForSelfPaintingDescendants(PhysicalRect& result) const;

  // This is private because PaintLayerStackingNode is only for PaintLayer and
  // PaintLayerPaintOrderIterator.
  PaintLayerStackingNode* StackingNode() const { return stacking_node_; }

  void SetNeedsReorderOverlayOverflowControls(bool b) {
    needs_reorder_overlay_overflow_controls_ = b;
  }

  bool ComputeHasFilterThatMovesPixels() const;

  // Self-painting layer is an optimization where we avoid the heavy Layer
  // painting machinery for a Layer allocated only to handle the overflow clip
  // case.
  // FIXME(crbug.com/332791): Self-painting layer should be merged into the
  // overflow-only concept.
  unsigned is_self_painting_layer_ : 1;

  const unsigned is_root_layer_ : 1;

  unsigned has_visible_content_ : 1;

  unsigned needs_descendant_dependent_flags_update_ : 1;
  unsigned needs_visual_overflow_recalc_ : 1;

  unsigned has_visible_self_painting_descendant_ : 1;

#if DCHECK_IS_ON()
  unsigned needs_position_update_ : 1;
#endif

  // Set on a stacking context layer that has 3D descendants anywhere
  // in a preserves3D hierarchy. Hint to do 3D-aware hit testing.
  unsigned has3d_transformed_descendant_ : 1;

  unsigned self_needs_repaint_ : 1;
  unsigned descendant_needs_repaint_ : 1;

  unsigned needs_cull_rect_update_ : 1;
  unsigned forces_children_cull_rect_update_ : 1;
  // True if any descendant needs cull rect update.
  unsigned descendant_needs_cull_rect_update_ : 1;

  unsigned previous_paint_result_ : 1;  // PaintResult
  static_assert(kMaxPaintResult < 2,
                "Should update number of bits of previous_paint_result_");

  unsigned needs_paint_phase_descendant_outlines_ : 1;
  unsigned needs_paint_phase_float_ : 1;

  // These bitfields are part of ancestor/descendant dependent compositing
  // inputs.
  unsigned has_non_isolated_descendant_with_blend_mode_ : 1;
  unsigned has_fixed_position_descendant_ : 1;
  unsigned has_non_contained_absolute_position_descendant_ : 1;
  unsigned has_stacked_descendant_in_current_stacking_context_ : 1;

  // These are set to true when filter style or filter resource changes,
  // indicating that we need to update the filter (or backdrop_filter) field of
  // the effect paint property node. They are cleared when the effect paint
  // property node is updated.
  unsigned filter_on_effect_node_dirty_ : 1;
  unsigned backdrop_filter_on_effect_node_dirty_ : 1;

  // Caches |ComputeHasFilterThatMovesPixels()|, updated on style changes.
  unsigned has_filter_that_moves_pixels_ : 1;

  // True if the current subtree is underneath a LayoutSVGHiddenContainer
  // ancestor.
  unsigned is_under_svg_hidden_container_ : 1;

  unsigned has_self_painting_layer_descendant_ : 1;

  unsigned needs_reorder_overlay_overflow_controls_ : 1;
  unsigned static_inline_edge_ : 2;
  unsigned static_block_edge_ : 2;

#if DCHECK_IS_ON()
  mutable unsigned layer_list_mutation_allowed_ : 1;
  bool is_destroyed_ = false;
#endif

  const Member<LayoutBoxModelObject> layout_object_;

  Member<PaintLayer> parent_;
  Member<PaintLayer> previous_;
  Member<PaintLayer> next_;
  Member<PaintLayer> first_;
  Member<PaintLayer> last_;

  // Our (x,y) coordinates are in our containing layer's coordinate space,
  // excluding positioning offset and scroll.
  PhysicalOffset location_without_position_offset_;

  // The layer's size.
  //
  // If the associated LayoutBoxModelObject is a LayoutBox, it's its border
  // box. Otherwise, this is the LayoutInline's lines' bounding box.
  LayoutSize size_;

  // Cached normal flow values for absolute positioned elements with static
  // left/top values.
  LayoutUnit static_inline_position_;
  LayoutUnit static_block_position_;

  Member<PaintLayerScrollableArea> scrollable_area_;

  Member<PaintLayerStackingNode> stacking_node_;

  Member<PaintLayerRareData> rare_data_;

  // For layer_list_mutation_allowed_.
  friend class PaintLayerListMutationDetector;

  // For stacking_node_ to avoid exposing it publicly.
  friend class PaintLayerPaintOrderIterator;
  friend class PaintLayerPaintOrderReverseIterator;
  friend class PaintLayerStackingNode;

  FRIEND_TEST_ALL_PREFIXES(PaintLayerTest,
                           DescendantDependentFlagsStopsAtThrottledFrames);
  FRIEND_TEST_ALL_PREFIXES(PaintLayerTest,
                           PaintLayerTransformUpdatedOnStyleTransformAnimation);
  FRIEND_TEST_ALL_PREFIXES(
      PaintLayerOverlapTest,
      FixedUnderTransformDoesNotExpandBoundingBoxForOverlap);
  FRIEND_TEST_ALL_PREFIXES(PaintLayerOverlapTest,
                           FixedUsesExpandedBoundingBoxForOverlap);
  FRIEND_TEST_ALL_PREFIXES(PaintLayerOverlapTest,
                           FixedInScrollerUsesExpandedBoundingBoxForOverlap);
  FRIEND_TEST_ALL_PREFIXES(PaintLayerOverlapTest,
                           NestedFixedUsesExpandedBoundingBoxForOverlap);
  FRIEND_TEST_ALL_PREFIXES(PaintLayerOverlapTest,

                           FixedWithExpandedBoundsForChild);
  FRIEND_TEST_ALL_PREFIXES(PaintLayerOverlapTest,
                           FixedWithClippedExpandedBoundsForChild);
  FRIEND_TEST_ALL_PREFIXES(PaintLayerOverlapTest,
                           FixedWithExpandedBoundsForGrandChild);
  FRIEND_TEST_ALL_PREFIXES(PaintLayerOverlapTest,

                           FixedWithExpandedBoundsForFixedChild);
};

#if DCHECK_IS_ON()
class PaintLayerListMutationDetector {
  STACK_ALLOCATED();

 public:
  explicit PaintLayerListMutationDetector(const PaintLayer* layer)
      : layer_(layer),
        previous_mutation_allowed_state_(layer->layer_list_mutation_allowed_) {
    layer->layer_list_mutation_allowed_ = false;
  }

  ~PaintLayerListMutationDetector() {
    layer_->layer_list_mutation_allowed_ = previous_mutation_allowed_state_;
  }

 private:
  const PaintLayer* layer_;
  bool previous_mutation_allowed_state_;
};
#endif

}  // namespace blink

#if DCHECK_IS_ON()
// Outside the blink namespace for ease of invocation from gdb.
CORE_EXPORT void ShowLayerTree(const blink::PaintLayer*);
CORE_EXPORT void ShowLayerTree(const blink::LayoutObject*);
#endif

namespace cppgc {
// Assign PaintLayer to be allocated on custom LayoutObjectSpace.
template <typename T>
struct SpaceTrait<
    T,
    std::enable_if_t<std::is_base_of<blink::PaintLayer, T>::value>> {
  using Space = blink::LayoutObjectSpace;
};
}  // namespace cppgc

#endif  // THIRD_PARTY_BLINK_RENDERER_CORE_PAINT_PAINT_LAYER_H_
