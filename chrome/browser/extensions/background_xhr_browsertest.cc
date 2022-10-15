// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include "base/bind.h"
#include "base/json/json_reader.h"
#include "base/strings/escape.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "chrome/browser/extensions/extension_apitest.h"
#include "chrome/browser/extensions/extension_browsertest.h"
#include "chrome/browser/extensions/extension_with_management_policy_apitest.h"
#include "chrome/browser/net/profile_network_context_service.h"
#include "chrome/browser/net/profile_network_context_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/browser_task_traits.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "extensions/browser/browsertest_util.h"
#include "extensions/common/extension.h"
#include "extensions/common/extension_urls.h"
#include "extensions/test/extension_test_message_listener.h"
#include "extensions/test/result_catcher.h"
#include "extensions/test/test_extension_dir.h"
#include "net/base/url_util.h"
#include "net/dns/mock_host_resolver.h"
#include "net/ssl/client_cert_store.h"
#include "net/ssl/ssl_server_config.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "services/network/public/cpp/network_switches.h"
#include "url/gurl.h"

namespace extensions {

namespace {

std::unique_ptr<net::ClientCertStore> CreateNullCertStore() {
  return nullptr;
}

}  // namespace

class BackgroundXhrTest : public ExtensionBrowserTest {
 protected:
  void RunTest(const std::string& path, const GURL& url) {
    const Extension* extension =
        LoadExtension(test_data_dir_.AppendASCII("background_xhr"));
    ASSERT_TRUE(extension);

    ResultCatcher catcher;
    GURL test_url = net::AppendQueryParameter(extension->GetResourceURL(path),
                                              "url", url.spec());
    ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), test_url));
    profile()->GetDefaultStoragePartition()->FlushNetworkInterfaceForTesting();
    constexpr char kSendXHRScript[] = R"(
      var xhr = new XMLHttpRequest();
      xhr.open('GET', '%s');
      xhr.send();
      domAutomationController.send('');
    )";
    browsertest_util::ExecuteScriptInBackgroundPage(
        profile(), extension->id(),
        base::StringPrintf(kSendXHRScript, url.spec().c_str()));
    ASSERT_TRUE(catcher.GetNextResult());
  }
};

// Test that fetching a URL using TLS client auth doesn't crash, hang, or
// prompt.
IN_PROC_BROWSER_TEST_F(BackgroundXhrTest, TlsClientAuth) {
  // Install a null ClientCertStore so the client auth prompt isn't bypassed due
  // to the system certificate store returning no certificates.
  ProfileNetworkContextServiceFactory::GetForContext(browser()->profile())
      ->set_client_cert_store_factory_for_testing(
          base::BindRepeating(&CreateNullCertStore));

  // Launch HTTPS server.
  net::EmbeddedTestServer https_server(net::EmbeddedTestServer::TYPE_HTTPS);
  net::SSLServerConfig ssl_config;
  ssl_config.client_cert_type =
      net::SSLServerConfig::ClientCertType::REQUIRE_CLIENT_CERT;
  https_server.SetSSLConfig(net::EmbeddedTestServer::CERT_OK, ssl_config);
  https_server.ServeFilesFromSourceDirectory("content/test/data");
  ASSERT_TRUE(https_server.Start());

  ASSERT_NO_FATAL_FAILURE(
      RunTest("test_tls_client_auth.html", https_server.GetURL("/")));
}

// Test that fetching a URL using HTTP auth doesn't crash, hang, or prompt.
IN_PROC_BROWSER_TEST_F(BackgroundXhrTest, HttpAuth) {
  ASSERT_TRUE(embedded_test_server()->Start());
  ASSERT_NO_FATAL_FAILURE(RunTest(
      "test_http_auth.html", embedded_test_server()->GetURL("/auth-basic")));
}

class BackgroundXhrPolicyTest : public ExtensionApiTestWithManagementPolicy {
 public:
  void SetUpCommandLine(base::CommandLine* command_line) override {
    ExtensionApiTest::SetUpCommandLine(command_line);
    // Note: we need to start the embedded test server here specifically as it
    // needs to come after SetUp has been run in the superclass, but before any
    // subclasses need it in their own SetUpCommandLine functions.
    ASSERT_TRUE(embedded_test_server()->Start());
  }

  void SetUpOnMainThread() override {
    ExtensionApiTest::SetUpOnMainThread();
    host_resolver()->AddRule("*", "127.0.0.1");
  }

