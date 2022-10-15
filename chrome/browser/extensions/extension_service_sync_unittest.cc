// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>

#include <map>
#include <memory>
#include <string>
#include <utility>

#include "base/bind.h"
#include "base/command_line.h"
#include "base/files/file_util.h"
#include "base/memory/ptr_util.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/extensions/chrome_test_extension_loader.h"
#include "chrome/browser/extensions/component_loader.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/extensions/extension_service_test_with_install.h"
#include "chrome/browser/extensions/extension_sync_data.h"
#include "chrome/browser/extensions/extension_sync_service.h"
#include "chrome/browser/extensions/extension_util.h"
#include "chrome/browser/extensions/test_blocklist.h"
#include "chrome/browser/extensions/updater/extension_updater.h"
#include "chrome/browser/profiles/profile_key.h"
#include "chrome/browser/sync/sync_service_factory.h"
#include "chrome/browser/themes/test/theme_service_changed_waiter.h"
#include "chrome/browser/themes/theme_service.h"
#include "chrome/browser/themes/theme_service_factory.h"
#include "chrome/common/chrome_constants.h"
#include "chrome/common/extensions/extension_test_util.h"
#include "chrome/common/extensions/sync_helper.h"
#include "chrome/test/base/testing_profile.h"
#include "components/crx_file/id_util.h"
#include "components/sync/driver/sync_service.h"
#include "components/sync/driver/sync_user_settings.h"
#include "components/sync/model/sync_data.h"
#include "components/sync/protocol/app_specifics.pb.h"
#include "components/sync/protocol/entity_specifics.pb.h"
#include "components/sync/protocol/extension_specifics.pb.h"
#include "components/sync/test/fake_sync_change_processor.h"
#include "components/sync/test/sync_change_processor_wrapper_for_test.h"
#include "components/sync/test/sync_error_factory_mock.h"
#include "components/variations/variations_associated_data.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/storage_partition.h"
#include "extensions/browser/app_sorting.h"
#include "extensions/browser/disable_reason.h"
#include "extensions/browser/extension_prefs.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/extension_system.h"
#include "extensions/browser/extension_util.h"
#include "extensions/browser/management_policy.h"
#include "extensions/common/extension_builder.h"
#include "extensions/common/extension_urls.h"
#include "extensions/common/manifest_url_handlers.h"
#include "extensions/common/mojom/manifest.mojom-shared.h"
#include "extensions/common/permissions/permission_set.h"

using extensions::AppSorting;
using extensions::Extension;
using extensions::ExtensionPrefs;
using extensions::ExtensionRegistry;
using extensions::ExtensionSyncData;
using extensions::ExtensionSystem;
using extensions::Manifest;
using extensions::PermissionSet;
using extensions::mojom::ManifestLocation;
using syncer::SyncChange;
using syncer::SyncChangeList;
using testing::Mock;

namespace {

const char good0[] = "behllobkkfkfnphdnhnkndlbkcpglgmj";
const char good2[] = "bjafgdebaacbbbecmhlhpofkepfkgcpa";
const char good_crx[] = "ldnnhddmnhbkjipkidpdiheffobcpfmf";
const char page_action[] = "obcimlgaoabeegjmmpldobjndiealpln";
const char theme2_crx[] = "ibcijncamhmjjdodjamgiipcgnnaeagd";
const syncer::SyncFirstSetupCompleteSource kSetSourceFromTest =
    syncer::SyncFirstSetupCompleteSource::BASIC_FLOW;

ExtensionSyncData GetDisableSyncData(const Extension& extension,
                                     int disable_reasons) {
  bool enabled = false;
  bool incognito_enabled = false;
  bool remote_install = false;
  return ExtensionSyncData(extension, enabled, disable_reasons,
                           incognito_enabled, remote_install, GURL());
}

ExtensionSyncData GetEnableSyncData(const Extension& extension) {
  bool enabled = true;
  bool incognito_enabled = false;
  bool remote_install = false;
  return ExtensionSyncData(extension, enabled,
                           extensions::disable_reason::DISABLE_NONE,
                           incognito_enabled, remote_install, GURL());
}

SyncChangeList MakeSyncChangeList(const std::string& id,
                                  const sync_pb::EntitySpecifics& specifics,
                                  SyncChange::SyncChangeType change_type) {
  syncer::SyncData sync_data =
      syncer::SyncData::CreateLocalData(id, "Name", specifics);
  return SyncChangeList(1, SyncChange(FROM_HERE, change_type, sync_data));
}

// This is a FakeSyncChangeProcessor specialization that maintains a store of
// SyncData items in the superclass' data_ member variable, treating it like a
// map keyed by the extension id from the SyncData. Each instance of this class
// should only be used for one model type (which should be either extensions or
// apps) to match how the real sync system handles things.
class StatefulChangeProcessor : public syncer::FakeSyncChangeProcessor {
 public:
  explicit StatefulChangeProcessor(syncer::ModelType expected_type)
      : expected_type_(expected_type) {
    EXPECT_TRUE(expected_type == syncer::ModelType::EXTENSIONS ||
                expected_type == syncer::ModelType::APPS);
  }

  StatefulChangeProcessor(const StatefulChangeProcessor&) = delete;
  StatefulChangeProcessor& operator=(const StatefulChangeProcessor&) = delete;

  ~StatefulChangeProcessor() override {}

  // We let our parent class, FakeSyncChangeProcessor, handle saving the
  // changes for us, but in addition we "apply" these changes by treating
  // the FakeSyncChangeProcessor's SyncDataList as a map keyed by extension
  // id.
  absl::optional<syncer::ModelError> ProcessSyncChanges(
      const base::Location& from_here,
      const syncer::SyncChangeList& change_list) override {
    syncer::FakeSyncChangeProcessor::ProcessSyncChanges(from_here, change_list);
    for (const auto& change : change_list) {
      syncer::SyncData sync_data = change.sync_data();
      EXPECT_EQ(expected_type_, sync_data.GetDataType());

      std::unique_ptr<ExtensionSyncData> modified =
          ExtensionSyncData::CreateFromSyncData(sync_data);

      // Start by removing any existing entry for this extension id.
      for (auto iter = data_.begin(); iter != data_.end(); ++iter) {
        std::unique_ptr<ExtensionSyncData> existing =
            ExtensionSyncData::CreateFromSyncData(*iter);
        if (existing->id() == modified->id()) {
          data_.erase(iter);
          break;
        }
      }

      // Now add in the new data for this id, if appropriate.
      if (change.change_type() == SyncChange::ACTION_ADD ||
          change.change_type() == SyncChange::ACTION_UPDATE) {
        data_.push_back(sync_data);
      } else if (change.change_type() != SyncChange::ACTION_DELETE) {
        ADD_FAILURE() << "Unexpected change type " << change.change_type();
      }
    }
    return absl::nullopt;
  }

  // This is a helper to vend a wrapped version of this object suitable for
  // passing in to MergeDataAndStartSyncing, which takes a
  // std::unique_ptr<SyncChangeProcessor>, since in tests we typically don't
  // want to
  // give up ownership of a local change processor.
  std::unique_ptr<syncer::SyncChangeProcessor> GetWrapped() {
    return std::make_unique<syncer::SyncChangeProcessorWrapperForTest>(this);
  }

  const syncer::SyncDataList& data() const { return data_; }

 private:
  // The expected ModelType of changes that this processor will see.
  const syncer::ModelType expected_type_;
  syncer::SyncDataList data_;
};

}  // namespace

class ExtensionServiceSyncTest
    : public extensions::ExtensionServiceTestWithInstall {
 public:
  void MockSyncStartFlare(bool* was_called,
                          syncer::ModelType* model_type_passed_in,
                          syncer::ModelType model_type) {
    *was_called = true;
    *model_type_passed_in = model_type;
  }

  // Helper to call MergeDataAndStartSyncing with no server data and dummy
  // change processor / error factory.
  void StartSyncing(syncer::ModelType type) {
    ASSERT_TRUE(type == syncer::EXTENSIONS || type == syncer::APPS);
    extension_sync_service()->MergeDataAndStartSyncing(
        type, syncer::SyncDataList(),
        std::make_unique<syncer::FakeSyncChangeProcessor>(),
        std::make_unique<syncer::SyncErrorFactoryMock>());
  }

  void DisableExtensionFromSync(const Extension& extension,
                                int disable_reasons) {
    ExtensionSyncData disable_extension = GetDisableSyncData(
        extension, extensions::disable_reason::DISABLE_USER_ACTION);
    SyncChangeList list(
        1, disable_extension.GetSyncChange(SyncChange::ACTION_UPDATE));
    extension_sync_service()->ProcessSyncChanges(FROM_HERE, list);
  }

  void EnableExtensionFromSync(const Extension& extension) {
    ExtensionSyncData enable_extension = GetEnableSyncData(extension);
    SyncChangeList list(
        1, enable_extension.GetSyncChange(SyncChange::ACTION_UPDATE));
    extension_sync_service()->ProcessSyncChanges(FROM_HERE, list);
  }

 protected:
  // Paths to some of the fake extensions.
  base::FilePath good0_path() {
    return data_dir()
        .AppendASCII("good")
        .AppendASCII("Extensions")
        .AppendASCII(good0)
        .AppendASCII("1.0.0.0");
  }

  ExtensionSyncService* extension_sync_service() {
    return ExtensionSyncService::Get(profile());
  }

  ExtensionSystem* extension_system() {
    return ExtensionSystem::Get(profile());
  }
};

TEST_F(ExtensionServiceSyncTest, DeferredSyncStartupPreInstalledComponent) {
  InitializeEmptyExtensionService();

  bool flare_was_called = false;
  syncer::ModelType triggered_type(syncer::UNSPECIFIED);
  base::WeakPtrFactory<ExtensionServiceSyncTest> factory(this);
  extension_sync_service()->SetSyncStartFlareForTesting(base::BindRepeating(
      &ExtensionServiceSyncTest::MockSyncStartFlare, factory.GetWeakPtr(),
      &flare_was_called,  // Safe due to WeakPtrFactory scope.
      &triggered_type));  // Safe due to WeakPtrFactory scope.

  // Install a component extension.
  std::string manifest;
  ASSERT_TRUE(base::ReadFileToString(
      good0_path().Append(extensions::kManifestFilename), &manifest));
  service()->component_loader()->Add(manifest, good0_path());
  ASSERT_FALSE(extension_system()->is_ready());
  service()->Init();
  ASSERT_TRUE(extension_system()->is_ready());

  // Extensions added before service is_ready() don't trigger sync startup.
  EXPECT_FALSE(flare_was_called);
  ASSERT_EQ(syncer::UNSPECIFIED, triggered_type);
}

