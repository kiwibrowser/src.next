// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "third_party/blink/renderer/core/paint/paint_timing_detector.h"

#include "third_party/blink/public/common/features.h"
#include "third_party/blink/public/common/input/web_input_event.h"
#include "third_party/blink/public/common/performance/largest_contentful_paint_type.h"
#include "third_party/blink/renderer/core/dom/document.h"
#include "third_party/blink/renderer/core/frame/local_frame.h"
#include "third_party/blink/renderer/core/frame/local_frame_view.h"
#include "third_party/blink/renderer/core/frame/web_frame_widget_impl.h"
#include "third_party/blink/renderer/core/frame/web_local_frame_impl.h"
#include "third_party/blink/renderer/core/html/html_element.h"
#include "third_party/blink/renderer/core/html/html_image_element.h"
#include "third_party/blink/renderer/core/layout/layout_box_model_object.h"
#include "third_party/blink/renderer/core/layout/layout_object.h"
#include "third_party/blink/renderer/core/layout/layout_view.h"
#include "third_party/blink/renderer/core/loader/document_loader.h"
#include "third_party/blink/renderer/core/loader/resource/image_resource_content.h"
#include "third_party/blink/renderer/core/page/chrome_client.h"
#include "third_party/blink/renderer/core/page/page.h"
#include "third_party/blink/renderer/core/paint/image_paint_timing_detector.h"
#include "third_party/blink/renderer/core/paint/largest_contentful_paint_calculator.h"
#include "third_party/blink/renderer/core/paint/paint_layer.h"
#include "third_party/blink/renderer/core/paint/text_paint_timing_detector.h"
#include "third_party/blink/renderer/core/style/style_fetched_image.h"
#include "third_party/blink/renderer/core/svg/graphics/svg_image.h"
#include "third_party/blink/renderer/core/timing/dom_window_performance.h"
#include "third_party/blink/renderer/platform/graphics/bitmap_image.h"
#include "third_party/blink/renderer/platform/graphics/graphics_context.h"
#include "third_party/blink/renderer/platform/graphics/image.h"
#include "third_party/blink/renderer/platform/graphics/paint/float_clip_rect.h"
#include "third_party/blink/renderer/platform/graphics/paint/geometry_mapper.h"
#include "third_party/blink/renderer/platform/graphics/paint/property_tree_state.h"
#include "third_party/blink/renderer/platform/graphics/paint/scoped_paint_chunk_properties.h"
#include "third_party/blink/renderer/platform/graphics/static_bitmap_image.h"
#include "third_party/blink/renderer/platform/runtime_enabled_features.h"
#include "third_party/blink/renderer/platform/wtf/cross_thread_copier_std.h"
#include "third_party/blink/renderer/platform/wtf/cross_thread_functional.h"
#include "third_party/blink/renderer/platform/wtf/functional.h"
#include "ui/gfx/geometry/rect.h"

