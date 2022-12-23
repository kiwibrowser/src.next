// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/command_line.h"
#include "base/strings/string_piece.h"
#include "base/test/scoped_feature_list.h"
#include "base/threading/platform_thread.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "components/metrics/content/subprocess_metrics_provider.h"
#include "components/network_session_configurator/common/network_switches.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/content_features.h"
#include "content/public/common/content_switches.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/content_browser_test_utils.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "services/network/public/cpp/features.h"
#include "services/network/public/mojom/cross_origin_opener_policy.mojom.h"
#include "third_party/blink/public/common/features.h"

namespace {
const int kWasmPageSize = 1 << 16;
}  // namespace

// Web platform security features are implemented by content/ and blink/.
// However, since ContentBrowserClientImpl::LogWebFeatureForCurrentPage() is
// currently left blank in content/, metrics logging can't be tested from
// content/. So it is tested from chrome/ instead.
class ChromeWebPlatformSecurityMetricsBrowserTest
    : public InProcessBrowserTest {
 public:
  using WebFeature = blink::mojom::WebFeature;

  ChromeWebPlatformSecurityMetricsBrowserTest()
      : https_server_(net::EmbeddedTestServer::TYPE_HTTPS),
        http_server_(net::EmbeddedTestServer::TYPE_HTTP) {
    features_.InitWithFeatures(
        {
            // Enabled:
            network::features::kCrossOriginOpenerPolicy,
            // SharedArrayBuffer is needed for these tests.
            features::kSharedArrayBuffer,
        },
        {});
  }

  content::WebContents* web_contents() const {
    return browser()->tab_strip_model()->GetActiveWebContents();
  }

  void set_monitored_feature(WebFeature feature) {
    monitored_feature_ = feature;
  }

  void LoadIFrame(const GURL& url) {
    LoadIFrameInWebContents(web_contents(), url);
  }

  content::WebContents* OpenPopup(const GURL& url) {
    content::WebContentsAddedObserver new_tab_observer;
    EXPECT_TRUE(
        content::ExecJs(web_contents(), "window.open('" + url.spec() + "')"));
    content::WebContents* web_contents = new_tab_observer.GetWebContents();
    EXPECT_TRUE(content::WaitForLoadStop(web_contents));
    return web_contents;
  }

  void LoadIFrameInWebContents(content::WebContents* web_contents,
                               const GURL& url) {
    EXPECT_EQ(true, content::EvalJs(web_contents, content::JsReplace(R"(
      new Promise(resolve => {
        let iframe = document.createElement("iframe");
        iframe.src = $1;
        iframe.onload = () => resolve(true);
        document.body.appendChild(iframe);
      });
    )",
                                                                     url)));
  }

  void ExpectHistogramIncreasedBy(int count) {
    expected_count_ += count;
    histogram_.ExpectBucketCount("Blink.UseCounter.Features",
                                 monitored_feature_, expected_count_);
  }

  net::EmbeddedTestServer& https_server() { return https_server_; }
  net::EmbeddedTestServer& http_server() { return http_server_; }

  // Fetch the Blink.UseCounter.Features histogram in every renderer process
  // until reaching, but not exceeding, |expected_count|.
  void CheckCounter(WebFeature feature, int expected_count) {
    CheckFeatureBucketCount("Blink.UseCounter.Features", feature,
                            expected_count);
  }

  // Fetch the Blink.UseCounter.MainFrame.Features histogram in every renderer
  // process until reaching, but not exceeding, |expected_count|.
  void CheckCounterMainFrame(WebFeature feature, int expected_count) {
    CheckFeatureBucketCount("Blink.UseCounter.MainFrame.Features", feature,
                            expected_count);
  }

  // Fetch the |histogram|'s |feature| in every renderer process until reaching,
  // but not exceeding, |expected_count|.
  void CheckFeatureBucketCount(base::StringPiece histogram,
                               WebFeature feature,
                               int expected_count) {
    while (true) {
      content::FetchHistogramsFromChildProcesses();
      metrics::SubprocessMetricsProvider::MergeHistogramDeltasForTesting();

      int count = histogram_.GetBucketCount(histogram, feature);
      CHECK_LE(count, expected_count);
      if (count == expected_count)
        return;

      base::PlatformThread::Sleep(base::Milliseconds(5));
    }
  }

 private:
  void SetUpOnMainThread() final {
    host_resolver()->AddRule("*", "127.0.0.1");

    https_server_.AddDefaultHandlers(GetChromeTestDataDir());
    http_server_.AddDefaultHandlers(GetChromeTestDataDir());

    // Add content/test/data for cross_site_iframe_factory.html
    https_server_.ServeFilesFromSourceDirectory("content/test/data");
    http_server_.ServeFilesFromSourceDirectory("content/test/data");

    https_server_.SetSSLConfig(net::EmbeddedTestServer::CERT_OK);
    ASSERT_TRUE(https_server_.Start());
    ASSERT_TRUE(http_server_.Start());
    EXPECT_TRUE(content::NavigateToURL(web_contents(), GURL("about:blank")));
  }

  void SetUpCommandLine(base::CommandLine* command_line) final {
    // For anonymous iframe:
    command_line->AppendSwitch(switches::kEnableBlinkTestFeatures);
    command_line->AppendSwitch(switches::kIgnoreCertificateErrors);
  }

  net::EmbeddedTestServer https_server_;
  net::EmbeddedTestServer http_server_;
  int expected_count_ = 0;
  base::HistogramTester histogram_;
  WebFeature monitored_feature_;
  base::test::ScopedFeatureList features_;
};

// Check the kCrossOriginOpenerPolicyReporting feature usage. No header => 0
// count.
IN_PROC_BROWSER_TEST_F(ChromeWebPlatformSecurityMetricsBrowserTest,
                       CrossOriginOpenerPolicyReportingNoHeader) {
  set_monitored_feature(WebFeature::kCrossOriginOpenerPolicyReporting);
  GURL url = https_server().GetURL("a.com", "/title1.html");
  EXPECT_TRUE(content::NavigateToURL(web_contents(), url));
  ExpectHistogramIncreasedBy(0);
}

// Check the kCrossOriginOpenerPolicyReporting feature usage. COOP-Report-Only +
// HTTP => 0 count.
IN_PROC_BROWSER_TEST_F(ChromeWebPlatformSecurityMetricsBrowserTest,
                       CrossOriginOpenerPolicyReportingReportOnlyHTTP) {
  set_monitored_feature(WebFeature::kCrossOriginOpenerPolicyReporting);
  GURL url = http_server().GetURL("a.com",
                                  "/set-header?"
                                  "Cross-Origin-Opener-Policy-Report-Only: "
                                  "same-origin; report-to%3d\"a\"");
  EXPECT_TRUE(content::NavigateToURL(web_contents(), url));
  ExpectHistogramIncreasedBy(0);
}

// Check the kCrossOriginOpenerPolicyReporting feature usage. COOP-Report-Only +
// HTTPS => 1 count.
IN_PROC_BROWSER_TEST_F(ChromeWebPlatformSecurityMetricsBrowserTest,
                       CrossOriginOpenerPolicyReportingReportOnlyHTTPS) {
  set_monitored_feature(WebFeature::kCrossOriginOpenerPolicyReporting);
  GURL url = https_server().GetURL("a.com",
                                   "/set-header?"
                                   "Cross-Origin-Opener-Policy-Report-Only: "
                                   "same-origin; report-to%3d\"a\"");
  EXPECT_TRUE(content::NavigateToURL(web_contents(), url));
  ExpectHistogramIncreasedBy(1);
}

// Check the kCrossOriginOpenerPolicyReporting feature usage. COOP + HTPS => 1
// count.
IN_PROC_BROWSER_TEST_F(ChromeWebPlatformSecurityMetricsBrowserTest,
                       CrossOriginOpenerPolicyReportingCOOPHTTPS) {
  set_monitored_feature(WebFeature::kCrossOriginOpenerPolicyReporting);
  GURL url = https_server().GetURL("a.com",
                                   "/set-header?"
                                   "Cross-Origin-Opener-Policy: "
                                   "same-origin; report-to%3d\"a\"");
  EXPECT_TRUE(content::NavigateToURL(web_contents(), url));
  ExpectHistogramIncreasedBy(1);
}

// Check the kCrossOriginOpenerPolicyReporting feature usage. COOP + COOP-RO  +
// HTTPS => 1 count.
IN_PROC_BROWSER_TEST_F(ChromeWebPlatformSecurityMetricsBrowserTest,
                       CrossOriginOpenerPolicyReportingCOOPAndReportOnly) {
  set_monitored_feature(WebFeature::kCrossOriginOpenerPolicyReporting);
  GURL url = https_server().GetURL("a.com",
                                   "/set-header?"
                                   "Cross-Origin-Opener-Policy: "
                                   "same-origin; report-to%3d\"a\"&"
                                   "Cross-Origin-Opener-Policy-Report-Only: "
                                   "same-origin; report-to%3d\"a\"");
  EXPECT_TRUE(content::NavigateToURL(web_contents(), url));
  ExpectHistogramIncreasedBy(1);
}

// Check the kCrossOriginOpenerPolicyReporting feature usage. No report
// endpoints defined => 0 count.
IN_PROC_BROWSER_TEST_F(ChromeWebPlatformSecurityMetricsBrowserTest,
                       CrossOriginOpenerPolicyReportingNoEndpoint) {
  set_monitored_feature(WebFeature::kCrossOriginOpenerPolicyReporting);
  GURL url = https_server().GetURL(
      "a.com",
      "/set-header?"
      "Cross-Origin-Opener-Policy: same-origin&"
      "Cross-Origin-Opener-Policy-Report-Only: same-origin");
  EXPECT_TRUE(content::NavigateToURL(web_contents(), url));
  ExpectHistogramIncreasedBy(0);
}

// Check the kCrossOriginOpenerPolicyReporting feature usage. Main frame
// (COOP-RO), subframe (COOP-RO) => 1 count.
IN_PROC_BROWSER_TEST_F(ChromeWebPlatformSecurityMetricsBrowserTest,
                       CrossOriginOpenerPolicyReportingMainFrameAndSubframe) {
  set_monitored_feature(WebFeature::kCrossOriginOpenerPolicyReporting);
  GURL url = https_server().GetURL("a.com",
                                   "/set-header?"
                                   "Cross-Origin-Opener-Policy-Report-Only: "
                                   "same-origin; report-to%3d\"a\"");
  EXPECT_TRUE(content::NavigateToURL(web_contents(), url));
  LoadIFrame(url);
  ExpectHistogramIncreasedBy(1);
}

