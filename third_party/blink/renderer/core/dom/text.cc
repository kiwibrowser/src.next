/*
 * Copyright (C) 1999 Lars Knoll (knoll@kde.org)
 *           (C) 1999 Antti Koivisto (koivisto@kde.org)
 * Copyright (C) 2003, 2004, 2005, 2006, 2007, 2008, 2009 Apple Inc. All rights
 * reserved.
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

#include "third_party/blink/renderer/core/dom/text.h"

#include <utility>

#include "third_party/blink/renderer/core/css/resolver/style_resolver.h"
#include "third_party/blink/renderer/core/dom/events/scoped_event_queue.h"
#include "third_party/blink/renderer/core/dom/first_letter_pseudo_element.h"
#include "third_party/blink/renderer/core/dom/layout_tree_builder.h"
#include "third_party/blink/renderer/core/dom/layout_tree_builder_traversal.h"
#include "third_party/blink/renderer/core/dom/node_computed_style.h"
#include "third_party/blink/renderer/core/dom/node_traversal.h"
#include "third_party/blink/renderer/core/dom/shadow_root.h"
#include "third_party/blink/renderer/core/dom/whitespace_attacher.h"
#include "third_party/blink/renderer/core/layout/layout_object_factory.h"
#include "third_party/blink/renderer/core/layout/layout_text.h"
#include "third_party/blink/renderer/core/layout/layout_text_fragment.h"
#include "third_party/blink/renderer/core/layout/svg/layout_svg_inline_text.h"
#include "third_party/blink/renderer/core/svg/svg_foreign_object_element.h"
#include "third_party/blink/renderer/core/svg_names.h"
#include "third_party/blink/renderer/platform/bindings/dom_data_store.h"
#include "third_party/blink/renderer/platform/bindings/exception_state.h"
#include "third_party/blink/renderer/platform/wtf/text/string_builder.h"

namespace blink {

Text* Text::Create(Document& document, const String& data) {
  return MakeGarbageCollected<Text>(document, data, kCreateText);
}

Text* Text::CreateEditingText(Document& document, const String& data) {
  return MakeGarbageCollected<Text>(document, data, kCreateEditingText);
}

Node* Text::MergeNextSiblingNodesIfPossible() {
  // Remove empty text nodes.
  if (!length()) {
    // Care must be taken to get the next node before removing the current node.
    Node* next_node = NodeTraversal::NextPostOrder(*this);
    remove(IGNORE_EXCEPTION_FOR_TESTING);
    return next_node;
  }

  // Merge text nodes.
  while (Node* next_sibling = nextSibling()) {
    if (next_sibling->getNodeType() != kTextNode)
      break;

    auto* next_text = To<Text>(next_sibling);

    // Remove empty text nodes.
    if (!next_text->length()) {
      next_text->remove(IGNORE_EXCEPTION_FOR_TESTING);
      continue;
    }

    // Both non-empty text nodes. Merge them.
    unsigned offset = length();
    String next_text_data = next_text->data();
    String old_text_data = data();
    SetDataWithoutUpdate(data() + next_text_data);
    UpdateTextLayoutObject(old_text_data.length(), 0);

    GetDocument().DidMergeTextNodes(*this, *next_text, offset);

    // Empty nextText for layout update.
    next_text->SetDataWithoutUpdate(g_empty_string);
    next_text->UpdateTextLayoutObject(0, next_text_data.length());

    // Restore nextText for mutation event.
    next_text->SetDataWithoutUpdate(next_text_data);
    next_text->UpdateTextLayoutObject(0, 0);

    GetDocument().IncDOMTreeVersion();
    DidModifyData(old_text_data, CharacterData::kUpdateFromNonParser);
    next_text->remove(IGNORE_EXCEPTION_FOR_TESTING);
  }

  return NodeTraversal::NextPostOrder(*this);
}

Text* Text::splitText(unsigned offset, ExceptionState& exception_state) {
  // IndexSizeError: Raised if the specified offset is negative or greater than
  // the number of 16-bit units in data.
  if (offset > length()) {
    exception_state.ThrowDOMException(
        DOMExceptionCode::kIndexSizeError,
        "The offset " + String::Number(offset) +
            " is larger than the Text node's length.");
    return nullptr;
  }

  EventQueueScope scope;
  String old_str = data();
  Text* new_text = CloneWithData(GetDocument(), old_str.Substring(offset));
  SetDataWithoutUpdate(old_str.Substring(0, offset));

  DidModifyData(old_str, CharacterData::kUpdateFromNonParser);

  if (parentNode())
    parentNode()->InsertBefore(new_text, nextSibling(), exception_state);
  if (exception_state.HadException())
    return nullptr;

  if (GetLayoutObject()) {
    GetLayoutObject()->SetTextWithOffset(DataImpl(), 0, old_str.length());
    if (ContainsOnlyWhitespaceOrEmpty()) {
      // To avoid |LayoutText| has empty text, we rebuild layout tree.
      SetForceReattachLayoutTree();
    }
  }

  if (parentNode())
    GetDocument().DidSplitTextNode(*this);
  else
    GetDocument().DidRemoveText(*this, offset, old_str.length() - offset);

  // [NewObject] must always create a new wrapper.  Check that a wrapper
  // does not exist yet.
  DCHECK(
      DOMDataStore::GetWrapper(new_text, v8::Isolate::GetCurrent()).IsEmpty());

  return new_text;
}

static const Text* EarliestLogicallyAdjacentTextNode(const Text* t) {
  for (const Node* n = t->previousSibling(); n; n = n->previousSibling()) {
    if (auto* text_node = DynamicTo<Text>(n)) {
      t = text_node;
      continue;
    }

    break;
  }
  return t;
}

static const Text* LatestLogicallyAdjacentTextNode(const Text* t) {
  for (const Node* n = t->nextSibling(); n; n = n->nextSibling()) {
    if (auto* text_node = DynamicTo<Text>(n)) {
      t = text_node;
      continue;
    }

    break;
  }
  return t;
}

String Text::wholeText() const {
  const Text* start_text = EarliestLogicallyAdjacentTextNode(this);
  const Text* end_text = LatestLogicallyAdjacentTextNode(this);

  Node* one_past_end_text = end_text->nextSibling();
  unsigned result_length = 0;
  for (const Node* n = start_text; n != one_past_end_text;
       n = n->nextSibling()) {
    auto* text_node = DynamicTo<Text>(n);
    if (!text_node)
      continue;
    const String& data = text_node->data();
    CHECK_GE(std::numeric_limits<unsigned>::max() - data.length(),
             result_length);
    result_length += data.length();
  }
  StringBuilder result;
  result.ReserveCapacity(result_length);
  for (const Node* n = start_text; n != one_past_end_text;
       n = n->nextSibling()) {
    auto* text_node = DynamicTo<Text>(n);
    if (!text_node)
      continue;
    result.Append(text_node->data());
  }
  DCHECK_EQ(result.length(), result_length);

  return result.ReleaseString();
}

Text* Text::ReplaceWholeText(const String& new_text) {
  // Remove all adjacent text nodes, and replace the contents of this one.

  // Protect startText and endText against mutation event handlers removing the
  // last ref
  Text* start_text = const_cast<Text*>(EarliestLogicallyAdjacentTextNode(this));
  Text* end_text = const_cast<Text*>(LatestLogicallyAdjacentTextNode(this));

  ContainerNode* parent = parentNode();  // Protect against mutation handlers
                                         // moving this node during traversal
  for (Node* n = start_text;
       n && n != this && n->IsTextNode() && n->parentNode() == parent;) {
    Node* node_to_remove = n;
    n = node_to_remove->nextSibling();
    parent->RemoveChild(node_to_remove, IGNORE_EXCEPTION_FOR_TESTING);
  }

  if (this != end_text) {
    Node* one_past_end_text = end_text->nextSibling();
    for (Node* n = nextSibling(); n && n != one_past_end_text &&
                                  n->IsTextNode() &&
                                  n->parentNode() == parent;) {
      Node* node_to_remove = n;
      n = node_to_remove->nextSibling();
      parent->RemoveChild(node_to_remove, IGNORE_EXCEPTION_FOR_TESTING);
    }
  }

  if (new_text.IsEmpty()) {
    if (parent && parentNode() == parent)
      parent->RemoveChild(this, IGNORE_EXCEPTION_FOR_TESTING);
    return nullptr;
  }

  setData(new_text);
  return this;
}

String Text::nodeName() const {
  return "#text";
}

Node::NodeType Text::getNodeType() const {
  return kTextNode;
}

Node* Text::Clone(Document& factory, CloneChildrenFlag) const {
  return CloneWithData(factory, data());
}

static inline bool EndsWithWhitespace(const String& text) {
  return text.length() && IsASCIISpace(text[text.length() - 1]);
}

static inline bool CanHaveWhitespaceChildren(
    const ComputedStyle& style,
    const Text::AttachContext& context) {
  const LayoutObject& parent = *context.parent;
  // <button> and <fieldset> should allow whitespace even though
  // LayoutFlexibleBox doesn't.
  if (parent.IsButtonIncludingNG() || parent.IsFieldset())
    return true;

  if (parent.IsTable() || parent.IsTableRow() || parent.IsTableSection() ||
      parent.IsLayoutTableCol() || parent.IsFrameSetIncludingNG() ||
      parent.IsFlexibleBoxIncludingNG() || parent.IsLayoutGridIncludingNG() ||
      parent.IsSVGRoot() || parent.IsSVGContainer() || parent.IsSVGImage() ||
      parent.IsSVGShape()) {
    if (!context.use_previous_in_flow || !context.previous_in_flow ||
        !context.previous_in_flow->IsText())
      return false;

    return style.PreserveNewline() ||
           !EndsWithWhitespace(
               To<LayoutText>(context.previous_in_flow)->GetText());
  }
  return true;
}

bool Text::TextLayoutObjectIsNeeded(const AttachContext& context,
                                    const ComputedStyle& style) const {
  const LayoutObject& parent = *context.parent;
  if (!parent.CanHaveChildren())
    return false;

  if (IsEditingText())
    return true;

  if (!length())
    return false;

  if (style.Display() == EDisplay::kNone)
    return false;

  if (!ContainsOnlyWhitespaceOrEmpty())
    return true;

  if (!CanHaveWhitespaceChildren(style, context))
    return false;

  // pre-wrap in SVG never makes layoutObject.
  if (style.WhiteSpace() == EWhiteSpace::kPreWrap && parent.IsSVG())
    return false;

  // pre/pre-wrap/pre-line always make layoutObjects.
  if (style.PreserveNewline())
    return true;

  if (!context.use_previous_in_flow)
    return false;

  if (!context.previous_in_flow)
    return parent.IsLayoutInline();

  if (context.previous_in_flow->IsText()) {
    return !EndsWithWhitespace(
        To<LayoutText>(context.previous_in_flow)->GetText());
  }

  return context.previous_in_flow->IsInline() &&
         !context.previous_in_flow->IsBR();
}

static bool IsSVGText(Text* text) {
  Node* parent_or_shadow_host_node = text->ParentOrShadowHostNode();
  DCHECK(parent_or_shadow_host_node);
  return parent_or_shadow_host_node->IsSVGElement() &&
         !IsA<SVGForeignObjectElement>(*parent_or_shadow_host_node);
}

LayoutText* Text::CreateTextLayoutObject(const ComputedStyle& style,
                                         LegacyLayout legacy) {
  if (IsSVGText(this))
    return MakeGarbageCollected<LayoutSVGInlineText>(this, DataImpl());

  if (style.HasTextCombine())
    return LayoutObjectFactory::CreateTextCombine(this, DataImpl(), legacy);

  return LayoutObjectFactory::CreateText(this, DataImpl(), legacy);
}

void Text::AttachLayoutTree(AttachContext& context) {
  if (context.parent) {
    ContainerNode* style_parent = LayoutTreeBuilderTraversal::Parent(*this);
    if (style_parent) {
      // To handle <body> to <html> writing-mode propagation, we should use
      // style in layout object instead of |Node::GetComputedStyle()|.
      // See http://crbug.com/988585
      const ComputedStyle* const style =
          IsA<HTMLHtmlElement>(style_parent) && style_parent->GetLayoutObject()
              ? style_parent->GetLayoutObject()->Style()
              : style_parent->GetComputedStyle();
      DCHECK(style);
      if (TextLayoutObjectIsNeeded(context, *style)) {
        LayoutTreeBuilderForText(*this, context, style).CreateLayoutObject();
        context.previous_in_flow = GetLayoutObject();
      }
    }
  }
  CharacterData::AttachLayoutTree(context);
}

void Text::ReattachLayoutTreeIfNeeded(AttachContext& context) {
  bool layout_object_is_needed = false;
  ContainerNode* style_parent = LayoutTreeBuilderTraversal::Parent(*this);
  if (style_parent && context.parent) {
    DCHECK(style_parent->GetComputedStyle());
    layout_object_is_needed =
        TextLayoutObjectIsNeeded(context, *style_parent->GetComputedStyle());
  }

  if (layout_object_is_needed == !!GetLayoutObject())
    return;

  AttachContext reattach_context(context);
  reattach_context.performing_reattach = true;

  if (layout_object_is_needed) {
    DCHECK(!GetLayoutObject());
    LayoutTreeBuilderForText(*this, context, style_parent->GetComputedStyle())
        .CreateLayoutObject();
  } else {
    DetachLayoutTree(true /* performing_reattach*/);
  }
  CharacterData::AttachLayoutTree(reattach_context);
}

