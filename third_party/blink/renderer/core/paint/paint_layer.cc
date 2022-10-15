/*
 * Copyright (C) 2006, 2007, 2008, 2009, 2010, 2011, 2012 Apple Inc. All rights
 * reserved.
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

#include "third_party/blink/renderer/core/paint/paint_layer.h"

#include <limits>

#include "base/allocator/partition_allocator/partition_alloc.h"
#include "base/containers/adapters.h"
#include "build/build_config.h"
#include "third_party/blink/public/platform/task_type.h"
#include "third_party/blink/renderer/core/animation/scroll_timeline.h"
#include "third_party/blink/renderer/core/css/css_property_names.h"
#include "third_party/blink/renderer/core/css/style_request.h"
#include "third_party/blink/renderer/core/document_transition/document_transition_supplement.h"
#include "third_party/blink/renderer/core/dom/document.h"
#include "third_party/blink/renderer/core/dom/shadow_root.h"
#include "third_party/blink/renderer/core/frame/local_frame.h"
#include "third_party/blink/renderer/core/frame/local_frame_view.h"
#include "third_party/blink/renderer/core/frame/settings.h"
#include "third_party/blink/renderer/core/frame/visual_viewport.h"
#include "third_party/blink/renderer/core/html_names.h"
#include "third_party/blink/renderer/core/layout/fragmentainer_iterator.h"
#include "third_party/blink/renderer/core/layout/geometry/transform_state.h"
#include "third_party/blink/renderer/core/layout/hit_test_request.h"
#include "third_party/blink/renderer/core/layout/hit_test_result.h"
#include "third_party/blink/renderer/core/layout/layout_embedded_content.h"
#include "third_party/blink/renderer/core/layout/layout_flow_thread.h"
#include "third_party/blink/renderer/core/layout/layout_inline.h"
#include "third_party/blink/renderer/core/layout/layout_tree_as_text.h"
#include "third_party/blink/renderer/core/layout/layout_view.h"
#include "third_party/blink/renderer/core/layout/ng/ng_fragmentation_utils.h"
#include "third_party/blink/renderer/core/layout/svg/layout_svg_resource_clipper.h"
#include "third_party/blink/renderer/core/layout/svg/layout_svg_root.h"
#include "third_party/blink/renderer/core/page/page.h"
#include "third_party/blink/renderer/core/page/scrolling/sticky_position_scrolling_constraints.h"
#include "third_party/blink/renderer/core/paint/box_reflection_utils.h"
#include "third_party/blink/renderer/core/paint/clip_path_clipper.h"
#include "third_party/blink/renderer/core/paint/compositing/compositing_reason_finder.h"
#include "third_party/blink/renderer/core/paint/filter_effect_builder.h"
#include "third_party/blink/renderer/core/paint/hit_testing_transform_state.h"
#include "third_party/blink/renderer/core/paint/ng/ng_box_fragment_painter.h"
#include "third_party/blink/renderer/core/paint/object_paint_invalidator.h"
#include "third_party/blink/renderer/core/paint/paint_info.h"
#include "third_party/blink/renderer/core/paint/paint_layer_paint_order_iterator.h"
#include "third_party/blink/renderer/core/paint/paint_layer_painter.h"
#include "third_party/blink/renderer/core/paint/paint_layer_scrollable_area.h"
#include "third_party/blink/renderer/core/paint/paint_property_tree_builder.h"
#include "third_party/blink/renderer/core/paint/rounded_border_geometry.h"
#include "third_party/blink/renderer/core/style/computed_style.h"
#include "third_party/blink/renderer/core/style/reference_clip_path_operation.h"
#include "third_party/blink/renderer/core/style/shape_clip_path_operation.h"
#include "third_party/blink/renderer/platform/bindings/runtime_call_stats.h"
#include "third_party/blink/renderer/platform/bindings/v8_per_isolate_data.h"
#include "third_party/blink/renderer/platform/geometry/length_functions.h"
#include "third_party/blink/renderer/platform/graphics/compositor_filter_operations.h"
#include "third_party/blink/renderer/platform/graphics/filters/filter.h"
#include "third_party/blink/renderer/platform/graphics/paint/geometry_mapper.h"
#include "third_party/blink/renderer/platform/heap/garbage_collected.h"
#include "third_party/blink/renderer/platform/instrumentation/tracing/trace_event.h"
#include "third_party/blink/renderer/platform/runtime_enabled_features.h"
#include "third_party/blink/renderer/platform/transforms/transformation_matrix.h"
#include "third_party/blink/renderer/platform/wtf/allocator/partitions.h"
#include "third_party/blink/renderer/platform/wtf/size_assertions.h"
#include "third_party/blink/renderer/platform/wtf/std_lib_extras.h"
#include "ui/gfx/geometry/point3_f.h"
#include "ui/gfx/geometry/rect_f.h"

namespace blink {

namespace {

#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)
struct SameSizeAsPaintLayer : GarbageCollected<PaintLayer>, DisplayItemClient {
  // The bit fields may fit into the machine word of DisplayItemClient which
  // has only 8-bit data.
  unsigned bit_fields1 : 24;
  unsigned bit_fields2 : 24;
#if DCHECK_IS_ON()
  bool is_destroyed;
#endif
  Member<void*> members1[6];
  PhysicalOffset offset;
  LayoutSize size;
  LayoutUnit layout_units[2];
  Member<void*> members2[3];
};

ASSERT_SIZE(PaintLayer, SameSizeAsPaintLayer);
#endif

inline PhysicalRect PhysicalVisualOverflowRectAllowingUnset(
    const LayoutBoxModelObject& layout_object) {
#if DCHECK_IS_ON()
  NGInkOverflow::ReadUnsetAsNoneScope read_unset_as_none;
#endif
  return layout_object.PhysicalVisualOverflowRect();
}

PaintLayer* SlowContainingLayer(const PaintLayer* ancestor,
                                bool* skipped_ancestor,
                                LayoutObject* layout_object) {
  // This is a universal approach to find the containing layer, but it is
  // slower.
  absl::optional<LayoutObject::AncestorSkipInfo> skip_info;
  if (skipped_ancestor)
    skip_info.emplace(&ancestor->GetLayoutObject());
  while (auto* container = layout_object->Container(
             skipped_ancestor ? &*skip_info : nullptr)) {
    if (skipped_ancestor) {
      if (skip_info->AncestorSkipped())
        *skipped_ancestor = true;
      skip_info.emplace(&ancestor->GetLayoutObject());
    }
    if (container->HasLayer())
      return To<LayoutBoxModelObject>(container)->Layer();
    layout_object = container;
  }
  return nullptr;
}

}  // namespace

PaintLayerRareData::PaintLayerRareData()
    : enclosing_pagination_layer(nullptr) {}

PaintLayerRareData::~PaintLayerRareData() = default;

void PaintLayerRareData::Trace(Visitor* visitor) const {
  visitor->Trace(enclosing_pagination_layer);
  visitor->Trace(resource_info);
}

PaintLayer::PaintLayer(LayoutBoxModelObject* layout_object)
    : is_root_layer_(IsA<LayoutView>(layout_object)),
      has_visible_content_(false),
      needs_descendant_dependent_flags_update_(true),
      needs_visual_overflow_recalc_(true),
      has_visible_self_painting_descendant_(false),
#if DCHECK_IS_ON()
      // The root layer (LayoutView) does not need position update at start
      // because its Location() is always 0.
      needs_position_update_(!IsRootLayer()),
#endif
      has3d_transformed_descendant_(false),
      self_needs_repaint_(false),
      descendant_needs_repaint_(false),
      needs_cull_rect_update_(false),
      forces_children_cull_rect_update_(false),
      descendant_needs_cull_rect_update_(false),
      previous_paint_result_(kMayBeClippedByCullRect),
      needs_paint_phase_descendant_outlines_(false),
      needs_paint_phase_float_(false),
      has_non_isolated_descendant_with_blend_mode_(false),
      has_fixed_position_descendant_(false),
      has_non_contained_absolute_position_descendant_(false),
      has_stacked_descendant_in_current_stacking_context_(false),
      filter_on_effect_node_dirty_(false),
      backdrop_filter_on_effect_node_dirty_(false),
      has_filter_that_moves_pixels_(false),
      is_under_svg_hidden_container_(false),
      has_self_painting_layer_descendant_(false),
      needs_reorder_overlay_overflow_controls_(false),
      static_inline_edge_(InlineEdge::kInlineStart),
      static_block_edge_(BlockEdge::kBlockStart),
#if DCHECK_IS_ON()
      layer_list_mutation_allowed_(true),
#endif
      layout_object_(layout_object),
      parent_(nullptr),
      previous_(nullptr),
      next_(nullptr),
      first_(nullptr),
      last_(nullptr),
      static_inline_position_(0),
      static_block_position_(0) {
  is_self_painting_layer_ = ShouldBeSelfPaintingLayer();

  UpdateScrollableArea();
}

PaintLayer::~PaintLayer() {
#if DCHECK_IS_ON()
  DCHECK(is_destroyed_);
#endif
}

void PaintLayer::Destroy() {
#if DCHECK_IS_ON()
  DCHECK(!is_destroyed_);
#endif
  if (rare_data_ && rare_data_->resource_info) {
    const ComputedStyle& style = GetLayoutObject().StyleRef();
    if (style.HasFilter())
      style.Filter().RemoveClient(*rare_data_->resource_info);
    if (auto* reference_clip =
            DynamicTo<ReferenceClipPathOperation>(style.ClipPath()))
      reference_clip->RemoveClient(*rare_data_->resource_info);
    rare_data_->resource_info->ClearLayer();
  }

  // Reset this flag before disposing scrollable_area_ to prevent
  // PaintLayerScrollableArea::WillRemoveScrollbar() from dirtying the z-order
  // list of the stacking context. If this layer is removed from the parent,
  // the z-order list should have been invalidated in RemoveChild().
  needs_reorder_overlay_overflow_controls_ = false;

  if (scrollable_area_)
    scrollable_area_->Dispose();

#if DCHECK_IS_ON()
  is_destroyed_ = true;
#endif
}

String PaintLayer::DebugName() const {
  return GetLayoutObject().DebugName();
}

DOMNodeId PaintLayer::OwnerNodeId() const {
  return static_cast<const DisplayItemClient&>(GetLayoutObject()).OwnerNodeId();
}

bool PaintLayer::PaintsWithFilters() const {
  if (!GetLayoutObject().HasFilterInducingProperty())
    return false;
  return true;
}

const PaintLayer* PaintLayer::ContainingScrollContainerLayer(
    bool* is_fixed_to_view) const {
  bool is_fixed = GetLayoutObject().IsFixedPositioned();
  for (const PaintLayer* container = ContainingLayer(); container;
       container = container->ContainingLayer()) {
    if (container->GetLayoutObject().IsScrollContainer()) {
      if (is_fixed_to_view)
        *is_fixed_to_view = is_fixed && container->IsRootLayer();
      DCHECK(container->GetScrollableArea());
      return container;
    }
    is_fixed = container->GetLayoutObject().IsFixedPositioned();
  }
  DCHECK(IsRootLayer());
  if (is_fixed_to_view)
    *is_fixed_to_view = true;
  return nullptr;
}

void PaintLayer::UpdateLayerPositionsAfterLayout() {
  DCHECK(IsRootLayer());

  TRACE_EVENT0("blink,benchmark",
               "PaintLayer::updateLayerPositionsAfterLayout");
  RUNTIME_CALL_TIMER_SCOPE(
      V8PerIsolateData::MainThreadIsolate(),
      RuntimeCallStats::CounterId::kUpdateLayerPositionsAfterLayout);

  UpdateLayerPositionRecursive();
  UpdatePaginationRecursive(EnclosingPaginationLayer());
}

void PaintLayer::UpdateLayerPositionRecursive() {
  UpdateLayerPosition();

  if (GetLayoutObject().UpdateStickyPositionConstraints()) {
    // Sticky position constraints and ancestor overflow scroller affect
    // the sticky layer position, so we need to update it again here.
    UpdateLayerPosition();
  }

  if (LayoutBox* box = DynamicTo<LayoutBox>(GetLayoutObject());
      box && box->AnchorScrollContainer()) {
    LayoutBox::AnchorScrollData anchor_scroll_data =
        box->ComputeAnchorScrollData();
    DCHECK(anchor_scroll_data.inner_most_scroll_container_layer);

    bool needs_paint_property_update = false;
    for (const PaintLayer* scroller_layer =
             anchor_scroll_data.inner_most_scroll_container_layer;
         ; scroller_layer = scroller_layer->ContainingScrollContainerLayer()) {
      DCHECK(scroller_layer);
      bool is_new_entry =
          scroller_layer->GetScrollableArea()->AddAnchorPositionedLayer(this);
      if (!is_new_entry)
        break;
      needs_paint_property_update = true;
      if (scroller_layer ==
          anchor_scroll_data.outer_most_scroll_container_layer)
        break;
    }
    if (needs_paint_property_update)
      box->SetNeedsPaintPropertyUpdate();
  }

  // Display-locked elements always have a PaintLayer, meaning that the
  // PaintLayer traversal won't skip locked elements. Thus, we don't have to do
  // an ancestor check, and simply skip iterating children when this element is
  // locked for child layout.
  if (GetLayoutObject().ChildLayoutBlockedByDisplayLock())
    return;

  for (PaintLayer* child = FirstChild(); child; child = child->NextSibling())
    child->UpdateLayerPositionRecursive();
}

void PaintLayer::UpdateTransformationMatrix() {
  if (TransformationMatrix* transform = Transform()) {
    LayoutBox* box = GetLayoutBox();
    DCHECK(box);
    transform->MakeIdentity();
    box->StyleRef().ApplyTransform(
        *transform, box->Size(), ComputedStyle::kIncludeTransformOperations,
        ComputedStyle::kIncludeTransformOrigin,
        ComputedStyle::kIncludeMotionPath,
        ComputedStyle::kIncludeIndependentTransformProperties);
    if (!box->GetDocument().GetSettings()->GetAcceleratedCompositingEnabled())
      transform->MakeAffine();
  }
}

void PaintLayer::UpdateTransform(const ComputedStyle* old_style,
                                 const ComputedStyle& new_style) {
  // It's possible for the old and new style transform data to be equivalent
  // while HasTransform() differs, as it checks a number of conditions aside
  // from just the matrix, including but not limited to animation state.
  bool had_transform = Transform();
  bool has_transform = GetLayoutObject().HasTransform();
  if (had_transform == has_transform && old_style &&
      new_style.TransformDataEquivalent(*old_style)) {
    return;
  }
  bool had_3d_transform = Has3DTransform();

  if (has_transform != had_transform) {
    if (has_transform)
      EnsureRareData().transform = std::make_unique<TransformationMatrix>();
    else
      rare_data_->transform.reset();
  }

  UpdateTransformationMatrix();

  if (had_3d_transform != Has3DTransform())
    MarkAncestorChainForFlagsUpdate();

  if (LocalFrameView* frame_view = GetLayoutObject().GetDocument().View())
    frame_view->SetNeedsUpdateGeometries();
}

TransformationMatrix PaintLayer::CurrentTransform() const {
  if (TransformationMatrix* transform = Transform())
    return *transform;
  return TransformationMatrix();
}

void PaintLayer::ConvertFromFlowThreadToVisualBoundingBoxInAncestor(
    const PaintLayer* ancestor_layer,
    PhysicalRect& rect) const {
  PaintLayer* pagination_layer = EnclosingPaginationLayer();
  DCHECK(pagination_layer);
  auto& flow_thread = To<LayoutFlowThread>(pagination_layer->GetLayoutObject());

  // First make the flow thread rectangle relative to the flow thread, not to
  // |layer|.
  PhysicalOffset offset_within_pagination_layer;
  ConvertToLayerCoords(pagination_layer, offset_within_pagination_layer);
  rect.Move(offset_within_pagination_layer);

  // Then make the rectangle visual, relative to the fragmentation context.
  // Split our box up into the actual fragment boxes that layout in the
  // columns/pages and unite those together to get our true bounding box.
  rect = PhysicalRectToBeNoop(
      flow_thread.FragmentsBoundingBox(rect.ToLayoutRect()));

  // Finally, make the visual rectangle relative to |ancestorLayer|.
  if (ancestor_layer->EnclosingPaginationLayer() != pagination_layer) {
    rect.Move(pagination_layer->VisualOffsetFromAncestor(ancestor_layer));
    return;
  }
  // The ancestor layer is inside the same pagination layer as |layer|, so we
  // need to subtract the visual distance from the ancestor layer to the
  // pagination layer.
  rect.Move(-ancestor_layer->VisualOffsetFromAncestor(pagination_layer));
}

void PaintLayer::UpdatePaginationRecursive(bool needs_pagination_update) {
  if (rare_data_)
    rare_data_->enclosing_pagination_layer = nullptr;

  if (GetLayoutObject().IsLayoutFlowThread())
    needs_pagination_update = true;

  if (needs_pagination_update) {
    // Each paginated layer has to paint on its own. There is no recurring into
    // child layers. Each layer has to be checked individually and genuinely
    // know if it is going to have to split itself up when painting only its
    // contents (and not any other descendant layers). We track an
    // enclosingPaginationLayer instead of using a simple bit, since we want to
    // be able to get back to that layer easily.
    if (LayoutFlowThread* containing_flow_thread =
            GetLayoutObject().FlowThreadContainingBlock())
      EnsureRareData().enclosing_pagination_layer =
          containing_flow_thread->Layer();
  }

  // If this element prevents child painting, then we can skip updating
  // pagination info, since it won't be used anyway.
  if (GetLayoutObject().ChildPaintBlockedByDisplayLock())
    return;

  for (PaintLayer* child = FirstChild(); child; child = child->NextSibling())
    child->UpdatePaginationRecursive(needs_pagination_update);
}

void PaintLayer::ClearPaginationRecursive() {
  if (rare_data_)
    rare_data_->enclosing_pagination_layer = nullptr;
  for (PaintLayer* child = FirstChild(); child; child = child->NextSibling())
    child->ClearPaginationRecursive();
}

void PaintLayer::DirtyVisibleContentStatus() {
  MarkAncestorChainForFlagsUpdate();
  // Non-self-painting layers paint into their ancestor layer, and count as part
  // of the "visible contents" of the parent, so we need to dirty it.
  if (!IsSelfPaintingLayer())
    Parent()->DirtyVisibleContentStatus();
}

void PaintLayer::MarkAncestorChainForFlagsUpdate(
    DescendantDependentFlagsUpdateFlag flag) {
#if DCHECK_IS_ON()
  DCHECK(flag == kDoesNotNeedDescendantDependentUpdate ||
         !layout_object_->GetDocument()
              .View()
              ->IsUpdatingDescendantDependentFlags());
#endif
  for (PaintLayer* layer = this; layer; layer = layer->Parent()) {
    if (layer->needs_descendant_dependent_flags_update_ &&
        layer->GetLayoutObject().NeedsPaintPropertyUpdate())
      break;
    if (flag == kNeedsDescendantDependentUpdate)
      layer->needs_descendant_dependent_flags_update_ = true;
    layer->GetLayoutObject().SetNeedsPaintPropertyUpdate();
  }
}

void PaintLayer::SetNeedsDescendantDependentFlagsUpdate() {
  for (PaintLayer* layer = this; layer; layer = layer->Parent()) {
    if (layer->needs_descendant_dependent_flags_update_)
      break;
    layer->needs_descendant_dependent_flags_update_ = true;
  }
}

void PaintLayer::UpdateDescendantDependentFlags() {
  if (needs_descendant_dependent_flags_update_) {
    bool old_has_non_isolated_descendant_with_blend_mode =
        has_non_isolated_descendant_with_blend_mode_;
    has_visible_self_painting_descendant_ = false;
    has_non_isolated_descendant_with_blend_mode_ = false;
    has_fixed_position_descendant_ = false;
    has_non_contained_absolute_position_descendant_ = false;
    has_stacked_descendant_in_current_stacking_context_ = false;
    has_self_painting_layer_descendant_ = false;

    bool can_contain_abs =
        GetLayoutObject().CanContainAbsolutePositionObjects();

    auto* first_child = [this]() -> PaintLayer* {
      if (GetLayoutObject().ChildPrePaintBlockedByDisplayLock()) {
        GetLayoutObject()
            .GetDisplayLockContext()
            ->NotifyCompositingDescendantDependentFlagUpdateWasBlocked();
        return nullptr;
      }
      return FirstChild();
    }();

    for (PaintLayer* child = first_child; child; child = child->NextSibling()) {
      const ComputedStyle& child_style = child->GetLayoutObject().StyleRef();

      child->UpdateDescendantDependentFlags();

      if ((child->has_visible_content_ && child->IsSelfPaintingLayer()) ||
          child->has_visible_self_painting_descendant_)
        has_visible_self_painting_descendant_ = true;

      has_non_isolated_descendant_with_blend_mode_ |=
          (!child->GetLayoutObject().IsStackingContext() &&
           child->HasNonIsolatedDescendantWithBlendMode()) ||
          child_style.HasBlendMode();

      has_fixed_position_descendant_ |=
          child->HasFixedPositionDescendant() ||
          child_style.GetPosition() == EPosition::kFixed;

      if (!can_contain_abs) {
        has_non_contained_absolute_position_descendant_ |=
            (child->HasNonContainedAbsolutePositionDescendant() ||
             child_style.GetPosition() == EPosition::kAbsolute);
      }

      if (!has_stacked_descendant_in_current_stacking_context_) {
        if (child->GetLayoutObject().IsStacked()) {
          has_stacked_descendant_in_current_stacking_context_ = true;
        } else if (!child->GetLayoutObject().IsStackingContext()) {
          has_stacked_descendant_in_current_stacking_context_ =
              child->has_stacked_descendant_in_current_stacking_context_;
        }
      }

      has_self_painting_layer_descendant_ =
          has_self_painting_layer_descendant_ ||
          child->HasSelfPaintingLayerDescendant() ||
          child->IsSelfPaintingLayer();
    }

    UpdateStackingNode();

    if (old_has_non_isolated_descendant_with_blend_mode !=
        static_cast<bool>(has_non_isolated_descendant_with_blend_mode_)) {
      // The LayoutView DisplayItemClient owns painting of the background
      // of the HTML element. When blending isolation of the HTML element's
      // descendants change, there will be an addition or removal of an
      // isolation effect node for the HTML element to add (or remove)
      // isolated blending, and that case we need to re-paint the LayoutView.
      if (Parent() && Parent()->IsRootLayer())
        GetLayoutObject().View()->SetBackgroundNeedsFullPaintInvalidation();
      GetLayoutObject().SetNeedsPaintPropertyUpdate();
    }
    needs_descendant_dependent_flags_update_ = false;

    if (IsSelfPaintingLayer() && needs_visual_overflow_recalc_) {
      PhysicalRect old_visual_rect =
          PhysicalVisualOverflowRectAllowingUnset(GetLayoutObject());
      GetLayoutObject().RecalcVisualOverflow();
      if (old_visual_rect != GetLayoutObject().PhysicalVisualOverflowRect())
        MarkAncestorChainForFlagsUpdate(kDoesNotNeedDescendantDependentUpdate);
    }
    needs_visual_overflow_recalc_ = false;
  }

  bool previously_has_visible_content = has_visible_content_;
  if (GetLayoutObject().StyleRef().Visibility() == EVisibility::kVisible) {
    has_visible_content_ = true;
  } else {
    // layer may be hidden but still have some visible content, check for this
    has_visible_content_ = false;
    LayoutObject* r = GetLayoutObject().SlowFirstChild();
    while (r) {
      if (r->StyleRef().Visibility() == EVisibility::kVisible &&
          (!r->HasLayer() || !r->EnclosingLayer()->IsSelfPaintingLayer())) {
        has_visible_content_ = true;
        break;
      }
      LayoutObject* layout_object_first_child = r->SlowFirstChild();
      if (layout_object_first_child &&
          (!r->HasLayer() || !r->EnclosingLayer()->IsSelfPaintingLayer())) {
        r = layout_object_first_child;
      } else if (r->NextSibling()) {
        r = r->NextSibling();
      } else {
        do {
          r = r->Parent();
          if (r == &GetLayoutObject())
            r = nullptr;
        } while (r && !r->NextSibling());
        if (r)
          r = r->NextSibling();
      }
    }
  }

  if (HasVisibleContent() != previously_has_visible_content) {
    // We need to tell layout_object_ to recheck its rect because we pretend
    // that invisible LayoutObjects have 0x0 rects. Changing visibility
    // therefore changes our rect and we need to visit this LayoutObject during
    // the PrePaintTreeWalk.
    layout_object_->SetShouldCheckForPaintInvalidation();
  }

  Update3DTransformedDescendantStatus();
}

void PaintLayer::Update3DTransformedDescendantStatus() {
  has3d_transformed_descendant_ = false;

  // Transformed or preserve-3d descendants can only be in the z-order lists,
  // not in the normal flow list, so we only need to check those.
  PaintLayerPaintOrderIterator iterator(this, kStackedChildren);
  while (PaintLayer* child_layer = iterator.Next()) {
    bool child_has3d = false;
    // If the child lives in a 3d hierarchy, then the layer at the root of
    // that hierarchy needs the m_has3DTransformedDescendant set.
    if (child_layer->Preserves3D() &&
        (child_layer->Has3DTransform() ||
         child_layer->Has3DTransformedDescendant()))
      child_has3d = true;
    else if (child_layer->Has3DTransform())
      child_has3d = true;

    if (child_has3d) {
      has3d_transformed_descendant_ = true;
      break;
    }
  }
}

void PaintLayer::UpdateLayerPosition() {
  // LayoutBoxes will call UpdateSizeAndScrollingAfterLayout() from
  // LayoutBox::UpdateAfterLayout, but LayoutInlines will still need to update
  // their size.
  if (GetLayoutObject().IsLayoutInline())
    UpdateSize();
  PhysicalOffset local_point;
  if (LayoutBox* box = GetLayoutBox()) {
    local_point += box->PhysicalLocation();
  }

  if (!GetLayoutObject().IsOutOfFlowPositioned() &&
      !GetLayoutObject().IsColumnSpanAll()) {
    // We must adjust our position by walking up the layout tree looking for the
    // nearest enclosing object with a layer.
    LayoutObject* curr = GetLayoutObject().Container();
    while (curr && !curr->HasLayer()) {
      if (curr->IsBox() && !curr->IsLegacyTableRow()) {
        // Rows and cells share the same coordinate space (that of the section).
        // Omit them when computing our xpos/ypos.
        local_point += To<LayoutBox>(curr)->PhysicalLocation();
      }
      curr = curr->Container();
    }
    if (curr && curr->IsLegacyTableRow()) {
      // Put ourselves into the row coordinate space.
      local_point -= To<LayoutBox>(curr)->PhysicalLocation();
    }
  }

  if (PaintLayer* containing_layer = ContainingLayer()) {
    auto& container = containing_layer->GetLayoutObject();
    if (GetLayoutObject().IsOutOfFlowPositioned() &&
        container.IsLayoutInline() &&
        container.CanContainOutOfFlowPositionedElement(
            GetLayoutObject().StyleRef().GetPosition())) {
      // Adjust offset for absolute under in-flow positioned inline.
      PhysicalOffset offset =
          To<LayoutInline>(container).OffsetForInFlowPositionedInline(
              To<LayoutBox>(GetLayoutObject()));
      local_point += offset;
    }
  }

  if (GetLayoutObject().IsInFlowPositioned() &&
      GetLayoutObject().IsRelPositioned()) {
    auto new_offset = GetLayoutObject().OffsetForInFlowPosition();
    if (rare_data_ || !new_offset.IsZero())
      EnsureRareData().offset_for_in_flow_rel_position = new_offset;
  } else if (rare_data_) {
    rare_data_->offset_for_in_flow_rel_position = PhysicalOffset();
  }
  location_without_position_offset_ = local_point;

#if DCHECK_IS_ON()
  needs_position_update_ = false;
#endif
}

bool PaintLayer::UpdateSize() {
  LayoutSize old_size = size_;
  if (IsRootLayer()) {
    size_ = LayoutSize(GetLayoutObject().GetDocument().View()->Size());
  } else if (GetLayoutObject().IsInline() &&
             GetLayoutObject().IsLayoutInline()) {
    auto& inline_flow = To<LayoutInline>(GetLayoutObject());
    gfx::Rect line_box =
        ToEnclosingRect(inline_flow.PhysicalLinesBoundingBox());
    size_ = LayoutSize(line_box.size());
  } else if (LayoutBox* box = GetLayoutBox()) {
    size_ = box->Size();
  }

  return old_size != size_;
}

void PaintLayer::UpdateSizeAndScrollingAfterLayout() {
  bool did_resize = UpdateSize();
  if (RequiresScrollableArea()) {
    DCHECK(scrollable_area_);
    scrollable_area_->UpdateAfterLayout();
    if (did_resize)
      scrollable_area_->VisibleSizeChanged();
  }
}

PaintLayer* PaintLayer::ContainingLayer(const PaintLayer* ancestor,
                                        bool* skipped_ancestor) const {
  // If we have specified an ancestor, surely the caller needs to know whether
  // we skipped it.
  DCHECK(!ancestor || skipped_ancestor);
  if (skipped_ancestor)
    *skipped_ancestor = false;

  LayoutObject& layout_object = GetLayoutObject();
  if (layout_object.IsOutOfFlowPositioned()) {
    // In NG, the containing block chain goes directly from a column spanner to
    // the multi-column container. Thus, for an OOF nested inside a spanner, we
    // need to find its containing layer through its containing block to handle
    // this case correctly. Therefore, we technically only need to take this
    // path for OOFs inside an NG spanner. However, doing so for all OOF
    // descendants of a multicol container is reasonable enough.
    if (layout_object.IsInsideFlowThread())
      return SlowContainingLayer(ancestor, skipped_ancestor, &layout_object);
    auto can_contain_this_layer =
        layout_object.IsFixedPositioned()
            ? &LayoutObject::CanContainFixedPositionObjects
            : &LayoutObject::CanContainAbsolutePositionObjects;

    PaintLayer* curr = Parent();
    while (curr && !((&curr->GetLayoutObject())->*can_contain_this_layer)()) {
      if (skipped_ancestor && curr == ancestor)
        *skipped_ancestor = true;
      curr = curr->Parent();
    }
    return curr;
  }

  // If the parent layer is not a block, there might be floating objects
  // between this layer (included) and parent layer which need to escape the
  // inline parent to find the actual containing layer through the containing
  // block chain.
  // Column span need to find the containing layer through its containing block.
  if ((!Parent() || Parent()->GetLayoutObject().IsLayoutBlock()) &&
      !layout_object.IsColumnSpanAll())
    return Parent();

  return SlowContainingLayer(ancestor, skipped_ancestor, &layout_object);
}

PaintLayer* PaintLayer::CompositingContainer() const {
  if (IsReplacedNormalFlowStacking())
    return Parent();
  if (!GetLayoutObject().IsStacked()) {
    if (IsSelfPaintingLayer() || GetLayoutObject().IsColumnSpanAll())
      return Parent();
    return ContainingLayer();
  }
  return AncestorStackingContext();
}

PaintLayer* PaintLayer::AncestorStackingContext() const {
  for (PaintLayer* ancestor = Parent(); ancestor;
       ancestor = ancestor->Parent()) {
    if (ancestor->GetLayoutObject().IsStackingContext())
      return ancestor;
  }
  return nullptr;
}

void PaintLayer::SetNeedsCompositingInputsUpdate() {
  // TODO(chrishtr): These are a bit of a heavy hammer, because not all
  // things which require compositing inputs update require a descendant-
  // dependent flags update. Reduce call sites after CAP launch allows
  /// removal of CompositingInputsUpdater.
  MarkAncestorChainForFlagsUpdate();
}

void PaintLayer::ScrollContainerStatusChanged() {
  SetNeedsCompositingInputsUpdate();

  // Invalidate sticky layers and anchor positioned layers in ancestor
  // scrollable areas. We could invalidate only the affected scrollable areas,
  // but it's complicated considering the change of containing block
  // relationship for out-of-flow descendants. This function is called rarely.
  for (auto* layer = this; layer; layer = layer->Parent()) {
    if (auto* scrollable_area = layer->GetScrollableArea()) {
      scrollable_area->InvalidateAllStickyConstraints();
      scrollable_area->InvalidateAllAnchorPositionedLayers();
    }
  }

  // Make sure UpdateLayerPositionsAfterLayout() will be called to update
  // sticky and anchor positioned layers.
  if (auto* frame_view = GetLayoutObject().GetFrameView())
    frame_view->SetNeedsLayout();
}

void PaintLayer::SetNeedsVisualOverflowRecalc() {
  DCHECK(IsSelfPaintingLayer());
#if DCHECK_IS_ON()
  GetLayoutObject().InvalidateVisualOverflow();
#endif
  needs_visual_overflow_recalc_ = true;
  MarkAncestorChainForFlagsUpdate();
}

bool PaintLayer::HasNonIsolatedDescendantWithBlendMode() const {
  DCHECK(!needs_descendant_dependent_flags_update_);
  if (has_non_isolated_descendant_with_blend_mode_)
    return true;
  if (GetLayoutObject().IsSVGRoot()) {
    return To<LayoutSVGRoot>(GetLayoutObject())
        .HasNonIsolatedBlendingDescendants();
  }
  return false;
}

void PaintLayer::AddChild(PaintLayer* child, PaintLayer* before_child) {
#if DCHECK_IS_ON()
  DCHECK(layer_list_mutation_allowed_);
#endif

  PaintLayer* prev_sibling =
      before_child ? before_child->PreviousSibling() : LastChild();
  if (prev_sibling) {
    child->SetPreviousSibling(prev_sibling);
    prev_sibling->SetNextSibling(child);
    DCHECK_NE(prev_sibling, child);
  } else {
    SetFirstChild(child);
  }

  if (before_child) {
    before_child->SetPreviousSibling(child);
    child->SetNextSibling(before_child);
    DCHECK_NE(before_child, child);
  } else {
    SetLastChild(child);
  }

  child->parent_ = this;

  if (child->GetLayoutObject().IsStacked() || child->FirstChild()) {
    // Dirty the z-order list in which we are contained. The
    // ancestorStackingContextNode() can be null in the case where we're
    // building up generated content layers. This is ok, since the lists will
    // start off dirty in that case anyway.
    child->DirtyStackingContextZOrderLists();
  }

  // Non-self-painting children paint into this layer, so the visible contents
  // status of this layer is affected.
  if (!child->IsSelfPaintingLayer())
    DirtyVisibleContentStatus();

  MarkAncestorChainForFlagsUpdate();

  if (child->SelfNeedsRepaint())
    MarkCompositingContainerChainForNeedsRepaint();
  else
    child->SetNeedsRepaint();

  if (child->NeedsCullRectUpdate()) {
    if (RuntimeEnabledFeatures::ScrollUpdateOptimizationsEnabled())
      SetDescendantNeedsCullRectUpdate();
    else
      MarkCompositingContainerChainForNeedsCullRectUpdate();
  } else {
    child->SetNeedsCullRectUpdate();
  }
}

void PaintLayer::RemoveChild(PaintLayer* old_child) {
#if DCHECK_IS_ON()
  DCHECK(layer_list_mutation_allowed_);
#endif

  old_child->MarkCompositingContainerChainForNeedsRepaint();

  if (old_child->PreviousSibling())
    old_child->PreviousSibling()->SetNextSibling(old_child->NextSibling());
  if (old_child->NextSibling())
    old_child->NextSibling()->SetPreviousSibling(old_child->PreviousSibling());

  if (first_ == old_child)
    first_ = old_child->NextSibling();
  if (last_ == old_child)
    last_ = old_child->PreviousSibling();

  if (!GetLayoutObject().DocumentBeingDestroyed()) {
    // Dirty the z-order list in which we are contained.
    old_child->DirtyStackingContextZOrderLists();
    MarkAncestorChainForFlagsUpdate();

    if (old_child->GetLayoutObject()
            .StyleRef()
            .HasStickyConstrainedPosition()) {
      if (const auto* scroll_container =
              old_child->ContainingScrollContainerLayer()) {
        scroll_container->GetScrollableArea()->RemoveStickyLayer(old_child);
      }
    }
  }

  if (GetLayoutObject().StyleRef().Visibility() != EVisibility::kVisible)
    DirtyVisibleContentStatus();

  old_child->SetPreviousSibling(nullptr);
  old_child->SetNextSibling(nullptr);
  old_child->parent_ = nullptr;

  if (old_child->has_visible_content_ ||
      old_child->has_visible_self_painting_descendant_)
    MarkAncestorChainForFlagsUpdate();

  if (old_child->EnclosingPaginationLayer())
    old_child->ClearPaginationRecursive();
}

void PaintLayer::RemoveOnlyThisLayerAfterStyleChange(
    const ComputedStyle* old_style) {
  if (!parent_)
    return;

  if (old_style) {
    if (GetLayoutObject().IsStacked(*old_style))
      DirtyStackingContextZOrderLists();

    if (PaintLayerPainter::PaintedOutputInvisible(*old_style)) {
      // PaintedOutputInvisible() was true because opacity was near zero, and
      // this layer is to be removed because opacity becomes 1. Do the same as
      // StyleDidChange() on change of PaintedOutputInvisible().
      GetLayoutObject().SetSubtreeShouldDoFullPaintInvalidation();
    }
  }

  if (IsSelfPaintingLayer()) {
    if (PaintLayer* enclosing_self_painting_layer =
            parent_->EnclosingSelfPaintingLayer())
      enclosing_self_painting_layer->MergeNeedsPaintPhaseFlagsFrom(*this);
  }

  PaintLayer* next_sib = NextSibling();

  // Now walk our kids and reattach them to our parent.
  PaintLayer* current = first_;
  while (current) {
    PaintLayer* next = current->NextSibling();
    RemoveChild(current);
    parent_->AddChild(current, next_sib);
    current = next;
  }

  // Remove us from the parent.
  parent_->RemoveChild(this);
  layout_object_->DestroyLayer();
}

void PaintLayer::InsertOnlyThisLayerAfterStyleChange() {
  if (!parent_ && GetLayoutObject().Parent()) {
    // We need to connect ourselves when our layoutObject() has a parent.
    // Find our enclosingLayer and add ourselves.
    PaintLayer* parent_layer = GetLayoutObject().Parent()->EnclosingLayer();
    DCHECK(parent_layer);
    PaintLayer* before_child = GetLayoutObject().Parent()->FindNextLayer(
        parent_layer, &GetLayoutObject());
    parent_layer->AddChild(this, before_child);
  }

  // Remove all descendant layers from the hierarchy and add them to the new
  // position.
  for (LayoutObject* curr = GetLayoutObject().SlowFirstChild(); curr;
       curr = curr->NextSibling())
    curr->MoveLayers(parent_, this);

  if (IsSelfPaintingLayer() && parent_) {
    if (PaintLayer* enclosing_self_painting_layer =
            parent_->EnclosingSelfPaintingLayer())
      MergeNeedsPaintPhaseFlagsFrom(*enclosing_self_painting_layer);
  }
}

// Returns the layer reached on the walk up towards the ancestor.
static inline const PaintLayer* AccumulateOffsetTowardsAncestor(
    const PaintLayer* layer,
    const PaintLayer* ancestor_layer,
    PhysicalOffset& location) {
  DCHECK(ancestor_layer != layer);

  const LayoutBoxModelObject& layout_object = layer->GetLayoutObject();

  if (layout_object.IsFixedPositioned() &&
      (!ancestor_layer || ancestor_layer == layout_object.View()->Layer())) {
    // If the fixed layer's container is the root, just add in the offset of the
    // view. We can obtain this by calling localToAbsolute() on the LayoutView.
    location +=
        layout_object.LocalToAbsolutePoint(PhysicalOffset(), kIgnoreTransforms);
    return ancestor_layer;
  }

  bool found_ancestor_first = false;
  PaintLayer* containing_layer =
      ancestor_layer
          ? layer->ContainingLayer(ancestor_layer, &found_ancestor_first)
          : layer->ContainingLayer(ancestor_layer, nullptr);

  if (found_ancestor_first) {
    // Found ancestorLayer before the containing layer, so compute offset of
    // both relative to the container and subtract.
    PhysicalOffset this_coords;
    layer->ConvertToLayerCoords(containing_layer, this_coords);

    PhysicalOffset ancestor_coords;
    ancestor_layer->ConvertToLayerCoords(containing_layer, ancestor_coords);

    location += (this_coords - ancestor_coords);
    return ancestor_layer;
  }

  if (!containing_layer)
    return nullptr;

  location += layer->LocationWithoutPositionOffset();
  if (layer->GetLayoutObject().IsRelPositioned()) {
    location += layer->OffsetForInFlowRelPosition();
  } else if (layer->GetLayoutObject().IsInFlowPositioned()) {
    location += layer->GetLayoutObject().OffsetForInFlowPosition();
  } else if (layer->GetLayoutObject().IsBox() &&
             layer->GetLayoutBox()->AnchorScrollObject()) {
    location += layer->GetLayoutBox()->ComputeAnchorScrollOffset();
  }
  location -=
      PhysicalOffset(containing_layer->PixelSnappedScrolledContentOffset());

  return containing_layer;
}

void PaintLayer::ConvertToLayerCoords(const PaintLayer* ancestor_layer,
                                      PhysicalOffset& location) const {
  if (ancestor_layer == this)
    return;

  const PaintLayer* curr_layer = this;
  while (curr_layer && curr_layer != ancestor_layer)
    curr_layer =
        AccumulateOffsetTowardsAncestor(curr_layer, ancestor_layer, location);
}

void PaintLayer::ConvertToLayerCoords(const PaintLayer* ancestor_layer,
                                      PhysicalRect& rect) const {
  PhysicalOffset delta;
  ConvertToLayerCoords(ancestor_layer, delta);
  rect.Move(delta);
}

PhysicalOffset PaintLayer::VisualOffsetFromAncestor(
    const PaintLayer* ancestor_layer,
    PhysicalOffset offset) const {
  if (ancestor_layer == this)
    return offset;
  PaintLayer* pagination_layer = EnclosingPaginationLayer();
  if (pagination_layer == this)
    pagination_layer = Parent()->EnclosingPaginationLayer();
  if (!pagination_layer) {
    ConvertToLayerCoords(ancestor_layer, offset);
    return offset;
  }

  auto& flow_thread = To<LayoutFlowThread>(pagination_layer->GetLayoutObject());
  ConvertToLayerCoords(pagination_layer, offset);
  offset = PhysicalOffsetToBeNoop(
      flow_thread.FlowThreadPointToVisualPoint(offset.ToLayoutPoint()));
  if (ancestor_layer == pagination_layer)
    return offset;

  if (ancestor_layer->EnclosingPaginationLayer() != pagination_layer) {
    offset += pagination_layer->VisualOffsetFromAncestor(ancestor_layer);
  } else {
    // The ancestor layer is also inside the pagination layer, so we need to
    // subtract the visual distance from the ancestor layer to the pagination
    // layer.
    offset -= ancestor_layer->VisualOffsetFromAncestor(pagination_layer);
  }
  return offset;
}

void PaintLayer::DidUpdateScrollsOverflow() {
  UpdateSelfPaintingLayer();
}

void PaintLayer::UpdateStackingNode() {
#if DCHECK_IS_ON()
  DCHECK(layer_list_mutation_allowed_);
#endif

  bool needs_stacking_node =
      has_stacked_descendant_in_current_stacking_context_ &&
      GetLayoutObject().IsStackingContext();

  if (needs_stacking_node != !!stacking_node_) {
    if (needs_stacking_node) {
      stacking_node_ = MakeGarbageCollected<PaintLayerStackingNode>(this);
    } else {
      stacking_node_.Clear();
    }
  }

  if (stacking_node_)
    stacking_node_->UpdateZOrderLists();
}

bool PaintLayer::RequiresScrollableArea() const {
  if (!GetLayoutBox())
    return false;
  if (GetLayoutObject().IsScrollContainer())
    return true;
  // Iframes with the resize property can be resized. This requires
  // scroll corner painting, which is implemented, in part, by
  // PaintLayerScrollableArea.
  if (GetLayoutBox()->CanResize())
    return true;
  // With custom scrollbars unfortunately we may need a PaintLayerScrollableArea
  // to be able to calculate the size of scrollbar gutters.
  const ComputedStyle& style = GetLayoutObject().StyleRef();
  if (style.IsScrollbarGutterStable() &&
      style.OverflowBlockDirection() == EOverflow::kHidden &&
      style.HasPseudoElementStyle(kPseudoIdScrollbar)) {
    return true;
  }
  return false;
}

void PaintLayer::UpdateScrollableArea() {
  if (RequiresScrollableArea() == !!scrollable_area_)
    return;

  if (!scrollable_area_) {
    scrollable_area_ = MakeGarbageCollected<PaintLayerScrollableArea>(*this);
  } else {
    scrollable_area_->Dispose();
    scrollable_area_.Clear();
    GetLayoutObject().SetBackgroundPaintLocation(
        kBackgroundPaintInBorderBoxSpace);
  }

  GetLayoutObject().SetNeedsPaintPropertyUpdate();
  // To clear z-ordering information of overlay overflow controls.
  if (NeedsReorderOverlayOverflowControls())
    DirtyStackingContextZOrderLists();
}

void PaintLayer::AppendSingleFragmentForHitTesting(
    PaintLayerFragments& fragments,
    const PaintLayerFragment* container_fragment,
    ShouldRespectOverflowClipType respect_overflow_clip) const {
  PaintLayerFragment fragment;
  if (container_fragment) {
    fragment = *container_fragment;
  } else {
    fragment.fragment_data = &GetLayoutObject().FirstFragment();
    if (GetLayoutObject().CanTraversePhysicalFragments()) {
      // Make sure that we actually traverse the fragment tree, by providing a
      // physical fragment. Otherwise we'd fall back to LayoutObject traversal.
      if (const auto* layout_box = GetLayoutBox())
        fragment.physical_fragment = layout_box->GetPhysicalFragment(0);
    }
    fragment.fragment_idx = 0;
  }

  ClipRectsContext clip_rects_context(this, fragment.fragment_data,
                                      kExcludeOverlayScrollbarSizeForHitTesting,
                                      respect_overflow_clip);
  Clipper(GeometryMapperOption::kUseGeometryMapper)
      .CalculateRects(clip_rects_context, fragment.fragment_data,
                      fragment.layer_offset, fragment.background_rect,
                      fragment.foreground_rect);

  fragments.push_back(fragment);
}

const LayoutBox* PaintLayer::GetLayoutBoxWithBlockFragments() const {
  const LayoutBox* layout_box = GetLayoutBox();
  if (!layout_box)
    return nullptr;
  if (!layout_box->CanTraversePhysicalFragments())
    return nullptr;
  if (!layout_box->PhysicalFragmentCount()) {
    NOTREACHED();
    // TODO(crbug.com/1273068): The box has no fragments. This is
    // unexpected, and we must have failed a bunch of DCHECKs (if enabled)
    // on our way here. If the LayoutBox has never been laid out, it will
    // have no fragments. But then we shouldn't really be here. Fall back to
    // legacy LayoutObject tree traversal for this layer.
    return nullptr;
  }
  return layout_box;
}

void PaintLayer::CollectFragments(
    PaintLayerFragments& fragments,
    const PaintLayer* root_layer,
    ShouldRespectOverflowClipType respect_overflow_clip,
    const FragmentData* root_fragment_arg) const {
  PaintLayerFragment fragment;
  const auto& first_fragment_data = GetLayoutObject().FirstFragment();
  const auto& first_root_fragment_data =
      root_layer->GetLayoutObject().FirstFragment();

  const LayoutBox* layout_box_with_fragments = GetLayoutBoxWithBlockFragments();

  // The NG hit-testing code guards against painting multiple fragments for
  // content that doesn't support it, but the legacy hit-testing code has no
  // such guards.
  // TODO(crbug.com/1229581): Remove this when everything is handled by NG.
  bool multiple_fragments_allowed =
      layout_box_with_fragments || CanPaintMultipleFragments(GetLayoutObject());

  // The inherited offset_from_root does not include any pagination offsets.
  // In the presence of fragmentation, we cannot use it.
  wtf_size_t physical_fragment_idx = 0u;
  for (auto* fragment_data = &first_fragment_data; fragment_data;
       fragment_data = fragment_data->NextFragment(), physical_fragment_idx++) {
    const FragmentData* root_fragment_data = nullptr;
    if (root_fragment_arg) {
      DCHECK(this != root_layer);
      if (!root_fragment_arg->ContentsProperties().Transform().IsAncestorOf(
              fragment_data->LocalBorderBoxProperties().Transform())) {
        // We only want to collect fragments that are descendants of
        // |root_fragment_arg|.
        continue;
      }
      root_fragment_data = root_fragment_arg;
    } else if (root_layer == this) {
      root_fragment_data = fragment_data;
    } else {
      root_fragment_data = &first_root_fragment_data;
    }

    ClipRectsContext clip_rects_context(
        root_layer, root_fragment_data,
        kExcludeOverlayScrollbarSizeForHitTesting, respect_overflow_clip,
        PhysicalOffset());

    Clipper(GeometryMapperOption::kUseGeometryMapper)
        .CalculateRects(clip_rects_context, fragment_data,
                        fragment.layer_offset, fragment.background_rect,
                        fragment.foreground_rect);

    fragment.fragment_data = fragment_data;

    if (layout_box_with_fragments) {
      fragment.physical_fragment =
          layout_box_with_fragments->GetPhysicalFragment(physical_fragment_idx);
      DCHECK(fragment.physical_fragment);
    }

    fragment.fragment_idx = physical_fragment_idx;

    fragments.push_back(fragment);

    if (!multiple_fragments_allowed)
      break;
  }
}

PaintLayer::HitTestRecursionData::HitTestRecursionData(
    const PhysicalRect& rect_arg,
    const HitTestLocation& location_arg,
    const HitTestLocation& original_location_arg)
    : rect(rect_arg),
      location(location_arg),
      original_location(original_location_arg),
      intersects_location(location_arg.Intersects(rect_arg)) {}

bool PaintLayer::HitTest(const HitTestLocation& hit_test_location,
                         HitTestResult& result,
                         const PhysicalRect& hit_test_area) {
  // The root PaintLayer of HitTest must contain all descendants.
  DCHECK(GetLayoutObject().CanContainFixedPositionObjects());
  DCHECK(GetLayoutObject().CanContainAbsolutePositionObjects());

  // LayoutView should make sure to update layout before entering hit testing
  DCHECK(!GetLayoutObject().GetFrame()->View()->LayoutPending());
  DCHECK(!GetLayoutObject().GetDocument().GetLayoutView()->NeedsLayout());

  const HitTestRequest& request = result.GetHitTestRequest();

  HitTestRecursionData recursion_data(hit_test_area, hit_test_location,
                                      hit_test_location);
  PaintLayer* inside_layer = HitTestLayer(*this, /*container_fragment*/ nullptr,
                                          result, recursion_data);
  if (!inside_layer && IsRootLayer()) {
    bool fallback = false;
    // If we didn't hit any layers but are still inside the document
    // bounds, then we should fallback to hitting the document.
    // For rect-based hit test, we do the fallback only when the hit-rect
    // is totally within the document bounds.
    if (hit_test_area.Contains(hit_test_location.BoundingBox())) {
      fallback = true;

      // Mouse dragging outside the main document should also be
      // delivered to the document.
      // TODO(miletus): Capture behavior inconsistent with iframes
      // crbug.com/522109.
      // TODO(majidvp): This should apply more consistently across different
      // event types and we should not use RequestType for it. Perhaps best for
      // it to be done at a higher level. See http://crbug.com/505825
    } else if ((request.Active() || request.Release()) &&
               !request.IsChildFrameHitTest()) {
      fallback = true;
    }
    if (fallback) {
      GetLayoutObject().UpdateHitTestResult(result, hit_test_location.Point());
      inside_layer = this;

      // Don't cache this result since it really wasn't a true hit.
      result.SetCacheable(false);
    }
  }

  // Now determine if the result is inside an anchor - if the urlElement isn't
  // already set.
  Node* node = result.InnerNode();
  if (node && !result.URLElement())
    result.SetURLElement(node->EnclosingLinkEventParentOrSelf());

  // Now return whether we were inside this layer (this will always be true for
  // the root layer).
  return inside_layer;
}