// Check the kCrossOriginOpenerPolicyReporting feature usage. Main frame
// (no-headers), subframe (COOP-RO) => 0 count.
IN_PROC_BROWSER_TEST_F(ChromeWebPlatformSecurityMetricsBrowserTest,
                       CrossOriginOpenerPolicyReportingUsageSubframeOnly) {
  set_monitored_feature(WebFeature::kCrossOriginOpenerPolicyReporting);
  GURL main_document_url = https_server().GetURL("a.com", "/title1.html");
  GURL sub_document_url =
      https_server().GetURL("a.com",
                            "/set-header?"
                            "Cross-Origin-Opener-Policy-Report-Only: "
                            "same-origin; report-to%3d\"a\"");
  EXPECT_TRUE(content::NavigateToURL(web_contents(), main_document_url));
  LoadIFrame(sub_document_url);
  ExpectHistogramIncreasedBy(0);
}

// Check kCrossOriginSubframeWithoutEmbeddingControl reporting. Same-origin
// iframe (no headers) => 0 count.
IN_PROC_BROWSER_TEST_F(ChromeWebPlatformSecurityMetricsBrowserTest,
                       CrossOriginSubframeWithoutEmbeddingControlSameOrigin) {
  set_monitored_feature(
      WebFeature::kCrossOriginSubframeWithoutEmbeddingControl);
  GURL url = https_server().GetURL("a.com", "/title1.html");
  EXPECT_TRUE(content::NavigateToURL(web_contents(), url));
  LoadIFrame(url);
  ExpectHistogramIncreasedBy(0);
}

// Check kCrossOriginSubframeWithoutEmbeddingControl reporting. Cross-origin
// iframe (no headers) => 0 count.
IN_PROC_BROWSER_TEST_F(ChromeWebPlatformSecurityMetricsBrowserTest,
                       CrossOriginSubframeWithoutEmbeddingControlNoHeaders) {
  set_monitored_feature(
      WebFeature::kCrossOriginSubframeWithoutEmbeddingControl);
  GURL main_document_url = https_server().GetURL("a.com", "/title1.html");
  GURL sub_document_url = https_server().GetURL("b.com", "/title1.html");
  EXPECT_TRUE(content::NavigateToURL(web_contents(), main_document_url));
  LoadIFrame(sub_document_url);
  ExpectHistogramIncreasedBy(1);
}

// Check kCrossOriginSubframeWithoutEmbeddingControl reporting. Cross-origin
// iframe (CSP frame-ancestors) => 0 count.
IN_PROC_BROWSER_TEST_F(
    ChromeWebPlatformSecurityMetricsBrowserTest,
    CrossOriginSubframeWithoutEmbeddingControlFrameAncestors) {
  set_monitored_feature(
      WebFeature::kCrossOriginSubframeWithoutEmbeddingControl);
  GURL main_document_url = https_server().GetURL("a.com", "/title1.html");
  url::Origin main_document_origin = url::Origin::Create(main_document_url);
  std::string csp_header = "Content-Security-Policy: frame-ancestors 'self' *;";
  GURL sub_document_url =
      https_server().GetURL("b.com", "/set-header?" + csp_header);
  EXPECT_TRUE(content::NavigateToURL(web_contents(), main_document_url));
  LoadIFrame(sub_document_url);
  ExpectHistogramIncreasedBy(0);
}

// Check kCrossOriginSubframeWithoutEmbeddingControl reporting. Cross-origin
// iframe (blocked by CSP header) => 0 count.
IN_PROC_BROWSER_TEST_F(ChromeWebPlatformSecurityMetricsBrowserTest,
                       CrossOriginSubframeWithoutEmbeddingControlNoEmbedding) {
  set_monitored_feature(
      WebFeature::kCrossOriginSubframeWithoutEmbeddingControl);
  GURL main_document_url = https_server().GetURL("a.com", "/title1.html");
  GURL sub_document_url =
      https_server().GetURL("b.com",
                            "/set-header?"
                            "Content-Security-Policy: frame-ancestors 'self';");
  EXPECT_TRUE(content::NavigateToURL(web_contents(), main_document_url));
  LoadIFrame(sub_document_url);
  ExpectHistogramIncreasedBy(0);
}

// Check kCrossOriginSubframeWithoutEmbeddingControl reporting. Cross-origin
// iframe (other CSP header) => 1 count.
IN_PROC_BROWSER_TEST_F(ChromeWebPlatformSecurityMetricsBrowserTest,
                       CrossOriginSubframeWithoutEmbeddingControlOtherCSP) {
  set_monitored_feature(
      WebFeature::kCrossOriginSubframeWithoutEmbeddingControl);
  GURL main_document_url = https_server().GetURL("a.com", "/title1.html");
  GURL sub_document_url =
      https_server().GetURL("b.com",
                            "/set-header?"
                            "Content-Security-Policy: script-src 'self';");
  EXPECT_TRUE(content::NavigateToURL(web_contents(), main_document_url));
  LoadIFrame(sub_document_url);
  ExpectHistogramIncreasedBy(1);
}

// Check kEmbeddedCrossOriginFrameWithoutFrameAncestorsOrXFO feature usage.
// This should increment in cases where a cross-origin frame is embedded which
// does not assert either X-Frame-Options or CSP's frame-ancestors.
IN_PROC_BROWSER_TEST_F(ChromeWebPlatformSecurityMetricsBrowserTest,
                       EmbeddingOptIn) {
  set_monitored_feature(
      WebFeature::kEmbeddedCrossOriginFrameWithoutFrameAncestorsOrXFO);
  GURL main_document_url = https_server().GetURL("a.com", "/title1.html");

  struct TestCase {
    const char* name;
    const char* host;
    const char* header;
    bool expect_counter;
  } cases[] = {{
                   "Same-origin, no XFO, no frame-ancestors",
                   "a.com",
                   nullptr,
                   false,
               },
               {
                   "Cross-origin, no XFO, no frame-ancestors",
                   "b.com",
                   nullptr,
                   true,
               },
               {
                   "Same-origin, yes XFO, no frame-ancestors",
                   "a.com",
                   "X-Frame-Options: ALLOWALL",
                   false,
               },
               {
                   "Cross-origin, yes XFO, no frame-ancestors",
                   "b.com",
                   "X-Frame-Options: ALLOWALL",
                   false,
               },
               {
                   "Same-origin, no XFO, yes frame-ancestors",
                   "a.com",
                   "Content-Security-Policy: frame-ancestors *",
                   false,
               },
               {
                   "Cross-origin, no XFO, yes frame-ancestors",
                   "b.com",
                   "Content-Security-Policy: frame-ancestors *",
                   false,
               }};

  for (auto test : cases) {
    SCOPED_TRACE(test.name);
    EXPECT_TRUE(content::NavigateToURL(web_contents(), main_document_url));

    std::string path = "/set-header?";
    if (test.header)
      path += test.header;
    GURL url = https_server().GetURL(test.host, path);
    LoadIFrame(url);

    ExpectHistogramIncreasedBy(test.expect_counter ? 1 : 0);
  }
}

IN_PROC_BROWSER_TEST_F(ChromeWebPlatformSecurityMetricsBrowserTest,
                       NonCrossOriginIsolatedCheckSabConstructor) {
  GURL url = https_server().GetURL("a.com", "/empty.html");
  EXPECT_TRUE(NavigateToURL(web_contents(), url));
  EXPECT_EQ(true, content::EvalJs(web_contents(),
                                  "'SharedArrayBuffer' in globalThis"));
  CheckCounter(WebFeature::kV8SharedArrayBufferConstructedWithoutIsolation, 0);
  CheckCounter(WebFeature::kV8SharedArrayBufferConstructed, 0);
}

IN_PROC_BROWSER_TEST_F(ChromeWebPlatformSecurityMetricsBrowserTest,
                       NonCrossOriginIsolatedSabSizeZero) {
  GURL url = https_server().GetURL("a.com", "/empty.html");
  EXPECT_TRUE(content::NavigateToURL(web_contents(), url));
  EXPECT_EQ(true, content::ExecJs(web_contents(), "new SharedArrayBuffer(0)"));
  CheckCounter(WebFeature::kV8SharedArrayBufferConstructedWithoutIsolation, 1);
  CheckCounter(WebFeature::kV8SharedArrayBufferConstructed, 0);
}

IN_PROC_BROWSER_TEST_F(ChromeWebPlatformSecurityMetricsBrowserTest,
                       NonCrossOriginIsolatedSab) {
  GURL url = https_server().GetURL("a.com", "/empty.html");
  EXPECT_TRUE(content::NavigateToURL(web_contents(), url));
  EXPECT_EQ(true,
            content::ExecJs(web_contents(), "new SharedArrayBuffer(8192)"));
  CheckCounter(WebFeature::kV8SharedArrayBufferConstructedWithoutIsolation, 1);
  CheckCounter(WebFeature::kV8SharedArrayBufferConstructed, 0);
}

IN_PROC_BROWSER_TEST_F(ChromeWebPlatformSecurityMetricsBrowserTest,
                       CrossOriginIsolatedSab) {
  GURL url =
      https_server().GetURL("a.com",
                            "/set-header"
                            "?Cross-Origin-Opener-Policy: same-origin"
                            "&Cross-Origin-Embedder-Policy: require-corp");
  EXPECT_TRUE(content::NavigateToURL(web_contents(), url));
  EXPECT_EQ(true,
            content::ExecJs(web_contents(), "new SharedArrayBuffer(8192)"));
  CheckCounter(WebFeature::kV8SharedArrayBufferConstructedWithoutIsolation, 0);
  CheckCounter(WebFeature::kV8SharedArrayBufferConstructed, 1);
}

