// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_PLATFORM_GRAPHICS_UNACCELERATED_STATIC_BITMAP_IMAGE_H_
#define THIRD_PARTY_BLINK_RENDERER_PLATFORM_GRAPHICS_UNACCELERATED_STATIC_BITMAP_IMAGE_H_

#include "base/memory/weak_ptr.h"
#include "base/task/single_thread_task_runner.h"
#include "base/threading/thread_checker.h"
#include "third_party/blink/renderer/platform/graphics/static_bitmap_image.h"
#include "third_party/blink/renderer/platform/scheduler/public/thread.h"

namespace blink {

class PLATFORM_EXPORT UnacceleratedStaticBitmapImage final
    : public StaticBitmapImage {
 public:
  ~UnacceleratedStaticBitmapImage() override;

  // The ImageOrientation should be derived from the source of the image data.
  static scoped_refptr<UnacceleratedStaticBitmapImage> Create(
      sk_sp<SkImage>,
      ImageOrientation orientation = ImageOrientationEnum::kDefault);
  static scoped_refptr<UnacceleratedStaticBitmapImage> Create(
      PaintImage,
      ImageOrientation orientation = ImageOrientationEnum::kDefault);

  bool CurrentFrameKnownToBeOpaque() override;
  scoped_refptr<StaticBitmapImage> ConvertToColorSpace(sk_sp<SkColorSpace>,
                                                       SkColorType) override;

  void Draw(cc::PaintCanvas*,
            const cc::PaintFlags&,
            const gfx::RectF& dst_rect,
            const gfx::RectF& src_rect,
            const ImageDrawOptions&) override;

  PaintImage PaintImageForCurrentFrame() override;

  void Transfer() final;

  bool CopyToResourceProvider(CanvasResourceProvider*) override;

 private:
  UnacceleratedStaticBitmapImage(sk_sp<SkImage>, ImageOrientation);
  UnacceleratedStaticBitmapImage(PaintImage, ImageOrientation);
  SkImageInfo GetSkImageInfoInternal() const override;

  PaintImage paint_image_;
  THREAD_CHECKER(thread_checker_);

  sk_sp<SkImage> original_skia_image_;
  scoped_refptr<base::SingleThreadTaskRunner> original_skia_image_task_runner_;
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_PLATFORM_GRAPHICS_UNACCELERATED_STATIC_BITMAP_IMAGE_H_