Node* PaintLayer::EnclosingNode() const {
  for (LayoutObject* r = &GetLayoutObject(); r; r = r->Parent()) {
    if (Node* e = r->GetNode())
      return e;
  }
  NOTREACHED();
  return nullptr;
}

bool PaintLayer::IsInTopLayer() const {
  auto* element = DynamicTo<Element>(GetLayoutObject().GetNode());
  return element && element->IsInTopLayer();
}

// Compute the z-offset of the point in the transformState.
// This is effectively projecting a ray normal to the plane of ancestor, finding
// where that ray intersects target, and computing the z delta between those two
// points.
static double ComputeZOffset(const HitTestingTransformState& transform_state) {
  // We got an affine transform, so no z-offset
  if (transform_state.AccumulatedTransform().IsAffine())
    return 0;

  // Flatten the point into the target plane
  gfx::PointF target_point = transform_state.MappedPoint();

  // Now map the point back through the transform, which computes Z.
  gfx::Point3F backmapped_point =
      transform_state.AccumulatedTransform().MapPoint(
          gfx::Point3F(target_point));
  return backmapped_point.z();
}

HitTestingTransformState PaintLayer::CreateLocalTransformState(
    const PaintLayer& transform_container,
    const FragmentData& transform_container_fragment,
    const FragmentData& local_fragment,
    const HitTestRecursionData& recursion_data,
    const HitTestingTransformState* container_transform_state) const {
  // If we're already computing transform state, then it's relative to the
  // container (which we know is non-null).
  // If this is the first time we need to make transform state, then base it
  // off of hitTestLocation, which is relative to rootLayer.
  HitTestingTransformState transform_state =
      container_transform_state
          ? *container_transform_state
          : HitTestingTransformState(
                recursion_data.location.TransformedPoint(),
                recursion_data.location.TransformedRect(),
                gfx::QuadF(gfx::RectF(recursion_data.rect)));

  if (&transform_container == this) {
    DCHECK(!container_transform_state);
    return transform_state;
  }

  if (container_transform_state &&
      (!transform_container.Preserves3D() ||
       &transform_container.GetLayoutObject() !=
           GetLayoutObject().NearestAncestorForElement())) {
    // The transform container layer doesn't preserve 3d, or its preserve-3d
    // doesn't apply to this layer because our element is not a child of the
    // transform container layer's element.
    transform_state.Flatten();
  }

  DCHECK_NE(&transform_container_fragment, &local_fragment);

  const auto* container_transform =
      &transform_container_fragment.LocalBorderBoxProperties().Transform();
  if (const auto* properties = transform_container_fragment.PaintProperties()) {
    if (const auto* perspective = properties->Perspective()) {
      transform_state.ApplyTransform(*perspective);
      container_transform = perspective;
    }
  }

  transform_state.Translate(
      gfx::Vector2dF(-transform_container_fragment.PaintOffset()));
  transform_state.ApplyTransform(GeometryMapper::SourceToDestinationProjection(
      local_fragment.PreTransform(), *container_transform));
  transform_state.Translate(gfx::Vector2dF(local_fragment.PaintOffset()));

  if (const auto* properties = local_fragment.PaintProperties()) {
    for (const TransformPaintPropertyNode* transform :
         properties->AllCSSTransformPropertiesOutsideToInside()) {
      if (transform)
        transform_state.ApplyTransform(*transform);
    }
  }

  return transform_state;
}

