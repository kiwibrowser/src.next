// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_CSS_CSS_PROPERTY_RULE_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_CSS_CSS_PROPERTY_RULE_H_

#include "third_party/blink/renderer/core/css/css_rule.h"
#include "third_party/blink/renderer/platform/heap/garbage_collected.h"
#include "third_party/blink/renderer/platform/heap/member.h"
#include "third_party/blink/renderer/platform/wtf/casting.h"

namespace blink {

class StyleRuleProperty;

class CSSPropertyRule final : public CSSRule {
  DEFINE_WRAPPERTYPEINFO();

 public:
  CSSPropertyRule(StyleRuleProperty*, CSSStyleSheet*);
  ~CSSPropertyRule() override;

  String cssText() const override;
  void Reattach(StyleRuleBase*) override;

  String name() const;
  String syntax() const;
  bool inherits() const;
  String initialValue() const;

  void Trace(Visitor*) const override;

 private:
  CSSRule::Type GetType() const override { return kPropertyRule; }

  Member<StyleRuleProperty> property_rule_;
};

template <>
struct DowncastTraits<CSSPropertyRule> {
  static bool AllowFrom(const CSSRule& rule) {
    return rule.GetType() == CSSRule::kPropertyRule;
  }
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_CORE_CSS_CSS_PROPERTY_RULE_H_
