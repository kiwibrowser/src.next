// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/loader/threaded_icon_loader.h"

#include <algorithm>

#include "base/cxx17_backports.h"
#include "base/metrics/histogram_macros.h"
#include "skia/ext/image_operations.h"
#include "third_party/blink/public/mojom/fetch/fetch_api_request.mojom-blink.h"
#include "third_party/blink/renderer/core/execution_context/execution_context.h"
#include "third_party/blink/renderer/platform/graphics/skia/skia_utils.h"
#include "third_party/blink/renderer/platform/image-decoders/image_decoder.h"
#include "third_party/blink/renderer/platform/image-decoders/image_frame.h"
#include "third_party/blink/renderer/platform/image-decoders/segment_reader.h"
#include "third_party/blink/renderer/platform/loader/fetch/resource_loader_options.h"
#include "third_party/blink/renderer/platform/loader/fetch/resource_request.h"
#include "third_party/blink/renderer/platform/scheduler/public/post_cross_thread_task.h"
#include "third_party/blink/renderer/platform/scheduler/public/thread.h"
#include "third_party/blink/renderer/platform/scheduler/public/worker_pool.h"
#include "third_party/blink/renderer/platform/weborigin/kurl.h"
#include "third_party/blink/renderer/platform/wtf/cross_thread_copier_base.h"
#include "third_party/blink/renderer/platform/wtf/cross_thread_copier_gfx.h"
#include "third_party/blink/renderer/platform/wtf/cross_thread_functional.h"

namespace blink {

namespace {

void DecodeAndResizeImage(
    scoped_refptr<base::SingleThreadTaskRunner> task_runner,
    scoped_refptr<SegmentReader> data,
    gfx::Size resize_dimensions,
    CrossThreadOnceFunction<void(SkBitmap, double)> done_callback) {
  auto notify_complete = [&](SkBitmap icon, double resize_scale) {
    // This is needed so it can be moved cross-thread.
    icon.setImmutable();
    PostCrossThreadTask(*task_runner, FROM_HERE,
                        CrossThreadBindOnce(std::move(done_callback),
                                            std::move(icon), resize_scale));
  };

  std::unique_ptr<ImageDecoder> decoder = ImageDecoder::Create(
      std::move(data), /* data_complete= */ true,
      ImageDecoder::kAlphaPremultiplied, ImageDecoder::kDefaultBitDepth,
      ColorBehavior::TransformToSRGB());

  if (!decoder) {
    notify_complete(SkBitmap(), -1.0);
    return;
  }

  ImageFrame* image_frame = decoder->DecodeFrameBufferAtIndex(0);

  if (!image_frame) {
    notify_complete(SkBitmap(), -1.0);
    return;
  }

  SkBitmap decoded_icon = image_frame->Bitmap();
  if (resize_dimensions.IsEmpty()) {
    notify_complete(std::move(decoded_icon), 1.0);
    return;
  }

  // If the icon is larger than |resize_dimensions| permits, we need to
  // resize it as well. This can be done synchronously given that we're on a
  // background thread already.
  double scale = std::min(
      static_cast<double>(resize_dimensions.width()) / decoded_icon.width(),
      static_cast<double>(resize_dimensions.height()) / decoded_icon.height());

  if (scale >= 1.0) {
    notify_complete(std::move(decoded_icon), 1.0);
    return;
  }

  int resized_width =
      base::clamp(static_cast<int>(scale * decoded_icon.width()), 1,
                  resize_dimensions.width());
  int resized_height =
      base::clamp(static_cast<int>(scale * decoded_icon.height()), 1,
                  resize_dimensions.height());

  // Use the RESIZE_GOOD quality allowing the implementation to pick an
  // appropriate method for the resize. Can be increased to RESIZE_BETTER
  // or RESIZE_BEST if the quality looks poor.
  SkBitmap resized_icon = skia::ImageOperations::Resize(
      decoded_icon, skia::ImageOperations::RESIZE_GOOD, resized_width,
      resized_height);

  if (resized_icon.isNull()) {
    notify_complete(std::move(decoded_icon), 1.0);
    return;
  }

  notify_complete(std::move(resized_icon), scale);
}

}  // namespace

void ThreadedIconLoader::Start(
    ExecutionContext* execution_context,
    const ResourceRequestHead& resource_request,
    const absl::optional<gfx::Size>& resize_dimensions,
    IconCallback callback) {
  DCHECK(!stopped_);
  DCHECK(resource_request.Url().IsValid());
  DCHECK_EQ(resource_request.GetRequestContext(),
            mojom::blink::RequestContextType::IMAGE);
  DCHECK(!icon_callback_);

  icon_callback_ = std::move(callback);
  resize_dimensions_ = resize_dimensions;

  ResourceLoaderOptions resource_loader_options(
      execution_context->GetCurrentWorld());
  threadable_loader_ = MakeGarbageCollected<ThreadableLoader>(
      *execution_context, this, resource_loader_options);
  threadable_loader_->SetTimeout(resource_request.TimeoutInterval());
  threadable_loader_->Start(ResourceRequest(resource_request));

  start_time_ = base::TimeTicks::Now();
}

void ThreadedIconLoader::Stop() {
  stopped_ = true;
  if (threadable_loader_) {
    threadable_loader_->Cancel();
    threadable_loader_ = nullptr;
  }
}

void ThreadedIconLoader::DidReceiveData(const char* data, unsigned length) {
  if (!data_)
    data_ = SharedBuffer::Create();
  data_->Append(data, length);
}

void ThreadedIconLoader::DidFinishLoading(uint64_t resource_identifier) {
  if (stopped_)
    return;

  if (!data_) {
    std::move(icon_callback_).Run(SkBitmap(), -1);
    return;
  }

  UMA_HISTOGRAM_MEDIUM_TIMES("Blink.ThreadedIconLoader.LoadTime",
                             base::TimeTicks::Now() - start_time_);

  scoped_refptr<base::SingleThreadTaskRunner> task_runner =
      Thread::Current()->GetTaskRunner();

  worker_pool::PostTask(
      FROM_HERE,
      CrossThreadBindOnce(
          &DecodeAndResizeImage, std::move(task_runner),
          SegmentReader::CreateFromSharedBuffer(std::move(data_)),
          resize_dimensions_ ? *resize_dimensions_ : gfx::Size(),
          CrossThreadBindOnce(&ThreadedIconLoader::OnBackgroundTaskComplete,
                              WrapCrossThreadWeakPersistent(this))));
}

void ThreadedIconLoader::OnBackgroundTaskComplete(SkBitmap icon,
                                                  double resize_scale) {
  if (stopped_)
    return;
  std::move(icon_callback_).Run(std::move(icon), resize_scale);
}

void ThreadedIconLoader::DidFail(uint64_t, const ResourceError& error) {
  if (stopped_)
    return;
  std::move(icon_callback_).Run(SkBitmap(), -1);
}

void ThreadedIconLoader::DidFailRedirectCheck(uint64_t) {
  if (stopped_)
    return;
  std::move(icon_callback_).Run(SkBitmap(), -1);
}

void ThreadedIconLoader::Trace(Visitor* visitor) const {
  visitor->Trace(threadable_loader_);
  ThreadableLoaderClient::Trace(visitor);
}

}  // namespace blink
