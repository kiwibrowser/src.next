// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <tuple>

#include "base/base64.h"
#include "base/files/file_path.h"
#include "base/memory/raw_ref.h"
#include "base/notreached.h"
#include "base/path_service.h"
#include "base/test/scoped_feature_list.h"
#include "base/threading/thread_restrictions.h"
#include "content/browser/renderer_host/render_frame_host_impl.h"
#include "content/browser/web_contents/web_contents_impl.h"
#include "content/public/browser/content_browser_client.h"
#include "content/public/common/content_client.h"
#include "content/public/common/content_paths.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/content_browser_test.h"
#include "content/public/test/content_browser_test_content_browser_client.h"
#include "content/public/test/content_browser_test_utils.h"
#include "content/public/test/content_mock_cert_verifier.h"
#include "content/public/test/test_navigation_observer.h"
#include "content/shell/browser/shell.h"
#include "net/base/features.h"
#include "net/base/filename_util.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"
#include "net/test/spawned_test_server/spawned_test_server.h"
#include "net/test/test_data_directory.h"

namespace content {

class ContentSecurityPolicyBrowserTest : public ContentBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    host_resolver()->AddRule("*", "127.0.0.1");
    ASSERT_TRUE(embedded_test_server()->Start());
  }

  WebContentsImpl* web_contents() const {
    return static_cast<WebContentsImpl*>(shell()->web_contents());
  }

  RenderFrameHostImpl* current_frame_host() {
    return web_contents()->GetPrimaryMainFrame();
  }
};

// Test that the console error message for a Content Security Policy violation
// triggered by web assembly compilation does not mention the keyword
// 'wasm-eval' (which is currently only supported for extensions).  This is a
// regression test for https://crbug.com/1169592.
IN_PROC_BROWSER_TEST_F(ContentSecurityPolicyBrowserTest,
                       WasmEvalBlockedConsoleMessage) {
  GURL url = embedded_test_server()->GetURL("/csp_wasm_eval.html");

  WebContentsConsoleObserver console_observer(web_contents());
  console_observer.SetPattern(
      "[Report Only] Refused to compile or instantiate WebAssembly module "
      "because 'unsafe-eval' is not an allowed source of script in the "
      "following Content Security Policy directive: \"script-src "
      "'unsafe-inline'\".\n");
  EXPECT_TRUE(NavigateToURL(shell(), url));
  ASSERT_TRUE(console_observer.Wait());
}

// Test that creating a duplicate Trusted Types policy will yield a console
// message containing "already exists".
//
// This & the following test together ensure that different error causes get
// appropriate messages.
//
// Note: The bulk of Trusted Types related tests are found in the WPT suite
// under trusted-types/*. These two are here, because they need to access
// console messages.
IN_PROC_BROWSER_TEST_F(ContentSecurityPolicyBrowserTest,
                       TrustedTypesCreatePolicyDupeMessage) {
  const char* page = R"(
      data:text/html,
      <meta http-equiv="Content-Security-Policy"
            content="require-trusted-types-for 'script';trusted-types a;">
      <script>
        trustedTypes.createPolicy("a", {});
        trustedTypes.createPolicy("a", {});
      </script>)";

  GURL url(page);
  WebContentsConsoleObserver console_observer(web_contents());
  console_observer.SetPattern("*already exists*");
  EXPECT_TRUE(NavigateToURL(shell(), url));
  ASSERT_TRUE(console_observer.Wait());
}

// Test that creating a Trusted Types policy with a disallowed name will yield
// a console message indicating a directive has been violated.
IN_PROC_BROWSER_TEST_F(ContentSecurityPolicyBrowserTest,
                       TrustedTypesCreatePolicyForbiddenMessage) {
  const char* page = R"(
      data:text/html,
      <meta http-equiv="Content-Security-Policy"
            content="require-trusted-types-for 'script';trusted-types a;">
      <script>
        trustedTypes.createPolicy("b", {});
      </script>)";

  GURL url(page);
  WebContentsConsoleObserver console_observer(web_contents());
  console_observer.SetPattern("*violates*the following*directive*");
  EXPECT_TRUE(NavigateToURL(shell(), url));
  ASSERT_TRUE(console_observer.Wait());
}

