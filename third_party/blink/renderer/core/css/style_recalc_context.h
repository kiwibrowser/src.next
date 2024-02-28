// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_CSS_STYLE_RECALC_CONTEXT_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_CSS_STYLE_RECALC_CONTEXT_H_

#include "third_party/blink/renderer/core/core_export.h"
#include "third_party/blink/renderer/platform/heap/collection_support/heap_vector.h"

namespace blink {

class ComputedStyle;
class Element;
class HTMLSlotElement;
class StyleScopeFrame;

// StyleRecalcContext is an object that is passed on the stack during
// the style recalc process.
//
// Its purpose is to hold context related to the style recalc process as
// a whole, i.e. information not directly associated to the specific element
// style is being calculated for.
class CORE_EXPORT StyleRecalcContext {
  STACK_ALLOCATED();

 public:
  // Using the ancestor chain, build a StyleRecalcContext suitable for
  // resolving the style of the given Element.
  //
  // It is valid to pass an Element without a ComputedStyle only when
  // the shadow-including parent of Element has a ComputedStyle.
  static StyleRecalcContext FromAncestors(Element&);

  // If the passed in StyleRecalcContext is nullptr, build a StyleRecalcContext
  // suitable for resolving the style for child elements of the passed in
  // element. Otherwise return the passed in context as a value.
  //
  // It is invalid to pass an Element without a ComputedStyle. This means that
  // if the Element is in display:none, the ComputedStyle must be ensured
  // before calling this function.
  static StyleRecalcContext FromInclusiveAncestors(Element&);

  // When traversing into slotted children, the container is in the shadow-
  // including inclusive ancestry of the slotted element's host. Return a
  // context with the container adjusted as necessary.
  StyleRecalcContext ForSlotChildren(const HTMLSlotElement& slot) const;

  // Called to update the context when matching ::slotted rules for shadow host
  // children. ::slotted rules may query containers inside the slot's shadow
  // tree as well.
  StyleRecalcContext ForSlottedRules(HTMLSlotElement& slot) const;

  // Called to update the context when matching ::part rules for shadow hosts.
  StyleRecalcContext ForPartRules(Element& host) const;

  // Set to the nearest container (for size container queries), if any.
  // This is used to evaluate container queries in ElementRuleCollector.
  Element* container = nullptr;

  // Used to decide which is the the closest style() @container candidate for
  // ::slotted() and ::part() rule matching. Otherwise nullptr.
  Element* style_container = nullptr;

  StyleScopeFrame* style_scope_frame = nullptr;

  // The style for the element at the start of the lifecycle update, or the
  // @starting-style styles for the second pass when transitioning from
  // display:none.
  const ComputedStyle* old_style = nullptr;

  // If false, something about the parent's style (e.g., that it has
  // modifications to one or more non-independent inherited properties)
  // forces a full recalculation of this element's style, precluding
  // any incremental style calculation. This is false by default so that
  // any “weird” calls to ResolveStyle() (e.g., those where the element
  // is not marked for recalc) don't get incremental style.
  //
  // NOTE: For the base computed style optimization, we do not only
  // rely on this, but also on the fact that the caller calls
  // SetAnimationStyleChange(false) directly. This is somewhat out of
  // legacy reasons.
  bool can_use_incremental_style = false;

  // True when we're ensuring the style of an element. This can only happen
  // when regular style can't reach the element (i.e. inside display:none, or
  // outside the flat tree).
  bool is_ensuring_style = false;

  // An element can be outside the flat tree if it's a non-slotted
  // child of a shadow host, or a descendant of such a child.
  // ComputedStyles produced under these circumstances need to be marked
  // as such, primarily for the benefit of
  // Element::MarkNonSlottedHostChildrenForStyleRecalc.
  //
  // TODO(crbug.com/831568): Elements outside the flat tree should
  // not have a style.
  bool is_outside_flat_tree = false;

  // True if we're computing the position fallback style of an element
  // triggered by layout. Note however that try styles may still be included
  // when this flag is false (see PositionFallbackData,
  // "speculative @try styling").
  bool is_position_fallback = false;
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_CORE_CSS_STYLE_RECALC_CONTEXT_H_
