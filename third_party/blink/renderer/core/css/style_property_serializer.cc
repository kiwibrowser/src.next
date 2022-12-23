/*
 * (C) 1999-2003 Lars Knoll (knoll@kde.org)
 * Copyright (C) 2004, 2005, 2006, 2007, 2008, 2009, 2010, 2012 Apple Inc. All
 * rights reserved.
 * Copyright (C) 2011 Research In Motion Limited. All rights reserved.
 * Copyright (C) 2013 Intel Corporation. All rights reserved.
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

#include "third_party/blink/renderer/core/css/style_property_serializer.h"

#include <bitset>

#include "base/memory/values_equivalent.h"
#include "third_party/blink/renderer/core/animation/css/css_animation_data.h"
#include "third_party/blink/renderer/core/css/css_custom_property_declaration.h"
#include "third_party/blink/renderer/core/css/css_grid_template_areas_value.h"
#include "third_party/blink/renderer/core/css/css_identifier_value.h"
#include "third_party/blink/renderer/core/css/css_markup.h"
#include "third_party/blink/renderer/core/css/css_pending_substitution_value.h"
#include "third_party/blink/renderer/core/css/css_pending_system_font_value.h"
#include "third_party/blink/renderer/core/css/css_value_pair.h"
#include "third_party/blink/renderer/core/css/css_value_pool.h"
#include "third_party/blink/renderer/core/css/properties/css_property.h"
#include "third_party/blink/renderer/core/css/properties/longhand.h"
#include "third_party/blink/renderer/core/css_value_keywords.h"
#include "third_party/blink/renderer/core/style_property_shorthand.h"
#include "third_party/blink/renderer/platform/runtime_enabled_features.h"
#include "third_party/blink/renderer/platform/wtf/std_lib_extras.h"
#include "third_party/blink/renderer/platform/wtf/text/string_builder.h"

namespace blink {

StylePropertySerializer::CSSPropertyValueSetForSerializer::
    CSSPropertyValueSetForSerializer(const CSSPropertyValueSet& properties)
    : property_set_(&properties),
      all_index_(property_set_->FindPropertyIndex(CSSPropertyID::kAll)),
      need_to_expand_all_(false) {
  if (!HasAllProperty())
    return;

  CSSPropertyValueSet::PropertyReference all_property =
      property_set_->PropertyAt(all_index_);
  for (unsigned i = 0; i < property_set_->PropertyCount(); ++i) {
    CSSPropertyValueSet::PropertyReference property =
        property_set_->PropertyAt(i);
    if (property.IsAffectedByAll()) {
      if (all_property.IsImportant() && !property.IsImportant())
        continue;
      if (static_cast<unsigned>(all_index_) >= i)
        continue;
      if (property.Value() == all_property.Value() &&
          property.IsImportant() == all_property.IsImportant())
        continue;
      need_to_expand_all_ = true;
    }
    if (!IsCSSPropertyIDWithName(property.Id()))
      continue;
    longhand_property_used_.set(GetCSSPropertyIDIndex(property.Id()));
  }
}

void StylePropertySerializer::CSSPropertyValueSetForSerializer::Trace(
    blink::Visitor* visitor) const {
  visitor->Trace(property_set_);
}

unsigned
StylePropertySerializer::CSSPropertyValueSetForSerializer::PropertyCount()
    const {
  if (!HasExpandedAllProperty())
    return property_set_->PropertyCount();
  return kIntLastCSSProperty - kIntFirstCSSProperty + 1;
}

StylePropertySerializer::PropertyValueForSerializer
StylePropertySerializer::CSSPropertyValueSetForSerializer::PropertyAt(
    unsigned index) const {
  if (!HasExpandedAllProperty())
    return StylePropertySerializer::PropertyValueForSerializer(
        property_set_->PropertyAt(index));

  CSSPropertyID property_id =
      static_cast<CSSPropertyID>(index + kIntFirstCSSProperty);
  DCHECK(IsCSSPropertyIDWithName(property_id));
  if (longhand_property_used_.test(index)) {
    int real_index = property_set_->FindPropertyIndex(property_id);
    DCHECK_NE(real_index, -1);
    return StylePropertySerializer::PropertyValueForSerializer(
        property_set_->PropertyAt(real_index));
  }

  CSSPropertyValueSet::PropertyReference property =
      property_set_->PropertyAt(all_index_);
  return StylePropertySerializer::PropertyValueForSerializer(
      CSSProperty::Get(property_id).GetCSSPropertyName(), &property.Value(),
      property.IsImportant());
}

bool StylePropertySerializer::CSSPropertyValueSetForSerializer::
    ShouldProcessPropertyAt(unsigned index) const {
  // CSSPropertyValueSet has all valid longhands. We should process.
  if (!HasAllProperty())
    return true;

  // If all is not expanded, we need to process "all" and properties which
  // are not overwritten by "all".
  if (!need_to_expand_all_) {
    CSSPropertyValueSet::PropertyReference property =
        property_set_->PropertyAt(index);
    if (property.Id() == CSSPropertyID::kAll || !property.IsAffectedByAll())
      return true;
    if (!IsCSSPropertyIDWithName(property.Id()))
      return false;
    return longhand_property_used_.test(GetCSSPropertyIDIndex(property.Id()));
  }

  CSSPropertyID property_id =
      static_cast<CSSPropertyID>(index + kIntFirstCSSProperty);
  DCHECK(IsCSSPropertyIDWithName(property_id));
  const CSSProperty& property_class =
      CSSProperty::Get(ResolveCSSPropertyID(property_id));

  // Since "all" is expanded, we don't need to process "all".
  // We should not process expanded shorthands (e.g. font, background,
  // and so on) either.
  if (property_class.IsShorthand() ||
      property_class.IDEquals(CSSPropertyID::kAll))
    return false;

  // The all property is a shorthand that resets all CSS properties except
  // direction and unicode-bidi. It only accepts the CSS-wide keywords.
  // c.f. https://drafts.csswg.org/css-cascade/#all-shorthand
  if (!property_class.IsAffectedByAll())
    return longhand_property_used_.test(index);

  return true;
}

int StylePropertySerializer::CSSPropertyValueSetForSerializer::
    FindPropertyIndex(const CSSProperty& property) const {
  CSSPropertyID property_id = property.PropertyID();
  if (!HasExpandedAllProperty())
    return property_set_->FindPropertyIndex(property_id);
  return GetCSSPropertyIDIndex(property_id);
}

const CSSValue*
StylePropertySerializer::CSSPropertyValueSetForSerializer::GetPropertyCSSValue(
    const CSSProperty& property) const {
  int index = FindPropertyIndex(property);
  if (index == -1)
    return nullptr;
  StylePropertySerializer::PropertyValueForSerializer value = PropertyAt(index);
  return value.Value();
}

bool StylePropertySerializer::CSSPropertyValueSetForSerializer::
    IsDescriptorContext() const {
  return property_set_->CssParserMode() == kCSSViewportRuleMode ||
         property_set_->CssParserMode() == kCSSFontFaceRuleMode;
}

StylePropertySerializer::StylePropertySerializer(
    const CSSPropertyValueSet& properties)
    : property_set_(properties) {}

String StylePropertySerializer::GetCustomPropertyText(
    const PropertyValueForSerializer& property,
    bool is_not_first_decl) const {
  DCHECK_EQ(property.Name().Id(), CSSPropertyID::kVariable);
  StringBuilder result;
  if (is_not_first_decl)
    result.Append(' ');
  const auto* value = To<CSSCustomPropertyDeclaration>(property.Value());
  SerializeIdentifier(property.Name().ToAtomicString(), result,
                      is_not_first_decl);
  result.Append(':');
  if (!value->Value())
    result.Append(' ');
  result.Append(value->CustomCSSText());
  if (property.IsImportant())
    result.Append(" !important");
  result.Append(';');
  return result.ReleaseString();
}

String StylePropertySerializer::GetPropertyText(const CSSPropertyName& name,
                                                const String& value,
                                                bool is_important,
                                                bool is_not_first_decl) const {
  StringBuilder result;
  if (is_not_first_decl)
    result.Append(' ');
  result.Append(name.ToAtomicString());
  result.Append(": ");
  result.Append(value);
  if (is_important)
    result.Append(" !important");
  result.Append(';');
  return result.ReleaseString();
}

String StylePropertySerializer::AsText() const {
  StringBuilder result;

  std::bitset<kNumCSSProperties> longhand_serialized;
  std::bitset<kNumCSSProperties> shorthand_appeared;

  unsigned size = property_set_.PropertyCount();
  unsigned num_decls = 0;
  for (unsigned n = 0; n < size; ++n) {
    if (!property_set_.ShouldProcessPropertyAt(n))
      continue;

    StylePropertySerializer::PropertyValueForSerializer property =
        property_set_.PropertyAt(n);

    const CSSPropertyName& name = property.Name();
    CSSPropertyID property_id = name.Id();

#if DCHECK_IS_ON()
    if (property_id != CSSPropertyID::kVariable) {
      const CSSProperty& property_class = CSSProperty::Get(property_id);
      // Only web exposed properties should be part of the style.
      DCHECK(property_class.IsWebExposed());
      // All shorthand properties should have been expanded at parse time.
      DCHECK(property_set_.IsDescriptorContext() ||
             (property_class.IsProperty() && !property_class.IsShorthand()));
      DCHECK(!property_set_.IsDescriptorContext() ||
             property_class.IsDescriptor());
    }
#endif  // DCHECK_IS_ON()

    switch (property_id) {
      case CSSPropertyID::kVariable:
        result.Append(GetCustomPropertyText(property, num_decls++));
        continue;
      case CSSPropertyID::kAll:
        result.Append(GetPropertyText(name, property.Value()->CssText(),
                                      property.IsImportant(), num_decls++));
        continue;
      default:
        break;
    }
    if (longhand_serialized.test(GetCSSPropertyIDIndex(property_id)))
      continue;

    Vector<StylePropertyShorthand, 4> shorthands;
    getMatchingShorthandsForLonghand(property_id, &shorthands);
    bool serialized_as_shorthand = false;
    for (const StylePropertyShorthand& shorthand : shorthands) {
      // Some aliases are implemented as a shorthand, in which case
      // we prefer to not use the shorthand.
      if (shorthand.length() == 1)
        continue;

      CSSPropertyID shorthand_property = shorthand.id();
      int shorthand_property_index = GetCSSPropertyIDIndex(shorthand_property);
      // We already tried serializing as this shorthand
      if (shorthand_appeared.test(shorthand_property_index))
        continue;

      shorthand_appeared.set(shorthand_property_index);
      bool serialized_other_longhand = false;
      for (unsigned i = 0; i < shorthand.length(); i++) {
        if (longhand_serialized.test(GetCSSPropertyIDIndex(
                shorthand.properties()[i]->PropertyID()))) {
          serialized_other_longhand = true;
          break;
        }
      }
      if (serialized_other_longhand)
        continue;

      String shorthand_result = SerializeShorthand(shorthand_property);
      if (shorthand_result.IsEmpty())
        continue;

      result.Append(GetPropertyText(
          CSSProperty::Get(shorthand_property).GetCSSPropertyName(),
          shorthand_result, property.IsImportant(), num_decls++));
      serialized_as_shorthand = true;
      for (unsigned i = 0; i < shorthand.length(); i++) {
        longhand_serialized.set(
            GetCSSPropertyIDIndex(shorthand.properties()[i]->PropertyID()));
      }
      break;
    }

    if (serialized_as_shorthand)
      continue;

    result.Append(GetPropertyText(name, property.Value()->CssText(),
                                  property.IsImportant(), num_decls++));
  }

  DCHECK(!num_decls ^ !result.IsEmpty());
  return result.ReleaseString();
}

// As per css-cascade, shorthands do not expand longhands to the value
// "initial", except when the shorthand is set to "initial", instead
// setting "missing" sub-properties to their initial values. This means
// that a shorthand can never represent a list of subproperties where
// some are "initial" and some are not, and so serialization should
// always fail in these cases (as per cssom). However we currently use
// "initial" instead of the initial values for certain shorthands, so
// these are special-cased here.
// TODO(timloh): Don't use "initial" in shorthands and remove this
// special-casing
static bool AllowInitialInShorthand(CSSPropertyID property_id) {
  switch (property_id) {
    case CSSPropertyID::kBackground:
    case CSSPropertyID::kBorder:
    case CSSPropertyID::kBorderTop:
    case CSSPropertyID::kBorderRight:
    case CSSPropertyID::kBorderBottom:
    case CSSPropertyID::kBorderLeft:
    case CSSPropertyID::kBorderBlockStart:
    case CSSPropertyID::kBorderBlockEnd:
    case CSSPropertyID::kBorderInlineStart:
    case CSSPropertyID::kBorderInlineEnd:
    case CSSPropertyID::kBorderBlock:
    case CSSPropertyID::kBorderInline:
    case CSSPropertyID::kOutline:
    case CSSPropertyID::kColumnRule:
    case CSSPropertyID::kColumns:
    case CSSPropertyID::kFlexFlow:
    case CSSPropertyID::kGridColumn:
    case CSSPropertyID::kGridRow:
    case CSSPropertyID::kGridArea:
    case CSSPropertyID::kGap:
    case CSSPropertyID::kListStyle:
    case CSSPropertyID::kOffset:
    case CSSPropertyID::kTextDecoration:
    case CSSPropertyID::kTextEmphasis:
    case CSSPropertyID::kWebkitMask:
    case CSSPropertyID::kWebkitTextStroke:
      return true;
    default:
      return false;
  }
}

String StylePropertySerializer::CommonShorthandChecks(
    const StylePropertyShorthand& shorthand) const {
  int longhand_count = shorthand.length();
  if (!longhand_count || longhand_count > 17) {
    NOTREACHED();
    return g_empty_string;
  }

  const CSSValue* longhands[17] = {};

  bool has_important = false;
  bool has_non_important = false;

  for (int i = 0; i < longhand_count; i++) {
    int index = property_set_.FindPropertyIndex(*shorthand.properties()[i]);
    if (index == -1)
      return g_empty_string;
    PropertyValueForSerializer value = property_set_.PropertyAt(index);

    has_important |= value.IsImportant();
    has_non_important |= !value.IsImportant();
    longhands[i] = value.Value();
  }

  if (has_important && has_non_important)
    return g_empty_string;

  if (longhands[0]->IsCSSWideKeyword() ||
      longhands[0]->IsPendingSubstitutionValue()) {
    bool success = true;
    for (int i = 1; i < longhand_count; i++) {
      if (!base::ValuesEquivalent(longhands[i], longhands[0])) {
        // This should just return emptyString but some shorthands currently
        // allow 'initial' for their longhands.
        success = false;
        break;
      }
    }
    if (success) {
      if (const auto* substitution_value =
              DynamicTo<cssvalue::CSSPendingSubstitutionValue>(longhands[0])) {
        if (substitution_value->ShorthandPropertyId() != shorthand.id())
          return g_empty_string;
        return substitution_value->ShorthandValue()->CssText();
      }
      return longhands[0]->CssText();
    }
  }

  bool allow_initial = AllowInitialInShorthand(shorthand.id());
  for (int i = 0; i < longhand_count; i++) {
    const CSSValue& value = *longhands[i];
    if (!allow_initial && value.IsInitialValue())
      return g_empty_string;
    if ((value.IsCSSWideKeyword() && !value.IsInitialValue()) ||
        value.IsPendingSubstitutionValue()) {
      return g_empty_string;
    }
    if (value.IsVariableReferenceValue())
      return g_empty_string;
  }

  return String();
}

String StylePropertySerializer::SerializeShorthand(
    CSSPropertyID property_id) const {
  const StylePropertyShorthand& shorthand = shorthandForProperty(property_id);
  DCHECK(shorthand.length());

  String result = CommonShorthandChecks(shorthand);
  if (!result.IsNull())
    return result;

  switch (property_id) {
    case CSSPropertyID::kAnimation:
      return GetLayeredShorthandValue(animationShorthand());
    case CSSPropertyID::kBorderSpacing:
      return Get2Values(borderSpacingShorthand());
    case CSSPropertyID::kBackgroundPosition:
      return GetLayeredShorthandValue(backgroundPositionShorthand());
    case CSSPropertyID::kBackgroundRepeat:
      return BackgroundRepeatPropertyValue();
    case CSSPropertyID::kBackground:
      return GetLayeredShorthandValue(backgroundShorthand());
    case CSSPropertyID::kBorder:
      return BorderPropertyValue(borderWidthShorthand(), borderStyleShorthand(),
                                 borderColorShorthand());
    case CSSPropertyID::kBorderImage:
      return BorderImagePropertyValue();
    case CSSPropertyID::kBorderTop:
      return GetShorthandValue(borderTopShorthand());
    case CSSPropertyID::kBorderRight:
      return GetShorthandValue(borderRightShorthand());
    case CSSPropertyID::kBorderBottom:
      return GetShorthandValue(borderBottomShorthand());
    case CSSPropertyID::kBorderLeft:
      return GetShorthandValue(borderLeftShorthand());
    case CSSPropertyID::kBorderBlock:
      return BorderPropertyValue(borderBlockWidthShorthand(),
                                 borderBlockStyleShorthand(),
                                 borderBlockColorShorthand());
    case CSSPropertyID::kBorderBlockColor:
      return Get2Values(borderBlockColorShorthand());
    case CSSPropertyID::kBorderBlockStyle:
      return Get2Values(borderBlockStyleShorthand());
    case CSSPropertyID::kBorderBlockWidth:
      return Get2Values(borderBlockWidthShorthand());
    case CSSPropertyID::kBorderBlockStart:
      return GetShorthandValue(borderBlockStartShorthand());
    case CSSPropertyID::kBorderBlockEnd:
      return GetShorthandValue(borderBlockEndShorthand());
    case CSSPropertyID::kBorderInline:
      return BorderPropertyValue(borderInlineWidthShorthand(),
                                 borderInlineStyleShorthand(),
                                 borderInlineColorShorthand());
    case CSSPropertyID::kBorderInlineColor:
      return Get2Values(borderInlineColorShorthand());
    case CSSPropertyID::kBorderInlineStyle:
      return Get2Values(borderInlineStyleShorthand());
    case CSSPropertyID::kBorderInlineWidth:
      return Get2Values(borderInlineWidthShorthand());
    case CSSPropertyID::kBorderInlineStart:
      return GetShorthandValue(borderInlineStartShorthand());
    case CSSPropertyID::kBorderInlineEnd:
      return GetShorthandValue(borderInlineEndShorthand());
    case CSSPropertyID::kContainer:
      return ContainerValue();
    case CSSPropertyID::kOutline:
      return GetShorthandValue(outlineShorthand());
    case CSSPropertyID::kBorderColor:
      return Get4Values(borderColorShorthand());
    case CSSPropertyID::kBorderWidth:
      return Get4Values(borderWidthShorthand());
    case CSSPropertyID::kBorderStyle:
      return Get4Values(borderStyleShorthand());
    case CSSPropertyID::kColumnRule:
      return GetShorthandValue(columnRuleShorthand());
    case CSSPropertyID::kColumns:
      return GetShorthandValue(columnsShorthand());
    case CSSPropertyID::kContainIntrinsicSize:
      return ContainIntrinsicSizeValue();
    case CSSPropertyID::kFlex:
      return GetShorthandValue(flexShorthand());
    case CSSPropertyID::kFlexFlow:
      return GetShorthandValue(flexFlowShorthand());
    case CSSPropertyID::kGrid:
      return GetShorthandValueForGrid(gridShorthand());
    case CSSPropertyID::kGridTemplate:
      return GetShorthandValueForGridTemplate(gridTemplateShorthand());
    case CSSPropertyID::kGridColumn:
      return GetShorthandValue(gridColumnShorthand(), " / ");
    case CSSPropertyID::kGridRow:
      return GetShorthandValue(gridRowShorthand(), " / ");
    case CSSPropertyID::kGridArea:
      return GetShorthandValue(gridAreaShorthand(), " / ");
    case CSSPropertyID::kGap:
      return Get2Values(gapShorthand());
    case CSSPropertyID::kInset:
      return Get4Values(insetShorthand());
    case CSSPropertyID::kInsetBlock:
      return Get2Values(insetBlockShorthand());
    case CSSPropertyID::kInsetInline:
      return Get2Values(insetInlineShorthand());
    case CSSPropertyID::kPlaceContent:
      return Get2Values(placeContentShorthand());
    case CSSPropertyID::kPlaceItems:
      return Get2Values(placeItemsShorthand());
    case CSSPropertyID::kPlaceSelf:
      return Get2Values(placeSelfShorthand());
    case CSSPropertyID::kFont:
      return FontValue();
    case CSSPropertyID::kFontSynthesis:
      return FontSynthesisValue();
    case CSSPropertyID::kFontVariant:
      return FontVariantValue();
    case CSSPropertyID::kMargin:
      return Get4Values(marginShorthand());
    case CSSPropertyID::kMarginBlock:
      return Get2Values(marginBlockShorthand());
    case CSSPropertyID::kMarginInline:
      return Get2Values(marginInlineShorthand());
    case CSSPropertyID::kOffset:
      return OffsetValue();
    case CSSPropertyID::kOverflow:
      return Get2Values(overflowShorthand());
    case CSSPropertyID::kOverscrollBehavior:
      return Get2Values(overscrollBehaviorShorthand());
    case CSSPropertyID::kPadding:
      return Get4Values(paddingShorthand());
    case CSSPropertyID::kPaddingBlock:
      return Get2Values(paddingBlockShorthand());
    case CSSPropertyID::kPaddingInline:
      return Get2Values(paddingInlineShorthand());
    case CSSPropertyID::kTextDecoration:
      return TextDecorationValue();
    case CSSPropertyID::kTransition:
      return GetLayeredShorthandValue(transitionShorthand());
    case CSSPropertyID::kListStyle:
      return GetShorthandValue(listStyleShorthand());
    case CSSPropertyID::kWebkitMaskPosition:
      return GetLayeredShorthandValue(webkitMaskPositionShorthand());
    case CSSPropertyID::kWebkitMaskRepeat:
      return GetLayeredShorthandValue(webkitMaskRepeatShorthand());
    case CSSPropertyID::kWebkitMask:
      return GetLayeredShorthandValue(webkitMaskShorthand());
    case CSSPropertyID::kTextEmphasis:
      return GetShorthandValue(textEmphasisShorthand());
    case CSSPropertyID::kWebkitTextStroke:
      return GetShorthandValue(webkitTextStrokeShorthand());
    case CSSPropertyID::kMarker: {
      if (const CSSValue* start =
              property_set_.GetPropertyCSSValue(GetCSSPropertyMarkerStart())) {
        const CSSValue* mid =
            property_set_.GetPropertyCSSValue(GetCSSPropertyMarkerMid());
        const CSSValue* end =
            property_set_.GetPropertyCSSValue(GetCSSPropertyMarkerEnd());
        if (mid && end && *start == *mid && *start == *end)
          return start->CssText();
      }
      return String();
    }
    case CSSPropertyID::kBorderRadius:
      return BorderRadiusValue();
    case CSSPropertyID::kScrollPadding:
      return Get4Values(scrollPaddingShorthand());
    case CSSPropertyID::kScrollPaddingBlock:
      return Get2Values(scrollPaddingBlockShorthand());
    case CSSPropertyID::kScrollPaddingInline:
      return Get2Values(scrollPaddingInlineShorthand());
    case CSSPropertyID::kScrollMargin:
      return Get4Values(scrollMarginShorthand());
    case CSSPropertyID::kScrollMarginBlock:
      return Get2Values(scrollMarginBlockShorthand());
    case CSSPropertyID::kScrollMarginInline:
      return Get2Values(scrollMarginInlineShorthand());
    case CSSPropertyID::kPageBreakAfter:
      return PageBreakPropertyValue(pageBreakAfterShorthand());
    case CSSPropertyID::kPageBreakBefore:
      return PageBreakPropertyValue(pageBreakBeforeShorthand());
    case CSSPropertyID::kPageBreakInside:
      return PageBreakPropertyValue(pageBreakInsideShorthand());
    case CSSPropertyID::kToggle: {
      const CSSValue* toggle_root =
          property_set_.GetPropertyCSSValue(GetCSSPropertyToggleRoot());
      DCHECK(toggle_root);
      const CSSValue* toggle_trigger =
          property_set_.GetPropertyCSSValue(GetCSSPropertyToggleTrigger());
      DCHECK(toggle_trigger);
      if (!IsValidToggleShorthand(toggle_root, toggle_trigger)) {
        return g_empty_string;
      }
      return toggle_root->CssText();
    }
    case CSSPropertyID::kGridColumnGap:
    case CSSPropertyID::kGridGap:
    case CSSPropertyID::kGridRowGap:
    case CSSPropertyID::kWebkitColumnBreakAfter:
    case CSSPropertyID::kWebkitColumnBreakBefore:
    case CSSPropertyID::kWebkitColumnBreakInside:
    case CSSPropertyID::kWebkitMaskBoxImage:
      // Temporary exceptions to the NOTREACHED() below.
      // TODO(crbug.com/1316689): Write something real here.
      return String();
    default:
      NOTREACHED()
          << "Shorthand property "
          << CSSPropertyName(property_id).ToAtomicString()
          << " must be handled in StylePropertySerializer::SerializeShorthand.";
      return String();
  }
}

// The font shorthand only allows keyword font-stretch values. Thus, we check if
// a percentage value can be parsed as a keyword, and if so, serialize it as
// that keyword.
const CSSValue* GetFontStretchKeyword(const CSSValue* font_stretch_value) {
  if (IsA<CSSIdentifierValue>(font_stretch_value))
    return font_stretch_value;
  if (auto* primitive_value =
          DynamicTo<CSSPrimitiveValue>(font_stretch_value)) {
    double value = primitive_value->GetDoubleValue();
    if (value == 50)
      return CSSIdentifierValue::Create(CSSValueID::kUltraCondensed);
    if (value == 62.5)
      return CSSIdentifierValue::Create(CSSValueID::kExtraCondensed);
    if (value == 75)
      return CSSIdentifierValue::Create(CSSValueID::kCondensed);
    if (value == 87.5)
      return CSSIdentifierValue::Create(CSSValueID::kSemiCondensed);
    if (value == 100)
      return CSSIdentifierValue::Create(CSSValueID::kNormal);
    if (value == 112.5)
      return CSSIdentifierValue::Create(CSSValueID::kSemiExpanded);
    if (value == 125)
      return CSSIdentifierValue::Create(CSSValueID::kExpanded);
    if (value == 150)
      return CSSIdentifierValue::Create(CSSValueID::kExtraExpanded);
    if (value == 200)
      return CSSIdentifierValue::Create(CSSValueID::kUltraExpanded);
  }
  return nullptr;
}

// Returns false if the value cannot be represented in the font shorthand
bool StylePropertySerializer::AppendFontLonghandValueIfNotNormal(
    const CSSProperty& property,
    StringBuilder& result) const {
  int found_property_index = property_set_.FindPropertyIndex(property);
  DCHECK_NE(found_property_index, -1);

  const CSSValue* val = property_set_.PropertyAt(found_property_index).Value();
  if (property.IDEquals(CSSPropertyID::kFontStretch)) {
    const CSSValue* keyword = GetFontStretchKeyword(val);
    if (!keyword)
      return false;
    val = keyword;
  }
  auto* identifier_value = DynamicTo<CSSIdentifierValue>(val);
  if (identifier_value && identifier_value->GetValueID() == CSSValueID::kNormal)
    return true;

  if (!result.IsEmpty()) {
    switch (property.PropertyID()) {
      case CSSPropertyID::kFontStyle:
        break;  // No prefix.
      case CSSPropertyID::kFontFamily:
      case CSSPropertyID::kFontStretch:
      case CSSPropertyID::kFontVariantCaps:
      case CSSPropertyID::kFontVariantLigatures:
      case CSSPropertyID::kFontVariantNumeric:
      case CSSPropertyID::kFontVariantEastAsian:
      case CSSPropertyID::kFontWeight:
        result.Append(' ');
        break;
      case CSSPropertyID::kLineHeight:
        result.Append(" / ");
        break;
      default:
        NOTREACHED();
    }
  }

  String value;
  // In the font-variant shorthand a "none" ligatures value needs to be
  // expanded.
  if (property.IDEquals(CSSPropertyID::kFontVariantLigatures) &&
      identifier_value && identifier_value->GetValueID() == CSSValueID::kNone) {
    value =
        "no-common-ligatures no-discretionary-ligatures "
        "no-historical-ligatures no-contextual";
  } else {
    value = val->CssText();
  }

  result.Append(value);
  return true;
}

String StylePropertySerializer::ContainerValue() const {
  CHECK_EQ(containerShorthand().length(), 2u);
  CHECK_EQ(containerShorthand().properties()[0],
           &GetCSSPropertyContainerName());
  CHECK_EQ(containerShorthand().properties()[1],
           &GetCSSPropertyContainerType());

  CSSValueList* list = CSSValueList::CreateSlashSeparated();

  const CSSValue* name =
      property_set_.GetPropertyCSSValue(GetCSSPropertyContainerName());
  const CSSValue* type =
      property_set_.GetPropertyCSSValue(GetCSSPropertyContainerType());

  DCHECK(name);
  DCHECK(type);

  list->Append(*name);

  if (!(IsA<CSSIdentifierValue>(type) &&
        To<CSSIdentifierValue>(*type).GetValueID() == CSSValueID::kNormal)) {
    list->Append(*type);
  }

  return list->CssText();
}

String StylePropertySerializer::FontValue() const {
  int font_size_property_index =
      property_set_.FindPropertyIndex(GetCSSPropertyFontSize());
  int font_family_property_index =
      property_set_.FindPropertyIndex(GetCSSPropertyFontFamily());
  int font_variant_caps_property_index =
      property_set_.FindPropertyIndex(GetCSSPropertyFontVariantCaps());
  int font_variant_ligatures_property_index =
      property_set_.FindPropertyIndex(GetCSSPropertyFontVariantLigatures());
  int font_variant_numeric_property_index =
      property_set_.FindPropertyIndex(GetCSSPropertyFontVariantNumeric());
  int font_variant_east_asian_property_index =
      property_set_.FindPropertyIndex(GetCSSPropertyFontVariantEastAsian());
  DCHECK_NE(font_size_property_index, -1);
  DCHECK_NE(font_family_property_index, -1);
  DCHECK_NE(font_variant_caps_property_index, -1);
  DCHECK_NE(font_variant_ligatures_property_index, -1);
  DCHECK_NE(font_variant_numeric_property_index, -1);
  DCHECK_NE(font_variant_east_asian_property_index, -1);

  PropertyValueForSerializer font_size_property =
      property_set_.PropertyAt(font_size_property_index);
  PropertyValueForSerializer font_family_property =
      property_set_.PropertyAt(font_family_property_index);
  PropertyValueForSerializer font_variant_caps_property =
      property_set_.PropertyAt(font_variant_caps_property_index);
  PropertyValueForSerializer font_variant_ligatures_property =
      property_set_.PropertyAt(font_variant_ligatures_property_index);
  PropertyValueForSerializer font_variant_numeric_property =
      property_set_.PropertyAt(font_variant_numeric_property_index);
  PropertyValueForSerializer font_variant_east_asian_property =
      property_set_.PropertyAt(font_variant_east_asian_property_index);

  // Check that non-initial font-variant subproperties are not conflicting with
  // this serialization.
  const CSSValue* ligatures_value = font_variant_ligatures_property.Value();
  const CSSValue* numeric_value = font_variant_numeric_property.Value();
  const CSSValue* east_asian_value = font_variant_east_asian_property.Value();

  auto* ligatures_identifier_value =
      DynamicTo<CSSIdentifierValue>(ligatures_value);
  if ((ligatures_identifier_value &&
       ligatures_identifier_value->GetValueID() != CSSValueID::kNormal) ||
      ligatures_value->IsValueList())
    return g_empty_string;

  auto* numeric_identifier_value = DynamicTo<CSSIdentifierValue>(numeric_value);
  if ((numeric_identifier_value &&
       numeric_identifier_value->GetValueID() != CSSValueID::kNormal) ||
      numeric_value->IsValueList())
    return g_empty_string;

  auto* east_asian_identifier_value =
      DynamicTo<CSSIdentifierValue>(east_asian_value);
  if ((east_asian_identifier_value &&
       east_asian_identifier_value->GetValueID() != CSSValueID::kNormal) ||
      east_asian_value->IsValueList())
    return g_empty_string;

  const StylePropertyShorthand& shorthand = fontShorthand();
  const CSSProperty** longhands = shorthand.properties();
  unsigned length = shorthand.length();
  const CSSValue* first = property_set_.GetPropertyCSSValue(*longhands[0]);
  if (const auto* system_font =
          DynamicTo<cssvalue::CSSPendingSystemFontValue>(first)) {
    for (unsigned i = 1; i < length; i++) {
      const CSSValue* value = property_set_.GetPropertyCSSValue(*longhands[i]);
      if (!base::ValuesEquivalent(first, value))
        return g_empty_string;
    }
    return getValueName(system_font->SystemFontId());
  } else {
    for (unsigned i = 1; i < length; i++) {
      const CSSValue* value = property_set_.GetPropertyCSSValue(*longhands[i]);
      if (value->IsPendingSystemFontValue())
        return g_empty_string;
    }
  }

  StringBuilder result;
  AppendFontLonghandValueIfNotNormal(GetCSSPropertyFontStyle(), result);

  const CSSValue* val = font_variant_caps_property.Value();
  auto* identifier_value = DynamicTo<CSSIdentifierValue>(val);
  if (identifier_value &&
      (identifier_value->GetValueID() != CSSValueID::kSmallCaps &&
       identifier_value->GetValueID() != CSSValueID::kNormal))
    return g_empty_string;
  AppendFontLonghandValueIfNotNormal(GetCSSPropertyFontVariantCaps(), result);

  AppendFontLonghandValueIfNotNormal(GetCSSPropertyFontWeight(), result);
  bool font_stretch_valid =
      AppendFontLonghandValueIfNotNormal(GetCSSPropertyFontStretch(), result);
  if (!font_stretch_valid)
    return String();
  if (!result.IsEmpty())
    result.Append(' ');
  result.Append(font_size_property.Value()->CssText());
  AppendFontLonghandValueIfNotNormal(GetCSSPropertyLineHeight(), result);
  if (!result.IsEmpty())
    result.Append(' ');
  result.Append(font_family_property.Value()->CssText());
  return result.ReleaseString();
}

String StylePropertySerializer::FontVariantValue() const {
  StringBuilder result;

  // TODO(drott): Decide how we want to return ligature values in shorthands,
  // reduced to "none" or spelled out, filed as W3C bug:
  // https://www.w3.org/Bugs/Public/show_bug.cgi?id=29594
  AppendFontLonghandValueIfNotNormal(GetCSSPropertyFontVariantLigatures(),
                                     result);
  AppendFontLonghandValueIfNotNormal(GetCSSPropertyFontVariantCaps(), result);
  AppendFontLonghandValueIfNotNormal(GetCSSPropertyFontVariantNumeric(),
                                     result);
  AppendFontLonghandValueIfNotNormal(GetCSSPropertyFontVariantEastAsian(),
                                     result);

  if (result.IsEmpty()) {
    return "normal";
  }

  return result.ReleaseString();
}

String StylePropertySerializer::FontSynthesisValue() const {
  StringBuilder result;

  int font_synthesis_weight_property_index =
      property_set_.FindPropertyIndex(GetCSSPropertyFontSynthesisWeight());
  int font_synthesis_style_property_index =
      property_set_.FindPropertyIndex(GetCSSPropertyFontSynthesisStyle());
  int font_synthesis_small_caps_property_index =
      property_set_.FindPropertyIndex(GetCSSPropertyFontSynthesisSmallCaps());
  DCHECK_NE(font_synthesis_weight_property_index, -1);
  DCHECK_NE(font_synthesis_style_property_index, -1);
  DCHECK_NE(font_synthesis_small_caps_property_index, -1);

  PropertyValueForSerializer font_synthesis_weight_property =
      property_set_.PropertyAt(font_synthesis_weight_property_index);
  PropertyValueForSerializer font_synthesis_style_property =
      property_set_.PropertyAt(font_synthesis_style_property_index);
  PropertyValueForSerializer font_synthesis_small_caps_property =
      property_set_.PropertyAt(font_synthesis_small_caps_property_index);

  const CSSValue* font_synthesis_weight_value =
      font_synthesis_weight_property.Value();
  const CSSValue* font_synthesis_style_value =
      font_synthesis_style_property.Value();
  const CSSValue* font_synthesis_small_caps_value =
      font_synthesis_small_caps_property.Value();

  auto* font_synthesis_weight_identifier_value =
      DynamicTo<CSSIdentifierValue>(font_synthesis_weight_value);
  if (font_synthesis_weight_identifier_value &&
      font_synthesis_weight_identifier_value->GetValueID() ==
          CSSValueID::kAuto) {
    result.Append("weight");
  }

  auto* font_synthesis_style_identifier_value =
      DynamicTo<CSSIdentifierValue>(font_synthesis_style_value);
  if (font_synthesis_style_identifier_value &&
      font_synthesis_style_identifier_value->GetValueID() ==
          CSSValueID::kAuto) {
    if (!result.IsEmpty())
      result.Append(' ');
    result.Append("style");
  }

  auto* font_synthesis_small_caps_identifier_value =
      DynamicTo<CSSIdentifierValue>(font_synthesis_small_caps_value);
  if (font_synthesis_small_caps_identifier_value &&
      font_synthesis_small_caps_identifier_value->GetValueID() ==
          CSSValueID::kAuto) {
    if (!result.IsEmpty())
      result.Append(' ');
    result.Append("small-caps");
  }

  if (result.IsEmpty())
    return "none";

  return result.ReleaseString();
}

String StylePropertySerializer::OffsetValue() const {
  StringBuilder result;
  if (RuntimeEnabledFeatures::CSSOffsetPositionAnchorEnabled()) {
    const CSSValue* position =
        property_set_.GetPropertyCSSValue(GetCSSPropertyOffsetPosition());
    if (!position->IsInitialValue()) {
      result.Append(position->CssText());
    }
  }
  const CSSValue* path =
      property_set_.GetPropertyCSSValue(GetCSSPropertyOffsetPath());
  const CSSValue* distance =
      property_set_.GetPropertyCSSValue(GetCSSPropertyOffsetDistance());
  const CSSValue* rotate =
      property_set_.GetPropertyCSSValue(GetCSSPropertyOffsetRotate());
  if (!path->IsInitialValue()) {
    if (!result.IsEmpty())
      result.Append(" ");
    result.Append(path->CssText());
    if (!distance->IsInitialValue()) {
      result.Append(" ");
      result.Append(distance->CssText());
    }
    if (!rotate->IsInitialValue()) {
      result.Append(" ");
      result.Append(rotate->CssText());
    }
  } else {
    // The longhand values cannot be serialized as a valid shorthand value.
    // Serialize them as individual longhands instead.
    if (!distance->IsInitialValue() || !rotate->IsInitialValue())
      return String();
  }
  if (RuntimeEnabledFeatures::CSSOffsetPositionAnchorEnabled()) {
    const CSSValue* anchor =
        property_set_.GetPropertyCSSValue(GetCSSPropertyOffsetAnchor());
    if (!anchor->IsInitialValue()) {
      result.Append(" / ");
      result.Append(anchor->CssText());
    }
  }
  return result.ReleaseString();
}

String StylePropertySerializer::TextDecorationValue() const {
  StringBuilder result;
  const auto& shorthand = shorthandForProperty(CSSPropertyID::kTextDecoration);
  for (unsigned i = 0; i < shorthand.length(); ++i) {
    const CSSValue* value =
        property_set_.GetPropertyCSSValue(*shorthand.properties()[i]);
    String value_text = value->CssText();
    if (value->IsInitialValue())
      continue;
    if (shorthand.properties()[i]->PropertyID() ==
        CSSPropertyID::kTextDecorationThickness) {
      if (auto* identifier_value = DynamicTo<CSSIdentifierValue>(value)) {
        // Do not include initial value 'auto' for thickness.
        // TODO(https://crbug.com/1093826): general shorthand serialization
        // issues remain, in particular for text-decoration.
        CSSValueID value_id = identifier_value->GetValueID();
        if (value_id == CSSValueID::kAuto)
          continue;
      }
    }
    if (!result.IsEmpty())
      result.Append(" ");
    result.Append(value_text);
  }

  if (result.IsEmpty()) {
    return "none";
  }
  return result.ReleaseString();
}

String StylePropertySerializer::Get2Values(
    const StylePropertyShorthand& shorthand) const {
  // Assume the properties are in the usual order start, end.
  int start_value_index =
      property_set_.FindPropertyIndex(*shorthand.properties()[0]);
  int end_value_index =
      property_set_.FindPropertyIndex(*shorthand.properties()[1]);

  if (start_value_index == -1 || end_value_index == -1)
    return String();

  PropertyValueForSerializer start =
      property_set_.PropertyAt(start_value_index);
  PropertyValueForSerializer end = property_set_.PropertyAt(end_value_index);

  bool show_end = !base::ValuesEquivalent(start.Value(), end.Value());

  StringBuilder result;
  result.Append(start.Value()->CssText());
  if (show_end) {
    result.Append(' ');
    result.Append(end.Value()->CssText());
  }
  return result.ReleaseString();
}

String StylePropertySerializer::Get4Values(
    const StylePropertyShorthand& shorthand) const {
  // Assume the properties are in the usual order top, right, bottom, left.
  int top_value_index =
      property_set_.FindPropertyIndex(*shorthand.properties()[0]);
  int right_value_index =
      property_set_.FindPropertyIndex(*shorthand.properties()[1]);
  int bottom_value_index =
      property_set_.FindPropertyIndex(*shorthand.properties()[2]);
  int left_value_index =
      property_set_.FindPropertyIndex(*shorthand.properties()[3]);

  if (top_value_index == -1 || right_value_index == -1 ||
      bottom_value_index == -1 || left_value_index == -1)
    return String();

  PropertyValueForSerializer top = property_set_.PropertyAt(top_value_index);
  PropertyValueForSerializer right =
      property_set_.PropertyAt(right_value_index);
  PropertyValueForSerializer bottom =
      property_set_.PropertyAt(bottom_value_index);
  PropertyValueForSerializer left = property_set_.PropertyAt(left_value_index);

  bool show_left = !base::ValuesEquivalent(right.Value(), left.Value());
  bool show_bottom =
      !base::ValuesEquivalent(top.Value(), bottom.Value()) || show_left;
  bool show_right =
      !base::ValuesEquivalent(top.Value(), right.Value()) || show_bottom;

  StringBuilder result;
  result.Append(top.Value()->CssText());
  if (show_right) {
    result.Append(' ');
    result.Append(right.Value()->CssText());
  }
  if (show_bottom) {
    result.Append(' ');
    result.Append(bottom.Value()->CssText());
  }
  if (show_left) {
    result.Append(' ');
    result.Append(left.Value()->CssText());
  }
  return result.ReleaseString();
}

String StylePropertySerializer::GetLayeredShorthandValue(
    const StylePropertyShorthand& shorthand) const {
  const unsigned size = shorthand.length();

  // Begin by collecting the properties into a vector.
  HeapVector<Member<const CSSValue>> values(size);
  // If the below loop succeeds, there should always be at minimum 1 layer.
  wtf_size_t num_layers = 1U;

  // TODO(timloh): Shouldn't we fail if the lists are differently sized, with
  // the exception of background-color?
  for (unsigned i = 0; i < size; i++) {
    values[i] = property_set_.GetPropertyCSSValue(*shorthand.properties()[i]);
    if (values[i]->IsBaseValueList()) {
      const CSSValueList* value_list = To<CSSValueList>(values[i].Get());
      num_layers = std::max(num_layers, value_list->length());
    }
  }

  StringBuilder result;

  // Now stitch the properties together.
  for (wtf_size_t layer = 0; layer < num_layers; layer++) {
    StringBuilder layer_result;
    bool use_repeat_x_shorthand = false;
    bool use_repeat_y_shorthand = false;
    bool use_single_word_shorthand = false;
    bool found_position_xcss_property = false;
    bool found_position_ycss_property = false;

    for (unsigned property_index = 0; property_index < size; property_index++) {
      const CSSValue* value = nullptr;
      const CSSProperty* property = shorthand.properties()[property_index];

      // Get a CSSValue for this property and layer.
      if (values[property_index]->IsBaseValueList()) {
        const auto* property_values =
            To<CSSValueList>(values[property_index].Get());
        // There might not be an item for this layer for this property.
        if (layer < property_values->length())
          value = &property_values->Item(layer);
      } else if ((layer == 0 &&
                  !property->IDEquals(CSSPropertyID::kBackgroundColor)) ||
                 (layer == num_layers - 1 &&
                  property->IDEquals(CSSPropertyID::kBackgroundColor))) {
        // Singletons except background color belong in the 0th layer.
        // Background color belongs in the last layer.
        value = values[property_index];
      }
      // No point proceeding if there's not a value to look at.
      if (!value)
        continue;

      // Special case for background-repeat.
      if (property->IDEquals(CSSPropertyID::kBackgroundRepeatX) ||
          property->IDEquals(CSSPropertyID::kWebkitMaskRepeatX)) {
        DCHECK(shorthand.properties()[property_index + 1]->IDEquals(
                   CSSPropertyID::kBackgroundRepeatY) ||
               shorthand.properties()[property_index + 1]->IDEquals(
                   CSSPropertyID::kWebkitMaskRepeatY));
        auto* value_list =
            DynamicTo<CSSValueList>(values[property_index + 1].Get());
        const CSSValue& y_value =
            value_list ? value_list->Item(layer) : *values[property_index + 1];

        // FIXME: At some point we need to fix this code to avoid returning an
        // invalid shorthand, since some longhand combinations are not
        // serializable into a single shorthand.
        if (!IsA<CSSIdentifierValue>(value) ||
            !IsA<CSSIdentifierValue>(y_value))
          continue;

        CSSValueID x_id = To<CSSIdentifierValue>(value)->GetValueID();
        CSSValueID y_id = To<CSSIdentifierValue>(y_value).GetValueID();
        // Maybe advance propertyIndex to look at the next CSSValue in the list
        // for the checks below.
        if (x_id == y_id) {
          use_single_word_shorthand = true;
          property = shorthand.properties()[++property_index];
        } else if (x_id == CSSValueID::kRepeat &&
                   y_id == CSSValueID::kNoRepeat) {
          use_repeat_x_shorthand = true;
          property = shorthand.properties()[++property_index];
        } else if (x_id == CSSValueID::kNoRepeat &&
                   y_id == CSSValueID::kRepeat) {
          use_repeat_y_shorthand = true;
          property = shorthand.properties()[++property_index];
        }
      }

      bool is_initial_value = value->IsInitialValue();

      // When serializing shorthands, a component value must be omitted
      // if doesn't change the meaning of the overall value.
      // https://drafts.csswg.org/cssom/#serializing-css-values
      if (property->IDEquals(CSSPropertyID::kAnimationTimeline)) {
        if (auto* ident = DynamicTo<CSSIdentifierValue>(value)) {
          if (ident->GetValueID() ==
              CSSAnimationData::InitialTimeline().GetKeyword()) {
            DCHECK(RuntimeEnabledFeatures::CSSScrollTimelineEnabled());
            is_initial_value = true;
          }
        }
      }

      if (!is_initial_value) {
        if (property->IDEquals(CSSPropertyID::kBackgroundSize) ||
            property->IDEquals(CSSPropertyID::kWebkitMaskSize)) {
          if (found_position_ycss_property || found_position_xcss_property)
            layer_result.Append(" / ");
          else
            layer_result.Append(" 0% 0% / ");
        } else if (!layer_result.IsEmpty()) {
          // Do this second to avoid ending up with an extra space in the output
          // if we hit the continue above.
          layer_result.Append(' ');
        }

        if (use_repeat_x_shorthand) {
          use_repeat_x_shorthand = false;
          layer_result.Append(getValueName(CSSValueID::kRepeatX));
        } else if (use_repeat_y_shorthand) {
          use_repeat_y_shorthand = false;
          layer_result.Append(getValueName(CSSValueID::kRepeatY));
        } else {
          if (use_single_word_shorthand)
            use_single_word_shorthand = false;
          layer_result.Append(value->CssText());
        }
        if (property->IDEquals(CSSPropertyID::kBackgroundPositionX) ||
            property->IDEquals(CSSPropertyID::kWebkitMaskPositionX))
          found_position_xcss_property = true;
        if (property->IDEquals(CSSPropertyID::kBackgroundPositionY) ||
            property->IDEquals(CSSPropertyID::kWebkitMaskPositionY)) {
          found_position_ycss_property = true;
          // background-position is a special case. If only the first offset is
          // specified, the second one defaults to "center", not the same value.
        }
      }
    }
    if (!layer_result.IsEmpty()) {
      if (!result.IsEmpty())
        result.Append(", ");
      result.Append(layer_result);
    }
  }

  return result.ReleaseString();
}

String StylePropertySerializer::GetShorthandValue(
    const StylePropertyShorthand& shorthand,
    String separator) const {
  StringBuilder result;
  for (unsigned i = 0; i < shorthand.length(); ++i) {
    const CSSValue* value =
        property_set_.GetPropertyCSSValue(*shorthand.properties()[i]);
    String value_text = value->CssText();
    if (value->IsInitialValue())
      continue;
    if (!result.IsEmpty())
      result.Append(separator);
    result.Append(value_text);
  }
  return result.ReleaseString();
}

namespace {

String NamedGridAreaTextForPosition(const NamedGridAreaMap& grid_area_map,
                                    wtf_size_t row,
                                    wtf_size_t column) {
  for (const auto& item : grid_area_map) {
    const GridArea& area = item.value;
    if (row >= area.rows.StartLine() && row < area.rows.EndLine() &&
        column >= area.columns.StartLine() && column < area.columns.EndLine()) {
      return item.key;
    }
  }
  return ".";
}

}  // namespace

String StylePropertySerializer::GetShorthandValueForGrid(
    const StylePropertyShorthand& shorthand) const {
  DCHECK_EQ(shorthand.length(), 6u);

  const CSSValue* auto_flow_values =
      property_set_.GetPropertyCSSValue(*shorthand.properties()[3]);
  const CSSValue* auto_row_values =
      property_set_.GetPropertyCSSValue(*shorthand.properties()[4]);
  const CSSValue* auto_column_values =
      property_set_.GetPropertyCSSValue(*shorthand.properties()[5]);

  // 1- <'grid-template'>
  if (IsA<CSSIdentifierValue>(auto_flow_values) &&
      To<CSSIdentifierValue>(auto_flow_values)->GetValueID() ==
          CSSValueID::kRow &&
      IsA<CSSIdentifierValue>(auto_row_values) &&
      To<CSSIdentifierValue>(auto_row_values)->GetValueID() ==
          CSSValueID::kAuto &&
      IsA<CSSIdentifierValue>(auto_column_values) &&
      To<CSSIdentifierValue>(auto_column_values)->GetValueID() ==
          CSSValueID::kAuto) {
    return GetShorthandValueForGridTemplate(shorthand);
  }

  // If we have grid-auto-{flow,row,column} along with named lines, we can't
  // serialize as a "grid" shorthand, as a syntax that combines the two is not
  // valid per the grammar.
  const CSSValue* template_area_value =
      property_set_.GetPropertyCSSValue(*shorthand.properties()[2]);
  if (*template_area_value !=
      *(To<Longhand>(GetCSSPropertyGridTemplateAreas()).InitialValue()))
    return String();

  const CSSValue* template_row_values =
      property_set_.GetPropertyCSSValue(*shorthand.properties()[0]);
  const CSSValue* template_column_values =
      property_set_.GetPropertyCSSValue(*shorthand.properties()[1]);
  const CSSValueList* auto_flow_value_list =
      DynamicTo<CSSValueList>(auto_flow_values);

  StringBuilder auto_flow_text;
  auto_flow_text.Append("auto-flow ");
  if (auto_flow_value_list &&
      auto_flow_value_list->HasValue(
          *CSSIdentifierValue::Create(CSSValueID::kDense))) {
    auto_flow_text.Append("dense ");
  }

  // 2- <'grid-template-rows'> / [ auto-flow && dense? ] <'grid-auto-columns'>?
  // | [ auto-flow && dense? ] <'grid-auto-rows'>? / <'grid-template-columns'>
  StringBuilder result;
  if (auto_flow_value_list &&
      auto_flow_value_list->HasValue(
          *CSSIdentifierValue::Create(CSSValueID::kColumn))) {
    result.Append(template_row_values->CssText());
    result.Append(" / ");
    result.Append(auto_flow_text);
    result.Append(auto_column_values->CssText());
  } else {
    result.Append(auto_flow_text);
    result.Append(auto_row_values->CssText());
    result.Append(" / ");
    result.Append(template_column_values->CssText());
  }
  return result.ReleaseString();
}

String StylePropertySerializer::GetShorthandValueForGridTemplate(
    const StylePropertyShorthand& shorthand) const {
  const CSSValue* template_row_values =
      property_set_.GetPropertyCSSValue(*shorthand.properties()[0]);
  const CSSValue* template_column_values =
      property_set_.GetPropertyCSSValue(*shorthand.properties()[1]);
  const CSSValue* template_area_values =
      property_set_.GetPropertyCSSValue(*shorthand.properties()[2]);

  // 1- 'none' case.
  if (IsA<CSSIdentifierValue>(template_row_values) &&
      To<CSSIdentifierValue>(template_row_values)->GetValueID() ==
          CSSValueID::kNone &&
      IsA<CSSIdentifierValue>(template_column_values) &&
      To<CSSIdentifierValue>(template_column_values)->GetValueID() ==
          CSSValueID::kNone) {
    return "none";
  }

  const auto* template_row_value_list =
      DynamicTo<CSSValueList>(template_row_values);
  StringBuilder result;

  // 2- <grid-template-rows> / <grid-template-columns>
  if (!template_row_value_list ||
      (IsA<CSSIdentifierValue>(template_area_values) &&
       To<CSSIdentifierValue>(template_area_values)->GetValueID() ==
           CSSValueID::kNone)) {
    result.Append(template_row_values->CssText());
    result.Append(" / ");
    result.Append(template_column_values->CssText());
    return result.ReleaseString();
  }

  // 3- [ <line-names>? <string> <track-size>? <line-names>? ]+
  // [ / <track-list> ]?
  if (template_row_value_list->length() == 1 &&
      IsA<CSSIdentifierValue>(template_row_value_list->Item(0)) &&
      To<CSSIdentifierValue>(template_row_value_list->Item(0)).GetValueID() ==
          CSSValueID::kAuto) {
    // If the |template_row_value_list| has only one value and it is 'auto',
    // then we append the 'grid-template-area' values.
    result.Append(template_area_values->CssText());
  } else {
    const auto* template_areas =
        DynamicTo<cssvalue::CSSGridTemplateAreasValue>(template_area_values);
    DCHECK(template_areas);
    const NamedGridAreaMap& grid_area_map = template_areas->GridAreaMap();
    wtf_size_t grid_area_column_count = template_areas->ColumnCount();
    wtf_size_t grid_area_index = 0;
    for (const auto& row_value : *template_row_value_list) {
      const String row_value_text = row_value->CssText();
      if (row_value->IsGridLineNamesValue()) {
        if (!result.IsEmpty())
          result.Append(' ');
        result.Append(row_value_text);
        continue;
      }
      StringBuilder grid_area_text;
      for (wtf_size_t column = 0; column < grid_area_column_count; ++column) {
        grid_area_text.Append(NamedGridAreaTextForPosition(
            grid_area_map, grid_area_index, column));
        if (column != grid_area_column_count - 1)
          grid_area_text.Append(' ');
      }
      if (!grid_area_text.IsEmpty()) {
        if (!result.IsEmpty())
          result.Append(' ');
        result.Append('"');
        result.Append(grid_area_text);
        result.Append('"');
        ++grid_area_index;
      }
      if (!result.IsEmpty())
        result.Append(' ');
      result.Append(row_value_text);
    }
  }
  if (!(IsA<CSSIdentifierValue>(template_column_values) &&
        To<CSSIdentifierValue>(template_column_values)->GetValueID() ==
            CSSValueID::kNone)) {
    result.Append(" / ");
    result.Append(template_column_values->CssText());
  }
  return result.ReleaseString();
}

// only returns a non-null value if all properties have the same, non-null value
String StylePropertySerializer::GetCommonValue(
    const StylePropertyShorthand& shorthand) const {
  String res;
  for (unsigned i = 0; i < shorthand.length(); ++i) {
    const CSSValue* value =
        property_set_.GetPropertyCSSValue(*shorthand.properties()[i]);
    // FIXME: CSSInitialValue::CssText should generate the right value.
    String text = value->CssText();
    if (res.IsNull())
      res = text;
    else if (res != text)
      return String();
  }
  return res;
}

String StylePropertySerializer::BorderPropertyValue(
    const StylePropertyShorthand& width,
    const StylePropertyShorthand& style,
    const StylePropertyShorthand& color) const {
  const CSSProperty* border_image_properties[] = {
      &GetCSSPropertyBorderImageSource(), &GetCSSPropertyBorderImageSlice(),
      &GetCSSPropertyBorderImageWidth(), &GetCSSPropertyBorderImageOutset(),
      &GetCSSPropertyBorderImageRepeat()};

  // If any of the border-image longhands differ from their initial
  // specified values, we should not serialize to a border shorthand
  // declaration.
  for (const auto* border_image_property : border_image_properties) {
    const CSSValue* value =
        property_set_.GetPropertyCSSValue(*border_image_property);
    const CSSValue* initial_specified_value =
        To<Longhand>(*border_image_property).InitialValue();
    if (value && !value->IsInitialValue() &&
        *value != *initial_specified_value) {
      return String();
    }
  }

  const StylePropertyShorthand shorthand_properties[3] = {width, style, color};
  StringBuilder result;
  for (const auto& shorthand_property : shorthand_properties) {
    const String value = GetCommonValue(shorthand_property);
    if (value.IsNull())
      return String();
    if (value == "initial")
      continue;
    if (!result.IsEmpty())
      result.Append(' ');
    result.Append(value);
  }
  return result.IsEmpty() ? String() : result.ReleaseString();
}

String StylePropertySerializer::BorderImagePropertyValue() const {
  StringBuilder result;
  const CSSProperty* properties[] = {
      &GetCSSPropertyBorderImageSource(), &GetCSSPropertyBorderImageSlice(),
      &GetCSSPropertyBorderImageWidth(), &GetCSSPropertyBorderImageOutset(),
      &GetCSSPropertyBorderImageRepeat()};
  size_t length = std::size(properties);
  for (size_t i = 0; i < length; ++i) {
    const CSSValue& value = *property_set_.GetPropertyCSSValue(*properties[i]);
    if (!result.IsEmpty())
      result.Append(" ");
    if (i == 2 || i == 3)
      result.Append("/ ");
    result.Append(value.CssText());
  }
  return result.ReleaseString();
}

String StylePropertySerializer::BorderRadiusValue() const {
  auto serialize = [](const CSSValue& top_left, const CSSValue& top_right,
                      const CSSValue& bottom_right,
                      const CSSValue& bottom_left) -> String {
    bool show_bottom_left = !(top_right == bottom_left);
    bool show_bottom_right = !(top_left == bottom_right) || show_bottom_left;
    bool show_top_right = !(top_left == top_right) || show_bottom_right;

    StringBuilder result;
    result.Append(top_left.CssText());
    if (show_top_right) {
      result.Append(' ');
      result.Append(top_right.CssText());
    }
    if (show_bottom_right) {
      result.Append(' ');
      result.Append(bottom_right.CssText());
    }
    if (show_bottom_left) {
      result.Append(' ');
      result.Append(bottom_left.CssText());
    }
    return result.ReleaseString();
  };

  const CSSValuePair& top_left = To<CSSValuePair>(
      *property_set_.GetPropertyCSSValue(GetCSSPropertyBorderTopLeftRadius()));
  const CSSValuePair& top_right = To<CSSValuePair>(
      *property_set_.GetPropertyCSSValue(GetCSSPropertyBorderTopRightRadius()));
  const CSSValuePair& bottom_right =
      To<CSSValuePair>(*property_set_.GetPropertyCSSValue(
          GetCSSPropertyBorderBottomRightRadius()));
  const CSSValuePair& bottom_left =
      To<CSSValuePair>(*property_set_.GetPropertyCSSValue(
          GetCSSPropertyBorderBottomLeftRadius()));

  StringBuilder builder;
  builder.Append(serialize(top_left.First(), top_right.First(),
                           bottom_right.First(), bottom_left.First()));

  if (!(top_left.First() == top_left.Second()) ||
      !(top_right.First() == top_right.Second()) ||
      !(bottom_right.First() == bottom_right.Second()) ||
      !(bottom_left.First() == bottom_left.Second())) {
    builder.Append(" / ");
    builder.Append(serialize(top_left.Second(), top_right.Second(),
                             bottom_right.Second(), bottom_left.Second()));
  }

  return builder.ReleaseString();
}

static void AppendBackgroundRepeatValue(StringBuilder& builder,
                                        const CSSValue& repeat_xcss_value,
                                        const CSSValue& repeat_ycss_value) {
  // FIXME: Ensure initial values do not appear in CSS_VALUE_LISTS.
  DEFINE_STATIC_LOCAL(const Persistent<CSSIdentifierValue>,
                      initial_repeat_value,
                      (CSSIdentifierValue::Create(CSSValueID::kRepeat)));
  const CSSIdentifierValue& repeat_x =
      repeat_xcss_value.IsInitialValue()
          ? *initial_repeat_value
          : To<CSSIdentifierValue>(repeat_xcss_value);
  const CSSIdentifierValue& repeat_y =
      repeat_ycss_value.IsInitialValue()
          ? *initial_repeat_value
          : To<CSSIdentifierValue>(repeat_ycss_value);
  CSSValueID repeat_x_value_id = repeat_x.GetValueID();
  CSSValueID repeat_y_value_id = repeat_y.GetValueID();
  if (repeat_x_value_id == repeat_y_value_id) {
    builder.Append(repeat_x.CssText());
  } else if (repeat_x_value_id == CSSValueID::kNoRepeat &&
             repeat_y_value_id == CSSValueID::kRepeat) {
    builder.Append("repeat-y");
  } else if (repeat_x_value_id == CSSValueID::kRepeat &&
             repeat_y_value_id == CSSValueID::kNoRepeat) {
    builder.Append("repeat-x");
  } else {
    builder.Append(repeat_x.CssText());
    builder.Append(' ');
    builder.Append(repeat_y.CssText());
  }
}

String StylePropertySerializer::BackgroundRepeatPropertyValue() const {
  const CSSValue& repeat_x =
      *property_set_.GetPropertyCSSValue(GetCSSPropertyBackgroundRepeatX());
  const CSSValue& repeat_y =
      *property_set_.GetPropertyCSSValue(GetCSSPropertyBackgroundRepeatY());

  const auto* repeat_x_list = DynamicTo<CSSValueList>(repeat_x);
  int repeat_x_length = 1;
  if (repeat_x_list)
    repeat_x_length = repeat_x_list->length();
  else if (!repeat_x.IsIdentifierValue())
    return String();

  const auto* repeat_y_list = DynamicTo<CSSValueList>(repeat_y);
  int repeat_y_length = 1;
  if (repeat_y_list)
    repeat_y_length = repeat_y_list->length();
  else if (!repeat_y.IsIdentifierValue())
    return String();

  size_t shorthand_length =
      LowestCommonMultiple(repeat_x_length, repeat_y_length);
  StringBuilder builder;
  for (size_t i = 0; i < shorthand_length; ++i) {
    if (i)
      builder.Append(", ");

    const CSSValue& x_value =
        repeat_x_list ? repeat_x_list->Item(i % repeat_x_list->length())
                      : repeat_x;
    const CSSValue& y_value =
        repeat_y_list ? repeat_y_list->Item(i % repeat_y_list->length())
                      : repeat_y;
    AppendBackgroundRepeatValue(builder, x_value, y_value);
  }
  return builder.ReleaseString();
}

String StylePropertySerializer::PageBreakPropertyValue(
    const StylePropertyShorthand& shorthand) const {
  const CSSValue* value =
      property_set_.GetPropertyCSSValue(*shorthand.properties()[0]);
  CSSValueID value_id = To<CSSIdentifierValue>(value)->GetValueID();
  // https://drafts.csswg.org/css-break/#page-break-properties
  if (value_id == CSSValueID::kPage)
    return "always";
  if (value_id == CSSValueID::kAuto || value_id == CSSValueID::kLeft ||
      value_id == CSSValueID::kRight || value_id == CSSValueID::kAvoid)
    return value->CssText();
  return String();
}

String StylePropertySerializer::ContainIntrinsicSizeValue() const {
  // If the two values are identical, we return just one.
  String res = GetCommonValue(containIntrinsicSizeShorthand());
  if (!res.IsNull())
    return res;
  // Otherwise just serialize them in sequence.
  return GetShorthandValue(containIntrinsicSizeShorthand());
}

bool StylePropertySerializer::IsValidToggleShorthand(
    const CSSValue* toggle_root,
    const CSSValue* toggle_trigger) {
  if (const auto* toggle_root_ident =
          DynamicTo<CSSIdentifierValue>(toggle_root)) {
    DCHECK_EQ(toggle_root_ident->GetValueID(), CSSValueID::kNone);
    if (const auto* toggle_trigger_ident =
            DynamicTo<CSSIdentifierValue>(toggle_trigger)) {
      DCHECK_EQ(toggle_trigger_ident->GetValueID(), CSSValueID::kNone);
      return true;
    }
    return false;
  }

  const auto* toggle_trigger_list = DynamicTo<CSSValueList>(toggle_trigger);
  if (!toggle_trigger_list)
    return false;
  const auto* toggle_root_list = To<CSSValueList>(toggle_root);
  wtf_size_t length = toggle_trigger_list->length();
  if (length != toggle_root_list->length())
    return false;
  for (wtf_size_t i = 0; i < length; ++i) {
    const auto& toggle_root_item = To<CSSValueList>(toggle_root_list->Item(i));
    const auto& toggle_trigger_item =
        To<CSSValueList>(toggle_trigger_list->Item(i));
    if (toggle_trigger_item.length() > 1u) {
      DCHECK_EQ(toggle_trigger_item.length(), 2u);
      return false;
    }
    if (!base::ValuesEquivalent(&toggle_root_item.Item(0),
                                &toggle_trigger_item.Item(0))) {
      return false;
    }
  }
  return true;
}

}  // namespace blink
