// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/download/android/download_manager_service.h"

#include <memory>

#include "base/android/callback_android.h"
#include "base/android/jni_string.h"
#include "base/android/path_utils.h"
#include "base/bind.h"
#include "base/containers/contains.h"
#include "base/location.h"
#include "base/metrics/field_trial_params.h"
#include "base/strings/string_number_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "base/threading/thread_task_runner_handle.h"
#include "base/time/default_clock.h"
#include "base/time/time.h"
#include "chrome/android/chrome_jni_headers/DownloadItem_jni.h"
#include "chrome/android/chrome_jni_headers/DownloadManagerService_jni.h"
#include "chrome/browser/android/profile_key_startup_accessor.h"
#include "chrome/browser/download/android/download_controller.h"
#include "chrome/browser/download/android/download_startup_utils.h"
#include "chrome/browser/download/android/download_utils.h"
#include "chrome/browser/download/android/jni_headers/DownloadInfo_jni.h"
#include "chrome/browser/download/android/service/download_task_scheduler.h"
#include "chrome/browser/download/offline_item_utils.h"
#include "chrome/browser/download/simple_download_manager_coordinator_factory.h"
#include "chrome/browser/flags/android/cached_feature_flags.h"
#include "chrome/browser/flags/android/chrome_feature_list.h"
#include "chrome/browser/profiles/profile_android.h"
#include "chrome/browser/profiles/profile_key.h"
#include "chrome/browser/profiles/profile_key_android.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/common/chrome_constants.h"
#include "components/download/network/android/network_status_listener_android.h"
#include "components/download/public/common/auto_resumption_handler.h"
#include "components/download/public/common/download_features.h"
#include "components/download/public/common/download_item.h"
#include "components/download/public/common/download_item_impl.h"
#include "components/download/public/common/download_stats.h"
#include "components/download/public/common/simple_download_manager_coordinator.h"
#include "components/download/public/common/url_download_handler_factory.h"
#include "components/download/public/task/task_manager_impl.h"
#include "components/offline_items_collection/core/android/offline_item_bridge.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/download_item_utils.h"
#include "content/public/browser/download_request_utils.h"
#include "net/url_request/referrer_policy.h"
#include "third_party/abseil-cpp/absl/types/optional.h"
#include "third_party/blink/public/common/mime_util/mime_util.h"
#include "url/android/gurl_android.h"
#include "url/origin.h"

using base::android::ConvertJavaStringToUTF8;
using base::android::ConvertUTF16ToJavaString;
using base::android::ConvertUTF8ToJavaString;
using base::android::JavaParamRef;
using base::android::ScopedJavaLocalRef;
using offline_items_collection::android::OfflineItemBridge;
using OfflineItemSchedule = offline_items_collection::OfflineItemSchedule;

namespace {

// The remaining time for a download item if it cannot be calculated.
constexpr int64_t kUnknownRemainingTime = -1;

// Finch flag for controlling auto resumption limit.
int kDefaultAutoResumptionLimit = 5;
const char kAutoResumptionLimitParamName[] = "AutoResumptionLimit";

bool ShouldShowDownloadItem(download::DownloadItem* item) {
  return !item->IsTemporary() && !item->IsTransient();
}

ScopedJavaLocalRef<jobject> JNI_DownloadManagerService_CreateJavaDownloadItem(
    JNIEnv* env,
    download::DownloadItem* item) {
  DCHECK(!item->IsTransient());
  return Java_DownloadItem_createDownloadItem(
      env, DownloadManagerService::CreateJavaDownloadInfo(env, item),
      item->GetStartTime().ToJavaTime(), item->GetEndTime().ToJavaTime(),
      item->GetFileExternallyRemoved());
}

void RenameItemCallback(
    const base::android::ScopedJavaGlobalRef<jobject> j_callback,
    download::DownloadItem::DownloadRenameResult result) {
  base::android::RunIntCallbackAndroid(
      j_callback,
      static_cast<int32_t>(
          OfflineItemUtils::ConvertDownloadRenameResultToRenameResult(result)));
}

bool IsReducedModeProfileKey(ProfileKey* profile_key) {
  return profile_key == ProfileKeyStartupAccessor::GetInstance()->profile_key();
}

}  // namespace

