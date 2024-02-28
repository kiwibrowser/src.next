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

#include "third_party/blink/renderer/core/css/css_math_expression_node.h"

#include <algorithm>
#include <cfloat>
#include <numeric>

#include "base/memory/values_equivalent.h"
#include "third_party/blink/renderer/core/css/calculation_expression_anchor_query_node.h"
#include "third_party/blink/renderer/core/css/css_custom_ident_value.h"
#include "third_party/blink/renderer/core/css/css_math_operator.h"
#include "third_party/blink/renderer/core/css/css_numeric_literal_value.h"
#include "third_party/blink/renderer/core/css/css_primitive_value.h"
#include "third_party/blink/renderer/core/css/css_value_clamping_utils.h"
#include "third_party/blink/renderer/core/css/parser/css_parser_context.h"
#include "third_party/blink/renderer/core/css/properties/css_parsing_utils.h"
#include "third_party/blink/renderer/core/css/resolver/style_resolver.h"
#include "third_party/blink/renderer/core/css_value_keywords.h"
#include "third_party/blink/renderer/core/frame/web_feature.h"
#include "third_party/blink/renderer/core/style/anchor_specifier_value.h"
#include "third_party/blink/renderer/platform/geometry/calculation_expression_node.h"
#include "third_party/blink/renderer/platform/geometry/length.h"
#include "third_party/blink/renderer/platform/geometry/math_functions.h"
#include "third_party/blink/renderer/platform/heap/garbage_collected.h"
#include "third_party/blink/renderer/platform/runtime_enabled_features.h"
#include "third_party/blink/renderer/platform/wtf/allocator/allocator.h"
#include "third_party/blink/renderer/platform/wtf/math_extras.h"
#include "third_party/blink/renderer/platform/wtf/text/string_builder.h"
#include "ui/gfx/geometry/sin_cos_degrees.h"

namespace blink {

static CalculationResultCategory UnitCategory(
    CSSPrimitiveValue::UnitType type) {
  switch (type) {
    case CSSPrimitiveValue::UnitType::kNumber:
    case CSSPrimitiveValue::UnitType::kInteger:
      return kCalcNumber;
    case CSSPrimitiveValue::UnitType::kPercentage:
      return kCalcPercent;
    case CSSPrimitiveValue::UnitType::kEms:
    case CSSPrimitiveValue::UnitType::kExs:
    case CSSPrimitiveValue::UnitType::kPixels:
    case CSSPrimitiveValue::UnitType::kCentimeters:
    case CSSPrimitiveValue::UnitType::kMillimeters:
    case CSSPrimitiveValue::UnitType::kQuarterMillimeters:
    case CSSPrimitiveValue::UnitType::kInches:
    case CSSPrimitiveValue::UnitType::kPoints:
    case CSSPrimitiveValue::UnitType::kPicas:
    case CSSPrimitiveValue::UnitType::kUserUnits:
    case CSSPrimitiveValue::UnitType::kRems:
    case CSSPrimitiveValue::UnitType::kChs:
    case CSSPrimitiveValue::UnitType::kViewportWidth:
    case CSSPrimitiveValue::UnitType::kViewportHeight:
    case CSSPrimitiveValue::UnitType::kViewportMin:
    case CSSPrimitiveValue::UnitType::kViewportMax:
    case CSSPrimitiveValue::UnitType::kRexs:
    case CSSPrimitiveValue::UnitType::kRchs:
    case CSSPrimitiveValue::UnitType::kRics:
    case CSSPrimitiveValue::UnitType::kRlhs:
    case CSSPrimitiveValue::UnitType::kIcs:
    case CSSPrimitiveValue::UnitType::kLhs:
      return kCalcLength;
    case CSSPrimitiveValue::UnitType::kCaps:
    case CSSPrimitiveValue::UnitType::kRcaps:
      return RuntimeEnabledFeatures::CSSCapFontUnitsEnabled() ? kCalcLength
                                                              : kCalcOther;
    case CSSPrimitiveValue::UnitType::kViewportInlineSize:
    case CSSPrimitiveValue::UnitType::kViewportBlockSize:
    case CSSPrimitiveValue::UnitType::kSmallViewportWidth:
    case CSSPrimitiveValue::UnitType::kSmallViewportHeight:
    case CSSPrimitiveValue::UnitType::kSmallViewportInlineSize:
    case CSSPrimitiveValue::UnitType::kSmallViewportBlockSize:
    case CSSPrimitiveValue::UnitType::kSmallViewportMin:
    case CSSPrimitiveValue::UnitType::kSmallViewportMax:
    case CSSPrimitiveValue::UnitType::kLargeViewportWidth:
    case CSSPrimitiveValue::UnitType::kLargeViewportHeight:
    case CSSPrimitiveValue::UnitType::kLargeViewportInlineSize:
    case CSSPrimitiveValue::UnitType::kLargeViewportBlockSize:
    case CSSPrimitiveValue::UnitType::kLargeViewportMin:
    case CSSPrimitiveValue::UnitType::kLargeViewportMax:
    case CSSPrimitiveValue::UnitType::kDynamicViewportWidth:
    case CSSPrimitiveValue::UnitType::kDynamicViewportHeight:
    case CSSPrimitiveValue::UnitType::kDynamicViewportInlineSize:
    case CSSPrimitiveValue::UnitType::kDynamicViewportBlockSize:
    case CSSPrimitiveValue::UnitType::kDynamicViewportMin:
    case CSSPrimitiveValue::UnitType::kDynamicViewportMax:
      return RuntimeEnabledFeatures::CSSViewportUnits4Enabled() ? kCalcLength
                                                                : kCalcOther;
    case CSSPrimitiveValue::UnitType::kContainerWidth:
    case CSSPrimitiveValue::UnitType::kContainerHeight:
    case CSSPrimitiveValue::UnitType::kContainerInlineSize:
    case CSSPrimitiveValue::UnitType::kContainerBlockSize:
    case CSSPrimitiveValue::UnitType::kContainerMin:
    case CSSPrimitiveValue::UnitType::kContainerMax:
      return kCalcLength;
    case CSSPrimitiveValue::UnitType::kDegrees:
    case CSSPrimitiveValue::UnitType::kGradians:
    case CSSPrimitiveValue::UnitType::kRadians:
    case CSSPrimitiveValue::UnitType::kTurns:
      return kCalcAngle;
    case CSSPrimitiveValue::UnitType::kMilliseconds:
    case CSSPrimitiveValue::UnitType::kSeconds:
      return kCalcTime;
    case CSSPrimitiveValue::UnitType::kHertz:
    case CSSPrimitiveValue::UnitType::kKilohertz:
      return kCalcFrequency;

    // Resolution units
    case CSSPrimitiveValue::UnitType::kDotsPerPixel:
    case CSSPrimitiveValue::UnitType::kX:
    case CSSPrimitiveValue::UnitType::kDotsPerInch:
    case CSSPrimitiveValue::UnitType::kDotsPerCentimeter:
      return kCalcResolution;

    // Identifier
    case CSSPrimitiveValue::UnitType::kIdent:
      return kCalcIdent;

    default:
      return kCalcOther;
  }
}

static bool HasDoubleValue(CSSPrimitiveValue::UnitType type) {
  switch (type) {
    case CSSPrimitiveValue::UnitType::kNumber:
    case CSSPrimitiveValue::UnitType::kPercentage:
    case CSSPrimitiveValue::UnitType::kEms:
    case CSSPrimitiveValue::UnitType::kExs:
    case CSSPrimitiveValue::UnitType::kChs:
    case CSSPrimitiveValue::UnitType::kIcs:
    case CSSPrimitiveValue::UnitType::kLhs:
    case CSSPrimitiveValue::UnitType::kCaps:
    case CSSPrimitiveValue::UnitType::kRcaps:
    case CSSPrimitiveValue::UnitType::kRlhs:
    case CSSPrimitiveValue::UnitType::kRems:
    case CSSPrimitiveValue::UnitType::kRexs:
    case CSSPrimitiveValue::UnitType::kRchs:
    case CSSPrimitiveValue::UnitType::kRics:
    case CSSPrimitiveValue::UnitType::kPixels:
    case CSSPrimitiveValue::UnitType::kCentimeters:
    case CSSPrimitiveValue::UnitType::kMillimeters:
    case CSSPrimitiveValue::UnitType::kQuarterMillimeters:
    case CSSPrimitiveValue::UnitType::kInches:
    case CSSPrimitiveValue::UnitType::kPoints:
    case CSSPrimitiveValue::UnitType::kPicas:
    case CSSPrimitiveValue::UnitType::kUserUnits:
    case CSSPrimitiveValue::UnitType::kDegrees:
    case CSSPrimitiveValue::UnitType::kRadians:
    case CSSPrimitiveValue::UnitType::kGradians:
    case CSSPrimitiveValue::UnitType::kTurns:
    case CSSPrimitiveValue::UnitType::kMilliseconds:
    case CSSPrimitiveValue::UnitType::kSeconds:
    case CSSPrimitiveValue::UnitType::kHertz:
    case CSSPrimitiveValue::UnitType::kKilohertz:
    case CSSPrimitiveValue::UnitType::kViewportWidth:
    case CSSPrimitiveValue::UnitType::kViewportHeight:
    case CSSPrimitiveValue::UnitType::kViewportMin:
    case CSSPrimitiveValue::UnitType::kViewportMax:
    case CSSPrimitiveValue::UnitType::kContainerWidth:
    case CSSPrimitiveValue::UnitType::kContainerHeight:
    case CSSPrimitiveValue::UnitType::kContainerInlineSize:
    case CSSPrimitiveValue::UnitType::kContainerBlockSize:
    case CSSPrimitiveValue::UnitType::kContainerMin:
    case CSSPrimitiveValue::UnitType::kContainerMax:
    case CSSPrimitiveValue::UnitType::kDotsPerPixel:
    case CSSPrimitiveValue::UnitType::kX:
    case CSSPrimitiveValue::UnitType::kDotsPerInch:
    case CSSPrimitiveValue::UnitType::kDotsPerCentimeter:
    case CSSPrimitiveValue::UnitType::kFlex:
    case CSSPrimitiveValue::UnitType::kInteger:
      return true;
    default:
      return false;
  }
}

namespace {

double TanDegrees(double degrees) {
  // Use table values for tan() if possible.
  // We pick a pretty arbitrary limit that should be safe.
  if (degrees > -90000000.0 && degrees < 90000000.0) {
    // Make sure 0, 45, 90, 135, 180, 225 and 270 degrees get exact results.
    double n45degrees = degrees / 45.0;
    int octant = static_cast<int>(n45degrees);
    if (octant == n45degrees) {
      constexpr double kTanN45[] = {
          /* 0deg */ 0.0,
          /* 45deg */ 1.0,
          /* 90deg */ std::numeric_limits<double>::infinity(),
          /* 135deg */ -1.0,
          /* 180deg */ 0.0,
          /* 225deg */ 1.0,
          /* 270deg */ -std::numeric_limits<double>::infinity(),
          /* 315deg */ -1.0,
      };
      return kTanN45[octant & 7];
    }
  }
  // Slow path for non-table cases.
  double x = Deg2rad(degrees);
  return std::tan(x);
}

const PixelsAndPercent CreateClampedSamePixelsAndPercent(float value) {
  return PixelsAndPercent(CSSValueClampingUtils::ClampLength(value),
                          CSSValueClampingUtils::ClampLength(value),
                          /*has_explicit_pixels=*/true,
                          /*has_explicit_percent=*/true);
}

bool IsNaN(PixelsAndPercent value, bool allows_negative_percentage_reference) {
  if (std::isnan(value.pixels + value.percent) ||
      (allows_negative_percentage_reference && std::isinf(value.percent))) {
    return true;
  }
  return false;
}

absl::optional<PixelsAndPercent> EvaluateValueIfNaNorInfinity(
    scoped_refptr<const blink::CalculationExpressionNode> value,
    bool allows_negative_percentage_reference) {
  // |anchor_evaluator| is not needed because this function is just for handling
  // inf and NaN.
  float evaluated_value = value->Evaluate(1, /* anchor_evaluator */ nullptr);
  if (!std::isfinite(evaluated_value)) {
    return CreateClampedSamePixelsAndPercent(evaluated_value);
  }
  if (allows_negative_percentage_reference) {
    evaluated_value = value->Evaluate(-1, /* anchor_evaluator */ nullptr);
    if (!std::isfinite(evaluated_value)) {
      return CreateClampedSamePixelsAndPercent(evaluated_value);
    }
  }
  return absl::nullopt;
}

bool CanEagerlySimplify(const CSSMathExpressionNode* operand) {
  if (operand->IsOperation()) {
    return false;
  }

  switch (operand->Category()) {
    case CalculationResultCategory::kCalcNumber:
    case CalculationResultCategory::kCalcAngle:
    case CalculationResultCategory::kCalcTime:
    case CalculationResultCategory::kCalcFrequency:
    case CalculationResultCategory::kCalcResolution:
      return true;
    case CalculationResultCategory::kCalcLength:
      return !CSSPrimitiveValue::IsRelativeUnit(operand->ResolvedUnitType());
    default:
      return false;
  }
}

bool CanEagerlySimplify(const CSSMathExpressionOperation::Operands& operands) {
  for (const CSSMathExpressionNode* operand : operands) {
    if (!CanEagerlySimplify(operand)) {
      return false;
    }
  }
  return true;
}

enum class ProgressArgsSimplificationStatus {
  kAllArgsResolveToCanonical,
  kAllArgsHaveSameType,
  kCanNotSimplify,
};

// Either all the arguments are numerics and have the same unit type (e.g.
// progress(1em from 0em to 1em)), or they are all numerics and can be resolved
// to the canonical unit (e.g. progress(1deg from 0rad to 1deg)). Note: this
// can't be eagerly simplified - progress(1em from 0px to 1em).
ProgressArgsSimplificationStatus CanEagerlySimplifyProgressArgs(
    const CSSMathExpressionOperation::Operands& operands) {
  if (std::all_of(operands.begin(), operands.end(),
                  [](const CSSMathExpressionNode* node) {
                    return node->IsNumericLiteral() &&
                           node->ComputeValueInCanonicalUnit().has_value();
                  })) {
    return ProgressArgsSimplificationStatus::kAllArgsResolveToCanonical;
  }
  if (std::all_of(operands.begin(), operands.end(),
                  [&](const CSSMathExpressionNode* node) {
                    return node->IsNumericLiteral() &&
                           node->ResolvedUnitType() ==
                               operands.front()->ResolvedUnitType();
                  })) {
    return ProgressArgsSimplificationStatus::kAllArgsHaveSameType;
  }
  return ProgressArgsSimplificationStatus::kCanNotSimplify;
}

using UnitsHashMap = HashMap<CSSPrimitiveValue::UnitType, double>;
struct CSSMathExpressionNodeWithOperator {
  DISALLOW_NEW();

 public:
  CSSMathOperator op;
  Member<const CSSMathExpressionNode> node;

  CSSMathExpressionNodeWithOperator(CSSMathOperator op,
                                    const CSSMathExpressionNode* node)
      : op(op), node(node) {}

