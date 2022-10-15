/*
 * Copyright (C) 1999 Lars Knoll (knoll@kde.org)
 *           (C) 2004-2005 Allan Sandfeld Jensen (kde@carewolf.com)
 * Copyright (C) 2006, 2007 Nicholas Shanks (webkit@nickshanks.com)
 * Copyright (C) 2005, 2006, 2007, 2008, 2009, 2010, 2011, 2012, 2013 Apple Inc.
 * All rights reserved.
 * Copyright (C) 2007 Alexey Proskuryakov <ap@webkit.org>
 * Copyright (C) 2007, 2008 Eric Seidel <eric@webkit.org>
 * Copyright (C) 2008, 2009 Torch Mobile Inc. All rights reserved.
 * (http://www.torchmobile.com/)
 * Copyright (c) 2011, Code Aurora Forum. All rights reserved.
 * Copyright (C) Research In Motion Limited 2011. All rights reserved.
 * Copyright (C) 2013 Google Inc. All rights reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include "third_party/blink/renderer/core/css/resolver/style_adjuster.h"

#include "third_party/blink/public/common/features.h"
#include "third_party/blink/renderer/core/css/resolver/style_resolver.h"
#include "third_party/blink/renderer/core/css/resolver/style_resolver_state.h"
#include "third_party/blink/renderer/core/css/style_change_reason.h"
#include "third_party/blink/renderer/core/css/style_engine.h"
#include "third_party/blink/renderer/core/dom/container_node.h"
#include "third_party/blink/renderer/core/dom/document.h"
#include "third_party/blink/renderer/core/dom/element.h"
#include "third_party/blink/renderer/core/dom/flat_tree_traversal.h"
#include "third_party/blink/renderer/core/dom/node_computed_style.h"
#include "third_party/blink/renderer/core/dom/pseudo_element.h"
#include "third_party/blink/renderer/core/dom/shadow_root.h"
#include "third_party/blink/renderer/core/frame/event_handler_registry.h"
#include "third_party/blink/renderer/core/frame/local_frame.h"
#include "third_party/blink/renderer/core/frame/local_frame_view.h"
#include "third_party/blink/renderer/core/frame/settings.h"
#include "third_party/blink/renderer/core/frame/web_feature.h"
#include "third_party/blink/renderer/core/fullscreen/fullscreen.h"
#include "third_party/blink/renderer/core/html/fenced_frame/html_fenced_frame_element.h"
#include "third_party/blink/renderer/core/html/forms/html_input_element.h"
#include "third_party/blink/renderer/core/html/forms/html_text_area_element.h"
#include "third_party/blink/renderer/core/html/html_body_element.h"
#include "third_party/blink/renderer/core/html/html_dialog_element.h"
#include "third_party/blink/renderer/core/html/html_iframe_element.h"
#include "third_party/blink/renderer/core/html/html_image_element.h"
#include "third_party/blink/renderer/core/html/html_plugin_element.h"
#include "third_party/blink/renderer/core/html/html_table_cell_element.h"
#include "third_party/blink/renderer/core/html/media/html_media_element.h"
#include "third_party/blink/renderer/core/html/shadow/shadow_element_names.h"
#include "third_party/blink/renderer/core/html_names.h"
#include "third_party/blink/renderer/core/input_type_names.h"
#include "third_party/blink/renderer/core/layout/layout_theme.h"
#include "third_party/blink/renderer/core/layout/list_marker.h"
#include "third_party/blink/renderer/core/layout/ng/inline/layout_ng_text_combine.h"
#include "third_party/blink/renderer/core/mathml/mathml_element.h"
#include "third_party/blink/renderer/core/style/computed_style.h"
#include "third_party/blink/renderer/core/style/computed_style_constants.h"
#include "third_party/blink/renderer/core/style/style_intrinsic_length.h"
#include "third_party/blink/renderer/core/svg/svg_svg_element.h"
#include "third_party/blink/renderer/core/svg_names.h"
#include "third_party/blink/renderer/platform/geometry/length.h"
#include "third_party/blink/renderer/platform/instrumentation/use_counter.h"
#include "third_party/blink/renderer/platform/runtime_enabled_features.h"
#include "third_party/blink/renderer/platform/transforms/transform_operations.h"
#include "ui/base/ui_base_features.h"

namespace blink {

namespace {

bool IsOverflowClipOrVisible(EOverflow overflow) {
  return overflow == EOverflow::kClip || overflow == EOverflow::kVisible;
}

TouchAction AdjustTouchActionForElement(TouchAction touch_action,
                                        const ComputedStyle& style,
                                        const ComputedStyle& parent_style,
                                        Element* element) {
  Element* document_element = element->GetDocument().documentElement();
  bool scrolls_overflow = style.ScrollsOverflow();
  if (element && element == element->GetDocument().FirstBodyElement()) {
    // Body scrolls overflow if html root overflow is not visible or the
    // propagation of overflow is stopped by containment.
    if (parent_style.IsOverflowVisibleAlongBothAxes()) {
      if (!parent_style.ShouldApplyAnyContainment(*document_element) &&
          !style.ShouldApplyAnyContainment(*element)) {
        scrolls_overflow = false;
      }
    }
  }
  bool is_child_document = element && element == document_element &&
                           element->GetDocument().LocalOwner();
  if (scrolls_overflow || is_child_document) {
    return touch_action | TouchAction::kPan |
           TouchAction::kInternalPanXScrolls |
           TouchAction::kInternalNotWritable;
  }
  return touch_action;
}

bool HostIsInputFile(const Element* element) {
  if (!element || !element->IsInUserAgentShadowRoot())
    return false;
  if (const Element* shadow_host = element->OwnerShadowHost()) {
    if (const auto* input = DynamicTo<HTMLInputElement>(shadow_host))
      return input->type() == input_type_names::kFile;
  }
  return false;
}

void AdjustStyleForSvgElement(const SVGElement& element, ComputedStyle& style) {
  // Disable some of text decoration properties.
  //
  // Note that SetFooBar() is more efficient than ResetFooBar() if the current
  // value is same as the reset value.
  style.SetTextDecorationSkipInk(ETextDecorationSkipInk::kAuto);
  style.SetTextDecorationStyle(
      ETextDecorationStyle::kSolid);  // crbug.com/1246719
  style.SetTextDecorationThickness(TextDecorationThickness(Length::Auto()));
  style.SetTextEmphasisMark(TextEmphasisMark::kNone);
  style.SetTextUnderlineOffset(Length());  // crbug.com/1247912
  style.SetTextUnderlinePosition(kTextUnderlinePositionAuto);
}

// Adjust style for anchor() and anchor-size() queries.
void AdjustAnchorQueryStyles(ComputedStyle& style) {
  if (!RuntimeEnabledFeatures::CSSAnchorPositioningEnabled())
    return;

  // anchor() and anchor-size() can only be used on absolutely positioned
  // elements.
  if (style.GetPosition() != EPosition::kAbsolute &&
      style.GetPosition() != EPosition::kFixed) {
    if (style.Left().HasAnchorQueries())
      style.SetLeft(Length::Auto());
    if (style.Right().HasAnchorQueries())
      style.SetRight(Length::Auto());
    if (style.Top().HasAnchorQueries())
      style.SetTop(Length::Auto());
    if (style.Bottom().HasAnchorQueries())
      style.SetBottom(Length::Auto());
    if (style.Width().HasAnchorQueries())
      style.SetWidth(Length::Auto());
    if (style.MinWidth().HasAnchorQueries())
      style.SetMinWidth(Length::Auto());
    if (style.MaxWidth().HasAnchorQueries())
      style.SetMaxWidth(Length::Auto());
    if (style.Height().HasAnchorQueries())
      style.SetHeight(Length::Auto());
    if (style.MinHeight().HasAnchorQueries())
      style.SetMinHeight(Length::Auto());
    if (style.MaxHeight().HasAnchorQueries())
      style.SetMaxHeight(Length::Auto());
  }
}

// Returns the `<display-outside>` for a `EDisplay` value.
// https://drafts.csswg.org/css-display-3/#propdef-display
EDisplay DisplayOutside(EDisplay display) {
  switch (display) {
    case EDisplay::kBlock:
    case EDisplay::kTable:
    case EDisplay::kWebkitBox:
    case EDisplay::kFlex:
    case EDisplay::kGrid:
    case EDisplay::kBlockMath:
    case EDisplay::kListItem:
    case EDisplay::kFlowRoot:
    case EDisplay::kLayoutCustom:
    case EDisplay::kTableRowGroup:
    case EDisplay::kTableHeaderGroup:
    case EDisplay::kTableFooterGroup:
    case EDisplay::kTableRow:
    case EDisplay::kTableColumnGroup:
    case EDisplay::kTableColumn:
    case EDisplay::kTableCell:
    case EDisplay::kTableCaption:
      return EDisplay::kBlock;
    case EDisplay::kInline:
    case EDisplay::kInlineBlock:
    case EDisplay::kInlineTable:
    case EDisplay::kWebkitInlineBox:
    case EDisplay::kInlineFlex:
    case EDisplay::kInlineGrid:
    case EDisplay::kInlineLayoutCustom:
    case EDisplay::kMath:
      return EDisplay::kInline;
    case EDisplay::kNone:
    case EDisplay::kContents:
      // These values don't have `<display-outside>`.
      // Returns the original value.
      return display;
  }
  NOTREACHED();
  return EDisplay::kBlock;
}

}  // namespace

static EDisplay EquivalentBlockDisplay(EDisplay display) {
  switch (display) {
    case EDisplay::kBlock:
    case EDisplay::kTable:
    case EDisplay::kWebkitBox:
    case EDisplay::kFlex:
    case EDisplay::kGrid:
    case EDisplay::kBlockMath:
    case EDisplay::kListItem:
    case EDisplay::kFlowRoot:
    case EDisplay::kLayoutCustom:
      return display;
    case EDisplay::kInlineTable:
      return EDisplay::kTable;
    case EDisplay::kWebkitInlineBox:
      return EDisplay::kWebkitBox;
    case EDisplay::kInlineFlex:
      return EDisplay::kFlex;
    case EDisplay::kInlineGrid:
      return EDisplay::kGrid;
    case EDisplay::kMath:
      return EDisplay::kBlockMath;
    case EDisplay::kInlineLayoutCustom:
      return EDisplay::kLayoutCustom;

    case EDisplay::kContents:
    case EDisplay::kInline:
    case EDisplay::kInlineBlock:
    case EDisplay::kTableRowGroup:
    case EDisplay::kTableHeaderGroup:
    case EDisplay::kTableFooterGroup:
    case EDisplay::kTableRow:
    case EDisplay::kTableColumnGroup:
    case EDisplay::kTableColumn:
    case EDisplay::kTableCell:
    case EDisplay::kTableCaption:
      return EDisplay::kBlock;
    case EDisplay::kNone:
      NOTREACHED();
      return display;
  }
  NOTREACHED();
  return EDisplay::kBlock;
}

static bool IsOutermostSVGElement(const Element* element) {
  auto* svg_element = DynamicTo<SVGElement>(element);
  return svg_element && svg_element->IsOutermostSVGSVGElement();
}

static bool IsAtMediaUAShadowBoundary(const Element* element) {
  if (!element)
    return false;
  if (ContainerNode* parent = element->parentNode()) {
    if (auto* shadow_root = DynamicTo<ShadowRoot>(parent))
      return shadow_root->host().IsMediaElement();
  }
  return false;
}

// CSS requires text-decoration to be reset at each DOM element for inline
// blocks, inline tables, floating elements, and absolute or relatively
// positioned elements. Outermost <svg> roots are considered to be atomic
// inline-level. Media elements have a special rendering where the media
// controls do not use a proper containing block model which means we need
// to manually stop text-decorations to apply to text inside media controls.
static bool StopPropagateTextDecorations(const ComputedStyle& style,
                                         const Element* element) {
  return style.Display() == EDisplay::kInlineTable ||
         style.Display() == EDisplay::kInlineBlock ||
         style.Display() == EDisplay::kWebkitInlineBox ||
         IsAtMediaUAShadowBoundary(element) || style.IsFloating() ||
         style.HasOutOfFlowPosition() || IsOutermostSVGElement(element) ||
         IsA<HTMLRTElement>(element);
}

// Certain elements (<a>, <font>) override text decoration colors.  "The font
// element is expected to override the color of any text decoration that spans
// the text of the element to the used value of the element's 'color' property."
// (https://html.spec.whatwg.org/C/#phrasing-content-3)
// The <a> behavior is non-standard.
static bool OverridesTextDecorationColors(const Element* element) {
  return element &&
         (IsA<HTMLFontElement>(element) || IsA<HTMLAnchorElement>(element));
}

// FIXME: This helper is only needed because ResolveStyle passes a null
// element to AdjustComputedStyle for pseudo-element styles, so we can't just
// use element->isInTopLayer().
static bool IsInTopLayer(const Element* element, const ComputedStyle& style) {
  return (element && element->IsInTopLayer()) ||
         style.StyleType() == kPseudoIdBackdrop;
}

static bool LayoutParentStyleForcesZIndexToCreateStackingContext(
    const ComputedStyle& layout_parent_style) {
  return layout_parent_style.IsDisplayFlexibleOrGridBox();
}

void StyleAdjuster::AdjustStyleForEditing(ComputedStyle& style) {
  if (style.UserModify() != EUserModify::kReadWritePlaintextOnly)
    return;
  // Collapsing whitespace is harmful in plain-text editing.
  if (style.WhiteSpace() == EWhiteSpace::kNormal)
    style.SetWhiteSpace(EWhiteSpace::kPreWrap);
  else if (style.WhiteSpace() == EWhiteSpace::kNowrap)
    style.SetWhiteSpace(EWhiteSpace::kPre);
  else if (style.WhiteSpace() == EWhiteSpace::kPreLine)
    style.SetWhiteSpace(EWhiteSpace::kPreWrap);
}

void StyleAdjuster::AdjustStyleForTextCombine(ComputedStyle& style) {
  DCHECK_EQ(style.Display(), EDisplay::kInlineBlock);
  // Set box sizes
  const Font& font = style.GetFont();
  DCHECK(font.GetFontDescription().IsVerticalBaseline());
  const auto one_em = style.ComputedFontSizeAsFixed();
  const auto line_height = style.GetFontHeight().LineHeight();
  const auto size =
      LengthSize(Length::Fixed(line_height), Length::Fixed(one_em));
  style.SetContainIntrinsicWidth(StyleIntrinsicLength(false, size.Width()));
  style.SetContainIntrinsicHeight(StyleIntrinsicLength(false, size.Height()));
  style.SetHeight(size.Height());
  style.SetLineHeight(size.Height());
  style.SetMaxHeight(size.Height());
  style.SetMaxWidth(size.Width());
  style.SetMinHeight(size.Height());
  style.SetMinWidth(size.Width());
  style.SetWidth(size.Width());
  AdjustStyleForCombinedText(style);
}

void StyleAdjuster::AdjustStyleForCombinedText(ComputedStyle& style) {
  style.ResetTextCombine();
  style.SetLetterSpacing(0.0f);
  style.SetTextAlign(ETextAlign::kCenter);
  style.SetTextDecorationLine(TextDecorationLine::kNone);
  style.SetTextEmphasisMark(TextEmphasisMark::kNone);
  style.SetVerticalAlign(EVerticalAlign ::kMiddle);
  style.SetWordBreak(EWordBreak::kKeepAll);
  style.SetWordSpacing(0.0f);
  style.SetWritingMode(WritingMode::kHorizontalTb);

  style.ClearAppliedTextDecorations();
  style.ResetTextIndent();
  style.UpdateFontOrientation();

  DCHECK_EQ(style.GetFont().GetFontDescription().Orientation(),
            FontOrientation::kHorizontal);

  LayoutNGTextCombine::AssertStyleIsValid(style);
}

static void AdjustStyleForFirstLetter(ComputedStyle& style) {
  if (style.StyleType() != kPseudoIdFirstLetter)
    return;

  // Force inline display (except for floating first-letters).
  style.SetDisplay(style.IsFloating() ? EDisplay::kBlock : EDisplay::kInline);
}

static void AdjustStyleForMarker(ComputedStyle& style,
                                 const ComputedStyle& parent_style,
                                 const Element& parent_element) {
  if (style.StyleType() != kPseudoIdMarker)
    return;

  bool is_inside =
      parent_style.ListStylePosition() == EListStylePosition::kInside ||
      (IsA<HTMLLIElement>(parent_element) &&
       !parent_style.IsInsideListElement());

  if (is_inside) {
    Document& document = parent_element.GetDocument();
    auto margins =
        ListMarker::InlineMarginsForInside(document, style, parent_style);
    style.SetMarginStart(Length::Fixed(margins.first));
    style.SetMarginEnd(Length::Fixed(margins.second));
  } else {
    // Outside list markers should generate a block container.
    style.SetDisplay(EDisplay::kInlineBlock);

    // Do not break inside the marker, and honor the trailing spaces.
    style.SetWhiteSpace(EWhiteSpace::kPre);

    // Compute margins for 'outside' during layout, because it requires the
    // layout size of the marker.
    // TODO(kojii): absolute position looks more reasonable, and maybe required
    // in some cases, but this is currently blocked by crbug.com/734554
    // style.SetPosition(EPosition::kAbsolute);
  }
}

static void AdjustStyleForHTMLElement(ComputedStyle& style,
                                      HTMLElement& element) {
  // <div> and <span> are the most common elements on the web, we skip all the
  // work for them.
  if (IsA<HTMLDivElement>(element) || IsA<HTMLSpanElement>(element))
    return;

  if (auto* image = DynamicTo<HTMLImageElement>(element)) {
    if (image->IsCollapsed() || style.Display() == EDisplay::kContents)
      style.SetDisplay(EDisplay::kNone);
    return;
  }

  if (IsA<HTMLTableElement>(element)) {
    // Tables never support the -webkit-* values for text-align and will reset
    // back to the default.
    if (style.GetTextAlign() == ETextAlign::kWebkitLeft ||
        style.GetTextAlign() == ETextAlign::kWebkitCenter ||
        style.GetTextAlign() == ETextAlign::kWebkitRight)
      style.SetTextAlign(ETextAlign::kStart);
    return;
  }

  if (IsA<HTMLFrameElement>(element) || IsA<HTMLFrameSetElement>(element)) {
    // Frames and framesets never honor position:relative or position:absolute.
    // This is necessary to fix a crash where a site tries to position these
    // objects. They also never honor display nor floating.
    style.SetPosition(EPosition::kStatic);
    style.SetDisplay(EDisplay::kBlock);
    style.SetFloating(EFloat::kNone);
    return;
  }

  if (IsA<HTMLFrameElementBase>(element)) {
    if (style.Display() == EDisplay::kContents) {
      style.SetDisplay(EDisplay::kNone);
      return;
    }
    return;
  }

  if (IsA<HTMLFencedFrameElement>(element)) {
    // Force the CSS style `zoom` property to 1 so that the embedder cannot
    // communicate into the fenced frame by adjusting it, but still include
    // the page zoom factor in the effective zoom, which is safe because it
    // comes from user intervention. crbug.com/1285327
    style.SetEffectiveZoom(
        element.GetDocument().GetStyleResolver().InitialZoom());

    if (!features::IsFencedFramesMPArchBased()) {
      // Force the inside-display to `flow`, but honors the outside-display.
      switch (DisplayOutside(style.Display())) {
        case EDisplay::kInline:
        case EDisplay::kContents:
          style.SetDisplay(EDisplay::kInlineBlock);
          break;
        case EDisplay::kBlock:
          style.SetDisplay(EDisplay::kBlock);
          break;
        case EDisplay::kNone:
          break;
        default:
          NOTREACHED();
          style.SetDisplay(EDisplay::kInlineBlock);
          break;
      }
    }
  }

  if (IsA<HTMLRTElement>(element)) {
    // Ruby text does not support float or position. This might change with
    // evolution of the specification.
    style.SetPosition(EPosition::kStatic);
    style.SetFloating(EFloat::kNone);
    return;
  }

  if (IsA<HTMLLegendElement>(element) &&
      style.Display() != EDisplay::kContents) {
    // Allow any blockified display value for legends. Note that according to
    // the spec, this shouldn't affect computed style (like we do here).
    // Instead, the display override should be determined during box creation,
    // and even then only be applied to the rendered legend inside a
    // fieldset. However, Blink determines the rendered legend during layout
    // instead of during layout object creation, and also generally makes
    // assumptions that the computed display value is the one to use.
    style.SetDisplay(EquivalentBlockDisplay(style.Display()));
    return;
  }

  if (IsA<HTMLMarqueeElement>(element)) {
    // For now, <marquee> requires an overflow clip to work properly.
    style.SetOverflowX(EOverflow::kHidden);
    style.SetOverflowY(EOverflow::kHidden);
    return;
  }

  if (IsA<HTMLTextAreaElement>(element)) {
    // Textarea considers overflow visible as auto.
    style.SetOverflowX(style.OverflowX() == EOverflow::kVisible
                           ? EOverflow::kAuto
                           : style.OverflowX());
    style.SetOverflowY(style.OverflowY() == EOverflow::kVisible
                           ? EOverflow::kAuto
                           : style.OverflowY());
    if (style.Display() == EDisplay::kContents)
      style.SetDisplay(EDisplay::kNone);
    return;
  }

  if (auto* html_plugin_element = DynamicTo<HTMLPlugInElement>(element)) {
    style.SetRequiresAcceleratedCompositingForExternalReasons(
        html_plugin_element->ShouldAccelerate());
    if (style.Display() == EDisplay::kContents)
      style.SetDisplay(EDisplay::kNone);
    return;
  }

  if (IsA<HTMLUListElement>(element) || IsA<HTMLOListElement>(element)) {
    style.SetIsInsideListElement();
    return;
  }

  if (style.Display() == EDisplay::kContents) {
    // See https://drafts.csswg.org/css-display/#unbox-html
    // Some of these elements are handled with other adjustments above.
    if (IsA<HTMLBRElement>(element) || IsA<HTMLWBRElement>(element) ||
        IsA<HTMLMeterElement>(element) || IsA<HTMLProgressElement>(element) ||
        IsA<HTMLCanvasElement>(element) || IsA<HTMLMediaElement>(element) ||
        IsA<HTMLInputElement>(element) || IsA<HTMLTextAreaElement>(element) ||
        IsA<HTMLSelectElement>(element)) {
      style.SetDisplay(EDisplay::kNone);
    }
  }

  if (IsA<HTMLBodyElement>(element) &&
      element.GetDocument().FirstBodyElement() != element) {
    style.SetIsSecondaryBodyElement();
  }
}

void StyleAdjuster::AdjustOverflow(ComputedStyle& style, Element* element) {
  DCHECK(style.OverflowX() != EOverflow::kVisible ||
         style.OverflowY() != EOverflow::kVisible);

  bool overflow_is_clip_or_visible =
      IsOverflowClipOrVisible(style.OverflowY()) &&
      IsOverflowClipOrVisible(style.OverflowX());
  if (!overflow_is_clip_or_visible && style.IsDisplayTableBox()) {
    // Tables only support overflow:hidden and overflow:visible and ignore
    // anything else, see https://drafts.csswg.org/css2/visufx.html#overflow. As
    // a table is not a block container box the rules for resolving conflicting
    // x and y values in CSS Overflow Module Level 3 do not apply. Arguably
    // overflow-x and overflow-y aren't allowed on tables but all UAs allow it.
    if (style.OverflowX() != EOverflow::kHidden)
      style.SetOverflowX(EOverflow::kVisible);
    if (style.OverflowY() != EOverflow::kHidden)
      style.SetOverflowY(EOverflow::kVisible);
    // If we are left with conflicting overflow values for the x and y axes on a
    // table then resolve both to OverflowVisible. This is interoperable
    // behaviour but is not specced anywhere.
    if (style.OverflowX() == EOverflow::kVisible)
      style.SetOverflowY(EOverflow::kVisible);
    else if (style.OverflowY() == EOverflow::kVisible)
      style.SetOverflowX(EOverflow::kVisible);
  } else if (!IsOverflowClipOrVisible(style.OverflowY())) {
    // Values of 'clip' and 'visible' can only be used with 'clip' and
    // 'visible.' If they aren't, 'clip' and 'visible' is reset.
    if (style.OverflowX() == EOverflow::kVisible)
      style.SetOverflowX(EOverflow::kAuto);
    else if (style.OverflowX() == EOverflow::kClip)
      style.SetOverflowX(EOverflow::kHidden);
  } else if (!IsOverflowClipOrVisible(style.OverflowX())) {
    // Values of 'clip' and 'visible' can only be used with 'clip' and
    // 'visible.' If they aren't, 'clip' and 'visible' is reset.
    if (style.OverflowY() == EOverflow::kVisible)
      style.SetOverflowY(EOverflow::kAuto);
    else if (style.OverflowY() == EOverflow::kClip)
      style.SetOverflowY(EOverflow::kHidden);
  }

  if (element && !element->IsPseudoElement() &&
      (style.OverflowX() == EOverflow::kClip ||
       style.OverflowY() == EOverflow::kClip)) {
    UseCounter::Count(element->GetDocument(),
                      WebFeature::kOverflowClipAlongEitherAxis);
  }
}

static void AdjustStyleForDisplay(ComputedStyle& style,
                                  const ComputedStyle& layout_parent_style,
                                  const Element* element,
                                  Document* document) {
  // Blockify the children of flex, grid or LayoutCustom containers.
  if (layout_parent_style.BlockifiesChildren() && !HostIsInputFile(element)) {
    style.SetIsInBlockifyingDisplay();
    if (style.Display() != EDisplay::kContents) {
      style.SetDisplay(EquivalentBlockDisplay(style.Display()));
      if (!style.HasOutOfFlowPosition())
        style.SetIsFlexOrGridOrCustomItem();
    }
    if (layout_parent_style.IsDisplayFlexibleOrGridBox())
      style.SetIsFlexOrGridItem();
  }

  if (style.Display() == EDisplay::kBlock)
    return;

  // FIXME: Don't support this mutation for pseudo styles like first-letter or
  // first-line, since it's not completely clear how that should work.
  if (style.Display() == EDisplay::kInline &&
      style.StyleType() == kPseudoIdNone &&
      style.GetWritingMode() != layout_parent_style.GetWritingMode())
    style.SetDisplay(EDisplay::kInlineBlock);

  // writing-mode does not apply to table row groups, table column groups, table
  // rows, and table columns.
  // TODO(crbug.com/736072): Borders specified with logical css properties will
  // not change to reflect new writing mode. ex: border-block-start.
  if (style.Display() == EDisplay::kTableColumn ||
      style.Display() == EDisplay::kTableColumnGroup ||
      style.Display() == EDisplay::kTableFooterGroup ||
      style.Display() == EDisplay::kTableHeaderGroup ||
      style.Display() == EDisplay::kTableRow ||
      style.Display() == EDisplay::kTableRowGroup) {
    style.SetWritingMode(layout_parent_style.GetWritingMode());
    style.SetTextOrientation(layout_parent_style.GetTextOrientation());
    style.UpdateFontOrientation();
  }
}

bool StyleAdjuster::IsEditableElement(Element* element,
                                      const ComputedStyle& style) {
  if (style.UserModify() != EUserModify::kReadOnly)
    return true;

  if (!element)
    return false;

  if (auto* textarea = DynamicTo<HTMLTextAreaElement>(*element))
    return !textarea->IsDisabledOrReadOnly();

  if (auto* input = DynamicTo<HTMLInputElement>(*element))
    return !input->IsDisabledOrReadOnly() && input->IsTextField();

  return false;
}

bool StyleAdjuster::IsPasswordFieldWithUnrevealedPassword(Element* element) {
  if (!element)
    return false;
  if (auto* input = DynamicTo<HTMLInputElement>(element)) {
    return (input->type() == input_type_names::kPassword) &&
           !input->ShouldRevealPassword();
  }
  return false;
}

void StyleAdjuster::AdjustEffectiveTouchAction(
    ComputedStyle& style,
    const ComputedStyle& parent_style,
    Element* element,
    bool is_svg_root) {
  TouchAction inherited_action = parent_style.GetEffectiveTouchAction();

  bool is_replaced_canvas = element && IsA<HTMLCanvasElement>(element) &&
                            element->GetExecutionContext() &&
                            element->GetExecutionContext()->CanExecuteScripts(
                                kNotAboutToExecuteScript);
  bool is_non_replaced_inline_elements =
      style.IsDisplayInlineType() &&
      !(style.IsDisplayReplacedType() || is_svg_root ||
        IsA<HTMLImageElement>(element) || is_replaced_canvas);
  bool is_table_row_or_column = style.IsDisplayTableRowOrColumnType();
  bool is_layout_object_needed =
      element && element->LayoutObjectIsNeeded(style);

  TouchAction element_touch_action = TouchAction::kAuto;
  // Touch actions are only supported by elements that support both the CSS
  // width and height properties.
  // See https://www.w3.org/TR/pointerevents/#the-touch-action-css-property.
  if (!is_non_replaced_inline_elements && !is_table_row_or_column &&
      is_layout_object_needed) {
    element_touch_action = style.GetTouchAction();
    // kInternalPanXScrolls is only for internal usage, GetTouchAction()
    // doesn't contain this bit. We set this bit when kPanX is set so it can be
    // cleared for eligible editable areas later on.
    if ((element_touch_action & TouchAction::kPanX) != TouchAction::kNone) {
      element_touch_action |= TouchAction::kInternalPanXScrolls;
    }

    // kInternalNotWritable is only for internal usage, GetTouchAction()
    // doesn't contain this bit. We set this bit when kPan is set so it can be
    // cleared for eligible non-password editable areas later on.
    if ((element_touch_action & TouchAction::kPan) != TouchAction::kNone)
      element_touch_action |= TouchAction::kInternalNotWritable;
  }

  if (!element) {
    style.SetEffectiveTouchAction(element_touch_action & inherited_action);
    return;
  }

  bool is_child_document = element == element->GetDocument().documentElement();

  // Apply touch action inherited from parent frame.
  if (is_child_document && element->GetDocument().GetFrame()) {
    inherited_action &=
        TouchAction::kPan | TouchAction::kInternalPanXScrolls |
        TouchAction::kInternalNotWritable |
        element->GetDocument().GetFrame()->InheritedEffectiveTouchAction();
  }

  // The effective touch action is the intersection of the touch-action values
  // of the current element and all of its ancestors up to the one that
  // implements the gesture. Since panning is implemented by the scroller it is
  // re-enabled for scrolling elements.
  // The panning-restricted cancellation should also apply to iframes, so we
  // allow (panning & local touch action) on the first descendant element of a
  // iframe element.
  inherited_action = AdjustTouchActionForElement(inherited_action, style,
                                                 parent_style, element);

  TouchAction enforced_by_policy = TouchAction::kNone;
  if (element->GetDocument().IsVerticalScrollEnforced())
    enforced_by_policy = TouchAction::kPanY;
  if (::features::IsSwipeToMoveCursorEnabled() &&
      IsEditableElement(element, style)) {
    element_touch_action &= ~TouchAction::kInternalPanXScrolls;
  }

  // TODO(crbug.com/1346169): Full style invalidation is needed when this
  // feature status changes at runtime as it affects the computed style.
  if (base::FeatureList::IsEnabled(blink::features::kStylusWritingToInput) &&
      RuntimeEnabledFeatures::StylusHandwritingEnabled() &&
      (element_touch_action & TouchAction::kPan) == TouchAction::kPan &&
      IsEditableElement(element, style) &&
      !IsPasswordFieldWithUnrevealedPassword(element)) {
    element_touch_action &= ~TouchAction::kInternalNotWritable;
  }

  // Apply the adjusted parent effective touch actions.
  style.SetEffectiveTouchAction((element_touch_action & inherited_action) |
                                enforced_by_policy);

  // Propagate touch action to child frames.
  if (auto* frame_owner = DynamicTo<HTMLFrameOwnerElement>(element)) {
    Frame* content_frame = frame_owner->ContentFrame();
    if (content_frame) {
      content_frame->SetInheritedEffectiveTouchAction(
          style.GetEffectiveTouchAction());
    }
  }
}

static void AdjustStyleForInert(ComputedStyle& style, Element* element) {
  if (!element)
    return;

  if (element->IsInertRoot()) {
    style.SetIsInert(true);
    style.SetIsInertIsInherited(false);
    return;
  }

  Document& document = element->GetDocument();
  const Element* modal_element = document.ActiveModalDialog();
  if (!modal_element)
    modal_element = Fullscreen::FullscreenElementFrom(document);
  if (modal_element == element) {
    style.SetIsInert(false);
    style.SetIsInertIsInherited(false);
    return;
  }
  if (modal_element && element == document.documentElement()) {
    style.SetIsInert(true);
    style.SetIsInertIsInherited(false);
    return;
  }
}

void StyleAdjuster::AdjustForForcedColorsMode(ComputedStyle& style) {
  if (!style.InForcedColorsMode() ||
      style.ForcedColorAdjust() != EForcedColorAdjust::kAuto)
    return;

  style.SetTextShadow(ComputedStyleInitialValues::InitialTextShadow());
  style.SetBoxShadow(ComputedStyleInitialValues::InitialBoxShadow());
  style.SetColorScheme({"light", "dark"});
  if (style.ShouldForceColor(style.AccentColor()))
    style.SetAccentColor(ComputedStyleInitialValues::InitialAccentColor());
  if (!style.HasUrlBackgroundImage())
    style.ClearBackgroundImage();
}

void StyleAdjuster::AdjustComputedStyle(StyleResolverState& state,
                                        Element* element) {
  DCHECK(state.LayoutParentStyle());
  DCHECK(state.ParentStyle());
  ComputedStyle& style = state.StyleRef();
  const ComputedStyle& parent_style = *state.ParentStyle();
  const ComputedStyle& layout_parent_style = *state.LayoutParentStyle();

  auto* html_element = DynamicTo<HTMLElement>(element);
  if (html_element && (style.Display() != EDisplay::kNone ||
                       element->LayoutObjectIsNeeded(style))) {
    AdjustStyleForHTMLElement(style, *html_element);
  }

  auto* svg_element = DynamicTo<SVGElement>(element);

  bool is_mathml_element = RuntimeEnabledFeatures::MathMLCoreEnabled() &&
                           IsA<MathMLElement>(element);

  if (style.Display() != EDisplay::kNone) {
    if (svg_element)
      AdjustStyleForSvgElement(*svg_element, style);

    bool is_document_element =
        element && element->GetDocument().documentElement() == element;
    // Per the spec, position 'static' and 'relative' in the top layer compute
    // to 'absolute'. Root elements that are in the top layer should just
    // be left alone because the fullscreen.css doesn't apply any style to
    // them.
    if (IsInTopLayer(element, style) && !is_document_element) {
      if (style.GetPosition() == EPosition::kStatic ||
          style.GetPosition() == EPosition::kRelative) {
        style.SetPosition(EPosition::kAbsolute);
      }
      if (style.Display() == EDisplay::kContents) {
        // See crbug.com/1240701 for more details.
        // https://fullscreen.spec.whatwg.org/#new-stacking-layer
        // If its specified display property is contents, it computes to block.
        style.SetDisplay(EDisplay::kBlock);
      }
    }

    // Absolute/fixed positioned elements, floating elements and the document
    // element need block-like outside display.
    if (style.Display() != EDisplay::kContents &&
        (style.HasOutOfFlowPosition() || style.IsFloating()))
      style.SetDisplay(EquivalentBlockDisplay(style.Display()));

    if (is_document_element)
      style.SetDisplay(EquivalentBlockDisplay(style.Display()));

    // math display values on non-MathML elements compute to flow display
    // values.
    if ((!element || !is_mathml_element) &&
        style.IsDisplayMathBox(style.Display())) {
      DCHECK(RuntimeEnabledFeatures::MathMLCoreEnabled());
      style.SetDisplay(style.Display() == EDisplay::kBlockMath
                           ? EDisplay::kBlock
                           : EDisplay::kInline);
    }

    // We don't adjust the first letter style earlier because we may change the
    // display setting in AdjustStyleForHTMLElement() above.
    AdjustStyleForFirstLetter(style);
    AdjustStyleForMarker(style, parent_style, state.GetElement());

    AdjustStyleForDisplay(style, layout_parent_style, element,
                          element ? &element->GetDocument() : nullptr);

    // If this is a child of a LayoutNGCustom, we need the name of the parent
    // layout function for invalidation purposes.
    if (layout_parent_style.IsDisplayLayoutCustomBox()) {
      style.SetDisplayLayoutCustomParentName(
          layout_parent_style.DisplayLayoutCustomName());
    }

    bool is_in_main_frame = element && element->GetDocument().IsInMainFrame();
    // The root element of the main frame has no backdrop, so don't allow
    // it to have a backdrop filter either.
    if (is_document_element && is_in_main_frame && style.HasBackdropFilter())
      style.MutableBackdropFilter().clear();
  } else {
    AdjustStyleForFirstLetter(style);
  }

  // Make sure our z-index value is only applied if the object is positioned.
  if (style.GetPosition() == EPosition::kStatic &&
      !LayoutParentStyleForcesZIndexToCreateStackingContext(
          layout_parent_style)) {
    style.SetIsStackingContextWithoutContainment(false);
    if (!style.HasAutoZIndex())
      style.SetEffectiveZIndexZero(true);
  } else if (!style.HasAutoZIndex()) {
    style.SetIsStackingContextWithoutContainment(true);
  }

  if (style.OverflowX() != EOverflow::kVisible ||
      style.OverflowY() != EOverflow::kVisible)
    AdjustOverflow(style, element ? element : state.GetPseudoElement());

  // Highlight pseudos propagate decorations with inheritance only.
  if (StopPropagateTextDecorations(style, element) || state.IsForHighlight())
    style.ClearAppliedTextDecorations();
  else
    style.RestoreParentTextDecorations(layout_parent_style);

  // The computed value of currentColor for highlight pseudos is the
  // color that would have been used if no highlights were applied,
  // i.e. the originating element's color.
  if (state.UsesHighlightPseudoInheritance() &&
      state.OriginatingElementStyle()) {
    const ComputedStyle* originating_style = state.OriginatingElementStyle();
    if (style.ColorIsCurrentColor())
      style.SetColor(originating_style->GetColor());
    if (style.InternalVisitedColorIsCurrentColor())
      style.SetInternalVisitedColor(originating_style->InternalVisitedColor());
  }

  if (style.Display() != EDisplay::kContents) {
    style.ApplyTextDecorations(
        parent_style.VisitedDependentColor(GetCSSPropertyTextDecorationColor()),
        OverridesTextDecorationColors(element));
  }

  // Cull out any useless layers and also repeat patterns into additional
  // layers.
  style.AdjustBackgroundLayers();
  style.AdjustMaskLayers();

  // A subset of CSS properties should be forced at computed value time:
  // https://drafts.csswg.org/css-color-adjust-1/#forced-colors-properties.
  AdjustForForcedColorsMode(style);

  // Let the theme also have a crack at adjusting the style.
  LayoutTheme::GetTheme().AdjustStyle(element, style);

  AdjustStyleForInert(style, element);

  AdjustStyleForEditing(style);

  bool is_svg_root = false;

  if (svg_element) {
    is_svg_root = svg_element->IsOutermostSVGSVGElement();
    if (!is_svg_root) {
      // Only the root <svg> element in an SVG document fragment tree honors css
      // position.
      style.SetPosition(ComputedStyleInitialValues::InitialPosition());
    }

    if (style.Display() == EDisplay::kContents &&
        (is_svg_root ||
         (!IsA<SVGSVGElement>(element) && !IsA<SVGGElement>(element) &&
          !IsA<SVGUseElement>(element) && !IsA<SVGTSpanElement>(element)))) {
      // According to the CSS Display spec[1], nested <svg> elements, <g>,
      // <use>, and <tspan> elements are not rendered and their children are
      // "hoisted". For other elements display:contents behaves as display:none.
      //
      // [1] https://drafts.csswg.org/css-display/#unbox-svg
      style.SetDisplay(EDisplay::kNone);
    }

    // SVG text layout code expects us to be a block-level style element.
    if ((IsA<SVGForeignObjectElement>(*element) ||
         IsA<SVGTextElement>(*element)) &&
        style.IsDisplayInlineType())
      style.SetDisplay(EDisplay::kBlock);

    // Columns don't apply to svg text elements.
    if (IsA<SVGTextElement>(*element))
      style.ClearMultiCol();

    // Copy DominantBaseline to CssDominantBaseline without 'no-change',
    // 'reset-size', and 'use-script'.
    auto baseline = style.DominantBaseline();
    if (baseline == EDominantBaseline::kUseScript) {
      // TODO(fs): The dominant-baseline and the baseline-table components
      // are set by determining the predominant script of the character data
      // content.
      baseline = EDominantBaseline::kAlphabetic;
    } else if (baseline == EDominantBaseline::kNoChange ||
               baseline == EDominantBaseline::kResetSize) {
      baseline = layout_parent_style.CssDominantBaseline();
    }
    style.SetCssDominantBaseline(baseline);

  } else if (is_mathml_element) {
    if (style.Display() == EDisplay::kContents) {
      // https://drafts.csswg.org/css-display/#unbox-mathml
      style.SetDisplay(EDisplay::kNone);
    }

    if (style.GetWritingMode() != WritingMode::kHorizontalTb) {
      // TODO(rbuis): this will not work with logical CSS properties.
      // Disable vertical writing-mode for now.
      style.SetWritingMode(WritingMode::kHorizontalTb);
      style.UpdateFontOrientation();
    }
  }

  // If this node is sticky it marks the creation of a sticky subtree, which we
  // must track to properly handle document lifecycle in some cases.
  //
  // It is possible that this node is already in a sticky subtree (i.e. we have
  // nested sticky nodes) - in that case the bit will already be set via
  // inheritance from the ancestor and there is no harm to setting it again.
  if (style.GetPosition() == EPosition::kSticky)
    style.SetSubtreeIsSticky(true);

  // If the inherited value of justify-items includes the 'legacy'
  // keyword (plus 'left', 'right' or 'center'), 'legacy' computes to
  // the the inherited value.  Otherwise, 'auto' computes to 'normal'.
  if (parent_style.JustifyItemsPositionType() == ItemPositionType::kLegacy &&
      style.JustifyItemsPosition() == ItemPosition::kLegacy) {
    style.SetJustifyItems(parent_style.JustifyItems());
  }

  AdjustEffectiveTouchAction(style, parent_style, element, is_svg_root);

  bool is_media_control =
      element && element->ShadowPseudoId().StartsWith("-webkit-media-controls");
  if (is_media_control && !style.HasEffectiveAppearance()) {
    // For compatibility reasons if the element is a media control and the
    // -webkit-appearance is none then we should clear the background image.
    style.MutableBackgroundInternal().ClearImage();
  }

  if (element && style.TextOverflow() == ETextOverflow::kEllipsis) {
    const AtomicString& pseudo_id = element->ShadowPseudoId();
    if (pseudo_id == shadow_element_names::kPseudoInputPlaceholder ||
        pseudo_id == shadow_element_names::kPseudoInternalInputSuggested) {
      TextControlElement* text_control =
          ToTextControl(element->OwnerShadowHost());
      DCHECK(text_control);
      // TODO(futhark@chromium.org): We force clipping text overflow for focused
      // input elements since we don't want to render ellipsis during editing.
      // We should do this as a general solution which also includes
      // contenteditable elements being edited. The computed style should not
      // change, but LayoutBlockFlow::ShouldTruncateOverflowingText() should
      // instead return false when text is being edited inside that block.
      // https://crbug.com/814954
      style.SetTextOverflow(text_control->ValueForTextOverflow());
    }
  }

  AdjustAnchorQueryStyles(style);

  if (!HasFullNGFragmentationSupport()) {
    // When establishing a block fragmentation context for LayoutNG, we require
    // that everything fragmentable inside can be laid out by NG natively, since
    // NG and legacy layout cannot cooperate within the same fragmentation
    // context. And vice versa (everything inside a legacy fragmentation context
    // needs to be legacy objects, in order to be fragmentable). Set a flag, so
    // that we can quickly determine whether we need to check that an element is
    // compatible with the block fragmentation implementation being used.
    if (style.SpecifiesColumns() ||
        (element && element->GetDocument().Printing()))
      style.SetInsideFragmentationContextWithNondeterministicEngine(true);
  }
}
}  // namespace blink
