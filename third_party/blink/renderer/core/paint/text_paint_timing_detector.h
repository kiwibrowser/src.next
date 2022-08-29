// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_PAINT_TEXT_PAINT_TIMING_DETECTOR_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_PAINT_TEXT_PAINT_TIMING_DETECTOR_H_

#include <memory>
#include <queue>
#include <set>

#include "third_party/blink/renderer/core/core_export.h"
#include "third_party/blink/renderer/core/dom/node.h"
#include "third_party/blink/renderer/core/paint/paint_timing_detector.h"
#include "third_party/blink/renderer/core/paint/text_element_timing.h"
#include "third_party/blink/renderer/platform/heap/collection_support/heap_hash_map.h"
#include "third_party/blink/renderer/platform/heap/collection_support/heap_hash_set.h"
#include "ui/gfx/geometry/rect_conversions.h"

namespace blink {
class LayoutBoxModelObject;
class LocalFrameView;
class PropertyTreeStateOrAlias;
class TextElementTiming;
class TracedValue;

class TextRecord final : public GarbageCollected<TextRecord> {
 public:
  TextRecord(Node& node,
             uint64_t new_first_size,
             const gfx::RectF& element_timing_rect,
             const gfx::Rect& frame_visual_rect,
             const gfx::RectF& root_visual_rect,
             uint32_t frame_index)
      : node_(&node),
        first_size(new_first_size),
        frame_index_(frame_index),
        element_timing_rect_(element_timing_rect) {
    if (PaintTimingVisualizer::IsTracingEnabled()) {
      lcp_rect_info_ = std::make_unique<LCPRectInfo>(
          frame_visual_rect, gfx::ToRoundedRect(root_visual_rect));
    }
  }
  TextRecord(const TextRecord&) = delete;
  TextRecord& operator=(const TextRecord&) = delete;

  void Trace(Visitor*) const;

  WeakMember<Node> node_;
  uint64_t first_size = 0;
  uint32_t frame_index_ = 0;
  gfx::RectF element_timing_rect_;
  std::unique_ptr<LCPRectInfo> lcp_rect_info_;
  // The time of the first paint after fully loaded.
  base::TimeTicks paint_time = base::TimeTicks();
};

class CORE_EXPORT LargestTextPaintManager final
    : public GarbageCollected<LargestTextPaintManager> {
 public:
  LargestTextPaintManager(LocalFrameView*, PaintTimingDetector*);
  LargestTextPaintManager(const LargestTextPaintManager&) = delete;
  LargestTextPaintManager& operator=(const LargestTextPaintManager&) = delete;

  inline TextRecord* LargestText() {
    DCHECK(!largest_text_ || !largest_text_->paint_time.is_null());
    return largest_text_;
  }
  void MaybeUpdateLargestText(TextRecord* record);

  void ReportCandidateToTrace(const TextRecord&);
  TextRecord* UpdateCandidate();
  void PopulateTraceValue(TracedValue&, const TextRecord& first_text_paint);

  void MaybeUpdateLargestIgnoredText(const LayoutObject&,
                                     const uint64_t&,
                                     const gfx::Rect& frame_visual_rect,
                                     const gfx::RectF& root_visual_rect);
  Member<TextRecord> PopLargestIgnoredText() {
    return std::move(largest_ignored_text_);
  }

  void Trace(Visitor*) const;

 private:
  friend class LargestContentfulPaintCalculatorTest;
  friend class TextPaintTimingDetectorTest;

  // The current largest text.
  Member<TextRecord> largest_text_;

  unsigned count_candidates_ = 0;

  // Text paints are ignored when they (or an ancestor) have opacity 0. This can
  // be a problem later on if the opacity changes to nonzero but this change is
  // composited. We solve this for the special case of documentElement by
  // storing a record for the largest ignored text without nested opacity. We
  // consider this an LCP candidate when the documentElement's opacity changes
  // from zero to nonzero.
  Member<TextRecord> largest_ignored_text_;

  Member<const LocalFrameView> frame_view_;
  Member<PaintTimingDetector> paint_timing_detector_;
};

// TextPaintTimingDetector contains Largest Text Paint and support for Text
// Element Timing.
//
// Largest Text Paint timing measures when the largest text element gets painted
// within the viewport. Specifically, it:
// 1. Tracks all texts' first paints. If the text may be a largest text or is
// required by Element Timing, it records the visual size and paint time.
// 2. It keeps track of information regarding the largest text paint seen so
// far. Because the new version of LCP includes removed content, this record may
// only increase in size over time. See also this doc, which is now somewhat
// outdated: http://bit.ly/fcp_plus_plus.
class CORE_EXPORT TextPaintTimingDetector final
    : public GarbageCollected<TextPaintTimingDetector> {
  friend class TextPaintTimingDetectorTest;

 public:
  explicit TextPaintTimingDetector(LocalFrameView*,
                                   PaintTimingDetector*,
                                   PaintTimingCallbackManager*);
  TextPaintTimingDetector(const TextPaintTimingDetector&) = delete;
  TextPaintTimingDetector& operator=(const TextPaintTimingDetector&) = delete;

  bool ShouldWalkObject(const LayoutBoxModelObject&) const;
  void RecordAggregatedText(const LayoutBoxModelObject& aggregator,
                            const gfx::Rect& aggregated_visual_rect,
                            const PropertyTreeStateOrAlias&);
  void OnPaintFinished();
  void LayoutObjectWillBeDestroyed(const LayoutObject&);
  void StopRecordingLargestTextPaint();
  void ResetCallbackManager(PaintTimingCallbackManager* manager) {
    callback_manager_ = manager;
  }
  inline bool IsRecordingLargestTextPaint() const { return ltp_manager_; }
  inline TextRecord* UpdateCandidate() {
    return ltp_manager_->UpdateCandidate();
  }
  void ReportLargestIgnoredText();
  void ReportPresentationTime(uint32_t frame_index, base::TimeTicks timestamp);
  void Trace(Visitor*) const;

 private:
  friend class LargestContentfulPaintCalculatorTest;

  void RegisterNotifyPresentationTime(
      PaintTimingCallbackManager::LocalThreadCallback callback);

  void AssignPaintTimeToQueuedRecords(uint32_t frame_index,
                                      const base::TimeTicks&);
  void MaybeRecordTextRecord(
      const LayoutObject& object,
      const uint64_t& visual_size,
      const PropertyTreeStateOrAlias& property_tree_state,
      const gfx::Rect& frame_visual_rect,
      const gfx::RectF& root_visual_rect);
  inline void QueueToMeasurePaintTime(const LayoutObject& object,
                                      TextRecord* record) {
    texts_queued_for_paint_time_.insert(&object, record);
    added_entry_in_latest_frame_ = true;
  }

  Member<PaintTimingCallbackManager> callback_manager_;
  Member<const LocalFrameView> frame_view_;
  // Set lazily because we may not have the correct Window when first
  // initializing this class.
  Member<TextElementTiming> text_element_timing_;

  // LayoutObjects for which text has been aggregated.
  HeapHashSet<Member<const LayoutObject>> recorded_set_;

  // Text records queued for paint time. Indexed by LayoutObject to make removal
  // easy.
  HeapHashMap<Member<const LayoutObject>, Member<TextRecord>>
      texts_queued_for_paint_time_;

  Member<LargestTextPaintManager> ltp_manager_;

  // Used to decide which frame a record belongs to, monotonically increasing.
  uint32_t frame_index_ = 1;
  bool added_entry_in_latest_frame_ = false;
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_CORE_PAINT_TEXT_PAINT_TIMING_DETECTOR_H_
