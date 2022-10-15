// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_BASE_NETWORK_ANONYMIZATION_KEY_H_
#define NET_BASE_NETWORK_ANONYMIZATION_KEY_H_

#include <string>
#include <tuple>

#include "base/unguessable_token.h"
#include "net/base/net_export.h"
#include "net/base/schemeful_site.h"
#include "third_party/abseil-cpp/absl/types/optional.h"

namespace net {

// NetworkAnonymizationKey will be used to partition shared network state based
// on the context on which they were made. This class is an expiremental key
// that contains properties that will be changed via feature flags.

// NetworkAnonymizationKey contains the following properties:

// `top_frame_site` represents the SchemefulSite of the pages top level frame.
// In order to separate first and third party context from each other this field
// will always be populated.

// `frame_site` represents the SchemefulSite of the requestor frame. This will
// be empty when kEnableDoubleKeyNetworkAnonymizationKey is enabled.

//`is_cross_site` is an expiremental boolean that will be used with the
//`top_frame_site` to create a partition key that separates the
//`top_frame_site`s first party partition from any cross-site iframes. This will
// be used only when `kEnableCrossSiteFlagNetworkAnonymizationKey` is enabled.
// When `kEnableCrossSiteFlagNetworkAnonymizationKey` is disabled,
// `is_cross_site_` will be an empty optional.

// The following show how the `is_cross_site` boolean is populated for the
// innermost frame in the chain.
// a->a => is_cross_site = true
// a->b => is_cross_site = false
// a->b->a => is_cross_site = false
// a->(sandboxed a [has nonce]) => is_cross_site = true

// The `nonce` value creates a key for anonymous iframes by giving them a
// temporary `nonce` value which changes per top level navigation. For now, any
// NetworkAnonymizationKey with a nonce will be considered transient. This is
// being considered to possibly change in the future in an effort to allow
// anonymous iframes with the same partition key access to shared resources.
// The nonce value will be empty except for anonymous iframes.

// TODO @brgoldstein, add link to public documentation of key scheme naming
// conventions.

class NET_EXPORT NetworkAnonymizationKey {
 public:
  NetworkAnonymizationKey(
      const SchemefulSite& top_frame_site,
      const absl::optional<SchemefulSite>& frame_site = absl::nullopt,
      const absl::optional<bool> is_cross_site = absl::nullopt,
      const absl::optional<base::UnguessableToken> nonce = absl::nullopt);

  // Construct an empty key.
  NetworkAnonymizationKey();

  NetworkAnonymizationKey(
      const NetworkAnonymizationKey& network_anonymization_key);
  NetworkAnonymizationKey(NetworkAnonymizationKey&& network_anonymization_key);

  ~NetworkAnonymizationKey();

  NetworkAnonymizationKey& operator=(
      const NetworkAnonymizationKey& network_anonymization_key);
  NetworkAnonymizationKey& operator=(
      NetworkAnonymizationKey&& network_anonymization_key);

  // Compare keys for equality, true if all enabled fields are equal.
  bool operator==(const NetworkAnonymizationKey& other) const {
    return std::tie(top_frame_site_, frame_site_, is_cross_site_, nonce_) ==
           std::tie(other.top_frame_site_, other.frame_site_,
                    other.is_cross_site_, other.nonce_);
  }

  // Compare keys for inequality, true if any enabled field varies.
  bool operator!=(const NetworkAnonymizationKey& other) const {
    return !(*this == other);
  }

  // Provide an ordering for keys based on all enabled fields.
  bool operator<(const NetworkAnonymizationKey& other) const {
    return std::tie(top_frame_site_, frame_site_, is_cross_site_, nonce_) <
           std::tie(other.top_frame_site_, other.frame_site_,
                    other.is_cross_site_, other.nonce_);
  }

  // Returns the string representation of the key.
  std::string ToDebugString() const;

  // Returns true if all parts of the key are empty.
  bool IsEmpty() const;

  // Returns true if `top_frame_site_` and `frame_site_` of the key are
  // non-empty.
  bool IsFullyPopulated() const;

  // Returns true if this key's lifetime is short-lived. It may not make sense
  // to persist state to disk related to it (e.g., disk cache).
  // A NetworkAnonymizationKey will be considered transient if either
  // `top_frame_site_` or `frame_site_` are empty or opaque or if the key has a
  // `nonce_`.
  bool IsTransient() const;

  // Getters for the top frame, frame site, nonce and is cross site flag.
  const absl::optional<SchemefulSite>& GetTopFrameSite() const {
    return top_frame_site_;
  }

  const absl::optional<SchemefulSite>& GetFrameSite() const;

  // Do not use outside of testing. Returns the `frame_site_` if neither
  // `kEnableCrossSiteFlagNetworkAnonymizationKey` or
  // `kEnableDoubleKeyNetworkAnonymizationKey` are enabled. Else it
  // returns nullopt.
  const absl::optional<SchemefulSite>& GetFrameSiteForTesting() const {
    return frame_site_;
  }

  bool GetIsCrossSite() const;

  const absl::optional<base::UnguessableToken>& GetNonce() const {
    return nonce_;
  }

  // Returns true if the NetworkAnonymizationKey has a triple keyed scheme. This
  // means the values of the NetworkAnonymizationKey are as follows:
  // `top_frame_site` -> the schemeful site of the top level page.
  // `frame_site ` -> the schemeful site of the requestor frame
  // `is_cross_site` -> nullopt
  static bool IsFrameSiteEnabled();

  // Returns true if the NetworkAnonymizationKey has a double keyed scheme. This
  // means the values of the NetworkAnonymizationKey are as follows:
  // `top_frame_site` -> the schemeful site of the top level page.
  // `frame_site ` -> nullopt
  // `is_cross_site` -> nullopt
  static bool IsDoubleKeySchemeEnabled();

  // Returns true if the NetworkAnonymizationKey has a <double keyed +
  // is_cross_site> scheme. This means the values of the NetworkAnonymizationKey
  // are as follows:
  // `top_frame_site` -> the schemeful site of the top level page.
  // `frame_site ` -> nullopt
  // `is_cross_site` -> a boolean indicating if the requestor frame site is
  // cross site from the top level site.
  static bool IsCrossSiteFlagSchemeEnabled();

 private:
  std::string GetSiteDebugString(
      const absl::optional<SchemefulSite>& site) const;
  // The origin/etld+1 of the top frame of the page making the request. This
  // will always be populated unless all other fields are also nullopt.
  absl::optional<SchemefulSite> top_frame_site_;

  // The origin/etld+1 of the frame that initiates the request.
  absl::optional<SchemefulSite> frame_site_;

  // True if the frame site is cross site when compared to the top frame site.
  absl::optional<bool> is_cross_site_;

  // for non-opaque origins.
  absl::optional<base::UnguessableToken> nonce_;
};

}  // namespace net

#endif  // NET_BASE_NETWORK_ANONYMIZATION_KEY_H_