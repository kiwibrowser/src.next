// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/extension_management_internal.h"

#include <utility>

#include "base/logging.h"
#include "base/values.h"
#include "base/version.h"
#include "chrome/browser/extensions/extension_management_constants.h"
#include "extensions/common/extension_urls.h"
#include "extensions/common/url_pattern_set.h"
#include "url/gurl.h"

namespace extensions {

namespace internal {

namespace {
const char kMalformedPreferenceWarning[] =
    "Malformed extension management preference.";

// Maximum number of characters for a 'blocked_install_message' value.
const int kBlockedInstallMessageMaxLength = 1000;

bool GetString(const base::Value& dict, const char* key, std::string* result) {
  DCHECK(dict.is_dict());
  const std::string* value = dict.FindStringKey(key);
  if (!value)
    return false;
  *result = *value;
  return true;
}
}  // namespace

IndividualSettings::IndividualSettings() {
  Reset();
}

// Initializes from default settings.
IndividualSettings::IndividualSettings(
    const IndividualSettings* default_settings) {
  installation_mode = default_settings->installation_mode;
  update_url = default_settings->update_url;
  // We are not initializing `minimum_version_required` from `default_settings`
  // here since it's not applicable to default settings.
  //
  // We also do not inherit `blocked_permissions`, `runtime_allowed_hosts` or
  // `runtime_blocked_hosts` from default either. It's likely not a behavior by
  // design but fixing these issues may break users that rely on them. For
  // now, we will keep it as is until there is a long term plan.
}

IndividualSettings::~IndividualSettings() {
}

bool IndividualSettings::Parse(const base::DictionaryValue* dict,
                               ParsingScope scope) {
  std::string installation_mode_str;
  if (GetString(*dict, schema_constants::kInstallationMode,
                &installation_mode_str)) {
    if (installation_mode_str == schema_constants::kAllowed) {
      installation_mode = ExtensionManagement::INSTALLATION_ALLOWED;
    } else if (installation_mode_str == schema_constants::kBlocked) {
      installation_mode = ExtensionManagement::INSTALLATION_BLOCKED;
    } else if (installation_mode_str == schema_constants::kForceInstalled) {
      installation_mode = ExtensionManagement::INSTALLATION_FORCED;
    } else if (installation_mode_str == schema_constants::kNormalInstalled) {
      installation_mode = ExtensionManagement::INSTALLATION_RECOMMENDED;
    } else if (installation_mode_str == schema_constants::kRemoved) {
      installation_mode = ExtensionManagement::INSTALLATION_REMOVED;
    } else {
      // Invalid value for 'installation_mode'.
      LOG(WARNING) << kMalformedPreferenceWarning;
      return false;
    }

    // Only proceed to fetch update url if force or recommended install mode
    // is set.
    if (installation_mode == ExtensionManagement::INSTALLATION_FORCED ||
        installation_mode == ExtensionManagement::INSTALLATION_RECOMMENDED) {
      if (scope != SCOPE_INDIVIDUAL) {
        // Only individual extensions are allowed to be automatically
        // installed.
        LOG(WARNING) << kMalformedPreferenceWarning;
        return false;
      }
      std::string update_url_str;
      if (GetString(*dict, schema_constants::kUpdateUrl, &update_url_str) &&
          GURL(update_url_str).is_valid()) {
        update_url = update_url_str;
      } else {
        // No valid update URL for extension.
        LOG(WARNING) << kMalformedPreferenceWarning;
        return false;
      }
    }
  }

  bool is_policy_installed =
      installation_mode == ExtensionManagement::INSTALLATION_FORCED ||
      installation_mode == ExtensionManagement::INSTALLATION_RECOMMENDED;
  // Note: We ignore the override update URL policy when the update URL is from
  // the webstore.
  if (is_policy_installed &&
      !extension_urls::IsWebstoreUpdateUrl(GURL(update_url))) {
    const absl::optional<bool> is_update_url_overridden =
        dict->FindBoolKey(schema_constants::kOverrideUpdateUrl);
    if (is_update_url_overridden)
      override_update_url = is_update_url_overridden.value();
  }

  // Parses the blocked permission settings.
  const base::Value* list_value = nullptr;
  std::u16string error;

  // Parse the blocked and allowed permissions.
  // Note that we currently don't use default permission settings for
  // per-update-url or per-id settings at all even though they are not set.
  // For example:
  // {"*" : {blocked_permissions:["audio"]}, "id1":{}}
  // {"*" : {blocked_permissions:["audio"]}}
  // Extension id1 is able to get the audio permission with the first config but
  // not the second one.
  // It's against the intuition but we will NOT change this behavior until we
  // find a good way to fix this issue as external users may rely on it anyway.
  // This also makes the "allowed_permissions" attribute meaningless. However,
  // for the same reason, we keep the code for now.
  APIPermissionSet parsed_blocked_permissions;
  APIPermissionSet explicitly_allowed_permissions;
  list_value = dict->FindListKey(schema_constants::kAllowedPermissions);
  if (list_value) {
    if (!APIPermissionSet::ParseFromJSON(
            list_value, APIPermissionSet::kDisallowInternalPermissions,
            &explicitly_allowed_permissions, &error, nullptr)) {
      LOG(WARNING) << error;
    }
  }
  list_value = dict->FindListKey(schema_constants::kBlockedPermissions);
  if (list_value) {
    if (!APIPermissionSet::ParseFromJSON(
            list_value, APIPermissionSet::kDisallowInternalPermissions,
            &parsed_blocked_permissions, &error, nullptr)) {
      LOG(WARNING) << error;
    }
  }
  APIPermissionSet::Difference(parsed_blocked_permissions,
                               explicitly_allowed_permissions,
                               &blocked_permissions);

  // Parses list of Match Patterns into a URLPatternSet.
  auto parse_url_pattern_set = [](const base::DictionaryValue* dict,
                                  const char key[], URLPatternSet* out_value) {
    // Get the list of URLPatterns.
    const base::Value* host_list_value = dict->FindListKey(key);
    if (host_list_value) {
      base::Value::ConstListView host_list_view =
          host_list_value->GetListDeprecated();
      if (host_list_view.size() > schema_constants::kMaxItemsURLPatternSet) {
        LOG(WARNING) << "Exceeded maximum number of URL match patterns ("
                     << schema_constants::kMaxItemsURLPatternSet
                     << ") for attribute '" << key << "'";
      }

      out_value->ClearPatterns();
      const int extension_scheme_mask =
          URLPattern::GetValidSchemeMaskForExtensions();
      auto numItems = std::min(host_list_view.size(),
                               schema_constants::kMaxItemsURLPatternSet);
      for (size_t i = 0; i < numItems; ++i) {
        std::string unparsed_str;
        if (host_list_view[i].is_string())
          unparsed_str = host_list_view[i].GetString();
        URLPattern pattern(extension_scheme_mask);
        if (unparsed_str != URLPattern::kAllUrlsPattern)
          unparsed_str.append("/*");
        URLPattern::ParseResult parse_result = pattern.Parse(unparsed_str);
        if (parse_result != URLPattern::ParseResult::kSuccess) {
          LOG(WARNING) << kMalformedPreferenceWarning;
          LOG(WARNING) << "Invalid URL pattern '" + unparsed_str +
                              "' for attribute " + key;
          return false;
        }
        out_value->AddPattern(pattern);
      }
    }
    return true;
  };

  if (!parse_url_pattern_set(dict, schema_constants::kPolicyBlockedHosts,
                             &policy_blocked_hosts))
    return false;

  if (!parse_url_pattern_set(dict, schema_constants::kPolicyAllowedHosts,
                             &policy_allowed_hosts))
    return false;

  // Parses the minimum version settings.
  std::string minimum_version_required_str;
  if (scope == SCOPE_INDIVIDUAL &&
      GetString(*dict, schema_constants::kMinimumVersionRequired,
                &minimum_version_required_str)) {
    std::unique_ptr<base::Version> version(
        new base::Version(minimum_version_required_str));
    // We accept a general version string here. Note that count of components in
    // version string of extensions is limited to 4.
    if (!version->IsValid())
      LOG(WARNING) << kMalformedPreferenceWarning;
    else
      minimum_version_required = std::move(version);
  }

  if (GetString(*dict, schema_constants::kBlockedInstallMessage,
                &blocked_install_message)) {
    if (blocked_install_message.length() > kBlockedInstallMessageMaxLength) {
      LOG(WARNING) << "Truncated blocked install message to 1000 characters";
      blocked_install_message.erase(kBlockedInstallMessageMaxLength,
                                    std::string::npos);
    }
  }

  std::string toolbar_pin_str;
  if (GetString(*dict, schema_constants::kToolbarPin, &toolbar_pin_str)) {
    if (toolbar_pin_str == schema_constants::kDefaultUnpinned) {
      toolbar_pin = ExtensionManagement::ToolbarPinMode::kDefaultUnpinned;
    } else if (toolbar_pin_str == schema_constants::kForcePinned) {
      toolbar_pin = ExtensionManagement::ToolbarPinMode::kForcePinned;
    } else {
      // Invalid value for 'toolbar_pin'.
      LOG(WARNING) << kMalformedPreferenceWarning;
      return false;
    }
  }

  return true;
}

void IndividualSettings::Reset() {
  installation_mode = ExtensionManagement::INSTALLATION_ALLOWED;
  update_url.clear();
  blocked_permissions.clear();
  policy_blocked_hosts.ClearPatterns();
  policy_allowed_hosts.ClearPatterns();
  blocked_install_message.clear();
}

GlobalSettings::GlobalSettings() {
  Reset();
}

GlobalSettings::~GlobalSettings() {
}

void GlobalSettings::Reset() {
  has_restricted_install_sources = false;
  install_sources.ClearPatterns();
  has_restricted_allowed_types = false;
  allowed_types.clear();
}

}  // namespace internal

}  // namespace extensions
