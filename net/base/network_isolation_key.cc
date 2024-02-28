// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/base/network_isolation_key.h"

#include <cstddef>
#include <optional>
#include <string>

#include "base/unguessable_token.h"
#include "net/base/features.h"
#include "net/base/registry_controlled_domains/registry_controlled_domain.h"
#include "schemeful_site.h"
#include "url/gurl.h"
#include "url/origin.h"
#include "url/url_constants.h"

namespace net {

namespace {

std::string GetSiteDebugString(const std::optional<SchemefulSite>& site) {
  return site ? site->GetDebugString() : "null";
}

}  // namespace

NetworkIsolationKey::NetworkIsolationKey(
    const SchemefulSite& top_frame_site,
    const SchemefulSite& frame_site,
    const std::optional<base::UnguessableToken>& nonce)
    : NetworkIsolationKey(SchemefulSite(top_frame_site),
                          SchemefulSite(frame_site),
                          std::optional<base::UnguessableToken>(nonce)) {}

NetworkIsolationKey::NetworkIsolationKey(
    SchemefulSite&& top_frame_site,
    SchemefulSite&& frame_site,
    std::optional<base::UnguessableToken>&& nonce)
    : top_frame_site_(std::move(top_frame_site)),
      frame_site_(std::make_optional(std::move(frame_site))),
      is_cross_site_((GetMode() == Mode::kCrossSiteFlagEnabled)
                         ? std::make_optional(*top_frame_site_ != *frame_site_)
                         : std::nullopt),
      nonce_(std::move(nonce)) {
  DCHECK(!nonce_ || !nonce_->is_empty());
}

NetworkIsolationKey::NetworkIsolationKey(const url::Origin& top_frame_origin,
                                         const url::Origin& frame_origin)
    : NetworkIsolationKey(SchemefulSite(top_frame_origin),
                          SchemefulSite(frame_origin)) {}

NetworkIsolationKey::NetworkIsolationKey() = default;

NetworkIsolationKey::NetworkIsolationKey(
    const NetworkIsolationKey& network_isolation_key) = default;

NetworkIsolationKey::NetworkIsolationKey(
    NetworkIsolationKey&& network_isolation_key) = default;

NetworkIsolationKey::~NetworkIsolationKey() = default;

NetworkIsolationKey& NetworkIsolationKey::operator=(
    const NetworkIsolationKey& network_isolation_key) = default;

NetworkIsolationKey& NetworkIsolationKey::operator=(
    NetworkIsolationKey&& network_isolation_key) = default;

NetworkIsolationKey NetworkIsolationKey::CreateTransientForTesting() {
  SchemefulSite site_with_opaque_origin;
  return NetworkIsolationKey(site_with_opaque_origin, site_with_opaque_origin);
}

NetworkIsolationKey NetworkIsolationKey::CreateWithNewFrameSite(
    const SchemefulSite& new_frame_site) const {
  if (!top_frame_site_)
    return NetworkIsolationKey();
  return NetworkIsolationKey(top_frame_site_.value(), new_frame_site, nonce_);
}

std::optional<std::string> NetworkIsolationKey::ToCacheKeyString() const {
  if (IsTransient())
    return std::nullopt;

  std::string variable_key_piece;
  switch (GetMode()) {
    case Mode::kFrameSiteEnabled:
      variable_key_piece = frame_site_->Serialize();
      break;
    case Mode::kFrameSiteWithSharedOpaqueEnabled:
      if (frame_site_->opaque()) {
        variable_key_piece = "_opaque";
        break;
      }
      variable_key_piece = frame_site_->Serialize();
      break;
    case Mode::kCrossSiteFlagEnabled:
      variable_key_piece = (*is_cross_site_ ? "_1" : "_0");
      break;
  }
  return top_frame_site_->Serialize() + " " + variable_key_piece;
}

std::string NetworkIsolationKey::ToDebugString() const {
  // The space-separated serialization of |top_frame_site_| and
  // |frame_site_|.
  std::string return_string = GetSiteDebugString(top_frame_site_);
  switch (GetMode()) {
    case Mode::kFrameSiteEnabled:
      return_string += " " + GetSiteDebugString(frame_site_);
      break;
    case Mode::kFrameSiteWithSharedOpaqueEnabled:
      if (frame_site_ && frame_site_->opaque()) {
        return_string += " opaque-origin";
        break;
      }
      return_string += " " + GetSiteDebugString(frame_site_);
      break;
    case Mode::kCrossSiteFlagEnabled:
      if (is_cross_site_.has_value()) {
        return_string += (*is_cross_site_ ? " cross-site" : " same-site");
      }
      break;
  }

  if (nonce_.has_value()) {
    return_string += " (with nonce " + nonce_->ToString() + ")";
  }

  return return_string;
}

bool NetworkIsolationKey::IsFullyPopulated() const {
  if (!top_frame_site_.has_value()) {
    return false;
  }
  if (GetMode() == Mode::kFrameSiteEnabled && !frame_site_.has_value()) {
    return false;
  }
  return true;
}

bool NetworkIsolationKey::IsTransient() const {
  if (!IsFullyPopulated())
    return true;
  return IsOpaque();
}

// static
NetworkIsolationKey::Mode NetworkIsolationKey::GetMode() {
  if (base::FeatureList::IsEnabled(
          net::features::kEnableCrossSiteFlagNetworkIsolationKey)) {
    DCHECK(!base::FeatureList::IsEnabled(
        net::features::kEnableFrameSiteSharedOpaqueNetworkIsolationKey));
    return Mode::kCrossSiteFlagEnabled;
  } else if (base::FeatureList::IsEnabled(
                 net::features::
                     kEnableFrameSiteSharedOpaqueNetworkIsolationKey)) {
    return Mode::kFrameSiteWithSharedOpaqueEnabled;
  } else {
    return Mode::kFrameSiteEnabled;
  }
}

bool NetworkIsolationKey::IsEmpty() const {
  return !top_frame_site_.has_value() && !frame_site_.has_value();
}

bool NetworkIsolationKey::IsOpaque() const {
  if (top_frame_site_->opaque()) {
    return true;
  }
  // For Mode::kCrossSiteFlagEnabled and Mode::kFrameSiteWithSharedOpaqueEnabled
  // we don't want to treat NIKs for opaque origin frames as opaque.
  if (GetMode() == Mode::kFrameSiteEnabled && frame_site_->opaque()) {
    return true;
  }
  if (nonce_.has_value()) {
    return true;
  }
  return false;
}

}  // namespace net
