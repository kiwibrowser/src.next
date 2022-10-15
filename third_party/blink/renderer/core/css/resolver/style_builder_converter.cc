/*
 * Copyright (C) 2013 Google Inc. All rights reserved.
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "third_party/blink/renderer/core/css/resolver/style_builder_converter.h"

#include <algorithm>
#include <memory>
#include <utility>

#include "base/notreached.h"
#include "build/build_config.h"
#include "third_party/blink/renderer/core/css/basic_shape_functions.h"
#include "third_party/blink/renderer/core/css/css_axis_value.h"
#include "third_party/blink/renderer/core/css/css_color.h"
#include "third_party/blink/renderer/core/css/css_content_distribution_value.h"
#include "third_party/blink/renderer/core/css/css_custom_ident_value.h"
#include "third_party/blink/renderer/core/css/css_font_family_value.h"
#include "third_party/blink/renderer/core/css/css_font_feature_value.h"
#include "third_party/blink/renderer/core/css/css_font_style_range_value.h"
#include "third_party/blink/renderer/core/css/css_font_variation_value.h"
#include "third_party/blink/renderer/core/css/css_grid_auto_repeat_value.h"
#include "third_party/blink/renderer/core/css/css_grid_integer_repeat_value.h"
#include "third_party/blink/renderer/core/css/css_identifier_value.h"
#include "third_party/blink/renderer/core/css/css_math_expression_node.h"
#include "third_party/blink/renderer/core/css/css_math_function_value.h"
#include "third_party/blink/renderer/core/css/css_numeric_literal_value.h"
#include "third_party/blink/renderer/core/css/css_path_value.h"
#include "third_party/blink/renderer/core/css/css_pending_system_font_value.h"
#include "third_party/blink/renderer/core/css/css_primitive_value.h"
#include "third_party/blink/renderer/core/css/css_primitive_value_mappings.h"
#include "third_party/blink/renderer/core/css/css_quad_value.h"
#include "third_party/blink/renderer/core/css/css_ratio_value.h"
#include "third_party/blink/renderer/core/css/css_reflect_value.h"
#include "third_party/blink/renderer/core/css/css_shadow_value.h"
#include "third_party/blink/renderer/core/css/css_uri_value.h"
#include "third_party/blink/renderer/core/css/css_value_list.h"
#include "third_party/blink/renderer/core/css/parser/css_tokenizer.h"
#include "third_party/blink/renderer/core/css/resolver/filter_operation_resolver.h"
#include "third_party/blink/renderer/core/css/resolver/transform_builder.h"
#include "third_party/blink/renderer/core/css/style_engine.h"
#include "third_party/blink/renderer/core/frame/local_frame.h"
#include "third_party/blink/renderer/core/frame/web_feature.h"
#include "third_party/blink/renderer/core/style/reference_clip_path_operation.h"
#include "third_party/blink/renderer/core/style/shape_clip_path_operation.h"
#include "third_party/blink/renderer/core/style/style_overflow_clip_margin.h"
#include "third_party/blink/renderer/core/style/style_svg_resource.h"
#include "third_party/blink/renderer/platform/fonts/opentype/open_type_math_support.h"
#include "third_party/blink/renderer/platform/geometry/layout_unit.h"
#include "third_party/blink/renderer/platform/heap/garbage_collected.h"
#include "third_party/blink/renderer/platform/instrumentation/use_counter.h"
#include "third_party/blink/renderer/platform/wtf/hash_map.h"
#include "third_party/blink/renderer/platform/wtf/text/wtf_string.h"

namespace blink {

namespace {

static GridLength ConvertGridTrackBreadth(const StyleResolverState& state,
                                          const CSSValue& value) {
  // Fractional unit.
  auto* primitive_value = DynamicTo<CSSPrimitiveValue>(value);
  if (primitive_value && primitive_value->IsFlex())
    return GridLength(primitive_value->GetDoubleValue());

  auto* identifier_value = DynamicTo<CSSIdentifierValue>(value);
  if (identifier_value) {
    if (identifier_value->GetValueID() == CSSValueID::kMinContent)
      return Length::MinContent();
    if (identifier_value->GetValueID() == CSSValueID::kMaxContent)
      return Length::MaxContent();
  }

  return StyleBuilderConverter::ConvertLengthOrAuto(state, value);
}

}  // namespace

scoped_refptr<StyleReflection> StyleBuilderConverter::ConvertBoxReflect(
    StyleResolverState& state,
    const CSSValue& value) {
  if (auto* identifier_value = DynamicTo<CSSIdentifierValue>(value)) {
    DCHECK_EQ(identifier_value->GetValueID(), CSSValueID::kNone);
    return ComputedStyleInitialValues::InitialBoxReflect();
  }

  const auto& reflect_value = To<cssvalue::CSSReflectValue>(value);
  scoped_refptr<StyleReflection> reflection = StyleReflection::Create();
  reflection->SetDirection(
      reflect_value.Direction()->ConvertTo<CSSReflectionDirection>());
  if (reflect_value.Offset())
    reflection->SetOffset(reflect_value.Offset()->ConvertToLength(
        state.CssToLengthConversionData()));
  if (reflect_value.Mask()) {
    NinePieceImage mask = NinePieceImage::MaskDefaults();
    CSSToStyleMap::MapNinePieceImage(state, CSSPropertyID::kWebkitBoxReflect,
                                     *reflect_value.Mask(), mask);
    reflection->SetMask(mask);
  }

  return reflection;
}

scoped_refptr<StyleSVGResource> StyleBuilderConverter::ConvertElementReference(
    StyleResolverState& state,
    const CSSValue& value) {
  if (auto* identifier_value = DynamicTo<CSSIdentifierValue>(value)) {
    DCHECK_EQ(identifier_value->GetValueID(), CSSValueID::kNone);
    return nullptr;
  }

  const auto& url_value = To<cssvalue::CSSURIValue>(value);
  SVGResource* resource =
      state.GetElementStyleResources().GetSVGResourceFromValue(
          CSSPropertyID::kInvalid, url_value);
  return StyleSVGResource::Create(resource, url_value.ValueForSerialization());
}

LengthBox StyleBuilderConverter::ConvertClip(StyleResolverState& state,
                                             const CSSValue& value) {
  const CSSQuadValue& rect = To<CSSQuadValue>(value);

  return LengthBox(ConvertLengthOrAuto(state, *rect.Top()),
                   ConvertLengthOrAuto(state, *rect.Right()),
                   ConvertLengthOrAuto(state, *rect.Bottom()),
                   ConvertLengthOrAuto(state, *rect.Left()));
}

scoped_refptr<ClipPathOperation> StyleBuilderConverter::ConvertClipPath(
    StyleResolverState& state,
    const CSSValue& value) {
  if (value.IsBasicShapeValue() || value.IsPathValue())
    return ShapeClipPathOperation::Create(BasicShapeForValue(state, value));

  if (const auto* url_value = DynamicTo<cssvalue::CSSURIValue>(value)) {
    SVGResource* resource =
        state.GetElementStyleResources().GetSVGResourceFromValue(
            CSSPropertyID::kClipPath, *url_value);
    // TODO(fs): Doesn't work with external SVG references (crbug.com/109212.)
    return ReferenceClipPathOperation::Create(
        url_value->ValueForSerialization(), resource);
  }
  auto* identifier_value = DynamicTo<CSSIdentifierValue>(value);
  DCHECK(identifier_value &&
         identifier_value->GetValueID() == CSSValueID::kNone);
  return nullptr;
}

FilterOperations StyleBuilderConverter::ConvertFilterOperations(
    StyleResolverState& state,
    const CSSValue& value,
    CSSPropertyID property_id) {
  return FilterOperationResolver::CreateFilterOperations(state, value,
                                                         property_id);
}

FilterOperations StyleBuilderConverter::ConvertOffscreenFilterOperations(
    const CSSValue& value,
    const Font& font) {
  return FilterOperationResolver::CreateOffscreenFilterOperations(value, font);
}

static FontDescription::GenericFamilyType ConvertGenericFamily(
    CSSValueID value_id) {
  switch (value_id) {
    case CSSValueID::kWebkitBody:
      return FontDescription::kWebkitBodyFamily;
    case CSSValueID::kSerif:
      return FontDescription::kSerifFamily;
    case CSSValueID::kSansSerif:
      return FontDescription::kSansSerifFamily;
    case CSSValueID::kCursive:
      return FontDescription::kCursiveFamily;
    case CSSValueID::kFantasy:
      return FontDescription::kFantasyFamily;
    case CSSValueID::kMonospace:
      return FontDescription::kMonospaceFamily;
    default:
      return FontDescription::kNoFamily;
  }
}

static bool ConvertFontFamilyName(
    const CSSValue& value,
    FontDescription::GenericFamilyType& generic_family,
    AtomicString& family_name,
    FontBuilder* font_builder,
    const Document* document_for_count) {
  if (auto* font_family_value = DynamicTo<CSSFontFamilyValue>(value)) {
    generic_family = FontDescription::kNoFamily;
    family_name = font_family_value->Value();
  } else if (font_builder) {
    // TODO(crbug.com/1065468): Get rid of GenericFamilyType.
    auto cssValueID = To<CSSIdentifierValue>(value).GetValueID();
    generic_family = ConvertGenericFamily(cssValueID);
    if (generic_family != FontDescription::kNoFamily) {
      family_name = font_builder->GenericFontFamilyName(generic_family);
      if (document_for_count && cssValueID == CSSValueID::kWebkitBody &&
          !family_name.IsEmpty()) {
        // TODO(crbug.com/1065468): Remove this counter when it's no longer
        // necessary.
        document_for_count->CountUse(
            WebFeature::kFontBuilderCSSFontFamilyWebKitPrefixBody);
      }
    } else if (cssValueID == CSSValueID::kSystemUi) {
      family_name = font_family_names::kSystemUi;
    } else if (RuntimeEnabledFeatures::CSSFontFamilyMathEnabled() &&
               cssValueID == CSSValueID::kMath) {
      family_name = font_family_names::kMath;
    }
  }

  return !family_name.IsEmpty();
}

FontDescription::FamilyDescription StyleBuilderConverterBase::ConvertFontFamily(
    const CSSValue& value,
    FontBuilder* font_builder,
    const Document* document_for_count) {
  FontDescription::FamilyDescription desc(FontDescription::kNoFamily);

  if (const auto* system_font =
          DynamicTo<cssvalue::CSSPendingSystemFontValue>(value)) {
    desc.family.SetFamily(system_font->ResolveFontFamily(),
                          FontFamily::Type::kFamilyName);
    return desc;
  }

  FontFamily* curr_family = nullptr;

#if BUILDFLAG(IS_MAC)
  bool has_seen_system_ui = false;
#endif

  for (auto& family : To<CSSValueList>(value)) {
    FontDescription::GenericFamilyType generic_family =
        FontDescription::kNoFamily;
    AtomicString family_name;

    if (!ConvertFontFamilyName(*family, generic_family, family_name,
                               font_builder, document_for_count))
      continue;

    if (!curr_family) {
      curr_family = &desc.family;
    } else {
      scoped_refptr<SharedFontFamily> new_family = SharedFontFamily::Create();
      curr_family->AppendFamily(new_family);
      curr_family = new_family.get();
    }

    // TODO(crbug.com/1065468): Get rid of GenericFamilyType.
    bool is_generic = generic_family != FontDescription::kNoFamily ||
                      IsA<CSSIdentifierValue>(*family);
#if BUILDFLAG(IS_MAC)
    // TODO(https://crbug.com/554590): Remove this counter when it's no longer
    // necessary.
    if (!has_seen_system_ui) {
      has_seen_system_ui =
          is_generic && family_name == font_family_names::kSystemUi;
    }
    if (IsA<CSSFontFamilyValue>(*family) &&
        family_name == FontCache::LegacySystemFontFamily()) {
      family_name = font_family_names::kSystemUi;
      if (document_for_count && !has_seen_system_ui)
        document_for_count->CountUse(WebFeature::kBlinkMacSystemFont);
    }
#endif

    curr_family->SetFamily(family_name, is_generic
                                            ? FontFamily::Type::kGenericFamily
                                            : FontFamily::Type::kFamilyName);

    if (is_generic)
      desc.generic_family = generic_family;
  }

  return desc;
}

FontDescription::FamilyDescription StyleBuilderConverter::ConvertFontFamily(
    StyleResolverState& state,
    const CSSValue& value) {
  return StyleBuilderConverterBase::ConvertFontFamily(
      value,
      state.GetDocument().GetSettings() ? &state.GetFontBuilder() : nullptr,
      &state.GetDocument());
}

scoped_refptr<FontFeatureSettings>
StyleBuilderConverter::ConvertFontFeatureSettings(StyleResolverState& state,
                                                  const CSSValue& value) {
  auto* identifier_value = DynamicTo<CSSIdentifierValue>(value);
  if (identifier_value && identifier_value->GetValueID() == CSSValueID::kNormal)
    return FontBuilder::InitialFeatureSettings();

  const auto& list = To<CSSValueList>(value);
  scoped_refptr<FontFeatureSettings> settings = FontFeatureSettings::Create();
  int len = list.length();
  for (int i = 0; i < len; ++i) {
    const auto& feature = To<cssvalue::CSSFontFeatureValue>(list.Item(i));
    settings->Append(FontFeature(feature.Tag(), feature.Value()));
  }
  return settings;
}

static bool CompareTags(FontVariationAxis a, FontVariationAxis b) {
  return a.Tag() < b.Tag();
}

scoped_refptr<FontVariationSettings>
StyleBuilderConverter::ConvertFontVariationSettings(
    const StyleResolverState& state,
    const CSSValue& value) {
  auto* identifier_value = DynamicTo<CSSIdentifierValue>(value);
  if (identifier_value && identifier_value->GetValueID() == CSSValueID::kNormal)
    return FontBuilder::InitialVariationSettings();

  const auto& list = To<CSSValueList>(value);
  int len = list.length();
  HashMap<uint32_t, float> axes;
  // Use a temporary HashMap to remove duplicate tags, keeping the last
  // occurrence of each.
  for (int i = 0; i < len; ++i) {
    const auto& feature = To<cssvalue::CSSFontVariationValue>(list.Item(i));
    axes.Set(AtomicStringToFourByteTag(feature.Tag()), feature.Value());
  }
  scoped_refptr<FontVariationSettings> settings =
      FontVariationSettings::Create();
  for (auto& axis : axes) {
    settings->Append(FontVariationAxis(axis.key, axis.value));
  }
  std::sort(settings->begin(), settings->end(), CompareTags);
  return settings;
}

scoped_refptr<FontPalette> StyleBuilderConverter::ConvertFontPalette(
    StyleResolverState& state,
    const CSSValue& value) {
  auto* identifier_value = DynamicTo<CSSIdentifierValue>(value);
  if (identifier_value &&
      identifier_value->GetValueID() == CSSValueID::kNormal) {
    return nullptr;
  }

  if (identifier_value && identifier_value->GetValueID() == CSSValueID::kDark) {
    return FontPalette::Create(FontPalette::kDarkPalette);
  }

  if (identifier_value &&
      identifier_value->GetValueID() == CSSValueID::kLight) {
    return FontPalette::Create(FontPalette::kLightPalette);
  }

  auto* custom_identifier = DynamicTo<CSSCustomIdentValue>(value);
  if (custom_identifier) {
    return FontPalette::Create(custom_identifier->Value());
  }

  return nullptr;
}

float MathScriptScaleFactor(StyleResolverState& state) {
  int a = state.ParentStyle()->MathDepth();
  int b = state.Style()->MathDepth();
  if (b == a)
    return 1.0;
  bool invertScaleFactor = false;
  if (b < a) {
    std::swap(a, b);
    invertScaleFactor = true;
  }

  // Determine the scale factors from the inherited font.
  float defaultScaleDown = 0.71;
  int exponent = b - a;
  float scaleFactor = 1.0;
  if (const SimpleFontData* font_data =
          state.ParentStyle()->GetFont().PrimaryFont()) {
    HarfBuzzFace* parent_harfbuzz_face =
        font_data->PlatformData().GetHarfBuzzFace();
    if (OpenTypeMathSupport::HasMathData(parent_harfbuzz_face)) {
      float scriptPercentScaleDown =
          OpenTypeMathSupport::MathConstant(
              parent_harfbuzz_face,
              OpenTypeMathSupport::MathConstants::kScriptPercentScaleDown)
              .value_or(0);
      // Note: zero can mean both zero for the math constant and the fallback.
      if (!scriptPercentScaleDown)
        scriptPercentScaleDown = defaultScaleDown;
      float scriptScriptPercentScaleDown =
          OpenTypeMathSupport::MathConstant(
              parent_harfbuzz_face,
              OpenTypeMathSupport::MathConstants::kScriptScriptPercentScaleDown)
              .value_or(0);
      // Note: zero can mean both zero for the math constant and the fallback.
      if (!scriptScriptPercentScaleDown)
        scriptScriptPercentScaleDown = defaultScaleDown * defaultScaleDown;
      if (a <= 0 && b >= 2) {
        scaleFactor *= scriptScriptPercentScaleDown;
        exponent -= 2;
      } else if (a == 1) {
        scaleFactor *= scriptScriptPercentScaleDown / scriptPercentScaleDown;
        exponent--;
      } else if (b == 1) {
        scaleFactor *= scriptPercentScaleDown;
        exponent--;
      }
    }
  }
  scaleFactor *= pow(defaultScaleDown, exponent);
  return invertScaleFactor ? 1 / scaleFactor : scaleFactor;
}

static float ComputeFontSize(const CSSToLengthConversionData& conversion_data,
                             const CSSPrimitiveValue& primitive_value,
                             const FontDescription::Size& parent_size) {
  if (primitive_value.IsLength())
    return primitive_value.ComputeLength<float>(conversion_data);
  if (primitive_value.IsCalculated()) {
    return To<CSSMathFunctionValue>(primitive_value)
        .ToCalcValue(conversion_data)
        ->Evaluate(parent_size.value);
  }
  NOTREACHED();
  return 0;
}

FontDescription::Size StyleBuilderConverterBase::ConvertFontSize(
    const CSSValue& value,
    const CSSToLengthConversionData& conversion_data,
    FontDescription::Size parent_size,
    const Document* document) {
  if (auto* identifier_value = DynamicTo<CSSIdentifierValue>(value)) {
    CSSValueID value_id = identifier_value->GetValueID();
    if (FontSizeFunctions::IsValidValueID(value_id)) {
      return FontDescription::Size(FontSizeFunctions::KeywordSize(value_id),
                                   0.0f, false);
    }
    if (value_id == CSSValueID::kSmaller)
      return FontDescription::SmallerSize(parent_size);
    if (value_id == CSSValueID::kLarger)
      return FontDescription::LargerSize(parent_size);
    NOTREACHED();
    return FontBuilder::InitialSize();
  }

  if (const auto* system_font =
          DynamicTo<cssvalue::CSSPendingSystemFontValue>(value)) {
    return FontDescription::Size(0, system_font->ResolveFontSize(document),
                                 true);
  }

  const auto& primitive_value = To<CSSPrimitiveValue>(value);
  if (primitive_value.IsPercentage()) {
    return FontDescription::Size(
        0, (primitive_value.GetFloatValue() * parent_size.value / 100.0f),
        parent_size.is_absolute);
  }

  // TODO(crbug.com/979895): This is the result of a refactoring, which might
  // have revealed an existing bug with calculated lengths. Investigate.
  const bool is_absolute =
      parent_size.is_absolute || primitive_value.IsMathFunctionValue() ||
      !To<CSSNumericLiteralValue>(primitive_value).IsFontRelativeLength() ||
      To<CSSNumericLiteralValue>(primitive_value).GetType() ==
          CSSPrimitiveValue::UnitType::kRems;
  return FontDescription::Size(
      0, ComputeFontSize(conversion_data, primitive_value, parent_size),
      is_absolute);
}

FontDescription::Size StyleBuilderConverter::ConvertFontSize(
    StyleResolverState& state,
    const CSSValue& value) {
  // FIXME: Find out when parentStyle could be 0?
  auto parent_size = state.ParentStyle()
                         ? state.ParentFontDescription().GetSize()
                         : FontDescription::Size(0, 0.0f, false);

  auto* identifier_value = DynamicTo<CSSIdentifierValue>(value);
  if (identifier_value && identifier_value->GetValueID() == CSSValueID::kMath) {
    auto scale_factor = MathScriptScaleFactor(state);
    state.Style()->SetHasGlyphRelativeUnits();
    return FontDescription::Size(0, (scale_factor * parent_size.value),
                                 parent_size.is_absolute);
  }

  return StyleBuilderConverterBase::ConvertFontSize(
      value, state.FontSizeConversionData(), parent_size, &state.GetDocument());
}

float StyleBuilderConverter::ConvertFontSizeAdjust(StyleResolverState& state,
                                                   const CSSValue& value) {
  auto* identifier_value = DynamicTo<CSSIdentifierValue>(value);
  if (identifier_value && identifier_value->GetValueID() == CSSValueID::kNone)
    return FontBuilder::InitialSizeAdjust();

  const auto& primitive_value = To<CSSPrimitiveValue>(value);
  DCHECK(primitive_value.IsNumber());
  return primitive_value.GetFloatValue();
}

FontSelectionValue StyleBuilderConverterBase::ConvertFontStretch(
    const blink::CSSValue& value) {
  if (const auto* primitive_value = DynamicTo<CSSPrimitiveValue>(value)) {
    if (primitive_value->IsPercentage())
      return ClampTo<FontSelectionValue>(primitive_value->GetFloatValue());
  }

  // TODO(drott) crbug.com/750014: Consider not parsing them as IdentifierValue
  // any more?
  if (const auto* identifier_value = DynamicTo<CSSIdentifierValue>(value)) {
    switch (identifier_value->GetValueID()) {
      case CSSValueID::kUltraCondensed:
        return UltraCondensedWidthValue();
      case CSSValueID::kExtraCondensed:
        return ExtraCondensedWidthValue();
      case CSSValueID::kCondensed:
        return CondensedWidthValue();
      case CSSValueID::kSemiCondensed:
        return SemiCondensedWidthValue();
      case CSSValueID::kNormal:
        return NormalWidthValue();
      case CSSValueID::kSemiExpanded:
        return SemiExpandedWidthValue();
      case CSSValueID::kExpanded:
        return ExpandedWidthValue();
      case CSSValueID::kExtraExpanded:
        return ExtraExpandedWidthValue();
      case CSSValueID::kUltraExpanded:
        return UltraExpandedWidthValue();
      default:
        break;
    }
  }

  if (value.IsPendingSystemFontValue())
    return NormalWidthValue();

  NOTREACHED();
  return NormalWidthValue();
}

FontSelectionValue StyleBuilderConverter::ConvertFontStretch(
    blink::StyleResolverState& state,
    const blink::CSSValue& value) {
  return StyleBuilderConverterBase::ConvertFontStretch(value);
}

FontSelectionValue StyleBuilderConverterBase::ConvertFontStyle(
    const CSSValue& value) {
  DCHECK(!value.IsPrimitiveValue());

  if (const auto* identifier_value = DynamicTo<CSSIdentifierValue>(value)) {
    switch (identifier_value->GetValueID()) {
      case CSSValueID::kItalic:
      case CSSValueID::kOblique:
        return ItalicSlopeValue();
      case CSSValueID::kNormal:
        return NormalSlopeValue();
      default:
        NOTREACHED();
        return NormalSlopeValue();
    }
  } else if (const auto* system_font =
                 DynamicTo<cssvalue::CSSPendingSystemFontValue>(value)) {
    if (system_font->ResolveFontStyle() == ItalicSlopeValue())
      return ItalicSlopeValue();
    return NormalSlopeValue();
  } else if (const auto* style_range_value =
                 DynamicTo<cssvalue::CSSFontStyleRangeValue>(value)) {
    const CSSValueList* values = style_range_value->GetObliqueValues();
    CHECK_LT(values->length(), 2u);
    if (values->length()) {
      return FontSelectionValue(
          To<CSSPrimitiveValue>(values->Item(0)).ComputeDegrees());
    } else {
      identifier_value = style_range_value->GetFontStyleValue();
      if (identifier_value->GetValueID() == CSSValueID::kNormal)
        return NormalSlopeValue();
      if (identifier_value->GetValueID() == CSSValueID::kItalic ||
          identifier_value->GetValueID() == CSSValueID::kOblique)
        return ItalicSlopeValue();
    }
  }

  NOTREACHED();
  return NormalSlopeValue();
}

FontSelectionValue StyleBuilderConverter::ConvertFontStyle(
    StyleResolverState& state,
    const CSSValue& value) {
  return StyleBuilderConverterBase::ConvertFontStyle(value);
}

FontSelectionValue StyleBuilderConverterBase::ConvertFontWeight(
    const CSSValue& value,
    FontSelectionValue parent_weight) {
  if (const auto* primitive_value = DynamicTo<CSSPrimitiveValue>(value)) {
    if (primitive_value->IsNumber())
      return ClampTo<FontSelectionValue>(primitive_value->GetFloatValue());
  }

  if (const auto* system_font =
          DynamicTo<cssvalue::CSSPendingSystemFontValue>(value))
    return system_font->ResolveFontWeight();

  if (const auto* identifier_value = DynamicTo<CSSIdentifierValue>(value)) {
    switch (identifier_value->GetValueID()) {
      case CSSValueID::kNormal:
        return NormalWeightValue();
      case CSSValueID::kBold:
        return BoldWeightValue();
      case CSSValueID::kBolder:
        return FontDescription::BolderWeight(parent_weight);
      case CSSValueID::kLighter:
        return FontDescription::LighterWeight(parent_weight);
      default:
        NOTREACHED();
        return NormalWeightValue();
    }
  }
  NOTREACHED();
  return NormalWeightValue();
}

FontSelectionValue StyleBuilderConverter::ConvertFontWeight(
    StyleResolverState& state,
    const CSSValue& value) {
  return StyleBuilderConverterBase::ConvertFontWeight(
      value, state.ParentStyle()->GetFontDescription().Weight());
}

FontDescription::FontVariantCaps
StyleBuilderConverterBase::ConvertFontVariantCaps(const CSSValue& value) {
  if (value.IsPendingSystemFontValue())
    return FontDescription::kCapsNormal;

  CSSValueID value_id = To<CSSIdentifierValue>(value).GetValueID();
  switch (value_id) {
    case CSSValueID::kNormal:
      return FontDescription::kCapsNormal;
    case CSSValueID::kSmallCaps:
      return FontDescription::kSmallCaps;
    case CSSValueID::kAllSmallCaps:
      return FontDescription::kAllSmallCaps;
    case CSSValueID::kPetiteCaps:
      return FontDescription::kPetiteCaps;
    case CSSValueID::kAllPetiteCaps:
      return FontDescription::kAllPetiteCaps;
    case CSSValueID::kUnicase:
      return FontDescription::kUnicase;
    case CSSValueID::kTitlingCaps:
      return FontDescription::kTitlingCaps;
    default:
      return FontDescription::kCapsNormal;
  }
}

FontDescription::FontVariantCaps StyleBuilderConverter::ConvertFontVariantCaps(
    StyleResolverState&,
    const CSSValue& value) {
  return StyleBuilderConverterBase::ConvertFontVariantCaps(value);
}

FontDescription::VariantLigatures
StyleBuilderConverter::ConvertFontVariantLigatures(StyleResolverState&,
                                                   const CSSValue& value) {
  if (const auto* value_list = DynamicTo<CSSValueList>(value)) {
    FontDescription::VariantLigatures ligatures;
    for (wtf_size_t i = 0; i < value_list->length(); ++i) {
      const CSSValue& item = value_list->Item(i);
      switch (To<CSSIdentifierValue>(item).GetValueID()) {
        case CSSValueID::kNoCommonLigatures:
          ligatures.common = FontDescription::kDisabledLigaturesState;
          break;
        case CSSValueID::kCommonLigatures:
          ligatures.common = FontDescription::kEnabledLigaturesState;
          break;
        case CSSValueID::kNoDiscretionaryLigatures:
          ligatures.discretionary = FontDescription::kDisabledLigaturesState;
          break;
        case CSSValueID::kDiscretionaryLigatures:
          ligatures.discretionary = FontDescription::kEnabledLigaturesState;
          break;
        case CSSValueID::kNoHistoricalLigatures:
          ligatures.historical = FontDescription::kDisabledLigaturesState;
          break;
        case CSSValueID::kHistoricalLigatures:
          ligatures.historical = FontDescription::kEnabledLigaturesState;
          break;
        case CSSValueID::kNoContextual:
          ligatures.contextual = FontDescription::kDisabledLigaturesState;
          break;
        case CSSValueID::kContextual:
          ligatures.contextual = FontDescription::kEnabledLigaturesState;
          break;
        default:
          NOTREACHED();
          break;
      }
    }
    return ligatures;
  }

  if (value.IsPendingSystemFontValue())
    return FontDescription::VariantLigatures();

  if (To<CSSIdentifierValue>(value).GetValueID() == CSSValueID::kNone) {
    return FontDescription::VariantLigatures(
        FontDescription::kDisabledLigaturesState);
  }

  DCHECK_EQ(To<CSSIdentifierValue>(value).GetValueID(), CSSValueID::kNormal);
  return FontDescription::VariantLigatures();
}

FontVariantNumeric StyleBuilderConverter::ConvertFontVariantNumeric(
    StyleResolverState&,
    const CSSValue& value) {
  if (auto* identifier_value = DynamicTo<CSSIdentifierValue>(value)) {
    DCHECK_EQ(identifier_value->GetValueID(), CSSValueID::kNormal);
    return FontVariantNumeric();
  }

  if (value.IsPendingSystemFontValue())
    return FontVariantNumeric();

  FontVariantNumeric variant_numeric;
  for (const CSSValue* feature : To<CSSValueList>(value)) {
    switch (To<CSSIdentifierValue>(feature)->GetValueID()) {
      case CSSValueID::kLiningNums:
        variant_numeric.SetNumericFigure(FontVariantNumeric::kLiningNums);
        break;
      case CSSValueID::kOldstyleNums:
        variant_numeric.SetNumericFigure(FontVariantNumeric::kOldstyleNums);
        break;
      case CSSValueID::kProportionalNums:
        variant_numeric.SetNumericSpacing(
            FontVariantNumeric::kProportionalNums);
        break;
      case CSSValueID::kTabularNums:
        variant_numeric.SetNumericSpacing(FontVariantNumeric::kTabularNums);
        break;
      case CSSValueID::kDiagonalFractions:
        variant_numeric.SetNumericFraction(
            FontVariantNumeric::kDiagonalFractions);
        break;
      case CSSValueID::kStackedFractions:
        variant_numeric.SetNumericFraction(
            FontVariantNumeric::kStackedFractions);
        break;
      case CSSValueID::kOrdinal:
        variant_numeric.SetOrdinal(FontVariantNumeric::kOrdinalOn);
        break;
      case CSSValueID::kSlashedZero:
        variant_numeric.SetSlashedZero(FontVariantNumeric::kSlashedZeroOn);
        break;
      default:
        NOTREACHED();
        break;
    }
  }
  return variant_numeric;
}

FontVariantEastAsian StyleBuilderConverter::ConvertFontVariantEastAsian(
    StyleResolverState&,
    const CSSValue& value) {
  if (auto* identifier_value = DynamicTo<CSSIdentifierValue>(value)) {
    DCHECK_EQ(identifier_value->GetValueID(), CSSValueID::kNormal);
    return FontVariantEastAsian();
  }

  if (value.IsPendingSystemFontValue())
    return FontVariantEastAsian();

  FontVariantEastAsian variant_east_asian;
  for (const CSSValue* feature : To<CSSValueList>(value)) {
    switch (To<CSSIdentifierValue>(feature)->GetValueID()) {
      case CSSValueID::kJis78:
        variant_east_asian.SetForm(FontVariantEastAsian::kJis78);
        break;
      case CSSValueID::kJis83:
        variant_east_asian.SetForm(FontVariantEastAsian::kJis83);
        break;
      case CSSValueID::kJis90:
        variant_east_asian.SetForm(FontVariantEastAsian::kJis90);
        break;
      case CSSValueID::kJis04:
        variant_east_asian.SetForm(FontVariantEastAsian::kJis04);
        break;
      case CSSValueID::kSimplified:
        variant_east_asian.SetForm(FontVariantEastAsian::kSimplified);
        break;
      case CSSValueID::kTraditional:
        variant_east_asian.SetForm(FontVariantEastAsian::kTraditional);
        break;
      case CSSValueID::kFullWidth:
        variant_east_asian.SetWidth(FontVariantEastAsian::kFullWidth);
        break;
      case CSSValueID::kProportionalWidth:
        variant_east_asian.SetWidth(FontVariantEastAsian::kProportionalWidth);
        break;
      case CSSValueID::kRuby:
        variant_east_asian.SetRuby(true);
        break;
      default:
        NOTREACHED();
        break;
    }
  }
  return variant_east_asian;
}

StyleSelfAlignmentData StyleBuilderConverter::ConvertSelfOrDefaultAlignmentData(
    StyleResolverState&,
    const CSSValue& value) {
  StyleSelfAlignmentData alignment_data =
      ComputedStyleInitialValues::InitialAlignSelf();
  if (const auto* pair = DynamicTo<CSSValuePair>(value)) {
    if (To<CSSIdentifierValue>(pair->First()).GetValueID() ==
        CSSValueID::kLegacy) {
      alignment_data.SetPositionType(ItemPositionType::kLegacy);
      alignment_data.SetPosition(
          To<CSSIdentifierValue>(pair->Second()).ConvertTo<ItemPosition>());
    } else if (To<CSSIdentifierValue>(pair->First()).GetValueID() ==
               CSSValueID::kFirst) {
      alignment_data.SetPosition(ItemPosition::kBaseline);
    } else if (To<CSSIdentifierValue>(pair->First()).GetValueID() ==
               CSSValueID::kLast) {
      alignment_data.SetPosition(ItemPosition::kLastBaseline);
    } else {
      alignment_data.SetOverflow(
          To<CSSIdentifierValue>(pair->First()).ConvertTo<OverflowAlignment>());
      alignment_data.SetPosition(
          To<CSSIdentifierValue>(pair->Second()).ConvertTo<ItemPosition>());
    }
  } else {
    alignment_data.SetPosition(
        To<CSSIdentifierValue>(value).ConvertTo<ItemPosition>());
  }
  return alignment_data;
}

StyleContentAlignmentData StyleBuilderConverter::ConvertContentAlignmentData(
    StyleResolverState&,
    const CSSValue& value) {
  StyleContentAlignmentData alignment_data =
      ComputedStyleInitialValues::InitialContentAlignment();
  const cssvalue::CSSContentDistributionValue& content_value =
      To<cssvalue::CSSContentDistributionValue>(value);
  if (IsValidCSSValueID(content_value.Distribution())) {
    alignment_data.SetDistribution(
        CSSIdentifierValue::Create(content_value.Distribution())
            ->ConvertTo<ContentDistributionType>());
  }
  if (IsValidCSSValueID(content_value.Position())) {
    alignment_data.SetPosition(
        CSSIdentifierValue::Create(content_value.Position())
            ->ConvertTo<ContentPosition>());
  }
  if (IsValidCSSValueID(content_value.Overflow())) {
    alignment_data.SetOverflow(
        CSSIdentifierValue::Create(content_value.Overflow())
            ->ConvertTo<OverflowAlignment>());
  }

  return alignment_data;
}

GridAutoFlow StyleBuilderConverter::ConvertGridAutoFlow(StyleResolverState&,
                                                        const CSSValue& value) {
  const auto* list = DynamicTo<CSSValueList>(&value);
  if (list)
    DCHECK_GE(list->length(), 1u);
  else
    DCHECK(value.IsIdentifierValue());

  const CSSIdentifierValue& first =
      To<CSSIdentifierValue>(list ? list->Item(0) : value);
  const CSSIdentifierValue* second =
      list && list->length() == 2 ? &To<CSSIdentifierValue>(list->Item(1))
                                  : nullptr;

  switch (first.GetValueID()) {
    case CSSValueID::kRow:
      if (second && second->GetValueID() == CSSValueID::kDense)
        return kAutoFlowRowDense;
      return kAutoFlowRow;
    case CSSValueID::kColumn:
      if (second && second->GetValueID() == CSSValueID::kDense)
        return kAutoFlowColumnDense;
      return kAutoFlowColumn;
    case CSSValueID::kDense:
      if (second && second->GetValueID() == CSSValueID::kColumn)
        return kAutoFlowColumnDense;
      return kAutoFlowRowDense;
    default:
      NOTREACHED();
      return ComputedStyleInitialValues::InitialGridAutoFlow();
  }
}

GridPosition StyleBuilderConverter::ConvertGridPosition(StyleResolverState&,
                                                        const CSSValue& value) {
  // We accept the specification's grammar:
  // 'auto' | [ <integer> || <custom-ident> ] |
  // [ span && [ <integer> || <custom-ident> ] ] | <custom-ident>

  GridPosition position;

  if (auto* ident_value = DynamicTo<CSSCustomIdentValue>(value)) {
    position.SetNamedGridArea(ident_value->Value());
    return position;
  }

  if (auto* identifier_value = DynamicTo<CSSIdentifierValue>(value)) {
    DCHECK_EQ(identifier_value->GetValueID(), CSSValueID::kAuto);
    return position;
  }

  const auto& values = To<CSSValueList>(value);
  DCHECK(values.length());

  bool is_span_position = false;
  // The specification makes the <integer> optional, in which case it default to
  // '1'.
  int grid_line_number = 1;
  AtomicString grid_line_name;

  auto* it = values.begin();
  const CSSValue* current_value = it->Get();
  auto* current_identifier_value = DynamicTo<CSSIdentifierValue>(current_value);
  if (current_identifier_value &&
      current_identifier_value->GetValueID() == CSSValueID::kSpan) {
    is_span_position = true;
    ++it;
    current_value = it != values.end() ? it->Get() : nullptr;
  }

  auto* current_primitive_value = DynamicTo<CSSPrimitiveValue>(current_value);
  if (current_primitive_value && current_primitive_value->IsNumber()) {
    grid_line_number = current_primitive_value->GetIntValue();
    ++it;
    current_value = it != values.end() ? it->Get() : nullptr;
  }

  auto* current_ident_value = DynamicTo<CSSCustomIdentValue>(current_value);
  if (current_ident_value) {
    grid_line_name = current_ident_value->Value();
    ++it;
  }

  DCHECK_EQ(it, values.end());
  if (is_span_position)
    position.SetSpanPosition(grid_line_number, grid_line_name);
  else
    position.SetExplicitPosition(grid_line_number, grid_line_name);

  return position;
}

GridTrackSize StyleBuilderConverter::ConvertGridTrackSize(
    StyleResolverState& state,
    const CSSValue& value) {
  if (value.IsPrimitiveValue() || value.IsIdentifierValue())
    return GridTrackSize(ConvertGridTrackBreadth(state, value));

  auto& function = To<CSSFunctionValue>(value);
  if (function.FunctionType() == CSSValueID::kFitContent) {
    SECURITY_DCHECK(function.length() == 1);
    return GridTrackSize(ConvertGridTrackBreadth(state, function.Item(0)),
                         kFitContentTrackSizing);
  }

  SECURITY_DCHECK(function.length() == 2);
  GridLength min_track_breadth(
      ConvertGridTrackBreadth(state, function.Item(0)));
  GridLength max_track_breadth(
      ConvertGridTrackBreadth(state, function.Item(1)));
  return GridTrackSize(min_track_breadth, max_track_breadth);
}

static void ConvertGridLineNamesList(
    const CSSValue& value,
    wtf_size_t current_named_grid_line,
    NamedGridLinesMap& named_grid_lines,
    OrderedNamedGridLines& ordered_named_grid_lines,
    bool is_in_repeat = false,
    bool is_first_repeat = false) {
  DCHECK(value.IsGridLineNamesValue());

  // TODO(ansollan): Serialize subgrid's empty lines.
  for (auto& named_grid_line_value : To<CSSValueList>(value)) {
    String named_grid_line =
        To<CSSCustomIdentValue>(*named_grid_line_value).Value();
    NamedGridLinesMap::AddResult result =
        named_grid_lines.insert(named_grid_line, Vector<wtf_size_t>());
    result.stored_value->value.push_back(current_named_grid_line);
    OrderedNamedGridLines::AddResult ordered_insertion_result =
        ordered_named_grid_lines.insert(current_named_grid_line,
                                        Vector<NamedGridLine>());
    ordered_insertion_result.stored_value->value.push_back(
        NamedGridLine(named_grid_line, is_in_repeat, is_first_repeat));
  }
}

GridTrackList StyleBuilderConverter::ConvertGridTrackSizeList(
    StyleResolverState& state,
    const CSSValue& value) {
  const CSSValueList* list = DynamicTo<CSSValueList>(value);
  if (!list) {
    const auto& ident = To<CSSIdentifierValue>(value);
    DCHECK_EQ(ident.GetValueID(), CSSValueID::kAuto);
    return GridTrackList(GridTrackSize(Length::Auto()));
  }

  Vector<GridTrackSize, 1> track_sizes;
  for (auto& curr_value : To<CSSValueList>(value)) {
    DCHECK(!curr_value->IsGridLineNamesValue());
    DCHECK(!curr_value->IsGridAutoRepeatValue());
    DCHECK(!curr_value->IsGridIntegerRepeatValue());
    track_sizes.push_back(ConvertGridTrackSize(state, *curr_value));
  }

  return GridTrackList(track_sizes);
}

void StyleBuilderConverter::ConvertGridTrackList(
    const CSSValue& value,
    ComputedGridTrackList& computed_grid_track_list,
    StyleResolverState& state) {
  if (auto* identifier_value = DynamicTo<CSSIdentifierValue>(value)) {
    DCHECK_EQ(identifier_value->GetValueID(), CSSValueID::kNone);
    return;
  }

  GridTrackList& track_sizes = computed_grid_track_list.track_sizes;
  Vector<GridTrackSize, 1>& auto_repeat_track_sizes =
      computed_grid_track_list.auto_repeat_track_sizes;

  wtf_size_t current_named_grid_line = 0;
  auto ConvertLineNameOrTrackSize = [&](const CSSValue& curr_value,
                                        bool is_in_repeat = false,
                                        bool is_first_repeat = false) {
    if (curr_value.IsGridLineNamesValue()) {
      ConvertGridLineNamesList(
          curr_value, current_named_grid_line,
          computed_grid_track_list.named_grid_lines,
          computed_grid_track_list.ordered_named_grid_lines, is_in_repeat,
          is_first_repeat);
      if (computed_grid_track_list.axis_type == GridAxisType::kSubgriddedAxis)
        ++current_named_grid_line;
    } else {
      DCHECK_EQ(computed_grid_track_list.axis_type,
                GridAxisType::kStandaloneAxis);
      ++current_named_grid_line;
      track_sizes.LegacyTrackList().push_back(
          ConvertGridTrackSize(state, curr_value));
    }
  };

  const auto& values = To<CSSValueList>(value);
  auto* curr_value = values.begin();

  if (RuntimeEnabledFeatures::LayoutNGSubgridEnabled()) {
    auto* identifier_value = DynamicTo<CSSIdentifierValue>(curr_value->Get());
    if (identifier_value &&
        identifier_value->GetValueID() == CSSValueID::kSubgrid) {
      computed_grid_track_list.axis_type = GridAxisType::kSubgriddedAxis;
      ++curr_value;
    }
  }

  for (; curr_value != values.end(); ++curr_value) {
    if (auto* grid_auto_repeat_value =
            DynamicTo<cssvalue::CSSGridAutoRepeatValue>(curr_value->Get())) {
      Vector<GridTrackSize, 1> repeated_track_sizes;
      wtf_size_t auto_repeat_index = 0;
      CSSValueID auto_repeat_id = grid_auto_repeat_value->AutoRepeatID();
      DCHECK(auto_repeat_id == CSSValueID::kAutoFill ||
             auto_repeat_id == CSSValueID::kAutoFit);
      computed_grid_track_list.auto_repeat_type =
          (auto_repeat_id == CSSValueID::kAutoFill) ? AutoRepeatType::kAutoFill
                                                    : AutoRepeatType::kAutoFit;
      for (const CSSValue* auto_repeat_value : To<CSSValueList>(**curr_value)) {
        if (auto_repeat_value->IsGridLineNamesValue()) {
          ConvertGridLineNamesList(
              *auto_repeat_value, auto_repeat_index,
              computed_grid_track_list.auto_repeat_named_grid_lines,
              computed_grid_track_list.auto_repeat_ordered_named_grid_lines);
          if (computed_grid_track_list.axis_type ==
              GridAxisType::kSubgriddedAxis)
            ++auto_repeat_index;
          continue;
        }
        ++auto_repeat_index;
        repeated_track_sizes.push_back(
            ConvertGridTrackSize(state, *auto_repeat_value));
      }
      if (RuntimeEnabledFeatures::LayoutNGEnabled()) {
        track_sizes.NGTrackList().AddRepeater(
            repeated_track_sizes,
            static_cast<NGGridTrackRepeater::RepeatType>(
                computed_grid_track_list.auto_repeat_type));
      }
      DCHECK(auto_repeat_track_sizes.IsEmpty());
      auto_repeat_track_sizes = std::move(repeated_track_sizes);
      computed_grid_track_list.auto_repeat_insertion_point =
          current_named_grid_line++;
      continue;
    }

    if (auto* grid_integer_repeat_value =
            DynamicTo<cssvalue::CSSGridIntegerRepeatValue>(curr_value->Get())) {
      const wtf_size_t repetitions = grid_integer_repeat_value->Repetitions();

      for (wtf_size_t i = 0; i < repetitions; ++i) {
        for (auto integer_repeat_value : *grid_integer_repeat_value) {
          ConvertLineNameOrTrackSize(*integer_repeat_value,
                                     /* is_inside_repeat */ true,
                                     /* is_first_repeat */ i == 0);
        }
      }

      if (RuntimeEnabledFeatures::LayoutNGEnabled() &&
          (computed_grid_track_list.axis_type ==
           GridAxisType::kStandaloneAxis)) {
        Vector<GridTrackSize, 1> repeater_track_sizes;
        for (auto integer_repeat_value : *grid_integer_repeat_value) {
          if (!integer_repeat_value->IsGridLineNamesValue()) {
            repeater_track_sizes.push_back(
                ConvertGridTrackSize(state, *integer_repeat_value));
          }
        }
        track_sizes.NGTrackList().AddRepeater(
            repeater_track_sizes, NGGridTrackRepeater::RepeatType::kInteger,
            repetitions);
      }
      continue;
    }

    ConvertLineNameOrTrackSize(**curr_value);
    if (RuntimeEnabledFeatures::LayoutNGEnabled() &&
        !curr_value->Get()->IsGridLineNamesValue()) {
      track_sizes.NGTrackList().AddRepeater(
          {ConvertGridTrackSize(state, **curr_value)});
    }
  }

  // Unless the axis is subgridded, the parser should have rejected any
  // <track-list> without any <track-size> as this is not conformant to
  // the syntax.
  DCHECK(!track_sizes.LegacyTrackList().IsEmpty() ||
         !auto_repeat_track_sizes.IsEmpty() ||
         (computed_grid_track_list.axis_type == GridAxisType::kSubgriddedAxis));
}

