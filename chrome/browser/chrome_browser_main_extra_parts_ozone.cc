// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chrome_browser_main_extra_parts_ozone.h"

#include "base/bind.h"
#include "base/callback.h"
#include "base/logging.h"
#include "chrome/browser/lifetime/application_lifetime.h"
#include "ui/ozone/public/ozone_platform.h"

ChromeBrowserMainExtraPartsOzone::ChromeBrowserMainExtraPartsOzone() = default;

ChromeBrowserMainExtraPartsOzone::~ChromeBrowserMainExtraPartsOzone() = default;

void ChromeBrowserMainExtraPartsOzone::PreEarlyInitialization() {
    ui::OzonePlatform::PreEarlyInitialization();
}

void ChromeBrowserMainExtraPartsOzone::PostCreateMainMessageLoop() {
  auto shutdown_cb = base::BindOnce([] {
    chrome::SessionEnding();
    LOG(FATAL) << "Browser failed to shutdown.";
  });
  ui::OzonePlatform::GetInstance()->PostCreateMainMessageLoop(
      std::move(shutdown_cb));
}

void ChromeBrowserMainExtraPartsOzone::PostMainMessageLoopRun() {
    ui::OzonePlatform::GetInstance()->PostMainMessageLoopRun();
}
