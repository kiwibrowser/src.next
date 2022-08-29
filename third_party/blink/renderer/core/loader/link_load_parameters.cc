// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/loader/link_load_parameters.h"

#include "services/network/public/mojom/referrer_policy.mojom-blink.h"
#include "third_party/blink/renderer/platform/loader/link_header.h"
#include "third_party/blink/renderer/platform/weborigin/security_policy.h"

namespace blink {

LinkLoadParameters::LinkLoadParameters(
    const LinkRelAttribute& rel,
    const CrossOriginAttributeValue& cross_origin,
    const String& type,
    const String& as,
    const String& media,
    const String& nonce,
    const String& integrity,
    const String& fetch_priority_hint,
    network::mojom::ReferrerPolicy referrer_policy,
    const KURL& href,
    const String& image_srcset,
    const String& image_sizes,
    const String& blocking,
    LinkLoadParameters::Reason reason)
    : rel(rel),
      cross_origin(cross_origin),
      type(type),
      as(as),
      media(media),
      nonce(nonce),
      integrity(integrity),
      fetch_priority_hint(fetch_priority_hint),
      referrer_policy(referrer_policy),
      href(href),
      image_srcset(image_srcset),
      image_sizes(image_sizes),
      blocking(blocking),
      reason(reason) {}

// TODO(domfarolino)
// Eventually we'll want to support a |fetchpriority| value on
// LinkHeaders. We can communicate a header's fetchpriority value
// to LinkLoadParameters here, likely after modifying the LinkHeader
// class. See https://crbug.com/821464 for info on Priority Hints.
LinkLoadParameters::LinkLoadParameters(const LinkHeader& header,
                                       const KURL& base_url)
    : rel(LinkRelAttribute(header.Rel())),
      cross_origin(GetCrossOriginAttributeValue(header.CrossOrigin())),
      type(header.MimeType()),
      as(header.As()),
      media(header.Media()),
      nonce(header.Nonce()),
      integrity(header.Integrity()),
      referrer_policy(network::mojom::ReferrerPolicy::kDefault),
      href(KURL(base_url, header.Url())),
      image_srcset(header.ImageSrcset()),
      image_sizes(header.ImageSizes()),
      blocking(header.Blocking()),
      reason(Reason::kDefault) {
  if (!header.ReferrerPolicy().IsEmpty()) {
    SecurityPolicy::ReferrerPolicyFromString(
        header.ReferrerPolicy(), kDoNotSupportReferrerPolicyLegacyKeywords,
        &referrer_policy);
  }
}

}  // namespace blink