  void Trace(Visitor* visitor) const { visitor->Trace(node); }
};
using UnitsVector = HeapVector<CSSMathExpressionNodeWithOperator>;
using UnitsVectorHashMap =
    HeapHashMap<CSSPrimitiveValue::UnitType, Member<UnitsVector>>;

bool IsNumericNodeWithDoubleValue(const CSSMathExpressionNode* node) {
  return node->IsNumericLiteral() && HasDoubleValue(node->ResolvedUnitType());
}

const CSSMathExpressionNode* MaybeNegateFirstNode(
    CSSMathOperator op,
    const CSSMathExpressionNode* node) {
  // If first node's operator is -, negate the value.
  if (IsNumericNodeWithDoubleValue(node) && op == CSSMathOperator::kSubtract) {
    return CSSMathExpressionNumericLiteral::Create(-node->DoubleValue(),
                                                   node->ResolvedUnitType());
  }
  return node;
}

CSSMathOperator MaybeChangeOperatorSignIfNesting(bool is_in_nesting,
                                                 CSSMathOperator outer_op,
                                                 CSSMathOperator current_op) {
  // For the cases like "a - (b + c)" we need to turn + c into - c.
  if (is_in_nesting && outer_op == CSSMathOperator::kSubtract &&
      current_op == CSSMathOperator::kAdd) {
    return CSSMathOperator::kSubtract;
  }
  // For the cases like "a - (b - c)" we need to turn - c into + c.
  if (is_in_nesting && outer_op == CSSMathOperator::kSubtract &&
      current_op == CSSMathOperator::kSubtract) {
    return CSSMathOperator::kAdd;
  }
  // No need to change the sign.
  return current_op;
}

CSSMathExpressionNodeWithOperator MaybeReplaceNodeWithCombined(
    const CSSMathExpressionNode* node,
    CSSMathOperator op,
    const UnitsHashMap& units_map) {
  if (!node->IsNumericLiteral()) {
    return {op, node};
  }
  CSSPrimitiveValue::UnitType unit_type = node->ResolvedUnitType();
  auto it = units_map.find(unit_type);
  if (it != units_map.end()) {
    double value = it->value;
    CSSMathOperator new_op =
        value < 0.0f ? CSSMathOperator::kSubtract : CSSMathOperator::kAdd;
    CSSMathExpressionNode* new_node =
        CSSMathExpressionNumericLiteral::Create(std::abs(value), unit_type);
    return {new_op, new_node};
  }
  return {op, node};
}

// This function combines numeric values that have double value and are of the
// same unit type together in numeric_children and saves all the non add/sub
// operation children and their correct simplified operator in all_children.
void CombineNumericChildrenFromNode(const CSSMathExpressionNode* root,
                                    CSSMathOperator op,
                                    UnitsHashMap& numeric_children,
                                    UnitsVector& all_children,
                                    bool is_in_nesting = false) {
  const CSSPrimitiveValue::UnitType unit_type = root->ResolvedUnitType();
  // Go deeper inside the operation node if possible.
  if (auto* operation = DynamicTo<CSSMathExpressionOperation>(root);
      operation && operation->IsAddOrSubtract()) {
    const CSSMathOperator operation_op = operation->OperatorType();
    is_in_nesting |= operation->IsNestedCalc();
    // Nest from the left (first op) to the right (second op).
    CombineNumericChildrenFromNode(operation->GetOperands().front(), op,
                                   numeric_children, all_children,
                                   is_in_nesting);
    // Change the sign of expression, if we are nesting (inside brackets).
    op = MaybeChangeOperatorSignIfNesting(is_in_nesting, op, operation_op);
    CombineNumericChildrenFromNode(operation->GetOperands().back(), op,
                                   numeric_children, all_children,
                                   is_in_nesting);
    return;
  }
  // If we have numeric with double value - combine under one unit type.
  if (IsNumericNodeWithDoubleValue(root)) {
    double value = op == CSSMathOperator::kAdd ? root->DoubleValue()
                                               : -root->DoubleValue();
    if (auto it = numeric_children.find(unit_type);
        it != numeric_children.end()) {
      it->value += value;
    } else {
      numeric_children.insert(unit_type, value);
    }
  }
  // Save all non add/sub operations.
  all_children.emplace_back(op, root);
}

// This function collects numeric values that have double value
// in the numeric_children vector under the same type and saves all the complex
// children and their correct simplified operator in complex_children.
void CollectNumericChildrenFromNode(const CSSMathExpressionNode* root,
                                    CSSMathOperator op,
                                    UnitsVectorHashMap& numeric_children,
                                    UnitsVector& complex_children,
                                    bool is_in_nesting = false) {
  // Go deeper inside the operation node if possible.
  if (auto* operation = DynamicTo<CSSMathExpressionOperation>(root);
      operation && operation->IsAddOrSubtract()) {
    const CSSMathOperator operation_op = operation->OperatorType();
    is_in_nesting |= operation->IsNestedCalc();
    // Nest from the left (first op) to the right (second op).
    CollectNumericChildrenFromNode(operation->GetOperands().front(), op,
                                   numeric_children, complex_children,
                                   is_in_nesting);
    // Change the sign of expression, if we are nesting (inside brackets).
    op = MaybeChangeOperatorSignIfNesting(is_in_nesting, op, operation_op);
    CollectNumericChildrenFromNode(operation->GetOperands().back(), op,
                                   numeric_children, complex_children,
                                   is_in_nesting);
    return;
  }
  CSSPrimitiveValue::UnitType unit_type = root->ResolvedUnitType();
  // If we have numeric with double value - collect in numeric_children.
  if (IsNumericNodeWithDoubleValue(root)) {
    if (auto it = numeric_children.find(unit_type);
        it != numeric_children.end()) {
      it->value->emplace_back(op, root);
    } else {
      numeric_children.insert(
          unit_type, MakeGarbageCollected<UnitsVector>(
                         1, CSSMathExpressionNodeWithOperator(op, root)));
    }
    return;
  }
  // Save all non add/sub operations.
  complex_children.emplace_back(op, root);
}

CSSMathExpressionNode* AddNodeToSumNode(CSSMathExpressionNode* sum_node,
                                        const CSSMathExpressionNode* node,
                                        CSSMathOperator op) {
  // If the sum node is nullptr, create and return the numeric literal node.
  if (!sum_node) {
    return MaybeNegateFirstNode(op, node)->Copy();
  }
  // If the node is numeric with double values,
  // add the numeric literal node with |value| and
  // operator to match the value's sign.
  if (IsNumericNodeWithDoubleValue(node)) {
    double value = node->DoubleValue();
    CSSMathExpressionNode* new_node = CSSMathExpressionNumericLiteral::Create(
        std::abs(value), node->ResolvedUnitType());
    // Change the operator correctly.
    if (value < 0.0f && op == CSSMathOperator::kAdd) {
      // + -10 -> -10
      op = CSSMathOperator::kSubtract;
    } else if (value < 0.0f && op == CSSMathOperator::kSubtract) {
      // - -10 -> + 10.
      op = CSSMathOperator::kAdd;
    }
    return MakeGarbageCollected<CSSMathExpressionOperation>(
        sum_node, new_node, op, sum_node->Category());
  }
  // Add the node to the sum_node otherwise.
  return MakeGarbageCollected<CSSMathExpressionOperation>(sum_node, node, op,
                                                          sum_node->Category());
}

CSSMathExpressionNode* AddNodesVectorToSumNode(CSSMathExpressionNode* sum_node,
                                               const UnitsVector& vector) {
  for (const auto& [op, node] : vector) {
    sum_node = AddNodeToSumNode(sum_node, node, op);
  }
  return sum_node;
}

// This function follows:
// https://drafts.csswg.org/css-values-4/#sort-a-calculations-children
// As in Blink the math expression tree is binary, we need to collect all the
// elements of this tree together and create a new tree as a result.
CSSMathExpressionNode* MaybeSortSumNode(
    const CSSMathExpressionOperation* root) {
  CHECK(root->IsAddOrSubtract());
  CHECK_EQ(root->GetOperands().size(), 2u);
  // Hash map of vectors of numeric literal values with double value with the
  // same unit type.
  UnitsVectorHashMap numeric_children;
  // Vector of all non add/sub operation children.
  UnitsVector complex_children;
  // Collect all the numeric literal with double value in one vector.
  // Note: using kAdd here as the operator for the first child
  // (e.g. a - b = +a - b, a + b = +a + b)
  CollectNumericChildrenFromNode(root, CSSMathOperator::kAdd, numeric_children,
                                 complex_children, false);
  // Form the final node.
  CSSMathExpressionNode* final_node = nullptr;
  // From spec: If nodes contains a number, remove it from nodes and append it
  // to ret.
  if (auto it = numeric_children.find(CSSPrimitiveValue::UnitType::kNumber);
      it != numeric_children.end()) {
    final_node = AddNodesVectorToSumNode(final_node, *it->value);
    numeric_children.erase(it);
  }
  // From spec: If nodes contains a percentage, remove it from nodes and append
  // it to ret.
  if (auto it = numeric_children.find(CSSPrimitiveValue::UnitType::kPercentage);
      it != numeric_children.end()) {
    final_node = AddNodesVectorToSumNode(final_node, *it->value);
    numeric_children.erase(it);
  }
  // Now, sort the rest numeric values alphabatically.
  // From spec: If nodes contains any dimensions, remove them from nodes, sort
  // them by their units, ordered ASCII case-insensitively, and append them to
  // ret.
  auto comp = [&](const CSSPrimitiveValue::UnitType& key_a,
                  const CSSPrimitiveValue::UnitType& key_b) {
    return strcmp(CSSPrimitiveValue::UnitTypeToString(key_a),
                  CSSPrimitiveValue::UnitTypeToString(key_b)) < 0;
  };
  Vector<CSSPrimitiveValue::UnitType> keys;
  keys.reserve(numeric_children.size());
  for (const CSSPrimitiveValue::UnitType& key : numeric_children.Keys()) {
    keys.push_back(key);
  }
  std::sort(keys.begin(), keys.end(), comp);
  // Now, add those numeric nodes in the sorted order.
  for (const auto& unit_type : keys) {
    final_node =
        AddNodesVectorToSumNode(final_node, *numeric_children.at(unit_type));
  }
  // Now, add all the complex (non-numerics with double value) values.
  final_node = AddNodesVectorToSumNode(final_node, complex_children);
  return final_node;
}

// This function follows:
// https://drafts.csswg.org/css-values-4/#calc-simplification
// As in Blink the math expression tree is binary, we need to collect all the
// elements of this tree together and create a new tree as a result.
CSSMathExpressionNode* MaybeSimplifySumNode(
    const CSSMathExpressionOperation* root) {
  CHECK(root->IsAddOrSubtract());
  CHECK_EQ(root->GetOperands().size(), 2u);
  // Hash map of numeric literal values of the same type, that can be
  // combined together.
  UnitsHashMap numeric_children;
  // Vector of all non add/sub operation children.
  UnitsVector all_children;
  // Collect all the numeric literal values together.
  // Note: using kAdd here as the operator for the first child
  // (e.g. a - b = +a - b, a + b = +a + b)
  CombineNumericChildrenFromNode(root, CSSMathOperator::kAdd, numeric_children,
                                 all_children);
  // Form the final node.
  HashSet<CSSPrimitiveValue::UnitType> used_units;
  CSSMathExpressionNode* final_node = nullptr;
  for (const auto& child : all_children) {
    auto [op, node] =
        MaybeReplaceNodeWithCombined(child.node, child.op, numeric_children);
    CSSPrimitiveValue::UnitType unit_type = node->ResolvedUnitType();
    // Skip already used unit types, as they have been already combined.
    if (IsNumericNodeWithDoubleValue(node)) {
      if (used_units.Contains(unit_type)) {
        continue;
      }
      used_units.insert(unit_type);
    }
    if (!final_node) {
      // First child.
      final_node = MaybeNegateFirstNode(op, node)->Copy();
      continue;
    }
    final_node = MakeGarbageCollected<CSSMathExpressionOperation>(
        final_node, node, op, root->Category());
  }
  return final_node;
}

CSSMathExpressionNode* MaybeDistributeArithmeticOperation(
    const CSSMathExpressionNode* left_side,
    const CSSMathExpressionNode* right_side,
    CSSMathOperator op) {
  if (op != CSSMathOperator::kMultiply && op != CSSMathOperator::kDivide) {
    return nullptr;
  }
  // NOTE: we should not simplify num * (fn + fn), all the operands inside
  // the sum should be numeric.
  // Case (Op1 + Op2) * Num.
  auto* left_operation = DynamicTo<CSSMathExpressionOperation>(left_side);
  auto* right_numeric = DynamicTo<CSSMathExpressionNumericLiteral>(right_side);
  if (left_operation && left_operation->IsAddOrSubtract() &&
      left_operation->AllOperandsAreNumeric() && right_numeric &&
      right_numeric->Category() == CalculationResultCategory::kCalcNumber) {
    auto* new_left_side =
        CSSMathExpressionOperation::CreateArithmeticOperationSimplified(
            left_operation->GetOperands().front(), right_side, op);
    auto* new_right_side =
        CSSMathExpressionOperation::CreateArithmeticOperationSimplified(
            left_operation->GetOperands().back(), right_side, op);
    CSSMathExpressionNode* operation =
        CSSMathExpressionOperation::CreateArithmeticOperationSimplified(
            new_left_side, new_right_side, left_operation->OperatorType());
    // Note: setting SetIsNestedCalc is needed, as we can be in this situation:
    // A - B * (C + D)
    //     /\/\/\/\/\ - we are B * (C + D)
    // and we don't know about the -, as it's another operation,
    // so make the simplified operation nested to end up with:
    // A - (B * C + B * D).
    operation->SetIsNestedCalc();
    return operation;
  }
  // Case Num * (Op1 + Op2). But don't do num / (Op1 + Op2), as it can invert
  // the type.
  auto* right_operation = DynamicTo<CSSMathExpressionOperation>(right_side);
  auto* left_numeric = DynamicTo<CSSMathExpressionNumericLiteral>(left_side);
  if (right_operation && right_operation->IsAddOrSubtract() &&
      right_operation->AllOperandsAreNumeric() && left_numeric &&
      left_numeric->Category() == CalculationResultCategory::kCalcNumber &&
      op != CSSMathOperator::kDivide) {
    auto* new_right_side =
        CSSMathExpressionOperation::CreateArithmeticOperationSimplified(
            left_side, right_operation->GetOperands().front(), op);
    auto* new_left_side =
        CSSMathExpressionOperation::CreateArithmeticOperationSimplified(
            left_side, right_operation->GetOperands().back(), op);
    CSSMathExpressionNode* operation =
        CSSMathExpressionOperation::CreateArithmeticOperationSimplified(
            new_right_side, new_left_side, right_operation->OperatorType());
    // Note: setting SetIsNestedCalc is needed, as we can be in this situation:
    // A - (C + D) * B
    //     /\/\/\/\/\ - we are (C + D) * B
    // and we don't know about the -, as it's another operation,
    // so make the simplified operation nested to end up with:
    // A - (B * C + B * D).
    operation->SetIsNestedCalc();
    return operation;
  }
  return nullptr;
}

}  // namespace

// ------ Start of CSSMathExpressionNumericLiteral member functions ------

// static
CSSMathExpressionNumericLiteral* CSSMathExpressionNumericLiteral::Create(
    const CSSNumericLiteralValue* value) {
  return MakeGarbageCollected<CSSMathExpressionNumericLiteral>(value);
}

// static
CSSMathExpressionNumericLiteral* CSSMathExpressionNumericLiteral::Create(
    double value,
    CSSPrimitiveValue::UnitType type) {
  return MakeGarbageCollected<CSSMathExpressionNumericLiteral>(
      CSSNumericLiteralValue::Create(value, type));
}

CSSMathExpressionNumericLiteral::CSSMathExpressionNumericLiteral(
    const CSSNumericLiteralValue* value)
    : CSSMathExpressionNode(
          UnitCategory(value->GetType()),
          false /* has_comparisons*/,
          false /* needs_tree_scope_population*/),
      value_(value) {
  if (!value_->IsNumber() && CanEagerlySimplify(this)) {
    // "If root is a dimension that is not expressed in its canonical unit, and
    // there is enough information available to convert it to the canonical
    // unit, do so, and return the value."
    // https://w3c.github.io/csswg-drafts/css-values/#calc-simplification
    //
    // However, Numbers should not be eagerly simplified here since that would
    // result in converting Integers to Doubles (kNumber, canonical unit for
    // Numbers).

    value_ = value_->CreateCanonicalUnitValue();
  }
}

bool CSSMathExpressionNumericLiteral::IsZero() const {
  return !value_->GetDoubleValue();
}

String CSSMathExpressionNumericLiteral::CustomCSSText() const {
  return value_->CssText();
}

absl::optional<PixelsAndPercent>
CSSMathExpressionNumericLiteral::ToPixelsAndPercent(
    const CSSLengthResolver& length_resolver) const {
  switch (category_) {
    case kCalcLength:
      return PixelsAndPercent(value_->ComputeLengthPx(length_resolver), 0.0f,
                              /*has_explicit_pixels=*/true,
                              /*has_explicit_percent=*/false);
    case kCalcPercent:
      DCHECK(value_->IsPercentage());
      return PixelsAndPercent(0.0f, value_->GetDoubleValueWithoutClamping(),
                              /*has_explicit_pixels=*/false,
                              /*has_explicit_percent=*/true);
    case kCalcNumber:
      // TODO(alancutter): Stop treating numbers like pixels unconditionally
      // in calcs to be able to accomodate border-image-width
      // https://drafts.csswg.org/css-backgrounds-3/#the-border-image-width
      return PixelsAndPercent(value_->GetFloatValue() * length_resolver.Zoom(),
                              0.0f, /*has_explicit_pixels=*/true,
                              /*has_explicit_percent=*/false);
    default:
      NOTREACHED();
      return {};
  }
}

scoped_refptr<const CalculationExpressionNode>
CSSMathExpressionNumericLiteral::ToCalculationExpression(
    const CSSLengthResolver& length_resolver) const {
  if (Category() == kCalcNumber) {
    return base::MakeRefCounted<CalculationExpressionNumberNode>(
        value_->DoubleValue());
  }
  return base::MakeRefCounted<CalculationExpressionPixelsAndPercentNode>(
      *ToPixelsAndPercent(length_resolver));
}

double CSSMathExpressionNumericLiteral::DoubleValue() const {
  if (HasDoubleValue(ResolvedUnitType())) {
    return value_->GetDoubleValueWithoutClamping();
  }
  NOTREACHED();
  return 0;
}

absl::optional<double>
CSSMathExpressionNumericLiteral::ComputeValueInCanonicalUnit() const {
  switch (category_) {
    case kCalcNumber:
    case kCalcPercent:
      return value_->DoubleValue();
    case kCalcLength:
      if (CSSPrimitiveValue::IsRelativeUnit(value_->GetType())) {
        return absl::nullopt;
      }
      [[fallthrough]];
    case kCalcAngle:
    case kCalcTime:
    case kCalcFrequency:
    case kCalcResolution:
      return value_->DoubleValue() *
             CSSPrimitiveValue::ConversionToCanonicalUnitsScaleFactor(
                 value_->GetType());
    default:
      return absl::nullopt;
  }
}

double CSSMathExpressionNumericLiteral::ComputeDouble(
    const CSSLengthResolver& length_resolver) const {
  switch (category_) {
    case kCalcLength:
      return value_->ComputeLengthPx(length_resolver);
    case kCalcNumber:
      return value_->DoubleValue();
    case kCalcAngle:
      return value_->ComputeDegrees();
    case kCalcTime:
      return value_->ComputeSeconds();
    case kCalcResolution:
      return value_->ComputeDotsPerPixel();
    case kCalcFrequency:
      return value_->ComputeInCanonicalUnit();
    case kCalcPercentLength:
    case kCalcPercent:
    case kCalcOther:
    case kCalcIdent:
      NOTREACHED();
      break;
  }
  NOTREACHED();
  return 0;
}

double CSSMathExpressionNumericLiteral::ComputeLengthPx(
    const CSSLengthResolver& length_resolver) const {
  switch (category_) {
    case kCalcLength:
      return value_->ComputeLengthPx(length_resolver);
    case kCalcNumber:
    case kCalcPercent:
    case kCalcAngle:
    case kCalcFrequency:
    case kCalcPercentLength:
    case kCalcTime:
    case kCalcResolution:
    case kCalcOther:
    case kCalcIdent:
      NOTREACHED();
      break;
  }
  NOTREACHED();
  return 0;
}

bool CSSMathExpressionNumericLiteral::AccumulateLengthArray(
    CSSLengthArray& length_array,
    double multiplier) const {
  DCHECK_NE(Category(), kCalcNumber);
  return value_->AccumulateLengthArray(length_array, multiplier);
}

void CSSMathExpressionNumericLiteral::AccumulateLengthUnitTypes(
    CSSPrimitiveValue::LengthTypeFlags& types) const {
  value_->AccumulateLengthUnitTypes(types);
}

bool CSSMathExpressionNumericLiteral::operator==(
    const CSSMathExpressionNode& other) const {
  if (!other.IsNumericLiteral()) {
    return false;
  }

  return base::ValuesEquivalent(
      value_, To<CSSMathExpressionNumericLiteral>(other).value_);
}

CSSPrimitiveValue::UnitType CSSMathExpressionNumericLiteral::ResolvedUnitType()
    const {
  return value_->GetType();
}

bool CSSMathExpressionNumericLiteral::IsComputationallyIndependent() const {
  return value_->IsComputationallyIndependent();
}

void CSSMathExpressionNumericLiteral::Trace(Visitor* visitor) const {
  visitor->Trace(value_);
  CSSMathExpressionNode::Trace(visitor);
}

#if DCHECK_IS_ON()
bool CSSMathExpressionNumericLiteral::InvolvesPercentageComparisons() const {
  return false;
}
#endif

// ------ End of CSSMathExpressionNumericLiteral member functions

static const CalculationResultCategory
    kAddSubtractResult[kCalcOther][kCalcOther] = {
        /* CalcNumber */ {kCalcNumber, kCalcOther, kCalcOther, kCalcOther,
                          kCalcOther, kCalcOther, kCalcOther, kCalcOther,
                          kCalcOther},
        /* CalcLength */
        {kCalcOther, kCalcLength, kCalcPercentLength, kCalcPercentLength,
         kCalcOther, kCalcOther, kCalcOther, kCalcOther, kCalcOther},
        /* CalcPercent */
        {kCalcOther, kCalcPercentLength, kCalcPercent, kCalcPercentLength,
         kCalcOther, kCalcOther, kCalcOther, kCalcOther, kCalcOther},
        /* CalcPercentLength */
        {kCalcOther, kCalcPercentLength, kCalcPercentLength, kCalcPercentLength,
         kCalcOther, kCalcOther, kCalcOther, kCalcOther, kCalcOther},
        /* CalcAngle  */
        {kCalcOther, kCalcOther, kCalcOther, kCalcOther, kCalcAngle, kCalcOther,
         kCalcOther, kCalcOther, kCalcOther},
        /* CalcTime */
        {kCalcOther, kCalcOther, kCalcOther, kCalcOther, kCalcOther, kCalcTime,
         kCalcOther, kCalcOther, kCalcOther},
        /* CalcFrequency */
        {kCalcOther, kCalcOther, kCalcOther, kCalcOther, kCalcOther, kCalcOther,
         kCalcFrequency, kCalcOther, kCalcOther},
        /* CalcResolution */
        {kCalcOther, kCalcOther, kCalcOther, kCalcOther, kCalcOther, kCalcOther,
         kCalcOther, kCalcResolution, kCalcOther},
        /* CalcIdent */
        {kCalcOther, kCalcOther, kCalcOther, kCalcOther, kCalcOther, kCalcOther,
         kCalcOther, kCalcOther, kCalcOther},
};

static CalculationResultCategory DetermineCategory(
    const CSSMathExpressionNode& left_side,
    const CSSMathExpressionNode& right_side,
    CSSMathOperator op) {
  CalculationResultCategory left_category = left_side.Category();
  CalculationResultCategory right_category = right_side.Category();

  if (left_category == kCalcOther || right_category == kCalcOther) {
    return kCalcOther;
  }

  switch (op) {
    case CSSMathOperator::kAdd:
    case CSSMathOperator::kSubtract:
      return kAddSubtractResult[left_category][right_category];
    case CSSMathOperator::kMultiply:
      if (left_category != kCalcNumber && right_category != kCalcNumber) {
        return kCalcOther;
      }
      return left_category == kCalcNumber ? right_category : left_category;
    case CSSMathOperator::kDivide:
      if (right_category != kCalcNumber) {
        return kCalcOther;
      }
      return left_category;
    default:
      break;
  }

  NOTREACHED();
  return kCalcOther;
}

static CalculationResultCategory DetermineComparisonCategory(
    const CSSMathExpressionOperation::Operands& operands) {
  DCHECK(!operands.empty());

  bool is_first = true;
  CalculationResultCategory category = kCalcOther;
  for (const CSSMathExpressionNode* operand : operands) {
    if (is_first) {
      category = operand->Category();
    } else {
      category = kAddSubtractResult[category][operand->Category()];
    }

    is_first = false;
    if (category == kCalcOther) {
      break;
    }
  }

  return category;
}

// ------ Start of CSSMathExpressionIdentifierLiteral member functions -

CSSMathExpressionIdentifierLiteral::CSSMathExpressionIdentifierLiteral(
    AtomicString identifier)
    : CSSMathExpressionNode(UnitCategory(CSSPrimitiveValue::UnitType::kIdent),
                            false /* has_comparisons*/,
                            false /* needs_tree_scope_population*/),
      identifier_(std::move(identifier)) {}

scoped_refptr<const CalculationExpressionNode>
CSSMathExpressionIdentifierLiteral::ToCalculationExpression(
    const CSSLengthResolver&) const {
  return base::MakeRefCounted<CalculationExpressionIdentifierNode>(identifier_);
}

// ------ End of CSSMathExpressionIdentifierLiteral member functions ----

// ------ Start of CSSMathExpressionOperation member functions ------

bool CSSMathExpressionOperation::AllOperandsAreNumeric() const {
  return std::all_of(
      operands_.begin(), operands_.end(),
      [](const CSSMathExpressionNode* op) { return op->IsNumericLiteral(); });
}

// static
CSSMathExpressionNode* CSSMathExpressionOperation::CreateArithmeticOperation(
    const CSSMathExpressionNode* left_side,
    const CSSMathExpressionNode* right_side,
    CSSMathOperator op) {
  DCHECK_NE(left_side->Category(), kCalcOther);
  DCHECK_NE(right_side->Category(), kCalcOther);

  CalculationResultCategory new_category =
      DetermineCategory(*left_side, *right_side, op);
  if (new_category == kCalcOther) {
    return nullptr;
  }

  return MakeGarbageCollected<CSSMathExpressionOperation>(left_side, right_side,
                                                          op, new_category);
}

// static
CSSMathExpressionNode* CSSMathExpressionOperation::CreateComparisonFunction(
    Operands&& operands,
    CSSMathOperator op) {
  DCHECK(op == CSSMathOperator::kMin || op == CSSMathOperator::kMax ||
         op == CSSMathOperator::kClamp);

  CalculationResultCategory category = DetermineComparisonCategory(operands);
  if (category == kCalcOther) {
    return nullptr;
  }

  return MakeGarbageCollected<CSSMathExpressionOperation>(
      category, std::move(operands), op);
}

// static
CSSMathExpressionNode*
CSSMathExpressionOperation::CreateComparisonFunctionSimplified(
    Operands&& operands,
    CSSMathOperator op) {
  DCHECK(op == CSSMathOperator::kMin || op == CSSMathOperator::kMax ||
         op == CSSMathOperator::kClamp);

  CalculationResultCategory category = DetermineComparisonCategory(operands);
  if (category == kCalcOther) {
    return nullptr;
  }

  if (CanEagerlySimplify(operands)) {
    Vector<double> canonical_values;
    canonical_values.reserve(operands.size());
    for (const CSSMathExpressionNode* operand : operands) {
      absl::optional<double> canonical_value =
          operand->ComputeValueInCanonicalUnit();

      DCHECK(canonical_value.has_value());

      canonical_values.push_back(canonical_value.value());
    }

    CSSPrimitiveValue::UnitType canonical_unit =
        CSSPrimitiveValue::CanonicalUnit(operands.front()->ResolvedUnitType());

    return CSSMathExpressionNumericLiteral::Create(
        EvaluateOperator(canonical_values, op), canonical_unit);
  }

  if (operands.size() == 1) {
    return operands.front()->Copy();
  }

  return MakeGarbageCollected<CSSMathExpressionOperation>(
      category, std::move(operands), op);
}

// Helper function for parsing number value
static double ValueAsNumber(const CSSMathExpressionNode* node, bool& error) {
  if (node->Category() == kCalcNumber) {
    return node->DoubleValue();
  }
  error = true;
  return 0;
}

static bool SupportedCategoryForAtan2(
    const CalculationResultCategory category) {
  switch (category) {
    case kCalcNumber:
    case kCalcLength:
    case kCalcPercent:
    case kCalcTime:
    case kCalcFrequency:
    case kCalcAngle:
      return true;
    default:
      return false;
  }
}

static bool IsRelativeLength(CSSPrimitiveValue::UnitType type) {
  return CSSPrimitiveValue::IsRelativeUnit(type) &&
         CSSPrimitiveValue::IsLength(type);
}

static double ResolveAtan2(const CSSMathExpressionNode* y_node,
                           const CSSMathExpressionNode* x_node,
                           bool& error) {
  const CalculationResultCategory category = y_node->Category();
  if (category != x_node->Category() || !SupportedCategoryForAtan2(category)) {
    error = true;
    return 0;
  }
  CSSPrimitiveValue::UnitType y_type = y_node->ResolvedUnitType();
  CSSPrimitiveValue::UnitType x_type = x_node->ResolvedUnitType();

  // TODO(crbug.com/1392594): We ignore parameters in complex relative units
  // (e.g., 1rem + 1px) until they can be supported.
  if (y_type == CSSPrimitiveValue::UnitType::kUnknown ||
      x_type == CSSPrimitiveValue::UnitType::kUnknown) {
    error = true;
    return 0;
  }

  if (IsRelativeLength(y_type) || IsRelativeLength(x_type)) {
    // TODO(crbug.com/1392594): Relative length units are currently hard
    // to resolve. We ignore the units for now, so that
    // we can at least support the case where both operands have the same unit.
    double y = y_node->DoubleValue();
    double x = x_node->DoubleValue();
    return std::atan2(y, x);
  }
  auto y = y_node->ComputeValueInCanonicalUnit();
  auto x = x_node->ComputeValueInCanonicalUnit();
  return std::atan2(y.value(), x.value());
}

// Helper function for parsing trigonometric functions' parameter
static double ValueAsDegrees(const CSSMathExpressionNode* node, bool& error) {
  if (node->Category() == kCalcAngle) {
    return node->ComputeValueInCanonicalUnit().value();
  }
  return Rad2deg(ValueAsNumber(node, error));
}

CSSMathExpressionNode*
CSSMathExpressionOperation::CreateTrigonometricFunctionSimplified(
    Operands&& operands,
    CSSValueID function_id) {
  double value;
  auto unit_type = CSSPrimitiveValue::UnitType::kUnknown;
  bool error = false;
  switch (function_id) {
    case CSSValueID::kSin: {
      DCHECK_EQ(operands.size(), 1u);
      unit_type = CSSPrimitiveValue::UnitType::kNumber;
      value = gfx::SinCosDegrees(ValueAsDegrees(operands[0], error)).sin;
      break;
    }
    case CSSValueID::kCos: {
      DCHECK_EQ(operands.size(), 1u);
      unit_type = CSSPrimitiveValue::UnitType::kNumber;
      value = gfx::SinCosDegrees(ValueAsDegrees(operands[0], error)).cos;
      break;
    }
    case CSSValueID::kTan: {
      DCHECK_EQ(operands.size(), 1u);
      unit_type = CSSPrimitiveValue::UnitType::kNumber;
      value = TanDegrees(ValueAsDegrees(operands[0], error));
      break;
    }
    case CSSValueID::kAsin: {
      DCHECK_EQ(operands.size(), 1u);
      unit_type = CSSPrimitiveValue::UnitType::kDegrees;
      value = Rad2deg(std::asin(ValueAsNumber(operands[0], error)));
      DCHECK(value >= -90 && value <= 90 || std::isnan(value));
      break;
    }
    case CSSValueID::kAcos: {
      DCHECK_EQ(operands.size(), 1u);
      unit_type = CSSPrimitiveValue::UnitType::kDegrees;
      value = Rad2deg(std::acos(ValueAsNumber(operands[0], error)));
      DCHECK(value >= 0 && value <= 180 || std::isnan(value));
      break;
    }
    case CSSValueID::kAtan: {
      DCHECK_EQ(operands.size(), 1u);
      unit_type = CSSPrimitiveValue::UnitType::kDegrees;
      value = Rad2deg(std::atan(ValueAsNumber(operands[0], error)));
      DCHECK(value >= -90 && value <= 90 || std::isnan(value));
      break;
    }
    case CSSValueID::kAtan2: {
      DCHECK_EQ(operands.size(), 2u);
      unit_type = CSSPrimitiveValue::UnitType::kDegrees;
      value = Rad2deg(ResolveAtan2(operands[0], operands[1], error));
      DCHECK(value >= -180 && value <= 180 || std::isnan(value));
      break;
    }
    default:
      return nullptr;
  }

  if (error) {
    return nullptr;
  }

  DCHECK_NE(unit_type, CSSPrimitiveValue::UnitType::kUnknown);
  return CSSMathExpressionNumericLiteral::Create(value, unit_type);
}

CSSMathExpressionNode* CSSMathExpressionOperation::CreateSteppedValueFunction(
    Operands&& operands,
    CSSMathOperator op) {
  if (!RuntimeEnabledFeatures::CSSSteppedValueFunctionsEnabled()) {
    return nullptr;
  }
  DCHECK_EQ(operands.size(), 2u);
  if (operands[0]->Category() == kCalcOther ||
      operands[1]->Category() == kCalcOther) {
    return nullptr;
  }
  CalculationResultCategory category =
      kAddSubtractResult[operands[0]->Category()][operands[1]->Category()];
  if (category == kCalcOther) {
    return nullptr;
  }
  if (CanEagerlySimplify(operands)) {
    absl::optional<double> a = operands[0]->ComputeValueInCanonicalUnit();
    absl::optional<double> b = operands[1]->ComputeValueInCanonicalUnit();
    DCHECK(a.has_value());
    DCHECK(b.has_value());
    double value = EvaluateSteppedValueFunction(op, a.value(), b.value());
    return CSSMathExpressionNumericLiteral::Create(
        value,
        CSSPrimitiveValue::CanonicalUnit(operands.front()->ResolvedUnitType()));
  }
  return MakeGarbageCollected<CSSMathExpressionOperation>(
      category, std::move(operands), op);
}

// static
CSSMathExpressionNode* CSSMathExpressionOperation::CreateExponentialFunction(
    Operands&& operands,
    CSSValueID function_id) {
  if (!RuntimeEnabledFeatures::CSSExponentialFunctionsEnabled()) {
    return nullptr;
  }

  double value = 0;
  bool error = false;
  auto unit_type = CSSPrimitiveValue::UnitType::kNumber;
  switch (function_id) {
    case CSSValueID::kPow: {
      DCHECK_EQ(operands.size(), 2u);
      double a = ValueAsNumber(operands[0], error);
      double b = ValueAsNumber(operands[1], error);
      value = std::pow(a, b);
      break;
    }
    case CSSValueID::kSqrt: {
      DCHECK_EQ(operands.size(), 1u);
      double a = ValueAsNumber(operands[0], error);
      value = std::sqrt(a);
      break;
    }
    case CSSValueID::kHypot: {
      DCHECK_GE(operands.size(), 1u);
      CalculationResultCategory category =
          DetermineComparisonCategory(operands);
      if (category == kCalcOther) {
        return nullptr;
      }
      if (CanEagerlySimplify(operands)) {
        for (const CSSMathExpressionNode* operand : operands) {
          absl::optional<double> a = operand->ComputeValueInCanonicalUnit();
          DCHECK(a.has_value());
          value = std::hypot(value, a.value());
        }
        unit_type = CSSPrimitiveValue::CanonicalUnit(
            operands.front()->ResolvedUnitType());
      } else {
        return MakeGarbageCollected<CSSMathExpressionOperation>(
            category, std::move(operands), CSSMathOperator::kHypot);
      }
      break;
    }
    case CSSValueID::kLog: {
      DCHECK_GE(operands.size(), 1u);
      DCHECK_LE(operands.size(), 2u);
      double a = ValueAsNumber(operands[0], error);
      if (operands.size() == 2) {
        double b = ValueAsNumber(operands[1], error);
        value = std::log2(a) / std::log2(b);
      } else {
        value = std::log(a);
      }
      break;
    }
    case CSSValueID::kExp: {
      DCHECK_EQ(operands.size(), 1u);
      double a = ValueAsNumber(operands[0], error);
      value = std::exp(a);
      break;
    }
    default:
      return nullptr;
  }
  if (error) {
    return nullptr;
  }

  DCHECK_NE(unit_type, CSSPrimitiveValue::UnitType::kUnknown);
  return CSSMathExpressionNumericLiteral::Create(value, unit_type);
}

CSSMathExpressionNode* CSSMathExpressionOperation::CreateSignRelatedFunction(
    Operands&& operands,
    CSSValueID function_id) {
  if (!RuntimeEnabledFeatures::CSSSignRelatedFunctionsEnabled()) {
    return nullptr;
  }

  const CSSMathExpressionNode* operand = operands.front();

  switch (function_id) {
    case CSSValueID::kAbs: {
      if (CanEagerlySimplify(operand)) {
        const absl::optional<double> opt =
            operand->ComputeValueInCanonicalUnit();
        DCHECK(opt.has_value());
        return CSSMathExpressionNumericLiteral::Create(
            std::abs(opt.value()), operand->ResolvedUnitType());
      }
      return MakeGarbageCollected<CSSMathExpressionOperation>(
          operand->Category(), std::move(operands), CSSMathOperator::kAbs);
    }
    case CSSValueID::kSign: {
      if (CanEagerlySimplify(operand)) {
        const absl::optional<double> opt =
            operand->ComputeValueInCanonicalUnit();
        DCHECK(opt.has_value());
        const double value = opt.value();
        const double signum =
            (value == 0 || std::isnan(value)) ? value : ((value > 0) ? 1 : -1);
        return CSSMathExpressionNumericLiteral::Create(
            signum, CSSPrimitiveValue::UnitType::kNumber);
      }
      return MakeGarbageCollected<CSSMathExpressionOperation>(
          kCalcNumber, std::move(operands), CSSMathOperator::kSign);
    }
    default:
      NOTREACHED();
      return nullptr;
  }
}

// static
CSSMathExpressionNode*
CSSMathExpressionOperation::CreateArithmeticOperationSimplified(
    const CSSMathExpressionNode* left_side,
    const CSSMathExpressionNode* right_side,
    CSSMathOperator op) {
  if (CSSMathExpressionNode* result =
          MaybeDistributeArithmeticOperation(left_side, right_side, op)) {
    return result;
  }

  if (left_side->IsOperation() || right_side->IsOperation()) {
    return CreateArithmeticOperation(left_side, right_side, op);
  }

  CalculationResultCategory left_category = left_side->Category();
  CalculationResultCategory right_category = right_side->Category();
  DCHECK_NE(left_category, kCalcOther);
  DCHECK_NE(right_category, kCalcOther);

  // Simplify numbers.
  if (left_category == kCalcNumber && left_side->IsNumericLiteral() &&
      right_category == kCalcNumber && right_side->IsNumericLiteral()) {
    return CSSMathExpressionNumericLiteral::Create(
        EvaluateOperator({left_side->DoubleValue(), right_side->DoubleValue()},
                         op),
        CSSPrimitiveValue::UnitType::kNumber);
  }

  // Simplify addition and subtraction between same types.
  if (op == CSSMathOperator::kAdd || op == CSSMathOperator::kSubtract) {
    if (left_category == right_side->Category()) {
      CSSPrimitiveValue::UnitType left_type = left_side->ResolvedUnitType();
      if (HasDoubleValue(left_type)) {
        CSSPrimitiveValue::UnitType right_type = right_side->ResolvedUnitType();
        if (left_type == right_type) {
          return CSSMathExpressionNumericLiteral::Create(
              EvaluateOperator(
                  {left_side->DoubleValue(), right_side->DoubleValue()}, op),
              left_type);
        }
        CSSPrimitiveValue::UnitCategory left_unit_category =
            CSSPrimitiveValue::UnitTypeToUnitCategory(left_type);
        if (left_unit_category != CSSPrimitiveValue::kUOther &&
            left_unit_category ==
                CSSPrimitiveValue::UnitTypeToUnitCategory(right_type)) {
          CSSPrimitiveValue::UnitType canonical_type =
              CSSPrimitiveValue::CanonicalUnitTypeForCategory(
                  left_unit_category);
          if (canonical_type != CSSPrimitiveValue::UnitType::kUnknown) {
            double left_value =
                left_side->DoubleValue() *
                CSSPrimitiveValue::ConversionToCanonicalUnitsScaleFactor(
                    left_type);
            double right_value =
                right_side->DoubleValue() *
                CSSPrimitiveValue::ConversionToCanonicalUnitsScaleFactor(
                    right_type);
            return CSSMathExpressionNumericLiteral::Create(
                EvaluateOperator({left_value, right_value}, op),
                canonical_type);
          }
        }
      }
    }
  } else {
    // Simplify multiplying or dividing by a number for simplifiable types.
    DCHECK(op == CSSMathOperator::kMultiply || op == CSSMathOperator::kDivide);
    const CSSMathExpressionNode* number_side =
        GetNumericLiteralSide(left_side, right_side);
    if (!number_side) {
      return CreateArithmeticOperation(left_side, right_side, op);
    }
    if (number_side == left_side && op == CSSMathOperator::kDivide) {
      return nullptr;
    }
    const CSSMathExpressionNode* other_side =
        left_side == number_side ? right_side : left_side;

    double number = number_side->DoubleValue();

    CSSPrimitiveValue::UnitType other_type = other_side->ResolvedUnitType();
    if (HasDoubleValue(other_type)) {
      return CSSMathExpressionNumericLiteral::Create(
          EvaluateOperator({other_side->DoubleValue(), number}, op),
          other_type);
    }
  }

  return CreateArithmeticOperation(left_side, right_side, op);
}

CSSMathExpressionOperation::CSSMathExpressionOperation(
    const CSSMathExpressionNode* left_side,
    const CSSMathExpressionNode* right_side,
    CSSMathOperator op,
    CalculationResultCategory category)
    : CSSMathExpressionNode(
          category,
          left_side->HasComparisons() || right_side->HasComparisons(),
          !left_side->IsScopedValue() || !right_side->IsScopedValue()),
      operands_({left_side, right_side}),
      operator_(op) {}

bool CSSMathExpressionOperation::InvolvesPercentage() const {
  if (Category() == kCalcPercent || Category() == kCalcPercentLength) {
    return true;
  }
  for (const CSSMathExpressionNode* operand : operands_) {
    if (operand->InvolvesPercentage()) {
      return true;
    }
  }
  return false;
}

bool CSSMathExpressionOperation::InvolvesAnchorQueries() const {
  for (const CSSMathExpressionNode* operand : operands_) {
    if (operand->InvolvesAnchorQueries()) {
      return true;
    }
  }
  return false;
}

static bool AnyOperandHasComparisons(
    CSSMathExpressionOperation::Operands& operands) {
  for (const CSSMathExpressionNode* operand : operands) {
    if (operand->HasComparisons()) {
      return true;
    }
  }
  return false;
}

static bool AnyOperandNeedsTreeScopePopulation(
    CSSMathExpressionOperation::Operands& operands) {
  for (const CSSMathExpressionNode* operand : operands) {
    if (!operand->IsScopedValue()) {
      return true;
    }
  }
  return false;
}

CSSMathExpressionOperation::CSSMathExpressionOperation(
    CalculationResultCategory category,
    Operands&& operands,
    CSSMathOperator op)
    : CSSMathExpressionNode(
          category,
          IsComparison(op) || AnyOperandHasComparisons(operands),
          AnyOperandNeedsTreeScopePopulation(operands)),
      operands_(std::move(operands)),
      operator_(op) {}

CSSMathExpressionOperation::CSSMathExpressionOperation(
    CalculationResultCategory category,
    CSSMathOperator op)
    : CSSMathExpressionNode(category,
                            IsComparison(op),
                            false),
      operator_(op) {}

bool CSSMathExpressionOperation::IsZero() const {
  absl::optional<double> maybe_value = ComputeValueInCanonicalUnit();
  return maybe_value && !*maybe_value;
}

absl::optional<PixelsAndPercent> CSSMathExpressionOperation::ToPixelsAndPercent(
    const CSSLengthResolver& length_resolver) const {
  absl::optional<PixelsAndPercent> result;
  switch (operator_) {
    case CSSMathOperator::kAdd:
    case CSSMathOperator::kSubtract: {
      DCHECK_EQ(operands_.size(), 2u);
      result = operands_[0]->ToPixelsAndPercent(length_resolver);
      if (!result) {
        return absl::nullopt;
      }

      absl::optional<PixelsAndPercent> other_side =
          operands_[1]->ToPixelsAndPercent(length_resolver);
      if (!other_side) {
        return absl::nullopt;
      }
      if (operator_ == CSSMathOperator::kAdd) {
        result.value() += other_side.value();
      } else {
        result.value() -= other_side.value();
      }
      break;
    }
    case CSSMathOperator::kMultiply:
    case CSSMathOperator::kDivide: {
      DCHECK_EQ(operands_.size(), 2u);
      const CSSMathExpressionNode* number_side =
          GetNumericLiteralSide(operands_[0], operands_[1]);
      if (!number_side) {
        return absl::nullopt;
      }
      const CSSMathExpressionNode* other_side =
          operands_[0] == number_side ? operands_[1] : operands_[0];
      result = other_side->ToPixelsAndPercent(length_resolver);
      if (!result) {
        return absl::nullopt;
      }
      float number = number_side->DoubleValue();
      if (operator_ == CSSMathOperator::kDivide) {
        number = 1.0 / number;
      }
      result.value() *= number;
      break;
    }
    case CSSMathOperator::kMin:
    case CSSMathOperator::kMax:
    case CSSMathOperator::kClamp:
    case CSSMathOperator::kRoundNearest:
    case CSSMathOperator::kRoundUp:
    case CSSMathOperator::kRoundDown:
    case CSSMathOperator::kRoundToZero:
    case CSSMathOperator::kMod:
    case CSSMathOperator::kRem:
    case CSSMathOperator::kHypot:
    case CSSMathOperator::kAbs:
    case CSSMathOperator::kProgress:
    case CSSMathOperator::kSign:
      return absl::nullopt;
    case CSSMathOperator::kInvalid:
      NOTREACHED();
  }
  return result;
}

scoped_refptr<const CalculationExpressionNode>
CSSMathExpressionOperation::ToCalculationExpression(
    const CSSLengthResolver& length_resolver) const {
  switch (operator_) {
    case CSSMathOperator::kAdd:
      DCHECK_EQ(operands_.size(), 2u);
      return CalculationExpressionOperationNode::CreateSimplified(
          CalculationExpressionOperationNode::Children(
              {operands_[0]->ToCalculationExpression(length_resolver),
               operands_[1]->ToCalculationExpression(length_resolver)}),
          CalculationOperator::kAdd);
    case CSSMathOperator::kSubtract:
      DCHECK_EQ(operands_.size(), 2u);
      return CalculationExpressionOperationNode::CreateSimplified(
          CalculationExpressionOperationNode::Children(
              {operands_[0]->ToCalculationExpression(length_resolver),
               operands_[1]->ToCalculationExpression(length_resolver)}),
          CalculationOperator::kSubtract);
    case CSSMathOperator::kMultiply:
      DCHECK_EQ(operands_.size(), 2u);
      return CalculationExpressionOperationNode::CreateSimplified(
          {operands_.front()->ToCalculationExpression(length_resolver),
           operands_.back()->ToCalculationExpression(length_resolver)},
          CalculationOperator::kMultiply);
    case CSSMathOperator::kDivide:
      DCHECK_EQ(operands_.size(), 2u);
      DCHECK_EQ(operands_[1]->Category(), kCalcNumber);
      return CalculationExpressionOperationNode::CreateSimplified(
          CalculationExpressionOperationNode::Children(
              {operands_[0]->ToCalculationExpression(length_resolver),
               base::MakeRefCounted<CalculationExpressionNumberNode>(
                   1.0 / operands_[1]->DoubleValue())}),
          CalculationOperator::kMultiply);
    case CSSMathOperator::kMin:
    case CSSMathOperator::kMax: {
      Vector<scoped_refptr<const CalculationExpressionNode>> operands;
      operands.reserve(operands_.size());
      for (const CSSMathExpressionNode* operand : operands_) {
        operands.push_back(operand->ToCalculationExpression(length_resolver));
      }
      auto expression_operator = operator_ == CSSMathOperator::kMin
                                     ? CalculationOperator::kMin
                                     : CalculationOperator::kMax;
      return CalculationExpressionOperationNode::CreateSimplified(
          std::move(operands), expression_operator);
    }
    case CSSMathOperator::kClamp: {
      Vector<scoped_refptr<const CalculationExpressionNode>> operands;
      operands.reserve(operands_.size());
      for (const CSSMathExpressionNode* operand : operands_) {
        operands.push_back(operand->ToCalculationExpression(length_resolver));
      }
      return CalculationExpressionOperationNode::CreateSimplified(
          std::move(operands), CalculationOperator::kClamp);
    }
    case CSSMathOperator::kRoundNearest:
    case CSSMathOperator::kRoundUp:
    case CSSMathOperator::kRoundDown:
    case CSSMathOperator::kRoundToZero:
    case CSSMathOperator::kMod:
    case CSSMathOperator::kRem:
    case CSSMathOperator::kHypot:
    case CSSMathOperator::kAbs:
    case CSSMathOperator::kSign:
    case CSSMathOperator::kProgress: {
      Vector<scoped_refptr<const CalculationExpressionNode>> operands;
      operands.reserve(operands_.size());
      for (const CSSMathExpressionNode* operand : operands_) {
        operands.push_back(operand->ToCalculationExpression(length_resolver));
      }
      CalculationOperator op;
      if (operator_ == CSSMathOperator::kRoundNearest) {
        op = CalculationOperator::kRoundNearest;
      } else if (operator_ == CSSMathOperator::kRoundUp) {
        op = CalculationOperator::kRoundUp;
      } else if (operator_ == CSSMathOperator::kRoundDown) {
        op = CalculationOperator::kRoundDown;
      } else if (operator_ == CSSMathOperator::kRoundToZero) {
        op = CalculationOperator::kRoundToZero;
      } else if (operator_ == CSSMathOperator::kMod) {
        op = CalculationOperator::kMod;
      } else if (operator_ == CSSMathOperator::kRem) {
        op = CalculationOperator::kRem;
      } else if (operator_ == CSSMathOperator::kHypot) {
        op = CalculationOperator::kHypot;
      } else if (operator_ == CSSMathOperator::kAbs) {
        op = CalculationOperator::kAbs;
      } else if (operator_ == CSSMathOperator::kSign) {
        op = CalculationOperator::kSign;
      } else {
        CHECK(operator_ == CSSMathOperator::kProgress);
        op = CalculationOperator::kProgress;
      }
      return CalculationExpressionOperationNode::CreateSimplified(
          std::move(operands), op);
    }
    case CSSMathOperator::kInvalid:
      NOTREACHED();
      return nullptr;
  }
}

double CSSMathExpressionOperation::DoubleValue() const {
  DCHECK(HasDoubleValue(ResolvedUnitType())) << CustomCSSText();
  Vector<double> double_values;
  double_values.reserve(operands_.size());
  for (const CSSMathExpressionNode* operand : operands_) {
    double_values.push_back(operand->DoubleValue());
  }
  return Evaluate(double_values);
}

static bool HasCanonicalUnit(CalculationResultCategory category) {
  return category == kCalcNumber || category == kCalcLength ||
         category == kCalcPercent || category == kCalcAngle ||
         category == kCalcTime || category == kCalcFrequency ||
         category == kCalcResolution;
}

absl::optional<double> CSSMathExpressionOperation::ComputeValueInCanonicalUnit()
    const {
  if (!HasCanonicalUnit(category_)) {
    return absl::nullopt;
  }

  Vector<double> double_values;
  double_values.reserve(operands_.size());
  for (const CSSMathExpressionNode* operand : operands_) {
    absl::optional<double> maybe_value = operand->ComputeValueInCanonicalUnit();
    if (!maybe_value) {
      return absl::nullopt;
    }
    double_values.push_back(*maybe_value);
  }
  return Evaluate(double_values);
}

double CSSMathExpressionOperation::ComputeDouble(
    const CSSLengthResolver& length_resolver) const {
  Vector<double> double_values;
  double_values.reserve(operands_.size());
  for (const CSSMathExpressionNode* operand : operands_) {
    double_values.push_back(
        CSSMathExpressionNode::ComputeDouble(operand, length_resolver));
  }
  return Evaluate(double_values);
}

double CSSMathExpressionOperation::ComputeLengthPx(
    const CSSLengthResolver& length_resolver) const {
  DCHECK(!HasPercentage());
  DCHECK_EQ(Category(), kCalcLength);
  return ComputeDouble(length_resolver);
}

bool CSSMathExpressionOperation::AccumulateLengthArray(
    CSSLengthArray& length_array,
    double multiplier) const {
  switch (operator_) {
    case CSSMathOperator::kAdd:
      DCHECK_EQ(operands_.size(), 2u);
      if (!operands_[0]->AccumulateLengthArray(length_array, multiplier)) {
        return false;
      }
      if (!operands_[1]->AccumulateLengthArray(length_array, multiplier)) {
        return false;
      }
      return true;
    case CSSMathOperator::kSubtract:
      DCHECK_EQ(operands_.size(), 2u);
      if (!operands_[0]->AccumulateLengthArray(length_array, multiplier)) {
        return false;
      }
      if (!operands_[1]->AccumulateLengthArray(length_array, -multiplier)) {
        return false;
      }
      return true;
    case CSSMathOperator::kMultiply:
      DCHECK_EQ(operands_.size(), 2u);
      DCHECK_NE((operands_[0]->Category() == kCalcNumber),
                (operands_[1]->Category() == kCalcNumber));
      if (operands_[0]->Category() == kCalcNumber) {
        return operands_[1]->AccumulateLengthArray(
            length_array, multiplier * operands_[0]->DoubleValue());
      } else {
        return operands_[0]->AccumulateLengthArray(
            length_array, multiplier * operands_[1]->DoubleValue());
      }
    case CSSMathOperator::kDivide:
      DCHECK_EQ(operands_.size(), 2u);
      DCHECK_EQ(operands_[1]->Category(), kCalcNumber);
      return operands_[0]->AccumulateLengthArray(
          length_array, multiplier / operands_[1]->DoubleValue());
    case CSSMathOperator::kMin:
    case CSSMathOperator::kMax:
    case CSSMathOperator::kClamp:
      // When comparison functions are involved, we can't resolve the expression
      // into a length array.
    case CSSMathOperator::kRoundNearest:
    case CSSMathOperator::kRoundUp:
    case CSSMathOperator::kRoundDown:
    case CSSMathOperator::kRoundToZero:
    case CSSMathOperator::kMod:
    case CSSMathOperator::kRem:
    case CSSMathOperator::kHypot:
    case CSSMathOperator::kAbs:
    case CSSMathOperator::kSign:
      // When stepped value functions are involved, we can't resolve the
      // expression into a length array.
    case CSSMathOperator::kProgress:
      return false;
    case CSSMathOperator::kInvalid:
      NOTREACHED();
      return false;
  }
}

void CSSMathExpressionOperation::AccumulateLengthUnitTypes(
    CSSPrimitiveValue::LengthTypeFlags& types) const {
  for (const CSSMathExpressionNode* operand : operands_) {
    operand->AccumulateLengthUnitTypes(types);
  }
}

bool CSSMathExpressionOperation::IsComputationallyIndependent() const {
  if (Category() != kCalcLength && Category() != kCalcPercentLength) {
    return true;
  }
  for (const CSSMathExpressionNode* operand : operands_) {
    if (!operand->IsComputationallyIndependent()) {
      return false;
    }
  }
  return true;
}

String CSSMathExpressionOperation::CustomCSSText() const {
  switch (operator_) {
    case CSSMathOperator::kAdd:
    case CSSMathOperator::kSubtract:
    case CSSMathOperator::kMultiply:
    case CSSMathOperator::kDivide: {
      DCHECK_EQ(operands_.size(), 2u);

      // As per
      // https://drafts.csswg.org/css-values-4/#sort-a-calculations-children
      // we should sort the dimensions of the sum node.
      const CSSMathExpressionOperation* operation = this;
      if (IsAddOrSubtract()) {
        const CSSMathExpressionNode* node = MaybeSortSumNode(this);
        // Note: we can hit here, since CSS Typed OM doesn't currently follow
        // the same simplifications as CSS Values spec.
        // https://github.com/w3c/csswg-drafts/issues/9451
        if (!node->IsOperation()) {
          return node->CustomCSSText();
        }
        operation = To<CSSMathExpressionOperation>(node);
      }
      CSSMathOperator op = operation->OperatorType();
      const Operands& operands = operation->GetOperands();

      StringBuilder result;

      // After all the simplifications we only need parentheses here for the
      // cases like: (lhs as unsimplified sum/sub) [* or /] rhs
      const bool left_side_needs_parentheses =
          IsMultiplyOrDivide() && operands.front()->IsOperation() &&
          To<CSSMathExpressionOperation>(operands.front().Get())
              ->IsAddOrSubtract();
      if (left_side_needs_parentheses) {
        result.Append('(');
      }
      result.Append(operands[0]->CustomCSSText());
      if (left_side_needs_parentheses) {
        result.Append(')');
      }

      result.Append(' ');
      result.Append(ToString(op));
      result.Append(' ');

      // After all the simplifications we only need parentheses here for the
      // cases like: lhs [* or /] (rhs as unsimplified sum/sub)
      const bool right_side_needs_parentheses =
          IsMultiplyOrDivide() && operands.back()->IsOperation() &&
          To<CSSMathExpressionOperation>(operands.back().Get())
              ->IsAddOrSubtract();
      if (right_side_needs_parentheses) {
        result.Append('(');
      }
      result.Append(operands[1]->CustomCSSText());
      if (right_side_needs_parentheses) {
        result.Append(')');
      }

      return result.ReleaseString();
    }
    case CSSMathOperator::kMin:
    case CSSMathOperator::kMax:
    case CSSMathOperator::kClamp:
    case CSSMathOperator::kRoundNearest:
    case CSSMathOperator::kMod:
    case CSSMathOperator::kRem:
    case CSSMathOperator::kHypot:
    case CSSMathOperator::kAbs:
    case CSSMathOperator::kSign: {
      StringBuilder result;
      result.Append(ToString(operator_));
      result.Append('(');
      result.Append(operands_.front()->CustomCSSText());
      for (const CSSMathExpressionNode* operand : SecondToLastOperands()) {
        result.Append(", ");
        result.Append(operand->CustomCSSText());
      }
      result.Append(')');

      return result.ReleaseString();
    }
    case CSSMathOperator::kRoundUp:
    case CSSMathOperator::kRoundDown:
    case CSSMathOperator::kRoundToZero: {
      StringBuilder result;
      result.Append(ToString(operator_));
      result.Append(operands_.front()->CustomCSSText());
      for (const CSSMathExpressionNode* operand : SecondToLastOperands()) {
        result.Append(", ");
        result.Append(operand->CustomCSSText());
      }
      result.Append(')');

      return result.ReleaseString();
    }
    case CSSMathOperator::kProgress: {
      CHECK_EQ(operands_.size(), 3u);
      StringBuilder result;
      result.Append(ToString(operator_));
      result.Append('(');
      result.Append(operands_.front()->CustomCSSText());
      result.Append(" from ");
      result.Append(operands_[1]->CustomCSSText());
      result.Append(" to ");
      result.Append(operands_.back()->CustomCSSText());
      result.Append(')');

      return result.ReleaseString();
    }
    case CSSMathOperator::kInvalid:
      NOTREACHED();
      return String();
  }
}

bool CSSMathExpressionOperation::operator==(
    const CSSMathExpressionNode& exp) const {
  if (!exp.IsOperation()) {
    return false;
  }

  const CSSMathExpressionOperation& other = To<CSSMathExpressionOperation>(exp);
  if (operator_ != other.operator_) {
    return false;
  }
  if (operands_.size() != other.operands_.size()) {
    return false;
  }
  for (wtf_size_t i = 0; i < operands_.size(); ++i) {
    if (!base::ValuesEquivalent(operands_[i], other.operands_[i])) {
      return false;
    }
  }
  return true;
}

CSSPrimitiveValue::UnitType CSSMathExpressionOperation::ResolvedUnitType()
    const {
  switch (category_) {
    case kCalcNumber:
      return CSSPrimitiveValue::UnitType::kNumber;
    case kCalcAngle:
    case kCalcTime:
    case kCalcFrequency:
    case kCalcLength:
    case kCalcPercent:
    case kCalcResolution:
      switch (operator_) {
        case CSSMathOperator::kMultiply:
        case CSSMathOperator::kDivide: {
          DCHECK_EQ(operands_.size(), 2u);
          if (operands_[0]->Category() == kCalcNumber) {
            return operands_[1]->ResolvedUnitType();
          }
          if (operands_[1]->Category() == kCalcNumber) {
            return operands_[0]->ResolvedUnitType();
          }
          NOTREACHED();
          return CSSPrimitiveValue::UnitType::kUnknown;
        }
        case CSSMathOperator::kAdd:
        case CSSMathOperator::kSubtract:
        case CSSMathOperator::kMin:
        case CSSMathOperator::kMax:
        case CSSMathOperator::kClamp:
        case CSSMathOperator::kRoundNearest:
        case CSSMathOperator::kRoundUp:
        case CSSMathOperator::kRoundDown:
        case CSSMathOperator::kRoundToZero:
        case CSSMathOperator::kMod:
        case CSSMathOperator::kRem:
        case CSSMathOperator::kHypot:
        case CSSMathOperator::kAbs: {
          CSSPrimitiveValue::UnitType first_type =
              operands_.front()->ResolvedUnitType();
          if (first_type == CSSPrimitiveValue::UnitType::kUnknown) {
            return CSSPrimitiveValue::UnitType::kUnknown;
          }
          for (const CSSMathExpressionNode* operand : SecondToLastOperands()) {
            CSSPrimitiveValue::UnitType next = operand->ResolvedUnitType();
            if (next == CSSPrimitiveValue::UnitType::kUnknown ||
                next != first_type) {
              return CSSPrimitiveValue::UnitType::kUnknown;
            }
          }
          return first_type;
        }
        case CSSMathOperator::kSign:
        case CSSMathOperator::kProgress:
          return CSSPrimitiveValue::UnitType::kNumber;
        case CSSMathOperator::kInvalid:
          NOTREACHED();
          return CSSPrimitiveValue::UnitType::kUnknown;
      }
    case kCalcPercentLength:
    case kCalcOther:
      return CSSPrimitiveValue::UnitType::kUnknown;
    case kCalcIdent:
      return CSSPrimitiveValue::UnitType::kIdent;
  }

  NOTREACHED();
  return CSSPrimitiveValue::UnitType::kUnknown;
}

void CSSMathExpressionOperation::Trace(Visitor* visitor) const {
  visitor->Trace(operands_);
  CSSMathExpressionNode::Trace(visitor);
}

// static
const CSSMathExpressionNode* CSSMathExpressionOperation::GetNumericLiteralSide(
    const CSSMathExpressionNode* left_side,
    const CSSMathExpressionNode* right_side) {
  if (left_side->Category() == kCalcNumber && left_side->IsNumericLiteral()) {
    return left_side;
  }
  if (right_side->Category() == kCalcNumber && right_side->IsNumericLiteral()) {
    return right_side;
  }
  return nullptr;
}

// static
double CSSMathExpressionOperation::EvaluateOperator(
    const Vector<double>& operands,
    CSSMathOperator op) {
  // Design doc for infinity and NaN: https://bit.ly/349gXjq

  // Any operation with at least one NaN argument produces NaN
  // https://drafts.csswg.org/css-values/#calc-type-checking
  for (double operand : operands) {
    if (std::isnan(operand)) {
      return operand;
    }
  }

  switch (op) {
    case CSSMathOperator::kAdd:
      DCHECK_EQ(operands.size(), 2u);
      return operands[0] + operands[1];
    case CSSMathOperator::kSubtract:
      DCHECK_EQ(operands.size(), 2u);
      return operands[0] - operands[1];
    case CSSMathOperator::kMultiply:
      DCHECK_EQ(operands.size(), 2u);
      return operands[0] * operands[1];
    case CSSMathOperator::kDivide:
      DCHECK(operands.size() == 1u || operands.size() == 2u);
      return operands[0] / operands[1];
    case CSSMathOperator::kMin: {
      if (operands.empty()) {
        return std::numeric_limits<double>::quiet_NaN();
      }
      double minimum = operands[0];
      for (double operand : operands) {
        minimum = std::min(minimum, operand);
      }
      return minimum;
    }
    case CSSMathOperator::kMax: {
      if (operands.empty()) {
        return std::numeric_limits<double>::quiet_NaN();
      }
      double maximum = operands[0];
      for (double operand : operands) {
        maximum = std::max(maximum, operand);
      }
      return maximum;
    }
    case CSSMathOperator::kClamp: {
      DCHECK_EQ(operands.size(), 3u);
      double min = operands[0];
      double val = operands[1];
      double max = operands[2];
      // clamp(MIN, VAL, MAX) is identical to max(MIN, min(VAL, MAX))
      // according to the spec,
      // https://drafts.csswg.org/css-values-4/#funcdef-clamp.
      return std::max(min, std::min(val, max));
    }
    case CSSMathOperator::kRoundNearest:
    case CSSMathOperator::kRoundUp:
    case CSSMathOperator::kRoundDown:
    case CSSMathOperator::kRoundToZero:
    case CSSMathOperator::kMod:
    case CSSMathOperator::kRem: {
      DCHECK_EQ(operands.size(), 2u);
      return EvaluateSteppedValueFunction(op, operands[0], operands[1]);
    }
    case CSSMathOperator::kHypot: {
      DCHECK_GE(operands.size(), 1u);
      double value = 0;
      for (double operand : operands) {
        value = std::hypot(value, operand);
      }
      return value;
    }
    case CSSMathOperator::kAbs: {
      DCHECK_EQ(operands.size(), 1u);
      return std::abs(operands.front());
    }
    case CSSMathOperator::kSign: {
      DCHECK_EQ(operands.size(), 1u);
      const double value = operands.front();
      const double signum =
          (value == 0 || std::isnan(value)) ? value : ((value > 0) ? 1 : -1);
      return signum;
    }
    case CSSMathOperator::kProgress: {
      CHECK_EQ(operands.size(), 3u);
      return (operands[0] - operands[1]) / (operands[2] - operands[1]);
    }
    case CSSMathOperator::kInvalid:
      NOTREACHED();
      break;
  }
  return 0;
}

const CSSMathExpressionNode& CSSMathExpressionOperation::PopulateWithTreeScope(
    const TreeScope* tree_scope) const {
  Operands populated_operands;
  for (const CSSMathExpressionNode* op : operands_) {
    populated_operands.push_back(&op->EnsureScopedValue(tree_scope));
  }
  return *MakeGarbageCollected<CSSMathExpressionOperation>(
      Category(), std::move(populated_operands), operator_);
}

#if DCHECK_IS_ON()
bool CSSMathExpressionOperation::InvolvesPercentageComparisons() const {
  if (IsMinOrMax() && Category() == kCalcPercent && operands_.size() > 1u) {
    return true;
  }
  for (const CSSMathExpressionNode* operand : operands_) {
    if (operand->InvolvesPercentageComparisons()) {
      return true;
    }
  }
  return false;
}
#endif

// ------ End of CSSMathExpressionOperation member functions ------

// ------ Start of CSSMathExpressionAnchorQuery member functions ------

CSSMathExpressionAnchorQuery::CSSMathExpressionAnchorQuery(
    CSSAnchorQueryType type,
    const CSSValue* anchor_specifier,
    const CSSValue& value,
    const CSSPrimitiveValue* fallback)
    : CSSMathExpressionNode(
          kCalcPercentLength,
          false /* has_comparisons */,
          (anchor_specifier && !anchor_specifier->IsScopedValue()) ||
              (fallback && !fallback->IsScopedValue())),
      type_(type),
      anchor_specifier_(anchor_specifier),
      value_(value),
      fallback_(fallback) {}

String CSSMathExpressionAnchorQuery::CustomCSSText() const {
  StringBuilder result;
  result.Append(IsAnchor() ? "anchor(" : "anchor-size(");
  if (anchor_specifier_) {
    result.Append(anchor_specifier_->CssText());
    result.Append(" ");
  }
  result.Append(value_->CssText());
  if (fallback_) {
    result.Append(", ");
    result.Append(fallback_->CustomCSSText());
  }
  result.Append(")");
  return result.ToString();
}

bool CSSMathExpressionAnchorQuery::operator==(
    const CSSMathExpressionNode& other) const {
  const auto* other_anchor = DynamicTo<CSSMathExpressionAnchorQuery>(other);
  if (!other_anchor) {
    return false;
  }
  return type_ == other_anchor->type_ &&
         base::ValuesEquivalent(anchor_specifier_,
                                other_anchor->anchor_specifier_) &&
         base::ValuesEquivalent(value_, other_anchor->value_) &&
         base::ValuesEquivalent(fallback_, other_anchor->fallback_);
}

namespace {

CSSAnchorValue CSSValueIDToAnchorValueEnum(CSSValueID value) {
  switch (value) {
    case CSSValueID::kAuto:
      return CSSAnchorValue::kAuto;
    case CSSValueID::kAutoSame:
      return CSSAnchorValue::kAutoSame;
    case CSSValueID::kTop:
      return CSSAnchorValue::kTop;
    case CSSValueID::kLeft:
      return CSSAnchorValue::kLeft;
    case CSSValueID::kRight:
      return CSSAnchorValue::kRight;
    case CSSValueID::kBottom:
      return CSSAnchorValue::kBottom;
    case CSSValueID::kStart:
      return CSSAnchorValue::kStart;
    case CSSValueID::kEnd:
      return CSSAnchorValue::kEnd;
    case CSSValueID::kSelfStart:
      return CSSAnchorValue::kSelfStart;
    case CSSValueID::kSelfEnd:
      return CSSAnchorValue::kSelfEnd;
    case CSSValueID::kCenter:
      return CSSAnchorValue::kCenter;
    default:
      NOTREACHED();
      return CSSAnchorValue::kCenter;
  }
}

CSSAnchorSizeValue CSSValueIDToAnchorSizeValueEnum(CSSValueID value) {
  switch (value) {
    case CSSValueID::kWidth:
      return CSSAnchorSizeValue::kWidth;
    case CSSValueID::kHeight:
      return CSSAnchorSizeValue::kHeight;
    case CSSValueID::kBlock:
      return CSSAnchorSizeValue::kBlock;
    case CSSValueID::kInline:
      return CSSAnchorSizeValue::kInline;
    case CSSValueID::kSelfBlock:
      return CSSAnchorSizeValue::kSelfBlock;
    case CSSValueID::kSelfInline:
      return CSSAnchorSizeValue::kSelfInline;
    default:
      NOTREACHED();
      return CSSAnchorSizeValue::kSelfInline;
  }
}

}  // namespace

scoped_refptr<const CalculationExpressionNode>
CSSMathExpressionAnchorQuery::ToCalculationExpression(
    const CSSLengthResolver& length_resolver) const {
  DCHECK(IsScopedValue());
  AnchorSpecifierValue* anchor_specifier = AnchorSpecifierValue::Default();
  if (const auto* implicit =
          DynamicTo<CSSIdentifierValue>(anchor_specifier_.Get())) {
    DCHECK_EQ(implicit->GetValueID(), CSSValueID::kImplicit);
    anchor_specifier = AnchorSpecifierValue::Implicit();
  } else if (const auto* custom_ident =
                 DynamicTo<CSSCustomIdentValue>(anchor_specifier_.Get())) {
    length_resolver.ReferenceAnchor();
    anchor_specifier = MakeGarbageCollected<AnchorSpecifierValue>(
        *MakeGarbageCollected<ScopedCSSName>(custom_ident->Value(),
                                             custom_ident->GetTreeScope()));
  }
  Length fallback = fallback_ ? fallback_->ConvertToLength(length_resolver)
                              : Length::Fixed(0);

  if (type_ == CSSAnchorQueryType::kAnchor) {
    if (const CSSPrimitiveValue* percentage =
            DynamicTo<CSSPrimitiveValue>(*value_)) {
      DCHECK(percentage->IsPercentage());
      return CalculationExpressionAnchorQueryNode::CreateAnchorPercentage(
          *anchor_specifier, percentage->GetFloatValue(), fallback);
    }
    const CSSIdentifierValue& side = To<CSSIdentifierValue>(*value_);
    return CalculationExpressionAnchorQueryNode::CreateAnchor(
        *anchor_specifier, CSSValueIDToAnchorValueEnum(side.GetValueID()),
        fallback);
  }

  DCHECK_EQ(type_, CSSAnchorQueryType::kAnchorSize);
  const CSSIdentifierValue& size = To<CSSIdentifierValue>(*value_);
  return CalculationExpressionAnchorQueryNode::CreateAnchorSize(
      *anchor_specifier, CSSValueIDToAnchorSizeValueEnum(size.GetValueID()),
      fallback);
}

const CSSMathExpressionNode&
CSSMathExpressionAnchorQuery::PopulateWithTreeScope(
    const TreeScope* tree_scope) const {
  return *MakeGarbageCollected<CSSMathExpressionAnchorQuery>(
      type_,
      anchor_specifier_ ? &anchor_specifier_->EnsureScopedValue(tree_scope)
                        : nullptr,
      *value_,
      fallback_
          ? To<CSSPrimitiveValue>(&fallback_->EnsureScopedValue(tree_scope))
          : nullptr);
}

void CSSMathExpressionAnchorQuery::Trace(Visitor* visitor) const {
  visitor->Trace(anchor_specifier_);
  visitor->Trace(value_);
  visitor->Trace(fallback_);
  CSSMathExpressionNode::Trace(visitor);
}

// ------ End of CSSMathExpressionAnchorQuery member functions ------

class CSSMathExpressionNodeParser {
  STACK_ALLOCATED();

