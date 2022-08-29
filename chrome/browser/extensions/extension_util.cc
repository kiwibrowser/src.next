// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/extension_util.h"

#include <vector>

#include "base/check_op.h"
#include "base/command_line.h"
#include "base/feature_list.h"
#include "base/metrics/field_trial.h"
#include "base/values.h"
#include "build/build_config.h"
#include "build/chromeos_buildflags.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/extensions/extension_sync_service.h"
#include "chrome/browser/extensions/launch_util.h"
#include "chrome/browser/extensions/permissions_updater.h"
#include "chrome/browser/extensions/scripting_permissions_modifier.h"
#include "chrome/browser/extensions/shared_module_service.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/webui/extensions/extension_icon_source.h"
#include "chrome/browser/web_applications/extensions/bookmark_app_util.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/extensions/api/url_handlers/url_handlers_parser.h"
#include "chrome/common/extensions/sync_helper.h"
#include "components/variations/variations_associated_data.h"
#include "components/webapps/browser/banners/app_banner_manager.h"
#include "content/public/browser/site_instance.h"
#include "extensions/browser/disable_reason.h"
#include "extensions/browser/extension_prefs.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/extension_system.h"
#include "extensions/browser/extension_util.h"
#include "extensions/common/extension.h"
#include "extensions/common/extension_icon_set.h"
#include "extensions/common/extension_urls.h"
#include "extensions/common/manifest.h"
#include "extensions/common/manifest_handlers/app_isolation_info.h"
#include "extensions/common/manifest_handlers/incognito_info.h"
#include "extensions/common/manifest_handlers/permissions_parser.h"
#include "extensions/common/permissions/permissions_data.h"
#include "extensions/grit/extensions_browser_resources.h"
#include "ui/base/resource/resource_bundle.h"
#include "url/gurl.h"

#if BUILDFLAG(IS_CHROMEOS_ASH)
#include "chrome/browser/ash/file_manager/app_id.h"
#include "chrome/browser/ash/profiles/profile_helper.h"
#endif

