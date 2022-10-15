// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef EXTENSIONS_BROWSER_EXTENSION_UTIL_H_
#define EXTENSIONS_BROWSER_EXTENSION_UTIL_H_

#include <string>
#include <vector>

#include "base/callback.h"
#include "extensions/common/manifest.h"
#include "url/gurl.h"

namespace base {
class FilePath;
}

namespace content {
class BrowserContext;
class SiteInstance;
class StoragePartition;
class StoragePartitionConfig;
}

namespace extensions {
class Extension;
class ExtensionSet;

namespace util {

// TODO(benwells): Move functions from
// chrome/browser/extensions/extension_util.h/cc that are only dependent on
// extensions/ here.

// Returns true if the extension can be enabled in incognito mode.
bool CanBeIncognitoEnabled(const Extension* extension);

// Returns true if |extension_id| can run in an incognito window.
bool IsIncognitoEnabled(const ExtensionId& extension_id,
                        content::BrowserContext* context);

// Returns true if |extension| can see events and data from another sub-profile
// (incognito to original profile, or vice versa).
bool CanCrossIncognito(const extensions::Extension* extension,
                       content::BrowserContext* context);

// Returns the StoragePartition domain for |extension|.
// Note: The reference returned has the same lifetime as |extension|.
const std::string& GetPartitionDomainForExtension(const Extension* extension);

// Returns an extension specific StoragePartitionConfig if the extension
// associated with |extension_id| has isolated storage.
// Otherwise, return the default StoragePartitionConfig.
content::StoragePartitionConfig GetStoragePartitionConfigForExtensionId(
    const ExtensionId& extension_id,
    content::BrowserContext* browser_context);

content::StoragePartition* GetStoragePartitionForExtensionId(
    const ExtensionId& extension_id,
    content::BrowserContext* browser_context,
    bool can_create = true);

// Maps a |file_url| to a |file_path| on the local filesystem, including
// resources in extensions. Returns true on success. See NaClBrowserDelegate for
// full details. If |use_blocking_api| is false, only a subset of URLs will be
// handled. If |use_blocking_api| is true, blocking file operations may be used,
// and this must be called on threads that allow blocking. Otherwise this can be
// called on any thread.
bool MapUrlToLocalFilePath(const ExtensionSet* extensions,
                           const GURL& file_url,
                           bool use_blocking_api,
                           base::FilePath* file_path);

// Returns true if the browser can potentially withhold permissions from the
// extension.
bool CanWithholdPermissionsFromExtension(const Extension& extension);
bool CanWithholdPermissionsFromExtension(
    const ExtensionId& extension_id,
    const Manifest::Type type,
    const mojom::ManifestLocation location);

// Returns a unique int id for each context.
int GetBrowserContextId(content::BrowserContext* context);

// Returns whether the |extension| should be loaded in the given
// |browser_context|.
bool IsExtensionVisibleToContext(const Extension& extension,
                                 content::BrowserContext* browser_context);

// Initializes file scheme access if the extension has such permission.
void InitializeFileSchemeAccessForExtension(
    int render_process_id,
    const std::string& extension_id,
    content::BrowserContext* browser_context);

// Gets the ExtensionId associated with the given `site_instance`.  An empty
// string is returned when `site_instance` is not associated with an extension.
ExtensionId GetExtensionIdForSiteInstance(content::SiteInstance& site_instance);

// Returns true if the process corresponding to `render_process_id` can host an
// extension with `extension_id`.  (It doesn't necessarily mean that the process
// *does* host this specific extension at this point in time.)
bool CanRendererHostExtensionOrigin(int render_process_id,
                                    const ExtensionId& extension_id);

}  // namespace util
}  // namespace extensions

#endif  // EXTENSIONS_BROWSER_EXTENSION_UTIL_H_