static bool IsHitCandidateForDepthOrder(
    const PaintLayer* hit_layer,
    bool can_depth_sort,
    double* z_offset,
    const HitTestingTransformState* transform_state) {
  if (!hit_layer)
    return false;

  // The hit layer is depth-sorting with other layers, so just say that it was
  // hit.
  if (can_depth_sort)
    return true;

  // We need to look at z-depth to decide if this layer was hit.
  //
  // See comment in PaintLayer::HitTestLayer regarding SVG
  // foreignObject; if it weren't for that case we could test z_offset
  // and then DCHECK(transform_state) inside of it.
  DCHECK(!z_offset || transform_state ||
         hit_layer->GetLayoutObject().IsSVGForeignObjectIncludingNG());
  if (z_offset && transform_state) {
    // This is actually computing our z, but that's OK because the hitLayer is
    // coplanar with us.
    double child_z_offset = ComputeZOffset(*transform_state);
    if (child_z_offset > *z_offset) {
      *z_offset = child_z_offset;
      return true;
    }
    return false;
  }

  return true;
}

// Calling IsDescendantOf is sad (slow), but it's the only way to tell
// whether a hit test candidate is a descendant of the stop node.
static bool IsHitCandidateForStopNode(const LayoutObject& candidate,
                                      const LayoutObject* stop_node) {
  return !stop_node || (&candidate == stop_node) ||
         !candidate.IsDescendantOf(stop_node);
}

