// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_LAYOUT_BLOCK_CHILD_ITERATOR_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_LAYOUT_BLOCK_CHILD_ITERATOR_H_

#include <optional>

#include "third_party/blink/renderer/core/core_export.h"
#include "third_party/blink/renderer/core/layout/block_node.h"
#include "third_party/blink/renderer/core/layout/inline/inline_break_token.h"

namespace blink {

class BlockBreakToken;

// A utility class for block-flow layout which given the first child and a
// break token will iterate through unfinished children.
//
// NextChild() is used to iterate through the children. This will be done in
// node order. If there are child break tokens, though, their nodes will be
// processed first, in break token order (which may or may not be the same as
// node order). When we're through those, we proceed to the next sibling node of
// that of the last break token - unless we have already seen and started all
// children (in which case the parent break token will be marked as such;
// |HasSeenAllChildren()| will return true).
//
// This class does not handle modifications to its arguments after it has been
// constructed.
class CORE_EXPORT BlockChildIterator {
  STACK_ALLOCATED();

 public:
  BlockChildIterator(LayoutInputNode first_child,
                     const BlockBreakToken* container_break_token,
                     bool calculate_child_idx = false);

  // Returns the next input node which should be laid out, along with its
  // respective break token.
  // @param previous_inline_break_token The previous inline break token is
  //    needed as multiple line-boxes can exist within the same parent
  //    fragment, unlike blocks.
  struct Entry;
  Entry NextChild(
      const InlineBreakToken* previous_inline_break_token = nullptr);

 private:
  void AdvanceToNextChild(const BlockNode&);

  BlockNode next_unstarted_child_ = nullptr;
  BlockNode tracked_child_ = nullptr;
  const BlockBreakToken* container_break_token_;

  // An index into break_token_'s ChildBreakTokens() vector. Used for keeping
  // track of the next child break token to inspect.
  wtf_size_t child_token_idx_;

  std::optional<wtf_size_t> child_idx_;

  bool did_handle_first_child_ = false;
  bool is_ifc_ = false;
};

struct BlockChildIterator::Entry {
  STACK_ALLOCATED();

 public:
  Entry() = default;
  Entry(const BlockNode& block_node,
        const BreakToken* token,
        std::optional<wtf_size_t> index = std::nullopt)
      : block_node(block_node), token(token), index(index) {}
  explicit Entry(const InlineBreakToken* token, bool is_unstarted_ifc = false)
      : token(token), is_unstarted_ifc(is_unstarted_ifc) {}

  BlockNode block_node = nullptr;
  const BreakToken* token = nullptr;
  std::optional<wtf_size_t> index;
  bool is_unstarted_ifc = false;

  bool operator==(const BlockChildIterator::Entry& other) const {
    return block_node == other.block_node && token == other.token &&
           index == other.index && is_unstarted_ifc == other.is_unstarted_ifc;
  }

  bool AtEnd() const { return !block_node && !token && !is_unstarted_ifc; }
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_CORE_LAYOUT_BLOCK_CHILD_ITERATOR_H_
