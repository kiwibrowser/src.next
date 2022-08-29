// Copyright 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_DOM_FOCUSGROUP_FLAGS_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_DOM_FOCUSGROUP_FLAGS_H_

#include "third_party/blink/renderer/platform/wtf/text/atomic_string.h"

namespace blink {

class Element;

enum FocusgroupFlags : uint8_t {
  kNone = 0,
  kExtend = 1 << 0,
  kHorizontal = 1 << 1,
  kVertical = 1 << 2,
  kGrid = 1 << 3,
  kWrapHorizontally = 1 << 4,
  kWrapVertically = 1 << 5,
  kRowFlow = 1 << 6,
  kColFlow = 1 << 7,
};

inline constexpr FocusgroupFlags operator&(FocusgroupFlags a,
                                           FocusgroupFlags b) {
  return static_cast<FocusgroupFlags>(static_cast<uint8_t>(a) &
                                      static_cast<uint8_t>(b));
}

inline constexpr FocusgroupFlags operator|(FocusgroupFlags a,
                                           FocusgroupFlags b) {
  return static_cast<FocusgroupFlags>(static_cast<uint8_t>(a) |
                                      static_cast<uint8_t>(b));
}

inline FocusgroupFlags& operator|=(FocusgroupFlags& a, FocusgroupFlags b) {
  return a = a | b;
}

inline FocusgroupFlags& operator&=(FocusgroupFlags& a, FocusgroupFlags b) {
  return a = a & b;
}

inline constexpr FocusgroupFlags operator~(FocusgroupFlags flags) {
  return static_cast<FocusgroupFlags>(~static_cast<uint8_t>(flags));
}

namespace focusgroup {
FocusgroupFlags FindNearestFocusgroupAncestorFlags(const Element* element);
// Implemented based on this explainer:
// https://github.com/MicrosoftEdge/MSEdgeExplainers/blob/main/Focusgroup/explainer.md
FocusgroupFlags ParseFocusgroup(const Element* element,
                                const AtomicString& input);
}  // namespace focusgroup

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_CORE_DOM_FOCUSGROUP_FLAGS_H_