// recursion_data.location and rect are relative to |transform_container|.
// A 'flattening' layer is one preserves3D() == false.
// transform_state.AccumulatedTransform() holds the transform from the
// containing flattening layer.
// transform_state.last_planar_point_ is the hit test location in the plane of
// the containing flattening layer.
// transform_state.last_planar_quad_ is the hit test rect as a quad in the
// plane of the containing flattening layer.
//
// If z_offset is non-null (which indicates that the caller wants z offset
// information), *z_offset on return is the z offset of the hit point relative
// to the containing flattening layer.
//
// If |container_fragment| is null, we'll hit test all fragments. Otherwise it
// points to a fragment of |transform_container|, and descendants should hit
// test their fragments that are descendants of |container_fragment|.
PaintLayer* PaintLayer::HitTestLayer(
    const PaintLayer& transform_container,
    const PaintLayerFragment* container_fragment,
    HitTestResult& result,
    const HitTestRecursionData& recursion_data,
    bool applied_transform,
    HitTestingTransformState* container_transform_state,
    double* z_offset,
    bool overflow_controls_only) {
  const FragmentData* container_fragment_data =
      container_fragment ? container_fragment->fragment_data : nullptr;
  const auto& container_layout_object = transform_container.GetLayoutObject();
  DCHECK(container_layout_object.CanContainFixedPositionObjects());
  DCHECK(container_layout_object.CanContainAbsolutePositionObjects());

  const LayoutObject& layout_object = GetLayoutObject();
  DCHECK_GE(layout_object.GetDocument().Lifecycle().GetState(),
            DocumentLifecycle::kPrePaintClean);

  if (UNLIKELY(layout_object.NeedsLayout() &&
               !layout_object.ChildLayoutBlockedByDisplayLock())) {
    // Skip if we need layout. This should never happen. See crbug.com/1244130
    NOTREACHED();
    return nullptr;
  }

  if (!IsSelfPaintingLayer() && !HasSelfPaintingLayerDescendant())
    return nullptr;

  if ((result.GetHitTestRequest().GetType() &
       HitTestRequest::kIgnoreZeroOpacityObjects) &&
      !layout_object.HasNonZeroEffectiveOpacity()) {
    return nullptr;
  }

  // TODO(vmpstr): We need to add a simple document flag which says whether
  // there is an ongoing transition, since this may be too heavy of a check for
  // each hit test.
  if (auto* supplement = DocumentTransitionSupplement::FromIfExists(
          layout_object.GetDocument())) {
    // This means that the contents of the object are drawn elsewhere.
    if (supplement->GetTransition()->IsRepresentedViaPseudoElements(
            layout_object)) {
      return nullptr;
    }
  }

  ShouldRespectOverflowClipType clip_behavior = kRespectOverflowClip;
  if (result.GetHitTestRequest().IgnoreClipping())
    clip_behavior = kIgnoreOverflowClip;

  // For the global root scroller, hit test the layout viewport scrollbars
  // first, as they are visually presented on top of the content.
  if (layout_object.IsGlobalRootScroller()) {
    // There are a number of early outs below that don't apply to the the
    // global root scroller.
    DCHECK(!Transform());
    DCHECK(!Preserves3D());
    DCHECK(!layout_object.HasClipPath());
    if (scrollable_area_) {
      gfx::Point point = scrollable_area_->ConvertFromRootFrameToVisualViewport(
          ToRoundedPoint(recursion_data.location.Point()));

      DCHECK(GetLayoutBox());
      if (GetLayoutBox()->HitTestOverflowControl(result, HitTestLocation(point),
                                                 PhysicalOffset()))
        return this;
    }
  }

  // We can only reach an SVG foreign object's PaintLayer from
  // LayoutSVGForeignObject::NodeAtFloatPoint (because
  // IsReplacedNormalFlowStacking() true for LayoutSVGForeignObject),
  // where the hit_test_rect has already been transformed to local coordinates.
  bool use_transform = false;
  if (!layout_object.IsSVGForeignObjectIncludingNG() &&
      // Only a layer that can contain all descendants can become a transform
      // container. This excludes layout objects having transform nodes created
      // for animating opacity etc. or for backface-visibility:hidden.
      layout_object.CanContainFixedPositionObjects()) {
    DCHECK(layout_object.CanContainAbsolutePositionObjects());
    if (const auto* properties =
            layout_object.FirstFragment().PaintProperties()) {
      if (properties->HasCSSTransformPropertyNode() ||
          properties->Perspective())
        use_transform = true;
    }
  }

  // Apply a transform if we have one.
  if (use_transform && !applied_transform) {
    return HitTestTransformedLayerInFragments(
        transform_container, container_fragment, result, recursion_data,
        container_transform_state, z_offset, overflow_controls_only,
        clip_behavior);
  }

  // Don't hit test the clip-path area when checking for occlusion. This is
  // necessary because SVG doesn't support rect-based hit testing, so
  // HitTestClippedOutByClipPath may erroneously return true for a rect-based
  // hit test).
  bool is_occlusion_test = result.GetHitTestRequest().GetType() &
                           HitTestRequest::kHitTestVisualOverflow;
  if (!is_occlusion_test && layout_object.HasClipPath() &&
      HitTestClippedOutByClipPath(transform_container,
                                  recursion_data.location)) {
    return nullptr;
  }

  HitTestingTransformState* local_transform_state = nullptr;
  STACK_UNINITIALIZED absl::optional<HitTestingTransformState> storage;

  if (applied_transform) {
    // We computed the correct state in the caller (above code), so just
    // reference it.
    DCHECK(container_transform_state);
    local_transform_state = container_transform_state;
  } else if (container_transform_state || has3d_transformed_descendant_) {
    DCHECK(!Preserves3D());
    // We need transform state for the first time, or to offset the container
    // state, so create it here.
    const FragmentData* local_fragment_for_transform_state =
        &layout_object.FirstFragment();
    const FragmentData* container_fragment_for_transform_state;
    if (container_fragment_data) {
      container_fragment_for_transform_state = container_fragment_data;
      const auto& container_transform =
          container_fragment_data->ContentsProperties().Transform();
      while (local_fragment_for_transform_state) {
        // Find the first local fragment that is a descendant of
        // container_fragment.
        if (container_transform.IsAncestorOf(
                local_fragment_for_transform_state->LocalBorderBoxProperties()
                    .Transform())) {
          break;
        }
        local_fragment_for_transform_state =
            local_fragment_for_transform_state->NextFragment();
      }
      if (!local_fragment_for_transform_state)
        return nullptr;
    } else {
      container_fragment_for_transform_state =
          &container_layout_object.FirstFragment();
    }
    storage = CreateLocalTransformState(
        transform_container, *container_fragment_for_transform_state,
        *local_fragment_for_transform_state, recursion_data,
        container_transform_state);
    local_transform_state = &*storage;
  }

  // Check for hit test on backface if backface-visibility is 'hidden'
  if (local_transform_state && layout_object.StyleRef().BackfaceVisibility() ==
                                   EBackfaceVisibility::kHidden) {
    STACK_UNINITIALIZED TransformationMatrix inverted_matrix =
        local_transform_state->AccumulatedTransform().Inverse();
    // If the z-vector of the matrix is negative, the back is facing towards the
    // viewer. TODO(crbug.com/1359528): Use something like
    // gfx::Transform::IsBackfaceVisible().
    if (inverted_matrix.rc(2, 2) < 0)
      return nullptr;
  }

  // The following are used for keeping track of the z-depth of the hit point of
  // 3d-transformed descendants.
  double local_z_offset = -std::numeric_limits<double>::infinity();
  double* z_offset_for_descendants_ptr = nullptr;
  double* z_offset_for_contents_ptr = nullptr;

  bool depth_sort_descendants = false;
  if (Preserves3D()) {
    depth_sort_descendants = true;
    // Our layers can depth-test with our container, so share the z depth
    // pointer with the container, if it passed one down.
    z_offset_for_descendants_ptr = z_offset ? z_offset : &local_z_offset;
    z_offset_for_contents_ptr = z_offset ? z_offset : &local_z_offset;
  } else if (z_offset) {
    z_offset_for_descendants_ptr = nullptr;
    // Container needs us to give back a z offset for the hit layer.
    z_offset_for_contents_ptr = z_offset;
  }

  // Collect the fragments. This will compute the clip rectangles for each
  // layer fragment.
  PaintLayerFragments layer_fragments;
  ClearCollectionScope<PaintLayerFragments> scope(&layer_fragments);
  if (recursion_data.intersects_location) {
    if (applied_transform) {
      DCHECK_EQ(&transform_container, this);
      AppendSingleFragmentForHitTesting(layer_fragments, container_fragment,
                                        clip_behavior);
    } else {
      CollectFragments(layer_fragments, &transform_container, clip_behavior,
                       container_fragment_data);
    }

    // See if the hit test pos is inside the overflow controls of current layer.
    // This should be done before walking child layers to avoid that the
    // overflow controls are obscured by the positive child layers.
    if (scrollable_area_ &&
        layer_fragments[0].background_rect.Intersects(
            recursion_data.location) &&
        GetLayoutBox()->HitTestOverflowControl(
            result, recursion_data.location, layer_fragments[0].layer_offset)) {
      return this;
    }
  }

  if (overflow_controls_only)
    return nullptr;

  // See if the hit test pos is inside the overflow controls of the child
  // layers that have reordered the painting of the overlay overflow controls.
  if (stacking_node_) {
    for (auto& layer : base::Reversed(
             stacking_node_->OverlayOverflowControlsReorderedList())) {
      if (layer->HitTestLayer(transform_container, container_fragment, result,
                              recursion_data, /*applied_transform*/ false,
                              container_transform_state,
                              z_offset_for_descendants_ptr,
                              /*overflow_controls_only*/ true)) {
        return layer;
      }
    }
  }

  // This variable tracks which layer the mouse ends up being inside.
  PaintLayer* candidate_layer = nullptr;

  // Begin by walking our list of positive layers from highest z-index down to
  // the lowest z-index.
  PaintLayer* hit_layer = HitTestChildren(
      kPositiveZOrderChildren, transform_container, container_fragment, result,
      recursion_data, container_transform_state, z_offset_for_descendants_ptr,
      z_offset, local_transform_state, depth_sort_descendants);
  if (hit_layer) {
    if (!depth_sort_descendants)
      return hit_layer;
    candidate_layer = hit_layer;
  }

  // Now check our overflow objects.
  hit_layer = HitTestChildren(
      kNormalFlowChildren, transform_container, container_fragment, result,
      recursion_data, container_transform_state, z_offset_for_descendants_ptr,
      z_offset, local_transform_state, depth_sort_descendants);
  if (hit_layer) {
    if (!depth_sort_descendants)
      return hit_layer;
    candidate_layer = hit_layer;
  }

  const LayoutObject* stop_node = result.GetHitTestRequest().GetStopNode();
  if (recursion_data.intersects_location) {
    // Next we want to see if the mouse pos is inside the child LayoutObjects of
    // the layer. Check every fragment in reverse order.
    if (IsSelfPaintingLayer() &&
        !layout_object.ChildPaintBlockedByDisplayLock()) {
      // Hit test with a temporary HitTestResult, because we only want to commit
      // to 'result' if we know we're frontmost.
      STACK_UNINITIALIZED HitTestResult temp_result(
          result.GetHitTestRequest(), recursion_data.original_location);
      bool inside_fragment_foreground_rect = false;

      if (HitTestForegroundForFragments(layer_fragments, temp_result,
                                        recursion_data.location,
                                        inside_fragment_foreground_rect) &&
          IsHitCandidateForDepthOrder(this, false, z_offset_for_contents_ptr,
                                      local_transform_state) &&
          IsHitCandidateForStopNode(GetLayoutObject(), stop_node)) {
        if (result.GetHitTestRequest().ListBased())
          result.Append(temp_result);
        else
          result = temp_result;
        if (!depth_sort_descendants)
          return this;
        // Foreground can depth-sort with descendant layers, so keep this as a
        // candidate.
        candidate_layer = this;
      } else if (inside_fragment_foreground_rect &&
                 result.GetHitTestRequest().ListBased() &&
                 IsHitCandidateForStopNode(GetLayoutObject(), stop_node)) {
        result.Append(temp_result);
      }
    }
  }

  // Now check our negative z-index children.
  hit_layer = HitTestChildren(
      kNegativeZOrderChildren, transform_container, container_fragment, result,
      recursion_data, container_transform_state, z_offset_for_descendants_ptr,
      z_offset, local_transform_state, depth_sort_descendants);
  if (hit_layer) {
    if (!depth_sort_descendants)
      return hit_layer;
    candidate_layer = hit_layer;
  }

  // If we found a layer, return. Child layers, and foreground always render
  // in front of background.
  if (candidate_layer)
    return candidate_layer;

  if (recursion_data.intersects_location && IsSelfPaintingLayer()) {
    STACK_UNINITIALIZED HitTestResult temp_result(
        result.GetHitTestRequest(), recursion_data.original_location);
    bool inside_fragment_background_rect = false;
    if (HitTestFragmentsWithPhase(layer_fragments, temp_result,
                                  recursion_data.location,
                                  HitTestPhase::kSelfBlockBackground,
                                  inside_fragment_background_rect) &&
        IsHitCandidateForDepthOrder(this, false, z_offset_for_contents_ptr,
                                    local_transform_state) &&
        IsHitCandidateForStopNode(GetLayoutObject(), stop_node)) {
      if (result.GetHitTestRequest().ListBased())
        result.Append(temp_result);
      else
        result = temp_result;
      return this;
    }
    if (inside_fragment_background_rect &&
        result.GetHitTestRequest().ListBased() &&
        IsHitCandidateForStopNode(GetLayoutObject(), stop_node)) {
      result.Append(temp_result);
    }
  }

  return nullptr;
}

