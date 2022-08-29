// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_DOM_DOM_HIGH_RES_TIME_STAMP_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_DOM_DOM_HIGH_RES_TIME_STAMP_H_

#include "base/time/time.h"

namespace blink {

typedef double DOMHighResTimeStamp;

inline double ConvertDOMHighResTimeStampToSeconds(
    DOMHighResTimeStamp milliseconds) {
  return milliseconds / base::Time::kMillisecondsPerSecond;
}

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_CORE_DOM_DOM_HIGH_RES_TIME_STAMP_H_
