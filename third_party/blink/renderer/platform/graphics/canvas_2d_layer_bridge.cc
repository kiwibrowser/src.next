/*
 * Copyright (C) 2012 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE AND ITS CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL APPLE OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "third_party/blink/renderer/platform/graphics/canvas_2d_layer_bridge.h"

#include <memory>
#include <utility>

#include "base/feature_list.h"
#include "base/location.h"
#include "base/rand_util.h"
#include "base/task/single_thread_task_runner.h"
#include "base/timer/elapsed_timer.h"
#include "cc/layers/texture_layer.h"
#include "components/viz/common/resources/transferable_resource.h"
#include "gpu/command_buffer/client/raster_interface.h"
#include "gpu/config/gpu_finch_features.h"

#include "third_party/blink/public/common/features.h"
#include "third_party/blink/public/platform/platform.h"
#include "third_party/blink/renderer/platform/graphics/canvas_resource.h"
#include "third_party/blink/renderer/platform/graphics/canvas_resource_provider.h"
#include "third_party/blink/renderer/platform/graphics/gpu/shared_context_rate_limiter.h"
#include "third_party/blink/renderer/platform/graphics/gpu/shared_gpu_context.h"
#include "third_party/blink/renderer/platform/graphics/paint/paint_canvas.h"
#include "third_party/blink/renderer/platform/graphics/static_bitmap_image.h"
#include "third_party/blink/renderer/platform/graphics/unaccelerated_static_bitmap_image.h"
#include "third_party/blink/renderer/platform/graphics/web_graphics_context_3d_provider_wrapper.h"
#include "third_party/blink/renderer/platform/instrumentation/histogram.h"
#include "third_party/blink/renderer/platform/instrumentation/tracing/trace_event.h"
#include "third_party/blink/renderer/platform/runtime_enabled_features.h"
#include "third_party/blink/renderer/platform/scheduler/public/thread_scheduler.h"
#include "third_party/blink/renderer/platform/wtf/functional.h"
#include "third_party/skia/include/core/SkData.h"
#include "third_party/skia/include/core/SkSurface.h"

namespace blink {

namespace {

const base::Feature kCanvas2DHibernation {
  "Canvas2DHibernation",
#if BUILDFLAG(IS_MAC)
      // Canvas hibernation is not always enabled on MacOS X due to a bug that
      // causes content loss. TODO: Find a better fix for crbug.com/588434
      base::FeatureState::FEATURE_DISABLED_BY_DEFAULT
#else
      base::FeatureState::FEATURE_ENABLED_BY_DEFAULT
#endif
};
}

// static
bool Canvas2DLayerBridge::IsHibernationEnabled() {
  return base::FeatureList::IsEnabled(kCanvas2DHibernation);
}

Canvas2DLayerBridge::Canvas2DLayerBridge(const gfx::Size& size,
                                         RasterMode raster_mode,
                                         OpacityMode opacity_mode)
    : logger_(std::make_unique<Logger>()),
      have_recorded_draw_commands_(false),
      is_hidden_(false),
      is_being_displayed_(false),
      raster_mode_(raster_mode),
      opacity_mode_(opacity_mode),
      size_(size),
      snapshot_state_(kInitialSnapshotState),
      resource_host_(nullptr),
      last_recording_(nullptr) {
  // Used by browser tests to detect the use of a Canvas2DLayerBridge.
  TRACE_EVENT_INSTANT0("test_gpu", "Canvas2DLayerBridgeCreation",
                       TRACE_EVENT_SCOPE_GLOBAL);
}

Canvas2DLayerBridge::~Canvas2DLayerBridge() {
  ClearPendingRasterTimers();
  if (IsHibernating())
    logger_->ReportHibernationEvent(kHibernationEndedWithTeardown);
  ResetResourceProvider();

  if (!layer_)
    return;

  if (raster_mode_ == RasterMode::kGPU) {
    layer_->ClearTexture();
    // Orphaning the layer is required to trigger the recreation of a new layer
    // in the case where destruction is caused by a canvas resize. Test:
    // virtual/gpu/fast/canvas/canvas-resize-after-paint-without-layout.html
    layer_->RemoveFromParent();
  }
  layer_->ClearClient();
  layer_ = nullptr;
}

void Canvas2DLayerBridge::SetCanvasResourceHost(CanvasResourceHost* host) {
  resource_host_ = host;
}

void Canvas2DLayerBridge::ResetResourceProvider() {
  if (resource_host_)
    resource_host_->ReplaceResourceProvider(nullptr);
}

bool Canvas2DLayerBridge::ShouldAccelerate() const {
  bool use_gpu = raster_mode_ == RasterMode::kGPU;

  base::WeakPtr<WebGraphicsContext3DProviderWrapper> context_provider_wrapper =
      SharedGpuContext::ContextProviderWrapper();
  if (use_gpu &&
      (!context_provider_wrapper ||
       context_provider_wrapper->ContextProvider()->IsContextLost())) {
    use_gpu = false;
  }
  return use_gpu;
}

bool Canvas2DLayerBridge::IsAccelerated() const {
  if (raster_mode_ == RasterMode::kCPU)
    return false;
  if (IsHibernating())
    return false;
  if (resource_host_ && resource_host_->ResourceProvider())
    return resource_host_->ResourceProvider()->IsAccelerated();

  // Whether or not to accelerate is not yet resolved, the canvas cannot be
  // accelerated if the gpu context is lost.
  return ShouldAccelerate();
}

static void HibernateWrapper(base::WeakPtr<Canvas2DLayerBridge> bridge,
                             base::TimeTicks /*idleDeadline*/) {
  if (bridge) {
    bridge->Hibernate();
  } else {
    Canvas2DLayerBridge::Logger local_logger;
    local_logger.ReportHibernationEvent(
        Canvas2DLayerBridge::
            kHibernationAbortedDueToDestructionWhileHibernatePending);
  }
}

