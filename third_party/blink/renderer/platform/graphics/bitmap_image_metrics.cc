// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/platform/graphics/bitmap_image_metrics.h"

#include "base/metrics/histogram_base.h"
#include "base/metrics/histogram_macros.h"
#include "base/numerics/safe_conversions.h"
#include "media/media_buildflags.h"
#include "third_party/blink/public/common/buildflags.h"
#include "third_party/blink/public/mojom/use_counter/metrics/web_feature.mojom-blink.h"
#include "third_party/blink/renderer/platform/graphics/color_space_gamut.h"
#include "third_party/blink/renderer/platform/instrumentation/histogram.h"
#include "third_party/blink/renderer/platform/instrumentation/use_counter.h"
#include "third_party/blink/renderer/platform/wtf/text/wtf_string.h"
#include "third_party/blink/renderer/platform/wtf/threading.h"
#include "ui/gfx/geometry/size.h"

namespace blink {

namespace {
constexpr base::HistogramBase::Sample kImageAreaHistogramMin = 1;
constexpr base::HistogramBase::Sample kImageAreaHistogramMax = 8192 * 8192;
constexpr int32_t kImageAreaHistogramBucketCount = 100;
}  // namespace

BitmapImageMetrics::DecodedImageType
BitmapImageMetrics::StringToDecodedImageType(const String& type) {
  if (type == "jpg")
    return BitmapImageMetrics::DecodedImageType::kJPEG;
  if (type == "png")
    return BitmapImageMetrics::DecodedImageType::kPNG;
  if (type == "gif")
    return BitmapImageMetrics::DecodedImageType::kGIF;
  if (type == "webp")
    return BitmapImageMetrics::DecodedImageType::kWebP;
  if (type == "ico")
    return BitmapImageMetrics::DecodedImageType::kICO;
  if (type == "bmp")
    return BitmapImageMetrics::DecodedImageType::kBMP;
#if BUILDFLAG(ENABLE_AV1_DECODER)
  if (type == "avif")
    return BitmapImageMetrics::DecodedImageType::kAVIF;
#endif
#if BUILDFLAG(ENABLE_JXL_DECODER)
  if (type == "jxl")
    return BitmapImageMetrics::DecodedImageType::kJXL;
#endif
  return BitmapImageMetrics::DecodedImageType::kUnknown;
}

void BitmapImageMetrics::CountDecodedImageFrameTime(
    const String& type,
    base::TimeDelta elapsed,
    uint64_t original_frame_rect_area,
    bool first) {
  BitmapImageMetrics::DecodedImageType dit = StringToDecodedImageType(type);
  switch (dit) {
    case BitmapImageMetrics::DecodedImageType::kUnknown:
      // No-op.
      break;
    case BitmapImageMetrics::DecodedImageType::kJPEG:
      UMA_HISTOGRAM_TIMES("Blink.ImageDecoders.Jpeg.EveryDecode.Time", elapsed);
      if (first) {
        UMA_HISTOGRAM_TIMES("Blink.ImageDecoders.Jpeg.FirstDecode.Time",
                            elapsed);
      }
      break;
    case BitmapImageMetrics::DecodedImageType::kPNG:
      UMA_HISTOGRAM_TIMES("Blink.ImageDecoders.Png.EveryDecode.Time", elapsed);
      if (first) {
        UMA_HISTOGRAM_TIMES("Blink.ImageDecoders.Png.FirstDecode.Time",
                            elapsed);
        DEFINE_THREAD_SAFE_STATIC_LOCAL(
            CustomCountHistogram, image_area_histogram,
            ("Blink.ImageDecoders.Png.FirstDecode.Area", kImageAreaHistogramMin,
             kImageAreaHistogramMax, kImageAreaHistogramBucketCount));
        // A base::HistogramBase::Sample may not fit |size.Area()|. Hence the
        // use of saturated_cast.
        image_area_histogram.Count(
            base::saturated_cast<base::HistogramBase::Sample>(
                original_frame_rect_area));
      }
      break;
    case BitmapImageMetrics::DecodedImageType::kGIF:
      UMA_HISTOGRAM_TIMES("Blink.ImageDecoders.Gif.EveryDecode.Time", elapsed);
      if (first) {
        UMA_HISTOGRAM_TIMES("Blink.ImageDecoders.Gif.FirstDecode.Time",
                            elapsed);
      }
      break;
    case BitmapImageMetrics::DecodedImageType::kWebP:
      UMA_HISTOGRAM_TIMES("Blink.ImageDecoders.WebP.EveryDecode.Time", elapsed);
      if (first) {
        UMA_HISTOGRAM_TIMES("Blink.ImageDecoders.WebP.FirstDecode.Time",
                            elapsed);
      }
      break;
    case BitmapImageMetrics::DecodedImageType::kICO:
      UMA_HISTOGRAM_TIMES("Blink.ImageDecoders.Ico.EveryDecode.Time", elapsed);
      if (first) {
        UMA_HISTOGRAM_TIMES("Blink.ImageDecoders.Ico.FirstDecode.Time",
                            elapsed);
      }
      break;
    case BitmapImageMetrics::DecodedImageType::kBMP:
      UMA_HISTOGRAM_TIMES("Blink.ImageDecoders.Bmp.EveryDecode.Time", elapsed);
      if (first) {
        UMA_HISTOGRAM_TIMES("Blink.ImageDecoders.Bmp.FirstDecode.Time",
                            elapsed);
      }
      break;
    case BitmapImageMetrics::DecodedImageType::kAVIF:
      UMA_HISTOGRAM_TIMES("Blink.ImageDecoders.Avif.EveryDecode.Time", elapsed);
      if (first) {
        UMA_HISTOGRAM_TIMES("Blink.ImageDecoders.Avif.FirstDecode.Time",
                            elapsed);
      }
      break;
    case BitmapImageMetrics::DecodedImageType::kJXL:
      UMA_HISTOGRAM_TIMES("Blink.ImageDecoders.Jxl.EveryDecode.Time", elapsed);
      if (first) {
        UMA_HISTOGRAM_TIMES("Blink.ImageDecoders.Jxl.FirstDecode.Time",
                            elapsed);
      }
      break;
  }
}

void BitmapImageMetrics::CountDecodedImageType(const String& type) {
  UMA_HISTOGRAM_ENUMERATION("Blink.DecodedImageType",
                            StringToDecodedImageType(type));
}

void BitmapImageMetrics::CountDecodedImageType(const String& type,
                                               UseCounter* use_counter) {
  if (use_counter) {
    if (type == "webp") {
      use_counter->CountUse(WebFeature::kWebPImage);
#if BUILDFLAG(ENABLE_AV1_DECODER)
    } else if (type == "avif") {
      use_counter->CountUse(WebFeature::kAVIFImage);
#endif
    }
  }
}

void BitmapImageMetrics::CountDecodedImageDensity(const String& type,
                                                  int image_min_side,
                                                  uint64_t density_centi_bpp,
                                                  size_t image_size_bytes) {
  // All bpp samples are reported in the range 0.01 to 10 bpp as integer number
  // of 0.01 bpp. We don't report for any sample for small images (0 to 99px on
  // the smallest dimension).
  //
  // The histogram JpegDensity.KiBWeighted reports the number of KiB decoded for
  // a given bpp value.
  if (image_min_side < 100)
    return;
  int image_size_kib = static_cast<int>((image_size_bytes + 512) / 1024);
  if (image_size_kib <= 0)
    return;

  DEFINE_THREAD_SAFE_STATIC_LOCAL(
      CustomCountHistogram, jpeg_density_histogram,
      ("Blink.DecodedImage.JpegDensity.KiBWeighted", 1, 1000, 100));
  DEFINE_THREAD_SAFE_STATIC_LOCAL(
      CustomCountHistogram, webp_density_histogram,
      ("Blink.DecodedImage.WebPDensity.KiBWeighted", 1, 1000, 100));
  DEFINE_THREAD_SAFE_STATIC_LOCAL(
      CustomCountHistogram, avif_density_histogram,
      ("Blink.DecodedImage.AvifDensity.KiBWeighted", 1, 1000, 100));

  CustomCountHistogram* density_histogram = nullptr;
  BitmapImageMetrics::DecodedImageType decoded_image_type =
      StringToDecodedImageType(type);
  switch (decoded_image_type) {
    case BitmapImageMetrics::DecodedImageType::kJPEG:
      density_histogram = &jpeg_density_histogram;
      break;
    case BitmapImageMetrics::DecodedImageType::kWebP:
      density_histogram = &webp_density_histogram;
      break;
    case BitmapImageMetrics::DecodedImageType::kAVIF:
      density_histogram = &avif_density_histogram;
      break;
    default:
      // All other formats are not reported.
      return;
  }

  density_histogram->CountMany(
      base::saturated_cast<base::Histogram::Sample>(density_centi_bpp),
      image_size_kib);
}

void BitmapImageMetrics::CountJpegArea(const gfx::Size& size) {
  DEFINE_THREAD_SAFE_STATIC_LOCAL(
      CustomCountHistogram, image_area_histogram,
      ("Blink.ImageDecoders.Jpeg.Area", kImageAreaHistogramMin,
       kImageAreaHistogramMax, kImageAreaHistogramBucketCount));
  // A base::HistogramBase::Sample may not fit |size.Area()|. Hence the use of
  // saturated_cast.
  image_area_histogram.Count(
      base::saturated_cast<base::HistogramBase::Sample>(size.Area64()));
}

void BitmapImageMetrics::CountJpegColorSpace(JpegColorSpace color_space) {
  UMA_HISTOGRAM_ENUMERATION("Blink.ImageDecoders.Jpeg.ColorSpace", color_space);
}

}  // namespace blink
