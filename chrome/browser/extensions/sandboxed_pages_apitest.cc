// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/threading/thread_restrictions.h"
#include "chrome/browser/extensions/extension_apitest.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/test/browser_test.h"
#include "extensions/common/constants.h"
#include "extensions/common/file_util.h"
#include "extensions/test/test_extension_dir.h"

namespace extensions {

enum class ManifestVersion { TWO, THREE };

class SandboxedPagesTest
    : public ExtensionApiTest,
      public ::testing::WithParamInterface<ManifestVersion> {
 public:
  SandboxedPagesTest() = default;

  [[nodiscard]] bool RunTest(const char* extension_name,
                             const char* manifest,
                             const RunOptions& run_options,
                             const LoadOptions& load_options) {
    const char* kCustomArg =
        GetParam() == ManifestVersion::TWO ? "manifest_v2" : "manifest_v3";
    SetCustomArg(kCustomArg);

    base::ScopedAllowBlockingForTesting scoped_allow_blocking;

    //  Load the extension with the given `manifest`.
    if (!temp_dir_.CreateUniqueTempDir()) {
      ADD_FAILURE() << "Could not create temporary dir for test";
      return false;
    }

    base::FilePath source_extension_path =
        test_data_dir_.AppendASCII(extension_name);
    base::FilePath destination_extension_path =
        temp_dir_.GetPath().AppendASCII(extension_name);
    if (!base::CopyDirectory(source_extension_path, destination_extension_path,
                             true /* recursive */)) {
      ADD_FAILURE() << source_extension_path.value()
                    << " could not be copied to "
                    << destination_extension_path.value();
      return false;
    }

    test_data_dir_ = temp_dir_.GetPath();
    base::FilePath manifest_path =
        destination_extension_path.Append(kManifestFilename);
    if (!base::WriteFile(manifest_path, manifest)) {
      ADD_FAILURE() << "Could not write manifest file to "
                    << manifest_path.value();
      return false;
    }

    return RunExtensionTest(extension_name, run_options, load_options);
  }

 private:
  base::ScopedTempDir temp_dir_;
};

IN_PROC_BROWSER_TEST_P(SandboxedPagesTest, SandboxedPages) {
  const char* kManifestV2 = R"(
    {
      "name": "Extension with sandboxed pages",
      "manifest_version": 2,
      "version": "0.1",
      "sandbox": {
        "pages": ["sandboxed.html"]
      }
    }
  )";
  const char* kManifestV3 = R"(
    {
      "name": "Extension with sandboxed pages",
      "manifest_version": 3,
      "version": "0.1",
      "sandbox": {
        "pages": ["sandboxed.html"]
      }
    }
  )";
  const char* kManifest =
      GetParam() == ManifestVersion::TWO ? kManifestV2 : kManifestV3;
  EXPECT_TRUE(
      RunTest("sandboxed_pages", kManifest, {.page_url = "main.html"}, {}))
      << message_;
}

IN_PROC_BROWSER_TEST_P(SandboxedPagesTest, SandboxedPagesCSP) {
  ASSERT_TRUE(StartEmbeddedTestServer());

  const char* kManifestV2 = R"(
    {
      "name": "Tests that loading web content fails inside sandboxed pages",
      "manifest_version": 2,
      "version": "0.1",
      "web_accessible_resources": ["local_frame.html", "remote_frame.html"],
      "sandbox": {
        "pages": ["sandboxed.html"],
        "content_security_policy": "sandbox allow-scripts; child-src *;"
      }
    }
  )";

  const char* kManifestV3 = R"(
    {
      "name": "Tests that loading web content fails inside sandboxed pages",
      "manifest_version": 3,
      "version": "0.1",
      "web_accessible_resources": [{
        "resources" : ["local_frame.html", "remote_frame.html"],
        "matches": ["<all_urls>"]
      }],
      "sandbox": {
        "pages": ["sandboxed.html"]
      },
      "content_security_policy": {
        "sandbox": "sandbox allow-scripts; child-src *;"
      }
    }
  )";
  const char* kManifest =
      GetParam() == ManifestVersion::TWO ? kManifestV2 : kManifestV3;
  // This extension attempts to load remote web content inside a sandboxed page.
  // Loading web content will fail because of CSP. In addition to that we will
  // show manifest warnings, hence ignore_manifest_warnings is set to true.
  ASSERT_TRUE(RunTest("sandboxed_pages_csp", kManifest,
                      {.page_url = "main.html"},
                      {.ignore_manifest_warnings = true}))
      << message_;
}

