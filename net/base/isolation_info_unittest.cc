// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/base/isolation_info.h"

#include "base/test/scoped_feature_list.h"
#include "base/unguessable_token.h"
#include "net/base/features.h"
#include "net/base/network_isolation_key.h"
#include "net/base/schemeful_site.h"
#include "net/cookies/site_for_cookies.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/abseil-cpp/absl/types/optional.h"
#include "url/gurl.h"
#include "url/origin.h"
#include "url/url_util.h"

namespace net {

namespace {

class IsolationInfoTest : public testing::Test,
                          public testing::WithParamInterface<bool> {
 public:
  IsolationInfoTest() {
    if (ForceIsolationInfoFrameOriginToTopLevelFrameEnabled()) {
      scoped_feature_list_.InitAndEnableFeature(
          net::features::kForceIsolationInfoFrameOriginToTopLevelFrame);
    } else {
      scoped_feature_list_.InitAndDisableFeature(
          net::features::kForceIsolationInfoFrameOriginToTopLevelFrame);
    }
  }
  static bool ForceIsolationInfoFrameOriginToTopLevelFrameEnabled() {
    return GetParam();
  }

  const url::Origin kOrigin1 = url::Origin::Create(GURL("https://a.foo.test"));
  const url::Origin kSite1 = url::Origin::Create(GURL("https://foo.test"));
  const url::Origin kOrigin2 = url::Origin::Create(GURL("https://b.bar.test"));
  const url::Origin kSite2 = url::Origin::Create(GURL("https://bar.test"));
  const url::Origin kOrigin3 = url::Origin::Create(GURL("https://c.baz.test"));
  const url::Origin kOpaqueOrigin;

  const absl::optional<std::set<net::SchemefulSite>> kPartyContextNull =
      absl::nullopt;
  const absl::optional<std::set<net::SchemefulSite>> kPartyContextEmpty =
      std::set<net::SchemefulSite>();
  const absl::optional<std::set<net::SchemefulSite>> kPartyContext1 =
      std::set<net::SchemefulSite>{net::SchemefulSite(kOrigin1)};
  const absl::optional<std::set<net::SchemefulSite>> kPartyContext2 =
      std::set<net::SchemefulSite>{net::SchemefulSite(kOrigin2)};
  const absl::optional<std::set<net::SchemefulSite>> kPartyContext3 =
      std::set<net::SchemefulSite>{net::SchemefulSite(kOrigin3)};
  const absl::optional<std::set<net::SchemefulSite>> kPartyContextMultiple =
      std::set<net::SchemefulSite>{net::SchemefulSite(kOrigin1),
                                   net::SchemefulSite(kOrigin2)};

  const base::UnguessableToken kNonce1 = base::UnguessableToken::Create();
  const base::UnguessableToken kNonce2 = base::UnguessableToken::Create();

 private:
  base::test::ScopedFeatureList scoped_feature_list_;
};

INSTANTIATE_TEST_SUITE_P(
    All,
    IsolationInfoTest,
    /*force_isolation_info_frame_origin_to_top_level_frame=*/testing::Bool());

void DuplicateAndCompare(const IsolationInfo& isolation_info) {
  absl::optional<IsolationInfo> duplicate_isolation_info =
      IsolationInfo::CreateIfConsistent(
          isolation_info.request_type(), isolation_info.top_frame_origin(),
          IsolationInfoTest::
                  ForceIsolationInfoFrameOriginToTopLevelFrameEnabled()
              ? isolation_info.top_frame_origin()
              : isolation_info.frame_origin(),
          isolation_info.site_for_cookies(), isolation_info.party_context(),
          isolation_info.nonce().has_value() ? &isolation_info.nonce().value()
                                             : nullptr);

  ASSERT_TRUE(duplicate_isolation_info);
  EXPECT_TRUE(isolation_info.IsEqualForTesting(*duplicate_isolation_info));
}

TEST_P(IsolationInfoTest, RequestTypeMainFrame) {
  IsolationInfo isolation_info = IsolationInfo::Create(
      IsolationInfo::RequestType::kMainFrame, kOrigin1, kOrigin1,
      SiteForCookies::FromOrigin(kOrigin1), kPartyContextEmpty);
  EXPECT_EQ(IsolationInfo::RequestType::kMainFrame,
            isolation_info.request_type());
  EXPECT_EQ(kOrigin1, isolation_info.top_frame_origin());

  if (ForceIsolationInfoFrameOriginToTopLevelFrameEnabled()) {
    EXPECT_EQ(kOrigin1, isolation_info.frame_origin());
  } else {
    EXPECT_EQ(kOrigin1, isolation_info.frame_origin());
  }
  EXPECT_TRUE(isolation_info.network_isolation_key().IsFullyPopulated());
  EXPECT_FALSE(isolation_info.network_isolation_key().IsTransient());
  EXPECT_EQ("https://foo.test https://foo.test",
            isolation_info.network_isolation_key().ToString());
  EXPECT_TRUE(
      isolation_info.site_for_cookies().IsFirstParty(kOrigin1.GetURL()));
  EXPECT_EQ(kPartyContextEmpty, isolation_info.party_context());
  EXPECT_FALSE(isolation_info.nonce().has_value());

  DuplicateAndCompare(isolation_info);

  IsolationInfo redirected_isolation_info =
      isolation_info.CreateForRedirect(kOrigin3);
  EXPECT_EQ(IsolationInfo::RequestType::kMainFrame,
            redirected_isolation_info.request_type());
  EXPECT_EQ(kOrigin3, redirected_isolation_info.top_frame_origin());
  if (ForceIsolationInfoFrameOriginToTopLevelFrameEnabled()) {
    EXPECT_EQ(kOrigin3, redirected_isolation_info.frame_origin());
  } else {
    EXPECT_EQ(kOrigin3, redirected_isolation_info.frame_origin());
  }
  EXPECT_TRUE(
      redirected_isolation_info.network_isolation_key().IsFullyPopulated());
  EXPECT_FALSE(redirected_isolation_info.network_isolation_key().IsTransient());
  EXPECT_EQ("https://baz.test https://baz.test",
            redirected_isolation_info.network_isolation_key().ToString());
  EXPECT_TRUE(redirected_isolation_info.site_for_cookies().IsFirstParty(
      kOrigin3.GetURL()));
  EXPECT_EQ(kPartyContextEmpty, redirected_isolation_info.party_context());
  EXPECT_FALSE(redirected_isolation_info.nonce().has_value());
}

TEST_P(IsolationInfoTest, RequestTypeSubFrame) {
  IsolationInfo isolation_info = IsolationInfo::Create(
      IsolationInfo::RequestType::kSubFrame, kOrigin1, kOrigin2,
      SiteForCookies::FromOrigin(kOrigin1), kPartyContext1);
  EXPECT_EQ(IsolationInfo::RequestType::kSubFrame,
            isolation_info.request_type());
  EXPECT_EQ(kOrigin1, isolation_info.top_frame_origin());
  if (ForceIsolationInfoFrameOriginToTopLevelFrameEnabled()) {
    EXPECT_EQ(kOrigin1, isolation_info.frame_origin());
    EXPECT_EQ("https://foo.test https://foo.test",
              isolation_info.network_isolation_key().ToString());
  } else {
    EXPECT_EQ(kOrigin2, isolation_info.frame_origin());
    EXPECT_EQ("https://foo.test https://bar.test",
              isolation_info.network_isolation_key().ToString());
  }
  EXPECT_TRUE(isolation_info.network_isolation_key().IsFullyPopulated());
  EXPECT_FALSE(isolation_info.network_isolation_key().IsTransient());
  EXPECT_TRUE(
      isolation_info.site_for_cookies().IsFirstParty(kOrigin1.GetURL()));
  EXPECT_EQ(kPartyContext1, isolation_info.party_context());
  EXPECT_FALSE(isolation_info.nonce().has_value());

  DuplicateAndCompare(isolation_info);

  IsolationInfo redirected_isolation_info =
      isolation_info.CreateForRedirect(kOrigin3);
  EXPECT_EQ(IsolationInfo::RequestType::kSubFrame,
            redirected_isolation_info.request_type());
  EXPECT_EQ(kOrigin1, redirected_isolation_info.top_frame_origin());

  if (ForceIsolationInfoFrameOriginToTopLevelFrameEnabled()) {
    EXPECT_EQ(kOrigin1, redirected_isolation_info.frame_origin());
    EXPECT_EQ("https://foo.test https://foo.test",
              redirected_isolation_info.network_isolation_key().ToString());
  } else {
    EXPECT_EQ(kOrigin3, redirected_isolation_info.frame_origin());
    EXPECT_EQ("https://foo.test https://baz.test",
              redirected_isolation_info.network_isolation_key().ToString());
  }
  EXPECT_TRUE(
      redirected_isolation_info.network_isolation_key().IsFullyPopulated());
  EXPECT_FALSE(redirected_isolation_info.network_isolation_key().IsTransient());
  EXPECT_TRUE(redirected_isolation_info.site_for_cookies().IsFirstParty(
      kOrigin1.GetURL()));
  EXPECT_EQ(kPartyContext1, isolation_info.party_context());
  EXPECT_FALSE(redirected_isolation_info.nonce().has_value());
}

TEST_P(IsolationInfoTest, RequestTypeMainFrameWithNonce) {
  IsolationInfo isolation_info = IsolationInfo::Create(
      IsolationInfo::RequestType::kMainFrame, kOrigin1, kOrigin1,
      SiteForCookies::FromOrigin(kOrigin1), kPartyContextEmpty, &kNonce1);
  EXPECT_EQ(IsolationInfo::RequestType::kMainFrame,
            isolation_info.request_type());
  EXPECT_EQ(kOrigin1, isolation_info.top_frame_origin());
  if (ForceIsolationInfoFrameOriginToTopLevelFrameEnabled()) {
    EXPECT_EQ(kOrigin1, isolation_info.frame_origin());
  } else {
    EXPECT_EQ(kOrigin1, isolation_info.frame_origin());
  }
  EXPECT_TRUE(isolation_info.network_isolation_key().IsFullyPopulated());
  EXPECT_TRUE(isolation_info.network_isolation_key().IsTransient());
  EXPECT_EQ("", isolation_info.network_isolation_key().ToString());
  EXPECT_TRUE(
      isolation_info.site_for_cookies().IsFirstParty(kOrigin1.GetURL()));
  EXPECT_EQ(kPartyContextEmpty, isolation_info.party_context());
  EXPECT_EQ(kNonce1, isolation_info.nonce().value());

  DuplicateAndCompare(isolation_info);

  IsolationInfo redirected_isolation_info =
      isolation_info.CreateForRedirect(kOrigin3);
  EXPECT_EQ(IsolationInfo::RequestType::kMainFrame,
            redirected_isolation_info.request_type());
  EXPECT_EQ(kOrigin3, redirected_isolation_info.top_frame_origin());
  if (ForceIsolationInfoFrameOriginToTopLevelFrameEnabled()) {
    EXPECT_EQ(kOrigin3, redirected_isolation_info.frame_origin());
  } else {
    EXPECT_EQ(kOrigin3, redirected_isolation_info.frame_origin());
  }
  EXPECT_TRUE(
      redirected_isolation_info.network_isolation_key().IsFullyPopulated());
  EXPECT_TRUE(redirected_isolation_info.network_isolation_key().IsTransient());
  EXPECT_EQ("", redirected_isolation_info.network_isolation_key().ToString());
  EXPECT_TRUE(redirected_isolation_info.site_for_cookies().IsFirstParty(
      kOrigin3.GetURL()));
  EXPECT_EQ(kPartyContextEmpty, redirected_isolation_info.party_context());
  EXPECT_EQ(kNonce1, redirected_isolation_info.nonce().value());
}

TEST_P(IsolationInfoTest, RequestTypeSubFrameWithNonce) {
  IsolationInfo isolation_info = IsolationInfo::Create(
      IsolationInfo::RequestType::kSubFrame, kOrigin1, kOrigin2,
      SiteForCookies::FromOrigin(kOrigin1), kPartyContext1, &kNonce1);
  EXPECT_EQ(IsolationInfo::RequestType::kSubFrame,
            isolation_info.request_type());
  EXPECT_EQ(kOrigin1, isolation_info.top_frame_origin());
  if (ForceIsolationInfoFrameOriginToTopLevelFrameEnabled()) {
    EXPECT_EQ(kOrigin1, isolation_info.frame_origin());
  } else {
    EXPECT_EQ(kOrigin2, isolation_info.frame_origin());
  }
  EXPECT_TRUE(isolation_info.network_isolation_key().IsFullyPopulated());
  EXPECT_TRUE(isolation_info.network_isolation_key().IsTransient());
  EXPECT_EQ("", isolation_info.network_isolation_key().ToString());
  EXPECT_TRUE(
      isolation_info.site_for_cookies().IsFirstParty(kOrigin1.GetURL()));
  EXPECT_EQ(kPartyContext1, isolation_info.party_context());
  EXPECT_EQ(kNonce1, isolation_info.nonce().value());

  DuplicateAndCompare(isolation_info);

  IsolationInfo redirected_isolation_info =
      isolation_info.CreateForRedirect(kOrigin3);
  EXPECT_EQ(IsolationInfo::RequestType::kSubFrame,
            redirected_isolation_info.request_type());
  EXPECT_EQ(kOrigin1, redirected_isolation_info.top_frame_origin());
  if (ForceIsolationInfoFrameOriginToTopLevelFrameEnabled()) {
    EXPECT_EQ(kOrigin1, redirected_isolation_info.frame_origin());
  } else {
    EXPECT_EQ(kOrigin3, redirected_isolation_info.frame_origin());
  }
  EXPECT_TRUE(
      redirected_isolation_info.network_isolation_key().IsFullyPopulated());
  EXPECT_TRUE(redirected_isolation_info.network_isolation_key().IsTransient());
  EXPECT_EQ("", redirected_isolation_info.network_isolation_key().ToString());
  EXPECT_TRUE(redirected_isolation_info.site_for_cookies().IsFirstParty(
      kOrigin1.GetURL()));
  EXPECT_EQ(kPartyContext1, redirected_isolation_info.party_context());
  EXPECT_EQ(kNonce1, redirected_isolation_info.nonce().value());
}

TEST_P(IsolationInfoTest, RequestTypeOther) {
  IsolationInfo isolation_info;
  EXPECT_EQ(IsolationInfo::RequestType::kOther, isolation_info.request_type());
  EXPECT_FALSE(isolation_info.top_frame_origin());
  if (ForceIsolationInfoFrameOriginToTopLevelFrameEnabled()) {
    EXPECT_FALSE(isolation_info.frame_origin());
  } else {
    EXPECT_FALSE(isolation_info.frame_origin());
  }
  EXPECT_TRUE(isolation_info.network_isolation_key().IsEmpty());
  EXPECT_TRUE(isolation_info.site_for_cookies().IsNull());
  EXPECT_FALSE(isolation_info.party_context());
  EXPECT_FALSE(isolation_info.nonce());

  DuplicateAndCompare(isolation_info);

  IsolationInfo redirected_isolation_info =
      isolation_info.CreateForRedirect(kOrigin3);
  EXPECT_TRUE(isolation_info.IsEqualForTesting(redirected_isolation_info));
}

TEST_P(IsolationInfoTest, RequestTypeOtherWithSiteForCookies) {
  IsolationInfo isolation_info = IsolationInfo::Create(
      IsolationInfo::RequestType::kOther, kOrigin1, kOrigin1,
      SiteForCookies::FromOrigin(kOrigin1), kPartyContextEmpty);
  EXPECT_EQ(IsolationInfo::RequestType::kOther, isolation_info.request_type());
  EXPECT_EQ(kOrigin1, isolation_info.top_frame_origin());
  if (ForceIsolationInfoFrameOriginToTopLevelFrameEnabled()) {
    EXPECT_EQ(kOrigin1, isolation_info.frame_origin());
  } else {
    EXPECT_EQ(kOrigin1, isolation_info.frame_origin());
  }
  EXPECT_TRUE(isolation_info.network_isolation_key().IsFullyPopulated());
  EXPECT_FALSE(isolation_info.network_isolation_key().IsTransient());
  EXPECT_EQ("https://foo.test https://foo.test",
            isolation_info.network_isolation_key().ToString());
  EXPECT_TRUE(
      isolation_info.site_for_cookies().IsFirstParty(kOrigin1.GetURL()));
  EXPECT_EQ(kPartyContextEmpty, isolation_info.party_context());
  EXPECT_FALSE(isolation_info.nonce());

  DuplicateAndCompare(isolation_info);

  IsolationInfo redirected_isolation_info =
      isolation_info.CreateForRedirect(kOrigin3);
  EXPECT_TRUE(isolation_info.IsEqualForTesting(redirected_isolation_info));
}

// Test case of a subresource for cross-site subframe (which has an empty
// site-for-cookies).
TEST_P(IsolationInfoTest, RequestTypeOtherWithEmptySiteForCookies) {
  IsolationInfo isolation_info =
      IsolationInfo::Create(IsolationInfo::RequestType::kOther, kOrigin1,
                            kOrigin2, SiteForCookies(), kPartyContext2);
  EXPECT_EQ(IsolationInfo::RequestType::kOther, isolation_info.request_type());
  EXPECT_EQ(kOrigin1, isolation_info.top_frame_origin());
  if (ForceIsolationInfoFrameOriginToTopLevelFrameEnabled()) {
    EXPECT_EQ(kOrigin1, isolation_info.frame_origin());
    EXPECT_EQ("https://foo.test https://foo.test",
              isolation_info.network_isolation_key().ToString());
  } else {
    EXPECT_EQ(kOrigin2, isolation_info.frame_origin());
    EXPECT_EQ("https://foo.test https://bar.test",
              isolation_info.network_isolation_key().ToString());
  }
  EXPECT_TRUE(isolation_info.network_isolation_key().IsFullyPopulated());
  EXPECT_FALSE(isolation_info.network_isolation_key().IsTransient());
  EXPECT_TRUE(isolation_info.site_for_cookies().IsNull());
  EXPECT_EQ(kPartyContext2, isolation_info.party_context());
  EXPECT_FALSE(isolation_info.nonce());

  DuplicateAndCompare(isolation_info);

  IsolationInfo redirected_isolation_info =
      isolation_info.CreateForRedirect(kOrigin3);
  EXPECT_TRUE(isolation_info.IsEqualForTesting(redirected_isolation_info));
}

TEST_P(IsolationInfoTest, CreateTransient) {
  IsolationInfo isolation_info = IsolationInfo::CreateTransient();
  EXPECT_EQ(IsolationInfo::RequestType::kOther, isolation_info.request_type());
  EXPECT_TRUE(isolation_info.top_frame_origin()->opaque());
  if (ForceIsolationInfoFrameOriginToTopLevelFrameEnabled()) {
    EXPECT_TRUE(isolation_info.frame_origin()->opaque());
  } else {
    EXPECT_TRUE(isolation_info.frame_origin()->opaque());
  }
  EXPECT_TRUE(isolation_info.network_isolation_key().IsFullyPopulated());
  EXPECT_TRUE(isolation_info.network_isolation_key().IsTransient());
  EXPECT_TRUE(isolation_info.site_for_cookies().IsNull());
  EXPECT_FALSE(isolation_info.party_context());
  EXPECT_FALSE(isolation_info.nonce());

  DuplicateAndCompare(isolation_info);

  IsolationInfo redirected_isolation_info =
      isolation_info.CreateForRedirect(kOrigin3);
  EXPECT_TRUE(isolation_info.IsEqualForTesting(redirected_isolation_info));
}

TEST_P(IsolationInfoTest, CreateForInternalRequest) {
  IsolationInfo isolation_info =
      IsolationInfo::CreateForInternalRequest(kOrigin1);
  EXPECT_EQ(IsolationInfo::RequestType::kOther, isolation_info.request_type());
  EXPECT_EQ(kOrigin1, isolation_info.top_frame_origin());
  if (ForceIsolationInfoFrameOriginToTopLevelFrameEnabled()) {
    EXPECT_EQ(kOrigin1, isolation_info.frame_origin());
  } else {
    EXPECT_EQ(kOrigin1, isolation_info.frame_origin());
  }
  EXPECT_TRUE(isolation_info.network_isolation_key().IsFullyPopulated());
  EXPECT_FALSE(isolation_info.network_isolation_key().IsTransient());
  EXPECT_EQ("https://foo.test https://foo.test",
            isolation_info.network_isolation_key().ToString());
  EXPECT_TRUE(
      isolation_info.site_for_cookies().IsFirstParty(kOrigin1.GetURL()));
  EXPECT_EQ(kPartyContextEmpty, isolation_info.party_context());
  EXPECT_FALSE(isolation_info.nonce());

  DuplicateAndCompare(isolation_info);

  IsolationInfo redirected_isolation_info =
      isolation_info.CreateForRedirect(kOrigin3);
  EXPECT_TRUE(isolation_info.IsEqualForTesting(redirected_isolation_info));
}

TEST_P(IsolationInfoTest, CreatePartialUpdateTopFrame) {
  const NetworkIsolationKey kNIK{SchemefulSite(kOrigin1),
                                 SchemefulSite(kOrigin1)};
  IsolationInfo isolation_info = IsolationInfo::CreatePartial(
      IsolationInfo::RequestType::kMainFrame, kNIK);
  EXPECT_EQ(IsolationInfo::RequestType::kMainFrame,
            isolation_info.request_type());
  EXPECT_EQ(kSite1, isolation_info.top_frame_origin());
  if (ForceIsolationInfoFrameOriginToTopLevelFrameEnabled()) {
    EXPECT_EQ(kSite1, isolation_info.frame_origin());
  } else {
    EXPECT_EQ(kSite1, isolation_info.frame_origin());
  }
  EXPECT_EQ(kNIK, isolation_info.network_isolation_key());
  EXPECT_TRUE(isolation_info.site_for_cookies().IsNull());
  EXPECT_FALSE(isolation_info.party_context());
  EXPECT_FALSE(isolation_info.nonce());

  DuplicateAndCompare(isolation_info);
}

TEST_P(IsolationInfoTest, CreatePartialUpdateFrameOnly) {
  const NetworkIsolationKey kNIK{SchemefulSite(kOrigin1),
                                 SchemefulSite(kOrigin2)};
  IsolationInfo isolation_info =
      IsolationInfo::CreatePartial(IsolationInfo::RequestType::kSubFrame, kNIK);
  EXPECT_EQ(IsolationInfo::RequestType::kSubFrame,
            isolation_info.request_type());
  EXPECT_EQ(kSite1, isolation_info.top_frame_origin());
  if (ForceIsolationInfoFrameOriginToTopLevelFrameEnabled()) {
    EXPECT_EQ(kSite1, isolation_info.frame_origin());
  } else {
    EXPECT_EQ(kSite2, isolation_info.frame_origin());
  }
  EXPECT_EQ(kNIK, isolation_info.network_isolation_key());
  EXPECT_TRUE(isolation_info.site_for_cookies().IsNull());
  EXPECT_FALSE(isolation_info.party_context());
  EXPECT_FALSE(isolation_info.nonce());

  DuplicateAndCompare(isolation_info);
}

TEST_P(IsolationInfoTest, CreatePartialUpdateNothing) {
  const NetworkIsolationKey kNIK{SchemefulSite(kOrigin1),
                                 SchemefulSite(kOrigin2)};
  IsolationInfo isolation_info =
      IsolationInfo::CreatePartial(IsolationInfo::RequestType::kOther, kNIK);
  EXPECT_EQ(IsolationInfo::RequestType::kOther, isolation_info.request_type());
  EXPECT_EQ(kSite1, isolation_info.top_frame_origin());
  if (ForceIsolationInfoFrameOriginToTopLevelFrameEnabled()) {
    EXPECT_EQ(kSite1, isolation_info.frame_origin());
  } else {
    EXPECT_EQ(kSite2, isolation_info.frame_origin());
  }
  EXPECT_EQ(kNIK, isolation_info.network_isolation_key());
  EXPECT_TRUE(isolation_info.site_for_cookies().IsNull());
  EXPECT_FALSE(isolation_info.party_context());
  EXPECT_FALSE(isolation_info.nonce());

  DuplicateAndCompare(isolation_info);
}

TEST_P(IsolationInfoTest, CreatePartialTransient) {
  const NetworkIsolationKey kNIK = NetworkIsolationKey::CreateTransient();
  IsolationInfo isolation_info =
      IsolationInfo::CreatePartial(IsolationInfo::RequestType::kOther, kNIK);
  EXPECT_EQ(IsolationInfo::RequestType::kOther, isolation_info.request_type());
  EXPECT_EQ(kNIK.GetTopFrameSite(),
            SchemefulSite(*isolation_info.top_frame_origin()));
  if (ForceIsolationInfoFrameOriginToTopLevelFrameEnabled()) {
    EXPECT_EQ(kNIK.GetFrameSite(),
              SchemefulSite(*isolation_info.frame_origin()));
  } else {
    EXPECT_EQ(kNIK.GetFrameSite(),
              SchemefulSite(*isolation_info.frame_origin()));
  }
  EXPECT_EQ(kNIK, isolation_info.network_isolation_key());
  EXPECT_TRUE(isolation_info.site_for_cookies().IsNull());
  EXPECT_FALSE(isolation_info.party_context());
  EXPECT_FALSE(isolation_info.nonce());

  DuplicateAndCompare(isolation_info);
}

TEST_P(IsolationInfoTest, CreatePartialEmpty) {
  IsolationInfo isolation_info = IsolationInfo::CreatePartial(
      IsolationInfo::RequestType::kOther, NetworkIsolationKey());
  EXPECT_EQ(IsolationInfo::RequestType::kOther, isolation_info.request_type());
  EXPECT_FALSE(isolation_info.top_frame_origin());
  if (ForceIsolationInfoFrameOriginToTopLevelFrameEnabled()) {
    EXPECT_FALSE(isolation_info.frame_origin());
  } else {
    EXPECT_FALSE(isolation_info.frame_origin());
  }
  EXPECT_EQ(NetworkIsolationKey(), isolation_info.network_isolation_key());
  EXPECT_TRUE(isolation_info.site_for_cookies().IsNull());
  EXPECT_FALSE(isolation_info.party_context());
  EXPECT_FALSE(isolation_info.nonce());

  DuplicateAndCompare(isolation_info);
}

// Test that in the UpdateNothing case, the SiteForCookies does not have to
// match the frame origin, unlike in the HTTP/HTTPS case.
TEST_P(IsolationInfoTest, CustomSchemeRequestTypeOther) {
  // Have to register the scheme, or url::Origin::Create() will return an opaque
  // origin.
  url::ScopedSchemeRegistryForTests scoped_registry;
  url::AddStandardScheme("foo", url::SCHEME_WITH_HOST);

  const GURL kCustomOriginUrl = GURL("foo://a.foo.com");
  const url::Origin kCustomOrigin = url::Origin::Create(kCustomOriginUrl);

  IsolationInfo isolation_info = IsolationInfo::Create(
      IsolationInfo::RequestType::kOther, kCustomOrigin, kOrigin1,
      SiteForCookies::FromOrigin(kCustomOrigin), kPartyContext1);
  EXPECT_EQ(IsolationInfo::RequestType::kOther, isolation_info.request_type());
  EXPECT_EQ(kCustomOrigin, isolation_info.top_frame_origin());
  if (ForceIsolationInfoFrameOriginToTopLevelFrameEnabled()) {
    EXPECT_EQ(kCustomOrigin, isolation_info.frame_origin());
    EXPECT_EQ("foo://a.foo.com foo://a.foo.com",
              isolation_info.network_isolation_key().ToString());
  } else {
    EXPECT_EQ(kOrigin1, isolation_info.frame_origin());
    EXPECT_EQ("foo://a.foo.com https://foo.test",
              isolation_info.network_isolation_key().ToString());
  }
  EXPECT_TRUE(isolation_info.network_isolation_key().IsFullyPopulated());
  EXPECT_FALSE(isolation_info.network_isolation_key().IsTransient());
  EXPECT_TRUE(isolation_info.site_for_cookies().IsFirstParty(kCustomOriginUrl));
  EXPECT_EQ(kPartyContext1, isolation_info.party_context());
  EXPECT_FALSE(isolation_info.nonce());

  DuplicateAndCompare(isolation_info);

  IsolationInfo redirected_isolation_info =
      isolation_info.CreateForRedirect(kOrigin2);
  EXPECT_TRUE(isolation_info.IsEqualForTesting(redirected_isolation_info));
}

// Success cases are covered by other tests, so only need a separate test to
// cover the failure cases.
TEST_P(IsolationInfoTest, CreateIfConsistentFails) {
  // Main frames with inconsistent SiteForCookies.
  EXPECT_FALSE(IsolationInfo::CreateIfConsistent(
      IsolationInfo::RequestType::kMainFrame, kOrigin1, kOrigin1,
      SiteForCookies::FromOrigin(kOrigin2)));
  EXPECT_FALSE(IsolationInfo::CreateIfConsistent(
      IsolationInfo::RequestType::kMainFrame, kOpaqueOrigin, kOpaqueOrigin,
      SiteForCookies::FromOrigin(kOrigin1)));

  // Sub frame with inconsistent SiteForCookies.
  EXPECT_FALSE(IsolationInfo::CreateIfConsistent(
      IsolationInfo::RequestType::kSubFrame, kOrigin1, kOrigin2,
      SiteForCookies::FromOrigin(kOrigin2)));

  // Sub resources with inconsistent SiteForCookies.
  EXPECT_FALSE(IsolationInfo::CreateIfConsistent(
      IsolationInfo::RequestType::kOther, kOrigin1, kOrigin2,
      SiteForCookies::FromOrigin(kOrigin1)));
  EXPECT_FALSE(IsolationInfo::CreateIfConsistent(
      IsolationInfo::RequestType::kOther, kOrigin1, kOrigin2,
      SiteForCookies::FromOrigin(kOrigin2)));

  // Correctly have empty/non-empty origins:
  EXPECT_TRUE(IsolationInfo::CreateIfConsistent(
      IsolationInfo::RequestType::kOther, absl::nullopt, absl::nullopt,
      SiteForCookies()));

  // Incorrectly have empty/non-empty origins:
  EXPECT_FALSE(IsolationInfo::CreateIfConsistent(
      IsolationInfo::RequestType::kOther, absl::nullopt, kOrigin1,
      SiteForCookies()));
  EXPECT_FALSE(IsolationInfo::CreateIfConsistent(
      IsolationInfo::RequestType::kOther, kOrigin1, absl::nullopt,
      SiteForCookies()));
  EXPECT_FALSE(IsolationInfo::CreateIfConsistent(
      IsolationInfo::RequestType::kMainFrame, absl::nullopt, kOrigin1,
      SiteForCookies::FromOrigin(kOrigin1)));
  EXPECT_FALSE(IsolationInfo::CreateIfConsistent(
      IsolationInfo::RequestType::kMainFrame, kOrigin1, absl::nullopt,
      SiteForCookies::FromOrigin(kOrigin1)));
  EXPECT_FALSE(IsolationInfo::CreateIfConsistent(
      IsolationInfo::RequestType::kSubFrame, absl::nullopt, kOrigin2,
      SiteForCookies()));
  EXPECT_FALSE(IsolationInfo::CreateIfConsistent(
      IsolationInfo::RequestType::kSubFrame, kOrigin1, absl::nullopt,
      SiteForCookies()));

  // No origins with non-null SiteForCookies.
  EXPECT_FALSE(IsolationInfo::CreateIfConsistent(
      IsolationInfo::RequestType::kOther, absl::nullopt, absl::nullopt,
      SiteForCookies::FromOrigin(kOrigin1)));

  // No origins with non-null party_context.
  EXPECT_FALSE(IsolationInfo::CreateIfConsistent(
      IsolationInfo::RequestType::kOther, absl::nullopt, absl::nullopt,
      SiteForCookies(), kPartyContextEmpty));

  // No origins with non-null nonce.
  EXPECT_FALSE(IsolationInfo::CreateIfConsistent(
      IsolationInfo::RequestType::kOther, absl::nullopt, absl::nullopt,
      SiteForCookies(), absl::nullopt /* party_context */, &kNonce1));
}

TEST_P(IsolationInfoTest, CreateForRedirectPartyContext) {
  // RequestTypeMainFrame, PartyContext is empty
  {
    IsolationInfo isolation_info = IsolationInfo::Create(
        IsolationInfo::RequestType::kMainFrame, kOrigin1, kOrigin1,
        SiteForCookies::FromOrigin(kOrigin1), kPartyContextEmpty);
    IsolationInfo redirected_isolation_info =
        isolation_info.CreateForRedirect(kOrigin3);
    EXPECT_EQ(kPartyContextEmpty, redirected_isolation_info.party_context());
  }
  // RequestTypeSubFrame, PartyContext is empty
  {
    IsolationInfo isolation_info = IsolationInfo::Create(
        IsolationInfo::RequestType::kSubFrame, kOrigin1, kOrigin2,
        SiteForCookies::FromOrigin(kOrigin1), kPartyContextEmpty);
    IsolationInfo redirected_isolation_info =
        isolation_info.CreateForRedirect(kOrigin3);
    EXPECT_EQ(kPartyContextEmpty, redirected_isolation_info.party_context());
  }
  // RequestTypeSubFrame, PartyContext not empty
  {
    IsolationInfo isolation_info = IsolationInfo::Create(
        IsolationInfo::RequestType::kSubFrame, kOrigin1, kOrigin2,
        SiteForCookies::FromOrigin(kOrigin1), kPartyContext1);
    IsolationInfo redirected_isolation_info =
        isolation_info.CreateForRedirect(kOrigin3);
    EXPECT_EQ(kPartyContext1, redirected_isolation_info.party_context());
  }
  // RequestTypeOther, PartyContext not empty
  {
    IsolationInfo isolation_info =
        IsolationInfo::Create(IsolationInfo::RequestType::kOther, kOrigin1,
                              kOrigin2, SiteForCookies(), kPartyContext2);
    IsolationInfo redirected_isolation_info =
        isolation_info.CreateForRedirect(kOrigin3);
    EXPECT_EQ(kPartyContext2, redirected_isolation_info.party_context());
  }
}

TEST_P(IsolationInfoTest, Serialization) {
  EXPECT_FALSE(IsolationInfo::Deserialize(""));
  EXPECT_FALSE(IsolationInfo::Deserialize("garbage"));

  const IsolationInfo kPositiveTestCases[] = {
      IsolationInfo::Create(IsolationInfo::RequestType::kSubFrame, kOrigin1,
                            kOrigin2, SiteForCookies::FromOrigin(kOrigin1),
                            kPartyContext1),
      // Null party context
      IsolationInfo::Create(IsolationInfo::RequestType::kSubFrame, kOrigin1,
                            kOrigin2, SiteForCookies::FromOrigin(kOrigin1),
                            kPartyContextNull),
      // Empty party context
      IsolationInfo::Create(IsolationInfo::RequestType::kSubFrame, kOrigin1,
                            kOrigin2, SiteForCookies::FromOrigin(kOrigin1),
                            kPartyContextEmpty),
      // Multiple party context entries.
      IsolationInfo::Create(IsolationInfo::RequestType::kSubFrame, kOrigin1,
                            kOrigin2, SiteForCookies::FromOrigin(kOrigin1),
                            kPartyContextMultiple),
      // Without SiteForCookies
      IsolationInfo::Create(IsolationInfo::RequestType::kSubFrame, kOrigin1,
                            kOrigin2, SiteForCookies(), absl::nullopt),
      // Request type kOther
      IsolationInfo::Create(IsolationInfo::RequestType::kOther, kOrigin1,
                            kOrigin1, SiteForCookies::FromOrigin(kOrigin1),
                            absl::nullopt),
      // Request type kMainframe
      IsolationInfo::Create(IsolationInfo::RequestType::kMainFrame, kOrigin1,
                            kOrigin1, SiteForCookies::FromOrigin(kOrigin1),
                            absl::nullopt),
  };
  for (const auto& info : kPositiveTestCases) {
    auto rt = IsolationInfo::Deserialize(info.Serialize());
    ASSERT_TRUE(rt);
    EXPECT_TRUE(rt->IsEqualForTesting(info));
  }

  const IsolationInfo kNegativeTestCases[] = {
      IsolationInfo::CreateTransient(),
      // With nonce (i.e transient).
      IsolationInfo::Create(IsolationInfo::RequestType::kSubFrame, kOrigin1,
                            kOrigin2, SiteForCookies::FromOrigin(kOrigin1),
                            kPartyContext1, &kNonce1),
      // With an opaque origin (i.e transient)
      IsolationInfo::Create(IsolationInfo::RequestType::kSubFrame, kOrigin1,
                            url::Origin(), SiteForCookies::FromOrigin(kOrigin1),
                            absl::nullopt),
  };
  const IsolationInfo kNegativeWhenDoubleKeyEnabledTestCases[] = {
      IsolationInfo::CreateTransient(),
      // With nonce (i.e transient).
      IsolationInfo::Create(IsolationInfo::RequestType::kSubFrame, kOrigin1,
                            kOrigin2, SiteForCookies::FromOrigin(kOrigin1),
                            kPartyContext1, &kNonce1),
  };
  if (ForceIsolationInfoFrameOriginToTopLevelFrameEnabled()) {
    for (const auto& info : kNegativeWhenDoubleKeyEnabledTestCases) {
      EXPECT_TRUE(info.Serialize().empty());
    }

  } else {
    for (const auto& info : kNegativeTestCases) {
      EXPECT_TRUE(info.Serialize().empty());
    }
  }
}

}  // namespace

}  // namespace net
