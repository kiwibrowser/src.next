// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_FRAME_ATTRIBUTION_SRC_LOADER_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_FRAME_ATTRIBUTION_SRC_LOADER_H_

#include <stdint.h>

#include "components/attribution_reporting/registration_eligibility.mojom-blink-forward.h"
#include "services/network/public/cpp/attribution_reporting_runtime_features.h"
#include "services/network/public/mojom/attribution.mojom-forward.h"
#include "third_party/abseil-cpp/absl/types/optional.h"
#include "third_party/blink/public/common/tokens/tokens.h"
#include "third_party/blink/renderer/core/core_export.h"
#include "third_party/blink/renderer/platform/heap/forward.h"
#include "third_party/blink/renderer/platform/heap/garbage_collected.h"
#include "third_party/blink/renderer/platform/heap/member.h"
#include "third_party/blink/renderer/platform/wtf/forward.h"

namespace network {
class TriggerVerification;
}  // namespace network

namespace attribution_reporting {
class SuitableOrigin;
}  // namespace attribution_reporting

namespace blink {

class HTMLAnchorElement;
class HTMLElement;
class KURL;
class LocalFrame;
class Resource;
class ResourceRequest;
class ResourceResponse;
class WebString;

template <typename T>
class WebVector;

struct Impression;

class CORE_EXPORT AttributionSrcLoader
    : public GarbageCollected<AttributionSrcLoader> {
 public:
  explicit AttributionSrcLoader(LocalFrame* frame);
  AttributionSrcLoader(const AttributionSrcLoader&) = delete;
  AttributionSrcLoader& operator=(const AttributionSrcLoader&) = delete;
  AttributionSrcLoader(AttributionSrcLoader&& other) = delete;
  AttributionSrcLoader& operator=(AttributionSrcLoader&& other) = delete;
  ~AttributionSrcLoader();

  // Registers zero or more attribution srcs by splitting `attribution_src` on
  // spaces and completing each token as a URL against the frame's document.
  // This method handles fetching each URL and notifying the browser process to
  // begin tracking it. It is a no-op if the frame is not attached.
  void Register(const AtomicString& attribution_src, HTMLElement* element);

  // Registers an attribution resource client for the given resource if
  // the request is eligible for attribution registration. Safe to call multiple
  // times for the same `resource`. Returns whether a registration was
  // successful.
  bool MaybeRegisterAttributionHeaders(const ResourceRequest& request,
                                       const ResourceResponse& response,
                                       const Resource* resource);

  // Splits `attribution_src` on spaces and completes each token as a URL
  // against the frame's document.
  //
  // For each URL eligible for Attribution Reporting, initiates a fetch for it,
  // and notifies the browser to begin tracking it.
  //
  // If at least one URL is eligible or `navigation_url` is, returns a
  // non-`absl::nullopt` `Impression` to live alongside the navigation.
  [[nodiscard]] absl::optional<Impression> RegisterNavigation(
      const KURL& navigation_url,
      const AtomicString& attribution_src,
      HTMLAnchorElement* element,
      bool has_transient_user_activation);

  // Same as the above, but uses an already-tokenized attribution src for use
  // with `window.open`.
  [[nodiscard]] absl::optional<Impression> RegisterNavigation(
      const KURL& navigation_url,
      const WebVector<WebString>& attribution_srcs,
      bool has_transient_user_activation);

  // Returns true if `url` can be used as an attributionsrc: its scheme is HTTP
  // or HTTPS, its origin is potentially trustworthy, the document's permission
  // policy supports Attribution Reporting, the window's context is secure, and
  // the Attribution Reporting runtime-enabled feature is enabled.
  //
  // Reports a DevTools issue using `element` and `request_id` otherwise, if
  // `log_issues` is true.
  [[nodiscard]] bool CanRegister(const KURL& url,
                                 HTMLElement* element,
                                 absl::optional<uint64_t> request_id,
                                 bool log_issues = true);

  void Trace(Visitor* visitor) const;

  network::mojom::AttributionSupport GetSupport() const;

  network::AttributionReportingRuntimeFeatures GetRuntimeFeatures() const;

 private:
  class ResourceClient;

  Vector<KURL> ParseAttributionSrc(const AtomicString& attribution_src,
                                   HTMLElement*);

  bool DoRegistration(const Vector<KURL>&, absl::optional<AttributionSrcToken>);

  [[nodiscard]] absl::optional<Impression> RegisterNavigationInternal(
      const KURL& navigation_url,
      Vector<KURL> attribution_src_urls,
      HTMLAnchorElement*,
      bool has_transient_user_activation);

  // Returns the reporting origin corresponding to `url` if its protocol is in
  // the HTTP family, its origin is potentially trustworthy, and attribution is
  // allowed. Returns `absl::nullopt` otherwise, and reports a DevTools issue
  // using `element` and `request_id if `log_issues` is true.
  absl::optional<attribution_reporting::SuitableOrigin>
  ReportingOriginForUrlIfValid(const KURL& url,
                               HTMLElement* element,
                               absl::optional<uint64_t> request_id,
                               bool log_issues = true);

  bool CreateAndSendRequests(Vector<KURL>,
                             HTMLElement*,
                             absl::optional<AttributionSrcToken>);

  struct AttributionHeaders;

  void RegisterAttributionHeaders(
      attribution_reporting::mojom::blink::RegistrationEligibility,
      network::mojom::AttributionSupport,
      attribution_reporting::SuitableOrigin reporting_origin,
      const AttributionHeaders&,
      const Vector<network::TriggerVerification>&);

  const Member<LocalFrame> local_frame_;
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_CORE_FRAME_ATTRIBUTION_SRC_LOADER_H_