bool PaintLayer::HitTestForegroundForFragments(
    const PaintLayerFragments& layer_fragments,
    HitTestResult& result,
    const HitTestLocation& hit_test_location,
    bool& inside_clip_rect) const {
  if (HitTestFragmentsWithPhase(layer_fragments, result, hit_test_location,
                                HitTestPhase::kForeground, inside_clip_rect)) {
    return true;
  }
  if (inside_clip_rect &&
      HitTestFragmentsWithPhase(layer_fragments, result, hit_test_location,
                                HitTestPhase::kFloat, inside_clip_rect)) {
    return true;
  }
  if (inside_clip_rect &&
      HitTestFragmentsWithPhase(layer_fragments, result, hit_test_location,
                                HitTestPhase::kDescendantBlockBackgrounds,
                                inside_clip_rect)) {
    return true;
  }
  return false;
}

bool PaintLayer::HitTestFragmentsWithPhase(
    const PaintLayerFragments& layer_fragments,
    HitTestResult& result,
    const HitTestLocation& hit_test_location,
    HitTestPhase phase,
    bool& inside_clip_rect) const {
  if (layer_fragments.IsEmpty())
    return false;

  for (int i = layer_fragments.size() - 1; i >= 0; --i) {
    const PaintLayerFragment& fragment = layer_fragments.at(i);
    const ClipRect& bounds = phase == HitTestPhase::kSelfBlockBackground
                                 ? fragment.background_rect
                                 : fragment.foreground_rect;
    if (!bounds.Intersects(hit_test_location))
      continue;

    inside_clip_rect = true;

    if (UNLIKELY(GetLayoutObject().IsLayoutInline() &&
                 GetLayoutObject().CanTraversePhysicalFragments())) {
      // When hit-testing an inline that has a layer, we'll search for it in
      // each fragment of the containing block. Each fragment has its own
      // offset, and we need to do one fragment at a time. If the inline uses a
      // transform, though, we'll only have one PaintLayerFragment in the list
      // at this point (we iterate over them further up on the stack, and pass a
      // "list" of one fragment at a time from there instead).
      DCHECK(fragment.fragment_idx != WTF::kNotFound);
      HitTestLocation location_for_fragment(hit_test_location,
                                            fragment.fragment_idx);
      if (HitTestFragmentWithPhase(result, fragment.physical_fragment,
                                   fragment.layer_offset, location_for_fragment,
                                   phase))
        return true;
    } else if (HitTestFragmentWithPhase(result, fragment.physical_fragment,
                                        fragment.layer_offset,
                                        hit_test_location, phase)) {
      return true;
    }
  }

  return false;
}