IN_PROC_BROWSER_TEST_F(ContentSecurityPolicyBrowserTest,
                       WildcardNotMatchingNonNetworkSchemeBrowserSide) {
  const char* page = R"(
    data:text/html,
    <meta http-equiv="Content-Security-Policy" content="frame-src *">
    <iframe src="mailto:arthursonzogni@chromium.org"></iframe>
  )";

  GURL url(page);
  WebContentsConsoleObserver console_observer(web_contents());
  console_observer.SetPattern(
      "Refused to frame '' because it violates the following Content Security "
      "Policy directive: \"frame-src *\". Note that '*' matches only URLs with "
      "network schemes ('http', 'https', 'ws', 'wss'), or URLs whose scheme "
      "matches `self`'s scheme. The scheme 'mailto:' must be added "
      "explicitly.\n");
  EXPECT_TRUE(NavigateToURL(shell(), url));
  ASSERT_TRUE(console_observer.Wait());
}

IN_PROC_BROWSER_TEST_F(ContentSecurityPolicyBrowserTest,
                       WildcardNotMatchingNonNetworkSchemeRendererSide) {
  const char* page = R"(
    data:text/html,
    <meta http-equiv="Content-Security-Policy" content="script-src *">
    <script src="mailto:arthursonzogni@chromium.org"></script>
  )";

  GURL url(page);
  WebContentsConsoleObserver console_observer(web_contents());
  console_observer.SetPattern(
      "Refused to load the script 'mailto:arthursonzogni@chromium.org' because "
      "it violates the following Content Security Policy directive: "
      "\"script-src *\". Note that 'script-src-elem' was not explicitly set, "
      "so 'script-src' is used as a fallback. Note that '*' matches only URLs "
      "with network schemes ('http', 'https', 'ws', 'wss'), or URLs whose "
      "scheme matches `self`'s scheme. The scheme 'mailto:' must be added "
      "explicitly.\n");
  EXPECT_TRUE(NavigateToURL(shell(), url));
  ASSERT_TRUE(console_observer.Wait());
}

namespace {

base::FilePath TestFilePath(const char* filename) {
  base::ScopedAllowBlockingForTesting allow_blocking;
  return GetTestFilePath("", filename);
}

}  // namespace

