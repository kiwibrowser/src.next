// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_PAINT_FRAGMENT_DATA_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_PAINT_FRAGMENT_DATA_H_

#include "third_party/abseil-cpp/absl/types/optional.h"
#include "third_party/blink/renderer/core/core_export.h"
#include "third_party/blink/renderer/core/layout/geometry/physical_rect.h"
#include "third_party/blink/renderer/core/paint/object_paint_properties.h"
#include "third_party/blink/renderer/platform/graphics/paint/cull_rect.h"
#include "third_party/blink/renderer/platform/graphics/paint/ref_counted_property_tree_state.h"
#include "third_party/blink/renderer/platform/heap/garbage_collected.h"
#include "third_party/blink/renderer/platform/heap/member.h"
#include "third_party/blink/renderer/platform/heap/visitor.h"
#include "third_party/blink/renderer/platform/wtf/allocator/allocator.h"

namespace blink {

class PaintLayer;
struct StickyPositionScrollingConstraints;

// Represents the data for a particular fragment of a LayoutObject.
// See README.md.
class CORE_EXPORT FragmentData final : public GarbageCollected<FragmentData> {
 public:
  FragmentData* NextFragment() const {
    return rare_data_ ? rare_data_->next_fragment_ : nullptr;
  }
  FragmentData& EnsureNextFragment();

  // We could let the compiler generate code to automatically clear the
  // next_fragment_ chain, but the code would cause stack overflow in some
  // cases (e.g. fast/multicol/infinitely-tall-content-in-outer-crash.html).
  // This function crear the next_fragment_ chain non-recursively.
  void ClearNextFragment();

  FragmentData& LastFragment();
  const FragmentData& LastFragment() const;

  // Physical offset of this fragment's local border box's top-left position
  // from the origin of the transform node of the fragment's property tree
  // state.
  PhysicalOffset PaintOffset() const { return paint_offset_; }
  void SetPaintOffset(const PhysicalOffset& paint_offset) {
    paint_offset_ = paint_offset;
  }

  // An id for this object that is unique for the lifetime of the WebView.
  UniqueObjectId UniqueId() const {
    DCHECK(rare_data_);
    return rare_data_->unique_id;
  }

  // The PaintLayer associated with this LayoutBoxModelObject. This can be null
  // depending on the return value of LayoutBoxModelObject::LayerTypeRequired().
  PaintLayer* Layer() const { return rare_data_ ? rare_data_->layer : nullptr; }
  void SetLayer(PaintLayer*);

  StickyPositionScrollingConstraints* StickyConstraints() const {
    return rare_data_ ? rare_data_->sticky_constraints : nullptr;
  }
  void SetStickyConstraints(StickyPositionScrollingConstraints* constraints) {
    if (!rare_data_ && !constraints)
      return;
    EnsureRareData().sticky_constraints = constraints;
  }

  // A fragment ID unique within the LayoutObject. In NG block fragmentation,
  // this is the fragmentainer index. In legacy block fragmentation, it's the
  // flow thread block-offset.
  wtf_size_t FragmentID() const {
    return rare_data_ ? rare_data_->fragment_id : 0;
  }
  void SetFragmentID(wtf_size_t id) {
    if (!rare_data_ && id == 0)
      return;
    EnsureRareData().fragment_id = id;
  }

  LayoutUnit LogicalTopInFlowThread() const {
#if DCHECK_IS_ON()
    DCHECK(!rare_data_ || rare_data_->has_set_flow_thread_offset_ ||
           !rare_data_->fragment_id);
#endif
    return LayoutUnit::FromRawValue(static_cast<int>(FragmentID()));
  }

  void SetLogicalTopInFlowThread(LayoutUnit top) {
    SetFragmentID(top.RawValue());
#if DCHECK_IS_ON()
    if (rare_data_)
      rare_data_->has_set_flow_thread_offset_ = true;
#endif
  }

  // The pagination offset is the additional factor to add in to map from flow
  // thread coordinates relative to the enclosing pagination layer, to visual
  // coordinates relative to that pagination layer. Not to be used in LayoutNG
  // fragment painting.
  PhysicalOffset LegacyPaginationOffset() const {
    return rare_data_ ? rare_data_->legacy_pagination_offset : PhysicalOffset();
  }
  void SetLegacyPaginationOffset(const PhysicalOffset& pagination_offset) {
    if (rare_data_ || pagination_offset != PhysicalOffset())
      EnsureRareData().legacy_pagination_offset = pagination_offset;
  }

  // Holds references to the paint property nodes created by this object.
  const ObjectPaintProperties* PaintProperties() const {
    return rare_data_ ? rare_data_->paint_properties.get() : nullptr;
  }
  ObjectPaintProperties* PaintProperties() {
    return rare_data_ ? rare_data_->paint_properties.get() : nullptr;
  }
  ObjectPaintProperties& EnsurePaintProperties() {
    EnsureRareData();
    if (!rare_data_->paint_properties)
      rare_data_->paint_properties = std::make_unique<ObjectPaintProperties>();
    return *rare_data_->paint_properties;
  }
  void ClearPaintProperties() {
    if (rare_data_)
      rare_data_->paint_properties = nullptr;
  }
  void EnsureId() { EnsureRareData(); }
  bool HasUniqueId() const { return rare_data_ && rare_data_->unique_id; }

