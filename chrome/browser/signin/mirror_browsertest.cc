// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <map>
#include <memory>
#include <string>
#include <utility>

#include "base/base_switches.h"
#include "base/bind.h"
#include "base/callback.h"
#include "base/callback_helpers.h"
#include "base/command_line.h"
#include "base/containers/flat_map.h"
#include "base/path_service.h"
#include "base/run_loop.h"
#include "base/strings/string_util.h"
#include "base/test/bind.h"
#include "build/build_config.h"
#include "build/chromeos_buildflags.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/chrome_content_browser_client.h"
#include "chrome/browser/extensions/api/identity/web_auth_flow.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/signin/signin_util.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/google/core/common/google_switches.h"
#include "components/network_session_configurator/common/network_switches.h"
#include "components/prefs/pref_service.h"
#include "components/signin/core/browser/dice_header_helper.h"
#include "components/signin/core/browser/signin_header_helper.h"
#include "components/signin/public/base/signin_pref_names.h"
#include "content/public/common/content_client.h"
#include "content/public/test/browser_test.h"
#include "google_apis/gaia/gaia_switches.h"
#include "google_apis/gaia/gaia_urls.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/request_handler_util.h"
#include "third_party/blink/public/common/loader/url_loader_throttle.h"
#include "url/gurl.h"

#if BUILDFLAG(IS_CHROMEOS_LACROS)
#include "chrome/browser/lacros/account_manager/fake_account_manager_ui_dialog_waiter.h"
#endif

namespace {

// A delegate to insert a user generated X-Chrome-Connected header to a specific
// URL.
class HeaderModifyingThrottle : public blink::URLLoaderThrottle {
 public:
  HeaderModifyingThrottle() = default;

  HeaderModifyingThrottle(const HeaderModifyingThrottle&) = delete;
  HeaderModifyingThrottle& operator=(const HeaderModifyingThrottle&) = delete;

  ~HeaderModifyingThrottle() override = default;

  void WillStartRequest(network::ResourceRequest* request,
                        bool* defer) override {
    request->headers.SetHeader(signin::kChromeConnectedHeader, "User Data");
  }
};

class ThrottleContentBrowserClient : public ChromeContentBrowserClient {
 public:
  explicit ThrottleContentBrowserClient(const GURL& watch_url)
      : watch_url_(watch_url) {}

  ThrottleContentBrowserClient(const ThrottleContentBrowserClient&) = delete;
  ThrottleContentBrowserClient& operator=(const ThrottleContentBrowserClient&) =
      delete;

  ~ThrottleContentBrowserClient() override = default;

  // ContentBrowserClient overrides:
  std::vector<std::unique_ptr<blink::URLLoaderThrottle>>
  CreateURLLoaderThrottles(
      const network::ResourceRequest& request,
      content::BrowserContext* browser_context,
      const base::RepeatingCallback<content::WebContents*()>& wc_getter,
      content::NavigationUIData* navigation_ui_data,
      int frame_tree_node_id) override {
    std::vector<std::unique_ptr<blink::URLLoaderThrottle>> throttles;
    if (request.url == watch_url_)
      throttles.push_back(std::make_unique<HeaderModifyingThrottle>());
    return throttles;
  }

 private:
  const GURL watch_url_;
};

// Subclass of DiceManageAccountBrowserTest with Mirror enabled.
class MirrorBrowserTest : public InProcessBrowserTest {
 protected:
  void RunExtensionConsentTest(extensions::WebAuthFlow::Partition partition,
                               bool expects_header) {
    net::EmbeddedTestServer https_server(net::EmbeddedTestServer::TYPE_HTTPS);
    https_server.AddDefaultHandlers(GetChromeTestDataDir());
    const std::string kAuthPath = "/auth";
    net::test_server::HttpRequest::HeaderMap headers;
    base::RunLoop run_loop;
    https_server.RegisterRequestMonitor(base::BindLambdaForTesting(
        [&](const net::test_server::HttpRequest& request) {
          if (request.GetURL().path() != kAuthPath)
            return;

          headers = request.headers;
          run_loop.Quit();
        }));
    ASSERT_TRUE(https_server.Start());

    auto web_auth_flow = std::make_unique<extensions::WebAuthFlow>(
        nullptr, browser()->profile(),
        https_server.GetURL("google.com", kAuthPath),
        extensions::WebAuthFlow::INTERACTIVE, partition);

    web_auth_flow->Start();
    run_loop.Run();
    EXPECT_EQ(!!headers.count(signin::kChromeConnectedHeader), expects_header);

    web_auth_flow.release()->DetachDelegateAndDelete();
    base::RunLoop().RunUntilIdle();
  }

