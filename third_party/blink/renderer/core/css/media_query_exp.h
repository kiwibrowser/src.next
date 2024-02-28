/*
 * CSS Media Query
 *
 * Copyright (C) 2006 Kimmo Kinnunen <kimmo.t.kinnunen@nokia.com>.
 * Copyright (C) 2010 Nokia Corporation and/or its subsidiary(-ies).
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE COMPUTER, INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_CSS_MEDIA_QUERY_EXP_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_CSS_MEDIA_QUERY_EXP_H_

#include "third_party/abseil-cpp/absl/types/optional.h"
#include "third_party/blink/renderer/core/core_export.h"
#include "third_party/blink/renderer/core/css/css_primitive_value.h"
#include "third_party/blink/renderer/core/css/css_value.h"
#include "third_party/blink/renderer/core/css/media_feature_names.h"
#include "third_party/blink/renderer/core/css_value_keywords.h"
#include "third_party/blink/renderer/core/layout/geometry/axis.h"
#include "third_party/blink/renderer/platform/wtf/allocator/allocator.h"
#include "third_party/blink/renderer/platform/wtf/casting.h"

namespace blink {

class CSSParserContext;
class CSSParserTokenRange;
class CSSParserTokenOffsets;

class CORE_EXPORT MediaQueryExpValue {
  DISALLOW_NEW();

 public:
  // Type::kInvalid
  MediaQueryExpValue() = default;

  explicit MediaQueryExpValue(CSSValueID id) : type_(Type::kId), id_(id) {}
  MediaQueryExpValue(double value, CSSPrimitiveValue::UnitType unit)
      : type_(Type::kNumeric), numeric_({value, unit}) {}
  MediaQueryExpValue(double numerator, double denominator)
      : type_(Type::kRatio), ratio_({numerator, denominator}) {}
  explicit MediaQueryExpValue(const CSSValue& value)
      : type_(Type::kCSSValue), css_value_(&value) {}
  void Trace(Visitor* visitor) const { visitor->Trace(css_value_); }

  bool IsValid() const { return type_ != Type::kInvalid; }
  bool IsId() const { return type_ == Type::kId; }
  bool IsNumeric() const { return type_ == Type::kNumeric; }
  bool IsRatio() const { return type_ == Type::kRatio; }
  bool IsCSSValue() const { return type_ == Type::kCSSValue; }
  bool IsResolution() const;

  CSSValueID Id() const {
    DCHECK(IsId());
    return id_;
  }

  double Value() const;

  CSSPrimitiveValue::UnitType Unit() const;

  double Numerator() const {
    DCHECK(IsRatio());
    return ratio_.numerator;
  }

  double Denominator() const {
    DCHECK(IsRatio());
    return ratio_.denominator;
  }

  const CSSValue& GetCSSValue() const {
    DCHECK(IsCSSValue());
    DCHECK(css_value_);
    return *css_value_;
  }

  enum UnitFlags {
    kNone = 0,
    kFontRelative = 1 << 0,
    kRootFontRelative = 1 << 1,
    kDynamicViewport = 1 << 2,
    kStaticViewport = 1 << 3,
    kContainer = 1 << 4,
  };

  static const int kUnitFlagsBits = 5;

  unsigned GetUnitFlags() const;

  String CssText() const;
  bool operator==(const MediaQueryExpValue& other) const {
    if (type_ != other.type_) {
      return false;
    }
    switch (type_) {
      case Type::kInvalid:
        return true;
      case Type::kId:
        return id_ == other.id_;
      case Type::kNumeric:
        return (numeric_.value == other.numeric_.value) &&
               (numeric_.unit == other.numeric_.unit);
      case Type::kRatio:
        return (ratio_.numerator == other.ratio_.numerator) &&
               (ratio_.denominator == other.ratio_.denominator);
      case Type::kCSSValue:
        return base::ValuesEquivalent(css_value_, other.css_value_);
    }
  }
  bool operator!=(const MediaQueryExpValue& other) const {
    return !(*this == other);
  }

  // Consume a MediaQueryExpValue for the provided feature, which must already
  // be lower-cased.
  //
  // absl::nullopt is returned on errors.
  static absl::optional<MediaQueryExpValue> Consume(
      const String& lower_media_feature,
      CSSParserTokenRange&,
      const CSSParserTokenOffsets&,
      const CSSParserContext&);

 private:
  enum class Type { kInvalid, kId, kNumeric, kRatio, kCSSValue };

  Type type_ = Type::kInvalid;

  // Used when the value can't be represented by the union below (e.g. math
  // functions). Also used for style features in style container queries.
  Member<const CSSValue> css_value_;

  union {
    CSSValueID id_;
    struct {
      double value;
      CSSPrimitiveValue::UnitType unit;
    } numeric_;
    struct {
      double numerator;
      double denominator;
    } ratio_;
  };
};

// https://drafts.csswg.org/mediaqueries-4/#mq-syntax
enum class MediaQueryOperator {
  // Used for <mf-plain>, <mf-boolean>
  kNone,

  // Used for <mf-range>
  kEq,
  kLt,
  kLe,
  kGt,
  kGe,
};

// This represents the following part of a <media-feature> (example):
//
//  (width >= 10px)
//         ^^^^^^^
//
struct CORE_EXPORT MediaQueryExpComparison {
  DISALLOW_NEW();
  MediaQueryExpComparison() = default;
  explicit MediaQueryExpComparison(const MediaQueryExpValue& value)
      : value(value) {}
  MediaQueryExpComparison(const MediaQueryExpValue& value,
                          MediaQueryOperator op)
      : value(value), op(op) {}
  void Trace(Visitor* visitor) const { visitor->Trace(value); }

  bool operator==(const MediaQueryExpComparison& o) const {
    return value == o.value && op == o.op;
  }
  bool operator!=(const MediaQueryExpComparison& o) const {
    return !(*this == o);
  }

  bool IsValid() const { return value.IsValid(); }

  MediaQueryExpValue value;
  MediaQueryOperator op = MediaQueryOperator::kNone;
};

// There exists three types of <media-feature>s.
//
//  1) Boolean features, which is just the feature name, e.g. (color)
//  2) Plain features, which can appear in two different forms:
//       - Feature with specific value, e.g. (width: 100px)
//       - Feature with min/max prefix, e.g. (min-width: 100px)
//  3) Range features, which can appear in three different forms:
//       - Feature compared with value, e.g. (width >= 100px)
//       - Feature compared with value (reversed), e.g. (100px <= width)
//       - Feature within a certain range, e.g. (100px < width < 200px)
//
// In the first case, both |left| and |right| values are not set.
// In the second case, only |right| is set.
// In the third case, either |left| is set, |right| is set, or both, depending
// on the form.
//
// https://drafts.csswg.org/mediaqueries-4/#typedef-media-feature
struct CORE_EXPORT MediaQueryExpBounds {
  DISALLOW_NEW();
  MediaQueryExpBounds() = default;
  explicit MediaQueryExpBounds(const MediaQueryExpComparison& right)
      : right(right) {}
  MediaQueryExpBounds(const MediaQueryExpComparison& left,
                      const MediaQueryExpComparison& right)
      : left(left), right(right) {}
  void Trace(Visitor* visitor) const {
    visitor->Trace(left);
    visitor->Trace(right);
  }

  bool IsRange() const {
    return left.op != MediaQueryOperator::kNone ||
           right.op != MediaQueryOperator::kNone;
  }

  bool operator==(const MediaQueryExpBounds& o) const {
    return left == o.left && right == o.right;
  }
  bool operator!=(const MediaQueryExpBounds& o) const { return !(*this == o); }

  MediaQueryExpComparison left;
  MediaQueryExpComparison right;
};

class CORE_EXPORT MediaQueryExp {
  DISALLOW_NEW();

 public:
  // Returns an invalid MediaQueryExp if the arguments are invalid.
  static MediaQueryExp Create(const String& media_feature,
                              CSSParserTokenRange&,
                              const CSSParserTokenOffsets&,
                              const CSSParserContext&);
  static MediaQueryExp Create(const String& media_feature,
                              const MediaQueryExpBounds&);
  static MediaQueryExp Invalid() {
    return MediaQueryExp(String(), MediaQueryExpValue());
  }

  MediaQueryExp(const MediaQueryExp& other);
  ~MediaQueryExp();
  void Trace(Visitor*) const;

  const String& MediaFeature() const { return media_feature_; }

  const MediaQueryExpBounds& Bounds() const { return bounds_; }

  bool IsValid() const { return !media_feature_.IsNull(); }

  bool operator==(const MediaQueryExp& other) const;
  bool operator!=(const MediaQueryExp& other) const {
    return !(*this == other);
  }

  bool IsViewportDependent() const;

  bool IsDeviceDependent() const;

  bool IsWidthDependent() const;
  bool IsHeightDependent() const;
  bool IsInlineSizeDependent() const;
  bool IsBlockSizeDependent() const;

  String Serialize() const;

  // Return the union of GetUnitFlags() from the expr values.
  unsigned GetUnitFlags() const;

 private:
  MediaQueryExp(const String&, const MediaQueryExpValue&);
  MediaQueryExp(const String&, const MediaQueryExpBounds&);

  String media_feature_;
  MediaQueryExpBounds bounds_;
};

// MediaQueryExpNode representing a tree of MediaQueryExp objects capable of
// nested/compound expressions.
class CORE_EXPORT MediaQueryExpNode
    : public GarbageCollected<MediaQueryExpNode> {
 public:
  virtual ~MediaQueryExpNode() = default;
  virtual void Trace(Visitor*) const {}

  enum class Type { kFeature, kNested, kFunction, kNot, kAnd, kOr, kUnknown };

  enum FeatureFlag {
    kFeatureUnknown = 1 << 1,
    kFeatureWidth = 1 << 2,
    kFeatureHeight = 1 << 3,
    kFeatureInlineSize = 1 << 4,
    kFeatureBlockSize = 1 << 5,
    kFeatureStyle = 1 << 6,
    kFeatureSticky = 1 << 7,
    kFeatureSnap = 1 << 8,
  };

  using FeatureFlags = unsigned;

  String Serialize() const;

  bool HasUnknown() const { return CollectFeatureFlags() & kFeatureUnknown; }

  virtual Type GetType() const = 0;
  virtual void SerializeTo(StringBuilder&) const = 0;
  virtual void CollectExpressions(HeapVector<MediaQueryExp>&) const = 0;
  virtual FeatureFlags CollectFeatureFlags() const = 0;

  // These helper functions return nullptr if any argument is nullptr.
  static const MediaQueryExpNode* Not(const MediaQueryExpNode*);
  static const MediaQueryExpNode* Nested(const MediaQueryExpNode*);
  static const MediaQueryExpNode* Function(const MediaQueryExpNode*,
                                           const AtomicString& name);
  static const MediaQueryExpNode* And(const MediaQueryExpNode*,
                                      const MediaQueryExpNode*);
  static const MediaQueryExpNode* Or(const MediaQueryExpNode*,
                                     const MediaQueryExpNode*);
};

class CORE_EXPORT MediaQueryFeatureExpNode : public MediaQueryExpNode {
 public:
  explicit MediaQueryFeatureExpNode(const MediaQueryExp& exp) : exp_(exp) {}
  void Trace(Visitor*) const override;

  const String& Name() const { return exp_.MediaFeature(); }
  const MediaQueryExpBounds& Bounds() const { return exp_.Bounds(); }

  unsigned GetUnitFlags() const;
  bool IsViewportDependent() const;
  bool IsDeviceDependent() const;
  bool IsWidthDependent() const;
  bool IsHeightDependent() const;
  bool IsInlineSizeDependent() const;
  bool IsBlockSizeDependent() const;

  Type GetType() const override { return Type::kFeature; }
  void SerializeTo(StringBuilder&) const override;
  void CollectExpressions(HeapVector<MediaQueryExp>&) const override;
  FeatureFlags CollectFeatureFlags() const override;

 private:
  MediaQueryExp exp_;
};

class CORE_EXPORT MediaQueryUnaryExpNode : public MediaQueryExpNode {
 public:
  explicit MediaQueryUnaryExpNode(const MediaQueryExpNode* operand)
      : operand_(operand) {
    DCHECK(operand_);
  }
  void Trace(Visitor*) const override;

  void CollectExpressions(HeapVector<MediaQueryExp>&) const override;
  FeatureFlags CollectFeatureFlags() const override;
  const MediaQueryExpNode& Operand() const { return *operand_; }

 private:
  Member<const MediaQueryExpNode> operand_;
};

class CORE_EXPORT MediaQueryNestedExpNode : public MediaQueryUnaryExpNode {
 public:
  explicit MediaQueryNestedExpNode(const MediaQueryExpNode* operand)
      : MediaQueryUnaryExpNode(operand) {}

  Type GetType() const override { return Type::kNested; }
  void SerializeTo(StringBuilder&) const override;
};

class CORE_EXPORT MediaQueryFunctionExpNode : public MediaQueryUnaryExpNode {
 public:
  explicit MediaQueryFunctionExpNode(const MediaQueryExpNode* operand,
                                     const AtomicString& name)
      : MediaQueryUnaryExpNode(operand), name_(name) {}

  Type GetType() const override { return Type::kFunction; }
  void SerializeTo(StringBuilder&) const override;
  FeatureFlags CollectFeatureFlags() const override;

 private:
  AtomicString name_;
};

class CORE_EXPORT MediaQueryNotExpNode : public MediaQueryUnaryExpNode {
 public:
  explicit MediaQueryNotExpNode(const MediaQueryExpNode* operand)
      : MediaQueryUnaryExpNode(operand) {}

  Type GetType() const override { return Type::kNot; }
  void SerializeTo(StringBuilder&) const override;
};

class CORE_EXPORT MediaQueryCompoundExpNode : public MediaQueryExpNode {
 public:
  MediaQueryCompoundExpNode(const MediaQueryExpNode* left,
                            const MediaQueryExpNode* right)
      : left_(left), right_(right) {
    DCHECK(left_);
    DCHECK(right_);
  }
  void Trace(Visitor*) const override;

  void CollectExpressions(HeapVector<MediaQueryExp>&) const override;
  FeatureFlags CollectFeatureFlags() const override;
  const MediaQueryExpNode& Left() const { return *left_; }
  const MediaQueryExpNode& Right() const { return *right_; }

 private:
  Member<const MediaQueryExpNode> left_;
  Member<const MediaQueryExpNode> right_;
};

class CORE_EXPORT MediaQueryAndExpNode : public MediaQueryCompoundExpNode {
 public:
  MediaQueryAndExpNode(const MediaQueryExpNode* left,
                       const MediaQueryExpNode* right)
      : MediaQueryCompoundExpNode(left, right) {}

  Type GetType() const override { return Type::kAnd; }
  void SerializeTo(StringBuilder&) const override;
};

class CORE_EXPORT MediaQueryOrExpNode : public MediaQueryCompoundExpNode {
 public:
  MediaQueryOrExpNode(const MediaQueryExpNode* left,
                      const MediaQueryExpNode* right)
      : MediaQueryCompoundExpNode(left, right) {}

  Type GetType() const override { return Type::kOr; }
  void SerializeTo(StringBuilder&) const override;
};

class CORE_EXPORT MediaQueryUnknownExpNode : public MediaQueryExpNode {
 public:
  explicit MediaQueryUnknownExpNode(String string) : string_(string) {}

  Type GetType() const override { return Type::kUnknown; }
  void SerializeTo(StringBuilder&) const override;
  void CollectExpressions(HeapVector<MediaQueryExp>&) const override;
  FeatureFlags CollectFeatureFlags() const override;

 private:
  String string_;
};

template <>
struct DowncastTraits<MediaQueryFeatureExpNode> {
  static bool AllowFrom(const MediaQueryExpNode& node) {
    return node.GetType() == MediaQueryExpNode::Type::kFeature;
  }
};

template <>
struct DowncastTraits<MediaQueryNestedExpNode> {
  static bool AllowFrom(const MediaQueryExpNode& node) {
    return node.GetType() == MediaQueryExpNode::Type::kNested;
  }
};

template <>
struct DowncastTraits<MediaQueryFunctionExpNode> {
  static bool AllowFrom(const MediaQueryExpNode& node) {
    return node.GetType() == MediaQueryExpNode::Type::kFunction;
  }
};

template <>
struct DowncastTraits<MediaQueryNotExpNode> {
  static bool AllowFrom(const MediaQueryExpNode& node) {
    return node.GetType() == MediaQueryExpNode::Type::kNot;
  }
};

template <>
struct DowncastTraits<MediaQueryAndExpNode> {
  static bool AllowFrom(const MediaQueryExpNode& node) {
    return node.GetType() == MediaQueryExpNode::Type::kAnd;
  }
};

template <>
struct DowncastTraits<MediaQueryOrExpNode> {
  static bool AllowFrom(const MediaQueryExpNode& node) {
    return node.GetType() == MediaQueryExpNode::Type::kOr;
  }
};

template <>
struct DowncastTraits<MediaQueryUnknownExpNode> {
  static bool AllowFrom(const MediaQueryExpNode& node) {
    return node.GetType() == MediaQueryExpNode::Type::kUnknown;
  }
};

}  // namespace blink

WTF_ALLOW_MOVE_AND_INIT_WITH_MEM_FUNCTIONS(blink::MediaQueryExpValue)
WTF_ALLOW_MOVE_AND_INIT_WITH_MEM_FUNCTIONS(blink::MediaQueryExpComparison)
WTF_ALLOW_MOVE_AND_INIT_WITH_MEM_FUNCTIONS(blink::MediaQueryExpBounds)
WTF_ALLOW_MOVE_AND_INIT_WITH_MEM_FUNCTIONS(blink::MediaQueryExp)

#endif  // THIRD_PARTY_BLINK_RENDERER_CORE_CSS_MEDIA_QUERY_EXP_H_
