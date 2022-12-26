// Copyright 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_CSS_LAYOUT_UPGRADE_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_CSS_LAYOUT_UPGRADE_H_

#include "third_party/blink/renderer/platform/wtf/allocator/allocator.h"

namespace blink {

class Node;
class Document;
class HTMLFrameOwnerElement;

// Various APIs require that style information is updated immediately, e.g.
// getComputedStyle. This is done by calling Document::UpdateStyleAndLayoutTree-
// [ForNode]. However, because of container queries, it is no longer always
// possible to update style without also updating layout. When such a
// dependency exists, the call to update style must be *upgraded* to update
// layout as well.
//
// Whether or not an upgrade is needed depends on the element (or elements) the
// API in question needs to interact with. We typically want to avoid
// doing layout if it isn't necessary to satisfy a given API. The LayoutUpgrade
// classes can determine whether or not an upgrade is needed for a given
// situation.
class LayoutUpgrade {
 public:
  virtual bool ShouldUpgrade() = 0;
};

// Upgrades when *any* element in the document may depend on layout. Suitable
// when the style update isn't focused on a single element.
class DocumentLayoutUpgrade : public LayoutUpgrade {
  STACK_ALLOCATED();

 public:
  explicit DocumentLayoutUpgrade(Document& document) : document_(document) {}

  bool ShouldUpgrade() override;

 private:
  Document& document_;
};

// Upgrades when the document depends on layout information in the parent frame.
class ParentLayoutUpgrade : public LayoutUpgrade {
  STACK_ALLOCATED();

 public:
  explicit ParentLayoutUpgrade(Document& document, HTMLFrameOwnerElement& owner)
      : document_(document), owner_(owner) {}

  bool ShouldUpgrade() override;

 private:
  // That the `document_` is the inner Document, i.e. inside the iframe.
  Document& document_;
  HTMLFrameOwnerElement& owner_;
};

// Upgrades whenever the (inclusive) ancestor chain has a relevant upgrade
// reason. Suitable when the style of a specific node will be accessed.
class NodeLayoutUpgrade : public LayoutUpgrade {
  STACK_ALLOCATED();

 public:
  explicit NodeLayoutUpgrade(const Node& node) : node_(node) {}

  using Reasons = unsigned;

  enum Reason {
    // The current ComputedStyle of this node depends on size container queries.
    kDependsOnSizeContainerQueries = 1 << 0,
    // The node is an interleaving root. This means that we *may* enter
    // interleaved style recalc (via layout) on this node.
    kInterleavingRoot = 1 << 1,
  };

  static Reasons GetReasons(const Node&);

  bool ShouldUpgrade() override;

 private:
  const Node& node_;
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_CORE_CSS_LAYOUT_UPGRADE_H_
