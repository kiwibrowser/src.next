// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_PENDING_EXTENSION_MANAGER_H_
#define CHROME_BROWSER_EXTENSIONS_PENDING_EXTENSION_MANAGER_H_

#include <list>
#include <map>
#include <string>

#include "base/gtest_prod_util.h"
#include "base/memory/raw_ptr.h"
#include "base/time/time.h"
#include "chrome/browser/extensions/pending_extension_info.h"
#include "extensions/common/extension_id.h"
#include "extensions/common/manifest.h"
#include "third_party/abseil-cpp/absl/types/optional.h"
#include "third_party/blink/public/mojom/manifest/manifest.mojom-shared.h"

class GURL;

namespace base {
class Version;
}

namespace content {
class BrowserContext;
}

namespace extensions {
FORWARD_DECLARE_TEST(ExtensionServiceTest,
                     UpdatePendingExtensionAlreadyInstalled);

class PendingExtensionManager;

class ExtensionUpdaterTest;
void SetupPendingExtensionManagerForTest(
    int count, const GURL& update_url,
    PendingExtensionManager* pending_extension_manager);

// Class PendingExtensionManager manages the set of extensions which are
// being installed or updated. In general, installation and updates take
// time, because they involve downloading, unpacking, and installing.
// This class allows us to avoid race cases where multiple sources install
// the same extension.
// The ExtensionService creates an instance of this class, and manages its
// lifetime. This class should only be used from the UI thread.
class PendingExtensionManager {
 public:
  explicit PendingExtensionManager(content::BrowserContext* context);

  PendingExtensionManager(const PendingExtensionManager&) = delete;
  PendingExtensionManager& operator=(const PendingExtensionManager&) = delete;

  ~PendingExtensionManager();

  // TODO(skerner): Many of these methods can be private once code in
  // ExtensionService is moved into methods of this class.

  // Remove extension with id |id| from the set of pending extensions. Returns
  // true if such an extension was found and removed, false otherwise.
  bool Remove(const std::string& id);

  // Get the  information for a pending extension.  Returns a pointer to the
  // pending extension with id |id|, or NULL if there is no such extension.
  const PendingExtensionInfo* GetById(const std::string& id) const;

  // Is |id| in the set of pending extensions?
  bool IsIdPending(const std::string& id) const;

  // Returns true if there are any extensions pending.
  bool HasPendingExtensions() const;

  // Whether there is pending extension install from sync.
  bool HasPendingExtensionFromSync() const;

  // Whether there is a high-priority pending extension (one from either policy
  // or an external component extension).
  bool HasHighPriorityPendingExtension() const;

  // Adds an extension in a pending state; the extension with the
  // given info will be installed on the next auto-update cycle.
  // Return true if the extension was added.  Will return false
  // if the extension is pending from another source which overrides
  // sync installs (such as a policy extension) or if the extension
  // is already installed.
  // After installation, the extension will be granted permissions iff
  // |version| is valid and matches the actual installed version.
  bool AddFromSync(
      const std::string& id,
      const GURL& update_url,
      const base::Version& version,
      PendingExtensionInfo::ShouldAllowInstallPredicate should_allow_install,
      bool remote_install);

  // Adds an extension that was depended on by another extension.
  bool AddFromExtensionImport(
      const std::string& id,
      const GURL& update_url,
      PendingExtensionInfo::ShouldAllowInstallPredicate should_allow_install);

  // Given an extension id and an update URL, schedule the extension
  // to be fetched, installed, and activated.
  bool AddFromExternalUpdateUrl(const std::string& id,
                                const std::string& install_parameter,
                                const GURL& update_url,
                                mojom::ManifestLocation location,
                                int creation_flags,
                                bool mark_acknowledged);

  // Add a pending extension record for an external CRX file.
  // Return true if the CRX should be installed, false if an existing
  // pending record overrides it.
  bool AddFromExternalFile(const std::string& id,
                           mojom::ManifestLocation location,
                           const base::Version& version,
                           int creation_flags,
                           bool mark_acknowledged);

  // Get the list of pending IDs that should be installed from an update URL.
  // Pending extensions that will be installed from local files will not be
  // included in the set.
  std::list<std::string> GetPendingIdsForUpdateCheck() const;

 private:
  // Assumes an extension with id |id| is not already installed.
  // Return true if the extension was added.
  bool AddExtensionImpl(
      const std::string& id,
      const std::string& install_parameter,
      const GURL& update_url,
      const base::Version& version,
      PendingExtensionInfo::ShouldAllowInstallPredicate should_allow_install,
      bool is_from_sync,
      mojom::ManifestLocation install_source,
      int creation_flags,
      bool mark_acknowledged,
      bool remote_install);

  // Caches the set of Chrome app IDs undergoing migration to web apps because
  // it is expensive to generate every time (multiple SkBitmap copies).
  void EnsureMigratedDefaultChromeAppIdsCachePopulated();

  // Add a pending extension record directly.  Used for unit tests that need
  // to set an inital state. Use friendship to allow the tests to call this
  // method.
  void AddForTesting(PendingExtensionInfo pending_extension_info);

  // The BrowserContext with which the manager is associated.
  raw_ptr<content::BrowserContext> context_;

  std::map<std::string, PendingExtensionInfo> pending_extensions_;

  absl::optional<base::flat_set<std::string>>
      migrating_default_chrome_app_ids_cache_;

  FRIEND_TEST_ALL_PREFIXES(ExtensionServiceTest,
                           UpdatePendingExtensionAlreadyInstalled);
  friend class ExtensionUpdaterTest;
  friend void SetupPendingExtensionManagerForTest(
      int count, const GURL& update_url,
      PendingExtensionManager* pending_extension_manager);
};

}  // namespace extensions

#endif  // CHROME_BROWSER_EXTENSIONS_PENDING_EXTENSION_MANAGER_H_
