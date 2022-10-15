// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/paint/image_painter.h"

#include "third_party/blink/public/mojom/permissions_policy/permissions_policy_feature.mojom-blink.h"
#include "third_party/blink/public/mojom/permissions_policy/policy_value.mojom-blink.h"
#include "third_party/blink/renderer/core/dom/document.h"
#include "third_party/blink/renderer/core/dom/element.h"
#include "third_party/blink/renderer/core/editing/frame_selection.h"
#include "third_party/blink/renderer/core/frame/local_dom_window.h"
#include "third_party/blink/renderer/core/frame/local_frame.h"
#include "third_party/blink/renderer/core/html/html_area_element.h"
#include "third_party/blink/renderer/core/html/html_image_element.h"
#include "third_party/blink/renderer/core/inspector/inspector_trace_events.h"
#include "third_party/blink/renderer/core/layout/adjust_for_absolute_zoom.h"
#include "third_party/blink/renderer/core/layout/layout_image.h"
#include "third_party/blink/renderer/core/layout/layout_replaced.h"
#include "third_party/blink/renderer/core/layout/text_run_constructor.h"
#include "third_party/blink/renderer/core/page/chrome_client.h"
#include "third_party/blink/renderer/core/page/page.h"
#include "third_party/blink/renderer/core/paint/box_painter.h"
#include "third_party/blink/renderer/core/paint/image_element_timing.h"
#include "third_party/blink/renderer/core/paint/outline_painter.h"
#include "third_party/blink/renderer/core/paint/paint_auto_dark_mode.h"
#include "third_party/blink/renderer/core/paint/paint_info.h"
#include "third_party/blink/renderer/core/paint/paint_timing_detector.h"
#include "third_party/blink/renderer/core/paint/scoped_paint_state.h"
#include "third_party/blink/renderer/platform/geometry/layout_point.h"
#include "third_party/blink/renderer/platform/graphics/graphics_context.h"
#include "third_party/blink/renderer/platform/graphics/paint/display_item_cache_skipper.h"
#include "third_party/blink/renderer/platform/graphics/paint/drawing_recorder.h"
#include "third_party/blink/renderer/platform/graphics/path.h"
#include "third_party/blink/renderer/platform/graphics/placeholder_image.h"
#include "third_party/blink/renderer/platform/graphics/scoped_interpolation_quality.h"
#include "third_party/blink/renderer/platform/runtime_enabled_features.h"