INSTANTIATE_TEST_SUITE_P(,
                         SandboxedPagesTest,
                         ::testing::Values(ManifestVersion::TWO,
                                           ManifestVersion::THREE));

// Verify sandbox behavior.
IN_PROC_BROWSER_TEST_F(SandboxedPagesTest, WebAccessibleResourcesTest) {
  ASSERT_TRUE(embedded_test_server()->Start());

  // Install extension.
  TestExtensionDir extension_dir;
  constexpr char kManifest[] = R"({
    "name": "Extension sandbox text",
    "version": "1.0",
    "manifest_version": 2,
    "sandbox": {
      "pages": ["sandboxed_page.html"]
    },
    "web_accessible_resources": [
      "web_accessible_resource.html"
    ]
  })";
  extension_dir.WriteManifest(kManifest);
  extension_dir.WriteFile(FILE_PATH_LITERAL("sandboxed_page.html"), "");
  extension_dir.WriteFile(FILE_PATH_LITERAL("page.html"), "");
  extension_dir.WriteFile(FILE_PATH_LITERAL("resource.html"), "resource.html");
  extension_dir.WriteFile(FILE_PATH_LITERAL("web_accessible_resource.html"),
                          "web_accessible_resource.html");
  const Extension* extension = LoadExtension(extension_dir.UnpackedPath());
  ASSERT_TRUE(extension);

  // Fetch url from frame to verify histograms match expectations.
  auto test_frame_with_fetch = [&](const char* frame_url, const char* fetch_url,
                                   bool is_web_accessible_resource, int count,
                                   std::string expected_frame_origin) {
    // Prepare histogram.
    base::HistogramTester histograms;
    const char* kHistogramName =
        "Extensions.SandboxedPageLoad.IsWebAccessibleResource";

    // Fetch and test resource.
    ASSERT_TRUE(ui_test_utils::NavigateToURL(
        browser(), extension->GetResourceURL(frame_url)));
    content::WebContents* web_contents =
        browser()->tab_strip_model()->GetActiveWebContents();
    std::string result;
    constexpr char kFetchScriptTemplate[] =
        R"(
        fetch($1).then(result => {
          return result.text();
        }).then(text => {
          domAutomationController.send(text);
        }).catch(err => {
          domAutomationController.send(String(err));
        });)";
    EXPECT_TRUE(content::ExecuteScriptAndExtractString(
        web_contents,
        content::JsReplace(kFetchScriptTemplate,
                           extension->GetResourceURL(fetch_url)),
        &result));
    EXPECT_EQ(result, fetch_url);
    histograms.ExpectBucketCount(kHistogramName, is_web_accessible_resource,
                                 count);
    EXPECT_EQ(expected_frame_origin, web_contents->GetPrimaryMainFrame()
                                         ->GetLastCommittedOrigin()
                                         .Serialize());
  };

  // Extension page fetching an extension file.
  test_frame_with_fetch("page.html", "resource.html", false, 0,
                        extension->origin().Serialize());

  // Extension page fetching a web accessible resource.
  test_frame_with_fetch("page.html", "web_accessible_resource.html", true, 0,
                        extension->origin().Serialize());

  // Sandboxed extension page fetching an extension file.
  test_frame_with_fetch("sandboxed_page.html", "resource.html", false, 1,
                        "null");

  // Sandboxed extension page fetching a web accessible resource.
  test_frame_with_fetch("sandboxed_page.html", "web_accessible_resource.html",
                        true, 1, "null");
}

}  // namespace extensions
