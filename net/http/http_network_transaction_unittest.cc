// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/http/http_network_transaction.h"

#include <math.h>  // ceil
#include <stdarg.h>
#include <stdint.h>

#include <limits>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "base/bind.h"
#include "base/compiler_specific.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/memory/ptr_util.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/run_loop.h"
#include "base/strings/string_piece.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/metrics/histogram_tester.h"
#include "base/test/scoped_feature_list.h"
#include "base/test/simple_test_clock.h"
#include "base/test/simple_test_tick_clock.h"
#include "base/test/task_environment.h"
#include "base/test/test_file_util.h"
#include "base/threading/thread_task_runner_handle.h"
#include "base/time/time.h"
#include "build/build_config.h"
#include "net/base/auth.h"
#include "net/base/chunked_upload_data_stream.h"
#include "net/base/completion_once_callback.h"
#include "net/base/elements_upload_data_stream.h"
#include "net/base/features.h"
#include "net/base/host_port_pair.h"
#include "net/base/ip_address.h"
#include "net/base/ip_endpoint.h"
#include "net/base/load_timing_info.h"
#include "net/base/load_timing_info_test_util.h"
#include "net/base/net_errors.h"
#include "net/base/privacy_mode.h"
#include "net/base/proxy_delegate.h"
#include "net/base/proxy_server.h"
#include "net/base/proxy_string_util.h"
#include "net/base/request_priority.h"
#include "net/base/schemeful_site.h"
#include "net/base/test_completion_callback.h"
#include "net/base/test_proxy_delegate.h"
#include "net/base/upload_bytes_element_reader.h"
#include "net/base/upload_file_element_reader.h"
#include "net/cert/cert_status_flags.h"
#include "net/cert/mock_cert_verifier.h"
#include "net/dns/mock_host_resolver.h"
#include "net/dns/public/secure_dns_policy.h"
#include "net/http/http_auth_challenge_tokenizer.h"
#include "net/http/http_auth_handler_digest.h"
#include "net/http/http_auth_handler_mock.h"
#include "net/http/http_auth_handler_ntlm.h"
#include "net/http/http_auth_ntlm_mechanism.h"
#include "net/http/http_auth_scheme.h"
#include "net/http/http_basic_stream.h"
#include "net/http/http_network_session.h"
#include "net/http/http_network_session_peer.h"
#include "net/http/http_proxy_connect_job.h"
#include "net/http/http_request_headers.h"
#include "net/http/http_response_info.h"
#include "net/http/http_server_properties.h"
#include "net/http/http_stream.h"
#include "net/http/http_stream_factory.h"
#include "net/http/http_transaction_test_util.h"
#include "net/log/net_log.h"
#include "net/log/net_log_event_type.h"
#include "net/log/net_log_source.h"
#include "net/log/test_net_log.h"
#include "net/log/test_net_log_util.h"
#include "net/proxy_resolution/configured_proxy_resolution_service.h"
#include "net/proxy_resolution/mock_proxy_resolver.h"
#include "net/proxy_resolution/proxy_config_service_fixed.h"
#include "net/proxy_resolution/proxy_info.h"
#include "net/proxy_resolution/proxy_resolver.h"
#include "net/proxy_resolution/proxy_resolver_factory.h"
#include "net/socket/client_socket_factory.h"
#include "net/socket/client_socket_pool.h"
#include "net/socket/client_socket_pool_manager.h"
#include "net/socket/connect_job.h"
#include "net/socket/connection_attempts.h"
#include "net/socket/mock_client_socket_pool_manager.h"
#include "net/socket/next_proto.h"
#include "net/socket/socket_tag.h"
#include "net/socket/socket_test_util.h"
#include "net/socket/socks_connect_job.h"
#include "net/socket/ssl_client_socket.h"
#include "net/spdy/spdy_session.h"
#include "net/spdy/spdy_session_pool.h"
#include "net/spdy/spdy_test_util_common.h"
#include "net/ssl/client_cert_identity_test_util.h"
#include "net/ssl/ssl_cert_request_info.h"
#include "net/ssl/ssl_config.h"
#include "net/ssl/ssl_config_service.h"
#include "net/ssl/ssl_info.h"
#include "net/ssl/ssl_private_key.h"
#include "net/ssl/test_ssl_config_service.h"
#include "net/test/cert_test_util.h"
#include "net/test/gtest_util.h"
#include "net/test/test_data_directory.h"
#include "net/test/test_with_task_environment.h"
#include "net/third_party/quiche/src/quiche/spdy/core/spdy_framer.h"
#include "net/traffic_annotation/network_traffic_annotation_test_helper.h"
#include "net/url_request/static_http_user_agent_settings.h"
#include "net/websockets/websocket_handshake_stream_base.h"
#include "net/websockets/websocket_test_util.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "testing/platform_test.h"
#include "third_party/abseil-cpp/absl/types/optional.h"
#include "url/gurl.h"
#include "url/scheme_host_port.h"
#include "url/url_constants.h"

#if defined(NTLM_PORTABLE)
#include "base/base64.h"
#include "net/ntlm/ntlm_test_data.h"
#endif

#if BUILDFLAG(ENABLE_REPORTING)
#include "net/network_error_logging/network_error_logging_service.h"
#include "net/network_error_logging/network_error_logging_test_util.h"
#include "net/reporting/reporting_cache.h"
#include "net/reporting/reporting_endpoint.h"
#include "net/reporting/reporting_header_parser.h"
#include "net/reporting/reporting_service.h"
#include "net/reporting/reporting_test_util.h"
#endif  // BUILDFLAG(ENABLE_REPORTING)

using net::test::IsError;
using net::test::IsOk;

using base::ASCIIToUTF16;

using testing::AnyOf;
using testing::ElementsAre;
using testing::IsEmpty;

//-----------------------------------------------------------------------------

namespace net {

namespace {

const std::u16string kBar(u"bar");
const std::u16string kBar2(u"bar2");
const std::u16string kBar3(u"bar3");
const std::u16string kBaz(u"baz");
const std::u16string kFirst(u"first");
const std::u16string kFoo(u"foo");
const std::u16string kFoo2(u"foo2");
const std::u16string kFoo3(u"foo3");
const std::u16string kFou(u"fou");
const std::u16string kSecond(u"second");
const std::u16string kWrongPassword(u"wrongpassword");

const char kAlternativeServiceHttpHeader[] =
    "Alt-Svc: h2=\"mail.example.org:443\"\r\n";

int GetIdleSocketCountInTransportSocketPool(HttpNetworkSession* session) {
  return session
      ->GetSocketPool(HttpNetworkSession::NORMAL_SOCKET_POOL,
                      ProxyServer::Direct())
      ->IdleSocketCount();
}

bool IsTransportSocketPoolStalled(HttpNetworkSession* session) {
  return session
      ->GetSocketPool(HttpNetworkSession::NORMAL_SOCKET_POOL,
                      ProxyServer::Direct())
      ->IsStalled();
}

// Takes in a Value created from a NetLogHttpResponseParameter, and returns
// a JSONified list of headers as a single string.  Uses single quotes instead
// of double quotes for easier comparison.
std::string GetHeaders(const base::Value& params) {
  if (!params.is_dict())
    return "";
  const base::Value::List* header_list = params.GetDict().FindList("headers");
  if (!header_list)
    return "";
  std::string headers;
  base::JSONWriter::Write(*header_list, &headers);
  base::ReplaceChars(headers, "\"", "'", &headers);
  return headers;
}

// Tests LoadTimingInfo in the case a socket is reused and no PAC script is
// used.
void TestLoadTimingReused(const LoadTimingInfo& load_timing_info) {
  EXPECT_TRUE(load_timing_info.socket_reused);
  EXPECT_NE(NetLogSource::kInvalidId, load_timing_info.socket_log_id);

  EXPECT_TRUE(load_timing_info.proxy_resolve_start.is_null());
  EXPECT_TRUE(load_timing_info.proxy_resolve_end.is_null());

  ExpectConnectTimingHasNoTimes(load_timing_info.connect_timing);
  EXPECT_FALSE(load_timing_info.send_start.is_null());

  EXPECT_LE(load_timing_info.send_start, load_timing_info.send_end);

  // Set at a higher level.
  EXPECT_TRUE(load_timing_info.request_start_time.is_null());
  EXPECT_TRUE(load_timing_info.request_start.is_null());
  EXPECT_TRUE(load_timing_info.receive_headers_end.is_null());
}

// Tests LoadTimingInfo in the case a new socket is used and no PAC script is
// used.
void TestLoadTimingNotReused(const LoadTimingInfo& load_timing_info,
                             int connect_timing_flags) {
  EXPECT_FALSE(load_timing_info.socket_reused);
  EXPECT_NE(NetLogSource::kInvalidId, load_timing_info.socket_log_id);

  EXPECT_TRUE(load_timing_info.proxy_resolve_start.is_null());
  EXPECT_TRUE(load_timing_info.proxy_resolve_end.is_null());

  ExpectConnectTimingHasTimes(load_timing_info.connect_timing,
                              connect_timing_flags);
  EXPECT_LE(load_timing_info.connect_timing.connect_end,
            load_timing_info.send_start);

  EXPECT_LE(load_timing_info.send_start, load_timing_info.send_end);

  // Set at a higher level.
  EXPECT_TRUE(load_timing_info.request_start_time.is_null());
  EXPECT_TRUE(load_timing_info.request_start.is_null());
  EXPECT_TRUE(load_timing_info.receive_headers_end.is_null());
}

// Tests LoadTimingInfo in the case a socket is reused and a PAC script is
// used.
void TestLoadTimingReusedWithPac(const LoadTimingInfo& load_timing_info) {
  EXPECT_TRUE(load_timing_info.socket_reused);
  EXPECT_NE(NetLogSource::kInvalidId, load_timing_info.socket_log_id);

  ExpectConnectTimingHasNoTimes(load_timing_info.connect_timing);

  EXPECT_FALSE(load_timing_info.proxy_resolve_start.is_null());
  EXPECT_LE(load_timing_info.proxy_resolve_start,
            load_timing_info.proxy_resolve_end);
  EXPECT_LE(load_timing_info.proxy_resolve_end,
            load_timing_info.send_start);
  EXPECT_LE(load_timing_info.send_start, load_timing_info.send_end);

  // Set at a higher level.
  EXPECT_TRUE(load_timing_info.request_start_time.is_null());
  EXPECT_TRUE(load_timing_info.request_start.is_null());
  EXPECT_TRUE(load_timing_info.receive_headers_end.is_null());
}

// Tests LoadTimingInfo in the case a new socket is used and a PAC script is
// used.
void TestLoadTimingNotReusedWithPac(const LoadTimingInfo& load_timing_info,
                                    int connect_timing_flags) {
  EXPECT_FALSE(load_timing_info.socket_reused);
  EXPECT_NE(NetLogSource::kInvalidId, load_timing_info.socket_log_id);

  EXPECT_FALSE(load_timing_info.proxy_resolve_start.is_null());
  EXPECT_LE(load_timing_info.proxy_resolve_start,
            load_timing_info.proxy_resolve_end);
  EXPECT_LE(load_timing_info.proxy_resolve_end,
            load_timing_info.connect_timing.connect_start);
  ExpectConnectTimingHasTimes(load_timing_info.connect_timing,
                              connect_timing_flags);
  EXPECT_LE(load_timing_info.connect_timing.connect_end,
            load_timing_info.send_start);

  EXPECT_LE(load_timing_info.send_start, load_timing_info.send_end);

  // Set at a higher level.
  EXPECT_TRUE(load_timing_info.request_start_time.is_null());
  EXPECT_TRUE(load_timing_info.request_start.is_null());
  EXPECT_TRUE(load_timing_info.receive_headers_end.is_null());
}

// ProxyResolver that records URLs passed to it, and that can be told what
// result to return.
class CapturingProxyResolver : public ProxyResolver {
 public:
  struct LookupInfo {
    GURL url;
    NetworkIsolationKey network_isolation_key;
  };

  CapturingProxyResolver()
      : proxy_server_(ProxyServer::SCHEME_HTTP, HostPortPair("myproxy", 80)) {}

  CapturingProxyResolver(const CapturingProxyResolver&) = delete;
  CapturingProxyResolver& operator=(const CapturingProxyResolver&) = delete;

  ~CapturingProxyResolver() override = default;

  int GetProxyForURL(const GURL& url,
                     const NetworkIsolationKey& network_isolation_key,
                     ProxyInfo* results,
                     CompletionOnceCallback callback,
                     std::unique_ptr<Request>* request,
                     const NetLogWithSource& net_log) override {
    results->UseProxyServer(proxy_server_);
    lookup_info_.push_back(LookupInfo{url, network_isolation_key});
    return OK;
  }

  // Sets whether the resolver should use direct connections, instead of a
  // proxy.
  void set_proxy_server(ProxyServer proxy_server) {
    proxy_server_ = proxy_server;
  }

  const std::vector<LookupInfo>& lookup_info() const { return lookup_info_; }

 private:
  std::vector<LookupInfo> lookup_info_;

  ProxyServer proxy_server_;
};

class CapturingProxyResolverFactory : public ProxyResolverFactory {
 public:
  explicit CapturingProxyResolverFactory(CapturingProxyResolver* resolver)
      : ProxyResolverFactory(false), resolver_(resolver) {}

  int CreateProxyResolver(const scoped_refptr<PacFileData>& pac_script,
                          std::unique_ptr<ProxyResolver>* resolver,
                          CompletionOnceCallback callback,
                          std::unique_ptr<Request>* request) override {
    *resolver = std::make_unique<ForwardingProxyResolver>(resolver_);
    return OK;
  }

 private:
  raw_ptr<ProxyResolver> resolver_;
};

std::unique_ptr<HttpNetworkSession> CreateSession(
    SpdySessionDependencies* session_deps) {
  return SpdySessionDependencies::SpdyCreateSession(session_deps);
}

class FailingProxyResolverFactory : public ProxyResolverFactory {
 public:
  FailingProxyResolverFactory() : ProxyResolverFactory(false) {}

  // ProxyResolverFactory override.
  int CreateProxyResolver(const scoped_refptr<PacFileData>& script_data,
                          std::unique_ptr<ProxyResolver>* result,
                          CompletionOnceCallback callback,
                          std::unique_ptr<Request>* request) override {
    return ERR_PAC_SCRIPT_FAILED;
  }
};

class SingleProxyDelegate : public ProxyDelegate {
 public:
  void set_proxy(const ProxyServer& proxy_server) {
    proxy_server_ = proxy_server;
  }

  // ProxyDelegate implementation:
  void OnResolveProxy(const GURL& url,
                      const std::string& method,
                      const ProxyRetryInfoMap& proxy_retry_info,
                      ProxyInfo* result) override {
    if (proxy_server_.is_valid())
      result->UseProxyServer(proxy_server_);
  }
  void OnFallback(const ProxyServer& bad_proxy, int net_error) override {}
  void OnBeforeTunnelRequest(const ProxyServer& proxy_server,
                             HttpRequestHeaders* extra_headers) override {}
  Error OnTunnelHeadersReceived(
      const ProxyServer& proxy_server,
      const HttpResponseHeaders& response_headers) override {
    return OK;
  }

 private:
  ProxyServer proxy_server_;
};

// A default minimal HttpRequestInfo for use in tests, targeting HTTP.
HttpRequestInfo DefaultRequestInfo() {
  HttpRequestInfo info;
  info.method = "GET";
  info.url = GURL("http://foo.test");
  info.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);
  return info;
}

// The default info for transports to the embedded HTTP server.
TransportInfo EmbeddedHttpServerTransportInfo() {
  TransportInfo info;
  info.endpoint = IPEndPoint(IPAddress::IPv4Localhost(), 80);
  return info;
}

}  // namespace

class HttpNetworkTransactionTest : public PlatformTest,
                                   public WithTaskEnvironment {
 public:
  ~HttpNetworkTransactionTest() override {
    // Important to restore the per-pool limit first, since the pool limit must
    // always be greater than group limit, and the tests reduce both limits.
    ClientSocketPoolManager::set_max_sockets_per_pool(
        HttpNetworkSession::NORMAL_SOCKET_POOL, old_max_pool_sockets_);
    ClientSocketPoolManager::set_max_sockets_per_group(
        HttpNetworkSession::NORMAL_SOCKET_POOL, old_max_group_sockets_);
  }

 protected:
  HttpNetworkTransactionTest()
      : WithTaskEnvironment(base::test::TaskEnvironment::TimeSource::MOCK_TIME),
        dummy_connect_job_params_(
            nullptr /* client_socket_factory */,
            nullptr /* host_resolver */,
            nullptr /* http_auth_cache */,
            nullptr /* http_auth_handler_factory */,
            nullptr /* spdy_session_pool */,
            nullptr /* quic_supported_versions */,
            nullptr /* quic_stream_factory */,
            nullptr /* proxy_delegate */,
            nullptr /* http_user_agent_settings */,
            nullptr /* ssl_client_context */,
            nullptr /* socket_performance_watcher_factory */,
            nullptr /* network_quality_estimator */,
            nullptr /* net_log */,
            nullptr /* websocket_endpoint_lock_manager */),
        ssl_(ASYNC, OK),
        old_max_group_sockets_(ClientSocketPoolManager::max_sockets_per_group(
            HttpNetworkSession::NORMAL_SOCKET_POOL)),
        old_max_pool_sockets_(ClientSocketPoolManager::max_sockets_per_pool(
            HttpNetworkSession::NORMAL_SOCKET_POOL)) {
    session_deps_.enable_http2_alternative_service = true;
  }

  struct SimpleGetHelperResult {
    int rv;
    std::string status_line;
    std::string response_data;
    int64_t total_received_bytes;
    int64_t total_sent_bytes;
    LoadTimingInfo load_timing_info;
    ConnectionAttempts connection_attempts;
    IPEndPoint remote_endpoint_after_start;
  };

  void SetUp() override {
    NetworkChangeNotifier::NotifyObserversOfIPAddressChangeForTests();
    base::RunLoop().RunUntilIdle();
    // Set an initial delay to ensure that the first call to TimeTicks::Now()
    // before incrementing the counter does not return a null value.
    FastForwardBy(base::Seconds(1));
  }

  void TearDown() override {
    NetworkChangeNotifier::NotifyObserversOfIPAddressChangeForTests();
    base::RunLoop().RunUntilIdle();
    // Empty the current queue.
    base::RunLoop().RunUntilIdle();
    PlatformTest::TearDown();
    NetworkChangeNotifier::NotifyObserversOfIPAddressChangeForTests();
    base::RunLoop().RunUntilIdle();
  }

  void Check100ResponseTiming(bool use_spdy);

  // Either |write_failure| specifies a write failure or |read_failure|
  // specifies a read failure when using a reused socket.  In either case, the
  // failure should cause the network transaction to resend the request, and the
  // other argument should be NULL.
  void KeepAliveConnectionResendRequestTest(const MockWrite* write_failure,
                                            const MockRead* read_failure);

  // Either |write_failure| specifies a write failure or |read_failure|
  // specifies a read failure when using a reused socket.  In either case, the
  // failure should cause the network transaction to resend the request, and the
  // other argument should be NULL.
  void PreconnectErrorResendRequestTest(const MockWrite* write_failure,
                                        const MockRead* read_failure,
                                        bool use_spdy,
                                        bool upload = false);

  SimpleGetHelperResult SimpleGetHelperForData(
      base::span<StaticSocketDataProvider*> providers) {
    SimpleGetHelperResult out;

    HttpRequestInfo request;
    request.method = "GET";
    request.url = GURL("http://www.example.org/");
    request.traffic_annotation =
        net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

    RecordingNetLogObserver net_log_observer;
    NetLogWithSource net_log_with_source =
        NetLogWithSource::Make(NetLogSourceType::NONE);
    session_deps_.net_log = NetLog::Get();
    std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));
    HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

    for (auto* provider : providers) {
      session_deps_.socket_factory->AddSocketDataProvider(provider);
    }

    TestCompletionCallback callback;

    EXPECT_TRUE(net_log_with_source.IsCapturing());
    int rv = trans.Start(&request, callback.callback(), net_log_with_source);
    EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

    out.rv = callback.WaitForResult();
    out.total_received_bytes = trans.GetTotalReceivedBytes();
    out.total_sent_bytes = trans.GetTotalSentBytes();

    // Even in the failure cases that use this function, connections are always
    // successfully established before the error.
    EXPECT_TRUE(trans.GetLoadTimingInfo(&out.load_timing_info));
    TestLoadTimingNotReused(out.load_timing_info, CONNECT_TIMING_HAS_DNS_TIMES);

    if (out.rv != OK)
      return out;

    const HttpResponseInfo* response = trans.GetResponseInfo();
    // Can't use ASSERT_* inside helper functions like this, so
    // return an error.
    if (!response || !response->headers) {
      out.rv = ERR_UNEXPECTED;
      return out;
    }
    out.status_line = response->headers->GetStatusLine();

    EXPECT_EQ("127.0.0.1", response->remote_endpoint.ToStringWithoutPort());
    EXPECT_EQ(80, response->remote_endpoint.port());

    bool got_endpoint =
        trans.GetRemoteEndpoint(&out.remote_endpoint_after_start);
    EXPECT_EQ(got_endpoint,
              out.remote_endpoint_after_start.address().size() > 0);

    rv = ReadTransaction(&trans, &out.response_data);
    EXPECT_THAT(rv, IsOk());

    auto entries = net_log_observer.GetEntries();
    size_t pos = ExpectLogContainsSomewhere(
        entries, 0, NetLogEventType::HTTP_TRANSACTION_SEND_REQUEST_HEADERS,
        NetLogEventPhase::NONE);
    ExpectLogContainsSomewhere(
        entries, pos, NetLogEventType::HTTP_TRANSACTION_READ_RESPONSE_HEADERS,
        NetLogEventPhase::NONE);

    EXPECT_EQ("GET / HTTP/1.1\r\n",
              GetStringValueFromParams(entries[pos], "line"));

    EXPECT_EQ("['Host: www.example.org','Connection: keep-alive']",
              GetHeaders(entries[pos].params));

    out.total_received_bytes = trans.GetTotalReceivedBytes();
    // The total number of sent bytes should not have changed.
    EXPECT_EQ(out.total_sent_bytes, trans.GetTotalSentBytes());

    out.connection_attempts = trans.GetConnectionAttempts();
    return out;
  }

  SimpleGetHelperResult SimpleGetHelper(base::span<const MockRead> data_reads) {
    MockWrite data_writes[] = {
        MockWrite("GET / HTTP/1.1\r\n"
                  "Host: www.example.org\r\n"
                  "Connection: keep-alive\r\n\r\n"),
    };

    StaticSocketDataProvider reads(data_reads, data_writes);
    StaticSocketDataProvider* data[] = {&reads};
    SimpleGetHelperResult out = SimpleGetHelperForData(data);

    EXPECT_EQ(CountWriteBytes(data_writes), out.total_sent_bytes);
    return out;
  }

  void AddSSLSocketData() {
    ssl_.next_proto = kProtoHTTP2;
    ssl_.ssl_info.cert =
        ImportCertFromFile(GetTestCertsDirectory(), "spdy_pooling.pem");
    ASSERT_TRUE(ssl_.ssl_info.cert);
    session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl_);
  }

  void ConnectStatusHelperWithExpectedStatus(const MockRead& status,
                                             int expected_status);

  void ConnectStatusHelper(const MockRead& status);

  void CheckErrorIsPassedBack(int error, IoMode mode);

  base::RepeatingClosure FastForwardByCallback(base::TimeDelta delta) {
    return base::BindRepeating(&HttpNetworkTransactionTest::FastForwardBy,
                               base::Unretained(this), delta);
  }

  const CommonConnectJobParams dummy_connect_job_params_;

  const net::NetworkIsolationKey kNetworkIsolationKey =
      NetworkIsolationKey(SchemefulSite(GURL("https://foo.test/")),
                          SchemefulSite(GURL("https://bar.test/")));

  // These clocks are defined here, even though they're only used in the
  // Reporting tests below, since they need to be destroyed after
  // |session_deps_|.
  base::SimpleTestClock clock_;
  base::SimpleTestTickClock tick_clock_;

  SpdyTestUtil spdy_util_;
  SpdySessionDependencies session_deps_;
  SSLSocketDataProvider ssl_;

  // Original socket limits.  Some tests set these.  Safest to always restore
  // them once each test has been run.
  int old_max_group_sockets_;
  int old_max_pool_sockets_;
};

namespace {

// Fill |str| with a long header list that consumes >= |size| bytes.
void FillLargeHeadersString(std::string* str, int size) {
  const char row[] =
      "SomeHeaderName: xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\r\n";
  const int sizeof_row = strlen(row);
  const int num_rows = static_cast<int>(
      ceil(static_cast<float>(size) / sizeof_row));
  const int sizeof_data = num_rows * sizeof_row;
  DCHECK(sizeof_data >= size);
  str->reserve(sizeof_data);

  for (int i = 0; i < num_rows; ++i)
    str->append(row, sizeof_row);
}

#if defined(NTLM_PORTABLE)
uint64_t MockGetMSTime() {
  // Tue, 23 May 2017 20:13:07 +0000
  return 131400439870000000;
}

// Alternative functions that eliminate randomness and dependency on the local
// host name so that the generated NTLM messages are reproducible.
void MockGenerateRandom(uint8_t* output, size_t n) {
  // This is set to 0xaa because the client challenge for testing in
  // [MS-NLMP] Section 4.2.1 is 8 bytes of 0xaa.
  memset(output, 0xaa, n);
}

std::string MockGetHostName() {
  return ntlm::test::kHostnameAscii;
}
#endif  // defined(NTLM_PORTABLE)

class CaptureGroupIdTransportSocketPool : public TransportClientSocketPool {
 public:
  explicit CaptureGroupIdTransportSocketPool(
      const CommonConnectJobParams* common_connect_job_params)
      : TransportClientSocketPool(0,
                                  0,
                                  base::TimeDelta(),
                                  ProxyServer::Direct(),
                                  false /* is_for_websockets */,
                                  common_connect_job_params) {}

  const ClientSocketPool::GroupId& last_group_id_received() const {
    return last_group_id_;
  }

  bool socket_requested() const { return socket_requested_; }

  int RequestSocket(
      const ClientSocketPool::GroupId& group_id,
      scoped_refptr<ClientSocketPool::SocketParams> socket_params,
      const absl::optional<NetworkTrafficAnnotationTag>& proxy_annotation_tag,
      RequestPriority priority,
      const SocketTag& socket_tag,
      ClientSocketPool::RespectLimits respect_limits,
      ClientSocketHandle* handle,
      CompletionOnceCallback callback,
      const ClientSocketPool::ProxyAuthCallback& proxy_auth_callback,
      const NetLogWithSource& net_log) override {
    last_group_id_ = group_id;
    socket_requested_ = true;
    return ERR_IO_PENDING;
  }
  void CancelRequest(const ClientSocketPool::GroupId& group_id,
                     ClientSocketHandle* handle,
                     bool cancel_connect_job) override {}
  void ReleaseSocket(const ClientSocketPool::GroupId& group_id,
                     std::unique_ptr<StreamSocket> socket,
                     int64_t generation) override {}
  void CloseIdleSockets(const char* net_log_reason_utf8) override {}
  void CloseIdleSocketsInGroup(const ClientSocketPool::GroupId& group_id,
                               const char* net_log_reason_utf8) override {}
  int IdleSocketCount() const override { return 0; }
  size_t IdleSocketCountInGroup(
      const ClientSocketPool::GroupId& group_id) const override {
    return 0;
  }
  LoadState GetLoadState(const ClientSocketPool::GroupId& group_id,
                         const ClientSocketHandle* handle) const override {
    return LOAD_STATE_IDLE;
  }

 private:
  ClientSocketPool::GroupId last_group_id_;
  bool socket_requested_ = false;
};

//-----------------------------------------------------------------------------

// Helper functions for validating that AuthChallengeInfo's are correctly
// configured for common cases.
bool CheckBasicServerAuth(
    const absl::optional<AuthChallengeInfo>& auth_challenge) {
  if (!auth_challenge)
    return false;
  EXPECT_FALSE(auth_challenge->is_proxy);
  EXPECT_EQ("http://www.example.org", auth_challenge->challenger.Serialize());
  EXPECT_EQ("MyRealm1", auth_challenge->realm);
  EXPECT_EQ(kBasicAuthScheme, auth_challenge->scheme);
  return true;
}

bool CheckBasicSecureServerAuth(
    const absl::optional<AuthChallengeInfo>& auth_challenge) {
  if (!auth_challenge)
    return false;
  EXPECT_FALSE(auth_challenge->is_proxy);
  EXPECT_EQ("https://www.example.org", auth_challenge->challenger.Serialize());
  EXPECT_EQ("MyRealm1", auth_challenge->realm);
  EXPECT_EQ(kBasicAuthScheme, auth_challenge->scheme);
  return true;
}

bool CheckBasicProxyAuth(
    const absl::optional<AuthChallengeInfo>& auth_challenge) {
  if (!auth_challenge)
    return false;
  EXPECT_TRUE(auth_challenge->is_proxy);
  EXPECT_EQ("http://myproxy:70", auth_challenge->challenger.Serialize());
  EXPECT_EQ("MyRealm1", auth_challenge->realm);
  EXPECT_EQ(kBasicAuthScheme, auth_challenge->scheme);
  return true;
}

bool CheckBasicSecureProxyAuth(
    const absl::optional<AuthChallengeInfo>& auth_challenge) {
  if (!auth_challenge)
    return false;
  EXPECT_TRUE(auth_challenge->is_proxy);
  EXPECT_EQ("https://myproxy:70", auth_challenge->challenger.Serialize());
  EXPECT_EQ("MyRealm1", auth_challenge->realm);
  EXPECT_EQ(kBasicAuthScheme, auth_challenge->scheme);
  return true;
}

bool CheckDigestServerAuth(
    const absl::optional<AuthChallengeInfo>& auth_challenge) {
  if (!auth_challenge)
    return false;
  EXPECT_FALSE(auth_challenge->is_proxy);
  EXPECT_EQ("http://www.example.org", auth_challenge->challenger.Serialize());
  EXPECT_EQ("digestive", auth_challenge->realm);
  EXPECT_EQ(kDigestAuthScheme, auth_challenge->scheme);
  return true;
}

#if defined(NTLM_PORTABLE)
bool CheckNTLMServerAuth(
    const absl::optional<AuthChallengeInfo>& auth_challenge) {
  if (!auth_challenge)
    return false;
  EXPECT_FALSE(auth_challenge->is_proxy);
  EXPECT_EQ("https://server", auth_challenge->challenger.Serialize());
  EXPECT_EQ(std::string(), auth_challenge->realm);
  EXPECT_EQ(kNtlmAuthScheme, auth_challenge->scheme);
  return true;
}

bool CheckNTLMProxyAuth(
    const absl::optional<AuthChallengeInfo>& auth_challenge) {
  if (!auth_challenge)
    return false;
  EXPECT_TRUE(auth_challenge->is_proxy);
  EXPECT_EQ("http://server", auth_challenge->challenger.Serialize());
  EXPECT_EQ(std::string(), auth_challenge->realm);
  EXPECT_EQ(kNtlmAuthScheme, auth_challenge->scheme);
  return true;
}
#endif  // defined(NTLM_PORTABLE)

}  // namespace

TEST_F(HttpNetworkTransactionTest, Basic) {
  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));
  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());
}

TEST_F(HttpNetworkTransactionTest, SimpleGET) {
  MockRead data_reads[] = {
    MockRead("HTTP/1.0 200 OK\r\n\r\n"),
    MockRead("hello world"),
    MockRead(SYNCHRONOUS, OK),
  };
  SimpleGetHelperResult out = SimpleGetHelper(data_reads);
  EXPECT_THAT(out.rv, IsOk());
  EXPECT_EQ("HTTP/1.0 200 OK", out.status_line);
  EXPECT_EQ("hello world", out.response_data);
  int64_t reads_size = CountReadBytes(data_reads);
  EXPECT_EQ(reads_size, out.total_received_bytes);
  EXPECT_EQ(0u, out.connection_attempts.size());

  EXPECT_FALSE(out.remote_endpoint_after_start.address().empty());
}

// Response with no status line.
TEST_F(HttpNetworkTransactionTest, SimpleGETNoHeaders) {
  MockRead data_reads[] = {
    MockRead("hello world"),
    MockRead(SYNCHRONOUS, OK),
  };
  SimpleGetHelperResult out = SimpleGetHelper(data_reads);
  EXPECT_THAT(out.rv, IsOk());
  EXPECT_EQ("HTTP/0.9 200 OK", out.status_line);
  EXPECT_EQ("hello world", out.response_data);
  int64_t reads_size = CountReadBytes(data_reads);
  EXPECT_EQ(reads_size, out.total_received_bytes);
}

// Response with no status line, and a weird port.  Should fail by default.
TEST_F(HttpNetworkTransactionTest, SimpleGETNoHeadersWeirdPort) {
  MockRead data_reads[] = {
      MockRead("hello world"), MockRead(SYNCHRONOUS, OK),
  };

  StaticSocketDataProvider data(data_reads, base::span<MockWrite>());
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));

  HttpRequestInfo request;
  auto trans =
      std::make_unique<HttpNetworkTransaction>(DEFAULT_PRIORITY, session.get());

  request.method = "GET";
  request.url = GURL("http://www.example.com:2000/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  TestCompletionCallback callback;
  int rv = trans->Start(&request, callback.callback(), NetLogWithSource());
  EXPECT_THAT(callback.GetResult(rv), IsError(ERR_INVALID_HTTP_RESPONSE));
}

// Tests that request info can be destroyed after the headers phase is complete.
TEST_F(HttpNetworkTransactionTest, SimpleGETNoReadDestroyRequestInfo) {
  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));
  auto trans =
      std::make_unique<HttpNetworkTransaction>(DEFAULT_PRIORITY, session.get());

  MockRead data_reads[] = {
      MockRead("HTTP/1.0 200 OK\r\n"), MockRead("Connection: keep-alive\r\n"),
      MockRead("Content-Length: 100\r\n\r\n"), MockRead(SYNCHRONOUS, 0),
  };
  StaticSocketDataProvider data(data_reads, base::span<MockWrite>());
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  TestCompletionCallback callback;

  {
    auto request = std::make_unique<HttpRequestInfo>();
    request->method = "GET";
    request->url = GURL("http://www.example.org/");
    request->traffic_annotation =
        net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

    int rv =
        trans->Start(request.get(), callback.callback(), NetLogWithSource());

    EXPECT_THAT(callback.GetResult(rv), IsOk());
  }  // Let request info be destroyed.

  trans.reset();
}

// Test that a failure in resolving the hostname is retrievable.
TEST_F(HttpNetworkTransactionTest, SimpleGETHostResolutionFailure) {
  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("http://www.example.org/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  auto resolver = std::make_unique<MockHostResolver>();
  resolver->rules()->AddSimulatedTimeoutFailure("www.example.org");
  session_deps_.net_log = net::NetLog::Get();
  session_deps_.host_resolver = std::move(resolver);
  std::unique_ptr<HttpNetworkSession> session = CreateSession(&session_deps_);
  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());
  TestCompletionCallback callback;

  int rv = trans.Start(&request, callback.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));
  EXPECT_THAT(callback.WaitForResult(), IsError(ERR_NAME_NOT_RESOLVED));

  const HttpResponseInfo* response = trans.GetResponseInfo();
  ASSERT_TRUE(response);
  EXPECT_THAT(response->resolve_error_info.error, IsError(ERR_DNS_TIMED_OUT));
}

// This test verifies that if the transaction fails before even connecting to a
// remote endpoint, the ConnectedCallback is never called.
TEST_F(HttpNetworkTransactionTest, ConnectedCallbackNeverCalled) {
  auto resolver = std::make_unique<MockHostResolver>();
  resolver->rules()->AddSimulatedTimeoutFailure("bar.test");
  session_deps_.host_resolver = std::move(resolver);

  ConnectedHandler connected_handler;
  std::unique_ptr<HttpNetworkSession> session = CreateSession(&session_deps_);

  auto request = DefaultRequestInfo();
  request.url = GURL("http://bar.test");

  HttpNetworkTransaction transaction(DEFAULT_PRIORITY, session.get());
  transaction.SetConnectedCallback(connected_handler.Callback());

  TestCompletionCallback callback;
  transaction.Start(&request, callback.callback(), NetLogWithSource());
  callback.WaitForResult();

  EXPECT_THAT(connected_handler.transports(), IsEmpty());
}

// This test verifies that if the ConnectedCallback returns an error, the
// entire transaction fails with that error.
TEST_F(HttpNetworkTransactionTest, ConnectedCallbackFailure) {
  // The exact error code does not matter, as long as it is the same one
  // returned by the transaction overall.
  ConnectedHandler connected_handler;
  connected_handler.set_result(ERR_NOT_IMPLEMENTED);

  std::unique_ptr<HttpNetworkSession> session = CreateSession(&session_deps_);
  auto request = DefaultRequestInfo();
  HttpNetworkTransaction transaction(DEFAULT_PRIORITY, session.get());
  transaction.SetConnectedCallback(connected_handler.Callback());

  // We never get to writing any data, but we still need a socket.
  StaticSocketDataProvider data;
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  TestCompletionCallback callback;
  EXPECT_THAT(
      transaction.Start(&request, callback.callback(), NetLogWithSource()),
      IsError(ERR_IO_PENDING));
  EXPECT_THAT(callback.WaitForResult(), IsError(ERR_NOT_IMPLEMENTED));

  EXPECT_THAT(connected_handler.transports(),
              ElementsAre(EmbeddedHttpServerTransportInfo()));
}

// This test verifies that if the ConnectedCallback returns an error, the
// underlying socket is not closed and can be reused by the next transaction.
TEST_F(HttpNetworkTransactionTest, ConnectedCallbackFailureAllowsSocketReuse) {
  ConnectedHandler connected_handler;
  connected_handler.set_result(ERR_NOT_IMPLEMENTED);

  std::unique_ptr<HttpNetworkSession> session = CreateSession(&session_deps_);
  auto request = DefaultRequestInfo();

  // A single socket should be opened and used for both transactions. Data
  // providers are matched to sockets at most once.
  MockRead data_reads[] = {
      MockRead("HTTP/1.0 200 OK\r\n"),
      MockRead("X-Test-Header: foo\r\n\r\n"),
      MockRead(SYNCHRONOUS, OK),
  };
  StaticSocketDataProvider data(data_reads, base::span<MockWrite>());
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  {
    HttpNetworkTransaction transaction(DEFAULT_PRIORITY, session.get());
    transaction.SetConnectedCallback(connected_handler.Callback());

    TestCompletionCallback callback;
    EXPECT_THAT(
        transaction.Start(&request, callback.callback(), NetLogWithSource()),
        IsError(ERR_IO_PENDING));
    EXPECT_THAT(callback.WaitForResult(), IsError(ERR_NOT_IMPLEMENTED));
  }

  // The data provider should still be linked to a socket.
  EXPECT_TRUE(data.socket());
  auto* socket = data.socket();

  {
    HttpNetworkTransaction transaction(DEFAULT_PRIORITY, session.get());

    TestCompletionCallback callback;
    EXPECT_THAT(
        transaction.Start(&request, callback.callback(), NetLogWithSource()),
        IsError(ERR_IO_PENDING));
    EXPECT_THAT(callback.WaitForResult(), IsOk());

    EXPECT_TRUE(transaction.GetResponseInfo()->headers->HasHeaderValue(
        "X-Test-Header", "foo"));

    // Still linked to the same socket.
    EXPECT_EQ(data.socket(), socket);
  }
}

// This test verifies that the ConnectedCallback is called once in the case of
// simple requests.
TEST_F(HttpNetworkTransactionTest, ConnectedCallbackCalledOnce) {
  MockRead data_reads[] = {
      MockRead("HTTP/1.0 200 OK\r\n"),
      MockRead(SYNCHRONOUS, OK),
  };
  StaticSocketDataProvider data(data_reads, base::span<MockWrite>());
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  ConnectedHandler connected_handler;
  std::unique_ptr<HttpNetworkSession> session = CreateSession(&session_deps_);
  auto request = DefaultRequestInfo();
  HttpNetworkTransaction transaction(DEFAULT_PRIORITY, session.get());
  transaction.SetConnectedCallback(connected_handler.Callback());

  TestCompletionCallback callback;
  EXPECT_THAT(
      transaction.Start(&request, callback.callback(), NetLogWithSource()),
      IsError(ERR_IO_PENDING));
  EXPECT_THAT(callback.WaitForResult(), IsOk());

  EXPECT_THAT(connected_handler.transports(),
              ElementsAre(EmbeddedHttpServerTransportInfo()));
}

// This test verifies that the ConnectedCallback is called once more per
// authentication challenge.
TEST_F(HttpNetworkTransactionTest, ConnectedCallbackCalledOnEachAuthChallenge) {
  ConnectedHandler connected_handler;
  std::unique_ptr<HttpNetworkSession> session = CreateSession(&session_deps_);
  auto request = DefaultRequestInfo();
  HttpNetworkTransaction transaction(DEFAULT_PRIORITY, session.get());
  transaction.SetConnectedCallback(connected_handler.Callback());

  // First request receives an auth challenge.
  MockRead data_reads1[] = {
      MockRead("HTTP/1.0 401 Unauthorized\r\n"),
      MockRead("WWW-Authenticate: Basic realm=\"MyRealm1\"\r\n\r\n"),
      MockRead(SYNCHRONOUS, ERR_FAILED),
  };
  StaticSocketDataProvider data1(data_reads1, base::span<MockWrite>());
  session_deps_.socket_factory->AddSocketDataProvider(&data1);

  // Second request is allowed through.
  MockRead data_reads2[] = {
      MockRead("HTTP/1.0 200 OK\r\n\r\n"),
      MockRead(SYNCHRONOUS, OK),
  };
  StaticSocketDataProvider data2(data_reads2, base::span<MockWrite>());
  session_deps_.socket_factory->AddSocketDataProvider(&data2);

  // First request, connects once.
  TestCompletionCallback callback1;
  EXPECT_THAT(
      transaction.Start(&request, callback1.callback(), NetLogWithSource()),
      IsError(ERR_IO_PENDING));
  EXPECT_THAT(callback1.WaitForResult(), IsOk());

  EXPECT_THAT(connected_handler.transports(),
              ElementsAre(EmbeddedHttpServerTransportInfo()));

  // Second request, connects again.
  TestCompletionCallback callback2;
  EXPECT_THAT(transaction.RestartWithAuth(AuthCredentials(kFoo, kBar),
                                          callback2.callback()),
              IsError(ERR_IO_PENDING));
  EXPECT_THAT(callback2.WaitForResult(), IsOk());

  EXPECT_THAT(connected_handler.transports(),
              ElementsAre(EmbeddedHttpServerTransportInfo(),
                          EmbeddedHttpServerTransportInfo()));
}

// This test verifies that the ConnectedCallback is called once more per retry.
TEST_F(HttpNetworkTransactionTest, ConnectedCallbackCalledOnEachRetry) {
  ConnectedHandler connected_handler;
  std::unique_ptr<HttpNetworkSession> session = CreateSession(&session_deps_);
  auto request = DefaultRequestInfo();
  HttpNetworkTransaction transaction(DEFAULT_PRIORITY, session.get());
  transaction.SetConnectedCallback(connected_handler.Callback());

  // First request receives a retryable error.
  MockRead data_reads1[] = {
      MockRead(SYNCHRONOUS, ERR_HTTP2_SERVER_REFUSED_STREAM),
  };
  StaticSocketDataProvider data1(data_reads1, base::span<MockWrite>());
  session_deps_.socket_factory->AddSocketDataProvider(&data1);

  // Second request is allowed through.
  MockRead data_reads2[] = {
      MockRead("HTTP/1.0 200 OK\r\n\r\n"),
      MockRead(SYNCHRONOUS, OK),
  };
  StaticSocketDataProvider data2(data_reads2, base::span<MockWrite>());
  session_deps_.socket_factory->AddSocketDataProvider(&data2);

  TestCompletionCallback callback1;
  EXPECT_THAT(
      transaction.Start(&request, callback1.callback(), NetLogWithSource()),
      IsError(ERR_IO_PENDING));
  EXPECT_THAT(callback1.WaitForResult(), IsOk());

  EXPECT_THAT(connected_handler.transports(),
              ElementsAre(EmbeddedHttpServerTransportInfo(),
                          EmbeddedHttpServerTransportInfo()));
}

TEST_F(HttpNetworkTransactionTest, ConnectedCallbackCalledAsync) {
  MockRead data_reads[] = {
      MockRead("HTTP/1.0 200 OK\r\n"),
      MockRead(SYNCHRONOUS, OK),
  };
  StaticSocketDataProvider data(data_reads, base::span<MockWrite>());
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  ConnectedHandler connected_handler;
  connected_handler.set_run_callback(true);
  std::unique_ptr<HttpNetworkSession> session = CreateSession(&session_deps_);
  auto request = DefaultRequestInfo();
  HttpNetworkTransaction transaction(DEFAULT_PRIORITY, session.get());
  transaction.SetConnectedCallback(connected_handler.Callback());

  TestCompletionCallback callback;
  EXPECT_THAT(
      transaction.Start(&request, callback.callback(), NetLogWithSource()),
      IsError(ERR_IO_PENDING));
  EXPECT_THAT(callback.WaitForResult(), IsOk());

  EXPECT_THAT(connected_handler.transports(),
              ElementsAre(EmbeddedHttpServerTransportInfo()));
}

TEST_F(HttpNetworkTransactionTest, ConnectedCallbackCalledAsyncError) {
  MockRead data_reads[] = {
      MockRead("HTTP/1.0 200 OK\r\n"),
      MockRead(SYNCHRONOUS, OK),
  };
  StaticSocketDataProvider data(data_reads, base::span<MockWrite>());
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  ConnectedHandler connected_handler;
  connected_handler.set_run_callback(true);
  connected_handler.set_result(ERR_FAILED);
  std::unique_ptr<HttpNetworkSession> session = CreateSession(&session_deps_);
  auto request = DefaultRequestInfo();
  HttpNetworkTransaction transaction(DEFAULT_PRIORITY, session.get());
  transaction.SetConnectedCallback(connected_handler.Callback());

  TestCompletionCallback callback;
  EXPECT_THAT(
      transaction.Start(&request, callback.callback(), NetLogWithSource()),
      IsError(ERR_IO_PENDING));
  EXPECT_THAT(callback.WaitForResult(), IsError(ERR_FAILED));

  EXPECT_THAT(connected_handler.transports(),
              ElementsAre(EmbeddedHttpServerTransportInfo()));
}

// Allow up to 4 bytes of junk to precede status line.
TEST_F(HttpNetworkTransactionTest, StatusLineJunk3Bytes) {
  MockRead data_reads[] = {
    MockRead("xxxHTTP/1.0 404 Not Found\nServer: blah\n\nDATA"),
    MockRead(SYNCHRONOUS, OK),
  };
  SimpleGetHelperResult out = SimpleGetHelper(data_reads);
  EXPECT_THAT(out.rv, IsOk());
  EXPECT_EQ("HTTP/1.0 404 Not Found", out.status_line);
  EXPECT_EQ("DATA", out.response_data);
  int64_t reads_size = CountReadBytes(data_reads);
  EXPECT_EQ(reads_size, out.total_received_bytes);
}

// Allow up to 4 bytes of junk to precede status line.
TEST_F(HttpNetworkTransactionTest, StatusLineJunk4Bytes) {
  MockRead data_reads[] = {
    MockRead("\n\nQJHTTP/1.0 404 Not Found\nServer: blah\n\nDATA"),
    MockRead(SYNCHRONOUS, OK),
  };
  SimpleGetHelperResult out = SimpleGetHelper(data_reads);
  EXPECT_THAT(out.rv, IsOk());
  EXPECT_EQ("HTTP/1.0 404 Not Found", out.status_line);
  EXPECT_EQ("DATA", out.response_data);
  int64_t reads_size = CountReadBytes(data_reads);
  EXPECT_EQ(reads_size, out.total_received_bytes);
}

// Beyond 4 bytes of slop and it should fail to find a status line.
TEST_F(HttpNetworkTransactionTest, StatusLineJunk5Bytes) {
  MockRead data_reads[] = {
    MockRead("xxxxxHTTP/1.1 404 Not Found\nServer: blah"),
    MockRead(SYNCHRONOUS, OK),
  };
  SimpleGetHelperResult out = SimpleGetHelper(data_reads);
  EXPECT_THAT(out.rv, IsOk());
  EXPECT_EQ("HTTP/0.9 200 OK", out.status_line);
  EXPECT_EQ("xxxxxHTTP/1.1 404 Not Found\nServer: blah", out.response_data);
  int64_t reads_size = CountReadBytes(data_reads);
  EXPECT_EQ(reads_size, out.total_received_bytes);
}

// Same as StatusLineJunk4Bytes, except the read chunks are smaller.
TEST_F(HttpNetworkTransactionTest, StatusLineJunk4Bytes_Slow) {
  MockRead data_reads[] = {
    MockRead("\n"),
    MockRead("\n"),
    MockRead("Q"),
    MockRead("J"),
    MockRead("HTTP/1.0 404 Not Found\nServer: blah\n\nDATA"),
    MockRead(SYNCHRONOUS, OK),
  };
  SimpleGetHelperResult out = SimpleGetHelper(data_reads);
  EXPECT_THAT(out.rv, IsOk());
  EXPECT_EQ("HTTP/1.0 404 Not Found", out.status_line);
  EXPECT_EQ("DATA", out.response_data);
  int64_t reads_size = CountReadBytes(data_reads);
  EXPECT_EQ(reads_size, out.total_received_bytes);
}

// Close the connection before enough bytes to have a status line.
TEST_F(HttpNetworkTransactionTest, StatusLinePartial) {
  MockRead data_reads[] = {
    MockRead("HTT"),
    MockRead(SYNCHRONOUS, OK),
  };
  SimpleGetHelperResult out = SimpleGetHelper(data_reads);
  EXPECT_THAT(out.rv, IsOk());
  EXPECT_EQ("HTTP/0.9 200 OK", out.status_line);
  EXPECT_EQ("HTT", out.response_data);
  int64_t reads_size = CountReadBytes(data_reads);
  EXPECT_EQ(reads_size, out.total_received_bytes);
}

// Simulate a 204 response, lacking a Content-Length header, sent over a
// persistent connection.  The response should still terminate since a 204
// cannot have a response body.
TEST_F(HttpNetworkTransactionTest, StopsReading204) {
  char junk[] = "junk";
  MockRead data_reads[] = {
    MockRead("HTTP/1.1 204 No Content\r\n\r\n"),
    MockRead(junk),  // Should not be read!!
    MockRead(SYNCHRONOUS, OK),
  };
  SimpleGetHelperResult out = SimpleGetHelper(data_reads);
  EXPECT_THAT(out.rv, IsOk());
  EXPECT_EQ("HTTP/1.1 204 No Content", out.status_line);
  EXPECT_EQ("", out.response_data);
  int64_t reads_size = CountReadBytes(data_reads);
  int64_t response_size = reads_size - strlen(junk);
  EXPECT_EQ(response_size, out.total_received_bytes);
}

// A simple request using chunked encoding with some extra data after.
TEST_F(HttpNetworkTransactionTest, ChunkedEncoding) {
  std::string final_chunk = "0\r\n\r\n";
  std::string extra_data = "HTTP/1.1 200 OK\r\n";
  std::string last_read = final_chunk + extra_data;
  MockRead data_reads[] = {
    MockRead("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"),
    MockRead("5\r\nHello\r\n"),
    MockRead("1\r\n"),
    MockRead(" \r\n"),
    MockRead("5\r\nworld\r\n"),
    MockRead(last_read.data()),
    MockRead(SYNCHRONOUS, OK),
  };
  SimpleGetHelperResult out = SimpleGetHelper(data_reads);
  EXPECT_THAT(out.rv, IsOk());
  EXPECT_EQ("HTTP/1.1 200 OK", out.status_line);
  EXPECT_EQ("Hello world", out.response_data);
  int64_t reads_size = CountReadBytes(data_reads);
  int64_t response_size = reads_size - extra_data.size();
  EXPECT_EQ(response_size, out.total_received_bytes);
}

// Next tests deal with http://crbug.com/56344.

TEST_F(HttpNetworkTransactionTest,
       MultipleContentLengthHeadersNoTransferEncoding) {
  MockRead data_reads[] = {
    MockRead("HTTP/1.1 200 OK\r\n"),
    MockRead("Content-Length: 10\r\n"),
    MockRead("Content-Length: 5\r\n\r\n"),
  };
  SimpleGetHelperResult out = SimpleGetHelper(data_reads);
  EXPECT_THAT(out.rv, IsError(ERR_RESPONSE_HEADERS_MULTIPLE_CONTENT_LENGTH));
}

TEST_F(HttpNetworkTransactionTest,
       DuplicateContentLengthHeadersNoTransferEncoding) {
  MockRead data_reads[] = {
    MockRead("HTTP/1.1 200 OK\r\n"),
    MockRead("Content-Length: 5\r\n"),
    MockRead("Content-Length: 5\r\n\r\n"),
    MockRead("Hello"),
  };
  SimpleGetHelperResult out = SimpleGetHelper(data_reads);
  EXPECT_THAT(out.rv, IsOk());
  EXPECT_EQ("HTTP/1.1 200 OK", out.status_line);
  EXPECT_EQ("Hello", out.response_data);
}

TEST_F(HttpNetworkTransactionTest,
       ComplexContentLengthHeadersNoTransferEncoding) {
  // More than 2 dupes.
  {
    MockRead data_reads[] = {
      MockRead("HTTP/1.1 200 OK\r\n"),
      MockRead("Content-Length: 5\r\n"),
      MockRead("Content-Length: 5\r\n"),
      MockRead("Content-Length: 5\r\n\r\n"),
      MockRead("Hello"),
    };
    SimpleGetHelperResult out = SimpleGetHelper(data_reads);
    EXPECT_THAT(out.rv, IsOk());
    EXPECT_EQ("HTTP/1.1 200 OK", out.status_line);
    EXPECT_EQ("Hello", out.response_data);
  }
  // HTTP/1.0
  {
    MockRead data_reads[] = {
      MockRead("HTTP/1.0 200 OK\r\n"),
      MockRead("Content-Length: 5\r\n"),
      MockRead("Content-Length: 5\r\n"),
      MockRead("Content-Length: 5\r\n\r\n"),
      MockRead("Hello"),
    };
    SimpleGetHelperResult out = SimpleGetHelper(data_reads);
    EXPECT_THAT(out.rv, IsOk());
    EXPECT_EQ("HTTP/1.0 200 OK", out.status_line);
    EXPECT_EQ("Hello", out.response_data);
  }
  // 2 dupes and one mismatched.
  {
    MockRead data_reads[] = {
      MockRead("HTTP/1.1 200 OK\r\n"),
      MockRead("Content-Length: 10\r\n"),
      MockRead("Content-Length: 10\r\n"),
      MockRead("Content-Length: 5\r\n\r\n"),
    };
    SimpleGetHelperResult out = SimpleGetHelper(data_reads);
    EXPECT_THAT(out.rv, IsError(ERR_RESPONSE_HEADERS_MULTIPLE_CONTENT_LENGTH));
  }
}

TEST_F(HttpNetworkTransactionTest,
       MultipleContentLengthHeadersTransferEncoding) {
  MockRead data_reads[] = {
    MockRead("HTTP/1.1 200 OK\r\n"),
    MockRead("Content-Length: 666\r\n"),
    MockRead("Content-Length: 1337\r\n"),
    MockRead("Transfer-Encoding: chunked\r\n\r\n"),
    MockRead("5\r\nHello\r\n"),
    MockRead("1\r\n"),
    MockRead(" \r\n"),
    MockRead("5\r\nworld\r\n"),
    MockRead("0\r\n\r\nHTTP/1.1 200 OK\r\n"),
    MockRead(SYNCHRONOUS, OK),
  };
  SimpleGetHelperResult out = SimpleGetHelper(data_reads);
  EXPECT_THAT(out.rv, IsOk());
  EXPECT_EQ("HTTP/1.1 200 OK", out.status_line);
  EXPECT_EQ("Hello world", out.response_data);
}

// Next tests deal with http://crbug.com/98895.

// Checks that a single Content-Disposition header results in no error.
TEST_F(HttpNetworkTransactionTest, SingleContentDispositionHeader) {
  MockRead data_reads[] = {
    MockRead("HTTP/1.1 200 OK\r\n"),
    MockRead("Content-Disposition: attachment;filename=\"salutations.txt\"r\n"),
    MockRead("Content-Length: 5\r\n\r\n"),
    MockRead("Hello"),
  };
  SimpleGetHelperResult out = SimpleGetHelper(data_reads);
  EXPECT_THAT(out.rv, IsOk());
  EXPECT_EQ("HTTP/1.1 200 OK", out.status_line);
  EXPECT_EQ("Hello", out.response_data);
}

// Checks that two identical Content-Disposition headers result in no error.
TEST_F(HttpNetworkTransactionTest, TwoIdenticalContentDispositionHeaders) {
  MockRead data_reads[] = {
    MockRead("HTTP/1.1 200 OK\r\n"),
    MockRead("Content-Disposition: attachment;filename=\"greetings.txt\"r\n"),
    MockRead("Content-Disposition: attachment;filename=\"greetings.txt\"r\n"),
    MockRead("Content-Length: 5\r\n\r\n"),
    MockRead("Hello"),
  };
  SimpleGetHelperResult out = SimpleGetHelper(data_reads);
  EXPECT_THAT(out.rv, IsOk());
  EXPECT_EQ("HTTP/1.1 200 OK", out.status_line);
  EXPECT_EQ("Hello", out.response_data);
}

// Checks that two distinct Content-Disposition headers result in an error.
TEST_F(HttpNetworkTransactionTest, TwoDistinctContentDispositionHeaders) {
  MockRead data_reads[] = {
    MockRead("HTTP/1.1 200 OK\r\n"),
    MockRead("Content-Disposition: attachment;filename=\"greetings.txt\"r\n"),
    MockRead("Content-Disposition: attachment;filename=\"hi.txt\"r\n"),
    MockRead("Content-Length: 5\r\n\r\n"),
    MockRead("Hello"),
  };
  SimpleGetHelperResult out = SimpleGetHelper(data_reads);
  EXPECT_THAT(out.rv,
              IsError(ERR_RESPONSE_HEADERS_MULTIPLE_CONTENT_DISPOSITION));
}

// Checks that two identical Location headers result in no error.
// Also tests Location header behavior.
TEST_F(HttpNetworkTransactionTest, TwoIdenticalLocationHeaders) {
  MockRead data_reads[] = {
    MockRead("HTTP/1.1 302 Redirect\r\n"),
    MockRead("Location: http://good.com/\r\n"),
    MockRead("Location: http://good.com/\r\n"),
    MockRead("Content-Length: 0\r\n\r\n"),
    MockRead(SYNCHRONOUS, OK),
  };

  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("http://redirect.com/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));
  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

  StaticSocketDataProvider data(data_reads, base::span<MockWrite>());
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  TestCompletionCallback callback;

  int rv = trans.Start(&request, callback.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  EXPECT_THAT(callback.WaitForResult(), IsOk());

  const HttpResponseInfo* response = trans.GetResponseInfo();
  ASSERT_TRUE(response);
  ASSERT_TRUE(response->headers);
  EXPECT_EQ("HTTP/1.1 302 Redirect", response->headers->GetStatusLine());
  std::string url;
  EXPECT_TRUE(response->headers->IsRedirect(&url));
  EXPECT_EQ("http://good.com/", url);
  EXPECT_TRUE(response->proxy_server.is_direct());
}

// Checks that two distinct Location headers result in an error.
TEST_F(HttpNetworkTransactionTest, TwoDistinctLocationHeaders) {
  MockRead data_reads[] = {
    MockRead("HTTP/1.1 302 Redirect\r\n"),
    MockRead("Location: http://good.com/\r\n"),
    MockRead("Location: http://evil.com/\r\n"),
    MockRead("Content-Length: 0\r\n\r\n"),
    MockRead(SYNCHRONOUS, OK),
  };
  SimpleGetHelperResult out = SimpleGetHelper(data_reads);
  EXPECT_THAT(out.rv, IsError(ERR_RESPONSE_HEADERS_MULTIPLE_LOCATION));
}

// Do a request using the HEAD method. Verify that we don't try to read the
// message body (since HEAD has none).
TEST_F(HttpNetworkTransactionTest, Head) {
  HttpRequestInfo request;
  request.method = "HEAD";
  request.url = GURL("http://www.example.org/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));
  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());
  ConnectedHandler connected_handler;
  trans.SetConnectedCallback(connected_handler.Callback());

  MockWrite data_writes1[] = {
      MockWrite("HEAD / HTTP/1.1\r\n"
                "Host: www.example.org\r\n"
                "Connection: keep-alive\r\n\r\n"),
  };
  MockRead data_reads1[] = {
      MockRead("HTTP/1.1 404 Not Found\r\n"), MockRead("Server: Blah\r\n"),
      MockRead("Content-Length: 1234\r\n\r\n"),

      // No response body because the test stops reading here.
      MockRead(SYNCHRONOUS, ERR_UNEXPECTED),
  };

  StaticSocketDataProvider data1(data_reads1, data_writes1);
  session_deps_.socket_factory->AddSocketDataProvider(&data1);

  TestCompletionCallback callback1;

  int rv = trans.Start(&request, callback1.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback1.WaitForResult();
  EXPECT_THAT(rv, IsOk());

  const HttpResponseInfo* response = trans.GetResponseInfo();
  ASSERT_TRUE(response);

  // Check that the headers got parsed.
  EXPECT_TRUE(response->headers);
  EXPECT_EQ(1234, response->headers->GetContentLength());
  EXPECT_EQ("HTTP/1.1 404 Not Found", response->headers->GetStatusLine());
  EXPECT_TRUE(response->proxy_server.is_direct());
  EXPECT_THAT(connected_handler.transports(),
              ElementsAre(EmbeddedHttpServerTransportInfo()));

  std::string server_header;
  size_t iter = 0;
  bool has_server_header = response->headers->EnumerateHeader(
      &iter, "Server", &server_header);
  EXPECT_TRUE(has_server_header);
  EXPECT_EQ("Blah", server_header);

  // Reading should give EOF right away, since there is no message body
  // (despite non-zero content-length).
  std::string response_data;
  rv = ReadTransaction(&trans, &response_data);
  EXPECT_THAT(rv, IsOk());
  EXPECT_EQ("", response_data);
}

TEST_F(HttpNetworkTransactionTest, ReuseConnection) {
  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));

  MockRead data_reads[] = {
    MockRead("HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\n"),
    MockRead("hello"),
    MockRead("HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\n"),
    MockRead("world"),
    MockRead(SYNCHRONOUS, OK),
  };
  StaticSocketDataProvider data(data_reads, base::span<MockWrite>());
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  const char* const kExpectedResponseData[] = {
    "hello", "world"
  };

  for (const auto* expected_response_data : kExpectedResponseData) {
    HttpRequestInfo request;
    request.method = "GET";
    request.url = GURL("http://www.example.org/");
    request.traffic_annotation =
        net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

    HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

    TestCompletionCallback callback;

    int rv = trans.Start(&request, callback.callback(), NetLogWithSource());
    EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

    rv = callback.WaitForResult();
    EXPECT_THAT(rv, IsOk());

    const HttpResponseInfo* response = trans.GetResponseInfo();
    ASSERT_TRUE(response);

    EXPECT_TRUE(response->headers);
    EXPECT_EQ("HTTP/1.1 200 OK", response->headers->GetStatusLine());
    EXPECT_TRUE(response->proxy_server.is_direct());

    std::string response_data;
    rv = ReadTransaction(&trans, &response_data);
    EXPECT_THAT(rv, IsOk());
    EXPECT_EQ(expected_response_data, response_data);
  }
}

TEST_F(HttpNetworkTransactionTest, Ignores100) {
  std::vector<std::unique_ptr<UploadElementReader>> element_readers;
  element_readers.push_back(
      std::make_unique<UploadBytesElementReader>("foo", 3));
  ElementsUploadDataStream upload_data_stream(std::move(element_readers), 0);

  HttpRequestInfo request;
  request.method = "POST";
  request.url = GURL("http://www.foo.com/");
  request.upload_data_stream = &upload_data_stream;
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  // Check the upload progress returned before initialization is correct.
  UploadProgress progress = request.upload_data_stream->GetUploadProgress();
  EXPECT_EQ(0u, progress.size());
  EXPECT_EQ(0u, progress.position());

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));
  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

  MockRead data_reads[] = {
    MockRead("HTTP/1.0 100 Continue\r\n\r\n"),
    MockRead("HTTP/1.0 200 OK\r\n\r\n"),
    MockRead("hello world"),
    MockRead(SYNCHRONOUS, OK),
  };
  StaticSocketDataProvider data(data_reads, base::span<MockWrite>());
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  TestCompletionCallback callback;

  int rv = trans.Start(&request, callback.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback.WaitForResult();
  EXPECT_THAT(rv, IsOk());

  const HttpResponseInfo* response = trans.GetResponseInfo();
  ASSERT_TRUE(response);

  EXPECT_TRUE(response->headers);
  EXPECT_EQ("HTTP/1.0 200 OK", response->headers->GetStatusLine());

  std::string response_data;
  rv = ReadTransaction(&trans, &response_data);
  EXPECT_THAT(rv, IsOk());
  EXPECT_EQ("hello world", response_data);
}

// This test is almost the same as Ignores100 above, but the response contains
// a 102 instead of a 100. Also, instead of HTTP/1.0 the response is
// HTTP/1.1 and the two status headers are read in one read.
TEST_F(HttpNetworkTransactionTest, Ignores1xx) {
  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("http://www.foo.com/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));
  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

  MockRead data_reads[] = {
    MockRead("HTTP/1.1 102 Unspecified status code\r\n\r\n"
             "HTTP/1.1 200 OK\r\n\r\n"),
    MockRead("hello world"),
    MockRead(SYNCHRONOUS, OK),
  };
  StaticSocketDataProvider data(data_reads, base::span<MockWrite>());
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  TestCompletionCallback callback;

  int rv = trans.Start(&request, callback.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback.WaitForResult();
  EXPECT_THAT(rv, IsOk());

  const HttpResponseInfo* response = trans.GetResponseInfo();
  ASSERT_TRUE(response);

  EXPECT_TRUE(response->headers);
  EXPECT_EQ("HTTP/1.1 200 OK", response->headers->GetStatusLine());

  std::string response_data;
  rv = ReadTransaction(&trans, &response_data);
  EXPECT_THAT(rv, IsOk());
  EXPECT_EQ("hello world", response_data);
}

TEST_F(HttpNetworkTransactionTest, LoadTimingMeasuresTimeToFirstByteForHttp) {
  static const base::TimeDelta kDelayAfterFirstByte = base::Milliseconds(10);

  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("http://www.foo.com/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  std::vector<MockWrite> data_writes = {
      MockWrite(ASYNC, 0,
                "GET / HTTP/1.1\r\n"
                "Host: www.foo.com\r\n"
                "Connection: keep-alive\r\n\r\n"),
  };

  std::vector<MockRead> data_reads = {
      // Write one byte of the status line, followed by a pause.
      MockRead(ASYNC, 1, "H"),
      MockRead(ASYNC, ERR_IO_PENDING, 2),
      MockRead(ASYNC, 3, "TTP/1.1 200 OK\r\n\r\n"),
      MockRead(ASYNC, 4, "hello world"),
      MockRead(SYNCHRONOUS, OK, 5),
  };

  SequencedSocketData data(data_reads, data_writes);
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));

  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

  TestCompletionCallback callback;

  int rv = trans.Start(&request, callback.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  data.RunUntilPaused();
  ASSERT_TRUE(data.IsPaused());
  FastForwardBy(kDelayAfterFirstByte);
  data.Resume();

  rv = callback.WaitForResult();
  EXPECT_THAT(rv, IsOk());

  const HttpResponseInfo* response = trans.GetResponseInfo();
  ASSERT_TRUE(response);

  EXPECT_TRUE(response->headers);
  EXPECT_EQ("HTTP/1.1 200 OK", response->headers->GetStatusLine());

  LoadTimingInfo load_timing_info;
  EXPECT_TRUE(trans.GetLoadTimingInfo(&load_timing_info));
  EXPECT_FALSE(load_timing_info.receive_headers_start.is_null());
  EXPECT_FALSE(load_timing_info.connect_timing.connect_end.is_null());
  // Ensure we didn't include the delay in the TTFB time.
  EXPECT_EQ(load_timing_info.receive_headers_start,
            load_timing_info.connect_timing.connect_end);
  // Ensure that the mock clock advanced at all.
  EXPECT_EQ(base::TimeTicks::Now() - load_timing_info.receive_headers_start,
            kDelayAfterFirstByte);

  std::string response_data;
  rv = ReadTransaction(&trans, &response_data);
  EXPECT_THAT(rv, IsOk());
  EXPECT_EQ("hello world", response_data);
}

// Tests that the time-to-first-byte reported in a transaction's load timing
// info uses the first response, even if 1XX/informational.
void HttpNetworkTransactionTest::Check100ResponseTiming(bool use_spdy) {
  static const base::TimeDelta kDelayAfter100Response = base::Milliseconds(10);

  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("https://www.foo.com/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  SSLSocketDataProvider ssl(ASYNC, OK);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl);

  std::vector<MockWrite> data_writes;
  std::vector<MockRead> data_reads;

  spdy::SpdySerializedFrame spdy_req(
      spdy_util_.ConstructSpdyGet(request.url.spec().c_str(), 1, LOWEST));

  spdy::Http2HeaderBlock spdy_resp1_headers;
  spdy_resp1_headers[spdy::kHttp2StatusHeader] = "100";
  spdy::SpdySerializedFrame spdy_resp1(
      spdy_util_.ConstructSpdyReply(1, spdy_resp1_headers.Clone()));
  spdy::SpdySerializedFrame spdy_resp2(
      spdy_util_.ConstructSpdyGetReply(nullptr, 0, 1));
  spdy::SpdySerializedFrame spdy_data(
      spdy_util_.ConstructSpdyDataFrame(1, "hello world", true));

  if (use_spdy) {
    ssl.next_proto = kProtoHTTP2;

    data_writes = {CreateMockWrite(spdy_req, 0)};

    data_reads = {
        CreateMockRead(spdy_resp1, 1), MockRead(ASYNC, ERR_IO_PENDING, 2),
        CreateMockRead(spdy_resp2, 3), CreateMockRead(spdy_data, 4),
        MockRead(SYNCHRONOUS, OK, 5),
    };
  } else {
    data_writes = {
        MockWrite(ASYNC, 0,
                  "GET / HTTP/1.1\r\n"
                  "Host: www.foo.com\r\n"
                  "Connection: keep-alive\r\n\r\n"),
    };

    data_reads = {
        MockRead(ASYNC, 1, "HTTP/1.1 100 Continue\r\n\r\n"),
        MockRead(ASYNC, ERR_IO_PENDING, 2),

        MockRead(ASYNC, 3, "HTTP/1.1 200 OK\r\n\r\n"),
        MockRead(ASYNC, 4, "hello world"),
        MockRead(SYNCHRONOUS, OK, 5),
    };
  }

  SequencedSocketData data(data_reads, data_writes);
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));

  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

  TestCompletionCallback callback;

  int rv = trans.Start(&request, callback.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  data.RunUntilPaused();
  // We should now have parsed the 100 response and hit ERR_IO_PENDING. Insert
  // the delay before parsing the 200 response.
  ASSERT_TRUE(data.IsPaused());
  FastForwardBy(kDelayAfter100Response);
  data.Resume();

  rv = callback.WaitForResult();
  EXPECT_THAT(rv, IsOk());

  const HttpResponseInfo* response = trans.GetResponseInfo();
  ASSERT_TRUE(response);

  LoadTimingInfo load_timing_info;
  EXPECT_TRUE(trans.GetLoadTimingInfo(&load_timing_info));
  EXPECT_FALSE(load_timing_info.receive_headers_start.is_null());
  EXPECT_FALSE(load_timing_info.connect_timing.connect_end.is_null());
  // Ensure we didn't include the delay in the TTFB time.
  EXPECT_EQ(load_timing_info.receive_headers_start,
            load_timing_info.connect_timing.connect_end);
  // Ensure that the mock clock advanced at all.
  EXPECT_EQ(base::TimeTicks::Now() - load_timing_info.receive_headers_start,
            kDelayAfter100Response);

  std::string response_data;
  rv = ReadTransaction(&trans, &response_data);
  EXPECT_THAT(rv, IsOk());
  EXPECT_EQ("hello world", response_data);
}

TEST_F(HttpNetworkTransactionTest, MeasuresTimeToFirst100ResponseForHttp) {
  Check100ResponseTiming(false /* use_spdy */);
}

TEST_F(HttpNetworkTransactionTest, MeasuresTimeToFirst100ResponseForSpdy) {
  Check100ResponseTiming(true /* use_spdy */);
}

TEST_F(HttpNetworkTransactionTest, Incomplete100ThenEOF) {
  HttpRequestInfo request;
  request.method = "POST";
  request.url = GURL("http://www.foo.com/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));
  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

  MockRead data_reads[] = {
    MockRead(SYNCHRONOUS, "HTTP/1.0 100 Continue\r\n"),
    MockRead(ASYNC, 0),
  };
  StaticSocketDataProvider data(data_reads, base::span<MockWrite>());
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  TestCompletionCallback callback;

  int rv = trans.Start(&request, callback.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback.WaitForResult();
  EXPECT_THAT(rv, IsOk());

  std::string response_data;
  rv = ReadTransaction(&trans, &response_data);
  EXPECT_THAT(rv, IsOk());
  EXPECT_EQ("", response_data);
}

TEST_F(HttpNetworkTransactionTest, EmptyResponse) {
  HttpRequestInfo request;
  request.method = "POST";
  request.url = GURL("http://www.foo.com/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));
  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

  MockRead data_reads[] = {
    MockRead(ASYNC, 0),
  };
  StaticSocketDataProvider data(data_reads, base::span<MockWrite>());
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  TestCompletionCallback callback;

  int rv = trans.Start(&request, callback.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback.WaitForResult();
  EXPECT_THAT(rv, IsError(ERR_EMPTY_RESPONSE));
}

void HttpNetworkTransactionTest::KeepAliveConnectionResendRequestTest(
    const MockWrite* write_failure,
    const MockRead* read_failure) {
  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("http://www.foo.com/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  session_deps_.net_log = net::NetLog::Get();
  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));

  // Written data for successfully sending both requests.
  MockWrite data1_writes[] = {
    MockWrite("GET / HTTP/1.1\r\n"
              "Host: www.foo.com\r\n"
              "Connection: keep-alive\r\n\r\n"),
    MockWrite("GET / HTTP/1.1\r\n"
              "Host: www.foo.com\r\n"
              "Connection: keep-alive\r\n\r\n")
  };

  // Read results for the first request.
  MockRead data1_reads[] = {
    MockRead("HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\n"),
    MockRead("hello"),
    MockRead(ASYNC, OK),
  };

  if (write_failure) {
    ASSERT_FALSE(read_failure);
    data1_writes[1] = *write_failure;
  } else {
    ASSERT_TRUE(read_failure);
    data1_reads[2] = *read_failure;
  }

  StaticSocketDataProvider data1(data1_reads, data1_writes);
  session_deps_.socket_factory->AddSocketDataProvider(&data1);

  MockRead data2_reads[] = {
    MockRead("HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\n"),
    MockRead("world"),
    MockRead(ASYNC, OK),
  };
  StaticSocketDataProvider data2(data2_reads, base::span<MockWrite>());
  session_deps_.socket_factory->AddSocketDataProvider(&data2);

  const char* const kExpectedResponseData[] = {
    "hello", "world"
  };

  uint32_t first_socket_log_id = NetLogSource::kInvalidId;
  for (int i = 0; i < 2; ++i) {
    TestCompletionCallback callback;

    HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

    int rv = trans.Start(&request, callback.callback(), NetLogWithSource());
    EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

    rv = callback.WaitForResult();
    EXPECT_THAT(rv, IsOk());

    LoadTimingInfo load_timing_info;
    EXPECT_TRUE(trans.GetLoadTimingInfo(&load_timing_info));
    TestLoadTimingNotReused(load_timing_info, CONNECT_TIMING_HAS_DNS_TIMES);
    if (i == 0) {
      first_socket_log_id = load_timing_info.socket_log_id;
    } else {
      // The second request should be using a new socket.
      EXPECT_NE(first_socket_log_id, load_timing_info.socket_log_id);
    }

    const HttpResponseInfo* response = trans.GetResponseInfo();
    ASSERT_TRUE(response);

    EXPECT_TRUE(response->headers);
    EXPECT_TRUE(response->proxy_server.is_direct());
    EXPECT_EQ("HTTP/1.1 200 OK", response->headers->GetStatusLine());

    std::string response_data;
    rv = ReadTransaction(&trans, &response_data);
    EXPECT_THAT(rv, IsOk());
    EXPECT_EQ(kExpectedResponseData[i], response_data);
  }
}

void HttpNetworkTransactionTest::PreconnectErrorResendRequestTest(
    const MockWrite* write_failure,
    const MockRead* read_failure,
    bool use_spdy,
    bool chunked_upload) {
  SpdyTestUtil spdy_util;
  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("https://www.foo.com/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  const char upload_data[] = "foobar";
  ChunkedUploadDataStream upload_data_stream(0);
  if (chunked_upload) {
    request.method = "POST";
    upload_data_stream.AppendData(upload_data, std::size(upload_data) - 1,
                                  true);
    request.upload_data_stream = &upload_data_stream;
  }

  session_deps_.net_log = net::NetLog::Get();
  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));

  SSLSocketDataProvider ssl1(ASYNC, OK);
  SSLSocketDataProvider ssl2(ASYNC, OK);
  if (use_spdy) {
    ssl1.next_proto = kProtoHTTP2;
    ssl2.next_proto = kProtoHTTP2;
  }
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl1);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl2);

  // SPDY versions of the request and response.

  spdy::Http2HeaderBlock spdy_post_header_block;
  spdy_post_header_block[spdy::kHttp2MethodHeader] = "POST";
  spdy_util.AddUrlToHeaderBlock(request.url.spec(), &spdy_post_header_block);
  spdy::SpdySerializedFrame spdy_request(
      chunked_upload
          ? spdy_util.ConstructSpdyHeaders(1, std::move(spdy_post_header_block),
                                           DEFAULT_PRIORITY, false)
          : spdy_util.ConstructSpdyGet(request.url.spec().c_str(), 1,
                                       DEFAULT_PRIORITY));

  spdy::SpdySerializedFrame spdy_request_body(
      spdy_util.ConstructSpdyDataFrame(1, "foobar", true));
  spdy::SpdySerializedFrame spdy_response(
      spdy_util.ConstructSpdyGetReply(nullptr, 0, 1));
  spdy::SpdySerializedFrame spdy_data(
      spdy_util.ConstructSpdyDataFrame(1, "hello", true));

  // HTTP/1.1 versions of the request and response.
  const std::string http_request =
      std::string(chunked_upload ? "POST" : "GET") +
      " / HTTP/1.1\r\n"
      "Host: www.foo.com\r\n"
      "Connection: keep-alive\r\n" +
      (chunked_upload ? "Transfer-Encoding: chunked\r\n\r\n" : "\r\n");
  const char* kHttpRequest = http_request.c_str();
  const char kHttpResponse[] = "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\n";
  const char kHttpData[] = "hello";

  std::vector<MockRead> data1_reads;
  std::vector<MockWrite> data1_writes;
  if (write_failure) {
    ASSERT_FALSE(read_failure);
    data1_writes.push_back(*write_failure);
    data1_reads.emplace_back(ASYNC, OK);
  } else {
    ASSERT_TRUE(read_failure);
    if (use_spdy) {
      data1_writes.push_back(CreateMockWrite(spdy_request));
      if (chunked_upload)
        data1_writes.push_back(CreateMockWrite(spdy_request_body));
    } else {
      data1_writes.emplace_back(kHttpRequest);
      if (chunked_upload) {
        data1_writes.emplace_back("6\r\nfoobar\r\n");
        data1_writes.emplace_back("0\r\n\r\n");
      }
    }
    data1_reads.push_back(*read_failure);
  }

  StaticSocketDataProvider data1(data1_reads, data1_writes);
  session_deps_.socket_factory->AddSocketDataProvider(&data1);

  std::vector<MockRead> data2_reads;
  std::vector<MockWrite> data2_writes;

  if (use_spdy) {
    int seq = 0;
    data2_writes.push_back(CreateMockWrite(spdy_request, seq++, ASYNC));
    if (chunked_upload)
      data2_writes.push_back(CreateMockWrite(spdy_request_body, seq++, ASYNC));
    data2_reads.push_back(CreateMockRead(spdy_response, seq++, ASYNC));
    data2_reads.push_back(CreateMockRead(spdy_data, seq++, ASYNC));
    data2_reads.emplace_back(ASYNC, OK, seq++);
  } else {
    int seq = 0;
    data2_writes.emplace_back(ASYNC, kHttpRequest, strlen(kHttpRequest), seq++);
    if (chunked_upload) {
      data2_writes.emplace_back(ASYNC, "6\r\nfoobar\r\n", 11, seq++);
      data2_writes.emplace_back(ASYNC, "0\r\n\r\n", 5, seq++);
    }
    data2_reads.emplace_back(ASYNC, kHttpResponse, strlen(kHttpResponse),
                             seq++);
    data2_reads.emplace_back(ASYNC, kHttpData, strlen(kHttpData), seq++);
    data2_reads.emplace_back(ASYNC, OK, seq++);
  }
  SequencedSocketData data2(data2_reads, data2_writes);
  session_deps_.socket_factory->AddSocketDataProvider(&data2);

  // Preconnect a socket.
  session->http_stream_factory()->PreconnectStreams(1, request);
  // Wait for the preconnect to complete.
  // TODO(davidben): Some way to wait for an idle socket count might be handy.
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(1, GetIdleSocketCountInTransportSocketPool(session.get()));

  // Make the request.
  TestCompletionCallback callback;

  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

  int rv = trans.Start(&request, callback.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback.WaitForResult();
  EXPECT_THAT(rv, IsOk());

  LoadTimingInfo load_timing_info;
  EXPECT_TRUE(trans.GetLoadTimingInfo(&load_timing_info));
  TestLoadTimingNotReused(
      load_timing_info,
      CONNECT_TIMING_HAS_DNS_TIMES|CONNECT_TIMING_HAS_SSL_TIMES);

  const HttpResponseInfo* response = trans.GetResponseInfo();
  ASSERT_TRUE(response);

  EXPECT_TRUE(response->headers);
  if (response->was_fetched_via_spdy) {
    EXPECT_EQ("HTTP/1.1 200", response->headers->GetStatusLine());
  } else {
    EXPECT_EQ("HTTP/1.1 200 OK", response->headers->GetStatusLine());
  }

  std::string response_data;
  rv = ReadTransaction(&trans, &response_data);
  EXPECT_THAT(rv, IsOk());
  EXPECT_EQ(kHttpData, response_data);
}

// Test that we do not retry indefinitely when a server sends an error like
// ERR_HTTP2_PING_FAILED, ERR_HTTP2_SERVER_REFUSED_STREAM,
// ERR_QUIC_HANDSHAKE_FAILED or ERR_QUIC_PROTOCOL_ERROR.
TEST_F(HttpNetworkTransactionTest, FiniteRetriesOnIOError) {
  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("https://www.foo.com/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  // Check whether we give up after the third try.

  // Construct an HTTP2 request and a "Go away" response.
  spdy::SpdySerializedFrame spdy_request(spdy_util_.ConstructSpdyGet(
      request.url.spec().c_str(), 1, DEFAULT_PRIORITY));
  spdy::SpdySerializedFrame spdy_response_go_away(
      spdy_util_.ConstructSpdyGoAway(0));
  MockRead data_read1[] = {CreateMockRead(spdy_response_go_away)};
  MockWrite data_write[] = {CreateMockWrite(spdy_request, 0)};

  // Three go away responses.
  StaticSocketDataProvider data1(data_read1, data_write);
  StaticSocketDataProvider data2(data_read1, data_write);
  StaticSocketDataProvider data3(data_read1, data_write);

  session_deps_.socket_factory->AddSocketDataProvider(&data1);
  AddSSLSocketData();
  session_deps_.socket_factory->AddSocketDataProvider(&data2);
  AddSSLSocketData();
  session_deps_.socket_factory->AddSocketDataProvider(&data3);
  AddSSLSocketData();

  TestCompletionCallback callback;
  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));
  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

  int rv = trans.Start(&request, callback.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback.WaitForResult();
  EXPECT_THAT(rv, IsError(ERR_HTTP2_SERVER_REFUSED_STREAM));
}

TEST_F(HttpNetworkTransactionTest, RetryTwiceOnIOError) {
  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("https://www.foo.com/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  // Check whether we try atleast thrice before giving up.

  // Construct an HTTP2 request and a "Go away" response.
  spdy::SpdySerializedFrame spdy_request(spdy_util_.ConstructSpdyGet(
      request.url.spec().c_str(), 1, DEFAULT_PRIORITY));
  spdy::SpdySerializedFrame spdy_response_go_away(
      spdy_util_.ConstructSpdyGoAway(0));
  MockRead data_read1[] = {CreateMockRead(spdy_response_go_away)};
  MockWrite data_write[] = {CreateMockWrite(spdy_request, 0)};

  // Construct a non error HTTP2 response.
  spdy::SpdySerializedFrame spdy_response_no_error(
      spdy_util_.ConstructSpdyGetReply(nullptr, 0, 1));
  spdy::SpdySerializedFrame spdy_data(
      spdy_util_.ConstructSpdyDataFrame(1, true));
  MockRead data_read2[] = {CreateMockRead(spdy_response_no_error, 1),
                           CreateMockRead(spdy_data, 2)};

  // Two error responses.
  StaticSocketDataProvider data1(data_read1, data_write);
  StaticSocketDataProvider data2(data_read1, data_write);
  // Followed by a success response.
  SequencedSocketData data3(data_read2, data_write);

  session_deps_.socket_factory->AddSocketDataProvider(&data1);
  AddSSLSocketData();
  session_deps_.socket_factory->AddSocketDataProvider(&data2);
  AddSSLSocketData();
  session_deps_.socket_factory->AddSocketDataProvider(&data3);
  AddSSLSocketData();

  TestCompletionCallback callback;
  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));
  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

  int rv = trans.Start(&request, callback.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback.WaitForResult();
  EXPECT_THAT(rv, IsOk());
}

TEST_F(HttpNetworkTransactionTest, KeepAliveConnectionNotConnectedOnWrite) {
  MockWrite write_failure(ASYNC, ERR_SOCKET_NOT_CONNECTED);
  KeepAliveConnectionResendRequestTest(&write_failure, nullptr);
}

TEST_F(HttpNetworkTransactionTest, KeepAliveConnectionReset) {
  MockRead read_failure(ASYNC, ERR_CONNECTION_RESET);
  KeepAliveConnectionResendRequestTest(nullptr, &read_failure);
}

TEST_F(HttpNetworkTransactionTest, KeepAliveConnectionEOF) {
  MockRead read_failure(SYNCHRONOUS, OK);  // EOF
  KeepAliveConnectionResendRequestTest(nullptr, &read_failure);
}

// Make sure that on a 408 response (Request Timeout), the request is retried,
// if the socket was a reused keep alive socket.
TEST_F(HttpNetworkTransactionTest, KeepAlive408) {
  MockRead read_failure(SYNCHRONOUS,
                        "HTTP/1.1 408 Request Timeout\r\n"
                        "Connection: Keep-Alive\r\n"
                        "Content-Length: 6\r\n\r\n"
                        "Pickle");
  KeepAliveConnectionResendRequestTest(nullptr, &read_failure);
}

TEST_F(HttpNetworkTransactionTest, PreconnectErrorNotConnectedOnWrite) {
  MockWrite write_failure(ASYNC, ERR_SOCKET_NOT_CONNECTED);
  PreconnectErrorResendRequestTest(&write_failure, nullptr,
                                   false /* use_spdy */);
  PreconnectErrorResendRequestTest(
      &write_failure, nullptr, false /* use_spdy */, true /* chunked_upload */);
}

TEST_F(HttpNetworkTransactionTest, PreconnectErrorReset) {
  MockRead read_failure(ASYNC, ERR_CONNECTION_RESET);
  PreconnectErrorResendRequestTest(nullptr, &read_failure,
                                   false /* use_spdy */);
  PreconnectErrorResendRequestTest(nullptr, &read_failure, false /* use_spdy */,
                                   true /* chunked_upload */);
}

TEST_F(HttpNetworkTransactionTest, PreconnectErrorEOF) {
  MockRead read_failure(SYNCHRONOUS, OK);  // EOF
  PreconnectErrorResendRequestTest(nullptr, &read_failure,
                                   false /* use_spdy */);
  PreconnectErrorResendRequestTest(nullptr, &read_failure, false /* use_spdy */,
                                   true /* chunked_upload */);
}

TEST_F(HttpNetworkTransactionTest, PreconnectErrorAsyncEOF) {
  MockRead read_failure(ASYNC, OK);  // EOF
  PreconnectErrorResendRequestTest(nullptr, &read_failure,
                                   false /* use_spdy */);
  PreconnectErrorResendRequestTest(nullptr, &read_failure, false /* use_spdy */,
                                   true /* chunked_upload */);
}

// Make sure that on a 408 response (Request Timeout), the request is retried,
// if the socket was a preconnected (UNUSED_IDLE) socket.
TEST_F(HttpNetworkTransactionTest, RetryOnIdle408) {
  MockRead read_failure(SYNCHRONOUS,
                        "HTTP/1.1 408 Request Timeout\r\n"
                        "Connection: Keep-Alive\r\n"
                        "Content-Length: 6\r\n\r\n"
                        "Pickle");
  KeepAliveConnectionResendRequestTest(nullptr, &read_failure);
  PreconnectErrorResendRequestTest(nullptr, &read_failure,
                                   false /* use_spdy */);
  PreconnectErrorResendRequestTest(nullptr, &read_failure, false /* use_spdy */,
                                   true /* chunked_upload */);
}

TEST_F(HttpNetworkTransactionTest, SpdyPreconnectErrorNotConnectedOnWrite) {
  MockWrite write_failure(ASYNC, ERR_SOCKET_NOT_CONNECTED);
  PreconnectErrorResendRequestTest(&write_failure, nullptr,
                                   true /* use_spdy */);
  PreconnectErrorResendRequestTest(&write_failure, nullptr, true /* use_spdy */,
                                   true /* chunked_upload */);
}

TEST_F(HttpNetworkTransactionTest, SpdyPreconnectErrorReset) {
  MockRead read_failure(ASYNC, ERR_CONNECTION_RESET);
  PreconnectErrorResendRequestTest(nullptr, &read_failure, true /* use_spdy */);
  PreconnectErrorResendRequestTest(nullptr, &read_failure, true /* use_spdy */,
                                   true /* chunked_upload */);
}

TEST_F(HttpNetworkTransactionTest, SpdyPreconnectErrorEOF) {
  MockRead read_failure(SYNCHRONOUS, OK);  // EOF
  PreconnectErrorResendRequestTest(nullptr, &read_failure, true /* use_spdy */);
  PreconnectErrorResendRequestTest(nullptr, &read_failure, true /* use_spdy */,
                                   true /* chunked_upload */);
}

TEST_F(HttpNetworkTransactionTest, SpdyPreconnectErrorAsyncEOF) {
  MockRead read_failure(ASYNC, OK);  // EOF
  PreconnectErrorResendRequestTest(nullptr, &read_failure, true /* use_spdy */);
  PreconnectErrorResendRequestTest(nullptr, &read_failure, true /* use_spdy */,
                                   true /* chunked_upload */);
}

TEST_F(HttpNetworkTransactionTest, NonKeepAliveConnectionReset) {
  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("http://www.example.org/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));
  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

  MockRead data_reads[] = {
    MockRead(ASYNC, ERR_CONNECTION_RESET),
    MockRead("HTTP/1.0 200 OK\r\n\r\n"),  // Should not be used
    MockRead("hello world"),
    MockRead(SYNCHRONOUS, OK),
  };
  StaticSocketDataProvider data(data_reads, base::span<MockWrite>());
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  TestCompletionCallback callback;

  int rv = trans.Start(&request, callback.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback.WaitForResult();
  EXPECT_THAT(rv, IsError(ERR_CONNECTION_RESET));

  IPEndPoint endpoint;
  EXPECT_TRUE(trans.GetRemoteEndpoint(&endpoint));
  EXPECT_LT(0u, endpoint.address().size());
}

// What do various browsers do when the server closes a non-keepalive
// connection without sending any response header or body?
//
// IE7: error page
// Safari 3.1.2 (Windows): error page
// Firefox 3.0.1: blank page
// Opera 9.52: after five attempts, blank page
// Us with WinHTTP: error page (ERR_INVALID_RESPONSE)
// Us: error page (EMPTY_RESPONSE)
TEST_F(HttpNetworkTransactionTest, NonKeepAliveConnectionEOF) {
  MockRead data_reads[] = {
    MockRead(SYNCHRONOUS, OK),  // EOF
    MockRead("HTTP/1.0 200 OK\r\n\r\n"),  // Should not be used
    MockRead("hello world"),
    MockRead(SYNCHRONOUS, OK),
  };
  SimpleGetHelperResult out = SimpleGetHelper(data_reads);
  EXPECT_THAT(out.rv, IsError(ERR_EMPTY_RESPONSE));
}

// Next 2 cases (KeepAliveEarlyClose and KeepAliveEarlyClose2) are regression
// tests. There was a bug causing HttpNetworkTransaction to hang in the
// destructor in such situations.
// See http://crbug.com/154712 and http://crbug.com/156609.
TEST_F(HttpNetworkTransactionTest, KeepAliveEarlyClose) {
  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("http://www.example.org/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));
  auto trans =
      std::make_unique<HttpNetworkTransaction>(DEFAULT_PRIORITY, session.get());

  MockRead data_reads[] = {
    MockRead("HTTP/1.0 200 OK\r\n"),
    MockRead("Connection: keep-alive\r\n"),
    MockRead("Content-Length: 100\r\n\r\n"),
    MockRead("hello"),
    MockRead(SYNCHRONOUS, 0),
  };
  StaticSocketDataProvider data(data_reads, base::span<MockWrite>());
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  TestCompletionCallback callback;

  int rv = trans->Start(&request, callback.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback.WaitForResult();
  EXPECT_THAT(rv, IsOk());

  scoped_refptr<IOBufferWithSize> io_buf =
      base::MakeRefCounted<IOBufferWithSize>(100);
  rv = trans->Read(io_buf.get(), io_buf->size(), callback.callback());
  if (rv == ERR_IO_PENDING)
    rv = callback.WaitForResult();
  EXPECT_EQ(5, rv);
  rv = trans->Read(io_buf.get(), io_buf->size(), callback.callback());
  EXPECT_THAT(rv, IsError(ERR_CONTENT_LENGTH_MISMATCH));

  trans.reset();
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(0, GetIdleSocketCountInTransportSocketPool(session.get()));
}

TEST_F(HttpNetworkTransactionTest, KeepAliveEarlyClose2) {
  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("http://www.example.org/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));
  auto trans =
      std::make_unique<HttpNetworkTransaction>(DEFAULT_PRIORITY, session.get());

  MockRead data_reads[] = {
    MockRead("HTTP/1.0 200 OK\r\n"),
    MockRead("Connection: keep-alive\r\n"),
    MockRead("Content-Length: 100\r\n\r\n"),
    MockRead(SYNCHRONOUS, 0),
  };
  StaticSocketDataProvider data(data_reads, base::span<MockWrite>());
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  TestCompletionCallback callback;

  int rv = trans->Start(&request, callback.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback.WaitForResult();
  EXPECT_THAT(rv, IsOk());

  scoped_refptr<IOBufferWithSize> io_buf(
      base::MakeRefCounted<IOBufferWithSize>(100));
  rv = trans->Read(io_buf.get(), io_buf->size(), callback.callback());
  if (rv == ERR_IO_PENDING)
    rv = callback.WaitForResult();
  EXPECT_THAT(rv, IsError(ERR_CONTENT_LENGTH_MISMATCH));

  trans.reset();
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(0, GetIdleSocketCountInTransportSocketPool(session.get()));
}

// Test that we correctly reuse a keep-alive connection after not explicitly
// reading the body.
TEST_F(HttpNetworkTransactionTest, KeepAliveAfterUnreadBody) {
  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("http://www.foo.com/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  session_deps_.net_log = net::NetLog::Get();
  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));

  const char* request_data =
      "GET / HTTP/1.1\r\n"
      "Host: www.foo.com\r\n"
      "Connection: keep-alive\r\n\r\n";
  MockWrite data_writes[] = {
      MockWrite(ASYNC, 0, request_data),  MockWrite(ASYNC, 2, request_data),
      MockWrite(ASYNC, 4, request_data),  MockWrite(ASYNC, 6, request_data),
      MockWrite(ASYNC, 8, request_data),  MockWrite(ASYNC, 10, request_data),
      MockWrite(ASYNC, 12, request_data), MockWrite(ASYNC, 14, request_data),
      MockWrite(ASYNC, 17, request_data), MockWrite(ASYNC, 20, request_data),
  };

  // Note that because all these reads happen in the same
  // StaticSocketDataProvider, it shows that the same socket is being reused for
  // all transactions.
  MockRead data_reads[] = {
      MockRead(ASYNC, 1, "HTTP/1.1 204 No Content\r\n\r\n"),
      MockRead(ASYNC, 3, "HTTP/1.1 205 Reset Content\r\n\r\n"),
      MockRead(ASYNC, 5, "HTTP/1.1 304 Not Modified\r\n\r\n"),
      MockRead(ASYNC, 7,
               "HTTP/1.1 302 Found\r\n"
               "Content-Length: 0\r\n\r\n"),
      MockRead(ASYNC, 9,
               "HTTP/1.1 302 Found\r\n"
               "Content-Length: 5\r\n\r\n"
               "hello"),
      MockRead(ASYNC, 11,
               "HTTP/1.1 301 Moved Permanently\r\n"
               "Content-Length: 0\r\n\r\n"),
      MockRead(ASYNC, 13,
               "HTTP/1.1 301 Moved Permanently\r\n"
               "Content-Length: 5\r\n\r\n"
               "hello"),

      // In the next two rounds, IsConnectedAndIdle returns false, due to
      // the set_busy_before_sync_reads(true) call, while the
      // HttpNetworkTransaction is being shut down, but the socket is still
      // reuseable.  See http://crbug.com/544255.
      MockRead(ASYNC, 15,
               "HTTP/1.1 200 Hunky-Dory\r\n"
               "Content-Length: 5\r\n\r\n"),
      MockRead(SYNCHRONOUS, 16, "hello"),

      MockRead(ASYNC, 18,
               "HTTP/1.1 200 Hunky-Dory\r\n"
               "Content-Length: 5\r\n\r\n"
               "he"),
      MockRead(SYNCHRONOUS, 19, "llo"),

      // The body of the final request is actually read.
      MockRead(ASYNC, 21, "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\n"),
      MockRead(ASYNC, 22, "hello"),
  };
  SequencedSocketData data(data_reads, data_writes);
  data.set_busy_before_sync_reads(true);
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  const int kNumUnreadBodies = std::size(data_writes) - 1;
  std::string response_lines[kNumUnreadBodies];

  uint32_t first_socket_log_id = NetLogSource::kInvalidId;
  for (size_t i = 0; i < kNumUnreadBodies; ++i) {
    TestCompletionCallback callback;

    auto trans = std::make_unique<HttpNetworkTransaction>(DEFAULT_PRIORITY,
                                                          session.get());

    int rv = trans->Start(&request, callback.callback(), NetLogWithSource());
    EXPECT_THAT(callback.GetResult(rv), IsOk());

    LoadTimingInfo load_timing_info;
    EXPECT_TRUE(trans->GetLoadTimingInfo(&load_timing_info));
    if (i == 0) {
      TestLoadTimingNotReused(load_timing_info, CONNECT_TIMING_HAS_DNS_TIMES);
      first_socket_log_id = load_timing_info.socket_log_id;
    } else {
      TestLoadTimingReused(load_timing_info);
      EXPECT_EQ(first_socket_log_id, load_timing_info.socket_log_id);
    }

    const HttpResponseInfo* response = trans->GetResponseInfo();
    ASSERT_TRUE(response);

    ASSERT_TRUE(response->headers);
    response_lines[i] = response->headers->GetStatusLine();

    // Delete the transaction without reading the response bodies.  Then spin
    // the message loop, so the response bodies are drained.
    trans.reset();
    base::RunLoop().RunUntilIdle();
  }

  const char* const kStatusLines[] = {
      "HTTP/1.1 204 No Content",
      "HTTP/1.1 205 Reset Content",
      "HTTP/1.1 304 Not Modified",
      "HTTP/1.1 302 Found",
      "HTTP/1.1 302 Found",
      "HTTP/1.1 301 Moved Permanently",
      "HTTP/1.1 301 Moved Permanently",
      "HTTP/1.1 200 Hunky-Dory",
      "HTTP/1.1 200 Hunky-Dory",
  };

  static_assert(kNumUnreadBodies == std::size(kStatusLines),
                "forgot to update kStatusLines");

  for (int i = 0; i < kNumUnreadBodies; ++i)
    EXPECT_EQ(kStatusLines[i], response_lines[i]);

  TestCompletionCallback callback;
  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());
  int rv = trans.Start(&request, callback.callback(), NetLogWithSource());
  EXPECT_THAT(callback.GetResult(rv), IsOk());
  const HttpResponseInfo* response = trans.GetResponseInfo();
  ASSERT_TRUE(response);
  ASSERT_TRUE(response->headers);
  EXPECT_EQ("HTTP/1.1 200 OK", response->headers->GetStatusLine());
  std::string response_data;
  rv = ReadTransaction(&trans, &response_data);
  EXPECT_THAT(rv, IsOk());
  EXPECT_EQ("hello", response_data);
}

// Sockets that receive extra data after a response is complete should not be
// reused.
TEST_F(HttpNetworkTransactionTest, KeepAliveWithUnusedData1) {
  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));
  MockWrite data_writes1[] = {
      MockWrite("HEAD / HTTP/1.1\r\n"
                "Host: www.borked.com\r\n"
                "Connection: keep-alive\r\n\r\n"),
  };

  MockRead data_reads1[] = {
      MockRead("HTTP/1.1 200 OK\r\n"
               "Connection: keep-alive\r\n"
               "Content-Length: 22\r\n\r\n"
               "This server is borked."),
  };

  MockWrite data_writes2[] = {
      MockWrite("GET /foo HTTP/1.1\r\n"
                "Host: www.borked.com\r\n"
                "Connection: keep-alive\r\n\r\n"),
  };

  MockRead data_reads2[] = {
      MockRead("HTTP/1.1 200 OK\r\n"
               "Content-Length: 3\r\n\r\n"
               "foo"),
  };
  StaticSocketDataProvider data1(data_reads1, data_writes1);
  session_deps_.socket_factory->AddSocketDataProvider(&data1);
  StaticSocketDataProvider data2(data_reads2, data_writes2);
  session_deps_.socket_factory->AddSocketDataProvider(&data2);

  TestCompletionCallback callback;
  HttpRequestInfo request1;
  request1.method = "HEAD";
  request1.url = GURL("http://www.borked.com/");
  request1.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  auto trans1 =
      std::make_unique<HttpNetworkTransaction>(DEFAULT_PRIORITY, session.get());
  int rv = trans1->Start(&request1, callback.callback(), NetLogWithSource());
  EXPECT_THAT(callback.GetResult(rv), IsOk());

  const HttpResponseInfo* response1 = trans1->GetResponseInfo();
  ASSERT_TRUE(response1);
  ASSERT_TRUE(response1->headers);
  EXPECT_EQ(200, response1->headers->response_code());
  EXPECT_TRUE(response1->headers->IsKeepAlive());

  std::string response_data1;
  EXPECT_THAT(ReadTransaction(trans1.get(), &response_data1), IsOk());
  EXPECT_EQ("", response_data1);
  // Deleting the transaction attempts to release the socket back into the
  // socket pool.
  trans1.reset();

  HttpRequestInfo request2;
  request2.method = "GET";
  request2.url = GURL("http://www.borked.com/foo");
  request2.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  auto trans2 =
      std::make_unique<HttpNetworkTransaction>(DEFAULT_PRIORITY, session.get());
  rv = trans2->Start(&request2, callback.callback(), NetLogWithSource());
  EXPECT_THAT(callback.GetResult(rv), IsOk());

  const HttpResponseInfo* response2 = trans2->GetResponseInfo();
  ASSERT_TRUE(response2);
  ASSERT_TRUE(response2->headers);
  EXPECT_EQ(200, response2->headers->response_code());

  std::string response_data2;
  EXPECT_THAT(ReadTransaction(trans2.get(), &response_data2), IsOk());
  EXPECT_EQ("foo", response_data2);
}

TEST_F(HttpNetworkTransactionTest, KeepAliveWithUnusedData2) {
  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));
  MockWrite data_writes1[] = {
      MockWrite("GET / HTTP/1.1\r\n"
                "Host: www.borked.com\r\n"
                "Connection: keep-alive\r\n\r\n"),
  };

  MockRead data_reads1[] = {
      MockRead("HTTP/1.1 200 OK\r\n"
               "Connection: keep-alive\r\n"
               "Content-Length: 22\r\n\r\n"
               "This server is borked."
               "Bonus data!"),
  };

  MockWrite data_writes2[] = {
      MockWrite("GET /foo HTTP/1.1\r\n"
                "Host: www.borked.com\r\n"
                "Connection: keep-alive\r\n\r\n"),
  };

  MockRead data_reads2[] = {
      MockRead("HTTP/1.1 200 OK\r\n"
               "Content-Length: 3\r\n\r\n"
               "foo"),
  };
  StaticSocketDataProvider data1(data_reads1, data_writes1);
  session_deps_.socket_factory->AddSocketDataProvider(&data1);
  StaticSocketDataProvider data2(data_reads2, data_writes2);
  session_deps_.socket_factory->AddSocketDataProvider(&data2);

  TestCompletionCallback callback;
  HttpRequestInfo request1;
  request1.method = "GET";
  request1.url = GURL("http://www.borked.com/");
  request1.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  auto trans1 =
      std::make_unique<HttpNetworkTransaction>(DEFAULT_PRIORITY, session.get());
  int rv = trans1->Start(&request1, callback.callback(), NetLogWithSource());
  EXPECT_THAT(callback.GetResult(rv), IsOk());

  const HttpResponseInfo* response1 = trans1->GetResponseInfo();
  ASSERT_TRUE(response1);
  ASSERT_TRUE(response1->headers);
  EXPECT_EQ(200, response1->headers->response_code());
  EXPECT_TRUE(response1->headers->IsKeepAlive());

  std::string response_data1;
  EXPECT_THAT(ReadTransaction(trans1.get(), &response_data1), IsOk());
  EXPECT_EQ("This server is borked.", response_data1);
  // Deleting the transaction attempts to release the socket back into the
  // socket pool.
  trans1.reset();

  HttpRequestInfo request2;
  request2.method = "GET";
  request2.url = GURL("http://www.borked.com/foo");
  request2.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  auto trans2 =
      std::make_unique<HttpNetworkTransaction>(DEFAULT_PRIORITY, session.get());
  rv = trans2->Start(&request2, callback.callback(), NetLogWithSource());
  EXPECT_THAT(callback.GetResult(rv), IsOk());

  const HttpResponseInfo* response2 = trans2->GetResponseInfo();
  ASSERT_TRUE(response2);
  ASSERT_TRUE(response2->headers);
  EXPECT_EQ(200, response2->headers->response_code());

  std::string response_data2;
  EXPECT_THAT(ReadTransaction(trans2.get(), &response_data2), IsOk());
  EXPECT_EQ("foo", response_data2);
}

TEST_F(HttpNetworkTransactionTest, KeepAliveWithUnusedData3) {
  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));
  MockWrite data_writes1[] = {
      MockWrite("GET / HTTP/1.1\r\n"
                "Host: www.borked.com\r\n"
                "Connection: keep-alive\r\n\r\n"),
  };

  MockRead data_reads1[] = {
      MockRead("HTTP/1.1 200 OK\r\n"
               "Connection: keep-alive\r\n"
               "Transfer-Encoding: chunked\r\n\r\n"),
      MockRead("16\r\nThis server is borked.\r\n"),
      MockRead("0\r\n\r\nBonus data!"),
  };

  MockWrite data_writes2[] = {
      MockWrite("GET /foo HTTP/1.1\r\n"
                "Host: www.borked.com\r\n"
                "Connection: keep-alive\r\n\r\n"),
  };

  MockRead data_reads2[] = {
      MockRead("HTTP/1.1 200 OK\r\n"
               "Content-Length: 3\r\n\r\n"
               "foo"),
  };
  StaticSocketDataProvider data1(data_reads1, data_writes1);
  session_deps_.socket_factory->AddSocketDataProvider(&data1);
  StaticSocketDataProvider data2(data_reads2, data_writes2);
  session_deps_.socket_factory->AddSocketDataProvider(&data2);

  TestCompletionCallback callback;
  HttpRequestInfo request1;
  request1.method = "GET";
  request1.url = GURL("http://www.borked.com/");
  request1.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  auto trans1 =
      std::make_unique<HttpNetworkTransaction>(DEFAULT_PRIORITY, session.get());
  int rv = trans1->Start(&request1, callback.callback(), NetLogWithSource());
  EXPECT_THAT(callback.GetResult(rv), IsOk());

  const HttpResponseInfo* response1 = trans1->GetResponseInfo();
  ASSERT_TRUE(response1);
  ASSERT_TRUE(response1->headers);
  EXPECT_EQ(200, response1->headers->response_code());
  EXPECT_TRUE(response1->headers->IsKeepAlive());

  std::string response_data1;
  EXPECT_THAT(ReadTransaction(trans1.get(), &response_data1), IsOk());
  EXPECT_EQ("This server is borked.", response_data1);
  // Deleting the transaction attempts to release the socket back into the
  // socket pool.
  trans1.reset();

  HttpRequestInfo request2;
  request2.method = "GET";
  request2.url = GURL("http://www.borked.com/foo");
  request2.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  auto trans2 =
      std::make_unique<HttpNetworkTransaction>(DEFAULT_PRIORITY, session.get());
  rv = trans2->Start(&request2, callback.callback(), NetLogWithSource());
  EXPECT_THAT(callback.GetResult(rv), IsOk());

  const HttpResponseInfo* response2 = trans2->GetResponseInfo();
  ASSERT_TRUE(response2);
  ASSERT_TRUE(response2->headers);
  EXPECT_EQ(200, response2->headers->response_code());

  std::string response_data2;
  EXPECT_THAT(ReadTransaction(trans2.get(), &response_data2), IsOk());
  EXPECT_EQ("foo", response_data2);
}

// This is a little different from the others - it tests the case that the
// HttpStreamParser doesn't know if there's extra data on a socket or not when
// the HttpNetworkTransaction is torn down, because the response body hasn't
// been read from yet, but the request goes through the HttpResponseBodyDrainer.
TEST_F(HttpNetworkTransactionTest, KeepAliveWithUnusedData4) {
  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));
  MockWrite data_writes1[] = {
      MockWrite("GET / HTTP/1.1\r\n"
                "Host: www.borked.com\r\n"
                "Connection: keep-alive\r\n\r\n"),
  };

  MockRead data_reads1[] = {
      MockRead("HTTP/1.1 200 OK\r\n"
               "Connection: keep-alive\r\n"
               "Transfer-Encoding: chunked\r\n\r\n"),
      MockRead("16\r\nThis server is borked.\r\n"),
      MockRead("0\r\n\r\nBonus data!"),
  };
  StaticSocketDataProvider data1(data_reads1, data_writes1);
  session_deps_.socket_factory->AddSocketDataProvider(&data1);

  TestCompletionCallback callback;
  HttpRequestInfo request1;
  request1.method = "GET";
  request1.url = GURL("http://www.borked.com/");
  request1.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  auto trans =
      std::make_unique<HttpNetworkTransaction>(DEFAULT_PRIORITY, session.get());
  int rv = trans->Start(&request1, callback.callback(), NetLogWithSource());
  EXPECT_THAT(callback.GetResult(rv), IsOk());

  const HttpResponseInfo* response1 = trans->GetResponseInfo();
  ASSERT_TRUE(response1);
  ASSERT_TRUE(response1->headers);
  EXPECT_EQ(200, response1->headers->response_code());
  EXPECT_TRUE(response1->headers->IsKeepAlive());

  // Deleting the transaction creates an HttpResponseBodyDrainer to read the
  // response body.
  trans.reset();

  // Let the HttpResponseBodyDrainer drain the socket.  It should determine the
  // socket can't be reused, rather than returning it to the socket pool.
  base::RunLoop().RunUntilIdle();

  // There should be no idle sockets in the pool.
  EXPECT_EQ(0, GetIdleSocketCountInTransportSocketPool(session.get()));
}

// Test the request-challenge-retry sequence for basic auth.
// (basic auth is the easiest to mock, because it has no randomness).
TEST_F(HttpNetworkTransactionTest, BasicAuth) {
  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("http://www.example.org/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  session_deps_.net_log = net::NetLog::Get();
  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));
  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

  MockWrite data_writes1[] = {
      MockWrite(
          "GET / HTTP/1.1\r\n"
          "Host: www.example.org\r\n"
          "Connection: keep-alive\r\n\r\n"),
  };

  MockRead data_reads1[] = {
    MockRead("HTTP/1.0 401 Unauthorized\r\n"),
    // Give a couple authenticate options (only the middle one is actually
    // supported).
    MockRead("WWW-Authenticate: Basic invalid\r\n"),  // Malformed.
    MockRead("WWW-Authenticate: Basic realm=\"MyRealm1\"\r\n"),
    MockRead("WWW-Authenticate: UNSUPPORTED realm=\"FOO\"\r\n"),
    MockRead("Content-Type: text/html; charset=iso-8859-1\r\n"),
    // Large content-length -- won't matter, as connection will be reset.
    MockRead("Content-Length: 10000\r\n\r\n"),
    MockRead(SYNCHRONOUS, ERR_FAILED),
  };

  // After calling trans->RestartWithAuth(), this is the request we should
  // be issuing -- the final header line contains the credentials.
  MockWrite data_writes2[] = {
      MockWrite(
          "GET / HTTP/1.1\r\n"
          "Host: www.example.org\r\n"
          "Connection: keep-alive\r\n"
          "Authorization: Basic Zm9vOmJhcg==\r\n\r\n"),
  };

  // Lastly, the server responds with the actual content.
  MockRead data_reads2[] = {
    MockRead("HTTP/1.0 200 OK\r\n"),
    MockRead("Content-Type: text/html; charset=iso-8859-1\r\n"),
    MockRead("Content-Length: 100\r\n\r\n"),
    MockRead(SYNCHRONOUS, OK),
  };

  StaticSocketDataProvider data1(data_reads1, data_writes1);
  StaticSocketDataProvider data2(data_reads2, data_writes2);
  session_deps_.socket_factory->AddSocketDataProvider(&data1);
  session_deps_.socket_factory->AddSocketDataProvider(&data2);

  TestCompletionCallback callback1;

  int rv = trans.Start(&request, callback1.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback1.WaitForResult();
  EXPECT_THAT(rv, IsOk());

  LoadTimingInfo load_timing_info1;
  EXPECT_TRUE(trans.GetLoadTimingInfo(&load_timing_info1));
  TestLoadTimingNotReused(load_timing_info1, CONNECT_TIMING_HAS_DNS_TIMES);

  int64_t writes_size1 = CountWriteBytes(data_writes1);
  EXPECT_EQ(writes_size1, trans.GetTotalSentBytes());
  int64_t reads_size1 = CountReadBytes(data_reads1);
  EXPECT_EQ(reads_size1, trans.GetTotalReceivedBytes());

  const HttpResponseInfo* response = trans.GetResponseInfo();
  ASSERT_TRUE(response);
  EXPECT_TRUE(CheckBasicServerAuth(response->auth_challenge));

  TestCompletionCallback callback2;

  rv = trans.RestartWithAuth(AuthCredentials(kFoo, kBar), callback2.callback());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback2.WaitForResult();
  EXPECT_THAT(rv, IsOk());

  LoadTimingInfo load_timing_info2;
  EXPECT_TRUE(trans.GetLoadTimingInfo(&load_timing_info2));
  TestLoadTimingNotReused(load_timing_info2, CONNECT_TIMING_HAS_DNS_TIMES);
  // The load timing after restart should have a new socket ID, and times after
  // those of the first load timing.
  EXPECT_LE(load_timing_info1.receive_headers_end,
            load_timing_info2.connect_timing.connect_start);
  EXPECT_NE(load_timing_info1.socket_log_id, load_timing_info2.socket_log_id);

  int64_t writes_size2 = CountWriteBytes(data_writes2);
  EXPECT_EQ(writes_size1 + writes_size2, trans.GetTotalSentBytes());
  int64_t reads_size2 = CountReadBytes(data_reads2);
  EXPECT_EQ(reads_size1 + reads_size2, trans.GetTotalReceivedBytes());

  response = trans.GetResponseInfo();
  ASSERT_TRUE(response);
  EXPECT_FALSE(response->auth_challenge.has_value());
  EXPECT_EQ(100, response->headers->GetContentLength());
}

// Test the request-challenge-retry sequence for basic auth.
// (basic auth is the easiest to mock, because it has no randomness).
TEST_F(HttpNetworkTransactionTest, BasicAuthWithAddressChange) {
  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("http://www.example.org/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  auto resolver = std::make_unique<MockHostResolver>();
  auto* resolver_ptr = resolver.get();
  session_deps_.net_log = net::NetLog::Get();
  session_deps_.host_resolver = std::move(resolver);
  std::unique_ptr<HttpNetworkSession> session = CreateSession(&session_deps_);
  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

  resolver_ptr->rules()->ClearRules();
  resolver_ptr->rules()->AddRule("www.example.org", "127.0.0.1");

  MockWrite data_writes1[] = {
      MockWrite("GET / HTTP/1.1\r\n"
                "Host: www.example.org\r\n"
                "Connection: keep-alive\r\n\r\n"),
  };

  MockRead data_reads1[] = {
      MockRead("HTTP/1.0 401 Unauthorized\r\n"),
      // Give a couple authenticate options (only the middle one is actually
      // supported).
      MockRead("WWW-Authenticate: Basic invalid\r\n"),  // Malformed.
      MockRead("WWW-Authenticate: Basic realm=\"MyRealm1\"\r\n"),
      MockRead("WWW-Authenticate: UNSUPPORTED realm=\"FOO\"\r\n"),
      MockRead("Content-Type: text/html; charset=iso-8859-1\r\n"),
      // Large content-length -- won't matter, as connection will be reset.
      MockRead("Content-Length: 10000\r\n\r\n"),
      MockRead(SYNCHRONOUS, ERR_FAILED),
  };

  // After calling trans->RestartWithAuth(), this is the request we should
  // be issuing -- the final header line contains the credentials.
  MockWrite data_writes2[] = {
      MockWrite("GET / HTTP/1.1\r\n"
                "Host: www.example.org\r\n"
                "Connection: keep-alive\r\n"
                "Authorization: Basic Zm9vOmJhcg==\r\n\r\n"),
  };

  // Lastly, the server responds with the actual content.
  MockRead data_reads2[] = {
      MockRead("HTTP/1.0 200 OK\r\n"),
      MockRead("Content-Type: text/html; charset=iso-8859-1\r\n"),
      MockRead("Content-Length: 100\r\n\r\n"), MockRead(SYNCHRONOUS, OK),
  };

  StaticSocketDataProvider data1(data_reads1, data_writes1);
  StaticSocketDataProvider data2(data_reads2, data_writes2);
  session_deps_.socket_factory->AddSocketDataProvider(&data1);
  session_deps_.socket_factory->AddSocketDataProvider(&data2);

  TestCompletionCallback callback1;

  EXPECT_EQ(OK, callback1.GetResult(trans.Start(&request, callback1.callback(),
                                                NetLogWithSource())));

  LoadTimingInfo load_timing_info1;
  EXPECT_TRUE(trans.GetLoadTimingInfo(&load_timing_info1));
  TestLoadTimingNotReused(load_timing_info1, CONNECT_TIMING_HAS_DNS_TIMES);

  int64_t writes_size1 = CountWriteBytes(data_writes1);
  EXPECT_EQ(writes_size1, trans.GetTotalSentBytes());
  int64_t reads_size1 = CountReadBytes(data_reads1);
  EXPECT_EQ(reads_size1, trans.GetTotalReceivedBytes());

  const HttpResponseInfo* response = trans.GetResponseInfo();
  ASSERT_TRUE(response);
  EXPECT_TRUE(CheckBasicServerAuth(response->auth_challenge));

  IPEndPoint endpoint;
  EXPECT_TRUE(trans.GetRemoteEndpoint(&endpoint));
  ASSERT_FALSE(endpoint.address().empty());
  EXPECT_EQ("127.0.0.1:80", endpoint.ToString());

  resolver_ptr->rules()->ClearRules();
  resolver_ptr->rules()->AddRule("www.example.org", "127.0.0.2");

  TestCompletionCallback callback2;

  EXPECT_EQ(OK, callback2.GetResult(trans.RestartWithAuth(
                    AuthCredentials(kFoo, kBar), callback2.callback())));

  LoadTimingInfo load_timing_info2;
  EXPECT_TRUE(trans.GetLoadTimingInfo(&load_timing_info2));
  TestLoadTimingNotReused(load_timing_info2, CONNECT_TIMING_HAS_DNS_TIMES);
  // The load timing after restart should have a new socket ID, and times after
  // those of the first load timing.
  EXPECT_LE(load_timing_info1.receive_headers_end,
            load_timing_info2.connect_timing.connect_start);
  EXPECT_NE(load_timing_info1.socket_log_id, load_timing_info2.socket_log_id);

  int64_t writes_size2 = CountWriteBytes(data_writes2);
  EXPECT_EQ(writes_size1 + writes_size2, trans.GetTotalSentBytes());
  int64_t reads_size2 = CountReadBytes(data_reads2);
  EXPECT_EQ(reads_size1 + reads_size2, trans.GetTotalReceivedBytes());

  response = trans.GetResponseInfo();
  ASSERT_TRUE(response);
  EXPECT_FALSE(response->auth_challenge.has_value());
  EXPECT_EQ(100, response->headers->GetContentLength());

  EXPECT_TRUE(trans.GetRemoteEndpoint(&endpoint));
  ASSERT_FALSE(endpoint.address().empty());
  EXPECT_EQ("127.0.0.2:80", endpoint.ToString());
}

// Test that, if the server requests auth indefinitely, HttpNetworkTransaction
// will eventually give up.
TEST_F(HttpNetworkTransactionTest, BasicAuthForever) {
  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("http://www.example.org/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  session_deps_.net_log = net::NetLog::Get();
  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));
  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

  MockWrite data_writes[] = {
      MockWrite("GET / HTTP/1.1\r\n"
                "Host: www.example.org\r\n"
                "Connection: keep-alive\r\n\r\n"),
  };

  MockRead data_reads[] = {
      MockRead("HTTP/1.0 401 Unauthorized\r\n"),
      // Give a couple authenticate options (only the middle one is actually
      // supported).
      MockRead("WWW-Authenticate: Basic invalid\r\n"),  // Malformed.
      MockRead("WWW-Authenticate: Basic realm=\"MyRealm1\"\r\n"),
      MockRead("WWW-Authenticate: UNSUPPORTED realm=\"FOO\"\r\n"),
      MockRead("Content-Type: text/html; charset=iso-8859-1\r\n"),
      // Large content-length -- won't matter, as connection will be reset.
      MockRead("Content-Length: 10000\r\n\r\n"),
      MockRead(SYNCHRONOUS, ERR_FAILED),
  };

  // After calling trans->RestartWithAuth(), this is the request we should
  // be issuing -- the final header line contains the credentials.
  MockWrite data_writes_restart[] = {
      MockWrite("GET / HTTP/1.1\r\n"
                "Host: www.example.org\r\n"
                "Connection: keep-alive\r\n"
                "Authorization: Basic Zm9vOmJhcg==\r\n\r\n"),
  };

  StaticSocketDataProvider data(data_reads, data_writes);
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  TestCompletionCallback callback;
  int rv = callback.GetResult(
      trans.Start(&request, callback.callback(), NetLogWithSource()));

  std::vector<std::unique_ptr<StaticSocketDataProvider>> data_restarts;
  for (int i = 0; i < 32; i++) {
    // Check the previous response was a 401.
    EXPECT_THAT(rv, IsOk());
    const HttpResponseInfo* response = trans.GetResponseInfo();
    ASSERT_TRUE(response);
    EXPECT_TRUE(CheckBasicServerAuth(response->auth_challenge));

    data_restarts.push_back(std::make_unique<StaticSocketDataProvider>(
        data_reads, data_writes_restart));
    session_deps_.socket_factory->AddSocketDataProvider(
        data_restarts.back().get());
    rv = callback.GetResult(trans.RestartWithAuth(AuthCredentials(kFoo, kBar),
                                                  callback.callback()));
  }

  // After too many tries, the transaction should have given up.
  EXPECT_THAT(rv, IsError(ERR_TOO_MANY_RETRIES));
}

TEST_F(HttpNetworkTransactionTest, DoNotSendAuth) {
  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("http://www.example.org/");
  request.privacy_mode = PRIVACY_MODE_ENABLED;
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));
  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

  MockWrite data_writes[] = {
      MockWrite(
          "GET / HTTP/1.1\r\n"
          "Host: www.example.org\r\n"
          "Connection: keep-alive\r\n\r\n"),
  };

  MockRead data_reads[] = {
    MockRead("HTTP/1.0 401 Unauthorized\r\n"),
    MockRead("WWW-Authenticate: Basic realm=\"MyRealm1\"\r\n"),
    MockRead("Content-Type: text/html; charset=iso-8859-1\r\n"),
    // Large content-length -- won't matter, as connection will be reset.
    MockRead("Content-Length: 10000\r\n\r\n"),
    MockRead(SYNCHRONOUS, ERR_FAILED),
  };

  StaticSocketDataProvider data(data_reads, data_writes);
  session_deps_.socket_factory->AddSocketDataProvider(&data);
  TestCompletionCallback callback;

  int rv = trans.Start(&request, callback.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback.WaitForResult();
  EXPECT_EQ(0, rv);

  int64_t writes_size = CountWriteBytes(data_writes);
  EXPECT_EQ(writes_size, trans.GetTotalSentBytes());
  int64_t reads_size = CountReadBytes(data_reads);
  EXPECT_EQ(reads_size, trans.GetTotalReceivedBytes());

  const HttpResponseInfo* response = trans.GetResponseInfo();
  ASSERT_TRUE(response);
  EXPECT_FALSE(response->auth_challenge.has_value());
}

// Test the request-challenge-retry sequence for basic auth, over a keep-alive
// connection.
TEST_F(HttpNetworkTransactionTest, BasicAuthKeepAlive) {
  // On the second pass, the body read of the auth challenge is synchronous, so
  // IsConnectedAndIdle returns false.  The socket should still be drained and
  // reused.  See http://crbug.com/544255.
  for (int i = 0; i < 2; ++i) {
    HttpRequestInfo request;
    request.method = "GET";
    request.url = GURL("http://www.example.org/");
    request.traffic_annotation =
        net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

    session_deps_.net_log = net::NetLog::Get();
    std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));

    MockWrite data_writes[] = {
        MockWrite(ASYNC, 0,
                  "GET / HTTP/1.1\r\n"
                  "Host: www.example.org\r\n"
                  "Connection: keep-alive\r\n\r\n"),

        // After calling trans.RestartWithAuth(), this is the request we should
        // be issuing -- the final header line contains the credentials.
        MockWrite(ASYNC, 6,
                  "GET / HTTP/1.1\r\n"
                  "Host: www.example.org\r\n"
                  "Connection: keep-alive\r\n"
                  "Authorization: Basic Zm9vOmJhcg==\r\n\r\n"),
    };

    MockRead data_reads[] = {
        MockRead(ASYNC, 1, "HTTP/1.1 401 Unauthorized\r\n"),
        MockRead(ASYNC, 2, "WWW-Authenticate: Basic realm=\"MyRealm1\"\r\n"),
        MockRead(ASYNC, 3, "Content-Type: text/html; charset=iso-8859-1\r\n"),
        MockRead(ASYNC, 4, "Content-Length: 14\r\n\r\n"),
        MockRead(i == 0 ? ASYNC : SYNCHRONOUS, 5, "Unauthorized\r\n"),

        // Lastly, the server responds with the actual content.
        MockRead(ASYNC, 7, "HTTP/1.1 200 OK\r\n"),
        MockRead(ASYNC, 8, "Content-Type: text/html; charset=iso-8859-1\r\n"),
        MockRead(ASYNC, 9, "Content-Length: 5\r\n\r\n"),
        MockRead(ASYNC, 10, "Hello"),
    };

    SequencedSocketData data(data_reads, data_writes);
    data.set_busy_before_sync_reads(true);
    session_deps_.socket_factory->AddSocketDataProvider(&data);

    TestCompletionCallback callback1;

    HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());
    int rv = trans.Start(&request, callback1.callback(), NetLogWithSource());
    ASSERT_THAT(callback1.GetResult(rv), IsOk());

    LoadTimingInfo load_timing_info1;
    EXPECT_TRUE(trans.GetLoadTimingInfo(&load_timing_info1));
    TestLoadTimingNotReused(load_timing_info1, CONNECT_TIMING_HAS_DNS_TIMES);

    const HttpResponseInfo* response = trans.GetResponseInfo();
    ASSERT_TRUE(response);
    EXPECT_TRUE(CheckBasicServerAuth(response->auth_challenge));

    TestCompletionCallback callback2;

    rv = trans.RestartWithAuth(AuthCredentials(kFoo, kBar),
                               callback2.callback());
    ASSERT_THAT(callback2.GetResult(rv), IsOk());

    LoadTimingInfo load_timing_info2;
    EXPECT_TRUE(trans.GetLoadTimingInfo(&load_timing_info2));
    TestLoadTimingReused(load_timing_info2);
    // The load timing after restart should have the same socket ID, and times
    // those of the first load timing.
    EXPECT_LE(load_timing_info1.receive_headers_end,
              load_timing_info2.send_start);
    EXPECT_EQ(load_timing_info1.socket_log_id, load_timing_info2.socket_log_id);

    response = trans.GetResponseInfo();
    ASSERT_TRUE(response);
    EXPECT_FALSE(response->auth_challenge.has_value());
    EXPECT_EQ(5, response->headers->GetContentLength());

    std::string response_data;
    EXPECT_THAT(ReadTransaction(&trans, &response_data), IsOk());

    int64_t writes_size = CountWriteBytes(data_writes);
    EXPECT_EQ(writes_size, trans.GetTotalSentBytes());
    int64_t reads_size = CountReadBytes(data_reads);
    EXPECT_EQ(reads_size, trans.GetTotalReceivedBytes());
  }
}

// Test the request-challenge-retry sequence for basic auth, over a keep-alive
// connection and with no response body to drain.
TEST_F(HttpNetworkTransactionTest, BasicAuthKeepAliveNoBody) {
  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("http://www.example.org/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));

  MockWrite data_writes1[] = {
      MockWrite("GET / HTTP/1.1\r\n"
                "Host: www.example.org\r\n"
                "Connection: keep-alive\r\n\r\n"),

      // After calling trans.RestartWithAuth(), this is the request we should
      // be issuing -- the final header line contains the credentials.
      MockWrite("GET / HTTP/1.1\r\n"
                "Host: www.example.org\r\n"
                "Connection: keep-alive\r\n"
                "Authorization: Basic Zm9vOmJhcg==\r\n\r\n"),
  };

  MockRead data_reads1[] = {
    MockRead("HTTP/1.1 401 Unauthorized\r\n"),
    MockRead("WWW-Authenticate: Basic realm=\"MyRealm1\"\r\n"),
    MockRead("Content-Length: 0\r\n\r\n"),  // No response body.

    // Lastly, the server responds with the actual content.
    MockRead("HTTP/1.1 200 OK\r\n"),
    MockRead("Content-Type: text/html; charset=iso-8859-1\r\n"),
    MockRead("Content-Length: 5\r\n\r\n"),
    MockRead("hello"),
  };

  // An incorrect reconnect would cause this to be read.
  MockRead data_reads2[] = {
    MockRead(SYNCHRONOUS, ERR_FAILED),
  };

  StaticSocketDataProvider data1(data_reads1, data_writes1);
  StaticSocketDataProvider data2(data_reads2, base::span<MockWrite>());
  session_deps_.socket_factory->AddSocketDataProvider(&data1);
  session_deps_.socket_factory->AddSocketDataProvider(&data2);

  TestCompletionCallback callback1;

  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());
  int rv = trans.Start(&request, callback1.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback1.WaitForResult();
  EXPECT_THAT(rv, IsOk());

  const HttpResponseInfo* response = trans.GetResponseInfo();
  ASSERT_TRUE(response);
  EXPECT_TRUE(CheckBasicServerAuth(response->auth_challenge));

  TestCompletionCallback callback2;

  rv = trans.RestartWithAuth(AuthCredentials(kFoo, kBar), callback2.callback());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback2.WaitForResult();
  EXPECT_THAT(rv, IsOk());

  response = trans.GetResponseInfo();
  ASSERT_TRUE(response);
  EXPECT_FALSE(response->auth_challenge.has_value());
  EXPECT_EQ(5, response->headers->GetContentLength());
}

// Test the request-challenge-retry sequence for basic auth, over a keep-alive
// connection and with a large response body to drain.
TEST_F(HttpNetworkTransactionTest, BasicAuthKeepAliveLargeBody) {
  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("http://www.example.org/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));

  MockWrite data_writes1[] = {
      MockWrite("GET / HTTP/1.1\r\n"
                "Host: www.example.org\r\n"
                "Connection: keep-alive\r\n\r\n"),

      // After calling trans.RestartWithAuth(), this is the request we should
      // be issuing -- the final header line contains the credentials.
      MockWrite("GET / HTTP/1.1\r\n"
                "Host: www.example.org\r\n"
                "Connection: keep-alive\r\n"
                "Authorization: Basic Zm9vOmJhcg==\r\n\r\n"),
  };

  // Respond with 5 kb of response body.
  std::string large_body_string("Unauthorized");
  large_body_string.append(5 * 1024, ' ');
  large_body_string.append("\r\n");

  MockRead data_reads1[] = {
    MockRead("HTTP/1.1 401 Unauthorized\r\n"),
    MockRead("WWW-Authenticate: Basic realm=\"MyRealm1\"\r\n"),
    MockRead("Content-Type: text/html; charset=iso-8859-1\r\n"),
    // 5134 = 12 + 5 * 1024 + 2
    MockRead("Content-Length: 5134\r\n\r\n"),
    MockRead(ASYNC, large_body_string.data(), large_body_string.size()),

    // Lastly, the server responds with the actual content.
    MockRead("HTTP/1.1 200 OK\r\n"),
    MockRead("Content-Type: text/html; charset=iso-8859-1\r\n"),
    MockRead("Content-Length: 5\r\n\r\n"),
    MockRead("hello"),
  };

  // An incorrect reconnect would cause this to be read.
  MockRead data_reads2[] = {
    MockRead(SYNCHRONOUS, ERR_FAILED),
  };

  StaticSocketDataProvider data1(data_reads1, data_writes1);
  StaticSocketDataProvider data2(data_reads2, base::span<MockWrite>());
  session_deps_.socket_factory->AddSocketDataProvider(&data1);
  session_deps_.socket_factory->AddSocketDataProvider(&data2);

  TestCompletionCallback callback1;

  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());
  int rv = trans.Start(&request, callback1.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback1.WaitForResult();
  EXPECT_THAT(rv, IsOk());

  const HttpResponseInfo* response = trans.GetResponseInfo();
  ASSERT_TRUE(response);
  EXPECT_TRUE(CheckBasicServerAuth(response->auth_challenge));

  TestCompletionCallback callback2;

  rv = trans.RestartWithAuth(AuthCredentials(kFoo, kBar), callback2.callback());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback2.WaitForResult();
  EXPECT_THAT(rv, IsOk());

  response = trans.GetResponseInfo();
  ASSERT_TRUE(response);
  EXPECT_FALSE(response->auth_challenge.has_value());
  EXPECT_EQ(5, response->headers->GetContentLength());
}

// Test the request-challenge-retry sequence for basic auth, over a keep-alive
// connection, but the server gets impatient and closes the connection.
TEST_F(HttpNetworkTransactionTest, BasicAuthKeepAliveImpatientServer) {
  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("http://www.example.org/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));

  MockWrite data_writes1[] = {
      MockWrite(
          "GET / HTTP/1.1\r\n"
          "Host: www.example.org\r\n"
          "Connection: keep-alive\r\n\r\n"),
      // This simulates the seemingly successful write to a closed connection
      // if the bug is not fixed.
      MockWrite(
          "GET / HTTP/1.1\r\n"
          "Host: www.example.org\r\n"
          "Connection: keep-alive\r\n"
          "Authorization: Basic Zm9vOmJhcg==\r\n\r\n"),
  };

  MockRead data_reads1[] = {
    MockRead("HTTP/1.1 401 Unauthorized\r\n"),
    MockRead("WWW-Authenticate: Basic realm=\"MyRealm1\"\r\n"),
    MockRead("Content-Type: text/html; charset=iso-8859-1\r\n"),
    MockRead("Content-Length: 14\r\n\r\n"),
    // Tell MockTCPClientSocket to simulate the server closing the connection.
    MockRead(SYNCHRONOUS, ERR_TEST_PEER_CLOSE_AFTER_NEXT_MOCK_READ),
    MockRead("Unauthorized\r\n"),
    MockRead(SYNCHRONOUS, OK),  // The server closes the connection.
  };

  // After calling trans.RestartWithAuth(), this is the request we should
  // be issuing -- the final header line contains the credentials.
  MockWrite data_writes2[] = {
      MockWrite(
          "GET / HTTP/1.1\r\n"
          "Host: www.example.org\r\n"
          "Connection: keep-alive\r\n"
          "Authorization: Basic Zm9vOmJhcg==\r\n\r\n"),
  };

  // Lastly, the server responds with the actual content.
  MockRead data_reads2[] = {
    MockRead("HTTP/1.1 200 OK\r\n"),
    MockRead("Content-Type: text/html; charset=iso-8859-1\r\n"),
    MockRead("Content-Length: 5\r\n\r\n"),
    MockRead("hello"),
  };

  StaticSocketDataProvider data1(data_reads1, data_writes1);
  StaticSocketDataProvider data2(data_reads2, data_writes2);
  session_deps_.socket_factory->AddSocketDataProvider(&data1);
  session_deps_.socket_factory->AddSocketDataProvider(&data2);

  TestCompletionCallback callback1;

  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());
  int rv = trans.Start(&request, callback1.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback1.WaitForResult();
  EXPECT_THAT(rv, IsOk());

  const HttpResponseInfo* response = trans.GetResponseInfo();
  ASSERT_TRUE(response);
  EXPECT_TRUE(CheckBasicServerAuth(response->auth_challenge));

  TestCompletionCallback callback2;

  rv = trans.RestartWithAuth(AuthCredentials(kFoo, kBar), callback2.callback());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback2.WaitForResult();
  EXPECT_THAT(rv, IsOk());

  response = trans.GetResponseInfo();
  ASSERT_TRUE(response);
  EXPECT_FALSE(response->auth_challenge.has_value());
  EXPECT_EQ(5, response->headers->GetContentLength());
}

// Test the request-challenge-retry sequence for basic auth, over a connection
// that requires a restart when setting up an SSL tunnel.
TEST_F(HttpNetworkTransactionTest, BasicAuthProxyNoKeepAliveHttp10) {
  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("https://www.example.org/");
  // when the no authentication data flag is set.
  request.privacy_mode = PRIVACY_MODE_ENABLED;
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  // Configure against proxy server "myproxy:70".
  session_deps_.proxy_resolution_service =
      ConfiguredProxyResolutionService::CreateFixedFromPacResultForTest(
          "PROXY myproxy:70", TRAFFIC_ANNOTATION_FOR_TESTS);
  RecordingNetLogObserver net_log_observer;
  NetLogWithSource net_log_with_source =
      NetLogWithSource::Make(NetLogSourceType::NONE);
  session_deps_.net_log = NetLog::Get();
  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));

  // Since we have proxy, should try to establish tunnel.
  MockWrite data_writes1[] = {
      MockWrite("CONNECT www.example.org:443 HTTP/1.1\r\n"
                "Host: www.example.org:443\r\n"
                "Proxy-Connection: keep-alive\r\n\r\n"),
  };

  // The proxy responds to the connect with a 407, using a non-persistent
  // connection.
  MockRead data_reads1[] = {
      // No credentials.
      MockRead("HTTP/1.0 407 Proxy Authentication Required\r\n"),
      MockRead("Proxy-Authenticate: Basic realm=\"MyRealm1\"\r\n\r\n"),
  };

  // Since the first connection couldn't be reused, need to establish another
  // once given credentials.
  MockWrite data_writes2[] = {
      // After calling trans->RestartWithAuth(), this is the request we should
      // be issuing -- the final header line contains the credentials.
      MockWrite("CONNECT www.example.org:443 HTTP/1.1\r\n"
                "Host: www.example.org:443\r\n"
                "Proxy-Connection: keep-alive\r\n"
                "Proxy-Authorization: Basic Zm9vOmJhcg==\r\n\r\n"),

      MockWrite("GET / HTTP/1.1\r\n"
                "Host: www.example.org\r\n"
                "Connection: keep-alive\r\n\r\n"),
  };

  MockRead data_reads2[] = {
      MockRead("HTTP/1.0 200 Connection Established\r\n\r\n"),

      MockRead("HTTP/1.1 200 OK\r\n"),
      MockRead("Content-Type: text/html; charset=iso-8859-1\r\n"),
      MockRead("Content-Length: 5\r\n\r\n"),
      MockRead(SYNCHRONOUS, "hello"),
  };

  StaticSocketDataProvider data1(data_reads1, data_writes1);
  session_deps_.socket_factory->AddSocketDataProvider(&data1);
  StaticSocketDataProvider data2(data_reads2, data_writes2);
  session_deps_.socket_factory->AddSocketDataProvider(&data2);
  SSLSocketDataProvider ssl(ASYNC, OK);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl);

  TestCompletionCallback callback1;
  ConnectedHandler connected_handler;

  auto trans =
      std::make_unique<HttpNetworkTransaction>(DEFAULT_PRIORITY, session.get());

  trans->SetConnectedCallback(connected_handler.Callback());

  int rv = trans->Start(&request, callback1.callback(), net_log_with_source);
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback1.WaitForResult();
  EXPECT_THAT(rv, IsOk());
  auto entries = net_log_observer.GetEntries();
  size_t pos = ExpectLogContainsSomewhere(
      entries, 0, NetLogEventType::HTTP_TRANSACTION_SEND_TUNNEL_HEADERS,
      NetLogEventPhase::NONE);
  ExpectLogContainsSomewhere(
      entries, pos,
      NetLogEventType::HTTP_TRANSACTION_READ_TUNNEL_RESPONSE_HEADERS,
      NetLogEventPhase::NONE);

  // TODO(crbug.com/986744): Fix handling of OnConnected() when proxy
  // authentication is required. We should notify the callback that a connection
  // was established, even though the stream might not be ready for us to send
  // data through it.
  EXPECT_THAT(connected_handler.transports(), IsEmpty());

  const HttpResponseInfo* response = trans->GetResponseInfo();
  ASSERT_TRUE(response);
  EXPECT_FALSE(response->headers->IsKeepAlive());
  ASSERT_TRUE(response->headers);
  EXPECT_EQ(407, response->headers->response_code());
  EXPECT_TRUE(HttpVersion(1, 0) == response->headers->GetHttpVersion());
  EXPECT_TRUE(CheckBasicProxyAuth(response->auth_challenge));

  LoadTimingInfo load_timing_info;
  // CONNECT requests and responses are handled at the connect job level, so
  // the transaction does not yet have a connection.
  EXPECT_FALSE(trans->GetLoadTimingInfo(&load_timing_info));

  TestCompletionCallback callback2;

  rv =
      trans->RestartWithAuth(AuthCredentials(kFoo, kBar), callback2.callback());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback2.WaitForResult();
  EXPECT_THAT(rv, IsOk());

  response = trans->GetResponseInfo();
  ASSERT_TRUE(response);

  EXPECT_TRUE(response->headers->IsKeepAlive());
  EXPECT_EQ(200, response->headers->response_code());
  EXPECT_EQ(5, response->headers->GetContentLength());
  EXPECT_TRUE(HttpVersion(1, 1) == response->headers->GetHttpVersion());

  TransportInfo expected_transport;
  expected_transport.type = TransportType::kProxied;
  expected_transport.endpoint = IPEndPoint(IPAddress::IPv4Localhost(), 70);
  EXPECT_THAT(connected_handler.transports(), ElementsAre(expected_transport));

  // Check that credentials were successfully cached, with the right target.
  HttpAuthCache::Entry* entry = session->http_auth_cache()->Lookup(
      url::SchemeHostPort(url::SchemeHostPort(GURL("http://myproxy:70"))),
      HttpAuth::AUTH_PROXY, "MyRealm1", HttpAuth::AUTH_SCHEME_BASIC,
      NetworkIsolationKey());
  ASSERT_TRUE(entry);
  ASSERT_EQ(kFoo, entry->credentials().username());
  ASSERT_EQ(kBar, entry->credentials().password());

  // The password prompt info should not be set.
  EXPECT_FALSE(response->auth_challenge.has_value());

  EXPECT_TRUE(trans->GetLoadTimingInfo(&load_timing_info));
  TestLoadTimingNotReusedWithPac(load_timing_info,
                                 CONNECT_TIMING_HAS_SSL_TIMES);

  trans.reset();
  session->CloseAllConnections(ERR_FAILED, "Very good reason");
}

// Test the request-challenge-retry sequence for basic auth, over a connection
// that requires a restart when setting up an SSL tunnel.
TEST_F(HttpNetworkTransactionTest, BasicAuthProxyNoKeepAliveHttp11) {
  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("https://www.example.org/");
  // when the no authentication data flag is set.
  request.privacy_mode = PRIVACY_MODE_ENABLED;
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  // Configure against proxy server "myproxy:70".
  session_deps_.proxy_resolution_service =
      ConfiguredProxyResolutionService::CreateFixedFromPacResultForTest(
          "PROXY myproxy:70", TRAFFIC_ANNOTATION_FOR_TESTS);
  RecordingNetLogObserver net_log_observer;
  session_deps_.net_log = NetLog::Get();
  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));

  // Since we have proxy, should try to establish tunnel.
  MockWrite data_writes1[] = {
      MockWrite("CONNECT www.example.org:443 HTTP/1.1\r\n"
                "Host: www.example.org:443\r\n"
                "Proxy-Connection: keep-alive\r\n\r\n"),
  };

  // The proxy responds to the connect with a 407, using a non-persistent
  // connection.
  MockRead data_reads1[] = {
      // No credentials.
      MockRead("HTTP/1.1 407 Proxy Authentication Required\r\n"),
      MockRead("Proxy-Authenticate: Basic realm=\"MyRealm1\"\r\n"),
      MockRead("Proxy-Connection: close\r\n\r\n"),
  };

  MockWrite data_writes2[] = {
      // After calling trans->RestartWithAuth(), this is the request we should
      // be issuing -- the final header line contains the credentials.
      MockWrite("CONNECT www.example.org:443 HTTP/1.1\r\n"
                "Host: www.example.org:443\r\n"
                "Proxy-Connection: keep-alive\r\n"
                "Proxy-Authorization: Basic Zm9vOmJhcg==\r\n\r\n"),

      MockWrite("GET / HTTP/1.1\r\n"
                "Host: www.example.org\r\n"
                "Connection: keep-alive\r\n\r\n"),
  };

  MockRead data_reads2[] = {
      MockRead("HTTP/1.1 200 Connection Established\r\n\r\n"),

      MockRead("HTTP/1.1 200 OK\r\n"),
      MockRead("Content-Type: text/html; charset=iso-8859-1\r\n"),
      MockRead("Content-Length: 5\r\n\r\n"),
      MockRead(SYNCHRONOUS, "hello"),
  };

  StaticSocketDataProvider data1(data_reads1, data_writes1);
  session_deps_.socket_factory->AddSocketDataProvider(&data1);
  StaticSocketDataProvider data2(data_reads2, data_writes2);
  session_deps_.socket_factory->AddSocketDataProvider(&data2);
  SSLSocketDataProvider ssl(ASYNC, OK);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl);

  ConnectedHandler connected_handler;
  TestCompletionCallback callback1;

  auto trans =
      std::make_unique<HttpNetworkTransaction>(DEFAULT_PRIORITY, session.get());

  trans->SetConnectedCallback(connected_handler.Callback());

  int rv = trans->Start(&request, callback1.callback(),
                        NetLogWithSource::Make(NetLogSourceType::NONE));
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback1.WaitForResult();
  EXPECT_THAT(rv, IsOk());
  auto entries = net_log_observer.GetEntries();
  size_t pos = ExpectLogContainsSomewhere(
      entries, 0, NetLogEventType::HTTP_TRANSACTION_SEND_TUNNEL_HEADERS,
      NetLogEventPhase::NONE);
  ExpectLogContainsSomewhere(
      entries, pos,
      NetLogEventType::HTTP_TRANSACTION_READ_TUNNEL_RESPONSE_HEADERS,
      NetLogEventPhase::NONE);

  const HttpResponseInfo* response = trans->GetResponseInfo();
  ASSERT_TRUE(response);
  EXPECT_FALSE(response->headers->IsKeepAlive());
  ASSERT_TRUE(response->headers);
  EXPECT_EQ(407, response->headers->response_code());
  EXPECT_TRUE(HttpVersion(1, 1) == response->headers->GetHttpVersion());
  EXPECT_TRUE(CheckBasicProxyAuth(response->auth_challenge));
  EXPECT_EQ(PacResultElementToProxyServer("PROXY myproxy:70"),
            response->proxy_server);

  // TODO(crbug.com/986744): Fix handling of OnConnected() when proxy
  // authentication is required. We should notify the callback that a connection
  // was established, even though the stream might not be ready for us to send
  // data through it.
  EXPECT_THAT(connected_handler.transports(), IsEmpty());

  LoadTimingInfo load_timing_info;
  // CONNECT requests and responses are handled at the connect job level, so
  // the transaction does not yet have a connection.
  EXPECT_FALSE(trans->GetLoadTimingInfo(&load_timing_info));

  TestCompletionCallback callback2;

  rv = trans->RestartWithAuth(
      AuthCredentials(kFoo, kBar), callback2.callback());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback2.WaitForResult();
  EXPECT_THAT(rv, IsOk());

  response = trans->GetResponseInfo();
  ASSERT_TRUE(response);

  EXPECT_TRUE(response->headers->IsKeepAlive());
  EXPECT_EQ(200, response->headers->response_code());
  EXPECT_EQ(5, response->headers->GetContentLength());
  EXPECT_TRUE(HttpVersion(1, 1) == response->headers->GetHttpVersion());
  EXPECT_EQ(PacResultElementToProxyServer("PROXY myproxy:70"),
            response->proxy_server);

  TransportInfo expected_transport;
  expected_transport.type = TransportType::kProxied;
  expected_transport.endpoint = IPEndPoint(IPAddress::IPv4Localhost(), 70);
  EXPECT_THAT(connected_handler.transports(), ElementsAre(expected_transport));

  // The password prompt info should not be set.
  EXPECT_FALSE(response->auth_challenge.has_value());

  EXPECT_TRUE(trans->GetLoadTimingInfo(&load_timing_info));
  TestLoadTimingNotReusedWithPac(load_timing_info,
                                 CONNECT_TIMING_HAS_SSL_TIMES);

  trans.reset();
  session->CloseAllConnections(ERR_FAILED, "Very good reason");
}

// Test the request-challenge-retry sequence for basic auth, over a keep-alive
// proxy connection with HTTP/1.0 responses, when setting up an SSL tunnel.
TEST_F(HttpNetworkTransactionTest, BasicAuthProxyKeepAliveHttp10) {
  // On the second pass, the body read of the auth challenge is synchronous, so
  // IsConnectedAndIdle returns false.  The socket should still be drained and
  // reused.  See http://crbug.com/544255.
  for (int i = 0; i < 2; ++i) {
    HttpRequestInfo request;
    request.method = "GET";
    request.url = GURL("https://www.example.org/");
    // Ensure that proxy authentication is attempted even
    // when the no authentication data flag is set.
    request.privacy_mode = PRIVACY_MODE_ENABLED;
    request.traffic_annotation =
        net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

    // Configure against proxy server "myproxy:70".
    session_deps_.proxy_resolution_service =
        ConfiguredProxyResolutionService::CreateFixedForTest(
            "myproxy:70", TRAFFIC_ANNOTATION_FOR_TESTS);
    RecordingNetLogObserver net_log_observer;
    session_deps_.net_log = NetLog::Get();
    std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));

    HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

    // Since we have proxy, should try to establish tunnel.
    MockWrite data_writes1[] = {
        MockWrite(ASYNC, 0,
                  "CONNECT www.example.org:443 HTTP/1.1\r\n"
                  "Host: www.example.org:443\r\n"
                  "Proxy-Connection: keep-alive\r\n\r\n"),

        // After calling trans.RestartWithAuth(), this is the request we should
        // be issuing -- the final header line contains the credentials.
        MockWrite(ASYNC, 3,
                  "CONNECT www.example.org:443 HTTP/1.1\r\n"
                  "Host: www.example.org:443\r\n"
                  "Proxy-Connection: keep-alive\r\n"
                  "Proxy-Authorization: Basic Zm9vOmJheg==\r\n\r\n"),
    };

    // The proxy responds to the connect with a 407, using a persistent
    // connection. (Since it's HTTP/1.0, keep-alive has to be explicit.)
    MockRead data_reads1[] = {
        // No credentials.
        MockRead(ASYNC, 1,
                 "HTTP/1.0 407 Proxy Authentication Required\r\n"
                 "Proxy-Authenticate: Basic realm=\"MyRealm1\"\r\n"
                 "Proxy-Connection: keep-alive\r\n"
                 "Content-Length: 10\r\n\r\n"),
        MockRead(i == 0 ? ASYNC : SYNCHRONOUS, 2, "0123456789"),

        // Wrong credentials (wrong password).
        MockRead(ASYNC, 4,
                 "HTTP/1.0 407 Proxy Authentication Required\r\n"
                 "Proxy-Authenticate: Basic realm=\"MyRealm1\"\r\n"
                 "Proxy-Connection: keep-alive\r\n"
                 "Content-Length: 10\r\n\r\n"),
        // No response body because the test stops reading here.
        MockRead(SYNCHRONOUS, ERR_UNEXPECTED, 5),
    };

    SequencedSocketData data1(data_reads1, data_writes1);
    data1.set_busy_before_sync_reads(true);
    session_deps_.socket_factory->AddSocketDataProvider(&data1);

    TestCompletionCallback callback1;

    int rv = trans.Start(&request, callback1.callback(),
                         NetLogWithSource::Make(NetLogSourceType::NONE));
    EXPECT_THAT(callback1.GetResult(rv), IsOk());

    auto entries = net_log_observer.GetEntries();
    size_t pos = ExpectLogContainsSomewhere(
        entries, 0, NetLogEventType::HTTP_TRANSACTION_SEND_TUNNEL_HEADERS,
        NetLogEventPhase::NONE);
    ExpectLogContainsSomewhere(
        entries, pos,
        NetLogEventType::HTTP_TRANSACTION_READ_TUNNEL_RESPONSE_HEADERS,
        NetLogEventPhase::NONE);

    const HttpResponseInfo* response = trans.GetResponseInfo();
    ASSERT_TRUE(response);
    ASSERT_TRUE(response->headers);
    EXPECT_TRUE(response->headers->IsKeepAlive());
    EXPECT_EQ(407, response->headers->response_code());
    EXPECT_EQ(10, response->headers->GetContentLength());
    EXPECT_TRUE(HttpVersion(1, 0) == response->headers->GetHttpVersion());
    EXPECT_TRUE(CheckBasicProxyAuth(response->auth_challenge));

    TestCompletionCallback callback2;

    // Wrong password (should be "bar").
    rv = trans.RestartWithAuth(AuthCredentials(kFoo, kBaz),
                               callback2.callback());
    EXPECT_THAT(callback2.GetResult(rv), IsOk());

    response = trans.GetResponseInfo();
    ASSERT_TRUE(response);
    ASSERT_TRUE(response->headers);
    EXPECT_TRUE(response->headers->IsKeepAlive());
    EXPECT_EQ(407, response->headers->response_code());
    EXPECT_EQ(10, response->headers->GetContentLength());
    EXPECT_TRUE(HttpVersion(1, 0) == response->headers->GetHttpVersion());
    EXPECT_TRUE(CheckBasicProxyAuth(response->auth_challenge));

    // Flush the idle socket before the NetLog and HttpNetworkTransaction go
    // out of scope.
    session->CloseAllConnections(ERR_FAILED, "Very good reason");
  }
}

// Test the request-challenge-retry sequence for basic auth, over a keep-alive
// proxy connection with HTTP/1.1 responses, when setting up an SSL tunnel.
TEST_F(HttpNetworkTransactionTest, BasicAuthProxyKeepAliveHttp11) {
  // On the second pass, the body read of the auth challenge is synchronous, so
  // IsConnectedAndIdle returns false.  The socket should still be drained and
  // reused.  See http://crbug.com/544255.
  for (int i = 0; i < 2; ++i) {
    HttpRequestInfo request;
    request.method = "GET";
    request.url = GURL("https://www.example.org/");
    // Ensure that proxy authentication is attempted even
    // when the no authentication data flag is set.
    request.privacy_mode = PRIVACY_MODE_ENABLED;
    request.traffic_annotation =
        net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

    // Configure against proxy server "myproxy:70".
    session_deps_.proxy_resolution_service =
        ConfiguredProxyResolutionService::CreateFixedForTest(
            "myproxy:70", TRAFFIC_ANNOTATION_FOR_TESTS);
    RecordingNetLogObserver net_log_observer;
    session_deps_.net_log = NetLog::Get();
    std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));

    HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

    // Since we have proxy, should try to establish tunnel.
    MockWrite data_writes1[] = {
        MockWrite(ASYNC, 0,
                  "CONNECT www.example.org:443 HTTP/1.1\r\n"
                  "Host: www.example.org:443\r\n"
                  "Proxy-Connection: keep-alive\r\n\r\n"),

        // After calling trans.RestartWithAuth(), this is the request we should
        // be issuing -- the final header line contains the credentials.
        MockWrite(ASYNC, 3,
                  "CONNECT www.example.org:443 HTTP/1.1\r\n"
                  "Host: www.example.org:443\r\n"
                  "Proxy-Connection: keep-alive\r\n"
                  "Proxy-Authorization: Basic Zm9vOmJheg==\r\n\r\n"),
    };

    // The proxy responds to the connect with a 407, using a persistent
    // connection. (Since it's HTTP/1.0, keep-alive has to be explicit.)
    MockRead data_reads1[] = {
        // No credentials.
        MockRead(ASYNC, 1,
                 "HTTP/1.1 407 Proxy Authentication Required\r\n"
                 "Proxy-Authenticate: Basic realm=\"MyRealm1\"\r\n"
                 "Content-Length: 10\r\n\r\n"),
        MockRead(i == 0 ? ASYNC : SYNCHRONOUS, 2, "0123456789"),

        // Wrong credentials (wrong password).
        MockRead(ASYNC, 4,
                 "HTTP/1.1 407 Proxy Authentication Required\r\n"
                 "Proxy-Authenticate: Basic realm=\"MyRealm1\"\r\n"
                 "Content-Length: 10\r\n\r\n"),
        // No response body because the test stops reading here.
        MockRead(SYNCHRONOUS, ERR_UNEXPECTED, 5),
    };

    SequencedSocketData data1(data_reads1, data_writes1);
    data1.set_busy_before_sync_reads(true);
    session_deps_.socket_factory->AddSocketDataProvider(&data1);

    TestCompletionCallback callback1;

    int rv = trans.Start(&request, callback1.callback(),
                         NetLogWithSource::Make(NetLogSourceType::NONE));
    EXPECT_THAT(callback1.GetResult(rv), IsOk());

    auto entries = net_log_observer.GetEntries();
    size_t pos = ExpectLogContainsSomewhere(
        entries, 0, NetLogEventType::HTTP_TRANSACTION_SEND_TUNNEL_HEADERS,
        NetLogEventPhase::NONE);
    ExpectLogContainsSomewhere(
        entries, pos,
        NetLogEventType::HTTP_TRANSACTION_READ_TUNNEL_RESPONSE_HEADERS,
        NetLogEventPhase::NONE);

    const HttpResponseInfo* response = trans.GetResponseInfo();
    ASSERT_TRUE(response);
    ASSERT_TRUE(response->headers);
    EXPECT_TRUE(response->headers->IsKeepAlive());
    EXPECT_EQ(407, response->headers->response_code());
    EXPECT_EQ(10, response->headers->GetContentLength());
    EXPECT_TRUE(HttpVersion(1, 1) == response->headers->GetHttpVersion());
    EXPECT_TRUE(CheckBasicProxyAuth(response->auth_challenge));
    EXPECT_FALSE(response->did_use_http_auth);
    EXPECT_EQ(PacResultElementToProxyServer("PROXY myproxy:70"),
              response->proxy_server);

    TestCompletionCallback callback2;

    // Wrong password (should be "bar").
    rv = trans.RestartWithAuth(AuthCredentials(kFoo, kBaz),
                               callback2.callback());
    EXPECT_THAT(callback2.GetResult(rv), IsOk());

    response = trans.GetResponseInfo();
    ASSERT_TRUE(response);
    ASSERT_TRUE(response->headers);
    EXPECT_TRUE(response->headers->IsKeepAlive());
    EXPECT_EQ(407, response->headers->response_code());
    EXPECT_EQ(10, response->headers->GetContentLength());
    EXPECT_TRUE(HttpVersion(1, 1) == response->headers->GetHttpVersion());
    EXPECT_TRUE(CheckBasicProxyAuth(response->auth_challenge));
    EXPECT_TRUE(response->did_use_http_auth);
    EXPECT_EQ(PacResultElementToProxyServer("PROXY myproxy:70"),
              response->proxy_server);

    // Flush the idle socket before the NetLog and HttpNetworkTransaction go
    // out of scope.
    session->CloseAllConnections(ERR_FAILED, "Very good reason");
  }
}

// Test the request-challenge-retry sequence for basic auth, over a keep-alive
// proxy connection with HTTP/1.1 responses, when setting up an SSL tunnel, in
// the case the server sends extra data on the original socket, so it can't be
// reused.
TEST_F(HttpNetworkTransactionTest, BasicAuthProxyKeepAliveExtraData) {
  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("https://www.example.org/");
  // when the no authentication data flag is set.
  request.privacy_mode = PRIVACY_MODE_ENABLED;
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  // Configure against proxy server "myproxy:70".
  session_deps_.proxy_resolution_service =
      ConfiguredProxyResolutionService::CreateFixedFromPacResultForTest(
          "PROXY myproxy:70", TRAFFIC_ANNOTATION_FOR_TESTS);
  RecordingNetLogObserver net_log_observer;
  session_deps_.net_log = NetLog::Get();
  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));

  // Since we have proxy, should try to establish tunnel.
  MockWrite data_writes1[] = {
      MockWrite(ASYNC, 0,
                "CONNECT www.example.org:443 HTTP/1.1\r\n"
                "Host: www.example.org:443\r\n"
                "Proxy-Connection: keep-alive\r\n\r\n"),
  };

  // The proxy responds to the connect with a 407, using a persistent, but sends
  // extra data, so the socket cannot be reused.
  MockRead data_reads1[] = {
      // No credentials.
      MockRead(ASYNC, 1,
               "HTTP/1.1 407 Proxy Authentication Required\r\n"
               "Proxy-Authenticate: Basic realm=\"MyRealm1\"\r\n"
               "Content-Length: 10\r\n\r\n"),
      MockRead(SYNCHRONOUS, 2, "0123456789"),
      MockRead(SYNCHRONOUS, 3, "I'm broken!"),
  };

  MockWrite data_writes2[] = {
      // After calling trans->RestartWithAuth(), this is the request we should
      // be issuing -- the final header line contains the credentials.
      MockWrite(ASYNC, 0,
                "CONNECT www.example.org:443 HTTP/1.1\r\n"
                "Host: www.example.org:443\r\n"
                "Proxy-Connection: keep-alive\r\n"
                "Proxy-Authorization: Basic Zm9vOmJhcg==\r\n\r\n"),

      MockWrite(ASYNC, 2,
                "GET / HTTP/1.1\r\n"
                "Host: www.example.org\r\n"
                "Connection: keep-alive\r\n\r\n"),
  };

  MockRead data_reads2[] = {
      MockRead(ASYNC, 1, "HTTP/1.1 200 Connection Established\r\n\r\n"),

      MockRead(ASYNC, 3,
               "HTTP/1.1 200 OK\r\n"
               "Content-Type: text/html; charset=iso-8859-1\r\n"
               "Content-Length: 5\r\n\r\n"),
      // No response body because the test stops reading here.
      MockRead(SYNCHRONOUS, ERR_UNEXPECTED, 4),
  };

  SequencedSocketData data1(data_reads1, data_writes1);
  data1.set_busy_before_sync_reads(true);
  session_deps_.socket_factory->AddSocketDataProvider(&data1);
  SequencedSocketData data2(data_reads2, data_writes2);
  session_deps_.socket_factory->AddSocketDataProvider(&data2);
  SSLSocketDataProvider ssl(ASYNC, OK);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl);

  TestCompletionCallback callback1;

  auto trans =
      std::make_unique<HttpNetworkTransaction>(DEFAULT_PRIORITY, session.get());

  int rv = trans->Start(&request, callback1.callback(),
                        NetLogWithSource::Make(NetLogSourceType::NONE));
  EXPECT_THAT(callback1.GetResult(rv), IsOk());

  auto entries = net_log_observer.GetEntries();
  size_t pos = ExpectLogContainsSomewhere(
      entries, 0, NetLogEventType::HTTP_TRANSACTION_SEND_TUNNEL_HEADERS,
      NetLogEventPhase::NONE);
  ExpectLogContainsSomewhere(
      entries, pos,
      NetLogEventType::HTTP_TRANSACTION_READ_TUNNEL_RESPONSE_HEADERS,
      NetLogEventPhase::NONE);

  const HttpResponseInfo* response = trans->GetResponseInfo();
  ASSERT_TRUE(response);
  ASSERT_TRUE(response->headers);
  EXPECT_TRUE(response->headers->IsKeepAlive());
  EXPECT_EQ(407, response->headers->response_code());
  EXPECT_TRUE(HttpVersion(1, 1) == response->headers->GetHttpVersion());
  EXPECT_TRUE(CheckBasicProxyAuth(response->auth_challenge));

  LoadTimingInfo load_timing_info;
  // CONNECT requests and responses are handled at the connect job level, so
  // the transaction does not yet have a connection.
  EXPECT_FALSE(trans->GetLoadTimingInfo(&load_timing_info));

  TestCompletionCallback callback2;

  rv =
      trans->RestartWithAuth(AuthCredentials(kFoo, kBar), callback2.callback());
  EXPECT_THAT(callback2.GetResult(rv), IsOk());

  EXPECT_TRUE(response->headers->IsKeepAlive());
  EXPECT_EQ(200, response->headers->response_code());
  EXPECT_EQ(5, response->headers->GetContentLength());
  EXPECT_TRUE(HttpVersion(1, 1) == response->headers->GetHttpVersion());

  // The password prompt info should not be set.
  EXPECT_FALSE(response->auth_challenge.has_value());

  EXPECT_TRUE(trans->GetLoadTimingInfo(&load_timing_info));
  TestLoadTimingNotReusedWithPac(load_timing_info,
                                 CONNECT_TIMING_HAS_SSL_TIMES);

  trans.reset();
  session->CloseAllConnections(ERR_FAILED, "Very good reason");
}

// Test the case a proxy closes a socket while the challenge body is being
// drained.
TEST_F(HttpNetworkTransactionTest, BasicAuthProxyKeepAliveHangupDuringBody) {
  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("https://www.example.org/");
  // Ensure that proxy authentication is attempted even
  // when the no authentication data flag is set.
  request.privacy_mode = PRIVACY_MODE_ENABLED;
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  // Configure against proxy server "myproxy:70".
  session_deps_.proxy_resolution_service =
      ConfiguredProxyResolutionService::CreateFixedForTest(
          "myproxy:70", TRAFFIC_ANNOTATION_FOR_TESTS);
  std::unique_ptr<HttpNetworkSession> session = CreateSession(&session_deps_);

  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

  // Since we have proxy, should try to establish tunnel.
  MockWrite data_writes1[] = {
      MockWrite("CONNECT www.example.org:443 HTTP/1.1\r\n"
                "Host: www.example.org:443\r\n"
                "Proxy-Connection: keep-alive\r\n\r\n"),
  };

  // The proxy responds to the connect with a 407, using a persistent
  // connection.
  MockRead data_reads1[] = {
      // No credentials.
      MockRead("HTTP/1.1 407 Proxy Authentication Required\r\n"),
      MockRead("Proxy-Authenticate: Basic realm=\"MyRealm1\"\r\n"),
      MockRead("Content-Length: 10\r\n\r\n"), MockRead("spam!"),
      // Server hands up in the middle of the body.
      MockRead(ASYNC, ERR_CONNECTION_CLOSED),
  };

  MockWrite data_writes2[] = {
      // After calling trans.RestartWithAuth(), this is the request we should
      // be issuing -- the final header line contains the credentials.
      MockWrite("CONNECT www.example.org:443 HTTP/1.1\r\n"
                "Host: www.example.org:443\r\n"
                "Proxy-Connection: keep-alive\r\n"
                "Proxy-Authorization: Basic Zm9vOmJhcg==\r\n\r\n"),

      MockWrite("GET / HTTP/1.1\r\n"
                "Host: www.example.org\r\n"
                "Connection: keep-alive\r\n\r\n"),
  };

  MockRead data_reads2[] = {
      MockRead("HTTP/1.1 200 Connection Established\r\n\r\n"),

      MockRead("HTTP/1.1 200 OK\r\n"),
      MockRead("Content-Type: text/html; charset=iso-8859-1\r\n"),
      MockRead("Content-Length: 5\r\n\r\n"),
      MockRead(SYNCHRONOUS, "hello"),
  };

  StaticSocketDataProvider data1(data_reads1, data_writes1);
  session_deps_.socket_factory->AddSocketDataProvider(&data1);
  StaticSocketDataProvider data2(data_reads2, data_writes2);
  session_deps_.socket_factory->AddSocketDataProvider(&data2);
  SSLSocketDataProvider ssl(ASYNC, OK);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl);

  TestCompletionCallback callback;

  int rv = trans.Start(&request, callback.callback(), NetLogWithSource());
  EXPECT_THAT(callback.GetResult(rv), IsOk());

  const HttpResponseInfo* response = trans.GetResponseInfo();
  ASSERT_TRUE(response);
  ASSERT_TRUE(response->headers);
  EXPECT_TRUE(response->headers->IsKeepAlive());
  EXPECT_EQ(407, response->headers->response_code());
  EXPECT_TRUE(CheckBasicProxyAuth(response->auth_challenge));

  rv = trans.RestartWithAuth(AuthCredentials(kFoo, kBar), callback.callback());
  EXPECT_THAT(callback.GetResult(rv), IsOk());

  response = trans.GetResponseInfo();
  ASSERT_TRUE(response);
  ASSERT_TRUE(response->headers);
  EXPECT_TRUE(response->headers->IsKeepAlive());
  EXPECT_EQ(200, response->headers->response_code());
  std::string body;
  EXPECT_THAT(ReadTransaction(&trans, &body), IsOk());
  EXPECT_EQ("hello", body);
}

// Test that we don't read the response body when we fail to establish a tunnel,
// even if the user cancels the proxy's auth attempt.
TEST_F(HttpNetworkTransactionTest, BasicAuthProxyCancelTunnel) {
  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("https://www.example.org/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  // Configure against proxy server "myproxy:70".
  session_deps_.proxy_resolution_service =
      ConfiguredProxyResolutionService::CreateFixedForTest(
          "myproxy:70", TRAFFIC_ANNOTATION_FOR_TESTS);

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));

  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

  // Since we have proxy, should try to establish tunnel.
  MockWrite data_writes[] = {
      MockWrite("CONNECT www.example.org:443 HTTP/1.1\r\n"
                "Host: www.example.org:443\r\n"
                "Proxy-Connection: keep-alive\r\n\r\n"),
  };

  // The proxy responds to the connect with a 407.
  MockRead data_reads[] = {
      MockRead("HTTP/1.1 407 Proxy Authentication Required\r\n"),
      MockRead("Proxy-Authenticate: Basic realm=\"MyRealm1\"\r\n"),
      MockRead("Content-Length: 10\r\n\r\n"),
      MockRead("0123456789"),
      MockRead(SYNCHRONOUS, ERR_UNEXPECTED),
  };

  StaticSocketDataProvider data(data_reads, data_writes);
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  TestCompletionCallback callback;

  int rv = trans.Start(&request, callback.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback.WaitForResult();
  EXPECT_THAT(rv, IsOk());

  const HttpResponseInfo* response = trans.GetResponseInfo();
  ASSERT_TRUE(response);
  ASSERT_TRUE(response->headers);
  EXPECT_TRUE(response->headers->IsKeepAlive());
  EXPECT_EQ(407, response->headers->response_code());
  EXPECT_TRUE(HttpVersion(1, 1) == response->headers->GetHttpVersion());

  std::string response_data;
  rv = ReadTransaction(&trans, &response_data);
  EXPECT_THAT(rv, IsError(ERR_TUNNEL_CONNECTION_FAILED));

  // Flush the idle socket before the HttpNetworkTransaction goes out of scope.
  session->CloseAllConnections(ERR_FAILED, "Very good reason");
}

// Test the no-tunnel HTTP auth case where proxy and server origins and realms
// are the same, but the user/passwords are different. Serves to verify
// credentials are correctly separated based on HttpAuth::Target.
TEST_F(HttpNetworkTransactionTest, BasicAuthProxyMatchesServerAuthNoTunnel) {
  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("http://myproxy:70/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  // Proxy matches request URL.
  session_deps_.proxy_resolution_service =
      ConfiguredProxyResolutionService::CreateFixedFromPacResultForTest(
          "PROXY myproxy:70", TRAFFIC_ANNOTATION_FOR_TESTS);
  NetLogWithSource net_log_with_source =
      NetLogWithSource::Make(NetLogSourceType::NONE);
  session_deps_.net_log = NetLog::Get();
  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));

  MockWrite data_writes[] = {
      // Initial request gets a proxy auth challenge.
      MockWrite("GET http://myproxy:70/ HTTP/1.1\r\n"
                "Host: myproxy:70\r\n"
                "Proxy-Connection: keep-alive\r\n\r\n"),
      // Retry with proxy auth credentials, which will result in a server auth
      // challenge.
      MockWrite("GET http://myproxy:70/ HTTP/1.1\r\n"
                "Host: myproxy:70\r\n"
                "Proxy-Connection: keep-alive\r\n"
                "Proxy-Authorization: Basic Zm9vOmJhcg==\r\n\r\n"),
      // Retry with proxy and server auth credentials, which gets a response.
      MockWrite("GET http://myproxy:70/ HTTP/1.1\r\n"
                "Host: myproxy:70\r\n"
                "Proxy-Connection: keep-alive\r\n"
                "Proxy-Authorization: Basic Zm9vOmJhcg==\r\n"
                "Authorization: Basic Zm9vMjpiYXIy\r\n\r\n"),
      // A second request should preemptively send the correct proxy and server
      // auth headers.
      MockWrite("GET http://myproxy:70/ HTTP/1.1\r\n"
                "Host: myproxy:70\r\n"
                "Proxy-Connection: keep-alive\r\n"
                "Proxy-Authorization: Basic Zm9vOmJhcg==\r\n"
                "Authorization: Basic Zm9vMjpiYXIy\r\n\r\n"),
  };

  MockRead data_reads[] = {
      // Proxy auth challenge.
      MockRead("HTTP/1.0 407 Proxy Authentication Required\r\n"
               "Proxy-Connection: keep-alive\r\n"
               "Proxy-Authenticate: Basic realm=\"MyRealm1\"\r\n"
               "Content-Length: 0\r\n\r\n"),
      // Server auth challenge.
      MockRead("HTTP/1.0 401 Authentication Required\r\n"
               "Proxy-Connection: keep-alive\r\n"
               "WWW-Authenticate: Basic realm=\"MyRealm1\"\r\n"
               "Content-Length: 0\r\n\r\n"),
      // Response.
      MockRead("HTTP/1.1 200 OK\r\n"
               "Proxy-Connection: keep-alive\r\n"
               "Content-Length: 5\r\n\r\n"
               "hello"),
      // Response to second request.
      MockRead("HTTP/1.1 200 OK\r\n"
               "Proxy-Connection: keep-alive\r\n"
               "Content-Length: 2\r\n\r\n"
               "hi"),
  };

  StaticSocketDataProvider data(data_reads, data_writes);
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  TestCompletionCallback callback;

  auto trans =
      std::make_unique<HttpNetworkTransaction>(DEFAULT_PRIORITY, session.get());
  int rv = trans->Start(&request, callback.callback(), net_log_with_source);
  EXPECT_THAT(callback.GetResult(rv), IsOk());
  const HttpResponseInfo* response = trans->GetResponseInfo();
  ASSERT_TRUE(response);
  ASSERT_TRUE(response->headers);
  EXPECT_EQ(407, response->headers->response_code());
  EXPECT_TRUE(CheckBasicProxyAuth(response->auth_challenge));

  rv = trans->RestartWithAuth(AuthCredentials(kFoo, kBar), callback.callback());
  EXPECT_THAT(callback.GetResult(rv), IsOk());
  response = trans->GetResponseInfo();
  ASSERT_TRUE(response);
  EXPECT_EQ(401, response->headers->response_code());
  EXPECT_FALSE(response->auth_challenge->is_proxy);
  EXPECT_EQ("http://myproxy:70",
            response->auth_challenge->challenger.Serialize());
  EXPECT_EQ("MyRealm1", response->auth_challenge->realm);
  EXPECT_EQ(kBasicAuthScheme, response->auth_challenge->scheme);

  rv = trans->RestartWithAuth(AuthCredentials(kFoo2, kBar2),
                              callback.callback());
  EXPECT_THAT(callback.GetResult(rv), IsOk());
  response = trans->GetResponseInfo();
  ASSERT_TRUE(response);
  EXPECT_EQ(200, response->headers->response_code());
  // The password prompt info should not be set.
  EXPECT_FALSE(response->auth_challenge.has_value());

  std::string response_data;
  EXPECT_THAT(ReadTransaction(trans.get(), &response_data), IsOk());
  EXPECT_EQ("hello", response_data);

  // Check that the credentials were cached correctly.
  HttpAuthCache::Entry* entry = session->http_auth_cache()->Lookup(
      url::SchemeHostPort(url::SchemeHostPort(GURL("http://myproxy:70"))),
      HttpAuth::AUTH_PROXY, "MyRealm1", HttpAuth::AUTH_SCHEME_BASIC,
      NetworkIsolationKey());
  ASSERT_TRUE(entry);
  ASSERT_EQ(kFoo, entry->credentials().username());
  ASSERT_EQ(kBar, entry->credentials().password());
  entry = session->http_auth_cache()->Lookup(
      url::SchemeHostPort(url::SchemeHostPort(GURL("http://myproxy:70"))),
      HttpAuth::AUTH_SERVER, "MyRealm1", HttpAuth::AUTH_SCHEME_BASIC,
      NetworkIsolationKey());
  ASSERT_TRUE(entry);
  ASSERT_EQ(kFoo2, entry->credentials().username());
  ASSERT_EQ(kBar2, entry->credentials().password());

  // Make another request, which should automatically send the correct proxy and
  // server auth credentials and get another response.
  trans =
      std::make_unique<HttpNetworkTransaction>(DEFAULT_PRIORITY, session.get());
  rv = trans->Start(&request, callback.callback(), net_log_with_source);
  EXPECT_THAT(callback.GetResult(rv), IsOk());
  response = trans->GetResponseInfo();
  ASSERT_TRUE(response);
  EXPECT_EQ(200, response->headers->response_code());
  // The password prompt info should not be set.
  EXPECT_FALSE(response->auth_challenge.has_value());

  EXPECT_THAT(ReadTransaction(trans.get(), &response_data), IsOk());
  EXPECT_EQ("hi", response_data);

  trans.reset();
  session->CloseAllConnections(ERR_FAILED, "Very good reason");
}

// Test the no-tunnel HTTP auth case where proxy and server origins and realms
// are the same, but the user/passwords are different, and with different
// NetworkIsolationKeys. Sends one request with a NIK, response to both proxy
// and auth challenges, sends another request with another NIK, expecting only
// the proxy credentials to be cached, and thus sees only a server auth
// challenge. Then sends a request with the original NIK, expecting cached proxy
// and auth credentials that match the ones used in the first request.
//
// Serves to verify credentials are correctly separated based on
// HttpAuth::Target and NetworkIsolationKeys, but NetworkIsolationKey only
// affects server credentials, not proxy credentials.
TEST_F(HttpNetworkTransactionTest,
       BasicAuthProxyMatchesServerAuthWithNetworkIsolationKeyNoTunnel) {
  const SchemefulSite kSite1(GURL("https://foo.test/"));
  const net::NetworkIsolationKey kNetworkIsolationKey1(kSite1, kSite1);
  const SchemefulSite kSite2(GURL("https://bar.test/"));
  const net::NetworkIsolationKey kNetworkIsolationKey2(kSite2, kSite2);

  // This test would need to use a single socket without this option enabled.
  // Best to use this option when it would affect a test, as it will eventually
  // become the default behavior.
  base::test::ScopedFeatureList feature_list;
  feature_list.InitAndEnableFeature(
      features::kPartitionConnectionsByNetworkIsolationKey);

  // Proxy matches request URL.
  session_deps_.proxy_resolution_service =
      ConfiguredProxyResolutionService::CreateFixedFromPacResultForTest(
          "PROXY myproxy:70", TRAFFIC_ANNOTATION_FOR_TESTS);
  NetLogWithSource net_log_with_source =
      NetLogWithSource::Make(NetLogSourceType::NONE);

  session_deps_.net_log = NetLog::Get();
  session_deps_.key_auth_cache_server_entries_by_network_isolation_key = true;
  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));

  MockWrite data_writes[] = {
      // Initial request gets a proxy auth challenge.
      MockWrite("GET http://myproxy:70/ HTTP/1.1\r\n"
                "Host: myproxy:70\r\n"
                "Proxy-Connection: keep-alive\r\n\r\n"),
      // Retry with proxy auth credentials, which will result in a server auth
      // challenge.
      MockWrite("GET http://myproxy:70/ HTTP/1.1\r\n"
                "Host: myproxy:70\r\n"
                "Proxy-Connection: keep-alive\r\n"
                "Proxy-Authorization: Basic Zm9vOmJhcg==\r\n\r\n"),
      // Retry with proxy and server auth credentials, which gets a response.
      MockWrite("GET http://myproxy:70/ HTTP/1.1\r\n"
                "Host: myproxy:70\r\n"
                "Proxy-Connection: keep-alive\r\n"
                "Proxy-Authorization: Basic Zm9vOmJhcg==\r\n"
                "Authorization: Basic Zm9vMjpiYXIy\r\n\r\n"),
      // Another request to the same server and using the same NIK should
      // preemptively send the correct cached proxy and server
      // auth headers.
      MockWrite("GET http://myproxy:70/ HTTP/1.1\r\n"
                "Host: myproxy:70\r\n"
                "Proxy-Connection: keep-alive\r\n"
                "Proxy-Authorization: Basic Zm9vOmJhcg==\r\n"
                "Authorization: Basic Zm9vMjpiYXIy\r\n\r\n"),
  };

  MockRead data_reads[] = {
      // Proxy auth challenge.
      MockRead("HTTP/1.0 407 Proxy Authentication Required\r\n"
               "Proxy-Connection: keep-alive\r\n"
               "Proxy-Authenticate: Basic realm=\"MyRealm1\"\r\n"
               "Content-Length: 0\r\n\r\n"),
      // Server auth challenge.
      MockRead("HTTP/1.0 401 Authentication Required\r\n"
               "Proxy-Connection: keep-alive\r\n"
               "WWW-Authenticate: Basic realm=\"MyRealm1\"\r\n"
               "Content-Length: 0\r\n\r\n"),
      // Response.
      MockRead("HTTP/1.1 200 OK\r\n"
               "Proxy-Connection: keep-alive\r\n"
               "Content-Length: 5\r\n\r\n"
               "hello"),
      // Response to second request.
      MockRead("HTTP/1.1 200 OK\r\n"
               "Proxy-Connection: keep-alive\r\n"
               "Content-Length: 2\r\n\r\n"
               "hi"),
  };

  StaticSocketDataProvider data(data_reads, data_writes);
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  MockWrite data_writes2[] = {
      // Initial request using a different NetworkIsolationKey includes the
      // cached proxy credentials, but not server credentials.
      MockWrite("GET http://myproxy:70/ HTTP/1.1\r\n"
                "Host: myproxy:70\r\n"
                "Proxy-Connection: keep-alive\r\n"
                "Proxy-Authorization: Basic Zm9vOmJhcg==\r\n\r\n"),
      // Retry with proxy and new server auth credentials, which gets a
      // response.
      MockWrite("GET http://myproxy:70/ HTTP/1.1\r\n"
                "Host: myproxy:70\r\n"
                "Proxy-Connection: keep-alive\r\n"
                "Proxy-Authorization: Basic Zm9vOmJhcg==\r\n"
                "Authorization: Basic Zm9vMzpiYXIz\r\n\r\n"),
  };

  MockRead data_reads2[] = {
      // Server auth challenge.
      MockRead("HTTP/1.0 401 Authentication Required\r\n"
               "Proxy-Connection: keep-alive\r\n"
               "WWW-Authenticate: Basic realm=\"MyRealm1\"\r\n"
               "Content-Length: 0\r\n\r\n"),
      // Response.
      MockRead("HTTP/1.1 200 OK\r\n"
               "Proxy-Connection: keep-alive\r\n"
               "Content-Length: 9\r\n\r\n"
               "greetings"),
  };

  StaticSocketDataProvider data2(data_reads2, data_writes2);
  session_deps_.socket_factory->AddSocketDataProvider(&data2);

  TestCompletionCallback callback;

  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("http://myproxy:70/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);
  request.network_isolation_key = kNetworkIsolationKey1;

  auto trans =
      std::make_unique<HttpNetworkTransaction>(DEFAULT_PRIORITY, session.get());
  int rv = trans->Start(&request, callback.callback(), net_log_with_source);
  EXPECT_THAT(callback.GetResult(rv), IsOk());
  const HttpResponseInfo* response = trans->GetResponseInfo();
  ASSERT_TRUE(response);
  ASSERT_TRUE(response->headers);
  EXPECT_EQ(407, response->headers->response_code());
  EXPECT_TRUE(CheckBasicProxyAuth(response->auth_challenge));

  rv = trans->RestartWithAuth(AuthCredentials(kFoo, kBar), callback.callback());
  EXPECT_THAT(callback.GetResult(rv), IsOk());
  response = trans->GetResponseInfo();
  ASSERT_TRUE(response);
  EXPECT_EQ(401, response->headers->response_code());
  EXPECT_FALSE(response->auth_challenge->is_proxy);
  EXPECT_EQ("http://myproxy:70",
            response->auth_challenge->challenger.Serialize());
  EXPECT_EQ("MyRealm1", response->auth_challenge->realm);
  EXPECT_EQ(kBasicAuthScheme, response->auth_challenge->scheme);

  rv = trans->RestartWithAuth(AuthCredentials(kFoo2, kBar2),
                              callback.callback());
  EXPECT_THAT(callback.GetResult(rv), IsOk());
  response = trans->GetResponseInfo();
  ASSERT_TRUE(response);
  EXPECT_EQ(200, response->headers->response_code());
  // The password prompt info should not be set.
  EXPECT_FALSE(response->auth_challenge.has_value());
  std::string response_data;
  EXPECT_THAT(ReadTransaction(trans.get(), &response_data), IsOk());
  EXPECT_EQ("hello", response_data);

  // Check that the proxy credentials were cached correctly. The should be
  // accessible with any NetworkIsolationKey.
  HttpAuthCache::Entry* entry = session->http_auth_cache()->Lookup(
      url::SchemeHostPort(url::SchemeHostPort(GURL("http://myproxy:70"))),
      HttpAuth::AUTH_PROXY, "MyRealm1", HttpAuth::AUTH_SCHEME_BASIC,
      kNetworkIsolationKey1);
  ASSERT_TRUE(entry);
  ASSERT_EQ(kFoo, entry->credentials().username());
  ASSERT_EQ(kBar, entry->credentials().password());
  EXPECT_EQ(entry, session->http_auth_cache()->Lookup(
                       url::SchemeHostPort(GURL("http://myproxy:70")),
                       HttpAuth::AUTH_PROXY, "MyRealm1",
                       HttpAuth::AUTH_SCHEME_BASIC, kNetworkIsolationKey2));

  // Check that the server credentials were cached correctly. The should be
  // accessible with only kNetworkIsolationKey1.
  entry = session->http_auth_cache()->Lookup(
      url::SchemeHostPort(GURL("http://myproxy:70")), HttpAuth::AUTH_SERVER,
      "MyRealm1", HttpAuth::AUTH_SCHEME_BASIC, kNetworkIsolationKey1);
  ASSERT_TRUE(entry);
  ASSERT_EQ(kFoo2, entry->credentials().username());
  ASSERT_EQ(kBar2, entry->credentials().password());
  // Looking up the server entry with another NetworkIsolationKey should fail.
  EXPECT_FALSE(session->http_auth_cache()->Lookup(
      url::SchemeHostPort(GURL("http://myproxy:70")), HttpAuth::AUTH_SERVER,
      "MyRealm1", HttpAuth::AUTH_SCHEME_BASIC, kNetworkIsolationKey2));

  // Make another request with a different NetworkIsolationKey. It should use
  // another socket, reuse the cached proxy credentials, but result in a server
  // auth challenge.
  request.network_isolation_key = kNetworkIsolationKey2;
  trans =
      std::make_unique<HttpNetworkTransaction>(DEFAULT_PRIORITY, session.get());
  rv = trans->Start(&request, callback.callback(), net_log_with_source);
  EXPECT_THAT(callback.GetResult(rv), IsOk());
  response = trans->GetResponseInfo();
  ASSERT_TRUE(response);
  EXPECT_EQ(401, response->headers->response_code());
  EXPECT_FALSE(response->auth_challenge->is_proxy);
  EXPECT_EQ("http://myproxy:70",
            response->auth_challenge->challenger.Serialize());
  EXPECT_EQ("MyRealm1", response->auth_challenge->realm);
  EXPECT_EQ(kBasicAuthScheme, response->auth_challenge->scheme);

  rv = trans->RestartWithAuth(AuthCredentials(kFoo3, kBar3),
                              callback.callback());
  EXPECT_THAT(callback.GetResult(rv), IsOk());
  response = trans->GetResponseInfo();
  ASSERT_TRUE(response);
  EXPECT_EQ(200, response->headers->response_code());
  // The password prompt info should not be set.
  EXPECT_FALSE(response->auth_challenge.has_value());
  EXPECT_THAT(ReadTransaction(trans.get(), &response_data), IsOk());
  EXPECT_EQ("greetings", response_data);

  // Check that the proxy credentials are still cached.
  entry = session->http_auth_cache()->Lookup(
      url::SchemeHostPort(GURL("http://myproxy:70")), HttpAuth::AUTH_PROXY,
      "MyRealm1", HttpAuth::AUTH_SCHEME_BASIC, kNetworkIsolationKey1);
  ASSERT_TRUE(entry);
  ASSERT_EQ(kFoo, entry->credentials().username());
  ASSERT_EQ(kBar, entry->credentials().password());
  EXPECT_EQ(entry, session->http_auth_cache()->Lookup(
                       url::SchemeHostPort(GURL("http://myproxy:70")),
                       HttpAuth::AUTH_PROXY, "MyRealm1",
                       HttpAuth::AUTH_SCHEME_BASIC, kNetworkIsolationKey2));

  // Check that the correct server credentials are cached for each
  // NetworkIsolationKey.
  entry = session->http_auth_cache()->Lookup(
      url::SchemeHostPort(GURL("http://myproxy:70")), HttpAuth::AUTH_SERVER,
      "MyRealm1", HttpAuth::AUTH_SCHEME_BASIC, kNetworkIsolationKey1);
  ASSERT_TRUE(entry);
  ASSERT_EQ(kFoo2, entry->credentials().username());
  ASSERT_EQ(kBar2, entry->credentials().password());
  entry = session->http_auth_cache()->Lookup(
      url::SchemeHostPort(GURL("http://myproxy:70")), HttpAuth::AUTH_SERVER,
      "MyRealm1", HttpAuth::AUTH_SCHEME_BASIC, kNetworkIsolationKey2);
  ASSERT_TRUE(entry);
  ASSERT_EQ(kFoo3, entry->credentials().username());
  ASSERT_EQ(kBar3, entry->credentials().password());

  // Make a request with the original NetworkIsolationKey. It should reuse the
  // first socket, and the proxy credentials sent on the first socket.
  request.network_isolation_key = kNetworkIsolationKey1;
  trans =
      std::make_unique<HttpNetworkTransaction>(DEFAULT_PRIORITY, session.get());
  rv = trans->Start(&request, callback.callback(), net_log_with_source);
  EXPECT_THAT(callback.GetResult(rv), IsOk());
  response = trans->GetResponseInfo();
  ASSERT_TRUE(response);
  EXPECT_EQ(200, response->headers->response_code());
  // The password prompt info should not be set.
  EXPECT_FALSE(response->auth_challenge.has_value());
  EXPECT_THAT(ReadTransaction(trans.get(), &response_data), IsOk());
  EXPECT_EQ("hi", response_data);

  trans.reset();
  session->CloseAllConnections(ERR_FAILED, "Very good reason");
}

// Much like the test above, but uses tunnelled connections.
TEST_F(HttpNetworkTransactionTest,
       BasicAuthProxyMatchesServerAuthWithNetworkIsolationKeyWithTunnel) {
  const SchemefulSite kSite1(GURL("https://foo.test/"));
  const net::NetworkIsolationKey kNetworkIsolationKey1(kSite1, kSite1);
  const SchemefulSite kSite2(GURL("https://bar.test/"));
  const net::NetworkIsolationKey kNetworkIsolationKey2(kSite2, kSite2);

  // This test would need to use a single socket without this option enabled.
  // Best to use this option when it would affect a test, as it will eventually
  // become the default behavior.
  base::test::ScopedFeatureList feature_list;
  feature_list.InitAndEnableFeature(
      features::kPartitionConnectionsByNetworkIsolationKey);

  // Proxy matches request URL.
  session_deps_.proxy_resolution_service =
      ConfiguredProxyResolutionService::CreateFixedFromPacResultForTest(
          "HTTPS myproxy:70", TRAFFIC_ANNOTATION_FOR_TESTS);
  NetLogWithSource net_log_with_source =
      NetLogWithSource::Make(NetLogSourceType::NONE);
  session_deps_.net_log = NetLog::Get();
  session_deps_.key_auth_cache_server_entries_by_network_isolation_key = true;
  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));

  MockWrite data_writes[] = {
      // Initial tunnel request gets a proxy auth challenge.
      MockWrite("CONNECT myproxy:70 HTTP/1.1\r\n"
                "Host: myproxy:70\r\n"
                "Proxy-Connection: keep-alive\r\n\r\n"),
      // Retry with proxy auth credentials, which will result in establishing a
      // tunnel.
      MockWrite("CONNECT myproxy:70 HTTP/1.1\r\n"
                "Host: myproxy:70\r\n"
                "Proxy-Connection: keep-alive\r\n"
                "Proxy-Authorization: Basic Zm9vOmJhcg==\r\n\r\n"),
      // Request over the tunnel, which gets a server auth challenge.
      MockWrite("GET / HTTP/1.1\r\n"
                "Host: myproxy:70\r\n"
                "Connection: keep-alive\r\n\r\n"),
      // Retry with server auth credentials, which gets a response.
      MockWrite("GET / HTTP/1.1\r\n"
                "Host: myproxy:70\r\n"
                "Connection: keep-alive\r\n"
                "Authorization: Basic Zm9vMjpiYXIy\r\n\r\n"),
      // Another request to the same server and using the same NIK should
      // preemptively send the correct cached server
      // auth header. Since a tunnel was already established, the proxy headers
      // won't be sent again except when establishing another tunnel.
      MockWrite("GET / HTTP/1.1\r\n"
                "Host: myproxy:70\r\n"
                "Connection: keep-alive\r\n"
                "Authorization: Basic Zm9vMjpiYXIy\r\n\r\n"),
  };

  MockRead data_reads[] = {
      // Proxy auth challenge.
      MockRead("HTTP/1.0 407 Proxy Authentication Required\r\n"
               "Proxy-Connection: keep-alive\r\n"
               "Proxy-Authenticate: Basic realm=\"MyRealm1\"\r\n"
               "Content-Length: 0\r\n\r\n"),
      // Tunnel success
      MockRead("HTTP/1.1 200 Connection Established\r\n\r\n"),
      // Server auth challenge.
      MockRead("HTTP/1.0 401 Authentication Required\r\n"
               "Connection: keep-alive\r\n"
               "WWW-Authenticate: Basic realm=\"MyRealm1\"\r\n"
               "Content-Length: 0\r\n\r\n"),
      // Response.
      MockRead("HTTP/1.1 200 OK\r\n"
               "Connection: keep-alive\r\n"
               "Content-Length: 5\r\n\r\n"
               "hello"),
      // Response to second request.
      MockRead("HTTP/1.1 200 OK\r\n"
               "Connection: keep-alive\r\n"
               "Content-Length: 2\r\n\r\n"
               "hi"),
  };

  StaticSocketDataProvider data(data_reads, data_writes);
  session_deps_.socket_factory->AddSocketDataProvider(&data);
  // One for the proxy connection, one of the server connection.
  SSLSocketDataProvider ssl(ASYNC, OK);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl);
  SSLSocketDataProvider ssl2(ASYNC, OK);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl2);

  MockWrite data_writes2[] = {
      // Initial request using a different NetworkIsolationKey includes the
      // cached proxy credentials when establishing a tunnel.
      MockWrite("CONNECT myproxy:70 HTTP/1.1\r\n"
                "Host: myproxy:70\r\n"
                "Proxy-Connection: keep-alive\r\n"
                "Proxy-Authorization: Basic Zm9vOmJhcg==\r\n\r\n"),
      // Request over the tunnel, which gets a server auth challenge. Cached
      // credentials cannot be used, since the NIK is different.
      MockWrite("GET / HTTP/1.1\r\n"
                "Host: myproxy:70\r\n"
                "Connection: keep-alive\r\n\r\n"),
      // Retry with server auth credentials, which gets a response.
      MockWrite("GET / HTTP/1.1\r\n"
                "Host: myproxy:70\r\n"
                "Connection: keep-alive\r\n"
                "Authorization: Basic Zm9vMzpiYXIz\r\n\r\n"),
  };

  MockRead data_reads2[] = {
      // Tunnel success
      MockRead("HTTP/1.1 200 Connection Established\r\n\r\n"),
      // Server auth challenge.
      MockRead("HTTP/1.0 401 Authentication Required\r\n"
               "Connection: keep-alive\r\n"
               "WWW-Authenticate: Basic realm=\"MyRealm1\"\r\n"
               "Content-Length: 0\r\n\r\n"),
      // Response.
      MockRead("HTTP/1.1 200 OK\r\n"
               "Connection: keep-alive\r\n"
               "Content-Length: 9\r\n\r\n"
               "greetings"),
  };

  StaticSocketDataProvider data2(data_reads2, data_writes2);
  session_deps_.socket_factory->AddSocketDataProvider(&data2);
  // One for the proxy connection, one of the server connection.
  SSLSocketDataProvider ssl3(ASYNC, OK);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl3);
  SSLSocketDataProvider ssl4(ASYNC, OK);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl4);

  TestCompletionCallback callback;

  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("https://myproxy:70/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);
  request.network_isolation_key = kNetworkIsolationKey1;

  auto trans =
      std::make_unique<HttpNetworkTransaction>(DEFAULT_PRIORITY, session.get());
  int rv = trans->Start(&request, callback.callback(), net_log_with_source);
  EXPECT_THAT(callback.GetResult(rv), IsOk());
  const HttpResponseInfo* response = trans->GetResponseInfo();
  ASSERT_TRUE(response);
  ASSERT_TRUE(response->headers);
  EXPECT_EQ(407, response->headers->response_code());
  EXPECT_TRUE(CheckBasicSecureProxyAuth(response->auth_challenge));

  rv = trans->RestartWithAuth(AuthCredentials(kFoo, kBar), callback.callback());
  EXPECT_THAT(callback.GetResult(rv), IsOk());
  response = trans->GetResponseInfo();
  ASSERT_TRUE(response);
  EXPECT_EQ(401, response->headers->response_code());
  EXPECT_FALSE(response->auth_challenge->is_proxy);
  EXPECT_EQ("https://myproxy:70",
            response->auth_challenge->challenger.Serialize());
  EXPECT_EQ("MyRealm1", response->auth_challenge->realm);
  EXPECT_EQ(kBasicAuthScheme, response->auth_challenge->scheme);

  rv = trans->RestartWithAuth(AuthCredentials(kFoo2, kBar2),
                              callback.callback());
  EXPECT_THAT(callback.GetResult(rv), IsOk());
  response = trans->GetResponseInfo();
  ASSERT_TRUE(response);
  EXPECT_EQ(200, response->headers->response_code());
  // The password prompt info should not be set.
  EXPECT_FALSE(response->auth_challenge.has_value());
  std::string response_data;
  EXPECT_THAT(ReadTransaction(trans.get(), &response_data), IsOk());
  EXPECT_EQ("hello", response_data);

  // Check that the proxy credentials were cached correctly. The should be
  // accessible with any NetworkIsolationKey.
  HttpAuthCache::Entry* entry = session->http_auth_cache()->Lookup(
      url::SchemeHostPort(url::SchemeHostPort(GURL("https://myproxy:70"))),
      HttpAuth::AUTH_PROXY, "MyRealm1", HttpAuth::AUTH_SCHEME_BASIC,
      kNetworkIsolationKey1);
  ASSERT_TRUE(entry);
  ASSERT_EQ(kFoo, entry->credentials().username());
  ASSERT_EQ(kBar, entry->credentials().password());
  EXPECT_EQ(entry, session->http_auth_cache()->Lookup(
                       url::SchemeHostPort(GURL("https://myproxy:70")),
                       HttpAuth::AUTH_PROXY, "MyRealm1",
                       HttpAuth::AUTH_SCHEME_BASIC, kNetworkIsolationKey2));

  // Check that the server credentials were cached correctly. The should be
  // accessible with only kNetworkIsolationKey1.
  entry = session->http_auth_cache()->Lookup(
      url::SchemeHostPort(GURL("https://myproxy:70")), HttpAuth::AUTH_SERVER,
      "MyRealm1", HttpAuth::AUTH_SCHEME_BASIC, kNetworkIsolationKey1);
  ASSERT_TRUE(entry);
  ASSERT_EQ(kFoo2, entry->credentials().username());
  ASSERT_EQ(kBar2, entry->credentials().password());
  // Looking up the server entry with another NetworkIsolationKey should fail.
  EXPECT_FALSE(session->http_auth_cache()->Lookup(
      url::SchemeHostPort(GURL("https://myproxy:70")), HttpAuth::AUTH_SERVER,
      "MyRealm1", HttpAuth::AUTH_SCHEME_BASIC, kNetworkIsolationKey2));

  // Make another request with a different NetworkIsolationKey. It should use
  // another socket, reuse the cached proxy credentials, but result in a server
  // auth challenge.
  request.network_isolation_key = kNetworkIsolationKey2;
  trans =
      std::make_unique<HttpNetworkTransaction>(DEFAULT_PRIORITY, session.get());
  rv = trans->Start(&request, callback.callback(), net_log_with_source);
  EXPECT_THAT(callback.GetResult(rv), IsOk());
  response = trans->GetResponseInfo();
  ASSERT_TRUE(response);
  EXPECT_EQ(401, response->headers->response_code());
  EXPECT_FALSE(response->auth_challenge->is_proxy);
  EXPECT_EQ("https://myproxy:70",
            response->auth_challenge->challenger.Serialize());
  EXPECT_EQ("MyRealm1", response->auth_challenge->realm);
  EXPECT_EQ(kBasicAuthScheme, response->auth_challenge->scheme);

  rv = trans->RestartWithAuth(AuthCredentials(kFoo3, kBar3),
                              callback.callback());
  EXPECT_THAT(callback.GetResult(rv), IsOk());
  response = trans->GetResponseInfo();
  ASSERT_TRUE(response);
  EXPECT_EQ(200, response->headers->response_code());
  // The password prompt info should not be set.
  EXPECT_FALSE(response->auth_challenge.has_value());
  EXPECT_THAT(ReadTransaction(trans.get(), &response_data), IsOk());
  EXPECT_EQ("greetings", response_data);

  // Check that the proxy credentials are still cached.
  entry = session->http_auth_cache()->Lookup(
      url::SchemeHostPort(GURL("https://myproxy:70")), HttpAuth::AUTH_PROXY,
      "MyRealm1", HttpAuth::AUTH_SCHEME_BASIC, kNetworkIsolationKey1);
  ASSERT_TRUE(entry);
  ASSERT_EQ(kFoo, entry->credentials().username());
  ASSERT_EQ(kBar, entry->credentials().password());
  EXPECT_EQ(entry, session->http_auth_cache()->Lookup(
                       url::SchemeHostPort(GURL("https://myproxy:70")),
                       HttpAuth::AUTH_PROXY, "MyRealm1",
                       HttpAuth::AUTH_SCHEME_BASIC, kNetworkIsolationKey2));

  // Check that the correct server credentials are cached for each
  // NetworkIsolationKey.
  entry = session->http_auth_cache()->Lookup(
      url::SchemeHostPort(GURL("https://myproxy:70")), HttpAuth::AUTH_SERVER,
      "MyRealm1", HttpAuth::AUTH_SCHEME_BASIC, kNetworkIsolationKey1);
  ASSERT_TRUE(entry);
  ASSERT_EQ(kFoo2, entry->credentials().username());
  ASSERT_EQ(kBar2, entry->credentials().password());
  entry = session->http_auth_cache()->Lookup(
      url::SchemeHostPort(GURL("https://myproxy:70")), HttpAuth::AUTH_SERVER,
      "MyRealm1", HttpAuth::AUTH_SCHEME_BASIC, kNetworkIsolationKey2);
  ASSERT_TRUE(entry);
  ASSERT_EQ(kFoo3, entry->credentials().username());
  ASSERT_EQ(kBar3, entry->credentials().password());

  // Make a request with the original NetworkIsolationKey. It should reuse the
  // first socket, and the proxy credentials sent on the first socket.
  request.network_isolation_key = kNetworkIsolationKey1;
  trans =
      std::make_unique<HttpNetworkTransaction>(DEFAULT_PRIORITY, session.get());
  rv = trans->Start(&request, callback.callback(), net_log_with_source);
  EXPECT_THAT(callback.GetResult(rv), IsOk());
  response = trans->GetResponseInfo();
  ASSERT_TRUE(response);
  EXPECT_EQ(200, response->headers->response_code());
  // The password prompt info should not be set.
  EXPECT_FALSE(response->auth_challenge.has_value());
  EXPECT_THAT(ReadTransaction(trans.get(), &response_data), IsOk());
  EXPECT_EQ("hi", response_data);

  trans.reset();
  session->CloseAllConnections(ERR_FAILED, "Very good reason");
}

// Test that we don't pass extraneous headers from the proxy's response to the
// caller when the proxy responds to CONNECT with 407.
TEST_F(HttpNetworkTransactionTest, SanitizeProxyAuthHeaders) {
  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("https://www.example.org/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  // Configure against proxy server "myproxy:70".
  session_deps_.proxy_resolution_service =
      ConfiguredProxyResolutionService::CreateFixedForTest(
          "myproxy:70", TRAFFIC_ANNOTATION_FOR_TESTS);

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));

  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

  // Since we have proxy, should try to establish tunnel.
  MockWrite data_writes[] = {
      MockWrite("CONNECT www.example.org:443 HTTP/1.1\r\n"
                "Host: www.example.org:443\r\n"
                "Proxy-Connection: keep-alive\r\n\r\n"),
  };

  // The proxy responds to the connect with a 407.
  MockRead data_reads[] = {
      MockRead("HTTP/1.1 407 Proxy Authentication Required\r\n"),
      MockRead("X-Foo: bar\r\n"),
      MockRead("Set-Cookie: foo=bar\r\n"),
      MockRead("Proxy-Authenticate: Basic realm=\"MyRealm1\"\r\n"),
      MockRead("Content-Length: 10\r\n\r\n"),
      MockRead(SYNCHRONOUS, ERR_UNEXPECTED),
  };

  StaticSocketDataProvider data(data_reads, data_writes);
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  TestCompletionCallback callback;

  int rv = trans.Start(&request, callback.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback.WaitForResult();
  EXPECT_THAT(rv, IsOk());

  const HttpResponseInfo* response = trans.GetResponseInfo();
  ASSERT_TRUE(response);
  ASSERT_TRUE(response->headers);
  EXPECT_TRUE(response->headers->IsKeepAlive());
  EXPECT_EQ(407, response->headers->response_code());
  EXPECT_TRUE(HttpVersion(1, 1) == response->headers->GetHttpVersion());
  EXPECT_FALSE(response->headers->HasHeader("X-Foo"));
  EXPECT_FALSE(response->headers->HasHeader("Set-Cookie"));

  std::string response_data;
  rv = ReadTransaction(&trans, &response_data);
  EXPECT_THAT(rv, IsError(ERR_TUNNEL_CONNECTION_FAILED));

  // Flush the idle socket before the HttpNetworkTransaction goes out of scope.
  session->CloseAllConnections(ERR_FAILED, "Very good reason");
}

// Test when a server (non-proxy) returns a 407 (proxy-authenticate).
// The request should fail with ERR_UNEXPECTED_PROXY_AUTH.
TEST_F(HttpNetworkTransactionTest, UnexpectedProxyAuth) {
  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("http://www.example.org/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  // We are using a DIRECT connection (i.e. no proxy) for this session.
  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));
  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

  MockWrite data_writes1[] = {
      MockWrite(
          "GET / HTTP/1.1\r\n"
          "Host: www.example.org\r\n"
          "Connection: keep-alive\r\n\r\n"),
  };

  MockRead data_reads1[] = {
    MockRead("HTTP/1.0 407 Proxy Auth required\r\n"),
    MockRead("Proxy-Authenticate: Basic realm=\"MyRealm1\"\r\n"),
    // Large content-length -- won't matter, as connection will be reset.
    MockRead("Content-Length: 10000\r\n\r\n"),
    MockRead(SYNCHRONOUS, ERR_FAILED),
  };

  StaticSocketDataProvider data1(data_reads1, data_writes1);
  session_deps_.socket_factory->AddSocketDataProvider(&data1);

  TestCompletionCallback callback;

  int rv = trans.Start(&request, callback.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback.WaitForResult();
  EXPECT_THAT(rv, IsError(ERR_UNEXPECTED_PROXY_AUTH));
}

// Tests when an HTTPS server (non-proxy) returns a 407 (proxy-authentication)
// through a non-authenticating proxy. The request should fail with
// ERR_UNEXPECTED_PROXY_AUTH.
// Note that it is impossible to detect if an HTTP server returns a 407 through
// a non-authenticating proxy - there is nothing to indicate whether the
// response came from the proxy or the server, so it is treated as if the proxy
// issued the challenge.
TEST_F(HttpNetworkTransactionTest, HttpsServerRequestsProxyAuthThroughProxy) {
  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("https://www.example.org/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  session_deps_.proxy_resolution_service =
      ConfiguredProxyResolutionService::CreateFixedForTest(
          "myproxy:70", TRAFFIC_ANNOTATION_FOR_TESTS);
  RecordingNetLogObserver net_log_observer;
  session_deps_.net_log = NetLog::Get();
  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));

  // Since we have proxy, should try to establish tunnel.
  MockWrite data_writes1[] = {
      MockWrite("CONNECT www.example.org:443 HTTP/1.1\r\n"
                "Host: www.example.org:443\r\n"
                "Proxy-Connection: keep-alive\r\n\r\n"),

      MockWrite("GET / HTTP/1.1\r\n"
                "Host: www.example.org\r\n"
                "Connection: keep-alive\r\n\r\n"),
  };

  MockRead data_reads1[] = {
    MockRead("HTTP/1.1 200 Connection Established\r\n\r\n"),

    MockRead("HTTP/1.1 407 Unauthorized\r\n"),
    MockRead("Proxy-Authenticate: Basic realm=\"MyRealm1\"\r\n"),
    MockRead("\r\n"),
    MockRead(SYNCHRONOUS, OK),
  };

  StaticSocketDataProvider data1(data_reads1, data_writes1);
  session_deps_.socket_factory->AddSocketDataProvider(&data1);
  SSLSocketDataProvider ssl(ASYNC, OK);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl);

  TestCompletionCallback callback1;

  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

  int rv = trans.Start(&request, callback1.callback(),
                       NetLogWithSource::Make(NetLogSourceType::NONE));
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback1.WaitForResult();
  EXPECT_THAT(rv, IsError(ERR_UNEXPECTED_PROXY_AUTH));
  auto entries = net_log_observer.GetEntries();
  size_t pos = ExpectLogContainsSomewhere(
      entries, 0, NetLogEventType::HTTP_TRANSACTION_SEND_TUNNEL_HEADERS,
      NetLogEventPhase::NONE);
  ExpectLogContainsSomewhere(
      entries, pos,
      NetLogEventType::HTTP_TRANSACTION_READ_TUNNEL_RESPONSE_HEADERS,
      NetLogEventPhase::NONE);
}

// Test a proxy auth scheme that allows default credentials and a proxy server
// that uses non-persistent connections.
TEST_F(HttpNetworkTransactionTest,
       AuthAllowsDefaultCredentialsTunnelConnectionClose) {
  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("https://www.example.org/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  // Configure against proxy server "myproxy:70".
  session_deps_.proxy_resolution_service =
      ConfiguredProxyResolutionService::CreateFixedFromPacResultForTest(
          "PROXY myproxy:70", TRAFFIC_ANNOTATION_FOR_TESTS);

  auto auth_handler_factory = std::make_unique<HttpAuthHandlerMock::Factory>();
  auth_handler_factory->set_do_init_from_challenge(true);
  auto mock_handler = std::make_unique<HttpAuthHandlerMock>();
  mock_handler->set_allows_default_credentials(true);
  auth_handler_factory->AddMockHandler(std::move(mock_handler),
                                       HttpAuth::AUTH_PROXY);
  session_deps_.http_auth_handler_factory = std::move(auth_handler_factory);

  // Add NetLog just so can verify load timing information gets a NetLog ID.
  session_deps_.net_log = NetLog::Get();
  std::unique_ptr<HttpNetworkSession> session = CreateSession(&session_deps_);

  // Since we have proxy, should try to establish tunnel.
  MockWrite data_writes1[] = {
      MockWrite("CONNECT www.example.org:443 HTTP/1.1\r\n"
                "Host: www.example.org:443\r\n"
                "Proxy-Connection: keep-alive\r\n\r\n"),
  };

  // The proxy responds to the connect with a 407, using a non-persistent
  // connection.
  MockRead data_reads1[] = {
      // No credentials.
      MockRead("HTTP/1.1 407 Proxy Authentication Required\r\n"),
      MockRead("Proxy-Authenticate: Mock\r\n"),
      MockRead("Proxy-Connection: close\r\n\r\n"),
  };

  // Since the first connection couldn't be reused, need to establish another
  // once given credentials.
  MockWrite data_writes2[] = {
      // After calling trans->RestartWithAuth(), this is the request we should
      // be issuing -- the final header line contains the credentials.
      MockWrite("CONNECT www.example.org:443 HTTP/1.1\r\n"
                "Host: www.example.org:443\r\n"
                "Proxy-Connection: keep-alive\r\n"
                "Proxy-Authorization: auth_token\r\n\r\n"),

      MockWrite("GET / HTTP/1.1\r\n"
                "Host: www.example.org\r\n"
                "Connection: keep-alive\r\n\r\n"),
  };

  MockRead data_reads2[] = {
      MockRead("HTTP/1.0 200 Connection Established\r\n\r\n"),

      MockRead("HTTP/1.1 200 OK\r\n"),
      MockRead("Content-Type: text/html; charset=iso-8859-1\r\n"),
      MockRead("Content-Length: 5\r\n\r\n"),
      MockRead(SYNCHRONOUS, "hello"),
  };

  StaticSocketDataProvider data1(data_reads1, data_writes1);
  session_deps_.socket_factory->AddSocketDataProvider(&data1);
  StaticSocketDataProvider data2(data_reads2, data_writes2);
  session_deps_.socket_factory->AddSocketDataProvider(&data2);
  SSLSocketDataProvider ssl(ASYNC, OK);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl);

  auto trans =
      std::make_unique<HttpNetworkTransaction>(DEFAULT_PRIORITY, session.get());

  TestCompletionCallback callback;
  int rv = trans->Start(&request, callback.callback(), NetLogWithSource());
  EXPECT_THAT(callback.GetResult(rv), IsOk());

  const HttpResponseInfo* response = trans->GetResponseInfo();
  ASSERT_TRUE(response);
  ASSERT_TRUE(response->headers);
  EXPECT_FALSE(response->headers->IsKeepAlive());
  EXPECT_EQ(407, response->headers->response_code());
  EXPECT_TRUE(HttpVersion(1, 1) == response->headers->GetHttpVersion());
  EXPECT_TRUE(trans->IsReadyToRestartForAuth());
  EXPECT_FALSE(response->auth_challenge.has_value());

  LoadTimingInfo load_timing_info;
  // CONNECT requests and responses are handled at the connect job level, so
  // the transaction does not yet have a connection.
  EXPECT_FALSE(trans->GetLoadTimingInfo(&load_timing_info));

  rv = trans->RestartWithAuth(AuthCredentials(), callback.callback());
  EXPECT_THAT(callback.GetResult(rv), IsOk());
  response = trans->GetResponseInfo();
  ASSERT_TRUE(response);
  ASSERT_TRUE(response->headers);
  EXPECT_TRUE(response->headers->IsKeepAlive());
  EXPECT_EQ(200, response->headers->response_code());
  EXPECT_EQ(5, response->headers->GetContentLength());
  EXPECT_TRUE(HttpVersion(1, 1) == response->headers->GetHttpVersion());

  // The password prompt info should not be set.
  EXPECT_FALSE(response->auth_challenge.has_value());

  EXPECT_TRUE(trans->GetLoadTimingInfo(&load_timing_info));
  TestLoadTimingNotReusedWithPac(load_timing_info,
                                 CONNECT_TIMING_HAS_SSL_TIMES);

  trans.reset();
  session->CloseAllConnections(ERR_FAILED, "Very good reason");
}

// Test a proxy auth scheme that allows default credentials and a proxy server
// that hangs up when credentials are initially sent.
TEST_F(HttpNetworkTransactionTest,
       AuthAllowsDefaultCredentialsTunnelServerClosesConnection) {
  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("https://www.example.org/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  // Configure against proxy server "myproxy:70".
  session_deps_.proxy_resolution_service =
      ConfiguredProxyResolutionService::CreateFixedFromPacResultForTest(
          "PROXY myproxy:70", TRAFFIC_ANNOTATION_FOR_TESTS);

  auto auth_handler_factory = std::make_unique<HttpAuthHandlerMock::Factory>();
  auth_handler_factory->set_do_init_from_challenge(true);
  auto mock_handler = std::make_unique<HttpAuthHandlerMock>();
  mock_handler->set_allows_default_credentials(true);
  auth_handler_factory->AddMockHandler(std::move(mock_handler),
                                       HttpAuth::AUTH_PROXY);
  session_deps_.http_auth_handler_factory = std::move(auth_handler_factory);

  // Add NetLog just so can verify load timing information gets a NetLog ID.
  session_deps_.net_log = NetLog::Get();
  std::unique_ptr<HttpNetworkSession> session = CreateSession(&session_deps_);

  // Should try to establish tunnel.
  MockWrite data_writes1[] = {
      MockWrite("CONNECT www.example.org:443 HTTP/1.1\r\n"
                "Host: www.example.org:443\r\n"
                "Proxy-Connection: keep-alive\r\n\r\n"),

      MockWrite("CONNECT www.example.org:443 HTTP/1.1\r\n"
                "Host: www.example.org:443\r\n"
                "Proxy-Connection: keep-alive\r\n"
                "Proxy-Authorization: auth_token\r\n\r\n"),
  };

  // The proxy responds to the connect with a 407, using a non-persistent
  // connection.
  MockRead data_reads1[] = {
      // No credentials.
      MockRead("HTTP/1.1 407 Proxy Authentication Required\r\n"),
      MockRead("Proxy-Authenticate: Mock\r\n\r\n"),
      MockRead(SYNCHRONOUS, ERR_CONNECTION_CLOSED),
  };

  // Since the first connection was closed, need to establish another once given
  // credentials.
  MockWrite data_writes2[] = {
      MockWrite("CONNECT www.example.org:443 HTTP/1.1\r\n"
                "Host: www.example.org:443\r\n"
                "Proxy-Connection: keep-alive\r\n"
                "Proxy-Authorization: auth_token\r\n\r\n"),

      MockWrite("GET / HTTP/1.1\r\n"
                "Host: www.example.org\r\n"
                "Connection: keep-alive\r\n\r\n"),
  };

  MockRead data_reads2[] = {
      MockRead("HTTP/1.0 200 Connection Established\r\n\r\n"),

      MockRead("HTTP/1.1 200 OK\r\n"),
      MockRead("Content-Type: text/html; charset=iso-8859-1\r\n"),
      MockRead("Content-Length: 5\r\n\r\n"),
      MockRead(SYNCHRONOUS, "hello"),
  };

  StaticSocketDataProvider data1(data_reads1, data_writes1);
  session_deps_.socket_factory->AddSocketDataProvider(&data1);
  StaticSocketDataProvider data2(data_reads2, data_writes2);
  session_deps_.socket_factory->AddSocketDataProvider(&data2);
  SSLSocketDataProvider ssl(ASYNC, OK);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl);

  auto trans =
      std::make_unique<HttpNetworkTransaction>(DEFAULT_PRIORITY, session.get());

  TestCompletionCallback callback;
  int rv = trans->Start(&request, callback.callback(), NetLogWithSource());
  EXPECT_THAT(callback.GetResult(rv), IsOk());

  const HttpResponseInfo* response = trans->GetResponseInfo();
  ASSERT_TRUE(response);
  ASSERT_TRUE(response->headers);
  EXPECT_TRUE(response->headers->IsKeepAlive());
  EXPECT_EQ(407, response->headers->response_code());
  EXPECT_TRUE(HttpVersion(1, 1) == response->headers->GetHttpVersion());
  EXPECT_TRUE(trans->IsReadyToRestartForAuth());
  EXPECT_FALSE(response->auth_challenge.has_value());

  LoadTimingInfo load_timing_info;
  // CONNECT requests and responses are handled at the connect job level, so
  // the transaction does not yet have a connection.
  EXPECT_FALSE(trans->GetLoadTimingInfo(&load_timing_info));

  rv = trans->RestartWithAuth(AuthCredentials(), callback.callback());
  EXPECT_THAT(callback.GetResult(rv), IsOk());

  response = trans->GetResponseInfo();
  ASSERT_TRUE(response);
  ASSERT_TRUE(response->headers);
  EXPECT_TRUE(response->headers->IsKeepAlive());
  EXPECT_EQ(200, response->headers->response_code());
  EXPECT_EQ(5, response->headers->GetContentLength());
  EXPECT_TRUE(HttpVersion(1, 1) == response->headers->GetHttpVersion());

  // The password prompt info should not be set.
  EXPECT_FALSE(response->auth_challenge.has_value());

  EXPECT_TRUE(trans->GetLoadTimingInfo(&load_timing_info));
  TestLoadTimingNotReusedWithPac(load_timing_info,
                                 CONNECT_TIMING_HAS_SSL_TIMES);

  trans.reset();
  session->CloseAllConnections(ERR_FAILED, "Very good reason");
}

// Test a proxy auth scheme that allows default credentials and a proxy server
// that hangs up when credentials are initially sent, and hangs up again when
// they are retried.
TEST_F(HttpNetworkTransactionTest,
       AuthAllowsDefaultCredentialsTunnelServerClosesConnectionTwice) {
  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("https://www.example.org/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  // Configure against proxy server "myproxy:70".
  session_deps_.proxy_resolution_service =
      ConfiguredProxyResolutionService::CreateFixedFromPacResultForTest(
          "PROXY myproxy:70", TRAFFIC_ANNOTATION_FOR_TESTS);

  auto auth_handler_factory = std::make_unique<HttpAuthHandlerMock::Factory>();
  auth_handler_factory->set_do_init_from_challenge(true);
  auto mock_handler = std::make_unique<HttpAuthHandlerMock>();
  mock_handler->set_allows_default_credentials(true);
  auth_handler_factory->AddMockHandler(std::move(mock_handler),
                                       HttpAuth::AUTH_PROXY);
  session_deps_.http_auth_handler_factory = std::move(auth_handler_factory);

  // Add NetLog just so can verify load timing information gets a NetLog ID.
  session_deps_.net_log = NetLog::Get();
  std::unique_ptr<HttpNetworkSession> session = CreateSession(&session_deps_);

  // Should try to establish tunnel.
  MockWrite data_writes1[] = {
      MockWrite("CONNECT www.example.org:443 HTTP/1.1\r\n"
                "Host: www.example.org:443\r\n"
                "Proxy-Connection: keep-alive\r\n\r\n"),

      MockWrite("CONNECT www.example.org:443 HTTP/1.1\r\n"
                "Host: www.example.org:443\r\n"
                "Proxy-Connection: keep-alive\r\n"
                "Proxy-Authorization: auth_token\r\n\r\n"),
  };

  // The proxy responds to the connect with a 407, and then hangs up after the
  // second request is sent.
  MockRead data_reads1[] = {
      // No credentials.
      MockRead("HTTP/1.1 407 Proxy Authentication Required\r\n"),
      MockRead("Content-Length: 0\r\n"),
      MockRead("Proxy-Connection: keep-alive\r\n"),
      MockRead("Proxy-Authenticate: Mock\r\n\r\n"),
      MockRead(SYNCHRONOUS, ERR_CONNECTION_CLOSED),
  };

  // HttpNetworkTransaction sees a reused connection that was closed with
  // ERR_CONNECTION_CLOSED, realized it might have been a race, so retries the
  // request.
  MockWrite data_writes2[] = {
      MockWrite("CONNECT www.example.org:443 HTTP/1.1\r\n"
                "Host: www.example.org:443\r\n"
                "Proxy-Connection: keep-alive\r\n\r\n"),
  };

  // The proxy, having had more than enough of us, just hangs up.
  MockRead data_reads2[] = {
      // No credentials.
      MockRead(SYNCHRONOUS, ERR_CONNECTION_CLOSED),
  };

  StaticSocketDataProvider data1(data_reads1, data_writes1);
  session_deps_.socket_factory->AddSocketDataProvider(&data1);
  StaticSocketDataProvider data2(data_reads2, data_writes2);
  session_deps_.socket_factory->AddSocketDataProvider(&data2);

  auto trans =
      std::make_unique<HttpNetworkTransaction>(DEFAULT_PRIORITY, session.get());

  TestCompletionCallback callback;
  int rv = trans->Start(&request, callback.callback(), NetLogWithSource());
  EXPECT_THAT(callback.GetResult(rv), IsOk());

  const HttpResponseInfo* response = trans->GetResponseInfo();
  ASSERT_TRUE(response);
  ASSERT_TRUE(response->headers);
  EXPECT_TRUE(response->headers->IsKeepAlive());
  EXPECT_EQ(407, response->headers->response_code());
  EXPECT_TRUE(HttpVersion(1, 1) == response->headers->GetHttpVersion());
  EXPECT_TRUE(trans->IsReadyToRestartForAuth());
  EXPECT_FALSE(response->auth_challenge.has_value());

  LoadTimingInfo load_timing_info;
  EXPECT_FALSE(trans->GetLoadTimingInfo(&load_timing_info));

  rv = trans->RestartWithAuth(AuthCredentials(), callback.callback());
  EXPECT_THAT(callback.GetResult(rv), IsError(ERR_EMPTY_RESPONSE));

  trans.reset();
  session->CloseAllConnections(ERR_FAILED, "Very good reason");
}

// This test exercises an odd edge case where the proxy closes the connection
// after the authentication handshake is complete. Presumably this technique is
// used in lieu of returning a 403 or 5xx status code when the authentication
// succeeds, but the user is not authorized to connect to the destination
// server. There's no standard for what a proxy should do to indicate a blocked
// site.
TEST_F(HttpNetworkTransactionTest,
       AuthAllowsDefaultCredentialsTunnelConnectionClosesBeforeBody) {
  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("https://www.example.org/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  // Configure against proxy server "myproxy:70".
  session_deps_.proxy_resolution_service =
      ConfiguredProxyResolutionService::CreateFixedFromPacResultForTest(
          "PROXY myproxy:70", TRAFFIC_ANNOTATION_FOR_TESTS);

  auto auth_handler_factory = std::make_unique<HttpAuthHandlerMock::Factory>();
  auth_handler_factory->set_do_init_from_challenge(true);

  // Create two mock AuthHandlers. This is because the transaction gets retried
  // after the first ERR_CONNECTION_CLOSED since it's ambiguous whether there
  // was a real network error.
  //
  // The handlers support both default and explicit credentials. The retry
  // mentioned above should be able to reuse the default identity. Thus there
  // should never be a need to prompt for explicit credentials.
  auto mock_handler = std::make_unique<HttpAuthHandlerMock>();
  mock_handler->set_allows_default_credentials(true);
  mock_handler->set_allows_explicit_credentials(true);
  mock_handler->set_connection_based(true);
  auth_handler_factory->AddMockHandler(std::move(mock_handler),
                                       HttpAuth::AUTH_PROXY);
  mock_handler = std::make_unique<HttpAuthHandlerMock>();
  mock_handler->set_allows_default_credentials(true);
  mock_handler->set_allows_explicit_credentials(true);
  mock_handler->set_connection_based(true);
  auth_handler_factory->AddMockHandler(std::move(mock_handler),
                                       HttpAuth::AUTH_PROXY);
  session_deps_.http_auth_handler_factory = std::move(auth_handler_factory);

  session_deps_.net_log = NetLog::Get();
  std::unique_ptr<HttpNetworkSession> session = CreateSession(&session_deps_);

  // Data for both sockets.
  //
  // Writes are for the tunnel establishment attempts and the
  // authentication handshake.
  MockWrite data_writes1[] = {
      MockWrite("CONNECT www.example.org:443 HTTP/1.1\r\n"
                "Host: www.example.org:443\r\n"
                "Proxy-Connection: keep-alive\r\n\r\n"),

      MockWrite("CONNECT www.example.org:443 HTTP/1.1\r\n"
                "Host: www.example.org:443\r\n"
                "Proxy-Connection: keep-alive\r\n"
                "Proxy-Authorization: auth_token\r\n\r\n"),

      MockWrite("CONNECT www.example.org:443 HTTP/1.1\r\n"
                "Host: www.example.org:443\r\n"
                "Proxy-Connection: keep-alive\r\n"
                "Proxy-Authorization: auth_token\r\n\r\n"),
  };

  // The server side of the authentication handshake. Note that the response to
  // the final CONNECT request is ERR_CONNECTION_CLOSED.
  MockRead data_reads1[] = {
      MockRead("HTTP/1.1 407 Proxy Authentication Required\r\n"),
      MockRead("Content-Length: 0\r\n"),
      MockRead("Proxy-Connection: keep-alive\r\n"),
      MockRead("Proxy-Authenticate: Mock\r\n\r\n"),

      MockRead("HTTP/1.1 407 Proxy Authentication Required\r\n"),
      MockRead("Content-Length: 0\r\n"),
      MockRead("Proxy-Connection: keep-alive\r\n"),
      MockRead("Proxy-Authenticate: Mock foo\r\n\r\n"),

      MockRead(SYNCHRONOUS, ERR_CONNECTION_CLOSED),
  };

  StaticSocketDataProvider data1(data_reads1, data_writes1);
  session_deps_.socket_factory->AddSocketDataProvider(&data1);

  // The second socket is for the reconnection attempt. Data is identical to the
  // first attempt.
  StaticSocketDataProvider data2(data_reads1, data_writes1);
  session_deps_.socket_factory->AddSocketDataProvider(&data2);

  auto trans =
      std::make_unique<HttpNetworkTransaction>(DEFAULT_PRIORITY, session.get());

  TestCompletionCallback callback;
  int rv = trans->Start(&request, callback.callback(), NetLogWithSource());

  // Two rounds per handshake. After one retry, the error is propagated up the
  // stack.
  for (int i = 0; i < 4; ++i) {
    EXPECT_THAT(callback.GetResult(rv), IsOk());

    const HttpResponseInfo* response = trans->GetResponseInfo();
    ASSERT_TRUE(response);
    ASSERT_TRUE(response->headers);
    EXPECT_EQ(407, response->headers->response_code());
    ASSERT_TRUE(trans->IsReadyToRestartForAuth());

    rv = trans->RestartWithAuth(AuthCredentials(), callback.callback());
  }

  // One shall be the number thou shalt retry, and the number of the retrying
  // shall be one.  Two shalt thou not retry, neither retry thou zero, excepting
  // that thou then proceed to one.  Three is right out.  Once the number one,
  // being the first number, be reached, then lobbest thou thy
  // ERR_CONNECTION_CLOSED towards they network transaction, who shall snuff it.
  EXPECT_EQ(ERR_CONNECTION_CLOSED, callback.GetResult(rv));

  trans.reset();
  session->CloseAllConnections(ERR_FAILED, "Very good reason");
}

// Test a proxy auth scheme that allows default credentials and a proxy server
// that hangs up when credentials are initially sent, and sends a challenge
// again they are retried.
TEST_F(HttpNetworkTransactionTest,
       AuthAllowsDefaultCredentialsTunnelServerChallengesTwice) {
  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("https://www.example.org/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  // Configure against proxy server "myproxy:70".
  session_deps_.proxy_resolution_service =
      ConfiguredProxyResolutionService::CreateFixedFromPacResultForTest(
          "PROXY myproxy:70", TRAFFIC_ANNOTATION_FOR_TESTS);

  auto auth_handler_factory = std::make_unique<HttpAuthHandlerMock::Factory>();
  auth_handler_factory->set_do_init_from_challenge(true);
  auto mock_handler = std::make_unique<HttpAuthHandlerMock>();
  mock_handler->set_allows_default_credentials(true);
  auth_handler_factory->AddMockHandler(std::move(mock_handler),
                                       HttpAuth::AUTH_PROXY);
  // Add another handler for the second challenge. It supports default
  // credentials, but they shouldn't be used, since they were already tried.
  mock_handler = std::make_unique<HttpAuthHandlerMock>();
  mock_handler->set_allows_default_credentials(true);
  auth_handler_factory->AddMockHandler(std::move(mock_handler),
                                       HttpAuth::AUTH_PROXY);
  session_deps_.http_auth_handler_factory = std::move(auth_handler_factory);

  // Add NetLog just so can verify load timing information gets a NetLog ID.
  session_deps_.net_log = NetLog::Get();
  std::unique_ptr<HttpNetworkSession> session = CreateSession(&session_deps_);

  // Should try to establish tunnel.
  MockWrite data_writes1[] = {
      MockWrite("CONNECT www.example.org:443 HTTP/1.1\r\n"
                "Host: www.example.org:443\r\n"
                "Proxy-Connection: keep-alive\r\n\r\n"),
  };

  // The proxy responds to the connect with a 407, using a non-persistent
  // connection.
  MockRead data_reads1[] = {
      // No credentials.
      MockRead("HTTP/1.1 407 Proxy Authentication Required\r\n"),
      MockRead("Proxy-Authenticate: Mock\r\n"),
      MockRead("Proxy-Connection: close\r\n\r\n"),
  };

  // Since the first connection was closed, need to establish another once given
  // credentials.
  MockWrite data_writes2[] = {
      MockWrite("CONNECT www.example.org:443 HTTP/1.1\r\n"
                "Host: www.example.org:443\r\n"
                "Proxy-Connection: keep-alive\r\n"
                "Proxy-Authorization: auth_token\r\n\r\n"),
  };

  MockRead data_reads2[] = {
      MockRead("HTTP/1.1 407 Proxy Authentication Required\r\n"),
      MockRead("Proxy-Authenticate: Mock\r\n"),
      MockRead("Proxy-Connection: close\r\n\r\n"),
  };

  StaticSocketDataProvider data1(data_reads1, data_writes1);
  session_deps_.socket_factory->AddSocketDataProvider(&data1);
  StaticSocketDataProvider data2(data_reads2, data_writes2);
  session_deps_.socket_factory->AddSocketDataProvider(&data2);
  SSLSocketDataProvider ssl(ASYNC, OK);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl);

  auto trans =
      std::make_unique<HttpNetworkTransaction>(DEFAULT_PRIORITY, session.get());

  TestCompletionCallback callback;
  int rv = trans->Start(&request, callback.callback(), NetLogWithSource());
  EXPECT_THAT(callback.GetResult(rv), IsOk());

  const HttpResponseInfo* response = trans->GetResponseInfo();
  ASSERT_TRUE(response);
  ASSERT_TRUE(response->headers);
  EXPECT_EQ(407, response->headers->response_code());
  EXPECT_TRUE(HttpVersion(1, 1) == response->headers->GetHttpVersion());
  EXPECT_TRUE(trans->IsReadyToRestartForAuth());
  EXPECT_FALSE(response->auth_challenge.has_value());

  LoadTimingInfo load_timing_info;
  EXPECT_FALSE(trans->GetLoadTimingInfo(&load_timing_info));

  rv = trans->RestartWithAuth(AuthCredentials(), callback.callback());
  EXPECT_THAT(callback.GetResult(rv), IsOk());
  response = trans->GetResponseInfo();
  ASSERT_TRUE(response);
  ASSERT_TRUE(response->headers);
  EXPECT_EQ(407, response->headers->response_code());
  EXPECT_FALSE(trans->IsReadyToRestartForAuth());
  EXPECT_TRUE(response->auth_challenge.has_value());

  trans.reset();
  session->CloseAllConnections(ERR_FAILED, "Very good reason");
}

// A more nuanced test than GenerateAuthToken test which asserts that
// ERR_INVALID_AUTH_CREDENTIALS does not cause the auth scheme to be
// unnecessarily invalidated, and that if the server co-operates, the
// authentication handshake can continue with the same scheme but with a
// different identity.
TEST_F(HttpNetworkTransactionTest, NonPermanentGenerateAuthTokenError) {
  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("http://www.example.org/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  auto auth_handler_factory = std::make_unique<HttpAuthHandlerMock::Factory>();
  auth_handler_factory->set_do_init_from_challenge(true);

  // First handler. Uses default credentials, but barfs at generate auth token.
  auto mock_handler = std::make_unique<HttpAuthHandlerMock>();
  mock_handler->set_allows_default_credentials(true);
  mock_handler->set_allows_explicit_credentials(true);
  mock_handler->set_connection_based(true);
  mock_handler->SetGenerateExpectation(true, ERR_INVALID_AUTH_CREDENTIALS);
  auth_handler_factory->AddMockHandler(std::move(mock_handler),
                                       HttpAuth::AUTH_SERVER);

  // Add another handler for the second challenge. It supports default
  // credentials, but they shouldn't be used, since they were already tried.
  mock_handler = std::make_unique<HttpAuthHandlerMock>();
  mock_handler->set_allows_default_credentials(true);
  mock_handler->set_allows_explicit_credentials(true);
  mock_handler->set_connection_based(true);
  auth_handler_factory->AddMockHandler(std::move(mock_handler),
                                       HttpAuth::AUTH_SERVER);
  session_deps_.http_auth_handler_factory = std::move(auth_handler_factory);

  std::unique_ptr<HttpNetworkSession> session = CreateSession(&session_deps_);

  MockWrite data_writes1[] = {
      MockWrite("GET / HTTP/1.1\r\n"
                "Host: www.example.org\r\n"
                "Connection: keep-alive\r\n\r\n"),
  };

  MockRead data_reads1[] = {
      MockRead("HTTP/1.1 401 Authentication Required\r\n"
               "WWW-Authenticate: Mock\r\n"
               "Connection: keep-alive\r\n\r\n"),
  };

  // Identical to data_writes1[]. The AuthHandler encounters a
  // ERR_INVALID_AUTH_CREDENTIALS during the GenerateAuthToken stage, so the
  // transaction procceds without an authorization header.
  MockWrite data_writes2[] = {
      MockWrite("GET / HTTP/1.1\r\n"
                "Host: www.example.org\r\n"
                "Connection: keep-alive\r\n\r\n"),
  };

  MockRead data_reads2[] = {
      MockRead("HTTP/1.1 401 Authentication Required\r\n"
               "WWW-Authenticate: Mock\r\n"
               "Connection: keep-alive\r\n\r\n"),
  };

  MockWrite data_writes3[] = {
      MockWrite("GET / HTTP/1.1\r\n"
                "Host: www.example.org\r\n"
                "Connection: keep-alive\r\n"
                "Authorization: auth_token\r\n\r\n"),
  };

  MockRead data_reads3[] = {
      MockRead("HTTP/1.1 200 OK\r\n"
               "Content-Length: 5\r\n"
               "Content-Type: text/plain\r\n"
               "Connection: keep-alive\r\n\r\n"
               "Hello"),
  };

  StaticSocketDataProvider data1(data_reads1, data_writes1);
  session_deps_.socket_factory->AddSocketDataProvider(&data1);

  StaticSocketDataProvider data2(data_reads2, data_writes2);
  session_deps_.socket_factory->AddSocketDataProvider(&data2);

  StaticSocketDataProvider data3(data_reads3, data_writes3);
  session_deps_.socket_factory->AddSocketDataProvider(&data3);

  auto trans =
      std::make_unique<HttpNetworkTransaction>(DEFAULT_PRIORITY, session.get());

  TestCompletionCallback callback;
  int rv = trans->Start(&request, callback.callback(), NetLogWithSource());
  EXPECT_THAT(callback.GetResult(rv), IsOk());

  const HttpResponseInfo* response = trans->GetResponseInfo();
  ASSERT_TRUE(response);
  ASSERT_TRUE(response->headers);
  EXPECT_TRUE(HttpVersion(1, 1) == response->headers->GetHttpVersion());

  // The following three tests assert that an authentication challenge was
  // received and that the stack is ready to respond to the challenge using
  // ambient credentials.
  EXPECT_EQ(401, response->headers->response_code());
  EXPECT_TRUE(trans->IsReadyToRestartForAuth());
  EXPECT_FALSE(response->auth_challenge.has_value());

  rv = trans->RestartWithAuth(AuthCredentials(), callback.callback());
  EXPECT_THAT(callback.GetResult(rv), IsOk());
  response = trans->GetResponseInfo();
  ASSERT_TRUE(response);
  ASSERT_TRUE(response->headers);

  // The following three tests assert that an authentication challenge was
  // received and that the stack needs explicit credentials before it is ready
  // to respond to the challenge.
  EXPECT_EQ(401, response->headers->response_code());
  EXPECT_FALSE(trans->IsReadyToRestartForAuth());
  EXPECT_TRUE(response->auth_challenge.has_value());

  rv = trans->RestartWithAuth(AuthCredentials(), callback.callback());
  EXPECT_THAT(callback.GetResult(rv), IsOk());
  response = trans->GetResponseInfo();
  ASSERT_TRUE(response);
  ASSERT_TRUE(response->headers);
  EXPECT_EQ(200, response->headers->response_code());

  trans.reset();
  session->CloseAllConnections(ERR_FAILED, "Very good reason");
}

// Proxy resolver that returns a proxy with the same host and port for different
// schemes, based on the path of the URL being requests.
class SameProxyWithDifferentSchemesProxyResolver : public ProxyResolver {
 public:
  SameProxyWithDifferentSchemesProxyResolver() = default;

  SameProxyWithDifferentSchemesProxyResolver(
      const SameProxyWithDifferentSchemesProxyResolver&) = delete;
  SameProxyWithDifferentSchemesProxyResolver& operator=(
      const SameProxyWithDifferentSchemesProxyResolver&) = delete;

  ~SameProxyWithDifferentSchemesProxyResolver() override = default;

  static constexpr uint16_t kProxyPort = 10000;

  static HostPortPair ProxyHostPortPair() {
    return HostPortPair("proxy.test", kProxyPort);
  }

  static std::string ProxyHostPortPairAsString() {
    return ProxyHostPortPair().ToString();
  }

  // ProxyResolver implementation.
  int GetProxyForURL(const GURL& url,
                     const NetworkIsolationKey& network_isolation_key,
                     ProxyInfo* results,
                     CompletionOnceCallback callback,
                     std::unique_ptr<Request>* request,
                     const NetLogWithSource& /*net_log*/) override {
    *results = ProxyInfo();
    results->set_traffic_annotation(
        MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS));
    if (url.path() == "/socks4") {
      results->UsePacString("SOCKS " + ProxyHostPortPairAsString());
      return OK;
    }
    if (url.path() == "/socks5") {
      results->UsePacString("SOCKS5 " + ProxyHostPortPairAsString());
      return OK;
    }
    if (url.path() == "/http") {
      results->UsePacString("PROXY " + ProxyHostPortPairAsString());
      return OK;
    }
    if (url.path() == "/https") {
      results->UsePacString("HTTPS " + ProxyHostPortPairAsString());
      return OK;
    }
    NOTREACHED();
    return ERR_NOT_IMPLEMENTED;
  }
};

class SameProxyWithDifferentSchemesProxyResolverFactory
    : public ProxyResolverFactory {
 public:
  SameProxyWithDifferentSchemesProxyResolverFactory()
      : ProxyResolverFactory(false) {}

  SameProxyWithDifferentSchemesProxyResolverFactory(
      const SameProxyWithDifferentSchemesProxyResolverFactory&) = delete;
  SameProxyWithDifferentSchemesProxyResolverFactory& operator=(
      const SameProxyWithDifferentSchemesProxyResolverFactory&) = delete;

  int CreateProxyResolver(const scoped_refptr<PacFileData>& pac_script,
                          std::unique_ptr<ProxyResolver>* resolver,
                          CompletionOnceCallback callback,
                          std::unique_ptr<Request>* request) override {
    *resolver = std::make_unique<SameProxyWithDifferentSchemesProxyResolver>();
    return OK;
  }
};

// Check that when different proxy schemes are all applied to a proxy at the
// same address, the connections are not grouped together.  i.e., a request to
// foo.com using proxy.com as an HTTPS proxy won't use the same socket as a
// request to foo.com using proxy.com as an HTTP proxy.
TEST_F(HttpNetworkTransactionTest, SameDestinationForDifferentProxyTypes) {
  session_deps_.proxy_resolution_service =
      std::make_unique<ConfiguredProxyResolutionService>(
          std::make_unique<ProxyConfigServiceFixed>(ProxyConfigWithAnnotation(
              ProxyConfig::CreateAutoDetect(), TRAFFIC_ANNOTATION_FOR_TESTS)),
          std::make_unique<SameProxyWithDifferentSchemesProxyResolverFactory>(),
          nullptr, /*quick_check_enabled=*/true);

  std::unique_ptr<HttpNetworkSession> session = CreateSession(&session_deps_);

  MockWrite socks_writes[] = {
      MockWrite(SYNCHRONOUS, kSOCKS4OkRequestLocalHostPort80,
                kSOCKS4OkRequestLocalHostPort80Length),
      MockWrite(SYNCHRONOUS,
                "GET /socks4 HTTP/1.1\r\n"
                "Host: test\r\n"
                "Connection: keep-alive\r\n\r\n"),
  };
  MockRead socks_reads[] = {
      MockRead(SYNCHRONOUS, kSOCKS4OkReply, kSOCKS4OkReplyLength),
      MockRead("HTTP/1.0 200 OK\r\n"
               "Connection: keep-alive\r\n"
               "Content-Length: 15\r\n\r\n"
               "SOCKS4 Response"),
  };
  StaticSocketDataProvider socks_data(socks_reads, socks_writes);
  session_deps_.socket_factory->AddSocketDataProvider(&socks_data);

  const char kSOCKS5Request[] = {
      0x05,                  // Version
      0x01,                  // Command (CONNECT)
      0x00,                  // Reserved
      0x03,                  // Address type (DOMAINNAME)
      0x04,                  // Length of domain (4)
      't',  'e',  's', 't',  // Domain string
      0x00, 0x50,            // 16-bit port (80)
  };
  MockWrite socks5_writes[] = {
      MockWrite(ASYNC, kSOCKS5GreetRequest, kSOCKS5GreetRequestLength),
      MockWrite(ASYNC, kSOCKS5Request, std::size(kSOCKS5Request)),
      MockWrite(SYNCHRONOUS,
                "GET /socks5 HTTP/1.1\r\n"
                "Host: test\r\n"
                "Connection: keep-alive\r\n\r\n"),
  };
  MockRead socks5_reads[] = {
      MockRead(ASYNC, kSOCKS5GreetResponse, kSOCKS5GreetResponseLength),
      MockRead(ASYNC, kSOCKS5OkResponse, kSOCKS5OkResponseLength),
      MockRead("HTTP/1.0 200 OK\r\n"
               "Connection: keep-alive\r\n"
               "Content-Length: 15\r\n\r\n"
               "SOCKS5 Response"),
  };
  StaticSocketDataProvider socks5_data(socks5_reads, socks5_writes);
  session_deps_.socket_factory->AddSocketDataProvider(&socks5_data);

  MockWrite http_writes[] = {
      MockWrite(SYNCHRONOUS,
                "GET http://test/http HTTP/1.1\r\n"
                "Host: test\r\n"
                "Proxy-Connection: keep-alive\r\n\r\n"),
  };
  MockRead http_reads[] = {
      MockRead("HTTP/1.1 200 OK\r\n"
               "Proxy-Connection: keep-alive\r\n"
               "Content-Length: 13\r\n\r\n"
               "HTTP Response"),
  };
  StaticSocketDataProvider http_data(http_reads, http_writes);
  session_deps_.socket_factory->AddSocketDataProvider(&http_data);

  MockWrite https_writes[] = {
      MockWrite(SYNCHRONOUS,
                "GET http://test/https HTTP/1.1\r\n"
                "Host: test\r\n"
                "Proxy-Connection: keep-alive\r\n\r\n"),
  };
  MockRead https_reads[] = {
      MockRead("HTTP/1.1 200 OK\r\n"
               "Proxy-Connection: keep-alive\r\n"
               "Content-Length: 14\r\n\r\n"
               "HTTPS Response"),
  };
  StaticSocketDataProvider https_data(https_reads, https_writes);
  session_deps_.socket_factory->AddSocketDataProvider(&https_data);
  SSLSocketDataProvider ssl(SYNCHRONOUS, OK);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl);

  SSLSocketDataProvider ssl2(SYNCHRONOUS, OK);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl2);

  struct TestCase {
    GURL url;
    std::string expected_response;
    // How many idle sockets there should be in the SOCKS 4/5 proxy socket pools
    // after the test.
    int expected_idle_socks4_sockets;
    int expected_idle_socks5_sockets;
    // How many idle sockets there should be in the HTTP/HTTPS proxy socket
    // pools after the test.
    int expected_idle_http_sockets;
    int expected_idle_https_sockets;
  } const kTestCases[] = {
      {GURL("http://test/socks4"), "SOCKS4 Response", 1, 0, 0, 0},
      {GURL("http://test/socks5"), "SOCKS5 Response", 1, 1, 0, 0},
      {GURL("http://test/http"), "HTTP Response", 1, 1, 1, 0},
      {GURL("http://test/https"), "HTTPS Response", 1, 1, 1, 1},
  };

  for (const auto& test_case : kTestCases) {
    SCOPED_TRACE(test_case.url);

    HttpRequestInfo request;
    request.method = "GET";
    request.url = test_case.url;
    request.traffic_annotation =
        net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);
    ConnectedHandler connected_handler;

    auto transaction = std::make_unique<HttpNetworkTransaction>(
        DEFAULT_PRIORITY, session.get());

    transaction->SetConnectedCallback(connected_handler.Callback());

    TestCompletionCallback callback;
    int rv =
        transaction->Start(&request, callback.callback(), NetLogWithSource());
    EXPECT_THAT(callback.GetResult(rv), IsOk());

    const HttpResponseInfo* response = transaction->GetResponseInfo();
    ASSERT_TRUE(response);
    ASSERT_TRUE(response->headers);
    EXPECT_EQ(200, response->headers->response_code());
    std::string response_data;
    EXPECT_THAT(ReadTransaction(transaction.get(), &response_data), IsOk());
    EXPECT_EQ(test_case.expected_response, response_data);

    TransportInfo expected_transport;
    expected_transport.type = TransportType::kProxied;
    expected_transport.endpoint =
        IPEndPoint(IPAddress::IPv4Localhost(),
                   SameProxyWithDifferentSchemesProxyResolver::kProxyPort);
    EXPECT_THAT(connected_handler.transports(),
                ElementsAre(expected_transport));

    // Return the socket to the socket pool, so can make sure it's not used for
    // the next requests.
    transaction.reset();
    base::RunLoop().RunUntilIdle();

    // Check the number of idle sockets in the pool, to make sure that used
    // sockets are indeed being returned to the socket pool.  If each request
    // doesn't return an idle socket to the pool, the test would incorrectly
    // pass.
    EXPECT_EQ(test_case.expected_idle_socks4_sockets,
              session
                  ->GetSocketPool(
                      HttpNetworkSession::NORMAL_SOCKET_POOL,
                      ProxyServer(ProxyServer::SCHEME_SOCKS4,
                                  SameProxyWithDifferentSchemesProxyResolver::
                                      ProxyHostPortPair()))
                  ->IdleSocketCount());
    EXPECT_EQ(test_case.expected_idle_socks5_sockets,
              session
                  ->GetSocketPool(
                      HttpNetworkSession::NORMAL_SOCKET_POOL,
                      ProxyServer(ProxyServer::SCHEME_SOCKS5,
                                  SameProxyWithDifferentSchemesProxyResolver::
                                      ProxyHostPortPair()))
                  ->IdleSocketCount());
    EXPECT_EQ(test_case.expected_idle_http_sockets,
              session
                  ->GetSocketPool(
                      HttpNetworkSession::NORMAL_SOCKET_POOL,
                      ProxyServer(ProxyServer::SCHEME_HTTP,
                                  SameProxyWithDifferentSchemesProxyResolver::
                                      ProxyHostPortPair()))
                  ->IdleSocketCount());
    EXPECT_EQ(test_case.expected_idle_https_sockets,
              session
                  ->GetSocketPool(
                      HttpNetworkSession::NORMAL_SOCKET_POOL,
                      ProxyServer(ProxyServer::SCHEME_HTTPS,
                                  SameProxyWithDifferentSchemesProxyResolver::
                                      ProxyHostPortPair()))
                  ->IdleSocketCount());
  }
}

// Test the load timing for HTTPS requests with an HTTP proxy.
TEST_F(HttpNetworkTransactionTest, HttpProxyLoadTimingNoPacTwoRequests) {
  HttpRequestInfo request1;
  request1.method = "GET";
  request1.url = GURL("https://www.example.org/1");
  request1.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  HttpRequestInfo request2;
  request2.method = "GET";
  request2.url = GURL("https://www.example.org/2");
  request2.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  // Configure against proxy server "myproxy:70".
  session_deps_.proxy_resolution_service =
      ConfiguredProxyResolutionService::CreateFixedForTest(
          "myproxy:70", TRAFFIC_ANNOTATION_FOR_TESTS);
  NetLogWithSource net_log_with_source =
      NetLogWithSource::Make(NetLogSourceType::NONE);
  session_deps_.net_log = NetLog::Get();
  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));

  // Since we have proxy, should try to establish tunnel.
  MockWrite data_writes1[] = {
      MockWrite("CONNECT www.example.org:443 HTTP/1.1\r\n"
                "Host: www.example.org:443\r\n"
                "Proxy-Connection: keep-alive\r\n\r\n"),

      MockWrite("GET /1 HTTP/1.1\r\n"
                "Host: www.example.org\r\n"
                "Connection: keep-alive\r\n\r\n"),

      MockWrite("GET /2 HTTP/1.1\r\n"
                "Host: www.example.org\r\n"
                "Connection: keep-alive\r\n\r\n"),
  };

  // The proxy responds to the connect with a 407, using a persistent
  // connection.
  MockRead data_reads1[] = {
    MockRead("HTTP/1.1 200 Connection Established\r\n\r\n"),

    MockRead("HTTP/1.1 200 OK\r\n"),
    MockRead("Content-Length: 1\r\n\r\n"),
    MockRead(SYNCHRONOUS, "1"),

    MockRead("HTTP/1.1 200 OK\r\n"),
    MockRead("Content-Length: 2\r\n\r\n"),
    MockRead(SYNCHRONOUS, "22"),
  };

  StaticSocketDataProvider data1(data_reads1, data_writes1);
  session_deps_.socket_factory->AddSocketDataProvider(&data1);
  SSLSocketDataProvider ssl(ASYNC, OK);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl);

  TestCompletionCallback callback1;
  auto trans1 =
      std::make_unique<HttpNetworkTransaction>(DEFAULT_PRIORITY, session.get());

  int rv = trans1->Start(&request1, callback1.callback(), net_log_with_source);
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback1.WaitForResult();
  EXPECT_THAT(rv, IsOk());

  const HttpResponseInfo* response1 = trans1->GetResponseInfo();
  ASSERT_TRUE(response1);
  EXPECT_TRUE(response1->proxy_server.is_http());
  ASSERT_TRUE(response1->headers);
  EXPECT_EQ(1, response1->headers->GetContentLength());

  LoadTimingInfo load_timing_info1;
  EXPECT_TRUE(trans1->GetLoadTimingInfo(&load_timing_info1));
  TestLoadTimingNotReused(load_timing_info1, CONNECT_TIMING_HAS_SSL_TIMES);

  trans1.reset();

  TestCompletionCallback callback2;
  auto trans2 =
      std::make_unique<HttpNetworkTransaction>(DEFAULT_PRIORITY, session.get());

  rv = trans2->Start(&request2, callback2.callback(), net_log_with_source);
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback2.WaitForResult();
  EXPECT_THAT(rv, IsOk());

  const HttpResponseInfo* response2 = trans2->GetResponseInfo();
  ASSERT_TRUE(response2);
  EXPECT_TRUE(response2->proxy_server.is_http());
  ASSERT_TRUE(response2->headers);
  EXPECT_EQ(2, response2->headers->GetContentLength());

  LoadTimingInfo load_timing_info2;
  EXPECT_TRUE(trans2->GetLoadTimingInfo(&load_timing_info2));
  TestLoadTimingReused(load_timing_info2);

  EXPECT_EQ(load_timing_info1.socket_log_id, load_timing_info2.socket_log_id);

  trans2.reset();
  session->CloseAllConnections(ERR_FAILED, "Very good reason");
}

// Test the load timing for HTTPS requests with an HTTP proxy and a PAC script.
TEST_F(HttpNetworkTransactionTest, HttpProxyLoadTimingWithPacTwoRequests) {
  HttpRequestInfo request1;
  request1.method = "GET";
  request1.url = GURL("https://www.example.org/1");
  request1.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  HttpRequestInfo request2;
  request2.method = "GET";
  request2.url = GURL("https://www.example.org/2");
  request2.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  // Configure against proxy server "myproxy:70".
  session_deps_.proxy_resolution_service =
      ConfiguredProxyResolutionService::CreateFixedFromPacResultForTest(
          "PROXY myproxy:70", TRAFFIC_ANNOTATION_FOR_TESTS);
  NetLogWithSource net_log_with_source =
      NetLogWithSource::Make(NetLogSourceType::NONE);
  session_deps_.net_log = NetLog::Get();
  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));

  // Since we have proxy, should try to establish tunnel.
  MockWrite data_writes1[] = {
      MockWrite("CONNECT www.example.org:443 HTTP/1.1\r\n"
                "Host: www.example.org:443\r\n"
                "Proxy-Connection: keep-alive\r\n\r\n"),

      MockWrite("GET /1 HTTP/1.1\r\n"
                "Host: www.example.org\r\n"
                "Connection: keep-alive\r\n\r\n"),

      MockWrite("GET /2 HTTP/1.1\r\n"
                "Host: www.example.org\r\n"
                "Connection: keep-alive\r\n\r\n"),
  };

  // The proxy responds to the connect with a 407, using a persistent
  // connection.
  MockRead data_reads1[] = {
    MockRead("HTTP/1.1 200 Connection Established\r\n\r\n"),

    MockRead("HTTP/1.1 200 OK\r\n"),
    MockRead("Content-Length: 1\r\n\r\n"),
    MockRead(SYNCHRONOUS, "1"),

    MockRead("HTTP/1.1 200 OK\r\n"),
    MockRead("Content-Length: 2\r\n\r\n"),
    MockRead(SYNCHRONOUS, "22"),
  };

  StaticSocketDataProvider data1(data_reads1, data_writes1);
  session_deps_.socket_factory->AddSocketDataProvider(&data1);
  SSLSocketDataProvider ssl(ASYNC, OK);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl);

  TestCompletionCallback callback1;
  auto trans1 =
      std::make_unique<HttpNetworkTransaction>(DEFAULT_PRIORITY, session.get());

  int rv = trans1->Start(&request1, callback1.callback(), net_log_with_source);
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback1.WaitForResult();
  EXPECT_THAT(rv, IsOk());

  const HttpResponseInfo* response1 = trans1->GetResponseInfo();
  ASSERT_TRUE(response1);
  ASSERT_TRUE(response1->headers);
  EXPECT_EQ(1, response1->headers->GetContentLength());

  LoadTimingInfo load_timing_info1;
  EXPECT_TRUE(trans1->GetLoadTimingInfo(&load_timing_info1));
  TestLoadTimingNotReusedWithPac(load_timing_info1,
                                 CONNECT_TIMING_HAS_SSL_TIMES);

  trans1.reset();

  TestCompletionCallback callback2;
  auto trans2 =
      std::make_unique<HttpNetworkTransaction>(DEFAULT_PRIORITY, session.get());

  rv = trans2->Start(&request2, callback2.callback(), net_log_with_source);
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback2.WaitForResult();
  EXPECT_THAT(rv, IsOk());

  const HttpResponseInfo* response2 = trans2->GetResponseInfo();
  ASSERT_TRUE(response2);
  ASSERT_TRUE(response2->headers);
  EXPECT_EQ(2, response2->headers->GetContentLength());

  LoadTimingInfo load_timing_info2;
  EXPECT_TRUE(trans2->GetLoadTimingInfo(&load_timing_info2));
  TestLoadTimingReusedWithPac(load_timing_info2);

  EXPECT_EQ(load_timing_info1.socket_log_id, load_timing_info2.socket_log_id);

  trans2.reset();
  session->CloseAllConnections(ERR_FAILED, "Very good reason");
}

// Make sure that NetworkIsolationKeys are passed down to the proxy layer.
TEST_F(HttpNetworkTransactionTest, ProxyResolvedWithNetworkIsolationKey) {
  const SchemefulSite kSite(GURL("https://foo.test/"));

  ProxyConfig proxy_config;
  proxy_config.set_auto_detect(true);
  proxy_config.set_pac_url(GURL("http://fooproxyurl"));

  CapturingProxyResolver capturing_proxy_resolver;
  capturing_proxy_resolver.set_proxy_server(ProxyServer::Direct());
  session_deps_.proxy_resolution_service =
      std::make_unique<ConfiguredProxyResolutionService>(
          std::make_unique<ProxyConfigServiceFixed>(ProxyConfigWithAnnotation(
              proxy_config, TRAFFIC_ANNOTATION_FOR_TESTS)),
          std::make_unique<CapturingProxyResolverFactory>(
              &capturing_proxy_resolver),
          nullptr, /*quick_check_enabled=*/true);

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));

  // No need to continue with the network request - proxy resolution occurs
  // before establishing a data.
  StaticSocketDataProvider data{base::span<MockRead>(),
                                base::span<MockWrite>()};
  data.set_connect_data(MockConnect(SYNCHRONOUS, ERR_FAILED));
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  // Run first request until an auth challenge is observed.
  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("http://foo.test/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);
  HttpNetworkTransaction trans(LOWEST, session.get());
  TestCompletionCallback callback;
  int rv = trans.Start(&request, callback.callback(), NetLogWithSource());
  EXPECT_THAT(callback.GetResult(rv), IsError(ERR_FAILED));
}

// Test that a failure in resolving the proxy hostname is retrievable.
TEST_F(HttpNetworkTransactionTest, ProxyHostResolutionFailure) {
  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("http://www.example.org/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  // Configure against https proxy server "proxy:70".
  session_deps_.proxy_resolution_service =
      ConfiguredProxyResolutionService::CreateFixedForTest(
          "https://proxy:70", TRAFFIC_ANNOTATION_FOR_TESTS);
  auto resolver = std::make_unique<MockHostResolver>();
  resolver->rules()->AddSimulatedTimeoutFailure("proxy");
  session_deps_.net_log = net::NetLog::Get();
  session_deps_.host_resolver = std::move(resolver);
  std::unique_ptr<HttpNetworkSession> session = CreateSession(&session_deps_);
  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());
  TestCompletionCallback callback;

  int rv = trans.Start(&request, callback.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));
  EXPECT_THAT(callback.WaitForResult(), IsError(ERR_PROXY_CONNECTION_FAILED));

  const HttpResponseInfo* response = trans.GetResponseInfo();
  ASSERT_TRUE(response);
  EXPECT_THAT(response->resolve_error_info.error, IsError(ERR_DNS_TIMED_OUT));
}

// Test a simple get through an HTTPS Proxy.
TEST_F(HttpNetworkTransactionTest, HttpsProxyGet) {
  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("http://www.example.org/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  // Configure against https proxy server "proxy:70".
  session_deps_.proxy_resolution_service =
      ConfiguredProxyResolutionService::CreateFixedForTest(
          "https://proxy:70", TRAFFIC_ANNOTATION_FOR_TESTS);
  session_deps_.net_log = NetLog::Get();
  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));

  // Since we have proxy, should use full url
  MockWrite data_writes1[] = {
      MockWrite(
          "GET http://www.example.org/ HTTP/1.1\r\n"
          "Host: www.example.org\r\n"
          "Proxy-Connection: keep-alive\r\n\r\n"),
  };

  MockRead data_reads1[] = {
    MockRead("HTTP/1.1 200 OK\r\n"),
    MockRead("Content-Type: text/html; charset=iso-8859-1\r\n"),
    MockRead("Content-Length: 100\r\n\r\n"),
    MockRead(SYNCHRONOUS, OK),
  };

  StaticSocketDataProvider data1(data_reads1, data_writes1);
  session_deps_.socket_factory->AddSocketDataProvider(&data1);
  SSLSocketDataProvider ssl(ASYNC, OK);
  ssl.ssl_info.cert =
      ImportCertFromFile(GetTestCertsDirectory(), "ok_cert.pem");
  ASSERT_TRUE(ssl.ssl_info.cert);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl);

  ConnectedHandler connected_handler;
  TestCompletionCallback callback1;

  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

  trans.SetConnectedCallback(connected_handler.Callback());

  int rv = trans.Start(&request, callback1.callback(),
                       NetLogWithSource::Make(NetLogSourceType::NONE));
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback1.WaitForResult();
  EXPECT_THAT(rv, IsOk());

  LoadTimingInfo load_timing_info;
  EXPECT_TRUE(trans.GetLoadTimingInfo(&load_timing_info));
  TestLoadTimingNotReused(load_timing_info,
                          CONNECT_TIMING_HAS_CONNECT_TIMES_ONLY);

  const HttpResponseInfo* response = trans.GetResponseInfo();
  ASSERT_TRUE(response);

  EXPECT_TRUE(response->proxy_server.is_https());
  EXPECT_TRUE(response->headers->IsKeepAlive());
  EXPECT_EQ(200, response->headers->response_code());
  EXPECT_EQ(100, response->headers->GetContentLength());
  EXPECT_TRUE(HttpVersion(1, 1) == response->headers->GetHttpVersion());

  // DNS aliases should be empty when using a proxy.
  EXPECT_TRUE(response->dns_aliases.empty());

  TransportInfo expected_transport;
  expected_transport.type = TransportType::kProxied;
  expected_transport.endpoint = IPEndPoint(IPAddress::IPv4Localhost(), 70);
  EXPECT_THAT(connected_handler.transports(), ElementsAre(expected_transport));

  // The password prompt info should not be set.
  EXPECT_FALSE(response->auth_challenge.has_value());

  // Although we use an HTTPS proxy, the `SSLInfo` from that connection should
  // not be reported as a property of the origin.
  EXPECT_FALSE(response->ssl_info.cert);
}

// Test a SPDY get through an HTTPS Proxy.
TEST_F(HttpNetworkTransactionTest, HttpsProxySpdyGet) {
  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("http://www.example.org/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  // Configure against https proxy server "proxy:70".
  session_deps_.proxy_resolution_service =
      ConfiguredProxyResolutionService::CreateFixedForTest(
          "https://proxy:70", TRAFFIC_ANNOTATION_FOR_TESTS);
  session_deps_.net_log = NetLog::Get();
  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));

  // fetch http://www.example.org/ via SPDY
  spdy::SpdySerializedFrame req(
      spdy_util_.ConstructSpdyGet("http://www.example.org/", 1, LOWEST));
  MockWrite spdy_writes[] = {CreateMockWrite(req, 0)};

  spdy::SpdySerializedFrame resp(
      spdy_util_.ConstructSpdyGetReply(nullptr, 0, 1));
  spdy::SpdySerializedFrame data(spdy_util_.ConstructSpdyDataFrame(1, true));
  MockRead spdy_reads[] = {
      CreateMockRead(resp, 1), CreateMockRead(data, 2), MockRead(ASYNC, 0, 3),
  };

  SequencedSocketData spdy_data(spdy_reads, spdy_writes);
  session_deps_.socket_factory->AddSocketDataProvider(&spdy_data);

  SSLSocketDataProvider ssl(ASYNC, OK);
  ssl.ssl_info.cert =
      ImportCertFromFile(GetTestCertsDirectory(), "ok_cert.pem");
  ASSERT_TRUE(ssl.ssl_info.cert);
  ssl.next_proto = kProtoHTTP2;
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl);

  ConnectedHandler connected_handler;
  TestCompletionCallback callback1;

  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

  trans.SetConnectedCallback(connected_handler.Callback());

  int rv = trans.Start(&request, callback1.callback(),
                       NetLogWithSource::Make(NetLogSourceType::NONE));
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback1.WaitForResult();
  EXPECT_THAT(rv, IsOk());

  LoadTimingInfo load_timing_info;
  EXPECT_TRUE(trans.GetLoadTimingInfo(&load_timing_info));
  TestLoadTimingNotReused(load_timing_info,
                          CONNECT_TIMING_HAS_CONNECT_TIMES_ONLY);

  const HttpResponseInfo* response = trans.GetResponseInfo();
  ASSERT_TRUE(response);
  EXPECT_TRUE(response->proxy_server.is_https());
  ASSERT_TRUE(response->headers);
  EXPECT_EQ("HTTP/1.1 200", response->headers->GetStatusLine());

  // DNS aliases should be empty when using a proxy.
  EXPECT_TRUE(response->dns_aliases.empty());

  TransportInfo expected_transport;
  expected_transport.type = TransportType::kProxied;
  expected_transport.endpoint = IPEndPoint(IPAddress::IPv4Localhost(), 70);
  EXPECT_THAT(connected_handler.transports(), ElementsAre(expected_transport));

  std::string response_data;
  ASSERT_THAT(ReadTransaction(&trans, &response_data), IsOk());
  EXPECT_EQ(kUploadData, response_data);

  // Although we use an HTTPS proxy, the `SSLInfo` from that connection should
  // not be reported as a property of the origin.
  EXPECT_FALSE(response->ssl_info.cert);
}

// Verifies that a session which races and wins against the owning transaction
// (completing prior to host resolution), doesn't fail the transaction.
// Regression test for crbug.com/334413.
TEST_F(HttpNetworkTransactionTest, HttpsProxySpdyGetWithSessionRace) {
  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("http://www.example.org/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  // Configure SPDY proxy server "proxy:70".
  session_deps_.proxy_resolution_service =
      ConfiguredProxyResolutionService::CreateFixedForTest(
          "https://proxy:70", TRAFFIC_ANNOTATION_FOR_TESTS);
  NetLogWithSource net_log_with_source =
      NetLogWithSource::Make(NetLogSourceType::NONE);
  session_deps_.net_log = NetLog::Get();
  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));

  // Fetch http://www.example.org/ through the SPDY proxy.
  spdy::SpdySerializedFrame req(
      spdy_util_.ConstructSpdyGet("http://www.example.org/", 1, LOWEST));
  MockWrite spdy_writes[] = {CreateMockWrite(req, 0)};

  spdy::SpdySerializedFrame resp(
      spdy_util_.ConstructSpdyGetReply(nullptr, 0, 1));
  spdy::SpdySerializedFrame data(spdy_util_.ConstructSpdyDataFrame(1, true));
  MockRead spdy_reads[] = {
      CreateMockRead(resp, 1), CreateMockRead(data, 2), MockRead(ASYNC, 0, 3),
  };

  SequencedSocketData spdy_data(spdy_reads, spdy_writes);
  session_deps_.socket_factory->AddSocketDataProvider(&spdy_data);

  SSLSocketDataProvider ssl(ASYNC, OK);
  ssl.next_proto = kProtoHTTP2;
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl);

  TestCompletionCallback callback1;

  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

  // Stall the hostname resolution begun by the transaction.
  session_deps_.host_resolver->set_ondemand_mode(true);

  int rv = trans.Start(&request, callback1.callback(), net_log_with_source);
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  // Race a session to the proxy, which completes first.
  session_deps_.host_resolver->set_ondemand_mode(false);
  SpdySessionKey key(HostPortPair("proxy", 70), ProxyServer::Direct(),
                     PRIVACY_MODE_DISABLED,
                     SpdySessionKey::IsProxySession::kTrue, SocketTag(),
                     NetworkIsolationKey(), SecureDnsPolicy::kAllow);
  base::WeakPtr<SpdySession> spdy_session =
      CreateSpdySession(session.get(), key, net_log_with_source);

  // Unstall the resolution begun by the transaction.
  session_deps_.host_resolver->set_ondemand_mode(true);
  session_deps_.host_resolver->ResolveAllPending();

  EXPECT_FALSE(callback1.have_result());
  rv = callback1.WaitForResult();
  EXPECT_THAT(rv, IsOk());

  const HttpResponseInfo* response = trans.GetResponseInfo();
  ASSERT_TRUE(response);
  ASSERT_TRUE(response->headers);
  EXPECT_EQ("HTTP/1.1 200", response->headers->GetStatusLine());

  std::string response_data;
  ASSERT_THAT(ReadTransaction(&trans, &response_data), IsOk());
  EXPECT_EQ(kUploadData, response_data);
}

// Test a SPDY get through an HTTPS Proxy.
TEST_F(HttpNetworkTransactionTest, HttpsProxySpdyGetWithProxyAuth) {
  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("http://www.example.org/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  // Configure against https proxy server "myproxy:70".
  session_deps_.proxy_resolution_service =
      ConfiguredProxyResolutionService::CreateFixedForTest(
          "https://myproxy:70", TRAFFIC_ANNOTATION_FOR_TESTS);
  session_deps_.net_log = NetLog::Get();
  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));

  // The first request will be a bare GET, the second request will be a
  // GET with a Proxy-Authorization header.
  spdy_util_.set_default_url(request.url);
  spdy::SpdySerializedFrame req_get(
      spdy_util_.ConstructSpdyGet(nullptr, 0, 1, LOWEST));
  spdy_util_.UpdateWithStreamDestruction(1);
  const char* const kExtraAuthorizationHeaders[] = {
    "proxy-authorization", "Basic Zm9vOmJhcg=="
  };
  spdy::SpdySerializedFrame req_get_authorization(spdy_util_.ConstructSpdyGet(
      kExtraAuthorizationHeaders, std::size(kExtraAuthorizationHeaders) / 2, 3,
      LOWEST));
  MockWrite spdy_writes[] = {
      CreateMockWrite(req_get, 0), CreateMockWrite(req_get_authorization, 3),
  };

  // The first response is a 407 proxy authentication challenge, and the second
  // response will be a 200 response since the second request includes a valid
  // Authorization header.
  const char* const kExtraAuthenticationHeaders[] = {
    "proxy-authenticate", "Basic realm=\"MyRealm1\""
  };
  spdy::SpdySerializedFrame resp_authentication(
      spdy_util_.ConstructSpdyReplyError(
          "407", kExtraAuthenticationHeaders,
          std::size(kExtraAuthenticationHeaders) / 2, 1));
  spdy::SpdySerializedFrame body_authentication(
      spdy_util_.ConstructSpdyDataFrame(1, true));
  spdy::SpdySerializedFrame resp_data(
      spdy_util_.ConstructSpdyGetReply(nullptr, 0, 3));
  spdy::SpdySerializedFrame body_data(
      spdy_util_.ConstructSpdyDataFrame(3, true));
  MockRead spdy_reads[] = {
      CreateMockRead(resp_authentication, 1),
      CreateMockRead(body_authentication, 2, SYNCHRONOUS),
      CreateMockRead(resp_data, 4),
      CreateMockRead(body_data, 5),
      MockRead(ASYNC, 0, 6),
  };

  SequencedSocketData data(spdy_reads, spdy_writes);
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  SSLSocketDataProvider ssl(ASYNC, OK);
  ssl.next_proto = kProtoHTTP2;
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl);

  TestCompletionCallback callback1;

  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

  int rv = trans.Start(&request, callback1.callback(),
                       NetLogWithSource::Make(NetLogSourceType::NONE));
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback1.WaitForResult();
  EXPECT_THAT(rv, IsOk());

  const HttpResponseInfo* const response = trans.GetResponseInfo();

  ASSERT_TRUE(response);
  ASSERT_TRUE(response->headers);
  EXPECT_EQ(407, response->headers->response_code());
  EXPECT_TRUE(response->was_fetched_via_spdy);
  EXPECT_TRUE(CheckBasicSecureProxyAuth(response->auth_challenge));

  TestCompletionCallback callback2;

  rv = trans.RestartWithAuth(AuthCredentials(kFoo, kBar), callback2.callback());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback2.WaitForResult();
  EXPECT_THAT(rv, IsOk());

  const HttpResponseInfo* const response_restart = trans.GetResponseInfo();

  ASSERT_TRUE(response_restart);
  ASSERT_TRUE(response_restart->headers);
  EXPECT_EQ(200, response_restart->headers->response_code());
  // The password prompt info should not be set.
  EXPECT_FALSE(response_restart->auth_challenge.has_value());
}

// Test a SPDY CONNECT through an HTTPS Proxy to an HTTPS (non-SPDY) Server.
TEST_F(HttpNetworkTransactionTest, HttpsProxySpdyConnectHttps) {
  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("https://www.example.org/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  // Configure against https proxy server "proxy:70".
  session_deps_.proxy_resolution_service =
      ConfiguredProxyResolutionService::CreateFixedForTest(
          "https://proxy:70", TRAFFIC_ANNOTATION_FOR_TESTS);
  session_deps_.net_log = NetLog::Get();
  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));

  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

  // CONNECT to www.example.org:443 via SPDY
  spdy::SpdySerializedFrame connect(spdy_util_.ConstructSpdyConnect(
      nullptr, 0, 1, HttpProxyConnectJob::kH2QuicTunnelPriority,
      HostPortPair("www.example.org", 443)));
  // fetch https://www.example.org/ via HTTP

  const char get[] =
      "GET / HTTP/1.1\r\n"
      "Host: www.example.org\r\n"
      "Connection: keep-alive\r\n\r\n";
  spdy::SpdySerializedFrame wrapped_get(
      spdy_util_.ConstructSpdyDataFrame(1, get, false));
  spdy::SpdySerializedFrame conn_resp(
      spdy_util_.ConstructSpdyGetReply(nullptr, 0, 1));
  const char resp[] = "HTTP/1.1 200 OK\r\n"
      "Content-Length: 10\r\n\r\n";
  spdy::SpdySerializedFrame wrapped_get_resp(
      spdy_util_.ConstructSpdyDataFrame(1, resp, false));
  spdy::SpdySerializedFrame wrapped_body(
      spdy_util_.ConstructSpdyDataFrame(1, "1234567890", false));
  spdy::SpdySerializedFrame window_update(
      spdy_util_.ConstructSpdyWindowUpdate(1, wrapped_get_resp.size()));

  MockWrite spdy_writes[] = {
      CreateMockWrite(connect, 0), CreateMockWrite(wrapped_get, 2),
      CreateMockWrite(window_update, 6),
  };

  MockRead spdy_reads[] = {
      CreateMockRead(conn_resp, 1, ASYNC),
      CreateMockRead(wrapped_get_resp, 3, ASYNC),
      CreateMockRead(wrapped_body, 4, ASYNC),
      CreateMockRead(wrapped_body, 5, ASYNC),
      MockRead(ASYNC, 0, 7),
  };

  SequencedSocketData spdy_data(spdy_reads, spdy_writes);
  session_deps_.socket_factory->AddSocketDataProvider(&spdy_data);

  SSLSocketDataProvider ssl(ASYNC, OK);
  ssl.next_proto = kProtoHTTP2;
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl);
  SSLSocketDataProvider ssl2(ASYNC, OK);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl2);

  TestCompletionCallback callback1;

  int rv = trans.Start(&request, callback1.callback(),
                       NetLogWithSource::Make(NetLogSourceType::NONE));
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback1.WaitForResult();
  ASSERT_THAT(rv, IsOk());

  LoadTimingInfo load_timing_info;
  EXPECT_TRUE(trans.GetLoadTimingInfo(&load_timing_info));
  TestLoadTimingNotReused(load_timing_info, CONNECT_TIMING_HAS_SSL_TIMES);

  const HttpResponseInfo* response = trans.GetResponseInfo();
  ASSERT_TRUE(response);
  ASSERT_TRUE(response->headers);
  EXPECT_EQ("HTTP/1.1 200 OK", response->headers->GetStatusLine());

  std::string response_data;
  ASSERT_THAT(ReadTransaction(&trans, &response_data), IsOk());
  EXPECT_EQ("1234567890", response_data);
}

// Test a SPDY CONNECT through an HTTPS Proxy to a SPDY server.
TEST_F(HttpNetworkTransactionTest, HttpsProxySpdyConnectSpdy) {
  SpdyTestUtil spdy_util_wrapped;

  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("https://www.example.org/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  // Configure against https proxy server "proxy:70".
  session_deps_.proxy_resolution_service =
      ConfiguredProxyResolutionService::CreateFixedForTest(
          "https://proxy:70", TRAFFIC_ANNOTATION_FOR_TESTS);
  session_deps_.net_log = NetLog::Get();
  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));

  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

  // CONNECT to www.example.org:443 via SPDY
  spdy::SpdySerializedFrame connect(spdy_util_.ConstructSpdyConnect(
      nullptr, 0, 1, HttpProxyConnectJob::kH2QuicTunnelPriority,
      HostPortPair("www.example.org", 443)));
  // fetch https://www.example.org/ via SPDY
  const char kMyUrl[] = "https://www.example.org/";
  spdy::SpdySerializedFrame get(
      spdy_util_wrapped.ConstructSpdyGet(kMyUrl, 1, LOWEST));
  spdy::SpdySerializedFrame wrapped_get(
      spdy_util_.ConstructWrappedSpdyFrame(get, 1));
  spdy::SpdySerializedFrame conn_resp(
      spdy_util_.ConstructSpdyGetReply(nullptr, 0, 1));
  spdy::SpdySerializedFrame get_resp(
      spdy_util_wrapped.ConstructSpdyGetReply(nullptr, 0, 1));
  spdy::SpdySerializedFrame wrapped_get_resp(
      spdy_util_.ConstructWrappedSpdyFrame(get_resp, 1));
  spdy::SpdySerializedFrame body(
      spdy_util_wrapped.ConstructSpdyDataFrame(1, true));
  spdy::SpdySerializedFrame wrapped_body(
      spdy_util_.ConstructWrappedSpdyFrame(body, 1));
  spdy::SpdySerializedFrame window_update_get_resp(
      spdy_util_.ConstructSpdyWindowUpdate(1, wrapped_get_resp.size()));
  spdy::SpdySerializedFrame window_update_body(
      spdy_util_.ConstructSpdyWindowUpdate(1, wrapped_body.size()));

  MockWrite spdy_writes[] = {
      CreateMockWrite(connect, 0), CreateMockWrite(wrapped_get, 2),
      CreateMockWrite(window_update_get_resp, 6),
      CreateMockWrite(window_update_body, 7),
  };

  MockRead spdy_reads[] = {
      CreateMockRead(conn_resp, 1, ASYNC),
      MockRead(ASYNC, ERR_IO_PENDING, 3),
      CreateMockRead(wrapped_get_resp, 4, ASYNC),
      CreateMockRead(wrapped_body, 5, ASYNC),
      MockRead(ASYNC, 0, 8),
  };

  SequencedSocketData spdy_data(spdy_reads, spdy_writes);
  session_deps_.socket_factory->AddSocketDataProvider(&spdy_data);

  SSLSocketDataProvider ssl(ASYNC, OK);
  ssl.next_proto = kProtoHTTP2;
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl);
  SSLSocketDataProvider ssl2(ASYNC, OK);
  ssl2.next_proto = kProtoHTTP2;
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl2);

  TestCompletionCallback callback1;

  int rv = trans.Start(&request, callback1.callback(),
                       NetLogWithSource::Make(NetLogSourceType::NONE));
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  // Allow the SpdyProxyClientSocket's write callback to complete.
  base::RunLoop().RunUntilIdle();
  // Now allow the read of the response to complete.
  spdy_data.Resume();
  rv = callback1.WaitForResult();
  EXPECT_THAT(rv, IsOk());

  LoadTimingInfo load_timing_info;
  EXPECT_TRUE(trans.GetLoadTimingInfo(&load_timing_info));
  TestLoadTimingNotReused(load_timing_info, CONNECT_TIMING_HAS_SSL_TIMES);

  const HttpResponseInfo* response = trans.GetResponseInfo();
  ASSERT_TRUE(response);
  ASSERT_TRUE(response->headers);
  EXPECT_EQ("HTTP/1.1 200", response->headers->GetStatusLine());

  std::string response_data;
  ASSERT_THAT(ReadTransaction(&trans, &response_data), IsOk());
  EXPECT_EQ(kUploadData, response_data);
}

// Test a SPDY CONNECT failure through an HTTPS Proxy.
TEST_F(HttpNetworkTransactionTest, HttpsProxySpdyConnectFailure) {
  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("https://www.example.org/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  // Configure against https proxy server "proxy:70".
  session_deps_.proxy_resolution_service =
      ConfiguredProxyResolutionService::CreateFixedForTest(
          "https://proxy:70", TRAFFIC_ANNOTATION_FOR_TESTS);
  session_deps_.net_log = NetLog::Get();
  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));

  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

  // CONNECT to www.example.org:443 via SPDY
  spdy::SpdySerializedFrame connect(spdy_util_.ConstructSpdyConnect(
      nullptr, 0, 1, HttpProxyConnectJob::kH2QuicTunnelPriority,
      HostPortPair("www.example.org", 443)));
  spdy::SpdySerializedFrame get(
      spdy_util_.ConstructSpdyRstStream(1, spdy::ERROR_CODE_CANCEL));

  MockWrite spdy_writes[] = {
      CreateMockWrite(connect, 0), CreateMockWrite(get, 2),
  };

  spdy::SpdySerializedFrame resp(spdy_util_.ConstructSpdyReplyError(1));
  spdy::SpdySerializedFrame data(spdy_util_.ConstructSpdyDataFrame(1, true));
  MockRead spdy_reads[] = {
      CreateMockRead(resp, 1, ASYNC), MockRead(ASYNC, 0, 3),
  };

  SequencedSocketData spdy_data(spdy_reads, spdy_writes);
  session_deps_.socket_factory->AddSocketDataProvider(&spdy_data);

  SSLSocketDataProvider ssl(ASYNC, OK);
  ssl.next_proto = kProtoHTTP2;
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl);
  SSLSocketDataProvider ssl2(ASYNC, OK);
  ssl2.next_proto = kProtoHTTP2;
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl2);

  TestCompletionCallback callback1;

  int rv = trans.Start(&request, callback1.callback(),
                       NetLogWithSource::Make(NetLogSourceType::NONE));
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback1.WaitForResult();
  EXPECT_THAT(rv, IsError(ERR_TUNNEL_CONNECTION_FAILED));

  // TODO(juliatuttle): Anything else to check here?
}

// Test the case where a proxied H2 session doesn't exist when an auth challenge
// is observed, but does exist by the time auth credentials are provided. In
// this case, auth and SSL are fully negotated on the second request, but then
// the socket is discarded to use the shared session.
TEST_F(HttpNetworkTransactionTest, ProxiedH2SessionAppearsDuringAuth) {
  ProxyConfig proxy_config;
  proxy_config.set_auto_detect(true);
  proxy_config.set_pac_url(GURL("http://fooproxyurl"));

  CapturingProxyResolver capturing_proxy_resolver;
  capturing_proxy_resolver.set_proxy_server(
      ProxyServer(ProxyServer::SCHEME_HTTP, HostPortPair("myproxy", 70)));
  session_deps_.proxy_resolution_service =
      std::make_unique<ConfiguredProxyResolutionService>(
          std::make_unique<ProxyConfigServiceFixed>(ProxyConfigWithAnnotation(
              proxy_config, TRAFFIC_ANNOTATION_FOR_TESTS)),
          std::make_unique<CapturingProxyResolverFactory>(
              &capturing_proxy_resolver),
          nullptr, /*quick_check_enabled=*/true);

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));

  const char kMyUrl[] = "https://www.example.org/";
  spdy::SpdySerializedFrame get(spdy_util_.ConstructSpdyGet(kMyUrl, 1, LOWEST));
  spdy::SpdySerializedFrame get_resp(
      spdy_util_.ConstructSpdyGetReply(nullptr, 0, 1));
  spdy::SpdySerializedFrame body(spdy_util_.ConstructSpdyDataFrame(1, true));

  spdy_util_.UpdateWithStreamDestruction(1);
  spdy::SpdySerializedFrame get2(
      spdy_util_.ConstructSpdyGet(kMyUrl, 3, LOWEST));
  spdy::SpdySerializedFrame get_resp2(
      spdy_util_.ConstructSpdyGetReply(nullptr, 0, 3));
  spdy::SpdySerializedFrame body2(spdy_util_.ConstructSpdyDataFrame(3, true));

  MockWrite auth_challenge_writes[] = {
      MockWrite(ASYNC, 0,
                "CONNECT www.example.org:443 HTTP/1.1\r\n"
                "Host: www.example.org:443\r\n"
                "Proxy-Connection: keep-alive\r\n\r\n"),
      MockWrite(ASYNC, 2,
                "CONNECT www.example.org:443 HTTP/1.1\r\n"
                "Host: www.example.org:443\r\n"
                "Proxy-Connection: keep-alive\r\n\r\n"),
  };

  MockRead auth_challenge_reads[] = {
      MockRead(ASYNC, 1,
               "HTTP/1.1 407 Authentication Required\r\n"
               "Content-Length: 0\r\n"
               "Proxy-Connection: close\r\n"
               "Proxy-Authenticate: Basic realm=\"MyRealm1\"\r\n\r\n"),
  };

  MockWrite spdy_writes[] = {
      MockWrite(ASYNC, 0,
                "CONNECT www.example.org:443 HTTP/1.1\r\n"
                "Host: www.example.org:443\r\n"
                "Proxy-Connection: keep-alive\r\n"
                "Proxy-Authorization: Basic Zm9vOmJhcg==\r\n\r\n"),
      CreateMockWrite(get, 2),
      CreateMockWrite(get2, 5),
  };

  MockRead spdy_reads[] = {
      MockRead(ASYNC, 1, "HTTP/1.1 200 OK\r\n\r\n"),
      CreateMockRead(get_resp, 3, ASYNC),
      CreateMockRead(body, 4, ASYNC),
      CreateMockRead(get_resp2, 6, ASYNC),
      CreateMockRead(body2, 7, ASYNC),

      MockRead(SYNCHRONOUS, ERR_IO_PENDING, 8),
  };

  MockWrite auth_response_writes_discarded_socket[] = {
      MockWrite(ASYNC, 0,
                "CONNECT www.example.org:443 HTTP/1.1\r\n"
                "Host: www.example.org:443\r\n"
                "Proxy-Connection: keep-alive\r\n"
                "Proxy-Authorization: Basic Zm9vOmJhcg==\r\n\r\n"),
  };

  MockRead auth_response_reads_discarded_socket[] = {
      MockRead(ASYNC, 1, "HTTP/1.1 200 OK\r\n\r\n"),
  };

  SequencedSocketData auth_challenge1(auth_challenge_reads,
                                      auth_challenge_writes);
  session_deps_.socket_factory->AddSocketDataProvider(&auth_challenge1);

  SequencedSocketData auth_challenge2(auth_challenge_reads,
                                      auth_challenge_writes);
  session_deps_.socket_factory->AddSocketDataProvider(&auth_challenge2);

  SequencedSocketData spdy_data(spdy_reads, spdy_writes);
  session_deps_.socket_factory->AddSocketDataProvider(&spdy_data);

  SequencedSocketData auth_response_discarded_socket(
      auth_response_reads_discarded_socket,
      auth_response_writes_discarded_socket);
  session_deps_.socket_factory->AddSocketDataProvider(
      &auth_response_discarded_socket);

  SSLSocketDataProvider ssl(ASYNC, OK);
  ssl.next_proto = kProtoHTTP2;
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl);

  SSLSocketDataProvider ssl2(ASYNC, OK);
  ssl2.next_proto = kProtoHTTP2;
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl2);

  TestCompletionCallback callback;
  std::string response_data;

  // Run first request until an auth challenge is observed.
  HttpRequestInfo request1;
  request1.method = "GET";
  request1.url = GURL(kMyUrl);
  request1.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);
  HttpNetworkTransaction trans1(LOWEST, session.get());
  int rv = trans1.Start(&request1, callback.callback(), NetLogWithSource());
  EXPECT_THAT(callback.GetResult(rv), IsOk());
  const HttpResponseInfo* response = trans1.GetResponseInfo();
  ASSERT_TRUE(response);
  ASSERT_TRUE(response->headers);
  EXPECT_EQ(407, response->headers->response_code());
  EXPECT_TRUE(HttpVersion(1, 1) == response->headers->GetHttpVersion());
  EXPECT_TRUE(CheckBasicProxyAuth(response->auth_challenge));

  // Run second request until an auth challenge is observed.
  HttpRequestInfo request2;
  request2.method = "GET";
  request2.url = GURL(kMyUrl);
  request2.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);
  HttpNetworkTransaction trans2(LOWEST, session.get());
  rv = trans2.Start(&request2, callback.callback(), NetLogWithSource());
  EXPECT_THAT(callback.GetResult(rv), IsOk());
  response = trans2.GetResponseInfo();
  ASSERT_TRUE(response);
  ASSERT_TRUE(response->headers);
  EXPECT_EQ(407, response->headers->response_code());
  EXPECT_TRUE(HttpVersion(1, 1) == response->headers->GetHttpVersion());
  EXPECT_TRUE(CheckBasicProxyAuth(response->auth_challenge));

  // Now provide credentials for the first request, and wait for it to complete.
  rv = trans1.RestartWithAuth(AuthCredentials(kFoo, kBar), callback.callback());
  rv = callback.GetResult(rv);
  EXPECT_THAT(rv, IsOk());
  response = trans1.GetResponseInfo();
  ASSERT_TRUE(response);
  ASSERT_TRUE(response->headers);
  EXPECT_EQ("HTTP/1.1 200", response->headers->GetStatusLine());
  ASSERT_THAT(ReadTransaction(&trans1, &response_data), IsOk());
  EXPECT_EQ(kUploadData, response_data);

  // Now provide credentials for the second request. It should notice the
  // existing session, and reuse it.
  rv = trans2.RestartWithAuth(AuthCredentials(kFoo, kBar), callback.callback());
  EXPECT_THAT(callback.GetResult(rv), IsOk());
  response = trans2.GetResponseInfo();
  ASSERT_TRUE(response);
  ASSERT_TRUE(response->headers);
  EXPECT_EQ("HTTP/1.1 200", response->headers->GetStatusLine());
  ASSERT_THAT(ReadTransaction(&trans2, &response_data), IsOk());
  EXPECT_EQ(kUploadData, response_data);
}

// Test load timing in the case of two HTTPS (non-SPDY) requests through a SPDY
// HTTPS Proxy to different servers.
TEST_F(HttpNetworkTransactionTest,
       HttpsProxySpdyConnectHttpsLoadTimingTwoRequestsTwoServers) {
  // Configure against https proxy server "proxy:70".
  session_deps_.proxy_resolution_service =
      ConfiguredProxyResolutionService::CreateFixedForTest(
          "https://proxy:70", TRAFFIC_ANNOTATION_FOR_TESTS);
  session_deps_.net_log = NetLog::Get();
  std::unique_ptr<HttpNetworkSession> session(
      SpdySessionDependencies::SpdyCreateSession(&session_deps_));

  HttpRequestInfo request1;
  request1.method = "GET";
  request1.url = GURL("https://www.example.org/");
  request1.load_flags = 0;
  request1.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  HttpRequestInfo request2;
  request2.method = "GET";
  request2.url = GURL("https://mail.example.org/");
  request2.load_flags = 0;
  request2.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  // CONNECT to www.example.org:443 via SPDY.
  spdy::SpdySerializedFrame connect1(spdy_util_.ConstructSpdyConnect(
      nullptr, 0, 1, HttpProxyConnectJob::kH2QuicTunnelPriority,
      HostPortPair("www.example.org", 443)));
  spdy::SpdySerializedFrame conn_resp1(
      spdy_util_.ConstructSpdyGetReply(nullptr, 0, 1));

  // Fetch https://www.example.org/ via HTTP.
  const char get1[] =
      "GET / HTTP/1.1\r\n"
      "Host: www.example.org\r\n"
      "Connection: keep-alive\r\n\r\n";
  spdy::SpdySerializedFrame wrapped_get1(
      spdy_util_.ConstructSpdyDataFrame(1, get1, false));
  const char resp1[] = "HTTP/1.1 200 OK\r\n"
      "Content-Length: 1\r\n\r\n";
  spdy::SpdySerializedFrame wrapped_get_resp1(
      spdy_util_.ConstructSpdyDataFrame(1, resp1, false));
  spdy::SpdySerializedFrame wrapped_body1(
      spdy_util_.ConstructSpdyDataFrame(1, "1", false));
  spdy::SpdySerializedFrame window_update(
      spdy_util_.ConstructSpdyWindowUpdate(1, wrapped_get_resp1.size()));

  // CONNECT to mail.example.org:443 via SPDY.
  spdy::Http2HeaderBlock connect2_block;
  connect2_block[spdy::kHttp2MethodHeader] = "CONNECT";
  connect2_block[spdy::kHttp2AuthorityHeader] = "mail.example.org:443";
  spdy::SpdySerializedFrame connect2(spdy_util_.ConstructSpdyHeaders(
      3, std::move(connect2_block), HttpProxyConnectJob::kH2QuicTunnelPriority,
      false));

  spdy::SpdySerializedFrame conn_resp2(
      spdy_util_.ConstructSpdyGetReply(nullptr, 0, 3));

  // Fetch https://mail.example.org/ via HTTP.
  const char get2[] =
      "GET / HTTP/1.1\r\n"
      "Host: mail.example.org\r\n"
      "Connection: keep-alive\r\n\r\n";
  spdy::SpdySerializedFrame wrapped_get2(
      spdy_util_.ConstructSpdyDataFrame(3, get2, false));
  const char resp2[] = "HTTP/1.1 200 OK\r\n"
      "Content-Length: 2\r\n\r\n";
  spdy::SpdySerializedFrame wrapped_get_resp2(
      spdy_util_.ConstructSpdyDataFrame(3, resp2, false));
  spdy::SpdySerializedFrame wrapped_body2(
      spdy_util_.ConstructSpdyDataFrame(3, "22", false));

  MockWrite spdy_writes[] = {
      CreateMockWrite(connect1, 0), CreateMockWrite(wrapped_get1, 2),
      CreateMockWrite(connect2, 5), CreateMockWrite(wrapped_get2, 7),
  };

  MockRead spdy_reads[] = {
      CreateMockRead(conn_resp1, 1, ASYNC),
      CreateMockRead(wrapped_get_resp1, 3, ASYNC),
      CreateMockRead(wrapped_body1, 4, ASYNC),
      CreateMockRead(conn_resp2, 6, ASYNC),
      CreateMockRead(wrapped_get_resp2, 8, ASYNC),
      CreateMockRead(wrapped_body2, 9, ASYNC),
      MockRead(ASYNC, 0, 10),
  };

  SequencedSocketData spdy_data(spdy_reads, spdy_writes);
  session_deps_.socket_factory->AddSocketDataProvider(&spdy_data);

  SSLSocketDataProvider ssl(ASYNC, OK);
  ssl.next_proto = kProtoHTTP2;
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl);
  SSLSocketDataProvider ssl2(ASYNC, OK);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl2);
  SSLSocketDataProvider ssl3(ASYNC, OK);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl3);

  TestCompletionCallback callback;

  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());
  int rv = trans.Start(&request1, callback.callback(), NetLogWithSource());
  EXPECT_THAT(callback.GetResult(rv), IsOk());

  LoadTimingInfo load_timing_info;
  EXPECT_TRUE(trans.GetLoadTimingInfo(&load_timing_info));
  TestLoadTimingNotReused(load_timing_info, CONNECT_TIMING_HAS_SSL_TIMES);

  const HttpResponseInfo* response = trans.GetResponseInfo();
  ASSERT_TRUE(response);
  ASSERT_TRUE(response->headers);
  EXPECT_EQ("HTTP/1.1 200 OK", response->headers->GetStatusLine());

  std::string response_data;
  scoped_refptr<IOBuffer> buf = base::MakeRefCounted<IOBuffer>(256);
  rv = trans.Read(buf.get(), 256, callback.callback());
  EXPECT_EQ(1, callback.GetResult(rv));

  HttpNetworkTransaction trans2(DEFAULT_PRIORITY, session.get());
  rv = trans2.Start(&request2, callback.callback(), NetLogWithSource());
  EXPECT_THAT(callback.GetResult(rv), IsOk());

  LoadTimingInfo load_timing_info2;
  EXPECT_TRUE(trans2.GetLoadTimingInfo(&load_timing_info2));
  // Even though the SPDY connection is reused, a new tunnelled connection has
  // to be created, so the socket's load timing looks like a fresh connection.
  TestLoadTimingNotReused(load_timing_info2, CONNECT_TIMING_HAS_SSL_TIMES);

  // The requests should have different IDs, since they each are using their own
  // separate stream.
  EXPECT_NE(load_timing_info.socket_log_id, load_timing_info2.socket_log_id);

  rv = trans2.Read(buf.get(), 256, callback.callback());
  EXPECT_EQ(2, callback.GetResult(rv));
}

// Test load timing in the case of two HTTPS (non-SPDY) requests through a SPDY
// HTTPS Proxy to the same server.
TEST_F(HttpNetworkTransactionTest,
       HttpsProxySpdyConnectHttpsLoadTimingTwoRequestsSameServer) {
  // Configure against https proxy server "proxy:70".
  session_deps_.proxy_resolution_service =
      ConfiguredProxyResolutionService::CreateFixedForTest(
          "https://proxy:70", TRAFFIC_ANNOTATION_FOR_TESTS);
  session_deps_.net_log = NetLog::Get();
  std::unique_ptr<HttpNetworkSession> session(
      SpdySessionDependencies::SpdyCreateSession(&session_deps_));

  HttpRequestInfo request1;
  request1.method = "GET";
  request1.url = GURL("https://www.example.org/");
  request1.load_flags = 0;
  request1.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  HttpRequestInfo request2;
  request2.method = "GET";
  request2.url = GURL("https://www.example.org/2");
  request2.load_flags = 0;
  request2.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  // CONNECT to www.example.org:443 via SPDY.
  spdy::SpdySerializedFrame connect1(spdy_util_.ConstructSpdyConnect(
      nullptr, 0, 1, HttpProxyConnectJob::kH2QuicTunnelPriority,
      HostPortPair("www.example.org", 443)));
  spdy::SpdySerializedFrame conn_resp1(
      spdy_util_.ConstructSpdyGetReply(nullptr, 0, 1));

  // Fetch https://www.example.org/ via HTTP.
  const char get1[] =
      "GET / HTTP/1.1\r\n"
      "Host: www.example.org\r\n"
      "Connection: keep-alive\r\n\r\n";
  spdy::SpdySerializedFrame wrapped_get1(
      spdy_util_.ConstructSpdyDataFrame(1, get1, false));
  const char resp1[] = "HTTP/1.1 200 OK\r\n"
      "Content-Length: 1\r\n\r\n";
  spdy::SpdySerializedFrame wrapped_get_resp1(
      spdy_util_.ConstructSpdyDataFrame(1, resp1, false));
  spdy::SpdySerializedFrame wrapped_body1(
      spdy_util_.ConstructSpdyDataFrame(1, "1", false));
  spdy::SpdySerializedFrame window_update(
      spdy_util_.ConstructSpdyWindowUpdate(1, wrapped_get_resp1.size()));

  // Fetch https://www.example.org/2 via HTTP.
  const char get2[] =
      "GET /2 HTTP/1.1\r\n"
      "Host: www.example.org\r\n"
      "Connection: keep-alive\r\n\r\n";
  spdy::SpdySerializedFrame wrapped_get2(
      spdy_util_.ConstructSpdyDataFrame(1, get2, false));
  const char resp2[] = "HTTP/1.1 200 OK\r\n"
      "Content-Length: 2\r\n\r\n";
  spdy::SpdySerializedFrame wrapped_get_resp2(
      spdy_util_.ConstructSpdyDataFrame(1, resp2, false));
  spdy::SpdySerializedFrame wrapped_body2(
      spdy_util_.ConstructSpdyDataFrame(1, "22", false));

  MockWrite spdy_writes[] = {
      CreateMockWrite(connect1, 0), CreateMockWrite(wrapped_get1, 2),
      CreateMockWrite(wrapped_get2, 5),
  };

  MockRead spdy_reads[] = {
      CreateMockRead(conn_resp1, 1, ASYNC),
      CreateMockRead(wrapped_get_resp1, 3, ASYNC),
      CreateMockRead(wrapped_body1, 4, SYNCHRONOUS),
      CreateMockRead(wrapped_get_resp2, 6, ASYNC),
      CreateMockRead(wrapped_body2, 7, SYNCHRONOUS),
      MockRead(ASYNC, 0, 8),
  };

  SequencedSocketData spdy_data(spdy_reads, spdy_writes);
  session_deps_.socket_factory->AddSocketDataProvider(&spdy_data);

  SSLSocketDataProvider ssl(ASYNC, OK);
  ssl.next_proto = kProtoHTTP2;
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl);
  SSLSocketDataProvider ssl2(ASYNC, OK);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl2);

  TestCompletionCallback callback;

  auto trans =
      std::make_unique<HttpNetworkTransaction>(DEFAULT_PRIORITY, session.get());
  int rv = trans->Start(&request1, callback.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback.WaitForResult();
  EXPECT_THAT(rv, IsOk());

  LoadTimingInfo load_timing_info;
  EXPECT_TRUE(trans->GetLoadTimingInfo(&load_timing_info));
  TestLoadTimingNotReused(load_timing_info, CONNECT_TIMING_HAS_SSL_TIMES);

  const HttpResponseInfo* response = trans->GetResponseInfo();
  ASSERT_TRUE(response);
  ASSERT_TRUE(response->headers);
  EXPECT_EQ("HTTP/1.1 200 OK", response->headers->GetStatusLine());

  std::string response_data;
  scoped_refptr<IOBuffer> buf = base::MakeRefCounted<IOBuffer>(256);
  EXPECT_EQ(1, trans->Read(buf.get(), 256, callback.callback()));
  trans.reset();

  auto trans2 =
      std::make_unique<HttpNetworkTransaction>(DEFAULT_PRIORITY, session.get());
  rv = trans2->Start(&request2, callback.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback.WaitForResult();
  EXPECT_THAT(rv, IsOk());

  LoadTimingInfo load_timing_info2;
  EXPECT_TRUE(trans2->GetLoadTimingInfo(&load_timing_info2));
  TestLoadTimingReused(load_timing_info2);

  // The requests should have the same ID.
  EXPECT_EQ(load_timing_info.socket_log_id, load_timing_info2.socket_log_id);

  EXPECT_EQ(2, trans2->Read(buf.get(), 256, callback.callback()));
}

// Test load timing in the case of of two HTTP requests through a SPDY HTTPS
// Proxy to different servers.
TEST_F(HttpNetworkTransactionTest, HttpsProxySpdyLoadTimingTwoHttpRequests) {
  // Configure against https proxy server "proxy:70".
  session_deps_.proxy_resolution_service =
      ConfiguredProxyResolutionService::CreateFixedForTest(
          "https://proxy:70", TRAFFIC_ANNOTATION_FOR_TESTS);
  session_deps_.net_log = NetLog::Get();
  std::unique_ptr<HttpNetworkSession> session(
      SpdySessionDependencies::SpdyCreateSession(&session_deps_));

  HttpRequestInfo request1;
  request1.method = "GET";
  request1.url = GURL("http://www.example.org/");
  request1.load_flags = 0;
  request1.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  HttpRequestInfo request2;
  request2.method = "GET";
  request2.url = GURL("http://mail.example.org/");
  request2.load_flags = 0;
  request2.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  // http://www.example.org/
  spdy::Http2HeaderBlock headers(
      spdy_util_.ConstructGetHeaderBlockForProxy("http://www.example.org/"));
  spdy::SpdySerializedFrame get1(
      spdy_util_.ConstructSpdyHeaders(1, std::move(headers), LOWEST, true));
  spdy::SpdySerializedFrame get_resp1(
      spdy_util_.ConstructSpdyGetReply(nullptr, 0, 1));
  spdy::SpdySerializedFrame body1(
      spdy_util_.ConstructSpdyDataFrame(1, "1", true));
  spdy_util_.UpdateWithStreamDestruction(1);

  // http://mail.example.org/
  spdy::Http2HeaderBlock headers2(
      spdy_util_.ConstructGetHeaderBlockForProxy("http://mail.example.org/"));
  spdy::SpdySerializedFrame get2(
      spdy_util_.ConstructSpdyHeaders(3, std::move(headers2), LOWEST, true));
  spdy::SpdySerializedFrame get_resp2(
      spdy_util_.ConstructSpdyGetReply(nullptr, 0, 3));
  spdy::SpdySerializedFrame body2(
      spdy_util_.ConstructSpdyDataFrame(3, "22", true));

  MockWrite spdy_writes[] = {
      CreateMockWrite(get1, 0), CreateMockWrite(get2, 3),
  };

  MockRead spdy_reads[] = {
      CreateMockRead(get_resp1, 1, ASYNC),
      CreateMockRead(body1, 2, ASYNC),
      CreateMockRead(get_resp2, 4, ASYNC),
      CreateMockRead(body2, 5, ASYNC),
      MockRead(ASYNC, 0, 6),
  };

  SequencedSocketData spdy_data(spdy_reads, spdy_writes);
  session_deps_.socket_factory->AddSocketDataProvider(&spdy_data);

  SSLSocketDataProvider ssl(ASYNC, OK);
  ssl.next_proto = kProtoHTTP2;
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl);

  TestCompletionCallback callback;

  auto trans =
      std::make_unique<HttpNetworkTransaction>(DEFAULT_PRIORITY, session.get());
  int rv = trans->Start(&request1, callback.callback(), NetLogWithSource());
  EXPECT_THAT(callback.GetResult(rv), IsOk());

  LoadTimingInfo load_timing_info;
  EXPECT_TRUE(trans->GetLoadTimingInfo(&load_timing_info));
  TestLoadTimingNotReused(load_timing_info,
                          CONNECT_TIMING_HAS_CONNECT_TIMES_ONLY);

  const HttpResponseInfo* response = trans->GetResponseInfo();
  ASSERT_TRUE(response);
  ASSERT_TRUE(response->headers);
  EXPECT_EQ("HTTP/1.1 200", response->headers->GetStatusLine());

  std::string response_data;
  scoped_refptr<IOBuffer> buf = base::MakeRefCounted<IOBuffer>(256);
  rv = trans->Read(buf.get(), 256, callback.callback());
  EXPECT_EQ(1, callback.GetResult(rv));
  // Delete the first request, so the second one can reuse the socket.
  trans.reset();

  HttpNetworkTransaction trans2(DEFAULT_PRIORITY, session.get());
  rv = trans2.Start(&request2, callback.callback(), NetLogWithSource());
  EXPECT_THAT(callback.GetResult(rv), IsOk());

  LoadTimingInfo load_timing_info2;
  EXPECT_TRUE(trans2.GetLoadTimingInfo(&load_timing_info2));
  TestLoadTimingReused(load_timing_info2);

  // The requests should have the same ID.
  EXPECT_EQ(load_timing_info.socket_log_id, load_timing_info2.socket_log_id);

  rv = trans2.Read(buf.get(), 256, callback.callback());
  EXPECT_EQ(2, callback.GetResult(rv));
}

// Test that an HTTP/2 CONNECT through an HTTPS Proxy to a HTTP/2 server and a
// direct (non-proxied) request to the proxy server are not pooled, as that
// would break socket pool isolation.
TEST_F(HttpNetworkTransactionTest, SpdyProxyIsolation1) {
  ProxyConfig proxy_config;
  proxy_config.set_auto_detect(true);
  proxy_config.set_pac_url(GURL("http://fooproxyurl"));

  CapturingProxyResolver capturing_proxy_resolver;
  session_deps_.proxy_resolution_service =
      std::make_unique<ConfiguredProxyResolutionService>(
          std::make_unique<ProxyConfigServiceFixed>(ProxyConfigWithAnnotation(
              proxy_config, TRAFFIC_ANNOTATION_FOR_TESTS)),
          std::make_unique<CapturingProxyResolverFactory>(
              &capturing_proxy_resolver),
          nullptr, /*quick_check_enabled=*/true);

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));

  SpdyTestUtil spdy_util1;
  // CONNECT to www.example.org:443 via HTTP/2.
  spdy::SpdySerializedFrame connect(spdy_util_.ConstructSpdyConnect(
      nullptr, 0, 1, HttpProxyConnectJob::kH2QuicTunnelPriority,
      HostPortPair("www.example.org", 443)));
  // fetch https://www.example.org/ via HTTP/2.
  const char kMyUrl[] = "https://www.example.org/";
  spdy::SpdySerializedFrame get(spdy_util1.ConstructSpdyGet(kMyUrl, 1, LOWEST));
  spdy::SpdySerializedFrame wrapped_get(
      spdy_util_.ConstructWrappedSpdyFrame(get, 1));
  spdy::SpdySerializedFrame conn_resp(
      spdy_util_.ConstructSpdyGetReply(nullptr, 0, 1));
  spdy::SpdySerializedFrame get_resp(
      spdy_util1.ConstructSpdyGetReply(nullptr, 0, 1));
  spdy::SpdySerializedFrame wrapped_get_resp(
      spdy_util_.ConstructWrappedSpdyFrame(get_resp, 1));
  spdy::SpdySerializedFrame body(spdy_util1.ConstructSpdyDataFrame(1, true));
  spdy::SpdySerializedFrame wrapped_body(
      spdy_util_.ConstructWrappedSpdyFrame(body, 1));
  spdy::SpdySerializedFrame window_update_get_resp(
      spdy_util_.ConstructSpdyWindowUpdate(1, wrapped_get_resp.size()));
  spdy::SpdySerializedFrame window_update_body(
      spdy_util_.ConstructSpdyWindowUpdate(1, wrapped_body.size()));

  MockWrite spdy_writes1[] = {
      CreateMockWrite(connect, 0),
      CreateMockWrite(wrapped_get, 2),
      CreateMockWrite(window_update_get_resp, 6),
      CreateMockWrite(window_update_body, 7),
  };

  MockRead spdy_reads1[] = {
      CreateMockRead(conn_resp, 1, ASYNC),
      MockRead(ASYNC, ERR_IO_PENDING, 3),
      CreateMockRead(wrapped_get_resp, 4, ASYNC),
      CreateMockRead(wrapped_body, 5, ASYNC),
      MockRead(ASYNC, 0, 8),
  };

  SequencedSocketData spdy_data1(spdy_reads1, spdy_writes1);
  session_deps_.socket_factory->AddSocketDataProvider(&spdy_data1);

  // Fetch https://proxy:70/ via HTTP/2. Needs a new SpdyTestUtil, since it uses
  // a new pipe.
  SpdyTestUtil spdy_util2;
  spdy::SpdySerializedFrame req(
      spdy_util2.ConstructSpdyGet("https://proxy:70/", 1, LOWEST));
  MockWrite spdy_writes2[] = {CreateMockWrite(req, 0)};

  spdy::SpdySerializedFrame resp(
      spdy_util2.ConstructSpdyGetReply(nullptr, 0, 1));
  spdy::SpdySerializedFrame data(spdy_util2.ConstructSpdyDataFrame(1, true));
  MockRead spdy_reads2[] = {
      CreateMockRead(resp, 1),
      CreateMockRead(data, 2),
      MockRead(ASYNC, 0, 3),
  };
  SequencedSocketData spdy_data2(spdy_reads2, spdy_writes2);
  session_deps_.socket_factory->AddSocketDataProvider(&spdy_data2);

  SSLSocketDataProvider ssl(ASYNC, OK);
  ssl.next_proto = kProtoHTTP2;
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl);
  SSLSocketDataProvider ssl2(ASYNC, OK);
  ssl2.next_proto = kProtoHTTP2;
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl2);
  SSLSocketDataProvider ssl3(ASYNC, OK);
  ssl3.next_proto = kProtoHTTP2;
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl3);

  TestCompletionCallback callback;
  std::string response_data;

  // Make a request using proxy:70 as a HTTP/2 proxy.
  capturing_proxy_resolver.set_proxy_server(
      ProxyServer(ProxyServer::SCHEME_HTTPS, HostPortPair("proxy", 70)));
  HttpRequestInfo request1;
  request1.method = "GET";
  request1.url = GURL("https://www.example.org/");
  request1.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  HttpNetworkTransaction trans1(LOWEST, session.get());
  int rv = trans1.Start(&request1, callback.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  // Allow the SpdyProxyClientSocket's write callback to complete.
  base::RunLoop().RunUntilIdle();
  // Now allow the read of the response to complete.
  spdy_data1.Resume();
  rv = callback.WaitForResult();
  EXPECT_THAT(rv, IsOk());

  const HttpResponseInfo* response = trans1.GetResponseInfo();
  ASSERT_TRUE(response);
  ASSERT_TRUE(response->headers);
  EXPECT_EQ("HTTP/1.1 200", response->headers->GetStatusLine());

  ASSERT_THAT(ReadTransaction(&trans1, &response_data), IsOk());
  EXPECT_EQ(kUploadData, response_data);
  RunUntilIdle();

  // Make a direct HTTP/2 request to proxy:70.
  capturing_proxy_resolver.set_proxy_server(ProxyServer::Direct());
  HttpRequestInfo request2;
  request2.method = "GET";
  request2.url = GURL("https://proxy:70/");
  request2.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);
  HttpNetworkTransaction trans2(LOWEST, session.get());
  EXPECT_THAT(callback.GetResult(trans2.Start(&request2, callback.callback(),
                                              NetLogWithSource())),
              IsOk());
  ASSERT_THAT(ReadTransaction(&trans2, &response_data), IsOk());
}

// Same as above, but reverse request order, since the code to check for an
// existing session is different for tunnels and direct connections.
TEST_F(HttpNetworkTransactionTest, SpdyProxyIsolation2) {
  // Configure against https proxy server "myproxy:80".
  ProxyConfig proxy_config;
  proxy_config.set_auto_detect(true);
  proxy_config.set_pac_url(GURL("http://fooproxyurl"));

  CapturingProxyResolver capturing_proxy_resolver;
  session_deps_.proxy_resolution_service =
      std::make_unique<ConfiguredProxyResolutionService>(
          std::make_unique<ProxyConfigServiceFixed>(ProxyConfigWithAnnotation(
              proxy_config, TRAFFIC_ANNOTATION_FOR_TESTS)),
          std::make_unique<CapturingProxyResolverFactory>(
              &capturing_proxy_resolver),
          nullptr, /*quick_check_enabled=*/true);

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));
  // Fetch https://proxy:70/ via HTTP/2.
  SpdyTestUtil spdy_util1;
  spdy::SpdySerializedFrame req(
      spdy_util1.ConstructSpdyGet("https://proxy:70/", 1, LOWEST));
  MockWrite spdy_writes1[] = {CreateMockWrite(req, 0)};

  spdy::SpdySerializedFrame resp(
      spdy_util1.ConstructSpdyGetReply(nullptr, 0, 1));
  spdy::SpdySerializedFrame data(spdy_util1.ConstructSpdyDataFrame(1, true));
  MockRead spdy_reads1[] = {
      CreateMockRead(resp, 1),
      CreateMockRead(data, 2),
      MockRead(ASYNC, 0, 3),
  };
  SequencedSocketData spdy_data1(spdy_reads1, spdy_writes1);
  session_deps_.socket_factory->AddSocketDataProvider(&spdy_data1);

  SpdyTestUtil spdy_util2;
  // CONNECT to www.example.org:443 via HTTP/2.
  spdy::SpdySerializedFrame connect(spdy_util_.ConstructSpdyConnect(
      nullptr, 0, 1, HttpProxyConnectJob::kH2QuicTunnelPriority,
      HostPortPair("www.example.org", 443)));
  // fetch https://www.example.org/ via HTTP/2.
  const char kMyUrl[] = "https://www.example.org/";
  spdy::SpdySerializedFrame get(spdy_util2.ConstructSpdyGet(kMyUrl, 1, LOWEST));
  spdy::SpdySerializedFrame wrapped_get(
      spdy_util_.ConstructWrappedSpdyFrame(get, 1));
  spdy::SpdySerializedFrame conn_resp(
      spdy_util_.ConstructSpdyGetReply(nullptr, 0, 1));
  spdy::SpdySerializedFrame get_resp(
      spdy_util2.ConstructSpdyGetReply(nullptr, 0, 1));
  spdy::SpdySerializedFrame wrapped_get_resp(
      spdy_util_.ConstructWrappedSpdyFrame(get_resp, 1));
  spdy::SpdySerializedFrame body(spdy_util2.ConstructSpdyDataFrame(1, true));
  spdy::SpdySerializedFrame wrapped_body(
      spdy_util_.ConstructWrappedSpdyFrame(body, 1));
  spdy::SpdySerializedFrame window_update_get_resp(
      spdy_util_.ConstructSpdyWindowUpdate(1, wrapped_get_resp.size()));
  spdy::SpdySerializedFrame window_update_body(
      spdy_util_.ConstructSpdyWindowUpdate(1, wrapped_body.size()));

  MockWrite spdy_writes2[] = {
      CreateMockWrite(connect, 0),
      CreateMockWrite(wrapped_get, 2),
      CreateMockWrite(window_update_get_resp, 6),
      CreateMockWrite(window_update_body, 7),
  };

  MockRead spdy_reads2[] = {
      CreateMockRead(conn_resp, 1, ASYNC),
      MockRead(ASYNC, ERR_IO_PENDING, 3),
      CreateMockRead(wrapped_get_resp, 4, ASYNC),
      CreateMockRead(wrapped_body, 5, ASYNC),
      MockRead(ASYNC, 0, 8),
  };

  SequencedSocketData spdy_data2(spdy_reads2, spdy_writes2);
  session_deps_.socket_factory->AddSocketDataProvider(&spdy_data2);

  SSLSocketDataProvider ssl(ASYNC, OK);
  ssl.next_proto = kProtoHTTP2;
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl);
  SSLSocketDataProvider ssl2(ASYNC, OK);
  ssl2.next_proto = kProtoHTTP2;
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl2);
  SSLSocketDataProvider ssl3(ASYNC, OK);
  ssl3.next_proto = kProtoHTTP2;
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl3);

  TestCompletionCallback callback;
  std::string response_data;

  // Make a direct HTTP/2 request to proxy:70.
  capturing_proxy_resolver.set_proxy_server(ProxyServer::Direct());
  HttpRequestInfo request1;
  request1.method = "GET";
  request1.url = GURL("https://proxy:70/");
  request1.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);
  HttpNetworkTransaction trans1(LOWEST, session.get());
  EXPECT_THAT(callback.GetResult(trans1.Start(&request1, callback.callback(),
                                              NetLogWithSource())),
              IsOk());
  ASSERT_THAT(ReadTransaction(&trans1, &response_data), IsOk());
  RunUntilIdle();

  // Make a request using proxy:70 as a HTTP/2 proxy.
  capturing_proxy_resolver.set_proxy_server(
      ProxyServer(ProxyServer::SCHEME_HTTPS, HostPortPair("proxy", 70)));
  HttpRequestInfo request2;
  request2.method = "GET";
  request2.url = GURL("https://www.example.org/");
  request2.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  HttpNetworkTransaction trans2(LOWEST, session.get());
  int rv = trans2.Start(&request2, callback.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  // Allow the SpdyProxyClientSocket's write callback to complete.
  base::RunLoop().RunUntilIdle();
  // Now allow the read of the response to complete.
  spdy_data2.Resume();
  rv = callback.WaitForResult();
  EXPECT_THAT(rv, IsOk());

  const HttpResponseInfo* response2 = trans2.GetResponseInfo();
  ASSERT_TRUE(response2);
  ASSERT_TRUE(response2->headers);
  EXPECT_EQ("HTTP/1.1 200", response2->headers->GetStatusLine());

  ASSERT_THAT(ReadTransaction(&trans2, &response_data), IsOk());
  EXPECT_EQ(kUploadData, response_data);
}

// Test the challenge-response-retry sequence through an HTTPS Proxy
TEST_F(HttpNetworkTransactionTest, HttpsProxyAuthRetry) {
  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("http://www.example.org/");
  // when the no authentication data flag is set.
  request.privacy_mode = PRIVACY_MODE_ENABLED;
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  // Configure against https proxy server "myproxy:70".
  session_deps_.proxy_resolution_service =
      ConfiguredProxyResolutionService::CreateFixedForTest(
          "https://myproxy:70", TRAFFIC_ANNOTATION_FOR_TESTS);
  session_deps_.net_log = NetLog::Get();
  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));

  // Since we have proxy, should use full url
  MockWrite data_writes1[] = {
      MockWrite("GET http://www.example.org/ HTTP/1.1\r\n"
                "Host: www.example.org\r\n"
                "Proxy-Connection: keep-alive\r\n\r\n"),

      // After calling trans.RestartWithAuth(), this is the request we should
      // be issuing -- the final header line contains the credentials.
      MockWrite("GET http://www.example.org/ HTTP/1.1\r\n"
                "Host: www.example.org\r\n"
                "Proxy-Connection: keep-alive\r\n"
                "Proxy-Authorization: Basic Zm9vOmJhcg==\r\n\r\n"),
  };

  // The proxy responds to the GET with a 407, using a persistent
  // connection.
  MockRead data_reads1[] = {
    // No credentials.
    MockRead("HTTP/1.1 407 Proxy Authentication Required\r\n"),
    MockRead("Proxy-Authenticate: Basic realm=\"MyRealm1\"\r\n"),
    MockRead("Proxy-Connection: keep-alive\r\n"),
    MockRead("Content-Length: 0\r\n\r\n"),

    MockRead("HTTP/1.1 200 OK\r\n"),
    MockRead("Content-Type: text/html; charset=iso-8859-1\r\n"),
    MockRead("Content-Length: 100\r\n\r\n"),
    MockRead(SYNCHRONOUS, OK),
  };

  StaticSocketDataProvider data1(data_reads1, data_writes1);
  session_deps_.socket_factory->AddSocketDataProvider(&data1);
  SSLSocketDataProvider ssl(ASYNC, OK);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl);

  TestCompletionCallback callback1;

  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

  int rv = trans.Start(&request, callback1.callback(),
                       NetLogWithSource::Make(NetLogSourceType::NONE));
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback1.WaitForResult();
  EXPECT_THAT(rv, IsOk());

  LoadTimingInfo load_timing_info;
  EXPECT_TRUE(trans.GetLoadTimingInfo(&load_timing_info));
  TestLoadTimingNotReused(load_timing_info,
                          CONNECT_TIMING_HAS_CONNECT_TIMES_ONLY);

  const HttpResponseInfo* response = trans.GetResponseInfo();
  ASSERT_TRUE(response);
  ASSERT_TRUE(response->headers);
  EXPECT_EQ(407, response->headers->response_code());
  EXPECT_TRUE(HttpVersion(1, 1) == response->headers->GetHttpVersion());
  EXPECT_TRUE(CheckBasicSecureProxyAuth(response->auth_challenge));
  EXPECT_FALSE(response->did_use_http_auth);
  EXPECT_EQ(PacResultElementToProxyServer("HTTPS myproxy:70"),
            response->proxy_server);

  TestCompletionCallback callback2;

  rv = trans.RestartWithAuth(AuthCredentials(kFoo, kBar), callback2.callback());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback2.WaitForResult();
  EXPECT_THAT(rv, IsOk());

  load_timing_info = LoadTimingInfo();
  EXPECT_TRUE(trans.GetLoadTimingInfo(&load_timing_info));
  // Retrying with HTTP AUTH is considered to be reusing a socket.
  TestLoadTimingReused(load_timing_info);

  response = trans.GetResponseInfo();
  ASSERT_TRUE(response);

  EXPECT_TRUE(response->headers->IsKeepAlive());
  EXPECT_EQ(200, response->headers->response_code());
  EXPECT_EQ(100, response->headers->GetContentLength());
  EXPECT_TRUE(HttpVersion(1, 1) == response->headers->GetHttpVersion());
  EXPECT_TRUE(response->did_use_http_auth);
  EXPECT_EQ(PacResultElementToProxyServer("HTTPS myproxy:70"),
            response->proxy_server);

  // The password prompt info should not be set.
  EXPECT_FALSE(response->auth_challenge.has_value());
}

// Test the challenge-response-retry sequence through an HTTPS Proxy over a
// connection that requires a restart.
TEST_F(HttpNetworkTransactionTest, HttpsProxyAuthRetryNoKeepAlive) {
  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("http://www.example.org/");
  // when the no authentication data flag is set.
  request.privacy_mode = PRIVACY_MODE_ENABLED;
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  // Configure against https proxy server "myproxy:70".
  session_deps_.proxy_resolution_service =
      ConfiguredProxyResolutionService::CreateFixedForTest(
          "https://myproxy:70", TRAFFIC_ANNOTATION_FOR_TESTS);
  session_deps_.net_log = NetLog::Get();
  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));

  // Since we have proxy, should use full url
  MockWrite data_writes1[] = {
      MockWrite("GET http://www.example.org/ HTTP/1.1\r\n"
                "Host: www.example.org\r\n"
                "Proxy-Connection: keep-alive\r\n\r\n"),
  };

  // The proxy responds to the GET with a 407, using a non-persistent
  // connection.
  MockRead data_reads1[] = {
      // No credentials.
      MockRead("HTTP/1.1 407 Proxy Authentication Required\r\n"),
      MockRead("Proxy-Authenticate: Basic realm=\"MyRealm1\"\r\n"),
      MockRead("Proxy-Connection: close\r\n"),
      MockRead("Content-Length: 0\r\n\r\n"),
  };

  MockWrite data_writes2[] = {
      // After calling trans.RestartWithAuth(), this is the request we should
      // be issuing -- the final header line contains the credentials.
      MockWrite("GET http://www.example.org/ HTTP/1.1\r\n"
                "Host: www.example.org\r\n"
                "Proxy-Connection: keep-alive\r\n"
                "Proxy-Authorization: Basic Zm9vOmJhcg==\r\n\r\n"),
  };

  MockRead data_reads2[] = {
      MockRead("HTTP/1.1 200 OK\r\n"),
      MockRead("Content-Type: text/html; charset=iso-8859-1\r\n"),
      MockRead("Content-Length: 100\r\n\r\n"),
      MockRead(SYNCHRONOUS, OK),
  };

  StaticSocketDataProvider data1(data_reads1, data_writes1);
  session_deps_.socket_factory->AddSocketDataProvider(&data1);
  StaticSocketDataProvider data2(data_reads2, data_writes2);
  session_deps_.socket_factory->AddSocketDataProvider(&data2);

  // One per each proxy connection.
  SSLSocketDataProvider ssl1(ASYNC, OK);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl1);
  SSLSocketDataProvider ssl2(ASYNC, OK);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl2);

  TestCompletionCallback callback1;

  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

  int rv = trans.Start(&request, callback1.callback(),
                       NetLogWithSource::Make(NetLogSourceType::NONE));
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback1.WaitForResult();
  EXPECT_THAT(rv, IsOk());

  LoadTimingInfo load_timing_info;
  EXPECT_TRUE(trans.GetLoadTimingInfo(&load_timing_info));
  TestLoadTimingNotReused(load_timing_info,
                          CONNECT_TIMING_HAS_CONNECT_TIMES_ONLY);

  const HttpResponseInfo* response = trans.GetResponseInfo();
  ASSERT_TRUE(response);
  ASSERT_TRUE(response->headers);
  EXPECT_EQ(407, response->headers->response_code());
  EXPECT_TRUE(HttpVersion(1, 1) == response->headers->GetHttpVersion());
  EXPECT_TRUE(CheckBasicSecureProxyAuth(response->auth_challenge));
  EXPECT_FALSE(response->did_use_http_auth);
  EXPECT_EQ(PacResultElementToProxyServer("HTTPS myproxy:70"),
            response->proxy_server);

  TestCompletionCallback callback2;

  rv = trans.RestartWithAuth(AuthCredentials(kFoo, kBar), callback2.callback());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback2.WaitForResult();
  EXPECT_THAT(rv, IsOk());

  load_timing_info = LoadTimingInfo();
  EXPECT_TRUE(trans.GetLoadTimingInfo(&load_timing_info));
  TestLoadTimingNotReused(load_timing_info,
                          CONNECT_TIMING_HAS_CONNECT_TIMES_ONLY);

  response = trans.GetResponseInfo();
  ASSERT_TRUE(response);

  EXPECT_TRUE(response->headers->IsKeepAlive());
  EXPECT_EQ(200, response->headers->response_code());
  EXPECT_EQ(100, response->headers->GetContentLength());
  EXPECT_TRUE(HttpVersion(1, 1) == response->headers->GetHttpVersion());
  EXPECT_TRUE(response->did_use_http_auth);
  EXPECT_EQ(PacResultElementToProxyServer("HTTPS myproxy:70"),
            response->proxy_server);

  // The password prompt info should not be set.
  EXPECT_FALSE(response->auth_challenge.has_value());
}

// Test the challenge-response-retry sequence through an HTTPS Proxy over a
// connection that requires a restart, with a proxy change occurring over the
// restart.
TEST_F(HttpNetworkTransactionTest, HttpsProxyAuthRetryNoKeepAliveChangeProxy) {
  const auto proxy1 = PacResultElementToProxyServer("HTTPS myproxy:70");
  const auto proxy2 = PacResultElementToProxyServer("HTTPS myproxy2:70");
  auto proxy_delegate = std::make_unique<SingleProxyDelegate>();
  proxy_delegate->set_proxy(proxy1);

  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("http://www.example.org/");
  // when the no authentication data flag is set.
  request.privacy_mode = PRIVACY_MODE_ENABLED;
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  // Configure against https proxy server "myproxy:70".
  session_deps_.proxy_resolution_service =
      ConfiguredProxyResolutionService::CreateFixedForTest(
          "https://myproxy:70", TRAFFIC_ANNOTATION_FOR_TESTS);
  session_deps_.proxy_resolution_service->SetProxyDelegate(
      proxy_delegate.get());
  session_deps_.net_log = NetLog::Get();
  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));

  // Since we have proxy, should use full url
  MockWrite data_writes1[] = {
      MockWrite("GET http://www.example.org/ HTTP/1.1\r\n"
                "Host: www.example.org\r\n"
                "Proxy-Connection: keep-alive\r\n\r\n"),
  };

  // The proxy responds to the GET with a 407, using a non-persistent
  // connection.
  MockRead data_reads1[] = {
      // No credentials.
      MockRead("HTTP/1.1 407 Proxy Authentication Required\r\n"),
      MockRead("Proxy-Authenticate: Basic realm=\"MyRealm1\"\r\n"),
      MockRead("Proxy-Connection: close\r\n"),
      MockRead("Content-Length: 0\r\n\r\n"),
  };

  MockWrite data_writes2[] = {
      // After calling trans.RestartWithAuth(), this is the request we should
      // be issuing -- the final header line contains the credentials.
      MockWrite("GET http://www.example.org/ HTTP/1.1\r\n"
                "Host: www.example.org\r\n"
                "Proxy-Connection: keep-alive\r\n"
                "Proxy-Authorization: Basic Zm9vOmJhcg==\r\n\r\n"),
  };

  MockRead data_reads2[] = {
      MockRead("HTTP/1.1 200 OK\r\n"),
      MockRead("Content-Type: text/html; charset=iso-8859-1\r\n"),
      MockRead("Content-Length: 100\r\n\r\n"),
      MockRead(SYNCHRONOUS, OK),
  };

  StaticSocketDataProvider data1(data_reads1, data_writes1);
  session_deps_.socket_factory->AddSocketDataProvider(&data1);
  StaticSocketDataProvider data2(data_reads2, data_writes2);
  session_deps_.socket_factory->AddSocketDataProvider(&data2);

  // One per each proxy connection.
  SSLSocketDataProvider ssl1(ASYNC, OK);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl1);
  SSLSocketDataProvider ssl2(ASYNC, OK);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl2);

  TestCompletionCallback callback1;

  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

  int rv = trans.Start(&request, callback1.callback(),
                       NetLogWithSource::Make(NetLogSourceType::NONE));
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback1.WaitForResult();
  EXPECT_THAT(rv, IsOk());

  LoadTimingInfo load_timing_info;
  EXPECT_TRUE(trans.GetLoadTimingInfo(&load_timing_info));
  TestLoadTimingNotReused(load_timing_info,
                          CONNECT_TIMING_HAS_CONNECT_TIMES_ONLY);

  const HttpResponseInfo* response = trans.GetResponseInfo();
  ASSERT_TRUE(response);
  ASSERT_TRUE(response->headers);
  EXPECT_EQ(407, response->headers->response_code());
  EXPECT_TRUE(HttpVersion(1, 1) == response->headers->GetHttpVersion());
  EXPECT_TRUE(CheckBasicSecureProxyAuth(response->auth_challenge));
  EXPECT_FALSE(response->did_use_http_auth);
  EXPECT_EQ(proxy1, response->proxy_server);

  TestCompletionCallback callback2;

  // Configure against https proxy server "myproxy2:70".
  proxy_delegate->set_proxy(proxy2);

  rv = trans.RestartWithAuth(AuthCredentials(kFoo, kBar), callback2.callback());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback2.WaitForResult();
  EXPECT_THAT(rv, IsOk());

  load_timing_info = LoadTimingInfo();
  EXPECT_TRUE(trans.GetLoadTimingInfo(&load_timing_info));
  TestLoadTimingNotReused(load_timing_info,
                          CONNECT_TIMING_HAS_CONNECT_TIMES_ONLY);

  response = trans.GetResponseInfo();
  ASSERT_TRUE(response);

  EXPECT_TRUE(response->headers->IsKeepAlive());
  EXPECT_EQ(200, response->headers->response_code());
  EXPECT_EQ(100, response->headers->GetContentLength());
  EXPECT_TRUE(HttpVersion(1, 1) == response->headers->GetHttpVersion());
  EXPECT_TRUE(response->did_use_http_auth);
  EXPECT_EQ(proxy2, response->proxy_server);

  // The password prompt info should not be set.
  EXPECT_FALSE(response->auth_challenge.has_value());
}

// Test the challenge-response-retry sequence through an HTTPS Proxy over a
// connection that requires a restart, with a change to a direct connection
// occurring over the restart.
TEST_F(HttpNetworkTransactionTest,
       HttpsProxyAuthRetryNoKeepAliveChangeToDirect) {
  const auto proxy = PacResultElementToProxyServer("HTTPS myproxy:70");
  const auto direct = ProxyServer::Direct();
  auto proxy_delegate = std::make_unique<SingleProxyDelegate>();
  proxy_delegate->set_proxy(proxy);

  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("http://www.example.org/");
  // when the no authentication data flag is set.
  request.privacy_mode = PRIVACY_MODE_ENABLED;
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  // Configure against https proxy server "myproxy:70".
  session_deps_.proxy_resolution_service =
      ConfiguredProxyResolutionService::CreateFixedForTest(
          "https://myproxy:70", TRAFFIC_ANNOTATION_FOR_TESTS);
  session_deps_.proxy_resolution_service->SetProxyDelegate(
      proxy_delegate.get());
  session_deps_.net_log = NetLog::Get();
  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));

  // Since we have proxy, should use full url
  MockWrite data_writes1[] = {
      MockWrite("GET http://www.example.org/ HTTP/1.1\r\n"
                "Host: www.example.org\r\n"
                "Proxy-Connection: keep-alive\r\n\r\n"),
  };

  // The proxy responds to the GET with a 407, using a non-persistent
  // connection.
  MockRead data_reads1[] = {
      // No credentials.
      MockRead("HTTP/1.1 407 Proxy Authentication Required\r\n"),
      MockRead("Proxy-Authenticate: Basic realm=\"MyRealm1\"\r\n"),
      MockRead("Proxy-Connection: close\r\n"),
      MockRead("Content-Length: 0\r\n\r\n"),
  };

  MockWrite data_writes2[] = {
      // After calling trans.RestartWithAuth(), this is the request we should
      // be issuing.
      MockWrite("GET / HTTP/1.1\r\n"
                "Host: www.example.org\r\n"
                "Connection: keep-alive\r\n\r\n"),
  };

  MockRead data_reads2[] = {
      MockRead("HTTP/1.1 200 OK\r\n"),
      MockRead("Content-Type: text/html; charset=iso-8859-1\r\n"),
      MockRead("Content-Length: 100\r\n\r\n"),
      MockRead(SYNCHRONOUS, OK),
  };

  StaticSocketDataProvider data1(data_reads1, data_writes1);
  session_deps_.socket_factory->AddSocketDataProvider(&data1);
  StaticSocketDataProvider data2(data_reads2, data_writes2);
  session_deps_.socket_factory->AddSocketDataProvider(&data2);

  // One per each connection.
  SSLSocketDataProvider ssl1(ASYNC, OK);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl1);
  SSLSocketDataProvider ssl2(ASYNC, OK);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl2);

  TestCompletionCallback callback1;

  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

  int rv = trans.Start(&request, callback1.callback(),
                       NetLogWithSource::Make(NetLogSourceType::NONE));
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback1.WaitForResult();
  EXPECT_THAT(rv, IsOk());

  LoadTimingInfo load_timing_info;
  EXPECT_TRUE(trans.GetLoadTimingInfo(&load_timing_info));
  TestLoadTimingNotReused(load_timing_info,
                          CONNECT_TIMING_HAS_CONNECT_TIMES_ONLY);

  const HttpResponseInfo* response = trans.GetResponseInfo();
  ASSERT_TRUE(response);
  ASSERT_TRUE(response->headers);
  EXPECT_EQ(407, response->headers->response_code());
  EXPECT_TRUE(HttpVersion(1, 1) == response->headers->GetHttpVersion());
  EXPECT_TRUE(CheckBasicSecureProxyAuth(response->auth_challenge));
  EXPECT_FALSE(response->did_use_http_auth);
  EXPECT_EQ(proxy, response->proxy_server);

  TestCompletionCallback callback2;

  // Configure to use a direct connection.
  proxy_delegate->set_proxy(direct);

  rv = trans.RestartWithAuth(AuthCredentials(kFoo, kBar), callback2.callback());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback2.WaitForResult();
  EXPECT_THAT(rv, IsOk());

  load_timing_info = LoadTimingInfo();
  EXPECT_TRUE(trans.GetLoadTimingInfo(&load_timing_info));
  TestLoadTimingNotReused(load_timing_info, CONNECT_TIMING_HAS_DNS_TIMES);

  response = trans.GetResponseInfo();
  ASSERT_TRUE(response);

  EXPECT_TRUE(response->headers->IsKeepAlive());
  EXPECT_EQ(200, response->headers->response_code());
  EXPECT_EQ(100, response->headers->GetContentLength());
  EXPECT_TRUE(HttpVersion(1, 1) == response->headers->GetHttpVersion());
  EXPECT_FALSE(response->did_use_http_auth);
  EXPECT_EQ(direct, response->proxy_server);

  // The password prompt info should not be set.
  EXPECT_FALSE(response->auth_challenge.has_value());
}

void HttpNetworkTransactionTest::ConnectStatusHelperWithExpectedStatus(
    const MockRead& status, int expected_status) {
  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("https://www.example.org/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  // Configure against proxy server "myproxy:70".
  session_deps_.proxy_resolution_service =
      ConfiguredProxyResolutionService::CreateFixedForTest(
          "myproxy:70", TRAFFIC_ANNOTATION_FOR_TESTS);
  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));

  // Since we have proxy, should try to establish tunnel.
  MockWrite data_writes[] = {
      MockWrite("CONNECT www.example.org:443 HTTP/1.1\r\n"
                "Host: www.example.org:443\r\n"
                "Proxy-Connection: keep-alive\r\n\r\n"),
  };

  MockRead data_reads[] = {
      status, MockRead("Content-Length: 10\r\n\r\n"),
      // No response body because the test stops reading here.
      MockRead(SYNCHRONOUS, ERR_UNEXPECTED),
  };

  StaticSocketDataProvider data(data_reads, data_writes);
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  TestCompletionCallback callback;

  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

  int rv = trans.Start(&request, callback.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback.WaitForResult();
  EXPECT_EQ(expected_status, rv);
}

void HttpNetworkTransactionTest::ConnectStatusHelper(
    const MockRead& status) {
  ConnectStatusHelperWithExpectedStatus(
      status, ERR_TUNNEL_CONNECTION_FAILED);
}

TEST_F(HttpNetworkTransactionTest, ConnectStatus100) {
  ConnectStatusHelper(MockRead("HTTP/1.1 100 Continue\r\n"));
}

TEST_F(HttpNetworkTransactionTest, ConnectStatus101) {
  ConnectStatusHelper(MockRead("HTTP/1.1 101 Switching Protocols\r\n"));
}

TEST_F(HttpNetworkTransactionTest, ConnectStatus201) {
  ConnectStatusHelper(MockRead("HTTP/1.1 201 Created\r\n"));
}

TEST_F(HttpNetworkTransactionTest, ConnectStatus202) {
  ConnectStatusHelper(MockRead("HTTP/1.1 202 Accepted\r\n"));
}

TEST_F(HttpNetworkTransactionTest, ConnectStatus203) {
  ConnectStatusHelper(
      MockRead("HTTP/1.1 203 Non-Authoritative Information\r\n"));
}

TEST_F(HttpNetworkTransactionTest, ConnectStatus204) {
  ConnectStatusHelper(MockRead("HTTP/1.1 204 No Content\r\n"));
}

TEST_F(HttpNetworkTransactionTest, ConnectStatus205) {
  ConnectStatusHelper(MockRead("HTTP/1.1 205 Reset Content\r\n"));
}

TEST_F(HttpNetworkTransactionTest, ConnectStatus206) {
  ConnectStatusHelper(MockRead("HTTP/1.1 206 Partial Content\r\n"));
}

TEST_F(HttpNetworkTransactionTest, ConnectStatus300) {
  ConnectStatusHelper(MockRead("HTTP/1.1 300 Multiple Choices\r\n"));
}

TEST_F(HttpNetworkTransactionTest, ConnectStatus301) {
  ConnectStatusHelper(MockRead("HTTP/1.1 301 Moved Permanently\r\n"));
}

TEST_F(HttpNetworkTransactionTest, ConnectStatus302) {
  ConnectStatusHelper(MockRead("HTTP/1.1 302 Found\r\n"));
}

TEST_F(HttpNetworkTransactionTest, ConnectStatus303) {
  ConnectStatusHelper(MockRead("HTTP/1.1 303 See Other\r\n"));
}

TEST_F(HttpNetworkTransactionTest, ConnectStatus304) {
  ConnectStatusHelper(MockRead("HTTP/1.1 304 Not Modified\r\n"));
}

TEST_F(HttpNetworkTransactionTest, ConnectStatus305) {
  ConnectStatusHelper(MockRead("HTTP/1.1 305 Use Proxy\r\n"));
}

TEST_F(HttpNetworkTransactionTest, ConnectStatus306) {
  ConnectStatusHelper(MockRead("HTTP/1.1 306\r\n"));
}

TEST_F(HttpNetworkTransactionTest, ConnectStatus307) {
  ConnectStatusHelper(MockRead("HTTP/1.1 307 Temporary Redirect\r\n"));
}

TEST_F(HttpNetworkTransactionTest, ConnectStatus308) {
  ConnectStatusHelper(MockRead("HTTP/1.1 308 Permanent Redirect\r\n"));
}

TEST_F(HttpNetworkTransactionTest, ConnectStatus400) {
  ConnectStatusHelper(MockRead("HTTP/1.1 400 Bad Request\r\n"));
}

TEST_F(HttpNetworkTransactionTest, ConnectStatus401) {
  ConnectStatusHelper(MockRead("HTTP/1.1 401 Unauthorized\r\n"));
}

TEST_F(HttpNetworkTransactionTest, ConnectStatus402) {
  ConnectStatusHelper(MockRead("HTTP/1.1 402 Payment Required\r\n"));
}

TEST_F(HttpNetworkTransactionTest, ConnectStatus403) {
  ConnectStatusHelper(MockRead("HTTP/1.1 403 Forbidden\r\n"));
}

TEST_F(HttpNetworkTransactionTest, ConnectStatus404) {
  ConnectStatusHelper(MockRead("HTTP/1.1 404 Not Found\r\n"));
}

TEST_F(HttpNetworkTransactionTest, ConnectStatus405) {
  ConnectStatusHelper(MockRead("HTTP/1.1 405 Method Not Allowed\r\n"));
}

TEST_F(HttpNetworkTransactionTest, ConnectStatus406) {
  ConnectStatusHelper(MockRead("HTTP/1.1 406 Not Acceptable\r\n"));
}

TEST_F(HttpNetworkTransactionTest, ConnectStatus407) {
  ConnectStatusHelperWithExpectedStatus(
      MockRead("HTTP/1.1 407 Proxy Authentication Required\r\n"),
      ERR_PROXY_AUTH_UNSUPPORTED);
}

TEST_F(HttpNetworkTransactionTest, ConnectStatus408) {
  ConnectStatusHelper(MockRead("HTTP/1.1 408 Request Timeout\r\n"));
}

TEST_F(HttpNetworkTransactionTest, ConnectStatus409) {
  ConnectStatusHelper(MockRead("HTTP/1.1 409 Conflict\r\n"));
}

TEST_F(HttpNetworkTransactionTest, ConnectStatus410) {
  ConnectStatusHelper(MockRead("HTTP/1.1 410 Gone\r\n"));
}

TEST_F(HttpNetworkTransactionTest, ConnectStatus411) {
  ConnectStatusHelper(MockRead("HTTP/1.1 411 Length Required\r\n"));
}

TEST_F(HttpNetworkTransactionTest, ConnectStatus412) {
  ConnectStatusHelper(MockRead("HTTP/1.1 412 Precondition Failed\r\n"));
}

TEST_F(HttpNetworkTransactionTest, ConnectStatus413) {
  ConnectStatusHelper(MockRead("HTTP/1.1 413 Request Entity Too Large\r\n"));
}

TEST_F(HttpNetworkTransactionTest, ConnectStatus414) {
  ConnectStatusHelper(MockRead("HTTP/1.1 414 Request-URI Too Long\r\n"));
}

TEST_F(HttpNetworkTransactionTest, ConnectStatus415) {
  ConnectStatusHelper(MockRead("HTTP/1.1 415 Unsupported Media Type\r\n"));
}

TEST_F(HttpNetworkTransactionTest, ConnectStatus416) {
  ConnectStatusHelper(
      MockRead("HTTP/1.1 416 Requested Range Not Satisfiable\r\n"));
}

TEST_F(HttpNetworkTransactionTest, ConnectStatus417) {
  ConnectStatusHelper(MockRead("HTTP/1.1 417 Expectation Failed\r\n"));
}

TEST_F(HttpNetworkTransactionTest, ConnectStatus500) {
  ConnectStatusHelper(MockRead("HTTP/1.1 500 Internal Server Error\r\n"));
}

TEST_F(HttpNetworkTransactionTest, ConnectStatus501) {
  ConnectStatusHelper(MockRead("HTTP/1.1 501 Not Implemented\r\n"));
}

TEST_F(HttpNetworkTransactionTest, ConnectStatus502) {
  ConnectStatusHelper(MockRead("HTTP/1.1 502 Bad Gateway\r\n"));
}

TEST_F(HttpNetworkTransactionTest, ConnectStatus503) {
  ConnectStatusHelper(MockRead("HTTP/1.1 503 Service Unavailable\r\n"));
}

TEST_F(HttpNetworkTransactionTest, ConnectStatus504) {
  ConnectStatusHelper(MockRead("HTTP/1.1 504 Gateway Timeout\r\n"));
}

TEST_F(HttpNetworkTransactionTest, ConnectStatus505) {
  ConnectStatusHelper(MockRead("HTTP/1.1 505 HTTP Version Not Supported\r\n"));
}

// Test the flow when both the proxy server AND origin server require
// authentication. Again, this uses basic auth for both since that is
// the simplest to mock.
TEST_F(HttpNetworkTransactionTest, BasicAuthProxyThenServer) {
  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("http://www.example.org/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  // Configure against proxy server "myproxy:70".
  session_deps_.proxy_resolution_service =
      ConfiguredProxyResolutionService::CreateFixedForTest(
          "myproxy:70", TRAFFIC_ANNOTATION_FOR_TESTS);
  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));

  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

  MockWrite data_writes1[] = {
      MockWrite(
          "GET http://www.example.org/ HTTP/1.1\r\n"
          "Host: www.example.org\r\n"
          "Proxy-Connection: keep-alive\r\n\r\n"),
  };

  MockRead data_reads1[] = {
    MockRead("HTTP/1.0 407 Unauthorized\r\n"),
    // Give a couple authenticate options (only the middle one is actually
    // supported).
    MockRead("Proxy-Authenticate: Basic invalid\r\n"),  // Malformed.
    MockRead("Proxy-Authenticate: Basic realm=\"MyRealm1\"\r\n"),
    MockRead("Proxy-Authenticate: UNSUPPORTED realm=\"FOO\"\r\n"),
    MockRead("Content-Type: text/html; charset=iso-8859-1\r\n"),
    // Large content-length -- won't matter, as connection will be reset.
    MockRead("Content-Length: 10000\r\n\r\n"),
    MockRead(SYNCHRONOUS, ERR_FAILED),
  };

  // After calling trans.RestartWithAuth() the first time, this is the
  // request we should be issuing -- the final header line contains the
  // proxy's credentials.
  MockWrite data_writes2[] = {
      MockWrite(
          "GET http://www.example.org/ HTTP/1.1\r\n"
          "Host: www.example.org\r\n"
          "Proxy-Connection: keep-alive\r\n"
          "Proxy-Authorization: Basic Zm9vOmJhcg==\r\n\r\n"),
  };

  // Now the proxy server lets the request pass through to origin server.
  // The origin server responds with a 401.
  MockRead data_reads2[] = {
    MockRead("HTTP/1.0 401 Unauthorized\r\n"),
    // Note: We are using the same realm-name as the proxy server. This is
    // completely valid, as realms are unique across hosts.
    MockRead("WWW-Authenticate: Basic realm=\"MyRealm1\"\r\n"),
    MockRead("Content-Type: text/html; charset=iso-8859-1\r\n"),
    MockRead("Content-Length: 2000\r\n\r\n"),
    MockRead(SYNCHRONOUS, ERR_FAILED),  // Won't be reached.
  };

  // After calling trans.RestartWithAuth() the second time, we should send
  // the credentials for both the proxy and origin server.
  MockWrite data_writes3[] = {
      MockWrite(
          "GET http://www.example.org/ HTTP/1.1\r\n"
          "Host: www.example.org\r\n"
          "Proxy-Connection: keep-alive\r\n"
          "Proxy-Authorization: Basic Zm9vOmJhcg==\r\n"
          "Authorization: Basic Zm9vMjpiYXIy\r\n\r\n"),
  };

  // Lastly we get the desired content.
  MockRead data_reads3[] = {
    MockRead("HTTP/1.0 200 OK\r\n"),
    MockRead("Content-Type: text/html; charset=iso-8859-1\r\n"),
    MockRead("Content-Length: 100\r\n\r\n"),
    MockRead(SYNCHRONOUS, OK),
  };

  StaticSocketDataProvider data1(data_reads1, data_writes1);
  StaticSocketDataProvider data2(data_reads2, data_writes2);
  StaticSocketDataProvider data3(data_reads3, data_writes3);
  session_deps_.socket_factory->AddSocketDataProvider(&data1);
  session_deps_.socket_factory->AddSocketDataProvider(&data2);
  session_deps_.socket_factory->AddSocketDataProvider(&data3);

  TestCompletionCallback callback1;

  int rv = trans.Start(&request, callback1.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback1.WaitForResult();
  EXPECT_THAT(rv, IsOk());

  const HttpResponseInfo* response = trans.GetResponseInfo();
  ASSERT_TRUE(response);
  EXPECT_TRUE(CheckBasicProxyAuth(response->auth_challenge));

  TestCompletionCallback callback2;

  rv = trans.RestartWithAuth(AuthCredentials(kFoo, kBar), callback2.callback());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback2.WaitForResult();
  EXPECT_THAT(rv, IsOk());

  response = trans.GetResponseInfo();
  ASSERT_TRUE(response);
  EXPECT_TRUE(CheckBasicServerAuth(response->auth_challenge));

  TestCompletionCallback callback3;

  rv = trans.RestartWithAuth(AuthCredentials(kFoo2, kBar2),
                             callback3.callback());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback3.WaitForResult();
  EXPECT_THAT(rv, IsOk());

  response = trans.GetResponseInfo();
  EXPECT_FALSE(response->auth_challenge.has_value());
  EXPECT_EQ(100, response->headers->GetContentLength());
}

// For the NTLM implementation using SSPI, we skip the NTLM tests since we
// can't hook into its internals to cause it to generate predictable NTLM
// authorization headers.
#if defined(NTLM_PORTABLE)
// The NTLM authentication unit tests are based on known test data from the
// [MS-NLMP] Specification [1]. These tests are primarily of the authentication
// flow rather than the implementation of the NTLM protocol. See net/ntlm
// for the implementation and testing of the protocol.
//
// [1] https://msdn.microsoft.com/en-us/library/cc236621.aspx

// Enter the correct password and authenticate successfully.
TEST_F(HttpNetworkTransactionTest, NTLMAuthV2) {
  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("https://server/kids/login.aspx");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  // Ensure load is not disrupted by flags which suppress behaviour specific
  // to other auth schemes.
  request.load_flags = LOAD_DO_NOT_USE_EMBEDDED_IDENTITY;

  HttpAuthNtlmMechanism::ScopedProcSetter proc_setter(
      MockGetMSTime, MockGenerateRandom, MockGetHostName);
  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));

  // Generate the NTLM messages based on known test data.
  std::string negotiate_msg;
  std::string challenge_msg;
  std::string authenticate_msg;
  base::Base64Encode(
      base::StringPiece(
          reinterpret_cast<const char*>(ntlm::test::kExpectedNegotiateMsg),
          std::size(ntlm::test::kExpectedNegotiateMsg)),
      &negotiate_msg);
  base::Base64Encode(
      base::StringPiece(
          reinterpret_cast<const char*>(ntlm::test::kChallengeMsgFromSpecV2),
          std::size(ntlm::test::kChallengeMsgFromSpecV2)),
      &challenge_msg);
  base::Base64Encode(
      base::StringPiece(
          reinterpret_cast<const char*>(
              ntlm::test::kExpectedAuthenticateMsgEmptyChannelBindingsV2),
          std::size(
              ntlm::test::kExpectedAuthenticateMsgEmptyChannelBindingsV2)),
      &authenticate_msg);

  MockWrite data_writes1[] = {
      MockWrite("GET /kids/login.aspx HTTP/1.1\r\n"
                "Host: server\r\n"
                "Connection: keep-alive\r\n\r\n"),
  };

  MockRead data_reads1[] = {
    MockRead("HTTP/1.1 401 Access Denied\r\n"),
    // Negotiate and NTLM are often requested together.  However, we only want
    // to test NTLM. Since Negotiate is preferred over NTLM, we have to skip
    // the header that requests Negotiate for this test.
    MockRead("WWW-Authenticate: NTLM\r\n"),
    MockRead("Connection: close\r\n"),
    MockRead("Content-Length: 42\r\n"),
    MockRead("Content-Type: text/html\r\n\r\n"),
    // Missing content -- won't matter, as connection will be reset.
  };

  MockWrite data_writes2[] = {
      // After restarting with a null identity, this is the
      // request we should be issuing -- the final header line contains a Type
      // 1 message.
      MockWrite("GET /kids/login.aspx HTTP/1.1\r\n"
                "Host: server\r\n"
                "Connection: keep-alive\r\n"
                "Authorization: NTLM "),
      MockWrite(negotiate_msg.c_str()), MockWrite("\r\n\r\n"),

      // After calling trans.RestartWithAuth(), we should send a Type 3 message
      // (using correct credentials).  The second request continues on the
      // same connection.
      MockWrite("GET /kids/login.aspx HTTP/1.1\r\n"
                "Host: server\r\n"
                "Connection: keep-alive\r\n"
                "Authorization: NTLM "),
      MockWrite(authenticate_msg.c_str()), MockWrite("\r\n\r\n"),
  };

  MockRead data_reads2[] = {
      // The origin server responds with a Type 2 message.
      MockRead("HTTP/1.1 401 Access Denied\r\n"),
      MockRead("WWW-Authenticate: NTLM "), MockRead(challenge_msg.c_str()),
      MockRead("\r\n"), MockRead("Content-Length: 42\r\n"),
      MockRead("Content-Type: text/html\r\n\r\n"),
      MockRead("You are not authorized to view this page\r\n"),

      // Lastly we get the desired content.
      MockRead("HTTP/1.1 200 OK\r\n"),
      MockRead("Content-Type: text/html; charset=utf-8\r\n"),
      MockRead("Content-Length: 14\r\n\r\n"), MockRead("Please Login\r\n"),
  };

  StaticSocketDataProvider data1(data_reads1, data_writes1);
  StaticSocketDataProvider data2(data_reads2, data_writes2);
  session_deps_.socket_factory->AddSocketDataProvider(&data1);
  session_deps_.socket_factory->AddSocketDataProvider(&data2);

  SSLSocketDataProvider ssl1(ASYNC, OK);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl1);
  SSLSocketDataProvider ssl2(ASYNC, OK);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl2);

  TestCompletionCallback callback1;

  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

  int rv = trans.Start(&request, callback1.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback1.WaitForResult();
  EXPECT_THAT(rv, IsOk());

  EXPECT_FALSE(trans.IsReadyToRestartForAuth());

  const HttpResponseInfo* response = trans.GetResponseInfo();
  ASSERT_TRUE(response);
  EXPECT_TRUE(CheckNTLMServerAuth(response->auth_challenge));

  TestCompletionCallback callback2;

  rv = trans.RestartWithAuth(
      AuthCredentials(ntlm::test::kDomainUserCombined, ntlm::test::kPassword),
      callback2.callback());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback2.WaitForResult();
  EXPECT_THAT(rv, IsOk());

  EXPECT_TRUE(trans.IsReadyToRestartForAuth());

  response = trans.GetResponseInfo();
  ASSERT_TRUE(response);
  EXPECT_FALSE(response->auth_challenge.has_value());

  TestCompletionCallback callback3;

  rv = trans.RestartWithAuth(AuthCredentials(), callback3.callback());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback3.WaitForResult();
  EXPECT_THAT(rv, IsOk());

  response = trans.GetResponseInfo();
  ASSERT_TRUE(response);
  EXPECT_FALSE(response->auth_challenge.has_value());
  EXPECT_EQ(14, response->headers->GetContentLength());

  std::string response_data;
  rv = ReadTransaction(&trans, &response_data);
  EXPECT_THAT(rv, IsOk());
  EXPECT_EQ("Please Login\r\n", response_data);

  EXPECT_TRUE(data1.AllReadDataConsumed());
  EXPECT_TRUE(data1.AllWriteDataConsumed());
  EXPECT_TRUE(data2.AllReadDataConsumed());
  EXPECT_TRUE(data2.AllWriteDataConsumed());
}

// Enter a wrong password, and then the correct one.
TEST_F(HttpNetworkTransactionTest, NTLMAuthV2WrongThenRightPassword) {
  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("https://server/kids/login.aspx");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  HttpAuthNtlmMechanism::ScopedProcSetter proc_setter(
      MockGetMSTime, MockGenerateRandom, MockGetHostName);
  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));

  // Generate the NTLM messages based on known test data.
  std::string negotiate_msg;
  std::string challenge_msg;
  std::string authenticate_msg;
  base::Base64Encode(
      base::StringPiece(
          reinterpret_cast<const char*>(ntlm::test::kExpectedNegotiateMsg),
          std::size(ntlm::test::kExpectedNegotiateMsg)),
      &negotiate_msg);
  base::Base64Encode(
      base::StringPiece(
          reinterpret_cast<const char*>(ntlm::test::kChallengeMsgFromSpecV2),
          std::size(ntlm::test::kChallengeMsgFromSpecV2)),
      &challenge_msg);
  base::Base64Encode(
      base::StringPiece(
          reinterpret_cast<const char*>(
              ntlm::test::kExpectedAuthenticateMsgEmptyChannelBindingsV2),
          std::size(
              ntlm::test::kExpectedAuthenticateMsgEmptyChannelBindingsV2)),
      &authenticate_msg);

  // The authenticate message when |kWrongPassword| is sent.
  std::string wrong_password_authenticate_msg(
      "TlRMTVNTUAADAAAAGAAYAFgAAACKAIoAcAAAAAwADAD6AAAACAAIAAYBAAAQABAADgEAAAAA"
      "AABYAAAAA4IIAAAAAAAAAAAAAPknEYqtJQtusopDRSfYzAAAAAAAAAAAAAAAAAAAAAAAAAAA"
      "AAAAAOtVz38osnFdRRggUQHUJ3EBAQAAAAAAAIALyP0A1NIBqqqqqqqqqqoAAAAAAgAMAEQA"
      "bwBtAGEAaQBuAAEADABTAGUAcgB2AGUAcgAGAAQAAgAAAAoAEAAAAAAAAAAAAAAAAAAAAAAA"
      "CQAWAEgAVABUAFAALwBzAGUAcgB2AGUAcgAAAAAAAAAAAEQAbwBtAGEAaQBuAFUAcwBlAHIA"
      "QwBPAE0AUABVAFQARQBSAA==");

  // Sanity check that it's the same length as the correct authenticate message
  // and that it's different.
  ASSERT_EQ(authenticate_msg.length(),
            wrong_password_authenticate_msg.length());
  ASSERT_NE(authenticate_msg, wrong_password_authenticate_msg);

  MockWrite data_writes1[] = {
      MockWrite("GET /kids/login.aspx HTTP/1.1\r\n"
                "Host: server\r\n"
                "Connection: keep-alive\r\n\r\n"),
  };

  MockRead data_reads1[] = {
    MockRead("HTTP/1.1 401 Access Denied\r\n"),
    // Negotiate and NTLM are often requested together.  However, we only want
    // to test NTLM. Since Negotiate is preferred over NTLM, we have to skip
    // the header that requests Negotiate for this test.
    MockRead("WWW-Authenticate: NTLM\r\n"),
    MockRead("Connection: close\r\n"),
    MockRead("Content-Length: 42\r\n"),
    MockRead("Content-Type: text/html\r\n\r\n"),
    // Missing content -- won't matter, as connection will be reset.
  };

  MockWrite data_writes2[] = {
      // After restarting with a null identity, this is the
      // request we should be issuing -- the final header line contains a Type
      // 1 message.
      MockWrite("GET /kids/login.aspx HTTP/1.1\r\n"
                "Host: server\r\n"
                "Connection: keep-alive\r\n"
                "Authorization: NTLM "),
      MockWrite(negotiate_msg.c_str()), MockWrite("\r\n\r\n"),

      // After calling trans.RestartWithAuth(), we should send a Type 3 message
      // (using incorrect credentials).  The second request continues on the
      // same connection.
      MockWrite("GET /kids/login.aspx HTTP/1.1\r\n"
                "Host: server\r\n"
                "Connection: keep-alive\r\n"
                "Authorization: NTLM "),
      MockWrite(wrong_password_authenticate_msg.c_str()), MockWrite("\r\n\r\n"),
  };

  MockRead data_reads2[] = {
      // The origin server responds with a Type 2 message.
      MockRead("HTTP/1.1 401 Access Denied\r\n"),
      MockRead("WWW-Authenticate: NTLM "), MockRead(challenge_msg.c_str()),
      MockRead("\r\n"), MockRead("Content-Length: 42\r\n"),
      MockRead("Content-Type: text/html\r\n\r\n"),
      MockRead("You are not authorized to view this page\r\n"),

      // Wrong password.
      MockRead("HTTP/1.1 401 Access Denied\r\n"),
      MockRead("WWW-Authenticate: NTLM\r\n"), MockRead("Connection: close\r\n"),
      MockRead("Content-Length: 42\r\n"),
      MockRead("Content-Type: text/html\r\n\r\n"),
      // Missing content -- won't matter, as connection will be reset.
  };

  MockWrite data_writes3[] = {
      // After restarting with a null identity, this is the
      // request we should be issuing -- the final header line contains a Type
      // 1 message.
      MockWrite("GET /kids/login.aspx HTTP/1.1\r\n"
                "Host: server\r\n"
                "Connection: keep-alive\r\n"
                "Authorization: NTLM "),
      MockWrite(negotiate_msg.c_str()), MockWrite("\r\n\r\n"),

      // After calling trans.RestartWithAuth(), we should send a Type 3 message
      // (the credentials for the origin server).  The second request continues
      // on the same connection.
      MockWrite("GET /kids/login.aspx HTTP/1.1\r\n"
                "Host: server\r\n"
                "Connection: keep-alive\r\n"
                "Authorization: NTLM "),
      MockWrite(authenticate_msg.c_str()), MockWrite("\r\n\r\n"),
  };

  MockRead data_reads3[] = {
      // The origin server responds with a Type 2 message.
      MockRead("HTTP/1.1 401 Access Denied\r\n"),
      MockRead("WWW-Authenticate: NTLM "), MockRead(challenge_msg.c_str()),
      MockRead("\r\n"), MockRead("Content-Length: 42\r\n"),
      MockRead("Content-Type: text/html\r\n\r\n"),
      MockRead("You are not authorized to view this page\r\n"),

      // Lastly we get the desired content.
      MockRead("HTTP/1.1 200 OK\r\n"),
      MockRead("Content-Type: text/html; charset=utf-8\r\n"),
      MockRead("Content-Length: 14\r\n\r\n"), MockRead("Please Login\r\n"),
  };

  StaticSocketDataProvider data1(data_reads1, data_writes1);
  StaticSocketDataProvider data2(data_reads2, data_writes2);
  StaticSocketDataProvider data3(data_reads3, data_writes3);
  session_deps_.socket_factory->AddSocketDataProvider(&data1);
  session_deps_.socket_factory->AddSocketDataProvider(&data2);
  session_deps_.socket_factory->AddSocketDataProvider(&data3);

  SSLSocketDataProvider ssl1(ASYNC, OK);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl1);
  SSLSocketDataProvider ssl2(ASYNC, OK);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl2);
  SSLSocketDataProvider ssl3(ASYNC, OK);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl3);

  TestCompletionCallback callback1;

  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

  int rv = trans.Start(&request, callback1.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback1.WaitForResult();
  EXPECT_THAT(rv, IsOk());

  EXPECT_FALSE(trans.IsReadyToRestartForAuth());

  const HttpResponseInfo* response = trans.GetResponseInfo();
  ASSERT_TRUE(response);
  EXPECT_TRUE(CheckNTLMServerAuth(response->auth_challenge));

  TestCompletionCallback callback2;

  // Enter the wrong password.
  rv = trans.RestartWithAuth(
      AuthCredentials(ntlm::test::kDomainUserCombined, kWrongPassword),
      callback2.callback());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback2.WaitForResult();
  EXPECT_THAT(rv, IsOk());

  EXPECT_TRUE(trans.IsReadyToRestartForAuth());
  TestCompletionCallback callback3;
  rv = trans.RestartWithAuth(AuthCredentials(), callback3.callback());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));
  rv = callback3.WaitForResult();
  EXPECT_THAT(rv, IsOk());
  EXPECT_FALSE(trans.IsReadyToRestartForAuth());

  response = trans.GetResponseInfo();
  ASSERT_TRUE(response);
  EXPECT_TRUE(CheckNTLMServerAuth(response->auth_challenge));

  TestCompletionCallback callback4;

  // Now enter the right password.
  rv = trans.RestartWithAuth(
      AuthCredentials(ntlm::test::kDomainUserCombined, ntlm::test::kPassword),
      callback4.callback());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback4.WaitForResult();
  EXPECT_THAT(rv, IsOk());

  EXPECT_TRUE(trans.IsReadyToRestartForAuth());

  TestCompletionCallback callback5;

  // One more roundtrip
  rv = trans.RestartWithAuth(AuthCredentials(), callback5.callback());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback5.WaitForResult();
  EXPECT_THAT(rv, IsOk());

  response = trans.GetResponseInfo();
  EXPECT_FALSE(response->auth_challenge.has_value());
  EXPECT_EQ(14, response->headers->GetContentLength());

  std::string response_data;
  rv = ReadTransaction(&trans, &response_data);
  EXPECT_THAT(rv, IsOk());
  EXPECT_EQ("Please Login\r\n", response_data);

  EXPECT_TRUE(data1.AllReadDataConsumed());
  EXPECT_TRUE(data1.AllWriteDataConsumed());
  EXPECT_TRUE(data2.AllReadDataConsumed());
  EXPECT_TRUE(data2.AllWriteDataConsumed());
  EXPECT_TRUE(data3.AllReadDataConsumed());
  EXPECT_TRUE(data3.AllWriteDataConsumed());
}

// Server requests NTLM authentication, which is not supported over HTTP/2.
// Subsequent request with authorization header should be sent over HTTP/1.1.
TEST_F(HttpNetworkTransactionTest, NTLMOverHttp2) {
  HttpAuthNtlmMechanism::ScopedProcSetter proc_setter(
      MockGetMSTime, MockGenerateRandom, MockGetHostName);

  const char* kUrl = "https://server/kids/login.aspx";

  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL(kUrl);
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  // First request without credentials.
  spdy::Http2HeaderBlock request_headers0(
      spdy_util_.ConstructGetHeaderBlock(kUrl));
  spdy::SpdySerializedFrame request0(spdy_util_.ConstructSpdyHeaders(
      1, std::move(request_headers0), LOWEST, true));

  spdy::Http2HeaderBlock response_headers0;
  response_headers0[spdy::kHttp2StatusHeader] = "401";
  response_headers0["www-authenticate"] = "NTLM";
  spdy::SpdySerializedFrame resp(spdy_util_.ConstructSpdyResponseHeaders(
      1, std::move(response_headers0), true));

  // Stream 1 is closed.
  spdy_util_.UpdateWithStreamDestruction(1);

  // Generate the NTLM messages based on known test data.
  std::string negotiate_msg;
  std::string challenge_msg;
  std::string authenticate_msg;
  base::Base64Encode(
      base::StringPiece(
          reinterpret_cast<const char*>(ntlm::test::kExpectedNegotiateMsg),
          std::size(ntlm::test::kExpectedNegotiateMsg)),
      &negotiate_msg);
  base::Base64Encode(
      base::StringPiece(
          reinterpret_cast<const char*>(ntlm::test::kChallengeMsgFromSpecV2),
          std::size(ntlm::test::kChallengeMsgFromSpecV2)),
      &challenge_msg);
  base::Base64Encode(
      base::StringPiece(
          reinterpret_cast<const char*>(
              ntlm::test::kExpectedAuthenticateMsgEmptyChannelBindingsV2),
          std::size(
              ntlm::test::kExpectedAuthenticateMsgEmptyChannelBindingsV2)),
      &authenticate_msg);

  MockWrite writes0[] = {CreateMockWrite(request0, 0)};
  MockRead reads0[] = {CreateMockRead(resp, 1),
                       MockRead(SYNCHRONOUS, ERR_IO_PENDING, 2)};

  // Retry yet again using HTTP/1.1.
  MockWrite writes1[] = {
      // After restarting with a null identity, this is the
      // request we should be issuing -- the final header line contains a Type
      // 1 message.
      MockWrite("GET /kids/login.aspx HTTP/1.1\r\n"
                "Host: server\r\n"
                "Connection: keep-alive\r\n"
                "Authorization: NTLM "),
      MockWrite(negotiate_msg.c_str()), MockWrite("\r\n\r\n"),

      // After calling trans.RestartWithAuth(), we should send a Type 3 message
      // (the credentials for the origin server).  The second request continues
      // on the same connection.
      MockWrite("GET /kids/login.aspx HTTP/1.1\r\n"
                "Host: server\r\n"
                "Connection: keep-alive\r\n"
                "Authorization: NTLM "),
      MockWrite(authenticate_msg.c_str()), MockWrite("\r\n\r\n"),
  };

  MockRead reads1[] = {
      // The origin server responds with a Type 2 message.
      MockRead("HTTP/1.1 401 Access Denied\r\n"),
      MockRead("WWW-Authenticate: NTLM "), MockRead(challenge_msg.c_str()),
      MockRead("\r\n"), MockRead("Content-Length: 42\r\n"),
      MockRead("Content-Type: text/html\r\n\r\n"),
      MockRead("You are not authorized to view this page\r\n"),

      // Lastly we get the desired content.
      MockRead("HTTP/1.1 200 OK\r\n"),
      MockRead("Content-Type: text/html; charset=utf-8\r\n"),
      MockRead("Content-Length: 14\r\n\r\n"), MockRead("Please Login\r\n"),
  };
  SequencedSocketData data0(reads0, writes0);
  StaticSocketDataProvider data1(reads1, writes1);
  session_deps_.socket_factory->AddSocketDataProvider(&data0);
  session_deps_.socket_factory->AddSocketDataProvider(&data1);

  SSLSocketDataProvider ssl0(ASYNC, OK);
  ssl0.next_proto = kProtoHTTP2;
  ssl0.next_protos_expected_in_ssl_config =
      NextProtoVector{kProtoHTTP2, kProtoHTTP11};
  SSLSocketDataProvider ssl1(ASYNC, OK);
  // When creating the second connection, only HTTP/1.1 should be allowed.
  ssl1.next_protos_expected_in_ssl_config = NextProtoVector{kProtoHTTP11};
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl0);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl1);

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));
  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

  TestCompletionCallback callback1;
  int rv = trans.Start(&request, callback1.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback1.WaitForResult();
  EXPECT_THAT(rv, IsOk());

  EXPECT_FALSE(trans.IsReadyToRestartForAuth());

  const HttpResponseInfo* response = trans.GetResponseInfo();
  ASSERT_TRUE(response);
  EXPECT_TRUE(CheckNTLMServerAuth(response->auth_challenge));

  TestCompletionCallback callback2;

  rv = trans.RestartWithAuth(
      AuthCredentials(ntlm::test::kDomainUserCombined, ntlm::test::kPassword),
      callback2.callback());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback2.WaitForResult();
  EXPECT_THAT(rv, IsOk());

  EXPECT_TRUE(trans.IsReadyToRestartForAuth());

  response = trans.GetResponseInfo();
  ASSERT_TRUE(response);
  EXPECT_FALSE(response->auth_challenge.has_value());

  TestCompletionCallback callback3;

  rv = trans.RestartWithAuth(AuthCredentials(), callback3.callback());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback3.WaitForResult();
  EXPECT_THAT(rv, IsOk());

  response = trans.GetResponseInfo();
  ASSERT_TRUE(response);
  EXPECT_FALSE(response->auth_challenge.has_value());
  EXPECT_EQ(14, response->headers->GetContentLength());

  std::string response_data;
  rv = ReadTransaction(&trans, &response_data);
  EXPECT_THAT(rv, IsOk());
  EXPECT_EQ("Please Login\r\n", response_data);

  EXPECT_TRUE(data0.AllReadDataConsumed());
  EXPECT_TRUE(data0.AllWriteDataConsumed());
  EXPECT_TRUE(data1.AllReadDataConsumed());
  EXPECT_TRUE(data1.AllWriteDataConsumed());
}

#if BUILDFLAG(ENABLE_WEBSOCKETS)

// Variant of above test using WebSockets.
TEST_F(HttpNetworkTransactionTest, NTLMOverHttp2WithWebsockets) {
  const GURL kInitialUrl("https://server/");
  const GURL kWebSocketUrl("wss://server/");
  HttpAuthNtlmMechanism::ScopedProcSetter proc_setter(
      MockGetMSTime, MockGenerateRandom, MockGetHostName);

  // Initial request establishes an H2 connection, which will then be reused for
  // WebSockets. This is needed since WebSockets will reuse H2 connections, but
  // it won't create a new one.
  spdy::Http2HeaderBlock initial_request_headers(
      spdy_util_.ConstructGetHeaderBlock(kInitialUrl.spec()));
  spdy::SpdySerializedFrame initial_request(spdy_util_.ConstructSpdyHeaders(
      1, std::move(initial_request_headers), DEFAULT_PRIORITY, true));
  spdy::SpdySerializedFrame settings_ack(spdy_util_.ConstructSpdySettingsAck());

  // Settings frame, indicating WebSockets is supported.
  spdy::SettingsMap settings;
  settings[spdy::SETTINGS_ENABLE_CONNECT_PROTOCOL] = 1;
  spdy::SpdySerializedFrame settings_frame(
      spdy_util_.ConstructSpdySettings(settings));

  // Response headers for first request. Body is never received, but that
  // shouldn't matter for the purposes of this test.
  spdy::SpdySerializedFrame initial_response(
      spdy_util_.ConstructSpdyGetReply(nullptr, 0, 1));

  // First WebSocket request, which has no credentials.
  spdy::Http2HeaderBlock websocket_request_headers;
  websocket_request_headers[spdy::kHttp2MethodHeader] = "CONNECT";
  websocket_request_headers[spdy::kHttp2AuthorityHeader] = "server";
  websocket_request_headers[spdy::kHttp2SchemeHeader] = "https";
  websocket_request_headers[spdy::kHttp2PathHeader] = "/";
  websocket_request_headers[spdy::kHttp2ProtocolHeader] = "websocket";
  websocket_request_headers["origin"] = "http://server";
  websocket_request_headers["sec-websocket-version"] = "13";
  websocket_request_headers["sec-websocket-extensions"] =
      "permessage-deflate; client_max_window_bits";
  spdy::SpdySerializedFrame websocket_request(spdy_util_.ConstructSpdyHeaders(
      3, std::move(websocket_request_headers), MEDIUM, false));

  // Auth challenge to WebSocket request.
  spdy::Http2HeaderBlock auth_challenge_headers;
  auth_challenge_headers[spdy::kHttp2StatusHeader] = "401";
  auth_challenge_headers["www-authenticate"] = "NTLM";
  spdy::SpdySerializedFrame websocket_auth_challenge(
      spdy_util_.ConstructSpdyResponseHeaders(
          3, std::move(auth_challenge_headers), true));

  MockWrite writes0[] = {CreateMockWrite(initial_request, 0),
                         CreateMockWrite(settings_ack, 2),
                         CreateMockWrite(websocket_request, 4),
                         MockWrite(SYNCHRONOUS, ERR_IO_PENDING, 7)};
  MockRead reads0[] = {CreateMockRead(settings_frame, 1),
                       CreateMockRead(initial_response, 3),
                       CreateMockRead(websocket_auth_challenge, 5),
                       MockRead(SYNCHRONOUS, ERR_IO_PENDING, 6)};

  // Generate the NTLM messages based on known test data.
  std::string negotiate_msg;
  std::string challenge_msg;
  std::string authenticate_msg;
  base::Base64Encode(
      base::StringPiece(
          reinterpret_cast<const char*>(ntlm::test::kExpectedNegotiateMsg),
          std::size(ntlm::test::kExpectedNegotiateMsg)),
      &negotiate_msg);
  base::Base64Encode(
      base::StringPiece(
          reinterpret_cast<const char*>(ntlm::test::kChallengeMsgFromSpecV2),
          std::size(ntlm::test::kChallengeMsgFromSpecV2)),
      &challenge_msg);
  base::Base64Encode(
      base::StringPiece(
          reinterpret_cast<const char*>(
              ntlm::test::kExpectedAuthenticateMsgEmptyChannelBindingsV2),
          std::size(
              ntlm::test::kExpectedAuthenticateMsgEmptyChannelBindingsV2)),
      &authenticate_msg);

  // Retry yet again using HTTP/1.1.
  MockWrite writes1[] = {
      // After restarting with a null identity, this is the
      // request we should be issuing -- the final header line contains a Type
      // 1 message.
      MockWrite("GET / HTTP/1.1\r\n"
                "Host: server\r\n"
                "Connection: Upgrade\r\n"
                "Authorization: NTLM "),
      MockWrite(negotiate_msg.c_str()),
      MockWrite("\r\n"),
      MockWrite("Origin: http://server\r\n"
                "Sec-WebSocket-Version: 13\r\n"
                "Upgrade: websocket\r\n"
                "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
                "Sec-WebSocket-Extensions: permessage-deflate; "
                "client_max_window_bits\r\n\r\n"),

      // After calling trans.RestartWithAuth(), we should send a Type 3 message
      // (the credentials for the origin server).  The second request continues
      // on the same connection.
      MockWrite("GET / HTTP/1.1\r\n"
                "Host: server\r\n"
                "Connection: Upgrade\r\n"
                "Authorization: NTLM "),
      MockWrite(authenticate_msg.c_str()),
      MockWrite("\r\n"),
      MockWrite("Origin: http://server\r\n"
                "Sec-WebSocket-Version: 13\r\n"
                "Upgrade: websocket\r\n"
                "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
                "Sec-WebSocket-Extensions: permessage-deflate; "
                "client_max_window_bits\r\n\r\n"),
  };

  MockRead reads1[] = {
      // The origin server responds with a Type 2 message.
      MockRead("HTTP/1.1 401 Access Denied\r\n"),
      MockRead("WWW-Authenticate: NTLM "),
      MockRead(challenge_msg.c_str()),
      MockRead("\r\n"),
      MockRead("Content-Length: 42\r\n"),
      MockRead("Content-Type: text/html\r\n\r\n"),
      MockRead("You are not authorized to view this page\r\n"),

      // Lastly we get the desired content.
      MockRead("HTTP/1.1 101 Switching Protocols\r\n"
               "Upgrade: websocket\r\n"
               "Connection: Upgrade\r\n"
               "Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n\r\n"),
  };
  SequencedSocketData data0(reads0, writes0);
  session_deps_.socket_factory->AddSocketDataProvider(&data0);
  SSLSocketDataProvider ssl0(ASYNC, OK);
  ssl0.next_proto = kProtoHTTP2;
  ssl0.next_protos_expected_in_ssl_config =
      NextProtoVector{kProtoHTTP2, kProtoHTTP11};
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl0);

  StaticSocketDataProvider data1(reads1, writes1);
  session_deps_.socket_factory->AddSocketDataProvider(&data1);
  SSLSocketDataProvider ssl1(ASYNC, OK);
  // When creating the second connection, only HTTP/1.1 should be allowed.
  ssl1.next_protos_expected_in_ssl_config = NextProtoVector{kProtoHTTP11};
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl1);

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));

  HttpRequestInfo initial_request_info;
  initial_request_info.method = "GET";
  initial_request_info.url = kInitialUrl;
  initial_request_info.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);
  HttpNetworkTransaction initial_trans(DEFAULT_PRIORITY, session.get());
  TestCompletionCallback initial_callback;
  int rv = initial_trans.Start(&initial_request_info,
                               initial_callback.callback(), NetLogWithSource());
  EXPECT_THAT(initial_callback.GetResult(rv), IsOk());

  EXPECT_FALSE(session->http_server_properties()->RequiresHTTP11(
      url::SchemeHostPort(kInitialUrl), NetworkIsolationKey()));

  HttpRequestInfo websocket_request_info;
  websocket_request_info.method = "GET";
  websocket_request_info.url = kWebSocketUrl;
  websocket_request_info.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);
  EXPECT_TRUE(HostPortPair::FromURL(initial_request_info.url)
                  .Equals(HostPortPair::FromURL(websocket_request_info.url)));
  websocket_request_info.extra_headers.SetHeader("Origin", "http://server");
  websocket_request_info.extra_headers.SetHeader("Sec-WebSocket-Version", "13");
  // The following two headers must be removed by WebSocketHttp2HandshakeStream.
  websocket_request_info.extra_headers.SetHeader("Connection", "Upgrade");
  websocket_request_info.extra_headers.SetHeader("Upgrade", "websocket");

  TestWebSocketHandshakeStreamCreateHelper websocket_stream_create_helper;

  HttpNetworkTransaction websocket_trans(MEDIUM, session.get());
  websocket_trans.SetWebSocketHandshakeStreamCreateHelper(
      &websocket_stream_create_helper);

  TestCompletionCallback websocket_callback;
  rv = websocket_trans.Start(&websocket_request_info,
                             websocket_callback.callback(), NetLogWithSource());
  EXPECT_THAT(websocket_callback.GetResult(rv), IsOk());

  EXPECT_FALSE(websocket_trans.IsReadyToRestartForAuth());

  const HttpResponseInfo* response = websocket_trans.GetResponseInfo();
  ASSERT_TRUE(response);
  EXPECT_TRUE(CheckNTLMServerAuth(response->auth_challenge));

  rv = websocket_trans.RestartWithAuth(
      AuthCredentials(ntlm::test::kDomainUserCombined, ntlm::test::kPassword),
      websocket_callback.callback());
  EXPECT_THAT(websocket_callback.GetResult(rv), IsOk());

  EXPECT_TRUE(websocket_trans.IsReadyToRestartForAuth());

  response = websocket_trans.GetResponseInfo();
  ASSERT_TRUE(response);
  EXPECT_FALSE(response->auth_challenge.has_value());

  rv = websocket_trans.RestartWithAuth(AuthCredentials(),
                                       websocket_callback.callback());
  EXPECT_THAT(websocket_callback.GetResult(rv), IsOk());

  // The server should have been marked as requiring HTTP/1.1. The important
  // part here is that the scheme that requires HTTP/1.1 should be HTTPS, not
  // WSS.
  EXPECT_TRUE(session->http_server_properties()->RequiresHTTP11(
      url::SchemeHostPort(kInitialUrl), NetworkIsolationKey()));
}

#endif  // BUILDFLAG(ENABLE_WEBSOCKETS)

// Test that, if we have an NTLM proxy and the origin resets the connection, we
// do no retry forever as a result of TLS retries. This is a regression test for
// https://crbug.com/823387. The version interference probe has since been
// removed, but we now have a legacy crypto fallback. (If that fallback is
// removed, this test should be kept but with the expectations tweaked, in case
// future fallbacks are added.)
TEST_F(HttpNetworkTransactionTest, NTLMProxyTLSHandshakeReset) {
  // The NTLM test data expects the proxy to be named 'server'. The origin is
  // https://origin/.
  session_deps_.proxy_resolution_service =
      ConfiguredProxyResolutionService::CreateFixedFromPacResultForTest(
          "PROXY server", TRAFFIC_ANNOTATION_FOR_TESTS);

  SSLContextConfig config;
  session_deps_.ssl_config_service =
      std::make_unique<TestSSLConfigService>(config);

  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("https://origin/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  // Ensure load is not disrupted by flags which suppress behaviour specific
  // to other auth schemes.
  request.load_flags = LOAD_DO_NOT_USE_EMBEDDED_IDENTITY;

  HttpAuthNtlmMechanism::ScopedProcSetter proc_setter(
      MockGetMSTime, MockGenerateRandom, MockGetHostName);
  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));

  // Generate the NTLM messages based on known test data.
  std::string negotiate_msg;
  std::string challenge_msg;
  std::string authenticate_msg;
  base::Base64Encode(
      base::StringPiece(
          reinterpret_cast<const char*>(ntlm::test::kExpectedNegotiateMsg),
          std::size(ntlm::test::kExpectedNegotiateMsg)),
      &negotiate_msg);
  base::Base64Encode(
      base::StringPiece(
          reinterpret_cast<const char*>(ntlm::test::kChallengeMsgFromSpecV2),
          std::size(ntlm::test::kChallengeMsgFromSpecV2)),
      &challenge_msg);
  base::Base64Encode(
      base::StringPiece(
          reinterpret_cast<const char*>(
              ntlm::test::kExpectedAuthenticateMsgEmptyChannelBindingsV2),
          std::size(
              ntlm::test::kExpectedAuthenticateMsgEmptyChannelBindingsV2)),
      &authenticate_msg);

  MockWrite data_writes[] = {
      // The initial CONNECT request.
      MockWrite("CONNECT origin:443 HTTP/1.1\r\n"
                "Host: origin:443\r\n"
                "Proxy-Connection: keep-alive\r\n\r\n"),

      // After restarting with an identity.
      MockWrite("CONNECT origin:443 HTTP/1.1\r\n"
                "Host: origin:443\r\n"
                "Proxy-Connection: keep-alive\r\n"
                "Proxy-Authorization: NTLM "),
      MockWrite(negotiate_msg.c_str()),
      // End headers.
      MockWrite("\r\n\r\n"),

      // The second restart.
      MockWrite("CONNECT origin:443 HTTP/1.1\r\n"
                "Host: origin:443\r\n"
                "Proxy-Connection: keep-alive\r\n"
                "Proxy-Authorization: NTLM "),
      MockWrite(authenticate_msg.c_str()),
      // End headers.
      MockWrite("\r\n\r\n"),
  };

  MockRead data_reads[] = {
      // The initial NTLM response.
      MockRead("HTTP/1.1 407 Access Denied\r\n"
               "Content-Length: 0\r\n"
               "Proxy-Authenticate: NTLM\r\n\r\n"),

      // The NTLM challenge message.
      MockRead("HTTP/1.1 407 Access Denied\r\n"
               "Content-Length: 0\r\n"
               "Proxy-Authenticate: NTLM "),
      MockRead(challenge_msg.c_str()),
      // End headers.
      MockRead("\r\n\r\n"),

      // Finally the tunnel is established.
      MockRead("HTTP/1.1 200 Connected\r\n\r\n"),
  };

  StaticSocketDataProvider data(data_reads, data_writes);
  SSLSocketDataProvider data_ssl(ASYNC, ERR_CONNECTION_RESET);
  session_deps_.socket_factory->AddSocketDataProvider(&data);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&data_ssl);

  StaticSocketDataProvider data2(data_reads, data_writes);
  SSLSocketDataProvider data2_ssl(ASYNC, ERR_CONNECTION_RESET);
  session_deps_.socket_factory->AddSocketDataProvider(&data2);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&data2_ssl);

  // Start the transaction. The proxy responds with an NTLM authentication
  // request.
  TestCompletionCallback callback;
  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());
  int rv = callback.GetResult(
      trans.Start(&request, callback.callback(), NetLogWithSource()));

  EXPECT_THAT(rv, IsOk());
  EXPECT_FALSE(trans.IsReadyToRestartForAuth());
  const HttpResponseInfo* response = trans.GetResponseInfo();
  ASSERT_TRUE(response);
  EXPECT_TRUE(CheckNTLMProxyAuth(response->auth_challenge));

  // Configure credentials and restart. The proxy responds with the challenge
  // message.
  rv = callback.GetResult(trans.RestartWithAuth(
      AuthCredentials(ntlm::test::kDomainUserCombined, ntlm::test::kPassword),
      callback.callback()));
  EXPECT_THAT(rv, IsOk());
  EXPECT_TRUE(trans.IsReadyToRestartForAuth());
  response = trans.GetResponseInfo();
  ASSERT_TRUE(response);
  EXPECT_FALSE(response->auth_challenge.has_value());

  // Restart once more. The tunnel will be established and the the SSL handshake
  // will reset. The fallback will then kick in and restart the process. The
  // proxy responds with another NTLM authentiation request, but we don't need
  // to provide credentials as the cached ones work.
  rv = callback.GetResult(
      trans.RestartWithAuth(AuthCredentials(), callback.callback()));
  EXPECT_THAT(rv, IsOk());
  EXPECT_TRUE(trans.IsReadyToRestartForAuth());
  response = trans.GetResponseInfo();
  ASSERT_TRUE(response);
  EXPECT_FALSE(response->auth_challenge.has_value());

  // The proxy responds with the NTLM challenge message.
  rv = callback.GetResult(
      trans.RestartWithAuth(AuthCredentials(), callback.callback()));
  EXPECT_THAT(rv, IsOk());
  EXPECT_TRUE(trans.IsReadyToRestartForAuth());
  response = trans.GetResponseInfo();
  ASSERT_TRUE(response);
  EXPECT_FALSE(response->auth_challenge.has_value());

  // Send the NTLM authenticate message. The tunnel is established and the
  // handshake resets again. We should not retry again.
  rv = callback.GetResult(
      trans.RestartWithAuth(AuthCredentials(), callback.callback()));
  EXPECT_THAT(rv, IsError(ERR_CONNECTION_RESET));
}

#endif  // NTLM_PORTABLE

// Test reading a server response which has only headers, and no body.
// After some maximum number of bytes is consumed, the transaction should
// fail with ERR_RESPONSE_HEADERS_TOO_BIG.
TEST_F(HttpNetworkTransactionTest, LargeHeadersNoBody) {
  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("http://www.example.org/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));
  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

  // Respond with 300 kb of headers (we should fail after 256 kb).
  std::string large_headers_string;
  FillLargeHeadersString(&large_headers_string, 300 * 1024);

  MockRead data_reads[] = {
    MockRead("HTTP/1.0 200 OK\r\n"),
    MockRead(ASYNC, large_headers_string.data(), large_headers_string.size()),
    MockRead("\r\nBODY"),
    MockRead(SYNCHRONOUS, OK),
  };
  StaticSocketDataProvider data(data_reads, base::span<MockWrite>());
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  TestCompletionCallback callback;

  int rv = trans.Start(&request, callback.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback.WaitForResult();
  EXPECT_THAT(rv, IsError(ERR_RESPONSE_HEADERS_TOO_BIG));
}

// Make sure that we don't try to reuse a TCPClientSocket when failing to
// establish tunnel.
// http://code.google.com/p/chromium/issues/detail?id=3772
TEST_F(HttpNetworkTransactionTest, DontRecycleTransportSocketForSSLTunnel) {
  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("https://www.example.org/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  // Configure against proxy server "myproxy:70".
  session_deps_.proxy_resolution_service =
      ConfiguredProxyResolutionService::CreateFixedForTest(
          "myproxy:70", TRAFFIC_ANNOTATION_FOR_TESTS);

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));

  auto trans =
      std::make_unique<HttpNetworkTransaction>(DEFAULT_PRIORITY, session.get());

  // Since we have proxy, should try to establish tunnel.
  MockWrite data_writes1[] = {
      MockWrite("CONNECT www.example.org:443 HTTP/1.1\r\n"
                "Host: www.example.org:443\r\n"
                "Proxy-Connection: keep-alive\r\n\r\n"),
  };

  // The proxy responds to the connect with a 404, using a persistent
  // connection. Usually a proxy would return 501 (not implemented),
  // or 200 (tunnel established).
  MockRead data_reads1[] = {
      MockRead("HTTP/1.1 404 Not Found\r\n"),
      MockRead("Content-Length: 10\r\n\r\n"),
      MockRead(SYNCHRONOUS, ERR_UNEXPECTED),
  };

  StaticSocketDataProvider data1(data_reads1, data_writes1);
  session_deps_.socket_factory->AddSocketDataProvider(&data1);

  TestCompletionCallback callback1;

  int rv = trans->Start(&request, callback1.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback1.WaitForResult();
  EXPECT_THAT(rv, IsError(ERR_TUNNEL_CONNECTION_FAILED));

  // Empty the current queue.  This is necessary because idle sockets are
  // added to the connection pool asynchronously with a PostTask.
  base::RunLoop().RunUntilIdle();

  // We now check to make sure the TCPClientSocket was not added back to
  // the pool.
  EXPECT_EQ(0, GetIdleSocketCountInTransportSocketPool(session.get()));
  trans.reset();
  base::RunLoop().RunUntilIdle();
  // Make sure that the socket didn't get recycled after calling the destructor.
  EXPECT_EQ(0, GetIdleSocketCountInTransportSocketPool(session.get()));
}

// Make sure that we recycle a socket after reading all of the response body.
TEST_F(HttpNetworkTransactionTest, RecycleSocket) {
  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("http://www.example.org/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));

  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

  MockRead data_reads[] = {
    // A part of the response body is received with the response headers.
    MockRead("HTTP/1.1 200 OK\r\nContent-Length: 11\r\n\r\nhel"),
    // The rest of the response body is received in two parts.
    MockRead("lo"),
    MockRead(" world"),
    MockRead("junk"),  // Should not be read!!
    MockRead(SYNCHRONOUS, OK),
  };

  StaticSocketDataProvider data(data_reads, base::span<MockWrite>());
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  TestCompletionCallback callback;

  int rv = trans.Start(&request, callback.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback.WaitForResult();
  EXPECT_THAT(rv, IsOk());

  const HttpResponseInfo* response = trans.GetResponseInfo();
  ASSERT_TRUE(response);

  EXPECT_TRUE(response->headers);
  std::string status_line = response->headers->GetStatusLine();
  EXPECT_EQ("HTTP/1.1 200 OK", status_line);

  EXPECT_EQ(0, GetIdleSocketCountInTransportSocketPool(session.get()));

  std::string response_data;
  rv = ReadTransaction(&trans, &response_data);
  EXPECT_THAT(rv, IsOk());
  EXPECT_EQ("hello world", response_data);

  // Empty the current queue.  This is necessary because idle sockets are
  // added to the connection pool asynchronously with a PostTask.
  base::RunLoop().RunUntilIdle();

  // We now check to make sure the socket was added back to the pool.
  EXPECT_EQ(1, GetIdleSocketCountInTransportSocketPool(session.get()));
}

// Make sure that we recycle a SSL socket after reading all of the response
// body.
TEST_F(HttpNetworkTransactionTest, RecycleSSLSocket) {
  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("https://www.example.org/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  MockWrite data_writes[] = {
      MockWrite(
          "GET / HTTP/1.1\r\n"
          "Host: www.example.org\r\n"
          "Connection: keep-alive\r\n\r\n"),
  };

  MockRead data_reads[] = {
    MockRead("HTTP/1.1 200 OK\r\n"),
    MockRead("Content-Length: 11\r\n\r\n"),
    MockRead("hello world"),
    MockRead(SYNCHRONOUS, OK),
  };

  SSLSocketDataProvider ssl(ASYNC, OK);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl);

  StaticSocketDataProvider data(data_reads, data_writes);
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  TestCompletionCallback callback;

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));
  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

  int rv = trans.Start(&request, callback.callback(), NetLogWithSource());

  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));
  EXPECT_THAT(callback.WaitForResult(), IsOk());

  const HttpResponseInfo* response = trans.GetResponseInfo();
  ASSERT_TRUE(response);
  ASSERT_TRUE(response->headers);
  EXPECT_EQ("HTTP/1.1 200 OK", response->headers->GetStatusLine());

  EXPECT_EQ(0, GetIdleSocketCountInTransportSocketPool(session.get()));

  std::string response_data;
  rv = ReadTransaction(&trans, &response_data);
  EXPECT_THAT(rv, IsOk());
  EXPECT_EQ("hello world", response_data);

  // Empty the current queue.  This is necessary because idle sockets are
  // added to the connection pool asynchronously with a PostTask.
  base::RunLoop().RunUntilIdle();

  // We now check to make sure the socket was added back to the pool.
  EXPECT_EQ(1, GetIdleSocketCountInTransportSocketPool(session.get()));
}

// Grab a SSL socket, use it, and put it back into the pool.  Then, reuse it
// from the pool and make sure that we recover okay.
TEST_F(HttpNetworkTransactionTest, RecycleDeadSSLSocket) {
  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("https://www.example.org/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  MockWrite data_writes[] = {
      MockWrite(
          "GET / HTTP/1.1\r\n"
          "Host: www.example.org\r\n"
          "Connection: keep-alive\r\n\r\n"),
      MockWrite(
          "GET / HTTP/1.1\r\n"
          "Host: www.example.org\r\n"
          "Connection: keep-alive\r\n\r\n"),
  };

  MockRead data_reads[] = {
      MockRead("HTTP/1.1 200 OK\r\n"), MockRead("Content-Length: 11\r\n\r\n"),
      MockRead("hello world"), MockRead(ASYNC, ERR_CONNECTION_CLOSED)};

  SSLSocketDataProvider ssl(ASYNC, OK);
  SSLSocketDataProvider ssl2(ASYNC, OK);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl2);

  StaticSocketDataProvider data(data_reads, data_writes);
  StaticSocketDataProvider data2(data_reads, data_writes);
  session_deps_.socket_factory->AddSocketDataProvider(&data);
  session_deps_.socket_factory->AddSocketDataProvider(&data2);

  TestCompletionCallback callback;

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));
  auto trans =
      std::make_unique<HttpNetworkTransaction>(DEFAULT_PRIORITY, session.get());

  int rv = trans->Start(&request, callback.callback(), NetLogWithSource());

  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));
  EXPECT_THAT(callback.WaitForResult(), IsOk());

  const HttpResponseInfo* response = trans->GetResponseInfo();
  ASSERT_TRUE(response);
  ASSERT_TRUE(response->headers);
  EXPECT_EQ("HTTP/1.1 200 OK", response->headers->GetStatusLine());

  EXPECT_EQ(0, GetIdleSocketCountInTransportSocketPool(session.get()));

  std::string response_data;
  rv = ReadTransaction(trans.get(), &response_data);
  EXPECT_THAT(rv, IsOk());
  EXPECT_EQ("hello world", response_data);

  // Empty the current queue.  This is necessary because idle sockets are
  // added to the connection pool asynchronously with a PostTask.
  base::RunLoop().RunUntilIdle();

  // We now check to make sure the socket was added back to the pool.
  EXPECT_EQ(1, GetIdleSocketCountInTransportSocketPool(session.get()));

  // Now start the second transaction, which should reuse the previous socket.

  trans =
      std::make_unique<HttpNetworkTransaction>(DEFAULT_PRIORITY, session.get());

  rv = trans->Start(&request, callback.callback(), NetLogWithSource());

  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));
  EXPECT_THAT(callback.WaitForResult(), IsOk());

  response = trans->GetResponseInfo();
  ASSERT_TRUE(response);
  ASSERT_TRUE(response->headers);
  EXPECT_EQ("HTTP/1.1 200 OK", response->headers->GetStatusLine());

  EXPECT_EQ(0, GetIdleSocketCountInTransportSocketPool(session.get()));

  rv = ReadTransaction(trans.get(), &response_data);
  EXPECT_THAT(rv, IsOk());
  EXPECT_EQ("hello world", response_data);

  // Empty the current queue.  This is necessary because idle sockets are
  // added to the connection pool asynchronously with a PostTask.
  base::RunLoop().RunUntilIdle();

  // We now check to make sure the socket was added back to the pool.
  EXPECT_EQ(1, GetIdleSocketCountInTransportSocketPool(session.get()));
}

TEST_F(HttpNetworkTransactionTest, CloseConnectionOnDestruction) {
  enum class TestCase {
    kReadHeaders,
    kReadPartOfBodyRead,
    kReadAllOfBody,
  };

  for (auto test_case : {TestCase::kReadHeaders, TestCase::kReadPartOfBodyRead,
                         TestCase::kReadAllOfBody}) {
    SCOPED_TRACE(testing::Message()
                 << "Test case: " << static_cast<int>(test_case));
    for (bool close_connection : {false, true}) {
      if (test_case != TestCase::kReadAllOfBody || close_connection == false)
        continue;
      SCOPED_TRACE(testing::Message()
                   << "Close connection: " << close_connection);

      HttpRequestInfo request;
      request.method = "GET";
      request.url = GURL("http://foo.test/");
      request.traffic_annotation =
          net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

      std::unique_ptr<HttpNetworkSession> session(
          CreateSession(&session_deps_));

      std::unique_ptr<HttpNetworkTransaction> trans =
          std::make_unique<HttpNetworkTransaction>(DEFAULT_PRIORITY,
                                                   session.get());

      MockRead data_reads[] = {
          // A part of the response body is received with the response headers.
          MockRead("HTTP/1.1 200 OK\r\n"
                   "Content-Length: 11\r\n\r\n"
                   "hello world"),
          MockRead(SYNCHRONOUS, OK),
      };

      StaticSocketDataProvider data(data_reads, base::span<MockWrite>());
      session_deps_.socket_factory->AddSocketDataProvider(&data);

      TestCompletionCallback callback;

      int rv = trans->Start(&request, callback.callback(), NetLogWithSource());
      EXPECT_THAT(callback.GetResult(rv), IsOk());

      const HttpResponseInfo* response = trans->GetResponseInfo();
      ASSERT_TRUE(response);

      EXPECT_TRUE(response->headers);
      std::string status_line = response->headers->GetStatusLine();
      EXPECT_EQ("HTTP/1.1 200 OK", status_line);

      EXPECT_EQ(0, GetIdleSocketCountInTransportSocketPool(session.get()));

      std::string response_data;
      switch (test_case) {
        case TestCase::kReadHeaders: {
          // Already read the headers, nothing else to do.
          break;
        }

        case TestCase::kReadPartOfBodyRead: {
          scoped_refptr<IOBuffer> buf = base::MakeRefCounted<IOBuffer>(5);
          rv = trans->Read(buf.get(), 5, callback.callback());
          ASSERT_EQ(5, callback.GetResult(rv));
          response_data.assign(buf->data(), 5);
          EXPECT_EQ("hello", response_data);
          break;
        }

        case TestCase::kReadAllOfBody: {
          rv = ReadTransaction(trans.get(), &response_data);
          EXPECT_THAT(rv, IsOk());
          EXPECT_EQ("hello world", response_data);
          break;
        }
      }

      if (close_connection)
        trans->CloseConnectionOnDestruction();
      trans.reset();

      // Wait for the socket to be drained and added to the socket pool or
      // destroyed.
      base::RunLoop().RunUntilIdle();

      // In the case all the body was read, the socket will have been released
      // before the CloseConnectionOnDestruction() call, so will not be
      // destroyed.
      if (close_connection && test_case != TestCase::kReadAllOfBody) {
        EXPECT_EQ(0, GetIdleSocketCountInTransportSocketPool(session.get()));
      } else {
        EXPECT_EQ(1, GetIdleSocketCountInTransportSocketPool(session.get()));
      }
    }
  }
}

// Grab a socket, use it, and put it back into the pool. Then, make
// low memory notification and ensure the socket pool is flushed.
TEST_F(HttpNetworkTransactionTest, FlushSocketPoolOnLowMemoryNotifications) {
  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("http://www.example.org/");
  request.load_flags = 0;
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));

  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

  MockRead data_reads[] = {
      // A part of the response body is received with the response headers.
      MockRead("HTTP/1.1 200 OK\r\nContent-Length: 11\r\n\r\nhel"),
      // The rest of the response body is received in two parts.
      MockRead("lo"), MockRead(" world"),
      MockRead("junk"),  // Should not be read!!
      MockRead(SYNCHRONOUS, OK),
  };

  StaticSocketDataProvider data(data_reads, base::span<MockWrite>());
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  TestCompletionCallback callback;

  int rv = trans.Start(&request, callback.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  EXPECT_THAT(callback.GetResult(rv), IsOk());

  const HttpResponseInfo* response = trans.GetResponseInfo();
  ASSERT_TRUE(response);
  EXPECT_TRUE(response->headers);
  std::string status_line = response->headers->GetStatusLine();
  EXPECT_EQ("HTTP/1.1 200 OK", status_line);

  // Make memory critical notification and ensure the transaction still has been
  // operating right.
  base::MemoryPressureListener::NotifyMemoryPressure(
      base::MemoryPressureListener::MEMORY_PRESSURE_LEVEL_CRITICAL);
  base::RunLoop().RunUntilIdle();

  // Socket should not be flushed as long as it is not idle.
  EXPECT_EQ(0, GetIdleSocketCountInTransportSocketPool(session.get()));

  std::string response_data;
  rv = ReadTransaction(&trans, &response_data);
  EXPECT_THAT(rv, IsOk());
  EXPECT_EQ("hello world", response_data);

  // Empty the current queue.  This is necessary because idle sockets are
  // added to the connection pool asynchronously with a PostTask.
  base::RunLoop().RunUntilIdle();

  // We now check to make sure the socket was added back to the pool.
  EXPECT_EQ(1, GetIdleSocketCountInTransportSocketPool(session.get()));

  // Idle sockets should be flushed now.
  base::MemoryPressureListener::NotifyMemoryPressure(
      base::MemoryPressureListener::MEMORY_PRESSURE_LEVEL_CRITICAL);
  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(0, GetIdleSocketCountInTransportSocketPool(session.get()));
}

// Disable idle socket closing on memory pressure.
// Grab a socket, use it, and put it back into the pool. Then, make
// low memory notification and ensure the socket pool is NOT flushed.
TEST_F(HttpNetworkTransactionTest, NoFlushSocketPoolOnLowMemoryNotifications) {
  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("http://www.example.org/");
  request.load_flags = 0;
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  // Disable idle socket closing on memory pressure.
  session_deps_.disable_idle_sockets_close_on_memory_pressure = true;
  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));

  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

  MockRead data_reads[] = {
      // A part of the response body is received with the response headers.
      MockRead("HTTP/1.1 200 OK\r\nContent-Length: 11\r\n\r\nhel"),
      // The rest of the response body is received in two parts.
      MockRead("lo"), MockRead(" world"),
      MockRead("junk"),  // Should not be read!!
      MockRead(SYNCHRONOUS, OK),
  };

  StaticSocketDataProvider data(data_reads, base::span<MockWrite>());
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  TestCompletionCallback callback;

  int rv = trans.Start(&request, callback.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  EXPECT_THAT(callback.GetResult(rv), IsOk());

  const HttpResponseInfo* response = trans.GetResponseInfo();
  ASSERT_TRUE(response);
  EXPECT_TRUE(response->headers);
  std::string status_line = response->headers->GetStatusLine();
  EXPECT_EQ("HTTP/1.1 200 OK", status_line);

  // Make memory critical notification and ensure the transaction still has been
  // operating right.
  base::MemoryPressureListener::NotifyMemoryPressure(
      base::MemoryPressureListener::MEMORY_PRESSURE_LEVEL_CRITICAL);
  base::RunLoop().RunUntilIdle();

  // Socket should not be flushed as long as it is not idle.
  EXPECT_EQ(0, GetIdleSocketCountInTransportSocketPool(session.get()));

  std::string response_data;
  rv = ReadTransaction(&trans, &response_data);
  EXPECT_THAT(rv, IsOk());
  EXPECT_EQ("hello world", response_data);

  // Empty the current queue.  This is necessary because idle sockets are
  // added to the connection pool asynchronously with a PostTask.
  base::RunLoop().RunUntilIdle();

  // We now check to make sure the socket was added back to the pool.
  EXPECT_EQ(1, GetIdleSocketCountInTransportSocketPool(session.get()));

  // Idle sockets should NOT be flushed on moderate memory pressure.
  base::MemoryPressureListener::NotifyMemoryPressure(
      base::MemoryPressureListener::MEMORY_PRESSURE_LEVEL_MODERATE);
  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(1, GetIdleSocketCountInTransportSocketPool(session.get()));

  // Idle sockets should NOT be flushed on critical memory pressure.
  base::MemoryPressureListener::NotifyMemoryPressure(
      base::MemoryPressureListener::MEMORY_PRESSURE_LEVEL_CRITICAL);
  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(1, GetIdleSocketCountInTransportSocketPool(session.get()));
}

// Grab an SSL socket, use it, and put it back into the pool. Then, make
// low memory notification and ensure the socket pool is flushed.
TEST_F(HttpNetworkTransactionTest, FlushSSLSocketPoolOnLowMemoryNotifications) {
  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("https://www.example.org/");
  request.load_flags = 0;
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  MockWrite data_writes[] = {
      MockWrite("GET / HTTP/1.1\r\n"
                "Host: www.example.org\r\n"
                "Connection: keep-alive\r\n\r\n"),
  };

  MockRead data_reads[] = {
      MockRead("HTTP/1.1 200 OK\r\n"), MockRead("Content-Length: 11\r\n\r\n"),
      MockRead("hello world"), MockRead(ASYNC, ERR_CONNECTION_CLOSED)};

  SSLSocketDataProvider ssl(ASYNC, OK);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl);

  StaticSocketDataProvider data(data_reads, data_writes);
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  TestCompletionCallback callback;

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));
  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

  EXPECT_EQ(0, GetIdleSocketCountInTransportSocketPool(session.get()));
  int rv = trans.Start(&request, callback.callback(), NetLogWithSource());

  EXPECT_THAT(callback.GetResult(rv), IsOk());

  const HttpResponseInfo* response = trans.GetResponseInfo();
  ASSERT_TRUE(response);
  ASSERT_TRUE(response->headers);
  EXPECT_EQ("HTTP/1.1 200 OK", response->headers->GetStatusLine());

  // Make memory critical notification and ensure the transaction still has been
  // operating right.
  base::MemoryPressureListener::NotifyMemoryPressure(
      base::MemoryPressureListener::MEMORY_PRESSURE_LEVEL_CRITICAL);
  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(0, GetIdleSocketCountInTransportSocketPool(session.get()));

  std::string response_data;
  rv = ReadTransaction(&trans, &response_data);
  EXPECT_THAT(rv, IsOk());
  EXPECT_EQ("hello world", response_data);

  // Empty the current queue.  This is necessary because idle sockets are
  // added to the connection pool asynchronously with a PostTask.
  base::RunLoop().RunUntilIdle();

  // We now check to make sure the socket was added back to the pool.
  EXPECT_EQ(1, GetIdleSocketCountInTransportSocketPool(session.get()));

  // Make memory notification once again and ensure idle socket is closed.
  base::MemoryPressureListener::NotifyMemoryPressure(
      base::MemoryPressureListener::MEMORY_PRESSURE_LEVEL_CRITICAL);
  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(0, GetIdleSocketCountInTransportSocketPool(session.get()));
}

// Make sure that we recycle a socket after a zero-length response.
// http://crbug.com/9880
TEST_F(HttpNetworkTransactionTest, RecycleSocketAfterZeroContentLength) {
  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL(
      "http://www.example.org/csi?v=3&s=web&action=&"
      "tran=undefined&ei=mAXcSeegAo-SMurloeUN&"
      "e=17259,18167,19592,19773,19981,20133,20173,20233&"
      "rt=prt.2642,ol.2649,xjs.2951");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));

  MockRead data_reads[] = {
    MockRead("HTTP/1.1 204 No Content\r\n"
             "Content-Length: 0\r\n"
             "Content-Type: text/html\r\n\r\n"),
    MockRead("junk"),  // Should not be read!!
    MockRead(SYNCHRONOUS, OK),
  };

  StaticSocketDataProvider data(data_reads, base::span<MockWrite>());
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  // Transaction must be created after the MockReads, so it's destroyed before
  // them.
  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

  TestCompletionCallback callback;

  int rv = trans.Start(&request, callback.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback.WaitForResult();
  EXPECT_THAT(rv, IsOk());

  const HttpResponseInfo* response = trans.GetResponseInfo();
  ASSERT_TRUE(response);

  EXPECT_TRUE(response->headers);
  std::string status_line = response->headers->GetStatusLine();
  EXPECT_EQ("HTTP/1.1 204 No Content", status_line);

  EXPECT_EQ(0, GetIdleSocketCountInTransportSocketPool(session.get()));

  std::string response_data;
  rv = ReadTransaction(&trans, &response_data);
  EXPECT_THAT(rv, IsOk());
  EXPECT_EQ("", response_data);

  // Empty the current queue.  This is necessary because idle sockets are
  // added to the connection pool asynchronously with a PostTask.
  base::RunLoop().RunUntilIdle();

  // We now check to make sure the socket was added back to the pool.
  EXPECT_EQ(1, GetIdleSocketCountInTransportSocketPool(session.get()));
}

TEST_F(HttpNetworkTransactionTest, ResendRequestOnWriteBodyError) {
  std::vector<std::unique_ptr<UploadElementReader>> element_readers;
  element_readers.push_back(
      std::make_unique<UploadBytesElementReader>("foo", 3));
  ElementsUploadDataStream upload_data_stream(std::move(element_readers), 0);

  HttpRequestInfo request[2];
  // Transaction 1: a GET request that succeeds.  The socket is recycled
  // after use.
  request[0].method = "GET";
  request[0].url = GURL("http://www.google.com/");
  request[0].load_flags = 0;
  request[0].traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);
  // Transaction 2: a POST request.  Reuses the socket kept alive from
  // transaction 1.  The first attempts fails when writing the POST data.
  // This causes the transaction to retry with a new socket.  The second
  // attempt succeeds.
  request[1].method = "POST";
  request[1].url = GURL("http://www.google.com/login.cgi");
  request[1].upload_data_stream = &upload_data_stream;
  request[1].load_flags = 0;
  request[1].traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));

  // The first socket is used for transaction 1 and the first attempt of
  // transaction 2.

  // The response of transaction 1.
  MockRead data_reads1[] = {
    MockRead("HTTP/1.1 200 OK\r\nContent-Length: 11\r\n\r\n"),
    MockRead("hello world"),
    MockRead(SYNCHRONOUS, OK),
  };
  // The mock write results of transaction 1 and the first attempt of
  // transaction 2.
  MockWrite data_writes1[] = {
    MockWrite(SYNCHRONOUS, 64),  // GET
    MockWrite(SYNCHRONOUS, 93),  // POST
    MockWrite(SYNCHRONOUS, ERR_CONNECTION_ABORTED),  // POST data
  };
  StaticSocketDataProvider data1(data_reads1, data_writes1);

  // The second socket is used for the second attempt of transaction 2.

  // The response of transaction 2.
  MockRead data_reads2[] = {
    MockRead("HTTP/1.1 200 OK\r\nContent-Length: 7\r\n\r\n"),
    MockRead("welcome"),
    MockRead(SYNCHRONOUS, OK),
  };
  // The mock write results of the second attempt of transaction 2.
  MockWrite data_writes2[] = {
    MockWrite(SYNCHRONOUS, 93),  // POST
    MockWrite(SYNCHRONOUS, 3),  // POST data
  };
  StaticSocketDataProvider data2(data_reads2, data_writes2);

  session_deps_.socket_factory->AddSocketDataProvider(&data1);
  session_deps_.socket_factory->AddSocketDataProvider(&data2);

  const char* const kExpectedResponseData[] = {
    "hello world", "welcome"
  };

  for (int i = 0; i < 2; ++i) {
    HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

    TestCompletionCallback callback;

    int rv = trans.Start(&request[i], callback.callback(), NetLogWithSource());
    EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

    rv = callback.WaitForResult();
    EXPECT_THAT(rv, IsOk());

    const HttpResponseInfo* response = trans.GetResponseInfo();
    ASSERT_TRUE(response);

    EXPECT_TRUE(response->headers);
    EXPECT_EQ("HTTP/1.1 200 OK", response->headers->GetStatusLine());

    std::string response_data;
    rv = ReadTransaction(&trans, &response_data);
    EXPECT_THAT(rv, IsOk());
    EXPECT_EQ(kExpectedResponseData[i], response_data);
  }
}

// Test the request-challenge-retry sequence for basic auth when there is
// an identity in the URL. The request should be sent as normal, but when
// it fails the identity from the URL is used to answer the challenge.
TEST_F(HttpNetworkTransactionTest, AuthIdentityInURL) {
  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("http://foo:b@r@www.example.org/");
  request.load_flags = LOAD_NORMAL;
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));
  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

  // The password contains an escaped character -- for this test to pass it
  // will need to be unescaped by HttpNetworkTransaction.
  EXPECT_EQ("b%40r", request.url.password());

  MockWrite data_writes1[] = {
      MockWrite(
          "GET / HTTP/1.1\r\n"
          "Host: www.example.org\r\n"
          "Connection: keep-alive\r\n\r\n"),
  };

  MockRead data_reads1[] = {
    MockRead("HTTP/1.0 401 Unauthorized\r\n"),
    MockRead("WWW-Authenticate: Basic realm=\"MyRealm1\"\r\n"),
    MockRead("Content-Length: 10\r\n\r\n"),
    MockRead(SYNCHRONOUS, ERR_FAILED),
  };

  // After the challenge above, the transaction will be restarted using the
  // identity from the url (foo, b@r) to answer the challenge.
  MockWrite data_writes2[] = {
      MockWrite(
          "GET / HTTP/1.1\r\n"
          "Host: www.example.org\r\n"
          "Connection: keep-alive\r\n"
          "Authorization: Basic Zm9vOmJAcg==\r\n\r\n"),
  };

  MockRead data_reads2[] = {
    MockRead("HTTP/1.0 200 OK\r\n"),
    MockRead("Content-Length: 100\r\n\r\n"),
    MockRead(SYNCHRONOUS, OK),
  };

  StaticSocketDataProvider data1(data_reads1, data_writes1);
  StaticSocketDataProvider data2(data_reads2, data_writes2);
  session_deps_.socket_factory->AddSocketDataProvider(&data1);
  session_deps_.socket_factory->AddSocketDataProvider(&data2);

  TestCompletionCallback callback1;
  int rv = trans.Start(&request, callback1.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));
  rv = callback1.WaitForResult();
  EXPECT_THAT(rv, IsOk());
  EXPECT_TRUE(trans.IsReadyToRestartForAuth());

  TestCompletionCallback callback2;
  rv = trans.RestartWithAuth(AuthCredentials(), callback2.callback());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));
  rv = callback2.WaitForResult();
  EXPECT_THAT(rv, IsOk());
  EXPECT_FALSE(trans.IsReadyToRestartForAuth());

  const HttpResponseInfo* response = trans.GetResponseInfo();
  ASSERT_TRUE(response);

  // There is no challenge info, since the identity in URL worked.
  EXPECT_FALSE(response->auth_challenge.has_value());

  EXPECT_EQ(100, response->headers->GetContentLength());

  // Empty the current queue.
  base::RunLoop().RunUntilIdle();
}

// Test the request-challenge-retry sequence for basic auth when there is an
// incorrect identity in the URL. The identity from the URL should be used only
// once.
TEST_F(HttpNetworkTransactionTest, WrongAuthIdentityInURL) {
  HttpRequestInfo request;
  request.method = "GET";
  // Note: the URL has a username:password in it.  The password "baz" is
  // wrong (should be "bar").
  request.url = GURL("http://foo:baz@www.example.org/");

  request.load_flags = LOAD_NORMAL;
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));
  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

  MockWrite data_writes1[] = {
      MockWrite(
          "GET / HTTP/1.1\r\n"
          "Host: www.example.org\r\n"
          "Connection: keep-alive\r\n\r\n"),
  };

  MockRead data_reads1[] = {
    MockRead("HTTP/1.0 401 Unauthorized\r\n"),
    MockRead("WWW-Authenticate: Basic realm=\"MyRealm1\"\r\n"),
    MockRead("Content-Length: 10\r\n\r\n"),
    MockRead(SYNCHRONOUS, ERR_FAILED),
  };

  // After the challenge above, the transaction will be restarted using the
  // identity from the url (foo, baz) to answer the challenge.
  MockWrite data_writes2[] = {
      MockWrite(
          "GET / HTTP/1.1\r\n"
          "Host: www.example.org\r\n"
          "Connection: keep-alive\r\n"
          "Authorization: Basic Zm9vOmJheg==\r\n\r\n"),
  };

  MockRead data_reads2[] = {
    MockRead("HTTP/1.0 401 Unauthorized\r\n"),
    MockRead("WWW-Authenticate: Basic realm=\"MyRealm1\"\r\n"),
    MockRead("Content-Length: 10\r\n\r\n"),
    MockRead(SYNCHRONOUS, ERR_FAILED),
  };

  // After the challenge above, the transaction will be restarted using the
  // identity supplied by the user (foo, bar) to answer the challenge.
  MockWrite data_writes3[] = {
      MockWrite(
          "GET / HTTP/1.1\r\n"
          "Host: www.example.org\r\n"
          "Connection: keep-alive\r\n"
          "Authorization: Basic Zm9vOmJhcg==\r\n\r\n"),
  };

  MockRead data_reads3[] = {
    MockRead("HTTP/1.0 200 OK\r\n"),
    MockRead("Content-Length: 100\r\n\r\n"),
    MockRead(SYNCHRONOUS, OK),
  };

  StaticSocketDataProvider data1(data_reads1, data_writes1);
  StaticSocketDataProvider data2(data_reads2, data_writes2);
  StaticSocketDataProvider data3(data_reads3, data_writes3);
  session_deps_.socket_factory->AddSocketDataProvider(&data1);
  session_deps_.socket_factory->AddSocketDataProvider(&data2);
  session_deps_.socket_factory->AddSocketDataProvider(&data3);

  TestCompletionCallback callback1;

  int rv = trans.Start(&request, callback1.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback1.WaitForResult();
  EXPECT_THAT(rv, IsOk());

  EXPECT_TRUE(trans.IsReadyToRestartForAuth());
  TestCompletionCallback callback2;
  rv = trans.RestartWithAuth(AuthCredentials(), callback2.callback());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));
  rv = callback2.WaitForResult();
  EXPECT_THAT(rv, IsOk());
  EXPECT_FALSE(trans.IsReadyToRestartForAuth());

  const HttpResponseInfo* response = trans.GetResponseInfo();
  ASSERT_TRUE(response);
  EXPECT_TRUE(CheckBasicServerAuth(response->auth_challenge));

  TestCompletionCallback callback3;
  rv = trans.RestartWithAuth(AuthCredentials(kFoo, kBar), callback3.callback());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));
  rv = callback3.WaitForResult();
  EXPECT_THAT(rv, IsOk());
  EXPECT_FALSE(trans.IsReadyToRestartForAuth());

  response = trans.GetResponseInfo();
  ASSERT_TRUE(response);

  // There is no challenge info, since the identity worked.
  EXPECT_FALSE(response->auth_challenge.has_value());

  EXPECT_EQ(100, response->headers->GetContentLength());

  // Empty the current queue.
  base::RunLoop().RunUntilIdle();
}


// Test the request-challenge-retry sequence for basic auth when there is a
// correct identity in the URL, but its use is being suppressed. The identity
// from the URL should never be used.
TEST_F(HttpNetworkTransactionTest, AuthIdentityInURLSuppressed) {
  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("http://foo:bar@www.example.org/");
  request.load_flags = LOAD_DO_NOT_USE_EMBEDDED_IDENTITY;
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));
  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

  MockWrite data_writes1[] = {
      MockWrite(
          "GET / HTTP/1.1\r\n"
          "Host: www.example.org\r\n"
          "Connection: keep-alive\r\n\r\n"),
  };

  MockRead data_reads1[] = {
    MockRead("HTTP/1.0 401 Unauthorized\r\n"),
    MockRead("WWW-Authenticate: Basic realm=\"MyRealm1\"\r\n"),
    MockRead("Content-Length: 10\r\n\r\n"),
    MockRead(SYNCHRONOUS, ERR_FAILED),
  };

  // After the challenge above, the transaction will be restarted using the
  // identity supplied by the user, not the one in the URL, to answer the
  // challenge.
  MockWrite data_writes3[] = {
      MockWrite(
          "GET / HTTP/1.1\r\n"
          "Host: www.example.org\r\n"
          "Connection: keep-alive\r\n"
          "Authorization: Basic Zm9vOmJhcg==\r\n\r\n"),
  };

  MockRead data_reads3[] = {
    MockRead("HTTP/1.0 200 OK\r\n"),
    MockRead("Content-Length: 100\r\n\r\n"),
    MockRead(SYNCHRONOUS, OK),
  };

  StaticSocketDataProvider data1(data_reads1, data_writes1);
  StaticSocketDataProvider data3(data_reads3, data_writes3);
  session_deps_.socket_factory->AddSocketDataProvider(&data1);
  session_deps_.socket_factory->AddSocketDataProvider(&data3);

  TestCompletionCallback callback1;
  int rv = trans.Start(&request, callback1.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));
  rv = callback1.WaitForResult();
  EXPECT_THAT(rv, IsOk());
  EXPECT_FALSE(trans.IsReadyToRestartForAuth());

  const HttpResponseInfo* response = trans.GetResponseInfo();
  ASSERT_TRUE(response);
  EXPECT_TRUE(CheckBasicServerAuth(response->auth_challenge));

  TestCompletionCallback callback3;
  rv = trans.RestartWithAuth(AuthCredentials(kFoo, kBar), callback3.callback());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));
  rv = callback3.WaitForResult();
  EXPECT_THAT(rv, IsOk());
  EXPECT_FALSE(trans.IsReadyToRestartForAuth());

  response = trans.GetResponseInfo();
  ASSERT_TRUE(response);

  // There is no challenge info, since the identity worked.
  EXPECT_FALSE(response->auth_challenge.has_value());
  EXPECT_EQ(100, response->headers->GetContentLength());

  // Empty the current queue.
  base::RunLoop().RunUntilIdle();
}

// Test that previously tried username/passwords for a realm get re-used.
TEST_F(HttpNetworkTransactionTest, BasicAuthCacheAndPreauth) {
  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));

  // Transaction 1: authenticate (foo, bar) on MyRealm1
  {
    HttpRequestInfo request;
    request.method = "GET";
    request.url = GURL("http://www.example.org/x/y/z");
    request.traffic_annotation =
        net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

    HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

    MockWrite data_writes1[] = {
        MockWrite(
            "GET /x/y/z HTTP/1.1\r\n"
            "Host: www.example.org\r\n"
            "Connection: keep-alive\r\n\r\n"),
    };

    MockRead data_reads1[] = {
      MockRead("HTTP/1.0 401 Unauthorized\r\n"),
      MockRead("WWW-Authenticate: Basic realm=\"MyRealm1\"\r\n"),
      MockRead("Content-Length: 10000\r\n\r\n"),
      MockRead(SYNCHRONOUS, ERR_FAILED),
    };

    // Resend with authorization (username=foo, password=bar)
    MockWrite data_writes2[] = {
        MockWrite(
            "GET /x/y/z HTTP/1.1\r\n"
            "Host: www.example.org\r\n"
            "Connection: keep-alive\r\n"
            "Authorization: Basic Zm9vOmJhcg==\r\n\r\n"),
    };

    // Sever accepts the authorization.
    MockRead data_reads2[] = {
      MockRead("HTTP/1.0 200 OK\r\n"),
      MockRead("Content-Length: 100\r\n\r\n"),
      MockRead(SYNCHRONOUS, OK),
    };

    StaticSocketDataProvider data1(data_reads1, data_writes1);
    StaticSocketDataProvider data2(data_reads2, data_writes2);
    session_deps_.socket_factory->AddSocketDataProvider(&data1);
    session_deps_.socket_factory->AddSocketDataProvider(&data2);

    TestCompletionCallback callback1;

    int rv = trans.Start(&request, callback1.callback(), NetLogWithSource());
    EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

    rv = callback1.WaitForResult();
    EXPECT_THAT(rv, IsOk());

    const HttpResponseInfo* response = trans.GetResponseInfo();
    ASSERT_TRUE(response);
    EXPECT_TRUE(CheckBasicServerAuth(response->auth_challenge));

    TestCompletionCallback callback2;

    rv = trans.RestartWithAuth(AuthCredentials(kFoo, kBar),
                               callback2.callback());
    EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

    rv = callback2.WaitForResult();
    EXPECT_THAT(rv, IsOk());

    response = trans.GetResponseInfo();
    ASSERT_TRUE(response);
    EXPECT_FALSE(response->auth_challenge.has_value());
    EXPECT_EQ(100, response->headers->GetContentLength());
  }

  // ------------------------------------------------------------------------

  // Transaction 2: authenticate (foo2, bar2) on MyRealm2
  {
    HttpRequestInfo request;
    request.method = "GET";
    // Note that Transaction 1 was at /x/y/z, so this is in the same
    // protection space as MyRealm1.
    request.url = GURL("http://www.example.org/x/y/a/b");
    request.traffic_annotation =
        net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

    HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

    MockWrite data_writes1[] = {
        MockWrite(
            "GET /x/y/a/b HTTP/1.1\r\n"
            "Host: www.example.org\r\n"
            "Connection: keep-alive\r\n"
            // Send preemptive authorization for MyRealm1
            "Authorization: Basic Zm9vOmJhcg==\r\n\r\n"),
    };

    // The server didn't like the preemptive authorization, and
    // challenges us for a different realm (MyRealm2).
    MockRead data_reads1[] = {
      MockRead("HTTP/1.0 401 Unauthorized\r\n"),
      MockRead("WWW-Authenticate: Basic realm=\"MyRealm2\"\r\n"),
      MockRead("Content-Length: 10000\r\n\r\n"),
      MockRead(SYNCHRONOUS, ERR_FAILED),
    };

    // Resend with authorization for MyRealm2 (username=foo2, password=bar2)
    MockWrite data_writes2[] = {
        MockWrite(
            "GET /x/y/a/b HTTP/1.1\r\n"
            "Host: www.example.org\r\n"
            "Connection: keep-alive\r\n"
            "Authorization: Basic Zm9vMjpiYXIy\r\n\r\n"),
    };

    // Sever accepts the authorization.
    MockRead data_reads2[] = {
      MockRead("HTTP/1.0 200 OK\r\n"),
      MockRead("Content-Length: 100\r\n\r\n"),
      MockRead(SYNCHRONOUS, OK),
    };

    StaticSocketDataProvider data1(data_reads1, data_writes1);
    StaticSocketDataProvider data2(data_reads2, data_writes2);
    session_deps_.socket_factory->AddSocketDataProvider(&data1);
    session_deps_.socket_factory->AddSocketDataProvider(&data2);

    TestCompletionCallback callback1;

    int rv = trans.Start(&request, callback1.callback(), NetLogWithSource());
    EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

    rv = callback1.WaitForResult();
    EXPECT_THAT(rv, IsOk());

    const HttpResponseInfo* response = trans.GetResponseInfo();
    ASSERT_TRUE(response);
    ASSERT_TRUE(response->auth_challenge);
    EXPECT_FALSE(response->auth_challenge->is_proxy);
    EXPECT_EQ("http://www.example.org",
              response->auth_challenge->challenger.Serialize());
    EXPECT_EQ("MyRealm2", response->auth_challenge->realm);
    EXPECT_EQ(kBasicAuthScheme, response->auth_challenge->scheme);

    TestCompletionCallback callback2;

    rv = trans.RestartWithAuth(AuthCredentials(kFoo2, kBar2),
                               callback2.callback());
    EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

    rv = callback2.WaitForResult();
    EXPECT_THAT(rv, IsOk());

    response = trans.GetResponseInfo();
    ASSERT_TRUE(response);
    EXPECT_FALSE(response->auth_challenge.has_value());
    EXPECT_EQ(100, response->headers->GetContentLength());
  }

  // ------------------------------------------------------------------------

  // Transaction 3: Resend a request in MyRealm's protection space --
  // succeed with preemptive authorization.
  {
    HttpRequestInfo request;
    request.method = "GET";
    request.url = GURL("http://www.example.org/x/y/z2");
    request.traffic_annotation =
        net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

    HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

    MockWrite data_writes1[] = {
        MockWrite(
            "GET /x/y/z2 HTTP/1.1\r\n"
            "Host: www.example.org\r\n"
            "Connection: keep-alive\r\n"
            // The authorization for MyRealm1 gets sent preemptively
            // (since the url is in the same protection space)
            "Authorization: Basic Zm9vOmJhcg==\r\n\r\n"),
    };

    // Sever accepts the preemptive authorization
    MockRead data_reads1[] = {
      MockRead("HTTP/1.0 200 OK\r\n"),
      MockRead("Content-Length: 100\r\n\r\n"),
      MockRead(SYNCHRONOUS, OK),
    };

    StaticSocketDataProvider data1(data_reads1, data_writes1);
    session_deps_.socket_factory->AddSocketDataProvider(&data1);

    TestCompletionCallback callback1;

    int rv = trans.Start(&request, callback1.callback(), NetLogWithSource());
    EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

    rv = callback1.WaitForResult();
    EXPECT_THAT(rv, IsOk());

    const HttpResponseInfo* response = trans.GetResponseInfo();
    ASSERT_TRUE(response);

    EXPECT_FALSE(response->auth_challenge.has_value());
    EXPECT_EQ(100, response->headers->GetContentLength());
  }

  // ------------------------------------------------------------------------

  // Transaction 4: request another URL in MyRealm (however the
  // url is not known to belong to the protection space, so no pre-auth).
  {
    HttpRequestInfo request;
    request.method = "GET";
    request.url = GURL("http://www.example.org/x/1");
    request.traffic_annotation =
        net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

    HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

    MockWrite data_writes1[] = {
        MockWrite(
            "GET /x/1 HTTP/1.1\r\n"
            "Host: www.example.org\r\n"
            "Connection: keep-alive\r\n\r\n"),
    };

    MockRead data_reads1[] = {
      MockRead("HTTP/1.0 401 Unauthorized\r\n"),
      MockRead("WWW-Authenticate: Basic realm=\"MyRealm1\"\r\n"),
      MockRead("Content-Length: 10000\r\n\r\n"),
      MockRead(SYNCHRONOUS, ERR_FAILED),
    };

    // Resend with authorization from MyRealm's cache.
    MockWrite data_writes2[] = {
        MockWrite(
            "GET /x/1 HTTP/1.1\r\n"
            "Host: www.example.org\r\n"
            "Connection: keep-alive\r\n"
            "Authorization: Basic Zm9vOmJhcg==\r\n\r\n"),
    };

    // Sever accepts the authorization.
    MockRead data_reads2[] = {
      MockRead("HTTP/1.0 200 OK\r\n"),
      MockRead("Content-Length: 100\r\n\r\n"),
      MockRead(SYNCHRONOUS, OK),
    };

    StaticSocketDataProvider data1(data_reads1, data_writes1);
    StaticSocketDataProvider data2(data_reads2, data_writes2);
    session_deps_.socket_factory->AddSocketDataProvider(&data1);
    session_deps_.socket_factory->AddSocketDataProvider(&data2);

    TestCompletionCallback callback1;

    int rv = trans.Start(&request, callback1.callback(), NetLogWithSource());
    EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

    rv = callback1.WaitForResult();
    EXPECT_THAT(rv, IsOk());

    EXPECT_TRUE(trans.IsReadyToRestartForAuth());
    TestCompletionCallback callback2;
    rv = trans.RestartWithAuth(AuthCredentials(), callback2.callback());
    EXPECT_THAT(rv, IsError(ERR_IO_PENDING));
    rv = callback2.WaitForResult();
    EXPECT_THAT(rv, IsOk());
    EXPECT_FALSE(trans.IsReadyToRestartForAuth());

    const HttpResponseInfo* response = trans.GetResponseInfo();
    ASSERT_TRUE(response);
    EXPECT_FALSE(response->auth_challenge.has_value());
    EXPECT_EQ(100, response->headers->GetContentLength());
  }

  // ------------------------------------------------------------------------

  // Transaction 5: request a URL in MyRealm, but the server rejects the
  // cached identity. Should invalidate and re-prompt.
  {
    HttpRequestInfo request;
    request.method = "GET";
    request.url = GURL("http://www.example.org/p/q/t");
    request.traffic_annotation =
        net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

    HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

    MockWrite data_writes1[] = {
        MockWrite(
            "GET /p/q/t HTTP/1.1\r\n"
            "Host: www.example.org\r\n"
            "Connection: keep-alive\r\n\r\n"),
    };

    MockRead data_reads1[] = {
      MockRead("HTTP/1.0 401 Unauthorized\r\n"),
      MockRead("WWW-Authenticate: Basic realm=\"MyRealm1\"\r\n"),
      MockRead("Content-Length: 10000\r\n\r\n"),
      MockRead(SYNCHRONOUS, ERR_FAILED),
    };

    // Resend with authorization from cache for MyRealm.
    MockWrite data_writes2[] = {
        MockWrite(
            "GET /p/q/t HTTP/1.1\r\n"
            "Host: www.example.org\r\n"
            "Connection: keep-alive\r\n"
            "Authorization: Basic Zm9vOmJhcg==\r\n\r\n"),
    };

    // Sever rejects the authorization.
    MockRead data_reads2[] = {
      MockRead("HTTP/1.0 401 Unauthorized\r\n"),
      MockRead("WWW-Authenticate: Basic realm=\"MyRealm1\"\r\n"),
      MockRead("Content-Length: 10000\r\n\r\n"),
      MockRead(SYNCHRONOUS, ERR_FAILED),
    };

    // At this point we should prompt for new credentials for MyRealm.
    // Restart with username=foo3, password=foo4.
    MockWrite data_writes3[] = {
        MockWrite(
            "GET /p/q/t HTTP/1.1\r\n"
            "Host: www.example.org\r\n"
            "Connection: keep-alive\r\n"
            "Authorization: Basic Zm9vMzpiYXIz\r\n\r\n"),
    };

    // Sever accepts the authorization.
    MockRead data_reads3[] = {
      MockRead("HTTP/1.0 200 OK\r\n"),
      MockRead("Content-Length: 100\r\n\r\n"),
      MockRead(SYNCHRONOUS, OK),
    };

    StaticSocketDataProvider data1(data_reads1, data_writes1);
    StaticSocketDataProvider data2(data_reads2, data_writes2);
    StaticSocketDataProvider data3(data_reads3, data_writes3);
    session_deps_.socket_factory->AddSocketDataProvider(&data1);
    session_deps_.socket_factory->AddSocketDataProvider(&data2);
    session_deps_.socket_factory->AddSocketDataProvider(&data3);

    TestCompletionCallback callback1;

    int rv = trans.Start(&request, callback1.callback(), NetLogWithSource());
    EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

    rv = callback1.WaitForResult();
    EXPECT_THAT(rv, IsOk());

    EXPECT_TRUE(trans.IsReadyToRestartForAuth());
    TestCompletionCallback callback2;
    rv = trans.RestartWithAuth(AuthCredentials(), callback2.callback());
    EXPECT_THAT(rv, IsError(ERR_IO_PENDING));
    rv = callback2.WaitForResult();
    EXPECT_THAT(rv, IsOk());
    EXPECT_FALSE(trans.IsReadyToRestartForAuth());

    const HttpResponseInfo* response = trans.GetResponseInfo();
    ASSERT_TRUE(response);
    EXPECT_TRUE(CheckBasicServerAuth(response->auth_challenge));

    TestCompletionCallback callback3;

    rv = trans.RestartWithAuth(AuthCredentials(kFoo3, kBar3),
                               callback3.callback());
    EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

    rv = callback3.WaitForResult();
    EXPECT_THAT(rv, IsOk());

    response = trans.GetResponseInfo();
    ASSERT_TRUE(response);
    EXPECT_FALSE(response->auth_challenge.has_value());
    EXPECT_EQ(100, response->headers->GetContentLength());
  }
}

// Tests that nonce count increments when multiple auth attempts
// are started with the same nonce.
TEST_F(HttpNetworkTransactionTest, DigestPreAuthNonceCount) {
  auto digest_factory = std::make_unique<HttpAuthHandlerDigest::Factory>();
  auto nonce_generator =
      std::make_unique<HttpAuthHandlerDigest::FixedNonceGenerator>(
          "0123456789abcdef");
  digest_factory->set_nonce_generator(std::move(nonce_generator));
  session_deps_.http_auth_handler_factory = std::move(digest_factory);
  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));

  // Transaction 1: authenticate (foo, bar) on MyRealm1
  {
    HttpRequestInfo request;
    request.method = "GET";
    request.url = GURL("http://www.example.org/x/y/z");
    request.traffic_annotation =
        net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

    HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

    MockWrite data_writes1[] = {
        MockWrite(
            "GET /x/y/z HTTP/1.1\r\n"
            "Host: www.example.org\r\n"
            "Connection: keep-alive\r\n\r\n"),
    };

    MockRead data_reads1[] = {
      MockRead("HTTP/1.0 401 Unauthorized\r\n"),
      MockRead("WWW-Authenticate: Digest realm=\"digestive\", nonce=\"OU812\", "
               "algorithm=MD5, qop=\"auth\"\r\n\r\n"),
      MockRead(SYNCHRONOUS, OK),
    };

    // Resend with authorization (username=foo, password=bar)
    MockWrite data_writes2[] = {
        MockWrite(
            "GET /x/y/z HTTP/1.1\r\n"
            "Host: www.example.org\r\n"
            "Connection: keep-alive\r\n"
            "Authorization: Digest username=\"foo\", realm=\"digestive\", "
            "nonce=\"OU812\", uri=\"/x/y/z\", algorithm=MD5, "
            "response=\"03ffbcd30add722589c1de345d7a927f\", qop=auth, "
            "nc=00000001, cnonce=\"0123456789abcdef\"\r\n\r\n"),
    };

    // Sever accepts the authorization.
    MockRead data_reads2[] = {
      MockRead("HTTP/1.0 200 OK\r\n"),
      MockRead(SYNCHRONOUS, OK),
    };

    StaticSocketDataProvider data1(data_reads1, data_writes1);
    StaticSocketDataProvider data2(data_reads2, data_writes2);
    session_deps_.socket_factory->AddSocketDataProvider(&data1);
    session_deps_.socket_factory->AddSocketDataProvider(&data2);

    TestCompletionCallback callback1;

    int rv = trans.Start(&request, callback1.callback(), NetLogWithSource());
    EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

    rv = callback1.WaitForResult();
    EXPECT_THAT(rv, IsOk());

    const HttpResponseInfo* response = trans.GetResponseInfo();
    ASSERT_TRUE(response);
    EXPECT_TRUE(CheckDigestServerAuth(response->auth_challenge));

    TestCompletionCallback callback2;

    rv = trans.RestartWithAuth(AuthCredentials(kFoo, kBar),
                               callback2.callback());
    EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

    rv = callback2.WaitForResult();
    EXPECT_THAT(rv, IsOk());

    response = trans.GetResponseInfo();
    ASSERT_TRUE(response);
    EXPECT_FALSE(response->auth_challenge.has_value());
  }

  // ------------------------------------------------------------------------

  // Transaction 2: Request another resource in digestive's protection space.
  // This will preemptively add an Authorization header which should have an
  // "nc" value of 2 (as compared to 1 in the first use.
  {
    HttpRequestInfo request;
    request.method = "GET";
    // Note that Transaction 1 was at /x/y/z, so this is in the same
    // protection space as digest.
    request.url = GURL("http://www.example.org/x/y/a/b");
    request.traffic_annotation =
        net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

    HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

    MockWrite data_writes1[] = {
        MockWrite(
            "GET /x/y/a/b HTTP/1.1\r\n"
            "Host: www.example.org\r\n"
            "Connection: keep-alive\r\n"
            "Authorization: Digest username=\"foo\", realm=\"digestive\", "
            "nonce=\"OU812\", uri=\"/x/y/a/b\", algorithm=MD5, "
            "response=\"d6f9a2c07d1c5df7b89379dca1269b35\", qop=auth, "
            "nc=00000002, cnonce=\"0123456789abcdef\"\r\n\r\n"),
    };

    // Sever accepts the authorization.
    MockRead data_reads1[] = {
      MockRead("HTTP/1.0 200 OK\r\n"),
      MockRead("Content-Length: 100\r\n\r\n"),
      MockRead(SYNCHRONOUS, OK),
    };

    StaticSocketDataProvider data1(data_reads1, data_writes1);
    session_deps_.socket_factory->AddSocketDataProvider(&data1);

    TestCompletionCallback callback1;

    int rv = trans.Start(&request, callback1.callback(), NetLogWithSource());
    EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

    rv = callback1.WaitForResult();
    EXPECT_THAT(rv, IsOk());

    const HttpResponseInfo* response = trans.GetResponseInfo();
    ASSERT_TRUE(response);
    EXPECT_FALSE(response->auth_challenge.has_value());
  }
}

// Test the ResetStateForRestart() private method.
TEST_F(HttpNetworkTransactionTest, ResetStateForRestart) {
  // Create a transaction (the dependencies aren't important).
  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));
  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

  // Setup some state (which we expect ResetStateForRestart() will clear).
  trans.read_buf_ = base::MakeRefCounted<IOBuffer>(15);
  trans.read_buf_len_ = 15;
  trans.request_headers_.SetHeader("Authorization", "NTLM");

  // Setup state in response_
  HttpResponseInfo* response = &trans.response_;
  response->auth_challenge = absl::nullopt;
  response->ssl_info.cert_status = static_cast<CertStatus>(-1);  // Nonsensical.
  response->response_time = base::Time::Now();
  response->was_cached = true;  // (Wouldn't ever actually be true...)

  // Cause the above state to be reset.
  trans.ResetStateForRestart();

  // Verify that the state that needed to be reset, has been reset.
  EXPECT_FALSE(trans.read_buf_);
  EXPECT_EQ(0, trans.read_buf_len_);
  EXPECT_TRUE(trans.request_headers_.IsEmpty());
  EXPECT_FALSE(response->auth_challenge.has_value());
  EXPECT_FALSE(response->headers);
  EXPECT_FALSE(response->was_cached);
  EXPECT_EQ(0U, response->ssl_info.cert_status);
}

// Test HTTPS connections to a site with a bad certificate
TEST_F(HttpNetworkTransactionTest, HTTPSBadCertificate) {
  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("https://www.example.org/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));
  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

  MockWrite data_writes[] = {
      MockWrite(
          "GET / HTTP/1.1\r\n"
          "Host: www.example.org\r\n"
          "Connection: keep-alive\r\n\r\n"),
  };

  MockRead data_reads[] = {
    MockRead("HTTP/1.0 200 OK\r\n"),
    MockRead("Content-Type: text/html; charset=iso-8859-1\r\n"),
    MockRead("Content-Length: 100\r\n\r\n"),
    MockRead(SYNCHRONOUS, OK),
  };

  StaticSocketDataProvider ssl_bad_certificate;
  StaticSocketDataProvider data(data_reads, data_writes);
  SSLSocketDataProvider ssl_bad(ASYNC, ERR_CERT_AUTHORITY_INVALID);
  SSLSocketDataProvider ssl(ASYNC, OK);

  session_deps_.socket_factory->AddSocketDataProvider(&ssl_bad_certificate);
  session_deps_.socket_factory->AddSocketDataProvider(&data);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl_bad);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl);

  TestCompletionCallback callback;

  int rv = trans.Start(&request, callback.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback.WaitForResult();
  EXPECT_THAT(rv, IsError(ERR_CERT_AUTHORITY_INVALID));

  rv = trans.RestartIgnoringLastError(callback.callback());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback.WaitForResult();
  EXPECT_THAT(rv, IsOk());

  const HttpResponseInfo* response = trans.GetResponseInfo();

  ASSERT_TRUE(response);
  EXPECT_EQ(100, response->headers->GetContentLength());
}

// Test HTTPS connections to a site with a bad certificate, going through a
// proxy
TEST_F(HttpNetworkTransactionTest, HTTPSBadCertificateViaProxy) {
  session_deps_.proxy_resolution_service =
      ConfiguredProxyResolutionService::CreateFixedForTest(
          "myproxy:70", TRAFFIC_ANNOTATION_FOR_TESTS);

  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("https://www.example.org/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  MockWrite proxy_writes[] = {
      MockWrite("CONNECT www.example.org:443 HTTP/1.1\r\n"
                "Host: www.example.org:443\r\n"
                "Proxy-Connection: keep-alive\r\n\r\n"),
  };

  MockRead proxy_reads[] = {
    MockRead("HTTP/1.0 200 Connected\r\n\r\n"),
    MockRead(SYNCHRONOUS, OK)
  };

  MockWrite data_writes[] = {
      MockWrite("CONNECT www.example.org:443 HTTP/1.1\r\n"
                "Host: www.example.org:443\r\n"
                "Proxy-Connection: keep-alive\r\n\r\n"),
      MockWrite("GET / HTTP/1.1\r\n"
                "Host: www.example.org\r\n"
                "Connection: keep-alive\r\n\r\n"),
  };

  MockRead data_reads[] = {
    MockRead("HTTP/1.0 200 Connected\r\n\r\n"),
    MockRead("HTTP/1.0 200 OK\r\n"),
    MockRead("Content-Type: text/html; charset=iso-8859-1\r\n"),
    MockRead("Content-Length: 100\r\n\r\n"),
    MockRead(SYNCHRONOUS, OK),
  };

  StaticSocketDataProvider ssl_bad_certificate(proxy_reads, proxy_writes);
  StaticSocketDataProvider data(data_reads, data_writes);
  SSLSocketDataProvider ssl_bad(ASYNC, ERR_CERT_AUTHORITY_INVALID);
  SSLSocketDataProvider ssl(ASYNC, OK);

  session_deps_.socket_factory->AddSocketDataProvider(&ssl_bad_certificate);
  session_deps_.socket_factory->AddSocketDataProvider(&data);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl_bad);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl);

  TestCompletionCallback callback;

  for (int i = 0; i < 2; i++) {
    session_deps_.socket_factory->ResetNextMockIndexes();

    std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));
    HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

    int rv = trans.Start(&request, callback.callback(), NetLogWithSource());
    EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

    rv = callback.WaitForResult();
    EXPECT_THAT(rv, IsError(ERR_CERT_AUTHORITY_INVALID));

    rv = trans.RestartIgnoringLastError(callback.callback());
    EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

    rv = callback.WaitForResult();
    EXPECT_THAT(rv, IsOk());

    const HttpResponseInfo* response = trans.GetResponseInfo();

    ASSERT_TRUE(response);
    EXPECT_EQ(100, response->headers->GetContentLength());
  }
}


// Test HTTPS connections to a site, going through an HTTPS proxy
TEST_F(HttpNetworkTransactionTest, HTTPSViaHttpsProxy) {
  session_deps_.proxy_resolution_service =
      ConfiguredProxyResolutionService::CreateFixedFromPacResultForTest(
          "HTTPS proxy:70", TRAFFIC_ANNOTATION_FOR_TESTS);
  session_deps_.net_log = net::NetLog::Get();

  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("https://www.example.org/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  MockWrite data_writes[] = {
      MockWrite("CONNECT www.example.org:443 HTTP/1.1\r\n"
                "Host: www.example.org:443\r\n"
                "Proxy-Connection: keep-alive\r\n\r\n"),
      MockWrite("GET / HTTP/1.1\r\n"
                "Host: www.example.org\r\n"
                "Connection: keep-alive\r\n\r\n"),
  };

  MockRead data_reads[] = {
    MockRead("HTTP/1.0 200 Connected\r\n\r\n"),
    MockRead("HTTP/1.1 200 OK\r\n"),
    MockRead("Content-Type: text/html; charset=iso-8859-1\r\n"),
    MockRead("Content-Length: 100\r\n\r\n"),
    MockRead(SYNCHRONOUS, OK),
  };

  StaticSocketDataProvider data(data_reads, data_writes);
  SSLSocketDataProvider proxy_ssl(ASYNC, OK);  // SSL to the proxy
  SSLSocketDataProvider tunnel_ssl(ASYNC, OK);  // SSL through the tunnel

  session_deps_.socket_factory->AddSocketDataProvider(&data);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&proxy_ssl);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&tunnel_ssl);

  TestCompletionCallback callback;

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));
  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

  int rv = trans.Start(&request, callback.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback.WaitForResult();
  EXPECT_THAT(rv, IsOk());
  const HttpResponseInfo* response = trans.GetResponseInfo();

  ASSERT_TRUE(response);

  EXPECT_TRUE(response->proxy_server.is_https());
  EXPECT_TRUE(response->headers->IsKeepAlive());
  EXPECT_EQ(200, response->headers->response_code());
  EXPECT_EQ(100, response->headers->GetContentLength());
  EXPECT_TRUE(HttpVersion(1, 1) == response->headers->GetHttpVersion());

  LoadTimingInfo load_timing_info;
  EXPECT_TRUE(trans.GetLoadTimingInfo(&load_timing_info));
  TestLoadTimingNotReusedWithPac(load_timing_info,
                                 CONNECT_TIMING_HAS_SSL_TIMES);
}

// Test that an HTTPS Proxy cannot redirect a CONNECT request for main frames.
TEST_F(HttpNetworkTransactionTest, RedirectOfHttpsConnectViaHttpsProxy) {
  session_deps_.proxy_resolution_service =
      ConfiguredProxyResolutionService::CreateFixedFromPacResultForTest(
          "HTTPS proxy:70", TRAFFIC_ANNOTATION_FOR_TESTS);
  session_deps_.net_log = net::NetLog::Get();

  const base::TimeDelta kTimeIncrement = base::Seconds(4);
  session_deps_.host_resolver->set_ondemand_mode(true);

  HttpRequestInfo request;
  request.load_flags = LOAD_MAIN_FRAME_DEPRECATED;
  request.method = "GET";
  request.url = GURL("https://www.example.org/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  MockWrite data_writes[] = {
      MockWrite(ASYNC, 0,
                "CONNECT www.example.org:443 HTTP/1.1\r\n"
                "Host: www.example.org:443\r\n"
                "Proxy-Connection: keep-alive\r\n\r\n"),
  };

  MockRead data_reads[] = {
      // Pause on first read.
      MockRead(ASYNC, ERR_IO_PENDING, 1),
      MockRead(ASYNC, 2, "HTTP/1.1 302 Redirect\r\n"),
      MockRead(ASYNC, 3, "Location: http://login.example.com/\r\n"),
      MockRead(ASYNC, 4, "Content-Length: 0\r\n\r\n"),
  };

  SequencedSocketData data(MockConnect(ASYNC, OK), data_reads, data_writes);
  SSLSocketDataProvider proxy_ssl(ASYNC, OK);  // SSL to the proxy

  session_deps_.socket_factory->AddSocketDataProvider(&data);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&proxy_ssl);

  TestCompletionCallback callback;

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));
  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

  int rv = trans.Start(&request, callback.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));
  EXPECT_TRUE(session_deps_.host_resolver->has_pending_requests());

  // Host resolution takes |kTimeIncrement|.
  FastForwardBy(kTimeIncrement);
  // Resolving the current request with |ResolveNow| will cause the pending
  // request to instantly complete, and the async connect will start as well.
  session_deps_.host_resolver->ResolveOnlyRequestNow();

  // Connecting takes |kTimeIncrement|.
  FastForwardBy(kTimeIncrement);
  data.RunUntilPaused();

  // The server takes |kTimeIncrement| to respond.
  FastForwardBy(kTimeIncrement);
  data.Resume();

  rv = callback.WaitForResult();
  EXPECT_THAT(rv, IsError(ERR_TUNNEL_CONNECTION_FAILED));
}

// Test that an HTTPS Proxy cannot redirect a CONNECT request for subresources.
TEST_F(HttpNetworkTransactionTest,
       RedirectOfHttpsConnectSubresourceViaHttpsProxy) {
  base::HistogramTester histograms;
  session_deps_.proxy_resolution_service =
      ConfiguredProxyResolutionService::CreateFixedFromPacResultForTest(
          "HTTPS proxy:70", TRAFFIC_ANNOTATION_FOR_TESTS);
  session_deps_.net_log = net::NetLog::Get();

  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("https://www.example.org/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  MockWrite data_writes[] = {
      MockWrite(ASYNC, 0,
                "CONNECT www.example.org:443 HTTP/1.1\r\n"
                "Host: www.example.org:443\r\n"
                "Proxy-Connection: keep-alive\r\n\r\n"),
  };

  MockRead data_reads[] = {
      MockRead(ASYNC, 1, "HTTP/1.1 302 Redirect\r\n"),
      MockRead(ASYNC, 2, "Location: http://login.example.com/\r\n"),
      MockRead(ASYNC, 3, "Content-Length: 0\r\n\r\n"),
  };

  SequencedSocketData data(MockConnect(ASYNC, OK), data_reads, data_writes);
  SSLSocketDataProvider proxy_ssl(ASYNC, OK);  // SSL to the proxy

  session_deps_.socket_factory->AddSocketDataProvider(&data);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&proxy_ssl);

  TestCompletionCallback callback;

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));
  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

  int rv = trans.Start(&request, callback.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback.WaitForResult();
  EXPECT_THAT(rv, IsError(ERR_TUNNEL_CONNECTION_FAILED));
}

// Test that an HTTPS Proxy which was auto-detected cannot redirect a CONNECT
// request for main frames.
TEST_F(HttpNetworkTransactionTest,
       RedirectOfHttpsConnectViaAutoDetectedHttpsProxy) {
  base::HistogramTester histograms;
  session_deps_.proxy_resolution_service = ConfiguredProxyResolutionService::
      CreateFixedFromAutoDetectedPacResultForTest("HTTPS proxy:70",
                                                  TRAFFIC_ANNOTATION_FOR_TESTS);
  session_deps_.net_log = net::NetLog::Get();

  HttpRequestInfo request;
  request.load_flags = LOAD_MAIN_FRAME_DEPRECATED;
  request.method = "GET";
  request.url = GURL("https://www.example.org/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  MockWrite data_writes[] = {
      MockWrite(ASYNC, 0,
                "CONNECT www.example.org:443 HTTP/1.1\r\n"
                "Host: www.example.org:443\r\n"
                "Proxy-Connection: keep-alive\r\n\r\n"),
  };

  MockRead data_reads[] = {
      MockRead(ASYNC, 1, "HTTP/1.1 302 Redirect\r\n"),
      MockRead(ASYNC, 2, "Location: http://login.example.com/\r\n"),
      MockRead(ASYNC, 3, "Content-Length: 0\r\n\r\n"),
  };

  SequencedSocketData data(MockConnect(ASYNC, OK), data_reads, data_writes);
  SSLSocketDataProvider proxy_ssl(ASYNC, OK);  // SSL to the proxy

  session_deps_.socket_factory->AddSocketDataProvider(&data);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&proxy_ssl);

  TestCompletionCallback callback;

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));
  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

  int rv = trans.Start(&request, callback.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback.WaitForResult();
  EXPECT_THAT(rv, IsError(ERR_TUNNEL_CONNECTION_FAILED));
}

// Tests that an HTTPS (SPDY) Proxy's cannot redirect a CONNECT request for main
// frames.
TEST_F(HttpNetworkTransactionTest, RedirectOfHttpsConnectViaSpdyProxy) {
  base::HistogramTester histograms;
  session_deps_.proxy_resolution_service =
      ConfiguredProxyResolutionService::CreateFixedForTest(
          "https://proxy:70", TRAFFIC_ANNOTATION_FOR_TESTS);
  session_deps_.net_log = net::NetLog::Get();

  const base::TimeDelta kTimeIncrement = base::Seconds(4);
  session_deps_.host_resolver->set_ondemand_mode(true);

  HttpRequestInfo request;
  request.method = "GET";
  request.load_flags = LOAD_MAIN_FRAME_DEPRECATED;
  request.url = GURL("https://www.example.org/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  spdy::SpdySerializedFrame conn(spdy_util_.ConstructSpdyConnect(
      nullptr, 0, 1, HttpProxyConnectJob::kH2QuicTunnelPriority,
      HostPortPair("www.example.org", 443)));
  spdy::SpdySerializedFrame goaway(
      spdy_util_.ConstructSpdyRstStream(1, spdy::ERROR_CODE_CANCEL));
  MockWrite data_writes[] = {
      CreateMockWrite(conn, 0, SYNCHRONOUS),
      CreateMockWrite(goaway, 3, SYNCHRONOUS),
  };

  static const char* const kExtraHeaders[] = {
    "location",
    "http://login.example.com/",
  };
  spdy::SpdySerializedFrame resp(spdy_util_.ConstructSpdyReplyError(
      "302", kExtraHeaders, std::size(kExtraHeaders) / 2, 1));
  MockRead data_reads[] = {
      // Pause on first read.
      MockRead(ASYNC, ERR_IO_PENDING, 1), CreateMockRead(resp, 2),
      MockRead(ASYNC, 0, 4),  // EOF
  };

  SequencedSocketData data(MockConnect(ASYNC, OK), data_reads, data_writes);
  SSLSocketDataProvider proxy_ssl(ASYNC, OK);  // SSL to the proxy
  proxy_ssl.next_proto = kProtoHTTP2;

  session_deps_.socket_factory->AddSocketDataProvider(&data);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&proxy_ssl);

  TestCompletionCallback callback;

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));
  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

  int rv = trans.Start(&request, callback.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));
  EXPECT_TRUE(session_deps_.host_resolver->has_pending_requests());

  // Host resolution takes |kTimeIncrement|.
  FastForwardBy(kTimeIncrement);
  // Resolving the current request with |ResolveNow| will cause the pending
  // request to instantly complete, and the async connect will start as well.
  session_deps_.host_resolver->ResolveOnlyRequestNow();

  // Connecting takes |kTimeIncrement|.
  FastForwardBy(kTimeIncrement);
  data.RunUntilPaused();

  FastForwardBy(kTimeIncrement);
  data.Resume();
  rv = callback.WaitForResult();
  EXPECT_THAT(rv, IsError(ERR_TUNNEL_CONNECTION_FAILED));
}

// Test that an HTTPS proxy's response to a CONNECT request is filtered.
TEST_F(HttpNetworkTransactionTest, ErrorResponseToHttpsConnectViaHttpsProxy) {
  session_deps_.proxy_resolution_service =
      ConfiguredProxyResolutionService::CreateFixedForTest(
          "https://proxy:70", TRAFFIC_ANNOTATION_FOR_TESTS);

  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("https://www.example.org/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  MockWrite data_writes[] = {
      MockWrite("CONNECT www.example.org:443 HTTP/1.1\r\n"
                "Host: www.example.org:443\r\n"
                "Proxy-Connection: keep-alive\r\n\r\n"),
  };

  MockRead data_reads[] = {
    MockRead("HTTP/1.1 404 Not Found\r\n"),
    MockRead("Content-Length: 23\r\n\r\n"),
    MockRead("The host does not exist"),
    MockRead(SYNCHRONOUS, OK),
  };

  StaticSocketDataProvider data(data_reads, data_writes);
  SSLSocketDataProvider proxy_ssl(ASYNC, OK);  // SSL to the proxy

  session_deps_.socket_factory->AddSocketDataProvider(&data);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&proxy_ssl);

  TestCompletionCallback callback;

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));
  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

  int rv = trans.Start(&request, callback.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback.WaitForResult();
  EXPECT_THAT(rv, IsError(ERR_TUNNEL_CONNECTION_FAILED));

  // TODO(juliatuttle): Anything else to check here?
}

// Test that a SPDY proxy's response to a CONNECT request is filtered.
TEST_F(HttpNetworkTransactionTest, ErrorResponseToHttpsConnectViaSpdyProxy) {
  session_deps_.proxy_resolution_service =
      ConfiguredProxyResolutionService::CreateFixedForTest(
          "https://proxy:70", TRAFFIC_ANNOTATION_FOR_TESTS);

  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("https://www.example.org/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  spdy::SpdySerializedFrame conn(spdy_util_.ConstructSpdyConnect(
      nullptr, 0, 1, HttpProxyConnectJob::kH2QuicTunnelPriority,
      HostPortPair("www.example.org", 443)));
  spdy::SpdySerializedFrame rst(
      spdy_util_.ConstructSpdyRstStream(1, spdy::ERROR_CODE_CANCEL));
  MockWrite data_writes[] = {
      CreateMockWrite(conn, 0), CreateMockWrite(rst, 3),
  };

  static const char* const kExtraHeaders[] = {
    "location",
    "http://login.example.com/",
  };
  spdy::SpdySerializedFrame resp(spdy_util_.ConstructSpdyReplyError(
      "404", kExtraHeaders, std::size(kExtraHeaders) / 2, 1));
  spdy::SpdySerializedFrame body(
      spdy_util_.ConstructSpdyDataFrame(1, "The host does not exist", true));
  MockRead data_reads[] = {
      CreateMockRead(resp, 1), CreateMockRead(body, 2),
      MockRead(ASYNC, 0, 4),  // EOF
  };

  SequencedSocketData data(data_reads, data_writes);
  SSLSocketDataProvider proxy_ssl(ASYNC, OK);  // SSL to the proxy
  proxy_ssl.next_proto = kProtoHTTP2;

  session_deps_.socket_factory->AddSocketDataProvider(&data);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&proxy_ssl);

  TestCompletionCallback callback;

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));
  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

  int rv = trans.Start(&request, callback.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback.WaitForResult();
  EXPECT_THAT(rv, IsError(ERR_TUNNEL_CONNECTION_FAILED));

  // TODO(juliatuttle): Anything else to check here?
}

// Test the request-challenge-retry sequence for basic auth, through
// a SPDY proxy over a single SPDY session.
TEST_F(HttpNetworkTransactionTest, BasicAuthSpdyProxy) {
  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("https://www.example.org/");
  // when the no authentication data flag is set.
  request.privacy_mode = PRIVACY_MODE_ENABLED;
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  // Configure against https proxy server "myproxy:70".
  session_deps_.proxy_resolution_service =
      ConfiguredProxyResolutionService::CreateFixedFromPacResultForTest(
          "HTTPS myproxy:70", TRAFFIC_ANNOTATION_FOR_TESTS);
  RecordingNetLogObserver net_log_observer;
  session_deps_.net_log = NetLog::Get();
  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));

  // Since we have proxy, should try to establish tunnel.
  spdy::SpdySerializedFrame req(spdy_util_.ConstructSpdyConnect(
      nullptr, 0, 1, HttpProxyConnectJob::kH2QuicTunnelPriority,
      HostPortPair("www.example.org", 443)));
  spdy::SpdySerializedFrame rst(
      spdy_util_.ConstructSpdyRstStream(1, spdy::ERROR_CODE_CANCEL));
  spdy_util_.UpdateWithStreamDestruction(1);

  // After calling trans.RestartWithAuth(), this is the request we should
  // be issuing -- the final header line contains the credentials.
  const char* const kAuthCredentials[] = {
      "proxy-authorization", "Basic Zm9vOmJhcg==",
  };
  spdy::SpdySerializedFrame connect2(spdy_util_.ConstructSpdyConnect(
      kAuthCredentials, std::size(kAuthCredentials) / 2, 3,
      HttpProxyConnectJob::kH2QuicTunnelPriority,
      HostPortPair("www.example.org", 443)));
  // fetch https://www.example.org/ via HTTP
  const char get[] =
      "GET / HTTP/1.1\r\n"
      "Host: www.example.org\r\n"
      "Connection: keep-alive\r\n\r\n";
  spdy::SpdySerializedFrame wrapped_get(
      spdy_util_.ConstructSpdyDataFrame(3, get, false));

  MockWrite spdy_writes[] = {
      CreateMockWrite(req, 0, ASYNC), CreateMockWrite(rst, 2, ASYNC),
      CreateMockWrite(connect2, 3), CreateMockWrite(wrapped_get, 5),
  };

  // The proxy responds to the connect with a 407, using a persistent
  // connection.
  const char kAuthStatus[] = "407";
  const char* const kAuthChallenge[] = {
    "proxy-authenticate", "Basic realm=\"MyRealm1\"",
  };
  spdy::SpdySerializedFrame conn_auth_resp(spdy_util_.ConstructSpdyReplyError(
      kAuthStatus, kAuthChallenge, std::size(kAuthChallenge) / 2, 1));

  spdy::SpdySerializedFrame conn_resp(
      spdy_util_.ConstructSpdyGetReply(nullptr, 0, 3));
  const char resp[] = "HTTP/1.1 200 OK\r\n"
      "Content-Length: 5\r\n\r\n";

  spdy::SpdySerializedFrame wrapped_get_resp(
      spdy_util_.ConstructSpdyDataFrame(3, resp, false));
  spdy::SpdySerializedFrame wrapped_body(
      spdy_util_.ConstructSpdyDataFrame(3, "hello", false));
  MockRead spdy_reads[] = {
      CreateMockRead(conn_auth_resp, 1, ASYNC),
      CreateMockRead(conn_resp, 4, ASYNC),
      CreateMockRead(wrapped_get_resp, 6, ASYNC),
      CreateMockRead(wrapped_body, 7, ASYNC),
      MockRead(ASYNC, OK, 8),  // EOF.  May or may not be read.
  };

  SequencedSocketData spdy_data(spdy_reads, spdy_writes);
  session_deps_.socket_factory->AddSocketDataProvider(&spdy_data);
  // Negotiate SPDY to the proxy
  SSLSocketDataProvider proxy(ASYNC, OK);
  proxy.next_proto = kProtoHTTP2;
  session_deps_.socket_factory->AddSSLSocketDataProvider(&proxy);
  // Vanilla SSL to the server
  SSLSocketDataProvider server(ASYNC, OK);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&server);

  TestCompletionCallback callback1;

  auto trans =
      std::make_unique<HttpNetworkTransaction>(DEFAULT_PRIORITY, session.get());

  int rv = trans->Start(&request, callback1.callback(),
                        NetLogWithSource::Make(NetLogSourceType::NONE));
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback1.WaitForResult();
  EXPECT_THAT(rv, IsOk());
  auto entries = net_log_observer.GetEntries();
  size_t pos = ExpectLogContainsSomewhere(
      entries, 0, NetLogEventType::HTTP_TRANSACTION_SEND_TUNNEL_HEADERS,
      NetLogEventPhase::NONE);
  ExpectLogContainsSomewhere(
      entries, pos,
      NetLogEventType::HTTP_TRANSACTION_READ_TUNNEL_RESPONSE_HEADERS,
      NetLogEventPhase::NONE);

  const HttpResponseInfo* response = trans->GetResponseInfo();
  ASSERT_TRUE(response);
  ASSERT_TRUE(response->headers);
  EXPECT_EQ(407, response->headers->response_code());
  EXPECT_TRUE(HttpVersion(1, 1) == response->headers->GetHttpVersion());
  EXPECT_TRUE(response->auth_challenge.has_value());
  EXPECT_TRUE(CheckBasicSecureProxyAuth(response->auth_challenge));

  TestCompletionCallback callback2;

  rv = trans->RestartWithAuth(AuthCredentials(kFoo, kBar),
                              callback2.callback());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback2.WaitForResult();
  EXPECT_THAT(rv, IsOk());

  response = trans->GetResponseInfo();
  ASSERT_TRUE(response);

  EXPECT_TRUE(response->headers->IsKeepAlive());
  EXPECT_EQ(200, response->headers->response_code());
  EXPECT_EQ(5, response->headers->GetContentLength());
  EXPECT_TRUE(HttpVersion(1, 1) == response->headers->GetHttpVersion());

  // The password prompt info should not be set.
  EXPECT_FALSE(response->auth_challenge.has_value());

  LoadTimingInfo load_timing_info;
  EXPECT_TRUE(trans->GetLoadTimingInfo(&load_timing_info));
  TestLoadTimingNotReusedWithPac(load_timing_info,
                                 CONNECT_TIMING_HAS_SSL_TIMES);

  trans.reset();
  session->CloseAllConnections(ERR_FAILED, "Very good reason");
}

// Test HTTPS connections to a site with a bad certificate, going through an
// HTTPS proxy
TEST_F(HttpNetworkTransactionTest, HTTPSBadCertificateViaHttpsProxy) {
  session_deps_.proxy_resolution_service =
      ConfiguredProxyResolutionService::CreateFixedForTest(
          "https://proxy:70", TRAFFIC_ANNOTATION_FOR_TESTS);

  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("https://www.example.org/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  // Attempt to fetch the URL from a server with a bad cert
  MockWrite bad_cert_writes[] = {
      MockWrite("CONNECT www.example.org:443 HTTP/1.1\r\n"
                "Host: www.example.org:443\r\n"
                "Proxy-Connection: keep-alive\r\n\r\n"),
  };

  MockRead bad_cert_reads[] = {
    MockRead("HTTP/1.0 200 Connected\r\n\r\n"),
    MockRead(SYNCHRONOUS, OK)
  };

  // Attempt to fetch the URL with a good cert
  MockWrite good_data_writes[] = {
      MockWrite("CONNECT www.example.org:443 HTTP/1.1\r\n"
                "Host: www.example.org:443\r\n"
                "Proxy-Connection: keep-alive\r\n\r\n"),
      MockWrite("GET / HTTP/1.1\r\n"
                "Host: www.example.org\r\n"
                "Connection: keep-alive\r\n\r\n"),
  };

  MockRead good_cert_reads[] = {
    MockRead("HTTP/1.0 200 Connected\r\n\r\n"),
    MockRead("HTTP/1.0 200 OK\r\n"),
    MockRead("Content-Type: text/html; charset=iso-8859-1\r\n"),
    MockRead("Content-Length: 100\r\n\r\n"),
    MockRead(SYNCHRONOUS, OK),
  };

  StaticSocketDataProvider ssl_bad_certificate(bad_cert_reads, bad_cert_writes);
  StaticSocketDataProvider data(good_cert_reads, good_data_writes);
  SSLSocketDataProvider ssl_bad(ASYNC, ERR_CERT_AUTHORITY_INVALID);
  SSLSocketDataProvider ssl(ASYNC, OK);

  // SSL to the proxy, then CONNECT request, then SSL with bad certificate
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl);
  session_deps_.socket_factory->AddSocketDataProvider(&ssl_bad_certificate);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl_bad);

  // SSL to the proxy, then CONNECT request, then valid SSL certificate
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl);
  session_deps_.socket_factory->AddSocketDataProvider(&data);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl);

  TestCompletionCallback callback;

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));
  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

  int rv = trans.Start(&request, callback.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback.WaitForResult();
  EXPECT_THAT(rv, IsError(ERR_CERT_AUTHORITY_INVALID));

  rv = trans.RestartIgnoringLastError(callback.callback());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback.WaitForResult();
  EXPECT_THAT(rv, IsOk());

  const HttpResponseInfo* response = trans.GetResponseInfo();

  ASSERT_TRUE(response);
  EXPECT_EQ(100, response->headers->GetContentLength());
}

TEST_F(HttpNetworkTransactionTest, BuildRequest_UserAgent) {
  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("http://www.example.org/");
  request.extra_headers.SetHeader(HttpRequestHeaders::kUserAgent,
                                  "Chromium Ultra Awesome X Edition");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));
  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

  MockWrite data_writes[] = {
      MockWrite(
          "GET / HTTP/1.1\r\n"
          "Host: www.example.org\r\n"
          "Connection: keep-alive\r\n"
          "User-Agent: Chromium Ultra Awesome X Edition\r\n\r\n"),
  };

  // Lastly, the server responds with the actual content.
  MockRead data_reads[] = {
    MockRead("HTTP/1.0 200 OK\r\n"),
    MockRead("Content-Type: text/html; charset=iso-8859-1\r\n"),
    MockRead("Content-Length: 100\r\n\r\n"),
    MockRead(SYNCHRONOUS, OK),
  };

  StaticSocketDataProvider data(data_reads, data_writes);
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  TestCompletionCallback callback;

  int rv = trans.Start(&request, callback.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback.WaitForResult();
  EXPECT_THAT(rv, IsOk());
}

TEST_F(HttpNetworkTransactionTest, BuildRequest_UserAgentOverTunnel) {
  // Test user agent values, used both for the request header of the original
  // request, and the value returned by the HttpUserAgentSettings. nullptr means
  // no request header / no HttpUserAgentSettings object.
  const char* kTestUserAgents[] = {nullptr, "", "Foopy"};

  for (const char* setting_user_agent : kTestUserAgents) {
    if (!setting_user_agent) {
      session_deps_.http_user_agent_settings.reset();
    } else {
      session_deps_.http_user_agent_settings =
          std::make_unique<StaticHttpUserAgentSettings>(
              std::string() /* accept-language */, setting_user_agent);
    }
    session_deps_.proxy_resolution_service =
        ConfiguredProxyResolutionService::CreateFixedForTest(
            "myproxy:70", TRAFFIC_ANNOTATION_FOR_TESTS);
    std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));
    for (const char* request_user_agent : kTestUserAgents) {
      HttpRequestInfo request;
      request.method = "GET";
      request.url = GURL("https://www.example.org/");
      if (request_user_agent) {
        request.extra_headers.SetHeader(HttpRequestHeaders::kUserAgent,
                                        request_user_agent);
      }
      request.traffic_annotation =
          net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

      HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

      std::string expected_request;
      if (!setting_user_agent || strlen(setting_user_agent) == 0) {
        expected_request =
            "CONNECT www.example.org:443 HTTP/1.1\r\n"
            "Host: www.example.org:443\r\n"
            "Proxy-Connection: keep-alive\r\n\r\n";
      } else {
        expected_request = base::StringPrintf(
            "CONNECT www.example.org:443 HTTP/1.1\r\n"
            "Host: www.example.org:443\r\n"
            "Proxy-Connection: keep-alive\r\n"
            "User-Agent: %s\r\n\r\n",
            setting_user_agent);
      }
      MockWrite data_writes[] = {
          MockWrite(expected_request.c_str()),
      };
      MockRead data_reads[] = {
          // Return an error, so the transaction stops here (this test isn't
          // interested in the rest).
          MockRead("HTTP/1.1 407 Proxy Authentication Required\r\n"),
          MockRead("Proxy-Authenticate: Basic realm=\"MyRealm1\"\r\n"),
          MockRead("Proxy-Connection: close\r\n\r\n"),
      };

      StaticSocketDataProvider data(data_reads, data_writes);
      session_deps_.socket_factory->AddSocketDataProvider(&data);

      TestCompletionCallback callback;

      int rv = trans.Start(&request, callback.callback(), NetLogWithSource());
      EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

      rv = callback.WaitForResult();
      EXPECT_THAT(rv, IsOk());
    }
  }
}

TEST_F(HttpNetworkTransactionTest, BuildRequest_Referer) {
  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("http://www.example.org/");
  request.extra_headers.SetHeader(HttpRequestHeaders::kReferer,
                                  "http://the.previous.site.com/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));
  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

  MockWrite data_writes[] = {
      MockWrite(
          "GET / HTTP/1.1\r\n"
          "Host: www.example.org\r\n"
          "Connection: keep-alive\r\n"
          "Referer: http://the.previous.site.com/\r\n\r\n"),
  };

  // Lastly, the server responds with the actual content.
  MockRead data_reads[] = {
    MockRead("HTTP/1.0 200 OK\r\n"),
    MockRead("Content-Type: text/html; charset=iso-8859-1\r\n"),
    MockRead("Content-Length: 100\r\n\r\n"),
    MockRead(SYNCHRONOUS, OK),
  };

  StaticSocketDataProvider data(data_reads, data_writes);
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  TestCompletionCallback callback;

  int rv = trans.Start(&request, callback.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback.WaitForResult();
  EXPECT_THAT(rv, IsOk());
}

TEST_F(HttpNetworkTransactionTest, BuildRequest_PostContentLengthZero) {
  HttpRequestInfo request;
  request.method = "POST";
  request.url = GURL("http://www.example.org/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));
  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

  MockWrite data_writes[] = {
      MockWrite(
          "POST / HTTP/1.1\r\n"
          "Host: www.example.org\r\n"
          "Connection: keep-alive\r\n"
          "Content-Length: 0\r\n\r\n"),
  };

  // Lastly, the server responds with the actual content.
  MockRead data_reads[] = {
    MockRead("HTTP/1.0 200 OK\r\n"),
    MockRead("Content-Type: text/html; charset=iso-8859-1\r\n"),
    MockRead("Content-Length: 100\r\n\r\n"),
    MockRead(SYNCHRONOUS, OK),
  };

  StaticSocketDataProvider data(data_reads, data_writes);
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  TestCompletionCallback callback;

  int rv = trans.Start(&request, callback.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback.WaitForResult();
  EXPECT_THAT(rv, IsOk());
}

TEST_F(HttpNetworkTransactionTest, BuildRequest_PutContentLengthZero) {
  HttpRequestInfo request;
  request.method = "PUT";
  request.url = GURL("http://www.example.org/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));
  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

  MockWrite data_writes[] = {
      MockWrite(
          "PUT / HTTP/1.1\r\n"
          "Host: www.example.org\r\n"
          "Connection: keep-alive\r\n"
          "Content-Length: 0\r\n\r\n"),
  };

  // Lastly, the server responds with the actual content.
  MockRead data_reads[] = {
    MockRead("HTTP/1.0 200 OK\r\n"),
    MockRead("Content-Type: text/html; charset=iso-8859-1\r\n"),
    MockRead("Content-Length: 100\r\n\r\n"),
    MockRead(SYNCHRONOUS, OK),
  };

  StaticSocketDataProvider data(data_reads, data_writes);
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  TestCompletionCallback callback;

  int rv = trans.Start(&request, callback.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback.WaitForResult();
  EXPECT_THAT(rv, IsOk());
}

TEST_F(HttpNetworkTransactionTest, BuildRequest_HeadContentLengthZero) {
  HttpRequestInfo request;
  request.method = "HEAD";
  request.url = GURL("http://www.example.org/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));
  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

  MockWrite data_writes[] = {
      MockWrite("HEAD / HTTP/1.1\r\n"
                "Host: www.example.org\r\n"
                "Connection: keep-alive\r\n\r\n"),
  };

  // Lastly, the server responds with the actual content.
  MockRead data_reads[] = {
    MockRead("HTTP/1.0 200 OK\r\n"),
    MockRead("Content-Type: text/html; charset=iso-8859-1\r\n"),
    MockRead("Content-Length: 100\r\n\r\n"),
    MockRead(SYNCHRONOUS, OK),
  };

  StaticSocketDataProvider data(data_reads, data_writes);
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  TestCompletionCallback callback;

  int rv = trans.Start(&request, callback.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback.WaitForResult();
  EXPECT_THAT(rv, IsOk());
}

TEST_F(HttpNetworkTransactionTest, BuildRequest_CacheControlNoCache) {
  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("http://www.example.org/");
  request.load_flags = LOAD_BYPASS_CACHE;
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));
  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

  MockWrite data_writes[] = {
      MockWrite(
          "GET / HTTP/1.1\r\n"
          "Host: www.example.org\r\n"
          "Connection: keep-alive\r\n"
          "Pragma: no-cache\r\n"
          "Cache-Control: no-cache\r\n\r\n"),
  };

  // Lastly, the server responds with the actual content.
  MockRead data_reads[] = {
    MockRead("HTTP/1.0 200 OK\r\n"),
    MockRead("Content-Type: text/html; charset=iso-8859-1\r\n"),
    MockRead("Content-Length: 100\r\n\r\n"),
    MockRead(SYNCHRONOUS, OK),
  };

  StaticSocketDataProvider data(data_reads, data_writes);
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  TestCompletionCallback callback;

  int rv = trans.Start(&request, callback.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback.WaitForResult();
  EXPECT_THAT(rv, IsOk());
}

TEST_F(HttpNetworkTransactionTest, BuildRequest_CacheControlValidateCache) {
  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("http://www.example.org/");
  request.load_flags = LOAD_VALIDATE_CACHE;
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));
  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

  MockWrite data_writes[] = {
      MockWrite(
          "GET / HTTP/1.1\r\n"
          "Host: www.example.org\r\n"
          "Connection: keep-alive\r\n"
          "Cache-Control: max-age=0\r\n\r\n"),
  };

  // Lastly, the server responds with the actual content.
  MockRead data_reads[] = {
    MockRead("HTTP/1.0 200 OK\r\n"),
    MockRead("Content-Type: text/html; charset=iso-8859-1\r\n"),
    MockRead("Content-Length: 100\r\n\r\n"),
    MockRead(SYNCHRONOUS, OK),
  };

  StaticSocketDataProvider data(data_reads, data_writes);
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  TestCompletionCallback callback;

  int rv = trans.Start(&request, callback.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback.WaitForResult();
  EXPECT_THAT(rv, IsOk());
}

TEST_F(HttpNetworkTransactionTest, BuildRequest_ExtraHeaders) {
  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("http://www.example.org/");
  request.extra_headers.SetHeader("FooHeader", "Bar");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));
  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

  MockWrite data_writes[] = {
      MockWrite(
          "GET / HTTP/1.1\r\n"
          "Host: www.example.org\r\n"
          "Connection: keep-alive\r\n"
          "FooHeader: Bar\r\n\r\n"),
  };

  // Lastly, the server responds with the actual content.
  MockRead data_reads[] = {
    MockRead("HTTP/1.0 200 OK\r\n"),
    MockRead("Content-Type: text/html; charset=iso-8859-1\r\n"),
    MockRead("Content-Length: 100\r\n\r\n"),
    MockRead(SYNCHRONOUS, OK),
  };

  StaticSocketDataProvider data(data_reads, data_writes);
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  TestCompletionCallback callback;

  int rv = trans.Start(&request, callback.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback.WaitForResult();
  EXPECT_THAT(rv, IsOk());
}

TEST_F(HttpNetworkTransactionTest, BuildRequest_ExtraHeadersStripped) {
  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("http://www.example.org/");
  request.extra_headers.SetHeader("referer", "www.foo.com");
  request.extra_headers.SetHeader("hEllo", "Kitty");
  request.extra_headers.SetHeader("FoO", "bar");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));
  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

  MockWrite data_writes[] = {
      MockWrite(
          "GET / HTTP/1.1\r\n"
          "Host: www.example.org\r\n"
          "Connection: keep-alive\r\n"
          "referer: www.foo.com\r\n"
          "hEllo: Kitty\r\n"
          "FoO: bar\r\n\r\n"),
  };

  // Lastly, the server responds with the actual content.
  MockRead data_reads[] = {
    MockRead("HTTP/1.0 200 OK\r\n"),
    MockRead("Content-Type: text/html; charset=iso-8859-1\r\n"),
    MockRead("Content-Length: 100\r\n\r\n"),
    MockRead(SYNCHRONOUS, OK),
  };

  StaticSocketDataProvider data(data_reads, data_writes);
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  TestCompletionCallback callback;

  int rv = trans.Start(&request, callback.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback.WaitForResult();
  EXPECT_THAT(rv, IsOk());
}

TEST_F(HttpNetworkTransactionTest, SOCKS4_HTTP_GET) {
  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("http://www.example.org/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  session_deps_.proxy_resolution_service =
      ConfiguredProxyResolutionService::CreateFixedFromPacResultForTest(
          "SOCKS myproxy:1080", TRAFFIC_ANNOTATION_FOR_TESTS);
  session_deps_.net_log = net::NetLog::Get();

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));
  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

  char write_buffer[] = { 0x04, 0x01, 0x00, 0x50, 127, 0, 0, 1, 0 };
  char read_buffer[] = { 0x00, 0x5A, 0x00, 0x00, 0, 0, 0, 0 };

  MockWrite data_writes[] = {
      MockWrite(ASYNC, write_buffer, std::size(write_buffer)),
      MockWrite("GET / HTTP/1.1\r\n"
                "Host: www.example.org\r\n"
                "Connection: keep-alive\r\n\r\n")};

  MockRead data_reads[] = {
      MockRead(ASYNC, read_buffer, std::size(read_buffer)),
      MockRead("HTTP/1.0 200 OK\r\n"),
      MockRead("Content-Type: text/html; charset=iso-8859-1\r\n\r\n"),
      MockRead("Payload"), MockRead(SYNCHRONOUS, OK)};

  StaticSocketDataProvider data(data_reads, data_writes);
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  TestCompletionCallback callback;

  int rv = trans.Start(&request, callback.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback.WaitForResult();
  EXPECT_THAT(rv, IsOk());

  const HttpResponseInfo* response = trans.GetResponseInfo();
  ASSERT_TRUE(response);

  EXPECT_EQ(ProxyServer::SCHEME_SOCKS4, response->proxy_server.scheme());
  LoadTimingInfo load_timing_info;
  EXPECT_TRUE(trans.GetLoadTimingInfo(&load_timing_info));
  TestLoadTimingNotReusedWithPac(load_timing_info,
                                 CONNECT_TIMING_HAS_CONNECT_TIMES_ONLY);

  std::string response_text;
  rv = ReadTransaction(&trans, &response_text);
  EXPECT_THAT(rv, IsOk());
  EXPECT_EQ("Payload", response_text);
}

TEST_F(HttpNetworkTransactionTest, SOCKS4_SSL_GET) {
  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("https://www.example.org/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  session_deps_.proxy_resolution_service =
      ConfiguredProxyResolutionService::CreateFixedFromPacResultForTest(
          "SOCKS myproxy:1080", TRAFFIC_ANNOTATION_FOR_TESTS);
  session_deps_.net_log = net::NetLog::Get();

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));
  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

  unsigned char write_buffer[] = { 0x04, 0x01, 0x01, 0xBB, 127, 0, 0, 1, 0 };
  unsigned char read_buffer[] = { 0x00, 0x5A, 0x00, 0x00, 0, 0, 0, 0 };

  MockWrite data_writes[] = {
      MockWrite(ASYNC, reinterpret_cast<char*>(write_buffer),
                std::size(write_buffer)),
      MockWrite("GET / HTTP/1.1\r\n"
                "Host: www.example.org\r\n"
                "Connection: keep-alive\r\n\r\n")};

  MockRead data_reads[] = {
      MockRead(ASYNC, reinterpret_cast<char*>(read_buffer),
               std::size(read_buffer)),
      MockRead("HTTP/1.0 200 OK\r\n"),
      MockRead("Content-Type: text/html; charset=iso-8859-1\r\n\r\n"),
      MockRead("Payload"), MockRead(SYNCHRONOUS, OK)};

  StaticSocketDataProvider data(data_reads, data_writes);
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  SSLSocketDataProvider ssl(ASYNC, OK);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl);

  TestCompletionCallback callback;

  int rv = trans.Start(&request, callback.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback.WaitForResult();
  EXPECT_THAT(rv, IsOk());

  LoadTimingInfo load_timing_info;
  EXPECT_TRUE(trans.GetLoadTimingInfo(&load_timing_info));
  TestLoadTimingNotReusedWithPac(load_timing_info,
                                 CONNECT_TIMING_HAS_SSL_TIMES);

  const HttpResponseInfo* response = trans.GetResponseInfo();
  ASSERT_TRUE(response);
  EXPECT_EQ(ProxyServer::SCHEME_SOCKS4, response->proxy_server.scheme());

  std::string response_text;
  rv = ReadTransaction(&trans, &response_text);
  EXPECT_THAT(rv, IsOk());
  EXPECT_EQ("Payload", response_text);
}

TEST_F(HttpNetworkTransactionTest, SOCKS4_HTTP_GET_no_PAC) {
  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("http://www.example.org/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  session_deps_.proxy_resolution_service =
      ConfiguredProxyResolutionService::CreateFixedForTest(
          "socks4://myproxy:1080", TRAFFIC_ANNOTATION_FOR_TESTS);
  session_deps_.net_log = net::NetLog::Get();

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));
  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

  char write_buffer[] = { 0x04, 0x01, 0x00, 0x50, 127, 0, 0, 1, 0 };
  char read_buffer[] = { 0x00, 0x5A, 0x00, 0x00, 0, 0, 0, 0 };

  MockWrite data_writes[] = {
      MockWrite(ASYNC, write_buffer, std::size(write_buffer)),
      MockWrite("GET / HTTP/1.1\r\n"
                "Host: www.example.org\r\n"
                "Connection: keep-alive\r\n\r\n")};

  MockRead data_reads[] = {
      MockRead(ASYNC, read_buffer, std::size(read_buffer)),
      MockRead("HTTP/1.0 200 OK\r\n"),
      MockRead("Content-Type: text/html; charset=iso-8859-1\r\n\r\n"),
      MockRead("Payload"), MockRead(SYNCHRONOUS, OK)};

  StaticSocketDataProvider data(data_reads, data_writes);
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  TestCompletionCallback callback;

  int rv = trans.Start(&request, callback.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback.WaitForResult();
  EXPECT_THAT(rv, IsOk());

  const HttpResponseInfo* response = trans.GetResponseInfo();
  ASSERT_TRUE(response);

  LoadTimingInfo load_timing_info;
  EXPECT_TRUE(trans.GetLoadTimingInfo(&load_timing_info));
  TestLoadTimingNotReused(load_timing_info,
                          CONNECT_TIMING_HAS_CONNECT_TIMES_ONLY);

  std::string response_text;
  rv = ReadTransaction(&trans, &response_text);
  EXPECT_THAT(rv, IsOk());
  EXPECT_EQ("Payload", response_text);
}

TEST_F(HttpNetworkTransactionTest, SOCKS5_HTTP_GET) {
  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("http://www.example.org/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  session_deps_.proxy_resolution_service =
      ConfiguredProxyResolutionService::CreateFixedFromPacResultForTest(
          "SOCKS5 myproxy:1080", TRAFFIC_ANNOTATION_FOR_TESTS);
  session_deps_.net_log = net::NetLog::Get();

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));
  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

  const char kSOCKS5ExampleOkRequest[] = {
      0x05,  // Version
      0x01,  // Command (CONNECT)
      0x00,  // Reserved.
      0x03,  // Address type (DOMAINNAME).
      0x0F,  // Length of domain (15)
      'w',  'w', 'w', '.', 'e',  'x',
      'a',  'm', 'p', 'l', 'e',         // Domain string
      '.',  'o', 'r', 'g', 0x00, 0x50,  // 16-bit port (80)
  };

  MockWrite data_writes[] = {
      MockWrite(ASYNC, kSOCKS5GreetRequest, kSOCKS5GreetRequestLength),
      MockWrite(ASYNC, kSOCKS5ExampleOkRequest,
                std::size(kSOCKS5ExampleOkRequest)),
      MockWrite("GET / HTTP/1.1\r\n"
                "Host: www.example.org\r\n"
                "Connection: keep-alive\r\n\r\n")};

  MockRead data_reads[] = {
      MockRead(ASYNC, kSOCKS5GreetResponse, kSOCKS5GreetResponseLength),
      MockRead(ASYNC, kSOCKS5OkResponse, kSOCKS5OkResponseLength),
      MockRead("HTTP/1.0 200 OK\r\n"),
      MockRead("Content-Type: text/html; charset=iso-8859-1\r\n\r\n"),
      MockRead("Payload"),
      MockRead(SYNCHRONOUS, OK)};

  StaticSocketDataProvider data(data_reads, data_writes);
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  TestCompletionCallback callback;

  int rv = trans.Start(&request, callback.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback.WaitForResult();
  EXPECT_THAT(rv, IsOk());

  const HttpResponseInfo* response = trans.GetResponseInfo();
  ASSERT_TRUE(response);
  EXPECT_EQ(ProxyServer::SCHEME_SOCKS5, response->proxy_server.scheme());

  LoadTimingInfo load_timing_info;
  EXPECT_TRUE(trans.GetLoadTimingInfo(&load_timing_info));
  TestLoadTimingNotReusedWithPac(load_timing_info,
                                 CONNECT_TIMING_HAS_CONNECT_TIMES_ONLY);

  std::string response_text;
  rv = ReadTransaction(&trans, &response_text);
  EXPECT_THAT(rv, IsOk());
  EXPECT_EQ("Payload", response_text);
}

TEST_F(HttpNetworkTransactionTest, SOCKS5_SSL_GET) {
  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("https://www.example.org/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  session_deps_.proxy_resolution_service =
      ConfiguredProxyResolutionService::CreateFixedFromPacResultForTest(
          "SOCKS5 myproxy:1080", TRAFFIC_ANNOTATION_FOR_TESTS);
  session_deps_.net_log = net::NetLog::Get();

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));
  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

  const unsigned char kSOCKS5ExampleOkRequest[] = {
      0x05,  // Version
      0x01,  // Command (CONNECT)
      0x00,  // Reserved.
      0x03,  // Address type (DOMAINNAME).
      0x0F,  // Length of domain (15)
      'w',  'w', 'w', '.', 'e',  'x',
      'a',  'm', 'p', 'l', 'e',         // Domain string
      '.',  'o', 'r', 'g', 0x01, 0xBB,  // 16-bit port (443)
  };

  const char kSOCKS5SslOkResponse[] = {0x05, 0x00, 0x00, 0x01, 0,
                                       0,    0,    0,    0x00, 0x00};

  MockWrite data_writes[] = {
      MockWrite(ASYNC, kSOCKS5GreetRequest, kSOCKS5GreetRequestLength),
      MockWrite(ASYNC, reinterpret_cast<const char*>(kSOCKS5ExampleOkRequest),
                std::size(kSOCKS5ExampleOkRequest)),
      MockWrite("GET / HTTP/1.1\r\n"
                "Host: www.example.org\r\n"
                "Connection: keep-alive\r\n\r\n")};

  MockRead data_reads[] = {
      MockRead(ASYNC, kSOCKS5GreetResponse, kSOCKS5GreetResponseLength),
      MockRead(ASYNC, kSOCKS5SslOkResponse, std::size(kSOCKS5SslOkResponse)),
      MockRead("HTTP/1.0 200 OK\r\n"),
      MockRead("Content-Type: text/html; charset=iso-8859-1\r\n\r\n"),
      MockRead("Payload"),
      MockRead(SYNCHRONOUS, OK)};

  StaticSocketDataProvider data(data_reads, data_writes);
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  SSLSocketDataProvider ssl(ASYNC, OK);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl);

  TestCompletionCallback callback;

  int rv = trans.Start(&request, callback.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback.WaitForResult();
  EXPECT_THAT(rv, IsOk());

  const HttpResponseInfo* response = trans.GetResponseInfo();
  ASSERT_TRUE(response);
  EXPECT_EQ(ProxyServer::SCHEME_SOCKS5, response->proxy_server.scheme());

  LoadTimingInfo load_timing_info;
  EXPECT_TRUE(trans.GetLoadTimingInfo(&load_timing_info));
  TestLoadTimingNotReusedWithPac(load_timing_info,
                                 CONNECT_TIMING_HAS_SSL_TIMES);

  std::string response_text;
  rv = ReadTransaction(&trans, &response_text);
  EXPECT_THAT(rv, IsOk());
  EXPECT_EQ("Payload", response_text);
}

namespace {

// Tests that for connection endpoints the group ids are correctly set.

struct GroupIdTest {
  std::string proxy_server;
  std::string url;
  ClientSocketPool::GroupId expected_group_id;
  bool ssl;
};

std::unique_ptr<HttpNetworkSession> SetupSessionForGroupIdTests(
    SpdySessionDependencies* session_deps_) {
  std::unique_ptr<HttpNetworkSession> session(CreateSession(session_deps_));

  HttpServerProperties* http_server_properties =
      session->http_server_properties();
  AlternativeService alternative_service(kProtoHTTP2, "", 444);
  base::Time expiration = base::Time::Now() + base::Days(1);
  http_server_properties->SetHttp2AlternativeService(
      url::SchemeHostPort("https", "host.with.alternate", 443),
      NetworkIsolationKey(), alternative_service, expiration);

  return session;
}

int GroupIdTransactionHelper(const std::string& url,
                             HttpNetworkSession* session) {
  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL(url);
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session);

  TestCompletionCallback callback;

  // We do not complete this request, the dtor will clean the transaction up.
  return trans.Start(&request, callback.callback(), NetLogWithSource());
}

}  // namespace

TEST_F(HttpNetworkTransactionTest, GroupIdForDirectConnections) {
  const GroupIdTest tests[] = {
      {
          "",  // unused
          "http://www.example.org/direct",
          ClientSocketPool::GroupId(
              url::SchemeHostPort(url::kHttpScheme, "www.example.org", 80),
              PrivacyMode::PRIVACY_MODE_DISABLED, NetworkIsolationKey(),
              SecureDnsPolicy::kAllow),
          false,
      },
      {
          "",  // unused
          "http://[2001:1418:13:1::25]/direct",
          ClientSocketPool::GroupId(
              url::SchemeHostPort(url::kHttpScheme, "[2001:1418:13:1::25]", 80),
              PrivacyMode::PRIVACY_MODE_DISABLED, NetworkIsolationKey(),
              SecureDnsPolicy::kAllow),
          false,
      },

      // SSL Tests
      {
          "",  // unused
          "https://www.example.org/direct_ssl",
          ClientSocketPool::GroupId(
              url::SchemeHostPort(url::kHttpsScheme, "www.example.org", 443),
              PrivacyMode::PRIVACY_MODE_DISABLED, NetworkIsolationKey(),
              SecureDnsPolicy::kAllow),
          true,
      },
      {
          "",  // unused
          "https://[2001:1418:13:1::25]/direct",
          ClientSocketPool::GroupId(
              url::SchemeHostPort(url::kHttpsScheme, "[2001:1418:13:1::25]",
                                  443),
              PrivacyMode::PRIVACY_MODE_DISABLED, NetworkIsolationKey(),
              SecureDnsPolicy::kAllow),
          true,
      },
      {
          "",  // unused
          "https://host.with.alternate/direct",
          ClientSocketPool::GroupId(
              url::SchemeHostPort(url::kHttpsScheme, "host.with.alternate",
                                  443),
              PrivacyMode::PRIVACY_MODE_DISABLED, NetworkIsolationKey(),
              SecureDnsPolicy::kAllow),
          true,
      },
  };

  for (const auto& test : tests) {
    session_deps_.proxy_resolution_service =
        ConfiguredProxyResolutionService::CreateFixedForTest(
            test.proxy_server, TRAFFIC_ANNOTATION_FOR_TESTS);
    std::unique_ptr<HttpNetworkSession> session(
        SetupSessionForGroupIdTests(&session_deps_));

    HttpNetworkSessionPeer peer(session.get());
    auto transport_conn_pool =
        std::make_unique<CaptureGroupIdTransportSocketPool>(
            &dummy_connect_job_params_);
    auto* transport_conn_pool_ptr = transport_conn_pool.get();
    auto mock_pool_manager = std::make_unique<MockClientSocketPoolManager>();
    mock_pool_manager->SetSocketPool(ProxyServer::Direct(),
                                     std::move(transport_conn_pool));
    peer.SetClientSocketPoolManager(std::move(mock_pool_manager));

    EXPECT_EQ(ERR_IO_PENDING,
              GroupIdTransactionHelper(test.url, session.get()));
    EXPECT_EQ(test.expected_group_id,
              transport_conn_pool_ptr->last_group_id_received());
    EXPECT_TRUE(transport_conn_pool_ptr->socket_requested());
  }
}

TEST_F(HttpNetworkTransactionTest, GroupIdForHTTPProxyConnections) {
  const GroupIdTest tests[] = {
      {
          "http_proxy",
          "http://www.example.org/http_proxy_normal",
          ClientSocketPool::GroupId(
              url::SchemeHostPort(url::kHttpScheme, "www.example.org", 80),
              PrivacyMode::PRIVACY_MODE_DISABLED, NetworkIsolationKey(),
              SecureDnsPolicy::kAllow),
          false,
      },

      // SSL Tests
      {
          "http_proxy",
          "https://www.example.org/http_connect_ssl",
          ClientSocketPool::GroupId(
              url::SchemeHostPort(url::kHttpsScheme, "www.example.org", 443),
              PrivacyMode::PRIVACY_MODE_DISABLED, NetworkIsolationKey(),
              SecureDnsPolicy::kAllow),
          true,
      },

      {
          "http_proxy",
          "https://host.with.alternate/direct",
          ClientSocketPool::GroupId(
              url::SchemeHostPort(url::kHttpsScheme, "host.with.alternate",
                                  443),
              PrivacyMode::PRIVACY_MODE_DISABLED, NetworkIsolationKey(),
              SecureDnsPolicy::kAllow),
          true,
      },
  };

  for (const auto& test : tests) {
    session_deps_.proxy_resolution_service =
        ConfiguredProxyResolutionService::CreateFixedForTest(
            test.proxy_server, TRAFFIC_ANNOTATION_FOR_TESTS);
    std::unique_ptr<HttpNetworkSession> session(
        SetupSessionForGroupIdTests(&session_deps_));

    HttpNetworkSessionPeer peer(session.get());

    ProxyServer proxy_server(ProxyServer::SCHEME_HTTP,
                             HostPortPair("http_proxy", 80));
    auto http_proxy_pool = std::make_unique<CaptureGroupIdTransportSocketPool>(
        &dummy_connect_job_params_);
    auto* http_proxy_pool_ptr = http_proxy_pool.get();
    auto mock_pool_manager = std::make_unique<MockClientSocketPoolManager>();
    mock_pool_manager->SetSocketPool(proxy_server, std::move(http_proxy_pool));
    peer.SetClientSocketPoolManager(std::move(mock_pool_manager));

    EXPECT_EQ(ERR_IO_PENDING,
              GroupIdTransactionHelper(test.url, session.get()));
    EXPECT_EQ(test.expected_group_id,
              http_proxy_pool_ptr->last_group_id_received());
  }
}

TEST_F(HttpNetworkTransactionTest, GroupIdForSOCKSConnections) {
  const GroupIdTest tests[] = {
      {
          "socks4://socks_proxy:1080",
          "http://www.example.org/socks4_direct",
          ClientSocketPool::GroupId(
              url::SchemeHostPort(url::kHttpScheme, "www.example.org", 80),
              PrivacyMode::PRIVACY_MODE_DISABLED, NetworkIsolationKey(),
              SecureDnsPolicy::kAllow),
          false,
      },
      {
          "socks5://socks_proxy:1080",
          "http://www.example.org/socks5_direct",
          ClientSocketPool::GroupId(
              url::SchemeHostPort(url::kHttpScheme, "www.example.org", 80),
              PrivacyMode::PRIVACY_MODE_DISABLED, NetworkIsolationKey(),
              SecureDnsPolicy::kAllow),
          false,
      },

      // SSL Tests
      {
          "socks4://socks_proxy:1080",
          "https://www.example.org/socks4_ssl",
          ClientSocketPool::GroupId(
              url::SchemeHostPort(url::kHttpsScheme, "www.example.org", 443),
              PrivacyMode::PRIVACY_MODE_DISABLED, NetworkIsolationKey(),
              SecureDnsPolicy::kAllow),
          true,
      },
      {
          "socks5://socks_proxy:1080",
          "https://www.example.org/socks5_ssl",
          ClientSocketPool::GroupId(
              url::SchemeHostPort(url::kHttpsScheme, "www.example.org", 443),
              PrivacyMode::PRIVACY_MODE_DISABLED, NetworkIsolationKey(),
              SecureDnsPolicy::kAllow),
          true,
      },

      {
          "socks4://socks_proxy:1080",
          "https://host.with.alternate/direct",
          ClientSocketPool::GroupId(
              url::SchemeHostPort(url::kHttpsScheme, "host.with.alternate",
                                  443),
              PrivacyMode::PRIVACY_MODE_DISABLED, NetworkIsolationKey(),
              SecureDnsPolicy::kAllow),
          true,
      },
  };

  for (const auto& test : tests) {
    session_deps_.proxy_resolution_service =
        ConfiguredProxyResolutionService::CreateFixedForTest(
            test.proxy_server, TRAFFIC_ANNOTATION_FOR_TESTS);
    std::unique_ptr<HttpNetworkSession> session(
        SetupSessionForGroupIdTests(&session_deps_));

    HttpNetworkSessionPeer peer(session.get());

    ProxyServer proxy_server(
        ProxyUriToProxyServer(test.proxy_server, ProxyServer::SCHEME_HTTP));
    ASSERT_TRUE(proxy_server.is_valid());
    auto socks_conn_pool = std::make_unique<CaptureGroupIdTransportSocketPool>(
        &dummy_connect_job_params_);
    auto* socks_conn_pool_ptr = socks_conn_pool.get();
    auto mock_pool_manager = std::make_unique<MockClientSocketPoolManager>();
    mock_pool_manager->SetSocketPool(proxy_server, std::move(socks_conn_pool));
    peer.SetClientSocketPoolManager(std::move(mock_pool_manager));

    HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

    EXPECT_EQ(ERR_IO_PENDING,
              GroupIdTransactionHelper(test.url, session.get()));
    EXPECT_EQ(test.expected_group_id,
              socks_conn_pool_ptr->last_group_id_received());
  }
}

TEST_F(HttpNetworkTransactionTest, ReconsiderProxyAfterFailedConnection) {
  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("http://www.example.org/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  session_deps_.proxy_resolution_service =
      ConfiguredProxyResolutionService::CreateFixedForTest(
          "myproxy:70;foobar:80", TRAFFIC_ANNOTATION_FOR_TESTS);

  // This simulates failure resolving all hostnames; that means we will fail
  // connecting to both proxies (myproxy:70 and foobar:80).
  session_deps_.host_resolver->rules()->AddSimulatedFailure("*");

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));
  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

  TestCompletionCallback callback;

  int rv = trans.Start(&request, callback.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback.WaitForResult();
  EXPECT_THAT(rv, IsError(ERR_PROXY_CONNECTION_FAILED));
}

// Make sure we can handle an error when writing the request.
TEST_F(HttpNetworkTransactionTest, RequestWriteError) {
  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("http://www.foo.com/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  MockWrite write_failure[] = {
    MockWrite(ASYNC, ERR_CONNECTION_RESET),
  };
  StaticSocketDataProvider data(base::span<MockRead>(), write_failure);
  session_deps_.socket_factory->AddSocketDataProvider(&data);
  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));

  TestCompletionCallback callback;

  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

  int rv = trans.Start(&request, callback.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback.WaitForResult();
  EXPECT_THAT(rv, IsError(ERR_CONNECTION_RESET));

  IPEndPoint endpoint;
  EXPECT_TRUE(trans.GetRemoteEndpoint(&endpoint));
  EXPECT_LT(0u, endpoint.address().size());
}

// Check that a connection closed after the start of the headers finishes ok.
TEST_F(HttpNetworkTransactionTest, ConnectionClosedAfterStartOfHeaders) {
  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("http://www.foo.com/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  MockRead data_reads[] = {
    MockRead("HTTP/1."),
    MockRead(SYNCHRONOUS, OK),
  };

  StaticSocketDataProvider data(data_reads, base::span<MockWrite>());
  session_deps_.socket_factory->AddSocketDataProvider(&data);
  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));

  TestCompletionCallback callback;

  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

  int rv = trans.Start(&request, callback.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback.WaitForResult();
  EXPECT_THAT(rv, IsOk());

  const HttpResponseInfo* response = trans.GetResponseInfo();
  ASSERT_TRUE(response);

  EXPECT_TRUE(response->headers);
  EXPECT_EQ("HTTP/1.0 200 OK", response->headers->GetStatusLine());

  std::string response_data;
  rv = ReadTransaction(&trans, &response_data);
  EXPECT_THAT(rv, IsOk());
  EXPECT_EQ("", response_data);

  IPEndPoint endpoint;
  EXPECT_TRUE(trans.GetRemoteEndpoint(&endpoint));
  EXPECT_LT(0u, endpoint.address().size());
}

// Make sure that a dropped connection while draining the body for auth
// restart does the right thing.
TEST_F(HttpNetworkTransactionTest, DrainResetOK) {
  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("http://www.example.org/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  MockWrite data_writes1[] = {
      MockWrite(
          "GET / HTTP/1.1\r\n"
          "Host: www.example.org\r\n"
          "Connection: keep-alive\r\n\r\n"),
  };

  MockRead data_reads1[] = {
    MockRead("HTTP/1.1 401 Unauthorized\r\n"),
    MockRead("WWW-Authenticate: Basic realm=\"MyRealm1\"\r\n"),
    MockRead("Content-Type: text/html; charset=iso-8859-1\r\n"),
    MockRead("Content-Length: 14\r\n\r\n"),
    MockRead("Unauth"),
    MockRead(ASYNC, ERR_CONNECTION_RESET),
  };

  StaticSocketDataProvider data1(data_reads1, data_writes1);
  session_deps_.socket_factory->AddSocketDataProvider(&data1);

  // After calling trans.RestartWithAuth(), this is the request we should
  // be issuing -- the final header line contains the credentials.
  MockWrite data_writes2[] = {
      MockWrite(
          "GET / HTTP/1.1\r\n"
          "Host: www.example.org\r\n"
          "Connection: keep-alive\r\n"
          "Authorization: Basic Zm9vOmJhcg==\r\n\r\n"),
  };

  // Lastly, the server responds with the actual content.
  MockRead data_reads2[] = {
    MockRead("HTTP/1.1 200 OK\r\n"),
    MockRead("Content-Type: text/html; charset=iso-8859-1\r\n"),
    MockRead("Content-Length: 100\r\n\r\n"),
    MockRead(SYNCHRONOUS, OK),
  };

  StaticSocketDataProvider data2(data_reads2, data_writes2);
  session_deps_.socket_factory->AddSocketDataProvider(&data2);
  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));

  TestCompletionCallback callback1;

  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

  int rv = trans.Start(&request, callback1.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback1.WaitForResult();
  EXPECT_THAT(rv, IsOk());

  const HttpResponseInfo* response = trans.GetResponseInfo();
  ASSERT_TRUE(response);
  EXPECT_TRUE(CheckBasicServerAuth(response->auth_challenge));

  TestCompletionCallback callback2;

  rv = trans.RestartWithAuth(AuthCredentials(kFoo, kBar), callback2.callback());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback2.WaitForResult();
  EXPECT_THAT(rv, IsOk());

  response = trans.GetResponseInfo();
  ASSERT_TRUE(response);
  EXPECT_FALSE(response->auth_challenge.has_value());
  EXPECT_EQ(100, response->headers->GetContentLength());
}

// Test HTTPS connections going through a proxy that sends extra data.
TEST_F(HttpNetworkTransactionTest, HTTPSViaProxyWithExtraData) {
  session_deps_.proxy_resolution_service =
      ConfiguredProxyResolutionService::CreateFixedForTest(
          "myproxy:70", TRAFFIC_ANNOTATION_FOR_TESTS);

  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("https://www.example.org/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  MockRead proxy_reads[] = {
    MockRead("HTTP/1.0 200 Connected\r\n\r\nExtra data"),
    MockRead(SYNCHRONOUS, OK)
  };

  StaticSocketDataProvider data(proxy_reads, base::span<MockWrite>());
  SSLSocketDataProvider ssl(ASYNC, OK);

  session_deps_.socket_factory->AddSocketDataProvider(&data);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl);

  TestCompletionCallback callback;

  session_deps_.socket_factory->ResetNextMockIndexes();

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));
  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

  int rv = trans.Start(&request, callback.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback.WaitForResult();
  EXPECT_THAT(rv, IsError(ERR_TUNNEL_CONNECTION_FAILED));
}

TEST_F(HttpNetworkTransactionTest, LargeContentLengthThenClose) {
  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("http://www.example.org/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));
  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

  MockRead data_reads[] = {
    MockRead("HTTP/1.0 200 OK\r\nContent-Length:6719476739\r\n\r\n"),
    MockRead(SYNCHRONOUS, OK),
  };

  StaticSocketDataProvider data(data_reads, base::span<MockWrite>());
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  TestCompletionCallback callback;

  int rv = trans.Start(&request, callback.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  EXPECT_THAT(callback.WaitForResult(), IsOk());

  const HttpResponseInfo* response = trans.GetResponseInfo();
  ASSERT_TRUE(response);

  EXPECT_TRUE(response->headers);
  EXPECT_EQ("HTTP/1.0 200 OK", response->headers->GetStatusLine());

  std::string response_data;
  rv = ReadTransaction(&trans, &response_data);
  EXPECT_THAT(rv, IsError(ERR_CONTENT_LENGTH_MISMATCH));
}

TEST_F(HttpNetworkTransactionTest, UploadFileSmallerThanLength) {
  base::FilePath temp_file_path;
  ASSERT_TRUE(base::CreateTemporaryFile(&temp_file_path));
  const uint64_t kFakeSize = 100000;  // file is actually blank
  UploadFileElementReader::ScopedOverridingContentLengthForTests
      overriding_content_length(kFakeSize);

  std::vector<std::unique_ptr<UploadElementReader>> element_readers;
  element_readers.push_back(std::make_unique<UploadFileElementReader>(
      base::ThreadTaskRunnerHandle::Get().get(), temp_file_path, 0,
      std::numeric_limits<uint64_t>::max(), base::Time()));
  ElementsUploadDataStream upload_data_stream(std::move(element_readers), 0);

  HttpRequestInfo request;
  request.method = "POST";
  request.url = GURL("http://www.example.org/upload");
  request.upload_data_stream = &upload_data_stream;
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));
  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

  MockRead data_reads[] = {
    MockRead("HTTP/1.0 200 OK\r\n\r\n"),
    MockRead("hello world"),
    MockRead(SYNCHRONOUS, OK),
  };
  StaticSocketDataProvider data(data_reads, base::span<MockWrite>());
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  TestCompletionCallback callback;

  int rv = trans.Start(&request, callback.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback.WaitForResult();
  EXPECT_THAT(rv, IsError(ERR_UPLOAD_FILE_CHANGED));

  const HttpResponseInfo* response = trans.GetResponseInfo();
  ASSERT_TRUE(response);

  EXPECT_FALSE(response->headers);

  base::DeleteFile(temp_file_path);
}

TEST_F(HttpNetworkTransactionTest, UploadUnreadableFile) {
  base::FilePath temp_file;
  ASSERT_TRUE(base::CreateTemporaryFile(&temp_file));
  std::string temp_file_content("Unreadable file.");
  ASSERT_EQ(static_cast<int>(temp_file_content.length()),
            base::WriteFile(temp_file, temp_file_content.c_str(),
                            temp_file_content.length()));
  ASSERT_TRUE(base::MakeFileUnreadable(temp_file));

  std::vector<std::unique_ptr<UploadElementReader>> element_readers;
  element_readers.push_back(std::make_unique<UploadFileElementReader>(
      base::ThreadTaskRunnerHandle::Get().get(), temp_file, 0,
      std::numeric_limits<uint64_t>::max(), base::Time()));
  ElementsUploadDataStream upload_data_stream(std::move(element_readers), 0);

  HttpRequestInfo request;
  request.method = "POST";
  request.url = GURL("http://www.example.org/upload");
  request.upload_data_stream = &upload_data_stream;
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  // If we try to upload an unreadable file, the transaction should fail.
  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));
  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

  StaticSocketDataProvider data;
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  TestCompletionCallback callback;

  int rv = trans.Start(&request, callback.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback.WaitForResult();
  EXPECT_THAT(rv, IsError(ERR_ACCESS_DENIED));

  base::DeleteFile(temp_file);
}

TEST_F(HttpNetworkTransactionTest, CancelDuringInitRequestBody) {
  class FakeUploadElementReader : public UploadElementReader {
   public:
    FakeUploadElementReader() = default;
    ~FakeUploadElementReader() override = default;

    CompletionOnceCallback TakeCallback() { return std::move(callback_); }

    // UploadElementReader overrides:
    int Init(CompletionOnceCallback callback) override {
      callback_ = std::move(callback);
      return ERR_IO_PENDING;
    }
    uint64_t GetContentLength() const override { return 0; }
    uint64_t BytesRemaining() const override { return 0; }
    int Read(IOBuffer* buf,
             int buf_length,
             CompletionOnceCallback callback) override {
      return ERR_FAILED;
    }

   private:
    CompletionOnceCallback callback_;
  };

  auto fake_reader = std::make_unique<FakeUploadElementReader>();
  auto* fake_reader_ptr = fake_reader.get();
  std::vector<std::unique_ptr<UploadElementReader>> element_readers;
  element_readers.push_back(std::move(fake_reader));
  ElementsUploadDataStream upload_data_stream(std::move(element_readers), 0);

  HttpRequestInfo request;
  request.method = "POST";
  request.url = GURL("http://www.example.org/upload");
  request.upload_data_stream = &upload_data_stream;
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));
  auto trans =
      std::make_unique<HttpNetworkTransaction>(DEFAULT_PRIORITY, session.get());

  StaticSocketDataProvider data;
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  TestCompletionCallback callback;
  int rv = trans->Start(&request, callback.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));
  base::RunLoop().RunUntilIdle();

  // Transaction is pending on request body initialization.
  CompletionOnceCallback init_callback = fake_reader_ptr->TakeCallback();
  ASSERT_FALSE(init_callback.is_null());

  // Return Init()'s result after the transaction gets destroyed.
  trans.reset();
  std::move(init_callback).Run(OK);  // Should not crash.
}

// Tests that changes to Auth realms are treated like auth rejections.
TEST_F(HttpNetworkTransactionTest, ChangeAuthRealms) {
  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("http://www.example.org/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  // First transaction will request a resource and receive a Basic challenge
  // with realm="first_realm".
  MockWrite data_writes1[] = {
      MockWrite(
          "GET / HTTP/1.1\r\n"
          "Host: www.example.org\r\n"
          "Connection: keep-alive\r\n"
          "\r\n"),
  };
  MockRead data_reads1[] = {
    MockRead("HTTP/1.1 401 Unauthorized\r\n"
             "WWW-Authenticate: Basic realm=\"first_realm\"\r\n"
             "\r\n"),
  };

  // After calling trans.RestartWithAuth(), provide an Authentication header
  // for first_realm. The server will reject and provide a challenge with
  // second_realm.
  MockWrite data_writes2[] = {
      MockWrite(
          "GET / HTTP/1.1\r\n"
          "Host: www.example.org\r\n"
          "Connection: keep-alive\r\n"
          "Authorization: Basic Zmlyc3Q6YmF6\r\n"
          "\r\n"),
  };
  MockRead data_reads2[] = {
    MockRead("HTTP/1.1 401 Unauthorized\r\n"
             "WWW-Authenticate: Basic realm=\"second_realm\"\r\n"
             "\r\n"),
  };

  // This again fails, and goes back to first_realm. Make sure that the
  // entry is removed from cache.
  MockWrite data_writes3[] = {
      MockWrite(
          "GET / HTTP/1.1\r\n"
          "Host: www.example.org\r\n"
          "Connection: keep-alive\r\n"
          "Authorization: Basic c2Vjb25kOmZvdQ==\r\n"
          "\r\n"),
  };
  MockRead data_reads3[] = {
    MockRead("HTTP/1.1 401 Unauthorized\r\n"
             "WWW-Authenticate: Basic realm=\"first_realm\"\r\n"
             "\r\n"),
  };

  // Try one last time (with the correct password) and get the resource.
  MockWrite data_writes4[] = {
      MockWrite(
          "GET / HTTP/1.1\r\n"
          "Host: www.example.org\r\n"
          "Connection: keep-alive\r\n"
          "Authorization: Basic Zmlyc3Q6YmFy\r\n"
          "\r\n"),
  };
  MockRead data_reads4[] = {
    MockRead("HTTP/1.1 200 OK\r\n"
             "Content-Type: text/html; charset=iso-8859-1\r\n"
             "Content-Length: 5\r\n"
             "\r\n"
             "hello"),
  };

  StaticSocketDataProvider data1(data_reads1, data_writes1);
  StaticSocketDataProvider data2(data_reads2, data_writes2);
  StaticSocketDataProvider data3(data_reads3, data_writes3);
  StaticSocketDataProvider data4(data_reads4, data_writes4);
  session_deps_.socket_factory->AddSocketDataProvider(&data1);
  session_deps_.socket_factory->AddSocketDataProvider(&data2);
  session_deps_.socket_factory->AddSocketDataProvider(&data3);
  session_deps_.socket_factory->AddSocketDataProvider(&data4);

  TestCompletionCallback callback1;

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));
  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

  // Issue the first request with Authorize headers. There should be a
  // password prompt for first_realm waiting to be filled in after the
  // transaction completes.
  int rv = trans.Start(&request, callback1.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));
  rv = callback1.WaitForResult();
  EXPECT_THAT(rv, IsOk());
  const HttpResponseInfo* response = trans.GetResponseInfo();
  ASSERT_TRUE(response);
  absl::optional<AuthChallengeInfo> challenge = response->auth_challenge;
  ASSERT_TRUE(challenge);
  EXPECT_FALSE(challenge->is_proxy);
  EXPECT_EQ("http://www.example.org", challenge->challenger.Serialize());
  EXPECT_EQ("first_realm", challenge->realm);
  EXPECT_EQ(kBasicAuthScheme, challenge->scheme);

  // Issue the second request with an incorrect password. There should be a
  // password prompt for second_realm waiting to be filled in after the
  // transaction completes.
  TestCompletionCallback callback2;
  rv = trans.RestartWithAuth(AuthCredentials(kFirst, kBaz),
                             callback2.callback());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));
  rv = callback2.WaitForResult();
  EXPECT_THAT(rv, IsOk());
  response = trans.GetResponseInfo();
  ASSERT_TRUE(response);
  challenge = response->auth_challenge;
  ASSERT_TRUE(challenge);
  EXPECT_FALSE(challenge->is_proxy);
  EXPECT_EQ("http://www.example.org", challenge->challenger.Serialize());
  EXPECT_EQ("second_realm", challenge->realm);
  EXPECT_EQ(kBasicAuthScheme, challenge->scheme);

  // Issue the third request with another incorrect password. There should be
  // a password prompt for first_realm waiting to be filled in. If the password
  // prompt is not present, it indicates that the HttpAuthCacheEntry for
  // first_realm was not correctly removed.
  TestCompletionCallback callback3;
  rv = trans.RestartWithAuth(AuthCredentials(kSecond, kFou),
                             callback3.callback());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));
  rv = callback3.WaitForResult();
  EXPECT_THAT(rv, IsOk());
  response = trans.GetResponseInfo();
  ASSERT_TRUE(response);
  challenge = response->auth_challenge;
  ASSERT_TRUE(challenge);
  EXPECT_FALSE(challenge->is_proxy);
  EXPECT_EQ("http://www.example.org", challenge->challenger.Serialize());
  EXPECT_EQ("first_realm", challenge->realm);
  EXPECT_EQ(kBasicAuthScheme, challenge->scheme);

  // Issue the fourth request with the correct password and username.
  TestCompletionCallback callback4;
  rv = trans.RestartWithAuth(AuthCredentials(kFirst, kBar),
                             callback4.callback());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));
  rv = callback4.WaitForResult();
  EXPECT_THAT(rv, IsOk());
  response = trans.GetResponseInfo();
  ASSERT_TRUE(response);
  EXPECT_FALSE(response->auth_challenge.has_value());
}

// Regression test for https://crbug.com/754395.
TEST_F(HttpNetworkTransactionTest, IgnoreAltSvcWithInvalidCert) {
  MockRead data_reads[] = {
      MockRead("HTTP/1.1 200 OK\r\n"),
      MockRead(kAlternativeServiceHttpHeader),
      MockRead("\r\n"),
      MockRead("hello world"),
      MockRead(SYNCHRONOUS, OK),
  };

  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("https://www.example.org/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  StaticSocketDataProvider data(data_reads, base::span<MockWrite>());
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  SSLSocketDataProvider ssl(ASYNC, OK);
  ssl.ssl_info.cert =
      ImportCertFromFile(GetTestCertsDirectory(), "wildcard.pem");
  ASSERT_TRUE(ssl.ssl_info.cert);
  ssl.ssl_info.cert_status = CERT_STATUS_COMMON_NAME_INVALID;
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl);

  TestCompletionCallback callback;

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));
  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

  int rv = trans.Start(&request, callback.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  url::SchemeHostPort test_server(request.url);
  HttpServerProperties* http_server_properties =
      session->http_server_properties();
  EXPECT_TRUE(
      http_server_properties
          ->GetAlternativeServiceInfos(test_server, NetworkIsolationKey())
          .empty());

  EXPECT_THAT(callback.WaitForResult(), IsOk());

  const HttpResponseInfo* response = trans.GetResponseInfo();
  ASSERT_TRUE(response);
  ASSERT_TRUE(response->headers);
  EXPECT_EQ("HTTP/1.1 200 OK", response->headers->GetStatusLine());
  EXPECT_FALSE(response->was_fetched_via_spdy);
  EXPECT_FALSE(response->was_alpn_negotiated);

  std::string response_data;
  ASSERT_THAT(ReadTransaction(&trans, &response_data), IsOk());
  EXPECT_EQ("hello world", response_data);

  EXPECT_TRUE(
      http_server_properties
          ->GetAlternativeServiceInfos(test_server, NetworkIsolationKey())
          .empty());
}

TEST_F(HttpNetworkTransactionTest, HonorAlternativeServiceHeader) {
  MockRead data_reads[] = {
      MockRead("HTTP/1.1 200 OK\r\n"),
      MockRead(kAlternativeServiceHttpHeader),
      MockRead("\r\n"),
      MockRead("hello world"),
      MockRead(SYNCHRONOUS, OK),
  };

  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("https://www.example.org/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  StaticSocketDataProvider data(data_reads, base::span<MockWrite>());
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  SSLSocketDataProvider ssl(ASYNC, OK);
  ssl.ssl_info.cert =
      ImportCertFromFile(GetTestCertsDirectory(), "wildcard.pem");
  ASSERT_TRUE(ssl.ssl_info.cert);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl);

  TestCompletionCallback callback;

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));
  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

  int rv = trans.Start(&request, callback.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  url::SchemeHostPort test_server(request.url);
  HttpServerProperties* http_server_properties =
      session->http_server_properties();
  EXPECT_TRUE(
      http_server_properties
          ->GetAlternativeServiceInfos(test_server, NetworkIsolationKey())
          .empty());

  EXPECT_THAT(callback.WaitForResult(), IsOk());

  const HttpResponseInfo* response = trans.GetResponseInfo();
  ASSERT_TRUE(response);
  ASSERT_TRUE(response->headers);
  EXPECT_EQ("HTTP/1.1 200 OK", response->headers->GetStatusLine());
  EXPECT_FALSE(response->was_fetched_via_spdy);
  EXPECT_FALSE(response->was_alpn_negotiated);

  std::string response_data;
  ASSERT_THAT(ReadTransaction(&trans, &response_data), IsOk());
  EXPECT_EQ("hello world", response_data);

  AlternativeServiceInfoVector alternative_service_info_vector =
      http_server_properties->GetAlternativeServiceInfos(test_server,
                                                         NetworkIsolationKey());
  ASSERT_EQ(1u, alternative_service_info_vector.size());
  AlternativeService alternative_service(kProtoHTTP2, "mail.example.org", 443);
  EXPECT_EQ(alternative_service,
            alternative_service_info_vector[0].alternative_service());
}

TEST_F(HttpNetworkTransactionTest,
       HonorAlternativeServiceHeaderWithNetworkIsolationKey) {
  base::test::ScopedFeatureList feature_list;
  feature_list.InitWithFeatures(
      // enabled_features
      {features::kPartitionHttpServerPropertiesByNetworkIsolationKey,
       // Need to partition connections by NetworkIsolationKey for
       // SpdySessionKeys to include NetworkIsolationKeys.
       features::kPartitionConnectionsByNetworkIsolationKey},
      // disabled_features
      {});
  // Since HttpServerProperties caches the feature value, have to create a new
  // one.
  session_deps_.http_server_properties =
      std::make_unique<HttpServerProperties>();

  const SchemefulSite kSite1(GURL("https://foo.test/"));
  const net::NetworkIsolationKey kNetworkIsolationKey1(kSite1, kSite1);
  const SchemefulSite kSite2(GURL("https://bar.test/"));
  const net::NetworkIsolationKey kNetworkIsolationKey2(kSite2, kSite2);

  MockRead data_reads[] = {
      MockRead("HTTP/1.1 200 OK\r\n"),
      MockRead(kAlternativeServiceHttpHeader),
      MockRead("\r\n"),
      MockRead("hello world"),
      MockRead(SYNCHRONOUS, OK),
  };

  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("https://www.example.org/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);
  request.network_isolation_key = kNetworkIsolationKey1;

  StaticSocketDataProvider data(data_reads, base::span<MockWrite>());
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  SSLSocketDataProvider ssl(ASYNC, OK);
  ssl.ssl_info.cert =
      ImportCertFromFile(GetTestCertsDirectory(), "wildcard.pem");
  ASSERT_TRUE(ssl.ssl_info.cert);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl);

  TestCompletionCallback callback;

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));
  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

  int rv = trans.Start(&request, callback.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  url::SchemeHostPort test_server(request.url);
  HttpServerProperties* http_server_properties =
      session->http_server_properties();
  EXPECT_TRUE(
      http_server_properties
          ->GetAlternativeServiceInfos(test_server, kNetworkIsolationKey1)
          .empty());

  EXPECT_THAT(callback.WaitForResult(), IsOk());

  const HttpResponseInfo* response = trans.GetResponseInfo();
  ASSERT_TRUE(response);
  ASSERT_TRUE(response->headers);
  EXPECT_EQ("HTTP/1.1 200 OK", response->headers->GetStatusLine());
  EXPECT_FALSE(response->was_fetched_via_spdy);
  EXPECT_FALSE(response->was_alpn_negotiated);

  std::string response_data;
  ASSERT_THAT(ReadTransaction(&trans, &response_data), IsOk());
  EXPECT_EQ("hello world", response_data);

  AlternativeServiceInfoVector alternative_service_info_vector =
      http_server_properties->GetAlternativeServiceInfos(test_server,
                                                         kNetworkIsolationKey1);
  ASSERT_EQ(1u, alternative_service_info_vector.size());
  AlternativeService alternative_service(kProtoHTTP2, "mail.example.org", 443);
  EXPECT_EQ(alternative_service,
            alternative_service_info_vector[0].alternative_service());

  // Make sure the alternative service information is only associated with
  // kNetworkIsolationKey1.
  EXPECT_TRUE(
      http_server_properties
          ->GetAlternativeServiceInfos(test_server, NetworkIsolationKey())
          .empty());
  EXPECT_TRUE(
      http_server_properties
          ->GetAlternativeServiceInfos(test_server, kNetworkIsolationKey2)
          .empty());
}

// Regression test for https://crbug.com/615497.
TEST_F(HttpNetworkTransactionTest,
       DoNotParseAlternativeServiceHeaderOnInsecureRequest) {
  MockRead data_reads[] = {
      MockRead("HTTP/1.1 200 OK\r\n"),
      MockRead(kAlternativeServiceHttpHeader),
      MockRead("\r\n"),
      MockRead("hello world"),
      MockRead(SYNCHRONOUS, OK),
  };

  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("http://www.example.org/");
  request.load_flags = 0;
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  StaticSocketDataProvider data(data_reads, base::span<MockWrite>());
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  TestCompletionCallback callback;

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));
  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

  url::SchemeHostPort test_server(request.url);
  HttpServerProperties* http_server_properties =
      session->http_server_properties();
  EXPECT_TRUE(
      http_server_properties
          ->GetAlternativeServiceInfos(test_server, NetworkIsolationKey())
          .empty());

  int rv = trans.Start(&request, callback.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));
  EXPECT_THAT(callback.WaitForResult(), IsOk());

  const HttpResponseInfo* response = trans.GetResponseInfo();
  ASSERT_TRUE(response);
  ASSERT_TRUE(response->headers);
  EXPECT_EQ("HTTP/1.1 200 OK", response->headers->GetStatusLine());
  EXPECT_FALSE(response->was_fetched_via_spdy);
  EXPECT_FALSE(response->was_alpn_negotiated);

  std::string response_data;
  ASSERT_THAT(ReadTransaction(&trans, &response_data), IsOk());
  EXPECT_EQ("hello world", response_data);

  EXPECT_TRUE(
      http_server_properties
          ->GetAlternativeServiceInfos(test_server, NetworkIsolationKey())
          .empty());
}

// HTTP/2 Alternative Services should be disabled by default.
// TODO(bnc): Remove when https://crbug.com/615413 is fixed.
TEST_F(HttpNetworkTransactionTest,
       DisableHTTP2AlternativeServicesWithDifferentHost) {
  session_deps_.enable_http2_alternative_service = false;

  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("https://www.example.org/");
  request.load_flags = 0;
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  MockConnect mock_connect(ASYNC, ERR_CONNECTION_REFUSED);
  StaticSocketDataProvider first_data;
  first_data.set_connect_data(mock_connect);
  session_deps_.socket_factory->AddSocketDataProvider(&first_data);
  SSLSocketDataProvider ssl_http11(ASYNC, OK);
  ssl_http11.next_proto = kProtoHTTP11;
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl_http11);

  MockRead data_reads[] = {
      MockRead("HTTP/1.1 200 OK\r\n\r\n"), MockRead("hello world"),
      MockRead(ASYNC, OK),
  };
  StaticSocketDataProvider second_data(data_reads, base::span<MockWrite>());
  session_deps_.socket_factory->AddSocketDataProvider(&second_data);

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));

  HttpServerProperties* http_server_properties =
      session->http_server_properties();
  AlternativeService alternative_service(kProtoHTTP2, "different.example.org",
                                         444);
  base::Time expiration = base::Time::Now() + base::Days(1);
  http_server_properties->SetHttp2AlternativeService(
      url::SchemeHostPort(request.url), NetworkIsolationKey(),
      alternative_service, expiration);

  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());
  TestCompletionCallback callback;

  int rv = trans.Start(&request, callback.callback(), NetLogWithSource());
  // Alternative service is not used, request fails.
  EXPECT_THAT(callback.GetResult(rv), IsError(ERR_CONNECTION_REFUSED));
}

// Regression test for https://crbug.com/615497:
// Alternative Services should be disabled for http origin.
TEST_F(HttpNetworkTransactionTest,
       DisableAlternativeServicesForInsecureOrigin) {
  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("http://www.example.org/");
  request.load_flags = 0;
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  MockConnect mock_connect(ASYNC, ERR_CONNECTION_REFUSED);
  StaticSocketDataProvider first_data;
  first_data.set_connect_data(mock_connect);
  session_deps_.socket_factory->AddSocketDataProvider(&first_data);

  MockRead data_reads[] = {
      MockRead("HTTP/1.1 200 OK\r\n\r\n"), MockRead("hello world"),
      MockRead(ASYNC, OK),
  };
  StaticSocketDataProvider second_data(data_reads, base::span<MockWrite>());
  session_deps_.socket_factory->AddSocketDataProvider(&second_data);

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));

  HttpServerProperties* http_server_properties =
      session->http_server_properties();
  AlternativeService alternative_service(kProtoHTTP2, "", 444);
  base::Time expiration = base::Time::Now() + base::Days(1);
  http_server_properties->SetHttp2AlternativeService(
      url::SchemeHostPort(request.url), NetworkIsolationKey(),
      alternative_service, expiration);

  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());
  TestCompletionCallback callback;

  int rv = trans.Start(&request, callback.callback(), NetLogWithSource());
  // Alternative service is not used, request fails.
  EXPECT_THAT(callback.GetResult(rv), IsError(ERR_CONNECTION_REFUSED));
}

TEST_F(HttpNetworkTransactionTest, ClearAlternativeServices) {
  // Set an alternative service for origin.
  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));
  HttpServerProperties* http_server_properties =
      session->http_server_properties();
  url::SchemeHostPort test_server("https", "www.example.org", 443);
  AlternativeService alternative_service(kProtoQUIC, "", 80);
  base::Time expiration = base::Time::Now() + base::Days(1);
  http_server_properties->SetQuicAlternativeService(
      test_server, NetworkIsolationKey(), alternative_service, expiration,
      session->context().quic_context->params()->supported_versions);
  EXPECT_EQ(1u,
            http_server_properties
                ->GetAlternativeServiceInfos(test_server, NetworkIsolationKey())
                .size());

  // Send a clear header.
  MockRead data_reads[] = {
      MockRead("HTTP/1.1 200 OK\r\n"),
      MockRead("Alt-Svc: clear\r\n"),
      MockRead("\r\n"),
      MockRead("hello world"),
      MockRead(SYNCHRONOUS, OK),
  };
  StaticSocketDataProvider data(data_reads, base::span<MockWrite>());
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  SSLSocketDataProvider ssl(ASYNC, OK);
  ssl.ssl_info.cert =
      ImportCertFromFile(GetTestCertsDirectory(), "wildcard.pem");
  ASSERT_TRUE(ssl.ssl_info.cert);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl);

  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("https://www.example.org/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  TestCompletionCallback callback;

  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

  int rv = trans.Start(&request, callback.callback(), NetLogWithSource());
  EXPECT_THAT(callback.GetResult(rv), IsOk());

  const HttpResponseInfo* response = trans.GetResponseInfo();
  ASSERT_TRUE(response);
  ASSERT_TRUE(response->headers);
  EXPECT_EQ("HTTP/1.1 200 OK", response->headers->GetStatusLine());
  EXPECT_FALSE(response->was_fetched_via_spdy);
  EXPECT_FALSE(response->was_alpn_negotiated);

  std::string response_data;
  ASSERT_THAT(ReadTransaction(&trans, &response_data), IsOk());
  EXPECT_EQ("hello world", response_data);

  EXPECT_TRUE(
      http_server_properties
          ->GetAlternativeServiceInfos(test_server, NetworkIsolationKey())
          .empty());
}

TEST_F(HttpNetworkTransactionTest, HonorMultipleAlternativeServiceHeaders) {
  MockRead data_reads[] = {
      MockRead("HTTP/1.1 200 OK\r\n"),
      MockRead("Alt-Svc: h2=\"www.example.com:443\","),
      MockRead("h2=\":1234\"\r\n\r\n"),
      MockRead("hello world"),
      MockRead(SYNCHRONOUS, OK),
  };

  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("https://www.example.org/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  StaticSocketDataProvider data(data_reads, base::span<MockWrite>());
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  SSLSocketDataProvider ssl(ASYNC, OK);
  ssl.ssl_info.cert =
      ImportCertFromFile(GetTestCertsDirectory(), "wildcard.pem");
  ASSERT_TRUE(ssl.ssl_info.cert);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl);

  TestCompletionCallback callback;

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));
  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

  int rv = trans.Start(&request, callback.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  url::SchemeHostPort test_server("https", "www.example.org", 443);
  HttpServerProperties* http_server_properties =
      session->http_server_properties();
  EXPECT_TRUE(
      http_server_properties
          ->GetAlternativeServiceInfos(test_server, NetworkIsolationKey())
          .empty());

  EXPECT_THAT(callback.WaitForResult(), IsOk());

  const HttpResponseInfo* response = trans.GetResponseInfo();
  ASSERT_TRUE(response);
  ASSERT_TRUE(response->headers);
  EXPECT_EQ("HTTP/1.1 200 OK", response->headers->GetStatusLine());
  EXPECT_FALSE(response->was_fetched_via_spdy);
  EXPECT_FALSE(response->was_alpn_negotiated);

  std::string response_data;
  ASSERT_THAT(ReadTransaction(&trans, &response_data), IsOk());
  EXPECT_EQ("hello world", response_data);

  AlternativeServiceInfoVector alternative_service_info_vector =
      http_server_properties->GetAlternativeServiceInfos(test_server,
                                                         NetworkIsolationKey());
  ASSERT_EQ(2u, alternative_service_info_vector.size());

  AlternativeService alternative_service(kProtoHTTP2, "www.example.com", 443);
  EXPECT_EQ(alternative_service,
            alternative_service_info_vector[0].alternative_service());
  AlternativeService alternative_service_2(kProtoHTTP2, "www.example.org",
                                           1234);
  EXPECT_EQ(alternative_service_2,
            alternative_service_info_vector[1].alternative_service());
}

TEST_F(HttpNetworkTransactionTest, IdentifyQuicBroken) {
  url::SchemeHostPort server("https", "origin.example.org", 443);
  HostPortPair alternative("alternative.example.org", 443);
  std::string origin_url = "https://origin.example.org:443";
  std::string alternative_url = "https://alternative.example.org:443";

  // Negotiate HTTP/1.1 with alternative.example.org.
  SSLSocketDataProvider ssl(ASYNC, OK);
  ssl.next_proto = kProtoHTTP11;
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl);

  // HTTP/1.1 data for request.
  MockWrite http_writes[] = {
      MockWrite("GET / HTTP/1.1\r\n"
                "Host: alternative.example.org\r\n"
                "Connection: keep-alive\r\n\r\n"),
  };

  MockRead http_reads[] = {
      MockRead("HTTP/1.1 200 OK\r\n"
               "Content-Type: text/html; charset=iso-8859-1\r\n"
               "Content-Length: 40\r\n\r\n"
               "first HTTP/1.1 response from alternative"),
  };
  StaticSocketDataProvider http_data(http_reads, http_writes);
  session_deps_.socket_factory->AddSocketDataProvider(&http_data);

  StaticSocketDataProvider data_refused;
  data_refused.set_connect_data(MockConnect(ASYNC, ERR_CONNECTION_REFUSED));
  session_deps_.socket_factory->AddSocketDataProvider(&data_refused);

  // Set up a QUIC alternative service for server.
  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));
  HttpServerProperties* http_server_properties =
      session->http_server_properties();
  AlternativeService alternative_service(kProtoQUIC, alternative);
  base::Time expiration = base::Time::Now() + base::Days(1);
  http_server_properties->SetQuicAlternativeService(
      server, NetworkIsolationKey(), alternative_service, expiration,
      DefaultSupportedQuicVersions());
  // Mark the QUIC alternative service as broken.
  http_server_properties->MarkAlternativeServiceBroken(alternative_service,
                                                       NetworkIsolationKey());

  HttpRequestInfo request;
  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());
  request.method = "GET";
  request.url = GURL(origin_url);
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  TestCompletionCallback callback;
  NetErrorDetails details;
  EXPECT_FALSE(details.quic_broken);

  trans.Start(&request, callback.callback(), NetLogWithSource());
  trans.PopulateNetErrorDetails(&details);
  EXPECT_TRUE(details.quic_broken);
}

TEST_F(HttpNetworkTransactionTest, IdentifyQuicNotBroken) {
  url::SchemeHostPort server("https", "origin.example.org", 443);
  HostPortPair alternative1("alternative1.example.org", 443);
  HostPortPair alternative2("alternative2.example.org", 443);
  std::string origin_url = "https://origin.example.org:443";
  std::string alternative_url1 = "https://alternative1.example.org:443";
  std::string alternative_url2 = "https://alternative2.example.org:443";

  // Negotiate HTTP/1.1 with alternative1.example.org.
  SSLSocketDataProvider ssl(ASYNC, OK);
  ssl.next_proto = kProtoHTTP11;
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl);

  // HTTP/1.1 data for request.
  MockWrite http_writes[] = {
      MockWrite("GET / HTTP/1.1\r\n"
                "Host: alternative1.example.org\r\n"
                "Connection: keep-alive\r\n\r\n"),
  };

  MockRead http_reads[] = {
      MockRead("HTTP/1.1 200 OK\r\n"
               "Content-Type: text/html; charset=iso-8859-1\r\n"
               "Content-Length: 40\r\n\r\n"
               "first HTTP/1.1 response from alternative1"),
  };
  StaticSocketDataProvider http_data(http_reads, http_writes);
  session_deps_.socket_factory->AddSocketDataProvider(&http_data);

  StaticSocketDataProvider data_refused;
  data_refused.set_connect_data(MockConnect(ASYNC, ERR_CONNECTION_REFUSED));
  session_deps_.socket_factory->AddSocketDataProvider(&data_refused);

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));
  HttpServerProperties* http_server_properties =
      session->http_server_properties();

  // Set up two QUIC alternative services for server.
  AlternativeServiceInfoVector alternative_service_info_vector;
  base::Time expiration = base::Time::Now() + base::Days(1);

  AlternativeService alternative_service1(kProtoQUIC, alternative1);
  alternative_service_info_vector.push_back(
      AlternativeServiceInfo::CreateQuicAlternativeServiceInfo(
          alternative_service1, expiration,
          session->context().quic_context->params()->supported_versions));
  AlternativeService alternative_service2(kProtoQUIC, alternative2);
  alternative_service_info_vector.push_back(
      AlternativeServiceInfo::CreateQuicAlternativeServiceInfo(
          alternative_service2, expiration,
          session->context().quic_context->params()->supported_versions));

  http_server_properties->SetAlternativeServices(
      server, NetworkIsolationKey(), alternative_service_info_vector);

  // Mark one of the QUIC alternative service as broken.
  http_server_properties->MarkAlternativeServiceBroken(alternative_service1,
                                                       NetworkIsolationKey());
  EXPECT_EQ(2u, http_server_properties
                    ->GetAlternativeServiceInfos(server, NetworkIsolationKey())
                    .size());

  HttpRequestInfo request;
  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());
  request.method = "GET";
  request.url = GURL(origin_url);
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  TestCompletionCallback callback;
  NetErrorDetails details;
  EXPECT_FALSE(details.quic_broken);

  trans.Start(&request, callback.callback(), NetLogWithSource());
  trans.PopulateNetErrorDetails(&details);
  EXPECT_FALSE(details.quic_broken);
}

TEST_F(HttpNetworkTransactionTest, MarkBrokenAlternateProtocolAndFallback) {
  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("https://www.example.org/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  MockConnect mock_connect(ASYNC, ERR_CONNECTION_REFUSED);
  StaticSocketDataProvider first_data;
  first_data.set_connect_data(mock_connect);
  session_deps_.socket_factory->AddSocketDataProvider(&first_data);
  SSLSocketDataProvider ssl_http11(ASYNC, OK);
  ssl_http11.next_proto = kProtoHTTP11;
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl_http11);

  MockRead data_reads[] = {
    MockRead("HTTP/1.1 200 OK\r\n\r\n"),
    MockRead("hello world"),
    MockRead(ASYNC, OK),
  };
  StaticSocketDataProvider second_data(data_reads, base::span<MockWrite>());
  session_deps_.socket_factory->AddSocketDataProvider(&second_data);

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));

  HttpServerProperties* http_server_properties =
      session->http_server_properties();
  const url::SchemeHostPort server(request.url);
  // Port must be < 1024, or the header will be ignored (since initial port was
  // port 80 (another restricted port).
  // Port is ignored by MockConnect anyway.
  const AlternativeService alternative_service(kProtoHTTP2, "www.example.org",
                                               666);
  base::Time expiration = base::Time::Now() + base::Days(1);
  http_server_properties->SetHttp2AlternativeService(
      server, NetworkIsolationKey(), alternative_service, expiration);

  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());
  TestCompletionCallback callback;

  int rv = trans.Start(&request, callback.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));
  EXPECT_THAT(callback.WaitForResult(), IsOk());

  const HttpResponseInfo* response = trans.GetResponseInfo();
  ASSERT_TRUE(response);
  ASSERT_TRUE(response->headers);
  EXPECT_EQ("HTTP/1.1 200 OK", response->headers->GetStatusLine());

  std::string response_data;
  ASSERT_THAT(ReadTransaction(&trans, &response_data), IsOk());
  EXPECT_EQ("hello world", response_data);

  const AlternativeServiceInfoVector alternative_service_info_vector =
      http_server_properties->GetAlternativeServiceInfos(server,
                                                         NetworkIsolationKey());
  ASSERT_EQ(1u, alternative_service_info_vector.size());
  EXPECT_EQ(alternative_service,
            alternative_service_info_vector[0].alternative_service());
  EXPECT_TRUE(http_server_properties->IsAlternativeServiceBroken(
      alternative_service, NetworkIsolationKey()));
}

// Ensure that we are not allowed to redirect traffic via an alternate protocol
// to an unrestricted (port >= 1024) when the original traffic was on a
// restricted port (port < 1024).  Ensure that we can redirect in all other
// cases.
TEST_F(HttpNetworkTransactionTest, AlternateProtocolPortRestrictedBlocked) {
  HttpRequestInfo restricted_port_request;
  restricted_port_request.method = "GET";
  restricted_port_request.url = GURL("https://www.example.org:1023/");
  restricted_port_request.load_flags = 0;
  restricted_port_request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  MockConnect mock_connect(ASYNC, ERR_CONNECTION_REFUSED);
  StaticSocketDataProvider first_data;
  first_data.set_connect_data(mock_connect);
  session_deps_.socket_factory->AddSocketDataProvider(&first_data);

  MockRead data_reads[] = {
    MockRead("HTTP/1.1 200 OK\r\n\r\n"),
    MockRead("hello world"),
    MockRead(ASYNC, OK),
  };
  StaticSocketDataProvider second_data(data_reads, base::span<MockWrite>());
  session_deps_.socket_factory->AddSocketDataProvider(&second_data);
  SSLSocketDataProvider ssl_http11(ASYNC, OK);
  ssl_http11.next_proto = kProtoHTTP11;
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl_http11);

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));

  HttpServerProperties* http_server_properties =
      session->http_server_properties();
  const int kUnrestrictedAlternatePort = 1024;
  AlternativeService alternative_service(kProtoHTTP2, "www.example.org",
                                         kUnrestrictedAlternatePort);
  base::Time expiration = base::Time::Now() + base::Days(1);
  http_server_properties->SetHttp2AlternativeService(
      url::SchemeHostPort(restricted_port_request.url), NetworkIsolationKey(),
      alternative_service, expiration);

  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());
  TestCompletionCallback callback;

  int rv = trans.Start(&restricted_port_request, callback.callback(),
                       NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));
  // Invalid change to unrestricted port should fail.
  EXPECT_THAT(callback.WaitForResult(), IsError(ERR_CONNECTION_REFUSED));
}

// Ensure that we are allowed to redirect traffic via an alternate protocol to
// an unrestricted (port >= 1024) when the original traffic was on a restricted
// port (port < 1024) if we set |enable_user_alternate_protocol_ports|.
TEST_F(HttpNetworkTransactionTest, AlternateProtocolPortRestrictedPermitted) {
  session_deps_.enable_user_alternate_protocol_ports = true;

  HttpRequestInfo restricted_port_request;
  restricted_port_request.method = "GET";
  restricted_port_request.url = GURL("https://www.example.org:1023/");
  restricted_port_request.load_flags = 0;
  restricted_port_request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  MockConnect mock_connect(ASYNC, ERR_CONNECTION_REFUSED);
  StaticSocketDataProvider first_data;
  first_data.set_connect_data(mock_connect);
  session_deps_.socket_factory->AddSocketDataProvider(&first_data);

  MockRead data_reads[] = {
    MockRead("HTTP/1.1 200 OK\r\n\r\n"),
    MockRead("hello world"),
    MockRead(ASYNC, OK),
  };
  StaticSocketDataProvider second_data(data_reads, base::span<MockWrite>());
  session_deps_.socket_factory->AddSocketDataProvider(&second_data);
  SSLSocketDataProvider ssl_http11(ASYNC, OK);
  ssl_http11.next_proto = kProtoHTTP11;
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl_http11);

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));

  HttpServerProperties* http_server_properties =
      session->http_server_properties();
  const int kUnrestrictedAlternatePort = 1024;
  AlternativeService alternative_service(kProtoHTTP2, "www.example.org",
                                         kUnrestrictedAlternatePort);
  base::Time expiration = base::Time::Now() + base::Days(1);
  http_server_properties->SetHttp2AlternativeService(
      url::SchemeHostPort(restricted_port_request.url), NetworkIsolationKey(),
      alternative_service, expiration);

  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());
  TestCompletionCallback callback;

  EXPECT_EQ(ERR_IO_PENDING,
            trans.Start(&restricted_port_request, callback.callback(),
                        NetLogWithSource()));
  // Change to unrestricted port should succeed.
  EXPECT_THAT(callback.WaitForResult(), IsOk());
}

// Ensure that we are not allowed to redirect traffic via an alternate protocol
// to an unrestricted (port >= 1024) when the original traffic was on a
// restricted port (port < 1024).  Ensure that we can redirect in all other
// cases.
TEST_F(HttpNetworkTransactionTest, AlternateProtocolPortRestrictedAllowed) {
  HttpRequestInfo restricted_port_request;
  restricted_port_request.method = "GET";
  restricted_port_request.url = GURL("https://www.example.org:1023/");
  restricted_port_request.load_flags = 0;
  restricted_port_request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  MockConnect mock_connect(ASYNC, ERR_CONNECTION_REFUSED);
  StaticSocketDataProvider first_data;
  first_data.set_connect_data(mock_connect);
  session_deps_.socket_factory->AddSocketDataProvider(&first_data);

  MockRead data_reads[] = {
    MockRead("HTTP/1.1 200 OK\r\n\r\n"),
    MockRead("hello world"),
    MockRead(ASYNC, OK),
  };
  StaticSocketDataProvider second_data(data_reads, base::span<MockWrite>());
  session_deps_.socket_factory->AddSocketDataProvider(&second_data);

  SSLSocketDataProvider ssl(ASYNC, OK);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl);

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));

  HttpServerProperties* http_server_properties =
      session->http_server_properties();
  const int kRestrictedAlternatePort = 80;
  AlternativeService alternative_service(kProtoHTTP2, "www.example.org",
                                         kRestrictedAlternatePort);
  base::Time expiration = base::Time::Now() + base::Days(1);
  http_server_properties->SetHttp2AlternativeService(
      url::SchemeHostPort(restricted_port_request.url), NetworkIsolationKey(),
      alternative_service, expiration);

  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());
  TestCompletionCallback callback;

  int rv = trans.Start(&restricted_port_request, callback.callback(),
                       NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));
  // Valid change to restricted port should pass.
  EXPECT_THAT(callback.WaitForResult(), IsOk());
}

// Ensure that we are not allowed to redirect traffic via an alternate protocol
// to an unrestricted (port >= 1024) when the original traffic was on a
// restricted port (port < 1024).  Ensure that we can redirect in all other
// cases.
TEST_F(HttpNetworkTransactionTest, AlternateProtocolPortUnrestrictedAllowed1) {
  HttpRequestInfo unrestricted_port_request;
  unrestricted_port_request.method = "GET";
  unrestricted_port_request.url = GURL("https://www.example.org:1024/");
  unrestricted_port_request.load_flags = 0;
  unrestricted_port_request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  MockConnect mock_connect(ASYNC, ERR_CONNECTION_REFUSED);
  StaticSocketDataProvider first_data;
  first_data.set_connect_data(mock_connect);
  session_deps_.socket_factory->AddSocketDataProvider(&first_data);

  MockRead data_reads[] = {
    MockRead("HTTP/1.1 200 OK\r\n\r\n"),
    MockRead("hello world"),
    MockRead(ASYNC, OK),
  };
  StaticSocketDataProvider second_data(data_reads, base::span<MockWrite>());
  session_deps_.socket_factory->AddSocketDataProvider(&second_data);
  SSLSocketDataProvider ssl_http11(ASYNC, OK);
  ssl_http11.next_proto = kProtoHTTP11;
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl_http11);

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));

  HttpServerProperties* http_server_properties =
      session->http_server_properties();
  const int kRestrictedAlternatePort = 80;
  AlternativeService alternative_service(kProtoHTTP2, "www.example.org",
                                         kRestrictedAlternatePort);
  base::Time expiration = base::Time::Now() + base::Days(1);
  http_server_properties->SetHttp2AlternativeService(
      url::SchemeHostPort(unrestricted_port_request.url), NetworkIsolationKey(),
      alternative_service, expiration);

  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());
  TestCompletionCallback callback;

  int rv = trans.Start(&unrestricted_port_request, callback.callback(),
                       NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));
  // Valid change to restricted port should pass.
  EXPECT_THAT(callback.WaitForResult(), IsOk());
}

// Ensure that we are not allowed to redirect traffic via an alternate protocol
// to an unrestricted (port >= 1024) when the original traffic was on a
// restricted port (port < 1024).  Ensure that we can redirect in all other
// cases.
TEST_F(HttpNetworkTransactionTest, AlternateProtocolPortUnrestrictedAllowed2) {
  HttpRequestInfo unrestricted_port_request;
  unrestricted_port_request.method = "GET";
  unrestricted_port_request.url = GURL("https://www.example.org:1024/");
  unrestricted_port_request.load_flags = 0;
  unrestricted_port_request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  MockConnect mock_connect(ASYNC, ERR_CONNECTION_REFUSED);
  StaticSocketDataProvider first_data;
  first_data.set_connect_data(mock_connect);
  session_deps_.socket_factory->AddSocketDataProvider(&first_data);

  MockRead data_reads[] = {
    MockRead("HTTP/1.1 200 OK\r\n\r\n"),
    MockRead("hello world"),
    MockRead(ASYNC, OK),
  };
  StaticSocketDataProvider second_data(data_reads, base::span<MockWrite>());
  session_deps_.socket_factory->AddSocketDataProvider(&second_data);

  SSLSocketDataProvider ssl(ASYNC, OK);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl);

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));

  HttpServerProperties* http_server_properties =
      session->http_server_properties();
  const int kUnrestrictedAlternatePort = 1025;
  AlternativeService alternative_service(kProtoHTTP2, "www.example.org",
                                         kUnrestrictedAlternatePort);
  base::Time expiration = base::Time::Now() + base::Days(1);
  http_server_properties->SetHttp2AlternativeService(
      url::SchemeHostPort(unrestricted_port_request.url), NetworkIsolationKey(),
      alternative_service, expiration);

  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());
  TestCompletionCallback callback;

  int rv = trans.Start(&unrestricted_port_request, callback.callback(),
                       NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));
  // Valid change to an unrestricted port should pass.
  EXPECT_THAT(callback.WaitForResult(), IsOk());
}

// Ensure that we are not allowed to redirect traffic via an alternate protocol
// to an unsafe port, and that we resume the second HttpStreamFactory::Job once
// the alternate protocol request fails.
TEST_F(HttpNetworkTransactionTest, AlternateProtocolUnsafeBlocked) {
  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("http://www.example.org/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  // The alternate protocol request will error out before we attempt to connect,
  // so only the standard HTTP request will try to connect.
  MockRead data_reads[] = {
    MockRead("HTTP/1.1 200 OK\r\n\r\n"),
    MockRead("hello world"),
    MockRead(ASYNC, OK),
  };
  StaticSocketDataProvider data(data_reads, base::span<MockWrite>());
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));

  HttpServerProperties* http_server_properties =
      session->http_server_properties();
  const int kUnsafePort = 7;
  AlternativeService alternative_service(kProtoHTTP2, "www.example.org",
                                         kUnsafePort);
  base::Time expiration = base::Time::Now() + base::Days(1);
  http_server_properties->SetHttp2AlternativeService(
      url::SchemeHostPort(request.url), NetworkIsolationKey(),
      alternative_service, expiration);

  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());
  TestCompletionCallback callback;

  int rv = trans.Start(&request, callback.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));
  // The HTTP request should succeed.
  EXPECT_THAT(callback.WaitForResult(), IsOk());

  const HttpResponseInfo* response = trans.GetResponseInfo();
  ASSERT_TRUE(response);
  ASSERT_TRUE(response->headers);
  EXPECT_EQ("HTTP/1.1 200 OK", response->headers->GetStatusLine());

  std::string response_data;
  ASSERT_THAT(ReadTransaction(&trans, &response_data), IsOk());
  EXPECT_EQ("hello world", response_data);
}

TEST_F(HttpNetworkTransactionTest, UseAlternateProtocolForNpnSpdy) {
  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("https://www.example.org/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  MockRead data_reads[] = {
      MockRead("HTTP/1.1 200 OK\r\n"),
      MockRead(kAlternativeServiceHttpHeader),
      MockRead("\r\n"),
      MockRead("hello world"),
      MockRead(SYNCHRONOUS, ERR_TEST_PEER_CLOSE_AFTER_NEXT_MOCK_READ),
      MockRead(ASYNC, OK)};

  StaticSocketDataProvider first_transaction(data_reads,
                                             base::span<MockWrite>());
  session_deps_.socket_factory->AddSocketDataProvider(&first_transaction);
  SSLSocketDataProvider ssl_http11(ASYNC, OK);
  ssl_http11.next_proto = kProtoHTTP11;
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl_http11);

  AddSSLSocketData();

  spdy::SpdySerializedFrame req(
      spdy_util_.ConstructSpdyGet("https://www.example.org/", 1, LOWEST));
  MockWrite spdy_writes[] = {CreateMockWrite(req, 0)};

  spdy::SpdySerializedFrame resp(
      spdy_util_.ConstructSpdyGetReply(nullptr, 0, 1));
  spdy::SpdySerializedFrame data(spdy_util_.ConstructSpdyDataFrame(1, true));
  MockRead spdy_reads[] = {
      CreateMockRead(resp, 1), CreateMockRead(data, 2), MockRead(ASYNC, 0, 3),
  };

  SequencedSocketData spdy_data(spdy_reads, spdy_writes);
  session_deps_.socket_factory->AddSocketDataProvider(&spdy_data);

  MockConnect never_finishing_connect(SYNCHRONOUS, ERR_IO_PENDING);
  StaticSocketDataProvider hanging_non_alternate_protocol_socket;
  hanging_non_alternate_protocol_socket.set_connect_data(
      never_finishing_connect);
  session_deps_.socket_factory->AddSocketDataProvider(
      &hanging_non_alternate_protocol_socket);

  TestCompletionCallback callback;

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));
  auto trans =
      std::make_unique<HttpNetworkTransaction>(DEFAULT_PRIORITY, session.get());

  int rv = trans->Start(&request, callback.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));
  EXPECT_THAT(callback.WaitForResult(), IsOk());

  const HttpResponseInfo* response = trans->GetResponseInfo();
  ASSERT_TRUE(response);
  ASSERT_TRUE(response->headers);
  EXPECT_EQ("HTTP/1.1 200 OK", response->headers->GetStatusLine());

  std::string response_data;
  ASSERT_THAT(ReadTransaction(trans.get(), &response_data), IsOk());
  EXPECT_EQ("hello world", response_data);

  trans =
      std::make_unique<HttpNetworkTransaction>(DEFAULT_PRIORITY, session.get());

  rv = trans->Start(&request, callback.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));
  EXPECT_THAT(callback.WaitForResult(), IsOk());

  response = trans->GetResponseInfo();
  ASSERT_TRUE(response);
  ASSERT_TRUE(response->headers);
  EXPECT_EQ("HTTP/1.1 200", response->headers->GetStatusLine());
  EXPECT_TRUE(response->was_fetched_via_spdy);
  EXPECT_TRUE(response->was_alpn_negotiated);

  ASSERT_THAT(ReadTransaction(trans.get(), &response_data), IsOk());
  EXPECT_EQ("hello!", response_data);
}

TEST_F(HttpNetworkTransactionTest, AlternateProtocolWithSpdyLateBinding) {
  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("https://www.example.org/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  // First transaction receives Alt-Svc header over HTTP/1.1.
  MockRead data_reads[] = {
      MockRead("HTTP/1.1 200 OK\r\n"),
      MockRead(kAlternativeServiceHttpHeader),
      MockRead("\r\n"),
      MockRead("hello world"),
      MockRead(SYNCHRONOUS, ERR_TEST_PEER_CLOSE_AFTER_NEXT_MOCK_READ),
      MockRead(ASYNC, OK),
  };

  StaticSocketDataProvider http11_data(data_reads, base::span<MockWrite>());
  session_deps_.socket_factory->AddSocketDataProvider(&http11_data);

  SSLSocketDataProvider ssl_http11(ASYNC, OK);
  ssl_http11.ssl_info.cert =
      ImportCertFromFile(GetTestCertsDirectory(), "wildcard.pem");
  ASSERT_TRUE(ssl_http11.ssl_info.cert);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl_http11);

  // Second transaction starts an alternative and a non-alternative Job.
  // Both sockets hang.
  MockConnect never_finishing_connect(SYNCHRONOUS, ERR_IO_PENDING);
  StaticSocketDataProvider hanging_socket1;
  hanging_socket1.set_connect_data(never_finishing_connect);
  session_deps_.socket_factory->AddSocketDataProvider(&hanging_socket1);

  StaticSocketDataProvider hanging_socket2;
  hanging_socket2.set_connect_data(never_finishing_connect);
  session_deps_.socket_factory->AddSocketDataProvider(&hanging_socket2);

  // Third transaction starts an alternative and a non-alternative job.
  // The non-alternative job hangs, but the alternative one succeeds.
  // The second transaction, still pending, binds to this socket.
  spdy::SpdySerializedFrame req1(
      spdy_util_.ConstructSpdyGet("https://www.example.org/", 1, LOWEST));
  spdy::SpdySerializedFrame req2(
      spdy_util_.ConstructSpdyGet("https://www.example.org/", 3, LOWEST));
  MockWrite spdy_writes[] = {
      CreateMockWrite(req1, 0), CreateMockWrite(req2, 1),
  };
  spdy::SpdySerializedFrame resp1(
      spdy_util_.ConstructSpdyGetReply(nullptr, 0, 1));
  spdy::SpdySerializedFrame data1(spdy_util_.ConstructSpdyDataFrame(1, true));
  spdy::SpdySerializedFrame resp2(
      spdy_util_.ConstructSpdyGetReply(nullptr, 0, 3));
  spdy::SpdySerializedFrame data2(spdy_util_.ConstructSpdyDataFrame(3, true));
  MockRead spdy_reads[] = {
      CreateMockRead(resp1, 2), CreateMockRead(data1, 3),
      CreateMockRead(resp2, 4), CreateMockRead(data2, 5),
      MockRead(ASYNC, 0, 6),
  };

  SequencedSocketData spdy_data(spdy_reads, spdy_writes);
  session_deps_.socket_factory->AddSocketDataProvider(&spdy_data);

  AddSSLSocketData();

  StaticSocketDataProvider hanging_socket3;
  hanging_socket3.set_connect_data(never_finishing_connect);
  session_deps_.socket_factory->AddSocketDataProvider(&hanging_socket3);

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));
  TestCompletionCallback callback1;
  HttpNetworkTransaction trans1(DEFAULT_PRIORITY, session.get());

  int rv = trans1.Start(&request, callback1.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));
  EXPECT_THAT(callback1.WaitForResult(), IsOk());

  const HttpResponseInfo* response = trans1.GetResponseInfo();
  ASSERT_TRUE(response);
  ASSERT_TRUE(response->headers);
  EXPECT_EQ("HTTP/1.1 200 OK", response->headers->GetStatusLine());

  std::string response_data;
  ASSERT_THAT(ReadTransaction(&trans1, &response_data), IsOk());
  EXPECT_EQ("hello world", response_data);

  TestCompletionCallback callback2;
  HttpNetworkTransaction trans2(DEFAULT_PRIORITY, session.get());
  rv = trans2.Start(&request, callback2.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  TestCompletionCallback callback3;
  HttpNetworkTransaction trans3(DEFAULT_PRIORITY, session.get());
  rv = trans3.Start(&request, callback3.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  EXPECT_THAT(callback2.WaitForResult(), IsOk());
  EXPECT_THAT(callback3.WaitForResult(), IsOk());

  response = trans2.GetResponseInfo();
  ASSERT_TRUE(response);
  ASSERT_TRUE(response->headers);
  EXPECT_EQ("HTTP/1.1 200", response->headers->GetStatusLine());
  EXPECT_TRUE(response->was_fetched_via_spdy);
  EXPECT_TRUE(response->was_alpn_negotiated);
  ASSERT_THAT(ReadTransaction(&trans2, &response_data), IsOk());
  EXPECT_EQ("hello!", response_data);

  response = trans3.GetResponseInfo();
  ASSERT_TRUE(response);
  ASSERT_TRUE(response->headers);
  EXPECT_EQ("HTTP/1.1 200", response->headers->GetStatusLine());
  EXPECT_TRUE(response->was_fetched_via_spdy);
  EXPECT_TRUE(response->was_alpn_negotiated);
  ASSERT_THAT(ReadTransaction(&trans3, &response_data), IsOk());
  EXPECT_EQ("hello!", response_data);
}

TEST_F(HttpNetworkTransactionTest, StallAlternativeServiceForNpnSpdy) {
  session_deps_.host_resolver->set_synchronous_mode(true);

  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("https://www.example.org/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  MockRead data_reads[] = {
      MockRead("HTTP/1.1 200 OK\r\n"),
      MockRead(kAlternativeServiceHttpHeader),
      MockRead("\r\n"),
      MockRead("hello world"),
      MockRead(SYNCHRONOUS, ERR_TEST_PEER_CLOSE_AFTER_NEXT_MOCK_READ),
      MockRead(ASYNC, OK),
  };

  StaticSocketDataProvider first_transaction(data_reads,
                                             base::span<MockWrite>());
  session_deps_.socket_factory->AddSocketDataProvider(&first_transaction);

  SSLSocketDataProvider ssl(ASYNC, OK);
  ssl.ssl_info.cert =
      ImportCertFromFile(GetTestCertsDirectory(), "wildcard.pem");
  ASSERT_TRUE(ssl.ssl_info.cert);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl);

  MockConnect never_finishing_connect(SYNCHRONOUS, ERR_IO_PENDING);
  StaticSocketDataProvider hanging_alternate_protocol_socket;
  hanging_alternate_protocol_socket.set_connect_data(
      never_finishing_connect);
  session_deps_.socket_factory->AddSocketDataProvider(
      &hanging_alternate_protocol_socket);

  // 2nd request is just a copy of the first one, over HTTP/1.1 again.
  StaticSocketDataProvider second_transaction(data_reads,
                                              base::span<MockWrite>());
  session_deps_.socket_factory->AddSocketDataProvider(&second_transaction);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl);

  TestCompletionCallback callback;

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));
  auto trans =
      std::make_unique<HttpNetworkTransaction>(DEFAULT_PRIORITY, session.get());

  int rv = trans->Start(&request, callback.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));
  EXPECT_THAT(callback.WaitForResult(), IsOk());

  const HttpResponseInfo* response = trans->GetResponseInfo();
  ASSERT_TRUE(response);
  ASSERT_TRUE(response->headers);
  EXPECT_EQ("HTTP/1.1 200 OK", response->headers->GetStatusLine());

  std::string response_data;
  ASSERT_THAT(ReadTransaction(trans.get(), &response_data), IsOk());
  EXPECT_EQ("hello world", response_data);

  trans =
      std::make_unique<HttpNetworkTransaction>(DEFAULT_PRIORITY, session.get());

  rv = trans->Start(&request, callback.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));
  EXPECT_THAT(callback.WaitForResult(), IsOk());

  response = trans->GetResponseInfo();
  ASSERT_TRUE(response);
  ASSERT_TRUE(response->headers);
  EXPECT_EQ("HTTP/1.1 200 OK", response->headers->GetStatusLine());
  EXPECT_FALSE(response->was_fetched_via_spdy);
  EXPECT_FALSE(response->was_alpn_negotiated);

  ASSERT_THAT(ReadTransaction(trans.get(), &response_data), IsOk());
  EXPECT_EQ("hello world", response_data);
}

// Test that proxy is resolved using the origin url,
// regardless of the alternative server.
TEST_F(HttpNetworkTransactionTest, UseOriginNotAlternativeForProxy) {
  // Configure proxy to bypass www.example.org, which is the origin URL.
  ProxyConfig proxy_config;
  proxy_config.proxy_rules().ParseFromString("myproxy:70");
  proxy_config.proxy_rules().bypass_rules.AddRuleFromString("www.example.org");
  auto proxy_config_service = std::make_unique<ProxyConfigServiceFixed>(
      ProxyConfigWithAnnotation(proxy_config, TRAFFIC_ANNOTATION_FOR_TESTS));

  CapturingProxyResolver capturing_proxy_resolver;
  auto proxy_resolver_factory = std::make_unique<CapturingProxyResolverFactory>(
      &capturing_proxy_resolver);

  session_deps_.proxy_resolution_service =
      std::make_unique<ConfiguredProxyResolutionService>(
          std::move(proxy_config_service), std::move(proxy_resolver_factory),
          net::NetLog::Get(), /*quick_check_enabled=*/true);

  session_deps_.net_log = net::NetLog::Get();

  // Configure alternative service with a hostname that is not bypassed by the
  // proxy.
  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));
  HttpServerProperties* http_server_properties =
      session->http_server_properties();
  url::SchemeHostPort server("https", "www.example.org", 443);
  HostPortPair alternative("www.example.com", 443);
  AlternativeService alternative_service(kProtoHTTP2, alternative);
  base::Time expiration = base::Time::Now() + base::Days(1);
  http_server_properties->SetHttp2AlternativeService(
      server, NetworkIsolationKey(), alternative_service, expiration);

  // Non-alternative job should hang.
  MockConnect never_finishing_connect(SYNCHRONOUS, ERR_IO_PENDING);
  StaticSocketDataProvider hanging_alternate_protocol_socket;
  hanging_alternate_protocol_socket.set_connect_data(never_finishing_connect);
  session_deps_.socket_factory->AddSocketDataProvider(
      &hanging_alternate_protocol_socket);

  AddSSLSocketData();

  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("https://www.example.org/");
  request.load_flags = 0;
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  spdy::SpdySerializedFrame req(
      spdy_util_.ConstructSpdyGet("https://www.example.org/", 1, LOWEST));

  MockWrite spdy_writes[] = {CreateMockWrite(req, 0)};

  spdy::SpdySerializedFrame resp(
      spdy_util_.ConstructSpdyGetReply(nullptr, 0, 1));
  spdy::SpdySerializedFrame data(spdy_util_.ConstructSpdyDataFrame(1, true));
  MockRead spdy_reads[] = {
      CreateMockRead(resp, 1), CreateMockRead(data, 2), MockRead(ASYNC, 0, 3),
  };

  SequencedSocketData spdy_data(spdy_reads, spdy_writes);
  session_deps_.socket_factory->AddSocketDataProvider(&spdy_data);

  TestCompletionCallback callback;

  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

  int rv = trans.Start(&request, callback.callback(), NetLogWithSource());
  EXPECT_THAT(callback.GetResult(rv), IsOk());

  const HttpResponseInfo* response = trans.GetResponseInfo();
  ASSERT_TRUE(response);
  ASSERT_TRUE(response->headers);
  EXPECT_EQ("HTTP/1.1 200", response->headers->GetStatusLine());
  EXPECT_TRUE(response->was_fetched_via_spdy);
  EXPECT_TRUE(response->was_alpn_negotiated);

  std::string response_data;
  ASSERT_THAT(ReadTransaction(&trans, &response_data), IsOk());
  EXPECT_EQ("hello!", response_data);

  // Origin host bypasses proxy, no resolution should have happened.
  ASSERT_TRUE(capturing_proxy_resolver.lookup_info().empty());
}

TEST_F(HttpNetworkTransactionTest, UseAlternativeServiceForTunneledNpnSpdy) {
  ProxyConfig proxy_config;
  proxy_config.set_auto_detect(true);
  proxy_config.set_pac_url(GURL("http://fooproxyurl"));

  CapturingProxyResolver capturing_proxy_resolver;
  session_deps_.proxy_resolution_service =
      std::make_unique<ConfiguredProxyResolutionService>(
          std::make_unique<ProxyConfigServiceFixed>(ProxyConfigWithAnnotation(
              proxy_config, TRAFFIC_ANNOTATION_FOR_TESTS)),
          std::make_unique<CapturingProxyResolverFactory>(
              &capturing_proxy_resolver),
          nullptr, /*quick_check_enabled=*/true);
  session_deps_.net_log = net::NetLog::Get();

  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("https://www.example.org/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  MockRead data_reads[] = {
      MockRead("HTTP/1.1 200 OK\r\n"),
      MockRead(kAlternativeServiceHttpHeader),
      MockRead("\r\n"),
      MockRead("hello world"),
      MockRead(SYNCHRONOUS, ERR_TEST_PEER_CLOSE_AFTER_NEXT_MOCK_READ),
      MockRead(ASYNC, OK),
  };

  StaticSocketDataProvider first_transaction(data_reads,
                                             base::span<MockWrite>());
  session_deps_.socket_factory->AddSocketDataProvider(&first_transaction);
  SSLSocketDataProvider ssl_http11(ASYNC, OK);
  ssl_http11.next_proto = kProtoHTTP11;
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl_http11);

  AddSSLSocketData();

  spdy::SpdySerializedFrame req(
      spdy_util_.ConstructSpdyGet("https://www.example.org/", 1, LOWEST));
  MockWrite spdy_writes[] = {
      MockWrite(ASYNC, 0,
                "CONNECT www.example.org:443 HTTP/1.1\r\n"
                "Host: www.example.org:443\r\n"
                "Proxy-Connection: keep-alive\r\n\r\n"),
      CreateMockWrite(req, 2),
  };

  const char kCONNECTResponse[] = "HTTP/1.1 200 Connected\r\n\r\n";

  spdy::SpdySerializedFrame resp(
      spdy_util_.ConstructSpdyGetReply(nullptr, 0, 1));
  spdy::SpdySerializedFrame data(spdy_util_.ConstructSpdyDataFrame(1, true));
  MockRead spdy_reads[] = {
      MockRead(ASYNC, 1, kCONNECTResponse), CreateMockRead(resp, 3),
      CreateMockRead(data, 4), MockRead(SYNCHRONOUS, ERR_IO_PENDING, 5),
  };

  SequencedSocketData spdy_data(spdy_reads, spdy_writes);
  session_deps_.socket_factory->AddSocketDataProvider(&spdy_data);

  MockConnect never_finishing_connect(SYNCHRONOUS, ERR_IO_PENDING);
  StaticSocketDataProvider hanging_non_alternate_protocol_socket;
  hanging_non_alternate_protocol_socket.set_connect_data(
      never_finishing_connect);
  session_deps_.socket_factory->AddSocketDataProvider(
      &hanging_non_alternate_protocol_socket);

  TestCompletionCallback callback;

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));
  auto trans =
      std::make_unique<HttpNetworkTransaction>(DEFAULT_PRIORITY, session.get());

  int rv = trans->Start(&request, callback.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));
  EXPECT_THAT(callback.WaitForResult(), IsOk());

  const HttpResponseInfo* response = trans->GetResponseInfo();
  ASSERT_TRUE(response);
  ASSERT_TRUE(response->headers);
  EXPECT_EQ("HTTP/0.9 200 OK", response->headers->GetStatusLine());
  EXPECT_FALSE(response->was_fetched_via_spdy);
  EXPECT_TRUE(response->was_alpn_negotiated);

  std::string response_data;
  ASSERT_THAT(ReadTransaction(trans.get(), &response_data), IsOk());
  EXPECT_EQ("hello world", response_data);

  trans =
      std::make_unique<HttpNetworkTransaction>(DEFAULT_PRIORITY, session.get());

  rv = trans->Start(&request, callback.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));
  EXPECT_THAT(callback.WaitForResult(), IsOk());

  response = trans->GetResponseInfo();
  ASSERT_TRUE(response);
  ASSERT_TRUE(response->headers);
  EXPECT_EQ("HTTP/1.1 200", response->headers->GetStatusLine());
  EXPECT_TRUE(response->was_fetched_via_spdy);
  EXPECT_TRUE(response->was_alpn_negotiated);

  ASSERT_THAT(ReadTransaction(trans.get(), &response_data), IsOk());
  EXPECT_EQ("hello!", response_data);
  ASSERT_EQ(2u, capturing_proxy_resolver.lookup_info().size());
  EXPECT_EQ("https://www.example.org/",
            capturing_proxy_resolver.lookup_info()[0].url.spec());
  EXPECT_EQ("https://www.example.org/",
            capturing_proxy_resolver.lookup_info()[1].url.spec());

  LoadTimingInfo load_timing_info;
  EXPECT_TRUE(trans->GetLoadTimingInfo(&load_timing_info));
  TestLoadTimingNotReusedWithPac(load_timing_info,
                                 CONNECT_TIMING_HAS_SSL_TIMES);
}

TEST_F(HttpNetworkTransactionTest,
       UseAlternativeServiceForNpnSpdyWithExistingSpdySession) {
  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("https://www.example.org/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  MockRead data_reads[] = {
      MockRead("HTTP/1.1 200 OK\r\n"),
      MockRead(kAlternativeServiceHttpHeader),
      MockRead("\r\n"),
      MockRead("hello world"),
      MockRead(ASYNC, OK),
  };

  StaticSocketDataProvider first_transaction(data_reads,
                                             base::span<MockWrite>());
  session_deps_.socket_factory->AddSocketDataProvider(&first_transaction);
  SSLSocketDataProvider ssl_http11(ASYNC, OK);
  ssl_http11.next_proto = kProtoHTTP11;
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl_http11);

  AddSSLSocketData();

  spdy::SpdySerializedFrame req(
      spdy_util_.ConstructSpdyGet("https://www.example.org/", 1, LOWEST));
  MockWrite spdy_writes[] = {CreateMockWrite(req, 0)};

  spdy::SpdySerializedFrame resp(
      spdy_util_.ConstructSpdyGetReply(nullptr, 0, 1));
  spdy::SpdySerializedFrame data(spdy_util_.ConstructSpdyDataFrame(1, true));
  MockRead spdy_reads[] = {
      CreateMockRead(resp, 1), CreateMockRead(data, 2), MockRead(ASYNC, 0, 3),
  };

  SequencedSocketData spdy_data(spdy_reads, spdy_writes);
  session_deps_.socket_factory->AddSocketDataProvider(&spdy_data);

  TestCompletionCallback callback;

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));

  auto trans =
      std::make_unique<HttpNetworkTransaction>(DEFAULT_PRIORITY, session.get());

  int rv = trans->Start(&request, callback.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));
  EXPECT_THAT(callback.WaitForResult(), IsOk());

  const HttpResponseInfo* response = trans->GetResponseInfo();
  ASSERT_TRUE(response);
  ASSERT_TRUE(response->headers);
  EXPECT_EQ("HTTP/1.1 200 OK", response->headers->GetStatusLine());

  std::string response_data;
  ASSERT_THAT(ReadTransaction(trans.get(), &response_data), IsOk());
  EXPECT_EQ("hello world", response_data);

  // Set up an initial SpdySession in the pool to reuse.
  HostPortPair host_port_pair("www.example.org", 443);
  SpdySessionKey key(host_port_pair, ProxyServer::Direct(),
                     PRIVACY_MODE_DISABLED,
                     SpdySessionKey::IsProxySession::kFalse, SocketTag(),
                     NetworkIsolationKey(), SecureDnsPolicy::kAllow);
  base::WeakPtr<SpdySession> spdy_session =
      CreateSpdySession(session.get(), key, NetLogWithSource());

  trans =
      std::make_unique<HttpNetworkTransaction>(DEFAULT_PRIORITY, session.get());

  rv = trans->Start(&request, callback.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));
  EXPECT_THAT(callback.WaitForResult(), IsOk());

  response = trans->GetResponseInfo();
  ASSERT_TRUE(response);
  ASSERT_TRUE(response->headers);
  EXPECT_EQ("HTTP/1.1 200", response->headers->GetStatusLine());
  EXPECT_TRUE(response->was_fetched_via_spdy);
  EXPECT_TRUE(response->was_alpn_negotiated);

  ASSERT_THAT(ReadTransaction(trans.get(), &response_data), IsOk());
  EXPECT_EQ("hello!", response_data);
}

// GenerateAuthToken is a mighty big test.
// It tests all permutation of GenerateAuthToken behavior:
//   - Synchronous and Asynchronous completion.
//   - OK or error on completion.
//   - Direct connection, non-authenticating proxy, and authenticating proxy.
//   - HTTP or HTTPS backend (to include proxy tunneling).
//   - Non-authenticating and authenticating backend.
//
// In all, there are 44 reasonable permuations (for example, if there are
// problems generating an auth token for an authenticating proxy, we don't
// need to test all permutations of the backend server).
//
// The test proceeds by going over each of the configuration cases, and
// potentially running up to three rounds in each of the tests. The TestConfig
// specifies both the configuration for the test as well as the expectations
// for the results.
TEST_F(HttpNetworkTransactionTest, GenerateAuthToken) {
  static const char kServer[] = "http://www.example.com";
  static const char kSecureServer[] = "https://www.example.com";
  static const char kProxy[] = "myproxy:70";

  enum AuthTiming {
    AUTH_NONE,
    AUTH_SYNC,
    AUTH_ASYNC,
  };

  const MockWrite kGet(
      "GET / HTTP/1.1\r\n"
      "Host: www.example.com\r\n"
      "Connection: keep-alive\r\n\r\n");
  const MockWrite kGetProxy(
      "GET http://www.example.com/ HTTP/1.1\r\n"
      "Host: www.example.com\r\n"
      "Proxy-Connection: keep-alive\r\n\r\n");
  const MockWrite kGetAuth(
      "GET / HTTP/1.1\r\n"
      "Host: www.example.com\r\n"
      "Connection: keep-alive\r\n"
      "Authorization: auth_token\r\n\r\n");
  const MockWrite kGetProxyAuth(
      "GET http://www.example.com/ HTTP/1.1\r\n"
      "Host: www.example.com\r\n"
      "Proxy-Connection: keep-alive\r\n"
      "Proxy-Authorization: auth_token\r\n\r\n");
  const MockWrite kGetAuthThroughProxy(
      "GET http://www.example.com/ HTTP/1.1\r\n"
      "Host: www.example.com\r\n"
      "Proxy-Connection: keep-alive\r\n"
      "Authorization: auth_token\r\n\r\n");
  const MockWrite kGetAuthWithProxyAuth(
      "GET http://www.example.com/ HTTP/1.1\r\n"
      "Host: www.example.com\r\n"
      "Proxy-Connection: keep-alive\r\n"
      "Proxy-Authorization: auth_token\r\n"
      "Authorization: auth_token\r\n\r\n");
  const MockWrite kConnect(
      "CONNECT www.example.com:443 HTTP/1.1\r\n"
      "Host: www.example.com:443\r\n"
      "Proxy-Connection: keep-alive\r\n\r\n");
  const MockWrite kConnectProxyAuth(
      "CONNECT www.example.com:443 HTTP/1.1\r\n"
      "Host: www.example.com:443\r\n"
      "Proxy-Connection: keep-alive\r\n"
      "Proxy-Authorization: auth_token\r\n\r\n");

  const MockRead kSuccess(
      "HTTP/1.1 200 OK\r\n"
      "Content-Type: text/html; charset=iso-8859-1\r\n"
      "Content-Length: 3\r\n\r\n"
      "Yes");
  const MockRead kFailure(
      "Should not be called.");
  const MockRead kServerChallenge(
      "HTTP/1.1 401 Unauthorized\r\n"
      "WWW-Authenticate: Mock realm=server\r\n"
      "Content-Type: text/html; charset=iso-8859-1\r\n"
      "Content-Length: 14\r\n\r\n"
      "Unauthorized\r\n");
  const MockRead kProxyChallenge(
      "HTTP/1.1 407 Unauthorized\r\n"
      "Proxy-Authenticate: Mock realm=proxy\r\n"
      "Proxy-Connection: close\r\n"
      "Content-Type: text/html; charset=iso-8859-1\r\n"
      "Content-Length: 14\r\n\r\n"
      "Unauthorized\r\n");
  const MockRead kProxyConnected(
      "HTTP/1.1 200 Connection Established\r\n\r\n");

  // NOTE(cbentzel): I wanted TestReadWriteRound to be a simple struct with
  // no constructors, but the C++ compiler on Windows warns about
  // unspecified data in compound literals. So, moved to using constructors,
  // and TestRound's created with the default constructor should not be used.
  struct TestRound {
    TestRound()
        : expected_rv(ERR_UNEXPECTED),
          extra_write(nullptr),
          extra_read(nullptr) {}
    TestRound(const MockWrite& write_arg,
              const MockRead& read_arg,
              int expected_rv_arg)
        : write(write_arg),
          read(read_arg),
          expected_rv(expected_rv_arg),
          extra_write(nullptr),
          extra_read(nullptr) {}
    TestRound(const MockWrite& write_arg, const MockRead& read_arg,
              int expected_rv_arg, const MockWrite* extra_write_arg,
              const MockRead* extra_read_arg)
        : write(write_arg),
          read(read_arg),
          expected_rv(expected_rv_arg),
          extra_write(extra_write_arg),
          extra_read(extra_read_arg) {
    }
    MockWrite write;
    MockRead read;
    int expected_rv;
    raw_ptr<const MockWrite> extra_write;
    raw_ptr<const MockRead> extra_read;
  };

  static const int kNoSSL = 500;

  struct TestConfig {
    int line_number;
    const char* const proxy_url;
    AuthTiming proxy_auth_timing;
    int first_generate_proxy_token_rv;
    const char* const server_url;
    AuthTiming server_auth_timing;
    int first_generate_server_token_rv;
    int num_auth_rounds;
    int first_ssl_round;
    TestRound rounds[4];
  } test_configs[] = {
      // Non-authenticating HTTP server with a direct connection.
      {__LINE__,
       nullptr,
       AUTH_NONE,
       OK,
       kServer,
       AUTH_NONE,
       OK,
       1,
       kNoSSL,
       {TestRound(kGet, kSuccess, OK)}},
      // Authenticating HTTP server with a direct connection.
      {__LINE__,
       nullptr,
       AUTH_NONE,
       OK,
       kServer,
       AUTH_SYNC,
       OK,
       2,
       kNoSSL,
       {TestRound(kGet, kServerChallenge, OK),
        TestRound(kGetAuth, kSuccess, OK)}},
      {__LINE__,
       nullptr,
       AUTH_NONE,
       OK,
       kServer,
       AUTH_SYNC,
       ERR_INVALID_AUTH_CREDENTIALS,
       3,
       kNoSSL,
       {TestRound(kGet, kServerChallenge, OK),
        TestRound(kGet, kServerChallenge, OK),
        TestRound(kGetAuth, kSuccess, OK)}},
      {__LINE__,
       nullptr,
       AUTH_NONE,
       OK,
       kServer,
       AUTH_SYNC,
       ERR_UNSUPPORTED_AUTH_SCHEME,
       2,
       kNoSSL,
       {TestRound(kGet, kServerChallenge, OK), TestRound(kGet, kSuccess, OK)}},
      {__LINE__,
       nullptr,
       AUTH_NONE,
       OK,
       kServer,
       AUTH_SYNC,
       ERR_UNDOCUMENTED_SECURITY_LIBRARY_STATUS,
       2,
       kNoSSL,
       {TestRound(kGet, kServerChallenge, OK), TestRound(kGet, kSuccess, OK)}},
      {__LINE__,
       kProxy,
       AUTH_SYNC,
       ERR_FAILED,
       kServer,
       AUTH_NONE,
       OK,
       2,
       kNoSSL,
       {TestRound(kGetProxy, kProxyChallenge, OK),
        TestRound(kGetProxy, kFailure, ERR_FAILED)}},
      {__LINE__,
       kProxy,
       AUTH_ASYNC,
       ERR_FAILED,
       kServer,
       AUTH_NONE,
       OK,
       2,
       kNoSSL,
       {TestRound(kGetProxy, kProxyChallenge, OK),
        TestRound(kGetProxy, kFailure, ERR_FAILED)}},
      {__LINE__,
       nullptr,
       AUTH_NONE,
       OK,
       kServer,
       AUTH_SYNC,
       ERR_FAILED,
       2,
       kNoSSL,
       {TestRound(kGet, kServerChallenge, OK),
        TestRound(kGet, kFailure, ERR_FAILED)}},
      {__LINE__,
       nullptr,
       AUTH_NONE,
       OK,
       kServer,
       AUTH_ASYNC,
       ERR_FAILED,
       2,
       kNoSSL,
       {TestRound(kGet, kServerChallenge, OK),
        TestRound(kGet, kFailure, ERR_FAILED)}},
      {__LINE__,
       nullptr,
       AUTH_NONE,
       OK,
       kServer,
       AUTH_ASYNC,
       OK,
       2,
       kNoSSL,
       {TestRound(kGet, kServerChallenge, OK),
        TestRound(kGetAuth, kSuccess, OK)}},
      {__LINE__,
       nullptr,
       AUTH_NONE,
       OK,
       kServer,
       AUTH_ASYNC,
       ERR_INVALID_AUTH_CREDENTIALS,
       3,
       kNoSSL,
       {TestRound(kGet, kServerChallenge, OK),
        // The second round uses a HttpAuthHandlerMock that always succeeds.
        TestRound(kGet, kServerChallenge, OK),
        TestRound(kGetAuth, kSuccess, OK)}},
      // Non-authenticating HTTP server through a non-authenticating proxy.
      {__LINE__,
       kProxy,
       AUTH_NONE,
       OK,
       kServer,
       AUTH_NONE,
       OK,
       1,
       kNoSSL,
       {TestRound(kGetProxy, kSuccess, OK)}},
      // Authenticating HTTP server through a non-authenticating proxy.
      {__LINE__,
       kProxy,
       AUTH_NONE,
       OK,
       kServer,
       AUTH_SYNC,
       OK,
       2,
       kNoSSL,
       {TestRound(kGetProxy, kServerChallenge, OK),
        TestRound(kGetAuthThroughProxy, kSuccess, OK)}},
      {__LINE__,
       kProxy,
       AUTH_NONE,
       OK,
       kServer,
       AUTH_SYNC,
       ERR_INVALID_AUTH_CREDENTIALS,
       3,
       kNoSSL,
       {TestRound(kGetProxy, kServerChallenge, OK),
        TestRound(kGetProxy, kServerChallenge, OK),
        TestRound(kGetAuthThroughProxy, kSuccess, OK)}},
      {__LINE__,
       kProxy,
       AUTH_NONE,
       OK,
       kServer,
       AUTH_ASYNC,
       OK,
       2,
       kNoSSL,
       {TestRound(kGetProxy, kServerChallenge, OK),
        TestRound(kGetAuthThroughProxy, kSuccess, OK)}},
      {__LINE__,
       kProxy,
       AUTH_NONE,
       OK,
       kServer,
       AUTH_ASYNC,
       ERR_INVALID_AUTH_CREDENTIALS,
       2,
       kNoSSL,
       {TestRound(kGetProxy, kServerChallenge, OK),
        TestRound(kGetProxy, kSuccess, OK)}},
      // Non-authenticating HTTP server through an authenticating proxy.
      {__LINE__,
       kProxy,
       AUTH_SYNC,
       OK,
       kServer,
       AUTH_NONE,
       OK,
       2,
       kNoSSL,
       {TestRound(kGetProxy, kProxyChallenge, OK),
        TestRound(kGetProxyAuth, kSuccess, OK)}},
      {__LINE__,
       kProxy,
       AUTH_SYNC,
       ERR_INVALID_AUTH_CREDENTIALS,
       kServer,
       AUTH_NONE,
       OK,
       2,
       kNoSSL,
       {TestRound(kGetProxy, kProxyChallenge, OK),
        TestRound(kGetProxy, kSuccess, OK)}},
      {__LINE__,
       kProxy,
       AUTH_ASYNC,
       OK,
       kServer,
       AUTH_NONE,
       OK,
       2,
       kNoSSL,
       {TestRound(kGetProxy, kProxyChallenge, OK),
        TestRound(kGetProxyAuth, kSuccess, OK)}},
      {__LINE__,
       kProxy,
       AUTH_ASYNC,
       ERR_INVALID_AUTH_CREDENTIALS,
       kServer,
       AUTH_NONE,
       OK,
       2,
       kNoSSL,
       {TestRound(kGetProxy, kProxyChallenge, OK),
        TestRound(kGetProxy, kSuccess, OK)}},
      {__LINE__,
       kProxy,
       AUTH_ASYNC,
       ERR_INVALID_AUTH_CREDENTIALS,
       kServer,
       AUTH_NONE,
       OK,
       3,
       kNoSSL,
       {TestRound(kGetProxy, kProxyChallenge, OK),
        TestRound(kGetProxy, kProxyChallenge, OK),
        TestRound(kGetProxyAuth, kSuccess, OK)}},
      // Authenticating HTTP server through an authenticating proxy.
      {__LINE__,
       kProxy,
       AUTH_SYNC,
       OK,
       kServer,
       AUTH_SYNC,
       OK,
       3,
       kNoSSL,
       {TestRound(kGetProxy, kProxyChallenge, OK),
        TestRound(kGetProxyAuth, kServerChallenge, OK),
        TestRound(kGetAuthWithProxyAuth, kSuccess, OK)}},
      {__LINE__,
       kProxy,
       AUTH_SYNC,
       OK,
       kServer,
       AUTH_SYNC,
       ERR_INVALID_AUTH_CREDENTIALS,
       3,
       kNoSSL,
       {TestRound(kGetProxy, kProxyChallenge, OK),
        TestRound(kGetProxyAuth, kServerChallenge, OK),
        TestRound(kGetProxyAuth, kSuccess, OK)}},
      {__LINE__,
       kProxy,
       AUTH_ASYNC,
       OK,
       kServer,
       AUTH_SYNC,
       OK,
       3,
       kNoSSL,
       {TestRound(kGetProxy, kProxyChallenge, OK),
        TestRound(kGetProxyAuth, kServerChallenge, OK),
        TestRound(kGetAuthWithProxyAuth, kSuccess, OK)}},
      {__LINE__,
       kProxy,
       AUTH_ASYNC,
       OK,
       kServer,
       AUTH_SYNC,
       ERR_INVALID_AUTH_CREDENTIALS,
       3,
       kNoSSL,
       {TestRound(kGetProxy, kProxyChallenge, OK),
        TestRound(kGetProxyAuth, kServerChallenge, OK),
        TestRound(kGetProxyAuth, kSuccess, OK)}},
      {__LINE__,
       kProxy,
       AUTH_SYNC,
       OK,
       kServer,
       AUTH_ASYNC,
       OK,
       3,
       kNoSSL,
       {TestRound(kGetProxy, kProxyChallenge, OK),
        TestRound(kGetProxyAuth, kServerChallenge, OK),
        TestRound(kGetAuthWithProxyAuth, kSuccess, OK)}},
      {__LINE__,
       kProxy,
       AUTH_SYNC,
       ERR_INVALID_AUTH_CREDENTIALS,
       kServer,
       AUTH_ASYNC,
       OK,
       4,
       kNoSSL,
       {TestRound(kGetProxy, kProxyChallenge, OK),
        TestRound(kGetProxy, kProxyChallenge, OK),
        TestRound(kGetProxyAuth, kServerChallenge, OK),
        TestRound(kGetAuthWithProxyAuth, kSuccess, OK)}},
      {__LINE__,
       kProxy,
       AUTH_SYNC,
       OK,
       kServer,
       AUTH_ASYNC,
       ERR_INVALID_AUTH_CREDENTIALS,
       3,
       kNoSSL,
       {TestRound(kGetProxy, kProxyChallenge, OK),
        TestRound(kGetProxyAuth, kServerChallenge, OK),
        TestRound(kGetProxyAuth, kSuccess, OK)}},
      {__LINE__,
       kProxy,
       AUTH_ASYNC,
       OK,
       kServer,
       AUTH_ASYNC,
       OK,
       3,
       kNoSSL,
       {TestRound(kGetProxy, kProxyChallenge, OK),
        TestRound(kGetProxyAuth, kServerChallenge, OK),
        TestRound(kGetAuthWithProxyAuth, kSuccess, OK)}},
      {__LINE__,
       kProxy,
       AUTH_ASYNC,
       OK,
       kServer,
       AUTH_ASYNC,
       ERR_INVALID_AUTH_CREDENTIALS,
       3,
       kNoSSL,
       {TestRound(kGetProxy, kProxyChallenge, OK),
        TestRound(kGetProxyAuth, kServerChallenge, OK),
        TestRound(kGetProxyAuth, kSuccess, OK)}},
      {__LINE__,
       kProxy,
       AUTH_ASYNC,
       ERR_INVALID_AUTH_CREDENTIALS,
       kServer,
       AUTH_ASYNC,
       ERR_INVALID_AUTH_CREDENTIALS,
       4,
       kNoSSL,
       {TestRound(kGetProxy, kProxyChallenge, OK),
        TestRound(kGetProxy, kProxyChallenge, OK),
        TestRound(kGetProxyAuth, kServerChallenge, OK),
        TestRound(kGetProxyAuth, kSuccess, OK)}},
      // Non-authenticating HTTPS server with a direct connection.
      {__LINE__,
       nullptr,
       AUTH_NONE,
       OK,
       kSecureServer,
       AUTH_NONE,
       OK,
       1,
       0,
       {TestRound(kGet, kSuccess, OK)}},
      // Authenticating HTTPS server with a direct connection.
      {__LINE__,
       nullptr,
       AUTH_NONE,
       OK,
       kSecureServer,
       AUTH_SYNC,
       OK,
       2,
       0,
       {TestRound(kGet, kServerChallenge, OK),
        TestRound(kGetAuth, kSuccess, OK)}},
      {__LINE__,
       nullptr,
       AUTH_NONE,
       OK,
       kSecureServer,
       AUTH_SYNC,
       ERR_INVALID_AUTH_CREDENTIALS,
       2,
       0,
       {TestRound(kGet, kServerChallenge, OK), TestRound(kGet, kSuccess, OK)}},
      {__LINE__,
       nullptr,
       AUTH_NONE,
       OK,
       kSecureServer,
       AUTH_ASYNC,
       OK,
       2,
       0,
       {TestRound(kGet, kServerChallenge, OK),
        TestRound(kGetAuth, kSuccess, OK)}},
      {__LINE__,
       nullptr,
       AUTH_NONE,
       OK,
       kSecureServer,
       AUTH_ASYNC,
       ERR_INVALID_AUTH_CREDENTIALS,
       2,
       0,
       {TestRound(kGet, kServerChallenge, OK), TestRound(kGet, kSuccess, OK)}},
      // Non-authenticating HTTPS server with a non-authenticating proxy.
      {__LINE__,
       kProxy,
       AUTH_NONE,
       OK,
       kSecureServer,
       AUTH_NONE,
       OK,
       1,
       0,
       {TestRound(kConnect, kProxyConnected, OK, &kGet, &kSuccess)}},
      // Authenticating HTTPS server through a non-authenticating proxy.
      {__LINE__,
       kProxy,
       AUTH_NONE,
       OK,
       kSecureServer,
       AUTH_SYNC,
       OK,
       2,
       0,
       {TestRound(kConnect, kProxyConnected, OK, &kGet, &kServerChallenge),
        TestRound(kGetAuth, kSuccess, OK)}},
      {__LINE__,
       kProxy,
       AUTH_NONE,
       OK,
       kSecureServer,
       AUTH_SYNC,
       ERR_INVALID_AUTH_CREDENTIALS,
       2,
       0,
       {TestRound(kConnect, kProxyConnected, OK, &kGet, &kServerChallenge),
        TestRound(kGet, kSuccess, OK)}},
      {__LINE__,
       kProxy,
       AUTH_NONE,
       OK,
       kSecureServer,
       AUTH_ASYNC,
       OK,
       2,
       0,
       {TestRound(kConnect, kProxyConnected, OK, &kGet, &kServerChallenge),
        TestRound(kGetAuth, kSuccess, OK)}},
      {__LINE__,
       kProxy,
       AUTH_NONE,
       OK,
       kSecureServer,
       AUTH_ASYNC,
       ERR_INVALID_AUTH_CREDENTIALS,
       2,
       0,
       {TestRound(kConnect, kProxyConnected, OK, &kGet, &kServerChallenge),
        TestRound(kGet, kSuccess, OK)}},
      // Non-Authenticating HTTPS server through an authenticating proxy.
      {__LINE__,
       kProxy,
       AUTH_SYNC,
       OK,
       kSecureServer,
       AUTH_NONE,
       OK,
       2,
       1,
       {TestRound(kConnect, kProxyChallenge, OK),
        TestRound(kConnectProxyAuth, kProxyConnected, OK, &kGet, &kSuccess)}},
      {__LINE__,
       kProxy,
       AUTH_SYNC,
       ERR_INVALID_AUTH_CREDENTIALS,
       kSecureServer,
       AUTH_NONE,
       OK,
       2,
       kNoSSL,
       {TestRound(kConnect, kProxyChallenge, OK),
        TestRound(kConnect, kProxyConnected, OK, &kGet, &kSuccess)}},
      {__LINE__,
       kProxy,
       AUTH_SYNC,
       ERR_UNSUPPORTED_AUTH_SCHEME,
       kSecureServer,
       AUTH_NONE,
       OK,
       2,
       kNoSSL,
       {TestRound(kConnect, kProxyChallenge, OK),
        TestRound(kConnect, kProxyConnected, OK, &kGet, &kSuccess)}},
      {__LINE__,
       kProxy,
       AUTH_SYNC,
       ERR_UNEXPECTED,
       kSecureServer,
       AUTH_NONE,
       OK,
       2,
       kNoSSL,
       {TestRound(kConnect, kProxyChallenge, OK),
        TestRound(kConnect, kProxyConnected, ERR_UNEXPECTED)}},
      {__LINE__,
       kProxy,
       AUTH_ASYNC,
       OK,
       kSecureServer,
       AUTH_NONE,
       OK,
       2,
       1,
       {TestRound(kConnect, kProxyChallenge, OK),
        TestRound(kConnectProxyAuth, kProxyConnected, OK, &kGet, &kSuccess)}},
      {__LINE__,
       kProxy,
       AUTH_ASYNC,
       ERR_INVALID_AUTH_CREDENTIALS,
       kSecureServer,
       AUTH_NONE,
       OK,
       2,
       kNoSSL,
       {TestRound(kConnect, kProxyChallenge, OK),
        TestRound(kConnect, kProxyConnected, OK, &kGet, &kSuccess)}},
      // Authenticating HTTPS server through an authenticating proxy.
      {__LINE__,
       kProxy,
       AUTH_SYNC,
       OK,
       kSecureServer,
       AUTH_SYNC,
       OK,
       3,
       1,
       {TestRound(kConnect, kProxyChallenge, OK),
        TestRound(kConnectProxyAuth, kProxyConnected, OK, &kGet,
                  &kServerChallenge),
        TestRound(kGetAuth, kSuccess, OK)}},
      {__LINE__,
       kProxy,
       AUTH_SYNC,
       OK,
       kSecureServer,
       AUTH_SYNC,
       ERR_INVALID_AUTH_CREDENTIALS,
       3,
       1,
       {TestRound(kConnect, kProxyChallenge, OK),
        TestRound(kConnectProxyAuth, kProxyConnected, OK, &kGet,
                  &kServerChallenge),
        TestRound(kGet, kSuccess, OK)}},
      {__LINE__,
       kProxy,
       AUTH_ASYNC,
       OK,
       kSecureServer,
       AUTH_SYNC,
       OK,
       3,
       1,
       {TestRound(kConnect, kProxyChallenge, OK),
        TestRound(kConnectProxyAuth, kProxyConnected, OK, &kGet,
                  &kServerChallenge),
        TestRound(kGetAuth, kSuccess, OK)}},
      {__LINE__,
       kProxy,
       AUTH_ASYNC,
       OK,
       kSecureServer,
       AUTH_SYNC,
       ERR_INVALID_AUTH_CREDENTIALS,
       3,
       1,
       {TestRound(kConnect, kProxyChallenge, OK),
        TestRound(kConnectProxyAuth, kProxyConnected, OK, &kGet,
                  &kServerChallenge),
        TestRound(kGet, kSuccess, OK)}},
      {__LINE__,
       kProxy,
       AUTH_SYNC,
       OK,
       kSecureServer,
       AUTH_ASYNC,
       OK,
       3,
       1,
       {TestRound(kConnect, kProxyChallenge, OK),
        TestRound(kConnectProxyAuth, kProxyConnected, OK, &kGet,
                  &kServerChallenge),
        TestRound(kGetAuth, kSuccess, OK)}},
      {__LINE__,
       kProxy,
       AUTH_SYNC,
       OK,
       kSecureServer,
       AUTH_ASYNC,
       ERR_INVALID_AUTH_CREDENTIALS,
       3,
       1,
       {TestRound(kConnect, kProxyChallenge, OK),
        TestRound(kConnectProxyAuth, kProxyConnected, OK, &kGet,
                  &kServerChallenge),
        TestRound(kGet, kSuccess, OK)}},
      {__LINE__,
       kProxy,
       AUTH_ASYNC,
       OK,
       kSecureServer,
       AUTH_ASYNC,
       OK,
       3,
       1,
       {TestRound(kConnect, kProxyChallenge, OK),
        TestRound(kConnectProxyAuth, kProxyConnected, OK, &kGet,
                  &kServerChallenge),
        TestRound(kGetAuth, kSuccess, OK)}},
      {__LINE__,
       kProxy,
       AUTH_ASYNC,
       OK,
       kSecureServer,
       AUTH_ASYNC,
       ERR_INVALID_AUTH_CREDENTIALS,
       3,
       1,
       {TestRound(kConnect, kProxyChallenge, OK),
        TestRound(kConnectProxyAuth, kProxyConnected, OK, &kGet,
                  &kServerChallenge),
        TestRound(kGet, kSuccess, OK)}},
      {__LINE__,
       kProxy,
       AUTH_ASYNC,
       ERR_INVALID_AUTH_CREDENTIALS,
       kSecureServer,
       AUTH_ASYNC,
       ERR_INVALID_AUTH_CREDENTIALS,
       4,
       2,
       {TestRound(kConnect, kProxyChallenge, OK),
        TestRound(kConnect, kProxyChallenge, OK),
        TestRound(kConnectProxyAuth, kProxyConnected, OK, &kGet,
                  &kServerChallenge),
        TestRound(kGet, kSuccess, OK)}},
  };

  for (const auto& test_config : test_configs) {
    SCOPED_TRACE(::testing::Message() << "Test config at "
                                      << test_config.line_number);
    auto auth_factory = std::make_unique<HttpAuthHandlerMock::Factory>();
    auto* auth_factory_ptr = auth_factory.get();
    session_deps_.http_auth_handler_factory = std::move(auth_factory);
    SSLInfo empty_ssl_info;

    // Set up authentication handlers as necessary.
    if (test_config.proxy_auth_timing != AUTH_NONE) {
      for (int n = 0; n < 3; n++) {
        auto auth_handler = std::make_unique<HttpAuthHandlerMock>();
        std::string auth_challenge = "Mock realm=proxy";
        url::SchemeHostPort scheme_host_port(GURL(test_config.proxy_url));
        HttpAuthChallengeTokenizer tokenizer(auth_challenge.begin(),
                                             auth_challenge.end());
        auth_handler->InitFromChallenge(&tokenizer, HttpAuth::AUTH_PROXY,
                                        empty_ssl_info, NetworkIsolationKey(),
                                        scheme_host_port, NetLogWithSource());
        auth_handler->SetGenerateExpectation(
            test_config.proxy_auth_timing == AUTH_ASYNC,
            n == 0 ? test_config.first_generate_proxy_token_rv : OK);
        auth_factory_ptr->AddMockHandler(std::move(auth_handler),
                                         HttpAuth::AUTH_PROXY);
      }
    }
    if (test_config.server_auth_timing != AUTH_NONE) {
      auto auth_handler = std::make_unique<HttpAuthHandlerMock>();
      std::string auth_challenge = "Mock realm=server";
      url::SchemeHostPort scheme_host_port(GURL(test_config.server_url));
      HttpAuthChallengeTokenizer tokenizer(auth_challenge.begin(),
                                           auth_challenge.end());
      auth_handler->InitFromChallenge(&tokenizer, HttpAuth::AUTH_SERVER,
                                      empty_ssl_info, NetworkIsolationKey(),
                                      scheme_host_port, NetLogWithSource());
      auth_handler->SetGenerateExpectation(
          test_config.server_auth_timing == AUTH_ASYNC,
          test_config.first_generate_server_token_rv);
      auth_factory_ptr->AddMockHandler(std::move(auth_handler),
                                       HttpAuth::AUTH_SERVER);

      // The second handler always succeeds. It should only be used where there
      // are multiple auth sessions for server auth in the same network
      // transaction using the same auth scheme.
      std::unique_ptr<HttpAuthHandlerMock> second_handler =
          std::make_unique<HttpAuthHandlerMock>();
      second_handler->InitFromChallenge(&tokenizer, HttpAuth::AUTH_SERVER,
                                        empty_ssl_info, NetworkIsolationKey(),
                                        scheme_host_port, NetLogWithSource());
      second_handler->SetGenerateExpectation(true, OK);
      auth_factory_ptr->AddMockHandler(std::move(second_handler),
                                       HttpAuth::AUTH_SERVER);
    }
    if (test_config.proxy_url) {
      session_deps_.proxy_resolution_service =
          ConfiguredProxyResolutionService::CreateFixedForTest(
              test_config.proxy_url, TRAFFIC_ANNOTATION_FOR_TESTS);
    } else {
      session_deps_.proxy_resolution_service =
          ConfiguredProxyResolutionService::CreateDirect();
    }

    HttpRequestInfo request;
    request.method = "GET";
    request.url = GURL(test_config.server_url);
    request.traffic_annotation =
        net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

    std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));

    SSLSocketDataProvider ssl_socket_data_provider(SYNCHRONOUS, OK);

    std::vector<std::vector<MockRead>> mock_reads(1);
    std::vector<std::vector<MockWrite>> mock_writes(1);
    for (int round = 0; round < test_config.num_auth_rounds; ++round) {
      SCOPED_TRACE(round);
      const TestRound& read_write_round = test_config.rounds[round];

      // Set up expected reads and writes.
      mock_reads.back().push_back(read_write_round.read);
      mock_writes.back().push_back(read_write_round.write);

      // kProxyChallenge uses Proxy-Connection: close which means that the
      // socket is closed and a new one will be created for the next request.
      if (read_write_round.read.data == kProxyChallenge.data) {
        mock_reads.emplace_back();
        mock_writes.emplace_back();
      }

      if (read_write_round.extra_read) {
        mock_reads.back().push_back(*read_write_round.extra_read);
      }
      if (read_write_round.extra_write) {
        mock_writes.back().push_back(*read_write_round.extra_write);
      }

      // Add an SSL sequence if necessary.
      if (round >= test_config.first_ssl_round)
        session_deps_.socket_factory->AddSSLSocketDataProvider(
            &ssl_socket_data_provider);
    }

    std::vector<std::unique_ptr<StaticSocketDataProvider>> data_providers;
    for (size_t i = 0; i < mock_reads.size(); ++i) {
      data_providers.push_back(std::make_unique<StaticSocketDataProvider>(
          mock_reads[i], mock_writes[i]));
      session_deps_.socket_factory->AddSocketDataProvider(
          data_providers.back().get());
    }

    // Transaction must be created after DataProviders, so it's destroyed before
    // they are as well.
    HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

    for (int round = 0; round < test_config.num_auth_rounds; ++round) {
      SCOPED_TRACE(round);
      const TestRound& read_write_round = test_config.rounds[round];
      // Start or restart the transaction.
      TestCompletionCallback callback;
      int rv;
      if (round == 0) {
        rv = trans.Start(&request, callback.callback(), NetLogWithSource());
      } else {
        rv = trans.RestartWithAuth(
            AuthCredentials(kFoo, kBar), callback.callback());
      }
      if (rv == ERR_IO_PENDING)
        rv = callback.WaitForResult();

      // Compare results with expected data.
      EXPECT_THAT(rv, IsError(read_write_round.expected_rv));
      const HttpResponseInfo* response = trans.GetResponseInfo();
      if (read_write_round.expected_rv != OK) {
        EXPECT_EQ(round + 1, test_config.num_auth_rounds);
        continue;
      }
      if (round + 1 < test_config.num_auth_rounds) {
        EXPECT_TRUE(response->auth_challenge.has_value());
      } else {
        EXPECT_FALSE(response->auth_challenge.has_value());
        EXPECT_FALSE(trans.IsReadyToRestartForAuth());
      }
    }
  }
}

TEST_F(HttpNetworkTransactionTest, MultiRoundAuth) {
  // Do multi-round authentication and make sure it works correctly.
  auto auth_factory = std::make_unique<HttpAuthHandlerMock::Factory>();
  auto* auth_factory_ptr = auth_factory.get();
  session_deps_.http_auth_handler_factory = std::move(auth_factory);
  session_deps_.proxy_resolution_service =
      ConfiguredProxyResolutionService::CreateDirect();
  session_deps_.host_resolver->rules()->AddRule("www.example.com", "10.0.0.1");

  auto auth_handler = std::make_unique<HttpAuthHandlerMock>();
  auto* auth_handler_ptr = auth_handler.get();
  auth_handler->set_connection_based(true);
  std::string auth_challenge = "Mock realm=server";
  GURL url("http://www.example.com");
  HttpAuthChallengeTokenizer tokenizer(auth_challenge.begin(),
                                       auth_challenge.end());
  SSLInfo empty_ssl_info;
  auth_handler->InitFromChallenge(&tokenizer, HttpAuth::AUTH_SERVER,
                                  empty_ssl_info, NetworkIsolationKey(),
                                  url::SchemeHostPort(url), NetLogWithSource());
  auth_factory_ptr->AddMockHandler(std::move(auth_handler),
                                   HttpAuth::AUTH_SERVER);

  int rv = OK;
  const HttpResponseInfo* response = nullptr;
  HttpRequestInfo request;
  request.method = "GET";
  request.url = url;
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));

  // Use a TCP Socket Pool with only one connection per group. This is used
  // to validate that the TCP socket is not released to the pool between
  // each round of multi-round authentication.
  HttpNetworkSessionPeer session_peer(session.get());
  CommonConnectJobParams common_connect_job_params(
      session->CreateCommonConnectJobParams());
  auto transport_pool = std::make_unique<TransportClientSocketPool>(
      50,                            // Max sockets for pool
      1,                             // Max sockets per group
      base::Seconds(10),             // unused_idle_socket_timeout
      ProxyServer::Direct(), false,  // is_for_websockets
      &common_connect_job_params);
  auto* transport_pool_ptr = transport_pool.get();
  auto mock_pool_manager = std::make_unique<MockClientSocketPoolManager>();
  mock_pool_manager->SetSocketPool(ProxyServer::Direct(),
                                   std::move(transport_pool));
  session_peer.SetClientSocketPoolManager(std::move(mock_pool_manager));

  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());
  TestCompletionCallback callback;

  const MockWrite kGet(
      "GET / HTTP/1.1\r\n"
      "Host: www.example.com\r\n"
      "Connection: keep-alive\r\n\r\n");
  const MockWrite kGetAuth(
      "GET / HTTP/1.1\r\n"
      "Host: www.example.com\r\n"
      "Connection: keep-alive\r\n"
      "Authorization: auth_token\r\n\r\n");

  const MockRead kServerChallenge(
      "HTTP/1.1 401 Unauthorized\r\n"
      "WWW-Authenticate: Mock realm=server\r\n"
      "Content-Type: text/html; charset=iso-8859-1\r\n"
      "Content-Length: 14\r\n\r\n"
      "Unauthorized\r\n");
  const MockRead kSuccess(
      "HTTP/1.1 200 OK\r\n"
      "Content-Type: text/html; charset=iso-8859-1\r\n"
      "Content-Length: 3\r\n\r\n"
      "Yes");

  MockWrite writes[] = {
    // First round
    kGet,
    // Second round
    kGetAuth,
    // Third round
    kGetAuth,
    // Fourth round
    kGetAuth,
    // Competing request
    kGet,
  };
  MockRead reads[] = {
    // First round
    kServerChallenge,
    // Second round
    kServerChallenge,
    // Third round
    kServerChallenge,
    // Fourth round
    kSuccess,
    // Competing response
    kSuccess,
  };
  StaticSocketDataProvider data_provider(reads, writes);
  session_deps_.socket_factory->AddSocketDataProvider(&data_provider);

  const ClientSocketPool::GroupId kSocketGroup(
      url::SchemeHostPort(url::kHttpScheme, "www.example.com", 80),
      PrivacyMode::PRIVACY_MODE_DISABLED, NetworkIsolationKey(),
      SecureDnsPolicy::kAllow);

  // First round of authentication.
  auth_handler_ptr->SetGenerateExpectation(false, OK);
  rv = trans.Start(&request, callback.callback(), NetLogWithSource());
  if (rv == ERR_IO_PENDING)
    rv = callback.WaitForResult();
  EXPECT_THAT(rv, IsOk());
  response = trans.GetResponseInfo();
  ASSERT_TRUE(response);
  EXPECT_TRUE(response->auth_challenge.has_value());
  EXPECT_EQ(0u, transport_pool_ptr->IdleSocketCountInGroup(kSocketGroup));
  EXPECT_EQ(HttpAuthHandlerMock::State::WAIT_FOR_GENERATE_AUTH_TOKEN,
            auth_handler_ptr->state());

  // In between rounds, another request comes in for the same domain.
  // It should not be able to grab the TCP socket that trans has already
  // claimed.
  HttpNetworkTransaction trans_compete(DEFAULT_PRIORITY, session.get());
  TestCompletionCallback callback_compete;
  rv = trans_compete.Start(&request, callback_compete.callback(),
                           NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));
  // callback_compete.WaitForResult at this point would stall forever,
  // since the HttpNetworkTransaction does not release the request back to
  // the pool until after authentication completes.

  // Second round of authentication.
  auth_handler_ptr->SetGenerateExpectation(false, OK);
  rv = trans.RestartWithAuth(AuthCredentials(kFoo, kBar), callback.callback());
  if (rv == ERR_IO_PENDING)
    rv = callback.WaitForResult();
  EXPECT_THAT(rv, IsOk());
  response = trans.GetResponseInfo();
  ASSERT_TRUE(response);
  EXPECT_FALSE(response->auth_challenge.has_value());
  EXPECT_EQ(0u, transport_pool_ptr->IdleSocketCountInGroup(kSocketGroup));
  EXPECT_EQ(HttpAuthHandlerMock::State::WAIT_FOR_GENERATE_AUTH_TOKEN,
            auth_handler_ptr->state());

  // Third round of authentication.
  auth_handler_ptr->SetGenerateExpectation(false, OK);
  rv = trans.RestartWithAuth(AuthCredentials(), callback.callback());
  if (rv == ERR_IO_PENDING)
    rv = callback.WaitForResult();
  EXPECT_THAT(rv, IsOk());
  response = trans.GetResponseInfo();
  ASSERT_TRUE(response);
  EXPECT_FALSE(response->auth_challenge.has_value());
  EXPECT_EQ(0u, transport_pool_ptr->IdleSocketCountInGroup(kSocketGroup));
  EXPECT_EQ(HttpAuthHandlerMock::State::WAIT_FOR_GENERATE_AUTH_TOKEN,
            auth_handler_ptr->state());

  // Fourth round of authentication, which completes successfully.
  auth_handler_ptr->SetGenerateExpectation(false, OK);
  rv = trans.RestartWithAuth(AuthCredentials(), callback.callback());
  if (rv == ERR_IO_PENDING)
    rv = callback.WaitForResult();
  EXPECT_THAT(rv, IsOk());
  response = trans.GetResponseInfo();
  ASSERT_TRUE(response);
  EXPECT_FALSE(response->auth_challenge.has_value());
  EXPECT_EQ(0u, transport_pool_ptr->IdleSocketCountInGroup(kSocketGroup));

  // In WAIT_FOR_CHALLENGE, although in reality the auth handler is done. A real
  // auth handler should transition to a DONE state in concert with the remote
  // server. But that's not something we can test here with a mock handler.
  EXPECT_EQ(HttpAuthHandlerMock::State::WAIT_FOR_CHALLENGE,
            auth_handler_ptr->state());

  // Read the body since the fourth round was successful. This will also
  // release the socket back to the pool.
  scoped_refptr<IOBufferWithSize> io_buf =
      base::MakeRefCounted<IOBufferWithSize>(50);
  rv = trans.Read(io_buf.get(), io_buf->size(), callback.callback());
  if (rv == ERR_IO_PENDING)
    rv = callback.WaitForResult();
  EXPECT_EQ(3, rv);
  rv = trans.Read(io_buf.get(), io_buf->size(), callback.callback());
  EXPECT_EQ(0, rv);
  // There are still 0 idle sockets, since the trans_compete transaction
  // will be handed it immediately after trans releases it to the group.
  EXPECT_EQ(0u, transport_pool_ptr->IdleSocketCountInGroup(kSocketGroup));

  // The competing request can now finish. Wait for the headers and then
  // read the body.
  rv = callback_compete.WaitForResult();
  EXPECT_THAT(rv, IsOk());
  rv = trans_compete.Read(io_buf.get(), io_buf->size(), callback.callback());
  if (rv == ERR_IO_PENDING)
    rv = callback.WaitForResult();
  EXPECT_EQ(3, rv);
  rv = trans_compete.Read(io_buf.get(), io_buf->size(), callback.callback());
  EXPECT_EQ(0, rv);

  // Finally, the socket is released to the group.
  EXPECT_EQ(1u, transport_pool_ptr->IdleSocketCountInGroup(kSocketGroup));
}

// This tests the case that a request is issued via http instead of spdy after
// npn is negotiated.
TEST_F(HttpNetworkTransactionTest, NpnWithHttpOverSSL) {
  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("https://www.example.org/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  MockWrite data_writes[] = {
      MockWrite(
          "GET / HTTP/1.1\r\n"
          "Host: www.example.org\r\n"
          "Connection: keep-alive\r\n\r\n"),
  };

  MockRead data_reads[] = {
      MockRead("HTTP/1.1 200 OK\r\n"),
      MockRead(kAlternativeServiceHttpHeader),
      MockRead("\r\n"),
      MockRead("hello world"),
      MockRead(SYNCHRONOUS, OK),
  };

  SSLSocketDataProvider ssl(ASYNC, OK);
  ssl.next_proto = kProtoHTTP11;

  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl);

  StaticSocketDataProvider data(data_reads, data_writes);
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  TestCompletionCallback callback;

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));
  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

  int rv = trans.Start(&request, callback.callback(), NetLogWithSource());

  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));
  EXPECT_THAT(callback.WaitForResult(), IsOk());

  const HttpResponseInfo* response = trans.GetResponseInfo();
  ASSERT_TRUE(response);
  ASSERT_TRUE(response->headers);
  EXPECT_EQ("HTTP/1.1 200 OK", response->headers->GetStatusLine());

  std::string response_data;
  ASSERT_THAT(ReadTransaction(&trans, &response_data), IsOk());
  EXPECT_EQ("hello world", response_data);

  EXPECT_FALSE(response->was_fetched_via_spdy);
  EXPECT_TRUE(response->was_alpn_negotiated);
}

// Simulate the SSL handshake completing with a ALPN negotiation followed by an
// immediate server closing of the socket.
// Regression test for https://crbug.com/46369.
TEST_F(HttpNetworkTransactionTest, SpdyPostALPNServerHangup) {
  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("https://www.example.org/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  SSLSocketDataProvider ssl(ASYNC, OK);
  ssl.next_proto = kProtoHTTP2;
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl);

  spdy::SpdySerializedFrame req(
      spdy_util_.ConstructSpdyGet(nullptr, 0, 1, LOWEST));
  MockWrite spdy_writes[] = {CreateMockWrite(req, 1)};

  MockRead spdy_reads[] = {
    MockRead(SYNCHRONOUS, 0, 0)   // Not async - return 0 immediately.
  };

  SequencedSocketData spdy_data(spdy_reads, spdy_writes);
  session_deps_.socket_factory->AddSocketDataProvider(&spdy_data);

  TestCompletionCallback callback;

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));
  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

  int rv = trans.Start(&request, callback.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));
  EXPECT_THAT(callback.WaitForResult(), IsError(ERR_CONNECTION_CLOSED));
}

// A subclass of HttpAuthHandlerMock that records the request URL when
// it gets it. This is needed since the auth handler may get destroyed
// before we get a chance to query it.
class UrlRecordingHttpAuthHandlerMock : public HttpAuthHandlerMock {
 public:
  explicit UrlRecordingHttpAuthHandlerMock(GURL* url) : url_(url) {}

  ~UrlRecordingHttpAuthHandlerMock() override = default;

 protected:
  int GenerateAuthTokenImpl(const AuthCredentials* credentials,
                            const HttpRequestInfo* request,
                            CompletionOnceCallback callback,
                            std::string* auth_token) override {
    *url_ = request->url;
    return HttpAuthHandlerMock::GenerateAuthTokenImpl(
        credentials, request, std::move(callback), auth_token);
  }

 private:
  raw_ptr<GURL> url_;
};

// Test that if we cancel the transaction as the connection is completing, that
// everything tears down correctly.
TEST_F(HttpNetworkTransactionTest, SimpleCancel) {
  // Setup everything about the connection to complete synchronously, so that
  // after calling HttpNetworkTransaction::Start, the only thing we're waiting
  // for is the callback from the HttpStreamRequest.
  // Then cancel the transaction.
  // Verify that we don't crash.
  MockConnect mock_connect(SYNCHRONOUS, OK);
  MockRead data_reads[] = {
    MockRead(SYNCHRONOUS, "HTTP/1.0 200 OK\r\n\r\n"),
    MockRead(SYNCHRONOUS, "hello world"),
    MockRead(SYNCHRONOUS, OK),
  };

  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("http://www.example.org/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));
  auto trans =
      std::make_unique<HttpNetworkTransaction>(DEFAULT_PRIORITY, session.get());

  StaticSocketDataProvider data(data_reads, base::span<MockWrite>());
  data.set_connect_data(mock_connect);
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  TestCompletionCallback callback;

  int rv = trans->Start(&request, callback.callback(),
                        NetLogWithSource::Make(NetLogSourceType::NONE));
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));
  trans.reset();  // Cancel the transaction here.

  base::RunLoop().RunUntilIdle();
}

// Test that if a transaction is cancelled after receiving the headers, the
// stream is drained properly and added back to the socket pool.  The main
// purpose of this test is to make sure that an HttpStreamParser can be read
// from after the HttpNetworkTransaction and the objects it owns have been
// deleted.
// See http://crbug.com/368418
TEST_F(HttpNetworkTransactionTest, CancelAfterHeaders) {
  MockRead data_reads[] = {
    MockRead(ASYNC, "HTTP/1.1 200 OK\r\n"),
    MockRead(ASYNC, "Content-Length: 2\r\n"),
    MockRead(ASYNC, "Connection: Keep-Alive\r\n\r\n"),
    MockRead(ASYNC, "1"),
    // 2 async reads are necessary to trigger a ReadResponseBody call after the
    // HttpNetworkTransaction has been deleted.
    MockRead(ASYNC, "2"),
    MockRead(SYNCHRONOUS, ERR_IO_PENDING),  // Should never read this.
  };
  StaticSocketDataProvider data(data_reads, base::span<MockWrite>());
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));

  {
    HttpRequestInfo request;
    request.method = "GET";
    request.url = GURL("http://www.example.org/");
    request.traffic_annotation =
        net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

    HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());
    TestCompletionCallback callback;

    int rv = trans.Start(&request, callback.callback(), NetLogWithSource());
    EXPECT_THAT(rv, IsError(ERR_IO_PENDING));
    callback.WaitForResult();

    const HttpResponseInfo* response = trans.GetResponseInfo();
    ASSERT_TRUE(response);
    EXPECT_TRUE(response->headers);
    EXPECT_EQ("HTTP/1.1 200 OK", response->headers->GetStatusLine());

    // The transaction and HttpRequestInfo are deleted.
  }

  // Let the HttpResponseBodyDrainer drain the socket.
  base::RunLoop().RunUntilIdle();

  // Socket should now be idle, waiting to be reused.
  EXPECT_EQ(1, GetIdleSocketCountInTransportSocketPool(session.get()));
}

// Test a basic GET request through a proxy.
TEST_F(HttpNetworkTransactionTest, ProxyGet) {
  session_deps_.proxy_resolution_service =
      ConfiguredProxyResolutionService::CreateFixedFromPacResultForTest(
          "PROXY myproxy:70", TRAFFIC_ANNOTATION_FOR_TESTS);
  session_deps_.net_log = NetLog::Get();
  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));

  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("http://www.example.org/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  MockWrite data_writes1[] = {
      MockWrite(
          "GET http://www.example.org/ HTTP/1.1\r\n"
          "Host: www.example.org\r\n"
          "Proxy-Connection: keep-alive\r\n\r\n"),
  };

  MockRead data_reads1[] = {
    MockRead("HTTP/1.1 200 OK\r\n"),
    MockRead("Content-Type: text/html; charset=iso-8859-1\r\n"),
    MockRead("Content-Length: 100\r\n\r\n"),
    MockRead(SYNCHRONOUS, OK),
  };

  StaticSocketDataProvider data1(data_reads1, data_writes1);
  session_deps_.socket_factory->AddSocketDataProvider(&data1);

  TestCompletionCallback callback1;

  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());
  ConnectedHandler connected_handler;
  trans.SetConnectedCallback(connected_handler.Callback());

  int rv = trans.Start(&request, callback1.callback(),
                       NetLogWithSource::Make(NetLogSourceType::NONE));
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback1.WaitForResult();
  EXPECT_THAT(rv, IsOk());

  const HttpResponseInfo* response = trans.GetResponseInfo();
  ASSERT_TRUE(response);

  EXPECT_TRUE(response->headers->IsKeepAlive());
  EXPECT_EQ(200, response->headers->response_code());
  EXPECT_EQ(100, response->headers->GetContentLength());
  EXPECT_TRUE(response->was_fetched_via_proxy);
  EXPECT_EQ(ProxyServer(ProxyServer::SCHEME_HTTP,
                        HostPortPair::FromString("myproxy:70")),
            response->proxy_server);
  EXPECT_TRUE(HttpVersion(1, 1) == response->headers->GetHttpVersion());

  TransportInfo expected_transport;
  expected_transport.type = TransportType::kProxied;
  expected_transport.endpoint = IPEndPoint(IPAddress::IPv4Localhost(), 70);
  EXPECT_THAT(connected_handler.transports(), ElementsAre(expected_transport));

  LoadTimingInfo load_timing_info;
  EXPECT_TRUE(trans.GetLoadTimingInfo(&load_timing_info));
  TestLoadTimingNotReusedWithPac(load_timing_info,
                                 CONNECT_TIMING_HAS_CONNECT_TIMES_ONLY);
}

// Test a basic HTTPS GET request through a proxy.
TEST_F(HttpNetworkTransactionTest, ProxyTunnelGet) {
  session_deps_.proxy_resolution_service =
      ConfiguredProxyResolutionService::CreateFixedFromPacResultForTest(
          "PROXY myproxy:70", TRAFFIC_ANNOTATION_FOR_TESTS);
  RecordingNetLogObserver net_log_observer;
  session_deps_.net_log = NetLog::Get();
  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));

  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("https://www.example.org/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  // Since we have proxy, should try to establish tunnel.
  MockWrite data_writes1[] = {
      MockWrite("CONNECT www.example.org:443 HTTP/1.1\r\n"
                "Host: www.example.org:443\r\n"
                "Proxy-Connection: keep-alive\r\n\r\n"),

      MockWrite("GET / HTTP/1.1\r\n"
                "Host: www.example.org\r\n"
                "Connection: keep-alive\r\n\r\n"),
  };

  MockRead data_reads1[] = {
    MockRead("HTTP/1.1 200 Connection Established\r\n\r\n"),

    MockRead("HTTP/1.1 200 OK\r\n"),
    MockRead("Content-Type: text/html; charset=iso-8859-1\r\n"),
    MockRead("Content-Length: 100\r\n\r\n"),
    MockRead(SYNCHRONOUS, OK),
  };

  StaticSocketDataProvider data1(data_reads1, data_writes1);
  session_deps_.socket_factory->AddSocketDataProvider(&data1);
  SSLSocketDataProvider ssl(ASYNC, OK);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl);

  TestCompletionCallback callback1;

  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());
  ConnectedHandler connected_handler;
  trans.SetConnectedCallback(connected_handler.Callback());

  int rv = trans.Start(&request, callback1.callback(),
                       NetLogWithSource::Make(NetLogSourceType::NONE));
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback1.WaitForResult();
  EXPECT_THAT(rv, IsOk());
  auto entries = net_log_observer.GetEntries();
  size_t pos = ExpectLogContainsSomewhere(
      entries, 0, NetLogEventType::HTTP_TRANSACTION_SEND_TUNNEL_HEADERS,
      NetLogEventPhase::NONE);
  ExpectLogContainsSomewhere(
      entries, pos,
      NetLogEventType::HTTP_TRANSACTION_READ_TUNNEL_RESPONSE_HEADERS,
      NetLogEventPhase::NONE);

  const HttpResponseInfo* response = trans.GetResponseInfo();
  ASSERT_TRUE(response);

  EXPECT_TRUE(response->headers->IsKeepAlive());
  EXPECT_EQ(200, response->headers->response_code());
  EXPECT_EQ(100, response->headers->GetContentLength());
  EXPECT_TRUE(HttpVersion(1, 1) == response->headers->GetHttpVersion());
  EXPECT_TRUE(response->was_fetched_via_proxy);
  EXPECT_EQ(ProxyServer(ProxyServer::SCHEME_HTTP,
                        HostPortPair::FromString("myproxy:70")),
            response->proxy_server);

  TransportInfo expected_transport;
  expected_transport.type = TransportType::kProxied;
  expected_transport.endpoint = IPEndPoint(IPAddress::IPv4Localhost(), 70);
  EXPECT_THAT(connected_handler.transports(), ElementsAre(expected_transport));

  LoadTimingInfo load_timing_info;
  EXPECT_TRUE(trans.GetLoadTimingInfo(&load_timing_info));
  TestLoadTimingNotReusedWithPac(load_timing_info,
                                 CONNECT_TIMING_HAS_SSL_TIMES);
}

// Test a basic HTTPS GET request through a proxy, connecting to an IPv6
// literal host.
TEST_F(HttpNetworkTransactionTest, ProxyTunnelGetIPv6) {
  session_deps_.proxy_resolution_service =
      ConfiguredProxyResolutionService::CreateFixedFromPacResultForTest(
          "PROXY myproxy:70", TRAFFIC_ANNOTATION_FOR_TESTS);
  RecordingNetLogObserver net_log_observer;
  session_deps_.net_log = NetLog::Get();
  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));

  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("https://[::2]:443/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  // Since we have proxy, should try to establish tunnel.
  MockWrite data_writes1[] = {
      MockWrite("CONNECT [::2]:443 HTTP/1.1\r\n"
                "Host: [::2]:443\r\n"
                "Proxy-Connection: keep-alive\r\n\r\n"),

      MockWrite("GET / HTTP/1.1\r\n"
                "Host: [::2]\r\n"
                "Connection: keep-alive\r\n\r\n"),
  };

  MockRead data_reads1[] = {
      MockRead("HTTP/1.1 200 Connection Established\r\n\r\n"),

      MockRead("HTTP/1.1 200 OK\r\n"),
      MockRead("Content-Type: text/html; charset=iso-8859-1\r\n"),
      MockRead("Content-Length: 100\r\n\r\n"),
      MockRead(SYNCHRONOUS, OK),
  };

  StaticSocketDataProvider data1(data_reads1, data_writes1);
  session_deps_.socket_factory->AddSocketDataProvider(&data1);
  SSLSocketDataProvider ssl(ASYNC, OK);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl);

  TestCompletionCallback callback1;

  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

  int rv = trans.Start(&request, callback1.callback(),
                       NetLogWithSource::Make(NetLogSourceType::NONE));
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback1.WaitForResult();
  EXPECT_THAT(rv, IsOk());
  auto entries = net_log_observer.GetEntries();
  size_t pos = ExpectLogContainsSomewhere(
      entries, 0, NetLogEventType::HTTP_TRANSACTION_SEND_TUNNEL_HEADERS,
      NetLogEventPhase::NONE);
  ExpectLogContainsSomewhere(
      entries, pos,
      NetLogEventType::HTTP_TRANSACTION_READ_TUNNEL_RESPONSE_HEADERS,
      NetLogEventPhase::NONE);

  const HttpResponseInfo* response = trans.GetResponseInfo();
  ASSERT_TRUE(response);

  EXPECT_TRUE(response->headers->IsKeepAlive());
  EXPECT_EQ(200, response->headers->response_code());
  EXPECT_EQ(100, response->headers->GetContentLength());
  EXPECT_TRUE(HttpVersion(1, 1) == response->headers->GetHttpVersion());
  EXPECT_TRUE(response->was_fetched_via_proxy);
  EXPECT_EQ(ProxyServer(ProxyServer::SCHEME_HTTP,
                        HostPortPair::FromString("myproxy:70")),
            response->proxy_server);

  LoadTimingInfo load_timing_info;
  EXPECT_TRUE(trans.GetLoadTimingInfo(&load_timing_info));
  TestLoadTimingNotReusedWithPac(load_timing_info,
                                 CONNECT_TIMING_HAS_SSL_TIMES);
}

// Test a basic HTTPS GET request through a proxy, but the server hangs up
// while establishing the tunnel.
TEST_F(HttpNetworkTransactionTest, ProxyTunnelGetHangup) {
  session_deps_.proxy_resolution_service =
      ConfiguredProxyResolutionService::CreateFixedForTest(
          "myproxy:70", TRAFFIC_ANNOTATION_FOR_TESTS);
  RecordingNetLogObserver net_log_observer;
  session_deps_.net_log = NetLog::Get();
  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));

  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("https://www.example.org/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  // Since we have proxy, should try to establish tunnel.
  MockWrite data_writes1[] = {
      MockWrite("CONNECT www.example.org:443 HTTP/1.1\r\n"
                "Host: www.example.org:443\r\n"
                "Proxy-Connection: keep-alive\r\n\r\n"),

      MockWrite("GET / HTTP/1.1\r\n"
                "Host: www.example.org\r\n"
                "Connection: keep-alive\r\n\r\n"),
  };

  MockRead data_reads1[] = {
    MockRead("HTTP/1.1 200 Connection Established\r\n\r\n"),
    MockRead(ASYNC, 0, 0),  // EOF
  };

  StaticSocketDataProvider data1(data_reads1, data_writes1);
  session_deps_.socket_factory->AddSocketDataProvider(&data1);
  SSLSocketDataProvider ssl(ASYNC, OK);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl);

  TestCompletionCallback callback1;

  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

  int rv = trans.Start(&request, callback1.callback(),
                       NetLogWithSource::Make(NetLogSourceType::NONE));
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback1.WaitForResult();
  EXPECT_THAT(rv, IsError(ERR_EMPTY_RESPONSE));
  auto entries = net_log_observer.GetEntries();
  size_t pos = ExpectLogContainsSomewhere(
      entries, 0, NetLogEventType::HTTP_TRANSACTION_SEND_TUNNEL_HEADERS,
      NetLogEventPhase::NONE);
  ExpectLogContainsSomewhere(
      entries, pos,
      NetLogEventType::HTTP_TRANSACTION_READ_TUNNEL_RESPONSE_HEADERS,
      NetLogEventPhase::NONE);
}

// Test for crbug.com/55424.
TEST_F(HttpNetworkTransactionTest, PreconnectWithExistingSpdySession) {
  spdy::SpdySerializedFrame req(
      spdy_util_.ConstructSpdyGet("https://www.example.org", 1, LOWEST));
  MockWrite spdy_writes[] = {CreateMockWrite(req, 0)};

  spdy::SpdySerializedFrame resp(
      spdy_util_.ConstructSpdyGetReply(nullptr, 0, 1));
  spdy::SpdySerializedFrame data(spdy_util_.ConstructSpdyDataFrame(1, true));
  MockRead spdy_reads[] = {
      CreateMockRead(resp, 1), CreateMockRead(data, 2), MockRead(ASYNC, 0, 3),
  };

  SequencedSocketData spdy_data(spdy_reads, spdy_writes);
  session_deps_.socket_factory->AddSocketDataProvider(&spdy_data);

  SSLSocketDataProvider ssl(ASYNC, OK);
  ssl.next_proto = kProtoHTTP2;
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl);

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));

  // Set up an initial SpdySession in the pool to reuse.
  HostPortPair host_port_pair("www.example.org", 443);
  SpdySessionKey key(host_port_pair, ProxyServer::Direct(),
                     PRIVACY_MODE_DISABLED,
                     SpdySessionKey::IsProxySession::kFalse, SocketTag(),
                     NetworkIsolationKey(), SecureDnsPolicy::kAllow);
  base::WeakPtr<SpdySession> spdy_session =
      CreateSpdySession(session.get(), key, NetLogWithSource());

  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("https://www.example.org/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

  TestCompletionCallback callback;
  int rv = trans.Start(&request, callback.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));
  EXPECT_THAT(callback.WaitForResult(), IsOk());
}

// Given a net error, cause that error to be returned from the first Write()
// call and verify that the HttpNetworkTransaction fails with that error.
void HttpNetworkTransactionTest::CheckErrorIsPassedBack(
    int error, IoMode mode) {
  HttpRequestInfo request_info;
  request_info.url = GURL("https://www.example.com/");
  request_info.method = "GET";
  request_info.load_flags = LOAD_NORMAL;
  request_info.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  SSLSocketDataProvider ssl_data(mode, OK);
  MockWrite data_writes[] = {
      MockWrite(mode, error),
  };
  StaticSocketDataProvider data(base::span<MockRead>(), data_writes);
  session_deps_.socket_factory->AddSocketDataProvider(&data);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl_data);

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));
  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

  TestCompletionCallback callback;
  int rv = trans.Start(&request_info, callback.callback(), NetLogWithSource());
  if (rv == ERR_IO_PENDING)
    rv = callback.WaitForResult();
  ASSERT_EQ(error, rv);
}

TEST_F(HttpNetworkTransactionTest, SSLWriteCertError) {
  // Just check a grab bag of cert errors.
  static const int kErrors[] = {
    ERR_CERT_COMMON_NAME_INVALID,
    ERR_CERT_AUTHORITY_INVALID,
    ERR_CERT_DATE_INVALID,
  };
  for (int error : kErrors) {
    CheckErrorIsPassedBack(error, ASYNC);
    CheckErrorIsPassedBack(error, SYNCHRONOUS);
  }
}

// Ensure that a client certificate is removed from the SSL client auth
// cache when:
//  1) No proxy is involved.
//  2) TLS False Start is disabled.
//  3) The initial TLS handshake requests a client certificate.
//  4) The client supplies an invalid/unacceptable certificate.
TEST_F(HttpNetworkTransactionTest, ClientAuthCertCache_Direct_NoFalseStart) {
  HttpRequestInfo request_info;
  request_info.url = GURL("https://www.example.com/");
  request_info.method = "GET";
  request_info.load_flags = LOAD_NORMAL;
  request_info.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  auto cert_request = base::MakeRefCounted<SSLCertRequestInfo>();
  cert_request->host_and_port = HostPortPair("www.example.com", 443);

  // [ssl_]data1 contains the data for the first SSL handshake. When a
  // CertificateRequest is received for the first time, the handshake will
  // be aborted to allow the caller to provide a certificate.
  SSLSocketDataProvider ssl_data1(ASYNC, ERR_SSL_CLIENT_AUTH_CERT_NEEDED);
  ssl_data1.cert_request_info = cert_request.get();
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl_data1);
  StaticSocketDataProvider data1;
  session_deps_.socket_factory->AddSocketDataProvider(&data1);

  // [ssl_]data2 contains the data for the second SSL handshake. When TLS
  // False Start is not being used, the result of the SSL handshake will be
  // returned as part of the SSLClientSocket::Connect() call. This test
  // matches the result of a server sending a handshake_failure alert,
  // rather than a Finished message, because it requires a client
  // certificate and none was supplied.
  SSLSocketDataProvider ssl_data2(ASYNC, ERR_SSL_PROTOCOL_ERROR);
  ssl_data2.cert_request_info = cert_request.get();
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl_data2);
  StaticSocketDataProvider data2;
  session_deps_.socket_factory->AddSocketDataProvider(&data2);

  // [ssl_]data3 contains the data for the third SSL handshake. When a
  // connection to a server fails during an SSL handshake,
  // HttpNetworkTransaction will attempt to fallback with legacy cryptography
  // enabled on some errors. This is transparent to the caller
  // of the HttpNetworkTransaction. Because this test failure is due to
  // requiring a client certificate, this fallback handshake should also
  // fail.
  SSLSocketDataProvider ssl_data3(ASYNC, ERR_SSL_PROTOCOL_ERROR);
  ssl_data3.expected_disable_legacy_crypto = false;
  ssl_data3.cert_request_info = cert_request.get();
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl_data3);
  StaticSocketDataProvider data3;
  session_deps_.socket_factory->AddSocketDataProvider(&data3);

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));
  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

  // Begin the SSL handshake with the peer. This consumes ssl_data1.
  TestCompletionCallback callback;
  int rv = trans.Start(&request_info, callback.callback(), NetLogWithSource());
  ASSERT_THAT(rv, IsError(ERR_IO_PENDING));

  // Complete the SSL handshake, which should abort due to requiring a
  // client certificate.
  rv = callback.WaitForResult();
  ASSERT_THAT(rv, IsError(ERR_SSL_CLIENT_AUTH_CERT_NEEDED));

  // Indicate that no certificate should be supplied. From the perspective
  // of SSLClientCertCache, NULL is just as meaningful as a real
  // certificate, so this is the same as supply a
  // legitimate-but-unacceptable certificate.
  rv = trans.RestartWithCertificate(nullptr, nullptr, callback.callback());
  ASSERT_THAT(rv, IsError(ERR_IO_PENDING));

  // Ensure the certificate was added to the client auth cache before
  // allowing the connection to continue restarting.
  scoped_refptr<X509Certificate> client_cert;
  scoped_refptr<SSLPrivateKey> client_private_key;
  ASSERT_TRUE(session->ssl_client_context()->GetClientCertificate(
      HostPortPair("www.example.com", 443), &client_cert, &client_private_key));
  ASSERT_FALSE(client_cert);

  // Restart the handshake. This will consume ssl_data2, which fails, and
  // then consume ssl_data3 and ssl_data4, both of which should also fail.
  // The result code is checked against what ssl_data4 should return.
  rv = callback.WaitForResult();
  ASSERT_THAT(rv, IsError(ERR_SSL_PROTOCOL_ERROR));

  // Ensure that the client certificate is removed from the cache on a
  // handshake failure.
  ASSERT_FALSE(session->ssl_client_context()->GetClientCertificate(
      HostPortPair("www.example.com", 443), &client_cert, &client_private_key));
}

// Ensure that a client certificate is removed from the SSL client auth
// cache when:
//  1) No proxy is involved.
//  2) TLS False Start is enabled.
//  3) The initial TLS handshake requests a client certificate.
//  4) The client supplies an invalid/unacceptable certificate.
TEST_F(HttpNetworkTransactionTest, ClientAuthCertCache_Direct_FalseStart) {
  HttpRequestInfo request_info;
  request_info.url = GURL("https://www.example.com/");
  request_info.method = "GET";
  request_info.load_flags = LOAD_NORMAL;
  request_info.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  auto cert_request = base::MakeRefCounted<SSLCertRequestInfo>();
  cert_request->host_and_port = HostPortPair("www.example.com", 443);

  // When TLS False Start is used, SSLClientSocket::Connect() calls will
  // return successfully after reading up to the peer's Certificate message.
  // This is to allow the caller to call SSLClientSocket::Write(), which can
  // enqueue application data to be sent in the same packet as the
  // ChangeCipherSpec and Finished messages.
  // The actual handshake will be finished when SSLClientSocket::Read() is
  // called, which expects to process the peer's ChangeCipherSpec and
  // Finished messages. If there was an error negotiating with the peer,
  // such as due to the peer requiring a client certificate when none was
  // supplied, the alert sent by the peer won't be processed until Read() is
  // called.

  // Like the non-False Start case, when a client certificate is requested by
  // the peer, the handshake is aborted during the Connect() call.
  // [ssl_]data1 represents the initial SSL handshake with the peer.
  SSLSocketDataProvider ssl_data1(ASYNC, ERR_SSL_CLIENT_AUTH_CERT_NEEDED);
  ssl_data1.cert_request_info = cert_request.get();
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl_data1);
  StaticSocketDataProvider data1;
  session_deps_.socket_factory->AddSocketDataProvider(&data1);

  // When a client certificate is supplied, Connect() will not be aborted
  // when the peer requests the certificate. Instead, the handshake will
  // artificially succeed, allowing the caller to write the HTTP request to
  // the socket. The handshake messages are not processed until Read() is
  // called, which then detects that the handshake was aborted, due to the
  // peer sending a handshake_failure because it requires a client
  // certificate.
  SSLSocketDataProvider ssl_data2(ASYNC, OK);
  ssl_data2.cert_request_info = cert_request.get();
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl_data2);
  MockRead data2_reads[] = {
      MockRead(ASYNC /* async */, ERR_SSL_PROTOCOL_ERROR),
  };
  StaticSocketDataProvider data2(data2_reads, base::span<MockWrite>());
  session_deps_.socket_factory->AddSocketDataProvider(&data2);

  // As described in ClientAuthCertCache_Direct_NoFalseStart, [ssl_]data3 is
  // the data for the SSL handshake once the TLSv1.1 connection falls back to
  // TLSv1. It has the same behaviour as [ssl_]data2.
  SSLSocketDataProvider ssl_data3(ASYNC, OK);
  ssl_data3.cert_request_info = cert_request.get();
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl_data3);
  StaticSocketDataProvider data3(data2_reads, base::span<MockWrite>());
  session_deps_.socket_factory->AddSocketDataProvider(&data3);

  // [ssl_]data4 is the data for the SSL handshake once the TLSv1 connection
  // falls back to SSLv3. It has the same behaviour as [ssl_]data2.
  SSLSocketDataProvider ssl_data4(ASYNC, OK);
  ssl_data4.cert_request_info = cert_request.get();
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl_data4);
  StaticSocketDataProvider data4(data2_reads, base::span<MockWrite>());
  session_deps_.socket_factory->AddSocketDataProvider(&data4);

  // Need one more if TLSv1.2 is enabled.
  SSLSocketDataProvider ssl_data5(ASYNC, OK);
  ssl_data5.cert_request_info = cert_request.get();
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl_data5);
  StaticSocketDataProvider data5(data2_reads, base::span<MockWrite>());
  session_deps_.socket_factory->AddSocketDataProvider(&data5);

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));
  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

  // Begin the initial SSL handshake.
  TestCompletionCallback callback;
  int rv = trans.Start(&request_info, callback.callback(), NetLogWithSource());
  ASSERT_THAT(rv, IsError(ERR_IO_PENDING));

  // Complete the SSL handshake, which should abort due to requiring a
  // client certificate.
  rv = callback.WaitForResult();
  ASSERT_THAT(rv, IsError(ERR_SSL_CLIENT_AUTH_CERT_NEEDED));

  // Indicate that no certificate should be supplied. From the perspective
  // of SSLClientCertCache, NULL is just as meaningful as a real
  // certificate, so this is the same as supply a
  // legitimate-but-unacceptable certificate.
  rv = trans.RestartWithCertificate(nullptr, nullptr, callback.callback());
  ASSERT_THAT(rv, IsError(ERR_IO_PENDING));

  // Ensure the certificate was added to the client auth cache before
  // allowing the connection to continue restarting.
  scoped_refptr<X509Certificate> client_cert;
  scoped_refptr<SSLPrivateKey> client_private_key;
  ASSERT_TRUE(session->ssl_client_context()->GetClientCertificate(
      HostPortPair("www.example.com", 443), &client_cert, &client_private_key));
  ASSERT_FALSE(client_cert);

  // Restart the handshake. This will consume ssl_data2, which fails, and
  // then consume ssl_data3 and ssl_data4, both of which should also fail.
  // The result code is checked against what ssl_data4 should return.
  rv = callback.WaitForResult();
  ASSERT_THAT(rv, IsError(ERR_SSL_PROTOCOL_ERROR));

  // Ensure that the client certificate is removed from the cache on a
  // handshake failure.
  ASSERT_FALSE(session->ssl_client_context()->GetClientCertificate(
      HostPortPair("www.example.com", 443), &client_cert, &client_private_key));
}

// Ensure that a client certificate is removed from the SSL client auth
// cache when:
//  1) An HTTPS proxy is involved.
//  3) The HTTPS proxy requests a client certificate.
//  4) The client supplies an invalid/unacceptable certificate for the
//     proxy.
TEST_F(HttpNetworkTransactionTest, ClientAuthCertCache_Proxy_Fail) {
  session_deps_.proxy_resolution_service =
      ConfiguredProxyResolutionService::CreateFixedForTest(
          "https://proxy:70", TRAFFIC_ANNOTATION_FOR_TESTS);
  session_deps_.net_log = NetLog::Get();

  auto cert_request = base::MakeRefCounted<SSLCertRequestInfo>();
  cert_request->host_and_port = HostPortPair("proxy", 70);

  // Repeat the test for connecting to an HTTPS endpoint, then for connecting to
  // an HTTP endpoint.
  HttpRequestInfo requests[2];
  requests[0].url = GURL("https://www.example.com/");
  requests[0].method = "GET";
  requests[0].load_flags = LOAD_NORMAL;
  requests[0].traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  // HTTPS requests are tunneled.
  MockWrite https_writes[] = {
      MockWrite("CONNECT www.example.com:443 HTTP/1.1\r\n"
                "Host: www.example.com:443\r\n"
                "Proxy-Connection: keep-alive\r\n\r\n"),
  };

  requests[1].url = GURL("http://www.example.com/");
  requests[1].method = "GET";
  requests[1].load_flags = LOAD_NORMAL;
  requests[1].traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  // HTTP requests are not.
  MockWrite http_writes[] = {
      MockWrite("GET http://www.example.com/ HTTP/1.1\r\n"
                "Host: www.example.com\r\n"
                "Proxy-Connection: keep-alive\r\n\r\n"),
  };

  // When the server rejects the client certificate, it will close the
  // connection. In TLS 1.2, this is signaled out of Connect(). In TLS 1.3 (or
  // TLS 1.2 with False Start), the error is returned out of the first Read().
  for (bool reject_in_connect : {true, false}) {
    SCOPED_TRACE(reject_in_connect);
    // Client certificate errors are typically signaled with
    // ERR_BAD_SSL_CLIENT_AUTH_CERT, but sometimes the server gives an arbitrary
    // protocol error.
    for (Error reject_error :
         {ERR_SSL_PROTOCOL_ERROR, ERR_BAD_SSL_CLIENT_AUTH_CERT}) {
      SCOPED_TRACE(reject_error);
      // Tunneled and non-tunneled requests are handled differently. Test both.
      for (const HttpRequestInfo& request : requests) {
        SCOPED_TRACE(request.url);

        session_deps_.socket_factory =
            std::make_unique<MockClientSocketFactory>();

        // See ClientAuthCertCache_Direct_NoFalseStart for the explanation of
        // [ssl_]data[1-3].
        SSLSocketDataProvider ssl_data1(ASYNC, ERR_SSL_CLIENT_AUTH_CERT_NEEDED);
        ssl_data1.cert_request_info = cert_request.get();
        session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl_data1);
        StaticSocketDataProvider data1;
        session_deps_.socket_factory->AddSocketDataProvider(&data1);

        absl::optional<SSLSocketDataProvider> ssl_data2;
        absl::optional<StaticSocketDataProvider> data2;
        MockRead error_in_read[] = {MockRead(ASYNC, reject_error)};
        if (reject_in_connect) {
          ssl_data2.emplace(ASYNC, reject_error);
          // There are no reads or writes.
          data2.emplace();
        } else {
          ssl_data2.emplace(ASYNC, OK);
          // We will get one Write() in before observing the error in Read().
          if (request.url.SchemeIsCryptographic()) {
            data2.emplace(error_in_read, https_writes);
          } else {
            data2.emplace(error_in_read, http_writes);
          }
        }
        ssl_data2->cert_request_info = cert_request.get();
        session_deps_.socket_factory->AddSSLSocketDataProvider(
            &ssl_data2.value());
        session_deps_.socket_factory->AddSocketDataProvider(&data2.value());

        // If the handshake returns ERR_SSL_PROTOCOL_ERROR, we attempt to
        // connect twice.
        absl::optional<SSLSocketDataProvider> ssl_data3;
        absl::optional<StaticSocketDataProvider> data3;
        if (reject_in_connect && reject_error == ERR_SSL_PROTOCOL_ERROR) {
          ssl_data3.emplace(ASYNC, reject_error);
          data3.emplace();  // There are no reads or writes.
          ssl_data3->cert_request_info = cert_request.get();
          session_deps_.socket_factory->AddSSLSocketDataProvider(
              &ssl_data3.value());
          session_deps_.socket_factory->AddSocketDataProvider(&data3.value());
        }

        std::unique_ptr<HttpNetworkSession> session =
            CreateSession(&session_deps_);
        HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

        // Begin the SSL handshake with the proxy.
        TestCompletionCallback callback;
        int rv = trans.Start(&request, callback.callback(), NetLogWithSource());
        ASSERT_THAT(rv, IsError(ERR_IO_PENDING));

        // Complete the SSL handshake, which should abort due to requiring a
        // client certificate.
        rv = callback.WaitForResult();
        ASSERT_THAT(rv, IsError(ERR_SSL_CLIENT_AUTH_CERT_NEEDED));

        // Indicate that no certificate should be supplied. From the
        // perspective of SSLClientCertCache, NULL is just as meaningful as a
        // real certificate, so this is the same as supply a
        // legitimate-but-unacceptable certificate.
        rv =
            trans.RestartWithCertificate(nullptr, nullptr, callback.callback());
        ASSERT_THAT(rv, IsError(ERR_IO_PENDING));

        // Ensure the certificate was added to the client auth cache before
        // allowing the connection to continue restarting.
        scoped_refptr<X509Certificate> client_cert;
        scoped_refptr<SSLPrivateKey> client_private_key;
        ASSERT_TRUE(session->ssl_client_context()->GetClientCertificate(
            HostPortPair("proxy", 70), &client_cert, &client_private_key));
        ASSERT_FALSE(client_cert);
        // Ensure the certificate was NOT cached for the endpoint. This only
        // applies to HTTPS requests, but is fine to check for HTTP requests.
        ASSERT_FALSE(session->ssl_client_context()->GetClientCertificate(
            HostPortPair("www.example.com", 443), &client_cert,
            &client_private_key));

        // Restart the handshake. This will consume ssl_data2. The result code
        // is checked against what ssl_data2 should return.
        rv = callback.WaitForResult();
        ASSERT_THAT(rv, AnyOf(IsError(ERR_PROXY_CONNECTION_FAILED),
                              IsError(reject_error)));

        // Now that the new handshake has failed, ensure that the client
        // certificate was removed from the client auth cache.
        ASSERT_FALSE(session->ssl_client_context()->GetClientCertificate(
            HostPortPair("proxy", 70), &client_cert, &client_private_key));
        ASSERT_FALSE(session->ssl_client_context()->GetClientCertificate(
            HostPortPair("www.example.com", 443), &client_cert,
            &client_private_key));
      }
    }
  }
}

// Test that HttpNetworkTransaction correctly handles (mocked) certificate
// requests during a TLS renegotiation.
TEST_F(HttpNetworkTransactionTest, CertificateRequestInRenego) {
  HttpRequestInfo request_info;
  request_info.url = GURL("https://www.example.com/");
  request_info.method = "GET";
  request_info.load_flags = LOAD_NORMAL;
  request_info.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  auto cert_request = base::MakeRefCounted<SSLCertRequestInfo>();
  cert_request->host_and_port = HostPortPair("www.example.com", 443);

  std::unique_ptr<FakeClientCertIdentity> identity =
      FakeClientCertIdentity::CreateFromCertAndKeyFiles(
          GetTestCertsDirectory(), "client_1.pem", "client_1.pk8");
  ASSERT_TRUE(identity);

  // The first connection's handshake succeeds, but we get
  // ERR_SSL_CLIENT_AUTH_CERT_NEEDED instead of an HTTP response.
  SSLSocketDataProvider ssl_data1(ASYNC, OK);
  ssl_data1.cert_request_info = cert_request.get();
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl_data1);
  MockWrite data1_writes[] = {
      MockWrite("GET / HTTP/1.1\r\n"
                "Host: www.example.com\r\n"
                "Connection: keep-alive\r\n\r\n"),
  };
  MockRead data1_reads[] = {
      MockRead(ASYNC, ERR_SSL_CLIENT_AUTH_CERT_NEEDED),
  };
  StaticSocketDataProvider data1(data1_reads, data1_writes);
  session_deps_.socket_factory->AddSocketDataProvider(&data1);

  // After supplying with certificate, we restart the request from the top,
  // which succeeds this time.
  SSLSocketDataProvider ssl_data2(ASYNC, OK);
  ssl_data2.expected_send_client_cert = true;
  ssl_data2.expected_client_cert = identity->certificate();
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl_data2);
  MockWrite data2_writes[] = {
      MockWrite("GET / HTTP/1.1\r\n"
                "Host: www.example.com\r\n"
                "Connection: keep-alive\r\n\r\n"),
  };
  MockRead data2_reads[] = {
      MockRead("HTTP/1.1 200 OK\r\n"
               "Content-Length: 0\r\n\r\n"),
  };
  StaticSocketDataProvider data2(data2_reads, data2_writes);
  session_deps_.socket_factory->AddSocketDataProvider(&data2);

  std::unique_ptr<HttpNetworkSession> session = CreateSession(&session_deps_);
  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

  TestCompletionCallback callback;
  int rv = callback.GetResult(
      trans.Start(&request_info, callback.callback(), NetLogWithSource()));
  ASSERT_THAT(rv, IsError(ERR_SSL_CLIENT_AUTH_CERT_NEEDED));

  rv = trans.RestartWithCertificate(identity->certificate(),
                                    identity->ssl_private_key(),
                                    callback.callback());
  ASSERT_THAT(rv, IsError(ERR_IO_PENDING));

  // Ensure the certificate was added to the client auth cache
  // allowing the connection to continue restarting.
  scoped_refptr<X509Certificate> client_cert;
  scoped_refptr<SSLPrivateKey> client_private_key;
  ASSERT_TRUE(session->ssl_client_context()->GetClientCertificate(
      HostPortPair("www.example.com", 443), &client_cert, &client_private_key));
  EXPECT_TRUE(client_cert->EqualsIncludingChain(identity->certificate()));

  // Complete the handshake. The request now succeeds.
  rv = callback.WaitForResult();
  ASSERT_THAT(rv, IsError(OK));
  EXPECT_EQ(200, trans.GetResponseInfo()->headers->response_code());

  // The client certificate remains in the cache.
  ASSERT_TRUE(session->ssl_client_context()->GetClientCertificate(
      HostPortPair("www.example.com", 443), &client_cert, &client_private_key));
  EXPECT_TRUE(client_cert->EqualsIncludingChain(identity->certificate()));
}

TEST_F(HttpNetworkTransactionTest, UseIPConnectionPooling) {
  // Set up a special HttpNetworkSession with a MockCachingHostResolver.
  session_deps_.host_resolver = std::make_unique<MockCachingHostResolver>();
  session_deps_.host_resolver->rules()->AddRule("www.example.org", "1.2.3.4");
  session_deps_.host_resolver->rules()->AddRule("mail.example.com", "1.2.3.4");
  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));

  AddSSLSocketData();

  spdy::SpdySerializedFrame host1_req(
      spdy_util_.ConstructSpdyGet("https://www.example.org", 1, LOWEST));
  spdy_util_.UpdateWithStreamDestruction(1);
  spdy::SpdySerializedFrame host2_req(
      spdy_util_.ConstructSpdyGet("https://mail.example.com", 3, LOWEST));
  MockWrite spdy_writes[] = {
      CreateMockWrite(host1_req, 0), CreateMockWrite(host2_req, 3),
  };
  spdy::SpdySerializedFrame host1_resp(
      spdy_util_.ConstructSpdyGetReply(nullptr, 0, 1));
  spdy::SpdySerializedFrame host1_resp_body(
      spdy_util_.ConstructSpdyDataFrame(1, true));
  spdy::SpdySerializedFrame host2_resp(
      spdy_util_.ConstructSpdyGetReply(nullptr, 0, 3));
  spdy::SpdySerializedFrame host2_resp_body(
      spdy_util_.ConstructSpdyDataFrame(3, true));
  MockRead spdy_reads[] = {
      CreateMockRead(host1_resp, 1), CreateMockRead(host1_resp_body, 2),
      CreateMockRead(host2_resp, 4), CreateMockRead(host2_resp_body, 5),
      MockRead(ASYNC, 0, 6),
  };

  IPEndPoint peer_addr(IPAddress::IPv4Localhost(), 443);
  MockConnect connect(ASYNC, OK, peer_addr);
  SequencedSocketData spdy_data(connect, spdy_reads, spdy_writes);
  session_deps_.socket_factory->AddSocketDataProvider(&spdy_data);

  TestCompletionCallback callback;
  HttpRequestInfo request1;
  request1.method = "GET";
  request1.url = GURL("https://www.example.org/");
  request1.load_flags = 0;
  request1.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);
  HttpNetworkTransaction trans1(DEFAULT_PRIORITY, session.get());

  int rv = trans1.Start(&request1, callback.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));
  EXPECT_THAT(callback.WaitForResult(), IsOk());

  const HttpResponseInfo* response = trans1.GetResponseInfo();
  ASSERT_TRUE(response);
  ASSERT_TRUE(response->headers);
  EXPECT_EQ("HTTP/1.1 200", response->headers->GetStatusLine());

  std::string response_data;
  ASSERT_THAT(ReadTransaction(&trans1, &response_data), IsOk());
  EXPECT_EQ("hello!", response_data);

  // Preload mail.example.com into HostCache.
  rv = session_deps_.host_resolver->LoadIntoCache(
      HostPortPair("mail.example.com", 443), NetworkIsolationKey(),
      absl::nullopt);
  EXPECT_THAT(rv, IsOk());

  HttpRequestInfo request2;
  request2.method = "GET";
  request2.url = GURL("https://mail.example.com/");
  request2.load_flags = 0;
  request2.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);
  HttpNetworkTransaction trans2(DEFAULT_PRIORITY, session.get());

  rv = trans2.Start(&request2, callback.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));
  EXPECT_THAT(callback.WaitForResult(), IsOk());

  response = trans2.GetResponseInfo();
  ASSERT_TRUE(response);
  ASSERT_TRUE(response->headers);
  EXPECT_EQ("HTTP/1.1 200", response->headers->GetStatusLine());
  EXPECT_TRUE(response->was_fetched_via_spdy);
  EXPECT_TRUE(response->was_alpn_negotiated);
  ASSERT_THAT(ReadTransaction(&trans2, &response_data), IsOk());
  EXPECT_EQ("hello!", response_data);
}

TEST_F(HttpNetworkTransactionTest, UseIPConnectionPoolingAfterResolution) {
  // Set up a special HttpNetworkSession with a MockCachingHostResolver.
  session_deps_.host_resolver = std::make_unique<MockCachingHostResolver>();
  session_deps_.host_resolver->rules()->AddRule("www.example.org", "1.2.3.4");
  session_deps_.host_resolver->rules()->AddRule("mail.example.com", "1.2.3.4");
  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));

  AddSSLSocketData();

  spdy::SpdySerializedFrame host1_req(
      spdy_util_.ConstructSpdyGet("https://www.example.org", 1, LOWEST));
  spdy_util_.UpdateWithStreamDestruction(1);
  spdy::SpdySerializedFrame host2_req(
      spdy_util_.ConstructSpdyGet("https://mail.example.com", 3, LOWEST));
  MockWrite spdy_writes[] = {
      CreateMockWrite(host1_req, 0), CreateMockWrite(host2_req, 3),
  };
  spdy::SpdySerializedFrame host1_resp(
      spdy_util_.ConstructSpdyGetReply(nullptr, 0, 1));
  spdy::SpdySerializedFrame host1_resp_body(
      spdy_util_.ConstructSpdyDataFrame(1, true));
  spdy::SpdySerializedFrame host2_resp(
      spdy_util_.ConstructSpdyGetReply(nullptr, 0, 3));
  spdy::SpdySerializedFrame host2_resp_body(
      spdy_util_.ConstructSpdyDataFrame(3, true));
  MockRead spdy_reads[] = {
      CreateMockRead(host1_resp, 1), CreateMockRead(host1_resp_body, 2),
      CreateMockRead(host2_resp, 4), CreateMockRead(host2_resp_body, 5),
      MockRead(ASYNC, 0, 6),
  };

  IPEndPoint peer_addr(IPAddress::IPv4Localhost(), 443);
  MockConnect connect(ASYNC, OK, peer_addr);
  SequencedSocketData spdy_data(connect, spdy_reads, spdy_writes);
  session_deps_.socket_factory->AddSocketDataProvider(&spdy_data);

  TestCompletionCallback callback;
  HttpRequestInfo request1;
  request1.method = "GET";
  request1.url = GURL("https://www.example.org/");
  request1.load_flags = 0;
  request1.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);
  HttpNetworkTransaction trans1(DEFAULT_PRIORITY, session.get());

  int rv = trans1.Start(&request1, callback.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));
  EXPECT_THAT(callback.WaitForResult(), IsOk());

  const HttpResponseInfo* response = trans1.GetResponseInfo();
  ASSERT_TRUE(response);
  ASSERT_TRUE(response->headers);
  EXPECT_EQ("HTTP/1.1 200", response->headers->GetStatusLine());

  std::string response_data;
  ASSERT_THAT(ReadTransaction(&trans1, &response_data), IsOk());
  EXPECT_EQ("hello!", response_data);

  HttpRequestInfo request2;
  request2.method = "GET";
  request2.url = GURL("https://mail.example.com/");
  request2.load_flags = 0;
  request2.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);
  HttpNetworkTransaction trans2(DEFAULT_PRIORITY, session.get());

  rv = trans2.Start(&request2, callback.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));
  EXPECT_THAT(callback.WaitForResult(), IsOk());

  response = trans2.GetResponseInfo();
  ASSERT_TRUE(response);
  ASSERT_TRUE(response->headers);
  EXPECT_EQ("HTTP/1.1 200", response->headers->GetStatusLine());
  EXPECT_TRUE(response->was_fetched_via_spdy);
  EXPECT_TRUE(response->was_alpn_negotiated);
  ASSERT_THAT(ReadTransaction(&trans2, &response_data), IsOk());
  EXPECT_EQ("hello!", response_data);
}

// Regression test for https://crbug.com/546991.
// The server might not be able to serve an IP pooled request, and might send a
// 421 Misdirected Request response status to indicate this.
// HttpNetworkTransaction should reset the request and retry without IP pooling.
TEST_F(HttpNetworkTransactionTest, RetryWithoutConnectionPooling) {
  // Two hosts resolve to the same IP address.
  const std::string ip_addr = "1.2.3.4";
  IPAddress ip;
  ASSERT_TRUE(ip.AssignFromIPLiteral(ip_addr));
  IPEndPoint peer_addr = IPEndPoint(ip, 443);

  session_deps_.host_resolver = std::make_unique<MockCachingHostResolver>();
  session_deps_.host_resolver->rules()->AddRule("www.example.org", ip_addr);
  session_deps_.host_resolver->rules()->AddRule("mail.example.org", ip_addr);

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));

  // Two requests on the first connection.
  spdy::SpdySerializedFrame req1(
      spdy_util_.ConstructSpdyGet("https://www.example.org", 1, LOWEST));
  spdy_util_.UpdateWithStreamDestruction(1);
  spdy::SpdySerializedFrame req2(
      spdy_util_.ConstructSpdyGet("https://mail.example.org", 3, LOWEST));
  spdy::SpdySerializedFrame rst(
      spdy_util_.ConstructSpdyRstStream(3, spdy::ERROR_CODE_CANCEL));
  MockWrite writes1[] = {
      CreateMockWrite(req1, 0), CreateMockWrite(req2, 3),
      CreateMockWrite(rst, 6),
  };

  // The first one succeeds, the second gets error 421 Misdirected Request.
  spdy::SpdySerializedFrame resp1(
      spdy_util_.ConstructSpdyGetReply(nullptr, 0, 1));
  spdy::SpdySerializedFrame body1(spdy_util_.ConstructSpdyDataFrame(1, true));
  spdy::Http2HeaderBlock response_headers;
  response_headers[spdy::kHttp2StatusHeader] = "421";
  spdy::SpdySerializedFrame resp2(
      spdy_util_.ConstructSpdyReply(3, std::move(response_headers)));
  MockRead reads1[] = {CreateMockRead(resp1, 1), CreateMockRead(body1, 2),
                       CreateMockRead(resp2, 4), MockRead(ASYNC, 0, 5)};

  MockConnect connect1(ASYNC, OK, peer_addr);
  SequencedSocketData data1(connect1, reads1, writes1);
  session_deps_.socket_factory->AddSocketDataProvider(&data1);

  AddSSLSocketData();

  // Retry the second request on a second connection.
  SpdyTestUtil spdy_util2;
  spdy::SpdySerializedFrame req3(
      spdy_util2.ConstructSpdyGet("https://mail.example.org", 1, LOWEST));
  MockWrite writes2[] = {
      CreateMockWrite(req3, 0),
  };

  spdy::SpdySerializedFrame resp3(
      spdy_util2.ConstructSpdyGetReply(nullptr, 0, 1));
  spdy::SpdySerializedFrame body3(spdy_util2.ConstructSpdyDataFrame(1, true));
  MockRead reads2[] = {CreateMockRead(resp3, 1), CreateMockRead(body3, 2),
                       MockRead(ASYNC, 0, 3)};

  MockConnect connect2(ASYNC, OK, peer_addr);
  SequencedSocketData data2(connect2, reads2, writes2);
  session_deps_.socket_factory->AddSocketDataProvider(&data2);

  AddSSLSocketData();

  // Preload mail.example.org into HostCache.
  int rv = session_deps_.host_resolver->LoadIntoCache(
      HostPortPair("mail.example.org", 443), NetworkIsolationKey(),
      absl::nullopt);
  EXPECT_THAT(rv, IsOk());

  HttpRequestInfo request1;
  request1.method = "GET";
  request1.url = GURL("https://www.example.org/");
  request1.load_flags = 0;
  request1.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);
  HttpNetworkTransaction trans1(DEFAULT_PRIORITY, session.get());

  TestCompletionCallback callback;
  rv = trans1.Start(&request1, callback.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));
  rv = callback.WaitForResult();
  EXPECT_THAT(rv, IsOk());

  const HttpResponseInfo* response = trans1.GetResponseInfo();
  ASSERT_TRUE(response);
  ASSERT_TRUE(response->headers);
  EXPECT_EQ("HTTP/1.1 200", response->headers->GetStatusLine());
  EXPECT_TRUE(response->was_fetched_via_spdy);
  EXPECT_TRUE(response->was_alpn_negotiated);
  std::string response_data;
  ASSERT_THAT(ReadTransaction(&trans1, &response_data), IsOk());
  EXPECT_EQ("hello!", response_data);

  HttpRequestInfo request2;
  request2.method = "GET";
  request2.url = GURL("https://mail.example.org/");
  request2.load_flags = 0;
  request2.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);
  HttpNetworkTransaction trans2(DEFAULT_PRIORITY, session.get());

  RecordingNetLogObserver net_log_observer;
  rv = trans2.Start(&request2, callback.callback(),
                    NetLogWithSource::Make(NetLogSourceType::NONE));
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));
  rv = callback.WaitForResult();
  EXPECT_THAT(rv, IsOk());

  response = trans2.GetResponseInfo();
  ASSERT_TRUE(response);
  ASSERT_TRUE(response->headers);
  EXPECT_EQ("HTTP/1.1 200", response->headers->GetStatusLine());
  EXPECT_TRUE(response->was_fetched_via_spdy);
  EXPECT_TRUE(response->was_alpn_negotiated);
  ASSERT_THAT(ReadTransaction(&trans2, &response_data), IsOk());
  EXPECT_EQ("hello!", response_data);

  auto entries = net_log_observer.GetEntries();
  ExpectLogContainsSomewhere(
      entries, 0, NetLogEventType::HTTP_TRANSACTION_RESTART_MISDIRECTED_REQUEST,
      NetLogEventPhase::NONE);
}

// Test that HTTP 421 responses are properly returned to the caller if received
// on the retry as well. HttpNetworkTransaction should not infinite loop or lose
// portions of the response.
TEST_F(HttpNetworkTransactionTest, ReturnHTTP421OnRetry) {
  // Two hosts resolve to the same IP address.
  const std::string ip_addr = "1.2.3.4";
  IPAddress ip;
  ASSERT_TRUE(ip.AssignFromIPLiteral(ip_addr));
  IPEndPoint peer_addr = IPEndPoint(ip, 443);

  session_deps_.host_resolver = std::make_unique<MockCachingHostResolver>();
  session_deps_.host_resolver->rules()->AddRule("www.example.org", ip_addr);
  session_deps_.host_resolver->rules()->AddRule("mail.example.org", ip_addr);

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));

  // Two requests on the first connection.
  spdy::SpdySerializedFrame req1(
      spdy_util_.ConstructSpdyGet("https://www.example.org", 1, LOWEST));
  spdy_util_.UpdateWithStreamDestruction(1);
  spdy::SpdySerializedFrame req2(
      spdy_util_.ConstructSpdyGet("https://mail.example.org", 3, LOWEST));
  spdy::SpdySerializedFrame rst(
      spdy_util_.ConstructSpdyRstStream(3, spdy::ERROR_CODE_CANCEL));
  MockWrite writes1[] = {
      CreateMockWrite(req1, 0), CreateMockWrite(req2, 3),
      CreateMockWrite(rst, 6),
  };

  // The first one succeeds, the second gets error 421 Misdirected Request.
  spdy::SpdySerializedFrame resp1(
      spdy_util_.ConstructSpdyGetReply(nullptr, 0, 1));
  spdy::SpdySerializedFrame body1(spdy_util_.ConstructSpdyDataFrame(1, true));
  spdy::Http2HeaderBlock response_headers;
  response_headers[spdy::kHttp2StatusHeader] = "421";
  spdy::SpdySerializedFrame resp2(
      spdy_util_.ConstructSpdyReply(3, response_headers.Clone()));
  MockRead reads1[] = {CreateMockRead(resp1, 1), CreateMockRead(body1, 2),
                       CreateMockRead(resp2, 4), MockRead(ASYNC, 0, 5)};

  MockConnect connect1(ASYNC, OK, peer_addr);
  SequencedSocketData data1(connect1, reads1, writes1);
  session_deps_.socket_factory->AddSocketDataProvider(&data1);

  AddSSLSocketData();

  // Retry the second request on a second connection. It returns 421 Misdirected
  // Retry again.
  SpdyTestUtil spdy_util2;
  spdy::SpdySerializedFrame req3(
      spdy_util2.ConstructSpdyGet("https://mail.example.org", 1, LOWEST));
  MockWrite writes2[] = {
      CreateMockWrite(req3, 0),
  };

  spdy::SpdySerializedFrame resp3(
      spdy_util2.ConstructSpdyReply(1, std::move(response_headers)));
  spdy::SpdySerializedFrame body3(spdy_util2.ConstructSpdyDataFrame(1, true));
  MockRead reads2[] = {CreateMockRead(resp3, 1), CreateMockRead(body3, 2),
                       MockRead(ASYNC, 0, 3)};

  MockConnect connect2(ASYNC, OK, peer_addr);
  SequencedSocketData data2(connect2, reads2, writes2);
  session_deps_.socket_factory->AddSocketDataProvider(&data2);

  AddSSLSocketData();

  // Preload mail.example.org into HostCache.
  int rv = session_deps_.host_resolver->LoadIntoCache(
      HostPortPair("mail.example.org", 443), NetworkIsolationKey(),
      absl::nullopt);
  EXPECT_THAT(rv, IsOk());

  HttpRequestInfo request1;
  request1.method = "GET";
  request1.url = GURL("https://www.example.org/");
  request1.load_flags = 0;
  request1.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);
  HttpNetworkTransaction trans1(DEFAULT_PRIORITY, session.get());

  TestCompletionCallback callback;
  rv = trans1.Start(&request1, callback.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));
  rv = callback.WaitForResult();
  EXPECT_THAT(rv, IsOk());

  const HttpResponseInfo* response = trans1.GetResponseInfo();
  ASSERT_TRUE(response);
  ASSERT_TRUE(response->headers);
  EXPECT_EQ("HTTP/1.1 200", response->headers->GetStatusLine());
  EXPECT_TRUE(response->was_fetched_via_spdy);
  EXPECT_TRUE(response->was_alpn_negotiated);
  std::string response_data;
  ASSERT_THAT(ReadTransaction(&trans1, &response_data), IsOk());
  EXPECT_EQ("hello!", response_data);

  HttpRequestInfo request2;
  request2.method = "GET";
  request2.url = GURL("https://mail.example.org/");
  request2.load_flags = 0;
  request2.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);
  HttpNetworkTransaction trans2(DEFAULT_PRIORITY, session.get());

  rv = trans2.Start(&request2, callback.callback(),
                    NetLogWithSource::Make(NetLogSourceType::NONE));
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));
  rv = callback.WaitForResult();
  EXPECT_THAT(rv, IsOk());

  // After a retry, the 421 Misdirected Request is reported back up to the
  // caller.
  response = trans2.GetResponseInfo();
  ASSERT_TRUE(response);
  ASSERT_TRUE(response->headers);
  EXPECT_EQ("HTTP/1.1 421", response->headers->GetStatusLine());
  EXPECT_TRUE(response->was_fetched_via_spdy);
  EXPECT_TRUE(response->was_alpn_negotiated);
  EXPECT_TRUE(response->ssl_info.cert);
  ASSERT_THAT(ReadTransaction(&trans2, &response_data), IsOk());
  EXPECT_EQ("hello!", response_data);
}

TEST_F(HttpNetworkTransactionTest,
       Response421WithStreamingBodyWithNonNullSource) {
  const std::string ip_addr = "1.2.3.4";
  IPAddress ip;
  ASSERT_TRUE(ip.AssignFromIPLiteral(ip_addr));
  IPEndPoint peer_addr = IPEndPoint(ip, 443);

  session_deps_.host_resolver = std::make_unique<MockCachingHostResolver>();
  session_deps_.host_resolver->rules()->AddRule("www.example.org", ip_addr);
  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));

  const std::string request_body = "hello";
  spdy::SpdySerializedFrame req1 = spdy_util_.ConstructChunkedSpdyPost({}, 0);
  spdy::SpdySerializedFrame req1_body =
      spdy_util_.ConstructSpdyDataFrame(1, request_body, /*fin=*/true);
  spdy::SpdySerializedFrame rst =
      spdy_util_.ConstructSpdyRstStream(1, spdy::ERROR_CODE_CANCEL);
  MockWrite writes1[] = {
      CreateMockWrite(req1, 0),
      CreateMockWrite(req1_body, 1),
      CreateMockWrite(rst, 4),
  };

  spdy::Http2HeaderBlock response_headers;
  response_headers[spdy::kHttp2StatusHeader] = "421";
  spdy::SpdySerializedFrame resp1 =
      spdy_util_.ConstructSpdyReply(1, std::move(response_headers));
  MockRead reads1[] = {CreateMockRead(resp1, 2), MockRead(ASYNC, 0, 3)};

  MockConnect connect1(ASYNC, OK, peer_addr);
  SequencedSocketData data1(connect1, reads1, writes1);
  session_deps_.socket_factory->AddSocketDataProvider(&data1);

  AddSSLSocketData();

  SpdyTestUtil spdy_util2;
  spdy::SpdySerializedFrame req2 = spdy_util2.ConstructChunkedSpdyPost({}, 0);
  spdy::SpdySerializedFrame req2_body =
      spdy_util2.ConstructSpdyDataFrame(1, request_body, /*fin=*/true);
  MockWrite writes2[] = {
      CreateMockWrite(req2, 0),
      CreateMockWrite(req2_body, 1),
  };

  spdy::Http2HeaderBlock resp2_headers;
  resp2_headers[spdy::kHttp2StatusHeader] = "200";
  spdy::SpdySerializedFrame resp2 =
      spdy_util2.ConstructSpdyReply(1, std::move(resp2_headers));
  spdy::SpdySerializedFrame resp2_body(
      spdy_util2.ConstructSpdyDataFrame(1, true));
  MockRead reads2[] = {CreateMockRead(resp2, 2), CreateMockRead(resp2_body, 3),
                       MockRead(ASYNC, 0, 4)};

  MockConnect connect2(ASYNC, OK, peer_addr);
  SequencedSocketData data2(connect2, reads2, writes2);
  session_deps_.socket_factory->AddSocketDataProvider(&data2);

  AddSSLSocketData();

  TestCompletionCallback callback;
  HttpRequestInfo request;
  ChunkedUploadDataStream upload_data_stream(0, /*has_null_source=*/false);
  upload_data_stream.AppendData(request_body.data(), request_body.size(),
                                /*is_done=*/true);
  request.method = "POST";
  request.url = GURL("https://www.example.org");
  request.load_flags = 0;
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);
  request.upload_data_stream = &upload_data_stream;
  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

  int rv = trans.Start(&request, callback.callback(),
                       NetLogWithSource::Make(NetLogSourceType::NONE));
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));
  rv = callback.WaitForResult();
  EXPECT_THAT(rv, IsOk());

  const HttpResponseInfo* response = trans.GetResponseInfo();
  std::string response_data;
  ASSERT_TRUE(response);
  ASSERT_TRUE(response->headers);
  EXPECT_EQ("HTTP/1.1 200", response->headers->GetStatusLine());
  EXPECT_TRUE(response->was_fetched_via_spdy);
  EXPECT_TRUE(response->was_alpn_negotiated);
  EXPECT_TRUE(response->ssl_info.cert);
  ASSERT_THAT(ReadTransaction(&trans, &response_data), IsOk());
  EXPECT_EQ("hello!", response_data);
}

TEST_F(HttpNetworkTransactionTest, Response421WithStreamingBodyWithNullSource) {
  const std::string ip_addr = "1.2.3.4";
  IPAddress ip;
  ASSERT_TRUE(ip.AssignFromIPLiteral(ip_addr));
  IPEndPoint peer_addr = IPEndPoint(ip, 443);

  session_deps_.host_resolver = std::make_unique<MockCachingHostResolver>();
  session_deps_.host_resolver->rules()->AddRule("www.example.org", ip_addr);
  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));

  const std::string request_body = "hello";
  spdy::SpdySerializedFrame req1 = spdy_util_.ConstructChunkedSpdyPost({}, 0);
  spdy::SpdySerializedFrame req1_body =
      spdy_util_.ConstructSpdyDataFrame(1, request_body, /*fin=*/true);
  spdy::SpdySerializedFrame rst =
      spdy_util_.ConstructSpdyRstStream(1, spdy::ERROR_CODE_CANCEL);
  MockWrite writes1[] = {
      CreateMockWrite(req1, 0),
      CreateMockWrite(req1_body, 1),
      CreateMockWrite(rst, 5),
  };

  spdy::Http2HeaderBlock response_headers;
  response_headers[spdy::kHttp2StatusHeader] = "421";
  spdy::SpdySerializedFrame resp1 =
      spdy_util_.ConstructSpdyReply(1, std::move(response_headers));
  spdy::SpdySerializedFrame resp1_body(
      spdy_util_.ConstructSpdyDataFrame(1, true));
  MockRead reads1[] = {CreateMockRead(resp1, 2), CreateMockRead(resp1_body, 3),
                       MockRead(ASYNC, 0, 4)};

  MockConnect connect1(ASYNC, OK, peer_addr);
  SequencedSocketData data1(connect1, reads1, writes1);
  session_deps_.socket_factory->AddSocketDataProvider(&data1);

  AddSSLSocketData();

  TestCompletionCallback callback;
  HttpRequestInfo request;
  ChunkedUploadDataStream upload_data_stream(0, /*has_null_source=*/true);
  upload_data_stream.AppendData(request_body.data(), request_body.size(),
                                /*is_done=*/true);
  request.method = "POST";
  request.url = GURL("https://www.example.org");
  request.load_flags = 0;
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);
  request.upload_data_stream = &upload_data_stream;
  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

  int rv = trans.Start(&request, callback.callback(),
                       NetLogWithSource::Make(NetLogSourceType::NONE));
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));
  rv = callback.WaitForResult();
  EXPECT_THAT(rv, IsOk());

  const HttpResponseInfo* response = trans.GetResponseInfo();
  std::string response_data;
  ASSERT_TRUE(response);
  ASSERT_TRUE(response->headers);
  EXPECT_EQ("HTTP/1.1 421", response->headers->GetStatusLine());
  EXPECT_TRUE(response->was_fetched_via_spdy);
  EXPECT_TRUE(response->was_alpn_negotiated);
  EXPECT_TRUE(response->ssl_info.cert);
  ASSERT_THAT(ReadTransaction(&trans, &response_data), IsOk());
  EXPECT_EQ("hello!", response_data);
}

TEST_F(HttpNetworkTransactionTest,
       UseIPConnectionPoolingWithHostCacheExpiration) {
  // Set up HostResolver to invalidate cached entries after 1 cached resolve.
  session_deps_.host_resolver =
      std::make_unique<MockCachingHostResolver>(1 /* cache_invalidation_num */);
  session_deps_.host_resolver->rules()->AddRule("www.example.org", "1.2.3.4");
  session_deps_.host_resolver->rules()->AddRule("mail.example.com", "1.2.3.4");
  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));

  AddSSLSocketData();

  spdy::SpdySerializedFrame host1_req(
      spdy_util_.ConstructSpdyGet("https://www.example.org", 1, LOWEST));
  spdy_util_.UpdateWithStreamDestruction(1);
  spdy::SpdySerializedFrame host2_req(
      spdy_util_.ConstructSpdyGet("https://mail.example.com", 3, LOWEST));
  MockWrite spdy_writes[] = {
      CreateMockWrite(host1_req, 0), CreateMockWrite(host2_req, 3),
  };
  spdy::SpdySerializedFrame host1_resp(
      spdy_util_.ConstructSpdyGetReply(nullptr, 0, 1));
  spdy::SpdySerializedFrame host1_resp_body(
      spdy_util_.ConstructSpdyDataFrame(1, true));
  spdy::SpdySerializedFrame host2_resp(
      spdy_util_.ConstructSpdyGetReply(nullptr, 0, 3));
  spdy::SpdySerializedFrame host2_resp_body(
      spdy_util_.ConstructSpdyDataFrame(3, true));
  MockRead spdy_reads[] = {
      CreateMockRead(host1_resp, 1), CreateMockRead(host1_resp_body, 2),
      CreateMockRead(host2_resp, 4), CreateMockRead(host2_resp_body, 5),
      MockRead(ASYNC, 0, 6),
  };

  IPEndPoint peer_addr(IPAddress::IPv4Localhost(), 443);
  MockConnect connect(ASYNC, OK, peer_addr);
  SequencedSocketData spdy_data(connect, spdy_reads, spdy_writes);
  session_deps_.socket_factory->AddSocketDataProvider(&spdy_data);

  TestCompletionCallback callback;
  HttpRequestInfo request1;
  request1.method = "GET";
  request1.url = GURL("https://www.example.org/");
  request1.load_flags = 0;
  request1.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);
  HttpNetworkTransaction trans1(DEFAULT_PRIORITY, session.get());

  int rv = trans1.Start(&request1, callback.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));
  EXPECT_THAT(callback.WaitForResult(), IsOk());

  const HttpResponseInfo* response = trans1.GetResponseInfo();
  ASSERT_TRUE(response);
  ASSERT_TRUE(response->headers);
  EXPECT_EQ("HTTP/1.1 200", response->headers->GetStatusLine());

  std::string response_data;
  ASSERT_THAT(ReadTransaction(&trans1, &response_data), IsOk());
  EXPECT_EQ("hello!", response_data);

  // Preload cache entries into HostCache.
  rv = session_deps_.host_resolver->LoadIntoCache(
      HostPortPair("mail.example.com", 443), NetworkIsolationKey(),
      absl::nullopt);
  EXPECT_THAT(rv, IsOk());

  HttpRequestInfo request2;
  request2.method = "GET";
  request2.url = GURL("https://mail.example.com/");
  request2.load_flags = 0;
  request2.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);
  HttpNetworkTransaction trans2(DEFAULT_PRIORITY, session.get());

  rv = trans2.Start(&request2, callback.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));
  EXPECT_THAT(callback.WaitForResult(), IsOk());

  response = trans2.GetResponseInfo();
  ASSERT_TRUE(response);
  ASSERT_TRUE(response->headers);
  EXPECT_EQ("HTTP/1.1 200", response->headers->GetStatusLine());
  EXPECT_TRUE(response->was_fetched_via_spdy);
  EXPECT_TRUE(response->was_alpn_negotiated);
  ASSERT_THAT(ReadTransaction(&trans2, &response_data), IsOk());
  EXPECT_EQ("hello!", response_data);
}

TEST_F(HttpNetworkTransactionTest, DoNotUseSpdySessionForHttp) {
  const std::string https_url = "https://www.example.org:8080/";
  const std::string http_url = "http://www.example.org:8080/";

  // SPDY GET for HTTPS URL
  spdy::SpdySerializedFrame req1(
      spdy_util_.ConstructSpdyGet(https_url.c_str(), 1, LOWEST));

  MockWrite writes1[] = {
      CreateMockWrite(req1, 0),
  };

  spdy::SpdySerializedFrame resp1(
      spdy_util_.ConstructSpdyGetReply(nullptr, 0, 1));
  spdy::SpdySerializedFrame body1(spdy_util_.ConstructSpdyDataFrame(1, true));
  MockRead reads1[] = {CreateMockRead(resp1, 1), CreateMockRead(body1, 2),
                       MockRead(SYNCHRONOUS, ERR_IO_PENDING, 3)};

  SequencedSocketData data1(reads1, writes1);
  MockConnect connect_data1(ASYNC, OK);
  data1.set_connect_data(connect_data1);

  // HTTP GET for the HTTP URL
  MockWrite writes2[] = {
      MockWrite(ASYNC, 0,
                "GET / HTTP/1.1\r\n"
                "Host: www.example.org:8080\r\n"
                "Connection: keep-alive\r\n\r\n"),
  };

  MockRead reads2[] = {
      MockRead(ASYNC, 1, "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\n"),
      MockRead(ASYNC, 2, "hello"),
      MockRead(ASYNC, OK, 3),
  };

  SequencedSocketData data2(reads2, writes2);

  SSLSocketDataProvider ssl(ASYNC, OK);
  ssl.next_proto = kProtoHTTP2;
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl);
  session_deps_.socket_factory->AddSocketDataProvider(&data1);
  session_deps_.socket_factory->AddSocketDataProvider(&data2);

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));

  // Start the first transaction to set up the SpdySession
  HttpRequestInfo request1;
  request1.method = "GET";
  request1.url = GURL(https_url);
  request1.load_flags = 0;
  request1.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);
  HttpNetworkTransaction trans1(LOWEST, session.get());
  TestCompletionCallback callback1;
  EXPECT_EQ(ERR_IO_PENDING,
            trans1.Start(&request1, callback1.callback(), NetLogWithSource()));
  base::RunLoop().RunUntilIdle();

  EXPECT_THAT(callback1.WaitForResult(), IsOk());
  EXPECT_TRUE(trans1.GetResponseInfo()->was_fetched_via_spdy);

  // Now, start the HTTP request
  HttpRequestInfo request2;
  request2.method = "GET";
  request2.url = GURL(http_url);
  request2.load_flags = 0;
  request2.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);
  HttpNetworkTransaction trans2(MEDIUM, session.get());
  TestCompletionCallback callback2;
  EXPECT_EQ(ERR_IO_PENDING,
            trans2.Start(&request2, callback2.callback(), NetLogWithSource()));
  base::RunLoop().RunUntilIdle();

  EXPECT_THAT(callback2.WaitForResult(), IsOk());
  EXPECT_FALSE(trans2.GetResponseInfo()->was_fetched_via_spdy);
}

// Alternative service requires HTTP/2 (or SPDY), but HTTP/1.1 is negotiated
// with the alternative server.  That connection should not be used.
TEST_F(HttpNetworkTransactionTest, AlternativeServiceNotOnHttp11) {
  url::SchemeHostPort server("https", "www.example.org", 443);
  HostPortPair alternative("www.example.org", 444);

  // Negotiate HTTP/1.1 with alternative.
  SSLSocketDataProvider ssl(ASYNC, OK);
  ssl.next_proto = kProtoHTTP11;
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl);

  // No data should be read from the alternative, because HTTP/1.1 is
  // negotiated.
  StaticSocketDataProvider data;
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  // This test documents that an alternate Job should not be used if HTTP/1.1 is
  // negotiated.  In order to test this, a failed connection to the server is
  // mocked.  This way the request relies on the alternate Job.
  StaticSocketDataProvider data_refused;
  data_refused.set_connect_data(MockConnect(ASYNC, ERR_CONNECTION_REFUSED));
  session_deps_.socket_factory->AddSocketDataProvider(&data_refused);

  // Set up alternative service for server.
  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));
  HttpServerProperties* http_server_properties =
      session->http_server_properties();
  AlternativeService alternative_service(kProtoHTTP2, alternative);
  base::Time expiration = base::Time::Now() + base::Days(1);
  http_server_properties->SetHttp2AlternativeService(
      server, NetworkIsolationKey(), alternative_service, expiration);

  HttpRequestInfo request;
  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());
  request.method = "GET";
  request.url = GURL("https://www.example.org:443");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);
  TestCompletionCallback callback;

  // HTTP/2 (or SPDY) is required for alternative service, if HTTP/1.1 is
  // negotiated, the alternate Job should fail with ERR_ALPN_NEGOTIATION_FAILED.
  int rv = trans.Start(&request, callback.callback(), NetLogWithSource());
  EXPECT_THAT(callback.GetResult(rv), IsError(ERR_ALPN_NEGOTIATION_FAILED));
}

// A request to a server with an alternative service fires two Jobs: one to the
// server, and an alternate one to the alternative server.  If the former
// succeeds, the request should succeed,  even if the latter fails because
// HTTP/1.1 is negotiated which is insufficient for alternative service.
TEST_F(HttpNetworkTransactionTest, FailedAlternativeServiceIsNotUserVisible) {
  url::SchemeHostPort server("https", "www.example.org", 443);
  HostPortPair alternative("www.example.org", 444);

  // Negotiate HTTP/1.1 with alternative.
  SSLSocketDataProvider alternative_ssl(ASYNC, OK);
  alternative_ssl.next_proto = kProtoHTTP11;
  session_deps_.socket_factory->AddSSLSocketDataProvider(&alternative_ssl);

  // No data should be read from the alternative, because HTTP/1.1 is
  // negotiated.
  StaticSocketDataProvider data;
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  // Negotiate HTTP/1.1 with server.
  SSLSocketDataProvider origin_ssl(ASYNC, OK);
  origin_ssl.next_proto = kProtoHTTP11;
  session_deps_.socket_factory->AddSSLSocketDataProvider(&origin_ssl);

  MockWrite http_writes[] = {
      MockWrite("GET / HTTP/1.1\r\n"
                "Host: www.example.org\r\n"
                "Connection: keep-alive\r\n\r\n"),
      MockWrite("GET /second HTTP/1.1\r\n"
                "Host: www.example.org\r\n"
                "Connection: keep-alive\r\n\r\n"),
  };

  MockRead http_reads[] = {
      MockRead("HTTP/1.1 200 OK\r\n"),
      MockRead("Content-Type: text/html\r\n"),
      MockRead("Content-Length: 6\r\n\r\n"),
      MockRead("foobar"),
      MockRead("HTTP/1.1 200 OK\r\n"),
      MockRead("Content-Type: text/html\r\n"),
      MockRead("Content-Length: 7\r\n\r\n"),
      MockRead("another"),
  };
  StaticSocketDataProvider http_data(http_reads, http_writes);
  session_deps_.socket_factory->AddSocketDataProvider(&http_data);

  // Set up alternative service for server.
  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));
  HttpServerProperties* http_server_properties =
      session->http_server_properties();
  AlternativeService alternative_service(kProtoHTTP2, alternative);
  base::Time expiration = base::Time::Now() + base::Days(1);
  http_server_properties->SetHttp2AlternativeService(
      server, NetworkIsolationKey(), alternative_service, expiration);

  HttpNetworkTransaction trans1(DEFAULT_PRIORITY, session.get());
  HttpRequestInfo request1;
  request1.method = "GET";
  request1.url = GURL("https://www.example.org:443");
  request1.load_flags = 0;
  request1.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);
  TestCompletionCallback callback1;

  int rv = trans1.Start(&request1, callback1.callback(), NetLogWithSource());
  rv = callback1.GetResult(rv);
  EXPECT_THAT(rv, IsOk());

  const HttpResponseInfo* response1 = trans1.GetResponseInfo();
  ASSERT_TRUE(response1);
  ASSERT_TRUE(response1->headers);
  EXPECT_EQ("HTTP/1.1 200 OK", response1->headers->GetStatusLine());

  std::string response_data1;
  ASSERT_THAT(ReadTransaction(&trans1, &response_data1), IsOk());
  EXPECT_EQ("foobar", response_data1);

  // Alternative should be marked as broken, because HTTP/1.1 is not sufficient
  // for alternative service.
  EXPECT_TRUE(http_server_properties->IsAlternativeServiceBroken(
      alternative_service, NetworkIsolationKey()));

  // Since |alternative_service| is broken, a second transaction to server
  // should not start an alternate Job.  It should pool to existing connection
  // to server.
  HttpNetworkTransaction trans2(DEFAULT_PRIORITY, session.get());
  HttpRequestInfo request2;
  request2.method = "GET";
  request2.url = GURL("https://www.example.org:443/second");
  request2.load_flags = 0;
  request2.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);
  TestCompletionCallback callback2;

  rv = trans2.Start(&request2, callback2.callback(), NetLogWithSource());
  rv = callback2.GetResult(rv);
  EXPECT_THAT(rv, IsOk());

  const HttpResponseInfo* response2 = trans2.GetResponseInfo();
  ASSERT_TRUE(response2);
  ASSERT_TRUE(response2->headers);
  EXPECT_EQ("HTTP/1.1 200 OK", response2->headers->GetStatusLine());

  std::string response_data2;
  ASSERT_THAT(ReadTransaction(&trans2, &response_data2), IsOk());
  EXPECT_EQ("another", response_data2);
}

// Alternative service requires HTTP/2 (or SPDY), but there is already a
// HTTP/1.1 socket open to the alternative server.  That socket should not be
// used.
TEST_F(HttpNetworkTransactionTest, AlternativeServiceShouldNotPoolToHttp11) {
  url::SchemeHostPort server("https", "origin.example.org", 443);
  HostPortPair alternative("alternative.example.org", 443);
  std::string origin_url = "https://origin.example.org:443";
  std::string alternative_url = "https://alternative.example.org:443";

  // Negotiate HTTP/1.1 with alternative.example.org.
  SSLSocketDataProvider ssl(ASYNC, OK);
  ssl.next_proto = kProtoHTTP11;
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl);

  // HTTP/1.1 data for |request1| and |request2|.
  MockWrite http_writes[] = {
      MockWrite(
          "GET / HTTP/1.1\r\n"
          "Host: alternative.example.org\r\n"
          "Connection: keep-alive\r\n\r\n"),
      MockWrite(
          "GET / HTTP/1.1\r\n"
          "Host: alternative.example.org\r\n"
          "Connection: keep-alive\r\n\r\n"),
  };

  MockRead http_reads[] = {
      MockRead(
          "HTTP/1.1 200 OK\r\n"
          "Content-Type: text/html; charset=iso-8859-1\r\n"
          "Content-Length: 40\r\n\r\n"
          "first HTTP/1.1 response from alternative"),
      MockRead(
          "HTTP/1.1 200 OK\r\n"
          "Content-Type: text/html; charset=iso-8859-1\r\n"
          "Content-Length: 41\r\n\r\n"
          "second HTTP/1.1 response from alternative"),
  };
  StaticSocketDataProvider http_data(http_reads, http_writes);
  session_deps_.socket_factory->AddSocketDataProvider(&http_data);

  // This test documents that an alternate Job should not pool to an already
  // existing HTTP/1.1 connection.  In order to test this, a failed connection
  // to the server is mocked.  This way |request2| relies on the alternate Job.
  StaticSocketDataProvider data_refused;
  data_refused.set_connect_data(MockConnect(ASYNC, ERR_CONNECTION_REFUSED));
  session_deps_.socket_factory->AddSocketDataProvider(&data_refused);

  // Set up alternative service for server.
  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));
  HttpServerProperties* http_server_properties =
      session->http_server_properties();
  AlternativeService alternative_service(kProtoHTTP2, alternative);
  base::Time expiration = base::Time::Now() + base::Days(1);
  http_server_properties->SetHttp2AlternativeService(
      server, NetworkIsolationKey(), alternative_service, expiration);

  // First transaction to alternative to open an HTTP/1.1 socket.
  HttpRequestInfo request1;
  HttpNetworkTransaction trans1(DEFAULT_PRIORITY, session.get());
  request1.method = "GET";
  request1.url = GURL(alternative_url);
  request1.load_flags = 0;
  request1.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);
  TestCompletionCallback callback1;

  int rv = trans1.Start(&request1, callback1.callback(), NetLogWithSource());
  EXPECT_THAT(callback1.GetResult(rv), IsOk());
  const HttpResponseInfo* response1 = trans1.GetResponseInfo();
  ASSERT_TRUE(response1);
  ASSERT_TRUE(response1->headers);
  EXPECT_EQ("HTTP/1.1 200 OK", response1->headers->GetStatusLine());
  EXPECT_TRUE(response1->was_alpn_negotiated);
  EXPECT_FALSE(response1->was_fetched_via_spdy);
  std::string response_data1;
  ASSERT_THAT(ReadTransaction(&trans1, &response_data1), IsOk());
  EXPECT_EQ("first HTTP/1.1 response from alternative", response_data1);

  // Request for origin.example.org, which has an alternative service.  This
  // will start two Jobs: the alternative looks for connections to pool to,
  // finds one which is HTTP/1.1, and should ignore it, and should not try to
  // open other connections to alternative server.  The Job to server fails, so
  // this request fails.
  HttpRequestInfo request2;
  HttpNetworkTransaction trans2(DEFAULT_PRIORITY, session.get());
  request2.method = "GET";
  request2.url = GURL(origin_url);
  request2.load_flags = 0;
  request2.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);
  TestCompletionCallback callback2;

  rv = trans2.Start(&request2, callback2.callback(), NetLogWithSource());
  EXPECT_THAT(callback2.GetResult(rv), IsError(ERR_CONNECTION_REFUSED));

  // Another transaction to alternative.  This is to test that the HTTP/1.1
  // socket is still open and in the pool.
  HttpRequestInfo request3;
  HttpNetworkTransaction trans3(DEFAULT_PRIORITY, session.get());
  request3.method = "GET";
  request3.url = GURL(alternative_url);
  request3.load_flags = 0;
  request3.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);
  TestCompletionCallback callback3;

  rv = trans3.Start(&request3, callback3.callback(), NetLogWithSource());
  EXPECT_THAT(callback3.GetResult(rv), IsOk());
  const HttpResponseInfo* response3 = trans3.GetResponseInfo();
  ASSERT_TRUE(response3);
  ASSERT_TRUE(response3->headers);
  EXPECT_EQ("HTTP/1.1 200 OK", response3->headers->GetStatusLine());
  EXPECT_TRUE(response3->was_alpn_negotiated);
  EXPECT_FALSE(response3->was_fetched_via_spdy);
  std::string response_data3;
  ASSERT_THAT(ReadTransaction(&trans3, &response_data3), IsOk());
  EXPECT_EQ("second HTTP/1.1 response from alternative", response_data3);
}

TEST_F(HttpNetworkTransactionTest, DoNotUseSpdySessionForHttpOverTunnel) {
  const std::string https_url = "https://www.example.org:8080/";
  const std::string http_url = "http://www.example.org:8080/";

  // Separate SPDY util instance for naked and wrapped requests.
  SpdyTestUtil spdy_util_wrapped;

  // SPDY GET for HTTPS URL (through CONNECT tunnel)
  const HostPortPair host_port_pair("www.example.org", 8080);
  spdy::SpdySerializedFrame connect(spdy_util_.ConstructSpdyConnect(
      nullptr, 0, 1, HttpProxyConnectJob::kH2QuicTunnelPriority,
      host_port_pair));
  spdy::SpdySerializedFrame req1(
      spdy_util_wrapped.ConstructSpdyGet(https_url.c_str(), 1, LOWEST));
  spdy::SpdySerializedFrame wrapped_req1(
      spdy_util_.ConstructWrappedSpdyFrame(req1, 1));

  // SPDY GET for HTTP URL (through the proxy, but not the tunnel).
  spdy::Http2HeaderBlock req2_block;
  req2_block[spdy::kHttp2MethodHeader] = "GET";
  req2_block[spdy::kHttp2AuthorityHeader] = "www.example.org:8080";
  req2_block[spdy::kHttp2SchemeHeader] = "http";
  req2_block[spdy::kHttp2PathHeader] = "/";
  spdy::SpdySerializedFrame req2(
      spdy_util_.ConstructSpdyHeaders(3, std::move(req2_block), MEDIUM, true));

  MockWrite writes1[] = {
      CreateMockWrite(connect, 0), CreateMockWrite(wrapped_req1, 2),
      CreateMockWrite(req2, 6),
  };

  spdy::SpdySerializedFrame conn_resp(
      spdy_util_.ConstructSpdyGetReply(nullptr, 0, 1));
  spdy::SpdySerializedFrame resp1(
      spdy_util_wrapped.ConstructSpdyGetReply(nullptr, 0, 1));
  spdy::SpdySerializedFrame body1(
      spdy_util_wrapped.ConstructSpdyDataFrame(1, true));
  spdy::SpdySerializedFrame wrapped_resp1(
      spdy_util_wrapped.ConstructWrappedSpdyFrame(resp1, 1));
  spdy::SpdySerializedFrame wrapped_body1(
      spdy_util_wrapped.ConstructWrappedSpdyFrame(body1, 1));
  spdy::SpdySerializedFrame resp2(
      spdy_util_.ConstructSpdyGetReply(nullptr, 0, 3));
  spdy::SpdySerializedFrame body2(spdy_util_.ConstructSpdyDataFrame(3, true));
  MockRead reads1[] = {
      CreateMockRead(conn_resp, 1),
      MockRead(ASYNC, ERR_IO_PENDING, 3),
      CreateMockRead(wrapped_resp1, 4),
      CreateMockRead(wrapped_body1, 5),
      MockRead(ASYNC, ERR_IO_PENDING, 7),
      CreateMockRead(resp2, 8),
      CreateMockRead(body2, 9),
      MockRead(SYNCHRONOUS, ERR_IO_PENDING, 10),
  };

  SequencedSocketData data1(reads1, writes1);
  MockConnect connect_data1(ASYNC, OK);
  data1.set_connect_data(connect_data1);

  session_deps_.proxy_resolution_service =
      ConfiguredProxyResolutionService::CreateFixedFromPacResultForTest(
          "HTTPS proxy:70", TRAFFIC_ANNOTATION_FOR_TESTS);
  session_deps_.net_log = net::NetLog::Get();
  SSLSocketDataProvider ssl1(ASYNC, OK);  // to the proxy
  ssl1.next_proto = kProtoHTTP2;
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl1);
  SSLSocketDataProvider ssl2(ASYNC, OK);  // to the server
  ssl2.next_proto = kProtoHTTP2;
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl2);
  session_deps_.socket_factory->AddSocketDataProvider(&data1);

  std::unique_ptr<HttpNetworkSession> session = CreateSession(&session_deps_);

  // Start the first transaction to set up the SpdySession
  HttpRequestInfo request1;
  request1.method = "GET";
  request1.url = GURL(https_url);
  request1.load_flags = 0;
  request1.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);
  HttpNetworkTransaction trans1(LOWEST, session.get());
  TestCompletionCallback callback1;
  int rv = trans1.Start(&request1, callback1.callback(), NetLogWithSource());

  // This pause is a hack to avoid running into https://crbug.com/497228.
  data1.RunUntilPaused();
  base::RunLoop().RunUntilIdle();
  data1.Resume();
  EXPECT_THAT(callback1.GetResult(rv), IsOk());
  EXPECT_TRUE(trans1.GetResponseInfo()->was_fetched_via_spdy);

  LoadTimingInfo load_timing_info1;
  EXPECT_TRUE(trans1.GetLoadTimingInfo(&load_timing_info1));
  TestLoadTimingNotReusedWithPac(load_timing_info1,
                                 CONNECT_TIMING_HAS_SSL_TIMES);

  // Now, start the HTTP request.
  HttpRequestInfo request2;
  request2.method = "GET";
  request2.url = GURL(http_url);
  request2.load_flags = 0;
  request2.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);
  HttpNetworkTransaction trans2(MEDIUM, session.get());
  TestCompletionCallback callback2;
  rv = trans2.Start(&request2, callback2.callback(), NetLogWithSource());

  // This pause is a hack to avoid running into https://crbug.com/497228.
  data1.RunUntilPaused();
  base::RunLoop().RunUntilIdle();
  data1.Resume();
  EXPECT_THAT(callback2.GetResult(rv), IsOk());

  EXPECT_TRUE(trans2.GetResponseInfo()->was_fetched_via_spdy);

  LoadTimingInfo load_timing_info2;
  EXPECT_TRUE(trans2.GetLoadTimingInfo(&load_timing_info2));
  // The established SPDY sessions is considered reused by the HTTP request.
  TestLoadTimingReusedWithPac(load_timing_info2);
  // HTTP requests over a SPDY session should have a different connection
  // socket_log_id than requests over a tunnel.
  EXPECT_NE(load_timing_info1.socket_log_id, load_timing_info2.socket_log_id);
}

// Test that in the case where we have a SPDY session to a SPDY proxy
// that we do not pool other origins that resolve to the same IP when
// the certificate does not match the new origin.
// http://crbug.com/134690
TEST_F(HttpNetworkTransactionTest, DoNotUseSpdySessionIfCertDoesNotMatch) {
  const std::string url1 = "http://www.example.org/";
  const std::string url2 = "https://news.example.org/";
  const std::string ip_addr = "1.2.3.4";

  // Second SpdyTestUtil instance for the second socket.
  SpdyTestUtil spdy_util_secure;

  // SPDY GET for HTTP URL (through SPDY proxy)
  spdy::Http2HeaderBlock headers(
      spdy_util_.ConstructGetHeaderBlockForProxy("http://www.example.org/"));
  spdy::SpdySerializedFrame req1(
      spdy_util_.ConstructSpdyHeaders(1, std::move(headers), LOWEST, true));

  MockWrite writes1[] = {
      CreateMockWrite(req1, 0),
  };

  spdy::SpdySerializedFrame resp1(
      spdy_util_.ConstructSpdyGetReply(nullptr, 0, 1));
  spdy::SpdySerializedFrame body1(spdy_util_.ConstructSpdyDataFrame(1, true));
  MockRead reads1[] = {
      MockRead(ASYNC, ERR_IO_PENDING, 1), CreateMockRead(resp1, 2),
      CreateMockRead(body1, 3), MockRead(ASYNC, OK, 4),  // EOF
  };

  SequencedSocketData data1(reads1, writes1);
  IPAddress ip;
  ASSERT_TRUE(ip.AssignFromIPLiteral(ip_addr));
  IPEndPoint peer_addr = IPEndPoint(ip, 443);
  MockConnect connect_data1(ASYNC, OK, peer_addr);
  data1.set_connect_data(connect_data1);

  // SPDY GET for HTTPS URL (direct)
  spdy::SpdySerializedFrame req2(
      spdy_util_secure.ConstructSpdyGet(url2.c_str(), 1, MEDIUM));

  MockWrite writes2[] = {
      CreateMockWrite(req2, 0),
  };

  spdy::SpdySerializedFrame resp2(
      spdy_util_secure.ConstructSpdyGetReply(nullptr, 0, 1));
  spdy::SpdySerializedFrame body2(
      spdy_util_secure.ConstructSpdyDataFrame(1, true));
  MockRead reads2[] = {CreateMockRead(resp2, 1), CreateMockRead(body2, 2),
                       MockRead(ASYNC, OK, 3)};

  SequencedSocketData data2(reads2, writes2);
  MockConnect connect_data2(ASYNC, OK);
  data2.set_connect_data(connect_data2);

  // Set up a proxy config that sends HTTP requests to a proxy, and
  // all others direct.
  ProxyConfig proxy_config;
  proxy_config.proxy_rules().ParseFromString("http=https://proxy:443");
  session_deps_.proxy_resolution_service =
      std::make_unique<ConfiguredProxyResolutionService>(
          std::make_unique<ProxyConfigServiceFixed>(ProxyConfigWithAnnotation(
              proxy_config, TRAFFIC_ANNOTATION_FOR_TESTS)),
          nullptr, nullptr, /*quick_check_enabled=*/true);

  SSLSocketDataProvider ssl1(ASYNC, OK);  // to the proxy
  ssl1.next_proto = kProtoHTTP2;
  // Load a valid cert.  Note, that this does not need to
  // be valid for proxy because the MockSSLClientSocket does
  // not actually verify it.  But SpdySession will use this
  // to see if it is valid for the new origin
  ssl1.ssl_info.cert =
      ImportCertFromFile(GetTestCertsDirectory(), "ok_cert.pem");
  ASSERT_TRUE(ssl1.ssl_info.cert);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl1);
  session_deps_.socket_factory->AddSocketDataProvider(&data1);

  SSLSocketDataProvider ssl2(ASYNC, OK);  // to the server
  ssl2.next_proto = kProtoHTTP2;
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl2);
  session_deps_.socket_factory->AddSocketDataProvider(&data2);

  session_deps_.host_resolver = std::make_unique<MockCachingHostResolver>();
  session_deps_.host_resolver->rules()->AddRule("news.example.org", ip_addr);
  session_deps_.host_resolver->rules()->AddRule("proxy", ip_addr);

  std::unique_ptr<HttpNetworkSession> session = CreateSession(&session_deps_);

  // Start the first transaction to set up the SpdySession
  HttpRequestInfo request1;
  request1.method = "GET";
  request1.url = GURL(url1);
  request1.load_flags = 0;
  request1.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);
  HttpNetworkTransaction trans1(LOWEST, session.get());
  TestCompletionCallback callback1;
  ASSERT_EQ(ERR_IO_PENDING,
            trans1.Start(&request1, callback1.callback(), NetLogWithSource()));
  // This pause is a hack to avoid running into https://crbug.com/497228.
  data1.RunUntilPaused();
  base::RunLoop().RunUntilIdle();
  data1.Resume();

  EXPECT_THAT(callback1.WaitForResult(), IsOk());
  EXPECT_TRUE(trans1.GetResponseInfo()->was_fetched_via_spdy);

  // Now, start the HTTP request
  HttpRequestInfo request2;
  request2.method = "GET";
  request2.url = GURL(url2);
  request2.load_flags = 0;
  request2.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);
  HttpNetworkTransaction trans2(MEDIUM, session.get());
  TestCompletionCallback callback2;
  EXPECT_EQ(ERR_IO_PENDING,
            trans2.Start(&request2, callback2.callback(), NetLogWithSource()));
  base::RunLoop().RunUntilIdle();

  ASSERT_TRUE(callback2.have_result());
  EXPECT_THAT(callback2.WaitForResult(), IsOk());
  EXPECT_TRUE(trans2.GetResponseInfo()->was_fetched_via_spdy);
}

// Test to verify that a failed socket read (due to an ERR_CONNECTION_CLOSED
// error) in SPDY session, removes the socket from pool and closes the SPDY
// session. Verify that new url's from the same HttpNetworkSession (and a new
// SpdySession) do work. http://crbug.com/224701
TEST_F(HttpNetworkTransactionTest, ErrorSocketNotConnected) {
  const std::string https_url = "https://www.example.org/";

  MockRead reads1[] = {
    MockRead(SYNCHRONOUS, ERR_CONNECTION_CLOSED, 0)
  };

  SequencedSocketData data1(reads1, base::span<MockWrite>());

  spdy::SpdySerializedFrame req2(
      spdy_util_.ConstructSpdyGet(https_url.c_str(), 1, MEDIUM));
  MockWrite writes2[] = {
      CreateMockWrite(req2, 0),
  };

  spdy::SpdySerializedFrame resp2(
      spdy_util_.ConstructSpdyGetReply(nullptr, 0, 1));
  spdy::SpdySerializedFrame body2(spdy_util_.ConstructSpdyDataFrame(1, true));
  MockRead reads2[] = {
      CreateMockRead(resp2, 1), CreateMockRead(body2, 2),
      MockRead(ASYNC, OK, 3)  // EOF
  };

  SequencedSocketData data2(reads2, writes2);

  SSLSocketDataProvider ssl1(ASYNC, OK);
  ssl1.next_proto = kProtoHTTP2;
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl1);
  session_deps_.socket_factory->AddSocketDataProvider(&data1);

  SSLSocketDataProvider ssl2(ASYNC, OK);
  ssl2.next_proto = kProtoHTTP2;
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl2);
  session_deps_.socket_factory->AddSocketDataProvider(&data2);

  std::unique_ptr<HttpNetworkSession> session(
      SpdySessionDependencies::SpdyCreateSession(&session_deps_));

  // Start the first transaction to set up the SpdySession and verify that
  // connection was closed.
  HttpRequestInfo request1;
  request1.method = "GET";
  request1.url = GURL(https_url);
  request1.load_flags = 0;
  request1.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);
  HttpNetworkTransaction trans1(MEDIUM, session.get());
  TestCompletionCallback callback1;
  EXPECT_EQ(ERR_IO_PENDING,
            trans1.Start(&request1, callback1.callback(), NetLogWithSource()));
  EXPECT_THAT(callback1.WaitForResult(), IsError(ERR_CONNECTION_CLOSED));

  // Now, start the second request and make sure it succeeds.
  HttpRequestInfo request2;
  request2.method = "GET";
  request2.url = GURL(https_url);
  request2.load_flags = 0;
  request2.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);
  HttpNetworkTransaction trans2(MEDIUM, session.get());
  TestCompletionCallback callback2;
  EXPECT_EQ(ERR_IO_PENDING,
            trans2.Start(&request2, callback2.callback(), NetLogWithSource()));

  ASSERT_THAT(callback2.WaitForResult(), IsOk());
  EXPECT_TRUE(trans2.GetResponseInfo()->was_fetched_via_spdy);
}

TEST_F(HttpNetworkTransactionTest, CloseIdleSpdySessionToOpenNewOne) {
  ClientSocketPoolManager::set_max_sockets_per_group(
      HttpNetworkSession::NORMAL_SOCKET_POOL, 1);
  ClientSocketPoolManager::set_max_sockets_per_pool(
      HttpNetworkSession::NORMAL_SOCKET_POOL, 1);

  // Use two different hosts with different IPs so they don't get pooled.
  session_deps_.host_resolver->rules()->AddRule("www.a.com", "10.0.0.1");
  session_deps_.host_resolver->rules()->AddRule("www.b.com", "10.0.0.2");
  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));

  SSLSocketDataProvider ssl1(ASYNC, OK);
  ssl1.next_proto = kProtoHTTP2;
  SSLSocketDataProvider ssl2(ASYNC, OK);
  ssl2.next_proto = kProtoHTTP2;
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl1);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl2);

  spdy::SpdySerializedFrame host1_req(
      spdy_util_.ConstructSpdyGet("https://www.a.com", 1, DEFAULT_PRIORITY));
  MockWrite spdy1_writes[] = {
      CreateMockWrite(host1_req, 0),
  };
  spdy::SpdySerializedFrame host1_resp(
      spdy_util_.ConstructSpdyGetReply(nullptr, 0, 1));
  spdy::SpdySerializedFrame host1_resp_body(
      spdy_util_.ConstructSpdyDataFrame(1, true));
  MockRead spdy1_reads[] = {
      CreateMockRead(host1_resp, 1), CreateMockRead(host1_resp_body, 2),
      MockRead(SYNCHRONOUS, ERR_IO_PENDING, 3),
  };

  // Use a separate test instance for the separate SpdySession that will be
  // created.
  SpdyTestUtil spdy_util_2;
  SequencedSocketData spdy1_data(spdy1_reads, spdy1_writes);
  session_deps_.socket_factory->AddSocketDataProvider(&spdy1_data);

  spdy::SpdySerializedFrame host2_req(
      spdy_util_2.ConstructSpdyGet("https://www.b.com", 1, DEFAULT_PRIORITY));
  MockWrite spdy2_writes[] = {
      CreateMockWrite(host2_req, 0),
  };
  spdy::SpdySerializedFrame host2_resp(
      spdy_util_2.ConstructSpdyGetReply(nullptr, 0, 1));
  spdy::SpdySerializedFrame host2_resp_body(
      spdy_util_2.ConstructSpdyDataFrame(1, true));
  MockRead spdy2_reads[] = {
      CreateMockRead(host2_resp, 1), CreateMockRead(host2_resp_body, 2),
      MockRead(SYNCHRONOUS, ERR_IO_PENDING, 3),
  };

  SequencedSocketData spdy2_data(spdy2_reads, spdy2_writes);
  session_deps_.socket_factory->AddSocketDataProvider(&spdy2_data);

  MockWrite http_write[] = {
    MockWrite("GET / HTTP/1.1\r\n"
              "Host: www.a.com\r\n"
              "Connection: keep-alive\r\n\r\n"),
  };

  MockRead http_read[] = {
    MockRead("HTTP/1.1 200 OK\r\n"),
    MockRead("Content-Type: text/html; charset=iso-8859-1\r\n"),
    MockRead("Content-Length: 6\r\n\r\n"),
    MockRead("hello!"),
  };
  StaticSocketDataProvider http_data(http_read, http_write);
  session_deps_.socket_factory->AddSocketDataProvider(&http_data);

  HostPortPair host_port_pair_a("www.a.com", 443);
  SpdySessionKey spdy_session_key_a(
      host_port_pair_a, ProxyServer::Direct(), PRIVACY_MODE_DISABLED,
      SpdySessionKey::IsProxySession::kFalse, SocketTag(),
      NetworkIsolationKey(), SecureDnsPolicy::kAllow);
  EXPECT_FALSE(
      HasSpdySession(session->spdy_session_pool(), spdy_session_key_a));

  TestCompletionCallback callback;
  HttpRequestInfo request1;
  request1.method = "GET";
  request1.url = GURL("https://www.a.com/");
  request1.load_flags = 0;
  request1.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);
  auto trans =
      std::make_unique<HttpNetworkTransaction>(DEFAULT_PRIORITY, session.get());

  int rv = trans->Start(&request1, callback.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));
  EXPECT_THAT(callback.WaitForResult(), IsOk());

  const HttpResponseInfo* response = trans->GetResponseInfo();
  ASSERT_TRUE(response);
  ASSERT_TRUE(response->headers);
  EXPECT_EQ("HTTP/1.1 200", response->headers->GetStatusLine());
  EXPECT_TRUE(response->was_fetched_via_spdy);
  EXPECT_TRUE(response->was_alpn_negotiated);

  std::string response_data;
  ASSERT_THAT(ReadTransaction(trans.get(), &response_data), IsOk());
  EXPECT_EQ("hello!", response_data);
  trans.reset();
  EXPECT_TRUE(
      HasSpdySession(session->spdy_session_pool(), spdy_session_key_a));

  HostPortPair host_port_pair_b("www.b.com", 443);
  SpdySessionKey spdy_session_key_b(
      host_port_pair_b, ProxyServer::Direct(), PRIVACY_MODE_DISABLED,
      SpdySessionKey::IsProxySession::kFalse, SocketTag(),
      NetworkIsolationKey(), SecureDnsPolicy::kAllow);
  EXPECT_FALSE(
      HasSpdySession(session->spdy_session_pool(), spdy_session_key_b));
  HttpRequestInfo request2;
  request2.method = "GET";
  request2.url = GURL("https://www.b.com/");
  request2.load_flags = 0;
  request2.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);
  trans =
      std::make_unique<HttpNetworkTransaction>(DEFAULT_PRIORITY, session.get());

  rv = trans->Start(&request2, callback.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));
  EXPECT_THAT(callback.WaitForResult(), IsOk());

  response = trans->GetResponseInfo();
  ASSERT_TRUE(response);
  ASSERT_TRUE(response->headers);
  EXPECT_EQ("HTTP/1.1 200", response->headers->GetStatusLine());
  EXPECT_TRUE(response->was_fetched_via_spdy);
  EXPECT_TRUE(response->was_alpn_negotiated);
  ASSERT_THAT(ReadTransaction(trans.get(), &response_data), IsOk());
  EXPECT_EQ("hello!", response_data);
  EXPECT_FALSE(
      HasSpdySession(session->spdy_session_pool(), spdy_session_key_a));
  EXPECT_TRUE(
      HasSpdySession(session->spdy_session_pool(), spdy_session_key_b));

  HostPortPair host_port_pair_a1("www.a.com", 80);
  SpdySessionKey spdy_session_key_a1(
      host_port_pair_a1, ProxyServer::Direct(), PRIVACY_MODE_DISABLED,
      SpdySessionKey::IsProxySession::kFalse, SocketTag(),
      NetworkIsolationKey(), SecureDnsPolicy::kAllow);
  EXPECT_FALSE(
      HasSpdySession(session->spdy_session_pool(), spdy_session_key_a1));
  HttpRequestInfo request3;
  request3.method = "GET";
  request3.url = GURL("http://www.a.com/");
  request3.load_flags = 0;
  request3.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);
  trans =
      std::make_unique<HttpNetworkTransaction>(DEFAULT_PRIORITY, session.get());

  rv = trans->Start(&request3, callback.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));
  EXPECT_THAT(callback.WaitForResult(), IsOk());

  response = trans->GetResponseInfo();
  ASSERT_TRUE(response);
  ASSERT_TRUE(response->headers);
  EXPECT_EQ("HTTP/1.1 200 OK", response->headers->GetStatusLine());
  EXPECT_FALSE(response->was_fetched_via_spdy);
  EXPECT_FALSE(response->was_alpn_negotiated);
  ASSERT_THAT(ReadTransaction(trans.get(), &response_data), IsOk());
  EXPECT_EQ("hello!", response_data);
  EXPECT_FALSE(
      HasSpdySession(session->spdy_session_pool(), spdy_session_key_a));
  EXPECT_FALSE(
      HasSpdySession(session->spdy_session_pool(), spdy_session_key_b));
}

TEST_F(HttpNetworkTransactionTest, HttpSyncConnectError) {
  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("http://www.example.org/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));
  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

  MockConnect mock_connect(SYNCHRONOUS, ERR_NAME_NOT_RESOLVED);
  StaticSocketDataProvider data;
  data.set_connect_data(mock_connect);
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  TestCompletionCallback callback;

  int rv = trans.Start(&request, callback.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback.WaitForResult();
  EXPECT_THAT(rv, IsError(ERR_NAME_NOT_RESOLVED));

  ConnectionAttempts attempts = trans.GetConnectionAttempts();
  ASSERT_EQ(1u, attempts.size());
  EXPECT_THAT(attempts[0].result, IsError(ERR_NAME_NOT_RESOLVED));

  IPEndPoint endpoint;
  EXPECT_FALSE(trans.GetRemoteEndpoint(&endpoint));
  EXPECT_TRUE(endpoint.address().empty());
}

TEST_F(HttpNetworkTransactionTest, HttpAsyncConnectError) {
  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("http://www.example.org/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));
  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

  MockConnect mock_connect(ASYNC, ERR_NAME_NOT_RESOLVED);
  StaticSocketDataProvider data;
  data.set_connect_data(mock_connect);
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  TestCompletionCallback callback;

  int rv = trans.Start(&request, callback.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback.WaitForResult();
  EXPECT_THAT(rv, IsError(ERR_NAME_NOT_RESOLVED));

  ConnectionAttempts attempts = trans.GetConnectionAttempts();
  ASSERT_EQ(1u, attempts.size());
  EXPECT_THAT(attempts[0].result, IsError(ERR_NAME_NOT_RESOLVED));

  IPEndPoint endpoint;
  EXPECT_FALSE(trans.GetRemoteEndpoint(&endpoint));
  EXPECT_TRUE(endpoint.address().empty());
}

TEST_F(HttpNetworkTransactionTest, HttpSyncWriteError) {
  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("http://www.example.org/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));
  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

  MockWrite data_writes[] = {
    MockWrite(SYNCHRONOUS, ERR_CONNECTION_RESET),
  };
  MockRead data_reads[] = {
    MockRead(SYNCHRONOUS, ERR_UNEXPECTED),  // Should not be reached.
  };

  StaticSocketDataProvider data(data_reads, data_writes);
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  TestCompletionCallback callback;

  int rv = trans.Start(&request, callback.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback.WaitForResult();
  EXPECT_THAT(rv, IsError(ERR_CONNECTION_RESET));
}

TEST_F(HttpNetworkTransactionTest, HttpAsyncWriteError) {
  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("http://www.example.org/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));
  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

  MockWrite data_writes[] = {
    MockWrite(ASYNC, ERR_CONNECTION_RESET),
  };
  MockRead data_reads[] = {
    MockRead(SYNCHRONOUS, ERR_UNEXPECTED),  // Should not be reached.
  };

  StaticSocketDataProvider data(data_reads, data_writes);
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  TestCompletionCallback callback;

  int rv = trans.Start(&request, callback.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback.WaitForResult();
  EXPECT_THAT(rv, IsError(ERR_CONNECTION_RESET));
}

TEST_F(HttpNetworkTransactionTest, HttpSyncReadError) {
  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("http://www.example.org/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));
  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

  MockWrite data_writes[] = {
      MockWrite(
          "GET / HTTP/1.1\r\n"
          "Host: www.example.org\r\n"
          "Connection: keep-alive\r\n\r\n"),
  };
  MockRead data_reads[] = {
    MockRead(SYNCHRONOUS, ERR_CONNECTION_RESET),
  };

  StaticSocketDataProvider data(data_reads, data_writes);
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  TestCompletionCallback callback;

  int rv = trans.Start(&request, callback.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback.WaitForResult();
  EXPECT_THAT(rv, IsError(ERR_CONNECTION_RESET));
}

TEST_F(HttpNetworkTransactionTest, HttpAsyncReadError) {
  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("http://www.example.org/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));
  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

  MockWrite data_writes[] = {
      MockWrite(
          "GET / HTTP/1.1\r\n"
          "Host: www.example.org\r\n"
          "Connection: keep-alive\r\n\r\n"),
  };
  MockRead data_reads[] = {
    MockRead(ASYNC, ERR_CONNECTION_RESET),
  };

  StaticSocketDataProvider data(data_reads, data_writes);
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  TestCompletionCallback callback;

  int rv = trans.Start(&request, callback.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback.WaitForResult();
  EXPECT_THAT(rv, IsError(ERR_CONNECTION_RESET));
}

// Tests that when a used socket is returned to the SSL socket pool, it's closed
// if the transport socket pool is stalled on the global socket limit.
TEST_F(HttpNetworkTransactionTest, CloseSSLSocketOnIdleForHttpRequest) {
  ClientSocketPoolManager::set_max_sockets_per_group(
      HttpNetworkSession::NORMAL_SOCKET_POOL, 1);
  ClientSocketPoolManager::set_max_sockets_per_pool(
      HttpNetworkSession::NORMAL_SOCKET_POOL, 1);

  // Set up SSL request.

  HttpRequestInfo ssl_request;
  ssl_request.method = "GET";
  ssl_request.url = GURL("https://www.example.org/");
  ssl_request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  MockWrite ssl_writes[] = {
      MockWrite(
          "GET / HTTP/1.1\r\n"
          "Host: www.example.org\r\n"
          "Connection: keep-alive\r\n\r\n"),
  };
  MockRead ssl_reads[] = {
    MockRead("HTTP/1.1 200 OK\r\n"),
    MockRead("Content-Length: 11\r\n\r\n"),
    MockRead("hello world"),
    MockRead(SYNCHRONOUS, OK),
  };
  StaticSocketDataProvider ssl_data(ssl_reads, ssl_writes);
  session_deps_.socket_factory->AddSocketDataProvider(&ssl_data);

  SSLSocketDataProvider ssl(ASYNC, OK);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl);

  // Set up HTTP request.

  HttpRequestInfo http_request;
  http_request.method = "GET";
  http_request.url = GURL("http://www.example.org/");
  http_request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  MockWrite http_writes[] = {
      MockWrite(
          "GET / HTTP/1.1\r\n"
          "Host: www.example.org\r\n"
          "Connection: keep-alive\r\n\r\n"),
  };
  MockRead http_reads[] = {
    MockRead("HTTP/1.1 200 OK\r\n"),
    MockRead("Content-Length: 7\r\n\r\n"),
    MockRead("falafel"),
    MockRead(SYNCHRONOUS, OK),
  };
  StaticSocketDataProvider http_data(http_reads, http_writes);
  session_deps_.socket_factory->AddSocketDataProvider(&http_data);

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));

  // Start the SSL request.
  TestCompletionCallback ssl_callback;
  HttpNetworkTransaction ssl_trans(DEFAULT_PRIORITY, session.get());
  ASSERT_EQ(ERR_IO_PENDING,
            ssl_trans.Start(&ssl_request, ssl_callback.callback(),
                            NetLogWithSource()));

  // Start the HTTP request.  Pool should stall.
  TestCompletionCallback http_callback;
  HttpNetworkTransaction http_trans(DEFAULT_PRIORITY, session.get());
  ASSERT_EQ(ERR_IO_PENDING,
            http_trans.Start(&http_request, http_callback.callback(),
                             NetLogWithSource()));
  EXPECT_TRUE(IsTransportSocketPoolStalled(session.get()));

  // Wait for response from SSL request.
  ASSERT_THAT(ssl_callback.WaitForResult(), IsOk());
  std::string response_data;
  ASSERT_THAT(ReadTransaction(&ssl_trans, &response_data), IsOk());
  EXPECT_EQ("hello world", response_data);

  // The SSL socket should automatically be closed, so the HTTP request can
  // start.
  EXPECT_EQ(0, GetIdleSocketCountInTransportSocketPool(session.get()));
  ASSERT_FALSE(IsTransportSocketPoolStalled(session.get()));

  // The HTTP request can now complete.
  ASSERT_THAT(http_callback.WaitForResult(), IsOk());
  ASSERT_THAT(ReadTransaction(&http_trans, &response_data), IsOk());
  EXPECT_EQ("falafel", response_data);

  EXPECT_EQ(1, GetIdleSocketCountInTransportSocketPool(session.get()));
}

// Tests that when a SSL connection is established but there's no corresponding
// request that needs it, the new socket is closed if the transport socket pool
// is stalled on the global socket limit.
TEST_F(HttpNetworkTransactionTest, CloseSSLSocketOnIdleForHttpRequest2) {
  ClientSocketPoolManager::set_max_sockets_per_group(
      HttpNetworkSession::NORMAL_SOCKET_POOL, 1);
  ClientSocketPoolManager::set_max_sockets_per_pool(
      HttpNetworkSession::NORMAL_SOCKET_POOL, 1);

  // Set up an ssl request.

  HttpRequestInfo ssl_request;
  ssl_request.method = "GET";
  ssl_request.url = GURL("https://www.foopy.com/");
  ssl_request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  // No data will be sent on the SSL socket.
  StaticSocketDataProvider ssl_data;
  session_deps_.socket_factory->AddSocketDataProvider(&ssl_data);

  SSLSocketDataProvider ssl(ASYNC, OK);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl);

  // Set up HTTP request.

  HttpRequestInfo http_request;
  http_request.method = "GET";
  http_request.url = GURL("http://www.example.org/");
  http_request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  MockWrite http_writes[] = {
      MockWrite(
          "GET / HTTP/1.1\r\n"
          "Host: www.example.org\r\n"
          "Connection: keep-alive\r\n\r\n"),
  };
  MockRead http_reads[] = {
    MockRead("HTTP/1.1 200 OK\r\n"),
    MockRead("Content-Length: 7\r\n\r\n"),
    MockRead("falafel"),
    MockRead(SYNCHRONOUS, OK),
  };
  StaticSocketDataProvider http_data(http_reads, http_writes);
  session_deps_.socket_factory->AddSocketDataProvider(&http_data);

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));

  // Preconnect an SSL socket.  A preconnect is needed because connect jobs are
  // cancelled when a normal transaction is cancelled.
  HttpStreamFactory* http_stream_factory = session->http_stream_factory();
  http_stream_factory->PreconnectStreams(1, ssl_request);
  EXPECT_EQ(0, GetIdleSocketCountInTransportSocketPool(session.get()));

  // Start the HTTP request.  Pool should stall.
  TestCompletionCallback http_callback;
  HttpNetworkTransaction http_trans(DEFAULT_PRIORITY, session.get());
  ASSERT_EQ(ERR_IO_PENDING,
            http_trans.Start(&http_request, http_callback.callback(),
                             NetLogWithSource()));
  EXPECT_TRUE(IsTransportSocketPoolStalled(session.get()));

  // The SSL connection will automatically be closed once the connection is
  // established, to let the HTTP request start.
  ASSERT_THAT(http_callback.WaitForResult(), IsOk());
  std::string response_data;
  ASSERT_THAT(ReadTransaction(&http_trans, &response_data), IsOk());
  EXPECT_EQ("falafel", response_data);

  EXPECT_EQ(1, GetIdleSocketCountInTransportSocketPool(session.get()));
}

TEST_F(HttpNetworkTransactionTest, PostReadsErrorResponseAfterReset) {
  std::vector<std::unique_ptr<UploadElementReader>> element_readers;
  element_readers.push_back(
      std::make_unique<UploadBytesElementReader>("foo", 3));
  ElementsUploadDataStream upload_data_stream(std::move(element_readers), 0);

  HttpRequestInfo request;
  request.method = "POST";
  request.url = GURL("http://www.foo.com/");
  request.upload_data_stream = &upload_data_stream;
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));
  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());
  // Send headers successfully, but get an error while sending the body.
  MockWrite data_writes[] = {
    MockWrite("POST / HTTP/1.1\r\n"
              "Host: www.foo.com\r\n"
              "Connection: keep-alive\r\n"
              "Content-Length: 3\r\n\r\n"),
    MockWrite(SYNCHRONOUS, ERR_CONNECTION_RESET),
  };

  MockRead data_reads[] = {
    MockRead("HTTP/1.0 400 Not OK\r\n\r\n"),
    MockRead("hello world"),
    MockRead(SYNCHRONOUS, OK),
  };
  StaticSocketDataProvider data(data_reads, data_writes);
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  TestCompletionCallback callback;

  int rv = trans.Start(&request, callback.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback.WaitForResult();
  EXPECT_THAT(rv, IsOk());

  const HttpResponseInfo* response = trans.GetResponseInfo();
  ASSERT_TRUE(response);

  EXPECT_TRUE(response->headers);
  EXPECT_EQ("HTTP/1.0 400 Not OK", response->headers->GetStatusLine());

  std::string response_data;
  rv = ReadTransaction(&trans, &response_data);
  EXPECT_THAT(rv, IsOk());
  EXPECT_EQ("hello world", response_data);
}

// This test makes sure the retry logic doesn't trigger when reading an error
// response from a server that rejected a POST with a CONNECTION_RESET.
TEST_F(HttpNetworkTransactionTest,
       PostReadsErrorResponseAfterResetOnReusedSocket) {
  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));
  MockWrite data_writes[] = {
    MockWrite("GET / HTTP/1.1\r\n"
              "Host: www.foo.com\r\n"
              "Connection: keep-alive\r\n\r\n"),
    MockWrite("POST / HTTP/1.1\r\n"
              "Host: www.foo.com\r\n"
              "Connection: keep-alive\r\n"
              "Content-Length: 3\r\n\r\n"),
    MockWrite(SYNCHRONOUS, ERR_CONNECTION_RESET),
  };

  MockRead data_reads[] = {
    MockRead("HTTP/1.1 200 Peachy\r\n"
             "Content-Length: 14\r\n\r\n"),
    MockRead("first response"),
    MockRead("HTTP/1.1 400 Not OK\r\n"
             "Content-Length: 15\r\n\r\n"),
    MockRead("second response"),
    MockRead(SYNCHRONOUS, OK),
  };
  StaticSocketDataProvider data(data_reads, data_writes);
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  TestCompletionCallback callback;
  HttpRequestInfo request1;
  request1.method = "GET";
  request1.url = GURL("http://www.foo.com/");
  request1.load_flags = 0;
  request1.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  auto trans1 =
      std::make_unique<HttpNetworkTransaction>(DEFAULT_PRIORITY, session.get());
  int rv = trans1->Start(&request1, callback.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback.WaitForResult();
  EXPECT_THAT(rv, IsOk());

  const HttpResponseInfo* response1 = trans1->GetResponseInfo();
  ASSERT_TRUE(response1);

  EXPECT_TRUE(response1->headers);
  EXPECT_EQ("HTTP/1.1 200 Peachy", response1->headers->GetStatusLine());

  std::string response_data1;
  rv = ReadTransaction(trans1.get(), &response_data1);
  EXPECT_THAT(rv, IsOk());
  EXPECT_EQ("first response", response_data1);
  // Delete the transaction to release the socket back into the socket pool.
  trans1.reset();

  std::vector<std::unique_ptr<UploadElementReader>> element_readers;
  element_readers.push_back(
      std::make_unique<UploadBytesElementReader>("foo", 3));
  ElementsUploadDataStream upload_data_stream(std::move(element_readers), 0);

  HttpRequestInfo request2;
  request2.method = "POST";
  request2.url = GURL("http://www.foo.com/");
  request2.upload_data_stream = &upload_data_stream;
  request2.load_flags = 0;
  request2.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  HttpNetworkTransaction trans2(DEFAULT_PRIORITY, session.get());
  rv = trans2.Start(&request2, callback.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback.WaitForResult();
  EXPECT_THAT(rv, IsOk());

  const HttpResponseInfo* response2 = trans2.GetResponseInfo();
  ASSERT_TRUE(response2);

  EXPECT_TRUE(response2->headers);
  EXPECT_EQ("HTTP/1.1 400 Not OK", response2->headers->GetStatusLine());

  std::string response_data2;
  rv = ReadTransaction(&trans2, &response_data2);
  EXPECT_THAT(rv, IsOk());
  EXPECT_EQ("second response", response_data2);
}

TEST_F(HttpNetworkTransactionTest,
       PostReadsErrorResponseAfterResetPartialBodySent) {
  std::vector<std::unique_ptr<UploadElementReader>> element_readers;
  element_readers.push_back(
      std::make_unique<UploadBytesElementReader>("foo", 3));
  ElementsUploadDataStream upload_data_stream(std::move(element_readers), 0);

  HttpRequestInfo request;
  request.method = "POST";
  request.url = GURL("http://www.foo.com/");
  request.upload_data_stream = &upload_data_stream;
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));
  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());
  // Send headers successfully, but get an error while sending the body.
  MockWrite data_writes[] = {
    MockWrite("POST / HTTP/1.1\r\n"
              "Host: www.foo.com\r\n"
              "Connection: keep-alive\r\n"
              "Content-Length: 3\r\n\r\n"
              "fo"),
    MockWrite(SYNCHRONOUS, ERR_CONNECTION_RESET),
  };

  MockRead data_reads[] = {
    MockRead("HTTP/1.0 400 Not OK\r\n\r\n"),
    MockRead("hello world"),
    MockRead(SYNCHRONOUS, OK),
  };
  StaticSocketDataProvider data(data_reads, data_writes);
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  TestCompletionCallback callback;

  int rv = trans.Start(&request, callback.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback.WaitForResult();
  EXPECT_THAT(rv, IsOk());

  const HttpResponseInfo* response = trans.GetResponseInfo();
  ASSERT_TRUE(response);

  EXPECT_TRUE(response->headers);
  EXPECT_EQ("HTTP/1.0 400 Not OK", response->headers->GetStatusLine());

  std::string response_data;
  rv = ReadTransaction(&trans, &response_data);
  EXPECT_THAT(rv, IsOk());
  EXPECT_EQ("hello world", response_data);
}

// This tests the more common case than the previous test, where headers and
// body are not merged into a single request.
TEST_F(HttpNetworkTransactionTest, ChunkedPostReadsErrorResponseAfterReset) {
  ChunkedUploadDataStream upload_data_stream(0);

  HttpRequestInfo request;
  request.method = "POST";
  request.url = GURL("http://www.foo.com/");
  request.upload_data_stream = &upload_data_stream;
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));
  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());
  // Send headers successfully, but get an error while sending the body.
  MockWrite data_writes[] = {
    MockWrite("POST / HTTP/1.1\r\n"
              "Host: www.foo.com\r\n"
              "Connection: keep-alive\r\n"
              "Transfer-Encoding: chunked\r\n\r\n"),
    MockWrite(SYNCHRONOUS, ERR_CONNECTION_RESET),
  };

  MockRead data_reads[] = {
    MockRead("HTTP/1.0 400 Not OK\r\n\r\n"),
    MockRead("hello world"),
    MockRead(SYNCHRONOUS, OK),
  };
  StaticSocketDataProvider data(data_reads, data_writes);
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  TestCompletionCallback callback;

  int rv = trans.Start(&request, callback.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));
  // Make sure the headers are sent before adding a chunk.  This ensures that
  // they can't be merged with the body in a single send.  Not currently
  // necessary since a chunked body is never merged with headers, but this makes
  // the test more future proof.
  base::RunLoop().RunUntilIdle();

  upload_data_stream.AppendData("last chunk", 10, true);

  rv = callback.WaitForResult();
  EXPECT_THAT(rv, IsOk());

  const HttpResponseInfo* response = trans.GetResponseInfo();
  ASSERT_TRUE(response);

  EXPECT_TRUE(response->headers);
  EXPECT_EQ("HTTP/1.0 400 Not OK", response->headers->GetStatusLine());

  std::string response_data;
  rv = ReadTransaction(&trans, &response_data);
  EXPECT_THAT(rv, IsOk());
  EXPECT_EQ("hello world", response_data);
}

TEST_F(HttpNetworkTransactionTest, PostReadsErrorResponseAfterResetAnd100) {
  std::vector<std::unique_ptr<UploadElementReader>> element_readers;
  element_readers.push_back(
      std::make_unique<UploadBytesElementReader>("foo", 3));
  ElementsUploadDataStream upload_data_stream(std::move(element_readers), 0);

  HttpRequestInfo request;
  request.method = "POST";
  request.url = GURL("http://www.foo.com/");
  request.upload_data_stream = &upload_data_stream;
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));
  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

  MockWrite data_writes[] = {
    MockWrite("POST / HTTP/1.1\r\n"
              "Host: www.foo.com\r\n"
              "Connection: keep-alive\r\n"
              "Content-Length: 3\r\n\r\n"),
    MockWrite(SYNCHRONOUS, ERR_CONNECTION_RESET),
  };

  MockRead data_reads[] = {
    MockRead("HTTP/1.0 100 Continue\r\n\r\n"),
    MockRead("HTTP/1.0 400 Not OK\r\n\r\n"),
    MockRead("hello world"),
    MockRead(SYNCHRONOUS, OK),
  };
  StaticSocketDataProvider data(data_reads, data_writes);
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  TestCompletionCallback callback;

  int rv = trans.Start(&request, callback.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback.WaitForResult();
  EXPECT_THAT(rv, IsOk());

  const HttpResponseInfo* response = trans.GetResponseInfo();
  ASSERT_TRUE(response);

  EXPECT_TRUE(response->headers);
  EXPECT_EQ("HTTP/1.0 400 Not OK", response->headers->GetStatusLine());

  std::string response_data;
  rv = ReadTransaction(&trans, &response_data);
  EXPECT_THAT(rv, IsOk());
  EXPECT_EQ("hello world", response_data);
}

TEST_F(HttpNetworkTransactionTest, PostIgnoresNonErrorResponseAfterReset) {
  std::vector<std::unique_ptr<UploadElementReader>> element_readers;
  element_readers.push_back(
      std::make_unique<UploadBytesElementReader>("foo", 3));
  ElementsUploadDataStream upload_data_stream(std::move(element_readers), 0);

  HttpRequestInfo request;
  request.method = "POST";
  request.url = GURL("http://www.foo.com/");
  request.upload_data_stream = &upload_data_stream;
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));
  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());
  // Send headers successfully, but get an error while sending the body.
  MockWrite data_writes[] = {
    MockWrite("POST / HTTP/1.1\r\n"
              "Host: www.foo.com\r\n"
              "Connection: keep-alive\r\n"
              "Content-Length: 3\r\n\r\n"),
    MockWrite(SYNCHRONOUS, ERR_CONNECTION_RESET),
  };

  MockRead data_reads[] = {
    MockRead("HTTP/1.0 200 Just Dandy\r\n\r\n"),
    MockRead("hello world"),
    MockRead(SYNCHRONOUS, OK),
  };
  StaticSocketDataProvider data(data_reads, data_writes);
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  TestCompletionCallback callback;

  int rv = trans.Start(&request, callback.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback.WaitForResult();
  EXPECT_THAT(rv, IsError(ERR_CONNECTION_RESET));
}

TEST_F(HttpNetworkTransactionTest,
       PostIgnoresNonErrorResponseAfterResetAnd100) {
  std::vector<std::unique_ptr<UploadElementReader>> element_readers;
  element_readers.push_back(
      std::make_unique<UploadBytesElementReader>("foo", 3));
  ElementsUploadDataStream upload_data_stream(std::move(element_readers), 0);

  HttpRequestInfo request;
  request.method = "POST";
  request.url = GURL("http://www.foo.com/");
  request.upload_data_stream = &upload_data_stream;
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));
  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());
  // Send headers successfully, but get an error while sending the body.
  MockWrite data_writes[] = {
    MockWrite("POST / HTTP/1.1\r\n"
              "Host: www.foo.com\r\n"
              "Connection: keep-alive\r\n"
              "Content-Length: 3\r\n\r\n"),
    MockWrite(SYNCHRONOUS, ERR_CONNECTION_RESET),
  };

  MockRead data_reads[] = {
    MockRead("HTTP/1.0 100 Continue\r\n\r\n"),
    MockRead("HTTP/1.0 302 Redirect\r\n"),
    MockRead("Location: http://somewhere-else.com/\r\n"),
    MockRead("Content-Length: 0\r\n\r\n"),
    MockRead(SYNCHRONOUS, OK),
  };
  StaticSocketDataProvider data(data_reads, data_writes);
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  TestCompletionCallback callback;

  int rv = trans.Start(&request, callback.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback.WaitForResult();
  EXPECT_THAT(rv, IsError(ERR_CONNECTION_RESET));
}

TEST_F(HttpNetworkTransactionTest, PostIgnoresHttp09ResponseAfterReset) {
  std::vector<std::unique_ptr<UploadElementReader>> element_readers;
  element_readers.push_back(
      std::make_unique<UploadBytesElementReader>("foo", 3));
  ElementsUploadDataStream upload_data_stream(std::move(element_readers), 0);

  HttpRequestInfo request;
  request.method = "POST";
  request.url = GURL("http://www.foo.com/");
  request.upload_data_stream = &upload_data_stream;
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));
  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());
  // Send headers successfully, but get an error while sending the body.
  MockWrite data_writes[] = {
    MockWrite("POST / HTTP/1.1\r\n"
              "Host: www.foo.com\r\n"
              "Connection: keep-alive\r\n"
              "Content-Length: 3\r\n\r\n"),
    MockWrite(SYNCHRONOUS, ERR_CONNECTION_RESET),
  };

  MockRead data_reads[] = {
    MockRead("HTTP 0.9 rocks!"),
    MockRead(SYNCHRONOUS, OK),
  };
  StaticSocketDataProvider data(data_reads, data_writes);
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  TestCompletionCallback callback;

  int rv = trans.Start(&request, callback.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback.WaitForResult();
  EXPECT_THAT(rv, IsError(ERR_CONNECTION_RESET));
}

TEST_F(HttpNetworkTransactionTest, PostIgnoresPartial400HeadersAfterReset) {
  std::vector<std::unique_ptr<UploadElementReader>> element_readers;
  element_readers.push_back(
      std::make_unique<UploadBytesElementReader>("foo", 3));
  ElementsUploadDataStream upload_data_stream(std::move(element_readers), 0);

  HttpRequestInfo request;
  request.method = "POST";
  request.url = GURL("http://www.foo.com/");
  request.upload_data_stream = &upload_data_stream;
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));
  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());
  // Send headers successfully, but get an error while sending the body.
  MockWrite data_writes[] = {
    MockWrite("POST / HTTP/1.1\r\n"
              "Host: www.foo.com\r\n"
              "Connection: keep-alive\r\n"
              "Content-Length: 3\r\n\r\n"),
    MockWrite(SYNCHRONOUS, ERR_CONNECTION_RESET),
  };

  MockRead data_reads[] = {
    MockRead("HTTP/1.0 400 Not a Full Response\r\n"),
    MockRead(SYNCHRONOUS, OK),
  };
  StaticSocketDataProvider data(data_reads, data_writes);
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  TestCompletionCallback callback;

  int rv = trans.Start(&request, callback.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback.WaitForResult();
  EXPECT_THAT(rv, IsError(ERR_CONNECTION_RESET));
}

#if BUILDFLAG(ENABLE_WEBSOCKETS)

namespace {

void AddWebSocketHeaders(HttpRequestHeaders* headers) {
  headers->SetHeader("Connection", "Upgrade");
  headers->SetHeader("Upgrade", "websocket");
  headers->SetHeader("Origin", "http://www.example.org");
  headers->SetHeader("Sec-WebSocket-Version", "13");
}

}  // namespace

TEST_F(HttpNetworkTransactionTest, CreateWebSocketHandshakeStream) {
  for (bool secure : {true, false}) {
    MockWrite data_writes[] = {
        MockWrite("GET / HTTP/1.1\r\n"
                  "Host: www.example.org\r\n"
                  "Connection: Upgrade\r\n"
                  "Upgrade: websocket\r\n"
                  "Origin: http://www.example.org\r\n"
                  "Sec-WebSocket-Version: 13\r\n"
                  "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
                  "Sec-WebSocket-Extensions: permessage-deflate; "
                  "client_max_window_bits\r\n\r\n")};

    MockRead data_reads[] = {
        MockRead("HTTP/1.1 101 Switching Protocols\r\n"
                 "Upgrade: websocket\r\n"
                 "Connection: Upgrade\r\n"
                 "Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n\r\n")};

    StaticSocketDataProvider data(data_reads, data_writes);
    session_deps_.socket_factory->AddSocketDataProvider(&data);
    SSLSocketDataProvider ssl(ASYNC, OK);
    if (secure)
      session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl);

    HttpRequestInfo request;
    request.method = "GET";
    request.url =
        GURL(secure ? "ws://www.example.org/" : "wss://www.example.org/");
    AddWebSocketHeaders(&request.extra_headers);
    request.traffic_annotation =
        net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

    TestWebSocketHandshakeStreamCreateHelper
        websocket_handshake_stream_create_helper;

    std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));
    HttpNetworkTransaction trans(LOW, session.get());
    trans.SetWebSocketHandshakeStreamCreateHelper(
        &websocket_handshake_stream_create_helper);

    TestCompletionCallback callback;
    int rv = trans.Start(&request, callback.callback(), NetLogWithSource());
    EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

    const HttpStreamRequest* stream_request = trans.stream_request_.get();
    ASSERT_TRUE(stream_request);
    EXPECT_EQ(&websocket_handshake_stream_create_helper,
              stream_request->websocket_handshake_stream_create_helper());

    rv = callback.WaitForResult();
    EXPECT_THAT(rv, IsOk());

    EXPECT_TRUE(data.AllReadDataConsumed());
    EXPECT_TRUE(data.AllWriteDataConsumed());
  }
}

// Verify that proxy headers are not sent to the destination server when
// establishing a tunnel for a secure WebSocket connection.
TEST_F(HttpNetworkTransactionTest, ProxyHeadersNotSentOverWssTunnel) {
  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("wss://www.example.org/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);
  AddWebSocketHeaders(&request.extra_headers);

  // Configure against proxy server "myproxy:70".
  session_deps_.proxy_resolution_service =
      ConfiguredProxyResolutionService::CreateFixedFromPacResultForTest(
          "PROXY myproxy:70", TRAFFIC_ANNOTATION_FOR_TESTS);

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));

  // Since a proxy is configured, try to establish a tunnel.
  MockWrite data_writes[] = {
      MockWrite("CONNECT www.example.org:443 HTTP/1.1\r\n"
                "Host: www.example.org:443\r\n"
                "Proxy-Connection: keep-alive\r\n\r\n"),

      // After calling trans->RestartWithAuth(), this is the request we should
      // be issuing -- the final header line contains the credentials.
      MockWrite("CONNECT www.example.org:443 HTTP/1.1\r\n"
                "Host: www.example.org:443\r\n"
                "Proxy-Connection: keep-alive\r\n"
                "Proxy-Authorization: Basic Zm9vOmJhcg==\r\n\r\n"),

      MockWrite("GET / HTTP/1.1\r\n"
                "Host: www.example.org\r\n"
                "Connection: Upgrade\r\n"
                "Upgrade: websocket\r\n"
                "Origin: http://www.example.org\r\n"
                "Sec-WebSocket-Version: 13\r\n"
                "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
                "Sec-WebSocket-Extensions: permessage-deflate; "
                "client_max_window_bits\r\n\r\n")};

  // The proxy responds to the connect with a 407, using a persistent
  // connection.
  MockRead data_reads[] = {
      // No credentials.
      MockRead("HTTP/1.1 407 Proxy Authentication Required\r\n"
               "Proxy-Authenticate: Basic realm=\"MyRealm1\"\r\n"
               "Content-Length: 0\r\n"
               "Proxy-Connection: keep-alive\r\n\r\n"),

      MockRead("HTTP/1.1 200 Connection Established\r\n\r\n"),

      MockRead("HTTP/1.1 101 Switching Protocols\r\n"
               "Upgrade: websocket\r\n"
               "Connection: Upgrade\r\n"
               "Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n\r\n")};

  StaticSocketDataProvider data(data_reads, data_writes);
  session_deps_.socket_factory->AddSocketDataProvider(&data);
  SSLSocketDataProvider ssl(ASYNC, OK);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl);

  TestWebSocketHandshakeStreamCreateHelper websocket_stream_create_helper;

  auto trans =
      std::make_unique<HttpNetworkTransaction>(DEFAULT_PRIORITY, session.get());
  trans->SetWebSocketHandshakeStreamCreateHelper(
      &websocket_stream_create_helper);

  {
    TestCompletionCallback callback;

    int rv = trans->Start(&request, callback.callback(), NetLogWithSource());
    EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

    rv = callback.WaitForResult();
    EXPECT_THAT(rv, IsOk());
  }

  const HttpResponseInfo* response = trans->GetResponseInfo();
  ASSERT_TRUE(response);
  ASSERT_TRUE(response->headers);
  EXPECT_EQ(407, response->headers->response_code());

  {
    TestCompletionCallback callback;

    int rv = trans->RestartWithAuth(AuthCredentials(kFoo, kBar),
                                    callback.callback());
    EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

    rv = callback.WaitForResult();
    EXPECT_THAT(rv, IsOk());
  }

  response = trans->GetResponseInfo();
  ASSERT_TRUE(response);
  ASSERT_TRUE(response->headers);

  EXPECT_EQ(101, response->headers->response_code());

  trans.reset();
  session->CloseAllConnections(ERR_FAILED, "Very good reason");
}

// Verify that proxy headers are not sent to the destination server when
// establishing a tunnel for an insecure WebSocket connection.
// This requires the authentication info to be injected into the auth cache
// due to crbug.com/395064
// TODO(ricea): Change to use a 407 response once issue 395064 is fixed.
TEST_F(HttpNetworkTransactionTest, ProxyHeadersNotSentOverWsTunnel) {
  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("ws://www.example.org/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);
  AddWebSocketHeaders(&request.extra_headers);

  // Configure against proxy server "myproxy:70".
  session_deps_.proxy_resolution_service =
      ConfiguredProxyResolutionService::CreateFixedFromPacResultForTest(
          "PROXY myproxy:70", TRAFFIC_ANNOTATION_FOR_TESTS);

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));

  MockWrite data_writes[] = {
      // Try to establish a tunnel for the WebSocket connection, with
      // credentials. Because WebSockets have a separate set of socket pools,
      // they cannot and will not use the same TCP/IP connection as the
      // preflight HTTP request.
      MockWrite("CONNECT www.example.org:80 HTTP/1.1\r\n"
                "Host: www.example.org:80\r\n"
                "Proxy-Connection: keep-alive\r\n"
                "Proxy-Authorization: Basic Zm9vOmJhcg==\r\n\r\n"),

      MockWrite("GET / HTTP/1.1\r\n"
                "Host: www.example.org\r\n"
                "Connection: Upgrade\r\n"
                "Upgrade: websocket\r\n"
                "Origin: http://www.example.org\r\n"
                "Sec-WebSocket-Version: 13\r\n"
                "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
                "Sec-WebSocket-Extensions: permessage-deflate; "
                "client_max_window_bits\r\n\r\n")};

  MockRead data_reads[] = {
      // HTTP CONNECT with credentials.
      MockRead("HTTP/1.1 200 Connection Established\r\n\r\n"),

      // WebSocket connection established inside tunnel.
      MockRead("HTTP/1.1 101 Switching Protocols\r\n"
               "Upgrade: websocket\r\n"
               "Connection: Upgrade\r\n"
               "Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n\r\n")};

  StaticSocketDataProvider data(data_reads, data_writes);
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  session->http_auth_cache()->Add(
      url::SchemeHostPort(GURL("http://myproxy:70/")), HttpAuth::AUTH_PROXY,
      "MyRealm1", HttpAuth::AUTH_SCHEME_BASIC, NetworkIsolationKey(),
      "Basic realm=MyRealm1", AuthCredentials(kFoo, kBar), "/");

  TestWebSocketHandshakeStreamCreateHelper websocket_stream_create_helper;

  auto trans =
      std::make_unique<HttpNetworkTransaction>(DEFAULT_PRIORITY, session.get());
  trans->SetWebSocketHandshakeStreamCreateHelper(
      &websocket_stream_create_helper);

  TestCompletionCallback callback;

  int rv = trans->Start(&request, callback.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback.WaitForResult();
  EXPECT_THAT(rv, IsOk());

  const HttpResponseInfo* response = trans->GetResponseInfo();
  ASSERT_TRUE(response);
  ASSERT_TRUE(response->headers);

  EXPECT_EQ(101, response->headers->response_code());

  trans.reset();
  session->CloseAllConnections(ERR_FAILED, "Very good reason");
}

// WebSockets over QUIC is not supported, including over QUIC proxies.
TEST_F(HttpNetworkTransactionTest, WebSocketNotSentOverQuicProxy) {
  for (bool secure : {true, false}) {
    SCOPED_TRACE(secure);
    session_deps_.proxy_resolution_service =
        ConfiguredProxyResolutionService::CreateFixedFromPacResultForTest(
            "QUIC myproxy.org:443", TRAFFIC_ANNOTATION_FOR_TESTS);
    session_deps_.enable_quic = true;

    HttpRequestInfo request;
    request.url =
        GURL(secure ? "ws://www.example.org/" : "wss://www.example.org/");
    AddWebSocketHeaders(&request.extra_headers);
    request.traffic_annotation =
        net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

    TestWebSocketHandshakeStreamCreateHelper
        websocket_handshake_stream_create_helper;

    std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));
    HttpNetworkTransaction trans(LOW, session.get());
    trans.SetWebSocketHandshakeStreamCreateHelper(
        &websocket_handshake_stream_create_helper);

    TestCompletionCallback callback;
    int rv = trans.Start(&request, callback.callback(), NetLogWithSource());
    EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

    rv = callback.WaitForResult();
    EXPECT_THAT(rv, IsError(ERR_NO_SUPPORTED_PROXIES));
  }
}

#endif  // BUILDFLAG(ENABLE_WEBSOCKETS)

TEST_F(HttpNetworkTransactionTest, TotalNetworkBytesPost) {
  std::vector<std::unique_ptr<UploadElementReader>> element_readers;
  element_readers.push_back(
      std::make_unique<UploadBytesElementReader>("foo", 3));
  ElementsUploadDataStream upload_data_stream(std::move(element_readers), 0);

  HttpRequestInfo request;
  request.method = "POST";
  request.url = GURL("http://www.foo.com/");
  request.upload_data_stream = &upload_data_stream;
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));
  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());
  MockWrite data_writes[] = {
      MockWrite("POST / HTTP/1.1\r\n"
                "Host: www.foo.com\r\n"
                "Connection: keep-alive\r\n"
                "Content-Length: 3\r\n\r\n"),
      MockWrite("foo"),
  };

  MockRead data_reads[] = {
      MockRead("HTTP/1.1 200 OK\r\n\r\n"), MockRead("hello world"),
      MockRead(SYNCHRONOUS, OK),
  };
  StaticSocketDataProvider data(data_reads, data_writes);
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  TestCompletionCallback callback;

  EXPECT_EQ(ERR_IO_PENDING,
            trans.Start(&request, callback.callback(), NetLogWithSource()));
  EXPECT_THAT(callback.WaitForResult(), IsOk());

  std::string response_data;
  EXPECT_THAT(ReadTransaction(&trans, &response_data), IsOk());

  EXPECT_EQ(CountWriteBytes(data_writes), trans.GetTotalSentBytes());
  EXPECT_EQ(CountReadBytes(data_reads), trans.GetTotalReceivedBytes());
}

TEST_F(HttpNetworkTransactionTest, TotalNetworkBytesPost100Continue) {
  std::vector<std::unique_ptr<UploadElementReader>> element_readers;
  element_readers.push_back(
      std::make_unique<UploadBytesElementReader>("foo", 3));
  ElementsUploadDataStream upload_data_stream(std::move(element_readers), 0);

  HttpRequestInfo request;
  request.method = "POST";
  request.url = GURL("http://www.foo.com/");
  request.upload_data_stream = &upload_data_stream;
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));
  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());
  MockWrite data_writes[] = {
      MockWrite("POST / HTTP/1.1\r\n"
                "Host: www.foo.com\r\n"
                "Connection: keep-alive\r\n"
                "Content-Length: 3\r\n\r\n"),
      MockWrite("foo"),
  };

  MockRead data_reads[] = {
      MockRead("HTTP/1.1 100 Continue\r\n\r\n"),
      MockRead("HTTP/1.1 200 OK\r\n\r\n"), MockRead("hello world"),
      MockRead(SYNCHRONOUS, OK),
  };
  StaticSocketDataProvider data(data_reads, data_writes);
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  TestCompletionCallback callback;

  EXPECT_EQ(ERR_IO_PENDING,
            trans.Start(&request, callback.callback(), NetLogWithSource()));
  EXPECT_THAT(callback.WaitForResult(), IsOk());

  std::string response_data;
  EXPECT_THAT(ReadTransaction(&trans, &response_data), IsOk());

  EXPECT_EQ(CountWriteBytes(data_writes), trans.GetTotalSentBytes());
  EXPECT_EQ(CountReadBytes(data_reads), trans.GetTotalReceivedBytes());
}

TEST_F(HttpNetworkTransactionTest, TotalNetworkBytesChunkedPost) {
  ChunkedUploadDataStream upload_data_stream(0);

  HttpRequestInfo request;
  request.method = "POST";
  request.url = GURL("http://www.foo.com/");
  request.upload_data_stream = &upload_data_stream;
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));
  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());
  // Send headers successfully, but get an error while sending the body.
  MockWrite data_writes[] = {
      MockWrite("POST / HTTP/1.1\r\n"
                "Host: www.foo.com\r\n"
                "Connection: keep-alive\r\n"
                "Transfer-Encoding: chunked\r\n\r\n"),
      MockWrite("1\r\nf\r\n"), MockWrite("2\r\noo\r\n"), MockWrite("0\r\n\r\n"),
  };

  MockRead data_reads[] = {
      MockRead("HTTP/1.1 200 OK\r\n\r\n"), MockRead("hello world"),
      MockRead(SYNCHRONOUS, OK),
  };
  StaticSocketDataProvider data(data_reads, data_writes);
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  TestCompletionCallback callback;

  EXPECT_EQ(ERR_IO_PENDING,
            trans.Start(&request, callback.callback(), NetLogWithSource()));

  base::RunLoop().RunUntilIdle();
  upload_data_stream.AppendData("f", 1, false);

  base::RunLoop().RunUntilIdle();
  upload_data_stream.AppendData("oo", 2, true);

  EXPECT_THAT(callback.WaitForResult(), IsOk());

  std::string response_data;
  EXPECT_THAT(ReadTransaction(&trans, &response_data), IsOk());

  EXPECT_EQ(CountWriteBytes(data_writes), trans.GetTotalSentBytes());
  EXPECT_EQ(CountReadBytes(data_reads), trans.GetTotalReceivedBytes());
}

void CheckContentEncodingMatching(SpdySessionDependencies* session_deps,
                                  const std::string& accept_encoding,
                                  const std::string& content_encoding,
                                  const std::string& location,
                                  bool should_match) {
  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("http://www.foo.com/");
  request.extra_headers.SetHeader(HttpRequestHeaders::kAcceptEncoding,
                                  accept_encoding);
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  std::unique_ptr<HttpNetworkSession> session(CreateSession(session_deps));
  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());
  // Send headers successfully, but get an error while sending the body.
  MockWrite data_writes[] = {
      MockWrite("GET / HTTP/1.1\r\n"
                "Host: www.foo.com\r\n"
                "Connection: keep-alive\r\n"
                "Accept-Encoding: "),
      MockWrite(accept_encoding.data()), MockWrite("\r\n\r\n"),
  };

  std::string response_code = "200 OK";
  std::string extra;
  if (!location.empty()) {
    response_code = "301 Redirect\r\nLocation: ";
    response_code.append(location);
  }

  MockRead data_reads[] = {
      MockRead("HTTP/1.0 "),
      MockRead(response_code.data()),
      MockRead("\r\nContent-Encoding: "),
      MockRead(content_encoding.data()),
      MockRead("\r\n\r\n"),
      MockRead(SYNCHRONOUS, OK),
  };
  StaticSocketDataProvider data(data_reads, data_writes);
  session_deps->socket_factory->AddSocketDataProvider(&data);

  TestCompletionCallback callback;

  int rv = trans.Start(&request, callback.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback.WaitForResult();
  if (should_match) {
    EXPECT_THAT(rv, IsOk());
  } else {
    EXPECT_THAT(rv, IsError(ERR_CONTENT_DECODING_FAILED));
  }
}

TEST_F(HttpNetworkTransactionTest, MatchContentEncoding1) {
  CheckContentEncodingMatching(&session_deps_, "gzip,sdch", "br", "", false);
}

TEST_F(HttpNetworkTransactionTest, MatchContentEncoding2) {
  CheckContentEncodingMatching(&session_deps_, "identity;q=1, *;q=0", "", "",
                               true);
}

TEST_F(HttpNetworkTransactionTest, MatchContentEncoding3) {
  CheckContentEncodingMatching(&session_deps_, "identity;q=1, *;q=0", "gzip",
                               "", false);
}

TEST_F(HttpNetworkTransactionTest, MatchContentEncoding4) {
  CheckContentEncodingMatching(&session_deps_, "identity;q=1, *;q=0", "gzip",
                               "www.foo.com/other", true);
}

TEST_F(HttpNetworkTransactionTest, ProxyResolutionFailsSync) {
  ProxyConfig proxy_config;
  proxy_config.set_pac_url(GURL("http://fooproxyurl"));
  proxy_config.set_pac_mandatory(true);
  MockAsyncProxyResolver resolver;
  session_deps_.proxy_resolution_service =
      std::make_unique<ConfiguredProxyResolutionService>(

          std::make_unique<ProxyConfigServiceFixed>(ProxyConfigWithAnnotation(
              proxy_config, TRAFFIC_ANNOTATION_FOR_TESTS)),
          std::make_unique<FailingProxyResolverFactory>(), nullptr,
          /*quick_check_enabled=*/true);

  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("http://www.example.org/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));
  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

  TestCompletionCallback callback;

  int rv = trans.Start(&request, callback.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));
  EXPECT_THAT(callback.WaitForResult(),
              IsError(ERR_MANDATORY_PROXY_CONFIGURATION_FAILED));
}

TEST_F(HttpNetworkTransactionTest, ProxyResolutionFailsAsync) {
  ProxyConfig proxy_config;
  proxy_config.set_pac_url(GURL("http://fooproxyurl"));
  proxy_config.set_pac_mandatory(true);
  auto proxy_resolver_factory =
      std::make_unique<MockAsyncProxyResolverFactory>(false);
  auto* proxy_resolver_factory_ptr = proxy_resolver_factory.get();
  MockAsyncProxyResolver resolver;
  session_deps_.proxy_resolution_service =
      std::make_unique<ConfiguredProxyResolutionService>(

          std::make_unique<ProxyConfigServiceFixed>(ProxyConfigWithAnnotation(
              proxy_config, TRAFFIC_ANNOTATION_FOR_TESTS)),
          std::move(proxy_resolver_factory), nullptr,
          /*quick_check_enabled=*/true);
  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("http://www.example.org/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));
  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

  TestCompletionCallback callback;
  int rv = trans.Start(&request, callback.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  proxy_resolver_factory_ptr->pending_requests()[0]->CompleteNowWithForwarder(
      ERR_FAILED, &resolver);
  EXPECT_THAT(callback.WaitForResult(),
              IsError(ERR_MANDATORY_PROXY_CONFIGURATION_FAILED));
}

TEST_F(HttpNetworkTransactionTest, NoSupportedProxies) {
  session_deps_.proxy_resolution_service =
      ConfiguredProxyResolutionService::CreateFixedFromPacResultForTest(
          "QUIC myproxy.org:443", TRAFFIC_ANNOTATION_FOR_TESTS);
  session_deps_.enable_quic = false;
  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));

  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("http://www.example.org/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  TestCompletionCallback callback;
  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());
  int rv = trans.Start(&request, callback.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  EXPECT_THAT(callback.WaitForResult(), IsError(ERR_NO_SUPPORTED_PROXIES));
}

//-----------------------------------------------------------------------------
// Reporting tests

#if BUILDFLAG(ENABLE_REPORTING)
class HttpNetworkTransactionReportingTest
    : public HttpNetworkTransactionTest,
      public ::testing::WithParamInterface<bool> {
 protected:
  HttpNetworkTransactionReportingTest() {
    std::vector<base::Feature> required_features = {
        features::kPartitionNelAndReportingByNetworkIsolationKey};
    if (UseDocumentReporting()) {
      required_features.push_back(features::kDocumentReporting);
    }
    feature_list_.InitWithFeatures(required_features, {});
  }

  void SetUp() override {
    HttpNetworkTransactionTest::SetUp();
    auto test_reporting_context = std::make_unique<TestReportingContext>(
        &clock_, &tick_clock_, ReportingPolicy());
    test_reporting_context_ = test_reporting_context.get();
    session_deps_.reporting_service =
        ReportingService::CreateForTesting(std::move(test_reporting_context));
  }

  TestReportingContext* reporting_context() const {
    return test_reporting_context_;
  }

  void clear_reporting_service() {
    session_deps_.reporting_service.reset();
    test_reporting_context_ = nullptr;
  }

  // Makes an HTTPS request that should install a valid Reporting policy
  // using Report-To header.
  void RequestPolicy(CertStatus cert_status = 0) {
    HttpRequestInfo request;
    request.method = "GET";
    request.url = GURL(url_);
    request.traffic_annotation =
        net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);
    request.network_isolation_key = kNetworkIsolationKey;

    MockWrite data_writes[] = {
        MockWrite("GET / HTTP/1.1\r\n"
                  "Host: www.example.org\r\n"
                  "Connection: keep-alive\r\n\r\n"),
    };

    MockRead reporting_header;
    reporting_header = MockRead(
        "Report-To: {\"group\": \"nel\", \"max_age\": 86400, "
        "\"endpoints\": [{\"url\": "
        "\"https://www.example.org/upload/\"}]}\r\n");
    MockRead data_reads[] = {
        MockRead("HTTP/1.0 200 OK\r\n"),
        std::move(reporting_header),
        MockRead("\r\n"),
        MockRead("hello world"),
        MockRead(SYNCHRONOUS, OK),
    };

    StaticSocketDataProvider reads(data_reads, data_writes);
    session_deps_.socket_factory->AddSocketDataProvider(&reads);

    SSLSocketDataProvider ssl(ASYNC, OK);
    if (request.url.SchemeIsCryptographic()) {
      ssl.ssl_info.cert =
          ImportCertFromFile(GetTestCertsDirectory(), "wildcard.pem");
      ASSERT_TRUE(ssl.ssl_info.cert);
      ssl.ssl_info.cert_status = cert_status;
      session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl);
    }

    TestCompletionCallback callback;
    auto session = CreateSession(&session_deps_);
    HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());
    int rv = trans.Start(&request, callback.callback(), NetLogWithSource());
    EXPECT_THAT(callback.GetResult(rv), IsOk());
  }

 protected:
  bool UseDocumentReporting() const { return GetParam(); }
  std::string url_ = "https://www.example.org/";

 private:
  base::test::ScopedFeatureList feature_list_;
  raw_ptr<TestReportingContext> test_reporting_context_;
};

TEST_P(HttpNetworkTransactionReportingTest,
       DontProcessReportToHeaderNoService) {
  clear_reporting_service();
  RequestPolicy();
  // No crash.
}

TEST_P(HttpNetworkTransactionReportingTest, DontProcessReportToHeaderHttp) {
  url_ = "http://www.example.org/";
  RequestPolicy();
  EXPECT_EQ(0u, reporting_context()->cache()->GetEndpointCount());
}

TEST_P(HttpNetworkTransactionReportingTest, ProcessReportToHeaderHttps) {
  RequestPolicy();
  ASSERT_EQ(1u, reporting_context()->cache()->GetEndpointCount());
  const ReportingEndpoint endpoint =
      reporting_context()->cache()->GetEndpointForTesting(
          ReportingEndpointGroupKey(
              kNetworkIsolationKey,
              url::Origin::Create(GURL("https://www.example.org/")), "nel"),
          GURL("https://www.example.org/upload/"));
  EXPECT_TRUE(endpoint);
}

TEST_P(HttpNetworkTransactionReportingTest,
       DontProcessReportToHeaderInvalidHttps) {
  CertStatus cert_status = CERT_STATUS_COMMON_NAME_INVALID;
  RequestPolicy(cert_status);
  EXPECT_EQ(0u, reporting_context()->cache()->GetEndpointCount());
}

INSTANTIATE_TEST_SUITE_P(All,
                         HttpNetworkTransactionReportingTest,
                         ::testing::Bool());

#endif  // BUILDFLAG(ENABLE_REPORTING)

//-----------------------------------------------------------------------------
// Network Error Logging tests

#if BUILDFLAG(ENABLE_REPORTING)
namespace {

const char kUserAgent[] = "Mozilla/1.0";
const char kReferrer[] = "https://www.referrer.org/";

}  // namespace

class HttpNetworkTransactionNetworkErrorLoggingTest
    : public HttpNetworkTransactionTest {
 protected:
  void SetUp() override {
    HttpNetworkTransactionTest::SetUp();
    auto network_error_logging_service =
        std::make_unique<TestNetworkErrorLoggingService>();
    test_network_error_logging_service_ = network_error_logging_service.get();
    session_deps_.network_error_logging_service =
        std::move(network_error_logging_service);

    extra_headers_.SetHeader("User-Agent", kUserAgent);
    extra_headers_.SetHeader("Referer", kReferrer);

    request_.method = "GET";
    request_.url = GURL(url_);
    request_.network_isolation_key = kNetworkIsolationKey;
    request_.extra_headers = extra_headers_;
    request_.reporting_upload_depth = reporting_upload_depth_;
    request_.traffic_annotation =
        net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);
  }

  TestNetworkErrorLoggingService* network_error_logging_service() const {
    return test_network_error_logging_service_;
  }

  void clear_network_error_logging_service() {
    session_deps_.network_error_logging_service.reset();
    test_network_error_logging_service_ = nullptr;
  }

  // Makes an HTTPS request that should install a valid NEL policy.
  void RequestPolicy(CertStatus cert_status = 0) {
    std::string extra_header_string = extra_headers_.ToString();
    MockWrite data_writes[] = {
        MockWrite("GET / HTTP/1.1\r\n"
                  "Host: www.example.org\r\n"
                  "Connection: keep-alive\r\n"),
        MockWrite(ASYNC, extra_header_string.data(),
                  extra_header_string.size()),
    };
    MockRead data_reads[] = {
        MockRead("HTTP/1.0 200 OK\r\n"),
        MockRead("NEL: {\"report_to\": \"nel\", \"max_age\": 86400}\r\n"),
        MockRead("\r\n"),
        MockRead("hello world"),
        MockRead(SYNCHRONOUS, OK),
    };

    StaticSocketDataProvider reads(data_reads, data_writes);
    session_deps_.socket_factory->AddSocketDataProvider(&reads);

    SSLSocketDataProvider ssl(ASYNC, OK);
    if (request_.url.SchemeIsCryptographic()) {
      ssl.ssl_info.cert =
          ImportCertFromFile(GetTestCertsDirectory(), "wildcard.pem");
      ASSERT_TRUE(ssl.ssl_info.cert);
      ssl.ssl_info.cert_status = cert_status;
      session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl);
    }

    TestCompletionCallback callback;
    auto session = CreateSession(&session_deps_);
    HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());
    int rv = trans.Start(&request_, callback.callback(), NetLogWithSource());
    EXPECT_THAT(callback.GetResult(rv), IsOk());

    std::string response_data;
    ASSERT_THAT(ReadTransaction(&trans, &response_data), IsOk());
    EXPECT_EQ("hello world", response_data);
  }

  void CheckReport(size_t index,
                   int status_code,
                   int error_type,
                   IPAddress server_ip = IPAddress::IPv4Localhost()) {
    ASSERT_LT(index, network_error_logging_service()->errors().size());

    const NetworkErrorLoggingService::RequestDetails& error =
        network_error_logging_service()->errors()[index];
    EXPECT_EQ(url_, error.uri);
    EXPECT_EQ(kNetworkIsolationKey, error.network_isolation_key);
    EXPECT_EQ(kReferrer, error.referrer);
    EXPECT_EQ(kUserAgent, error.user_agent);
    EXPECT_EQ(server_ip, error.server_ip);
    EXPECT_EQ("http/1.1", error.protocol);
    EXPECT_EQ("GET", error.method);
    EXPECT_EQ(status_code, error.status_code);
    EXPECT_EQ(error_type, error.type);
    EXPECT_EQ(0, error.reporting_upload_depth);
  }

 protected:
  std::string url_ = "https://www.example.org/";
  CertStatus cert_status_ = 0;
  HttpRequestInfo request_;
  HttpRequestHeaders extra_headers_;
  int reporting_upload_depth_ = 0;

 private:
  raw_ptr<TestNetworkErrorLoggingService> test_network_error_logging_service_;
};

TEST_F(HttpNetworkTransactionNetworkErrorLoggingTest,
       DontProcessNelHeaderNoService) {
  clear_network_error_logging_service();
  RequestPolicy();
  // No crash.
}

TEST_F(HttpNetworkTransactionNetworkErrorLoggingTest,
       DontProcessNelHeaderHttp) {
  url_ = "http://www.example.org/";
  request_.url = GURL(url_);
  RequestPolicy();
  EXPECT_EQ(0u, network_error_logging_service()->headers().size());
}

// Don't set NEL policies received on a proxied connection.
TEST_F(HttpNetworkTransactionNetworkErrorLoggingTest,
       DontProcessNelHeaderProxy) {
  session_deps_.proxy_resolution_service =
      ConfiguredProxyResolutionService::CreateFixedFromPacResultForTest(
          "PROXY myproxy:70", TRAFFIC_ANNOTATION_FOR_TESTS);
  session_deps_.net_log = NetLog::Get();
  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));

  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("https://www.example.org/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  // Since we have proxy, should try to establish tunnel.
  MockWrite data_writes1[] = {
      MockWrite("CONNECT www.example.org:443 HTTP/1.1\r\n"
                "Host: www.example.org:443\r\n"
                "Proxy-Connection: keep-alive\r\n\r\n"),

      MockWrite("GET / HTTP/1.1\r\n"
                "Host: www.example.org\r\n"
                "Connection: keep-alive\r\n\r\n"),
  };

  MockRead data_reads1[] = {
      MockRead("HTTP/1.1 200 Connection Established\r\n\r\n"),

      MockRead("HTTP/1.1 200 OK\r\n"),
      MockRead("NEL: {\"report_to\": \"nel\", \"max_age\": 86400}\r\n"),
      MockRead("Content-Type: text/html; charset=iso-8859-1\r\n"),
      MockRead("Content-Length: 100\r\n\r\n"),
      MockRead(SYNCHRONOUS, OK),
  };

  StaticSocketDataProvider data1(data_reads1, data_writes1);
  session_deps_.socket_factory->AddSocketDataProvider(&data1);
  SSLSocketDataProvider ssl(ASYNC, OK);
  ssl.ssl_info.cert =
      ImportCertFromFile(GetTestCertsDirectory(), "wildcard.pem");
  ASSERT_TRUE(ssl.ssl_info.cert);
  ssl.ssl_info.cert_status = 0;
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl);

  TestCompletionCallback callback1;
  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

  int rv = trans.Start(&request, callback1.callback(),
                       NetLogWithSource::Make(NetLogSourceType::NONE));
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback1.WaitForResult();
  EXPECT_THAT(rv, IsOk());

  const HttpResponseInfo* response = trans.GetResponseInfo();
  ASSERT_TRUE(response);
  EXPECT_EQ(200, response->headers->response_code());
  EXPECT_TRUE(response->was_fetched_via_proxy);

  // No NEL header was set.
  EXPECT_EQ(0u, network_error_logging_service()->headers().size());
}

TEST_F(HttpNetworkTransactionNetworkErrorLoggingTest, ProcessNelHeaderHttps) {
  RequestPolicy();
  ASSERT_EQ(1u, network_error_logging_service()->headers().size());
  const auto& header = network_error_logging_service()->headers()[0];
  EXPECT_EQ(kNetworkIsolationKey, header.network_isolation_key);
  EXPECT_EQ(url::Origin::Create(GURL("https://www.example.org/")),
            header.origin);
  EXPECT_EQ(IPAddress::IPv4Localhost(), header.received_ip_address);
  EXPECT_EQ("{\"report_to\": \"nel\", \"max_age\": 86400}", header.value);
}

TEST_F(HttpNetworkTransactionNetworkErrorLoggingTest,
       DontProcessNelHeaderInvalidHttps) {
  CertStatus cert_status = CERT_STATUS_COMMON_NAME_INVALID;
  RequestPolicy(cert_status);
  EXPECT_EQ(0u, network_error_logging_service()->headers().size());
}

TEST_F(HttpNetworkTransactionNetworkErrorLoggingTest, CreateReportSuccess) {
  RequestPolicy();
  ASSERT_EQ(1u, network_error_logging_service()->errors().size());
  CheckReport(0 /* index */, 200 /* status_code */, OK);
}

TEST_F(HttpNetworkTransactionNetworkErrorLoggingTest,
       CreateReportDNSErrorAfterStartSync) {
  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));
  auto trans =
      std::make_unique<HttpNetworkTransaction>(DEFAULT_PRIORITY, session.get());

  session_deps_.host_resolver->set_synchronous_mode(true);
  session_deps_.host_resolver->rules()->AddRule(GURL(url_).host(),
                                                ERR_NAME_NOT_RESOLVED);
  TestCompletionCallback callback;

  int rv = trans->Start(&request_, callback.callback(), NetLogWithSource());
  EXPECT_THAT(callback.GetResult(rv), IsError(ERR_NAME_NOT_RESOLVED));

  trans.reset();

  ASSERT_EQ(1u, network_error_logging_service()->errors().size());
  CheckReport(0 /* index */, 0 /* status_code */, ERR_NAME_NOT_RESOLVED,
              IPAddress() /* server_ip */);
}

TEST_F(HttpNetworkTransactionNetworkErrorLoggingTest,
       CreateReportDNSErrorAfterStartAsync) {
  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));
  auto trans =
      std::make_unique<HttpNetworkTransaction>(DEFAULT_PRIORITY, session.get());

  session_deps_.host_resolver->set_synchronous_mode(false);
  session_deps_.host_resolver->rules()->AddRule(GURL(url_).host(),
                                                ERR_NAME_NOT_RESOLVED);
  TestCompletionCallback callback;

  int rv = trans->Start(&request_, callback.callback(), NetLogWithSource());
  EXPECT_THAT(callback.GetResult(rv), IsError(ERR_NAME_NOT_RESOLVED));

  trans.reset();

  ASSERT_EQ(1u, network_error_logging_service()->errors().size());
  CheckReport(0 /* index */, 0 /* status_code */, ERR_NAME_NOT_RESOLVED,
              IPAddress() /* server_ip */);
}

TEST_F(HttpNetworkTransactionNetworkErrorLoggingTest,
       CreateReportErrorAfterStart) {
  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));
  auto trans =
      std::make_unique<HttpNetworkTransaction>(DEFAULT_PRIORITY, session.get());

  MockConnect mock_connect(SYNCHRONOUS, ERR_CONNECTION_REFUSED);
  StaticSocketDataProvider data;
  data.set_connect_data(mock_connect);
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  TestCompletionCallback callback;

  int rv = trans->Start(&request_, callback.callback(), NetLogWithSource());
  EXPECT_THAT(callback.GetResult(rv), IsError(ERR_CONNECTION_REFUSED));

  trans.reset();

  ASSERT_EQ(1u, network_error_logging_service()->errors().size());
  CheckReport(0 /* index */, 0 /* status_code */, ERR_CONNECTION_REFUSED,
              IPAddress::IPv4Localhost() /* server_ip */);
}

// Same as above except the error is ASYNC
TEST_F(HttpNetworkTransactionNetworkErrorLoggingTest,
       CreateReportErrorAfterStartAsync) {
  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));
  auto trans =
      std::make_unique<HttpNetworkTransaction>(DEFAULT_PRIORITY, session.get());

  MockConnect mock_connect(ASYNC, ERR_CONNECTION_REFUSED);
  StaticSocketDataProvider data;
  data.set_connect_data(mock_connect);
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  TestCompletionCallback callback;

  int rv = trans->Start(&request_, callback.callback(), NetLogWithSource());
  EXPECT_THAT(callback.GetResult(rv), IsError(ERR_CONNECTION_REFUSED));

  trans.reset();

  ASSERT_EQ(1u, network_error_logging_service()->errors().size());
  CheckReport(0 /* index */, 0 /* status_code */, ERR_CONNECTION_REFUSED,
              IPAddress::IPv4Localhost() /* server_ip */);
}

TEST_F(HttpNetworkTransactionNetworkErrorLoggingTest,
       CreateReportReadBodyError) {
  std::string extra_header_string = extra_headers_.ToString();
  MockWrite data_writes[] = {
      MockWrite("GET / HTTP/1.1\r\n"
                "Host: www.example.org\r\n"
                "Connection: keep-alive\r\n"),
      MockWrite(ASYNC, extra_header_string.data(), extra_header_string.size()),
  };
  MockRead data_reads[] = {
      MockRead("HTTP/1.0 200 OK\r\n"),
      MockRead("Content-Length: 100\r\n\r\n"),  // wrong content length
      MockRead("hello world"),
      MockRead(SYNCHRONOUS, OK),
  };

  StaticSocketDataProvider reads(data_reads, data_writes);
  session_deps_.socket_factory->AddSocketDataProvider(&reads);

  SSLSocketDataProvider ssl(ASYNC, OK);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl);

  // Log start time
  base::TimeTicks start_time = base::TimeTicks::Now();

  TestCompletionCallback callback;
  auto session = CreateSession(&session_deps_);
  auto trans =
      std::make_unique<HttpNetworkTransaction>(DEFAULT_PRIORITY, session.get());
  int rv = trans->Start(&request_, callback.callback(), NetLogWithSource());
  EXPECT_THAT(callback.GetResult(rv), IsOk());

  const HttpResponseInfo* response = trans->GetResponseInfo();
  ASSERT_TRUE(response);

  EXPECT_TRUE(response->headers);
  EXPECT_EQ("HTTP/1.0 200 OK", response->headers->GetStatusLine());

  std::string response_data;
  rv = ReadTransaction(trans.get(), &response_data);
  EXPECT_THAT(rv, IsError(ERR_CONTENT_LENGTH_MISMATCH));

  trans.reset();

  ASSERT_EQ(1u, network_error_logging_service()->errors().size());

  CheckReport(0 /* index */, 200 /* status_code */,
              ERR_CONTENT_LENGTH_MISMATCH);
  const NetworkErrorLoggingService::RequestDetails& error =
      network_error_logging_service()->errors()[0];
  EXPECT_LE(error.elapsed_time, base::TimeTicks::Now() - start_time);
}

// Same as above except the final read is ASYNC.
TEST_F(HttpNetworkTransactionNetworkErrorLoggingTest,
       CreateReportReadBodyErrorAsync) {
  std::string extra_header_string = extra_headers_.ToString();
  MockWrite data_writes[] = {
      MockWrite("GET / HTTP/1.1\r\n"
                "Host: www.example.org\r\n"
                "Connection: keep-alive\r\n"),
      MockWrite(ASYNC, extra_header_string.data(), extra_header_string.size()),
  };
  MockRead data_reads[] = {
      MockRead("HTTP/1.0 200 OK\r\n"),
      MockRead("Content-Length: 100\r\n\r\n"),  // wrong content length
      MockRead("hello world"),
      MockRead(ASYNC, OK),
  };

  StaticSocketDataProvider reads(data_reads, data_writes);
  session_deps_.socket_factory->AddSocketDataProvider(&reads);

  SSLSocketDataProvider ssl(ASYNC, OK);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl);

  // Log start time
  base::TimeTicks start_time = base::TimeTicks::Now();

  TestCompletionCallback callback;
  auto session = CreateSession(&session_deps_);
  auto trans =
      std::make_unique<HttpNetworkTransaction>(DEFAULT_PRIORITY, session.get());
  int rv = trans->Start(&request_, callback.callback(), NetLogWithSource());
  EXPECT_THAT(callback.GetResult(rv), IsOk());

  const HttpResponseInfo* response = trans->GetResponseInfo();
  ASSERT_TRUE(response);

  EXPECT_TRUE(response->headers);
  EXPECT_EQ("HTTP/1.0 200 OK", response->headers->GetStatusLine());

  std::string response_data;
  rv = ReadTransaction(trans.get(), &response_data);
  EXPECT_THAT(rv, IsError(ERR_CONTENT_LENGTH_MISMATCH));

  trans.reset();

  ASSERT_EQ(1u, network_error_logging_service()->errors().size());

  CheckReport(0 /* index */, 200 /* status_code */,
              ERR_CONTENT_LENGTH_MISMATCH);
  const NetworkErrorLoggingService::RequestDetails& error =
      network_error_logging_service()->errors()[0];
  EXPECT_LE(error.elapsed_time, base::TimeTicks::Now() - start_time);
}

TEST_F(HttpNetworkTransactionNetworkErrorLoggingTest,
       CreateReportRestartWithAuth) {
  std::string extra_header_string = extra_headers_.ToString();
  static const base::TimeDelta kSleepDuration = base::Milliseconds(10);

  MockWrite data_writes1[] = {
      MockWrite("GET / HTTP/1.1\r\n"
                "Host: www.example.org\r\n"
                "Connection: keep-alive\r\n"),
      MockWrite(ASYNC, extra_header_string.data(), extra_header_string.size()),
  };

  MockRead data_reads1[] = {
      MockRead("HTTP/1.0 401 Unauthorized\r\n"),
      // Give a couple authenticate options (only the middle one is actually
      // supported).
      MockRead("WWW-Authenticate: Basic invalid\r\n"),  // Malformed.
      MockRead("WWW-Authenticate: Basic realm=\"MyRealm1\"\r\n"),
      MockRead("WWW-Authenticate: UNSUPPORTED realm=\"FOO\"\r\n"),
      MockRead("Content-Type: text/html; charset=iso-8859-1\r\n"),
      // Large content-length -- won't matter, as connection will be reset.
      MockRead("Content-Length: 10000\r\n\r\n"),
      MockRead(SYNCHRONOUS, ERR_FAILED),
  };

  // After calling trans->RestartWithAuth(), this is the request we should
  // be issuing -- the final header line contains the credentials.
  MockWrite data_writes2[] = {
      MockWrite("GET / HTTP/1.1\r\n"
                "Host: www.example.org\r\n"
                "Connection: keep-alive\r\n"
                "Authorization: Basic Zm9vOmJhcg==\r\n"),
      MockWrite(ASYNC, extra_header_string.data(), extra_header_string.size()),
  };

  // Lastly, the server responds with the actual content.
  MockRead data_reads2[] = {
      MockRead("HTTP/1.0 200 OK\r\n\r\n"),
      MockRead("hello world"),
      MockRead(SYNCHRONOUS, OK),
  };

  StaticSocketDataProvider data1(data_reads1, data_writes1);
  StaticSocketDataProvider data2(data_reads2, data_writes2);
  session_deps_.socket_factory->AddSocketDataProvider(&data1);
  session_deps_.socket_factory->AddSocketDataProvider(&data2);

  SSLSocketDataProvider ssl1(ASYNC, OK);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl1);
  SSLSocketDataProvider ssl2(ASYNC, OK);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl2);

  base::TimeTicks start_time = base::TimeTicks::Now();
  base::TimeTicks restart_time;

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));
  auto trans =
      std::make_unique<HttpNetworkTransaction>(DEFAULT_PRIORITY, session.get());

  TestCompletionCallback callback1;

  int rv = trans->Start(&request_, callback1.callback(), NetLogWithSource());
  EXPECT_THAT(callback1.GetResult(rv), IsOk());

  ASSERT_EQ(1u, network_error_logging_service()->errors().size());

  TestCompletionCallback callback2;

  // Wait 10 ms then restart with auth
  FastForwardBy(kSleepDuration);
  restart_time = base::TimeTicks::Now();
  rv =
      trans->RestartWithAuth(AuthCredentials(kFoo, kBar), callback2.callback());
  EXPECT_THAT(callback2.GetResult(rv), IsOk());

  std::string response_data;
  ASSERT_THAT(ReadTransaction(trans.get(), &response_data), IsOk());
  EXPECT_EQ("hello world", response_data);

  trans.reset();

  // One 401 report for the auth challenge, then a 200 report for the successful
  // retry. Note that we don't report the error draining the body, as the first
  // request already generated a report for the auth challenge.
  ASSERT_EQ(2u, network_error_logging_service()->errors().size());

  // Check error report contents
  CheckReport(0 /* index */, 401 /* status_code */, OK);
  CheckReport(1 /* index */, 200 /* status_code */, OK);

  const NetworkErrorLoggingService::RequestDetails& error1 =
      network_error_logging_service()->errors()[0];
  const NetworkErrorLoggingService::RequestDetails& error2 =
      network_error_logging_service()->errors()[1];

  // Sanity-check elapsed time values
  EXPECT_EQ(error1.elapsed_time, restart_time - start_time - kSleepDuration);
  // Check that the start time is refreshed when restarting with auth.
  EXPECT_EQ(error2.elapsed_time, base::TimeTicks::Now() - restart_time);
}

// Same as above, except draining the body before restarting fails
// asynchronously.
TEST_F(HttpNetworkTransactionNetworkErrorLoggingTest,
       CreateReportRestartWithAuthAsync) {
  std::string extra_header_string = extra_headers_.ToString();
  static const base::TimeDelta kSleepDuration = base::Milliseconds(10);

  MockWrite data_writes1[] = {
      MockWrite("GET / HTTP/1.1\r\n"
                "Host: www.example.org\r\n"
                "Connection: keep-alive\r\n"),
      MockWrite(ASYNC, extra_header_string.data(), extra_header_string.size()),
  };

  MockRead data_reads1[] = {
      MockRead("HTTP/1.0 401 Unauthorized\r\n"),
      // Give a couple authenticate options (only the middle one is actually
      // supported).
      MockRead("WWW-Authenticate: Basic invalid\r\n"),  // Malformed.
      MockRead("WWW-Authenticate: Basic realm=\"MyRealm1\"\r\n"),
      MockRead("WWW-Authenticate: UNSUPPORTED realm=\"FOO\"\r\n"),
      MockRead("Content-Type: text/html; charset=iso-8859-1\r\n"),
      // Large content-length -- won't matter, as connection will be reset.
      MockRead("Content-Length: 10000\r\n\r\n"),
      MockRead(ASYNC, ERR_FAILED),
  };

  // After calling trans->RestartWithAuth(), this is the request we should
  // be issuing -- the final header line contains the credentials.
  MockWrite data_writes2[] = {
      MockWrite("GET / HTTP/1.1\r\n"
                "Host: www.example.org\r\n"
                "Connection: keep-alive\r\n"
                "Authorization: Basic Zm9vOmJhcg==\r\n"),
      MockWrite(ASYNC, extra_header_string.data(), extra_header_string.size()),
  };

  // Lastly, the server responds with the actual content.
  MockRead data_reads2[] = {
      MockRead("HTTP/1.0 200 OK\r\n\r\n"),
      MockRead("hello world"),
      MockRead(SYNCHRONOUS, OK),
  };

  StaticSocketDataProvider data1(data_reads1, data_writes1);
  StaticSocketDataProvider data2(data_reads2, data_writes2);
  session_deps_.socket_factory->AddSocketDataProvider(&data1);
  session_deps_.socket_factory->AddSocketDataProvider(&data2);

  SSLSocketDataProvider ssl1(ASYNC, OK);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl1);
  SSLSocketDataProvider ssl2(ASYNC, OK);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl2);

  base::TimeTicks start_time = base::TimeTicks::Now();
  base::TimeTicks restart_time;

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));
  auto trans =
      std::make_unique<HttpNetworkTransaction>(DEFAULT_PRIORITY, session.get());

  TestCompletionCallback callback1;

  int rv = trans->Start(&request_, callback1.callback(), NetLogWithSource());
  EXPECT_THAT(callback1.GetResult(rv), IsOk());

  ASSERT_EQ(1u, network_error_logging_service()->errors().size());

  TestCompletionCallback callback2;

  // Wait 10 ms then restart with auth
  FastForwardBy(kSleepDuration);
  restart_time = base::TimeTicks::Now();
  rv =
      trans->RestartWithAuth(AuthCredentials(kFoo, kBar), callback2.callback());
  EXPECT_THAT(callback2.GetResult(rv), IsOk());

  std::string response_data;
  ASSERT_THAT(ReadTransaction(trans.get(), &response_data), IsOk());
  EXPECT_EQ("hello world", response_data);

  trans.reset();

  // One 401 report for the auth challenge, then a 200 report for the successful
  // retry. Note that we don't report the error draining the body, as the first
  // request already generated a report for the auth challenge.
  ASSERT_EQ(2u, network_error_logging_service()->errors().size());

  // Check error report contents
  CheckReport(0 /* index */, 401 /* status_code */, OK);
  CheckReport(1 /* index */, 200 /* status_code */, OK);

  const NetworkErrorLoggingService::RequestDetails& error1 =
      network_error_logging_service()->errors()[0];
  const NetworkErrorLoggingService::RequestDetails& error2 =
      network_error_logging_service()->errors()[1];

  // Sanity-check elapsed time values
  EXPECT_EQ(error1.elapsed_time, restart_time - start_time - kSleepDuration);
  // Check that the start time is refreshed when restarting with auth.
  EXPECT_EQ(error2.elapsed_time, base::TimeTicks::Now() - restart_time);
}

TEST_F(HttpNetworkTransactionNetworkErrorLoggingTest,
       CreateReportRetryKeepAliveConnectionReset) {
  std::string extra_header_string = extra_headers_.ToString();
  MockWrite data_writes1[] = {
      MockWrite("GET / HTTP/1.1\r\n"
                "Host: www.example.org\r\n"
                "Connection: keep-alive\r\n"),
      MockWrite(ASYNC, extra_header_string.data(), extra_header_string.size()),
      MockWrite("GET / HTTP/1.1\r\n"
                "Host: www.example.org\r\n"
                "Connection: keep-alive\r\n"),
      MockWrite(ASYNC, extra_header_string.data(), extra_header_string.size()),
  };

  MockRead data_reads1[] = {
      MockRead("HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\n"),
      MockRead("hello"),
      // Connection is reset
      MockRead(ASYNC, ERR_CONNECTION_RESET),
  };

  // Successful retry
  MockRead data_reads2[] = {
      MockRead("HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\n"),
      MockRead("world"),
      MockRead(ASYNC, OK),
  };

  StaticSocketDataProvider data1(data_reads1, data_writes1);
  StaticSocketDataProvider data2(data_reads2, base::span<MockWrite>());
  session_deps_.socket_factory->AddSocketDataProvider(&data1);
  session_deps_.socket_factory->AddSocketDataProvider(&data2);

  SSLSocketDataProvider ssl1(ASYNC, OK);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl1);
  SSLSocketDataProvider ssl2(ASYNC, OK);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl2);

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));
  auto trans1 =
      std::make_unique<HttpNetworkTransaction>(DEFAULT_PRIORITY, session.get());

  TestCompletionCallback callback1;

  int rv = trans1->Start(&request_, callback1.callback(), NetLogWithSource());
  EXPECT_THAT(callback1.GetResult(rv), IsOk());

  std::string response_data;
  ASSERT_THAT(ReadTransaction(trans1.get(), &response_data), IsOk());
  EXPECT_EQ("hello", response_data);

  ASSERT_EQ(1u, network_error_logging_service()->errors().size());

  auto trans2 =
      std::make_unique<HttpNetworkTransaction>(DEFAULT_PRIORITY, session.get());

  TestCompletionCallback callback2;

  rv = trans2->Start(&request_, callback2.callback(), NetLogWithSource());
  EXPECT_THAT(callback2.GetResult(rv), IsOk());

  ASSERT_THAT(ReadTransaction(trans2.get(), &response_data), IsOk());
  EXPECT_EQ("world", response_data);

  trans1.reset();
  trans2.reset();

  // One OK report from first request, then a ERR_CONNECTION_RESET report from
  // the second request, then an OK report from the successful retry.
  ASSERT_EQ(3u, network_error_logging_service()->errors().size());

  // Check error report contents
  CheckReport(0 /* index */, 200 /* status_code */, OK);
  CheckReport(1 /* index */, 0 /* status_code */, ERR_CONNECTION_RESET);
  CheckReport(2 /* index */, 200 /* status_code */, OK);
}

TEST_F(HttpNetworkTransactionNetworkErrorLoggingTest,
       CreateReportRetryKeepAlive408) {
  std::string extra_header_string = extra_headers_.ToString();
  MockWrite data_writes1[] = {
      MockWrite("GET / HTTP/1.1\r\n"
                "Host: www.example.org\r\n"
                "Connection: keep-alive\r\n"),
      MockWrite(ASYNC, extra_header_string.data(), extra_header_string.size()),
      MockWrite("GET / HTTP/1.1\r\n"
                "Host: www.example.org\r\n"
                "Connection: keep-alive\r\n"),
      MockWrite(ASYNC, extra_header_string.data(), extra_header_string.size()),
  };

  MockRead data_reads1[] = {
      MockRead("HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\n"),
      MockRead("hello"),
      // 408 Request Timeout
      MockRead(SYNCHRONOUS,
               "HTTP/1.1 408 Request Timeout\r\n"
               "Connection: Keep-Alive\r\n"
               "Content-Length: 6\r\n\r\n"
               "Pickle"),
  };

  // Successful retry
  MockRead data_reads2[] = {
      MockRead("HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\n"),
      MockRead("world"),
      MockRead(ASYNC, OK),
  };

  StaticSocketDataProvider data1(data_reads1, data_writes1);
  StaticSocketDataProvider data2(data_reads2, base::span<MockWrite>());
  session_deps_.socket_factory->AddSocketDataProvider(&data1);
  session_deps_.socket_factory->AddSocketDataProvider(&data2);

  SSLSocketDataProvider ssl1(ASYNC, OK);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl1);
  SSLSocketDataProvider ssl2(ASYNC, OK);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl2);

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));
  auto trans1 =
      std::make_unique<HttpNetworkTransaction>(DEFAULT_PRIORITY, session.get());

  TestCompletionCallback callback1;

  int rv = trans1->Start(&request_, callback1.callback(), NetLogWithSource());
  EXPECT_THAT(callback1.GetResult(rv), IsOk());

  std::string response_data;
  ASSERT_THAT(ReadTransaction(trans1.get(), &response_data), IsOk());
  EXPECT_EQ("hello", response_data);

  ASSERT_EQ(1u, network_error_logging_service()->errors().size());

  auto trans2 =
      std::make_unique<HttpNetworkTransaction>(DEFAULT_PRIORITY, session.get());

  TestCompletionCallback callback2;

  rv = trans2->Start(&request_, callback2.callback(), NetLogWithSource());
  EXPECT_THAT(callback2.GetResult(rv), IsOk());

  ASSERT_THAT(ReadTransaction(trans2.get(), &response_data), IsOk());
  EXPECT_EQ("world", response_data);

  trans1.reset();
  trans2.reset();

  // One 200 report from first request, then a 408 report from
  // the second request, then a 200 report from the successful retry.
  ASSERT_EQ(3u, network_error_logging_service()->errors().size());

  // Check error report contents
  CheckReport(0 /* index */, 200 /* status_code */, OK);
  CheckReport(1 /* index */, 408 /* status_code */, OK);
  CheckReport(2 /* index */, 200 /* status_code */, OK);
}

TEST_F(HttpNetworkTransactionNetworkErrorLoggingTest,
       CreateReportRetry421WithoutConnectionPooling) {
  // Two hosts resolve to the same IP address.
  const std::string ip_addr = "1.2.3.4";
  IPAddress ip;
  ASSERT_TRUE(ip.AssignFromIPLiteral(ip_addr));
  IPEndPoint peer_addr = IPEndPoint(ip, 443);

  session_deps_.host_resolver = std::make_unique<MockCachingHostResolver>();
  session_deps_.host_resolver->rules()->AddRule("www.example.org", ip_addr);
  session_deps_.host_resolver->rules()->AddRule("mail.example.org", ip_addr);

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));

  // Two requests on the first connection.
  spdy::SpdySerializedFrame req1(
      spdy_util_.ConstructSpdyGet("https://www.example.org", 1, LOWEST));
  spdy_util_.UpdateWithStreamDestruction(1);
  spdy::SpdySerializedFrame req2(
      spdy_util_.ConstructSpdyGet("https://mail.example.org", 3, LOWEST));
  spdy::SpdySerializedFrame rst(
      spdy_util_.ConstructSpdyRstStream(3, spdy::ERROR_CODE_CANCEL));
  MockWrite writes1[] = {
      CreateMockWrite(req1, 0),
      CreateMockWrite(req2, 3),
      CreateMockWrite(rst, 6),
  };

  // The first one succeeds, the second gets error 421 Misdirected Request.
  spdy::SpdySerializedFrame resp1(
      spdy_util_.ConstructSpdyGetReply(nullptr, 0, 1));
  spdy::SpdySerializedFrame body1(spdy_util_.ConstructSpdyDataFrame(1, true));
  spdy::Http2HeaderBlock response_headers;
  response_headers[spdy::kHttp2StatusHeader] = "421";
  spdy::SpdySerializedFrame resp2(
      spdy_util_.ConstructSpdyReply(3, std::move(response_headers)));
  MockRead reads1[] = {CreateMockRead(resp1, 1), CreateMockRead(body1, 2),
                       CreateMockRead(resp2, 4), MockRead(ASYNC, 0, 5)};

  MockConnect connect1(ASYNC, OK, peer_addr);
  SequencedSocketData data1(connect1, reads1, writes1);
  session_deps_.socket_factory->AddSocketDataProvider(&data1);

  AddSSLSocketData();

  // Retry the second request on a second connection.
  SpdyTestUtil spdy_util2;
  spdy::SpdySerializedFrame req3(
      spdy_util2.ConstructSpdyGet("https://mail.example.org", 1, LOWEST));
  MockWrite writes2[] = {
      CreateMockWrite(req3, 0),
  };

  spdy::SpdySerializedFrame resp3(
      spdy_util2.ConstructSpdyGetReply(nullptr, 0, 1));
  spdy::SpdySerializedFrame body3(spdy_util2.ConstructSpdyDataFrame(1, true));
  MockRead reads2[] = {CreateMockRead(resp3, 1), CreateMockRead(body3, 2),
                       MockRead(ASYNC, 0, 3)};

  MockConnect connect2(ASYNC, OK, peer_addr);
  SequencedSocketData data2(connect2, reads2, writes2);
  session_deps_.socket_factory->AddSocketDataProvider(&data2);

  AddSSLSocketData();

  // Preload mail.example.org into HostCache.
  int rv = session_deps_.host_resolver->LoadIntoCache(
      HostPortPair("mail.example.org", 443), NetworkIsolationKey(),
      absl::nullopt);
  EXPECT_THAT(rv, IsOk());

  HttpRequestInfo request1;
  request1.method = "GET";
  request1.url = GURL("https://www.example.org/");
  request1.load_flags = 0;
  request1.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);
  auto trans1 =
      std::make_unique<HttpNetworkTransaction>(DEFAULT_PRIORITY, session.get());

  TestCompletionCallback callback;
  rv = trans1->Start(&request1, callback.callback(), NetLogWithSource());
  EXPECT_THAT(callback.GetResult(rv), IsOk());

  const HttpResponseInfo* response = trans1->GetResponseInfo();
  ASSERT_TRUE(response);
  ASSERT_TRUE(response->headers);
  EXPECT_EQ("HTTP/1.1 200", response->headers->GetStatusLine());
  EXPECT_TRUE(response->was_fetched_via_spdy);
  EXPECT_TRUE(response->was_alpn_negotiated);
  std::string response_data;
  ASSERT_THAT(ReadTransaction(trans1.get(), &response_data), IsOk());
  EXPECT_EQ("hello!", response_data);

  trans1.reset();

  ASSERT_EQ(1u, network_error_logging_service()->errors().size());

  HttpRequestInfo request2;
  request2.method = "GET";
  request2.url = GURL("https://mail.example.org/");
  request2.load_flags = 0;
  request2.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);
  auto trans2 =
      std::make_unique<HttpNetworkTransaction>(DEFAULT_PRIORITY, session.get());

  rv = trans2->Start(&request2, callback.callback(),
                     NetLogWithSource::Make(NetLogSourceType::NONE));
  EXPECT_THAT(callback.GetResult(rv), IsOk());

  response = trans2->GetResponseInfo();
  ASSERT_TRUE(response);
  ASSERT_TRUE(response->headers);
  EXPECT_EQ("HTTP/1.1 200", response->headers->GetStatusLine());
  EXPECT_TRUE(response->was_fetched_via_spdy);
  EXPECT_TRUE(response->was_alpn_negotiated);
  ASSERT_THAT(ReadTransaction(trans2.get(), &response_data), IsOk());
  EXPECT_EQ("hello!", response_data);

  trans2.reset();

  // One 200 report from the first request, then a 421 report from the
  // second request, then a 200 report from the successful retry.
  ASSERT_EQ(3u, network_error_logging_service()->errors().size());

  // Check error report contents
  const NetworkErrorLoggingService::RequestDetails& error1 =
      network_error_logging_service()->errors()[0];
  EXPECT_EQ(GURL("https://www.example.org/"), error1.uri);
  EXPECT_TRUE(error1.referrer.is_empty());
  EXPECT_EQ("", error1.user_agent);
  EXPECT_EQ(ip, error1.server_ip);
  EXPECT_EQ("h2", error1.protocol);
  EXPECT_EQ("GET", error1.method);
  EXPECT_EQ(200, error1.status_code);
  EXPECT_EQ(OK, error1.type);
  EXPECT_EQ(0, error1.reporting_upload_depth);

  const NetworkErrorLoggingService::RequestDetails& error2 =
      network_error_logging_service()->errors()[1];
  EXPECT_EQ(GURL("https://mail.example.org/"), error2.uri);
  EXPECT_TRUE(error2.referrer.is_empty());
  EXPECT_EQ("", error2.user_agent);
  EXPECT_EQ(ip, error2.server_ip);
  EXPECT_EQ("h2", error2.protocol);
  EXPECT_EQ("GET", error2.method);
  EXPECT_EQ(421, error2.status_code);
  EXPECT_EQ(OK, error2.type);
  EXPECT_EQ(0, error2.reporting_upload_depth);

  const NetworkErrorLoggingService::RequestDetails& error3 =
      network_error_logging_service()->errors()[2];
  EXPECT_EQ(GURL("https://mail.example.org/"), error3.uri);
  EXPECT_TRUE(error3.referrer.is_empty());
  EXPECT_EQ("", error3.user_agent);
  EXPECT_EQ(ip, error3.server_ip);
  EXPECT_EQ("h2", error3.protocol);
  EXPECT_EQ("GET", error3.method);
  EXPECT_EQ(200, error3.status_code);
  EXPECT_EQ(OK, error3.type);
  EXPECT_EQ(0, error3.reporting_upload_depth);
}

TEST_F(HttpNetworkTransactionNetworkErrorLoggingTest,
       CreateReportCancelAfterStart) {
  StaticSocketDataProvider data;
  data.set_connect_data(MockConnect(SYNCHRONOUS, ERR_IO_PENDING));
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  TestCompletionCallback callback;
  auto session = CreateSession(&session_deps_);
  auto trans =
      std::make_unique<HttpNetworkTransaction>(DEFAULT_PRIORITY, session.get());
  int rv = trans->Start(&request_, callback.callback(), NetLogWithSource());
  EXPECT_EQ(rv, ERR_IO_PENDING);

  // Cancel after start.
  trans.reset();

  ASSERT_EQ(1u, network_error_logging_service()->errors().size());
  CheckReport(0 /* index */, 0 /* status_code */, ERR_ABORTED,
              IPAddress() /* server_ip */);
}

TEST_F(HttpNetworkTransactionNetworkErrorLoggingTest,
       CreateReportCancelBeforeReadingBody) {
  std::string extra_header_string = extra_headers_.ToString();
  MockWrite data_writes[] = {
      MockWrite("GET / HTTP/1.1\r\n"
                "Host: www.example.org\r\n"
                "Connection: keep-alive\r\n"),
      MockWrite(ASYNC, extra_header_string.data(), extra_header_string.size()),
  };
  MockRead data_reads[] = {
      MockRead("HTTP/1.0 200 OK\r\n"),
      MockRead("Content-Length: 100\r\n\r\n"),  // Body is never read.
  };

  StaticSocketDataProvider data(data_reads, data_writes);
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  SSLSocketDataProvider ssl(ASYNC, OK);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl);

  TestCompletionCallback callback;
  auto session = CreateSession(&session_deps_);
  auto trans =
      std::make_unique<HttpNetworkTransaction>(DEFAULT_PRIORITY, session.get());
  int rv = trans->Start(&request_, callback.callback(), NetLogWithSource());
  EXPECT_THAT(callback.GetResult(rv), IsOk());

  const HttpResponseInfo* response = trans->GetResponseInfo();
  ASSERT_TRUE(response);

  EXPECT_TRUE(response->headers);
  EXPECT_EQ("HTTP/1.0 200 OK", response->headers->GetStatusLine());

  // Cancel before reading the body.
  trans.reset();

  ASSERT_EQ(1u, network_error_logging_service()->errors().size());
  CheckReport(0 /* index */, 200 /* status_code */, ERR_ABORTED);
}

TEST_F(HttpNetworkTransactionNetworkErrorLoggingTest, DontCreateReportHttp) {
  RequestPolicy();
  EXPECT_EQ(1u, network_error_logging_service()->headers().size());
  EXPECT_EQ(1u, network_error_logging_service()->errors().size());

  // Make HTTP request
  std::string extra_header_string = extra_headers_.ToString();
  MockRead data_reads[] = {
      MockRead("HTTP/1.0 200 OK\r\n\r\n"),
      MockRead("hello world"),
      MockRead(SYNCHRONOUS, OK),
  };
  MockWrite data_writes[] = {
      MockWrite("GET / HTTP/1.1\r\n"
                "Host: www.example.org\r\n"
                "Connection: keep-alive\r\n"),
      MockWrite(ASYNC, extra_header_string.data(), extra_header_string.size()),
  };

  StaticSocketDataProvider data(data_reads, data_writes);
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  // Insecure url
  url_ = "http://www.example.org/";
  request_.url = GURL(url_);

  TestCompletionCallback callback;
  auto session = CreateSession(&session_deps_);
  auto trans =
      std::make_unique<HttpNetworkTransaction>(DEFAULT_PRIORITY, session.get());
  int rv = trans->Start(&request_, callback.callback(), NetLogWithSource());
  EXPECT_THAT(callback.GetResult(rv), IsOk());

  std::string response_data;
  ASSERT_THAT(ReadTransaction(trans.get(), &response_data), IsOk());
  EXPECT_EQ("hello world", response_data);

  // Insecure request does not generate a report
  EXPECT_EQ(1u, network_error_logging_service()->errors().size());
}

TEST_F(HttpNetworkTransactionNetworkErrorLoggingTest,
       DontCreateReportHttpError) {
  RequestPolicy();
  EXPECT_EQ(1u, network_error_logging_service()->headers().size());
  EXPECT_EQ(1u, network_error_logging_service()->errors().size());

  // Make HTTP request that fails
  MockRead data_reads[] = {
      MockRead("hello world"),
      MockRead(SYNCHRONOUS, OK),
  };

  StaticSocketDataProvider data(data_reads, base::span<MockWrite>());
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  url_ = "http://www.originwithoutpolicy.com:2000/";
  request_.url = GURL(url_);

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));

  auto trans =
      std::make_unique<HttpNetworkTransaction>(DEFAULT_PRIORITY, session.get());
  TestCompletionCallback callback;
  int rv = trans->Start(&request_, callback.callback(), NetLogWithSource());
  EXPECT_THAT(callback.GetResult(rv), IsError(ERR_INVALID_HTTP_RESPONSE));

  // Insecure request does not generate a report, regardless of existence of a
  // policy for the origin.
  EXPECT_EQ(1u, network_error_logging_service()->errors().size());
}

// Don't report on proxy auth challenges, don't report if connecting through a
// proxy.
TEST_F(HttpNetworkTransactionNetworkErrorLoggingTest, DontCreateReportProxy) {
  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("https://www.example.org/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  // Configure against proxy server "myproxy:70".
  session_deps_.proxy_resolution_service =
      ConfiguredProxyResolutionService::CreateFixedFromPacResultForTest(
          "PROXY myproxy:70", TRAFFIC_ANNOTATION_FOR_TESTS);
  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));

  // Since we have proxy, should try to establish tunnel.
  MockWrite data_writes1[] = {
      MockWrite("CONNECT www.example.org:443 HTTP/1.1\r\n"
                "Host: www.example.org:443\r\n"
                "Proxy-Connection: keep-alive\r\n\r\n"),
  };

  // The proxy responds to the connect with a 407, using a non-persistent
  // connection.
  MockRead data_reads1[] = {
      // No credentials.
      MockRead("HTTP/1.1 407 Proxy Authentication Required\r\n"),
      MockRead("Proxy-Authenticate: Basic realm=\"MyRealm1\"\r\n"),
      MockRead("Proxy-Connection: close\r\n\r\n"),
  };

  MockWrite data_writes2[] = {
      // After calling trans->RestartWithAuth(), this is the request we should
      // be issuing -- the final header line contains the credentials.
      MockWrite("CONNECT www.example.org:443 HTTP/1.1\r\n"
                "Host: www.example.org:443\r\n"
                "Proxy-Connection: keep-alive\r\n"
                "Proxy-Authorization: Basic Zm9vOmJhcg==\r\n\r\n"),

      MockWrite("GET / HTTP/1.1\r\n"
                "Host: www.example.org\r\n"
                "Connection: keep-alive\r\n\r\n"),
  };

  MockRead data_reads2[] = {
      MockRead("HTTP/1.1 200 Connection Established\r\n\r\n"),

      MockRead("HTTP/1.1 200 OK\r\n"),
      MockRead("Content-Type: text/html; charset=iso-8859-1\r\n"),
      MockRead("Content-Length: 5\r\n\r\n"),
      MockRead(SYNCHRONOUS, "hello"),
  };

  StaticSocketDataProvider data1(data_reads1, data_writes1);
  session_deps_.socket_factory->AddSocketDataProvider(&data1);
  StaticSocketDataProvider data2(data_reads2, data_writes2);
  session_deps_.socket_factory->AddSocketDataProvider(&data2);
  SSLSocketDataProvider ssl(ASYNC, OK);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl);

  TestCompletionCallback callback1;

  auto trans =
      std::make_unique<HttpNetworkTransaction>(DEFAULT_PRIORITY, session.get());

  int rv = trans->Start(&request, callback1.callback(), NetLogWithSource());
  EXPECT_THAT(callback1.GetResult(rv), IsOk());

  const HttpResponseInfo* response = trans->GetResponseInfo();
  EXPECT_EQ(407, response->headers->response_code());

  std::string response_data;
  rv = ReadTransaction(trans.get(), &response_data);
  EXPECT_THAT(rv, IsError(ERR_TUNNEL_CONNECTION_FAILED));

  // No NEL report is generated for the 407.
  EXPECT_EQ(0u, network_error_logging_service()->errors().size());

  TestCompletionCallback callback2;

  rv =
      trans->RestartWithAuth(AuthCredentials(kFoo, kBar), callback2.callback());
  EXPECT_THAT(callback2.GetResult(rv), IsOk());

  response = trans->GetResponseInfo();
  EXPECT_EQ(200, response->headers->response_code());

  ASSERT_THAT(ReadTransaction(trans.get(), &response_data), IsOk());
  EXPECT_EQ("hello", response_data);

  trans.reset();

  // No NEL report is generated because we are behind a proxy.
  EXPECT_EQ(0u, network_error_logging_service()->errors().size());
}

TEST_F(HttpNetworkTransactionNetworkErrorLoggingTest,
       ReportContainsUploadDepth) {
  reporting_upload_depth_ = 7;
  request_.reporting_upload_depth = reporting_upload_depth_;
  RequestPolicy();
  ASSERT_EQ(1u, network_error_logging_service()->errors().size());
  const NetworkErrorLoggingService::RequestDetails& error =
      network_error_logging_service()->errors()[0];
  EXPECT_EQ(7, error.reporting_upload_depth);
}

TEST_F(HttpNetworkTransactionNetworkErrorLoggingTest, ReportElapsedTime) {
  std::string extra_header_string = extra_headers_.ToString();
  static const base::TimeDelta kSleepDuration = base::Milliseconds(10);

  std::vector<MockWrite> data_writes = {
      MockWrite(ASYNC, 0,
                "GET / HTTP/1.1\r\n"
                "Host: www.example.org\r\n"
                "Connection: keep-alive\r\n"),
      MockWrite(ASYNC, 1, extra_header_string.data()),
  };

  std::vector<MockRead> data_reads = {
      // Write one byte of the status line, followed by a pause.
      MockRead(ASYNC, 2, "H"),
      MockRead(ASYNC, ERR_IO_PENDING, 3),
      MockRead(ASYNC, 4, "TTP/1.1 200 OK\r\n\r\n"),
      MockRead(ASYNC, 5, "hello world"),
      MockRead(SYNCHRONOUS, OK, 6),
  };

  SequencedSocketData data(data_reads, data_writes);
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  SSLSocketDataProvider ssl(ASYNC, OK);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl);

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));

  auto trans =
      std::make_unique<HttpNetworkTransaction>(DEFAULT_PRIORITY, session.get());

  TestCompletionCallback callback;

  int rv = trans->Start(&request_, callback.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  data.RunUntilPaused();
  ASSERT_TRUE(data.IsPaused());
  FastForwardBy(kSleepDuration);
  data.Resume();

  EXPECT_THAT(callback.GetResult(rv), IsOk());

  std::string response_data;
  ASSERT_THAT(ReadTransaction(trans.get(), &response_data), IsOk());
  EXPECT_EQ("hello world", response_data);

  trans.reset();

  ASSERT_EQ(1u, network_error_logging_service()->errors().size());

  CheckReport(0 /* index */, 200 /* status_code */, OK);

  const NetworkErrorLoggingService::RequestDetails& error =
      network_error_logging_service()->errors()[0];

  // Sanity-check elapsed time in error report
  EXPECT_EQ(kSleepDuration, error.elapsed_time);
}

#endif  // BUILDFLAG(ENABLE_REPORTING)

TEST_F(HttpNetworkTransactionTest, AlwaysFailRequestToCache) {
  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("http://example.org/");

  request.load_flags = LOAD_ONLY_FROM_CACHE;

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));
  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());
  TestCompletionCallback callback1;
  int rv = trans.Start(&request, callback1.callback(), NetLogWithSource());

  EXPECT_THAT(rv, IsError(ERR_CACHE_MISS));
}

TEST_F(HttpNetworkTransactionTest, ZeroRTTDoesntConfirm) {
  static const base::TimeDelta kDelay = base::Milliseconds(10);
  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("https://www.example.org/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  MockWrite data_writes[] = {
      MockWrite("GET / HTTP/1.1\r\n"
                "Host: www.example.org\r\n"
                "Connection: keep-alive\r\n\r\n"),
  };

  MockRead data_reads[] = {
      MockRead("HTTP/1.1 200 OK\r\n"),
      MockRead("Content-Length: 1\r\n\r\n"),
      MockRead(SYNCHRONOUS, "1"),
  };

  StaticSocketDataProvider data(data_reads, data_writes);
  session_deps_.socket_factory->AddSocketDataProvider(&data);
  SSLSocketDataProvider ssl(SYNCHRONOUS, OK);
  ssl.connect_callback = FastForwardByCallback(kDelay);
  ssl.confirm = MockConfirm(SYNCHRONOUS, OK);
  ssl.confirm_callback = FastForwardByCallback(kDelay);
  session_deps_.enable_early_data = true;
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl);

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));

  TestCompletionCallback callback;
  auto trans =
      std::make_unique<HttpNetworkTransaction>(DEFAULT_PRIORITY, session.get());

  base::TimeTicks start_time = base::TimeTicks::Now();
  int rv = trans->Start(&request, callback.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback.WaitForResult();
  EXPECT_THAT(rv, IsOk());

  const HttpResponseInfo* response = trans->GetResponseInfo();
  ASSERT_TRUE(response);
  ASSERT_TRUE(response->headers);
  EXPECT_EQ(200, response->headers->response_code());
  EXPECT_EQ(1, response->headers->GetContentLength());

  // Check that ConfirmHandshake wasn't called.
  ASSERT_FALSE(ssl.ConfirmDataConsumed());
  ASSERT_TRUE(ssl.WriteBeforeConfirm());

  // The handshake time should include the time it took to run Connect(), but
  // not ConfirmHandshake().
  LoadTimingInfo load_timing_info;
  EXPECT_TRUE(trans->GetLoadTimingInfo(&load_timing_info));
  EXPECT_EQ(load_timing_info.connect_timing.connect_start, start_time);
  EXPECT_EQ(load_timing_info.connect_timing.ssl_start, start_time);
  EXPECT_EQ(load_timing_info.connect_timing.ssl_end, start_time + kDelay);
  EXPECT_EQ(load_timing_info.connect_timing.connect_end, start_time + kDelay);

  trans.reset();

  session->CloseAllConnections(ERR_FAILED, "Very good reason");
}

TEST_F(HttpNetworkTransactionTest, ZeroRTTSyncConfirmSyncWrite) {
  static const base::TimeDelta kDelay = base::Milliseconds(10);
  HttpRequestInfo request;
  request.method = "POST";
  request.url = GURL("https://www.example.org/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  MockWrite data_writes[] = {
      MockWrite(SYNCHRONOUS,
                "POST / HTTP/1.1\r\n"
                "Host: www.example.org\r\n"
                "Connection: keep-alive\r\n"
                "Content-Length: 0\r\n\r\n"),
  };

  MockRead data_reads[] = {
      MockRead("HTTP/1.1 200 OK\r\n"),
      MockRead("Content-Length: 1\r\n\r\n"),
      MockRead(SYNCHRONOUS, "1"),
  };

  StaticSocketDataProvider data(data_reads, data_writes);
  session_deps_.socket_factory->AddSocketDataProvider(&data);
  SSLSocketDataProvider ssl(SYNCHRONOUS, OK);
  ssl.connect_callback = FastForwardByCallback(kDelay);
  ssl.confirm = MockConfirm(SYNCHRONOUS, OK);
  ssl.confirm_callback = FastForwardByCallback(kDelay);
  session_deps_.enable_early_data = true;
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl);

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));

  TestCompletionCallback callback;
  auto trans =
      std::make_unique<HttpNetworkTransaction>(DEFAULT_PRIORITY, session.get());

  base::TimeTicks start_time = base::TimeTicks::Now();
  int rv = trans->Start(&request, callback.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback.WaitForResult();
  EXPECT_THAT(rv, IsOk());

  const HttpResponseInfo* response = trans->GetResponseInfo();
  ASSERT_TRUE(response);
  ASSERT_TRUE(response->headers);
  EXPECT_EQ(200, response->headers->response_code());
  EXPECT_EQ(1, response->headers->GetContentLength());

  // Check that the Write didn't get called before ConfirmHandshake completed.
  ASSERT_FALSE(ssl.WriteBeforeConfirm());

  // The handshake time should include the time it took to run Connect(), but
  // not ConfirmHandshake(). If ConfirmHandshake() returns synchronously, we
  // assume the connection did not negotiate 0-RTT or the handshake was already
  // confirmed.
  LoadTimingInfo load_timing_info;
  EXPECT_TRUE(trans->GetLoadTimingInfo(&load_timing_info));
  EXPECT_EQ(load_timing_info.connect_timing.connect_start, start_time);
  EXPECT_EQ(load_timing_info.connect_timing.ssl_start, start_time);
  EXPECT_EQ(load_timing_info.connect_timing.ssl_end, start_time + kDelay);
  EXPECT_EQ(load_timing_info.connect_timing.connect_end, start_time + kDelay);

  trans.reset();

  session->CloseAllConnections(ERR_FAILED, "Very good reason");
}

TEST_F(HttpNetworkTransactionTest, ZeroRTTSyncConfirmAsyncWrite) {
  HttpRequestInfo request;
  request.method = "POST";
  request.url = GURL("https://www.example.org/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  MockWrite data_writes[] = {
      MockWrite(ASYNC,
                "POST / HTTP/1.1\r\n"
                "Host: www.example.org\r\n"
                "Connection: keep-alive\r\n"
                "Content-Length: 0\r\n\r\n"),
  };

  MockRead data_reads[] = {
      MockRead("HTTP/1.1 200 OK\r\n"),
      MockRead("Content-Length: 1\r\n\r\n"),
      MockRead(SYNCHRONOUS, "1"),
  };

  StaticSocketDataProvider data(data_reads, data_writes);
  session_deps_.socket_factory->AddSocketDataProvider(&data);
  SSLSocketDataProvider ssl(SYNCHRONOUS, OK);
  ssl.confirm = MockConfirm(SYNCHRONOUS, OK);
  session_deps_.enable_early_data = true;
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl);

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));

  TestCompletionCallback callback;
  auto trans =
      std::make_unique<HttpNetworkTransaction>(DEFAULT_PRIORITY, session.get());

  int rv = trans->Start(&request, callback.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback.WaitForResult();
  EXPECT_THAT(rv, IsOk());

  const HttpResponseInfo* response = trans->GetResponseInfo();
  ASSERT_TRUE(response);
  ASSERT_TRUE(response->headers);
  EXPECT_EQ(200, response->headers->response_code());
  EXPECT_EQ(1, response->headers->GetContentLength());

  // Check that the Write didn't get called before ConfirmHandshake completed.
  ASSERT_FALSE(ssl.WriteBeforeConfirm());

  trans.reset();

  session->CloseAllConnections(ERR_FAILED, "Very good reason");
}

TEST_F(HttpNetworkTransactionTest, ZeroRTTAsyncConfirmSyncWrite) {
  static const base::TimeDelta kDelay = base::Milliseconds(10);
  HttpRequestInfo request;
  request.method = "POST";
  request.url = GURL("https://www.example.org/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  MockWrite data_writes[] = {
      MockWrite(SYNCHRONOUS,
                "POST / HTTP/1.1\r\n"
                "Host: www.example.org\r\n"
                "Connection: keep-alive\r\n"
                "Content-Length: 0\r\n\r\n"),
  };

  MockRead data_reads[] = {
      MockRead("HTTP/1.1 200 OK\r\n"),
      MockRead("Content-Length: 1\r\n\r\n"),
      MockRead(SYNCHRONOUS, "1"),
  };

  StaticSocketDataProvider data(data_reads, data_writes);
  session_deps_.socket_factory->AddSocketDataProvider(&data);
  SSLSocketDataProvider ssl(SYNCHRONOUS, OK);
  ssl.connect_callback = FastForwardByCallback(kDelay);
  ssl.confirm = MockConfirm(ASYNC, OK);
  ssl.confirm_callback = FastForwardByCallback(kDelay);
  session_deps_.enable_early_data = true;
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl);

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));

  TestCompletionCallback callback;
  auto trans =
      std::make_unique<HttpNetworkTransaction>(DEFAULT_PRIORITY, session.get());

  base::TimeTicks start_time = base::TimeTicks::Now();
  int rv = trans->Start(&request, callback.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback.WaitForResult();
  EXPECT_THAT(rv, IsOk());

  const HttpResponseInfo* response = trans->GetResponseInfo();
  ASSERT_TRUE(response);
  ASSERT_TRUE(response->headers);
  EXPECT_EQ(200, response->headers->response_code());
  EXPECT_EQ(1, response->headers->GetContentLength());

  // Check that the Write didn't get called before ConfirmHandshake completed.
  ASSERT_FALSE(ssl.WriteBeforeConfirm());

  // The handshake time should include the time it took to run Connect() and
  // ConfirmHandshake().
  LoadTimingInfo load_timing_info;
  EXPECT_TRUE(trans->GetLoadTimingInfo(&load_timing_info));
  EXPECT_EQ(load_timing_info.connect_timing.connect_start, start_time);
  EXPECT_EQ(load_timing_info.connect_timing.ssl_start, start_time);
  EXPECT_EQ(load_timing_info.connect_timing.ssl_end, start_time + 2 * kDelay);
  EXPECT_EQ(load_timing_info.connect_timing.connect_end,
            start_time + 2 * kDelay);

  trans.reset();

  session->CloseAllConnections(ERR_FAILED, "Very good reason");
}

TEST_F(HttpNetworkTransactionTest, ZeroRTTAsyncConfirmAsyncWrite) {
  HttpRequestInfo request;
  request.method = "POST";
  request.url = GURL("https://www.example.org/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  MockWrite data_writes[] = {
      MockWrite(ASYNC,
                "POST / HTTP/1.1\r\n"
                "Host: www.example.org\r\n"
                "Connection: keep-alive\r\n"
                "Content-Length: 0\r\n\r\n"),
  };

  MockRead data_reads[] = {
      MockRead("HTTP/1.1 200 OK\r\n"),
      MockRead("Content-Length: 1\r\n\r\n"),
      MockRead(SYNCHRONOUS, "1"),
  };

  StaticSocketDataProvider data(data_reads, data_writes);
  session_deps_.socket_factory->AddSocketDataProvider(&data);
  SSLSocketDataProvider ssl(SYNCHRONOUS, OK);
  ssl.confirm = MockConfirm(ASYNC, OK);
  session_deps_.enable_early_data = true;
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl);

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));

  TestCompletionCallback callback;
  auto trans =
      std::make_unique<HttpNetworkTransaction>(DEFAULT_PRIORITY, session.get());

  int rv = trans->Start(&request, callback.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback.WaitForResult();
  EXPECT_THAT(rv, IsOk());

  const HttpResponseInfo* response = trans->GetResponseInfo();
  ASSERT_TRUE(response);
  ASSERT_TRUE(response->headers);
  EXPECT_EQ(200, response->headers->response_code());
  EXPECT_EQ(1, response->headers->GetContentLength());

  // Check that the Write didn't get called before ConfirmHandshake completed.
  ASSERT_FALSE(ssl.WriteBeforeConfirm());

  trans.reset();

  session->CloseAllConnections(ERR_FAILED, "Very good reason");
}

// 0-RTT rejects are handled at HttpNetworkTransaction.
TEST_F(HttpNetworkTransactionTest, ZeroRTTReject) {
  enum class RejectType {
    kRead,
    kWrite,
    kConfirm,
  };

  for (RejectType type :
       {RejectType::kRead, RejectType::kWrite, RejectType::kConfirm}) {
    SCOPED_TRACE(static_cast<int>(type));
    for (Error reject_error :
         {ERR_EARLY_DATA_REJECTED, ERR_WRONG_VERSION_ON_EARLY_DATA}) {
      SCOPED_TRACE(reject_error);
      session_deps_.socket_factory =
          std::make_unique<MockClientSocketFactory>();

      HttpRequestInfo request;
      request.method = type == RejectType::kConfirm ? "POST" : "GET";
      request.url = GURL("https://www.example.org/");
      request.traffic_annotation =
          net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

      // The first request fails.
      std::vector<MockWrite> data1_writes;
      std::vector<MockRead> data1_reads;
      SSLSocketDataProvider ssl1(SYNCHRONOUS, OK);
      switch (type) {
        case RejectType::kRead:
          data1_writes.emplace_back(
              "GET / HTTP/1.1\r\n"
              "Host: www.example.org\r\n"
              "Connection: keep-alive\r\n\r\n");
          data1_reads.emplace_back(ASYNC, reject_error);
          // Cause ConfirmHandshake to hang (it should not be called).
          ssl1.confirm = MockConfirm(SYNCHRONOUS, ERR_IO_PENDING);
          break;
        case RejectType::kWrite:
          data1_writes.emplace_back(ASYNC, reject_error);
          // Cause ConfirmHandshake to hang (it should not be called).
          ssl1.confirm = MockConfirm(SYNCHRONOUS, ERR_IO_PENDING);
          break;
        case RejectType::kConfirm:
          // The request never gets far enough to read or write.
          ssl1.confirm = MockConfirm(ASYNC, reject_error);
          break;
      }

      StaticSocketDataProvider data1(data1_reads, data1_writes);
      session_deps_.socket_factory->AddSocketDataProvider(&data1);
      session_deps_.enable_early_data = true;
      session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl1);

      // The retry succeeds.
      //
      // TODO(https://crbug.com/950705): If |reject_error| is
      // ERR_EARLY_DATA_REJECTED, the retry should happen over the same socket.
      MockWrite data2_writes[] = {
          request.method == "POST"
              ? MockWrite("POST / HTTP/1.1\r\n"
                          "Host: www.example.org\r\n"
                          "Connection: keep-alive\r\n"
                          "Content-Length: 0\r\n\r\n")
              : MockWrite("GET / HTTP/1.1\r\n"
                          "Host: www.example.org\r\n"
                          "Connection: keep-alive\r\n\r\n"),
      };

      MockRead data2_reads[] = {
          MockRead("HTTP/1.1 200 OK\r\n"),
          MockRead("Content-Length: 1\r\n\r\n"),
          MockRead(SYNCHRONOUS, "1"),
      };

      StaticSocketDataProvider data2(data2_reads, data2_writes);
      session_deps_.socket_factory->AddSocketDataProvider(&data2);
      SSLSocketDataProvider ssl2(SYNCHRONOUS, OK);
      ssl2.confirm = MockConfirm(ASYNC, OK);
      session_deps_.enable_early_data = true;
      session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl2);

      std::unique_ptr<HttpNetworkSession> session(
          CreateSession(&session_deps_));

      TestCompletionCallback callback;
      auto trans = std::make_unique<HttpNetworkTransaction>(DEFAULT_PRIORITY,
                                                            session.get());

      EXPECT_THAT(callback.GetResult(trans->Start(&request, callback.callback(),
                                                  NetLogWithSource())),
                  IsOk());

      const HttpResponseInfo* response = trans->GetResponseInfo();
      ASSERT_TRUE(response);
      ASSERT_TRUE(response->headers);
      EXPECT_EQ(200, response->headers->response_code());
      EXPECT_EQ(1, response->headers->GetContentLength());
    }
  }
}

TEST_F(HttpNetworkTransactionTest, ZeroRTTConfirmErrorSync) {
  HttpRequestInfo request;
  request.method = "POST";
  request.url = GURL("https://www.example.org/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  MockWrite data_writes[] = {
      MockWrite("POST / HTTP/1.1\r\n"
                "Host: www.example.org\r\n"
                "Connection: keep-alive\r\n"
                "Content-Length: 0\r\n\r\n"),
  };

  MockRead data_reads[] = {
      MockRead("HTTP/1.1 200 OK\r\n"),
      MockRead("Content-Length: 1\r\n\r\n"),
      MockRead(SYNCHRONOUS, "1"),
  };

  StaticSocketDataProvider data(data_reads, data_writes);
  session_deps_.socket_factory->AddSocketDataProvider(&data);
  SSLSocketDataProvider ssl(SYNCHRONOUS, OK);
  ssl.confirm = MockConfirm(SYNCHRONOUS, ERR_SSL_PROTOCOL_ERROR);
  session_deps_.enable_early_data = true;
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl);

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));

  TestCompletionCallback callback;
  auto trans =
      std::make_unique<HttpNetworkTransaction>(DEFAULT_PRIORITY, session.get());

  int rv = trans->Start(&request, callback.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback.WaitForResult();
  EXPECT_THAT(rv, IsError(ERR_SSL_PROTOCOL_ERROR));

  // Check that the Write didn't get called before ConfirmHandshake completed.
  ASSERT_FALSE(ssl.WriteBeforeConfirm());

  trans.reset();

  session->CloseAllConnections(ERR_FAILED, "Very good reason");
}

TEST_F(HttpNetworkTransactionTest, ZeroRTTConfirmErrorAsync) {
  HttpRequestInfo request;
  request.method = "POST";
  request.url = GURL("https://www.example.org/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  MockWrite data_writes[] = {
      MockWrite("POST / HTTP/1.1\r\n"
                "Host: www.example.org\r\n"
                "Connection: keep-alive\r\n"
                "Content-Length: 0\r\n\r\n"),
  };

  MockRead data_reads[] = {
      MockRead("HTTP/1.1 200 OK\r\n"),
      MockRead("Content-Length: 1\r\n\r\n"),
      MockRead(SYNCHRONOUS, "1"),
  };

  StaticSocketDataProvider data(data_reads, data_writes);
  session_deps_.socket_factory->AddSocketDataProvider(&data);
  SSLSocketDataProvider ssl(SYNCHRONOUS, OK);
  ssl.confirm = MockConfirm(ASYNC, ERR_SSL_PROTOCOL_ERROR);
  session_deps_.enable_early_data = true;
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl);

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));

  TestCompletionCallback callback;
  auto trans =
      std::make_unique<HttpNetworkTransaction>(DEFAULT_PRIORITY, session.get());

  int rv = trans->Start(&request, callback.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = callback.WaitForResult();
  EXPECT_THAT(rv, IsError(ERR_SSL_PROTOCOL_ERROR));

  // Check that the Write didn't get called before ConfirmHandshake completed.
  ASSERT_FALSE(ssl.WriteBeforeConfirm());

  trans.reset();

  session->CloseAllConnections(ERR_FAILED, "Very good reason");
}

// Test the proxy and origin server each requesting both TLS client certificates
// and HTTP auth. This is a regression test for https://crbug.com/946406.
TEST_F(HttpNetworkTransactionTest, AuthEverything) {
  // Note these hosts must match the CheckBasic*Auth() functions.
  session_deps_.proxy_resolution_service =
      ConfiguredProxyResolutionService::CreateFixedForTest(
          "https://myproxy:70", TRAFFIC_ANNOTATION_FOR_TESTS);

  auto cert_request_info_proxy = base::MakeRefCounted<SSLCertRequestInfo>();
  cert_request_info_proxy->host_and_port = HostPortPair("myproxy", 70);

  std::unique_ptr<FakeClientCertIdentity> identity_proxy =
      FakeClientCertIdentity::CreateFromCertAndKeyFiles(
          GetTestCertsDirectory(), "client_1.pem", "client_1.pk8");
  ASSERT_TRUE(identity_proxy);

  auto cert_request_info_origin = base::MakeRefCounted<SSLCertRequestInfo>();
  cert_request_info_origin->host_and_port =
      HostPortPair("www.example.org", 443);

  std::unique_ptr<FakeClientCertIdentity> identity_origin =
      FakeClientCertIdentity::CreateFromCertAndKeyFiles(
          GetTestCertsDirectory(), "client_2.pem", "client_2.pk8");
  ASSERT_TRUE(identity_origin);

  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("https://www.example.org/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  // First, the client connects to the proxy, which requests a client
  // certificate.
  SSLSocketDataProvider ssl_proxy1(ASYNC, ERR_SSL_CLIENT_AUTH_CERT_NEEDED);
  ssl_proxy1.cert_request_info = cert_request_info_proxy.get();
  ssl_proxy1.expected_send_client_cert = false;
  StaticSocketDataProvider data1;
  session_deps_.socket_factory->AddSocketDataProvider(&data1);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl_proxy1);

  // The client responds with a certificate on a new connection. The handshake
  // succeeds.
  SSLSocketDataProvider ssl_proxy2(ASYNC, OK);
  ssl_proxy2.expected_send_client_cert = true;
  ssl_proxy2.expected_client_cert = identity_proxy->certificate();
  // The client attempts an HTTP CONNECT, but the proxy requests basic auth.
  std::vector<MockWrite> mock_writes2;
  std::vector<MockRead> mock_reads2;
  mock_writes2.emplace_back(
      "CONNECT www.example.org:443 HTTP/1.1\r\n"
      "Host: www.example.org:443\r\n"
      "Proxy-Connection: keep-alive\r\n\r\n");
  mock_reads2.emplace_back(
      "HTTP/1.1 407 Proxy Authentication Required\r\n"
      "Content-Length: 0\r\n"
      "Proxy-Authenticate: Basic realm=\"MyRealm1\"\r\n\r\n");
  // The client retries with credentials.
  mock_writes2.emplace_back(
      "CONNECT www.example.org:443 HTTP/1.1\r\n"
      "Host: www.example.org:443\r\n"
      "Proxy-Connection: keep-alive\r\n"
      // Authenticate as proxyuser:proxypass.
      "Proxy-Authorization: Basic cHJveHl1c2VyOnByb3h5cGFzcw==\r\n\r\n");
  mock_reads2.emplace_back("HTTP/1.1 200 Connection Established\r\n\r\n");
  // The origin requests client certificates.
  SSLSocketDataProvider ssl_origin2(ASYNC, ERR_SSL_CLIENT_AUTH_CERT_NEEDED);
  ssl_origin2.cert_request_info = cert_request_info_origin.get();
  StaticSocketDataProvider data2(mock_reads2, mock_writes2);
  session_deps_.socket_factory->AddSocketDataProvider(&data2);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl_proxy2);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl_origin2);

  // The client responds to the origin client certificate request on a new
  // connection.
  SSLSocketDataProvider ssl_proxy3(ASYNC, OK);
  ssl_proxy3.expected_send_client_cert = true;
  ssl_proxy3.expected_client_cert = identity_proxy->certificate();
  std::vector<MockWrite> mock_writes3;
  std::vector<MockRead> mock_reads3;
  mock_writes3.emplace_back(
      "CONNECT www.example.org:443 HTTP/1.1\r\n"
      "Host: www.example.org:443\r\n"
      "Proxy-Connection: keep-alive\r\n"
      // Authenticate as proxyuser:proxypass.
      "Proxy-Authorization: Basic cHJveHl1c2VyOnByb3h5cGFzcw==\r\n\r\n");
  mock_reads3.emplace_back("HTTP/1.1 200 Connection Established\r\n\r\n");
  SSLSocketDataProvider ssl_origin3(ASYNC, OK);
  ssl_origin3.expected_send_client_cert = true;
  ssl_origin3.expected_client_cert = identity_origin->certificate();
  // The client sends the origin HTTP request, which results in another HTTP
  // auth request.
  mock_writes3.emplace_back(
      "GET / HTTP/1.1\r\n"
      "Host: www.example.org\r\n"
      "Connection: keep-alive\r\n\r\n");
  mock_reads3.emplace_back(
      "HTTP/1.1 401 Unauthorized\r\n"
      "WWW-Authenticate: Basic realm=\"MyRealm1\"\r\n"
      "Content-Length: 0\r\n\r\n");
  // The client retries with credentials, and the request finally succeeds.
  mock_writes3.emplace_back(
      "GET / HTTP/1.1\r\n"
      "Host: www.example.org\r\n"
      "Connection: keep-alive\r\n"
      // Authenticate as user:pass.
      "Authorization: Basic dXNlcjpwYXNz\r\n\r\n");
  mock_reads3.emplace_back(
      "HTTP/1.1 200 OK\r\n"
      "Content-Length: 0\r\n\r\n");
  // The client makes another request. This should reuse the socket with all
  // credentials cached.
  mock_writes3.emplace_back(
      "GET / HTTP/1.1\r\n"
      "Host: www.example.org\r\n"
      "Connection: keep-alive\r\n"
      // Authenticate as user:pass.
      "Authorization: Basic dXNlcjpwYXNz\r\n\r\n");
  mock_reads3.emplace_back(
      "HTTP/1.1 200 OK\r\n"
      "Content-Length: 0\r\n\r\n");
  StaticSocketDataProvider data3(mock_reads3, mock_writes3);
  session_deps_.socket_factory->AddSocketDataProvider(&data3);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl_proxy3);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl_origin3);

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));

  // Start the request.
  TestCompletionCallback callback;
  auto trans =
      std::make_unique<HttpNetworkTransaction>(DEFAULT_PRIORITY, session.get());
  int rv = callback.GetResult(
      trans->Start(&request, callback.callback(), NetLogWithSource()));

  // Handle the proxy client certificate challenge.
  ASSERT_THAT(rv, IsError(ERR_SSL_CLIENT_AUTH_CERT_NEEDED));
  SSLCertRequestInfo* cert_request_info =
      trans->GetResponseInfo()->cert_request_info.get();
  ASSERT_TRUE(cert_request_info);
  EXPECT_TRUE(cert_request_info->is_proxy);
  EXPECT_EQ(cert_request_info->host_and_port,
            cert_request_info_proxy->host_and_port);
  rv = callback.GetResult(trans->RestartWithCertificate(
      identity_proxy->certificate(), identity_proxy->ssl_private_key(),
      callback.callback()));

  // Handle the proxy HTTP auth challenge.
  ASSERT_THAT(rv, IsOk());
  EXPECT_EQ(407, trans->GetResponseInfo()->headers->response_code());
  EXPECT_TRUE(
      CheckBasicSecureProxyAuth(trans->GetResponseInfo()->auth_challenge));
  rv = callback.GetResult(trans->RestartWithAuth(
      AuthCredentials(u"proxyuser", u"proxypass"), callback.callback()));

  // Handle the origin client certificate challenge.
  ASSERT_THAT(rv, IsError(ERR_SSL_CLIENT_AUTH_CERT_NEEDED));
  cert_request_info = trans->GetResponseInfo()->cert_request_info.get();
  ASSERT_TRUE(cert_request_info);
  EXPECT_FALSE(cert_request_info->is_proxy);
  EXPECT_EQ(cert_request_info->host_and_port,
            cert_request_info_origin->host_and_port);
  rv = callback.GetResult(trans->RestartWithCertificate(
      identity_origin->certificate(), identity_origin->ssl_private_key(),
      callback.callback()));

  // Handle the origin HTTP auth challenge.
  ASSERT_THAT(rv, IsOk());
  EXPECT_EQ(401, trans->GetResponseInfo()->headers->response_code());
  EXPECT_TRUE(
      CheckBasicSecureServerAuth(trans->GetResponseInfo()->auth_challenge));
  rv = callback.GetResult(trans->RestartWithAuth(
      AuthCredentials(u"user", u"pass"), callback.callback()));

  // The request completes.
  ASSERT_THAT(rv, IsOk());
  EXPECT_EQ(200, trans->GetResponseInfo()->headers->response_code());

  // Make a second request. This time all credentials are cached.
  trans =
      std::make_unique<HttpNetworkTransaction>(DEFAULT_PRIORITY, session.get());
  ASSERT_THAT(callback.GetResult(trans->Start(&request, callback.callback(),
                                              NetLogWithSource())),
              IsOk());
  EXPECT_EQ(200, trans->GetResponseInfo()->headers->response_code());
}

// Test the proxy and origin server each requesting both TLS client certificates
// and HTTP auth and each HTTP auth closing the connection. This is a regression
// test for https://crbug.com/946406.
TEST_F(HttpNetworkTransactionTest, AuthEverythingWithConnectClose) {
  // Note these hosts must match the CheckBasic*Auth() functions.
  session_deps_.proxy_resolution_service =
      ConfiguredProxyResolutionService::CreateFixedForTest(
          "https://myproxy:70", TRAFFIC_ANNOTATION_FOR_TESTS);

  auto cert_request_info_proxy = base::MakeRefCounted<SSLCertRequestInfo>();
  cert_request_info_proxy->host_and_port = HostPortPair("myproxy", 70);

  std::unique_ptr<FakeClientCertIdentity> identity_proxy =
      FakeClientCertIdentity::CreateFromCertAndKeyFiles(
          GetTestCertsDirectory(), "client_1.pem", "client_1.pk8");
  ASSERT_TRUE(identity_proxy);

  auto cert_request_info_origin = base::MakeRefCounted<SSLCertRequestInfo>();
  cert_request_info_origin->host_and_port =
      HostPortPair("www.example.org", 443);

  std::unique_ptr<FakeClientCertIdentity> identity_origin =
      FakeClientCertIdentity::CreateFromCertAndKeyFiles(
          GetTestCertsDirectory(), "client_2.pem", "client_2.pk8");
  ASSERT_TRUE(identity_origin);

  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("https://www.example.org/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  // First, the client connects to the proxy, which requests a client
  // certificate.
  SSLSocketDataProvider ssl_proxy1(ASYNC, ERR_SSL_CLIENT_AUTH_CERT_NEEDED);
  ssl_proxy1.cert_request_info = cert_request_info_proxy.get();
  ssl_proxy1.expected_send_client_cert = false;
  StaticSocketDataProvider data1;
  session_deps_.socket_factory->AddSocketDataProvider(&data1);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl_proxy1);

  // The client responds with a certificate on a new connection. The handshake
  // succeeds.
  SSLSocketDataProvider ssl_proxy2(ASYNC, OK);
  ssl_proxy2.expected_send_client_cert = true;
  ssl_proxy2.expected_client_cert = identity_proxy->certificate();
  // The client attempts an HTTP CONNECT, but the proxy requests basic auth.
  std::vector<MockWrite> mock_writes2;
  std::vector<MockRead> mock_reads2;
  mock_writes2.emplace_back(
      "CONNECT www.example.org:443 HTTP/1.1\r\n"
      "Host: www.example.org:443\r\n"
      "Proxy-Connection: keep-alive\r\n\r\n");
  mock_reads2.emplace_back(
      "HTTP/1.1 407 Proxy Authentication Required\r\n"
      "Content-Length: 0\r\n"
      "Proxy-Connection: close\r\n"
      "Proxy-Authenticate: Basic realm=\"MyRealm1\"\r\n\r\n");
  StaticSocketDataProvider data2(mock_reads2, mock_writes2);
  session_deps_.socket_factory->AddSocketDataProvider(&data2);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl_proxy2);

  // The client retries with credentials on a new connection.
  SSLSocketDataProvider ssl_proxy3(ASYNC, OK);
  ssl_proxy3.expected_send_client_cert = true;
  ssl_proxy3.expected_client_cert = identity_proxy->certificate();
  std::vector<MockWrite> mock_writes3;
  std::vector<MockRead> mock_reads3;
  mock_writes3.emplace_back(
      "CONNECT www.example.org:443 HTTP/1.1\r\n"
      "Host: www.example.org:443\r\n"
      "Proxy-Connection: keep-alive\r\n"
      // Authenticate as proxyuser:proxypass.
      "Proxy-Authorization: Basic cHJveHl1c2VyOnByb3h5cGFzcw==\r\n\r\n");
  mock_reads3.emplace_back("HTTP/1.1 200 Connection Established\r\n\r\n");
  // The origin requests client certificates.
  SSLSocketDataProvider ssl_origin3(ASYNC, ERR_SSL_CLIENT_AUTH_CERT_NEEDED);
  ssl_origin3.cert_request_info = cert_request_info_origin.get();
  StaticSocketDataProvider data3(mock_reads3, mock_writes3);
  session_deps_.socket_factory->AddSocketDataProvider(&data3);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl_proxy3);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl_origin3);

  // The client responds to the origin client certificate request on a new
  // connection.
  SSLSocketDataProvider ssl_proxy4(ASYNC, OK);
  ssl_proxy4.expected_send_client_cert = true;
  ssl_proxy4.expected_client_cert = identity_proxy->certificate();
  std::vector<MockWrite> mock_writes4;
  std::vector<MockRead> mock_reads4;
  mock_writes4.emplace_back(
      "CONNECT www.example.org:443 HTTP/1.1\r\n"
      "Host: www.example.org:443\r\n"
      "Proxy-Connection: keep-alive\r\n"
      // Authenticate as proxyuser:proxypass.
      "Proxy-Authorization: Basic cHJveHl1c2VyOnByb3h5cGFzcw==\r\n\r\n");
  mock_reads4.emplace_back("HTTP/1.1 200 Connection Established\r\n\r\n");
  SSLSocketDataProvider ssl_origin4(ASYNC, OK);
  ssl_origin4.expected_send_client_cert = true;
  ssl_origin4.expected_client_cert = identity_origin->certificate();
  // The client sends the origin HTTP request, which results in another HTTP
  // auth request and closed connection.
  mock_writes4.emplace_back(
      "GET / HTTP/1.1\r\n"
      "Host: www.example.org\r\n"
      "Connection: keep-alive\r\n\r\n");
  mock_reads4.emplace_back(
      "HTTP/1.1 401 Unauthorized\r\n"
      "WWW-Authenticate: Basic realm=\"MyRealm1\"\r\n"
      "Connection: close\r\n"
      "Content-Length: 0\r\n\r\n");
  StaticSocketDataProvider data4(mock_reads4, mock_writes4);
  session_deps_.socket_factory->AddSocketDataProvider(&data4);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl_proxy4);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl_origin4);

  // The client retries with credentials on a new connection, and the request
  // finally succeeds.
  SSLSocketDataProvider ssl_proxy5(ASYNC, OK);
  ssl_proxy5.expected_send_client_cert = true;
  ssl_proxy5.expected_client_cert = identity_proxy->certificate();
  std::vector<MockWrite> mock_writes5;
  std::vector<MockRead> mock_reads5;
  mock_writes5.emplace_back(
      "CONNECT www.example.org:443 HTTP/1.1\r\n"
      "Host: www.example.org:443\r\n"
      "Proxy-Connection: keep-alive\r\n"
      // Authenticate as proxyuser:proxypass.
      "Proxy-Authorization: Basic cHJveHl1c2VyOnByb3h5cGFzcw==\r\n\r\n");
  mock_reads5.emplace_back("HTTP/1.1 200 Connection Established\r\n\r\n");
  SSLSocketDataProvider ssl_origin5(ASYNC, OK);
  ssl_origin5.expected_send_client_cert = true;
  ssl_origin5.expected_client_cert = identity_origin->certificate();
  mock_writes5.emplace_back(
      "GET / HTTP/1.1\r\n"
      "Host: www.example.org\r\n"
      "Connection: keep-alive\r\n"
      // Authenticate as user:pass.
      "Authorization: Basic dXNlcjpwYXNz\r\n\r\n");
  mock_reads5.emplace_back(
      "HTTP/1.1 200 OK\r\n"
      "Connection: close\r\n"
      "Content-Length: 0\r\n\r\n");
  StaticSocketDataProvider data5(mock_reads5, mock_writes5);
  session_deps_.socket_factory->AddSocketDataProvider(&data5);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl_proxy5);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl_origin5);

  // The client makes a second request. This needs yet another connection, but
  // all credentials are cached.
  SSLSocketDataProvider ssl_proxy6(ASYNC, OK);
  ssl_proxy6.expected_send_client_cert = true;
  ssl_proxy6.expected_client_cert = identity_proxy->certificate();
  std::vector<MockWrite> mock_writes6;
  std::vector<MockRead> mock_reads6;
  mock_writes6.emplace_back(
      "CONNECT www.example.org:443 HTTP/1.1\r\n"
      "Host: www.example.org:443\r\n"
      "Proxy-Connection: keep-alive\r\n"
      // Authenticate as proxyuser:proxypass.
      "Proxy-Authorization: Basic cHJveHl1c2VyOnByb3h5cGFzcw==\r\n\r\n");
  mock_reads6.emplace_back("HTTP/1.1 200 Connection Established\r\n\r\n");
  SSLSocketDataProvider ssl_origin6(ASYNC, OK);
  ssl_origin6.expected_send_client_cert = true;
  ssl_origin6.expected_client_cert = identity_origin->certificate();
  mock_writes6.emplace_back(
      "GET / HTTP/1.1\r\n"
      "Host: www.example.org\r\n"
      "Connection: keep-alive\r\n"
      // Authenticate as user:pass.
      "Authorization: Basic dXNlcjpwYXNz\r\n\r\n");
  mock_reads6.emplace_back(
      "HTTP/1.1 200 OK\r\n"
      "Connection: close\r\n"
      "Content-Length: 0\r\n\r\n");
  StaticSocketDataProvider data6(mock_reads6, mock_writes6);
  session_deps_.socket_factory->AddSocketDataProvider(&data6);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl_proxy6);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl_origin6);

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));

  // Start the request.
  TestCompletionCallback callback;
  auto trans =
      std::make_unique<HttpNetworkTransaction>(DEFAULT_PRIORITY, session.get());
  int rv = callback.GetResult(
      trans->Start(&request, callback.callback(), NetLogWithSource()));

  // Handle the proxy client certificate challenge.
  ASSERT_THAT(rv, IsError(ERR_SSL_CLIENT_AUTH_CERT_NEEDED));
  SSLCertRequestInfo* cert_request_info =
      trans->GetResponseInfo()->cert_request_info.get();
  ASSERT_TRUE(cert_request_info);
  EXPECT_TRUE(cert_request_info->is_proxy);
  EXPECT_EQ(cert_request_info->host_and_port,
            cert_request_info_proxy->host_and_port);
  rv = callback.GetResult(trans->RestartWithCertificate(
      identity_proxy->certificate(), identity_proxy->ssl_private_key(),
      callback.callback()));

  // Handle the proxy HTTP auth challenge.
  ASSERT_THAT(rv, IsOk());
  EXPECT_EQ(407, trans->GetResponseInfo()->headers->response_code());
  EXPECT_TRUE(
      CheckBasicSecureProxyAuth(trans->GetResponseInfo()->auth_challenge));
  rv = callback.GetResult(trans->RestartWithAuth(
      AuthCredentials(u"proxyuser", u"proxypass"), callback.callback()));

  // Handle the origin client certificate challenge.
  ASSERT_THAT(rv, IsError(ERR_SSL_CLIENT_AUTH_CERT_NEEDED));
  cert_request_info = trans->GetResponseInfo()->cert_request_info.get();
  ASSERT_TRUE(cert_request_info);
  EXPECT_FALSE(cert_request_info->is_proxy);
  EXPECT_EQ(cert_request_info->host_and_port,
            cert_request_info_origin->host_and_port);
  rv = callback.GetResult(trans->RestartWithCertificate(
      identity_origin->certificate(), identity_origin->ssl_private_key(),
      callback.callback()));

  // Handle the origin HTTP auth challenge.
  ASSERT_THAT(rv, IsOk());
  EXPECT_EQ(401, trans->GetResponseInfo()->headers->response_code());
  EXPECT_TRUE(
      CheckBasicSecureServerAuth(trans->GetResponseInfo()->auth_challenge));
  rv = callback.GetResult(trans->RestartWithAuth(
      AuthCredentials(u"user", u"pass"), callback.callback()));

  // The request completes.
  ASSERT_THAT(rv, IsOk());
  EXPECT_EQ(200, trans->GetResponseInfo()->headers->response_code());

  // Make a second request. This time all credentials are cached.
  trans =
      std::make_unique<HttpNetworkTransaction>(DEFAULT_PRIORITY, session.get());
  ASSERT_THAT(callback.GetResult(trans->Start(&request, callback.callback(),
                                              NetLogWithSource())),
              IsOk());
  EXPECT_EQ(200, trans->GetResponseInfo()->headers->response_code());
}

// Test the proxy requesting HTTP auth and the server requesting TLS client
// certificates. This is a regression test for https://crbug.com/946406.
TEST_F(HttpNetworkTransactionTest, ProxyHTTPAndServerTLSAuth) {
  // Note these hosts must match the CheckBasic*Auth() functions.
  session_deps_.proxy_resolution_service =
      ConfiguredProxyResolutionService::CreateFixedForTest(
          "https://myproxy:70", TRAFFIC_ANNOTATION_FOR_TESTS);

  auto cert_request_info_origin = base::MakeRefCounted<SSLCertRequestInfo>();
  cert_request_info_origin->host_and_port =
      HostPortPair("www.example.org", 443);

  std::unique_ptr<FakeClientCertIdentity> identity_origin =
      FakeClientCertIdentity::CreateFromCertAndKeyFiles(
          GetTestCertsDirectory(), "client_2.pem", "client_2.pk8");
  ASSERT_TRUE(identity_origin);

  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("https://www.example.org/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  // The client connects to the proxy. The handshake succeeds.
  SSLSocketDataProvider ssl_proxy1(ASYNC, OK);
  // The client attempts an HTTP CONNECT, but the proxy requests basic auth.
  std::vector<MockWrite> mock_writes1;
  std::vector<MockRead> mock_reads1;
  mock_writes1.emplace_back(
      "CONNECT www.example.org:443 HTTP/1.1\r\n"
      "Host: www.example.org:443\r\n"
      "Proxy-Connection: keep-alive\r\n\r\n");
  mock_reads1.emplace_back(
      "HTTP/1.1 407 Proxy Authentication Required\r\n"
      "Content-Length: 0\r\n"
      "Proxy-Authenticate: Basic realm=\"MyRealm1\"\r\n\r\n");
  // The client retries with credentials, and the request finally succeeds.
  mock_writes1.emplace_back(
      "CONNECT www.example.org:443 HTTP/1.1\r\n"
      "Host: www.example.org:443\r\n"
      "Proxy-Connection: keep-alive\r\n"
      // Authenticate as proxyuser:proxypass.
      "Proxy-Authorization: Basic cHJveHl1c2VyOnByb3h5cGFzcw==\r\n\r\n");
  mock_reads1.emplace_back("HTTP/1.1 200 Connection Established\r\n\r\n");
  // The origin requests client certificates.
  SSLSocketDataProvider ssl_origin1(ASYNC, ERR_SSL_CLIENT_AUTH_CERT_NEEDED);
  ssl_origin1.cert_request_info = cert_request_info_origin.get();
  StaticSocketDataProvider data1(mock_reads1, mock_writes1);
  session_deps_.socket_factory->AddSocketDataProvider(&data1);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl_proxy1);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl_origin1);

  // The client responds to the origin client certificate request on a new
  // connection.
  SSLSocketDataProvider ssl_proxy2(ASYNC, OK);
  std::vector<MockWrite> mock_writes2;
  std::vector<MockRead> mock_reads2;
  mock_writes2.emplace_back(
      "CONNECT www.example.org:443 HTTP/1.1\r\n"
      "Host: www.example.org:443\r\n"
      "Proxy-Connection: keep-alive\r\n"
      // Authenticate as proxyuser:proxypass.
      "Proxy-Authorization: Basic cHJveHl1c2VyOnByb3h5cGFzcw==\r\n\r\n");
  mock_reads2.emplace_back("HTTP/1.1 200 Connection Established\r\n\r\n");
  SSLSocketDataProvider ssl_origin2(ASYNC, OK);
  ssl_origin2.expected_send_client_cert = true;
  ssl_origin2.expected_client_cert = identity_origin->certificate();
  // The client sends the origin HTTP request, which succeeds.
  mock_writes2.emplace_back(
      "GET / HTTP/1.1\r\n"
      "Host: www.example.org\r\n"
      "Connection: keep-alive\r\n\r\n");
  mock_reads2.emplace_back(
      "HTTP/1.1 200 OK\r\n"
      "Content-Length: 0\r\n\r\n");
  StaticSocketDataProvider data2(mock_reads2, mock_writes2);
  session_deps_.socket_factory->AddSocketDataProvider(&data2);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl_proxy2);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl_origin2);

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));

  // Start the request.
  TestCompletionCallback callback;
  auto trans =
      std::make_unique<HttpNetworkTransaction>(DEFAULT_PRIORITY, session.get());
  int rv = callback.GetResult(
      trans->Start(&request, callback.callback(), NetLogWithSource()));

  // Handle the proxy HTTP auth challenge.
  ASSERT_THAT(rv, IsOk());
  EXPECT_EQ(407, trans->GetResponseInfo()->headers->response_code());
  EXPECT_TRUE(
      CheckBasicSecureProxyAuth(trans->GetResponseInfo()->auth_challenge));
  rv = callback.GetResult(trans->RestartWithAuth(
      AuthCredentials(u"proxyuser", u"proxypass"), callback.callback()));

  // Handle the origin client certificate challenge.
  ASSERT_THAT(rv, IsError(ERR_SSL_CLIENT_AUTH_CERT_NEEDED));
  SSLCertRequestInfo* cert_request_info =
      trans->GetResponseInfo()->cert_request_info.get();
  ASSERT_TRUE(cert_request_info);
  EXPECT_FALSE(cert_request_info->is_proxy);
  EXPECT_EQ(cert_request_info->host_and_port,
            cert_request_info_origin->host_and_port);
  rv = callback.GetResult(trans->RestartWithCertificate(
      identity_origin->certificate(), identity_origin->ssl_private_key(),
      callback.callback()));

  // The request completes.
  ASSERT_THAT(rv, IsOk());
  EXPECT_EQ(200, trans->GetResponseInfo()->headers->response_code());
}

// Test that socket reuse works with client certificates.
TEST_F(HttpNetworkTransactionTest, ClientCertSocketReuse) {
  auto cert_request_info = base::MakeRefCounted<SSLCertRequestInfo>();
  cert_request_info->host_and_port = HostPortPair("www.example.org", 443);

  std::unique_ptr<FakeClientCertIdentity> identity =
      FakeClientCertIdentity::CreateFromCertAndKeyFiles(
          GetTestCertsDirectory(), "client_1.pem", "client_1.pk8");
  ASSERT_TRUE(identity);

  HttpRequestInfo request1;
  request1.method = "GET";
  request1.url = GURL("https://www.example.org/a");
  request1.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  HttpRequestInfo request2;
  request2.method = "GET";
  request2.url = GURL("https://www.example.org/b");
  request2.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  // The first connection results in a client certificate request.
  StaticSocketDataProvider data1;
  session_deps_.socket_factory->AddSocketDataProvider(&data1);
  SSLSocketDataProvider ssl1(ASYNC, ERR_SSL_CLIENT_AUTH_CERT_NEEDED);
  ssl1.cert_request_info = cert_request_info.get();
  ssl1.expected_send_client_cert = false;
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl1);

  // The second connection succeeds and is usable for both requests.
  MockWrite mock_writes[] = {
      MockWrite("GET /a HTTP/1.1\r\n"
                "Host: www.example.org\r\n"
                "Connection: keep-alive\r\n\r\n"),
      MockWrite("GET /b HTTP/1.1\r\n"
                "Host: www.example.org\r\n"
                "Connection: keep-alive\r\n\r\n"),
  };
  MockRead mock_reads[] = {
      MockRead("HTTP/1.1 200 OK\r\n"
               "Content-Length: 0\r\n\r\n"),
      MockRead("HTTP/1.1 200 OK\r\n"
               "Content-Length: 0\r\n\r\n"),
  };
  StaticSocketDataProvider data2(mock_reads, mock_writes);
  session_deps_.socket_factory->AddSocketDataProvider(&data2);
  SSLSocketDataProvider ssl2(ASYNC, OK);
  ssl2.expected_send_client_cert = true;
  ssl2.expected_client_cert = identity->certificate();
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl2);

  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));

  // Start the first request. It succeeds after providing client certificates.
  TestCompletionCallback callback;
  auto trans =
      std::make_unique<HttpNetworkTransaction>(DEFAULT_PRIORITY, session.get());
  ASSERT_THAT(callback.GetResult(trans->Start(&request1, callback.callback(),
                                              NetLogWithSource())),
              IsError(ERR_SSL_CLIENT_AUTH_CERT_NEEDED));

  SSLCertRequestInfo* info = trans->GetResponseInfo()->cert_request_info.get();
  ASSERT_TRUE(info);
  EXPECT_FALSE(info->is_proxy);
  EXPECT_EQ(info->host_and_port, cert_request_info->host_and_port);

  ASSERT_THAT(callback.GetResult(trans->RestartWithCertificate(
                  identity->certificate(), identity->ssl_private_key(),
                  callback.callback())),
              IsOk());
  EXPECT_EQ(200, trans->GetResponseInfo()->headers->response_code());

  // Make the second request. It completes without requesting client
  // certificates.
  trans =
      std::make_unique<HttpNetworkTransaction>(DEFAULT_PRIORITY, session.get());
  ASSERT_THAT(callback.GetResult(trans->Start(&request2, callback.callback(),
                                              NetLogWithSource())),
              IsOk());
  EXPECT_EQ(200, trans->GetResponseInfo()->headers->response_code());
}

// Test for kPartitionConnectionsByNetworkIsolationKey. Runs 3 requests in
// sequence with two different NetworkIsolationKeys, the first and last have the
// same key, the second a different one. Checks that the requests are
// partitioned across sockets as expected.
TEST_F(HttpNetworkTransactionTest, NetworkIsolation) {
  const SchemefulSite kSite1(GURL("http://origin1/"));
  const SchemefulSite kSite2(GURL("http://origin2/"));
  NetworkIsolationKey network_isolation_key1(kSite1, kSite1);
  NetworkIsolationKey network_isolation_key2(kSite2, kSite2);

  for (bool partition_connections : {false, true}) {
    SCOPED_TRACE(partition_connections);

    base::test::ScopedFeatureList feature_list;
    if (partition_connections) {
      feature_list.InitAndEnableFeature(
          features::kPartitionConnectionsByNetworkIsolationKey);
    } else {
      feature_list.InitAndDisableFeature(
          features::kPartitionConnectionsByNetworkIsolationKey);
    }

    std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));

    // Reads and writes for the unpartitioned case, where only one socket is
    // used.

    const MockWrite kUnpartitionedWrites[] = {
        MockWrite("GET /1 HTTP/1.1\r\n"
                  "Host: foo.test\r\n"
                  "Connection: keep-alive\r\n\r\n"),
        MockWrite("GET /2 HTTP/1.1\r\n"
                  "Host: foo.test\r\n"
                  "Connection: keep-alive\r\n\r\n"),
        MockWrite("GET /3 HTTP/1.1\r\n"
                  "Host: foo.test\r\n"
                  "Connection: keep-alive\r\n\r\n"),
    };

    const MockRead kUnpartitionedReads[] = {
        MockRead("HTTP/1.1 200 OK\r\n"
                 "Connection: keep-alive\r\n"
                 "Content-Length: 1\r\n\r\n"
                 "1"),
        MockRead("HTTP/1.1 200 OK\r\n"
                 "Connection: keep-alive\r\n"
                 "Content-Length: 1\r\n\r\n"
                 "2"),
        MockRead("HTTP/1.1 200 OK\r\n"
                 "Connection: keep-alive\r\n"
                 "Content-Length: 1\r\n\r\n"
                 "3"),
    };

    StaticSocketDataProvider unpartitioned_data(kUnpartitionedReads,
                                                kUnpartitionedWrites);

    // Reads and writes for the partitioned case, where two sockets are used.

    const MockWrite kPartitionedWrites1[] = {
        MockWrite("GET /1 HTTP/1.1\r\n"
                  "Host: foo.test\r\n"
                  "Connection: keep-alive\r\n\r\n"),
        MockWrite("GET /3 HTTP/1.1\r\n"
                  "Host: foo.test\r\n"
                  "Connection: keep-alive\r\n\r\n"),
    };

    const MockRead kPartitionedReads1[] = {
        MockRead("HTTP/1.1 200 OK\r\n"
                 "Connection: keep-alive\r\n"
                 "Content-Length: 1\r\n\r\n"
                 "1"),
        MockRead("HTTP/1.1 200 OK\r\n"
                 "Connection: keep-alive\r\n"
                 "Content-Length: 1\r\n\r\n"
                 "3"),
    };

    const MockWrite kPartitionedWrites2[] = {
        MockWrite("GET /2 HTTP/1.1\r\n"
                  "Host: foo.test\r\n"
                  "Connection: keep-alive\r\n\r\n"),
    };

    const MockRead kPartitionedReads2[] = {
        MockRead("HTTP/1.1 200 OK\r\n"
                 "Connection: keep-alive\r\n"
                 "Content-Length: 1\r\n\r\n"
                 "2"),
    };

    StaticSocketDataProvider partitioned_data1(kPartitionedReads1,
                                               kPartitionedWrites1);
    StaticSocketDataProvider partitioned_data2(kPartitionedReads2,
                                               kPartitionedWrites2);

    if (partition_connections) {
      session_deps_.socket_factory->AddSocketDataProvider(&partitioned_data1);
      session_deps_.socket_factory->AddSocketDataProvider(&partitioned_data2);
    } else {
      session_deps_.socket_factory->AddSocketDataProvider(&unpartitioned_data);
    }

    TestCompletionCallback callback;
    HttpRequestInfo request1;
    request1.method = "GET";
    request1.url = GURL("http://foo.test/1");
    request1.traffic_annotation =
        net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);
    request1.network_isolation_key = network_isolation_key1;
    auto trans1 = std::make_unique<HttpNetworkTransaction>(DEFAULT_PRIORITY,
                                                           session.get());
    int rv = trans1->Start(&request1, callback.callback(), NetLogWithSource());
    EXPECT_THAT(callback.GetResult(rv), IsOk());
    std::string response_data1;
    EXPECT_THAT(ReadTransaction(trans1.get(), &response_data1), IsOk());
    EXPECT_EQ("1", response_data1);
    trans1.reset();

    HttpRequestInfo request2;
    request2.method = "GET";
    request2.url = GURL("http://foo.test/2");
    request2.traffic_annotation =
        net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);
    request2.network_isolation_key = network_isolation_key2;
    auto trans2 = std::make_unique<HttpNetworkTransaction>(DEFAULT_PRIORITY,
                                                           session.get());
    rv = trans2->Start(&request2, callback.callback(), NetLogWithSource());
    EXPECT_THAT(callback.GetResult(rv), IsOk());
    std::string response_data2;
    EXPECT_THAT(ReadTransaction(trans2.get(), &response_data2), IsOk());
    EXPECT_EQ("2", response_data2);
    trans2.reset();

    HttpRequestInfo request3;
    request3.method = "GET";
    request3.url = GURL("http://foo.test/3");
    request3.traffic_annotation =
        net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);
    request3.network_isolation_key = network_isolation_key1;
    auto trans3 = std::make_unique<HttpNetworkTransaction>(DEFAULT_PRIORITY,
                                                           session.get());
    rv = trans3->Start(&request3, callback.callback(), NetLogWithSource());
    EXPECT_THAT(callback.GetResult(rv), IsOk());
    std::string response_data3;
    EXPECT_THAT(ReadTransaction(trans3.get(), &response_data3), IsOk());
    EXPECT_EQ("3", response_data3);
    trans3.reset();
  }
}

TEST_F(HttpNetworkTransactionTest, NetworkIsolationH2) {
  const SchemefulSite kSite1(GURL("http://origin1/"));
  const SchemefulSite kSite2(GURL("http://origin2/"));
  NetworkIsolationKey network_isolation_key1(kSite1, kSite1);
  NetworkIsolationKey network_isolation_key2(kSite2, kSite2);

  // Whether to use an H2 proxy. When false, uses HTTPS H2 requests without a
  // proxy, when true, uses HTTP requests over an H2 proxy. It's unnecessary to
  // test tunneled HTTPS over an H2 proxy, since that path sets up H2 sessions
  // the same way as the HTTP over H2 proxy case.
  for (bool use_proxy : {false, true}) {
    SCOPED_TRACE(use_proxy);
    if (use_proxy) {
      session_deps_.proxy_resolution_service =
          ConfiguredProxyResolutionService::CreateFixedFromPacResultForTest(
              "HTTPS proxy:443", TRAFFIC_ANNOTATION_FOR_TESTS);
    } else {
      session_deps_.proxy_resolution_service =
          ConfiguredProxyResolutionService::CreateDirect();
    }
    const char* url1 = nullptr;
    const char* url2 = nullptr;
    const char* url3 = nullptr;
    if (use_proxy) {
      url1 = "http://foo.test/1";
      url2 = "http://foo.test/2";
      url3 = "http://foo.test/3";
    } else {
      url1 = "https://foo.test/1";
      url2 = "https://foo.test/2";
      url3 = "https://foo.test/3";
    }

    for (bool partition_connections : {false, true}) {
      SCOPED_TRACE(partition_connections);

      base::test::ScopedFeatureList feature_list;
      if (partition_connections) {
        feature_list.InitAndEnableFeature(
            features::kPartitionConnectionsByNetworkIsolationKey);
      } else {
        feature_list.InitAndDisableFeature(
            features::kPartitionConnectionsByNetworkIsolationKey);
      }

      std::unique_ptr<HttpNetworkSession> session(
          CreateSession(&session_deps_));

      // Reads and writes for the unpartitioned case, where only one socket is
      // used.

      SpdyTestUtil spdy_util;
      spdy::SpdySerializedFrame unpartitioned_req1(
          spdy_util.ConstructSpdyGet(url1, 1, LOWEST));
      spdy::SpdySerializedFrame unpartitioned_response1(
          spdy_util.ConstructSpdyGetReply(nullptr, 0, 1));
      spdy::SpdySerializedFrame unpartitioned_body1(
          spdy_util.ConstructSpdyDataFrame(1, "1", true));
      spdy_util.UpdateWithStreamDestruction(1);

      spdy::SpdySerializedFrame unpartitioned_req2(
          spdy_util.ConstructSpdyGet(url2, 3, LOWEST));
      spdy::SpdySerializedFrame unpartitioned_response2(
          spdy_util.ConstructSpdyGetReply(nullptr, 0, 3));
      spdy::SpdySerializedFrame unpartitioned_body2(
          spdy_util.ConstructSpdyDataFrame(3, "2", true));
      spdy_util.UpdateWithStreamDestruction(3);

      spdy::SpdySerializedFrame unpartitioned_req3(
          spdy_util.ConstructSpdyGet(url3, 5, LOWEST));
      spdy::SpdySerializedFrame unpartitioned_response3(
          spdy_util.ConstructSpdyGetReply(nullptr, 0, 5));
      spdy::SpdySerializedFrame unpartitioned_body3(
          spdy_util.ConstructSpdyDataFrame(5, "3", true));

      const MockWrite kUnpartitionedWrites[] = {
          CreateMockWrite(unpartitioned_req1, 0),
          CreateMockWrite(unpartitioned_req2, 3),
          CreateMockWrite(unpartitioned_req3, 6),
      };

      const MockRead kUnpartitionedReads[] = {
          CreateMockRead(unpartitioned_response1, 1),
          CreateMockRead(unpartitioned_body1, 2),
          CreateMockRead(unpartitioned_response2, 4),
          CreateMockRead(unpartitioned_body2, 5),
          CreateMockRead(unpartitioned_response3, 7),
          CreateMockRead(unpartitioned_body3, 8),
          MockRead(SYNCHRONOUS, ERR_IO_PENDING, 9),
      };

      SequencedSocketData unpartitioned_data(kUnpartitionedReads,
                                             kUnpartitionedWrites);

      // Reads and writes for the partitioned case, where two sockets are used.

      SpdyTestUtil spdy_util2;
      spdy::SpdySerializedFrame partitioned_req1(
          spdy_util2.ConstructSpdyGet(url1, 1, LOWEST));
      spdy::SpdySerializedFrame partitioned_response1(
          spdy_util2.ConstructSpdyGetReply(nullptr, 0, 1));
      spdy::SpdySerializedFrame partitioned_body1(
          spdy_util2.ConstructSpdyDataFrame(1, "1", true));
      spdy_util2.UpdateWithStreamDestruction(1);

      spdy::SpdySerializedFrame partitioned_req3(
          spdy_util2.ConstructSpdyGet(url3, 3, LOWEST));
      spdy::SpdySerializedFrame partitioned_response3(
          spdy_util2.ConstructSpdyGetReply(nullptr, 0, 3));
      spdy::SpdySerializedFrame partitioned_body3(
          spdy_util2.ConstructSpdyDataFrame(3, "3", true));

      const MockWrite kPartitionedWrites1[] = {
          CreateMockWrite(partitioned_req1, 0),
          CreateMockWrite(partitioned_req3, 3),
      };

      const MockRead kPartitionedReads1[] = {
          CreateMockRead(partitioned_response1, 1),
          CreateMockRead(partitioned_body1, 2),
          CreateMockRead(partitioned_response3, 4),
          CreateMockRead(partitioned_body3, 5),
          MockRead(SYNCHRONOUS, ERR_IO_PENDING, 6),
      };

      SpdyTestUtil spdy_util3;
      spdy::SpdySerializedFrame partitioned_req2(
          spdy_util3.ConstructSpdyGet(url2, 1, LOWEST));
      spdy::SpdySerializedFrame partitioned_response2(
          spdy_util3.ConstructSpdyGetReply(nullptr, 0, 1));
      spdy::SpdySerializedFrame partitioned_body2(
          spdy_util3.ConstructSpdyDataFrame(1, "2", true));

      const MockWrite kPartitionedWrites2[] = {
          CreateMockWrite(partitioned_req2, 0),
      };

      const MockRead kPartitionedReads2[] = {
          CreateMockRead(partitioned_response2, 1),
          CreateMockRead(partitioned_body2, 2),
          MockRead(SYNCHRONOUS, ERR_IO_PENDING, 3),
      };

      SequencedSocketData partitioned_data1(kPartitionedReads1,
                                            kPartitionedWrites1);
      SequencedSocketData partitioned_data2(kPartitionedReads2,
                                            kPartitionedWrites2);

      // No need to segment SSLDataProviders by whether or not partitioning is
      // enabled.
      SSLSocketDataProvider ssl_data1(ASYNC, OK);
      ssl_data1.next_proto = kProtoHTTP2;
      SSLSocketDataProvider ssl_data2(ASYNC, OK);
      ssl_data2.next_proto = kProtoHTTP2;

      if (partition_connections) {
        session_deps_.socket_factory->AddSocketDataProvider(&partitioned_data1);
        session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl_data1);
        session_deps_.socket_factory->AddSocketDataProvider(&partitioned_data2);
        session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl_data2);
      } else {
        session_deps_.socket_factory->AddSocketDataProvider(
            &unpartitioned_data);
        session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl_data1);
      }

      TestCompletionCallback callback;
      HttpRequestInfo request1;
      request1.method = "GET";
      request1.url = GURL(url1);
      request1.traffic_annotation =
          net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);
      request1.network_isolation_key = network_isolation_key1;
      auto trans1 =
          std::make_unique<HttpNetworkTransaction>(LOWEST, session.get());
      int rv =
          trans1->Start(&request1, callback.callback(), NetLogWithSource());
      EXPECT_THAT(callback.GetResult(rv), IsOk());
      std::string response_data1;
      EXPECT_THAT(ReadTransaction(trans1.get(), &response_data1), IsOk());
      EXPECT_EQ("1", response_data1);
      trans1.reset();

      HttpRequestInfo request2;
      request2.method = "GET";
      request2.url = GURL(url2);
      request2.traffic_annotation =
          net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);
      request2.network_isolation_key = network_isolation_key2;
      auto trans2 =
          std::make_unique<HttpNetworkTransaction>(LOWEST, session.get());
      rv = trans2->Start(&request2, callback.callback(), NetLogWithSource());
      EXPECT_THAT(callback.GetResult(rv), IsOk());
      std::string response_data2;
      EXPECT_THAT(ReadTransaction(trans2.get(), &response_data2), IsOk());
      EXPECT_EQ("2", response_data2);
      trans2.reset();

      HttpRequestInfo request3;
      request3.method = "GET";
      request3.url = GURL(url3);
      request3.traffic_annotation =
          net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);
      request3.network_isolation_key = network_isolation_key1;
      auto trans3 =
          std::make_unique<HttpNetworkTransaction>(LOWEST, session.get());
      rv = trans3->Start(&request3, callback.callback(), NetLogWithSource());
      EXPECT_THAT(callback.GetResult(rv), IsOk());
      std::string response_data3;
      EXPECT_THAT(ReadTransaction(trans3.get(), &response_data3), IsOk());
      EXPECT_EQ("3", response_data3);
      trans3.reset();
    }
  }
}

// Preconnect two sockets with different NetworkIsolationKeys when
// features::kPartitionConnectionsByNetworkIsolationKey is enabled. Then issue a
// request and make sure the correct socket is used. Loops three times,
// expecting to use the first preconnect, second preconnect, and neither.
TEST_F(HttpNetworkTransactionTest, NetworkIsolationPreconnect) {
  base::test::ScopedFeatureList feature_list;
  feature_list.InitAndEnableFeature(
      features::kPartitionConnectionsByNetworkIsolationKey);

  enum class TestCase {
    kUseFirstPreconnect,
    kUseSecondPreconnect,
    kDontUsePreconnect,
  };

  const SchemefulSite kSite1(GURL("http://origin1/"));
  const SchemefulSite kSite2(GURL("http://origin2/"));
  const SchemefulSite kSite3(GURL("http://origin3/"));
  NetworkIsolationKey preconnect1_isolation_key(kSite1, kSite1);
  NetworkIsolationKey preconnect2_isolation_key(kSite2, kSite2);
  NetworkIsolationKey not_preconnected_isolation_key(kSite3, kSite3);

  // Test that only preconnects with
  for (TestCase test_case :
       {TestCase::kUseFirstPreconnect, TestCase::kUseSecondPreconnect,
        TestCase::kDontUsePreconnect}) {
    SpdySessionDependencies session_deps;
    // Make DNS lookups completely synchronously, so preconnects complete
    // immediately.
    session_deps.host_resolver->set_synchronous_mode(true);

    const MockWrite kMockWrites[] = {
        MockWrite(ASYNC, 0,
                  "GET / HTTP/1.1\r\n"
                  "Host: www.foo.com\r\n"
                  "Connection: keep-alive\r\n\r\n"),
    };

    const MockRead kMockReads[] = {
        MockRead(ASYNC, 1,
                 "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\n"
                 "hello"),
    };

    // Used for the socket that will actually be used, which may or may not be
    // one of the preconnects
    SequencedSocketData used_socket_data(MockConnect(SYNCHRONOUS, OK),
                                         kMockReads, kMockWrites);

    // Used for the preconnects that won't actually be used.
    SequencedSocketData preconnect1_data(MockConnect(SYNCHRONOUS, OK),
                                         base::span<const MockRead>(),
                                         base::span<const MockWrite>());
    SequencedSocketData preconnect2_data(MockConnect(SYNCHRONOUS, OK),
                                         base::span<const MockRead>(),
                                         base::span<const MockWrite>());

    NetworkIsolationKey network_isolation_key_for_request;

    switch (test_case) {
      case TestCase::kUseFirstPreconnect:
        session_deps.socket_factory->AddSocketDataProvider(&used_socket_data);
        session_deps.socket_factory->AddSocketDataProvider(&preconnect2_data);
        network_isolation_key_for_request = preconnect1_isolation_key;
        break;
      case TestCase::kUseSecondPreconnect:
        session_deps.socket_factory->AddSocketDataProvider(&preconnect1_data);
        session_deps.socket_factory->AddSocketDataProvider(&used_socket_data);
        network_isolation_key_for_request = preconnect2_isolation_key;
        break;
      case TestCase::kDontUsePreconnect:
        session_deps.socket_factory->AddSocketDataProvider(&preconnect1_data);
        session_deps.socket_factory->AddSocketDataProvider(&preconnect2_data);
        session_deps.socket_factory->AddSocketDataProvider(&used_socket_data);
        network_isolation_key_for_request = not_preconnected_isolation_key;
        break;
    }

    std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps));

    // Preconnect sockets.
    HttpRequestInfo request;
    request.method = "GET";
    request.url = GURL("http://www.foo.com/");
    request.traffic_annotation =
        net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

    request.network_isolation_key = preconnect1_isolation_key;
    session->http_stream_factory()->PreconnectStreams(1, request);

    request.network_isolation_key = preconnect2_isolation_key;
    session->http_stream_factory()->PreconnectStreams(1, request);

    request.network_isolation_key = network_isolation_key_for_request;

    EXPECT_EQ(2, GetIdleSocketCountInTransportSocketPool(session.get()));

    // Make the request.
    TestCompletionCallback callback;

    HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

    int rv = trans.Start(&request, callback.callback(), NetLogWithSource());
    EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

    rv = callback.WaitForResult();
    EXPECT_THAT(rv, IsOk());

    const HttpResponseInfo* response = trans.GetResponseInfo();
    ASSERT_TRUE(response);
    ASSERT_TRUE(response->headers);
    EXPECT_EQ(200, response->headers->response_code());

    std::string response_data;
    rv = ReadTransaction(&trans, &response_data);
    EXPECT_THAT(rv, IsOk());
    EXPECT_EQ("hello", response_data);

    if (test_case != TestCase::kDontUsePreconnect) {
      EXPECT_EQ(2, GetIdleSocketCountInTransportSocketPool(session.get()));
    } else {
      EXPECT_EQ(3, GetIdleSocketCountInTransportSocketPool(session.get()));
    }
  }
}

// Test that the NetworkIsolationKey is passed down to SSLConfig so the session
// cache is isolated.
TEST_F(HttpNetworkTransactionTest, NetworkIsolationSSL) {
  base::test::ScopedFeatureList feature_list;
  feature_list.InitWithFeatures(
      {features::kPartitionConnectionsByNetworkIsolationKey,
       features::kPartitionSSLSessionsByNetworkIsolationKey},
      {});

  const SchemefulSite kSite1(GURL("http://origin1/"));
  const SchemefulSite kSite2(GURL("http://origin2/"));
  const NetworkIsolationKey kNetworkIsolationKey1(kSite1, kSite1);
  const NetworkIsolationKey kNetworkIsolationKey2(kSite2, kSite2);
  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));

  // The server always sends Connection: close, so each request goes over a
  // distinct socket.

  const MockWrite kWrites1[] = {
      MockWrite("GET /1 HTTP/1.1\r\n"
                "Host: foo.test\r\n"
                "Connection: keep-alive\r\n\r\n")};

  const MockRead kReads1[] = {
      MockRead("HTTP/1.1 200 OK\r\n"
               "Connection: close\r\n"
               "Content-Length: 1\r\n\r\n"
               "1")};

  const MockWrite kWrites2[] = {
      MockWrite("GET /2 HTTP/1.1\r\n"
                "Host: foo.test\r\n"
                "Connection: keep-alive\r\n\r\n")};

  const MockRead kReads2[] = {
      MockRead("HTTP/1.1 200 OK\r\n"
               "Connection: close\r\n"
               "Content-Length: 1\r\n\r\n"
               "2")};

  const MockWrite kWrites3[] = {
      MockWrite("GET /3 HTTP/1.1\r\n"
                "Host: foo.test\r\n"
                "Connection: keep-alive\r\n\r\n")};

  const MockRead kReads3[] = {
      MockRead("HTTP/1.1 200 OK\r\n"
               "Connection: close\r\n"
               "Content-Length: 1\r\n\r\n"
               "3")};

  StaticSocketDataProvider data1(kReads1, kWrites1);
  StaticSocketDataProvider data2(kReads2, kWrites2);
  StaticSocketDataProvider data3(kReads3, kWrites3);
  session_deps_.socket_factory->AddSocketDataProvider(&data1);
  session_deps_.socket_factory->AddSocketDataProvider(&data2);
  session_deps_.socket_factory->AddSocketDataProvider(&data3);

  SSLSocketDataProvider ssl_data1(ASYNC, OK);
  ssl_data1.expected_host_and_port = HostPortPair("foo.test", 443);
  ssl_data1.expected_network_isolation_key = kNetworkIsolationKey1;
  SSLSocketDataProvider ssl_data2(ASYNC, OK);
  ssl_data2.expected_host_and_port = HostPortPair("foo.test", 443);
  ssl_data2.expected_network_isolation_key = kNetworkIsolationKey2;
  SSLSocketDataProvider ssl_data3(ASYNC, OK);
  ssl_data3.expected_host_and_port = HostPortPair("foo.test", 443);
  ssl_data3.expected_network_isolation_key = kNetworkIsolationKey1;
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl_data1);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl_data2);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl_data3);

  TestCompletionCallback callback;
  HttpRequestInfo request1;
  request1.method = "GET";
  request1.url = GURL("https://foo.test/1");
  request1.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);
  request1.network_isolation_key = kNetworkIsolationKey1;
  auto trans1 =
      std::make_unique<HttpNetworkTransaction>(DEFAULT_PRIORITY, session.get());
  int rv = trans1->Start(&request1, callback.callback(), NetLogWithSource());
  EXPECT_THAT(callback.GetResult(rv), IsOk());
  std::string response_data1;
  EXPECT_THAT(ReadTransaction(trans1.get(), &response_data1), IsOk());
  EXPECT_EQ("1", response_data1);
  trans1.reset();

  HttpRequestInfo request2;
  request2.method = "GET";
  request2.url = GURL("https://foo.test/2");
  request2.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);
  request2.network_isolation_key = kNetworkIsolationKey2;
  auto trans2 =
      std::make_unique<HttpNetworkTransaction>(DEFAULT_PRIORITY, session.get());
  rv = trans2->Start(&request2, callback.callback(), NetLogWithSource());
  EXPECT_THAT(callback.GetResult(rv), IsOk());
  std::string response_data2;
  EXPECT_THAT(ReadTransaction(trans2.get(), &response_data2), IsOk());
  EXPECT_EQ("2", response_data2);
  trans2.reset();

  HttpRequestInfo request3;
  request3.method = "GET";
  request3.url = GURL("https://foo.test/3");
  request3.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);
  request3.network_isolation_key = kNetworkIsolationKey1;
  auto trans3 =
      std::make_unique<HttpNetworkTransaction>(DEFAULT_PRIORITY, session.get());
  rv = trans3->Start(&request3, callback.callback(), NetLogWithSource());
  EXPECT_THAT(callback.GetResult(rv), IsOk());
  std::string response_data3;
  EXPECT_THAT(ReadTransaction(trans3.get(), &response_data3), IsOk());
  EXPECT_EQ("3", response_data3);
  trans3.reset();
}

// Test that the NetworkIsolationKey is passed down to SSLConfig so the session
// cache is isolated, for both origins and proxies.
TEST_F(HttpNetworkTransactionTest, NetworkIsolationSSLProxy) {
  base::test::ScopedFeatureList feature_list;
  feature_list.InitWithFeatures(
      {features::kPartitionConnectionsByNetworkIsolationKey,
       features::kPartitionSSLSessionsByNetworkIsolationKey},
      {});

  session_deps_.proxy_resolution_service =
      ConfiguredProxyResolutionService::CreateFixedForTest(
          "https://myproxy:70", TRAFFIC_ANNOTATION_FOR_TESTS);

  const SchemefulSite kSite1(GURL("http://origin1/"));
  const SchemefulSite kSite2(GURL("http://origin2/"));
  const NetworkIsolationKey kNetworkIsolationKey1(kSite1, kSite1);
  const NetworkIsolationKey kNetworkIsolationKey2(kSite2, kSite2);
  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));

  // Make both a tunneled and non-tunneled request.
  HttpRequestInfo request1;
  request1.method = "GET";
  request1.url = GURL("https://foo.test/1");
  request1.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);
  request1.network_isolation_key = kNetworkIsolationKey1;

  HttpRequestInfo request2;
  request2.method = "GET";
  request2.url = GURL("http://foo.test/2");
  request2.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);
  request2.network_isolation_key = kNetworkIsolationKey2;

  const MockWrite kWrites1[] = {
      MockWrite("CONNECT foo.test:443 HTTP/1.1\r\n"
                "Host: foo.test:443\r\n"
                "Proxy-Connection: keep-alive\r\n\r\n"),
      MockWrite("GET /1 HTTP/1.1\r\n"
                "Host: foo.test\r\n"
                "Connection: keep-alive\r\n\r\n")};

  const MockRead kReads1[] = {
      MockRead("HTTP/1.1 200 Connection Established\r\n\r\n"),
      MockRead("HTTP/1.1 200 OK\r\n"
               "Connection: close\r\n"
               "Content-Length: 1\r\n\r\n"
               "1")};

  const MockWrite kWrites2[] = {
      MockWrite("GET http://foo.test/2 HTTP/1.1\r\n"
                "Host: foo.test\r\n"
                "Proxy-Connection: keep-alive\r\n\r\n")};

  const MockRead kReads2[] = {
      MockRead("HTTP/1.1 200 OK\r\n"
               "Connection: close\r\n"
               "Content-Length: 1\r\n\r\n"
               "2")};

  StaticSocketDataProvider data1(kReads1, kWrites1);
  StaticSocketDataProvider data2(kReads2, kWrites2);
  session_deps_.socket_factory->AddSocketDataProvider(&data1);
  session_deps_.socket_factory->AddSocketDataProvider(&data2);
  session_deps_.socket_factory->AddSocketDataProvider(&data2);

  SSLSocketDataProvider ssl_proxy1(ASYNC, OK);
  ssl_proxy1.expected_host_and_port = HostPortPair("myproxy", 70);
  ssl_proxy1.expected_network_isolation_key = kNetworkIsolationKey1;
  SSLSocketDataProvider ssl_origin1(ASYNC, OK);
  ssl_origin1.expected_host_and_port = HostPortPair("foo.test", 443);
  ssl_origin1.expected_network_isolation_key = kNetworkIsolationKey1;
  SSLSocketDataProvider ssl_proxy2(ASYNC, OK);
  ssl_proxy2.expected_host_and_port = HostPortPair("myproxy", 70);
  ssl_proxy2.expected_network_isolation_key = kNetworkIsolationKey2;
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl_proxy1);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl_origin1);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl_proxy2);

  TestCompletionCallback callback;
  auto trans1 =
      std::make_unique<HttpNetworkTransaction>(DEFAULT_PRIORITY, session.get());
  int rv = trans1->Start(&request1, callback.callback(), NetLogWithSource());
  EXPECT_THAT(callback.GetResult(rv), IsOk());
  std::string response_data1;
  EXPECT_THAT(ReadTransaction(trans1.get(), &response_data1), IsOk());
  EXPECT_EQ("1", response_data1);
  trans1.reset();

  auto trans2 =
      std::make_unique<HttpNetworkTransaction>(DEFAULT_PRIORITY, session.get());
  rv = trans2->Start(&request2, callback.callback(), NetLogWithSource());
  EXPECT_THAT(callback.GetResult(rv), IsOk());
  std::string response_data2;
  EXPECT_THAT(ReadTransaction(trans2.get(), &response_data2), IsOk());
  EXPECT_EQ("2", response_data2);
  trans2.reset();
}

// Test that SSLConfig changes from SSLConfigService are picked up even when
// there are live sockets.
TEST_F(HttpNetworkTransactionTest, SSLConfigChanged) {
  SSLContextConfig ssl_context_config;
  ssl_context_config.version_max = SSL_PROTOCOL_VERSION_TLS1_3;
  auto ssl_config_service =
      std::make_unique<TestSSLConfigService>(ssl_context_config);
  TestSSLConfigService* ssl_config_service_raw = ssl_config_service.get();

  session_deps_.ssl_config_service = std::move(ssl_config_service);

  // Make three requests. Between the second and third, the SSL config will
  // change.
  HttpRequestInfo request1;
  request1.method = "GET";
  request1.url = GURL("https://foo.test/1");
  request1.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  HttpRequestInfo request2;
  request2.method = "GET";
  request2.url = GURL("https://foo.test/2");
  request2.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  HttpRequestInfo request3;
  request3.method = "GET";
  request3.url = GURL("https://foo.test/3");
  request3.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  const MockWrite kWrites1[] = {
      MockWrite("GET /1 HTTP/1.1\r\n"
                "Host: foo.test\r\n"
                "Connection: keep-alive\r\n\r\n"),
      MockWrite("GET /2 HTTP/1.1\r\n"
                "Host: foo.test\r\n"
                "Connection: keep-alive\r\n\r\n"),
  };

  const MockRead kReads1[] = {
      MockRead("HTTP/1.1 200 OK\r\n"
               "Connection: keep-alive\r\n"
               "Content-Length: 1\r\n\r\n"
               "1"),
      MockRead("HTTP/1.1 200 OK\r\n"
               "Connection: keep-alive\r\n"
               "Content-Length: 1\r\n\r\n"
               "2"),
  };

  // The third request goes on a different socket because the SSL config has
  // changed.
  const MockWrite kWrites2[] = {
      MockWrite("GET /3 HTTP/1.1\r\n"
                "Host: foo.test\r\n"
                "Connection: keep-alive\r\n\r\n")};

  const MockRead kReads2[] = {
      MockRead("HTTP/1.1 200 OK\r\n"
               "Connection: keep-alive\r\n"
               "Content-Length: 1\r\n\r\n"
               "3")};

  StaticSocketDataProvider data1(kReads1, kWrites1);
  StaticSocketDataProvider data2(kReads2, kWrites2);
  session_deps_.socket_factory->AddSocketDataProvider(&data1);
  session_deps_.socket_factory->AddSocketDataProvider(&data2);

  SSLSocketDataProvider ssl1(ASYNC, OK);
  ssl1.expected_ssl_version_max = SSL_PROTOCOL_VERSION_TLS1_3;
  SSLSocketDataProvider ssl2(ASYNC, OK);
  ssl2.expected_ssl_version_max = SSL_PROTOCOL_VERSION_TLS1_2;
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl1);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl2);

  std::unique_ptr<HttpNetworkSession> session = CreateSession(&session_deps_);

  TestCompletionCallback callback;
  auto trans1 =
      std::make_unique<HttpNetworkTransaction>(DEFAULT_PRIORITY, session.get());
  int rv = trans1->Start(&request1, callback.callback(), NetLogWithSource());
  EXPECT_THAT(callback.GetResult(rv), IsOk());
  std::string response_data1;
  EXPECT_THAT(ReadTransaction(trans1.get(), &response_data1), IsOk());
  EXPECT_EQ("1", response_data1);
  trans1.reset();

  auto trans2 =
      std::make_unique<HttpNetworkTransaction>(DEFAULT_PRIORITY, session.get());
  rv = trans2->Start(&request2, callback.callback(), NetLogWithSource());
  EXPECT_THAT(callback.GetResult(rv), IsOk());
  std::string response_data2;
  EXPECT_THAT(ReadTransaction(trans2.get(), &response_data2), IsOk());
  EXPECT_EQ("2", response_data2);
  trans2.reset();

  ssl_context_config.version_max = SSL_PROTOCOL_VERSION_TLS1_2;
  ssl_config_service_raw->UpdateSSLConfigAndNotify(ssl_context_config);

  auto trans3 =
      std::make_unique<HttpNetworkTransaction>(DEFAULT_PRIORITY, session.get());
  rv = trans3->Start(&request3, callback.callback(), NetLogWithSource());
  EXPECT_THAT(callback.GetResult(rv), IsOk());
  std::string response_data3;
  EXPECT_THAT(ReadTransaction(trans3.get(), &response_data3), IsOk());
  EXPECT_EQ("3", response_data3);
  trans3.reset();
}

TEST_F(HttpNetworkTransactionTest, SSLConfigChangedPendingConnect) {
  SSLContextConfig ssl_context_config;
  ssl_context_config.version_max = SSL_PROTOCOL_VERSION_TLS1_3;
  auto ssl_config_service =
      std::make_unique<TestSSLConfigService>(ssl_context_config);
  TestSSLConfigService* ssl_config_service_raw = ssl_config_service.get();

  session_deps_.ssl_config_service = std::move(ssl_config_service);

  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("https://foo.test/1");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  // Make a socket which never connects.
  StaticSocketDataProvider data({}, {});
  session_deps_.socket_factory->AddSocketDataProvider(&data);
  SSLSocketDataProvider ssl_data(SYNCHRONOUS, ERR_IO_PENDING);
  ssl_data.expected_ssl_version_max = SSL_PROTOCOL_VERSION_TLS1_3;
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl_data);

  std::unique_ptr<HttpNetworkSession> session = CreateSession(&session_deps_);

  TestCompletionCallback callback;
  auto trans =
      std::make_unique<HttpNetworkTransaction>(DEFAULT_PRIORITY, session.get());
  int rv = trans->Start(&request, callback.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  ssl_context_config.version_max = SSL_PROTOCOL_VERSION_TLS1_2;
  ssl_config_service_raw->UpdateSSLConfigAndNotify(ssl_context_config);

  EXPECT_THAT(callback.GetResult(rv), IsError(ERR_NETWORK_CHANGED));
}

// Test that HttpNetworkTransaction correctly handles existing sockets when the
// server requests a client certificate post-handshake (via a TLS
// renegotiation). This is a regression test for https://crbug.com/829184.
TEST_F(HttpNetworkTransactionTest, PostHandshakeClientCertWithSockets) {
  const MutableNetworkTrafficAnnotationTag kTrafficAnnotation(
      TRAFFIC_ANNOTATION_FOR_TESTS);

  auto cert_request_info = base::MakeRefCounted<SSLCertRequestInfo>();
  cert_request_info->host_and_port = HostPortPair("foo.test", 443);

  std::unique_ptr<FakeClientCertIdentity> identity =
      FakeClientCertIdentity::CreateFromCertAndKeyFiles(
          GetTestCertsDirectory(), "client_1.pem", "client_1.pk8");
  ASSERT_TRUE(identity);

  // This test will make several requests so that, when the client certificate
  // request comes in, we have a socket in use, an idle socket, and a socket for
  // an unrelated host.
  //
  // First, two long-lived requests which do not complete until after the client
  // certificate request. This arranges for sockets to be in use during the
  // request. They should not be interrupted.
  HttpRequestInfo request_long_lived;
  request_long_lived.method = "GET";
  request_long_lived.url = GURL("https://foo.test/long-lived");
  request_long_lived.traffic_annotation = kTrafficAnnotation;

  HttpRequestInfo request_long_lived_bar;
  request_long_lived_bar.method = "GET";
  request_long_lived_bar.url = GURL("https://bar.test/long-lived");
  request_long_lived_bar.traffic_annotation = kTrafficAnnotation;

  // Next, make a request that needs client certificates.
  HttpRequestInfo request_auth;
  request_auth.method = "GET";
  request_auth.url = GURL("https://foo.test/auth");
  request_auth.traffic_annotation = kTrafficAnnotation;

  // Before responding to the challenge, make a request to an unauthenticated
  // endpoint. This will result in an idle socket when the client certificate
  // challenge is resolved.
  HttpRequestInfo request_unauth;
  request_unauth.method = "GET";
  request_unauth.url = GURL("https://foo.test/unauth");
  request_unauth.traffic_annotation = kTrafficAnnotation;

  // After all the preceding requests complete, end with two additional requests
  // to ensure pre-authentication foo.test sockets are not used and bar.test
  // sockets are unaffected.
  HttpRequestInfo request_post_auth;
  request_post_auth.method = "GET";
  request_post_auth.url = GURL("https://foo.test/post-auth");
  request_post_auth.traffic_annotation = kTrafficAnnotation;

  HttpRequestInfo request_post_auth_bar;
  request_post_auth_bar.method = "GET";
  request_post_auth_bar.url = GURL("https://bar.test/post-auth");
  request_post_auth_bar.traffic_annotation = kTrafficAnnotation;

  // The sockets for /long-lived and /unauth complete their request but are
  // not allocated for /post-auth or /retry because SSL state has since changed.
  const MockWrite kLongLivedWrites[] = {
      MockWrite(ASYNC, 0,
                "GET /long-lived HTTP/1.1\r\n"
                "Host: foo.test\r\n"
                "Connection: keep-alive\r\n\r\n"),
  };
  const MockRead kLongLivedReads[] = {
      // Pause so /long-lived completes after the client presents client
      // certificates.
      MockRead(ASYNC, ERR_IO_PENDING, 1),
      MockRead(ASYNC, 2,
               "HTTP/1.1 200 OK\r\n"
               "Connection: keep-alive\r\n"
               "Content-Length: 10\r\n\r\n"
               "long-lived"),
  };
  SequencedSocketData data_long_lived(kLongLivedReads, kLongLivedWrites);
  SSLSocketDataProvider ssl_long_lived(ASYNC, OK);
  session_deps_.socket_factory->AddSocketDataProvider(&data_long_lived);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl_long_lived);

  // Requests for bar.test should be unaffected by foo.test and get allocated
  // a single socket.
  const MockWrite kBarWrites[] = {
      MockWrite(ASYNC, 0,
                "GET /long-lived HTTP/1.1\r\n"
                "Host: bar.test\r\n"
                "Connection: keep-alive\r\n\r\n"),
      MockWrite(ASYNC, 3,
                "GET /post-auth HTTP/1.1\r\n"
                "Host: bar.test\r\n"
                "Connection: keep-alive\r\n\r\n"),
  };
  const MockRead kBarReads[] = {
      // Pause on /long-lived so it completes after foo.test's authentication.
      MockRead(ASYNC, ERR_IO_PENDING, 1),
      MockRead(ASYNC, 2,
               "HTTP/1.1 200 OK\r\n"
               "Connection: keep-alive\r\n"
               "Content-Length: 10\r\n\r\n"
               "long-lived"),
      MockRead(ASYNC, 4,
               "HTTP/1.1 200 OK\r\n"
               "Connection: keep-alive\r\n"
               "Content-Length: 9\r\n\r\n"
               "post-auth"),
  };
  SequencedSocketData data_bar(kBarReads, kBarWrites);
  SSLSocketDataProvider ssl_bar(ASYNC, OK);
  session_deps_.socket_factory->AddSocketDataProvider(&data_bar);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl_bar);

  // Requesting /auth results in a post-handshake client certificate challenge.
  const MockWrite kAuthWrites[] = {
      MockWrite(ASYNC, 0,
                "GET /auth HTTP/1.1\r\n"
                "Host: foo.test\r\n"
                "Connection: keep-alive\r\n\r\n"),
  };
  const MockRead kAuthReads[] = {
      MockRead(ASYNC, ERR_SSL_CLIENT_AUTH_CERT_NEEDED, 1),
  };
  SequencedSocketData data_auth(kAuthReads, kAuthWrites);
  SSLSocketDataProvider ssl_auth(ASYNC, OK);
  ssl_auth.cert_request_info = cert_request_info.get();
  session_deps_.socket_factory->AddSocketDataProvider(&data_auth);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl_auth);

  // Requesting /unauth completes.
  const MockWrite kUnauthWrites[] = {
      MockWrite(ASYNC, 0,
                "GET /unauth HTTP/1.1\r\n"
                "Host: foo.test\r\n"
                "Connection: keep-alive\r\n\r\n"),
  };
  const MockRead kUnauthReads[] = {
      MockRead(ASYNC, 1,
               "HTTP/1.1 200 OK\r\n"
               "Connection: keep-alive\r\n"
               "Content-Length: 6\r\n\r\n"
               "unauth"),
  };
  SequencedSocketData data_unauth(kUnauthReads, kUnauthWrites);
  SSLSocketDataProvider ssl_unauth(ASYNC, OK);
  session_deps_.socket_factory->AddSocketDataProvider(&data_unauth);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl_unauth);

  // When the client certificate is selected, /auth is retried on a new
  // connection. In particular, it should not be retried on |data_unauth|,
  // which would not honor the new client certificate configuration.
  const MockWrite kRetryWrites[] = {
      MockWrite(ASYNC, 0,
                "GET /auth HTTP/1.1\r\n"
                "Host: foo.test\r\n"
                "Connection: keep-alive\r\n\r\n"),
  };
  const MockRead kRetryReads[] = {
      MockRead(ASYNC, 1,
               "HTTP/1.1 200 OK\r\n"
               // Close the connection so we test that /post-auth is not
               // allocated to |data_unauth| or |data_long_lived|.
               "Connection: close\r\n"
               "Content-Length: 4\r\n\r\n"
               "auth"),
  };
  SequencedSocketData data_retry(kRetryReads, kRetryWrites);
  SSLSocketDataProvider ssl_retry(ASYNC, OK);
  ssl_retry.expected_send_client_cert = true;
  ssl_retry.expected_client_cert = identity->certificate();
  session_deps_.socket_factory->AddSocketDataProvider(&data_retry);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl_retry);

  // /post-auth gets its own socket.
  const MockWrite kPostAuthWrites[] = {
      MockWrite(ASYNC, 0,
                "GET /post-auth HTTP/1.1\r\n"
                "Host: foo.test\r\n"
                "Connection: keep-alive\r\n\r\n"),
  };
  const MockRead kPostAuthReads[] = {
      MockRead(ASYNC, 1,
               "HTTP/1.1 200 OK\r\n"
               "Connection: keep-alive\r\n"
               "Content-Length: 9\r\n\r\n"
               "post-auth"),
  };
  SequencedSocketData data_post_auth(kPostAuthReads, kPostAuthWrites);
  SSLSocketDataProvider ssl_post_auth(ASYNC, OK);
  ssl_post_auth.expected_send_client_cert = true;
  ssl_post_auth.expected_client_cert = identity->certificate();
  session_deps_.socket_factory->AddSocketDataProvider(&data_post_auth);
  session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl_post_auth);

  std::unique_ptr<HttpNetworkSession> session = CreateSession(&session_deps_);

  // Start the two long-lived requests.
  TestCompletionCallback callback_long_lived;
  auto trans_long_lived =
      std::make_unique<HttpNetworkTransaction>(DEFAULT_PRIORITY, session.get());
  int rv = trans_long_lived->Start(
      &request_long_lived, callback_long_lived.callback(), NetLogWithSource());
  ASSERT_THAT(rv, IsError(ERR_IO_PENDING));
  data_long_lived.RunUntilPaused();

  TestCompletionCallback callback_long_lived_bar;
  auto trans_long_lived_bar =
      std::make_unique<HttpNetworkTransaction>(DEFAULT_PRIORITY, session.get());
  rv = trans_long_lived_bar->Start(&request_long_lived_bar,
                                   callback_long_lived_bar.callback(),
                                   NetLogWithSource());
  ASSERT_THAT(rv, IsError(ERR_IO_PENDING));
  data_bar.RunUntilPaused();

  // Request /auth. This gives a client certificate challenge.
  TestCompletionCallback callback_auth;
  auto trans_auth =
      std::make_unique<HttpNetworkTransaction>(DEFAULT_PRIORITY, session.get());
  rv = trans_auth->Start(&request_auth, callback_auth.callback(),
                         NetLogWithSource());
  EXPECT_THAT(callback_auth.GetResult(rv),
              IsError(ERR_SSL_CLIENT_AUTH_CERT_NEEDED));

  // Make an unauthenticated request. This completes.
  TestCompletionCallback callback_unauth;
  auto trans_unauth =
      std::make_unique<HttpNetworkTransaction>(DEFAULT_PRIORITY, session.get());
  rv = trans_unauth->Start(&request_unauth, callback_unauth.callback(),
                           NetLogWithSource());
  EXPECT_THAT(callback_unauth.GetResult(rv), IsOk());
  std::string response_unauth;
  EXPECT_THAT(ReadTransaction(trans_unauth.get(), &response_unauth), IsOk());
  EXPECT_EQ("unauth", response_unauth);
  trans_unauth.reset();

  // Complete the authenticated request.
  rv = trans_auth->RestartWithCertificate(identity->certificate(),
                                          identity->ssl_private_key(),
                                          callback_auth.callback());
  EXPECT_THAT(callback_auth.GetResult(rv), IsOk());
  std::string response_auth;
  EXPECT_THAT(ReadTransaction(trans_auth.get(), &response_auth), IsOk());
  EXPECT_EQ("auth", response_auth);
  trans_auth.reset();

  // Complete the long-lived requests.
  data_long_lived.Resume();
  EXPECT_THAT(callback_long_lived.GetResult(ERR_IO_PENDING), IsOk());
  std::string response_long_lived;
  EXPECT_THAT(ReadTransaction(trans_long_lived.get(), &response_long_lived),
              IsOk());
  EXPECT_EQ("long-lived", response_long_lived);
  trans_long_lived.reset();

  data_bar.Resume();
  EXPECT_THAT(callback_long_lived_bar.GetResult(ERR_IO_PENDING), IsOk());
  std::string response_long_lived_bar;
  EXPECT_THAT(
      ReadTransaction(trans_long_lived_bar.get(), &response_long_lived_bar),
      IsOk());
  EXPECT_EQ("long-lived", response_long_lived_bar);
  trans_long_lived_bar.reset();

  // Run the post-authentication requests.
  TestCompletionCallback callback_post_auth;
  auto trans_post_auth =
      std::make_unique<HttpNetworkTransaction>(DEFAULT_PRIORITY, session.get());
  rv = trans_post_auth->Start(&request_post_auth, callback_post_auth.callback(),
                              NetLogWithSource());
  EXPECT_THAT(callback_post_auth.GetResult(rv), IsOk());
  std::string response_post_auth;
  EXPECT_THAT(ReadTransaction(trans_post_auth.get(), &response_post_auth),
              IsOk());
  EXPECT_EQ("post-auth", response_post_auth);
  trans_post_auth.reset();

  TestCompletionCallback callback_post_auth_bar;
  auto trans_post_auth_bar =
      std::make_unique<HttpNetworkTransaction>(DEFAULT_PRIORITY, session.get());
  rv = trans_post_auth_bar->Start(&request_post_auth_bar,
                                  callback_post_auth_bar.callback(),
                                  NetLogWithSource());
  EXPECT_THAT(callback_post_auth_bar.GetResult(rv), IsOk());
  std::string response_post_auth_bar;
  EXPECT_THAT(
      ReadTransaction(trans_post_auth_bar.get(), &response_post_auth_bar),
      IsOk());
  EXPECT_EQ("post-auth", response_post_auth_bar);
  trans_post_auth_bar.reset();
}

TEST_F(HttpNetworkTransactionTest, RequestWithDnsAliases) {
  // Create a request.
  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("http://www.example.org/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  // Add a rule with DNS aliases to the host resolver.
  std::vector<std::string> aliases({"alias1", "alias2", "www.example.org"});
  session_deps_.host_resolver->rules()->AddIPLiteralRuleWithDnsAliases(
      "www.example.org", "127.0.0.1", std::move(aliases));

  // Create a HttpNetworkSession.
  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));

  // Create a transaction.
  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

  // Prepare the expected data to be written and read. The client should send
  // the request below.
  MockWrite data_writes[] = {
      MockWrite("GET / HTTP/1.1\r\n"
                "Host: www.example.org\r\n"
                "Connection: keep-alive\r\n\r\n"),
  };

  // The server should respond with the following.
  MockRead data_reads[] = {
      MockRead("HTTP/1.0 200 OK\r\n"),
      MockRead("Content-Type: text/html; charset=iso-8859-1\r\n"),
      MockRead("Content-Length: 100\r\n\r\n"),
      MockRead(SYNCHRONOUS, OK),
  };

  StaticSocketDataProvider data(data_reads, data_writes);
  session_deps_.socket_factory->AddSocketDataProvider(&data);
  TestCompletionCallback callback;

  // Start the transaction.
  int rv = trans.Start(&request, callback.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  // Wait for completion.
  rv = callback.WaitForResult();
  EXPECT_THAT(rv, IsOk());

  // Get the response info.
  const HttpResponseInfo* response = trans.GetResponseInfo();

  // Verify that the alias list was stored in the response info as expected.
  ASSERT_TRUE(response);
  EXPECT_THAT(response->dns_aliases,
              testing::ElementsAre("alias1", "alias2", "www.example.org"));
}

TEST_F(HttpNetworkTransactionTest, RequestWithNoAdditionalDnsAliases) {
  // Create a request.
  HttpRequestInfo request;
  request.method = "GET";
  request.url = GURL("http://www.example.org/");
  request.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS);

  // Add a rule without DNS aliases to the host resolver.
  session_deps_.host_resolver->rules()->AddRule("www.example.org", "127.0.0.1");

  // Create a HttpNetworkSession.
  std::unique_ptr<HttpNetworkSession> session(CreateSession(&session_deps_));

  // Create a transaction.
  HttpNetworkTransaction trans(DEFAULT_PRIORITY, session.get());

  // Prepare the expected data to be written and read. The client should send
  // the request below.
  MockWrite data_writes[] = {
      MockWrite("GET / HTTP/1.1\r\n"
                "Host: www.example.org\r\n"
                "Connection: keep-alive\r\n\r\n"),
  };

  // The server should respond with the following.
  MockRead data_reads[] = {
      MockRead("HTTP/1.0 200 OK\r\n"),
      MockRead("Content-Type: text/html; charset=iso-8859-1\r\n"),
      MockRead("Content-Length: 100\r\n\r\n"),
      MockRead(SYNCHRONOUS, OK),
  };

  StaticSocketDataProvider data(data_reads, data_writes);
  session_deps_.socket_factory->AddSocketDataProvider(&data);
  TestCompletionCallback callback;

  // Start the transaction.
  int rv = trans.Start(&request, callback.callback(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  // Wait for completion.
  rv = callback.WaitForResult();
  EXPECT_THAT(rv, IsOk());

  // Get the response info.
  const HttpResponseInfo* response = trans.GetResponseInfo();

  // Verify that the alias list was stored in the response info as expected.
  ASSERT_TRUE(response);
  EXPECT_THAT(response->dns_aliases, testing::ElementsAre("www.example.org"));
}

}  // namespace net
