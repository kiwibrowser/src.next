// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/extension_special_storage_policy.h"

#include <stddef.h>
#include <stdint.h>
#include <utility>

#include "base/bind.h"
#include "base/callback.h"
#include "base/check.h"
#include "base/command_line.h"
#include "base/feature_list.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/ref_counted.h"
#include "base/metrics/histogram_macros.h"
#include "base/strings/utf_string_conversions.h"
#include "base/synchronization/lock.h"
#include "base/task/task_traits.h"
#include "base/thread_annotations.h"
#include "build/chromeos_buildflags.h"
#include "chrome/browser/content_settings/cookie_settings_factory.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/url_constants.h"
#include "components/content_settings/core/browser/cookie_settings.h"
#include "components/content_settings/core/common/content_settings.h"
#include "components/content_settings/core/common/content_settings_types.h"
#include "content/public/browser/browser_task_traits.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/common/url_constants.h"
#include "extensions/common/constants.h"
#include "extensions/common/extension.h"
#include "extensions/common/extension_set.h"
#include "extensions/common/manifest_handlers/app_isolation_info.h"
#include "extensions/common/manifest_handlers/content_capabilities_handler.h"
#include "extensions/common/permissions/permissions_data.h"
#include "url/origin.h"

#if BUILDFLAG(IS_CHROMEOS_ASH)
#include "chrome/common/webui_url_constants.h"
#endif

using content::BrowserThread;
using extensions::APIPermission;
using extensions::Extension;
using extensions::mojom::APIPermissionID;
using storage::SpecialStoragePolicy;

class ExtensionSpecialStoragePolicy::CookieSettingsObserver
    : public content_settings::CookieSettings::Observer {
 public:
  CookieSettingsObserver(
      scoped_refptr<content_settings::CookieSettings> cookie_settings,
      ExtensionSpecialStoragePolicy* weak_policy)
      : cookie_settings_(std::move(cookie_settings)),
        weak_policy_(weak_policy) {
    if (cookie_settings_)
      cookie_settings_->AddObserver(this);
  }

  ~CookieSettingsObserver() override {
    if (cookie_settings_)
      cookie_settings_->RemoveObserver(this);
  }

  void WillDestroyPolicy() {
    base::AutoLock lock(policy_lock_);
    weak_policy_ = nullptr;
  }

 private:
  // content_settings::CookieSettings::Observer:
  void OnThirdPartyCookieBlockingChanged(bool) override {
    NotifyPolicyChanged();
  }

  void OnCookieSettingChanged() override { NotifyPolicyChanged(); }

  void NotifyPolicyChanged() {
    // Post a task to avoid any potential re-entrancy issues with
    // |NotifyPolicyChangedImpl()| since it holds a lock while calling back into
    // ExtensionSpecialStoragePolicy.
    content::GetIOThreadTaskRunner({})->PostTask(
        FROM_HERE,
        base::BindOnce(&CookieSettingsObserver::NotifyPolicyChangedImpl,
                       base::Unretained(this)));
  }

  void NotifyPolicyChangedImpl() {
    base::AutoLock lock(policy_lock_);
    if (weak_policy_)
      weak_policy_->NotifyPolicyChanged();
  }

  const scoped_refptr<content_settings::CookieSettings> cookie_settings_;

  base::Lock policy_lock_;
  raw_ptr<ExtensionSpecialStoragePolicy> weak_policy_ GUARDED_BY(policy_lock_);
};

ExtensionSpecialStoragePolicy::ExtensionSpecialStoragePolicy(
    content_settings::CookieSettings* cookie_settings)
    : cookie_settings_(cookie_settings),
      cookie_settings_observer_(
          new CookieSettingsObserver(cookie_settings_, this),
          base::OnTaskRunnerDeleter(content::GetUIThreadTaskRunner({}))) {}

ExtensionSpecialStoragePolicy::~ExtensionSpecialStoragePolicy() {
  cookie_settings_observer_->WillDestroyPolicy();
}

bool ExtensionSpecialStoragePolicy::IsStorageProtected(const GURL& origin) {
  if (origin.SchemeIs(extensions::kExtensionScheme))
    return true;
  base::AutoLock locker(lock_);
  return protected_apps_.Contains(origin);
}

bool ExtensionSpecialStoragePolicy::IsStorageUnlimited(const GURL& origin) {
  if (base::CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kUnlimitedStorage))
    return true;

  if (origin.SchemeIs(content::kChromeDevToolsScheme) &&
      origin.host_piece() == chrome::kChromeUIDevToolsHost)
    return true;

#if BUILDFLAG(IS_CHROMEOS_ASH)
  // chrome-untrusted://terminal/ runs the SSH extension code which can store
  // SSH known_hosts, config, and Identity keys. Use unlimitedStorage to match
  // extension config.
  if (origin == chrome::kChromeUIUntrustedTerminalURL)
    return true;