// We test that we correctly match the file: scheme against file: URLs.
// Unfortunately, we cannot write this as Web Platform Test since Web Platform
// Tests don't support file: urls.
IN_PROC_BROWSER_TEST_F(ContentSecurityPolicyBrowserTest, FileURLs) {
  GURL::Replacements add_localhost;
  add_localhost.SetHostStr("localhost");
  GURL::Replacements none;
  struct {
    const char* csp;
    std::string element_name;
    const raw_ref<const GURL::Replacements> document_host;
    const raw_ref<const GURL::Replacements> element_host;
    bool expect_allowed;
  } test_cases[] = {
      {"img-src 'none'", "img", raw_ref(none), raw_ref(none), false},
      {"img-src file:", "img", raw_ref(none), raw_ref(none), true},
      {"img-src 'self'", "img", raw_ref(none), raw_ref(none), true},
      {"img-src 'none'", "img", raw_ref(none), raw_ref(add_localhost), false},
      {"img-src file:", "img", raw_ref(none), raw_ref(add_localhost), true},
      {"img-src 'self'", "img", raw_ref(none), raw_ref(add_localhost), true},
      {"img-src 'none'", "img", raw_ref(add_localhost), raw_ref(none), false},
      {"img-src file:", "img", raw_ref(add_localhost), raw_ref(none), true},
      {"img-src 'self'", "img", raw_ref(add_localhost), raw_ref(none), true},
      {"img-src 'none'", "img", raw_ref(add_localhost), raw_ref(add_localhost),
       false},
      {"img-src file:", "img", raw_ref(add_localhost), raw_ref(add_localhost),
       true},
      {"img-src 'self'", "img", raw_ref(add_localhost), raw_ref(add_localhost),
       true},
      {"frame-src 'none'", "iframe", raw_ref(none), raw_ref(none), false},
      {"frame-src file:", "iframe", raw_ref(none), raw_ref(none), true},
      {"frame-src 'self'", "iframe", raw_ref(none), raw_ref(none), true},
      {"frame-src 'none'", "iframe", raw_ref(none), raw_ref(add_localhost),
       false},
      {"frame-src file:", "iframe", raw_ref(none), raw_ref(add_localhost),
       true},
      // TODO(antoniosartori): The following one behaves differently than
      // img-src.
      {"frame-src 'self'", "iframe", raw_ref(none), raw_ref(add_localhost),
       true},
      {"frame-src 'none'", "iframe", raw_ref(add_localhost), raw_ref(none),
       false},
      {"frame-src file:", "iframe", raw_ref(add_localhost), raw_ref(none),
       true},
      // TODO(antoniosartori): The following one behaves differently than
      // img-src.
      {"frame-src 'self'", "iframe", raw_ref(add_localhost), raw_ref(none),
       true},
      {"frame-src 'none'", "iframe", raw_ref(add_localhost),
       raw_ref(add_localhost), false},
      {"frame-src file:", "iframe", raw_ref(add_localhost),
       raw_ref(add_localhost), true},
      {"frame-src 'self'", "iframe", raw_ref(add_localhost),
       raw_ref(add_localhost), true},
  };

  for (const auto& test_case : test_cases) {
    GURL document_url = net::FilePathToFileURL(TestFilePath("hello.html"))
                            .ReplaceComponents(*test_case.document_host);

    // On windows, if `document_url` contains the host part "localhost", the
    // actual committed URL does not. So we omit EXPECT_TRUE and ignore the
    // result value here.
    std::ignore = NavigateToURL(shell(), document_url);

    GURL element_url = net::FilePathToFileURL(TestFilePath(
        test_case.element_name == "iframe" ? "empty.html" : "blank.jpg"));
    element_url = element_url.ReplaceComponents(*test_case.element_host);
    TestNavigationObserver load_observer(shell()->web_contents());

    EXPECT_TRUE(
        ExecJs(current_frame_host(),
               JsReplace(R"(
          var violation = new Promise(resolve => {
            document.addEventListener("securitypolicyviolation", (e) => {
              resolve("got violation");
            });
          });

          let meta = document.createElement('meta');
          meta.httpEquiv = 'Content-Security-Policy';
          meta.content = $1;
          document.head.appendChild(meta);

          let element = document.createElement($2);
          element.src = $3;
          var promise = new Promise(resolve => {
            element.onload = () => { resolve("allowed"); };
            element.onerror = () => { resolve("blocked"); };
          });
          document.body.appendChild(element);
    )",
                         test_case.csp, test_case.element_name, element_url)));

    if (test_case.element_name == "iframe") {
      // Since iframes always trigger the onload event, we need to be more
      // careful checking whether the iframe was blocked or not.
      load_observer.Wait();
      const url::Origin child_origin = current_frame_host()
                                           ->child_at(0)
                                           ->current_frame_host()
                                           ->GetLastCommittedOrigin();
      if (test_case.expect_allowed) {
        EXPECT_TRUE(load_observer.last_navigation_succeeded())
            << element_url << " in " << document_url << " with CSPs \""
            << test_case.csp << "\" should be allowed";
        EXPECT_FALSE(child_origin.opaque());
      } else {
        EXPECT_FALSE(load_observer.last_navigation_succeeded());
        EXPECT_EQ(net::ERR_BLOCKED_BY_CSP, load_observer.last_net_error_code());
        // The blocked frame's origin should become unique.
        EXPECT_TRUE(child_origin.opaque())
            << element_url << " in " << document_url << " with CSPs \""
            << test_case.csp << "\" should be blocked";
      }
    } else {
      std::string expect_message =
          test_case.expect_allowed ? "allowed" : "blocked";
      EXPECT_EQ(expect_message, EvalJs(current_frame_host(), "promise"))
          << element_url << " in " << document_url << " with CSPs \""
          << test_case.csp << "\" should be " << expect_message;
    }

    if (!test_case.expect_allowed) {
      EXPECT_EQ("got violation", EvalJs(current_frame_host(), "violation"));
    }
  }
}

// Test that a 'csp' attribute longer than 4096 bytes is ignored.
IN_PROC_BROWSER_TEST_F(ContentSecurityPolicyBrowserTest, CSPAttributeTooLong) {
  std::string long_csp_attribute = "script-src 'none' ";
  long_csp_attribute.resize(4097, 'a');
  std::string page = "data:text/html,<body><iframe csp=\"" +
                     long_csp_attribute + "\"></iframe></body>";

  GURL url(page);
  WebContentsConsoleObserver console_observer(web_contents());
  console_observer.SetPattern("'csp' attribute too long*");
  EXPECT_TRUE(NavigateToURL(shell(), url));
  ASSERT_TRUE(console_observer.Wait());

  EXPECT_EQ(current_frame_host()->child_count(), 1u);
  EXPECT_FALSE(current_frame_host()->child_at(0)->csp_attribute());
}

