// Copyright 2013 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef EXTENSIONS_COMMON_EXTENSION_BUILDER_H_
#define EXTENSIONS_COMMON_EXTENSION_BUILDER_H_

#include <initializer_list>
#include <memory>
#include <string>
#include <utility>

#include "base/files/file_path.h"
#include "base/memory/ref_counted.h"
#include "base/strings/string_piece.h"
#include "extensions/common/api/extension_action/action_info.h"
#include "extensions/common/manifest.h"
#include "extensions/common/mojom/manifest.mojom-shared.h"
#include "extensions/common/value_builder.h"

namespace extensions {
class Extension;

// An easier way to create extensions than Extension::Create. The
// constructor sets up some defaults which are customized using the
// methods.
// This class can be used in two ways:
// Aided Manifest Construction
//   The easy way. Use the constructor that takes a name and use helper methods
//   like AddPermission() to customize the extension without needing to
//   construct the manifest dictionary by hand. For more customization, you can
//   use MergeManifest() to add additional keys (which will take precedence over
//   others).
// Custom Manifest Construction
//   The hard way. Use the default constructor. SetManifest() *must* be called
//   with a valid manifest dictionary.
//   TODO(devlin): My suspicion is that this is almost always less readable and
//   useful, but it came first and is used in many places. It'd be nice to maybe
//   get rid of it.
// These are not interchangable - calling SetManifest() with aided manifest
// construction or e.g. AddPermissions() with custom manifest construction will
// crash.
class ExtensionBuilder {
 public:
  enum class Type {
    EXTENSION,
    PLATFORM_APP,
  };

  enum class BackgroundContext {
    BACKGROUND_PAGE,
    EVENT_PAGE,
    SERVICE_WORKER,
  };

  static constexpr char kServiceWorkerScriptFile[] = "sw.js";

  // Initializes an ExtensionBuilder that can be used with SetManifest() for
  // complete customization.
  ExtensionBuilder();

  // Initializes an ExtensionBuilder that can be used with various utility
  // methods to automatically construct a manifest. |name| will be the name of
  // the extension and used to generate a stable ID.
  ExtensionBuilder(const std::string& name, Type type = Type::EXTENSION);

  ExtensionBuilder(const ExtensionBuilder&) = delete;
  ExtensionBuilder& operator=(const ExtensionBuilder&) = delete;

  ~ExtensionBuilder();

  // Move constructor and operator=.
  ExtensionBuilder(ExtensionBuilder&& other);
  ExtensionBuilder& operator=(ExtensionBuilder&& other);

  // Returns the base::Value for the manifest, rather than constructing a full
  // extension. This is useful if you want to then use this in a ManifestTest or
  // to write a manifest with a TestExtensionDir.
  base::Value BuildManifest();

  // Can only be called once, after which it's invalid to use the builder.
  // CHECKs that the extension was created successfully.
  scoped_refptr<const Extension> Build();

  //////////////////////////////////////////////////////////////////////////////
  // Utility methods for use with aided manifest construction.

  // Add one or more permissions to the extension.
  ExtensionBuilder& AddPermission(const std::string& permission);
  ExtensionBuilder& AddPermissions(const std::vector<std::string>& permissions);

  // Sets an action type for the extension to have. By default, no action will
  // be set (though note that we synthesize a page action for most extensions).
  ExtensionBuilder& SetAction(ActionInfo::Type type);

  // Sets a background context for the extension. By default, none will be set.
  ExtensionBuilder& SetBackgroundContext(BackgroundContext background_context);

  // Adds a content script to the extension, with a script with the specified
  // |script_name| that matches the given |match_patterns|.
  ExtensionBuilder& AddContentScript(
      const std::string& script_name,
      const std::vector<std::string>& match_patterns);

  // Shortcuts for extremely popular keys.
  // Typically we'd use SetManifestKey() or SetManifestPath() for these, but
  // provide a faster route for these, since they're so central.
  ExtensionBuilder& SetVersion(const std::string& version);
  ExtensionBuilder& SetManifestVersion(int manifest_version);

  // Shortcuts to setting values on the manifest dictionary without needing to
  // go all the way through MergeManifest(). Sample usage:
  // ExtensionBuilder("name").SetManifestKey("version", "0.2").Build();
  // Can be used in conjuction with ListBuilder and DictionaryBuilder for more
  // complex types.
  template <typename T>
  ExtensionBuilder& SetManifestKey(base::StringPiece key, T&& value) {
    SetManifestKeyImpl(key, base::Value(std::forward<T>(value)));
    return *this;
  }
  template <typename T>
  ExtensionBuilder& SetManifestPath(
      std::initializer_list<base::StringPiece> path,
      T&& value) {
    SetManifestPathImpl(path, base::Value(std::forward<T>(value)));
    return *this;
  }
  // Specializations for unique_ptr<> to allow passing unique_ptr<base::Value>.
  // All other types will fail to compile.
  template <typename T>
  ExtensionBuilder& SetManifestKey(base::StringPiece key,
                                   std::unique_ptr<T> value) {
    SetManifestKeyImpl(key, std::move(*value));
    return *this;
  }
  template <typename T>
  ExtensionBuilder& SetManifestPath(
      std::initializer_list<base::StringPiece> path,
      std::unique_ptr<T> value) {
    SetManifestPathImpl(path, std::move(*value));
    return *this;
  }

  // A shortcut for adding raw JSON to the extension manifest. Useful if
  // constructing the values with a ValueBuilder is more painful than seeing
  // them with a string.
  // This JSON should be what you would add at the root node of the manifest;
  // for instance:
  // builder.AddJSON(R"("content_scripts": [...], "action": {})");
  // Keys specified in `json` take precedence over previously-set values.
  ExtensionBuilder& AddJSON(base::StringPiece json);

  //////////////////////////////////////////////////////////////////////////////
  // Utility methods for use with custom manifest construction.

  // Assigns the extension's manifest to |manifest|.
  ExtensionBuilder& SetManifest(
      std::unique_ptr<base::DictionaryValue> manifest);

  //////////////////////////////////////////////////////////////////////////////
  // Common utility methods (usable with both aided and custom manifest
  // creation).

  // Defaults to FilePath().
  ExtensionBuilder& SetPath(const base::FilePath& path);

  // Defaults to mojom::ManifestLocation::kUnpacked.
  ExtensionBuilder& SetLocation(mojom::ManifestLocation location);

  // Merge another manifest into the current manifest, with new keys taking
  // precedence.
  ExtensionBuilder& MergeManifest(const base::Value& manifest);
  ExtensionBuilder& MergeManifest(
      std::unique_ptr<base::DictionaryValue> manifest);

  // Add flags to the extension. Default is no flags.
  ExtensionBuilder& AddFlags(int init_from_value_flags);

  // Defaults to the default extension ID created in Extension::Create or to an
  // ID generated from the extension's name, if aided manifest construction is
  // used.
  ExtensionBuilder& SetID(const std::string& id);

 private:
  struct ManifestData;

  void SetManifestKeyImpl(base::StringPiece key, base::Value value);
  void SetManifestPathImpl(std::initializer_list<base::StringPiece> path,
                           base::Value value);

  // Information for constructing the manifest; either metadata about the
  // manifest which will be used to construct it, or the dictionary itself. Only
  // one will be present.
  std::unique_ptr<ManifestData> manifest_data_;
  std::unique_ptr<base::DictionaryValue> manifest_value_;

  base::FilePath path_;
  mojom::ManifestLocation location_;
  int flags_;
  std::string id_;
};

}  // namespace extensions

#endif  // EXTENSIONS_COMMON_EXTENSION_BUILDER_H_
