/*
 * Copyright (C) 1999 Lars Knoll (knoll@kde.org)
 *           (C) 1999 Antti Koivisto (koivisto@kde.org)
 *           (C) 2001 Dirk Mueller (mueller@kde.org)
 *           (C) 2006 Alexey Proskuryakov (ap@webkit.org)
 * Copyright (C) 2004, 2005, 2006, 2007, 2008, 2011 Apple Inc. All rights
 * reserved.
 * Copyright (C) 2008 Torch Mobile Inc. All rights reserved.
 * (http://www.torchmobile.com/)
 * Copyright (C) 2010 Nokia Corporation and/or its subsidiary(-ies)
 * Copyright (C) 2012-2013 Intel Corporation. All rights reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 */

#include "third_party/blink/renderer/core/page/viewport_description.h"

#include "base/metrics/histogram_macros.h"
#include "build/build_config.h"
#include "third_party/blink/renderer/core/dom/document.h"
#include "third_party/blink/renderer/core/frame/local_frame.h"
#include "third_party/blink/renderer/core/frame/local_frame_view.h"
#include "third_party/blink/renderer/core/frame/settings.h"
#include "third_party/blink/renderer/core/frame/visual_viewport.h"
#include "third_party/blink/renderer/core/page/page.h"
#include "third_party/blink/renderer/platform/weborigin/kurl.h"

