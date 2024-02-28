// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_SCRIPTING_PERMISSIONS_MODIFIER_H_
#define CHROME_BROWSER_EXTENSIONS_SCRIPTING_PERMISSIONS_MODIFIER_H_

#include <memory>

#include "base/memory/raw_ptr.h"
#include "base/memory/ref_counted.h"

class GURL;

namespace content {
class BrowserContext;
}

namespace extensions {
class Extension;
class ExtensionPrefs;
class PermissionSet;
class PermissionsManager;

// Responsible for managing the majority of click-to-script features, including
// granting, withholding, and querying host permissions, and determining if an
// extension has been affected by the click-to-script project.
class ScriptingPermissionsModifier {
 public:
  ScriptingPermissionsModifier(content::BrowserContext* browser_context,
                               const scoped_refptr<const Extension>& extension);

  ScriptingPermissionsModifier(const ScriptingPermissionsModifier&) = delete;
  ScriptingPermissionsModifier& operator=(const ScriptingPermissionsModifier&) =
      delete;

  ~ScriptingPermissionsModifier();

  // Sets whether Chrome should withhold host permissions from the extension.
  // This may only be called for extensions that can be affected (i.e., for
  // which CanAffectExtension() returns true). Anything else will DCHECK.
  void SetWithholdHostPermissions(bool withhold);

  // Grants the extension permission to run on the origin of |url|.
  // This may only be called for extensions that can be affected (i.e., for
  // which CanAffectExtension() returns true). Anything else will DCHECK.
  void GrantHostPermission(const GURL& url);

  // Revokes permission to run on the origin of |url|, including any permissions
  // that match or overlap with the origin. For instance, removing access to
  // https://google.com will remove access to *://*.com/* as well.
  // DCHECKs if |url| has not been granted.
  // This may only be called for extensions that can be affected (i.e., for
  // which CanAffectExtension() returns true). Anything else will DCHECK.
  void RemoveGrantedHostPermission(const GURL& url);

  // Revokes host permission patterns granted to the extension that effectively
  // grant access to all urls.
  void RemoveBroadGrantedHostPermissions();

  // Revokes all host permissions granted to the extension. Note that this will
  // only withhold hosts explicitly granted to the extension; this will not
  // implicitly change the value of HasWithheldHostPermissions().
  // This may only be called for extensions that can be affected (i.e., for
  // which CanAffectExtension() returns true). Anything else will DCHECK.
  void RemoveAllGrantedHostPermissions();

  // Takes in a set of permissions and withholds any permissions that should not
  // be granted for the given |extension|, returning a permission set with all
  // of the permissions that can be granted.
  // Note: we pass in |permissions| explicitly here, as this is used during
  // permission initialization, where the active permissions on the extension
  // may not be the permissions to compare against.
  std::unique_ptr<const PermissionSet> WithholdPermissionsIfNecessary(
      const PermissionSet& permissions);

 private:
  // Grants any withheld host permissions.
  void GrantWithheldHostPermissions();

  // Revokes any granted host permissions.
  void WithholdHostPermissions();

  raw_ptr<content::BrowserContext> browser_context_;

  scoped_refptr<const Extension> extension_;

  raw_ptr<ExtensionPrefs> extension_prefs_;
  raw_ptr<PermissionsManager> permissions_manager_;
};

}  // namespace extensions

#endif  // CHROME_BROWSER_EXTENSIONS_SCRIPTING_PERMISSIONS_MODIFIER_H_
