// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_PLATFORM_GRAPHICS_CANVAS_RESOURCE_DISPATCHER_H_
#define THIRD_PARTY_BLINK_RENDERER_PLATFORM_GRAPHICS_CANVAS_RESOURCE_DISPATCHER_H_

#include <memory>

#include "base/memory/read_only_shared_memory_region.h"
#include "cc/paint/paint_flags.h"
#include "components/power_scheduler/power_mode_voter.h"
#include "components/viz/common/frame_sinks/begin_frame_args.h"
#include "components/viz/common/resources/resource_id.h"
#include "components/viz/common/surfaces/parent_local_surface_id_allocator.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "services/viz/public/mojom/compositing/compositor_frame_sink.mojom-blink.h"
#include "third_party/blink/public/mojom/frame_sinks/embedded_frame_sink.mojom-blink.h"
#include "third_party/blink/renderer/platform/graphics/resource_id_traits.h"
#include "third_party/blink/renderer/platform/platform_export.h"
#include "ui/gfx/geometry/size.h"

namespace blink {

class CanvasResource;

class CanvasResourceDispatcherClient {
 public:
  virtual bool BeginFrame() = 0;
  virtual void SetFilterQualityInResource(
      cc::PaintFlags::FilterQuality filter_quality) = 0;
};

class PLATFORM_EXPORT CanvasResourceDispatcher
    : public viz::mojom::blink::CompositorFrameSinkClient {
 public:
  base::WeakPtr<CanvasResourceDispatcher> GetWeakPtr() {
    return weak_ptr_factory_.GetWeakPtr();
  }

  CanvasResourceDispatcherClient* Client() { return client_; }

  enum {
    kInvalidPlaceholderCanvasId = -1,
  };

  CanvasResourceDispatcher(CanvasResourceDispatcherClient*,
                           uint32_t client_id,
                           uint32_t sink_id,
                           int placeholder_canvas_id,
                           const gfx::Size&);

  ~CanvasResourceDispatcher() override;
  void SetNeedsBeginFrame(bool);
  void SetSuspendAnimation(bool);
  bool NeedsBeginFrame() const { return needs_begin_frame_; }
  bool IsAnimationSuspended() const { return suspend_animation_; }
  void DispatchFrame(scoped_refptr<CanvasResource>,
                     base::TimeTicks commit_start_time,
                     const SkIRect& damage_rect,
                     bool needs_vertical_flip,
                     bool is_opaque);
  void ReclaimResource(viz::ResourceId);
  void DispatchFrameSync(scoped_refptr<CanvasResource>,
                         base::TimeTicks commit_start_time,
                         const SkIRect& damage_rect,
                         bool needs_vertical_flip,
                         bool is_opaque);
  void ReplaceBeginFrameAck(const viz::BeginFrameArgs& args) {
    current_begin_frame_ack_ = viz::BeginFrameAck(args, true);
  }
  bool HasTooManyPendingFrames() const;

  void Reshape(const gfx::Size&);

  // viz::mojom::blink::CompositorFrameSinkClient implementation.
  void DidReceiveCompositorFrameAck(
      WTF::Vector<viz::ReturnedResource> resources) final;
  void OnBeginFrame(
      const viz::BeginFrameArgs&,
      const WTF::HashMap<uint32_t, viz::FrameTimingDetails>&) final;
  void OnBeginFramePausedChanged(bool paused) final {}
  void ReclaimResources(WTF::Vector<viz::ReturnedResource> resources) final;
  void OnCompositorFrameTransitionDirectiveProcessed(
      uint32_t sequence_id) final {}

  void DidAllocateSharedBitmap(base::ReadOnlySharedMemoryRegion region,
                               const gpu::Mailbox& id);
  void DidDeleteSharedBitmap(const gpu::Mailbox& id);

  void SetFilterQuality(cc::PaintFlags::FilterQuality filter_quality);
  void SetPlaceholderCanvasDispatcher(int placeholder_canvas_id);

 private:
  friend class CanvasResourceDispatcherTest;
  struct FrameResource;

  using ResourceMap =
      HashMap<viz::ResourceId, std::unique_ptr<FrameResource>, ResourceIdHash>;

  bool PrepareFrame(scoped_refptr<CanvasResource>,
                    base::TimeTicks commit_start_time,
                    const SkIRect& damage_rect,
                    bool needs_vertical_flip,
                    bool is_opaque,
                    viz::CompositorFrame* frame);

  // Surface-related
  viz::ParentLocalSurfaceIdAllocator parent_local_surface_id_allocator_;
  const viz::FrameSinkId frame_sink_id_;

  gfx::Size size_;
  bool change_size_for_next_commit_;
  bool suspend_animation_ = false;
  bool needs_begin_frame_ = false;
  int pending_compositor_frames_ = 0;

  void SetNeedsBeginFrameInternal();

  bool VerifyImageSize(const gfx::Size&);
  void PostImageToPlaceholderIfNotBlocked(scoped_refptr<CanvasResource>,
                                          viz::ResourceId resource_id);
  // virtual for testing
  virtual void PostImageToPlaceholder(scoped_refptr<CanvasResource>,
                                      viz::ResourceId resource_id);

  void ReclaimResourceInternal(viz::ResourceId resource_id);
  void ReclaimResourceInternal(const ResourceMap::iterator&);

  mojo::Remote<viz::mojom::blink::CompositorFrameSink> sink_;
  mojo::Remote<mojom::blink::SurfaceEmbedder> surface_embedder_;
  mojo::Receiver<viz::mojom::blink::CompositorFrameSinkClient> receiver_{this};

  int placeholder_canvas_id_;

  viz::ResourceIdGenerator id_generator_;
  ResourceMap resources_;

  viz::FrameTokenGenerator next_frame_token_;

  // The latest_unposted_resource_id_ always refers to the Id of the frame
  // resource used by the latest_unposted_image_.
  scoped_refptr<CanvasResource> latest_unposted_image_;
  viz::ResourceId latest_unposted_resource_id_;
  unsigned num_unreclaimed_frames_posted_;

  viz::BeginFrameAck current_begin_frame_ack_;

  CanvasResourceDispatcherClient* client_;

  std::unique_ptr<power_scheduler::PowerModeVoter> animation_power_mode_voter_;

  base::WeakPtrFactory<CanvasResourceDispatcher> weak_ptr_factory_{this};
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_PLATFORM_GRAPHICS_CANVAS_RESOURCE_DISPATCHER_H_