namespace blink {

static const float& CompareIgnoringAuto(const float& value1,
                                        const float& value2,
                                        const float& (*compare)(const float&,
                                                                const float&)) {
  if (value1 == ViewportDescription::kValueAuto)
    return value2;

  if (value2 == ViewportDescription::kValueAuto)
    return value1;

  return compare(value1, value2);
}

static void RecordViewportTypeMetric(
    ViewportDescription::ViewportUMAType type) {
  UMA_HISTOGRAM_ENUMERATION("Viewport.MetaTagType", type);
}

float ViewportDescription::ResolveViewportLength(
    const Length& length,
    const gfx::SizeF& initial_viewport_size,
    Direction direction) {
  if (length.IsAuto())
    return ViewportDescription::kValueAuto;

  if (length.IsFixed())
    return length.GetFloatValue();

  if (length.IsExtendToZoom())
    return ViewportDescription::kValueExtendToZoom;

  if (length.IsPercent() && direction == Direction::kHorizontal)
    return initial_viewport_size.width() * length.GetFloatValue() / 100.0f;

  if (length.IsPercent() && direction == Direction::kVertical)
    return initial_viewport_size.height() * length.GetFloatValue() / 100.0f;

  if (length.IsDeviceWidth())
    return initial_viewport_size.width();

  if (length.IsDeviceHeight())
    return initial_viewport_size.height();

  NOTREACHED();
  return ViewportDescription::kValueAuto;
}

PageScaleConstraints ViewportDescription::Resolve(
    const gfx::SizeF& initial_viewport_size,
    const Length& legacy_fallback_width) const {
  float result_width = kValueAuto;

  Length copy_max_width = max_width;
  Length copy_min_width = min_width;
  // In case the width (used for min- and max-width) is undefined.
  if (IsLegacyViewportType() && max_width.IsAuto()) {
    // The width viewport META property is translated into 'width' descriptors,
    // setting the 'min' value to 'extend-to-zoom' and the 'max' value to the
    // intended length.  In case the UA-defines a min-width, use that as length.
    if (zoom == ViewportDescription::kValueAuto) {
      copy_min_width = Length::ExtendToZoom();
      copy_max_width = legacy_fallback_width;
    } else if (max_height.IsAuto()) {
      copy_min_width = Length::ExtendToZoom();
      copy_max_width = Length::ExtendToZoom();
    }
  }

  float result_max_width = ResolveViewportLength(
      copy_max_width, initial_viewport_size, Direction::kHorizontal);
  float result_min_width = ResolveViewportLength(
      copy_min_width, initial_viewport_size, Direction::kHorizontal);

  float result_height = kValueAuto;
  float result_max_height = ResolveViewportLength(
      max_height, initial_viewport_size, Direction::kVertical);
  float result_min_height = ResolveViewportLength(
      min_height, initial_viewport_size, Direction::kVertical);

  float result_zoom = zoom;
  float result_min_zoom = min_zoom;
  float result_max_zoom = max_zoom;
  bool result_user_zoom = user_zoom;

  // Resolve min-zoom and max-zoom values.
  if (result_min_zoom != ViewportDescription::kValueAuto &&
      result_max_zoom != ViewportDescription::kValueAuto)
    result_max_zoom = std::max(result_min_zoom, result_max_zoom);

  // Constrain zoom value to the [min-zoom, max-zoom] range.
  if (result_zoom != ViewportDescription::kValueAuto)
    result_zoom = CompareIgnoringAuto(
        result_min_zoom,
        CompareIgnoringAuto(result_max_zoom, result_zoom, std::min), std::max);

  float extend_zoom =
      CompareIgnoringAuto(result_zoom, result_max_zoom, std::min);

  // Resolve non-"auto" lengths to pixel lengths.
  if (extend_zoom == ViewportDescription::kValueAuto) {
    if (result_max_width == ViewportDescription::kValueExtendToZoom)
      result_max_width = ViewportDescription::kValueAuto;

    if (result_max_height == ViewportDescription::kValueExtendToZoom)
      result_max_height = ViewportDescription::kValueAuto;

    if (result_min_width == ViewportDescription::kValueExtendToZoom)
      result_min_width = result_max_width;

    if (result_min_height == ViewportDescription::kValueExtendToZoom)
      result_min_height = result_max_height;
  } else {
    float extend_width = initial_viewport_size.width() / extend_zoom;
    float extend_height = initial_viewport_size.height() / extend_zoom;

    if (result_max_width == ViewportDescription::kValueExtendToZoom)
      result_max_width = extend_width;

    if (result_max_height == ViewportDescription::kValueExtendToZoom)
      result_max_height = extend_height;

    if (result_min_width == ViewportDescription::kValueExtendToZoom)
      result_min_width =
          CompareIgnoringAuto(extend_width, result_max_width, std::max);

    if (result_min_height == ViewportDescription::kValueExtendToZoom)
      result_min_height =
          CompareIgnoringAuto(extend_height, result_max_height, std::max);
  }

  // Resolve initial width from min/max descriptors.
  if (result_min_width != ViewportDescription::kValueAuto ||
      result_max_width != ViewportDescription::kValueAuto)
    result_width = CompareIgnoringAuto(
        result_min_width,
        CompareIgnoringAuto(result_max_width, initial_viewport_size.width(),
                            std::min),
        std::max);

  // Resolve initial height from min/max descriptors.
  if (result_min_height != ViewportDescription::kValueAuto ||
      result_max_height != ViewportDescription::kValueAuto)
    result_height = CompareIgnoringAuto(
        result_min_height,
        CompareIgnoringAuto(result_max_height, initial_viewport_size.height(),
                            std::min),
        std::max);

  // Resolve width value.
  if (result_width == ViewportDescription::kValueAuto) {
    if (result_height == ViewportDescription::kValueAuto ||
        !initial_viewport_size.height()) {
      result_width = initial_viewport_size.width();
    } else {
      result_width = result_height * (initial_viewport_size.width() /
                                      initial_viewport_size.height());
    }
  }

  // Resolve height value.
  if (result_height == ViewportDescription::kValueAuto) {
    if (!initial_viewport_size.width()) {
      result_height = initial_viewport_size.height();
    } else {
      result_height = result_width * initial_viewport_size.height() /
                      initial_viewport_size.width();
    }
  }

  // Resolve initial-scale value.
  if (result_zoom == ViewportDescription::kValueAuto) {
    if (result_width != ViewportDescription::kValueAuto && result_width > 0)
      result_zoom = initial_viewport_size.width() / result_width;
    if (result_height != ViewportDescription::kValueAuto && result_height > 0) {
      // if 'auto', the initial-scale will be negative here and thus ignored.
      result_zoom = std::max<float>(
          result_zoom, initial_viewport_size.height() / result_height);
    }

    // Reconstrain zoom value to the [min-zoom, max-zoom] range.
    result_zoom = CompareIgnoringAuto(
        result_min_zoom,
        CompareIgnoringAuto(result_max_zoom, result_zoom, std::min), std::max);
  }

  // If user-scalable = no, lock the min/max scale to the computed initial
  // scale.
  if (!result_user_zoom)
    result_min_zoom = result_max_zoom = result_zoom;

  // Only set initialScale to a value if it was explicitly set.
  if (zoom == ViewportDescription::kValueAuto)
    result_zoom = ViewportDescription::kValueAuto;

  PageScaleConstraints result;
  result.minimum_scale = result_min_zoom;
  result.maximum_scale = result_max_zoom;
  result.initial_scale = result_zoom;
  result.layout_size.set_width(result_width);
  result.layout_size.set_height(result_height);
  return result;
}

void ViewportDescription::ReportMobilePageStats(
    const LocalFrame* main_frame) const {
  if (!main_frame || !main_frame->GetPage() || !main_frame->View() ||
      !main_frame->GetDocument())
    return;

  if (!main_frame->GetSettings() ||
      !main_frame->GetSettings()->GetViewportEnabled())
    return;

  // Avoid chrome:// pages like the new-tab page (on Android new tab is
  // non-http).
  if (!main_frame->GetDocument()->Url().ProtocolIsInHTTPFamily())
    return;

  if (!IsSpecifiedByAuthor()) {
    RecordViewportTypeMetric(main_frame->GetDocument()->IsMobileDocument()
                                 ? ViewportUMAType::kXhtmlMobileProfile
                                 : ViewportUMAType::kNoViewportTag);
    return;
  }

  if (IsMetaViewportType()) {
    if (max_width.IsFixed()) {
      RecordViewportTypeMetric(ViewportUMAType::kConstantWidth);
    } else if (max_width.IsDeviceWidth() || max_width.IsExtendToZoom()) {
      RecordViewportTypeMetric(ViewportUMAType::kDeviceWidth);
    } else {
      // Overflow bucket for cases we may be unaware of.
      RecordViewportTypeMetric(ViewportUMAType::kMetaWidthOther);
    }
  } else if (type == ViewportDescription::kHandheldFriendlyMeta) {
    RecordViewportTypeMetric(ViewportUMAType::kMetaHandheldFriendly);
  } else if (type == ViewportDescription::kMobileOptimizedMeta) {
    RecordViewportTypeMetric(ViewportUMAType::kMetaMobileOptimized);
  }
}

}  // namespace blink
