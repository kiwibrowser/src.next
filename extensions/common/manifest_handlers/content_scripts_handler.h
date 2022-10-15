// Copyright 2013 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef EXTENSIONS_COMMON_MANIFEST_HANDLERS_CONTENT_SCRIPTS_HANDLER_H_
#define EXTENSIONS_COMMON_MANIFEST_HANDLERS_CONTENT_SCRIPTS_HANDLER_H_

#include <string>

#include "extensions/common/extension.h"
#include "extensions/common/manifest_handler.h"
#include "extensions/common/user_script.h"

namespace extensions {

class URLPatternSet;

struct ContentScriptsInfo : public Extension::ManifestData {
  ContentScriptsInfo();
  ~ContentScriptsInfo() override;

  // Paths to the content scripts the extension contains (possibly empty).
  UserScriptList content_scripts;

  // Returns the content scripts for the extension (if the extension has
  // no content scripts, this returns an empty list).
  static const UserScriptList& GetContentScripts(const Extension* extension);

  // Returns the list of hosts that this extension can run content scripts on.
  static URLPatternSet GetScriptableHosts(const Extension* extension);

  // Returns true if the extension has a content script declared at |url|.
  static bool ExtensionHasScriptAtURL(const Extension* extension,
                                      const GURL& url);
};

// Parses the "content_scripts" manifest key.
class ContentScriptsHandler : public ManifestHandler {
 public:
  ContentScriptsHandler();

  ContentScriptsHandler(const ContentScriptsHandler&) = delete;
  ContentScriptsHandler& operator=(const ContentScriptsHandler&) = delete;

  ~ContentScriptsHandler() override;

  bool Parse(Extension* extension, std::u16string* error) override;
  bool Validate(const Extension* extension,
                std::string* error,
                std::vector<InstallWarning>* warnings) const override;

 private:
  base::span<const char* const> Keys() const override;
};

}  // namespace extensions

#endif  // EXTENSIONS_COMMON_MANIFEST_HANDLERS_CONTENT_SCRIPTS_HANDLER_H_
