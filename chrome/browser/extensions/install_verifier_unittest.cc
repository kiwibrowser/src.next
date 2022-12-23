// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/install_verifier.h"

#include "base/values.h"
#include "chrome/browser/extensions/extension_management.h"
#include "chrome/browser/extensions/extension_service_test_base.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/test/base/testing_profile.h"
#include "components/sync_preferences/testing_pref_service_syncable.h"
#include "extensions/browser/pref_names.h"
#include "extensions/common/extension.h"
#include "extensions/common/extension_builder.h"
#include "extensions/common/extension_urls.h"
#include "extensions/common/manifest.h"
#include "extensions/common/value_builder.h"
#include "testing/gtest/include/gtest/gtest.h"

using extensions::mojom::ManifestLocation;

namespace extensions {

class InstallVerifierTest : public ExtensionServiceTestBase {
 public:
  InstallVerifierTest() = default;

  InstallVerifierTest(const InstallVerifierTest&) = delete;
  InstallVerifierTest& operator=(const InstallVerifierTest&) = delete;

  ~InstallVerifierTest() override = default;

  void SetUp() override {
    ExtensionServiceTestBase::SetUp();

    InitializeExtensionService(ExtensionServiceInitParams());
  }

  // Adds an extension as being allowed by policy.
  void AddExtensionAsPolicyInstalled(const ExtensionId& id) {
    std::unique_ptr<base::DictionaryValue> extension_entry =
        DictionaryBuilder().Set("installation_mode", "allowed").Build();
    testing_profile()->GetTestingPrefService()->SetManagedPref(
        pref_names::kExtensionManagement,
        DictionaryBuilder().Set(id, std::move(extension_entry)).Build());
    EXPECT_TRUE(ExtensionManagementFactory::GetForBrowserContext(profile())
                    ->IsInstallationExplicitlyAllowed(id));
  }

 private:
  ScopedInstallVerifierBypassForTest force_install_verification{
      ScopedInstallVerifierBypassForTest::kForceOn};
  std::unique_ptr<ExtensionManagement> extension_management_;
  std::unique_ptr<TestingPrefServiceSimple> pref_service_;
};

// Test the behavior of the InstallVerifier for various extensions.
TEST_F(InstallVerifierTest, TestIsFromStoreAndMustRemainDisabled) {
  enum FromStoreStatus {
    FROM_STORE,
    NOT_FROM_STORE,
  };

  enum MustRemainDisabledStatus {
    MUST_REMAIN_DISABLED,
    CAN_BE_ENABLED,
  };

  GURL store_update_url = extension_urls::GetWebstoreUpdateUrl();
  GURL non_store_update_url("https://example.com");
  struct {
    const char* test_name;
    ManifestLocation location;
    absl::optional<GURL> update_url;
    FromStoreStatus expected_from_store_status;
    MustRemainDisabledStatus expected_must_remain_disabled_status;
  } test_cases[] = {
      {"internal from store", ManifestLocation::kInternal, store_update_url,
       FROM_STORE, CAN_BE_ENABLED},
      {"internal non-store update url", ManifestLocation::kInternal,
       non_store_update_url, NOT_FROM_STORE, MUST_REMAIN_DISABLED},
      {"internal no update url", ManifestLocation::kInternal, absl::nullopt,
       NOT_FROM_STORE, MUST_REMAIN_DISABLED},
      {"unpacked from store", ManifestLocation::kUnpacked, store_update_url,
       FROM_STORE, CAN_BE_ENABLED},
      {"unpacked non-store update url", ManifestLocation::kUnpacked,
       non_store_update_url, NOT_FROM_STORE, CAN_BE_ENABLED},
      {"unpacked no update url", ManifestLocation::kUnpacked, absl::nullopt,
       NOT_FROM_STORE, CAN_BE_ENABLED},
      {"external from store", ManifestLocation::kExternalPolicyDownload,
       store_update_url, FROM_STORE, CAN_BE_ENABLED},
      {"external non-store update url",
       ManifestLocation::kExternalPolicyDownload, non_store_update_url,
       NOT_FROM_STORE, CAN_BE_ENABLED},
  };

  InstallVerifier* install_verifier = InstallVerifier::Get(profile());
  for (const auto& test_case : test_cases) {
    SCOPED_TRACE(test_case.test_name);
    ExtensionBuilder extension_builder(test_case.test_name);
    extension_builder.SetLocation(test_case.location);
    if (test_case.update_url) {
      extension_builder.SetManifestKey("update_url",
                                       test_case.update_url->spec());
    }
    scoped_refptr<const Extension> extension = extension_builder.Build();

    if (Manifest::IsPolicyLocation(test_case.location))
      AddExtensionAsPolicyInstalled(extension->id());

    EXPECT_EQ(test_case.expected_from_store_status == FROM_STORE,
              InstallVerifier::IsFromStore(*extension, profile()));
    disable_reason::DisableReason disable_reason;
    std::u16string error;
    EXPECT_EQ(
        test_case.expected_must_remain_disabled_status == MUST_REMAIN_DISABLED,
        install_verifier->MustRemainDisabled(extension.get(), &disable_reason,
                                             &error))
        << error;
  }
}

}  // namespace extensions
