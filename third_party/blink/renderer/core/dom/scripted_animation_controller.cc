/*
 * Copyright (C) 2011 Google Inc. All Rights Reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 *  THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include "third_party/blink/renderer/core/dom/scripted_animation_controller.h"

#include "third_party/blink/public/mojom/frame/lifecycle.mojom-blink.h"
#include "third_party/blink/renderer/core/css/media_query_list_listener.h"
#include "third_party/blink/renderer/core/dom/document.h"
#include "third_party/blink/renderer/core/dom/events/event.h"
#include "third_party/blink/renderer/core/event_interface_names.h"
#include "third_party/blink/renderer/core/frame/local_dom_window.h"
#include "third_party/blink/renderer/core/frame/local_frame_view.h"
#include "third_party/blink/renderer/core/inspector/inspector_trace_events.h"
#include "third_party/blink/renderer/core/loader/document_loader.h"
#include "third_party/blink/renderer/core/page/page.h"
#include "third_party/blink/renderer/core/page/page_animator.h"
#include "third_party/blink/renderer/core/probe/core_probes.h"

namespace blink {

bool ScriptedAnimationController::InsertToPerFrameEventsMap(
    const Event* event) {
  HashSet<const StringImpl*>& set =
      per_frame_events_.insert(event->target(), HashSet<const StringImpl*>())
          .stored_value->value;
  return set.insert(event->type().Impl()).is_new_entry;
}

void ScriptedAnimationController::EraseFromPerFrameEventsMap(
    const Event* event) {
  EventTarget* target = event->target();
  PerFrameEventsMap::iterator it = per_frame_events_.find(target);
  if (it != per_frame_events_.end()) {
    HashSet<const StringImpl*>& set = it->value;
    set.erase(event->type().Impl());
    if (set.IsEmpty())
      per_frame_events_.erase(target);
  }
}

ScriptedAnimationController::ScriptedAnimationController(LocalDOMWindow* window)
    : ExecutionContextLifecycleStateObserver(window),
      callback_collection_(window) {
  UpdateStateIfNeeded();
}

void ScriptedAnimationController::Trace(Visitor* visitor) const {
  ExecutionContextLifecycleStateObserver::Trace(visitor);
  visitor->Trace(callback_collection_);
  visitor->Trace(event_queue_);
  visitor->Trace(media_query_list_listeners_);
  visitor->Trace(media_query_list_listeners_set_);
  visitor->Trace(per_frame_events_);
}

void ScriptedAnimationController::ContextLifecycleStateChanged(
    mojom::FrameLifecycleState state) {
  if (state == mojom::FrameLifecycleState::kRunning)
    ScheduleAnimationIfNeeded();
}

void ScriptedAnimationController::DispatchEventsAndCallbacksForPrinting() {
  DispatchEvents([](const Event* event) {
    return event->InterfaceName() ==
           event_interface_names::kMediaQueryListEvent;
  });
  CallMediaQueryListListeners();
}

void ScriptedAnimationController::ScheduleVideoFrameCallbacksExecution(
    ExecuteVfcCallback execute_vfc_callback) {
  vfc_execution_queue_.push_back(std::move(execute_vfc_callback));
  ScheduleAnimationIfNeeded();
}

ScriptedAnimationController::CallbackId
ScriptedAnimationController::RegisterFrameCallback(FrameCallback* callback) {
  CallbackId id = callback_collection_.RegisterFrameCallback(callback);
  ScheduleAnimationIfNeeded();
  return id;
}

void ScriptedAnimationController::CancelFrameCallback(CallbackId id) {
  callback_collection_.CancelFrameCallback(id);
}

bool ScriptedAnimationController::HasFrameCallback() const {
  return callback_collection_.HasFrameCallback() ||
         !vfc_execution_queue_.IsEmpty();
}

void ScriptedAnimationController::RunTasks() {
  Vector<base::OnceClosure> tasks;
  tasks.swap(task_queue_);
  for (auto& task : tasks)
    std::move(task).Run();
}

void ScriptedAnimationController::DispatchEvents(const DispatchFilter& filter) {
  HeapVector<Member<Event>> events;
  if (!filter.has_value()) {
    events.swap(event_queue_);
    per_frame_events_.clear();
  } else {
    HeapVector<Member<Event>> remaining;
    for (auto& event : event_queue_) {
      if (event && filter.value()(event)) {
        EraseFromPerFrameEventsMap(event.Get());
        events.push_back(event.Release());
      } else {
        remaining.push_back(event.Release());
      }
    }
    remaining.swap(event_queue_);
  }

  for (const auto& event : events) {
    EventTarget* event_target = event->target();
    // FIXME: we should figure out how to make dispatchEvent properly virtual to
    // avoid special casting window.
    // FIXME: We should not fire events for nodes that are no longer in the
    // tree.
    probe::AsyncTask async_task(event_target->GetExecutionContext(),
                                event->async_task_context());
    if (LocalDOMWindow* window = event_target->ToLocalDOMWindow())
      window->DispatchEvent(*event, nullptr);
    else
      event_target->DispatchEvent(*event);
  }
}

void ScriptedAnimationController::ExecuteVideoFrameCallbacks() {
  // dispatchEvents() runs script which can cause the context to be destroyed.
  if (!GetExecutionContext())
    return;

  Vector<ExecuteVfcCallback> execute_vfc_callbacks;
  vfc_execution_queue_.swap(execute_vfc_callbacks);
  for (auto& callback : execute_vfc_callbacks)
    std::move(callback).Run(current_frame_time_ms_);
}

void ScriptedAnimationController::ExecuteFrameCallbacks() {
  // dispatchEvents() runs script which can cause the context to be destroyed.
  if (!GetExecutionContext())
    return;

  callback_collection_.ExecuteFrameCallbacks(current_frame_time_ms_,
                                             current_frame_legacy_time_ms_);
}

void ScriptedAnimationController::CallMediaQueryListListeners() {
  MediaQueryListListeners listeners;
  listeners.swap(media_query_list_listeners_);
  media_query_list_listeners_set_.clear();

  for (const auto& listener : listeners) {
    listener->NotifyMediaQueryChanged();
  }
}

bool ScriptedAnimationController::HasScheduledFrameTasks() const {
  return callback_collection_.HasFrameCallback() || !task_queue_.IsEmpty() ||
         !event_queue_.IsEmpty() || !media_query_list_listeners_.IsEmpty() ||
         GetWindow()->document()->HasAutofocusCandidates() ||
         !vfc_execution_queue_.IsEmpty();
}

PageAnimator* ScriptedAnimationController::GetPageAnimator() {
  if (GetWindow()->document() && GetWindow()->document()->GetPage())
    return &(GetWindow()->document()->GetPage()->Animator());
  return nullptr;
}

void ScriptedAnimationController::ServiceScriptedAnimations(
    base::TimeTicks monotonic_time_now,
    bool can_throttle) {
  if (!GetExecutionContext() || GetExecutionContext()->IsContextPaused())
    return;
  auto* loader = GetWindow()->document()->Loader();
  if (!loader)
    return;

  if (can_throttle) {
    DispatchEvents([](const Event* event) {
      return event->type() == event_type_names::kResize;
    });
    return;
  }

  current_frame_time_ms_ =
      loader->GetTiming()
          .MonotonicTimeToZeroBasedDocumentTime(monotonic_time_now)
          .InMillisecondsF();
  current_frame_legacy_time_ms_ =
      loader->GetTiming()
          .MonotonicTimeToPseudoWallTime(monotonic_time_now)
          .InMillisecondsF();
  auto* animator = GetPageAnimator();
  if (animator && HasFrameCallback())
    animator->SetCurrentFrameHadRaf();

  if (!HasScheduledFrameTasks())
    return;

  // https://gpuweb.github.io/gpuweb/#abstract-opdef-expire-stale-external-textures
  WebGPUCheckStateToExpireVideoFrame();

  // https://html.spec.whatwg.org/C/#update-the-rendering

  // 10.5. For each fully active Document in docs, flush autofocus
  // candidates for that Document if its browsing context is a top-level
  // browsing context.
  GetWindow()->document()->FlushAutofocusCandidates();

  // 10.8. For each fully active Document in docs, evaluate media
  // queries and report changes for that Document, passing in now as the
  // timestamp
  CallMediaQueryListListeners();

  // 10.6. For each fully active Document in docs, run the resize steps
  // for that Document, passing in now as the timestamp.
  // 10.7. For each fully active Document in docs, run the scroll steps
  // for that Document, passing in now as the timestamp.
  // 10.9. For each fully active Document in docs, update animations and
  // send events for that Document, passing in now as the timestamp.
  //
  // We share a single event queue for them.
  DispatchEvents();

  // 10.10. For each fully active Document in docs, run the fullscreen
  // steps for that Document, passing in now as the timestamp.
  RunTasks();

  // Run the fulfilled HTMLVideoELement.requestVideoFrameCallback() callbacks.
  // See https://wicg.github.io/video-rvfc/.
  ExecuteVideoFrameCallbacks();

  // 10.11. For each fully active Document in docs, run the animation
  // frame callbacks for that Document, passing in now as the timestamp.
  ExecuteFrameCallbacks();
  if (animator && HasFrameCallback())
    animator->SetNextFrameHasPendingRaf();

  // See LocalFrameView::RunPostLifecycleSteps() for 10.12.

  ScheduleAnimationIfNeeded();
}

void ScriptedAnimationController::EnqueueTask(base::OnceClosure task) {
  task_queue_.push_back(std::move(task));
  ScheduleAnimationIfNeeded();
}

void ScriptedAnimationController::EnqueueEvent(Event* event) {
  event->async_task_context()->Schedule(event->target()->GetExecutionContext(),
                                        event->type());
  event_queue_.push_back(event);
  ScheduleAnimationIfNeeded();
}

void ScriptedAnimationController::EnqueuePerFrameEvent(Event* event) {
  if (!InsertToPerFrameEventsMap(event))
    return;
  EnqueueEvent(event);
}

void ScriptedAnimationController::EnqueueMediaQueryChangeListeners(
    HeapVector<Member<MediaQueryListListener>>& listeners) {
  for (const auto& listener : listeners) {
    if (!media_query_list_listeners_set_.Contains(listener)) {
      media_query_list_listeners_.push_back(listener);
      media_query_list_listeners_set_.insert(listener);
    }
  }
  DCHECK_EQ(media_query_list_listeners_.size(),
            media_query_list_listeners_set_.size());
  ScheduleAnimationIfNeeded();
}

void ScriptedAnimationController::ScheduleAnimationIfNeeded() {
  if (!GetExecutionContext() || GetExecutionContext()->IsContextPaused())
    return;

  auto* frame = GetWindow()->GetFrame();
  if (!frame)
    return;

  if (HasScheduledFrameTasks()) {
    frame->View()->ScheduleAnimation();
    return;
  }
}

LocalDOMWindow* ScriptedAnimationController::GetWindow() const {
  return To<LocalDOMWindow>(GetExecutionContext());
}

void ScriptedAnimationController::WebGPURegisterVideoFrameStateCallback(
    WebGPUVideoFrameStateCallback webgpu_video_frame_state_callback) {
  webgpu_video_frame_state_callbacks_.push_back(
      std::move(webgpu_video_frame_state_callback));
}

// If a callback |IsCancelled| or returns false, remove that callback
// from the list. Otherwise, keep it to be checked again later.
void ScriptedAnimationController::WebGPUCheckStateToExpireVideoFrame() {
  for (auto* it = webgpu_video_frame_state_callbacks_.begin();
       it != webgpu_video_frame_state_callbacks_.end();) {
    if (it->IsCancelled() || !it->Run()) {
      it = webgpu_video_frame_state_callbacks_.erase(it);
    } else {
      ++it;
    }
  }
}

}  // namespace blink
