// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>

#include <memory>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/run_loop.h"
#include "base/strings/stringprintf.h"
#include "base/test/scoped_feature_list.h"
#include "chrome/browser/browser_features.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/download/chrome_download_manager_delegate.h"
#include "chrome/browser/download/download_core_service.h"
#include "chrome/browser/download/download_core_service_factory.h"
#include "chrome/browser/download/download_core_service_impl.h"
#include "chrome/browser/download/download_prefs.h"
#include "chrome/browser/download/download_status_updater.h"
#include "chrome/browser/profiles/keep_alive/profile_keep_alive_types.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/test/base/testing_browser_process.h"
#include "chrome/test/base/testing_profile.h"
#include "chrome/test/base/testing_profile_manager.h"
#include "components/download/public/common/mock_download_item.h"
#include "content/public/test/browser_task_environment.h"
#include "content/public/test/mock_download_manager.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using testing::AtLeast;
using testing::Invoke;
using testing::Mock;
using testing::Return;
using testing::SetArgPointee;
using testing::StrictMock;
using testing::WithArg;
using testing::_;

class TestDownloadStatusUpdater : public DownloadStatusUpdater {
 public:
  TestDownloadStatusUpdater()
      : notification_count_(0), acceptable_notification_item_(nullptr) {}
  void SetAcceptableNotificationItem(download::DownloadItem* item) {
    acceptable_notification_item_ = item;
  }
  size_t NotificationCount() {
    return notification_count_;
  }
 protected:
  void UpdateAppIconDownloadProgress(
      download::DownloadItem* download) override {
    ++notification_count_;
    if (acceptable_notification_item_)
      EXPECT_EQ(acceptable_notification_item_, download);
  }
 private:
  size_t notification_count_;
  raw_ptr<download::DownloadItem> acceptable_notification_item_;
};

class DownloadStatusUpdaterTest : public testing::Test {
 public:
  DownloadStatusUpdaterTest()
      : updater_(new TestDownloadStatusUpdater()),
        profile_manager_(TestingBrowserProcess::GetGlobal()) {}

  ~DownloadStatusUpdaterTest() override {
    for (size_t mgr_idx = 0; mgr_idx < managers_.size(); ++mgr_idx) {
      EXPECT_CALL(*Manager(mgr_idx), RemoveObserver(_));
    }

    delete updater_;
    updater_ = nullptr;
    VerifyAndClearExpectations();

    managers_.clear();
    manager_items_.clear();
    all_owned_items_.clear();

    base::RunLoop().RunUntilIdle();  // Allow DownloadManager destruction.
  }

  void SetUp() override { ASSERT_TRUE(profile_manager_.SetUp()); }

 protected:
  // Attach some number of DownloadManagers to the updater.
  void SetupManagers(int manager_count) {
    DCHECK_EQ(0U, managers_.size());
    for (int i = 0; i < manager_count; ++i) {
      managers_.push_back(
          std::make_unique<StrictMock<content::MockDownloadManager>>());
    }
  }

  void SetObserver(content::DownloadManager::Observer* observer) {
    manager_observers_[manager_observer_index_] = observer;
  }

  // Hook the specified manager into the updater.
  void LinkManager(int i) {
    content::MockDownloadManager* mgr = managers_[i].get();
    manager_observer_index_ = i;
    while (manager_observers_.size() <= static_cast<size_t>(i)) {
      manager_observers_.push_back(nullptr);
    }
    EXPECT_CALL(*mgr, IsManagerInitialized());
    EXPECT_CALL(*mgr, AddObserver(_))
        .WillOnce(WithArg<0>(Invoke(
            this, &DownloadStatusUpdaterTest::SetObserver)));
    TestingProfile* profile = profile_manager_.CreateTestingProfile(
        base::StringPrintf("Profile %d", i + 1));
    testing_profiles_.push_back(profile);
    EXPECT_CALL(*mgr, GetBrowserContext()).WillRepeatedly(Return(profile));
    auto delegate = std::make_unique<ChromeDownloadManagerDelegate>(profile);
    DownloadCoreServiceFactory::GetForBrowserContext(profile)
        ->SetDownloadManagerDelegateForTesting(std::move(delegate));
    updater_->AddManager(mgr);
  }