#endif

  base::AutoLock locker(lock_);
  return unlimited_extensions_.Contains(origin) ||
         content_capabilities_unlimited_extensions_.GrantsCapabilitiesTo(
             origin);
}

bool ExtensionSpecialStoragePolicy::IsStorageSessionOnly(const GURL& origin) {
  if (!cookie_settings_)
    return false;
  return cookie_settings_->IsCookieSessionOnly(
      origin, content_settings::CookieSettings::QueryReason::kSiteStorage);
}

bool ExtensionSpecialStoragePolicy::HasSessionOnlyOrigins() {
  if (!cookie_settings_)
    return false;
  if (cookie_settings_->GetDefaultCookieSetting(nullptr) ==
      CONTENT_SETTING_SESSION_ONLY)
    return true;
  for (const ContentSettingPatternSource& entry :
       cookie_settings_->GetCookieSettings()) {
    if (entry.GetContentSetting() == CONTENT_SETTING_SESSION_ONLY)
      return true;
  }
  return false;
}

bool ExtensionSpecialStoragePolicy::HasIsolatedStorage(const GURL& origin) {
  base::AutoLock locker(lock_);
  return isolated_extensions_.Contains(origin);
}

bool ExtensionSpecialStoragePolicy::IsStorageDurable(const GURL& origin) {
  return cookie_settings_->IsStorageDurable(origin);
}

bool ExtensionSpecialStoragePolicy::NeedsProtection(
    const extensions::Extension* extension) {
  // We only consider "protecting" storage for hosted apps.
  if (!extension->is_hosted_app())
    return false;

  // Default-installed apps don't have protected storage.
  if (extension->was_installed_by_default())
    return false;

  // Otherwise, this is a user-installed hosted app, and we grant it
  // special protected storage.
  return true;
}

const extensions::ExtensionSet*
ExtensionSpecialStoragePolicy::ExtensionsProtectingOrigin(
    const GURL& origin) {
  base::AutoLock locker(lock_);
  return protected_apps_.ExtensionsContaining(origin);
}

void ExtensionSpecialStoragePolicy::GrantRightsForExtension(
    const extensions::Extension* extension) {
  base::AutoLock locker(lock_);
  DCHECK(extension);

  int change_flags = 0;
  if (extensions::ContentCapabilitiesInfo::Get(extension).permissions.count(
          APIPermissionID::kUnlimitedStorage) > 0) {
    content_capabilities_unlimited_extensions_.Add(extension);
    change_flags |= SpecialStoragePolicy::STORAGE_UNLIMITED;
  }

  if (NeedsProtection(extension) ||
      extension->permissions_data()->HasAPIPermission(
          APIPermissionID::kUnlimitedStorage) ||
      extension->permissions_data()->HasAPIPermission(
          APIPermissionID::kFileBrowserHandler) ||
      extensions::AppIsolationInfo::HasIsolatedStorage(extension) ||
      extension->is_app()) {
    if (NeedsProtection(extension) && protected_apps_.Add(extension))
      change_flags |= SpecialStoragePolicy::STORAGE_PROTECTED;

    if (extension->permissions_data()->HasAPIPermission(
            APIPermissionID::kUnlimitedStorage) &&
        unlimited_extensions_.Add(extension)) {
      change_flags |= SpecialStoragePolicy::STORAGE_UNLIMITED;
    }

    if (extension->permissions_data()->HasAPIPermission(
            APIPermissionID::kFileBrowserHandler)) {
      file_handler_extensions_.Add(extension);
    }

    if (extensions::AppIsolationInfo::HasIsolatedStorage(extension))
      isolated_extensions_.Add(extension);
  }

  if (change_flags) {
    NotifyGranted(Extension::GetBaseURLFromExtensionId(extension->id()),
                  change_flags);
  }
}

void ExtensionSpecialStoragePolicy::RevokeRightsForExtension(
    const extensions::Extension* extension) {
  base::AutoLock locker(lock_);
  DCHECK(extension);

  int change_flags = 0;
  if (extensions::ContentCapabilitiesInfo::Get(extension).permissions.count(
          APIPermissionID::kUnlimitedStorage) > 0) {
    content_capabilities_unlimited_extensions_.Remove(extension);
    change_flags |= SpecialStoragePolicy::STORAGE_UNLIMITED;
  }

  if (NeedsProtection(extension) ||
      extension->permissions_data()->HasAPIPermission(
          APIPermissionID::kUnlimitedStorage) ||
      extension->permissions_data()->HasAPIPermission(
          APIPermissionID::kFileBrowserHandler) ||
      extensions::AppIsolationInfo::HasIsolatedStorage(extension) ||
      extension->is_app()) {
    if (NeedsProtection(extension) && protected_apps_.Remove(extension))
      change_flags |= SpecialStoragePolicy::STORAGE_PROTECTED;

    if (extension->permissions_data()->HasAPIPermission(
            APIPermissionID::kUnlimitedStorage) &&
        unlimited_extensions_.Remove(extension)) {
      change_flags |= SpecialStoragePolicy::STORAGE_UNLIMITED;
    }

    if (extension->permissions_data()->HasAPIPermission(
            APIPermissionID::kFileBrowserHandler)) {
      file_handler_extensions_.Remove(extension);
    }

    if (extensions::AppIsolationInfo::HasIsolatedStorage(extension))
      isolated_extensions_.Remove(extension);
  }

  if (change_flags) {
    NotifyRevoked(Extension::GetBaseURLFromExtensionId(extension->id()),
                  change_flags);
  }
}