void StyleBuilderConverter::CreateImplicitNamedGridLinesFromGridArea(
    const NamedGridAreaMap& named_grid_areas,
    NamedGridLinesMap& named_grid_lines,
    GridTrackSizingDirection direction) {
  for (const auto& named_grid_area_entry : named_grid_areas) {
    GridSpan area_span = direction == kForRows
                             ? named_grid_area_entry.value.rows
                             : named_grid_area_entry.value.columns;
    {
      NamedGridLinesMap::AddResult start_result = named_grid_lines.insert(
          named_grid_area_entry.key + "-start", Vector<wtf_size_t>());
      start_result.stored_value->value.push_back(area_span.StartLine());
      std::sort(start_result.stored_value->value.begin(),
                start_result.stored_value->value.end());
    }
    {
      NamedGridLinesMap::AddResult end_result = named_grid_lines.insert(
          named_grid_area_entry.key + "-end", Vector<wtf_size_t>());
      end_result.stored_value->value.push_back(area_span.EndLine());
      std::sort(end_result.stored_value->value.begin(),
                end_result.stored_value->value.end());
    }
  }
}

LayoutUnit StyleBuilderConverter::ConvertBorderWidth(StyleResolverState& state,
                                                     const CSSValue& value) {
  double result = 0;
  if (auto* identifier_value = DynamicTo<CSSIdentifierValue>(value)) {
    switch (identifier_value->GetValueID()) {
      case CSSValueID::kThin:
        result = 1;
        break;
      case CSSValueID::kMedium:
        result = 3;
        break;
      case CSSValueID::kThick:
        result = 5;
        break;
      default:
        NOTREACHED();
        break;
    }
    result = state.CssToLengthConversionData().ZoomedComputedPixels(
        result, CSSPrimitiveValue::UnitType::kPixels);
  } else {
    const auto& primitive_value = To<CSSPrimitiveValue>(value);
    result =
        primitive_value.ComputeLength<float>(state.CssToLengthConversionData());
  }
  if (result > 0.0 && result < 1.0)
    return LayoutUnit(1);
  return LayoutUnit(result);
}

