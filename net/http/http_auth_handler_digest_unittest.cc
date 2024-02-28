// Copyright 2011 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/http/http_auth_handler_digest.h"

#include <string>

#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "net/base/net_errors.h"
#include "net/base/network_anonymization_key.h"
#include "net/base/test_completion_callback.h"
#include "net/dns/mock_host_resolver.h"
#include "net/http/http_auth_challenge_tokenizer.h"
#include "net/http/http_request_info.h"
#include "net/log/net_log_with_source.h"
#include "net/ssl/ssl_info.h"
#include "net/test/gtest_util.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"
#include "url/scheme_host_port.h"

using net::test::IsOk;

namespace net {

namespace {

const char* const kSimpleChallenge =
  "Digest realm=\"Oblivion\", nonce=\"nonce-value\"";

// RespondToChallenge creates an HttpAuthHandlerDigest for the specified
// |challenge|, and generates a response to the challenge which is returned in
// |token|.
//
// The return value indicates whether the |token| was successfully created.
//
// If |target| is HttpAuth::AUTH_PROXY, then |proxy_name| specifies the source
// of the |challenge|. Otherwise, the scheme and host and port of |request_url|
// indicates the origin of the challenge.
bool RespondToChallenge(HttpAuth::Target target,
                        const std::string& proxy_name,
                        const std::string& request_url,
                        const std::string& challenge,
                        std::string* token) {
  // Input validation.
  if (token == nullptr) {
    ADD_FAILURE() << "|token| must be valid";
    return false;
  }
  EXPECT_TRUE(target != HttpAuth::AUTH_PROXY || !proxy_name.empty());
  EXPECT_FALSE(request_url.empty());
  EXPECT_FALSE(challenge.empty());

  token->clear();
  auto factory = std::make_unique<HttpAuthHandlerDigest::Factory>();
  auto nonce_generator =
      std::make_unique<HttpAuthHandlerDigest::FixedNonceGenerator>(
          "client_nonce");
  factory->set_nonce_generator(std::move(nonce_generator));
  auto host_resolver = std::make_unique<MockHostResolver>();
  std::unique_ptr<HttpAuthHandler> handler;

  // Create a handler for a particular challenge.
  SSLInfo null_ssl_info;
  url::SchemeHostPort scheme_host_port(
      target == HttpAuth::AUTH_SERVER ? GURL(request_url) : GURL(proxy_name));
  int rv_create = factory->CreateAuthHandlerFromString(
      challenge, target, null_ssl_info, NetworkAnonymizationKey(),
      scheme_host_port, NetLogWithSource(), host_resolver.get(), &handler);
  if (rv_create != OK || handler.get() == nullptr) {
    ADD_FAILURE() << "Unable to create auth handler.";
    return false;
  }

  // Create a token in response to the challenge.
  // NOTE: HttpAuthHandlerDigest's implementation of GenerateAuthToken always
  // completes synchronously. That's why this test can get away with a
  // TestCompletionCallback without an IO thread.
  TestCompletionCallback callback;
  auto request = std::make_unique<HttpRequestInfo>();
  request->url = GURL(request_url);
  AuthCredentials credentials(u"foo", u"bar");
  int rv_generate = handler->GenerateAuthToken(
      &credentials, request.get(), callback.callback(), token);
  if (rv_generate != OK) {
    ADD_FAILURE() << "Problems generating auth token";
    return false;
  }

  return true;
}

}  // namespace


TEST(HttpAuthHandlerDigestTest, ParseChallenge) {
  // clang-format off
  static const struct {
    // The challenge string.
    const char* challenge;
    // Expected return value of ParseChallenge.
    bool parsed_success;
    // The expected values that were parsed.
    const char* parsed_realm;
    const char* parsed_nonce;
    const char* parsed_domain;
    const char* parsed_opaque;
    bool parsed_stale;
    HttpAuthHandlerDigest::Algorithm parsed_algorithm;
    int parsed_qop;
  } tests[] = {
    { // Check that a minimal challenge works correctly.
      "Digest nonce=\"xyz\", realm=\"Thunder Bluff\"",
      true,
      "Thunder Bluff",
      "xyz",
      "",
      "",
      false,
      HttpAuthHandlerDigest::Algorithm::UNSPECIFIED,
      HttpAuthHandlerDigest::QOP_UNSPECIFIED
    },

    { // Realm does not need to be quoted, even though RFC2617 requires it.
      "Digest nonce=\"xyz\", realm=ThunderBluff",
      true,
      "ThunderBluff",
      "xyz",
      "",
      "",
      false,
      HttpAuthHandlerDigest::Algorithm::UNSPECIFIED,
      HttpAuthHandlerDigest::QOP_UNSPECIFIED
    },

    { // We allow the realm to be omitted, and will default it to empty string.
      // See http://crbug.com/20984.
      "Digest nonce=\"xyz\"",
      true,
      "",
      "xyz",
      "",
      "",
      false,
      HttpAuthHandlerDigest::Algorithm::UNSPECIFIED,
      HttpAuthHandlerDigest::QOP_UNSPECIFIED
    },

    { // Try with realm set to empty string.
      "Digest realm=\"\", nonce=\"xyz\"",
      true,
      "",
      "xyz",
      "",
      "",
      false,
      HttpAuthHandlerDigest::Algorithm::UNSPECIFIED,
      HttpAuthHandlerDigest::QOP_UNSPECIFIED
    },

    // Handle ISO-8859-1 character as part of the realm. The realm is converted
    // to UTF-8. However, the credentials will still use the original encoding.
    {
      "Digest nonce=\"xyz\", realm=\"foo-\xE5\"",
      true,
      "foo-\xC3\xA5",
      "xyz",
      "",
      "",
      false,
      HttpAuthHandlerDigest::Algorithm::UNSPECIFIED,
      HttpAuthHandlerDigest::QOP_UNSPECIFIED,
    },

    { // At a minimum, a nonce must be provided.
      "Digest realm=\"Thunder Bluff\"",
      false,
      "",
      "",
      "",
      "",
      false,
      HttpAuthHandlerDigest::Algorithm::UNSPECIFIED,
      HttpAuthHandlerDigest::QOP_UNSPECIFIED
    },

    { // The nonce does not need to be quoted, even though RFC2617
      // requires it.
      "Digest nonce=xyz, realm=\"Thunder Bluff\"",
      true,
      "Thunder Bluff",
      "xyz",
      "",
      "",
      false,
      HttpAuthHandlerDigest::Algorithm::UNSPECIFIED,
      HttpAuthHandlerDigest::QOP_UNSPECIFIED
    },

    { // Unknown authentication parameters are ignored.
      "Digest nonce=\"xyz\", realm=\"Thunder Bluff\", foo=\"bar\"",
      true,
      "Thunder Bluff",
      "xyz",
      "",
      "",
      false,
      HttpAuthHandlerDigest::Algorithm::UNSPECIFIED,
      HttpAuthHandlerDigest::QOP_UNSPECIFIED
    },

    { // Check that when algorithm has an unsupported value, parsing fails.
      "Digest nonce=\"xyz\", algorithm=\"awezum\", realm=\"Thunder\"",
      false,
      // The remaining values don't matter (but some have been set already).
      "",
      "xyz",
      "",
      "",
      false,
      HttpAuthHandlerDigest::Algorithm::UNSPECIFIED,
      HttpAuthHandlerDigest::QOP_UNSPECIFIED
    },

    { // Check that algorithm's value is case insensitive, and that MD5 is
      // a supported algorithm.
      "Digest nonce=\"xyz\", algorithm=\"mD5\", realm=\"Oblivion\"",
      true,
      "Oblivion",
      "xyz",
      "",
      "",
      false,
      HttpAuthHandlerDigest::Algorithm::MD5,
      HttpAuthHandlerDigest::QOP_UNSPECIFIED
    },

    { // Check that md5-sess is a supported algorithm.
      "Digest nonce=\"xyz\", algorithm=\"md5-sess\", realm=\"Oblivion\"",
      true,
      "Oblivion",
      "xyz",
      "",
      "",
      false,
      HttpAuthHandlerDigest::Algorithm::MD5_SESS,
      HttpAuthHandlerDigest::QOP_UNSPECIFIED,
    },

    { // Check that that SHA-256 is a supported algorithm.
      "Digest nonce=\"xyz\", algorithm=SHA-256, realm=\"Oblivion\"",
      true,
      "Oblivion",
      "xyz",
      "",
      "",
      false,
      HttpAuthHandlerDigest::Algorithm::SHA256,
      HttpAuthHandlerDigest::QOP_UNSPECIFIED
    },

    { // Check that that SHA-256-sess is a supported algorithm.
      "Digest nonce=\"xyz\", algorithm=SHA-256-sess, realm=\"Oblivion\"",
      true,
      "Oblivion",
      "xyz",
      "",
      "",
      false,
      HttpAuthHandlerDigest::Algorithm::SHA256_SESS,
      HttpAuthHandlerDigest::QOP_UNSPECIFIED
    },

    { // Check that md5-sess is a supported algorithm.
      "Digest nonce=\"xyz\", algorithm=\"md5-sess\", realm=\"Oblivion\"",
      true,
      "Oblivion",
      "xyz",
      "",
      "",
      false,
      HttpAuthHandlerDigest::Algorithm::MD5_SESS,
      HttpAuthHandlerDigest::QOP_UNSPECIFIED,
    },

    { // Check that qop's value is case insensitive, and that auth is known.
      "Digest nonce=\"xyz\", realm=\"Oblivion\", qop=\"aUth\"",
      true,
      "Oblivion",
      "xyz",
      "",
      "",
      false,
      HttpAuthHandlerDigest::Algorithm::UNSPECIFIED,
      HttpAuthHandlerDigest::QOP_AUTH
    },

    { // auth-int is not handled, but will fall back to default qop.
      "Digest nonce=\"xyz\", realm=\"Oblivion\", qop=\"auth-int\"",
      true,
      "Oblivion",
      "xyz",
      "",
      "",
      false,
      HttpAuthHandlerDigest::Algorithm::UNSPECIFIED,
      HttpAuthHandlerDigest::QOP_UNSPECIFIED
    },

    { // Unknown qop values are ignored.
      "Digest nonce=\"xyz\", realm=\"Oblivion\", qop=\"auth,foo\"",
      true,
      "Oblivion",
      "xyz",
      "",
      "",
      false,
      HttpAuthHandlerDigest::Algorithm::UNSPECIFIED,
      HttpAuthHandlerDigest::QOP_AUTH
    },

    { // If auth-int is included with auth, then use auth.
      "Digest nonce=\"xyz\", realm=\"Oblivion\", qop=\"auth,auth-int\"",
      true,
      "Oblivion",
      "xyz",
      "",
      "",
      false,
      HttpAuthHandlerDigest::Algorithm::UNSPECIFIED,
      HttpAuthHandlerDigest::QOP_AUTH
    },

    { // Opaque parameter parsing should work correctly.
      "Digest nonce=\"xyz\", realm=\"Thunder Bluff\", opaque=\"foobar\"",
      true,
      "Thunder Bluff",
      "xyz",
      "",
      "foobar",
      false,
      HttpAuthHandlerDigest::Algorithm::UNSPECIFIED,
      HttpAuthHandlerDigest::QOP_UNSPECIFIED
    },

    { // Opaque parameters do not need to be quoted, even though RFC2617
      // seems to require it.
      "Digest nonce=\"xyz\", realm=\"Thunder Bluff\", opaque=foobar",
      true,
      "Thunder Bluff",
      "xyz",
      "",
      "foobar",
      false,
      HttpAuthHandlerDigest::Algorithm::UNSPECIFIED,
      HttpAuthHandlerDigest::QOP_UNSPECIFIED
    },

    { // Domain can be parsed.
      "Digest nonce=\"xyz\", realm=\"Thunder Bluff\", "
      "domain=\"http://intranet.example.com/protection\"",
      true,
      "Thunder Bluff",
      "xyz",
      "http://intranet.example.com/protection",
      "",
      false,
      HttpAuthHandlerDigest::Algorithm::UNSPECIFIED,
      HttpAuthHandlerDigest::QOP_UNSPECIFIED
    },

    { // Multiple domains can be parsed.
      "Digest nonce=\"xyz\", realm=\"Thunder Bluff\", "
      "domain=\"http://intranet.example.com/protection http://www.google.com\"",
      true,
      "Thunder Bluff",
      "xyz",
      "http://intranet.example.com/protection http://www.google.com",
      "",
      false,
      HttpAuthHandlerDigest::Algorithm::UNSPECIFIED,
      HttpAuthHandlerDigest::QOP_UNSPECIFIED
    },

    { // If a non-Digest scheme is somehow passed in, it should be rejected.
      "Basic realm=\"foo\"",
      false,
      "",
      "",
      "",
      "",
      false,
      HttpAuthHandlerDigest::Algorithm::UNSPECIFIED,
      HttpAuthHandlerDigest::QOP_UNSPECIFIED
    },
  };
  // clang-format on

  url::SchemeHostPort scheme_host_port(GURL("http://www.example.com"));
  auto factory = std::make_unique<HttpAuthHandlerDigest::Factory>();
  for (const auto& test : tests) {
    SSLInfo null_ssl_info;
    auto host_resolver = std::make_unique<MockHostResolver>();
    std::unique_ptr<HttpAuthHandler> handler;
    int rv = factory->CreateAuthHandlerFromString(
        test.challenge, HttpAuth::AUTH_SERVER, null_ssl_info,
        NetworkAnonymizationKey(), scheme_host_port, NetLogWithSource(),
        host_resolver.get(), &handler);
    if (test.parsed_success) {
      EXPECT_THAT(rv, IsOk());
    } else {
      EXPECT_NE(OK, rv);
      EXPECT_TRUE(handler.get() == nullptr);
      continue;
    }
    ASSERT_TRUE(handler.get() != nullptr);
    HttpAuthHandlerDigest* digest =
        static_cast<HttpAuthHandlerDigest*>(handler.get());
    EXPECT_STREQ(test.parsed_realm, digest->realm_.c_str());
    EXPECT_STREQ(test.parsed_nonce, digest->nonce_.c_str());
    EXPECT_STREQ(test.parsed_domain, digest->domain_.c_str());
    EXPECT_STREQ(test.parsed_opaque, digest->opaque_.c_str());
    EXPECT_EQ(test.parsed_stale, digest->stale_);
    EXPECT_EQ(test.parsed_algorithm, digest->algorithm_);
    EXPECT_EQ(test.parsed_qop, digest->qop_);
    EXPECT_TRUE(handler->encrypts_identity());
    EXPECT_FALSE(handler->is_connection_based());
    EXPECT_TRUE(handler->NeedsIdentity());
    EXPECT_FALSE(handler->AllowsDefaultCredentials());
  }
}

TEST(HttpAuthHandlerDigestTest, AssembleCredentials) {
  // clang-format off
  static const struct {
    const char* req_method;
    const char* req_path;
    const char* challenge;
    const char* username;
    const char* password;
    const char* cnonce;
    int nonce_count;
    const char* expected_creds;
  } tests[] = {
    { // MD5 (default) with username/password
      "GET",
      "/test/drealm1",

      // Challenge
      "Digest realm=\"DRealm1\", "
      "nonce=\"claGgoRXBAA=7583377687842fdb7b56ba0555d175baa0b800e3\", "
      "qop=\"auth\"",

      "foo", "bar", // username/password
      "082c875dcb2ca740", // cnonce
      1, // nc

      // Authorization
      "Digest username=\"foo\", realm=\"DRealm1\", "
      "nonce=\"claGgoRXBAA=7583377687842fdb7b56ba0555d175baa0b800e3\", "
      "uri=\"/test/drealm1\", "
      "response=\"bcfaa62f1186a31ff1b474a19a17cf57\", "
      "qop=auth, nc=00000001, cnonce=\"082c875dcb2ca740\""
    },

    { // MD5 with username but empty password. username has space in it.
      "GET",
      "/test/drealm1/",

      // Challenge
      "Digest realm=\"DRealm1\", "
      "nonce=\"Ure30oRXBAA=7eca98bbf521ac6642820b11b86bd2d9ed7edc70\", "
      "algorithm=MD5, qop=\"auth\"",

      "foo bar", "", // Username/password
      "082c875dcb2ca740", // cnonce
      1, // nc

      // Authorization
      "Digest username=\"foo bar\", realm=\"DRealm1\", "
      "nonce=\"Ure30oRXBAA=7eca98bbf521ac6642820b11b86bd2d9ed7edc70\", "
      "uri=\"/test/drealm1/\", algorithm=MD5, "
      "response=\"93c9c6d5930af3b0eb26c745e02b04a0\", "
      "qop=auth, nc=00000001, cnonce=\"082c875dcb2ca740\""
    },

    { // MD5 with no username.
      "GET",
      "/test/drealm1/",

      // Challenge
      "Digest realm=\"DRealm1\", "
      "nonce=\"7thGplhaBAA=41fb92453c49799cf353c8cd0aabee02d61a98a8\", "
      "algorithm=MD5, qop=\"auth\"",

      "", "pass", // Username/password
      "6509bc74daed8263", // cnonce
      1, // nc

      // Authorization
      "Digest username=\"\", realm=\"DRealm1\", "
      "nonce=\"7thGplhaBAA=41fb92453c49799cf353c8cd0aabee02d61a98a8\", "
      "uri=\"/test/drealm1/\", algorithm=MD5, "
      "response=\"bc597110f41a62d07f8b70b6977fcb61\", "
      "qop=auth, nc=00000001, cnonce=\"6509bc74daed8263\""
    },

    { // MD5 with no username and no password.
      "GET",
      "/test/drealm1/",

      // Challenge
      "Digest realm=\"DRealm1\", "
      "nonce=\"s3MzvFhaBAA=4c520af5acd9d8d7ae26947529d18c8eae1e98f4\", "
      "algorithm=MD5, qop=\"auth\"",

      "", "", // Username/password
      "1522e61005789929", // cnonce
      1, // nc

      // Authorization
      "Digest username=\"\", realm=\"DRealm1\", "
      "nonce=\"s3MzvFhaBAA=4c520af5acd9d8d7ae26947529d18c8eae1e98f4\", "
      "uri=\"/test/drealm1/\", algorithm=MD5, "
      "response=\"22cfa2b30cb500a9591c6d55ec5590a8\", "
      "qop=auth, nc=00000001, cnonce=\"1522e61005789929\""
    },

    { // No algorithm, and no qop.
      "GET",
      "/",

      // Challenge
      "Digest realm=\"Oblivion\", nonce=\"nonce-value\"",

      "FooBar", "pass", // Username/password
      "", // cnonce
      1, // nc

      // Authorization
      "Digest username=\"FooBar\", realm=\"Oblivion\", "
      "nonce=\"nonce-value\", uri=\"/\", "
      "response=\"f72ff54ebde2f928860f806ec04acd1b\""
    },

    { // MD5-sess
      "GET",
      "/",

      // Challenge
      "Digest realm=\"Baztastic\", nonce=\"AAAAAAAA\", "
      "algorithm=\"md5-sess\", qop=auth",

      "USER", "123", // Username/password
      "15c07961ed8575c4", // cnonce
      1, // nc

      // Authorization
      "Digest username=\"USER\", realm=\"Baztastic\", "
      "nonce=\"AAAAAAAA\", uri=\"/\", algorithm=MD5-sess, "
      "response=\"cbc1139821ee7192069580570c541a03\", "
      "qop=auth, nc=00000001, cnonce=\"15c07961ed8575c4\""
    },

    { // RFC MD5 (https://www.rfc-editor.org/rfc/rfc7616#section-3.9.1)
      "GET",
      "/dir/index.html",

      // Challenge
      "Digest realm=\"http-auth@example.org\", "
      "qop=\"auth, auth-int\", "
      "algorithm=MD5, "
      "nonce=\"7ypf/xlj9XXwfDPEoM4URrv/xwf94BcCAzFZH4GiTo0v\","
      "opaque=\"FQhe/qaU925kfnzjCev0ciny7QMkPqMAFRtzCUYo5tdS\"",

      "Mufasa", "Circle of Life", // Username/password
      "f2/wE4q74E6zIJEtWaHKaf5wv/H5QzzpXusqGemxURZJ", // cnonce
      1, // nc

      // Authorization
      "Digest username=\"Mufasa\", realm=\"http-auth@example.org\", "
      "nonce=\"7ypf/xlj9XXwfDPEoM4URrv/xwf94BcCAzFZH4GiTo0v\", "
      "uri=\"/dir/index.html\", algorithm=MD5, "
      "response=\"8ca523f5e9506fed4657c9700eebdbec\", "
      "opaque=\"FQhe/qaU925kfnzjCev0ciny7QMkPqMAFRtzCUYo5tdS\", "
      "qop=auth, nc=00000001, "
      "cnonce=\"f2/wE4q74E6zIJEtWaHKaf5wv/H5QzzpXusqGemxURZJ\""
    },

    { // RFC SHA-256 (https://www.rfc-editor.org/rfc/rfc7616#section-3.9.1)
      "GET",
      "/dir/index.html",

      // Challenge
      "Digest realm=\"http-auth@example.org\", "
      "qop=\"auth, auth-int\", "
      "algorithm=SHA-256, "
      "nonce=\"7ypf/xlj9XXwfDPEoM4URrv/xwf94BcCAzFZH4GiTo0v\","
      "opaque=\"FQhe/qaU925kfnzjCev0ciny7QMkPqMAFRtzCUYo5tdS\"",

      "Mufasa", "Circle of Life", // Username/password
      "f2/wE4q74E6zIJEtWaHKaf5wv/H5QzzpXusqGemxURZJ", // cnonce
      1, // nc

      // Authorization
      "Digest username=\"Mufasa\", realm=\"http-auth@example.org\", "
      "nonce=\"7ypf/xlj9XXwfDPEoM4URrv/xwf94BcCAzFZH4GiTo0v\", "
      "uri=\"/dir/index.html\", algorithm=SHA-256, "
      "response=\"753927fa0e85d155564e2e272a28d1802ca10daf4496794697cf8db5856cb6c1\", "
      "opaque=\"FQhe/qaU925kfnzjCev0ciny7QMkPqMAFRtzCUYo5tdS\", "
      "qop=auth, nc=00000001, "
      "cnonce=\"f2/wE4q74E6zIJEtWaHKaf5wv/H5QzzpXusqGemxURZJ\""
    },

    { // RFC SHA-256 and userhash
      "GET",
      "/doe.json",

      // Challenge
      "Digest realm=\"api@example.org\", "
      "qop=\"auth\", "
      "algorithm=SHA-256, "
      "nonce=\"5TsQWLVdgBdmrQ0XsxbDODV+57QdFR34I9HAbC/RVvkK\", "
      "opaque=\"HRPCssKJSGjCrkzDg8OhwpzCiGPChXYjwrI2QmXDnsOS\", "
      "charset=UTF-8, userhash=true",

      "J\xc3\xa4s\xc3\xb8n Doe", "Secret, or not?", // Username/password
      "NTg6RKcb9boFIAS3KrFK9BGeh+iDa/sm6jUMp2wds69v", // cnonce
      0x123, // nc

      // Authorization
      "Digest username=\"5a1a8a47df5c298551b9b42ba9b05835174a5bd7d511ff7fe9191d8e946fc4e7\", "
      "realm=\"api@example.org\", "
      "nonce=\"5TsQWLVdgBdmrQ0XsxbDODV+57QdFR34I9HAbC/RVvkK\", "
      "uri=\"/doe.json\", algorithm=SHA-256, "
      "response=\"61baba8a218e4b207f158ed9b9b3a95ed940c1872ef3ff4522eb10110720a145\", "
      "opaque=\"HRPCssKJSGjCrkzDg8OhwpzCiGPChXYjwrI2QmXDnsOS\", "
      "qop=auth, nc=00000123, "
      "cnonce=\"NTg6RKcb9boFIAS3KrFK9BGeh+iDa/sm6jUMp2wds69v\", "
      "userhash=true"
    },
  };
  // clang-format on
  url::SchemeHostPort scheme_host_port(GURL("http://www.example.com"));
  auto factory = std::make_unique<HttpAuthHandlerDigest::Factory>();
  for (const auto& test : tests) {
    SSLInfo null_ssl_info;
    auto host_resolver = std::make_unique<MockHostResolver>();
    std::unique_ptr<HttpAuthHandler> handler;
    int rv = factory->CreateAuthHandlerFromString(
        test.challenge, HttpAuth::AUTH_SERVER, null_ssl_info,
        NetworkAnonymizationKey(), scheme_host_port, NetLogWithSource(),
        host_resolver.get(), &handler);
    EXPECT_THAT(rv, IsOk());
    ASSERT_TRUE(handler != nullptr);

    HttpAuthHandlerDigest* digest =
        static_cast<HttpAuthHandlerDigest*>(handler.get());
    std::string creds = digest->AssembleCredentials(
        test.req_method, test.req_path,
        AuthCredentials(base::UTF8ToUTF16(test.username),
                        base::UTF8ToUTF16(test.password)),
        test.cnonce, test.nonce_count);

    EXPECT_STREQ(test.expected_creds, creds.c_str());
  }
}

TEST(HttpAuthHandlerDigest, HandleAnotherChallenge) {
  auto factory = std::make_unique<HttpAuthHandlerDigest::Factory>();
  auto host_resolver = std::make_unique<MockHostResolver>();
  std::unique_ptr<HttpAuthHandler> handler;
  std::string default_challenge =
      "Digest realm=\"Oblivion\", nonce=\"nonce-value\"";
  url::SchemeHostPort scheme_host_port(GURL("http://intranet.google.com"));
  SSLInfo null_ssl_info;
  int rv = factory->CreateAuthHandlerFromString(
      default_challenge, HttpAuth::AUTH_SERVER, null_ssl_info,
      NetworkAnonymizationKey(), scheme_host_port, NetLogWithSource(),
      host_resolver.get(), &handler);
  EXPECT_THAT(rv, IsOk());
  ASSERT_TRUE(handler.get() != nullptr);
  HttpAuthChallengeTokenizer tok_default(default_challenge.begin(),
                                         default_challenge.end());
  EXPECT_EQ(HttpAuth::AUTHORIZATION_RESULT_REJECT,
            handler->HandleAnotherChallenge(&tok_default));

  std::string stale_challenge = default_challenge + ", stale=true";
  HttpAuthChallengeTokenizer tok_stale(stale_challenge.begin(),
                                       stale_challenge.end());
  EXPECT_EQ(HttpAuth::AUTHORIZATION_RESULT_STALE,
            handler->HandleAnotherChallenge(&tok_stale));

  std::string stale_false_challenge = default_challenge + ", stale=false";
  HttpAuthChallengeTokenizer tok_stale_false(stale_false_challenge.begin(),
                                             stale_false_challenge.end());
  EXPECT_EQ(HttpAuth::AUTHORIZATION_RESULT_REJECT,
            handler->HandleAnotherChallenge(&tok_stale_false));

  std::string realm_change_challenge =
      "Digest realm=\"SomethingElse\", nonce=\"nonce-value2\"";
  HttpAuthChallengeTokenizer tok_realm_change(realm_change_challenge.begin(),
                                              realm_change_challenge.end());
  EXPECT_EQ(HttpAuth::AUTHORIZATION_RESULT_DIFFERENT_REALM,
            handler->HandleAnotherChallenge(&tok_realm_change));
}

TEST(HttpAuthHandlerDigest, RespondToServerChallenge) {
  std::string auth_token;
  EXPECT_TRUE(RespondToChallenge(
      HttpAuth::AUTH_SERVER,
      std::string(),
      "http://www.example.com/path/to/resource",
      kSimpleChallenge,
      &auth_token));
  EXPECT_EQ("Digest username=\"foo\", realm=\"Oblivion\", "
            "nonce=\"nonce-value\", uri=\"/path/to/resource\", "
            "response=\"6779f90bd0d658f937c1af967614fe84\"",
            auth_token);
}

TEST(HttpAuthHandlerDigest, RespondToHttpsServerChallenge) {
  std::string auth_token;
  EXPECT_TRUE(RespondToChallenge(
      HttpAuth::AUTH_SERVER,
      std::string(),
      "https://www.example.com/path/to/resource",
      kSimpleChallenge,
      &auth_token));
  EXPECT_EQ("Digest username=\"foo\", realm=\"Oblivion\", "
            "nonce=\"nonce-value\", uri=\"/path/to/resource\", "
            "response=\"6779f90bd0d658f937c1af967614fe84\"",
            auth_token);
}

TEST(HttpAuthHandlerDigest, RespondToProxyChallenge) {
  std::string auth_token;
  EXPECT_TRUE(RespondToChallenge(
      HttpAuth::AUTH_PROXY,
      "http://proxy.intranet.corp.com:3128",
      "http://www.example.com/path/to/resource",
      kSimpleChallenge,
      &auth_token));
  EXPECT_EQ("Digest username=\"foo\", realm=\"Oblivion\", "
            "nonce=\"nonce-value\", uri=\"/path/to/resource\", "
            "response=\"6779f90bd0d658f937c1af967614fe84\"",
            auth_token);
}

TEST(HttpAuthHandlerDigest, RespondToProxyChallengeHttps) {
  std::string auth_token;
  EXPECT_TRUE(RespondToChallenge(
      HttpAuth::AUTH_PROXY,
      "http://proxy.intranet.corp.com:3128",
      "https://www.example.com/path/to/resource",
      kSimpleChallenge,
      &auth_token));
  EXPECT_EQ("Digest username=\"foo\", realm=\"Oblivion\", "
            "nonce=\"nonce-value\", uri=\"www.example.com:443\", "
            "response=\"3270da8467afbe9ddf2334a48d46e9b9\"",
            auth_token);
}

TEST(HttpAuthHandlerDigest, RespondToProxyChallengeWs) {
  std::string auth_token;
  EXPECT_TRUE(RespondToChallenge(
      HttpAuth::AUTH_PROXY,
      "http://proxy.intranet.corp.com:3128",
      "ws://www.example.com/echo",
      kSimpleChallenge,
      &auth_token));
  EXPECT_EQ("Digest username=\"foo\", realm=\"Oblivion\", "
            "nonce=\"nonce-value\", uri=\"www.example.com:80\", "
            "response=\"aa1df184f68d5b6ab9d9aa4f88e41b4c\"",
            auth_token);
}

TEST(HttpAuthHandlerDigest, RespondToProxyChallengeWss) {
  std::string auth_token;
  EXPECT_TRUE(RespondToChallenge(
      HttpAuth::AUTH_PROXY,
      "http://proxy.intranet.corp.com:3128",
      "wss://www.example.com/echo",
      kSimpleChallenge,
      &auth_token));
  EXPECT_EQ("Digest username=\"foo\", realm=\"Oblivion\", "
            "nonce=\"nonce-value\", uri=\"www.example.com:443\", "
            "response=\"3270da8467afbe9ddf2334a48d46e9b9\"",
            auth_token);
}

TEST(HttpAuthHandlerDigest, RespondToChallengeAuthQop) {
  std::string auth_token;
  EXPECT_TRUE(RespondToChallenge(
      HttpAuth::AUTH_SERVER,
      std::string(),
      "http://www.example.com/path/to/resource",
      "Digest realm=\"Oblivion\", nonce=\"nonce-value\", qop=\"auth\"",
      &auth_token));
  EXPECT_EQ("Digest username=\"foo\", realm=\"Oblivion\", "
            "nonce=\"nonce-value\", uri=\"/path/to/resource\", "
            "response=\"5b1459beda5cee30d6ff9e970a69c0ea\", "
            "qop=auth, nc=00000001, cnonce=\"client_nonce\"",
            auth_token);
}

TEST(HttpAuthHandlerDigest, RespondToChallengeOpaque) {
  std::string auth_token;
  EXPECT_TRUE(RespondToChallenge(
      HttpAuth::AUTH_SERVER,
      std::string(),
      "http://www.example.com/path/to/resource",
      "Digest realm=\"Oblivion\", nonce=\"nonce-value\", "
      "qop=\"auth\", opaque=\"opaque text\"",
      &auth_token));
  EXPECT_EQ("Digest username=\"foo\", realm=\"Oblivion\", "
            "nonce=\"nonce-value\", uri=\"/path/to/resource\", "
            "response=\"5b1459beda5cee30d6ff9e970a69c0ea\", "
            "opaque=\"opaque text\", "
            "qop=auth, nc=00000001, cnonce=\"client_nonce\"",
            auth_token);
}


} // namespace net
