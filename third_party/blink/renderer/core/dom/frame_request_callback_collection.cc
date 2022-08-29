// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/dom/frame_request_callback_collection.h"

#include "third_party/blink/renderer/core/frame/web_feature.h"
#include "third_party/blink/renderer/core/inspector/inspector_trace_events.h"
#include "third_party/blink/renderer/core/probe/core_probes.h"
#include "third_party/blink/renderer/platform/instrumentation/use_counter.h"

namespace blink {

FrameRequestCallbackCollection::FrameRequestCallbackCollection(
    ExecutionContext* context)
    : context_(context) {}

FrameRequestCallbackCollection::CallbackId
FrameRequestCallbackCollection::RegisterFrameCallback(FrameCallback* callback) {
  FrameRequestCallbackCollection::CallbackId id = ++next_callback_id_;
  callback->SetIsCancelled(false);
  callback->SetId(id);
  frame_callbacks_.push_back(callback);

  DEVTOOLS_TIMELINE_TRACE_EVENT_INSTANT("RequestAnimationFrame",
                                        inspector_animation_frame_event::Data,
                                        context_, id);
  callback->async_task_context()->Schedule(context_, "requestAnimationFrame");
  probe::BreakableLocation(context_, "requestAnimationFrame");
  return id;
}

void FrameRequestCallbackCollection::CancelFrameCallback(CallbackId id) {
  for (wtf_size_t i = 0; i < frame_callbacks_.size(); ++i) {
    if (frame_callbacks_[i]->Id() == id) {
      frame_callbacks_[i]->async_task_context()->Cancel();
      probe::BreakableLocation(context_, "cancelAnimationFrame");
      frame_callbacks_.EraseAt(i);
      DEVTOOLS_TIMELINE_TRACE_EVENT_INSTANT(
          "CancelAnimationFrame", inspector_animation_frame_event::Data,
          context_, id);
      return;
    }
  }
  for (const auto& callback : callbacks_to_invoke_) {
    if (callback->Id() == id) {
      callback->async_task_context()->Cancel();
      probe::BreakableLocation(context_, "cancelAnimationFrame");
      DEVTOOLS_TIMELINE_TRACE_EVENT_INSTANT(
          "CancelAnimationFrame", inspector_animation_frame_event::Data,
          context_, id);
      callback->SetIsCancelled(true);
      // will be removed at the end of ExecuteCallbacks()
      return;
    }
  }
}

void FrameRequestCallbackCollection::ExecuteFrameCallbacks(
    double high_res_now_ms,
    double high_res_now_ms_legacy) {
  TRACE_EVENT0("blink",
               "FrameRequestCallbackCollection::ExecuteFrameCallbacks");
  ExecutionContext::ScopedRequestAnimationFrameStatus scoped_raf_status(
      context_);

  // First, generate a list of callbacks to consider.  Callbacks registered from
  // this point on are considered only for the "next" frame, not this one.
  DCHECK(callbacks_to_invoke_.IsEmpty());
  swap(callbacks_to_invoke_, frame_callbacks_);

  for (const auto& callback : callbacks_to_invoke_) {
    // When the ExecutionContext is destroyed (e.g. an iframe is detached),
    // there is no path to perform wrapper tracing for the callbacks. In such a
    // case, the callback functions may already have been collected by V8 GC.
    // Since it's possible that a callback function being invoked detaches an
    // iframe, we need to check the condition for each callback.
    if (context_->IsContextDestroyed())
      break;
    if (callback->IsCancelled()) {
      // Another requestAnimationFrame callback already cancelled this one
      UseCounter::Count(context_,
                        WebFeature::kAnimationFrameCancelledWithinFrame);
      continue;
    }
    DEVTOOLS_TIMELINE_TRACE_EVENT("FireAnimationFrame",
                                  inspector_animation_frame_event::Data,
                                  context_, callback->Id());
    probe::AsyncTask async_task(context_, callback->async_task_context());
    probe::UserCallback probe(context_, "requestAnimationFrame", AtomicString(),
                              true);
    if (callback->GetUseLegacyTimeBase())
      callback->Invoke(high_res_now_ms_legacy);
    else
      callback->Invoke(high_res_now_ms);
  }

  callbacks_to_invoke_.clear();
}

void FrameRequestCallbackCollection::Trace(Visitor* visitor) const {
  visitor->Trace(frame_callbacks_);
  visitor->Trace(callbacks_to_invoke_);
  visitor->Trace(context_);
}

V8FrameCallback::V8FrameCallback(V8FrameRequestCallback* callback)
    : callback_(callback) {}

void V8FrameCallback::Trace(blink::Visitor* visitor) const {
  visitor->Trace(callback_);
  FrameCallback::Trace(visitor);
}

void V8FrameCallback::Invoke(double highResTime) {
  callback_->InvokeAndReportException(nullptr, highResTime);
}

}  // namespace blink
