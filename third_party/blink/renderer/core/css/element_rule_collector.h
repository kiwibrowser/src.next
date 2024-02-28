/*
 * Copyright (C) 1999 Lars Knoll (knoll@kde.org)
 * Copyright (C) 2003, 2004, 2005, 2006, 2007, 2008, 2009, 2010, 2011 Apple Inc.
 * All rights reserved.
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

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_CSS_ELEMENT_RULE_COLLECTOR_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_CSS_ELEMENT_RULE_COLLECTOR_H_

#include "base/auto_reset.h"
#include "base/gtest_prod_util.h"
#include "base/memory/scoped_refptr.h"
#include "third_party/blink/renderer/core/core_export.h"
#include "third_party/blink/renderer/core/css/container_selector.h"
#include "third_party/blink/renderer/core/css/css_rule_list.h"
#include "third_party/blink/renderer/core/css/resolver/element_resolve_context.h"
#include "third_party/blink/renderer/core/css/resolver/match_request.h"
#include "third_party/blink/renderer/core/css/resolver/match_result.h"
#include "third_party/blink/renderer/core/css/selector_checker.h"
#include "third_party/blink/renderer/core/css/style_recalc_context.h"
#include "third_party/blink/renderer/core/css/style_request.h"
#include "third_party/blink/renderer/core/style/computed_style_base_constants.h"
#include "third_party/blink/renderer/platform/wtf/ref_counted.h"
#include "third_party/blink/renderer/platform/wtf/vector.h"

namespace blink {

class Element;
class ElementRuleCollector;
class HTMLSlotElement;
class PartNames;
class RuleData;
class SelectorFilter;
class StyleRuleUsageTracker;

class MatchedRule {
  DISALLOW_NEW();

  // Everything in this class is private to ElementRuleCollector, since it
  // contains non-owned references to RuleData (see the constructor), but we
  // cannot make the class itself private, since
  // WTF_ALLOW_MOVE_AND_INIT_WITH_MEM_FUNCTIONS() and Vector::emplace_back()
  // need it to be visible from the outside.
  static const unsigned kBitsForPositionInRuleData = 18;

 public:
  // Does not take overship of rule_data (it is owned by the appropriate
  // bucket in RuleSet), so the RuleData must live for at least as long as
  // the MatchedRule, ie., those buckets must not be modified (which would
  // invalidate the RuleData pointers). This is fine, because MatchedRule
  // is only used during matching (in ElementRuleCollector), and the
  // RuleData itself never escapes SortAndTransferMatchedRules() -- only
  // the other elements that it points to.
  MatchedRule(const RuleData* rule_data,
              unsigned layer_order,
              unsigned proximity,
              unsigned style_sheet_index)
      : rule_data_(rule_data),
        layer_order_(layer_order),
        proximity_(proximity),
        position_((static_cast<uint64_t>(style_sheet_index)
                   << kBitsForPositionInRuleData) +
                  rule_data->GetPosition()) {}

 private:
  const RuleData* GetRuleData() const { return rule_data_; }
  uint64_t GetPosition() const { return position_; }
  unsigned Specificity() const { return GetRuleData()->Specificity(); }
  unsigned LayerOrder() const { return layer_order_; }
  unsigned Proximity() const { return proximity_; }

 private:
  const RuleData* rule_data_;
  unsigned layer_order_;
  // https://drafts.csswg.org/css-cascade-6/#weak-scoping-proximity
  unsigned proximity_;
  uint64_t position_;

  friend class ElementRuleCollector;
  FRIEND_TEST_ALL_PREFIXES(ElementRuleCollectorTest, DirectNesting);
  FRIEND_TEST_ALL_PREFIXES(ElementRuleCollectorTest,
                           RuleNotStartingWithAmpersand);
  FRIEND_TEST_ALL_PREFIXES(ElementRuleCollectorTest, NestedRulesInMediaQuery);
};

}  // namespace blink

WTF_ALLOW_MOVE_AND_INIT_WITH_MEM_FUNCTIONS(blink::MatchedRule)

namespace blink {

using StyleRuleList = HeapVector<Member<StyleRule>>;

// Manages the process of finding what rules in a RuleSet apply to a given
// Element. These tend to be used several times in different contexts and should
// have ClearMatchedRules called before use.
//
// ElementRuleCollector is designed to be used as a stack object.
// Create one, ask what rules the ElementResolveContext matches
// and then let it go out of scope. In particular, do not change
// values in the RuleSet buckets (which would invalidate the RuleData
// pointers) before you have extracted the results, typically with
// SortAndTransferMatchedRules().
//
// FIXME: Currently it modifies the ComputedStyle but should not!
class CORE_EXPORT ElementRuleCollector {
  STACK_ALLOCATED();

 public:
  ElementRuleCollector(const ElementResolveContext&,
                       const StyleRecalcContext&,
                       const SelectorFilter&,
                       MatchResult&,
                       EInsideLink);
  ElementRuleCollector(const ElementRuleCollector&) = delete;
  ElementRuleCollector& operator=(const ElementRuleCollector&) = delete;
  ~ElementRuleCollector();

  void SetMode(SelectorChecker::Mode mode) { mode_ = mode; }
  void SetPseudoElementStyleRequest(const StyleRequest& request) {
    pseudo_style_request_ = request;
  }

  void SetMatchingUARules(bool matching_ua_rules) {
    matching_ua_rules_ = matching_ua_rules;
  }
  void SetMatchingRulesFromNoStyleSheet(
      bool matching_rules_from_no_style_sheet) {
    matching_rules_from_no_style_sheet_ = matching_rules_from_no_style_sheet;
  }
  // If true, :visited will never match. Has no effect otherwise.
  void SetSuppressVisited(bool suppress_visited) {
    suppress_visited_ = suppress_visited;
  }

  const MatchResult& MatchedResult() const;
  StyleRuleList* MatchedStyleRuleList();
  RuleIndexList* MatchedCSSRuleList();

  void CollectMatchingRules(const MatchRequest&);
  void CollectMatchingShadowHostRules(const MatchRequest&);
  void CollectMatchingSlottedRules(const MatchRequest&);
  void CollectMatchingPartPseudoRules(const MatchRequest&,
                                      PartNames&,
                                      bool for_shadow_pseudo);
  void SortAndTransferMatchedRules(CascadeOrigin origin,
                                   bool is_vtt_embedded_style,
                                   StyleRuleUsageTracker* tracker);
  void ClearMatchedRules();

  // Cheaper versions of CollectMatchingRules and CollectMatchingShadowHostRules
  // respectively, that only return true/false instead of actually collecting
  // the rules.
  bool CheckIfAnyRuleMatches(const MatchRequest&);
  bool CheckIfAnyShadowHostRuleMatches(const MatchRequest&);

  // True if an entire StyleScope can be rejected, i.e. all style rules
  // within the StyleScope are guaranteed to not match due to the given
  // StyleScope not being in scope [1].
  //
  // Return 'false' when we don't know if a StyleScope is in scope or not.
  //
  // [1] https://drafts.csswg.org/css-cascade-6/#in-scope
  bool CanRejectScope(const StyleScope&);

  void AddElementStyleProperties(const CSSPropertyValueSet*,
                                 CascadeOrigin,
                                 bool is_cacheable = true,
                                 bool is_inline_style = false);
  void AddTryStyleProperties(const CSSPropertyValueSet*);
  void BeginAddingAuthorRulesForTreeScope(const TreeScope& tree_scope) {
    current_matching_tree_scope_ = &tree_scope;
    result_.BeginAddingAuthorRulesForTreeScope(tree_scope);
  }
  void FinishAddingAuthorRulesForTreeScope() {
    current_matching_tree_scope_ = nullptr;
  }

  // Return the pseudo id if the style request is for rules associated with a
  // pseudo element, or kPseudoNone if not.
  PseudoId GetPseudoId() const { return pseudo_style_request_.pseudo_id; }
  const AtomicString& GetPseudoArgument() const {
    return pseudo_style_request_.pseudo_argument;
  }

  void AddMatchedRulesToTracker(StyleRuleUsageTracker*) const;

  // Writes out the collected selector statistics and clears the values.
  // These values are gathered during rule matching and require higher-level
  // control of when they are output - the statistics are designed to be
  // aggregated per-rule for the entire style recalc pass.
  static void DumpAndClearRulesPerfMap();

  const HeapVector<MatchedRule, 32>& MatchedRulesForTest() const {
    return matched_rules_;
  }

  // Temporarily swap the StyleRecalcContext with one which points to the
  // closest query container for matching ::slotted rules for a given slot.
  class SlottedRulesScope {
    STACK_ALLOCATED();

   public:
    SlottedRulesScope(ElementRuleCollector& collector, HTMLSlotElement& slot)
        : context_(&collector.style_recalc_context_,
                   collector.style_recalc_context_.ForSlottedRules(slot)) {}

   private:
    base::AutoReset<StyleRecalcContext> context_;
  };

  // Temporarily swap the StyleRecalcContext with one which points to the
  // closest query container for matching ::part rules for a given host.
  class PartRulesScope {
    STACK_ALLOCATED();

   public:
    PartRulesScope(ElementRuleCollector& collector, Element& host)
        : context_(&collector.style_recalc_context_,
                   collector.style_recalc_context_.ForPartRules(host)) {}

   private:
    base::AutoReset<StyleRecalcContext> context_;
  };

 private:
  struct PartRequest {
    PartNames& part_names;
    // If this is true, we're matching for a pseudo-element of the part, such as
    // ::placeholder.
    bool for_shadow_pseudo = false;
  };

  // If stop_at_first_match = true, CollectMatchingRules*() will stop
  // whenever any rule matches, return true, and not store the result
  // anywhere nor update the match counters. Otherwise, these functions
  // will return false (even if one or more rules matched).
  //
  // Note in the context of stop_at_first_match, a match against any
  // pseudo rule in the element counts as a match (e.g., “div::before”
  // will match the <div> element, not just its ::before pseudo-element).
  // This is convenient because this mode is used for invalidation on
  // changed rulesets only, where such a match causes us to have to
  // invalidate style on the element anyway.

  template <bool stop_at_first_match>
  bool CollectMatchingRulesInternal(const MatchRequest&);

  template <bool stop_at_first_match, bool perf_trace_enabled>
  bool CollectMatchingRulesForListInternal(base::span<const RuleData>,
                                           const MatchRequest&,
                                           const RuleSet*,
                                           int,
                                           const SelectorChecker&,
                                           PartRequest* = nullptr);

  template <bool stop_at_first_match>
  bool CollectMatchingRulesForList(base::span<const RuleData>,
                                   const MatchRequest&,
                                   const RuleSet*,
                                   int,
                                   const SelectorChecker&,
                                   PartRequest* = nullptr);

  bool Match(SelectorChecker&,
             const SelectorChecker::SelectorCheckingContext&,
             MatchResult&);
  void DidMatchRule(const RuleData*,
                    unsigned layer_order,
                    const ContainerQuery*,
                    unsigned proximity,
                    const SelectorChecker::MatchResult&,
                    int style_sheet_index);

  void AppendCSSOMWrapperForRule(const TreeScope* tree_scope_containing_rule,
                                 const RuleData*,
                                 wtf_size_t);

  void SortMatchedRules();

  RuleIndexList* EnsureRuleList();
  StyleRuleList* EnsureStyleRuleList();

 private:
  static inline bool CompareRules(const MatchedRule& matched_rule1,
                                  const MatchedRule& matched_rule2);

  const ElementResolveContext& context_;
  StyleRecalcContext style_recalc_context_;
  const SelectorFilter& selector_filter_;

  StyleRequest pseudo_style_request_;
  SelectorChecker::Mode mode_;
  bool can_use_fast_reject_;
  bool matching_ua_rules_;
  bool matching_rules_from_no_style_sheet_ =
      false;  // Document rules and watched selectors.
  bool suppress_visited_;
  EInsideLink inside_link_;
  const TreeScope* current_matching_tree_scope_ = nullptr;

  HeapVector<MatchedRule, 32> matched_rules_;
  ContainerSelectorCache container_selector_cache_;

  // Output.
  Member<RuleIndexList> css_rule_list_;
  Member<StyleRuleList> style_rule_list_;
  MatchResult& result_;
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_CORE_CSS_ELEMENT_RULE_COLLECTOR_H_
