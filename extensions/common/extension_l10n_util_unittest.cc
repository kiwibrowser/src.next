// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "extensions/common/extension_l10n_util.h"

#include <memory>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/path_service.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "extensions/common/constants.h"
#include "extensions/common/error_utils.h"
#include "extensions/common/extension_paths.h"
#include "extensions/common/manifest_constants.h"
#include "extensions/common/message_bundle.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/zlib/google/compression_utils.h"
#include "ui/base/l10n/l10n_util.h"

using extension_l10n_util::GzippedMessagesPermission;

namespace extensions {

namespace errors = manifest_errors;
namespace keys = manifest_keys;

namespace {

TEST(ExtensionL10nUtil, ValidateLocalesWithBadLocale) {
  base::ScopedTempDir temp;
  ASSERT_TRUE(temp.CreateUniqueTempDir());

  base::FilePath src_path = temp.GetPath().Append(kLocaleFolder);
  base::FilePath locale = src_path.AppendASCII("ms");
  ASSERT_TRUE(base::CreateDirectory(locale));

  base::FilePath messages_file = locale.Append(kMessagesFilename);
  std::string data = "{ \"name\":";
  ASSERT_EQ(static_cast<int>(data.length()),
            base::WriteFile(messages_file, data.c_str(), data.length()));

  base::DictionaryValue manifest;
  manifest.SetStringKey(keys::kDefaultLocale, "en");
  std::string error;
  EXPECT_FALSE(extension_l10n_util::ValidateExtensionLocales(
      temp.GetPath(), &manifest, &error));
  EXPECT_THAT(
      error,
      testing::HasSubstr(base::UTF16ToUTF8(messages_file.LossyDisplayName())));
}

TEST(ExtensionL10nUtil, GetValidLocalesEmptyLocaleFolder) {
  base::ScopedTempDir temp;
  ASSERT_TRUE(temp.CreateUniqueTempDir());

  base::FilePath src_path = temp.GetPath().Append(kLocaleFolder);
  ASSERT_TRUE(base::CreateDirectory(src_path));

  std::string error;
  std::set<std::string> locales;
  EXPECT_FALSE(
      extension_l10n_util::GetValidLocales(src_path, &locales, &error));

  EXPECT_TRUE(locales.empty());
}

TEST(ExtensionL10nUtil, GetValidLocalesWithValidLocaleNoMessagesFile) {
  base::ScopedTempDir temp;
  ASSERT_TRUE(temp.CreateUniqueTempDir());

  base::FilePath src_path = temp.GetPath().Append(kLocaleFolder);
  ASSERT_TRUE(base::CreateDirectory(src_path));
  ASSERT_TRUE(base::CreateDirectory(src_path.AppendASCII("sr")));

  std::string error;
  std::set<std::string> locales;
  EXPECT_FALSE(
      extension_l10n_util::GetValidLocales(src_path, &locales, &error));

  EXPECT_TRUE(locales.empty());
}

TEST(ExtensionL10nUtil, GetValidLocalesWithUnsupportedLocale) {
  base::ScopedTempDir temp;
  ASSERT_TRUE(temp.CreateUniqueTempDir());

  base::FilePath src_path = temp.GetPath().Append(kLocaleFolder);
  ASSERT_TRUE(base::CreateDirectory(src_path));
  // Supported locale.
  base::FilePath locale_1 = src_path.AppendASCII("sr");
  ASSERT_TRUE(base::CreateDirectory(locale_1));
  std::string data("whatever");
  ASSERT_EQ(static_cast<int>(data.length()),
            base::WriteFile(locale_1.Append(kMessagesFilename), data.c_str(),
                            data.length()));
  // Unsupported locale.
  ASSERT_TRUE(base::CreateDirectory(src_path.AppendASCII("xxx_yyy")));

  std::string error;
  std::set<std::string> locales;
  EXPECT_TRUE(extension_l10n_util::GetValidLocales(src_path, &locales, &error));

  EXPECT_FALSE(locales.empty());
  EXPECT_TRUE(locales.find("sr") != locales.end());
  EXPECT_FALSE(locales.find("xxx_yyy") != locales.end());
}

TEST(ExtensionL10nUtil, GetValidLocalesWithValidLocalesAndMessagesFile) {
  base::FilePath install_dir;
  ASSERT_TRUE(base::PathService::Get(DIR_TEST_DATA, &install_dir));
  install_dir =
      install_dir.AppendASCII("extension_with_locales").Append(kLocaleFolder);

  std::string error;
  std::set<std::string> locales;
  EXPECT_TRUE(
      extension_l10n_util::GetValidLocales(install_dir, &locales, &error));
  EXPECT_EQ(3U, locales.size());
  EXPECT_TRUE(locales.find("sr") != locales.end());
  EXPECT_TRUE(locales.find("en") != locales.end());
  EXPECT_TRUE(locales.find("en_US") != locales.end());
}

TEST(ExtensionL10nUtil, LoadMessageCatalogsValidFallback) {
  extension_l10n_util::ScopedLocaleForTest scoped_locale("en-US");
  base::FilePath install_dir;
  ASSERT_TRUE(base::PathService::Get(DIR_TEST_DATA, &install_dir));
  install_dir =
      install_dir.AppendASCII("extension_with_locales").Append(kLocaleFolder);

  std::string error;
  std::unique_ptr<MessageBundle> bundle(
      extension_l10n_util::LoadMessageCatalogs(
          install_dir, "sr", GzippedMessagesPermission::kDisallow, &error));
  ASSERT_TRUE(bundle);
  EXPECT_TRUE(error.empty());
  EXPECT_EQ("Color", bundle->GetL10nMessage("color"));
  EXPECT_EQ("Not in the US or GB.", bundle->GetL10nMessage("not_in_US_or_GB"));
}

TEST(ExtensionL10nUtil, LoadMessageCatalogsLowercaseLocales) {
  extension_l10n_util::ScopedLocaleForTest scoped_locale("en-US");
  base::FilePath install_dir;
  ASSERT_TRUE(base::PathService::Get(DIR_TEST_DATA, &install_dir));
  install_dir = install_dir.AppendASCII("extension_with_lowercase_locales")
                    .Append(kLocaleFolder);

  std::string error;
  std::unique_ptr<MessageBundle> bundle(
      extension_l10n_util::LoadMessageCatalogs(
          install_dir, "en-US", GzippedMessagesPermission::kDisallow, &error));
  ASSERT_TRUE(bundle);
  EXPECT_TRUE(error.empty());
  const base::FilePath locale_uppercase_path = install_dir.AppendASCII("en_US");
  const base::FilePath locale_lowercase_path = install_dir.AppendASCII("en_us");
  if (base::PathExists(locale_uppercase_path) &&
      base::PathExists(locale_lowercase_path)) {
    // Path system is case-insensitive.
    EXPECT_EQ("color lowercase", bundle->GetL10nMessage("color"));
  } else {
    EXPECT_EQ("", bundle->GetL10nMessage("color"));
  }
  std::set<std::string> all_locales;
  extension_l10n_util::GetAllLocales(&all_locales);
  EXPECT_FALSE(extension_l10n_util::ShouldSkipValidation(
      install_dir, locale_uppercase_path, all_locales));
  EXPECT_FALSE(extension_l10n_util::ShouldSkipValidation(
      install_dir, locale_lowercase_path, all_locales));
}

TEST(ExtensionL10nUtil, LoadMessageCatalogsMissingFiles) {
  extension_l10n_util::ScopedLocaleForTest scoped_locale("sr");
  base::ScopedTempDir temp;
  ASSERT_TRUE(temp.CreateUniqueTempDir());

  base::FilePath src_path = temp.GetPath().Append(kLocaleFolder);
  ASSERT_TRUE(base::CreateDirectory(src_path));
  ASSERT_TRUE(base::CreateDirectory(src_path.AppendASCII("en")));
  ASSERT_TRUE(base::CreateDirectory(src_path.AppendASCII("sr")));

  std::string error;
  EXPECT_FALSE(extension_l10n_util::LoadMessageCatalogs(
      src_path, "en", GzippedMessagesPermission::kDisallow, &error));
  EXPECT_FALSE(error.empty());
}

TEST(ExtensionL10nUtil, LoadMessageCatalogsBadJSONFormat) {
  extension_l10n_util::ScopedLocaleForTest scoped_locale("sr");
  base::ScopedTempDir temp;
  ASSERT_TRUE(temp.CreateUniqueTempDir());

  base::FilePath src_path = temp.GetPath().Append(kLocaleFolder);
  ASSERT_TRUE(base::CreateDirectory(src_path));

  base::FilePath locale = src_path.AppendASCII("sr");
  ASSERT_TRUE(base::CreateDirectory(locale));

  std::string data = "{ \"name\":";
  base::FilePath messages_file = locale.Append(kMessagesFilename);
  ASSERT_EQ(static_cast<int>(data.length()),
            base::WriteFile(messages_file, data.c_str(), data.length()));

  std::string error;
  EXPECT_FALSE(extension_l10n_util::LoadMessageCatalogs(
      src_path, "en_US", GzippedMessagesPermission::kDisallow, &error));
  EXPECT_NE(std::string::npos,
            error.find(ErrorUtils::FormatErrorMessage(
                errors::kLocalesInvalidLocale,
                base::UTF16ToUTF8(messages_file.LossyDisplayName()),
                "Line: 1, column: 10,")));
}

TEST(ExtensionL10nUtil, LoadMessageCatalogsDuplicateKeys) {
  extension_l10n_util::ScopedLocaleForTest scoped_locale("sr");
  base::ScopedTempDir temp;
  ASSERT_TRUE(temp.CreateUniqueTempDir());

  base::FilePath src_path = temp.GetPath().Append(kLocaleFolder);
  ASSERT_TRUE(base::CreateDirectory(src_path));

  base::FilePath locale_1 = src_path.AppendASCII("en");
  ASSERT_TRUE(base::CreateDirectory(locale_1));

  std::string data =
      "{ \"name\": { \"message\": \"something\" }, "
      "\"name\": { \"message\": \"something else\" } }";
  ASSERT_EQ(static_cast<int>(data.length()),
            base::WriteFile(locale_1.Append(kMessagesFilename), data.c_str(),
                            data.length()));

  base::FilePath locale_2 = src_path.AppendASCII("sr");
  ASSERT_TRUE(base::CreateDirectory(locale_2));

  ASSERT_EQ(static_cast<int>(data.length()),
            base::WriteFile(locale_2.Append(kMessagesFilename), data.c_str(),
                            data.length()));

  std::string error;
  // JSON parser hides duplicates. We are going to get only one key/value
  // pair at the end.
  std::unique_ptr<MessageBundle> message_bundle(
      extension_l10n_util::LoadMessageCatalogs(
          src_path, "en", GzippedMessagesPermission::kDisallow, &error));
  EXPECT_TRUE(message_bundle.get());
  EXPECT_TRUE(error.empty());
}

TEST(ExtensionL10nUtil, LoadMessageCatalogsCompressed) {
  extension_l10n_util::ScopedLocaleForTest scoped_locale("sr");
  base::ScopedTempDir temp;
  ASSERT_TRUE(temp.CreateUniqueTempDir());

  base::FilePath src_path = temp.GetPath().Append(kLocaleFolder);
  ASSERT_TRUE(base::CreateDirectory(src_path));

  base::FilePath locale = src_path.AppendASCII("en");
  ASSERT_TRUE(base::CreateDirectory(locale));

  // Create a compressed messages.json.gz file.
  std::string data = "{ \"name\": { \"message\": \"something\" } }";
  std::string compressed_data;
  ASSERT_TRUE(compression::GzipCompress(data, &compressed_data));
  ASSERT_EQ(static_cast<int>(compressed_data.length()),
            base::WriteFile(locale.Append(kMessagesFilename)
                                .AddExtension(FILE_PATH_LITERAL(".gz")),
                            compressed_data.c_str(), compressed_data.length()));

  // Test that LoadMessageCatalogs fails with gzip_permission = kDisallow.
  std::string error;
  std::unique_ptr<MessageBundle> message_bundle(
      extension_l10n_util::LoadMessageCatalogs(
          src_path, "en", GzippedMessagesPermission::kDisallow, &error));
  EXPECT_FALSE(message_bundle.get());
  EXPECT_FALSE(error.empty());

  // Test that LoadMessageCatalogs succeeds with gzip_permission =
  // kAllowForTrustedSource.
  error.clear();
  message_bundle.reset(extension_l10n_util::LoadMessageCatalogs(
      src_path, "en", GzippedMessagesPermission::kAllowForTrustedSource,
      &error));
  EXPECT_TRUE(message_bundle.get());
  EXPECT_TRUE(error.empty());
  EXPECT_EQ("something", message_bundle->GetL10nMessage("name"));
}

// Caller owns the returned object.
MessageBundle* CreateManifestBundle() {
  auto catalog = std::make_unique<base::DictionaryValue>();

  base::Value name_tree(base::Value::Type::DICTIONARY);
  name_tree.SetStringKey("message", "name");
  catalog->SetKey("name", std::move(name_tree));

  base::Value short_name_tree(base::Value::Type::DICTIONARY);
  short_name_tree.SetStringKey("message", "short_name");
  catalog->SetKey("short_name", std::move(short_name_tree));

  base::Value description_tree(base::Value::Type::DICTIONARY);
  description_tree.SetStringKey("message", "description");
  catalog->SetKey("description", std::move(description_tree));

  base::Value action_title_tree(base::Value::Type::DICTIONARY);
  action_title_tree.SetStringKey("message", "action title");
  catalog->SetKey("title", std::move(action_title_tree));

  base::Value omnibox_keyword_tree(base::Value::Type::DICTIONARY);
  omnibox_keyword_tree.SetStringKey("message", "omnibox keyword");
  catalog->SetKey("omnibox_keyword", std::move(omnibox_keyword_tree));

  base::Value file_handler_title_tree(base::Value::Type::DICTIONARY);
  file_handler_title_tree.SetStringKey("message", "file handler title");
  catalog->SetKey("file_handler_title", std::move(file_handler_title_tree));

  base::Value launch_local_path_tree(base::Value::Type::DICTIONARY);
  launch_local_path_tree.SetStringKey("message", "main.html");
  catalog->SetKey("launch_local_path", std::move(launch_local_path_tree));

  base::Value launch_web_url_tree(base::Value::Type::DICTIONARY);
  launch_web_url_tree.SetStringKey("message", "http://www.google.com/");
  catalog->SetKey("launch_web_url", std::move(launch_web_url_tree));

  base::Value first_command_description_tree(base::Value::Type::DICTIONARY);
  first_command_description_tree.SetStringKey("message", "first command");
  catalog->SetKey("first_command_description",
                  std::move(first_command_description_tree));

  base::Value second_command_description_tree(base::Value::Type::DICTIONARY);
  second_command_description_tree.SetStringKey("message", "second command");
  catalog->SetKey("second_command_description",
                  std::move(second_command_description_tree));

  base::Value url_country_tree(base::Value::Type::DICTIONARY);
  url_country_tree.SetStringKey("message", "de");
  catalog->SetKey("country", std::move(url_country_tree));

  std::vector<std::unique_ptr<base::DictionaryValue>> catalogs;
  catalogs.push_back(std::move(catalog));

  std::string error;
  MessageBundle* bundle = MessageBundle::Create(catalogs, &error);
  EXPECT_TRUE(bundle);
  EXPECT_TRUE(error.empty());

  return bundle;
}

TEST(ExtensionL10nUtil, LocalizeEmptyManifest) {
  base::DictionaryValue manifest;
  std::string error;
  std::unique_ptr<MessageBundle> messages(CreateManifestBundle());

  EXPECT_FALSE(
      extension_l10n_util::LocalizeManifest(*messages, &manifest, &error));
  EXPECT_EQ(std::string(errors::kInvalidName), error);
}

TEST(ExtensionL10nUtil, LocalizeManifestWithoutNameMsgAndEmptyDescription) {
  base::DictionaryValue manifest;
  manifest.SetStringKey(keys::kName, "no __MSG");
  std::string error;
  std::unique_ptr<MessageBundle> messages(CreateManifestBundle());

  EXPECT_TRUE(
      extension_l10n_util::LocalizeManifest(*messages, &manifest, &error));

  std::string result;
  ASSERT_TRUE(manifest.GetString(keys::kName, &result));
  EXPECT_EQ("no __MSG", result);

  EXPECT_FALSE(manifest.FindKey(keys::kDescription));

  EXPECT_TRUE(error.empty());
}

TEST(ExtensionL10nUtil, LocalizeManifestWithNameMsgAndEmptyDescription) {
  base::DictionaryValue manifest;
  manifest.SetStringKey(keys::kName, "__MSG_name__");
  std::string error;
  std::unique_ptr<MessageBundle> messages(CreateManifestBundle());

  EXPECT_TRUE(
      extension_l10n_util::LocalizeManifest(*messages, &manifest, &error));

  std::string result;
  ASSERT_TRUE(manifest.GetString(keys::kName, &result));
  EXPECT_EQ("name", result);

  EXPECT_FALSE(manifest.FindKey(keys::kDescription));

  EXPECT_TRUE(error.empty());
}

TEST(ExtensionL10nUtil, LocalizeManifestWithLocalLaunchURL) {
  base::DictionaryValue manifest;
  manifest.SetStringKey(keys::kName, "name");
  manifest.SetStringPath(keys::kLaunchLocalPath, "__MSG_launch_local_path__");
  std::string error;
  std::unique_ptr<MessageBundle> messages(CreateManifestBundle());

  EXPECT_TRUE(
      extension_l10n_util::LocalizeManifest(*messages, &manifest, &error));

  std::string result;
  ASSERT_TRUE(manifest.GetString(keys::kLaunchLocalPath, &result));
  EXPECT_EQ("main.html", result);

  EXPECT_TRUE(error.empty());
}

TEST(ExtensionL10nUtil, LocalizeManifestWithHostedLaunchURL) {
  base::DictionaryValue manifest;
  manifest.SetStringPath(keys::kName, "name");
  manifest.SetStringPath(keys::kLaunchWebURL, "__MSG_launch_web_url__");
  std::string error;
  std::unique_ptr<MessageBundle> messages(CreateManifestBundle());

  EXPECT_TRUE(
      extension_l10n_util::LocalizeManifest(*messages, &manifest, &error));

  std::string result;
  ASSERT_TRUE(manifest.GetString(keys::kLaunchWebURL, &result));
  EXPECT_EQ("http://www.google.com/", result);

  EXPECT_TRUE(error.empty());
}

TEST(ExtensionL10nUtil, LocalizeManifestWithBadNameMsg) {
  base::DictionaryValue manifest;
  manifest.SetStringKey(keys::kName, "__MSG_name_is_bad__");
  manifest.SetStringKey(keys::kDescription, "__MSG_description__");
  std::string error;
  std::unique_ptr<MessageBundle> messages(CreateManifestBundle());

  EXPECT_FALSE(
      extension_l10n_util::LocalizeManifest(*messages, &manifest, &error));

  std::string result;
  ASSERT_TRUE(manifest.GetString(keys::kName, &result));
  EXPECT_EQ("__MSG_name_is_bad__", result);

  ASSERT_TRUE(manifest.GetString(keys::kDescription, &result));
  EXPECT_EQ("__MSG_description__", result);

  EXPECT_EQ("Variable __MSG_name_is_bad__ used but not defined.", error);
}

TEST(ExtensionL10nUtil, LocalizeManifestWithNameDescriptionDefaultTitleMsgs) {
  base::DictionaryValue manifest;
  manifest.SetStringKey(keys::kName, "__MSG_name__");
  manifest.SetStringKey(keys::kDescription, "__MSG_description__");
  std::string action_title(keys::kBrowserAction);
  action_title.append(".");
  action_title.append(keys::kActionDefaultTitle);
  manifest.SetStringPath(action_title, "__MSG_title__");

  std::string error;
  std::unique_ptr<MessageBundle> messages(CreateManifestBundle());

  EXPECT_TRUE(
      extension_l10n_util::LocalizeManifest(*messages, &manifest, &error));

  std::string result;
  ASSERT_TRUE(manifest.GetString(keys::kName, &result));
  EXPECT_EQ("name", result);

  ASSERT_TRUE(manifest.GetString(keys::kDescription, &result));
  EXPECT_EQ("description", result);

  ASSERT_TRUE(manifest.GetString(action_title, &result));
  EXPECT_EQ("action title", result);

  EXPECT_TRUE(error.empty());
}

TEST(ExtensionL10nUtil, LocalizeManifestWithNameDescriptionOmniboxMsgs) {
  base::DictionaryValue manifest;
  manifest.SetStringKey(keys::kName, "__MSG_name__");
  manifest.SetStringKey(keys::kDescription, "__MSG_description__");
  manifest.SetStringPath(keys::kOmniboxKeyword, "__MSG_omnibox_keyword__");

  std::string error;
  std::unique_ptr<MessageBundle> messages(CreateManifestBundle());

  EXPECT_TRUE(
      extension_l10n_util::LocalizeManifest(*messages, &manifest, &error));

  std::string result;
  ASSERT_TRUE(manifest.GetString(keys::kName, &result));
  EXPECT_EQ("name", result);

  ASSERT_TRUE(manifest.GetString(keys::kDescription, &result));
  EXPECT_EQ("description", result);

  ASSERT_TRUE(manifest.GetString(keys::kOmniboxKeyword, &result));
  EXPECT_EQ("omnibox keyword", result);

  EXPECT_TRUE(error.empty());
}

TEST(ExtensionL10nUtil, LocalizeManifestWithNameDescriptionFileHandlerTitle) {
  base::DictionaryValue manifest;
  manifest.SetStringKey(keys::kName, "__MSG_name__");
  manifest.SetStringKey(keys::kDescription, "__MSG_description__");

  base::DictionaryValue handler;
  handler.SetStringKey(keys::kActionDefaultTitle, "__MSG_file_handler_title__");
  base::Value handlers(base::Value::Type::LIST);
  handlers.Append(std::move(handler));
  manifest.SetKey(keys::kFileBrowserHandlers, std::move(handlers));

  std::string error;
  std::unique_ptr<MessageBundle> messages(CreateManifestBundle());

  EXPECT_TRUE(
      extension_l10n_util::LocalizeManifest(*messages, &manifest, &error));

  std::string result;
  ASSERT_TRUE(manifest.GetString(keys::kName, &result));
  EXPECT_EQ("name", result);

  ASSERT_TRUE(manifest.GetString(keys::kDescription, &result));
  EXPECT_EQ("description", result);

  base::ListValue* handlers_raw = nullptr;
  manifest.GetList(keys::kFileBrowserHandlers, &handlers_raw);
  base::DictionaryValue* handler_raw = nullptr;
  handlers_raw->GetList()[0].GetAsDictionary(&handler_raw);
  ASSERT_TRUE(handler_raw->GetString(keys::kActionDefaultTitle, &result));
  EXPECT_EQ("file handler title", result);

  EXPECT_TRUE(error.empty());
}

TEST(ExtensionL10nUtil, LocalizeManifestWithNameDescriptionCommandDescription) {
  base::DictionaryValue manifest;
  manifest.SetStringKey(keys::kName, "__MSG_name__");
  manifest.SetStringKey(keys::kDescription, "__MSG_description__");
  base::Value commands(base::Value::Type::DICTIONARY);
  std::string commands_title(keys::kCommands);

  base::DictionaryValue first_command;
  first_command.SetStringKey(keys::kDescription,
                             "__MSG_first_command_description__");
  commands.SetKey("first_command", std::move(first_command));

  base::DictionaryValue second_command;
  second_command.SetStringKey(keys::kDescription,
                              "__MSG_second_command_description__");
  commands.SetKey("second_command", std::move(second_command));
  manifest.SetKey(commands_title, std::move(commands));

  std::string error;
  std::unique_ptr<MessageBundle> messages(CreateManifestBundle());

  EXPECT_TRUE(
      extension_l10n_util::LocalizeManifest(*messages, &manifest, &error));

  std::string result;
  ASSERT_TRUE(manifest.GetString(keys::kName, &result));
  EXPECT_EQ("name", result);

  ASSERT_TRUE(manifest.GetString(keys::kDescription, &result));
  EXPECT_EQ("description", result);

  ASSERT_TRUE(
      manifest.GetString("commands.first_command.description", &result));
  EXPECT_EQ("first command", result);

  ASSERT_TRUE(
      manifest.GetString("commands.second_command.description", &result));
  EXPECT_EQ("second command", result);

  EXPECT_TRUE(error.empty());
}

TEST(ExtensionL10nUtil, LocalizeManifestWithShortName) {
  base::DictionaryValue manifest;
  manifest.SetStringKey(keys::kName, "extension name");
  manifest.SetStringKey(keys::kShortName, "__MSG_short_name__");

  std::string error;
  std::unique_ptr<MessageBundle> messages(CreateManifestBundle());

  EXPECT_TRUE(
      extension_l10n_util::LocalizeManifest(*messages, &manifest, &error));
  EXPECT_TRUE(error.empty());

  std::string result;
  ASSERT_TRUE(manifest.GetString(keys::kShortName, &result));
  EXPECT_EQ("short_name", result);
}

TEST(ExtensionL10nUtil, LocalizeManifestWithBadShortName) {
  base::DictionaryValue manifest;
  manifest.SetStringKey(keys::kName, "extension name");
  manifest.SetStringKey(keys::kShortName, "__MSG_short_name_bad__");

  std::string error;
  std::unique_ptr<MessageBundle> messages(CreateManifestBundle());

  EXPECT_FALSE(
      extension_l10n_util::LocalizeManifest(*messages, &manifest, &error));
  EXPECT_FALSE(error.empty());

  std::string result;
  ASSERT_TRUE(manifest.GetString(keys::kShortName, &result));
  EXPECT_EQ("__MSG_short_name_bad__", result);
}

TEST(ExtensionL10nUtil, LocalizeManifestWithSearchProviderMsgs) {
  base::DictionaryValue manifest;
  manifest.SetStringKey(keys::kName, "__MSG_name__");
  manifest.SetStringKey(keys::kDescription, "__MSG_description__");

  base::Value search_provider(base::Value::Type::DICTIONARY);
  search_provider.SetStringKey("name", "__MSG_country__");
  search_provider.SetStringKey("keyword", "__MSG_omnibox_keyword__");
  search_provider.SetStringKey("search_url", "http://www.foo.__MSG_country__");
  search_provider.SetStringKey("favicon_url", "http://www.foo.__MSG_country__");
  search_provider.SetStringKey("suggest_url", "http://www.foo.__MSG_country__");
  manifest.SetPath(keys::kOverrideSearchProvider, std::move(search_provider));

  manifest.SetStringPath(keys::kOverrideHomepage,
                         "http://www.foo.__MSG_country__");

  base::Value startup_pages(base::Value::Type::LIST);
  startup_pages.Append("http://www.foo.__MSG_country__");
  manifest.SetPath(keys::kOverrideStartupPage, std::move(startup_pages));

  std::string error;
  std::unique_ptr<MessageBundle> messages(CreateManifestBundle());

  EXPECT_TRUE(
      extension_l10n_util::LocalizeManifest(*messages, &manifest, &error));

  std::string result;
  ASSERT_TRUE(manifest.GetString(keys::kName, &result));
  EXPECT_EQ("name", result);

  ASSERT_TRUE(manifest.GetString(keys::kDescription, &result));
  EXPECT_EQ("description", result);

  std::string key_prefix(keys::kOverrideSearchProvider);
  key_prefix += '.';
  ASSERT_TRUE(manifest.GetString(key_prefix + "name", &result));
  EXPECT_EQ("de", result);

  ASSERT_TRUE(manifest.GetString(key_prefix + "keyword", &result));
  EXPECT_EQ("omnibox keyword", result);

  ASSERT_TRUE(manifest.GetString(key_prefix + "search_url", &result));
  EXPECT_EQ("http://www.foo.de", result);

  ASSERT_TRUE(manifest.GetString(key_prefix + "favicon_url", &result));
  EXPECT_EQ("http://www.foo.de", result);

  ASSERT_TRUE(manifest.GetString(key_prefix + "suggest_url", &result));
  EXPECT_EQ("http://www.foo.de", result);

  ASSERT_TRUE(manifest.GetString(keys::kOverrideHomepage, &result));
  EXPECT_EQ("http://www.foo.de", result);

  base::ListValue* startup_pages_raw = nullptr;
  ASSERT_TRUE(manifest.GetList(keys::kOverrideStartupPage, &startup_pages_raw));
  base::Value::List& startup_pages_raw_list = startup_pages_raw->GetList();
  ASSERT_FALSE(startup_pages_raw_list.empty());
  ASSERT_TRUE(startup_pages_raw_list[0].is_string());
  EXPECT_EQ("http://www.foo.de", startup_pages_raw_list[0].GetString());

  EXPECT_TRUE(error.empty());
}

// Tests that we don't relocalize with a null manifest.
TEST(ExtensionL10nUtil, ShouldRelocalizeManifestWithNullManifest) {
  EXPECT_FALSE(extension_l10n_util::ShouldRelocalizeManifest(NULL));
}

// Tests that we don't relocalize with default and current locales missing.
TEST(ExtensionL10nUtil, ShouldRelocalizeManifestEmptyManifest) {
  base::DictionaryValue manifest;
  EXPECT_FALSE(extension_l10n_util::ShouldRelocalizeManifest(&manifest));
}

// Tests that we relocalize without a current locale.
TEST(ExtensionL10nUtil, ShouldRelocalizeManifestWithDefaultLocale) {
  base::DictionaryValue manifest;
  manifest.SetStringKey(keys::kDefaultLocale, "en_US");
  EXPECT_TRUE(extension_l10n_util::ShouldRelocalizeManifest(&manifest));
}

// Tests that we don't relocalize without a default locale.
TEST(ExtensionL10nUtil, ShouldRelocalizeManifestWithCurrentLocale) {
  extension_l10n_util::ScopedLocaleForTest scoped_locale("en-US");
  base::DictionaryValue manifest;
  manifest.SetStringKey(keys::kCurrentLocale, "en_US");
  EXPECT_FALSE(extension_l10n_util::ShouldRelocalizeManifest(&manifest));
}

// Tests that we don't relocalize with same current_locale as system locale.
TEST(ExtensionL10nUtil, ShouldRelocalizeManifestSameCurrentLocale) {
  extension_l10n_util::ScopedLocaleForTest scoped_locale("en-US");
  base::DictionaryValue manifest;
  manifest.SetStringKey(keys::kDefaultLocale, "en_US");
  manifest.SetStringKey(keys::kCurrentLocale, "en_US");
  EXPECT_FALSE(extension_l10n_util::ShouldRelocalizeManifest(&manifest));
}

// Tests that we relocalize with a different current_locale.
TEST(ExtensionL10nUtil, ShouldRelocalizeManifestDifferentCurrentLocale) {
  extension_l10n_util::ScopedLocaleForTest scoped_locale("en-US");
  base::DictionaryValue manifest;
  manifest.SetStringKey(keys::kDefaultLocale, "en_US");
  manifest.SetStringKey(keys::kCurrentLocale, "sr");
  EXPECT_TRUE(extension_l10n_util::ShouldRelocalizeManifest(&manifest));
}

// Tests that we don't relocalize with the same current_locale as preferred
// locale.
TEST(ExtensionL10nUtil, ShouldRelocalizeManifestSameCurrentLocaleAsPreferred) {
  extension_l10n_util::ScopedLocaleForTest scoped_locale("en-GB", "en-CA");
  base::DictionaryValue manifest;
  manifest.SetStringKey(keys::kDefaultLocale, "en_US");
  manifest.SetStringKey(keys::kCurrentLocale, "en_CA");

  // Preferred and current locale are both en_CA.
  EXPECT_FALSE(extension_l10n_util::ShouldRelocalizeManifest(&manifest));
}

// Tests that we relocalize with a different current_locale from the preferred
// locale.
TEST(ExtensionL10nUtil,
     ShouldRelocalizeManifestDifferentCurrentLocaleThanPreferred) {
  extension_l10n_util::ScopedLocaleForTest scoped_locale("en-GB", "en-CA");
  base::DictionaryValue manifest;
  manifest.SetStringKey(keys::kDefaultLocale, "en_US");
  manifest.SetStringKey(keys::kCurrentLocale, "en_GB");

  // Requires relocalization as the preferred (en_CA) differs from current
  // (en_GB).
  EXPECT_TRUE(extension_l10n_util::ShouldRelocalizeManifest(&manifest));
}

TEST(ExtensionL10nUtil, GetAllFallbackLocales) {
  extension_l10n_util::ScopedLocaleForTest scoped_locale("en-US");
  std::vector<std::string> fallback_locales;
  extension_l10n_util::GetAllFallbackLocales("all", &fallback_locales);
  ASSERT_EQ(3U, fallback_locales.size());

  EXPECT_EQ("en_US", fallback_locales[0]);
  EXPECT_EQ("en", fallback_locales[1]);
  EXPECT_EQ("all", fallback_locales[2]);
}

TEST(ExtensionL10nUtil, GetAllFallbackLocalesWithPreferredLocale) {
  extension_l10n_util::ScopedLocaleForTest scoped_locale("en-GB", "en-CA");
  std::vector<std::string> fallback_locales;
  extension_l10n_util::GetAllFallbackLocales("all", &fallback_locales);
  ASSERT_EQ(4U, fallback_locales.size());

  EXPECT_EQ("en_CA", fallback_locales[0]);
  EXPECT_EQ("en_GB", fallback_locales[1]);
  EXPECT_EQ("en", fallback_locales[2]);
  EXPECT_EQ("all", fallback_locales[3]);
}

}  // namespace
}  // namespace extensions