static void HibernateWrapperForTesting(
    base::WeakPtr<Canvas2DLayerBridge> bridge) {
  HibernateWrapper(std::move(bridge), base::TimeTicks());
}

static void LoseContextInBackgroundWrapper(
    base::WeakPtr<Canvas2DLayerBridge> bridge,
    base::TimeTicks /*idleDeadline*/) {
  if (bridge)
    bridge->LoseContext();
}

static void LoseContextInBackgroundForTestingWrapper(
    base::WeakPtr<Canvas2DLayerBridge> bridge) {
  LoseContextInBackgroundWrapper(std::move(bridge), base::TimeTicks());
}

void Canvas2DLayerBridge::Hibernate() {
  DCHECK(!IsHibernating());
  DCHECK(hibernation_scheduled_);

  hibernation_scheduled_ = false;

  if (!resource_host_ || !resource_host_->ResourceProvider()) {
    logger_->ReportHibernationEvent(kHibernationAbortedBecauseNoSurface);
    return;
  }

  if (!IsHidden()) {
    logger_->ReportHibernationEvent(kHibernationAbortedDueToVisibilityChange);
    return;
  }

  if (!IsValid()) {
    logger_->ReportHibernationEvent(kHibernationAbortedDueGpuContextLoss);
    return;
  }

  if (!IsAccelerated()) {
    logger_->ReportHibernationEvent(
        kHibernationAbortedDueToSwitchToUnacceleratedRendering);
    return;
  }

  TRACE_EVENT0("blink", "Canvas2DLayerBridge::hibernate");
  // No HibernationEvent reported on success. This is on purppose to avoid
  // non-complementary stats. Each HibernationScheduled event is paired with
  // exactly one failure or exit event.
  FlushRecording();
  // The following checks that the flush succeeded, which should always be the
  // case because flushRecording should only fail it it fails to allocate
  // a surface, and we have an early exit at the top of this function for when
  // 'this' does not already have a surface.
  DCHECK(!have_recorded_draw_commands_);
  SkPaint copy_paint;
  copy_paint.setBlendMode(SkBlendMode::kSrc);
  scoped_refptr<StaticBitmapImage> snapshot =
      resource_host_->ResourceProvider()->Snapshot();
  if (!snapshot) {
    logger_->ReportHibernationEvent(kHibernationAbortedDueSnapshotFailure);
    return;
  }
  hibernation_image_ = snapshot->PaintImageForCurrentFrame().GetSwSkImage();
  ResetResourceProvider();
  if (layer_)
    layer_->ClearTexture();

  // shouldBeDirectComposited() may have changed.
  if (resource_host_)
    resource_host_->SetNeedsCompositingUpdate();
  logger_->DidStartHibernating();
}