namespace blink {

namespace {

// In the context of FCP++, we define contentful background image as one that
// satisfies all of the following conditions:
// * has image reources attached to style of the object, i.e.,
//  { background-image: url('example.gif') }
// * not attached to <body> or <html>
// This function contains the above heuristics.
bool IsBackgroundImageContentful(const LayoutObject& object,
                                 const Image& image) {
  // Background images attached to <body> or <html> are likely for background
  // purpose, so we rule them out.
  if (IsA<LayoutView>(object) || object.IsBody() ||
      object.IsDocumentElement()) {
    return false;
  }

  DCHECK(!image.IsSVGImage());
  if (!base::FeatureList::IsEnabled(features::kIncludeBackgroundSVGInLCP) &&
      image.IsSVGImageForContainer())
    return false;
  return true;
}

}  // namespace

PaintTimingDetector::PaintTimingDetector(LocalFrameView* frame_view)
    : frame_view_(frame_view),
      text_paint_timing_detector_(
          MakeGarbageCollected<TextPaintTimingDetector>(frame_view,
                                                        this,
                                                        nullptr /*set later*/)),
      image_paint_timing_detector_(
          MakeGarbageCollected<ImagePaintTimingDetector>(
              frame_view,
              nullptr /*set later*/)),
      callback_manager_(
          MakeGarbageCollected<PaintTimingCallbackManagerImpl>(frame_view)) {
  if (PaintTimingVisualizer::IsTracingEnabled())
    visualizer_.emplace();
  text_paint_timing_detector_->ResetCallbackManager(callback_manager_.Get());
  image_paint_timing_detector_->ResetCallbackManager(callback_manager_.Get());
}

void PaintTimingDetector::NotifyPaintFinished() {
  if (PaintTimingVisualizer::IsTracingEnabled()) {
    if (!visualizer_)
      visualizer_.emplace();
    visualizer_->RecordMainFrameViewport(*frame_view_);
  } else {
    visualizer_.reset();
  }
  text_paint_timing_detector_->OnPaintFinished();
  if (image_paint_timing_detector_) {
    image_paint_timing_detector_->OnPaintFinished();
  }
  if (callback_manager_->CountCallbacks() > 0)
    callback_manager_->RegisterPaintTimeCallbackForCombinedCallbacks();
  LocalDOMWindow* window = frame_view_->GetFrame().DomWindow();
  if (window) {
    DOMWindowPerformance::performance(*window)->OnPaintFinished();
  }
  if (Document* document = frame_view_->GetFrame().GetDocument())
    document->OnPaintFinished();
}

// static
bool PaintTimingDetector::NotifyBackgroundImagePaint(
    const Node& node,
    const Image& image,
    const StyleFetchedImage& style_image,
    const PropertyTreeStateOrAlias& current_paint_chunk_properties,
    const gfx::Rect& image_border) {
  DCHECK(style_image.CachedImage());
  LayoutObject* object = node.GetLayoutObject();
  if (!object)
    return false;
  LocalFrameView* frame_view = object->GetFrameView();
  if (!frame_view)
    return false;

  ImagePaintTimingDetector* detector =
      frame_view->GetPaintTimingDetector().GetImagePaintTimingDetector();
  if (!detector)
    return false;

  if (!IsBackgroundImageContentful(*object, image))
    return false;

  ImageResourceContent* cached_image = style_image.CachedImage();
  DCHECK(cached_image);
  // TODO(yoav): |image| and |cached_image.GetImage()| are not the same here in
  // the case of SVGs. Figure out why and if we can remove this footgun.

  return detector->RecordImage(*object, image.Size(), *cached_image,
                               current_paint_chunk_properties, &style_image,
                               image_border);
}

// static
bool PaintTimingDetector::NotifyImagePaint(
    const LayoutObject& object,
    const gfx::Size& intrinsic_size,
    const MediaTiming& media_timing,
    const PropertyTreeStateOrAlias& current_paint_chunk_properties,
    const gfx::Rect& image_border) {
  if (IgnorePaintTimingScope::ShouldIgnore())
    return false;
  LocalFrameView* frame_view = object.GetFrameView();
  if (!frame_view)
    return false;
  ImagePaintTimingDetector* detector =
      frame_view->GetPaintTimingDetector().GetImagePaintTimingDetector();
  if (!detector)
    return false;

  return detector->RecordImage(object, intrinsic_size, media_timing,
                               current_paint_chunk_properties, nullptr,
                               image_border);
}

void PaintTimingDetector::NotifyImageFinished(const LayoutObject& object,
                                              const MediaTiming* media_timing) {
  if (IgnorePaintTimingScope::ShouldIgnore())
    return;
  if (image_paint_timing_detector_)
    image_paint_timing_detector_->NotifyImageFinished(object, media_timing);
}

void PaintTimingDetector::LayoutObjectWillBeDestroyed(
    const LayoutObject& object) {
  text_paint_timing_detector_->LayoutObjectWillBeDestroyed(object);
}

void PaintTimingDetector::NotifyImageRemoved(
    const LayoutObject& object,
    const ImageResourceContent* cached_image) {
  if (image_paint_timing_detector_) {
    image_paint_timing_detector_->NotifyImageRemoved(object, cached_image);
  }
}

void PaintTimingDetector::OnInputOrScroll() {
  // If we have already stopped, then abort. |image_paint_timing_detector_|
  // being nullptr is a reliable way to tell if we have already aborted or not
  // because it is initialized on the constructor and only destroyed on this
  // method.
  if (!image_paint_timing_detector_)
    return;

  // TextPaintTimingDetector is used for both Largest Contentful Paint and for
  // Element Timing. Therefore, here we only want to stop recording Largest
  // Contentful Paint.
  text_paint_timing_detector_->StopRecordingLargestTextPaint();
  // ImagePaintTimingDetector is currently only being used for
  // LargestContentfulPaint.
  if (image_paint_timing_detector_) {
    image_paint_timing_detector_->StopRecordEntries();
    image_paint_timing_detector_ = nullptr;
  }
  largest_contentful_paint_calculator_ = nullptr;

  DCHECK_EQ(first_input_or_scroll_notified_timestamp_, base::TimeTicks());
  first_input_or_scroll_notified_timestamp_ = base::TimeTicks::Now();
  DidChangePerformanceTiming();
}

void PaintTimingDetector::NotifyInputEvent(WebInputEvent::Type type) {
  // A single keyup event should be ignored. It could be caused by user actions
  // such as refreshing via Ctrl+R.
  if (type == WebInputEvent::Type::kMouseMove ||
      type == WebInputEvent::Type::kMouseEnter ||
      type == WebInputEvent::Type::kMouseLeave ||
      type == WebInputEvent::Type::kKeyUp ||
      WebInputEvent::IsPinchGestureEventType(type)) {
    return;
  }
  OnInputOrScroll();
}

void PaintTimingDetector::NotifyScroll(mojom::blink::ScrollType scroll_type) {
  if (scroll_type != mojom::blink::ScrollType::kUser &&
      scroll_type != mojom::blink::ScrollType::kCompositor)
    return;
  OnInputOrScroll();
}

bool PaintTimingDetector::NeedToNotifyInputOrScroll() const {
  DCHECK(text_paint_timing_detector_);
  return text_paint_timing_detector_->IsRecordingLargestTextPaint() ||
         image_paint_timing_detector_;
}

LargestContentfulPaintCalculator*
PaintTimingDetector::GetLargestContentfulPaintCalculator() {
  if (largest_contentful_paint_calculator_)
    return largest_contentful_paint_calculator_;

  auto* dom_window = frame_view_->GetFrame().DomWindow();
  if (!dom_window)
    return nullptr;

  largest_contentful_paint_calculator_ =
      MakeGarbageCollected<LargestContentfulPaintCalculator>(
          DOMWindowPerformance::performance(*dom_window));
  return largest_contentful_paint_calculator_;
}

bool PaintTimingDetector::NotifyIfChangedLargestImagePaint(
    base::TimeTicks image_paint_time,
    uint64_t image_paint_size,
    ImageRecord* image_record,
    double image_bpp) {
  // (Experimental) Images with insufficient entropy are not considered
  // candidates for LCP
  if (base::FeatureList::IsEnabled(features::kExcludeLowEntropyImagesFromLCP)) {
    if (image_bpp < features::kMinimumEntropyForLCP.Get())
      return false;
  }
  if (!HasLargestImagePaintChanged(image_paint_time, image_paint_size))
    return false;

  largest_contentful_paint_type_ = blink::LargestContentfulPaintType::kNone;
  if (image_record) {
    Node* image_node = DOMNodeIds::NodeForId(image_record->node_id);
    HTMLImageElement* element = DynamicTo<HTMLImageElement>(image_node);
    if (element && !image_node->IsInShadowTree() &&
        element->IsChangedShortlyAfterMouseover()) {
      largest_contentful_paint_type_ |=
          blink::LargestContentfulPaintType::kAfterMouseover;
    }
    // TODO(yoav): Once we'd enable the kLCPAnimatedImagesReporting flag by
    // default, we'd be able to use the value of
    // largest_image_record->first_animated_frame_time directly.
    if (image_record && image_record->media_timing &&
        image_record->media_timing->IsPaintedFirstFrame()) {
      // Set the animated image flag.
      largest_contentful_paint_type_ |=
          blink::LargestContentfulPaintType::kAnimatedImage;
    }
  }
  largest_image_paint_time_ = image_paint_time;
  largest_image_paint_size_ = image_paint_size;
  largest_contentful_paint_image_bpp_ = image_bpp;
  UpdateLargestContentfulPaintTime();
  DidChangePerformanceTiming();
  return true;
}

bool PaintTimingDetector::NotifyIfChangedLargestTextPaint(
    base::TimeTicks text_paint_time,
    uint64_t text_paint_size) {
  if (!HasLargestTextPaintChanged(text_paint_time, text_paint_size))
    return false;
  if (largest_text_paint_size_ < text_paint_size) {
    DCHECK(!text_paint_time.is_null());
    largest_text_paint_time_ = text_paint_time;
    largest_text_paint_size_ = text_paint_size;
  }
  UpdateLargestContentfulPaintTime();
  DidChangePerformanceTiming();
  return true;
}

void PaintTimingDetector::UpdateLargestContentfulPaintTime() {
  if (largest_text_paint_size_ > largest_image_paint_size_) {
    largest_contentful_paint_time_ = largest_text_paint_time_;
  } else if (largest_text_paint_size_ < largest_image_paint_size_) {
    largest_contentful_paint_time_ = largest_image_paint_time_;
  } else {
    // Size is the same, take the shorter time.
    largest_contentful_paint_time_ =
        std::min(largest_text_paint_time_, largest_image_paint_time_);
  }
}

bool PaintTimingDetector::HasLargestImagePaintChanged(
    base::TimeTicks largest_image_paint_time,
    uint64_t largest_image_paint_size) const {
  return largest_image_paint_time != largest_image_paint_time_ ||
         largest_image_paint_size != largest_image_paint_size_;
}

bool PaintTimingDetector::HasLargestTextPaintChanged(
    base::TimeTicks largest_text_paint_time,
    uint64_t largest_text_paint_size) const {
  return largest_text_paint_time != largest_text_paint_time_ ||
         largest_text_paint_size != largest_text_paint_size_;
}

void PaintTimingDetector::DidChangePerformanceTiming() {
  Document* document = frame_view_->GetFrame().GetDocument();
  if (!document)
    return;
  DocumentLoader* loader = document->Loader();
  if (!loader)
    return;
  loader->DidChangePerformanceTiming();
}

gfx::RectF PaintTimingDetector::BlinkSpaceToDIPs(const gfx::RectF& rect) const {
  FrameWidget* widget = frame_view_->GetFrame().GetWidgetForLocalRoot();
  // May be nullptr in tests.
  if (!widget)
    return rect;
  return widget->BlinkSpaceToDIPs(rect);
}

gfx::RectF PaintTimingDetector::CalculateVisualRect(
    const gfx::Rect& visual_rect,
    const PropertyTreeStateOrAlias& current_paint_chunk_properties) const {
  // This case should be dealt with outside the function.
  DCHECK(!visual_rect.IsEmpty());

  // As Layout objects live in different transform spaces, the object's rect
  // should be projected to the viewport's transform space.
  FloatClipRect float_clip_visual_rect((gfx::RectF(visual_rect)));
  const LocalFrame& local_root = frame_view_->GetFrame().LocalFrameRoot();
  GeometryMapper::LocalToAncestorVisualRect(current_paint_chunk_properties,
                                            local_root.ContentLayoutObject()
                                                ->FirstFragment()
                                                .LocalBorderBoxProperties(),
                                            float_clip_visual_rect);
  if (local_root.IsOutermostMainFrame()) {
    return BlinkSpaceToDIPs(float_clip_visual_rect.Rect());
  }

  // TODO(crbug.com/1346602): Enabling frames from a fenced frame tree to map
  // to the outermost main frame enables fenced content to learn about its
  // position in the embedder which can be used to communicate from embedder to
  // embeddee. For now, return the rect in the local root (not great for remote
  // frames) to avoid introducing a side channel but this will require design
  // work to fix in the long term.
  if (local_root.IsInFencedFrameTree()) {
    return BlinkSpaceToDIPs(float_clip_visual_rect.Rect());
  }

  // OOPIF. The final rect lives in the iframe's root frame space. We need to
  // project it to the top frame space.
  auto layout_visual_rect =
      PhysicalRect::EnclosingRect(float_clip_visual_rect.Rect());
  frame_view_->GetFrame()
      .LocalFrameRoot()
      .View()
      ->MapToVisualRectInRemoteRootFrame(layout_visual_rect);
  return BlinkSpaceToDIPs(gfx::RectF(layout_visual_rect));
}

void PaintTimingDetector::UpdateLargestContentfulPaintCandidate() {
  auto* lcp_calculator = GetLargestContentfulPaintCalculator();
  if (!lcp_calculator)
    return;

  // * nullptr means there is no new candidate update, which could be caused by
  // user input or no content show up on the page.
  // * Record.paint_time == 0 means there is an image but the image is still
  // loading. The perf API should wait until the paint-time is available.
  const TextRecord* largest_text_record = nullptr;
  const ImageRecord* largest_image_record = nullptr;
  if (auto* text_timing_detector = GetTextPaintTimingDetector()) {
    if (text_timing_detector->IsRecordingLargestTextPaint()) {
      largest_text_record = text_timing_detector->UpdateCandidate();
    }
  }
  if (auto* image_timing_detector = GetImagePaintTimingDetector()) {
    largest_image_record = image_timing_detector->UpdateCandidate();
  }

  lcp_calculator->UpdateLargestContentfulPaintIfNeeded(largest_text_record,
                                                       largest_image_record);
}

void PaintTimingDetector::ReportIgnoredContent() {
  if (auto* text_timing_detector = GetTextPaintTimingDetector()) {
    text_paint_timing_detector_->ReportLargestIgnoredText();
  }
  if (auto* image_timing_detector = GetImagePaintTimingDetector()) {
    image_timing_detector->ReportLargestIgnoredImage();
  }
}

ScopedPaintTimingDetectorBlockPaintHook*
    ScopedPaintTimingDetectorBlockPaintHook::top_ = nullptr;

void ScopedPaintTimingDetectorBlockPaintHook::EmplaceIfNeeded(
    const LayoutBoxModelObject& aggregator,
    const PropertyTreeStateOrAlias& property_tree_state) {
  if (IgnorePaintTimingScope::IgnoreDepth() > 1)
    return;
  // |reset_top_| is unset when |aggregator| is anonymous so that each
  // aggregation corresponds to an element. See crbug.com/988593. When set,
  // |top_| becomes |this|, and |top_| is restored to the previous value when
  // the ScopedPaintTimingDetectorBlockPaintHook goes out of scope.
  if (!aggregator.GetNode())
    return;

  reset_top_.emplace(&top_, this);
  TextPaintTimingDetector* detector = aggregator.GetFrameView()
                                          ->GetPaintTimingDetector()
                                          .GetTextPaintTimingDetector();
  // Only set |data_| if we need to walk the object.
  if (detector && detector->ShouldWalkObject(aggregator))
    data_.emplace(aggregator, property_tree_state, detector);
}

ScopedPaintTimingDetectorBlockPaintHook::Data::Data(
    const LayoutBoxModelObject& aggregator,
    const PropertyTreeStateOrAlias& property_tree_state,
    TextPaintTimingDetector* detector)
    : aggregator_(aggregator),
      property_tree_state_(property_tree_state),
      detector_(detector) {}

ScopedPaintTimingDetectorBlockPaintHook::
    ~ScopedPaintTimingDetectorBlockPaintHook() {
  if (!data_ || data_->aggregated_visual_rect_.IsEmpty())
    return;
  // TODO(crbug.com/987804): Checking |ShouldWalkObject| again is necessary
  // because the result can change, but more investigation is needed as to why
  // the change is possible.
  if (!data_->detector_ ||
      !data_->detector_->ShouldWalkObject(data_->aggregator_))
    return;
  data_->detector_->RecordAggregatedText(data_->aggregator_,
                                         data_->aggregated_visual_rect_,
                                         data_->property_tree_state_);
}

void PaintTimingDetector::Trace(Visitor* visitor) const {
  visitor->Trace(text_paint_timing_detector_);
  visitor->Trace(image_paint_timing_detector_);
  visitor->Trace(frame_view_);
  visitor->Trace(largest_contentful_paint_calculator_);
  visitor->Trace(callback_manager_);
}

void PaintTimingCallbackManagerImpl::
    RegisterPaintTimeCallbackForCombinedCallbacks() {
  DCHECK(!frame_callbacks_->empty());
  LocalFrame& frame = frame_view_->GetFrame();
  if (!frame.GetPage())
    return;

  auto combined_callback = CrossThreadBindOnce(
      &PaintTimingCallbackManagerImpl::ReportPaintTime,
      WrapCrossThreadWeakPersistent(this), std::move(frame_callbacks_));
  frame_callbacks_ =
      std::make_unique<PaintTimingCallbackManager::CallbackQueue>();

  // |ReportPaintTime| on |layerTreeView| will queue a presentation-promise, the
  // callback is called when the presentation for current render frame completes
  // or fails to happen.
  frame.GetPage()->GetChromeClient().NotifyPresentationTime(
      frame, std::move(combined_callback));
}

void PaintTimingCallbackManagerImpl::ReportPaintTime(
    std::unique_ptr<PaintTimingCallbackManager::CallbackQueue> frame_callbacks,
    base::TimeTicks paint_time) {
  // Do not report any paint timings for detached frames.
  if (frame_view_->GetFrame().IsDetached())
    return;

  while (!frame_callbacks->empty()) {
    std::move(frame_callbacks->front()).Run(paint_time);
    frame_callbacks->pop();
  }
  frame_view_->GetPaintTimingDetector().UpdateLargestContentfulPaintCandidate();
}

void PaintTimingCallbackManagerImpl::Trace(Visitor* visitor) const {
  visitor->Trace(frame_view_);
  PaintTimingCallbackManager::Trace(visitor);
}

void LCPRectInfo::OutputToTraceValue(TracedValue& value) const {
  value.SetInteger("frame_x", frame_rect_info_.x());
  value.SetInteger("frame_y", frame_rect_info_.y());
  value.SetInteger("frame_width", frame_rect_info_.width());
  value.SetInteger("frame_height", frame_rect_info_.height());
  value.SetInteger("root_x", root_rect_info_.x());
  value.SetInteger("root_y", root_rect_info_.y());
  value.SetInteger("root_width", root_rect_info_.width());
  value.SetInteger("root_height", root_rect_info_.height());
}

}  // namespace blink