TEST_F(ExtensionServiceSyncTest, DeferredSyncStartupPreInstalledNormal) {
  InitializeGoodInstalledExtensionService();

  bool flare_was_called = false;
  syncer::ModelType triggered_type(syncer::UNSPECIFIED);
  base::WeakPtrFactory<ExtensionServiceSyncTest> factory(this);
  extension_sync_service()->SetSyncStartFlareForTesting(base::BindRepeating(
      &ExtensionServiceSyncTest::MockSyncStartFlare, factory.GetWeakPtr(),
      &flare_was_called,  // Safe due to WeakPtrFactory scope.
      &triggered_type));  // Safe due to WeakPtrFactory scope.

  ASSERT_FALSE(extension_system()->is_ready());
  service()->Init();
  ASSERT_EQ(3u, loaded_extensions().size());
  ASSERT_TRUE(extension_system()->is_ready());

  // Extensions added before service is_ready() don't trigger sync startup.
  EXPECT_FALSE(flare_was_called);
  ASSERT_EQ(syncer::UNSPECIFIED, triggered_type);
}

TEST_F(ExtensionServiceSyncTest, DeferredSyncStartupOnInstall) {
  InitializeEmptyExtensionService();
  service()->Init();
  ASSERT_TRUE(extension_system()->is_ready());

  bool flare_was_called = false;
  syncer::ModelType triggered_type(syncer::UNSPECIFIED);
  base::WeakPtrFactory<ExtensionServiceSyncTest> factory(this);
  extension_sync_service()->SetSyncStartFlareForTesting(base::BindRepeating(
      &ExtensionServiceSyncTest::MockSyncStartFlare, factory.GetWeakPtr(),
      &flare_was_called,  // Safe due to WeakPtrFactory scope.
      &triggered_type));  // Safe due to WeakPtrFactory scope.

  base::FilePath path = data_dir().AppendASCII("good.crx");
  InstallCRX(path, INSTALL_NEW);

  EXPECT_TRUE(flare_was_called);
  EXPECT_EQ(syncer::EXTENSIONS, triggered_type);

  // Reset.
  flare_was_called = false;
  triggered_type = syncer::UNSPECIFIED;

  // Once sync starts, flare should no longer be invoked.
  extension_sync_service()->MergeDataAndStartSyncing(
      syncer::EXTENSIONS, syncer::SyncDataList(),
      std::make_unique<syncer::FakeSyncChangeProcessor>(),
      std::make_unique<syncer::SyncErrorFactoryMock>());
  path = data_dir().AppendASCII("page_action.crx");
  InstallCRX(path, INSTALL_NEW);
  EXPECT_FALSE(flare_was_called);
  ASSERT_EQ(syncer::UNSPECIFIED, triggered_type);
}

TEST_F(ExtensionServiceSyncTest, DisableExtensionFromSync) {
  // Start the extensions service with one external extension already installed.
  base::FilePath source_install_dir =
      data_dir().AppendASCII("good").AppendASCII("Extensions");
  base::FilePath pref_path =
      source_install_dir.DirName().Append(chrome::kPreferencesFilename);

  InitializeInstalledExtensionService(pref_path, source_install_dir);

  // The user has enabled sync.
  syncer::SyncService* sync_service =
      SyncServiceFactory::GetForProfile(profile());
  sync_service->GetUserSettings()->SetFirstSetupComplete(kSetSourceFromTest);

  service()->Init();
  ASSERT_TRUE(extension_system()->is_ready());

  ASSERT_EQ(3u, loaded_extensions().size());

  // We start enabled.
  const Extension* extension = registry()->enabled_extensions().GetByID(good0);
  ASSERT_TRUE(extension);
  ASSERT_TRUE(service()->IsExtensionEnabled(good0));

  // Sync starts up.
  extension_sync_service()->MergeDataAndStartSyncing(
      syncer::EXTENSIONS, syncer::SyncDataList(),
      std::make_unique<syncer::FakeSyncChangeProcessor>(),
      std::make_unique<syncer::SyncErrorFactoryMock>());

  // Then sync data arrives telling us to disable |good0|.
  ExtensionSyncData disable_good_crx(
      *extension, false, extensions::disable_reason::DISABLE_USER_ACTION, false,
      false, extension_urls::GetWebstoreUpdateUrl());
  SyncChangeList list(
      1, disable_good_crx.GetSyncChange(SyncChange::ACTION_UPDATE));
  extension_sync_service()->ProcessSyncChanges(FROM_HERE, list);

  ASSERT_FALSE(service()->IsExtensionEnabled(good0));
}

// Test that sync can enable and disable installed extensions.
TEST_F(ExtensionServiceSyncTest, ReenableDisabledExtensionFromSync) {
  InitializeEmptyExtensionService();

  // Enable sync.
  syncer::SyncService* sync_service =
      SyncServiceFactory::GetForProfile(profile());
  sync_service->GetUserSettings()->SetFirstSetupComplete(kSetSourceFromTest);

  service()->Init();

  // Load up a simple extension.
  extensions::ChromeTestExtensionLoader extension_loader(profile());
  extension_loader.set_pack_extension(true);
  scoped_refptr<const Extension> extension = extension_loader.LoadExtension(
      data_dir().AppendASCII("simple_with_file"));
  ASSERT_TRUE(extension);
  const std::string kExtensionId = extension->id();
  ASSERT_TRUE(registry()->enabled_extensions().GetByID(kExtensionId));

  syncer::FakeSyncChangeProcessor* processor_raw = nullptr;
  {
    auto processor = std::make_unique<syncer::FakeSyncChangeProcessor>();
    processor_raw = processor.get();
    extension_sync_service()->MergeDataAndStartSyncing(
        syncer::EXTENSIONS, syncer::SyncDataList(), std::move(processor),
        std::make_unique<syncer::SyncErrorFactoryMock>());
  }
  processor_raw->changes().clear();

  DisableExtensionFromSync(*extension,
                           extensions::disable_reason::DISABLE_USER_ACTION);

  // The extension should be disabled.
  EXPECT_TRUE(registry()->disabled_extensions().GetByID(kExtensionId));
  EXPECT_EQ(extensions::disable_reason::DISABLE_USER_ACTION,
            ExtensionPrefs::Get(profile())->GetDisableReasons(kExtensionId));
  EXPECT_TRUE(processor_raw->changes().empty());

  // Enable the extension. Sync should push the new state.
  service()->EnableExtension(kExtensionId);
  {
    ASSERT_EQ(1u, processor_raw->changes().size());
    const SyncChange& change = processor_raw->changes()[0];
    EXPECT_EQ(SyncChange::ACTION_UPDATE, change.change_type());
    std::unique_ptr<ExtensionSyncData> data =
        ExtensionSyncData::CreateFromSyncData(change.sync_data());
    EXPECT_EQ(kExtensionId, data->id());
    EXPECT_EQ(0, data->disable_reasons());
    EXPECT_TRUE(data->enabled());
  }

  // Disable the extension again. Sync should push the new state.
  processor_raw->changes().clear();
  service()->DisableExtension(kExtensionId,
                              extensions::disable_reason::DISABLE_USER_ACTION);
  EXPECT_TRUE(registry()->disabled_extensions().GetByID(kExtensionId));
  {
    ASSERT_EQ(1u, processor_raw->changes().size());
    const SyncChange& change = processor_raw->changes()[0];
    EXPECT_EQ(SyncChange::ACTION_UPDATE, change.change_type());
    std::unique_ptr<ExtensionSyncData> data =
        ExtensionSyncData::CreateFromSyncData(change.sync_data());
    EXPECT_EQ(kExtensionId, data->id());
    EXPECT_EQ(extensions::disable_reason::DISABLE_USER_ACTION,
              data->disable_reasons());
    EXPECT_FALSE(data->enabled());
  }
  processor_raw->changes().clear();

  // Enable the extension via sync.
  EnableExtensionFromSync(*extension);

  // The extension should be enabled.
  EXPECT_TRUE(registry()->enabled_extensions().GetByID(kExtensionId));
  EXPECT_TRUE(processor_raw->changes().empty());
}

// Tests that default-installed extensions won't be affected by incoming sync
// data. (It's feasible to have a sync entry for an extension that could be
// default installed, since one installation may be default-installed while
// another may not be).
TEST_F(ExtensionServiceSyncTest,
       DefaultInstalledExtensionsAreNotReenabledOrDisabledBySync) {
  InitializeEmptyExtensionService();

  // Enable sync.
  syncer::SyncService* sync_service =
      SyncServiceFactory::GetForProfile(profile());
  sync_service->GetUserSettings()->SetFirstSetupComplete(kSetSourceFromTest);

  service()->Init();

  // Load up an extension that's considered default installed.
  extensions::ChromeTestExtensionLoader extension_loader(profile());
  extension_loader.set_pack_extension(true);
  extension_loader.add_creation_flag(Extension::WAS_INSTALLED_BY_DEFAULT);
  scoped_refptr<const Extension> extension = extension_loader.LoadExtension(
      data_dir().AppendASCII("simple_with_file"));
  ASSERT_TRUE(extension);

  // The extension shouldn't sync.
  EXPECT_FALSE(extensions::util::ShouldSync(extension.get(), profile()));
  const std::string kExtensionId = extension->id();
  ASSERT_TRUE(registry()->enabled_extensions().GetByID(kExtensionId));

  syncer::FakeSyncChangeProcessor* processor_raw = nullptr;
  {
    auto processor = std::make_unique<syncer::FakeSyncChangeProcessor>();
    processor_raw = processor.get();
    extension_sync_service()->MergeDataAndStartSyncing(
        syncer::EXTENSIONS, syncer::SyncDataList(), std::move(processor),
        std::make_unique<syncer::SyncErrorFactoryMock>());
  }
  processor_raw->changes().clear();

  // Sync state says the extension is disabled (e.g. on another machine).
  DisableExtensionFromSync(*extension,
                           extensions::disable_reason::DISABLE_USER_ACTION);

  // The extension should still be enabled, since it's default-installed.
  EXPECT_TRUE(registry()->enabled_extensions().GetByID(kExtensionId));
  EXPECT_TRUE(processor_raw->changes().empty());

  // Now disable the extension locally. Sync should *not* push new state.
  service()->DisableExtension(kExtensionId,
                              extensions::disable_reason::DISABLE_USER_ACTION);
  EXPECT_TRUE(registry()->disabled_extensions().GetByID(kExtensionId));
  EXPECT_TRUE(processor_raw->changes().empty());

  // Sync state says the extension is enabled.
  EnableExtensionFromSync(*extension);

  // As above, the extension should not have been affected by sync.
  EXPECT_TRUE(registry()->disabled_extensions().GetByID(kExtensionId));
  EXPECT_TRUE(processor_raw->changes().empty());

  // And re-enabling the extension should not push new state to sync.
  service()->EnableExtension(kExtensionId);
  EXPECT_TRUE(registry()->enabled_extensions().GetByID(kExtensionId));
  EXPECT_TRUE(processor_raw->changes().empty());
}