void Canvas2DLayerBridge::LoseContext() {
  DCHECK(!lose_context_in_background_);
  DCHECK(lose_context_in_background_scheduled_);

  lose_context_in_background_scheduled_ = false;

  // If canvas becomes visible again or canvas already lost its resource,
  // return here.
  if (!resource_host_ || !resource_host_->ResourceProvider() || !IsHidden() ||
      !IsValid() || context_lost_)
    return;

  SkipQueuedDrawCommands();
  DCHECK(!have_recorded_draw_commands_);

  // Frees canvas resource.
  lose_context_in_background_ = true;
  ResetResourceProvider();

  if (layer_)
    layer_->ClearTexture();

  if (resource_host_)
    resource_host_->SetNeedsCompositingUpdate();
}

CanvasResourceProvider* Canvas2DLayerBridge::ResourceProvider() const {
  return resource_host_ ? resource_host_->ResourceProvider() : nullptr;
}

CanvasResourceProvider* Canvas2DLayerBridge::GetOrCreateResourceProvider() {
  DCHECK(resource_host_);
  CanvasResourceProvider* resource_provider = ResourceProvider();

  if (context_lost_) {
    DCHECK(!resource_provider);
    return nullptr;
  }

  if (resource_provider && resource_provider->IsValid()) {
#if DCHECK_IS_ON()
    // If resource provider is accelerated, a layer should already exist.
    // unless this is a canvas in low latency mode.
    // If this DCHECK fails, it probably means that
    // CanvasRenderingContextHost::GetOrCreateCanvasResourceProvider() was
    // called on a 2D context before this function.
    if (IsAccelerated()) {
      DCHECK(!!layer_ ||
             (resource_host_ && resource_host_->LowLatencyEnabled()));
    }
#endif
    return resource_provider;
  }

  // Restore() is tried at most four times in two seconds to recreate the
  // ResourceProvider before the final attempt, in which a new
  // Canvas2DLayerBridge is created along with its resource provider.

  bool want_acceleration = ShouldAccelerate();
  RasterModeHint adjusted_hint = want_acceleration ? RasterModeHint::kPreferGPU
                                                   : RasterModeHint::kPreferCPU;

  // Re-creation will happen through Restore().
  // If the Canvas2DLayerBridge has just been created, possibly due to failed
  // attempts of Restore(), the layer would not exist, therefore, it will not
  // fall through this clause to try Restore() again
  if (layer_ && adjusted_hint == RasterModeHint::kPreferGPU &&
      !lose_context_in_background_ && !IsHibernating()) {
    return nullptr;
  }

  // We call GetOrCreateCanvasResourceProviderImpl directly here to prevent a
  // circular callstack from HTMLCanvasElement.
  resource_provider =
      resource_host_->GetOrCreateCanvasResourceProviderImpl(adjusted_hint);
  if (!resource_provider || !resource_provider->IsValid())
    return nullptr;

  // Calling to DidDraw because GetOrCreateResourceProvider created a new
  // provider and cleared it
  // TODO crbug/1090081: Check possibility to move DidDraw inside Clear.
  DidDraw();

  if (IsAccelerated() && !layer_) {
    layer_ = cc::TextureLayer::CreateForMailbox(this);
    layer_->SetIsDrawable(true);
    layer_->SetHitTestable(true);
    layer_->SetContentsOpaque(opacity_mode_ == kOpaque);
    layer_->SetBlendBackgroundColor(opacity_mode_ != kOpaque);
    layer_->SetNearestNeighbor(resource_host_->FilterQuality() ==
                               cc::PaintFlags::FilterQuality::kNone);
  }
  // After the page becomes visible and successfully restored the canvas
  // resource provider, set |lose_context_in_background_| to false.
  if (lose_context_in_background_)
    lose_context_in_background_ = false;

  if (!IsHibernating())
    return resource_provider;

  if (resource_provider->IsAccelerated()) {
    logger_->ReportHibernationEvent(kHibernationEndedNormally);
  } else {
    if (IsHidden()) {
      logger_->ReportHibernationEvent(
          kHibernationEndedWithSwitchToBackgroundRendering);
    } else {
      logger_->ReportHibernationEvent(kHibernationEndedWithFallbackToSW);
    }
  }

  PaintImageBuilder builder = PaintImageBuilder::WithDefault();
  builder.set_image(hibernation_image_, PaintImage::GetNextContentId());
  builder.set_id(PaintImage::GetNextId());
  resource_provider->RestoreBackBuffer(builder.TakePaintImage());
  hibernation_image_.reset();

  if (resource_host_) {
    // shouldBeDirectComposited() may have changed.
    resource_host_->SetNeedsCompositingUpdate();
  }
  return resource_provider;
}