namespace blink {
namespace {

// TODO(loonybear): Currently oversized-images policy is only reinforced on
// HTMLImageElement. Use data from |layout_image|, |content_rect| and/or
// Document to support this policy on other image types (crbug.com/930281).
bool CheckForOversizedImagesPolicy(const LayoutImage& layout_image,
                                   scoped_refptr<Image> image) {
  DCHECK(image);
  if (!RuntimeEnabledFeatures::ExperimentalPoliciesEnabled(
          layout_image.GetDocument().GetExecutionContext()))
    return false;

  LayoutSize layout_size = layout_image.ContentSize();
  gfx::Size image_size = image->Size();
  if (layout_size.IsEmpty() || image_size.IsEmpty())
    return false;

  const double downscale_ratio_width =
      image_size.width() / layout_size.Width().ToDouble();
  const double downscale_ratio_height =
      image_size.height() / layout_size.Height().ToDouble();

  const LayoutImageResource* image_resource = layout_image.ImageResource();
  const ImageResourceContent* cached_image =
      image_resource ? image_resource->CachedImage() : nullptr;
  const String& image_url =
      cached_image ? cached_image->Url().GetString() : g_empty_string;

  return !layout_image.GetDocument().domWindow()->IsFeatureEnabled(
      mojom::blink::DocumentPolicyFeature::kOversizedImages,
      blink::PolicyValue::CreateDecDouble(
          std::max(downscale_ratio_width, downscale_ratio_height)),
      ReportOptions::kReportOnFailure, g_empty_string, image_url);
}

ImagePaintTimingInfo ComputeImagePaintTimingInfo(
    const LayoutImage& layout_image,
    const Image& image,
    const ImageResourceContent* image_content,
    const GraphicsContext& context,
    const gfx::Rect& image_border) {
  // |report_paint_timing| for ImagePaintTimingInfo is set to false since we
  // expect all images to be contentful and non-generated
  if (!image_content) {
    return ImagePaintTimingInfo(/* image_may_be_lcp_candidate */ false,
                                /* report_paint_timing */ false);
  }
  return ImagePaintTimingInfo(PaintTimingDetector::NotifyImagePaint(
      layout_image, image.Size(), *image_content,
      context.GetPaintController().CurrentPaintChunkProperties(),
      image_border));
}

}  // namespace

void ImagePainter::Paint(const PaintInfo& paint_info) {
  layout_image_.LayoutReplaced::Paint(paint_info);

  if (paint_info.phase == PaintPhase::kOutline)
    PaintAreaElementFocusRing(paint_info);
}

void ImagePainter::PaintAreaElementFocusRing(const PaintInfo& paint_info) {
  Document& document = layout_image_.GetDocument();

  if (document.Printing() ||
      !document.GetFrame()->Selection().FrameIsFocusedAndActive())
    return;

  auto* area_element = DynamicTo<HTMLAreaElement>(document.FocusedElement());
  if (!area_element)
    return;

  if (area_element->ImageElement() != layout_image_.GetNode())
    return;

  // We use EnsureComputedStyle() instead of GetComputedStyle() here because
  // <area> is used and its style applied even if it has display:none.
  const ComputedStyle* area_element_style = area_element->EnsureComputedStyle();
  // If the outline width is 0 we want to avoid drawing anything even if we
  // don't use the value directly.
  if (!area_element_style->OutlineWidth())
    return;

  Path path = area_element->GetPath(&layout_image_);
  if (path.IsEmpty())
    return;

  ScopedPaintState paint_state(layout_image_, paint_info);
  auto paint_offset = paint_state.PaintOffset();
  path.Translate(gfx::Vector2dF(paint_offset));

  if (DrawingRecorder::UseCachedDrawingIfPossible(
          paint_info.context, layout_image_, DisplayItem::kImageAreaFocusRing))
    return;

  BoxDrawingRecorder recorder(paint_info.context, layout_image_,
                              DisplayItem::kImageAreaFocusRing, paint_offset);

  // FIXME: Clip path instead of context when Skia pathops is ready.
  // https://crbug.com/251206

  paint_info.context.Save();
  PhysicalRect focus_rect = layout_image_.PhysicalContentBoxRect();
  focus_rect.Move(paint_offset);
  paint_info.context.Clip(ToPixelSnappedRect(focus_rect));
  OutlinePainter::PaintFocusRingPath(paint_info.context, path,
                                     *area_element_style);
  paint_info.context.Restore();
}

void ImagePainter::PaintReplaced(const PaintInfo& paint_info,
                                 const PhysicalOffset& paint_offset) {
  LayoutSize content_size = layout_image_.ContentSize();
  bool has_image = layout_image_.ImageResource()->HasImage();

  if (has_image) {
    if (content_size.IsEmpty())
      return;
  } else {
    if (paint_info.phase == PaintPhase::kSelectionDragImage)
      return;
    if (content_size.Width() <= 2 || content_size.Height() <= 2)
      return;
  }

  GraphicsContext& context = paint_info.context;
  if (DrawingRecorder::UseCachedDrawingIfPossible(context, layout_image_,
                                                  paint_info.phase))
    return;

  // Disable cache in under-invalidation checking mode for animated image
  // because it may change before it's actually invalidated.
  absl::optional<DisplayItemCacheSkipper> cache_skipper;
  if (RuntimeEnabledFeatures::PaintUnderInvalidationCheckingEnabled() &&
      layout_image_.ImageResource() &&
      layout_image_.ImageResource()->MaybeAnimated())
    cache_skipper.emplace(context);

  PhysicalRect content_rect(
      paint_offset + layout_image_.PhysicalContentBoxOffset(),
      PhysicalSizeToBeNoop(content_size));

  if (!has_image) {
    // Draw an outline rect where the image should be.
    gfx::Rect paint_rect = ToPixelSnappedRect(content_rect);
    BoxDrawingRecorder recorder(context, layout_image_, paint_info.phase,
                                paint_offset);
    context.SetStrokeStyle(kSolidStroke);
    context.SetStrokeColor(Color::kLightGray);
    context.SetFillColor(Color::kTransparent);
    context.DrawRect(paint_rect,
                     PaintAutoDarkMode(layout_image_.StyleRef(),
                                       DarkModeFilter::ElementRole::kBorder));
    return;
  }

  PhysicalRect paint_rect = layout_image_.ReplacedContentRect();
  paint_rect.offset += paint_offset;

  // If |overflow| is supported for replaced elements, paint the complete image
  // and the painting will be clipped based on overflow value by clip paint
  // property nodes.
  const PhysicalRect visual_rect =
      layout_image_.ClipsToContentBox() ? content_rect : paint_rect;

  DrawingRecorder recorder(context, layout_image_, paint_info.phase,
                           ToEnclosingRect(visual_rect));
  PaintIntoRect(context, paint_rect, visual_rect);
}

void ImagePainter::PaintIntoRect(GraphicsContext& context,
                                 const PhysicalRect& dest_rect,
                                 const PhysicalRect& content_rect) {
  const LayoutImageResource& image_resource = *layout_image_.ImageResource();
  if (!image_resource.HasImage() || image_resource.ErrorOccurred())
    return;  // FIXME: should we just ASSERT these conditions? (audit all
             // callers).

  gfx::Rect pixel_snapped_dest_rect = ToPixelSnappedRect(dest_rect);
  if (pixel_snapped_dest_rect.IsEmpty())
    return;

  scoped_refptr<Image> image =
      image_resource.GetImage(gfx::SizeF(dest_rect.size));
  if (!image || image->IsNull())
    return;

  // Get the oriented source rect in order to correctly clip. We check the
  // default orientation first to avoid expensive transform operations.
  auto respect_orientation = image->HasDefaultOrientation()
                                 ? kDoNotRespectImageOrientation
                                 : image_resource.ImageOrientation();
  gfx::RectF src_rect(image->SizeAsFloat(respect_orientation));

  // If the content rect requires clipping, adjust |srcRect| and
  // |pixelSnappedDestRect| over using a clip.
  if (!content_rect.Contains(dest_rect)) {
    gfx::Rect pixel_snapped_content_rect = ToPixelSnappedRect(content_rect);
    pixel_snapped_content_rect.Intersect(pixel_snapped_dest_rect);
    if (pixel_snapped_content_rect.IsEmpty())
      return;
    src_rect = gfx::MapRect(gfx::RectF(pixel_snapped_content_rect),
                            gfx::RectF(pixel_snapped_dest_rect), src_rect);
    pixel_snapped_dest_rect = pixel_snapped_content_rect;
  }

  // Undo the image orientation in the source rect because subsequent code
  // expects the source rect in unoriented image space.
  if (respect_orientation == kRespectImageOrientation) {
    src_rect = image->CorrectSrcRectForImageOrientation(
        image->SizeAsFloat(respect_orientation), src_rect);
  }

  DEVTOOLS_TIMELINE_TRACE_EVENT_WITH_CATEGORIES(
      TRACE_DISABLED_BY_DEFAULT("devtools.timeline"), "PaintImage",
      inspector_paint_image_event::Data, layout_image_, src_rect,
      gfx::RectF(dest_rect));

  ScopedInterpolationQuality interpolation_quality_scope(
      context, layout_image_.StyleRef().GetInterpolationQuality());

  Node* node = layout_image_.GetNode();
  auto* image_element = DynamicTo<HTMLImageElement>(node);
  Image::ImageDecodingMode decode_mode =
      image_element
          ? image_element->GetDecodingModeForPainting(image->paint_image_id())
          : Image::kUnspecifiedDecode;

  // TODO(loonybear): Support image policies on other image types in addition to
  // HTMLImageElement.
  if (image_element) {
    if (CheckForOversizedImagesPolicy(layout_image_, image) ||
        image_element->IsImagePolicyViolated()) {
      // Does not set an observer for the placeholder image, setting it to null.
      scoped_refptr<PlaceholderImage> placeholder_image =
          PlaceholderImage::Create(nullptr, image->Size(),
                                   image->HasData() ? image->DataSize() : 0);
      placeholder_image->SetIconAndTextScaleFactor(
          layout_image_.GetFrame()->PageZoomFactor());
      image = std::move(placeholder_image);
    }
  }

  auto image_auto_dark_mode = ImageClassifierHelper::GetImageAutoDarkMode(
      *layout_image_.GetFrame(), layout_image_.StyleRef(),
      gfx::RectF(pixel_snapped_dest_rect), src_rect);

  // At this point we have all the necessary information to report paint
  // timing data. Do so now in order to mark the resulting PaintImage as
  // an LCP candidate.
  ImageResourceContent* image_content = image_resource.CachedImage();
  if (image_content &&
      (IsA<HTMLImageElement>(node) || IsA<HTMLVideoElement>(node)) &&
      image_content->IsLoaded()) {
    LocalDOMWindow* window = layout_image_.GetDocument().domWindow();
    DCHECK(window);
    ImageElementTiming::From(*window).NotifyImagePainted(
        layout_image_, *image_content,
        context.GetPaintController().CurrentPaintChunkProperties(),
        pixel_snapped_dest_rect);
  }

  context.DrawImage(
      image.get(), decode_mode, image_auto_dark_mode,
      ComputeImagePaintTimingInfo(layout_image_, *image, image_content, context,
                                  pixel_snapped_dest_rect),
      gfx::RectF(pixel_snapped_dest_rect), &src_rect, SkBlendMode::kSrcOver,
      respect_orientation);
}

}  // namespace blink