void ExtensionSpecialStoragePolicy::RevokeRightsForAllExtensions() {
  {
    base::AutoLock locker(lock_);
    protected_apps_.Clear();
    unlimited_extensions_.Clear();
    file_handler_extensions_.Clear();
    isolated_extensions_.Clear();
    content_capabilities_unlimited_extensions_.Clear();
  }

  NotifyCleared();
}

void ExtensionSpecialStoragePolicy::NotifyGranted(
    const GURL& origin,
    int change_flags) {
  if (!BrowserThread::CurrentlyOn(BrowserThread::IO)) {
    content::GetIOThreadTaskRunner({})->PostTask(
        FROM_HERE, base::BindOnce(&ExtensionSpecialStoragePolicy::NotifyGranted,
                                  this, origin, change_flags));
    return;
  }
  SpecialStoragePolicy::NotifyGranted(url::Origin::Create(origin),
                                      change_flags);
}

void ExtensionSpecialStoragePolicy::NotifyRevoked(
    const GURL& origin,
    int change_flags) {
  if (!BrowserThread::CurrentlyOn(BrowserThread::IO)) {
    content::GetIOThreadTaskRunner({})->PostTask(
        FROM_HERE, base::BindOnce(&ExtensionSpecialStoragePolicy::NotifyRevoked,
                                  this, origin, change_flags));
    return;
  }
  SpecialStoragePolicy::NotifyRevoked(url::Origin::Create(origin),
                                      change_flags);
}

void ExtensionSpecialStoragePolicy::NotifyCleared() {
  if (!BrowserThread::CurrentlyOn(BrowserThread::IO)) {
    content::GetIOThreadTaskRunner({})->PostTask(
        FROM_HERE,
        base::BindOnce(&ExtensionSpecialStoragePolicy::NotifyCleared, this));
    return;
  }
  SpecialStoragePolicy::NotifyCleared();
}

//-----------------------------------------------------------------------------
// SpecialCollection helper class
//-----------------------------------------------------------------------------

ExtensionSpecialStoragePolicy::SpecialCollection::SpecialCollection() {}

ExtensionSpecialStoragePolicy::SpecialCollection::~SpecialCollection() {}

bool ExtensionSpecialStoragePolicy::SpecialCollection::Contains(
    const GURL& origin) {
  return !ExtensionsContaining(origin)->is_empty();
}

bool ExtensionSpecialStoragePolicy::SpecialCollection::GrantsCapabilitiesTo(
    const GURL& origin) {
  for (const auto& extension : extensions_) {
    if (extensions::ContentCapabilitiesInfo::Get(extension.get())
            .url_patterns.MatchesURL(origin)) {
      return true;
    }
  }
  return false;
}

const extensions::ExtensionSet*
ExtensionSpecialStoragePolicy::SpecialCollection::ExtensionsContaining(
    const GURL& origin) {
  std::unique_ptr<extensions::ExtensionSet>& result = cached_results_[origin];
  if (result)
    return result.get();

  result = std::make_unique<extensions::ExtensionSet>();
  for (auto& extension : extensions_) {
    if (extension->OverlapsWithOrigin(origin))
      result->Insert(extension);
  }

  return result.get();
}

bool ExtensionSpecialStoragePolicy::SpecialCollection::ContainsExtension(
    const std::string& extension_id) {
  return extensions_.Contains(extension_id);
}

bool ExtensionSpecialStoragePolicy::SpecialCollection::Add(
    const extensions::Extension* extension) {
  ClearCache();
  return extensions_.Insert(extension);
}

bool ExtensionSpecialStoragePolicy::SpecialCollection::Remove(
    const extensions::Extension* extension) {
  ClearCache();
  return extensions_.Remove(extension->id());
}

void ExtensionSpecialStoragePolicy::SpecialCollection::Clear() {
  ClearCache();
  extensions_.Clear();
}

void ExtensionSpecialStoragePolicy::SpecialCollection::ClearCache() {
  cached_results_.clear();
}
