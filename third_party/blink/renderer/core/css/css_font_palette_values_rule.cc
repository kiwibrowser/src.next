// Copyright 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/css/css_font_palette_values_rule.h"

#include "third_party/blink/renderer/core/css/css_style_sheet.h"
#include "third_party/blink/renderer/core/css/parser/at_rule_descriptor_parser.h"
#include "third_party/blink/renderer/core/css/parser/css_parser_context.h"
#include "third_party/blink/renderer/core/css/parser/css_tokenizer.h"
#include "third_party/blink/renderer/core/css/properties/css_parsing_utils.h"
#include "third_party/blink/renderer/core/css/style_engine.h"
#include "third_party/blink/renderer/core/css/style_rule_font_palette_values.h"
#include "third_party/blink/renderer/core/dom/document.h"
#include "third_party/blink/renderer/platform/wtf/text/string_builder.h"

namespace blink {

CSSFontPaletteValuesRule::CSSFontPaletteValuesRule(
    StyleRuleFontPaletteValues* font_palette_values_rule,
    CSSStyleSheet* sheet)
    : CSSRule(sheet), font_palette_values_rule_(font_palette_values_rule) {}

CSSFontPaletteValuesRule::~CSSFontPaletteValuesRule() = default;

String CSSFontPaletteValuesRule::cssText() const {
  StringBuilder result;
  result.Append("@font-palette-values ");
  result.Append(name());
  result.Append(" {");

  String font_family = fontFamily();
  if (font_family) {
    result.Append(" font-family: ");
    result.Append(font_family);
    result.Append(";");
  }

  String base_palette = basePalette();
  if (base_palette) {
    result.Append(" base-palette: ");
    result.Append(base_palette);
    result.Append(";");
  }

  String override_colors = overrideColors();
  if (!override_colors.IsEmpty()) {
    result.Append(" override-colors: ");
    result.Append(override_colors);
    result.Append(";");
  }

  result.Append(" }");
  return result.ReleaseString();
}

void CSSFontPaletteValuesRule::Reattach(StyleRuleBase* rule) {
  DCHECK(rule);
  font_palette_values_rule_ = To<StyleRuleFontPaletteValues>(rule);
}

String CSSFontPaletteValuesRule::name() const {
  return font_palette_values_rule_->GetName();
}

String CSSFontPaletteValuesRule::fontFamily() const {
  if (const CSSValue* value = font_palette_values_rule_->GetFontFamily())
    return value->CssText();
  return String();
}

String CSSFontPaletteValuesRule::basePalette() const {
  if (const CSSValue* value = font_palette_values_rule_->GetBasePalette())
    return value->CssText();
  return String();
}

String CSSFontPaletteValuesRule::overrideColors() const {
  if (const CSSValue* value = font_palette_values_rule_->GetOverrideColors())
    return value->CssText();
  return String();
}

void CSSFontPaletteValuesRule::Trace(Visitor* visitor) const {
  visitor->Trace(font_palette_values_rule_);
  CSSRule::Trace(visitor);
}

}  // namespace blink
