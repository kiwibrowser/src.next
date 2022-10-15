/*
 * Copyright (C) 1999 Lars Knoll (knoll@kde.org)
 *           (C) 1999 Antti Koivisto (koivisto@kde.org)
 *           (C) 2001 Peter Kelly (pmk@post.com)
 *           (C) 2001 Dirk Mueller (mueller@kde.org)
 * Copyright (C) 2003-2011, 2013, 2014 Apple Inc. All rights reserved.
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

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_DOM_ELEMENT_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_DOM_ELEMENT_H_

#include "base/check_op.h"
#include "base/dcheck_is_on.h"
#include "base/gtest_prod_util.h"
#include "base/types/pass_key.h"
#include "third_party/blink/public/common/input/pointer_id.h"
#include "third_party/blink/public/common/metrics/document_update_reason.h"
#include "third_party/blink/public/mojom/scroll/scroll_into_view_params.mojom-blink-forward.h"
#include "third_party/blink/renderer/bindings/core/v8/v8_typedefs.h"
#include "third_party/blink/renderer/core/animation/animatable.h"
#include "third_party/blink/renderer/core/core_export.h"
#include "third_party/blink/renderer/core/css/css_primitive_value.h"
#include "third_party/blink/renderer/core/css/css_selector.h"
#include "third_party/blink/renderer/core/css/style_recalc_change.h"
#include "third_party/blink/renderer/core/dom/container_node.h"
#include "third_party/blink/renderer/core/dom/dom_high_res_time_stamp.h"
#include "third_party/blink/renderer/core/dom/element_data.h"
#include "third_party/blink/renderer/core/dom/events/simulated_click_options.h"
#include "third_party/blink/renderer/core/dom/focusgroup_flags.h"
#include "third_party/blink/renderer/core/dom/has_invalidation_flags.h"
#include "third_party/blink/renderer/core/dom/names_map.h"
#include "third_party/blink/renderer/core/dom/whitespace_attacher.h"
#include "third_party/blink/renderer/core/html_names.h"
#include "third_party/blink/renderer/core/trustedtypes/trusted_types_util.h"
#include "third_party/blink/renderer/platform/heap/collection_support/heap_hash_map.h"
#include "third_party/blink/renderer/platform/heap/garbage_collected.h"
#include "third_party/blink/renderer/platform/region_capture_crop_id.h"
#include "third_party/blink/renderer/platform/text/text_direction.h"
#include "third_party/blink/renderer/platform/weborigin/kurl.h"
#include "third_party/blink/renderer/platform/wtf/text/atomic_string.h"
#include "third_party/blink/renderer/platform/wtf/text/atomic_string_table.h"
#include "third_party/blink/renderer/platform/wtf/wtf_size_t.h"
#include "ui/gfx/geometry/rect_f.h"

namespace gfx {
class QuadF;
class Vector2dF;
}  // namespace gfx

namespace blink {

class AccessibleNode;
class Attr;
class Attribute;
class ContainerQueryData;
class ContainerQueryEvaluator;
class CSSPropertyName;
class CSSPropertyValueSet;
class CSSStyleDeclaration;
class CSSToggleMap;
class CustomElementDefinition;
class DOMRect;
class DOMRectList;
class DOMStringMap;
class DOMTokenList;
class DisplayLockContext;
class Document;
class EditContext;
class ElementAnimations;
class ElementInternals;
class ElementIntersectionObserverData;
class ElementRareData;
class ExceptionState;
class FocusOptions;
class GetInnerHTMLOptions;
class HTMLSelectMenuElement;
class HTMLTemplateElement;
class Image;
class InputDeviceCapabilities;
class Locale;
class MutableCSSPropertyValueSet;
class NamedNodeMap;
class PointerLockOptions;
class PopupData;
class PseudoElement;
class ResizeObservation;
class ResizeObserver;
class ResizeObserverSize;
class ScrollIntoViewOptions;
class CheckVisibilityOptions;
class ScrollToOptions;
class ShadowRoot;
class ShadowRootInit;
class SpaceSplitString;
class StyleEngine;
class StylePropertyMap;
class StylePropertyMapReadOnly;
class StyleRecalcContext;
class StyleRequest;
class V8UnionBooleanOrScrollIntoViewOptions;

enum class CSSPropertyID;
enum class CSSValueID;
enum class DisplayLockActivationReason;
enum class DocumentUpdateReason;

struct FocusParams;

using ScrollOffset = gfx::Vector2dF;

enum SpellcheckAttributeState {
  kSpellcheckAttributeTrue,
  kSpellcheckAttributeFalse,
  kSpellcheckAttributeDefault
};

enum class ElementFlags {
  kTabIndexWasSetExplicitly = 1 << 0,
  kStyleAffectedByEmpty = 1 << 1,
  kIsInCanvasSubtree = 1 << 2,
  kContainsFullScreenElement = 1 << 3,
  kIsInTopLayer = 1 << 4,
  kContainsPersistentVideo = 1 << 5,

  kNumberOfElementFlags = 6,  // Size of bitfield used to store the flags.
};

enum class ShadowRootType;

enum class SlotAssignmentMode { kManual, kNamed };
enum class FocusDelegation { kNone, kDelegateFocus };

enum class SelectionBehaviorOnFocus {
  kReset,
  kRestore,
  kNone,
};

// https://html.spec.whatwg.org/C/#dom-document-nameditem-filter
enum class NamedItemType {
  kNone,
  kName,
  kNameOrId,
  kNameOrIdWithName,
};

enum class PopupValueType {
  kNone,
  kAuto,
  kHint,
  kManual,
};
constexpr const char* kPopupTypeValueAuto = "auto";
constexpr const char* kPopupTypeValueHint = "hint";
constexpr const char* kPopupTypeValueManual = "manual";

enum class PopupTriggerAction {
  kNone,
  kToggle,
  kShow,
  kHide,
};

enum class HidePopupFocusBehavior {
  kNone,
  kFocusPreviousElement,
};

enum class HidePopupForcingLevel {
  kHideAfterAnimations,
  kHideImmediately,
};

enum class HidePopupIndependence {
  kLeaveUnrelated,
  kHideUnrelated,
};

typedef HeapVector<Member<Attr>> AttrNodeList;

typedef HashMap<AtomicString, SpecificTrustedType> AttrNameToTrustedType;

class CORE_EXPORT Element : public ContainerNode, public Animatable {
  DEFINE_WRAPPERTYPEINFO();

 public:
  Element(const QualifiedName& tag_name,
          Document*,
          ConstructionType = kCreateElement);

  // Animatable implementation.
  Element* GetAnimationTarget() override;

  DEFINE_ATTRIBUTE_EVENT_LISTENER(beforecopy, kBeforecopy)
  DEFINE_ATTRIBUTE_EVENT_LISTENER(beforecut, kBeforecut)
  DEFINE_ATTRIBUTE_EVENT_LISTENER(beforepaste, kBeforepaste)
  DEFINE_ATTRIBUTE_EVENT_LISTENER(search, kSearch)

  bool hasAttribute(const QualifiedName&) const;
  const AtomicString& getAttribute(const QualifiedName&) const;

  // Passing g_null_atom as the second parameter removes the attribute when
  // calling either of these set methods.
  void setAttribute(const QualifiedName&, const AtomicString& value);
  void setAttribute(const QualifiedName&,
                    const AtomicString& value,
                    ExceptionState&);
  void SetSynchronizedLazyAttribute(const QualifiedName&,
                                    const AtomicString& value);

  void removeAttribute(const QualifiedName&);

  // Typed getters and setters for language bindings.
  int GetIntegralAttribute(const QualifiedName& attribute_name) const;
  int GetIntegralAttribute(const QualifiedName& attribute_name,
                           int default_value) const;
  unsigned int GetUnsignedIntegralAttribute(
      const QualifiedName& attribute_name) const;
  void SetIntegralAttribute(const QualifiedName& attribute_name, int value);
  void SetUnsignedIntegralAttribute(const QualifiedName& attribute_name,
                                    unsigned value,
                                    unsigned default_value = 0);
  double GetFloatingPointAttribute(
      const QualifiedName& attribute_name,
      double fallback_value = std::numeric_limits<double>::quiet_NaN()) const;
  void SetFloatingPointAttribute(const QualifiedName& attribute_name,
                                 double value);

  // Returns true if |this| element has attr-associated elements that were set
  // via the IDL, rather than computed from the content attribute.
  // See
  // https://whatpr.org/html/3917/common-dom-interfaces.html#reflecting-content-attributes-in-idl-attributes:element
  // for more information.
  // This is only exposed as an implementation detail to AXRelationCache, which
  // computes aria-owns differently for element reflection.
  bool HasExplicitlySetAttrAssociatedElements(const QualifiedName& name);
  Element* GetElementAttribute(const QualifiedName& name);
  void SetElementAttribute(const QualifiedName&, Element*);
  HeapVector<Member<Element>>* GetElementArrayAttribute(
      const QualifiedName& name);
  void SetElementArrayAttribute(
      const QualifiedName& name,
      const HeapVector<Member<Element>>* given_elements);

  // Call this to get the value of an attribute that is known not to be the
  // style attribute or one of the SVG animatable attributes.
  bool FastHasAttribute(const QualifiedName&) const;
  const AtomicString& FastGetAttribute(const QualifiedName&) const;
#if DCHECK_IS_ON()
  bool FastAttributeLookupAllowed(const QualifiedName&) const;
#endif

#if DUMP_NODE_STATISTICS
  bool HasNamedNodeMap() const;
#endif
  bool hasAttributes() const;

  bool hasAttribute(const AtomicString& name) const;
  bool hasAttributeNS(const AtomicString& namespace_uri,
                      const AtomicString& local_name) const;

  // Ignores namespace.
  bool HasAttributeIgnoringNamespace(const AtomicString& local_name) const;

  const AtomicString& getAttribute(const AtomicString& local_name) const {
    return GetAttributeHinted(local_name, WeakLowercaseIfNecessary(local_name));
  }

  const AtomicString& getAttributeNS(const AtomicString& namespace_uri,
                                     const AtomicString& local_name) const;

  void setAttribute(AtomicString name,
                    String value,
                    ExceptionState& exception_state = ASSERT_NO_EXCEPTION) {
    WTF::AtomicStringTable::WeakResult weak_lowercase_name =
        WeakLowercaseIfNecessary(name);
    SetAttributeHinted(std::move(name), weak_lowercase_name, std::move(value),
                       exception_state);
  }

  // Trusted Types variant for explicit setAttribute() use.
  void setAttribute(AtomicString name,
                    const V8TrustedType* trusted_string,
                    ExceptionState& exception_state) {
    WTF::AtomicStringTable::WeakResult weak_lowercase_name =
        WeakLowercaseIfNecessary(name);
    SetAttributeHinted(std::move(name), weak_lowercase_name, trusted_string,
                       exception_state);
  }

  // Returns attributes that should be checked against Trusted Types
  virtual const AttrNameToTrustedType& GetCheckedAttributeTypes() const;

  void setAttribute(const QualifiedName&, const String&, ExceptionState&);

  static bool ParseAttributeName(QualifiedName&,
                                 const AtomicString& namespace_uri,
                                 const AtomicString& qualified_name,
                                 ExceptionState&);
  void setAttributeNS(const AtomicString& namespace_uri,
                      const AtomicString& qualified_name,
                      String value,
                      ExceptionState& exception_state);
  void setAttributeNS(const AtomicString& namespace_uri,
                      const AtomicString& qualified_name,
                      const V8TrustedType* trusted_string,
                      ExceptionState& exception_state);

  bool toggleAttribute(const AtomicString&, ExceptionState&);
  bool toggleAttribute(const AtomicString&, bool force, ExceptionState&);

  const AtomicString& GetIdAttribute() const;
  void SetIdAttribute(const AtomicString&);

  const AtomicString& GetNameAttribute() const;
  const AtomicString& GetClassAttribute() const;

  // This is an operation defined in the DOM standard like:
  //   If element is in the HTML namespace and its node document is an HTML
  //   document, then set qualifiedName to qualifiedName in ASCII lowercase.
  //   https://dom.spec.whatwg.org/#concept-element-attributes-get-by-name
  AtomicString LowercaseIfNecessary(AtomicString) const;
  WTF::AtomicStringTable::WeakResult WeakLowercaseIfNecessary(
      const AtomicString&) const;

  // NoncedElement implementation: this is only used by HTMLElement and
  // SVGElement, but putting the implementation here allows us to use
  // ElementRareData to hold the data.
  const AtomicString& nonce() const;
  void setNonce(const AtomicString&);

  // Call this to get the value of the id attribute for style resolution
  // purposes.  The value will already be lowercased if the document is in
  // compatibility mode, so this function is not suitable for non-style uses.
  const AtomicString& IdForStyleResolution() const;

  // This getter takes care of synchronizing all attributes before returning the
  // AttributeCollection. If the Element has no attributes, an empty
  // AttributeCollection will be returned. This is not a trivial getter and its
  // return value should be cached for performance.
  AttributeCollection Attributes() const;
  // This variant will not update the potentially invalid attributes. To be used
  // when not interested in style attribute or one of the SVG attributes.
  AttributeCollection AttributesWithoutUpdate() const;
  // Similar to AttributesWithoutUpdate(), but with only the style attribute
  // exempt (ie., SVG attributes are always synchronized, for simplicity).
  // The style attribute is special because it is so frequently updated from
  // JavaScript and also easily identifiable (it is a single attribute).
  AttributeCollection AttributesWithoutStyleUpdate() const;

  void scrollIntoView(const V8UnionBooleanOrScrollIntoViewOptions* arg);
  void scrollIntoView(bool align_to_top = true);
  void scrollIntoViewWithOptions(const ScrollIntoViewOptions*);
  void ScrollIntoViewNoVisualUpdate(mojom::blink::ScrollIntoViewParamsPtr);
  void scrollIntoViewIfNeeded(bool center_if_needed = true);

  int OffsetLeft();
  int OffsetTop();
  int OffsetWidth();
  int OffsetHeight();

  Element* OffsetParent();
  int clientLeft();
  int clientTop();
  int ClientLeftNoLayout() const;
  int ClientTopNoLayout() const;
  int clientWidth();
  int clientHeight();
  double scrollLeft();
  double scrollTop();
  void setScrollLeft(double);
  void setScrollTop(double);
  virtual int scrollWidth();
  virtual int scrollHeight();

  void scrollBy(double x, double y);
  void scrollBy(const ScrollToOptions*);
  void scrollTo(double x, double y);
  void scrollTo(const ScrollToOptions*);
  LayoutBox* GetLayoutBoxForScrolling() const override;

  // Returns the bounds of this Element, unclipped, in the coordinate space of
  // the local root's widget. That is, in the outermost main frame, this will
  // scale and transform the bounds by the visual viewport transform (i.e.
  // pinch-zoom). In a local root that isn't main (i.e. a remote frame), the
  // returned bounds are unscaled by the visual viewport and are relative to
  // the local root frame.
  gfx::Rect BoundsInWidget() const;

  // Same as above but for outline rects.
  Vector<gfx::Rect> OutlineRectsInWidget(
      DocumentUpdateReason reason = DocumentUpdateReason::kUnknown) const;

  // Returns an intersection rectangle of the bounds rectangle and the visual
  // viewport's rectangle in the visual viewport's coordinate space.
  // Applies ancestors' frames' clipping, but does not (yet) apply (overflow)
  // element clipping (crbug.com/889840).
  gfx::Rect VisibleBoundsInVisualViewport() const;

  DOMRectList* getClientRects();
  // Returns a rectangle in zoomed pixel units.
  gfx::RectF GetBoundingClientRectNoLifecycleUpdateNoAdjustment() const;
  // Returns a rectangle in CSS pixel units.  i.e. ignorign zoom.
  gfx::RectF GetBoundingClientRectNoLifecycleUpdate() const;
  DOMRect* getBoundingClientRect();

  const AtomicString& computedRole();
  String computedName();

  AccessibleNode* ExistingAccessibleNode() const;
  AccessibleNode* accessibleNode();

  void DidMoveToNewDocument(Document&) override;

  void removeAttribute(const AtomicString& name) {
    RemoveAttributeHinted(name, WeakLowercaseIfNecessary(name));
  }
  void removeAttributeNS(const AtomicString& namespace_uri,
                         const AtomicString& local_name);

  Attr* DetachAttribute(wtf_size_t index);

  Attr* getAttributeNode(const AtomicString& name);
  Attr* getAttributeNodeNS(const AtomicString& namespace_uri,
                           const AtomicString& local_name);
  Attr* setAttributeNode(Attr*, ExceptionState&);
  Attr* setAttributeNodeNS(Attr*, ExceptionState&);
  Attr* removeAttributeNode(Attr*, ExceptionState&);

  Attr* AttrIfExists(const QualifiedName&);
  Attr* EnsureAttr(const QualifiedName&);

  AttrNodeList* GetAttrNodeList();

  CSSStyleDeclaration* style();
  StylePropertyMap* attributeStyleMap();
  StylePropertyMapReadOnly* ComputedStyleMap();

  const QualifiedName& TagQName() const { return tag_name_; }
  String tagName() const { return nodeName(); }

  bool HasTagName(const QualifiedName& tag_name) const {
    return tag_name_.Matches(tag_name);
  }
  bool HasTagName(const HTMLQualifiedName& tag_name) const {
    return ContainerNode::HasTagName(tag_name);
  }
  bool HasTagName(const MathMLQualifiedName& tag_name) const {
    return ContainerNode::HasTagName(tag_name);
  }
  bool HasTagName(const SVGQualifiedName& tag_name) const {
    return ContainerNode::HasTagName(tag_name);
  }

  // Should be called only by Document::createElementNS to fix up tag_name_
  // immediately after construction.
  void SetTagNameForCreateElementNS(const QualifiedName&);

  // A fast function for checking the local name against another atomic string.
  bool HasLocalName(const AtomicString& other) const {
    return tag_name_.LocalName() == other;
  }

  const AtomicString& localName() const { return tag_name_.LocalName(); }
  AtomicString LocalNameForSelectorMatching() const;
  const AtomicString& prefix() const { return tag_name_.Prefix(); }
  const AtomicString& namespaceURI() const { return tag_name_.NamespaceURI(); }

  bool IsHTMLWithTagName(const String& tag_name) const;

  const AtomicString& LocateNamespacePrefix(
      const AtomicString& namespace_uri) const;

  String nodeName() const override;

  Element& CloneWithChildren(CloneChildrenFlag flag, Document* = nullptr) const;
  Element& CloneWithoutChildren(Document* = nullptr) const;

  void SetBooleanAttribute(const QualifiedName&, bool);

  virtual const CSSPropertyValueSet* AdditionalPresentationAttributeStyle() {
    return nullptr;
  }
  void InvalidateStyleAttribute(bool only_changed_independent_properties);

  const CSSPropertyValueSet* InlineStyle() const {
    return GetElementData() ? GetElementData()->inline_style_.Get() : nullptr;
  }

  void SetInlineStyleProperty(CSSPropertyID,
                              CSSValueID identifier,
                              bool important = false);
  void SetInlineStyleProperty(CSSPropertyID,
                              double value,
                              CSSPrimitiveValue::UnitType,
                              bool important = false);
  void SetInlineStyleProperty(CSSPropertyID,
                              const CSSValue&,
                              bool important = false);
  bool SetInlineStyleProperty(CSSPropertyID,
                              const String& value,
                              bool important = false);
  void SetInlineStyleProperty(const CSSPropertyName&,
                              const CSSValue&,
                              bool important = false);

  bool RemoveInlineStyleProperty(CSSPropertyID);
  bool RemoveInlineStyleProperty(const AtomicString&);
  void RemoveAllInlineStyleProperties();

  void SynchronizeStyleAttributeInternal() const;

  const CSSPropertyValueSet* PresentationAttributeStyle();
  virtual bool IsPresentationAttribute(const QualifiedName& attr) const {
    return false;
  }
  virtual void CollectStyleForPresentationAttribute(
      const QualifiedName&,
      const AtomicString&,
      MutableCSSPropertyValueSet*) {}
  // Subclasses can override these functions if there is extra style that needs
  // to be mapped like attributes.
  virtual bool HasExtraStyleForPresentationAttribute() const { return false; }
  virtual void CollectExtraStyleForPresentationAttribute(
      MutableCSSPropertyValueSet*) {}

  // For exposing to DOM only.
  NamedNodeMap* attributesForBindings() const;
  Vector<AtomicString> getAttributeNames() const;

  enum class AttributeModificationReason {
    kDirectly,
    kByParser,
    kByCloning,
    kByMoveToNewDocument,
    kBySynchronizationOfLazyAttribute
  };
  struct AttributeModificationParams {
    STACK_ALLOCATED();

   public:
    AttributeModificationParams(const QualifiedName& qname,
                                const AtomicString& old_value,
                                const AtomicString& new_value,
                                AttributeModificationReason reason)
        : name(qname),
          old_value(old_value),
          new_value(new_value),
          reason(reason) {}

    const QualifiedName& name;
    const AtomicString& old_value;
    const AtomicString& new_value;
    const AttributeModificationReason reason;
  };

  // |attributeChanged| is called whenever an attribute is added, changed or
  // removed. It handles very common attributes such as id, class, name, style,
  // and slot.
  //
  // While the owner document is parsed, this function is called after all
  // attributes in a start tag were added to the element.
  virtual void AttributeChanged(const AttributeModificationParams&);

  // |parseAttribute| is called by |attributeChanged|. If an element
  // implementation needs to check an attribute update, override this function.
  //
  // While the owner document is parsed, this function is called after all
  // attributes in a start tag were added to the element.
  virtual void ParseAttribute(const AttributeModificationParams&);

  void DefaultEventHandler(Event&) override;

  // Popup API related functions.
  void UpdatePopupAttribute(String);
  bool HasPopupAttribute() const;
  PopupData* GetPopupData() const;
  PopupValueType PopupType() const;
  bool popupOpen() const;
  void showPopUp(ExceptionState& exception_state);
  void hidePopUp(ExceptionState& exception_state);
  void HidePopUpInternal(HidePopupFocusBehavior focus_behavior,
                         HidePopupForcingLevel forcing_level);
  void PopupHideFinishIfNeeded();
  static const Element* NearestOpenAncestralPopup(const Node& node,
                                                  bool inclusive = false);
  // Retrieves the element pointed to by this element's 'anchor' content
  // attribute, if that element exists, and if this element is a pop-up.
  Element* anchorElement() const;
  static void HandlePopupLightDismiss(const Event& event);
  void InvokePopup(Element* invoker);
  void SetPopupFocusOnShow();
  // This hides all visible popups up to, but not including,
  // |endpoint|. If |endpoint| is nullptr, all popups are hidden.
  static void HideAllPopupsUntil(const Element*,
                                 Document&,
                                 HidePopupFocusBehavior,
                                 HidePopupForcingLevel,
                                 HidePopupIndependence);

  // TODO(crbug.com/1197720): The popup position should be provided by the new
  // anchored positioning scheme.
  void SetNeedsRepositioningForSelectMenu(bool flag);
  void SetOwnerSelectMenuElement(HTMLSelectMenuElement* element);
  void AdjustPopupPositionForSelectMenu(ComputedStyle& style);

  virtual bool HasLegalLinkAttribute(const QualifiedName&) const;
  virtual const QualifiedName& SubResourceAttributeName() const;

  // Only called by the parser immediately after element construction.
  void ParserSetAttributes(const Vector<Attribute, kAttributePrealloc>&);

  // Remove attributes that might introduce scripting from the vector leaving
  // the element unchanged.
  void StripScriptingAttributes(Vector<Attribute, kAttributePrealloc>&) const;

  bool SharesSameElementData(const Element& other) const {
    return GetElementData() == other.GetElementData();
  }

  // Clones attributes only.
  void CloneAttributesFrom(const Element&);

  bool HasEquivalentAttributes(const Element& other) const;

  // Step 5 of https://dom.spec.whatwg.org/#concept-node-clone
  virtual void CloneNonAttributePropertiesFrom(const Element&,
                                               CloneChildrenFlag) {}

  void AttachLayoutTree(AttachContext&) override;
  void DetachLayoutTree(bool performing_reattach = false) override;

  virtual LayoutObject* CreateLayoutObject(const ComputedStyle&, LegacyLayout);
  virtual bool LayoutObjectIsNeeded(const ComputedStyle&) const;

  const ComputedStyle* ParentComputedStyle() const;

  // Returns a StyleRecalcChange to be combined with the outer
  // StyleRecalcChange. This is used to recalculate the style of subsequent
  // siblings.
  StyleRecalcChange RecalcStyle(const StyleRecalcChange,
                                const StyleRecalcContext&);
  void RecalcStyleForTraversalRootAncestor();
  void RebuildLayoutTreeForTraversalRootAncestor() {
    RebuildFirstLetterLayoutTree();
    WhitespaceAttacher whitespace_attacher;
    RebuildMarkerLayoutTree(whitespace_attacher);
    HandleSubtreeModifications();
  }
  bool NeedsRebuildChildLayoutTrees(
      const WhitespaceAttacher& whitespace_attacher) const {
    return ChildNeedsReattachLayoutTree() || NeedsWhitespaceChildrenUpdate() ||
           (whitespace_attacher.TraverseIntoDisplayContents() &&
            HasDisplayContentsStyle());
  }
  bool NeedsRebuildLayoutTree(
      const WhitespaceAttacher& whitespace_attacher) const {
    return NeedsReattachLayoutTree() ||
           NeedsRebuildChildLayoutTrees(whitespace_attacher) ||
           NeedsLayoutSubtreeUpdate();
  }
  void RebuildLayoutTree(WhitespaceAttacher&);

  // Reattach layout tree for all children but not the element itself. This is
  // only used for UpdateStyleAndLayoutTreeForContainer when:
  // 1. Re-attaching fieldset when the fieldset layout tree changes and the size
  //    query container is a fieldset.
  // 2. Re-attaching for legacy box tree when table-* boxes have columns.
  //
  // Case 2 is only necessary until table fragmentation is shipped for LayoutNG.
  //
  void ReattachLayoutTreeChildren(base::PassKey<StyleEngine>);

  void HandleSubtreeModifications();
  void PseudoStateChanged(CSSSelector::PseudoType);
  void PseudoStateChangedForTesting(CSSSelector::PseudoType);
  void SetAnimationStyleChange(bool);
  void SetNeedsAnimationStyleRecalc();

  void SetNeedsCompositingUpdate();

  // Assigns a crop-ID to the element. It's an error to try to call this
  // if the element already has a crop-ID attached (even if attempting to
  // set the same crop-ID again).
  //
  // Not all element types have a region capture crop-ID, however using it here
  // allows access to the element rare data struct. Currently, once an element
  // is marked for region capture it cannot be unmarked.
  void SetRegionCaptureCropId(std::unique_ptr<RegionCaptureCropId> crop_id);

  // Returns a pointer to the crop-ID if one was set; nullptr otherwise.
  const RegionCaptureCropId* GetRegionCaptureCropId() const;

  // Support for all elements with region capture is currently experimental.
  // TODO(crbug.com/1332641): Remove this after support is stable.
  virtual bool IsSupportedByRegionCapture() const;

  ShadowRoot* attachShadow(const ShadowRootInit*, ExceptionState&);

  void AttachDeclarativeShadowRoot(HTMLTemplateElement*,
                                   ShadowRootType,
                                   FocusDelegation,
                                   SlotAssignmentMode);

  ShadowRoot& CreateUserAgentShadowRoot();
  ShadowRoot& AttachShadowRootInternal(
      ShadowRootType,
      FocusDelegation focus_delegation = FocusDelegation::kNone,
      SlotAssignmentMode slot_assignment_mode = SlotAssignmentMode::kNamed);

  // Returns the shadow root attached to this element if it is a shadow host.
  ShadowRoot* GetShadowRoot() const;
  ShadowRoot* OpenShadowRoot() const;
  ShadowRoot* ClosedShadowRoot() const;
  ShadowRoot* AuthorShadowRoot() const;
  ShadowRoot* UserAgentShadowRoot() const;

  ShadowRoot& EnsureUserAgentShadowRoot();

  // Implements manual slot assignment for user agent shadow roots.
  virtual void ManuallyAssignSlots() { DCHECK(false); }

  bool IsInDescendantTreeOf(const Element* shadow_host) const;

  // Returns the Element’s ComputedStyle. If the ComputedStyle is not already
  // stored on the Element, computes the ComputedStyle and stores it on the
  // Element’s ElementRareData.  Used for getComputedStyle when Element is
  // display none.
  const ComputedStyle* EnsureComputedStyle(
      PseudoId = kPseudoIdNone,
      const AtomicString& pseudo_argument = g_null_atom);

  bool HasDisplayContentsStyle() const;

  bool ShouldStoreComputedStyle(const ComputedStyle&) const;

  // Methods for indicating the style is affected by dynamic updates (e.g.,
  // children changing, our position changing in our sibling list, etc.)
  bool StyleAffectedByEmpty() const {
    return HasElementFlag(ElementFlags::kStyleAffectedByEmpty);
  }
  void SetStyleAffectedByEmpty() {
    SetElementFlag(ElementFlags::kStyleAffectedByEmpty);
  }

  void SetIsInCanvasSubtree(bool value) {
    SetElementFlag(ElementFlags::kIsInCanvasSubtree, value);
  }
  bool IsInCanvasSubtree() const {
    return HasElementFlag(ElementFlags::kIsInCanvasSubtree);
  }

  bool IsDefined() const {
    // An element whose custom element state is "uncustomized" or "custom"
    // is said to be defined.
    // https://dom.spec.whatwg.org/#concept-element-defined
    return GetCustomElementState() == CustomElementState::kUncustomized ||
           GetCustomElementState() == CustomElementState::kCustom;
  }

  AtomicString ComputeInheritedLanguage() const;
  Locale& GetLocale() const;

  virtual void AccessKeyAction(SimulatedClickCreationScope) {}

  virtual bool IsURLAttribute(const Attribute&) const { return false; }
  virtual bool IsHTMLContentAttribute(const Attribute&) const { return false; }
  bool IsJavaScriptURLAttribute(const Attribute&) const;
  virtual bool IsSVGAnimationAttributeSettingJavaScriptURL(
      const Attribute&) const {
    return false;
  }
  bool IsScriptingAttribute(const Attribute&) const;

  virtual bool IsLiveLink() const { return false; }
  KURL HrefURL() const;

  KURL GetURLAttribute(const QualifiedName&) const;

  KURL GetNonEmptyURLAttribute(const QualifiedName&) const;

  virtual const AtomicString ImageSourceURL() const;
  virtual Image* ImageContents() { return nullptr; }

  // Returns true if this is a shadow host, and its ShadowRoot has
  // delegatesFocus flag.
  bool DelegatesFocus() const;
  Element* GetFocusableArea() const;
  Element* GetAutofocusDelegate() const;
  // Element focus function called through IDL (i.e. element.focus() in JS)
  // Delegates to virtual Focus() with focus type set to kScript
  void focusForBindings();
  void focusForBindings(const FocusOptions*);
  // Element focus function called from outside IDL (user focus,
  // accessibility, etc...)
  virtual void Focus(const FocusParams&);
  // Delegates to virtual Focus() with focus type set to kNone
  void Focus();
  void Focus(const FocusOptions*);

  void UpdateSelectionOnFocus(SelectionBehaviorOnFocus);
  // This function is called after SetFocused(true) before dispatching 'focus'
  // event, or is called just after a layout after changing <input> type.
  virtual void UpdateSelectionOnFocus(SelectionBehaviorOnFocus,
                                      const FocusOptions*);
  virtual void blur();

  // Whether this element can receive focus at all. Most elements are not
  // focusable but some elements, such as form controls and links, are. Unlike
  // layoutObjectIsFocusable(), this method may be called when layout is not up
  // to date, so it must not use the layoutObject to determine focusability.
  virtual bool SupportsFocus() const;
  // IsFocusable(), IsKeyboardFocusable(), and IsMouseFocusable() check
  // whether the element can actually be focused. Callers should ensure
  // ComputedStyle is up to date;
  // e.g. by calling Document::UpdateStyleAndLayoutTree().
  bool IsFocusable() const;
  virtual bool IsKeyboardFocusable() const;
  virtual bool IsMouseFocusable() const;
  // IsBaseElementFocusable() is used by some subclasses to check if the base
  // element is focusable. This is used to avoid infinite recursion.
  bool IsBaseElementFocusable() const;
  bool IsFocusedElementInDocument() const;
  Element* AdjustedFocusedElementInTreeScope() const;
  bool IsAutofocusable() const;

  virtual void DispatchFocusEvent(
      Element* old_focused_element,
      mojom::blink::FocusType,
      InputDeviceCapabilities* source_capabilities = nullptr);
  virtual void DispatchBlurEvent(
      Element* new_focused_element,
      mojom::blink::FocusType,
      InputDeviceCapabilities* source_capabilities = nullptr);
  virtual void DispatchFocusInEvent(
      const AtomicString& event_type,
      Element* old_focused_element,
      mojom::blink::FocusType,
      InputDeviceCapabilities* source_capabilities = nullptr);
  void DispatchFocusOutEvent(
      const AtomicString& event_type,
      Element* new_focused_element,
      InputDeviceCapabilities* source_capabilities = nullptr);

  // The implementations of |innerText()| and |GetInnerTextWithoutUpdate()| are
  // found in "element_inner_text.cc".
  String GetInnerTextWithoutUpdate();  // Avoids layout update.
  String innerText();
  String outerText();

  Element* insertAdjacentElement(const String& where,
                                 Element* new_child,
                                 ExceptionState&);
  void insertAdjacentText(const String& where,
                          const String& text,
                          ExceptionState&);
  void insertAdjacentHTML(const String& where,
                          const String& html,
                          ExceptionState&);

  String innerHTML() const;
  String outerHTML() const;
  void setInnerHTML(const String&, ExceptionState& = ASSERT_NO_EXCEPTION);
  void setInnerHTMLWithDeclarativeShadowDOMForTesting(const String& html);
  String getInnerHTML(const GetInnerHTMLOptions* options) const;
  void setOuterHTML(const String&, ExceptionState& = ASSERT_NO_EXCEPTION);

  void setPointerCapture(PointerId poinetr_id, ExceptionState&);
  void releasePointerCapture(PointerId pointer_id, ExceptionState&);

  // Returns true iff the element would capture the next pointer event. This
  // is true between a setPointerCapture call and a releasePointerCapture (or
  // implicit release) call:
  // https://w3c.github.io/pointerevents/#dom-element-haspointercapture
  bool hasPointerCapture(PointerId pointer_id) const;

  String TextFromChildren();

  virtual String title() const { return String(); }
  virtual String DefaultToolTip() const { return String(); }

  virtual const AtomicString& ShadowPseudoId() const;
  // The specified string must start with "-webkit-" or "-internal-". The
  // former can be used as a selector in any places, and the latter can be
  // used only in UA stylesheet.
  void SetShadowPseudoId(const AtomicString&);

  // Called by the parser when this element's close tag is reached, signaling
  // that all child tags have been parsed and added.  This is needed for
  // <applet> and <object> elements, which can't lay themselves out until they
  // know all of their nested <param>s. [Radar 3603191, 4040848].  Also used for
  // script elements and some SVG elements for similar purposes, but making
  // parsing a special case in this respect should be avoided if possible.
  virtual void FinishParsingChildren();

  virtual void BeginParsingChildren() { SetIsFinishedParsingChildren(false); }

  // Returns the pseudo element for the given PseudoId type.
  // |document_transition_tag| is used to uniquely identify a pseudo element
  // from a set of pseudo elements which share the same |pseudo_id|. The current
  // usage of this ID is limited to pseudo elements generated for a
  // DocumentTransition. See
  // third_party/blink/renderer/core/document_transition/README.md
  PseudoElement* GetPseudoElement(
      PseudoId,
      const AtomicString& document_transition_tag = g_null_atom) const;
  LayoutObject* PseudoElementLayoutObject(PseudoId) const;

  bool PseudoElementStylesDependOnFontMetrics() const;

  // Retrieve the ComputedStyle (if any) corresponding to the provided
  // PseudoId from cache, calculating the ComputedStyle on-demand if it's
  // missing from the cache. The |pseudo_argument| is also used to match the
  // ComputedStyle in cases where the PseudoId corresponds to a pseudo element
  // that takes arguments (e.g. ::highlight()).
  const ComputedStyle* CachedStyleForPseudoElement(
      PseudoId,
      const AtomicString& pseudo_argument = g_null_atom);

  // Calculate the ComputedStyle corresponding to the provided StyleRequest,
  // bypassing the pseudo style cache.
  //
  // This is appropriate to use if the cached version is invalid in a given
  // situation.
  scoped_refptr<ComputedStyle> UncachedStyleForPseudoElement(
      const StyleRequest&);

  // This is the same as UncachedStyleForPseudoElement, except that the caller
  // must provide an appropriate StyleRecalcContext such that e.g. @container
  // queries are evaluated correctly.
  //
  // See StyleRecalcContext for more information.
  scoped_refptr<ComputedStyle> StyleForPseudoElement(const StyleRecalcContext&,
                                                     const StyleRequest&);

  // Returns the ComputedStyle after applying the declarations in the @try block
  // at the given index. Returns nullptr if the current element doesn't use
  // position fallback, or if the index is out of bound.
  // The style is computed on demand and cached on the ComputedStyle of |this|.
  const ComputedStyle* StyleForPositionFallback(unsigned index);

  virtual bool CanGeneratePseudoElement(PseudoId) const;

  virtual bool MatchesDefaultPseudoClass() const { return false; }
  virtual bool MatchesEnabledPseudoClass() const { return false; }
  virtual bool MatchesReadOnlyPseudoClass() const { return false; }
  virtual bool MatchesReadWritePseudoClass() const { return false; }
  virtual bool MatchesValidityPseudoClasses() const { return false; }

  virtual bool MayTriggerVirtualKeyboard() const;

  // https://dom.spec.whatwg.org/#dom-element-matches
  bool matches(const AtomicString& selectors, ExceptionState&);
  bool matches(const AtomicString& selectors);

  // https://dom.spec.whatwg.org/#dom-element-closest
  Element* closest(const AtomicString& selectors, ExceptionState&);
  Element* closest(const AtomicString& selectors);

  virtual bool ShouldAppearIndeterminate() const { return false; }

  DOMTokenList& classList();

  DOMStringMap& dataset();

  virtual bool IsDateTimeEditElement() const { return false; }
  virtual bool IsDateTimeFieldElement() const { return false; }
  virtual bool IsPickerIndicatorElement() const { return false; }

  virtual bool IsFormControlElement() const { return false; }
  virtual bool IsSpinButtonElement() const { return false; }
  // This returns true for <textarea> and some types of <input>.
  virtual bool IsTextControl() const { return false; }
  virtual bool IsOptionalFormControl() const { return false; }
  virtual bool IsRequiredFormControl() const { return false; }
  virtual bool willValidate() const { return false; }
  virtual bool IsValidElement() { return false; }
  virtual bool IsInRange() const { return false; }
  virtual bool IsOutOfRange() const { return false; }
  virtual bool IsClearButtonElement() const { return false; }
  virtual bool IsScriptElement() const { return false; }
  virtual bool IsVTTCueBackgroundBox() const { return false; }
  virtual bool IsVTTCueBox() const { return false; }
  virtual bool IsSliderThumbElement() const { return false; }
  virtual bool IsOutputElement() const { return false; }

  // Elements that may have an insertion mode other than "in body" should
  // override this and return true.
  // https://html.spec.whatwg.org/C/#reset-the-insertion-mode-appropriately
  virtual bool HasNonInBodyInsertionMode() const { return false; }

  bool CanContainRangeEndPoint() const override { return true; }

  // Used for disabled form elements; if true, prevents mouse events from being
  // dispatched to event listeners, and prevents DOMActivate events from being
  // sent at all.
  virtual bool IsDisabledFormControl() const { return false; }

  // Return true if we should force legacy layout on this element and all
  // descendants. Note that even if this element returns true, it's not implied
  // that all descendants will return the same. Once an element needs to force
  // legacy layout, though, the layout engine knows that it will have to perform
  // legacy layout on the entire subtree, unless this is overridden by
  // ShouldForceNGLayout().
  bool ShouldForceLegacyLayout() const {
    if (TypeShouldForceLegacyLayout())
      return true;
    if (!HasRareData())
      return false;
    return StyleShouldForceLegacyLayout() || ShouldForceLegacyLayoutForChild();
  }

  void ResetForceLegacyLayoutForPrinting();

  virtual void BuildPendingResource() {}

  void SetCustomElementDefinition(CustomElementDefinition*);
  CustomElementDefinition* GetCustomElementDefinition() const;
  // https://dom.spec.whatwg.org/#concept-element-is-value
  void SetIsValue(const AtomicString&);
  const AtomicString& IsValue() const;
  void SetDidAttachInternals();
  bool DidAttachInternals() const;
  ElementInternals& EnsureElementInternals();
  const ElementInternals* GetElementInternals() const;

  bool ContainsFullScreenElement() const {
    return HasElementFlag(ElementFlags::kContainsFullScreenElement);
  }
  void SetContainsFullScreenElement(bool);
  void SetContainsFullScreenElementOnAncestorsCrossingFrameBoundaries(bool);

  bool ContainsPersistentVideo() const {
    return HasElementFlag(ElementFlags::kContainsPersistentVideo);
  }
  void SetContainsPersistentVideo(bool);

  bool IsInTopLayer() const {
    return HasElementFlag(ElementFlags::kIsInTopLayer);
  }
  void SetIsInTopLayer(bool);

  ScriptValue requestPointerLock(ScriptState* script_state,
                                 const PointerLockOptions* options,
                                 ExceptionState& exception_state);

  bool IsSpellCheckingEnabled() const;

  // FIXME: public for LayoutTreeBuilder, we shouldn't expose this though.
  scoped_refptr<ComputedStyle> StyleForLayoutObject(const StyleRecalcContext&);

  bool HasID() const;
  bool HasClass() const;
  const SpaceSplitString& ClassNames() const;
  bool HasClassName(const AtomicString& class_name) const;

  // Returns true if the element has 1 or more part names.
  bool HasPart() const;
  // Returns the list of part names if it has ever been created.
  DOMTokenList* GetPart() const;
  // IDL method.
  // Returns the list of part names, creating it if it doesn't exist.
  DOMTokenList& part();

  bool HasPartNamesMap() const;
  const NamesMap* PartNamesMap() const;

  ScrollOffset SavedLayerScrollOffset() const;
  void SetSavedLayerScrollOffset(const ScrollOffset&);

  ElementAnimations* GetElementAnimations() const;
  ElementAnimations& EnsureElementAnimations();
  bool HasAnimations() const;

  void SynchronizeAttribute(const AtomicString& local_name) const {
    SynchronizeAttributeHinted(local_name,
                               WeakLowercaseIfNecessary(local_name));
  }

  MutableCSSPropertyValueSet& EnsureMutableInlineStyle();
  void ClearMutableInlineStyleIfEmpty();

  CSSPropertyValueSet* CreatePresentationAttributeStyle();

  void setTabIndex(int);
  int tabIndex() const;

  void setEditContext(EditContext* editContext);
  EditContext* editContext() const;

  // Helpers for V8DOMActivityLogger::logEvent.  They call logEvent only if
  // the element is isConnected() and the context is an isolated world.
  void LogAddElementIfIsolatedWorldAndInDocument(const char element[],
                                                 const QualifiedName& attr1);
  void LogAddElementIfIsolatedWorldAndInDocument(const char element[],
                                                 const QualifiedName& attr1,
                                                 const QualifiedName& attr2);
  void LogAddElementIfIsolatedWorldAndInDocument(const char element[],
                                                 const QualifiedName& attr1,
                                                 const QualifiedName& attr2,
                                                 const QualifiedName& attr3);
  void LogUpdateAttributeIfIsolatedWorldAndInDocument(
      const char element[],
      const AttributeModificationParams&);

  void Trace(Visitor*) const override;

  SpellcheckAttributeState GetSpellcheckAttributeState() const;

  ElementIntersectionObserverData* IntersectionObserverData() const;
  ElementIntersectionObserverData& EnsureIntersectionObserverData();

  HeapHashMap<Member<ResizeObserver>, Member<ResizeObservation>>*
  ResizeObserverData() const;
  HeapHashMap<Member<ResizeObserver>, Member<ResizeObservation>>&
  EnsureResizeObserverData();

  DisplayLockContext* GetDisplayLockContext() const {
    if (LIKELY(!HasDisplayLockContext()))
      return nullptr;
    return GetDisplayLockContextFromRareData();
  }
  DisplayLockContext& EnsureDisplayLockContext();

  bool ChildStyleRecalcBlockedByDisplayLock() const;

  // Activates all activatable (for a given reason) locked ancestors for this
  // element. Return true if we activated at least one previously locked
  // element.
  bool ActivateDisplayLockIfNeeded(DisplayLockActivationReason reason);

  ContainerQueryData* GetContainerQueryData() const;
  ContainerQueryEvaluator* GetContainerQueryEvaluator() const;
  ContainerQueryEvaluator& EnsureContainerQueryEvaluator();
  bool SkippedContainerStyleRecalc() const;

  virtual void SetActive(bool active);
  virtual void SetHovered(bool hovered);

  // Classes overriding this method can return true when an element has
  // been determined to be from an ad. Returns false by default.
  virtual bool IsAdRelated() const { return false; }

  void NotifyInlineStyleMutation();

  // For undo stack cleanup
  bool HasUndoStack() const;
  void SetHasUndoStack(bool);

  // For font-related style invalidation.
  void SetScrollbarPseudoElementStylesDependOnFontMetrics(bool);

  bool AffectedBySubjectHas() const;
  void SetAffectedBySubjectHas();
  bool AffectedByNonSubjectHas() const;
  void SetAffectedByNonSubjectHas();
  bool AncestorsOrAncestorSiblingsAffectedByHas() const;
  void SetAncestorsOrAncestorSiblingsAffectedByHas();
  unsigned GetSiblingsAffectedByHasFlags() const;
  bool HasSiblingsAffectedByHasFlags(unsigned flags) const;
  void SetSiblingsAffectedByHasFlags(unsigned flags);
  bool AffectedByPseudoInHas() const;
  void SetAffectedByPseudoInHas();
  bool AncestorsOrSiblingsAffectedByHoverInHas() const;
  void SetAncestorsOrSiblingsAffectedByHoverInHas();
  bool AncestorsOrSiblingsAffectedByActiveInHas() const;
  void SetAncestorsOrSiblingsAffectedByActiveInHas();
  bool AncestorsOrSiblingsAffectedByFocusInHas() const;
  void SetAncestorsOrSiblingsAffectedByFocusInHas();
  bool AncestorsOrSiblingsAffectedByFocusVisibleInHas() const;
  void SetAncestorsOrSiblingsAffectedByFocusVisibleInHas();
  bool AffectedByLogicalCombinationsInHas() const;
  void SetAffectedByLogicalCombinationsInHas();
  bool AffectedByMultipleHas() const;
  void SetAffectedByMultipleHas();

  void SaveIntrinsicSize(ResizeObserverSize* size);
  const ResizeObserverSize* LastIntrinsicSize() const;

  // Returns a unique pseudo element for the given |pseudo_id| and
  // |document_transition_tag| originating from this DOM element.
  // This pseudo element may be directly associated with this element or nested
  // inside a hierarchy of pseudo elements.
  PseudoElement* GetNestedPseudoElement(
      PseudoId pseudo_id,
      const AtomicString& document_transition_tag) const;

  void RecalcTransitionPseudoTreeStyle(
      const Vector<AtomicString>& document_transition_tags);
  void RebuildTransitionPseudoLayoutTree(
      const Vector<AtomicString>& document_transition_tags);

  // Returns true if the element has the 'inert' attribute, forcing itself and
  // all its subtree to be inert.
  bool IsInertRoot();

  FocusgroupFlags GetFocusgroupFlags() const;

  bool checkVisibility(CheckVisibilityOptions* options) const;

  bool IsDocumentElement() const;

  bool IsReplacedElementRespectingCSSOverflow() const;

  CSSToggleMap* toggles() { return &EnsureToggleMap(); }

  CSSToggleMap* GetToggleMap();
  CSSToggleMap& EnsureToggleMap();

 protected:
  const ElementData* GetElementData() const { return element_data_.Get(); }
  UniqueElementData& EnsureUniqueElementData();

  bool IsViewportScrollElement();
  void RecordScrollbarSizeForStudy(int measurement,
                                   bool is_width,
                                   bool is_offset);

  void AddPropertyToPresentationAttributeStyle(MutableCSSPropertyValueSet*,
                                               CSSPropertyID,
                                               CSSValueID identifier);
  void AddPropertyToPresentationAttributeStyle(MutableCSSPropertyValueSet*,
                                               CSSPropertyID,
                                               double value,
                                               CSSPrimitiveValue::UnitType);
  void AddPropertyToPresentationAttributeStyle(MutableCSSPropertyValueSet*,
                                               CSSPropertyID,
                                               const String& value);
  void AddPropertyToPresentationAttributeStyle(MutableCSSPropertyValueSet*,
                                               CSSPropertyID,
                                               const CSSValue&);

  InsertionNotificationRequest InsertedInto(ContainerNode&) override;
  void RemovedFrom(ContainerNode&) override;
  void ChildrenChanged(const ChildrenChange&) override;

  virtual void WillRecalcStyle(const StyleRecalcChange);
  virtual void DidRecalcStyle(const StyleRecalcChange);
  virtual scoped_refptr<ComputedStyle> CustomStyleForLayoutObject(
      const StyleRecalcContext&);

  virtual NamedItemType GetNamedItemType() const {
    return NamedItemType::kNone;
  }

  bool SupportsSpatialNavigationFocus() const;

  void ClearTabIndexExplicitlyIfNeeded();
  void SetTabIndexExplicitly();
  // Returns false if the style prevents focus. Returning true doesn't imply
  // focusability, there may be other conditions like SupportsFocus().
  // Subclasses may override this method to affect focusability. This method
  // must be called on an up-to-date ComputedStyle, so it may use existence of
  // layoutObject and the LayoutObject::style() to reason about focusability.
  // However, it must not retrieve layout information like position and size.
  // This method cannot be moved to LayoutObject because some focusable nodes
  // don't have layoutObjects. e.g., HTMLOptionElement.
  virtual bool IsFocusableStyle() const;
  // Contains the base logic for IsFocusableStyle.
  bool IsBaseElementFocusableStyle() const;
  // Similar to IsFocusableStyle, except that it will ensure that any deferred
  // work to create layout objects is completed (e.g. in display-locked trees).
  bool IsFocusableStyleAfterUpdate() const;

  // Is the node descendant of this in something clickable/activatable, such
  // that we shouldn't handle events targeting it?
  bool IsClickableControl(Node*);

  // ClassAttributeChanged() and UpdateClassList() exist to share code between
  // ParseAttribute (called via setAttribute()) and SvgAttributeChanged (called
  // when element.className.baseVal is set or when the 'class' attribute is
  // animated by SMIL).
  void ClassAttributeChanged(const AtomicString& new_class_string);
  void UpdateClassList(const AtomicString& old_class_string,
                       const AtomicString& new_class_string);

  static bool AttributeValueIsJavaScriptURL(const Attribute&);

  scoped_refptr<ComputedStyle> OriginalStyleForLayoutObject(
      const StyleRecalcContext&);

  // Step 4 of http://domparsing.spec.whatwg.org/#insertadjacenthtml()
  Node* InsertAdjacent(const String& where, Node* new_child, ExceptionState&);

  virtual void ParserDidSetAttributes() {}

  // The "nonce" attribute is hidden when:
  // 1) The Content-Security-Policy is delivered from the HTTP headers.
  // 2) The Element is part of the active document.
  // See https://github.com/whatwg/html/pull/2373
  //
  // This applies to the element of the HTML and SVG namespaces.
  //
  // This function clears the "nonce" attribute whenever conditions (1) and (2)
  // are met.
  void HideNonce();

  // Adjust the state of legacy layout forcing for this element (and its
  // subtree). Input is the state inherited from the parent element. Output will
  // be modified if required by this element.
  void AdjustForceLegacyLayout(const ComputedStyle*,
                               bool* should_force_legacy_layout);

 private:
  friend class AXObject;
  struct AffectedByPseudoStateChange;

  void ScrollLayoutBoxBy(const ScrollToOptions*);
  void ScrollLayoutBoxTo(const ScrollToOptions*);
  void ScrollFrameBy(const ScrollToOptions*);
  void ScrollFrameTo(const ScrollToOptions*);

  bool HasElementFlag(ElementFlags mask) const {
    return HasRareData() && HasElementFlagInternal(mask);
  }
  void SetElementFlag(ElementFlags, bool value = true);
  void ClearElementFlag(ElementFlags);
  bool HasElementFlagInternal(ElementFlags) const;

  bool IsElementNode() const =
      delete;  // This will catch anyone doing an unnecessary check.
  bool IsDocumentFragment() const =
      delete;  // This will catch anyone doing an unnecessary check.
  bool IsDocumentNode() const =
      delete;  // This will catch anyone doing an unnecessary check.

  bool CanAttachShadowRoot() const;
  const char* ErrorMessageForAttachShadow() const;

  void StyleAttributeChanged(const AtomicString& new_style_string,
                             AttributeModificationReason);

  void UpdatePresentationAttributeStyle();

  void InlineStyleChanged();
  void SetInlineStyleFromString(const AtomicString&);

  // If the only inherited changes in the parent element are independent,
  // these changes can be directly propagated to this element (the child).
  // If these conditions are met, propagates the changes to the current style
  // and returns the new style. Otherwise, returns null.
  scoped_refptr<ComputedStyle> PropagateInheritedProperties();

  const ComputedStyle* EnsureOwnComputedStyle(
      const StyleRecalcContext&,
      PseudoId,
      const AtomicString& pseudo_argument = g_null_atom);

  // Recalculate the ComputedStyle for this element and return a
  // StyleRecalcChange for propagation/traversal into child nodes.
  StyleRecalcChange RecalcOwnStyle(const StyleRecalcChange,
                                   const StyleRecalcContext&);

  // Returns true if we should skip style recalc for the subtree because this
  // element is a container for size container queries and we are guaranteed to
  // reach this element during the subsequent layout to continue doing
  // interleaved style and layout.
  bool SkipStyleRecalcForContainer(const ComputedStyle& style,
                                   const StyleRecalcChange& child_change);

  void MarkNonSlottedHostChildrenForStyleRecalc();

  void RebuildPseudoElementLayoutTree(PseudoId, WhitespaceAttacher&);
  void RebuildFirstLetterLayoutTree();
  void RebuildMarkerLayoutTree(WhitespaceAttacher&);
  void RebuildShadowRootLayoutTree(WhitespaceAttacher&);
  inline void CheckForEmptyStyleChange(const Node* node_before_change,
                                       const Node* node_after_change);

  PseudoElement* UpdatePseudoElement(
      PseudoId,
      const StyleRecalcChange,
      const StyleRecalcContext&,
      const AtomicString& document_transition_tag = g_null_atom);

  enum class StyleUpdatePhase {
    kRecalc,
    kRebuildLayoutTree,
    kAttachLayoutTree,
  };

  // Special focus handling for popups.
  Element* GetPopupFocusableArea() const;

  void UpdateFirstLetterPseudoElement(StyleUpdatePhase,
                                      const StyleRecalcContext&);

  // Creates a StyleRecalcContext and invokes the method above. Only use this
  // when there is no StyleRecalcContext available.
  void UpdateFirstLetterPseudoElement(StyleUpdatePhase);

  inline PseudoElement* CreatePseudoElementIfNeeded(
      PseudoId,
      const StyleRecalcContext&,
      const AtomicString& document_transition_tag = g_null_atom);
  void AttachPseudoElement(PseudoId, AttachContext&);
  void DetachPseudoElement(PseudoId, bool performing_reattach);

  void AttachPrecedingPseudoElements(AttachContext& context) {
    AttachPseudoElement(kPseudoIdMarker, context);
    AttachPseudoElement(kPseudoIdBefore, context);
  }

  void AttachSucceedingPseudoElements(AttachContext& context) {
    AttachPseudoElement(kPseudoIdAfter, context);
    AttachPseudoElement(kPseudoIdBackdrop, context);
    UpdateFirstLetterPseudoElement(StyleUpdatePhase::kAttachLayoutTree);
    AttachPseudoElement(kPseudoIdFirstLetter, context);
  }

  void DetachPrecedingPseudoElements(bool performing_reattach) {
    DetachPseudoElement(kPseudoIdMarker, performing_reattach);
    DetachPseudoElement(kPseudoIdBefore, performing_reattach);
  }

  void DetachSucceedingPseudoElements(bool performing_reattach) {
    DetachPseudoElement(kPseudoIdAfter, performing_reattach);
    DetachPseudoElement(kPseudoIdBackdrop, performing_reattach);
    DetachPseudoElement(kPseudoIdFirstLetter, performing_reattach);
  }

  ShadowRoot& CreateAndAttachShadowRoot(ShadowRootType);

  // FIXME: Everyone should allow author shadows.
  virtual bool AreAuthorShadowsAllowed() const { return true; }
  virtual void DidAddUserAgentShadowRoot(ShadowRoot&) {}
  virtual bool AlwaysCreateUserAgentShadowRoot() const { return false; }

  void DidAddAttribute(const QualifiedName&, const AtomicString&);
  void WillModifyAttribute(const QualifiedName&,
                           const AtomicString& old_value,
                           const AtomicString& new_value);
  void DidModifyAttribute(const QualifiedName&,
                          const AtomicString& old_value,
                          const AtomicString& new_value,
                          AttributeModificationReason reason);
  void DidRemoveAttribute(const QualifiedName&, const AtomicString& old_value);

  void SynchronizeAllAttributes() const;
  void SynchronizeAttribute(const QualifiedName&) const;
  void SynchronizeAllAttributesExceptStyle() const;

  void UpdateId(const AtomicString& old_id, const AtomicString& new_id);
  void UpdateId(TreeScope&,
                const AtomicString& old_id,
                const AtomicString& new_id);
  void UpdateName(const AtomicString& old_name, const AtomicString& new_name);

  void UpdateFocusgroup(const AtomicString& input);
  void UpdateFocusgroupInShadowRootIfNeeded();

  void ClientQuads(Vector<gfx::QuadF>& quads) const;

  NodeType getNodeType() const final;
  bool ChildTypeAllowed(NodeType) const final;

  // Returns the attribute's index or `kNotFound` if not found.
  wtf_size_t FindAttributeIndex(const QualifiedName&);

  void SetAttributeInternal(wtf_size_t index,
                            const QualifiedName&,
                            const AtomicString& value,
                            AttributeModificationReason);
  void AppendAttributeInternal(const QualifiedName&,
                               const AtomicString& value,
                               AttributeModificationReason);
  void RemoveAttributeInternal(wtf_size_t index, AttributeModificationReason);
  SpecificTrustedType ExpectedTrustedTypeForAttribute(
      const QualifiedName&) const;

  // These Hinted versions of the functions are subtle hot path
  // optimizations designed to reduce the number of unnecessary AtomicString
  // creations, AtomicStringTable lookups, and LowerCaseIfNecessary calls.
  //
  // The `hint` is the result of a WeakLowercaseIfNecessary() call unless it is
  // known that the the incoming string already has the right case. Then
  // the `hint` can be constructed from calling AtomicString::Impl().
  const AtomicString& GetAttributeHinted(
      const AtomicString& name,
      WTF::AtomicStringTable::WeakResult hint) const;
  void RemoveAttributeHinted(const AtomicString& name,
                             WTF::AtomicStringTable::WeakResult hint);
  void SynchronizeAttributeHinted(
      const AtomicString& name,
      WTF::AtomicStringTable::WeakResult hint) const;
  void SetAttributeHinted(AtomicString name,
                          WTF::AtomicStringTable::WeakResult hint,
                          String value,
                          ExceptionState& = ASSERT_NO_EXCEPTION);
  void SetAttributeHinted(AtomicString name,
                          WTF::AtomicStringTable::WeakResult hint,
                          const V8TrustedType* trusted_string,
                          ExceptionState& exception_state);
  std::pair<wtf_size_t, const QualifiedName> LookupAttributeQNameHinted(
      AtomicString name,
      WTF::AtomicStringTable::WeakResult hint) const;

  void CancelSelectionAfterLayout();
  virtual int DefaultTabIndex() const;

  const ComputedStyle* VirtualEnsureComputedStyle(
      PseudoId pseudo_element_specifier = kPseudoIdNone,
      const AtomicString& pseudo_argument = g_null_atom) final {
    return EnsureComputedStyle(pseudo_element_specifier, pseudo_argument);
  }

  inline void UpdateCallbackSelectors(const ComputedStyle* old_style,
                                      const ComputedStyle* new_style);

  // Clone is private so that non-virtual CloneElementWithChildren and
  // CloneElementWithoutChildren are used instead.
  Node* Clone(Document&, CloneChildrenFlag) const override;
  virtual Element& CloneWithoutAttributesAndChildren(Document& factory) const;

  QualifiedName tag_name_;

  void UpdateNamedItemRegistration(NamedItemType,
                                   const AtomicString& old_name,
                                   const AtomicString& new_name);
  void UpdateIdNamedItemRegistration(NamedItemType,
                                     const AtomicString& old_name,
                                     const AtomicString& new_name);

  void CreateUniqueElementData();

  bool ShouldInvalidateDistributionWhenAttributeChanged(ShadowRoot&,
                                                        const QualifiedName&,
                                                        const AtomicString&);

  void SetInnerHTMLInternal(const String&,
                            bool include_shadow_roots,
                            ExceptionState&);

  ElementRareData* GetElementRareData() const;
  ElementRareData& EnsureElementRareData();

  void RemoveAttrNodeList();
  void DetachAllAttrNodesFromElement();
  void DetachAttrNodeFromElementWithValue(Attr*, const AtomicString& value);
  void DetachAttrNodeAtIndex(Attr*, wtf_size_t index);

  // Return whether this element type requires legacy layout.
  virtual bool TypeShouldForceLegacyLayout() const { return false; }

  // Return whether the computed style of this element causes need for legacy
  // layout.
  bool StyleShouldForceLegacyLayout() const {
    if (!HasRareData())
      return false;
    return StyleShouldForceLegacyLayoutInternal();
  }
  bool StyleShouldForceLegacyLayoutInternal() const;
  void SetStyleShouldForceLegacyLayout(bool force) {
    if (!force && !HasRareData())
      return;
    SetStyleShouldForceLegacyLayoutInternal(force);
  }
  FRIEND_TEST_ALL_PREFIXES(LayoutNGTextCombineTest, LegacyQuote);
  void SetStyleShouldForceLegacyLayoutInternal(bool);

  // Return whether this element needs legacy layout because of a child.
  bool ShouldForceLegacyLayoutForChild() const {
    if (!HasRareData())
      return false;
    return ShouldForceLegacyLayoutForChildInternal();
  }
  bool ShouldForceLegacyLayoutForChildInternal() const;
  void SetShouldForceLegacyLayoutForChild(bool force) {
    if (!force && !HasRareData())
      return;
    SetShouldForceLegacyLayoutForChildInternal(force);
  }
  void SetShouldForceLegacyLayoutForChildInternal(bool);

  // Update ForceLegacyLayout flags for this element, and for ancestors, if
  // necessary. We cannot establish a ForceLegacyLayout subtree at an arbitrary
  // element; it needs to be a block formatting context root.
  // Returns true if we need to reattach this element or any ancestor element.
  bool UpdateForceLegacyLayout(const ComputedStyle& new_style,
                               const ComputedStyle* old_style);

  // If this element requires legacy layout, and we can't tell for sure that it
  // is going to establish a new formatting context, we need to force legacy
  // layout on ancestors until we reach one that we're sure that will establish
  // a new formatting context. LayoutNG and legacy layout cannot cooperate
  // within a formatting context.
  // Returns true if we need to reattach this element or any ancestor element.
  bool ForceLegacyLayoutInFormattingContext(const ComputedStyle& new_style);

  // If this element requires legacy layout, and we're inside a fragmentation
  // context, we need to force legacy layout for the entire fragmentation
  // context. LayoutNG block fragmentation and legacy block fragmentation cannot
  // cooperate within a fragmentation context.
  // Returns true if we need to reattach this element or any ancestor element.
  bool ForceLegacyLayoutInFragmentationContext(const ComputedStyle& new_style);

  void SynchronizeContentAttributeAndElementReference(
      const QualifiedName& name);

  DisplayLockContext* GetDisplayLockContextFromRareData() const;

  void PseudoStateChanged(CSSSelector::PseudoType pseudo,
                          AffectedByPseudoStateChange&&);

  // Highlight pseudos inherit all properties from the corresponding highlight
  // in the parent, but virtually all existing content uses universal rules
  // like *::selection. To improve runtime and keep copy-on-write inheritance,
  // avoid recalc if neither parent nor child matched any non-universal rules.
  bool CanSkipRecalcForHighlightPseudos(const ComputedStyle& new_style) const;

  Member<ElementData> element_data_;
};

template <typename T>
bool IsElementOfType(const Node&);
template <>
inline bool IsElementOfType<const Element>(const Node& node) {
  return node.IsElementNode();
}
template <typename T>
inline bool IsElementOfType(const Element& element) {
  return IsElementOfType<T>(static_cast<const Node&>(element));
}
template <>
inline bool IsElementOfType<const Element>(const Element&) {
  return true;
}

template <>
struct DowncastTraits<Element> {
  static bool AllowFrom(const Node& node) { return node.IsElementNode(); }
};

inline bool IsDisabledFormControl(const Node* node) {
  auto* element = DynamicTo<Element>(node);
  return element && element->IsDisabledFormControl();
}

inline Element* Node::parentElement() const {
  return DynamicTo<Element>(parentNode());
}

inline bool Element::FastHasAttribute(const QualifiedName& name) const {
#if DCHECK_IS_ON()
  DCHECK(FastAttributeLookupAllowed(name))
      << TagQName().ToString().Utf8() << "/@" << name.ToString().Utf8();
#endif
  return GetElementData() &&
         GetElementData()->Attributes().FindIndex(name) != kNotFound;
}

inline const AtomicString& Element::FastGetAttribute(
    const QualifiedName& name) const {
#if DCHECK_IS_ON()
  DCHECK(FastAttributeLookupAllowed(name))
      << TagQName().ToString().Utf8() << "/@" << name.ToString().Utf8();
#endif
  if (GetElementData()) {
    if (const Attribute* attribute = GetElementData()->Attributes().Find(name))
      return attribute->Value();
  }
  return g_null_atom;
}

inline AttributeCollection Element::Attributes() const {
  if (!GetElementData())
    return AttributeCollection();
  SynchronizeAllAttributes();
  return GetElementData()->Attributes();
}

inline AttributeCollection Element::AttributesWithoutUpdate() const {
  if (!GetElementData())
    return AttributeCollection();
  return GetElementData()->Attributes();
}

inline AttributeCollection Element::AttributesWithoutStyleUpdate() const {
  if (!GetElementData())
    return AttributeCollection();
  SynchronizeAllAttributesExceptStyle();
  return GetElementData()->Attributes();
}

inline bool Element::hasAttributes() const {
  return !Attributes().IsEmpty();
}

inline const AtomicString& Element::IdForStyleResolution() const {
  DCHECK(HasID());
  return GetElementData()->IdForStyleResolution();
}

inline const AtomicString& Element::GetIdAttribute() const {
  return HasID() ? FastGetAttribute(html_names::kIdAttr) : g_null_atom;
}

inline const AtomicString& Element::GetNameAttribute() const {
  return HasName() ? FastGetAttribute(html_names::kNameAttr) : g_null_atom;
}

inline const AtomicString& Element::GetClassAttribute() const {
  if (!HasClass())
    return g_null_atom;
  if (IsSVGElement())
    return getAttribute(html_names::kClassAttr);
  return FastGetAttribute(html_names::kClassAttr);
}

inline void Element::SetIdAttribute(const AtomicString& value) {
  setAttribute(html_names::kIdAttr, value);
}

inline const SpaceSplitString& Element::ClassNames() const {
  DCHECK(HasClass());
  DCHECK(GetElementData());
  return GetElementData()->ClassNames();
}

inline bool Element::HasClassName(const AtomicString& class_name) const {
  return HasClass() && ClassNames().Contains(class_name);
}

inline bool Element::HasID() const {
  return GetElementData() && GetElementData()->HasID();
}

inline bool Element::HasClass() const {
  return GetElementData() && GetElementData()->HasClass();
}

inline UniqueElementData& Element::EnsureUniqueElementData() {
  if (!GetElementData() || !GetElementData()->IsUnique())
    CreateUniqueElementData();
  return To<UniqueElementData>(*element_data_);
}

inline const CSSPropertyValueSet* Element::PresentationAttributeStyle() {
  if (!GetElementData())
    return nullptr;
  if (GetElementData()->presentation_attribute_style_is_dirty())
    UpdatePresentationAttributeStyle();
  // Need to call elementData() again since updatePresentationAttributeStyle()
  // might swap it with a UniqueElementData.
  return GetElementData()->PresentationAttributeStyle();
}

inline void Element::SetTagNameForCreateElementNS(
    const QualifiedName& tag_name) {
  // We expect this method to be called only to reset the prefix.
  DCHECK_EQ(tag_name.LocalName(), tag_name_.LocalName());
  DCHECK_EQ(tag_name.NamespaceURI(), tag_name_.NamespaceURI());
  tag_name_ = tag_name;
}

inline bool IsShadowHost(const Node* node) {
  return node && node->GetShadowRoot();
}

inline bool IsShadowHost(const Node& node) {
  return node.GetShadowRoot();
}

inline bool IsShadowHost(const Element* element) {
  return element && element->GetShadowRoot();
}

inline bool IsShadowHost(const Element& element) {
  return element.GetShadowRoot();
}

inline bool IsAtShadowBoundary(const Element* element) {
  if (!element)
    return false;
  ContainerNode* parent_node = element->parentNode();
  return parent_node && parent_node->IsShadowRoot();
}

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_CORE_DOM_ELEMENT_H_
