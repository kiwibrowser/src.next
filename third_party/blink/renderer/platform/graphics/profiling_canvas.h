/*
 * Copyright (C) 2013 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef THIRD_PARTY_BLINK_RENDERER_PLATFORM_GRAPHICS_PROFILING_CANVAS_H_
#define THIRD_PARTY_BLINK_RENDERER_PLATFORM_GRAPHICS_PROFILING_CANVAS_H_

#include "third_party/blink/renderer/platform/graphics/intercepting_canvas.h"

#include "base/time/time.h"
#include "third_party/blink/renderer/platform/wtf/vector.h"

namespace blink {

class ProfilingCanvas;

template <>
class CanvasInterceptor<ProfilingCanvas>
    : protected InterceptingCanvasBase::CanvasInterceptorBase<ProfilingCanvas> {
 public:
  CanvasInterceptor(InterceptingCanvasBase*);
  ~CanvasInterceptor();

 private:
  base::TimeTicks start_time_;
};

class ProfilingCanvas : public InterceptingCanvas<ProfilingCanvas> {
 public:
  explicit ProfilingCanvas(SkBitmap);
  void SetTimings(Vector<base::TimeDelta>*);

 private:
  friend class CanvasInterceptor<ProfilingCanvas>;

  Vector<base::TimeDelta>* timings_;
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_PLATFORM_GRAPHICS_PROFILING_CANVAS_H_