 public:
  CSSMathExpressionNodeParser(
      const CSSParserContext& context,
      const bool is_percentage_allowed,
      CSSAnchorQueryTypes allowed_anchor_queries,
      const HashMap<CSSValueID, double>& color_channel_keyword_values)
      : context_(context),
        allowed_anchor_queries_(allowed_anchor_queries),
        is_percentage_allowed_(is_percentage_allowed),
        color_channel_keyword_values_(color_channel_keyword_values) {}

  bool IsSupportedMathFunction(CSSValueID function_id) {
    switch (function_id) {
      case CSSValueID::kMin:
      case CSSValueID::kMax:
      case CSSValueID::kClamp:
      case CSSValueID::kCalc:
      case CSSValueID::kWebkitCalc:
      case CSSValueID::kSin:
      case CSSValueID::kCos:
      case CSSValueID::kTan:
      case CSSValueID::kAsin:
      case CSSValueID::kAcos:
      case CSSValueID::kAtan:
      case CSSValueID::kAtan2:
        return true;
      case CSSValueID::kPow:
      case CSSValueID::kSqrt:
      case CSSValueID::kHypot:
      case CSSValueID::kLog:
      case CSSValueID::kExp:
        return RuntimeEnabledFeatures::CSSExponentialFunctionsEnabled();
      case CSSValueID::kRound:
      case CSSValueID::kMod:
      case CSSValueID::kRem:
        return RuntimeEnabledFeatures::CSSSteppedValueFunctionsEnabled();
      case CSSValueID::kAbs:
      case CSSValueID::kSign:
        return RuntimeEnabledFeatures::CSSSignRelatedFunctionsEnabled();
      case CSSValueID::kAnchor:
      case CSSValueID::kAnchorSize:
        return RuntimeEnabledFeatures::CSSAnchorPositioningEnabled();
      case CSSValueID::kProgress:
        return RuntimeEnabledFeatures::CSSProgressNotationEnabled();
      // TODO(crbug.com/1284199): Support other math functions.
      default:
        return false;
    }
  }

