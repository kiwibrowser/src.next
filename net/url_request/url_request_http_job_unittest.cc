// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/url_request/url_request_http_job.h"

#include <stdint.h>

#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

#include "base/compiler_specific.h"
#include "base/memory/ptr_util.h"
#include "base/memory/ref_counted.h"
#include "base/run_loop.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/test/bind.h"
#include "base/test/metrics/histogram_tester.h"
#include "base/test/scoped_feature_list.h"
#include "base/test/task_environment.h"
#include "build/build_config.h"
#include "net/base/auth.h"
#include "net/base/features.h"
#include "net/base/isolation_info.h"
#include "net/base/network_isolation_key.h"
#include "net/base/proxy_server.h"
#include "net/base/proxy_string_util.h"
#include "net/base/request_priority.h"
#include "net/cert/ct_policy_status.h"
#include "net/cookies/canonical_cookie_test_helpers.h"
#include "net/cookies/cookie_monster.h"
#include "net/cookies/cookie_store_test_callbacks.h"
#include "net/cookies/cookie_store_test_helpers.h"
#include "net/cookies/test_cookie_access_delegate.h"
#include "net/http/http_transaction_factory.h"
#include "net/http/http_transaction_test_util.h"
#include "net/http/transport_security_state.h"
#include "net/log/net_log_event_type.h"
#include "net/log/test_net_log.h"
#include "net/log/test_net_log_util.h"
#include "net/net_buildflags.h"
#include "net/proxy_resolution/configured_proxy_resolution_service.h"
#include "net/socket/next_proto.h"
#include "net/socket/socket_test_util.h"
#include "net/test/cert_test_util.h"
#include "net/test/embedded_test_server/default_handlers.h"
#include "net/test/gtest_util.h"
#include "net/test/test_data_directory.h"
#include "net/test/test_with_task_environment.h"
#include "net/traffic_annotation/network_traffic_annotation_test_helper.h"
#include "net/url_request/url_request.h"
#include "net/url_request/url_request_context.h"
#include "net/url_request/url_request_context_builder.h"
#include "net/url_request/url_request_test_util.h"
#include "net/url_request/websocket_handshake_userdata_key.h"
#include "net/websockets/websocket_test_util.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"
#include "url/url_constants.h"

#if BUILDFLAG(IS_ANDROID)
#include "base/android/jni_android.h"
#include "net/net_test_jni_headers/AndroidNetworkLibraryTestUtil_jni.h"
#endif

using net::test::IsError;
using net::test::IsOk;

namespace net {

namespace {

using ::testing::_;
using ::testing::Return;
using ::testing::UnorderedElementsAre;

const char kSimpleGetMockWrite[] =
    "GET / HTTP/1.1\r\n"
    "Host: www.example.com\r\n"
    "Connection: keep-alive\r\n"
    "User-Agent: \r\n"
    "Accept-Encoding: gzip, deflate\r\n"
    "Accept-Language: en-us,fr\r\n\r\n";

const char kSimpleHeadMockWrite[] =
    "HEAD / HTTP/1.1\r\n"
    "Host: www.example.com\r\n"
    "Connection: keep-alive\r\n"
    "User-Agent: \r\n"
    "Accept-Encoding: gzip, deflate\r\n"
    "Accept-Language: en-us,fr\r\n\r\n";

const char kTrustAnchorRequestHistogram[] =
    "Net.Certificate.TrustAnchor.Request";

// Inherit from URLRequestHttpJob to expose the priority and some
// other hidden functions.
class TestURLRequestHttpJob : public URLRequestHttpJob {
 public:
  explicit TestURLRequestHttpJob(URLRequest* request)
      : URLRequestHttpJob(request,
                          request->context()->http_user_agent_settings()) {}

  TestURLRequestHttpJob(const TestURLRequestHttpJob&) = delete;
  TestURLRequestHttpJob& operator=(const TestURLRequestHttpJob&) = delete;

  ~TestURLRequestHttpJob() override = default;

  // URLRequestJob implementation:
  std::unique_ptr<SourceStream> SetUpSourceStream() override {
    if (use_null_source_stream_)
      return nullptr;
    return URLRequestHttpJob::SetUpSourceStream();
  }

  void set_use_null_source_stream(bool use_null_source_stream) {
    use_null_source_stream_ = use_null_source_stream;
  }

  using URLRequestHttpJob::SetPriority;
  using URLRequestHttpJob::Start;
  using URLRequestHttpJob::Kill;
  using URLRequestHttpJob::priority;

 private:
  bool use_null_source_stream_ = false;
};

class URLRequestHttpJobSetUpSourceTest : public TestWithTaskEnvironment {
 public:
  URLRequestHttpJobSetUpSourceTest() {
    auto context_builder = CreateTestURLRequestContextBuilder();
    context_builder->set_client_socket_factory_for_testing(&socket_factory_);
    context_ = context_builder->Build();
  }

 protected:
  MockClientSocketFactory socket_factory_;

  std::unique_ptr<URLRequestContext> context_;
  TestDelegate delegate_;
};

// Tests that if SetUpSourceStream() returns nullptr, the request fails.
TEST_F(URLRequestHttpJobSetUpSourceTest, SetUpSourceFails) {
  MockWrite writes[] = {MockWrite(kSimpleGetMockWrite)};
  MockRead reads[] = {MockRead("HTTP/1.1 200 OK\r\n"
                               "Content-Length: 12\r\n\r\n"),
                      MockRead("Test Content")};

  StaticSocketDataProvider socket_data(reads, writes);
  socket_factory_.AddSocketDataProvider(&socket_data);

  std::unique_ptr<URLRequest> request =
      context_->CreateRequest(GURL("http://www.example.com"), DEFAULT_PRIORITY,
                              &delegate_, TRAFFIC_ANNOTATION_FOR_TESTS);
  auto job = std::make_unique<TestURLRequestHttpJob>(request.get());
  job->set_use_null_source_stream(true);
  TestScopedURLInterceptor interceptor(request->url(), std::move(job));
  request->Start();

  delegate_.RunUntilComplete();
  EXPECT_EQ(ERR_CONTENT_DECODING_INIT_FAILED, delegate_.request_status());
}

// Tests that if there is an unknown content-encoding type, the raw response
// body is passed through.
TEST_F(URLRequestHttpJobSetUpSourceTest, UnknownEncoding) {
  MockWrite writes[] = {MockWrite(kSimpleGetMockWrite)};
  MockRead reads[] = {MockRead("HTTP/1.1 200 OK\r\n"
                               "Content-Encoding: foo, gzip\r\n"
                               "Content-Length: 12\r\n\r\n"),
                      MockRead("Test Content")};

  StaticSocketDataProvider socket_data(reads, writes);
  socket_factory_.AddSocketDataProvider(&socket_data);

  std::unique_ptr<URLRequest> request =
      context_->CreateRequest(GURL("http://www.example.com"), DEFAULT_PRIORITY,
                              &delegate_, TRAFFIC_ANNOTATION_FOR_TESTS);
  auto job = std::make_unique<TestURLRequestHttpJob>(request.get());
  TestScopedURLInterceptor interceptor(request->url(), std::move(job));
  request->Start();

  delegate_.RunUntilComplete();
  EXPECT_EQ(OK, delegate_.request_status());
  EXPECT_EQ("Test Content", delegate_.data_received());
}

// TaskEnvironment is required to instantiate a
// net::ConfiguredProxyResolutionService, which registers itself as an IP
// Address Observer with the NetworkChangeNotifier.
using URLRequestHttpJobWithProxyTest = TestWithTaskEnvironment;

class URLRequestHttpJobWithProxy {
 public:
  explicit URLRequestHttpJobWithProxy(
      std::unique_ptr<ProxyResolutionService> proxy_resolution_service) {
    auto context_builder = CreateTestURLRequestContextBuilder();
    context_builder->set_client_socket_factory_for_testing(&socket_factory_);
    if (proxy_resolution_service) {
      context_builder->set_proxy_resolution_service(
          std::move(proxy_resolution_service));
    }
    context_ = context_builder->Build();
  }

  URLRequestHttpJobWithProxy(const URLRequestHttpJobWithProxy&) = delete;
  URLRequestHttpJobWithProxy& operator=(const URLRequestHttpJobWithProxy&) =
      delete;

  MockClientSocketFactory socket_factory_;
  std::unique_ptr<URLRequestContext> context_;
};

// Tests that when proxy is not used, the proxy server is set correctly on the
// URLRequest.
TEST_F(URLRequestHttpJobWithProxyTest, TestFailureWithoutProxy) {
  URLRequestHttpJobWithProxy http_job_with_proxy(nullptr);

  MockWrite writes[] = {MockWrite(kSimpleGetMockWrite)};
  MockRead reads[] = {MockRead(SYNCHRONOUS, ERR_CONNECTION_RESET)};

  StaticSocketDataProvider socket_data(reads, writes);
  http_job_with_proxy.socket_factory_.AddSocketDataProvider(&socket_data);

  TestDelegate delegate;
  std::unique_ptr<URLRequest> request =
      http_job_with_proxy.context_->CreateRequest(
          GURL("http://www.example.com"), DEFAULT_PRIORITY, &delegate,
          TRAFFIC_ANNOTATION_FOR_TESTS);

  request->Start();
  ASSERT_TRUE(request->is_pending());
  delegate.RunUntilComplete();

  EXPECT_THAT(delegate.request_status(), IsError(ERR_CONNECTION_RESET));
  EXPECT_EQ(ProxyServer::Direct(), request->proxy_server());
  EXPECT_EQ(0, request->received_response_content_length());
  EXPECT_EQ(CountWriteBytes(writes), request->GetTotalSentBytes());
  EXPECT_EQ(CountReadBytes(reads), request->GetTotalReceivedBytes());
}

// Tests that when one proxy is in use and the connection to the proxy server
// fails, the proxy server is still set correctly on the URLRequest.
TEST_F(URLRequestHttpJobWithProxyTest, TestSuccessfulWithOneProxy) {
  const char kSimpleProxyGetMockWrite[] =
      "GET http://www.example.com/ HTTP/1.1\r\n"
      "Host: www.example.com\r\n"
      "Proxy-Connection: keep-alive\r\n"
      "User-Agent: \r\n"
      "Accept-Encoding: gzip, deflate\r\n"
      "Accept-Language: en-us,fr\r\n\r\n";

  const ProxyServer proxy_server =
      ProxyUriToProxyServer("http://origin.net:80", ProxyServer::SCHEME_HTTP);

  std::unique_ptr<ProxyResolutionService> proxy_resolution_service =
      ConfiguredProxyResolutionService::CreateFixedFromPacResultForTest(
          ProxyServerToPacResultElement(proxy_server),
          TRAFFIC_ANNOTATION_FOR_TESTS);

  MockWrite writes[] = {MockWrite(kSimpleProxyGetMockWrite)};
  MockRead reads[] = {MockRead(SYNCHRONOUS, ERR_CONNECTION_RESET)};

  StaticSocketDataProvider socket_data(reads, writes);

  URLRequestHttpJobWithProxy http_job_with_proxy(
      std::move(proxy_resolution_service));
  http_job_with_proxy.socket_factory_.AddSocketDataProvider(&socket_data);

  TestDelegate delegate;
  std::unique_ptr<URLRequest> request =
      http_job_with_proxy.context_->CreateRequest(
          GURL("http://www.example.com"), DEFAULT_PRIORITY, &delegate,
          TRAFFIC_ANNOTATION_FOR_TESTS);

  request->Start();
  ASSERT_TRUE(request->is_pending());
  delegate.RunUntilComplete();

  EXPECT_THAT(delegate.request_status(), IsError(ERR_CONNECTION_RESET));
  // When request fails due to proxy connection errors, the proxy server should
  // still be set on the |request|.
  EXPECT_EQ(proxy_server, request->proxy_server());
  EXPECT_EQ(0, request->received_response_content_length());
  EXPECT_EQ(CountWriteBytes(writes), request->GetTotalSentBytes());
  EXPECT_EQ(0, request->GetTotalReceivedBytes());
}

// Tests that when two proxies are in use and the connection to the first proxy
// server fails, the proxy server is set correctly on the URLRequest.
TEST_F(URLRequestHttpJobWithProxyTest,
       TestContentLengthSuccessfulRequestWithTwoProxies) {
  const ProxyServer proxy_server =
      ProxyUriToProxyServer("http://origin.net:80", ProxyServer::SCHEME_HTTP);

  // Connection to |proxy_server| would fail. Request should be fetched over
  // DIRECT.
  std::unique_ptr<ProxyResolutionService> proxy_resolution_service =
      ConfiguredProxyResolutionService::CreateFixedFromPacResultForTest(
          ProxyServerToPacResultElement(proxy_server) + "; DIRECT",
          TRAFFIC_ANNOTATION_FOR_TESTS);

  MockWrite writes[] = {MockWrite(kSimpleGetMockWrite)};
  MockRead reads[] = {MockRead("HTTP/1.1 200 OK\r\n"
                               "Content-Length: 12\r\n\r\n"),
                      MockRead("Test Content"), MockRead(ASYNC, OK)};

  MockConnect mock_connect_1(SYNCHRONOUS, ERR_CONNECTION_RESET);
  StaticSocketDataProvider connect_data_1;
  connect_data_1.set_connect_data(mock_connect_1);

  StaticSocketDataProvider socket_data(reads, writes);

  URLRequestHttpJobWithProxy http_job_with_proxy(
      std::move(proxy_resolution_service));
  http_job_with_proxy.socket_factory_.AddSocketDataProvider(&connect_data_1);
  http_job_with_proxy.socket_factory_.AddSocketDataProvider(&socket_data);

  TestDelegate delegate;
  std::unique_ptr<URLRequest> request =
      http_job_with_proxy.context_->CreateRequest(
          GURL("http://www.example.com"), DEFAULT_PRIORITY, &delegate,
          TRAFFIC_ANNOTATION_FOR_TESTS);

  request->Start();
  ASSERT_TRUE(request->is_pending());
  base::RunLoop().RunUntilIdle();

  EXPECT_THAT(delegate.request_status(), IsOk());
  EXPECT_EQ(ProxyServer::Direct(), request->proxy_server());
  EXPECT_EQ(12, request->received_response_content_length());
  EXPECT_EQ(CountWriteBytes(writes), request->GetTotalSentBytes());
  EXPECT_EQ(CountReadBytes(reads), request->GetTotalReceivedBytes());
}

class URLRequestHttpJobTest : public TestWithTaskEnvironment {
 protected:
  URLRequestHttpJobTest() {
    auto context_builder = CreateTestURLRequestContextBuilder();
    context_builder->SetHttpTransactionFactoryForTesting(
        std::make_unique<MockNetworkLayer>());
    context_builder->DisableHttpCache();
    context_builder->set_net_log(NetLog::Get());
    context_ = context_builder->Build();

    req_ = context_->CreateRequest(GURL("http://www.example.com"),
                                   DEFAULT_PRIORITY, &delegate_,
                                   TRAFFIC_ANNOTATION_FOR_TESTS);
  }

