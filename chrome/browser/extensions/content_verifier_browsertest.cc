// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <list>
#include <memory>
#include <set>
#include <string>

#include "base/callback_helpers.h"
#include "base/files/file_util.h"
#include "base/strings/string_split.h"
#include "base/strings/stringprintf.h"
#include "base/test/scoped_feature_list.h"
#include "base/test/task_environment.h"
#include "base/test/test_file_util.h"
#include "base/threading/thread_restrictions.h"
#include "base/threading/thread_task_runner_handle.h"
#include "build/build_config.h"
#include "chrome/browser/extensions/browsertest_util.h"
#include "chrome/browser/extensions/chrome_content_verifier_delegate.h"
#include "chrome/browser/extensions/content_verifier_test_utils.h"
#include "chrome/browser/extensions/corrupted_extension_reinstaller.h"
#include "chrome/browser/extensions/crx_installer.h"
#include "chrome/browser/extensions/devtools_util.h"
#include "chrome/browser/extensions/extension_browsertest.h"
#include "chrome/browser/extensions/extension_management_test_util.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/crx_file/id_util.h"
#include "components/policy/core/browser/browser_policy_connector.h"
#include "components/policy/core/common/mock_configuration_policy_provider.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/test_utils.h"
#include "extensions/browser/content_verifier.h"
#include "extensions/browser/content_verifier/test_utils.h"
#include "extensions/browser/content_verify_job.h"
#include "extensions/browser/crx_file_info.h"
#include "extensions/browser/extension_prefs.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/external_install_info.h"
#include "extensions/browser/management_policy.h"
#include "extensions/browser/mock_external_provider.h"
#include "extensions/browser/test_extension_registry_observer.h"
#include "extensions/browser/updater/extension_update_data.h"
#include "extensions/browser/updater/manifest_fetch_data.h"
#include "extensions/common/extension_features.h"
#include "extensions/common/extension_urls.h"
#include "extensions/common/file_util.h"
#include "third_party/zlib/google/compression_utils.h"

using extensions::mojom::ManifestLocation;

namespace extensions {

namespace {
constexpr char kTenMegResourceExtensionId[] =
    "mibjhafkjlepkpbjleahhallgddpjgle";
constexpr char kStoragePermissionExtensionId[] =
    "dmabdbcjhngdcmkfmgiogpcpiniaoddk";
constexpr char kStoragePermissionExtensionCrx[] =
    "content_verifier/storage_permission.crx";

class MockUpdateService : public UpdateService {
 public:
  MockUpdateService() : UpdateService(nullptr, nullptr) {}
  MOCK_CONST_METHOD0(IsBusy, bool());
  MOCK_METHOD3(SendUninstallPing,
               void(const std::string& id,
                    const base::Version& version,
                    int reason));
  MOCK_METHOD2(StartUpdateCheck,
               void(const ExtensionUpdateCheckParams& params,
                    base::OnceClosure callback));
};

void ExtensionUpdateComplete(base::OnceClosure callback,
                             const absl::optional<CrxInstallError>& error) {
  // Expect success (no CrxInstallError). Assert on an error to put the error
  // message into the test log to aid debugging.
  ASSERT_FALSE(error.has_value()) << error->message();
  std::move(callback).Run();
}

}  // namespace

class ContentVerifierTest : public ExtensionBrowserTest {
 public:
  ContentVerifierTest() {}
  ~ContentVerifierTest() override {}

  void SetUp() override {
    // Override content verification mode before ExtensionSystemImpl initializes
    // ChromeContentVerifierDelegate.
    ChromeContentVerifierDelegate::SetDefaultModeForTesting(
        ChromeContentVerifierDelegate::VerifyInfo::Mode::ENFORCE);
    ON_CALL(update_service_, StartUpdateCheck)
        .WillByDefault(Invoke(this, &ContentVerifierTest::OnUpdateCheck));

    UpdateService::SupplyUpdateServiceForTest(&update_service_);

    ExtensionBrowserTest::SetUp();
  }

  void TearDown() override {
    ExtensionBrowserTest::TearDown();
    ChromeContentVerifierDelegate::SetDefaultModeForTesting(absl::nullopt);
  }

  bool ShouldEnableContentVerification() override { return true; }

  void AssertIsCorruptBitSetOnUpdateCheck(
      const ExtensionUpdateCheckParams& params,
      base::OnceClosure callback) {
    ASSERT_FALSE(params.update_info.empty());
    for (auto element : params.update_info) {
      ASSERT_TRUE(element.second.is_corrupt_reinstall);
    }
    OnUpdateCheck(params, std::move(callback));
  }

  virtual void OnUpdateCheck(const ExtensionUpdateCheckParams& params,
                             base::OnceClosure callback) {
    scoped_refptr<CrxInstaller> installer(
        CrxInstaller::CreateSilent(extension_service()));
    installer->set_install_source(ManifestLocation::kExternalPolicyDownload);
    installer->set_install_immediately(true);
    installer->set_allow_silent_install(true);
    installer->set_off_store_install_allow_reason(
        CrxInstaller::OffStoreInstallAllowedInTest);
    installer->set_installer_callback(
        base::BindOnce(&ExtensionUpdateComplete, std::move(callback)));
    installer->InstallCrx(
        test_data_dir_.AppendASCII("content_verifier/v1.crx"));
  }

  // Types of modification used by `TestContentScriptExtension` method below.
  enum class ScriptModificationAction {
    // Alter script content.
    kAlter,
    // Delete the script file.
    kDelete,
    // Make the script unreadable.
    kMakeUnreadable,
  };