LayoutUnit StyleBuilderConverter::ConvertLayoutUnit(StyleResolverState& state,
                                                    const CSSValue& value) {
  return LayoutUnit::Clamp(ConvertComputedLength<float>(state, value));
}

absl::optional<Length> StyleBuilderConverter::ConvertGapLength(
    const StyleResolverState& state,
    const CSSValue& value) {
  auto* identifier_value = DynamicTo<CSSIdentifierValue>(value);
  if (identifier_value && identifier_value->GetValueID() == CSSValueID::kNormal)
    return absl::nullopt;

  return ConvertLength(state, value);
}

Length StyleBuilderConverter::ConvertLength(const StyleResolverState& state,
                                            const CSSValue& value) {
  return To<CSSPrimitiveValue>(value).ConvertToLength(
      state.CssToLengthConversionData());
}

UnzoomedLength StyleBuilderConverter::ConvertUnzoomedLength(
    const StyleResolverState& state,
    const CSSValue& value) {
  return UnzoomedLength(To<CSSPrimitiveValue>(value).ConvertToLength(
      state.UnzoomedLengthConversionData()));
}

float StyleBuilderConverter::ConvertZoom(const StyleResolverState& state,
                                         const CSSValue& value) {
  SECURITY_DCHECK(value.IsPrimitiveValue() || value.IsIdentifierValue());

  if (const auto* identifier_value = DynamicTo<CSSIdentifierValue>(value)) {
    if (identifier_value->GetValueID() == CSSValueID::kNormal)
      return ComputedStyleInitialValues::InitialZoom();
  } else if (const auto* primitive_value =
                 DynamicTo<CSSPrimitiveValue>(value)) {
    if (primitive_value->IsPercentage()) {
      float percent = primitive_value->GetFloatValue();
      return percent ? (percent / 100.0f) : 1.0f;
    } else if (primitive_value->IsNumber()) {
      float number = primitive_value->GetFloatValue();
      return number ? number : 1.0f;
    }
  }

  NOTREACHED();
  return 1.0f;
}