cc::PaintCanvas* Canvas2DLayerBridge::GetPaintCanvas() {
  DCHECK(resource_host_);
  // We avoid only using GetOrCreateResourceProvider() here to skip the
  // IsValid/ContextLost checks since this is in hot code paths. The context
  // does not need to be valid here since only the recording canvas is used.
  if (!ResourceProvider() && !GetOrCreateResourceProvider())
    return nullptr;
  return ResourceProvider()->Canvas();
}

void Canvas2DLayerBridge::SetFilterQuality(
    cc::PaintFlags::FilterQuality filter_quality) {
  if (CanvasResourceProvider* resource_provider = ResourceProvider())
    resource_provider->SetFilterQuality(filter_quality);
  if (layer_)
    layer_->SetNearestNeighbor(filter_quality ==
                               cc::PaintFlags::FilterQuality::kNone);
}

void Canvas2DLayerBridge::SetIsInHiddenPage(bool hidden) {
  if (is_hidden_ == hidden)
    return;

  is_hidden_ = hidden;
  if (ResourceProvider())
    ResourceProvider()->SetResourceRecyclingEnabled(!IsHidden());

  if (!lose_context_in_background_ && !lose_context_in_background_scheduled_ &&
      ResourceProvider() && !context_lost_ && IsHidden() &&
      base::FeatureList::IsEnabled(
          ::features::kCanvasContextLostInBackground)) {
    lose_context_in_background_scheduled_ = true;
    if (dont_use_idle_scheduling_for_testing_) {
      Thread::Current()->GetDeprecatedTaskRunner()->PostTask(
          FROM_HERE, WTF::Bind(&LoseContextInBackgroundForTestingWrapper,
                               weak_ptr_factory_.GetWeakPtr()));
    } else {
      ThreadScheduler::Current()->PostIdleTask(
          FROM_HERE, WTF::Bind(&LoseContextInBackgroundWrapper,
                               weak_ptr_factory_.GetWeakPtr()));
    }
  } else if (IsHibernationEnabled() && ResourceProvider() && IsAccelerated() &&
             IsHidden() && !hibernation_scheduled_ &&
             !base::FeatureList::IsEnabled(
                 ::features::kCanvasContextLostInBackground)) {
    if (layer_)
      layer_->ClearTexture();
    logger_->ReportHibernationEvent(kHibernationScheduled);
    hibernation_scheduled_ = true;
    if (dont_use_idle_scheduling_for_testing_) {
      Thread::Current()->GetDeprecatedTaskRunner()->PostTask(
          FROM_HERE, WTF::Bind(&HibernateWrapperForTesting,
                               weak_ptr_factory_.GetWeakPtr()));
    } else {
      ThreadScheduler::Current()->PostIdleTask(
          FROM_HERE,
          WTF::Bind(&HibernateWrapper, weak_ptr_factory_.GetWeakPtr()));
    }
  }
  if (!IsHidden() && (IsHibernating() || lose_context_in_background_))
    GetOrCreateResourceProvider();  // Rude awakening
}

void Canvas2DLayerBridge::SetIsBeingDisplayed(bool displayed) {
  is_being_displayed_ = displayed;
  // If the canvas is no longer being displayed, stop using the rate
  // limiter.
  if (!is_being_displayed_) {
    frames_since_last_commit_ = 0;
    if (rate_limiter_) {
      rate_limiter_->Reset();
      rate_limiter_.reset(nullptr);
    }
  }
}

void Canvas2DLayerBridge::DrawFullImage(const cc::PaintImage& image) {
  GetPaintCanvas()->drawImage(image, 0, 0);
}