  // This is a complete set of property nodes that should be used as a
  // starting point to paint a LayoutObject. This data is cached because some
  // properties inherit from the containing block chain instead of the
  // painting parent and cannot be derived in O(1) during the paint walk.
  // LocalBorderBoxProperties() includes fragment clip.
  //
  // For example: <div style='opacity: 0.3;'/>
  //   The div's local border box properties would have an opacity 0.3 effect
  //   node. Even though the div has no transform, its local border box
  //   properties would have a transform node that points to the div's
  //   ancestor transform space.
  PropertyTreeStateOrAlias LocalBorderBoxProperties() const {
    DCHECK(HasLocalBorderBoxProperties());

    // TODO(chrishtr): this should never happen, but does in practice and
    // we haven't been able to find all of the cases where it happens yet.
    // See crbug.com/1137883. Once we find more of them, remove this.
    if (!rare_data_ || !rare_data_->local_border_box_properties)
      return PropertyTreeState::Root();
    return rare_data_->local_border_box_properties->GetPropertyTreeState();
  }
  bool HasLocalBorderBoxProperties() const {
    return rare_data_ && rare_data_->local_border_box_properties;
  }
  void ClearLocalBorderBoxProperties() {
    if (rare_data_)
      rare_data_->local_border_box_properties = nullptr;
  }
  void SetLocalBorderBoxProperties(const PropertyTreeStateOrAlias& state) {
    EnsureRareData();
    if (!rare_data_->local_border_box_properties) {
      rare_data_->local_border_box_properties =
          std::make_unique<RefCountedPropertyTreeStateOrAlias>(state);
    } else {
      *rare_data_->local_border_box_properties = state;
    }
  }

  void SetCullRect(const CullRect& cull_rect) {
    EnsureRareData().cull_rect_ = cull_rect;
  }
  CullRect GetCullRect() const {
    return rare_data_ ? rare_data_->cull_rect_ : CullRect();
  }
  void SetContentsCullRect(const CullRect& contents_cull_rect) {
    EnsureRareData().contents_cull_rect_ = contents_cull_rect;
  }
  CullRect GetContentsCullRect() const {
    return rare_data_ ? rare_data_->contents_cull_rect_ : CullRect();
  }

  // This is the complete set of property nodes that can be used to paint the
  // contents of this fragment. It is similar to LocalBorderBoxProperties()
  // but includes properties (e.g., overflow clip, scroll translation,
  // isolation nodes) that apply to contents.
  PropertyTreeStateOrAlias ContentsProperties() const {
    return PropertyTreeStateOrAlias(ContentsTransform(), ContentsClip(),
                                    ContentsEffect());
  }

  const TransformPaintPropertyNodeOrAlias& PreTransform() const;
  const ClipPaintPropertyNodeOrAlias& PreClip() const;
  const EffectPaintPropertyNodeOrAlias& PreEffect() const;

  const TransformPaintPropertyNodeOrAlias& ContentsTransform() const;
  const ClipPaintPropertyNodeOrAlias& ContentsClip() const;
  const EffectPaintPropertyNodeOrAlias& ContentsEffect() const;

  ~FragmentData() = default;
  void Trace(Visitor* visitor) const { visitor->Trace(rare_data_); }

 private:
  friend class FragmentDataTest;

  // Contains rare data that that is not needed on all fragments.
  struct CORE_EXPORT RareData final : public GarbageCollected<RareData> {
   public:
    RareData();
    RareData(const RareData&) = delete;
    RareData& operator=(const RareData&) = delete;
    ~RareData();

    void SetLayer(PaintLayer*);

    void Trace(Visitor* visitor) const;

    // The following data fields are not fragment specific. Placed here just to
    // avoid separate data structure for them.
    Member<PaintLayer> layer;
    Member<StickyPositionScrollingConstraints> sticky_constraints;
    UniqueObjectId unique_id;

    // Fragment specific data.
    PhysicalOffset legacy_pagination_offset;
    wtf_size_t fragment_id = 0;
    std::unique_ptr<ObjectPaintProperties> paint_properties;
    std::unique_ptr<RefCountedPropertyTreeStateOrAlias>
        local_border_box_properties;
    CullRect cull_rect_;
    CullRect contents_cull_rect_;
    Member<FragmentData> next_fragment_;

#if DCHECK_IS_ON()
    // Legacy block fragmentation sets the flow thread offset for each
    // FragmentData object, and this is used as its fragment_id, whereas NG
    // block fragmentation uses the fragmentainer index instead. Here's a flag
    // which can be used to assert that legacy code which expects flow thread
    // offsets actually gets that.
    bool has_set_flow_thread_offset_ = false;
#endif
  };

  RareData& EnsureRareData();

  PhysicalOffset paint_offset_;
  Member<RareData> rare_data_;
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_CORE_PAINT_FRAGMENT_DATA_H_
