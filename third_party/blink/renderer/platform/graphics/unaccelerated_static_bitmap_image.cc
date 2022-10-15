// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/platform/graphics/unaccelerated_static_bitmap_image.h"

#include "base/process/memory.h"
#include "components/viz/common/gpu/context_provider.h"
#include "third_party/blink/public/platform/platform.h"
#include "third_party/blink/public/platform/web_graphics_context_3d_provider.h"
#include "third_party/blink/renderer/platform/graphics/accelerated_static_bitmap_image.h"
#include "third_party/blink/renderer/platform/graphics/canvas_resource_provider.h"
#include "third_party/blink/renderer/platform/graphics/graphics_context.h"
#include "third_party/blink/renderer/platform/graphics/web_graphics_context_3d_provider_wrapper.h"
#include "third_party/blink/renderer/platform/scheduler/public/post_cross_thread_task.h"
#include "third_party/blink/renderer/platform/scheduler/public/thread.h"
#include "third_party/blink/renderer/platform/wtf/cross_thread_copier_skia.h"
#include "third_party/blink/renderer/platform/wtf/cross_thread_functional.h"
#include "third_party/skia/include/core/SkImage.h"

namespace blink {

scoped_refptr<UnacceleratedStaticBitmapImage>
UnacceleratedStaticBitmapImage::Create(sk_sp<SkImage> image,
                                       ImageOrientation orientation) {
  if (!image)
    return nullptr;
  DCHECK(!image->isTextureBacked());
  return base::AdoptRef(
      new UnacceleratedStaticBitmapImage(std::move(image), orientation));
}

UnacceleratedStaticBitmapImage::UnacceleratedStaticBitmapImage(
    sk_sp<SkImage> image,
    ImageOrientation orientation)
    : StaticBitmapImage(orientation) {
  CHECK(image);
  DCHECK(!image->isLazyGenerated());
  paint_image_ =
      CreatePaintImageBuilder()
          .set_image(std::move(image), cc::PaintImage::GetNextContentId())
          .TakePaintImage();
}

scoped_refptr<UnacceleratedStaticBitmapImage>
UnacceleratedStaticBitmapImage::Create(PaintImage image,
                                       ImageOrientation orientation) {
  return base::AdoptRef(
      new UnacceleratedStaticBitmapImage(std::move(image), orientation));
}

UnacceleratedStaticBitmapImage::UnacceleratedStaticBitmapImage(
    PaintImage image,
    ImageOrientation orientation)
    : StaticBitmapImage(orientation), paint_image_(std::move(image)) {
  DCHECK(paint_image_);
}

UnacceleratedStaticBitmapImage::~UnacceleratedStaticBitmapImage() {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  if (!original_skia_image_)
    return;

  if (!original_skia_image_task_runner_->BelongsToCurrentThread()) {
    PostCrossThreadTask(
        *original_skia_image_task_runner_, FROM_HERE,
        CrossThreadBindOnce([](sk_sp<SkImage> image) { image.reset(); },
                            std::move(original_skia_image_)));
  } else {
    original_skia_image_.reset();
  }
}

bool UnacceleratedStaticBitmapImage::CurrentFrameKnownToBeOpaque() {
  return paint_image_.IsOpaque();
}

void UnacceleratedStaticBitmapImage::Draw(
    cc::PaintCanvas* canvas,
    const cc::PaintFlags& flags,
    const gfx::RectF& dst_rect,
    const gfx::RectF& src_rect,
    const ImageDrawOptions& draw_options) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  auto image = PaintImageForCurrentFrame();
  if (image.may_be_lcp_candidate() != draw_options.may_be_lcp_candidate) {
    image = PaintImageBuilder::WithCopy(std::move(image))
                .set_may_be_lcp_candidate(draw_options.may_be_lcp_candidate)
                .TakePaintImage();
  }
  StaticBitmapImage::DrawHelper(canvas, flags, dst_rect, src_rect, draw_options,
                                image);
}

PaintImage UnacceleratedStaticBitmapImage::PaintImageForCurrentFrame() {
  return paint_image_;
}

void UnacceleratedStaticBitmapImage::Transfer() {
  DETACH_FROM_THREAD(thread_checker_);

  original_skia_image_ = paint_image_.GetSwSkImage();
  original_skia_image_task_runner_ =
      Thread::Current()->GetDeprecatedTaskRunner();
}

scoped_refptr<StaticBitmapImage>
UnacceleratedStaticBitmapImage::ConvertToColorSpace(
    sk_sp<SkColorSpace> color_space,
    SkColorType color_type) {
  DCHECK(color_space);

  sk_sp<SkImage> skia_image = PaintImageForCurrentFrame().GetSwSkImage();
  // If we don't need to change the color type, use SkImage::makeColorSpace()
  if (skia_image->colorType() == color_type) {
    skia_image = skia_image->makeColorSpace(color_space);
  } else {
    skia_image =
        skia_image->makeColorTypeAndColorSpace(color_type, color_space);
  }
  if (UNLIKELY(!skia_image)) {
    // Null value indicates that skia failed to allocate the destination
    // bitmap.
    base::TerminateBecauseOutOfMemory(
        skia_image->imageInfo().makeColorType(color_type).computeMinByteSize());
  }
  return UnacceleratedStaticBitmapImage::Create(skia_image, orientation_);
}

bool UnacceleratedStaticBitmapImage::CopyToResourceProvider(
    CanvasResourceProvider* resource_provider) {
  DCHECK(resource_provider);

  sk_sp<SkImage> image = paint_image_.GetSwSkImage();
  if (!image)
    return false;

  SkPixmap pixmap;
  if (!image->peekPixels(&pixmap))
    return false;

  const void* pixels = pixmap.addr();
  const size_t row_bytes = pixmap.rowBytes();
  std::vector<uint8_t> flipped;
  DCHECK(IsOriginTopLeft());
  if (!resource_provider->IsOriginTopLeft()) {
    const int height = pixmap.height();
    flipped.resize(row_bytes * height);
    for (int i = 0; i < height; ++i) {
      memcpy(flipped.data() + i * row_bytes,
             static_cast<const uint8_t*>(pixels) + (height - 1 - i) * row_bytes,
             row_bytes);
    }
    pixels = flipped.data();
  }

  return resource_provider->WritePixels(pixmap.info(), pixels, row_bytes,
                                        /*x=*/0, /*y=*/0);
}

SkImageInfo UnacceleratedStaticBitmapImage::GetSkImageInfoInternal() const {
  return paint_image_.GetSkImageInfo().makeWH(paint_image_.width(),
                                              paint_image_.height());
}

}  // namespace blink
