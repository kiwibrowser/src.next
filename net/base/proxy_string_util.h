// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_BASE_PROXY_STRING_UTIL_H_
#define NET_BASE_PROXY_STRING_UTIL_H_

#include <string>

#include "base/strings/string_piece.h"
#include "build/build_config.h"
#include "net/base/net_export.h"
#include "net/base/proxy_server.h"

#if BUILDFLAG(IS_APPLE)
#include <CoreFoundation/CoreFoundation.h>
#endif  // BUILDFLAG(IS_APPLE)

namespace net {

// Converts a PAC result element (commonly called a PAC string) to/from a
// ProxyServer. Note that this only deals with a single proxy server element
// separated out from the complete semicolon-delimited PAC result string.
//
// PAC result elements have the format:
// <scheme>" "<host>[":"<port>]
//
// Where <scheme> may be one of (case-insensitive):
// "DIRECT"
// "PROXY"
// "HTTPS"
// "SOCKS4"
// "SOCKS5"
// "SOCKS" (canonicalizes to "SOCKS4")
// "QUIC"
//
// If <port> is omitted, it will be assumed as the default port for the
// chosen scheme (via ProxyServer::GetDefaultPortForScheme()).
//
// If parsing fails the returned proxy will have scheme
// `ProxyServer::SCHEME_INVALID`.
//
// Examples:
//   "PROXY foopy:19"   {scheme=HTTP, host="foopy", port=19}
//   "DIRECT"           {scheme=DIRECT}
//   "SOCKS5 foopy"     {scheme=SOCKS5, host="foopy", port=1080}
//   "HTTPS foopy:123"  {scheme=HTTPS, host="foopy", port=123}
//   "QUIC foopy:123"   {scheme=QUIC, host="foopy", port=123}
//   "BLAH xxx:xx"      INVALID
NET_EXPORT ProxyServer
PacResultElementToProxyServer(base::StringPiece pac_result_element);
NET_EXPORT std::string ProxyServerToPacResultElement(
    const ProxyServer& proxy_server);

// Converts a non-standard URI string to/from a ProxyServer.
//
// The non-standard URI strings have the format:
//   [<scheme>"://"]<server>[":"<port>]
//
// Where <scheme> may be one of:
// "http"
// "socks4"
// "socks5
// "socks" (equivalent to "socks5")
// "direct"
// "https"
// "quic"
//
// Both <scheme> and <port> are optional. If <scheme> is omitted, it will be
// assumed as |default_scheme|. If <port> is omitted, it will be assumed as
// the default port for the chosen scheme (via
// ProxyServer::GetDefaultPortForScheme()).
//
// If parsing fails the returned proxy will have scheme
// `ProxyServer::SCHEME_INVALID`.
//
// Examples (for `default_pac_scheme` = `kHttp` ):
//   "foopy"            {scheme=HTTP, host="foopy", port=80}
//   "socks://foopy"    {scheme=SOCKS5, host="foopy", port=1080}
//   "socks4://foopy"   {scheme=SOCKS4, host="foopy", port=1080}
//   "socks5://foopy"   {scheme=SOCKS5, host="foopy", port=1080}
//   "http://foopy:17"  {scheme=HTTP, host="foopy", port=17}
//   "https://foopy:17" {scheme=HTTPS, host="foopy", port=17}
//   "quic://foopy:17"  {scheme=QUIC, host="foopy", port=17}
//   "direct://"        {scheme=DIRECT}
//   "foopy:X"          INVALID -- bad port.
NET_EXPORT ProxyServer
ProxyUriToProxyServer(base::StringPiece uri,
                      ProxyServer::Scheme default_scheme);
NET_EXPORT std::string ProxyServerToProxyUri(const ProxyServer& proxy_server);

// Parses the proxy scheme from the non-standard URI scheme string
// representation used in `ProxyUriToProxyServer()` and
// `ProxyServerToProxyUri()`. If no type could be matched, returns
// SCHEME_INVALID.
NET_EXPORT ProxyServer::Scheme GetSchemeFromUriScheme(base::StringPiece scheme);

#if BUILDFLAG(IS_APPLE)
// Utility function to pull out a host/port pair from a dictionary and return
// it as a ProxyServer object. Pass in a dictionary that has a  value for the
// host key and optionally a value for the port key. In the error condition
// where the host value is especially malformed, returns an invalid
// ProxyServer.
//
// TODO(ericorth@chromium.org): Dictionary isn't really a string representation,
// so this doesn't really belong in this file. Consider moving this logic to
// somewhere alongside the Apple-specific proxy-resolution code.
ProxyServer ProxyDictionaryToProxyServer(ProxyServer::Scheme scheme,
                                         CFDictionaryRef dict,
                                         CFStringRef host_key,
                                         CFStringRef port_key);
#endif  // BUILDFLAG(IS_APPLE)

}  // namespace net

#endif  // NET_BASE_PROXY_STRING_UTIL_H_