  void TestContentScriptExtension(const std::string& crx_relpath,
                                  const std::string& id,
                                  const std::string& script_relpath,
                                  ScriptModificationAction action) {
    VerifierObserver verifier_observer;

    // Install the extension with content scripts. The initial read of the
    // content scripts will fail verification because they are read before the
    // content verification system has completed a one-time processing of the
    // expected hashes. (The extension only contains the root level hashes of
    // the merkle tree, but the content verification system builds the entire
    // tree and caches it in the extension install directory - see
    // ContentHashFetcher for more details).
    const Extension* extension = InstallExtensionFromWebstore(
        test_data_dir_.AppendASCII(crx_relpath), 1);
    ASSERT_TRUE(extension);
    EXPECT_EQ(id, extension->id());

    // Wait for the content verification code to finish processing the hashes.
    verifier_observer.EnsureFetchCompleted(id);

    // Now disable the extension, since content scripts are read at enable time,
    // set up our job observer, and re-enable, expecting a success this time.
    DisableExtension(id);
    using Result = TestContentVerifyJobObserver::Result;
    TestContentVerifyJobObserver job_observer;
    base::FilePath script_relfilepath =
        base::FilePath().AppendASCII(script_relpath);
    job_observer.ExpectJobResult(id, script_relfilepath, Result::SUCCESS);
    EnableExtension(id);
    EXPECT_TRUE(job_observer.WaitForExpectedJobs());

    // Now alter the contents of the content script, reload the extension, and
    // expect to see a job failure due to the content script content hash not
    // being what was signed by the webstore.
    base::FilePath scriptfile = extension->path().AppendASCII(script_relpath);
    {
      base::ScopedAllowBlockingForTesting allow_blocking;
      switch (action) {
        case ScriptModificationAction::kAlter:
          ASSERT_TRUE(
              base::AppendToFile(scriptfile, "some_extra_function_call();"));
          break;
        case ScriptModificationAction::kDelete:
          ASSERT_TRUE(base::DeleteFile(scriptfile));
          break;
        case ScriptModificationAction::kMakeUnreadable:
          ASSERT_TRUE(base::MakeFileUnreadable(scriptfile));
          break;
      }
    }
    DisableExtension(id);
    job_observer.ExpectJobResult(id, script_relfilepath, Result::FAILURE);
    EnableExtension(id);
    EXPECT_TRUE(job_observer.WaitForExpectedJobs());
  }

  void NavigateToResourceAndExpectExtensionDisabled(
      const ExtensionId& extension_id,
      const GURL& extension_resource) {
    TestExtensionRegistryObserver unload_observer(
        ExtensionRegistry::Get(profile()), extension_id);
    ui_test_utils::NavigateToURLWithDisposition(
        browser(), extension_resource,
        WindowOpenDisposition::NEW_FOREGROUND_TAB,
        ui_test_utils::BROWSER_TEST_NONE);
    EXPECT_TRUE(unload_observer.WaitForExtensionUnloaded());
    ExtensionPrefs* prefs = ExtensionPrefs::Get(profile());
    int reasons = prefs->GetDisableReasons(extension_id);
    EXPECT_EQ(disable_reason::DISABLE_CORRUPTED, reasons);
  }

  // Reads private key from |private_key_path| and generates extension id using
  // it.
  std::string GetExtensionIdFromPrivateKeyFile(
      const base::FilePath& private_key_path) {
    std::string private_key_contents;
    EXPECT_TRUE(
        base::ReadFileToString(private_key_path, &private_key_contents));
    std::string private_key_bytes;
    EXPECT_TRUE(
        Extension::ParsePEMKeyBytes(private_key_contents, &private_key_bytes));
    auto signing_key =
        crypto::RSAPrivateKey::CreateFromPrivateKeyInfo(std::vector<uint8_t>(
            private_key_bytes.begin(), private_key_bytes.end()));
    std::vector<uint8_t> public_key;
    signing_key->ExportPublicKey(&public_key);
    const std::string public_key_str(public_key.begin(), public_key.end());
    return crx_file::id_util::GenerateId(public_key_str);
  }

  // Creates a CRX in a temporary directory under |temp_dir| using contents from
  // |unpacked_path|. Compresses the |verified_contents| and injects these
  // contents into the the header of the CRX. Creates a random signing key
  // and sets |extension_id| using it. Returns path to new CRX in |crx_path|.
  testing::AssertionResult CreateCrxWithVerifiedContentsInHeader(
      base::ScopedTempDir* temp_dir,
      const base::FilePath& unpacked_path,
      const std::string& verified_contents,
      std::string* extension_id,
      base::FilePath* crx_path) {
    std::string compressed_verified_contents;
    if (!compression::GzipCompress(verified_contents,
                                   &compressed_verified_contents)) {
      return testing::AssertionFailure();
    }

    if (!temp_dir->CreateUniqueTempDir()) {
      return testing::AssertionFailure();
    }
    *crx_path = temp_dir->GetPath().AppendASCII("temp.crx");

    ExtensionCreator creator;
    creator.CreateCrxWithVerifiedContentsInHeaderForTesting(
        unpacked_path, *crx_path, compressed_verified_contents, extension_id);
    return testing::AssertionSuccess();
  }