// static
void DownloadManagerService::CreateAutoResumptionHandler() {
  auto network_listener =
      std::make_unique<download::NetworkStatusListenerAndroid>();
  auto task_scheduler =
      std::make_unique<download::android::DownloadTaskScheduler>();
  auto task_manager =
      std::make_unique<download::TaskManagerImpl>(std::move(task_scheduler));
  auto config = std::make_unique<download::AutoResumptionHandler::Config>();
  config->auto_resumption_size_limit =
      DownloadUtils::GetAutoResumptionSizeLimit();
  config->is_auto_resumption_enabled_in_native =
      chrome::android::IsJavaDrivenFeatureEnabled(
          download::features::kDownloadAutoResumptionNative);
  download::AutoResumptionHandler::Create(
      std::move(network_listener), std::move(task_manager), std::move(config),
      base::DefaultClock::GetInstance());
}

// static
void DownloadManagerService::OnDownloadCanceled(
    download::DownloadItem* download,
    bool has_no_external_storage) {
  if (download->IsTransient()) {
    LOG(WARNING) << "Transient download should not have user interaction!";
    return;
  }

  // Inform the user in Java UI about file writing failures.
  JNIEnv* env = base::android::AttachCurrentThread();

  ScopedJavaLocalRef<jobject> j_item =
      JNI_DownloadManagerService_CreateJavaDownloadItem(env, download);
  Java_DownloadManagerService_onDownloadItemCanceled(env, j_item,
                                                     has_no_external_storage);
}

// static
DownloadManagerService* DownloadManagerService::GetInstance() {
  return base::Singleton<DownloadManagerService>::get();
}

// static
ScopedJavaLocalRef<jobject> DownloadManagerService::CreateJavaDownloadInfo(
    JNIEnv* env,
    download::DownloadItem* item) {
  base::TimeDelta time_delta;
  bool time_remaining_known = item->TimeRemaining(&time_delta);
  GURL original_url = item->GetOriginalUrl().SchemeIs(url::kDataScheme)
                          ? GURL::EmptyGURL()
                          : item->GetOriginalUrl();
  content::BrowserContext* browser_context =
      content::DownloadItemUtils::GetBrowserContext(item);

  base::android::ScopedJavaLocalRef<jobject> otr_profile_id;
  if (browser_context && browser_context->IsOffTheRecord()) {
    Profile* profile = Profile::FromBrowserContext(browser_context);
    otr_profile_id = profile->GetOTRProfileID().ConvertToJavaOTRProfileID(env);
  }

  absl::optional<OfflineItemSchedule> offline_item_schedule;
  auto j_offline_item_schedule =
      OfflineItemBridge::CreateOfflineItemSchedule(env, offline_item_schedule);
  return Java_DownloadInfo_createDownloadInfo(
      env, ConvertUTF8ToJavaString(env, item->GetGuid()),
      ConvertUTF8ToJavaString(env, item->GetFileNameToReportUser().value()),
      ConvertUTF8ToJavaString(env, item->GetTargetFilePath().value()),
      url::GURLAndroid::FromNativeGURL(env, item->GetURL()),
      ConvertUTF8ToJavaString(env, item->GetMimeType()),
      item->GetReceivedBytes(), item->GetTotalBytes(), otr_profile_id,
      item->GetState(), item->PercentComplete(), item->IsPaused(),
      DownloadUtils::IsDownloadUserInitiated(item), item->CanResume(),
      item->IsParallelDownload(),
      url::GURLAndroid::FromNativeGURL(env, original_url),
      url::GURLAndroid::FromNativeGURL(env, item->GetReferrerUrl()),
      time_remaining_known ? time_delta.InMilliseconds()
                           : kUnknownRemainingTime,
      item->GetLastAccessTime().ToJavaTime(), item->IsDangerous(),
      static_cast<int>(
          OfflineItemUtils::ConvertDownloadInterruptReasonToFailState(
              item->GetLastReason())),
      j_offline_item_schedule);
}

static jlong JNI_DownloadManagerService_Init(JNIEnv* env,
                                             const JavaParamRef<jobject>& jobj,
                                             jboolean is_full_browser_started) {
  DownloadManagerService* service = DownloadManagerService::GetInstance();
  service->Init(env, jobj, is_full_browser_started);
  return reinterpret_cast<intptr_t>(service);
}

