// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/css/media_query_exp.h"
#include "third_party/blink/renderer/core/css/css_test_helpers.h"
#include "third_party/blink/renderer/core/dom/document.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace blink {

namespace {

MediaQueryExpValue IdentValue(CSSValueID id) {
  return MediaQueryExpValue(id);
}

MediaQueryExpValue RatioValue(unsigned numerator, unsigned denominator) {
  return MediaQueryExpValue(numerator, denominator);
}

MediaQueryExpValue PxValue(double value) {
  return MediaQueryExpValue(value, CSSPrimitiveValue::UnitType::kPixels);
}

MediaQueryExpValue EmValue(double value) {
  return MediaQueryExpValue(value, CSSPrimitiveValue::UnitType::kEms);
}

MediaQueryExpValue RemValue(double value) {
  return MediaQueryExpValue(value, CSSPrimitiveValue::UnitType::kRems);
}

MediaQueryExpValue DvhValue(double value) {
  return MediaQueryExpValue(
      value, CSSPrimitiveValue::UnitType::kDynamicViewportHeight);
}

MediaQueryExpValue SvhValue(double value) {
  return MediaQueryExpValue(value,
                            CSSPrimitiveValue::UnitType::kSmallViewportHeight);
}

MediaQueryExpValue LvhValue(double value) {
  return MediaQueryExpValue(value,
                            CSSPrimitiveValue::UnitType::kLargeViewportHeight);
}

MediaQueryExpValue VhValue(double value) {
  return MediaQueryExpValue(value,
                            CSSPrimitiveValue::UnitType::kViewportHeight);
}

MediaQueryExpValue CqhValue(double value) {
  return MediaQueryExpValue(value,
                            CSSPrimitiveValue::UnitType::kContainerHeight);
}

MediaQueryExpValue CssValue(const CSSPrimitiveValue& value) {
  return MediaQueryExpValue(value);
}

MediaQueryExpValue InvalidValue() {
  return MediaQueryExpValue();
}

MediaQueryExpComparison NoCmp(MediaQueryExpValue v) {
  return MediaQueryExpComparison(v);
}

MediaQueryExpComparison LtCmp(MediaQueryExpValue v) {
  return MediaQueryExpComparison(v, MediaQueryOperator::kLt);
}

MediaQueryExpComparison LeCmp(MediaQueryExpValue v) {
  return MediaQueryExpComparison(v, MediaQueryOperator::kLe);
}

MediaQueryExpComparison GtCmp(MediaQueryExpValue v) {
  return MediaQueryExpComparison(v, MediaQueryOperator::kGt);
}

MediaQueryExpComparison GeCmp(MediaQueryExpValue v) {
  return MediaQueryExpComparison(v, MediaQueryOperator::kGe);
}

MediaQueryExpComparison EqCmp(MediaQueryExpValue v) {
  return MediaQueryExpComparison(v, MediaQueryOperator::kEq);
}

MediaQueryExp LeftExp(String feature, MediaQueryExpComparison cmp) {
  return MediaQueryExp::Create(feature,
                               MediaQueryExpBounds(cmp, NoCmp(InvalidValue())));
}

MediaQueryExp RightExp(String feature, MediaQueryExpComparison cmp) {
  return MediaQueryExp::Create(feature,
                               MediaQueryExpBounds(NoCmp(InvalidValue()), cmp));
}

MediaQueryExp PairExp(String feature,
                      MediaQueryExpComparison left,
                      MediaQueryExpComparison right) {
  return MediaQueryExp::Create(feature, MediaQueryExpBounds(left, right));
}

const MediaQueryExpNode* FeatureNode(MediaQueryExp expr) {
  return MakeGarbageCollected<MediaQueryFeatureExpNode>(expr);
}

const MediaQueryExpNode* EnclosedFeatureNode(MediaQueryExp expr) {
  return MediaQueryExpNode::Nested(
      MakeGarbageCollected<MediaQueryFeatureExpNode>(expr));
}

const MediaQueryExpNode* NestedNode(const MediaQueryExpNode* child) {
  return MediaQueryExpNode::Nested(child);
}

const MediaQueryExpNode* FunctionNode(const MediaQueryExpNode* child,
                                      const AtomicString& name) {
  return MediaQueryExpNode::Function(child, name);
}

const MediaQueryExpNode* NotNode(const MediaQueryExpNode* operand) {
  return MediaQueryExpNode::Not(operand);
}

const MediaQueryExpNode* AndNode(const MediaQueryExpNode* left,
                                 const MediaQueryExpNode* right) {
  return MediaQueryExpNode::And(left, right);
}

const MediaQueryExpNode* OrNode(const MediaQueryExpNode* left,
                                const MediaQueryExpNode* right) {
  return MediaQueryExpNode::Or(left, right);
}

const MediaQueryExpNode* UnknownNode(String string) {
  return MakeGarbageCollected<MediaQueryUnknownExpNode>(string);
}

}  // namespace

