// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/css/media_values_dynamic.h"

#include "third_party/blink/public/common/css/forced_colors.h"
#include "third_party/blink/public/common/css/navigation_controls.h"
#include "third_party/blink/renderer/core/css/css_primitive_value.h"
#include "third_party/blink/renderer/core/css/css_resolution_units.h"
#include "third_party/blink/renderer/core/css/css_to_length_conversion_data.h"
#include "third_party/blink/renderer/core/css/media_values_cached.h"
#include "third_party/blink/renderer/core/dom/document.h"
#include "third_party/blink/renderer/core/frame/local_frame.h"

namespace blink {

MediaValues* MediaValuesDynamic::Create(Document& document) {
  return MediaValuesDynamic::Create(document.GetFrame());
}

MediaValues* MediaValuesDynamic::Create(LocalFrame* frame) {
  if (!frame || !frame->View() || !frame->GetDocument() ||
      !frame->GetDocument()->GetLayoutView())
    return MakeGarbageCollected<MediaValuesCached>();
  return MakeGarbageCollected<MediaValuesDynamic>(frame);
}

MediaValuesDynamic::MediaValuesDynamic(LocalFrame* frame)
    : frame_(frame),
      viewport_dimensions_overridden_(false),
      viewport_width_override_(0),
      viewport_height_override_(0) {
  DCHECK(frame_);
}

MediaValuesDynamic::MediaValuesDynamic(LocalFrame* frame,
                                       bool overridden_viewport_dimensions,
                                       double viewport_width,
                                       double viewport_height)
    : frame_(frame),
      viewport_dimensions_overridden_(overridden_viewport_dimensions),
      viewport_width_override_(viewport_width),
      viewport_height_override_(viewport_height) {
  DCHECK(frame_);
}

float MediaValuesDynamic::EmFontSize() const {
  return CalculateEmSize(frame_);
}

float MediaValuesDynamic::RemFontSize() const {
  // For media queries rem and em units are both based on the initial font.
  return CalculateEmSize(frame_);
}

float MediaValuesDynamic::ExFontSize() const {
  return CalculateExSize(frame_);
}

float MediaValuesDynamic::ChFontSize() const {
  return CalculateChSize(frame_);
}

float MediaValuesDynamic::IcFontSize() const {
  return CalculateIcSize(frame_);
}

double MediaValuesDynamic::ViewportWidth() const {
  if (viewport_dimensions_overridden_)
    return viewport_width_override_;
  return CalculateViewportWidth(frame_);
}

double MediaValuesDynamic::ViewportHeight() const {
  if (viewport_dimensions_overridden_)
    return viewport_height_override_;
  return CalculateViewportHeight(frame_);
}

double MediaValuesDynamic::SmallViewportWidth() const {
  return CalculateSmallViewportWidth(frame_);
}

double MediaValuesDynamic::SmallViewportHeight() const {
  return CalculateSmallViewportHeight(frame_);
}

double MediaValuesDynamic::LargeViewportWidth() const {
  return CalculateLargeViewportWidth(frame_);
}

double MediaValuesDynamic::LargeViewportHeight() const {
  return CalculateLargeViewportHeight(frame_);
}

double MediaValuesDynamic::DynamicViewportWidth() const {
  return CalculateDynamicViewportWidth(frame_);
}

double MediaValuesDynamic::DynamicViewportHeight() const {
  return CalculateDynamicViewportHeight(frame_);
}

double MediaValuesDynamic::ContainerWidth() const {
  return SmallViewportWidth();
}

double MediaValuesDynamic::ContainerHeight() const {
  return SmallViewportHeight();
}

int MediaValuesDynamic::DeviceWidth() const {
  return CalculateDeviceWidth(frame_);
}

int MediaValuesDynamic::DeviceHeight() const {
  return CalculateDeviceHeight(frame_);
}

float MediaValuesDynamic::DevicePixelRatio() const {
  return CalculateDevicePixelRatio(frame_);
}

bool MediaValuesDynamic::DeviceSupportsHDR() const {
  return CalculateDeviceSupportsHDR(frame_);
}

int MediaValuesDynamic::ColorBitsPerComponent() const {
  return CalculateColorBitsPerComponent(frame_);
}

int MediaValuesDynamic::MonochromeBitsPerComponent() const {
  return CalculateMonochromeBitsPerComponent(frame_);
}

mojom::blink::PointerType MediaValuesDynamic::PrimaryPointerType() const {
  return CalculatePrimaryPointerType(frame_);
}

int MediaValuesDynamic::AvailablePointerTypes() const {
  return CalculateAvailablePointerTypes(frame_);
}

mojom::blink::HoverType MediaValuesDynamic::PrimaryHoverType() const {
  return CalculatePrimaryHoverType(frame_);
}

int MediaValuesDynamic::AvailableHoverTypes() const {
  return CalculateAvailableHoverTypes(frame_);
}

bool MediaValuesDynamic::ThreeDEnabled() const {
  return CalculateThreeDEnabled(frame_);
}

bool MediaValuesDynamic::InImmersiveMode() const {
  return CalculateInImmersiveMode(frame_);
}

const String MediaValuesDynamic::MediaType() const {
  return CalculateMediaType(frame_);
}

blink::mojom::DisplayMode MediaValuesDynamic::DisplayMode() const {
  return CalculateDisplayMode(frame_);
}

bool MediaValuesDynamic::StrictMode() const {
  return CalculateStrictMode(frame_);
}

ColorSpaceGamut MediaValuesDynamic::ColorGamut() const {
  return CalculateColorGamut(frame_);
}

mojom::blink::PreferredColorScheme MediaValuesDynamic::GetPreferredColorScheme()
    const {
  return CalculatePreferredColorScheme(frame_);
}

mojom::blink::PreferredContrast MediaValuesDynamic::GetPreferredContrast()
    const {
  return CalculatePreferredContrast(frame_);
}

bool MediaValuesDynamic::PrefersReducedMotion() const {
  return CalculatePrefersReducedMotion(frame_);
}

bool MediaValuesDynamic::PrefersReducedData() const {
  return CalculatePrefersReducedData(frame_);
}

ForcedColors MediaValuesDynamic::GetForcedColors() const {
  return CalculateForcedColors(frame_);
}

NavigationControls MediaValuesDynamic::GetNavigationControls() const {
  return CalculateNavigationControls(frame_);
}

int MediaValuesDynamic::GetHorizontalViewportSegments() const {
  return CalculateHorizontalViewportSegments(frame_);
}

int MediaValuesDynamic::GetVerticalViewportSegments() const {
  return CalculateVerticalViewportSegments(frame_);
}

device::mojom::blink::DevicePostureType MediaValuesDynamic::GetDevicePosture()
    const {
  return CalculateDevicePosture(frame_);
}

Document* MediaValuesDynamic::GetDocument() const {
  return frame_->GetDocument();
}

bool MediaValuesDynamic::HasValues() const {
  return frame_;
}

void MediaValuesDynamic::Trace(Visitor* visitor) const {
  visitor->Trace(frame_);
  MediaValues::Trace(visitor);
}

}  // namespace blink
