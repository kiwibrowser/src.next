// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/paint/object_paint_invalidator.h"

#include "third_party/blink/renderer/core/frame/local_frame.h"
#include "third_party/blink/renderer/core/frame/local_frame_view.h"
#include "third_party/blink/renderer/core/layout/layout_embedded_content.h"
#include "third_party/blink/renderer/core/layout/layout_view.h"
#include "third_party/blink/renderer/core/page/chrome_client.h"
#include "third_party/blink/renderer/core/paint/paint_invalidator.h"
#include "third_party/blink/renderer/core/paint/paint_layer.h"

namespace blink {

#if DCHECK_IS_ON()
void ObjectPaintInvalidator::CheckPaintLayerNeedsRepaint() {
  DCHECK(!object_.PaintingLayer() ||
         object_.PaintingLayer()->SelfNeedsRepaint());
}
#endif

void ObjectPaintInvalidator::SlowSetPaintingLayerNeedsRepaint() {
  if (PaintLayer* painting_layer = object_.PaintingLayer())
    painting_layer->SetNeedsRepaint();
}

DISABLE_CFI_PERF
PaintInvalidationReason
ObjectPaintInvalidatorWithContext::ComputePaintInvalidationReason() {
  // This is before any early return to ensure the previous visibility status is
  // saved.
  bool previous_visibility_visible = object_.PreviousVisibilityVisible();
  object_.GetMutableForPainting().UpdatePreviousVisibilityVisible();
  if (object_.VisualRectRespectsVisibility() && !previous_visibility_visible &&
      object_.StyleRef().Visibility() != EVisibility::kVisible)
    return PaintInvalidationReason::kNone;

  if (!object_.ShouldCheckForPaintInvalidation() && !context_.subtree_flags) {
    // No paint invalidation flag. No paint invalidation is needed.
    return PaintInvalidationReason::kNone;
  }

  if (object_.ShouldDoFullPaintInvalidation())
    return object_.FullPaintInvalidationReason();

  if (context_.subtree_flags &
      PaintInvalidatorContext::kSubtreeFullInvalidation)
    return PaintInvalidationReason::kSubtree;

  if (context_.fragment_data->PaintOffset() != context_.old_paint_offset)
    return PaintInvalidationReason::kGeometry;

  if (object_.GetDocument().InForcedColorsMode() && object_.IsLayoutBlockFlow())
    return PaintInvalidationReason::kBackplate;

  // Force full paint invalidation if the object has background-clip:text to
  // update the background on any change in the subtree.
  if (object_.StyleRef().BackgroundClip() == EFillBox::kText)
    return PaintInvalidationReason::kBackground;

  // Incremental invalidation is only applicable to LayoutBoxes. Return
  // kIncremental. BoxPaintInvalidator may override this reason with a full
  // paint invalidation reason if needed.
  if (object_.IsBox())
    return PaintInvalidationReason::kIncremental;

  return PaintInvalidationReason::kNone;
}

DISABLE_CFI_PERF
void ObjectPaintInvalidatorWithContext::InvalidatePaintWithComputedReason(
    PaintInvalidationReason reason) {
  DCHECK(!(context_.subtree_flags &
           PaintInvalidatorContext::kSubtreeNoInvalidation));

  if (reason == PaintInvalidationReason::kNone) {
    if (!object_.ShouldInvalidateSelection())
      return;
    // See layout_selection.cc SetShouldInvalidateIfNeeded() for the reason
    // for the IsSVGText() condition here.
    if (!object_.CanBeSelectionLeaf() && !object_.IsSVGText())
      return;

    reason = PaintInvalidationReason::kSelection;
    if (const auto* selection_client =
            object_.GetSelectionDisplayItemClient()) {
      // Invalidate the selection display item client only.
      context_.painting_layer->SetNeedsRepaint();
      selection_client->Invalidate(reason);
      return;
    }
  }

  context_.painting_layer->SetNeedsRepaint();
  object_.InvalidateDisplayItemClients(reason);
}

}  // namespace blink
