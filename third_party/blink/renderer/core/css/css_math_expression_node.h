/*
 * Copyright (C) 2011, 2012 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
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

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_CSS_CSS_MATH_EXPRESSION_NODE_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_CSS_CSS_MATH_EXPRESSION_NODE_H_

#include "base/check_op.h"
#include "base/dcheck_is_on.h"
#include "third_party/abseil-cpp/absl/types/optional.h"
#include "third_party/blink/renderer/core/core_export.h"
#include "third_party/blink/renderer/core/css/css_anchor_query_enums.h"
#include "third_party/blink/renderer/core/css/css_length_resolver.h"
#include "third_party/blink/renderer/core/css/css_math_operator.h"
#include "third_party/blink/renderer/core/css/css_primitive_value.h"
#include "third_party/blink/renderer/core/css/css_value.h"
#include "third_party/blink/renderer/core/css/parser/css_parser_token_range.h"
#include "third_party/blink/renderer/platform/geometry/calculation_value.h"
#include "third_party/blink/renderer/platform/wtf/forward.h"

namespace blink {

static const int kMaxExpressionDepth = 100;

class CalculationExpressionNode;
class CSSNumericLiteralValue;
class CSSParserContext;

// The order of this enum should not change since its elements are used as
// indices in the addSubtractResult matrix.
enum CalculationResultCategory {
  kCalcNumber,
  kCalcLength,
  kCalcPercent,
  // TODO(crbug.com/1309178): We are now using this for all calculated lengths
  // that can't be resolved at style time, including not only calc(px + %) but
  // also anchor queries. Rename this category accordingly.
  kCalcPercentLength,
  kCalcAngle,
  kCalcTime,
  kCalcFrequency,
  kCalcResolution,
  kCalcIdent,
  kCalcOther,
};

class CORE_EXPORT CSSMathExpressionNode
    : public GarbageCollected<CSSMathExpressionNode> {
 public:
  static CSSMathExpressionNode* Create(const CalculationValue& node);
  static CSSMathExpressionNode* Create(PixelsAndPercent pixels_and_percent);
  static CSSMathExpressionNode* Create(const CalculationExpressionNode& node);

  static CSSMathExpressionNode* ParseMathFunction(
      CSSValueID function_id,
      CSSParserTokenRange tokens,
      const CSSParserContext&,
      const bool is_percentage_allowed,
      CSSAnchorQueryTypes allowed_anchor_queries,
      // Variable substitutions for relative color syntax.
      // https://www.w3.org/TR/css-color-5/#relative-colors
      const HashMap<CSSValueID, double>& color_channel_keyword_values = {});

  virtual CSSMathExpressionNode* Copy() const = 0;

  virtual bool IsNumericLiteral() const { return false; }
  virtual bool IsOperation() const { return false; }
  virtual bool IsAnchorQuery() const { return false; }
  virtual bool IsIdentifierLiteral() const { return false; }

  virtual bool IsMathFunction() const { return false; }

  virtual bool IsZero() const = 0;

  // Resolves the expression into one value *without doing any type conversion*.
  // Hits DCHECK if type conversion is required.
  virtual double DoubleValue() const = 0;

  double ComputeNumber(const CSSLengthResolver& length_resolver) const {
    return ComputeDouble(length_resolver);
  }
  virtual double ComputeLengthPx(const CSSLengthResolver&) const = 0;
  virtual bool AccumulateLengthArray(CSSLengthArray&,
                                     double multiplier) const = 0;
  virtual void AccumulateLengthUnitTypes(
      CSSPrimitiveValue::LengthTypeFlags& types) const = 0;
  virtual scoped_refptr<const CalculationExpressionNode>
  ToCalculationExpression(const CSSLengthResolver&) const = 0;
  virtual absl::optional<PixelsAndPercent> ToPixelsAndPercent(
      const CSSLengthResolver&) const = 0;

  scoped_refptr<const CalculationValue> ToCalcValue(
      const CSSLengthResolver& length_resolver,
      Length::ValueRange range,
      bool allows_negative_percentage_reference) const;

  // Evaluates the expression with type conversion (e.g., cm -> px) handled, and
  // returns the result value in the canonical unit of the corresponding
  // category (see https://www.w3.org/TR/css3-values/#canonical-unit).
  // TODO(crbug.com/984372): We currently use 'ms' as the canonical unit of
  // <time>. Switch to 's' to follow the spec.
  // Returns |nullopt| on evaluation failures due to the following reasons:
  // - The category doesn't have a canonical unit (e.g., |kCalcPercentLength|).
  // - A type conversion that doesn't have a fixed conversion ratio is needed
  //   (e.g., between 'px' and 'em').
  // - There's an unsupported calculation, e.g., dividing two lengths.
  virtual absl::optional<double> ComputeValueInCanonicalUnit() const = 0;

  virtual String CustomCSSText() const = 0;
  virtual bool operator==(const CSSMathExpressionNode& other) const {
    return category_ == other.category_;
  }

  virtual bool IsComputationallyIndependent() const = 0;

  CalculationResultCategory Category() const { return category_; }
  bool HasPercentage() const {
    return category_ == kCalcPercent || category_ == kCalcPercentLength;
  }
  virtual bool InvolvesPercentage() const { return HasPercentage(); }
  virtual bool InvolvesAnchorQueries() const { return IsAnchorQuery(); }

  // Returns the unit type of the math expression *without doing any type
  // conversion* (e.g., 1px + 1em needs type conversion to resolve).
  // Returns |UnitType::kUnknown| if type conversion is required.
  virtual CSSPrimitiveValue::UnitType ResolvedUnitType() const = 0;

  bool IsNestedCalc() const { return is_nested_calc_; }
  void SetIsNestedCalc() { is_nested_calc_ = true; }

  bool HasComparisons() const { return has_comparisons_; }
  bool IsScopedValue() const { return !needs_tree_scope_population_; }

  const CSSMathExpressionNode& EnsureScopedValue(
      const TreeScope* tree_scope) const {
    if (!needs_tree_scope_population_) {
      return *this;
    }
    return PopulateWithTreeScope(tree_scope);
  }
  virtual const CSSMathExpressionNode& PopulateWithTreeScope(
      const TreeScope*) const = 0;

#if DCHECK_IS_ON()
  // There's a subtle issue in comparing two percentages, e.g., min(10%, 20%).
  // It doesn't always resolve into 10%, because the reference value may be
  // negative. We use this to prevent comparing two percentages without knowing
  // the sign of the reference value.
  virtual bool InvolvesPercentageComparisons() const = 0;
#endif

  virtual void Trace(Visitor* visitor) const {}

 protected:
  CSSMathExpressionNode(CalculationResultCategory category,
                        bool has_comparisons,
                        bool needs_tree_scope_population)
      : category_(category),
        has_comparisons_(has_comparisons),
        needs_tree_scope_population_(needs_tree_scope_population) {
    DCHECK_NE(category, kCalcOther);
  }

  virtual double ComputeDouble(
      const CSSLengthResolver& length_resolver) const = 0;
  static double ComputeDouble(const CSSMathExpressionNode* operand,
                              const CSSLengthResolver& length_resolver) {
    return operand->ComputeDouble(length_resolver);
  }

  CalculationResultCategory category_;
  bool is_nested_calc_ = false;
  bool has_comparisons_;
  bool needs_tree_scope_population_;
};

class CORE_EXPORT CSSMathExpressionNumericLiteral final
    : public CSSMathExpressionNode {
 public:
  static CSSMathExpressionNumericLiteral* Create(
      const CSSNumericLiteralValue* value);
  static CSSMathExpressionNumericLiteral* Create(
      double value,
      CSSPrimitiveValue::UnitType type);

  explicit CSSMathExpressionNumericLiteral(const CSSNumericLiteralValue* value);

  CSSMathExpressionNode* Copy() const final { return Create(value_.Get()); }

  const CSSNumericLiteralValue& GetValue() const { return *value_; }

  bool IsNumericLiteral() const final { return true; }

  const CSSMathExpressionNode& PopulateWithTreeScope(
      const TreeScope* tree_scope) const final {
    NOTREACHED();
    return *this;
  }

  bool IsZero() const final;
  String CustomCSSText() const final;
  scoped_refptr<const CalculationExpressionNode> ToCalculationExpression(
      const CSSLengthResolver&) const final;
  absl::optional<PixelsAndPercent> ToPixelsAndPercent(
      const CSSLengthResolver&) const final;
  double DoubleValue() const final;
  absl::optional<double> ComputeValueInCanonicalUnit() const final;
  double ComputeLengthPx(const CSSLengthResolver& length_resolver) const final;
  bool AccumulateLengthArray(CSSLengthArray& length_array,
                             double multiplier) const final;
  void AccumulateLengthUnitTypes(
      CSSPrimitiveValue::LengthTypeFlags& types) const final;
  bool IsComputationallyIndependent() const final;
  bool operator==(const CSSMathExpressionNode& other) const final;
  CSSPrimitiveValue::UnitType ResolvedUnitType() const final;
  void Trace(Visitor* visitor) const final;

#if DCHECK_IS_ON()
  bool InvolvesPercentageComparisons() const final;
#endif

 protected:
  double ComputeDouble(const CSSLengthResolver& length_resolver) const final;

 private:
  Member<const CSSNumericLiteralValue> value_;
};

template <>
struct DowncastTraits<CSSMathExpressionNumericLiteral> {
  static bool AllowFrom(const CSSMathExpressionNode& node) {
    return node.IsNumericLiteral();
  }
};

// Used for media-feature name in media-progress(),
// for container name in container-progress().
// Will possibly be used in container name for container units function.
class CORE_EXPORT CSSMathExpressionIdentifierLiteral final
    : public CSSMathExpressionNode {
 public:
  static CSSMathExpressionIdentifierLiteral* Create(AtomicString identifier) {
    return MakeGarbageCollected<CSSMathExpressionIdentifierLiteral>(
        std::move(identifier));
  }

  explicit CSSMathExpressionIdentifierLiteral(AtomicString identifier);

  CSSMathExpressionNode* Copy() const final { return Create(identifier_); }

  const AtomicString& GetValue() const { return identifier_; }

  bool IsIdentifierLiteral() const final { return true; }

  const CSSMathExpressionNode& PopulateWithTreeScope(
      const TreeScope* tree_scope) const final {
    NOTREACHED();
    return *this;
  }

  bool IsZero() const final { return false; }
  String CustomCSSText() const final { return identifier_; }
  scoped_refptr<const CalculationExpressionNode> ToCalculationExpression(
      const CSSLengthResolver&) const final;
  absl::optional<PixelsAndPercent> ToPixelsAndPercent(
      const CSSLengthResolver&) const final {
    return absl::nullopt;
  }
  double DoubleValue() const final {
    NOTREACHED();
    return 0;
  }
  absl::optional<double> ComputeValueInCanonicalUnit() const final {
    return absl::nullopt;
  }
  double ComputeLengthPx(const CSSLengthResolver& length_resolver) const final {
    NOTREACHED();
    return 0;
  }
  bool AccumulateLengthArray(CSSLengthArray& length_array,
                             double multiplier) const final {
    return false;
  }
  void AccumulateLengthUnitTypes(
      CSSPrimitiveValue::LengthTypeFlags& types) const final {}
  bool IsComputationallyIndependent() const final { return true; }
  bool operator==(const CSSMathExpressionNode& other) const final {
    return other.IsIdentifierLiteral() &&
           DynamicTo<CSSMathExpressionIdentifierLiteral>(other)->GetValue() ==
               GetValue();
  }
  CSSPrimitiveValue::UnitType ResolvedUnitType() const final {
    return CSSPrimitiveValue::UnitType::kIdent;
  }
  void Trace(Visitor* visitor) const final {
    CSSMathExpressionNode::Trace(visitor);
  }

#if DCHECK_IS_ON()
  bool InvolvesPercentageComparisons() const final { return false; }
#endif

 protected:
  double ComputeDouble(const CSSLengthResolver& length_resolver) const final {
    NOTREACHED();
    return 0;
  }

 private:
  AtomicString identifier_;
};

template <>
struct DowncastTraits<CSSMathExpressionIdentifierLiteral> {
  static bool AllowFrom(const CSSMathExpressionNode& node) {
    return node.IsIdentifierLiteral();
  }
};

class CORE_EXPORT CSSMathExpressionOperation final
    : public CSSMathExpressionNode {
 public:
  using Operands = HeapVector<Member<const CSSMathExpressionNode>>;

  static CSSMathExpressionNode* CreateArithmeticOperation(
      const CSSMathExpressionNode* left_side,
      const CSSMathExpressionNode* right_side,
      CSSMathOperator op);

  static CSSMathExpressionNode* CreateComparisonFunction(Operands&& operands,
                                                         CSSMathOperator op);
  static CSSMathExpressionNode* CreateComparisonFunctionSimplified(
      Operands&& operands,
      CSSMathOperator op);

  static CSSMathExpressionNode* CreateTrigonometricFunctionSimplified(
      Operands&& operands,
      CSSValueID function_id);

  static CSSMathExpressionNode* CreateSteppedValueFunction(Operands&& operands,
                                                           CSSMathOperator op);

  static CSSMathExpressionNode* CreateExponentialFunction(
      Operands&& operands,
      CSSValueID function_id);

  static CSSMathExpressionNode* CreateArithmeticOperationSimplified(
      const CSSMathExpressionNode* left_side,
      const CSSMathExpressionNode* right_side,
      CSSMathOperator op);

  static CSSMathExpressionNode* CreateSignRelatedFunction(
      Operands&& operands,
      CSSValueID function_id);

  CSSMathExpressionOperation(const CSSMathExpressionNode* left_side,
                             const CSSMathExpressionNode* right_side,
                             CSSMathOperator op,
                             CalculationResultCategory category);

  CSSMathExpressionOperation(CalculationResultCategory category,
                             Operands&& operands,
                             CSSMathOperator op);

  CSSMathExpressionOperation(CalculationResultCategory category,
                             CSSMathOperator op);

  CSSMathExpressionNode* Copy() const final {
    Operands operands(operands_);
    return MakeGarbageCollected<CSSMathExpressionOperation>(
        category_, std::move(operands), operator_);
  }

  const Operands& GetOperands() const { return operands_; }
  CSSMathOperator OperatorType() const { return operator_; }

  bool IsOperation() const final { return true; }
  bool IsAddOrSubtract() const {
    return operator_ == CSSMathOperator::kAdd ||
           operator_ == CSSMathOperator::kSubtract;
  }
  bool IsMultiplyOrDivide() const {
    return operator_ == CSSMathOperator::kMultiply ||
           operator_ == CSSMathOperator::kDivide;
  }
  bool AllOperandsAreNumeric() const;
  bool IsMinOrMax() const {
    return operator_ == CSSMathOperator::kMin ||
           operator_ == CSSMathOperator::kMax;
  }
  bool IsClamp() const { return operator_ == CSSMathOperator::kClamp; }
  bool IsRoundingStrategyKeyword() const {
    return CSSMathOperator::kRoundNearest <= operator_ &&
           operator_ <= CSSMathOperator::kRoundToZero && !operands_.size();
  }
  bool IsSteppedValueFunction() const {
    return CSSMathOperator::kRoundNearest <= operator_ &&
           operator_ <= CSSMathOperator::kRem;
  }
  bool IsTrigonometricFunction() const {
    return operator_ == CSSMathOperator::kHypot;
  }
  bool IsSignRelatedFunction() const {
    return operator_ == CSSMathOperator::kAbs ||
           operator_ == CSSMathOperator::kSign;
  }

  // TODO(crbug.com/1284199): Check other math functions too.
  bool IsMathFunction() const final {
    return IsMinOrMax() || IsClamp() || IsSteppedValueFunction() ||
           IsTrigonometricFunction() || IsSignRelatedFunction();
  }

  bool InvolvesPercentage() const final;
  bool InvolvesAnchorQueries() const final;

  String CSSTextAsClamp() const;

  bool IsZero() const final;
  scoped_refptr<const CalculationExpressionNode> ToCalculationExpression(
      const CSSLengthResolver&) const final;
  absl::optional<PixelsAndPercent> ToPixelsAndPercent(
      const CSSLengthResolver&) const final;
  double DoubleValue() const final;
  absl::optional<double> ComputeValueInCanonicalUnit() const final;
  double ComputeLengthPx(const CSSLengthResolver& length_resolver) const final;
  bool AccumulateLengthArray(CSSLengthArray& length_array,
                             double multiplier) const final;
  void AccumulateLengthUnitTypes(
      CSSPrimitiveValue::LengthTypeFlags& types) const final;
  bool IsComputationallyIndependent() const final;
  String CustomCSSText() const final;
  bool operator==(const CSSMathExpressionNode& exp) const final;
  CSSPrimitiveValue::UnitType ResolvedUnitType() const final;
  const CSSMathExpressionNode& PopulateWithTreeScope(
      const TreeScope*) const final;
  void Trace(Visitor* visitor) const final;

#if DCHECK_IS_ON()
  bool InvolvesPercentageComparisons() const final;
#endif

 protected:
  double ComputeDouble(const CSSLengthResolver& length_resolver) const final;

 private:
  static const CSSMathExpressionNode* GetNumericLiteralSide(
      const CSSMathExpressionNode* left_side,
      const CSSMathExpressionNode* right_side);

  double Evaluate(const Vector<double>& operands) const {
    return EvaluateOperator(operands, operator_);
  }

  static double EvaluateOperator(const Vector<double>& operands,
                                 CSSMathOperator op);

  // Helper for iterating from the 2nd to the last operands
  base::span<const Member<const CSSMathExpressionNode>> SecondToLastOperands()
      const {
    return base::make_span(std::next(operands_.begin()), operands_.end());
  }

  Operands operands_;
  const CSSMathOperator operator_;
};

template <>
struct DowncastTraits<CSSMathExpressionOperation> {
  static bool AllowFrom(const CSSMathExpressionNode& node) {
    return node.IsOperation();
  }
};

// anchor() and anchor-size()
class CORE_EXPORT CSSMathExpressionAnchorQuery final
    : public CSSMathExpressionNode {
 public:
  CSSMathExpressionAnchorQuery(CSSAnchorQueryType type,
                               const CSSValue* anchor_specifier,
                               const CSSValue& value,
                               const CSSPrimitiveValue* fallback);

  CSSMathExpressionNode* Copy() const final {
    return MakeGarbageCollected<CSSMathExpressionAnchorQuery>(
        type_, anchor_specifier_, *value_, fallback_);
  }

  bool IsAnchor() const { return type_ == CSSAnchorQueryType::kAnchor; }
  bool IsAnchorSize() const { return type_ == CSSAnchorQueryType::kAnchorSize; }

  // TODO(crbug.com/1309178): This is not entirely correct, since "math
  // function" should refer to functions defined in [1]. We may need to clean up
  // the terminology in the code.
  // [1] https://drafts.csswg.org/css-values-4/#math
  bool IsMathFunction() const final { return true; }

  bool IsAnchorQuery() const final { return true; }
  bool IsZero() const final { return false; }
  CSSPrimitiveValue::UnitType ResolvedUnitType() const final {
    return CSSPrimitiveValue::UnitType::kUnknown;
  }
  absl::optional<double> ComputeValueInCanonicalUnit() const final {
    return absl::nullopt;
  }
  absl::optional<PixelsAndPercent> ToPixelsAndPercent(
      const CSSLengthResolver&) const final {
    return absl::nullopt;
  }
  bool AccumulateLengthArray(CSSLengthArray& length_array,
                             double multiplier) const final {
    return false;
  }
  bool IsComputationallyIndependent() const final { return false; }
  double DoubleValue() const final {
    // We can't resolve an anchor query until layout time.
    NOTREACHED();
    return 0;
  }
  double ComputeLengthPx(const CSSLengthResolver& length_resolver) const final {
    // We can't resolve an anchor query until layout time.
    NOTREACHED();
    return 0;
  }
  void AccumulateLengthUnitTypes(
      CSSPrimitiveValue::LengthTypeFlags& types) const final {
    // AccumulateLengthUnitTypes() is only used when interpolating the
    // 'transform' property, where anchor queries are not allowed.
    NOTREACHED();
    return;
  }

  String CustomCSSText() const final;
  scoped_refptr<const CalculationExpressionNode> ToCalculationExpression(
      const CSSLengthResolver&) const final;
  bool operator==(const CSSMathExpressionNode& other) const final;
  const CSSMathExpressionNode& PopulateWithTreeScope(
      const TreeScope*) const final;
  void Trace(Visitor* visitor) const final;

#if DCHECK_IS_ON()
  bool InvolvesPercentageComparisons() const final { return false; }
#endif

 protected:
  double ComputeDouble(const CSSLengthResolver& length_resolver) const final {
    // We can't resolve an anchor query until layout time.
    NOTREACHED();
    return 0;
  }

 private:
  CSSAnchorQueryType type_;
  Member<const CSSValue> anchor_specifier_;
  Member<const CSSValue> value_;
  Member<const CSSPrimitiveValue> fallback_;
};

template <>
struct DowncastTraits<CSSMathExpressionAnchorQuery> {
  static bool AllowFrom(const CSSMathExpressionNode& node) {
    return node.IsAnchorQuery();
  }
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_CORE_CSS_CSS_MATH_EXPRESSION_NODE_H_