TEST(MediaQueryExpTest, ValuesType) {
  EXPECT_TRUE(IdentValue(CSSValueID::kTop).IsId());
  EXPECT_TRUE(PxValue(10).IsNumeric());
  EXPECT_TRUE(RatioValue(0, 1).IsRatio());
}

TEST(MediaQueryExpTest, ValueEquality) {
  EXPECT_EQ(PxValue(10), PxValue(10));
  EXPECT_EQ(EmValue(10), EmValue(10));
  EXPECT_EQ(IdentValue(CSSValueID::kTop), IdentValue(CSSValueID::kTop));
  EXPECT_EQ(IdentValue(CSSValueID::kTop), IdentValue(CSSValueID::kTop));
  EXPECT_EQ(RatioValue(1, 2), RatioValue(1, 2));

  // Mismatched values:
  EXPECT_NE(PxValue(10), PxValue(20));
  EXPECT_NE(EmValue(20), EmValue(10));
  EXPECT_NE(IdentValue(CSSValueID::kTop), IdentValue(CSSValueID::kLeft));
  EXPECT_NE(RatioValue(16, 9), RatioValue(4, 3));

  // Mismatched unit:
  EXPECT_NE(PxValue(10), EmValue(10));

  // Mismatched types:
  EXPECT_NE(PxValue(10), IdentValue(CSSValueID::kTop));
  EXPECT_NE(PxValue(10), RatioValue(1, 2));

  // Mismatched validity:
  EXPECT_NE(PxValue(10), InvalidValue());
  EXPECT_NE(PxValue(0), InvalidValue());
  EXPECT_NE(RatioValue(0, 1), InvalidValue());
}

TEST(MediaQueryExpTest, ComparisonEquality) {
  auto px1 = PxValue(10.0);
  auto px2 = PxValue(20.0);

  EXPECT_EQ(LtCmp(px1), LtCmp(px1));

  EXPECT_NE(LtCmp(px1), LeCmp(px1));
  EXPECT_NE(LtCmp(px1), LtCmp(px2));
}

TEST(MediaQueryExpTest, BoundaryEquality) {
  auto px1 = PxValue(10.0);
  auto px2 = PxValue(20.0);

  EXPECT_EQ(MediaQueryExpBounds(LtCmp(px1), LeCmp(px1)),
            MediaQueryExpBounds(LtCmp(px1), LeCmp(px1)));

  EXPECT_NE(MediaQueryExpBounds(LtCmp(px1), LeCmp(px1)),
            MediaQueryExpBounds(GtCmp(px1), LeCmp(px1)));
  EXPECT_NE(MediaQueryExpBounds(LtCmp(px1), LeCmp(px1)),
            MediaQueryExpBounds(LtCmp(px1), GeCmp(px1)));
  EXPECT_NE(MediaQueryExpBounds(LtCmp(px1), LeCmp(px2)),
            MediaQueryExpBounds(LtCmp(px1), LeCmp(px1)));
}