 private:
  void SetUpOnMainThread() override {
    // The test makes requests to google.com and other domains which we want to
    // redirect to the test server.
    host_resolver()->AddRule("*", "127.0.0.1");
  }

  void SetUpCommandLine(base::CommandLine* command_line) override {
    // HTTPS server only serves a valid cert for localhost, so this is needed to
    // load pages from "www.google.com" without an interstitial.
    command_line->AppendSwitch(switches::kIgnoreCertificateErrors);

    // The production code only allows known ports (80 for http and 443 for
    // https), but the test server runs on a random port.
    command_line->AppendSwitch(switches::kIgnoreGooglePortNumbers);
  }
};

// Verify the following items:
// 1- X-Chrome-Connected is appended on Google domains if account consistency is
//    enabled and access is secure.
// 2- The header is stripped in case a request is redirected from a Google
//    domain to non-google domain.
// 3- The header is NOT stripped in case it is added directly by the page and
//    not because it was on a secure Google domain.
// This is a regression test for crbug.com/588492.
IN_PROC_BROWSER_TEST_F(MirrorBrowserTest, MirrorRequestHeader) {
  browser()->profile()->GetPrefs()->SetString(prefs::kGoogleServicesAccountId,
                                              "account_id");

  base::Lock lock;
  // Map from the path of the URLs that test server sees to the request header.
  // This is the path, and not URL, because the requests use different domains
  // which the mock HostResolver converts to 127.0.0.1.
  std::map<std::string, net::test_server::HttpRequest::HeaderMap> header_map;
  embedded_test_server()->RegisterRequestMonitor(base::BindLambdaForTesting(
      [&](const net::test_server::HttpRequest& request) {
        base::AutoLock auto_lock(lock);
        header_map[request.GetURL().path()] = request.headers;
      }));
  ASSERT_TRUE(embedded_test_server()->Start());

  net::EmbeddedTestServer https_server(net::EmbeddedTestServer::TYPE_HTTPS);
  https_server.AddDefaultHandlers(GetChromeTestDataDir());
  https_server.RegisterRequestMonitor(base::BindLambdaForTesting(
      [&](const net::test_server::HttpRequest& request) {
        base::AutoLock auto_lock(lock);
        header_map[request.GetURL().path()] = request.headers;
      }));
  ASSERT_TRUE(https_server.Start());

  base::FilePath root_http;
  base::PathService::Get(chrome::DIR_TEST_DATA, &root_http);
  root_http = root_http.AppendASCII("mirror_request_header");

  struct TestCase {
    // The URL from which the request begins.
    GURL original_url;
    // The path to which navigation is redirected.
    std::string redirected_to_path;
    // Should X-Chrome-Connected header be injected to the original request.
    bool inject_header;
    // Expectation: The header should be visible in original URL.
    bool original_url_expects_header;
    // Expectation: The header should be visible in redirected URL.
    bool redirected_to_url_expects_header;
  };

  std::vector<TestCase> all_tests;

  // Neither should have the header.
  // Note we need to replace the port of the redirect's URL.
  base::StringPairs replacement_text;
  replacement_text.push_back(std::make_pair(
      "{{PORT}}", base::NumberToString(embedded_test_server()->port())));
  std::string replacement_path = net::test_server::GetFilePathWithReplacements(
      "/mirror_request_header/http.www.google.com.html", replacement_text);
  all_tests.push_back(
      {embedded_test_server()->GetURL("www.google.com", replacement_path),
       "/simple.html", false, false, false});

  // First one adds the header and transfers it to the second.
  replacement_path = net::test_server::GetFilePathWithReplacements(
      "/mirror_request_header/http.www.header_adder.com.html",
      replacement_text);
  all_tests.push_back(
      {embedded_test_server()->GetURL("www.header_adder.com", replacement_path),
       "/simple.html", true, true, true});

  // First one should have the header, but not transfered to second one.
  replacement_text.clear();
  replacement_text.push_back(
      std::make_pair("{{PORT}}", base::NumberToString(https_server.port())));
  replacement_path = net::test_server::GetFilePathWithReplacements(
      "/mirror_request_header/https.www.google.com.html", replacement_text);
  all_tests.push_back({https_server.GetURL("www.google.com", replacement_path),
                       "/simple.html", false, true, false});

  for (const auto& test_case : all_tests) {
    SCOPED_TRACE(test_case.original_url);

    // If test case requires adding header for the first url add a throttle.
    ThrottleContentBrowserClient browser_client(test_case.original_url);
    content::ContentBrowserClient* old_browser_client = nullptr;
    if (test_case.inject_header)
      old_browser_client = content::SetBrowserClientForTesting(&browser_client);

    // Navigate to first url.
    ASSERT_TRUE(
        ui_test_utils::NavigateToURL(browser(), test_case.original_url));

    if (test_case.inject_header)
      content::SetBrowserClientForTesting(old_browser_client);

    base::AutoLock auto_lock(lock);

    // Check if header exists and X-Chrome-Connected is correctly provided.
    ASSERT_EQ(1u, header_map.count(test_case.original_url.path()));
    if (test_case.original_url_expects_header) {
      ASSERT_TRUE(header_map[test_case.original_url.path()].count(
          signin::kChromeConnectedHeader));
    } else {
      ASSERT_FALSE(header_map[test_case.original_url.path()].count(
          signin::kChromeConnectedHeader));
    }

    ASSERT_EQ(1u, header_map.count(test_case.redirected_to_path));
    if (test_case.redirected_to_url_expects_header) {
      ASSERT_TRUE(header_map[test_case.redirected_to_path].count(
          signin::kChromeConnectedHeader));
    } else {
      ASSERT_FALSE(header_map[test_case.redirected_to_path].count(
          signin::kChromeConnectedHeader));
    }

    header_map.clear();
  }
}

// Verifies that requests originated from chrome.identity.launchWebAuthFlow()
// API don't have Mirror headers attached.
// This is a regression test for crbug.com/1077504.
IN_PROC_BROWSER_TEST_F(MirrorBrowserTest,
                       NoMirrorExtensionConsent_LaunchWebAuthFlow) {
  RunExtensionConsentTest(extensions::WebAuthFlow::LAUNCH_WEB_AUTH_FLOW, false);
}

// Verifies that requests originated from chrome.identity.getAuthToken()
// API have Mirror headers attached.
IN_PROC_BROWSER_TEST_F(MirrorBrowserTest, MirrorExtensionConsent_GetAuthToken) {
  RunExtensionConsentTest(extensions::WebAuthFlow::GET_AUTH_TOKEN, true);
}

#if BUILDFLAG(IS_CHROMEOS_LACROS)

// Tests the behavior of Chrome when it receives a Mirror response from Gaia:
// - listens to all network responses coming from Gaia with
//   `signin::HeaderModificationDelegate`.
// - parses the Mirror response header with
// `signin::BuildManageAccountsParams()`
// - triggers dialogs based on the action specified in the header, with
//   `ProcessMirrorHeader`
// The tests don't display real dialogs. Instead they use the
// `FakeAccountManagerUI` and only check that the dialogs were triggered.
class MirrorResponseBrowserTest : public InProcessBrowserTest {
 public:
  MirrorResponseBrowserTest(const MirrorResponseBrowserTest&) = delete;
  MirrorResponseBrowserTest& operator=(const MirrorResponseBrowserTest&) =
      delete;

