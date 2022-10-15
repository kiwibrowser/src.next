// Copyright 2011 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/base/proxy_string_util.h"

#include <CoreFoundation/CoreFoundation.h>
#include <string>

#include "base/logging.h"
#include "base/mac/foundation_util.h"
#include "base/strings/sys_string_conversions.h"
#include "net/base/host_port_pair.h"
#include "net/base/proxy_server.h"

namespace net {

ProxyServer ProxyDictionaryToProxyServer(ProxyServer::Scheme scheme,
                                         CFDictionaryRef dict,
                                         CFStringRef host_key,
                                         CFStringRef port_key) {
  if (scheme == ProxyServer::SCHEME_INVALID ||
      scheme == ProxyServer::SCHEME_DIRECT) {
    // No hostname port to extract; we are done.
    return ProxyServer(scheme, HostPortPair());
  }

  CFStringRef host_ref =
      base::mac::GetValueFromDictionary<CFStringRef>(dict, host_key);
  if (!host_ref) {
    LOG(WARNING) << "Could not find expected key "
                 << base::SysCFStringRefToUTF8(host_key)
                 << " in the proxy dictionary";
    return ProxyServer();  // Invalid.
  }
  std::string host = base::SysCFStringRefToUTF8(host_ref);

  CFNumberRef port_ref =
      base::mac::GetValueFromDictionary<CFNumberRef>(dict, port_key);
  int port;
  if (port_ref) {
    CFNumberGetValue(port_ref, kCFNumberIntType, &port);
  } else {
    port = ProxyServer::GetDefaultPortForScheme(scheme);
  }

  return ProxyServer(scheme, HostPortPair(host, port));
}

}  // namespace net
