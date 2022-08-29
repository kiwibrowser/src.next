// Copyright 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_DOM_POPUP_ANIMATION_FINISHED_EVENT_LISTENER_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_DOM_POPUP_ANIMATION_FINISHED_EVENT_LISTENER_H_

#include "third_party/blink/renderer/core/dom/events/native_event_listener.h"
#include "third_party/blink/renderer/platform/heap/collection_support/heap_hash_set.h"
#include "third_party/blink/renderer/platform/heap/collection_support/heap_vector.h"
#include "third_party/blink/renderer/platform/heap/garbage_collected.h"
#include "third_party/blink/renderer/platform/heap/member.h"
#include "third_party/blink/renderer/platform/heap/prefinalizer.h"
#include "third_party/blink/renderer/platform/heap/visitor.h"

namespace blink {

class Element;
class EventTarget;

// Helper class used to manage popup hide animations.
class PopupAnimationFinishedEventListener : public NativeEventListener {
  USING_PRE_FINALIZER(PopupAnimationFinishedEventListener, Dispose);

 public:
  explicit PopupAnimationFinishedEventListener(
      Member<Element>,
      HeapHashSet<Member<EventTarget>>&&);
  ~PopupAnimationFinishedEventListener() override;

  void Invoke(ExecutionContext*, Event*) override;

  bool IsFinished() const;
  void Dispose();

  void Trace(Visitor* visitor) const override;

 private:
  void RemoveEventListeners(EventTarget* animation) const;

  Member<Element> popup_element_;
  HeapHashSet<Member<EventTarget>> animations_;
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_CORE_DOM_POPUP_ANIMATION_FINISHED_EVENT_LISTENER_H_
