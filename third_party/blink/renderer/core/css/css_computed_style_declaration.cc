/*
 * Copyright (C) 2004 Zack Rusin <zack@kde.org>
 * Copyright (C) 2004, 2005, 2006, 2007, 2008, 2009, 2010, 2011, 2012 Apple Inc.
 * All rights reserved.
 * Copyright (C) 2007 Alexey Proskuryakov <ap@webkit.org>
 * Copyright (C) 2007 Nicholas Shanks <webkit@nickshanks.com>
 * Copyright (C) 2011 Sencha, Inc. All rights reserved.
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

#include "third_party/blink/renderer/core/css/css_computed_style_declaration.h"

#include "base/memory/values_equivalent.h"
#include "third_party/blink/renderer/core/css/computed_style_css_value_mapping.h"
#include "third_party/blink/renderer/core/css/css_identifier_value.h"
#include "third_party/blink/renderer/core/css/css_primitive_value.h"
#include "third_party/blink/renderer/core/css/css_primitive_value_mappings.h"
#include "third_party/blink/renderer/core/css/css_property_names.h"
#include "third_party/blink/renderer/core/css/css_selector.h"
#include "third_party/blink/renderer/core/css/css_variable_data.h"
#include "third_party/blink/renderer/core/css/parser/css_parser.h"
#include "third_party/blink/renderer/core/css/parser/css_selector_parser.h"
#include "third_party/blink/renderer/core/css/properties/css_property_ref.h"
#include "third_party/blink/renderer/core/css/style_engine.h"
#include "third_party/blink/renderer/core/css/zoom_adjusted_pixel_value.h"
#include "third_party/blink/renderer/core/display_lock/display_lock_document_state.h"
#include "third_party/blink/renderer/core/dom/document.h"
#include "third_party/blink/renderer/core/dom/flat_tree_traversal.h"
#include "third_party/blink/renderer/core/dom/node_computed_style.h"
#include "third_party/blink/renderer/core/dom/pseudo_element.h"
#include "third_party/blink/renderer/core/frame/web_feature.h"
#include "third_party/blink/renderer/core/html/html_frame_owner_element.h"
#include "third_party/blink/renderer/core/layout/layout_object.h"
#include "third_party/blink/renderer/core/style/computed_style.h"
#include "third_party/blink/renderer/platform/bindings/exception_state.h"
#include "third_party/blink/renderer/platform/heap/garbage_collected.h"
#include "third_party/blink/renderer/platform/instrumentation/use_counter.h"
#include "third_party/blink/renderer/platform/runtime_enabled_features.h"
#include "third_party/blink/renderer/platform/wtf/text/string_builder.h"

namespace blink {

namespace {

CSSValueID CssIdentifierForFontSizeKeyword(int keyword_size) {
  DCHECK_NE(keyword_size, 0);
  DCHECK_LE(keyword_size, 8);
  return static_cast<CSSValueID>(static_cast<int>(CSSValueID::kXxSmall) +
                                 keyword_size - 1);
}

void LogUnimplementedPropertyID(const CSSProperty& property) {
  DEFINE_STATIC_LOCAL(HashSet<CSSPropertyID>, property_id_set, ());
  if (property.PropertyID() == CSSPropertyID::kVariable)
    return;
  if (!property_id_set.insert(property.PropertyID()).is_new_entry)
    return;

  DLOG(ERROR) << "Blink does not yet implement getComputedStyle for '"
              << property.GetPropertyName() << "'.";
}

}  // namespace

const Vector<const CSSProperty*>&
CSSComputedStyleDeclaration::ComputableProperties(
    const ExecutionContext* execution_context) {
  DEFINE_STATIC_LOCAL(Vector<const CSSProperty*>, properties, ());
  if (properties.IsEmpty()) {
    CSSProperty::FilterWebExposedCSSPropertiesIntoVector(
        execution_context, kCSSComputableProperties,
        std::size(kCSSComputableProperties), properties);
  }
  return properties;
}

CSSComputedStyleDeclaration::CSSComputedStyleDeclaration(
    Node* n,
    bool allow_visited_style,
    const String& pseudo_element_name)
    : CSSStyleDeclaration(n ? n->GetExecutionContext() : nullptr),
      node_(n),
      pseudo_element_specifier_(
          CSSSelectorParser::ParsePseudoElement(pseudo_element_name, n)),
      allow_visited_style_(allow_visited_style) {
  pseudo_argument_ =
      PseudoElementHasArguments(pseudo_element_specifier_)
          ? CSSSelectorParser::ParsePseudoElementArgument(pseudo_element_name)
          : g_null_atom;
}

CSSComputedStyleDeclaration::~CSSComputedStyleDeclaration() = default;

String CSSComputedStyleDeclaration::cssText() const {
  // CSSStyleDeclaration.cssText should return empty string for computed style.
  return String();
}

void CSSComputedStyleDeclaration::setCSSText(const ExecutionContext*,
                                             const String&,
                                             ExceptionState& exception_state) {
  exception_state.ThrowDOMException(
      DOMExceptionCode::kNoModificationAllowedError,
      "These styles are computed, and therefore read-only.");
}

const CSSValue*
CSSComputedStyleDeclaration::GetFontSizeCSSValuePreferringKeyword() const {
  if (!node_)
    return nullptr;

  node_->GetDocument().UpdateStyleAndLayout(DocumentUpdateReason::kEditing);

  const ComputedStyle* style =
      node_->EnsureComputedStyle(pseudo_element_specifier_);
  if (!style)
    return nullptr;

  if (int keyword_size = style->GetFontDescription().KeywordSize()) {
    return CSSIdentifierValue::Create(
        CssIdentifierForFontSizeKeyword(keyword_size));
  }

  return ZoomAdjustedPixelValue(style->GetFontDescription().ComputedPixelSize(),
                                *style);
}

bool CSSComputedStyleDeclaration::IsMonospaceFont() const {
  if (!node_)
    return false;

  const ComputedStyle* style =
      node_->EnsureComputedStyle(pseudo_element_specifier_);
  if (!style)
    return false;

  return style->GetFontDescription().IsMonospace();
}
const ComputedStyle* CSSComputedStyleDeclaration::ComputeComputedStyle() const {
  Node* styled_node = StyledNode();
  DCHECK(styled_node);
  const ComputedStyle* style = styled_node->EnsureComputedStyle(
      styled_node->IsPseudoElement() ? kPseudoIdNone
                                     : pseudo_element_specifier_,
      pseudo_argument_);
  if (style && style->IsEnsuredOutsideFlatTree()) {
    UseCounter::Count(node_->GetDocument(),
                      WebFeature::kGetComputedStyleOutsideFlatTree);
  }
  return style;
}

const Vector<AtomicString>* CSSComputedStyleDeclaration::GetVariableNames()
    const {
  if (auto* style = ComputeComputedStyle())
    return &style->GetVariableNames();
  return nullptr;
}

wtf_size_t CSSComputedStyleDeclaration::GetVariableNamesCount() const {
  if (auto* style = ComputeComputedStyle())
    return style->GetVariableNamesCount();
  return 0;
}

Node* CSSComputedStyleDeclaration::StyledNode() const {
  if (!node_)
    return nullptr;

  if (auto* node_element = DynamicTo<Element>(node_.Get())) {
    if (PseudoElement* element = node_element->GetNestedPseudoElement(
            pseudo_element_specifier_, pseudo_argument_)) {
      return element;
    }
  }
  return node_.Get();
}

LayoutObject* CSSComputedStyleDeclaration::StyledLayoutObject() const {
  auto* node = StyledNode();
  if (!node)
    return nullptr;

  if (pseudo_element_specifier_ != kPseudoIdNone && node == node_.Get())
    return nullptr;

  return node->GetLayoutObject();
}

const CSSValue* CSSComputedStyleDeclaration::GetPropertyCSSValue(
    CSSPropertyID property_id) const {
  if (property_id == CSSPropertyID::kVariable) {
    // TODO(https://crbug.com/980160): Disallow calling this function with
    // kVariable.
    return nullptr;
  }
  return GetPropertyCSSValue(CSSPropertyName(property_id));
}

const CSSValue* CSSComputedStyleDeclaration::GetPropertyCSSValue(
    const AtomicString& custom_property_name) const {
  return GetPropertyCSSValue(CSSPropertyName(custom_property_name));
}

HeapHashMap<AtomicString, Member<const CSSValue>>
CSSComputedStyleDeclaration::GetVariables() const {
  const ComputedStyle* style = ComputeComputedStyle();
  if (!style)
    return {};
  DCHECK(StyledNode());
  return ComputedStyleCSSValueMapping::GetVariables(
      *style, StyledNode()->GetDocument().GetPropertyRegistry());
}

void CSSComputedStyleDeclaration::UpdateStyleAndLayoutTreeIfNeeded(
    const CSSPropertyName* property_name) const {
  Node* styled_node = StyledNode();
  if (!styled_node)
    return;

  Document& document = styled_node->GetDocument();

  if (HTMLFrameOwnerElement* owner = document.LocalOwner()) {
    // We are inside an iframe. If any of our ancestor iframes needs a style
    // and/or layout update, we need to make that up-to-date to resolve viewport
    // media queries and generate boxes as we might be moving to/from
    // display:none in some element in the chain of ancestors.
    //
    // TODO(futhark@chromium.org): There is an open question what the computed
    // style should be in a display:none iframe. If the property we are querying
    // is not layout dependent, we will not update the iframe layout box here.
    bool is_for_layout_dependent_property =
        property_name && !property_name->IsCustomProperty() &&
        CSSProperty::Get(property_name->Id()).IsLayoutDependentProperty();
    if (is_for_layout_dependent_property) {
      auto& owner_doc = owner->GetDocument();
      owner_doc.GetDisplayLockDocumentState().UnlockShapingDeferredElements(
          *styled_node, property_name->Id());
      owner_doc.UpdateStyleAndLayout(DocumentUpdateReason::kJavaScript);
      // The style recalc could have caused the styled node to be discarded or
      // replaced if it was a PseudoElement so we need to update it.
      styled_node = StyledNode();
    }
  }

  document.UpdateStyleAndLayoutTreeForNode(styled_node);
}

void CSSComputedStyleDeclaration::UpdateStyleAndLayoutIfNeeded(
    const CSSProperty* property) const {
  Node* styled_node = StyledNode();
  if (!styled_node)
    return;

  bool is_for_layout_dependent_property =
      property && property->IsLayoutDependent(styled_node->GetComputedStyle(),
                                              StyledLayoutObject());

  if (is_for_layout_dependent_property) {
    auto& doc = styled_node->GetDocument();
    // EditingStyle uses this class with DisallowTransitionScope.
    if (!doc.Lifecycle().StateTransitionDisallowed()) {
      doc.GetDisplayLockDocumentState().UnlockShapingDeferredElements(
          *styled_node, property->PropertyID());
      doc.UpdateStyleAndLayoutForNode(styled_node,
                                      DocumentUpdateReason::kJavaScript);
    }
  }
}

const CSSValue* CSSComputedStyleDeclaration::GetPropertyCSSValue(
    const CSSPropertyName& property_name) const {
  Node* styled_node = StyledNode();
  if (!styled_node)
    return nullptr;

  UpdateStyleAndLayoutTreeIfNeeded(&property_name);

  CSSPropertyRef ref(property_name, styled_node->GetDocument());
  if (!ref.IsValid())
    return nullptr;
  const CSSProperty& property_class = ref.GetProperty();

  UpdateStyleAndLayoutIfNeeded(&property_class);

  const ComputedStyle* style = ComputeComputedStyle();

  if (!style)
    return nullptr;

  const CSSValue* value = property_class.CSSValueFromComputedStyle(
      *style, StyledLayoutObject(), allow_visited_style_);
  if (value)
    return value;

  LogUnimplementedPropertyID(property_class);
  return nullptr;
}

String CSSComputedStyleDeclaration::GetPropertyValue(
    CSSPropertyID property_id) const {
  const CSSValue* value = GetPropertyCSSValue(property_id);
  if (value)
    return value->CssText();
  return "";
}

unsigned CSSComputedStyleDeclaration::length() const {
  if (!node_ || !node_->InActiveDocument())
    return 0;

  wtf_size_t variable_count = 0;

  if (RuntimeEnabledFeatures::CSSEnumeratedCustomPropertiesEnabled()) {
    UpdateStyleAndLayoutTreeIfNeeded(nullptr /* property_name */);
    UpdateStyleAndLayoutIfNeeded(nullptr /* property */);
    variable_count = GetVariableNamesCount();
  }

  return ComputableProperties(GetExecutionContext()).size() + variable_count;
}

