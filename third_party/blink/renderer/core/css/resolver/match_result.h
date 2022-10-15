/*
 * Copyright (C) 1999 Lars Knoll (knoll@kde.org)
 * Copyright (C) 2003, 2004, 2005, 2006, 2007, 2008, 2009, 2010, 2011 Apple Inc.
 * All rights reserved.
 * Copyright (C) 2013 Google Inc. All rights reserved.
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
 *
 */

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_CSS_RESOLVER_MATCH_RESULT_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_CSS_RESOLVER_MATCH_RESULT_H_

#include "base/memory/scoped_refptr.h"
#include "third_party/blink/renderer/core/core_export.h"
#include "third_party/blink/renderer/core/css/css_selector.h"
#include "third_party/blink/renderer/core/css/resolver/cascade_origin.h"
#include "third_party/blink/renderer/core/css/rule_set.h"
#include "third_party/blink/renderer/core/dom/tree_scope.h"
#include "third_party/blink/renderer/platform/heap/collection_support/heap_vector.h"
#include "third_party/blink/renderer/platform/heap/garbage_collected.h"
#include "third_party/blink/renderer/platform/wtf/vector.h"

namespace blink {

class CSSPropertyValueSet;

struct CORE_EXPORT MatchedProperties {
  DISALLOW_NEW();

 public:
  MatchedProperties();

  void Trace(Visitor*) const;

  Member<CSSPropertyValueSet> properties;

  struct Data {
    unsigned link_match_type : 2;
    unsigned valid_property_filter : 3;
    CascadeOrigin origin;
    // This is approximately equivalent to the 'shadow-including tree order'.
    // It can be used to evaluate the 'Shadow Tree' criteria. Note that the
    // number stored here is 'local' to each origin (user, author), and is
    // not used at all for the UA origin. Hence, it is not possible to compare
    // tree_orders from two different origins.
    //
    // https://drafts.csswg.org/css-scoping/#shadow-cascading
    uint16_t tree_order;
    // https://drafts.csswg.org/css-cascade-5/#layer-ordering
    uint16_t layer_order;
    bool is_inline_style;
  };
  Data types_;
};

}  // namespace blink

WTF_ALLOW_MOVE_AND_INIT_WITH_MEM_FUNCTIONS(blink::MatchedProperties)

namespace blink {

using MatchedPropertiesVector = HeapVector<MatchedProperties, 64>;

class AddMatchedPropertiesOptions {
  STACK_ALLOCATED();

 public:
  class Builder;

  unsigned GetLinkMatchType() const { return link_match_type_; }
  ValidPropertyFilter GetValidPropertyFilter() const {
    return valid_property_filter_;
  }
  unsigned GetLayerOrder() const { return layer_order_; }
  bool IsInlineStyle() const { return is_inline_style_; }

 private:
  unsigned link_match_type_ = CSSSelector::kMatchAll;
  ValidPropertyFilter valid_property_filter_ = ValidPropertyFilter::kNoFilter;
  unsigned layer_order_ = 0;
  bool is_inline_style_ = false;

  friend class Builder;
};

class AddMatchedPropertiesOptions::Builder {
  STACK_ALLOCATED();

 public:
  AddMatchedPropertiesOptions Build() { return options_; }

  Builder& SetLinkMatchType(unsigned type) {
    options_.link_match_type_ = type;
    return *this;
  }

  Builder& SetValidPropertyFilter(ValidPropertyFilter filter) {
    options_.valid_property_filter_ = filter;
    return *this;
  }

  Builder& SetLayerOrder(unsigned layer_order) {
    options_.layer_order_ = layer_order;
    return *this;
  }

  Builder& SetIsInlineStyle(bool is_inline_style) {
    options_.is_inline_style_ = is_inline_style;
    return *this;
  }

 private:
  AddMatchedPropertiesOptions options_;
};

class CORE_EXPORT MatchResult {
  STACK_ALLOCATED();

 public:
  MatchResult() = default;
  MatchResult(const MatchResult&) = delete;
  MatchResult& operator=(const MatchResult&) = delete;

  void AddMatchedProperties(
      const CSSPropertyValueSet* properties,
      const AddMatchedPropertiesOptions& = AddMatchedPropertiesOptions());
  bool HasMatchedProperties() const { return matched_properties_.size(); }

  void FinishAddingUARules();
  void FinishAddingUserRules();
  void FinishAddingPresentationalHints();
  void FinishAddingAuthorRulesForTreeScope(const TreeScope&);

  void SetIsCacheable(bool cacheable) { is_cacheable_ = cacheable; }
  bool IsCacheable() const { return is_cacheable_; }
  void SetDependsOnSizeContainerQueries() {
    depends_on_size_container_queries_ = true;
  }
  bool DependsOnSizeContainerQueries() const {
    return depends_on_size_container_queries_;
  }
  void SetDependsOnStyleContainerQueries() {
    depends_on_size_container_queries_ = true;
  }
  bool DependsOnStyleContainerQueries() const {
    return depends_on_size_container_queries_;
  }
  void SetDependsOnStaticViewportUnits() {
    depends_on_static_viewport_units_ = true;
  }
  void SetDependsOnDynamicViewportUnits() {
    depends_on_dynamic_viewport_units_ = true;
  }
  bool DependsOnStaticViewportUnits() const {
    return depends_on_static_viewport_units_;
  }
  bool DependsOnDynamicViewportUnits() const {
    return depends_on_dynamic_viewport_units_;
  }
  void SetDependsOnRemContainerQueries() {
    depends_on_rem_container_queries_ = true;
  }
  bool DependsOnRemContainerQueries() const {
    return depends_on_rem_container_queries_;
  }
  void SetConditionallyAffectsAnimations() {
    conditionally_affects_animations_ = true;
  }
  bool ConditionallyAffectsAnimations() const {
    return conditionally_affects_animations_;
  }

  const MatchedPropertiesVector& GetMatchedProperties() const {
    return matched_properties_;
  }

  // Reset the MatchResult to its initial state, as if no MatchedProperties
  // objects were added.
  void Reset();

  const TreeScope& ScopeFromTreeOrder(uint16_t tree_order) const {
    SECURITY_DCHECK(tree_order < tree_scopes_.size());
    return *tree_scopes_[tree_order];
  }

 private:
  MatchedPropertiesVector matched_properties_;
  HeapVector<Member<const TreeScope>, 4> tree_scopes_;
  bool is_cacheable_{true};
  bool depends_on_size_container_queries_{false};
  bool depends_on_static_viewport_units_{false};
  bool depends_on_dynamic_viewport_units_{false};
  bool depends_on_rem_container_queries_{false};
  bool conditionally_affects_animations_{false};
  CascadeOrigin current_origin_{CascadeOrigin::kUserAgent};
  uint16_t current_tree_order_{0};
};

inline bool operator==(const MatchedProperties& a, const MatchedProperties& b) {
  return a.properties == b.properties &&
         a.types_.link_match_type == b.types_.link_match_type;
}

inline bool operator!=(const MatchedProperties& a, const MatchedProperties& b) {
  return !(a == b);
}

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_CORE_CSS_RESOLVER_MATCH_RESULT_H_
