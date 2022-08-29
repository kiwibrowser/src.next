// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_PLATFORM_GRAPHICS_VIDEO_FRAME_SINK_BUNDLE_H_
#define THIRD_PARTY_BLINK_RENDERER_PLATFORM_GRAPHICS_VIDEO_FRAME_SINK_BUNDLE_H_

#include <stdint.h>

#include "base/callback.h"
#include "base/memory/weak_ptr.h"
#include "base/types/pass_key.h"
#include "build/build_config.h"
#include "components/viz/common/frame_sinks/begin_frame_args.h"
#include "components/viz/common/hit_test/hit_test_region_list.h"
#include "components/viz/common/quads/compositor_frame.h"
#include "components/viz/common/surfaces/frame_sink_bundle_id.h"
#include "components/viz/common/surfaces/frame_sink_id.h"
#include "components/viz/common/surfaces/local_surface_id.h"
#include "gpu/command_buffer/common/mailbox.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "services/viz/public/mojom/compositing/compositor_frame_sink.mojom-blink-forward.h"
#include "services/viz/public/mojom/compositing/frame_sink_bundle.mojom-blink.h"
#include "third_party/abseil-cpp/absl/types/optional.h"
#include "third_party/blink/public/mojom/frame_sinks/embedded_frame_sink.mojom-blink.h"
#include "third_party/blink/renderer/platform/platform_export.h"
#include "third_party/blink/renderer/platform/wtf/hash_map.h"
#include "third_party/blink/renderer/platform/wtf/vector.h"

namespace blink {

// Encapsulates a thread-local FrameSinkBundle connection for use by one or more
// VideoFrameSubmitters. This is responsible for demultiplexing batched
// communication from Viz, as well as for aggregating and apporopriately
// batching most outgoing communication to Viz on behalf of each
// VideoFrameSubmitter.
class PLATFORM_EXPORT VideoFrameSinkBundle
    : public viz::mojom::blink::FrameSinkBundleClient {
 public:
  VideoFrameSinkBundle(base::PassKey<VideoFrameSinkBundle>, uint32_t client_id);

  VideoFrameSinkBundle(const VideoFrameSinkBundle&) = delete;
  VideoFrameSinkBundle& operator=(const VideoFrameSinkBundle&) = delete;
  ~VideoFrameSinkBundle() override;

  // Acquires a lazily initialized VideoFrameSinkBundle instance for the calling
  // thread and given client ID. Note that in practice, a single renderer must
  // always call this with the same `client_id`.
  static VideoFrameSinkBundle& GetOrCreateSharedInstance(uint32_t client_id);

  // Acquires an instance that would be returned by GetOrCreateSharedInstance,
  // but does not create a new instance if one does not exist. Instead this
  // returns null in that case.
  static VideoFrameSinkBundle* GetSharedInstanceForTesting();

  // Ensures that the calling thread's shared instances are torn down.
  static void DestroySharedInstanceForTesting();

  // Overrides the EmbeddedFrameSinkProvider used to register new bundles in
  // tests. If null, any existing override is removed.
  static void SetFrameSinkProviderForTesting(
      mojom::blink::EmbeddedFrameSinkProvider* provider);

  // Sets a callback to be invoked on disconnection. Used by tests to observe
  // fake Viz connection lifetime.
  void set_disconnect_handler_for_testing(base::OnceClosure handler) {
    disconnect_handler_for_testing_ = std::move(handler);
  }

  const viz::FrameSinkBundleId& bundle_id() const { return id_; }

  // Adds a new client to this bundle, to receive batch notifications from Viz.
  // `client` must outlive this object or be explicitly removed by
  // RemoveClient() before being destroyed. Upon return, `receiver` and `remote`
  // are initialized with new connections to Viz for the sink. Returns a
  // WeakPtr to this VideoFrameSinkBundle which can be used by the client to
  // safely reference the it.
  base::WeakPtr<VideoFrameSinkBundle> AddClient(
      const viz::FrameSinkId& frame_sink_id,
      viz::mojom::blink::CompositorFrameSinkClient* client,
      mojo::Remote<mojom::blink::EmbeddedFrameSinkProvider>&
          frame_sink_provider,
      mojo::Receiver<viz::mojom::blink::CompositorFrameSinkClient>& receiver,
      mojo::Remote<viz::mojom::blink::CompositorFrameSink>& remote);
  void RemoveClient(const viz::FrameSinkId& id);

  // Helper methods used by VideoFrameSubmitters to communicate potentially
  // batched requests to Viz. These correspond closely to methods on the
  // CompositorFrameSink interface.
  void InitializeCompositorFrameSinkType(
      uint32_t sink_id,
      viz::mojom::blink::CompositorFrameSinkType);
  void SetNeedsBeginFrame(uint32_t sink_id, bool needs_begin_frame);
  void SubmitCompositorFrame(
      uint32_t sink_id,
      const viz::LocalSurfaceId& local_surface_id,
      viz::CompositorFrame frame,
      absl::optional<viz::HitTestRegionList> hit_test_region_list,
      uint64_t submit_time);
  void DidNotProduceFrame(uint32_t sink_id, const viz::BeginFrameAck& ack);
  void DidAllocateSharedBitmap(uint32_t sink_id,
                               base::ReadOnlySharedMemoryRegion region,
                               const gpu::Mailbox& id);
  void DidDeleteSharedBitmap(uint32_t sink_id, const gpu::Mailbox& id);
#if BUILDFLAG(IS_ANDROID)
  void SetThreadIds(uint32_t sink_id, const WTF::Vector<int32_t>& thread_ids);
#endif

  // viz::mojom::blink::FrameSinkBundleClient implementation:
  void FlushNotifications(
      WTF::Vector<viz::mojom::blink::BundledReturnedResourcesPtr> acks,
      WTF::Vector<viz::mojom::blink::BeginFrameInfoPtr> begin_frames,
      WTF::Vector<viz::mojom::blink::BundledReturnedResourcesPtr>
          reclaimed_resources) override;
  void OnBeginFramePausedChanged(uint32_t sink_id, bool paused) override;
  void OnCompositorFrameTransitionDirectiveProcessed(
      uint32_t sink_id,
      uint32_t sequence_id) override;

 private:
  void OnDisconnected();
  void FlushMessages();

  const viz::FrameSinkBundleId id_;
  mojo::Remote<viz::mojom::blink::FrameSinkBundle> bundle_;
  mojo::Receiver<viz::mojom::blink::FrameSinkBundleClient> receiver_{this};
  WTF::HashMap<uint32_t, viz::mojom::blink::CompositorFrameSinkClient*>
      clients_;

  bool defer_submissions_ = false;
  WTF::Vector<viz::mojom::blink::BundledFrameSubmissionPtr> submission_queue_;

  base::OnceClosure disconnect_handler_for_testing_;

  base::WeakPtrFactory<VideoFrameSinkBundle> weak_ptr_factory_{this};
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_PLATFORM_GRAPHICS_VIDEO_FRAME_SINK_BUNDLE_H_
