/*
 * Copyright (C) 2008 Apple Inc. All Rights Reserved.
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
 * THIS SOFTWARE IS PROVIDED BY APPLE COMPUTER, INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE COMPUTER, INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include "third_party/blink/renderer/core/frame/dom_timer.h"

#include "base/numerics/clamped_math.h"
#include "base/task/single_thread_task_runner.h"
#include "base/time/time.h"
#include "third_party/blink/public/common/features.h"
#include "third_party/blink/public/platform/task_type.h"
#include "third_party/blink/renderer/bindings/core/v8/scheduled_action.h"
#include "third_party/blink/renderer/core/execution_context/execution_context.h"
#include "third_party/blink/renderer/core/inspector/inspector_trace_events.h"
#include "third_party/blink/renderer/core/probe/core_probes.h"
#include "third_party/blink/renderer/platform/instrumentation/tracing/trace_event.h"
#include "third_party/blink/renderer/platform/scheduler/public/scheduling_policy.h"

namespace blink {

namespace {

// Step 11 of the algorithm at
// https://html.spec.whatwg.org/multipage/timers-and-user-prompts.html requires
// that a timeout less than 4ms is increased to 4ms when the nesting level is
// greater than 5.
constexpr int kMaxTimerNestingLevel = 5;
constexpr base::TimeDelta kMinimumInterval = base::Milliseconds(4);
constexpr base::TimeDelta kMaxHighResolutionInterval = base::Milliseconds(32);

}  // namespace

int DOMTimer::Install(ExecutionContext* context,
                      ScheduledAction* action,
                      base::TimeDelta timeout,
                      bool single_shot) {
  int timeout_id = context->Timers()->InstallNewTimeout(context, action,
                                                        timeout, single_shot);
  return timeout_id;
}

void DOMTimer::RemoveByID(ExecutionContext* context, int timeout_id) {
  DOMTimer* timer = context->Timers()->RemoveTimeoutByID(timeout_id);
  DEVTOOLS_TIMELINE_TRACE_EVENT_INSTANT(
      "TimerRemove", inspector_timer_remove_event::Data, context, timeout_id);
  // Eagerly unregister as ExecutionContext observer.
  if (timer)
    timer->SetExecutionContext(nullptr);
}

DOMTimer::DOMTimer(ExecutionContext* context,
                   ScheduledAction* action,
                   base::TimeDelta timeout,
                   bool single_shot,
                   int timeout_id)
    : ExecutionContextLifecycleObserver(context),
      TimerBase(nullptr),
      timeout_id_(timeout_id),
      // Step 9:
      nesting_level_(context->Timers()->TimerNestingLevel()),
      action_(action) {
  DCHECK_GT(timeout_id, 0);

  // Step 10:
  if (timeout.is_negative())
    timeout = base::TimeDelta();

  // Steps 12 and 13:
  // Note: The implementation increments the nesting level before using it to
  // adjust timeout, contrary to what the spec requires crbug.com/1108877.
  IncrementNestingLevel();

  // Step 11:
  // Note: The implementation uses >= instead of >, contrary to what the spec
  // requires crbug.com/1108877.
  int max_nesting_level = features::IsMaxUnthrottledTimeoutNestingLevelEnabled()
                              ? features::GetMaxUnthrottledTimeoutNestingLevel()
                              : kMaxTimerNestingLevel;
  // Under AlignWakeUps experiment, avoid timer alignment if the original delay
  // is small, to avoid being affected by ongoing experiments on delay clamping
  // MaxUnthrottledTimeoutNestingLevel and SetTimeoutZeroWithoutClamping.
  // TODO(1153139) Remove this logic one experiments have shipped.
  bool precise = (timeout < kMinimumInterval) ||
                 (scheduler::IsAlignWakeUpsDisabledForProcess() &&
                  timeout < kMaxHighResolutionInterval);

  if (nesting_level_ >= max_nesting_level && timeout < kMinimumInterval)
    timeout = kMinimumInterval;

  // Select TaskType based on nesting level.
  TaskType task_type;
  if (nesting_level_ >= kMaxTimerNestingLevel) {
    task_type = TaskType::kJavascriptTimerDelayedHighNesting;
  } else if (timeout.is_zero()) {
    task_type = TaskType::kJavascriptTimerImmediate;
    DCHECK_LT(nesting_level_, max_nesting_level);
  } else {
    task_type = TaskType::kJavascriptTimerDelayedLowNesting;
  }
  MoveToNewTaskRunner(context->GetTaskRunner(task_type));

  // Clamping up to 1ms for historical reasons crbug.com/402694.
  // Removing clamp for single_shot behind a feature flag.
  if (!single_shot || !blink::features::IsSetTimeoutWithoutClampEnabled())
    timeout = std::max(timeout, base::Milliseconds(1));

  if (single_shot)
    StartOneShot(timeout, FROM_HERE, precise);
  else
    StartRepeating(timeout, FROM_HERE, precise);

  DEVTOOLS_TIMELINE_TRACE_EVENT_INSTANT(
      "TimerInstall", inspector_timer_install_event::Data, context, timeout_id,
      timeout, single_shot);
  const char* name = single_shot ? "setTimeout" : "setInterval";
  async_task_context_.Schedule(context, name);
  probe::BreakableLocation(context, name);
}

DOMTimer::~DOMTimer() = default;

void DOMTimer::Dispose() {
  Stop();
}

void DOMTimer::Stop() {
  if (!action_)
    return;

  async_task_context_.Cancel();
  const bool is_interval = !RepeatInterval().is_zero();
  probe::BreakableLocation(GetExecutionContext(),
                           is_interval ? "clearInterval" : "clearTimeout");

  // Need to release JS objects potentially protected by ScheduledAction
  // because they can form circular references back to the ExecutionContext
  // which will cause a memory leak.
  if (action_)
    action_->Dispose();
  action_ = nullptr;
  TimerBase::Stop();
}

void DOMTimer::ContextDestroyed() {
  Stop();
}

void DOMTimer::Fired() {
  ExecutionContext* context = GetExecutionContext();
  DCHECK(context);
  context->Timers()->SetTimerNestingLevel(nesting_level_);
  DCHECK(!context->IsContextPaused());
  // Only the first execution of a multi-shot timer should get an affirmative
  // user gesture indicator.

  DEVTOOLS_TIMELINE_TRACE_EVENT("TimerFire", inspector_timer_fire_event::Data,
                                context, timeout_id_);
  const bool is_interval = !RepeatInterval().is_zero();
  probe::UserCallback probe(context, is_interval ? "setInterval" : "setTimeout",
                            g_null_atom, true);
  probe::AsyncTask async_task(context, &async_task_context_,
                              is_interval ? "fired" : nullptr);

  // Simple case for non-one-shot timers.
  if (IsActive()) {
    DCHECK(is_interval);

    // Steps 12 and 13:
    // Note: The implementation increments the nesting level before using it to
    // adjust timeout, contrary to what the spec requires crbug.com/1108877.
    IncrementNestingLevel();

    // Make adjustments when the nesting level becomes >= |kMaxNestingLevel|.
    // Note: The implementation uses >= instead of >, contrary to what the spec
    // requires crbug.com/1108877.
    int max_nesting_level =
        features::IsMaxUnthrottledTimeoutNestingLevelEnabled()
            ? features::GetMaxUnthrottledTimeoutNestingLevel()
            : kMaxTimerNestingLevel;
    // Step 11:
    if (nesting_level_ == max_nesting_level &&
        RepeatInterval() < kMinimumInterval) {
      AugmentRepeatInterval(kMinimumInterval - RepeatInterval());
    }
    if (nesting_level_ == kMaxTimerNestingLevel) {
      // Move to the TaskType that corresponds to nesting level >=
      // |kMaxNestingLevel|.
      MoveToNewTaskRunner(
          context->GetTaskRunner(TaskType::kJavascriptTimerDelayedHighNesting));
    }

    DCHECK(nesting_level_ < max_nesting_level ||
           RepeatInterval() >= kMinimumInterval);

    // No access to member variables after this point, it can delete the timer.
    action_->Execute(context);

    context->Timers()->SetTimerNestingLevel(0);

    return;
  }

  // Unregister the timer from ExecutionContext before executing the action
  // for one-shot timers.
  ScheduledAction* action = action_.Release();
  context->Timers()->RemoveTimeoutByID(timeout_id_);

  action->Execute(context);

  // Eagerly clear out |action|'s resources.
  action->Dispose();

  // ExecutionContext might be already gone when we executed action->execute().
  ExecutionContext* execution_context = GetExecutionContext();
  if (!execution_context)
    return;

  execution_context->Timers()->SetTimerNestingLevel(0);
  // Eagerly unregister as ExecutionContext observer.
  SetExecutionContext(nullptr);
}

void DOMTimer::Trace(Visitor* visitor) const {
  visitor->Trace(action_);
  ExecutionContextLifecycleObserver::Trace(visitor);
}

void DOMTimer::IncrementNestingLevel() {
  nesting_level_ = base::ClampAdd(nesting_level_, 1);
}

}  // namespace blink