  CSSMathExpressionNode* ParseAnchorQuery(CSSValueID function_id,
                                          CSSParserTokenRange& tokens) {
    DCHECK(RuntimeEnabledFeatures::CSSAnchorPositioningEnabled());
    CSSAnchorQueryType anchor_query_type;
    switch (function_id) {
      case CSSValueID::kAnchor:
        anchor_query_type = CSSAnchorQueryType::kAnchor;
        break;
      case CSSValueID::kAnchorSize:
        anchor_query_type = CSSAnchorQueryType::kAnchorSize;
        break;
      default:
        return nullptr;
    }

    if (!(static_cast<CSSAnchorQueryTypes>(anchor_query_type) &
          allowed_anchor_queries_)) {
      return nullptr;
    }

    // |anchor_specifier| may be omitted to represent the default anchor.
    const CSSValue* anchor_specifier =
        css_parsing_utils::ConsumeIdent<CSSValueID::kImplicit>(tokens);
    if (!anchor_specifier) {
      anchor_specifier =
          css_parsing_utils::ConsumeDashedIdent(tokens, context_);
    }

    tokens.ConsumeWhitespace();
    const CSSValue* value = nullptr;
    switch (anchor_query_type) {
      case CSSAnchorQueryType::kAnchor:
        value = css_parsing_utils::ConsumeIdent<
            CSSValueID::kAuto, CSSValueID::kAutoSame, CSSValueID::kTop,
            CSSValueID::kLeft, CSSValueID::kRight, CSSValueID::kBottom,
            CSSValueID::kStart, CSSValueID::kEnd, CSSValueID::kSelfStart,
            CSSValueID::kSelfEnd, CSSValueID::kCenter>(tokens);
        if (!value) {
          value = css_parsing_utils::ConsumePercent(
              tokens, context_, CSSPrimitiveValue::ValueRange::kAll);
        }
        break;
      case CSSAnchorQueryType::kAnchorSize:
        value = css_parsing_utils::ConsumeIdent<
            CSSValueID::kWidth, CSSValueID::kHeight, CSSValueID::kBlock,
            CSSValueID::kInline, CSSValueID::kSelfBlock,
            CSSValueID::kSelfInline>(tokens);
        break;
    }
    if (!value) {
      return nullptr;
    }

    const CSSPrimitiveValue* fallback = nullptr;
    if (css_parsing_utils::ConsumeCommaIncludingWhitespace(tokens)) {
      fallback = css_parsing_utils::ConsumeLengthOrPercent(
          tokens, context_, CSSPrimitiveValue::ValueRange::kAll,
          css_parsing_utils::UnitlessQuirk::kForbid, allowed_anchor_queries_);
      if (!fallback) {
        return nullptr;
      }
    }

    tokens.ConsumeWhitespace();
    if (!tokens.AtEnd()) {
      return nullptr;
    }
    return MakeGarbageCollected<CSSMathExpressionAnchorQuery>(
        anchor_query_type, anchor_specifier, *value, fallback);
  }

