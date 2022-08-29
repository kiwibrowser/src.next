// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/external_install_error.h"
#include "base/test/scoped_feature_list.h"
#include "chrome/browser/extensions/external_install_error_constants.h"
#include "chrome/common/chrome_features.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace extensions {

TEST(ExternalInstallErrorTest, DefaultButtonFromFeature) {
  base::test::ScopedFeatureList scoped_feature_list;
  scoped_feature_list.InitAndEnableFeatureWithParameters(
      features::kExternalExtensionDefaultButtonControl,
      {{WebstoreDataFetcherDelegate::kExternalInstallDefaultButtonKey,
        kDefaultDialogButtonSettingOk}});

  EXPECT_EQ(ExternalInstallError::DIALOG_BUTTON_OK,
            ExternalInstallError::GetDefaultDialogButton(
                base::Value(base::Value::Type::DICTIONARY)));
}

TEST(ExternalInstallErrorTest, DefaultButtonFromWebstoreResponse) {
  // Set the default button from the feature as well. The webstore response,
  // when present, should have priority.
  base::test::ScopedFeatureList scoped_feature_list;
  scoped_feature_list.InitAndEnableFeatureWithParameters(
      features::kExternalExtensionDefaultButtonControl,
      {{WebstoreDataFetcherDelegate::kExternalInstallDefaultButtonKey,
        kDefaultDialogButtonSettingOk}});

  base::Value webstore_data(base::Value::Type::DICTIONARY);
  webstore_data.SetKey(
      WebstoreDataFetcherDelegate::kExternalInstallDefaultButtonKey,
      base::Value(kDefaultDialogButtonSettingCancel));

  EXPECT_EQ(ExternalInstallError::DIALOG_BUTTON_CANCEL,
            ExternalInstallError::GetDefaultDialogButton(webstore_data));
}

TEST(ExternalInstallErrorTest, DefaultButtonFallback) {
  EXPECT_EQ(ExternalInstallError::NOT_SPECIFIED,
            ExternalInstallError::GetDefaultDialogButton(
                base::Value(base::Value::Type::DICTIONARY)));
}

}  // namespace extensions
