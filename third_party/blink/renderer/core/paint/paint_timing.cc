// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/paint/paint_timing.h"

#include <memory>
#include <utility>

#include "base/metrics/histogram_macros.h"
#include "base/time/default_tick_clock.h"
#include "third_party/blink/renderer/core/dom/document.h"
#include "third_party/blink/renderer/core/dom/frame_request_callback_collection.h"
#include "third_party/blink/renderer/core/frame/local_dom_window.h"
#include "third_party/blink/renderer/core/frame/local_frame.h"
#include "third_party/blink/renderer/core/frame/local_frame_view.h"
#include "third_party/blink/renderer/core/layout/deferred_shaping_controller.h"
#include "third_party/blink/renderer/core/loader/document_loader.h"
#include "third_party/blink/renderer/core/loader/interactive_detector.h"
#include "third_party/blink/renderer/core/loader/progress_tracker.h"
#include "third_party/blink/renderer/core/page/chrome_client.h"
#include "third_party/blink/renderer/core/page/page.h"
#include "third_party/blink/renderer/core/probe/core_probes.h"
#include "third_party/blink/renderer/core/timing/dom_window_performance.h"
#include "third_party/blink/renderer/core/timing/window_performance.h"
#include "third_party/blink/renderer/platform/graphics/paint/ignore_paint_timing_scope.h"
#include "third_party/blink/renderer/platform/instrumentation/resource_coordinator/document_resource_coordinator.h"
#include "third_party/blink/renderer/platform/instrumentation/tracing/trace_event.h"
#include "third_party/blink/renderer/platform/loader/fetch/resource_fetcher.h"
#include "third_party/blink/renderer/platform/scheduler/public/frame_scheduler.h"
#include "third_party/blink/renderer/platform/wtf/cross_thread_functional.h"
#include "third_party/blink/renderer/platform/wtf/wtf.h"