DownloadManagerService::DownloadActionParams::DownloadActionParams(
    DownloadAction download_action)
    : action(download_action), has_user_gesture(false) {}

DownloadManagerService::DownloadActionParams::DownloadActionParams(
    DownloadAction download_action,
    bool user_gesture)
    : action(download_action), has_user_gesture(user_gesture) {}

DownloadManagerService::DownloadActionParams::DownloadActionParams(
    const DownloadActionParams& other)
    : action(other.action), has_user_gesture(other.has_user_gesture) {}

DownloadManagerService::DownloadManagerService()
    : is_manager_initialized_(false), is_pending_downloads_loaded_(false) {}

DownloadManagerService::~DownloadManagerService() {}

void DownloadManagerService::Init(JNIEnv* env,
                                  jobject obj,
                                  bool is_profile_added) {
  java_ref_.Reset(env, obj);
  if (is_profile_added) {
    OnProfileAdded(
        ProfileManager::GetActiveUserProfile()->GetOriginalProfile());
  } else {
    // In reduced mode, only non-incognito downloads should be loaded.
    ResetCoordinatorIfNeeded(
        DownloadStartupUtils::EnsureDownloadSystemInitialized(nullptr));
  }
}

void DownloadManagerService::OnProfileAdded(
    JNIEnv* env,
    jobject obj,
    const JavaParamRef<jobject>& j_profile) {
  OnProfileAdded(ProfileAndroid::FromProfileAndroid(j_profile));
}

void DownloadManagerService::OnProfileAdded(Profile* profile) {
  InitializeForProfile(profile->GetProfileKey());
  observed_profiles_.AddObservation(profile);
  for (Profile* otr : profile->GetAllOffTheRecordProfiles())
    InitializeForProfile(otr->GetProfileKey());
}

void DownloadManagerService::OnOffTheRecordProfileCreated(
    Profile* off_the_record) {
  InitializeForProfile(off_the_record->GetProfileKey());
}

void DownloadManagerService::OpenDownload(download::DownloadItem* download,
                                          int source) {
  if (java_ref_.is_null())
    return;

  JNIEnv* env = base::android::AttachCurrentThread();
  ScopedJavaLocalRef<jobject> j_item =
      JNI_DownloadManagerService_CreateJavaDownloadItem(env, download);

  Java_DownloadManagerService_openDownloadItem(env, java_ref_, j_item, source);
}

void DownloadManagerService::HandleOMADownload(download::DownloadItem* download,
                                               int64_t system_download_id) {
  if (java_ref_.is_null())
    return;

  JNIEnv* env = base::android::AttachCurrentThread();
  ScopedJavaLocalRef<jobject> j_item =
      JNI_DownloadManagerService_CreateJavaDownloadItem(env, download);

  Java_DownloadManagerService_handleOMADownload(env, java_ref_, j_item,
                                                system_download_id);
}

void DownloadManagerService::OpenDownload(
    JNIEnv* env,
    jobject obj,
    const JavaParamRef<jstring>& jdownload_guid,
    const JavaParamRef<jobject>& j_profile_key,
    jint source) {
  if (!is_manager_initialized_)
    return;

  std::string download_guid = ConvertJavaStringToUTF8(env, jdownload_guid);
  download::DownloadItem* item = GetDownload(
      download_guid, ProfileKeyAndroid::FromProfileKeyAndroid(j_profile_key));
  if (!item)
    return;

  OpenDownload(item, source);
}

void DownloadManagerService::OpenDownloadsPage(
    Profile* profile,
    DownloadOpenSource download_open_source) {
  if (java_ref_.is_null() || !profile)
    return;

  JNIEnv* env = base::android::AttachCurrentThread();
  if (profile->IsIncognitoProfile()) {
    profile->GetOTRProfileID().ConvertToJavaOTRProfileID(env);
  }
  Java_DownloadManagerService_openDownloadsPage(
      env,
      profile->IsIncognitoProfile()
          ? profile->GetOTRProfileID().ConvertToJavaOTRProfileID(env)
          : nullptr,
      static_cast<int>(download_open_source));
}