  // Add some number of Download items to a particular manager.
  void AddItems(int manager_index, int item_count, int in_progress_count) {
    DCHECK_GT(managers_.size(), static_cast<size_t>(manager_index));
    content::MockDownloadManager* manager = managers_[manager_index].get();

    if (manager_items_.size() <= static_cast<size_t>(manager_index))
      manager_items_.resize(manager_index+1);

    std::vector<download::DownloadItem*> item_list;
    for (int i = 0; i < item_count; ++i) {
      std::unique_ptr<download::MockDownloadItem> item =
          std::make_unique<StrictMock<download::MockDownloadItem>>();
      download::DownloadItem::DownloadState state =
          i < in_progress_count ? download::DownloadItem::IN_PROGRESS
                                : download::DownloadItem::CANCELLED;
      EXPECT_CALL(*item, IsTransient()).WillRepeatedly(Return(false));
      EXPECT_CALL(*item, GetState()).WillRepeatedly(Return(state));
      manager_items_[manager_index].push_back(item.get());
      all_owned_items_.push_back(std::move(item));
    }
    EXPECT_CALL(*manager, GetAllDownloads(_))
        .WillRepeatedly(SetArgPointee<0>(manager_items_[manager_index]));
  }

  // Return the specified manager.
  content::MockDownloadManager* Manager(int manager_index) {
    DCHECK_GT(managers_.size(), static_cast<size_t>(manager_index));
    return managers_[manager_index].get();
  }

  // Return the specified item.
  download::MockDownloadItem* Item(int manager_index, int item_index) {
    DCHECK_GT(manager_items_.size(), static_cast<size_t>(manager_index));
    DCHECK_GT(manager_items_[manager_index].size(),
              static_cast<size_t>(item_index));
    // All DownloadItems in manager_items_ are MockDownloadItems.
    return static_cast<download::MockDownloadItem*>(
        manager_items_[manager_index][item_index]);
  }

  // Set return values relevant to |DownloadStatusUpdater::GetProgress()|
  // for the specified item.
  void SetItemValues(int manager_index, int item_index,
                     int received_bytes, int total_bytes, bool notify) {
    download::MockDownloadItem* item(Item(manager_index, item_index));
    EXPECT_CALL(*item, GetReceivedBytes())
        .WillRepeatedly(Return(received_bytes));
    EXPECT_CALL(*item, GetTotalBytes())
        .WillRepeatedly(Return(total_bytes));
    if (notify)
      updater_->OnDownloadUpdated(managers_[manager_index].get(), item);
  }

  // Transition specified item to completed.
  void CompleteItem(int manager_index, int item_index) {
    download::MockDownloadItem* item(Item(manager_index, item_index));
    EXPECT_CALL(*item, GetState())
        .WillRepeatedly(Return(download::DownloadItem::COMPLETE));
    updater_->OnDownloadUpdated(managers_[manager_index].get(), item);
  }

  // Verify and clear all mocks expectations.
  void VerifyAndClearExpectations() {
    for (const auto& manager : managers_)
      Mock::VerifyAndClearExpectations(manager.get());
    for (auto it = manager_items_.begin(); it != manager_items_.end(); ++it)
      for (auto sit = it->begin(); sit != it->end(); ++sit)
        Mock::VerifyAndClearExpectations(*sit);
  }

