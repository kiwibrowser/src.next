// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/paint/svg_inline_text_box_painter.h"

#include <memory>

#include "base/stl_util.h"
#include "third_party/abseil-cpp/absl/types/optional.h"
#include "third_party/blink/renderer/core/editing/editor.h"
#include "third_party/blink/renderer/core/editing/markers/document_marker_controller.h"
#include "third_party/blink/renderer/core/editing/markers/text_match_marker.h"
#include "third_party/blink/renderer/core/frame/local_frame.h"
#include "third_party/blink/renderer/core/layout/api/line_layout_api_shim.h"
#include "third_party/blink/renderer/core/layout/layout_theme.h"
#include "third_party/blink/renderer/core/layout/line/inline_flow_box.h"
#include "third_party/blink/renderer/core/layout/svg/layout_svg_inline_text.h"
#include "third_party/blink/renderer/core/layout/svg/line/svg_inline_text_box.h"
#include "third_party/blink/renderer/core/layout/svg/svg_layout_support.h"
#include "third_party/blink/renderer/core/layout/svg/svg_resources.h"
#include "third_party/blink/renderer/core/paint/highlight_painting_utils.h"
#include "third_party/blink/renderer/core/paint/inline_text_box_painter.h"
#include "third_party/blink/renderer/core/paint/paint_auto_dark_mode.h"
#include "third_party/blink/renderer/core/paint/paint_info.h"
#include "third_party/blink/renderer/core/paint/paint_timing.h"
#include "third_party/blink/renderer/core/paint/paint_timing_detector.h"
#include "third_party/blink/renderer/core/paint/selection_bounds_recorder.h"
#include "third_party/blink/renderer/core/paint/svg_object_painter.h"
#include "third_party/blink/renderer/core/paint/text_painter_base.h"
#include "third_party/blink/renderer/core/style/applied_text_decoration.h"
#include "third_party/blink/renderer/core/style/shadow_list.h"
#include "third_party/blink/renderer/core/svg/svg_element.h"
#include "third_party/blink/renderer/platform/fonts/text_run_paint_info.h"
#include "third_party/blink/renderer/platform/graphics/graphics_context_state_saver.h"
#include "third_party/blink/renderer/platform/graphics/paint/drawing_recorder.h"