namespace {

constexpr char kWebmPath[] = "/csp_video.webm";

std::unique_ptr<net::test_server::HttpResponse> ServeCSPMedia(
    const net::test_server::HttpRequest& request) {
  if (request.relative_url != kWebmPath) {
    return nullptr;
  }
  auto cookie_header = request.headers.find("cookie");
  auto response = std::make_unique<net::test_server::BasicHttpResponse>();
  if (cookie_header == request.headers.end()) {
    response->set_code(net::HTTP_UNAUTHORIZED);
    return std::move(response);
  }
  response->set_code(net::HTTP_OK);
  const std::string kOneFrameOnePixelWebm =
      "GkXfo0AgQoaBAUL3gQFC8oEEQvOBCEKCQAR3ZWJtQoeBAkKFgQIYU4BnQN8VSalmQCgq17FA"
      "Aw9CQE2AQAZ3aGFtbXlXQUAGd2hhbW15RIlACECPQAAAAAAAFlSua0AxrkAu14EBY8WBAZyB"
      "ACK1nEADdW5khkAFVl9WUDglhohAA1ZQOIOBAeBABrCBlrqBlh9DtnVAdOeBAKNAboEAAIDy"
      "CACdASqWAJYAPk0ci0WD+IBAAJiWlu4XdQTSq2H4MW0+sMO0gz8HMRe+"
      "0jRo0aNGjRo0aNGjRo0aNGjRo0aNGjRo0aNGjRo0aNGjRo0VAAD+/729RWRzH4mOZ9/"
      "O8Dl319afX4gsgAAA";
  std::string content;
  base::Base64Decode(kOneFrameOnePixelWebm, &content);
  response->AddCustomHeader("Content-Security-Policy", "sandbox allow-scripts");
  response->AddCustomHeader("Content-Type", "video/webm");
  response->AddCustomHeader("Access-Control-Allow-Origin", "null");
  response->AddCustomHeader("Access-Control-Allow-Credentials", "true");
  response->set_content(content);
  return std::move(response);
}

}  // namespace

class ThirdPartyCookiesContentSecurityPolicyBrowserTest
    : public ContentSecurityPolicyBrowserTest {
 public:
  ThirdPartyCookiesContentSecurityPolicyBrowserTest()
      : https_server_(net::EmbeddedTestServer::TYPE_HTTPS) {
    feature_list_.InitAndEnableFeature(
        net::features::kForceThirdPartyCookieBlocking);
  }

  void SetUpOnMainThread() override {
    ContentSecurityPolicyBrowserTest::SetUpOnMainThread();
    host_resolver()->AddRule("*", "127.0.0.1");
    mock_cert_verifier_.mock_cert_verifier()->set_default_result(net::OK);
    https_server()->ServeFilesFromSourceDirectory(GetTestDataFilePath());
    https_server()->RegisterRequestHandler(base::BindRepeating(&ServeCSPMedia));
    ASSERT_TRUE(https_server()->Start());
  }

  void SetUpCommandLine(base::CommandLine* command_line) override {
    ContentSecurityPolicyBrowserTest::SetUpCommandLine(command_line);
    mock_cert_verifier_.SetUpCommandLine(command_line);
  }

  void SetUpInProcessBrowserTestFixture() override {
    ContentSecurityPolicyBrowserTest::SetUpInProcessBrowserTestFixture();
    mock_cert_verifier_.SetUpInProcessBrowserTestFixture();
  }

  void TearDownInProcessBrowserTestFixture() override {
    mock_cert_verifier_.TearDownInProcessBrowserTestFixture();
    ContentSecurityPolicyBrowserTest::TearDownInProcessBrowserTestFixture();
  }

 protected:
  net::EmbeddedTestServer* https_server() { return &https_server_; }

 private:
  net::EmbeddedTestServer https_server_;
  ContentMockCertVerifier mock_cert_verifier_;
  base::test::ScopedFeatureList feature_list_;
};