  // https://drafts.csswg.org/css-values-5/#progress-func
  CSSMathExpressionNode* ParseProgressNotation(
      CSSValueID function_id,
      CSSParserTokenRange& tokens,
      int depth) {
    if (function_id != CSSValueID::kProgress) {
      return nullptr;
    }
    // <progress()> = progress(<calc-sum> from <calc-sum> to <calc-sum>)
    //                         0          1    2          3  4
    HeapVector<Member<const CSSMathExpressionNode>> nodes;
    tokens.ConsumeWhitespace();
    if (CSSMathExpressionNode* node = ParseValueExpression(tokens, depth)) {
      nodes.push_back(node);
    }
    if (tokens.ConsumeIncludingWhitespace().Id() != CSSValueID::kFrom) {
      return nullptr;
    }
    if (CSSMathExpressionNode* node = ParseValueExpression(tokens, depth)) {
      nodes.push_back(node);
    }
    if (tokens.ConsumeIncludingWhitespace().Id() != CSSValueID::kTo) {
      return nullptr;
    }
    if (CSSMathExpressionNode* node = ParseValueExpression(tokens, depth)) {
      nodes.push_back(node);
    }
    if (nodes.size() != 3u) {
      return nullptr;
    }
    if (!tokens.AtEnd()) {
      return nullptr;
    }
    if (nodes[0]->Category() != nodes[1]->Category() ||
        nodes[0]->Category() != nodes[2]->Category()) {
      return nullptr;
    }
    // Note: we don't need to resolve percents in such case,
    // as all the operands are numeric literals,
    // so p% / (t% - f%) will lose %.
    ProgressArgsSimplificationStatus status =
        CanEagerlySimplifyProgressArgs(nodes);
    if (status != ProgressArgsSimplificationStatus::kCanNotSimplify) {
      Vector<double> double_values;
      double_values.reserve(nodes.size());
      for (const CSSMathExpressionNode* operand : nodes) {
        if (status ==
            ProgressArgsSimplificationStatus::kAllArgsResolveToCanonical) {
          absl::optional<double> canonical_value =
              operand->ComputeValueInCanonicalUnit();
          CHECK(canonical_value.has_value());
          double_values.push_back(canonical_value.value());
        } else {
          CHECK(HasDoubleValue(operand->ResolvedUnitType()));
          double_values.push_back(operand->DoubleValue());
        }
      }
      double progress_value = (double_values[0] - double_values[1]) /
                              (double_values[2] - double_values[1]);
      return CSSMathExpressionNumericLiteral::Create(
          progress_value, CSSPrimitiveValue::UnitType::kNumber);
    }
    return MakeGarbageCollected<CSSMathExpressionOperation>(
        CalculationResultCategory::kCalcNumber, std::move(nodes),
        CSSMathOperator::kProgress);
  }

