// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_CSS_CONTAINER_QUERY_DATA_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_CSS_CONTAINER_QUERY_DATA_H_

#include "third_party/abseil-cpp/absl/types/optional.h"
#include "third_party/blink/renderer/core/css/style_recalc_change.h"
#include "third_party/blink/renderer/platform/heap/garbage_collected.h"
#include "third_party/blink/renderer/platform/heap/member.h"

namespace blink {

class ContainerQueryEvaluator;

// Class for storing Container Query data on ElementRareData.
class ContainerQueryData final : public GarbageCollected<ContainerQueryData> {
 public:
  StyleRecalcChange ClearAndReturnRecalcChangeForChildren() {
    DCHECK(child_change_.has_value());
    StyleRecalcChange change = child_change_.value();
    child_change_.reset();
    return change;
  }

  // Called when the container's subtree is skipped for style recalc to be
  // resumed during layout.
  void SkipStyleRecalc(StyleRecalcChange child_change) {
    DCHECK(!child_change_.has_value());
    child_change_ = child_change;
  }

  bool SkippedStyleRecalc() const { return child_change_.has_value(); }

  ContainerQueryEvaluator* GetContainerQueryEvaluator() const {
    return container_query_evaluator_;
  }
  void SetContainerQueryEvaluator(ContainerQueryEvaluator* evaluator) {
    container_query_evaluator_ = evaluator;
  }

  void Trace(Visitor*) const;

 private:
  Member<ContainerQueryEvaluator> container_query_evaluator_;

  // When the style recalc stopped at a container, the StyleRecalcChange which
  // would have been passed on to the children is stored here so that it can be
  // used when resuming the style recalc during layout.
  absl::optional<StyleRecalcChange> child_change_;
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_CORE_CSS_CONTAINER_QUERY_DATA_H_
