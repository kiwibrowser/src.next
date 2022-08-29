// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/css/css_math_function_value.h"

#include "third_party/blink/renderer/core/css/css_math_expression_node.h"
#include "third_party/blink/renderer/core/css/css_value_clamping_utils.h"
#include "third_party/blink/renderer/platform/geometry/calculation_expression_node.h"
#include "third_party/blink/renderer/platform/geometry/length.h"
#include "third_party/blink/renderer/platform/wtf/size_assertions.h"
#include "third_party/blink/renderer/platform/wtf/text/string_builder.h"

namespace blink {

struct SameSizeAsCSSMathFunctionValue : CSSPrimitiveValue {
  Member<void*> expression;
  ValueRange value_range_in_target_context_;
};
ASSERT_SIZE(CSSMathFunctionValue, SameSizeAsCSSMathFunctionValue);

void CSSMathFunctionValue::TraceAfterDispatch(blink::Visitor* visitor) const {
  visitor->Trace(expression_);
  CSSPrimitiveValue::TraceAfterDispatch(visitor);
}

CSSMathFunctionValue::CSSMathFunctionValue(
    const CSSMathExpressionNode* expression,
    CSSPrimitiveValue::ValueRange range)
    : CSSPrimitiveValue(kMathFunctionClass),
      expression_(expression),
      value_range_in_target_context_(range) {}

// static
CSSMathFunctionValue* CSSMathFunctionValue::Create(
    const CSSMathExpressionNode* expression,
    CSSPrimitiveValue::ValueRange range) {
  if (!expression)
    return nullptr;
  return MakeGarbageCollected<CSSMathFunctionValue>(expression, range);
}

// static
CSSMathFunctionValue* CSSMathFunctionValue::Create(const Length& length,
                                                   float zoom) {
  DCHECK(length.IsCalculated());
  auto calc = length.GetCalculationValue().Zoom(1.0 / zoom);
  return Create(
      CSSMathExpressionNode::Create(*calc),
      CSSPrimitiveValue::ValueRangeForLengthValueRange(calc->GetValueRange()));
}

bool CSSMathFunctionValue::MayHaveRelativeUnit() const {
  UnitType resolved_type = expression_->ResolvedUnitType();
  return IsRelativeUnit(resolved_type) || resolved_type == UnitType::kUnknown;
}

double CSSMathFunctionValue::DoubleValue() const {
#if DCHECK_IS_ON()
  if (IsPercentage()) {
    DCHECK(!AllowsNegativePercentageReference() ||
           !expression_->InvolvesPercentageComparisons());
  }
#endif
  return ClampToPermittedRange(expression_->DoubleValue());
}

double CSSMathFunctionValue::ComputeSeconds() const {
  DCHECK_EQ(kCalcTime, expression_->Category());
  return ClampToPermittedRange(*expression_->ComputeValueInCanonicalUnit());
}

double CSSMathFunctionValue::ComputeDegrees() const {
  DCHECK_EQ(kCalcAngle, expression_->Category());
  return ClampToPermittedRange(*expression_->ComputeValueInCanonicalUnit());
}

double CSSMathFunctionValue::ComputeLengthPx(
    const CSSLengthResolver& length_resolver) const {
  // |CSSToLengthConversionData| only resolves relative length units, but not
  // percentages.
  DCHECK_EQ(kCalcLength, expression_->Category());
  return ClampToPermittedRange(expression_->ComputeLengthPx(length_resolver));
}

bool CSSMathFunctionValue::AccumulateLengthArray(CSSLengthArray& length_array,
                                                 double multiplier) const {
  return expression_->AccumulateLengthArray(length_array, multiplier);
}

Length CSSMathFunctionValue::ConvertToLength(
    const CSSLengthResolver& length_resolver) const {
  if (IsLength())
    return Length::Fixed(ComputeLengthPx(length_resolver));
  return Length(ToCalcValue(length_resolver));
}

static String BuildCSSText(const String& expression) {
  StringBuilder result;
  result.Append("calc");
  result.Append('(');
  result.Append(expression);
  result.Append(')');
  return result.ReleaseString();
}

String CSSMathFunctionValue::CustomCSSText() const {
  const String& expression_text = expression_->CustomCSSText();
  if (expression_->IsMathFunction()) {
    // If |expression_| is already a math function (e.g., min/max), we don't
    // need to wrap it in |calc()|.
    return expression_text;
  }
  return BuildCSSText(expression_text);
}

bool CSSMathFunctionValue::Equals(const CSSMathFunctionValue& other) const {
  return base::ValuesEquivalent(expression_, other.expression_);
}

double CSSMathFunctionValue::ClampToPermittedRange(double value) const {
  switch (PermittedValueRange()) {
    case CSSPrimitiveValue::ValueRange::kInteger:
      return RoundHalfTowardsPositiveInfinity(value);
    case CSSPrimitiveValue::ValueRange::kNonNegativeInteger:
      return RoundHalfTowardsPositiveInfinity(std::max(value, 0.0));
    case CSSPrimitiveValue::ValueRange::kPositiveInteger:
      return RoundHalfTowardsPositiveInfinity(std::max(value, 1.0));
    case CSSPrimitiveValue::ValueRange::kNonNegative:
      return std::max(value, 0.0);
    case CSSPrimitiveValue::ValueRange::kAll:
      return value;
  }
}

bool CSSMathFunctionValue::IsZero() const {
  if (expression_->ResolvedUnitType() == UnitType::kUnknown)
    return false;
  return expression_->IsZero();
}

bool CSSMathFunctionValue::IsPx() const {
  // TODO(crbug.com/979895): This is the result of refactoring, which might be
  // an existing bug. Fix it if necessary.
  return Category() == kCalcLength;
}

bool CSSMathFunctionValue::IsComputationallyIndependent() const {
  return expression_->IsComputationallyIndependent();
}

scoped_refptr<const CalculationValue> CSSMathFunctionValue::ToCalcValue(
    const CSSLengthResolver& length_resolver) const {
  DCHECK_NE(value_range_in_target_context_,
            CSSPrimitiveValue::ValueRange::kInteger);
  DCHECK_NE(value_range_in_target_context_,
            CSSPrimitiveValue::ValueRange::kNonNegativeInteger);
  DCHECK_NE(value_range_in_target_context_,
            CSSPrimitiveValue::ValueRange::kPositiveInteger);
  return expression_->ToCalcValue(
      length_resolver,
      CSSPrimitiveValue::ConversionToLengthValueRange(PermittedValueRange()),
      AllowsNegativePercentageReference());
}

}  // namespace blink