  MockNetworkLayer& network_layer() {
    // This cast is safe because we set a MockNetworkLayer in the constructor.
    return *static_cast<MockNetworkLayer*>(
        context_->http_transaction_factory());
  }

  std::unique_ptr<URLRequest> CreateFirstPartyRequest(
      const URLRequestContext& context,
      const GURL& url,
      URLRequest::Delegate* delegate) {
    auto req = context.CreateRequest(url, DEFAULT_PRIORITY, delegate,
                                     TRAFFIC_ANNOTATION_FOR_TESTS);
    req->set_initiator(url::Origin::Create(url));
    req->set_site_for_cookies(SiteForCookies::FromUrl(url));
    return req;
  }

  std::unique_ptr<URLRequestContext> context_;
  TestDelegate delegate_;
  RecordingNetLogObserver net_log_observer_;
  std::unique_ptr<URLRequest> req_;
};

class URLRequestHttpJobWithMockSocketsTest : public TestWithTaskEnvironment {
 protected:
  URLRequestHttpJobWithMockSocketsTest() {
    auto context_builder = CreateTestURLRequestContextBuilder();
    context_builder->set_client_socket_factory_for_testing(&socket_factory_);
    context_ = context_builder->Build();
  }

  MockClientSocketFactory socket_factory_;
  std::unique_ptr<URLRequestContext> context_;
};

TEST_F(URLRequestHttpJobWithMockSocketsTest,
       TestContentLengthSuccessfulRequest) {
  MockWrite writes[] = {MockWrite(kSimpleGetMockWrite)};
  MockRead reads[] = {MockRead("HTTP/1.1 200 OK\r\n"
                               "Content-Length: 12\r\n\r\n"),
                      MockRead("Test Content")};

  StaticSocketDataProvider socket_data(reads, writes);
  socket_factory_.AddSocketDataProvider(&socket_data);

  TestDelegate delegate;
  std::unique_ptr<URLRequest> request =
      context_->CreateRequest(GURL("http://www.example.com"), DEFAULT_PRIORITY,
                              &delegate, TRAFFIC_ANNOTATION_FOR_TESTS);

  request->Start();
  ASSERT_TRUE(request->is_pending());
  delegate.RunUntilComplete();

  EXPECT_THAT(delegate.request_status(), IsOk());
  EXPECT_EQ(12, request->received_response_content_length());
  EXPECT_EQ(CountWriteBytes(writes), request->GetTotalSentBytes());
  EXPECT_EQ(CountReadBytes(reads), request->GetTotalReceivedBytes());
}

// Tests a successful HEAD request.
TEST_F(URLRequestHttpJobWithMockSocketsTest, TestSuccessfulHead) {
  MockWrite writes[] = {MockWrite(kSimpleHeadMockWrite)};
  MockRead reads[] = {
      MockRead("HTTP/1.1 200 OK\r\n"
               "Content-Length: 0\r\n\r\n")};

  StaticSocketDataProvider socket_data(reads, writes);
  socket_factory_.AddSocketDataProvider(&socket_data);

  TestDelegate delegate;
  std::unique_ptr<URLRequest> request =
      context_->CreateRequest(GURL("http://www.example.com"), DEFAULT_PRIORITY,
                              &delegate, TRAFFIC_ANNOTATION_FOR_TESTS);

  request->set_method("HEAD");
  request->Start();
  ASSERT_TRUE(request->is_pending());
  delegate.RunUntilComplete();

  EXPECT_THAT(delegate.request_status(), IsOk());
  EXPECT_EQ(0, request->received_response_content_length());
  EXPECT_EQ(CountWriteBytes(writes), request->GetTotalSentBytes());
  EXPECT_EQ(CountReadBytes(reads), request->GetTotalReceivedBytes());
}

// Similar to above test but tests that even if response body is there in the
// HEAD response stream, it should not be read due to HttpStreamParser's logic.
TEST_F(URLRequestHttpJobWithMockSocketsTest, TestSuccessfulHeadWithContent) {
  MockWrite writes[] = {MockWrite(kSimpleHeadMockWrite)};
  MockRead reads[] = {MockRead("HTTP/1.1 200 OK\r\n"
                               "Content-Length: 12\r\n\r\n"),
                      MockRead("Test Content")};

  StaticSocketDataProvider socket_data(reads, writes);
  socket_factory_.AddSocketDataProvider(&socket_data);

  TestDelegate delegate;
  std::unique_ptr<URLRequest> request =
      context_->CreateRequest(GURL("http://www.example.com"), DEFAULT_PRIORITY,
                              &delegate, TRAFFIC_ANNOTATION_FOR_TESTS);

  request->set_method("HEAD");
  request->Start();
  ASSERT_TRUE(request->is_pending());
  delegate.RunUntilComplete();

  EXPECT_THAT(delegate.request_status(), IsOk());
  EXPECT_EQ(0, request->received_response_content_length());
  EXPECT_EQ(CountWriteBytes(writes), request->GetTotalSentBytes());
  EXPECT_EQ(CountReadBytes(reads) - 12, request->GetTotalReceivedBytes());
}

TEST_F(URLRequestHttpJobWithMockSocketsTest, TestSuccessfulCachedHeadRequest) {
  const url::Origin kOrigin1 =
      url::Origin::Create(GURL("http://www.example.com"));
  const IsolationInfo kTestIsolationInfo =
      IsolationInfo::CreateForInternalRequest(kOrigin1);

  // Cache the response.
  {
    MockWrite writes[] = {MockWrite(kSimpleGetMockWrite)};
    MockRead reads[] = {MockRead("HTTP/1.1 200 OK\r\n"
                                 "Content-Length: 12\r\n\r\n"),
                        MockRead("Test Content")};

    StaticSocketDataProvider socket_data(reads, writes);
    socket_factory_.AddSocketDataProvider(&socket_data);

    TestDelegate delegate;
    std::unique_ptr<URLRequest> request = context_->CreateRequest(
        GURL("http://www.example.com"), DEFAULT_PRIORITY, &delegate,
        TRAFFIC_ANNOTATION_FOR_TESTS);

    request->set_isolation_info(kTestIsolationInfo);
    request->Start();
    ASSERT_TRUE(request->is_pending());
    delegate.RunUntilComplete();

    EXPECT_THAT(delegate.request_status(), IsOk());
    EXPECT_EQ(12, request->received_response_content_length());
    EXPECT_EQ(CountWriteBytes(writes), request->GetTotalSentBytes());
    EXPECT_EQ(CountReadBytes(reads), request->GetTotalReceivedBytes());
  }

  // Send a HEAD request for the cached response.
  {
    MockWrite writes[] = {MockWrite(kSimpleHeadMockWrite)};
    MockRead reads[] = {
        MockRead("HTTP/1.1 200 OK\r\n"
                 "Content-Length: 0\r\n\r\n")};

    StaticSocketDataProvider socket_data(reads, writes);
    socket_factory_.AddSocketDataProvider(&socket_data);

    TestDelegate delegate;
    std::unique_ptr<URLRequest> request = context_->CreateRequest(
        GURL("http://www.example.com"), DEFAULT_PRIORITY, &delegate,
        TRAFFIC_ANNOTATION_FOR_TESTS);

    // Use the cached version.
    request->SetLoadFlags(LOAD_SKIP_CACHE_VALIDATION);
    request->set_method("HEAD");
    request->set_isolation_info(kTestIsolationInfo);
    request->Start();
    ASSERT_TRUE(request->is_pending());
    delegate.RunUntilComplete();

    EXPECT_THAT(delegate.request_status(), IsOk());
    EXPECT_EQ(0, request->received_response_content_length());
    EXPECT_EQ(0, request->GetTotalSentBytes());
    EXPECT_EQ(0, request->GetTotalReceivedBytes());
  }
}

TEST_F(URLRequestHttpJobWithMockSocketsTest,
       TestContentLengthSuccessfulHttp09Request) {
  MockWrite writes[] = {MockWrite(kSimpleGetMockWrite)};
  MockRead reads[] = {MockRead("Test Content"),
                      MockRead(net::SYNCHRONOUS, net::OK)};

  StaticSocketDataProvider socket_data(reads, base::span<MockWrite>());
  socket_factory_.AddSocketDataProvider(&socket_data);

  TestDelegate delegate;
  std::unique_ptr<URLRequest> request =
      context_->CreateRequest(GURL("http://www.example.com"), DEFAULT_PRIORITY,
                              &delegate, TRAFFIC_ANNOTATION_FOR_TESTS);

  request->Start();
  ASSERT_TRUE(request->is_pending());
  delegate.RunUntilComplete();

  EXPECT_THAT(delegate.request_status(), IsOk());
  EXPECT_EQ(12, request->received_response_content_length());
  EXPECT_EQ(CountWriteBytes(writes), request->GetTotalSentBytes());
  EXPECT_EQ(CountReadBytes(reads), request->GetTotalReceivedBytes());
}

TEST_F(URLRequestHttpJobWithMockSocketsTest, TestContentLengthFailedRequest) {
  MockWrite writes[] = {MockWrite(kSimpleGetMockWrite)};
  MockRead reads[] = {MockRead("HTTP/1.1 200 OK\r\n"
                               "Content-Length: 20\r\n\r\n"),
                      MockRead("Test Content"),
                      MockRead(net::SYNCHRONOUS, net::ERR_FAILED)};

  StaticSocketDataProvider socket_data(reads, writes);
  socket_factory_.AddSocketDataProvider(&socket_data);

  TestDelegate delegate;
  std::unique_ptr<URLRequest> request =
      context_->CreateRequest(GURL("http://www.example.com"), DEFAULT_PRIORITY,
                              &delegate, TRAFFIC_ANNOTATION_FOR_TESTS);

  request->Start();
  ASSERT_TRUE(request->is_pending());
  delegate.RunUntilComplete();

  EXPECT_THAT(delegate.request_status(), IsError(ERR_FAILED));
  EXPECT_EQ(12, request->received_response_content_length());
  EXPECT_EQ(CountWriteBytes(writes), request->GetTotalSentBytes());
  EXPECT_EQ(CountReadBytes(reads), request->GetTotalReceivedBytes());
}

TEST_F(URLRequestHttpJobWithMockSocketsTest,
       TestContentLengthCancelledRequest) {
  MockWrite writes[] = {MockWrite(kSimpleGetMockWrite)};
  MockRead reads[] = {MockRead("HTTP/1.1 200 OK\r\n"
                               "Content-Length: 20\r\n\r\n"),
                      MockRead("Test Content"),
                      MockRead(net::SYNCHRONOUS, net::ERR_IO_PENDING)};

  StaticSocketDataProvider socket_data(reads, writes);
  socket_factory_.AddSocketDataProvider(&socket_data);

  TestDelegate delegate;
  std::unique_ptr<URLRequest> request =
      context_->CreateRequest(GURL("http://www.example.com"), DEFAULT_PRIORITY,
                              &delegate, TRAFFIC_ANNOTATION_FOR_TESTS);

  delegate.set_cancel_in_received_data(true);
  request->Start();
  base::RunLoop().RunUntilIdle();

  EXPECT_THAT(delegate.request_status(), IsError(ERR_ABORTED));
  EXPECT_EQ(12, request->received_response_content_length());
  EXPECT_EQ(CountWriteBytes(writes), request->GetTotalSentBytes());
  EXPECT_EQ(CountReadBytes(reads), request->GetTotalReceivedBytes());
}

TEST_F(URLRequestHttpJobWithMockSocketsTest,
       TestNetworkBytesRedirectedRequest) {
  MockWrite redirect_writes[] = {
      MockWrite("GET / HTTP/1.1\r\n"
                "Host: www.redirect.com\r\n"
                "Connection: keep-alive\r\n"
                "User-Agent: \r\n"
                "Accept-Encoding: gzip, deflate\r\n"
                "Accept-Language: en-us,fr\r\n\r\n")};

  MockRead redirect_reads[] = {
      MockRead("HTTP/1.1 302 Found\r\n"
               "Location: http://www.example.com\r\n\r\n"),
  };
  StaticSocketDataProvider redirect_socket_data(redirect_reads,
                                                redirect_writes);
  socket_factory_.AddSocketDataProvider(&redirect_socket_data);

  MockWrite final_writes[] = {MockWrite(kSimpleGetMockWrite)};
  MockRead final_reads[] = {MockRead("HTTP/1.1 200 OK\r\n"
                                     "Content-Length: 12\r\n\r\n"),
                            MockRead("Test Content")};
  StaticSocketDataProvider final_socket_data(final_reads, final_writes);
  socket_factory_.AddSocketDataProvider(&final_socket_data);

  TestDelegate delegate;
  std::unique_ptr<URLRequest> request =
      context_->CreateRequest(GURL("http://www.redirect.com"), DEFAULT_PRIORITY,
                              &delegate, TRAFFIC_ANNOTATION_FOR_TESTS);

  request->Start();
  ASSERT_TRUE(request->is_pending());
  base::RunLoop().RunUntilIdle();

  EXPECT_THAT(delegate.request_status(), IsOk());
  EXPECT_EQ(12, request->received_response_content_length());
  // Should not include the redirect.
  EXPECT_EQ(CountWriteBytes(final_writes), request->GetTotalSentBytes());
  EXPECT_EQ(CountReadBytes(final_reads), request->GetTotalReceivedBytes());
}

TEST_F(URLRequestHttpJobWithMockSocketsTest,
       TestNetworkBytesCancelledAfterHeaders) {
  MockWrite writes[] = {MockWrite(kSimpleGetMockWrite)};
  MockRead reads[] = {MockRead("HTTP/1.1 200 OK\r\n\r\n")};
  StaticSocketDataProvider socket_data(reads, writes);
  socket_factory_.AddSocketDataProvider(&socket_data);

  TestDelegate delegate;
  std::unique_ptr<URLRequest> request =
      context_->CreateRequest(GURL("http://www.example.com"), DEFAULT_PRIORITY,
                              &delegate, TRAFFIC_ANNOTATION_FOR_TESTS);

  delegate.set_cancel_in_response_started(true);
  request->Start();
  base::RunLoop().RunUntilIdle();

  EXPECT_THAT(delegate.request_status(), IsError(ERR_ABORTED));
  EXPECT_EQ(0, request->received_response_content_length());
  EXPECT_EQ(CountWriteBytes(writes), request->GetTotalSentBytes());
  EXPECT_EQ(CountReadBytes(reads), request->GetTotalReceivedBytes());
}

TEST_F(URLRequestHttpJobWithMockSocketsTest,
       TestNetworkBytesCancelledImmediately) {
  StaticSocketDataProvider socket_data;
  socket_factory_.AddSocketDataProvider(&socket_data);

  TestDelegate delegate;
  std::unique_ptr<URLRequest> request =
      context_->CreateRequest(GURL("http://www.example.com"), DEFAULT_PRIORITY,
                              &delegate, TRAFFIC_ANNOTATION_FOR_TESTS);

  request->Start();
  request->Cancel();
  base::RunLoop().RunUntilIdle();

  EXPECT_THAT(delegate.request_status(), IsError(ERR_ABORTED));
  EXPECT_EQ(0, request->received_response_content_length());
  EXPECT_EQ(0, request->GetTotalSentBytes());
  EXPECT_EQ(0, request->GetTotalReceivedBytes());
}

TEST_F(URLRequestHttpJobWithMockSocketsTest, TestHttpTimeToFirstByte) {
  base::HistogramTester histograms;
  MockWrite writes[] = {MockWrite(kSimpleGetMockWrite)};
  MockRead reads[] = {MockRead("HTTP/1.1 200 OK\r\n"
                               "Content-Length: 12\r\n\r\n"),
                      MockRead("Test Content")};

  StaticSocketDataProvider socket_data(reads, writes);
  socket_factory_.AddSocketDataProvider(&socket_data);

  TestDelegate delegate;
  std::unique_ptr<URLRequest> request =
      context_->CreateRequest(GURL("http://www.example.com"), DEFAULT_PRIORITY,
                              &delegate, TRAFFIC_ANNOTATION_FOR_TESTS);
  histograms.ExpectTotalCount("Net.HttpTimeToFirstByte", 0);

  request->Start();
  delegate.RunUntilComplete();

  EXPECT_THAT(delegate.request_status(), IsOk());
  histograms.ExpectTotalCount("Net.HttpTimeToFirstByte", 1);
}

TEST_F(URLRequestHttpJobWithMockSocketsTest,
       TestHttpTimeToFirstByteForCancelledTask) {
  base::HistogramTester histograms;
  MockWrite writes[] = {MockWrite(kSimpleGetMockWrite)};
  MockRead reads[] = {MockRead("HTTP/1.1 200 OK\r\n"
                               "Content-Length: 12\r\n\r\n"),
                      MockRead("Test Content")};

  StaticSocketDataProvider socket_data(reads, writes);
  socket_factory_.AddSocketDataProvider(&socket_data);

  TestDelegate delegate;
  std::unique_ptr<URLRequest> request =
      context_->CreateRequest(GURL("http://www.example.com"), DEFAULT_PRIORITY,
                              &delegate, TRAFFIC_ANNOTATION_FOR_TESTS);

  request->Start();
  request->Cancel();
  delegate.RunUntilComplete();

  EXPECT_THAT(delegate.request_status(), IsError(ERR_ABORTED));
  histograms.ExpectTotalCount("Net.HttpTimeToFirstByte", 0);
}

TEST_F(URLRequestHttpJobWithMockSocketsTest,
       TestHttpJobSuccessPriorityKeyedTotalTime) {
  base::HistogramTester histograms;

  for (int priority = 0; priority < net::NUM_PRIORITIES; ++priority) {
    for (int request_index = 0; request_index <= priority; ++request_index) {
      MockWrite writes[] = {MockWrite(kSimpleGetMockWrite)};
      MockRead reads[] = {MockRead("HTTP/1.1 200 OK\r\n"
                                   "Content-Length: 12\r\n\r\n"),
                          MockRead("Test Content")};

      StaticSocketDataProvider socket_data(reads, writes);
      socket_factory_.AddSocketDataProvider(&socket_data);

      TestDelegate delegate;
      std::unique_ptr<URLRequest> request =
          context_->CreateRequest(GURL("http://www.example.com/"),
                                  static_cast<net::RequestPriority>(priority),
                                  &delegate, TRAFFIC_ANNOTATION_FOR_TESTS);

      request->Start();
      delegate.RunUntilComplete();
      EXPECT_THAT(delegate.request_status(), IsOk());
    }
  }

  for (int priority = 0; priority < net::NUM_PRIORITIES; ++priority) {
    histograms.ExpectTotalCount("Net.HttpJob.TotalTimeSuccess.Priority" +
                                    base::NumberToString(priority),
                                priority + 1);
  }
}

TEST_F(URLRequestHttpJobWithMockSocketsTest,
       TestHttpJobRecordsTrustAnchorHistograms) {
  SSLSocketDataProvider ssl_socket_data(net::ASYNC, net::OK);
  ssl_socket_data.ssl_info.cert =
      ImportCertFromFile(GetTestCertsDirectory(), "ok_cert.pem");
  // Simulate a certificate chain issued by "C=US, O=Google Trust Services LLC,
  // CN=GTS Root R4". This publicly-trusted root was chosen as it was included
  // in 2017 and is not anticipated to be removed from all supported platforms
  // for a few decades.
  // Note: The actual cert in |cert| does not matter for this testing.
  SHA256HashValue leaf_hash = {{0}};
  SHA256HashValue intermediate_hash = {{1}};
  SHA256HashValue root_hash = {
      {0x98, 0x47, 0xe5, 0x65, 0x3e, 0x5e, 0x9e, 0x84, 0x75, 0x16, 0xe5,
       0xcb, 0x81, 0x86, 0x06, 0xaa, 0x75, 0x44, 0xa1, 0x9b, 0xe6, 0x7f,
       0xd7, 0x36, 0x6d, 0x50, 0x69, 0x88, 0xe8, 0xd8, 0x43, 0x47}};
  ssl_socket_data.ssl_info.public_key_hashes.push_back(HashValue(leaf_hash));
  ssl_socket_data.ssl_info.public_key_hashes.push_back(
      HashValue(intermediate_hash));
  ssl_socket_data.ssl_info.public_key_hashes.push_back(HashValue(root_hash));

  const base::HistogramBase::Sample kGTSRootR4HistogramID = 486;

  socket_factory_.AddSSLSocketDataProvider(&ssl_socket_data);

  MockWrite writes[] = {MockWrite(kSimpleGetMockWrite)};
  MockRead reads[] = {MockRead("HTTP/1.1 200 OK\r\n"
                               "Content-Length: 12\r\n\r\n"),
                      MockRead("Test Content")};
  StaticSocketDataProvider socket_data(reads, writes);
  socket_factory_.AddSocketDataProvider(&socket_data);

  base::HistogramTester histograms;
  histograms.ExpectTotalCount(kTrustAnchorRequestHistogram, 0);

  TestDelegate delegate;
  std::unique_ptr<URLRequest> request = context_->CreateRequest(
      GURL("https://www.example.com/"), DEFAULT_PRIORITY, &delegate,
      TRAFFIC_ANNOTATION_FOR_TESTS);
  request->Start();
  delegate.RunUntilComplete();
  EXPECT_THAT(delegate.request_status(), IsOk());

  histograms.ExpectTotalCount(kTrustAnchorRequestHistogram, 1);
  histograms.ExpectUniqueSample(kTrustAnchorRequestHistogram,
                                kGTSRootR4HistogramID, 1);
}

TEST_F(URLRequestHttpJobWithMockSocketsTest,
       TestHttpJobDoesNotRecordTrustAnchorHistogramsWhenNoNetworkLoad) {
  SSLSocketDataProvider ssl_socket_data(net::ASYNC, net::OK);
  ssl_socket_data.ssl_info.cert =
      ImportCertFromFile(GetTestCertsDirectory(), "ok_cert.pem");
  // Simulate a request loaded from a non-network source, such as a disk
  // cache.
  ssl_socket_data.ssl_info.public_key_hashes.clear();

  socket_factory_.AddSSLSocketDataProvider(&ssl_socket_data);

  MockWrite writes[] = {MockWrite(kSimpleGetMockWrite)};
  MockRead reads[] = {MockRead("HTTP/1.1 200 OK\r\n"
                               "Content-Length: 12\r\n\r\n"),
                      MockRead("Test Content")};
  StaticSocketDataProvider socket_data(reads, writes);
  socket_factory_.AddSocketDataProvider(&socket_data);

  base::HistogramTester histograms;
  histograms.ExpectTotalCount(kTrustAnchorRequestHistogram, 0);

  TestDelegate delegate;
  std::unique_ptr<URLRequest> request = context_->CreateRequest(
      GURL("https://www.example.com/"), DEFAULT_PRIORITY, &delegate,
      TRAFFIC_ANNOTATION_FOR_TESTS);
  request->Start();
  delegate.RunUntilComplete();
  EXPECT_THAT(delegate.request_status(), IsOk());

  histograms.ExpectTotalCount(kTrustAnchorRequestHistogram, 0);
}

TEST_F(URLRequestHttpJobWithMockSocketsTest,
       TestHttpJobRecordsMostSpecificTrustAnchorHistograms) {
  SSLSocketDataProvider ssl_socket_data(net::ASYNC, net::OK);
  ssl_socket_data.ssl_info.cert =
      ImportCertFromFile(GetTestCertsDirectory(), "ok_cert.pem");
  // Simulate a certificate chain issued by "C=US, O=Google Trust Services LLC,
  // CN=GTS Root R4". This publicly-trusted root was chosen as it was included
  // in 2017 and is not anticipated to be removed from all supported platforms
  // for a few decades.
  // Note: The actual cert in |cert| does not matter for this testing.
  SHA256HashValue leaf_hash = {{0}};
  SHA256HashValue intermediate_hash = {{1}};
  SHA256HashValue gts_root_r3_hash = {
      {0x41, 0x79, 0xed, 0xd9, 0x81, 0xef, 0x74, 0x74, 0x77, 0xb4, 0x96,
       0x26, 0x40, 0x8a, 0xf4, 0x3d, 0xaa, 0x2c, 0xa7, 0xab, 0x7f, 0x9e,
       0x08, 0x2c, 0x10, 0x60, 0xf8, 0x40, 0x96, 0x77, 0x43, 0x48}};
  SHA256HashValue gts_root_r4_hash = {
      {0x98, 0x47, 0xe5, 0x65, 0x3e, 0x5e, 0x9e, 0x84, 0x75, 0x16, 0xe5,
       0xcb, 0x81, 0x86, 0x06, 0xaa, 0x75, 0x44, 0xa1, 0x9b, 0xe6, 0x7f,
       0xd7, 0x36, 0x6d, 0x50, 0x69, 0x88, 0xe8, 0xd8, 0x43, 0x47}};
  ssl_socket_data.ssl_info.public_key_hashes.push_back(HashValue(leaf_hash));
  ssl_socket_data.ssl_info.public_key_hashes.push_back(
      HashValue(intermediate_hash));
  ssl_socket_data.ssl_info.public_key_hashes.push_back(
      HashValue(gts_root_r3_hash));
  ssl_socket_data.ssl_info.public_key_hashes.push_back(
      HashValue(gts_root_r4_hash));

  const base::HistogramBase::Sample kGTSRootR3HistogramID = 485;

  socket_factory_.AddSSLSocketDataProvider(&ssl_socket_data);

  MockWrite writes[] = {MockWrite(kSimpleGetMockWrite)};
  MockRead reads[] = {MockRead("HTTP/1.1 200 OK\r\n"
                               "Content-Length: 12\r\n\r\n"),
                      MockRead("Test Content")};
  StaticSocketDataProvider socket_data(reads, writes);
  socket_factory_.AddSocketDataProvider(&socket_data);

  base::HistogramTester histograms;
  histograms.ExpectTotalCount(kTrustAnchorRequestHistogram, 0);

  TestDelegate delegate;
  std::unique_ptr<URLRequest> request = context_->CreateRequest(
      GURL("https://www.example.com/"), DEFAULT_PRIORITY, &delegate,
      TRAFFIC_ANNOTATION_FOR_TESTS);
  request->Start();
  delegate.RunUntilComplete();
  EXPECT_THAT(delegate.request_status(), IsOk());

  histograms.ExpectTotalCount(kTrustAnchorRequestHistogram, 1);
  histograms.ExpectUniqueSample(kTrustAnchorRequestHistogram,
                                kGTSRootR3HistogramID, 1);
}

namespace {

// An ExpectCTReporter that records the number of times OnExpectCTFailed() was
// called.
class MockExpectCTReporter : public TransportSecurityState::ExpectCTReporter {
 public:
  MockExpectCTReporter() = default;
  ~MockExpectCTReporter() override = default;