 protected:
  ~MirrorResponseBrowserTest() override = default;

  MirrorResponseBrowserTest()
      : https_server_(net::EmbeddedTestServer::TYPE_HTTPS) {}

  // Navigates to Gaia and receives a response with the specified
  // "X-Chrome-Manage-Accounts" header.
  void ReceiveManageAccountsHeader(
      const base::flat_map<std::string, std::string>& header_params) {
    std::vector<std::string> parts;
    for (const auto& [key, value] : header_params) {
      // "=" must be escaped as "%3D" for the embedded server.
      const char kEscapedEquals[] = "%3D";
      parts.push_back(key + kEscapedEquals + value);
    }
    std::string path = std::string("/set-header?X-Chrome-Manage-Accounts: ") +
                       base::JoinString(parts, ",");
    ASSERT_TRUE(
        ui_test_utils::NavigateToURL(browser(), https_server_.GetURL(path)));
  }

  // InProcessBrowserTest:
  void SetUp() override {
    https_server_.AddDefaultHandlers(GetChromeTestDataDir());
    ASSERT_TRUE(https_server_.InitializeAndListen());
    InProcessBrowserTest::SetUp();
  }

  void SetUpCommandLine(base::CommandLine* command_line) override {
    InProcessBrowserTest::SetUpCommandLine(command_line);
    const GURL& base_url = https_server_.base_url();
    command_line->AppendSwitchASCII(switches::kGaiaUrl, base_url.spec());
    command_line->AppendSwitchASCII(switches::kGoogleApisUrl, base_url.spec());
    command_line->AppendSwitchASCII(switches::kLsoUrl, base_url.spec());
  }