TEST(MediaQueryExpTest, ExpEquality) {
  auto px1 = PxValue(10.0);
  auto px2 = PxValue(20.0);

  EXPECT_EQ(LeftExp("width", LtCmp(px1)), LeftExp("width", LtCmp(px1)));

  EXPECT_NE(LeftExp("width", LtCmp(px1)), LeftExp("height", LtCmp(px1)));
  EXPECT_NE(LeftExp("width", LtCmp(px2)), LeftExp("width", LtCmp(px1)));
  EXPECT_NE(LeftExp("width", LtCmp(px1)), RightExp("width", LtCmp(px1)));
  EXPECT_NE(LeftExp("width", LtCmp(px1)), LeftExp("width", GtCmp(px1)));
}

TEST(MediaQueryExpTest, Serialize) {
  // Boolean feature:
  EXPECT_EQ("color", RightExp("color", NoCmp(InvalidValue())).Serialize());

  auto px = PxValue(10.0);

  // Plain feature:
  EXPECT_EQ("width: 10px", RightExp("width", NoCmp(px)).Serialize());

  // Ranges:
  EXPECT_EQ("width = 10px", RightExp("width", EqCmp(px)).Serialize());
  EXPECT_EQ("width < 10px", RightExp("width", LtCmp(px)).Serialize());
  EXPECT_EQ("width <= 10px", RightExp("width", LeCmp(px)).Serialize());
  EXPECT_EQ("width > 10px", RightExp("width", GtCmp(px)).Serialize());
  EXPECT_EQ("width >= 10px", RightExp("width", GeCmp(px)).Serialize());

  EXPECT_EQ("10px = width", LeftExp("width", EqCmp(px)).Serialize());
  EXPECT_EQ("10px < width", LeftExp("width", LtCmp(px)).Serialize());
  EXPECT_EQ("10px <= width", LeftExp("width", LeCmp(px)).Serialize());
  EXPECT_EQ("10px > width", LeftExp("width", GtCmp(px)).Serialize());
  EXPECT_EQ("10px >= width", LeftExp("width", GeCmp(px)).Serialize());

  EXPECT_EQ(
      "10px < width < 20px",
      PairExp("width", LtCmp(PxValue(10.0)), LtCmp(PxValue(20.0))).Serialize());
  EXPECT_EQ(
      "20px > width > 10px",
      PairExp("width", GtCmp(PxValue(20.0)), GtCmp(PxValue(10.0))).Serialize());
  EXPECT_EQ(
      "10px <= width <= 20px",
      PairExp("width", LeCmp(PxValue(10.0)), LeCmp(PxValue(20.0))).Serialize());
  EXPECT_EQ(
      "20px > width >= 10px",
      PairExp("width", GtCmp(PxValue(20.0)), GeCmp(PxValue(10.0))).Serialize());
}