PaintLayer* PaintLayer::HitTestTransformedLayerInFragments(
    const PaintLayer& transform_container,
    const PaintLayerFragment* container_fragment,
    HitTestResult& result,
    const HitTestRecursionData& recursion_data,
    HitTestingTransformState* container_transform_state,
    double* z_offset,
    bool overflow_controls_only,
    ShouldRespectOverflowClipType clip_behavior) {
  const FragmentData* container_fragment_data =
      container_fragment ? container_fragment->fragment_data : nullptr;
  PaintLayerFragments fragments;
  ClearCollectionScope<PaintLayerFragments> scope(&fragments);

  CollectFragments(fragments, &transform_container, clip_behavior,
                   container_fragment_data);

  for (const auto& fragment : fragments) {
    // Apply any clips established by layers in between us and the root layer.
    if (!fragment.background_rect.Intersects(recursion_data.location))
      continue;

    PaintLayer* hit_layer = HitTestLayerByApplyingTransform(
        transform_container, container_fragment, fragment, result,
        recursion_data, container_transform_state, z_offset,
        overflow_controls_only);
    if (hit_layer)
      return hit_layer;
  }

  return nullptr;
}

PaintLayer* PaintLayer::HitTestLayerByApplyingTransform(
    const PaintLayer& transform_container,
    const PaintLayerFragment* container_fragment,
    const PaintLayerFragment& local_fragment,
    HitTestResult& result,
    const HitTestRecursionData& recursion_data,
    HitTestingTransformState* root_transform_state,
    double* z_offset,
    bool overflow_controls_only,
    const PhysicalOffset& translation_offset) {
  // Create a transform state to accumulate this transform.
  HitTestingTransformState new_transform_state = CreateLocalTransformState(
      transform_container,
      container_fragment
          ? *container_fragment->fragment_data
          : transform_container.GetLayoutObject().FirstFragment(),
      *local_fragment.fragment_data, recursion_data, root_transform_state);

  // If the transform can't be inverted, then don't hit test this layer at all.
  if (!new_transform_state.AccumulatedTransform().IsInvertible())
    return nullptr;

  // Compute the point and the hit test rect in the coords of this layer by
  // using the values from new_transform_state, which store the point and quad
  // in the coords of the last flattened layer, and the accumulated transform
  // which lets up map through preserve-3d layers.
  //
  // We can't just map HitTestLocation and HitTestRect because they may have
  // been flattened (losing z) by our container.
  gfx::PointF local_point = new_transform_state.MappedPoint();
  PhysicalRect bounds_of_mapped_area = new_transform_state.BoundsOfMappedArea();
  absl::optional<HitTestLocation> new_location;
  if (recursion_data.location.IsRectBasedTest())
    new_location.emplace(local_point, new_transform_state.MappedQuad());
  else
    new_location.emplace(local_point, new_transform_state.BoundsOfMappedQuad());
  HitTestRecursionData new_recursion_data(bounds_of_mapped_area, *new_location,
                                          recursion_data.original_location);

  // Now do a hit test with the transform container shifted to this layer.
  // As an optimization, pass nullptr as the new container_fragment if this
  // layer has only one fragment.
  const auto* new_container_fragment =
      GetLayoutObject().FirstFragment().NextFragment() ? &local_fragment
                                                       : nullptr;
  return HitTestLayer(*this, new_container_fragment, result, new_recursion_data,
                      /*applied_transform*/ true, &new_transform_state,
                      z_offset, overflow_controls_only);
}

bool PaintLayer::HitTestFragmentWithPhase(
    HitTestResult& result,
    const NGPhysicalBoxFragment* physical_fragment,
    const PhysicalOffset& fragment_offset,
    const HitTestLocation& hit_test_location,
    HitTestPhase phase) const {
  DCHECK(IsSelfPaintingLayer() || HasSelfPaintingLayerDescendant());

  bool did_hit;
  if (physical_fragment) {
    if (!physical_fragment->MayIntersect(result, hit_test_location,
                                         fragment_offset)) {
      did_hit = false;
    } else {
      did_hit =
          NGBoxFragmentPainter(*physical_fragment)
              .NodeAtPoint(result, hit_test_location, fragment_offset, phase);
    }
  } else {
    did_hit = GetLayoutObject().NodeAtPoint(result, hit_test_location,
                                            fragment_offset, phase);
  }

  if (!did_hit) {
    // It's wrong to set innerNode, but then claim that you didn't hit anything,
    // unless it is a list-based test.
    DCHECK(!result.InnerNode() || (result.GetHitTestRequest().ListBased() &&
                                   result.ListBasedTestResult().size()));
    return false;
  }

  if (!result.InnerNode()) {
    // We hit something anonymous, and we didn't find a DOM node ancestor in
    // this layer.

    if (GetLayoutObject().IsLayoutFlowThread()) {
      // For a flow thread it's safe to just say that we didn't hit anything.
      // That means that we'll continue as normally, and eventually hit a column
      // set sibling instead. Column sets are also anonymous, but, unlike flow
      // threads, they don't establish layers, so we'll fall back and hit the
      // multicol container parent (which should have a DOM node).
      return false;
    }

    Node* e = EnclosingNode();
    // FIXME: should be a call to result.setNodeAndPosition. What we would
    // really want to do here is to return and look for the nearest
    // non-anonymous ancestor, and ignore aunts and uncles on our way. It's bad
    // to look for it manually like we do here, and give up on setting a local
    // point in the result, because that has bad implications for text selection
    // and caretRangeFromPoint(). See crbug.com/461791
    // This code path only ever hits in fullscreen tests.
    result.SetInnerNode(e);
  }
  return true;
}

bool PaintLayer::IsReplacedNormalFlowStacking() const {
  return GetLayoutObject().IsSVGForeignObjectIncludingNG();
}

PaintLayer* PaintLayer::HitTestChildren(
    PaintLayerIteration children_to_visit,
    const PaintLayer& transform_container,
    const PaintLayerFragment* container_fragment,
    HitTestResult& result,
    const HitTestRecursionData& recursion_data,
    HitTestingTransformState* container_transform_state,
    double* z_offset_for_descendants,
    double* z_offset,
    HitTestingTransformState* local_transform_state,
    bool depth_sort_descendants) {
  if (!HasSelfPaintingLayerDescendant())
    return nullptr;

  if (GetLayoutObject().ChildPaintBlockedByDisplayLock())
    return nullptr;

  const LayoutObject* stop_node = result.GetHitTestRequest().GetStopNode();
  PaintLayer* stop_layer = stop_node ? stop_node->PaintingLayer() : nullptr;

  PaintLayer* result_layer = nullptr;
  PaintLayerPaintOrderReverseIterator iterator(this, children_to_visit);
  while (PaintLayer* child_layer = iterator.Next()) {
    if (child_layer->IsReplacedNormalFlowStacking())
      continue;

    // Avoid the call to child_layer->HitTestLayer() if possible.
    if (stop_layer == this &&
        !IsHitCandidateForStopNode(child_layer->GetLayoutObject(), stop_node)) {
      continue;
    }

    PaintLayer* hit_layer = nullptr;
    STACK_UNINITIALIZED HitTestResult temp_result(
        result.GetHitTestRequest(), recursion_data.original_location);
    hit_layer = child_layer->HitTestLayer(
        transform_container, container_fragment, temp_result, recursion_data,
        /*applied_transform*/ false, container_transform_state,
        z_offset_for_descendants);

    // If it is a list-based test, we can safely append the temporary result
    // since it might had hit nodes but not necesserily had hitLayer set.
    if (result.GetHitTestRequest().ListBased())
      result.Append(temp_result);

    if (IsHitCandidateForDepthOrder(hit_layer, depth_sort_descendants, z_offset,
                                    local_transform_state)) {
      result_layer = hit_layer;
      if (!result.GetHitTestRequest().ListBased())
        result = temp_result;
      if (!depth_sort_descendants)
        break;
    }
  }

  return result_layer;
}

void PaintLayer::UpdateFilterReferenceBox() {
  if (!HasFilterThatMovesPixels())
    return;
  PhysicalRect result = LocalBoundingBox();
  ExpandRectForSelfPaintingDescendants(result);
  gfx::RectF reference_box(result);
  if (!ResourceInfo() || ResourceInfo()->FilterReferenceBox() != reference_box)
    GetLayoutObject().SetNeedsPaintPropertyUpdate();
  EnsureResourceInfo().SetFilterReferenceBox(reference_box);
}

