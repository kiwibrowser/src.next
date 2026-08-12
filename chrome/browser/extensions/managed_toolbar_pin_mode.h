// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_MANAGED_TOOLBAR_PIN_MODE_H_
#define CHROME_BROWSER_EXTENSIONS_MANAGED_TOOLBAR_PIN_MODE_H_

#include "extensions/buildflags/buildflags.h"

static_assert(BUILDFLAG(ENABLE_EXTENSIONS_CORE));

namespace extensions {

// Policy-based behavior for "Pin extension to toolbar".
// If no policy is set (kNotSet), the default pinning behavior
// on installation is controlled by the `features::kExtensionsPinnedByDefault`
// feature flag: if enabled then kDefaultPinned, if disabled then
// kDefaultUnpinned.
//
// * kDefaultUnpinned: Extension starts unpinned, but the user can still pin
//                     it afterwards.
// * kDefaultPinned: Extension starts pinned, but the user can still unpin it
//                   afterwards.
// * kForcePinned: Extension starts pinned to the toolbar, and the user
//                 cannot unpin it.
enum class ManagedToolbarPinMode {
  kNotSet = 0,       // No admin policy set for pinning.
  kDefaultUnpinned,  // Starts unpinned, user can pin.
  kDefaultPinned,    // Starts pinned, user can unpin.
  kForcePinned,      // Starts pinned, user cannot unpin.
};

}  // namespace extensions

#endif  // CHROME_BROWSER_EXTENSIONS_MANAGED_TOOLBAR_PIN_MODE_H_