namespace blink {

static inline bool TextShouldBePainted(
    const LayoutSVGInlineText& text_layout_object) {
  // Font::pixelSize(), returns FontDescription::computedPixelSize(), which
  // returns "int(x + 0.5)".  If the absolute font size on screen is below
  // x=0.5, don't render anything.
  return text_layout_object.ScaledFont()
      .GetFontDescription()
      .ComputedPixelSize();
}

bool SVGInlineTextBoxPainter::ShouldPaintSelection(
    const PaintInfo& paint_info) const {
  // Don't paint selections when printing.
  if (InlineLayoutObject().GetDocument().Printing())
    return false;
  // Don't paint selections when rendering a mask, clip-path (as a mask),
  // pattern or feImage (element reference.)
  if (paint_info.IsRenderingResourceSubtree())
    return false;
  return svg_inline_text_box_.IsSelected();
}

LayoutObject& SVGInlineTextBoxPainter::InlineLayoutObject() const {
  return *LineLayoutAPIShim::LayoutObjectFrom(
      svg_inline_text_box_.GetLineLayoutItem());
}

LayoutObject& SVGInlineTextBoxPainter::ParentInlineLayoutObject() const {
  return *LineLayoutAPIShim::LayoutObjectFrom(
      svg_inline_text_box_.Parent()->GetLineLayoutItem());
}

LayoutSVGInlineText& SVGInlineTextBoxPainter::InlineText() const {
  return To<LayoutSVGInlineText>(InlineLayoutObject());
}

void SVGInlineTextBoxPainter::Paint(const PaintInfo& paint_info,
                                    const PhysicalOffset& paint_offset) {
  DCHECK(paint_info.phase == PaintPhase::kForeground ||
         paint_info.phase == PaintPhase::kSelectionDragImage);
  DCHECK(svg_inline_text_box_.Truncation() == kCNoTruncation);

  if (svg_inline_text_box_.GetLineLayoutItem().StyleRef().Visibility() !=
          EVisibility::kVisible ||
      !svg_inline_text_box_.Len())
    return;

  // We're explicitly not supporting composition & custom underlines and custom
  // highlighters -- unlike InlineTextBox.  If we ever need that for SVG, it's
  // very easy to refactor and reuse the code.

  bool have_selection = ShouldPaintSelection(paint_info);
  if (!have_selection && paint_info.phase == PaintPhase::kSelectionDragImage)
    return;

  LayoutObject& parent_layout_object = ParentInlineLayoutObject();
  const ComputedStyle& style = parent_layout_object.StyleRef();

  absl::optional<SelectionBoundsRecorder> start_bounds_recorder;
  absl::optional<SelectionBoundsRecorder> end_bounds_recorder;
  if (have_selection && paint_info.phase == PaintPhase::kForeground) {
    const FrameSelection& frame_selection =
        InlineLayoutObject().GetFrame()->Selection();
    SelectionState selection_state =
        frame_selection.ComputeLayoutSelectionStateForInlineTextBox(
            svg_inline_text_box_);
    if (SelectionBoundsRecorder::ShouldRecordSelection(frame_selection,
                                                       selection_state)) {
      // Even when the selection state is kStartAndEnd for the
      // SVGInlineTextBox, we have to record the start and end bounds
      // separately since the selection rects are calculated per-fragment, and
      // the start and end of the selection don't necessarily occur in the same
      // fragment (i.e. don't have the same selection rect).
      int start_position, end_position;
      svg_inline_text_box_.SelectionStartEnd(start_position, end_position);
      if (selection_state == SelectionState::kStart ||
          selection_state == SelectionState::kStartAndEnd) {
        RecordSelectionBoundsForRange(
            start_position, start_position + 1, SelectionState::kStart, style,
            paint_info.context.GetPaintController(), start_bounds_recorder);
      }

      if (selection_state == SelectionState::kStartAndEnd ||
          selection_state == SelectionState::kEnd) {
        RecordSelectionBoundsForRange(
            end_position - 1, end_position, SelectionState::kEnd, style,
            paint_info.context.GetPaintController(), end_bounds_recorder);
      }
    }
  }

  LayoutSVGInlineText& text_layout_object = InlineText();
  if (!TextShouldBePainted(text_layout_object))
    return;

  if (!DrawingRecorder::UseCachedDrawingIfPossible(
          paint_info.context, svg_inline_text_box_, paint_info.phase)) {
    DrawingRecorder recorder(
        paint_info.context, svg_inline_text_box_, paint_info.phase,
        gfx::ToEnclosingRect(
            parent_layout_object.VisualRectInLocalSVGCoordinates()));
    InlineTextBoxPainter text_painter(svg_inline_text_box_);
    const DocumentMarkerVector& markers_to_paint =
        text_painter.ComputeMarkersToPaint();
    text_painter.PaintDocumentMarkers(
        markers_to_paint, paint_info, paint_offset, style,
        text_layout_object.ScaledFont(), DocumentMarkerPaintPhase::kBackground);

    if (!svg_inline_text_box_.TextFragments().IsEmpty())
      PaintTextFragments(paint_info, parent_layout_object);

    text_painter.PaintDocumentMarkers(
        markers_to_paint, paint_info, paint_offset, style,
        text_layout_object.ScaledFont(), DocumentMarkerPaintPhase::kForeground);
  }
}

void SVGInlineTextBoxPainter::PaintTextFragments(
    const PaintInfo& paint_info,
    LayoutObject& parent_layout_object) {
  const ComputedStyle& style = parent_layout_object.StyleRef();

  bool has_fill = style.HasFill();
  bool has_visible_stroke = style.HasVisibleStroke();

  const ComputedStyle* selection_style = &style;
  bool should_paint_selection = ShouldPaintSelection(paint_info);
  if (should_paint_selection) {
    selection_style = parent_layout_object.GetSelectionStyle();
    if (selection_style) {
      if (!has_fill)
        has_fill = selection_style->HasFill();
      if (!has_visible_stroke)
        has_visible_stroke = selection_style->HasVisibleStroke();
    } else {
      selection_style = &style;
    }
  }

  if (paint_info.IsRenderingClipPathAsMaskImage()) {
    has_fill = true;
    has_visible_stroke = false;
  }

  for (const SVGTextFragment& fragment : svg_inline_text_box_.TextFragments()) {
    GraphicsContextStateSaver state_saver(paint_info.context, false);
    absl::optional<AffineTransform> shader_transform;
    if (fragment.IsTransformed()) {
      state_saver.Save();
      const auto fragment_transform = fragment.BuildFragmentTransform();
      paint_info.context.ConcatCTM(fragment_transform);
      DCHECK(fragment_transform.IsInvertible());
      shader_transform = fragment_transform.Inverse();
    }

    // Spec: All text decorations except line-through should be drawn before the
    // text is filled and stroked; thus, the text is rendered on top of these
    // decorations.
    const Vector<AppliedTextDecoration>& decorations =
        style.AppliedTextDecorations();
    for (const AppliedTextDecoration& decoration : decorations) {
      if (EnumHasFlags(decoration.Lines(), TextDecorationLine::kUnderline))
        PaintDecoration(paint_info, TextDecorationLine::kUnderline, fragment);
      if (EnumHasFlags(decoration.Lines(), TextDecorationLine::kOverline))
        PaintDecoration(paint_info, TextDecorationLine::kOverline, fragment);
    }

    for (int i = 0; i < 3; i++) {
      switch (style.PaintOrderType(i)) {
        case PT_FILL:
          if (has_fill) {
            PaintText(paint_info, style, *selection_style, fragment,
                      kApplyToFillMode, should_paint_selection,
                      base::OptionalOrNullptr(shader_transform));
          }
          break;
        case PT_STROKE:
          if (has_visible_stroke) {
            PaintText(paint_info, style, *selection_style, fragment,
                      kApplyToStrokeMode, should_paint_selection,
                      base::OptionalOrNullptr(shader_transform));
          }
          break;
        case PT_MARKERS:
          // Markers don't apply to text
          break;
        default:
          NOTREACHED();
          break;
      }
    }

    // Spec: Line-through should be drawn after the text is filled and stroked;
    // thus, the line-through is rendered on top of the text.
    for (const AppliedTextDecoration& decoration : decorations) {
      if (EnumHasFlags(decoration.Lines(), TextDecorationLine::kLineThrough))
        PaintDecoration(paint_info, TextDecorationLine::kLineThrough, fragment);
    }
  }
}

void SVGInlineTextBoxPainter::PaintSelectionBackground(
    const PaintInfo& paint_info) {
  auto layout_item = svg_inline_text_box_.GetLineLayoutItem();
  if (layout_item.StyleRef().Visibility() != EVisibility::kVisible)
    return;

  DCHECK(!layout_item.GetDocument().Printing());

  if (paint_info.phase == PaintPhase::kSelectionDragImage ||
      !ShouldPaintSelection(paint_info))
    return;

  Color background_color = HighlightPaintingUtils::HighlightBackgroundColor(
      layout_item.GetDocument(), layout_item.StyleRef(), layout_item.GetNode(),
      absl::nullopt, kPseudoIdSelection);
  if (!background_color.Alpha())
    return;

  LayoutSVGInlineText& text_layout_object = InlineText();
  if (!TextShouldBePainted(text_layout_object))
    return;

  const ComputedStyle& style =
      svg_inline_text_box_.Parent()->GetLineLayoutItem().StyleRef();

  int start_position, end_position;
  svg_inline_text_box_.SelectionStartEnd(start_position, end_position);

  const Vector<SVGTextFragmentWithRange> fragment_info_list =
      CollectFragmentsInRange(start_position, end_position);
  for (const SVGTextFragmentWithRange& fragment_with_range :
       fragment_info_list) {
    const SVGTextFragment& fragment = fragment_with_range.fragment;
    GraphicsContextStateSaver state_saver(paint_info.context);
    if (fragment.IsTransformed())
      paint_info.context.ConcatCTM(fragment.BuildFragmentTransform());

    paint_info.context.SetFillColor(background_color);
    paint_info.context.FillRect(
        svg_inline_text_box_.SelectionRectForTextFragment(
            fragment, fragment_with_range.start_position,
            fragment_with_range.end_position, style),
        background_color,
        PaintAutoDarkMode(style, DarkModeFilter::ElementRole::kSVG));
  }
}

static inline LayoutObject* FindLayoutObjectDefininingTextDecoration(
    InlineFlowBox* parent_box) {
  // Lookup first layout object in parent hierarchy which has text-decoration
  // set.
  LayoutObject* layout_object = nullptr;
  while (parent_box) {
    layout_object =
        LineLayoutAPIShim::LayoutObjectFrom(parent_box->GetLineLayoutItem());

    if (layout_object->Style() &&
        layout_object->StyleRef().GetTextDecorationLine() !=
            TextDecorationLine::kNone)
      break;

    parent_box = parent_box->Parent();
  }

  DCHECK(layout_object);
  return layout_object;
}

// Offset from the baseline for |decoration|. Positive offsets are above the
// baseline.
static inline float BaselineOffsetForDecoration(TextDecorationLine decoration,
                                                const FontMetrics& font_metrics,
                                                float thickness) {
  // FIXME: For SVG Fonts we need to use the attributes defined in the
  // <font-face> if specified.
  // Compatible with Batik/Presto.
  if (decoration == TextDecorationLine::kUnderline)
    return -thickness * 1.5f;
  if (decoration == TextDecorationLine::kOverline)
    return font_metrics.FloatAscent() - thickness;
  if (decoration == TextDecorationLine::kLineThrough)
    return font_metrics.FloatAscent() * 3 / 8.0f;

  NOTREACHED();
  return 0.0f;
}

static inline float ThicknessForDecoration(TextDecorationLine,
                                           const Font& font) {
  // FIXME: For SVG Fonts we need to use the attributes defined in the
  // <font-face> if specified.
  // Compatible with Batik/Presto
  return font.GetFontDescription().ComputedSize() / 20.0f;
}

void SVGInlineTextBoxPainter::PaintDecoration(const PaintInfo& paint_info,
                                              TextDecorationLine decoration,
                                              const SVGTextFragment& fragment) {
  if (svg_inline_text_box_.GetLineLayoutItem()
          .StyleRef()
          .TextDecorationsInEffect() == TextDecorationLine::kNone)
    return;

  if (fragment.width <= 0)
    return;

  // Find out which style defined the text-decoration, as its fill/stroke
  // properties have to be used for drawing instead of ours.
  LayoutObject* decoration_layout_object =
      FindLayoutObjectDefininingTextDecoration(svg_inline_text_box_.Parent());
  const ComputedStyle& decoration_style = decoration_layout_object->StyleRef();

  if (decoration_style.Visibility() != EVisibility::kVisible)
    return;

  float scaling_factor = 1;
  Font scaled_font;
  LayoutSVGInlineText::ComputeNewScaledFontForStyle(
      *decoration_layout_object, scaling_factor, scaled_font);
  DCHECK(scaling_factor);

  float thickness = ThicknessForDecoration(decoration, scaled_font);
  if (thickness <= 0)
    return;

  const SimpleFontData* font_data = scaled_font.PrimaryFont();
  DCHECK(font_data);
  if (!font_data)
    return;

  float decoration_offset = BaselineOffsetForDecoration(
      decoration, font_data->GetFontMetrics(), thickness);
  gfx::PointF decoration_origin(
      fragment.x, fragment.y - decoration_offset / scaling_factor);

  Path path;
  path.AddRect(
      gfx::RectF(decoration_origin,
                 gfx::SizeF(fragment.width, thickness / scaling_factor)));

  AutoDarkMode auto_dark_mode(
      PaintAutoDarkMode(decoration_style, DarkModeFilter::ElementRole::kSVG));

  for (int i = 0; i < 3; i++) {
    switch (decoration_style.PaintOrderType(i)) {
      case PT_FILL:
        if (decoration_style.HasFill()) {
          cc::PaintFlags fill_flags;
          if (!SVGObjectPainter(*decoration_layout_object)
                   .PreparePaint(paint_info.context,
                                 paint_info.IsRenderingClipPathAsMaskImage(),
                                 decoration_style, kApplyToFillMode,
                                 fill_flags)) {
            break;
          }
          fill_flags.setAntiAlias(true);
          paint_info.context.DrawPath(path.GetSkPath(), fill_flags,
                                      auto_dark_mode);
        }
        break;
      case PT_STROKE:
        if (decoration_style.HasVisibleStroke()) {
          cc::PaintFlags stroke_flags;
          if (!SVGObjectPainter(*decoration_layout_object)
                   .PreparePaint(paint_info.context,
                                 paint_info.IsRenderingClipPathAsMaskImage(),
                                 decoration_style, kApplyToStrokeMode,
                                 stroke_flags)) {
            break;
          }
          stroke_flags.setAntiAlias(true);
          float stroke_scale_factor = decoration_style.VectorEffect() ==
                                              EVectorEffect::kNonScalingStroke
                                          ? 1 / scaling_factor
                                          : 1;
          StrokeData stroke_data;
          SVGLayoutSupport::ApplyStrokeStyleToStrokeData(
              stroke_data, decoration_style, *decoration_layout_object,
              stroke_scale_factor);
          if (stroke_scale_factor != 1)
            stroke_data.SetThickness(stroke_data.Thickness() *
                                     stroke_scale_factor);
          stroke_data.SetupPaint(&stroke_flags);
          paint_info.context.DrawPath(path.GetSkPath(), stroke_flags,
                                      auto_dark_mode);
        }
        break;
      case PT_MARKERS:
        break;
      default:
        NOTREACHED();
    }
  }
}

bool SVGInlineTextBoxPainter::SetupTextPaint(
    const PaintInfo& paint_info,
    const ComputedStyle& style,
    LayoutSVGResourceMode resource_mode,
    cc::PaintFlags& flags,
    const AffineTransform* shader_transform) {
  LayoutSVGInlineText& text_layout_object = InlineText();

  float scaling_factor = text_layout_object.ScalingFactor();
  DCHECK(scaling_factor);

  absl::optional<AffineTransform> paint_server_transform;

  if (scaling_factor != 1 || shader_transform) {
    paint_server_transform.emplace();

    // Adjust the paint-server coordinate space.
    paint_server_transform->Scale(scaling_factor);

    if (shader_transform)
      paint_server_transform->Multiply(*shader_transform);
  }

  if (!SVGObjectPainter(ParentInlineLayoutObject())
           .PreparePaint(paint_info.context,
                         paint_info.IsRenderingClipPathAsMaskImage(), style,
                         resource_mode, flags,
                         base::OptionalOrNullptr(paint_server_transform))) {
    return false;
  }

  flags.setAntiAlias(true);

  if (style.TextShadow() &&
      // Text shadows are disabled when printing. http://crbug.com/258321
      !InlineLayoutObject().GetDocument().Printing()) {
    flags.setLooper(TextPainterBase::CreateDrawLooper(
        style.TextShadow(), DrawLooperBuilder::kShadowRespectsAlpha,
        style.VisitedDependentColor(GetCSSPropertyColor()),
        style.UsedColorScheme()));
  }

  if (resource_mode == kApplyToStrokeMode) {
    // The stroke geometry needs be generated based on the scaled font.
    float stroke_scale_factor =
        style.VectorEffect() != EVectorEffect::kNonScalingStroke
            ? scaling_factor
            : 1;
    StrokeData stroke_data;
    SVGLayoutSupport::ApplyStrokeStyleToStrokeData(
        stroke_data, style, ParentInlineLayoutObject(), stroke_scale_factor);
    if (stroke_scale_factor != 1)
      stroke_data.SetThickness(stroke_data.Thickness() * stroke_scale_factor);
    stroke_data.SetupPaint(&flags);
  }
  return true;
}

void SVGInlineTextBoxPainter::PaintText(const PaintInfo& paint_info,
                                        TextRun& text_run,
                                        const SVGTextFragment& fragment,
                                        int start_position,
                                        int end_position,
                                        const cc::PaintFlags& flags) {
  LayoutSVGInlineText& text_layout_object = InlineText();
  const Font& scaled_font = text_layout_object.ScaledFont();

  float scaling_factor = text_layout_object.ScalingFactor();
  DCHECK(scaling_factor);

  gfx::PointF text_origin(fragment.x, fragment.y);

  GraphicsContext& context = paint_info.context;
  GraphicsContextStateSaver state_saver(context, false);
  if (scaling_factor != 1) {
    text_origin.Scale(scaling_factor, scaling_factor);
    state_saver.Save();
    context.Scale(1 / scaling_factor, 1 / scaling_factor);
  }

  TextRunPaintInfo text_run_paint_info(text_run);
  text_run_paint_info.from = start_position;
  text_run_paint_info.to = end_position;

  context.DrawText(scaled_font, text_run_paint_info, text_origin, flags,
                   text_layout_object.EnsureNodeId(),
                   PaintAutoDarkMode(text_layout_object.StyleRef(),
                                     DarkModeFilter::ElementRole::kSVG));
  // TODO(npm): Check that there are non-whitespace characters. See
  // crbug.com/788444.
  context.GetPaintController().SetTextPainted();

  if (!scaled_font.ShouldSkipDrawing()) {
    PaintTiming& timing = PaintTiming::From(text_layout_object.GetDocument());
    timing.MarkFirstContentfulPaint();
    PaintTimingDetector::NotifyTextPaint(gfx::ToEnclosingRect(
        InlineLayoutObject().VisualRectInLocalSVGCoordinates()));
  }
}

namespace {

class SelectionStyleScope {
  STACK_ALLOCATED();

