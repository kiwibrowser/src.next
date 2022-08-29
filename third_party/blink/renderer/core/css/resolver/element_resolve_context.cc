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

#include "third_party/blink/renderer/core/css/resolver/element_resolve_context.h"

#include "third_party/blink/renderer/core/dom/layout_tree_builder_traversal.h"
#include "third_party/blink/renderer/core/dom/node.h"
#include "third_party/blink/renderer/core/dom/node_computed_style.h"
#include "third_party/blink/renderer/core/dom/visited_link_state.h"

namespace blink {

ElementResolveContext::ElementResolveContext(Element& element)
    : element_(&element),
      parent_node_(nullptr),
      layout_parent_(nullptr),
      element_link_state_(
          element.GetDocument().GetVisitedLinkState().DetermineLinkState(
              element)) {
  parent_node_ = LayoutTreeBuilderTraversal::Parent(element);
  layout_parent_ = LayoutTreeBuilderTraversal::LayoutParent(element);

  if (auto* document_element = element.GetDocument().documentElement()) {
    if (element != document_element)
      root_element_style_ = document_element->GetComputedStyle();
  }
}

}  // namespace blink
