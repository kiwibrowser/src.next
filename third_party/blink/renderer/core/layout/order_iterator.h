/*
 * Copyright (C) 2011 Google Inc. All rights reserved.
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

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_LAYOUT_ORDER_ITERATOR_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_LAYOUT_ORDER_ITERATOR_H_

#include "third_party/blink/renderer/platform/heap/member.h"
#include "third_party/blink/renderer/platform/heap/visitor.h"
#include "third_party/blink/renderer/platform/wtf/allocator/allocator.h"

#include <set>

namespace blink {

class LayoutBox;

class OrderIterator {
  DISALLOW_NEW();

 public:
  friend class OrderIteratorPopulator;

  explicit OrderIterator(const LayoutBox*);
  OrderIterator(const OrderIterator&) = delete;
  OrderIterator& operator=(const OrderIterator&) = delete;

  LayoutBox* CurrentChild() { return current_child_; }
  const LayoutBox* CurrentChild() const { return current_child_; }
  LayoutBox* First();
  const LayoutBox* First() const {
    return const_cast<OrderIterator*>(this)->First();
  }
  LayoutBox* Next();
  const LayoutBox* Next() const {
    return const_cast<OrderIterator*>(this)->Next();
  }

  void Trace(Visitor* visitor) const {
    visitor->Trace(container_box_);
    visitor->Trace(current_child_);
  }

 private:
  void Reset();

  // Returns the order to use for |child|.
  int ResolvedOrder(const LayoutBox& child) const;

  Member<const LayoutBox> container_box_;

  Member<LayoutBox> current_child_;

  using OrderValues = std::set<int>;
  OrderValues order_values_;
  OrderValues::const_iterator order_values_iterator_;
  // Set by |Reset()|, triggers iteration to start from the beginning.
  bool is_reset_ = false;
};

class OrderIteratorPopulator {
  STACK_ALLOCATED();

 public:
  explicit OrderIteratorPopulator(OrderIterator& iterator)
      : iterator_(iterator) {
    iterator_.order_values_.clear();
  }

  ~OrderIteratorPopulator();

  void CollectChild(const LayoutBox*);

 private:
  OrderIterator& iterator_;
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_CORE_LAYOUT_ORDER_ITERATOR_H_