// Test that CSP does not break rendering access-controlled media due to
// third-party cookie blocking.
IN_PROC_BROWSER_TEST_F(ThirdPartyCookiesContentSecurityPolicyBrowserTest,
                       CSPMediaThirdPartyCookieBlocking) {
  ASSERT_TRUE(content::SetCookie(web_contents()->GetBrowserContext(),
                                 https_server()->GetURL("/"),
                                 "foo=bar; SameSite=None; Secure;"));
  ASSERT_TRUE(NavigateToURL(shell(), https_server()->GetURL(kWebmPath)));
  EXPECT_TRUE(EvalJs(shell(),
                     "fetch('/csp_video.webm', {credentials: "
                     "'include'}).then(res => res.status == 200)")
                  .ExtractBool());
}

IN_PROC_BROWSER_TEST_F(ThirdPartyCookiesContentSecurityPolicyBrowserTest,
                       CSPMediaThirdPartyCookieBlocking_IFrame) {
  ASSERT_TRUE(content::SetCookie(web_contents()->GetBrowserContext(),
                                 https_server()->GetURL("/"),
                                 "foo=bar; SameSite=None; Secure;"));
  std::string page = "data:text/html,<iframe src=\"" +
                     https_server()->GetURL(kWebmPath).spec() + "\"></iframe>";
  ASSERT_TRUE(NavigateToURL(shell(), GURL(page)));
  content::RenderFrameHost* nested_iframe = content::ChildFrameAt(shell(), 0);
  EXPECT_FALSE(EvalJs(nested_iframe,
                      "fetch('/csp_video.webm', {credentials: "
                      "'include'}).then(res => res.status == 200)")
                   .ExtractBool());
}

namespace {
const char kAppHost[] = "app.com";
const char kNonAppHost[] = "other.com";
}  // namespace

class IsolatedWebAppContentBrowserClient
    : public ContentBrowserTestContentBrowserClient {
 public:
  bool ShouldUrlUseApplicationIsolationLevel(BrowserContext* browser_context,
                                             const GURL& url) override {
    return url.host() == kAppHost;
  }
};

class ContentSecurityPolicyIsolatedAppBrowserTest
    : public ContentSecurityPolicyBrowserTest {
 public:
  ContentSecurityPolicyIsolatedAppBrowserTest()
      : https_server_(net::EmbeddedTestServer::TYPE_HTTPS) {}

  void SetUpCommandLine(base::CommandLine* command_line) override {
    ContentSecurityPolicyBrowserTest::SetUpCommandLine(command_line);
    mock_cert_verifier_.SetUpCommandLine(command_line);
  }

  void SetUpInProcessBrowserTestFixture() override {
    ContentSecurityPolicyBrowserTest::SetUpInProcessBrowserTestFixture();
    mock_cert_verifier_.SetUpInProcessBrowserTestFixture();
  }

  void TearDownInProcessBrowserTestFixture() override {
    mock_cert_verifier_.TearDownInProcessBrowserTestFixture();
    ContentSecurityPolicyBrowserTest::TearDownInProcessBrowserTestFixture();
  }

  void SetUpOnMainThread() override {
    ContentSecurityPolicyBrowserTest::SetUpOnMainThread();
    client_ = std::make_unique<IsolatedWebAppContentBrowserClient>();

    host_resolver()->AddRule("*", "127.0.0.1");
    mock_cert_verifier_.mock_cert_verifier()->set_default_result(net::OK);
    https_server()->ServeFilesFromSourceDirectory(GetTestDataFilePath());
    ASSERT_TRUE(https_server()->Start());
  }

  void TearDownOnMainThread() override {
    client_.reset();
    ContentSecurityPolicyBrowserTest::TearDownOnMainThread();
  }

 protected:
  net::EmbeddedTestServer* https_server() { return &https_server_; }

 private:
  net::EmbeddedTestServer https_server_;
  ContentMockCertVerifier mock_cert_verifier_;

  std::unique_ptr<IsolatedWebAppContentBrowserClient> client_;
};

