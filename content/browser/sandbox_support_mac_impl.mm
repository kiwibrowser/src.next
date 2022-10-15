// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "content/browser/sandbox_support_mac_impl.h"

#include "base/bind.h"
#include "base/task/task_runner_util.h"
#import "content/browser/theme_helper_mac.h"
#include "content/common/mac/font_loader.h"
#include "content/public/browser/browser_task_traits.h"
#include "content/public/browser/browser_thread.h"

namespace content {

SandboxSupportMacImpl::SandboxSupportMacImpl() = default;

SandboxSupportMacImpl::~SandboxSupportMacImpl() = default;

void SandboxSupportMacImpl::BindReceiver(
    mojo::PendingReceiver<mojom::SandboxSupportMac> receiver) {
  receivers_.Add(this, std::move(receiver));
}

void SandboxSupportMacImpl::GetSystemColors(GetSystemColorsCallback callback) {
  auto task_runner = GetUIThreadTaskRunner({});
  base::PostTaskAndReplyWithResult(
      task_runner.get(), FROM_HERE,
      base::BindOnce(&ThemeHelperMac::DuplicateReadOnlyColorMapRegion,
                     base::Unretained(ThemeHelperMac::GetInstance())),
      std::move(callback));
}

void SandboxSupportMacImpl::LoadFont(const std::u16string& font_name,
                                     float font_point_size,
                                     LoadFontCallback callback) {
  FontLoader::LoadFont(font_name, font_point_size, std::move(callback));
}

}  // namespace content
