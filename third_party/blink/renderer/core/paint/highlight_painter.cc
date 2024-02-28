// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/paint/highlight_painter.h"

#include "third_party/blink/renderer/core/dom/node.h"
#include "third_party/blink/renderer/core/editing/editor.h"
#include "third_party/blink/renderer/core/editing/frame_selection.h"
#include "third_party/blink/renderer/core/editing/markers/custom_highlight_marker.h"
#include "third_party/blink/renderer/core/editing/markers/document_marker_controller.h"
#include "third_party/blink/renderer/core/editing/markers/styleable_marker.h"
#include "third_party/blink/renderer/core/editing/markers/text_match_marker.h"
#include "third_party/blink/renderer/core/frame/local_dom_window.h"
#include "third_party/blink/renderer/core/frame/local_frame.h"
#include "third_party/blink/renderer/core/highlight/highlight.h"
#include "third_party/blink/renderer/core/highlight/highlight_registry.h"
#include "third_party/blink/renderer/core/highlight/highlight_style_utils.h"
#include "third_party/blink/renderer/core/layout/inline/inline_cursor.h"
#include "third_party/blink/renderer/core/layout/inline/text_offset_range.h"
#include "third_party/blink/renderer/core/layout/layout_object.h"
#include "third_party/blink/renderer/core/layout/svg/layout_svg_inline_text.h"
#include "third_party/blink/renderer/core/paint/document_marker_painter.h"
#include "third_party/blink/renderer/core/paint/line_relative_rect.h"
#include "third_party/blink/renderer/core/paint/marker_range_mapping_context.h"
#include "third_party/blink/renderer/core/paint/highlight_overlay.h"
#include "third_party/blink/renderer/core/paint/text_decoration_painter.h"
#include "third_party/blink/renderer/core/paint/text_painter.h"
#include "third_party/blink/renderer/core/paint/paint_auto_dark_mode.h"
#include "third_party/blink/renderer/core/paint/paint_info.h"
#include "third_party/blink/renderer/platform/graphics/graphics_context_state_saver.h"
#include "third_party/blink/renderer/platform/instrumentation/use_counter.h"

