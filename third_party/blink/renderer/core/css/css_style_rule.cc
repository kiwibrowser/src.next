/*
 * (C) 1999-2003 Lars Knoll (knoll@kde.org)
 * (C) 2002-2003 Dirk Mueller (mueller@kde.org)
 * Copyright (C) 2002, 2005, 2006, 2008, 2012 Apple Inc. All rights reserved.
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

#include "third_party/blink/renderer/core/css/css_style_rule.h"

#include "third_party/blink/renderer/core/css/css_property_value_set.h"
#include "third_party/blink/renderer/core/css/css_selector.h"
#include "third_party/blink/renderer/core/css/css_style_sheet.h"
#include "third_party/blink/renderer/core/css/cssom/declared_style_property_map.h"
#include "third_party/blink/renderer/core/css/parser/css_parser.h"
#include "third_party/blink/renderer/core/css/parser/css_selector_parser.h"
#include "third_party/blink/renderer/core/css/style_rule.h"
#include "third_party/blink/renderer/core/css/style_rule_css_style_declaration.h"
#include "third_party/blink/renderer/core/css/style_sheet_contents.h"
#include "third_party/blink/renderer/core/execution_context/execution_context.h"
#include "third_party/blink/renderer/platform/heap/garbage_collected.h"
#include "third_party/blink/renderer/platform/wtf/text/string_builder.h"

namespace blink {

using SelectorTextCache = HeapHashMap<WeakMember<const CSSStyleRule>, String>;

static SelectorTextCache& GetSelectorTextCache() {
  DEFINE_STATIC_LOCAL(Persistent<SelectorTextCache>, cache,
                      (MakeGarbageCollected<SelectorTextCache>()));
  return *cache;
}

CSSStyleRule::CSSStyleRule(StyleRule* style_rule,
                           CSSStyleSheet* parent,
                           wtf_size_t position_hint)
    : CSSRule(parent),
      style_rule_(style_rule),
      style_map_(MakeGarbageCollected<DeclaredStylePropertyMap>(this)),
      position_hint_(position_hint) {}

CSSStyleRule::~CSSStyleRule() = default;

CSSStyleDeclaration* CSSStyleRule::style() const {
  if (!properties_cssom_wrapper_) {
    properties_cssom_wrapper_ =
        MakeGarbageCollected<StyleRuleCSSStyleDeclaration>(
            style_rule_->MutableProperties(), const_cast<CSSStyleRule*>(this));
  }
  return properties_cssom_wrapper_.Get();
}

String CSSStyleRule::selectorText() const {
  if (HasCachedSelectorText()) {
    DCHECK(GetSelectorTextCache().Contains(this));
    return GetSelectorTextCache().at(this);
  }

  DCHECK(!GetSelectorTextCache().Contains(this));
  String text = style_rule_->SelectorsText();
  GetSelectorTextCache().Set(this, text);
  SetHasCachedSelectorText(true);
  return text;
}

void CSSStyleRule::setSelectorText(const ExecutionContext* execution_context,
                                   const String& selector_text) {
  CSSStyleSheet::RuleMutationScope mutation_scope(this);

  const auto* context = MakeGarbageCollected<CSSParserContext>(
      ParserContext(execution_context->GetSecureContextMode()));
  StyleSheetContents* parent_contents =
      parentStyleSheet() ? parentStyleSheet()->Contents() : nullptr;
  CSSSelectorVector selector_vector =
      CSSParser::ParseSelector(context, parent_contents, selector_text);
  if (selector_vector.IsEmpty())
    return;

  Member<StyleRule> new_style_rule =
      StyleRule::Create(selector_vector, std::move(*style_rule_));
  if (parent_contents) {
    position_hint_ = parent_contents->ReplaceRuleIfExists(
        style_rule_, new_style_rule, position_hint_);
  }
  style_rule_ = new_style_rule;

  if (HasCachedSelectorText()) {
    GetSelectorTextCache().erase(this);
    SetHasCachedSelectorText(false);
  }
}

String CSSStyleRule::cssText() const {
  StringBuilder result;
  result.Append(selectorText());
  result.Append(" { ");
  String decls = style_rule_->Properties().AsText();
  result.Append(decls);
  if (!decls.IsEmpty())
    result.Append(' ');
  result.Append('}');
  return result.ReleaseString();
}

void CSSStyleRule::Reattach(StyleRuleBase* rule) {
  DCHECK(rule);
  style_rule_ = To<StyleRule>(rule);
  if (properties_cssom_wrapper_)
    properties_cssom_wrapper_->Reattach(style_rule_->MutableProperties());
}

void CSSStyleRule::Trace(Visitor* visitor) const {
  visitor->Trace(style_rule_);
  visitor->Trace(properties_cssom_wrapper_);
  visitor->Trace(style_map_);
  CSSRule::Trace(visitor);
}

}  // namespace blink
