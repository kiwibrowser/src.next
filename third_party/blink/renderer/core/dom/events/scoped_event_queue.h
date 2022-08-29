/*
 * Copyright (C) 2010 Google Inc. All rights reserved.
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

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_DOM_EVENTS_SCOPED_EVENT_QUEUE_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_DOM_EVENTS_SCOPED_EVENT_QUEUE_H_

#include "base/memory/scoped_refptr.h"
#include "third_party/blink/renderer/core/core_export.h"
#include "third_party/blink/renderer/core/dom/events/event.h"
#include "third_party/blink/renderer/platform/heap/collection_support/heap_vector.h"
#include "third_party/blink/renderer/platform/heap/persistent.h"
#include "third_party/blink/renderer/platform/wtf/vector.h"

namespace blink {

class CORE_EXPORT ScopedEventQueue {
  USING_FAST_MALLOC(ScopedEventQueue);

 public:
  ScopedEventQueue(const ScopedEventQueue&) = delete;
  ScopedEventQueue& operator=(const ScopedEventQueue&) = delete;
  ~ScopedEventQueue();

  void EnqueueEvent(Event&);
  static ScopedEventQueue* Instance();

  void IncrementScopingLevel();
  void DecrementScopingLevel();
  bool ShouldQueueEvents() const { return scoping_level_ > 0; }

 private:
  ScopedEventQueue();
  static void Initialize();
  void DispatchAllEvents();
  void DispatchEvent(Event&) const;

  Persistent<HeapVector<Member<Event>>> queued_events_;
  unsigned scoping_level_;

  static ScopedEventQueue* instance_;
};

class EventQueueScope {
  STACK_ALLOCATED();

 public:
  EventQueueScope() { ScopedEventQueue::Instance()->IncrementScopingLevel(); }
  EventQueueScope(const EventQueueScope&) = delete;
  EventQueueScope& operator=(const EventQueueScope&) = delete;
  ~EventQueueScope() { ScopedEventQueue::Instance()->DecrementScopingLevel(); }
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_CORE_DOM_EVENTS_SCOPED_EVENT_QUEUE_H_
