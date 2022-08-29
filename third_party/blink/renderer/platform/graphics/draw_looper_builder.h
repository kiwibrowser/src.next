/*
 * Copyright (C) 2013 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef THIRD_PARTY_BLINK_RENDERER_PLATFORM_GRAPHICS_DRAW_LOOPER_BUILDER_H_
#define THIRD_PARTY_BLINK_RENDERER_PLATFORM_GRAPHICS_DRAW_LOOPER_BUILDER_H_

#include "third_party/blink/renderer/platform/platform_export.h"
#include "third_party/blink/renderer/platform/wtf/allocator/allocator.h"
#include "third_party/skia/include/core/SkRefCnt.h"
#include "third_party/skia/include/effects/SkLayerDrawLooper.h"

class SkDrawLooper;

namespace gfx {
class Vector2dF;
}

namespace blink {

class Color;

class PLATFORM_EXPORT DrawLooperBuilder final {
  STACK_ALLOCATED();

 public:
  enum ShadowTransformMode {
    kShadowRespectsTransforms,
    kShadowIgnoresTransforms
  };
  enum ShadowAlphaMode { kShadowRespectsAlpha, kShadowIgnoresAlpha };

  DrawLooperBuilder();
  // Implementing the copy constructor properly would require writing code to
  // copy the underlying SkLayerDrawLooper::Builder.
  DrawLooperBuilder(const DrawLooperBuilder&) = delete;
  DrawLooperBuilder& operator=(const DrawLooperBuilder&) = delete;

  ~DrawLooperBuilder();

  // Creates the SkDrawLooper and passes ownership to the caller. The builder
  // should not be used any more after calling this method.
  sk_sp<SkDrawLooper> DetachDrawLooper();

  void AddUnmodifiedContent();
  void AddShadow(const gfx::Vector2dF& offset,
                 float blur,
                 const Color&,
                 ShadowTransformMode = kShadowRespectsTransforms,
                 ShadowAlphaMode = kShadowRespectsAlpha);

 private:
  SkLayerDrawLooper::Builder sk_draw_looper_builder_;
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_PLATFORM_GRAPHICS_DRAW_LOOPER_BUILDER_H_