bool Canvas2DLayerBridge::WritePixels(const SkImageInfo& orig_info,
                                      const void* pixels,
                                      size_t row_bytes,
                                      int x,
                                      int y) {
  if (!GetOrCreateResourceProvider())
    return false;

  if (x <= 0 && y <= 0 && x + orig_info.width() >= size_.width() &&
      y + orig_info.height() >= size_.height()) {
    SkipQueuedDrawCommands();
  } else {
    FlushRecording();
    if (!GetOrCreateResourceProvider())
      return false;
  }
  have_recorded_draw_commands_ = false;

  bool wrote_pixels =
      ResourceProvider()->WritePixels(orig_info, pixels, row_bytes, x, y);
  if (wrote_pixels)
    last_record_tainted_by_write_pixels_ = true;

  return wrote_pixels;
}

void Canvas2DLayerBridge::SkipQueuedDrawCommands() {
  ResourceProvider()->SkipQueuedDrawCommands();
  have_recorded_draw_commands_ = false;
}

void Canvas2DLayerBridge::ClearPendingRasterTimers() {
  gpu::raster::RasterInterface* raster_interface = nullptr;
  if (IsAccelerated() && SharedGpuContext::ContextProviderWrapper() &&
      SharedGpuContext::ContextProviderWrapper()->ContextProvider()) {
    raster_interface = SharedGpuContext::ContextProviderWrapper()
                           ->ContextProvider()
                           ->RasterInterface();
  }

  if (raster_interface) {
    while (!pending_raster_timers_.IsEmpty()) {
      RasterTimer rt = pending_raster_timers_.TakeFirst();
      raster_interface->DeleteQueriesEXT(1, &rt.gl_query_id);
    }
  } else {
    pending_raster_timers_.clear();
  }
}

void Canvas2DLayerBridge::FinishRasterTimers(
    gpu::raster::RasterInterface* raster_interface) {
  // If the context was lost, then the old queries are not valid anymore
  if (!CheckResourceProviderValid()) {
    ClearPendingRasterTimers();
    return;
  }

  // Finish up any pending queries that are complete
  while (!pending_raster_timers_.IsEmpty()) {
    auto it = pending_raster_timers_.begin();
    GLuint complete = 1;
    raster_interface->GetQueryObjectuivEXT(
        it->gl_query_id, GL_QUERY_RESULT_AVAILABLE_NO_FLUSH_CHROMIUM_EXT,
        &complete);
    if (!complete) {
      break;
    }

    GLuint raw_gpu_duration = 0u;
    raster_interface->GetQueryObjectuivEXT(it->gl_query_id, GL_QUERY_RESULT_EXT,
                                           &raw_gpu_duration);
    base::TimeDelta gpu_duration_microseconds =
        base::Microseconds(raw_gpu_duration);
    base::TimeDelta total_time =
        gpu_duration_microseconds + it->cpu_raster_duration;

    base::TimeDelta min = base::Microseconds(1);
    base::TimeDelta max = base::Milliseconds(100);
    int num_buckets = 100;
    UMA_HISTOGRAM_CUSTOM_MICROSECONDS_TIMES(
        "Blink.Canvas.RasterDuration.Accelerated.GPU",
        gpu_duration_microseconds, min, max, num_buckets);
    UMA_HISTOGRAM_CUSTOM_MICROSECONDS_TIMES(
        "Blink.Canvas.RasterDuration.Accelerated.CPU", it->cpu_raster_duration,
        min, max, num_buckets);
    UMA_HISTOGRAM_CUSTOM_MICROSECONDS_TIMES(
        "Blink.Canvas.RasterDuration.Accelerated.Total", total_time, min, max,
        num_buckets);

    raster_interface->DeleteQueriesEXT(1, &it->gl_query_id);

    pending_raster_timers_.erase(it);
  }
}