  void OnExpectCTFailed(
      const HostPortPair& host_port_pair,
      const GURL& report_uri,
      base::Time expiration,
      const X509Certificate* validated_certificate_chain,
      const X509Certificate* served_certificate_chain,
      const SignedCertificateTimestampAndStatusList&
          signed_certificate_timestamps,
      const NetworkIsolationKey& network_isolation_key) override {
    num_failures_++;
    network_isolation_key_ = network_isolation_key;
  }

  int num_failures() const { return num_failures_; }
  const NetworkIsolationKey& network_isolation_key() const {
    return network_isolation_key_;
  }

 private:
  int num_failures_ = 0;
  NetworkIsolationKey network_isolation_key_;
};

}  // namespace

TEST_F(URLRequestHttpJobWithMockSocketsTest,
       TestHttpJobSendsNetworkIsolationKeyWhenProcessingExpectCTHeader) {
  SSLSocketDataProvider ssl_socket_data(net::ASYNC, net::OK);
  ssl_socket_data.ssl_info.cert =
      ImportCertFromFile(GetTestCertsDirectory(), "ok_cert.pem");
  ssl_socket_data.ssl_info.is_issued_by_known_root = true;
  ssl_socket_data.ssl_info.ct_policy_compliance =
      ct::CTPolicyCompliance::CT_POLICY_NOT_DIVERSE_SCTS;

  socket_factory_.AddSSLSocketDataProvider(&ssl_socket_data);

  MockWrite writes[] = {MockWrite(kSimpleGetMockWrite)};
  MockRead reads[] = {
      MockRead(
          "HTTP/1.1 200 OK\r\n"
          "Expect-CT: max-age=100, enforce, report-uri=https://example.test\r\n"
          "Content-Length: 12\r\n\r\n"),
      MockRead("Test Content")};
  StaticSocketDataProvider socket_data(reads, writes);
  socket_factory_.AddSocketDataProvider(&socket_data);

  base::HistogramTester histograms;

  MockExpectCTReporter reporter;
  TransportSecurityState transport_security_state;
  context_->transport_security_state()->SetExpectCTReporter(&reporter);

  TestDelegate delegate;
  std::unique_ptr<URLRequest> request = context_->CreateRequest(
      GURL("https://www.example.com/"), DEFAULT_PRIORITY, &delegate,
      TRAFFIC_ANNOTATION_FOR_TESTS);
  IsolationInfo isolation_info = IsolationInfo::CreateTransient();
  request->set_isolation_info(isolation_info);
  request->Start();
  delegate.RunUntilComplete();
  EXPECT_THAT(delegate.request_status(), IsOk());

  ASSERT_EQ(1, reporter.num_failures());
  EXPECT_EQ(isolation_info.network_isolation_key(),
            reporter.network_isolation_key());
}

TEST_F(URLRequestHttpJobWithMockSocketsTest, EncodingAdvertisementOnRange) {
  MockWrite writes[] = {
      MockWrite("GET / HTTP/1.1\r\n"
                "Host: www.example.com\r\n"
                "Connection: keep-alive\r\n"
                "User-Agent: \r\n"
                "Accept-Encoding: identity\r\n"
                "Accept-Language: en-us,fr\r\n"
                "Range: bytes=0-1023\r\n\r\n")};

  MockRead reads[] = {MockRead("HTTP/1.1 200 OK\r\n"
                               "Accept-Ranges: bytes\r\n"
                               "Content-Length: 12\r\n\r\n"),
                      MockRead("Test Content")};

  StaticSocketDataProvider socket_data(reads, writes);
  socket_factory_.AddSocketDataProvider(&socket_data);

  TestDelegate delegate;
  std::unique_ptr<URLRequest> request =
      context_->CreateRequest(GURL("http://www.example.com"), DEFAULT_PRIORITY,
                              &delegate, TRAFFIC_ANNOTATION_FOR_TESTS);

  // Make the extra header to trigger the change in "Accepted-Encoding"
  HttpRequestHeaders headers;
  headers.SetHeader("Range", "bytes=0-1023");
  request->SetExtraRequestHeaders(headers);

  request->Start();
  base::RunLoop().RunUntilIdle();

  EXPECT_THAT(delegate.request_status(), IsOk());
  EXPECT_EQ(12, request->received_response_content_length());
  EXPECT_EQ(CountWriteBytes(writes), request->GetTotalSentBytes());
  EXPECT_EQ(CountReadBytes(reads), request->GetTotalReceivedBytes());
}

TEST_F(URLRequestHttpJobWithMockSocketsTest, RangeRequestOverrideEncoding) {
  MockWrite writes[] = {
      MockWrite("GET / HTTP/1.1\r\n"
                "Host: www.example.com\r\n"
                "Connection: keep-alive\r\n"
                "Accept-Encoding: gzip, deflate\r\n"
                "User-Agent: \r\n"
                "Accept-Language: en-us,fr\r\n"
                "Range: bytes=0-1023\r\n\r\n")};

  MockRead reads[] = {MockRead("HTTP/1.1 200 OK\r\n"
                               "Accept-Ranges: bytes\r\n"
                               "Content-Length: 12\r\n\r\n"),
                      MockRead("Test Content")};

  StaticSocketDataProvider socket_data(reads, writes);
  socket_factory_.AddSocketDataProvider(&socket_data);

  TestDelegate delegate;
  std::unique_ptr<URLRequest> request =
      context_->CreateRequest(GURL("http://www.example.com"), DEFAULT_PRIORITY,
                              &delegate, TRAFFIC_ANNOTATION_FOR_TESTS);

  // Explicitly set "Accept-Encoding" to make sure it's not overridden by
  // AddExtraHeaders
  HttpRequestHeaders headers;
  headers.SetHeader("Accept-Encoding", "gzip, deflate");
  headers.SetHeader("Range", "bytes=0-1023");
  request->SetExtraRequestHeaders(headers);

  request->Start();
  base::RunLoop().RunUntilIdle();

  EXPECT_THAT(delegate.request_status(), IsOk());
  EXPECT_EQ(12, request->received_response_content_length());
  EXPECT_EQ(CountWriteBytes(writes), request->GetTotalSentBytes());
  EXPECT_EQ(CountReadBytes(reads), request->GetTotalReceivedBytes());
}

TEST_F(URLRequestHttpJobTest, TestCancelWhileReadingCookies) {
  auto context_builder = CreateTestURLRequestContextBuilder();
  context_builder->SetCookieStore(std::make_unique<DelayedCookieMonster>());
  auto context = context_builder->Build();

  TestDelegate delegate;
  std::unique_ptr<URLRequest> request =
      context->CreateRequest(GURL("http://www.example.com"), DEFAULT_PRIORITY,
                             &delegate, TRAFFIC_ANNOTATION_FOR_TESTS);

  request->Start();
  request->Cancel();
  delegate.RunUntilComplete();

  EXPECT_THAT(delegate.request_status(), IsError(ERR_ABORTED));
}

// Make sure that SetPriority actually sets the URLRequestHttpJob's
// priority, before start.  Other tests handle the after start case.
TEST_F(URLRequestHttpJobTest, SetPriorityBasic) {
  auto job = std::make_unique<TestURLRequestHttpJob>(req_.get());
  EXPECT_EQ(DEFAULT_PRIORITY, job->priority());

  job->SetPriority(LOWEST);
  EXPECT_EQ(LOWEST, job->priority());

  job->SetPriority(LOW);
  EXPECT_EQ(LOW, job->priority());
}

// Make sure that URLRequestHttpJob passes on its priority to its
// transaction on start.
TEST_F(URLRequestHttpJobTest, SetTransactionPriorityOnStart) {
  TestScopedURLInterceptor interceptor(
      req_->url(), std::make_unique<TestURLRequestHttpJob>(req_.get()));
  req_->SetPriority(LOW);

  EXPECT_FALSE(network_layer().last_transaction());

  req_->Start();

  ASSERT_TRUE(network_layer().last_transaction());
  EXPECT_EQ(LOW, network_layer().last_transaction()->priority());
}

// Make sure that URLRequestHttpJob passes on its priority updates to
// its transaction.
TEST_F(URLRequestHttpJobTest, SetTransactionPriority) {
  TestScopedURLInterceptor interceptor(
      req_->url(), std::make_unique<TestURLRequestHttpJob>(req_.get()));
  req_->SetPriority(LOW);
  req_->Start();
  ASSERT_TRUE(network_layer().last_transaction());
  EXPECT_EQ(LOW, network_layer().last_transaction()->priority());

  req_->SetPriority(HIGHEST);
  EXPECT_EQ(HIGHEST, network_layer().last_transaction()->priority());
}

TEST_F(URLRequestHttpJobTest, HSTSInternalRedirectTest) {
  // Setup HSTS state.
  context_->transport_security_state()->AddHSTS(
      "upgrade.test", base::Time::Now() + base::Seconds(10), true);
  ASSERT_TRUE(
      context_->transport_security_state()->ShouldUpgradeToSSL("upgrade.test"));
  ASSERT_FALSE(context_->transport_security_state()->ShouldUpgradeToSSL(
      "no-upgrade.test"));

  struct TestCase {
    const char* url;
    bool upgrade_expected;
    const char* url_expected;
  } cases[] = {
    {"http://upgrade.test/", true, "https://upgrade.test/"},
    {"http://upgrade.test:123/", true, "https://upgrade.test:123/"},
    {"http://no-upgrade.test/", false, "http://no-upgrade.test/"},
    {"http://no-upgrade.test:123/", false, "http://no-upgrade.test:123/"},
#if BUILDFLAG(ENABLE_WEBSOCKETS)
    {"ws://upgrade.test/", true, "wss://upgrade.test/"},
    {"ws://upgrade.test:123/", true, "wss://upgrade.test:123/"},
    {"ws://no-upgrade.test/", false, "ws://no-upgrade.test/"},
    {"ws://no-upgrade.test:123/", false, "ws://no-upgrade.test:123/"},
#endif  // BUILDFLAG(ENABLE_WEBSOCKETS)
  };

  for (const auto& test : cases) {
    SCOPED_TRACE(test.url);

    GURL url = GURL(test.url);
    // This is needed to bypass logic that rejects using URLRequests directly
    // for WebSocket requests.
    bool is_for_websockets = url.SchemeIsWSOrWSS();

    TestDelegate d;
    TestNetworkDelegate network_delegate;
    std::unique_ptr<URLRequest> r(context_->CreateRequest(
        url, DEFAULT_PRIORITY, &d, TRAFFIC_ANNOTATION_FOR_TESTS,
        is_for_websockets));

    net_log_observer_.Clear();
    r->Start();
    d.RunUntilComplete();

    if (test.upgrade_expected) {
      auto entries = net_log_observer_.GetEntriesWithType(
          net::NetLogEventType::URL_REQUEST_REDIRECT_JOB);
      int redirects = entries.size();
      for (const auto& entry : entries) {
        EXPECT_EQ("HSTS", GetStringValueFromParams(entry, "reason"));
      }
      EXPECT_EQ(1, redirects);
      EXPECT_EQ(1, d.received_redirect_count());
      EXPECT_EQ(2u, r->url_chain().size());
    } else {
      EXPECT_EQ(0, d.received_redirect_count());
      EXPECT_EQ(1u, r->url_chain().size());
    }
    EXPECT_EQ(GURL(test.url_expected), r->url());
  }
}

TEST_F(URLRequestHttpJobTest, HSTSInternalRedirectCallback) {
  EmbeddedTestServer https_test(EmbeddedTestServer::TYPE_HTTPS);
  https_test.AddDefaultHandlers(base::FilePath());
  ASSERT_TRUE(https_test.Start());

  auto context = CreateTestURLRequestContextBuilder()->Build();
  context->transport_security_state()->AddHSTS(
      "127.0.0.1", base::Time::Now() + base::Seconds(10), true);
  ASSERT_TRUE(
      context->transport_security_state()->ShouldUpgradeToSSL("127.0.0.1"));

  GURL::Replacements replace_scheme;
  replace_scheme.SetSchemeStr("http");

  {
    GURL url(
        https_test.GetURL("/echoheader").ReplaceComponents(replace_scheme));
    TestDelegate delegate;
    HttpRequestHeaders extra_headers;
    extra_headers.SetHeader("X-HSTS-Test", "1");

    HttpRawRequestHeaders raw_req_headers;

    std::unique_ptr<URLRequest> r(context->CreateRequest(
        url, DEFAULT_PRIORITY, &delegate, TRAFFIC_ANNOTATION_FOR_TESTS));
    r->SetExtraRequestHeaders(extra_headers);
    r->SetRequestHeadersCallback(base::BindRepeating(
        &HttpRawRequestHeaders::Assign, base::Unretained(&raw_req_headers)));

    r->Start();
    delegate.RunUntilRedirect();

    EXPECT_FALSE(raw_req_headers.headers().empty());
    std::string value;
    EXPECT_TRUE(raw_req_headers.FindHeaderForTest("X-HSTS-Test", &value));
    EXPECT_EQ("1", value);
    EXPECT_EQ("GET /echoheader HTTP/1.1\r\n", raw_req_headers.request_line());

    raw_req_headers = HttpRawRequestHeaders();

    r->FollowDeferredRedirect(absl::nullopt /* removed_headers */,
                              absl::nullopt /* modified_headers */);
    delegate.RunUntilComplete();

    EXPECT_FALSE(raw_req_headers.headers().empty());
  }

  {
    GURL url(https_test.GetURL("/echoheader?foo=bar")
                 .ReplaceComponents(replace_scheme));
    TestDelegate delegate;

    HttpRawRequestHeaders raw_req_headers;

    std::unique_ptr<URLRequest> r(context->CreateRequest(
        url, DEFAULT_PRIORITY, &delegate, TRAFFIC_ANNOTATION_FOR_TESTS));
    r->SetRequestHeadersCallback(base::BindRepeating(
        &HttpRawRequestHeaders::Assign, base::Unretained(&raw_req_headers)));

    r->Start();
    delegate.RunUntilRedirect();

    EXPECT_EQ("GET /echoheader?foo=bar HTTP/1.1\r\n",
              raw_req_headers.request_line());
  }

  {
    GURL url(
        https_test.GetURL("/echoheader#foo").ReplaceComponents(replace_scheme));
    TestDelegate delegate;

    HttpRawRequestHeaders raw_req_headers;

    std::unique_ptr<URLRequest> r(context->CreateRequest(
        url, DEFAULT_PRIORITY, &delegate, TRAFFIC_ANNOTATION_FOR_TESTS));
    r->SetRequestHeadersCallback(base::BindRepeating(
        &HttpRawRequestHeaders::Assign, base::Unretained(&raw_req_headers)));

    r->Start();
    delegate.RunUntilRedirect();

    EXPECT_EQ("GET /echoheader HTTP/1.1\r\n", raw_req_headers.request_line());
  }
}

class URLRequestHttpJobWithBrotliSupportTest : public TestWithTaskEnvironment {
 protected:
  URLRequestHttpJobWithBrotliSupportTest() {
    HttpNetworkSessionParams params;
    auto context_builder = CreateTestURLRequestContextBuilder();
    context_builder->set_enable_brotli(true);
    context_builder->set_http_network_session_params(params);
    context_builder->set_client_socket_factory_for_testing(&socket_factory_);
    context_ = context_builder->Build();
  }

