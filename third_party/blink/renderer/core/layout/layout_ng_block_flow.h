// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_LAYOUT_LAYOUT_NG_BLOCK_FLOW_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_LAYOUT_LAYOUT_NG_BLOCK_FLOW_H_

#include "third_party/blink/renderer/core/core_export.h"
#include "third_party/blink/renderer/core/layout/layout_block_flow.h"

namespace blink {

struct InlineNodeData;

// This overrides the default layout block algorithm to use Layout NG.
class CORE_EXPORT LayoutNGBlockFlow : public LayoutBlockFlow {
 public:
  explicit LayoutNGBlockFlow(ContainerNode*);
  ~LayoutNGBlockFlow() override;
  void Trace(Visitor*) const override;

  const char* GetName() const override {
    NOT_DESTROYED();
    return "LayoutNGBlockFlow";
  }

  bool NodeAtPoint(HitTestResult&,
                   const HitTestLocation&,
                   const PhysicalOffset& accumulated_offset,
                   HitTestPhase) override;

  PositionWithAffinity PositionForPoint(const PhysicalOffset&) const override;

  // LayoutBlockFlow overrides:
  InlineNodeData* TakeInlineNodeData() final;
  InlineNodeData* GetInlineNodeData() const final;
  void ResetInlineNodeData() final;
  void ClearInlineNodeData() final;

 protected:
  bool IsLayoutNGBlockFlow() const final {
    NOT_DESTROYED();
    return true;
  }
  void StyleDidChange(StyleDifference, const ComputedStyle* old_style) override;

  void AddOutlineRects(OutlineRectCollector&,
                       LayoutObject::OutlineInfo*,
                       const PhysicalOffset& additional_offset,
                       OutlineType) const final;

  void DirtyLinesFromChangedChild(LayoutObject* child) final;

  Member<InlineNodeData> ng_inline_node_data_;
};

template <>
struct DowncastTraits<LayoutNGBlockFlow> {
  static bool AllowFrom(const LayoutObject& object) {
    return object.IsLayoutNGBlockFlow();
  }
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_CORE_LAYOUT_LAYOUT_NG_BLOCK_FLOW_H_