gfx::RectF PaintLayer::FilterReferenceBox() const {
#if DCHECK_IS_ON()
  DCHECK_GE(GetLayoutObject().GetDocument().Lifecycle().GetState(),
            DocumentLifecycle::kInPrePaint);
#endif
  if (ResourceInfo())
    return ResourceInfo()->FilterReferenceBox();
  return gfx::RectF();
}

gfx::RectF PaintLayer::BackdropFilterReferenceBox() const {
  return gfx::RectF(GetLayoutObject().BorderBoundingBox());
}

gfx::RRectF PaintLayer::BackdropFilterBounds() const {
  gfx::RRectF backdrop_filter_bounds(
      SkRRect(RoundedBorderGeometry::PixelSnappedRoundedBorder(
          GetLayoutObject().StyleRef(),
          PhysicalRect::EnclosingRect(BackdropFilterReferenceBox()))));
  return backdrop_filter_bounds;
}

bool PaintLayer::HitTestClippedOutByClipPath(
    const PaintLayer& root_layer,
    const HitTestLocation& hit_test_location) const {
  // TODO(crbug.com/1270522): Support LayoutNGBlockFragmentation.
  DCHECK(GetLayoutObject().HasClipPath());
  DCHECK(IsSelfPaintingLayer());

  PhysicalRect origin;
  if (EnclosingPaginationLayer())
    ConvertFromFlowThreadToVisualBoundingBoxInAncestor(&root_layer, origin);
  else
    ConvertToLayerCoords(&root_layer, origin);

  gfx::PointF point(hit_test_location.Point() - origin.offset);
  gfx::RectF reference_box =
      ClipPathClipper::LocalReferenceBox(GetLayoutObject());

  ClipPathOperation* clip_path_operation =
      GetLayoutObject().StyleRef().ClipPath();
  DCHECK(clip_path_operation);
  if (clip_path_operation->GetType() == ClipPathOperation::kShape) {
    ShapeClipPathOperation* clip_path =
        To<ShapeClipPathOperation>(clip_path_operation);
    float zoom = GetLayoutObject().StyleRef().EffectiveZoom();
    DCHECK(!GetLayoutObject().IsSVGChild() ||
           GetLayoutObject().IsSVGForeignObjectIncludingNG());
    return !clip_path->GetPath(reference_box, zoom).Contains(point);
  }
  DCHECK_EQ(clip_path_operation->GetType(), ClipPathOperation::kReference);
  LayoutSVGResourceClipper* clipper =
      GetSVGResourceAsType(*ResourceInfo(), clip_path_operation);
  if (!clipper)
    return false;
  // If the clipPath is using "userspace on use" units, then the origin of
  // the coordinate system is the top-left of the reference box, so adjust
  // the point accordingly.
  if (clipper->ClipPathUnits() == SVGUnitTypes::kSvgUnitTypeUserspaceonuse)
    point -= reference_box.OffsetFromOrigin();
  // Unzoom the point and the reference box, since the <clipPath> geometry is
  // not zoomed.
  float inverse_zoom = 1 / GetLayoutObject().StyleRef().EffectiveZoom();
  point.Scale(inverse_zoom, inverse_zoom);
  reference_box.Scale(inverse_zoom);
  HitTestLocation location(point);
  return !clipper->HitTestClipContent(reference_box, location);
}

PhysicalRect PaintLayer::LocalBoundingBox() const {
  PhysicalRect rect = GetLayoutObject().PhysicalVisualOverflowRect();
  if (GetLayoutObject().IsEffectiveRootScroller() || IsRootLayer()) {
    rect.Unite(
        PhysicalRect(rect.offset, GetLayoutObject().View()->ViewRect().size));
  }
  return rect;
}

PhysicalRect PaintLayer::PhysicalBoundingBox(
    const PaintLayer* ancestor_layer) const {
  PhysicalOffset offset_from_root;
  ConvertToLayerCoords(ancestor_layer, offset_from_root);
  return PhysicalBoundingBox(offset_from_root);
}

PhysicalRect PaintLayer::PhysicalBoundingBox(
    const PhysicalOffset& offset_from_root) const {
  PhysicalRect result = LocalBoundingBox();
  result.Move(offset_from_root);
  return result;
}

void PaintLayer::ExpandRectForSelfPaintingDescendants(
    PhysicalRect& result) const {
  // If we're locked, then the subtree does not contribute painted output.
  // Furthermore, we might not have up-to-date sizing and position information
  // in the subtree, so skip recursing into the subtree.
  if (GetLayoutObject().ChildPaintBlockedByDisplayLock())
    return;

  DCHECK_EQ(result, LocalBoundingBox());
  // The input |result| is based on LayoutObject::PhysicalVisualOverflowRect()
  // which already includes bounds non-self-painting descendants.
  if (!HasSelfPaintingLayerDescendant())
    return;

  // If the layer is known to clip the whole subtree, then we don't need to
  // expand for children. The clip of the current layer is always applied.
  if (KnownToClipSubtreeToPaddingBox())
    return;

  PaintLayerPaintOrderIterator iterator(this, kAllChildren);
  while (PaintLayer* child_layer = iterator.Next()) {
    if (!child_layer->IsSelfPaintingLayer())
      continue;

    // The layer created for the LayoutFlowThread is just a helper for painting
    // and hit-testing, and should not contribute to the bounding box. The
    // LayoutMultiColumnSets will contribute the correct size for the layout
    // content of the multicol container.
    if (child_layer->GetLayoutObject().IsLayoutFlowThread())
      continue;

    PhysicalRect added_rect = child_layer->LocalBoundingBox();
    child_layer->ExpandRectForSelfPaintingDescendants(added_rect);

    // Only enlarge by the filter outsets if we know the filter is going to be
    // rendered in software.  Accelerated filters will handle their own outsets.
    if (child_layer->PaintsWithFilters())
      added_rect = child_layer->MapRectForFilter(added_rect);

    if (child_layer->Transform()) {
      added_rect = PhysicalRect::EnclosingRect(
          child_layer->Transform()->MapRect(gfx::RectF(added_rect)));
    }

    PhysicalOffset delta;
    child_layer->ConvertToLayerCoords(this, delta);
    added_rect.Move(delta);

    result.Unite(added_rect);
  }
}

bool PaintLayer::KnownToClipSubtreeToPaddingBox() const {
  if (const auto* box = GetLayoutBox()) {
    if (!box->ShouldClipOverflowAlongBothAxis())
      return false;
    if (HasNonContainedAbsolutePositionDescendant())
      return false;
    if (HasFixedPositionDescendant() && !box->CanContainFixedPositionObjects())
      return false;
    if (box->StyleRef().OverflowClipMargin())
      return false;
    // The root frame's clip is special at least in Android WebView.
    if (is_root_layer_ && box->GetFrame()->IsLocalRoot())
      return false;
    return true;
  }
  return false;
}

bool PaintLayer::SupportsSubsequenceCaching() const {
  if (EnclosingPaginationLayer())
    return false;

  if (const LayoutBox* box = GetLayoutBox()) {
    // TODO(crbug.com/1253797): Revisit this when implementing correct paint
    // order of fragmented stacking contexts.
    if (box->PhysicalFragmentCount() > 1)
      return false;

    // SVG root and SVG foreign object paint atomically.
    if (box->IsSVGRoot() || box->IsSVGForeignObjectIncludingNG())
      return true;

    // Don't create subsequence for the document element because the subsequence
    // for LayoutView serves the same purpose. This can avoid unnecessary paint
    // chunks that would otherwise be forced by the subsequence.
    if (box->IsDocumentElement())
      return false;
  }

  // Create subsequence for only stacked objects whose paintings are atomic.
  return GetLayoutObject().IsStacked();
}

bool PaintLayer::ShouldBeSelfPaintingLayer() const {
  return GetLayoutObject().LayerTypeRequired() == kNormalPaintLayer;
}

void PaintLayer::UpdateSelfPaintingLayer() {
  bool is_self_painting_layer = ShouldBeSelfPaintingLayer();
  if (IsSelfPaintingLayer() == is_self_painting_layer)
    return;

  // Invalidate the old subsequences which may no longer contain some
  // descendants of this layer because of the self painting status change.
  SetNeedsRepaint();
  is_self_painting_layer_ = is_self_painting_layer;
  // Self-painting change can change the compositing container chain;
  // invalidate the new chain in addition to the old one.
  MarkCompositingContainerChainForNeedsRepaint();
  if (!RuntimeEnabledFeatures::ScrollUpdateOptimizationsEnabled()) {
    if (SelfOrDescendantNeedsCullRectUpdate())
      MarkCompositingContainerChainForNeedsCullRectUpdate();
  }

  if (is_self_painting_layer)
    SetNeedsVisualOverflowRecalc();

  if (PaintLayer* parent = Parent()) {
    parent->MarkAncestorChainForFlagsUpdate();

    if (PaintLayer* enclosing_self_painting_layer =
            parent->EnclosingSelfPaintingLayer()) {
      if (is_self_painting_layer)
        MergeNeedsPaintPhaseFlagsFrom(*enclosing_self_painting_layer);
      else
        enclosing_self_painting_layer->MergeNeedsPaintPhaseFlagsFrom(*this);
    }
  }
}

PaintLayer* PaintLayer::EnclosingSelfPaintingLayer() {
  PaintLayer* layer = this;
  while (layer && !layer->IsSelfPaintingLayer())
    layer = layer->Parent();
  return layer;
}

void PaintLayer::UpdateFilters(const ComputedStyle* old_style,
                               const ComputedStyle& new_style) {
  if (!filter_on_effect_node_dirty_) {
    filter_on_effect_node_dirty_ =
        old_style ? !old_style->FilterDataEquivalent(new_style) ||
                        !old_style->ReflectionDataEquivalent(new_style)
                  : new_style.HasFilterInducingProperty();
  }

  if (!new_style.HasFilterInducingProperty() &&
      (!old_style || !old_style->HasFilterInducingProperty()))
    return;

  const bool had_resource_info = ResourceInfo();
  if (new_style.HasFilterInducingProperty())
    new_style.Filter().AddClient(EnsureResourceInfo());
  if (had_resource_info && old_style)
    old_style->Filter().RemoveClient(*ResourceInfo());
}

void PaintLayer::UpdateBackdropFilters(const ComputedStyle* old_style,
                                       const ComputedStyle& new_style) {
  if (!backdrop_filter_on_effect_node_dirty_) {
    backdrop_filter_on_effect_node_dirty_ =
        old_style ? !old_style->BackdropFilterDataEquivalent(new_style)
                  : new_style.HasBackdropFilter();
  }
}

void PaintLayer::UpdateClipPath(const ComputedStyle* old_style,
                                const ComputedStyle& new_style) {
  ClipPathOperation* new_clip = new_style.ClipPath();
  ClipPathOperation* old_clip = old_style ? old_style->ClipPath() : nullptr;
  if (!new_clip && !old_clip)
    return;
  const bool had_resource_info = ResourceInfo();
  if (auto* reference_clip = DynamicTo<ReferenceClipPathOperation>(new_clip))
    reference_clip->AddClient(EnsureResourceInfo());
  if (had_resource_info) {
    if (auto* old_reference_clip =
            DynamicTo<ReferenceClipPathOperation>(old_clip))
      old_reference_clip->RemoveClient(*ResourceInfo());
  }
}

void PaintLayer::StyleDidChange(StyleDifference diff,
                                const ComputedStyle* old_style) {
  UpdateScrollableArea();

  bool had_filter_that_moves_pixels = has_filter_that_moves_pixels_;
  has_filter_that_moves_pixels_ = ComputeHasFilterThatMovesPixels();
  if (had_filter_that_moves_pixels != has_filter_that_moves_pixels_) {
    // The compositor cannot easily track the filters applied within a layer
    // (i.e. composited filters) and is unable to expand the damage rect.
    // Force paint invalidation to update any potentially affected animations.
    // See |CompositorMayHaveIncorrectDamageRect|.
    GetLayoutObject().SetSubtreeShouldDoFullPaintInvalidation();
  }

  if (PaintLayerStackingNode::StyleDidChange(*this, old_style)) {
    // The compositing container (see: |PaintLayer::CompositingContainer()|) may
    // have changed so we need to ensure |descendant_needs_repaint_| is
    // propagated up the new compositing chain.
    if (SelfOrDescendantNeedsRepaint())
      MarkCompositingContainerChainForNeedsRepaint();
    if (!RuntimeEnabledFeatures::ScrollUpdateOptimizationsEnabled()) {
      if (SelfOrDescendantNeedsCullRectUpdate())
        MarkCompositingContainerChainForNeedsCullRectUpdate();
    }

    MarkAncestorChainForFlagsUpdate();
  }

  if (RequiresScrollableArea()) {
    DCHECK(scrollable_area_);
    scrollable_area_->UpdateAfterStyleChange(old_style);
  }

  // Overlay scrollbars can make this layer self-painting so we need
  // to recompute the bit once scrollbars have been updated.
  UpdateSelfPaintingLayer();

  // A scroller that changes background color might become opaque or not
  // opaque, which in turn affects whether it can be composited on low-DPI
  // screens.
  if (GetScrollableArea() && GetScrollableArea()->ScrollsOverflow() &&
      diff.HasDifference()) {
    MarkAncestorChainForFlagsUpdate();
  }

  bool needs_full_transform_update = diff.TransformChanged();
  if (needs_full_transform_update) {
    // Schedule a direct transform update instead of full update.
    if (PaintPropertyTreeBuilder::ScheduleDeferredTransformNodeUpdate(
            GetLayoutObject())) {
      needs_full_transform_update = false;
      SetNeedsDescendantDependentFlagsUpdate();
    }
  }

  // See also |LayoutObject::SetStyle| which handles these invalidations if a
  // PaintLayer is not present.
  if (needs_full_transform_update || diff.OpacityChanged() ||
      diff.ZIndexChanged() || diff.FilterChanged() || diff.CssClipChanged() ||
      diff.BlendModeChanged() || diff.MaskChanged() ||
      diff.CompositingReasonsChanged()) {
    GetLayoutObject().SetNeedsPaintPropertyUpdate();
    MarkAncestorChainForFlagsUpdate();
  }

  const ComputedStyle& new_style = GetLayoutObject().StyleRef();
  // HasNonContainedAbsolutePositionDescendant depends on position changes.
  if (!old_style || old_style->GetPosition() != new_style.GetPosition())
    MarkAncestorChainForFlagsUpdate();

  UpdateTransform(old_style, new_style);
  UpdateFilters(old_style, new_style);
  UpdateBackdropFilters(old_style, new_style);
  UpdateClipPath(old_style, new_style);

  if (diff.ZIndexChanged()) {
    // We don't need to invalidate paint of objects when paint order
    // changes. However, we do need to repaint the containing stacking
    // context, in order to generate new paint chunks in the correct order.
    // Raster invalidation will be issued if needed during paint.
    if (auto* stacking_context = AncestorStackingContext())
      stacking_context->SetNeedsRepaint();
  }

  if (old_style) {
    bool new_painted_output_invisible =
        PaintLayerPainter::PaintedOutputInvisible(new_style);
    if (PaintLayerPainter::PaintedOutputInvisible(*old_style) !=
        new_painted_output_invisible) {
      // Force repaint of the subtree for two purposes:
      // 1. To ensure FCP/LCP will be reported. See crbug.com/1184903.
      // 2. To update effectively_invisible flags of PaintChunks.
      // TODO(crbug.com/1104218): Optimize this.
      GetLayoutObject().SetSubtreeShouldDoFullPaintInvalidation();
    }
  }
}

