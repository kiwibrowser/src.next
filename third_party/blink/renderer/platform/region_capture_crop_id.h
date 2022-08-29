// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_PLATFORM_REGION_CAPTURE_CROP_ID_H_
#define THIRD_PARTY_BLINK_RENDERER_PLATFORM_REGION_CAPTURE_CROP_ID_H_

#include "base/guid.h"
#include "base/token.h"
#include "base/types/strong_alias.h"
#include "third_party/blink/renderer/platform/platform_export.h"

namespace blink {

using RegionCaptureCropId =
    base::StrongAlias<class RegionCaptureCropIdTag, base::Token>;

// Convert between base::GUID and base::Token. Both encode identity using
// 128 bits of information, but GUID does so in a string-based way that is
// inefficient to move around.
PLATFORM_EXPORT base::Token GUIDToToken(const base::GUID& guid);
PLATFORM_EXPORT base::GUID TokenToGUID(const base::Token& token);

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_PLATFORM_REGION_CAPTURE_CROP_ID_H_
