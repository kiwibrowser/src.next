// Copyright 2013 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "extensions/common/extension_builder.h"

#include <optional>
#include <string_view>
#include <utility>

#include "base/json/json_reader.h"
#include "base/strings/stringprintf.h"
#include "components/crx_file/id_util.h"
#include "extensions/common/api/content_scripts.h"
#include "extensions/common/api/extension_action/action_info.h"
#include "extensions/common/extension.h"
#include "extensions/common/manifest_constants.h"

namespace extensions {

constexpr char ExtensionBuilder::kServiceWorkerScriptFile[];

struct ExtensionBuilder::ManifestData {
  Type type;
  std::string name;
  std::vector<std::string> permissions;
  std::vector<std::string> optional_permissions;
  std::optional<ActionInfo::Type> action;
  std::optional<BackgroundContext> background_context;
  std::optional<std::string> version;
  std::optional<int> manifest_version;

  // A ContentScriptEntry includes a string name, and a vector of string
  // match patterns.
  using ContentScriptEntry = std::pair<std::string, std::vector<std::string>>;
  std::vector<ContentScriptEntry> content_scripts;

  std::optional<base::Value::Dict> extra;

  base::Value::Dict GetValue() const {
    auto manifest =
        base::Value::Dict()
            .Set(manifest_keys::kName, name)
            .Set(manifest_keys::kManifestVersion, manifest_version.value_or(2))
            .Set(manifest_keys::kVersion, version.value_or("0.1"))
            .Set(manifest_keys::kDescription, "some description");

    switch (type) {
      case Type::EXTENSION:
        break;  // Sufficient already.
      case Type::PLATFORM_APP: {
        base::Value::Dict background;
        background.Set("scripts", base::Value::List().Append("test.js"));
        manifest.Set("app", base::Value::Dict().Set("background",
                                                    std::move(background)));
        break;
      }
    }

    if (!permissions.empty()) {
      base::Value::List permissions_builder;
      for (const std::string& permission : permissions)
        permissions_builder.Append(permission);
      manifest.Set(manifest_keys::kPermissions, std::move(permissions_builder));
    }

    if (!optional_permissions.empty()) {
      base::Value::List permissions_builder;
      for (const std::string& permission : optional_permissions) {
        permissions_builder.Append(permission);
      }
      manifest.Set(manifest_keys::kOptionalPermissions,
                   std::move(permissions_builder));
    }

    if (action) {
      const char* action_key = ActionInfo::GetManifestKeyForActionType(*action);
      manifest.Set(action_key, base::Value(base::Value::Dict()));
    }

    if (background_context) {
      base::Value::Dict background;
      std::optional<bool> persistent;
      switch (*background_context) {
        case BackgroundContext::BACKGROUND_PAGE:
          background.Set("page", "background_page.html");
          persistent = true;
          break;
        case BackgroundContext::EVENT_PAGE:
          background.Set("page", "background_page.html");
          persistent = false;
          break;
        case BackgroundContext::SERVICE_WORKER:
          background.Set("service_worker", kServiceWorkerScriptFile);
          break;
      }
      if (persistent) {
        background.Set("persistent", *persistent);
      }
      manifest.Set("background", std::move(background));
    }

    if (!content_scripts.empty()) {
      base::Value::List scripts_value;
      scripts_value.reserve(content_scripts.size());
      for (const auto& [script_name, pattern_matches] : content_scripts) {
        base::Value::List matches;
        matches.reserve(pattern_matches.size());
        for (const auto& pattern_match : pattern_matches) {
          matches.Append(pattern_match);
        }
        scripts_value.Append(
            base::Value::Dict()
                .Set(api::content_scripts::ContentScript::kJs,
                     base::Value::List().Append(script_name))
                .Set(api::content_scripts::ContentScript::kMatches,
                     std::move(matches)));
      }
      manifest.Set(api::content_scripts::ManifestKeys::kContentScripts,
                   std::move(scripts_value));
    }

    base::Value::Dict result = std::move(manifest);
    if (extra)
      result.Merge(extra->Clone());

    return result;
  }

