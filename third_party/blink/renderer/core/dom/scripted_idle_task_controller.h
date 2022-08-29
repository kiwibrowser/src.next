// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_DOM_SCRIPTED_IDLE_TASK_CONTROLLER_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_DOM_SCRIPTED_IDLE_TASK_CONTROLLER_H_

#include "third_party/blink/renderer/bindings/core/v8/v8_idle_request_callback.h"
#include "third_party/blink/renderer/core/core_export.h"
#include "third_party/blink/renderer/core/dom/idle_deadline.h"
#include "third_party/blink/renderer/core/execution_context/execution_context_lifecycle_state_observer.h"
#include "third_party/blink/renderer/core/probe/async_task_context.h"
#include "third_party/blink/renderer/platform/bindings/name_client.h"
#include "third_party/blink/renderer/platform/heap/collection_support/heap_hash_map.h"
#include "third_party/blink/renderer/platform/heap/garbage_collected.h"
#include "third_party/blink/renderer/platform/timer.h"
#include "third_party/blink/renderer/platform/wtf/vector.h"

namespace blink {
namespace internal {
class IdleRequestCallbackWrapper;
}

class ExecutionContext;
class IdleRequestOptions;
class ThreadScheduler;

// |IdleTask| is an interface type which generalizes tasks which are invoked
// on idle. The tasks need to define what to do on idle in |invoke|.
class IdleTask : public GarbageCollected<IdleTask>, public NameClient {
 public:
  virtual void Trace(Visitor* visitor) const {}
  const char* NameInHeapSnapshot() const override { return "IdleTask"; }
  ~IdleTask() override = default;
  virtual void invoke(IdleDeadline*) = 0;
  probe::AsyncTaskContext* async_task_context() { return &async_task_context_; }

 private:
  probe::AsyncTaskContext async_task_context_;
};

// |V8IdleTask| is the adapter class for the conversion from
// |V8IdleRequestCallback| to |IdleTask|.
class V8IdleTask : public IdleTask {
 public:
  static V8IdleTask* Create(V8IdleRequestCallback* callback) {
    return MakeGarbageCollected<V8IdleTask>(callback);
  }

  explicit V8IdleTask(V8IdleRequestCallback*);
  ~V8IdleTask() override = default;

  void invoke(IdleDeadline*) override;
  void Trace(Visitor*) const override;

 private:
  Member<V8IdleRequestCallback> callback_;
};

class CORE_EXPORT ScriptedIdleTaskController
    : public GarbageCollected<ScriptedIdleTaskController>,
      public ExecutionContextLifecycleStateObserver,
      public NameClient {
 public:
  static ScriptedIdleTaskController* Create(ExecutionContext* context) {
    ScriptedIdleTaskController* controller =
        MakeGarbageCollected<ScriptedIdleTaskController>(context);
    controller->UpdateStateIfNeeded();
    return controller;
  }

  explicit ScriptedIdleTaskController(ExecutionContext*);
  ~ScriptedIdleTaskController() override;

  void Trace(Visitor*) const override;
  const char* NameInHeapSnapshot() const override {
    return "ScriptedIdleTaskController";
  }

  using CallbackId = int;

  int RegisterCallback(IdleTask*, const IdleRequestOptions*);
  void CancelCallback(CallbackId);

  // ExecutionContextLifecycleStateObserver interface.
  void ContextDestroyed() override;
  void ContextLifecycleStateChanged(mojom::FrameLifecycleState) override;

  void CallbackFired(CallbackId,
                     base::TimeTicks deadline,
                     IdleDeadline::CallbackType);

 private:
  friend class internal::IdleRequestCallbackWrapper;

  void ContextPaused();
  void ContextUnpaused();
  void ScheduleCallback(scoped_refptr<internal::IdleRequestCallbackWrapper>,
                        uint32_t timeout_millis);

  int NextCallbackId();

  bool IsValidCallbackId(int id) {
    using Traits = HashTraits<CallbackId>;
    return !Traits::IsDeletedValue(id) &&
           !WTF::IsHashTraitsEmptyValue<Traits, CallbackId>(id);
  }

  void RunCallback(CallbackId,
                   base::TimeTicks deadline,
                   IdleDeadline::CallbackType);

  ThreadScheduler* scheduler_;  // Not owned.
  HeapHashMap<CallbackId, Member<IdleTask>> idle_tasks_;
  Vector<CallbackId> pending_timeouts_;
  CallbackId next_callback_id_;
  bool paused_;
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_CORE_DOM_SCRIPTED_IDLE_TASK_CONTROLLER_H_
