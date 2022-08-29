// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_PAINT_PAINT_TIMING_VISUALIZER_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_PAINT_PAINT_TIMING_VISUALIZER_H_

#include "third_party/blink/renderer/core/core_export.h"
#include "third_party/blink/renderer/platform/instrumentation/tracing/traced_value.h"

namespace gfx {
class Rect;
class RectF;
}  // namespace gfx

namespace blink {

class LayoutObject;
class LocalFrameView;
class MediaTiming;

// While Largest Contentful Paint only concerns about the largest contentful
// rect, the smaller rects used in its computation are helpful for debugging
// purpose. This helper class generates debugging traces that contains these
// intermediate rects. These debugging events, as well as their intermediate
// rects, can be visualized by third-party visualization tools.
class CORE_EXPORT PaintTimingVisualizer {
 public:
  static bool IsTracingEnabled();

  void DumpTextDebuggingRect(const LayoutObject&, const gfx::RectF&);
  void DumpImageDebuggingRect(const LayoutObject&,
                              const gfx::RectF&,
                              const MediaTiming&);
  void RecordMainFrameViewport(LocalFrameView& frame_view);
  inline void OnViewportChanged() { need_recording_viewport = true; }

 private:
  void RecordObject(const LayoutObject&, std::unique_ptr<TracedValue>&);
  void RecordRects(const gfx::Rect& rect, std::unique_ptr<TracedValue>&);
  void RecordMainFrameViewport(const gfx::Rect&);
  void DumpTrace(std::unique_ptr<TracedValue>);

  bool need_recording_viewport = true;
};
}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_CORE_PAINT_PAINT_TIMING_VISUALIZER_H_