TEST_F(ExtensionServiceSyncTest, IgnoreSyncChangesWhenLocalStateIsMoreRecent) {
  // Start the extension service with three extensions already installed.
  base::FilePath source_install_dir =
      data_dir().AppendASCII("good").AppendASCII("Extensions");
  base::FilePath pref_path =
      source_install_dir.DirName().Append(chrome::kPreferencesFilename);

  InitializeInstalledExtensionService(pref_path, source_install_dir);

  // The user has enabled sync.
  syncer::SyncService* sync_service =
      SyncServiceFactory::GetForProfile(profile());
  sync_service->GetUserSettings()->SetFirstSetupComplete(kSetSourceFromTest);
  // Make sure ExtensionSyncService is created, so it'll be notified of changes.
  extension_sync_service();

  service()->Init();
  ASSERT_TRUE(extension_system()->is_ready());
  ASSERT_EQ(3u, loaded_extensions().size());

  ASSERT_TRUE(service()->IsExtensionEnabled(good0));
  ASSERT_TRUE(service()->IsExtensionEnabled(good2));

  // Disable and re-enable good0 before first sync data arrives.
  service()->DisableExtension(good0,
                              extensions::disable_reason::DISABLE_USER_ACTION);
  ASSERT_FALSE(service()->IsExtensionEnabled(good0));
  service()->EnableExtension(good0);
  ASSERT_TRUE(service()->IsExtensionEnabled(good0));
  // Disable good2 before first sync data arrives (good1 is considered
  // non-syncable because it has plugin permission).
  service()->DisableExtension(good2,
                              extensions::disable_reason::DISABLE_USER_ACTION);
  ASSERT_FALSE(service()->IsExtensionEnabled(good2));

  const Extension* extension0 = registry()->enabled_extensions().GetByID(good0);
  const Extension* extension2 =
      registry()->disabled_extensions().GetByID(good2);
  ASSERT_TRUE(extensions::sync_helper::IsSyncable(extension0));
  ASSERT_TRUE(extensions::sync_helper::IsSyncable(extension2));

  // Now sync data comes in that says to disable good0 and enable good2.
  ExtensionSyncData disable_good0(
      *extension0, false, extensions::disable_reason::DISABLE_USER_ACTION,
      false, false, extension_urls::GetWebstoreUpdateUrl());
  ExtensionSyncData enable_good2(
      *extension2, true, extensions::disable_reason::DISABLE_NONE, false, false,
      extension_urls::GetWebstoreUpdateUrl());
  syncer::SyncDataList sync_data;
  sync_data.push_back(disable_good0.GetSyncData());
  sync_data.push_back(enable_good2.GetSyncData());
  extension_sync_service()->MergeDataAndStartSyncing(
      syncer::EXTENSIONS, sync_data,
      std::make_unique<syncer::FakeSyncChangeProcessor>(),
      std::make_unique<syncer::SyncErrorFactoryMock>());

  // Both sync changes should be ignored, since the local state was changed
  // before sync started, and so the local state is considered more recent.
  EXPECT_TRUE(service()->IsExtensionEnabled(good0));
  EXPECT_FALSE(service()->IsExtensionEnabled(good2));
}

TEST_F(ExtensionServiceSyncTest, DontSelfNotify) {
  // Start the extension service with three extensions already installed.
  base::FilePath source_install_dir =
      data_dir().AppendASCII("good").AppendASCII("Extensions");
  base::FilePath pref_path =
      source_install_dir.DirName().Append(chrome::kPreferencesFilename);

  InitializeInstalledExtensionService(pref_path, source_install_dir);

  // The user has enabled sync.
  SyncServiceFactory::GetForProfile(profile())
      ->GetUserSettings()
      ->SetFirstSetupComplete(kSetSourceFromTest);
  // Make sure ExtensionSyncService is created, so it'll be notified of changes.
  extension_sync_service();

  service()->Init();
  ASSERT_TRUE(extension_system()->is_ready());
  ASSERT_EQ(3u, loaded_extensions().size());
  ASSERT_TRUE(service()->IsExtensionEnabled(good0));

  syncer::FakeSyncChangeProcessor* processor =
      new syncer::FakeSyncChangeProcessor;
  extension_sync_service()->MergeDataAndStartSyncing(
      syncer::EXTENSIONS, syncer::SyncDataList(), base::WrapUnique(processor),
      std::make_unique<syncer::SyncErrorFactoryMock>());

  processor->changes().clear();

  // Simulate various incoming sync changes, and make sure they don't result in
  // any outgoing changes.

  {
    const Extension* extension =
        registry()->enabled_extensions().GetByID(good0);
    ASSERT_TRUE(extension);

    // Disable the extension.
    ExtensionSyncData data(
        *extension, false, extensions::disable_reason::DISABLE_USER_ACTION,
        false, false, extension_urls::GetWebstoreUpdateUrl());
    SyncChangeList list(1, data.GetSyncChange(SyncChange::ACTION_UPDATE));

    extension_sync_service()->ProcessSyncChanges(FROM_HERE, list);

    EXPECT_TRUE(processor->changes().empty());
  }

  {
    const Extension* extension =
        registry()->disabled_extensions().GetByID(good0);
    ASSERT_TRUE(extension);

    // Set incognito enabled to true.
    ExtensionSyncData data(*extension, false,
                           extensions::disable_reason::DISABLE_NONE, true,
                           false, extension_urls::GetWebstoreUpdateUrl());
    SyncChangeList list(1, data.GetSyncChange(SyncChange::ACTION_UPDATE));

    extension_sync_service()->ProcessSyncChanges(FROM_HERE, list);

    EXPECT_TRUE(processor->changes().empty());
  }

  {
    const Extension* extension =
        registry()->disabled_extensions().GetByID(good0);
    ASSERT_TRUE(extension);

    // Add another disable reason.
    ExtensionSyncData data(
        *extension, false,
        extensions::disable_reason::DISABLE_USER_ACTION |
            extensions::disable_reason::DISABLE_PERMISSIONS_INCREASE,
        false, false, extension_urls::GetWebstoreUpdateUrl());
    SyncChangeList list(1, data.GetSyncChange(SyncChange::ACTION_UPDATE));

    extension_sync_service()->ProcessSyncChanges(FROM_HERE, list);

    EXPECT_TRUE(processor->changes().empty());
  }

  {
    const Extension* extension =
        registry()->disabled_extensions().GetByID(good0);
    ASSERT_TRUE(extension);

    // Uninstall the extension.
    ExtensionSyncData data(
        *extension, false,
        extensions::disable_reason::DISABLE_USER_ACTION |
            extensions::disable_reason::DISABLE_PERMISSIONS_INCREASE,
        false, false, extension_urls::GetWebstoreUpdateUrl());
    SyncChangeList list(1, data.GetSyncChange(SyncChange::ACTION_DELETE));

    extension_sync_service()->ProcessSyncChanges(FROM_HERE, list);

    EXPECT_TRUE(processor->changes().empty());
  }
}

TEST_F(ExtensionServiceSyncTest, GetSyncData) {
  InitializeEmptyExtensionService();
  InstallCRX(data_dir().AppendASCII("good.crx"), INSTALL_NEW);
  const Extension* extension = registry()->GetInstalledExtension(good_crx);
  ASSERT_TRUE(extension);

  extension_sync_service()->MergeDataAndStartSyncing(
      syncer::EXTENSIONS, syncer::SyncDataList(),
      std::make_unique<syncer::FakeSyncChangeProcessor>(),
      std::make_unique<syncer::SyncErrorFactoryMock>());

  syncer::SyncDataList list =
      extension_sync_service()->GetAllSyncDataForTesting(syncer::EXTENSIONS);
  ASSERT_EQ(list.size(), 1U);
  std::unique_ptr<ExtensionSyncData> data =
      ExtensionSyncData::CreateFromSyncData(list[0]);
  ASSERT_TRUE(data.get());
  EXPECT_EQ(extension->id(), data->id());
  EXPECT_FALSE(data->uninstalled());
  EXPECT_EQ(service()->IsExtensionEnabled(good_crx), data->enabled());
  EXPECT_EQ(extensions::util::IsIncognitoEnabled(good_crx, profile()),
            data->incognito_enabled());
  EXPECT_EQ(data->version(), extension->version());
  EXPECT_EQ(extensions::ManifestURL::GetUpdateURL(extension),
            data->update_url());
}

TEST_F(ExtensionServiceSyncTest, GetSyncDataDisableReasons) {
  InitializeEmptyExtensionService();
  const Extension* extension =
      InstallCRX(data_dir().AppendASCII("good.crx"), INSTALL_NEW);
  ASSERT_TRUE(extension);

  extension_sync_service()->MergeDataAndStartSyncing(
      syncer::EXTENSIONS, syncer::SyncDataList(),
      std::make_unique<syncer::FakeSyncChangeProcessor>(),
      std::make_unique<syncer::SyncErrorFactoryMock>());

  {
    syncer::SyncDataList list =
        extension_sync_service()->GetAllSyncDataForTesting(syncer::EXTENSIONS);
    ASSERT_EQ(list.size(), 1U);
    std::unique_ptr<ExtensionSyncData> data =
        ExtensionSyncData::CreateFromSyncData(list[0]);
    ASSERT_TRUE(data.get());
    EXPECT_TRUE(data->enabled());
    EXPECT_TRUE(data->supports_disable_reasons());
    EXPECT_EQ(extensions::disable_reason::DISABLE_NONE,
              data->disable_reasons());
  }

  // Syncable disable reason, should propagate to sync.
  service()->DisableExtension(good_crx,
                              extensions::disable_reason::DISABLE_USER_ACTION);
  {
    syncer::SyncDataList list =
        extension_sync_service()->GetAllSyncDataForTesting(syncer::EXTENSIONS);
    ASSERT_EQ(list.size(), 1U);
    std::unique_ptr<ExtensionSyncData> data =
        ExtensionSyncData::CreateFromSyncData(list[0]);
    ASSERT_TRUE(data.get());
    EXPECT_FALSE(data->enabled());
    EXPECT_TRUE(data->supports_disable_reasons());
    EXPECT_EQ(extensions::disable_reason::DISABLE_USER_ACTION,
              data->disable_reasons());
  }
  service()->EnableExtension(good_crx);

  // Non-syncable disable reason. The sync data should still say "enabled".
  service()->DisableExtension(good_crx,
                              extensions::disable_reason::DISABLE_RELOAD);
  {
    syncer::SyncDataList list =
        extension_sync_service()->GetAllSyncDataForTesting(syncer::EXTENSIONS);
    ASSERT_EQ(list.size(), 1U);
    std::unique_ptr<ExtensionSyncData> data =
        ExtensionSyncData::CreateFromSyncData(list[0]);
    ASSERT_TRUE(data.get());
    EXPECT_TRUE(data->enabled());
    EXPECT_TRUE(data->supports_disable_reasons());
    EXPECT_EQ(extensions::disable_reason::DISABLE_NONE,
              data->disable_reasons());
  }
  service()->EnableExtension(good_crx);

  // Both a syncable and a non-syncable disable reason, only the former should
  // propagate to sync.
  service()->DisableExtension(good_crx,
                              extensions::disable_reason::DISABLE_USER_ACTION |
                                  extensions::disable_reason::DISABLE_RELOAD);
  {
    syncer::SyncDataList list =
        extension_sync_service()->GetAllSyncDataForTesting(syncer::EXTENSIONS);
    ASSERT_EQ(list.size(), 1U);
    std::unique_ptr<ExtensionSyncData> data =
        ExtensionSyncData::CreateFromSyncData(list[0]);
    ASSERT_TRUE(data.get());
    EXPECT_FALSE(data->enabled());
    EXPECT_TRUE(data->supports_disable_reasons());
    EXPECT_EQ(extensions::disable_reason::DISABLE_USER_ACTION,
              data->disable_reasons());
  }
  service()->EnableExtension(good_crx);
}