 protected:
  base::test::ScopedFeatureList scoped_feature_list_;
  base::AutoReset<bool> scoped_use_update_service_ =
      ExtensionUpdater::GetScopedUseUpdateServiceForTesting();
  testing::NiceMock<MockUpdateService> update_service_;
};

IN_PROC_BROWSER_TEST_F(ContentVerifierTest, DotSlashPaths) {
  TestContentVerifyJobObserver job_observer;
  std::string id = "hoipipabpcoomfapcecilckodldhmpgl";

  using Result = TestContentVerifyJobObserver::Result;
  job_observer.ExpectJobResult(
      id, base::FilePath(FILE_PATH_LITERAL("background.js")), Result::SUCCESS);
  job_observer.ExpectJobResult(
      id, base::FilePath(FILE_PATH_LITERAL("page.html")), Result::SUCCESS);
  job_observer.ExpectJobResult(id, base::FilePath(FILE_PATH_LITERAL("page.js")),
                               Result::SUCCESS);
  job_observer.ExpectJobResult(
      id, base::FilePath(FILE_PATH_LITERAL("dir/page2.html")), Result::SUCCESS);
  job_observer.ExpectJobResult(
      id, base::FilePath(FILE_PATH_LITERAL("page2.js")), Result::SUCCESS);
  job_observer.ExpectJobResult(id, base::FilePath(FILE_PATH_LITERAL("cs1.js")),
                               Result::SUCCESS);
  job_observer.ExpectJobResult(id, base::FilePath(FILE_PATH_LITERAL("cs2.js")),
                               Result::SUCCESS);

  auto verifier_observer = std::make_unique<VerifierObserver>();

  // Install a test extension we copied from the webstore that has actual
  // signatures, and contains paths with a leading "./" in various places.
  const Extension* extension = InstallExtensionFromWebstore(
      test_data_dir_.AppendASCII("content_verifier/dot_slash_paths.crx"), 1);

  ASSERT_TRUE(extension);
  ASSERT_EQ(extension->id(), id);

  // The content scripts might fail verification the first time since the
  // one-time processing might not be finished yet - if that's the case then
  // we want to wait until that work is done.
  verifier_observer->EnsureFetchCompleted(id);

  // It is important to destroy |verifier_observer| here so that it doesn't see
  // any fetch from EnableExtension call below (the observer pointer in
  // content_verifier.cc isn't thread safe, so it might asynchronously call
  // OnFetchComplete after this test's body executes).
  verifier_observer.reset();

  EXPECT_TRUE(job_observer.WaitForExpectedJobs());

  // Set expectations for extension enablement below.
  job_observer.ExpectJobResult(id, base::FilePath(FILE_PATH_LITERAL("cs1.js")),
                               Result::SUCCESS);
  job_observer.ExpectJobResult(id, base::FilePath(FILE_PATH_LITERAL("cs2.js")),
                               Result::SUCCESS);

  // Now disable/re-enable the extension to cause the content scripts to be
  // read again.
  DisableExtension(id);
  EnableExtension(id);

  EXPECT_TRUE(job_observer.WaitForExpectedJobs());
}

IN_PROC_BROWSER_TEST_F(ContentVerifierTest, ContentScripts) {
  TestContentScriptExtension("content_verifier/content_script.crx",
                             "jmllhlobpjcnnomjlipadejplhmheiif", "script.js",
                             ScriptModificationAction::kAlter);
}

// crbug.com/897059 tracks test flakiness.
#if BUILDFLAG(IS_WIN)
#define MAYBE_ContentScriptsInLocales DISABLED_ContentScriptsInLocales
#else
#define MAYBE_ContentScriptsInLocales ContentScriptsInLocales
#endif
IN_PROC_BROWSER_TEST_F(ContentVerifierTest, MAYBE_ContentScriptsInLocales) {
  TestContentScriptExtension("content_verifier/content_script_locales.crx",
                             "jaghonccckpcikmliipifpoodmeofoon",
                             "_locales/en/content_script.js",
                             ScriptModificationAction::kAlter);
}

// Tests that a deleted content_script results in content verification failure.
//
// Regression test for crbug.com/1296310.
IN_PROC_BROWSER_TEST_F(ContentVerifierTest,
                       DeletedContentScriptFailsContentVerification) {
  TestContentScriptExtension("content_verifier/content_script.crx",
                             "jmllhlobpjcnnomjlipadejplhmheiif", "script.js",
                             ScriptModificationAction::kDelete);
}

// Tests that an unreadable content_script results in content verification
// failure.
IN_PROC_BROWSER_TEST_F(ContentVerifierTest,
                       UnreadableContentScriptFailsContentVerification) {
  TestContentScriptExtension("content_verifier/content_script.crx",
                             "jmllhlobpjcnnomjlipadejplhmheiif", "script.js",
                             ScriptModificationAction::kMakeUnreadable);
}

// Tests the case of a corrupt extension that is force-installed by policy and
// should not be allowed to be manually uninstalled/disabled by the user.
IN_PROC_BROWSER_TEST_F(ContentVerifierTest, PolicyCorrupted) {
  ExtensionSystem* system = ExtensionSystem::Get(profile());
  ExtensionService* service = system->extension_service();

  // The id of our test extension.
  ExtensionId kExtensionId("dkjgfphccejbobpbljnpjcmhmagkdoia");

  // Setup fake policy and update check objects.
  content_verifier_test::ForceInstallProvider policy(kExtensionId);
  system->management_policy()->RegisterProvider(&policy);
  auto external_provider = std::make_unique<MockExternalProvider>(
      service, ManifestLocation::kExternalPolicyDownload);
  external_provider->UpdateOrAddExtension(
      std::make_unique<ExternalInstallInfoUpdateUrl>(
          kExtensionId, std::string() /* install_parameter */,
          extension_urls::GetWebstoreUpdateUrl(),
          ManifestLocation::kExternalPolicyDownload, 0 /* creation_flags */,
          true /* mark_acknowldged */));
  service->AddProviderForTesting(std::move(external_provider));

  base::FilePath crx_path =
      test_data_dir_.AppendASCII("content_verifier/v1.crx");
  const Extension* extension = InstallExtension(
      crx_path, 1, mojom::ManifestLocation::kExternalPolicyDownload);
  ASSERT_TRUE(extension);

  TestExtensionRegistryObserver registry_observer(
      ExtensionRegistry::Get(profile()), kExtensionId);
  ContentVerifier* verifier = system->content_verifier();
  verifier->VerifyFailedForTest(kExtensionId, ContentVerifyJob::HASH_MISMATCH);

  // Set our mock update client to check that the corrupt bit is set on the
  // data structure it receives.
  ON_CALL(update_service_, StartUpdateCheck)
      .WillByDefault(Invoke(
          this, &ContentVerifierTest::AssertIsCorruptBitSetOnUpdateCheck));

  // Make sure the extension first got disabled due to corruption.
  EXPECT_TRUE(registry_observer.WaitForExtensionUnloaded());
  ExtensionPrefs* prefs = ExtensionPrefs::Get(profile());
  int reasons = prefs->GetDisableReasons(kExtensionId);
  EXPECT_TRUE(reasons & disable_reason::DISABLE_CORRUPTED);

  // Make sure the extension then got re-installed, and that after reinstall it
  // is no longer disabled due to corruption.
  EXPECT_TRUE(registry_observer.WaitForExtensionInstalled());

  reasons = prefs->GetDisableReasons(kExtensionId);
  EXPECT_FALSE(reasons & disable_reason::DISABLE_CORRUPTED);
  system->management_policy()->UnregisterProvider(&policy);
}

// Tests the case when an extension is first manually installed, then it gets
// corrupted and then it is added to force installed list. The extension should
// get reinstalled and should be enabled.
IN_PROC_BROWSER_TEST_F(ContentVerifierTest,
                       ManualInstalledExtensionGotCorruptedThenForceInstalled) {
  ExtensionSystem* system = ExtensionSystem::Get(profile());
  ExtensionService* service = system->extension_service();

  ExtensionId kTestExtensionId("dkjgfphccejbobpbljnpjcmhmagkdoia");
  base::FilePath crx_path =
      test_data_dir_.AppendASCII("content_verifier/v1.crx");

  const Extension* extension = InstallExtension(crx_path, 1);
  ASSERT_TRUE(extension);

  TestExtensionRegistryObserver registry_observer(
      ExtensionRegistry::Get(profile()), kTestExtensionId);
  // Explicitly corrupt the extension.
  ContentVerifier* verifier = system->content_verifier();
  verifier->VerifyFailedForTest(kTestExtensionId,
                                ContentVerifyJob::HASH_MISMATCH);

  // Make sure the extension first got disabled due to corruption.
  EXPECT_TRUE(registry_observer.WaitForExtensionUnloaded());
  ExtensionPrefs* prefs = ExtensionPrefs::Get(profile());
  int reasons = prefs->GetDisableReasons(kTestExtensionId);
  EXPECT_TRUE(reasons & disable_reason::DISABLE_CORRUPTED);

  VerifierObserver verifier_observer;

  // Setup fake policy and update check objects.
  content_verifier_test::ForceInstallProvider policy(kTestExtensionId);
  system->management_policy()->RegisterProvider(&policy);
  auto external_provider = std::make_unique<MockExternalProvider>(
      service, ManifestLocation::kExternalPolicyDownload);

  external_provider->UpdateOrAddExtension(
      std::make_unique<ExternalInstallInfoUpdateUrl>(
          kTestExtensionId, std::string() /* install_parameter */,
          extension_urls::GetWebstoreUpdateUrl(),
          ManifestLocation::kExternalPolicyDownload, 0 /* creation_flags */,
          true /* mark_acknowldged */));
  service->AddProviderForTesting(std::move(external_provider));

  service->CheckForExternalUpdates();
  // Set our mock update client to check that the corrupt bit is set on the
  // data structure it receives.
  ON_CALL(update_service_, StartUpdateCheck)
      .WillByDefault(Invoke(
          this, &ContentVerifierTest::AssertIsCorruptBitSetOnUpdateCheck));

  // Make sure the extension then got re-installed, and that after reinstall it
  // is no longer disabled due to corruption.
  EXPECT_TRUE(registry_observer.WaitForExtensionInstalled());

  // Wait for the content verification code to finish processing the hashes.
  verifier_observer.EnsureFetchCompleted(kTestExtensionId);

  reasons = prefs->GetDisableReasons(kTestExtensionId);
  EXPECT_FALSE(reasons & disable_reason::DISABLE_CORRUPTED);
  EXPECT_TRUE(extensions::ExtensionRegistry::Get(profile())
                  ->enabled_extensions()
                  .GetByID(kTestExtensionId));
}

class UserInstalledContentVerifierTest : public ContentVerifierTest {
 public:
  void SetUpInProcessBrowserTestFixture() override {
    ContentVerifierTest::SetUpInProcessBrowserTestFixture();

    EXPECT_CALL(update_service_, StartUpdateCheck)
        .WillRepeatedly(
            Invoke(this, &UserInstalledContentVerifierTest::OnUpdateCheck));
  }

