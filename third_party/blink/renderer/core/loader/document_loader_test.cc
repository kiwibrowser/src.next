// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/loader/document_loader.h"

#include <utility>

#include "base/auto_reset.h"
#include "base/test/scoped_feature_list.h"
#include "base/unguessable_token.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/blink/public/common/features.h"
#include "third_party/blink/public/platform/platform.h"
#include "third_party/blink/public/platform/web_string.h"
#include "third_party/blink/public/platform/web_url_loader_client.h"
#include "third_party/blink/public/platform/web_url_loader_mock_factory.h"
#include "third_party/blink/renderer/core/frame/frame_test_helpers.h"
#include "third_party/blink/renderer/core/frame/local_dom_window.h"
#include "third_party/blink/renderer/core/frame/web_local_frame_impl.h"
#include "third_party/blink/renderer/core/inspector/console_message.h"
#include "third_party/blink/renderer/core/page/page.h"
#include "third_party/blink/renderer/core/testing/scoped_fake_plugin_registry.h"
#include "third_party/blink/renderer/core/testing/sim/sim_request.h"
#include "third_party/blink/renderer/core/testing/sim/sim_test.h"
#include "third_party/blink/renderer/platform/loader/static_data_navigation_body_loader.h"
#include "third_party/blink/renderer/platform/storage/blink_storage_key.h"
#include "third_party/blink/renderer/platform/testing/histogram_tester.h"
#include "third_party/blink/renderer/platform/testing/unit_test_helpers.h"
#include "third_party/blink/renderer/platform/testing/url_test_helpers.h"
#include "third_party/blink/renderer/platform/wtf/deque.h"

namespace blink {

class DocumentLoaderTest : public testing::Test {
 protected:
  void SetUp() override {
    web_view_helper_.Initialize();
    url_test_helpers::RegisterMockedURLLoad(
        url_test_helpers::ToKURL("http://example.com/foo.html"),
        test::CoreTestDataPath("foo.html"));
    url_test_helpers::RegisterMockedURLLoad(
        url_test_helpers::ToKURL("https://example.com/foo.html"),
        test::CoreTestDataPath("foo.html"));
    url_test_helpers::RegisterMockedURLLoad(
        url_test_helpers::ToKURL("https://example.com:8000/foo.html"),
        test::CoreTestDataPath("foo.html"));
    url_test_helpers::RegisterMockedURLLoad(
        url_test_helpers::ToKURL("http://192.168.1.1/foo.html"),
        test::CoreTestDataPath("foo.html"), WebString::FromUTF8("text/html"),
        WebURLLoaderMockFactory::GetSingletonInstance(),
        network::mojom::IPAddressSpace::kPrivate);
    url_test_helpers::RegisterMockedURLLoad(
        url_test_helpers::ToKURL("https://192.168.1.1/foo.html"),
        test::CoreTestDataPath("foo.html"), WebString::FromUTF8("text/html"),
        WebURLLoaderMockFactory::GetSingletonInstance(),
        network::mojom::IPAddressSpace::kPrivate);
    url_test_helpers::RegisterMockedURLLoad(
        url_test_helpers::ToKURL("http://somethinglocal/foo.html"),
        test::CoreTestDataPath("foo.html"), WebString::FromUTF8("text/html"),
        WebURLLoaderMockFactory::GetSingletonInstance(),
        network::mojom::IPAddressSpace::kLocal);
  }

  void TearDown() override {
    url_test_helpers::UnregisterAllURLsAndClearMemoryCache();
  }

  class ScopedLoaderDelegate {
   public:
    ScopedLoaderDelegate(WebURLLoaderTestDelegate* delegate) {
      url_test_helpers::SetLoaderDelegate(delegate);
    }
    ~ScopedLoaderDelegate() { url_test_helpers::SetLoaderDelegate(nullptr); }
  };

  WebLocalFrameImpl* MainFrame() { return web_view_helper_.LocalMainFrame(); }