  // The mocked download managers.
  std::vector<std::unique_ptr<content::MockDownloadManager>> managers_;
  // The download items being downloaded by those managers in |managers_|. The
  // top-level vector is the manager index, and the inner vector is the list of
  // items of that manager. The inner vector is a vector<DownloadItem*> for
  // compatibility with the return value of DownloadManager::GetAllDownloads().
  std::vector<std::vector<download::DownloadItem*>> manager_items_;
  // An owning container for items in |manager_items_|.
  std::vector<std::unique_ptr<download::DownloadItem>> all_owned_items_;
  int manager_observer_index_;

  std::vector<content::DownloadManager::Observer*> manager_observers_;

  // Pointer so we can verify that destruction triggers appropriate
  // changes.
  raw_ptr<TestDownloadStatusUpdater> updater_;

  // Thread so that the DownloadManager (which is a DeleteOnUIThread
  // object) can be deleted.
  content::BrowserTaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};

  // To test ScopedProfileKeepAlive behavior.
  TestingProfileManager profile_manager_;
  std::vector<TestingProfile*> testing_profiles_;
};

// Test null updater.
TEST_F(DownloadStatusUpdaterTest, Basic) {
  float progress = -1;
  int download_count = -1;
  EXPECT_TRUE(updater_->GetProgress(&progress, &download_count));
  EXPECT_FLOAT_EQ(0.0f, progress);
  EXPECT_EQ(0, download_count);
}

// Test updater with null manager.
TEST_F(DownloadStatusUpdaterTest, OneManagerNoItems) {
  SetupManagers(1);
  AddItems(0, 0, 0);
  LinkManager(0);
  VerifyAndClearExpectations();

  float progress = -1;
  int download_count = -1;
  EXPECT_CALL(*managers_[0], GetAllDownloads(_))
      .WillRepeatedly(SetArgPointee<0>(manager_items_[0]));
  EXPECT_TRUE(updater_->GetProgress(&progress, &download_count));
  EXPECT_FLOAT_EQ(0.0f, progress);
  EXPECT_EQ(0, download_count);
}

// Test updater with non-null manager, including transition an item to
// |download::DownloadItem::COMPLETE| and adding a new item.
TEST_F(DownloadStatusUpdaterTest, OneManagerManyItems) {
  SetupManagers(1);
  AddItems(0, 3, 2);
  LinkManager(0);

  // Prime items
  SetItemValues(0, 0, 10, 20, false);
  SetItemValues(0, 1, 50, 60, false);
  SetItemValues(0, 2, 90, 90, false);

  float progress = -1;
  int download_count = -1;
  EXPECT_TRUE(updater_->GetProgress(&progress, &download_count));
  EXPECT_FLOAT_EQ((10+50)/(20.0f+60), progress);
  EXPECT_EQ(2, download_count);

  // Transition one item to completed and confirm progress is updated
  // properly.
  CompleteItem(0, 0);
  EXPECT_TRUE(updater_->GetProgress(&progress, &download_count));
  EXPECT_FLOAT_EQ(50/60.0f, progress);
  EXPECT_EQ(1, download_count);

  // Add a new item to manager and confirm progress is updated properly.
  AddItems(0, 1, 1);
  SetItemValues(0, 3, 150, 200, false);
  manager_observers_[0]->OnDownloadCreated(
      managers_[0].get(), manager_items_[0][manager_items_[0].size() - 1]);

  EXPECT_TRUE(updater_->GetProgress(&progress, &download_count));
  EXPECT_FLOAT_EQ((50+150)/(60+200.0f), progress);
  EXPECT_EQ(2, download_count);
}

