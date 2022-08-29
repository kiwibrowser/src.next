/*
 * Copyright (C) 2007 Apple Inc.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 * 3.  Neither the name of Apple Computer, Inc. ("Apple") nor the names of
 *     its contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE AND ITS CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL APPLE OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "third_party/blink/renderer/core/frame/screen.h"

#include "base/numerics/safe_conversions.h"
#include "third_party/blink/public/mojom/permissions_policy/permissions_policy_feature.mojom-blink.h"
#include "third_party/blink/renderer/core/event_target_names.h"
#include "third_party/blink/renderer/core/frame/local_dom_window.h"
#include "third_party/blink/renderer/core/frame/local_frame.h"
#include "third_party/blink/renderer/core/frame/settings.h"
#include "third_party/blink/renderer/core/page/chrome_client.h"
#include "ui/display/screen_info.h"
#include "ui/display/screen_infos.h"

namespace blink {

namespace {

}  // namespace

Screen::Screen(LocalDOMWindow* window, int64_t display_id)
    : ExecutionContextClient(window), display_id_(display_id) {}

// static
bool Screen::AreWebExposedScreenPropertiesEqual(
    const display::ScreenInfo& prev,
    const display::ScreenInfo& current) {
  // height() / width() use rect / device_scale_factor
  if (prev.rect.size() != current.rect.size())
    return false;

  // Note: comparing device_scale_factor is a bit of a lie as Screen only uses
  // this with the PhysicalPixelsQuirk (see width() / height() below).  However,
  // this value likely changes rarely and should not throw many false positives.
  if (prev.device_scale_factor != current.device_scale_factor)
    return false;

  // availLeft() / availTop() / availHeight() / availWidth() use available_rect
  if (prev.available_rect != current.available_rect)
    return false;

  // colorDepth() / pixelDepth() use depth
  if (prev.depth != current.depth)
    return false;

  // isExtended()
  if (prev.is_extended != current.is_extended)
    return false;

  return true;
}

int Screen::height() const {
  if (!DomWindow())
    return 0;
  LocalFrame* frame = DomWindow()->GetFrame();
  const display::ScreenInfo& screen_info = GetScreenInfo();
  if (frame->GetSettings()->GetReportScreenSizeInPhysicalPixelsQuirk()) {
    return base::ClampRound(screen_info.rect.height() *
                            screen_info.device_scale_factor);
  }
  return screen_info.rect.height();
}

int Screen::width() const {
  if (!DomWindow())
    return 0;
  LocalFrame* frame = DomWindow()->GetFrame();
  const display::ScreenInfo& screen_info = GetScreenInfo();
  if (frame->GetSettings()->GetReportScreenSizeInPhysicalPixelsQuirk()) {
    return base::ClampRound(screen_info.rect.width() *
                            screen_info.device_scale_factor);
  }
  return screen_info.rect.width();
}

unsigned Screen::colorDepth() const {
  if (!DomWindow())
    return 0;
  return base::saturated_cast<unsigned>(GetScreenInfo().depth);
}

unsigned Screen::pixelDepth() const {
  return colorDepth();
}

int Screen::availLeft() const {
  if (!DomWindow())
    return 0;
  LocalFrame* frame = DomWindow()->GetFrame();
  const display::ScreenInfo& screen_info = GetScreenInfo();
  if (frame->GetSettings()->GetReportScreenSizeInPhysicalPixelsQuirk()) {
    return base::ClampRound(screen_info.available_rect.x() *
                            screen_info.device_scale_factor);
  }
  return screen_info.available_rect.x();
}

int Screen::availTop() const {
  if (!DomWindow())
    return 0;
  LocalFrame* frame = DomWindow()->GetFrame();
  const display::ScreenInfo& screen_info = GetScreenInfo();
  if (frame->GetSettings()->GetReportScreenSizeInPhysicalPixelsQuirk()) {
    return base::ClampRound(screen_info.available_rect.y() *
                            screen_info.device_scale_factor);
  }
  return screen_info.available_rect.y();
}

int Screen::availHeight() const {
  if (!DomWindow())
    return 0;
  LocalFrame* frame = DomWindow()->GetFrame();
  const display::ScreenInfo& screen_info = GetScreenInfo();
  if (frame->GetSettings()->GetReportScreenSizeInPhysicalPixelsQuirk()) {
    return base::ClampRound(screen_info.available_rect.height() *
                            screen_info.device_scale_factor);
  }
  return screen_info.available_rect.height();
}

int Screen::availWidth() const {
  if (!DomWindow())
    return 0;
  LocalFrame* frame = DomWindow()->GetFrame();
  const display::ScreenInfo& screen_info = GetScreenInfo();
  if (frame->GetSettings()->GetReportScreenSizeInPhysicalPixelsQuirk()) {
    return base::ClampRound(screen_info.available_rect.width() *
                            screen_info.device_scale_factor);
  }
  return screen_info.available_rect.width();
}

void Screen::Trace(Visitor* visitor) const {
  EventTargetWithInlineData::Trace(visitor);
  ExecutionContextClient::Trace(visitor);
  Supplementable<Screen>::Trace(visitor);
}

const WTF::AtomicString& Screen::InterfaceName() const {
  return event_target_names::kScreen;
}

ExecutionContext* Screen::GetExecutionContext() const {
  return ExecutionContextClient::GetExecutionContext();
}

bool Screen::isExtended() const {
  if (!DomWindow())
    return false;
  auto* context = GetExecutionContext();
  if (!context->IsFeatureEnabled(
          mojom::blink::PermissionsPolicyFeature::kWindowPlacement)) {
    return false;
  }

  return GetScreenInfo().is_extended;
}

const display::ScreenInfo& Screen::GetScreenInfo() const {
  DCHECK(DomWindow());
  LocalFrame* frame = DomWindow()->GetFrame();

  const auto& screen_infos = frame->GetChromeClient().GetScreenInfos(*frame);
  for (const auto& screen : screen_infos.screen_infos) {
    if (screen.display_id == display_id_)
      return screen;
  }
  DEFINE_STATIC_LOCAL(display::ScreenInfo, kEmptyScreenInfo, ());
  return kEmptyScreenInfo;
}

}  // namespace blink
