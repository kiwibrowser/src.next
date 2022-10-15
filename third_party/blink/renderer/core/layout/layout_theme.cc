/**
 * This file is part of the theme implementation for form controls in WebCore.
 *
 * Copyright (C) 2005, 2006, 2007, 2008, 2009, 2010, 2012 Apple Computer, Inc.
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

#include "third_party/blink/renderer/core/layout/layout_theme.h"

#include "build/build_config.h"
#include "third_party/blink/public/strings/grit/blink_strings.h"
#include "third_party/blink/public/web/blink.h"
#include "third_party/blink/renderer/core/css_value_keywords.h"
#include "third_party/blink/renderer/core/dom/document.h"
#include "third_party/blink/renderer/core/dom/shadow_root.h"
#include "third_party/blink/renderer/core/editing/frame_selection.h"
#include "third_party/blink/renderer/core/fileapi/file.h"
#include "third_party/blink/renderer/core/frame/local_frame.h"
#include "third_party/blink/renderer/core/frame/settings.h"
#include "third_party/blink/renderer/core/html/forms/html_data_list_element.h"
#include "third_party/blink/renderer/core/html/forms/html_data_list_options_collection.h"
#include "third_party/blink/renderer/core/html/forms/html_form_control_element.h"
#include "third_party/blink/renderer/core/html/forms/html_input_element.h"
#include "third_party/blink/renderer/core/html/forms/html_option_element.h"
#include "third_party/blink/renderer/core/html/forms/html_select_element.h"
#include "third_party/blink/renderer/core/html/forms/spin_button_element.h"
#include "third_party/blink/renderer/core/html/forms/text_control_inner_elements.h"
#include "third_party/blink/renderer/core/html/html_collection.h"
#include "third_party/blink/renderer/core/html/parser/html_parser_idioms.h"
#include "third_party/blink/renderer/core/html/shadow/shadow_element_names.h"
#include "third_party/blink/renderer/core/html/shadow/shadow_element_utils.h"
#include "third_party/blink/renderer/core/html_names.h"
#include "third_party/blink/renderer/core/input_type_names.h"
#include "third_party/blink/renderer/core/layout/layout_box.h"
#include "third_party/blink/renderer/core/layout/layout_theme_font_provider.h"
#include "third_party/blink/renderer/core/layout/layout_theme_mobile.h"
#include "third_party/blink/renderer/core/page/focus_controller.h"
#include "third_party/blink/renderer/core/page/page.h"
#include "third_party/blink/renderer/core/style/computed_style.h"
#include "third_party/blink/renderer/core/style/computed_style_initial_values.h"
#include "third_party/blink/renderer/platform/file_metadata.h"
#include "third_party/blink/renderer/platform/fonts/font_selector.h"
#include "third_party/blink/renderer/platform/graphics/touch_action.h"
#include "third_party/blink/renderer/platform/runtime_enabled_features.h"
#include "third_party/blink/renderer/platform/theme/web_theme_engine_helper.h"
#include "third_party/blink/renderer/platform/web_test_support.h"
#include "third_party/blink/renderer/platform/wtf/text/string_builder.h"
#include "ui/base/ui_base_features.h"
#include "ui/native_theme/native_theme.h"

// The methods in this file are shared by all themes on every platform.

namespace blink {

namespace {

// This function should match to the user-agent stylesheet.
ControlPart AutoAppearanceFor(const Element& element) {
  if (IsA<HTMLButtonElement>(element))
    return kButtonPart;
  if (IsA<HTMLMeterElement>(element))
    return kMeterPart;
  if (IsA<HTMLProgressElement>(element))
    return kProgressBarPart;
  if (IsA<HTMLTextAreaElement>(element))
    return kTextAreaPart;
  if (IsA<SpinButtonElement>(element))
    return kInnerSpinButtonPart;
  if (const auto* select = DynamicTo<HTMLSelectElement>(element))
    return select->UsesMenuList() ? kMenulistPart : kListboxPart;

  if (const auto* input = DynamicTo<HTMLInputElement>(element))
    return input->AutoAppearance();

  if (element.IsInUserAgentShadowRoot()) {
    const AtomicString& id_value =
        element.FastGetAttribute(html_names::kIdAttr);
    if (id_value == shadow_element_names::kIdSliderThumb)
      return kSliderThumbHorizontalPart;
    if (id_value == shadow_element_names::kIdSearchClearButton ||
        id_value == shadow_element_names::kIdClearButton)
      return kSearchFieldCancelButtonPart;

    // Slider container elements and -webkit-meter-inner-element don't have IDs.
    if (IsSliderContainer(element))
      return kSliderHorizontalPart;
    if (element.ShadowPseudoId() ==
        shadow_element_names::kPseudoMeterInnerElement)
      return kMeterPart;
  }
  return kNoControlPart;
}

}  // namespace

LayoutTheme& LayoutTheme::GetTheme() {
  if (RuntimeEnabledFeatures::MobileLayoutThemeEnabled()) {
    DEFINE_STATIC_REF(LayoutTheme, layout_theme_mobile,
                      (LayoutThemeMobile::Create()));
    return *layout_theme_mobile;
  }
  return NativeTheme();
}

LayoutTheme::LayoutTheme() : has_custom_focus_ring_color_(false) {
  UpdateForcedColorsState();
}

ControlPart LayoutTheme::AdjustAppearanceWithAuthorStyle(
    ControlPart part,
    const ComputedStyle& style) {
  if (IsControlStyled(part, style))
    return part == kMenulistPart ? kMenulistButtonPart : kNoControlPart;
  return part;
}

ControlPart LayoutTheme::AdjustAppearanceWithElementType(
    const ComputedStyle& style,
    const Element* element) {
  ControlPart part = style.EffectiveAppearance();
  if (!element)
    return kNoControlPart;

  ControlPart auto_appearance = AutoAppearanceFor(*element);
  if (part == auto_appearance)
    return part;

  switch (part) {
    // No restrictions.
    case kNoControlPart:
    case kMediaSliderPart:
    case kMediaSliderThumbPart:
    case kMediaVolumeSliderPart:
    case kMediaVolumeSliderThumbPart:
    case kMediaControlPart:
      return part;

    // Aliases of 'auto'.
    // https://drafts.csswg.org/css-ui-4/#typedef-appearance-compat-auto
    case kAutoPart:
    case kCheckboxPart:
    case kRadioPart:
    case kPushButtonPart:
    case kSquareButtonPart:
    case kInnerSpinButtonPart:
    case kListboxPart:
    case kMenulistPart:
    case kMeterPart:
    case kProgressBarPart:
    case kSliderHorizontalPart:
    case kSliderThumbHorizontalPart:
    case kSearchFieldPart:
    case kSearchFieldCancelButtonPart:
    case kTextAreaPart:
      return auto_appearance;

      // The following keywords should work well for some element types
      // even if their default appearances are different from the keywords.

    case kButtonPart:
      return (auto_appearance == kPushButtonPart ||
              auto_appearance == kSquareButtonPart)
                 ? part
                 : auto_appearance;

    case kMenulistButtonPart:
      return auto_appearance == kMenulistPart ? part : auto_appearance;

    case kSliderVerticalPart:
      return auto_appearance == kSliderHorizontalPart ? part : auto_appearance;

    case kSliderThumbVerticalPart:
      return auto_appearance == kSliderThumbHorizontalPart ? part
                                                           : auto_appearance;

    case kTextFieldPart:
      if (IsA<HTMLInputElement>(*element) &&
          To<HTMLInputElement>(*element).type() == input_type_names::kSearch)
        return part;
      return auto_appearance;
  }

  return part;
}

void LayoutTheme::AdjustStyle(const Element* element, ComputedStyle& style) {
  ControlPart original_part = style.Appearance();
  style.SetEffectiveAppearance(original_part);
  if (original_part == ControlPart::kNoControlPart)
    return;

  // Force inline and table display styles to be inline-block (except for table-
  // which is block)
  if (style.Display() == EDisplay::kInline ||
      style.Display() == EDisplay::kInlineTable ||
      style.Display() == EDisplay::kTableRowGroup ||
      style.Display() == EDisplay::kTableHeaderGroup ||
      style.Display() == EDisplay::kTableFooterGroup ||
      style.Display() == EDisplay::kTableRow ||
      style.Display() == EDisplay::kTableColumnGroup ||
      style.Display() == EDisplay::kTableColumn ||
      style.Display() == EDisplay::kTableCell ||
      style.Display() == EDisplay::kTableCaption)
    style.SetDisplay(EDisplay::kInlineBlock);
  else if (style.Display() == EDisplay::kListItem ||
           style.Display() == EDisplay::kTable)
    style.SetDisplay(EDisplay::kBlock);

  ControlPart part = AdjustAppearanceWithAuthorStyle(
      AdjustAppearanceWithElementType(style, element), style);
  style.SetEffectiveAppearance(part);
  DCHECK_NE(part, kAutoPart);
  if (part == kNoControlPart)
    return;
  DCHECK(element);
  // After this point, a Node must be non-null Element if
  // EffectiveAppearance() != kNoControlPart.

  AdjustControlPartStyle(style);

  // Call the appropriate style adjustment method based off the appearance
  // value.
  switch (part) {
    case kMenulistPart:
      return AdjustMenuListStyle(style);
    case kMenulistButtonPart:
      return AdjustMenuListButtonStyle(style);
    case kSliderThumbHorizontalPart:
    case kSliderThumbVerticalPart:
      return AdjustSliderThumbStyle(style);
    case kSearchFieldCancelButtonPart:
      return AdjustSearchFieldCancelButtonStyle(style);
    default:
      break;
  }

  if (IsSliderContainer(*element))
    AdjustSliderContainerStyle(*element, style);
}

String LayoutTheme::ExtraDefaultStyleSheet() {
  return g_empty_string;
}

String LayoutTheme::ExtraFullscreenStyleSheet() {
  return String();
}

Color LayoutTheme::ActiveSelectionBackgroundColor(
    mojom::blink::ColorScheme color_scheme) const {
  Color color = PlatformActiveSelectionBackgroundColor(color_scheme);
#if BUILDFLAG(IS_MAC)
  // BlendWithWhite() darkens Mac system colors too much.
  // Apply .8 (204/255) alpha instead, same as Safari.
  if (color_scheme == mojom::blink::ColorScheme::kDark)
    return Color(color.Red(), color.Green(), color.Blue(), 204);
#endif
  return color.BlendWithWhite();
}

Color LayoutTheme::InactiveSelectionBackgroundColor(
    mojom::blink::ColorScheme color_scheme) const {
  return PlatformInactiveSelectionBackgroundColor(color_scheme)
      .BlendWithWhite();
}

Color LayoutTheme::ActiveSelectionForegroundColor(
    mojom::blink::ColorScheme color_scheme) const {
  return PlatformActiveSelectionForegroundColor(color_scheme);
}

Color LayoutTheme::InactiveSelectionForegroundColor(
    mojom::blink::ColorScheme color_scheme) const {
  return PlatformInactiveSelectionForegroundColor(color_scheme);
}

Color LayoutTheme::ActiveListBoxSelectionBackgroundColor(
    mojom::blink::ColorScheme color_scheme) const {
  return PlatformActiveListBoxSelectionBackgroundColor(color_scheme);
}

Color LayoutTheme::InactiveListBoxSelectionBackgroundColor(
    mojom::blink::ColorScheme color_scheme) const {
  return PlatformInactiveListBoxSelectionBackgroundColor(color_scheme);
}

Color LayoutTheme::ActiveListBoxSelectionForegroundColor(
    mojom::blink::ColorScheme color_scheme) const {
  return PlatformActiveListBoxSelectionForegroundColor(color_scheme);
}

Color LayoutTheme::InactiveListBoxSelectionForegroundColor(
    mojom::blink::ColorScheme color_scheme) const {
  return PlatformInactiveListBoxSelectionForegroundColor(color_scheme);
}

Color LayoutTheme::PlatformSpellingMarkerUnderlineColor() const {
  return Color(255, 0, 0);
}

Color LayoutTheme::PlatformGrammarMarkerUnderlineColor() const {
  return Color(192, 192, 192);
}

Color LayoutTheme::PlatformActiveSpellingMarkerHighlightColor() const {
  return Color(255, 0, 0, 102);
}

Color LayoutTheme::PlatformActiveSelectionBackgroundColor(
    mojom::blink::ColorScheme color_scheme) const {
  // Use a blue color by default if the platform theme doesn't define anything.
  return Color(0, 0, 255);
}

Color LayoutTheme::PlatformActiveSelectionForegroundColor(
    mojom::blink::ColorScheme color_scheme) const {
  // Use a white color by default if the platform theme doesn't define anything.
  return Color::kWhite;
}

Color LayoutTheme::PlatformInactiveSelectionBackgroundColor(
    mojom::blink::ColorScheme color_scheme) const {
  // Use a grey color by default if the platform theme doesn't define anything.
  // This color matches Firefox's inactive color.
  return Color(176, 176, 176);
}

Color LayoutTheme::PlatformInactiveSelectionForegroundColor(
    mojom::blink::ColorScheme color_scheme) const {
  // Use a black color by default.
  return Color::kBlack;
}

Color LayoutTheme::PlatformActiveListBoxSelectionBackgroundColor(
    mojom::blink::ColorScheme color_scheme) const {
  return PlatformActiveSelectionBackgroundColor(color_scheme);
}

Color LayoutTheme::PlatformActiveListBoxSelectionForegroundColor(
    mojom::blink::ColorScheme color_scheme) const {
  return PlatformActiveSelectionForegroundColor(color_scheme);
}

Color LayoutTheme::PlatformInactiveListBoxSelectionBackgroundColor(
    mojom::blink::ColorScheme color_scheme) const {
  return PlatformInactiveSelectionBackgroundColor(color_scheme);
}

Color LayoutTheme::PlatformInactiveListBoxSelectionForegroundColor(
    mojom::blink::ColorScheme color_scheme) const {
  return PlatformInactiveSelectionForegroundColor(color_scheme);
}

bool LayoutTheme::IsControlStyled(ControlPart part,
                                  const ComputedStyle& style) const {
  switch (part) {
    case kPushButtonPart:
    case kSquareButtonPart:
    case kButtonPart:
    case kProgressBarPart:
      return style.HasAuthorBackground() || style.HasAuthorBorder();

    case kMenulistPart:
    case kSearchFieldPart:
    case kTextAreaPart:
    case kTextFieldPart:
      return style.HasAuthorBackground() || style.HasAuthorBorder() ||
             style.BoxShadow();

    default:
      return false;
  }
}

bool LayoutTheme::ShouldDrawDefaultFocusRing(const Node* node,
                                             const ComputedStyle& style) const {
  if (!node)
    return true;
  if (!style.HasEffectiveAppearance() && !node->IsLink())
    return true;
  // We can't use LayoutTheme::isFocused because outline:auto might be
  // specified to non-:focus rulesets.
  if (node->IsFocused() && !node->ShouldHaveFocusAppearance())
    return false;
  return true;
}

void LayoutTheme::AdjustCheckboxStyle(ComputedStyle& style) const {
  // padding - not honored by WinIE, needs to be removed.
  style.ResetPadding();

  // border - honored by WinIE, but looks terrible (just paints in the control
  // box and turns off the Windows XP theme) for now, we will not honor it.
  style.ResetBorder();
}

void LayoutTheme::AdjustRadioStyle(ComputedStyle& style) const {
  // padding - not honored by WinIE, needs to be removed.
  style.ResetPadding();

  // border - honored by WinIE, but looks terrible (just paints in the control
  // box and turns off the Windows XP theme) for now, we will not honor it.
  style.ResetBorder();
}

void LayoutTheme::AdjustButtonStyle(ComputedStyle& style) const {}

void LayoutTheme::AdjustInnerSpinButtonStyle(ComputedStyle&) const {}

void LayoutTheme::AdjustMenuListStyle(ComputedStyle& style) const {
  // Menulists should have visible overflow
  // https://bugs.webkit.org/show_bug.cgi?id=21287
  style.SetOverflowX(EOverflow::kVisible);
  style.SetOverflowY(EOverflow::kVisible);
}

void LayoutTheme::AdjustMenuListButtonStyle(ComputedStyle&) const {}

void LayoutTheme::AdjustSliderContainerStyle(const Element& element,
                                             ComputedStyle& style) const {
  DCHECK(IsSliderContainer(element));

  if (style.EffectiveAppearance() == kSliderVerticalPart) {
    style.SetTouchAction(TouchAction::kPanX);
    style.SetWritingMode(WritingMode::kVerticalRl);
    // It's always in RTL because the slider value increases up even in LTR.
    style.SetDirection(TextDirection::kRtl);
  } else {
    style.SetTouchAction(TouchAction::kPanY);
    style.SetWritingMode(WritingMode::kHorizontalTb);
    if (To<HTMLInputElement>(element.OwnerShadowHost())->list()) {
      style.SetAlignSelf(StyleSelfAlignmentData(ItemPosition::kCenter,
                                                OverflowAlignment::kUnsafe));
    }
  }
  style.SetEffectiveAppearance(kNoControlPart);
}

void LayoutTheme::AdjustSliderThumbStyle(ComputedStyle& style) const {
  AdjustSliderThumbSize(style);
}

void LayoutTheme::AdjustSliderThumbSize(ComputedStyle&) const {}

void LayoutTheme::AdjustSearchFieldCancelButtonStyle(ComputedStyle&) const {}

void LayoutTheme::PlatformColorsDidChange() {
  UpdateForcedColorsState();
  Page::PlatformColorsChanged();
}

void LayoutTheme::ColorSchemeDidChange() {
  Page::ColorSchemeChanged();
}

void LayoutTheme::ColorProvidersDidChange() {
  Page::ColorProvidersChanged();
}

void LayoutTheme::SetCaretBlinkInterval(base::TimeDelta interval) {
  caret_blink_interval_ = interval;
}

base::TimeDelta LayoutTheme::CaretBlinkInterval() const {
  // Disable the blinking caret in web test mode, as it introduces
  // a race condition for the pixel tests. http://b/1198440
  return WebTestSupport::IsRunningWebTest() ? base::TimeDelta()
                                            : caret_blink_interval_;
}

static FontDescription& GetCachedFontDescription(CSSValueID system_font_id) {
  DEFINE_STATIC_LOCAL(FontDescription, caption, ());
  DEFINE_STATIC_LOCAL(FontDescription, icon, ());
  DEFINE_STATIC_LOCAL(FontDescription, menu, ());
  DEFINE_STATIC_LOCAL(FontDescription, message_box, ());
  DEFINE_STATIC_LOCAL(FontDescription, small_caption, ());
  DEFINE_STATIC_LOCAL(FontDescription, status_bar, ());
  DEFINE_STATIC_LOCAL(FontDescription, webkit_mini_control, ());
  DEFINE_STATIC_LOCAL(FontDescription, webkit_small_control, ());
  DEFINE_STATIC_LOCAL(FontDescription, webkit_control, ());
  DEFINE_STATIC_LOCAL(FontDescription, default_description, ());
  switch (system_font_id) {
    case CSSValueID::kCaption:
      return caption;
    case CSSValueID::kIcon:
      return icon;
    case CSSValueID::kMenu:
      return menu;
    case CSSValueID::kMessageBox:
      return message_box;
    case CSSValueID::kSmallCaption:
      return small_caption;
    case CSSValueID::kStatusBar:
      return status_bar;
    case CSSValueID::kWebkitMiniControl:
      return webkit_mini_control;
    case CSSValueID::kWebkitSmallControl:
      return webkit_small_control;
    case CSSValueID::kWebkitControl:
      return webkit_control;
    case CSSValueID::kNone:
      return default_description;
    default:
      NOTREACHED();
      return default_description;
  }
}

void LayoutTheme::SystemFont(CSSValueID system_font_id,
                             FontDescription& font_description,
                             const Document* document) {
  font_description = GetCachedFontDescription(system_font_id);
  if (font_description.IsAbsoluteSize())
    return;

  font_description.SetStyle(
      LayoutThemeFontProvider::SystemFontStyle(system_font_id));
  font_description.SetWeight(
      LayoutThemeFontProvider::SystemFontWeight(system_font_id));
  font_description.SetSpecifiedSize(
      LayoutThemeFontProvider::SystemFontSize(system_font_id, document));
  font_description.SetIsAbsoluteSize(true);
  const AtomicString& system_font =
      LayoutThemeFontProvider::SystemFontFamily(system_font_id);
  font_description.FirstFamily().SetFamily(
      system_font, FontFamily::InferredTypeFor(system_font));
  font_description.SetGenericFamily(FontDescription::kNoFamily);
}

Color LayoutTheme::SystemColor(CSSValueID css_value_id,
                               mojom::blink::ColorScheme color_scheme) const {
  if (!WebTestSupport::IsRunningWebTest() && InForcedColorsMode())
    return SystemColorFromNativeTheme(css_value_id, color_scheme);
  return DefaultSystemColor(css_value_id, color_scheme);
}

Color LayoutTheme::DefaultSystemColor(
    CSSValueID css_value_id,
    mojom::blink::ColorScheme color_scheme) const {
  // The source for the deprecations commented on below is
  // https://www.w3.org/TR/css-color-4/#deprecated-system-colors.

  switch (css_value_id) {
    case CSSValueID::kActivetext:
      return Color::FromRGBA32(0xFFFF0000);
    case CSSValueID::kButtonborder:
    // The following system colors were deprecated to default to ButtonBorder.
    case CSSValueID::kActiveborder:
    case CSSValueID::kInactiveborder:
    case CSSValueID::kThreeddarkshadow:
    case CSSValueID::kThreedhighlight:
    case CSSValueID::kThreedlightshadow:
    case CSSValueID::kThreedshadow:
    case CSSValueID::kWindowframe:
      return color_scheme == mojom::blink::ColorScheme::kDark
                 ? Color::FromRGBA32(0xFF6B6B6B)
                 : Color::FromRGBA32(0xFF767676);
    case CSSValueID::kButtonface:
    // The following system colors were deprecated to default to ButtonFace.
    case CSSValueID::kButtonhighlight:
    case CSSValueID::kButtonshadow:
    case CSSValueID::kThreedface:
      return color_scheme == mojom::blink::ColorScheme::kDark
                 ? Color::FromRGBA32(0xFF6B6B6B)
                 : Color::FromRGBA32(0xFFEFEFEF);
    case CSSValueID::kButtontext:
      return color_scheme == mojom::blink::ColorScheme::kDark
                 ? Color::FromRGBA32(0xFFFFFFFF)
                 : Color::FromRGBA32(0xFF000000);
    case CSSValueID::kCanvas:
    // The following system colors were deprecated to default to Canvas.
    case CSSValueID::kAppworkspace:
    case CSSValueID::kBackground:
    case CSSValueID::kInactivecaption:
    case CSSValueID::kInfobackground:
    case CSSValueID::kMenu:
    case CSSValueID::kScrollbar:
    case CSSValueID::kWindow:
      return color_scheme == mojom::blink::ColorScheme::kDark
                 ? Color::FromRGBA32(0xFF121212)
                 : Color::FromRGBA32(0xFFFFFFFF);
    case CSSValueID::kCanvastext:
    // The following system colors were deprecated to default to CanvasText.
    case CSSValueID::kActivecaption:
    case CSSValueID::kCaptiontext:
    case CSSValueID::kInfotext:
    case CSSValueID::kMenutext:
    case CSSValueID::kWindowtext:
      return color_scheme == mojom::blink::ColorScheme::kDark
                 ? Color::FromRGBA32(0xFFFFFFFF)
                 : Color::FromRGBA32(0xFF000000);

    case CSSValueID::kField:
      return color_scheme == mojom::blink::ColorScheme::kDark
                 ? Color::FromRGBA32(0xFF3B3B3B)
                 : Color::FromRGBA32(0xFFFFFFFF);
    case CSSValueID::kFieldtext:
      return color_scheme == mojom::blink::ColorScheme::kDark
                 ? Color::FromRGBA32(0xFFFFFFFF)
                 : Color::FromRGBA32(0xFF000000);
    case CSSValueID::kGraytext:
    // The following system color was deprecated to default to GrayText.
    case CSSValueID::kInactivecaptiontext:
      return Color::FromRGBA32(0xFF808080);
    case CSSValueID::kHighlight:
      return Color::FromRGBA32(0xFFB5D5FF);
    case CSSValueID::kHighlighttext:
      return color_scheme == mojom::blink::ColorScheme::kDark
                 ? Color::FromRGBA32(0xFFFFFFFF)
                 : Color::FromRGBA32(0xFF000000);
    case CSSValueID::kLinktext:
      return Color::FromRGBA32(0xFF0000EE);
    case CSSValueID::kMark:
      return Color::FromRGBA32(0xFFFFFF00);
    case CSSValueID::kMarktext:
      return Color::FromRGBA32(0xFF000000);
    case CSSValueID::kText:
      return color_scheme == mojom::blink::ColorScheme::kDark
                 ? Color::FromRGBA32(0xFFFFFFFF)
                 : Color::FromRGBA32(0xFF000000);
    case CSSValueID::kVisitedtext:
      return Color::FromRGBA32(0xFF551A8B);
    case CSSValueID::kSelecteditem:
    case CSSValueID::kInternalActiveListBoxSelection:
      return ActiveListBoxSelectionBackgroundColor(color_scheme);
    case CSSValueID::kSelecteditemtext:
    case CSSValueID::kInternalActiveListBoxSelectionText:
      return ActiveListBoxSelectionForegroundColor(color_scheme);
    case CSSValueID::kInternalInactiveListBoxSelection:
      return InactiveListBoxSelectionBackgroundColor(color_scheme);
    case CSSValueID::kInternalInactiveListBoxSelectionText:
      return InactiveListBoxSelectionForegroundColor(color_scheme);
    case CSSValueID::kInternalSpellingErrorColor:
      return PlatformSpellingMarkerUnderlineColor();
    case CSSValueID::kInternalGrammarErrorColor:
      return PlatformGrammarMarkerUnderlineColor();
    default:
      break;
  }
  NOTREACHED();
  return Color();
}

Color LayoutTheme::SystemColorFromNativeTheme(
    CSSValueID css_value_id,
    mojom::blink::ColorScheme color_scheme) const {
  blink::WebThemeEngine::SystemThemeColor theme_color;
  switch (css_value_id) {
    case CSSValueID::kActivetext:
    case CSSValueID::kLinktext:
    case CSSValueID::kVisitedtext:
      theme_color = blink::WebThemeEngine::SystemThemeColor::kHotlight;
      break;
    case CSSValueID::kButtonface:
    case CSSValueID::kButtonhighlight:
    case CSSValueID::kButtonshadow:
    case CSSValueID::kThreedface:
      theme_color = blink::WebThemeEngine::SystemThemeColor::kButtonFace;
      break;
    case CSSValueID::kButtonborder:
    case CSSValueID::kButtontext:
    // Deprecated colors, see DefaultSystemColor().
    case CSSValueID::kActiveborder:
    case CSSValueID::kInactiveborder:
    case CSSValueID::kThreeddarkshadow:
    case CSSValueID::kThreedhighlight:
    case CSSValueID::kThreedlightshadow:
    case CSSValueID::kThreedshadow:
    case CSSValueID::kWindowframe:
      theme_color = blink::WebThemeEngine::SystemThemeColor::kButtonText;
      break;
    case CSSValueID::kGraytext:
      theme_color = blink::WebThemeEngine::SystemThemeColor::kGrayText;
      break;
    case CSSValueID::kHighlight:
      theme_color = blink::WebThemeEngine::SystemThemeColor::kHighlight;
      break;
    case CSSValueID::kHighlighttext:
      theme_color = blink::WebThemeEngine::SystemThemeColor::kHighlightText;
      break;
    case CSSValueID::kCanvas:
    case CSSValueID::kField:
    // Deprecated colors, see DefaultSystemColor().
    case CSSValueID::kAppworkspace:
    case CSSValueID::kBackground:
    case CSSValueID::kInactivecaption:
    case CSSValueID::kInfobackground:
    case CSSValueID::kMenu:
    case CSSValueID::kScrollbar:
    case CSSValueID::kWindow:
      theme_color = blink::WebThemeEngine::SystemThemeColor::kWindow;
      break;
    case CSSValueID::kCanvastext:
    case CSSValueID::kFieldtext:
    // Deprecated colors, see DefaultSystemColor().
    case CSSValueID::kActivecaption:
    case CSSValueID::kCaptiontext:
    case CSSValueID::kInfotext:
    case CSSValueID::kMenutext:
    case CSSValueID::kWindowtext:
      theme_color = blink::WebThemeEngine::SystemThemeColor::kWindowText;
      break;
    default:
      return DefaultSystemColor(css_value_id, color_scheme);
  }
  const absl::optional<SkColor> system_color =
      WebThemeEngineHelper::GetNativeThemeEngine()->GetSystemColor(theme_color);
  if (system_color)
    return Color::FromSkColor((system_color.value()));
  return DefaultSystemColor(css_value_id, color_scheme);
}

Color LayoutTheme::PlatformTextSearchHighlightColor(
    bool active_match,
    mojom::blink::ColorScheme color_scheme) const {
  if (active_match) {
    if (InForcedColorsMode())
      return GetTheme().SystemColor(CSSValueID::kHighlight, color_scheme);
    return Color(255, 150, 50);  // Orange.
  }
  return Color(255, 255, 0);     // Yellow.
}

Color LayoutTheme::PlatformTextSearchColor(
    bool active_match,
    mojom::blink::ColorScheme color_scheme) const {
  if (InForcedColorsMode() && active_match)
    return GetTheme().SystemColor(CSSValueID::kHighlighttext, color_scheme);
  return Color::kBlack;
}

Color LayoutTheme::TapHighlightColor() {
  return GetTheme().PlatformTapHighlightColor();
}

void LayoutTheme::SetCustomFocusRingColor(const Color& c) {
  custom_focus_ring_color_ = c;
  has_custom_focus_ring_color_ = true;
}

Color LayoutTheme::FocusRingColor(
    mojom::blink::ColorScheme color_scheme) const {
  return has_custom_focus_ring_color_ ? custom_focus_ring_color_
                                      : GetTheme().PlatformFocusRingColor();
}

bool LayoutTheme::DelegatesMenuListRendering() const {
  return delegates_menu_list_rendering_;
}

void LayoutTheme::SetDelegatesMenuListRenderingForTesting(bool flag) {
  delegates_menu_list_rendering_ = flag;
}

String LayoutTheme::DisplayNameForFile(const File& file) const {
  return file.name();
}

bool LayoutTheme::SupportsCalendarPicker(const AtomicString& type) const {
  DCHECK(RuntimeEnabledFeatures::InputMultipleFieldsUIEnabled());
  if (type == input_type_names::kTime)
    return true;

  return type == input_type_names::kDate ||
         type == input_type_names::kDatetime ||
         type == input_type_names::kDatetimeLocal ||
         type == input_type_names::kMonth || type == input_type_names::kWeek;
}

void LayoutTheme::AdjustControlPartStyle(ComputedStyle& style) {
  // Call the appropriate style adjustment method based off the appearance
  // value.
  switch (style.EffectiveAppearance()) {
    case kCheckboxPart:
      return AdjustCheckboxStyle(style);
    case kRadioPart:
      return AdjustRadioStyle(style);
    case kPushButtonPart:
    case kSquareButtonPart:
    case kButtonPart:
      return AdjustButtonStyle(style);
    case kInnerSpinButtonPart:
      return AdjustInnerSpinButtonStyle(style);
    default:
      break;
  }
}

bool LayoutTheme::HasCustomFocusRingColor() const {
  return has_custom_focus_ring_color_;
}

Color LayoutTheme::GetCustomFocusRingColor() const {
  return custom_focus_ring_color_;
}

void LayoutTheme::UpdateForcedColorsState() {
  in_forced_colors_mode_ =
      WebThemeEngineHelper::GetNativeThemeEngine()->GetForcedColors() !=
      ForcedColors::kNone;
}

}  // namespace blink
