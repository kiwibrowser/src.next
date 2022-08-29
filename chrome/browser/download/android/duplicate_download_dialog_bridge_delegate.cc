// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/download/android/duplicate_download_dialog_bridge_delegate.h"

#include <string>

#include "base/android/path_utils.h"
#include "base/files/file_path.h"
#include "base/memory/singleton.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/android/android_theme_resources.h"
#include "chrome/browser/download/android/download_dialog_utils.h"
#include "chrome/browser/download/android/duplicate_download_dialog_bridge.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/grit/generated_resources.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/download_item_utils.h"
#include "content/public/browser/web_contents.h"
#include "ui/android/window_android.h"
#include "ui/base/l10n/l10n_util.h"

using base::android::JavaParamRef;

// static
DuplicateDownloadDialogBridgeDelegate*
DuplicateDownloadDialogBridgeDelegate::GetInstance() {
  return base::Singleton<DuplicateDownloadDialogBridgeDelegate>::get();
}

DuplicateDownloadDialogBridgeDelegate::DuplicateDownloadDialogBridgeDelegate() =
    default;

DuplicateDownloadDialogBridgeDelegate::
    ~DuplicateDownloadDialogBridgeDelegate() {
  for (auto* download_item : download_items_)
    download_item->RemoveObserver(this);
}

void DuplicateDownloadDialogBridgeDelegate::CreateDialog(
    download::DownloadItem* download_item,
    const base::FilePath& file_path,
    content::WebContents* web_contents,
    DownloadTargetDeterminerDelegate::ConfirmationCallback
        file_selected_callback) {
  DCHECK(web_contents);
  // Don't shown duplicate dialog again if it is already showing.
  if (std::find(download_items_.begin(), download_items_.end(),
                download_item) != download_items_.end()) {
    return;
  }
  download_item->AddObserver(this);
  download_items_.push_back(download_item);

  DuplicateDownloadDialogBridge::GetInstance()->Show(
      file_path.value(), std::string() /*page_url*/,
      download_item->GetTotalBytes(), false /*duplicate_request_exists*/,
      web_contents,
      base::BindOnce(&DuplicateDownloadDialogBridgeDelegate::OnConfirmed,
                     weak_factory_.GetWeakPtr(), download_item->GetGuid(),
                     file_path, std::move(file_selected_callback)));
}

void DuplicateDownloadDialogBridgeDelegate::OnConfirmed(
    const std::string& download_guid,
    const base::FilePath& file_path,
    DownloadTargetDeterminerDelegate::ConfirmationCallback callback,
    bool accepted) {
  download::DownloadItem* download = DownloadDialogUtils::FindAndRemoveDownload(
      &download_items_, download_guid);
  if (!download)
    return;

  if (accepted) {
    base::FilePath download_dir;
    if (!base::android::GetDownloadsDirectory(&download_dir))
      return;

    download::DownloadPathReservationTracker::GetReservedPath(
        download, file_path, download_dir,
        base::FilePath(), /* fallback_directory */
        true, download::DownloadPathReservationTracker::UNIQUIFY,
        base::BindOnce(&DownloadDialogUtils::CreateNewFileDone,
                       std::move(callback)));
  } else {
    std::move(callback).Run(DownloadConfirmationResult::CANCELED,
                            base::FilePath(),
                            absl::nullopt /*download_schedule*/);
  }
}

void DuplicateDownloadDialogBridgeDelegate::OnDownloadDestroyed(
    download::DownloadItem* download_item) {
  auto iter =
      std::find(download_items_.begin(), download_items_.end(), download_item);
  if (iter != download_items_.end())
    download_items_.erase(iter);
}
