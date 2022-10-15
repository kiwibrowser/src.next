// Copyright 2010 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/base/proxy_server.h"

#include <stdint.h>

#include <ostream>
#include <string>

#include "base/check_op.h"
#include "base/numerics/safe_conversions.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_piece.h"
#include "net/base/proxy_string_util.h"
#include "third_party/abseil-cpp/absl/types/optional.h"
#include "url/third_party/mozilla/url_parse.h"
#include "url/url_canon.h"
#include "url/url_canon_stdstring.h"

namespace net {

ProxyServer::ProxyServer(Scheme scheme, const HostPortPair& host_port_pair)
      : scheme_(scheme), host_port_pair_(host_port_pair) {
  if (scheme_ == SCHEME_DIRECT || scheme_ == SCHEME_INVALID) {
    // |host_port_pair| isn't relevant for these special schemes, so none should
    // have been specified. It is important for this to be consistent since we
    // do raw field comparisons in the equality and comparison functions.
    DCHECK(host_port_pair.Equals(HostPortPair()));
    host_port_pair_ = HostPortPair();
  }
}

// static
ProxyServer ProxyServer::FromSchemeHostAndPort(Scheme scheme,
                                               base::StringPiece host,
                                               base::StringPiece port_str) {
  // Create INVALID proxies directly using `ProxyServer()`.
  DCHECK_NE(scheme, SCHEME_INVALID);

  // Create DIRECT proxies directly using `Direct()`.
  DCHECK_NE(scheme, SCHEME_DIRECT);

  int port_number =
      url::ParsePort(port_str.data(), url::Component(0, port_str.size()));
  if (port_number == url::PORT_UNSPECIFIED)
    return FromSchemeHostAndPort(scheme, host, absl::nullopt);
  if (port_number == url::PORT_INVALID)
    return ProxyServer();

  DCHECK(base::IsValueInRangeForNumericType<uint16_t>(port_number));

  return FromSchemeHostAndPort(scheme, host,
                               static_cast<uint16_t>(port_number));
}

// static
ProxyServer ProxyServer::FromSchemeHostAndPort(Scheme scheme,
                                               base::StringPiece host,
                                               absl::optional<uint16_t> port) {
  // Create INVALID proxies directly using `ProxyServer()`.
  DCHECK_NE(scheme, SCHEME_INVALID);

  // Create DIRECT proxies directly using `Direct()`.
  DCHECK_NE(scheme, SCHEME_DIRECT);

  // Add brackets to IPv6 literals if missing, as required by url
  // canonicalization.
  std::string bracketed_host;
  if (!host.empty() && host.front() != '[' &&
      host.find(":") != base::StringPiece::npos) {
    bracketed_host = base::StrCat({"[", host, "]"});
    host = bracketed_host;
  }

  std::string canonicalized_host;
  url::StdStringCanonOutput canonicalized_output(&canonicalized_host);
  url::Component component_output;

  if (!url::CanonicalizeHost(host.data(), url::Component(0, host.size()),
                             &canonicalized_output, &component_output)) {
    return ProxyServer();
  }
  if (!component_output.is_nonempty())
    return ProxyServer();

  canonicalized_output.Complete();

  // Remove IPv6 literal bracketing, as required by HostPortPair.
  base::StringPiece unbracketed_host = canonicalized_host;
  if (canonicalized_host.front() == '[' && canonicalized_host.back() == ']')
    unbracketed_host = unbracketed_host.substr(1, unbracketed_host.size() - 2);

  // A uint16_t port is always valid and canonicalized.
  uint16_t fixed_port = port.value_or(GetDefaultPortForScheme(scheme));

  return ProxyServer(scheme, HostPortPair(unbracketed_host, fixed_port));
}

std::string ProxyServer::GetHost() const {
  return host_port_pair().HostForURL();
}

uint16_t ProxyServer::GetPort() const {
  return host_port_pair().port();
}

const HostPortPair& ProxyServer::host_port_pair() const {
  // Doesn't make sense to call this if the URI scheme doesn't
  // have concept of a host.
  DCHECK(is_valid());
  DCHECK(!is_direct());
  return host_port_pair_;
}

// static
int ProxyServer::GetDefaultPortForScheme(Scheme scheme) {
  switch (scheme) {
    case SCHEME_HTTP:
      return 80;
    case SCHEME_SOCKS4:
    case SCHEME_SOCKS5:
      return 1080;
    case SCHEME_HTTPS:
    case SCHEME_QUIC:
      return 443;
    case SCHEME_INVALID:
    case SCHEME_DIRECT:
      break;
  }
  return -1;
}

std::ostream& operator<<(std::ostream& os, const ProxyServer& proxy_server) {
  return os << ProxyServerToPacResultElement(proxy_server);
}

}  // namespace net
