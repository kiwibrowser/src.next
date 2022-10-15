/*
 * Copyright (C) 1999 Lars Knoll (knoll@kde.org)
 *           (C) 2004-2005 Allan Sandfeld Jensen (kde@carewolf.com)
 * Copyright (C) 2006, 2007 Nicholas Shanks (webkit@nickshanks.com)
 * Copyright (C) 2005, 2006, 2007, 2008, 2009, 2010, 2011, 2012, 2013 Apple Inc.
 * All rights reserved.
 * Copyright (C) 2007 Alexey Proskuryakov <ap@webkit.org>
 * Copyright (C) 2007, 2008 Eric Seidel <eric@webkit.org>
 * Copyright (C) 2008, 2009 Torch Mobile Inc. All rights reserved.
 * (http://www.torchmobile.com/)
 * Copyright (c) 2011, Code Aurora Forum. All rights reserved.
 * Copyright (C) Research In Motion Limited 2011. All rights reserved.
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

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_CSS_SELECTOR_CHECKER_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_CSS_SELECTOR_CHECKER_H_

#include <limits>
#include "base/dcheck_is_on.h"
#include "third_party/blink/renderer/core/core_export.h"
#include "third_party/blink/renderer/core/css/css_selector.h"
#include "third_party/blink/renderer/core/css/style_request.h"
#include "third_party/blink/renderer/core/css/style_scope.h"
#include "third_party/blink/renderer/core/dom/element.h"
#include "third_party/blink/renderer/core/scroll/scroll_types.h"
#include "third_party/blink/renderer/platform/heap/collection_support/heap_hash_map.h"
#include "third_party/blink/renderer/platform/heap/collection_support/heap_vector.h"
#include "third_party/blink/renderer/platform/heap/garbage_collected.h"
#include "third_party/blink/renderer/platform/heap/member.h"
#include "third_party/blink/renderer/platform/heap/visitor.h"

namespace blink {

class CSSSelector;
class ContainerNode;
class CustomScrollbar;
class ComputedStyle;
class Element;
class PartNames;

class CORE_EXPORT SelectorChecker {
  STACK_ALLOCATED();

 public:
  enum Mode {
    // Used when matching selectors inside style recalc. This mode will set
    // restyle flags across the tree during matching which impact how style
    // sharing and invalidation work later.
    kResolvingStyle,

    // Used when collecting which rules match into a StyleRuleList, the engine
    // internal represention.
    //
    // TODO(esprehn): This doesn't change the behavior of the SelectorChecker
    // we should merge it with a generic CollectingRules mode.
    kCollectingStyleRules,

    // Used when collecting which rules match into a CSSRuleList, the CSSOM api
    // represention.
    //
    // TODO(esprehn): This doesn't change the behavior of the SelectorChecker
    // we should merge it with a generic CollectingRules mode.
    kCollectingCSSRules,

    // Used when matching rules for querySelector and <content select>. This
    // disables the special handling for positional selectors during parsing
    // and also enables static profile only selectors like >>>.
    kQueryingRules,
  };

  explicit inline SelectorChecker(const Mode& mode)
      : element_style_(nullptr),
        scrollbar_(nullptr),
        part_names_(nullptr),
        pseudo_argument_(g_null_atom),
        scrollbar_part_(kNoPart),
        mode_(mode),
        is_ua_rule_(false) {}
  inline SelectorChecker(ComputedStyle* element_style,
                         PartNames* part_names,
                         const StyleRequest& style_request,
                         const Mode& mode,
                         const bool& is_ua_rule)
      : element_style_(element_style),
        scrollbar_(style_request.scrollbar),
        part_names_(part_names),
        pseudo_argument_(style_request.pseudo_argument),
        scrollbar_part_(style_request.scrollbar_part),
        mode_(mode),
        is_ua_rule_(is_ua_rule) {}

  SelectorChecker(const SelectorChecker&) = delete;
  SelectorChecker& operator=(const SelectorChecker&) = delete;

  struct StyleScopeActivation {
    DISALLOW_NEW();

   public:
    void Trace(blink::Visitor*) const;

    // The root is the element when the activation happened. In other words,
    // the element that matched <scope-start>.
    //
    // https://drafts.csswg.org/css-cascade-6/#typedef-scope-start
    Member<Element> root;
    // The distance to the root, in terms of number of inclusive ancestors
    // between some subject element and the root.
    unsigned proximity = 0;
    // True if some subject element matches <scope-end>.
    //
    // https://drafts.csswg.org/css-cascade-6/#typedef-scope-end
    bool limit = false;
  };

  // Stores the current @scope activations for a given subject element.
  //
  // See documentation near EnsureActivations for more information.
  //
  // TODO(crbug.com/1280240): Provide a parent frame in the future.
  class StyleScopeFrame {
    STACK_ALLOCATED();

   public:
    using Activations = HeapVector<StyleScopeActivation>;

    explicit StyleScopeFrame(Element& element) : element_(element) {}

   private:
    friend class SelectorChecker;

    Element& element_;
    HeapHashMap<Member<const StyleScope>, Member<const Activations>> data_;
  };

  // Wraps the current element and a CSSSelector and stores some other state of
  // the selector matching process.
  struct SelectorCheckingContext {
    STACK_ALLOCATED();

   public:
    // Initial selector constructor
    explicit SelectorCheckingContext(Element* element) : element(element) {}

    // Group fields by type to avoid perf test regression.
    // https://crrev.com/c/3362008
    const CSSSelector* selector = nullptr;

    // Used to match the :scope pseudo-class.
    const ContainerNode* scope = nullptr;
    // If `style_scope` is specified, that is used to match the :scope
    // pseudo-class instead (and `scope` is ignored).
    const StyleScope* style_scope = nullptr;
    // StyleScopeFrame is required if style_scope is non-nullptr.
    StyleScopeFrame* style_scope_frame = nullptr;

    Element* element = nullptr;
    Element* previous_element = nullptr;
    Element* vtt_originating_element = nullptr;
    ContainerNode* relative_anchor_element = nullptr;

    PseudoId pseudo_id = kPseudoIdNone;

    bool is_sub_selector = false;
    bool in_rightmost_compound = true;
    bool has_scrollbar_pseudo = false;
    bool has_selection_pseudo = false;
    bool treat_shadow_host_as_normal_scope = false;
    bool in_nested_complex_selector = false;
    bool is_inside_visited_link = false;
    bool pseudo_has_in_rightmost_compound = true;
    bool is_inside_has_pseudo_class = false;
  };

  struct MatchResult {
    STACK_ALLOCATED();

   public:
    PseudoId dynamic_pseudo{kPseudoIdNone};

    // Comes from an AtomicString, but not stored as one to avoid
    // the cost of checking the refcount on cleaning up from every
    // Match() call. Owned by the CSS selector it came from.
    StringImpl* custom_highlight_name{nullptr};

    // From the :has() argument selector checking, we need to get the element
    // that matches the leftmost compound selector to mark all possible :has()
    // anchor elements (the relative anchor element of the :has() argument).
    //
    // <main id=main>
    //   <div id=d1>
    //     <div id=d2 class="a">
    //       <div id=d3 class="a">
    //         <div id=d4>
    //           <div id=d5 class="b">
    //           </div>
    //         </div>
    //       </div>
    //     </div>
    //   </div>
    // </div>
    // <script>
    //  main.querySelectorAll('div:has(.a .b)'); // Should return #d1, #d2
    // </script>
    //
    // In case of the above example, the selector 'div:has(.a .b)' is checked
    // on the descendants of '#main' element in this order:
    // - 'div#d1', 'div#d2', 'div#d3', 'div#d4', 'div#d5'
    // When checking the selector on 'div#d1', we can get all possible :has()
    // anchor element while checking the :has() argument selector ('.a .b')
    // on the descendants of 'div#d1'.
    // Among the descendants of 'div#d1', 'div#d5' matches the argument selector
    // '.a .b'. More precisely, the 'div#d5' matches the argument selector
    // ':-internal-relative-anchor .a .b' only when the ':-internal-relative-
    // anchor' matches any ancestors of the element matches the leftmost
    // compound of the argument selector ('.a').
    // So, in case of checking the 'div:has(.a .b)' on 'div#d1', 'div#d1' and
    // 'div#d2' can be a :has() argument anchor element because 'div#d3' and
    // 'div#d4' are the element that matches the leftmost compound '.a' of the
    // :has() argument '.a .b'.
    // To avoid repetitive argument checking, the :has() anchor elements are
    // stored in the CheckPseudoHasResultCache. To cache the anchor elements
    // correctly, MatchResult returns the elements that match the leftmost
    // compound of the :has() argument selector.
    //
    // This field is only for checking :has() pseudo class. To avoid the
    // MatchResult instance allocation overhead on checking the other selectors,
    // MatchResult has a pointer field to hold the reference of the vector
    // instance instead of having the vector instance field.
    HeapVector<Member<Element>>* has_argument_leftmost_compound_matches{
        nullptr};
    unsigned proximity{std::numeric_limits<unsigned>::max()};
  };

  bool Match(const SelectorCheckingContext& context, MatchResult& result) const;

  bool Match(const SelectorCheckingContext& context) const {
    MatchResult ignore_result;
    return Match(context, ignore_result);
  }

  static bool MatchesFocusPseudoClass(const Element&);
  static bool MatchesFocusVisiblePseudoClass(const Element&);
  static bool MatchesSpatialNavigationInterestPseudoClass(const Element&);
  static bool MatchesSelectorFragmentAnchorPseudoClass(const Element&);

 private:
  // Does the work of checking whether the simple selector and element pointed
  // to by the context are a match. Delegates most of the work to the Check*
  // methods below.
  bool CheckOne(const SelectorCheckingContext&, MatchResult&) const;

  enum MatchStatus {
    kSelectorMatches,
    kSelectorFailsLocally,
    kSelectorFailsAllSiblings,
    kSelectorFailsCompletely
  };

  // MatchSelector is the core of the recursive selector matching process. It
  // calls through to the Match* methods below and Match above.
  //
  // At each level of the recursion the context (which selector and element we
  // are considering) is represented by a SelectorCheckingContext. A context may
  // also contain a scope, this can limit the matching to occur within a
  // specific shadow tree (and its descendants). As the recursion proceeds, new
  // `SelectorCheckingContext` objects are created by copying a previous one and
  // changing the selector and/or the element being matched
  //
  // MatchSelector uses CheckOne to determine what element matches the current
  // selector. If CheckOne succeeds we recurse with a new context pointing to
  // the next selector (in a selector list, we proceed leftwards through the
  // compound selectors). If CheckOne fails we may try again with a different
  // element or we may fail the match entirely. In both cases, the next element
  // to try (e.g. same element, parent, sibling) depends on the combinators in
  // the selectors.
  MatchStatus MatchSelector(const SelectorCheckingContext&, MatchResult&) const;
  MatchStatus MatchForSubSelector(const SelectorCheckingContext&,
                                  MatchResult&) const;
  MatchStatus MatchForRelation(const SelectorCheckingContext&,
                               MatchResult&) const;
  MatchStatus MatchForPseudoContent(const SelectorCheckingContext&,
                                    const Element&,
                                    MatchResult&) const;
  MatchStatus MatchForPseudoShadow(const SelectorCheckingContext&,
                                   const ContainerNode*,
                                   MatchResult&) const;
  bool CheckPseudoClass(const SelectorCheckingContext&, MatchResult&) const;
  bool CheckPseudoElement(const SelectorCheckingContext&, MatchResult&) const;
  bool CheckScrollbarPseudoClass(const SelectorCheckingContext&,
                                 MatchResult&) const;
  bool CheckPseudoHost(const SelectorCheckingContext&, MatchResult&) const;
  bool CheckPseudoScope(const SelectorCheckingContext&, MatchResult&) const;
  bool CheckPseudoNot(const SelectorCheckingContext&, MatchResult&) const;
  bool CheckPseudoHas(const SelectorCheckingContext&, MatchResult&) const;

  // The *activations* for a given StyleScope/element, is a list of active
  // scopes found in the ancestor chain, their roots (Element*), and the
  // proximities to those roots.
  //
  // The idea is that, if we're matching a selector ':scope' within some
  // StyleScope, we look up the activations for that StyleScope, and
  // and check if the current element (`SelectorCheckingContext.element`)
  // matches any of the activation roots.
  using Activations = StyleScopeFrame::Activations;

  const Activations& EnsureActivations(const SelectorCheckingContext&,
                                       const StyleScope&) const;
  const Activations* CalculateActivations(
      Element&,
      const StyleScope&,
      const Activations& outer_activations) const;
  bool CheckInStyleScope(const SelectorCheckingContext&, MatchResult&) const;
  bool MatchesWithScope(Element&, const CSSSelectorList&, Element* scope) const;

  ComputedStyle* element_style_;
  CustomScrollbar* scrollbar_;
  PartNames* part_names_;
  const String pseudo_argument_;
  ScrollbarPart scrollbar_part_;
  Mode mode_;
  bool is_ua_rule_;
#if DCHECK_IS_ON()
  mutable bool inside_match_ = false;
#endif
};

}  // namespace blink

WTF_ALLOW_MOVE_INIT_AND_COMPARE_WITH_MEM_FUNCTIONS(
    blink::SelectorChecker::StyleScopeActivation)
WTF_ALLOW_MOVE_INIT_AND_COMPARE_WITH_MEM_FUNCTIONS(
    blink::SelectorChecker::StyleScopeFrame)

#endif  // THIRD_PARTY_BLINK_RENDERER_CORE_CSS_SELECTOR_CHECKER_H_
