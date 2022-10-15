// Copyright 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_CSS_CONTAINER_SELECTOR_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_CSS_CONTAINER_SELECTOR_H_

#include "third_party/blink/renderer/core/core_export.h"
#include "third_party/blink/renderer/core/css/media_query_exp.h"
#include "third_party/blink/renderer/core/layout/geometry/axis.h"
#include "third_party/blink/renderer/platform/heap/collection_support/heap_hash_map.h"
#include "third_party/blink/renderer/platform/text/writing_mode.h"
#include "third_party/blink/renderer/platform/wtf/hash_functions.h"
#include "third_party/blink/renderer/platform/wtf/text/atomic_string_hash.h"

namespace blink {

class Element;

// Not to be confused with regular selectors. This refers to container
// selection by e.g. a given name, or by implicit container selection
// according to the queried features.
//
// https://drafts.csswg.org/css-contain-3/#container-rule
class CORE_EXPORT ContainerSelector {
 public:
  ContainerSelector() = default;
  explicit ContainerSelector(WTF::HashTableDeletedValueType) {
    WTF::HashTraits<AtomicString>::ConstructDeletedValue(
        name_, false /* zero_value */);
  }
  explicit ContainerSelector(AtomicString name) : name_(std::move(name)) {}
  explicit ContainerSelector(PhysicalAxes physical_axes)
      : physical_axes_(physical_axes) {}
  ContainerSelector(AtomicString name, LogicalAxes physical_axes)
      : name_(std::move(name)), logical_axes_(physical_axes) {}
  ContainerSelector(AtomicString name, const MediaQueryExpNode&);

  bool IsHashTableDeletedValue() const {
    return WTF::HashTraits<AtomicString>::IsDeletedValue(name_);
  }

  bool operator==(const ContainerSelector& o) const {
    return (name_ == o.name_) && (physical_axes_ == o.physical_axes_) &&
           (logical_axes_ == o.logical_axes_) &&
           (has_style_query_ == o.has_style_query_);
  }
  bool operator!=(const ContainerSelector& o) const { return !(*this == o); }

  unsigned GetHash() const;

  const AtomicString& Name() const { return name_; }

  // Given the specified writing mode, return the EContainerTypes required
  // for this selector to match.
  unsigned Type(WritingMode) const;

  bool SelectsSizeContainers() const {
    return physical_axes_ != kPhysicalAxisNone ||
           logical_axes_ != kLogicalAxisNone;
  }

  bool SelectsStyleContainers() const { return has_style_query_; }

 private:
  AtomicString name_;
  PhysicalAxes physical_axes_{kPhysicalAxisNone};
  LogicalAxes logical_axes_{kLogicalAxisNone};
  bool has_style_query_{false};
};

}  // namespace blink

namespace WTF {

template <>
struct DefaultHash<blink::ContainerSelector> {
  struct Hash {
    STATIC_ONLY(Hash);
    static unsigned GetHash(const blink::ContainerSelector& selector) {
      return selector.GetHash();
    }

    static bool Equal(const blink::ContainerSelector& a,
                      const blink::ContainerSelector& b) {
      return a == b;
    }

    static const bool safe_to_compare_to_empty_or_deleted =
        DefaultHash<AtomicString>::Hash::safe_to_compare_to_empty_or_deleted;
  };
};

template <>
struct HashTraits<blink::ContainerSelector>
    : SimpleClassHashTraits<blink::ContainerSelector> {
  static const bool kEmptyValueIsZero = false;
  static const bool kNeedsDestruction = true;
};

}  // namespace WTF

namespace blink {

using ContainerSelectorCache = HeapHashMap<ContainerSelector, Member<Element>>;

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_CORE_CSS_CONTAINER_SELECTOR_H_