  MockClientSocketFactory socket_factory_;
  std::unique_ptr<URLRequestContext> context_;
};

TEST_F(URLRequestHttpJobWithBrotliSupportTest, NoBrotliAdvertisementOverHttp) {
  MockWrite writes[] = {MockWrite(kSimpleGetMockWrite)};
  MockRead reads[] = {MockRead("HTTP/1.1 200 OK\r\n"
                               "Content-Length: 12\r\n\r\n"),
                      MockRead("Test Content")};
  StaticSocketDataProvider socket_data(reads, writes);
  socket_factory_.AddSocketDataProvider(&socket_data);

  TestDelegate delegate;
  std::unique_ptr<URLRequest> request =
      context_->CreateRequest(GURL("http://www.example.com"), DEFAULT_PRIORITY,
                              &delegate, TRAFFIC_ANNOTATION_FOR_TESTS);
  request->Start();
  base::RunLoop().RunUntilIdle();

  EXPECT_THAT(delegate.request_status(), IsOk());
  EXPECT_EQ(12, request->received_response_content_length());
  EXPECT_EQ(CountWriteBytes(writes), request->GetTotalSentBytes());
  EXPECT_EQ(CountReadBytes(reads), request->GetTotalReceivedBytes());
}

TEST_F(URLRequestHttpJobWithBrotliSupportTest, BrotliAdvertisement) {
  net::SSLSocketDataProvider ssl_socket_data_provider(net::ASYNC, net::OK);
  ssl_socket_data_provider.next_proto = kProtoHTTP11;
  ssl_socket_data_provider.ssl_info.cert =
      ImportCertFromFile(GetTestCertsDirectory(), "unittest.selfsigned.der");
  ASSERT_TRUE(ssl_socket_data_provider.ssl_info.cert);
  socket_factory_.AddSSLSocketDataProvider(&ssl_socket_data_provider);

  MockWrite writes[] = {
      MockWrite("GET / HTTP/1.1\r\n"
                "Host: www.example.com\r\n"
                "Connection: keep-alive\r\n"
                "User-Agent: \r\n"
                "Accept-Encoding: gzip, deflate, br\r\n"
                "Accept-Language: en-us,fr\r\n\r\n")};
  MockRead reads[] = {MockRead("HTTP/1.1 200 OK\r\n"
                               "Content-Length: 12\r\n\r\n"),
                      MockRead("Test Content")};
  StaticSocketDataProvider socket_data(reads, writes);
  socket_factory_.AddSocketDataProvider(&socket_data);

  TestDelegate delegate;
  std::unique_ptr<URLRequest> request =
      context_->CreateRequest(GURL("https://www.example.com"), DEFAULT_PRIORITY,
                              &delegate, TRAFFIC_ANNOTATION_FOR_TESTS);
  request->Start();
  base::RunLoop().RunUntilIdle();

  EXPECT_THAT(delegate.request_status(), IsOk());
  EXPECT_EQ(12, request->received_response_content_length());
  EXPECT_EQ(CountWriteBytes(writes), request->GetTotalSentBytes());
  EXPECT_EQ(CountReadBytes(reads), request->GetTotalReceivedBytes());
}

TEST_F(URLRequestHttpJobWithBrotliSupportTest, DefaultAcceptEncodingOverriden) {
  struct {
    base::flat_set<net::SourceStream::SourceType> accepted_types;
    const char* expected_request_headers;
  } kTestCases[] = {{{net::SourceStream::SourceType::TYPE_DEFLATE},
                     "GET / HTTP/1.1\r\n"
                     "Host: www.example.com\r\n"
                     "Connection: keep-alive\r\n"
                     "User-Agent: \r\n"
                     "Accept-Encoding: deflate\r\n"
                     "Accept-Language: en-us,fr\r\n\r\n"},
                    {{},
                     "GET / HTTP/1.1\r\n"
                     "Host: www.example.com\r\n"
                     "Connection: keep-alive\r\n"
                     "User-Agent: \r\n"
                     "Accept-Language: en-us,fr\r\n\r\n"},
                    {{net::SourceStream::SourceType::TYPE_GZIP},
                     "GET / HTTP/1.1\r\n"
                     "Host: www.example.com\r\n"
                     "Connection: keep-alive\r\n"
                     "User-Agent: \r\n"
                     "Accept-Encoding: gzip\r\n"
                     "Accept-Language: en-us,fr\r\n\r\n"},
                    {{net::SourceStream::SourceType::TYPE_GZIP,
                      net::SourceStream::SourceType::TYPE_DEFLATE},
                     "GET / HTTP/1.1\r\n"
                     "Host: www.example.com\r\n"
                     "Connection: keep-alive\r\n"
                     "User-Agent: \r\n"
                     "Accept-Encoding: gzip, deflate\r\n"
                     "Accept-Language: en-us,fr\r\n\r\n"},
                    {{net::SourceStream::SourceType::TYPE_BROTLI},
                     "GET / HTTP/1.1\r\n"
                     "Host: www.example.com\r\n"
                     "Connection: keep-alive\r\n"
                     "User-Agent: \r\n"
                     "Accept-Encoding: br\r\n"
                     "Accept-Language: en-us,fr\r\n\r\n"},
                    {{net::SourceStream::SourceType::TYPE_BROTLI,
                      net::SourceStream::SourceType::TYPE_GZIP,
                      net::SourceStream::SourceType::TYPE_DEFLATE},
                     "GET / HTTP/1.1\r\n"
                     "Host: www.example.com\r\n"
                     "Connection: keep-alive\r\n"
                     "User-Agent: \r\n"
                     "Accept-Encoding: gzip, deflate, br\r\n"
                     "Accept-Language: en-us,fr\r\n\r\n"}};

  for (auto test : kTestCases) {
    net::SSLSocketDataProvider ssl_socket_data_provider(net::ASYNC, net::OK);
    ssl_socket_data_provider.next_proto = kProtoHTTP11;
    ssl_socket_data_provider.ssl_info.cert =
        ImportCertFromFile(GetTestCertsDirectory(), "unittest.selfsigned.der");
    ASSERT_TRUE(ssl_socket_data_provider.ssl_info.cert);
    socket_factory_.AddSSLSocketDataProvider(&ssl_socket_data_provider);

    MockWrite writes[] = {MockWrite(test.expected_request_headers)};
    MockRead reads[] = {MockRead("HTTP/1.1 200 OK\r\n"
                                 "Content-Length: 12\r\n\r\n"),
                        MockRead("Test Content")};
    StaticSocketDataProvider socket_data(reads, writes);
    socket_factory_.AddSocketDataProvider(&socket_data);

    TestDelegate delegate;
    std::unique_ptr<URLRequest> request = context_->CreateRequest(
        GURL("https://www.example.com"), DEFAULT_PRIORITY, &delegate,
        TRAFFIC_ANNOTATION_FOR_TESTS);
    request->set_accepted_stream_types(test.accepted_types);
    request->Start();
    delegate.RunUntilComplete();
    EXPECT_THAT(delegate.request_status(), IsOk());
    socket_factory_.ResetNextMockIndexes();
  }
}

#if BUILDFLAG(IS_ANDROID)
class URLRequestHttpJobWithCheckClearTextPermittedTest
    : public TestWithTaskEnvironment {
 protected:
  URLRequestHttpJobWithCheckClearTextPermittedTest() {
    auto context_builder = CreateTestURLRequestContextBuilder();
    context_builder->SetHttpTransactionFactoryForTesting(
        std::make_unique<MockNetworkLayer>());
    context_builder->set_check_cleartext_permitted(true);
    context_builder->set_client_socket_factory_for_testing(&socket_factory_);
    context_ = context_builder->Build();
  }

  MockClientSocketFactory socket_factory_;
  std::unique_ptr<URLRequestContext> context_;
};

TEST_F(URLRequestHttpJobWithCheckClearTextPermittedTest,
       AndroidCleartextPermittedTest) {
  static constexpr struct TestCase {
    const char* url;
    bool cleartext_permitted;
    bool should_block;
    int expected_per_host_call_count;
    int expected_default_call_count;
  } kTestCases[] = {
      {"http://unblocked.test/", true, false, 1, 0},
      {"https://unblocked.test/", true, false, 0, 0},
      {"http://blocked.test/", false, true, 1, 0},
      {"https://blocked.test/", false, false, 0, 0},
      // If determining the per-host cleartext policy causes an
      // IllegalArgumentException (because the hostname is invalid),
      // the default configuration should be applied, and the
      // exception should not cause a JNI error.
      {"http://./", false, true, 1, 1},
      {"http://./", true, false, 1, 1},
      // Even if the host name would be considered invalid, https
      // schemes should not trigger cleartext policy checks.
      {"https://./", false, false, 0, 0},
  };

  JNIEnv* env = base::android::AttachCurrentThread();
  for (const TestCase& test : kTestCases) {
    Java_AndroidNetworkLibraryTestUtil_setUpSecurityPolicyForTesting(
        env, test.cleartext_permitted);

    TestDelegate delegate;
    std::unique_ptr<URLRequest> request =
        context_->CreateRequest(GURL(test.url), DEFAULT_PRIORITY, &delegate,
                                TRAFFIC_ANNOTATION_FOR_TESTS);
    request->Start();
    delegate.RunUntilComplete();

    if (test.should_block) {
      EXPECT_THAT(delegate.request_status(),
                  IsError(ERR_CLEARTEXT_NOT_PERMITTED));
    } else {
      // Should fail since there's no test server running
      EXPECT_THAT(delegate.request_status(), IsError(ERR_FAILED));
    }
    EXPECT_EQ(
        Java_AndroidNetworkLibraryTestUtil_getPerHostCleartextCheckCount(env),
        test.expected_per_host_call_count);
    EXPECT_EQ(
        Java_AndroidNetworkLibraryTestUtil_getDefaultCleartextCheckCount(env),
        test.expected_default_call_count);
  }
}
#endif

#if BUILDFLAG(ENABLE_WEBSOCKETS)

class URLRequestHttpJobWebSocketTest : public TestWithTaskEnvironment {
 protected:
  URLRequestHttpJobWebSocketTest() {
    auto context_builder = CreateTestURLRequestContextBuilder();
    context_builder->set_client_socket_factory_for_testing(&socket_factory_);
    context_ = context_builder->Build();
    req_ =
        context_->CreateRequest(GURL("ws://www.example.org"), DEFAULT_PRIORITY,
                                &delegate_, TRAFFIC_ANNOTATION_FOR_TESTS,
                                /*is_for_websockets=*/true);
  }

