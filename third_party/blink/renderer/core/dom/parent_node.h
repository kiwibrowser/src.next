/*
 * Copyright (C) 2013 Samsung Electronics. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_DOM_PARENT_NODE_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_DOM_PARENT_NODE_H_

#include "third_party/blink/renderer/core/dom/container_node.h"
#include "third_party/blink/renderer/core/dom/element_traversal.h"
#include "third_party/blink/renderer/core/html/html_collection.h"
#include "third_party/blink/renderer/platform/heap/collection_support/heap_vector.h"
#include "third_party/blink/renderer/platform/heap/member.h"
#include "third_party/blink/renderer/platform/wtf/allocator/allocator.h"

namespace blink {

class ParentNode {
  STATIC_ONLY(ParentNode);

 public:
  static HTMLCollection* children(ContainerNode& node) {
    return node.Children();
  }

  static Element* firstElementChild(ContainerNode& node) {
    return ElementTraversal::FirstChild(node);
  }

  static Element* lastElementChild(ContainerNode& node) {
    return ElementTraversal::LastChild(node);
  }

  static unsigned childElementCount(ContainerNode& node) {
    unsigned count = 0;
    for (Element* child = ElementTraversal::FirstChild(node); child;
         child = ElementTraversal::NextSibling(*child))
      ++count;
    return count;
  }

  static void prepend(
      Node& node,
      const HeapVector<Member<V8UnionNodeOrStringOrTrustedScript>>& nodes,
      ExceptionState& exception_state) {
    return node.Prepend(nodes, exception_state);
  }

  static void append(
      Node& node,
      const HeapVector<Member<V8UnionNodeOrStringOrTrustedScript>>& nodes,
      ExceptionState& exception_state) {
    return node.Append(nodes, exception_state);
  }

  static void replaceChildren(
      Node& node,
      const HeapVector<Member<V8UnionNodeOrStringOrTrustedScript>>& nodes,
      ExceptionState& exception_state) {
    return node.ReplaceChildren(nodes, exception_state);
  }

  static Element* querySelector(ContainerNode& node,
                                const AtomicString& selectors,
                                ExceptionState& exception_state) {
    return node.QuerySelector(selectors, exception_state);
  }

  static StaticElementList* querySelectorAll(ContainerNode& node,
                                             const AtomicString& selectors,
                                             ExceptionState& exception_state) {
    return node.QuerySelectorAll(selectors, exception_state);
  }
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_CORE_DOM_PARENT_NODE_H_
