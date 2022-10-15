// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "extensions/browser/api_unittest.h"

#include "base/values.h"
#include "components/user_prefs/user_prefs.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/content_browser_client.h"
#include "content/public/browser/site_instance.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/content_client.h"
#include "content/public/common/url_constants.h"
#include "content/public/test/test_browser_context.h"
#include "content/public/test/web_contents_tester.h"
#include "extensions/browser/api_test_utils.h"
#include "extensions/browser/extension_function.h"
#include "extensions/browser/test_extensions_browser_client.h"
#include "extensions/common/extension.h"
#include "extensions/common/extension_builder.h"
#include "extensions/common/manifest.h"
#include "extensions/common/manifest_handlers/background_info.h"
#include "extensions/common/value_builder.h"

namespace utils = extensions::api_test_utils;

namespace extensions {

ApiUnitTest::ApiUnitTest() = default;

ApiUnitTest::~ApiUnitTest() = default;

void ApiUnitTest::SetUp() {
  ExtensionsTest::SetUp();

  user_prefs::UserPrefs::Set(browser_context(), &testing_pref_service_);

  extension_ = ExtensionBuilder("Test").Build();
}

void ApiUnitTest::TearDown() {
  extension_ = nullptr;
  contents_.reset();
  ExtensionsTest::TearDown();
}

void ApiUnitTest::CreateBackgroundPage() {
  if (!contents_) {
    GURL url = BackgroundInfo::GetBackgroundURL(extension());
    if (url.is_empty())
      url = GURL(url::kAboutBlankURL);
    contents_ = content::WebContents::Create(content::WebContents::CreateParams(
        browser_context(),
        content::SiteInstance::CreateForURL(browser_context(), url)));
  }
}

std::unique_ptr<base::Value> ApiUnitTest::RunFunctionAndReturnValue(
    ExtensionFunction* function,
    const std::string& args) {
  function->set_extension(extension());
  if (contents_)
    function->SetRenderFrameHost(contents_->GetPrimaryMainFrame());
  return std::unique_ptr<base::Value>(utils::RunFunctionAndReturnSingleResult(
      function, args, browser_context()));
}

std::unique_ptr<base::DictionaryValue>
ApiUnitTest::RunFunctionAndReturnDictionary(ExtensionFunction* function,
                                            const std::string& args) {
  base::Value* value = RunFunctionAndReturnValue(function, args).release();
  base::DictionaryValue* dict = nullptr;

  if (value && !value->GetAsDictionary(&dict))
    delete value;

  // We expect to either have successfully retrieved a dictionary from the
  // value, or the value to have been NULL.
  EXPECT_TRUE(dict || !value);
  return std::unique_ptr<base::DictionaryValue>(dict);
}

std::unique_ptr<base::Value> ApiUnitTest::RunFunctionAndReturnList(
    ExtensionFunction* function,
    const std::string& args) {
  base::Value* value = RunFunctionAndReturnValue(function, args).release();

  // We expect to either have successfully gotten a list value, or the value to
  // have been NULL.
  EXPECT_TRUE(!value || value->is_list());
  if (value && !value->is_list())
    delete value;

  return std::unique_ptr<base::Value>(value);
}

std::string ApiUnitTest::RunFunctionAndReturnError(ExtensionFunction* function,
                                                   const std::string& args) {
  function->set_extension(extension());
  if (contents_)
    function->SetRenderFrameHost(contents_->GetPrimaryMainFrame());
  return utils::RunFunctionAndReturnError(function, args, browser_context());
}

void ApiUnitTest::RunFunction(ExtensionFunction* function,
                              const std::string& args) {
  RunFunctionAndReturnValue(function, args);
}

}  // namespace extensions
