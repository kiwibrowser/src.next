// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/platform_util.h"

#include "base/notreached.h"
namespace platform_util {

void ShowItemInFolder(Profile* profile, const base::FilePath& full_path) {
  // TODO(crbug.com/1235293)
  NOTIMPLEMENTED_LOG_ONCE();
}

namespace internal {

void PlatformOpenVerifiedItem(const base::FilePath& path, OpenItemType type) {
  // TODO(crbug.com/1235293)
  NOTIMPLEMENTED_LOG_ONCE();
}

}  // namespace internal

void OpenExternal(Profile* profile, const GURL& url) {
  // TODO(crbug.com/1235293)
  NOTIMPLEMENTED_LOG_ONCE();
}

}  // namespace platform_util