Length StyleBuilderConverter::ConvertLengthOrAuto(
    const StyleResolverState& state,
    const CSSValue& value) {
  auto* identifier_value = DynamicTo<CSSIdentifierValue>(value);
  if (identifier_value && identifier_value->GetValueID() == CSSValueID::kAuto)
    return Length::Auto();
  return To<CSSPrimitiveValue>(value).ConvertToLength(
      state.CssToLengthConversionData());
}

Length StyleBuilderConverter::ConvertLengthSizing(StyleResolverState& state,
                                                  const CSSValue& value) {
  const auto* identifier_value = DynamicTo<CSSIdentifierValue>(value);
  if (!identifier_value)
    return ConvertLength(state, value);

  switch (identifier_value->GetValueID()) {
    case CSSValueID::kMinContent:
    case CSSValueID::kWebkitMinContent:
      return Length::MinContent();
    case CSSValueID::kMaxContent:
    case CSSValueID::kWebkitMaxContent:
      return Length::MaxContent();
    case CSSValueID::kWebkitFillAvailable:
      return Length::FillAvailable();
    case CSSValueID::kWebkitFitContent:
    case CSSValueID::kFitContent:
      return Length::FitContent();
    case CSSValueID::kContent:
      return Length::Content();
    case CSSValueID::kAuto:
      return Length::Auto();
    default:
      NOTREACHED();
      return Length();
  }
}