void DownloadManagerService::ResumeDownload(
    JNIEnv* env,
    jobject obj,
    const JavaParamRef<jstring>& jdownload_guid,
    const JavaParamRef<jobject>& j_profile_key,
    bool has_user_gesture) {
  std::string download_guid = ConvertJavaStringToUTF8(env, jdownload_guid);
  ProfileKey* profile_key =
      ProfileKeyAndroid::FromProfileKeyAndroid(j_profile_key);
  if (is_pending_downloads_loaded_ || profile_key->IsOffTheRecord()) {
    ResumeDownloadInternal(download_guid, profile_key, has_user_gesture);
  } else {
    EnqueueDownloadAction(download_guid,
                          DownloadActionParams(RESUME, has_user_gesture));
  }
}

void DownloadManagerService::RetryDownload(
    JNIEnv* env,
    jobject obj,
    const JavaParamRef<jstring>& jdownload_guid,
    const JavaParamRef<jobject>& j_profile_key,
    bool has_user_gesture) {
  std::string download_guid = ConvertJavaStringToUTF8(env, jdownload_guid);
  ProfileKey* profile_key =
      ProfileKeyAndroid::FromProfileKeyAndroid(j_profile_key);
  if (is_pending_downloads_loaded_ || profile_key->IsOffTheRecord())
    RetryDownloadInternal(download_guid, profile_key, has_user_gesture);
  else
    EnqueueDownloadAction(download_guid, DownloadActionParams(RETRY));
}

void DownloadManagerService::PauseDownload(
    JNIEnv* env,
    jobject obj,
    const JavaParamRef<jstring>& jdownload_guid,
    const JavaParamRef<jobject>& j_profile_key) {
  std::string download_guid = ConvertJavaStringToUTF8(env, jdownload_guid);
  ProfileKey* profile_key =
      ProfileKeyAndroid::FromProfileKeyAndroid(j_profile_key);
  if (is_pending_downloads_loaded_ || profile_key->IsOffTheRecord())
    PauseDownloadInternal(download_guid, profile_key);
  else
    EnqueueDownloadAction(download_guid, DownloadActionParams(PAUSE));
}

void DownloadManagerService::RemoveDownload(
    JNIEnv* env,
    jobject obj,
    const JavaParamRef<jstring>& jdownload_guid,
    const JavaParamRef<jobject>& j_profile_key) {
  std::string download_guid = ConvertJavaStringToUTF8(env, jdownload_guid);
  ProfileKey* profile_key =
      ProfileKeyAndroid::FromProfileKeyAndroid(j_profile_key);
  if (is_manager_initialized_ || profile_key->IsOffTheRecord())
    RemoveDownloadInternal(download_guid, profile_key);
  else
    EnqueueDownloadAction(download_guid, DownloadActionParams(REMOVE));
}

void DownloadManagerService::GetAllDownloads(
    JNIEnv* env,
    const JavaParamRef<jobject>& obj,
    const JavaParamRef<jobject>& j_profile_key) {
  ProfileKey* profile_key =
      ProfileKeyAndroid::FromProfileKeyAndroid(j_profile_key);
  if (is_manager_initialized_) {
    GetAllDownloadsInternal(profile_key);
    return;
  }

  // Full download manager is required for this call.
  GetDownloadManager(profile_key);
  profiles_with_pending_get_downloads_actions_.push_back(profile_key);
}

void DownloadManagerService::GetAllDownloadsInternal(ProfileKey* profile_key) {
  content::DownloadManager* manager = GetDownloadManager(profile_key);
  if (java_ref_.is_null() || !manager)
    return;

  content::DownloadManager::DownloadVector all_items;
  manager->GetAllDownloads(&all_items);

  // Create a Java array of all of the visible DownloadItems.
  JNIEnv* env = base::android::AttachCurrentThread();
  ScopedJavaLocalRef<jobject> j_download_item_list =
      Java_DownloadManagerService_createDownloadItemList(env, java_ref_);

  for (size_t i = 0; i < all_items.size(); i++) {
    download::DownloadItem* item = all_items[i];
    if (!ShouldShowDownloadItem(item))
      continue;

    ScopedJavaLocalRef<jobject> j_item =
        JNI_DownloadManagerService_CreateJavaDownloadItem(env, item);
    Java_DownloadManagerService_addDownloadItemToList(
        env, java_ref_, j_download_item_list, j_item);
  }

  Java_DownloadManagerService_onAllDownloadsRetrieved(
      env, java_ref_, j_download_item_list,
      profile_key->GetProfileKeyAndroid()->GetJavaObject());
}

