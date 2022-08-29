// Copyright 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_CSS_CSS_SCOPE_RULE_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_CSS_CSS_SCOPE_RULE_H_

#include "third_party/blink/renderer/core/core_export.h"
#include "third_party/blink/renderer/core/css/css_grouping_rule.h"
#include "third_party/blink/renderer/core/execution_context/execution_context.h"
#include "third_party/blink/renderer/platform/wtf/casting.h"

namespace blink {

class StyleRuleScope;

class CORE_EXPORT CSSScopeRule final : public CSSGroupingRule {
  DEFINE_WRAPPERTYPEINFO();

 public:
  CSSScopeRule(StyleRuleScope*, CSSStyleSheet*);
  ~CSSScopeRule() override;

  String PreludeText() const;
  String cssText() const override;

  void SetPreludeText(const ExecutionContext*, String);

 private:
  CSSRule::Type GetType() const override { return kScopeRule; }
};

template <>
struct DowncastTraits<CSSScopeRule> {
  static bool AllowFrom(const CSSRule& rule) {
    return rule.GetType() == CSSRule::kScopeRule;
  }
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_CORE_CSS_CSS_SCOPE_RULE_H_