TEST_F(ExtensionServiceSyncTest, GetSyncDataTerminated) {
  InitializeEmptyExtensionService();
  InstallCRX(data_dir().AppendASCII("good.crx"), INSTALL_NEW);
  TerminateExtension(good_crx);
  const Extension* extension = registry()->GetInstalledExtension(good_crx);
  ASSERT_TRUE(extension);

  extension_sync_service()->MergeDataAndStartSyncing(
      syncer::EXTENSIONS, syncer::SyncDataList(),
      std::make_unique<syncer::FakeSyncChangeProcessor>(),
      std::make_unique<syncer::SyncErrorFactoryMock>());

  syncer::SyncDataList list =
      extension_sync_service()->GetAllSyncDataForTesting(syncer::EXTENSIONS);
  ASSERT_EQ(list.size(), 1U);
  std::unique_ptr<ExtensionSyncData> data =
      ExtensionSyncData::CreateFromSyncData(list[0]);
  ASSERT_TRUE(data.get());
  EXPECT_EQ(extension->id(), data->id());
  EXPECT_FALSE(data->uninstalled());
  EXPECT_EQ(service()->IsExtensionEnabled(good_crx), data->enabled());
  EXPECT_EQ(extensions::util::IsIncognitoEnabled(good_crx, profile()),
            data->incognito_enabled());
  EXPECT_EQ(data->version(), extension->version());
  EXPECT_EQ(extensions::ManifestURL::GetUpdateURL(extension),
            data->update_url());
}

TEST_F(ExtensionServiceSyncTest, GetSyncDataFilter) {
  InitializeEmptyExtensionService();
  InstallCRX(data_dir().AppendASCII("good.crx"), INSTALL_NEW);
  const Extension* extension = registry()->GetInstalledExtension(good_crx);
  ASSERT_TRUE(extension);

  extension_sync_service()->MergeDataAndStartSyncing(
      syncer::APPS, syncer::SyncDataList(),
      std::make_unique<syncer::FakeSyncChangeProcessor>(),
      std::make_unique<syncer::SyncErrorFactoryMock>());

  syncer::SyncDataList list =
      extension_sync_service()->GetAllSyncDataForTesting(syncer::EXTENSIONS);
  ASSERT_EQ(list.size(), 0U);
}

TEST_F(ExtensionServiceSyncTest, GetSyncExtensionDataUserSettings) {
  InitializeEmptyExtensionService();
  InstallCRX(data_dir().AppendASCII("good.crx"), INSTALL_NEW);
  const Extension* extension = registry()->GetInstalledExtension(good_crx);
  ASSERT_TRUE(extension);

  extension_sync_service()->MergeDataAndStartSyncing(
      syncer::EXTENSIONS, syncer::SyncDataList(),
      std::make_unique<syncer::FakeSyncChangeProcessor>(),
      std::make_unique<syncer::SyncErrorFactoryMock>());

  {
    syncer::SyncDataList list =
        extension_sync_service()->GetAllSyncDataForTesting(syncer::EXTENSIONS);
    ASSERT_EQ(list.size(), 1U);
    std::unique_ptr<ExtensionSyncData> data =
        ExtensionSyncData::CreateFromSyncData(list[0]);
    ASSERT_TRUE(data.get());
    EXPECT_TRUE(data->enabled());
    EXPECT_FALSE(data->incognito_enabled());
  }

  service()->DisableExtension(good_crx,
                              extensions::disable_reason::DISABLE_USER_ACTION);
  {
    syncer::SyncDataList list =
        extension_sync_service()->GetAllSyncDataForTesting(syncer::EXTENSIONS);
    ASSERT_EQ(list.size(), 1U);
    std::unique_ptr<ExtensionSyncData> data =
        ExtensionSyncData::CreateFromSyncData(list[0]);
    ASSERT_TRUE(data.get());
    EXPECT_FALSE(data->enabled());
    EXPECT_FALSE(data->incognito_enabled());
  }

  extensions::util::SetIsIncognitoEnabled(good_crx, profile(), true);
  {
    syncer::SyncDataList list =
        extension_sync_service()->GetAllSyncDataForTesting(syncer::EXTENSIONS);
    ASSERT_EQ(list.size(), 1U);
    std::unique_ptr<ExtensionSyncData> data =
        ExtensionSyncData::CreateFromSyncData(list[0]);
    ASSERT_TRUE(data.get());
    EXPECT_FALSE(data->enabled());
    EXPECT_TRUE(data->incognito_enabled());
  }

  service()->EnableExtension(good_crx);
  {
    syncer::SyncDataList list =
        extension_sync_service()->GetAllSyncDataForTesting(syncer::EXTENSIONS);
    ASSERT_EQ(list.size(), 1U);
    std::unique_ptr<ExtensionSyncData> data =
        ExtensionSyncData::CreateFromSyncData(list[0]);
    ASSERT_TRUE(data.get());
    EXPECT_TRUE(data->enabled());
    EXPECT_TRUE(data->incognito_enabled());
  }
}

TEST_F(ExtensionServiceSyncTest, SyncForUninstalledExternalExtension) {
  InitializeEmptyExtensionService();
  InstallCRX(data_dir().AppendASCII("good.crx"),
             ManifestLocation::kExternalPref, INSTALL_NEW, Extension::NO_FLAGS);
  const Extension* extension = registry()->GetInstalledExtension(good_crx);
  ASSERT_TRUE(extension);

  extension_sync_service()->MergeDataAndStartSyncing(
      syncer::EXTENSIONS, syncer::SyncDataList(),
      std::make_unique<syncer::FakeSyncChangeProcessor>(),
      std::make_unique<syncer::SyncErrorFactoryMock>());
  StartSyncing(syncer::APPS);

  UninstallExtension(good_crx);
  EXPECT_TRUE(
      ExtensionPrefs::Get(profile())->IsExternalExtensionUninstalled(good_crx));

  sync_pb::EntitySpecifics specifics;
  sync_pb::AppSpecifics* app_specifics = specifics.mutable_app();
  sync_pb::ExtensionSpecifics* extension_specifics =
      app_specifics->mutable_extension();
  extension_specifics->set_id(good_crx);
  extension_specifics->set_version("1.0");
  extension_specifics->set_enabled(true);

  SyncChangeList list =
      MakeSyncChangeList(good_crx, specifics, SyncChange::ACTION_UPDATE);

  extension_sync_service()->ProcessSyncChanges(FROM_HERE, list);
  EXPECT_TRUE(
      ExtensionPrefs::Get(profile())->IsExternalExtensionUninstalled(good_crx));
}

TEST_F(ExtensionServiceSyncTest, GetSyncAppDataUserSettings) {
  InitializeEmptyExtensionService();
  const Extension* app =
      PackAndInstallCRX(data_dir().AppendASCII("app"), INSTALL_NEW);
  ASSERT_TRUE(app);
  ASSERT_TRUE(app->is_app());

  extension_sync_service()->MergeDataAndStartSyncing(
      syncer::APPS, syncer::SyncDataList(),
      std::make_unique<syncer::FakeSyncChangeProcessor>(),
      std::make_unique<syncer::SyncErrorFactoryMock>());

  syncer::StringOrdinal initial_ordinal =
      syncer::StringOrdinal::CreateInitialOrdinal();
  {
    syncer::SyncDataList list =
        extension_sync_service()->GetAllSyncDataForTesting(syncer::APPS);
    ASSERT_EQ(list.size(), 1U);

    std::unique_ptr<ExtensionSyncData> app_sync_data =
        ExtensionSyncData::CreateFromSyncData(list[0]);
    EXPECT_TRUE(initial_ordinal.Equals(app_sync_data->app_launch_ordinal()));
    EXPECT_TRUE(initial_ordinal.Equals(app_sync_data->page_ordinal()));
  }

  AppSorting* sorting = ExtensionSystem::Get(profile())->app_sorting();
  sorting->SetAppLaunchOrdinal(app->id(), initial_ordinal.CreateAfter());
  {
    syncer::SyncDataList list =
        extension_sync_service()->GetAllSyncDataForTesting(syncer::APPS);
    ASSERT_EQ(list.size(), 1U);

    std::unique_ptr<ExtensionSyncData> app_sync_data =
        ExtensionSyncData::CreateFromSyncData(list[0]);
    ASSERT_TRUE(app_sync_data.get());
    EXPECT_TRUE(initial_ordinal.LessThan(app_sync_data->app_launch_ordinal()));
    EXPECT_TRUE(initial_ordinal.Equals(app_sync_data->page_ordinal()));
  }

  sorting->SetPageOrdinal(app->id(), initial_ordinal.CreateAfter());
  {
    syncer::SyncDataList list =
        extension_sync_service()->GetAllSyncDataForTesting(syncer::APPS);
    ASSERT_EQ(list.size(), 1U);

    std::unique_ptr<ExtensionSyncData> app_sync_data =
        ExtensionSyncData::CreateFromSyncData(list[0]);
    ASSERT_TRUE(app_sync_data.get());
    EXPECT_TRUE(initial_ordinal.LessThan(app_sync_data->app_launch_ordinal()));
    EXPECT_TRUE(initial_ordinal.LessThan(app_sync_data->page_ordinal()));
  }
}