 public:
  SelectionStyleScope(LayoutObject&,
                      const ComputedStyle& style,
                      const ComputedStyle& selection_style);
  SelectionStyleScope(const SelectionStyleScope&) = delete;
  SelectionStyleScope& operator=(const SelectionStyleScope) = delete;
  ~SelectionStyleScope();

 private:
  LayoutObject& layout_object_;
  const ComputedStyle& selection_style_;
  const bool styles_are_equal_;
};

SelectionStyleScope::SelectionStyleScope(LayoutObject& layout_object,
                                         const ComputedStyle& style,
                                         const ComputedStyle& selection_style)
    : layout_object_(layout_object),
      selection_style_(selection_style),
      styles_are_equal_(style == selection_style) {
  if (styles_are_equal_)
    return;
  DCHECK(IsA<SVGElement>(layout_object.GetNode()) &&
         !layout_object.IsSVGInlineText());
  auto& element = To<SVGElement>(*layout_object_.GetNode());
  SVGResources::UpdatePaints(element, nullptr, selection_style_);
}

SelectionStyleScope::~SelectionStyleScope() {
  if (styles_are_equal_)
    return;
  auto& element = To<SVGElement>(*layout_object_.GetNode());
  SVGResources::ClearPaints(element, &selection_style_);
}

}  // namespace

void SVGInlineTextBoxPainter::PaintText(
    const PaintInfo& paint_info,
    const ComputedStyle& style,
    const ComputedStyle& selection_style,
    const SVGTextFragment& fragment,
    LayoutSVGResourceMode resource_mode,
    bool should_paint_selection,
    const AffineTransform* shader_transform) {
  int start_position = 0;
  int end_position = 0;
  if (should_paint_selection) {
    svg_inline_text_box_.SelectionStartEnd(start_position, end_position);
    should_paint_selection =
        svg_inline_text_box_.MapStartEndPositionsIntoFragmentCoordinates(
            fragment, start_position, end_position);
  }

  // Fast path if there is no selection, just draw the whole chunk part using
  // the regular style.
  TextRun text_run = svg_inline_text_box_.ConstructTextRun(style, fragment);
  if (!should_paint_selection || start_position >= end_position) {
    cc::PaintFlags flags;
    if (SetupTextPaint(paint_info, style, resource_mode, flags,
                       shader_transform))
      PaintText(paint_info, text_run, fragment, 0, fragment.length, flags);
    return;
  }

  // Eventually draw text using regular style until the start position of the
  // selection.
  bool paint_selected_text_only =
      paint_info.phase == PaintPhase::kSelectionDragImage;
  if (start_position > 0 && !paint_selected_text_only) {
    cc::PaintFlags flags;
    if (SetupTextPaint(paint_info, style, resource_mode, flags,
                       shader_transform))
      PaintText(paint_info, text_run, fragment, 0, start_position, flags);
  }

  // Draw text using selection style from the start to the end position of the
  // selection.
  {
    SelectionStyleScope scope(ParentInlineLayoutObject(), style,
                              selection_style);
    cc::PaintFlags flags;
    if (SetupTextPaint(paint_info, selection_style, resource_mode, flags,
                       shader_transform)) {
      PaintText(paint_info, text_run, fragment, start_position, end_position,
                flags);
    }
  }

  // Eventually draw text using regular style from the end position of the
  // selection to the end of the current chunk part.
  if (end_position < static_cast<int>(fragment.length) &&
      !paint_selected_text_only) {
    cc::PaintFlags flags;
    if (SetupTextPaint(paint_info, style, resource_mode, flags,
                       shader_transform)) {
      PaintText(paint_info, text_run, fragment, end_position, fragment.length,
                flags);
    }
  }
}

Vector<SVGTextFragmentWithRange> SVGInlineTextBoxPainter::CollectTextMatches(
    const DocumentMarker& marker) const {
  const Vector<SVGTextFragmentWithRange> empty_text_match_list;

  // SVG does not support grammar or spellcheck markers, so skip anything but
  // TextFragmentMarker and TextMatchMarker types.
  if (marker.GetType() != DocumentMarker::kTextMatch &&
      marker.GetType() != DocumentMarker::kTextFragment)
    return empty_text_match_list;

  if (!InlineLayoutObject()
           .GetFrame()
           ->GetEditor()
           .MarkedTextMatchesAreHighlighted())
    return empty_text_match_list;

  int marker_start_position =
      std::max<int>(marker.StartOffset() - svg_inline_text_box_.Start(), 0);
  int marker_end_position =
      std::min<int>(marker.EndOffset() - svg_inline_text_box_.Start(),
                    svg_inline_text_box_.Len());

  if (marker_start_position >= marker_end_position)
    return empty_text_match_list;

  return CollectFragmentsInRange(marker_start_position, marker_end_position);
}

Vector<SVGTextFragmentWithRange>
SVGInlineTextBoxPainter::CollectFragmentsInRange(int start_position,
                                                 int end_position) const {
  Vector<SVGTextFragmentWithRange> fragment_info_list;
  for (const SVGTextFragment& fragment : svg_inline_text_box_.TextFragments()) {
    // TODO(ramya.v): If these can't be negative we should use unsigned.
    int fragment_start_position = start_position;
    int fragment_end_position = end_position;
    if (!svg_inline_text_box_.MapStartEndPositionsIntoFragmentCoordinates(
            fragment, fragment_start_position, fragment_end_position))
      continue;

    fragment_info_list.push_back(SVGTextFragmentWithRange(
        fragment, fragment_start_position, fragment_end_position));
  }
  return fragment_info_list;
}

void SVGInlineTextBoxPainter::PaintTextMarkerForeground(
    const PaintInfo& paint_info,
    const PhysicalOffset& point,
    const DocumentMarker& marker,
    const ComputedStyle& style,
    const Font& font) {
  const Vector<SVGTextFragmentWithRange> text_match_info_list =
      CollectTextMatches(marker);
  if (text_match_info_list.IsEmpty())
    return;

  Color text_color = LayoutTheme::GetTheme().PlatformTextSearchColor(
      marker.GetType() == DocumentMarker::kTextMatch
          ? To<TextMatchMarker>(marker).IsActiveMatch()
          : false,
      style.UsedColorScheme());

  cc::PaintFlags fill_flags;
  fill_flags.setColor(text_color.Rgb());
  fill_flags.setAntiAlias(true);

  cc::PaintFlags stroke_flags;
  bool should_paint_stroke = false;
  if (SetupTextPaint(paint_info, style, kApplyToStrokeMode, stroke_flags,
                     nullptr)) {
    should_paint_stroke = true;
    stroke_flags.setLooper(nullptr);
    stroke_flags.setColor(text_color.Rgb());
  }

  for (const SVGTextFragmentWithRange& text_match_info : text_match_info_list) {
    const SVGTextFragment& fragment = text_match_info.fragment;
    GraphicsContextStateSaver state_saver(paint_info.context);
    if (fragment.IsTransformed())
      paint_info.context.ConcatCTM(fragment.BuildFragmentTransform());

    TextRun text_run = svg_inline_text_box_.ConstructTextRun(style, fragment);
    PaintText(paint_info, text_run, fragment, text_match_info.start_position,
              text_match_info.end_position, fill_flags);
    if (should_paint_stroke) {
      PaintText(paint_info, text_run, fragment, text_match_info.start_position,
                text_match_info.end_position, stroke_flags);
    }
  }
}

void SVGInlineTextBoxPainter::PaintTextMarkerBackground(
    const PaintInfo& paint_info,
    const PhysicalOffset& point,
    const DocumentMarker& marker,
    const ComputedStyle& style,
    const Font& font) {
  const Vector<SVGTextFragmentWithRange> text_match_info_list =
      CollectTextMatches(marker);
  if (text_match_info_list.IsEmpty())
    return;

  Color color = LayoutTheme::GetTheme().PlatformTextSearchHighlightColor(
      marker.GetType() == DocumentMarker::kTextMatch
          ? To<TextMatchMarker>(marker).IsActiveMatch()
          : false,
      style.UsedColorScheme());
  for (const SVGTextFragmentWithRange& text_match_info : text_match_info_list) {
    const SVGTextFragment& fragment = text_match_info.fragment;

    GraphicsContextStateSaver state_saver(paint_info.context, false);
    if (fragment.IsTransformed()) {
      state_saver.Save();
      paint_info.context.ConcatCTM(fragment.BuildFragmentTransform());
    }
    gfx::RectF fragment_rect =
        svg_inline_text_box_.SelectionRectForTextFragment(
            fragment, text_match_info.start_position,
            text_match_info.end_position, style);
    paint_info.context.SetFillColor(color);
    paint_info.context.FillRect(
        fragment_rect,
        PaintAutoDarkMode(style, DarkModeFilter::ElementRole::kSVG));
  }
}

void SVGInlineTextBoxPainter::RecordSelectionBoundsForRange(
    int start_position,
    int end_position,
    SelectionState selection_state,
    const ComputedStyle& style,
    PaintController& paint_controller,
    absl::optional<SelectionBoundsRecorder>& bounds_recorder) {
  const Vector<SVGTextFragmentWithRange> fragment_info_list =
      CollectFragmentsInRange(start_position, end_position);
  // We expect at most single fragment for which to record the selection rect.
  // There can be no fragments when the identified selection position is at the
  // end of an SVGInlineTextBox (selection_state is still kStart, but no
  // selection is painted).
  DCHECK_LE(fragment_info_list.size(), 1u);
  if (fragment_info_list.size()) {
    const SVGTextFragmentWithRange& fragment_with_range = fragment_info_list[0];
    const SVGTextFragment& fragment = fragment_with_range.fragment;
    PhysicalRect selection_rect = PhysicalRect::EnclosingRect(
        svg_inline_text_box_.SelectionRectForTextFragment(
            fragment, fragment_with_range.start_position,
            fragment_with_range.end_position, style));
    TextDirection direction = svg_inline_text_box_.IsLeftToRightDirection()
                                  ? TextDirection::kLtr
                                  : TextDirection::kRtl;
    bounds_recorder.emplace(selection_state, selection_rect, paint_controller,
                            direction, style.GetWritingMode(),
                            InlineLayoutObject());
  }
}

}  // namespace blink
