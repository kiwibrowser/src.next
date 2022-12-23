// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/paint/embedded_object_painter.h"

#include "third_party/blink/renderer/core/frame/settings.h"
#include "third_party/blink/renderer/core/layout/layout_embedded_object.h"
#include "third_party/blink/renderer/core/layout/layout_theme.h"
#include "third_party/blink/renderer/core/paint/box_painter.h"
#include "third_party/blink/renderer/core/paint/embedded_content_painter.h"
#include "third_party/blink/renderer/core/paint/paint_auto_dark_mode.h"
#include "third_party/blink/renderer/core/paint/paint_info.h"
#include "third_party/blink/renderer/platform/fonts/font.h"
#include "third_party/blink/renderer/platform/fonts/font_selector.h"
#include "third_party/blink/renderer/platform/fonts/text_run_paint_info.h"
#include "third_party/blink/renderer/platform/graphics/paint/drawing_recorder.h"
#include "third_party/blink/renderer/platform/graphics/path.h"
#include "third_party/blink/renderer/platform/text/text_run.h"

namespace blink {

static const float kReplacementTextRoundedRectHeight = 18;
static const float kReplacementTextRoundedRectLeftRightTextMargin = 6;
static const float kReplacementTextRoundedRectOpacity = 0.20f;
static const float kReplacementTextRoundedRectRadius = 5;
static const float kReplacementTextTextOpacity = 0.55f;

static Font ReplacementTextFont(const Document* document) {
  FontDescription font_description;
  LayoutTheme::GetTheme().SystemFont(CSSValueID::kWebkitSmallControl,
                                     font_description, document);
  font_description.SetWeight(BoldWeightValue());
  font_description.SetComputedSize(font_description.SpecifiedSize());
  Font font(font_description);
  return font;
}

void EmbeddedObjectPainter::PaintReplaced(const PaintInfo& paint_info,
                                          const PhysicalOffset& paint_offset) {
  if (!layout_embedded_object_.ShowsUnavailablePluginIndicator()) {
    EmbeddedContentPainter(layout_embedded_object_)
        .PaintReplaced(paint_info, paint_offset);
    return;
  }

  if (paint_info.phase == PaintPhase::kSelectionDragImage)
    return;

  GraphicsContext& context = paint_info.context;
  if (DrawingRecorder::UseCachedDrawingIfPossible(
          context, layout_embedded_object_, paint_info.phase))
    return;

  PhysicalRect content_rect = layout_embedded_object_.PhysicalContentBoxRect();
  content_rect.Move(paint_offset);
  BoxDrawingRecorder recorder(context, layout_embedded_object_,
                              paint_info.phase, paint_offset);

  Font font = ReplacementTextFont(&layout_embedded_object_.GetDocument());
  const SimpleFontData* font_data = font.PrimaryFont();
  DCHECK(font_data);
  if (!font_data)
    return;

  TextRun text_run(layout_embedded_object_.UnavailablePluginReplacementText());
  gfx::SizeF text_geometry(font.Width(text_run),
                           font_data->GetFontMetrics().Height());

  PhysicalRect background_rect(
      LayoutUnit(), LayoutUnit(),
      LayoutUnit(text_geometry.width() +
                 2 * kReplacementTextRoundedRectLeftRightTextMargin),
      LayoutUnit(kReplacementTextRoundedRectHeight));
  background_rect.offset += content_rect.Center() - background_rect.Center();
  FloatRoundedRect rounded_background_rect(
      gfx::RectF(ToPixelSnappedRect(background_rect)),
      kReplacementTextRoundedRectRadius);
  Color color = ScaleAlpha(Color::kWhite, kReplacementTextRoundedRectOpacity);
  AutoDarkMode auto_dark_mode(
      PaintAutoDarkMode(layout_embedded_object_.StyleRef(),
                        DarkModeFilter::ElementRole::kBackground));
  context.FillRoundedRect(rounded_background_rect, color, auto_dark_mode);

  gfx::RectF text_rect(gfx::PointF(), text_geometry);
  text_rect.Offset(gfx::PointF(content_rect.Center()) -
                   text_rect.CenterPoint());
  TextRunPaintInfo run_info(text_run);
  context.SetFillColor(ScaleAlpha(Color::kBlack, kReplacementTextTextOpacity));
  context.DrawBidiText(
      font, run_info,
      text_rect.origin() +
          gfx::Vector2dF(0, font_data->GetFontMetrics().Ascent()),
      auto_dark_mode);
}

}  // namespace blink
