// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/paint/text_painter_base.h"

#include "base/containers/adapters.h"
#include "third_party/blink/renderer/core/css/properties/longhands.h"
#include "third_party/blink/renderer/core/highlight/highlight_style_utils.h"
#include "third_party/blink/renderer/core/paint/applied_decoration_painter.h"
#include "third_party/blink/renderer/core/paint/box_painter_base.h"
#include "third_party/blink/renderer/core/paint/text_decoration_info.h"
#include "third_party/blink/renderer/core/style/computed_style.h"
#include "third_party/blink/renderer/core/style/shadow_list.h"
#include "third_party/blink/renderer/platform/graphics/graphics_context.h"
#include "third_party/blink/renderer/platform/graphics/graphics_context_state_saver.h"

namespace blink {

TextPainterBase::TextPainterBase(GraphicsContext& context,
                                 const Font& font,
                                 const LineRelativeOffset& text_origin,
                                 InlinePaintContext* inline_context,
                                 bool horizontal)
    : inline_context_(inline_context),
      graphics_context_(context),
      font_(font),
      text_origin_(text_origin),
      horizontal_(horizontal) {}

TextPainterBase::~TextPainterBase() = default;

void TextPainterBase::SetEmphasisMark(const AtomicString& emphasis_mark,
                                      TextEmphasisPosition position) {
  emphasis_mark_ = emphasis_mark;
  const SimpleFontData* font_data = font_.PrimaryFont();
  DCHECK(font_data);

  if (!font_data || emphasis_mark.IsNull()) {
    emphasis_mark_offset_ = 0;
  } else if ((horizontal_ && IsOver(position)) ||
             (!horizontal_ && IsRight(position))) {
    emphasis_mark_offset_ = -font_data->GetFontMetrics().Ascent() -
                            font_.EmphasisMarkDescent(emphasis_mark);
  } else {
    DCHECK(!IsOver(position) || position == TextEmphasisPosition::kOverLeft);
    emphasis_mark_offset_ = font_data->GetFontMetrics().Descent() +
                            font_.EmphasisMarkAscent(emphasis_mark);
  }
}

// static
void TextPainterBase::UpdateGraphicsContext(
    GraphicsContext& context,
    const TextPaintStyle& text_style,
    GraphicsContextStateSaver& state_saver,
    ShadowMode shadow_mode) {
  TextDrawingModeFlags mode = context.TextDrawingMode();
  if (text_style.stroke_width > 0) {
    TextDrawingModeFlags new_mode = mode | kTextModeStroke;
    if (mode != new_mode) {
      state_saver.SaveIfNeeded();
      context.SetTextDrawingMode(new_mode);
      mode = new_mode;
    }
  }

  if (mode & kTextModeFill && text_style.fill_color != context.FillColor())
    context.SetFillColor(text_style.fill_color);

  if (mode & kTextModeStroke) {
    if (text_style.stroke_color != context.StrokeColor())
      context.SetStrokeColor(text_style.stroke_color);
    if (text_style.stroke_width != context.StrokeThickness())
      context.SetStrokeThickness(text_style.stroke_width);
  }

  if (shadow_mode != kTextProperOnly) {
    DCHECK(shadow_mode == kBothShadowsAndTextProper ||
           shadow_mode == kShadowsOnly);

    // If there are shadows, we definitely need an SkDrawLooper, but if there
    // are no shadows (nullptr), we still need one iff we’re in kShadowsOnly
    // mode, because we suppress text proper by omitting AddUnmodifiedContent
    // when building a looper (cf. CRC2DState::ShadowAndForegroundDrawLooper).
    if (text_style.shadow || shadow_mode == kShadowsOnly) {
      state_saver.SaveIfNeeded();
      context.SetDrawLooper(CreateDrawLooper(
          text_style.shadow.get(), DrawLooperBuilder::kShadowIgnoresAlpha,
          text_style.current_color, text_style.color_scheme, shadow_mode));
    }
  }
}

// static
sk_sp<SkDrawLooper> TextPainterBase::CreateDrawLooper(
    const ShadowList* shadow_list,
    DrawLooperBuilder::ShadowAlphaMode alpha_mode,
    const Color& current_color,
    mojom::blink::ColorScheme color_scheme,
    ShadowMode shadow_mode) {
  DrawLooperBuilder draw_looper_builder;

  // ShadowList nullptr means there are no shadows.
  if (shadow_mode != kTextProperOnly && shadow_list) {
    for (wtf_size_t i = shadow_list->Shadows().size(); i--;) {
      const ShadowData& shadow = shadow_list->Shadows()[i];
      draw_looper_builder.AddShadow(
          shadow.Offset(), shadow.Blur(),
          shadow.GetColor().Resolve(current_color, color_scheme),
          DrawLooperBuilder::kShadowRespectsTransforms, alpha_mode);
    }
  }
  if (shadow_mode != kShadowsOnly)
    draw_looper_builder.AddUnmodifiedContent();
  return draw_looper_builder.DetachDrawLooper();
}

Color TextPainterBase::TextColorForWhiteBackground(Color text_color) {
  int distance_from_white = DifferenceSquared(text_color, Color::kWhite);
  // semi-arbitrarily chose 65025 (255^2) value here after a few tests;
  return distance_from_white > 65025 ? text_color : text_color.Dark();
}

// static
TextPaintStyle TextPainterBase::TextPaintingStyle(const Document& document,
                                                  const ComputedStyle& style,
                                                  const PaintInfo& paint_info) {
  TextPaintStyle text_style;
  text_style.stroke_width = style.TextStrokeWidth();
  text_style.color_scheme = style.UsedColorScheme();

  if (paint_info.phase == PaintPhase::kTextClip) {
    // When we use the text as a clip, we only care about the alpha, thus we
    // make all the colors black.
    text_style.current_color = Color::kBlack;
    text_style.fill_color = Color::kBlack;
    text_style.stroke_color = Color::kBlack;
    text_style.emphasis_mark_color = Color::kBlack;
    text_style.shadow = nullptr;
  } else {
    text_style.current_color =
        style.VisitedDependentColorFast(GetCSSPropertyColor());
    text_style.fill_color =
        style.VisitedDependentColorFast(GetCSSPropertyWebkitTextFillColor());
    text_style.stroke_color =
        style.VisitedDependentColorFast(GetCSSPropertyWebkitTextStrokeColor());
    text_style.emphasis_mark_color =
        style.VisitedDependentColorFast(GetCSSPropertyTextEmphasisColor());
    text_style.shadow = style.TextShadow();

    // Adjust text color when printing with a white background.
    bool force_background_to_white =
        BoxPainterBase::ShouldForceWhiteBackgroundForPrintEconomy(document,
                                                                  style);
    if (force_background_to_white) {
      text_style.fill_color =
          TextColorForWhiteBackground(text_style.fill_color);
      text_style.stroke_color =
          TextColorForWhiteBackground(text_style.stroke_color);
      text_style.emphasis_mark_color =
          TextColorForWhiteBackground(text_style.emphasis_mark_color);
    }
  }

  return text_style;
}

TextPaintStyle TextPainterBase::SelectionPaintingStyle(
    const Document& document,
    const ComputedStyle& style,
    Node* node,
    const PaintInfo& paint_info,
    const TextPaintStyle& text_style) {
  return HighlightStyleUtils::HighlightPaintingStyle(
      document, style, node, kPseudoIdSelection, text_style, paint_info);
}

void TextPainterBase::DecorationsStripeIntercepts(
    float upper,
    float stripe_width,
    float dilation,
    const Vector<Font::TextIntercept>& text_intercepts) {
  for (auto intercept : text_intercepts) {
    gfx::PointF clip_origin(text_origin_);
    gfx::RectF clip_rect(
        clip_origin + gfx::Vector2dF(intercept.begin_, upper),
        gfx::SizeF(intercept.end_ - intercept.begin_, stripe_width));
    // We need to ensure the clip rectangle is covering the full underline
    // extent. For horizontal drawing, using enclosingIntRect would be
    // sufficient, since we can clamp to full device pixels that way. However,
    // for vertical drawing, we have a transformation applied, which breaks the
    // integers-equal-device pixels assumption, so vertically inflating by 1
    // pixel makes sure we're always covering. This should only be done on the
    // clipping rectangle, not when computing the glyph intersects.
    clip_rect.Outset(gfx::OutsetsF::VH(1.0, dilation));

    if (!gfx::RectFToSkRect(clip_rect).isFinite())
      continue;
    graphics_context_.ClipOut(clip_rect);
  }
}

void TextPainterBase::PaintDecorationsOnlyLineThrough(
    TextDecorationInfo& decoration_info,
    const TextPaintStyle& text_style,
    const cc::PaintFlags* flags) {
  // Updating the graphics context and looping through applied decorations is
  // expensive, so avoid doing it if there are no ‘line-through’ decorations.
  if (!decoration_info.HasAnyLine(TextDecorationLine::kLineThrough))
    return;

  GraphicsContextStateSaver state_saver(graphics_context_);
  UpdateGraphicsContext(text_style, state_saver);

  for (wtf_size_t applied_decoration_index = 0;
       applied_decoration_index < decoration_info.AppliedDecorationCount();
       ++applied_decoration_index) {
    const AppliedTextDecoration& decoration =
        decoration_info.AppliedDecoration(applied_decoration_index);
    TextDecorationLine lines = decoration.Lines();
    if (EnumHasFlags(lines, TextDecorationLine::kLineThrough)) {
      decoration_info.SetDecorationIndex(applied_decoration_index);

      const float resolved_thickness = decoration_info.ResolvedThickness();
      graphics_context_.SetStrokeThickness(resolved_thickness);
      decoration_info.SetLineThroughLineData();
      AppliedDecorationPainter decoration_painter(graphics_context_,
                                                  decoration_info);
      // No skip: ink for line-through,
      // compare https://github.com/w3c/csswg-drafts/issues/711
      decoration_painter.Paint(flags);
    }
  }
}

// We have two functions to paint text decorations, because we should paint
// text and decorations in following order:
//   1. Paint underline or overline text decorations
//   2. Paint text
//   3. Paint line through
void TextPainterBase::PaintUnderOrOverLineDecorations(
    const TextFragmentPaintInfo& fragment_paint_info,
    const TextDecorationOffset& decoration_offset,
    TextDecorationInfo& decoration_info,
    TextDecorationLine lines_to_paint,
    const TextPaintStyle& text_style,
    const cc::PaintFlags* flags) {
  // Updating the graphics context and looping through applied decorations is
  // expensive, so avoid doing it if there are no decorations of the given
  // |lines_to_paint|, or the only decoration was a ‘line-through’.
  if (!decoration_info.HasAnyLine(lines_to_paint &
                                  ~TextDecorationLine::kLineThrough)) {
    return;
  }

  GraphicsContextStateSaver state_saver(graphics_context_);

  // Updating Graphics Context for text only (kTextProperOnly),
  // instead of the default text and shadows (kBothShadowsAndTextProper),
  // because shadows will be painted by
  // TextPainterBase::PaintUnderOrOverLineDecorationShadows.
  UpdateGraphicsContext(graphics_context_, text_style, state_saver,
                        ShadowMode::kTextProperOnly);

  PaintUnderOrOverLineDecorationShadows(fragment_paint_info, decoration_offset,
                                        decoration_info, lines_to_paint, flags,
                                        text_style);

  PaintUnderOrOverLineDecorations(fragment_paint_info, decoration_offset,
                                  decoration_info, lines_to_paint, flags);
}

void TextPainterBase::PaintUnderOrOverLineDecorationShadows(
    const TextFragmentPaintInfo& fragment_paint_info,
    const TextDecorationOffset& decoration_offset,
    TextDecorationInfo& decoration_info,
    TextDecorationLine lines_to_paint,
    const cc::PaintFlags* flags,
    const TextPaintStyle& text_style) {
  const ShadowList* shadow_list = text_style.shadow.get();
  if (!shadow_list) {
    return;
  }

  for (const auto& shadow : base::Reversed(shadow_list->Shadows())) {
    const Color& color = shadow.GetColor().Resolve(text_style.current_color,
                                                   text_style.color_scheme);
    // Detect when there's no effective shadow.
    if (color.IsFullyTransparent()) {
      continue;
    }

    const gfx::Vector2dF& offset = shadow.Offset();

    float blur = shadow.Blur();
    DCHECK_GE(blur, 0);
    const auto sigma = BlurRadiusToStdDev(blur);

    graphics_context_.BeginLayer(sk_make_sp<DropShadowPaintFilter>(
        offset.x(), offset.y(), sigma, sigma, color.toSkColor4f(),
        DropShadowPaintFilter::ShadowMode::kDrawShadowOnly, nullptr));

    PaintUnderOrOverLineDecorations(fragment_paint_info, decoration_offset,
                                    decoration_info, lines_to_paint, flags);

    graphics_context_.EndLayer();
  }
}

void TextPainterBase::PaintUnderOrOverLineDecorations(
    const TextFragmentPaintInfo& fragment_paint_info,
    const TextDecorationOffset& decoration_offset,
    TextDecorationInfo& decoration_info,
    TextDecorationLine lines_to_paint,
    const cc::PaintFlags* flags) {
  for (wtf_size_t i = 0; i < decoration_info.AppliedDecorationCount(); i++) {
    decoration_info.SetDecorationIndex(i);
    graphics_context_.SetStrokeThickness(decoration_info.ResolvedThickness());

    if (decoration_info.HasSpellingOrGrammerError() &&
        EnumHasFlags(lines_to_paint, TextDecorationLine::kSpellingError |
                                         TextDecorationLine::kGrammarError)) {
      decoration_info.SetSpellingOrGrammarErrorLineData(decoration_offset);
      // We ignore "text-decoration-skip-ink: auto" for spelling and grammar
      // error markers.
      AppliedDecorationPainter decoration_painter(graphics_context_,
                                                  decoration_info);
      decoration_painter.Paint(flags);
      continue;
    }

    if (decoration_info.HasUnderline() && decoration_info.FontData() &&
        EnumHasFlags(lines_to_paint, TextDecorationLine::kUnderline)) {
      decoration_info.SetUnderlineLineData(decoration_offset);
      PaintDecorationUnderOrOverLine(fragment_paint_info, decoration_info,
                                     TextDecorationLine::kUnderline, flags);
    }

    if (decoration_info.HasOverline() && decoration_info.FontData() &&
        EnumHasFlags(lines_to_paint, TextDecorationLine::kOverline)) {
      decoration_info.SetOverlineLineData(decoration_offset);
      PaintDecorationUnderOrOverLine(fragment_paint_info, decoration_info,
                                     TextDecorationLine::kOverline, flags);
    }
  }
}

void TextPainterBase::PaintDecorationUnderOrOverLine(
    const TextFragmentPaintInfo& fragment_paint_info,
    TextDecorationInfo& decoration_info,
    TextDecorationLine line,
    const cc::PaintFlags* flags) {
  AppliedDecorationPainter decoration_painter(graphics_context_,
                                              decoration_info);
  if (decoration_info.TargetStyle().TextDecorationSkipInk() ==
      ETextDecorationSkipInk::kAuto) {
    // In order to ignore intersects less than 0.5px, inflate by -0.5.
    gfx::RectF decoration_bounds = decoration_info.Bounds();
    decoration_bounds.Inset(gfx::InsetsF::VH(0.5, 0));
    ClipDecorationsStripe(
        fragment_paint_info,
        decoration_info.InkSkipClipUpper(decoration_bounds.y()),
        decoration_bounds.height(),
        std::min(decoration_info.ResolvedThickness(),
                 kDecorationClipMaxDilation));
  }
  decoration_painter.Paint(flags);
}

}  // namespace blink