  base::Value::Dict& get_extra() {
    if (!extra)
      extra.emplace();
    return *extra;
  }
};

ExtensionBuilder::ExtensionBuilder()
    : location_(mojom::ManifestLocation::kUnpacked),
      flags_(Extension::NO_FLAGS) {}

ExtensionBuilder::ExtensionBuilder(const std::string& name, Type type)
    : ExtensionBuilder() {
  manifest_data_ = std::make_unique<ManifestData>();
  manifest_data_->name = name;
  manifest_data_->type = type;
}

ExtensionBuilder::~ExtensionBuilder() = default;

ExtensionBuilder::ExtensionBuilder(ExtensionBuilder&& other) = default;
ExtensionBuilder& ExtensionBuilder::operator=(ExtensionBuilder&& other) =
    default;

scoped_refptr<const Extension> ExtensionBuilder::Build() {
  CHECK(manifest_data_ || manifest_value_);

  if (id_.empty() && manifest_data_)
    id_ = crx_file::id_util::GenerateId(manifest_data_->name);

  std::string error;

  // This allows `*manifest_value` to be passed as a reference instead of
  // needing to be cloned.
  std::optional<base::Value::Dict> manifest_data_value;
  if (manifest_data_) {
    manifest_data_value = manifest_data_->GetValue();
  }
  scoped_refptr<const Extension> extension = Extension::Create(
      path_, location_,
      manifest_data_value ? *manifest_data_value : *manifest_value_, flags_,
      id_, &error);

  CHECK(error.empty()) << error;
  CHECK(extension);

  return extension;
}

base::Value ExtensionBuilder::BuildManifest() {
  CHECK(manifest_data_ || manifest_value_);
  return base::Value(manifest_data_ ? manifest_data_->GetValue()
                                    : manifest_value_->Clone());
}

ExtensionBuilder& ExtensionBuilder::AddPermission(
    const std::string& permission) {
  CHECK(manifest_data_);
  manifest_data_->permissions.push_back(permission);
  return *this;
}

ExtensionBuilder& ExtensionBuilder::AddPermissions(
    const std::vector<std::string>& permissions) {
  CHECK(manifest_data_);
  manifest_data_->permissions.insert(manifest_data_->permissions.end(),
                                     permissions.begin(), permissions.end());
  return *this;
}

ExtensionBuilder& ExtensionBuilder::AddOptionalPermission(
    const std::string& permission) {
  CHECK(manifest_data_);
  manifest_data_->optional_permissions.push_back(permission);
  return *this;
}

ExtensionBuilder& ExtensionBuilder::AddOptionalPermissions(
    const std::vector<std::string>& permissions) {
  CHECK(manifest_data_);
  manifest_data_->optional_permissions.insert(
      manifest_data_->optional_permissions.end(), permissions.begin(),
      permissions.end());
  return *this;
}

ExtensionBuilder& ExtensionBuilder::SetAction(ActionInfo::Type type) {
  CHECK(manifest_data_);
  manifest_data_->action = type;
  return *this;
}

ExtensionBuilder& ExtensionBuilder::SetBackgroundContext(
    BackgroundContext background_context) {
  CHECK(manifest_data_);
  manifest_data_->background_context = background_context;
  return *this;
}

ExtensionBuilder& ExtensionBuilder::AddContentScript(
    const std::string& script_name,
    const std::vector<std::string>& match_patterns) {
  CHECK(manifest_data_);
  manifest_data_->content_scripts.emplace_back(script_name, match_patterns);
  return *this;
}

ExtensionBuilder& ExtensionBuilder::SetVersion(const std::string& version) {
  CHECK(manifest_data_);
  manifest_data_->version = version;
  return *this;
}

ExtensionBuilder& ExtensionBuilder::SetManifestVersion(int manifest_version) {
  CHECK(manifest_data_);
  manifest_data_->manifest_version = manifest_version;
  return *this;
}

ExtensionBuilder& ExtensionBuilder::AddJSON(std::string_view json) {
  CHECK(manifest_data_);
  std::string wrapped_json = base::StringPrintf("{%s}", json.data());
  auto parsed = base::JSONReader::ReadAndReturnValueWithError(wrapped_json);
  CHECK(parsed.has_value())
      << "Failed to parse json for extension '" << manifest_data_->name
      << "':" << parsed.error().message;
  return MergeManifest(std::move(*parsed).TakeDict());
}

ExtensionBuilder& ExtensionBuilder::SetPath(const base::FilePath& path) {
  path_ = path;
  return *this;
}

ExtensionBuilder& ExtensionBuilder::SetLocation(
    mojom::ManifestLocation location) {
  location_ = location;
  return *this;
}

ExtensionBuilder& ExtensionBuilder::SetManifest(base::Value::Dict manifest) {
  CHECK(!manifest_data_);
  manifest_value_ = std::move(manifest);
  return *this;
}

ExtensionBuilder& ExtensionBuilder::MergeManifest(base::Value::Dict to_merge) {
  if (manifest_data_) {
    manifest_data_->get_extra().Merge(std::move(to_merge));
  } else {
    manifest_value_->Merge(std::move(to_merge));
  }
  return *this;
}

ExtensionBuilder& ExtensionBuilder::AddFlags(int init_from_value_flags) {
  flags_ |= init_from_value_flags;
  return *this;
}

ExtensionBuilder& ExtensionBuilder::SetID(const std::string& id) {
  id_ = id;
  return *this;
}

void ExtensionBuilder::SetManifestKeyImpl(std::string_view key,
                                          base::Value value) {
  CHECK(manifest_data_);
  manifest_data_->get_extra().Set(key, std::move(value));
}

void ExtensionBuilder::SetManifestPathImpl(std::string_view path,
                                           base::Value value) {
  CHECK(manifest_data_);
  manifest_data_->get_extra().SetByDottedPath(path, std::move(value));
}

}  // namespace extensions