void DownloadManagerService::CheckForExternallyRemovedDownloads(
    JNIEnv* env,
    const JavaParamRef<jobject>& obj,
    const JavaParamRef<jobject>& j_profile_key) {
  // Once the DownloadManager is initlaized, DownloadHistory will check for the
  // removal of history files. If the history query is not yet complete, ignore
  // requests to check for externally removed downloads.
  if (!is_manager_initialized_)
    return;

  content::DownloadManager* manager = GetDownloadManager(
      ProfileKeyAndroid::FromProfileKeyAndroid(j_profile_key));
  if (!manager)
    return;
  manager->CheckForHistoryFilesRemoval();
}

void DownloadManagerService::UpdateLastAccessTime(
    JNIEnv* env,
    const JavaParamRef<jobject>& obj,
    const JavaParamRef<jstring>& jdownload_guid,
    const JavaParamRef<jobject>& j_profile_key) {
  std::string download_guid = ConvertJavaStringToUTF8(env, jdownload_guid);
  ProfileKey* profile_key =
      ProfileKeyAndroid::FromProfileKeyAndroid(j_profile_key);
  download::DownloadItem* item = GetDownload(download_guid, profile_key);
  if (item)
    item->SetLastAccessTime(base::Time::Now());
}

void DownloadManagerService::CancelDownload(
    JNIEnv* env,
    jobject obj,
    const JavaParamRef<jstring>& jdownload_guid,
    const JavaParamRef<jobject>& j_profile_key) {
  std::string download_guid = ConvertJavaStringToUTF8(env, jdownload_guid);
  ProfileKey* profile_key =
      ProfileKeyAndroid::FromProfileKeyAndroid(j_profile_key);
  if (is_pending_downloads_loaded_ || profile_key->IsOffTheRecord())
    CancelDownloadInternal(download_guid, profile_key);
  else
    EnqueueDownloadAction(download_guid, DownloadActionParams(CANCEL));
}

void DownloadManagerService::OnDownloadsInitialized(
    download::SimpleDownloadManagerCoordinator* coordinator,
    bool active_downloads_only) {
  if (active_downloads_only) {
    OnPendingDownloadsLoaded();
    return;
  }
  is_manager_initialized_ = true;
  OnPendingDownloadsLoaded();
  while (!profiles_with_pending_get_downloads_actions_.empty()) {
    ProfileKey* profile_key =
        profiles_with_pending_get_downloads_actions_.back();
    profiles_with_pending_get_downloads_actions_.pop_back();
    GetAllDownloadsInternal(profile_key);
  }
}

void DownloadManagerService::OnManagerGoingDown(
    download::SimpleDownloadManagerCoordinator* coordinator) {
  for (auto it = coordinators_.begin(); it != coordinators_.end(); it++) {
    if (it->second == coordinator) {
      coordinators_.erase(it->first);
      break;
    }
  }
}

void DownloadManagerService::OnDownloadCreated(
    download::SimpleDownloadManagerCoordinator* coordinator,
    download::DownloadItem* item) {
  if (item->IsTransient())
    return;

  JNIEnv* env = base::android::AttachCurrentThread();
  ScopedJavaLocalRef<jobject> j_item =
      JNI_DownloadManagerService_CreateJavaDownloadItem(env, item);
  Java_DownloadManagerService_onDownloadItemCreated(env, java_ref_, j_item);
}

void DownloadManagerService::OnDownloadUpdated(
    download::SimpleDownloadManagerCoordinator* coordinator,
    download::DownloadItem* item) {
  if (java_ref_.is_null())
    return;

  if (item->IsTemporary() || item->IsTransient())
    return;

  JNIEnv* env = base::android::AttachCurrentThread();
  ScopedJavaLocalRef<jobject> j_item =
      JNI_DownloadManagerService_CreateJavaDownloadItem(env, item);
  Java_DownloadManagerService_onDownloadItemUpdated(env, java_ref_, j_item);
}

