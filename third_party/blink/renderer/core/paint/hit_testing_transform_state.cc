/*
 * Copyright (C) 2011 Apple Inc.  All rights reserved.
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

#include "third_party/blink/renderer/core/paint/hit_testing_transform_state.h"

#include "third_party/blink/renderer/platform/geometry/layout_rect.h"
#include "third_party/blink/renderer/platform/graphics/paint/transform_paint_property_node.h"

namespace blink {

void HitTestingTransformState::Translate(const gfx::Vector2dF& offset) {
  accumulated_transform_.Translate(offset.x(), offset.y());
}

void HitTestingTransformState::ApplyTransform(
    const TransformPaintPropertyNode& transform) {
  if (transform.IsIdentityOr2DTranslation()) {
    Translate(transform.Translation2D());
  } else {
    accumulated_transform_.Multiply(transform.MatrixWithOriginApplied());
  }
}

void HitTestingTransformState::ApplyTransform(
    const GeometryMapper::Translation2DOrMatrix& transform) {
  if (transform.IsIdentityOr2DTranslation()) {
    Translate(transform.Translation2D());
  } else {
    accumulated_transform_.Multiply(transform.Matrix());
  }
}

void HitTestingTransformState::Flatten() {
  TransformationMatrix inverse_transform = accumulated_transform_.Inverse();
  last_planar_point_ = inverse_transform.ProjectPoint(last_planar_point_);
  last_planar_quad_ = inverse_transform.ProjectQuad(last_planar_quad_);
  last_planar_area_ = inverse_transform.ProjectQuad(last_planar_area_);

  accumulated_transform_.MakeIdentity();
}

gfx::PointF HitTestingTransformState::MappedPoint() const {
  return accumulated_transform_.Inverse().ProjectPoint(last_planar_point_);
}

gfx::QuadF HitTestingTransformState::MappedQuad() const {
  return accumulated_transform_.Inverse().ProjectQuad(last_planar_quad_);
}

PhysicalRect HitTestingTransformState::BoundsOfMappedQuad() const {
  return PhysicalRectToBeNoop(
      accumulated_transform_.Inverse().ClampedBoundsOfProjectedQuad(
          last_planar_quad_));
}

PhysicalRect HitTestingTransformState::BoundsOfMappedArea() const {
  return PhysicalRectToBeNoop(
      accumulated_transform_.Inverse().ClampedBoundsOfProjectedQuad(
          last_planar_area_));
}

}  // namespace blink
