/*
 * Copyright (C) 2006, 2007, 2008, 2010 Apple Inc. All rights reserved.
 * Copyright (C) 2007 Alp Toker <alp@atoker.com>
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

#include "third_party/blink/renderer/platform/graphics/gradient.h"

#include <algorithm>
#include "third_party/abseil-cpp/absl/types/optional.h"
#include "third_party/blink/renderer/platform/graphics/dark_mode_settings_builder.h"
#include "third_party/blink/renderer/platform/graphics/graphics_context.h"
#include "third_party/blink/renderer/platform/graphics/paint/paint_shader.h"
#include "third_party/blink/renderer/platform/graphics/skia/skia_utils.h"
#include "third_party/skia/include/core/SkColor.h"
#include "third_party/skia/include/core/SkMatrix.h"
#include "third_party/skia/include/core/SkShader.h"
#include "third_party/skia/include/effects/SkGradientShader.h"
#include "ui/gfx/geometry/rect_f.h"

namespace blink {

Gradient::Gradient(Type type,
                   GradientSpreadMethod spread_method,
                   ColorInterpolation interpolation,
                   DegenerateHandling degenerate_handling)
    : type_(type),
      spread_method_(spread_method),
      color_interpolation_(interpolation),
      degenerate_handling_(degenerate_handling),
      stops_sorted_(true) {}

Gradient::~Gradient() = default;

static inline bool CompareStops(const Gradient::ColorStop& a,
                                const Gradient::ColorStop& b) {
  return a.stop < b.stop;
}

void Gradient::AddColorStop(const Gradient::ColorStop& stop) {
  if (stops_.IsEmpty()) {
    stops_sorted_ = true;
  } else {
    stops_sorted_ = stops_sorted_ && CompareStops(stops_.back(), stop);
  }

  stops_.push_back(stop);
  cached_shader_.reset();
}

void Gradient::AddColorStops(const Vector<Gradient::ColorStop>& stops) {
  for (const auto& stop : stops)
    AddColorStop(stop);
}

void Gradient::SortStopsIfNecessary() const {
  if (stops_sorted_)
    return;

  stops_sorted_ = true;

  if (!stops_.size())
    return;

  std::stable_sort(stops_.begin(), stops_.end(), CompareStops);
}

// Collect sorted stop position and color information into the pos and colors
// buffers, ensuring stops at both 0.0 and 1.0.
// TODO(fmalita): theoretically Skia should provide the same 0.0/1.0 padding
// (making this logic redundant), but in practice there are rendering diffs;
// investigate.
void Gradient::FillSkiaStops(ColorBuffer& colors, OffsetBuffer& pos) const {
  if (stops_.IsEmpty()) {
    // A gradient with no stops must be transparent black.
    pos.push_back(WebCoreDoubleToSkScalar(0));
    colors.push_back(SK_ColorTRANSPARENT);
  } else if (stops_.front().stop > 0) {
    // Copy the first stop to 0.0. The first stop position may have a slight
    // rounding error, but we don't care in this float comparison, since
    // 0.0 comes through cleanly and people aren't likely to want a gradient
    // with a stop at (0 + epsilon).
    pos.push_back(WebCoreDoubleToSkScalar(0));
    if (color_filter_) {
      colors.push_back(
          color_filter_->filterColor(SkColor(stops_.front().color)));
    } else {
      colors.push_back(SkColor(stops_.front().color));
    }
  }

  for (const auto& stop : stops_) {
    pos.push_back(WebCoreDoubleToSkScalar(stop.stop));
    if (color_filter_)
      colors.push_back(color_filter_->filterColor(SkColor(stop.color)));
    else
      colors.push_back(SkColor(stop.color));
  }

  // Copy the last stop to 1.0 if needed. See comment above about this float
  // comparison.
  DCHECK(!pos.IsEmpty());
  if (pos.back() < 1) {
    pos.push_back(WebCoreDoubleToSkScalar(1));
    colors.push_back(colors.back());
  }
}

sk_sp<PaintShader> Gradient::CreateShaderInternal(
    const SkMatrix& local_matrix) {
  SortStopsIfNecessary();
  DCHECK(stops_sorted_);

  ColorBuffer colors;
  colors.ReserveCapacity(stops_.size());
  OffsetBuffer pos;
  pos.ReserveCapacity(stops_.size());

  FillSkiaStops(colors, pos);
  DCHECK_GE(colors.size(), 2ul);
  DCHECK_EQ(pos.size(), colors.size());

  SkTileMode tile = SkTileMode::kClamp;
  switch (spread_method_) {
    case kSpreadMethodReflect:
      tile = SkTileMode::kMirror;
      break;
    case kSpreadMethodRepeat:
      tile = SkTileMode::kRepeat;
      break;
    case kSpreadMethodPad:
      tile = SkTileMode::kClamp;
      break;
  }

  if (is_dark_mode_enabled_) {
    for (auto& color : colors) {
      color = EnsureDarkModeFilter().InvertColorIfNeeded(
          SkColor(color), DarkModeFilter::ElementRole::kBackground);
    }
  }

  uint32_t flags = color_interpolation_ == ColorInterpolation::kPremultiplied
                       ? SkGradientShader::kInterpolateColorsInPremul_Flag
                       : 0;
  sk_sp<PaintShader> shader =
      CreateShader(colors, pos, tile, flags, local_matrix, colors.back());
  DCHECK(shader);

  return shader;
}

void Gradient::ApplyToFlags(cc::PaintFlags& flags,
                            const SkMatrix& local_matrix,
                            const ImageDrawOptions& draw_options) {
  if (is_dark_mode_enabled_ != draw_options.apply_dark_mode) {
    is_dark_mode_enabled_ = draw_options.apply_dark_mode;
    cached_shader_.reset();
  }
  if (!cached_shader_ || local_matrix != cached_shader_->GetLocalMatrix() ||
      flags.getColorFilter().get() != color_filter_.get()) {
    color_filter_ = flags.getColorFilter();
    flags.setColorFilter(nullptr);
    cached_shader_ = CreateShaderInternal(local_matrix);
  }

  flags.setShader(cached_shader_);

  // Legacy behavior: gradients are always dithered.
  flags.setDither(true);
}

DarkModeFilter& Gradient::EnsureDarkModeFilter() {
  if (!dark_mode_filter_) {
    dark_mode_filter_ =
        std::make_unique<DarkModeFilter>(GetCurrentDarkModeSettings());
  }
  return *dark_mode_filter_;
}

namespace {

class LinearGradient final : public Gradient {
 public:
  LinearGradient(const gfx::PointF& p0,
                 const gfx::PointF& p1,
                 GradientSpreadMethod spread_method,
                 ColorInterpolation interpolation,
                 DegenerateHandling degenerate_handling)
      : Gradient(Type::kLinear,
                 spread_method,
                 interpolation,
                 degenerate_handling),
        p0_(p0),
        p1_(p1) {}

 protected:
  sk_sp<PaintShader> CreateShader(const ColorBuffer& colors,
                                  const OffsetBuffer& pos,
                                  SkTileMode tile_mode,
                                  uint32_t flags,
                                  const SkMatrix& local_matrix,
                                  SkColor fallback_color) const override {
    if (GetDegenerateHandling() == DegenerateHandling::kDisallow &&
        p0_ == p1_) {
      return PaintShader::MakeEmpty();
    }

    SkPoint pts[2] = {FloatPointToSkPoint(p0_), FloatPointToSkPoint(p1_)};
    // TODO(crbug/1308932): Remove this helper vector colors4f and make all
    // SkColor4f.
    std::vector<SkColor4f> colors4f;
    colors4f.reserve(colors.size());
    for (auto& color : colors)
      colors4f.push_back(SkColor4f::FromColor(color));
    return PaintShader::MakeLinearGradient(
        pts, colors4f.data(), pos.data(), static_cast<int>(colors4f.size()),
        tile_mode, flags, &local_matrix, SkColor4f::FromColor(fallback_color));
  }

 private:
  const gfx::PointF p0_;
  const gfx::PointF p1_;
};

class RadialGradient final : public Gradient {
 public:
  RadialGradient(const gfx::PointF& p0,
                 float r0,
                 const gfx::PointF& p1,
                 float r1,
                 float aspect_ratio,
                 GradientSpreadMethod spread_method,
                 ColorInterpolation interpolation,
                 DegenerateHandling degenerate_handling)
      : Gradient(Type::kRadial,
                 spread_method,
                 interpolation,
                 degenerate_handling),
        p0_(p0),
        p1_(p1),
        r0_(r0),
        r1_(r1),
        aspect_ratio_(aspect_ratio) {}

 protected:
  sk_sp<PaintShader> CreateShader(const ColorBuffer& colors,
                                  const OffsetBuffer& pos,
                                  SkTileMode tile_mode,
                                  uint32_t flags,
                                  const SkMatrix& local_matrix,
                                  SkColor fallback_color) const override {
    const SkMatrix* matrix = &local_matrix;
    absl::optional<SkMatrix> adjusted_local_matrix;
    if (aspect_ratio_ != 1) {
      // CSS3 elliptical gradients: apply the elliptical scaling at the
      // gradient center point.
      DCHECK(p0_ == p1_);
      adjusted_local_matrix.emplace(local_matrix);
      adjusted_local_matrix->preScale(1, 1 / aspect_ratio_, p0_.x(), p0_.y());
      matrix = &*adjusted_local_matrix;
    }

    // The radii we give to Skia must be positive. If we're given a
    // negative radius, ask for zero instead.
    const SkScalar radius0 = std::max(WebCoreFloatToSkScalar(r0_), 0.0f);
    const SkScalar radius1 = std::max(WebCoreFloatToSkScalar(r1_), 0.0f);

    if (GetDegenerateHandling() == DegenerateHandling::kDisallow &&
        p0_ == p1_ && radius0 == radius1) {
      return PaintShader::MakeEmpty();
    }

    // TODO(crbug/1308932): Remove this helper vector colors4f and make all
    // SkColor4f.
    std::vector<SkColor4f> colors4f;
    colors4f.reserve(colors.size());
    for (auto& color : colors)
      colors4f.push_back(SkColor4f::FromColor(color));
    return PaintShader::MakeTwoPointConicalGradient(
        FloatPointToSkPoint(p0_), radius0, FloatPointToSkPoint(p1_), radius1,
        colors4f.data(), pos.data(), static_cast<int>(colors4f.size()),
        tile_mode, flags, matrix, SkColor4f::FromColor(fallback_color));
  }

 private:
  const gfx::PointF p0_;
  const gfx::PointF p1_;
  const float r0_;
  const float r1_;
  const float aspect_ratio_;  // For elliptical gradient, width / height.
};

class ConicGradient final : public Gradient {
 public:
  ConicGradient(const gfx::PointF& position,
                float rotation,
                float start_angle,
                float end_angle,
                GradientSpreadMethod spread_method,
                ColorInterpolation interpolation,
                DegenerateHandling degenerate_handling)
      : Gradient(Type::kConic,
                 spread_method,
                 interpolation,
                 degenerate_handling),
        position_(position),
        rotation_(rotation),
        start_angle_(start_angle),
        end_angle_(end_angle) {}

 protected:
  sk_sp<PaintShader> CreateShader(const ColorBuffer& colors,
                                  const OffsetBuffer& pos,
                                  SkTileMode tile_mode,
                                  uint32_t flags,
                                  const SkMatrix& local_matrix,
                                  SkColor fallback_color) const override {
    if (GetDegenerateHandling() == DegenerateHandling::kDisallow &&
        start_angle_ == end_angle_) {
      return PaintShader::MakeEmpty();
    }

    // Skia's sweep gradient angles are relative to the x-axis, not the y-axis.
    const float skia_rotation = rotation_ - 90;
    const SkMatrix* matrix = &local_matrix;
    absl::optional<SkMatrix> adjusted_local_matrix;
    if (skia_rotation) {
      adjusted_local_matrix.emplace(local_matrix);
      adjusted_local_matrix->preRotate(skia_rotation, position_.x(),
                                       position_.y());
      matrix = &*adjusted_local_matrix;
    }

    // TODO(crbug/1308932): Remove this helper vector colors4f and make all
    // SkColor4f.
    std::vector<SkColor4f> colors4f;
    colors4f.reserve(colors.size());
    for (auto& color : colors)
      colors4f.push_back(SkColor4f::FromColor(color));
    return PaintShader::MakeSweepGradient(
        position_.x(), position_.y(), colors4f.data(), pos.data(),
        static_cast<int>(colors4f.size()), tile_mode, start_angle_, end_angle_,
        flags, matrix, SkColor4f::FromColor(fallback_color));
  }

 private:
  const gfx::PointF position_;  // center point
  const float rotation_;       // global rotation (deg)
  const float start_angle_;    // angle (deg) corresponding to color position 0
  const float end_angle_;      // angle (deg) corresponding to color position 1
};

}  // namespace

scoped_refptr<Gradient> Gradient::CreateLinear(
    const gfx::PointF& p0,
    const gfx::PointF& p1,
    GradientSpreadMethod spread_method,
    ColorInterpolation interpolation,
    DegenerateHandling degenerate_handling) {
  return base::AdoptRef(new LinearGradient(p0, p1, spread_method, interpolation,
                                           degenerate_handling));
}

scoped_refptr<Gradient> Gradient::CreateRadial(
    const gfx::PointF& p0,
    float r0,
    const gfx::PointF& p1,
    float r1,
    float aspect_ratio,
    GradientSpreadMethod spread_method,
    ColorInterpolation interpolation,
    DegenerateHandling degenerate_handling) {
  return base::AdoptRef(new RadialGradient(p0, r0, p1, r1, aspect_ratio,
                                           spread_method, interpolation,
                                           degenerate_handling));
}

scoped_refptr<Gradient> Gradient::CreateConic(
    const gfx::PointF& position,
    float rotation,
    float start_angle,
    float end_angle,
    GradientSpreadMethod spread_method,
    ColorInterpolation interpolation,
    DegenerateHandling degenerate_handling) {
  return base::AdoptRef(new ConicGradient(position, rotation, start_angle,
                                          end_angle, spread_method,
                                          interpolation, degenerate_handling));
}

}  // namespace blink