void DownloadManagerService::OnDownloadRemoved(
    download::SimpleDownloadManagerCoordinator* coordinator,
    download::DownloadItem* item) {
  if (java_ref_.is_null() || item->IsTransient())
    return;

  const Profile* profile = Profile::FromBrowserContext(
      content::DownloadItemUtils::GetBrowserContext(item));

  JNIEnv* env = base::android::AttachCurrentThread();
  Java_DownloadManagerService_onDownloadItemRemoved(
      env, java_ref_, ConvertUTF8ToJavaString(env, item->GetGuid()),
      profile->IsOffTheRecord()
          ? profile->GetOTRProfileID().ConvertToJavaOTRProfileID(env)
          : nullptr);
}

void DownloadManagerService::ResumeDownloadInternal(
    const std::string& download_guid,
    ProfileKey* profile_key,
    bool has_user_gesture) {
  download::DownloadItem* item = GetDownload(download_guid, profile_key);
  if (!item) {
    OnResumptionFailed(download_guid);
    return;
  }
  if (!item->CanResume()) {
    OnResumptionFailed(download_guid);
    return;
  }
  DownloadControllerBase::Get()->AboutToResumeDownload(item);
  item->Resume(has_user_gesture);
  if (resume_callback_for_testing_)
    std::move(resume_callback_for_testing_).Run(true);
}

void DownloadManagerService::RetryDownloadInternal(
    const std::string& download_guid,
    ProfileKey* profile_key,
    bool has_user_gesture) {
  content::DownloadManager* manager = GetDownloadManager(profile_key);
  if (!manager)
    return;

  download::DownloadItem* item = manager->GetDownloadByGuid(download_guid);
  if (!item)
    return;

  // Try to resume first.
  if (item->CanResume()) {
    item->Resume(has_user_gesture);
    return;
  }

  net::NetworkTrafficAnnotationTag traffic_annotation =
      net::DefineNetworkTrafficAnnotation("download_manager_service_retry", R"(
        semantics {
          sender: "DownloadManagerService"
          description:
            "Retry a download by creating new network request."
          trigger:
            "User retries a download."
          data: "None."
          destination: WEBSITE
        }
        policy {
          cookies_allowed: YES
          cookies_store: "user"
          setting:
            "This feature cannot be disabled in settings, but it is activated "
            "by direct user action."
          chrome_policy {
            DownloadRestrictions {
              DownloadRestrictions: 3
            }
          }
        })");
  auto download_url_params = std::make_unique<download::DownloadUrlParameters>(
      item->GetURL(), traffic_annotation);

  // Retry allows redirect.
  download_url_params->set_cross_origin_redirects(
      network::mojom::RedirectMode::kFollow);

  // Retry is triggered through user gesture, and don't have renderer
  // associated, content initiated has to be false to avoid download being
  // blocked.
  download_url_params->set_content_initiated(false);

  // TODO(xingliu): See if we need to persist the referrer policy. Never clear
  // referrer potentially may result in delivering unexpected referrer to web
  // servers.
  download_url_params->set_referrer_policy(net::ReferrerPolicy::NEVER_CLEAR);
  download_url_params->set_referrer(item->GetReferrerUrl());
  download_url_params->set_download_source(download::DownloadSource::RETRY);

  // Creates a new download.
  manager->DownloadUrl(std::move(download_url_params));

  // Removes the current download.
  item->Remove();
}

void DownloadManagerService::CancelDownloadInternal(
    const std::string& download_guid,
    ProfileKey* profile_key) {
  download::DownloadItem* item = GetDownload(download_guid, profile_key);
  if (item) {
    // Remove the observer first to avoid item->Cancel() causing re-entrance
    // issue.
    item->RemoveObserver(DownloadControllerBase::Get());
    item->Cancel(true);
  }
}

void DownloadManagerService::PauseDownloadInternal(
    const std::string& download_guid,
    ProfileKey* profile_key) {
  download::DownloadItem* item = GetDownload(download_guid, profile_key);
  if (item)
    item->Pause();
}

void DownloadManagerService::RemoveDownloadInternal(
    const std::string& download_guid,
    ProfileKey* profile_key) {
  download::DownloadItem* item = GetDownload(download_guid, profile_key);
  if (item)
    item->Remove();
}

