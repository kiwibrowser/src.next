/*
 * Copyright (C) 2003, 2006, 2007, 2008, 2009 Apple Inc. All rights reserved.
 * Copyright (C) 2008-2009 Torch Mobile, Inc.
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

#ifndef THIRD_PARTY_BLINK_RENDERER_PLATFORM_GRAPHICS_GRAPHICS_CONTEXT_H_
#define THIRD_PARTY_BLINK_RENDERER_PLATFORM_GRAPHICS_GRAPHICS_CONTEXT_H_

#include <memory>

#include "base/dcheck_is_on.h"
#include "cc/paint/paint_flags.h"
#include "third_party/blink/renderer/platform/fonts/font.h"
#include "third_party/blink/renderer/platform/graphics/dark_mode_filter.h"
#include "third_party/blink/renderer/platform/graphics/dark_mode_settings.h"
#include "third_party/blink/renderer/platform/graphics/dash_array.h"
#include "third_party/blink/renderer/platform/graphics/dom_node_id.h"
#include "third_party/blink/renderer/platform/graphics/draw_looper_builder.h"
#include "third_party/blink/renderer/platform/graphics/graphics_context_state.h"
#include "third_party/blink/renderer/platform/graphics/image_orientation.h"
#include "third_party/blink/renderer/platform/graphics/paint/paint_filter.h"
#include "third_party/blink/renderer/platform/graphics/paint/paint_record.h"
#include "third_party/blink/renderer/platform/graphics/paint/paint_recorder.h"
#include "third_party/blink/renderer/platform/graphics/skia/skia_utils.h"
#include "third_party/blink/renderer/platform/platform_export.h"
#include "third_party/blink/renderer/platform/wtf/allocator/allocator.h"
#include "third_party/blink/renderer/platform/wtf/forward.h"
#include "third_party/skia/include/core/SkClipOp.h"
#include "third_party/skia/include/core/SkRefCnt.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/geometry/skia_conversions.h"
#include "ui/gfx/geometry/vector2d_f.h"

class SkPath;
class SkRRect;
struct SkRect;

namespace paint_preview {
class PaintPreviewTracker;
}  // namespace paint_preview

namespace blink {

class FloatRoundedRect;
class KURL;
class PaintController;
class Path;
struct TextRunPaintInfo;

// Tiling parameters for the DrawImageTiled() method.
struct ImageTilingInfo {
  // The part of the Image (the |image| argument to the method) to tile. It's in
  // the space of the image.
  gfx::RectF image_rect;

  // Scale factor from image space to destination space. Will include
  // image-resolution information.
  gfx::Vector2dF scale{1.0f, 1.0f};

  // Origin of the full image in destination space.
  gfx::PointF phase;

  // Additional spacing between tiles in destination space.
  gfx::SizeF spacing;
};

struct ImageDrawOptions {
  ImageDrawOptions() = default;
  explicit ImageDrawOptions(DarkModeFilter* dark_mode_filter,
                            SkSamplingOptions& sampling_options,
                            RespectImageOrientationEnum respect_orientation,
                            Image::ImageClampingMode clamping_mode,
                            Image::ImageDecodingMode decode_mode,
                            bool apply_dark_mode,
                            bool may_be_lcp_candidate)
      : dark_mode_filter(dark_mode_filter),
        sampling_options(sampling_options),
        respect_orientation(respect_orientation),
        clamping_mode(clamping_mode),
        decode_mode(decode_mode),
        apply_dark_mode(apply_dark_mode),
        may_be_lcp_candidate(may_be_lcp_candidate) {}
  DarkModeFilter* dark_mode_filter = nullptr;
  SkSamplingOptions sampling_options;
  RespectImageOrientationEnum respect_orientation = kRespectImageOrientation;
  Image::ImageClampingMode clamping_mode = Image::kClampImageToSourceRect;
  Image::ImageDecodingMode decode_mode = Image::kSyncDecode;
  bool apply_dark_mode = false;
  bool may_be_lcp_candidate = false;
};

struct AutoDarkMode {
  AutoDarkMode(DarkModeFilter::ElementRole role, bool enabled)
      : role(role), enabled(enabled) {}

  AutoDarkMode(DarkModeFilter::ElementRole role,
               bool enabled,
               SkColor contrast_color)
      : role(role), enabled(enabled), contrast_color(contrast_color) {}

  explicit AutoDarkMode(const ImageDrawOptions& draw_options)
      : role(DarkModeFilter::ElementRole::kBackground),
        enabled(draw_options.apply_dark_mode) {}

  static AutoDarkMode Disabled(DarkModeFilter::ElementRole role =
                                   DarkModeFilter::ElementRole::kBackground) {
    return AutoDarkMode(role, false);
  }

  DarkModeFilter::ElementRole role;
  bool enabled;
  SkColor contrast_color = 0;
};

struct ImageAutoDarkMode : AutoDarkMode {
  ImageAutoDarkMode(DarkModeFilter::ElementRole role,
                    bool enabled,
                    DarkModeFilter::ImageType image_type)
      : AutoDarkMode(role, enabled), image_type(image_type) {}

  static ImageAutoDarkMode Disabled(
      DarkModeFilter::ElementRole role =
          DarkModeFilter::ElementRole::kBackground) {
    return ImageAutoDarkMode(role, false, DarkModeFilter::ImageType::kNone);
  }

  DarkModeFilter::ImageType image_type;
};

struct ImagePaintTimingInfo {
  explicit ImagePaintTimingInfo(bool image_may_be_lcp_candidate)
      : image_may_be_lcp_candidate(image_may_be_lcp_candidate) {}
  ImagePaintTimingInfo(bool image_may_be_lcp_candidate,
                       bool report_paint_timing)
      : image_may_be_lcp_candidate(image_may_be_lcp_candidate),
        report_paint_timing(report_paint_timing) {}
  ImagePaintTimingInfo() = default;
  bool image_may_be_lcp_candidate = false;
  // Whether |PaintController::SetImagePainted| should be called if the image
  // is painted.
  bool report_paint_timing = true;
};

class PLATFORM_EXPORT GraphicsContext {
  USING_FAST_MALLOC(GraphicsContext);

 public:
  explicit GraphicsContext(PaintController&);
  GraphicsContext(const GraphicsContext&) = delete;
  GraphicsContext& operator=(const GraphicsContext&) = delete;
  ~GraphicsContext();

  // Copy configs such as printing, dark mode, device scale factor etc. from
  // another GraphicsContext.
  void CopyConfigFrom(GraphicsContext&);

  void SetPrintingMetafile(printing::MetafileSkia* metafile) {
    printing_metafile_ = metafile;
  }

  void SetPaintPreviewTracker(paint_preview::PaintPreviewTracker* tracker) {
    paint_preview_tracker_ = tracker;
  }

  cc::PaintCanvas* Canvas() { return canvas_; }
  const cc::PaintCanvas* Canvas() const { return canvas_; }

  PaintController& GetPaintController() { return paint_controller_; }
  const PaintController& GetPaintController() const {
    return paint_controller_;
  }

  DarkModeFilter* GetDarkModeFilter();
  DarkModeFilter* GetDarkModeFilterForImage(
      const ImageAutoDarkMode& auto_dark_mode);

  void UpdateDarkModeSettingsForTest(const DarkModeSettings&);

  // ---------- State management methods -----------------
  void Save();
  void Restore();

#if DCHECK_IS_ON()
  unsigned SaveCount() const;
#endif

  float StrokeThickness() const {
    return ImmutableState()->GetStrokeData().Thickness();
  }
  void SetStrokeThickness(float thickness) {
    MutableState()->SetStrokeThickness(thickness);
  }

  StrokeStyle GetStrokeStyle() const {
    return ImmutableState()->GetStrokeData().Style();
  }
  void SetStrokeStyle(StrokeStyle style) {
    MutableState()->SetStrokeStyle(style);
  }

  Color StrokeColor() const { return ImmutableState()->StrokeColor(); }
  void SetStrokeColor(const Color& color) {
    MutableState()->SetStrokeColor(color);
  }

  void SetLineCap(LineCap cap) { MutableState()->SetLineCap(cap); }
  void SetLineDash(const DashArray& dashes, float dash_offset) {
    MutableState()->SetLineDash(dashes, dash_offset);
  }
  void SetLineJoin(LineJoin join) { MutableState()->SetLineJoin(join); }
  void SetMiterLimit(float limit) { MutableState()->SetMiterLimit(limit); }

  Color FillColor() const { return ImmutableState()->FillColor(); }
  void SetFillColor(const Color& color) { MutableState()->SetFillColor(color); }

  void SetShouldAntialias(bool antialias) {
    MutableState()->SetShouldAntialias(antialias);
  }
  bool ShouldAntialias() const { return ImmutableState()->ShouldAntialias(); }

  void SetTextDrawingMode(TextDrawingModeFlags mode) {
    MutableState()->SetTextDrawingMode(mode);
  }
  TextDrawingModeFlags TextDrawingMode() const {
    return ImmutableState()->TextDrawingMode();
  }

  void SetImageInterpolationQuality(InterpolationQuality quality) {
    MutableState()->SetInterpolationQuality(quality);
  }
  InterpolationQuality ImageInterpolationQuality() const {
    return ImmutableState()->GetInterpolationQuality();
  }

  SkSamplingOptions ImageSamplingOptions() const {
    return cc::PaintFlags::FilterQualityToSkSamplingOptions(
        static_cast<cc::PaintFlags::FilterQuality>(
            ImageInterpolationQuality()));
  }

  // Specify the device scale factor which may change the way document markers
  // and fonts are rendered.
  void SetDeviceScaleFactor(float factor) { device_scale_factor_ = factor; }
  float DeviceScaleFactor() const { return device_scale_factor_; }

  // Set to true if context is for printing. Bitmaps won't be resampled when
  // printing to keep the best possible quality. When printing text will be
  // provided along with glyphs.
  void SetPrinting(bool printing) { printing_ = printing; }

  SkColorFilter* GetColorFilter() const;
  void SetColorFilter(ColorFilter);
  // ---------- End state management methods -----------------

  // DrawRect() fills and always strokes using a 1-pixel stroke inset from
  // the rect borders (of the pre-set stroke color).
  void DrawRect(const gfx::Rect&, const AutoDarkMode& auto_dark_mode);

  // DrawLine() only operates on horizontal or vertical lines and uses the
  // current stroke settings. For dotted or dashed stroke, the line need to be
  // top-to-down or left-to-right to get correct interval of dots/dashes.
  void DrawLine(const gfx::Point&,
                const gfx::Point&,
                const AutoDarkMode& auto_dark_mode,
                bool is_text_line = false,
                const cc::PaintFlags* flags = nullptr);

  void FillPath(const Path&, const AutoDarkMode& auto_dark_mode);

  // The length parameter is only used when the path has a dashed or dotted
  // stroke style, with the default dash/dot path effect. If a non-zero length
  // is provided the number of dashes/dots on a dashed/dotted
  // line will be adjusted to start and end that length with a dash/dot.
  // The dash_thickness parameter is only used when drawing dashed borders,
  // where the stroke thickness has been set for corner miters but we want the
  // dash length set from the border width.
  void StrokePath(const Path&,
                  const AutoDarkMode& auto_dark_mode,
                  const int length = 0,
                  const int dash_thickness = 0);

  void FillEllipse(const gfx::RectF&, const AutoDarkMode& auto_dark_mode);
  void StrokeEllipse(const gfx::RectF&, const AutoDarkMode& auto_dark_mode);

  void FillRect(const gfx::Rect&, const AutoDarkMode& auto_dark_mode);
  void FillRect(const gfx::Rect&,
                const Color&,
                const AutoDarkMode& auto_dark_mode,
                SkBlendMode = SkBlendMode::kSrcOver);
  void FillRect(const gfx::RectF&, const AutoDarkMode& auto_dark_mode);
  void FillRect(const gfx::RectF&,
                const Color&,
                const AutoDarkMode& auto_dark_mode,
                SkBlendMode = SkBlendMode::kSrcOver);
  void FillRoundedRect(const FloatRoundedRect&,
                       const Color&,
                       const AutoDarkMode& auto_dark_mode);
  void FillDRRect(const FloatRoundedRect&,
                  const FloatRoundedRect&,
                  const Color&,
                  const AutoDarkMode& auto_dark_mode);
  void FillRectWithRoundedHole(const gfx::RectF&,
                               const FloatRoundedRect& rounded_hole_rect,
                               const Color&,
                               const AutoDarkMode& auto_dark_mode);

  void StrokeRect(const gfx::RectF&,
                  float line_width,
                  const AutoDarkMode& auto_dark_mode);

  void DrawRecord(sk_sp<const PaintRecord>);
  void CompositeRecord(sk_sp<PaintRecord>,
                       const gfx::RectF& dest,
                       const gfx::RectF& src,
                       SkBlendMode);
  void DrawImage(Image*,
                 Image::ImageDecodingMode,
                 const ImageAutoDarkMode& auto_dark_mode,
                 const ImagePaintTimingInfo& paint_timing_info,
                 const gfx::RectF& dest_rect,
                 const gfx::RectF* src_rect = nullptr,
                 SkBlendMode = SkBlendMode::kSrcOver,
                 RespectImageOrientationEnum = kRespectImageOrientation,
                 Image::ImageClampingMode clamping_mode =
                     Image::ImageClampingMode::kClampImageToSourceRect);
  void DrawImageRRect(Image*,
                      Image::ImageDecodingMode,
                      const ImageAutoDarkMode& auto_dark_mode,
                      const ImagePaintTimingInfo& paint_timing_info,
                      const FloatRoundedRect& dest,
                      const gfx::RectF& src_rect,
                      SkBlendMode = SkBlendMode::kSrcOver,
                      RespectImageOrientationEnum = kRespectImageOrientation,
                      Image::ImageClampingMode clamping_mode =
                          Image::ImageClampingMode::kClampImageToSourceRect);
  void DrawImageTiled(Image* image,
                      const gfx::RectF& dest_rect,
                      const ImageTilingInfo& tiling_info,
                      const ImageAutoDarkMode& auto_dark_mode,
                      const ImagePaintTimingInfo& paint_timing_info,
                      SkBlendMode = SkBlendMode::kSrcOver,
                      RespectImageOrientationEnum = kRespectImageOrientation);
  void SetImagePainted(bool report_paint_timing);
  // These methods write to the canvas.
  // Also drawLine(const gfx::Point& point1, const gfx::Point& point2) and
  // fillRoundedRect().
  void DrawOval(const SkRect&,
                const cc::PaintFlags&,
                const AutoDarkMode& auto_dark_mode);
  void DrawPath(const SkPath&,
                const cc::PaintFlags&,
                const AutoDarkMode& auto_dark_mode);
  void DrawRect(const SkRect&,
                const cc::PaintFlags&,
                const AutoDarkMode& auto_dark_mode);
  void DrawRRect(const SkRRect&,
                 const cc::PaintFlags&,
                 const AutoDarkMode& auto_dark_mode);

  void Clip(const gfx::Rect& rect) { ClipRect(gfx::RectToSkRect(rect)); }
  void Clip(const gfx::RectF& rect) { ClipRect(gfx::RectFToSkRect(rect)); }
  void ClipRoundedRect(const FloatRoundedRect&,
                       SkClipOp = SkClipOp::kIntersect,
                       AntiAliasingMode = kAntiAliased);
  void ClipOut(const gfx::Rect& rect) {
    ClipRect(gfx::RectToSkRect(rect), kNotAntiAliased, SkClipOp::kDifference);
  }
  void ClipOut(const gfx::RectF& rect) {
    ClipRect(gfx::RectFToSkRect(rect), kNotAntiAliased, SkClipOp::kDifference);
  }
  void ClipOut(const Path&);
  void ClipOutRoundedRect(const FloatRoundedRect&);
  void ClipPath(const SkPath&,
                AntiAliasingMode = kNotAntiAliased,
                SkClipOp = SkClipOp::kIntersect);
  void ClipRect(const SkRect&,
                AntiAliasingMode = kNotAntiAliased,
                SkClipOp = SkClipOp::kIntersect);

  void DrawText(const Font&,
                const TextRunPaintInfo&,
                const gfx::PointF&,
                DOMNodeId,
                const AutoDarkMode& auto_dark_mode);
  void DrawText(const Font&,
                const NGTextFragmentPaintInfo&,
                const gfx::PointF&,
                DOMNodeId,
                const AutoDarkMode& auto_dark_mode);

  // TODO(layout-dev): This method is only used by SVGInlineTextBoxPainter, see
  // if we can change that to use the four parameter version above.
  void DrawText(const Font&,
                const TextRunPaintInfo&,
                const gfx::PointF&,
                const cc::PaintFlags&,
                DOMNodeId,
                const AutoDarkMode& auto_dark_mode);

  // TODO(layout-dev): This method is only used by NGTextPainter, see if the
  // four parameter overload can be removed or if it can wrap this method.
  void DrawText(const Font&,
                const NGTextFragmentPaintInfo&,
                const gfx::PointF&,
                const cc::PaintFlags&,
                DOMNodeId,
                const AutoDarkMode& auto_dark_mode);

  void DrawEmphasisMarks(const Font&,
                         const TextRunPaintInfo&,
                         const AtomicString& mark,
                         const gfx::PointF&,
                         const AutoDarkMode& auto_dark_mode);
  void DrawEmphasisMarks(const Font&,
                         const NGTextFragmentPaintInfo&,
                         const AtomicString& mark,
                         const gfx::PointF&,
                         const AutoDarkMode& auto_dark_mode);

  void DrawBidiText(
      const Font&,
      const TextRunPaintInfo&,
      const gfx::PointF&,
      const AutoDarkMode& auto_dark_mode,
      Font::CustomFontNotReadyAction = Font::kDoNotPaintIfFontNotReady);
  void DrawHighlightForText(const Font&,
                            const TextRun&,
                            const gfx::PointF&,
                            int h,
                            const Color& background_color,
                            const AutoDarkMode& auto_dark_mode,
                            int from = 0,
                            int to = -1);

  void DrawLineForText(const gfx::PointF&,
                       float width,
                       const AutoDarkMode& auto_dark_mode,
                       const cc::PaintFlags* flags = nullptr);

  // beginLayer()/endLayer() behave like save()/restore() for CTM and clip
  // states. Apply SkBlendMode when the layer is composited on the backdrop
  // (i.e. endLayer()).
  void BeginLayer(float opacity = 1.0f,
                  SkBlendMode = SkBlendMode::kSrcOver,
                  const gfx::RectF* = nullptr,
                  ColorFilter = kColorFilterNone,
                  sk_sp<PaintFilter> = nullptr);
  void EndLayer();

  // Instead of being dispatched to the active canvas, draw commands following
  // beginRecording() are stored in a display list that can be replayed at a
  // later time. Pass in the bounding rectangle for the content in the list.
  void BeginRecording(const gfx::RectF&);

  // Returns a record with any recorded draw commands since the prerequisite
  // call to beginRecording().  The record is guaranteed to be non-null (but
  // not necessarily non-empty), even when the context is disabled.
  sk_sp<PaintRecord> EndRecording();

  void SetDrawLooper(sk_sp<SkDrawLooper>);

  void DrawFocusRingPath(const SkPath&,
                         const Color&,
                         float width,
                         float corner_radius,
                         const AutoDarkMode& auto_dark_mode);
  void DrawFocusRingRect(const SkRRect&,
                         const Color&,
                         float width,
                         const AutoDarkMode& auto_dark_mode);

  const cc::PaintFlags& FillFlags() const {
    return ImmutableState()->FillFlags();
  }
  // If the length of the path to be stroked is known, pass it in for correct
  // dash or dot placement. Border painting uses a stroke thickness determined
  // by the corner miters. Set the dash_thickness to a non-zero number for
  // cases where dashes should be based on a different thickness.
  const cc::PaintFlags& StrokeFlags(const int length = 0,
                                    const int dash_thickness = 0) const {
    return ImmutableState()->StrokeFlags(length, dash_thickness);
  }

  // ---------- Transformation methods -----------------
  void ConcatCTM(const AffineTransform&);

  void Scale(float x, float y);
  void Rotate(float angle_in_radians);
  void Translate(float x, float y);
  // ---------- End transformation methods -----------------

  cc::PaintFlags::FilterQuality ComputeFilterQuality(
      Image*,
      const gfx::RectF& dest,
      const gfx::RectF& src) const;

  SkSamplingOptions ComputeSamplingOptions(Image* image,
                                           const gfx::RectF& dest,
                                           const gfx::RectF& src) const {
    return cc::PaintFlags::FilterQualityToSkSamplingOptions(
        ComputeFilterQuality(image, dest, src));
  }

  // Sets target URL of a clickable area.
  void SetURLForRect(const KURL&, const gfx::Rect&);

  // Sets the destination of a clickable area of a URL fragment (in a URL
  // pointing to the same web page). When the area is clicked, the page should
  // be scrolled to the location set by setURLDestinationLocation() for the
  // destination whose name is |name|.
  void SetURLFragmentForRect(const String& name, const gfx::Rect&);

  // Sets location of a URL destination (a.k.a. anchor) in the page.
  void SetURLDestinationLocation(const String& name, const gfx::Point&);

  static void AdjustLineToPixelBoundaries(gfx::PointF& p1,
                                          gfx::PointF& p2,
                                          float stroke_width);

  static Path GetPathForTextLine(const gfx::PointF&,
                                 float width,
                                 float stroke_thickness,
                                 StrokeStyle);
  static bool ShouldUseStrokeForTextLine(StrokeStyle);

  void SetInDrawingRecorder(bool);
  bool InDrawingRecorder() const { return in_drawing_recorder_; }

  // Set the DOM Node Id on the canvas. This is used to associate
  // the drawing commands with the structure tree for the page when
  // creating a tagged PDF. Callers are responsible for restoring it.
  void SetDOMNodeId(DOMNodeId);
  DOMNodeId GetDOMNodeId() const;
  bool NeedsDOMNodeId() const { return printing_; }

  static sk_sp<SkColorFilter> WebCoreColorFilterToSkiaColorFilter(ColorFilter);

 private:
  const GraphicsContextState* ImmutableState() const { return paint_state_; }

  GraphicsContextState* MutableState() {
    RealizePaintSave();
    return paint_state_;
  }

  template <typename TextPaintInfo>
  void DrawTextInternal(const Font&,
                        const TextPaintInfo&,
                        const gfx::PointF&,
                        DOMNodeId,
                        const AutoDarkMode& auto_dark_mode);

  template <typename TextPaintInfo>
  void DrawEmphasisMarksInternal(const Font&,
                                 const TextPaintInfo&,
                                 const AtomicString& mark,
                                 const gfx::PointF&,
                                 const AutoDarkMode& auto_dark_mode);

  template <typename DrawTextFunc>
  void DrawTextPasses(const AutoDarkMode& auto_dark_mode, const DrawTextFunc&);

  void SaveLayer(const SkRect* bounds, const cc::PaintFlags*);
  void RestoreLayer();

  // SkCanvas wrappers.
  void ClipRRect(const SkRRect&,
                 AntiAliasingMode = kNotAntiAliased,
                 SkClipOp = SkClipOp::kIntersect);
  void Concat(const SkMatrix&);

  // Apply deferred paint state saves
  void RealizePaintSave() {
    if (paint_state_->SaveCount()) {
      paint_state_->DecrementSaveCount();
      ++paint_state_index_;
      if (paint_state_stack_.size() == paint_state_index_) {
        paint_state_stack_.push_back(
            GraphicsContextState::CreateAndCopy(*paint_state_));
        paint_state_ = paint_state_stack_[paint_state_index_].get();
      } else {
        GraphicsContextState* prior_paint_state = paint_state_;
        paint_state_ = paint_state_stack_[paint_state_index_].get();
        paint_state_->Copy(*prior_paint_state);
      }
    }
  }

  class DarkModeFlags;

  // This is owned by paint_recorder_. Never delete this object.
  // Drawing operations are allowed only after the first BeginRecording() which
  // initializes this to not null.
  cc::PaintCanvas* canvas_ = nullptr;

  PaintController& paint_controller_;

  // Paint states stack. The state controls the appearance of drawn content, so
  // this stack enables local drawing state changes with save()/restore() calls.
  // We do not delete from this stack to avoid memory churn.
  Vector<std::unique_ptr<GraphicsContextState>> paint_state_stack_;

  // Current index on the stack. May not be the last thing on the stack.
  wtf_size_t paint_state_index_ = 0;

  // Raw pointer to the current state.
  GraphicsContextState* paint_state_ = nullptr;

  PaintRecorder paint_recorder_;

  printing::MetafileSkia* printing_metafile_ = nullptr;
  paint_preview::PaintPreviewTracker* paint_preview_tracker_ = nullptr;

#if DCHECK_IS_ON()
  int layer_count_ = 0;
  bool disable_destruction_checks_ = false;
#endif

  float device_scale_factor_ = 1.0f;

  std::unique_ptr<DarkModeFilter> dark_mode_filter_;

  bool printing_ = false;
  bool in_drawing_recorder_ = false;

  // The current node ID, which is used for marked content in a tagged PDF.
  DOMNodeId dom_node_id_ = kInvalidDOMNodeId;
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_PLATFORM_GRAPHICS_GRAPHICS_CONTEXT_H_
