/*
 * Copyright (C) 2012 Google Inc. All rights reserved.
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

#include "third_party/blink/renderer/core/dom/element_rare_data.h"

#include "third_party/blink/renderer/core/accessibility/ax_object_cache.h"
#include "third_party/blink/renderer/core/css/container_query_data.h"
#include "third_party/blink/renderer/core/css/cssom/inline_style_property_map.h"
#include "third_party/blink/renderer/core/editing/ime/edit_context.h"
#include "third_party/blink/renderer/core/html/custom/element_internals.h"
#include "third_party/blink/renderer/core/resize_observer/resize_observation.h"
#include "third_party/blink/renderer/core/resize_observer/resize_observer.h"
#include "third_party/blink/renderer/core/style/computed_style.h"
#include "third_party/blink/renderer/platform/wtf/size_assertions.h"

namespace blink {

struct SameSizeAsElementRareData : NodeRareData {
  Member<void*> members[10];
  float scroll_offset[2];
  bool flags[1];
};

ElementRareData::ElementRareData(NodeRenderingData* node_layout_data)
    : NodeRareData(ClassType::kElementRareData, node_layout_data),
      class_list_(nullptr),
      did_attach_internals_(false),
      should_force_legacy_layout_for_child_(false),
      style_should_force_legacy_layout_(false),
      has_undo_stack_(false),
      scrollbar_pseudo_element_styles_depend_on_font_metrics_(false) {
  // When The ElementSuperRareData flag is disabled, then always initialize
  // ElementSuperRareData immediately in order to measure the memory usage
  // improvements.
  if (!RuntimeEnabledFeatures::ElementSuperRareDataEnabled())
    super_rare_data_ = MakeGarbageCollected<ElementSuperRareData>();
}

ElementRareData::~ElementRareData() {
  DCHECK(!pseudo_element_data_);
}

CSSStyleDeclaration& ElementRareData::EnsureInlineCSSStyleDeclaration(
    Element* owner_element) {
  if (!cssom_wrapper_) {
    cssom_wrapper_ =
        MakeGarbageCollected<InlineCSSStyleDeclaration>(owner_element);
  }
  return *cssom_wrapper_;
}

InlineStylePropertyMap& ElementSuperRareData::EnsureInlineStylePropertyMap(
    Element* owner_element) {
  if (!cssom_map_wrapper_) {
    cssom_map_wrapper_ =
        MakeGarbageCollected<InlineStylePropertyMap>(owner_element);
  }
  return *cssom_map_wrapper_;
}
InlineStylePropertyMap& ElementRareData::EnsureInlineStylePropertyMap(
    Element* owner_element) {
  return EnsureSuperRareData().EnsureInlineStylePropertyMap(owner_element);
}

AttrNodeList& ElementRareData::EnsureAttrNodeList() {
  if (!attr_node_list_)
    attr_node_list_ = MakeGarbageCollected<AttrNodeList>();
  return *attr_node_list_;
}

ElementSuperRareData::ResizeObserverDataMap&
ElementSuperRareData::EnsureResizeObserverData() {
  if (!resize_observer_data_) {
    resize_observer_data_ = MakeGarbageCollected<
        HeapHashMap<Member<ResizeObserver>, Member<ResizeObservation>>>();
  }
  return *resize_observer_data_;
}

ElementRareData::ResizeObserverDataMap&
ElementRareData::EnsureResizeObserverData() {
  return EnsureSuperRareData().EnsureResizeObserverData();
}

PopupData& ElementSuperRareData::EnsurePopupData() {
  if (!popup_data_)
    popup_data_ = MakeGarbageCollected<PopupData>();
  return *popup_data_;
}

PopupData& ElementRareData::EnsurePopupData() {
  return EnsureSuperRareData().EnsurePopupData();
}

void ElementSuperRareData::RemovePopupData() {
  popup_data_.Clear();
}

void ElementRareData::RemovePopupData() {
  if (super_rare_data_)
    super_rare_data_->RemovePopupData();
}

ElementInternals& ElementSuperRareData::EnsureElementInternals(
    HTMLElement& target) {
  if (element_internals_)
    return *element_internals_;
  element_internals_ = MakeGarbageCollected<ElementInternals>(target);
  return *element_internals_;
}

void ElementRareData::TraceAfterDispatch(blink::Visitor* visitor) const {
  visitor->Trace(super_rare_data_);
  visitor->Trace(dataset_);
  visitor->Trace(shadow_root_);
  visitor->Trace(class_list_);
  visitor->Trace(attribute_map_);
  visitor->Trace(attr_node_list_);
  visitor->Trace(cssom_wrapper_);
  visitor->Trace(element_animations_);
  visitor->Trace(intersection_observer_data_);
  visitor->Trace(pseudo_element_data_);
  NodeRareData::TraceAfterDispatch(visitor);
}

void ElementSuperRareData::Trace(blink::Visitor* visitor) const {
  visitor->Trace(edit_context_);
  visitor->Trace(part_);
  visitor->Trace(cssom_map_wrapper_);
  visitor->Trace(element_internals_);
  visitor->Trace(accessible_node_);
  visitor->Trace(display_lock_context_);
  visitor->Trace(container_query_data_);
  visitor->Trace(resize_observer_data_);
  visitor->Trace(custom_element_definition_);
  visitor->Trace(last_intrinsic_size_);
  visitor->Trace(popup_data_);
}

ASSERT_SIZE(ElementRareData, SameSizeAsElementRareData);

}  // namespace blink
