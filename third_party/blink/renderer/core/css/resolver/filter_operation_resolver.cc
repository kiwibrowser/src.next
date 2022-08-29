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
 * Copyright (C) 2012 Google Inc. All rights reserved.
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

#include "third_party/blink/renderer/core/css/resolver/filter_operation_resolver.h"

#include "third_party/blink/renderer/core/css/css_function_value.h"
#include "third_party/blink/renderer/core/css/css_primitive_value_mappings.h"
#include "third_party/blink/renderer/core/css/css_uri_value.h"
#include "third_party/blink/renderer/core/css/resolver/style_builder_converter.h"
#include "third_party/blink/renderer/core/css/resolver/style_resolver_state.h"
#include "third_party/blink/renderer/core/frame/web_feature.h"
#include "third_party/blink/renderer/core/style/computed_style.h"
#include "third_party/blink/renderer/platform/heap/garbage_collected.h"
#include "third_party/blink/renderer/platform/instrumentation/use_counter.h"

namespace blink {

static const float kOffScreenCanvasEmFontSize = 16.0;
static const float kOffScreenCanvasRemFontSize = 16.0;

FilterOperation::OperationType FilterOperationResolver::FilterOperationForType(
    CSSValueID type) {
  switch (type) {
    case CSSValueID::kGrayscale:
      return FilterOperation::OperationType::kGrayscale;
    case CSSValueID::kSepia:
      return FilterOperation::OperationType::kSepia;
    case CSSValueID::kSaturate:
      return FilterOperation::OperationType::kSaturate;
    case CSSValueID::kHueRotate:
      return FilterOperation::OperationType::kHueRotate;
    case CSSValueID::kInvert:
      return FilterOperation::OperationType::kInvert;
    case CSSValueID::kOpacity:
      return FilterOperation::OperationType::kOpacity;
    case CSSValueID::kBrightness:
      return FilterOperation::OperationType::kBrightness;
    case CSSValueID::kContrast:
      return FilterOperation::OperationType::kContrast;
    case CSSValueID::kBlur:
      return FilterOperation::OperationType::kBlur;
    case CSSValueID::kDropShadow:
      return FilterOperation::OperationType::kDropShadow;
    default:
      NOTREACHED();
      // FIXME: We shouldn't have a type None since we never create them
      return FilterOperation::OperationType::kNone;
  }
}

static void CountFilterUse(FilterOperation::OperationType operation_type,
                           const Document& document) {
  // This variable is always reassigned, but MSVC thinks it might be left
  // uninitialized.
  WebFeature feature = WebFeature::kNumberOfFeatures;
  switch (operation_type) {
    case FilterOperation::OperationType::kNone:
    case FilterOperation::OperationType::kBoxReflect:
    case FilterOperation::OperationType::kConvolveMatrix:
    case FilterOperation::OperationType::kComponentTransfer:
    case FilterOperation::OperationType::kTurbulence:
      NOTREACHED();
      return;
    case FilterOperation::OperationType::kReference:
      feature = WebFeature::kCSSFilterReference;
      break;
    case FilterOperation::OperationType::kGrayscale:
      feature = WebFeature::kCSSFilterGrayscale;
      break;
    case FilterOperation::OperationType::kSepia:
      feature = WebFeature::kCSSFilterSepia;
      break;
    case FilterOperation::OperationType::kSaturate:
      feature = WebFeature::kCSSFilterSaturate;
      break;
    case FilterOperation::OperationType::kHueRotate:
      feature = WebFeature::kCSSFilterHueRotate;
      break;
    case FilterOperation::OperationType::kLuminanceToAlpha:
      feature = WebFeature::kCSSFilterLuminanceToAlpha;
      break;
    case FilterOperation::OperationType::kColorMatrix:
      feature = WebFeature::kCSSFilterColorMatrix;
      break;
    case FilterOperation::OperationType::kInvert:
      feature = WebFeature::kCSSFilterInvert;
      break;
    case FilterOperation::OperationType::kOpacity:
      feature = WebFeature::kCSSFilterOpacity;
      break;
    case FilterOperation::OperationType::kBrightness:
      feature = WebFeature::kCSSFilterBrightness;
      break;
    case FilterOperation::OperationType::kContrast:
      feature = WebFeature::kCSSFilterContrast;
      break;
    case FilterOperation::OperationType::kBlur:
      feature = WebFeature::kCSSFilterBlur;
      break;
    case FilterOperation::OperationType::kDropShadow:
      feature = WebFeature::kCSSFilterDropShadow;
      break;
  };
  document.CountUse(feature);
}

double FilterOperationResolver::ResolveNumericArgumentForFunction(
    const CSSFunctionValue& filter) {
  switch (filter.FunctionType()) {
    case CSSValueID::kGrayscale:
    case CSSValueID::kSepia:
    case CSSValueID::kSaturate:
    case CSSValueID::kInvert:
    case CSSValueID::kBrightness:
    case CSSValueID::kContrast:
    case CSSValueID::kOpacity: {
      double amount = 1;
      if (filter.length() == 1) {
        const CSSPrimitiveValue& value = To<CSSPrimitiveValue>(filter.Item(0));
        amount = value.GetDoubleValue();
        if (value.IsPercentage())
          amount /= 100;
      }
      return amount;
    }
    case CSSValueID::kHueRotate: {
      double angle = 0;
      if (filter.length() == 1) {
        const CSSPrimitiveValue& value = To<CSSPrimitiveValue>(filter.Item(0));
        angle = value.ComputeDegrees();
      }
      return angle;
    }
    default:
      return 0;
  }
}

FilterOperations FilterOperationResolver::CreateFilterOperations(
    StyleResolverState& state,
    const CSSValue& in_value,
    CSSPropertyID property_id) {
  FilterOperations operations;

  if (auto* in_identifier_value = DynamicTo<CSSIdentifierValue>(in_value)) {
    DCHECK_EQ(in_identifier_value->GetValueID(), CSSValueID::kNone);
    return operations;
  }

  const CSSToLengthConversionData& conversion_data =
      state.CssToLengthConversionData();

  for (auto& curr_value : To<CSSValueList>(in_value)) {
    if (const auto* url_value =
            DynamicTo<cssvalue::CSSURIValue>(curr_value.Get())) {
      CountFilterUse(FilterOperation::OperationType::kReference,
                     state.GetDocument());

      SVGResource* resource =
          state.GetElementStyleResources().GetSVGResourceFromValue(property_id,
                                                                   *url_value);
      operations.Operations().push_back(
          MakeGarbageCollected<ReferenceFilterOperation>(
              url_value->ValueForSerialization(), resource));
      continue;
    }

    const auto* filter_value = To<CSSFunctionValue>(curr_value.Get());
    FilterOperation::OperationType operation_type =
        FilterOperationForType(filter_value->FunctionType());
    CountFilterUse(operation_type, state.GetDocument());
    DCHECK_LE(filter_value->length(), 1u);
    switch (filter_value->FunctionType()) {
      case CSSValueID::kGrayscale:
      case CSSValueID::kSepia:
      case CSSValueID::kSaturate:
      case CSSValueID::kHueRotate: {
        operations.Operations().push_back(
            MakeGarbageCollected<BasicColorMatrixFilterOperation>(
                ResolveNumericArgumentForFunction(*filter_value),
                operation_type));
        break;
      }
      case CSSValueID::kInvert:
      case CSSValueID::kBrightness:
      case CSSValueID::kContrast:
      case CSSValueID::kOpacity: {
        operations.Operations().push_back(
            MakeGarbageCollected<BasicComponentTransferFilterOperation>(
                ResolveNumericArgumentForFunction(*filter_value),
                operation_type));
        break;
      }
      case CSSValueID::kBlur: {
        Length std_deviation = Length::Fixed(0);
        if (filter_value->length() >= 1) {
          const CSSPrimitiveValue* first_value =
              DynamicTo<CSSPrimitiveValue>(filter_value->Item(0));
          std_deviation = first_value->ConvertToLength(conversion_data);
        }
        operations.Operations().push_back(
            MakeGarbageCollected<BlurFilterOperation>(std_deviation));
        break;
      }
      case CSSValueID::kDropShadow: {
        ShadowData shadow = StyleBuilderConverter::ConvertShadow(
            conversion_data, &state, filter_value->Item(0));
        // TODO(fs): Resolve 'currentcolor' when constructing the filter chain.
        if (shadow.GetColor().IsCurrentColor()) {
          shadow.OverrideColor(state.Style()->GetCurrentColor());
        }
        operations.Operations().push_back(
            MakeGarbageCollected<DropShadowFilterOperation>(shadow));
        break;
      }
      default:
        NOTREACHED();
        break;
    }
  }

  return operations;
}

FilterOperations FilterOperationResolver::CreateOffscreenFilterOperations(
    const CSSValue& in_value,
    const Font& font) {
  FilterOperations operations;

  if (auto* in_identifier_value = DynamicTo<CSSIdentifierValue>(in_value)) {
    DCHECK_EQ(in_identifier_value->GetValueID(), CSSValueID::kNone);
    return operations;
  }

  // TODO(layout-dev): Should document zoom factor apply for offscreen canvas?
  float zoom = 1.0f;
  CSSToLengthConversionData::FontSizes font_sizes(
      kOffScreenCanvasEmFontSize, kOffScreenCanvasRemFontSize, &font, zoom);
  CSSToLengthConversionData::ViewportSize viewport_size(0, 0);
  CSSToLengthConversionData::ContainerSizes container_sizes;
  CSSToLengthConversionData conversion_data(
      nullptr,  // ComputedStyle
      WritingMode::kHorizontalTb, font_sizes, viewport_size, container_sizes,
      1);  // zoom

  for (auto& curr_value : To<CSSValueList>(in_value)) {
    if (curr_value->IsURIValue())
      continue;

    const auto* filter_value = To<CSSFunctionValue>(curr_value.Get());
    FilterOperation::OperationType operation_type =
        FilterOperationForType(filter_value->FunctionType());
    // TODO(fserb): Take an ExecutionContext argument to this function,
    // so we can have workers using UseCounter as well.
    // countFilterUse(operationType, state.document());
    DCHECK_LE(filter_value->length(), 1u);
    switch (filter_value->FunctionType()) {
      case CSSValueID::kGrayscale:
      case CSSValueID::kSepia:
      case CSSValueID::kSaturate:
      case CSSValueID::kHueRotate: {
        operations.Operations().push_back(
            MakeGarbageCollected<BasicColorMatrixFilterOperation>(
                ResolveNumericArgumentForFunction(*filter_value),
                operation_type));
        break;
      }
      case CSSValueID::kInvert:
      case CSSValueID::kBrightness:
      case CSSValueID::kContrast:
      case CSSValueID::kOpacity: {
        operations.Operations().push_back(
            MakeGarbageCollected<BasicComponentTransferFilterOperation>(
                ResolveNumericArgumentForFunction(*filter_value),
                operation_type));
        break;
      }
      case CSSValueID::kBlur: {
        Length std_deviation = Length::Fixed(0);
        if (filter_value->length() >= 1) {
          const CSSPrimitiveValue* first_value =
              DynamicTo<CSSPrimitiveValue>(filter_value->Item(0));
          std_deviation = first_value->ConvertToLength(conversion_data);
        }
        operations.Operations().push_back(
            MakeGarbageCollected<BlurFilterOperation>(std_deviation));
        break;
      }
      case CSSValueID::kDropShadow: {
        ShadowData shadow = StyleBuilderConverter::ConvertShadow(
            conversion_data, nullptr, filter_value->Item(0));
        // For offscreen canvas, the default color is always black.
        if (shadow.GetColor().IsCurrentColor()) {
          shadow.OverrideColor(Color::kBlack);
        }
        operations.Operations().push_back(
            MakeGarbageCollected<DropShadowFilterOperation>(shadow));
        break;
      }
      default:
        NOTREACHED();
        break;
    }
  }
  return operations;
}

}  // namespace blink
