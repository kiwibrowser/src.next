// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_CSS_CONTAINER_QUERY_SCROLL_SNAPSHOT_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_CSS_CONTAINER_QUERY_SCROLL_SNAPSHOT_H_

#include "third_party/blink/renderer/core/css/container_state.h"
#include "third_party/blink/renderer/core/scroll/scroll_snapshot_client.h"
#include "third_party/blink/renderer/platform/heap/member.h"

namespace blink {

class Element;

// Created for each container-type:sticky element. Stores a snapshot of whether
// the sticky container is stuck or not by reading the sticky offset from the
// layout object. The snapshot state is used to update the ContainerValues for
// the query container so that @container queries with state(stuck: ...)
// evaluate correctly on the subsequent style update.
class ContainerQueryScrollSnapshot
    : public GarbageCollected<ContainerQueryScrollSnapshot>,
      public ScrollSnapshotClient {
 public:
  explicit ContainerQueryScrollSnapshot(Element& container);

  ContainerStuckPhysical StuckHorizontal() const { return stuck_horizontal_; }
  ContainerStuckPhysical StuckVertical() const { return stuck_vertical_; }

  // ScrollSnapshotClient:
  void UpdateSnapshot() override;
  bool ValidateSnapshot() override;
  bool ShouldScheduleNextService() override;

  void Trace(Visitor* visitor) const override;

 private:
  bool UpdateStuckState();

  Member<Element> container_;
  ContainerStuckPhysical stuck_horizontal_ = ContainerStuckPhysical::kNo;
  ContainerStuckPhysical stuck_vertical_ = ContainerStuckPhysical::kNo;
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_CORE_CSS_CONTAINER_QUERY_SCROLL_SNAPSHOT_H_
