// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/platform_util.h"

#include "base/bind.h"
#include "base/logging.h"
#include "base/notreached.h"
#include "chrome/browser/platform_util_internal.h"
#include "chromeos/crosapi/mojom/file_manager.mojom.h"
#include "chromeos/lacros/lacros_service.h"
#include "content/public/browser/browser_task_traits.h"
#include "content/public/browser/browser_thread.h"

namespace platform_util {
namespace {

void OnOpenResult(const base::FilePath& path,
                  crosapi::mojom::OpenResult result) {
  if (result == crosapi::mojom::OpenResult::kSucceeded)
    return;
  // TODO(https://crbug.com/1144316): Show error messages. This will require
  // refactoring the existing file manager string files, or introducing new
  // lacros strings.
  LOG(ERROR) << "Unable to open " << path.AsUTF8Unsafe() << " " << result;
}

// Requests that ash open an item at |path|.
void OpenItemOnUiThread(const base::FilePath& path, OpenItemType type) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  auto* service = chromeos::LacrosService::Get();
  if (service->GetInterfaceVersion(crosapi::mojom::FileManager::Uuid_) < 1) {
    LOG(ERROR) << "Unsupported ash version.";
    return;
  }
  switch (type) {
    case OPEN_FILE:
      service->GetRemote<crosapi::mojom::FileManager>()->OpenFile(
          path, base::BindOnce(&OnOpenResult, path));
      break;
    case OPEN_FOLDER:
      service->GetRemote<crosapi::mojom::FileManager>()->OpenFolder(
          path, base::BindOnce(&OnOpenResult, path));
      break;
  }
}

}  // namespace

namespace internal {

void PlatformOpenVerifiedItem(const base::FilePath& path, OpenItemType type) {
  // The file manager remote can only be accessed on the UI thread.
  content::GetUIThreadTaskRunner({})->PostTask(
      FROM_HERE, base::BindOnce(&OpenItemOnUiThread, path, type));
}

}  // namespace internal

void ShowItemInFolder(Profile* profile, const base::FilePath& full_path) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  auto* service = chromeos::LacrosService::Get();
  int interface_version =
      service->GetInterfaceVersion(crosapi::mojom::FileManager::Uuid_);
  if (interface_version < 1) {
    DLOG(ERROR) << "Unsupported ash version.";
    return;
  }
  service->GetRemote<crosapi::mojom::FileManager>()->ShowItemInFolder(
      full_path, base::BindOnce(&OnOpenResult, full_path));
}

void OpenExternal(Profile* profile, const GURL& url) {
  // TODO(https://crbug.com/1140585): Add crosapi for opening links with
  // external protocol handlers.
  NOTIMPLEMENTED();
}

}  // namespace platform_util