TEST(MediaQueryExpTest, SerializeNode) {
  EXPECT_EQ("width < 10px",
            FeatureNode(RightExp("width", LtCmp(PxValue(10))))->Serialize());

  EXPECT_EQ(
      "(width < 10px)",
      EnclosedFeatureNode(RightExp("width", LtCmp(PxValue(10))))->Serialize());

  EXPECT_EQ(
      "(width < 10px) and (11px >= thing) and (height = 12px)",
      AndNode(
          EnclosedFeatureNode(RightExp("width", LtCmp(PxValue(10)))),
          AndNode(EnclosedFeatureNode(LeftExp("thing", GeCmp(PxValue(11)))),
                  EnclosedFeatureNode(RightExp("height", EqCmp(PxValue(12))))))
          ->Serialize());

  // Same as previous, but with 'or' instead:
  EXPECT_EQ(
      "(width < 10px) or (11px >= thing) or (height = 12px)",
      OrNode(
          EnclosedFeatureNode(RightExp("width", LtCmp(PxValue(10)))),
          OrNode(EnclosedFeatureNode(LeftExp("thing", GeCmp(PxValue(11)))),
                 EnclosedFeatureNode(RightExp("height", EqCmp(PxValue(12))))))
          ->Serialize());

  EXPECT_EQ("not (width < 10px)",
            NotNode(EnclosedFeatureNode(RightExp("width", LtCmp(PxValue(10)))))
                ->Serialize());

  EXPECT_EQ(
      "((width < 10px))",
      NestedNode(EnclosedFeatureNode(RightExp("width", LtCmp(PxValue(10)))))
          ->Serialize());
  EXPECT_EQ("(((width < 10px)))",
            NestedNode(NestedNode(EnclosedFeatureNode(
                           RightExp("width", LtCmp(PxValue(10))))))
                ->Serialize());

  EXPECT_EQ(
      "not ((11px >= thing) and (height = 12px))",
      NotNode(NestedNode(AndNode(
                  EnclosedFeatureNode(LeftExp("thing", GeCmp(PxValue(11)))),
                  EnclosedFeatureNode(RightExp("height", EqCmp(PxValue(12)))))))
          ->Serialize());

  EXPECT_EQ("special(width < 10px)",
            FunctionNode(FeatureNode(RightExp("width", LtCmp(PxValue(10)))),
                         "special")
                ->Serialize());
  EXPECT_EQ(
      "special((width < 10px))",
      FunctionNode(EnclosedFeatureNode(RightExp("width", LtCmp(PxValue(10)))),
                   "special")
          ->Serialize());
  EXPECT_EQ(
      "special((11px >= thing) and (height = 12px))",
      FunctionNode(
          AndNode(EnclosedFeatureNode(LeftExp("thing", GeCmp(PxValue(11)))),
                  EnclosedFeatureNode(RightExp("height", EqCmp(PxValue(12))))),
          "special")
          ->Serialize());
}

TEST(MediaQueryExpTest, CollectExpressions) {
  MediaQueryExp width_lt10 = RightExp("width", LtCmp(PxValue(10)));
  MediaQueryExp height_lt10 = RightExp("height", LtCmp(PxValue(10)));

  // (width < 10px)
  {
    HeapVector<MediaQueryExp> expressions;
    EnclosedFeatureNode(width_lt10)->CollectExpressions(expressions);
    ASSERT_EQ(1u, expressions.size());
    EXPECT_EQ(width_lt10, expressions[0]);
  }

  // (width < 10px) and (height < 10px)
  {
    HeapVector<MediaQueryExp> expressions;
    AndNode(EnclosedFeatureNode(width_lt10), EnclosedFeatureNode(height_lt10))
        ->CollectExpressions(expressions);
    ASSERT_EQ(2u, expressions.size());
    EXPECT_EQ(width_lt10, expressions[0]);
    EXPECT_EQ(height_lt10, expressions[1]);
  }

  // (width < 10px) or (height < 10px)
  {
    HeapVector<MediaQueryExp> expressions;
    OrNode(EnclosedFeatureNode(width_lt10), EnclosedFeatureNode(height_lt10))
        ->CollectExpressions(expressions);
    ASSERT_EQ(2u, expressions.size());
    EXPECT_EQ(width_lt10, expressions[0]);
    EXPECT_EQ(height_lt10, expressions[1]);
  }

  // ((width < 10px))
  {
    HeapVector<MediaQueryExp> expressions;
    NestedNode(EnclosedFeatureNode(width_lt10))
        ->CollectExpressions(expressions);
    ASSERT_EQ(1u, expressions.size());
    EXPECT_EQ(width_lt10, expressions[0]);
  }

  // not (width < 10px)
  {
    HeapVector<MediaQueryExp> expressions;
    NotNode(EnclosedFeatureNode(width_lt10))->CollectExpressions(expressions);
    ASSERT_EQ(1u, expressions.size());
    EXPECT_EQ(width_lt10, expressions[0]);
  }

  // unknown
  {
    HeapVector<MediaQueryExp> expressions;
    UnknownNode("foo")->CollectExpressions(expressions);
    EXPECT_EQ(0u, expressions.size());
  }
}

