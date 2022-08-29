/*
 * Copyright (C) 2004 Zack Rusin <zack@kde.org>
 * Copyright (C) 2004, 2005, 2006, 2008, 2012 Apple Inc. All rights reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301  USA
 */

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_CSS_CSS_COMPUTED_STYLE_DECLARATION_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_CSS_CSS_COMPUTED_STYLE_DECLARATION_H_

#include "base/memory/scoped_refptr.h"
#include "third_party/blink/renderer/core/core_export.h"
#include "third_party/blink/renderer/core/css/css_style_declaration.h"
#include "third_party/blink/renderer/core/css/properties/css_property.h"
#include "third_party/blink/renderer/core/style/computed_style_constants.h"
#include "third_party/blink/renderer/platform/heap/collection_support/heap_hash_map.h"
#include "third_party/blink/renderer/platform/wtf/text/atomic_string.h"
#include "third_party/blink/renderer/platform/wtf/text/atomic_string_hash.h"
#include "third_party/blink/renderer/platform/wtf/text/wtf_string.h"

namespace blink {

class ExceptionState;
class ExecutionContext;
class LayoutObject;
class MutableCSSPropertyValueSet;
class Node;
class ComputedStyle;

class CORE_EXPORT CSSComputedStyleDeclaration final
    : public CSSStyleDeclaration {
 public:
  static const Vector<const CSSProperty*>& ComputableProperties(
      const ExecutionContext*);

  CSSComputedStyleDeclaration(Node*,
                              bool allow_visited_style = false,
                              const String& = String());
  ~CSSComputedStyleDeclaration() override;

  String GetPropertyValue(CSSPropertyID) const;
  bool GetPropertyPriority(CSSPropertyID) const;

  MutableCSSPropertyValueSet* CopyProperties() const;

  const CSSValue* GetPropertyCSSValue(CSSPropertyID) const;
  const CSSValue* GetPropertyCSSValue(
      const AtomicString& custom_property_name) const;
  const CSSValue* GetPropertyCSSValue(const CSSPropertyName&) const;
  HeapHashMap<AtomicString, Member<const CSSValue>> GetVariables() const;

  const CSSValue* GetFontSizeCSSValuePreferringKeyword() const;
  bool IsMonospaceFont() const;

  MutableCSSPropertyValueSet* CopyPropertiesInSet(
      const Vector<const CSSProperty*>&) const;

  // CSSOM functions.
  unsigned length() const override;
  String item(unsigned index) const override;

  void Trace(Visitor*) const override;

 private:
  // The styled node is either the node passed into getComputedStyle, or the
  // PseudoElement for :before and :after if they exist.
  // FIXME: This should be styledElement since in JS getComputedStyle only works
  // on Elements, but right now editing creates these for text nodes. We should
  // fix that.
  Node* StyledNode() const;

  // The styled layout object is the layout object corresponding to the node
  // being queried, if any.
  LayoutObject* StyledLayoutObject() const;

  // If we are updating the style/layout-tree/layout with the intent to
  // retrieve the computed value of a property, the appropriate
  // property name/instance must be provided.
  void UpdateStyleAndLayoutTreeIfNeeded(const CSSPropertyName*) const;
  void UpdateStyleAndLayoutIfNeeded(const CSSProperty*) const;

  // CSSOM functions.
  CSSRule* parentRule() const override;
  const ComputedStyle* ComputeComputedStyle() const;
  const Vector<AtomicString>* GetVariableNames() const;
  wtf_size_t GetVariableNamesCount() const;
  String getPropertyValue(const String& property_name) override;
  String getPropertyPriority(const String& property_name) override;
  String GetPropertyShorthand(const String& property_name) override;
  bool IsPropertyImplicit(const String& property_name) override;
  void setProperty(const ExecutionContext*,
                   const String& property_name,
                   const String& value,
                   const String& priority,
                   ExceptionState&) override;
  String removeProperty(const String& property_name, ExceptionState&) override;
  String CssFloat() const;
  void SetCSSFloat(const String&, ExceptionState&);
  String cssText() const override;
  void setCSSText(const ExecutionContext*,
                  const String&,
                  ExceptionState&) override;
  const CSSValue* GetPropertyCSSValueInternal(CSSPropertyID) override;
  const CSSValue* GetPropertyCSSValueInternal(
      const AtomicString& custom_property_name) override;
  String GetPropertyValueInternal(CSSPropertyID) override;
  String GetPropertyValueWithHint(const String& property_name,
                                  unsigned index) override;
  String GetPropertyPriorityWithHint(const String& property_name,
                                     unsigned index) override;
  void SetPropertyInternal(CSSPropertyID,
                           const String& custom_property_name,
                           const String& value,
                           bool important,
                           SecureContextMode,
                           ExceptionState&) override;

  bool CssPropertyMatches(CSSPropertyID, const CSSValue&) const override;

  Member<Node> node_;
  PseudoId pseudo_element_specifier_;
  AtomicString pseudo_argument_;
  bool allow_visited_style_;
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_CORE_CSS_CSS_COMPUTED_STYLE_DECLARATION_H_
