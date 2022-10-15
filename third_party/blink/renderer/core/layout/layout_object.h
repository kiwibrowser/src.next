/*
 * Copyright (C) 2000 Lars Knoll (knoll@kde.org)
 *           (C) 2000 Antti Koivisto (koivisto@kde.org)
 *           (C) 2000 Dirk Mueller (mueller@kde.org)
 *           (C) 2004 Allan Sandfeld Jensen (kde@carewolf.com)
 * Copyright (C) 2003, 2004, 2005, 2006, 2007, 2008, 2009, 2012 Apple Inc.
 *               All rights reserved.
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
 *
 */

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_LAYOUT_LAYOUT_OBJECT_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_LAYOUT_LAYOUT_OBJECT_H_

#include <utility>

#include "base/check_op.h"
#include "base/dcheck_is_on.h"
#include "base/gtest_prod_util.h"
#include "base/notreached.h"
#include "third_party/blink/public/mojom/scroll/scroll_into_view_params.mojom-blink-forward.h"
#include "third_party/blink/renderer/core/core_export.h"
#include "third_party/blink/renderer/core/display_lock/display_lock_context.h"
#include "third_party/blink/renderer/core/dom/document.h"
#include "third_party/blink/renderer/core/dom/document_lifecycle.h"
#include "third_party/blink/renderer/core/dom/element.h"
#include "third_party/blink/renderer/core/editing/forward.h"
#include "third_party/blink/renderer/core/html_names.h"
#include "third_party/blink/renderer/core/layout/api/selection_state.h"
#include "third_party/blink/renderer/core/layout/geometry/physical_rect.h"
#include "third_party/blink/renderer/core/layout/geometry/transform_state.h"
#include "third_party/blink/renderer/core/layout/hit_test_phase.h"
#include "third_party/blink/renderer/core/layout/hit_test_result.h"
#include "third_party/blink/renderer/core/layout/layout_object_child_list.h"
#include "third_party/blink/renderer/core/layout/map_coordinates_flags.h"
#include "third_party/blink/renderer/core/layout/min_max_sizes.h"
#include "third_party/blink/renderer/core/layout/ng/ng_outline_type.h"
#include "third_party/blink/renderer/core/layout/ng/ng_style_variant.h"
#include "third_party/blink/renderer/core/layout/subtree_layout_scope.h"
#include "third_party/blink/renderer/core/loader/resource/image_resource_observer.h"
#include "third_party/blink/renderer/core/paint/fragment_data.h"
#include "third_party/blink/renderer/core/paint/paint_phase.h"
#include "third_party/blink/renderer/core/style/computed_style.h"
#include "third_party/blink/renderer/core/style/style_difference.h"
#include "third_party/blink/renderer/platform/geometry/layout_rect.h"
#include "third_party/blink/renderer/platform/graphics/compositing_reasons.h"
#include "third_party/blink/renderer/platform/graphics/image_orientation.h"
#include "third_party/blink/renderer/platform/graphics/paint/display_item_client.h"
#include "third_party/blink/renderer/platform/graphics/paint_invalidation_reason.h"
#include "third_party/blink/renderer/platform/graphics/subtree_paint_property_update_reason.h"
#include "third_party/blink/renderer/platform/transforms/transformation_matrix.h"
#include "third_party/blink/renderer/platform/wtf/allocator/allocator.h"
#include "ui/gfx/geometry/quad_f.h"

namespace ui {
class Cursor;
}

namespace blink {
class AffineTransform;
class FragmentDataIterator;
class HitTestLocation;
class HitTestRequest;
class InlineBox;
class LayoutBlock;
class LayoutBlockFlow;
class LayoutFlowThread;
class LayoutMultiColumnSpannerPlaceholder;
class LayoutNGGridInterface;
class LayoutNGTableInterface;
class LayoutNGTableRowInterface;
class LayoutNGTableSectionInterface;
class LayoutNGTableCellInterface;
class LayoutView;
class LocalFrameView;
class PaintLayer;
class StyleRequest;
struct PaintInfo;
struct PaintInvalidatorContext;

enum VisualRectFlags {
  kDefaultVisualRectFlags = 0,
  kEdgeInclusive = 1 << 0,
  // Use the GeometryMapper fast-path, if possible.
  kUseGeometryMapper = 1 << 1,
  // When mapping to absolute coordinates and the main frame is remote, don't
  // apply the main frame root scroller's overflow clip.
  kDontApplyMainFrameOverflowClip = 1 << 2,
};

enum CursorDirective { kSetCursorBasedOnStyle, kSetCursor, kDoNotSetCursor };

enum MarkingBehavior {
  kMarkOnlyThis,
  kMarkContainerChain,
};

enum ScheduleRelayoutBehavior { kScheduleRelayout, kDontScheduleRelayout };

enum {
  // Backgrounds paint under FragmentData::LocalBorderBoxProperties().
  kBackgroundPaintInBorderBoxSpace = 1 << 0,
  // Backgrounds paint under FragmentData::ContentsProperties().
  kBackgroundPaintInContentsSpace = 1 << 1,
  // Paint backgrounds twice.
  kBackgroundPaintInBothSpaces =
      kBackgroundPaintInBorderBoxSpace | kBackgroundPaintInContentsSpace,
};
using BackgroundPaintLocation = unsigned;

struct AnnotatedRegionValue {
  DISALLOW_NEW();
  bool operator==(const AnnotatedRegionValue& o) const {
    return draggable == o.draggable && bounds == o.bounds;
  }

  PhysicalRect bounds;
  bool draggable;
};

// The axes which overflows should be clipped. This is not just because of
// overflow clip, but other types of clip as well, such as control clips or
// contain: paint.
using OverflowClipAxes = unsigned;

enum {
  kNoOverflowClip = 0,
  kOverflowClipX = 1 << 0,
  kOverflowClipY = 1 << 1,
  kOverflowClipBothAxis = kOverflowClipX | kOverflowClipY,
};

// Expands |clip_rect| to allow infinite overflow in horizontal and/or vertical
// direction.
void ApplyVisibleOverflowToClipRect(OverflowClipAxes, PhysicalRect& clip_rect);

#if DCHECK_IS_ON()
const int kShowTreeCharacterOffset = 39;
#endif

// Usually calling LayooutObject::Destroy() is banned. This scope can be used to
// exclude certain functions like ~SVGImage() from this rule. This is allowed
// when a Persistent is guaranteeing to keep the LayoutObject alive for that GC
// cycle.
class CORE_EXPORT AllowDestroyingLayoutObjectInFinalizerScope {
  STACK_ALLOCATED();

 public:
  AllowDestroyingLayoutObjectInFinalizerScope();
  ~AllowDestroyingLayoutObjectInFinalizerScope();
};

// The result of |LayoutObject::RecalcLayoutOverflow|.
struct RecalcLayoutOverflowResult {
  STACK_ALLOCATED();

 public:
  // True if the layout-overflow (from the viewpoint of the parent) changed,
  // indicating that the parent should also recalculate its layout-overflow.
  bool layout_overflow_changed = false;

  // True if parents should rebuild their fragments to ensure fragment tree
  // consistency. This may be true even if |layout_overflow_changed| is false.
  bool rebuild_fragment_tree = false;

