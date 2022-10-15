// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "extensions/common/manifest.h"

#include <memory>
#include <utility>

#include "base/json/json_reader.h"
#include "components/crx_file/id_util.h"
#include "extensions/common/install_warning.h"
#include "extensions/common/manifest_constants.h"
#include "extensions/common/value_builder.h"
#include "testing/gtest/include/gtest/gtest.h"

using ManifestTest = testing::Test;
using extensions::mojom::ManifestLocation;

namespace extensions {

TEST(ManifestTest, ValidateWarnsOnDiffFingerprintKeyUnpacked) {
  std::string error;
  std::vector<InstallWarning> warnings;
  Manifest(ManifestLocation::kUnpacked,
           DictionaryBuilder()
               .Set(manifest_keys::kDifferentialFingerprint, "")
               .Build(),
           crx_file::id_util::GenerateId("extid"))
      .ValidateManifest(&error, &warnings);
  EXPECT_EQ("", error);
  EXPECT_EQ(1uL, warnings.size());
  EXPECT_EQ(manifest_errors::kHasDifferentialFingerprint, warnings[0].message);
}

TEST(ManifestTest, ValidateWarnsOnDiffFingerprintKeyCommandLine) {
  std::string error;
  std::vector<InstallWarning> warnings;
  Manifest(ManifestLocation::kCommandLine,
           DictionaryBuilder()
               .Set(manifest_keys::kDifferentialFingerprint, "")
               .Build(),
           crx_file::id_util::GenerateId("extid"))
      .ValidateManifest(&error, &warnings);
  EXPECT_EQ("", error);
  EXPECT_EQ(1uL, warnings.size());
  EXPECT_EQ(manifest_errors::kHasDifferentialFingerprint, warnings[0].message);
}

TEST(ManifestTest, ValidateSilentOnDiffFingerprintKeyInternal) {
  std::string error;
  std::vector<InstallWarning> warnings;
  Manifest(ManifestLocation::kInternal,
           DictionaryBuilder()
               .Set(manifest_keys::kDifferentialFingerprint, "")
               .Build(),
           crx_file::id_util::GenerateId("extid"))
      .ValidateManifest(&error, &warnings);
  EXPECT_EQ("", error);
  EXPECT_EQ(0uL, warnings.size());
}

TEST(ManifestTest, ValidateSilentOnNoDiffFingerprintKeyUnpacked) {
  std::string error;
  std::vector<InstallWarning> warnings;
  Manifest(ManifestLocation::kUnpacked, DictionaryBuilder().Build(),
           crx_file::id_util::GenerateId("extid"))
      .ValidateManifest(&error, &warnings);
  EXPECT_EQ("", error);
  EXPECT_EQ(0uL, warnings.size());
}

TEST(ManifestTest, ValidateSilentOnNoDiffFingerprintKeyInternal) {
  std::string error;
  std::vector<InstallWarning> warnings;
  Manifest(ManifestLocation::kInternal, DictionaryBuilder().Build(),
           crx_file::id_util::GenerateId("extid"))
      .ValidateManifest(&error, &warnings);
  EXPECT_EQ("", error);
  EXPECT_EQ(0uL, warnings.size());
}

// Tests `Manifest::available_values()` and whether it correctly filters keys
// not available to the manifest.
TEST(ManifestTest, AvailableValues) {
  struct {
    const char* input_manifest;
    const char* expected_available_manifest;
  } test_cases[] =
      // clang-format off
  {
    // In manifest version 2, "host_permissions" key is not available.
    // Additionally "background.service_worker" key is not available to hosted
    // apps.
    {R"(
      {
        "name": "Test Extension",
        "app": {
          "urls": ""
        },
        "background": {
          "service_worker": "service_worker.js"
        },
        "manifest_version": 2,
        "host_permissions": [],
        "nacl_modules": ""
      }
    )",
    R"(
      {
        "name": "Test Extension",
        "app": {
          "urls": ""
        },
        "background": {},
        "manifest_version": 2,
        "nacl_modules": ""
      }
    )"},
    // In manifest version 3, "nacl_modules" key is not available.
    {R"(
      {
        "name": "Test Extension",
        "manifest_version": 3,
        "host_permissions": [],
        "nacl_modules": ""
      }
    )",
      R"(
      {
        "name": "Test Extension",
        "manifest_version": 3,
        "host_permissions": []
      }
    )"}
  };
  // clang-format on

  for (const auto& test_case : test_cases) {
    absl::optional<base::Value> manifest_value =
        base::JSONReader::Read(test_case.input_manifest);
    ASSERT_TRUE(manifest_value) << test_case.input_manifest;
    ASSERT_TRUE(manifest_value->is_dict()) << test_case.input_manifest;

    Manifest manifest(ManifestLocation::kInternal,
                      base::DictionaryValue::From(base::Value::ToUniquePtrValue(
                          std::move(*manifest_value))),
                      crx_file::id_util::GenerateId("extid"));

    absl::optional<base::Value> expected_value =
        base::JSONReader::Read(test_case.expected_available_manifest);
    ASSERT_TRUE(expected_value) << test_case.expected_available_manifest;
    EXPECT_EQ(*expected_value,
              static_cast<const base::Value&>(manifest.available_values()));
  }
}

}  // namespace extensions
