// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_LAYOUT_LAYOUT_FONT_ACCESSOR_WIN_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_LAYOUT_LAYOUT_FONT_ACCESSOR_WIN_H_

#include "third_party/blink/renderer/platform/wtf/hash_set.h"
#include "third_party/blink/renderer/platform/wtf/text/wtf_string.h"

namespace blink {

class LocalFrame;

struct FontFamilyNames {
  WTF::HashSet<WTF::String> primary_fonts;
  WTF::HashSet<WTF::String> fallback_fonts;
};

void GetFontsUsedByFrame(const LocalFrame& frame, FontFamilyNames& result);

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_CORE_LAYOUT_LAYOUT_FONT_ACCESSOR_WIN_H_
