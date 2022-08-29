// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_CSS_RESOLVER_CASCADE_EXPANSION_INL_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_CSS_RESOLVER_CASCADE_EXPANSION_INL_H_

// Implementation of cascade_expansion.h.

#include "third_party/blink/renderer/core/css/resolver/cascade_expansion.h"

#include "third_party/blink/renderer/core/css/properties/css_property.h"
#include "third_party/blink/renderer/core/css/resolver/match_result.h"

namespace blink {

template <class Callback>
void ExpandCascade(const MatchedProperties& matched_properties,
                   const Document& document,
                   wtf_size_t matched_properties_index,
                   Callback&& callback) {
  CascadeFilter filter = CreateExpansionFilter(matched_properties);

  // We can't handle a MatchResult with more than 0xFFFF MatchedProperties,
  // or a MatchedProperties object with more than 0xFFFF declarations. If this
  // happens, we skip right to the end, and emit nothing.
  wtf_size_t size = matched_properties.properties->PropertyCount();
  if (size > kMaxDeclarationIndex + 1 ||
      matched_properties_index > kMaxMatchedPropertiesIndex) {
    return;
  }

  const bool expand_visited = !filter.Rejects(CSSProperty::kVisited, true);

  for (wtf_size_t property_idx = 0; property_idx < size; ++property_idx) {
    auto reference = matched_properties.properties->PropertyAt(property_idx);
    const auto& metadata = reference.PropertyMetadata();
    CSSPropertyID id = metadata.PropertyID();
    CascadePriority priority(
        matched_properties.types_.origin, metadata.important_,
        matched_properties.types_.tree_order,
        matched_properties.types_.is_inline_style,
        matched_properties.types_.layer_order,
        EncodeMatchResultPosition(matched_properties_index, property_idx));

    if (id == CSSPropertyID::kVariable) {
      CustomProperty custom(reference.Name().ToAtomicString(), document);
      if (!filter.Rejects(custom)) {
        callback(priority, custom,
                 CSSPropertyName(reference.Name().ToAtomicString()),
                 reference.Value(), matched_properties.types_.tree_order);
      }
      // Custom properties never have visited counterparts,
      // so no need to check for expand_visited here.
    } else if (id == CSSPropertyID::kAll) {
      for (int i = kIntFirstCSSProperty; i <= kIntLastCSSProperty; ++i) {
        CSSPropertyID expanded_id = ConvertToCSSPropertyID(i);
        if (!IsInAllExpansion(expanded_id)) {
          continue;
        }
        const CSSProperty& property = CSSProperty::Get(expanded_id);
        if (!filter.Rejects(property)) {
          callback(priority, property, CSSPropertyName(expanded_id),
                   reference.Value(), matched_properties.types_.tree_order);
        }
      }
    } else {
      const CSSProperty& property = CSSProperty::Get(id);
      if (!filter.Rejects(property)) {
        callback(priority, property, CSSPropertyName(id), reference.Value(),
                 matched_properties.types_.tree_order);
      }
      if (expand_visited) {
        const CSSProperty* visited_property = property.GetVisitedProperty();
        if (visited_property && !filter.Rejects(*visited_property)) {
          callback(priority, *visited_property,
                   visited_property->GetCSSPropertyName(), reference.Value(),
                   matched_properties.types_.tree_order);
        }
      }
    }
  }
}

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_CORE_CSS_RESOLVER_CASCADE_EXPANSION_INL_H_