// TODO (rdevlin.cronin): The OnExtensionMoved() method has been removed from
// ExtensionService, so this test probably needs a new home. Unfortunately, it
// relies pretty heavily on things like InitializeExtension[Sync]Service() and
// PackAndInstallCRX(). When we clean up a bit more, this should move out.
TEST_F(ExtensionServiceSyncTest, GetSyncAppDataUserSettingsOnExtensionMoved) {
  InitializeEmptyExtensionService();
  const size_t kAppCount = 3;
  const Extension* apps[kAppCount];
  apps[0] = PackAndInstallCRX(data_dir().AppendASCII("app1"), INSTALL_NEW);
  apps[1] = PackAndInstallCRX(data_dir().AppendASCII("app2"), INSTALL_NEW);
  apps[2] = PackAndInstallCRX(data_dir().AppendASCII("app4"), INSTALL_NEW);
  for (size_t i = 0; i < kAppCount; ++i) {
    ASSERT_TRUE(apps[i]);
    ASSERT_TRUE(apps[i]->is_app());
  }

  extension_sync_service()->MergeDataAndStartSyncing(
      syncer::APPS, syncer::SyncDataList(),
      base::WrapUnique(new syncer::FakeSyncChangeProcessor()),
      base::WrapUnique(new syncer::SyncErrorFactoryMock()));

  ExtensionSystem::Get(service()->GetBrowserContext())
      ->app_sorting()
      ->OnExtensionMoved(apps[0]->id(), apps[1]->id(), apps[2]->id());
  {
    syncer::SyncDataList list =
        extension_sync_service()->GetAllSyncDataForTesting(syncer::APPS);
    ASSERT_EQ(list.size(), 3U);

    std::unique_ptr<ExtensionSyncData> data[kAppCount];
    for (size_t i = 0; i < kAppCount; ++i) {
      data[i] = ExtensionSyncData::CreateFromSyncData(list[i]);
      ASSERT_TRUE(data[i].get());
    }

    // The sync data is not always in the same order our apps were installed in,
    // so we do that sorting here so we can make sure the values are changed as
    // expected.
    syncer::StringOrdinal app_launch_ordinals[kAppCount];
    for (size_t i = 0; i < kAppCount; ++i) {
      for (size_t j = 0; j < kAppCount; ++j) {
        if (apps[i]->id() == data[j]->id())
          app_launch_ordinals[i] = data[j]->app_launch_ordinal();
      }
    }

    EXPECT_TRUE(app_launch_ordinals[1].LessThan(app_launch_ordinals[0]));
    EXPECT_TRUE(app_launch_ordinals[0].LessThan(app_launch_ordinals[2]));
  }
}

TEST_F(ExtensionServiceSyncTest, GetSyncDataList) {
  InitializeEmptyExtensionService();
  InstallCRX(data_dir().AppendASCII("good.crx"), INSTALL_NEW);
  InstallCRX(data_dir().AppendASCII("page_action.crx"), INSTALL_NEW);
  InstallCRX(data_dir().AppendASCII("theme.crx"), INSTALL_NEW);
  InstallCRX(data_dir().AppendASCII("theme2.crx"), INSTALL_NEW);

  extension_sync_service()->MergeDataAndStartSyncing(
      syncer::APPS, syncer::SyncDataList(),
      std::make_unique<syncer::FakeSyncChangeProcessor>(),
      std::make_unique<syncer::SyncErrorFactoryMock>());
  extension_sync_service()->MergeDataAndStartSyncing(
      syncer::EXTENSIONS, syncer::SyncDataList(),
      std::make_unique<syncer::FakeSyncChangeProcessor>(),
      std::make_unique<syncer::SyncErrorFactoryMock>());

  service()->DisableExtension(page_action,
                              extensions::disable_reason::DISABLE_USER_ACTION);
  TerminateExtension(theme2_crx);

  EXPECT_EQ(
      0u,
      extension_sync_service()->GetAllSyncDataForTesting(syncer::APPS).size());
  EXPECT_EQ(2u, extension_sync_service()
                    ->GetAllSyncDataForTesting(syncer::EXTENSIONS)
                    .size());
}

TEST_F(ExtensionServiceSyncTest, ProcessSyncDataUninstall) {
  InitializeEmptyExtensionService();
  extension_sync_service()->MergeDataAndStartSyncing(
      syncer::EXTENSIONS, syncer::SyncDataList(),
      std::make_unique<syncer::FakeSyncChangeProcessor>(),
      std::make_unique<syncer::SyncErrorFactoryMock>());

  sync_pb::EntitySpecifics specifics;
  sync_pb::ExtensionSpecifics* ext_specifics = specifics.mutable_extension();
  ext_specifics->set_id(good_crx);
  ext_specifics->set_version("1.0");

  SyncChangeList list =
      MakeSyncChangeList(good_crx, specifics, SyncChange::ACTION_DELETE);

  // Should do nothing.
  extension_sync_service()->ProcessSyncChanges(FROM_HERE, list);
  EXPECT_FALSE(
      registry()->GetExtensionById(good_crx, ExtensionRegistry::EVERYTHING));

  // Install the extension.
  base::FilePath extension_path = data_dir().AppendASCII("good.crx");
  InstallCRX(extension_path, INSTALL_NEW);
  EXPECT_TRUE(registry()->enabled_extensions().GetByID(good_crx));

  // Should uninstall the extension.
  extension_sync_service()->ProcessSyncChanges(FROM_HERE, list);
  EXPECT_FALSE(
      registry()->GetExtensionById(good_crx, ExtensionRegistry::EVERYTHING));

  // Should again do nothing.
  extension_sync_service()->ProcessSyncChanges(FROM_HERE, list);
  EXPECT_FALSE(
      registry()->GetExtensionById(good_crx, ExtensionRegistry::EVERYTHING));
}

TEST_F(ExtensionServiceSyncTest, ProcessSyncDataWrongType) {
  InitializeEmptyExtensionService();
  StartSyncing(syncer::EXTENSIONS);
  StartSyncing(syncer::APPS);

  // Install the extension.
  base::FilePath extension_path = data_dir().AppendASCII("good.crx");
  InstallCRX(extension_path, INSTALL_NEW);
  EXPECT_TRUE(registry()->enabled_extensions().GetByID(good_crx));

  sync_pb::EntitySpecifics specifics;
  sync_pb::AppSpecifics* app_specifics = specifics.mutable_app();
  sync_pb::ExtensionSpecifics* extension_specifics =
      app_specifics->mutable_extension();
  extension_specifics->set_id(good_crx);
  extension_specifics->set_version(
      registry()->GetInstalledExtension(good_crx)->version().GetString());

  {
    extension_specifics->set_enabled(true);

    SyncChangeList list =
        MakeSyncChangeList(good_crx, specifics, SyncChange::ACTION_DELETE);

    // Should do nothing
    extension_sync_service()->ProcessSyncChanges(FROM_HERE, list);
    EXPECT_TRUE(registry()->enabled_extensions().GetByID(good_crx));
  }

  {
    extension_specifics->set_enabled(false);

    SyncChangeList list =
        MakeSyncChangeList(good_crx, specifics, SyncChange::ACTION_UPDATE);

    // Should again do nothing.
    extension_sync_service()->ProcessSyncChanges(FROM_HERE, list);
    EXPECT_TRUE(registry()->enabled_extensions().GetByID(good_crx));
  }
}

TEST_F(ExtensionServiceSyncTest, ProcessSyncDataSettings) {
  InitializeEmptyExtensionService();
  extension_sync_service()->MergeDataAndStartSyncing(
      syncer::EXTENSIONS, syncer::SyncDataList(),
      std::make_unique<syncer::FakeSyncChangeProcessor>(),
      std::make_unique<syncer::SyncErrorFactoryMock>());

  InstallCRX(data_dir().AppendASCII("good.crx"), INSTALL_NEW);
  EXPECT_TRUE(service()->IsExtensionEnabled(good_crx));
  EXPECT_FALSE(extensions::util::IsIncognitoEnabled(good_crx, profile()));

  sync_pb::EntitySpecifics specifics;
  sync_pb::ExtensionSpecifics* ext_specifics = specifics.mutable_extension();
  ext_specifics->set_id(good_crx);
  ext_specifics->set_version(
      registry()->GetInstalledExtension(good_crx)->version().GetString());
  ext_specifics->set_enabled(false);

  {
    SyncChangeList list =
        MakeSyncChangeList(good_crx, specifics, SyncChange::ACTION_UPDATE);

    extension_sync_service()->ProcessSyncChanges(FROM_HERE, list);
    EXPECT_FALSE(service()->IsExtensionEnabled(good_crx));
    EXPECT_FALSE(extensions::util::IsIncognitoEnabled(good_crx, profile()));
  }

  {
    ext_specifics->set_enabled(true);
    ext_specifics->set_incognito_enabled(true);

    SyncChangeList list =
        MakeSyncChangeList(good_crx, specifics, SyncChange::ACTION_UPDATE);

    extension_sync_service()->ProcessSyncChanges(FROM_HERE, list);
    EXPECT_TRUE(service()->IsExtensionEnabled(good_crx));
    EXPECT_TRUE(extensions::util::IsIncognitoEnabled(good_crx, profile()));
  }

  {
    ext_specifics->set_enabled(false);
    ext_specifics->set_incognito_enabled(true);

    SyncChangeList list =
        MakeSyncChangeList(good_crx, specifics, SyncChange::ACTION_UPDATE);

    extension_sync_service()->ProcessSyncChanges(FROM_HERE, list);
    EXPECT_FALSE(service()->IsExtensionEnabled(good_crx));
    EXPECT_TRUE(extensions::util::IsIncognitoEnabled(good_crx, profile()));
  }

  {
    ext_specifics->set_enabled(true);

    SyncChangeList list =
        MakeSyncChangeList(good_crx, specifics, SyncChange::ACTION_UPDATE);

    extension_sync_service()->ProcessSyncChanges(FROM_HERE, list);
    EXPECT_TRUE(service()->IsExtensionEnabled(good_crx));
  }

  EXPECT_FALSE(service()->pending_extension_manager()->IsIdPending(good_crx));
}

TEST_F(ExtensionServiceSyncTest, ProcessSyncDataNewExtension) {
  InitializeEmptyExtensionService();
  extension_sync_service()->MergeDataAndStartSyncing(
      syncer::EXTENSIONS, syncer::SyncDataList(),
      std::make_unique<syncer::FakeSyncChangeProcessor>(),
      std::make_unique<syncer::SyncErrorFactoryMock>());

  const base::FilePath path = data_dir().AppendASCII("good.crx");
  const ExtensionPrefs* prefs = ExtensionPrefs::Get(profile());

  struct TestCase {
    const char* name;  // For failure output only.
    bool sync_enabled;  // The "enabled" flag coming in from Sync.
    // The disable reason(s) coming in from Sync, or -1 for "not set".
    int sync_disable_reasons;
    // The disable reason(s) that should be set on the installed extension.
    // This will usually be the same as |sync_disable_reasons|, but see the
    // "Legacy" case.
    int expect_disable_reasons;
    // Whether the extension's permissions should be auto-granted during
    // installation.
    bool expect_permissions_granted;
  } test_cases[] = {
      // Standard case: Extension comes in enabled; permissions should be
      // granted
      // during installation.
      {"Standard", true, 0, 0, true},
      // If the extension comes in disabled, its permissions should still be
      // granted (the user already approved them on another machine).
      {"Disabled", false, extensions::disable_reason::DISABLE_USER_ACTION,
       extensions::disable_reason::DISABLE_USER_ACTION, true},
      // Legacy case (<M45): No disable reasons come in from Sync (see
      // crbug.com/484214). After installation, the reason should be set to
      // DISABLE_USER_ACTION (default assumption).
      {"Legacy", false, -1, extensions::disable_reason::DISABLE_USER_ACTION,
       true},
      // If the extension came in disabled due to a permissions increase, then
      // the
      // user has *not* approved the permissions, and they shouldn't be granted.
      // crbug.com/484214
      {"PermissionsIncrease", false,
       extensions::disable_reason::DISABLE_PERMISSIONS_INCREASE,
       extensions::disable_reason::DISABLE_PERMISSIONS_INCREASE, false},
  };

  for (const TestCase& test_case : test_cases) {
    SCOPED_TRACE(test_case.name);

    sync_pb::EntitySpecifics specifics;
    sync_pb::ExtensionSpecifics* ext_specifics = specifics.mutable_extension();
    ext_specifics->set_id(good_crx);
    ext_specifics->set_version(base::Version("1").GetString());
    ext_specifics->set_enabled(test_case.sync_enabled);
    if (test_case.sync_disable_reasons != -1)
      ext_specifics->set_disable_reasons(test_case.sync_disable_reasons);

    SyncChangeList list =
        MakeSyncChangeList(good_crx, specifics, SyncChange::ACTION_UPDATE);

    extension_sync_service()->ProcessSyncChanges(FROM_HERE, list);

    ASSERT_TRUE(service()->pending_extension_manager()->IsIdPending(good_crx));
    UpdateExtension(good_crx, path, test_case.sync_enabled ? ENABLED
                                                           : DISABLED);
    EXPECT_EQ(test_case.expect_disable_reasons,
              prefs->GetDisableReasons(good_crx));
    std::unique_ptr<const PermissionSet> permissions =
        prefs->GetGrantedPermissions(good_crx);
    EXPECT_EQ(test_case.expect_permissions_granted, !permissions->IsEmpty());
    ASSERT_FALSE(service()->pending_extension_manager()->IsIdPending(good_crx));
    if (test_case.sync_enabled)
      EXPECT_TRUE(registry()->enabled_extensions().GetByID(good_crx));
    else
      EXPECT_TRUE(registry()->disabled_extensions().GetByID(good_crx));

    // Remove the extension again, so we can install it again for the next case.
    UninstallExtension(good_crx);
  }
}