IN_PROC_BROWSER_TEST_F(ContentSecurityPolicyIsolatedAppBrowserTest, Base) {
  EXPECT_TRUE(NavigateToURL(
      shell(),
      https_server()->GetURL(kAppHost, "/cross-origin-isolated.html")));

  // Base element should be disabled.
  EXPECT_EQ("violation", EvalJs(shell(), R"(
    new Promise(resolve => {
      document.addEventListener('securitypolicyviolation', e => {
        resolve('violation');
      });

      let base = document.createElement('base');
      base.href = '/test';
      document.body.appendChild(base);
    })
  )"));
}

IN_PROC_BROWSER_TEST_F(ContentSecurityPolicyIsolatedAppBrowserTest, Src) {
  constexpr auto kHttp = net::EmbeddedTestServer::TYPE_HTTP;
  constexpr auto kHttps = net::EmbeddedTestServer::TYPE_HTTPS;
  struct {
    std::string element_name;
    net::EmbeddedTestServer::Type scheme;
    std::string host;
    std::string path;
    std::string expectation;
  } test_cases[] = {
      // Cross-origin HTTPS images and media are allowed (but need a
      // Cross-Origin-Resource-Policy header, and will error otherwise)
      {"img", kHttps, kAppHost, "/single_face.jpg", "allowed"},
      {"img", kHttps, kNonAppHost, "/single_face.jpg", "error"},
      {"img", kHttps, kNonAppHost, "/single_face_corp.jpg", "allowed"},
      {"audio", kHttps, kAppHost, "/media/bear.flac", "allowed"},
      {"audio", kHttps, kNonAppHost, "/media/bear.flac", "error"},
      {"audio", kHttps, kNonAppHost, "/media/bear_corp.flac", "allowed"},
      {"video", kHttps, kAppHost, "/media/bear.webm", "allowed"},
      {"video", kHttps, kNonAppHost, "/media/bear.webm", "error"},
      {"video", kHttps, kNonAppHost, "/media/bear_corp.webm", "allowed"},
      // Plugins are disabled.
      {"embed", kHttps, kAppHost, "/single_face.jpg", "violation"},
      // Iframes can contain cross-origin HTTPS content.
      {"iframe", kHttps, kAppHost, "/cross-origin-isolated.html", "allowed"},
      {"iframe", kHttps, kNonAppHost, "/simple.html", "allowed"},
      {"iframe", kHttp, kNonAppHost, "/simple.html", "violation"},
      // Script tags must be same-origin.
      {"script", kHttps, kAppHost, "/result_queue.js", "allowed"},
      {"script", kHttps, kNonAppHost, "/result_queue.js", "violation"},
      // Stylesheets must be same-origin as per style-src CSP.
      {"link", kHttps, kAppHost, "/empty-style.css", "allowed"},
      {"link", kHttps, kNonAppHost, "/empty-style.css", "violation"},
  };

  for (const auto& test_case : test_cases) {
    EXPECT_TRUE(NavigateToURL(
        shell(),
        https_server()->GetURL(kAppHost, "/cross-origin-isolated.html")));

    net::EmbeddedTestServer* test_server =
        test_case.scheme == net::EmbeddedTestServer::TYPE_HTTP
            ? embedded_test_server()
            : https_server();
    GURL src = test_server->GetURL(test_case.host, test_case.path);
    std::string test_js = JsReplace(R"(
      const policy = window.trustedTypes.createPolicy('policy', {
        createScriptURL: url => url,
      });

      new Promise(resolve => {
        document.addEventListener('securitypolicyviolation', e => {
          resolve('violation');
        });

        let element = document.createElement($1);

        if($1 === 'link') {
          // Stylesheets require `rel` and `href` instead of `src` to work.
          element.rel = 'stylesheet';
          element.href = $2;
        } else {
          // Not all elements being tested require Trusted Types, but passing
          // src through the policy for all non-stylesheet elements works.
          element.src = policy.createScriptURL($2);
        }

        element.addEventListener('canplay', () => resolve('allowed'));
        element.addEventListener('load', () => resolve('allowed'));
        element.addEventListener('error', e => resolve('error'));
        document.body.appendChild(element);
      })
    )",
                                    test_case.element_name, src);
    SCOPED_TRACE(testing::Message() << "Running testcase: "
                                    << test_case.element_name << " " << src);
    EXPECT_EQ(test_case.expectation, EvalJs(shell(), test_js));
  }
}