namespace blink {

namespace {

WindowPerformance* GetPerformanceInstance(LocalFrame* frame) {
  WindowPerformance* performance = nullptr;
  if (frame && frame->DomWindow()) {
    performance = DOMWindowPerformance::performance(*frame->DomWindow());
  }
  return performance;
}

}  // namespace

class RecodingTimeAfterBackForwardCacheRestoreFrameCallback
    : public FrameCallback {
 public:
  RecodingTimeAfterBackForwardCacheRestoreFrameCallback(
      PaintTiming* paint_timing,
      wtf_size_t record_index)
      : paint_timing_(paint_timing), record_index_(record_index) {}
  ~RecodingTimeAfterBackForwardCacheRestoreFrameCallback() override = default;

  void Invoke(double high_res_time_ms) override {
    // Instead of |high_res_time_ms|, use PaintTiming's |clock_->NowTicks()| for
    // consistency and testability.
    paint_timing_->SetRequestAnimationFrameAfterBackForwardCacheRestore(
        record_index_, count_);

    count_++;
    if (count_ ==
        WebPerformance::
            kRequestAnimationFramesToRecordAfterBackForwardCacheRestore) {
      paint_timing_->NotifyPaintTimingChanged();
      return;
    }

    if (auto* frame = paint_timing_->GetFrame()) {
      if (auto* document = frame->GetDocument()) {
        document->RequestAnimationFrame(this);
      }
    }
  }

  void Trace(Visitor* visitor) const override {
    visitor->Trace(paint_timing_);
    FrameCallback::Trace(visitor);
  }

 private:
  Member<PaintTiming> paint_timing_;
  const wtf_size_t record_index_;
  size_t count_ = 0;
};

// static
const char PaintTiming::kSupplementName[] = "PaintTiming";

// static
PaintTiming& PaintTiming::From(Document& document) {
  PaintTiming* timing = Supplement<Document>::From<PaintTiming>(document);
  if (!timing) {
    timing = MakeGarbageCollected<PaintTiming>(document);
    ProvideTo(document, timing);
  }
  return *timing;
}

void PaintTiming::MarkFirstPaint() {
  // Test that |first_paint_| is non-zero here, as well as in setFirstPaint, so
  // we avoid invoking monotonicallyIncreasingTime() on every call to
  // markFirstPaint().
  if (!first_paint_.is_null())
    return;
  DCHECK_EQ(IgnorePaintTimingScope::IgnoreDepth(), 0);
  SetFirstPaint(clock_->NowTicks());
}

void PaintTiming::MarkFirstContentfulPaint() {
  // Test that |first_contentful_paint_| is non-zero here, as well as in
  // setFirstContentfulPaint, so we avoid invoking
  // monotonicallyIncreasingTime() on every call to
  // markFirstContentfulPaint().
  if (!first_contentful_paint_.is_null())
    return;
  if (IgnorePaintTimingScope::IgnoreDepth() > 0)
    return;
  SetFirstContentfulPaint(clock_->NowTicks());
}

void PaintTiming::MarkFirstImagePaint() {
  if (!first_image_paint_.is_null())
    return;
  DCHECK_EQ(IgnorePaintTimingScope::IgnoreDepth(), 0);
  first_image_paint_ = clock_->NowTicks();
  SetFirstContentfulPaint(first_image_paint_);
  RegisterNotifyPresentationTime(PaintEvent::kFirstImagePaint);
}

void PaintTiming::MarkFirstEligibleToPaint() {
  if (!first_eligible_to_paint_.is_null())
    return;

  first_eligible_to_paint_ = clock_->NowTicks();
  NotifyPaintTimingChanged();
}

// We deliberately use |first_paint_| here rather than
// |first_paint_presentation_|, because |first_paint_presentation_| is set
// asynchronously and we need to be able to rely on a synchronous check that
// SetFirstPaintPresentation hasn't been scheduled or run.
void PaintTiming::MarkIneligibleToPaint() {
  if (first_eligible_to_paint_.is_null() || !first_paint_.is_null())
    return;

  first_eligible_to_paint_ = base::TimeTicks();
  NotifyPaintTimingChanged();
}

void PaintTiming::SetFirstMeaningfulPaintCandidate(base::TimeTicks timestamp) {
  if (!first_meaningful_paint_candidate_.is_null())
    return;
  first_meaningful_paint_candidate_ = timestamp;
  if (GetFrame() && GetFrame()->View() && !GetFrame()->View()->IsAttached()) {
    GetFrame()->GetFrameScheduler()->OnFirstMeaningfulPaint();
  }
}

void PaintTiming::SetFirstMeaningfulPaint(
    base::TimeTicks presentation_time,
    FirstMeaningfulPaintDetector::HadUserInput had_input) {
  DCHECK(first_meaningful_paint_presentation_.is_null());
  DCHECK(!presentation_time.is_null());

  TRACE_EVENT_MARK_WITH_TIMESTAMP2("loading,rail,devtools.timeline",
                                   "firstMeaningfulPaint", presentation_time,
                                   "frame", ToTraceValue(GetFrame()),
                                   "afterUserInput", had_input);

  // Notify FMP for UMA only if there's no user input before FMP, so that layout
  // changes caused by user interactions wouldn't be considered as FMP.
  if (had_input == FirstMeaningfulPaintDetector::kNoUserInput) {
    first_meaningful_paint_presentation_ = presentation_time;
    NotifyPaintTimingChanged();
  }
}

void PaintTiming::NotifyPaint(bool is_first_paint,
                              bool text_painted,
                              bool image_painted) {
  if (IgnorePaintTimingScope::IgnoreDepth() > 0)
    return;
  if (is_first_paint)
    MarkFirstPaint();
  if (text_painted)
    MarkFirstContentfulPaint();
  if (image_painted)
    MarkFirstImagePaint();
  fmp_detector_->NotifyPaint();

  if (is_first_paint)
    GetFrame()->OnFirstPaint(text_painted, image_painted);
}

void PaintTiming::OnPortalActivate() {
  last_portal_activated_presentation_ = base::TimeTicks();
  RegisterNotifyPresentationTime(PaintEvent::kPortalActivatedPaint);
}

void PaintTiming::SetPortalActivatedPaint(base::TimeTicks stamp) {
  DCHECK(last_portal_activated_presentation_.is_null());
  last_portal_activated_presentation_ = stamp;
  NotifyPaintTimingChanged();
}

void PaintTiming::SetTickClockForTesting(const base::TickClock* clock) {
  clock_ = clock;
}

void PaintTiming::Trace(Visitor* visitor) const {
  visitor->Trace(fmp_detector_);
  Supplement<Document>::Trace(visitor);
}

PaintTiming::PaintTiming(Document& document)
    : Supplement<Document>(document),
      fmp_detector_(MakeGarbageCollected<FirstMeaningfulPaintDetector>(this)),
      clock_(base::DefaultTickClock::GetInstance()) {}

LocalFrame* PaintTiming::GetFrame() const {
  return GetSupplementable()->GetFrame();
}

void PaintTiming::NotifyPaintTimingChanged() {
  if (GetSupplementable()->Loader())
    GetSupplementable()->Loader()->DidChangePerformanceTiming();
}

void PaintTiming::SetFirstPaint(base::TimeTicks stamp) {
  if (!first_paint_.is_null())
    return;

  DCHECK_EQ(IgnorePaintTimingScope::IgnoreDepth(), 0);

  first_paint_ = stamp;
  RegisterNotifyPresentationTime(PaintEvent::kFirstPaint);

  LocalFrame* frame = GetFrame();
  if (frame && frame->GetDocument()) {
    frame->GetDocument()->MarkFirstPaint();
  }
}

void PaintTiming::SetFirstContentfulPaint(base::TimeTicks stamp) {
  if (!first_contentful_paint_.is_null())
    return;
  DCHECK_EQ(IgnorePaintTimingScope::IgnoreDepth(), 0);
  SetFirstPaint(stamp);
  first_contentful_paint_ = stamp;
  RegisterNotifyPresentationTime(PaintEvent::kFirstContentfulPaint);

  LocalFrame* frame = GetFrame();
  if (!frame)
    return;
  frame->View()->OnFirstContentfulPaint();

  if (frame->IsMainFrame() && frame->GetFrameScheduler())
    frame->GetFrameScheduler()->OnFirstContentfulPaintInMainFrame();

  NotifyPaintTimingChanged();
}

void PaintTiming::RegisterNotifyPresentationTime(PaintEvent event) {
  RegisterNotifyPresentationTime(
      CrossThreadBindOnce(&PaintTiming::ReportPresentationTime,
                          WrapCrossThreadWeakPersistent(this), event));
}

void PaintTiming::
    RegisterNotifyFirstPaintAfterBackForwardCacheRestorePresentationTime(
        wtf_size_t index) {
  RegisterNotifyPresentationTime(CrossThreadBindOnce(
      &PaintTiming::
          ReportFirstPaintAfterBackForwardCacheRestorePresentationTime,
      WrapCrossThreadWeakPersistent(this), index));
}

void PaintTiming::RegisterNotifyPresentationTime(ReportTimeCallback callback) {
  // ReportPresentationTime will queue a presentation-promise, the callback is
  // called when the compositor submission of the current render frame completes
  // or fails to happen.
  if (!GetFrame() || !GetFrame()->GetPage())
    return;
  GetFrame()->GetPage()->GetChromeClient().NotifyPresentationTime(
      *GetFrame(), std::move(callback));
}

void PaintTiming::ReportPresentationTime(PaintEvent event,
                                         base::TimeTicks timestamp) {
  DCHECK(IsMainThread());
  switch (event) {
    case PaintEvent::kFirstPaint:
      SetFirstPaintPresentation(timestamp);
      return;
    case PaintEvent::kFirstContentfulPaint:
      SetFirstContentfulPaintPresentation(timestamp);
      return;
    case PaintEvent::kFirstImagePaint:
      SetFirstImagePaintPresentation(timestamp);
      return;
    case PaintEvent::kPortalActivatedPaint:
      SetPortalActivatedPaint(timestamp);
      return;
    default:
      NOTREACHED();
  }
}

void PaintTiming::ReportFirstPaintAfterBackForwardCacheRestorePresentationTime(
    wtf_size_t index,
    base::TimeTicks timestamp) {
  DCHECK(IsMainThread());
  SetFirstPaintAfterBackForwardCacheRestorePresentation(timestamp, index);
}

void PaintTiming::SetFirstPaintPresentation(base::TimeTicks stamp) {
  DCHECK(first_paint_presentation_.is_null());
  first_paint_presentation_ = stamp;
  probe::PaintTiming(GetSupplementable(), "firstPaint",
                     first_paint_presentation_.since_origin().InSecondsF());
  WindowPerformance* performance = GetPerformanceInstance(GetFrame());
  if (performance)
    performance->AddFirstPaintTiming(first_paint_presentation_);
  NotifyPaintTimingChanged();
}

void PaintTiming::SetFirstContentfulPaintPresentation(base::TimeTicks stamp) {
  DCHECK(first_contentful_paint_presentation_.is_null());
  TRACE_EVENT_INSTANT_WITH_TIMESTAMP0("benchmark,loading",
                                      "GlobalFirstContentfulPaint",
                                      TRACE_EVENT_SCOPE_GLOBAL, stamp);
  first_contentful_paint_presentation_ = stamp;
  probe::PaintTiming(
      GetSupplementable(), "firstContentfulPaint",
      first_contentful_paint_presentation_.since_origin().InSecondsF());
  WindowPerformance* performance = GetPerformanceInstance(GetFrame());
  if (performance) {
    performance->AddFirstContentfulPaintTiming(
        first_contentful_paint_presentation_);
  }
  if (GetFrame())
    GetFrame()->Loader().Progress().DidFirstContentfulPaint();
  NotifyPaintTimingChanged();
  fmp_detector_->NotifyFirstContentfulPaint(
      first_contentful_paint_presentation_);
  InteractiveDetector* interactive_detector =
      InteractiveDetector::From(*GetSupplementable());
  if (interactive_detector) {
    interactive_detector->OnFirstContentfulPaint(
        first_contentful_paint_presentation_);
  }
  auto* coordinator = GetSupplementable()->GetResourceCoordinator();
  if (coordinator && GetFrame() && GetFrame()->IsOutermostMainFrame()) {
    PerformanceTiming* timing = performance->timing();
    base::TimeDelta fcp = stamp - timing->NavigationStartAsMonotonicTime();
    coordinator->OnFirstContentfulPaint(fcp);
  }

  if (auto* ds_controller =
          DeferredShapingController::From(*GetSupplementable())) {
    ds_controller->OnFirstContentfulPaint();
  }
}

void PaintTiming::SetFirstImagePaintPresentation(base::TimeTicks stamp) {
  DCHECK(first_image_paint_presentation_.is_null());
  first_image_paint_presentation_ = stamp;
  probe::PaintTiming(
      GetSupplementable(), "firstImagePaint",
      first_image_paint_presentation_.since_origin().InSecondsF());
  NotifyPaintTimingChanged();
}

void PaintTiming::SetFirstPaintAfterBackForwardCacheRestorePresentation(
    base::TimeTicks stamp,
    wtf_size_t index) {
  // The elements are allocated when the page is restored from the cache.
  DCHECK_GE(first_paints_after_back_forward_cache_restore_presentation_.size(),
            index);
  DCHECK(first_paints_after_back_forward_cache_restore_presentation_[index]
             .is_null());
  first_paints_after_back_forward_cache_restore_presentation_[index] = stamp;
  NotifyPaintTimingChanged();
}

void PaintTiming::SetRequestAnimationFrameAfterBackForwardCacheRestore(
    wtf_size_t index,
    size_t count) {
  auto now = clock_->NowTicks();

  // The elements are allocated when the page is restored from the cache.
  DCHECK_LT(index,
            request_animation_frames_after_back_forward_cache_restore_.size());
  auto& current_rafs =
      request_animation_frames_after_back_forward_cache_restore_[index];
  DCHECK_LT(count, current_rafs.size());
  DCHECK_EQ(current_rafs[count], base::TimeTicks());
  current_rafs[count] = now;
}

void PaintTiming::OnRestoredFromBackForwardCache() {
  // Allocate the last element with 0, which indicates that the first paint
  // after this navigation doesn't happen yet.
  wtf_size_t index =
      first_paints_after_back_forward_cache_restore_presentation_.size();
  DCHECK_EQ(index,
            request_animation_frames_after_back_forward_cache_restore_.size());

  first_paints_after_back_forward_cache_restore_presentation_.push_back(
      base::TimeTicks());
  RegisterNotifyFirstPaintAfterBackForwardCacheRestorePresentationTime(index);

  request_animation_frames_after_back_forward_cache_restore_.push_back(
      RequestAnimationFrameTimesAfterBackForwardCacheRestore{});

  LocalFrame* frame = GetFrame();
  if (!frame->IsOutermostMainFrame()) {
    return;
  }

  Document* document = frame->GetDocument();
  DCHECK(document);

  // Cancel if there is already a registered callback.
  if (raf_after_bfcache_restore_measurement_callback_id_) {
    document->CancelAnimationFrame(
        raf_after_bfcache_restore_measurement_callback_id_);
    raf_after_bfcache_restore_measurement_callback_id_ = 0;
  }

  raf_after_bfcache_restore_measurement_callback_id_ =
      document->RequestAnimationFrame(
          MakeGarbageCollected<
              RecodingTimeAfterBackForwardCacheRestoreFrameCallback>(this,
                                                                     index));
}

bool PaintTiming::IsLCPMouseoverDispatchedRecently() {
  static constexpr base::TimeDelta kRecencyDelta = base::Milliseconds(500);
  return (
      !lcp_mouse_over_dispatch_time_.is_null() &&
      ((clock_->NowTicks() - lcp_mouse_over_dispatch_time_) < kRecencyDelta));
}

void PaintTiming::SetLCPMouseoverDispatched() {
  lcp_mouse_over_dispatch_time_ = clock_->NowTicks();
}

}  // namespace blink