String CSSComputedStyleDeclaration::item(unsigned i) const {
  if (i >= length())
    return "";

  const auto& standard_names = ComputableProperties(GetExecutionContext());

  if (i < standard_names.size())
    return standard_names[i]->GetPropertyNameString();

  DCHECK(RuntimeEnabledFeatures::CSSEnumeratedCustomPropertiesEnabled());
  DCHECK(GetVariableNames());
  const auto& variable_names = *GetVariableNames();
  CHECK_LT(i - standard_names.size(), variable_names.size());

  return variable_names[i - standard_names.size()];
}

bool CSSComputedStyleDeclaration::CssPropertyMatches(
    CSSPropertyID property_id,
    const CSSValue& property_value) const {
  if (property_id == CSSPropertyID::kFontSize &&
      (property_value.IsPrimitiveValue() ||
       property_value.IsIdentifierValue()) &&
      node_) {
    // This is only used by editing code.
    node_->GetDocument().UpdateStyleAndLayout(DocumentUpdateReason::kEditing);
    const ComputedStyle* style =
        node_->EnsureComputedStyle(pseudo_element_specifier_);
    if (style && style->GetFontDescription().KeywordSize()) {
      CSSValueID size_value = CssIdentifierForFontSizeKeyword(
          style->GetFontDescription().KeywordSize());
      auto* identifier_value = DynamicTo<CSSIdentifierValue>(property_value);
      if (identifier_value && identifier_value->GetValueID() == size_value)
        return true;
    }
  }
  const CSSValue* value = GetPropertyCSSValue(property_id);
  return base::ValuesEquivalent(value, &property_value);
}

