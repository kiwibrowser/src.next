// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_PLATFORM_GRAPHICS_STATIC_BITMAP_IMAGE_H_
#define THIRD_PARTY_BLINK_RENDERER_PLATFORM_GRAPHICS_STATIC_BITMAP_IMAGE_H_

#include "base/memory/weak_ptr.h"
#include "base/notreached.h"
#include "gpu/command_buffer/common/mailbox_holder.h"
#include "third_party/blink/renderer/platform/graphics/canvas_color_params.h"
#include "third_party/blink/renderer/platform/graphics/graphics_types.h"
#include "third_party/blink/renderer/platform/graphics/image.h"
#include "third_party/blink/renderer/platform/wtf/casting.h"
#include "third_party/khronos/GLES2/gl2.h"
#include "third_party/skia/include/core/SkRefCnt.h"

namespace gpu {
namespace gles2 {
class GLES2Interface;
}
}  // namespace gpu

namespace blink {
class CanvasResourceProvider;

class PLATFORM_EXPORT StaticBitmapImage : public Image {
 public:
  // The ImageOrientation should be derived from the source of the image data.
  static scoped_refptr<StaticBitmapImage> Create(
      PaintImage,
      ImageOrientation = ImageOrientationEnum::kDefault);
  static scoped_refptr<StaticBitmapImage> Create(
      sk_sp<SkData> data,
      const SkImageInfo&,
      ImageOrientation = ImageOrientationEnum::kDefault);

  StaticBitmapImage(ImageOrientation orientation) : orientation_(orientation) {}

  bool IsStaticBitmapImage() const override { return true; }

  // Methods overridden by all sub-classes
  ~StaticBitmapImage() override = default;

  gfx::Size SizeWithConfig(SizeConfig) const final;

  virtual scoped_refptr<StaticBitmapImage> ConvertToColorSpace(
      sk_sp<SkColorSpace>,
      SkColorType = kN32_SkColorType) = 0;

  // Methods have common implementation for all sub-classes
  bool CurrentFrameIsComplete() override { return true; }
  void DestroyDecodedData() override {}

  // Methods that have a default implementation, and overridden by only one
  // sub-class
  virtual bool IsValid() const { return true; }
  virtual void Transfer() {}
  virtual bool IsOriginTopLeft() const { return true; }
  virtual bool SupportsDisplayCompositing() const { return true; }
  virtual bool IsOverlayCandidate() const { return false; }

  // Creates a non-gpu copy of the image, or returns this if image is already
  // non-gpu.
  virtual scoped_refptr<StaticBitmapImage> MakeUnaccelerated() { return this; }

  // Methods overridden by AcceleratedStaticBitmapImage only
  // Assumes the destination texture has already been allocated.
  virtual bool CopyToTexture(gpu::gles2::GLES2Interface*,
                             GLenum,
                             GLuint,
                             GLint,
                             bool,
                             bool,
                             const gfx::Point&,
                             const gfx::Rect&) {
    NOTREACHED();
    return false;
  }

  virtual bool CopyToResourceProvider(CanvasResourceProvider*) {
    NOTREACHED();
    return false;
  }

  virtual bool CopyToResourceProvider(CanvasResourceProvider* resource_provider,
                                      const gfx::Rect& copy_rect) {
    NOTREACHED();
    return false;
  }

  virtual void EnsureSyncTokenVerified() { NOTREACHED(); }
  virtual gpu::MailboxHolder GetMailboxHolder() const {
    NOTREACHED();
    return gpu::MailboxHolder();
  }
  virtual void UpdateSyncToken(const gpu::SyncToken&) { NOTREACHED(); }

  // For gpu based images the Usage is a bitmap indicating set of API(s) and
  // underlying gpu::SharedImage may be used with.
  // The gpu::SharedImageInterface is using uint32_t directly.
  virtual uint32_t GetUsage() const {
    NOTREACHED();
    return 0;
  }
  bool IsPremultiplied() const {
    return GetSkImageInfo().alphaType() == SkAlphaType::kPremul_SkAlphaType;
  }
  SkColorInfo GetSkColorInfo() const { return GetSkImageInfo().colorInfo(); }

  // Methods have exactly the same implementation for all sub-classes
  bool OriginClean() const { return is_origin_clean_; }
  void SetOriginClean(bool flag) { is_origin_clean_ = flag; }

  // StaticBitmapImage needs to store the orientation of the image itself,
  // because the underlying representations do not. If the bitmap represents
  // a non-default orientation it must be explicitly given in the constructor.
  ImageOrientation CurrentFrameOrientation() const override {
    return orientation_;
  }

  void SetOrientation(ImageOrientation orientation) {
    orientation_ = orientation;
  }

  // This function results in a readback due to using SkImage::readPixels().
  // Returns transparent black pixels if the input SkImageInfo.bounds() does
  // not intersect with the input image boundaries. When `apply_orientation`
  // is true this method will orient the data according to the source's EXIF
  // information.
  Vector<uint8_t> CopyImageData(const SkImageInfo& info,
                                bool apply_orientation);

  // Return the SkImageInfo of the internal representation of this image.
  virtual SkImageInfo GetSkImageInfo() const = 0;

 protected:
  // Helper for sub-classes
  void DrawHelper(cc::PaintCanvas*,
                  const cc::PaintFlags&,
                  const gfx::RectF&,
                  const gfx::RectF&,
                  const ImageDrawOptions&,
                  const PaintImage&);

  // The image orientation is stored here because it is only available when the
  // static image is created and the underlying representations do not store
  // the information. The property is set at construction based on the source of
  // the image data.
  ImageOrientation orientation_ = ImageOrientationEnum::kDefault;

  // The following property is here because the SkImage API doesn't expose the
  // info. It is applied to both UnacceleratedStaticBitmapImage and
  // AcceleratedStaticBitmapImage. To change this property, the call site would
  // have to call SetOriginClean().
  bool is_origin_clean_ = true;
};

template <>
struct DowncastTraits<StaticBitmapImage> {
  static bool AllowFrom(const Image& image) {
    return image.IsStaticBitmapImage();
  }
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_PLATFORM_GRAPHICS_STATIC_BITMAP_IMAGE_H_
