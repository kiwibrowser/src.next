// Copyright 2013 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef EXTENSIONS_COMMON_MANIFEST_HANDLERS_BACKGROUND_INFO_H_
#define EXTENSIONS_COMMON_MANIFEST_HANDLERS_BACKGROUND_INFO_H_

#include <string>
#include <vector>

#include "extensions/common/extension.h"
#include "extensions/common/manifest_handler.h"
#include "third_party/abseil-cpp/absl/types/optional.h"
#include "url/gurl.h"

namespace extensions {

enum class BackgroundServiceWorkerType {
  kClassic,
  kModule,
};

class BackgroundInfo : public Extension::ManifestData {
 public:
  BackgroundInfo();

  BackgroundInfo(const BackgroundInfo&) = delete;
  BackgroundInfo& operator=(const BackgroundInfo&) = delete;

  ~BackgroundInfo() override;

  static GURL GetBackgroundURL(const Extension* extension);
  static const std::vector<std::string>& GetBackgroundScripts(
      const Extension* extension);
  static const std::string& GetBackgroundServiceWorkerScript(
      const Extension* extension);
  static BackgroundServiceWorkerType GetBackgroundServiceWorkerType(
      const Extension* extension);
  static bool HasBackgroundPage(const Extension* extension);
  static bool HasPersistentBackgroundPage(const Extension* extension);
  static bool HasLazyBackgroundPage(const Extension* extension);
  static bool HasGeneratedBackgroundPage(const Extension* extension);
  static bool AllowJSAccess(const Extension* extension);
  static bool IsServiceWorkerBased(const Extension* extension);
  static bool HasLazyContext(const Extension* extension) {
    return HasLazyBackgroundPage(extension) || IsServiceWorkerBased(extension);
  }

  bool has_background_page() const {
    return background_url_.is_valid() || !background_scripts_.empty();
  }

  bool has_persistent_background_page() const {
    return has_background_page() && is_persistent_;
  }

  bool has_lazy_background_page() const {
    return has_background_page() && !is_persistent_;
  }

  bool Parse(const Extension* extension, std::u16string* error);

 private:
  bool LoadBackgroundScripts(const Extension* extension,
                             const std::string& key,
                             std::u16string* error);
  bool LoadBackgroundPage(const Extension* extension,
                          const std::string& key,
                          std::u16string* error);
  bool LoadBackgroundPage(const Extension* extension, std::u16string* error);
  bool LoadBackgroundServiceWorkerScript(const Extension* extension,
                                         std::u16string* error);
  bool LoadBackgroundPersistent(const Extension* extension,
                                std::u16string* error);
  bool LoadAllowJSAccess(const Extension* extension, std::u16string* error);

  // Optional URL to a master page of which a single instance should be always
  // loaded in the background.
  GURL background_url_;

  // Optional list of scripts to use to generate a background page. If this is
  // present, background_url_ will be empty and generated by GetBackgroundURL().
  std::vector<std::string> background_scripts_;

  // Optional service worker based background script.
  absl::optional<std::string> background_service_worker_script_;

  // Optional service worker based background type.
  absl::optional<BackgroundServiceWorkerType> background_service_worker_type_;

  // True if the background page should stay loaded forever; false if it should
  // load on-demand (when it needs to handle an event). Defaults to true.
  bool is_persistent_;

  // True if the background page can be scripted by pages of the app or
  // extension, in which case all such pages must run in the same process.
  // False if such pages are not permitted to script the background page,
  // allowing them to run in different processes.
  // Defaults to true.
  bool allow_js_access_;
};

// Parses all background/event page-related keys in the manifest.
class BackgroundManifestHandler : public ManifestHandler {
 public:
  BackgroundManifestHandler();

  BackgroundManifestHandler(const BackgroundManifestHandler&) = delete;
  BackgroundManifestHandler& operator=(const BackgroundManifestHandler&) =
      delete;

  ~BackgroundManifestHandler() override;

  bool Parse(Extension* extension, std::u16string* error) override;
  bool Validate(const Extension* extension,
                std::string* error,
                std::vector<InstallWarning>* warnings) const override;
  bool AlwaysParseForType(Manifest::Type type) const override;

 private:
  base::span<const char* const> Keys() const override;
};

}  // namespace extensions

#endif  // EXTENSIONS_COMMON_MANIFEST_HANDLERS_BACKGROUND_INFO_H_