  CSSMathExpressionNode* ParseMathFunction(
      CSSValueID function_id,
      CSSParserTokenRange& tokens,
      int depth) {
    if (!IsSupportedMathFunction(function_id)) {
      return nullptr;
    }
    if (RuntimeEnabledFeatures::CSSAnchorPositioningEnabled()) {
      if (auto* anchor_query = ParseAnchorQuery(function_id, tokens)) {
        context_.Count(WebFeature::kCSSAnchorPositioning);
        return anchor_query;
      }
    }
    if (RuntimeEnabledFeatures::CSSProgressNotationEnabled()) {
      if (CSSMathExpressionNode* progress =
              ParseProgressNotation(function_id, tokens, depth)) {
        return progress;
      }
    }

    // "arguments" refers to comma separated ones.
    wtf_size_t min_argument_count = 1;
    wtf_size_t max_argument_count = std::numeric_limits<wtf_size_t>::max();

    switch (function_id) {
      case CSSValueID::kCalc:
      case CSSValueID::kWebkitCalc:
        max_argument_count = 1;
        break;
      case CSSValueID::kMin:
      case CSSValueID::kMax:
        break;
      case CSSValueID::kClamp:
        min_argument_count = 3;
        max_argument_count = 3;
        break;
      case CSSValueID::kSin:
      case CSSValueID::kCos:
      case CSSValueID::kTan:
      case CSSValueID::kAsin:
      case CSSValueID::kAcos:
      case CSSValueID::kAtan:
        max_argument_count = 1;
        break;
      case CSSValueID::kPow:
        DCHECK(RuntimeEnabledFeatures::CSSExponentialFunctionsEnabled());
        max_argument_count = 2;
        min_argument_count = 2;
        break;
      case CSSValueID::kExp:
      case CSSValueID::kSqrt:
        DCHECK(RuntimeEnabledFeatures::CSSExponentialFunctionsEnabled());
        max_argument_count = 1;
        break;
      case CSSValueID::kHypot:
        DCHECK(RuntimeEnabledFeatures::CSSExponentialFunctionsEnabled());
        max_argument_count = kMaxExpressionDepth;
        break;
      case CSSValueID::kLog:
        DCHECK(RuntimeEnabledFeatures::CSSExponentialFunctionsEnabled());
        max_argument_count = 2;
        break;
      case CSSValueID::kRound:
        DCHECK(RuntimeEnabledFeatures::CSSSteppedValueFunctionsEnabled());
        max_argument_count = 3;
        min_argument_count = 2;
        break;
      case CSSValueID::kMod:
      case CSSValueID::kRem:
        DCHECK(RuntimeEnabledFeatures::CSSSteppedValueFunctionsEnabled());
        max_argument_count = 2;
        min_argument_count = 2;
        break;
      case CSSValueID::kAtan2:
        max_argument_count = 2;
        min_argument_count = 2;
        break;
      case CSSValueID::kAbs:
      case CSSValueID::kSign:
        DCHECK(RuntimeEnabledFeatures::CSSSignRelatedFunctionsEnabled());
        max_argument_count = 1;
        min_argument_count = 1;
        break;
      // TODO(crbug.com/1284199): Support other math functions.
      default:
        break;
    }

    HeapVector<Member<const CSSMathExpressionNode>> nodes;
    // Parse the initial (optional) <rounding-strategy> argument to the round()
    // function.
    if (function_id == CSSValueID::kRound) {
      CSSMathExpressionNode* rounding_strategy = ParseRoundingStrategy(tokens);
      if (rounding_strategy) {
        nodes.push_back(rounding_strategy);
      }
    }

    while (!tokens.AtEnd() && nodes.size() < max_argument_count) {
      if (nodes.size()) {
        if (!css_parsing_utils::ConsumeCommaIncludingWhitespace(tokens)) {
          return nullptr;
        }
      }

      tokens.ConsumeWhitespace();
      CSSMathExpressionNode* node = ParseValueExpression(tokens, depth);
      if (!node) {
        return nullptr;
      }

      nodes.push_back(node);
    }

    if (!tokens.AtEnd() || nodes.size() < min_argument_count) {
      return nullptr;
    }

    switch (function_id) {
      case CSSValueID::kCalc:
      case CSSValueID::kWebkitCalc:
        return const_cast<CSSMathExpressionNode*>(nodes.front().Get());
      case CSSValueID::kMin:
      case CSSValueID::kMax:
      case CSSValueID::kClamp: {
        CSSMathOperator op = CSSMathOperator::kMin;
        if (function_id == CSSValueID::kMax) {
          op = CSSMathOperator::kMax;
        }
        if (function_id == CSSValueID::kClamp) {
          op = CSSMathOperator::kClamp;
        }
        CSSMathExpressionNode* node =
            CSSMathExpressionOperation::CreateComparisonFunctionSimplified(
                std::move(nodes), op);
        if (node) {
          context_.Count(WebFeature::kCSSComparisonFunctions);
        }
        return node;
      }
      case CSSValueID::kSin:
      case CSSValueID::kCos:
      case CSSValueID::kTan:
      case CSSValueID::kAsin:
      case CSSValueID::kAcos:
      case CSSValueID::kAtan:
      case CSSValueID::kAtan2:
        return CSSMathExpressionOperation::
            CreateTrigonometricFunctionSimplified(std::move(nodes),
                                                  function_id);
      case CSSValueID::kPow:
      case CSSValueID::kSqrt:
      case CSSValueID::kHypot:
      case CSSValueID::kLog:
      case CSSValueID::kExp:
        DCHECK(RuntimeEnabledFeatures::CSSExponentialFunctionsEnabled());
        return CSSMathExpressionOperation::CreateExponentialFunction(
            std::move(nodes), function_id);
      case CSSValueID::kRound:
      case CSSValueID::kMod:
      case CSSValueID::kRem: {
        DCHECK(RuntimeEnabledFeatures::CSSSteppedValueFunctionsEnabled());
        DCHECK_GE(nodes.size(), 2u);
        DCHECK_LE(nodes.size(), 3u);
        CSSMathOperator op;
        if (function_id == CSSValueID::kRound) {
          // If the first argument is a rounding strategy, use the specified
          // operation and drop the argument from the list of operands.
          const auto* maybe_rounding_strategy =
              DynamicTo<CSSMathExpressionOperation>(*nodes[0]);
          if (maybe_rounding_strategy &&
              maybe_rounding_strategy->IsRoundingStrategyKeyword()) {
            op = maybe_rounding_strategy->OperatorType();
            nodes.EraseAt(0);
          } else {
            op = CSSMathOperator::kRoundNearest;
          }
          if (nodes.size() != 2) {
            return nullptr;
          }
        } else if (function_id == CSSValueID::kMod) {
          op = CSSMathOperator::kMod;
        } else {
          op = CSSMathOperator::kRem;
        }
        return CSSMathExpressionOperation::CreateSteppedValueFunction(
            std::move(nodes), op);
      }
      case CSSValueID::kAbs:
      case CSSValueID::kSign:
        // TODO(seokho): Relative and Percent values cannot be evaluated at the
        // parsing time. So we should implement cannot be simplified value
        // using CalculationExpressionNode
        DCHECK(RuntimeEnabledFeatures::CSSSignRelatedFunctionsEnabled());
        return CSSMathExpressionOperation::CreateSignRelatedFunction(
            std::move(nodes), function_id);

      // TODO(crbug.com/1284199): Support other math functions.
      default:
        return nullptr;
    }
  }

