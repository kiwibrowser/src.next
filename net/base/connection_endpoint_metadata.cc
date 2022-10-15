// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/base/connection_endpoint_metadata.h"

#include <string>
#include <utility>
#include <vector>

#include "base/base64.h"
#include "base/values.h"
#include "third_party/abseil-cpp/absl/types/optional.h"

namespace net {

namespace {
const char kSupportedProtocolAlpnsKey[] = "supported_protocol_alpns";
const char kEchConfigListKey[] = "ech_config_list";
const char kTargetNameKey[] = "target_name";
}  // namespace

ConnectionEndpointMetadata::ConnectionEndpointMetadata() = default;
ConnectionEndpointMetadata::~ConnectionEndpointMetadata() = default;
ConnectionEndpointMetadata::ConnectionEndpointMetadata(
    const ConnectionEndpointMetadata&) = default;
ConnectionEndpointMetadata::ConnectionEndpointMetadata(
    ConnectionEndpointMetadata&&) = default;

base::Value ConnectionEndpointMetadata::ToValue() const {
  base::Value::Dict dict;

  base::Value::List alpns_list;
  for (const std::string& alpn : supported_protocol_alpns) {
    alpns_list.Append(alpn);
  }
  dict.Set(kSupportedProtocolAlpnsKey, std::move(alpns_list));

  dict.Set(kEchConfigListKey, base::Base64Encode(ech_config_list));

  if (!target_name.empty()) {
    dict.Set(kTargetNameKey, target_name);
  }

  return base::Value(std::move(dict));
}

// static
absl::optional<ConnectionEndpointMetadata>
ConnectionEndpointMetadata::FromValue(const base::Value& value) {
  const base::Value::Dict* dict = value.GetIfDict();
  if (!dict)
    return absl::nullopt;

  const base::Value::List* alpns_list =
      dict->FindList(kSupportedProtocolAlpnsKey);
  const std::string* ech_config_list_value =
      dict->FindString(kEchConfigListKey);
  const std::string* target_name_value = dict->FindString(kTargetNameKey);

  if (!alpns_list || !ech_config_list_value)
    return absl::nullopt;

  ConnectionEndpointMetadata metadata;

  std::vector<std::string> alpns;
  for (const base::Value& alpn : *alpns_list) {
    if (!alpn.is_string())
      return absl::nullopt;
    metadata.supported_protocol_alpns.push_back(alpn.GetString());
  }

  absl::optional<std::vector<uint8_t>> decoded =
      base::Base64Decode(*ech_config_list_value);
  if (!decoded)
    return absl::nullopt;
  metadata.ech_config_list = std::move(*decoded);

  if (target_name_value) {
    metadata.target_name = *target_name_value;
  }

  return metadata;
}

}  // namespace net