namespace extensions {
namespace util {

namespace {
// Returns |extension_id|. See note below.
std::string ReloadExtensionIfEnabled(const std::string& extension_id,
                                     content::BrowserContext* context) {
  ExtensionRegistry* registry = ExtensionRegistry::Get(context);
  bool extension_is_enabled =
      registry->enabled_extensions().Contains(extension_id);

  if (!extension_is_enabled)
    return extension_id;

  // When we reload the extension the ID may be invalidated if we've passed it
  // by const ref everywhere. Make a copy to be safe. http://crbug.com/103762
  std::string id = extension_id;
  ExtensionService* service =
      ExtensionSystem::Get(context)->extension_service();
  CHECK(service);
  service->ReloadExtension(id);
  return id;
}

}  // namespace

bool HasIsolatedStorage(const std::string& extension_id,
                        content::BrowserContext* context) {
  const Extension* extension =
      ExtensionRegistry::Get(context)->enabled_extensions().GetByID(
          extension_id);

#if BUILDFLAG(IS_CHROMEOS_ASH)
  const bool is_policy_extension =
      extension && Manifest::IsPolicyLocation(extension->location());
  Profile* profile = Profile::FromBrowserContext(context);
  if (profile && ash::ProfileHelper::IsSigninProfile(profile) &&
      is_policy_extension) {
    return true;
  }
#endif

  return extension && AppIsolationInfo::HasIsolatedStorage(extension);
}

void SetIsIncognitoEnabled(const std::string& extension_id,
                           content::BrowserContext* context,
                           bool enabled) {
  ExtensionRegistry* registry = ExtensionRegistry::Get(context);
  const Extension* extension =
      registry->GetExtensionById(extension_id, ExtensionRegistry::EVERYTHING);

  if (extension) {
    if (!util::CanBeIncognitoEnabled(extension))
      return;

    // TODO(treib,kalman): Should this be Manifest::IsComponentLocation(..)?
    // (which also checks for kExternalComponent).
    if (extension->location() == mojom::ManifestLocation::kComponent) {
      // This shouldn't be called for component extensions unless it is called
      // by sync, for syncable component extensions.
      // See http://crbug.com/112290 and associated CLs for the sordid history.
      bool syncable = sync_helper::IsSyncableComponentExtension(extension);
#if BUILDFLAG(IS_CHROMEOS_ASH)
      // For some users, the file manager app somehow ended up being synced even
      // though it's supposed to be unsyncable; see crbug.com/576964. If the bad
      // data ever gets cleaned up, this hack should be removed.
      syncable = syncable || extension->id() == file_manager::kFileManagerAppId;
#endif
      DCHECK(syncable);

      // If we are here, make sure the we aren't trying to change the value.
      DCHECK_EQ(enabled, IsIncognitoEnabled(extension_id, context));
      return;
    }
  }

  ExtensionPrefs* extension_prefs = ExtensionPrefs::Get(context);
  // Broadcast unloaded and loaded events to update browser state. Only bother
  // if the value changed and the extension is actually enabled, since there is
  // no UI otherwise.
  bool old_enabled = extension_prefs->IsIncognitoEnabled(extension_id);
  if (enabled == old_enabled)
    return;

  extension_prefs->SetIsIncognitoEnabled(extension_id, enabled);

  std::string id = ReloadExtensionIfEnabled(extension_id, context);

  // Reloading the extension invalidates the |extension| pointer.
  extension = registry->GetExtensionById(id, ExtensionRegistry::EVERYTHING);
  if (extension) {
    Profile* profile = Profile::FromBrowserContext(context);
    ExtensionSyncService::Get(profile)->SyncExtensionChangeIfNeeded(*extension);
  }
}

bool CanLoadInIncognito(const Extension* extension,
                        content::BrowserContext* context) {
  CHECK(extension);
  if (extension->is_hosted_app())
    return true;
  // Packaged apps and regular extensions need to be enabled specifically for
  // incognito (and split mode should be set).
  return IncognitoInfo::IsSplitMode(extension) &&
         IsIncognitoEnabled(extension->id(), context);
}

bool AllowFileAccess(const std::string& extension_id,
                     content::BrowserContext* context) {
  return base::CommandLine::ForCurrentProcess()->HasSwitch(
             ::switches::kDisableExtensionsFileAccessCheck) ||
         ExtensionPrefs::Get(context)->AllowFileAccess(extension_id);
}

void SetAllowFileAccess(const std::string& extension_id,
                        content::BrowserContext* context,
                        bool allow) {
  // Reload to update browser state. Only bother if the value changed and the
  // extension is actually enabled, since there is no UI otherwise.
  if (allow == AllowFileAccess(extension_id, context))
    return;

  ExtensionPrefs::Get(context)->SetAllowFileAccess(extension_id, allow);

  ReloadExtensionIfEnabled(extension_id, context);
}

bool IsAppLaunchable(const std::string& extension_id,
                     content::BrowserContext* context) {
  int reason = ExtensionPrefs::Get(context)->GetDisableReasons(extension_id);
  return !((reason & disable_reason::DISABLE_UNSUPPORTED_REQUIREMENT) ||
           (reason & disable_reason::DISABLE_CORRUPTED));
}

bool IsAppLaunchableWithoutEnabling(const std::string& extension_id,
                                    content::BrowserContext* context) {
  return ExtensionRegistry::Get(context)->GetExtensionById(
      extension_id, ExtensionRegistry::ENABLED) != NULL;
}

bool ShouldSync(const Extension* extension,
                content::BrowserContext* context) {
  ExtensionManagement* extension_management =
      ExtensionManagementFactory::GetForBrowserContext(context);
  // Update URL is overridden only for non webstore extensions and offstore
  // extensions should not be synced.
  if (extension_management->IsUpdateUrlOverridden(extension->id())) {
    const GURL update_url =
        extension_management->GetEffectiveUpdateURL(*extension);
    DCHECK(!extension_urls::IsWebstoreUpdateUrl(update_url))
        << "Update URL cannot be overridden to be the webstore URL!";
    return false;
  }
  return sync_helper::IsSyncable(extension) &&
         !ExtensionPrefs::Get(context)->DoNotSync(extension->id());
}

bool IsExtensionIdle(const std::string& extension_id,
                     content::BrowserContext* context) {
  std::vector<std::string> ids_to_check;
  ids_to_check.push_back(extension_id);

  const Extension* extension =
      ExtensionRegistry::Get(context)
          ->GetExtensionById(extension_id, ExtensionRegistry::ENABLED);
  if (extension && extension->is_shared_module()) {
    // We have to check all the extensions that use this shared module for idle
    // to tell whether it is really 'idle'.
    SharedModuleService* service = ExtensionSystem::Get(context)
                                       ->extension_service()
                                       ->shared_module_service();
    std::unique_ptr<ExtensionSet> dependents =
        service->GetDependentExtensions(extension);
    for (ExtensionSet::const_iterator i = dependents->begin();
         i != dependents->end();
         i++) {
      ids_to_check.push_back((*i)->id());
    }
  }

  ProcessManager* process_manager = ProcessManager::Get(context);
  for (std::vector<std::string>::const_iterator i = ids_to_check.begin();
       i != ids_to_check.end();
       i++) {
    const std::string id = (*i);
    ExtensionHost* host = process_manager->GetBackgroundHostForExtension(id);
    if (host)
      return false;

    scoped_refptr<content::SiteInstance> site_instance =
        process_manager->GetSiteInstanceForURL(
            Extension::GetBaseURLFromExtensionId(id));
    if (site_instance && site_instance->HasProcess())
      return false;

    if (!process_manager->GetRenderFrameHostsForExtension(id).empty())
      return false;
  }
  return true;
}

std::unique_ptr<base::DictionaryValue> GetExtensionInfo(
    const Extension* extension) {
  DCHECK(extension);
  std::unique_ptr<base::DictionaryValue> dict(new base::DictionaryValue);

  dict->SetStringKey("id", extension->id());
  dict->SetStringKey("name", extension->name());

  GURL icon = extensions::ExtensionIconSource::GetIconURL(
      extension, extension_misc::EXTENSION_ICON_SMALLISH,
      ExtensionIconSet::MATCH_BIGGER,
      false);  // Not grayscale.
  dict->SetStringKey("icon", icon.spec());

  return dict;
}

const gfx::ImageSkia& GetDefaultAppIcon() {
  return *ui::ResourceBundle::GetSharedInstance().GetImageSkiaNamed(
      IDR_APP_DEFAULT_ICON);
}

const gfx::ImageSkia& GetDefaultExtensionIcon() {
  return *ui::ResourceBundle::GetSharedInstance().GetImageSkiaNamed(
      IDR_EXTENSION_DEFAULT_ICON);
}

std::unique_ptr<const PermissionSet> GetInstallPromptPermissionSetForExtension(
    const Extension* extension,
    Profile* profile,
    bool include_optional_permissions) {
  // Initialize permissions if they have not already been set so that
  // any transformations are correctly reflected in the install prompt.
  PermissionsUpdater(profile, PermissionsUpdater::INIT_FLAG_TRANSIENT)
      .InitializePermissions(extension);

  std::unique_ptr<const PermissionSet> permissions_to_display =
      extension->permissions_data()->active_permissions().Clone();

  if (include_optional_permissions) {
    const PermissionSet& optional_permissions =
        PermissionsParser::GetOptionalPermissions(extension);
    permissions_to_display = PermissionSet::CreateUnion(*permissions_to_display,
                                                        optional_permissions);
  }
  return permissions_to_display;
}

std::vector<content::BrowserContext*> GetAllRelatedProfiles(
    Profile* profile,
    const Extension& extension) {
  std::vector<content::BrowserContext*> related_contexts;
  related_contexts.push_back(profile->GetOriginalProfile());

  // The returned `related_contexts` should include all the related incognito
  // profiles if the extension is globally allowed in incognito (this is a
  // global, rather than per-profile toggle - this is why we it can be checked
  // globally here, rather than once for every incognito profile looped over
  // below).
  if (IsIncognitoEnabled(extension.id(), profile)) {
    std::vector<Profile*> off_the_record_profiles =
        profile->GetAllOffTheRecordProfiles();
    related_contexts.reserve(related_contexts.size() +
                             off_the_record_profiles.size());
    for (Profile* off_the_record_profile : off_the_record_profiles)
      related_contexts.push_back(off_the_record_profile);
  }

  return related_contexts;
}

}  // namespace util
}  // namespace extensions