namespace {

bool NeedsWhitespaceLayoutObject(const ComputedStyle& style) {
  return style.PreserveNewline();
}

}  // namespace

void Text::RecalcTextStyle(const StyleRecalcChange change) {
  scoped_refptr<const ComputedStyle> new_style =
      GetDocument().GetStyleResolver().StyleForText(this);
  if (LayoutText* layout_text = GetLayoutObject()) {
    const ComputedStyle* layout_parent_style =
        GetLayoutObject()->Parent()->Style();
    if (!new_style || GetForceReattachLayoutTree() ||
        (new_style != layout_parent_style &&
         !new_style->InheritedEqual(*layout_parent_style))) {
      // The computed style or the need for an anonymous inline wrapper for a
      // display:contents text child changed.
      SetNeedsReattachLayoutTree();
    } else {
      layout_text->SetStyle(std::move(new_style));
      if (NeedsStyleRecalc())
        layout_text->SetTextIfNeeded(DataImpl());
    }
  } else if (new_style && (NeedsStyleRecalc() || change.ReattachLayoutTree() ||
                           GetForceReattachLayoutTree() ||
                           NeedsWhitespaceLayoutObject(*new_style))) {
    SetNeedsReattachLayoutTree();
  }
  ClearNeedsStyleRecalc();
}

