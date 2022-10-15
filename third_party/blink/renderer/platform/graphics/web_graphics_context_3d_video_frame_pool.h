// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_PLATFORM_GRAPHICS_WEB_GRAPHICS_CONTEXT_3D_VIDEO_FRAME_POOL_H_
#define THIRD_PARTY_BLINK_RENDERER_PLATFORM_GRAPHICS_WEB_GRAPHICS_CONTEXT_3D_VIDEO_FRAME_POOL_H_

#include "base/callback.h"
#include "base/memory/weak_ptr.h"
#include "components/viz/common/resources/resource_format.h"
#include "third_party/blink/renderer/platform/platform_export.h"
#include "third_party/skia/include/gpu/GrTypes.h"

namespace gfx {
class ColorSpace;
class Size;
}  // namespace gfx

namespace gpu {
class GpuMemoryBufferManager;
struct MailboxHolder;
namespace raster {
class RasterInterface;
}  // namespace raster
}  // namespace gpu

namespace media {
class RenderableGpuMemoryBufferVideoFramePool;
class VideoFrame;
}  // namespace media

namespace blink {

class WebGraphicsContext3DProviderWrapper;

// A video frame pool that will use a WebGraphicsContext3D to do an accelerated
// RGB to YUV conversion directly into a GpuMemoryBuffer-backed
// media::VideoFrame.
class PLATFORM_EXPORT WebGraphicsContext3DVideoFramePool {
 public:
  // This constructor is valid only on the main thread, as otherwise a
  // GpuMemoryBufferManager must be provided.
  explicit WebGraphicsContext3DVideoFramePool(
      base::WeakPtr<WebGraphicsContext3DProviderWrapper> weak_context_provider);
  WebGraphicsContext3DVideoFramePool(
      base::WeakPtr<WebGraphicsContext3DProviderWrapper> weak_context_provider,
      gpu::GpuMemoryBufferManager* gmb_manager);
  ~WebGraphicsContext3DVideoFramePool();

  gpu::raster::RasterInterface* GetRasterInterface() const;

  using FrameReadyCallback =
      base::OnceCallback<void(scoped_refptr<media::VideoFrame>)>;

  // On success, this function will issue return true and will call the
  // specified FrameCallback with the resulting VideoFrame when the frame is
  // ready. On failure this will return false. The resulting VideoFrame will
  // always be NV12. Note: In some paths `src_color_space` is ignored in favor
  // of the SharedImage color space associated with `src_mailbox_holder`. Note:
  // If the YUV to RGB matrix of `dst_color_space` is not Rec601, then this
  // function will use the matrix for Rec709 (it supports no other values). See
  // https://crbug.com/skia/12545.
  bool CopyRGBATextureToVideoFrame(viz::ResourceFormat src_format,
                                   const gfx::Size& src_size,
                                   const gfx::ColorSpace& src_color_space,
                                   GrSurfaceOrigin src_surface_origin,
                                   const gpu::MailboxHolder& src_mailbox_holder,
                                   const gfx::ColorSpace& dst_color_space,
                                   FrameReadyCallback callback);

  // Same as CopyRGBATextureToVideoFrame, but obtains the arguments from
  // src_video_frame, and applies relevant metadata to the resulting VideoFrame.
  // Always discards alpha. DCHECKs that src_video_frame is backed by a single
  // RGB texture.
  bool ConvertVideoFrame(scoped_refptr<media::VideoFrame> src_video_frame,
                         const gfx::ColorSpace& dst_color_space,
                         FrameReadyCallback callback);

  // This is a helper function to get whether GpuMemoryBuffer readback from
  // texture is enabled.
  static bool IsGpuMemoryBufferReadbackFromTextureEnabled();

 private:
  base::WeakPtr<blink::WebGraphicsContext3DProviderWrapper>
      weak_context_provider_;
  const std::unique_ptr<media::RenderableGpuMemoryBufferVideoFramePool> pool_;
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_PLATFORM_GRAPHICS_WEB_GRAPHICS_CONTEXT_3D_VIDEO_FRAME_POOL_H_
