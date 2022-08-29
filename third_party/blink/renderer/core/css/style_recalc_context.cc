// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/css/style_recalc_context.h"

#include "third_party/blink/renderer/core/dom/layout_tree_builder_traversal.h"
#include "third_party/blink/renderer/core/dom/node_computed_style.h"
#include "third_party/blink/renderer/core/html/html_slot_element.h"

namespace blink {

namespace {

Element* ClosestInclusiveAncestorContainer(Element& element,
                                           Element* stay_within = nullptr) {
  for (auto* container = &element; container && container != stay_within;
       container = container->ParentOrShadowHostElement()) {
    if (container->GetContainerQueryEvaluator())
      return container;
  }
  return nullptr;
}

}  // namespace

StyleRecalcContext StyleRecalcContext::FromInclusiveAncestors(
    Element& element) {
  if (!RuntimeEnabledFeatures::CSSContainerQueriesEnabled())
    return StyleRecalcContext();

  return StyleRecalcContext{ClosestInclusiveAncestorContainer(element)};
}

StyleRecalcContext StyleRecalcContext::FromAncestors(Element& element) {
  if (!RuntimeEnabledFeatures::CSSContainerQueriesEnabled())
    return StyleRecalcContext();

  // TODO(crbug.com/1145970): Avoid this work if we're not inside a container
  if (Element* shadow_including_parent = element.ParentOrShadowHostElement())
    return FromInclusiveAncestors(*shadow_including_parent);
  return StyleRecalcContext();
}

StyleRecalcContext StyleRecalcContext::ForSlotChildren(
    const HTMLSlotElement& slot) const {
  // If the container is in a different tree scope, it is already in the shadow-
  // including inclusive ancestry of the host.
  if (!container || container->GetTreeScope() != slot.GetTreeScope())
    return *this;

  DCHECK(RuntimeEnabledFeatures::CSSContainerQueriesEnabled());

  // No assigned nodes means we will render the light tree children of the
  // slot as a fallback. Those children are in the same tree scope as the slot
  // which means the current container is the correct one.
  if (slot.AssignedNodes().IsEmpty())
    return *this;

  // The slot's flat tree children are children of the slot's shadow host, and
  // their container is in the shadow-including inclusive ancestors of the host.
  DCHECK(slot.IsInShadowTree());
  Element* host = slot.OwnerShadowHost();
  DCHECK(host);
  return StyleRecalcContext{FromInclusiveAncestors(*host)};
}

StyleRecalcContext StyleRecalcContext::ForSlottedRules(
    HTMLSlotElement& slot) const {
  if (!RuntimeEnabledFeatures::CSSContainerQueriesEnabled())
    return *this;

  // The current container is the shadow-including inclusive ancestors of the
  // host. When matching ::slotted rules, the closest container may be found in
  // the shadow-including inclusive ancestry of the slot. If we reach the host,
  // the current container is still the closest one.
  if (Element* shadow_container =
          ClosestInclusiveAncestorContainer(slot, slot.OwnerShadowHost())) {
    return StyleRecalcContext{shadow_container};
  }
  return *this;
}

StyleRecalcContext StyleRecalcContext::ForPartRules(Element& host) const {
  if (!container || !RuntimeEnabledFeatures::CSSContainerQueriesEnabled())
    return *this;

  // The closest container for matching ::part rules is the originating host.
  // There is no need to walk past the current container.
  if (Element* host_container =
          ClosestInclusiveAncestorContainer(host, container)) {
    return StyleRecalcContext{host_container};
  }
  return *this;
}

}  // namespace blink
