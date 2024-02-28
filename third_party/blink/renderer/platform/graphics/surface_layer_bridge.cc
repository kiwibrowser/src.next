// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/platform/graphics/surface_layer_bridge.h"

#include <utility>

#include "base/feature_list.h"
#include "cc/layers/layer.h"
#include "cc/layers/solid_color_layer.h"
#include "cc/layers/surface_layer.h"
#include "components/viz/common/surfaces/surface_id.h"
#include "components/viz/common/surfaces/surface_info.h"
#include "media/base/media_switches.h"
#include "third_party/blink/public/common/thread_safe_browser_interface_broker_proxy.h"
#include "third_party/blink/public/platform/platform.h"
#include "third_party/blink/renderer/platform/wtf/functional.h"
#include "ui/gfx/geometry/size.h"

namespace blink {

SurfaceLayerBridge::SurfaceLayerBridge(
    viz::FrameSinkId parent_frame_sink_id,
    ContainsVideo contains_video,
    WebSurfaceLayerBridgeObserver* observer,
    cc::UpdateSubmissionStateCB update_submission_state_callback)
    : observer_(observer),
      update_submission_state_callback_(
          std::move(update_submission_state_callback)),
      frame_sink_id_(Platform::Current()->GenerateFrameSinkId()),
      contains_video_(contains_video),
      parent_frame_sink_id_(parent_frame_sink_id) {
  Platform::Current()->GetBrowserInterfaceBroker()->GetInterface(
      embedded_frame_sink_provider_.BindNewPipeAndPassReceiver());
  // TODO(xlai): Ensure OffscreenCanvas commit() is still functional when a
  // frame-less HTML canvas's document is reparenting under another frame.
  // See crbug.com/683172.
  embedded_frame_sink_provider_->RegisterEmbeddedFrameSink(
      parent_frame_sink_id_, frame_sink_id_,
      receiver_.BindNewPipeAndPassRemote());
}

SurfaceLayerBridge::~SurfaceLayerBridge() = default;

void SurfaceLayerBridge::CreateSolidColorLayer() {
  // TODO(lethalantidote): Remove this logic. It should be covered by setting
  // the layer's opacity to false.
  solid_color_layer_ = cc::SolidColorLayer::Create();
  solid_color_layer_->SetBackgroundColor(SkColors::kTransparent);
  if (observer_)
    observer_->RegisterContentsLayer(solid_color_layer_.get());
}

void SurfaceLayerBridge::SetLocalSurfaceId(
    const viz::LocalSurfaceId& local_surface_id) {
  EmbedSurface(viz::SurfaceId(frame_sink_id_, local_surface_id));
}

void SurfaceLayerBridge::EmbedSurface(const viz::SurfaceId& surface_id) {
  surface_activated_ = true;
  if (solid_color_layer_) {
    if (observer_)
      observer_->UnregisterContentsLayer(solid_color_layer_.get());
    solid_color_layer_->RemoveFromParent();
    solid_color_layer_ = nullptr;
  }
  if (!surface_layer_) {
    // This covers non-video cases, where we don't create the SurfaceLayer
    // early.
    // TODO(lethalantidote): Eliminate this case. Once you do that, you can
    // also just store the surface_id and not the frame_sink_id.
    CreateSurfaceLayer();
  }

  current_surface_id_ = surface_id;

  surface_layer_->SetSurfaceId(surface_id,
                               cc::DeadlinePolicy::UseSpecifiedDeadline(0u));

  if (observer_) {
    observer_->OnWebLayerUpdated();
    observer_->OnSurfaceIdUpdated(surface_id);
  }

  surface_layer_->SetContentsOpaque(opaque_);
}

void SurfaceLayerBridge::BindSurfaceEmbedder(
    mojo::PendingReceiver<mojom::blink::SurfaceEmbedder> receiver) {
  if (surface_embedder_receiver_.is_bound()) {
    // After recovering from a GPU context loss we have to re-bind to a new
    // surface embedder.
    std::ignore = surface_embedder_receiver_.Unbind();
  }
  surface_embedder_receiver_.Bind(std::move(receiver));
}

cc::Layer* SurfaceLayerBridge::GetCcLayer() const {
  if (surface_layer_)
    return surface_layer_.get();

  return solid_color_layer_.get();
}

const viz::FrameSinkId& SurfaceLayerBridge::GetFrameSinkId() const {
  return frame_sink_id_;
}

void SurfaceLayerBridge::ClearObserver() {
  observer_ = nullptr;
}

void SurfaceLayerBridge::SetContentsOpaque(bool opaque) {
  // If the surface isn't activated, we have nothing to show, do not change
  // opacity (defaults to false on surface_layer creation).
  if (surface_layer_ && surface_activated_)
    surface_layer_->SetContentsOpaque(opaque);
  opaque_ = opaque;
}

void SurfaceLayerBridge::CreateSurfaceLayer() {
  surface_layer_ = cc::SurfaceLayer::Create(update_submission_state_callback_);

  // This surface_id is essentially just a placeholder for the real one we will
  // get in OnFirstSurfaceActivation. We need it so that we properly get a
  // WillDraw, which then pushes the first compositor frame.
  parent_local_surface_id_allocator_.GenerateId();
  current_surface_id_ = viz::SurfaceId(
      frame_sink_id_,
      parent_local_surface_id_allocator_.GetCurrentLocalSurfaceId());

  surface_layer_->SetSurfaceId(current_surface_id_,
                               cc::DeadlinePolicy::UseDefaultDeadline());

  surface_layer_->SetStretchContentToFillBounds(true);
  surface_layer_->SetIsDrawable(true);
  surface_layer_->SetHitTestable(true);
  surface_layer_->SetMayContainVideo(contains_video_ == ContainsVideo::kYes);

  if (observer_) {
    observer_->RegisterContentsLayer(surface_layer_.get());
  }
  // We ignore our opacity until we are sure that we have something to show,
  // as indicated by getting an OnFirstSurfaceActivation call.
  surface_layer_->SetContentsOpaque(false);
}

void SurfaceLayerBridge::RegisterFrameSinkHierarchy() {
  embedded_frame_sink_provider_->RegisterFrameSinkHierarchy(frame_sink_id_);
}

void SurfaceLayerBridge::UnregisterFrameSinkHierarchy() {
  embedded_frame_sink_provider_->UnregisterFrameSinkHierarchy(frame_sink_id_);
}

}  // namespace blink
