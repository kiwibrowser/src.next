// Copyright 2013 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/install_tracker.h"

#include "base/bind.h"
#include "base/observer_list.h"
#include "chrome/browser/extensions/install_tracker_factory.h"
#include "extensions/browser/extension_prefs.h"
#include "extensions/browser/extension_system.h"
#include "extensions/browser/pref_names.h"

namespace extensions {

InstallTracker::InstallTracker(content::BrowserContext* browser_context,
                               extensions::ExtensionPrefs* prefs) {
  extension_registry_observation_.Observe(
      ExtensionRegistry::Get(browser_context));

  // Prefs may be null in tests.
  if (prefs) {
    pref_change_registrar_.Init(prefs->pref_service());
    pref_change_registrar_.Add(
        pref_names::kExtensions,
        base::BindRepeating(&InstallTracker::OnExtensionPrefChanged,
                            base::Unretained(this)));
  }
}

InstallTracker::~InstallTracker() {
}

// static
InstallTracker* InstallTracker::Get(content::BrowserContext* context) {
  return InstallTrackerFactory::GetForBrowserContext(context);
}

void InstallTracker::AddObserver(InstallObserver* observer) {
  observers_.AddObserver(observer);
}

void InstallTracker::RemoveObserver(InstallObserver* observer) {
  observers_.RemoveObserver(observer);
}

const ActiveInstallData* InstallTracker::GetActiveInstall(
    const std::string& extension_id) const {
  auto install_data = active_installs_.find(extension_id);
  if (install_data == active_installs_.end())
    return nullptr;
  else
    return &(install_data->second);
}

void InstallTracker::AddActiveInstall(const ActiveInstallData& install_data) {
  DCHECK(!install_data.extension_id.empty());
  DCHECK(active_installs_.find(install_data.extension_id) ==
         active_installs_.end());
  active_installs_.insert(
      std::make_pair(install_data.extension_id, install_data));
}

void InstallTracker::RemoveActiveInstall(const std::string& extension_id) {
  active_installs_.erase(extension_id);
}

void InstallTracker::OnBeginExtensionInstall(
    const InstallObserver::ExtensionInstallParams& params) {
  auto install_data = active_installs_.find(params.extension_id);
  if (install_data == active_installs_.end()) {
    ActiveInstallData active_install_data(params.extension_id);
    active_installs_.insert(
        std::make_pair(params.extension_id, active_install_data));
  }

  for (auto& observer : observers_)
    observer.OnBeginExtensionInstall(params);
}

void InstallTracker::OnBeginExtensionDownload(const std::string& extension_id) {
  for (auto& observer : observers_)
    observer.OnBeginExtensionDownload(extension_id);
}

void InstallTracker::OnDownloadProgress(const std::string& extension_id,
                                        int percent_downloaded) {
  auto install_data = active_installs_.find(extension_id);
  if (install_data != active_installs_.end()) {
    install_data->second.percent_downloaded = percent_downloaded;
  } else {
    NOTREACHED();
  }

  for (auto& observer : observers_)
    observer.OnDownloadProgress(extension_id, percent_downloaded);
}

void InstallTracker::OnBeginCrxInstall(const std::string& extension_id) {
  for (auto& observer : observers_)
    observer.OnBeginCrxInstall(extension_id);
}

void InstallTracker::OnFinishCrxInstall(const std::string& extension_id,
                                        bool success) {
  for (auto& observer : observers_)
    observer.OnFinishCrxInstall(extension_id, success);
}

void InstallTracker::OnInstallFailure(
    const std::string& extension_id) {
  RemoveActiveInstall(extension_id);
  for (auto& observer : observers_)
    observer.OnInstallFailure(extension_id);
}

void InstallTracker::Shutdown() {
  // Note: tests may call this method prematurely to avoid shutdown ordering
  // issues. Make sure observers don't need to handle this awkward complexity by
  // clearing them here and making this method idempotent.
  for (auto& observer : observers_)
    observer.OnShutdown();
  observers_.Clear();
  pref_change_registrar_.RemoveAll();
}

void InstallTracker::OnExtensionInstalled(
    content::BrowserContext* browser_context,
    const Extension* extension,
    bool is_update) {
  RemoveActiveInstall(extension->id());
}

void InstallTracker::OnAppsReordered(
    const absl::optional<std::string>& extension_id) {
  for (auto& observer : observers_)
    observer.OnAppsReordered(extension_id);
}

void InstallTracker::OnExtensionPrefChanged() {
  OnAppsReordered(absl::nullopt);
}

}  // namespace extensions
