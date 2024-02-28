// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_LAYOUT_ANCHOR_QUERY_MAP_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_LAYOUT_ANCHOR_QUERY_MAP_H_

#include "third_party/blink/renderer/core/core_export.h"

#include "third_party/blink/renderer/core/layout/inline/fragment_items_builder.h"
#include "third_party/blink/renderer/core/layout/logical_fragment_link.h"
#include "third_party/blink/renderer/core/layout/anchor_query.h"
#include "third_party/blink/renderer/platform/text/writing_direction_mode.h"

namespace blink {

class LayoutBox;
class LayoutObject;

// This computes anchor queries for each containing block by traversing
// descendants.
//
// Normally anchor queries are propagated to the containing block chain during
// the layout. However, there are some exceptions.
// 1. When the containing block is an inline box, all OOFs are added to their
// inline formatting context.
// 2. When the containing block is in block fragmentation context, all OOFs are
// added to their fragmentainers.
// In such cases, traversing descendants is needed to compute anchor queries.
class CORE_EXPORT LogicalAnchorQueryMap {
  STACK_ALLOCATED();

 public:
  LogicalAnchorQueryMap(const LayoutBox& root_box,
                        const LogicalFragmentLinkVector& children,
                        const FragmentItemsBuilder::ItemWithOffsetList* items,
                        const WritingModeConverter& converter);

  // This constructor is for when the size of the container is not known yet.
  // This happens when laying out OOFs in a block fragmentation context, and
  // assumes children are fragmentainers.
  LogicalAnchorQueryMap(const LayoutBox& root_box,
                        const LogicalFragmentLinkVector& children,
                        WritingDirectionMode writing_direction);

  bool IsEmpty() const { return !has_anchor_queries_; }

  // Get |LogicalAnchorQuery| in the stitched coordinate system for the given
  // containing block. If there is no anchor query for the containing block,
  // returns an empty instance.
  const LogicalAnchorQuery& AnchorQuery(
      const LayoutObject& containing_block) const;

  // Update |children| when their anchor queries are changed.
  void SetChildren(
      const LogicalFragmentLinkVector& children,
      const FragmentItemsBuilder::ItemWithOffsetList* items = nullptr);

 private:
  void Update(const LayoutObject& layout_object) const;

  mutable HeapHashMap<Member<const LayoutObject>, Member<LogicalAnchorQuery>>
      queries_;
  mutable const LayoutObject* computed_for_ = nullptr;
  const LayoutBox& root_box_;
  const LogicalFragmentLinkVector* children_ = nullptr;
  const FragmentItemsBuilder::ItemWithOffsetList* items_ = nullptr;
  absl::optional<const WritingModeConverter> converter_;
  WritingDirectionMode writing_direction_;
  bool has_anchor_queries_ = false;
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_CORE_LAYOUT_ANCHOR_QUERY_MAP_H_
