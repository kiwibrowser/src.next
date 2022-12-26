// Copyright 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/css/style_rule_font_palette_values.h"

#include "base/numerics/safe_conversions.h"
#include "third_party/blink/renderer/core/css/css_color.h"
#include "third_party/blink/renderer/core/css/css_font_family_value.h"
#include "third_party/blink/renderer/core/css/css_font_palette_values_rule.h"
#include "third_party/blink/renderer/core/css/css_identifier_value.h"
#include "third_party/blink/renderer/core/css/css_primitive_value.h"
#include "third_party/blink/renderer/core/css/css_value_list.h"
#include "third_party/blink/renderer/core/css/css_value_pair.h"
#include "third_party/blink/renderer/core/css/style_color.h"

namespace blink {

StyleRuleFontPaletteValues::StyleRuleFontPaletteValues(
    const AtomicString& name,
    CSSPropertyValueSet* properties)
    : StyleRuleBase(kFontPaletteValues),
      name_(name),
      font_family_(properties->GetPropertyCSSValue(CSSPropertyID::kFontFamily)),
      base_palette_(
          properties->GetPropertyCSSValue(CSSPropertyID::kBasePalette)),
      override_colors_(
          properties->GetPropertyCSSValue(CSSPropertyID::kOverrideColors)) {
  DCHECK(properties);
}

StyleRuleFontPaletteValues::StyleRuleFontPaletteValues(
    const StyleRuleFontPaletteValues&) = default;

StyleRuleFontPaletteValues::~StyleRuleFontPaletteValues() = default;

AtomicString StyleRuleFontPaletteValues::GetFontFamilyAsString() const {
  if (!font_family_ || !font_family_->IsFontFamilyValue())
    return g_empty_atom;

  return To<CSSFontFamilyValue>(*font_family_).Value();
}

FontPalette::BasePaletteValue StyleRuleFontPaletteValues::GetBasePaletteIndex()
    const {
  constexpr FontPalette::BasePaletteValue kNoBasePaletteValue = {
      FontPalette::kNoBasePalette, 0};
  if (!base_palette_) {
    return kNoBasePaletteValue;
  }

  if (auto* base_palette_identifier =
          DynamicTo<CSSIdentifierValue>(*base_palette_)) {
    switch (base_palette_identifier->GetValueID()) {
      case CSSValueID::kLight:
        return FontPalette::BasePaletteValue(
            {FontPalette::kLightBasePalette, 0});
      case CSSValueID::kDark:
        return FontPalette::BasePaletteValue(
            {FontPalette::kDarkBasePalette, 0});
      default:
        NOTREACHED();
        return kNoBasePaletteValue;
    }
  }

  const CSSPrimitiveValue& palette_primitive =
      To<CSSPrimitiveValue>(*base_palette_);
  return FontPalette::BasePaletteValue(
      {FontPalette::kIndexBasePalette, palette_primitive.GetIntValue()});
}

Vector<FontPalette::FontPaletteOverride>
StyleRuleFontPaletteValues::GetOverrideColorsAsVector() const {
  if (!override_colors_ || !override_colors_->IsValueList())
    return {};

  // Note: This function should not allocate Oilpan object, e.g. `CSSValue`,
  // because this function is called in font threads to determine primary
  // font data via `CSSFontSelector::GetFontData()`.
  // The test[1] reaches here.
  // [1] https://wpt.live/css/css-fonts/font-palette-35.html
  // TODO(yosin): Should we use ` ThreadState::NoAllocationScope` for main
  // thread? Font threads hit `DCHECK` because they don't have `ThreadState'.

  auto ConvertToSkColor = [](const CSSValuePair& override_pair) -> SkColor {
    if (override_pair.Second().IsIdentifierValue()) {
      const CSSIdentifierValue& color_identifier =
          To<CSSIdentifierValue>(override_pair.Second());
      // The value won't be a system color according to parsing, so we can pass
      // a fixed color scheme here.
      return static_cast<SkColor>(
          StyleColor::ColorFromKeyword(color_identifier.GetValueID(),
                                       mojom::blink::ColorScheme::kLight)
              .Rgb());
    }
    const cssvalue::CSSColor& css_color =
        To<cssvalue::CSSColor>(override_pair.Second());
    return static_cast<SkColor>(css_color.Value());
  };

  Vector<FontPalette::FontPaletteOverride> return_overrides;
  const CSSValueList& overrides_list = To<CSSValueList>(*override_colors_);
  for (auto& item : overrides_list) {
    const CSSValuePair& override_pair = To<CSSValuePair>(*item);

    const CSSPrimitiveValue& palette_index =
        To<CSSPrimitiveValue>(override_pair.First());
    DCHECK(palette_index.IsInteger());

    const SkColor override_color = ConvertToSkColor(override_pair);

    FontPalette::FontPaletteOverride palette_override{
        palette_index.GetIntValue(), override_color};
    return_overrides.push_back(palette_override);
  }

  return return_overrides;
}

void StyleRuleFontPaletteValues::TraceAfterDispatch(
    blink::Visitor* visitor) const {
  visitor->Trace(font_family_);
  visitor->Trace(base_palette_);
  visitor->Trace(override_colors_);
  StyleRuleBase::TraceAfterDispatch(visitor);
}

}  // namespace blink
