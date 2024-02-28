// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_LAYOUT_ANCHOR_QUERY_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_LAYOUT_ANCHOR_QUERY_H_

#include "third_party/blink/renderer/core/core_export.h"

#include "third_party/abseil-cpp/absl/types/optional.h"
#include "third_party/abseil-cpp/absl/types/variant.h"
#include "third_party/blink/renderer/core/core_export.h"
#include "third_party/blink/renderer/core/css/css_anchor_query_enums.h"
#include "third_party/blink/renderer/core/layout/geometry/physical_rect.h"
#include "third_party/blink/renderer/core/layout/geometry/writing_mode_converter.h"
#include "third_party/blink/renderer/core/style/scoped_css_name.h"
#include "third_party/blink/renderer/platform/geometry/length.h"
#include "third_party/blink/renderer/platform/heap/collection_support/heap_hash_map.h"
#include "third_party/blink/renderer/platform/heap/collection_support/heap_hash_set.h"
#include "third_party/blink/renderer/platform/wtf/text/atomic_string.h"
#include "third_party/blink/renderer/platform/wtf/text/atomic_string_hash.h"

namespace blink {

class AnchorSpecifierValue;
class LayoutObject;
class LogicalAnchorQuery;
class LogicalAnchorQueryMap;
class PaintLayer;
struct LogicalAnchorReference;

using AnchorKey = absl::variant<const ScopedCSSName*, const LayoutObject*>;

// This class is conceptually a concatenation of two hash maps with different
// key types but the same value type. To save memory, we don't implement it as
// one hash map with a unified key type; Otherwise, the size of each key will be
// increased by at least one pointer, which is undesired.
template <typename AnchorReference>
class AnchorQueryBase : public GarbageCollectedMixin {
  using NamedAnchorMap =
      HeapHashMap<Member<const ScopedCSSName>, Member<AnchorReference>>;
  using ImplicitAnchorMap =
      HeapHashMap<Member<const LayoutObject>, Member<AnchorReference>>;

 public:
  bool IsEmpty() const {
    return named_anchors_.empty() && implicit_anchors_.empty();
  }

  const AnchorReference* GetAnchorReference(const AnchorKey& key) const {
    if (const ScopedCSSName* const* name =
            absl::get_if<const ScopedCSSName*>(&key)) {
      return GetAnchorReference(named_anchors_, *name);
    }
    return GetAnchorReference(implicit_anchors_,
                              absl::get<const LayoutObject*>(key));
  }

  struct AddResult {
    Member<AnchorReference>* stored_value;
    bool is_new_entry;
    STACK_ALLOCATED();
  };
  AddResult insert(const AnchorKey& key, AnchorReference* reference) {
    if (const ScopedCSSName* const* name =
            absl::get_if<const ScopedCSSName*>(&key)) {
      return insert(named_anchors_, *name, reference);
    }
    return insert(implicit_anchors_, absl::get<const LayoutObject*>(key),
                  reference);
  }

  class Iterator {
    STACK_ALLOCATED();

    using NamedAnchorMap = typename AnchorQueryBase::NamedAnchorMap;
    using ImplicitAnchorMap = typename AnchorQueryBase::ImplicitAnchorMap;

   public:
    Iterator(const AnchorQueryBase* anchor_query,
             typename NamedAnchorMap::const_iterator named_map_iterator,
             typename ImplicitAnchorMap::const_iterator implicit_map_iterator)
        : anchor_query_(anchor_query),
          named_map_iterator_(named_map_iterator),
          implicit_map_iterator_(implicit_map_iterator) {}

    struct Entry {
      AnchorKey key;
      AnchorReference* value;
      STACK_ALLOCATED();
    };
    Entry operator*() const {
      if (named_map_iterator_ != anchor_query_->named_anchors_.end())
        return Entry{named_map_iterator_->key, named_map_iterator_->value};
      return Entry{implicit_map_iterator_->key, implicit_map_iterator_->value};
    }

    bool operator==(const Iterator& other) const {
      return named_map_iterator_ == other.named_map_iterator_ &&
             implicit_map_iterator_ == other.implicit_map_iterator_;
    }
    bool operator!=(const Iterator& other) const { return !operator==(other); }

    Iterator& operator++() {
      if (named_map_iterator_ != anchor_query_->named_anchors_.end())
        ++named_map_iterator_;
      else
        ++implicit_map_iterator_;
      return *this;
    }

   private:
    const AnchorQueryBase* anchor_query_;
    typename NamedAnchorMap::const_iterator named_map_iterator_;
    typename ImplicitAnchorMap::const_iterator implicit_map_iterator_;
  };
  Iterator begin() const {
    return Iterator{this, named_anchors_.begin(), implicit_anchors_.begin()};
  }
  Iterator end() const {
    return Iterator{this, named_anchors_.end(), implicit_anchors_.end()};
  }

  void Trace(Visitor* visitor) const override {
    visitor->Trace(named_anchors_);
    visitor->Trace(implicit_anchors_);
  }

