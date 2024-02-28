// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_LAYOUT_BLOCK_LAYOUT_ALGORITHM_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_LAYOUT_BLOCK_LAYOUT_ALGORITHM_H_

#include "base/memory/scoped_refptr.h"
#include "third_party/abseil-cpp/absl/types/optional.h"
#include "third_party/blink/renderer/core/core_export.h"
#include "third_party/blink/renderer/core/layout/block_break_token.h"
#include "third_party/blink/renderer/core/layout/block_node.h"
#include "third_party/blink/renderer/core/layout/box_fragment_builder.h"
#include "third_party/blink/renderer/core/layout/exclusions/exclusion_space.h"
#include "third_party/blink/renderer/core/layout/floats_utils.h"
#include "third_party/blink/renderer/core/layout/geometry/margin_strut.h"
#include "third_party/blink/renderer/core/layout/inline/inline_child_layout_context.h"
#include "third_party/blink/renderer/core/layout/layout_result.h"
#include "third_party/blink/renderer/core/layout/layout_algorithm.h"
#include "third_party/blink/renderer/core/layout/unpositioned_float.h"
#include "third_party/blink/renderer/platform/geometry/layout_unit.h"

namespace blink {

class ConstraintSpace;
class LogicalFragment;
enum class BreakStatus;

// This struct is used for communicating to a child the position of the previous
// inflow child. This will be used to calculate the position of the next child.
struct PreviousInflowPosition {
  LayoutUnit logical_block_offset;
  MarginStrut margin_strut;
  // > 0: Block-end annotation space of the previous line
  // < 0: Block-end annotation overflow of the previous line
  LayoutUnit block_end_annotation_space;
  bool self_collapsing_child_had_clearance;
};

// This struct holds information for the current inflow child. The data is not
// useful outside of handling this single inflow child.
struct InflowChildData {
  InflowChildData(BfcOffset bfc_offset_estimate,
                  const MarginStrut& margin_strut,
                  const BoxStrut& margins,
                  bool is_pushed_by_floats = false)
      : bfc_offset_estimate(bfc_offset_estimate),
        margin_strut(margin_strut),
        margins(margins),
        is_pushed_by_floats(is_pushed_by_floats) {}

  InflowChildData(const InflowChildData&) = default;

