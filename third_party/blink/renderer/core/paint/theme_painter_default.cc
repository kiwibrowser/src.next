/*
 * Copyright (C) 2007 Apple Inc.
 * Copyright (C) 2007 Alp Toker <alp@atoker.com>
 * Copyright (C) 2008 Collabora Ltd.
 * Copyright (C) 2008, 2009 Google Inc.
 * Copyright (C) 2009 Kenneth Rohde Christiansen
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
 *
 */

#include "third_party/blink/renderer/core/paint/theme_painter_default.h"

#include "third_party/abseil-cpp/absl/types/variant.h"
#include "third_party/blink/public/platform/web_theme_engine.h"
#include "third_party/blink/public/resources/grit/blink_image_resources.h"
#include "third_party/blink/renderer/core/dom/shadow_root.h"
#include "third_party/blink/renderer/core/frame/local_frame_view.h"
#include "third_party/blink/renderer/core/html/forms/html_input_element.h"
#include "third_party/blink/renderer/core/html/forms/slider_thumb_element.h"
#include "third_party/blink/renderer/core/html/forms/spin_button_element.h"
#include "third_party/blink/renderer/core/html/shadow/shadow_element_names.h"
#include "third_party/blink/renderer/core/layout/layout_object.h"
#include "third_party/blink/renderer/core/layout/layout_progress.h"
#include "third_party/blink/renderer/core/layout/layout_theme_default.h"
#include "third_party/blink/renderer/core/page/chrome_client.h"
#include "third_party/blink/renderer/core/page/page.h"
#include "third_party/blink/renderer/core/paint/paint_auto_dark_mode.h"
#include "third_party/blink/renderer/core/paint/paint_info.h"
#include "third_party/blink/renderer/platform/graphics/color.h"
#include "third_party/blink/renderer/platform/graphics/graphics_context.h"
#include "third_party/blink/renderer/platform/graphics/graphics_context_state_saver.h"
#include "third_party/blink/renderer/platform/text/writing_mode.h"
#include "third_party/blink/renderer/platform/theme/web_theme_engine_helper.h"
#include "ui/base/ui_base_features.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/color_utils.h"
#include "ui/native_theme/native_theme.h"

namespace blink {

namespace {

bool IsIndeterminate(const Element& element) {
  if (const auto* input = DynamicTo<HTMLInputElement>(element))
    return input->ShouldAppearIndeterminate();
  return false;
}

bool IsChecked(const Element& element) {
  if (const auto* input = DynamicTo<HTMLInputElement>(element))
    return input->ShouldAppearChecked();
  return false;
}

WebThemeEngine::State GetWebThemeState(const Element& element) {
  if (element.IsDisabledFormControl())
    return WebThemeEngine::kStateDisabled;
  if (element.IsActive())
    return WebThemeEngine::kStatePressed;
  if (element.IsHovered())
    return WebThemeEngine::kStateHover;

  return WebThemeEngine::kStateNormal;
}

class DirectionFlippingScope {
  STACK_ALLOCATED();

 public:
  DirectionFlippingScope(const LayoutObject&,
                         const PaintInfo&,
                         const gfx::Rect&);
  ~DirectionFlippingScope();

