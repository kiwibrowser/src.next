// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_FRAME_REMOTE_FRAME_CLIENT_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_FRAME_REMOTE_FRAME_CLIENT_H_

#include "cc/paint/paint_canvas.h"
#include "third_party/blink/public/common/tokens/tokens.h"
#include "third_party/blink/public/mojom/frame/frame_replication_state.mojom-blink-forward.h"
#include "third_party/blink/public/mojom/frame/remote_frame.mojom-blink-forward.h"
#include "third_party/blink/public/mojom/frame/tree_scope_type.mojom-blink-forward.h"
#include "third_party/blink/public/web/web_frame_load_type.h"
#include "third_party/blink/renderer/core/frame/frame_client.h"
#include "third_party/blink/renderer/core/frame/frame_types.h"
#include "third_party/blink/renderer/platform/graphics/touch_action.h"

namespace blink {

class RemoteFrameClient : public FrameClient {
 public:
  ~RemoteFrameClient() override = default;

  unsigned BackForwardLength() override = 0;

  // Create a new RemoteFrame child. This needs to be a client API
  // so that the appropriate WebRemoteFrameImpl is created first before
  // the core frame. In the future we should only create a WebRemoteFrame
  // when we pass a RemoteFrame handle outside of blink.
  virtual void CreateRemoteChild(
      const RemoteFrameToken& token,
      const absl::optional<FrameToken>& opener_frame_token,
      mojom::blink::TreeScopeType tree_scope_type,
      mojom::blink::FrameReplicationStatePtr replication_state,
      const base::UnguessableToken& devtools_frame_token,
      mojom::blink::RemoteFrameInterfacesFromBrowserPtr
          remote_frame_interfaces) = 0;
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_CORE_FRAME_REMOTE_FRAME_CLIENT_H_
