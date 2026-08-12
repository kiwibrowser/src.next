// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef EXTENSIONS_COMMON_MIME_HANDLER_AVAILABILITY_H_
#define EXTENSIONS_COMMON_MIME_HANDLER_AVAILABILITY_H_

#include "extensions/common/features/feature.h"

namespace extensions::mime_handler_availability {

Feature::FeatureDelegatedAvailabilityCheckMap CreateAvailabilityCheckMap();

}  // namespace extensions::mime_handler_availability

#endif  // EXTENSIONS_COMMON_MIME_HANDLER_AVAILABILITY_H_
