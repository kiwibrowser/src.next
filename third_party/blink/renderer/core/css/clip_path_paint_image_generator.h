// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_CSS_CLIP_PATH_PAINT_IMAGE_GENERATOR_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_CSS_CLIP_PATH_PAINT_IMAGE_GENERATOR_H_

#include "third_party/blink/renderer/core/core_export.h"
#include "third_party/blink/renderer/core/css/native_paint_image_generator.h"

namespace gfx {
class RectF;
}

namespace blink {

class Image;
class LocalFrame;
class Node;

class CORE_EXPORT ClipPathPaintImageGenerator
    : public NativePaintImageGenerator {
 public:
  static ClipPathPaintImageGenerator* Create(LocalFrame& local_root);

  ~ClipPathPaintImageGenerator() override = default;

  using ClipPathPaintImageGeneratorCreateFunction =
      ClipPathPaintImageGenerator*(LocalFrame&);
  static void Init(ClipPathPaintImageGeneratorCreateFunction* create_function);

  virtual scoped_refptr<Image> Paint(float zoom,
                                     const gfx::RectF& reference_box,
                                     const Node&) = 0;
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_CORE_CSS_CLIP_PATH_PAINT_IMAGE_GENERATOR_H_
