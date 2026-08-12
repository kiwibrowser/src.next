// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef EXTENSIONS_COMMON_MANIFEST_HANDLERS_APP_URLS_HANDLER_H_
#define EXTENSIONS_COMMON_MANIFEST_HANDLERS_APP_URLS_HANDLER_H_

#include "base/containers/span.h"
#include "extensions/common/extension.h"
#include "extensions/common/manifest_handler.h"

namespace extensions {

// Parses the "app.urls" manifest key.
class AppURLsHandler : public ManifestHandler {
 public:
  AppURLsHandler();

  AppURLsHandler(const AppURLsHandler&) = delete;
  AppURLsHandler& operator=(const AppURLsHandler&) = delete;

  ~AppURLsHandler() override;

  bool Parse(Extension* extension, std::u16string* error) override;

 private:
  base::span<const char* const> Keys() const override;
};

}  // namespace extensions

#endif  // EXTENSIONS_COMMON_MANIFEST_HANDLERS_APP_URLS_HANDLER_H_