Length StyleBuilderConverter::ConvertLengthMaxSizing(StyleResolverState& state,
                                                     const CSSValue& value) {
  auto* identifier_value = DynamicTo<CSSIdentifierValue>(value);
  if (identifier_value && identifier_value->GetValueID() == CSSValueID::kNone)
    return Length::None();
  return ConvertLengthSizing(state, value);
}

TabSize StyleBuilderConverter::ConvertLengthOrTabSpaces(
    StyleResolverState& state,
    const CSSValue& value) {
  const auto& primitive_value = To<CSSPrimitiveValue>(value);
  if (primitive_value.IsNumber())
    return TabSize(primitive_value.GetFloatValue(), TabSizeValueType::kSpace);
  return TabSize(
      primitive_value.ComputeLength<float>(state.CssToLengthConversionData()),
      TabSizeValueType::kLength);
}

static CSSToLengthConversionData LineHeightToLengthConversionData(
    StyleResolverState& state) {
  float multiplier = state.Style()->EffectiveZoom();
  if (LocalFrame* frame = state.GetDocument().GetFrame())
    multiplier *= frame->TextZoomFactor();
  return state.CssToLengthConversionData().CopyWithAdjustedZoom(multiplier);
}

Length StyleBuilderConverter::ConvertLineHeight(StyleResolverState& state,
                                                const CSSValue& value) {
  if (const auto* primitive_value = DynamicTo<CSSPrimitiveValue>(value)) {
    if (primitive_value->IsLength()) {
      return primitive_value->ComputeLength<Length>(
          LineHeightToLengthConversionData(state));
    }
    if (primitive_value->IsPercentage()) {
      return Length::Fixed(
          (state.Style()->ComputedFontSize() * primitive_value->GetIntValue()) /
          100.0);
    }
    if (primitive_value->IsNumber()) {
      return Length::Percent(
          ClampTo<float>(primitive_value->GetDoubleValue() * 100.0));
    }
    if (primitive_value->IsCalculated()) {
      Length zoomed_length =
          Length(To<CSSMathFunctionValue>(primitive_value)
                     ->ToCalcValue(LineHeightToLengthConversionData(state)));
      return Length::Fixed(ValueForLength(
          zoomed_length, LayoutUnit(state.Style()->ComputedFontSize())));
    }
  }

  if (value.IsPendingSystemFontValue())
    return ComputedStyleInitialValues::InitialLineHeight();

  DCHECK_EQ(To<CSSIdentifierValue>(value).GetValueID(), CSSValueID::kNormal);
  return ComputedStyleInitialValues::InitialLineHeight();
}

float StyleBuilderConverter::ConvertNumberOrPercentage(
    StyleResolverState& state,
    const CSSValue& value) {
  const auto& primitive_value = To<CSSPrimitiveValue>(value);
  DCHECK(primitive_value.IsNumber() || primitive_value.IsPercentage());
  if (primitive_value.IsNumber())
    return primitive_value.GetFloatValue();
  return primitive_value.GetFloatValue() / 100.0f;
}

float StyleBuilderConverter::ConvertAlpha(StyleResolverState& state,
                                          const CSSValue& value) {
  return ClampTo<float>(ConvertNumberOrPercentage(state, value), 0, 1);
}

AtomicString StyleBuilderConverter::ConvertNoneOrCustomIdent(
    StyleResolverState& state,
    const CSSValue& value) {
  if (const auto* identifier_value = DynamicTo<CSSIdentifierValue>(value)) {
    DCHECK_EQ(identifier_value->GetValueID(), CSSValueID::kNone);
    return g_null_atom;
  }
  return To<CSSCustomIdentValue>(value).Value();
}

StyleOffsetRotation StyleBuilderConverter::ConvertOffsetRotate(
    StyleResolverState&,
    const CSSValue& value) {
  return ConvertOffsetRotate(value);
}

StyleOffsetRotation StyleBuilderConverter::ConvertOffsetRotate(
    const CSSValue& value) {
  StyleOffsetRotation result(0, OffsetRotationType::kFixed);

  const auto& list = To<CSSValueList>(value);
  DCHECK(list.length() == 1 || list.length() == 2);
  for (const auto& item : list) {
    auto* identifier_value = DynamicTo<CSSIdentifierValue>(item.Get());
    if (identifier_value &&
        identifier_value->GetValueID() == CSSValueID::kAuto) {
      result.type = OffsetRotationType::kAuto;
    } else if (identifier_value &&
               identifier_value->GetValueID() == CSSValueID::kReverse) {
      result.type = OffsetRotationType::kAuto;
      result.angle = ClampTo<float>(result.angle + 180);
    } else {
      const auto& primitive_value = To<CSSPrimitiveValue>(*item);
      result.angle =
          ClampTo<float>(result.angle + primitive_value.ComputeDegrees());
    }
  }

  return result;
}

LengthPoint StyleBuilderConverter::ConvertPosition(StyleResolverState& state,
                                                   const CSSValue& value) {
  const auto& pair = To<CSSValuePair>(value);
  return LengthPoint(
      ConvertPositionLength<CSSValueID::kLeft, CSSValueID::kRight>(
          state, pair.First()),
      ConvertPositionLength<CSSValueID::kTop, CSSValueID::kBottom>(
          state, pair.Second()));
}

LengthPoint StyleBuilderConverter::ConvertPositionOrAuto(
    StyleResolverState& state,
    const CSSValue& value) {
  if (value.IsValuePair())
    return ConvertPosition(state, value);
  DCHECK(To<CSSIdentifierValue>(value).GetValueID() == CSSValueID::kAuto);
  return LengthPoint(Length::Auto(), Length::Auto());
}

static float ConvertPerspectiveLength(
    StyleResolverState& state,
    const CSSPrimitiveValue& primitive_value) {
  return std::max(
      primitive_value.ComputeLength<float>(state.CssToLengthConversionData()),
      0.0f);
}

float StyleBuilderConverter::ConvertPerspective(StyleResolverState& state,
                                                const CSSValue& value) {
  auto* identifier_value = DynamicTo<CSSIdentifierValue>(value);
  if (identifier_value && identifier_value->GetValueID() == CSSValueID::kNone)
    return ComputedStyleInitialValues::InitialPerspective();
  return ConvertPerspectiveLength(state, To<CSSPrimitiveValue>(value));
}

EPaintOrder StyleBuilderConverter::ConvertPaintOrder(
    StyleResolverState&,
    const CSSValue& css_paint_order) {
  if (const auto* order_type_list = DynamicTo<CSSValueList>(css_paint_order)) {
    switch (To<CSSIdentifierValue>(order_type_list->Item(0)).GetValueID()) {
      case CSSValueID::kFill:
        return order_type_list->length() > 1 ? kPaintOrderFillMarkersStroke
                                             : kPaintOrderFillStrokeMarkers;
      case CSSValueID::kStroke:
        return order_type_list->length() > 1 ? kPaintOrderStrokeMarkersFill
                                             : kPaintOrderStrokeFillMarkers;
      case CSSValueID::kMarkers:
        return order_type_list->length() > 1 ? kPaintOrderMarkersStrokeFill
                                             : kPaintOrderMarkersFillStroke;
      default:
        NOTREACHED();
        return kPaintOrderNormal;
    }
  }

  return kPaintOrderNormal;
}

Length StyleBuilderConverter::ConvertQuirkyLength(StyleResolverState& state,
                                                  const CSSValue& value) {
  Length length = ConvertLengthOrAuto(state, value);
  // This is only for margins which use __qem
  auto* numeric_literal = DynamicTo<CSSNumericLiteralValue>(value);
  length.SetQuirk(numeric_literal && numeric_literal->IsQuirkyEms());
  return length;
}

scoped_refptr<QuotesData> StyleBuilderConverter::ConvertQuotes(
    StyleResolverState&,
    const CSSValue& value) {
  if (const auto* list = DynamicTo<CSSValueList>(value)) {
    scoped_refptr<QuotesData> quotes = QuotesData::Create();
    for (wtf_size_t i = 0; i < list->length(); i += 2) {
      String start_quote = To<CSSStringValue>(list->Item(i)).Value();
      String end_quote = To<CSSStringValue>(list->Item(i + 1)).Value();
      quotes->AddPair(std::make_pair(start_quote, end_quote));
    }
    return quotes;
  }
  if (To<CSSIdentifierValue>(value).GetValueID() == CSSValueID::kNone)
    return QuotesData::Create();
  DCHECK_EQ(To<CSSIdentifierValue>(value).GetValueID(), CSSValueID::kAuto);
  return nullptr;
}

LengthSize StyleBuilderConverter::ConvertRadius(StyleResolverState& state,
                                                const CSSValue& value) {
  const auto& pair = To<CSSValuePair>(value);
  Length radius_width = To<CSSPrimitiveValue>(pair.First())
                            .ConvertToLength(state.CssToLengthConversionData());
  Length radius_height =
      To<CSSPrimitiveValue>(pair.Second())
          .ConvertToLength(state.CssToLengthConversionData());
  return LengthSize(radius_width, radius_height);
}

ShadowData StyleBuilderConverter::ConvertShadow(
    const CSSToLengthConversionData& conversion_data,
    StyleResolverState* state,
    const CSSValue& value) {
  const auto& shadow = To<CSSShadowValue>(value);
  float x = shadow.x->ComputeLength<float>(conversion_data);
  float y = shadow.y->ComputeLength<float>(conversion_data);
  float blur =
      shadow.blur ? shadow.blur->ComputeLength<float>(conversion_data) : 0;
  float spread =
      shadow.spread ? shadow.spread->ComputeLength<float>(conversion_data) : 0;
  ShadowStyle shadow_style =
      shadow.style && shadow.style->GetValueID() == CSSValueID::kInset
          ? ShadowStyle::kInset
          : ShadowStyle::kNormal;
  StyleColor color = StyleColor::CurrentColor();
  if (shadow.color) {
    if (state) {
      color = ConvertStyleColor(*state, *shadow.color);
    } else {
      // For OffScreen canvas, we default to black and only parse non
      // Document dependent CSS colors.
      color = StyleColor(Color::kBlack);
      if (auto* color_value =
              DynamicTo<cssvalue::CSSColor>(shadow.color.Get())) {
        color = StyleColor(color_value->Value());
      } else {
        CSSValueID value_id =
            To<CSSIdentifierValue>(*shadow.color).GetValueID();
        switch (value_id) {
          case CSSValueID::kInvalid:
            NOTREACHED();
            [[fallthrough]];
          case CSSValueID::kInternalQuirkInherit:
          case CSSValueID::kWebkitLink:
          case CSSValueID::kWebkitActivelink:
          case CSSValueID::kWebkitFocusRingColor:
          case CSSValueID::kCurrentcolor:
            break;
          default:
            color = StyleColor(StyleColor::ColorFromKeyword(
                value_id, mojom::blink::ColorScheme::kLight));
        }
      }
    }
  }

  return ShadowData(gfx::PointF(x, y), blur, spread, shadow_style, color);
}

scoped_refptr<ShadowList> StyleBuilderConverter::ConvertShadowList(
    StyleResolverState& state,
    const CSSValue& value) {
  if (auto* identifier_value = DynamicTo<CSSIdentifierValue>(value)) {
    DCHECK_EQ(identifier_value->GetValueID(), CSSValueID::kNone);
    return scoped_refptr<ShadowList>();
  }

  ShadowDataVector shadows;
  for (const auto& item : To<CSSValueList>(value)) {
    shadows.push_back(
        ConvertShadow(state.CssToLengthConversionData(), &state, *item));
  }

  return ShadowList::Adopt(shadows);
}

ShapeValue* StyleBuilderConverter::ConvertShapeValue(StyleResolverState& state,
                                                     const CSSValue& value) {
  if (auto* identifier_value = DynamicTo<CSSIdentifierValue>(value)) {
    DCHECK_EQ(identifier_value->GetValueID(), CSSValueID::kNone);
    return nullptr;
  }

  if (value.IsImageValue() || value.IsImageGeneratorValue() ||
      value.IsImageSetValue()) {
    return MakeGarbageCollected<ShapeValue>(
        state.GetStyleImage(CSSPropertyID::kShapeOutside, value));
  }

  scoped_refptr<BasicShape> shape;
  CSSBoxType css_box = CSSBoxType::kMissing;
  const auto& value_list = To<CSSValueList>(value);
  for (unsigned i = 0; i < value_list.length(); ++i) {
    const CSSValue& item_value = value_list.Item(i);
    if (item_value.IsBasicShapeValue()) {
      shape = BasicShapeForValue(state, item_value);
    } else {
      css_box = To<CSSIdentifierValue>(item_value).ConvertTo<CSSBoxType>();
    }
  }

  if (shape)
    return MakeGarbageCollected<ShapeValue>(std::move(shape), css_box);

  DCHECK_NE(css_box, CSSBoxType::kMissing);
  return MakeGarbageCollected<ShapeValue>(css_box);
}

float StyleBuilderConverter::ConvertSpacing(StyleResolverState& state,
                                            const CSSValue& value) {
  auto* identifier_value = DynamicTo<CSSIdentifierValue>(value);
  if (identifier_value && identifier_value->GetValueID() == CSSValueID::kNormal)
    return 0;
  return To<CSSPrimitiveValue>(value).ComputeLength<float>(
      state.CssToLengthConversionData());
}