  void SetUpOnMainThread() override {
    https_server_.StartAcceptingConnections();
    InProcessBrowserTest::SetUpOnMainThread();
  }

  net::EmbeddedTestServer https_server_;
  net::test_server::EmbeddedTestServerHandle https_server_handle_;
};

// Tests that the "Add Account" dialog is shown when receiving "ADDSESSION" from
// Gaia.
IN_PROC_BROWSER_TEST_F(MirrorResponseBrowserTest, AddSession) {
  FakeAccountManagerUIDialogWaiter dialog_waiter(
      GetFakeAccountManagerUI(),
      FakeAccountManagerUIDialogWaiter::Event::kAddAccount);
  ReceiveManageAccountsHeader({{"action", "ADDSESSION"}});
  dialog_waiter.Wait();
}

// Tests that the "Settings"" dialog is shown when receiving "DEFAULT" from
// Gaia.
IN_PROC_BROWSER_TEST_F(MirrorResponseBrowserTest, Settings) {
  FakeAccountManagerUIDialogWaiter dialog_waiter(
      GetFakeAccountManagerUI(),
      FakeAccountManagerUIDialogWaiter::Event::kSettings);
  ReceiveManageAccountsHeader({{"action", "DEFAULT"}});
  dialog_waiter.Wait();
}

// Tests that the "Reauth" dialog is shown when receiving an email from Gaia.
IN_PROC_BROWSER_TEST_F(MirrorResponseBrowserTest, Reauth) {
  FakeAccountManagerUIDialogWaiter dialog_waiter(
      GetFakeAccountManagerUI(),
      FakeAccountManagerUIDialogWaiter::Event::kReauth);
  ReceiveManageAccountsHeader(
      {{"action", "ADDSESSION"}, {"email", "user@example.com"}});
  dialog_waiter.Wait();
}

// Tests that incognito browser is opened when receiving "INCOGNITO" from Gaia.
IN_PROC_BROWSER_TEST_F(MirrorResponseBrowserTest, Incognito) {
  ui_test_utils::BrowserChangeObserver browser_change_observer(
      /*browser=*/nullptr,
      ui_test_utils::BrowserChangeObserver::ChangeType::kAdded);
  ReceiveManageAccountsHeader({{"action", "INCOGNITO"}});
  Browser* incognito_browser = browser_change_observer.Wait();
  EXPECT_TRUE(incognito_browser->profile()->IsIncognitoProfile());
}

#endif  // BUILDFLAG(IS_CHROMEOS_LACROS)

}  // namespace
