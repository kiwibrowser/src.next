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

#include "third_party/blink/renderer/core/frame/page_scale_constraints.h"

#include <algorithm>

namespace blink {

PageScaleConstraints::PageScaleConstraints()
    : initial_scale(-1), minimum_scale(-1), maximum_scale(-1) {}

PageScaleConstraints::PageScaleConstraints(float initial,
                                           float minimum,
                                           float maximum)
    : initial_scale(initial), minimum_scale(minimum), maximum_scale(maximum) {}

void PageScaleConstraints::OverrideWith(const PageScaleConstraints& other) {
  if (other.initial_scale != -1) {
    initial_scale = other.initial_scale;
    if (minimum_scale != -1)
      minimum_scale = std::min(minimum_scale, other.initial_scale);
  }
  if (other.minimum_scale != -1)
    minimum_scale = other.minimum_scale;
  if (other.maximum_scale != -1)
    maximum_scale = other.maximum_scale;
  if (!other.layout_size.IsZero())
    layout_size = other.layout_size;
  ClampAll();
}

float PageScaleConstraints::ClampToConstraints(float page_scale_factor) const {
  if (page_scale_factor == -1)
    return page_scale_factor;
  if (minimum_scale != -1)
    page_scale_factor = std::max(page_scale_factor, minimum_scale);
  if (maximum_scale != -1)
    page_scale_factor = std::min(page_scale_factor, maximum_scale);
  return page_scale_factor;
}

void PageScaleConstraints::ClampAll() {
  if (minimum_scale != -1 && maximum_scale != -1)
    maximum_scale = std::max(minimum_scale, maximum_scale);
  initial_scale = ClampToConstraints(initial_scale);
}

void PageScaleConstraints::FitToContentsWidth(
    float contents_width,
    int view_width_not_including_scrollbars) {
  if (!contents_width || !view_width_not_including_scrollbars)
    return;

  // Clamp the minimum scale so that the viewport can't exceed the document
  // width.
  minimum_scale = std::max(
      minimum_scale, view_width_not_including_scrollbars / contents_width);

  ClampAll();
}

void PageScaleConstraints::ResolveAutoInitialScale() {
  // If the initial scale wasn't defined, set it to minimum scale now that we
  // know the real value.
  if (initial_scale == -1)
    initial_scale = minimum_scale;

  ClampAll();
}

bool PageScaleConstraints::operator==(const PageScaleConstraints& other) const {
  return layout_size == other.layout_size &&
         initial_scale == other.initial_scale &&
         minimum_scale == other.minimum_scale &&
         maximum_scale == other.maximum_scale;
}

}  // namespace blink