void Canvas2DLayerBridge::FlushRecording(bool printing) {
  if (!have_recorded_draw_commands_ || !GetOrCreateResourceProvider())
    return;

  TRACE_EVENT0("cc", "Canvas2DLayerBridge::flushRecording");

  gpu::raster::RasterInterface* raster_interface = nullptr;
  if (IsAccelerated() && SharedGpuContext::ContextProviderWrapper() &&
      SharedGpuContext::ContextProviderWrapper()->ContextProvider()) {
    raster_interface = SharedGpuContext::ContextProviderWrapper()
                           ->ContextProvider()
                           ->RasterInterface();
    FinishRasterTimers(raster_interface);
  }

  // Sample one out of every kRasterMetricProbability frames to time
  // If the canvas is accelerated, we also need access to the raster_interface

  // We are using @dont_use_idle_scheduling_for_testing_ temporarily to always
  // measure while testing.
  const bool will_measure =
      dont_use_idle_scheduling_for_testing_ ||
      metrics_subsampler_.ShouldSample(kRasterMetricProbability);
  const bool measure_raster_metric =
      (raster_interface || !IsAccelerated()) && will_measure;

  RasterTimer rasterTimer;
  absl::optional<base::ElapsedTimer> timer;
  // Start Recording the raster duration
  if (measure_raster_metric) {
    if (IsAccelerated()) {
      GLuint gl_id = 0u;
      raster_interface->GenQueriesEXT(1, &gl_id);
      raster_interface->BeginQueryEXT(GL_COMMANDS_ISSUED_CHROMIUM, gl_id);
      rasterTimer.gl_query_id = gl_id;
    }
    timer.emplace();
  }

  last_recording_ =
      ResourceProvider()->FlushCanvasAndMaybePreserveRecording(printing);

  last_record_tainted_by_write_pixels_ = false;

  // Finish up the timing operation
  if (measure_raster_metric) {
    if (IsAccelerated()) {
      rasterTimer.cpu_raster_duration = timer->Elapsed();
      raster_interface->EndQueryEXT(GL_COMMANDS_ISSUED_CHROMIUM);
      pending_raster_timers_.push_back(rasterTimer);
    } else {
      UMA_HISTOGRAM_CUSTOM_MICROSECONDS_TIMES(
          "Blink.Canvas.RasterDuration.Unaccelerated", timer->Elapsed(),
          base::Microseconds(1), base::Milliseconds(100), 100);
    }
  }

  // Rastering the recording would have locked images, since we've flushed
  // all recorded ops, we should release all locked images as well.
  // A new null check on the resource provider is necessary just in case
  // the playback crashed the context.
  if (GetOrCreateResourceProvider())
    ResourceProvider()->ReleaseLockedImages();

  have_recorded_draw_commands_ = false;
}

bool Canvas2DLayerBridge::HasRateLimiterForTesting() {
  return !!rate_limiter_;
}

bool Canvas2DLayerBridge::IsValid() {
  return CheckResourceProviderValid();
}

bool Canvas2DLayerBridge::CheckResourceProviderValid() {
  if (IsHibernating())
    return true;
  if (!layer_ || raster_mode_ == RasterMode::kCPU)
    return true;
  if (context_lost_)
    return false;
  if (ResourceProvider() && IsAccelerated() &&
      ResourceProvider()->IsGpuContextLost()) {
    context_lost_ = true;
    ClearPendingRasterTimers();
    ResetResourceProvider();
    if (resource_host_)
      resource_host_->NotifyGpuContextLost();
    return false;
  }
  return !!GetOrCreateResourceProvider();
}

bool Canvas2DLayerBridge::Restore() {
  DCHECK(context_lost_);
  if (!IsAccelerated())
    return false;
  DCHECK(!ResourceProvider());

  if (layer_)
    layer_->ClearTexture();

  base::WeakPtr<WebGraphicsContext3DProviderWrapper> context_provider_wrapper =
      SharedGpuContext::ContextProviderWrapper();

  if (!context_provider_wrapper->ContextProvider()->IsContextLost()) {
    CanvasResourceProvider* resource_provider =
        resource_host_->GetOrCreateCanvasResourceProviderImpl(
            RasterModeHint::kPreferGPU);

    // The current paradigm does not support switching from accelerated to
    // non-accelerated, which would be tricky due to changes to the layer tree,
    // which can only happen at specific times during the document lifecycle.
    // Therefore, we can only accept the restored surface if it is accelerated.
    if (resource_provider && !IsAccelerated()) {
      resource_host_->ReplaceResourceProvider(nullptr);
      // FIXME: draw sad canvas picture into new buffer crbug.com/243842
    } else {
      context_lost_ = false;
    }
  }

  if (resource_host_)
    resource_host_->UpdateMemoryUsage();

  return ResourceProvider();
}

namespace {

// Adapter for wrapping a CanvasResourceReleaseCallback into a
// viz::ReleaseCallback
void ReleaseCanvasResource(CanvasResource::ReleaseCallback callback,
                           scoped_refptr<CanvasResource> canvas_resource,
                           const gpu::SyncToken& sync_token,
                           bool is_lost) {
  std::move(callback).Run(std::move(canvas_resource), sync_token, is_lost);
}

}  // unnamed namespace