gfx::Vector2d PaintLayer::PixelSnappedScrolledContentOffset() const {
  if (GetLayoutObject().IsScrollContainer())
    return GetLayoutBox()->PixelSnappedScrolledContentOffset();
  return gfx::Vector2d();
}

PaintLayerClipper PaintLayer::Clipper(
    GeometryMapperOption geometry_mapper_option) const {
  return PaintLayerClipper(
      this, geometry_mapper_option == GeometryMapperOption::kUseGeometryMapper);
}

bool PaintLayer::ScrollsOverflow() const {
  if (PaintLayerScrollableArea* scrollable_area = GetScrollableArea())
    return scrollable_area->ScrollsOverflow();

  return false;
}

FilterOperations PaintLayer::FilterOperationsIncludingReflection() const {
  const auto& style = GetLayoutObject().StyleRef();
  FilterOperations filter_operations = style.Filter();
  if (GetLayoutObject().HasReflection() && GetLayoutObject().IsBox()) {
    BoxReflection reflection = BoxReflectionForPaintLayer(*this, style);
    filter_operations.Operations().push_back(
        MakeGarbageCollected<BoxReflectFilterOperation>(reflection));
  }
  return filter_operations;
}

void PaintLayer::UpdateCompositorFilterOperationsForFilter(
    CompositorFilterOperations& operations) {
  auto filter = FilterOperationsIncludingReflection();
  gfx::RectF reference_box = FilterReferenceBox();

  // CompositorFilter needs the reference box to be unzoomed.
  float zoom = GetLayoutObject().StyleRef().EffectiveZoom();
  if (zoom != 1)
    reference_box.Scale(1 / zoom);

  // Use the existing |operations| if there is no change.
  if (!operations.IsEmpty() && !filter_on_effect_node_dirty_ &&
      reference_box == operations.ReferenceBox())
    return;

  operations =
      FilterEffectBuilder(reference_box, zoom).BuildFilterOperations(filter);
  filter_on_effect_node_dirty_ = false;
}

void PaintLayer::UpdateCompositorFilterOperationsForBackdropFilter(
    CompositorFilterOperations& operations,
    gfx::RRectF& backdrop_filter_bounds) {
  const auto& style = GetLayoutObject().StyleRef();
  if (style.BackdropFilter().IsEmpty()) {
    operations.Clear();
    backdrop_filter_on_effect_node_dirty_ = false;
    return;
  }

  gfx::RectF reference_box = BackdropFilterReferenceBox();
  backdrop_filter_bounds = BackdropFilterBounds();
  // CompositorFilter needs the reference box to be unzoomed.
  float zoom = style.EffectiveZoom();
  if (zoom != 1)
    reference_box.Scale(1 / zoom);

  // Use the existing |operations| if there is no change.
  if (!operations.IsEmpty() && !backdrop_filter_on_effect_node_dirty_ &&
      reference_box == operations.ReferenceBox())
    return;

  // Tack on regular filter values here - they need to be applied to the
  // backdrop image as well, in addition to being applied to the painted content
  // and children of the element. This is a bit of a hack - according to the
  // spec, filters should apply to the entire render pass as a whole, including
  // the backdrop-filtered content. However, because in the case that we have
  // both filters and backdrop-filters on a single element, we create two effect
  // nodes, and two render surfaces, and the backdrop-filter node comes first.
  // To get around that, we add the "regular" filters to the backdrop filters to
  // approximate.
  FilterOperations filter_operations = style.BackdropFilter();
  filter_operations.Operations().AppendVector(style.Filter().Operations());
  // Use kClamp tile mode to avoid pixel moving filters bringing in black
  // transparent pixels from the viewport edge.
  operations = FilterEffectBuilder(reference_box, zoom, nullptr, nullptr,
                                   SkTileMode::kClamp)
                   .BuildFilterOperations(filter_operations);
  // Note that |operations| may be empty here, if the |filter_operations| list
  // contains only invalid filters (e.g. invalid reference filters). See
  // https://crbug.com/983157 for details.
  backdrop_filter_on_effect_node_dirty_ = false;
}

PaintLayerResourceInfo& PaintLayer::EnsureResourceInfo() {
  PaintLayerRareData& rare_data = EnsureRareData();
  if (!rare_data.resource_info) {
    rare_data.resource_info =
        MakeGarbageCollected<PaintLayerResourceInfo>(this);
  }
  return *rare_data.resource_info;
}

gfx::RectF PaintLayer::MapRectForFilter(const gfx::RectF& rect) const {
  if (!HasFilterThatMovesPixels())
    return rect;
  return FilterOperationsIncludingReflection().MapRect(rect);
}

PhysicalRect PaintLayer::MapRectForFilter(const PhysicalRect& rect) const {
  if (!HasFilterThatMovesPixels())
    return rect;
  return PhysicalRect::EnclosingRect(MapRectForFilter(gfx::RectF(rect)));
}

bool PaintLayer::ComputeHasFilterThatMovesPixels() const {
  if (!HasFilterInducingProperty())
    return false;
  const ComputedStyle& style = GetLayoutObject().StyleRef();
  if (style.HasFilter() && style.Filter().HasFilterThatMovesPixels())
    return true;
  if (GetLayoutObject().HasReflection())
    return true;
  return false;
}

void PaintLayer::SetNeedsRepaint() {
  if (self_needs_repaint_)
    return;
  self_needs_repaint_ = true;
  // Invalidate as a display item client.
  static_cast<DisplayItemClient*>(this)->Invalidate();
  MarkCompositingContainerChainForNeedsRepaint();
}

void PaintLayer::SetDescendantNeedsRepaint() {
  if (descendant_needs_repaint_)
    return;
  descendant_needs_repaint_ = true;
  MarkCompositingContainerChainForNeedsRepaint();
}

void PaintLayer::MarkCompositingContainerChainForNeedsRepaint() {
  PaintLayer* layer = this;
  while (true) {
    // For a non-self-painting layer having self-painting descendant, the
    // descendant will be painted through this layer's Parent() instead of
    // this layer's Container(), so in addition to the CompositingContainer()
    // chain, we also need to mark NeedsRepaint for Parent().
    // TODO(crbug.com/828103): clean up this.
    if (layer->Parent() && !layer->IsSelfPaintingLayer())
      layer->Parent()->SetNeedsRepaint();

    // Don't mark across frame boundary here. LocalFrameView::PaintTree() will
    // propagate child frame NeedsRepaint flag into the owning frame.
    PaintLayer* container = layer->CompositingContainer();
    if (!container || container->descendant_needs_repaint_)
      break;

    // If the layer doesn't need painting itself (which means we're propagating
    // a bit from its children) and it blocks child painting via display lock,
    // then stop propagating the dirty bit.
    if (!layer->SelfNeedsRepaint() &&
        layer->GetLayoutObject().ChildPaintBlockedByDisplayLock())
      break;

    container->descendant_needs_repaint_ = true;
    layer = container;
  }
}

void PaintLayer::ClearNeedsRepaintRecursively() {
  self_needs_repaint_ = false;

  // Don't clear dirty bits in a display-locked subtree.
  if (GetLayoutObject().ChildPaintBlockedByDisplayLock())
    return;

  for (PaintLayer* child = FirstChild(); child; child = child->NextSibling())
    child->ClearNeedsRepaintRecursively();
  descendant_needs_repaint_ = false;
}

void PaintLayer::SetNeedsCullRectUpdate() {
  if (needs_cull_rect_update_)
    return;
  needs_cull_rect_update_ = true;
  if (RuntimeEnabledFeatures::ScrollUpdateOptimizationsEnabled()) {
    if (Parent())
      Parent()->SetDescendantNeedsCullRectUpdate();
  } else {
    MarkCompositingContainerChainForNeedsCullRectUpdate();
  }
}

void PaintLayer::SetForcesChildrenCullRectUpdate() {
  if (forces_children_cull_rect_update_)
    return;
  forces_children_cull_rect_update_ = true;
  descendant_needs_cull_rect_update_ = true;
  if (RuntimeEnabledFeatures::ScrollUpdateOptimizationsEnabled()) {
    if (Parent())
      Parent()->SetDescendantNeedsCullRectUpdate();
  } else {
    MarkCompositingContainerChainForNeedsCullRectUpdate();
  }
}

void PaintLayer::MarkCompositingContainerChainForNeedsCullRectUpdate() {
  // This is only used by the old cull rect updater.
  DCHECK(!RuntimeEnabledFeatures::ScrollUpdateOptimizationsEnabled());

  // Mark compositing container chain for needing cull rect update. This is
  // similar to MarkCompositingContainerChainForNeedsRepaint().
  PaintLayer* layer = this;
  while (true) {
    // For a non-self-painting layer having self-painting descendant, the
    // descendant will be painted through this layer's Parent() instead of
    // this layer's Container(), so in addition to the CompositingContainer()
    // chain, we also need to mark NeedsRepaint for Parent().
    // TODO(crbug.com/828103): clean up this.
    if (layer->Parent() && !layer->IsSelfPaintingLayer())
      layer->Parent()->SetNeedsCullRectUpdate();

    PaintLayer* container = layer->CompositingContainer();
    if (!container) {
      auto* owner = layer->GetLayoutObject().GetFrame()->OwnerLayoutObject();
      if (!owner)
        break;
      container = owner->EnclosingLayer();
    }

    if (container->descendant_needs_cull_rect_update_)
      break;

    container->descendant_needs_cull_rect_update_ = true;

    // Only propagate the dirty bit up to the display locked ancestor.
    if (container->GetLayoutObject().ChildPrePaintBlockedByDisplayLock())
      break;

    layer = container;
  }
}

void PaintLayer::SetDescendantNeedsCullRectUpdate() {
  // This is only used by the new cull rect updater.
  DCHECK(RuntimeEnabledFeatures::ScrollUpdateOptimizationsEnabled());

  for (auto* layer = this; layer; layer = layer->Parent()) {
    if (layer->descendant_needs_cull_rect_update_)
      break;
    layer->descendant_needs_cull_rect_update_ = true;
    // Only propagate the dirty bit up to the display locked ancestor.
    if (layer->GetLayoutObject().ChildPrePaintBlockedByDisplayLock())
      break;
  }
}

void PaintLayer::DirtyStackingContextZOrderLists() {
  auto* stacking_context = AncestorStackingContext();
  if (!stacking_context)
    return;
  if (stacking_context->StackingNode())
    stacking_context->StackingNode()->DirtyZOrderLists();

  MarkAncestorChainForFlagsUpdate();
}

void PaintLayer::Trace(Visitor* visitor) const {
  visitor->Trace(layout_object_);
  visitor->Trace(parent_);
  visitor->Trace(previous_);
  visitor->Trace(next_);
  visitor->Trace(first_);
  visitor->Trace(last_);
  visitor->Trace(scrollable_area_);
  visitor->Trace(stacking_node_);
  visitor->Trace(rare_data_);
  DisplayItemClient::Trace(visitor);
}

}  // namespace blink

#if DCHECK_IS_ON()
void ShowLayerTree(const blink::PaintLayer* layer) {
  if (!layer) {
    LOG(ERROR) << "Cannot showLayerTree. Root is (nil)";
    return;
  }

  if (blink::LocalFrame* frame = layer->GetLayoutObject().GetFrame()) {
    WTF::String output =
        ExternalRepresentation(frame,
                               blink::kLayoutAsTextShowLayerNesting |
                                   blink::kLayoutAsTextShowAddresses |
                                   blink::kLayoutAsTextShowIDAndClass |
                                   blink::kLayoutAsTextDontUpdateLayout |
                                   blink::kLayoutAsTextShowLayoutState |
                                   blink::kLayoutAsTextShowPaintProperties,
                               layer);
    LOG(INFO) << output.Utf8();
  }
}

void ShowLayerTree(const blink::LayoutObject* layoutObject) {
  if (!layoutObject) {
    LOG(ERROR) << "Cannot showLayerTree. Root is (nil)";
    return;
  }
  ShowLayerTree(layoutObject->EnclosingLayer());
}
#endif
