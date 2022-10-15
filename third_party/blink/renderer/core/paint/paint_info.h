/*
 * Copyright (C) 2000 Lars Knoll (knoll@kde.org)
 *           (C) 2000 Antti Koivisto (koivisto@kde.org)
 *           (C) 2000 Dirk Mueller (mueller@kde.org)
 *           (C) 2004 Allan Sandfeld Jensen (kde@carewolf.com)
 * Copyright (C) 2003, 2004, 2005, 2006, 2007, 2008, 2009 Apple Inc. All rights
 * reserved.
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

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_PAINT_PAINT_INFO_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_PAINT_PAINT_INFO_H_

#include "base/check_op.h"
#include "third_party/blink/renderer/core/core_export.h"
#include "third_party/blink/renderer/core/layout/layout_box.h"
#include "third_party/blink/renderer/core/layout/ng/ng_physical_box_fragment.h"
#include "third_party/blink/renderer/core/paint/paint_flags.h"
#include "third_party/blink/renderer/core/paint/paint_phase.h"
#include "third_party/blink/renderer/platform/geometry/layout_rect.h"
#include "third_party/blink/renderer/platform/graphics/graphics_context.h"
#include "third_party/blink/renderer/platform/graphics/paint/cull_rect.h"
#include "third_party/blink/renderer/platform/graphics/paint/display_item.h"
#include "third_party/blink/renderer/platform/wtf/allocator/allocator.h"
#include "ui/gfx/geometry/rect.h"

namespace blink {

struct CORE_EXPORT PaintInfo {
  STACK_ALLOCATED();

 public:
  PaintInfo(GraphicsContext& context,
            const CullRect& cull_rect,
            PaintPhase phase,
            PaintFlags paint_flags = PaintFlag::kNoFlag)
      : context(context),
        phase(phase),
        cull_rect_(cull_rect),
        paint_flags_(paint_flags) {}

  PaintInfo(GraphicsContext& new_context,
            const PaintInfo& copy_other_fields_from)
      : context(new_context),
        phase(copy_other_fields_from.phase),
        cull_rect_(copy_other_fields_from.cull_rect_),
        fragment_id_(copy_other_fields_from.fragment_id_),
        paint_flags_(copy_other_fields_from.paint_flags_) {
    // We should never pass these flags to other PaintInfo.
    DCHECK(!copy_other_fields_from.is_painting_background_in_contents_space);
    DCHECK(!copy_other_fields_from.skips_background_);
  }

  // Creates a PaintInfo for painting descendants. See comments about the paint
  // phases in PaintPhase.h for details.
  PaintInfo ForDescendants() const {
    PaintInfo result(*this);

    // We should never start to paint descendant when the flag is set.
    DCHECK(!result.is_painting_background_in_contents_space);

    if (phase == PaintPhase::kDescendantOutlinesOnly)
      result.phase = PaintPhase::kOutline;
    else if (phase == PaintPhase::kDescendantBlockBackgroundsOnly)
      result.phase = PaintPhase::kBlockBackground;
    return result;
  }

  bool ShouldOmitCompositingInfo() const {
    return paint_flags_ & PaintFlag::kOmitCompositingInfo;
  }

  bool IsRenderingClipPathAsMaskImage() const {
    return paint_flags_ & PaintFlag::kPaintingClipPathAsMask;
  }
  bool IsRenderingResourceSubtree() const {
    return paint_flags_ & PaintFlag::kPaintingResourceSubtree;
  }

  bool ShouldSkipBackground() const { return skips_background_; }
  void SetSkipsBackground(bool b) { skips_background_ = b; }

  bool ShouldAddUrlMetadata() const {
    return paint_flags_ & PaintFlag::kAddUrlMetadata;
  }

  DisplayItem::Type DisplayItemTypeForClipping() const {
    return DisplayItem::PaintPhaseToClipType(phase);
  }

  PaintFlags GetPaintFlags() const { return paint_flags_; }

  const CullRect& GetCullRect() const { return cull_rect_; }
  void SetCullRect(const CullRect& cull_rect) { cull_rect_ = cull_rect; }

  bool IntersectsCullRect(
      const PhysicalRect& rect,
      const PhysicalOffset& offset = PhysicalOffset()) const {
    return cull_rect_.Intersects(
        ToEnclosingRect(PhysicalRect(rect.offset + offset, rect.size)));
  }

  void ApplyInfiniteCullRect() { cull_rect_ = CullRect::Infinite(); }

  void TransformCullRect(const TransformPaintPropertyNode& transform) {
    cull_rect_.ApplyTransform(transform);
  }

  // Returns the fragment of the current painting object matching the current
  // layer fragment.
  const FragmentData* FragmentToPaint(const LayoutObject& object) const {
    if (fragment_id_ == WTF::kNotFound)
      return &object.FirstFragment();
    for (const auto* fragment = &object.FirstFragment(); fragment;
         fragment = fragment->NextFragment()) {
      if (fragment->FragmentID() == fragment_id_)
        return fragment;
    }
    // No fragment of the current painting object matches the layer fragment,
    // which means the object should not paint in this fragment.
    return nullptr;
  }

  // Returns the FragmentData of the specified physical fragment. If we're
  // performing fragment traversal, it will map directly to the right
  // FragmentData. Otherwise we'll fall back to matching against the current
  // PaintLayerFragment.
  const FragmentData* FragmentToPaint(
      const NGPhysicalFragment& fragment) const {
    if (fragment_id_ == WTF::kNotFound)
      return fragment.GetFragmentData();
    return FragmentToPaint(*fragment.GetLayoutObject());
  }

  wtf_size_t FragmentID() const { return fragment_id_; }
  void SetFragmentID(wtf_size_t id) { fragment_id_ = id; }
  void SetIsInFragmentTraversal() { fragment_id_ = WTF::kNotFound; }

  bool IsPaintingBackgroundInContentsSpace() const {
    return is_painting_background_in_contents_space;
  }
  void SetIsPaintingBackgroundInContentsSpace(bool b) {
    is_painting_background_in_contents_space = b;
  }

  bool DescendantPaintingBlocked() const {
    return descendant_painting_blocked_;
  }
  void SetDescendantPaintingBlocked(bool blocked) {
    descendant_painting_blocked_ = blocked;
  }

  GraphicsContext& context;
  PaintPhase phase;

 private:
  CullRect cull_rect_;

  // The ID of the fragment that we're currently painting.
  //
  // This is always used in legacy block fragmentation. In NG block
  // fragmentation, it's only used when painting self-painting non-atomic
  // inlines (because we currently have no way of mapping from
  // NGPhysicalFragment to FragmentData in such cases).
  wtf_size_t fragment_id_ = WTF::kNotFound;

  const PaintFlags paint_flags_;

  bool is_painting_background_in_contents_space = false;
  bool skips_background_ = false;

  // Used by display-locking.
  bool descendant_painting_blocked_ = false;
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_CORE_PAINT_PAINT_INFO_H_