  frame_test_helpers::WebViewHelper web_view_helper_;
};

TEST_F(DocumentLoaderTest, SingleChunk) {
  class TestDelegate : public WebURLLoaderTestDelegate {
   public:
    void DidReceiveData(WebURLLoaderClient* original_client,
                        const char* data,
                        int data_length) override {
      EXPECT_EQ(34, data_length) << "foo.html was not served in a single chunk";
      original_client->DidReceiveData(data, data_length);
    }
  } delegate;

  ScopedLoaderDelegate loader_delegate(&delegate);
  frame_test_helpers::LoadFrame(MainFrame(), "https://example.com/foo.html");

  // TODO(dcheng): How should the test verify that the original callback is
  // invoked? The test currently still passes even if the test delegate
  // forgets to invoke the callback.
}

// Test normal case of DocumentLoader::dataReceived(): data in multiple chunks,
// with no reentrancy.
TEST_F(DocumentLoaderTest, MultiChunkNoReentrancy) {
  class TestDelegate : public WebURLLoaderTestDelegate {
   public:
    void DidReceiveData(WebURLLoaderClient* original_client,
                        const char* data,
                        int data_length) override {
      EXPECT_EQ(34, data_length) << "foo.html was not served in a single chunk";
      // Chunk the reply into one byte chunks.
      for (int i = 0; i < data_length; ++i)
        original_client->DidReceiveData(&data[i], 1);
    }
  } delegate;

  ScopedLoaderDelegate loader_delegate(&delegate);
  frame_test_helpers::LoadFrame(MainFrame(), "https://example.com/foo.html");
}

// Finally, test reentrant callbacks to DocumentLoader::BodyDataReceived().
TEST_F(DocumentLoaderTest, MultiChunkWithReentrancy) {
  // This test delegate chunks the response stage into three distinct stages:
  // 1. The first BodyDataReceived() callback, which triggers frame detach
  //    due to committing a provisional load.
  // 2. The middle part of the response, which is dispatched to
  //    BodyDataReceived() reentrantly.
  // 3. The final chunk, which is dispatched normally at the top-level.
  class MainFrameClient : public WebURLLoaderTestDelegate,
                          public frame_test_helpers::TestWebFrameClient {
   public:
    // WebURLLoaderTestDelegate overrides:
    bool FillNavigationParamsResponse(WebNavigationParams* params) override {
      params->response = WebURLResponse(params->url);
      params->response.SetMimeType("application/x-webkit-test-webplugin");
      params->response.SetHttpStatusCode(200);

      String data("<html><body>foo</body></html>");
      for (wtf_size_t i = 0; i < data.length(); i++)
        data_.push_back(data[i]);

      auto body_loader = std::make_unique<StaticDataNavigationBodyLoader>();
      body_loader_ = body_loader.get();
      params->body_loader = std::move(body_loader);
      return true;
    }

    void Serve() {
      {
        // Serve the first byte to the real WebURLLoaderCLient, which
        // should trigger frameDetach() due to committing a provisional
        // load.
        base::AutoReset<bool> dispatching(&dispatching_did_receive_data_, true);
        DispatchOneByte();
      }

      // Serve the remaining bytes to complete the load.
      EXPECT_FALSE(data_.IsEmpty());
      while (!data_.IsEmpty())
        DispatchOneByte();

      body_loader_->Finish();
      body_loader_ = nullptr;
    }

    // WebLocalFrameClient overrides:
    void RunScriptsAtDocumentElementAvailable() override {
      if (dispatching_did_receive_data_) {
        // This should be called by the first BodyDataReceived() call, since
        // it should create a plugin document structure and trigger this.
        EXPECT_GT(data_.size(), 10u);
        // Dispatch BodyDataReceived() callbacks for part of the remaining
        // data, saving the rest to be dispatched at the top-level as
        // normal.
        while (data_.size() > 10)
          DispatchOneByte();
        served_reentrantly_ = true;
      }
      TestWebFrameClient::RunScriptsAtDocumentElementAvailable();
    }

    void DispatchOneByte() {
      char c = data_.TakeFirst();
      body_loader_->Write(&c, 1);
    }

    bool ServedReentrantly() const { return served_reentrantly_; }

   private:
    Deque<char> data_;
    bool dispatching_did_receive_data_ = false;
    bool served_reentrantly_ = false;
    StaticDataNavigationBodyLoader* body_loader_ = nullptr;
  };

  // We use a plugin document triggered by "application/x-webkit-test-webplugin"
  // mime type, because that gives us reliable way to get a WebLocalFrameClient
  // callback from inside BodyDataReceived() call.
  ScopedFakePluginRegistry fake_plugins;
  MainFrameClient main_frame_client;
  web_view_helper_.Initialize(&main_frame_client);
  web_view_helper_.GetWebView()->GetPage()->GetSettings().SetPluginsEnabled(
      true);

  {
    ScopedLoaderDelegate loader_delegate(&main_frame_client);
    frame_test_helpers::LoadFrameDontWait(
        MainFrame(), url_test_helpers::ToKURL("https://example.com/foo.html"));
    main_frame_client.Serve();
    frame_test_helpers::PumpPendingRequestsForFrameToLoad(MainFrame());
  }

  // Sanity check that we did actually test reeentrancy.
  EXPECT_TRUE(main_frame_client.ServedReentrantly());

  // MainFrameClient is stack-allocated, so manually Reset to avoid UAF.
  web_view_helper_.Reset();
}

TEST_F(DocumentLoaderTest, isCommittedButEmpty) {
  WebViewImpl* web_view_impl =
      web_view_helper_.InitializeAndLoad("about:blank");
  EXPECT_TRUE(To<LocalFrame>(web_view_impl->GetPage()->MainFrame())
                  ->Loader()
                  .GetDocumentLoader()
                  ->IsCommittedButEmpty());
}

class DocumentLoaderSimTest : public SimTest {};

TEST_F(DocumentLoaderSimTest, DocumentOpenUpdatesUrl) {
  SimRequest main_resource("https://example.com", "text/html");
  LoadURL("https://example.com");
  main_resource.Write("<iframe src='javascript:42;'></iframe>");

  auto* child_frame = To<WebLocalFrameImpl>(MainFrame().FirstChild());
  auto* child_document = child_frame->GetFrame()->GetDocument();
  EXPECT_TRUE(child_document->HasPendingJavaScriptUrlsForTest());

  main_resource.Write(
      "<script>"
      "window[0].document.open();"
      "window[0].document.write('hello');"
      "window[0].document.close();"
      "</script>");

  main_resource.Finish();

  // document.open() should have cancelled the pending JavaScript URLs.
  EXPECT_FALSE(child_document->HasPendingJavaScriptUrlsForTest());

  // Per https://whatwg.org/C/dynamic-markup-insertion.html#document-open-steps,
  // the URL associated with the Document should match the URL of the entry
  // Document.
  EXPECT_EQ(KURL("https://example.com"), child_document->Url());
  // Similarly, the URL of the DocumentLoader should also match.
  EXPECT_EQ(KURL("https://example.com"), child_document->Loader()->Url());
}

TEST_F(DocumentLoaderSimTest, FramePolicyIntegrityOnNavigationCommit) {
  SimRequest main_resource("https://example.com", "text/html");
  SimRequest iframe_resource("https://example.com/foo.html", "text/html");
  LoadURL("https://example.com");

  main_resource.Write(R"(
    <iframe id='frame1'></iframe>
    <script>
      const iframe = document.getElementById('frame1');
      iframe.src = 'https://example.com/foo.html'; // navigation triggered
      iframe.allow = "payment 'none'"; // should not take effect until the
                                       // next navigation on iframe
    </script>
  )");

  main_resource.Finish();
  iframe_resource.Finish();

  auto* child_frame = To<WebLocalFrameImpl>(MainFrame().FirstChild());
  auto* child_window = child_frame->GetFrame()->DomWindow();

  EXPECT_TRUE(child_window->IsFeatureEnabled(
      blink::mojom::blink::PermissionsPolicyFeature::kPayment));
}

TEST_F(DocumentLoaderTest, CommitsDeferredOnSameOriginNavigation) {
  const KURL& requestor_url =
      KURL(NullURL(), "https://www.example.com/foo.html");
  WebViewImpl* web_view_impl =
      web_view_helper_.InitializeAndLoad("https://example.com/foo.html");

  const KURL& same_origin_url =
      KURL(NullURL(), "https://www.example.com/bar.html");
  std::unique_ptr<WebNavigationParams> params =
      WebNavigationParams::CreateWithHTMLBufferForTesting(
          SharedBuffer::Create(), same_origin_url);
  params->requestor_origin = WebSecurityOrigin::Create(WebURL(requestor_url));
  LocalFrame* local_frame =
      To<LocalFrame>(web_view_impl->GetPage()->MainFrame());
  local_frame->Loader().CommitNavigation(std::move(params), nullptr);

  EXPECT_TRUE(local_frame->GetDocument()->DeferredCompositorCommitIsAllowed());
}

TEST_F(DocumentLoaderTest,
       CommitsNotDeferredOnDifferentOriginNavigationWithCrossOriginDisabled) {
  base::test::ScopedFeatureList scoped_feature_list;
  scoped_feature_list.InitAndDisableFeature(features::kPaintHoldingCrossOrigin);

  const KURL& requestor_url =
      KURL(NullURL(), "https://www.example.com/foo.html");
  WebViewImpl* web_view_impl =
      web_view_helper_.InitializeAndLoad("https://example.com/foo.html");

  const KURL& other_origin_url =
      KURL(NullURL(), "https://www.another.com/bar.html");
  std::unique_ptr<WebNavigationParams> params =
      WebNavigationParams::CreateWithHTMLBufferForTesting(
          SharedBuffer::Create(), other_origin_url);
  params->requestor_origin = WebSecurityOrigin::Create(WebURL(requestor_url));
  LocalFrame* local_frame =
      To<LocalFrame>(web_view_impl->GetPage()->MainFrame());
  local_frame->Loader().CommitNavigation(std::move(params), nullptr);

  EXPECT_FALSE(local_frame->GetDocument()->DeferredCompositorCommitIsAllowed());
}

TEST_F(DocumentLoaderTest,
       CommitsDeferredOnDifferentOriginNavigationWithCrossOriginEnabled) {
  base::test::ScopedFeatureList scoped_feature_list;
  scoped_feature_list.InitAndEnableFeature(features::kPaintHoldingCrossOrigin);

  const KURL& requestor_url =
      KURL(NullURL(), "https://www.example.com/foo.html");
  WebViewImpl* web_view_impl =
      web_view_helper_.InitializeAndLoad("https://example.com/foo.html");

  const KURL& other_origin_url =
      KURL(NullURL(), "https://www.another.com/bar.html");
  std::unique_ptr<WebNavigationParams> params =
      WebNavigationParams::CreateWithHTMLBufferForTesting(
          SharedBuffer::Create(), other_origin_url);
  params->requestor_origin = WebSecurityOrigin::Create(WebURL(requestor_url));
  LocalFrame* local_frame =
      To<LocalFrame>(web_view_impl->GetPage()->MainFrame());
  local_frame->Loader().CommitNavigation(std::move(params), nullptr);

  EXPECT_TRUE(local_frame->GetDocument()->DeferredCompositorCommitIsAllowed());
}

TEST_F(DocumentLoaderTest,
       CommitsNotDeferredOnDifferentPortNavigationWithCrossOriginDisabled) {
  base::test::ScopedFeatureList scoped_feature_list;
  scoped_feature_list.InitAndDisableFeature(features::kPaintHoldingCrossOrigin);

  const KURL& requestor_url =
      KURL(NullURL(), "https://www.example.com:8000/foo.html");
  WebViewImpl* web_view_impl =
      web_view_helper_.InitializeAndLoad("https://example.com:8000/foo.html");

  const KURL& different_port_url =
      KURL(NullURL(), "https://www.example.com:8080/bar.html");
  std::unique_ptr<WebNavigationParams> params =
      WebNavigationParams::CreateWithHTMLBufferForTesting(
          SharedBuffer::Create(), different_port_url);
  params->requestor_origin = WebSecurityOrigin::Create(WebURL(requestor_url));
  LocalFrame* local_frame =
      To<LocalFrame>(web_view_impl->GetPage()->MainFrame());
  local_frame->Loader().CommitNavigation(std::move(params), nullptr);

  EXPECT_FALSE(local_frame->GetDocument()->DeferredCompositorCommitIsAllowed());
}

TEST_F(DocumentLoaderTest,
       CommitsDeferredOnDifferentPortNavigationWithCrossOriginEnabled) {
  base::test::ScopedFeatureList scoped_feature_list;
  scoped_feature_list.InitAndEnableFeature(features::kPaintHoldingCrossOrigin);

  const KURL& requestor_url =
      KURL(NullURL(), "https://www.example.com:8000/foo.html");
  WebViewImpl* web_view_impl =
      web_view_helper_.InitializeAndLoad("https://example.com:8000/foo.html");

  const KURL& different_port_url =
      KURL(NullURL(), "https://www.example.com:8080/bar.html");
  std::unique_ptr<WebNavigationParams> params =
      WebNavigationParams::CreateWithHTMLBufferForTesting(
          SharedBuffer::Create(), different_port_url);
  params->requestor_origin = WebSecurityOrigin::Create(WebURL(requestor_url));
  LocalFrame* local_frame =
      To<LocalFrame>(web_view_impl->GetPage()->MainFrame());
  local_frame->Loader().CommitNavigation(std::move(params), nullptr);

  EXPECT_TRUE(local_frame->GetDocument()->DeferredCompositorCommitIsAllowed());
}

TEST_F(DocumentLoaderTest, CommitsNotDeferredOnDataURLNavigation) {
  const KURL& requestor_url =
      KURL(NullURL(), "https://www.example.com/foo.html");
  WebViewImpl* web_view_impl =
      web_view_helper_.InitializeAndLoad("https://example.com/foo.html");

  const KURL& data_url = KURL(NullURL(), "data:,Hello%2C%20World!");
  std::unique_ptr<WebNavigationParams> params =
      WebNavigationParams::CreateWithHTMLBufferForTesting(
          SharedBuffer::Create(), data_url);
  params->requestor_origin = WebSecurityOrigin::Create(WebURL(requestor_url));
  LocalFrame* local_frame =
      To<LocalFrame>(web_view_impl->GetPage()->MainFrame());
  local_frame->Loader().CommitNavigation(std::move(params), nullptr);

  EXPECT_FALSE(local_frame->GetDocument()->DeferredCompositorCommitIsAllowed());
}

TEST_F(DocumentLoaderTest,
       CommitsNotDeferredOnDataURLNavigationWithCrossOriginEnabled) {
  base::test::ScopedFeatureList scoped_feature_list;
  scoped_feature_list.InitAndEnableFeature(features::kPaintHoldingCrossOrigin);

  const KURL& requestor_url =
      KURL(NullURL(), "https://www.example.com/foo.html");
  WebViewImpl* web_view_impl =
      web_view_helper_.InitializeAndLoad("https://example.com/foo.html");

  const KURL& data_url = KURL(NullURL(), "data:,Hello%2C%20World!");
  std::unique_ptr<WebNavigationParams> params =
      WebNavigationParams::CreateWithHTMLBufferForTesting(
          SharedBuffer::Create(), data_url);
  params->requestor_origin = WebSecurityOrigin::Create(WebURL(requestor_url));
  LocalFrame* local_frame =
      To<LocalFrame>(web_view_impl->GetPage()->MainFrame());
  local_frame->Loader().CommitNavigation(std::move(params), nullptr);

  EXPECT_FALSE(local_frame->GetDocument()->DeferredCompositorCommitIsAllowed());
}

TEST_F(DocumentLoaderTest, NavigationToAboutBlank) {
  const KURL& requestor_url =
      KURL(NullURL(), "https://subdomain.example.com/foo.html");
  WebViewImpl* web_view_impl =
      web_view_helper_.InitializeAndLoad("https://example.com/foo.html");

  const KURL& about_blank_url = KURL(NullURL(), "about:blank");
  std::unique_ptr<WebNavigationParams> params =
      std::make_unique<WebNavigationParams>();
  params->url = about_blank_url;
  params->requestor_origin = WebSecurityOrigin::Create(WebURL(requestor_url));
  LocalFrame* local_frame =
      To<LocalFrame>(web_view_impl->GetPage()->MainFrame());
  local_frame->Loader().CommitNavigation(std::move(params), nullptr);

  EXPECT_EQ(BlinkStorageKey(SecurityOrigin::Create(requestor_url)),
            local_frame->DomWindow()->GetStorageKey());
}

TEST_F(DocumentLoaderTest, SameOriginNavigation) {
  const KURL& requestor_url =
      KURL(NullURL(), "https://www.example.com/foo.html");
  WebViewImpl* web_view_impl =
      web_view_helper_.InitializeAndLoad("https://example.com/foo.html");

  const KURL& same_origin_url =
      KURL(NullURL(), "https://www.example.com/bar.html");
  std::unique_ptr<WebNavigationParams> params =
      WebNavigationParams::CreateWithHTMLBufferForTesting(
          SharedBuffer::Create(), same_origin_url);
  params->requestor_origin = WebSecurityOrigin::Create(WebURL(requestor_url));
  LocalFrame* local_frame =
      To<LocalFrame>(web_view_impl->GetPage()->MainFrame());
  local_frame->Loader().CommitNavigation(std::move(params), nullptr);

  EXPECT_EQ(BlinkStorageKey(SecurityOrigin::Create(same_origin_url)),
            local_frame->DomWindow()->GetStorageKey());
  EXPECT_TRUE(local_frame->Loader()
                  .GetDocumentLoader()
                  ->LastNavigationHadTrustedInitiator());
}

TEST_F(DocumentLoaderTest, CrossOriginNavigation) {
  const KURL& requestor_url =
      KURL(NullURL(), "https://www.example.com/foo.html");
  WebViewImpl* web_view_impl =
      web_view_helper_.InitializeAndLoad("https://example.com/foo.html");

  const KURL& other_origin_url =
      KURL(NullURL(), "https://www.another.com/bar.html");
  std::unique_ptr<WebNavigationParams> params =
      WebNavigationParams::CreateWithHTMLBufferForTesting(
          SharedBuffer::Create(), other_origin_url);
  params->requestor_origin = WebSecurityOrigin::Create(WebURL(requestor_url));
  LocalFrame* local_frame =
      To<LocalFrame>(web_view_impl->GetPage()->MainFrame());
  local_frame->Loader().CommitNavigation(std::move(params), nullptr);

  EXPECT_EQ(BlinkStorageKey(SecurityOrigin::Create(other_origin_url)),
            local_frame->DomWindow()->GetStorageKey());
  EXPECT_FALSE(local_frame->Loader()
                   .GetDocumentLoader()
                   ->LastNavigationHadTrustedInitiator());
}

TEST_F(DocumentLoaderTest, StorageKeyFromNavigationParams) {
  const KURL& requestor_url =
      KURL(NullURL(), "https://www.example.com/foo.html");
  WebViewImpl* web_view_impl =
      web_view_helper_.InitializeAndLoad("https://example.com/foo.html");

  const KURL& other_origin_url =
      KURL(NullURL(), "https://www.another.com/bar.html");
  std::unique_ptr<WebNavigationParams> params =
      WebNavigationParams::CreateWithHTMLBufferForTesting(
          SharedBuffer::Create(), other_origin_url);
  params->requestor_origin = WebSecurityOrigin::Create(WebURL(requestor_url));

  blink::StorageKey storage_key_to_commit = blink::StorageKey::CreateWithNonce(
      url::Origin(), base::UnguessableToken::Create());
  params->storage_key = storage_key_to_commit;

  LocalFrame* local_frame =
      To<LocalFrame>(web_view_impl->GetPage()->MainFrame());
  local_frame->Loader().CommitNavigation(std::move(params), nullptr);

  EXPECT_EQ(
      BlinkStorageKey::CreateWithNonce(SecurityOrigin::Create(other_origin_url),
                                       storage_key_to_commit.nonce().value()),
      local_frame->DomWindow()->GetStorageKey());
}

// Tests that committing a Javascript URL keeps the storage key's nonce of the
// previous document, ensuring that
// `DocumentLoader::CreateWebNavigationParamsToCloneDocument` works correctly
// w.r.t. storage key.
TEST_F(DocumentLoaderTest, JavascriptURLKeepsStorageKeyNonce) {
  WebViewImpl* web_view_impl = web_view_helper_.Initialize();

  BlinkStorageKey storage_key = BlinkStorageKey::CreateWithNonce(
      SecurityOrigin::CreateUniqueOpaque(), base::UnguessableToken::Create());

  LocalFrame* frame = To<LocalFrame>(web_view_impl->GetPage()->MainFrame());
  frame->DomWindow()->SetStorageKey(storage_key);

  frame->LoadJavaScriptURL(
      url_test_helpers::ToKURL("javascript:'<p>hello world</p>'"));

  EXPECT_EQ(storage_key.GetNonce(),
            frame->DomWindow()->GetStorageKey().GetNonce());
}

TEST_F(DocumentLoaderTest, PublicSecureNotCounted) {
  // Checking to make sure secure pages served in the public address space
  // aren't counted for WebFeature::kMainFrameNonSecurePrivateAddressSpace
  WebViewImpl* web_view_impl =
      web_view_helper_.InitializeAndLoad("https://example.com/foo.html");
  Document* document =
      To<LocalFrame>(web_view_impl->GetPage()->MainFrame())->GetDocument();
  EXPECT_FALSE(document->IsUseCounted(
      WebFeature::kMainFrameNonSecurePrivateAddressSpace));
}

TEST_F(DocumentLoaderTest, PublicNonSecureNotCounted) {
  // Checking to make sure non-secure pages served in the public address space
  // aren't counted for WebFeature::kMainFrameNonSecurePrivateAddressSpace
  WebViewImpl* web_view_impl =
      web_view_helper_.InitializeAndLoad("http://example.com/foo.html");
  Document* document =
      To<LocalFrame>(web_view_impl->GetPage()->MainFrame())->GetDocument();
  EXPECT_FALSE(document->IsUseCounted(
      WebFeature::kMainFrameNonSecurePrivateAddressSpace));
}

TEST_F(DocumentLoaderTest, PrivateSecureNotCounted) {
  // Checking to make sure secure pages served in the private address space
  // aren't counted for WebFeature::kMainFrameNonSecurePrivateAddressSpace
  WebViewImpl* web_view_impl =
      web_view_helper_.InitializeAndLoad("https://192.168.1.1/foo.html");
  Document* document =
      To<LocalFrame>(web_view_impl->GetPage()->MainFrame())->GetDocument();
  EXPECT_FALSE(document->IsUseCounted(
      WebFeature::kMainFrameNonSecurePrivateAddressSpace));
}

TEST_F(DocumentLoaderTest, PrivateNonSecureIsCounted) {
  // Checking to make sure non-secure pages served in the private address space
  // are counted for WebFeature::kMainFrameNonSecurePrivateAddressSpace
  WebViewImpl* web_view_impl =
      web_view_helper_.InitializeAndLoad("http://192.168.1.1/foo.html");
  Document* document =
      To<LocalFrame>(web_view_impl->GetPage()->MainFrame())->GetDocument();
  EXPECT_TRUE(document->IsUseCounted(
      WebFeature::kMainFrameNonSecurePrivateAddressSpace));
}

TEST_F(DocumentLoaderTest, LocalNonSecureIsCounted) {
  // Checking to make sure non-secure pages served in the local address space
  // are counted for WebFeature::kMainFrameNonSecurePrivateAddressSpace
  WebViewImpl* web_view_impl =
      web_view_helper_.InitializeAndLoad("http://somethinglocal/foo.html");
  Document* document =
      To<LocalFrame>(web_view_impl->GetPage()->MainFrame())->GetDocument();
  EXPECT_TRUE(document->IsUseCounted(
      WebFeature::kMainFrameNonSecurePrivateAddressSpace));
}

TEST_F(DocumentLoaderSimTest, PrivateNonSecureChildFrameNotCounted) {
  // Checking to make sure non-secure iframes served in the private address
  // space are not counted for
  // WebFeature::kMainFrameNonSecurePrivateAddressSpace
  SimRequest main_resource("http://example.com", "text/html");
  SimRequest iframe_resource("http://192.168.1.1/foo.html", "text/html");
  LoadURL("http://example.com");

  main_resource.Write(R"(
    <iframe id='frame1'></iframe>
    <script>
      const iframe = document.getElementById('frame1');
      iframe.src = 'http://192.168.1.1/foo.html'; // navigation triggered
    </script>
  )");

  main_resource.Finish();
  iframe_resource.Finish();

  auto* child_frame = To<WebLocalFrameImpl>(MainFrame().FirstChild());
  auto* child_document = child_frame->GetFrame()->GetDocument();

  EXPECT_FALSE(child_document->IsUseCounted(
      WebFeature::kMainFrameNonSecurePrivateAddressSpace));
}

}  // namespace blink