 protected:
  void OnUpdateCheck(const ExtensionUpdateCheckParams& params,
                     base::OnceClosure callback) override {
    scoped_refptr<CrxInstaller> installer(
        CrxInstaller::CreateSilent(extension_service()));
    installer->set_install_source(ManifestLocation::kInternal);
    installer->set_install_immediately(true);
    installer->set_allow_silent_install(true);
    installer->set_off_store_install_allow_reason(
        CrxInstaller::OffStoreInstallAllowedInTest);
    installer->set_installer_callback(
        base::BindOnce(&ExtensionUpdateComplete, std::move(callback)));
    installer->InstallCrx(
        test_data_dir_.AppendASCII(kStoragePermissionExtensionCrx));
  }

  CorruptedExtensionReinstaller* corrupted_extension_reinstaller() {
    return extension_service()->corrupted_extension_reinstaller();
  }
};

// Setup a corrupted extension by tampering with one of its source files in
// PRE to verify that it is repaired at startup.
IN_PROC_BROWSER_TEST_F(UserInstalledContentVerifierTest,
                       PRE_UserInstalledCorruptedResourceOnStartup) {
  auto verifier_observer = std::make_unique<VerifierObserver>();
  InstallExtensionFromWebstore(
      test_data_dir_.AppendASCII(kStoragePermissionExtensionCrx), 1);
  verifier_observer->EnsureFetchCompleted(kStoragePermissionExtensionId);
  verifier_observer.reset();
  ExtensionRegistry* registry = ExtensionRegistry::Get(profile());
  const Extension* extension =
      registry->enabled_extensions().GetByID(kStoragePermissionExtensionId);
  EXPECT_TRUE(extension);
  const base::FilePath kResourcePath(FILE_PATH_LITERAL("background.js"));

  EXPECT_EQ("Test", ExecuteScriptInBackgroundPage(
                        kStoragePermissionExtensionId,
                        R"(chrome.storage.local.set({key: "Test"}, () =>
             domAutomationController.send("Test")))"));

  EXPECT_EQ("Test", ExecuteScriptInBackgroundPage(
                        kStoragePermissionExtensionId,
                        R"(chrome.storage.local.get(['key'], ({key}) =>
             domAutomationController.send(key)))"));
  // Corrupt the extension
  {
    base::FilePath resource_path = extension->path().Append(kResourcePath);
    base::ScopedAllowBlockingForTesting allow_blocking;
    // Temporarily disable extension, we don't want to tackle with resources of
    // enabled one.
    DisableExtension(kStoragePermissionExtensionId);
    ASSERT_TRUE(base::WriteFile(resource_path, "// corrupted\n"));
    EnableExtension(kStoragePermissionExtensionId);
  }

  TestExtensionRegistryObserver registry_observer(
      registry, kStoragePermissionExtensionId);
  ExtensionSystem* system = ExtensionSystem::Get(profile());
  system->content_verifier()->VerifyFailedForTest(
      kStoragePermissionExtensionId, ContentVerifyJob::HASH_MISMATCH);
  EXPECT_TRUE(registry_observer.WaitForExtensionUnloaded());

  // The extension should be disabled and not be in expected to be repaired yet.
  EXPECT_FALSE(
      corrupted_extension_reinstaller()->IsReinstallForCorruptionExpected(
          kStoragePermissionExtensionId));
  EXPECT_EQ(disable_reason::DISABLE_CORRUPTED,
            ExtensionPrefs::Get(profile())->GetDisableReasons(
                kStoragePermissionExtensionId));
}

// Now actually test what happens on the next startup after the PRE test above.
// TODO(https://crbug.com/1226260): Test is flaky.
IN_PROC_BROWSER_TEST_F(UserInstalledContentVerifierTest,
                       DISABLED_UserInstalledCorruptedResourceOnStartup) {
  ExtensionPrefs* prefs = ExtensionPrefs::Get(profile());
  ExtensionRegistry* registry = ExtensionRegistry::Get(profile());
  int disable_reasons = prefs->GetDisableReasons(kStoragePermissionExtensionId);

  // Depending on timing, the extension may have already been reinstalled
  // between SetUpInProcessBrowserTestFixture and now (usually not during local
  // testing on a developer machine, but sometimes on a heavily loaded system
  // such as the build waterfall / trybots). If the reinstall didn't already
  // happen, wait for it.
  if (disable_reasons & disable_reason::DISABLE_CORRUPTED) {
    EXPECT_TRUE(
        corrupted_extension_reinstaller()->IsReinstallForCorruptionExpected(
            kStoragePermissionExtensionId));
    TestExtensionRegistryObserver registry_observer(
        registry, kStoragePermissionExtensionId);
    ASSERT_TRUE(registry_observer.WaitForExtensionInstalled());
    disable_reasons = prefs->GetDisableReasons(kStoragePermissionExtensionId);
  }
  EXPECT_FALSE(
      corrupted_extension_reinstaller()->IsReinstallForCorruptionExpected(
          kStoragePermissionExtensionId));
  EXPECT_EQ(disable_reason::DISABLE_NONE, disable_reasons);
  const Extension* extension =
      ExtensionRegistry::Get(profile())->enabled_extensions().GetByID(
          kStoragePermissionExtensionId);
  EXPECT_TRUE(extension);

  {
    const base::FilePath kResourcePath(FILE_PATH_LITERAL("background.js"));
    base::ScopedAllowBlockingForTesting allow_blocking;
    base::FilePath resource_path = extension->path().Append(kResourcePath);
    std::string contents;
    ASSERT_TRUE(base::ReadFileToString(resource_path, &contents));
    EXPECT_EQ(std::string::npos, contents.find("corrupted"));
  }
  // This ensures that the background page is loaded. There is a unload/load
  // of the extension happening which crashes `ExtensionBackgroundPageWaiter`.
  devtools_util::InspectBackgroundPage(extension, profile());
  WaitForExtensionViewsToLoad();
  EXPECT_EQ("Test", ExecuteScriptInBackgroundPage(
                        kStoragePermissionExtensionId,
                        R"(chrome.storage.local.get(['key'], ({key}) =>
             domAutomationController.send(key)))"));
}

// Tests that verification failure during navigating to an extension resource
// correctly disables the extension.
IN_PROC_BROWSER_TEST_F(ContentVerifierTest, VerificationFailureOnNavigate) {
  const Extension* extension = InstallExtensionFromWebstore(
      test_data_dir_.AppendASCII("content_verifier/dot_slash_paths.crx"), 1);
  ASSERT_TRUE(extension);
  const ExtensionId kExtensionId = extension->id();
  const base::FilePath::CharType kResource[] = FILE_PATH_LITERAL("page.html");
  {
    // Modify content so that content verification fails.
    base::ScopedAllowBlockingForTesting allow_blocking;
    base::FilePath real_path = extension->path().Append(kResource);
    std::string extra = "some_extra_function_call();";
    ASSERT_TRUE(base::AppendToFile(real_path, extra));
  }

  GURL page_url = extension->GetResourceURL("page.html");
  NavigateToResourceAndExpectExtensionDisabled(kExtensionId, page_url);
}

// Verifies that CRX with verified contents injected into the header is
// successfully installed and verified.
IN_PROC_BROWSER_TEST_F(
    ContentVerifierTest,
    VerificationSuccessfulForCrxWithVerifiedContentsInjectedInHeader) {
  base::ScopedAllowBlockingForTesting allow_blocking;

  base::ScopedTempDir temp_dir;
  base::FilePath extension_dir =
      test_data_dir_.AppendASCII("content_verifier/storage_permission");
  base::FilePath resource_path = base::FilePath().AppendASCII("background.js");

  extensions::content_verifier_test_utils::TestExtensionBuilder
      verified_contents_builder;

  std::string resource_contents;
  base::ReadFileToString(extension_dir.Append(resource_path),
                         &resource_contents);
  verified_contents_builder.AddResource(resource_path.value(),
                                        resource_contents);
  std::string verified_contents =
      verified_contents_builder.CreateVerifiedContents();

  auto mock_content_verifier_delegate =
      std::make_unique<MockContentVerifierDelegate>();
  mock_content_verifier_delegate->SetVerifierKey(
      verified_contents_builder.GetTestContentVerifierPublicKey());
  ExtensionSystem::Get(profile())
      ->content_verifier()
      ->OverrideDelegateForTesting(std::move(mock_content_verifier_delegate));

  std::string extension_id;
  base::FilePath crx_path;
  ASSERT_TRUE(CreateCrxWithVerifiedContentsInHeader(
      &temp_dir, extension_dir, verified_contents, &extension_id, &crx_path));

  TestContentVerifySingleJobObserver observer(extension_id, resource_path);

  const Extension* extension = InstallExtensionFromWebstore(crx_path, 1);
  ASSERT_TRUE(extension);
  EXPECT_EQ(extension->id(), extension_id);

  ContentHashReader::InitStatus hashes_status = observer.WaitForOnHashesReady();
  EXPECT_EQ(ContentHashReader::InitStatus::SUCCESS, hashes_status);
}

// Verifies that CRX with malformed verified contents injected into the header
// is not installed.
IN_PROC_BROWSER_TEST_F(
    ContentVerifierTest,
    InstallationFailureForCrxWithMalformedVerifiedContentsInjectedInHeader) {
  base::ScopedAllowBlockingForTesting allow_blocking;
  base::ScopedTempDir temp_dir;
  base::FilePath test_dir = test_data_dir_.AppendASCII("content_verifier/v1");
  std::string extension_id;
  std::string verified_contents =
      "Not a valid verified contents, not even a valid JSON.";
  base::FilePath crx_path;
  ASSERT_TRUE(CreateCrxWithVerifiedContentsInHeader(
      &temp_dir, test_dir, verified_contents, &extension_id, &crx_path));

  const Extension* extension = InstallExtensionFromWebstore(crx_path, 0);
  EXPECT_FALSE(extension);
}

// Verifies that CRX with missing verified contents is successfully installed
// but not verified due to missing hashes.
IN_PROC_BROWSER_TEST_F(ContentVerifierTest,
                       VerificationFailureForMissingVerifiedContents) {
  base::ScopedAllowBlockingForTesting allow_blocking;
  base::FilePath unpacked_path =
      test_data_dir_.AppendASCII("content_verifier/storage_permission");
  base::FilePath crx_path = PackExtension(unpacked_path);
  ASSERT_TRUE(base::PathExists(crx_path.DirName().AppendASCII("temp.pem")));
  const std::string extension_id = GetExtensionIdFromPrivateKeyFile(
      crx_path.DirName().AppendASCII("temp.pem"));

  TestContentVerifySingleJobObserver observer(
      extension_id, base::FilePath().AppendASCII("background.js"));

  const Extension* extension = InstallExtensionFromWebstore(crx_path, 1);
  ASSERT_TRUE(extension);
  EXPECT_EQ(extension->id(), extension_id);

  ContentHashReader::InitStatus hashes_status = observer.WaitForOnHashesReady();
  EXPECT_EQ(ContentHashReader::InitStatus::HASHES_MISSING, hashes_status);
}

// Tests that tampering with a large resource fails content verification as
// expected. The size of the resource is such that it would trigger
// FileLoaderObserver::OnSeekComplete in extension_protocols.cc.
//
// Regression test for: http://crbug.com/965043.
IN_PROC_BROWSER_TEST_F(ContentVerifierTest, TamperLargeSizedResource) {
  // This test extension is copied from the webstore that has actual
  // signatures.
  const Extension* extension = InstallExtensionFromWebstore(
      test_data_dir_.AppendASCII("content_verifier/different_sized_files.crx"),
      1);
  ASSERT_TRUE(extension);

  const char kResource[] = "jquery-3.2.0.min.js";
  {
    // Modify content so that content verification fails.
    base::ScopedAllowBlockingForTesting allow_blocking;
    base::FilePath real_path = extension->path().AppendASCII(kResource);
    ASSERT_TRUE(base::PathExists(real_path));
    std::string extra = "some_extra_function_call();";
    ASSERT_TRUE(base::AppendToFile(real_path, extra));
  }

  NavigateToResourceAndExpectExtensionDisabled(
      extension->id(), extension->GetResourceURL(kResource));
}

// Tests that a resource reading failure due to FileURLLoader cancellation
// does not incorrectly result in content verificaton failure.
// Regression test for: http://crbug.com/977805.
IN_PROC_BROWSER_TEST_F(ContentVerifierTest,
                       PRE_ResourceReadCancellationDoesNotFailVerification) {
  // This test extension is copied from the webstore that has actual
  // signatures.
  const Extension* extension = InstallExtensionFromWebstore(
      test_data_dir_.AppendASCII("content_verifier/ten_meg_resource.crx"), 1);
  ASSERT_TRUE(extension);
  EXPECT_EQ(kTenMegResourceExtensionId, extension->id());

  // Navigate to a large resource that *likely* won't complete before
  // this test ends and results in FileDataPipeProducer shutdown. This results
  // in FILE_ERROR_ABORT in FileDataPipeProducer::Observer::BytesRead().
  //
  // Note that this can produce false-positive results because if the resource
  // completes loading before shutdown, this test will still pass. There
  // currently isn't a way to forcefully shut down FileDataPipeProducer.
  // Also, whether to pursue such effort is debatable as it feels poking into
  // the implementation detail a little too much.
  const char kLargeResource[] = "ten_meg_background.js";
  ui_test_utils::NavigateToURLWithDisposition(
      browser(), extension->GetResourceURL(kLargeResource),
      WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_NONE);
}

IN_PROC_BROWSER_TEST_F(ContentVerifierTest,
                       ResourceReadCancellationDoesNotFailVerification) {
  // Expect the extension to not get disabled due to corruption.
  extensions::ExtensionRegistry* registry =
      extensions::ExtensionRegistry::Get(profile());
  {
    // Add a helpful hint, in case the regression reappears.
    ExtensionPrefs* prefs = ExtensionPrefs::Get(profile());
    int reasons = prefs->GetDisableReasons(kTenMegResourceExtensionId);
    EXPECT_EQ(disable_reason::DISABLE_NONE, reasons)
        << "Unexpected disable reasons. Includes corruption: "
        << (reasons & disable_reason::DISABLE_CORRUPTED);
  }
  const Extension* extension =
      registry->enabled_extensions().GetByID(kTenMegResourceExtensionId);
  ASSERT_TRUE(extension);
}

// Tests that navigating to an extension resource with '/' at end does not
// disable the extension.
//
// Regression test for: https://crbug.com/929578.
IN_PROC_BROWSER_TEST_F(ContentVerifierTest,
                       RemainsEnabledOnNavigateToPathEndingWithSlash) {
  const Extension* extension = InstallExtensionFromWebstore(
      test_data_dir_.AppendASCII("content_verifier/dot_slash_paths.crx"), 1);
  ASSERT_TRUE(extension);
  const ExtensionId kExtensionId = extension->id();

  GURL page_url = extension->GetResourceURL("page.html/");
  ui_test_utils::NavigateToURLWithDispositionBlockUntilNavigationsComplete(
      browser(), page_url, 1, WindowOpenDisposition::CURRENT_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP);
  ExtensionPrefs* prefs = ExtensionPrefs::Get(profile());
  int reasons = prefs->GetDisableReasons(kExtensionId);
  EXPECT_FALSE(reasons);
}

// Tests that navigating to an extension resource with '.' at end does not
// disable the extension.
//
// Regression test for https://crbug.com/696208.
IN_PROC_BROWSER_TEST_F(ContentVerifierTest,
                       RemainsEnabledOnNavigateToPathEndingWithDot) {
  const Extension* extension = InstallExtensionFromWebstore(
      test_data_dir_.AppendASCII("content_verifier/dot_slash_paths.crx"), 1);
  ASSERT_TRUE(extension);
  const ExtensionId kExtensionId = extension->id();

  GURL page_url = extension->GetResourceURL("page.html.");
  ui_test_utils::NavigateToURLWithDispositionBlockUntilNavigationsComplete(
      browser(), page_url, 1, WindowOpenDisposition::CURRENT_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP);
  ExtensionPrefs* prefs = ExtensionPrefs::Get(profile());
  int reasons = prefs->GetDisableReasons(kExtensionId);
  EXPECT_EQ(disable_reason::DISABLE_NONE, reasons);
}

// Tests that navigating to an extension resource with incorrect case does not
// disable the extension, both in case-sensitive and case-insensitive systems.
//
// Regression test for https://crbug.com/1033294.
IN_PROC_BROWSER_TEST_F(ContentVerifierTest,
                       RemainsEnabledOnNavigateToPathWithIncorrectCase) {
  const Extension* extension = InstallExtensionFromWebstore(
      test_data_dir_.AppendASCII("content_verifier/dot_slash_paths.crx"), 1);
  ASSERT_TRUE(extension);
  const ExtensionId extension_id = extension->id();

  // Note: the resource in |extension| is "page.html".
  constexpr char kIncorrectCasePath[] = "PAGE.html";

  TestContentVerifySingleJobObserver job_observer(
      extension_id, base::FilePath().AppendASCII(kIncorrectCasePath));

  GURL page_url = extension->GetResourceURL(kIncorrectCasePath);
  ui_test_utils::NavigateToURLWithDispositionBlockUntilNavigationsComplete(
      browser(), page_url, 1, WindowOpenDisposition::CURRENT_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP);

  // Ensure that ContentVerifyJob has finished checking the resource.
  EXPECT_EQ(ContentVerifyJob::NONE, job_observer.WaitForJobFinished());

  ExtensionPrefs* prefs = ExtensionPrefs::Get(profile());
  int reasons = prefs->GetDisableReasons(extension_id);
  EXPECT_EQ(disable_reason::DISABLE_NONE, reasons);
}

class ContentVerifierPolicyTest : public ContentVerifierTest {
 public:
  // We need to do this work here because the force-install policy values are
  // checked pretty early on in the startup of the ExtensionService, which
  // happens between SetUpInProcessBrowserTestFixture and SetUpOnMainThread.
  void SetUpInProcessBrowserTestFixture() override {
    ContentVerifierTest::SetUpInProcessBrowserTestFixture();

    policy_provider_.SetDefaultReturns(
        /*is_initialization_complete_return=*/true,
        /*is_first_policy_load_complete_return=*/true);

    policy::BrowserPolicyConnector::SetPolicyProviderForTesting(
        &policy_provider_);
    // ExtensionManagementPolicyUpdater requires a single-threaded context to
    // call RunLoop::RunUntilIdle internally, and it isn't ready at this setup
    // moment.
    base::test::TaskEnvironment env;
    ExtensionManagementPolicyUpdater management_policy(&policy_provider_);
    management_policy.SetIndividualExtensionAutoInstalled(
        id_, extension_urls::kChromeWebstoreUpdateURL, true /* forced */);
  }

