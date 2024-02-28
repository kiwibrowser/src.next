// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "base/check.h"
#include "base/containers/contains.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/logging.h"
#include "base/memory/weak_ptr.h"
#include "base/supports_user_data.h"
#include "base/task/task_traits.h"
#include "base/task/thread_pool.h"
#include "chrome/browser/download/bubble/download_bubble_prefs.h"
#include "chrome/browser/download/bubble/download_bubble_ui_controller.h"
#include "chrome/browser/download/download_commands.h"
#include "chrome/browser/download/download_item_model.h"
#include "chrome/browser/download/download_item_warning_data.h"
#include "chrome/browser/download/download_status_updater.h"
#include "chrome/browser/download/download_ui_model.h"
#include "chrome/browser/download/offline_item_utils.h"
#include "chrome/browser/image_decoder/image_decoder.h"
#include "chrome/browser/profiles/keep_alive/scoped_profile_keep_alive.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_window.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/chrome_pages.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chromeos/crosapi/mojom/download_controller.mojom.h"
#include "chromeos/crosapi/mojom/download_status_updater.mojom.h"
#include "chromeos/lacros/lacros_service.h"
#include "components/download/content/public/all_download_item_notifier.h"
#include "components/download/public/common/download_item_utils.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/download_item_utils.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/display/types/display_constants.h"
#include "ui/gfx/image/image_skia.h"

namespace {

// Constants -------------------------------------------------------------------

// The key referring to an image decoder task.
constexpr char kImageDecoderTaskKey[] = "kImageDecoderTask";

// Images larger than this threshold should not be decoded.
constexpr size_t kImageDecoderTaskMaxFileSize = 10 * 1024 * 1024;  // 10 MB

// Helpers ---------------------------------------------------------------------

crosapi::mojom::DownloadStatusUpdater* GetRemote(
    std::optional<uint32_t> min_version = std::nullopt) {
  using DownloadStatusUpdater = crosapi::mojom::DownloadStatusUpdater;
  auto* service = chromeos::LacrosService::Get();
  if (!service || !service->IsAvailable<DownloadStatusUpdater>()) {
    return nullptr;
  }
  // NOTE: Use `remote.version()` rather than `service->GetInterfaceVersion()`
  // as the latter does not respect versions of remotes injected for testing.
  auto& remote = service->GetRemote<DownloadStatusUpdater>();
  return remote.version() >= min_version.value_or(remote.version())
             ? remote.get()
             : nullptr;
}

bool IsCommandEnabled(DownloadItemModel& model,
                      DownloadCommands::Command command) {
  // To support other commands, we may need to update checks below to also
  // inspect `BubbleUIInfo` subpage buttons.
  CHECK(command == DownloadCommands::CANCEL ||
        command == DownloadCommands::PAUSE ||
        command == DownloadCommands::RESUME);

  const DownloadUIModel::BubbleUIInfo info = model.GetBubbleUIInfo();

  // A command is enabled if `BubbleUIInfo` contains a quick action for it. This
  // is preferred over non-`BubbleUIInfo`-based determination of command
  // enablement as it takes more signals into account, e.g. if the download has
  // been marked dangerous.
  return base::Contains(info.quick_actions, command,
                        &DownloadUIModel::BubbleUIInfo::QuickAction::command);
}

// Reads a specified image into binary data. Returns an empty string if
// unsuccessful. NOTE:
// 1. This function should be called only when the file size is not greater than
//    `kImageDecoderTaskMaxFileSize`.
// 2. This function is blocking so it should not be called from the UI thread.
std::string ReadImage(const base::FilePath& file_path) {
  CHECK(!content::BrowserThread::CurrentlyOn(content::BrowserThread::UI));

  std::string data;
  if (!base::ReadFileToString(file_path, &data)) {
    return std::string();
  }

  if (data.size() > kImageDecoderTaskMaxFileSize) {
    data.clear();
    LOG(ERROR) << "Attempted to read a too large image file.";
  }

  return data;
}

// ImageDecoderTask ------------------------------------------------------------

// Represents an async task to decode a download image. Has two stages:
// 1. Load the image's binary data.
// 2. Decode the binary data into a `gfx::ImageSkia`.
class ImageDecoderTask : public base::SupportsUserData::Data,
                         public ImageDecoder::ImageRequest {
 public:
  void Run(const base::FilePath& image_path,
           base::OnceClosure task_success_callback) {
    CHECK(!task_success_callback_);
    CHECK(task_success_callback);
    task_success_callback_ = std::move(task_success_callback);

    base::ThreadPool::PostTaskAndReplyWithResult(
        FROM_HERE,
        /*traits=*/{base::MayBlock(), base::TaskPriority::BEST_EFFORT},
        base::BindOnce(&ReadImage, image_path),
        base::BindOnce(&ImageDecoderTask::OnImageLoaded,
                       weak_ptr_factory_.GetWeakPtr()));
  }

  const gfx::ImageSkia& image() const { return image_; }

 private:
  // ImageDecoder::ImageRequest:
  void OnImageDecoded(const SkBitmap& decoded_image) override {
    DCHECK_CURRENTLY_ON(content::BrowserThread::UI);

    if (!decoded_image.drawsNothing()) {
      image_ = gfx::ImageSkia::CreateFrom1xBitmap(decoded_image);
      std::move(task_success_callback_).Run();
    }
  }

  void OnImageLoaded(std::string image_data) {
    if (!image_data.empty()) {
      ImageDecoder::Start(/*image_request=*/this, std::move(image_data));
    }
  }

  // Called when the task successfully completes.
  base::OnceClosure task_success_callback_;

  // Caches the decoding result. Null if decoding is in progress or has failed.
  gfx::ImageSkia image_;

  base::WeakPtrFactory<ImageDecoderTask> weak_ptr_factory_{this};
};

}  // namespace

