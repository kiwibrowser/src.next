/*
 * Copyright (C) 1999 Lars Knoll (knoll@kde.org)
 *           (C) 1999 Antti Koivisto (koivisto@kde.org)
 *           (C) 2001 Dirk Mueller (mueller@kde.org)
 * Copyright (C) 2004-2011, 2014 Apple Inc. All rights reserved.
 * Copyright (C) 2008, 2009 Torch Mobile Inc. All rights reserved.
 * (http://www.torchmobile.com/)
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

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_DOM_NODE_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_DOM_NODE_H_

#include "base/dcheck_is_on.h"
#include "base/notreached.h"
#include "third_party/blink/public/mojom/input/focus_type.mojom-blink-forward.h"
#include "third_party/blink/renderer/core/core_export.h"
#include "third_party/blink/renderer/core/dom/events/event_target.h"
#include "third_party/blink/renderer/core/dom/events/simulated_click_options.h"
#include "third_party/blink/renderer/core/dom/mutation_observer_options.h"
#include "third_party/blink/renderer/core/dom/node_rare_data.h"
#include "third_party/blink/renderer/core/dom/tree_scope.h"
#include "third_party/blink/renderer/core/scroll/scroll_customization.h"
#include "third_party/blink/renderer/core/style/computed_style_constants.h"
#include "third_party/blink/renderer/platform/heap/collection_support/heap_hash_map.h"
#include "third_party/blink/renderer/platform/heap/collection_support/heap_hash_set.h"
#include "third_party/blink/renderer/platform/heap/custom_spaces.h"
#include "third_party/blink/renderer/platform/text/text_direction.h"
#include "third_party/blink/renderer/platform/wtf/wtf.h"

// Exposes |DumpStatistics()| for dumping information about nodes. To use, call
// |DumpStatistics()| from the Node constructor or GDB.
// This needs to be here because Element.h also depends on it.
#define DUMP_NODE_STATISTICS 0

namespace gfx {
class Rect;
}

namespace blink {

class ComputedStyle;
class ContainerNode;
class Document;
class Element;
class Event;
class EventDispatchHandlingState;
class ExceptionState;
class FlatTreeNodeData;
class GetRootNodeOptions;
class HTMLQualifiedName;
class HTMLSlotElement;
class KURL;
class LayoutBox;
class LayoutBoxModelObject;
class LayoutObject;
class MathMLQualifiedName;
class MutationObserver;
class MutationObserverRegistration;
class NodeList;
class NodeListsNodeData;
class NodeRareData;
class QualifiedName;
class RegisteredEventListener;
class ScrollTimeline;
class SVGQualifiedName;
class ScrollState;
class ScrollStateCallback;
class ShadowRoot;
template <typename NodeType>
class StaticNodeTypeList;
using StaticNodeList = StaticNodeTypeList<Node>;
class StyleChangeReasonForTracing;
class V8ScrollStateCallback;
class V8UnionNodeOrStringOrTrustedScript;
class V8UnionStringOrTrustedScript;
class WebPluginContainerImpl;
struct PhysicalRect;

const int kDOMNodeTypeShift = 2;
const int kElementNamespaceTypeShift = 4;
const int kNodeStyleChangeShift = 15;
const int kNodeCustomElementShift = 17;

// Values for kChildNeedsStyleRecalcFlag, controlling whether a node gets its
// style recalculated.
enum StyleChangeType : uint32_t {
  // This node does not need style recalculation.
  kNoStyleChange = 0,
  // This node needs style recalculation, but the changes are of
  // a very limited set:
  //
  //  1. They only touch the node's inline style (style="" attribute).
  //  2. They don't add or remove any properties.
  //  3. They only touch independent properties.
  //
  // If all changes are of this type, we can do incremental style
  // recalculation by reusing the previous style and just applying
  // any modified inline style, which is cheaper than a full recalc.
  // See CanApplyInlineStyleIncrementally() and comments on
  // StyleResolver::ApplyBaseStyle() for more details.
  kInlineIndependentStyleChange = 1 << kNodeStyleChangeShift,
  // This node needs (full) style recalculation.
  kLocalStyleChange = 2 << kNodeStyleChangeShift,
  // This node and all of its flat-tree descendeants need style recalculation.
  kSubtreeStyleChange = 3 << kNodeStyleChangeShift,
};

enum class CustomElementState : uint32_t {
  // https://dom.spec.whatwg.org/#concept-element-custom-element-state
  kUncustomized = 0,
  kCustom = 1 << kNodeCustomElementShift,
  kPreCustomized = 2 << kNodeCustomElementShift,
  kUndefined = 3 << kNodeCustomElementShift,
  kFailed = 4 << kNodeCustomElementShift,
};

enum class SlotChangeType {
  kSignalSlotChangeEvent,
  kSuppressSlotChangeEvent,
};

enum class CloneChildrenFlag { kSkip, kClone, kCloneWithShadows };

// Whether or not to force creation of a legacy layout object (i.e. disallow
// LayoutNG).
enum class LegacyLayout {
  // Allow LayoutNG, if nothing else is preventing it (runtime feature disabled,
  // specific object type not yet implemented, Element says no, etc.)
  kAuto,

  // Force legacy layout object creation.
  kForce
};

// LinkHighlight determines the largest enclosing node with hand cursor set.
enum class LinkHighlightCandidate {
  // This node is with hand cursor set.
  kYes,
  // This node is not with hand cursor set.
  kNo,
  // |kYes| if its ancestor is |kYes|.
  kMayBe
};

// A Node is a base class for all objects in the DOM tree.
// The spec governing this interface can be found here:
// https://dom.spec.whatwg.org/#interface-node
class CORE_EXPORT Node : public EventTarget {
  DEFINE_WRAPPERTYPEINFO();
  friend class TreeScope;
  friend class TreeScopeAdopter;

 public:
  enum NodeType {
    kElementNode = 1,
    kAttributeNode = 2,
    kTextNode = 3,
    kCdataSectionNode = 4,
    kProcessingInstructionNode = 7,
    kCommentNode = 8,
    kDocumentNode = 9,
    kDocumentTypeNode = 10,
    kDocumentFragmentNode = 11,
  };

  // Entity, EntityReference, and Notation nodes are impossible to create in
  // Blink.  But for compatibility reasons we want these enum values exist in
  // JS, and this enum makes the bindings generation not complain about
  // kEntityReferenceNode being missing from the implementation while not
  // requiring all switch(NodeType) blocks to include this deprecated constant.
  enum DeprecatedNodeType {
    kEntityReferenceNode = 5,
    kEntityNode = 6,
    kNotationNode = 12,
  };

  enum DocumentPosition {
    kDocumentPositionEquivalent = 0x00,
    kDocumentPositionDisconnected = 0x01,
    kDocumentPositionPreceding = 0x02,
    kDocumentPositionFollowing = 0x04,
    kDocumentPositionContains = 0x08,
    kDocumentPositionContainedBy = 0x10,
    kDocumentPositionImplementationSpecific = 0x20,
  };

#if DUMP_NODE_STATISTICS
  static void DumpStatistics();
#endif

  ~Node() override;

  // DOM methods & attributes for Node

  bool HasTagName(const HTMLQualifiedName&) const;
  bool HasTagName(const MathMLQualifiedName&) const;
  bool HasTagName(const SVGQualifiedName&) const;
  virtual String nodeName() const = 0;
  virtual String nodeValue() const;
  virtual void setNodeValue(const String&,
                            ExceptionState& = ASSERT_NO_EXCEPTION);
  virtual NodeType getNodeType() const = 0;
  ContainerNode* parentNode() const {
    return IsShadowRoot() ? nullptr : ParentOrShadowHostNode();
  }

  Element* parentElement() const;
  ContainerNode* ParentElementOrShadowRoot() const;
  ContainerNode* ParentElementOrDocumentFragment() const;
  Node* previousSibling() const { return previous_; }
  Node* nextSibling() const { return next_; }
  NodeList* childNodes();
  Node* firstChild() const;
  Node* lastChild() const;
  Node* getRootNode(const GetRootNodeOptions*) const;

  // Scroll Customization API. See crbug.com/410974 for details.
  void setDistributeScroll(V8ScrollStateCallback*,
                           const String& native_scroll_behavior);
  void setApplyScroll(V8ScrollStateCallback*,
                      const String& native_scroll_behavior);
  void SetApplyScroll(ScrollStateCallback*);
  void RemoveApplyScroll();
  ScrollStateCallback* GetApplyScroll();
  void NativeDistributeScroll(ScrollState&);
  void NativeApplyScroll(ScrollState&);
  void CallDistributeScroll(ScrollState&);
  void CallApplyScroll(ScrollState&);
  void WillBeginCustomizedScrollPhase(scroll_customization::ScrollDirection);
  void DidEndCustomizedScrollPhase();

  Node& TreeRoot() const;
  Node& ShadowIncludingRoot() const;
  // closed-shadow-hidden is defined at
  // https://dom.spec.whatwg.org/#concept-closed-shadow-hidden
  bool IsClosedShadowHiddenFrom(const Node&) const;

  void Prepend(
      const HeapVector<Member<V8UnionNodeOrStringOrTrustedScript>>& nodes,
      ExceptionState& exception_state);
  void Append(
      const HeapVector<Member<V8UnionNodeOrStringOrTrustedScript>>& nodes,
      ExceptionState& exception_state);
  void Before(
      const HeapVector<Member<V8UnionNodeOrStringOrTrustedScript>>& nodes,
      ExceptionState& exception_state);
  void After(
      const HeapVector<Member<V8UnionNodeOrStringOrTrustedScript>>& nodes,
      ExceptionState& exception_state);
  void ReplaceWith(
      const HeapVector<Member<V8UnionNodeOrStringOrTrustedScript>>& nodes,
      ExceptionState& exception_state);
  void ReplaceChildren(
      const HeapVector<Member<V8UnionNodeOrStringOrTrustedScript>>& nodes,
      ExceptionState& exception_state);
  void remove(ExceptionState&);
  void remove();

  Node* PseudoAwareNextSibling() const;
  Node* PseudoAwarePreviousSibling() const;
  Node* PseudoAwareFirstChild() const;
  Node* PseudoAwareLastChild() const;

  const KURL& baseURI() const;

  Node* insertBefore(Node* new_child, Node* ref_child, ExceptionState&);
  Node* insertBefore(Node* new_child, Node* ref_child);
  Node* replaceChild(Node* new_child, Node* old_child, ExceptionState&);
  Node* replaceChild(Node* new_child, Node* old_child);
  Node* removeChild(Node* child, ExceptionState&);
  Node* removeChild(Node* child);
  Node* appendChild(Node* new_child, ExceptionState&);
  Node* appendChild(Node* new_child);

  bool hasChildren() const { return firstChild(); }
  Node* cloneNode(bool deep, ExceptionState&) const;
  // https://dom.spec.whatwg.org/#concept-node-clone
  virtual Node* Clone(Document&, CloneChildrenFlag) const = 0;
  // This is not web-exposed. We should rename it or remove it.
  Node* cloneNode(bool deep) const;
  void normalize();

  bool isEqualNode(Node*) const;
  bool isSameNode(const Node* other) const { return this == other; }
  bool isDefaultNamespace(const AtomicString& namespace_uri) const;
  const AtomicString& lookupPrefix(const AtomicString& namespace_uri) const;
  const AtomicString& lookupNamespaceURI(const String& prefix) const;

  String textContent(bool convert_brs_to_newlines = false) const;
  virtual void setTextContent(const String&);
  V8UnionStringOrTrustedScript* textContentForBinding() const;
  virtual void setTextContentForBinding(
      const V8UnionStringOrTrustedScript* value,
      ExceptionState& exception_state);

  bool SupportsAltText();

  void SetComputedStyle(scoped_refptr<const ComputedStyle> computed_style);

  // Other methods (not part of DOM)
  ALWAYS_INLINE bool IsTextNode() const {
    return GetDOMNodeType() == DOMNodeType::kText;
  }
  ALWAYS_INLINE bool IsContainerNode() const {
    return GetFlag(kIsContainerFlag);
  }
  ALWAYS_INLINE bool IsElementNode() const {
    return GetDOMNodeType() == DOMNodeType::kElement;
  }
  ALWAYS_INLINE bool IsDocumentFragment() const {
    return GetDOMNodeType() == DOMNodeType::kDocumentFragment;
  }

  ALWAYS_INLINE bool IsHTMLElement() const {
    return GetElementNamespaceType() == ElementNamespaceType::kHTML;
  }
  ALWAYS_INLINE bool IsMathMLElement() const {
    return GetElementNamespaceType() == ElementNamespaceType::kMathML;
  }
  ALWAYS_INLINE bool IsSVGElement() const {
    return GetElementNamespaceType() == ElementNamespaceType::kSVG;
  }

  DISABLE_CFI_PERF bool IsPseudoElement() const {
    return GetPseudoId() != kPseudoIdNone;
  }
  DISABLE_CFI_PERF bool IsBeforePseudoElement() const {
    return GetPseudoId() == kPseudoIdBefore;
  }
  DISABLE_CFI_PERF bool IsAfterPseudoElement() const {
    return GetPseudoId() == kPseudoIdAfter;
  }
  DISABLE_CFI_PERF bool IsMarkerPseudoElement() const {
    return GetPseudoId() == kPseudoIdMarker;
  }
  DISABLE_CFI_PERF bool IsFirstLetterPseudoElement() const {
    return GetPseudoId() == kPseudoIdFirstLetter;
  }
  DISABLE_CFI_PERF bool IsBackdropPseudoElement() const {
    return GetPseudoId() == kPseudoIdBackdrop;
  }
  virtual PseudoId GetPseudoId() const { return kPseudoIdNone; }

  CustomElementState GetCustomElementState() const {
    return static_cast<CustomElementState>(node_flags_ &
                                           kCustomElementStateMask);
  }
  bool IsCustomElement() const {
    return GetCustomElementState() != CustomElementState::kUncustomized;
  }
  void SetCustomElementState(CustomElementState);

  virtual bool IsMediaControlElement() const { return false; }
  virtual bool IsMediaControls() const { return false; }
  virtual bool IsMediaElement() const { return false; }
  virtual bool IsTextTrackContainer() const { return false; }
  virtual bool IsVTTElement() const { return false; }
  virtual bool IsAttributeNode() const { return false; }
  virtual bool IsCharacterDataNode() const { return false; }
  virtual bool IsFrameOwnerElement() const { return false; }
  virtual bool IsMediaRemotingInterstitial() const { return false; }
  virtual bool IsPictureInPictureInterstitial() const { return false; }

  // Traverses the ancestors of this node and returns true if any of them are
  // either a MediaControlElement or MediaControls.
  bool HasMediaControlAncestor() const;

  // StyledElements allow inline style (style="border: 1px"), presentational
  // attributes (ex. color), class names (ex. class="foo bar") and other
  // non-basic styling features. They also control if this element can
  // participate in style sharing.
  //
  // TODO(crbug.com/1305488): The only things that ever go through StyleResolver
  // that aren't StyledElements are PseudoElements and VTTElements. It's
  // possible we can just remove this function entirely, and replace it at the
  // callsites with a DCHECK, since those elements will never have class
  // names, inline style, or other things that this apparently guards
  // against.
  bool IsStyledElement() const {
    return IsHTMLElement() || IsSVGElement() || IsMathMLElement();
  }

  bool IsDocumentNode() const;
  bool IsTreeScope() const;
  bool IsShadowRoot() const { return IsDocumentFragment() && IsTreeScope(); }

  bool IsActiveSlot() const;
  bool IsSlotable() const { return IsTextNode() || IsElementNode(); }
  AtomicString SlotName() const;

  bool HasCustomStyleCallbacks() const {
    return GetFlag(kHasCustomStyleCallbacksFlag);
  }

  // If this node is in a shadow tree, returns its shadow host. Otherwise,
  // returns nullptr.
  Element* OwnerShadowHost() const;
  // crbug.com/569532: containingShadowRoot() can return nullptr even if
  // isInShadowTree() returns true.
  // This can happen when handling queued events (e.g. during execCommand())
  ShadowRoot* ContainingShadowRoot() const;
  ShadowRoot* GetShadowRoot() const;
  bool IsInUserAgentShadowRoot() const;

  // Returns nullptr, a child of ShadowRoot, or a legacy shadow root.
  Node* NonBoundaryShadowTreeRootNode();

  // Node's parent, shadow tree host.
  ContainerNode* ParentOrShadowHostNode() const;
  Element* ParentOrShadowHostElement() const;
  void SetParentOrShadowHostNode(ContainerNode*);

  // Knows about all kinds of hosts.
  ContainerNode* ParentOrShadowHostOrTemplateHostNode() const;

  // Returns the parent node, but nullptr if the parent node is a ShadowRoot.
  ContainerNode* NonShadowBoundaryParentNode() const;

  // Returns the enclosing event parent Element (or self) that, when clicked,
  // would trigger a navigation.
  Element* EnclosingLinkEventParentOrSelf() const;

  // These low-level calls give the caller responsibility for maintaining the
  // integrity of the tree.
  void SetPreviousSibling(Node* previous) { previous_ = previous; }
  void SetNextSibling(Node* next) { next_ = next; }

  virtual bool CanContainRangeEndPoint() const { return false; }

  // For <link> and <style> elements.
  virtual bool SheetLoaded() { return true; }
  enum LoadedSheetErrorStatus {
    kNoErrorLoadingSubresource,
    kErrorOccurredLoadingSubresource
  };
  virtual void NotifyLoadedSheetAndAllCriticalSubresources(
      LoadedSheetErrorStatus) {}
  virtual void SetToPendingState() { NOTREACHED(); }

  bool HasName() const {
    DCHECK(!IsTextNode());
    return GetFlag(kHasNameOrIsEditingTextFlag);
  }

  bool IsUserActionElement() const { return GetFlag(kIsUserActionElementFlag); }
  void SetUserActionElement(bool flag) {
    SetFlag(flag, kIsUserActionElementFlag);
  }

  bool IsActive() const {
    return IsUserActionElement() && IsUserActionElementActive();
  }
  bool InActiveChain() const {
    return IsUserActionElement() && IsUserActionElementInActiveChain();
  }
  bool IsDragged() const {
    return IsUserActionElement() && IsUserActionElementDragged();
  }
  bool IsHovered() const {
    return IsUserActionElement() && IsUserActionElementHovered();
  }
  // Note: As a shadow host whose root with delegatesFocus=false may become
  // focused state when an inner element gets focused, in that case more than
  // one elements in a document can return true for |isFocused()|.  Use
  // Element::isFocusedElementInDocument() or Document::focusedElement() to
  // check which element is exactly focused.
  bool IsFocused() const {
    return IsUserActionElement() && IsUserActionElementFocused();
  }
  bool HasFocusWithin() const {
    return IsUserActionElement() && IsUserActionElementHasFocusWithin();
  }

  // True if the style recalc process should recalculate style for this node.
  bool NeedsStyleRecalc() const {
    return GetStyleChangeType() != kNoStyleChange;
  }
  StyleChangeType GetStyleChangeType() const {
    return static_cast<StyleChangeType>(node_flags_ & kStyleChangeMask);
  }
  // True if the style recalculation process should traverse this node's
  // children when looking for nodes that need recalculation.
  bool ChildNeedsStyleRecalc() const {
    return GetFlag(kChildNeedsStyleRecalcFlag);
  }
  bool IsLink() const { return GetFlag(kIsLinkFlag); }
  bool IsEditingText() const {
    DCHECK(IsTextNode());
    return GetFlag(kHasNameOrIsEditingTextFlag);
  }

  void SetHasName(bool f) {
    DCHECK(!IsTextNode());
    SetFlag(f, kHasNameOrIsEditingTextFlag);
  }
  void SetChildNeedsStyleRecalc() { SetFlag(kChildNeedsStyleRecalcFlag); }
  void ClearChildNeedsStyleRecalc() { ClearFlag(kChildNeedsStyleRecalcFlag); }

  // Sets the flag for the current node and also calls
  // MarkAncestorsWithChildNeedsStyleRecalc
  void SetNeedsStyleRecalc(StyleChangeType, const StyleChangeReasonForTracing&);
  void ClearNeedsStyleRecalc();

  // Propagates a dirty bit breadcrumb for this element up the ancestor chain.
  void MarkAncestorsWithChildNeedsStyleRecalc();

  // Traverses subtree (include pseudo elements and shadow trees) and
  // invalidates nodes whose styles depend on font metrics (e.g., 'ex' unit).
  void MarkSubtreeNeedsStyleRecalcForFontUpdates();

  // Nodes which are not connected are style clean. Mark them for style recalc
  // when inserting them into a document. This method was added as a light-
  // weight alternative to SetNeedsStyleRecalc because using that method caused
  // a micro-benchmark regression (https://crbug.com/926343).
  void SetStyleChangeOnInsertion() {
    DCHECK(isConnected());
    if (ShouldSkipMarkingStyleDirty())
      return;
    if (!NeedsStyleRecalc())
      SetStyleChange(kLocalStyleChange);
    MarkAncestorsWithChildNeedsStyleRecalc();
  }

  // Mark non-slotted shadow host child dirty for style recalc to enforce new
  // ComputedStyles for elements outside the flat tree for getComputedStyle().
  // To be called when descendant recalcs are skipped for such subtrees.
  void SetStyleChangeForNonSlotted() {
    DCHECK(IsElementNode());
    DCHECK(isConnected());
    DCHECK(parentElement() && !GetStyleRecalcParent());
    if (!NeedsStyleRecalc())
      SetStyleChange(kLocalStyleChange);
  }

  bool NeedsReattachLayoutTree() const {
    return GetFlag(kNeedsReattachLayoutTree);
  }
  bool ChildNeedsReattachLayoutTree() const {
    return GetFlag(kChildNeedsReattachLayoutTree);
  }

  void SetNeedsReattachLayoutTree();
  void SetChildNeedsReattachLayoutTree() {
    SetFlag(kChildNeedsReattachLayoutTree);
  }

  void ClearNeedsReattachLayoutTree() { ClearFlag(kNeedsReattachLayoutTree); }
  void ClearChildNeedsReattachLayoutTree() {
    ClearFlag(kChildNeedsReattachLayoutTree);
  }

  void MarkAncestorsWithChildNeedsReattachLayoutTree();

  // Mark node for forced layout tree re-attach during next lifecycle update.
  // This is to trigger layout tree re-attachment when we cannot detect that we
  // need to re-attach based on the computed style changes. This can happen when
  // re-slotting shadow host children, for instance.
  void SetForceReattachLayoutTree();
  bool GetForceReattachLayoutTree() const {
    return GetFlag(kForceReattachLayoutTree);
  }

  bool NeedsLayoutSubtreeUpdate() const;
  bool NeedsWhitespaceChildrenUpdate() const;
  bool IsDirtyForStyleRecalc() const {
    return NeedsStyleRecalc() || GetForceReattachLayoutTree() ||
           NeedsLayoutSubtreeUpdate();
  }
  bool IsDirtyForRebuildLayoutTree() const {
    return NeedsReattachLayoutTree() || NeedsLayoutSubtreeUpdate();
  }

  // True if the style invalidation process should traverse this node's children
  // when looking for pending invalidations.
  bool ChildNeedsStyleInvalidation() const {
    return GetFlag(kChildNeedsStyleInvalidationFlag);
  }
  void SetChildNeedsStyleInvalidation() {
    SetFlag(kChildNeedsStyleInvalidationFlag);
  }
  void ClearChildNeedsStyleInvalidation() {
    ClearFlag(kChildNeedsStyleInvalidationFlag);
  }
  void MarkAncestorsWithChildNeedsStyleInvalidation();

  // True if there are pending invalidations against this node.
  bool NeedsStyleInvalidation() const {
    return GetFlag(kNeedsStyleInvalidationFlag);
  }
  void ClearNeedsStyleInvalidation() { ClearFlag(kNeedsStyleInvalidationFlag); }
  // Sets the flag for the current node and also calls
  // MarkAncestorsWithChildNeedsStyleInvalidation
  void SetNeedsStyleInvalidation();

  void SetIsLink(bool f);

  bool HasEventTargetData() const { return GetFlag(kHasEventTargetDataFlag); }
  void SetHasEventTargetData(bool flag) {
    SetFlag(flag, kHasEventTargetDataFlag);
  }

  virtual void SetFocused(bool flag, mojom::blink::FocusType);
  void SetHasFocusWithin(bool flag);
  virtual void SetDragged(bool flag);

  // This is called only when the node is focused.
  virtual bool ShouldHaveFocusAppearance() const;

  // Returns how |this| participates to the nodes with hand cursor set.
  LinkHighlightCandidate IsLinkHighlightCandidate() const;

  virtual PhysicalRect BoundingBox() const;
  gfx::Rect PixelSnappedBoundingBox() const;

  // BoundingBoxForScrollIntoView() is the node's scroll snap area.
  // It is expanded from the BoundingBox() by scroll-margin.
  // https://drafts.csswg.org/css-scroll-snap-1/#scroll-snap-area
  PhysicalRect BoundingBoxForScrollIntoView() const;

  unsigned NodeIndex() const;

  // Returns the DOM ownerDocument attribute. This method never returns null,
  // except in the case of a Document node.
  Document* ownerDocument() const;

  // Returns the document associated with this node. A Document node returns
  // itself.
  Document& GetDocument() const { return GetTreeScope().GetDocument(); }

  TreeScope& GetTreeScope() const {
    DCHECK(tree_scope_);
    return *tree_scope_;
  }

  TreeScope& ContainingTreeScope() const {
    DCHECK(IsInTreeScope());
    return *tree_scope_;
  }

  // Returns the tree scope where this element originated.
  // Use this when resolving element references for (CSS url(...)s and #id).
  // This differs from GetTreeScope for shadow clones inside <svg:use/>.
  TreeScope& OriginatingTreeScope() const;

  bool InActiveDocument() const;

  // Returns true if this node is connected to a document, false otherwise.
  // See https://dom.spec.whatwg.org/#connected for the definition.
  bool isConnected() const { return GetFlag(kIsConnectedFlag); }

  bool IsInDocumentTree() const { return isConnected() && !IsInShadowTree(); }
  bool IsInShadowTree() const { return GetFlag(kIsInShadowTreeFlag); }
  bool IsInTreeScope() const {
    return GetFlag(
        static_cast<NodeFlags>(kIsConnectedFlag | kIsInShadowTreeFlag));
  }

  ShadowRoot* ParentElementShadowRoot() const;
  bool IsChildOfShadowHost() const;
  ShadowRoot* ShadowRootOfParent() const;
  Element* FlatTreeParentForChildDirty() const;
  Element* GetStyleRecalcParent() const {
    return FlatTreeParentForChildDirty();
  }
  Element* GetReattachParent() const { return FlatTreeParentForChildDirty(); }

  bool IsDocumentTypeNode() const { return getNodeType() == kDocumentTypeNode; }
  virtual bool ChildTypeAllowed(NodeType) const { return false; }
  unsigned CountChildren() const;

  bool IsDescendantOf(const Node*) const;
  bool IsDescendantOrShadowDescendantOf(const Node*) const;
  bool contains(const Node*) const;
  // https://dom.spec.whatwg.org/#concept-shadow-including-inclusive-ancestor
  bool IsShadowIncludingInclusiveAncestorOf(const Node&) const;
  // https://dom.spec.whatwg.org/#concept-shadow-including-ancestor
  bool IsShadowIncludingAncestorOf(const Node&) const;
  bool ContainsIncludingHostElements(const Node&) const;
  Node* CommonAncestor(const Node&,
                       ContainerNode* (*parent)(const Node&)) const;

  // Whether or not a selection can be started in this object
  virtual bool CanStartSelection() const;

  // TODO(bebeaudr): This is a temporary solution only. Accessibility and Blink
  // shouldn't differ when it comes to determining if a node is editable, richly
  // editable or not editable at all.
  // See https://crbug.com/1331359 for more info.
  virtual bool IsRichlyEditableForAccessibility() const;

  void NotifyPriorityScrollAnchorStatusChanged();

  // ---------------------------------------------------------------------------
  // Integration with layout tree

  // As layoutObject() includes a branch you should avoid calling it repeatedly
  // in hot code paths.
  // Note that if a Node has a layoutObject, it's parentNode is guaranteed to
  // have one as well.
  LayoutObject* GetLayoutObject() const {
    return HasRareData()
               ? DataAsNodeRareData()->GetNodeRenderingData()->GetLayoutObject()
               : DataAsNodeRenderingData()->GetLayoutObject();
  }
  void SetLayoutObject(LayoutObject*);
  // Use these two methods with caution.
  LayoutBox* GetLayoutBox() const;
  LayoutBoxModelObject* GetLayoutBoxModelObject() const;

  struct AttachContext {
    STACK_ALLOCATED();

   public:
    // Keep track of previously attached in-flow box during attachment so that
    // we don't need to backtrack past display:none/contents and out of flow
    // objects when we need to do whitespace re-attachment.
    LayoutObject* previous_in_flow = nullptr;
    // The parent LayoutObject to use when inserting a new child into the layout
    // tree in LayoutTreeBuilder::CreateLayoutObject.
    LayoutObject* parent = nullptr;
    // LayoutObject to be used as the next pointer when inserting a LayoutObject
    // into the tree.
    LayoutObject* next_sibling = nullptr;
    // Set to true if the AttachLayoutTree is done as part of the
    // RebuildLayoutTree pass.
    bool performing_reattach = false;
    // True if the previous_in_flow member is up-to-date, even if it is nullptr.
    bool use_previous_in_flow = false;
    // True if the next_sibling member is up-to-date, even if it is nullptr.
    bool next_sibling_valid = false;
    // True if we need to force legacy layout objects for the entire subtree.
    bool force_legacy_layout = false;

    AttachContext() {}
  };

  // Attaches this node to the layout tree. This calculates the style to be
  // applied to the node and creates an appropriate LayoutObject which will be
  // inserted into the tree (except when the style has display: none). This
  // makes the node visible in the LocalFrameView.
  virtual void AttachLayoutTree(AttachContext&);

  // Detaches the node from the layout tree, making it invisible in the rendered
  // view. This method will remove the node's layout object from the layout tree
  // and delete it.
  virtual void DetachLayoutTree(bool performing_reattach = false);

  void ReattachLayoutTree(AttachContext&);

  // ---------------------------------------------------------------------------
  // Inline ComputedStyle accessors
  //
  // Note that the following 'inline' functions are not defined in this header,
  // but in node_computed_style.h. Please include that file if you want to use
  // these functions.
  inline ComputedStyle* MutableComputedStyleForEditingDeprecated() const;
  inline const ComputedStyle* GetComputedStyle() const;
  inline const ComputedStyle& ComputedStyleRef() const;
  bool ShouldSkipMarkingStyleDirty() const;

  const ComputedStyle* EnsureComputedStyle(
      PseudoId pseudo_element_specifier = kPseudoIdNone,
      const AtomicString& pseudo_argument = g_null_atom) {
    return VirtualEnsureComputedStyle(pseudo_element_specifier,
                                      pseudo_argument);
  }

  // ---------------------------------------------------------------------------
  // Notification of document structure changes (see container_node.h for more
  // notification methods)
  //
  // At first, Blink notifies the node that it has been inserted into the
  // document. This is called during document parsing, and also when a node is
  // added through the DOM methods insertBefore(), appendChild() or
  // replaceChild(). The call happens _after_ the node has been added to the
  // tree.  This is similar to the DOMNodeInsertedIntoDocument DOM event, but
  // does not require the overhead of event dispatching.
  //
  // Blink notifies this callback regardless if the subtree of the node is a
  // document tree or a floating subtree.  Implementation can determine the type
  // of subtree by seeing insertion_point->isConnected().  For performance
  // reasons, notifications are delivered only to ContainerNode subclasses if
  // the insertion_point is not in a document tree.
  //
  // There is another callback, DidNotifySubtreeInsertionsToDocument(),
  // which is called after all the descendants are notified, if this node was
  // inserted into the document tree. Only a few subclasses actually need
  // this. To utilize this, the node should return
  // kInsertionShouldCallDidNotifySubtreeInsertions from InsertedInto().
  //
  // InsertedInto() implementations must not modify the DOM tree, and must not
  // dispatch synchronous events. On the other hand,
  // DidNotifySubtreeInsertionsToDocument() may modify the DOM tree, and may
  // dispatch synchronous events.
  enum InsertionNotificationRequest {
    kInsertionDone,
    kInsertionShouldCallDidNotifySubtreeInsertions
  };

  virtual InsertionNotificationRequest InsertedInto(
      ContainerNode& insertion_point);
  virtual void DidNotifySubtreeInsertionsToDocument() {}

  // Notifies the node that it is no longer part of the tree.
  //
  // This is a dual of InsertedInto(), and is similar to the
  // DOMNodeRemovedFromDocument DOM event, but does not require the overhead of
  // event dispatching, and is called _after_ the node is removed from the tree.
  //
  // RemovedFrom() implementations must not modify the DOM tree, and must not
  // dispatch synchronous events.
  virtual void RemovedFrom(ContainerNode& insertion_point);

  // FIXME(dominicc): This method is not debug-only--it is used by
  // Tracing--rename it to something indicative.
  String DebugName() const;

  String ToString() const;

#if DCHECK_IS_ON()
  String ToTreeStringForThis() const;
  String ToFlatTreeStringForThis() const;
  void PrintNodePathTo(std::ostream&) const;
  String ToMarkedTreeString(const Node* marked_node1,
                            const char* marked_label1,
                            const Node* marked_node2 = nullptr,
                            const char* marked_label2 = nullptr) const;
  String ToMarkedFlatTreeString(const Node* marked_node1,
                                const char* marked_label1,
                                const Node* marked_node2 = nullptr,
                                const char* marked_label2 = nullptr) const;
  void ShowTreeForThisAcrossFrame() const;
#endif

  NodeListsNodeData* NodeLists();
  void ClearNodeLists();

  FlatTreeNodeData* GetFlatTreeNodeData() const;
  FlatTreeNodeData& EnsureFlatTreeNodeData();
  void ClearFlatTreeNodeData();
  void ClearFlatTreeNodeDataIfHostChanged(const ContainerNode& parent);

  virtual bool WillRespondToMouseMoveEvents() const;
  virtual bool WillRespondToMouseClickEvents();

  enum ShadowTreesTreatment {
    kTreatShadowTreesAsDisconnected,
    kTreatShadowTreesAsComposed
  };

  uint16_t compareDocumentPosition(
      const Node*,
      ShadowTreesTreatment = kTreatShadowTreesAsDisconnected) const;

  const AtomicString& InterfaceName() const override;
  ExecutionContext* GetExecutionContext() const override;

  void RemoveAllEventListeners() override;
  void RemoveAllEventListenersRecursively();

  // Handlers to do/undo actions on the target node before an event is
  // dispatched to it and after the event has been dispatched.  The data pointer
  // is handed back by the preDispatch and passed to postDispatch.
  virtual EventDispatchHandlingState* PreDispatchEventHandler(Event&) {
    return nullptr;
  }
  virtual void PostDispatchEventHandler(Event&, EventDispatchHandlingState*) {}

  void DispatchScopedEvent(Event&);

  virtual void HandleLocalEvents(Event&);

  void DispatchSubtreeModifiedEvent();
  DispatchEventResult DispatchDOMActivateEvent(int detail,
                                               Event& underlying_event);

  void DispatchSimulatedClick(const Event* underlying_event,
                              SimulatedClickCreationScope =
                                  SimulatedClickCreationScope::kFromUserAgent);

  // Perform the default action for an event.
  virtual void DefaultEventHandler(Event&);
  void UpdateHadKeyboardEvent(const Event&);
  // Should return true if this Node has activation behavior.
  // https://dom.spec.whatwg.org/#eventtarget-activation-behavior
  virtual bool HasActivationBehavior() const;

  EventTargetData* GetEventTargetData() override;
  EventTargetData& EnsureEventTargetData() override;

  void GetRegisteredMutationObserversOfType(
      HeapHashMap<Member<MutationObserver>, MutationRecordDeliveryOptions>&,
      MutationType,
      const QualifiedName* attribute_name);
  void RegisterMutationObserver(MutationObserver&,
                                MutationObserverOptions,
                                const HashSet<AtomicString>& attribute_filter);
  void UnregisterMutationObserver(MutationObserverRegistration*);
  void RegisterTransientMutationObserver(MutationObserverRegistration*);
  void UnregisterTransientMutationObserver(MutationObserverRegistration*);
  void NotifyMutationObserversNodeWillDetach();

  unsigned ConnectedSubframeCount() const;
  void IncrementConnectedSubframeCount();
  void DecrementConnectedSubframeCount();

  StaticNodeList* getDestinationInsertionPoints();
  HTMLSlotElement* AssignedSlot() const;
  HTMLSlotElement* assignedSlotForBinding();
  HTMLSlotElement* AssignedSlotWithoutRecalc() const;

  bool IsFinishedParsingChildren() const {
    return GetFlag(kIsFinishedParsingChildrenFlag);
  }

  void CheckSlotChange(SlotChangeType);
  void CheckSlotChangeAfterInserted() {
    CheckSlotChange(SlotChangeType::kSignalSlotChangeEvent);
  }
  void CheckSlotChangeBeforeRemoved() {
    CheckSlotChange(SlotChangeType::kSignalSlotChangeEvent);
  }

  void FlatTreeParentChanged();
  void RemovedFromFlatTree();

  void SetHasDuplicateAttributes() { SetFlag(kHasDuplicateAttributes); }
  bool HasDuplicateAttribute() const {
    return GetFlag(kHasDuplicateAttributes);
  }

  bool IsEffectiveRootScroller() const;

  virtual LayoutBox* AutoscrollBox();
  virtual void StopAutoscroll();

  // If the node is a plugin, then this returns its WebPluginContainer.
  WebPluginContainerImpl* GetWebPluginContainer() const;

  void RegisterScrollTimeline(ScrollTimeline*);
  void UnregisterScrollTimeline(ScrollTimeline*);

  // For the imperative slot distribution API.
  void SetManuallyAssignedSlot(HTMLSlotElement* slot);
  HTMLSlotElement* ManuallyAssignedSlot();

  // For Element.
  void SetHasDisplayLockContext() { SetFlag(kHasDisplayLockContext); }
  bool HasDisplayLockContext() const { return GetFlag(kHasDisplayLockContext); }

  bool SelfOrAncestorHasDirAutoAttribute() const {
    return GetFlag(kSelfOrAncestorHasDirAutoAttribute);
  }
  void SetSelfOrAncestorHasDirAutoAttribute() {
    SetFlag(kSelfOrAncestorHasDirAutoAttribute);
  }
  void ClearSelfOrAncestorHasDirAutoAttribute() {
    ClearFlag(kSelfOrAncestorHasDirAutoAttribute);
  }
  TextDirection CachedDirectionality() const {
    return (node_flags_ & kCachedDirectionalityIsRtl) ? TextDirection::kRtl
                                                      : TextDirection::kLtr;
  }
  void SetCachedDirectionality(TextDirection direction) {
    switch (direction) {
      case TextDirection::kRtl:
        SetFlag(kCachedDirectionalityIsRtl);
        break;
      case TextDirection::kLtr:
        ClearFlag(kCachedDirectionalityIsRtl);
        break;
    }
    ClearFlag(kNeedsInheritDirectionalityFromParent);
  }
  bool NeedsInheritDirectionalityFromParent() const {
    return GetFlag(kNeedsInheritDirectionalityFromParent);
  }
  void SetNeedsInheritDirectionalityFromParent() {
    SetFlag(kNeedsInheritDirectionalityFromParent);
  }
  void ClearNeedsInheritDirectionalityFromParent() {
    ClearFlag(kNeedsInheritDirectionalityFromParent);
  }
  void Trace(Visitor*) const override;

 private:
  enum NodeFlags : uint32_t {
    kHasRareDataFlag = 1,

    // Node type flags. These never change once created.
    kIsContainerFlag = 1 << 1,
    kDOMNodeTypeMask = 0x3 << kDOMNodeTypeShift,
    kElementNamespaceTypeMask = 0x3 << kElementNamespaceTypeShift,

    // Changes based on if the element should be treated like a link,
    // ex. When setting the href attribute on an <a>.
    kIsLinkFlag = 1 << 6,

    // Changes based on :hover, :active and :focus state.
    kIsUserActionElementFlag = 1 << 7,

    // Tree state flags. These change when the element is added/removed
    // from a DOM tree.
    kIsConnectedFlag = 1 << 8,
    kIsInShadowTreeFlag = 1 << 9,

    // Set by the parser when the children are done parsing.
    kIsFinishedParsingChildrenFlag = 1 << 10,

    // Flags related to recalcStyle.
    kHasCustomStyleCallbacksFlag = 1 << 11,
    kChildNeedsStyleInvalidationFlag = 1 << 12,
    kNeedsStyleInvalidationFlag = 1 << 13,
    kChildNeedsStyleRecalcFlag = 1 << 14,
    kStyleChangeMask = 0x3 << kNodeStyleChangeShift,

    kCustomElementStateMask = 0x7 << kNodeCustomElementShift,

    kHasNameOrIsEditingTextFlag = 1 << 20,
    kHasEventTargetDataFlag = 1 << 21,

    kNeedsReattachLayoutTree = 1 << 22,
    kChildNeedsReattachLayoutTree = 1 << 23,

    kHasDuplicateAttributes = 1 << 24,

    kForceReattachLayoutTree = 1 << 25,

    kHasDisplayLockContext = 1 << 26,

    kSelfOrAncestorHasDirAutoAttribute = 1 << 27,
    kCachedDirectionalityIsRtl = 1 << 28,
    kNeedsInheritDirectionalityFromParent = 1 << 29,

    kDefaultNodeFlags = kIsFinishedParsingChildrenFlag,

    // 2 bits remaining.
  };

  ALWAYS_INLINE bool GetFlag(NodeFlags mask) const {
    return node_flags_ & mask;
  }
  void SetFlag(bool f, NodeFlags mask) {
    node_flags_ = (node_flags_ & ~mask) | (-(int32_t)f & mask);
  }
  void SetFlag(NodeFlags mask) { node_flags_ |= mask; }
  void ClearFlag(NodeFlags mask) { node_flags_ &= ~mask; }

  enum class DOMNodeType : uint32_t {
    kElement = 0,
    kText = 1 << kDOMNodeTypeShift,
    kDocumentFragment = 2 << kDOMNodeTypeShift,
    kOther = 3 << kDOMNodeTypeShift,
  };
  ALWAYS_INLINE DOMNodeType GetDOMNodeType() const {
    return static_cast<DOMNodeType>(node_flags_ & kDOMNodeTypeMask);
  }

  enum class ElementNamespaceType : uint32_t {
    kHTML = 0,
    kMathML = 1 << kElementNamespaceTypeShift,
    kSVG = 2 << kElementNamespaceTypeShift,
    kOther = 3 << kElementNamespaceTypeShift,
  };
  ALWAYS_INLINE ElementNamespaceType GetElementNamespaceType() const {
    return static_cast<ElementNamespaceType>(node_flags_ &
                                             kElementNamespaceTypeMask);
  }

 protected:
  enum ConstructionType {
    kCreateOther = kDefaultNodeFlags |
                   static_cast<NodeFlags>(DOMNodeType::kOther) |
                   static_cast<NodeFlags>(ElementNamespaceType::kOther),
    kCreateText = kDefaultNodeFlags |
                  static_cast<NodeFlags>(DOMNodeType::kText) |
                  static_cast<NodeFlags>(ElementNamespaceType::kOther),
    kCreateContainer = kDefaultNodeFlags | kIsContainerFlag |
                       static_cast<NodeFlags>(DOMNodeType::kOther) |
                       static_cast<NodeFlags>(ElementNamespaceType::kOther),
    kCreateElement = kDefaultNodeFlags | kIsContainerFlag |
                     static_cast<NodeFlags>(DOMNodeType::kElement) |
                     static_cast<NodeFlags>(ElementNamespaceType::kOther),
    kCreateDocumentFragment =
        kDefaultNodeFlags | kIsContainerFlag |
        static_cast<NodeFlags>(DOMNodeType::kDocumentFragment) |
        static_cast<NodeFlags>(ElementNamespaceType::kOther),
    kCreateShadowRoot = kCreateDocumentFragment | kIsInShadowTreeFlag,
    kCreateHTMLElement = kDefaultNodeFlags | kIsContainerFlag |
                         static_cast<NodeFlags>(DOMNodeType::kElement) |
                         static_cast<NodeFlags>(ElementNamespaceType::kHTML),
    kCreateMathMLElement =
        kDefaultNodeFlags | kIsContainerFlag |
        static_cast<NodeFlags>(DOMNodeType::kElement) |
        static_cast<NodeFlags>(ElementNamespaceType::kMathML),
    kCreateSVGElement = kDefaultNodeFlags | kIsContainerFlag |
                        static_cast<NodeFlags>(DOMNodeType::kElement) |
                        static_cast<NodeFlags>(ElementNamespaceType::kSVG),
    kCreateDocument = kCreateContainer | kIsConnectedFlag,
    kCreateEditingText = kCreateText | kHasNameOrIsEditingTextFlag,
  };

  Node(TreeScope*, ConstructionType);

  void WillMoveToNewDocument(Document& old_document, Document& new_document);
  virtual void DidMoveToNewDocument(Document& old_document);

  void AddedEventListener(const AtomicString& event_type,
                          RegisteredEventListener&) override;
  void RemovedEventListener(const AtomicString& event_type,
                            const RegisteredEventListener&) override;
  DispatchEventResult DispatchEventInternal(Event&) override;

  bool HasRareData() const { return GetFlag(kHasRareDataFlag); }

  // |RareData| cannot be replaced or removed once assigned.
  NodeRareData* RareData() const {
    SECURITY_DCHECK(HasRareData());
    return DataAsNodeRareData();
  }
  NodeRareData& EnsureRareData() {
    if (HasRareData())
      return *RareData();

    return CreateRareData();
  }

  void SetHasCustomStyleCallbacks() {
    SetFlag(true, kHasCustomStyleCallbacksFlag);
  }

  void SetTreeScope(TreeScope* scope) { tree_scope_ = scope; }
  void SetIsFinishedParsingChildren(bool value) {
    SetFlag(value, kIsFinishedParsingChildrenFlag);
  }

  void InvalidateIfHasEffectiveAppearance() const;

 private:
  // Gets nodeName without caching AtomicStrings. Used by
  // debugName. Compositor may call debugName from the "impl" thread
  // during "commit". The main thread is stopped at that time, but
  // it is not safe to cache AtomicStrings because those are
  // per-thread.
  virtual String DebugNodeName() const;

  Node* ToNode() final;

  bool IsUserActionElementActive() const;
  bool IsUserActionElementInActiveChain() const;
  bool IsUserActionElementDragged() const;
  bool IsUserActionElementHovered() const;
  bool IsUserActionElementFocused() const;
  bool IsUserActionElementHasFocusWithin() const;

  void SetStyleChange(StyleChangeType change_type) {
    node_flags_ = (node_flags_ & ~kStyleChangeMask) | change_type;
  }

  virtual const ComputedStyle* VirtualEnsureComputedStyle(
      PseudoId = kPseudoIdNone,
      const AtomicString& pseudo_argument = g_null_atom);

  // Used exclusively by |EnsureRareData|.
  NodeRareData& CreateRareData();

  const HeapVector<Member<MutationObserverRegistration>>*
  MutationObserverRegistry();
  const HeapHashSet<Member<MutationObserverRegistration>>*
  TransientMutationObserverRegistry();

  NodeRareData* DataAsNodeRareData() const {
    DCHECK(HasRareData());
    return reinterpret_cast<NodeRareData*>(data_.Get());
  }
  NodeRenderingData* DataAsNodeRenderingData() const {
    DCHECK(!HasRareData());
    return reinterpret_cast<NodeRenderingData*>(data_.Get());
  }
  ShadowRoot* GetSlotAssignmentRoot() const;

  void AddCandidateDirectionalityForSlot();

  uint32_t node_flags_;
  Member<Node> parent_or_shadow_host_node_;
  Member<TreeScope> tree_scope_;
  Member<Node> previous_;
  Member<Node> next_;
  // When a node has rare data we move the layoutObject into the rare data.
  Member<NodeData> data_;
};

inline void Node::SetParentOrShadowHostNode(ContainerNode* parent) {
  DCHECK(IsMainThread());
  parent_or_shadow_host_node_ = reinterpret_cast<Node*>(parent);
}

inline ContainerNode* Node::ParentOrShadowHostNode() const {
  DCHECK(IsMainThread());
  return reinterpret_cast<ContainerNode*>(parent_or_shadow_host_node_.Get());
}

// Allow equality comparisons of Nodes by reference or pointer, interchangeably.
DEFINE_COMPARISON_OPERATORS_WITH_REFERENCES(Node)

CORE_EXPORT std::ostream& operator<<(std::ostream&, const Node&);
CORE_EXPORT std::ostream& operator<<(std::ostream&, const Node*);

}  // namespace blink

#if DCHECK_IS_ON()
// Outside the blink namespace for ease of invocation from gdb.
void ShowNode(const blink::Node*);
void ShowTree(const blink::Node*);
void ShowNodePath(const blink::Node*);
#endif

namespace cppgc {
// Assign Node to be allocated on custom NodeSpace.
template <typename T>
struct SpaceTrait<T, std::enable_if_t<std::is_base_of<blink::Node, T>::value>> {
  using Space = blink::NodeSpace;
};
}  // namespace cppgc

namespace blink {
template <typename T>
struct ThreadingTrait<
    T,
    std::enable_if_t<std::is_base_of<blink::Node, T>::value>> {
  static constexpr ThreadAffinity kAffinity = kMainThreadOnly;
};
}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_CORE_DOM_NODE_H_
