// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_PAINT_BOX_PAINTER_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_PAINT_BOX_PAINTER_H_

#include "third_party/blink/renderer/core/layout/background_bleed_avoidance.h"
#include "third_party/blink/renderer/core/layout/layout_box.h"
#include "third_party/blink/renderer/core/paint/rounded_inner_rect_clipper.h"
#include "third_party/blink/renderer/platform/geometry/layout_size.h"
#include "third_party/blink/renderer/platform/graphics/graphics_types.h"
#include "third_party/blink/renderer/platform/graphics/paint/drawing_recorder.h"
#include "third_party/blink/renderer/platform/graphics/paint/region_capture_data.h"
#include "third_party/blink/renderer/platform/wtf/allocator/allocator.h"

namespace blink {

class BoxPainter {
  STACK_ALLOCATED();

 public:
  BoxPainter(const LayoutBox& layout_box) : layout_box_(layout_box) {}
  void Paint(const PaintInfo&);

  void PaintChildren(const PaintInfo&);
  void PaintBoxDecorationBackground(const PaintInfo&,
                                    const PhysicalOffset& paint_offset);
  void PaintMask(const PaintInfo&, const PhysicalOffset& paint_offset);

  void PaintMaskImages(const PaintInfo&, const PhysicalRect&);

  // |visual_rect| is for the drawing display item, covering overflowing box
  // shadows and border image outsets. |paint_rect| is the border box rect in
  // paint coordinates.
  void PaintBoxDecorationBackgroundWithRect(
      const PaintInfo& paint_info,
      const gfx::Rect& visual_rect,
      const PhysicalRect& paint_rect,
      const DisplayItemClient& background_client);

  // Expands the bounds of the current paint chunk for hit test, and records
  // special touch action if any. This should be called in the background paint
  // phase even if there is no other painted content.
  void RecordHitTestData(const PaintInfo& paint_info,
                         const PhysicalRect& paint_rect,
                         const DisplayItemClient& background_client);

  // Records the bounds of the current paint chunk for potential cropping later
  // as part of tab capture.
  void RecordRegionCaptureData(const PaintInfo& paint_info,
                               const PhysicalRect& paint_rect,
                               const DisplayItemClient& background_client);

  // This should be called in the background paint phase even if there is no
  // other painted content.
  void RecordScrollHitTestData(const PaintInfo& paint_info,
                               const DisplayItemClient& background_client);

  // Calculates the visual rect (see DisplayItem::VisualRect() for definition)
  // from the self visual overflow of the LayoutBox and |paint_offset|.
  // This visual rect contains all possible painted results of the LayoutBox.
  // In a particular painter, we can also use a tighter visual rect instead of
  // this visual rect, if it's easy and beneficial to do so.
  // In most cases we use BoxDrawingRecorder which calls this function, instead
  // of directly using this function.
  gfx::Rect VisualRect(const PhysicalOffset& paint_offset);

 private:
  void PaintBackground(const PaintInfo&,
                       const PhysicalRect&,
                       const Color& background_color,
                       BackgroundBleedAvoidance = kBackgroundBleedNone);

  const LayoutBox& layout_box_;
};

// A wrapper of DrawingRecorder for LayoutBox, providing the default visual
// rect. See BoxPainter::VisualRect().
class BoxDrawingRecorder : public DrawingRecorder {
 public:
  BoxDrawingRecorder(GraphicsContext& context,
                     const LayoutBox& box,
                     DisplayItem::Type type,
                     const PhysicalOffset& paint_offset)
      : DrawingRecorder(context,
                        box,
                        type,
                        BoxPainter(box).VisualRect(paint_offset)) {}

  BoxDrawingRecorder(GraphicsContext& context,
                     const LayoutBox& box,
                     PaintPhase phase,
                     const PhysicalOffset& paint_offset)
      : BoxDrawingRecorder(context,
                           box,
                           DisplayItem::PaintPhaseToDrawingType(phase),
                           paint_offset) {}
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_CORE_PAINT_BOX_PAINTER_H_