  void Unite(const RecalcLayoutOverflowResult& other) {
    layout_overflow_changed |= other.layout_overflow_changed;
    rebuild_fragment_tree |= other.rebuild_fragment_tree;
  }
};

// LayoutObject is the base class for all layout tree objects.
//
// LayoutObjects form a tree structure that is a close mapping of the DOM tree.
// The root of the LayoutObject tree is the LayoutView, which is the
// LayoutObject associated with the Document.
//
// Some LayoutObjects don't have an associated Node and are called "anonymous"
// (see the constructor below). Anonymous LayoutObjects exist for several
// purposes but are usually required by CSS. A good example is anonymous table
// parts (see LayoutTable for the expected structure). Anonymous LayoutObjects
// are generated when a new child is added to the tree in addChild(). See the
// function for some important information on this.
//
// Also some Node don't have an associated LayoutObjects e.g. if display: none
// or display: contents is set. For more detail, see LayoutObject::createObject
// that creates the right LayoutObject based on the style.
//
// Because the SVG and CSS classes both inherit from this object, functions can
// belong to either realm and sometimes to both.
//
// The purpose of the layout tree is to do layout (aka reflow) and store its
// results for painting and hit-testing. Layout is the process of sizing and
// positioning Nodes on the page. In Blink, layouts always start from a relayout
// boundary (see ObjectIsRelayoutBoundary in layout_object.cc). As such, we
// need to mark the ancestors all the way to the enclosing relayout boundary in
// order to do a correct layout.
//
// Due to the high cost of layout, a lot of effort is done to avoid doing full
// layouts of nodes. This is why there are several types of layout available to
// bypass the complex operations. See the comments on the layout booleans in
// LayoutObjectBitfields below about the different layouts.
//
// To save memory, especially for the common child class LayoutText,
// LayoutObject doesn't provide storage for children. Descendant classes that do
// allow children have to have a LayoutObjectChildList member that stores the
// actual children and override virtualChildren().
//
// LayoutObject is an ImageResourceObserver, which means that it gets notified
// when associated images are changed. This is used for 2 main use cases:
// - reply to 'background-image' as we need to invalidate the background in this
//   case.
//   (See https://drafts.csswg.org/css-backgrounds-3/#the-background-image)
// - image (LayoutImage, LayoutSVGImage) or video (LayoutVideo) objects that are
//   placeholders for displaying them.
//
//
// ***** LIFETIME *****
//
// LayoutObjects are fully owned by their associated DOM node. In other words,
// it's the DOM node's responsibility to free its LayoutObject, this is why
// LayoutObjects are not and SHOULD NOT be RefCounted.
//
// LayoutObjects are created during the DOM attachment. This phase computes
// the style and create the LayoutObject associated with the Node (see
// Node::attachLayoutTree). LayoutObjects are destructed during detachment (see
// Node::detachLayoutTree), which can happen when the DOM node is removed from
// the
// DOM tree, during page tear down or when the style is changed to contain
// 'display: none'.
//
// Anonymous LayoutObjects are owned by their enclosing DOM node. This means
// that if the DOM node is detached, it has to destroy any anonymous
// descendants. This is done in LayoutObject::destroy().
//
// Note that for correctness, destroy() is expected to clean any anonymous
// wrappers as sequences of insertion / removal could make them visible to
// the page. This is done by LayoutObject::destroyAndCleanupAnonymousWrappers()
// which is the preferred way to destroy an object.
//
//
// ***** INTRINSIC SIZES / PREFERRED LOGICAL WIDTHS *****
// The preferred logical widths are the intrinsic sizes of this element
// (https://drafts.csswg.org/css-sizing-3/#intrinsic). Intrinsic sizes depend
// mostly on the content and a limited set of style properties (e.g. any
// font-related property for text, 'min-width'/'max-width',
// 'min-height'/'max-height').
//
// Those widths are used to determine the final layout logical width, which
// depends on the layout algorithm used and the available logical width.
//
// LayoutObject only has a getter for the widths (PreferredLogicalWidths).
// However the storage for them is in LayoutBox (see
// min_preferred_logical_width_ and max_preferred_logical_width_). This is
// because only boxes implementing the full box model have a need for them.
// Because LayoutBlockFlow's intrinsic widths rely on the underlying text
// content, LayoutBlockFlow may call LayoutText::ComputePreferredLogicalWidths.
//
// The 2 widths are computed lazily during layout when the getters are called.
// The computation is done by calling ComputePreferredLogicalWidths() behind the
// scene. The boolean used to control the lazy recomputation is
// IntrinsicLogicalWidthsDirty.
//
// See the individual getters below for more details about what each width is.
class CORE_EXPORT LayoutObject : public GarbageCollected<LayoutObject>,
                                 public ImageResourceObserver,
                                 public DisplayItemClient {
  friend class LayoutObjectChildList;
  FRIEND_TEST_ALL_PREFIXES(LayoutObjectTest, MutableForPaintingClearPaintFlags);
  FRIEND_TEST_ALL_PREFIXES(
      LayoutObjectTest,
      ContainingBlockAbsoluteLayoutObjectShouldBeNonStaticallyPositionedBlockAncestor);
  FRIEND_TEST_ALL_PREFIXES(LayoutObjectTest,
                           ContainingBlockFixedLayoutObjectInTransformedDiv);
  FRIEND_TEST_ALL_PREFIXES(LayoutObjectTest,
                           ContainingBlockFixedLayoutObjectInTransformedDiv);
  FRIEND_TEST_ALL_PREFIXES(LayoutObjectTest,
                           ContainingBlockFixedLayoutObjectInBody);
  FRIEND_TEST_ALL_PREFIXES(LayoutObjectTest,
                           ContainingBlockAbsoluteLayoutObjectInBody);
  FRIEND_TEST_ALL_PREFIXES(
      LayoutObjectTest,
      ContainingBlockAbsoluteLayoutObjectShouldNotBeNonStaticallyPositionedInlineAncestor);
  FRIEND_TEST_ALL_PREFIXES(LayoutObjectTest, VisualRect);

  friend class VisualRectMappingTest;

 public:
  // Anonymous objects should pass the document as their node, and they will
  // then automatically be marked as anonymous in the constructor.
  explicit LayoutObject(Node*);
  LayoutObject(const LayoutObject&) = delete;
  LayoutObject& operator=(const LayoutObject&) = delete;
  ~LayoutObject() override;
  void Trace(Visitor*) const override;

// Should be added at the beginning of every method to ensure we are not
// accessing a LayoutObject after the Desroy() call.
#if DCHECK_IS_ON()
  ALWAYS_INLINE void CheckIsNotDestroyed() const { DCHECK(!is_destroyed_); }
#else
  ALWAYS_INLINE void CheckIsNotDestroyed() const {}
#endif
#define NOT_DESTROYED() CheckIsNotDestroyed()

  // Returns the name of the layout object.
  virtual const char* GetName() const = 0;

  // Returns the decorated name used by run-layout-tests. The name contains the
  // name of the object along with extra information about the layout object
  // state (e.g. positioning).
  String DecoratedName() const;

  // This is an inexact determination of whether the display of this objects is
  // altered or obscured by CSS effects.
  bool HasDistortingVisualEffects() const;

  // Returns false iff this object or one of its ancestors has opacity:0.
  bool HasNonZeroEffectiveOpacity() const;

  // Returns true if the offset ot the containing block depends on the point
  // being mapped.
  bool OffsetForContainerDependsOnPoint(const LayoutObject* container) const;

 protected:
  void EnsureIdForTesting() {
    NOT_DESTROYED();
    fragment_->EnsureId();
  }

 private:
  // DisplayItemClient methods.

  // Hide DisplayItemClient's methods whose names are too generic for
  // LayoutObjects. Should use LayoutObject's methods instead.
  using DisplayItemClient::GetPaintInvalidationReason;
  using DisplayItemClient::Invalidate;
  using DisplayItemClient::IsValid;

  DOMNodeId OwnerNodeId() const override;

 public:
  String DebugName() const final;

  // End of DisplayItemClient methods.

  LayoutObject* Parent() const {
    NOT_DESTROYED();
    return parent_;
  }
  bool IsDescendantOf(const LayoutObject*) const;
  LayoutObject* NonCulledParent() const;

  LayoutObject* PreviousSibling() const {
    NOT_DESTROYED();
    return previous_;
  }
  LayoutObject* NextSibling() const {
    NOT_DESTROYED();
    return next_;
  }

  DISABLE_CFI_PERF
  LayoutObject* SlowFirstChild() const {
    NOT_DESTROYED();
    if (const LayoutObjectChildList* children = VirtualChildren())
      return children->FirstChild();
    return nullptr;
  }
  LayoutObject* SlowLastChild() const {
    NOT_DESTROYED();
    if (const LayoutObjectChildList* children = VirtualChildren())
      return children->LastChild();
    return nullptr;
  }

  // See comment in the class description as to why there is no child.
  virtual LayoutObjectChildList* VirtualChildren() {
    NOT_DESTROYED();
    return nullptr;
  }
  virtual const LayoutObjectChildList* VirtualChildren() const {
    NOT_DESTROYED();
    return nullptr;
  }

  LayoutObject* NextInPreOrder() const;
  LayoutObject* NextInPreOrder(const LayoutObject* stay_within) const;
  LayoutObject* NextInPreOrderAfterChildren() const;
  LayoutObject* NextInPreOrderAfterChildren(
      const LayoutObject* stay_within) const;

  // Traverse in the exact reverse of the preorder traversal. In order words,
  // they traverse in the last child -> first child -> root ordering.
  LayoutObject* PreviousInPreOrder() const;
  LayoutObject* PreviousInPreOrder(const LayoutObject* stay_within) const;

  // Traverse in the exact reverse of the postorder traversal. In other words,
  // they traverse in the root -> last child -> first child ordering.
  LayoutObject* PreviousInPostOrder(const LayoutObject* stay_within) const;
  LayoutObject* PreviousInPostOrderBeforeChildren(
      const LayoutObject* stay_within) const;

  // The depth of the tree.
  wtf_size_t Depth() const;

  struct CommonAncestorData {
    STACK_ALLOCATED();

   public:
    // The last object before reaching the common ancestor from |this| and
    // |other|.
    LayoutObject* last = nullptr;
    LayoutObject* other_last = nullptr;
  };
  LayoutObject* CommonAncestor(const LayoutObject& other,
                               CommonAncestorData* data = nullptr) const;

  bool IsBeforeInPreOrder(const LayoutObject& other) const;

  LayoutObject* LastLeafChild() const;

  // The following functions are used when the layout tree hierarchy changes to
  // make sure layers get properly added and removed. Since containership can be
  // implemented by any subclass, and since a hierarchy can contain a mixture of
  // boxes and other object types, these functions need to be in the base class.
  PaintLayer* EnclosingLayer() const;
  void AddLayers(PaintLayer* parent_layer);
  void RemoveLayers(PaintLayer* parent_layer);
  void MoveLayers(PaintLayer* old_parent, PaintLayer* new_parent);
  PaintLayer* FindNextLayer(PaintLayer* parent_layer,
                            LayoutObject* start_point,
                            bool check_parent = true);

  // Returns the layer that will paint this object. During paint invalidation,
  // we should use the faster PaintInvalidatorContext::painting_layer instead.
  PaintLayer* PaintingLayer() const;

  bool IsFixedPositionObjectInPagedMedia() const;

  // Convenience function for getting to the nearest enclosing box of a
  // LayoutObject.
  LayoutBox* EnclosingBox() const;

  LayoutBox* EnclosingScrollableBox() const;

  // Return the NG |LayoutBlockFlow| that will have any |NGFragmentItems| for
  // |this|, or nullptr if the containing block isn't an NG inline formatting
  // context root. |this| is required to be an object that participates in an
  // inline formatting context (i.e. something inline-level, or a float).
  LayoutBlockFlow* FragmentItemsContainer() const;

  // Return the containing NG block, if the containing block is an NG block,
  // nullptr otherwise.
  LayoutBlock* ContainingNGBlock() const;

  // Return the nearest fragmentation context root, if any.
  LayoutBlock* ContainingFragmentationContextRoot() const;

  // Function to return our enclosing flow thread if we are contained inside
  // one. This function follows the containing block chain.
  LayoutFlowThread* FlowThreadContainingBlock() const {
    NOT_DESTROYED();
    if (!IsInsideFlowThread())
      return nullptr;
    return LocateFlowThreadContainingBlock();
  }

#if DCHECK_IS_ON()
  void SetHasAXObject(bool flag) {
    NOT_DESTROYED();
    has_ax_object_ = flag;
  }
  bool HasAXObject() const {
    NOT_DESTROYED();
    return has_ax_object_;
  }

  // Helper class forbidding calls to setNeedsLayout() during its lifetime.
  class SetLayoutNeededForbiddenScope {
    STACK_ALLOCATED();

   public:
    explicit SetLayoutNeededForbiddenScope(LayoutObject&);
    ~SetLayoutNeededForbiddenScope();

   private:
    LayoutObject& layout_object_;
    bool preexisting_forbidden_;
  };

  void AssertLaidOut() const {
    NOT_DESTROYED();
    if (NeedsLayout() && !ChildLayoutBlockedByDisplayLock())
      ShowLayoutTreeForThis();
    SECURITY_DCHECK(!NeedsLayout() || ChildLayoutBlockedByDisplayLock());
  }

  void AssertSubtreeIsLaidOut() const {
    NOT_DESTROYED();
    for (const LayoutObject* layout_object = this; layout_object;
         layout_object = layout_object->ChildLayoutBlockedByDisplayLock()
                             ? layout_object->NextInPreOrderAfterChildren(this)
                             : layout_object->NextInPreOrder(this)) {
      layout_object->AssertLaidOut();
    }
  }

  // This function checks if the fragment tree is consistent with the
  // |LayoutObject| tree. This consistency is critical, as sometimes we traverse
  // the fragment tree, sometimes the |LayoutObject| tree, or mix the
  // traversals. Also we rely on the consistency to avoid using fragments whose
  // |LayoutObject| were destroyed.
  void AssertFragmentTree(bool display_locked = false) const;

  void AssertClearedPaintInvalidationFlags() const;

  void AssertSubtreeClearedPaintInvalidationFlags() const {
    NOT_DESTROYED();
    for (const LayoutObject* layout_object = this; layout_object;
         layout_object = layout_object->ChildPrePaintBlockedByDisplayLock()
                             ? layout_object->NextInPreOrderAfterChildren(this)
                             : layout_object->NextInPreOrder(this)) {
      layout_object->AssertClearedPaintInvalidationFlags();
    }
  }

#endif  // DCHECK_IS_ON()

  // LayoutObject tree manipulation
  //////////////////////////////////////////
  DISABLE_CFI_PERF virtual bool CanHaveChildren() const {
    NOT_DESTROYED();
    return VirtualChildren();
  }
  virtual bool IsChildAllowed(LayoutObject*, const ComputedStyle&) const {
    NOT_DESTROYED();
    return true;
  }

  // This function is called whenever a child is inserted under |this|.
  //
  // The main purpose of this function is to generate a consistent layout
  // tree, which means generating the missing anonymous objects. Most of the
  // time there'll be no anonymous objects to generate.
  //
  // The following invariants are true on the input:
  // - |newChild->node()| is a child of |node()|, if |this| is not
  //   anonymous. If |this| is anonymous, the invariant holds with the
  //   enclosing non-anonymous LayoutObject.
  // - |beforeChild->node()| (if |beforeChild| is provided and not anonymous)
  //   is a sibling of |newChild->node()| (if |newChild| is not anonymous).
  //
  // The reason for these invariants is that insertions are performed on the
  // DOM tree. Because the layout tree may insert extra anonymous renderers,
  // the previous invariants are only guaranteed for the DOM tree. In
  // particular, |beforeChild| may not be a direct child when it's wrapped in
  // anonymous wrappers.
  //
  // Classes inserting anonymous LayoutObjects in the tree are expected to
  // check for the anonymous wrapper case with:
  //                    beforeChild->parent() != this
  //
  // The usage of |child/parent/sibling| in this comment actually means
  // |child/parent/sibling| in a flat tree because a layout tree is generated
  // from a structure of a flat tree if Shadow DOM is used.
  // See LayoutTreeBuilderTraversal and FlatTreeTraversal.
  //
  // See LayoutTable::addChild and LayoutBlock::addChild.
  // TODO(jchaffraix): |newChild| cannot be nullptr and should be a reference.
  virtual void AddChild(LayoutObject* new_child,
                        LayoutObject* before_child = nullptr);
  virtual void AddChildIgnoringContinuation(
      LayoutObject* new_child,
      LayoutObject* before_child = nullptr) {
    NOT_DESTROYED();
    return AddChild(new_child, before_child);
  }
  virtual void RemoveChild(LayoutObject*);
  virtual bool CreatesAnonymousWrapper() const {
    NOT_DESTROYED();
    return false;
  }
  //////////////////////////////////////////

  UniqueObjectId UniqueId() const {
    NOT_DESTROYED();
    return fragment_->UniqueId();
  }

  inline bool ShouldApplyOverflowClipMargin() const {
    NOT_DESTROYED();
    // If the object is clipped by something other than overflow:clip (i.e. it's
    // a scroll container), then we should not apply overflow-clip-margin.
    if (IsScrollContainer())
      return false;

    const auto& style = StyleRef();
    // Nothing to apply if there is no margin.
    if (!style.OverflowClipMargin())
      return false;

    // Replaced elements have a used value of 'clip' for all overflow values
    // except visible. See discussion at
    // https://github.com/w3c/csswg-drafts/issues/7714#issuecomment-1248761712.
    bool is_overflow_clip = false;
    if (IsLayoutReplaced() &&
        RuntimeEnabledFeatures::CSSOverflowForReplacedElementsEnabled()) {
      is_overflow_clip = style.OverflowX() != EOverflow::kVisible &&
                         style.OverflowY() != EOverflow::kVisible;
    } else {
      is_overflow_clip = style.OverflowX() == EOverflow::kClip &&
                         style.OverflowY() == EOverflow::kClip;
    }

    // In all other cases, we apply overflow-clip-margin when we clip to
    // overflow clip edge, meaning we have overflow: clip or paint containment.
    return is_overflow_clip || ShouldApplyPaintContainment();
  }

  inline bool IsEligibleForPaintOrLayoutContainment() const {
    NOT_DESTROYED();
    return (!IsInline() || IsAtomicInlineLevel()) && !IsRubyText() &&
           (!IsTablePart() || IsLayoutBlockFlow());
  }

  inline bool ShouldApplyPaintContainment(const ComputedStyle& style) const {
    NOT_DESTROYED();
    return style.ContainsPaint() && IsEligibleForPaintOrLayoutContainment();
  }

  inline bool ShouldApplyPaintContainment() const {
    NOT_DESTROYED();
    return ShouldApplyPaintContainment(StyleRef());
  }

  inline bool ShouldApplyLayoutContainment(const ComputedStyle& style) const {
    NOT_DESTROYED();
    return style.ContainsLayout() && IsEligibleForPaintOrLayoutContainment();
  }

  inline bool ShouldApplyLayoutContainment() const {
    NOT_DESTROYED();
    return ShouldApplyLayoutContainment(StyleRef());
  }

  inline bool IsEligibleForSizeContainment() const {
    NOT_DESTROYED();
    return (!IsInline() || IsAtomicInlineLevel()) && !IsRubyText() &&
           (!IsTablePart() || IsTableCaption()) && !IsTable();
  }
  inline bool ShouldApplySizeContainment() const {
    NOT_DESTROYED();
    return StyleRef().ContainsSize() && IsEligibleForSizeContainment();
  }
  inline bool ShouldApplyInlineSizeContainment() const {
    NOT_DESTROYED();
    return StyleRef().ContainsInlineSize() && IsEligibleForSizeContainment();
  }
  inline bool ShouldApplyBlockSizeContainment() const {
    NOT_DESTROYED();
    return StyleRef().ContainsBlockSize() && IsEligibleForSizeContainment();
  }
  inline bool ShouldApplyWidthContainment() const {
    NOT_DESTROYED();
    return IsHorizontalWritingMode() ? ShouldApplyInlineSizeContainment()
                                     : ShouldApplyBlockSizeContainment();
  }
  inline bool ShouldApplyHeightContainment() const {
    NOT_DESTROYED();
    return IsHorizontalWritingMode() ? ShouldApplyBlockSizeContainment()
                                     : ShouldApplyInlineSizeContainment();
  }
  inline bool ShouldApplyStyleContainment() const {
    NOT_DESTROYED();
    return StyleRef().ContainsStyle();
  }
  inline bool ShouldApplyContentContainment() const {
    NOT_DESTROYED();
    return ShouldApplyStyleContainment() && ShouldApplyPaintContainment() &&
           ShouldApplyLayoutContainment();
  }
  inline bool ShouldApplyStrictContainment() const {
    NOT_DESTROYED();
    return ShouldApplyStyleContainment() && ShouldApplyPaintContainment() &&
           ShouldApplyLayoutContainment() && ShouldApplySizeContainment();
  }
  inline bool ShouldApplyAnyContainment() const {
    NOT_DESTROYED();
    return ShouldApplyPaintContainment() || ShouldApplyLayoutContainment() ||
           ShouldApplyStyleContainment() || ShouldApplyBlockSizeContainment() ||
           ShouldApplyInlineSizeContainment();
  }

  inline bool CanMatchSizeContainerQueries() const {
    NOT_DESTROYED();
    if (Element* element = DynamicTo<Element>(GetNode()))
      return StyleRef().CanMatchSizeContainerQueries(*element);
    return false;
  }

  inline bool IsStackingContext() const {
    NOT_DESTROYED();
    return IsStackingContext(StyleRef());
  }
  inline bool IsStackingContext(const ComputedStyle& style) const {
    NOT_DESTROYED();
    // This is an inlined version of the following:
    // `IsStackingContextWithoutContainment() ||
    //  ShouldApplyLayoutContainment() ||
    //  ShouldApplyPaintContainment()`
    // The reason it is inlined is that the containment checks share
    // common logic, which is extracted here to avoid repeated computation.
    return style.IsStackingContextWithoutContainment() ||
           ((style.ContainsLayout() || style.ContainsPaint()) &&
            (!IsInline() || IsAtomicInlineLevel()) && !IsRubyText() &&
            (!IsTablePart() || IsLayoutBlockFlow()));
  }

  inline bool IsStacked() const {
    NOT_DESTROYED();
    return IsStacked(StyleRef());
  }
  inline bool IsStacked(const ComputedStyle& style) const {
    NOT_DESTROYED();
    return style.GetPosition() != EPosition::kStatic ||
           IsStackingContext(style);
  }

  void NotifyPriorityScrollAnchorStatusChanged();

 private:
  //////////////////////////////////////////
  // Helper functions. Dangerous to use!
  void SetPreviousSibling(LayoutObject* previous) {
    NOT_DESTROYED();
    previous_ = previous;
  }
  void SetNextSibling(LayoutObject* next) {
    NOT_DESTROYED();
    next_ = next;
  }
  void SetParent(LayoutObject* parent) {
    NOT_DESTROYED();
    parent_ = parent;

    // Only update if our flow thread state is different from our new parent and
    // if we're not a LayoutFlowThread.
    // A LayoutFlowThread is always considered to be inside itself, so it never
    // has to change its state in response to parent changes.
    bool inside_flow_thread = parent && parent->IsInsideFlowThread();
    if (inside_flow_thread != IsInsideFlowThread() && !IsLayoutFlowThread())
      SetIsInsideFlowThreadIncludingDescendants(inside_flow_thread);
  }

  //////////////////////////////////////////
 private:
#if DCHECK_IS_ON()
  bool IsSetNeedsLayoutForbidden() const {
    NOT_DESTROYED();
    return set_needs_layout_forbidden_;
  }
  void SetNeedsLayoutIsForbidden(bool flag) {
    NOT_DESTROYED();
    set_needs_layout_forbidden_ = flag;
  }
#endif

  void AddAbsoluteRectForLayer(gfx::Rect& result);
  bool RequiresAnonymousTableWrappers(const LayoutObject*) const;

 public:
#if DCHECK_IS_ON()
  // Dump this layout object to the specified string builder.
  void DumpLayoutObject(StringBuilder&,
                        bool dump_address,
                        unsigned show_tree_character_offset) const;
  void ShowTreeForThis() const;
  void ShowLayoutTreeForThis() const;
  void ShowLineTreeForThis() const;
  void ShowLayoutObject() const;

  // Dump the subtree established by this layout object to the specified string
  // builder. There will be one object per line, and descendants will be
  // indented according to their tree level. The optional "marked_foo"
  // parameters can be used to mark up to two objects in the subtree with a
  // label.
  void DumpLayoutTreeAndMark(StringBuilder&,
                             const LayoutObject* marked_object1 = nullptr,
                             const char* marked_label1 = nullptr,
                             const LayoutObject* marked_object2 = nullptr,
                             const char* marked_label2 = nullptr,
                             unsigned depth = 0) const;
#endif  // DCHECK_IS_ON()

  // This function is used to create the appropriate LayoutObject based
  // on the style, in particular 'display' and 'content'.
  // "display: none" or "display: contents" are the only times this function
  // will return nullptr.
  //
  // For renderer creation, the inline-* values create the same renderer
  // as the non-inline version. The difference is that inline-* sets
  // is_inline_ during initialization. This means that
  // "display: inline-table" creates a LayoutTable, like "display: table".
  //
  // Ideally every Element::createLayoutObject would call this function to
  // respond to 'display' but there are deep rooted assumptions about
  // which LayoutObject is created on a fair number of Elements. This
  // function also doesn't handle the default association between a tag
  // and its renderer (e.g. <iframe> creates a LayoutIFrame even if the
  // initial 'display' value is inline).
  static LayoutObject* CreateObject(Element*,
                                    const ComputedStyle&,
                                    LegacyLayout);

  bool IsPseudoElement() const {
    NOT_DESTROYED();
    return GetNode() && GetNode()->IsPseudoElement();
  }

  virtual bool IsBoxModelObject() const {
    NOT_DESTROYED();
    return false;
  }
  bool IsBR() const {
    NOT_DESTROYED();
    return IsOfType(kLayoutObjectBr);
  }
  bool IsCanvas() const {
    NOT_DESTROYED();
    return IsOfType(kLayoutObjectCanvas);
  }
  bool IsCounter() const {
    NOT_DESTROYED();
    return IsOfType(kLayoutObjectCounter);
  }
  bool IsEmbeddedObject() const {
    NOT_DESTROYED();
    return IsOfType(kLayoutObjectEmbeddedObject);
  }
  bool IsFieldset() const {
    NOT_DESTROYED();
    return IsOfType(kLayoutObjectFieldset);
  }
  bool IsLayoutNGFieldset() const {
    NOT_DESTROYED();
    return IsOfType(kLayoutObjectNGFieldset);
  }
  bool IsFieldsetIncludingNG() const {
    NOT_DESTROYED();
    return IsFieldset() || IsLayoutNGFieldset();
  }
  bool IsFileUploadControl() const {
    NOT_DESTROYED();
    return IsOfType(kLayoutObjectFileUploadControl);
  }
  bool IsFrame() const {
    NOT_DESTROYED();
    return IsOfType(kLayoutObjectFrame);
  }
  bool IsFrameSet() const {
    NOT_DESTROYED();
    return IsOfType(kLayoutObjectFrameSet);
  }
  bool IsLayoutNGFrameSet() const {
    NOT_DESTROYED();
    return IsOfType(kLayoutObjectNGFrameSet);
  }
  bool IsFrameSetIncludingNG() const {
    NOT_DESTROYED();
    return IsFrameSet() || IsLayoutNGFrameSet();
  }
  bool IsInsideListMarkerForCustomContent() const {
    NOT_DESTROYED();
    return IsOfType(kLayoutObjectInsideListMarker);
  }
  bool IsLayoutNGBlockFlow() const {
    NOT_DESTROYED();
    return IsOfType(kLayoutObjectNGBlockFlow);
  }
  bool IsLayoutNGFlexibleBox() const {
    NOT_DESTROYED();
    return IsOfType(kLayoutObjectNGFlexibleBox);
  }
  bool IsLayoutNGGrid() const {
    NOT_DESTROYED();
    return IsOfType(kLayoutObjectNGGrid);
  }
  bool IsLayoutNGListItem() const {
    NOT_DESTROYED();
    return IsOfType(kLayoutObjectNGListItem);
  }
  bool IsLayoutNGInsideListMarker() const {
    NOT_DESTROYED();
    return IsOfType(kLayoutObjectNGInsideListMarker);
  }
  bool IsLayoutNGOutsideListMarker() const {
    NOT_DESTROYED();
    return IsOfType(kLayoutObjectNGOutsideListMarker);
  }
  bool IsLayoutNGProgress() const {
    NOT_DESTROYED();
    return IsOfType(kLayoutObjectNGProgress);
  }
  bool IsLayoutNGText() const {
    NOT_DESTROYED();
    return IsOfType(kLayoutObjectNGText);
  }
  bool IsLayoutNGTextCombine() const {
    NOT_DESTROYED();
    return IsOfType(kLayoutObjectNGTextCombine);
  }
  bool IsLayoutNGView() const {
    NOT_DESTROYED();
    return IsOfType(kLayoutObjectNGView);
  }
  bool IsLayoutTableCol() const {
    NOT_DESTROYED();
    return IsOfType(kLayoutObjectTableCol);
  }
  bool IsListItem() const {
    NOT_DESTROYED();
    return IsOfType(kLayoutObjectListItem);
  }
  bool IsListMarkerForNormalContent() const {
    NOT_DESTROYED();
    return IsOfType(kLayoutObjectListMarker);
  }
  bool IsListMarkerImage() const {
    NOT_DESTROYED();
    return IsOfType(kLayoutObjectListMarkerImage);
  }
  bool IsMathML() const {
    NOT_DESTROYED();
    return IsOfType(kLayoutObjectMathML);
  }
  bool IsMathMLRoot() const {
    NOT_DESTROYED();
    return IsOfType(kLayoutObjectMathMLRoot);
  }
  bool IsMedia() const {
    NOT_DESTROYED();
    return IsOfType(kLayoutObjectMedia);
  }
  bool IsOutsideListMarkerForCustomContent() const {
    NOT_DESTROYED();
    return IsOfType(kLayoutObjectOutsideListMarker);
  }
  bool IsProgress() const {
    NOT_DESTROYED();
    return IsOfType(kLayoutObjectProgress);
  }
  bool IsQuote() const {
    NOT_DESTROYED();
    return IsOfType(kLayoutObjectQuote);
  }
  bool IsButtonIncludingNG() const {
    NOT_DESTROYED();
    return IsOfType(kLayoutObjectButton) || IsOfType(kLayoutObjectNGButton);
  }
  bool IsLayoutNGButton() const {
    NOT_DESTROYED();
    return IsOfType(kLayoutObjectNGButton);
  }
  bool IsLayoutNGCustom() const {
    NOT_DESTROYED();
    return IsOfType(kLayoutObjectNGCustom);
  }
  bool IsLayoutGrid() const {
    NOT_DESTROYED();
    return IsOfType(kLayoutObjectGrid);
  }
  bool IsLayoutGridIncludingNG() const {
    NOT_DESTROYED();
    return IsOfType(kLayoutObjectGrid) || IsOfType(kLayoutObjectNGGrid);
  }
  bool IsLayoutIFrame() const {
    NOT_DESTROYED();
    return IsOfType(kLayoutObjectIFrame);
  }
  bool IsLayoutImage() const {
    NOT_DESTROYED();
    return IsOfType(kLayoutObjectImage);
  }
  bool IsLayoutMultiColumnSet() const {
    NOT_DESTROYED();
    return IsOfType(kLayoutObjectMultiColumnSet);
  }
  bool IsLayoutMultiColumnSpannerPlaceholder() const {
    NOT_DESTROYED();
    return IsOfType(kLayoutObjectMultiColumnSpannerPlaceholder);
  }
  bool IsLayoutReplaced() const {
    NOT_DESTROYED();
    return IsOfType(kLayoutObjectReplaced);
  }
  bool IsLayoutCustomScrollbarPart() const {
    NOT_DESTROYED();
    return IsOfType(kLayoutObjectCustomScrollbarPart);
  }
  bool IsLayoutView() const {
    NOT_DESTROYED();
    return IsOfType(kLayoutObjectView);
  }
  bool IsRuby() const {
    NOT_DESTROYED();
    return IsOfType(kLayoutObjectRuby);
  }
  bool IsRubyBase() const {
    NOT_DESTROYED();
    return IsOfType(kLayoutObjectRubyBase);
  }
  bool IsRubyRun() const {
    NOT_DESTROYED();
    return IsOfType(kLayoutObjectRubyRun);
  }
  bool IsRubyText() const {
    NOT_DESTROYED();
    return IsOfType(kLayoutObjectRubyText);
  }
  bool IsTable() const {
    NOT_DESTROYED();
    return IsOfType(kLayoutObjectTable);
  }
  bool IsTableCaption() const {
    NOT_DESTROYED();
    return IsOfType(kLayoutObjectTableCaption);
  }
  bool IsTableCell() const {
    NOT_DESTROYED();
    return IsOfType(kLayoutObjectTableCell);
  }
  bool IsTableCellLegacy() const {
    NOT_DESTROYED();
    return IsOfType(kLayoutObjectTableCellLegacy);
  }
  bool IsTableRow() const {
    NOT_DESTROYED();
    return IsOfType(kLayoutObjectTableRow);
  }
  bool IsLegacyTableRow() const {
    NOT_DESTROYED();
    return IsTableRow() && !IsLayoutNGObject();
  }
  bool IsTableSection() const {
    NOT_DESTROYED();
    return IsOfType(kLayoutObjectTableSection);
  }
  bool IsLegacyTableSection() const {
    NOT_DESTROYED();
    return IsTableSection() && !IsLayoutNGObject();
  }
  bool IsTextAreaIncludingNG() const {
    NOT_DESTROYED();
    return IsOfType(kLayoutObjectTextControlMultiLine) ||
           IsOfType(kLayoutObjectNGTextControlMultiLine);
  }
  bool IsTextControlIncludingNG() const {
    NOT_DESTROYED();
    return IsOfType(kLayoutObjectTextControl) ||
           IsOfType(kLayoutObjectNGTextControlMultiLine) ||
           IsOfType(kLayoutObjectNGTextControlSingleLine);
  }
  bool IsTextFieldIncludingNG() const {
    NOT_DESTROYED();
    return IsOfType(kLayoutObjectTextControlSingleLine) ||
           IsOfType(kLayoutObjectNGTextControlSingleLine);
  }
  bool IsVideo() const {
    NOT_DESTROYED();
    return IsOfType(kLayoutObjectVideo);
  }
  bool IsWidget() const {
    NOT_DESTROYED();
    return IsOfType(kLayoutObjectWidget);
  }

  virtual bool IsImage() const {
    NOT_DESTROYED();
    return false;
  }

  virtual bool IsDocumentTransitionContent() const {
    NOT_DESTROYED();
    return false;
  }

  virtual bool IsInlineBlockOrInlineTable() const {
    NOT_DESTROYED();
    return false;
  }
  virtual bool IsLayoutBlock() const {
    NOT_DESTROYED();
    return false;
  }
  virtual bool IsLayoutBlockFlow() const {
    NOT_DESTROYED();
    return false;
  }
  virtual bool IsLayoutFlowThread() const {
    NOT_DESTROYED();
    return false;
  }
  virtual bool IsLayoutInline() const {
    NOT_DESTROYED();
    return false;
  }
  virtual bool IsLayoutEmbeddedContent() const {
    NOT_DESTROYED();
    return false;
  }
  virtual bool IsLayoutNGObject() const {
    NOT_DESTROYED();
    return false;
  }

  bool IsDocumentElement() const {
    NOT_DESTROYED();
    return GetDocument().documentElement() == node_;
  }
  // isBody is called from LayoutBox::styleWillChange and is thus quite hot.
  bool IsBody() const {
    NOT_DESTROYED();
    return GetNode() && GetNode()->HasTagName(html_names::kBodyTag);
  }

  bool IsHR() const;

  bool IsTablePart() const {
    NOT_DESTROYED();
    return IsTableCell() || IsLayoutTableCol() || IsTableCaption() ||
           IsTableRow() || IsTableSection();
  }
  virtual const LayoutNGTableInterface* ToLayoutNGTableInterface() const {
    NOT_DESTROYED();
    NOTREACHED();
    return nullptr;
  }
  virtual const LayoutNGTableSectionInterface* ToLayoutNGTableSectionInterface()
      const {
    NOT_DESTROYED();
    NOTREACHED();
    return nullptr;
  }
  virtual const LayoutNGTableRowInterface* ToLayoutNGTableRowInterface() const {
    NOT_DESTROYED();
    NOTREACHED();
    return nullptr;
  }
  virtual const LayoutNGTableCellInterface* ToLayoutNGTableCellInterface()
      const {
    NOT_DESTROYED();
    NOTREACHED();
    return nullptr;
  }
  virtual const LayoutNGGridInterface* ToLayoutNGGridInterface() const {
    NOT_DESTROYED();
    NOTREACHED();
    return nullptr;
  }
  inline bool IsBeforeContent() const;
  inline bool IsAfterContent() const;
  inline bool IsMarkerContent() const;
  inline bool IsBeforeOrAfterContent() const;
  static inline bool IsAfterContent(const LayoutObject* obj) {
    return obj && obj->IsAfterContent();
  }

  // Returns true if the text is generated (from, e.g., list marker,
  // pseudo-element, ...) instead of from a DOM text node. See
  // |NGTextType::kLayoutGenerated| for the other type of generated text.
  bool IsStyleGenerated() const;

  bool HasCounterNodeMap() const {
    NOT_DESTROYED();
    return bitfields_.HasCounterNodeMap();
  }
  void SetHasCounterNodeMap(bool has_counter_node_map) {
    NOT_DESTROYED();
    bitfields_.SetHasCounterNodeMap(has_counter_node_map);
  }

  bool IsTruncated() const {
    NOT_DESTROYED();
    return bitfields_.IsTruncated();
  }
  void SetIsTruncated(bool is_truncated) {
    NOT_DESTROYED();
    bitfields_.SetIsTruncated(is_truncated);
  }

  bool EverHadLayout() const {
    NOT_DESTROYED();
    return bitfields_.EverHadLayout();
  }

  bool ChildrenInline() const {
    NOT_DESTROYED();
    return bitfields_.ChildrenInline();
  }
  void SetChildrenInline(bool b) {
    NOT_DESTROYED();
    bitfields_.SetChildrenInline(b);
  }

  bool AlwaysCreateLineBoxesForLayoutInline() const {
    NOT_DESTROYED();
    DCHECK(IsLayoutInline());
    return bitfields_.AlwaysCreateLineBoxesForLayoutInline();
  }
  void SetAlwaysCreateLineBoxesForLayoutInline(bool always_create_line_boxes) {
    NOT_DESTROYED();
    DCHECK(IsLayoutInline());
    bitfields_.SetAlwaysCreateLineBoxesForLayoutInline(
        always_create_line_boxes);
  }

  bool AncestorLineBoxDirty() const {
    NOT_DESTROYED();
    return bitfields_.AncestorLineBoxDirty();
  }
  void SetAncestorLineBoxDirty(bool value = true) {
    NOT_DESTROYED();
    bitfields_.SetAncestorLineBoxDirty(value);
    if (value) {
      SetNeedsLayoutAndFullPaintInvalidation(
          layout_invalidation_reason::kLineBoxesChanged);
    }
  }

  void SetIsInsideFlowThreadIncludingDescendants(bool);

  bool IsInsideFlowThread() const {
    NOT_DESTROYED();
    return bitfields_.IsInsideFlowThread();
  }
  void SetIsInsideFlowThread(bool inside_flow_thread) {
    NOT_DESTROYED();
    bitfields_.SetIsInsideFlowThread(inside_flow_thread);
  }

  // Remove this object and all descendants from the containing
  // LayoutFlowThread.
  void RemoveFromLayoutFlowThread();

  // Return true if this object might be inside a fragmentation context, or
  // false if it's definitely *not* inside one.
  bool MightBeInsideFragmentationContext() const {
    NOT_DESTROYED();
    return IsInsideFlowThread() || GetDocument().Printing();
  }

  // FIXME: Until all SVG layoutObjects can be subclasses of
  // LayoutSVGModelObject we have to add SVG layoutObject methods to
  // LayoutObject with an NOTREACHED() default implementation.
  bool IsSVG() const {
    NOT_DESTROYED();
    return IsOfType(kLayoutObjectSVG);
  }
  bool IsSVGRoot() const {
    NOT_DESTROYED();
    return IsOfType(kLayoutObjectSVGRoot);
  }
  bool IsSVGChild() const {
    NOT_DESTROYED();
    return IsSVG() && !IsSVGRoot();
  }
  bool IsSVGContainer() const {
    NOT_DESTROYED();
    return IsOfType(kLayoutObjectSVGContainer);
  }
  bool IsSVGTransformableContainer() const {
    NOT_DESTROYED();
    return IsOfType(kLayoutObjectSVGTransformableContainer);
  }
  bool IsSVGViewportContainer() const {
    NOT_DESTROYED();
    return IsOfType(kLayoutObjectSVGViewportContainer);
  }
  bool IsSVGHiddenContainer() const {
    NOT_DESTROYED();
    return IsOfType(kLayoutObjectSVGHiddenContainer);
  }
  bool IsSVGShape() const {
    NOT_DESTROYED();
    return IsOfType(kLayoutObjectSVGShape);
  }
  bool IsSVGText() const {
    NOT_DESTROYED();
    return IsOfType(kLayoutObjectSVGText);
  }
  bool IsSVGTextPath() const {
    NOT_DESTROYED();
    return IsOfType(kLayoutObjectSVGTextPath);
  }
  bool IsSVGTSpan() const {
    NOT_DESTROYED();
    return IsOfType(kLayoutObjectSVGTSpan);
  }
  bool IsSVGInline() const {
    NOT_DESTROYED();
    return IsOfType(kLayoutObjectSVGInline);
  }
  bool IsSVGInlineText() const {
    NOT_DESTROYED();
    return IsOfType(kLayoutObjectSVGInlineText);
  }
  bool IsSVGImage() const {
    NOT_DESTROYED();
    return IsOfType(kLayoutObjectSVGImage);
  }
  bool IsSVGForeignObject() const {
    NOT_DESTROYED();
    return IsOfType(kLayoutObjectSVGForeignObject);
  }
  bool IsSVGForeignObjectIncludingNG() const {
    NOT_DESTROYED();
    return IsOfType(kLayoutObjectSVGForeignObject) ||
           IsOfType(kLayoutObjectNGSVGForeignObject);
  }
  bool IsSVGResourceContainer() const {
    NOT_DESTROYED();
    return IsOfType(kLayoutObjectSVGResourceContainer);
  }
  bool IsSVGFilterPrimitive() const {
    NOT_DESTROYED();
    return IsOfType(kLayoutObjectSVGFilterPrimitive);
  }
  bool IsNGSVGText() const {
    NOT_DESTROYED();
    return IsOfType(kLayoutObjectNGSVGText);
  }
  bool IsNGSVGForeignObject() const {
    NOT_DESTROYED();
    return IsOfType(kLayoutObjectNGSVGForeignObject);
  }

  // FIXME: Those belong into a SVG specific base-class for all layoutObjects
  // (see above). Unfortunately we don't have such a class yet, because it's not
  // possible for all layoutObjects to inherit from LayoutSVGObject ->
  // LayoutObject (some need LayoutBlock inheritance for instance)
  virtual void SetNeedsTransformUpdate() { NOT_DESTROYED(); }
  virtual void SetNeedsBoundariesUpdate();

  // Per the spec, mix-blend-mode applies to all non-SVG elements, and SVG
  // elements that are container elements, graphics elements or graphics
  // referencing elements.
  // https://www.w3.org/TR/compositing-1/#propdef-mix-blend-mode
  bool IsBlendingAllowed() const {
    NOT_DESTROYED();
    return !IsSVG() || IsSVGShape() || IsSVGImage() || IsSVGText() ||
           IsSVGInline() || IsSVGRoot() || IsSVGForeignObjectIncludingNG() ||
           IsNGSVGText() ||
           // Blending does not apply to non-renderable elements such as
           // patterns (see: https://github.com/w3c/fxtf-drafts/issues/309).
           (IsSVGContainer() && !IsSVGHiddenContainer());
  }
  virtual bool HasNonIsolatedBlendingDescendants() const {
    NOT_DESTROYED();
    // This is only implemented for layout objects that containt SVG flow.
    // For HTML/CSS layout objects, use the PaintLayer version instead.
    DCHECK(IsSVG());
    return false;
  }
  enum DescendantIsolationState {
    kDescendantIsolationRequired,
    kDescendantIsolationNeedsUpdate,
  };
  virtual void DescendantIsolationRequirementsChanged(
      DescendantIsolationState) {
    NOT_DESTROYED();
  }

  // Per SVG 1.1 objectBoundingBox ignores clipping, masking, filter effects,
  // opacity and stroke-width.
  // This is used for all computation of objectBoundingBox relative units and by
  // SVGGraphicsElement::getBBox().
  // NOTE: Markers are not specifically ignored here by SVG 1.1 spec, but we
  // ignore them since stroke-width is ignored (and marker size can depend on
  // stroke-width). objectBoundingBox is returned in local coordinates and
  // always unzoomed.
  // The name objectBoundingBox is taken from the SVG 1.1 spec.
  virtual gfx::RectF ObjectBoundingBox() const;

  // Returns the smallest rectangle enclosing all of the painted content
  // respecting clipping, masking, filters, opacity, stroke-width and markers.
  // The local SVG coordinate space is the space where localSVGTransform
  // applies. For SVG objects defining viewports (e.g.
  // LayoutSVGViewportContainer and  LayoutSVGResourceMarker), the local SVG
  // coordinate space is the viewport space.
  virtual gfx::RectF VisualRectInLocalSVGCoordinates() const;

  // Like VisualRectInLocalSVGCoordinates() but does not include visual overflow
  // (name is misleading). May be zoomed (currently only for <foreignObject>,
  // which represents this via its LocalToSVGParentTransform()).
  virtual gfx::RectF StrokeBoundingBox() const;

  // This returns the transform applying to the local SVG coordinate space,
  // which combines the CSS transform properties and animation motion transform.
  // See SVGElement::calculateTransform().
  // Most callsites want localToSVGParentTransform() instead.
  virtual AffineTransform LocalSVGTransform() const;

  // Returns the full transform mapping from local coordinates to parent's local
  // coordinates. For most SVG objects, this is the same as localSVGTransform.
  // For SVG objects defining viewports (see visualRectInLocalSVGCoordinates),
  // this includes any viewport transforms and x/y offsets as well as
  // localSVGTransform.
  virtual AffineTransform LocalToSVGParentTransform() const {
    NOT_DESTROYED();
    return LocalSVGTransform();
  }

  // End of SVG-specific methods.

  bool IsAnonymous() const {
    NOT_DESTROYED();
    return bitfields_.IsAnonymous();
  }
  bool IsAnonymousBlock() const {
    NOT_DESTROYED();
    // This function is kept in sync with anonymous block creation conditions in
    // LayoutBlock::createAnonymousBlock(). This includes creating an anonymous
    // LayoutBlock having a BLOCK or BOX display. Other classes such as
    // LayoutTextFragment are not LayoutBlocks and will return false.
    // See https://bugs.webkit.org/show_bug.cgi?id=56709.
    return IsAnonymous() &&
           (StyleRef().Display() == EDisplay::kBlock ||
            StyleRef().Display() == EDisplay::kWebkitBox) &&
           StyleRef().StyleType() == kPseudoIdNone && IsLayoutBlock() &&
           !IsLayoutFlowThread() && !IsLayoutMultiColumnSet();
  }
  // This is similar to the negation of IsAnonymous, with a single difference.
  // When a block is inside an inline, there is an anonymous block that is a
  // continuation of the inline, wrapping the block that is inside it, as
  // https://www.w3.org/TR/CSS21/visuren.html#anonymous-block-level describes.
  // That anonymous block also returns true here.  This allows us to track
  // when layout object parent-child relationships correspond to DOM
  // parent-child relationships.
  bool IsForElement() const;
  // If node has been split into continuations, it returns the first layout
  // object generated for the node.
  const LayoutObject* ContinuationRoot() const {
    NOT_DESTROYED();
    return GetNode() ? GetNode()->GetLayoutObject() : this;
  }
  LayoutObject* ContinuationRoot() {
    NOT_DESTROYED();
    return GetNode() ? GetNode()->GetLayoutObject() : this;
  }
  bool IsElementContinuation() const {
    NOT_DESTROYED();
    return GetNode() && GetNode()->GetLayoutObject() != this;
  }
  bool IsInlineElementContinuation() const {
    NOT_DESTROYED();
    return IsElementContinuation() && IsInline();
  }
  virtual LayoutBoxModelObject* VirtualContinuation() const {
    NOT_DESTROYED();
    return nullptr;
  }
  // Returns the |ComputedStyle| to use for painting outlines. When |this| is
  // a block in a continuation-chain, it may need to paint outlines if its
  // ancestor inline boxes in the DOM tree has outlines.
  const ComputedStyle* StyleForContinuationOutline() const {
    NOT_DESTROYED();
    if (UNLIKELY(IsAnonymous() && !IsInline()))
      return SlowStyleForContinuationOutline();
    return nullptr;
  }

  bool IsFloating() const {
    NOT_DESTROYED();
    return bitfields_.Floating();
  }

  bool IsFloatingWithNonContainingBlockParent() const {
    NOT_DESTROYED();
    return IsFloating() && Parent() && !Parent()->IsLayoutBlockFlow();
  }

  // absolute or fixed positioning
  bool IsOutOfFlowPositioned() const {
    NOT_DESTROYED();
    return bitfields_.IsOutOfFlowPositioned();
  }
  // relative or sticky positioning
  bool IsInFlowPositioned() const {
    NOT_DESTROYED();
    return bitfields_.IsInFlowPositioned();
  }
  bool IsRelPositioned() const {
    NOT_DESTROYED();
    return bitfields_.IsRelPositioned();
  }
  bool IsStickyPositioned() const {
    NOT_DESTROYED();
    return bitfields_.IsStickyPositioned();
  }
  bool IsFixedPositioned() const {
    NOT_DESTROYED();
    return IsOutOfFlowPositioned() &&
           StyleRef().GetPosition() == EPosition::kFixed;
  }
  bool IsAbsolutePositioned() const {
    NOT_DESTROYED();
    return IsOutOfFlowPositioned() &&
           StyleRef().GetPosition() == EPosition::kAbsolute;
  }
  bool IsPositioned() const {
    NOT_DESTROYED();
    return bitfields_.IsPositioned();
  }

  bool IsText() const {
    NOT_DESTROYED();
    return bitfields_.IsText();
  }
  bool IsBox() const {
    NOT_DESTROYED();
    return bitfields_.IsBox();
  }
  bool IsInline() const {
    NOT_DESTROYED();
    return bitfields_.IsInline();
  }  // inline object
  bool IsInLayoutNGInlineFormattingContext() const {
    NOT_DESTROYED();
    return bitfields_.IsInLayoutNGInlineFormattingContext();
  }
  bool ForceLegacyLayout() const {
    NOT_DESTROYED();
    return bitfields_.ForceLegacyLayout();
  }
  bool ForceLegacyLayoutForChildren() const;
  bool IsAtomicInlineLevel() const {
    NOT_DESTROYED();
    return bitfields_.IsAtomicInlineLevel();
  }
  bool IsBlockInInline() const {
    NOT_DESTROYED();
    return IsAnonymous() && !IsInline() && !IsFloatingOrOutOfFlowPositioned() &&
           Parent() && Parent()->IsLayoutInline();
  }
  bool IsHorizontalWritingMode() const {
    NOT_DESTROYED();
    return bitfields_.HorizontalWritingMode();
  }
  bool HasFlippedBlocksWritingMode() const {
    NOT_DESTROYED();
    return StyleRef().IsFlippedBlocksWritingMode();
  }

  // If HasFlippedBlocksWritingMode() is true, these functions flip the input
  // rect/point in blocks direction in this object's local coordinate space
  // (which is the ContainerBlock()'s space if this object is not a box).
  // For non-boxes, for better performance, the caller can prepare
  // |block_for_flipping| (= ContainingBlock()) if it will loop through many
  // rects/points to flip to avoid the cost of repeated ContainingBlock() calls.
  [[nodiscard]] LayoutRect FlipForWritingMode(
      const PhysicalRect& r,
      const LayoutBox* box_for_flipping = nullptr) const {
    NOT_DESTROYED();
    if (LIKELY(!HasFlippedBlocksWritingMode()))
      return r.ToLayoutRect();
    return {FlipForWritingModeInternal(r.X(), r.Width(), box_for_flipping),
            r.Y(), r.Width(), r.Height()};
  }
  [[nodiscard]] PhysicalRect FlipForWritingMode(
      const LayoutRect& r,
      const LayoutBox* box_for_flipping = nullptr) const {
    NOT_DESTROYED();
    if (LIKELY(!HasFlippedBlocksWritingMode()))
      return PhysicalRect(r);
    return {FlipForWritingModeInternal(r.X(), r.Width(), box_for_flipping),
            r.Y(), r.Width(), r.Height()};
  }
  [[nodiscard]] LayoutPoint FlipForWritingMode(
      const PhysicalOffset& p,
      const LayoutBox* box_for_flipping = nullptr) const {
    NOT_DESTROYED();
    if (LIKELY(!HasFlippedBlocksWritingMode()))
      return p.ToLayoutPoint();
    return {FlipForWritingModeInternal(p.left, LayoutUnit(), box_for_flipping),
            p.top};
  }
  [[nodiscard]] PhysicalOffset FlipForWritingMode(
      const LayoutPoint& p,
      const LayoutBox* box_for_flipping = nullptr) const {
    NOT_DESTROYED();
    if (LIKELY(!HasFlippedBlocksWritingMode()))
      return PhysicalOffset(p);
    return {FlipForWritingModeInternal(p.X(), LayoutUnit(), box_for_flipping),
            p.Y()};
  }

  bool HasLayer() const {
    NOT_DESTROYED();
    return bitfields_.HasLayer();
  }

  // This may be different from StyleRef().hasBoxDecorationBackground() because
  // some objects may have box decoration background other than from their own
  // style.
  bool HasBoxDecorationBackground() const {
    NOT_DESTROYED();
    return bitfields_.HasBoxDecorationBackground();
  }

  bool NeedsLayout() const {
    NOT_DESTROYED();
    return bitfields_.SelfNeedsLayoutForStyle() ||
           bitfields_.SelfNeedsLayoutForAvailableSpace() ||
           bitfields_.NormalChildNeedsLayout() ||
           bitfields_.PosChildNeedsLayout() ||
           bitfields_.NeedsSimplifiedNormalFlowLayout() ||
           bitfields_.NeedsPositionedMovementLayout();
  }

  bool NeedsPositionedMovementLayoutOnly() const {
    NOT_DESTROYED();
    return bitfields_.NeedsPositionedMovementLayout() &&
           !bitfields_.SelfNeedsLayoutForStyle() &&
           !bitfields_.SelfNeedsLayoutForAvailableSpace() &&
           !bitfields_.NormalChildNeedsLayout() &&
           !bitfields_.PosChildNeedsLayout() &&
           !bitfields_.NeedsSimplifiedNormalFlowLayout();
  }

  bool NeedsSimplifiedLayoutOnly() const {
    NOT_DESTROYED();
    // We don't need to check |SelfNeedsLayoutForAvailableSpace| as an
    // additional check will determine if we need to perform full layout based
    // on the available space.
    return (bitfields_.PosChildNeedsLayout() ||
            bitfields_.NeedsSimplifiedNormalFlowLayout()) &&
           !bitfields_.SelfNeedsLayoutForStyle() &&
           !bitfields_.NormalChildNeedsLayout() &&
           !bitfields_.NeedsPositionedMovementLayout();
  }

  bool SelfNeedsLayout() const {
    NOT_DESTROYED();
    return bitfields_.SelfNeedsLayoutForStyle() ||
           bitfields_.SelfNeedsLayoutForAvailableSpace();
  }
  bool SelfNeedsLayoutForStyle() const {
    NOT_DESTROYED();
    return bitfields_.SelfNeedsLayoutForStyle();
  }
  bool SelfNeedsLayoutForAvailableSpace() const {
    NOT_DESTROYED();
    return bitfields_.SelfNeedsLayoutForAvailableSpace();
  }
  bool NeedsPositionedMovementLayout() const {
    NOT_DESTROYED();
    return bitfields_.NeedsPositionedMovementLayout();
  }

  bool PosChildNeedsLayout() const {
    NOT_DESTROYED();
    return bitfields_.PosChildNeedsLayout();
  }
  bool NeedsSimplifiedNormalFlowLayout() const {
    NOT_DESTROYED();
    return bitfields_.NeedsSimplifiedNormalFlowLayout();
  }
  bool NormalChildNeedsLayout() const {
    NOT_DESTROYED();
    return bitfields_.NormalChildNeedsLayout();
  }
  bool NeedsCollectInlines() const {
    NOT_DESTROYED();
    return bitfields_.NeedsCollectInlines();
  }

  bool MaybeHasPercentHeightDescendant() const {
    NOT_DESTROYED();
    return bitfields_.MaybeHasPercentHeightDescendant();
  }
  void SetMaybeHasPercentHeightDescendant() {
    NOT_DESTROYED();
    bitfields_.SetMaybeHasPercentHeightDescendant(true);
  }

  // Return true if the min/max intrinsic logical widths aren't up-to-date.
  // Note that for objects that *don't* need to calculate intrinsic logical
  // widths (e.g. if inline-size is a fixed value, and no other inline lengths
  // are intrinsic, and the object isn't a descendant of something that needs
  // min/max), this flag will never be cleared (since the values will never be
  // calculated).
  bool IntrinsicLogicalWidthsDirty() const {
    NOT_DESTROYED();
    return bitfields_.IntrinsicLogicalWidthsDirty();
  }

  bool IntrinsicLogicalWidthsDependsOnBlockConstraints() const {
    NOT_DESTROYED();
    return bitfields_.IntrinsicLogicalWidthsDependsOnBlockConstraints();
  }
  void SetIntrinsicLogicalWidthsDependsOnBlockConstraints(bool b) {
    NOT_DESTROYED();
    bitfields_.SetIntrinsicLogicalWidthsDependsOnBlockConstraints(b);
  }
  bool IntrinsicLogicalWidthsChildDependsOnBlockConstraints() const {
    NOT_DESTROYED();
    return bitfields_.IntrinsicLogicalWidthsChildDependsOnBlockConstraints();
  }
  void SetIntrinsicLogicalWidthsChildDependsOnBlockConstraints(bool b) {
    NOT_DESTROYED();
    bitfields_.SetIntrinsicLogicalWidthsChildDependsOnBlockConstraints(b);
  }

  bool NeedsLayoutOverflowRecalc() const {
    NOT_DESTROYED();
    return bitfields_.SelfNeedsLayoutOverflowRecalc() ||
           bitfields_.ChildNeedsLayoutOverflowRecalc();
  }
  bool SelfNeedsLayoutOverflowRecalc() const {
    NOT_DESTROYED();
    return bitfields_.SelfNeedsLayoutOverflowRecalc();
  }
  bool ChildNeedsLayoutOverflowRecalc() const {
    NOT_DESTROYED();
    return bitfields_.ChildNeedsLayoutOverflowRecalc();
  }
  void SetSelfNeedsLayoutOverflowRecalc() {
    NOT_DESTROYED();
    bitfields_.SetSelfNeedsLayoutOverflowRecalc(true);
  }
  void SetChildNeedsLayoutOverflowRecalc() {
    NOT_DESTROYED();
    bitfields_.SetChildNeedsLayoutOverflowRecalc(true);
  }
  void ClearSelfNeedsLayoutOverflowRecalc() {
    NOT_DESTROYED();
    bitfields_.SetSelfNeedsLayoutOverflowRecalc(false);
  }
  void ClearChildNeedsLayoutOverflowRecalc() {
    NOT_DESTROYED();
    bitfields_.SetChildNeedsLayoutOverflowRecalc(false);
  }

  // CSS clip only applies when position is absolute or fixed. Prefer this check
  // over !StyleRef().HasAutoClip().
  bool HasClip() const {
    NOT_DESTROYED();
    return IsOutOfFlowPositioned() && !StyleRef().HasAutoClip();
  }
  bool HasNonVisibleOverflow() const {
    NOT_DESTROYED();
    return bitfields_.HasNonVisibleOverflow();
  }
  bool HasClipRelatedProperty() const;
  bool IsScrollContainer() const {
    NOT_DESTROYED();
    // Always check HasNonVisibleOverflow() in case the object is not allowed to
    // have non-visible overflow.
    // Replaced elements don't support scrolling. If overflow is non visible,
    // the behaviour applied is equivalent to `clip`. See discussion at:
    // https://github.com/w3c/csswg-drafts/issues/7435.
    if (IsLayoutReplaced() &&
        RuntimeEnabledFeatures::CSSOverflowForReplacedElementsEnabled())
      return false;
    return HasNonVisibleOverflow() && StyleRef().IsScrollContainer();
  }

  // Not returning StyleRef().HasTransformRelatedProperty() because some objects
  // ignore the transform-related styles (e.g., LayoutInline).
  bool HasTransformRelatedProperty() const {
    NOT_DESTROYED();
    return bitfields_.HasTransformRelatedProperty();
  }
  // Compared to StyleRef().HasTransform(), this excludes objects that ignore
  // transform-related styles (e.g. LayoutInline).
  bool HasTransform() const {
    NOT_DESTROYED();
    return HasTransformRelatedProperty() && StyleRef().HasTransform();
  }
  // Similar to the above.
  bool Preserves3D() const {
    NOT_DESTROYED();
    return HasTransformRelatedProperty() && StyleRef().Preserves3D() &&
           !IsSVGChild();
  }
  bool IsTransformApplicable() const {
    NOT_DESTROYED();
    return IsBox() || IsSVG();
  }

  bool HasMask() const {
    NOT_DESTROYED();
    return StyleRef().HasMask();
  }
  bool HasClipPath() const {
    NOT_DESTROYED();
    return StyleRef().HasClipPath();
  }
  bool HasHiddenBackface() const {
    NOT_DESTROYED();
    return StyleRef().BackfaceVisibility() == EBackfaceVisibility::kHidden;
  }
  bool HasNonInitialBackdropFilter() const {
    NOT_DESTROYED();
    return StyleRef().HasNonInitialBackdropFilter();
  }

  // Returns |true| if any property that renders using filter operations is
  // used (including, but not limited to, 'filter' and 'box-reflect').
  // Not calling StyleRef().HasFilterInducingProperty() because some objects
  // ignore reflection style (e.g. LayoutInline, LayoutSVGBlock).
  bool HasFilterInducingProperty() const {
    NOT_DESTROYED();
    return StyleRef().HasNonInitialFilter() || HasReflection();
  }

  bool HasShapeOutside() const {
    NOT_DESTROYED();
    return StyleRef().ShapeOutside();
  }

  // Return true if the given object is the effective root scroller in its
  // Document. See |effective root scroller| in page/scrolling/README.md.
  // Note: a root scroller always establishes a PaintLayer.
  // This bit is updated in
  // RootScrollerController::RecomputeEffectiveRootScroller in the LayoutClean
  // document lifecycle phase.
  bool IsEffectiveRootScroller() const {
    NOT_DESTROYED();
    return bitfields_.IsEffectiveRootScroller();
  }

  // Returns true if the given object is the global root scroller. See
  // |global root scroller| in page/scrolling/README.md.
  bool IsGlobalRootScroller() const {
    NOT_DESTROYED();
    return bitfields_.IsGlobalRootScroller();
  }

  bool IsHTMLLegendElement() const {
    NOT_DESTROYED();
    return bitfields_.IsHTMLLegendElement();
  }

  // Returns true if this can be used as a rendered legend.
  bool IsRenderedLegendCandidate() const {
    NOT_DESTROYED();
    // Note, we can't directly use LayoutObject::IsFloating() because in the
    // case where the legend is a flex/grid item, LayoutObject::IsFloating()
    // could get set to false, even if the legend's computed style indicates
    // that it is floating.
    return IsHTMLLegendElement() && !IsOutOfFlowPositioned() &&
           !Style()->IsFloating();
  }

  // Return true if this is the "rendered legend" of a fieldset. They get
  // special treatment, in that they establish a new formatting context, and
  // shrink to fit if no logical width is specified.
  //
  // This function is performance sensitive.
  inline bool IsRenderedLegend() const {
    NOT_DESTROYED();
    if (LIKELY(!IsRenderedLegendCandidate()))
      return false;

    return IsRenderedLegendInternal();
  }

  bool IsRenderedLegendInternal() const;

  // Returns true if this object represents ::marker for the first SUMMARY
  // child of a DETAILS, and list-style-type is disclosure-*.
  bool IsListMarkerForSummary() const;

  // Returns true if this object is a proper descendant of any list marker.
  bool IsInListMarker() const;

  bool IsTextDecorationBoundary(NGStyleVariant) const;

  // The pseudo element style can be cached or uncached. Use the cached method
  // if the pseudo element doesn't respect any pseudo classes (and therefore
  // has no concept of changing state). The cached pseudo style always inherits
  // from the originating element's style (because we can cache only one
  // version), while the uncached pseudo style can inherit from any style.
  const ComputedStyle* GetCachedPseudoElementStyle(PseudoId) const;
  scoped_refptr<ComputedStyle> GetUncachedPseudoElementStyle(
      const StyleRequest&) const;

  // Returns the ::selection style, which may be stored in StyleCachedData (old
  // impl) or StyleHighlightData (new impl).
  // TODO(crbug.com/1024156): inline and remove on shipping HighlightInheritance
  const ComputedStyle* GetSelectionStyle() const;

  LayoutView* View() const {
    NOT_DESTROYED();
    return GetDocument().GetLayoutView();
  }
  LocalFrameView* GetFrameView() const {
    NOT_DESTROYED();
    return GetDocument().View();
  }

  bool IsRooted() const;

  Node* GetNode() const {
    NOT_DESTROYED();
    return IsAnonymous() ? nullptr : node_;
  }

  Node* NonPseudoNode() const {
    NOT_DESTROYED();
    return IsPseudoElement() ? nullptr : GetNode();
  }

  void ClearNode() {
    NOT_DESTROYED();
    node_ = nullptr;
  }

  // Returns the styled node that caused the generation of this layoutObject.
  // This is the same as node() except for layoutObjects of :before, :after and
  // :first-letter pseudo elements for which their parent node is returned.
  Node* GeneratingNode() const {
    NOT_DESTROYED();
    return IsPseudoElement() ? GetNode()->ParentOrShadowHostNode() : GetNode();
  }

  Document& GetDocument() const {
    NOT_DESTROYED();
    DCHECK(node_ || Parent());  // crbug.com/402056
    return node_ ? node_->GetDocument() : Parent()->GetDocument();
  }
  LocalFrame* GetFrame() const {
    NOT_DESTROYED();
    return GetDocument().GetFrame();
  }

  virtual LayoutMultiColumnSpannerPlaceholder* SpannerPlaceholder() const {
    NOT_DESTROYED();
    return nullptr;
  }
  bool IsColumnSpanAll() const {
    NOT_DESTROYED();
    return StyleRef().GetColumnSpan() == EColumnSpan::kAll &&
           SpannerPlaceholder();
  }

  // We include IsButtonOrNGButton() in this check, because buttons are
  // implemented using flex box but should still support things like
  // first-line, first-letter and text-overflow.
  // The flex box and grid specs require that flex box and grid do not
  // support first-line|first-letter, though.
  // When LayoutObject and display do not agree, allow first-line|first-letter
  // only when both indicate it's a block container.
  // TODO(cbiesinger): Remove when buttons are implemented with align-items
  // instead of flex box. crbug.com/226252.
  bool BehavesLikeBlockContainer() const {
    NOT_DESTROYED();
    return (IsLayoutBlockFlow() && StyleRef().IsDisplayBlockContainer()) ||
           IsButtonIncludingNG();
  }

  // May be optionally passed to container() and various other similar methods
  // that search the ancestry for some sort of containing block. Used to
  // determine if we skipped certain objects while walking the ancestry.
  class AncestorSkipInfo {
    STACK_ALLOCATED();

   public:
    AncestorSkipInfo(const LayoutObject* ancestor,
                     bool check_for_filters = false)
        : ancestor_(ancestor), check_for_filters_(check_for_filters) {}

    // Update skip info output based on the layout object passed.
    void Update(const LayoutObject& object) {
      if (&object == ancestor_)
        ancestor_skipped_ = true;
      if (check_for_filters_ && object.HasFilterInducingProperty())
        filter_skipped_ = true;
    }

#if DCHECK_IS_ON()
    void AssertClean() {
      DCHECK(!ancestor_skipped_);
      DCHECK(!filter_skipped_);
    }
#endif

    bool AncestorSkipped() const { return ancestor_skipped_; }
    bool FilterSkipped() const {
      DCHECK(check_for_filters_);
      return filter_skipped_;
    }

   private:
    // Input: A potential ancestor to look for. If we walk past this one while
    // walking the ancestry in search of some containing block, ancestorSkipped
    // will be set to true.
    const LayoutObject* ancestor_;
    // Input: When set, we'll check if we skip objects with filter inducing
    // properties.
    bool check_for_filters_;

    // Output: Set to true if |ancestor| was walked past while walking the
    // ancestry.
    bool ancestor_skipped_ = false;
    // Output: Set to true if we walked past a filter object. This will be set
    // regardless of the value of |ancestor|.
    bool filter_skipped_ = false;
  };

  // This function returns the containing block of the object.
  // Due to CSS being inconsistent, a containing block can be a relatively
  // positioned inline, thus we can't return a LayoutBlock from this function.
  //
  // This method is extremely similar to containingBlock(), but with a few
  // notable exceptions.
  // (1) For normal flow elements, it just returns the parent.
  // (2) For absolute positioned elements, it will return a relative
  //     positioned inline. containingBlock() simply skips relpositioned inlines
  //     and lets an enclosing block handle the layout of the positioned object.
  //     This does mean that computePositionedLogicalWidth and
  //     computePositionedLogicalHeight have to use container().
  //
  // Note that floating objects don't belong to either of the above exceptions.
  //
  // This function should be used for any invalidation as it would correctly
  // walk the containing block chain. See e.g. markContainerChainForLayout.
  // It is also used for correctly sizing absolutely positioned elements
  // (point 3 above).
  LayoutObject* Container(AncestorSkipInfo* = nullptr) const;
  // Finds the container as if this object is absolute-position.
  LayoutObject* ContainerForAbsolutePosition(AncestorSkipInfo* = nullptr) const;
  // Finds the container as if this object is fixed-position.
  LayoutObject* ContainerForFixedPosition(AncestorSkipInfo* = nullptr) const;

  bool CanContainOutOfFlowPositionedElement(EPosition position) const {
    NOT_DESTROYED();
    DCHECK(position == EPosition::kAbsolute || position == EPosition::kFixed);
    return (position == EPosition::kAbsolute &&
            CanContainAbsolutePositionObjects()) ||
           (position == EPosition::kFixed && CanContainFixedPositionObjects());
  }

  // Returns true if style would make this object an absolute container.
  bool ComputeIsAbsoluteContainer(const ComputedStyle* style) const;

  // Returns true if style would make this object a fixed container.
  // This value gets cached by bitfields_.can_contain_fixed_position_objects_.
  bool ComputeIsFixedContainer(const ComputedStyle* style) const;

  Element* OffsetParent(const Element* = nullptr) const;

  // Mark this object needing to re-run |CollectInlines()|. Ancestors may be
  // marked too if needed.
  void SetNeedsCollectInlines();
  void SetChildNeedsCollectInlines();
  void ClearNeedsCollectInlines() {
    NOT_DESTROYED();
    SetNeedsCollectInlines(false);
  }
  void SetNeedsCollectInlines(bool b) {
    NOT_DESTROYED();
    DCHECK(!GetDocument().InPostLifecycleSteps());
    bitfields_.SetNeedsCollectInlines(b);
  }

  void MarkContainerChainForLayout(bool schedule_relayout = true,
                                   SubtreeLayoutScope* = nullptr);
  void MarkParentForOutOfFlowPositionedChange();
  void SetNeedsLayout(LayoutInvalidationReasonForTracing,
                      MarkingBehavior = kMarkContainerChain,
                      SubtreeLayoutScope* = nullptr);
  void SetNeedsLayoutAndFullPaintInvalidation(
      LayoutInvalidationReasonForTracing,
      MarkingBehavior = kMarkContainerChain,
      SubtreeLayoutScope* = nullptr);

  void ClearNeedsLayoutWithoutPaintInvalidation();
  // |ClearNeedsLayout()| calls |SetShouldCheckForPaintInvalidation()|.
  void ClearNeedsLayout();
  void ClearNeedsLayoutWithFullPaintInvalidation();

  void SetChildNeedsLayout(MarkingBehavior = kMarkContainerChain,
                           SubtreeLayoutScope* = nullptr);
  void SetNeedsPositionedMovementLayout();
  void SetIntrinsicLogicalWidthsDirty(MarkingBehavior = kMarkContainerChain);
  void ClearIntrinsicLogicalWidthsDirty();

  void SetNeedsLayoutAndIntrinsicWidthsRecalc(
      LayoutInvalidationReasonForTracing reason) {
    NOT_DESTROYED();
    SetNeedsLayout(reason);
    SetIntrinsicLogicalWidthsDirty();
  }
  void SetNeedsLayoutAndIntrinsicWidthsRecalcAndFullPaintInvalidation(
      LayoutInvalidationReasonForTracing reason) {
    NOT_DESTROYED();
    SetNeedsLayoutAndFullPaintInvalidation(reason);
    SetIntrinsicLogicalWidthsDirty();
  }

  // Returns false when certain font changes (e.g., font-face rule changes, web
  // font loaded, etc) have occurred, in which case |this| needs relayout.
  virtual bool IsFontFallbackValid() const;

  // Traverses subtree, and marks all layout objects as need relayout, repaint
  // and preferred width recalc. Also invalidates shaping on all text nodes.
  virtual void InvalidateSubtreeLayoutForFontUpdates();

  void InvalidateIntersectionObserverCachedRects();

  void SetPositionState(EPosition position) {
    NOT_DESTROYED();
    DCHECK(
        (position != EPosition::kAbsolute && position != EPosition::kFixed) ||
        IsBox());
    bitfields_.SetPositionedState(position);
  }
  void ClearPositionedState() {
    NOT_DESTROYED();
    bitfields_.ClearPositionedState();
  }

  void SetFloating(bool is_floating) {
    NOT_DESTROYED();
    bitfields_.SetFloating(is_floating);
  }
  void SetInline(bool is_inline) {
    NOT_DESTROYED();
    bitfields_.SetIsInline(is_inline);
  }

  // Return whether we can directly traverse fragments generated for this layout
  // object, when it comes to painting, hit-testing and other layout read
  // operations. If false is returned, we need to traverse the layout object
  // tree instead.
  bool CanTraversePhysicalFragments() const {
    NOT_DESTROYED();

    if (!bitfields_.MightTraversePhysicalFragments())
      return false;

    // Non-LayoutBox objects (such as LayoutInline) don't necessarily create NG
    // LayoutObjects. We'll allow traversing their fragments if they are laid
    // out by an NG container.
    if (!IsBox())
      return IsInLayoutNGInlineFormattingContext();
    return true;
  }

  // Return true if |this| produces one or more inline fragments, including
  // whitespace-only text fragments.
  virtual bool HasInlineFragments() const {
    NOT_DESTROYED();
    return false;
  }

  // Paint/Physical fragments are not in sync with LayoutObject tree until it is
  // laid out. For inline, it needs to check if the containing block is
  // layout-clean. crbug.com/963103
  bool IsFirstInlineFragmentSafe() const;
  void SetIsInLayoutNGInlineFormattingContext(bool);
  virtual wtf_size_t FirstInlineFragmentItemIndex() const {
    NOT_DESTROYED();
    return 0u;
  }
  virtual void ClearFirstInlineFragmentItemIndex() { NOT_DESTROYED(); }
  virtual void SetFirstInlineFragmentItemIndex(wtf_size_t) { NOT_DESTROYED(); }
  void SetForceLegacyLayout() {
    NOT_DESTROYED();
    DCHECK(!IsLayoutNGObject());
    bitfields_.SetForceLegacyLayout(true);
  }

  void SetHasBoxDecorationBackground(bool);

  void SetIsText() {
    NOT_DESTROYED();
    bitfields_.SetIsText(true);
  }
  void SetIsBox() {
    NOT_DESTROYED();
    bitfields_.SetIsBox(true);
  }
  void SetIsAtomicInlineLevel(bool is_atomic_inline_level) {
    NOT_DESTROYED();
    bitfields_.SetIsAtomicInlineLevel(is_atomic_inline_level);
  }
  void SetHorizontalWritingMode(bool has_horizontal_writing_mode) {
    NOT_DESTROYED();
    bitfields_.SetHorizontalWritingMode(has_horizontal_writing_mode);
  }
  void SetHasNonVisibleOverflow(bool has_non_visible_overflow) {
    NOT_DESTROYED();
    bitfields_.SetHasNonVisibleOverflow(has_non_visible_overflow);
  }
  void SetOverflowClipAxes(OverflowClipAxes axes) {
    NOT_DESTROYED();
    bitfields_.SetOverflowClipAxes(axes);
  }
  OverflowClipAxes GetOverflowClipAxes() const {
    NOT_DESTROYED();
    return bitfields_.GetOverflowClipAxes();
  }
  bool ShouldClipOverflowAlongEitherAxis() const {
    NOT_DESTROYED();
    return bitfields_.GetOverflowClipAxes() != kNoOverflowClip;
  }
  bool ShouldClipOverflowAlongBothAxis() const {
    NOT_DESTROYED();
    return bitfields_.GetOverflowClipAxes() == kOverflowClipBothAxis;
  }
  void SetHasLayer(bool has_layer) {
    NOT_DESTROYED();
    bitfields_.SetHasLayer(has_layer);
  }
  void SetHasTransformRelatedProperty(bool has_transform) {
    NOT_DESTROYED();
    bitfields_.SetHasTransformRelatedProperty(has_transform);
  }
  void SetHasReflection(bool has_reflection) {
    NOT_DESTROYED();
    bitfields_.SetHasReflection(has_reflection);
  }
  void SetCanContainAbsolutePositionObjects(bool can_contain) {
    NOT_DESTROYED();
    can_contain_absolute_position_objects_ = can_contain;
  }
  void SetCanContainFixedPositionObjects(bool can_contain_fixed_position) {
    NOT_DESTROYED();
    bitfields_.SetCanContainFixedPositionObjects(can_contain_fixed_position);
  }
  void SetIsEffectiveRootScroller(bool is_effective_root_scroller) {
    NOT_DESTROYED();
    bitfields_.SetIsEffectiveRootScroller(is_effective_root_scroller);
  }
  void SetIsGlobalRootScroller(bool is_global_root_scroller) {
    NOT_DESTROYED();
    bitfields_.SetIsGlobalRootScroller(is_global_root_scroller);
  }
  void SetIsHTMLLegendElement() {
    NOT_DESTROYED();
    bitfields_.SetIsHTMLLegendElement(true);
  }
  void SetWhitespaceChildrenMayChange(bool b) {
    NOT_DESTROYED();
    bitfields_.SetWhitespaceChildrenMayChange(b);
  }
  bool WhitespaceChildrenMayChange() const {
    NOT_DESTROYED();
    return bitfields_.WhitespaceChildrenMayChange();
  }
  void SetNeedsDevtoolsInfo(bool b) {
    NOT_DESTROYED();
    bitfields_.SetNeedsDevtoolsInfo(b);
  }
  bool NeedsDevtoolsInfo() const {
    NOT_DESTROYED();
    return bitfields_.NeedsDevtoolsInfo();
  }

  virtual void Paint(const PaintInfo&) const;

  virtual RecalcLayoutOverflowResult RecalcLayoutOverflow();

  // Recalculates visual overflow for this object and non-self-painting
  // PaintLayer descendants.
  virtual void RecalcVisualOverflow();
  void RecalcNormalFlowChildVisualOverflowIfNeeded();
#if DCHECK_IS_ON()
  // Enables DCHECK to ensure that the visual overflow for |this| is computed.
  // The actual invalidation is maintained in |PaintLayer|.
  void InvalidateVisualOverflow();
#endif

  // Subclasses must reimplement this method to compute the size and position
  // of this object and all its descendants.
  //
  // By default, layout only lays out the children that are marked for layout.
  // In some cases, layout has to force laying out more children. An example is
  // when the width of the LayoutObject changes as this impacts children with
  // 'width' set to auto.
  virtual void UpdateLayout() = 0;

  void HandleSubtreeModifications();
  virtual void SubtreeDidChange() { NOT_DESTROYED(); }

  // Flags used to mark if an object consumes subtree change notifications.
  bool ConsumesSubtreeChangeNotification() const {
    NOT_DESTROYED();
    return bitfields_.ConsumesSubtreeChangeNotification();
  }
  void SetConsumesSubtreeChangeNotification() {
    NOT_DESTROYED();
    bitfields_.SetConsumesSubtreeChangeNotification(true);
  }

  // Flags used to mark if a descendant subtree of this object has changed.

  // Returns true if the flag did change.
  bool NotifyOfSubtreeChange();
  bool WasNotifiedOfSubtreeChange() const {
    NOT_DESTROYED();
    return bitfields_.NotifiedOfSubtreeChange();
  }

  // Flags used to signify that a layoutObject needs to be notified by its
  // descendants that they have had their child subtree changed.
  void RegisterSubtreeChangeListenerOnDescendants(bool);
  bool HasSubtreeChangeListenerRegistered() const {
    NOT_DESTROYED();
    return bitfields_.SubtreeChangeListenerRegistered();
  }

  /* This function performs a layout only if one is needed. */
  DISABLE_CFI_PERF void LayoutIfNeeded() {
    NOT_DESTROYED();
    if (NeedsLayout())
      UpdateLayout();
  }

  void ForceLayout();
  void ForceLayoutWithPaintInvalidation() {
    NOT_DESTROYED();
    SetShouldDoFullPaintInvalidation();
    ForceLayout();
  }

  // Used for element state updates that cannot be fixed with a paint
  // invalidation and do not need a relayout.
  virtual void UpdateFromElement() { NOT_DESTROYED(); }

  virtual void AddAnnotatedRegions(Vector<AnnotatedRegionValue>&);

  // True for object types which override |AdditionalCompositingReasons|.
  virtual bool CanHaveAdditionalCompositingReasons() const;
  virtual CompositingReasons AdditionalCompositingReasons() const;

  // |accumulated_offset| is accumulated physical offset of this object from
  // the same origin as |hit_test_location|. The caller just ensures that
  // |hit_test_location| and |accumulated_offset| are in the same coordinate
  // space that is transform-compatible with this object (i.e. we can add 2d
  // local offset to it without considering transforms). The implementation
  // should not assume any specific coordinate space of them. The local offset
  // of |hit_test_location| in this object can be calculated by
  // |hit_test_location.Point() - accumulated_offset|.
  virtual bool HitTestAllPhases(HitTestResult&,
                                const HitTestLocation& hit_test_location,
                                const PhysicalOffset& accumulated_offset);
  // Returns the node that is ultimately added to the hit test result. Some
  // objects report a hit testing node that is not their own (such as
  // continuations and some psuedo elements) and it is important that the
  // node be consistent between point- and list-based hit test results.
  virtual Node* NodeForHitTest() const;
  virtual void UpdateHitTestResult(HitTestResult&, const PhysicalOffset&) const;
  // See HitTestAllPhases() for explanation of |hit_test_location| and
  // |accumulated_offset|.
  virtual bool NodeAtPoint(HitTestResult&,
                           const HitTestLocation& hit_test_location,
                           const PhysicalOffset& accumulated_offset,
                           HitTestPhase);

  virtual PositionWithAffinity PositionForPoint(const PhysicalOffset&) const;
  PositionWithAffinity CreatePositionWithAffinity(int offset,
                                                  TextAffinity) const;
  PositionWithAffinity CreatePositionWithAffinity(int offset) const;
  PositionWithAffinity FindPosition() const;
  PositionWithAffinity FirstPositionInOrBeforeThis() const;
  PositionWithAffinity LastPositionInOrAfterThis() const;
  PositionWithAffinity PositionAfterThis() const;
  PositionWithAffinity PositionBeforeThis() const;

  virtual void DirtyLinesFromChangedChild(
      LayoutObject*,
      MarkingBehavior marking_behaviour = kMarkContainerChain);

  // Set the style of the object and update the state of the object accordingly.
  // ApplyStyleChanges = kYes means we will apply any changes between the old
  // and new ComputedStyle like paint and size invalidations. If kNo, just set
  // the ComputedStyle member.
  enum class ApplyStyleChanges { kNo, kYes };
  void SetStyle(scoped_refptr<const ComputedStyle>,
                ApplyStyleChanges = ApplyStyleChanges::kYes);

  // Set the style of the object if it's generated content.
  void SetPseudoElementStyle(scoped_refptr<const ComputedStyle>,
                             bool match_parent_size = false);

  // In some cases we modify the ComputedStyle after the style recalc, either
  // for updating anonymous style or doing layout hacks for special elements
  // where we update the ComputedStyle during layout.
  // If the LayoutObject has an associated node, we will SetComputedStyle on
  // that node with the new ComputedStyle. Modifying the ComputedStyle of a node
  // outside of style recalc can break invariants in the style engine, so this
  // function must not gain any new call sites.
  void SetModifiedStyleOutsideStyleRecalc(scoped_refptr<const ComputedStyle>,
                                          ApplyStyleChanges);

  // This function returns an enclosing non-anonymous LayoutBlock for this
  // element. This function is not always returning the containing block as
  // defined by CSS. In particular:
  // - if the CSS containing block is a relatively positioned inline,
  //   the function returns the inline's enclosing non-anonymous LayoutBlock.
  //   This means that a LayoutInline would be skipped (expected as it's not a
  //   LayoutBlock) but so would be an inline LayoutTable or LayoutBlockFlow.
  //   TODO(jchaffraix): Is that REALLY what we want here?
  // - if the CSS containing block is anonymous, we find its enclosing
  //   non-anonymous LayoutBlock.
  //   Note that in the previous examples, the returned LayoutBlock has no
  //   logical relationship to the original element.
  //
  // LayoutBlocks are the one that handle laying out positioned elements,
  // thus this function is important during layout, to insert the positioned
  // elements into the correct LayoutBlock.
  //
  // See container() for the function that returns the containing block.
  // See LayoutBlock.h for some extra explanations on containing blocks.
  LayoutBlock* ContainingBlock(AncestorSkipInfo* = nullptr) const;

  bool IsAnonymousNGMulticolInlineWrapper() const;

  // Returns |container|'s containing block.
  static LayoutBlock* FindNonAnonymousContainingBlock(
      LayoutObject* container,
      AncestorSkipInfo* = nullptr);

  // Returns the nearest ancestor in the layout tree that is not anonymous,
  // or null if there is none.
  LayoutObject* NonAnonymousAncestor() const;

  // Returns the nearest ancestor in the layout tree that IsForElement(),
  // or null if there is none.
  LayoutObject* NearestAncestorForElement() const;

  const LayoutBlock* InclusiveContainingBlock() const;

  const LayoutBox* ContainingScrollContainer() const;

  bool CanContainAbsolutePositionObjects() const {
    NOT_DESTROYED();
    return can_contain_absolute_position_objects_;
  }
  bool CanContainFixedPositionObjects() const {
    NOT_DESTROYED();
    return bitfields_.CanContainFixedPositionObjects();
  }

  // Convert a rect/quad/point in ancestor coordinates to local physical
  // coordinates, taking transforms into account unless kIgnoreTransforms (not
  // allowed in the quad versions) is specified.
  // PhysicalRect parameter/return value is preferred to Float because they
  // force physical coordinates, unless we do need quads or float precision.
  // If the LayoutBoxModelObject ancestor is non-null, the input is in the
  // space of the ancestor.
  // Otherwise:
  //   If kTraverseDocumentBoundaries is specified, the input is in the space of
  //   the local root frame.
  //   Otherwise, the input is in the space of the containing frame.
  PhysicalRect AncestorToLocalRect(const LayoutBoxModelObject* ancestor,
                                   const PhysicalRect& rect,
                                   MapCoordinatesFlags mode = 0) const {
    NOT_DESTROYED();
    return PhysicalRect::EnclosingRect(
        AncestorToLocalQuad(ancestor, gfx::QuadF(gfx::RectF(rect)), mode)
            .BoundingBox());
  }
  gfx::QuadF AncestorToLocalQuad(const LayoutBoxModelObject*,
                                 const gfx::QuadF&,
                                 MapCoordinatesFlags mode = 0) const;
  PhysicalOffset AncestorToLocalPoint(const LayoutBoxModelObject* ancestor,
                                      const PhysicalOffset& p,
                                      MapCoordinatesFlags mode = 0) const {
    NOT_DESTROYED();
    return PhysicalOffset::FromPointFRound(
        AncestorToLocalPoint(ancestor, gfx::PointF(p), mode));
  }
  gfx::PointF AncestorToLocalPoint(const LayoutBoxModelObject* ancestor,
                                   const gfx::PointF& p,
                                   MapCoordinatesFlags = 0) const;

  // Convert a rect/quad/point in local physical coordinates into ancestor
  // coordinates, taking transforms into account unless kIgnoreTransforms is
  // specified.
  // PhysicalRect parameter/return value is preferred to Float because they
  // force physical coordinates, unless we do need quads or float precision.
  // If the LayoutBoxModelObject ancestor is non-null, the result will be in the
  // space of the ancestor.
  // Otherwise:
  //   If TraverseDocumentBoundaries is specified, the result will be in the
  //   space of the outermost root frame.
  //   Otherwise, the result will be in the space of the containing frame.
  // This method supports kUseGeometryMapperMode.
  PhysicalRect LocalToAncestorRect(const PhysicalRect& rect,
                                   const LayoutBoxModelObject* ancestor,
                                   MapCoordinatesFlags mode = 0) const;
  gfx::QuadF LocalRectToAncestorQuad(const PhysicalRect& rect,
                                     const LayoutBoxModelObject* ancestor,
                                     MapCoordinatesFlags mode = 0) const {
    NOT_DESTROYED();
    return LocalToAncestorQuad(gfx::QuadF(gfx::RectF(rect)), ancestor, mode);
  }
  gfx::QuadF LocalToAncestorQuad(const gfx::QuadF&,
                                 const LayoutBoxModelObject* ancestor,
                                 MapCoordinatesFlags = 0) const;
  PhysicalOffset LocalToAncestorPoint(const PhysicalOffset& p,
                                      const LayoutBoxModelObject* ancestor,
                                      MapCoordinatesFlags mode = 0) const {
    NOT_DESTROYED();
    return PhysicalOffset::FromPointFRound(
        LocalToAncestorPoint(gfx::PointF(p), ancestor, mode));
  }
  gfx::PointF LocalToAncestorPoint(const gfx::PointF&,
                                   const LayoutBoxModelObject* ancestor,
                                   MapCoordinatesFlags = 0) const;
  void LocalToAncestorRects(Vector<PhysicalRect>&,
                            const LayoutBoxModelObject* ancestor,
                            const PhysicalOffset& pre_offset,
                            const PhysicalOffset& post_offset) const;

  // Return the transformation matrix to map points from local to the coordinate
  // system of a container, taking transforms into account (kIgnoreTransforms is
  // not allowed).
  // Passing null for |ancestor| behaves the same as LocalToAncestorRect.
  TransformationMatrix LocalToAncestorTransform(
      const LayoutBoxModelObject* ancestor,
      MapCoordinatesFlags = 0) const;
  TransformationMatrix LocalToAbsoluteTransform(
      MapCoordinatesFlags mode = 0) const {
    NOT_DESTROYED();
    return LocalToAncestorTransform(nullptr, mode);
  }

  // Shorthands of the above LocalToAncestor* and AncestorToLocal* functions,
  // with nullptr as the ancestor. See the above functions for the meaning of
  // "absolute" coordinates.
  // This method supports kUseGeometryMapperMode.
  PhysicalRect LocalToAbsoluteRect(const PhysicalRect& rect,
                                   MapCoordinatesFlags mode = 0) const {
    NOT_DESTROYED();
    return LocalToAncestorRect(rect, nullptr, mode);
  }
  gfx::QuadF LocalRectToAbsoluteQuad(const PhysicalRect& rect,
                                     MapCoordinatesFlags mode = 0) const {
    NOT_DESTROYED();
    return LocalRectToAncestorQuad(rect, nullptr, mode);
  }
  gfx::QuadF LocalToAbsoluteQuad(const gfx::QuadF& quad,
                                 MapCoordinatesFlags mode = 0) const {
    NOT_DESTROYED();
    return LocalToAncestorQuad(quad, nullptr, mode);
  }
  PhysicalOffset LocalToAbsolutePoint(const PhysicalOffset& p,
                                      MapCoordinatesFlags mode = 0) const {
    NOT_DESTROYED();
    return LocalToAncestorPoint(p, nullptr, mode);
  }
  gfx::PointF LocalToAbsolutePoint(const gfx::PointF& p,
                                   MapCoordinatesFlags mode = 0) const {
    NOT_DESTROYED();
    return LocalToAncestorPoint(p, nullptr, mode);
  }
  PhysicalRect AbsoluteToLocalRect(const PhysicalRect& rect,
                                   MapCoordinatesFlags mode = 0) const {
    NOT_DESTROYED();
    return AncestorToLocalRect(nullptr, rect, mode);
  }
  gfx::QuadF AbsoluteToLocalQuad(const gfx::QuadF& quad,
                                 MapCoordinatesFlags mode = 0) const {
    NOT_DESTROYED();
    return AncestorToLocalQuad(nullptr, quad, mode);
  }
  PhysicalOffset AbsoluteToLocalPoint(const PhysicalOffset& p,
                                      MapCoordinatesFlags mode = 0) const {
    NOT_DESTROYED();
    return AncestorToLocalPoint(nullptr, p, mode);
  }
  gfx::PointF AbsoluteToLocalPoint(const gfx::PointF& p,
                                   MapCoordinatesFlags mode = 0) const {
    NOT_DESTROYED();
    return AncestorToLocalPoint(nullptr, p, mode);
  }

  // Return the offset from the container() layoutObject (excluding transforms
  // and multicol).
  PhysicalOffset OffsetFromContainer(const LayoutObject*,
                                     bool ignore_scroll_offset = false) const;
  // Return the offset from an object from the ancestor. The ancestor need
  // not be on the containing block chain of |this|. Note that this function
  // cannot be used when there are transforms between this object and the
  // ancestor - use |LocalToAncestorPoint| if there might be transforms.
  PhysicalOffset OffsetFromAncestor(const LayoutObject*) const;

  gfx::RectF AbsoluteBoundingBoxRectF(MapCoordinatesFlags = 0) const;
  // This returns an gfx::Rect enclosing this object. If this object has an
  // integral size and the position has fractional values, the resultant
  // gfx::Rect can be larger than the integral size.
  gfx::Rect AbsoluteBoundingBoxRect(MapCoordinatesFlags = 0) const;

  // These two functions also handle inlines without content for which the
  // location of the result rect (which may be empty) should be the absolute
  // location of the inline. This is especially useful to get the bounding
  // box of named anchors.
  // TODO(crbug.com/953479): After the bug is fixed, investigate whether we
  // can combine this with AbsoluteBoundingBoxRect().
  virtual PhysicalRect AbsoluteBoundingBoxRectHandlingEmptyInline(
      MapCoordinatesFlags flags = 0) const;
  // This returns an gfx::Rect expanded from
  // AbsoluteBoundingBoxRectHandlingEmptyInline by ScrollMargin.
  PhysicalRect AbsoluteBoundingBoxRectForScrollIntoView() const;

  // Build an array of quads in absolute coords for line boxes
  virtual void AbsoluteQuads(Vector<gfx::QuadF>&,
                             MapCoordinatesFlags mode = 0) const {
    NOT_DESTROYED();
  }

  // The bounding box (see: absoluteBoundingBoxRect) including all descendant
  // bounding boxes.
  gfx::Rect AbsoluteBoundingBoxRectIncludingDescendants() const;

  // For accessibility, we want the bounding box rect of this element
  // in local coordinates, which can then be converted to coordinates relative
  // to any ancestor using, e.g., localToAncestorTransform.
  virtual gfx::RectF LocalBoundingBoxRectForAccessibility() const = 0;

  // This function returns the:
  //  - Minimal logical width this object can have without overflowing. This
  //    means that all the opportunities for wrapping have been taken.
  //  - Maximal logical width.
  //
  // See INTRINSIC SIZES / PREFERRED LOGICAL WIDTHS above.
  //
  // CSS 2.1 calls this width the "preferred minimum width"/"preferred width"
  // (thus this name) and "minimum content width" (for table).
  // However CSS 3 calls it the "min/max-content inline size".
  // https://drafts.csswg.org/css-sizing-3/#min-content-inline-size
  // https://drafts.csswg.org/css-sizing-3/#max-content-inline-size
  // TODO(jchaffraix): We will probably want to rename it to match CSS 3.
  virtual MinMaxSizes PreferredLogicalWidths() const {
    NOT_DESTROYED();
    return MinMaxSizes();
  }

  const ComputedStyle* Style() const {
    NOT_DESTROYED();
    return style_.get();
  }

  // style_ can only be nullptr before the first style is set, thus most
  // callers will never see a nullptr style and should use StyleRef().
  const ComputedStyle& StyleRef() const {
    NOT_DESTROYED();
    DCHECK(style_);
    return *style_;
  }

  /* The following methods are inlined in LayoutObjectInlines.h */
  // If first line style is requested and there is no applicable first line
  // style, the functions will return the style of this object.
  inline const ComputedStyle* FirstLineStyle() const;
  inline const ComputedStyle& FirstLineStyleRef() const;
  inline const ComputedStyle* Style(bool first_line) const;
  inline const ComputedStyle& StyleRef(bool first_line) const;

  const ComputedStyle& EffectiveStyle(NGStyleVariant style_variant) const {
    NOT_DESTROYED();
    return style_variant == NGStyleVariant::kStandard
               ? StyleRef()
               : SlowEffectiveStyle(style_variant);
  }

  static inline Color ResolveColor(const ComputedStyle& style_to_use,
                                   const CSSProperty& color_property) {
    return style_to_use.VisitedDependentColor(color_property);
  }

  inline Color ResolveColor(const CSSProperty& color_property) const {
    NOT_DESTROYED();
    return StyleRef().VisitedDependentColor(color_property);
  }

  virtual CursorDirective GetCursor(const PhysicalOffset&, ui::Cursor&) const;

  // Returns the rect that should have raster invalidated whenever this object
  // changes. The rect is in the coordinate space of the document's scrolling
  // contents. This method deals with outlines and overflow.
  virtual PhysicalRect VisualRectInDocument(
      VisualRectFlags = kDefaultVisualRectFlags) const;

  // Returns the rect that should have raster invalidated whenever this object
  // changes. The rect is in the object's local physical coordinate space.
  // This is for non-SVG objects and LayoutSVGRoot only. SVG objects (except
  // LayoutSVGRoot) should use VisualRectInLocalSVGCoordinates() and map with
  // SVG transforms instead.
  PhysicalRect LocalVisualRect() const {
    NOT_DESTROYED();
    if (StyleRef().Visibility() != EVisibility::kVisible &&
        VisualRectRespectsVisibility())
      return PhysicalRect();
    return LocalVisualRectIgnoringVisibility();
  }

  // Given a rect in the object's physical coordinate space, mutates the rect
  // into one representing the size of its visual painted output as if
  // |ancestor| was the root of the page: the rect is modified by any
  // intervening clips, transforms and scrolls between |this| and |ancestor|
  // (not inclusive of |ancestor|), but not any above |ancestor|.
  // The output is in the physical, painted coordinate pixel space of
  // |ancestor|.
  // Overflow clipping, CSS clipping and scrolling is *not* applied for
  // |ancestor| itself if |ancestor| scrolls overflow.
  // The output rect is suitable for purposes such as paint invalidation.
  //
  // The ancestor can be nullptr which, if |this| is not the root view, will map
  // the rect to the main frame's space which includes the root view's scroll
  // and clip. This is even true if the main frame is remote.
  //
  // If VisualRectFlags has the kEdgeInclusive bit set, clipping operations will
  // use LayoutRect::InclusiveIntersect, and the return value of
  // InclusiveIntersect will be propagated to the return value of this method.
  // Otherwise, clipping operations will use LayoutRect::Intersect, and the
  // return value will be true only if the clipped rect has non-zero area.
  // See the documentation for LayoutRect::InclusiveIntersect for more
  // information.
  bool MapToVisualRectInAncestorSpace(
      const LayoutBoxModelObject* ancestor,
      PhysicalRect&,
      VisualRectFlags = kDefaultVisualRectFlags) const;

  // Do not call this method directly. Call mapToVisualRectInAncestorSpace
  // instead.
  virtual bool MapToVisualRectInAncestorSpaceInternal(
      const LayoutBoxModelObject* ancestor,
      TransformState&,
      VisualRectFlags = kDefaultVisualRectFlags) const;

  // Returns the nearest ancestor in the containing block chain that
  // HasLocalBorderBoxProperties. If AncestorSkipInfo* is non-null and the
  // ancestor was skipped, returns nullptr. If PropertyTreeState* is non-null,
  // it will be populated with paint property nodes suitable for mapping upward
  // from the coordinate system of the property container.
  const LayoutObject* GetPropertyContainer(
      AncestorSkipInfo*,
      PropertyTreeStateOrAlias* = nullptr) const;

  // Do a rect-based hit test with this object as the stop node.
  HitTestResult HitTestForOcclusion(const PhysicalRect&) const;
  HitTestResult HitTestForOcclusion() const {
    NOT_DESTROYED();
    return HitTestForOcclusion(VisualRectInDocument());
  }

  // Return the offset to the column in which the specified point (in
  // flow-thread coordinates) lives. This is used to convert a flow-thread point
  // to a point in the containing coordinate space.
  virtual LayoutSize ColumnOffset(const LayoutPoint&) const {
    NOT_DESTROYED();
    return LayoutSize();
  }

  virtual unsigned length() const {
    NOT_DESTROYED();
    return 1;
  }

  bool IsFloatingOrOutOfFlowPositioned() const {
    NOT_DESTROYED();
    return (IsFloating() || IsOutOfFlowPositioned());
  }

  // Outside list markers are in-flow but behave kind of out-of-flowish.
  // We include them here to prevent code like '<li> <ol></ol></li>' from
  // generating an anonymous block box for the whitespace between the marker
  // and the <ol>.
  bool AffectsWhitespaceSiblings() const {
    NOT_DESTROYED();
    return !IsFloatingOrOutOfFlowPositioned() &&
           !IsLayoutNGOutsideListMarker() && !IsOutsideListMarker();
  }

  // Not returning StyleRef().BoxReflect() because some objects ignore the
  // reflection style (e.g. LayoutInline, LayoutSVGBlock).
  bool HasReflection() const {
    NOT_DESTROYED();
    return bitfields_.HasReflection();
  }

  // The current selection state for an object.  For blocks, the state refers to
  // the state of the leaf descendants (as described above in the SelectionState
  // enum declaration).
  SelectionState GetSelectionState() const {
    NOT_DESTROYED();
    return bitfields_.GetSelectionState();
  }
  void SetSelectionState(SelectionState state) {
    NOT_DESTROYED();
    bitfields_.SetSelectionState(state);
  }
  bool CanUpdateSelectionOnRootLineBoxes() const;

  SelectionState GetSelectionStateForPaint() const {
    NOT_DESTROYED();
    return bitfields_.GetSelectionStateForPaint();
  }
  void SetSelectionStateForPaint(SelectionState state) {
    NOT_DESTROYED();
    bitfields_.SetSelectionStateForPaint(state);
  }

  // A single rectangle that encompasses all of the selected objects within this
  // object. Used to determine the tightest possible bounding box for the
  // selection. The rect is in the object's local physical coordinate space.
  virtual PhysicalRect LocalSelectionVisualRect() const {
    NOT_DESTROYED();
    return PhysicalRect();
  }

  PhysicalRect AbsoluteSelectionRect() const;

  bool CanBeSelectionLeaf() const;
  bool IsSelected() const;
  bool IsSelectable() const;

  /**
   * Returns the local coordinates of the caret within this layout object.
   * @param caretOffset zero-based offset determining position within the
   * layout object.
   * @param extraWidthToEndOfLine optional out arg to give extra width to end
   * of line -
   * useful for character range rect computations
   */
  virtual LayoutRect LocalCaretRect(
      const InlineBox*,
      int caret_offset,
      LayoutUnit* extra_width_to_end_of_line = nullptr) const;
  PhysicalRect PhysicalLocalCaretRect(
      const InlineBox* inline_box,
      int caret_offset,
      LayoutUnit* extra_width_to_end_of_line = nullptr) const {
    NOT_DESTROYED();
    return FlipForWritingMode(
        LocalCaretRect(inline_box, caret_offset, extra_width_to_end_of_line));
  }

  // When performing a global document tear-down, the layoutObject of the
  // document is cleared. We use this as a hook to detect the case of document
  // destruction and don't waste time doing unnecessary work.
  bool DocumentBeingDestroyed() const;

  void DestroyAndCleanupAnonymousWrappers(bool performing_reattach);

  void Destroy();

  // Virtual function helpers for the deprecated Flexible Box Layout (display:
  // -webkit-box).
  virtual bool IsDeprecatedFlexibleBox() const {
    NOT_DESTROYED();
    return false;
  }

  // Virtual function helper for the new FlexibleBox Layout (display:
  // -webkit-flex).
  virtual bool IsFlexibleBox() const {
    NOT_DESTROYED();
    return false;
  }

  virtual bool IsFlexibleBoxIncludingDeprecatedAndNG() const {
    NOT_DESTROYED();
    return false;
  }

  virtual bool IsFlexibleBoxIncludingNG() const {
    NOT_DESTROYED();
    return false;
  }

  bool IsListItemIncludingNG() const {
    NOT_DESTROYED();
    return IsListItem() || IsLayoutNGListItem();
  }

  // There 5 different types of list markers:
  // * LayoutListMarker (LayoutBox): for both outside and inside markers with
  //   'content: normal', in legacy layout.
  // * LayoutInsideListMarker (LayoutInline): for non-normal inside markers in
  //   legacy layout.
  // * LayoutOutsideListMarker (LayoutBlockFlow): for non-normal outside markers
  //   in legacy layout.
  // * LayoutNGInsideListMarker (LayoutInline): for inside markers in LayoutNG.
  // * LayoutNGOutsideListMarker (LayoutNGBlockFlowMixin<LayoutBlockFlow>):
  //   for outside markers in LayoutNG.

  // Legacy marker with inside position, normal or not.
  bool IsInsideListMarker() const;
  // Legacy marker with outside position, normal or not.
  bool IsOutsideListMarker() const;
  // Any kind of legacy list marker.
  bool IsListMarker() const {
    NOT_DESTROYED();
    return IsListMarkerForNormalContent() ||
           IsInsideListMarkerForCustomContent() ||
           IsOutsideListMarkerForCustomContent();
  }
  // Any kind of LayoutBox list marker.
  bool IsBoxListMarkerIncludingNG() const {
    NOT_DESTROYED();
    return IsListMarkerForNormalContent() ||
           IsOutsideListMarkerForCustomContent() ||
           IsLayoutNGOutsideListMarker();
  }
  // Any kind of LayoutNG list marker.
  bool IsLayoutNGListMarker() const {
    NOT_DESTROYED();
    return IsLayoutNGInsideListMarker() || IsLayoutNGOutsideListMarker();
  }
  // Any kind of list marker.
  bool IsListMarkerIncludingAll() const {
    NOT_DESTROYED();
    return IsListMarker() || IsLayoutNGListMarker();
  }

  virtual bool IsCombineText() const {
    NOT_DESTROYED();
    return false;
  }

  // ImageResourceObserver override.
  void ImageChanged(ImageResourceContent*, CanDeferInvalidation) final;
  void ImageChanged(WrappedImagePtr, CanDeferInvalidation) override {
    NOT_DESTROYED();
  }
  void ImageNotifyFinished(ImageResourceContent*) override;
  void NotifyImageFullyRemoved(ImageResourceContent*) override;
  bool WillRenderImage() final;
  bool GetImageAnimationPolicy(mojom::blink::ImageAnimationPolicy&) final;

  void Remove() {
    NOT_DESTROYED();
    if (Parent())
      Parent()->RemoveChild(this);
  }

  bool VisibleToHitTestRequest(const HitTestRequest& request) const {
    NOT_DESTROYED();
    return StyleRef().Visibility() == EVisibility::kVisible &&
           (request.IgnorePointerEventsNone() ||
            StyleRef().UsedPointerEvents() != EPointerEvents::kNone);
  }

  bool VisibleToHitTesting() const {
    NOT_DESTROYED();
    return StyleRef().VisibleToHitTesting();
  }

  // Map points and quads through elements, potentially via 3d transforms. You
  // should never need to call these directly; use localToAbsolute/
  // absoluteToLocal methods instead.
  virtual void MapLocalToAncestor(const LayoutBoxModelObject* ancestor,
                                  TransformState&,
                                  MapCoordinatesFlags) const;
  // If the LayoutBoxModelObject ancestor is non-null, the input quad is in the
  // space of the ancestor.
  // Otherwise:
  //   If TraverseDocumentBoundaries is specified, the input quad is in the
  //   space of the local root frame.
  //   Otherwise, the input quad is in the space of the containing frame.
  virtual void MapAncestorToLocal(const LayoutBoxModelObject*,
                                  TransformState&,
                                  MapCoordinatesFlags) const;

  bool ShouldUseTransformFromContainer(const LayoutObject* container) const;

  // The optional |size| parameter is used if the size of the object isn't
  // correct yet.
  void GetTransformFromContainer(const LayoutObject* container,
                                 const PhysicalOffset& offset_in_container,
                                 TransformationMatrix&,
                                 const PhysicalSize* size = nullptr) const;

  bool CreatesGroup() const {
    NOT_DESTROYED();
    // See |HasReflection()| for why |StyleRef().BoxReflect()| is not used.
    return StyleRef().HasGroupingProperty(HasReflection());
  }

  // Return the outline rectangles of the current fragmentainer, as indicated by
  // |iterator|. This method will also advance |iterator| to the next
  // FragmentData (and therefore also next fragmentainer), if any.
  Vector<PhysicalRect> CollectOutlineRectsAndAdvance(
      NGOutlineType,
      FragmentDataIterator& iterator) const;

  struct OutlineInfo {
    int width = 0;
    int offset = 0;

    // Convenience functions to initialize outline info.
    static OutlineInfo GetFromStyle(const ComputedStyle& style) {
      return {style.OutlineWidth().ToInt(), style.OutlineOffset().ToInt()};
    }

    // Unzoomed values modifies the style values by effective zoom. This is
    // used when the outline rects are specified in a space that does not
    // include EffectiveZoom, such as SVG.
    static OutlineInfo GetUnzoomedFromStyle(const ComputedStyle& style) {
      return {static_cast<int>(
                  std::floor(style.OutlineWidth() / style.EffectiveZoom())),
              static_cast<int>(
                  std::floor(style.OutlineOffset() / style.EffectiveZoom()))};
    }
  };

  // OutlineInfo, if specified, is filled in with the outline width and offset
  // in the same space as the physical rects returned.
  Vector<PhysicalRect> OutlineRects(OutlineInfo*,
                                    const PhysicalOffset& additional_offset,
                                    NGOutlineType) const;

  // Collects rectangles that the outline of this object would be drawing along
  // the outside of, even if the object isn't styled with a outline for now.
  // The rects also cover continuations. Note that the OutlineInfo, if
  // specified, is filled in in the same space as the rects.
  virtual void AddOutlineRects(Vector<PhysicalRect>&,
                               OutlineInfo*,
                               const PhysicalOffset& additional_offset,
                               NGOutlineType) const {
    NOT_DESTROYED();
  }

  // Only public for LayoutNG.
  void SetContainsInlineWithOutlineAndContinuation(bool b) {
    NOT_DESTROYED();
    bitfields_.SetContainsInlineWithOutlineAndContinuation(b);
  }

  static RespectImageOrientationEnum ShouldRespectImageOrientation(
      const LayoutObject*);

  bool IsRelayoutBoundary() const;

  void SetSelfNeedsLayoutForAvailableSpace(bool flag) {
    NOT_DESTROYED();
    bitfields_.SetSelfNeedsLayoutForAvailableSpace(flag);
    if (flag)
      MarkSelfPaintingLayerForVisualOverflowRecalc();
  }

  PaintInvalidationReason FullPaintInvalidationReason() const {
    NOT_DESTROYED();
    return full_paint_invalidation_reason_;
  }
  bool ShouldDoFullPaintInvalidation() const {
    NOT_DESTROYED();
    if (!ShouldDelayFullPaintInvalidation() &&
        full_paint_invalidation_reason_ != PaintInvalidationReason::kNone) {
      DCHECK(IsFullPaintInvalidationReason(full_paint_invalidation_reason_));
      DCHECK(ShouldCheckForPaintInvalidation());
      return true;
    }
    return false;
  }
  // Indicates that the paint of the object should be fully invalidated.
  // We will repaint the object, and reraster the area on the composited layer
  // where the object shows. Note that this function doesn't automatically
  // cause invalidation of background painted on the scrolling contents layer
  // because we don't want to invalidate the whole scrolling contents layer on
  // non-background changes. It's also not safe to specially handle
  // PaintInvalidationReason::kBackground in paint invalidator because we don't
  // track paint invalidation reasons separately. To indicate that the
  // background needs full invalidation, use
  // SetBackgroundNeedsFullPaintInvalidation().
  void SetShouldDoFullPaintInvalidation(
      PaintInvalidationReason = PaintInvalidationReason::kFull);
  void SetShouldDoFullPaintInvalidationWithoutGeometryChange(
      PaintInvalidationReason reason = PaintInvalidationReason::kFull) {
    NOT_DESTROYED();
    // Use SetBackgroundNeedsFullPaintInvalidation() instead. See comment above.
    DCHECK_NE(reason, PaintInvalidationReason::kBackground);
    SetShouldDoFullPaintInvalidationWithoutGeometryChangeInternal(reason);
  }

  void ClearPaintInvalidationFlags();

  bool ShouldCheckForPaintInvalidation() const {
    NOT_DESTROYED();
    return bitfields_.ShouldCheckForPaintInvalidation();
  }
  // Sets both ShouldCheckForPaintInvalidation() and
  // ShouldCheckGeometryForPaintInvalidation(). Though the setter and the getter
  // are asymmetric, this prevents callers from accidentally missing the
  // geometry checking flag.
  void SetShouldCheckForPaintInvalidation();
  // Sets ShouldCheckForPaintInvalidation() only. PaintInvalidator won't require
  // paint property tree update or other geometry related updates.
  void SetShouldCheckForPaintInvalidationWithoutGeometryChange();

  bool SubtreeShouldCheckForPaintInvalidation() const {
    NOT_DESTROYED();
    return bitfields_.SubtreeShouldCheckForPaintInvalidation();
  }
  void SetSubtreeShouldCheckForPaintInvalidation();

  bool ShouldCheckGeometryForPaintInvalidation() const {
    NOT_DESTROYED();
    return bitfields_.ShouldCheckGeometryForPaintInvalidation();
  }
  bool DescendantShouldCheckGeometryForPaintInvalidation() const {
    NOT_DESTROYED();
    return bitfields_.DescendantShouldCheckGeometryForPaintInvalidation();
  }

  bool MayNeedPaintInvalidationAnimatedBackgroundImage() const {
    NOT_DESTROYED();
    return bitfields_.MayNeedPaintInvalidationAnimatedBackgroundImage();
  }
  void SetMayNeedPaintInvalidationAnimatedBackgroundImage();

  void SetSubtreeShouldDoFullPaintInvalidation(
      PaintInvalidationReason reason = PaintInvalidationReason::kSubtree);
  bool SubtreeShouldDoFullPaintInvalidation() const {
    NOT_DESTROYED();
    DCHECK(!bitfields_.SubtreeShouldDoFullPaintInvalidation() ||
           ShouldDoFullPaintInvalidation());
    return bitfields_.SubtreeShouldDoFullPaintInvalidation();
  }

  // If true, it means that invalidation and repainting of the object can be
  // delayed until a future frame. This can be the case for an object whose
  // content is not visible to the user.
  bool ShouldDelayFullPaintInvalidation() const {
    NOT_DESTROYED();
    return bitfields_.ShouldDelayFullPaintInvalidation();
  }
  void SetShouldDelayFullPaintInvalidation();
  void ClearShouldDelayFullPaintInvalidation();

  bool ShouldInvalidateSelection() const {
    NOT_DESTROYED();
    return bitfields_.ShouldInvalidateSelection();
  }
  void SetShouldInvalidateSelection();

  virtual PhysicalRect ViewRect() const;

  // Called by PaintInvalidator during PrePaint. Checks paint invalidation flags
  // and other changes that will cause different painting, and invalidate
  // display item clients for painting if needed.
  virtual void InvalidatePaint(const PaintInvalidatorContext&) const;

  // When this object is invalidated for paint, this method is called to
  // invalidate any DisplayItemClients owned by this object, including the
  // object itself, LayoutText/LayoutInline line boxes, etc.,
  // not including children which will be invalidated normally during
  // invalidateTreeIfNeeded() and parts which are invalidated separately (e.g.
  // scrollbars). The caller should ensure the painting layer has been
  // setNeedsRepaint before calling this function.
  virtual void InvalidateDisplayItemClients(PaintInvalidationReason) const;

  // Get the dedicated DisplayItemClient for selection. Returns nullptr if this
  // object doesn't have a dedicated DisplayItemClient.
  virtual const DisplayItemClient* GetSelectionDisplayItemClient() const {
    NOT_DESTROYED();
    return nullptr;
  }

  // Called before setting style for existing/new anonymous child. Override to
  // set custom styles for the child. For new anonymous child, |child| is null.
  virtual void UpdateAnonymousChildStyle(const LayoutObject* child,
                                         ComputedStyle& style) const {
    NOT_DESTROYED();
  }

  // Returns a rect corresponding to this LayoutObject's bounds for use in
  // debugging output
  virtual PhysicalRect DebugRect() const;

  // Each LayoutObject has one or more painting fragments (exactly one
  // in the absence of multicol/pagination).
  // See ../paint/README.md for more on fragments.
  const FragmentData& FirstFragment() const {
    NOT_DESTROYED();
    return *fragment_;
  }

  enum OverflowRecalcType {
    kOnlyVisualOverflowRecalc,
    kLayoutAndVisualOverflowRecalc,
  };
  void SetNeedsOverflowRecalc(
      OverflowRecalcType = OverflowRecalcType::kLayoutAndVisualOverflowRecalc);

  // Call |SetShouldDoFullPaintInvalidation| for LayoutNG or
  // |SetShouldInvalidateSelection| on all selected children.
  void InvalidateSelectedChildrenOnStyleChange();

  // The allowed touch action is the union of the effective touch action
  // (from style) and blocking touch event handlers.
  TouchAction EffectiveAllowedTouchAction() const {
    NOT_DESTROYED();
    if (InsideBlockingTouchEventHandler())
      return TouchAction::kNone;
    return StyleRef().GetEffectiveTouchAction();
  }
  bool HasEffectiveAllowedTouchAction() const {
    NOT_DESTROYED();
    return EffectiveAllowedTouchAction() != TouchAction::kAuto;
  }

  // Whether this object's Node has a blocking touch event handler on itself
  // or an ancestor.
  bool InsideBlockingTouchEventHandler() const {
    NOT_DESTROYED();
    return bitfields_.InsideBlockingTouchEventHandler();
  }
  // Mark this object as having a |EffectiveAllowedTouchAction| changed, and
  // mark all ancestors as having a descendant that changed. This will cause a
  // PrePaint tree walk to update effective allowed touch action.
  void MarkEffectiveAllowedTouchActionChanged();
  void MarkDescendantEffectiveAllowedTouchActionChanged();
  bool EffectiveAllowedTouchActionChanged() const {
    NOT_DESTROYED();
    return bitfields_.EffectiveAllowedTouchActionChanged();
  }
  bool DescendantEffectiveAllowedTouchActionChanged() const {
    NOT_DESTROYED();
    return bitfields_.DescendantEffectiveAllowedTouchActionChanged();
  }
  void UpdateInsideBlockingTouchEventHandler(bool inside) {
    NOT_DESTROYED();
    bitfields_.SetInsideBlockingTouchEventHandler(inside);
  }

  // Whether this object's Node has a blocking wheel event handler on itself or
  // an ancestor.
  bool InsideBlockingWheelEventHandler() const {
    NOT_DESTROYED();
    return bitfields_.InsideBlockingWheelEventHandler();
  }
  // Mark this object as having a |InsideBlockingWheelEventHandler| changed, and
  // mark all ancestors as having a descendant that changed. This will cause a
  // PrePaint tree walk to update blocking wheel event handler state.
  void MarkBlockingWheelEventHandlerChanged();
  void MarkDescendantBlockingWheelEventHandlerChanged();
  bool BlockingWheelEventHandlerChanged() const {
    NOT_DESTROYED();
    return bitfields_.BlockingWheelEventHandlerChanged();
  }
  bool DescendantBlockingWheelEventHandlerChanged() const {
    NOT_DESTROYED();
    return bitfields_.DescendantBlockingWheelEventHandlerChanged();
  }
  void UpdateInsideBlockingWheelEventHandler(bool inside) {
    NOT_DESTROYED();
    bitfields_.SetInsideBlockingWheelEventHandler(inside);
  }

  // Painters can use const methods only, except for these explicitly declared
  // methods.
  class CORE_EXPORT MutableForPainting {
    STACK_ALLOCATED();

   public:
    // Convenience mutator that clears paint invalidation flags and this object
    // and its descendants' needs-paint-property-update flags.
    void ClearPaintFlags() { layout_object_.ClearPaintFlags(); }
    void SetShouldCheckForPaintInvalidation() {
      // This method is only intended to be called when visiting this object
      // during pre-paint, and as such it should only mark itself, and not the
      // entire containing block chain.
      DCHECK_EQ(layout_object_.GetDocument().Lifecycle().GetState(),
                DocumentLifecycle::kInPrePaint);
      layout_object_.bitfields_.SetShouldCheckGeometryForPaintInvalidation(
          true);
      layout_object_.bitfields_.SetShouldCheckForPaintInvalidation(true);
    }
    void SetShouldDoFullPaintInvalidation(PaintInvalidationReason reason) {
      layout_object_.SetShouldDoFullPaintInvalidation(reason);
    }
    void SetShouldDoFullPaintInvalidationWithoutGeometryChange(
        PaintInvalidationReason reason) {
      layout_object_
          .SetShouldDoFullPaintInvalidationWithoutGeometryChangeInternal(
              reason);
    }
    void SetBackgroundNeedsFullPaintInvalidation() {
      layout_object_.SetBackgroundNeedsFullPaintInvalidation();
    }
    void SetShouldDelayFullPaintInvalidation() {
      layout_object_.SetShouldDelayFullPaintInvalidation();
    }
    void EnsureIsReadyForPaintInvalidation() {
      layout_object_.EnsureIsReadyForPaintInvalidation();
    }
    void MarkEffectiveAllowedTouchActionChanged() {
      layout_object_.MarkEffectiveAllowedTouchActionChanged();
    }

    void SetBackgroundPaintLocation(BackgroundPaintLocation location) {
      layout_object_.SetBackgroundPaintLocation(location);
    }

    void UpdatePreviousVisibilityVisible() {
      layout_object_.bitfields_.SetPreviousVisibilityVisible(
          layout_object_.StyleRef().Visibility() == EVisibility::kVisible);
    }

    // Same as LayoutObject::SetNeedsPaintPropertyUpdate(), but does not mark
    // ancestors as having a descendant needing a paint property update.
    void SetOnlyThisNeedsPaintPropertyUpdate() {
      DCHECK(!layout_object_.GetDocument().InPostLifecycleSteps());
      layout_object_.bitfields_.SetNeedsPaintPropertyUpdate(true);
    }

    void AddSubtreePaintPropertyUpdateReason(
        SubtreePaintPropertyUpdateReason reason) {
      layout_object_.AddSubtreePaintPropertyUpdateReason(reason);
    }

    void UpdateInsideBlockingTouchEventHandler(bool inside) {
      layout_object_.UpdateInsideBlockingTouchEventHandler(inside);
    }

    void UpdateInsideBlockingWheelEventHandler(bool inside) {
      layout_object_.UpdateInsideBlockingWheelEventHandler(inside);
    }

    void InvalidateIntersectionObserverCachedRects() {
      layout_object_.InvalidateIntersectionObserverCachedRects();
    }

#if DCHECK_IS_ON()
    void ClearNeedsPaintPropertyUpdateForTesting() {
      layout_object_.bitfields_.SetNeedsPaintPropertyUpdate(false);
    }
#endif

    void SetShouldSkipNextLayoutShiftTracking(bool b) {
      layout_object_.SetShouldSkipNextLayoutShiftTracking(b);
    }

    void SetShouldAssumePaintOffsetTranslationForLayoutShiftTracking(bool b) {
      layout_object_
          .SetShouldAssumePaintOffsetTranslationForLayoutShiftTracking(b);
    }

    FragmentData& FirstFragment() { return *layout_object_.fragment_; }

    void EnsureId() { layout_object_.fragment_->EnsureId(); }

   protected:
    friend class LayoutBoxModelObject;
    friend class CustomScrollbar;
    friend class PaintInvalidator;
    friend class PaintPropertyTreeBuilder;
    friend class PrePaintTreeWalk;
    FRIEND_TEST_ALL_PREFIXES(AnimationCompositorAnimationsTest,
                             canStartElementOnCompositorTransformCAP);
    FRIEND_TEST_ALL_PREFIXES(AnimationCompositorAnimationsTest,
                             canStartElementOnCompositorEffectCAP);
    FRIEND_TEST_ALL_PREFIXES(PrePaintTreeWalkTest, ClipRects);
    FRIEND_TEST_ALL_PREFIXES(LayoutObjectTest, VisualRect);
    FRIEND_TEST_ALL_PREFIXES(BoxPaintInvalidatorTest,
                             ComputePaintInvalidationReasonBasic);

    friend class LayoutObject;
    MutableForPainting(const LayoutObject& layout_object)
        : layout_object_(const_cast<LayoutObject&>(layout_object)) {}

    LayoutObject& layout_object_;
  };
  MutableForPainting GetMutableForPainting() const {
    NOT_DESTROYED();
    return MutableForPainting(*this);
  }

  // Paint properties (see: |ObjectPaintProperties|) are built from an object's
  // state (location, transform, etc) as well as properties from ancestors.
  // When these inputs change, SetNeedsPaintPropertyUpdate will cause a property
  // tree update during the next document lifecycle update.
  //
  // In addition to tracking if an object needs its own paint properties
  // updated, SetNeedsPaintPropertyUpdate marks all ancestors as having a
  // descendant needing a paint property update too.
  void SetNeedsPaintPropertyUpdate();
  void SetNeedsPaintPropertyUpdatePreservingCachedRects();
  void SetDescendantNeedsPaintPropertyUpdate();
  bool NeedsPaintPropertyUpdate() const {
    NOT_DESTROYED();
    return bitfields_.NeedsPaintPropertyUpdate();
  }

  void AddSubtreePaintPropertyUpdateReason(
      SubtreePaintPropertyUpdateReason reason) {
    NOT_DESTROYED();
    bitfields_.AddSubtreePaintPropertyUpdateReason(reason);
    SetNeedsPaintPropertyUpdate();
  }
  unsigned SubtreePaintPropertyUpdateReasons() const {
    NOT_DESTROYED();
    return bitfields_.SubtreePaintPropertyUpdateReasons();
  }
  bool DescendantNeedsPaintPropertyUpdate() const {
    NOT_DESTROYED();
    return bitfields_.DescendantNeedsPaintPropertyUpdate();
  }
  // Called when some change needs paint property update of all ancestors (not
  // crossing frame boundaries).
  void ForceAllAncestorsNeedPaintPropertyUpdate();

  void SetIsScrollAnchorObject() {
    NOT_DESTROYED();
    bitfields_.SetIsScrollAnchorObject(true);
  }
  // Clears the IsScrollAnchorObject bit if and only if no ScrollAnchors still
  // reference this LayoutObject.
  void MaybeClearIsScrollAnchorObject();

  bool ScrollAnchorDisablingStyleChanged() {
    NOT_DESTROYED();
    return bitfields_.ScrollAnchorDisablingStyleChanged();
  }
  void SetScrollAnchorDisablingStyleChanged(bool changed) {
    NOT_DESTROYED();
    bitfields_.SetScrollAnchorDisablingStyleChanged(changed);
  }

  BackgroundPaintLocation GetBackgroundPaintLocation() const {
    NOT_DESTROYED();
    return bitfields_.GetBackgroundPaintLocation();
  }
  void SetBackgroundPaintLocation(BackgroundPaintLocation location) {
    NOT_DESTROYED();
    if (GetBackgroundPaintLocation() != location) {
      SetBackgroundNeedsFullPaintInvalidation();
      bitfields_.SetBackgroundPaintLocation(location);
    }
  }

  bool IsBackgroundAttachmentFixedObject() const {
    NOT_DESTROYED();
    return bitfields_.IsBackgroundAttachmentFixedObject();
  }

  bool BackgroundNeedsFullPaintInvalidation() const {
    NOT_DESTROYED();
    return !ShouldDelayFullPaintInvalidation() &&
           bitfields_.BackgroundNeedsFullPaintInvalidation();
  }
  void SetBackgroundNeedsFullPaintInvalidation() {
    NOT_DESTROYED();
    SetShouldDoFullPaintInvalidationWithoutGeometryChangeInternal(
        PaintInvalidationReason::kBackground);
    bitfields_.SetBackgroundNeedsFullPaintInvalidation(true);
  }

  bool ContainsInlineWithOutlineAndContinuation() const {
    NOT_DESTROYED();
    return bitfields_.ContainsInlineWithOutlineAndContinuation();
  }

  void SetOutlineMayBeAffectedByDescendants(bool b) {
    NOT_DESTROYED();
    bitfields_.SetOutlineMayBeAffectedByDescendants(b);
  }

  inline bool ChildLayoutBlockedByDisplayLock() const {
    NOT_DESTROYED();
    auto* context = GetDisplayLockContext();
    return context && !context->ShouldLayoutChildren();
  }

  bool ChildPrePaintBlockedByDisplayLock() const {
    NOT_DESTROYED();
    auto* context = GetDisplayLockContext();
    return context && !context->ShouldPrePaintChildren();
  }

  bool ChildPaintBlockedByDisplayLock() const {
    NOT_DESTROYED();
    auto* context = GetDisplayLockContext();
    return context && !context->ShouldPaintChildren();
  }

  bool IsShapingDeferred() const;

  // This flag caches StyleRef().HasBorderDecoration() &&
  // !Table()->ShouldCollapseBorders().
  bool HasNonCollapsedBorderDecoration() const {
    NOT_DESTROYED();
    DCHECK_GE(GetDocument().Lifecycle().GetState(),
              DocumentLifecycle::kInPerformLayout);
    return bitfields_.HasNonCollapsedBorderDecoration();
  }
  void SetHasNonCollapsedBorderDecoration(bool b) {
    NOT_DESTROYED();
    bitfields_.SetHasNonCollapsedBorderDecoration(b);
  }

  bool BeingDestroyed() const {
    NOT_DESTROYED();
    return bitfields_.BeingDestroyed();
  }

  bool IsTableColumnsConstraintsDirty() const {
    NOT_DESTROYED();
    return bitfields_.IsTableColumnsConstraintsDirty();
  }

  void SetTableColumnConstraintDirty(bool b) {
    NOT_DESTROYED();
    bitfields_.SetIsTableColumnsConstraintsDirty(b);
  }

  bool IsGridPlacementDirty() const {
    NOT_DESTROYED();
    return bitfields_.IsGridPlacementDirty();
  }

  void SetGridPlacementDirty(bool b) {
    NOT_DESTROYED();
    bitfields_.SetIsGridPlacementDirty(b);
  }

  DisplayLockContext* GetDisplayLockContext() const {
    NOT_DESTROYED();
    auto* element = DynamicTo<Element>(GetNode());
    if (!element)
      return nullptr;
    return element->GetDisplayLockContext();
  }

  void SetDocumentForAnonymous(Document* document) {
    NOT_DESTROYED();
    DCHECK(IsAnonymous());
    node_ = document;
  }

  bool IsLayoutNGObjectForListMarkerImage() const {
    NOT_DESTROYED();
    DCHECK(IsListMarkerImage());
    return bitfields_.IsLayoutNGObjectForListMarkerImage();
  }
  void SetIsLayoutNGObjectForListMarkerImage(bool b) {
    NOT_DESTROYED();
    DCHECK(IsListMarkerImage());
    bitfields_.SetIsLayoutNGObjectForListMarkerImage(b);
  }

  bool IsLayoutNGObjectForFormattedText() const {
    NOT_DESTROYED();
    return bitfields_.IsLayoutNGObjectForFormattedText();
  }
  void SetIsLayoutNGObjectForFormattedText(bool b) {
    NOT_DESTROYED();
    bitfields_.SetIsLayoutNGObjectForFormattedText(b);
  }

  bool PreviousVisibilityVisible() const {
    NOT_DESTROYED();
    return bitfields_.PreviousVisibilityVisible();
  }

  // See LocalVisualRect().
  virtual bool VisualRectRespectsVisibility() const {
    NOT_DESTROYED();
    return true;
  }

  bool TransformAffectsVectorEffect() const {
    NOT_DESTROYED();
    return bitfields_.TransformAffectsVectorEffect();
  }

  bool ShouldSkipNextLayoutShiftTracking() const {
    NOT_DESTROYED();
    return bitfields_.ShouldSkipNextLayoutShiftTracking();
  }
  void SetShouldSkipNextLayoutShiftTracking(bool b) {
    NOT_DESTROYED();
    bitfields_.SetShouldSkipNextLayoutShiftTracking(b);
  }

  bool ShouldAssumePaintOffsetTranslationForLayoutShiftTracking() const {
    NOT_DESTROYED();
    return bitfields_
        .ShouldAssumePaintOffsetTranslationForLayoutShiftTracking();
  }
  void SetShouldAssumePaintOffsetTranslationForLayoutShiftTracking(bool b) {
    NOT_DESTROYED();
    bitfields_.SetShouldAssumePaintOffsetTranslationForLayoutShiftTracking(b);
  }

  // Returns true if this layout object is created for an element which will be
  // changing behaviour for overflow: visible.
  // See
  // https://groups.google.com/a/chromium.org/g/blink-dev/c/MuTeW_AFgxA/m/IlT4QVEfAgAJ
  // for details.
  bool BelongsToElementChangingOverflowBehaviour() const;

 protected:
  // Identifiers for each of LayoutObject subclasses.
  // The identifier name for blink::LayoutFoo should be kLayoutObjectFoo.
  enum LayoutObjectType {
    kLayoutObjectBr,
    kLayoutObjectButton,
    kLayoutObjectCanvas,
    kLayoutObjectCounter,
    kLayoutObjectCustomScrollbarPart,
    kLayoutObjectEmbeddedObject,
    kLayoutObjectFieldset,
    kLayoutObjectFileUploadControl,
    kLayoutObjectFrame,
    kLayoutObjectFrameSet,
    kLayoutObjectGrid,
    kLayoutObjectIFrame,
    kLayoutObjectImage,
    kLayoutObjectInsideListMarker,
    kLayoutObjectListItem,
    kLayoutObjectListMarker,
    kLayoutObjectListMarkerImage,
    kLayoutObjectMathML,
    kLayoutObjectMathMLRoot,
    kLayoutObjectMedia,
    kLayoutObjectMultiColumnSet,
    kLayoutObjectMultiColumnSpannerPlaceholder,
    kLayoutObjectNGBlockFlow,
    kLayoutObjectNGButton,
    kLayoutObjectNGCustom,
    kLayoutObjectNGFieldset,
    kLayoutObjectNGFlexibleBox,
    kLayoutObjectNGFrameSet,
    kLayoutObjectNGGrid,
    kLayoutObjectNGInsideListMarker,
    kLayoutObjectNGListItem,
    kLayoutObjectNGOutsideListMarker,
    kLayoutObjectNGProgress,
    kLayoutObjectNGText,
    kLayoutObjectNGTextCombine,
    kLayoutObjectNGTextControlMultiLine,
    kLayoutObjectNGTextControlSingleLine,
    kLayoutObjectNGView,
    kLayoutObjectOutsideListMarker,
    kLayoutObjectProgress,
    kLayoutObjectQuote,
    kLayoutObjectReplaced,
    kLayoutObjectRuby,
    kLayoutObjectRubyBase,
    kLayoutObjectRubyRun,
    kLayoutObjectRubyText,
    kLayoutObjectTable,
    kLayoutObjectTableCaption,
    kLayoutObjectTableCell,
    kLayoutObjectTableCellLegacy,
    kLayoutObjectTableCol,
    kLayoutObjectTableRow,
    kLayoutObjectTableSection,
    kLayoutObjectTextControl,
    kLayoutObjectTextControlMultiLine,
    kLayoutObjectTextControlSingleLine,
    kLayoutObjectVideo,
    kLayoutObjectView,
    kLayoutObjectWidget,

    kLayoutObjectNGSVGForeignObject,
    kLayoutObjectNGSVGText,
    kLayoutObjectSVG, /* Keep by itself? */
    kLayoutObjectSVGContainer,
    kLayoutObjectSVGFilterPrimitive,
    kLayoutObjectSVGForeignObject,
    kLayoutObjectSVGHiddenContainer,
    kLayoutObjectSVGImage,
    kLayoutObjectSVGInline,
    kLayoutObjectSVGInlineText,
    kLayoutObjectSVGResourceContainer,
    kLayoutObjectSVGRoot,
    kLayoutObjectSVGShape,
    kLayoutObjectSVGText,
    kLayoutObjectSVGTextPath,
    kLayoutObjectSVGTransformableContainer,
    kLayoutObjectSVGTSpan,
    kLayoutObjectSVGViewportContainer,
  };
  virtual bool IsOfType(LayoutObjectType type) const {
    NOT_DESTROYED();
    return false;
  }

  void SetDestroyedForTesting() {
    NOT_DESTROYED();
    bitfields_.SetBeingDestroyed(true);
#if DCHECK_IS_ON()
    is_destroyed_ = true;
#endif
  }

  const ComputedStyle& SlowEffectiveStyle(NGStyleVariant style_variant) const;

  // Updates only the local style ptr of the object.  Does not update the state
  // of the object, and so only should be called when the style is known not to
  // have changed (or from SetStyle).
  void SetStyleInternal(scoped_refptr<const ComputedStyle> style) {
    NOT_DESTROYED();
    style_ = std::move(style);
  }
  // Overrides should call the superclass at the end. style_ will be 0 the
  // first time this function will be called.
  virtual void StyleWillChange(StyleDifference, const ComputedStyle& new_style);
  // Overrides should call the superclass at the start. |oldStyle| will be 0 the
  // first time this function is called.
  virtual void StyleDidChange(StyleDifference, const ComputedStyle* old_style);
  void PropagateStyleToAnonymousChildren();
  // Return true for objects that don't want style changes automatically
  // propagated via propagateStyleToAnonymousChildren(), but rather rely on
  // other custom mechanisms (if they need to be notified of parent style
  // changes at all).
  virtual bool AnonymousHasStylePropagationOverride() {
    NOT_DESTROYED();
    return false;
  }

  virtual void InLayoutNGInlineFormattingContextWillChange(bool) {
    NOT_DESTROYED();
  }

  // A fast path for MapToVisualRectInAncestorSpace for when GeometryMapper
  // can be used. |intersects| is set to whether the input rect intersected
  // (see documentation of return value of MapToVisualRectInAncestorSpace).
  //
  // The return value of this method is whether the fast path could be used.
  bool MapToVisualRectInAncestorSpaceInternalFastPath(
      const LayoutBoxModelObject* ancestor,
      PhysicalRect&,
      VisualRectFlags,
      bool& intersects) const;

  // This function is called before calling the destructor so that some clean-up
  // can happen regardless of whether they call a virtual function or not. As a
  // rule of thumb, this function should be preferred to the destructor. See
  // destroy() that is the one calling willBeDestroyed().
  //
  // There are 2 types of destructions: regular destructions and tree tear-down.
  // Regular destructions happen when the renderer is not needed anymore (e.g.
  // 'display' changed or the DOM Node was removed).
  // Tree tear-down is when the whole tree destroyed during navigation. It is
  // handled in the code by checking if documentBeingDestroyed() returns 'true'.
  // In this case, the code skips some unneeded expensive operations as we know
  // the tree is not reused (e.g. avoid clearing the containing block's line
  // box).
  virtual void WillBeDestroyed();

  virtual void InsertedIntoTree();
  virtual void WillBeRemovedFromTree();

#if DCHECK_IS_ON()
  virtual bool PaintInvalidationStateIsDirty() const;
#endif

  // Called before paint invalidation.
  virtual void EnsureIsReadyForPaintInvalidation();
  virtual void ClearPaintFlags();

  void SetIsBackgroundAttachmentFixedObject(bool);

  void SetEverHadLayout() {
    NOT_DESTROYED();
    bitfields_.SetEverHadLayout(true);
  }

  // See LocalVisualRect().
  virtual PhysicalRect LocalVisualRectIgnoringVisibility() const;

  virtual bool CanBeSelectionLeafInternal() const {
    NOT_DESTROYED();
    return false;
  }

  virtual PhysicalOffset OffsetFromContainerInternal(
      const LayoutObject*,
      bool ignore_scroll_offset) const;
  PhysicalOffset OffsetFromScrollableContainer(const LayoutObject*,
                                               bool ignore_scroll_offset) const;

  void NotifyDisplayLockDidLayoutChildren() {
    NOT_DESTROYED();
    if (auto* context = GetDisplayLockContext())
      context->DidLayoutChildren();
  }

  bool BackgroundIsKnownToBeObscured() const {
    NOT_DESTROYED();
    DCHECK_GE(GetDocument().Lifecycle().GetState(),
              DocumentLifecycle::kInPrePaint);
    return bitfields_.BackgroundIsKnownToBeObscured();
  }
  void SetBackgroundIsKnownToBeObscured(bool b) {
    NOT_DESTROYED();
    DCHECK_EQ(GetDocument().Lifecycle().GetState(),
              DocumentLifecycle::kInPrePaint);
    bitfields_.SetBackgroundIsKnownToBeObscured(b);
  }

  // Returns ContainerForAbsolutePosition() if it's a LayoutBlock, or the
  // containing LayoutBlock of it.
  LayoutBlock* ContainingBlockForAbsolutePosition(
      AncestorSkipInfo* = nullptr) const;
  // Returns ContainerForFixedPosition() if it's a LayoutBlock, or the
  // containing LayoutBlock of it.
  LayoutBlock* ContainingBlockForFixedPosition(
      AncestorSkipInfo* = nullptr) const;

  // Returns the first line style declared in CSS. The style may be declared on
  // an ancestor block (see LayoutBlock::FirstLineStyleParentBlock) that applies
  // to this object. Returns nullptr if there is no applicable first line style.
  // Whether the style applies is based on CSS rules, regardless of whether this
  // object is really in the first line which is unknown before layout.
  const ComputedStyle* FirstLineStyleWithoutFallback() const;

  void SetTransformAffectsVectorEffect(bool b) {
    NOT_DESTROYED();
    DCHECK(IsSVGChild());
    bitfields_.SetTransformAffectsVectorEffect(b);
  }

  void SetMightTraversePhysicalFragments(bool b) {
    NOT_DESTROYED();
    bitfields_.SetMightTraversePhysicalFragments(b);
  }

 private:
  gfx::QuadF LocalToAncestorQuadInternal(const gfx::QuadF&,
                                         const LayoutBoxModelObject* ancestor,
                                         MapCoordinatesFlags = 0) const;

  void ClearLayoutRootIfNeeded() const;

  void ScheduleRelayout();

  void AddAsImageObserver(StyleImage*);
  void RemoveAsImageObserver(StyleImage*);

  void UpdateImage(StyleImage*, StyleImage*);
  void UpdateShapeImage(const ShapeValue*, const ShapeValue*);
  void UpdateFillImages(const FillLayer* old_layers,
                        const FillLayer* new_layers);
  void UpdateCursorImages(const CursorList* old_cursors,
                          const CursorList* new_cursors);

  void CheckCounterChanges(const ComputedStyle* old_style,
                           const ComputedStyle* new_style);

  // Walk up the parent chain and find the first scrolling block to disable
  // scroll anchoring on.
  void SetScrollAnchorDisablingStyleChangedOnAncestor();

  bool SelfPaintingLayerNeedsVisualOverflowRecalc() const;
  inline void MarkContainerChainForOverflowRecalcIfNeeded(
      bool mark_container_chain_layout_overflow_recalc);

  inline void InvalidateContainerIntrinsicLogicalWidths();

  LayoutFlowThread* LocateFlowThreadContainingBlock() const;
  void RemoveFromLayoutFlowThreadRecursive(LayoutFlowThread*);

  const ComputedStyle* SlowStyleForContinuationOutline() const;

  StyleDifference AdjustStyleDifference(StyleDifference) const;

#if DCHECK_IS_ON()
  void CheckBlockPositionedObjectsNeedLayout();
#endif

  bool IsTextOrSVGChild() const {
    NOT_DESTROYED();
    return IsText() || IsSVGChild();
  }

  static bool IsAllowedToModifyLayoutTreeStructure(Document&);

  void UpdateImageObservers(const ComputedStyle* old_style,
                            const ComputedStyle* new_style);
  void UpdateFirstLineImageObservers(const ComputedStyle* new_style);

  void ApplyPseudoElementStyleChanges(const ComputedStyle* old_style);
  void ApplyFirstLineChanges(const ComputedStyle* old_style);

  virtual LayoutUnit FlipForWritingModeInternal(
      LayoutUnit position,
      LayoutUnit width,
      const LayoutBox* box_for_flipping) const;

  void MarkSelfPaintingLayerForVisualOverflowRecalc();

  void SetShouldDoFullPaintInvalidationWithoutGeometryChangeInternal(
      PaintInvalidationReason);

  // This is set by Set[Subtree]ShouldDoFullPaintInvalidation, and cleared
  // during PrePaint in this object's InvalidatePaint(). It's different from
  // DisplayItemClient::GetPaintInvalidationReason() which is set during
  // PrePaint and cleared in PaintController::FinishCycle().
  // It's defined as the first field so that it can use the memory gap between
  // DisplayItemClient and LayoutObject's other fields.
  PaintInvalidationReason full_paint_invalidation_reason_;

  // This boolean is used to know if this LayoutObject is a container for
  // absolute position descendants.
  // This is not in LayoutObjectBitfields to avoid to bump its size.
  // This is unit8_t in order to pack it together with PaintInvalidationReason.
  uint8_t can_contain_absolute_position_objects_ : 1;

#if DCHECK_IS_ON()
  unsigned has_ax_object_ : 1;
  unsigned set_needs_layout_forbidden_ : 1;
  unsigned as_image_observer_count_ : 20;
#endif

#define ADD_BOOLEAN_BITFIELD(field_name_, MethodNameBase)               \
 public:                                                                \
  bool MethodNameBase() const { return field_name_; }                   \
  void Set##MethodNameBase(bool new_value) { field_name_ = new_value; } \
                                                                        \
 private:                                                               \
  unsigned field_name_ : 1

  class LayoutObjectBitfields {
    DISALLOW_NEW();

    enum PositionedState {
      kIsStaticallyPositioned = 0,
      kIsRelativelyPositioned = 1,
      kIsOutOfFlowPositioned = 2,
      kIsStickyPositioned = 3,
    };

   public:
    // LayoutObjectBitfields holds all the boolean values for LayoutObject.
    //
    // This is done to promote better packing on LayoutObject (at the expense of
    // preventing bit field packing for the subclasses). Classes concerned about
    // packing and memory use should hoist their boolean to this class. See
    // below the field from sub-classes (e.g. childrenInline).
    //
    // Some of those booleans are caches of ComputedStyle values (e.g.
    // positionState). This enables better memory locality and thus better
    // performance.
    //
    // This class is an artifact of the WebKit era where LayoutObject wasn't
    // allowed to grow and each sub-class was strictly monitored for memory
    // increase. Our measurements indicate that the size of LayoutObject and
    // subsequent classes do not impact memory or speed in a significant
    // manner. This is based on growing LayoutObject in
    // https://codereview.chromium.org/44673003 and subsequent relaxations
    // of the memory constraints on layout objects.
    LayoutObjectBitfields(Node* node)
        : self_needs_layout_for_style_(false),
          self_needs_layout_for_available_space_(false),
          needs_positioned_movement_layout_(false),
          normal_child_needs_layout_(false),
          pos_child_needs_layout_(false),
          needs_simplified_normal_flow_layout_(false),
          self_needs_layout_overflow_recalc_(false),
          child_needs_layout_overflow_recalc_(false),
          intrinsic_logical_widths_dirty_(false),
          intrinsic_logical_widths_depends_on_block_constraints_(true),
          intrinsic_logical_widths_child_depends_on_block_constraints_(true),
          needs_collect_inlines_(false),
          maybe_has_percent_height_descendant_(false),
          should_check_for_paint_invalidation_(true),
          subtree_should_check_for_paint_invalidation_(false),
          should_delay_full_paint_invalidation_(false),
          subtree_should_do_full_paint_invalidation_(false),
          may_need_paint_invalidation_animated_background_image_(false),
          should_invalidate_selection_(false),
          should_check_geometry_for_paint_invalidation_(true),
          descendant_should_check_geometry_for_paint_invalidation_(true),
          needs_paint_property_update_(true),
          descendant_needs_paint_property_update_(true),
          floating_(false),
          is_anonymous_(!node),
          is_text_(false),
          is_box_(false),
          is_inline_(true),
          is_in_layout_ng_inline_formatting_context_(false),
          force_legacy_layout_(false),
          is_atomic_inline_level_(false),
          horizontal_writing_mode_(true),
          has_layer_(false),
          has_non_visible_overflow_(false),
          has_transform_related_property_(false),
          has_reflection_(false),
          can_contain_fixed_position_objects_(false),
          has_counter_node_map_(false),
          ever_had_layout_(false),
          ancestor_line_box_dirty_(false),
          is_inside_flow_thread_(false),
          subtree_change_listener_registered_(false),
          notified_of_subtree_change_(false),
          consumes_subtree_change_notification_(false),
          children_inline_(false),
          contains_inline_with_outline_and_continuation_(false),
          always_create_line_boxes_for_layout_inline_(false),
          background_is_known_to_be_obscured_(false),
          is_background_attachment_fixed_object_(false),
          is_scroll_anchor_object_(false),
          scroll_anchor_disabling_style_changed_(false),
          has_box_decoration_background_(false),
          background_needs_full_paint_invalidation_(true),
          outline_may_be_affected_by_descendants_(false),
          previous_outline_may_be_affected_by_descendants_(false),
          previous_visibility_visible_(false),
          is_truncated_(false),
          inside_blocking_touch_event_handler_(false),
          effective_allowed_touch_action_changed_(true),
          descendant_effective_allowed_touch_action_changed_(false),
          inside_blocking_wheel_event_handler_(false),
          blocking_wheel_event_handler_changed_(true),
          descendant_blocking_wheel_event_handler_changed_(false),
          is_effective_root_scroller_(false),
          is_global_root_scroller_(false),
          registered_as_first_line_image_observer_(false),
          is_html_legend_element_(false),
          has_non_collapsed_border_decoration_(false),
          being_destroyed_(false),
          is_layout_ng_object_for_list_marker_image_(false),
          is_table_column_constraints_dirty_(false),
          is_grid_placement_dirty_(true),
          transform_affects_vector_effect_(false),
          is_layout_ng_object_for_formatted_text(false),
          should_skip_next_layout_shift_tracking_(true),
          should_assume_paint_offset_translation_for_layout_shift_tracking_(
              false),
          might_traverse_physical_fragments_(false),
          whitespace_children_may_change_(false),
          needs_devtools_info_(false),
          positioned_state_(kIsStaticallyPositioned),
          selection_state_(static_cast<unsigned>(SelectionState::kNone)),
          selection_state_for_paint_(
              static_cast<unsigned>(SelectionState::kNone)),
          subtree_paint_property_update_reasons_(
              static_cast<unsigned>(SubtreePaintPropertyUpdateReason::kNone)),
          background_paint_location_(kBackgroundPaintInBorderBoxSpace),
          overflow_clip_axes_(kNoOverflowClip) {}

    // Self needs layout for style means that this layout object is marked for a
    // full layout. This is the default layout but it is expensive as it
    // recomputes everything. For CSS boxes, this includes the width (laying out
    // the line boxes again), the margins (due to block collapsing margins), the
    // positions, the height and the potential overflow.
    ADD_BOOLEAN_BITFIELD(self_needs_layout_for_style_, SelfNeedsLayoutForStyle);

    // Similar to SelfNeedsLayoutForStyle; however, this is set when the
    // available space (~parent height or width) changes, or the override size
    // has changed. In some cases this allows skipping layouts.
    ADD_BOOLEAN_BITFIELD(self_needs_layout_for_available_space_,
                         SelfNeedsLayoutForAvailableSpace);

    // A positioned movement layout is a specialized type of layout used on
    // positioned objects that only visually moved. This layout is used when
    // changing 'top'/'left' on a positioned element or margins on an
    // out-of-flow one. Because the following operations don't impact the size
    // of the object or sibling LayoutObjects, this layout is very lightweight.
    //
    // Positioned movement layout is implemented in
    // LayoutBlock::simplifiedLayout.
    ADD_BOOLEAN_BITFIELD(needs_positioned_movement_layout_,
                         NeedsPositionedMovementLayout);

    // This boolean is set when a normal flow ('position' == static || relative)
    // child requires layout (but this object doesn't). Due to the nature of
    // CSS, laying out a child can cause the parent to resize (e.g., if 'height'
    // is auto).
    ADD_BOOLEAN_BITFIELD(normal_child_needs_layout_, NormalChildNeedsLayout);

    // This boolean is set when an out-of-flow positioned ('position' == fixed
    // || absolute) child requires layout (but this object doesn't).
    ADD_BOOLEAN_BITFIELD(pos_child_needs_layout_, PosChildNeedsLayout);

    // Simplified normal flow layout only relayouts the normal flow children,
    // ignoring the out-of-flow descendants.
    //
    // The implementation of this layout is in
    // LayoutBlock::simplifiedNormalFlowLayout.
    ADD_BOOLEAN_BITFIELD(needs_simplified_normal_flow_layout_,
                         NeedsSimplifiedNormalFlowLayout);

    ADD_BOOLEAN_BITFIELD(self_needs_layout_overflow_recalc_,
                         SelfNeedsLayoutOverflowRecalc);

    ADD_BOOLEAN_BITFIELD(child_needs_layout_overflow_recalc_,
                         ChildNeedsLayoutOverflowRecalc);

    // This boolean marks the intrinsic logical widths for lazy recomputation.
    //
    // See INTRINSIC SIZES / PREFERRED LOGICAL WIDTHS above about those
    // widths.
    ADD_BOOLEAN_BITFIELD(intrinsic_logical_widths_dirty_,
                         IntrinsicLogicalWidthsDirty);

    // This boolean indicates if the cached intrinsic logical widths may depend
    // on the block constraints given by the parent.
    ADD_BOOLEAN_BITFIELD(intrinsic_logical_widths_depends_on_block_constraints_,
                         IntrinsicLogicalWidthsDependsOnBlockConstraints);

    // This boolean indicates if a *child* of this node may depend on the block
    // constraints given by the parent. Must always be true for legacy layout
    // roots.
    ADD_BOOLEAN_BITFIELD(
        intrinsic_logical_widths_child_depends_on_block_constraints_,
        IntrinsicLogicalWidthsChildDependsOnBlockConstraints);

    // This flag is set on inline container boxes that need to run the
    // Pre-layout phase in LayoutNG. See NGInlineNode::CollectInlines().
    // Also maybe set to inline boxes to optimize the propagation.
    ADD_BOOLEAN_BITFIELD(needs_collect_inlines_, NeedsCollectInlines);

    // This boolean tracks if a containing-block potentially has a percentage
    // height descentant within its subtree. A relayout may be required if a
    // %-block-size or definiteness changes. This flag is only set, and never
    // cleared.
    ADD_BOOLEAN_BITFIELD(maybe_has_percent_height_descendant_,
                         MaybeHasPercentHeightDescendant);

    // Paint related dirty bits.
    ADD_BOOLEAN_BITFIELD(should_check_for_paint_invalidation_,
                         ShouldCheckForPaintInvalidation);
    ADD_BOOLEAN_BITFIELD(subtree_should_check_for_paint_invalidation_,
                         SubtreeShouldCheckForPaintInvalidation);
    ADD_BOOLEAN_BITFIELD(should_delay_full_paint_invalidation_,
                         ShouldDelayFullPaintInvalidation);
    ADD_BOOLEAN_BITFIELD(subtree_should_do_full_paint_invalidation_,
                         SubtreeShouldDoFullPaintInvalidation);
    ADD_BOOLEAN_BITFIELD(may_need_paint_invalidation_animated_background_image_,
                         MayNeedPaintInvalidationAnimatedBackgroundImage);
    ADD_BOOLEAN_BITFIELD(should_invalidate_selection_,
                         ShouldInvalidateSelection);
    ADD_BOOLEAN_BITFIELD(should_check_geometry_for_paint_invalidation_,
                         ShouldCheckGeometryForPaintInvalidation);
    ADD_BOOLEAN_BITFIELD(
        descendant_should_check_geometry_for_paint_invalidation_,
        DescendantShouldCheckGeometryForPaintInvalidation);
    // Whether the paint properties need to be updated. For more details, see
    // LayoutObject::NeedsPaintPropertyUpdate().
    ADD_BOOLEAN_BITFIELD(needs_paint_property_update_,
                         NeedsPaintPropertyUpdate);
    // Whether the paint properties of a descendant need to be updated. For more
    // details, see LayoutObject::DescendantNeedsPaintPropertyUpdate().
    ADD_BOOLEAN_BITFIELD(descendant_needs_paint_property_update_,
                         DescendantNeedsPaintPropertyUpdate);
    // End paint related dirty bits.

    // This boolean is the cached value of 'float'
    // (see ComputedStyle::isFloating).
    ADD_BOOLEAN_BITFIELD(floating_, Floating);

    ADD_BOOLEAN_BITFIELD(is_anonymous_, IsAnonymous);
    ADD_BOOLEAN_BITFIELD(is_text_, IsText);
    ADD_BOOLEAN_BITFIELD(is_box_, IsBox);

    // This boolean represents whether the LayoutObject is 'inline-level'
    // (a CSS concept). Inline-level boxes are laid out inside a line. If
    // unset, the box is 'block-level' and thus stack on top of its
    // siblings (think of paragraphs).
    ADD_BOOLEAN_BITFIELD(is_inline_, IsInline);

    // This boolean is set when this LayoutObject is in LayoutNG inline
    // formatting context. Note, this LayoutObject itself may be laid out by
    // legacy.
    ADD_BOOLEAN_BITFIELD(is_in_layout_ng_inline_formatting_context_,
                         IsInLayoutNGInlineFormattingContext);

    // Set if we're to force legacy layout (i.e. disable LayoutNG) on this
    // object, and all descendants.
    ADD_BOOLEAN_BITFIELD(force_legacy_layout_, ForceLegacyLayout);

    // This boolean is set if the element is an atomic inline-level box.
    //
    // In CSS, atomic inline-level boxes are laid out on a line but they
    // are opaque from the perspective of line layout. This means that they
    // can't be split across lines like normal inline boxes (LayoutInline).
    // Examples of atomic inline-level elements: inline tables, inline
    // blocks and replaced inline elements.
    // See http://www.w3.org/TR/CSS2/visuren.html#inline-boxes.
    //
    // Our code is confused about the use of this boolean and confuses it
    // with being replaced (see LayoutReplaced about this).
    // TODO(jchaffraix): We should inspect callers and clarify their use.
    // TODO(jchaffraix): We set this boolean for replaced elements that are
    // not inline but shouldn't (crbug.com/567964). This should be enforced.
    ADD_BOOLEAN_BITFIELD(is_atomic_inline_level_, IsAtomicInlineLevel);
    ADD_BOOLEAN_BITFIELD(horizontal_writing_mode_, HorizontalWritingMode);

    ADD_BOOLEAN_BITFIELD(has_layer_, HasLayer);

    // This boolean is set if overflow != 'visible'.
    // This means that this object may need an overflow clip to be applied
    // at paint time to its visual overflow (see OverflowModel for more
    // details). Only set for LayoutBoxes and descendants.
    ADD_BOOLEAN_BITFIELD(has_non_visible_overflow_, HasNonVisibleOverflow);

    // The cached value from ComputedStyle::HasTransformRelatedProperty for
    // objects that do not ignore transform-related styles (e.g. not
    // LayoutInline).
    ADD_BOOLEAN_BITFIELD(has_transform_related_property_,
                         HasTransformRelatedProperty);
    ADD_BOOLEAN_BITFIELD(has_reflection_, HasReflection);

    // This boolean is used to know if this LayoutObject is a container for
    // fixed position descendants.
    ADD_BOOLEAN_BITFIELD(can_contain_fixed_position_objects_,
                         CanContainFixedPositionObjects);

    // This boolean is used to know if this LayoutObject has one (or more)
    // associated CounterNode(s).
    // See class comment in LayoutCounter.h for more detail.
    ADD_BOOLEAN_BITFIELD(has_counter_node_map_, HasCounterNodeMap);

    ADD_BOOLEAN_BITFIELD(ever_had_layout_, EverHadLayout);
    ADD_BOOLEAN_BITFIELD(ancestor_line_box_dirty_, AncestorLineBoxDirty);

    ADD_BOOLEAN_BITFIELD(is_inside_flow_thread_, IsInsideFlowThread);

    ADD_BOOLEAN_BITFIELD(subtree_change_listener_registered_,
                         SubtreeChangeListenerRegistered);
    ADD_BOOLEAN_BITFIELD(notified_of_subtree_change_, NotifiedOfSubtreeChange);
    ADD_BOOLEAN_BITFIELD(consumes_subtree_change_notification_,
                         ConsumesSubtreeChangeNotification);

    // from LayoutBlock
    ADD_BOOLEAN_BITFIELD(children_inline_, ChildrenInline);

    // from LayoutBlockFlow
    ADD_BOOLEAN_BITFIELD(contains_inline_with_outline_and_continuation_,
                         ContainsInlineWithOutlineAndContinuation);

    // from LayoutInline
    ADD_BOOLEAN_BITFIELD(always_create_line_boxes_for_layout_inline_,
                         AlwaysCreateLineBoxesForLayoutInline);

    // For LayoutBox to cache the result of LayoutBox::
    // ComputeBackgroundIsKnownToBeObscured(). It's updated during PrePaint.
    ADD_BOOLEAN_BITFIELD(background_is_known_to_be_obscured_,
                         BackgroundIsKnownToBeObscured);

    ADD_BOOLEAN_BITFIELD(is_background_attachment_fixed_object_,
                         IsBackgroundAttachmentFixedObject);
    ADD_BOOLEAN_BITFIELD(is_scroll_anchor_object_, IsScrollAnchorObject);

    // Whether changes in this LayoutObject's CSS properties since the last
    // layout should suppress any adjustments that would be made during the next
    // layout by ScrollAnchor objects for which this LayoutObject is on the path
    // from the anchor node to the scroller.
    // See http://bit.ly/sanaclap for more info.
    ADD_BOOLEAN_BITFIELD(scroll_anchor_disabling_style_changed_,
                         ScrollAnchorDisablingStyleChanged);

    ADD_BOOLEAN_BITFIELD(has_box_decoration_background_,
                         HasBoxDecorationBackground);

    ADD_BOOLEAN_BITFIELD(background_needs_full_paint_invalidation_,
                         BackgroundNeedsFullPaintInvalidation);

    // Whether shape of outline may be affected by any descendants. This is
    // updated before paint invalidation, checked during paint invalidation.
    ADD_BOOLEAN_BITFIELD(outline_may_be_affected_by_descendants_,
                         OutlineMayBeAffectedByDescendants);
    // The outlineMayBeAffectedByDescendants status of the last paint
    // invalidation.
    ADD_BOOLEAN_BITFIELD(previous_outline_may_be_affected_by_descendants_,
                         PreviousOutlineMayBeAffectedByDescendants);
    // CSS visibility : visible status of the last paint invalidation.
    ADD_BOOLEAN_BITFIELD(previous_visibility_visible_,
                         PreviousVisibilityVisible);

    ADD_BOOLEAN_BITFIELD(is_truncated_, IsTruncated);

    // Whether this object's Node has a blocking touch event handler on itself
    // or an ancestor. This is updated during the PrePaint phase.
    ADD_BOOLEAN_BITFIELD(inside_blocking_touch_event_handler_,
                         InsideBlockingTouchEventHandler);

    // Set when |EffectiveAllowedTouchAction| changes (i.e., blocking touch
    // event handlers change or effective touch action style changes). This only
    // needs to be set on the object that changes as the PrePaint walk will
    // ensure descendants are updated.
    ADD_BOOLEAN_BITFIELD(effective_allowed_touch_action_changed_,
                         EffectiveAllowedTouchActionChanged);

    // Set when a descendant's |EffectiveAllowedTouchAction| changes. This
    // is used to ensure the PrePaint tree walk processes objects with
    // |effective_allowed_touch_action_changed_|.
    ADD_BOOLEAN_BITFIELD(descendant_effective_allowed_touch_action_changed_,
                         DescendantEffectiveAllowedTouchActionChanged);

    // Whether this object's Node has a blocking wheel event handler on itself
    // or an ancestor. This is updated during the PrePaint phase.
    ADD_BOOLEAN_BITFIELD(inside_blocking_wheel_event_handler_,
                         InsideBlockingWheelEventHandler);

    // Set when |InsideBlockingWheelEventHandler| changes (i.e., blocking wheel
    // event handlers change). This only needs to be set on the object that
    // changes as the PrePaint walk will ensure descendants are updated.
    ADD_BOOLEAN_BITFIELD(blocking_wheel_event_handler_changed_,
                         BlockingWheelEventHandlerChanged);

    // Set when a descendant's |InsideBlockingWheelEventHandler| changes. This
    // is used to ensure the PrePaint tree walk processes objects with
    // |blocking_wheel_event_handler_changed_|.
    ADD_BOOLEAN_BITFIELD(descendant_blocking_wheel_event_handler_changed_,
                         DescendantBlockingWheelEventHandlerChanged);

    // See page/scrolling/README.md for an explanation of root scroller and how
    // it works.
    ADD_BOOLEAN_BITFIELD(is_effective_root_scroller_, IsEffectiveRootScroller);
    ADD_BOOLEAN_BITFIELD(is_global_root_scroller_, IsGlobalRootScroller);

    // Indicates whether this object has been added as a first line image
    // observer.
    ADD_BOOLEAN_BITFIELD(registered_as_first_line_image_observer_,
                         RegisteredAsFirstLineImageObserver);

    // Whether this object's |Node| is a HTMLLegendElement. Used to increase
    // performance of |IsRenderedLegend| which is performance sensitive.
    ADD_BOOLEAN_BITFIELD(is_html_legend_element_, IsHTMLLegendElement);

    // Caches StyleRef().HasBorderDecoration() &&
    // !Table()->ShouldCollapseBorders().
    ADD_BOOLEAN_BITFIELD(has_non_collapsed_border_decoration_,
                         HasNonCollapsedBorderDecoration);

    // True at start of |Destroy()| before calling |WillBeDestroyed()|.
    ADD_BOOLEAN_BITFIELD(being_destroyed_, BeingDestroyed);

    // From LayoutListMarkerImage
    ADD_BOOLEAN_BITFIELD(is_layout_ng_object_for_list_marker_image_,
                         IsLayoutNGObjectForListMarkerImage);

    // Column constraints are cached on LayoutNGTable.
    // When this flag is set, any cached constraints are invalid.
    ADD_BOOLEAN_BITFIELD(is_table_column_constraints_dirty_,
                         IsTableColumnsConstraintsDirty);

    // Grid item placement is cached on LayoutNGGrid.
    // When this flag is set, any cached item placements are invalid.
    ADD_BOOLEAN_BITFIELD(is_grid_placement_dirty_, IsGridPlacementDirty);

    // For transformable SVG child objects, indicates if this object or any
    // descendant has special vector effect that is affected by transform on
    // this object. For an SVG child object having special vector effect, this
    // flag is set on all transformable ancestors up to the SVG root (not
    // included).
    ADD_BOOLEAN_BITFIELD(transform_affects_vector_effect_,
                         TransformAffectsVectorEffect);
    ADD_BOOLEAN_BITFIELD(is_layout_ng_object_for_formatted_text,
                         IsLayoutNGObjectForFormattedText);

    // Whether to skip layout shift tracking in the next paint invalidation.
    // See PaintInvalidator::UpdateLayoutShiftTracking().
    ADD_BOOLEAN_BITFIELD(should_skip_next_layout_shift_tracking_,
                         ShouldSkipNextLayoutShiftTracking);

    // Whether, on the next time PaintPropertyTreeBuilder builds for this
    // object, it should be assumed it had the same paint offset transform last
    // time as it has this time. This is used when layout reattach loses the
    // information from the previous frame; this bit stores that information
    // to inform the next frame for layout shift tracking.
    ADD_BOOLEAN_BITFIELD(
        should_assume_paint_offset_translation_for_layout_shift_tracking_,
        ShouldAssumePaintOffsetTranslationForLayoutShiftTracking);

    // True if there's a possibility that we can walk NG fragment children of
    // this object. False if we definitely need to walk the LayoutObject tree.
    ADD_BOOLEAN_BITFIELD(might_traverse_physical_fragments_,
                         MightTraversePhysicalFragments);

    // True if children that may affect whitespace have been removed. If true
    // during style recalc, mark ancestors for layout tree rebuild to cause a
    // re-evaluation of whitespace children.
    ADD_BOOLEAN_BITFIELD(whitespace_children_may_change_,
                         WhitespaceChildrenMayChange);

    ADD_BOOLEAN_BITFIELD(needs_devtools_info_, NeedsDevtoolsInfo);

   private:
    // This is the cached 'position' value of this object
    // (see ComputedStyle::position).
    unsigned positioned_state_ : 2;  // PositionedState

    // `selection_state_` is direct mapping of the DOM selection into the
    // respective LayoutObjects that `CanBeSelectionLeaf()`.
    // `selection_state_for_paint_` is adjusted so that the state takes into
    // account whether such a LayoutObject will be painted. If selection
    // starts/ends in an object that is not painted, we won't be able to record
    // the bounds for composited selection state that is pushed to cc.
    unsigned selection_state_ : 3;   // SelectionState
    unsigned selection_state_for_paint_ : 3;  // SelectionState

    // Reasons for the full subtree invalidation.
    unsigned subtree_paint_property_update_reasons_
        : kSubtreePaintPropertyUpdateReasonsBitfieldWidth;

    // For LayoutBox. It's updated during PrePaint.
    unsigned background_paint_location_ : 2;  // BackgroundPaintLocation.

    unsigned overflow_clip_axes_ : 2;

   public:
    bool IsOutOfFlowPositioned() const {
      return positioned_state_ == kIsOutOfFlowPositioned;
    }
    bool IsRelPositioned() const {
      return positioned_state_ == kIsRelativelyPositioned;
    }
    bool IsStickyPositioned() const {
      return positioned_state_ == kIsStickyPositioned;
    }
    bool IsInFlowPositioned() const {
      return positioned_state_ == kIsRelativelyPositioned ||
             positioned_state_ == kIsStickyPositioned;
    }
    bool IsPositioned() const {
      return positioned_state_ != kIsStaticallyPositioned;
    }

    void SetPositionedState(EPosition position_state) {
      // This maps FixedPosition and AbsolutePosition to
      // IsOutOfFlowPositioned, saving one bit.
      switch (position_state) {
        case EPosition::kStatic:
          positioned_state_ = kIsStaticallyPositioned;
          break;
        case EPosition::kRelative:
          positioned_state_ = kIsRelativelyPositioned;
          break;
        case EPosition::kAbsolute:
        case EPosition::kFixed:
          positioned_state_ = kIsOutOfFlowPositioned;
          break;
        case EPosition::kSticky:
          positioned_state_ = kIsStickyPositioned;
          break;
        default:
          NOTREACHED();
          break;
      }
    }
    void ClearPositionedState() { positioned_state_ = kIsStaticallyPositioned; }

    ALWAYS_INLINE SelectionState GetSelectionState() const {
      return static_cast<SelectionState>(selection_state_);
    }
    ALWAYS_INLINE void SetSelectionState(SelectionState selection_state) {
      selection_state_ = static_cast<unsigned>(selection_state);
    }

    ALWAYS_INLINE SelectionState GetSelectionStateForPaint() const {
      return static_cast<SelectionState>(selection_state_for_paint_);
    }
    ALWAYS_INLINE void SetSelectionStateForPaint(
        SelectionState selection_state) {
      selection_state_for_paint_ = static_cast<unsigned>(selection_state);
    }

    ALWAYS_INLINE unsigned SubtreePaintPropertyUpdateReasons() const {
      return subtree_paint_property_update_reasons_;
    }
    ALWAYS_INLINE void AddSubtreePaintPropertyUpdateReason(
        SubtreePaintPropertyUpdateReason reason) {
      DCHECK_LE(static_cast<unsigned>(reason),
                1u << (kSubtreePaintPropertyUpdateReasonsBitfieldWidth - 1));
      subtree_paint_property_update_reasons_ |= static_cast<unsigned>(reason);
    }
    ALWAYS_INLINE void ResetSubtreePaintPropertyUpdateReasons() {
      subtree_paint_property_update_reasons_ =
          static_cast<unsigned>(SubtreePaintPropertyUpdateReason::kNone);
    }

    ALWAYS_INLINE BackgroundPaintLocation GetBackgroundPaintLocation() const {
      return static_cast<BackgroundPaintLocation>(background_paint_location_);
    }
    ALWAYS_INLINE void SetBackgroundPaintLocation(
        BackgroundPaintLocation location) {
      background_paint_location_ = static_cast<unsigned>(location);
      DCHECK_EQ(location, GetBackgroundPaintLocation());
    }

    ALWAYS_INLINE OverflowClipAxes GetOverflowClipAxes() const {
      return static_cast<OverflowClipAxes>(overflow_clip_axes_);
    }
    ALWAYS_INLINE void SetOverflowClipAxes(OverflowClipAxes axes) {
      overflow_clip_axes_ = axes;
    }
  };

#undef ADD_BOOLEAN_BITFIELD

  LayoutObjectBitfields bitfields_;

  void SetSelfNeedsLayoutForStyle(bool b) {
    NOT_DESTROYED();
    bitfields_.SetSelfNeedsLayoutForStyle(b);
  }
  void SetNeedsPositionedMovementLayout(bool b) {
    NOT_DESTROYED();
    DCHECK(!GetDocument().InPostLifecycleSteps());
    bitfields_.SetNeedsPositionedMovementLayout(b);
  }
  void SetNormalChildNeedsLayout(bool b) {
    NOT_DESTROYED();
    DCHECK(!GetDocument().InPostLifecycleSteps());
    bitfields_.SetNormalChildNeedsLayout(b);
    if (b)
      bitfields_.SetIsTableColumnsConstraintsDirty(true);
  }
  void SetPosChildNeedsLayout(bool b) {
    NOT_DESTROYED();
    DCHECK(!GetDocument().InPostLifecycleSteps());
    bitfields_.SetPosChildNeedsLayout(b);
  }
  void SetNeedsSimplifiedNormalFlowLayout(bool b) {
    NOT_DESTROYED();
    DCHECK(!GetDocument().InPostLifecycleSteps());
    bitfields_.SetNeedsSimplifiedNormalFlowLayout(b);
  }

 private:
  friend class LineLayoutItem;
  friend class LocalFrameView;
  friend class SubtreeLayoutScope;

  scoped_refptr<const ComputedStyle> style_;

  Member<Node> node_;

  Member<LayoutObject> parent_;
  Member<LayoutObject> previous_;
  Member<LayoutObject> next_;
  Member<FragmentData> fragment_;

  // Store state between styleWillChange and styleDidChange
  static bool affects_parent_block_;

#if DCHECK_IS_ON()
  bool is_destroyed_ = false;
#endif
};

// Allow equality comparisons of LayoutObjects by reference or pointer,
// interchangeably.
DEFINE_COMPARISON_OPERATORS_WITH_REFERENCES(LayoutObject)

inline bool LayoutObject::DocumentBeingDestroyed() const {
  return GetDocument().Lifecycle().GetState() >= DocumentLifecycle::kStopping;
}

inline bool LayoutObject::IsBeforeContent() const {
  if (StyleRef().StyleType() != kPseudoIdBefore)
    return false;
  // Text nodes don't have their own styles, so ignore the style on a text node.
  if (IsText() && !IsBR())
    return false;
  return true;
}

inline bool LayoutObject::IsAfterContent() const {
  if (StyleRef().StyleType() != kPseudoIdAfter)
    return false;
  // Text nodes don't have their own styles, so ignore the style on a text node.
  if (IsText() && !IsBR())
    return false;
  return true;
}

inline bool LayoutObject::IsMarkerContent() const {
  if (StyleRef().StyleType() != kPseudoIdMarker)
    return false;
  // Text nodes don't have their own styles, so ignore the style on a text node.
  if (IsText() && !IsBR())
    return false;
  return true;
}

inline bool LayoutObject::IsBeforeOrAfterContent() const {
  return IsBeforeContent() || IsAfterContent();
}

// setNeedsLayout() won't cause full paint invalidations as
// setNeedsLayoutAndFullPaintInvalidation() does. Otherwise the two methods are
// identical.
inline void LayoutObject::SetNeedsLayout(
    LayoutInvalidationReasonForTracing reason,
    MarkingBehavior mark_parents,
    SubtreeLayoutScope* layouter) {
#if DCHECK_IS_ON()
  DCHECK(!IsSetNeedsLayoutForbidden());
#endif
  bool already_needed_layout = bitfields_.SelfNeedsLayoutForStyle() ||
                               bitfields_.SelfNeedsLayoutForAvailableSpace();
  SetSelfNeedsLayoutForStyle(true);
  SetNeedsOverflowRecalc();
  SetTableColumnConstraintDirty(true);
  if (!already_needed_layout) {
    DEVTOOLS_TIMELINE_TRACE_EVENT_INSTANT_WITH_CATEGORIES(
        TRACE_DISABLED_BY_DEFAULT("devtools.timeline.invalidationTracking"),
        "LayoutInvalidationTracking",
        inspector_layout_invalidation_tracking_event::Data, this, reason);
    if (mark_parents == kMarkContainerChain &&
        (!layouter || layouter->Root() != this))
      MarkContainerChainForLayout(!layouter, layouter);
  }
}

inline void LayoutObject::SetNeedsLayoutAndFullPaintInvalidation(
    LayoutInvalidationReasonForTracing reason,
    MarkingBehavior mark_parents,
    SubtreeLayoutScope* layouter) {
  SetNeedsLayout(reason, mark_parents, layouter);
  SetShouldDoFullPaintInvalidation();
}

inline void LayoutObject::ClearNeedsLayoutWithoutPaintInvalidation() {
  // Set flags for later stages/cycles.
  SetEverHadLayout();

  // Clear needsLayout flags.
  SetSelfNeedsLayoutForStyle(false);
  SetSelfNeedsLayoutForAvailableSpace(false);
  SetNeedsPositionedMovementLayout(false);
  SetAncestorLineBoxDirty(false);

  if (!ChildLayoutBlockedByDisplayLock()) {
    SetPosChildNeedsLayout(false);
    SetNormalChildNeedsLayout(false);
    SetNeedsSimplifiedNormalFlowLayout(false);
  } else if (!PosChildNeedsLayout() && !NormalChildNeedsLayout() &&
             !NeedsSimplifiedNormalFlowLayout()) {
    // We aren't clearing the child dirty bits because the node is locked and
    // layout for children is not done. If the children aren't dirty,  we need
    // to notify the display lock that child traversal was blocked so that when
    // the subtree gets updated/unlocked we will traverse the children.
    auto* context = GetDisplayLockContext();
    DCHECK(context);
    context->NotifyChildLayoutWasBlocked();
  }

#if DCHECK_IS_ON()
  CheckBlockPositionedObjectsNeedLayout();
#endif

  SetScrollAnchorDisablingStyleChanged(false);
}

inline void LayoutObject::ClearNeedsLayout() {
  ClearNeedsLayoutWithoutPaintInvalidation();
  SetShouldCheckForPaintInvalidation();
}

inline void LayoutObject::ClearNeedsLayoutWithFullPaintInvalidation() {
  ClearNeedsLayoutWithoutPaintInvalidation();
  SetShouldDoFullPaintInvalidation();
}

inline void LayoutObject::SetChildNeedsLayout(MarkingBehavior mark_parents,
                                              SubtreeLayoutScope* layouter) {
#if DCHECK_IS_ON()
  DCHECK(!IsSetNeedsLayoutForbidden());
#endif
  bool already_needed_layout = NormalChildNeedsLayout();
  SetNeedsOverflowRecalc();
  SetNormalChildNeedsLayout(true);
  // FIXME: Replace MarkOnlyThis with the SubtreeLayoutScope code path and
  // remove the MarkingBehavior argument entirely.
  if (!already_needed_layout && mark_parents == kMarkContainerChain &&
      (!layouter || layouter->Root() != this))
    MarkContainerChainForLayout(!layouter, layouter);
}

inline void LayoutObject::SetNeedsPositionedMovementLayout() {
  bool already_needed_layout = NeedsPositionedMovementLayout();
  SetNeedsOverflowRecalc();
  SetNeedsPositionedMovementLayout(true);
#if DCHECK_IS_ON()
  DCHECK(!IsSetNeedsLayoutForbidden());
#endif
  if (!already_needed_layout)
    MarkContainerChainForLayout();
}

inline void LayoutObject::SetIsInLayoutNGInlineFormattingContext(
    bool new_value) {
  DCHECK(!GetDocument().InPostLifecycleSteps());
  if (IsInLayoutNGInlineFormattingContext() == new_value)
    return;
  InLayoutNGInlineFormattingContextWillChange(new_value);
  // The association cache for inline fragments is in union. Make sure the
  // cache is cleared before and after changing this flag.
  DCHECK(!HasInlineFragments());
  bitfields_.SetIsInLayoutNGInlineFormattingContext(new_value);
  DCHECK(!HasInlineFragments());
}

inline void LayoutObject::SetHasBoxDecorationBackground(bool b) {
  DCHECK(!GetDocument().InPostLifecycleSteps());
  if (b == bitfields_.HasBoxDecorationBackground())
    return;

  bitfields_.SetHasBoxDecorationBackground(b);
}

enum class LayoutObjectSide {
  kRemainingTextIfOnBoundary,
  kFirstLetterIfOnBoundary
};
CORE_EXPORT const LayoutObject* AssociatedLayoutObjectOf(
    const Node&,
    int offset_in_node,
    LayoutObjectSide = LayoutObjectSide::kRemainingTextIfOnBoundary);

CORE_EXPORT std::ostream& operator<<(std::ostream&, const LayoutObject*);
CORE_EXPORT std::ostream& operator<<(std::ostream&, const LayoutObject&);

bool IsMenuList(const LayoutObject* object);
CORE_EXPORT bool IsListBox(const LayoutObject* object);

}  // namespace blink

#if DCHECK_IS_ON()
// Outside the blink namespace for ease of invocation from gdb.
CORE_EXPORT void ShowTree(const blink::LayoutObject*);
CORE_EXPORT void ShowLineTree(const blink::LayoutObject*);
CORE_EXPORT void ShowLayoutTree(const blink::LayoutObject* object1);
// We don't make object2 an optional parameter so that showLayoutTree
// can be called from gdb easily.
CORE_EXPORT void ShowLayoutTree(const blink::LayoutObject* object1,
                                const blink::LayoutObject* object2);

#endif

namespace cppgc {
// Assign LayoutObject to be allocated on custom LayoutObjectSpace.
template <typename T>
struct SpaceTrait<
    T,
    std::enable_if_t<std::is_base_of<blink::LayoutObject, T>::value>> {
  using Space = blink::LayoutObjectSpace;
};
}  // namespace cppgc

#endif  // THIRD_PARTY_BLINK_RENDERER_CORE_LAYOUT_LAYOUT_OBJECT_H_
