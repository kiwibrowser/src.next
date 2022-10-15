// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/services/app_service/public/cpp/file_handler_info.h"
#include "extensions/common/manifest_constants.h"
#include "extensions/common/manifest_handlers/file_handler_info.h"
#include "extensions/common/manifest_test.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace extensions {

namespace errors = manifest_errors;

typedef ManifestTest FileHandlersManifestTest;

TEST_F(FileHandlersManifestTest, InvalidFileHandlers) {
  Testcase testcases[] = {
      Testcase("file_handlers_invalid_handlers.json",
               errors::kInvalidFileHandlers),
      Testcase("file_handlers_invalid_type.json",
               errors::kInvalidFileHandlerType),
      Testcase("file_handlers_invalid_extension.json",
               errors::kInvalidFileHandlerExtension),
      Testcase("file_handlers_invalid_no_type_or_extension.json",
               errors::kInvalidFileHandlerNoTypeOrExtension),
      Testcase("file_handlers_invalid_type_element.json",
               errors::kInvalidFileHandlerTypeElement),
      Testcase("file_handlers_invalid_extension_element.json",
               errors::kInvalidFileHandlerExtensionElement),
      Testcase("file_handlers_invalid_too_many.json",
               errors::kInvalidFileHandlersTooManyTypesAndExtensions),
      Testcase("file_handlers_invalid_include_directories.json",
               errors::kInvalidFileHandlerIncludeDirectories),
      Testcase("file_handlers_invalid_verb.json",
               errors::kInvalidFileHandlerVerb),
  };
  RunTestcases(testcases, std::size(testcases), EXPECT_TYPE_ERROR);
}

TEST_F(FileHandlersManifestTest, ValidFileHandlers) {
  scoped_refptr<const Extension> extension =
      LoadAndExpectSuccess("file_handlers_valid.json");

  ASSERT_TRUE(extension.get());
  const FileHandlersInfo* handlers =
      FileHandlers::GetFileHandlers(extension.get());
  ASSERT_TRUE(handlers != nullptr);
  ASSERT_EQ(3U, handlers->size());

  apps::FileHandlerInfo handler = handlers->at(0);
  EXPECT_EQ("directories", handler.id);
  EXPECT_EQ(0U, handler.types.size());
  EXPECT_EQ(1U, handler.extensions.size());
  EXPECT_EQ(1U, handler.extensions.count("*/*"));
  EXPECT_EQ(true, handler.include_directories);

  handler = handlers->at(1);
  EXPECT_EQ("image", handler.id);
  EXPECT_EQ(1U, handler.types.size());
  EXPECT_EQ(1U, handler.types.count("image/*"));
  EXPECT_EQ(2U, handler.extensions.size());
  EXPECT_EQ(1U, handler.extensions.count(".png"));
  EXPECT_EQ(1U, handler.extensions.count(".gif"));
  EXPECT_EQ("add_to", handler.verb);

  handler = handlers->at(2);
  EXPECT_EQ("text", handler.id);
  EXPECT_EQ(1U, handler.types.size());
  EXPECT_EQ(1U, handler.types.count("text/*"));
  EXPECT_EQ(0U, handler.extensions.size());
}

TEST_F(FileHandlersManifestTest, NotPlatformApp) {
  // This should load successfully but have the file handlers ignored.
  scoped_refptr<const Extension> extension =
      LoadAndExpectSuccess("file_handlers_invalid_not_app.json");

  ASSERT_TRUE(extension.get());
  const FileHandlersInfo* handlers =
      FileHandlers::GetFileHandlers(extension.get());
  ASSERT_TRUE(handlers == nullptr);
}

}  // namespace extensions