IN_PROC_BROWSER_TEST_F(ContentSecurityPolicyIsolatedAppBrowserTest,
                       TrustedTypes) {
  EXPECT_TRUE(NavigateToURL(
      shell(),
      https_server()->GetURL(kAppHost, "/cross-origin-isolated.html")));

  // Trusted Types should be required for scripts.
  EXPECT_EQ("exception", EvalJs(shell(), R"(
    new Promise(resolve => {
      document.addEventListener('securitypolicyviolation', e => {
        resolve('violation');
      });

      try {
        let element = document.createElement('script');
        element.src = '/result_queue.js';
        element.addEventListener('load', () => resolve('allowed'));
        element.addEventListener('error', e => resolve('error'));
        document.body.appendChild(element);
      } catch (e) {
        resolve('exception');
      }
    })
  )"));
}

IN_PROC_BROWSER_TEST_F(ContentSecurityPolicyIsolatedAppBrowserTest, Wasm) {
  EXPECT_TRUE(NavigateToURL(
      shell(),
      https_server()->GetURL(kAppHost, "/cross-origin-isolated.html")));

  EXPECT_EQ("allowed", EvalJs(shell(), R"(
    new Promise(async (resolve) => {
      document.addEventListener('securitypolicyviolation', e => {
        resolve('violation');
      });

      try {
        await WebAssembly.compile(new Uint8Array(
            // The smallest possible Wasm module. Just the header
            // (0, "A", "S", "M"), and the version (0x1).
            [0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00]));
        resolve('allowed');
      } catch (e) {
        resolve('exception: ' + e);
      }
    })
  )"));
}

IN_PROC_BROWSER_TEST_F(ContentSecurityPolicyIsolatedAppBrowserTest,
                       UnsafeInlineStyleSrc) {
  EXPECT_TRUE(NavigateToURL(
      shell(),
      https_server()->GetURL(kAppHost, "/cross-origin-isolated.html")));

  EXPECT_EQ("none", EvalJs(shell(), R"(
    new Promise(async (resolve) => {
      document.addEventListener('securitypolicyviolation', e => {
        resolve('violation');
      });

      try {
        document.body.setAttribute("style", "display: none;");
        const bodyStyles = window.getComputedStyle(document.body);
        resolve(bodyStyles.getPropertyValue("display"));
      } catch (e) {
        resolve('exception: ' + e);
      }
    })
  )"));
}

struct WebSocketTestParam {
  net::SpawnedTestServer::Type type;
  std::string expected_result;
};

class ContentSecurityPolicyIsolatedAppWebSocketBrowserTest
    : public ContentSecurityPolicyIsolatedAppBrowserTest,
      public testing::WithParamInterface<WebSocketTestParam> {};

// Disabled on Android, since we have problems starting up the WebSocket test
// server on the host.
//
// TODO(crbug.com/1448866): Enable the test after solving the WebSocket server
// issue.
#if BUILDFLAG(IS_ANDROID)
#define MAYBE_CheckCsp DISABLED_CheckCsp
#else
#define MAYBE_CheckCsp CheckCsp
#endif
IN_PROC_BROWSER_TEST_P(ContentSecurityPolicyIsolatedAppWebSocketBrowserTest,
                       MAYBE_CheckCsp) {
  auto websocket_test_server = std::make_unique<net::SpawnedTestServer>(
      GetParam().type, net::GetWebSocketTestDataDirectory());
  ASSERT_TRUE(websocket_test_server->Start());

  EXPECT_TRUE(NavigateToURL(
      shell(),
      https_server()->GetURL(kAppHost, "/cross-origin-isolated.html")));

  // The |websocket_url| will echo the message we send to it.
  GURL websocket_url = websocket_test_server->GetURL("echo-with-no-extension");

  EXPECT_EQ(GetParam().expected_result,
            EvalJs(shell(), JsReplace(R"(
    new Promise(async (resolve) => {
      document.addEventListener('securitypolicyviolation', e => {
        resolve('violation');
      });

      try {
        new WebSocket($1).onopen = () => resolve('allowed');
      } catch (e) {
        resolve('exception: ' + e);
      }
    })
  )",
                                      websocket_url)));
}

INSTANTIATE_TEST_SUITE_P(
    /* no prefix */,
    ContentSecurityPolicyIsolatedAppWebSocketBrowserTest,
    ::testing::Values(
        WebSocketTestParam{.type = net::SpawnedTestServer::TYPE_WS,
                           .expected_result = "violation"},
        WebSocketTestParam{.type = net::SpawnedTestServer::TYPE_WSS,
                           .expected_result = "allowed"}),
    [](const testing::TestParamInfo<
        ContentSecurityPolicyIsolatedAppWebSocketBrowserTest::ParamType>& info)
        -> std::string {
      switch (info.param.type) {
        case net::SpawnedTestServer::TYPE_WS:
          return "Ws";
        case net::SpawnedTestServer::TYPE_WSS:
          return "Wss";
        default:
          NOTREACHED_NORETURN();
      }
    });

}  // namespace content
