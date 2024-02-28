// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/css/style_traversal_root.h"
#include "third_party/blink/renderer/core/css/style_engine.h"
#include "third_party/blink/renderer/core/dom/document.h"
#include "third_party/blink/renderer/core/dom/node_traversal.h"

namespace blink {

void StyleTraversalRoot::Update(ContainerNode* common_ancestor,
                                Node* dirty_node) {
  DCHECK(dirty_node);
  DCHECK(dirty_node->isConnected());
  AssertRootNodeInvariants();

  if (!common_ancestor) {
    // This is either first dirty node in which case we are using it as a
    // single root, or the document/documentElement which we set as a common
    // root.
    //
    // TODO(futhark): Disallow Document as the root. All traversals start at
    // the RootElement().
    Element* document_element = dirty_node->GetDocument().documentElement();
    if (dirty_node->IsDocumentNode() ||
        (root_node_ && dirty_node == document_element)) {
      root_type_ = RootType::kCommonRoot;
    } else {
      DCHECK(!document_element ||
             (!root_node_ && root_type_ == RootType::kSingleRoot));
    }
    root_node_ = dirty_node;
    AssertRootNodeInvariants();
    return;
  }

  DCHECK(root_node_);
#if DCHECK_IS_ON()
  DCHECK(Parent(*dirty_node));
  DCHECK(!IsDirty(*Parent(*dirty_node)));
#endif  // DCHECK_IS_ON()
  if (common_ancestor == root_node_ || IsDirty(*common_ancestor)) {
    // If our common ancestor candidate is dirty, we are a descendant of the
    // current root node.
    root_type_ = RootType::kCommonRoot;
    return;
  }
  if (root_type_ == RootType::kCommonRoot) {
    // We already have a common root and we don't know if the common ancestor is
    // a descendent or ancestor of the current root. Fall back to make the
    // document the root node.
    root_node_ = &common_ancestor->GetDocument();
    return;
  }
  root_node_ = common_ancestor;
  root_type_ = RootType::kCommonRoot;
}

#if DCHECK_IS_ON()
bool StyleTraversalRoot::IsModifyingFlatTree() const {
  DCHECK(root_node_);
  return root_node_->GetDocument().GetStyleEngine().InDOMRemoval() ||
         root_node_->GetDocument().IsInSlotAssignmentRecalc();
}
#endif

}  // namespace blink