namespace blink {

namespace {

using HighlightLayerType = HighlightOverlay::HighlightLayerType;
using HighlightLayer = HighlightOverlay::HighlightLayer;
using HighlightRange = HighlightOverlay::HighlightRange;
using HighlightEdge = HighlightOverlay::HighlightEdge;
using HighlightDecoration = HighlightOverlay::HighlightDecoration;
using HighlightPart = HighlightOverlay::HighlightPart;

LineRelativeRect LineRelativeLocalRect(const FragmentItem& text_fragment,
                                       StringView text,
                                       unsigned start_offset,
                                       unsigned end_offset) {
  LayoutUnit start_position, end_position;
  std::tie(start_position, end_position) =
      text_fragment.LineLeftAndRightForOffsets(text, start_offset, end_offset);

  const LayoutUnit height = text_fragment.InkOverflowRect().Height();
  return {{start_position, LayoutUnit()},
          {end_position - start_position, height}};
}

void PaintRect(GraphicsContext& context,
               const PhysicalRect& rect,
               const Color color,
               const AutoDarkMode& auto_dark_mode) {
  if (color.IsFullyTransparent()) {
    return;
  }
  if (rect.size.IsEmpty())
    return;
  const gfx::Rect pixel_snapped_rect = ToPixelSnappedRect(rect);
  if (!pixel_snapped_rect.IsEmpty())
    context.FillRect(pixel_snapped_rect, color, auto_dark_mode);
}

void PaintRect(GraphicsContext& context,
               const PhysicalOffset& location,
               const PhysicalRect& rect,
               const Color color,
               const AutoDarkMode& auto_dark_mode) {
  PaintRect(context, PhysicalRect(rect.offset + location, rect.size), color,
            auto_dark_mode);
}

const HighlightRegistry* GetHighlightRegistry(const Node* node) {
  if (!node)
    return nullptr;
  return node->GetDocument()
      .domWindow()
      ->Supplementable<LocalDOMWindow>::RequireSupplement<HighlightRegistry>();
}

const LayoutSelectionStatus* GetSelectionStatus(
    const HighlightPainter::SelectionPaintState* selection) {
  if (!selection)
    return nullptr;
  return &selection->Status();
}

// Returns true if the styles for the given spelling or grammar pseudo require
// the full overlay painting algorithm.
bool HasNonTrivialSpellingGrammarStyles(const FragmentItem& fragment_item,
                                        Node* node,
                                        const ComputedStyle& originating_style,
                                        PseudoId pseudo) {
  DCHECK(pseudo == kPseudoIdSpellingError || pseudo == kPseudoIdGrammarError);
  if (const ComputedStyle* pseudo_style =
          HighlightStyleUtils::HighlightPseudoStyle(node, originating_style,
                                                    pseudo)) {
    const Document& document = node->GetDocument();
    // If the ‘color’, ‘-webkit-text-fill-color’, ‘-webkit-text-stroke-color’,
    // or ‘-webkit-text-stroke-width’ differs from the originating style.
    Color pseudo_color = HighlightStyleUtils::ResolveColor(
        document, originating_style, pseudo_style, pseudo,
        GetCSSPropertyColor(), {});
    if (pseudo_color !=
        originating_style.VisitedDependentColor(GetCSSPropertyColor())) {
      return true;
    }
    if (HighlightStyleUtils::ResolveColor(
            document, originating_style, pseudo_style, pseudo,
            GetCSSPropertyWebkitTextFillColor(), {}) !=
        originating_style.VisitedDependentColor(
            GetCSSPropertyWebkitTextFillColor())) {
      return true;
    }
    if (HighlightStyleUtils::ResolveColor(
            document, originating_style, pseudo_style, pseudo,
            GetCSSPropertyWebkitTextStrokeColor(), {}) !=
        originating_style.VisitedDependentColor(
            GetCSSPropertyWebkitTextStrokeColor())) {
      return true;
    }
    if (pseudo_style->TextStrokeWidth() != originating_style.TextStrokeWidth())
      return true;
    // If there is a background color.
    if (!HighlightStyleUtils::ResolveColor(document, originating_style,
                                           pseudo_style, pseudo,
                                           GetCSSPropertyBackgroundColor(), {})
             .IsFullyTransparent()) {
      return true;
    }
    // If the ‘text-shadow’ is not ‘none’.
    if (pseudo_style->TextShadow())
      return true;

    // If the ‘text-decoration-line’ is not ‘spelling-error’ or ‘grammar-error’,
    // depending on the pseudo. ‘text-decoration-color’ can vary without hurting
    // the optimisation, and for these line types, we ignore all other text
    // decoration related properties anyway.
    if (pseudo_style->TextDecorationsInEffect() !=
        (pseudo == kPseudoIdSpellingError
             ? TextDecorationLine::kSpellingError
             : TextDecorationLine::kGrammarError)) {
      return true;
    }
    // If any of the originating line decorations would need to be recolored.
    for (const AppliedTextDecoration& decoration :
         originating_style.AppliedTextDecorations()) {
      if (decoration.GetColor() != pseudo_color) {
        return true;
      }
    }
    // ‘text-emphasis-color’ should be meaningless for highlight pseudos, but
    // in our current impl, it sets the color of originating emphasis marks.
    // This means we can only use kFastSpellingGrammar if the color is the same
    // as in the originating style, or there are no emphasis marks.
    // TODO(crbug.com/1147859) clean up when spec issue is resolved again
    // https://github.com/w3c/csswg-drafts/issues/7101
    if (originating_style.GetTextEmphasisMark() != TextEmphasisMark::kNone &&
        HighlightStyleUtils::ResolveColor(
            document, originating_style, pseudo_style, pseudo,
            GetCSSPropertyTextEmphasisColor(), {}) !=
            originating_style.VisitedDependentColor(
                GetCSSPropertyTextEmphasisColor())) {
      return true;
    }
    // If the SVG-only fill- and stroke-related properties differ from their
    // values in the originating style. These checks must be skipped outside of
    // SVG content, because the initial ‘fill’ is ‘black’, not ‘currentColor’.
    if (fragment_item.IsSvgText()) {
      // If the ‘fill’ is ‘currentColor’, assume that it differs from the
      // originating style, even if the current color actually happens to
      // match. This simplifies the logic until we know it performs poorly.
      if (pseudo_style->FillPaint().HasCurrentColor())
        return true;
      // If the ‘fill’ differs from the originating style.
      if (pseudo_style->FillPaint() != originating_style.FillPaint())
        return true;
      // If the ‘stroke’ is ‘currentColor’, assume that it differs from the
      // originating style, even if the current color actually happens to
      // match. This simplifies the logic until we know it performs poorly.
      if (pseudo_style->StrokePaint().HasCurrentColor())
        return true;
      // If the ‘stroke’ differs from the originating style.
      if (pseudo_style->StrokePaint() != originating_style.StrokePaint())
        return true;
      // If the ‘stroke-width’ differs from the originating style.
      if (pseudo_style->StrokeWidth() != originating_style.StrokeWidth())
        return true;
    }
  }
  return false;
}

}  // namespace

HighlightPainter::SelectionPaintState::SelectionPaintState(
    const InlineCursor& containing_block,
    const PhysicalOffset& box_offset,
    const absl::optional<AffineTransform> writing_mode_rotation)
    : SelectionPaintState(containing_block,
                          box_offset,
                          writing_mode_rotation,
                          containing_block.Current()
                              .GetLayoutObject()
                              ->GetDocument()
                              .GetFrame()
                              ->Selection()) {}
HighlightPainter::SelectionPaintState::SelectionPaintState(
    const InlineCursor& containing_block,
    const PhysicalOffset& box_offset,
    const absl::optional<AffineTransform> writing_mode_rotation,
    const FrameSelection& frame_selection)
    : selection_status_(
          frame_selection.ComputeLayoutSelectionStatus(containing_block)),
      state_(frame_selection.ComputePaintingSelectionStateForCursor(
          containing_block.Current())),
      containing_block_(containing_block),
      box_offset_(box_offset),
      writing_mode_rotation_(writing_mode_rotation) {}

void HighlightPainter::SelectionPaintState::ComputeSelectionStyle(
    const Document& document,
    const ComputedStyle& style,
    Node* node,
    const PaintInfo& paint_info,
    const TextPaintStyle& text_style) {
  selection_style_ = TextPainterBase::SelectionPaintingStyle(
      document, style, node, paint_info, text_style);
  paint_selected_text_only_ =
      (paint_info.phase == PaintPhase::kSelectionDragImage);
}

void HighlightPainter::SelectionPaintState::ComputeSelectionRectIfNeeded() {
  if (!selection_rect_) {
    PhysicalRect physical =
        containing_block_.CurrentLocalSelectionRectForText(selection_status_);
    physical.offset += box_offset_;
    LineRelativeRect rotated =
        LineRelativeRect::Create(physical, writing_mode_rotation_);
    selection_rect_.emplace(SelectionRect{physical, rotated});
  }
}

const PhysicalRect&
HighlightPainter::SelectionPaintState::PhysicalSelectionRect() {
  ComputeSelectionRectIfNeeded();
  return selection_rect_->physical;
}

const LineRelativeRect&
HighlightPainter::SelectionPaintState::LineRelativeSelectionRect() {
  ComputeSelectionRectIfNeeded();
  return selection_rect_->rotated;
}

// |selection_start| and |selection_end| should be between
// [text_fragment.StartOffset(), text_fragment.EndOffset()].
void HighlightPainter::SelectionPaintState::PaintSelectionBackground(
    GraphicsContext& context,
    Node* node,
    const Document& document,
    const ComputedStyle& style,
    const absl::optional<AffineTransform>& rotation) {
  const Color color = HighlightStyleUtils::HighlightBackgroundColor(
      document, style, node, selection_style_.current_color,
      kPseudoIdSelection);
  HighlightPainter::PaintHighlightBackground(context, style, color,
                                             PhysicalSelectionRect(), rotation);
}

// Paint the selected text only.
void HighlightPainter::SelectionPaintState::PaintSelectedText(
    TextPainter& text_painter,
    const TextFragmentPaintInfo& fragment_paint_info,
    const TextPaintStyle& text_style,
    DOMNodeId node_id,
    const AutoDarkMode& auto_dark_mode) {
  text_painter.PaintSelectedText(fragment_paint_info, selection_status_.start,
                                 selection_status_.end, text_style,
                                 selection_style_, LineRelativeSelectionRect(),
                                 node_id, auto_dark_mode);
}

// Paint the given text range in the given style, suppressing the text proper
// (painting shadows only) where selected.
void HighlightPainter::SelectionPaintState::
    PaintSuppressingTextProperWhereSelected(
        TextPainter& text_painter,
        const TextFragmentPaintInfo& fragment_paint_info,
        const TextPaintStyle& text_style,
        DOMNodeId node_id,
        const AutoDarkMode& auto_dark_mode) {
  // First paint the shadows for the whole range.
  if (text_style.shadow) {
    text_painter.Paint(fragment_paint_info, text_style, node_id, auto_dark_mode,
                       TextPainter::kShadowsOnly);
  }

  // Then paint the text proper for any unselected parts in storage order, so
  // that they’re always on top of the shadows.
  if (fragment_paint_info.from < selection_status_.start) {
    text_painter.Paint(
        fragment_paint_info.WithEndOffset(selection_status_.start), text_style,
        node_id, auto_dark_mode, TextPainter::kTextProperOnly);
  }
  if (selection_status_.end < fragment_paint_info.to) {
    text_painter.Paint(
        fragment_paint_info.WithStartOffset(selection_status_.end), text_style,
        node_id, auto_dark_mode, TextPainter::kTextProperOnly);
  }
}

HighlightPainter::HighlightPainter(
    const TextFragmentPaintInfo& fragment_paint_info,
    TextPainter& text_painter,
    TextDecorationPainter& decoration_painter,
    const PaintInfo& paint_info,
    const InlineCursor& cursor,
    const FragmentItem& fragment_item,
    const absl::optional<AffineTransform> writing_mode_rotation,
    const PhysicalOffset& box_origin,
    const ComputedStyle& style,
    const TextPaintStyle& text_style,
    SelectionPaintState* selection,
    bool is_printing)
    : fragment_paint_info_(fragment_paint_info),
      text_painter_(text_painter),
      decoration_painter_(decoration_painter),
      paint_info_(paint_info),
      cursor_(cursor),
      fragment_item_(fragment_item),
      box_origin_(box_origin),
      originating_style_(style),
      originating_text_style_(text_style),
      selection_(selection),
      layout_object_(fragment_item_.GetLayoutObject()),
      node_(layout_object_->GetNode()),
      foreground_auto_dark_mode_(
          PaintAutoDarkMode(originating_style_,
                            DarkModeFilter::ElementRole::kForeground)),
      background_auto_dark_mode_(
          PaintAutoDarkMode(originating_style_,
                            DarkModeFilter::ElementRole::kBackground)),
      skip_backgrounds_(is_printing ||
                        paint_info.phase == PaintPhase::kTextClip ||
                        paint_info.phase == PaintPhase::kSelectionDragImage) {
  // Custom highlights and marker-based highlights are defined in terms of
  // DOM ranges in a Text node. Generated text either has no Text node or does
  // not derive its content from the Text node (e.g. ellipsis, soft hyphens).
  // TODO(crbug.com/17528) handle ::first-letter
  if (!fragment_item_.IsGeneratedText()) {
    const auto* text_node = DynamicTo<Text>(node_);
    if (text_node) {
      DocumentMarkerController& controller = node_->GetDocument().Markers();
      markers_ = controller.ComputeMarkersToPaint(*text_node);
      target_ = controller.MarkersFor(
          *text_node, DocumentMarker::MarkerTypes::TextFragment());
      spelling_ = controller.MarkersFor(
          *text_node, DocumentMarker::MarkerTypes::Spelling());
      grammar_ = controller.MarkersFor(*text_node,
                                       DocumentMarker::MarkerTypes::Grammar());
      custom_ = controller.MarkersFor(
          *text_node, DocumentMarker::MarkerTypes::CustomHighlight());
      // Check if there are any markers too, as required by OffsetMappingTest.
      if (selection || !markers_.empty() || !target_.empty() ||
          !spelling_.empty() || !grammar_.empty() || !custom_.empty()) {
        fragment_dom_offsets_ = GetFragmentDOMOffsets(
            *text_node, fragment_paint_info_.from, fragment_paint_info_.to);
      }
    }
  }

  paint_case_ = ComputePaintCase();

  // |layers_| and |parts_| are only needed when using the full overlay painting
  // algorithm, otherwise we can leave them empty.
  if (paint_case_ == kOverlay) {
    Vector<HighlightLayer> layers = HighlightOverlay::ComputeLayers(
        GetHighlightRegistry(node_), GetSelectionStatus(selection_), custom_,
        grammar_, spelling_, target_);
    Vector<HighlightEdge> edges = HighlightOverlay::ComputeEdges(
        node_, GetHighlightRegistry(node_), fragment_item_.IsGeneratedText(),
        fragment_dom_offsets_, GetSelectionStatus(selection_), custom_,
        grammar_, spelling_, target_);
    parts_ =
        HighlightOverlay::ComputeParts(fragment_paint_info_, layers, edges);

    const Document& document = layout_object_->GetDocument();
    for (wtf_size_t i = 0; i < layers.size(); i++) {
      if (layers[i].type == HighlightLayerType::kOriginating) {
        layers_.push_back(LayerPaintState{
            layers[i],
            &originating_style_,
            originating_text_style_,
        });
      } else {
        layers_.push_back(LayerPaintState{
            layers[i],
            HighlightStyleUtils::HighlightPseudoStyle(
                node_, originating_style_, layers[i].PseudoId(),
                layers[i].PseudoArgument()),
            HighlightStyleUtils::HighlightPaintingStyle(
                document, originating_style_, node_, layers[i].PseudoId(),
                layers_[i - 1].text_style, paint_info_,
                layers[i].PseudoArgument()),
        });
      }
    }
    if (!parts_.empty()) {
      if (const ShapeResultView* shape_result_view =
              fragment_item_->TextShapeResult()) {
        scoped_refptr<ShapeResult> shape_result =
            shape_result_view->CreateShapeResult();
        unsigned start_offset = fragment_item_->StartOffset();
        edges_info_.push_back(HighlightEdgeInfo{
            parts_[0].range.from,
            LayoutUnit::FromFloatRound(shape_result->CaretPositionForOffset(
                parts_[0].range.from - start_offset, cursor_.CurrentText()))});
        for (const HighlightPart& part : parts_) {
          edges_info_.push_back(HighlightEdgeInfo{
              part.range.to,
              LayoutUnit::FromFloatRound(shape_result->CaretPositionForOffset(
                  part.range.to - start_offset, cursor_.CurrentText()))});
        }
      } else {
        edges_info_.push_back(HighlightEdgeInfo{
            parts_[0].range.from,
            fragment_item_.CaretInlinePositionForOffset(cursor_.CurrentText(),
                                                        parts_[0].range.from)});
        for (const HighlightPart& part : parts_) {
          edges_info_.push_back(HighlightEdgeInfo{
              part.range.to, fragment_item_.CaretInlinePositionForOffset(
                                 cursor_.CurrentText(), part.range.to)});
        }
      }
    }
  }
}

void HighlightPainter::Paint(Phase phase) {
  if (markers_.empty())
    return;

  if (skip_backgrounds_ && phase == kBackground)
    return;

  DCHECK(fragment_item_.GetNode());
  const StringView text = cursor_.CurrentText();

  const auto* text_node = DynamicTo<Text>(node_);
  const MarkerRangeMappingContext mapping_context(*text_node,
                                                  *fragment_dom_offsets_);
  for (const DocumentMarker* marker : markers_) {
    absl::optional<TextOffsetRange> marker_offsets =
        mapping_context.GetTextContentOffsets(*marker);
    if (!marker_offsets || (marker_offsets->start == marker_offsets->end)) {
      continue;
    }
    const unsigned paint_start_offset = marker_offsets->start;
    const unsigned paint_end_offset = marker_offsets->end;

    DCHECK(!DocumentMarker::MarkerTypes::HighlightPseudos().Contains(
        marker->GetType()));

    switch (marker->GetType()) {
      case DocumentMarker::kSpelling:
      case DocumentMarker::kGrammar:
        if (phase == kForeground) {
          PaintOneSpellingGrammarDecoration(
              marker->GetType(), text, paint_start_offset, paint_end_offset);
        }
        break;

      case DocumentMarker::kTextMatch: {
        const Document& document = node_->GetDocument();
        if (!document.GetFrame()->GetEditor().MarkedTextMatchesAreHighlighted())
          break;
        const auto& text_match_marker = To<TextMatchMarker>(*marker);
        if (phase == kBackground) {
          Color color =
              LayoutTheme::GetTheme().PlatformTextSearchHighlightColor(
                  text_match_marker.IsActiveMatch(),
                  originating_style_.UsedColorScheme(),
                  document.GetColorProviderForPainting(
                      originating_style_.UsedColorScheme()));
          PaintRect(paint_info_.context, PhysicalOffset(box_origin_),
                    fragment_item_.LocalRect(text, paint_start_offset,
                                             paint_end_offset),
                    color, background_auto_dark_mode_);
          break;
        }

        TextPaintStyle text_style;
        if (fragment_item_->IsSvgText()) {
          // DocumentMarkerPainter::ComputeTextPaintStyleFrom() doesn't work
          // well with SVG <text>, which doesn't apply 'color' CSS property.
          const Color platform_matched_color =
              LayoutTheme::GetTheme().PlatformTextSearchColor(
                  text_match_marker.IsActiveMatch(),
                  originating_style_.UsedColorScheme(),
                  document.GetColorProviderForPainting(
                      originating_style_.UsedColorScheme()));
          text_painter_.SetSvgState(
              *To<LayoutSVGInlineText>(fragment_item_->GetLayoutObject()),
              originating_style_, platform_matched_color);
          text_style.current_color = platform_matched_color;
          text_style.stroke_width = originating_style_.TextStrokeWidth();
          text_style.color_scheme = originating_style_.UsedColorScheme();
        } else {
          text_style = DocumentMarkerPainter::ComputeTextPaintStyleFrom(
              document, node_, originating_style_, text_match_marker,
              paint_info_);
        }
        text_painter_.Paint(
            fragment_paint_info_.Slice(paint_start_offset, paint_end_offset),
            text_style, kInvalidDOMNodeId, foreground_auto_dark_mode_);
      } break;

      case DocumentMarker::kComposition:
      case DocumentMarker::kActiveSuggestion:
      case DocumentMarker::kSuggestion: {
        const auto& styleable_marker = To<StyleableMarker>(*marker);
        if (phase == kBackground) {
          PaintRect(paint_info_.context, PhysicalOffset(box_origin_),
                    fragment_item_.LocalRect(text, paint_start_offset,
                                             paint_end_offset),
                    styleable_marker.BackgroundColor(),
                    background_auto_dark_mode_);
          break;
        }
        if (DocumentMarkerPainter::ShouldPaintMarkerUnderline(
                styleable_marker)) {
          const SimpleFontData* font_data =
              originating_style_.GetFont().PrimaryFont();
          DocumentMarkerPainter::PaintStyleableMarkerUnderline(
              paint_info_.context, box_origin_, styleable_marker,
              originating_style_, node_->GetDocument(),
              LineRelativeLocalRect(fragment_item_, text, paint_start_offset,
                                    paint_end_offset),
              LayoutUnit(font_data->GetFontMetrics().Height()),
              fragment_item_.GetNode()->GetDocument().InDarkMode());
        }
        if (marker->GetType() == DocumentMarker::kComposition &&
            !styleable_marker.TextColor().IsFullyTransparent() &&
            RuntimeEnabledFeatures::CompositionForegroundMarkersEnabled()) {
          PaintDecoratedText(text, styleable_marker.TextColor(),
                             paint_start_offset, paint_end_offset);
        }
      } break;

      case DocumentMarker::kTextFragment:
      case DocumentMarker::kCustomHighlight: {
        const auto& highlight_pseudo_marker =
            To<HighlightPseudoMarker>(*marker);
        const Document& document = node_->GetDocument();

        // Paint background
        if (phase == kBackground) {
          Color background_color =
              HighlightStyleUtils::HighlightBackgroundColor(
                  document, originating_style_, node_, absl::nullopt,
                  highlight_pseudo_marker.GetPseudoId(),
                  highlight_pseudo_marker.GetPseudoArgument());

          PaintRect(paint_info_.context, PhysicalOffset(box_origin_),
                    fragment_item_.LocalRect(text, paint_start_offset,
                                             paint_end_offset),
                    background_color, background_auto_dark_mode_);
          break;
        }

        DCHECK_EQ(phase, kForeground);
        Color text_color =
            originating_style_.VisitedDependentColor(GetCSSPropertyColor());
        PaintDecoratedText(text, text_color, paint_start_offset,
                           paint_end_offset,
                           highlight_pseudo_marker.GetPseudoId(),
                           highlight_pseudo_marker.GetPseudoArgument());
      } break;

      default:
        NOTREACHED();
        break;
    }
  }
}

HighlightPainter::Case HighlightPainter::PaintCase() const {
  return paint_case_;
}

HighlightPainter::Case HighlightPainter::ComputePaintCase() const {
  if (selection_ && selection_->ShouldPaintSelectedTextOnly())
    return kSelectionOnly;

  // This can yield false positives (weakening the optimisations below) if all
  // non-spelling/grammar/selection highlights are outside the text fragment.
  if (!target_.empty() || !custom_.empty())
    return kOverlay;

  if (selection_ && spelling_.empty() && grammar_.empty()) {
    const ComputedStyle* pseudo_style =
        HighlightStyleUtils::HighlightPseudoStyle(node_, originating_style_,
                                                  kPseudoIdSelection);

    // If we only have a selection, and there are no selection or originating
    // decorations, we don’t need the expense of overlay painting.
    return !originating_style_.HasAppliedTextDecorations() &&
                   (!pseudo_style || !pseudo_style->HasAppliedTextDecorations())
               ? kFastSelection
               : kOverlay;
  }

  if (!spelling_.empty() || !grammar_.empty()) {
    // If there is a selection too, we must use the overlay painting algorithm.
    if (selection_)
      return kOverlay;

    // If there are only spelling and/or grammar highlights, and they use the
    // default style that only adds decorations without adding a background or
    // changing the text color, we don’t need the expense of overlay painting.
    bool spelling_ok =
        spelling_.empty() ||
        !HasNonTrivialSpellingGrammarStyles(
            fragment_item_, node_, originating_style_, kPseudoIdSpellingError);
    bool grammar_ok =
        grammar_.empty() ||
        !HasNonTrivialSpellingGrammarStyles(
            fragment_item_, node_, originating_style_, kPseudoIdGrammarError);
    return spelling_ok && grammar_ok ? kFastSpellingGrammar : kOverlay;
  }

  DCHECK(!selection_ && target_.empty() && spelling_.empty() &&
         grammar_.empty() && custom_.empty());
  return kNoHighlights;
}

void HighlightPainter::FastPaintSpellingGrammarDecorations() {
  DCHECK_EQ(paint_case_, kFastSpellingGrammar);
  DCHECK(fragment_item_.GetNode());
  const auto& text_node = To<Text>(*fragment_item_.GetNode());
  const StringView text = cursor_.CurrentText();

  // ::spelling-error overlay is drawn on top of ::grammar-error overlay.
  // https://drafts.csswg.org/css-pseudo-4/#highlight-backgrounds
  FastPaintSpellingGrammarDecorations(text_node, text, grammar_);
  FastPaintSpellingGrammarDecorations(text_node, text, spelling_);
}

void HighlightPainter::FastPaintSpellingGrammarDecorations(
    const Text& text_node,
    const StringView& text,
    const DocumentMarkerVector& markers) {
  const MarkerRangeMappingContext mapping_context(text_node,
                                                  *fragment_dom_offsets_);
  for (const DocumentMarker* marker : markers) {
    absl::optional<TextOffsetRange> marker_offsets =
        mapping_context.GetTextContentOffsets(*marker);
    if (!marker_offsets || (marker_offsets->start == marker_offsets->end)) {
      continue;
    }
    PaintOneSpellingGrammarDecoration(
        marker->GetType(), text, marker_offsets->start, marker_offsets->end);
  }
}

void HighlightPainter::PaintOneSpellingGrammarDecoration(
    DocumentMarker::MarkerType type,
    const StringView& text,
    unsigned paint_start_offset,
    unsigned paint_end_offset) {
  if (fragment_item_.GetNode()->GetDocument().Printing())
    return;

  // If the new ::spelling-error and ::grammar-error pseudos are not enabled,
  // use the old marker-based decorations for now.
  if (!RuntimeEnabledFeatures::CSSSpellingGrammarErrorsEnabled()) {
    return DocumentMarkerPainter::PaintDocumentMarker(
        paint_info_, box_origin_, originating_style_, type,
        LineRelativeLocalRect(fragment_item_, text, paint_start_offset,
                              paint_end_offset),
        HighlightStyleUtils::HighlightTextDecorationColor(
            layout_object_->GetDocument(), originating_style_, node_,
            originating_text_style_.current_color, PseudoFor(type)));
  }

  if (!text_painter_.GetSvgState()) {
    if (const auto* pseudo_style = HighlightStyleUtils::HighlightPseudoStyle(
            node_, originating_style_, PseudoFor(type))) {
      const TextPaintStyle text_style =
          HighlightStyleUtils::HighlightPaintingStyle(
              node_->GetDocument(), originating_style_, node_, PseudoFor(type),
              originating_text_style_, paint_info_);
      PaintOneSpellingGrammarDecoration(type, text, paint_start_offset,
                                        paint_end_offset, *pseudo_style,
                                        text_style, nullptr);
      return;
    }
  }

  // If they are not yet implemented (as is the case for SVG), or they have no
  // styles (as there can be for non-HTML content or for HTML content with the
  // wrong root), use the originating style with the decorations override set
  // to a synthesised AppliedTextDecoration.
  //
  // For the synthesised decoration, just like with our real spelling/grammar
  // decorations, the ‘text-decoration-style’, ‘text-decoration-thickness’, and
  // ‘text-underline-offset’ are irrelevant.
  //
  // SVG painting currently ignores ::selection styles, and will malfunction
  // or crash if asked to paint decorations introduced by highlight pseudos.
  // TODO(crbug.com/1147859) is SVG spec ready for highlight decorations?
  // TODO(crbug.com/1147859) https://github.com/w3c/svgwg/issues/894
  const AppliedTextDecoration synthesised{
      LineFor(type), {}, ColorFor(type), {}, {}};
  PaintOneSpellingGrammarDecoration(type, text, paint_start_offset,
                                    paint_end_offset, originating_style_,
                                    originating_text_style_, &synthesised);
}

void HighlightPainter::PaintOneSpellingGrammarDecoration(
    DocumentMarker::MarkerType marker_type,
    const StringView& text,
    unsigned paint_start_offset,
    unsigned paint_end_offset,
    const ComputedStyle& style,
    const TextPaintStyle& text_style,
    const AppliedTextDecoration* decoration_override) {
  // When painting decorations on the spelling/grammar fast path, the part and
  // the decoration have the same range, so we can use the same rect for both
  // clipping the canvas and painting the decoration.
  const HighlightRange range{paint_start_offset, paint_end_offset};
  const LineRelativeRect rect = LineRelativeWorldRect(range);

  absl::optional<TextDecorationInfo> decoration_info{};
  decoration_painter_.UpdateDecorationInfo(decoration_info, style, rect,
                                           decoration_override);

  GraphicsContextStateSaver saver{paint_info_.context};
  ClipToPartDecorations(rect);

  text_painter_.PaintDecorationsExceptLineThrough(
      fragment_paint_info_.Slice(paint_start_offset, paint_end_offset),
      fragment_item_, paint_info_, text_style, *decoration_info,
      LineFor(marker_type));
}

void HighlightPainter::PaintOriginatingText(const TextPaintStyle& text_style,
                                            DOMNodeId node_id) {
  DCHECK_EQ(paint_case_, kOverlay);

  // First paint the shadows for the whole range.
  if (text_style.shadow) {
    text_painter_.Paint(fragment_paint_info_, text_style, node_id,
                        foreground_auto_dark_mode_, TextPainter::kShadowsOnly);
  }

  // Then paint the text proper for any unhighlighted parts in storage order,
  // so that they’re always on top of the shadows.
  for (const HighlightPart& part : parts_) {
    if (part.layer.type != HighlightLayerType::kOriginating)
      continue;

    PaintDecorationsExceptLineThrough(part);
    text_painter_.Paint(
        fragment_paint_info_.Slice(part.range.from, part.range.to), text_style,
        node_id, foreground_auto_dark_mode_, TextPainter::kTextProperOnly);
    PaintDecorationsOnlyLineThrough(part);
    PaintSpellingGrammarDecorations(part);
  }
}

Vector<LayoutSelectionStatus> HighlightPainter::GetHighlights(
    const LayerPaintState& layer) {
  Vector<LayoutSelectionStatus> result{};
  const auto* text_node = DynamicTo<Text>(fragment_item_.GetNode());
  switch (layer.id.type) {
    case HighlightLayerType::kOriginating:
      NOTREACHED();
      break;
    case HighlightLayerType::kCustom: {
      DCHECK(text_node);
      const MarkerRangeMappingContext mapping_context(*text_node,
                                                      *fragment_dom_offsets_);
      for (const auto& marker : custom_) {
        // Filter custom highlight markers to one highlight at a time.
        auto* custom = To<CustomHighlightMarker>(marker.Get());
        if (custom->GetHighlightName() != layer.id.PseudoArgument()) {
          continue;
        }
        absl::optional<TextOffsetRange> marker_offsets =
            mapping_context.GetTextContentOffsets(*marker);
        if (marker_offsets && (marker_offsets->start != marker_offsets->end)) {
          result.push_back(
              LayoutSelectionStatus{marker_offsets->start, marker_offsets->end,
                                    SelectSoftLineBreak::kNotSelected});
        }
      }
      break;
    }
    case HighlightLayerType::kGrammar: {
      DCHECK(text_node);
      const MarkerRangeMappingContext mapping_context(*text_node,
                                                      *fragment_dom_offsets_);
      for (const auto& marker : grammar_) {
        absl::optional<TextOffsetRange> marker_offsets =
            mapping_context.GetTextContentOffsets(*marker);
        if (marker_offsets && (marker_offsets->start != marker_offsets->end)) {
          result.push_back(
              LayoutSelectionStatus{marker_offsets->start, marker_offsets->end,
                                    SelectSoftLineBreak::kNotSelected});
        }
      }
      break;
    }
    case HighlightLayerType::kSpelling: {
      DCHECK(text_node);
      const MarkerRangeMappingContext mapping_context(*text_node,
                                                      *fragment_dom_offsets_);
      for (const auto& marker : spelling_) {
        absl::optional<TextOffsetRange> marker_offsets =
            mapping_context.GetTextContentOffsets(*marker);
        if (marker_offsets && (marker_offsets->start != marker_offsets->end)) {
          result.push_back(
              LayoutSelectionStatus{marker_offsets->start, marker_offsets->end,
                                    SelectSoftLineBreak::kNotSelected});
        }
      }
      break;
    }
    case HighlightLayerType::kTargetText: {
      DCHECK(text_node);
      const MarkerRangeMappingContext mapping_context(*text_node,
                                                      *fragment_dom_offsets_);
      for (const auto& marker : target_) {
        absl::optional<TextOffsetRange> marker_offsets =
            mapping_context.GetTextContentOffsets(*marker);
        if (marker_offsets && (marker_offsets->start != marker_offsets->end)) {
          result.push_back(
              LayoutSelectionStatus{marker_offsets->start, marker_offsets->end,
                                    SelectSoftLineBreak::kNotSelected});
        }
      }
      break;
    }
    case HighlightLayerType::kSelection:
      result.push_back(*GetSelectionStatus(selection_));
      break;
  }
  return result;
}

TextOffsetRange HighlightPainter::GetFragmentDOMOffsets(const Text& text,
                                                        unsigned from,
                                                        unsigned to) {
  const OffsetMapping* mapping = OffsetMapping::GetFor(text.GetLayoutObject());
  unsigned last_from = mapping->GetLastPosition(from).OffsetInContainerNode();
  unsigned first_to = mapping->GetFirstPosition(to).OffsetInContainerNode();
  return {last_from, first_to};
}

const PhysicalRect HighlightPainter::ComputeBackgroundRect(
    StringView text,
    unsigned start_offset,
    unsigned end_offset) {
  const PhysicalRect& rect =
      fragment_item_.LocalRect(text, start_offset, end_offset);
  return PhysicalRect(rect.offset + PhysicalOffset(box_origin_), rect.size);
}

void HighlightPainter::PaintHighlightOverlays(
    const TextPaintStyle& originating_text_style,
    DOMNodeId node_id,
    bool paint_marker_backgrounds,
    absl::optional<AffineTransform> rotation) {
  DCHECK_EQ(paint_case_, kOverlay);

  // |node_| might not be a Text node (e.g. <br>), or it might be nullptr (e.g.
  // ::first-letter). In both cases, we should still try to paint kOriginating
  // and kSelection if necessary, but we can’t paint marker-based highlights,
  // because GetTextContentOffset requires a Text node. Markers are defined and
  // stored in terms of Text nodes anyway, so this should never be a problem.
  const Document& document = layout_object_->GetDocument();

  // For each overlay, paint its backgrounds and shadows over every highlighted
  // range in full.
  for (const LayerPaintState& layer : layers_) {
    if (layer.id.type == HighlightLayerType::kOriginating) {
      continue;
    }

    if (layer.id.type == HighlightLayerType::kSelection &&
        !paint_marker_backgrounds) {
      continue;
    }

    Vector<LayoutSelectionStatus> highlights = GetHighlights(layer);

    for (const auto& highlight : highlights) {
      if (highlight.end == highlight.start) {
        continue;
      }

      const StringView text = cursor_.CurrentText();

      // TODO(crbug.com/1480139) ComputeBackgroundRect should use the same logic
      // as CurrentLocalSelectionRectForText, that is, it should expand
      // selection to the line height and extend for line breaks.
      const PhysicalRect& rect =
          layer.id.type == HighlightLayerType::kSelection
              ? selection_->PhysicalSelectionRect()
              : ComputeBackgroundRect(text, highlight.start, highlight.end);

      Color background_color = HighlightStyleUtils::HighlightBackgroundColor(
          document, originating_style_, node_, layer.text_style.current_color,
          layer.id.PseudoId(), layer.id.PseudoArgument());

      PaintHighlightBackground(paint_info_.context, originating_style_,
                               background_color, rect, rotation);

      if (layer.text_style.shadow) {
        text_painter_.Paint(
            fragment_paint_info_.Slice(highlight.start, highlight.end),
            layer.text_style, node_id, foreground_auto_dark_mode_,
            TextPainterBase::kShadowsOnly);
      }
    }
  }

  // For each overlay, paint the text proper over every highlighted range,
  // except any parts for which we’re not the topmost active highlight.
  for (const LayerPaintState& layer : layers_) {
    if (layer.id.type == HighlightLayerType::kOriginating ||
        layer.id.type == HighlightLayerType::kSelection)
      continue;

    for (const HighlightPart& part : parts_) {
      if (part.layer != layer.id)
        continue;

      // TODO(crbug.com/1434114) expand range to include partial glyphs, then
      // paint with clipping (TextPainter::PaintSelectedText)
      PaintDecorationsExceptLineThrough(part);
      text_painter_.Paint(
          fragment_paint_info_.Slice(part.range.from, part.range.to),
          layer.text_style, node_id, foreground_auto_dark_mode_,
          TextPainterBase::kTextProperOnly);
      PaintDecorationsOnlyLineThrough(part);
      PaintSpellingGrammarDecorations(part);
    }
  }

  // Paint ::selection foreground, including its shadows.
  // TODO(crbug.com/1434114) generalise ::selection painting logic to support
  // all highlights, then merge this branch into the loop above
  if (UNLIKELY(selection_)) {
    for (const HighlightPart& part : parts_) {
      if (part.layer.type == HighlightLayerType::kSelection)
        PaintDecorationsExceptLineThrough(part);
    }

    selection_->PaintSelectedText(text_painter_, fragment_paint_info_,
                                  originating_text_style, node_id,
                                  foreground_auto_dark_mode_);

    for (const HighlightPart& part : parts_) {
      if (part.layer.type == HighlightLayerType::kSelection) {
        PaintDecorationsOnlyLineThrough(part);
        PaintSpellingGrammarDecorations(part);
      }
    }
  }
}

void HighlightPainter::PaintHighlightBackground(
    GraphicsContext& context,
    const ComputedStyle& style,
    Color color,
    const PhysicalRect& rect,
    const absl::optional<AffineTransform>& rotation) {
  AutoDarkMode auto_dark_mode(
      PaintAutoDarkMode(style, DarkModeFilter::ElementRole::kSelection));

  if (!rotation) {
    PaintRect(context, rect, color, auto_dark_mode);
    return;
  }

  // PaintRect tries to pixel-snap the given rect, but if we’re painting in a
  // non-horizontal writing mode, our context has been transformed, regressing
  // tests like <paint/invalidation/repaint-across-writing-mode-boundary>. To
  // fix this, we undo the transformation temporarily, then use the original
  // physical coordinates (before MapSelectionRectIntoRotatedSpace).
  context.ConcatCTM(rotation->Inverse());
  PaintRect(context, rect, color, auto_dark_mode);
  context.ConcatCTM(*rotation);
}

PseudoId HighlightPainter::PseudoFor(DocumentMarker::MarkerType type) {
  switch (type) {
    case DocumentMarker::kSpelling:
      return kPseudoIdSpellingError;
    case DocumentMarker::kGrammar:
      return kPseudoIdGrammarError;
    case DocumentMarker::kTextFragment:
      return kPseudoIdTargetText;
    default:
      NOTREACHED();
      return {};
  }
}

TextDecorationLine HighlightPainter::LineFor(DocumentMarker::MarkerType type) {
  switch (type) {
    case DocumentMarker::kSpelling:
      return TextDecorationLine::kSpellingError;
    case DocumentMarker::kGrammar:
      return TextDecorationLine::kGrammarError;
    default:
      NOTREACHED();
      return {};
  }
}

Color HighlightPainter::ColorFor(DocumentMarker::MarkerType type) {
  switch (type) {
    case DocumentMarker::kSpelling:
      return LayoutTheme::GetTheme().PlatformSpellingMarkerUnderlineColor();
    case DocumentMarker::kGrammar:
      return LayoutTheme::GetTheme().PlatformGrammarMarkerUnderlineColor();
    default:
      NOTREACHED();
      return {};
  }
}

LineRelativeRect HighlightPainter::LineRelativeWorldRect(
    const HighlightOverlay::HighlightRange& range) {
  return LocalRectInWritingModeSpace(range.from, range.to) +
         LineRelativeOffset::CreateFromBoxOrigin(box_origin_);
}

LineRelativeRect HighlightPainter::LocalRectInWritingModeSpace(
    unsigned from,
    unsigned to) const {
  if (paint_case_ != kOverlay) {
    const StringView text = cursor_.CurrentText();
    return LineRelativeLocalRect(fragment_item_, text, from, to);
  }

  const HighlightEdgeInfo* from_info =
      std::lower_bound(edges_info_.begin(), edges_info_.end(), from,
                       [](const HighlightEdgeInfo& info, unsigned offset) {
                         return info.offset < offset;
                       });
  const HighlightEdgeInfo* to_info =
      std::lower_bound(from_info, edges_info_.end(), to,
                       [](const HighlightEdgeInfo& info, unsigned offset) {
                         return info.offset < offset;
                       });
  DCHECK_NE(from_info, edges_info_.end());
  DCHECK_NE(to_info, edges_info_.end());

  const LayoutUnit height = fragment_item_.InkOverflowRect().Height();
  if (from_info->x > to_info->x) {
    return {{to_info->x, LayoutUnit{}}, {from_info->x - to_info->x, height}};
  }
  return {{from_info->x, LayoutUnit{}}, {to_info->x - from_info->x, height}};
}

void HighlightPainter::ClipToPartDecorations(
    const LineRelativeRect& part_rect) {
  gfx::RectF clip_rect{part_rect};

  // Whether it’s best to clip to selection rect on both axes or only inline
  // depends on the situation, but the latter can improve the appearance of
  // decorations. For example, we often paint overlines entirely past the
  // top edge of selection rect, and wavy underlines have similar problems.
  //
  // Sadly there’s no way to clip to a rect of infinite height, so for now,
  // let’s clip to selection rect plus its height both above and below. This
  // should be enough to avoid clipping most decorations in the wild.
  //
  // TODO(crbug.com/1433400): take text-underline-offset and other
  // text-decoration properties into account?
  clip_rect.set_y(clip_rect.y() - clip_rect.height());
  clip_rect.set_height(3.0 * clip_rect.height());
  paint_info_.context.Clip(clip_rect);
}

void HighlightPainter::PaintDecorationsExceptLineThrough(
    const HighlightPart& part) {
  // Line decorations in highlight pseudos are ordered first by the kind of line
  // (underlines before overlines), then by the highlight layer they came from.
  // https://github.com/w3c/csswg-drafts/issues/6022
  PaintDecorationsExceptLineThrough(part, TextDecorationLine::kUnderline);
  PaintDecorationsExceptLineThrough(part, TextDecorationLine::kOverline);
  PaintDecorationsExceptLineThrough(
      part,
      TextDecorationLine::kSpellingError | TextDecorationLine::kGrammarError);
}

void HighlightPainter::PaintDecorationsExceptLineThrough(
    const HighlightPart& part,
    TextDecorationLine lines_to_paint) {
  GraphicsContextStateSaver state_saver(paint_info_.context, false);

  for (const HighlightDecoration& decoration : part.decorations) {
    wtf_size_t decoration_layer_index = layers_.Find(decoration.layer);
    DCHECK_NE(decoration_layer_index, kNotFound);

    LayerPaintState& decoration_layer = layers_[decoration_layer_index];

    // Clipping the canvas unnecessarily is expensive, so avoid doing it if
    // there are no decorations of the given |lines_to_paint|.
    if (!EnumHasFlags(decoration_layer.decorations_in_effect, lines_to_paint)) {
      continue;
    }

    // SVG painting currently ignores ::selection styles, and will malfunction
    // or crash if asked to paint decorations introduced by highlight pseudos.
    // TODO(crbug.com/1147859) is SVG spec ready for highlight decorations?
    // TODO(crbug.com/1147859) https://github.com/w3c/svgwg/issues/894
    if (text_painter_.GetSvgState() &&
        decoration.layer.type != HighlightLayerType::kOriginating) {
      continue;
    }

    // Paint the decoration over the range of the originating fragment or active
    // highlight, but clip it to the range of the part.
    const LineRelativeRect decoration_rect =
        LineRelativeWorldRect(decoration.range);
    const LineRelativeRect part_rect = part.range != decoration.range
                                           ? LineRelativeWorldRect(part.range)
                                           : decoration_rect;

    absl::optional<TextDecorationInfo> decoration_info{};
    decoration_painter_.UpdateDecorationInfo(
        decoration_info, *decoration_layer.style, decoration_rect);

    if (!state_saver.Saved()) {
      state_saver.Save();
      ClipToPartDecorations(part_rect);
    }

    if (part.layer.type != HighlightLayerType::kOriginating) {
      if (decoration.layer.type == HighlightLayerType::kOriginating) {
        wtf_size_t part_layer_index = layers_.Find(part.layer);
        decoration_info->SetHighlightOverrideColor(
            layers_[part_layer_index].text_style.current_color);
      } else {
        decoration_info->SetHighlightOverrideColor(
            HighlightStyleUtils::ResolveColor(
                layout_object_->GetDocument(), originating_style_,
                decoration_layer.style.Get(), decoration_layer.id.PseudoId(),
                GetCSSPropertyTextDecorationColor(),
                layers_[decoration_layer_index - 1].text_style.current_color));
      }
    }

    text_painter_.PaintDecorationsExceptLineThrough(
        fragment_paint_info_.Slice(part.range.from, part.range.to),
        fragment_item_, paint_info_, decoration_layer.text_style,
        *decoration_info, lines_to_paint);
  }
}

void HighlightPainter::PaintDecorationsOnlyLineThrough(
    const HighlightPart& part) {
  GraphicsContextStateSaver state_saver(paint_info_.context, false);

  for (const HighlightDecoration& decoration : part.decorations) {
    wtf_size_t decoration_layer_index = layers_.Find(decoration.layer);
    DCHECK_NE(decoration_layer_index, kNotFound);

    LayerPaintState& decoration_layer = layers_[decoration_layer_index];

    // Clipping the canvas unnecessarily is expensive, so avoid doing it if
    // there are no ‘line-through’ decorations.
    if (!EnumHasFlags(decoration_layer.decorations_in_effect,
                      TextDecorationLine::kLineThrough)) {
      continue;
    }

    // SVG painting currently ignores ::selection styles, and will malfunction
    // or crash if asked to paint decorations introduced by highlight pseudos.
    // TODO(crbug.com/1147859) is SVG spec ready for highlight decorations?
    // TODO(crbug.com/1147859) https://github.com/w3c/svgwg/issues/894
    if (text_painter_.GetSvgState() &&
        decoration.layer.type != HighlightLayerType::kOriginating) {
      continue;
    }

    // Paint the decoration over the range of the originating fragment or active
    // highlight, but clip it to the range of the part.
    const LineRelativeRect decoration_rect =
        LineRelativeWorldRect(decoration.range);
    const LineRelativeRect part_rect = part.range != decoration.range
                                           ? LineRelativeWorldRect(part.range)
                                           : decoration_rect;

    absl::optional<TextDecorationInfo> decoration_info{};
    decoration_painter_.UpdateDecorationInfo(
        decoration_info, *decoration_layer.style, decoration_rect);

    if (!state_saver.Saved()) {
      state_saver.Save();
      ClipToPartDecorations(part_rect);
    }

    if (part.layer.type != HighlightLayerType::kOriginating) {
      if (decoration.layer.type == HighlightLayerType::kOriginating) {
        wtf_size_t part_layer_index = layers_.Find(part.layer);
        decoration_info->SetHighlightOverrideColor(
            layers_[part_layer_index].text_style.current_color);
      } else {
        decoration_info->SetHighlightOverrideColor(
            HighlightStyleUtils::ResolveColor(
                layout_object_->GetDocument(), originating_style_,
                decoration_layer.style.Get(), decoration_layer.id.PseudoId(),
                GetCSSPropertyTextDecorationColor(),
                layers_[decoration_layer_index - 1].text_style.current_color));
      }
    }

    text_painter_.PaintDecorationsOnlyLineThrough(fragment_item_, paint_info_,
                                                  decoration_layer.text_style,
                                                  *decoration_info);
  }
}

void HighlightPainter::PaintSpellingGrammarDecorations(
    const HighlightPart& part) {
  if (RuntimeEnabledFeatures::CSSSpellingGrammarErrorsEnabled())
    return;

  const StringView text = cursor_.CurrentText();
  absl::optional<LineRelativeRect> marker_rect;

  for (const HighlightDecoration& decoration : part.decorations) {
    switch (decoration.layer.type) {
      case HighlightLayerType::kSpelling:
      case HighlightLayerType::kGrammar: {
        wtf_size_t i = layers_.Find(decoration.layer);
        DCHECK_NE(i, kNotFound);
        const LayerPaintState& decoration_layer = layers_[i];

        // TODO(crbug.com/1163436): remove once UA stylesheet sets ::spelling
        // and ::grammar to text-decoration-line:{spelling,grammar}-error
        if (decoration_layer.style &&
            decoration_layer.style->HasAppliedTextDecorations()) {
          break;
        }

        if (!marker_rect) {
          marker_rect = LineRelativeLocalRect(fragment_item_, text,
                                              part.range.from, part.range.to);
        }

        DocumentMarkerPainter::PaintDocumentMarker(
            paint_info_, box_origin_, originating_style_,
            decoration.layer.type == HighlightLayerType::kSpelling
                ? DocumentMarker::kSpelling
                : DocumentMarker::kGrammar,
            *marker_rect,
            HighlightStyleUtils::HighlightTextDecorationColor(
                layout_object_->GetDocument(), originating_style_, node_,
                layers_[i - 1].text_style.current_color,
                decoration.layer.type == HighlightLayerType::kSpelling
                    ? kPseudoIdSpellingError
                    : kPseudoIdGrammarError));
      } break;

      default:
        break;
    }
  }
}

void HighlightPainter::PaintDecoratedText(const StringView& text,
                                          const Color& text_color,
                                          unsigned paint_start_offset,
                                          unsigned paint_end_offset,
                                          const PseudoId pseudo,
                                          const AtomicString& pseudo_argument) {
  const Document& document = node_->GetDocument();
  TextPaintStyle text_style;
  text_style.current_color = text_style.fill_color = text_style.stroke_color =
      text_style.emphasis_mark_color = text_color;
  text_style.stroke_width = originating_style_.TextStrokeWidth();
  text_style.color_scheme = originating_style_.UsedColorScheme();
  text_style.shadow = nullptr;

  const ComputedStyle* pseudo_style =
      pseudo == PseudoId::kPseudoIdNone
          ? nullptr
          : HighlightStyleUtils::HighlightPseudoStyle(node_, originating_style_,
                                                      pseudo, pseudo_argument);

  if (pseudo_style) {
    text_style = HighlightStyleUtils::HighlightPaintingStyle(
        document, originating_style_, node_, pseudo, text_style, paint_info_,
        pseudo_argument);
  }
  LineRelativeRect decoration_rect = LineRelativeLocalRect(
      fragment_item_, text, paint_start_offset, paint_end_offset);
  decoration_rect.Move(LineRelativeOffset::CreateFromBoxOrigin(box_origin_));
  TextDecorationPainter decoration_painter(
      text_painter_, fragment_item_, paint_info_,
      pseudo_style ? *pseudo_style : originating_style_, text_style,
      decoration_rect, selection_);

  decoration_painter.Begin(TextDecorationPainter::kOriginating);
  decoration_painter.PaintExceptLineThrough(
      fragment_paint_info_.Slice(paint_start_offset, paint_end_offset));

  text_painter_.Paint(
      fragment_paint_info_.Slice(paint_start_offset, paint_end_offset),
      text_style, kInvalidDOMNodeId, foreground_auto_dark_mode_);

  decoration_painter.PaintOnlyLineThrough();
}

HighlightPainter::LayerPaintState::LayerPaintState(
    HighlightOverlay::HighlightLayer id,
    const ComputedStyle* style,
    TextPaintStyle text_style)
    : id(id),
      style(style),
      text_style(text_style),
      decorations_in_effect(style && style->HasAppliedTextDecorations()
                                ? style->TextDecorationsInEffect()
                                : TextDecorationLine::kNone) {}

bool HighlightPainter::LayerPaintState::operator==(
    const HighlightLayer& other) const {
  return id == other;
}

bool HighlightPainter::LayerPaintState::operator!=(
    const HighlightLayer& other) const {
  return !operator==(other);
}

}  // namespace blink