  BfcOffset bfc_offset_estimate;
  MarginStrut margin_strut;
  BoxStrut margins;
  bool is_pushed_by_floats = false;
};

// A class for general block layout (e.g. a <div> with no special style).
// Lays out the children in sequence.
class CORE_EXPORT BlockLayoutAlgorithm
    : public LayoutAlgorithm<BlockNode, BoxFragmentBuilder, BlockBreakToken> {
 public:
  // Default constructor.
  explicit BlockLayoutAlgorithm(const LayoutAlgorithmParams& params);

  ~BlockLayoutAlgorithm() override;

  void SetBoxType(PhysicalFragment::BoxType type);

  MinMaxSizesResult ComputeMinMaxSizes(const MinMaxSizesFloatInput&) override;
  const LayoutResult* Layout() override;

 private:
  NOINLINE const LayoutResult* HandleNonsuccessfulLayoutResult(
      const LayoutResult*);

  const LayoutResult* LayoutInlineChild(const InlineNode& child);
  NOINLINE const LayoutResult* LayoutWithSimpleInlineChildLayoutContext(
      const InlineNode& child);
  template <wtf_size_t capacity>
  NOINLINE const LayoutResult* LayoutWithOptimalInlineChildLayoutContext(
      const InlineNode& child);

  NOINLINE const LayoutResult* RelayoutIgnoringLineClamp();

  inline const LayoutResult* Layout(
      InlineChildLayoutContext* inline_child_layout_context);

  const LayoutResult* FinishLayout(PreviousInflowPosition*,
                                   InlineChildLayoutContext*);

  // Return the BFC block offset of this block.
  LayoutUnit BfcBlockOffset() const {
    // If we have resolved our BFC block offset, use that.
    if (container_builder_.BfcBlockOffset())
      return *container_builder_.BfcBlockOffset();
    // Otherwise fall back to the BFC block offset assigned by the parent
    // algorithm.
    return GetConstraintSpace().GetBfcOffset().block_offset;
  }

  // Return the BFC block offset of the next block-start border edge (for some
  // child) we'd get if we commit pending margins.
  LayoutUnit NextBorderEdge(
      const PreviousInflowPosition& previous_inflow_position) const {
    return BfcBlockOffset() + previous_inflow_position.logical_block_offset +
           previous_inflow_position.margin_strut.Sum();
  }

  BoxStrut CalculateMargins(LayoutInputNode child,
                            bool is_new_fc,
                            LayoutUnit* additional_line_offset);

  // Creates a new constraint space for the current child.
  ConstraintSpace CreateConstraintSpaceForChild(
      const LayoutInputNode child,
      const BreakToken* child_break_token,
      const InflowChildData& child_data,
      const LogicalSize child_available_size,
      bool is_new_fc,
      const absl::optional<LayoutUnit> bfc_block_offset = absl::nullopt,
      bool has_clearance_past_adjoining_floats = false,
      LayoutUnit block_start_annotation_space = LayoutUnit());

  // @return Estimated BFC block offset for the "to be layout" child.
  InflowChildData ComputeChildData(const PreviousInflowPosition&,
                                   LayoutInputNode,
                                   const BreakToken* child_break_token,
                                   bool is_new_fc);

  PreviousInflowPosition ComputeInflowPosition(
      const PreviousInflowPosition&,
      const LayoutInputNode child,
      const InflowChildData&,
      const absl::optional<LayoutUnit>& child_bfc_block_offset,
      const LogicalOffset&,
      const LayoutResult&,
      const LogicalFragment&,
      bool self_collapsing_child_had_clearance);

  // Position an self-collapsing child using the parent BFC block-offset.
  // The fragment doesn't know its offset, but we can still calculate its BFC
  // position because the parent fragment's BFC is known.
  // Example:
  //   BFC Offset is known here because of the padding.
  //   <div style="padding: 1px">
  //     <div id="zero" style="margin: 1px"></div>
  LayoutUnit PositionSelfCollapsingChildWithParentBfc(
      const LayoutInputNode& child,
      const ConstraintSpace& child_space,
      const InflowChildData& child_data,
      const LayoutResult&) const;

  // Try to reuse part of cached fragments. When reusing is possible, this
  // function adds part of cached fragments to |container_builder_|, update
  // |break_token_| to continue layout from the last reused fragment, and
  // returns |true|. Otherwise returns |false|.
  bool TryReuseFragmentsFromCache(
      InlineNode child,
      PreviousInflowPosition*,
      const InlineBreakToken** inline_break_token_out);

  void HandleOutOfFlowPositioned(const PreviousInflowPosition&, BlockNode);
  void HandleFloat(const PreviousInflowPosition&,
                   BlockNode,
                   const BlockBreakToken*);

  // This uses the LayoutOpporunityIterator to position the fragment.
  //
  // An element that establishes a new formatting context must not overlap the
  // margin box of any floats within the current BFC.
  //
  // Example:
  // <div id="container">
  //   <div id="float"></div>
  //   <div id="new-fc" style="margin-top: 20px;"></div>
  // </div>
  // 1) If #new-fc is small enough to fit the available space right from #float
  //    then it will be placed there and we collapse its margin.
  // 2) If #new-fc is too big then we need to clear its position and place it
  //    below #float ignoring its vertical margin.
  //
  // Returns false if we need to abort layout, because a previously unknown BFC
  // block offset has now been resolved.
  LayoutResult::EStatus HandleNewFormattingContext(
      LayoutInputNode child,
      const BlockBreakToken* child_break_token,
      PreviousInflowPosition*);

  // Performs the actual layout of a new formatting context. This may be called
  // multiple times from HandleNewFormattingContext.
  const LayoutResult* LayoutNewFormattingContext(
      LayoutInputNode child,
      const BlockBreakToken* child_break_token,
      const InflowChildData&,
      BfcOffset origin_offset,
      bool abort_if_cleared,
      BfcOffset* out_child_bfc_offset,
      BoxStrut* out_resolved_margins);

  // Handle an in-flow child.
  // Returns false if we need to abort layout, because a previously unknown BFC
  // block offset has now been resolved. (Same as HandleNewFormattingContext).
  LayoutResult::EStatus HandleInflow(
      LayoutInputNode child,
      const BreakToken* child_break_token,
      PreviousInflowPosition*,
      InlineChildLayoutContext*,
      const InlineBreakToken** previous_inline_break_token);

  LayoutResult::EStatus FinishInflow(
      LayoutInputNode child,
      const BreakToken* child_break_token,
      const ConstraintSpace&,
      bool has_clearance_past_adjoining_floats,
      const LayoutResult*,
      InflowChildData*,
      PreviousInflowPosition*,
      InlineChildLayoutContext*,
      const InlineBreakToken** previous_inline_break_token);

  // Consume all remaining fragmentainer space. This happens when we decide to
  // break before a child.
  //
  // https://www.w3.org/TR/css-break-3/#box-splitting
  void ConsumeRemainingFragmentainerSpace(PreviousInflowPosition*);

  // Final adjustments before fragment creation. We need to prevent the fragment
  // from crossing fragmentainer boundaries, and rather create a break token if
  // we're out of space. As part of finalizing we may also discover that we need
  // to abort layout, because we've run out of space at a less-than-ideal
  // location, or that we need to relayout without block fragmentation (when a
  // clipped box gets overflowed past the fragmentation line). The return value
  // can be checked for this. Only if kContinue is returned, can a fragment be
  // created.
  BreakStatus FinalizeForFragmentation(
      LayoutUnit block_end_border_padding_added);

  // Insert a fragmentainer break before the child if necessary.
  // See |::blink::BreakBeforeChildIfNeeded()| for more documentation.
  BreakStatus BreakBeforeChildIfNeeded(LayoutInputNode child,
                                       const LayoutResult&,
                                       PreviousInflowPosition*,
                                       LayoutUnit bfc_block_offset,
                                       bool has_container_separation);

  // Look for a better breakpoint (than we already have) between lines (i.e. a
  // class B breakpoint), and store it.
  void UpdateEarlyBreakBetweenLines();

  // Propagates the baseline from the given |child| if needed.
  void PropagateBaselineFromLineBox(const PhysicalFragment& child,
                                    LayoutUnit block_offset);
  void PropagateBaselineFromBlockChild(const PhysicalFragment& child,
                                       const BoxStrut& margins,
                                       LayoutUnit block_offset);

  // If still unresolved, resolve the fragment's BFC block offset.
  //
  // This includes applying clearance, so the |bfc_block_offset| passed won't
  // be the final BFC block-offset, if it wasn't large enough to get past all
  // relevant floats. The updated BFC block-offset can be read out with
  // |ContainerBfcBlockOffset()|.
  //
  // If the |forced_bfc_block_offset| has a value, it will override the given
  // |bfc_block_offset|. Typically this comes from the input constraints, when
  // the current node has clearance past adjoining floats, or has a re-layout
  // due to a child resolving the BFC block-offset.
  //
  // In addition to resolving our BFC block offset, this will also position
  // pending floats, and update our in-flow layout state.
  //
  // Returns false if resolving the BFC block-offset resulted in needing to
  // abort layout. It will always return true otherwise. If the BFC
  // block-offset was already resolved, this method does nothing (and returns
  // true).
  bool ResolveBfcBlockOffset(
      PreviousInflowPosition*,
      LayoutUnit bfc_block_offset,
      const absl::optional<LayoutUnit> forced_bfc_block_offset);

  // This passes in the |forced_bfc_block_offset| from the input constraints,
  // which is almost always desired.
  bool ResolveBfcBlockOffset(PreviousInflowPosition* previous_inflow_position,
                             LayoutUnit bfc_block_offset) {
    return ResolveBfcBlockOffset(previous_inflow_position, bfc_block_offset,
                                 GetConstraintSpace().ForcedBfcBlockOffset());
  }

  // A very common way to resolve the BFC block offset is to simply commit the
  // pending margin, so here's a convenience overload for that.
  bool ResolveBfcBlockOffset(PreviousInflowPosition* previous_inflow_position) {
    return ResolveBfcBlockOffset(previous_inflow_position,
                                 NextBorderEdge(*previous_inflow_position));
  }

  // Mark this fragment as modifying its incoming margin-strut if it hasn't
  // resolved its BFC block-offset yet.
  void SetSubtreeModifiedMarginStrutIfNeeded(const Length* margin = nullptr) {
    if (container_builder_.BfcBlockOffset())
      return;

    if (margin && margin->IsZero())
      return;

    container_builder_.SetSubtreeModifiedMarginStrut();
  }

  // Return true if the BFC block offset has changed and this means that we
  // need to abort layout.
  bool NeedsAbortOnBfcBlockOffsetChange() const;

  // Positions a list marker for the specified block content.
  // Return false if it aborts when resolving BFC block offset for LI.
  bool PositionOrPropagateListMarker(const LayoutResult&,
                                     LogicalOffset*,
                                     PreviousInflowPosition*);

  // Positions a list marker when the block does not have any line boxes.
  // Return false if it aborts when resolving BFC block offset for LI.
  bool PositionListMarkerWithoutLineBoxes(PreviousInflowPosition*);

  // Calculates logical offset for the current fragment using either {@code
  // intrinsic_block_size_} when the fragment doesn't know it's offset or
  // {@code known_fragment_offset} if the fragment knows it's offset
  // @return Fragment's offset relative to the fragment's parent.
  LogicalOffset CalculateLogicalOffset(
      const LogicalFragment& fragment,
      LayoutUnit child_bfc_line_offset,
      const absl::optional<LayoutUnit>& child_bfc_block_offset);

  // In quirks mode the body element will stretch to fit the viewport.
  //
  // In order to determine the final block-size we need to take the available
  // block-size minus the total block-direction margin.
  //
  // This block-direction margin is non-trivial to calculate for the body
  // element, and is computed upfront for the |ClampIntrinsicBlockSize|
  // function.
  absl::optional<LayoutUnit> CalculateQuirkyBodyMarginBlockSum(
      const MarginStrut& end_margin_strut);

  // Return true if this is a list-item that may have to place a marker.
  bool ShouldPlaceUnpositionedListMarker() const {
    if (!node_.IsListItem())
      return false;
    // Also need to check if the constraint space is anonymous, which is the
    // case for columns (the list item marker should be placed by the multicol
    // container then, not the individual columns).
    if (!GetConstraintSpace().IsAnonymous()) {
      return true;
    }
    // Ensure we're really a column box. We can't use |BoxType| to call this
    // from the constructor.
    DCHECK(node_.GetLayoutBox()->SlowFirstChild()->IsLayoutFlowThread());
    return false;
  }

  // Returns true if |this| is a ruby segment (LayoutRubyColumn) and the
  // specified |child| is a ruby annotation box (LayoutRubyText).
  bool IsRubyText(const LayoutInputNode& child) const;

  // Layout |ruby_text_child| content, and decide the location of
  // |ruby_text_child|. This is called only if IsRubyText() returns true.
  void HandleRubyText(BlockNode ruby_text_child);

  // Layout |placeholder| content, and decide the location of |placeholder|.
  // This is called only if |this| is a text control.
  // This function returns a new value for `PreviousInflowPosition::
  // logical_block_offset`.
  LayoutUnit HandleTextControlPlaceholder(
      BlockNode placeholder,
      const PreviousInflowPosition& previous_inflow_position);
  // A helper for HandleTextControlPlaceholder().
  // This function returns a new value for `PreviousInflowPosition::
  // logical_block_offset`.
  LayoutUnit FinishTextControlPlaceholder(
      const LayoutResult* result,
      const LogicalOffset& offset,
      bool apply_fixed_size,
      const PreviousInflowPosition& previous_inflow_position);

  // Adjusts the inline offset of the slider thumb box from the value of
  // HTMLInputElement.
  LogicalOffset AdjustSliderThumbInlineOffset(
      const LogicalFragment& fragment,
      const LogicalOffset& logical_offset);

  LogicalSize child_percentage_size_;
  LogicalSize replaced_child_percentage_size_;

  const LayoutResult* previous_result_ = nullptr;

  const ColumnSpannerPath* column_spanner_path_ = nullptr;

  // Intrinsic block size based on child layout and containment.
  LayoutUnit intrinsic_block_size_;

  // The line box index at which we ran out of space. This where we'll actually
  // end up breaking, unless we determine that we should break earlier in order
  // to satisfy the widows request.
  int first_overflowing_line_ = 0;

  // Set if we should fit as many lines as there's room for, i.e. no early
  // break. In that case we'll break before first_overflowing_line_. In this
  // case there'll either be enough widows for the next fragment, or we have
  // determined that we're unable to fulfill the widows request.
  bool fit_all_lines_ : 1;

  // Set if we're resuming layout of a node that has already produced fragments.
  bool is_resuming_ : 1;

  // Set when we're to abort if the BFC block offset gets resolved or updated.
  // Sometimes we walk past elements (i.e. floats) that depend on the BFC block
  // offset being known (in order to position and lay themselves out properly).
  // When this happens, and we finally manage to resolve (or update) the BFC
  // block offset at some subsequent element, we need to check if this flag is
  // set, and abort layout if it is.
  bool abort_when_bfc_block_offset_updated_ : 1;

  // This will be set during block fragmentation, normally once we've processed
  // the first in-flow child of a container (but there are some exceptions to
  // this). It is used to check if we're at a valid class A or B breakpoint
  // (between block-level siblings or line box siblings).
  bool has_break_opportunity_before_next_child_ : 1;

  // If true, ignore the line-clamp property as truncation wont be required.
  bool ignore_line_clamp_ : 1;

  // If this is within a -webkit-line-clamp context.
  bool is_line_clamp_context_ : 1;

  // If set, this is the number of lines until a clamp. A value of 1 indicates
  // the current line should be clamped. This may go negative.
  absl::optional<int> lines_until_clamp_;

  // If set, one of the lines was clamped and this is the intrinsic size at the
  // time of the clamp.
  absl::optional<LayoutUnit> intrinsic_block_size_when_clamped_;
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_CORE_LAYOUT_BLOCK_LAYOUT_ALGORITHM_H_