MutableCSSPropertyValueSet* CSSComputedStyleDeclaration::CopyProperties()
    const {
  return CopyPropertiesInSet(ComputableProperties(GetExecutionContext()));
}

MutableCSSPropertyValueSet* CSSComputedStyleDeclaration::CopyPropertiesInSet(
    const Vector<const CSSProperty*>& properties) const {
  HeapVector<CSSPropertyValue, 64> list;
  list.ReserveInitialCapacity(properties.size());
  for (unsigned i = 0; i < properties.size(); ++i) {
    CSSPropertyName name = properties[i]->GetCSSPropertyName();
    const CSSValue* value = GetPropertyCSSValue(name);
    if (value)
      list.push_back(CSSPropertyValue(name, *value, false));
  }
  return MakeGarbageCollected<MutableCSSPropertyValueSet>(list.data(),
                                                          list.size());
}

CSSRule* CSSComputedStyleDeclaration::parentRule() const {
  return nullptr;
}

String CSSComputedStyleDeclaration::getPropertyValue(
    const String& property_name) {
  CSSPropertyID property_id =
      CssPropertyID(GetExecutionContext(), property_name);
  if (!IsValidCSSPropertyID(property_id))
    return String();
  if (property_id == CSSPropertyID::kVariable) {
    const CSSValue* value = GetPropertyCSSValue(AtomicString(property_name));
    if (value)
      return value->CssText();
    return String();
  }
#if DCHECK_IS_ON
  DCHECK(CSSProperty::Get(property_id).IsEnabled());
#endif
  return GetPropertyValue(property_id);
}