 private:
  CSSMathExpressionNode* ParseValue(CSSParserTokenRange& tokens) {
    CSSParserToken token = tokens.ConsumeIncludingWhitespace();
    if (token.Id() == CSSValueID::kInfinity) {
      return CSSMathExpressionNumericLiteral::Create(
          std::numeric_limits<double>::infinity(),
          CSSPrimitiveValue::UnitType::kNumber);
    }
    if (token.Id() == CSSValueID::kNegativeInfinity) {
      return CSSMathExpressionNumericLiteral::Create(
          -std::numeric_limits<double>::infinity(),
          CSSPrimitiveValue::UnitType::kNumber);
    }
    if (token.Id() == CSSValueID::kNan) {
      return CSSMathExpressionNumericLiteral::Create(
          std::numeric_limits<double>::quiet_NaN(),
          CSSPrimitiveValue::UnitType::kNumber);
    }
    if (token.Id() == CSSValueID::kPi) {
      return CSSMathExpressionNumericLiteral::Create(
          M_PI, CSSPrimitiveValue::UnitType::kNumber);
    }
    if (token.Id() == CSSValueID::kE) {
      return CSSMathExpressionNumericLiteral::Create(
          M_E, CSSPrimitiveValue::UnitType::kNumber);
    }
    if (!(token.GetType() == kNumberToken ||
          (token.GetType() == kPercentageToken && is_percentage_allowed_) ||
          token.GetType() == kDimensionToken)) {
      // For relative color syntax. Swap in the associated value of a color
      // channel here. e.g. color(from color(srgb 1 0 0) calc(r * 2) 0 0) should
      // swap in "1" for the value of "r" in the calc expression.
      if (color_channel_keyword_values_.Contains(token.Id())) {
        return CSSMathExpressionNumericLiteral::Create(
            color_channel_keyword_values_.at(token.Id()),
            CSSPrimitiveValue::UnitType::kNumber);
      }
      return nullptr;
    }

    CSSPrimitiveValue::UnitType type = token.GetUnitType();
    if (UnitCategory(type) == kCalcOther) {
      return nullptr;
    }

    return CSSMathExpressionNumericLiteral::Create(
        CSSNumericLiteralValue::Create(token.NumericValue(), type));
  }

  CSSMathExpressionNode* ParseRoundingStrategy(CSSParserTokenRange& tokens) {
    CSSMathOperator rounding_op = CSSMathOperator::kInvalid;
    switch (tokens.Peek().Id()) {
      case CSSValueID::kNearest:
        rounding_op = CSSMathOperator::kRoundNearest;
        break;
      case CSSValueID::kUp:
        rounding_op = CSSMathOperator::kRoundUp;
        break;
      case CSSValueID::kDown:
        rounding_op = CSSMathOperator::kRoundDown;
        break;
      case CSSValueID::kToZero:
        rounding_op = CSSMathOperator::kRoundToZero;
        break;
      default:
        return nullptr;
    }
    tokens.ConsumeIncludingWhitespace();
    return MakeGarbageCollected<CSSMathExpressionOperation>(
        CalculationResultCategory::kCalcNumber, rounding_op);
  }

  CSSMathExpressionNode* ParseValueTerm(CSSParserTokenRange& tokens,
                                        int depth) {
    if (tokens.AtEnd()) {
      return nullptr;
    }

    if (tokens.Peek().GetType() == kLeftParenthesisToken ||
        tokens.Peek().FunctionId() == CSSValueID::kCalc) {
      CSSParserTokenRange inner_range = tokens.ConsumeBlock();
      tokens.ConsumeWhitespace();
      inner_range.ConsumeWhitespace();
      CSSMathExpressionNode* result = ParseValueExpression(inner_range, depth);
      if (!result || !inner_range.AtEnd()) {
        return nullptr;
      }
      result->SetIsNestedCalc();
      return result;
    }

    if (tokens.Peek().GetType() == kFunctionToken) {
      CSSValueID function_id = tokens.Peek().FunctionId();
      CSSParserTokenRange inner_range = tokens.ConsumeBlock();
      tokens.ConsumeWhitespace();
      inner_range.ConsumeWhitespace();
      return ParseMathFunction(function_id, inner_range, depth);
    }

    return ParseValue(tokens);
  }

