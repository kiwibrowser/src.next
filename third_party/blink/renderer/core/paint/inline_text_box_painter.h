// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_PAINT_INLINE_TEXT_BOX_PAINTER_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_PAINT_INLINE_TEXT_BOX_PAINTER_H_

#include "third_party/blink/renderer/core/editing/markers/document_marker.h"
#include "third_party/blink/renderer/core/style/computed_style_constants.h"
#include "third_party/blink/renderer/platform/geometry/layout_rect.h"
#include "third_party/blink/renderer/platform/wtf/allocator/allocator.h"

namespace blink {

struct PaintInfo;

class Color;
class ComputedStyle;
class Font;
class GraphicsContext;
class InlineTextBox;
class LayoutObject;
class LayoutTextCombine;
class StyleableMarker;
class DocumentMarker;
struct PhysicalOffset;
struct PhysicalRect;

enum class DocumentMarkerPaintPhase { kForeground, kBackground };

class InlineTextBoxPainter {
  STACK_ALLOCATED();

 public:
  InlineTextBoxPainter(const InlineTextBox& inline_text_box)
      : inline_text_box_(inline_text_box) {}

  void Paint(const PaintInfo&, const PhysicalOffset& paint_offset);

  // We don't paint composition or spelling markers that overlap a suggestion
  // marker (to match the native Android behavior). This method lets us throw
  // out the overlapping composition and spelling markers in O(N log N) time
  // where N is the total number of DocumentMarkers in this node.
  DocumentMarkerVector ComputeMarkersToPaint() const;

  void PaintDocumentMarkers(const DocumentMarkerVector& markers_to_paint,
                            const PaintInfo&,
                            const PhysicalOffset& box_origin,
                            const ComputedStyle&,
                            const Font&,
                            DocumentMarkerPaintPhase);
  void PaintDocumentMarker(const PaintInfo&,
                           const PhysicalOffset& box_origin,
                           const DocumentMarker&,
                           const ComputedStyle&,
                           const Font&,
                           bool grammar);
  void PaintTextMarkerForeground(const PaintInfo&,
                                 const PhysicalOffset& box_origin,
                                 const DocumentMarker&,
                                 const ComputedStyle&,
                                 const Font&);
  void PaintTextMarkerBackground(const PaintInfo&,
                                 const PhysicalOffset& box_origin,
                                 const DocumentMarker&,
                                 const ComputedStyle&,
                                 const Font&);

 private:
  enum class PaintOptions { kNormal, kCombinedText };

  void PaintSingleMarkerBackgroundRun(GraphicsContext&,
                                      const PhysicalOffset& box_origin,
                                      const ComputedStyle&,
                                      const Font&,
                                      Color background_color,
                                      int start_pos,
                                      int end_pos);

  // Returns the selection rect.
  template <PaintOptions>
  PhysicalRect PaintSelection(GraphicsContext&,
                              const PhysicalRect& box_rect,
                              const ComputedStyle&,
                              const Font&,
                              Color text_color,
                              LayoutTextCombine* = nullptr);

  template <PaintOptions>
  PhysicalRect GetSelectionRect(GraphicsContext&,
                                const PhysicalRect& box_rect,
                                const ComputedStyle&,
                                const Font&,
                                LayoutTextCombine* = nullptr,
                                bool allow_empty_selection = false);

  void PaintStyleableMarkerUnderline(GraphicsContext&,
                                     const PhysicalOffset& box_origin,
                                     const StyleableMarker&,
                                     const ComputedStyle&,
                                     const Font&);
  struct PaintOffsets {
    unsigned start;
    unsigned end;
  };
  PaintOffsets ApplyTruncationToPaintOffsets(const PaintOffsets&);
  // For markers that shouldn't draw over a truncation ellipsis (i.e., not
  // text match markers, which do draw over said ellipsis)
  PaintOffsets MarkerPaintStartAndEnd(const DocumentMarker&);

  void ExpandToIncludeNewlineForSelection(PhysicalRect&);
  LayoutObject& InlineLayoutObject() const;

  const InlineTextBox& inline_text_box_;
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_CORE_PAINT_INLINE_TEXT_BOX_PAINTER_H_