// Test to ensure that the download progress notification is called correctly.
TEST_F(DownloadStatusUpdaterTest, ProgressNotification) {
  size_t expected_notifications = updater_->NotificationCount();
  SetupManagers(1);
  AddItems(0, 2, 2);
  LinkManager(0);

  // Expect two notifications, one for each item; which item will come first
  // isn't defined so it cannot be tested.
  expected_notifications += 2;
  ASSERT_EQ(expected_notifications, updater_->NotificationCount());

  // Make progress on the first item.
  updater_->SetAcceptableNotificationItem(Item(0, 0));
  SetItemValues(0, 0, 10, 20, true);
  ++expected_notifications;
  ASSERT_EQ(expected_notifications, updater_->NotificationCount());

  // Second item completes!
  updater_->SetAcceptableNotificationItem(Item(0, 1));
  CompleteItem(0, 1);
  ++expected_notifications;
  ASSERT_EQ(expected_notifications, updater_->NotificationCount());

  // First item completes.
  updater_->SetAcceptableNotificationItem(Item(0, 0));
  CompleteItem(0, 0);
  ++expected_notifications;
  ASSERT_EQ(expected_notifications, updater_->NotificationCount());

  updater_->SetAcceptableNotificationItem(nullptr);
}

// Confirm we recognize the situation where we have an unknown size.
TEST_F(DownloadStatusUpdaterTest, UnknownSize) {
  SetupManagers(1);
  AddItems(0, 2, 2);
  LinkManager(0);

  // Prime items
  SetItemValues(0, 0, 10, 20, false);
  SetItemValues(0, 1, 50, -1, false);

  float progress = -1;
  int download_count = -1;
  EXPECT_FALSE(updater_->GetProgress(&progress, &download_count));
}

// Test many null managers.
TEST_F(DownloadStatusUpdaterTest, ManyManagersNoItems) {
  SetupManagers(1);
  AddItems(0, 0, 0);
  LinkManager(0);

  float progress = -1;
  int download_count = -1;
  EXPECT_TRUE(updater_->GetProgress(&progress, &download_count));
  EXPECT_FLOAT_EQ(0.0f, progress);
  EXPECT_EQ(0, download_count);
}

// Test many managers with all items complete.
TEST_F(DownloadStatusUpdaterTest, ManyManagersEmptyItems) {
  SetupManagers(2);
  AddItems(0, 3, 0);
  LinkManager(0);
  AddItems(1, 3, 0);
  LinkManager(1);

  float progress = -1;
  int download_count = -1;
  EXPECT_TRUE(updater_->GetProgress(&progress, &download_count));
  EXPECT_FLOAT_EQ(0.0f, progress);
  EXPECT_EQ(0, download_count);
}

// Test many managers with some non-complete items.
TEST_F(DownloadStatusUpdaterTest, ManyManagersMixedItems) {
  SetupManagers(2);
  AddItems(0, 3, 2);
  LinkManager(0);
  AddItems(1, 3, 1);
  LinkManager(1);

  SetItemValues(0, 0, 10, 20, false);
  SetItemValues(0, 1, 50, 60, false);
  SetItemValues(1, 0, 80, 90, false);

  float progress = -1;
  int download_count = -1;
  EXPECT_TRUE(updater_->GetProgress(&progress, &download_count));
  EXPECT_FLOAT_EQ((10+50+80)/(20.0f+60+90), progress);
  EXPECT_EQ(3, download_count);
}

// Test that it prevents Profile deletion.
TEST_F(DownloadStatusUpdaterTest, HoldsKeepAlive) {
  base::test::ScopedFeatureList feature_list;
  feature_list.InitAndEnableFeature(features::kDestroyProfileOnBrowserClose);

  ProfileManager* profile_manager = g_browser_process->profile_manager();
  ASSERT_NE(nullptr, profile_manager);

  SetupManagers(2);
  AddItems(0, 2, 1);
  LinkManager(0);
  AddItems(1, 2, 0);
  LinkManager(1);

  // Profile 1 has a download in progress.
  Profile* profile1 = testing_profiles_[0];
  SetItemValues(0, 0, 10, 20, true);
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(profile_manager->HasKeepAliveForTesting(
      profile1, ProfileKeepAliveOrigin::kDownloadInProgress));

  // Profile 2 doesn't have a download in progress.
  Profile* profile2 = testing_profiles_[1];
  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(profile_manager->HasKeepAliveForTesting(
      profile2, ProfileKeepAliveOrigin::kDownloadInProgress));

  // Complete Profile 1's download. It should release its keepalive.
  CompleteItem(0, 0);
  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(profile_manager->HasKeepAliveForTesting(
      profile1, ProfileKeepAliveOrigin::kDownloadInProgress));
}