scoped_refptr<SVGDashArray> StyleBuilderConverter::ConvertStrokeDasharray(
    StyleResolverState& state,
    const CSSValue& value) {
  const auto* dashes = DynamicTo<CSSValueList>(value);
  if (!dashes)
    return EmptyDashArray();

  scoped_refptr<SVGDashArray> array = base::MakeRefCounted<SVGDashArray>();

  wtf_size_t length = dashes->length();
  for (wtf_size_t i = 0; i < length; ++i) {
    array->data.push_back(
        ConvertLength(state, To<CSSPrimitiveValue>(dashes->Item(i))));
  }

  return array;
}

AtomicString StyleBuilderConverter::ConvertPageTransitionTag(
    StyleResolverState& state,
    const CSSValue& value) {
  if (auto* custom_ident_value = DynamicTo<CSSCustomIdentValue>(value))
    return AtomicString(custom_ident_value->Value());
  DCHECK(DynamicTo<CSSIdentifierValue>(value));
  DCHECK_EQ(DynamicTo<CSSIdentifierValue>(value)->GetValueID(),
            CSSValueID::kNone);
  return AtomicString();
}

StyleColor StyleBuilderConverter::ConvertStyleColor(StyleResolverState& state,
                                                    const CSSValue& value,
                                                    bool for_visited_link) {
  auto* identifier_value = DynamicTo<CSSIdentifierValue>(value);
  if (identifier_value) {
    CSSValueID value_id = identifier_value->GetValueID();
    if (value_id == CSSValueID::kCurrentcolor)
      return StyleColor::CurrentColor();
    if (StyleColor::IsSystemColorIncludingDeprecated(value_id)) {
      return StyleColor(
          state.GetDocument().GetTextLinkColors().ColorFromCSSValue(
              value, Color(), state.Style()->UsedColorScheme(),
              for_visited_link),
          value_id);
    }
  }
  return StyleColor(state.GetDocument().GetTextLinkColors().ColorFromCSSValue(
      value, Color(), state.Style()->UsedColorScheme(), for_visited_link));
}

StyleAutoColor StyleBuilderConverter::ConvertStyleAutoColor(
    StyleResolverState& state,
    const CSSValue& value,
    bool for_visited_link) {
  if (auto* identifier_value = DynamicTo<CSSIdentifierValue>(value)) {
    CSSValueID value_id = identifier_value->GetValueID();
    if (value_id == CSSValueID::kCurrentcolor)
      return StyleAutoColor::CurrentColor();
    if (value_id == CSSValueID::kAuto)
      return StyleAutoColor::AutoColor();
    if (StyleColor::IsSystemColorIncludingDeprecated(value_id)) {
      return StyleAutoColor(
          state.GetDocument().GetTextLinkColors().ColorFromCSSValue(
              value, Color(), state.Style()->UsedColorScheme(),
              for_visited_link),
          value_id);
    }
  }
  return StyleAutoColor(
      state.GetDocument().GetTextLinkColors().ColorFromCSSValue(
          value, Color(), state.Style()->UsedColorScheme(), for_visited_link));
}

SVGPaint StyleBuilderConverter::ConvertSVGPaint(StyleResolverState& state,
                                                const CSSValue& value,
                                                bool for_visited_link) {
  const CSSValue* local_value = &value;
  SVGPaint paint;
  if (const auto* list = DynamicTo<CSSValueList>(value)) {
    DCHECK_EQ(list->length(), 2u);
    paint.resource = ConvertElementReference(state, list->Item(0));
    local_value = &list->Item(1);
  }

  if (local_value->IsURIValue()) {
    paint.type = SVGPaintType::kUri;
    paint.resource = ConvertElementReference(state, *local_value);
  } else {
    auto* local_identifier_value = DynamicTo<CSSIdentifierValue>(local_value);
    if (local_identifier_value &&
        local_identifier_value->GetValueID() == CSSValueID::kNone) {
      paint.type =
          !paint.resource ? SVGPaintType::kNone : SVGPaintType::kUriNone;
    } else {
      // TODO(fs): Pass along |for_visited_link|.
      paint.color = ConvertStyleColor(state, *local_value);
      paint.type =
          !paint.resource ? SVGPaintType::kColor : SVGPaintType::kUriColor;
    }
  }
  return paint;
}

TextDecorationThickness StyleBuilderConverter::ConvertTextDecorationThickness(
    StyleResolverState& state,
    const CSSValue& value) {
  auto* identifier_value = DynamicTo<CSSIdentifierValue>(value);
  if (identifier_value &&
      identifier_value->GetValueID() == CSSValueID::kFromFont)
    return TextDecorationThickness(identifier_value->GetValueID());

  return TextDecorationThickness(ConvertLengthOrAuto(state, value));
}

TextEmphasisPosition StyleBuilderConverter::ConvertTextTextEmphasisPosition(
    StyleResolverState& state,
    const CSSValue& value) {
  const auto& list = To<CSSValueList>(value);
  CSSValueID first = To<CSSIdentifierValue>(list.Item(0)).GetValueID();
  if (list.length() < 2) {
    if (first == CSSValueID::kOver)
      return TextEmphasisPosition::kOverRight;
    if (first == CSSValueID::kUnder)
      return TextEmphasisPosition::kUnderRight;
    return TextEmphasisPosition::kOverRight;
  }
  CSSValueID second = To<CSSIdentifierValue>(list.Item(1)).GetValueID();
  if (first == CSSValueID::kOver && second == CSSValueID::kRight)
    return TextEmphasisPosition::kOverRight;
  if (first == CSSValueID::kOver && second == CSSValueID::kLeft)
    return TextEmphasisPosition::kOverLeft;
  if (first == CSSValueID::kUnder && second == CSSValueID::kRight)
    return TextEmphasisPosition::kUnderRight;
  if (first == CSSValueID::kUnder && second == CSSValueID::kLeft)
    return TextEmphasisPosition::kUnderLeft;
  return TextEmphasisPosition::kOverRight;
}

float StyleBuilderConverter::ConvertTextStrokeWidth(StyleResolverState& state,
                                                    const CSSValue& value) {
  auto* identifier_value = DynamicTo<CSSIdentifierValue>(value);
  if (identifier_value && IsValidCSSValueID(identifier_value->GetValueID())) {
    float multiplier = ConvertLineWidth<float>(state, value);
    return CSSNumericLiteralValue::Create(multiplier / 48,
                                          CSSPrimitiveValue::UnitType::kEms)
        ->ComputeLength<float>(state.CssToLengthConversionData());
  }
  return To<CSSPrimitiveValue>(value).ComputeLength<float>(
      state.CssToLengthConversionData());
}

TextSizeAdjust StyleBuilderConverter::ConvertTextSizeAdjust(
    StyleResolverState& state,
    const CSSValue& value) {
  if (auto* identifier_value = DynamicTo<CSSIdentifierValue>(value)) {
    if (identifier_value->GetValueID() == CSSValueID::kNone)
      return TextSizeAdjust::AdjustNone();
    if (identifier_value->GetValueID() == CSSValueID::kAuto)
      return TextSizeAdjust::AdjustAuto();
  }
  const CSSPrimitiveValue& primitive_value = To<CSSPrimitiveValue>(value);
  DCHECK(primitive_value.IsPercentage());
  return TextSizeAdjust(primitive_value.GetFloatValue() / 100.0f);
}

TextUnderlinePosition StyleBuilderConverter::ConvertTextUnderlinePosition(
    StyleResolverState& state,
    const CSSValue& value) {
  TextUnderlinePosition flags = kTextUnderlinePositionAuto;

  auto process = [&flags](const CSSValue& identifier) {
    flags |=
        To<CSSIdentifierValue>(identifier).ConvertTo<TextUnderlinePosition>();
  };

  if (auto* value_list = DynamicTo<CSSValueList>(value)) {
    for (auto& entry : *value_list) {
      process(*entry);
    }
  } else {
    process(value);
  }
  return flags;
}

Length StyleBuilderConverter::ConvertTextUnderlineOffset(
    StyleResolverState& state,
    const CSSValue& value) {
  return ConvertLengthOrAuto(state, value);
}

TransformOperations StyleBuilderConverter::ConvertTransformOperations(
    StyleResolverState& state,
    const CSSValue& value) {
  return TransformBuilder::CreateTransformOperations(
      value, state.CssToLengthConversionData());
}

TransformOrigin StyleBuilderConverter::ConvertTransformOrigin(
    StyleResolverState& state,
    const CSSValue& value) {
  const auto& list = To<CSSValueList>(value);
  DCHECK_GE(list.length(), 2u);
  DCHECK(list.Item(0).IsPrimitiveValue() || list.Item(0).IsIdentifierValue());
  DCHECK(list.Item(1).IsPrimitiveValue() || list.Item(1).IsIdentifierValue());
  float z = 0;
  if (list.length() == 3) {
    DCHECK(list.Item(2).IsPrimitiveValue());
    z = StyleBuilderConverter::ConvertComputedLength<float>(state,
                                                            list.Item(2));
  }

  return TransformOrigin(
      ConvertPositionLength<CSSValueID::kLeft, CSSValueID::kRight>(
          state, list.Item(0)),
      ConvertPositionLength<CSSValueID::kTop, CSSValueID::kBottom>(
          state, list.Item(1)),
      z);
}

cc::ScrollSnapType StyleBuilderConverter::ConvertSnapType(
    StyleResolverState&,
    const CSSValue& value) {
  cc::ScrollSnapType snapType =
      ComputedStyleInitialValues::InitialScrollSnapType();
  if (const auto* pair = DynamicTo<CSSValuePair>(value)) {
    snapType.is_none = false;
    snapType.axis =
        To<CSSIdentifierValue>(pair->First()).ConvertTo<cc::SnapAxis>();
    snapType.strictness =
        To<CSSIdentifierValue>(pair->Second()).ConvertTo<cc::SnapStrictness>();
    return snapType;
  }

  if (To<CSSIdentifierValue>(value).GetValueID() == CSSValueID::kNone) {
    snapType.is_none = true;
    return snapType;
  }

  snapType.is_none = false;
  snapType.axis = To<CSSIdentifierValue>(value).ConvertTo<cc::SnapAxis>();
  return snapType;
}

cc::ScrollSnapAlign StyleBuilderConverter::ConvertSnapAlign(
    StyleResolverState&,
    const CSSValue& value) {
  cc::ScrollSnapAlign snapAlign =
      ComputedStyleInitialValues::InitialScrollSnapAlign();
  if (const auto* pair = DynamicTo<CSSValuePair>(value)) {
    snapAlign.alignment_block =
        To<CSSIdentifierValue>(pair->First()).ConvertTo<cc::SnapAlignment>();
    snapAlign.alignment_inline =
        To<CSSIdentifierValue>(pair->Second()).ConvertTo<cc::SnapAlignment>();
  } else {
    snapAlign.alignment_block =
        To<CSSIdentifierValue>(value).ConvertTo<cc::SnapAlignment>();
    snapAlign.alignment_inline = snapAlign.alignment_block;
  }
  return snapAlign;
}

scoped_refptr<TranslateTransformOperation>
StyleBuilderConverter::ConvertTranslate(StyleResolverState& state,
                                        const CSSValue& value) {
  if (auto* identifier_value = DynamicTo<CSSIdentifierValue>(value)) {
    DCHECK_EQ(identifier_value->GetValueID(), CSSValueID::kNone);
    return nullptr;
  }
  const auto& list = To<CSSValueList>(value);
  DCHECK_LE(list.length(), 3u);
  Length tx = ConvertLength(state, list.Item(0));
  Length ty = Length::Fixed(0);
  double tz = 0;
  if (list.length() >= 2)
    ty = ConvertLength(state, list.Item(1));
  if (list.length() == 3)
    tz = To<CSSPrimitiveValue>(list.Item(2))
             .ComputeLength<double>(state.CssToLengthConversionData());

  return TranslateTransformOperation::Create(tx, ty, tz,
                                             TransformOperation::kTranslate3D);
}

Rotation StyleBuilderConverter::ConvertRotation(const CSSValue& value) {
  if (auto* identifier_value = DynamicTo<CSSIdentifierValue>(value)) {
    DCHECK_EQ(identifier_value->GetValueID(), CSSValueID::kNone);
    return Rotation(gfx::Vector3dF(0, 0, 1), 0);
  }

  const auto& list = To<CSSValueList>(value);
  DCHECK(list.length() == 1 || list.length() == 2);
  double x = 0;
  double y = 0;
  double z = 1;
  if (list.length() == 2) {
    // axis angle
    const cssvalue::CSSAxisValue& axis =
        To<cssvalue::CSSAxisValue>(list.Item(0));
    x = axis.X();
    y = axis.Y();
    z = axis.Z();
  }
  double angle =
      To<CSSPrimitiveValue>(list.Item(list.length() - 1)).ComputeDegrees();
  return Rotation(gfx::Vector3dF(x, y, z), angle);
}

scoped_refptr<RotateTransformOperation> StyleBuilderConverter::ConvertRotate(
    StyleResolverState& state,
    const CSSValue& value) {
  if (auto* identifier_value = DynamicTo<CSSIdentifierValue>(value)) {
    DCHECK_EQ(identifier_value->GetValueID(), CSSValueID::kNone);
    return nullptr;
  }

  return RotateTransformOperation::Create(ConvertRotation(value),
                                          TransformOperation::kRotate3D);
}