bool Canvas2DLayerBridge::PrepareTransferableResource(
    cc::SharedBitmapIdRegistrar* bitmap_registrar,
    viz::TransferableResource* out_resource,
    viz::ReleaseCallback* out_release_callback) {
  DCHECK(layer_);  // This explodes if FinalizeFrame() was not called.

  frames_since_last_commit_ = 0;
  if (rate_limiter_)
    rate_limiter_->Reset();

  // If hibernating but not hidden, we want to wake up from hibernation.
  if (IsHibernating() && IsHidden())
    return false;

  if (!IsValid())
    return false;

  FlushRecording();

  // If the context is lost, we don't know if we should be producing GPU or
  // software frames, until we get a new context, since the compositor will
  // be trying to get a new context and may change modes.
  if (!GetOrCreateResourceProvider())
    return false;

  scoped_refptr<CanvasResource> frame =
      ResourceProvider()->ProduceCanvasResource();
  if (!frame || !frame->IsValid())
    return false;

  CanvasResource::ReleaseCallback release_callback;
  if (!frame->PrepareTransferableResource(out_resource, &release_callback,
                                          kUnverifiedSyncToken) ||
      *out_resource == layer_->current_transferable_resource()) {
    // If the resource did not change, the release will be handled correctly
    // when the callback from the previous frame is dispatched. But run the
    // |release_callback| to release the ref acquired above.
    std::move(release_callback)
        .Run(std::move(frame), gpu::SyncToken(), false /* is_lost */);
    return false;
  }
  // Note: frame is kept alive via a reference kept in out_release_callback.
  *out_release_callback = base::BindOnce(
      ReleaseCanvasResource, std::move(release_callback), std::move(frame));

  return true;
}

cc::Layer* Canvas2DLayerBridge::Layer() {
  // Trigger lazy layer creation
  GetOrCreateResourceProvider();
  return layer_.get();
}

void Canvas2DLayerBridge::DidDraw() {
  have_recorded_draw_commands_ = true;
}

void Canvas2DLayerBridge::FinalizeFrame(bool printing) {
  TRACE_EVENT0("blink", "Canvas2DLayerBridge::FinalizeFrame");

  // Make sure surface is ready for painting: fix the rendering mode now
  // because it will be too late during the paint invalidation phase.
  if (!GetOrCreateResourceProvider())
    return;

  FlushRecording(printing);
  if (is_being_displayed_) {
    ++frames_since_last_commit_;
    // Make sure the GPU is never more than two animation frames behind.
    constexpr unsigned kMaxCanvasAnimationBacklog = 2;
    if (frames_since_last_commit_ >=
        static_cast<int>(kMaxCanvasAnimationBacklog)) {
      if (IsAccelerated() && !rate_limiter_) {
        rate_limiter_ = std::make_unique<SharedContextRateLimiter>(
            kMaxCanvasAnimationBacklog);
      }
    }
  }

  if (rate_limiter_)
    rate_limiter_->Tick();
}

void Canvas2DLayerBridge::DoPaintInvalidation(const gfx::Rect& dirty_rect) {
  if (layer_ && raster_mode_ == RasterMode::kGPU)
    layer_->SetNeedsDisplayRect(dirty_rect);
}

scoped_refptr<StaticBitmapImage> Canvas2DLayerBridge::NewImageSnapshot() {
  if (snapshot_state_ == kInitialSnapshotState)
    snapshot_state_ = kDidAcquireSnapshot;
  if (IsHibernating())
    return UnacceleratedStaticBitmapImage::Create(hibernation_image_);
  if (!IsValid())
    return nullptr;
  // GetOrCreateResourceProvider needs to be called before FlushRecording, to
  // make sure "hint" is properly taken into account, as well as after
  // FlushRecording, in case the playback crashed the GPU context.
  if (!GetOrCreateResourceProvider())
    return nullptr;
  FlushRecording();
  if (!GetOrCreateResourceProvider())
    return nullptr;
  return ResourceProvider()->Snapshot();
}

void Canvas2DLayerBridge::WillOverwriteCanvas() {
  SkipQueuedDrawCommands();
}

void Canvas2DLayerBridge::Logger::ReportHibernationEvent(
    HibernationEvent event) {
  UMA_HISTOGRAM_ENUMERATION("Blink.Canvas.HibernationEvents", event);
}

}  // namespace blink