 private:
  bool needs_horizontal_flipping_;
  bool needs_vertical_flipping_;
  const PaintInfo& paint_info_;
};

DirectionFlippingScope::DirectionFlippingScope(
    const LayoutObject& layout_object,
    const PaintInfo& paint_info,
    const gfx::Rect& rect)
    : needs_horizontal_flipping_(
          IsHorizontalWritingMode(layout_object.StyleRef().GetWritingMode()) &&
          !layout_object.StyleRef().IsLeftToRightDirection()),
      needs_vertical_flipping_(
          !IsHorizontalWritingMode(layout_object.StyleRef().GetWritingMode()) &&
          RuntimeEnabledFeatures::
              FormControlsVerticalWritingModeDirectionSupportEnabled() &&
          layout_object.StyleRef().IsLeftToRightDirection()),
      paint_info_(paint_info) {
  if (needs_horizontal_flipping_) {
    paint_info_.context.Save();
    paint_info_.context.Translate(2 * rect.x() + rect.width(), 0);
    paint_info_.context.Scale(-1, 1);
  } else if (needs_vertical_flipping_) {
    paint_info_.context.Save();
    paint_info_.context.Translate(0, 2 * rect.y() + rect.height());
    paint_info_.context.Scale(1, -1);
  }
}

DirectionFlippingScope::~DirectionFlippingScope() {
  if (!needs_horizontal_flipping_ && !needs_vertical_flipping_) {
    return;
  }
  paint_info_.context.Restore();
}

gfx::Rect DeterminateProgressValueRectFor(const LayoutProgress& layout_progress,
                                          const gfx::Rect& rect) {
  int dx = rect.width();
  int dy = rect.height();
  int y = rect.y();
  if (IsHorizontalWritingMode(layout_progress.StyleRef().GetWritingMode())) {
    dx *= layout_progress.GetPosition();
  } else {
    dy *= layout_progress.GetPosition();
    y += rect.height() - dy;
  }
  return gfx::Rect(rect.x(), y, dx, dy);
}

gfx::Rect IndeterminateProgressValueRectFor(
    const LayoutProgress& layout_progress,
    const gfx::Rect& rect) {
  // Value comes from default of GTK+.
  static const int kProgressActivityBlocks = 5;

  int x = rect.x();
  int y = rect.y();
  int value_width = rect.width();
  int value_height = rect.height();
  double progress = layout_progress.AnimationProgress();

  if (IsHorizontalWritingMode(layout_progress.StyleRef().GetWritingMode())) {
    value_width = value_width / kProgressActivityBlocks;
    int movable_width = rect.width() - value_width;
    if (movable_width <= 0)
      return gfx::Rect();
    x = progress < 0.5 ? x + progress * 2 * movable_width
                       : rect.x() + (1.0 - progress) * 2 * movable_width;
  } else {
    value_height = value_height / kProgressActivityBlocks;
    int movable_height = rect.height() - value_height;
    if (movable_height <= 0)
      return gfx::Rect();
    y = progress < 0.5 ? y + progress * 2 * movable_height
                       : rect.y() + (1.0 - progress) * 2 * movable_height;
  }

  return gfx::Rect(x, y, value_width, value_height);
}

gfx::Rect ProgressValueRectFor(const LayoutProgress& layout_progress,
                               const gfx::Rect& rect) {
  return layout_progress.IsDeterminate()
             ? DeterminateProgressValueRectFor(layout_progress, rect)
             : IndeterminateProgressValueRectFor(layout_progress, rect);
}

gfx::Rect ConvertToPaintingRect(const LayoutObject& input_layout_object,
                                const LayoutObject& part_layout_object,
                                PhysicalRect part_rect,
                                const gfx::Rect& local_offset) {
  // Compute an offset between the partLayoutObject and the inputLayoutObject.
  PhysicalOffset offset_from_input_layout_object =
      -part_layout_object.OffsetFromAncestor(&input_layout_object);
  // Move the rect into partLayoutObject's coords.
  part_rect.Move(offset_from_input_layout_object);
  // Account for the local drawing offset.
  part_rect.Move(PhysicalOffset(local_offset.origin()));

  return ToPixelSnappedRect(part_rect);
}

absl::optional<SkColor> GetAccentColor(const ComputedStyle& style,
                                       const Document& document) {
  absl::optional<Color> css_accent_color = style.AccentColorResolved();
  if (css_accent_color)
    return css_accent_color->Rgb();

  bool in_image =
      document.GetPage()->GetChromeClient().IsSVGImageChromeClient();
  if (!RuntimeEnabledFeatures::PreventReadingSystemAccentColorEnabled() ||
      !in_image) {
    mojom::blink::ColorScheme color_scheme = style.UsedColorScheme();
    LayoutTheme& layout_theme = LayoutTheme::GetTheme();
    if (layout_theme.IsAccentColorCustomized(color_scheme)) {
      return layout_theme.GetSystemAccentColor(color_scheme).Rgb();
    }
  }

  return absl::nullopt;
}

}  // namespace

ThemePainterDefault::ThemePainterDefault(LayoutThemeDefault& theme)
    : ThemePainter(), theme_(theme) {}

bool ThemePainterDefault::PaintCheckbox(const Element& element,
                                        const Document& document,
                                        const ComputedStyle& style,
                                        const PaintInfo& paint_info,
                                        const gfx::Rect& rect) {
  WebThemeEngine::ButtonExtraParams button;
  button.checked = IsChecked(element);
  button.indeterminate = IsIndeterminate(element);

  float zoom_level = style.EffectiveZoom();
  button.zoom = zoom_level;
  GraphicsContextStateSaver state_saver(paint_info.context, false);
  gfx::Rect unzoomed_rect =
      ApplyZoomToRect(rect, paint_info, state_saver, zoom_level);
  WebThemeEngine::ExtraParams extra_params(button);
  mojom::blink::ColorScheme color_scheme = style.UsedColorScheme();
  const ui::ColorProvider* color_provider =
      document.GetColorProviderForPainting(color_scheme);
  WebThemeEngineHelper::GetNativeThemeEngine()->Paint(
      paint_info.context.Canvas(), WebThemeEngine::kPartCheckbox,
      GetWebThemeState(element), unzoomed_rect, &extra_params, color_scheme,
      color_provider, GetAccentColor(style, document));
  return false;
}

bool ThemePainterDefault::PaintRadio(const Element& element,
                                     const Document& document,
                                     const ComputedStyle& style,
                                     const PaintInfo& paint_info,
                                     const gfx::Rect& rect) {
  WebThemeEngine::ButtonExtraParams button;
  button.checked = IsChecked(element);

  float zoom_level = style.EffectiveZoom();
  button.zoom = zoom_level;
  WebThemeEngine::ExtraParams extra_params(button);
  GraphicsContextStateSaver state_saver(paint_info.context, false);
  gfx::Rect unzoomed_rect =
      ApplyZoomToRect(rect, paint_info, state_saver, zoom_level);
  mojom::blink::ColorScheme color_scheme = style.UsedColorScheme();
  const ui::ColorProvider* color_provider =
      document.GetColorProviderForPainting(color_scheme);
  WebThemeEngineHelper::GetNativeThemeEngine()->Paint(
      paint_info.context.Canvas(), WebThemeEngine::kPartRadio,
      GetWebThemeState(element), unzoomed_rect, &extra_params, color_scheme,
      color_provider, GetAccentColor(style, document));
  return false;
}

bool ThemePainterDefault::PaintButton(const Element& element,
                                      const Document& document,
                                      const ComputedStyle& style,
                                      const PaintInfo& paint_info,
                                      const gfx::Rect& rect) {
  WebThemeEngine::ButtonExtraParams button;
  button.has_border = true;
  button.zoom = style.EffectiveZoom();
  WebThemeEngine::ExtraParams extra_params(button);
  mojom::blink::ColorScheme color_scheme = style.UsedColorScheme();
  const ui::ColorProvider* color_provider =
      document.GetColorProviderForPainting(color_scheme);

  WebThemeEngineHelper::GetNativeThemeEngine()->Paint(
      paint_info.context.Canvas(), WebThemeEngine::kPartButton,
      GetWebThemeState(element), rect, &extra_params, color_scheme,
      color_provider, GetAccentColor(style, document));
  return false;
}

bool ThemePainterDefault::PaintTextField(const Element& element,
                                         const ComputedStyle& style,
                                         const PaintInfo& paint_info,
                                         const gfx::Rect& rect) {
  // WebThemeEngine does not handle border rounded corner and background image
  // so return true to draw CSS border and background.
  if (style.HasBorderRadius() || style.HasBackgroundImage())
    return true;

  ControlPart part = style.EffectiveAppearance();

  WebThemeEngine::TextFieldExtraParams text_field;
  text_field.is_text_area = part == kTextAreaPart;
  text_field.is_listbox = part == kListboxPart;
  text_field.has_border = true;
  text_field.zoom = style.EffectiveZoom();

  Color background_color =
      style.VisitedDependentColor(GetCSSPropertyBackgroundColor());
  text_field.background_color = background_color.Rgb();
  text_field.auto_complete_active =
      DynamicTo<HTMLFormControlElement>(element)->HighlightAutofilled() ||
      DynamicTo<HTMLFormControlElement>(element)->GetAutofillState() ==
          WebAutofillState::kPreviewed;

  WebThemeEngine::ExtraParams extra_params(text_field);
  mojom::blink::ColorScheme color_scheme = style.UsedColorScheme();
  const ui::ColorProvider* color_provider =
      element.GetDocument().GetColorProviderForPainting(color_scheme);

  WebThemeEngineHelper::GetNativeThemeEngine()->Paint(
      paint_info.context.Canvas(), WebThemeEngine::kPartTextField,
      GetWebThemeState(element), rect, &extra_params, color_scheme,
      color_provider, GetAccentColor(style, element.GetDocument()));
  return false;
}

bool ThemePainterDefault::PaintMenuList(const Element& element,
                                        const Document& document,
                                        const ComputedStyle& style,
                                        const PaintInfo& paint_info,
                                        const gfx::Rect& rect) {
  WebThemeEngine::MenuListExtraParams menu_list;
  // Match Chromium Win behaviour of showing all borders if any are shown.
  menu_list.has_border = style.HasBorder();
  menu_list.has_border_radius = style.HasBorderRadius();
  menu_list.zoom = style.EffectiveZoom();
  // Fallback to transparent if the specified color object is invalid.
  Color background_color(Color::kTransparent);
  if (style.HasBackground()) {
    background_color =
        style.VisitedDependentColor(GetCSSPropertyBackgroundColor());
  }
  menu_list.background_color = background_color.Rgb();

  // If we have a background image, don't fill the content area to expose the
  // parent's background. Also, we shouldn't fill the content area if the
  // alpha of the color is 0. The API of Windows GDI ignores the alpha.
  // FIXME: the normal Aura theme doesn't care about this, so we should
  // investigate if we really need fillContentArea.
  menu_list.fill_content_area =
      !style.HasBackgroundImage() && !background_color.IsFullyTransparent();

  WebThemeEngine::ExtraParams extra_params(menu_list);

  SetupMenuListArrow(document, style, rect, extra_params);

  mojom::blink::ColorScheme color_scheme = style.UsedColorScheme();
  const ui::ColorProvider* color_provider =
      document.GetColorProviderForPainting(color_scheme);

  WebThemeEngineHelper::GetNativeThemeEngine()->Paint(
      paint_info.context.Canvas(), WebThemeEngine::kPartMenuList,
      GetWebThemeState(element), rect, &extra_params, color_scheme,
      color_provider, GetAccentColor(style, document));
  return false;
}

bool ThemePainterDefault::PaintMenuListButton(const Element& element,
                                              const Document& document,
                                              const ComputedStyle& style,
                                              const PaintInfo& paint_info,
                                              const gfx::Rect& rect) {
  WebThemeEngine::MenuListExtraParams menu_list;
  menu_list.has_border = false;
  menu_list.has_border_radius = style.HasBorderRadius();
  menu_list.background_color = SK_ColorTRANSPARENT;
  menu_list.fill_content_area = false;
  WebThemeEngine::ExtraParams extra_params(menu_list);
  SetupMenuListArrow(document, style, rect, extra_params);
  mojom::blink::ColorScheme color_scheme = style.UsedColorScheme();
  const ui::ColorProvider* color_provider =
      document.GetColorProviderForPainting(color_scheme);

  WebThemeEngineHelper::GetNativeThemeEngine()->Paint(
      paint_info.context.Canvas(), WebThemeEngine::kPartMenuList,
      GetWebThemeState(element), rect, &extra_params, color_scheme,
      color_provider, GetAccentColor(style, document));
  return false;
}

void ThemePainterDefault::SetupMenuListArrow(
    const Document& document,
    const ComputedStyle& style,
    const gfx::Rect& rect,
    WebThemeEngine::ExtraParams& extra_params) {
  auto& menu_list =
      absl::get<WebThemeEngine::MenuListExtraParams>(extra_params);
  if (IsHorizontalWritingMode(style.GetWritingMode()) ||
      !RuntimeEnabledFeatures::
          FormControlsVerticalWritingModeSupportEnabled()) {
    menu_list.arrow_direction = WebThemeEngine::ArrowDirection::kDown;
    const int left = rect.x() + floorf(style.BorderLeftWidth());
    const int right =
        rect.x() + rect.width() - floorf(style.BorderRightWidth());
    const int middle = rect.y() + rect.height() / 2;

    menu_list.arrow_y = middle;
    float arrow_box_width =
        theme_.ClampedMenuListArrowPaddingSize(document.GetFrame(), style);
    float arrow_scale_factor =
        arrow_box_width / theme_.MenuListArrowWidthInDIP();
    // TODO(tkent): This should be 7.0 to match scroll bar buttons.
    float arrow_size = 8.0 * arrow_scale_factor;
    // Put the arrow at the center of paddingForArrow area.
    // |arrowX| is the left position for Aura theme engine.
    menu_list.arrow_x = (style.Direction() == TextDirection::kRtl)
                            ? left + (arrow_box_width - arrow_size) / 2
                            : right - (arrow_box_width + arrow_size) / 2;
    menu_list.arrow_size = arrow_size;
  } else {
    if (style.GetWritingMode() == WritingMode::kVerticalLr) {
      menu_list.arrow_direction = WebThemeEngine::ArrowDirection::kRight;
    } else {
      menu_list.arrow_direction = WebThemeEngine::ArrowDirection::kLeft;
    }
    const int bottom = rect.y() + floorf(style.BorderBottomWidth());
    const int top = rect.y() + rect.height() - floorf(style.BorderTopWidth());
    const int middle = rect.x() + rect.width() / 2;

    menu_list.arrow_x = middle;
    float arrow_box_height =
        theme_.ClampedMenuListArrowPaddingSize(document.GetFrame(), style);
    float arrow_scale_factor =
        arrow_box_height / theme_.MenuListArrowWidthInDIP();
    // TODO(tkent): This should be 7.0 to match scroll bar buttons.
    float arrow_size = 8.0 * arrow_scale_factor;
    // Put the arrow at the center of paddingForArrow area.
    // |arrowY| is the bottom position for Aura theme engine.
    menu_list.arrow_y = (style.Direction() == TextDirection::kRtl)
                            ? bottom + (arrow_box_height - arrow_size) / 2
                            : top - (arrow_box_height + arrow_size) / 2;
    menu_list.arrow_size = arrow_size;
  }

  // TODO: (https://crbug.com/1227305)This color still does not support forced
  // dark mode
  menu_list.arrow_color =
      style.VisitedDependentColor(GetCSSPropertyColor()).Rgb();
}

bool ThemePainterDefault::PaintSliderTrack(const Element& element,
                                           const LayoutObject& layout_object,
                                           const PaintInfo& paint_info,
                                           const gfx::Rect& rect,
                                           const ComputedStyle& style) {
  WebThemeEngine::SliderExtraParams slider;
  slider.vertical = (RuntimeEnabledFeatures::
                         FormControlsVerticalWritingModeSupportEnabled() &&
                     !IsHorizontalWritingMode(style.GetWritingMode())) ||
                    (RuntimeEnabledFeatures::
                         NonStandardAppearanceValueSliderVerticalEnabled() &&
                     style.EffectiveAppearance() == kSliderVerticalPart);
  slider.in_drag = false;

  PaintSliderTicks(layout_object, paint_info, rect);

  slider.zoom = style.EffectiveZoom();
  slider.thumb_x = 0;
  slider.thumb_y = 0;
  slider.right_to_left =
      !RuntimeEnabledFeatures::
                  FormControlsVerticalWritingModeDirectionSupportEnabled() &&
              slider.vertical
          ? true
          : !style.IsLeftToRightDirection();
  if (auto* input = DynamicTo<HTMLInputElement>(element)) {
    Element* thumb_element = input->UserAgentShadowRoot()
                                 ? input->UserAgentShadowRoot()->getElementById(
                                       shadow_element_names::kIdSliderThumb)
                                 : nullptr;
    LayoutBox* thumb = thumb_element ? thumb_element->GetLayoutBox() : nullptr;
    LayoutBox* input_box = input->GetLayoutBox();
    if (thumb) {
      gfx::Rect thumb_rect = ToPixelSnappedRect(
          PhysicalRect(thumb->PhysicalLocation(), thumb->Size()));
      slider.thumb_x = thumb_rect.x() + input_box->PaddingLeft().ToInt() +
                       input_box->BorderLeft().ToInt();
      slider.thumb_y = thumb_rect.y() + input_box->PaddingTop().ToInt() +
                       input_box->BorderTop().ToInt();
    }
  }
  WebThemeEngine::ExtraParams extra_params(slider);
  mojom::blink::ColorScheme color_scheme = style.UsedColorScheme();
  const ui::ColorProvider* color_provider =
      element.GetDocument().GetColorProviderForPainting(color_scheme);

  WebThemeEngineHelper::GetNativeThemeEngine()->Paint(
      paint_info.context.Canvas(), WebThemeEngine::kPartSliderTrack,
      GetWebThemeState(element), rect, &extra_params, color_scheme,
      color_provider, GetAccentColor(style, element.GetDocument()));
  return false;
}

bool ThemePainterDefault::PaintSliderThumb(const Element& element,
                                           const ComputedStyle& style,
                                           const PaintInfo& paint_info,
                                           const gfx::Rect& rect) {
  WebThemeEngine::SliderExtraParams slider;
  slider.vertical = (RuntimeEnabledFeatures::
                         FormControlsVerticalWritingModeSupportEnabled() &&
                     !IsHorizontalWritingMode(style.GetWritingMode())) ||
                    (RuntimeEnabledFeatures::
                         NonStandardAppearanceValueSliderVerticalEnabled() &&
                     style.EffectiveAppearance() == kSliderThumbVerticalPart);
  slider.in_drag = element.IsActive();
  slider.zoom = style.EffectiveZoom();

  // The element passed in is inside the user agent shadow DOM of the input
  // element, so we have to access the parent input element in order to get the
  // accent-color style set by the page.
  const SliderThumbElement* slider_element =
      DynamicTo<SliderThumbElement>(&element);
  DCHECK(slider_element);  // PaintSliderThumb should always be passed a
                           // SliderThumbElement
  absl::optional<SkColor> accent_color =
      GetAccentColor(*slider_element->HostInput()->EnsureComputedStyle(),
                     element.GetDocument());
  WebThemeEngine::ExtraParams extra_params(slider);
  mojom::blink::ColorScheme color_scheme = style.UsedColorScheme();
  const ui::ColorProvider* color_provider =
      element.GetDocument().GetColorProviderForPainting(color_scheme);

  WebThemeEngineHelper::GetNativeThemeEngine()->Paint(
      paint_info.context.Canvas(), WebThemeEngine::kPartSliderThumb,
      GetWebThemeState(element), rect, &extra_params, color_scheme,
      color_provider, accent_color);
  return false;
}

bool ThemePainterDefault::PaintInnerSpinButton(const Element& element,
                                               const ComputedStyle& style,
                                               const PaintInfo& paint_info,
                                               const gfx::Rect& rect) {
  WebThemeEngine::InnerSpinButtonExtraParams inner_spin;

  bool spin_up = false;
  if (const auto* spin_buttom = DynamicTo<SpinButtonElement>(element)) {
    if (spin_buttom->GetUpDownState() == SpinButtonElement::kUp)
      spin_up = element.IsHovered() || element.IsActive();
  }

  bool read_only = false;
  if (const auto* control = DynamicTo<HTMLFormControlElement>(element))
    read_only = control->IsReadOnly();

  inner_spin.spin_up = spin_up;
  inner_spin.read_only = read_only;
  inner_spin.spin_arrows_direction =
      IsHorizontalWritingMode(style.GetWritingMode())
          ? WebThemeEngine::SpinArrowsDirection::kUpDown
          : WebThemeEngine::SpinArrowsDirection::kLeftRight;

  WebThemeEngine::ExtraParams extra_params(inner_spin);
  mojom::blink::ColorScheme color_scheme = style.UsedColorScheme();
  const ui::ColorProvider* color_provider =
      element.GetDocument().GetColorProviderForPainting(color_scheme);

  WebThemeEngineHelper::GetNativeThemeEngine()->Paint(
      paint_info.context.Canvas(), WebThemeEngine::kPartInnerSpinButton,
      GetWebThemeState(element), rect, &extra_params, color_scheme,
      color_provider, GetAccentColor(style, element.GetDocument()));
  return false;
}

bool ThemePainterDefault::PaintProgressBar(const Element& element,
                                           const LayoutObject& layout_object,
                                           const PaintInfo& paint_info,
                                           const gfx::Rect& rect,
                                           const ComputedStyle& style) {
  const auto* layout_progress = DynamicTo<LayoutProgress>(layout_object);
  if (!layout_progress)
    return true;

  gfx::Rect value_rect = ProgressValueRectFor(*layout_progress, rect);

  WebThemeEngine::ProgressBarExtraParams progress_bar;
  progress_bar.determinate = layout_progress->IsDeterminate();
  progress_bar.value_rect_x = value_rect.x();
  progress_bar.value_rect_y = value_rect.y();
  progress_bar.value_rect_width = value_rect.width();
  progress_bar.value_rect_height = value_rect.height();
  progress_bar.zoom = style.EffectiveZoom();
  progress_bar.is_horizontal =
      IsHorizontalWritingMode(layout_progress->StyleRef().GetWritingMode());
  WebThemeEngine::ExtraParams extra_params(progress_bar);
  DirectionFlippingScope scope(layout_object, paint_info, rect);
  mojom::blink::ColorScheme color_scheme = style.UsedColorScheme();
  const ui::ColorProvider* color_provider =
      element.GetDocument().GetColorProviderForPainting(color_scheme);
  WebThemeEngineHelper::GetNativeThemeEngine()->Paint(
      paint_info.context.Canvas(), WebThemeEngine::kPartProgressBar,
      GetWebThemeState(element), rect, &extra_params, color_scheme,
      color_provider, GetAccentColor(style, element.GetDocument()));
  return false;
}

bool ThemePainterDefault::PaintTextArea(const Element& element,
                                        const ComputedStyle& style,
                                        const PaintInfo& paint_info,
                                        const gfx::Rect& rect) {
  return PaintTextField(element, style, paint_info, rect);
}

bool ThemePainterDefault::PaintSearchField(const Element& element,
                                           const ComputedStyle& style,
                                           const PaintInfo& paint_info,
                                           const gfx::Rect& rect) {
  return PaintTextField(element, style, paint_info, rect);
}

bool ThemePainterDefault::PaintSearchFieldCancelButton(
    const LayoutObject& cancel_button_object,
    const PaintInfo& paint_info,
    const gfx::Rect& r) {
  // Get the layoutObject of <input> element.
  Node* input = cancel_button_object.GetNode()->OwnerShadowHost();
  const LayoutObject& base_layout_object = input && input->GetLayoutObject()
                                               ? *input->GetLayoutObject()
                                               : cancel_button_object;
  if (!base_layout_object.IsBox())
    return false;
  const auto& input_layout_box = To<LayoutBox>(base_layout_object);
  PhysicalRect input_content_box = input_layout_box.PhysicalContentBoxRect();

  // Make sure the scaled button stays square and will fit in its parent's box.
  LayoutUnit cancel_button_size =
      std::min(input_content_box.size.width,
               std::min(input_content_box.size.height, LayoutUnit(r.height())));
  // Calculate cancel button's coordinates relative to the input element.
  // Center the button inline.  Round up though, so if it has to be one
  // pixel off-center, it will be one pixel closer to the bottom of the field.
  // This tends to look better with the text.
  const LayoutUnit cancel_button_rect_left =
      IsHorizontalWritingMode(cancel_button_object.StyleRef().GetWritingMode())
          ? cancel_button_object.OffsetFromAncestor(&input_layout_box).left
          : input_content_box.X() +
                (input_content_box.Width() - cancel_button_size + 1) / 2;
  const LayoutUnit cancel_button_rect_top =
      IsHorizontalWritingMode(cancel_button_object.StyleRef().GetWritingMode())
          ? input_content_box.Y() +
                (input_content_box.Height() - cancel_button_size + 1) / 2
          : cancel_button_object.OffsetFromAncestor(&input_layout_box).top;
  PhysicalRect cancel_button_rect(cancel_button_rect_left,
                                  cancel_button_rect_top, cancel_button_size,
                                  cancel_button_size);
  gfx::Rect painting_rect = ConvertToPaintingRect(
      input_layout_box, cancel_button_object, cancel_button_rect, r);
  DEFINE_STATIC_REF(Image, cancel_image,
                    (Image::LoadPlatformResource(IDR_SEARCH_CANCEL)));
  DEFINE_STATIC_REF(Image, cancel_pressed_image,
                    (Image::LoadPlatformResource(IDR_SEARCH_CANCEL_PRESSED)));
  DEFINE_STATIC_REF(Image, cancel_image_dark_mode,
                    (Image::LoadPlatformResource(IDR_SEARCH_CANCEL_DARK_MODE)));
  DEFINE_STATIC_REF(
      Image, cancel_pressed_image_dark_mode,
      (Image::LoadPlatformResource(IDR_SEARCH_CANCEL_PRESSED_DARK_MODE)));
  DEFINE_STATIC_REF(
      Image, cancel_image_hc_light_mode,
      (Image::LoadPlatformResource(IDR_SEARCH_CANCEL_HC_LIGHT_MODE)));
  DEFINE_STATIC_REF(
      Image, cancel_pressed_image_hc_light_mode,
      (Image::LoadPlatformResource(IDR_SEARCH_CANCEL_PRESSED_HC_LIGHT_MODE)));
  Image* color_scheme_adjusted_cancel_image;
  Image* color_scheme_adjusted_cancel_pressed_image;
  if (ui::NativeTheme::GetInstanceForWeb()->UserHasContrastPreference()) {
    // TODO(crbug.com/1159597): Ideally we want the cancel button to be the same
    // color as search field text. Since the cancel button is currently painted
    // with a .png, it can't be colored dynamically so currently our only
    // choices are black and white.
    Color search_field_text_color =
        cancel_button_object.StyleRef().VisitedDependentColor(
            GetCSSPropertyColor());
    bool text_is_dark = color_utils::GetRelativeLuminance4f(
                            search_field_text_color.toSkColor4f()) < 0.5;
    color_scheme_adjusted_cancel_image =
        text_is_dark ? cancel_image_hc_light_mode : cancel_image_dark_mode;
    color_scheme_adjusted_cancel_pressed_image =
        color_scheme_adjusted_cancel_image =
            text_is_dark ? cancel_pressed_image_hc_light_mode
                         : cancel_pressed_image_dark_mode;
  } else {
    mojom::blink::ColorScheme color_scheme =
        cancel_button_object.StyleRef().UsedColorScheme();
    color_scheme_adjusted_cancel_image =
        color_scheme == mojom::blink::ColorScheme::kLight
            ? cancel_image
            : cancel_image_dark_mode;
    color_scheme_adjusted_cancel_pressed_image =
        color_scheme == mojom::blink::ColorScheme::kLight
            ? cancel_pressed_image
            : cancel_pressed_image_dark_mode;
  }
  Image& target_image = To<Element>(cancel_button_object.GetNode())->IsActive()
                            ? *color_scheme_adjusted_cancel_pressed_image
                            : *color_scheme_adjusted_cancel_image;
  paint_info.context.DrawImage(
      target_image, Image::kSyncDecode, ImageAutoDarkMode::Disabled(),
      ImagePaintTimingInfo(), gfx::RectF(painting_rect));
  return false;
}

gfx::Rect ThemePainterDefault::ApplyZoomToRect(
    const gfx::Rect& rect,
    const PaintInfo& paint_info,
    GraphicsContextStateSaver& state_saver,
    float zoom_level) {
  gfx::Rect unzoomed_rect = rect;
  if (zoom_level != 1) {
    state_saver.Save();
    unzoomed_rect.set_width(unzoomed_rect.width() / zoom_level);
    unzoomed_rect.set_height(unzoomed_rect.height() / zoom_level);
    paint_info.context.Translate(unzoomed_rect.x(), unzoomed_rect.y());
    paint_info.context.Scale(zoom_level, zoom_level);
    paint_info.context.Translate(-unzoomed_rect.x(), -unzoomed_rect.y());
  }

  return unzoomed_rect;
}

}  // namespace blink