// DownloadStatusUpdater::Delegate ---------------------------------------------

// The delegate of the `DownloadStatusUpdater` in Lacros Chrome which serves as
// the client for the `DownloadStatusUpdater` in Ash Chrome.
class DownloadStatusUpdater::Delegate
    : public crosapi::mojom::DownloadStatusUpdaterClient {
 public:
  using GetDownloadItemCallback =
      base::RepeatingCallback<download::DownloadItem*(const std::string&)>;

  explicit Delegate(GetDownloadItemCallback get_download_item_callback)
      : get_download_item_callback_(std::move(get_download_item_callback)) {
    CHECK(!get_download_item_callback_.is_null());
    using crosapi::mojom::DownloadStatusUpdater;
    if (auto* remote =
            GetRemote(DownloadStatusUpdater::kBindClientMinVersion)) {
      remote->BindClient(receiver_.BindNewPipeAndPassRemoteWithVersion());
    }
  }

  Delegate(const Delegate&) = delete;
  Delegate& operator=(const Delegate&) = delete;
  ~Delegate() override = default;

  // Updates the remote download if it exists. Returns true on success.
  bool MaybeUpdate(download::DownloadItem* download) {
    auto* const remote = GetRemote();
    if (!remote) {
      return false;
    }

    DownloadItemModel model(
        download, std::make_unique<DownloadUIModel::BubbleStatusTextBuilder>());
    auto status = crosapi::mojom::DownloadStatus::New();
    status->cancellable = IsCommandEnabled(model, DownloadCommands::CANCEL);
    status->full_path = download->GetFullPath();
    status->guid = download->GetGuid();
    status->pausable = IsCommandEnabled(model, DownloadCommands::PAUSE);
    status->received_bytes = download->GetReceivedBytes();
    status->resumable = IsCommandEnabled(model, DownloadCommands::RESUME);
    status->state = download::download_item_utils::ConvertToMojoDownloadState(
        download->GetState());
    status->status_text = model.GetStatusText();
    status->target_file_path = download->GetTargetFilePath();
    status->total_bytes = download->GetTotalBytes();

    // If `task` exists and completes, copy the image generated by `task` to
    // `status` and delete `task`; otherwise, posts an image decoder task if
    // conditions satisfied. NOTE: Download updates after image decoding are
    // assumed to be rare.
    const auto* task = static_cast<const ImageDecoderTask*>(
        download->GetUserData(kImageDecoderTaskKey));
    if (task && !task->image().isNull()) {
      status->image = task->image();
      download->RemoveUserData(kImageDecoderTaskKey);
      task = nullptr;
    } else if (!task) {
      MaybePostImageDecoderTask(download);
    }

    remote->Update(std::move(status));
    return true;
  }

 private:
  download::DownloadItem* GetDownloadItem(const std::string& guid) {
    return get_download_item_callback_.Run(guid);
  }

  // crosapi::mojom::DownloadStatusUpdaterClient:
  void Cancel(const std::string& guid, CancelCallback callback) override {
    bool handled = false;
    if (download::DownloadItem* item = GetDownloadItem(guid); item) {
      handled = true;
      item->Cancel(/*user_cancel=*/true);
    }
    std::move(callback).Run(handled);
  }

  void Pause(const std::string& guid, PauseCallback callback) override {
    bool handled = false;
    if (download::DownloadItem* item = GetDownloadItem(guid); item) {
      handled = true;
      if (!item->IsPaused()) {
        item->Pause();
      }
    }
    std::move(callback).Run(handled);
  }

  void Resume(const std::string& guid, ResumeCallback callback) override {
    bool handled = false;
    if (download::DownloadItem* item = GetDownloadItem(guid); item) {
      handled = true;
      if (item->CanResume()) {
        item->Resume(/*user_resume=*/true);
      }
    }
    std::move(callback).Run(handled);
  }

  void ShowInBrowser(const std::string& guid,
                     ShowInBrowserCallback callback) override {
    // Look up the profile from the download item and find a relevant browser to
    // display the download bubble in.
    Profile* profile = nullptr;
    Browser* browser = nullptr;
    if (download::DownloadItem* item = GetDownloadItem(guid); item) {
      content::BrowserContext* browser_context =
          content::DownloadItemUtils::GetBrowserContext(item);
      profile = Profile::FromBrowserContext(browser_context);
      if (profile) {
        // TODO(chlily): This doesn't work for web app initiated downloads.
        browser = chrome::FindTabbedBrowser(profile,
                                            /*match_original_profiles=*/false,
                                            display::kInvalidDisplayId,
                                            /*ignore_closing_browsers=*/true);
      }
    }

    if (browser) {
      // If we found an appropriate browser, show the download bubble in it.
      OnBrowserLocated(guid, std::move(callback), browser);
      return;
    } else if (profile) {
      // Otherwise, attempt to open a new browser window and do the same.
      // This can happen if the last browser window shuts down while there are
      // downloads in progress, and the profile is kept alive. (Some downloads
      // do not block browser shutdown.)
      profiles::OpenBrowserWindowForProfile(
          base::BindOnce(&DownloadStatusUpdater::Delegate::OnBrowserLocated,
                         weak_factory_.GetWeakPtr(), guid, std::move(callback)),
          /*always_create=*/false,
          /*is_new_profile=*/false, /*unblock_extensions=*/true, profile);
      return;
    }
    std::move(callback).Run(/*handled=*/false);
  }

  // Posts an asynchronous task to decode the download image and then updates
  // the download iff:
  // 1. The download file exists and its size is not greater than the threshold.
  // 2. The underlying download is completed.
  // 3. The underlying download is an image download.
  // NOTE: This function should be called only when `download` does not have an
  // associated image decoder task.
  void MaybePostImageDecoderTask(download::DownloadItem* download) {
    CHECK(!download->GetUserData(kImageDecoderTaskKey));

    const base::FilePath& target_file_path = download->GetTargetFilePath();
    if (const std::optional<int64_t>& received_bytes =
            download->GetReceivedBytes();
        target_file_path.empty() || !received_bytes ||
        received_bytes > kImageDecoderTaskMaxFileSize ||
        download->GetState() != download::DownloadItem::COMPLETE ||
        !DownloadItemModel(download).HasSupportedImageMimeType()) {
      return;
    }

    // `download` outlives `image_decoder_task`. Therefore, it is safe to pass
    // `download` to the callback.
    auto image_decoder_task = std::make_unique<ImageDecoderTask>();
    image_decoder_task->Run(
        target_file_path,
        base::BindOnce(
            base::IgnoreResult(&DownloadStatusUpdater::Delegate::MaybeUpdate),
            weak_factory_.GetWeakPtr(), download));
    download->SetUserData(kImageDecoderTaskKey, std::move(image_decoder_task));
  }

  void OnBrowserLocated(const std::string& guid,
                        ShowInBrowserCallback callback,
                        Browser* browser) {
    if (!browser || !browser->window()) {
      std::move(callback).Run(/*handled=*/false);
      return;
    }

    // Activate the browser so that the bubble or chrome://downloads page can be
    // visible.
    if (browser->window()->IsMinimized()) {
      browser->window()->Restore();
    }
    browser->window()->Activate();

    bool showed_bubble = false;
    DownloadBubbleUIController* bubble_controller =
        browser->window()->GetDownloadBubbleUIController();
    // Look up the guid again because the item may have been removed in the
    // meantime.
    if (download::DownloadItem* item = GetDownloadItem(guid);
        item && bubble_controller) {
      offline_items_collection::ContentId content_id =
          OfflineItemUtils::GetContentIdForDownload(item);
      showed_bubble = bubble_controller->OpenMostSpecificDialog(content_id);

      if (item->IsDangerous() && !item->IsDone() && showed_bubble) {
        DownloadItemWarningData::AddWarningActionEvent(
            item,
            DownloadItemWarningData::WarningSurface::DOWNLOAD_NOTIFICATION,
            DownloadItemWarningData::WarningAction::OPEN_SUBPAGE);
      }
    }
    if (!showed_bubble) {
      // Fall back to showing chrome://downloads.
      chrome::ShowDownloads(browser);
    }
    std::move(callback).Run(/*handled=*/true);
  }

  // The receiver bound to `this` for use by crosapi.
  mojo::Receiver<crosapi::mojom::DownloadStatusUpdaterClient> receiver_{this};

  // Callback allowing the lookup of DownloadItem*s from guids.
  GetDownloadItemCallback get_download_item_callback_;

  base::WeakPtrFactory<DownloadStatusUpdater::Delegate> weak_factory_{this};
};

// DownloadStatusUpdater -------------------------------------------------------

DownloadStatusUpdater::DownloadStatusUpdater()
    : delegate_(std::make_unique<Delegate>(
          base::BindRepeating(&DownloadStatusUpdater::GetDownloadItemFromGuid,
                              base::Unretained(this)))) {}

DownloadStatusUpdater::~DownloadStatusUpdater() = default;

void DownloadStatusUpdater::UpdateAppIconDownloadProgress(
    download::DownloadItem* download) {
  if (delegate_->MaybeUpdate(download) && download->IsDangerous()) {
    DownloadItemWarningData::AddWarningActionEvent(
        download,
        DownloadItemWarningData::WarningSurface::DOWNLOAD_NOTIFICATION,
        DownloadItemWarningData::WarningAction::SHOWN);
  }
}

download::DownloadItem* DownloadStatusUpdater::GetDownloadItemFromGuid(
    const std::string& guid) {
  for (const auto& notifier : notifiers_) {
    content::DownloadManager* manager = notifier->GetManager();
    if (!manager) {
      continue;
    }
    download::DownloadItem* item = manager->GetDownloadByGuid(guid);
    if (item) {
      return item;
    }
  }
  return nullptr;
}