TEST_F(ExtensionServiceSyncTest, ProcessSyncDataTerminatedExtension) {
  InitializeExtensionServiceWithUpdater();
  extension_sync_service()->MergeDataAndStartSyncing(
      syncer::EXTENSIONS, syncer::SyncDataList(),
      std::make_unique<syncer::FakeSyncChangeProcessor>(),
      std::make_unique<syncer::SyncErrorFactoryMock>());

  InstallCRX(data_dir().AppendASCII("good.crx"), INSTALL_NEW);
  TerminateExtension(good_crx);
  EXPECT_TRUE(service()->IsExtensionEnabled(good_crx));
  EXPECT_FALSE(extensions::util::IsIncognitoEnabled(good_crx, profile()));

  sync_pb::EntitySpecifics specifics;
  sync_pb::ExtensionSpecifics* ext_specifics = specifics.mutable_extension();
  ext_specifics->set_id(good_crx);
  ext_specifics->set_version(
      registry()->GetInstalledExtension(good_crx)->version().GetString());
  ext_specifics->set_enabled(false);
  ext_specifics->set_incognito_enabled(true);

  SyncChangeList list =
      MakeSyncChangeList(good_crx, specifics, SyncChange::ACTION_UPDATE);

  extension_sync_service()->ProcessSyncChanges(FROM_HERE, list);
  EXPECT_FALSE(service()->IsExtensionEnabled(good_crx));
  EXPECT_TRUE(extensions::util::IsIncognitoEnabled(good_crx, profile()));

  EXPECT_FALSE(service()->pending_extension_manager()->IsIdPending(good_crx));
}

TEST_F(ExtensionServiceSyncTest, ProcessSyncDataVersionCheck) {
  InitializeExtensionServiceWithUpdater();
  extension_sync_service()->MergeDataAndStartSyncing(
      syncer::EXTENSIONS, syncer::SyncDataList(),
      std::make_unique<syncer::FakeSyncChangeProcessor>(),
      std::make_unique<syncer::SyncErrorFactoryMock>());

  InstallCRX(data_dir().AppendASCII("good.crx"), INSTALL_NEW);
  EXPECT_TRUE(service()->IsExtensionEnabled(good_crx));
  EXPECT_FALSE(extensions::util::IsIncognitoEnabled(good_crx, profile()));

  sync_pb::EntitySpecifics specifics;
  sync_pb::ExtensionSpecifics* ext_specifics = specifics.mutable_extension();
  ext_specifics->set_id(good_crx);
  ext_specifics->set_enabled(true);

  const base::Version installed_version =
      registry()->GetInstalledExtension(good_crx)->version();

  {
    ext_specifics->set_version(installed_version.GetString());

    SyncChangeList list =
        MakeSyncChangeList(good_crx, specifics, SyncChange::ACTION_UPDATE);

    // Should do nothing if extension version == sync version.
    extension_sync_service()->ProcessSyncChanges(FROM_HERE, list);
    EXPECT_FALSE(service()->updater()->WillCheckSoon());
    // Make sure the version we'll send back to sync didn't change.
    syncer::SyncDataList data =
        extension_sync_service()->GetAllSyncDataForTesting(syncer::EXTENSIONS);
    ASSERT_EQ(1u, data.size());
    std::unique_ptr<ExtensionSyncData> extension_data =
        ExtensionSyncData::CreateFromSyncData(data[0]);
    ASSERT_TRUE(extension_data);
    EXPECT_EQ(installed_version, extension_data->version());
  }

  // Should do nothing if extension version > sync version.
  {
    ext_specifics->set_version("0.0.0.0");

    SyncChangeList list =
        MakeSyncChangeList(good_crx, specifics, SyncChange::ACTION_UPDATE);

    extension_sync_service()->ProcessSyncChanges(FROM_HERE, list);
    EXPECT_FALSE(service()->updater()->WillCheckSoon());
    // Make sure the version we'll send back to sync didn't change.
    syncer::SyncDataList data =
        extension_sync_service()->GetAllSyncDataForTesting(syncer::EXTENSIONS);
    ASSERT_EQ(1u, data.size());
    std::unique_ptr<ExtensionSyncData> extension_data =
        ExtensionSyncData::CreateFromSyncData(data[0]);
    ASSERT_TRUE(extension_data);
    EXPECT_EQ(installed_version, extension_data->version());
  }

  // Should kick off an update if extension version < sync version.
  {
    const base::Version new_version("9.9.9.9");
    ext_specifics->set_version(new_version.GetString());

    SyncChangeList list =
        MakeSyncChangeList(good_crx, specifics, SyncChange::ACTION_UPDATE);

    extension_sync_service()->ProcessSyncChanges(FROM_HERE, list);
    EXPECT_TRUE(service()->updater()->WillCheckSoon());
    // Make sure that we'll send the NEW version back to sync, even though we
    // haven't actually updated yet. This is to prevent the data in sync from
    // flip-flopping back and forth until all clients are up to date.
    syncer::SyncDataList data =
        extension_sync_service()->GetAllSyncDataForTesting(syncer::EXTENSIONS);
    ASSERT_EQ(1u, data.size());
    std::unique_ptr<ExtensionSyncData> extension_data =
        ExtensionSyncData::CreateFromSyncData(data[0]);
    ASSERT_TRUE(extension_data);
    EXPECT_EQ(new_version, extension_data->version());
  }

  EXPECT_FALSE(service()->pending_extension_manager()->IsIdPending(good_crx));
}

TEST_F(ExtensionServiceSyncTest, ProcessSyncDataNotInstalled) {
  InitializeExtensionServiceWithUpdater();
  extension_sync_service()->MergeDataAndStartSyncing(
      syncer::EXTENSIONS, syncer::SyncDataList(),
      std::make_unique<syncer::FakeSyncChangeProcessor>(),
      std::make_unique<syncer::SyncErrorFactoryMock>());

  sync_pb::EntitySpecifics specifics;
  sync_pb::ExtensionSpecifics* ext_specifics = specifics.mutable_extension();
  ext_specifics->set_id(good_crx);
  ext_specifics->set_enabled(false);
  ext_specifics->set_incognito_enabled(true);
  ext_specifics->set_update_url("http://www.google.com/");
  ext_specifics->set_version("1.2.3.4");

  SyncChangeList list =
      MakeSyncChangeList(good_crx, specifics, SyncChange::ACTION_UPDATE);

  EXPECT_TRUE(service()->IsExtensionEnabled(good_crx));
  EXPECT_FALSE(extensions::util::IsIncognitoEnabled(good_crx, profile()));
  extension_sync_service()->ProcessSyncChanges(FROM_HERE, list);
  EXPECT_TRUE(service()->updater()->WillCheckSoon());
  EXPECT_FALSE(service()->IsExtensionEnabled(good_crx));
  EXPECT_TRUE(extensions::util::IsIncognitoEnabled(good_crx, profile()));

  const extensions::PendingExtensionInfo* info;
  EXPECT_TRUE(
      (info = service()->pending_extension_manager()->GetById(good_crx)));
  EXPECT_EQ(ext_specifics->update_url(), info->update_url().spec());
  EXPECT_TRUE(info->is_from_sync());
  EXPECT_EQ(ManifestLocation::kInternal, info->install_source());
  // TODO(akalin): Figure out a way to test |info.ShouldAllowInstall()|.
}