IN_PROC_BROWSER_TEST_F(ChromeWebPlatformSecurityMetricsBrowserTest,
                       WasmMemorySharingCrossSite) {
  GURL main_url = https_server().GetURL("a.com", "/empty.html");
  GURL sub_url = https_server().GetURL("b.com", "/empty.html");

  EXPECT_TRUE(content::NavigateToURL(web_contents(), main_url));
  LoadIFrame(sub_url);

  content::RenderFrameHost* main_document =
      web_contents()->GetPrimaryMainFrame();
  content::RenderFrameHost* sub_document = ChildFrameAt(main_document, 0);

  EXPECT_EQ(true, content::ExecJs(main_document, R"(
    received_memory = undefined;
    addEventListener("message", event => {
      received_memory = event.data;
    });
  )"));

  EXPECT_EQ(true, content::ExecJs(sub_document, R"(
    const memory = new WebAssembly.Memory({
      initial:1,
      maximum:1,
      shared:true
    });
    parent.postMessage(memory, "*");
  )"));

  // It doesn't exist yet a warning or an error being dispatched for failing to
  // send a WebAssembly.Memory. This test simply wait.
  EXPECT_EQ("Success: Nothing received", content::EvalJs(main_document, R"(
    new Promise(async resolve => {
      await new Promise(r => setTimeout(r, 1000));
      if (received_memory)
        resolve("Failure: Received Webassembly Memory");
      else
        resolve("Success: Nothing received");
    });
  )"));

  CheckCounter(WebFeature::kV8SharedArrayBufferConstructedWithoutIsolation, 1);
  CheckCounter(WebFeature::kV8SharedArrayBufferConstructed, 0);
}

IN_PROC_BROWSER_TEST_F(ChromeWebPlatformSecurityMetricsBrowserTest,
                       WasmMemorySharingCrossOrigin) {
  GURL main_url = https_server().GetURL("a.a.com", "/empty.html");
  GURL sub_url = https_server().GetURL("b.a.com", "/empty.html");

  EXPECT_TRUE(content::NavigateToURL(web_contents(), main_url));
  LoadIFrame(sub_url);

  content::RenderFrameHost* main_document =
      web_contents()->GetPrimaryMainFrame();
  content::RenderFrameHost* sub_document = ChildFrameAt(main_document, 0);

  EXPECT_EQ(true, content::ExecJs(main_document, R"(
    addEventListener("message", event => {
      received_memory = event.data;
    });
  )"));

  EXPECT_EQ(true, content::ExecJs(sub_document, R"(
    const memory = new WebAssembly.Memory({
      initial:1,
      maximum:1,
      shared:true
    });
    parent.postMessage(memory, "*");
  )"));

  EXPECT_EQ(1 * kWasmPageSize, content::EvalJs(main_document, R"(
    new Promise(async resolve => {
      while (!received_memory)
        await new Promise(r => setTimeout(r, 10));
      resolve(received_memory.buffer.byteLength);
    });
  )"));

  CheckCounter(WebFeature::kV8SharedArrayBufferConstructedWithoutIsolation, 1);
  CheckCounter(WebFeature::kV8SharedArrayBufferConstructed, 0);
}

IN_PROC_BROWSER_TEST_F(ChromeWebPlatformSecurityMetricsBrowserTest,
                       WasmMemorySharingSameOrigin) {
  GURL main_url = https_server().GetURL("a.com", "/empty.html");
  GURL sub_url = https_server().GetURL("a.com", "/empty.html");

  EXPECT_TRUE(content::NavigateToURL(web_contents(), main_url));
  LoadIFrame(sub_url);

  content::RenderFrameHost* main_document =
      web_contents()->GetPrimaryMainFrame();
  content::RenderFrameHost* sub_document = ChildFrameAt(main_document, 0);

  EXPECT_EQ(true, content::ExecJs(main_document, R"(
    received_memory = undefined;
    addEventListener("message", event => {
      received_memory = event.data;
    });
  )"));

  EXPECT_EQ(true, content::ExecJs(sub_document, R"(
    const memory = new WebAssembly.Memory({
      initial:1,
      maximum:1,
      shared:true
    });
    parent.postMessage(memory, "*");
  )"));

  EXPECT_EQ(1 * kWasmPageSize, content::EvalJs(main_document, R"(
    new Promise(async resolve => {
      while (!received_memory)
        await new Promise(r => setTimeout(r, 10));
      resolve(received_memory.buffer.byteLength);
    });
  )"));

  CheckCounter(WebFeature::kV8SharedArrayBufferConstructedWithoutIsolation, 1);
  CheckCounter(WebFeature::kV8SharedArrayBufferConstructed, 0);
}

IN_PROC_BROWSER_TEST_F(ChromeWebPlatformSecurityMetricsBrowserTest,
                       WasmMemorySharingCrossOriginBeforeSetDocumentDomain) {
  GURL main_url = https_server().GetURL("sub.a.com", "/empty.html");
  GURL sub_url = https_server().GetURL("a.com", "/empty.html");

  EXPECT_TRUE(content::NavigateToURL(web_contents(), main_url));
  LoadIFrame(sub_url);

  content::RenderFrameHost* main_document =
      web_contents()->GetPrimaryMainFrame();
  content::RenderFrameHost* sub_document = ChildFrameAt(main_document, 0);

  EXPECT_EQ(true, content::ExecJs(main_document, R"(
    document.domain = "a.com";
    received_memory = undefined;
    addEventListener("message", event => {
      received_memory = event.data;
    });
  )"));

  EXPECT_EQ(true, content::ExecJs(sub_document, R"(
    document.domain = "a.com";
    const memory = new WebAssembly.Memory({
      initial:1,
      maximum:1,
      shared:true
    });
    parent.postMessage(memory, "*");
  )"));

  EXPECT_EQ(1 * kWasmPageSize, content::EvalJs(main_document, R"(
    new Promise(async resolve => {
      while (!received_memory)
        await new Promise(r => setTimeout(r, 10));
      resolve(received_memory.buffer.byteLength);
    });
  )"));

  CheckCounter(WebFeature::kV8SharedArrayBufferConstructedWithoutIsolation, 1);
  CheckCounter(WebFeature::kV8SharedArrayBufferConstructed, 0);
}

IN_PROC_BROWSER_TEST_F(ChromeWebPlatformSecurityMetricsBrowserTest,
                       WasmMemorySharingCrossOriginAfterSetDocumentDomain) {
  GURL main_url = https_server().GetURL("sub.a.com", "/empty.html");
  GURL sub_url = https_server().GetURL("sub.a.com", "/empty.html");

  EXPECT_TRUE(content::NavigateToURL(web_contents(), main_url));
  LoadIFrame(sub_url);

  content::RenderFrameHost* main_document =
      web_contents()->GetPrimaryMainFrame();
  content::RenderFrameHost* sub_document = ChildFrameAt(main_document, 0);

  EXPECT_EQ(true, content::ExecJs(main_document, R"(
    document.domain = "a.com";
    received_memory = undefined;
    addEventListener("message", event => {
      received_memory = event.data;
    });
  )"));

  EXPECT_EQ(true, content::ExecJs(sub_document, R"(
    document.domain = "sub.a.com";
    const memory = new WebAssembly.Memory({
      initial:1,
      maximum:1,
      shared:true
    });
    parent.postMessage(memory, "*");
  )"));

  EXPECT_EQ(1 * kWasmPageSize, content::EvalJs(main_document, R"(
    new Promise(async resolve => {
      while (!received_memory)
        await new Promise(r => setTimeout(r, 10));
      resolve(received_memory.buffer.byteLength);
    });
  )"));

  CheckCounter(WebFeature::kV8SharedArrayBufferConstructedWithoutIsolation, 1);
  CheckCounter(WebFeature::kV8SharedArrayBufferConstructed, 0);
}

IN_PROC_BROWSER_TEST_F(ChromeWebPlatformSecurityMetricsBrowserTest,
                       WasmMemorySharingCrossOriginIsolated) {
  GURL url =
      https_server().GetURL("a.com",
                            "/set-header"
                            "?Cross-Origin-Opener-Policy: same-origin"
                            "&Cross-Origin-Embedder-Policy: require-corp");
  EXPECT_TRUE(content::NavigateToURL(web_contents(), url));
  LoadIFrame(url);

  content::RenderFrameHost* main_document =
      web_contents()->GetPrimaryMainFrame();
  content::RenderFrameHost* sub_document = ChildFrameAt(main_document, 0);

  EXPECT_EQ(true, content::ExecJs(main_document, R"(
    addEventListener("message", event => {
      received_memory = event.data;
    });
  )"));

  EXPECT_EQ(true, content::ExecJs(sub_document, R"(
    const memory = new WebAssembly.Memory({
      initial:1,
      maximum:1,
      shared:true
    });
    parent.postMessage(memory, "*");
  )"));

  EXPECT_EQ(1 * kWasmPageSize, content::EvalJs(main_document, R"(
    new Promise(async resolve => {
      while (!received_memory)
        await new Promise(r => setTimeout(r, 10));
      resolve(received_memory.buffer.byteLength);
    });
  )"));

  CheckCounter(WebFeature::kV8SharedArrayBufferConstructedWithoutIsolation, 0);
  CheckCounter(WebFeature::kV8SharedArrayBufferConstructed, 1);
}

IN_PROC_BROWSER_TEST_F(ChromeWebPlatformSecurityMetricsBrowserTest,
                       WasmModuleSharingCrossSite) {
  GURL main_url = https_server().GetURL("a.com", "/empty.html");
  GURL sub_url = https_server().GetURL("b.com", "/empty.html");

  EXPECT_TRUE(content::NavigateToURL(web_contents(), main_url));
  LoadIFrame(sub_url);

  content::RenderFrameHost* main_document =
      web_contents()->GetPrimaryMainFrame();
  content::RenderFrameHost* sub_document = ChildFrameAt(main_document, 0);

  EXPECT_EQ(true, content::ExecJs(main_document, R"(
    received_module = undefined;
    addEventListener("message", event => {
      received_module = event.data;
    });
  )"));

  EXPECT_EQ(true, content::ExecJs(sub_document, R"(
    let module = new WebAssembly.Module(new Uint8Array([
      0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00]));
    parent.postMessage(module, "*");
  )"));

  // It doesn't exist yet a warning or an error being dispatched for failing to
  // send a WebAssembly.Module. This test simply wait.
  EXPECT_EQ("Success: Nothing received", content::EvalJs(main_document, R"(
    new Promise(async resolve => {
      await new Promise(r => setTimeout(r, 1000));
      if (received_module)
        resolve("Failure: Received Webassembly module");
      else
        resolve("Success: Nothing received");
    });
  )"));

  CheckCounter(WebFeature::kV8SharedArrayBufferConstructedWithoutIsolation, 0);
  CheckCounter(WebFeature::kV8SharedArrayBufferConstructed, 0);

  // TODO(ahaas): Check the histogram for:
  // - kWasmModuleSharing
  // - kCrossOriginWasmModuleSharing
}

IN_PROC_BROWSER_TEST_F(ChromeWebPlatformSecurityMetricsBrowserTest,
                       WasmModuleSharingSameSite) {
  GURL main_url = https_server().GetURL("a.a.com", "/empty.html");
  GURL sub_url = https_server().GetURL("b.a.com", "/empty.html");

  EXPECT_TRUE(content::NavigateToURL(web_contents(), main_url));
  LoadIFrame(sub_url);

  content::RenderFrameHost* main_document =
      web_contents()->GetPrimaryMainFrame();
  content::RenderFrameHost* sub_document = ChildFrameAt(main_document, 0);

  EXPECT_EQ(true, content::ExecJs(main_document, R"(
    received_module = undefined;
    addEventListener("message", event => {
      received_module = event.data;
    });
  )"));

  EXPECT_EQ(true, content::ExecJs(sub_document, R"(
    let module = new WebAssembly.Module(new Uint8Array([
      0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00]));
    parent.postMessage(module, "*");
  )"));

  // It doesn't exist yet a warning or an error being dispatched for failing to
  // send a WebAssembly.Module. This test simply wait.
  EXPECT_EQ("Success: Nothing received", content::EvalJs(main_document, R"(
    new Promise(async resolve => {
      await new Promise(r => setTimeout(r, 1000));
      if (received_module)
        resolve("Failure: Received Webassembly module");
      else
        resolve("Success: Nothing received");
    });
  )"));

  CheckCounter(WebFeature::kV8SharedArrayBufferConstructedWithoutIsolation, 0);
  CheckCounter(WebFeature::kV8SharedArrayBufferConstructed, 0);
}

IN_PROC_BROWSER_TEST_F(ChromeWebPlatformSecurityMetricsBrowserTest,
                       WasmModuleSharingSameOrigin) {
  GURL main_url = https_server().GetURL("a.com", "/empty.html");
  GURL sub_url = https_server().GetURL("a.com", "/empty.html");

  EXPECT_TRUE(content::NavigateToURL(web_contents(), main_url));
  LoadIFrame(sub_url);

  content::RenderFrameHost* main_document =
      web_contents()->GetPrimaryMainFrame();
  content::RenderFrameHost* sub_document = ChildFrameAt(main_document, 0);

  EXPECT_EQ(true, content::ExecJs(main_document, R"(
    received_module = undefined;
    addEventListener("message", event => {
      received_module = event.data;
    });
  )"));

  EXPECT_EQ(true, content::ExecJs(sub_document, R"(
    let module = new WebAssembly.Module(new Uint8Array([
      0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00]));
    parent.postMessage(module, "*");
  )"));

  EXPECT_EQ(true, content::EvalJs(main_document, R"(
    new Promise(async resolve => {
      while (!received_module)
        await new Promise(r => setTimeout(r, 10));
      resolve(true);
    });
  )"));

  CheckCounter(WebFeature::kV8SharedArrayBufferConstructedWithoutIsolation, 0);
  CheckCounter(WebFeature::kV8SharedArrayBufferConstructed, 0);

  // TODO(ahaas): Check the histogram for:
  // - kWasmModuleSharing
  // - kCrossOriginWasmModuleSharing
}

IN_PROC_BROWSER_TEST_F(ChromeWebPlatformSecurityMetricsBrowserTest,
                       WasmModuleSharingSameSiteBeforeSetDocumentDomain) {
  GURL main_url = https_server().GetURL("sub.a.com", "/empty.html");
  GURL sub_url = https_server().GetURL("a.com", "/empty.html");

  EXPECT_TRUE(content::NavigateToURL(web_contents(), main_url));
  LoadIFrame(sub_url);

  content::RenderFrameHost* main_document =
      web_contents()->GetPrimaryMainFrame();
  content::RenderFrameHost* sub_document = ChildFrameAt(main_document, 0);

  EXPECT_EQ(true, content::ExecJs(main_document, R"(
    document.domain = "a.com";
    received_module = undefined;
    addEventListener("message", event => {
      received_module = event.data;
    });
  )"));

  EXPECT_EQ(true, content::ExecJs(sub_document, R"(
    document.domain = "a.com";
    let module = new WebAssembly.Module(new Uint8Array([
      0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00]));
    parent.postMessage(module, "*");
  )"));

  // It doesn't exist yet a warning or an error being dispatched for failing to
  // send a WebAssembly.Module. This test simply wait.
  EXPECT_EQ("Success: Nothing received", content::EvalJs(main_document, R"(
    new Promise(async resolve => {
      await new Promise(r => setTimeout(r, 1000));
      if (received_module)
        resolve("Failure: Received Webassembly module");
      else
        resolve("Success: Nothing received");
    });
  )"));

  CheckCounter(WebFeature::kV8SharedArrayBufferConstructedWithoutIsolation, 0);
  CheckCounter(WebFeature::kV8SharedArrayBufferConstructed, 0);
}

IN_PROC_BROWSER_TEST_F(ChromeWebPlatformSecurityMetricsBrowserTest,
                       WasmModuleSharingSameSiteAfterSetDocumentDomain) {
  GURL main_url = https_server().GetURL("sub.a.com", "/empty.html");
  GURL sub_url = https_server().GetURL("sub.a.com", "/empty.html");

  EXPECT_TRUE(content::NavigateToURL(web_contents(), main_url));
  LoadIFrame(sub_url);

  content::RenderFrameHost* main_document =
      web_contents()->GetPrimaryMainFrame();
  content::RenderFrameHost* sub_document = ChildFrameAt(main_document, 0);

  EXPECT_EQ(true, content::ExecJs(main_document, R"(
    document.domain = "a.com";
    received_module = undefined;
    addEventListener("message", event => {
      received_module = event.data;
    });
  )"));

  EXPECT_EQ(true, content::ExecJs(sub_document, R"(
    document.domain = "sub.a.com";
    let module = new WebAssembly.Module(new Uint8Array([
      0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00]));
    parent.postMessage(module, "*");
  )"));

  EXPECT_EQ(true, content::EvalJs(main_document, R"(
    new Promise(async resolve => {
      while (!received_module)
        await new Promise(r => setTimeout(r, 10));
      resolve(true);
    });
  )"));

  CheckCounter(WebFeature::kV8SharedArrayBufferConstructedWithoutIsolation, 0);
  CheckCounter(WebFeature::kV8SharedArrayBufferConstructed, 0);

  // TODO(ahaas): Check the histogram for:
  // - kWasmModuleSharing
  // - kCrossOriginWasmModuleSharing
}

// Check that two pages with same-origin documents do not get reported when the
// COOP status is the same.
IN_PROC_BROWSER_TEST_F(ChromeWebPlatformSecurityMetricsBrowserTest,
                       SameOriginDocumentsWithSameCOOPStatus) {
  set_monitored_feature(
      WebFeature::kSameOriginDocumentsWithDifferentCOOPStatus);
  GURL main_document_url = https_server().GetURL("a.com",
                                                 "/set-header?"
                                                 "Cross-Origin-Opener-Policy: "
                                                 "same-origin-allow-popups");
  EXPECT_TRUE(content::NavigateToURL(web_contents(), main_document_url));
  OpenPopup(main_document_url);
  ExpectHistogramIncreasedBy(0);
}

// Check that two pages with same-origin documents do get reported when the
// COOP status is not the same and they are in the same browsing context group.
IN_PROC_BROWSER_TEST_F(ChromeWebPlatformSecurityMetricsBrowserTest,
                       SameOriginDocumentsWithDifferentCOOPStatus) {
  set_monitored_feature(
      WebFeature::kSameOriginDocumentsWithDifferentCOOPStatus);
  GURL main_document_url = https_server().GetURL("a.com",
                                                 "/set-header?"
                                                 "Cross-Origin-Opener-Policy: "
                                                 "same-origin-allow-popups");
  GURL no_coop_url = https_server().GetURL("a.com", "/empty.html");
  EXPECT_TRUE(content::NavigateToURL(web_contents(), main_document_url));
  OpenPopup(no_coop_url);
  ExpectHistogramIncreasedBy(1);
}

// Check that two pages with same-origin documents do not get reported when the
// COOP status is not the same but they are in different browsing context
// groups.
IN_PROC_BROWSER_TEST_F(ChromeWebPlatformSecurityMetricsBrowserTest,
                       SameOriginDocumentsWithDifferentCOOPStatusBCGSwitch) {
  set_monitored_feature(
      WebFeature::kSameOriginDocumentsWithDifferentCOOPStatus);
  GURL main_document_url = https_server().GetURL("a.com",
                                                 "/set-header?"
                                                 "Cross-Origin-Opener-Policy: "
                                                 "same-origin-allow-popups");
  GURL coop_same_origin_url =
      https_server().GetURL("a.com",
                            "/set-header?"
                            "Cross-Origin-Opener-Policy: "
                            "same-origin");
  EXPECT_TRUE(content::NavigateToURL(web_contents(), main_document_url));
  OpenPopup(coop_same_origin_url);
  ExpectHistogramIncreasedBy(0);
}

// Check that two pages with two different COOP status are not reported when
// their documents are cross-origin.
IN_PROC_BROWSER_TEST_F(ChromeWebPlatformSecurityMetricsBrowserTest,
                       CrossOriginDocumentsWithNoCOOPStatus) {
  set_monitored_feature(
      WebFeature::kSameOriginDocumentsWithDifferentCOOPStatus);
  GURL main_document_url = https_server().GetURL("a.com",
                                                 "/set-header?"
                                                 "Cross-Origin-Opener-Policy: "
                                                 "same-origin-allow-popups");
  GURL no_coop_url = https_server().GetURL("b.com", "/empty.html");
  EXPECT_TRUE(content::NavigateToURL(web_contents(), main_document_url));
  OpenPopup(no_coop_url);
  ExpectHistogramIncreasedBy(0);
}

// Check that a COOP same-origin-allow-popups page with a cross-origin iframe
// that opens a popup to the same origin document gets reported.
IN_PROC_BROWSER_TEST_F(ChromeWebPlatformSecurityMetricsBrowserTest,
                       COOPSameOriginAllowPopupsIframeAndPopup) {
  set_monitored_feature(
      WebFeature::kSameOriginDocumentsWithDifferentCOOPStatus);
  GURL main_document_url = https_server().GetURL("a.com",
                                                 "/set-header?"
                                                 "Cross-Origin-Opener-Policy: "
                                                 "same-origin-allow-popups");
  GURL no_coop_url = https_server().GetURL("b.com", "/empty.html");
  EXPECT_TRUE(content::NavigateToURL(web_contents(), main_document_url));
  LoadIFrame(no_coop_url);
  OpenPopup(no_coop_url);
  ExpectHistogramIncreasedBy(1);
}

// Check that an iframe that is same-origin with its opener of a different COOP
// status gets reported.
IN_PROC_BROWSER_TEST_F(ChromeWebPlatformSecurityMetricsBrowserTest,
                       SameOriginIframeInCrossOriginPopupWithCOOP) {
  set_monitored_feature(
      WebFeature::kSameOriginDocumentsWithDifferentCOOPStatus);
  GURL main_document_url = https_server().GetURL("a.com",
                                                 "/set-header?"
                                                 "Cross-Origin-Opener-Policy: "
                                                 "same-origin-allow-popups");
  GURL no_coop_url = https_server().GetURL("b.com", "/empty.html");
  GURL same_origin_url = https_server().GetURL("a.com", "/empty.html");
  EXPECT_TRUE(content::NavigateToURL(web_contents(), main_document_url));
  content::WebContents* popup = OpenPopup(no_coop_url);
  LoadIFrameInWebContents(popup, same_origin_url);
  ExpectHistogramIncreasedBy(1);
}

// Check that two same-origin iframes in pages with different COOP status gets
// reported.
IN_PROC_BROWSER_TEST_F(ChromeWebPlatformSecurityMetricsBrowserTest,
                       IFramesWithDifferentCOOPStatus) {
  set_monitored_feature(
      WebFeature::kSameOriginDocumentsWithDifferentCOOPStatus);
  GURL main_document_url = https_server().GetURL("a.com",
                                                 "/set-header?"
                                                 "Cross-Origin-Opener-Policy: "
                                                 "same-origin-allow-popups");
  GURL popup_url = https_server().GetURL("b.com", "/empty.html");
  GURL iframe_url = https_server().GetURL("c.com", "/empty.html");
  EXPECT_TRUE(content::NavigateToURL(web_contents(), main_document_url));
  LoadIFrame(iframe_url);
  content::WebContents* popup = OpenPopup(popup_url);
  LoadIFrameInWebContents(popup, iframe_url);
  ExpectHistogramIncreasedBy(1);
}

// Check that when two pages both have frames that are same-origin with a
// document in the other page and have different COOP status, the metrics is
// only recorded once.
IN_PROC_BROWSER_TEST_F(ChromeWebPlatformSecurityMetricsBrowserTest,
                       SameOriginDifferentCOOPStatusRecordedOnce) {
  set_monitored_feature(
      WebFeature::kSameOriginDocumentsWithDifferentCOOPStatus);
  GURL main_document_url = https_server().GetURL("a.com",
                                                 "/set-header?"
                                                 "Cross-Origin-Opener-Policy: "
                                                 "same-origin-allow-popups");
  GURL popup_url = https_server().GetURL("b.com", "/empty.html");
  GURL same_origin_url = https_server().GetURL("a.com", "/empty.html");
  EXPECT_TRUE(content::NavigateToURL(web_contents(), main_document_url));
  content::WebContents* popup = OpenPopup(popup_url);
  LoadIFrame(popup_url);
  LoadIFrameInWebContents(popup, same_origin_url);
  ExpectHistogramIncreasedBy(1);
}

// Check that when two pages COOP same-origin-allow-popups have frames that are
// same-origin with a COOP unsafe-none, the metrcis is recorded twice (once per
// COOP same-origin-allow-popups page).
IN_PROC_BROWSER_TEST_F(ChromeWebPlatformSecurityMetricsBrowserTest,
                       SameOriginDifferentCOOPStatusTwoCOOPPages) {
  set_monitored_feature(
      WebFeature::kSameOriginDocumentsWithDifferentCOOPStatus);
  GURL main_document_url = https_server().GetURL("a.com",
                                                 "/set-header?"
                                                 "Cross-Origin-Opener-Policy: "
                                                 "same-origin-allow-popups");
  GURL same_origin_url = https_server().GetURL("a.com", "/empty.html");
  EXPECT_TRUE(content::NavigateToURL(web_contents(), main_document_url));
  OpenPopup(main_document_url);
  OpenPopup(same_origin_url);
  ExpectHistogramIncreasedBy(2);
}

IN_PROC_BROWSER_TEST_F(ChromeWebPlatformSecurityMetricsBrowserTest,
                       CoepNoneMainFrame) {
  GURL url = https_server().GetURL("a.com",
                                   "/set-header?"
                                   "Cross-Origin-Embedder-Policy: unsafe-none");
  EXPECT_TRUE(content::NavigateToURL(web_contents(), url));
  CheckCounter(WebFeature::kCrossOriginEmbedderPolicyRequireCorp, 0);
  CheckCounter(WebFeature::kCrossOriginEmbedderPolicyCredentialless, 0);
  CheckCounter(WebFeature::kCrossOriginEmbedderPolicyRequireCorpReportOnly, 0);
  CheckCounter(WebFeature::kCrossOriginEmbedderPolicyCredentiallessReportOnly,
               0);
}

IN_PROC_BROWSER_TEST_F(ChromeWebPlatformSecurityMetricsBrowserTest,
                       CoepCredentiallessMainFrame) {
  GURL url =
      https_server().GetURL("a.com",
                            "/set-header?"
                            "Cross-Origin-Embedder-Policy: credentialless");
  EXPECT_TRUE(content::NavigateToURL(web_contents(), url));
  CheckCounter(WebFeature::kCrossOriginEmbedderPolicyRequireCorp, 0);
  CheckCounter(WebFeature::kCrossOriginEmbedderPolicyCredentialless, 1);
  CheckCounter(WebFeature::kCrossOriginEmbedderPolicyRequireCorpReportOnly, 0);
  CheckCounter(WebFeature::kCrossOriginEmbedderPolicyCredentiallessReportOnly,
               0);

  CheckCounterMainFrame(WebFeature::kCrossOriginEmbedderPolicyCredentialless,
                        1);
}

IN_PROC_BROWSER_TEST_F(ChromeWebPlatformSecurityMetricsBrowserTest,
                       CoepRequireCorpMainFrame) {
  GURL url =
      https_server().GetURL("a.com",
                            "/set-header?"
                            "Cross-Origin-Embedder-Policy: require-corp");
  EXPECT_TRUE(content::NavigateToURL(web_contents(), url));
  CheckCounter(WebFeature::kCrossOriginEmbedderPolicyRequireCorp, 1);
  CheckCounter(WebFeature::kCrossOriginEmbedderPolicyCredentialless, 0);
  CheckCounter(WebFeature::kCrossOriginEmbedderPolicyRequireCorpReportOnly, 0);
  CheckCounter(WebFeature::kCrossOriginEmbedderPolicyCredentiallessReportOnly,
               0);

  CheckCounterMainFrame(WebFeature::kCrossOriginEmbedderPolicyRequireCorp, 1);
}

IN_PROC_BROWSER_TEST_F(ChromeWebPlatformSecurityMetricsBrowserTest,
                       CoepReportOnlyCredentiallessMainFrame) {
  GURL url = https_server().GetURL(
      "a.com",
      "/set-header?"
      "Cross-Origin-Embedder-Policy-Report-Only: credentialless");
  EXPECT_TRUE(content::NavigateToURL(web_contents(), url));
  CheckCounter(WebFeature::kCrossOriginEmbedderPolicyRequireCorp, 0);
  CheckCounter(WebFeature::kCrossOriginEmbedderPolicyCredentialless, 0);
  CheckCounter(WebFeature::kCrossOriginEmbedderPolicyRequireCorpReportOnly, 0);
  CheckCounter(WebFeature::kCrossOriginEmbedderPolicyCredentiallessReportOnly,
               1);
}

IN_PROC_BROWSER_TEST_F(ChromeWebPlatformSecurityMetricsBrowserTest,
                       CoepReportOnlyRequireCorpMainFrame) {
  GURL url = https_server().GetURL(
      "a.com",
      "/set-header?"
      "Cross-Origin-Embedder-Policy-Report-Only: require-corp");
  EXPECT_TRUE(content::NavigateToURL(web_contents(), url));
  CheckCounter(WebFeature::kCrossOriginEmbedderPolicyRequireCorp, 0);
  CheckCounter(WebFeature::kCrossOriginEmbedderPolicyCredentialless, 0);
  CheckCounter(WebFeature::kCrossOriginEmbedderPolicyRequireCorpReportOnly, 1);
  CheckCounter(WebFeature::kCrossOriginEmbedderPolicyCredentiallessReportOnly,
               0);
}

IN_PROC_BROWSER_TEST_F(ChromeWebPlatformSecurityMetricsBrowserTest,
                       CoopAndCoepIsolatedMainFrame) {
  GURL url =
      https_server().GetURL("a.com",
                            "/set-header?"
                            "Cross-Origin-Embedder-Policy: credentialless&"
                            "Cross-Origin-Opener-Policy: same-origin");
  EXPECT_TRUE(content::NavigateToURL(web_contents(), url));
  CheckCounter(WebFeature::kCoopAndCoepIsolated, 1);
  CheckCounter(WebFeature::kCoopAndCoepIsolatedReportOnly, 0);
}

IN_PROC_BROWSER_TEST_F(ChromeWebPlatformSecurityMetricsBrowserTest,
                       CoopAndCoepIsolatedEnforcedReportOnlyMainFrame) {
  GURL url = https_server().GetURL(
      "a.com",
      "/set-header?"
      "Cross-Origin-Embedder-Policy: credentialless&"
      "Cross-Origin-Embedder-Policy-Report-Only: credentialless&"
      "Cross-Origin-Opener-Policy: same-origin&"
      "Cross-Origin-Opener-Policy-Report-Only: same-origin");
  EXPECT_TRUE(content::NavigateToURL(web_contents(), url));
  CheckCounter(WebFeature::kCoopAndCoepIsolated, 1);
  CheckCounter(WebFeature::kCoopAndCoepIsolatedReportOnly, 1);
}

IN_PROC_BROWSER_TEST_F(ChromeWebPlatformSecurityMetricsBrowserTest,
                       CoopAndCoepIsolatedMainFrameReportOnly) {
  GURL url = https_server().GetURL(
      "a.com",
      "/set-header?"
      "Cross-Origin-Embedder-Policy: credentialless&"
      "Cross-Origin-Opener-Policy-Report-Only: same-origin");
  EXPECT_TRUE(content::NavigateToURL(web_contents(), url));
  CheckCounter(WebFeature::kCoopAndCoepIsolated, 0);
  CheckCounter(WebFeature::kCoopAndCoepIsolatedReportOnly, 1);
}

IN_PROC_BROWSER_TEST_F(ChromeWebPlatformSecurityMetricsBrowserTest,
                       CoopAndCoepIsolatedIframe) {
  GURL main_url = https_server().GetURL("a.com", "/set-header?");
  EXPECT_TRUE(content::NavigateToURL(web_contents(), main_url));
  GURL child_url =
      https_server().GetURL("a.com",
                            "/set-header?"
                            "Cross-Origin-Embedder-Policy: credentialless&"
                            "Cross-Origin-Opener-Policy: same-origin");
  LoadIFrame(child_url);
  EXPECT_TRUE(content::WaitForLoadStop(web_contents()));
  CheckCounter(WebFeature::kCoopAndCoepIsolated, 0);
  CheckCounter(WebFeature::kCoopAndCoepIsolatedReportOnly, 0);
}

IN_PROC_BROWSER_TEST_F(ChromeWebPlatformSecurityMetricsBrowserTest,
                       CoepRequireCorpEmbedsCredentialless) {
  GURL main_url =
      https_server().GetURL("a.com",
                            "/set-header?"
                            "Cross-Origin-Embedder-Policy: require-corp");
  EXPECT_TRUE(content::NavigateToURL(web_contents(), main_url));
  CheckCounter(WebFeature::kCrossOriginEmbedderPolicyCredentialless, 0);
  CheckCounter(WebFeature::kCrossOriginEmbedderPolicyRequireCorp, 1);
  GURL child_url =
      https_server().GetURL("a.com",
                            "/set-header?"
                            "Cross-Origin-Embedder-Policy: credentialless");
  LoadIFrame(child_url);
  EXPECT_TRUE(content::WaitForLoadStop(web_contents()));
  CheckCounter(WebFeature::kCrossOriginEmbedderPolicyCredentialless, 1);
  CheckCounter(WebFeature::kCrossOriginEmbedderPolicyRequireCorp, 1);
  CheckCounterMainFrame(WebFeature::kCrossOriginEmbedderPolicyCredentialless,
                        0);
  CheckCounterMainFrame(WebFeature::kCrossOriginEmbedderPolicyRequireCorp, 1);
}

IN_PROC_BROWSER_TEST_F(ChromeWebPlatformSecurityMetricsBrowserTest,
                       CoepCredentiallessEmbedsRequireCorp) {
  GURL main_url =
      https_server().GetURL("a.com",
                            "/set-header?"
                            "Cross-Origin-Embedder-Policy: credentialless");
  EXPECT_TRUE(content::NavigateToURL(web_contents(), main_url));
  CheckCounter(WebFeature::kCrossOriginEmbedderPolicyCredentialless, 1);
  CheckCounter(WebFeature::kCrossOriginEmbedderPolicyRequireCorp, 0);
  GURL child_url =
      https_server().GetURL("a.com",
                            "/set-header?"
                            "Cross-Origin-Embedder-Policy: require-corp");
  LoadIFrame(child_url);
  CheckCounter(WebFeature::kCrossOriginEmbedderPolicyCredentialless, 1);
  CheckCounter(WebFeature::kCrossOriginEmbedderPolicyRequireCorp, 1);
  CheckCounterMainFrame(WebFeature::kCrossOriginEmbedderPolicyCredentialless,
                        1);
  CheckCounterMainFrame(WebFeature::kCrossOriginEmbedderPolicyRequireCorp, 0);
}

IN_PROC_BROWSER_TEST_F(ChromeWebPlatformSecurityMetricsBrowserTest,
                       CoepNoneSharedWorker) {
  GURL main_page_url = https_server().GetURL("a.test", "/empty.html");
  GURL worker_url =
      https_server().GetURL("a.test",
                            "/set-header?"
                            "Cross-Origin-Embedder-Policy: unsafe-none");
  ASSERT_TRUE(content::NavigateToURL(web_contents(), main_page_url));
  EXPECT_TRUE(content::ExecJs(
      web_contents(),
      content::JsReplace("worker = new SharedWorker($1)", worker_url)));
  CheckCounter(WebFeature::kCoepNoneSharedWorker, 1);
  CheckCounter(WebFeature::kCoepCredentiallessSharedWorker, 0);
  CheckCounter(WebFeature::kCoepRequireCorpSharedWorker, 0);
}

IN_PROC_BROWSER_TEST_F(ChromeWebPlatformSecurityMetricsBrowserTest,
                       CoepCredentiallessSharedWorker) {
  GURL main_page_url = https_server().GetURL("a.test", "/empty.html");
  GURL worker_url =
      https_server().GetURL("a.test",
                            "/set-header?"
                            "Cross-Origin-Embedder-Policy: credentialless");
  ASSERT_TRUE(content::NavigateToURL(web_contents(), main_page_url));
  EXPECT_TRUE(content::ExecJs(
      web_contents(),
      content::JsReplace("worker = new SharedWorker($1)", worker_url)));
  CheckCounter(WebFeature::kCoepNoneSharedWorker, 0);
  CheckCounter(WebFeature::kCoepCredentiallessSharedWorker, 1);
  CheckCounter(WebFeature::kCoepRequireCorpSharedWorker, 0);
}

IN_PROC_BROWSER_TEST_F(ChromeWebPlatformSecurityMetricsBrowserTest,
                       CoepRequireCorpSharedWorker) {
  GURL main_page_url = https_server().GetURL("a.test", "/empty.html");
  GURL worker_url =
      https_server().GetURL("a.test",
                            "/set-header?"
                            "Cross-Origin-Embedder-Policy: require-corp");
  ASSERT_TRUE(content::NavigateToURL(web_contents(), main_page_url));
  EXPECT_TRUE(content::ExecJs(
      web_contents(),
      content::JsReplace("worker = new SharedWorker($1)", worker_url)));
  CheckCounter(WebFeature::kCoepNoneSharedWorker, 0);
  CheckCounter(WebFeature::kCoepCredentiallessSharedWorker, 0);
  CheckCounter(WebFeature::kCoepRequireCorpSharedWorker, 1);
}

IN_PROC_BROWSER_TEST_F(ChromeWebPlatformSecurityMetricsBrowserTest,
                       WindowProxyAccess) {
  GURL url =
      https_server().GetURL("a.com", "/cross_site_iframe_factory.html?a(a,b)");
  ASSERT_TRUE(content::NavigateToURL(web_contents(), url));
  content::RenderFrameHost* same_origin_subframe =
      ChildFrameAt(web_contents()->GetPrimaryMainFrame(), 0);
  content::RenderFrameHost* cross_origin_subframe =
      ChildFrameAt(web_contents()->GetPrimaryMainFrame(), 1);

  struct TestCase {
    const char* name;
    const char* property;
    WebFeature property_access;
    WebFeature property_access_from_other_page;
  } cases[] = {
      {
          "blur",
          "window.top.blur()",
          WebFeature::kWindowProxyCrossOriginAccessBlur,
          WebFeature::kWindowProxyCrossOriginAccessFromOtherPageBlur,
      },
      {
          "closed",
          "window.top.closed",
          WebFeature::kWindowProxyCrossOriginAccessClosed,
          WebFeature::kWindowProxyCrossOriginAccessFromOtherPageClosed,
      },
      {
          "focus",
          "window.top.focus()",
          WebFeature::kWindowProxyCrossOriginAccessFocus,
          WebFeature::kWindowProxyCrossOriginAccessFromOtherPageFocus,
      },
      {
          "frames",
          "window.top.frames",
          WebFeature::kWindowProxyCrossOriginAccessFrames,
          WebFeature::kWindowProxyCrossOriginAccessFromOtherPageFrames,
      },
      {
          "length",
          "window.top.length",
          WebFeature::kWindowProxyCrossOriginAccessLength,
          WebFeature::kWindowProxyCrossOriginAccessFromOtherPageLength,
      },
      {
          "location get",
          "window.top.location",
          WebFeature::kWindowProxyCrossOriginAccessLocation,
          WebFeature::kWindowProxyCrossOriginAccessFromOtherPageLocation,
      },
      {
          "opener get",
          "window.top.opener",
          WebFeature::kWindowProxyCrossOriginAccessOpener,
          WebFeature::kWindowProxyCrossOriginAccessFromOtherPageOpener,
      },
      {
          "parent",
          "window.top.parent",
          WebFeature::kWindowProxyCrossOriginAccessParent,
          WebFeature::kWindowProxyCrossOriginAccessFromOtherPageParent,
      },
      {
          "postMessage",
          "window.top.postMessage('','*')",
          WebFeature::kWindowProxyCrossOriginAccessPostMessage,
          WebFeature::kWindowProxyCrossOriginAccessFromOtherPagePostMessage,
      },
      {
          "self",
          "window.top.self",
          WebFeature::kWindowProxyCrossOriginAccessSelf,
          WebFeature::kWindowProxyCrossOriginAccessFromOtherPageSelf,
      },
      {
          "top",
          "window.top.top",
          WebFeature::kWindowProxyCrossOriginAccessTop,
          WebFeature::kWindowProxyCrossOriginAccessFromOtherPageTop,
      },
      {
          "window",
          "window.top.window",
          WebFeature::kWindowProxyCrossOriginAccessWindow,
          WebFeature::kWindowProxyCrossOriginAccessFromOtherPageWindow,
      }};

  for (auto test : cases) {
    SCOPED_TRACE(test.name);
    // Check that a same-origin access does not register use counters.
    EXPECT_TRUE(content::ExecJs(same_origin_subframe, test.property));
    CheckCounter(test.property_access, 0);
    CheckCounter(test.property_access_from_other_page, 0);

    // Check that a cross-origin access register use counters.
    EXPECT_TRUE(content::ExecJs(cross_origin_subframe, test.property));
    CheckCounter(test.property_access, 1);
    CheckCounter(test.property_access_from_other_page, 0);
  }
}

IN_PROC_BROWSER_TEST_F(ChromeWebPlatformSecurityMetricsBrowserTest,
                       WindowProxyAccessCloseSameOrigin) {
  GURL url =
      https_server().GetURL("a.com", "/cross_site_iframe_factory.html?a(a)");
  ASSERT_TRUE(content::NavigateToURL(web_contents(), url));

  // Check that a same-origin access does not register use counters.
  content::RenderFrameHost* same_origin_subframe =
      ChildFrameAt(web_contents()->GetPrimaryMainFrame(), 0);
  EXPECT_TRUE(content::ExecJs(same_origin_subframe, "window.top.close()"));
  CheckCounter(WebFeature::kWindowProxyCrossOriginAccessClose, 0);
  CheckCounter(WebFeature::kWindowProxyCrossOriginAccessFromOtherPageClose, 0);
}

IN_PROC_BROWSER_TEST_F(ChromeWebPlatformSecurityMetricsBrowserTest,
                       WindowProxyAccessCloseCrossOrigin) {
  GURL url =
      https_server().GetURL("a.com", "/cross_site_iframe_factory.html?a(b)");
  ASSERT_TRUE(content::NavigateToURL(web_contents(), url));

  // Check that a cross-origin access register use counters.
  content::RenderFrameHost* cross_origin_subframe =
      ChildFrameAt(web_contents()->GetPrimaryMainFrame(), 0);
  EXPECT_TRUE(content::ExecJs(cross_origin_subframe, "window.top.close()"));
  CheckCounter(WebFeature::kWindowProxyCrossOriginAccessClose, 1);
  CheckCounter(WebFeature::kWindowProxyCrossOriginAccessFromOtherPageClose, 0);
}

IN_PROC_BROWSER_TEST_F(ChromeWebPlatformSecurityMetricsBrowserTest,
                       WindowProxyAccessIndexedGetter) {
  GURL url =
      https_server().GetURL("a.com", "/cross_site_iframe_factory.html?a(a,b)");
  ASSERT_TRUE(content::NavigateToURL(web_contents(), url));

  // Check that a same-origin access does not register use counters.
  content::RenderFrameHost* same_origin_subframe =
      ChildFrameAt(web_contents()->GetPrimaryMainFrame(), 0);
  EXPECT_TRUE(content::ExecJs(same_origin_subframe, "window.top[0]"));
  CheckCounter(WebFeature::kWindowProxyCrossOriginAccessIndexedGetter, 0);
  CheckCounter(
      WebFeature::kWindowProxyCrossOriginAccessFromOtherPageIndexedGetter, 0);

  // Check that a cross-origin access register use counters.
  content::RenderFrameHost* cross_origin_subframe =
      ChildFrameAt(web_contents()->GetPrimaryMainFrame(), 1);
  EXPECT_TRUE(content::ExecJs(cross_origin_subframe, "window.top[0]"));
  CheckCounter(WebFeature::kWindowProxyCrossOriginAccessIndexedGetter, 1);
  CheckCounter(
      WebFeature::kWindowProxyCrossOriginAccessFromOtherPageIndexedGetter, 0);

  // A failed access should not register the use counter.
  EXPECT_FALSE(content::ExecJs(cross_origin_subframe, "window.top[2]"));
  CheckCounter(WebFeature::kWindowProxyCrossOriginAccessIndexedGetter, 1);
  CheckCounter(
      WebFeature::kWindowProxyCrossOriginAccessFromOtherPageIndexedGetter, 0);
}

IN_PROC_BROWSER_TEST_F(ChromeWebPlatformSecurityMetricsBrowserTest,
                       WindowProxyAccessLocationSetSameOrigin) {
  GURL url =
      https_server().GetURL("a.com", "/cross_site_iframe_factory.html?a(a,b)");
  ASSERT_TRUE(content::NavigateToURL(web_contents(), url));

  // Check that a same-origin access does not register use counters.
  content::RenderFrameHost* same_origin_subframe =
      ChildFrameAt(web_contents()->GetPrimaryMainFrame(), 0);
  EXPECT_TRUE(
      content::ExecJs(same_origin_subframe,
                      content::JsReplace("window.top.location = $1", url)));
  CheckCounter(WebFeature::kWindowProxyCrossOriginAccessLocation, 0);
  CheckCounter(WebFeature::kWindowProxyCrossOriginAccessFromOtherPageLocation,
               0);
}

IN_PROC_BROWSER_TEST_F(ChromeWebPlatformSecurityMetricsBrowserTest,
                       WindowProxyAccessLocationSetCrossOrigin) {
  GURL url =
      https_server().GetURL("a.com", "/cross_site_iframe_factory.html?a(a,b)");
  GURL fragment_url = https_server().GetURL(
      "a.com", "/cross_site_iframe_factory.html?a(a,b)#foo");
  ASSERT_TRUE(content::NavigateToURL(web_contents(), url));

  // Check that a cross-origin access register use counters.
  content::RenderFrameHost* cross_origin_subframe =
      ChildFrameAt(web_contents()->GetPrimaryMainFrame(), 1);
  EXPECT_TRUE(content::ExecJs(
      cross_origin_subframe,
      content::JsReplace("window.top.location = $1", fragment_url)));

  CheckCounter(WebFeature::kWindowProxyCrossOriginAccessLocation, 1);
  CheckCounter(WebFeature::kWindowProxyCrossOriginAccessFromOtherPageLocation,
               0);
}

IN_PROC_BROWSER_TEST_F(ChromeWebPlatformSecurityMetricsBrowserTest,
                       WindowProxyAccessNamedGetter) {
  GURL url = https_server().GetURL("a.test", "/iframe_about_blank.html");
  ASSERT_TRUE(content::NavigateToURL(web_contents(), url));
  GURL cross_origin_url = https_server().GetURL("b.test", "/empty.html");
  LoadIFrame(cross_origin_url);

  // Check that a same-origin access does not register use counters.
  content::RenderFrameHost* same_origin_subframe =
      ChildFrameAt(web_contents()->GetPrimaryMainFrame(), 0);
  EXPECT_TRUE(content::ExecJs(same_origin_subframe,
                              "window.top['about_blank_iframe']"));
  CheckCounter(WebFeature::kWindowProxyCrossOriginAccessNamedGetter, 0);
  CheckCounter(
      WebFeature::kWindowProxyCrossOriginAccessFromOtherPageNamedGetter, 0);

  // Check that a cross-origin access register use counters.
  content::RenderFrameHost* cross_origin_subframe =
      ChildFrameAt(web_contents()->GetPrimaryMainFrame(), 1);
  EXPECT_TRUE(content::ExecJs(cross_origin_subframe,
                              "window.top['about_blank_iframe']"));
  CheckCounter(WebFeature::kWindowProxyCrossOriginAccessNamedGetter, 1);
  CheckCounter(
      WebFeature::kWindowProxyCrossOriginAccessFromOtherPageNamedGetter, 0);

  // A failed access should not register the use counter.
  EXPECT_FALSE(
      content::ExecJs(cross_origin_subframe, "window.top['wrongName']"));
  CheckCounter(WebFeature::kWindowProxyCrossOriginAccessNamedGetter, 1);
  CheckCounter(
      WebFeature::kWindowProxyCrossOriginAccessFromOtherPageNamedGetter, 0);
}

IN_PROC_BROWSER_TEST_F(ChromeWebPlatformSecurityMetricsBrowserTest,
                       WindowProxyAccessOpenerSet) {
  GURL url =
      https_server().GetURL("a.com", "/cross_site_iframe_factory.html?a(a,b)");
  ASSERT_TRUE(content::NavigateToURL(web_contents(), url));

  // Check that a same-origin access does not register use counters.
  content::RenderFrameHost* same_origin_subframe =
      ChildFrameAt(web_contents()->GetPrimaryMainFrame(), 0);
  EXPECT_TRUE(content::ExecJs(same_origin_subframe, "window.top.opener = ''"));
  CheckCounter(WebFeature::kWindowProxyCrossOriginAccessOpener, 0);
  CheckCounter(WebFeature::kWindowProxyCrossOriginAccessFromOtherPageOpener, 0);

  // Check that a cross-origin access doesn't register use counters because it
  // is blocked by the same-origin policy.
  content::RenderFrameHost* cross_origin_subframe =
      ChildFrameAt(web_contents()->GetPrimaryMainFrame(), 1);
  EXPECT_FALSE(
      content::ExecJs(cross_origin_subframe, "window.top.opener = ''"));
  CheckCounter(WebFeature::kWindowProxyCrossOriginAccessOpener, 0);
  CheckCounter(WebFeature::kWindowProxyCrossOriginAccessFromOtherPageOpener, 0);
}

IN_PROC_BROWSER_TEST_F(ChromeWebPlatformSecurityMetricsBrowserTest,
                       WindowProxyAccessFromOtherPage) {
  GURL url = https_server().GetURL("a.com", "/empty.html");
  ASSERT_TRUE(content::NavigateToURL(web_contents(), url));

  content::WebContents* same_origin_popup = OpenPopup(url);

  GURL cross_origin_url = https_server().GetURL("b.test", "/empty.html");
  content::WebContents* cross_origin_popup = OpenPopup(cross_origin_url);

  struct TestCase {
    const char* name;
    const char* property;
    WebFeature property_access;
    WebFeature property_access_from_other_page;
  } cases[] = {
      {
          "blur",
          "window.opener.blur()",
          WebFeature::kWindowProxyCrossOriginAccessBlur,
          WebFeature::kWindowProxyCrossOriginAccessFromOtherPageBlur,
      },
      {
          "closed",
          "window.opener.closed",
          WebFeature::kWindowProxyCrossOriginAccessClosed,
          WebFeature::kWindowProxyCrossOriginAccessFromOtherPageClosed,
      },
      {
          "focus",
          "window.opener.focus()",
          WebFeature::kWindowProxyCrossOriginAccessFocus,
          WebFeature::kWindowProxyCrossOriginAccessFromOtherPageFocus,
      },
      {
          "frames",
          "window.opener.frames",
          WebFeature::kWindowProxyCrossOriginAccessFrames,
          WebFeature::kWindowProxyCrossOriginAccessFromOtherPageFrames,
      },
      {
          "length",
          "window.opener.length",
          WebFeature::kWindowProxyCrossOriginAccessLength,
          WebFeature::kWindowProxyCrossOriginAccessFromOtherPageLength,
      },
      {
          "location get",
          "window.opener.location",
          WebFeature::kWindowProxyCrossOriginAccessLocation,
          WebFeature::kWindowProxyCrossOriginAccessFromOtherPageLocation,
      },
      {
          "opener get",
          "window.opener.opener",
          WebFeature::kWindowProxyCrossOriginAccessOpener,
          WebFeature::kWindowProxyCrossOriginAccessFromOtherPageOpener,
      },
      {
          "parent",
          "window.opener.parent",
          WebFeature::kWindowProxyCrossOriginAccessParent,
          WebFeature::kWindowProxyCrossOriginAccessFromOtherPageParent,
      },
      {
          "postMessage",
          "window.opener.postMessage('','*')",
          WebFeature::kWindowProxyCrossOriginAccessPostMessage,
          WebFeature::kWindowProxyCrossOriginAccessFromOtherPagePostMessage,
      },
      {
          "self",
          "window.opener.self",
          WebFeature::kWindowProxyCrossOriginAccessSelf,
          WebFeature::kWindowProxyCrossOriginAccessFromOtherPageSelf,
      },
      {
          "top",
          "window.opener.top",
          WebFeature::kWindowProxyCrossOriginAccessTop,
          WebFeature::kWindowProxyCrossOriginAccessFromOtherPageTop,
      }};

  for (auto test : cases) {
    SCOPED_TRACE(test.name);
    // Check that a same-origin access does not register use counters.
    EXPECT_TRUE(content::ExecJs(same_origin_popup, test.property));
    CheckCounter(test.property_access, 0);
    CheckCounter(test.property_access_from_other_page, 0);

    // Check that a cross-origin access register use counters.
    EXPECT_TRUE(content::ExecJs(cross_origin_popup, test.property));
    CheckCounter(test.property_access, 1);
    CheckCounter(test.property_access_from_other_page, 1);
  }
}

IN_PROC_BROWSER_TEST_F(ChromeWebPlatformSecurityMetricsBrowserTest,
                       WindowProxyAccessFromOtherPageCloseSameOrigin) {
  GURL url = https_server().GetURL("a.test", "/empty.html");
  ASSERT_TRUE(content::NavigateToURL(web_contents(), url));

  // Check that a same-origin access does not register use counters.
  content::WebContents* same_origin_popup = OpenPopup(url);
  EXPECT_TRUE(content::ExecJs(same_origin_popup, "window.opener.close()"));
  CheckCounter(WebFeature::kWindowProxyCrossOriginAccessClose, 0);
  CheckCounter(WebFeature::kWindowProxyCrossOriginAccessFromOtherPageClose, 0);
}

IN_PROC_BROWSER_TEST_F(ChromeWebPlatformSecurityMetricsBrowserTest,
                       WindowProxyAccessFromOtherPageCloseCrossOrigin) {
  GURL url = https_server().GetURL("a.test", "/empty.html");
  ASSERT_TRUE(content::NavigateToURL(web_contents(), url));

  // Check that a cross-origin access register use counters.
  GURL cross_origin_url = https_server().GetURL("b.test", "/empty.html");
  content::WebContents* cross_origin_popup = OpenPopup(cross_origin_url);
  EXPECT_TRUE(content::ExecJs(cross_origin_popup, "window.opener.close()"));
  CheckCounter(WebFeature::kWindowProxyCrossOriginAccessClose, 1);
  CheckCounter(WebFeature::kWindowProxyCrossOriginAccessFromOtherPageClose, 1);
}

IN_PROC_BROWSER_TEST_F(ChromeWebPlatformSecurityMetricsBrowserTest,
                       WindowProxyAccessFromOtherPageIndexedGetter) {
  GURL url = https_server().GetURL("a.test", "/iframe.html");
  ASSERT_TRUE(content::NavigateToURL(web_contents(), url));

  // Check that a same-origin access does not register use counters.
  content::WebContents* same_origin_popup = OpenPopup(url);
  EXPECT_TRUE(content::ExecJs(same_origin_popup, "window.opener[0]"));
  CheckCounter(WebFeature::kWindowProxyCrossOriginAccessIndexedGetter, 0);
  CheckCounter(
      WebFeature::kWindowProxyCrossOriginAccessFromOtherPageIndexedGetter, 0);

  // Check that a cross-origin access register use counters.
  GURL cross_origin_url = https_server().GetURL("b.test", "/empty.html");
  content::WebContents* cross_origin_popup = OpenPopup(cross_origin_url);
  EXPECT_TRUE(content::ExecJs(cross_origin_popup, "window.opener[0]"));
  CheckCounter(WebFeature::kWindowProxyCrossOriginAccessIndexedGetter, 1);
  CheckCounter(
      WebFeature::kWindowProxyCrossOriginAccessFromOtherPageIndexedGetter, 1);

  // A failed access should not register the use counter.
  EXPECT_FALSE(content::ExecJs(cross_origin_popup, "window.opener[1]"));
  CheckCounter(WebFeature::kWindowProxyCrossOriginAccessIndexedGetter, 1);
  CheckCounter(
      WebFeature::kWindowProxyCrossOriginAccessFromOtherPageIndexedGetter, 1);
}

IN_PROC_BROWSER_TEST_F(ChromeWebPlatformSecurityMetricsBrowserTest,
                       WindowProxyAccessFromOtherPageLocationSetSameOrigin) {
  GURL url = https_server().GetURL("a.test", "/empty.html");
  ASSERT_TRUE(content::NavigateToURL(web_contents(), url));

  // Check that a same-origin access does not register use counters.
  content::WebContents* same_origin_popup = OpenPopup(url);
  EXPECT_TRUE(
      content::ExecJs(same_origin_popup,
                      content::JsReplace("window.opener.location = $1", url)));
  CheckCounter(WebFeature::kWindowProxyCrossOriginAccessLocation, 0);
  CheckCounter(WebFeature::kWindowProxyCrossOriginAccessFromOtherPageLocation,
               0);
}

IN_PROC_BROWSER_TEST_F(ChromeWebPlatformSecurityMetricsBrowserTest,
                       WindowProxyAccessFromOtherPageLocationSetCrossOrigin) {
  GURL url = https_server().GetURL("a.test", "/empty.html");
  ASSERT_TRUE(content::NavigateToURL(web_contents(), url));

  // Check that a cross-origin access register use counters.
  GURL cross_origin_url = https_server().GetURL("b.test", "/empty.html");
  content::WebContents* cross_origin_popup = OpenPopup(cross_origin_url);
  EXPECT_TRUE(
      content::ExecJs(cross_origin_popup,
                      content::JsReplace("window.opener.location = $1", url)));
  CheckCounter(WebFeature::kWindowProxyCrossOriginAccessLocation, 1);
  CheckCounter(WebFeature::kWindowProxyCrossOriginAccessFromOtherPageLocation,
               1);
}

IN_PROC_BROWSER_TEST_F(ChromeWebPlatformSecurityMetricsBrowserTest,
                       WindowProxyAccessFromOtherPageNamedGetter) {
  GURL url = https_server().GetURL("a.test", "/iframe_about_blank.html");
  GURL cross_origin_url = https_server().GetURL("b.test", "/empty.html");
  ASSERT_TRUE(content::NavigateToURL(web_contents(), url));

  // Check that a same-origin access does not register use counters.
  content::WebContents* same_origin_popup = OpenPopup(url);
  EXPECT_TRUE(content::ExecJs(same_origin_popup,
                              "window.opener['about_blank_iframe']"));
  CheckCounter(WebFeature::kWindowProxyCrossOriginAccessNamedGetter, 0);
  CheckCounter(
      WebFeature::kWindowProxyCrossOriginAccessFromOtherPageNamedGetter, 0);

  // Check that a cross-origin access register use counters.
  content::WebContents* cross_origin_popup = OpenPopup(cross_origin_url);
  EXPECT_TRUE(content::ExecJs(cross_origin_popup,
                              "window.opener['about_blank_iframe']"));
  CheckCounter(WebFeature::kWindowProxyCrossOriginAccessNamedGetter, 1);
  CheckCounter(
      WebFeature::kWindowProxyCrossOriginAccessFromOtherPageNamedGetter, 1);

  // A failed access should not register the use counter.
  EXPECT_FALSE(
      content::ExecJs(cross_origin_popup, "window.opener['wrongName']"));
  CheckCounter(WebFeature::kWindowProxyCrossOriginAccessNamedGetter, 1);
  CheckCounter(
      WebFeature::kWindowProxyCrossOriginAccessFromOtherPageNamedGetter, 1);
}

IN_PROC_BROWSER_TEST_F(ChromeWebPlatformSecurityMetricsBrowserTest,
                       WindowProxyAccessFromOtherPageOpenerSet) {
  GURL url = https_server().GetURL("a.test", "/empty.html");
  ASSERT_TRUE(content::NavigateToURL(web_contents(), url));

  // Check that a same-origin access does not register use counters.
  content::WebContents* same_origin_popup = OpenPopup(url);
  EXPECT_TRUE(content::ExecJs(same_origin_popup, "window.opener.opener = ''"));
  CheckCounter(WebFeature::kWindowProxyCrossOriginAccessOpener, 0);
  CheckCounter(WebFeature::kWindowProxyCrossOriginAccessFromOtherPageOpener, 0);

  // Check that a cross-origin access doesn't register use counters because it
  // is blocked by the same-origin policy.
  GURL cross_origin_url = https_server().GetURL("b.test", "/empty.html");
  content::WebContents* cross_origin_popup = OpenPopup(cross_origin_url);
  EXPECT_FALSE(
      content::ExecJs(cross_origin_popup, "window.opener.opener = ''"));
  CheckCounter(WebFeature::kWindowProxyCrossOriginAccessOpener, 0);
  CheckCounter(WebFeature::kWindowProxyCrossOriginAccessFromOtherPageOpener, 0);
}

IN_PROC_BROWSER_TEST_F(ChromeWebPlatformSecurityMetricsBrowserTest,
                       WindowProxyAccessFromOtherPageWindow) {
  GURL url = https_server().GetURL("a.test", "/empty.html");
  ASSERT_TRUE(content::NavigateToURL(web_contents(), url));

  // Check that a same-origin access does not register use counters.
  content::WebContents* same_origin_popup = OpenPopup(url);
  EXPECT_TRUE(content::ExecJs(same_origin_popup, "window.opener.window"));
  CheckCounter(WebFeature::kWindowProxyCrossOriginAccessWindow, 0);
  CheckCounter(WebFeature::kWindowProxyCrossOriginAccessFromOtherPageWindow, 0);

  // Check that a cross-origin access register use counters.
  GURL cross_origin_url = https_server().GetURL("b.test", "/empty.html");
  content::WebContents* cross_origin_popup = OpenPopup(cross_origin_url);
  EXPECT_TRUE(content::ExecJs(cross_origin_popup, "window.opener.window"));
  CheckCounter(WebFeature::kWindowProxyCrossOriginAccessWindow, 1);
  CheckCounter(WebFeature::kWindowProxyCrossOriginAccessFromOtherPageWindow, 1);
}

IN_PROC_BROWSER_TEST_F(ChromeWebPlatformSecurityMetricsBrowserTest,
                       AnonymousIframeInitialEmptyDocumentControl) {
  GURL url = https_server().GetURL("a.test", "/empty.html");
  EXPECT_TRUE(content::NavigateToURL(web_contents(), url));
  EXPECT_TRUE(content::ExecJs(web_contents(), R"(
    const iframe = document.createElement("iframe");
    iframe.anonymous = false;
    document.body.appendChild(iframe);
  )"));
  CheckCounter(WebFeature::kAnonymousIframe, 0);
}

IN_PROC_BROWSER_TEST_F(ChromeWebPlatformSecurityMetricsBrowserTest,
                       AnonymousIframeInitialEmptyDocument) {
  GURL url = https_server().GetURL("a.test", "/empty.html");
  EXPECT_TRUE(content::NavigateToURL(web_contents(), url));
  EXPECT_TRUE(content::ExecJs(web_contents(), R"(
    const iframe = document.createElement("iframe");
    iframe.anonymous = true;
    document.body.appendChild(iframe);
  )"));
  CheckCounter(WebFeature::kAnonymousIframe, 1);
}

IN_PROC_BROWSER_TEST_F(ChromeWebPlatformSecurityMetricsBrowserTest,
                       AnonymousIframeNavigationControl) {
  GURL url = https_server().GetURL("a.test", "/empty.html");
  EXPECT_TRUE(content::NavigateToURL(web_contents(), url));
  EXPECT_TRUE(content::ExecJs(web_contents(), R"(
    new Promise(resolve => {
      let iframe = document.createElement("iframe");
      iframe.src = location.href;
      iframe.anonymous = false;
      iframe.onload = resolve;
      document.body.appendChild(iframe);
    });
  )"));
  CheckCounter(WebFeature::kAnonymousIframe, 0);
}

IN_PROC_BROWSER_TEST_F(ChromeWebPlatformSecurityMetricsBrowserTest,
                       AnonymousIframeNavigation) {
  GURL url = https_server().GetURL("a.test", "/empty.html");
  EXPECT_TRUE(content::NavigateToURL(web_contents(), url));
  EXPECT_TRUE(content::ExecJs(web_contents(), R"(
    new Promise(resolve => {
      let iframe = document.createElement("iframe");
      iframe.src = location.href;
      iframe.anonymous = true;
      iframe.onload = resolve;
      document.body.appendChild(iframe);
    });
  )"));
  CheckCounter(WebFeature::kAnonymousIframe, 1);
}

// TODO(arthursonzogni): Add basic test(s) for the WebFeatures:
// [ ] CrossOriginOpenerPolicySameOrigin
// [ ] CrossOriginOpenerPolicySameOriginAllowPopups
// [X] CoopAndCoepIsolated
//
// Added by:
// https://chromium-review.googlesource.com/c/chromium/src/+/2122140