  std::string ExecuteFetch(const Extension* extension, const GURL& url) {
    ExtensionHost* host =
        ProcessManager::Get(profile())->GetBackgroundHostForExtension(
            extension->id());
    if (!host) {
      ADD_FAILURE() << "No background page found.";
      return "";
    }
    content::DOMMessageQueue message_queue(host->host_contents());

    browsertest_util::ExecuteScriptInBackgroundPageNoWait(
        profile(), extension->id(),
        content::JsReplace("executeFetch($1);", url));

    std::string json;
    EXPECT_TRUE(message_queue.WaitForMessage(&json));
    absl::optional<base::Value> value =
        base::JSONReader::Read(json, base::JSON_ALLOW_TRAILING_COMMAS);
    EXPECT_TRUE(value->is_string());
    std::string trimmed_result;
    base::TrimWhitespaceASCII(value->GetString(), base::TRIM_ALL,
                              &trimmed_result);
    return trimmed_result;
  }

  const Extension* LoadXhrExtension(const std::string& host) {
    ExtensionTestMessageListener listener("ready");
    TestExtensionDir test_dir;
    test_dir.WriteManifest(R"(
    {
      "name": "XHR Test",
      "manifest_version": 2,
      "version": "0.1",
      "background": {"scripts": ["background.js"]},
      "permissions": [")" + host + R"("]
    })");
    constexpr char kBackgroundScriptFile[] = R"(
    function executeFetch(url) {
      console.warn('Fetching: ' + url);
      fetch(url)
          .then(response => response.text())
          .then(text => domAutomationController.send(text))
          .catch(err => domAutomationController.send('ERROR: ' + err));
    }
    chrome.test.sendMessage('ready');)";

    test_dir.WriteFile(FILE_PATH_LITERAL("background.js"),
                       kBackgroundScriptFile);
    const Extension* extension = LoadExtension(test_dir.UnpackedPath());
    EXPECT_TRUE(listener.WaitUntilSatisfied());
    return extension;
  }
};

// Extensions should not be able to bypass same-origin despite declaring
// <all_urls> for hosts restricted by enterprise policy.
IN_PROC_BROWSER_TEST_F(BackgroundXhrPolicyTest, PolicyBlockedXHR) {
  {
    ExtensionManagementPolicyUpdater pref(&policy_provider_);
    pref.AddPolicyBlockedHost("*", "*://*.example.com");
    pref.AddPolicyAllowedHost("*", "*://public.example.com");
  }

  const Extension* extension = LoadXhrExtension("<all_urls>");

  // Should block due to "runtime_blocked_hosts" section of policy.
  GURL protected_url_to_fetch =
      embedded_test_server()->GetURL("example.com", "/simple.html");
  EXPECT_EQ("ERROR: TypeError: Failed to fetch",
            ExecuteFetch(extension, protected_url_to_fetch));

  // Should allow due to "runtime_allowed_hosts" section of policy.
  GURL exempted_url_to_fetch =
      embedded_test_server()->GetURL("public.example.com", "/simple.html");
  EXPECT_THAT(ExecuteFetch(extension, exempted_url_to_fetch),
              ::testing::HasSubstr("<head><title>OK</title></head>"));
}

// Make sure the blocklist and allowlist update for both Default and Individual
// scope policies. Testing with all host permissions granted (<all_urls>).
IN_PROC_BROWSER_TEST_F(BackgroundXhrPolicyTest, PolicyUpdateXHR) {
  const Extension* extension = LoadXhrExtension("<all_urls>");

  GURL example_url =
      embedded_test_server()->GetURL("example.com", "/simple.html");
  GURL public_example_url =
      embedded_test_server()->GetURL("public.example.com", "/simple.html");

  // Sanity check: Without restrictions all fetches should work.
  EXPECT_THAT(ExecuteFetch(extension, public_example_url),
              ::testing::HasSubstr("<head><title>OK</title></head>"));
  EXPECT_THAT(ExecuteFetch(extension, example_url),
              ::testing::HasSubstr("<head><title>OK</title></head>"));

  {
    ExtensionManagementPolicyUpdater pref(&policy_provider_);
    pref.AddPolicyBlockedHost("*", "*://*.example.com");
    pref.AddPolicyAllowedHost("*", "*://public.example.com");
  }

  // Default policies propagate.
  EXPECT_THAT(ExecuteFetch(extension, public_example_url),
              ::testing::HasSubstr("<head><title>OK</title></head>"));
  EXPECT_EQ("ERROR: TypeError: Failed to fetch",
            ExecuteFetch(extension, example_url));
  {
    ExtensionManagementPolicyUpdater pref(&policy_provider_);
    pref.AddPolicyBlockedHost(extension->id(), "*://*.example2.com");
    pref.AddPolicyAllowedHost(extension->id(), "*://public.example2.com");
  }

  // Default policies overridden when individual scope policies applied.
  EXPECT_THAT(ExecuteFetch(extension, public_example_url),
              ::testing::HasSubstr("<head><title>OK</title></head>"));
  EXPECT_THAT(ExecuteFetch(extension, example_url),
              ::testing::HasSubstr("<head><title>OK</title></head>"));

  GURL example2_url =
      embedded_test_server()->GetURL("example2.com", "/simple.html");
  GURL public_example2_url =
      embedded_test_server()->GetURL("public.example2.com", "/simple.html");

  // Individual scope policies propagate.
  EXPECT_THAT(ExecuteFetch(extension, public_example2_url),
              ::testing::HasSubstr("<head><title>OK</title></head>"));
  EXPECT_EQ("ERROR: TypeError: Failed to fetch",
            ExecuteFetch(extension, example2_url));
}

