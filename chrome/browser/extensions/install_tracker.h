// Copyright 2013 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_INSTALL_TRACKER_H_
#define CHROME_BROWSER_EXTENSIONS_INSTALL_TRACKER_H_

#include <map>

#include "base/observer_list.h"
#include "base/scoped_observation.h"
#include "chrome/browser/extensions/active_install_data.h"
#include "chrome/browser/extensions/install_observer.h"
#include "components/keyed_service/core/keyed_service.h"
#include "components/prefs/pref_change_registrar.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/extension_registry_observer.h"
#include "extensions/common/extension_id.h"
#include "third_party/abseil-cpp/absl/types/optional.h"

namespace content {
class BrowserContext;
}

namespace extensions {

class ExtensionPrefs;

class InstallTracker : public KeyedService, public ExtensionRegistryObserver {
 public:
  InstallTracker(content::BrowserContext* browser_context,
                 extensions::ExtensionPrefs* prefs);

  InstallTracker(const InstallTracker&) = delete;
  InstallTracker& operator=(const InstallTracker&) = delete;

  ~InstallTracker() override;

  static InstallTracker* Get(content::BrowserContext* context);

  void AddObserver(InstallObserver* observer);
  void RemoveObserver(InstallObserver* observer);

  // If an install is currently in progress for |extension_id|, returns details
  // of the installation. This instance retains ownership of the returned
  // pointer. Returns NULL if the extension is not currently being installed.
  const ActiveInstallData* GetActiveInstall(
      const std::string& extension_id) const;

  // Registers an install initiated by the user to allow checking of duplicate
  // installs. Download of the extension has not necessarily started.
  // RemoveActiveInstall() must be called when install is complete regardless of
  // success or failure. Consider using ScopedActiveInstall rather than calling
  // this directly.
  void AddActiveInstall(const ActiveInstallData& install_data);

  // Deregisters an active install.
  void RemoveActiveInstall(const std::string& extension_id);

  void OnBeginExtensionInstall(
      const InstallObserver::ExtensionInstallParams& params);
  void OnBeginExtensionDownload(const std::string& extension_id);
  void OnDownloadProgress(const std::string& extension_id,
                          int percent_downloaded);
  void OnBeginCrxInstall(const std::string& extension_id);
  void OnFinishCrxInstall(const std::string& extension_id, bool success);
  void OnInstallFailure(const std::string& extension_id);

  // NOTE(limasdf): For extension [un]load and [un]installed, use
  //                ExtensionRegistryObserver.

  // Overriddes for KeyedService.
  void Shutdown() override;

  // Called directly by AppSorting logic when apps are re-ordered on the new tab
  // page.
  void OnAppsReordered(const absl::optional<ExtensionId>& extension_id);

 private:
  void OnExtensionPrefChanged();

  // ExtensionRegistryObserver implementation.
  void OnExtensionInstalled(content::BrowserContext* browser_context,
                            const Extension* extension,
                            bool is_update) override;

  // Maps extension id to the details of an active install.
  typedef std::map<std::string, ActiveInstallData> ActiveInstallsMap;
  ActiveInstallsMap active_installs_;

  base::ObserverList<InstallObserver>::Unchecked observers_;
  PrefChangeRegistrar pref_change_registrar_;
  base::ScopedObservation<ExtensionRegistry, ExtensionRegistryObserver>
      extension_registry_observation_{this};
};

}  // namespace extensions

#endif  // CHROME_BROWSER_EXTENSIONS_INSTALL_TRACKER_H_