TEST_F(ExtensionServiceSyncTest, ProcessSyncDataEnableDisable) {
  InitializeEmptyExtensionService();
  extension_sync_service()->MergeDataAndStartSyncing(
      syncer::EXTENSIONS, syncer::SyncDataList(),
      std::make_unique<syncer::FakeSyncChangeProcessor>(),
      std::make_unique<syncer::SyncErrorFactoryMock>());

  const ExtensionPrefs* prefs = ExtensionPrefs::Get(profile());

  struct TestCase {
    const char* name;  // For failure output only.
    // Set of disable reasons before any Sync data comes in. If this is != 0,
    // the extension is disabled.
    int previous_disable_reasons;
    bool sync_enable;  // The enabled flag coming in from Sync.
    // The disable reason(s) coming in from Sync, or -1 for "not set".
    int sync_disable_reasons;
    // The expected set of disable reasons after processing the Sync update. The
    // extension should be disabled iff this is != 0.
    int expect_disable_reasons;
  } test_cases[] = {
      {"NopEnable", 0, true, 0, 0},
      {"NopDisable", extensions::disable_reason::DISABLE_USER_ACTION, false,
       extensions::disable_reason::DISABLE_USER_ACTION,
       extensions::disable_reason::DISABLE_USER_ACTION},
      {"Enable", extensions::disable_reason::DISABLE_USER_ACTION, true, 0, 0},
      {"Disable", 0, false, extensions::disable_reason::DISABLE_USER_ACTION,
       extensions::disable_reason::DISABLE_USER_ACTION},
      {"AddDisableReason", extensions::disable_reason::DISABLE_REMOTE_INSTALL,
       false,
       extensions::disable_reason::DISABLE_REMOTE_INSTALL |
           extensions::disable_reason::DISABLE_USER_ACTION,
       extensions::disable_reason::DISABLE_REMOTE_INSTALL |
           extensions::disable_reason::DISABLE_USER_ACTION},
      {"RemoveDisableReason",
       extensions::disable_reason::DISABLE_REMOTE_INSTALL |
           extensions::disable_reason::DISABLE_USER_ACTION,
       false, extensions::disable_reason::DISABLE_USER_ACTION,
       extensions::disable_reason::DISABLE_USER_ACTION},
      {"PreserveLocalDisableReason", extensions::disable_reason::DISABLE_RELOAD,
       true, 0, extensions::disable_reason::DISABLE_RELOAD},
      {"PreserveOnlyLocalDisableReason",
       extensions::disable_reason::DISABLE_USER_ACTION |
           extensions::disable_reason::DISABLE_RELOAD,
       true, 0, extensions::disable_reason::DISABLE_RELOAD},

      // Interaction with Chrome clients <=M44, which don't sync disable_reasons
      // at all (any existing reasons are preserved).
      {"M44Enable", extensions::disable_reason::DISABLE_USER_ACTION, true, -1,
       0},
      // An M44 client enables an extension that had been disabled on a new
      // client. The disable reasons are still be there, but should be ignored.
      {"M44ReEnable", extensions::disable_reason::DISABLE_USER_ACTION, true,
       extensions::disable_reason::DISABLE_USER_ACTION, 0},
      {"M44Disable", 0, false, -1,
       extensions::disable_reason::DISABLE_USER_ACTION},
      {"M44ReDisable", 0, false, 0,
       extensions::disable_reason::DISABLE_USER_ACTION},
      {"M44AlreadyDisabledByUser",
       extensions::disable_reason::DISABLE_USER_ACTION, false, -1,
       extensions::disable_reason::DISABLE_USER_ACTION},
      {"M44AlreadyDisabledWithOtherReason",
       extensions::disable_reason::DISABLE_REMOTE_INSTALL, false, -1,
       extensions::disable_reason::DISABLE_REMOTE_INSTALL |
           extensions::disable_reason::DISABLE_USER_ACTION},
  };

  for (const TestCase& test_case : test_cases) {
    SCOPED_TRACE(test_case.name);

    std::string id;
    std::string version;
    // Don't keep |extension| around longer than necessary.
    {
      const Extension* extension =
          InstallCRX(data_dir().AppendASCII("good.crx"), INSTALL_NEW);
      // The extension should now be installed and enabled.
      ASSERT_TRUE(extension);
      id = extension->id();
      version = extension->VersionString();
    }
    ASSERT_TRUE(registry()->enabled_extensions().Contains(id));

    // Disable it if the test case says so.
    if (test_case.previous_disable_reasons) {
      service()->DisableExtension(id, test_case.previous_disable_reasons);
      ASSERT_TRUE(registry()->disabled_extensions().Contains(id));
    }

    // Now a sync update comes in.
    sync_pb::EntitySpecifics specifics;
    sync_pb::ExtensionSpecifics* ext_specifics = specifics.mutable_extension();
    ext_specifics->set_id(id);
    ext_specifics->set_enabled(test_case.sync_enable);
    ext_specifics->set_version(version);
    if (test_case.sync_disable_reasons != -1)
      ext_specifics->set_disable_reasons(test_case.sync_disable_reasons);

    SyncChangeList list =
        MakeSyncChangeList(good_crx, specifics, SyncChange::ACTION_UPDATE);

    extension_sync_service()->ProcessSyncChanges(FROM_HERE, list);

    // Check expectations.
    const bool expect_enabled = !test_case.expect_disable_reasons;
    EXPECT_EQ(expect_enabled, service()->IsExtensionEnabled(id));
    EXPECT_EQ(test_case.expect_disable_reasons, prefs->GetDisableReasons(id));

    // Remove the extension again, so we can install it again for the next case.
    UninstallExtension(id);
  }
}

class ExtensionServiceSyncCustomGalleryTest : public ExtensionServiceSyncTest {
 public:
  void SetUp() override {
    ExtensionServiceSyncTest::SetUp();

    // This is the update URL specified in the permissions test extension.
    // Setting it here is necessary to make the extension considered syncable.
    extension_test_util::SetGalleryUpdateURL(
        GURL("http://localhost/autoupdate/updates.xml"));
  }
};

TEST_F(ExtensionServiceSyncCustomGalleryTest, ProcessSyncDataDeferredEnable) {
  InitializeEmptyExtensionService();
  extension_sync_service()->MergeDataAndStartSyncing(
      syncer::EXTENSIONS, syncer::SyncDataList(),
      std::make_unique<syncer::FakeSyncChangeProcessor>(),
      std::make_unique<syncer::SyncErrorFactoryMock>());

  base::FilePath base_path = data_dir().AppendASCII("permissions_increase");
  base::FilePath pem_path = base_path.AppendASCII("permissions.pem");

  base::FilePath path = base_path.AppendASCII("v1");
  const Extension* extension = PackAndInstallCRX(path, pem_path, INSTALL_NEW);
  // The extension must now be installed and enabled.
  ASSERT_TRUE(extension);
  ASSERT_TRUE(registry()->enabled_extensions().Contains(extension->id()));

  // Save the id, as the extension object will be destroyed during updating.
  std::string id = extension->id();

  // Update to a new version with increased permissions.
  path = base_path.AppendASCII("v2");
  PackCRXAndUpdateExtension(id, path, pem_path, DISABLED);

  // Now a sync update comes in, telling us to re-enable a *newer* version.
  sync_pb::EntitySpecifics specifics;
  sync_pb::ExtensionSpecifics* ext_specifics = specifics.mutable_extension();
  ext_specifics->set_id(id);
  ext_specifics->set_version("3");
  ext_specifics->set_enabled(true);
  ext_specifics->set_disable_reasons(extensions::disable_reason::DISABLE_NONE);

  SyncChangeList list =
      MakeSyncChangeList(good_crx, specifics, SyncChange::ACTION_UPDATE);

  extension_sync_service()->ProcessSyncChanges(FROM_HERE, list);

  // Since the version didn't match, the extension should still be disabled.
  EXPECT_TRUE(registry()->disabled_extensions().Contains(id));

  // After we update to the matching version, the extension should get enabled.
  path = base_path.AppendASCII("v3");
  PackCRXAndUpdateExtension(id, path, pem_path, ENABLED);
}

TEST_F(ExtensionServiceSyncCustomGalleryTest,
       ProcessSyncDataPermissionApproval) {
  InitializeEmptyExtensionService();
  extension_sync_service()->MergeDataAndStartSyncing(
      syncer::EXTENSIONS, syncer::SyncDataList(),
      std::make_unique<syncer::FakeSyncChangeProcessor>(),
      std::make_unique<syncer::SyncErrorFactoryMock>());

  const base::FilePath base_path =
      data_dir().AppendASCII("permissions_increase");
  const base::FilePath pem_path = base_path.AppendASCII("permissions.pem");
  const base::FilePath path_v1 = base_path.AppendASCII("v1");
  const base::FilePath path_v2 = base_path.AppendASCII("v2");

  base::ScopedTempDir crx_dir;
  ASSERT_TRUE(crx_dir.CreateUniqueTempDir());
  const base::FilePath crx_path_v1 = crx_dir.GetPath().AppendASCII("temp1.crx");
  PackCRX(path_v1, pem_path, crx_path_v1);
  const base::FilePath crx_path_v2 = crx_dir.GetPath().AppendASCII("temp2.crx");
  PackCRX(path_v2, pem_path, crx_path_v2);

  const std::string v1("1");
  const std::string v2("2");

  const ExtensionPrefs* prefs = ExtensionPrefs::Get(profile());

  struct TestCase {
    const char* name;  // For failure output only.
    const std::string& sync_version;  // The version coming in from Sync.
    // The disable reason(s) coming in from Sync, or -1 for "not set".
    int sync_disable_reasons;
    // The expected set of disable reasons after processing the Sync update. The
    // extension should be enabled iff this is 0.
    int expect_disable_reasons;
    // Whether the extension's permissions should be auto-granted.
    bool expect_permissions_granted;
  } test_cases[] = {
      // Sync tells us to re-enable an older version. No permissions should be
      // granted, since we can't be sure if the user actually approved the right
      // set of permissions.
      {"OldVersion", v1, 0,
       extensions::disable_reason::DISABLE_PERMISSIONS_INCREASE, false},
      // Legacy case: Sync tells us to re-enable the extension, but doesn't
      // specify disable reasons. No permissions should be granted.
      {"Legacy", v2, -1,
       extensions::disable_reason::DISABLE_PERMISSIONS_INCREASE, false},
      // Sync tells us to re-enable the extension and explicitly removes the
      // disable reasons. Now the extension should have its permissions granted.
      {"GrantPermissions", v2, 0, extensions::disable_reason::DISABLE_NONE,
       true},
  };

  for (const TestCase& test_case : test_cases) {
    SCOPED_TRACE(test_case.name);

    std::string id;
    // Don't keep |extension| around longer than necessary (it'll be destroyed
    // during updating).
    {
      const Extension* extension = InstallCRX(crx_path_v1, INSTALL_NEW);
      // The extension should now be installed and enabled.
      ASSERT_TRUE(extension);
      ASSERT_EQ(v1, extension->VersionString());
      id = extension->id();
    }
    ASSERT_TRUE(registry()->enabled_extensions().Contains(id));

    std::unique_ptr<const PermissionSet> granted_permissions_v1 =
        prefs->GetGrantedPermissions(id);

    // Update to a new version with increased permissions.
    UpdateExtension(id, crx_path_v2, DISABLED);

    // Now the extension should be disabled due to a permissions increase.
    {
      const Extension* extension =
          registry()->disabled_extensions().GetByID(id);
      ASSERT_TRUE(extension);
      ASSERT_EQ(v2, extension->VersionString());
    }
    ASSERT_TRUE(prefs->HasDisableReason(
        id, extensions::disable_reason::DISABLE_PERMISSIONS_INCREASE));

    // No new permissions should have been granted.
    std::unique_ptr<const PermissionSet> granted_permissions_v2 =
        prefs->GetGrantedPermissions(id);
    ASSERT_EQ(*granted_permissions_v1, *granted_permissions_v2);

    // Now a sync update comes in.
    sync_pb::EntitySpecifics specifics;
    sync_pb::ExtensionSpecifics* ext_specifics = specifics.mutable_extension();
    ext_specifics->set_id(id);
    ext_specifics->set_enabled(true);
    ext_specifics->set_version(test_case.sync_version);
    if (test_case.sync_disable_reasons != -1)
      ext_specifics->set_disable_reasons(test_case.sync_disable_reasons);

    SyncChangeList list =
        MakeSyncChangeList(good_crx, specifics, SyncChange::ACTION_UPDATE);

    extension_sync_service()->ProcessSyncChanges(FROM_HERE, list);

    // Check expectations.
    const bool expect_enabled = !test_case.expect_disable_reasons;
    EXPECT_EQ(expect_enabled, service()->IsExtensionEnabled(id));
    EXPECT_EQ(test_case.expect_disable_reasons, prefs->GetDisableReasons(id));
    std::unique_ptr<const PermissionSet> granted_permissions =
        prefs->GetGrantedPermissions(id);
    if (test_case.expect_permissions_granted) {
      std::unique_ptr<const PermissionSet> active_permissions =
          prefs->GetDesiredActivePermissions(id);
      EXPECT_EQ(*granted_permissions, *active_permissions);
    } else {
      EXPECT_EQ(*granted_permissions, *granted_permissions_v1);
    }

    // Remove the extension again, so we can install it again for the next case.
    UninstallExtension(id);
  }
}