// Make sure the allowlist entries added due to host permissions are removed
// when a more generic blocklist policy is updated and contains them.
// This tests the default policy scope update.
IN_PROC_BROWSER_TEST_F(BackgroundXhrPolicyTest, PolicyUpdateDefaultXHR) {
  const Extension* extension = LoadXhrExtension("*://public.example.com/*");

  GURL example_url =
      embedded_test_server()->GetURL("example.com", "/simple.html");
  GURL public_example_url =
      embedded_test_server()->GetURL("public.example.com", "/simple.html");

  // Sanity check: Without restrictions only public.example.com should work.
  EXPECT_THAT(ExecuteFetch(extension, public_example_url),
              ::testing::HasSubstr("<head><title>OK</title></head>"));
  EXPECT_EQ("ERROR: TypeError: Failed to fetch",
            ExecuteFetch(extension, example_url));

  {
    ExtensionManagementPolicyUpdater pref(&policy_provider_);
    pref.AddPolicyBlockedHost("*", "*://*.example.com");
  }

  // The blocklist of example.com overrides allowlist of public.example.com.
  EXPECT_EQ("ERROR: TypeError: Failed to fetch",
            ExecuteFetch(extension, example_url));
  EXPECT_EQ("ERROR: TypeError: Failed to fetch",
            ExecuteFetch(extension, public_example_url));
}

// Make sure the allowlist entries added due to host permissions are removed
// when a more generic blocklist policy is updated and contains them.
// This tests an individual policy scope update.
IN_PROC_BROWSER_TEST_F(BackgroundXhrPolicyTest, PolicyUpdateIndividualXHR) {
  const Extension* extension = LoadXhrExtension("*://public.example.com/*");

  GURL example_url =
      embedded_test_server()->GetURL("example.com", "/simple.html");
  GURL public_example_url =
      embedded_test_server()->GetURL("public.example.com", "/simple.html");

  // Sanity check: Without restrictions only public.example.com should work.
  EXPECT_THAT(ExecuteFetch(extension, public_example_url),
              ::testing::HasSubstr("<head><title>OK</title></head>"));
  EXPECT_EQ("ERROR: TypeError: Failed to fetch",
            ExecuteFetch(extension, example_url));

  {
    ExtensionManagementPolicyUpdater pref(&policy_provider_);
    pref.AddPolicyBlockedHost(extension->id(), "*://*.example.com");
  }

  // The blocklist of example.com overrides allowlist of public.example.com.
  EXPECT_EQ("ERROR: TypeError: Failed to fetch",
            ExecuteFetch(extension, example_url));
  EXPECT_EQ("ERROR: TypeError: Failed to fetch",
            ExecuteFetch(extension, public_example_url));
}

IN_PROC_BROWSER_TEST_F(BackgroundXhrPolicyTest, XHRAnyPortPermission) {
  const Extension* extension = LoadXhrExtension("http://example.com:*/*");

  GURL permitted_url_to_fetch =
      embedded_test_server()->GetURL("example.com", "/simple.html");

  EXPECT_THAT(ExecuteFetch(extension, permitted_url_to_fetch),
              ::testing::HasSubstr("<head><title>OK</title></head>"));
}

IN_PROC_BROWSER_TEST_F(BackgroundXhrPolicyTest,
                       XHRPortSpecificPermissionAllow) {
  const Extension* extension = LoadXhrExtension(
      "http://example.com:" +
      base::NumberToString(embedded_test_server()->port()) + "/*");

  GURL permitted_url_to_fetch =
      embedded_test_server()->GetURL("example.com", "/simple.html");

  EXPECT_THAT(ExecuteFetch(extension, permitted_url_to_fetch),
              ::testing::HasSubstr("<head><title>OK</title></head>"));
}

IN_PROC_BROWSER_TEST_F(BackgroundXhrPolicyTest,
                       XHRPortSpecificPermissionBlock) {
  const Extension* extension = LoadXhrExtension(
      "https://example.com:" +
      base::NumberToString(embedded_test_server()->port() + 1) + "/*");

  GURL not_permitted_url_to_fetch =
      embedded_test_server()->GetURL("example.com", "/simple.html");

  EXPECT_EQ("ERROR: TypeError: Failed to fetch",
            ExecuteFetch(extension, not_permitted_url_to_fetch));
}