scoped_refptr<ScaleTransformOperation> StyleBuilderConverter::ConvertScale(
    StyleResolverState& state,
    const CSSValue& value) {
  if (auto* identifier_value = DynamicTo<CSSIdentifierValue>(value)) {
    DCHECK_EQ(identifier_value->GetValueID(), CSSValueID::kNone);
    return nullptr;
  }

  const auto& list = To<CSSValueList>(value);
  DCHECK_LE(list.length(), 3u);
  double sx = To<CSSPrimitiveValue>(list.Item(0)).GetDoubleValue();
  double sy = sx;
  double sz = 1;
  if (list.length() >= 2)
    sy = To<CSSPrimitiveValue>(list.Item(1)).GetDoubleValue();
  if (list.length() == 3)
    sz = To<CSSPrimitiveValue>(list.Item(2)).GetDoubleValue();

  return ScaleTransformOperation::Create(sx, sy, sz,
                                         TransformOperation::kScale3D);
}

RespectImageOrientationEnum StyleBuilderConverter::ConvertImageOrientation(
    StyleResolverState& state,
    const CSSValue& value) {
  // The default is kFromImage, so branch on the only other valid value, kNone
  auto* identifier_value = DynamicTo<CSSIdentifierValue>(value);
  return identifier_value && identifier_value->GetValueID() == CSSValueID::kNone
             ? kDoNotRespectImageOrientation
             : kRespectImageOrientation;
}

scoped_refptr<StylePath> StyleBuilderConverter::ConvertPathOrNone(
    StyleResolverState& state,
    const CSSValue& value) {
  if (auto* path_value = DynamicTo<cssvalue::CSSPathValue>(value))
    return path_value->GetStylePath();
  DCHECK_EQ(To<CSSIdentifierValue>(value).GetValueID(), CSSValueID::kNone);
  return nullptr;
}

scoped_refptr<BasicShape> StyleBuilderConverter::ConvertOffsetPath(
    StyleResolverState& state,
    const CSSValue& value) {
  if (value.IsRayValue())
    return BasicShapeForValue(state, value);
  return ConvertPathOrNone(state, value);
}

scoped_refptr<BasicShape> StyleBuilderConverter::ConvertObjectViewBox(
    StyleResolverState& state,
    const CSSValue& value) {
  if (!value.IsBasicShapeInsetValue() && !value.IsBasicShapeRectValue() &&
      !value.IsBasicShapeXYWHValue())
    return nullptr;
  return BasicShapeForValue(state, value);
}

static const CSSValue& ComputeRegisteredPropertyValue(
    const Document& document,
    const StyleResolverState* state,
    const CSSToLengthConversionData& css_to_length_conversion_data,
    const CSSValue& value,
    const String& base_url,
    const WTF::TextEncoding& charset) {
  // TODO(timloh): Images values can also contain lengths.
  if (const auto* function_value = DynamicTo<CSSFunctionValue>(value)) {
    CSSFunctionValue* new_function =
        MakeGarbageCollected<CSSFunctionValue>(function_value->FunctionType());
    for (const CSSValue* inner_value : To<CSSValueList>(value)) {
      new_function->Append(ComputeRegisteredPropertyValue(
          document, state, css_to_length_conversion_data, *inner_value,
          base_url, charset));
    }
    return *new_function;
  }

  if (const auto* old_list = DynamicTo<CSSValueList>(value)) {
    CSSValueList* new_list = CSSValueList::CreateWithSeparatorFrom(*old_list);
    for (const CSSValue* inner_value : *old_list) {
      new_list->Append(ComputeRegisteredPropertyValue(
          document, state, css_to_length_conversion_data, *inner_value,
          base_url, charset));
    }
    return *new_list;
  }

  if (const auto* primitive_value = DynamicTo<CSSPrimitiveValue>(value)) {
    // For simple (non-calculated) px or percentage values, we do not need to
    // convert, as the value already has the proper computed form.
    if (!primitive_value->IsCalculated() &&
        (primitive_value->IsPx() || primitive_value->IsPercentage())) {
      return value;
    }

    if (primitive_value->IsLength() || primitive_value->IsPercentage() ||
        primitive_value->IsCalculatedPercentageWithLength()) {
      // Instead of the actual zoom, use 1 to avoid potential rounding errors
      Length length = primitive_value->ConvertToLength(
          css_to_length_conversion_data.Unzoomed());
      return *CSSPrimitiveValue::CreateFromLength(length, 1);
    }

    // If we encounter a calculated number that was not resolved during
    // parsing, it means that a calc()-expression was allowed in place of
    // an integer. Such calc()-for-integers must be rounded at computed value
    // time.
    // https://drafts.csswg.org/css-values-4/#calc-type-checking
    if (primitive_value->IsCalculated()) {
      const CSSMathFunctionValue& math_value =
          To<CSSMathFunctionValue>(*primitive_value);
      if (math_value.IsNumber()) {
        double double_value = math_value.GetDoubleValue();
        auto unit_type = CSSPrimitiveValue::UnitType::kInteger;
        return *CSSNumericLiteralValue::Create(std::round(double_value),
                                               unit_type);
      }
    }

    if (primitive_value->IsAngle()) {
      return *CSSNumericLiteralValue::Create(
          primitive_value->ComputeDegrees(),
          CSSPrimitiveValue::UnitType::kDegrees);
    }

    if (primitive_value->IsTime()) {
      return *CSSNumericLiteralValue::Create(
          primitive_value->ComputeSeconds(),
          CSSPrimitiveValue::UnitType::kSeconds);
    }

    if (primitive_value->IsResolution()) {
      return *CSSNumericLiteralValue::Create(
          primitive_value->ComputeDotsPerPixel(),
          CSSPrimitiveValue::UnitType::kDotsPerPixel);
    }
  }

  if (auto* identifier_value = DynamicTo<CSSIdentifierValue>(value)) {
    CSSValueID value_id = identifier_value->GetValueID();
    if (value_id == CSSValueID::kCurrentcolor)
      return value;
    if (StyleColor::IsColorKeyword(value_id)) {
      mojom::blink::ColorScheme scheme =
          state ? state->Style()->UsedColorScheme()
                : mojom::blink::ColorScheme::kLight;
      Color color = document.GetTextLinkColors().ColorFromCSSValue(
          value, Color(), scheme, false);
      return *cssvalue::CSSColor::Create(color);
    }
  }

  if (const auto* uri_value = DynamicTo<cssvalue::CSSURIValue>(value))
    return *uri_value->ValueWithURLMadeAbsolute(KURL(base_url), charset);

  return value;
}

const CSSValue& StyleBuilderConverter::ConvertRegisteredPropertyInitialValue(
    const Document& document,
    const CSSValue& value) {
  CSSToLengthConversionData::FontSizes font_sizes;
  CSSToLengthConversionData::ViewportSize viewport_size(
      document.GetLayoutView());
  CSSToLengthConversionData::ContainerSizes container_sizes;
  CSSToLengthConversionData conversion_data(
      /* style */ nullptr, WritingMode::kHorizontalTb, font_sizes,
      viewport_size, container_sizes,
      /* zoom */ 1.0f);

  return ComputeRegisteredPropertyValue(
      document, nullptr /* state */, conversion_data, value, document.BaseURL(),
      document.Encoding());
}

const CSSValue& StyleBuilderConverter::ConvertRegisteredPropertyValue(
    const StyleResolverState& state,
    const CSSValue& value,
    const String& base_url,
    const WTF::TextEncoding& charset) {
  return ComputeRegisteredPropertyValue(state.GetDocument(), &state,
                                        state.CssToLengthConversionData(),
                                        value, base_url, charset);
}

// Registered properties need to substitute as absolute values. This means
// that 'em' units (for instance) are converted to 'px ' and calc()-expressions
// are resolved. This function creates new tokens equivalent to the computed
// value of the registered property.
//
// This is necessary to make things like font-relative units in inherited
// (and registered) custom properties work correctly.
//
// https://drafts.css-houdini.org/css-properties-values-api-1/#substitution
scoped_refptr<CSSVariableData>
StyleBuilderConverter::ConvertRegisteredPropertyVariableData(
    const CSSValue& value,
    bool is_animation_tainted) {
  // TODO(andruud): Produce tokens directly from CSSValue.
  String text = value.CssText();

  CSSTokenizer tokenizer(text);
  Vector<CSSParserToken> tokens;
  tokens.AppendVector(tokenizer.TokenizeToEOF());

  Vector<String> backing_strings;
  backing_strings.push_back(text);
  // CSSTokenizer may allocate new strings for some tokens (e.g. for escapes)
  // and produce tokens that point to those strings. We need to retain those
  // strings (if any) as well.
  backing_strings.AppendVector(tokenizer.StringPool());

  const bool has_font_units = false;
  const bool has_root_font_units = false;

  return CSSVariableData::CreateResolved(
      std::move(tokens), std::move(backing_strings), is_animation_tainted,
      has_font_units, has_root_font_units, g_null_atom, WTF::TextEncoding());
}

namespace {
gfx::SizeF GetRatioFromList(const CSSValueList& list) {
  auto* ratio = DynamicTo<cssvalue::CSSRatioValue>(list.Item(0));
  if (!ratio) {
    DCHECK_EQ(list.length(), 2u);
    ratio = DynamicTo<cssvalue::CSSRatioValue>(list.Item(1));
  }
  DCHECK(ratio);
  return gfx::SizeF(ratio->First().GetFloatValue(),
                    ratio->Second().GetFloatValue());
}

bool ListHasAuto(const CSSValueList& list) {
  // If there's only one entry, it needs to be a ratio.
  // (A single auto is handled separately)
  if (list.length() == 1u)
    return false;
  auto* auto_value = DynamicTo<CSSIdentifierValue>(list.Item(0));
  if (!auto_value)
    auto_value = DynamicTo<CSSIdentifierValue>(list.Item(1));
  DCHECK(auto_value) << "If we have two items, one of them must be auto";
  DCHECK_EQ(auto_value->GetValueID(), CSSValueID::kAuto);
  return true;
}
}  // namespace

StyleAspectRatio StyleBuilderConverter::ConvertAspectRatio(
    const StyleResolverState& state,
    const CSSValue& value) {
  auto* identifier_value = DynamicTo<CSSIdentifierValue>(value);
  if (identifier_value && identifier_value->GetValueID() == CSSValueID::kAuto)
    return StyleAspectRatio(EAspectRatioType::kAuto, gfx::SizeF());

  // (auto, (1, 2)) or ((1, 2), auto) or ((1, 2))
  const CSSValueList& list = To<CSSValueList>(value);
  DCHECK_GE(list.length(), 1u);
  DCHECK_LE(list.length(), 2u);

  bool has_auto = ListHasAuto(list);
  EAspectRatioType type =
      has_auto ? EAspectRatioType::kAutoAndRatio : EAspectRatioType::kRatio;
  gfx::SizeF ratio = GetRatioFromList(list);
  return StyleAspectRatio(type, ratio);
}

bool StyleBuilderConverter::ConvertInternalAlignSelfBlock(
    StyleResolverState&,
    const CSSValue& value) {
  auto* identifier_value = DynamicTo<CSSIdentifierValue>(value);
  return identifier_value &&
         identifier_value->GetValueID() == CSSValueID::kCenter;
}

bool StyleBuilderConverter::ConvertInternalEmptyLineHeight(
    StyleResolverState&,
    const CSSValue& value) {
  auto* identifier_value = DynamicTo<CSSIdentifierValue>(value);
  return identifier_value &&
         identifier_value->GetValueID() == CSSValueID::kFabricated;
}

AtomicString StyleBuilderConverter::ConvertPage(StyleResolverState& state,
                                                const CSSValue& value) {
  if (auto* custom_ident_value = DynamicTo<CSSCustomIdentValue>(value))
    return AtomicString(custom_ident_value->Value());
  DCHECK(DynamicTo<CSSIdentifierValue>(value));
  DCHECK_EQ(DynamicTo<CSSIdentifierValue>(value)->GetValueID(),
            CSSValueID::kAuto);
  return AtomicString();
}

RubyPosition StyleBuilderConverter::ConvertRubyPosition(
    StyleResolverState& state,
    const CSSValue& value) {
  if (const auto* identifier_value = DynamicTo<CSSIdentifierValue>(value)) {
    CSSValueID value_id = identifier_value->GetValueID();
    if (value_id == CSSValueID::kOver)
      return RubyPosition::kBefore;
    if (value_id == CSSValueID::kUnder)
      return RubyPosition::kAfter;
    return identifier_value->ConvertTo<blink::RubyPosition>();
  }
  NOTREACHED();
  return RubyPosition::kBefore;
}

ScrollbarGutter StyleBuilderConverter::ConvertScrollbarGutter(
    StyleResolverState& state,
    const CSSValue& value) {
  ScrollbarGutter flags = kScrollbarGutterAuto;

  auto process = [&flags](const CSSValue& identifier) {
    flags |= To<CSSIdentifierValue>(identifier).ConvertTo<ScrollbarGutter>();
  };

  if (auto* value_list = DynamicTo<CSSValueList>(value)) {
    for (auto& entry : *value_list)
      process(*entry);
  } else {
    process(value);
  }
  return flags;
}

