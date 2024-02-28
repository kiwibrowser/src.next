// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/paint/svg_object_painter.h"

#include "cc/paint/color_filter.h"
#include "third_party/blink/renderer/core/layout/svg/layout_svg_resource_paint_server.h"
#include "third_party/blink/renderer/core/layout/svg/svg_resources.h"
#include "third_party/blink/renderer/core/paint/paint_auto_dark_mode.h"
#include "third_party/blink/renderer/core/paint/paint_info.h"

namespace blink {

namespace {

void ApplyColorInterpolation(PaintFlags paint_flags,
                             const ComputedStyle& style,
                             cc::PaintFlags& flags) {
  const bool is_rendering_svg_mask = paint_flags & PaintFlag::kPaintingSVGMask;
  if (is_rendering_svg_mask &&
      style.ColorInterpolation() == EColorInterpolation::kLinearrgb) {
    flags.setColorFilter(cc::ColorFilter::MakeSRGBToLinearGamma());
  }
}

}  // namespace

void SVGObjectPainter::PaintResourceSubtree(GraphicsContext& context,
                                            PaintFlags additional_flags) {
  DCHECK(!layout_object_.SelfNeedsFullLayout());

  PaintInfo info(context, CullRect::Infinite(), PaintPhase::kForeground,
                 PaintFlag::kOmitCompositingInfo |
                     PaintFlag::kPaintingResourceSubtree | additional_flags);
  layout_object_.Paint(info);
}

bool SVGObjectPainter::ApplyPaintResource(
    const SVGPaint& paint,
    const AffineTransform* additional_paint_server_transform,
    cc::PaintFlags& flags) {
  SVGElementResourceClient* client = SVGResources::GetClient(layout_object_);
  if (!client)
    return false;
  auto* uri_resource = GetSVGResourceAsType<LayoutSVGResourcePaintServer>(
      *client, paint.Resource());
  if (!uri_resource)
    return false;

  AutoDarkMode auto_dark_mode(PaintAutoDarkMode(
      layout_object_.StyleRef(), DarkModeFilter::ElementRole::kSVG));
  if (!uri_resource->ApplyShader(
          *client, SVGResources::ReferenceBoxForEffects(layout_object_),
          additional_paint_server_transform, auto_dark_mode, flags))
    return false;
  return true;
}

bool SVGObjectPainter::PreparePaint(
    PaintFlags paint_flags,
    const ComputedStyle& style,
    LayoutSVGResourceMode resource_mode,
    cc::PaintFlags& flags,
    const AffineTransform* additional_paint_server_transform) {
  const bool apply_to_fill = resource_mode == kApplyToFillMode;
  const SVGPaint& paint =
      apply_to_fill ? style.FillPaint() : style.StrokePaint();
  const float alpha =
      apply_to_fill ? style.FillOpacity() : style.StrokeOpacity();
  if (paint.HasUrl()) {
    if (ApplyPaintResource(paint, additional_paint_server_transform, flags)) {
      flags.setColor(ScaleAlpha(SK_ColorBLACK, alpha));
      ApplyColorInterpolation(paint_flags, style, flags);
      return true;
    }
  }
  if (paint.HasColor()) {
    const Longhand& property = apply_to_fill
                                   ? To<Longhand>(GetCSSPropertyFill())
                                   : To<Longhand>(GetCSSPropertyStroke());
    Color flag_color = style.VisitedDependentColor(property);
    flag_color.SetAlpha(flag_color.Alpha() * alpha);
    flags.setColor(flag_color.toSkColor4f());
    flags.setShader(nullptr);
    ApplyColorInterpolation(paint_flags, style, flags);
    return true;
  }
  return false;
}

}  // namespace blink