void DownloadManagerService::EnqueueDownloadAction(
    const std::string& download_guid,
    const DownloadActionParams& params) {
  auto iter = pending_actions_.find(download_guid);
  if (iter == pending_actions_.end()) {
    pending_actions_.insert(std::make_pair(download_guid, params));
    return;
  }
  switch (params.action) {
    case RESUME:
      if (iter->second.action == PAUSE)
        iter->second = params;
      break;
    case PAUSE:
      if (iter->second.action == RESUME)
        iter->second = params;
      break;
    case CANCEL:
      iter->second = params;
      break;
    case REMOVE:
      iter->second = params;
      break;
    default:
      NOTREACHED();
      break;
  }
}

void DownloadManagerService::OnResumptionFailed(
    const std::string& download_guid) {
  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE,
      base::BindOnce(&DownloadManagerService::OnResumptionFailedInternal,
                     base::Unretained(this), download_guid));
}

void DownloadManagerService::OnResumptionFailedInternal(
    const std::string& download_guid) {
  if (!java_ref_.is_null()) {
    JNIEnv* env = base::android::AttachCurrentThread();
    Java_DownloadManagerService_onResumptionFailed(
        env, java_ref_, ConvertUTF8ToJavaString(env, download_guid));
  }
  if (resume_callback_for_testing_)
    std::move(resume_callback_for_testing_).Run(false);
}

download::DownloadItem* DownloadManagerService::GetDownload(
    const std::string& download_guid,
    ProfileKey* profile_key) {
  download::SimpleDownloadManagerCoordinator* coordinator =
      GetCoordinator(profile_key);
  return coordinator ? coordinator->GetDownloadByGuid(download_guid) : nullptr;
}

void DownloadManagerService::OnPendingDownloadsLoaded() {
  is_pending_downloads_loaded_ = true;

  auto result =
      std::find_if(coordinators_.begin(), coordinators_.end(),
                   [](const auto& it) { return !it.first->IsOffTheRecord(); });
  CHECK(result != coordinators_.end())
      << "A non-OffTheRecord coordinator should exist when "
         "OnPendingDownloadsLoaded is triggered.";
  ProfileKey* profile_key = result->first;

  // Kick-off the auto-resumption handler.
  content::DownloadManager::DownloadVector all_items;
  GetCoordinator(profile_key)->GetAllDownloads(&all_items);

  if (!download::AutoResumptionHandler::Get())
    CreateAutoResumptionHandler();

  download::AutoResumptionHandler::Get()->SetResumableDownloads(all_items);

  for (auto iter = pending_actions_.begin(); iter != pending_actions_.end();
       ++iter) {
    DownloadActionParams params = iter->second;
    std::string download_guid = iter->first;
    switch (params.action) {
      case RESUME:
        ResumeDownloadInternal(download_guid, profile_key,
                               params.has_user_gesture);
        break;
      case PAUSE:
        PauseDownloadInternal(download_guid, profile_key);
        break;
      case CANCEL:
        CancelDownloadInternal(download_guid, profile_key);
        break;
      default:
        NOTREACHED();
        break;
    }
  }
  pending_actions_.clear();
}

content::DownloadManager* DownloadManagerService::GetDownloadManager(
    ProfileKey* profile_key) {
  Profile* profile =
      IsReducedModeProfileKey(profile_key)
          ? ProfileManager::GetActiveUserProfile()
          : ProfileManager::GetProfileFromProfileKey(profile_key);
  content::DownloadManager* manager = profile->GetDownloadManager();
  ResetCoordinatorIfNeeded(profile_key);
  return manager;
}

void DownloadManagerService::ResetCoordinatorIfNeeded(ProfileKey* profile_key) {
  download::SimpleDownloadManagerCoordinator* coordinator =
      SimpleDownloadManagerCoordinatorFactory::GetForKey(profile_key);
  UpdateCoordinator(coordinator, profile_key);
}

void DownloadManagerService::UpdateCoordinator(
    download::SimpleDownloadManagerCoordinator* new_coordinator,
    ProfileKey* profile_key) {
  bool coordinator_exists = base::Contains(coordinators_, profile_key);
  if (!coordinator_exists || coordinators_[profile_key] != new_coordinator) {
    if (coordinator_exists)
      coordinators_[profile_key]->GetNotifier()->RemoveObserver(this);
    coordinators_[profile_key] = new_coordinator;
    new_coordinator->GetNotifier()->AddObserver(this);
  }
}