Vector<AtomicString> StyleBuilderConverter::ConvertContainerName(
    StyleResolverState& state,
    const CSSValue& value) {
  Vector<AtomicString> names;

  if (auto* ident = DynamicTo<CSSIdentifierValue>(value)) {
    DCHECK_EQ(To<CSSIdentifierValue>(value).GetValueID(), CSSValueID::kNone);
    return names;
  }

  for (const auto& item : To<CSSValueList>(value))
    names.push_back(To<CSSCustomIdentValue>(item.Get())->Value());

  return names;
}

absl::optional<StyleIntrinsicLength>
StyleBuilderConverter::ConvertIntrinsicDimension(
    const StyleResolverState& state,
    const CSSValue& value) {
  // If we have a single identifier, it is "none" in either syntax.
  auto* identifier_value = DynamicTo<CSSIdentifierValue>(value);
  if (identifier_value) {
    DCHECK(identifier_value->GetValueID() == CSSValueID::kNone);
    return absl::nullopt;
  }

  bool has_auto = false;
  auto* primitive_value = DynamicTo<CSSPrimitiveValue>(value);
  if (!primitive_value) {
    // Must be new syntax
    const CSSValueList& list = To<CSSValueList>(value);

    identifier_value = DynamicTo<CSSIdentifierValue>(list.Item(0));
    DCHECK(!identifier_value ||
           identifier_value->GetValueID() == CSSValueID::kAuto);
    DCHECK(!identifier_value || list.length() == 2u);
    has_auto = identifier_value != nullptr;
    primitive_value = DynamicTo<CSSPrimitiveValue>(list.Item(0));
    if (!primitive_value) {
      DCHECK_EQ(list.length(), 2u);
      primitive_value = DynamicTo<CSSPrimitiveValue>(list.Item(1));
    }
  }
  DCHECK(primitive_value);

  return StyleIntrinsicLength(has_auto, ConvertLength(state, *primitive_value));
}

ColorSchemeFlags StyleBuilderConverter::ExtractColorSchemes(
    const Document& document,
    const CSSValueList& scheme_list,
    Vector<AtomicString>* color_schemes) {
  ColorSchemeFlags flags =
      static_cast<ColorSchemeFlags>(ColorSchemeFlag::kNormal);
  for (auto& item : scheme_list) {
    if (const auto* custom_ident = DynamicTo<CSSCustomIdentValue>(*item)) {
      if (color_schemes)
        color_schemes->push_back(custom_ident->Value());
    } else if (const auto* ident = DynamicTo<CSSIdentifierValue>(*item)) {
      if (color_schemes)
        color_schemes->push_back(ident->CssText());
      switch (ident->GetValueID()) {
        case CSSValueID::kDark:
          flags |= static_cast<ColorSchemeFlags>(ColorSchemeFlag::kDark);
          break;
        case CSSValueID::kLight:
          flags |= static_cast<ColorSchemeFlags>(ColorSchemeFlag::kLight);
          break;
        case CSSValueID::kOnly:
          flags |= static_cast<ColorSchemeFlags>(ColorSchemeFlag::kOnly);
          break;
        default:
          break;
      }
    } else {
      NOTREACHED();
    }
  }
  return flags;
}

double StyleBuilderConverter::ConvertTimeValue(const StyleResolverState& state,
                                               const CSSValue& value) {
  return To<CSSPrimitiveValue>(value).ComputeSeconds();
}

scoped_refptr<ToggleGroupList> StyleBuilderConverter::ConvertToggleGroup(
    const StyleResolverState& state,
    const CSSValue& value) {
  if (const auto* ident = DynamicTo<CSSIdentifierValue>(value)) {
    DCHECK_EQ(ident->GetValueID(), CSSValueID::kNone);
    return nullptr;
  }

  scoped_refptr<ToggleGroupList> result = ToggleGroupList::Create();
  for (const auto& item : To<CSSValueList>(value)) {
    const auto* item_list = To<CSSValueList>(item.Get());
    DCHECK_LE(1u, item_list->length());
    DCHECK_LE(item_list->length(), 2u);
    const AtomicString& name =
        To<CSSCustomIdentValue>(item_list->Item(0)).Value();
    ToggleScope scope = ToggleScope::kWide;
    if (item_list->length() == 2u) {
      DCHECK_EQ(To<CSSIdentifierValue>(item_list->Item(1)).GetValueID(),
                CSSValueID::kSelf);
      scope = ToggleScope::kNarrow;
    }

    result->Append(ToggleGroup(name, scope));
  }
  return result;
}
scoped_refptr<ToggleRootList> StyleBuilderConverter::ConvertToggleRoot(
    const StyleResolverState& state_unused,
    const CSSValue& value) {
  if (const auto* ident = DynamicTo<CSSIdentifierValue>(value)) {
    DCHECK_EQ(ident->GetValueID(), CSSValueID::kNone);
    return nullptr;
  }

  scoped_refptr<ToggleRootList> result = ToggleRootList::Create();
  for (const auto& item : To<CSSValueList>(value)) {
    const auto* item_list = To<CSSValueList>(item.Get());
    DCHECK_LE(1u, item_list->length());
    const AtomicString& name =
        To<CSSCustomIdentValue>(item_list->Item(0)).Value();

    wtf_size_t index = 1u;

    ToggleRoot::States states(1u);
    ToggleRoot::State initial_state(0u);
    if (index < item_list->length()) {
      bool found_states = false;
      const CSSValue& state_value = item_list->Item(index);
      if (const auto* states_list = DynamicTo<CSSValueList>(state_value)) {
        ++index;
        found_states = true;
        DCHECK_LE(2u, states_list->length());
        ToggleRoot::States::NamesType states_vec;
        states_vec.ReserveInitialCapacity(states_list->length());
        for (const auto& state : *states_list) {
          states_vec.push_back(To<CSSCustomIdentValue>(*state).Value());
        }
        states = ToggleRoot::States(std::move(states_vec));
      } else if (const auto* maximum_state_number =
                     DynamicTo<CSSPrimitiveValue>(state_value)) {
        ++index;
        found_states = true;
        states = ToggleRoot::States(maximum_state_number->GetValue<uint32_t>());
      }

      if (found_states && index < item_list->length()) {
        const auto* at_value =
            DynamicTo<CSSIdentifierValue>(item_list->Item(index));
        if (at_value && at_value->GetValueID() == CSSValueID::kAt) {
          ++index;

          DCHECK_LT(index, item_list->length());
          const CSSValue& initial_state_value = item_list->Item(index);
          ++index;
          if (const auto* initial_state_ident =
                  DynamicTo<CSSCustomIdentValue>(initial_state_value)) {
            initial_state = ToggleRoot::State(initial_state_ident->Value());
          } else {
            const auto& initial_state_number =
                To<CSSPrimitiveValue>(initial_state_value);
            initial_state =
                ToggleRoot::State(initial_state_number.GetValue<uint32_t>());
          }
        }
      }
    }

    ToggleOverflow overflow = ToggleOverflow::kCycle;
    if (index < item_list->length()) {
      switch (To<CSSIdentifierValue>(item_list->Item(index)).GetValueID()) {
        case CSSValueID::kCycle:
          overflow = ToggleOverflow::kCycle;
          ++index;
          break;
        case CSSValueID::kCycleOn:
          overflow = ToggleOverflow::kCycleOn;
          ++index;
          break;
        case CSSValueID::kSticky:
          overflow = ToggleOverflow::kSticky;
          ++index;
          break;
        default:
          break;
      }
    }

    bool is_group = false;
    if (index < item_list->length() &&
        To<CSSIdentifierValue>(item_list->Item(index)).GetValueID() ==
            CSSValueID::kGroup) {
      ++index;
      is_group = true;
    }

    ToggleScope scope = ToggleScope::kWide;
    if (index < item_list->length() &&
        To<CSSIdentifierValue>(item_list->Item(index)).GetValueID() ==
            CSSValueID::kSelf) {
      ++index;
      scope = ToggleScope::kNarrow;
    }
    DCHECK_EQ(item_list->length(), index);

    result->Append(
        ToggleRoot(name, states, initial_state, overflow, is_group, scope));
  }
  return result;
}
scoped_refptr<ToggleTriggerList> StyleBuilderConverter::ConvertToggleTrigger(
    const StyleResolverState& state,
    const CSSValue& value) {
  if (const auto* ident = DynamicTo<CSSIdentifierValue>(value)) {
    DCHECK_EQ(ident->GetValueID(), CSSValueID::kNone);
    return nullptr;
  }

  scoped_refptr<ToggleTriggerList> result = ToggleTriggerList::Create();
  for (const auto& item : To<CSSValueList>(value)) {
    const auto* item_list = To<CSSValueList>(item.Get());
    DCHECK_LE(1u, item_list->length());
    DCHECK_LE(item_list->length(), 3u);
    const AtomicString& name =
        To<CSSCustomIdentValue>(item_list->Item(0)).Value();

    ToggleTriggerMode mode = ToggleTriggerMode::kNext;
    if (item_list->length() > 1u) {
      switch (To<CSSIdentifierValue>(item_list->Item(1)).GetValueID()) {
        case CSSValueID::kPrev:
          mode = ToggleTriggerMode::kPrev;
          break;
        case CSSValueID::kNext:
          mode = ToggleTriggerMode::kNext;
          break;
        case CSSValueID::kSet:
          mode = ToggleTriggerMode::kSet;
          break;
        default:
          NOTREACHED();
      }
    }

    ToggleTrigger::State trigger_value(1);
    if (item_list->length() == 3u) {
      const CSSValue& target_value = item_list->Item(2);
      if (const auto* target_ident =
              DynamicTo<CSSCustomIdentValue>(target_value)) {
        trigger_value = ToggleTrigger::State(target_ident->Value());
      } else {
        const auto& target_number = To<CSSPrimitiveValue>(target_value);
        trigger_value =
            ToggleTrigger::State(target_number.GetValue<uint32_t>());
      }
    } else {
      DCHECK_NE(mode, ToggleTriggerMode::kSet);
    }

    result->Append(ToggleTrigger(name, mode, trigger_value));
  }
  return result;
}

absl::optional<StyleOverflowClipMargin>
StyleBuilderConverter::ConvertOverflowClipMargin(StyleResolverState& state,
                                                 const CSSValue& value) {
  const auto& css_value_list = To<CSSValueList>(value);
  DCHECK(css_value_list.length() == 1u || css_value_list.length() == 2u);

  const CSSIdentifierValue* reference_box_value = nullptr;
  const CSSPrimitiveValue* length_value = nullptr;

  if (css_value_list.Item(0).IsIdentifierValue()) {
    reference_box_value = &To<CSSIdentifierValue>(css_value_list.Item(0));
  } else {
    DCHECK(css_value_list.Item(0).IsPrimitiveValue());
    length_value = &To<CSSPrimitiveValue>(css_value_list.Item(0));
  }

  if (css_value_list.length() > 1) {
    const auto& primitive_value = css_value_list.Item(1);
    DCHECK(primitive_value.IsPrimitiveValue());
    DCHECK(!length_value);
    length_value = &To<CSSPrimitiveValue>(primitive_value);
  }

  auto reference_box = StyleOverflowClipMargin::ReferenceBox::kPaddingBox;
  if (reference_box_value) {
    switch (reference_box_value->GetValueID()) {
      case CSSValueID::kBorderBox:
        reference_box = StyleOverflowClipMargin::ReferenceBox::kBorderBox;
        break;
      case CSSValueID::kContentBox:
        reference_box = StyleOverflowClipMargin::ReferenceBox::kContentBox;
        break;
      case CSSValueID::kPaddingBox:
        reference_box = StyleOverflowClipMargin::ReferenceBox::kPaddingBox;
        break;
      default:
        NOTREACHED();
    }
  }

  LayoutUnit margin;
  if (length_value) {
    margin = StyleBuilderConverter::ConvertLayoutUnit(state, *length_value);
  }
  return StyleOverflowClipMargin(reference_box, margin);
}

Vector<TimelineAxis> StyleBuilderConverter::ConvertViewTimelineAxis(
    StyleResolverState& state,
    const CSSValue& value) {
  Vector<TimelineAxis> axes;
  for (const Member<const CSSValue>& item : To<CSSValueList>(value)) {
    axes.push_back(To<CSSIdentifierValue>(*item).ConvertTo<TimelineAxis>());
  }
  return axes;
}

namespace {

TimelineInset ConvertSingleTimelineInset(StyleResolverState& state,
                                         const CSSValue& value) {
  const CSSValuePair& pair = To<CSSValuePair>(value);
  Length start =
      StyleBuilderConverter::ConvertLengthOrAuto(state, pair.First());
  Length end = StyleBuilderConverter::ConvertLengthOrAuto(state, pair.Second());
  return TimelineInset(start, end);
}

}  // namespace

Vector<TimelineInset> StyleBuilderConverter::ConvertViewTimelineInset(
    StyleResolverState& state,
    const CSSValue& value) {
  Vector<TimelineInset> insets;
  for (const Member<const CSSValue>& item : To<CSSValueList>(value)) {
    insets.push_back(ConvertSingleTimelineInset(state, *item));
  }
  return insets;
}

Vector<AtomicString> StyleBuilderConverter::ConvertViewTimelineName(
    StyleResolverState& state,
    const CSSValue& value) {
  Vector<AtomicString> names;
  for (const Member<const CSSValue>& item : To<CSSValueList>(value)) {
    names.push_back(ConvertNoneOrCustomIdent(state, *item));
  }
  return names;
}

}  // namespace blink
