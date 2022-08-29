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

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_FRAME_SETTINGS_DELEGATE_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_FRAME_SETTINGS_DELEGATE_H_

#include <memory>
#include "third_party/blink/renderer/core/core_export.h"
#include "third_party/blink/renderer/platform/wtf/allocator/allocator.h"

namespace blink {

class Settings;

class CORE_EXPORT SettingsDelegate {
  DISALLOW_NEW();

 public:
  explicit SettingsDelegate(std::unique_ptr<Settings>);
  virtual ~SettingsDelegate();

  Settings* GetSettings() const { return settings_.get(); }

  // We currently use an enum instead of individual invalidation
  // functions to make generating Settings.in slightly easier.
  enum class ChangeType {
    kStyle,
    kViewportDescription,
    kViewportRule,
    kViewportPaintProperties,
    kDNSPrefetching,
    kImageLoading,
    kTextAutosizing,
    kFontFamily,
    kAcceleratedCompositing,
    kMediaQuery,
    kAccessibilityState,
    kTextTrackKindUserPreference,
    kDOMWorlds,
    kMediaControls,
    kPlugins,
    kHighlightAds,
    kPaint,
    kScrollbarLayout,
    kColorScheme,
    kSpatialNavigation,
    kUniversalAccess,
    kVisionDeficiency,
  };

  virtual void SettingsChanged(ChangeType) = 0;

 protected:
  std::unique_ptr<Settings> const settings_;
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_CORE_FRAME_SETTINGS_DELEGATE_H_