// URL the new webstore is associated with in production.
constexpr char kNewWebstoreURL[] = "https://webstore.google.com/";
// URL the webstore hosted app is associated with in production, minus the
// /webstore/ path which is added in the tests themselves.
constexpr char kWebstoreAppBaseURL[] = "https://chrome.google.com/";
// URL to test the command line override for the webstore.
constexpr char kWebstoreOverrideURL[] = "https://chrome.webstore.test.com/";
constexpr char kNonWebstoreURL[] = "https://google.com";
constexpr char kWebstorePath[] = "/webstore/mock_store.html";

class BackgroundXhrWebstoreTest : public BackgroundXhrPolicyTest,
                                  public testing::WithParamInterface<GURL> {
 public:
  BackgroundXhrWebstoreTest() {
    UseHttpsTestServer();
    // Override the test server SSL config with the webstore domain under test
    // and another non-webstore domain used in the tests.
    net::EmbeddedTestServer::ServerCertificateConfig cert_config;
    cert_config.dns_names = {GetParam().host(), "google.com"};
    embedded_test_server()->SetSSLConfig(cert_config);
    // Add the extensions directory to the test server as it has a /webstore/
    // directory to serve files from, which the webstore hosted app requires as
    // part of the URL it is associated with.
    embedded_test_server()->ServeFilesFromSourceDirectory(
        "chrome/test/data/extensions");
  }
  ~BackgroundXhrWebstoreTest() override = default;

  void SetUpCommandLine(base::CommandLine* command_line) override {
    BackgroundXhrPolicyTest::SetUpCommandLine(command_line);
    // Add a host resolver rule to map all outgoing requests to the test server.
    // This allows us to use "real" hostnames and standard ports in URLs (i.e.,
    // without having to inject the port number into all URLs).
    command_line->AppendSwitchASCII(
        network::switches::kHostResolverRules,
        "MAP * " + embedded_test_server()->host_port_pair().ToString());
    // Only override the webstore URL if this test case is testing the override.
    if (GetParam().spec() == kWebstoreOverrideURL) {
      command_line->AppendSwitchASCII(::switches::kAppsGalleryURL,
                                      kWebstoreOverrideURL);
    }
  }
};

// Extensions should not be able to XHR to the webstore.
IN_PROC_BROWSER_TEST_P(BackgroundXhrWebstoreTest, XHRToWebstore) {
  const Extension* extension = LoadXhrExtension("<all_urls>");

  GURL webstore_url_to_fetch = GetParam().Resolve(kWebstorePath);

  EXPECT_EQ("ERROR: TypeError: Failed to fetch",
            ExecuteFetch(extension, webstore_url_to_fetch));

  // Sanity check: the extension should be able to fetch the page if it's not on
  // the webstore.
  GURL non_webstore_url = GURL(kNonWebstoreURL).Resolve(kWebstorePath);
  EXPECT_THAT(ExecuteFetch(extension, non_webstore_url),
              ::testing::HasSubstr("<body>blank</body>"));
}

// Extensions should not be able to XHR to the webstore regardless of policy.
IN_PROC_BROWSER_TEST_P(BackgroundXhrWebstoreTest, XHRToWebstorePolicy) {
  {
    ExtensionManagementPolicyUpdater pref(&policy_provider_);
    pref.AddPolicyAllowedHost(
        "*", "*://" + extension_urls::GetWebstoreLaunchURL().host());
  }

  const Extension* extension = LoadXhrExtension("<all_urls>");

  GURL webstore_url_to_fetch = GetParam().Resolve(kWebstorePath);

  EXPECT_EQ("ERROR: TypeError: Failed to fetch",
            ExecuteFetch(extension, webstore_url_to_fetch));

  // Sanity check: the extension should be able to fetch the page if it's not on
  // the webstore.
  GURL non_webstore_url = GURL(kNonWebstoreURL).Resolve(kWebstorePath);
  EXPECT_THAT(ExecuteFetch(extension, non_webstore_url),
              ::testing::HasSubstr("<body>blank</body>"));
}

INSTANTIATE_TEST_SUITE_P(WebstoreNewURL,
                         BackgroundXhrWebstoreTest,
                         testing::Values(GURL(kNewWebstoreURL)));
INSTANTIATE_TEST_SUITE_P(WebstoreHostedAppURL,
                         BackgroundXhrWebstoreTest,
                         testing::Values(GURL(kWebstoreAppBaseURL)));
INSTANTIATE_TEST_SUITE_P(WebstoreOverrideURL,
                         BackgroundXhrWebstoreTest,
                         testing::Values(GURL(kWebstoreOverrideURL)));

}  // namespace extensions