 private:
  friend class Iterator;

  template <typename AnchorMapType, typename KeyType>
  static const AnchorReference* GetAnchorReference(const AnchorMapType& anchors,
                                                   const KeyType& key) {
    auto it = anchors.find(key);
    return it != anchors.end() ? it->value : nullptr;
  }

  template <typename AnchorMapType, typename KeyType>
  static AddResult insert(AnchorMapType& anchors,
                          const KeyType& key,
                          AnchorReference* reference) {
    auto result = anchors.insert(key, reference);
    return AddResult{&result.stored_value->value, result.is_new_entry};
  }

  NamedAnchorMap named_anchors_;
  ImplicitAnchorMap implicit_anchors_;
};

struct CORE_EXPORT PhysicalAnchorReference
    : public GarbageCollected<PhysicalAnchorReference> {
  PhysicalAnchorReference(const LogicalAnchorReference& logical_reference,
                          const WritingModeConverter& converter);

  void Trace(Visitor* visitor) const;

  PhysicalRect rect;
  Member<const LayoutObject> layout_object;
  // A singly linked list in the reverse tree order. There can be at most one
  // in-flow reference, which if exists must be at the end of the list.
  Member<PhysicalAnchorReference> next;
  bool is_out_of_flow = false;
};

class CORE_EXPORT PhysicalAnchorQuery
    : public AnchorQueryBase<PhysicalAnchorReference> {
  DISALLOW_NEW();

 public:
  using Base = AnchorQueryBase<PhysicalAnchorReference>;

  const PhysicalAnchorReference* AnchorReference(
      const LayoutObject& query_object,
      const AnchorKey&) const;
  const LayoutObject* AnchorLayoutObject(const LayoutObject& query_object,
                                         const AnchorKey&) const;

  void SetFromLogical(const LogicalAnchorQuery& logical_query,
                      const WritingModeConverter& converter);
};

struct CORE_EXPORT LogicalAnchorReference
    : public GarbageCollected<LogicalAnchorReference> {
  LogicalAnchorReference(const LayoutObject& layout_object,
                         const LogicalRect& rect,
                         bool is_out_of_flow)
      : rect(rect),
        layout_object(&layout_object),
        is_out_of_flow(is_out_of_flow) {}

  // Insert |this| into the given singly linked list in the reverse tree order.
  void InsertInReverseTreeOrderInto(Member<LogicalAnchorReference>* head_ptr);

  void Trace(Visitor* visitor) const;

  LogicalRect rect;
  Member<const LayoutObject> layout_object;
  // A singly linked list in the reverse tree order. There can be at most one
  // in-flow reference, which if exists must be at the end of the list.
  Member<LogicalAnchorReference> next;
  bool is_out_of_flow = false;
};

class CORE_EXPORT LogicalAnchorQuery
    : public GarbageCollected<LogicalAnchorQuery>,
      public AnchorQueryBase<LogicalAnchorReference> {
 public:
  using Base = AnchorQueryBase<LogicalAnchorReference>;

  // Returns an empty instance.
  static const LogicalAnchorQuery& Empty();

  const LogicalAnchorReference* AnchorReference(
      const LayoutObject& query_object,
      const AnchorKey&) const;

  enum class SetOptions {
    // An in-flow entry.
    kInFlow,
    // An out-of-flow entry.
    kOutOfFlow,
  };
  void Set(const AnchorKey&,
           const LayoutObject& layout_object,
           const LogicalRect& rect,
           SetOptions);
  void Set(const AnchorKey&, LogicalAnchorReference* reference);
  void SetFromPhysical(const PhysicalAnchorQuery& physical_query,
                       const WritingModeConverter& converter,
                       const LogicalOffset& additional_offset,
                       SetOptions);

  // Evaluate the |anchor_value| for the given reference. Returns |nullopt| if
  // the query is invalid (due to wrong axis).
  absl::optional<LayoutUnit> EvaluateAnchor(
      const LogicalAnchorReference& reference,
      CSSAnchorValue anchor_value,
      float percentage,
      LayoutUnit available_size,
      const WritingModeConverter& container_converter,
      WritingDirectionMode self_writing_direction,
      const PhysicalOffset& offset_to_padding_box,
      bool is_y_axis,
      bool is_right_or_bottom) const;
  LayoutUnit EvaluateSize(const LogicalAnchorReference& reference,
                          CSSAnchorSizeValue anchor_size_value,
                          WritingMode container_writing_mode,
                          WritingMode self_writing_mode) const;
};

class CORE_EXPORT AnchorEvaluatorImpl : public Length::AnchorEvaluator {
  STACK_ALLOCATED();

 public:
  // An empty evaluator that always return `nullopt`. This instance can still
  // compute `HasAnchorFunctions()`.
  AnchorEvaluatorImpl() = default;