  CSSMathExpressionNode* ParseValueMultiplicativeExpression(
      CSSParserTokenRange& tokens,
      int depth) {
    if (tokens.AtEnd()) {
      return nullptr;
    }

    CSSMathExpressionNode* result = ParseValueTerm(tokens, depth);
    if (!result) {
      return nullptr;
    }

    while (!tokens.AtEnd()) {
      CSSMathOperator math_operator = ParseCSSArithmeticOperator(tokens.Peek());
      if (math_operator != CSSMathOperator::kMultiply &&
          math_operator != CSSMathOperator::kDivide) {
        break;
      }
      tokens.ConsumeIncludingWhitespace();

      CSSMathExpressionNode* rhs = ParseValueTerm(tokens, depth);
      if (!rhs) {
        return nullptr;
      }

      result = CSSMathExpressionOperation::CreateArithmeticOperationSimplified(
          result, rhs, math_operator);

      if (!result) {
        return nullptr;
      }
    }

    return result;
  }

  CSSMathExpressionNode* ParseAdditiveValueExpression(
      CSSParserTokenRange& tokens,
      int depth) {
    if (tokens.AtEnd()) {
      return nullptr;
    }

    CSSMathExpressionNode* result =
        ParseValueMultiplicativeExpression(tokens, depth);
    if (!result) {
      return nullptr;
    }

    while (!tokens.AtEnd()) {
      CSSMathOperator math_operator = ParseCSSArithmeticOperator(tokens.Peek());
      if (math_operator != CSSMathOperator::kAdd &&
          math_operator != CSSMathOperator::kSubtract) {
        break;
      }
      if ((&tokens.Peek() - 1)->GetType() != kWhitespaceToken) {
        return nullptr;  // calc(1px+ 2px) is invalid
      }
      tokens.Consume();
      if (tokens.Peek().GetType() != kWhitespaceToken) {
        return nullptr;  // calc(1px +2px) is invalid
      }
      tokens.ConsumeIncludingWhitespace();

      CSSMathExpressionNode* rhs =
          ParseValueMultiplicativeExpression(tokens, depth);
      if (!rhs) {
        return nullptr;
      }

      result = CSSMathExpressionOperation::CreateArithmeticOperationSimplified(
          result, rhs, math_operator);

      if (!result) {
        return nullptr;
      }
    }

    if (auto* operation = DynamicTo<CSSMathExpressionOperation>(result)) {
      if (operation->IsAddOrSubtract()) {
        result = MaybeSimplifySumNode(operation);
      }
    }

    return result;
  }

  CSSMathExpressionNode* ParseValueExpression(
      CSSParserTokenRange& tokens,
      int depth) {
    if (++depth > kMaxExpressionDepth) {
      return nullptr;
    }
    return ParseAdditiveValueExpression(tokens, depth);
  }

  const CSSParserContext& context_;
  const CSSAnchorQueryTypes allowed_anchor_queries_;
  // Indicates if percentages are allowed.
  const bool is_percentage_allowed_;
  const HashMap<CSSValueID, double>& color_channel_keyword_values_;
};

scoped_refptr<const CalculationValue> CSSMathExpressionNode::ToCalcValue(
    const CSSLengthResolver& length_resolver,
    Length::ValueRange range,
    bool allows_negative_percentage_reference) const {
  if (auto maybe_pixels_and_percent = ToPixelsAndPercent(length_resolver)) {
    // Clamping if pixels + percent could result in NaN. In special case,
    // inf px + inf % could evaluate to nan when
    // allows_negative_percentage_reference is true.
    if (IsNaN(*maybe_pixels_and_percent,
              allows_negative_percentage_reference)) {
      maybe_pixels_and_percent = CreateClampedSamePixelsAndPercent(
          std::numeric_limits<float>::quiet_NaN());
    } else {
      maybe_pixels_and_percent->pixels =
          CSSValueClampingUtils::ClampLength(maybe_pixels_and_percent->pixels);
      maybe_pixels_and_percent->percent =
          CSSValueClampingUtils::ClampLength(maybe_pixels_and_percent->percent);
    }
    return CalculationValue::Create(*maybe_pixels_and_percent, range);
  }

  auto value = ToCalculationExpression(length_resolver);
  absl::optional<PixelsAndPercent> evaluated_value =
      EvaluateValueIfNaNorInfinity(value, allows_negative_percentage_reference);
  if (evaluated_value.has_value()) {
    return CalculationValue::Create(evaluated_value.value(), range);
  }
  return CalculationValue::CreateSimplified(value, range);
}

// static
CSSMathExpressionNode* CSSMathExpressionNode::Create(
    const CalculationValue& calc) {
  if (calc.IsExpression()) {
    return Create(*calc.GetOrCreateExpression());
  }
  return Create(calc.GetPixelsAndPercent());
}

// static
CSSMathExpressionNode* CSSMathExpressionNode::Create(PixelsAndPercent value) {
  double percent = value.percent;
  double pixels = value.pixels;
  if (!value.has_explicit_pixels) {
    CHECK(!pixels);
    return CSSMathExpressionNumericLiteral::Create(
        percent, CSSPrimitiveValue::UnitType::kPercentage);
  }
  if (!value.has_explicit_percent) {
    CHECK(!percent);
    return CSSMathExpressionNumericLiteral::Create(
        pixels, CSSPrimitiveValue::UnitType::kPixels);
  }
  CSSMathOperator op = CSSMathOperator::kAdd;
  if (pixels < 0) {
    pixels = -pixels;
    op = CSSMathOperator::kSubtract;
  }
  return CSSMathExpressionOperation::CreateArithmeticOperation(
      CSSMathExpressionNumericLiteral::Create(CSSNumericLiteralValue::Create(
          percent, CSSPrimitiveValue::UnitType::kPercentage)),
      CSSMathExpressionNumericLiteral::Create(CSSNumericLiteralValue::Create(
          pixels, CSSPrimitiveValue::UnitType::kPixels)),
      op);
}

namespace {

CSSValue* AnchorQueryValueToCSSValue(
    const CalculationExpressionAnchorQueryNode& anchor_query) {
  if (anchor_query.Type() == CSSAnchorQueryType::kAnchor) {
    switch (anchor_query.AnchorSide()) {
      case CSSAnchorValue::kAuto:
        return CSSIdentifierValue::Create(CSSValueID::kAuto);
      case CSSAnchorValue::kAutoSame:
        return CSSIdentifierValue::Create(CSSValueID::kAutoSame);
      case CSSAnchorValue::kTop:
        return CSSIdentifierValue::Create(CSSValueID::kTop);
      case CSSAnchorValue::kLeft:
        return CSSIdentifierValue::Create(CSSValueID::kLeft);
      case CSSAnchorValue::kRight:
        return CSSIdentifierValue::Create(CSSValueID::kRight);
      case CSSAnchorValue::kBottom:
        return CSSIdentifierValue::Create(CSSValueID::kBottom);
      case CSSAnchorValue::kStart:
        return CSSIdentifierValue::Create(CSSValueID::kStart);
      case CSSAnchorValue::kEnd:
        return CSSIdentifierValue::Create(CSSValueID::kEnd);
      case CSSAnchorValue::kSelfStart:
        return CSSIdentifierValue::Create(CSSValueID::kSelfStart);
      case CSSAnchorValue::kSelfEnd:
        return CSSIdentifierValue::Create(CSSValueID::kSelfEnd);
      case CSSAnchorValue::kCenter:
        return CSSIdentifierValue::Create(CSSValueID::kCenter);
      case CSSAnchorValue::kPercentage:
        return CSSNumericLiteralValue::Create(
            anchor_query.AnchorSidePercentage(),
            CSSPrimitiveValue::UnitType::kPercentage);
    }
  }

  DCHECK_EQ(anchor_query.Type(), CSSAnchorQueryType::kAnchorSize);
  switch (anchor_query.AnchorSize()) {
    case CSSAnchorSizeValue::kWidth:
      return CSSIdentifierValue::Create(CSSValueID::kWidth);
    case CSSAnchorSizeValue::kHeight:
      return CSSIdentifierValue::Create(CSSValueID::kHeight);
    case CSSAnchorSizeValue::kBlock:
      return CSSIdentifierValue::Create(CSSValueID::kBlock);
    case CSSAnchorSizeValue::kInline:
      return CSSIdentifierValue::Create(CSSValueID::kInline);
    case CSSAnchorSizeValue::kSelfBlock:
      return CSSIdentifierValue::Create(CSSValueID::kSelfBlock);
    case CSSAnchorSizeValue::kSelfInline:
      return CSSIdentifierValue::Create(CSSValueID::kSelfInline);
  }
}

}  // namespace

// static
CSSMathExpressionNode* CSSMathExpressionNode::Create(
    const CalculationExpressionNode& node) {
  if (node.IsPixelsAndPercent()) {
    const auto& pixels_and_percent =
        To<CalculationExpressionPixelsAndPercentNode>(node);
    return Create(pixels_and_percent.GetPixelsAndPercent());
  }

  if (node.IsIdentifier()) {
    return CSSMathExpressionIdentifierLiteral::Create(
        To<CalculationExpressionIdentifierNode>(node).Value());
  }

  if (node.IsNumber()) {
    return CSSMathExpressionNumericLiteral::Create(
        To<CalculationExpressionNumberNode>(node).Value(),
        CSSPrimitiveValue::UnitType::kNumber);
  }

  if (node.IsAnchorQuery()) {
    const auto& anchor_query = To<CalculationExpressionAnchorQueryNode>(node);
    CSSAnchorQueryType type = anchor_query.Type() == CSSAnchorQueryType::kAnchor
                                  ? CSSAnchorQueryType::kAnchor
                                  : CSSAnchorQueryType::kAnchorSize;
    const CSSValue* anchor_specifier = nullptr;
    if (anchor_query.AnchorSpecifier().IsImplicit()) {
      anchor_specifier = CSSIdentifierValue::Create(CSSValueID::kImplicit);
    } else if (anchor_query.AnchorSpecifier().IsNamed()) {
      const ScopedCSSName& name = anchor_query.AnchorSpecifier().GetName();
      anchor_specifier = To<CSSCustomIdentValue>(
          &MakeGarbageCollected<CSSCustomIdentValue>(name.GetName())
               ->EnsureScopedValue(name.GetTreeScope()));
    }
    CSSValue* value = AnchorQueryValueToCSSValue(anchor_query);
    CSSPrimitiveValue* fallback = CSSPrimitiveValue::CreateFromLength(
        anchor_query.GetFallback(), /* zoom */ 1);
    return MakeGarbageCollected<CSSMathExpressionAnchorQuery>(
        type, anchor_specifier, *value, fallback);
  }

  DCHECK(node.IsOperation());

  const auto& operation = To<CalculationExpressionOperationNode>(node);
  const auto& children = operation.GetChildren();
  const auto calc_op = operation.GetOperator();
  switch (calc_op) {
    case CalculationOperator::kMultiply: {
      DCHECK_EQ(children.size(), 2u);
      return CSSMathExpressionOperation::CreateArithmeticOperation(
          Create(*children.front()), Create(*children.back()),
          CSSMathOperator::kMultiply);
    }
    case CalculationOperator::kAdd:
    case CalculationOperator::kSubtract: {
      DCHECK_EQ(children.size(), 2u);
      auto* lhs = Create(*children[0]);
      auto* rhs = Create(*children[1]);
      CSSMathOperator op = (calc_op == CalculationOperator::kAdd)
                               ? CSSMathOperator::kAdd
                               : CSSMathOperator::kSubtract;
      return CSSMathExpressionOperation::CreateArithmeticOperation(lhs, rhs,
                                                                   op);
    }
    case CalculationOperator::kMin:
    case CalculationOperator::kMax: {
      DCHECK(children.size());
      CSSMathExpressionOperation::Operands operands;
      for (const auto& child : children) {
        operands.push_back(Create(*child));
      }
      CSSMathOperator op = (calc_op == CalculationOperator::kMin)
                               ? CSSMathOperator::kMin
                               : CSSMathOperator::kMax;
      return CSSMathExpressionOperation::CreateComparisonFunction(
          std::move(operands), op);
    }
    case CalculationOperator::kClamp: {
      DCHECK_EQ(children.size(), 3u);
      CSSMathExpressionOperation::Operands operands;
      for (const auto& child : children) {
        operands.push_back(Create(*child));
      }
      return CSSMathExpressionOperation::CreateComparisonFunction(
          std::move(operands), CSSMathOperator::kClamp);
    }
    case CalculationOperator::kRoundNearest:
    case CalculationOperator::kRoundUp:
    case CalculationOperator::kRoundDown:
    case CalculationOperator::kRoundToZero:
    case CalculationOperator::kMod:
    case CalculationOperator::kRem: {
      DCHECK_EQ(children.size(), 2u);
      CSSMathExpressionOperation::Operands operands;
      for (const auto& child : children) {
        operands.push_back(Create(*child));
      }
      CSSMathOperator op;
      if (calc_op == CalculationOperator::kRoundNearest) {
        op = CSSMathOperator::kRoundNearest;
      } else if (calc_op == CalculationOperator::kRoundUp) {
        op = CSSMathOperator::kRoundUp;
      } else if (calc_op == CalculationOperator::kRoundDown) {
        op = CSSMathOperator::kRoundDown;
      } else if (calc_op == CalculationOperator::kRoundToZero) {
        op = CSSMathOperator::kRoundToZero;
      } else if (calc_op == CalculationOperator::kMod) {
        op = CSSMathOperator::kMod;
      } else {
        op = CSSMathOperator::kRem;
      }
      return CSSMathExpressionOperation::CreateSteppedValueFunction(
          std::move(operands), op);
    }
    case CalculationOperator::kHypot: {
      DCHECK_GE(children.size(), 1u);
      CSSMathExpressionOperation::Operands operands;
      for (const auto& child : children) {
        operands.push_back(Create(*child));
      }
      return CSSMathExpressionOperation::CreateExponentialFunction(
          std::move(operands), CSSValueID::kHypot);
    }
    case CalculationOperator::kAbs:
    case CalculationOperator::kSign: {
      DCHECK_EQ(children.size(), 1u);
      CSSMathExpressionOperation::Operands operands;
      operands.push_back(Create(*children.front()));
      CSSValueID op = calc_op == CalculationOperator::kAbs ? CSSValueID::kAbs
                                                           : CSSValueID::kSign;
      return CSSMathExpressionOperation::CreateSignRelatedFunction(
          std::move(operands), op);
    }
    case CalculationOperator::kProgress: {
      CHECK_EQ(children.size(), 3u);
      CSSMathExpressionOperation::Operands operands;
      operands.push_back(Create(*children.front()));
      operands.push_back(Create(*children[1]));
      operands.push_back(Create(*children.back()));
      return MakeGarbageCollected<CSSMathExpressionOperation>(
          CalculationResultCategory::kCalcNumber, std::move(operands),
          CSSMathOperator::kProgress);
    }
    case CalculationOperator::kInvalid:
      NOTREACHED();
      return nullptr;
  }
}

// static
CSSMathExpressionNode* CSSMathExpressionNode::ParseMathFunction(
    CSSValueID function_id,
    CSSParserTokenRange tokens,
    const CSSParserContext& context,
    const bool is_percentage_allowed,
    CSSAnchorQueryTypes allowed_anchor_queries,
    const HashMap<CSSValueID, double>& color_channel_keyword_values) {
  CSSMathExpressionNodeParser parser(context, is_percentage_allowed,
                                     allowed_anchor_queries,
                                     color_channel_keyword_values);
  CSSMathExpressionNode* result =
      parser.ParseMathFunction(function_id, tokens, 0);

  // TODO(pjh0718): Do simplificiation for result above.
  return result;
}

}  // namespace blink

WTF_ALLOW_CLEAR_UNUSED_SLOTS_WITH_MEM_FUNCTIONS(
    blink::CSSMathExpressionNodeWithOperator)
