// Copyright 2013 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef EXTENSIONS_COMMON_EXTENSION_URLS_H_
#define EXTENSIONS_COMMON_EXTENSION_URLS_H_

#include <string>

#include "base/strings/string_piece.h"
#include "url/gurl.h"

namespace url {
class Origin;
}

namespace extensions {

// Determine whether or not a source came from an extension. |source| can link
// to a page or a script, and can be external (e.g., "http://www.google.com"),
// extension-related (e.g., "chrome-extension://<extension_id>/background.js"),
// or internal (e.g., "event_bindings" or "schemaUtils").
bool IsSourceFromAnExtension(const std::u16string& source);

}  // namespace extensions

namespace extension_urls {

// Canonical URLs for the Chrome Webstore. You probably want to use one of
// the calls below rather than using one of these constants directly, since
// the active extensions embedder may provide its own webstore URLs.
extern const char kChromeWebstoreBaseURL[];
extern const char kChromeWebstoreUpdateURL[];
extern const char kNewChromeWebstoreBaseURL[];

// Returns the URL prefix for the extension/apps gallery. Can be set via the
// --apps-gallery-url switch. The URL returned will not contain a trailing
// slash. Do not use this as a prefix/extent for the store.
GURL GetWebstoreLaunchURL();
GURL GetNewWebstoreLaunchURL();

// Returns the URL to the extensions category on the Web Store. This is
// derived from GetWebstoreLaunchURL().
std::string GetWebstoreExtensionsCategoryURL();

// Returns the URL prefix for an item in the extension/app gallery. This URL
// will contain a trailing slash and should be concatenated with an item ID
// to get the item detail URL.
std::string GetWebstoreItemDetailURLPrefix();

// Returns the URL used to get webstore data (ratings, manifest, icon URL,
// etc.) about an extension from the webstore as JSON.
GURL GetWebstoreItemJsonDataURL(const std::string& extension_id);

// Returns the compile-time constant webstore update url specific to
// Chrome. Usually you should prefer using GetWebstoreUpdateUrl.
GURL GetDefaultWebstoreUpdateUrl();

// Return the update URL used by gallery/webstore extensions/apps. This may
// have been overridden by a command line flag for testing purposes.
GURL GetWebstoreUpdateUrl();

// Returns the url to visit to report abuse for the given |extension_id|
// and |referrer_id|.
GURL GetWebstoreReportAbuseUrl(const std::string& extension_id,
                               const std::string& referrer_id);

// Returns whether the URL's host matches or is in the same domain as any of the
// webstore URLs. Note: This includes any subdomains of the webstore URLs.
// TODO(crbug.com/1355623): We should move the domain checks for the webstore to
// use the IsSameOrigin version below where appropriate.
bool IsWebstoreDomain(const GURL& url);

// Returns whether the origin is the same origin as any of the webstore URLs.
bool IsWebstoreOrigin(const url::Origin& origin);

// Returns whether the URL is the webstore update URL (just considering host
// and path, not scheme, query, etc.)
bool IsWebstoreUpdateUrl(const GURL& update_url);

// Returns true if the URL points to an extension blocklist.
bool IsBlocklistUpdateUrl(const GURL& url);

// Returns true if the origin points to an URL used for safebrowsing.
// TODO(devlin): Update other methods to also take an url::Origin?
bool IsSafeBrowsingUrl(const url::Origin& origin, base::StringPiece path);

}  // namespace extension_urls

#endif  // EXTENSIONS_COMMON_EXTENSION_URLS_H_