void Text::RebuildTextLayoutTree(WhitespaceAttacher& whitespace_attacher) {
  DCHECK(!ChildNeedsStyleRecalc());
  DCHECK(NeedsReattachLayoutTree());
  DCHECK(parentNode());

  AttachContext context;
  context.parent = LayoutTreeBuilderTraversal::ParentLayoutObject(*this);
  ReattachLayoutTree(context);
  whitespace_attacher.DidReattachText(this);
  ClearNeedsReattachLayoutTree();
}

// Passing both |text_node| and its layout object because repeated calls to
// |Node::GetLayoutObject()| are discouraged.
static bool ShouldUpdateLayoutByReattaching(const Text& text_node,
                                            LayoutText* text_layout_object) {
  DCHECK_EQ(text_node.GetLayoutObject(), text_layout_object);
  if (!text_layout_object)
    return true;
  Node::AttachContext context;
  context.parent = text_layout_object->Parent();
  if (!text_node.TextLayoutObjectIsNeeded(context,
                                          *text_layout_object->Style())) {
    return true;
  }
  if (text_layout_object->IsTextFragment()) {
    // Changes of |text_node| may change first letter part, so we should
    // reattach. Note: When |text_node| is empty or holds collapsed whitespaces
    // |text_fragment_layout_object| represents first-letter part but it isn't
    // inside first-letter-pseudo element. See http://crbug.com/978947
    const auto& text_fragment_layout_object =
        *To<LayoutTextFragment>(text_layout_object);
    return text_fragment_layout_object.GetFirstLetterPseudoElement() ||
           !text_fragment_layout_object.IsRemainingTextLayoutObject();
  }
  // If we force a re-attach for password inputs and other elements hiding text
  // input via -webkit-text-security, the last character input will be hidden
  // immediately, even if the passwordEchoEnabled setting is enabled.
  // ::first-letter do not seem to apply to text inputs, so for those skipping
  // the re-attachment should be safe.
  // We can possibly still cause DCHECKs for mismatch of first letter text in
  // editing with the combination of -webkit-text-security in author styles on
  // other elements in combination with ::first-letter.
  // See crbug.com/1240988
  if (text_layout_object->IsSecure())
    return false;
  if (!FirstLetterPseudoElement::FirstLetterLength(
          text_layout_object->GetText()) &&
      FirstLetterPseudoElement::FirstLetterLength(text_node.data())) {
    // We did not previously apply ::first-letter styles to this |text_node|,
    // and if there was no first formatted letter, but now is, we may need to
    // reattach.
    return true;
  }
  return false;
}

void Text::UpdateTextLayoutObject(unsigned offset_of_replaced_data,
                                  unsigned length_of_replaced_data) {
  if (!InActiveDocument())
    return;
  LayoutText* text_layout_object = GetLayoutObject();
  if (ShouldUpdateLayoutByReattaching(*this, text_layout_object)) {
    SetForceReattachLayoutTree();
    return;
  }

  text_layout_object->SetTextWithOffset(DataImpl(), offset_of_replaced_data,
                                        length_of_replaced_data);
}

Text* Text::CloneWithData(Document& factory, const String& data) const {
  return Create(factory, data);
}

void Text::Trace(Visitor* visitor) const {
  CharacterData::Trace(visitor);
}

}  // namespace blink