  void SetUpOnMainThread() override {
    extensions::browsertest_util::CreateAndInitializeLocalCache();
  }

 protected:
  // The id of the extension we want to have force-installed.
  std::string id_ = "dkjgfphccejbobpbljnpjcmhmagkdoia";

 private:
  testing::NiceMock<policy::MockConfigurationPolicyProvider> policy_provider_;
};

// We want to test what happens at startup with a corroption-disabled policy
// force installed extension. So we set that up in the PRE test here.
IN_PROC_BROWSER_TEST_F(ContentVerifierPolicyTest,
                       PRE_PolicyCorruptedOnStartup) {
  ExtensionRegistry* registry = ExtensionRegistry::Get(profile());
  TestExtensionRegistryObserver registry_observer(registry, id_);

  // Wait for the extension to be installed by policy we set up in
  // SetUpInProcessBrowserTestFixture.
  if (!registry->GetInstalledExtension(id_))
    EXPECT_TRUE(registry_observer.WaitForExtensionInstalled());

  // Simulate corruption of the extension so that we can test what happens
  // at startup in the non-PRE test.
  ExtensionSystem* system = ExtensionSystem::Get(profile());
  ContentVerifier* verifier = system->content_verifier();
  verifier->VerifyFailedForTest(id_, ContentVerifyJob::HASH_MISMATCH);
  EXPECT_TRUE(registry_observer.WaitForExtensionUnloaded());
  ExtensionPrefs* prefs = ExtensionPrefs::Get(profile());
  int reasons = prefs->GetDisableReasons(id_);
  EXPECT_TRUE(reasons & disable_reason::DISABLE_CORRUPTED);
}

// Now actually test what happens on the next startup after the PRE test above.
// TODO(crbug.com/1271946): Flaky on mac arm64.
#if BUILDFLAG(IS_MAC) && defined(ARCH_CPU_ARM64)
#define MAYBE_PolicyCorruptedOnStartup DISABLED_PolicyCorruptedOnStartup
#else
#define MAYBE_PolicyCorruptedOnStartup PolicyCorruptedOnStartup
#endif
IN_PROC_BROWSER_TEST_F(ContentVerifierPolicyTest,
                       MAYBE_PolicyCorruptedOnStartup) {
  // Depdending on timing, the extension may have already been reinstalled
  // between SetUpInProcessBrowserTestFixture and now (usually not during local
  // testing on a developer machine, but sometimes on a heavily loaded system
  // such as the build waterfall / trybots). If the reinstall didn't already
  // happen, wait for it.
  ExtensionPrefs* prefs = ExtensionPrefs::Get(profile());
  ExtensionRegistry* registry = ExtensionRegistry::Get(profile());
  int disable_reasons = prefs->GetDisableReasons(id_);
  if (disable_reasons & disable_reason::DISABLE_CORRUPTED) {
    TestExtensionRegistryObserver registry_observer(registry, id_);
    EXPECT_TRUE(registry_observer.WaitForExtensionInstalled());
    disable_reasons = prefs->GetDisableReasons(id_);
  }
  EXPECT_FALSE(disable_reasons & disable_reason::DISABLE_CORRUPTED);
  EXPECT_TRUE(registry->enabled_extensions().Contains(id_));
}

IN_PROC_BROWSER_TEST_F(ContentVerifierPolicyTest, Backoff) {
  ExtensionRegistry* registry = ExtensionRegistry::Get(profile());
  ExtensionSystem* system = ExtensionSystem::Get(profile());
  ContentVerifier* verifier = system->content_verifier();

  // Wait for the extension to be installed by the policy we set up in
  // SetUpInProcessBrowserTestFixture.
  if (!registry->GetInstalledExtension(id_)) {
    TestExtensionRegistryObserver registry_observer(registry, id_);
    EXPECT_TRUE(registry_observer.WaitForExtensionInstalled());
  }

  // Setup to intercept reinstall action, so we can see what the delay would
  // have been for the real action.
  content_verifier_test::DelayTracker delay_tracker;

  // Do 4 iterations of disabling followed by reinstall.
  const size_t iterations = 4;
  for (size_t i = 0; i < iterations; i++) {
    TestExtensionRegistryObserver registry_observer(registry, id_);
    verifier->VerifyFailedForTest(id_, ContentVerifyJob::HASH_MISMATCH);
    EXPECT_TRUE(registry_observer.WaitForExtensionUnloaded());
    // Resolve the request to |delay_tracker|, so the reinstallation can
    // proceed.
    delay_tracker.Proceed();
    EXPECT_TRUE(registry_observer.WaitForExtensionInstalled());
  }
  const std::vector<base::TimeDelta>& calls = delay_tracker.calls();

  // After |delay_tracker| resolves the 4 (|iterations|) reinstallation
  // requests, it will get an additional request (right away) for retrying
  // reinstallation.
  // Note: the additional request in non-test environment will arrive with
  // a (backoff) delay. But during test, |delay_tracker| issues the request
  // immediately.
  ASSERT_EQ(iterations, calls.size() - 1);
  // Assert that the first reinstall action happened with a delay of 0, and
  // then kept growing each additional time.
  EXPECT_EQ(base::TimeDelta(), delay_tracker.calls()[0]);
  for (size_t i = 1; i < delay_tracker.calls().size(); i++) {
    EXPECT_LT(calls[i - 1], calls[i]);
  }
}

// Tests that if CheckForExternalUpdates() fails, then we retry reinstalling
// corrupted policy extensions. For example: if network is unavailable,
// CheckForExternalUpdates() will fail.
IN_PROC_BROWSER_TEST_F(ContentVerifierPolicyTest, FailedUpdateRetries) {
  ExtensionRegistry* registry = ExtensionRegistry::Get(profile());
  ExtensionSystem* system = ExtensionSystem::Get(profile());
  ContentVerifier* verifier = system->content_verifier();

  // Wait for the extension to be installed by the policy we set up in
  // SetUpInProcessBrowserTestFixture.
  if (!registry->GetInstalledExtension(id_)) {
    TestExtensionRegistryObserver registry_observer(registry, id_);
    EXPECT_TRUE(registry_observer.WaitForExtensionInstalled());
  }

  content_verifier_test::DelayTracker delay_tracker;
  TestExtensionRegistryObserver registry_observer(registry, id_);
  {
    base::AutoReset<bool> disable_scope =
        ExtensionService::DisableExternalUpdatesForTesting();
    verifier->VerifyFailedForTest(id_, ContentVerifyJob::HASH_MISMATCH);
    EXPECT_TRUE(registry_observer.WaitForExtensionUnloaded());

    const std::vector<base::TimeDelta>& calls = delay_tracker.calls();
    ASSERT_EQ(1u, calls.size());
    EXPECT_EQ(base::TimeDelta(), delay_tracker.calls()[0]);

    delay_tracker.Proceed();

    CorruptedExtensionReinstaller::set_reinstall_action_for_test(nullptr);
  }
  // Update ExtensionService again without disabling external updates.
  // The extension should now get installed.
  delay_tracker.Proceed();

  EXPECT_TRUE(registry_observer.WaitForExtensionInstalled());
}

}  // namespace extensions