String CSSComputedStyleDeclaration::getPropertyPriority(const String&) {
  // All computed styles have a priority of not "important".
  return "";
}

String CSSComputedStyleDeclaration::GetPropertyShorthand(const String&) {
  return "";
}

bool CSSComputedStyleDeclaration::IsPropertyImplicit(const String&) {
  return false;
}

void CSSComputedStyleDeclaration::setProperty(const ExecutionContext*,
                                              const String& name,
                                              const String&,
                                              const String&,
                                              ExceptionState& exception_state) {
  exception_state.ThrowDOMException(
      DOMExceptionCode::kNoModificationAllowedError,
      "These styles are computed, and therefore the '" + name +
          "' property is read-only.");
}

String CSSComputedStyleDeclaration::removeProperty(
    const String& name,
    ExceptionState& exception_state) {
  exception_state.ThrowDOMException(
      DOMExceptionCode::kNoModificationAllowedError,
      "These styles are computed, and therefore the '" + name +
          "' property is read-only.");
  return String();
}

const CSSValue* CSSComputedStyleDeclaration::GetPropertyCSSValueInternal(
    CSSPropertyID property_id) {
  return GetPropertyCSSValue(property_id);
}

const CSSValue* CSSComputedStyleDeclaration::GetPropertyCSSValueInternal(
    const AtomicString& custom_property_name) {
  DCHECK_EQ(CSSPropertyID::kVariable,
            CssPropertyID(GetExecutionContext(), custom_property_name));
  return GetPropertyCSSValue(custom_property_name);
}

String CSSComputedStyleDeclaration::GetPropertyValueInternal(
    CSSPropertyID property_id) {
  return GetPropertyValue(property_id);
}

String CSSComputedStyleDeclaration::GetPropertyValueWithHint(
    const String& property_name,
    unsigned index) {
  NOTREACHED();
  return "";
}

String CSSComputedStyleDeclaration::GetPropertyPriorityWithHint(
    const String& property_name,
    unsigned index) {
  NOTREACHED();
  return "";
}

void CSSComputedStyleDeclaration::SetPropertyInternal(
    CSSPropertyID id,
    const String&,
    const String&,
    bool,
    SecureContextMode,
    ExceptionState& exception_state) {
  exception_state.ThrowDOMException(
      DOMExceptionCode::kNoModificationAllowedError,
      "These styles are computed, and therefore the '" +
          CSSUnresolvedProperty::Get(id).GetPropertyNameString() +
          "' property is read-only.");
}

void CSSComputedStyleDeclaration::Trace(Visitor* visitor) const {
  visitor->Trace(node_);
  CSSStyleDeclaration::Trace(visitor);
}

}  // namespace blink
