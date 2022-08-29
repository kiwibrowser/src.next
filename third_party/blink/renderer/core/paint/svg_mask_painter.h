// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_PAINT_SVG_MASK_PAINTER_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_PAINT_SVG_MASK_PAINTER_H_

#include "third_party/blink/renderer/platform/wtf/allocator/allocator.h"

namespace blink {

class DisplayItemClient;
class GraphicsContext;
class LayoutObject;

class SVGMaskPainter {
  STATIC_ONLY(SVGMaskPainter);

 public:
  static void Paint(GraphicsContext& context,
                    const LayoutObject& layout_object,
                    const DisplayItemClient& display_item_client);
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_CORE_PAINT_SVG_MASK_PAINTER_H_
