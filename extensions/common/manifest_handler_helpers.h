// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef EXTENSIONS_COMMON_MANIFEST_HANDLER_HELPERS_H_
#define EXTENSIONS_COMMON_MANIFEST_HANDLER_HELPERS_H_

#include <memory>
#include <string>
#include <vector>

#include "base/strings/string_piece.h"

class ExtensionIconSet;

namespace base {
class Value;
}

namespace extensions {
namespace manifest_handler_helpers {

// Tokenize a dictionary path.
std::vector<base::StringPiece> TokenizeDictionaryPath(base::StringPiece path);

// Strips leading slashes from the file path. Returns true iff the final path is
// not empty.
bool NormalizeAndValidatePath(std::string* path);
bool NormalizeAndValidatePath(const std::string& path,
                              std::string* normalized_path);

// Loads icon paths defined in dictionary |icons_value| into ExtensionIconSet
// |icons|. |icons_value| is a dictionary value {icon size -> icon path}.
// Returns success. If load fails, |error| will be set.
bool LoadIconsFromDictionary(const base::Value* icons_value,
                             ExtensionIconSet* icons,
                             std::u16string* error);

}  // namespace manifest_handler_helpers
}  // namespace extensions

#endif  // EXTENSIONS_COMMON_MANIFEST_HANDLER_HELPERS_H_