// Regression test for crbug.com/558299
TEST_F(ExtensionServiceSyncTest, DontSyncThemes) {
  InitializeEmptyExtensionService();

  // The user has enabled sync.
  SyncServiceFactory::GetForProfile(profile())
      ->GetUserSettings()
      ->SetFirstSetupComplete(kSetSourceFromTest);
  // Make sure ExtensionSyncService is created, so it'll be notified of changes.
  extension_sync_service();

  service()->Init();
  ASSERT_TRUE(extension_system()->is_ready());

  syncer::FakeSyncChangeProcessor* processor =
      new syncer::FakeSyncChangeProcessor;
  extension_sync_service()->MergeDataAndStartSyncing(
      syncer::EXTENSIONS, syncer::SyncDataList(), base::WrapUnique(processor),
      base::WrapUnique(new syncer::SyncErrorFactoryMock));

  processor->changes().clear();

  // Sanity check: Installing an extension should result in a sync change.
  InstallCRX(data_dir().AppendASCII("good.crx"), INSTALL_NEW);
  EXPECT_EQ(1u, processor->changes().size());

  processor->changes().clear();

  // Installing a theme should not result in a sync change (themes are handled
  // separately by ThemeSyncableService).
  test::ThemeServiceChangedWaiter waiter(
      ThemeServiceFactory::GetForProfile(profile()));
  InstallCRX(data_dir().AppendASCII("theme.crx"), INSTALL_NEW);
  waiter.WaitForThemeChanged();
  EXPECT_TRUE(processor->changes().empty());
}

// Tests sync behavior in the case of an item that starts out as an app and gets
// updated to become an extension.
TEST_F(ExtensionServiceSyncTest, AppToExtension) {
  InitializeEmptyExtensionService();
  service()->Init();
  ASSERT_TRUE(extension_system()->is_ready());

  // Install v1, which is an app.
  const Extension* v1 =
      InstallCRX(data_dir().AppendASCII("sync_datatypes").AppendASCII("v1.crx"),
                 INSTALL_NEW);
  EXPECT_TRUE(v1->is_app());
  EXPECT_FALSE(v1->is_extension());
  std::string id = v1->id();

  StatefulChangeProcessor extensions_processor(syncer::ModelType::EXTENSIONS);
  StatefulChangeProcessor apps_processor(syncer::ModelType::APPS);
  extension_sync_service()->MergeDataAndStartSyncing(
      syncer::EXTENSIONS, syncer::SyncDataList(),
      extensions_processor.GetWrapped(),
      base::WrapUnique(new syncer::SyncErrorFactoryMock()));
  extension_sync_service()->MergeDataAndStartSyncing(
      syncer::APPS, syncer::SyncDataList(), apps_processor.GetWrapped(),
      base::WrapUnique(new syncer::SyncErrorFactoryMock()));

  // Check the app/extension change processors to be sure the right data was
  // added.
  EXPECT_TRUE(extensions_processor.changes().empty());
  EXPECT_TRUE(extensions_processor.data().empty());
  EXPECT_EQ(1u, apps_processor.data().size());
  ASSERT_EQ(1u, apps_processor.changes().size());
  const SyncChange& app_change = apps_processor.changes()[0];
  EXPECT_EQ(SyncChange::ACTION_ADD, app_change.change_type());
  std::unique_ptr<ExtensionSyncData> app_data =
      ExtensionSyncData::CreateFromSyncData(app_change.sync_data());
  EXPECT_TRUE(app_data->is_app());
  EXPECT_EQ(id, app_data->id());
  EXPECT_EQ(v1->version(), app_data->version());

  // Update the app to v2, which is an extension.
  const Extension* v2 =
      InstallCRX(data_dir().AppendASCII("sync_datatypes").AppendASCII("v2.crx"),
                 INSTALL_UPDATED);
  EXPECT_FALSE(v2->is_app());
  EXPECT_TRUE(v2->is_extension());
  EXPECT_EQ(id, v2->id());

  // Make sure we saw an extension item added.
  ASSERT_EQ(1u, extensions_processor.changes().size());
  const SyncChange& extension_change = extensions_processor.changes()[0];
  EXPECT_EQ(SyncChange::ACTION_ADD, extension_change.change_type());
  std::unique_ptr<ExtensionSyncData> extension_data =
      ExtensionSyncData::CreateFromSyncData(extension_change.sync_data());
  EXPECT_FALSE(extension_data->is_app());
  EXPECT_EQ(id, extension_data->id());
  EXPECT_EQ(v2->version(), extension_data->version());

  // Get the current data from the change processors to use as the input to
  // the following call to MergeDataAndStartSyncing. This simulates what should
  // happen with sync.
  syncer::SyncDataList extensions_data = extensions_processor.data();
  syncer::SyncDataList apps_data = apps_processor.data();

  // Stop syncing, then start again.
  extension_sync_service()->StopSyncing(syncer::EXTENSIONS);
  extension_sync_service()->StopSyncing(syncer::APPS);
  extension_sync_service()->MergeDataAndStartSyncing(
      syncer::EXTENSIONS, extensions_data, extensions_processor.GetWrapped(),
      base::WrapUnique(new syncer::SyncErrorFactoryMock()));
  extension_sync_service()->MergeDataAndStartSyncing(
      syncer::APPS, apps_data, apps_processor.GetWrapped(),
      base::WrapUnique(new syncer::SyncErrorFactoryMock()));

  // Make sure we saw an app item deleted.
  bool found_delete = false;
  for (const auto& change : apps_processor.changes()) {
    if (change.change_type() == SyncChange::ACTION_DELETE) {
      std::unique_ptr<ExtensionSyncData> data =
          ExtensionSyncData::CreateFromSyncChange(change);
      if (data->id() == id) {
        found_delete = true;
        break;
      }
    }
  }
  EXPECT_TRUE(found_delete);

  // Make sure there is one extension, and there are no more apps.
  EXPECT_EQ(1u, extensions_processor.data().size());
  EXPECT_TRUE(apps_processor.data().empty());
}

class BlocklistedExtensionSyncServiceTest : public ExtensionServiceSyncTest {
 public:
  BlocklistedExtensionSyncServiceTest() {}

  BlocklistedExtensionSyncServiceTest(
      const BlocklistedExtensionSyncServiceTest&) = delete;
  BlocklistedExtensionSyncServiceTest& operator=(
      const BlocklistedExtensionSyncServiceTest&) = delete;

  void SetUp() override {
    ExtensionServiceSyncTest::SetUp();

    InitializeEmptyExtensionService();

    // Enable sync.
    syncer::SyncService* sync_service =
        SyncServiceFactory::GetForProfile(profile());
    sync_service->GetUserSettings()->SetFirstSetupComplete(kSetSourceFromTest);

    test_blocklist_.Attach(service()->blocklist_);
    service()->Init();

    // Load up a simple extension.
    extensions::ChromeTestExtensionLoader extension_loader(profile());
    extension_loader.set_pack_extension(true);
    extension_ = extension_loader.LoadExtension(
        data_dir().AppendASCII("simple_with_file"));
    ASSERT_TRUE(extension_);
    extension_id_ = extension_->id();
    ASSERT_TRUE(registry()->enabled_extensions().GetByID(extension_id_));

    {
      auto processor = std::make_unique<syncer::FakeSyncChangeProcessor>();
      processor_raw_ = processor.get();
      extension_sync_service()->MergeDataAndStartSyncing(
          syncer::EXTENSIONS, syncer::SyncDataList(), std::move(processor),
          std::make_unique<syncer::SyncErrorFactoryMock>());
    }
    processor_raw_->changes().clear();
  }

  void ForceBlocklistUpdate() {
    service()->OnBlocklistUpdated();
    content::RunAllTasksUntilIdle();
  }

  syncer::FakeSyncChangeProcessor* processor() { return processor_raw_; }

  const Extension* extension() { return extension_.get(); }

  std::string& extension_id() { return extension_id_; }

  extensions::TestBlocklist& test_blocklist() { return test_blocklist_; }

 private:
  raw_ptr<syncer::FakeSyncChangeProcessor> processor_raw_;
  scoped_refptr<const Extension> extension_;
  std::string extension_id_;
  extensions::TestBlocklist test_blocklist_;
};

// Test that sync cannot enable blocklisted extensions.
TEST_F(BlocklistedExtensionSyncServiceTest, SyncBlocklistedExtension) {
  std::string& extension_id = this->extension_id();

  // Blocklist the extension.
  test_blocklist().SetBlocklistState(extension_id,
                                     extensions::BLOCKLISTED_MALWARE, true);
  ForceBlocklistUpdate();

  // Try enabling the extension via sync.
  EnableExtensionFromSync(*extension());

  // The extension should not be enabled.
  EXPECT_FALSE(registry()->enabled_extensions().GetByID(extension_id));
  EXPECT_TRUE(processor()->changes().empty());
}

// Test that some greylisted extensions can be enabled through sync.
TEST_F(BlocklistedExtensionSyncServiceTest, SyncAllowedGreylistedExtension) {
  std::string& extension_id = this->extension_id();

  // Greylist the extension.
  test_blocklist().SetBlocklistState(
      extension_id, extensions::BLOCKLISTED_POTENTIALLY_UNWANTED, true);
  ForceBlocklistUpdate();

  EXPECT_FALSE(registry()->enabled_extensions().GetByID(extension_id));
  {
    ASSERT_EQ(1u, processor()->changes().size());
    const SyncChange& change = processor()->changes()[0];
    EXPECT_EQ(SyncChange::ACTION_UPDATE, change.change_type());
    std::unique_ptr<ExtensionSyncData> data =
        ExtensionSyncData::CreateFromSyncData(change.sync_data());
    EXPECT_EQ(extension_id, data->id());
    EXPECT_EQ(extensions::disable_reason::DISABLE_GREYLIST,
              data->disable_reasons());
    EXPECT_FALSE(data->enabled());
  }
  processor()->changes().clear();

  // Manually re-enabling the extension should work.
  service()->EnableExtension(extension_id);
  EXPECT_TRUE(registry()->enabled_extensions().GetByID(extension_id));
  {
    ASSERT_EQ(1u, processor()->changes().size());
    const SyncChange& change = processor()->changes()[0];
    EXPECT_EQ(SyncChange::ACTION_UPDATE, change.change_type());
    std::unique_ptr<ExtensionSyncData> data =
        ExtensionSyncData::CreateFromSyncData(change.sync_data());
    EXPECT_EQ(extension_id, data->id());
    EXPECT_EQ(0, data->disable_reasons());
    EXPECT_TRUE(data->enabled());
  }
  processor()->changes().clear();
}
