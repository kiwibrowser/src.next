// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_LAYOUT_INTRINSIC_SIZING_INFO_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_LAYOUT_INTRINSIC_SIZING_INFO_H_

#include "third_party/blink/renderer/platform/wtf/allocator/allocator.h"
#include "ui/gfx/geometry/size_f.h"

namespace blink {

struct IntrinsicSizingInfo {
  DISALLOW_NEW();

  IntrinsicSizingInfo() : has_width(true), has_height(true) {}

  // Both size and aspect_ratio use logical coordinates.
  // Because they are using float instead of LayoutUnit, we can't use
  // LogicalSize here.
  gfx::SizeF size;
  gfx::SizeF aspect_ratio;
  bool has_width;
  bool has_height;

  void Transpose() {
    size.Transpose();
    aspect_ratio.Transpose();
    std::swap(has_width, has_height);
  }
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_CORE_LAYOUT_INTRINSIC_SIZING_INFO_H_