  AnchorEvaluatorImpl(const LayoutObject& query_object,
                      const LogicalAnchorQuery& anchor_query,
                      const ScopedCSSName* default_anchor_specifier,
                      const LayoutObject* implicit_anchor,
                      const WritingModeConverter& container_converter,
                      WritingDirectionMode self_writing_direction,
                      const PhysicalOffset& offset_to_padding_box)
      : query_object_(&query_object),
        anchor_query_(&anchor_query),
        default_anchor_specifier_(default_anchor_specifier),
        implicit_anchor_(implicit_anchor),
        container_converter_(container_converter),
        self_writing_direction_(self_writing_direction),
        offset_to_padding_box_(offset_to_padding_box) {
    DCHECK(anchor_query_);
  }

  // This constructor takes |LogicalAnchorQueryMap| and |containing_block|
  // instead of |LogicalAnchorQuery|.
  AnchorEvaluatorImpl(const LayoutObject& query_object,
                      const LogicalAnchorQueryMap& anchor_queries,
                      const ScopedCSSName* default_anchor_specifier,
                      const LayoutObject* implicit_anchor,
                      const LayoutObject& containing_block,
                      const WritingModeConverter& container_converter,
                      WritingDirectionMode self_writing_direction,
                      const PhysicalOffset& offset_to_padding_box)
      : query_object_(&query_object),
        anchor_queries_(&anchor_queries),
        default_anchor_specifier_(default_anchor_specifier),
        implicit_anchor_(implicit_anchor),
        containing_block_(&containing_block),
        container_converter_(container_converter),
        self_writing_direction_(self_writing_direction),
        offset_to_padding_box_(offset_to_padding_box) {
    DCHECK(anchor_queries_);
    DCHECK(containing_block_);
  }

  // Returns true if this evaluator was invoked for `anchor()` or
  // `anchor-size()` functions.
  bool HasAnchorFunctions() const { return has_anchor_functions_; }

  // Returns true if any anchor reference in the axis is in the same scroll
  // container as the default anchor, in which case we need scroll adjustment in
  // the axis after layout.
  bool NeedsScrollAdjustmentInX() const {
    return needs_scroll_adjustment_in_x_;
  }
  bool NeedsScrollAdjustmentInY() const {
    return needs_scroll_adjustment_in_y_;
  }

  // This must be set before evaluating `anchor()` function.
  void SetAxis(bool is_y_axis,
               bool is_right_or_bottom,
               LayoutUnit available_size) {
    available_size_ = available_size;
    is_y_axis_ = is_y_axis;
    is_right_or_bottom_ = is_right_or_bottom;
  }

  // Evaluates the given anchor query. Returns nullopt if the query invalid
  // (e.g., no target or wrong axis).
  absl::optional<LayoutUnit> Evaluate(
      const CalculationExpressionNode&) const override;

  // Finds the rect of the element referenced by the `position-fallback-bounds`
  // property, or nullopt if there's no such element.
  absl::optional<LogicalRect> GetAdditionalFallbackBoundsRect() const;

  bool HasDefaultAnchor() const { return DefaultAnchor() != nullptr; }

 private:
  const LogicalAnchorQuery* AnchorQuery() const;
  const LogicalAnchorReference* ResolveAnchorReference(
      const AnchorSpecifierValue& anchor_specifier) const;
  bool ShouldUseScrollAdjustmentFor(const LayoutObject* anchor) const;

  absl::optional<LayoutUnit> EvaluateAnchor(
      const AnchorSpecifierValue& anchor_specifier,
      CSSAnchorValue anchor_value,
      float percentage) const;
  absl::optional<LayoutUnit> EvaluateAnchorSize(
      const AnchorSpecifierValue& anchor_specifier,
      CSSAnchorSizeValue anchor_size_value) const;

  const LayoutObject* DefaultAnchor() const;
  const PaintLayer* DefaultAnchorScrollContainerLayer() const;

  const LayoutObject* query_object_ = nullptr;
  mutable const LogicalAnchorQuery* anchor_query_ = nullptr;
  const LogicalAnchorQueryMap* anchor_queries_ = nullptr;
  const ScopedCSSName* default_anchor_specifier_ = nullptr;
  const LayoutObject* implicit_anchor_ = nullptr;
  const LayoutObject* containing_block_ = nullptr;
  const WritingModeConverter container_converter_{
      {WritingMode::kHorizontalTb, TextDirection::kLtr}};
  WritingDirectionMode self_writing_direction_{WritingMode::kHorizontalTb,
                                               TextDirection::kLtr};

  PhysicalOffset offset_to_padding_box_;
  LayoutUnit available_size_;

  // These fields will be populated during `anchor()` evaluation if needed.
  mutable absl::optional<const LayoutObject*> default_anchor_;
  mutable absl::optional<const PaintLayer*>
      default_anchor_scroll_container_layer_;

  bool is_y_axis_ = false;
  bool is_right_or_bottom_ = false;
  mutable bool has_anchor_functions_ = false;
  mutable bool needs_scroll_adjustment_in_x_ = false;
  mutable bool needs_scroll_adjustment_in_y_ = false;
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_CORE_LAYOUT_ANCHOR_QUERY_H_
