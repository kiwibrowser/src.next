/*
 * Copyright (c) 2008, Google Inc. All rights reserved.
 * Copyright (C) 2009 Dirk Schulze <krit@webkit.org>
 * Copyright (C) 2010 Torch Mobile (Beijing) Co. Ltd. All rights reserved.
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

#include "third_party/blink/renderer/platform/graphics/interpolation_space.h"

#include "base/notreached.h"
#include "third_party/skia/include/core/SkColorFilter.h"

namespace blink {

namespace interpolation_space_utilities {

namespace {

sk_sp<SkColorFilter> GetConversionFilter(
    InterpolationSpace dst_interpolation_space,
    InterpolationSpace src_interpolation_space) {
  // Identity.
  if (src_interpolation_space == dst_interpolation_space)
    return nullptr;

  switch (dst_interpolation_space) {
    case kInterpolationSpaceLinear:
      return SkColorFilters::SRGBToLinearGamma();
    case kInterpolationSpaceSRGB:
      return SkColorFilters::LinearToSRGBGamma();
  }

  NOTREACHED();
  return nullptr;
}

}  // namespace

Color ConvertColor(const Color& src_color,
                   InterpolationSpace dst_interpolation_space,
                   InterpolationSpace src_interpolation_space) {
  sk_sp<SkColorFilter> conversion_filter =
      GetConversionFilter(dst_interpolation_space, src_interpolation_space);
  // TODO(https://crbug.com/1351544): This should be SkColor4f and not Color.
  return conversion_filter ? Color::FromRGBA32(conversion_filter->filterColor(
                                 src_color.Rgb()))
                           : src_color;
}

sk_sp<SkColorFilter> CreateInterpolationSpaceFilter(
    InterpolationSpace src_interpolation_space,
    InterpolationSpace dst_interpolation_space) {
  return GetConversionFilter(dst_interpolation_space, src_interpolation_space);
}

}  // namespace interpolation_space_utilities

}  // namespace blink