// Test that the last completion time is logged in pref.
TEST_F(DownloadStatusUpdaterTest, LogLastCompletionTimeInPrefs) {
  SetupManagers(/*manager_count=*/1);
  AddItems(/*manager_index=*/0, /*item_count=*/3, /*in_progress_count=*/0);
  LinkManager(0);

  DownloadPrefs* download_prefs =
      DownloadPrefs::FromDownloadManager(Manager(0));
  base::Time initial_time = download_prefs->GetLastCompleteTime();
  base::Time current_time = base::Time::Now();

  // Set the first download to in progress and notify the update.
  SetItemValues(/*manager_index=*/0, /*item_index=*/0, /*received_bytes=*/90,
                /*total_bytes=*/100, /*notify=*/true);
  // The last complete time is still the initial time, because the download is
  // not complete yet.
  EXPECT_EQ(initial_time, download_prefs->GetLastCompleteTime());

  // The first download has completed.
  CompleteItem(/*manager_index=*/0, /*item_index=*/0);
  // The last complete time is updated.
  EXPECT_EQ(current_time, download_prefs->GetLastCompleteTime());

  task_environment_.FastForwardBy(base::Hours(1));
  // Set the second download item to in progress and notify the update.
  SetItemValues(/*manager_index=*/0, /*item_index=*/1, /*received_bytes=*/90,
                /*total_bytes=*/100, /*notify=*/true);
  // The last complete time is not updated yet, because the second download is
  // still in progress.
  EXPECT_EQ(current_time, download_prefs->GetLastCompleteTime());

  task_environment_.FastForwardBy(base::Hours(1));
  // The second download has completed.
  CompleteItem(/*manager_index=*/0, /*item_index=*/1);
  // Completed time is updated
  EXPECT_EQ(current_time + base::Hours(2),
            download_prefs->GetLastCompleteTime());

  task_environment_.FastForwardBy(base::Hours(1));
  SetItemValues(/*manager_index=*/0, /*item_index=*/2, /*received_bytes=*/90,
                /*total_bytes=*/100, /*notify=*/true);
  EXPECT_CALL(*Item(/*manager_index=*/0, /*item_index=*/2), IsTransient())
      .WillRepeatedly(Return(true));
  CompleteItem(/*manager_index=*/0, /*item_index=*/2);
  // Completed time is not updated, because this download is transient.
  EXPECT_EQ(current_time + base::Hours(2),
            download_prefs->GetLastCompleteTime());
}

// Tests that transient download will not trigger any updates.
TEST_F(DownloadStatusUpdaterTest, TransientDownload) {
  SetupManagers(/*manager_count=*/1);
  AddItems(/*manager_index=*/0, /*item_count=*/2, /*in_progress_count=*/0);
  LinkManager(0);

  std::unique_ptr<download::MockDownloadItem> item =
      std::make_unique<StrictMock<download::MockDownloadItem>>();

  EXPECT_CALL(*item, GetState())
      .WillRepeatedly(Return(download::DownloadItem::IN_PROGRESS));
  EXPECT_CALL(*item, IsTransient()).WillRepeatedly(Return(true));
  manager_items_[0].push_back(item.get());
  all_owned_items_.push_back(std::move(item));
  manager_observers_[0]->OnDownloadCreated(
      managers_[0].get(), manager_items_[0][manager_items_[0].size() - 1]);

  float progress = -1;
  int download_count = -1;
  EXPECT_TRUE(updater_->GetProgress(&progress, &download_count));
  EXPECT_FLOAT_EQ(0.0f, progress);
  EXPECT_EQ(0, download_count);
}