TEST(MediaQueryExpTest, UnitFlags) {
  // width < 10px
  EXPECT_EQ(MediaQueryExpValue::UnitFlags::kNone,
            RightExp("width", LtCmp(PxValue(10.0))).GetUnitFlags());
  // width < 10em
  EXPECT_EQ(MediaQueryExpValue::UnitFlags::kFontRelative,
            RightExp("width", LtCmp(EmValue(10.0))).GetUnitFlags());
  // width < 10rem
  EXPECT_EQ(MediaQueryExpValue::UnitFlags::kRootFontRelative,
            RightExp("width", LtCmp(RemValue(10.0))).GetUnitFlags());
  // 10px < width
  EXPECT_EQ(MediaQueryExpValue::UnitFlags::kNone,
            LeftExp("width", LtCmp(PxValue(10.0))).GetUnitFlags());
  // 10em <  width
  EXPECT_EQ(MediaQueryExpValue::UnitFlags::kFontRelative,
            LeftExp("width", LtCmp(EmValue(10.0))).GetUnitFlags());
  // 10rem < width
  EXPECT_EQ(MediaQueryExpValue::UnitFlags::kRootFontRelative,
            LeftExp("width", LtCmp(RemValue(10.0))).GetUnitFlags());
  // 10dvh < width
  EXPECT_EQ(MediaQueryExpValue::UnitFlags::kDynamicViewport,
            LeftExp("width", LtCmp(DvhValue(10.0))).GetUnitFlags());
  // 10svh < width
  EXPECT_EQ(MediaQueryExpValue::UnitFlags::kStaticViewport,
            LeftExp("width", LtCmp(SvhValue(10.0))).GetUnitFlags());
  // 10lvh < width
  EXPECT_EQ(MediaQueryExpValue::UnitFlags::kStaticViewport,
            LeftExp("width", LtCmp(LvhValue(10.0))).GetUnitFlags());
  // 10vh < width
  EXPECT_EQ(MediaQueryExpValue::UnitFlags::kStaticViewport,
            LeftExp("width", LtCmp(VhValue(10.0))).GetUnitFlags());
  // 10cqh < width
  EXPECT_EQ(MediaQueryExpValue::UnitFlags::kContainer,
            LeftExp("width", LtCmp(CqhValue(10.0))).GetUnitFlags());

  // width < calc(10em + 10dvh)
  const auto* calc_value =
      DynamicTo<CSSPrimitiveValue>(css_test_helpers::ParseValue(
          *Document::CreateForTest(), "<length>", "calc(10em + 10dvh)"));
  ASSERT_TRUE(calc_value);
  EXPECT_EQ(
      static_cast<unsigned>(MediaQueryExpValue::UnitFlags::kFontRelative |
                            MediaQueryExpValue::UnitFlags::kDynamicViewport),
      RightExp("width", LtCmp(CssValue(*calc_value))).GetUnitFlags());
}

TEST(MediaQueryExpTest, UtilsNullptrHandling) {
  MediaQueryExp exp = RightExp("width", LtCmp(PxValue(10)));

  EXPECT_FALSE(MediaQueryExpNode::Nested(nullptr));
  EXPECT_FALSE(MediaQueryExpNode::Function(nullptr, "test"));
  EXPECT_FALSE(MediaQueryExpNode::Not(nullptr));
  EXPECT_FALSE(MediaQueryExpNode::And(nullptr, FeatureNode(exp)));
  EXPECT_FALSE(MediaQueryExpNode::And(FeatureNode(exp), nullptr));
  EXPECT_FALSE(MediaQueryExpNode::And(nullptr, nullptr));
  EXPECT_FALSE(MediaQueryExpNode::Or(nullptr, FeatureNode(exp)));
  EXPECT_FALSE(MediaQueryExpNode::Or(FeatureNode(exp), nullptr));
  EXPECT_FALSE(MediaQueryExpNode::Or(nullptr, nullptr));
}

}  // namespace blink