download::SimpleDownloadManagerCoordinator*
DownloadManagerService::GetCoordinator(ProfileKey* profile_key) {
  DCHECK(base::Contains(coordinators_, profile_key));
  return coordinators_[profile_key];
}

void DownloadManagerService::RenameDownload(
    JNIEnv* env,
    const JavaParamRef<jobject>& obj,
    const JavaParamRef<jstring>& id,
    const JavaParamRef<jstring>& name,
    const JavaParamRef<jobject>& j_callback,
    const JavaParamRef<jobject>& j_profile_key) {
  std::string download_guid = ConvertJavaStringToUTF8(id);
  ProfileKey* profile_key =
      ProfileKeyAndroid::FromProfileKeyAndroid(j_profile_key);
  download::DownloadItem* item = GetDownload(download_guid, profile_key);
  if (!item) {
    base::ThreadTaskRunnerHandle::Get()->PostTask(
        FROM_HERE,
        base::BindOnce(
            &RenameItemCallback,
            base::android::ScopedJavaGlobalRef<jobject>(env, j_callback),
            download::DownloadItem::DownloadRenameResult::FAILURE_UNAVAILABLE));

    return;
  }
  std::string target_name = ConvertJavaStringToUTF8(name);
  base::OnceCallback<void(download::DownloadItem::DownloadRenameResult)>
      callback = base::BindOnce(
          &RenameItemCallback,
          base::android::ScopedJavaGlobalRef<jobject>(env, j_callback));
  item->Rename(base::FilePath(target_name), std::move(callback));
}

void DownloadManagerService::CreateInterruptedDownloadForTest(
    JNIEnv* env,
    jobject obj,
    const JavaParamRef<jstring>& jurl,
    const JavaParamRef<jstring>& jdownload_guid,
    const JavaParamRef<jstring>& jtarget_path) {
  download::InProgressDownloadManager* in_progress_manager =
      DownloadManagerUtils::GetInProgressDownloadManager(
          ProfileKeyStartupAccessor::GetInstance()->profile_key());
  std::vector<GURL> url_chain;
  url_chain.emplace_back(ConvertJavaStringToUTF8(env, jurl));
  base::FilePath target_path(ConvertJavaStringToUTF8(env, jtarget_path));
  in_progress_manager->AddInProgressDownloadForTest(
      std::make_unique<download::DownloadItemImpl>(
          in_progress_manager, ConvertJavaStringToUTF8(env, jdownload_guid), 1,
          target_path.AddExtension("crdownload"), target_path, url_chain,
          GURL(), "", GURL(), GURL(), url::Origin(), "", "", base::Time(),
          base::Time(), "", "", 0, -1, 0, "",
          download::DownloadItem::INTERRUPTED,
          download::DOWNLOAD_DANGER_TYPE_NOT_DANGEROUS,
          download::DOWNLOAD_INTERRUPT_REASON_CRASH, false, false, false,
          base::Time(), false,
          std::vector<download::DownloadItem::ReceivedSlice>(),
          download::DownloadItemRerouteInfo(), download::kInvalidRange,
          download::kInvalidRange, nullptr));
}

void DownloadManagerService::InitializeForProfile(ProfileKey* profile_key) {
  ResetCoordinatorIfNeeded(
      DownloadStartupUtils::EnsureDownloadSystemInitialized(profile_key));
}

// static
jboolean JNI_DownloadManagerService_IsSupportedMimeType(
    JNIEnv* env,
    const JavaParamRef<jstring>& jmime_type) {
  std::string mime_type = ConvertJavaStringToUTF8(env, jmime_type);
  return blink::IsSupportedMimeType(mime_type);
}

// static
jint JNI_DownloadManagerService_GetAutoResumptionLimit(JNIEnv* env) {
  std::string value = base::GetFieldTrialParamValueByFeature(
      chrome::android::kDownloadAutoResumptionThrottling,
      kAutoResumptionLimitParamName);
  int auto_resumption_limit;
  return base::StringToInt(value, &auto_resumption_limit)
             ? auto_resumption_limit
             : kDefaultAutoResumptionLimit;
}
