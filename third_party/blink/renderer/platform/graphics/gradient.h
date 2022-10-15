/*
 * Copyright (C) 2006, 2007, 2008, 2011 Apple Inc. All rights reserved.
 * Copyright (C) 2007 Alp Toker <alp@atoker.com>
 * Copyright (C) 2008 Torch Mobile, Inc.
 * Copyright (C) 2013 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE COMPUTER, INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE COMPUTER, INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef THIRD_PARTY_BLINK_RENDERER_PLATFORM_GRAPHICS_GRADIENT_H_
#define THIRD_PARTY_BLINK_RENDERER_PLATFORM_GRAPHICS_GRADIENT_H_

#include "base/memory/scoped_refptr.h"
#include "cc/paint/paint_flags.h"
#include "third_party/blink/renderer/platform/graphics/color.h"
#include "third_party/blink/renderer/platform/graphics/graphics_types.h"
#include "third_party/blink/renderer/platform/graphics/paint/paint_shader.h"
#include "third_party/blink/renderer/platform/platform_export.h"
#include "third_party/blink/renderer/platform/wtf/allocator/allocator.h"
#include "third_party/blink/renderer/platform/wtf/ref_counted.h"
#include "third_party/blink/renderer/platform/wtf/vector.h"
#include "third_party/skia/include/core/SkRefCnt.h"

class SkMatrix;

namespace gfx {
class PointF;
}

namespace blink {

struct ImageDrawOptions;
class DarkModeFilter;

class PLATFORM_EXPORT Gradient : public RefCounted<Gradient> {
  USING_FAST_MALLOC(Gradient);

 public:
  enum class Type { kLinear, kRadial, kConic };

  enum class ColorInterpolation {
    kPremultiplied,
    kUnpremultiplied,
  };

  enum class DegenerateHandling {
    kAllow,
    kDisallow,
  };

  static scoped_refptr<Gradient> CreateLinear(
      const gfx::PointF& p0,
      const gfx::PointF& p1,
      GradientSpreadMethod = kSpreadMethodPad,
      ColorInterpolation = ColorInterpolation::kUnpremultiplied,
      DegenerateHandling = DegenerateHandling::kAllow);

  static scoped_refptr<Gradient> CreateRadial(
      const gfx::PointF& p0,
      float r0,
      const gfx::PointF& p1,
      float r1,
      float aspect_ratio = 1,
      GradientSpreadMethod = kSpreadMethodPad,
      ColorInterpolation = ColorInterpolation::kUnpremultiplied,
      DegenerateHandling = DegenerateHandling::kAllow);

  static scoped_refptr<Gradient> CreateConic(
      const gfx::PointF& position,
      float rotation,
      float start_angle,
      float end_angle,
      GradientSpreadMethod = kSpreadMethodPad,
      ColorInterpolation = ColorInterpolation::kUnpremultiplied,
      DegenerateHandling = DegenerateHandling::kAllow);

  Gradient(const Gradient&) = delete;
  Gradient& operator=(const Gradient&) = delete;
  virtual ~Gradient();

  Type GetType() const { return type_; }

  struct ColorStop {
    DISALLOW_NEW();
    double stop;
    Color color;

    ColorStop(double s, const Color& c) : stop(s), color(c) {}
  };
  void AddColorStop(const ColorStop&);
  void AddColorStop(double value, const Color& color) {
    AddColorStop(ColorStop(value, color));
  }
  void AddColorStops(const Vector<Gradient::ColorStop>&);

  void ApplyToFlags(cc::PaintFlags&,
                    const SkMatrix& local_matrix,
                    const ImageDrawOptions& draw_options);
  void SetColorInterpolationSpace(
      Color::ColorInterpolationSpace color_space_interpolation_space,
      Color::HueInterpolationMethod hue_interpolation_method) {
    color_space_interpolation_space_ = color_space_interpolation_space;
    hue_interpolation_method_ = hue_interpolation_method;
  }

  DarkModeFilter& EnsureDarkModeFilter();

 protected:
  Gradient(Type, GradientSpreadMethod, ColorInterpolation, DegenerateHandling);

  using ColorBuffer = Vector<SkColor, 8>;
  using OffsetBuffer = Vector<SkScalar, 8>;
  virtual sk_sp<PaintShader> CreateShader(const ColorBuffer&,
                                          const OffsetBuffer&,
                                          SkTileMode,
                                          uint32_t flags,
                                          const SkMatrix&,
                                          SkColor) const = 0;

  DegenerateHandling GetDegenerateHandling() const {
    return degenerate_handling_;
  }

 private:
  sk_sp<PaintShader> CreateShaderInternal(const SkMatrix& local_matrix);

  void SortStopsIfNecessary() const;
  void FillSkiaStops(ColorBuffer&, OffsetBuffer&) const;

  const Type type_;
  const GradientSpreadMethod spread_method_;
  const ColorInterpolation color_interpolation_;
  const DegenerateHandling degenerate_handling_;

  mutable Vector<ColorStop, 2> stops_;
  mutable bool stops_sorted_;
  bool is_dark_mode_enabled_ = false;
  std::unique_ptr<DarkModeFilter> dark_mode_filter_;

  mutable sk_sp<PaintShader> cached_shader_;
  mutable sk_sp<SkColorFilter> color_filter_;

  Color::ColorInterpolationSpace color_space_interpolation_space_;
  Color::HueInterpolationMethod hue_interpolation_method_;
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_PLATFORM_GRAPHICS_GRADIENT_H_