  std::unique_ptr<URLRequestContext> context_;
  MockClientSocketFactory socket_factory_;
  TestDelegate delegate_;
  std::unique_ptr<URLRequest> req_;
};

TEST_F(URLRequestHttpJobWebSocketTest, RejectedWithoutCreateHelper) {
  req_->Start();
  base::RunLoop().RunUntilIdle();
  EXPECT_THAT(delegate_.request_status(), IsError(ERR_DISALLOWED_URL_SCHEME));
}

TEST_F(URLRequestHttpJobWebSocketTest, CreateHelperPassedThrough) {
  HttpRequestHeaders headers;
  headers.SetHeader("Connection", "Upgrade");
  headers.SetHeader("Upgrade", "websocket");
  headers.SetHeader("Origin", "http://www.example.org");
  headers.SetHeader("Sec-WebSocket-Version", "13");
  req_->SetExtraRequestHeaders(headers);

  MockWrite writes[] = {
      MockWrite("GET / HTTP/1.1\r\n"
                "Host: www.example.org\r\n"
                "Connection: Upgrade\r\n"
                "Upgrade: websocket\r\n"
                "Origin: http://www.example.org\r\n"
                "Sec-WebSocket-Version: 13\r\n"
                "User-Agent: \r\n"
                "Accept-Encoding: gzip, deflate\r\n"
                "Accept-Language: en-us,fr\r\n"
                "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
                "Sec-WebSocket-Extensions: permessage-deflate; "
                "client_max_window_bits\r\n\r\n")};

  MockRead reads[] = {
      MockRead("HTTP/1.1 101 Switching Protocols\r\n"
               "Upgrade: websocket\r\n"
               "Connection: Upgrade\r\n"
               "Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n\r\n"),
      MockRead(ASYNC, 0)};

  StaticSocketDataProvider data(reads, writes);
  socket_factory_.AddSocketDataProvider(&data);

  auto websocket_stream_create_helper =
      std::make_unique<TestWebSocketHandshakeStreamCreateHelper>();

  req_->SetUserData(kWebSocketHandshakeUserDataKey,
                    std::move(websocket_stream_create_helper));
  req_->SetLoadFlags(LOAD_DISABLE_CACHE);
  req_->Start();
  base::RunLoop().RunUntilIdle();
  EXPECT_THAT(delegate_.request_status(), IsOk());
  EXPECT_TRUE(delegate_.response_completed());

  EXPECT_TRUE(data.AllWriteDataConsumed());
  EXPECT_TRUE(data.AllReadDataConsumed());
}

#endif  // BUILDFLAG(ENABLE_WEBSOCKETS)

bool SetAllCookies(CookieMonster* cm, const CookieList& list) {
  DCHECK(cm);
  ResultSavingCookieCallback<CookieAccessResult> callback;
  cm->SetAllCookiesAsync(list, callback.MakeCallback());
  callback.WaitUntilDone();
  return callback.result().status.IsInclude();
}

bool CreateAndSetCookie(CookieStore* cs,
                        const GURL& url,
                        const std::string& cookie_line) {
  auto cookie = CanonicalCookie::Create(
      url, cookie_line, base::Time::Now(), absl::nullopt /* server_time */,
      absl::nullopt /* cookie_partition_key */);
  if (!cookie)
    return false;
  DCHECK(cs);
  ResultSavingCookieCallback<CookieAccessResult> callback;
  cs->SetCanonicalCookieAsync(std::move(cookie), url,
                              CookieOptions::MakeAllInclusive(),
                              callback.MakeCallback());
  callback.WaitUntilDone();
  return callback.result().status.IsInclude();
}

void RunRequest(URLRequestContext* context, const GURL& url) {
  TestDelegate delegate;
  std::unique_ptr<URLRequest> request = context->CreateRequest(
      url, DEFAULT_PRIORITY, &delegate, TRAFFIC_ANNOTATION_FOR_TESTS);

  // Make this a laxly same-site context to allow setting
  // SameSite=Lax-by-default cookies.
  request->set_site_for_cookies(SiteForCookies::FromUrl(url));
  request->Start();
  delegate.RunUntilComplete();
}

}  // namespace

TEST_F(URLRequestHttpJobTest, CookieSchemeRequestSchemeHistogram) {
  base::HistogramTester histograms;
  const std::string test_histogram = "Cookie.CookieSchemeRequestScheme";

  auto context_builder = CreateTestURLRequestContextBuilder();
  context_builder->SetCookieStore(std::make_unique<CookieMonster>(
      /*store=*/nullptr, /*net_log=*/nullptr,
      /*first_party_sets_enabled=*/false));
  auto context = context_builder->Build();

  auto* cookie_store = static_cast<CookieMonster*>(context->cookie_store());

  // Secure set cookie marked as Unset source scheme.
  // Using port 7 because it fails the transaction without sending a request and
  // prevents a timeout due to the fake addresses. Because we only need the
  // headers to be generated (and thus the histogram filled) and not actually
  // sent this is acceptable.
  GURL nonsecure_url_for_unset1("http://unset1.example:7");
  GURL secure_url_for_unset1("https://unset1.example:7");

  // Normally the source scheme would be set by
  // CookieMonster::SetCanonicalCookie(), however we're using SetAllCookies() to
  // bypass the source scheme check in order to test the kUnset state which
  // would normally only happen during an existing cookie DB version upgrade.
  std::unique_ptr<CanonicalCookie> unset_cookie1 = CanonicalCookie::Create(
      secure_url_for_unset1, "NoSourceSchemeHttps=val", base::Time::Now(),
      absl::nullopt /* server_time */,
      absl::nullopt /* cookie_partition_key */);
  unset_cookie1->SetSourceScheme(net::CookieSourceScheme::kUnset);

  CookieList list1 = {*unset_cookie1};
  EXPECT_TRUE(SetAllCookies(cookie_store, list1));
  RunRequest(context.get(), nonsecure_url_for_unset1);
  histograms.ExpectBucketCount(
      test_histogram,
      URLRequestHttpJob::CookieRequestScheme::kUnsetCookieScheme, 1);
  RunRequest(context.get(), secure_url_for_unset1);
  histograms.ExpectBucketCount(
      test_histogram,
      URLRequestHttpJob::CookieRequestScheme::kUnsetCookieScheme, 2);

  // Nonsecure set cookie marked as unset source scheme.
  GURL nonsecure_url_for_unset2("http://unset2.example:7");
  GURL secure_url_for_unset2("https://unset2.example:7");

  std::unique_ptr<CanonicalCookie> unset_cookie2 = CanonicalCookie::Create(
      nonsecure_url_for_unset2, "NoSourceSchemeHttp=val", base::Time::Now(),
      absl::nullopt /* server_time */,
      absl::nullopt /* cookie_partition_key */);
  unset_cookie2->SetSourceScheme(net::CookieSourceScheme::kUnset);

  CookieList list2 = {*unset_cookie2};
  EXPECT_TRUE(SetAllCookies(cookie_store, list2));
  RunRequest(context.get(), nonsecure_url_for_unset2);
  histograms.ExpectBucketCount(
      test_histogram,
      URLRequestHttpJob::CookieRequestScheme::kUnsetCookieScheme, 3);
  RunRequest(context.get(), secure_url_for_unset2);
  histograms.ExpectBucketCount(
      test_histogram,
      URLRequestHttpJob::CookieRequestScheme::kUnsetCookieScheme, 4);

  // Secure set cookie with source scheme marked appropriately.
  GURL nonsecure_url_for_secure_set("http://secureset.example:7");
  GURL secure_url_for_secure_set("https://secureset.example:7");

  EXPECT_TRUE(CreateAndSetCookie(cookie_store, secure_url_for_secure_set,
                                 "SecureScheme=val"));
  RunRequest(context.get(), nonsecure_url_for_secure_set);
  histograms.ExpectBucketCount(
      test_histogram,
      URLRequestHttpJob::CookieRequestScheme::kSecureSetNonsecureRequest, 1);
  RunRequest(context.get(), secure_url_for_secure_set);
  histograms.ExpectBucketCount(
      test_histogram,
      URLRequestHttpJob::CookieRequestScheme::kSecureSetSecureRequest, 1);

  // Nonsecure set cookie with source scheme marked appropriately.
  GURL nonsecure_url_for_nonsecure_set("http://nonsecureset.example:7");
  GURL secure_url_for_nonsecure_set("https://nonsecureset.example:7");

  EXPECT_TRUE(CreateAndSetCookie(cookie_store, nonsecure_url_for_nonsecure_set,
                                 "NonSecureScheme=val"));
  RunRequest(context.get(), nonsecure_url_for_nonsecure_set);
  histograms.ExpectBucketCount(
      test_histogram,
      URLRequestHttpJob::CookieRequestScheme::kNonsecureSetNonsecureRequest, 1);
  RunRequest(context.get(), secure_url_for_nonsecure_set);
  histograms.ExpectBucketCount(
      test_histogram,
      URLRequestHttpJob::CookieRequestScheme::kNonsecureSetSecureRequest, 1);
}

// Test that cookies are annotated with the appropriate exclusion reason when
// privacy mode is enabled.
TEST_F(URLRequestHttpJobTest, PrivacyMode_ExclusionReason) {
  HttpTestServer test_server;
  ASSERT_TRUE(test_server.Start());

  auto context_builder = CreateTestURLRequestContextBuilder();
  context_builder->SetCookieStore(std::make_unique<CookieMonster>(
      /*store=*/nullptr, /*net_log=*/nullptr,
      /*first_party_sets_enabled=*/false));
  auto& network_delegate = *context_builder->set_network_delegate(
      std::make_unique<FilteringTestNetworkDelegate>());
  auto context = context_builder->Build();

  // Set cookies.
  {
    TestDelegate d;
    GURL test_url = test_server.GetURL(
        "/set-cookie?one=1&"
        "two=2&"
        "three=3");
    std::unique_ptr<URLRequest> req =
        CreateFirstPartyRequest(*context, test_url, &d);
    req->Start();
    d.RunUntilComplete();
  }

  // Get cookies.
  network_delegate.ResetAnnotateCookiesCalledCount();
  ASSERT_EQ(0, network_delegate.annotate_cookies_called_count());
  // We want to fetch cookies from the cookie store, so we use the
  // NetworkDelegate to override the privacy mode (rather than setting it via
  // `allow_credentials`, since that skips querying the cookie store).
  network_delegate.set_force_privacy_mode(true);
  TestDelegate d;
  std::unique_ptr<URLRequest> req = CreateFirstPartyRequest(
      *context, test_server.GetURL("/echoheader?Cookie"), &d);
  req->Start();
  d.RunUntilComplete();

  EXPECT_EQ("None", d.data_received());
  EXPECT_THAT(
      req->maybe_sent_cookies(),
      UnorderedElementsAre(
          MatchesCookieWithAccessResult(
              MatchesCookieWithName("one"),
              MatchesCookieAccessResult(
                  HasExactlyExclusionReasonsForTesting(
                      std::vector<CookieInclusionStatus::ExclusionReason>{
                          CookieInclusionStatus::EXCLUDE_USER_PREFERENCES}),
                  _, _, _)),
          MatchesCookieWithAccessResult(
              MatchesCookieWithName("two"),
              MatchesCookieAccessResult(
                  HasExactlyExclusionReasonsForTesting(
                      std::vector<CookieInclusionStatus::ExclusionReason>{
                          CookieInclusionStatus::EXCLUDE_USER_PREFERENCES}),
                  _, _, _)),
          MatchesCookieWithAccessResult(
              MatchesCookieWithName("three"),
              MatchesCookieAccessResult(
                  HasExactlyExclusionReasonsForTesting(
                      std::vector<CookieInclusionStatus::ExclusionReason>{
                          CookieInclusionStatus::EXCLUDE_USER_PREFERENCES}),
                  _, _, _))));

  EXPECT_EQ(0, network_delegate.annotate_cookies_called_count());
}

// Test that cookies are allowed to be selectively blocked by the network
// delegate.
TEST_F(URLRequestHttpJobTest, IndividuallyBlockedCookies) {
  HttpTestServer test_server;
  ASSERT_TRUE(test_server.Start());

  auto network_delegate = std::make_unique<FilteringTestNetworkDelegate>();
  network_delegate->set_block_get_cookies_by_name(true);
  network_delegate->SetCookieFilter("blocked_");
  auto context_builder = CreateTestURLRequestContextBuilder();
  context_builder->SetCookieStore(std::make_unique<CookieMonster>(
      /*store=*/nullptr, /*net_log=*/nullptr,
      /*first_party_sets_enabled=*/false));
  context_builder->set_network_delegate(std::move(network_delegate));
  auto context = context_builder->Build();

  // Set cookies.
  {
    TestDelegate d;
    GURL test_url = test_server.GetURL(
        "/set-cookie?blocked_one=1;SameSite=Lax;Secure&"
        "blocked_two=1;SameSite=Lax;Secure&"
        "allowed=1;SameSite=Lax;Secure");
    std::unique_ptr<URLRequest> req =
        CreateFirstPartyRequest(*context, test_url, &d);
    req->Start();
    d.RunUntilComplete();
  }

  // Get cookies.
  TestDelegate d;
  std::unique_ptr<URLRequest> req = CreateFirstPartyRequest(
      *context, test_server.GetURL("/echoheader?Cookie"), &d);
  req->Start();
  d.RunUntilComplete();

  EXPECT_EQ("allowed=1", d.data_received());
  EXPECT_THAT(
      req->maybe_sent_cookies(),
      UnorderedElementsAre(
          MatchesCookieWithAccessResult(
              MatchesCookieWithName("blocked_one"),
              MatchesCookieAccessResult(
                  HasExactlyExclusionReasonsForTesting(
                      std::vector<CookieInclusionStatus::ExclusionReason>{
                          CookieInclusionStatus::EXCLUDE_USER_PREFERENCES}),
                  _, _, _)),
          MatchesCookieWithAccessResult(
              MatchesCookieWithName("blocked_two"),
              MatchesCookieAccessResult(
                  HasExactlyExclusionReasonsForTesting(
                      std::vector<CookieInclusionStatus::ExclusionReason>{
                          CookieInclusionStatus::EXCLUDE_USER_PREFERENCES}),
                  _, _, _)),
          MatchesCookieWithAccessResult(
              MatchesCookieWithName("allowed"),
              MatchesCookieAccessResult(IsInclude(), _, _, _))));
}

class PartitionedCookiesURLRequestHttpJobTest
    : public URLRequestHttpJobTest,
      public testing::WithParamInterface<bool> {
 protected:
  // testing::Test
  void SetUp() override {
    if (PartitionedCookiesEnabled())
      scoped_feature_list_.InitAndEnableFeature(features::kPartitionedCookies);
    URLRequestHttpJobTest::SetUp();
  }

  bool PartitionedCookiesEnabled() { return GetParam(); }

 private:
  base::test::ScopedFeatureList scoped_feature_list_;
};

INSTANTIATE_TEST_SUITE_P(/* no label */,
                         PartitionedCookiesURLRequestHttpJobTest,
                         testing::Bool());

TEST_P(PartitionedCookiesURLRequestHttpJobTest, SetPartitionedCookie) {
  EmbeddedTestServer https_test(EmbeddedTestServer::TYPE_HTTPS);
  https_test.AddDefaultHandlers(base::FilePath());
  ASSERT_TRUE(https_test.Start());

  auto context_builder = CreateTestURLRequestContextBuilder();
  context_builder->SetCookieStore(std::make_unique<CookieMonster>(
      /*store=*/nullptr, /*net_log=*/nullptr,
      /*first_party_sets_enabled=*/false));
  auto context = context_builder->Build();

  TestDelegate delegate;
  std::unique_ptr<URLRequest> req(context->CreateRequest(
      https_test.GetURL("/set-cookie?__Host-foo=bar;SameSite=None;Secure;Path=/"
                        ";Partitioned;"),
      DEFAULT_PRIORITY, &delegate, TRAFFIC_ANNOTATION_FOR_TESTS));

  const url::Origin kTopFrameOrigin =
      url::Origin::Create(GURL("https://www.toplevelsite.com"));
  const IsolationInfo kTestIsolationInfo =
      IsolationInfo::CreateForInternalRequest(kTopFrameOrigin);

  req->set_isolation_info(kTestIsolationInfo);
  req->Start();
  ASSERT_TRUE(req->is_pending());
  delegate.RunUntilComplete();

  {  // Test request from the same top-level site.
    TestDelegate delegate;
    std::unique_ptr<URLRequest> req(context->CreateRequest(
        https_test.GetURL("/echoheader?Cookie"), DEFAULT_PRIORITY, &delegate,
        TRAFFIC_ANNOTATION_FOR_TESTS));
    req->set_isolation_info(kTestIsolationInfo);
    req->Start();
    delegate.RunUntilComplete();
    EXPECT_EQ("__Host-foo=bar", delegate.data_received());
  }

  {  // Test request from a different top-level site.
    const url::Origin kOtherTopFrameOrigin =
        url::Origin::Create(GURL("https://www.anothertoplevelsite.com"));
    const IsolationInfo kOtherTestIsolationInfo =
        IsolationInfo::CreateForInternalRequest(kOtherTopFrameOrigin);

    TestDelegate delegate;
    std::unique_ptr<URLRequest> req(context->CreateRequest(
        https_test.GetURL("/echoheader?Cookie"), DEFAULT_PRIORITY, &delegate,
        TRAFFIC_ANNOTATION_FOR_TESTS));
    req->set_isolation_info(kOtherTestIsolationInfo);
    req->Start();
    delegate.RunUntilComplete();
    if (PartitionedCookiesEnabled()) {
      EXPECT_EQ("None", delegate.data_received());
    } else {
      EXPECT_EQ("__Host-foo=bar", delegate.data_received());
    }
  }
}

// This class test partitioned cookies' interaction with First-Party Sets.
// When FPS is enabled, top-level sites that are in the same set share a cookie
// partition.
TEST_P(PartitionedCookiesURLRequestHttpJobTest,
       PartitionedCookiesAndFirstPartySets) {
  EmbeddedTestServer https_test(EmbeddedTestServer::TYPE_HTTPS);
  https_test.AddDefaultHandlers(base::FilePath());
  ASSERT_TRUE(https_test.Start());

  const GURL kOwnerURL("https://owner.com");
  const SchemefulSite kOwnerSite(kOwnerURL);
  const url::Origin kOwnerOrigin = url::Origin::Create(kOwnerURL);
  const IsolationInfo kOwnerIsolationInfo =
      IsolationInfo::CreateForInternalRequest(kOwnerOrigin);

  const GURL kMemberURL("https://member.com");
  const SchemefulSite kMemberSite(kMemberURL);
  const url::Origin kMemberOrigin = url::Origin::Create(kMemberURL);
  const IsolationInfo kMemberIsolationInfo =
      IsolationInfo::CreateForInternalRequest(kMemberOrigin);

  const GURL kNonMemberURL("https://nonmember.com");
  const url::Origin kNonMemberOrigin = url::Origin::Create(kNonMemberURL);
  const IsolationInfo kNonMemberIsolationInfo =
      IsolationInfo::CreateForInternalRequest(kNonMemberOrigin);

  base::flat_map<SchemefulSite, std::set<SchemefulSite>> first_party_sets;
  first_party_sets.insert(std::make_pair(
      kOwnerSite, std::set<SchemefulSite>({kOwnerSite, kMemberSite})));

  auto context_builder = CreateTestURLRequestContextBuilder();
  auto cookie_monster = std::make_unique<CookieMonster>(
      /*store=*/nullptr, /*net_log=*/nullptr,
      /*first_party_sets_enabled=*/false);
  auto cookie_access_delegate = std::make_unique<TestCookieAccessDelegate>();
  cookie_access_delegate->SetFirstPartySets(first_party_sets);
  cookie_monster->SetCookieAccessDelegate(std::move(cookie_access_delegate));
  context_builder->SetCookieStore(std::move(cookie_monster));
  auto context = context_builder->Build();

  TestDelegate delegate;
  std::unique_ptr<URLRequest> req(context->CreateRequest(
      https_test.GetURL("/set-cookie?__Host-foo=0;SameSite=None;Secure;Path=/"
                        ";Partitioned;"),
      DEFAULT_PRIORITY, &delegate, TRAFFIC_ANNOTATION_FOR_TESTS));

  // Start with the set's owner as the top-level site.
  req->set_isolation_info(kOwnerIsolationInfo);
  req->Start();
  ASSERT_TRUE(req->is_pending());
  delegate.RunUntilComplete();

  {
    // Test the cookie is present in a request with the same top-frame site as
    // when the cookie was set.
    TestDelegate delegate;
    std::unique_ptr<URLRequest> req(context->CreateRequest(
        https_test.GetURL("/echoheader?Cookie"), DEFAULT_PRIORITY, &delegate,
        TRAFFIC_ANNOTATION_FOR_TESTS));
    req->set_isolation_info(kOwnerIsolationInfo);
    req->Start();
    delegate.RunUntilComplete();
    EXPECT_EQ("__Host-foo=0", delegate.data_received());
  }

  {
    // Requests whose top-frame site are in the set should have access to the
    // partitioned cookie.
    TestDelegate delegate;
    std::unique_ptr<URLRequest> req(context->CreateRequest(
        https_test.GetURL("/echoheader?Cookie"), DEFAULT_PRIORITY, &delegate,
        TRAFFIC_ANNOTATION_FOR_TESTS));
    req->set_isolation_info(kMemberIsolationInfo);
    req->Start();
    delegate.RunUntilComplete();
    EXPECT_EQ("__Host-foo=0", delegate.data_received());
  }

  // Set a cookie from the member site.
  req = context->CreateRequest(
      https_test.GetURL("/set-cookie?__Host-bar=1;SameSite=None;Secure;Path=/"
                        ";Partitioned;"),
      DEFAULT_PRIORITY, &delegate, TRAFFIC_ANNOTATION_FOR_TESTS);
  req->set_isolation_info(kMemberIsolationInfo);
  req->Start();
  ASSERT_TRUE(req->is_pending());
  delegate.RunUntilComplete();

  {
    // Check request whose top-frame site is the owner site has the cookie set
    // on the member site.
    TestDelegate delegate;
    std::unique_ptr<URLRequest> req(context->CreateRequest(
        https_test.GetURL("/echoheader?Cookie"), DEFAULT_PRIORITY, &delegate,
        TRAFFIC_ANNOTATION_FOR_TESTS));
    req->set_isolation_info(kOwnerIsolationInfo);
    req->Start();
    delegate.RunUntilComplete();
    EXPECT_EQ("__Host-foo=0; __Host-bar=1", delegate.data_received());
  }

  {
    // Check that the cookies are not available when the top-frame site is not
    // in the set. If partitioned cookies are disabled, then the cookies should
    // be available.
    TestDelegate delegate;
    std::unique_ptr<URLRequest> req(context->CreateRequest(
        https_test.GetURL("/echoheader?Cookie"), DEFAULT_PRIORITY, &delegate,
        TRAFFIC_ANNOTATION_FOR_TESTS));
    req->set_isolation_info(kNonMemberIsolationInfo);
    req->Start();
    delegate.RunUntilComplete();
    if (PartitionedCookiesEnabled()) {
      EXPECT_EQ("None", delegate.data_received());
    } else {
      EXPECT_EQ("__Host-foo=0; __Host-bar=1", delegate.data_received());
    }
  }
}

TEST_P(PartitionedCookiesURLRequestHttpJobTest, PrivacyMode) {
  EmbeddedTestServer https_test(EmbeddedTestServer::TYPE_HTTPS);
  https_test.AddDefaultHandlers(base::FilePath());
  ASSERT_TRUE(https_test.Start());

  auto context_builder = CreateTestURLRequestContextBuilder();
  context_builder->SetCookieStore(
      std::make_unique<CookieMonster>(/*store=*/nullptr, /*net_log=*/nullptr,
                                      /*first_party_sets_enabled=*/false));
  auto& network_delegate = *context_builder->set_network_delegate(
      std::make_unique<FilteringTestNetworkDelegate>());
  auto context = context_builder->Build();

  const url::Origin kTopFrameOrigin =
      url::Origin::Create(GURL("https://www.toplevelsite.com"));
  const IsolationInfo kTestIsolationInfo =
      IsolationInfo::CreateForInternalRequest(kTopFrameOrigin);

  // Set an unpartitioned and partitioned cookie.
  TestDelegate delegate;
  std::unique_ptr<URLRequest> req(context->CreateRequest(
      https_test.GetURL(
          "/set-cookie?__Host-partitioned=0;SameSite=None;Secure;Path=/"
          ";Partitioned;&__Host-unpartitioned=1;SameSite=None;Secure;Path=/"),
      DEFAULT_PRIORITY, &delegate, TRAFFIC_ANNOTATION_FOR_TESTS));
  req->set_isolation_info(kTestIsolationInfo);
  req->Start();
  ASSERT_TRUE(req->is_pending());
  delegate.RunUntilComplete();

  {  // Get both cookies when privacy mode is disabled.
    TestDelegate delegate;
    std::unique_ptr<URLRequest> req(context->CreateRequest(
        https_test.GetURL("/echoheader?Cookie"), DEFAULT_PRIORITY, &delegate,
        TRAFFIC_ANNOTATION_FOR_TESTS));
    req->set_isolation_info(kTestIsolationInfo);
    req->Start();
    delegate.RunUntilComplete();
    EXPECT_EQ("__Host-partitioned=0; __Host-unpartitioned=1",
              delegate.data_received());
  }

  {  // Get cookies with privacy mode enabled and partitioned state allowed.
    network_delegate.set_force_privacy_mode(true);
    network_delegate.set_partitioned_state_allowed(true);
    TestDelegate delegate;
    std::unique_ptr<URLRequest> req(context->CreateRequest(
        https_test.GetURL("/echoheader?Cookie"), DEFAULT_PRIORITY, &delegate,
        TRAFFIC_ANNOTATION_FOR_TESTS));
    req->set_isolation_info(kTestIsolationInfo);
    req->Start();
    delegate.RunUntilComplete();
    EXPECT_EQ(PartitionedCookiesEnabled() ? "__Host-partitioned=0" : "None",
              delegate.data_received());
    auto want_exclusion_reasons =
        PartitionedCookiesEnabled()
            ? std::vector<CookieInclusionStatus::ExclusionReason>{}
            : std::vector<CookieInclusionStatus::ExclusionReason>{
                  CookieInclusionStatus::EXCLUDE_USER_PREFERENCES};
    EXPECT_THAT(
        req->maybe_sent_cookies(),
        UnorderedElementsAre(
            MatchesCookieWithAccessResult(
                MatchesCookieWithName("__Host-partitioned"),
                MatchesCookieAccessResult(HasExactlyExclusionReasonsForTesting(
                                              want_exclusion_reasons),
                                          _, _, _)),
            MatchesCookieWithAccessResult(
                MatchesCookieWithName("__Host-unpartitioned"),
                MatchesCookieAccessResult(
                    HasExactlyExclusionReasonsForTesting(
                        std::vector<CookieInclusionStatus::ExclusionReason>{
                            CookieInclusionStatus::EXCLUDE_USER_PREFERENCES}),
                    _, _, _))));
  }

  {  // Get cookies with privacy mode enabled and partitioned state is not
     // allowed.
    network_delegate.set_force_privacy_mode(true);
    network_delegate.set_partitioned_state_allowed(false);
    TestDelegate delegate;
    std::unique_ptr<URLRequest> req(context->CreateRequest(
        https_test.GetURL("/echoheader?Cookie"), DEFAULT_PRIORITY, &delegate,
        TRAFFIC_ANNOTATION_FOR_TESTS));
    req->set_isolation_info(kTestIsolationInfo);
    req->Start();
    delegate.RunUntilComplete();
    EXPECT_EQ("None", delegate.data_received());
    EXPECT_THAT(
        req->maybe_sent_cookies(),
        UnorderedElementsAre(
            MatchesCookieWithAccessResult(
                MatchesCookieWithName("__Host-partitioned"),
                MatchesCookieAccessResult(
                    HasExactlyExclusionReasonsForTesting(
                        std::vector<CookieInclusionStatus::ExclusionReason>{
                            CookieInclusionStatus::EXCLUDE_USER_PREFERENCES}),
                    _, _, _)),
            MatchesCookieWithAccessResult(
                MatchesCookieWithName("__Host-unpartitioned"),
                MatchesCookieAccessResult(
                    HasExactlyExclusionReasonsForTesting(
                        std::vector<CookieInclusionStatus::ExclusionReason>{
                            CookieInclusionStatus::EXCLUDE_USER_PREFERENCES}),
                    _, _, _))));
  }
}

// TODO(crbug.com/1296161): Remove this code when the partitioned cookies
// Origin Trial is over.
TEST_P(PartitionedCookiesURLRequestHttpJobTest,
       AddsSecCHPartitionedCookiesHeader) {
  EmbeddedTestServer https_test(EmbeddedTestServer::TYPE_HTTPS);
  https_test.AddDefaultHandlers(base::FilePath());
  ASSERT_TRUE(https_test.Start());

  auto context_builder = CreateTestURLRequestContextBuilder();
  context_builder->SetCookieStore(
      std::make_unique<CookieMonster>(/*store=*/nullptr, /*net_log=*/nullptr,
                                      /*first_party_sets_enabled=*/false));
  auto context = context_builder->Build();

  TestDelegate delegate;
  std::unique_ptr<URLRequest> req(context->CreateRequest(
      https_test.GetURL("/set-cookie?__Host-foo=bar;SameSite=None;Secure;Path=/"
                        ";Partitioned;"),
      DEFAULT_PRIORITY, &delegate, TRAFFIC_ANNOTATION_FOR_TESTS));

  const url::Origin kTopFrameOrigin =
      url::Origin::Create(GURL("https://www.toplevelsite.com"));
  const IsolationInfo kTestIsolationInfo =
      IsolationInfo::CreateForInternalRequest(kTopFrameOrigin);

  req->set_isolation_info(kTestIsolationInfo);
  req->Start();
  ASSERT_TRUE(req->is_pending());
  delegate.RunUntilComplete();

  ASSERT_TRUE(req->HasPartitionedCookie());

  {  // Test request from the same top-level site.
    TestDelegate delegate;
    std::unique_ptr<URLRequest> req(context->CreateRequest(
        https_test.GetURL("/echoheader?sec-ch-partitioned-cookies"),
        DEFAULT_PRIORITY, &delegate, TRAFFIC_ANNOTATION_FOR_TESTS));
    req->set_isolation_info(kTestIsolationInfo);
    req->Start();
    delegate.RunUntilComplete();
    if (PartitionedCookiesEnabled()) {
      EXPECT_EQ("?0", delegate.data_received());
    } else {
      EXPECT_EQ("None", delegate.data_received());
    }
  }

  {  // Test request from a different top-level site.
    const url::Origin kOtherTopFrameOrigin =
        url::Origin::Create(GURL("https://www.anothertoplevelsite.com"));
    const IsolationInfo kOtherTestIsolationInfo =
        IsolationInfo::CreateForInternalRequest(kOtherTopFrameOrigin);

    TestDelegate delegate;
    std::unique_ptr<URLRequest> req(context->CreateRequest(
        https_test.GetURL("/echoheader?sec-ch-partitioned-cookies"),
        DEFAULT_PRIORITY, &delegate, TRAFFIC_ANNOTATION_FOR_TESTS));
    req->set_isolation_info(kOtherTestIsolationInfo);
    req->Start();
    delegate.RunUntilComplete();
    EXPECT_EQ("None", delegate.data_received());
  }
}

}  // namespace net
