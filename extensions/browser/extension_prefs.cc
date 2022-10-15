// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "extensions/browser/extension_prefs.h"

#include <stddef.h>
#include <stdint.h>

#include <iterator>
#include <utility>

#include "base/check_op.h"
#include "base/containers/contains.h"
#include "base/containers/cxx20_erase.h"
#include "base/json/values_util.h"
#include "base/metrics/histogram_macros.h"
#include "base/observer_list.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_piece.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/clock.h"
#include "base/time/default_clock.h"
#include "base/trace_event/trace_event.h"
#include "build/build_config.h"
#include "components/content_settings/core/common/content_settings.h"
#include "components/crx_file/id_util.h"
#include "components/pref_registry/pref_registry_syncable.h"
#include "components/prefs/pref_service.h"
#include "extensions/browser/api/declarative_net_request/utils.h"
#include "extensions/browser/app_sorting.h"
#include "extensions/browser/blocklist_extension_prefs.h"
#include "extensions/browser/blocklist_state.h"
#include "extensions/browser/disable_reason.h"
#include "extensions/browser/event_router.h"
#include "extensions/browser/extension_pref_store.h"
#include "extensions/browser/extension_prefs_factory.h"
#include "extensions/browser/extension_prefs_observer.h"
#include "extensions/browser/extension_system.h"
#include "extensions/browser/extension_util.h"
#include "extensions/browser/install_flag.h"
#include "extensions/browser/pref_names.h"
#include "extensions/common/constants.h"
#include "extensions/common/extension_features.h"
#include "extensions/common/manifest.h"
#include "extensions/common/permissions/permission_set.h"
#include "extensions/common/permissions/permissions_info.h"
#include "extensions/common/url_pattern.h"
#include "extensions/common/user_script.h"

using extensions::mojom::ManifestLocation;

namespace extensions {

namespace {

// Additional preferences keys, which are not needed by external clients.

// True if this extension is running. Note this preference stops getting updated
// during Chrome shutdown (and won't be updated on a browser crash) and so can
// be used at startup to determine whether the extension was running when Chrome
// was last terminated.
constexpr const char kPrefRunning[] = "running";

// Whether this extension had windows when it was last running.
constexpr const char kIsActive[] = "is_active";

// Where an extension was installed from. (see mojom::ManifestLocation)
constexpr const char kPrefLocation[] = "location";

// Enabled, disabled, killed, etc. (see Extension::State)
constexpr const char kPrefState[] = "state";

// The path to the current version's manifest file.
constexpr const char kPrefPath[] = "path";

// The dictionary containing the extension's manifest.
constexpr const char kPrefManifest[] = "manifest";

// The version number.
constexpr const char kPrefManifestVersion[] = "manifest.version";

// The count of how many times we prompted the user to acknowledge an
// extension.
constexpr const char kPrefAcknowledgePromptCount[] = "ack_prompt_count";

// Indicates whether the user has acknowledged various types of extensions.
constexpr const char kPrefExternalAcknowledged[] = "ack_external";

// Indicates whether the external extension was installed during the first
// run of this profile.
constexpr const char kPrefExternalInstallFirstRun[] = "external_first_run";

// A bitmask of all the reasons an extension is disabled.
constexpr const char kPrefDisableReasons[] = "disable_reasons";

// The key for a serialized Time value indicating the start of the day (from the
// server's perspective) an extension last included a "ping" parameter during
// its update check.
constexpr const char kLastPingDay[] = "lastpingday";

// Similar to kLastPingDay, but for "active" instead of "rollcall" pings.
constexpr const char kLastActivePingDay[] = "last_active_pingday";

// A bit we use to keep track of whether we need to do an "active" ping.
constexpr const char kActiveBit[] = "active_bit";

// Path for settings specific to blocklist update.
constexpr const char kExtensionsBlocklistUpdate[] =
    "extensions.blacklistupdate";

// Path for the delayed install info dictionary preference. The actual string
// value is a legacy artifact for when delayed installs only pertained to
// updates that were waiting for idle.
constexpr const char kDelayedInstallInfo[] = "idle_install_info";

// Reason why the extension's install was delayed.
constexpr const char kDelayedInstallReason[] = "delay_install_reason";

// Path for the suggested page ordinal of a delayed extension install.
constexpr const char kPrefSuggestedPageOrdinal[] = "suggested_page_ordinal";

// A preference that, if true, will allow this extension to run in incognito
// mode.
constexpr const char kPrefIncognitoEnabled[] = "incognito";

// A preference to control whether an extension is allowed to inject script in
// pages with file URLs.
constexpr const char kPrefAllowFileAccess[] = "newAllowFileAccess";
// TODO(jstritar): As part of fixing http://crbug.com/91577, we revoked all
// extension file access by renaming the pref. We should eventually clean up
// the old flag and possibly go back to that name.
// constexpr const char kPrefAllowFileAccessOld[] = "allowFileAccess";

// The set of permissions the extension desires to have active. This may include
// more than the required permissions from the manifest if the extension has
// optional permissions.
constexpr const char kPrefDesiredActivePermissions[] = "active_permissions";

// The set of permissions that the user has approved for the extension either at
// install time or through an optional permissions request. We track this in
// order to alert the user of permissions escalation.
// This also works with not-yet-recognized permissions (such as if an extension
// installed on stable channel uses a new permission that's only available in
// canary): the recorded granted permissions are determined from the recognized
// set of permissions, so when the new requested permission is later recognized
// (when it's available on stable), the requested set of permissions will
// differ from the stored granted set, and Chrome will notify the user of a
// permissions increase.
constexpr const char kPrefGrantedPermissions[] = "granted_permissions";

// Pref that was previously used to indicate if host permissions should be
// withheld. Due to the confusing name and the need to logically invert it when
// being used, we transitioned to use kPrefWithholdingPermissions
// instead.
const char kGrantExtensionAllHostPermissions[] =
    "extension_can_script_all_urls";

// A preference indicating if requested host permissions are being withheld from
// the extension, requiring them to be granted through the permissions API or
// runtime host permissions.
const char kPrefWithholdingPermissions[] = "withholding_permissions";

// The set of permissions that were granted at runtime, rather than at install
// time. This includes permissions granted through the permissions API and
// runtime host permissions.
constexpr const char kPrefRuntimeGrantedPermissions[] =
    "runtime_granted_permissions";

// The preference names for PermissionSet values.
constexpr const char kPrefAPIs[] = "api";
constexpr const char kPrefManifestPermissions[] = "manifest_permissions";
constexpr const char kPrefExplicitHosts[] = "explicit_host";
constexpr const char kPrefScriptableHosts[] = "scriptable_host";

// A preference that indicates when an extension was installed.
constexpr const char kPrefInstallTime[] = "install_time";

// A preference which saves the creation flags for extensions.
constexpr const char kPrefCreationFlags[] = "creation_flags";

// A preference that indicates whether the extension was installed from the
// Chrome Web Store.
constexpr const char kPrefFromWebStore[] = "from_webstore";

// A preference that indicates whether the extension was installed as a
// default app.
constexpr const char kPrefWasInstalledByDefault[] = "was_installed_by_default";

// A preference that indicates whether the extension was installed as an
// OEM app.
constexpr const char kPrefWasInstalledByOem[] = "was_installed_by_oem";

// Key for Geometry Cache preference.
constexpr const char kPrefGeometryCache[] = "geometry_cache";

// A preference that indicates when an extension is last launched.
constexpr const char kPrefLastLaunchTime[] = "last_launch_time";

// An installation parameter bundled with an extension.
constexpr const char kPrefInstallParam[] = "install_parameter";

// A list of installed ids and a signature.
constexpr const char kInstallSignature[] = "extensions.install_signature";

// A list of IDs of external extensions that the user has chosen to uninstall;
// saved as an indication to not re-install that extension.
constexpr const char kExternalUninstalls[] = "extensions.external_uninstalls";

// A boolean preference that indicates whether the extension should not be
// synced. Default value is false.
constexpr const char kPrefDoNotSync[] = "do_not_sync";

// A boolean preference that indicates whether the extension has local changes
// that need to be synced. Default value is false.
constexpr const char kPrefNeedsSync[] = "needs_sync";

// Stores preferences corresponding to static indexed rulesets for the
// Declarative Net Request API.
constexpr const char kDNRStaticRulesetPref[] = "dnr_static_ruleset";

// Stores preferences corresponding to dynamic indexed ruleset for the
// Declarative Net Request API. Note: we use a separate preference key for
// dynamic rulesets instead of using the |kDNRStaticRulesetPref| dictionary.
// This is because the |kDNRStaticRulesetPref| dictionary is re-populated on
// each packed extension update and also on reloads of unpacked extensions.
// However for both of these cases, we want the dynamic ruleset preferences to
// stay unchanged. Also, this helps provide flexibility to have the dynamic
// ruleset preference schema diverge from the static one.
constexpr const char kDNRDynamicRulesetPref[] = "dnr_dynamic_ruleset";

// Key corresponding to which we store a ruleset's checksum for the Declarative
// Net Request API.
constexpr const char kDNRChecksumKey[] = "checksum";

// Key corresponding to the list of enabled static ruleset IDs for an extension.
// Used for the Declarative Net Request API.
constexpr const char kDNREnabledStaticRulesetIDs[] = "dnr_enabled_ruleset_ids";

// A boolean preference that indicates whether the extension's icon should be
// automatically badged to the matched action count for a tab. False by default.
constexpr const char kPrefDNRUseActionCountAsBadgeText[] =
    "dnr_use_action_count_as_badge_text";

// A boolean that indicates if a ruleset should be ignored.
constexpr const char kDNRIgnoreRulesetKey[] = "ignore_ruleset";

// A preference that indicates the amount of rules allocated to an extension
// from the global pool.
constexpr const char kDNRExtensionRulesAllocated[] =
    "dnr_extension_rules_allocated";

// A boolean that indicates if an extension should have its unused rule
// allocation kept during its next load.
constexpr const char kPrefDNRKeepExcessAllocation[] =
    "dnr_keep_excess_allocation";

// The default value to use for permission withholding when setting the pref on
// installation or for extensions where the pref has not been set.
constexpr bool kDefaultWithholdingBehavior = false;

// Checks whether the value passed in is consistent with the expected PrefType.
bool CheckPrefType(PrefType pref_type, const base::Value* value) {
  switch (pref_type) {
    case kBool:
      return value->is_bool();
    case kGURL:
    case kTime:
    case kString:
      return value->is_string();
    case kInteger:
      return value->is_int();
    case kDictionary:
      return value->is_dict();
    case kList:
      return value->is_list();
  }
}

// Provider of write access to a dictionary storing extension prefs.
class ScopedExtensionPrefUpdate : public prefs::ScopedDictionaryPrefUpdate {
 public:
  ScopedExtensionPrefUpdate(PrefService* service,
                            const std::string& extension_id)
      : ScopedDictionaryPrefUpdate(service, pref_names::kExtensions),
        extension_id_(extension_id) {
    DCHECK(crx_file::id_util::IdIsValid(extension_id_));
  }

  ScopedExtensionPrefUpdate(const ScopedExtensionPrefUpdate&) = delete;
  ScopedExtensionPrefUpdate& operator=(const ScopedExtensionPrefUpdate&) =
      delete;

  ~ScopedExtensionPrefUpdate() override {}

  // ScopedDictionaryPrefUpdate overrides:
  std::unique_ptr<prefs::DictionaryValueUpdate> Get() override {
    std::unique_ptr<prefs::DictionaryValueUpdate> dict =
        ScopedDictionaryPrefUpdate::Get();
    std::unique_ptr<prefs::DictionaryValueUpdate> extension;
    if (!dict->GetDictionary(extension_id_, &extension)) {
      // Extension pref does not exist, create it.
      extension = dict->SetDictionary(
          extension_id_, std::make_unique<base::DictionaryValue>());
    }
    return extension;
  }

 private:
  const std::string extension_id_;
};

std::string JoinPrefs(const std::vector<base::StringPiece>& parts) {
  return base::JoinString(parts, ".");
}

// Whether SetAlertSystemFirstRun() should always return true, so that alerts
// are triggered, even in first run.
bool g_run_alerts_in_first_run_for_testing = false;

}  // namespace

//
// ScopedDictionaryUpdate
//
ExtensionPrefs::ScopedDictionaryUpdate::ScopedDictionaryUpdate(
    ExtensionPrefs* prefs,
    const std::string& extension_id,
    const std::string& key)
    : update_(std::make_unique<ScopedExtensionPrefUpdate>(prefs->pref_service(),
                                                          extension_id)),
      key_(key) {}

ExtensionPrefs::ScopedDictionaryUpdate::~ScopedDictionaryUpdate() = default;

std::unique_ptr<prefs::DictionaryValueUpdate>
ExtensionPrefs::ScopedDictionaryUpdate::Get() {
  auto dict = update_->Get();
  std::unique_ptr<prefs::DictionaryValueUpdate> key_value;
  dict->GetDictionary(key_, &key_value);
  return key_value;
}

std::unique_ptr<prefs::DictionaryValueUpdate>
ExtensionPrefs::ScopedDictionaryUpdate::Create() {
  auto dict = update_->Get();
  std::unique_ptr<prefs::DictionaryValueUpdate> key_value;
  if (dict->GetDictionary(key_, &key_value))
    return key_value;

  return dict->SetDictionary(key_, std::make_unique<base::DictionaryValue>());
}

ExtensionPrefs::ScopedListUpdate::ScopedListUpdate(
    ExtensionPrefs* prefs,
    const std::string& extension_id,
    const std::string& key)
    : update_(std::make_unique<ScopedExtensionPrefUpdate>(prefs->pref_service(),
                                                          extension_id)),
      key_(key) {}

ExtensionPrefs::ScopedListUpdate::~ScopedListUpdate() = default;

base::Value::List* ExtensionPrefs::ScopedListUpdate::Get() {
  base::Value::List* key_value = nullptr;
  (*update_)->GetListWithoutPathExpansion(key_, &key_value);
  return key_value;
}

base::Value::List* ExtensionPrefs::ScopedListUpdate::Ensure() {
  if (base::Value::List* existing = Get())
    return existing;
  return &(*update_)->SetKey(key_, base::Value(base::Value::List()))->GetList();
}

//
// ExtensionPrefs
//

// static
ExtensionPrefs* ExtensionPrefs::Create(
    content::BrowserContext* browser_context,
    PrefService* prefs,
    const base::FilePath& root_dir,
    ExtensionPrefValueMap* extension_pref_value_map,
    bool extensions_disabled,
    const std::vector<EarlyExtensionPrefsObserver*>& early_observers) {
  return ExtensionPrefs::Create(
      browser_context, prefs, root_dir, extension_pref_value_map,
      extensions_disabled, early_observers, base::DefaultClock::GetInstance());
}

// static
ExtensionPrefs* ExtensionPrefs::Create(
    content::BrowserContext* browser_context,
    PrefService* pref_service,
    const base::FilePath& root_dir,
    ExtensionPrefValueMap* extension_pref_value_map,
    bool extensions_disabled,
    const std::vector<EarlyExtensionPrefsObserver*>& early_observers,
    base::Clock* clock) {
  return new ExtensionPrefs(browser_context, pref_service, root_dir,
                            extension_pref_value_map, clock,
                            extensions_disabled, early_observers);
}

ExtensionPrefs::~ExtensionPrefs() {
  for (auto& observer : observer_list_)
    observer.OnExtensionPrefsWillBeDestroyed(this);
  DCHECK(observer_list_.begin() == observer_list_.end());
}

// static
ExtensionPrefs* ExtensionPrefs::Get(content::BrowserContext* context) {
  return ExtensionPrefsFactory::GetInstance()->GetForBrowserContext(context);
}

static std::string MakePathRelative(const base::FilePath& parent,
                                    const base::FilePath& child) {
  if (!parent.IsParent(child))
    return child.AsUTF8Unsafe();

  base::FilePath::StringType retval = child.value().substr(
      parent.value().length());
  if (base::FilePath::IsSeparator(retval[0]))
    retval = retval.substr(1);
#if BUILDFLAG(IS_WIN)
  return base::WideToUTF8(retval);
#else
  return retval;
#endif
}

void ExtensionPrefs::MakePathsRelative() {
  const base::Value::Dict& dict = prefs_->GetDict(pref_names::kExtensions);
  if (dict.empty())
    return;

  // Collect all extensions ids with absolute paths in |absolute_keys|.
  std::set<std::string> absolute_keys;
  for (const auto [extension_id, extension_item] : dict) {
    if (!extension_item.is_dict())
      continue;
    const base::Value::Dict& extension_dict = extension_item.GetDict();
    absl::optional<int> location_value = extension_dict.FindInt(kPrefLocation);
    if (location_value && Manifest::IsUnpackedLocation(
                              static_cast<ManifestLocation>(*location_value))) {
      // Unpacked extensions can have absolute paths.
      continue;
    }
    const std::string* path_string = extension_dict.FindString(kPrefPath);
    if (!path_string)
      continue;
    base::FilePath path = base::FilePath::FromUTF8Unsafe(*path_string);
    if (path.IsAbsolute())
      absolute_keys.insert(extension_id);
  }
  if (absolute_keys.empty())
    return;

  // Fix these paths.
  prefs::ScopedDictionaryPrefUpdate update(prefs_, pref_names::kExtensions);
  auto update_dict = update.Get();
  for (auto i = absolute_keys.begin(); i != absolute_keys.end(); ++i) {
    std::unique_ptr<prefs::DictionaryValueUpdate> extension_dict;
    if (!update_dict->GetDictionaryWithoutPathExpansion(*i, &extension_dict)) {
      NOTREACHED() << "Control should never reach here for extension " << *i;
      continue;
    }
    std::string path_string;
    extension_dict->GetString(kPrefPath, &path_string);
    base::FilePath path = base::FilePath::FromUTF8Unsafe(path_string);
    extension_dict->SetString(kPrefPath,
        MakePathRelative(install_directory_, path));
  }
}

const base::DictionaryValue* ExtensionPrefs::GetExtensionPref(
    const std::string& extension_id) const {
  // TODO(https://1297144): Should callers of this method proactively filter out
  // extension IDs? Previously, this function would (potentially surprisingly)
  // return `extensions` below if supplied with an empty `extension_id` due to
  // the legacy behavior of `base::Value::FindDictPath()`.
  if (extension_id.empty()) {
    return nullptr;
  }
  // TODO (https://crbug.com/1342019) This should call
  // `PrefService::GetDict`, which will in turn require the return type to
  // be `base::Value::Dict`.
  const base::Value& extensions = prefs_->GetValue(pref_names::kExtensions);
  if (!extensions.is_dict())
    return nullptr;
  const base::Value* extension_dict = extensions.FindDictPath(extension_id);
  return extension_dict ? &base::Value::AsDictionaryValue(*extension_dict)
                        : nullptr;
}

void ExtensionPrefs::SetIntegerPref(const std::string& id,
                                    const PrefMap& pref,
                                    int value) {
  DCHECK_EQ(pref.type, PrefType::kInteger);
  UpdateExtensionPref(id, pref, std::make_unique<base::Value>(value));
}

void ExtensionPrefs::SetBooleanPref(const std::string& id,
                                    const PrefMap& pref,
                                    bool value) {
  DCHECK_EQ(pref.type, PrefType::kBool);
  UpdateExtensionPref(id, pref, std::make_unique<base::Value>(value));
}

void ExtensionPrefs::SetStringPref(const std::string& id,
                                   const PrefMap& pref,
                                   const std::string value) {
  DCHECK_EQ(pref.type, PrefType::kString);
  UpdateExtensionPref(id, pref,
                      std::make_unique<base::Value>(std::move(value)));
}

void ExtensionPrefs::SetListPref(const std::string& id,
                                 const PrefMap& pref,
                                 base::Value value) {
  DCHECK_EQ(pref.type, PrefType::kList);
  DCHECK_EQ(base::Value::Type::LIST, value.type());
  UpdateExtensionPref(id, pref,
                      std::make_unique<base::Value>(std::move(value)));
}

void ExtensionPrefs::SetDictionaryPref(
    const std::string& id,
    const PrefMap& pref,
    std::unique_ptr<base::DictionaryValue> value) {
  DCHECK_EQ(pref.type, PrefType::kDictionary);
  DCHECK_EQ(base::Value::Type::DICTIONARY, value->type());
  UpdateExtensionPref(id, pref, std::move(value));
}

void ExtensionPrefs::SetTimePref(const std::string& id,
                                 const PrefMap& pref,
                                 const base::Time value) {
  DCHECK_EQ(pref.type, PrefType::kTime);
  UpdateExtensionPref(
      id, pref, std::make_unique<base::Value>(::base::TimeToValue(value)));
}

void ExtensionPrefs::UpdateExtensionPref(
    const std::string& extension_id,
    const PrefMap& pref,
    std::unique_ptr<base::Value> data_value) {
  DCHECK_EQ(PrefScope::kExtensionSpecific, pref.scope);
  DCHECK(CheckPrefType(pref.type, data_value.get()));
  DCHECK(crx_file::id_util::IdIsValid(extension_id));
  ScopedExtensionPrefUpdate update(prefs_, extension_id);
  update->Set(pref.name, std::move(data_value));
}

void ExtensionPrefs::UpdateExtensionPref(
    const std::string& extension_id,
    base::StringPiece key,
    std::unique_ptr<base::Value> data_value) {
  if (!crx_file::id_util::IdIsValid(extension_id)) {
    NOTREACHED() << "Invalid extension_id " << extension_id;
    return;
  }
  ScopedExtensionPrefUpdate update(prefs_, extension_id);
  if (data_value)
    update->Set(key, std::move(data_value));
  else
    update->Remove(key);
}

void ExtensionPrefs::DeleteExtensionPrefs(const std::string& extension_id) {
  extension_pref_value_map_->UnregisterExtension(extension_id);
  for (auto& observer : observer_list_)
    observer.OnExtensionPrefsDeleted(extension_id);
  prefs::ScopedDictionaryPrefUpdate update(prefs_, pref_names::kExtensions);
  update->Remove(extension_id);
}

void ExtensionPrefs::DeleteExtensionPrefsIfPrefEmpty(
    const std::string& extension_id) {
  const base::DictionaryValue* dict = GetExtensionPref(extension_id);
  if (dict && dict->DictEmpty())
    DeleteExtensionPrefs(extension_id);
}

bool ExtensionPrefs::ReadPrefAsBoolean(const std::string& extension_id,
                                       const PrefMap& pref,
                                       bool* out_value) const {
  DCHECK_EQ(PrefScope::kExtensionSpecific, pref.scope);
  DCHECK_EQ(PrefType::kBool, pref.type);

  const base::DictionaryValue* ext = GetExtensionPref(extension_id);
  if (!ext)
    return false;

  absl::optional<bool> value = ext->FindBoolPath(pref.name);
  if (!value)
    return false;

  *out_value = *value;
  return true;
}

bool ExtensionPrefs::ReadPrefAsInteger(const std::string& extension_id,
                                       const PrefMap& pref,
                                       int* out_value) const {
  DCHECK_EQ(PrefScope::kExtensionSpecific, pref.scope);
  DCHECK_EQ(PrefType::kInteger, pref.type);
  const base::DictionaryValue* ext = GetExtensionPref(extension_id);
  if (!ext)
    return false;
  absl::optional<int> value = ext->FindIntPath(pref.name);
  if (!value)
    return false;
  *out_value = *value;
  return true;
}

bool ExtensionPrefs::ReadPrefAsString(const std::string& extension_id,
                                      const PrefMap& pref,
                                      std::string* out_value) const {
  DCHECK_EQ(PrefScope::kExtensionSpecific, pref.scope);
  DCHECK_EQ(PrefType::kString, pref.type);
  const base::DictionaryValue* ext = GetExtensionPref(extension_id);
  if (!ext || !ext->GetString(pref.name, out_value))
    return false;

  return true;
}

bool ExtensionPrefs::ReadPrefAsList(const std::string& extension_id,
                                    const PrefMap& pref,
                                    const base::ListValue** out_value) const {
  DCHECK_EQ(PrefScope::kExtensionSpecific, pref.scope);
  DCHECK_EQ(PrefType::kList, pref.type);
  DCHECK(out_value);
  const base::DictionaryValue* ext = GetExtensionPref(extension_id);
  if (!ext || !ext->GetList(pref.name, out_value))
    return false;
  return true;
}

bool ExtensionPrefs::ReadPrefAsDictionary(
    const std::string& extension_id,
    const PrefMap& pref,
    const base::DictionaryValue** out_value) const {
  DCHECK_EQ(PrefScope::kExtensionSpecific, pref.scope);
  DCHECK_EQ(PrefType::kDictionary, pref.type);
  DCHECK(out_value);
  const base::DictionaryValue* ext = GetExtensionPref(extension_id);
  if (!ext)
    return false;
  const base::Value* dict = ext->FindDictPath(pref.name);
  if (!dict)
    return false;
  *out_value = &base::Value::AsDictionaryValue(*dict);
  return true;
}

base::Time ExtensionPrefs::ReadPrefAsTime(const std::string& extension_id,
                                          const PrefMap& pref) const {
  DCHECK_EQ(PrefScope::kExtensionSpecific, pref.scope);
  DCHECK_EQ(PrefType::kTime, pref.type);
  const base::DictionaryValue* ext = GetExtensionPref(extension_id);
  const base::Value* value;
  if (!ext || !ext->Get(pref.name, &value))
    return base::Time();
  absl::optional<base::Time> time = ::base::ValueToTime(value);
  DCHECK(time);
  return time.value_or(base::Time());
}

bool ExtensionPrefs::ReadPrefAsBoolean(const std::string& extension_id,
                                       base::StringPiece pref_key,
                                       bool* out_value) const {
  const base::DictionaryValue* ext = GetExtensionPref(extension_id);
  if (!ext)
    return false;

  absl::optional<bool> value = ext->FindBoolPath(pref_key);
  if (!value)
    return false;

  *out_value = *value;
  return true;
}

bool ExtensionPrefs::ReadPrefAsInteger(const std::string& extension_id,
                                       base::StringPiece pref_key,
                                       int* out_value) const {
  const base::DictionaryValue* ext = GetExtensionPref(extension_id);
  if (!ext)
    return false;

  absl::optional<int> value = ext->FindIntPath(pref_key);
  if (!value)
    return false;

  *out_value = *value;
  return true;
}

bool ExtensionPrefs::ReadPrefAsString(const std::string& extension_id,
                                      base::StringPiece pref_key,
                                      std::string* out_value) const {
  const base::DictionaryValue* ext = GetExtensionPref(extension_id);
  if (!ext || !ext->GetString(pref_key, out_value))
    return false;

  return true;
}

bool ExtensionPrefs::ReadPrefAsList(const std::string& extension_id,
                                    base::StringPiece pref_key,
                                    const base::ListValue** out_value) const {
  const base::DictionaryValue* ext = GetExtensionPref(extension_id);
  const base::ListValue* out = nullptr;
  if (!ext || !ext->GetList(pref_key, &out))
    return false;
  if (out_value)
    *out_value = out;

  return true;
}

const base::Value* ExtensionPrefs::GetPrefAsValue(
    const std::string& extension_id,
    base::StringPiece pref_key) const {
  const base::DictionaryValue* ext = GetExtensionPref(extension_id);
  return ext ? ext->FindDictPath(pref_key) : nullptr;
}

bool ExtensionPrefs::ReadPrefAsDictionary(
    const std::string& extension_id,
    base::StringPiece pref_key,
    const base::DictionaryValue** out_value) const {
  const base::Value* out = GetPrefAsValue(extension_id, pref_key);
  if (!out)
    return false;
  if (out_value)
    *out_value = &base::Value::AsDictionaryValue(*out);

  return true;
}

const base::Value::Dict* ExtensionPrefs::ReadPrefAsDict(
    const std::string& extension_id,
    base::StringPiece pref_key) const {
  const base::Value* out = GetPrefAsValue(extension_id, pref_key);
  return out ? &out->GetDict() : nullptr;
}

bool ExtensionPrefs::HasPrefForExtension(
    const std::string& extension_id) const {
  return GetExtensionPref(extension_id) != nullptr;
}

bool ExtensionPrefs::ReadPrefAsURLPatternSet(const std::string& extension_id,
                                             base::StringPiece pref_key,
                                             URLPatternSet* result,
                                             int valid_schemes) const {
  const base::ListValue* value = nullptr;
  if (!ReadPrefAsList(extension_id, pref_key, &value))
    return false;
  const base::DictionaryValue* extension = GetExtensionPref(extension_id);
  if (!extension)
    return false;
  absl::optional<int> location = extension->FindIntKey(kPrefLocation);
  if (location && static_cast<ManifestLocation>(*location) ==
                      ManifestLocation::kComponent) {
    valid_schemes |= URLPattern::SCHEME_CHROMEUI;
  }

  bool allow_file_access = AllowFileAccess(extension_id);
  return result->Populate(value->GetList(), valid_schemes, allow_file_access,
                          nullptr);
}

void ExtensionPrefs::SetExtensionPrefURLPatternSet(
    const std::string& extension_id,
    base::StringPiece pref_key,
    const URLPatternSet& set) {
  // Clear the |pref_key| in case |set| is empty.
  std::unique_ptr<base::Value> value =
      std::make_unique<base::Value>(set.ToValue());
  UpdateExtensionPref(extension_id, pref_key, std::move(value));
}

bool ExtensionPrefs::ReadPrefAsBooleanAndReturn(
    const std::string& extension_id,
    base::StringPiece pref_key) const {
  bool out_value = false;
  return ReadPrefAsBoolean(extension_id, pref_key, &out_value) && out_value;
}

std::unique_ptr<PermissionSet> ExtensionPrefs::ReadPrefAsPermissionSet(
    const std::string& extension_id,
    base::StringPiece pref_key) const {
  if (!GetExtensionPref(extension_id))
    return nullptr;

  // Retrieve the API permissions. Please refer SetExtensionPrefPermissionSet()
  // for api_values format.
  APIPermissionSet apis;
  const base::ListValue* api_values = nullptr;
  std::string api_pref = JoinPrefs({pref_key, kPrefAPIs});
  if (ReadPrefAsList(extension_id, api_pref, &api_values)) {
    APIPermissionSet::ParseFromJSON(api_values,
                                    APIPermissionSet::kAllowInternalPermissions,
                                    &apis, nullptr, nullptr);
  }

  // Retrieve the Manifest Keys permissions. Please refer to
  // |SetExtensionPrefPermissionSet| for manifest_permissions_values format.
  ManifestPermissionSet manifest_permissions;
  const base::ListValue* manifest_permissions_values = nullptr;
  std::string manifest_permission_pref =
      JoinPrefs({pref_key, kPrefManifestPermissions});
  if (ReadPrefAsList(extension_id, manifest_permission_pref,
                     &manifest_permissions_values)) {
    ManifestPermissionSet::ParseFromJSON(
        manifest_permissions_values, &manifest_permissions, nullptr, nullptr);
  }

  // Retrieve the explicit host permissions.
  URLPatternSet explicit_hosts;
  ReadPrefAsURLPatternSet(
      extension_id, JoinPrefs({pref_key, kPrefExplicitHosts}), &explicit_hosts,
      Extension::kValidHostPermissionSchemes);

  // Retrieve the scriptable host permissions.
  URLPatternSet scriptable_hosts;
  ReadPrefAsURLPatternSet(
      extension_id, JoinPrefs({pref_key, kPrefScriptableHosts}),
      &scriptable_hosts, UserScript::ValidUserScriptSchemes());

  return std::make_unique<PermissionSet>(
      std::move(apis), std::move(manifest_permissions),
      std::move(explicit_hosts), std::move(scriptable_hosts));
}

// Set the API or Manifest permissions.
// The format of api_values is:
// [ "permission_name1",   // permissions do not support detail.
//   "permission_name2",
//   {"permission_name3": value },
//   // permission supports detail, permission detail will be stored in value.
//   ...
// ]
template <typename T>
static std::unique_ptr<base::ListValue> CreatePermissionList(
    const T& permissions) {
  auto values = std::make_unique<base::ListValue>();
  for (typename T::const_iterator i = permissions.begin();
      i != permissions.end(); ++i) {
    std::unique_ptr<base::Value> detail(i->ToValue());
    if (detail) {
      base::Value::Dict tmp;
      tmp.Set(i->name(), base::Value::FromUniquePtrValue(std::move(detail)));
      values->Append(base::Value(std::move(tmp)));
    } else {
      values->Append(i->name());
    }
  }
  return values;
}

void ExtensionPrefs::SetExtensionPrefPermissionSet(
    const std::string& extension_id,
    base::StringPiece pref_key,
    const PermissionSet& new_value) {
  std::string api_pref = JoinPrefs({pref_key, kPrefAPIs});
  UpdateExtensionPref(extension_id, api_pref,
                      CreatePermissionList(new_value.apis()));

  std::string manifest_permissions_pref =
      JoinPrefs({pref_key, kPrefManifestPermissions});
  UpdateExtensionPref(extension_id, manifest_permissions_pref,
                      CreatePermissionList(new_value.manifest_permissions()));

  // Set the explicit host permissions.
  SetExtensionPrefURLPatternSet(extension_id,
                                JoinPrefs({pref_key, kPrefExplicitHosts}),
                                new_value.explicit_hosts());

  // Set the scriptable host permissions.
  SetExtensionPrefURLPatternSet(extension_id,
                                JoinPrefs({pref_key, kPrefScriptableHosts}),
                                new_value.scriptable_hosts());
}

void ExtensionPrefs::AddToPrefPermissionSet(const ExtensionId& extension_id,
                                            const PermissionSet& permissions,
                                            const char* pref_name) {
  CHECK(crx_file::id_util::IdIsValid(extension_id));
  std::unique_ptr<PermissionSet> current =
      ReadPrefAsPermissionSet(extension_id, pref_name);
  std::unique_ptr<PermissionSet> union_set;
  if (current)
    union_set = PermissionSet::CreateUnion(permissions, *current);
  // The new permissions are the union of the already stored permissions and the
  // newly added permissions.
  SetExtensionPrefPermissionSet(extension_id, pref_name,
                                union_set ? *union_set : permissions);
}

void ExtensionPrefs::RemoveFromPrefPermissionSet(
    const ExtensionId& extension_id,
    const PermissionSet& permissions,
    const char* pref_name) {
  CHECK(crx_file::id_util::IdIsValid(extension_id));

  std::unique_ptr<PermissionSet> current =
      ReadPrefAsPermissionSet(extension_id, pref_name);

  if (!current)
    return;  // Nothing to remove.

  // The new permissions are the difference of the already stored permissions
  // and the newly removed permissions.
  SetExtensionPrefPermissionSet(
      extension_id, pref_name,
      *PermissionSet::CreateDifference(*current, permissions));
}

int ExtensionPrefs::IncrementAcknowledgePromptCount(
    const std::string& extension_id) {
  int count = 0;
  ReadPrefAsInteger(extension_id, kPrefAcknowledgePromptCount, &count);
  ++count;
  UpdateExtensionPref(extension_id, kPrefAcknowledgePromptCount,
                      std::make_unique<base::Value>(count));
  return count;
}

bool ExtensionPrefs::IsExternalExtensionAcknowledged(
    const std::string& extension_id) const {
  return ReadPrefAsBooleanAndReturn(extension_id, kPrefExternalAcknowledged);
}

void ExtensionPrefs::AcknowledgeExternalExtension(
    const std::string& extension_id) {
  DCHECK(crx_file::id_util::IdIsValid(extension_id));
  UpdateExtensionPref(extension_id, kPrefExternalAcknowledged,
                      std::make_unique<base::Value>(true));
  UpdateExtensionPref(extension_id, kPrefAcknowledgePromptCount, nullptr);
}

bool ExtensionPrefs::IsBlocklistedExtensionAcknowledged(
    const std::string& extension_id) const {
  return blocklist_prefs::HasAcknowledgedBlocklistState(
      extension_id, BitMapBlocklistState::BLOCKLISTED_MALWARE, this);
}

void ExtensionPrefs::AcknowledgeBlocklistedExtension(
    const std::string& extension_id) {
  DCHECK(crx_file::id_util::IdIsValid(extension_id));
  blocklist_prefs::AddAcknowledgedBlocklistState(
      extension_id, BitMapBlocklistState::BLOCKLISTED_MALWARE, this);
  UpdateExtensionPref(extension_id, kPrefAcknowledgePromptCount, nullptr);
}

bool ExtensionPrefs::IsExternalInstallFirstRun(
    const std::string& extension_id) const {
  return ReadPrefAsBooleanAndReturn(extension_id, kPrefExternalInstallFirstRun);
}

void ExtensionPrefs::SetExternalInstallFirstRun(
    const std::string& extension_id) {
  DCHECK(crx_file::id_util::IdIsValid(extension_id));
  UpdateExtensionPref(extension_id, kPrefExternalInstallFirstRun,
                      std::make_unique<base::Value>(true));
}

bool ExtensionPrefs::SetAlertSystemFirstRun() {
  if (prefs_->GetBoolean(pref_names::kAlertsInitialized)) {
    return true;
  }
  prefs_->SetBoolean(pref_names::kAlertsInitialized, true);
  return g_run_alerts_in_first_run_for_testing;  // Note: normally false.
}

bool ExtensionPrefs::DidExtensionEscalatePermissions(
    const std::string& extension_id) const {
  return HasDisableReason(extension_id,
                          disable_reason::DISABLE_PERMISSIONS_INCREASE) ||
         HasDisableReason(extension_id, disable_reason::DISABLE_REMOTE_INSTALL);
}

int ExtensionPrefs::GetDisableReasons(const std::string& extension_id) const {
  return GetBitMapPrefBits(extension_id, kPrefDisableReasons,
                           disable_reason::DISABLE_NONE);
}

int ExtensionPrefs::GetBitMapPrefBits(const std::string& extension_id,
                                      base::StringPiece pref_key,
                                      int default_bit) const {
  int value = -1;
  if (ReadPrefAsInteger(extension_id, pref_key, &value) && value >= 0) {
    return value;
  }
  return default_bit;
}

bool ExtensionPrefs::HasDisableReason(
    const std::string& extension_id,
    disable_reason::DisableReason disable_reason) const {
  return (GetDisableReasons(extension_id) & disable_reason) != 0;
}

void ExtensionPrefs::AddDisableReason(
    const std::string& extension_id,
    disable_reason::DisableReason disable_reason) {
  AddDisableReasons(extension_id, disable_reason);
}

void ExtensionPrefs::AddDisableReasons(const std::string& extension_id,
                                       int disable_reasons) {
  DCHECK(!DoesExtensionHaveState(extension_id, Extension::ENABLED) ||
         blocklist_prefs::IsExtensionBlocklisted(extension_id, this));
  ModifyDisableReasons(extension_id, disable_reasons, BIT_MAP_PREF_ADD);
}

void ExtensionPrefs::RemoveDisableReason(
    const std::string& extension_id,
    disable_reason::DisableReason disable_reason) {
  ModifyDisableReasons(extension_id, disable_reason, BIT_MAP_PREF_REMOVE);
}

void ExtensionPrefs::ReplaceDisableReasons(const std::string& extension_id,
                                           int disable_reasons) {
  ModifyDisableReasons(extension_id, disable_reasons, BIT_MAP_PREF_REPLACE);
}

void ExtensionPrefs::ClearDisableReasons(const std::string& extension_id) {
  ModifyDisableReasons(extension_id, disable_reason::DISABLE_NONE,
                       BIT_MAP_PREF_CLEAR);
}

void ExtensionPrefs::ClearInapplicableDisableReasonsForComponentExtension(
    const std::string& component_extension_id) {
  static constexpr int kAllowDisableReasons =
      disable_reason::DISABLE_RELOAD |
      disable_reason::DISABLE_UNSUPPORTED_REQUIREMENT |
      disable_reason::DISABLE_CORRUPTED | disable_reason::DISABLE_REINSTALL;
  int allowed_disable_reasons = kAllowDisableReasons;

  // Some disable reasons incorrectly cause component extensions to never
  // activate on load. See https://crbug.com/946839 for more details on why we
  // do this.
  ModifyDisableReasons(
      component_extension_id,
      allowed_disable_reasons & GetDisableReasons(component_extension_id),
      BIT_MAP_PREF_REPLACE);
}

void ExtensionPrefs::ModifyDisableReasons(const std::string& extension_id,
                                          int reasons,
                                          BitMapPrefOperation operation) {
  int old_value = GetBitMapPrefBits(extension_id, kPrefDisableReasons,
                                    disable_reason::DISABLE_NONE);
  ModifyBitMapPrefBits(extension_id, reasons, operation, kPrefDisableReasons,
                       disable_reason::DISABLE_NONE);
  int new_value = GetBitMapPrefBits(extension_id, kPrefDisableReasons,
                                    disable_reason::DISABLE_NONE);

  if (old_value == new_value)  // no change, do not notify observers.
    return;

  for (auto& observer : observer_list_)
    observer.OnExtensionDisableReasonsChanged(extension_id, new_value);
}

void ExtensionPrefs::ModifyBitMapPrefBits(const std::string& extension_id,
                                          int pending_bits,
                                          BitMapPrefOperation operation,
                                          base::StringPiece pref_key,
                                          int default_bit) {
  int old_value = GetBitMapPrefBits(extension_id, pref_key, default_bit);
  int new_value = old_value;
  switch (operation) {
    case BIT_MAP_PREF_ADD:
      new_value |= pending_bits;
      break;
    case BIT_MAP_PREF_REMOVE:
      new_value &= ~pending_bits;
      break;
    case BIT_MAP_PREF_REPLACE:
      new_value = pending_bits;
      break;
    case BIT_MAP_PREF_CLEAR:
      new_value = pending_bits;
      break;
  }

  if (old_value == new_value)  // no change, return.
    return;

  if (new_value == default_bit) {
    UpdateExtensionPref(extension_id, pref_key, nullptr);
  } else {
    UpdateExtensionPref(extension_id, pref_key,
                        std::make_unique<base::Value>(new_value));
  }
}

namespace {

// Serializes a 64bit integer as a string value.
void SaveInt64(prefs::DictionaryValueUpdate* dictionary,
               const char* key,
               const int64_t value) {
  if (!dictionary)
    return;

  std::string string_value = base::NumberToString(value);
  dictionary->SetString(key, string_value);
}

// Deserializes a 64bit integer stored as a string value.
bool ReadInt64(const base::DictionaryValue* dictionary,
               const char* key,
               int64_t* value) {
  if (!dictionary)
    return false;

  std::string string_value;
  if (!dictionary->GetString(key, &string_value))
    return false;

  return base::StringToInt64(string_value, value);
}

// Serializes |time| as a string value mapped to |key| in |dictionary|.
void SaveTime(prefs::DictionaryValueUpdate* dictionary,
              const char* key,
              const base::Time& time) {
  SaveInt64(dictionary, key, time.ToInternalValue());
}

// The opposite of SaveTime. If |key| is not found, this returns an empty Time
// (is_null() will return true).
base::Time ReadTime(const base::DictionaryValue* dictionary, const char* key) {
  int64_t value;
  if (ReadInt64(dictionary, key, &value))
    return base::Time::FromInternalValue(value);

  return base::Time();
}

}  // namespace

base::Time ExtensionPrefs::LastPingDay(const std::string& extension_id) const {
  DCHECK(crx_file::id_util::IdIsValid(extension_id));
  return ReadTime(GetExtensionPref(extension_id), kLastPingDay);
}

void ExtensionPrefs::SetLastPingDay(const std::string& extension_id,
                                    const base::Time& time) {
  DCHECK(crx_file::id_util::IdIsValid(extension_id));
  ScopedExtensionPrefUpdate update(prefs_, extension_id);
  SaveTime(update.Get().get(), kLastPingDay, time);
}

base::Time ExtensionPrefs::BlocklistLastPingDay() const {
  // TODO (https://crbug.com/1342019) This should call
  // `PrefService::GetDict`, which will in turn require the return type to
  // be `base::Value::Dict`.
  return ReadTime(&base::Value::AsDictionaryValue(
                      prefs_->GetValue(kExtensionsBlocklistUpdate)),
                  kLastPingDay);
}

void ExtensionPrefs::SetBlocklistLastPingDay(const base::Time& time) {
  prefs::ScopedDictionaryPrefUpdate update(prefs_, kExtensionsBlocklistUpdate);
  SaveTime(update.Get().get(), kLastPingDay, time);
}

base::Time ExtensionPrefs::LastActivePingDay(
    const std::string& extension_id) const {
  DCHECK(crx_file::id_util::IdIsValid(extension_id));
  return ReadTime(GetExtensionPref(extension_id), kLastActivePingDay);
}

void ExtensionPrefs::SetLastActivePingDay(const std::string& extension_id,
                                          const base::Time& time) {
  DCHECK(crx_file::id_util::IdIsValid(extension_id));
  ScopedExtensionPrefUpdate update(prefs_, extension_id);
  SaveTime(update.Get().get(), kLastActivePingDay, time);
}

bool ExtensionPrefs::GetActiveBit(const std::string& extension_id) const {
  const base::DictionaryValue* dictionary = GetExtensionPref(extension_id);
  if (dictionary)
    return dictionary->FindBoolKey(kActiveBit).value_or(false);
  return false;
}

void ExtensionPrefs::SetActiveBit(const std::string& extension_id,
                                  bool active) {
  UpdateExtensionPref(extension_id, kActiveBit,
                      std::make_unique<base::Value>(active));
}

std::unique_ptr<PermissionSet> ExtensionPrefs::GetGrantedPermissions(
    const std::string& extension_id) const {
  CHECK(crx_file::id_util::IdIsValid(extension_id));
  return ReadPrefAsPermissionSet(extension_id, kPrefGrantedPermissions);
}

void ExtensionPrefs::AddGrantedPermissions(const std::string& extension_id,
                                           const PermissionSet& permissions) {
  AddToPrefPermissionSet(extension_id, permissions, kPrefGrantedPermissions);
}

void ExtensionPrefs::RemoveGrantedPermissions(
    const std::string& extension_id,
    const PermissionSet& permissions) {
  RemoveFromPrefPermissionSet(extension_id, permissions,
                              kPrefGrantedPermissions);
}

std::unique_ptr<PermissionSet> ExtensionPrefs::GetDesiredActivePermissions(
    const std::string& extension_id) const {
  CHECK(crx_file::id_util::IdIsValid(extension_id));
  return ReadPrefAsPermissionSet(extension_id, kPrefDesiredActivePermissions);
}

void ExtensionPrefs::SetDesiredActivePermissions(
    const std::string& extension_id,
    const PermissionSet& permissions) {
  SetExtensionPrefPermissionSet(extension_id, kPrefDesiredActivePermissions,
                                permissions);
}

void ExtensionPrefs::AddDesiredActivePermissions(
    const ExtensionId& extension_id,
    const PermissionSet& permissions) {
  AddToPrefPermissionSet(extension_id, permissions,
                         kPrefDesiredActivePermissions);
}

void ExtensionPrefs::RemoveDesiredActivePermissions(
    const ExtensionId& extension_id,
    const PermissionSet& permissions) {
  RemoveFromPrefPermissionSet(extension_id, permissions,
                              kPrefDesiredActivePermissions);
}

void ExtensionPrefs::SetWithholdingPermissions(const ExtensionId& extension_id,
                                               bool should_withhold) {
  UpdateExtensionPref(extension_id, kPrefWithholdingPermissions,
                      std::make_unique<base::Value>(should_withhold));
}

bool ExtensionPrefs::GetWithholdingPermissions(
    const ExtensionId& extension_id) const {
  bool permissions_allowed = false;
  if (ReadPrefAsBoolean(extension_id, kPrefWithholdingPermissions,
                        &permissions_allowed)) {
    return permissions_allowed;
  }

  // If no pref was found, we use the default.
  return kDefaultWithholdingBehavior;
}

bool ExtensionPrefs::HasWithholdingPermissionsSetting(
    const ExtensionId& extension_id) const {
  const base::DictionaryValue* ext = GetExtensionPref(extension_id);
  return ext && ext->FindKey(kPrefWithholdingPermissions);
}

std::unique_ptr<PermissionSet> ExtensionPrefs::GetRuntimeGrantedPermissions(
    const ExtensionId& extension_id) const {
  CHECK(crx_file::id_util::IdIsValid(extension_id));
  return ReadPrefAsPermissionSet(extension_id, kPrefRuntimeGrantedPermissions);
}

void ExtensionPrefs::AddRuntimeGrantedPermissions(
    const ExtensionId& extension_id,
    const PermissionSet& permissions) {
  AddToPrefPermissionSet(extension_id, permissions,
                         kPrefRuntimeGrantedPermissions);
  for (auto& observer : observer_list_)
    observer.OnExtensionRuntimePermissionsChanged(extension_id);
}

void ExtensionPrefs::RemoveRuntimeGrantedPermissions(
    const ExtensionId& extension_id,
    const PermissionSet& permissions) {
  RemoveFromPrefPermissionSet(extension_id, permissions,
                              kPrefRuntimeGrantedPermissions);
  for (auto& observer : observer_list_)
    observer.OnExtensionRuntimePermissionsChanged(extension_id);
}

void ExtensionPrefs::SetExtensionRunning(const std::string& extension_id,
    bool is_running) {
  UpdateExtensionPref(extension_id, kPrefRunning,
                      std::make_unique<base::Value>(is_running));
}

bool ExtensionPrefs::IsExtensionRunning(const std::string& extension_id) const {
  const base::DictionaryValue* extension = GetExtensionPref(extension_id);
  if (extension)
    return extension->FindBoolKey(kPrefRunning).value_or(false);
  return false;
}

void ExtensionPrefs::SetIsActive(const std::string& extension_id,
                                 bool is_active) {
  UpdateExtensionPref(extension_id, kIsActive,
                      std::make_unique<base::Value>(is_active));
}

bool ExtensionPrefs::IsActive(const std::string& extension_id) const {
  const base::DictionaryValue* extension = GetExtensionPref(extension_id);
  if (extension)
    return extension->FindBoolKey(kIsActive).value_or(false);
  return false;
}

bool ExtensionPrefs::IsIncognitoEnabled(const std::string& extension_id) const {
  return ReadPrefAsBooleanAndReturn(extension_id, kPrefIncognitoEnabled);
}

void ExtensionPrefs::SetIsIncognitoEnabled(const std::string& extension_id,
                                           bool enabled) {
  UpdateExtensionPref(extension_id, kPrefIncognitoEnabled,
                      std::make_unique<base::Value>(enabled));
  extension_pref_value_map_->SetExtensionIncognitoState(extension_id, enabled);
}

bool ExtensionPrefs::AllowFileAccess(const std::string& extension_id) const {
  return ReadPrefAsBooleanAndReturn(extension_id, kPrefAllowFileAccess);
}

void ExtensionPrefs::SetAllowFileAccess(const std::string& extension_id,
                                        bool allow) {
  UpdateExtensionPref(extension_id, kPrefAllowFileAccess,
                      std::make_unique<base::Value>(allow));
}

bool ExtensionPrefs::HasAllowFileAccessSetting(
    const std::string& extension_id) const {
  const base::DictionaryValue* ext = GetExtensionPref(extension_id);
  return ext && ext->FindKey(kPrefAllowFileAccess);
}

bool ExtensionPrefs::DoesExtensionHaveState(
    const std::string& id, Extension::State check_state) const {
  const base::DictionaryValue* extension = GetExtensionPref(id);
  if (!extension)
    return false;

  absl::optional<int> state = extension->FindIntKey(kPrefState);
  if (!state)
    return false;

  if (*state < 0 || *state >= Extension::NUM_STATES) {
    LOG(ERROR) << "Bad pref 'state' for extension '" << id << "'";
    return false;
  }

  return *state == check_state;
}

bool ExtensionPrefs::IsExternalExtensionUninstalled(
    const std::string& id) const {
  ExtensionIdList uninstalled_ids;
  GetUserExtensionPrefIntoContainer(kExternalUninstalls, &uninstalled_ids);
  return base::Contains(uninstalled_ids, id);
}

bool ExtensionPrefs::IsExtensionDisabled(const std::string& id) const {
  return DoesExtensionHaveState(id, Extension::DISABLED);
}

ExtensionIdList ExtensionPrefs::GetPinnedExtensions() const {
  ExtensionIdList id_list_out;
  GetUserExtensionPrefIntoContainer(pref_names::kPinnedExtensions,
                                    &id_list_out);
  return id_list_out;
}

void ExtensionPrefs::SetPinnedExtensions(const ExtensionIdList& extension_ids) {
  SetExtensionPrefFromContainer(pref_names::kPinnedExtensions, extension_ids);
}

void ExtensionPrefs::OnExtensionInstalled(
    const Extension* extension,
    Extension::State initial_state,
    const syncer::StringOrdinal& page_ordinal,
    int install_flags,
    const std::string& install_parameter,
    const declarative_net_request::RulesetInstallPrefs& ruleset_install_prefs) {
  // If the extension was previously an external extension that was uninstalled,
  // clear the external uninstall bit.
  // TODO(devlin): We previously did this because we indicated external
  // uninstallation through the extension dictionary itself (on the "state"
  // key), and needed a way to have other installation - such as user or policy
  // installations - override that state. Now that external uninstalls are
  // stored separately, we shouldn't necessarily have to do this - a new install
  // can still override the external uninstall without clearing the bit.
  // However, it's not clear if existing subsystems may also be relying on this
  // bit being set/unset. For now, maintain existing behavior.
  if (IsExternalExtensionUninstalled(extension->id()))
    ClearExternalUninstallBit(extension->id());

  ScopedExtensionPrefUpdate update(prefs_, extension->id());
  auto extension_dict = update.Get();
  const base::Time install_time = clock_->Now();
  PopulateExtensionInfoPrefs(extension, install_time, initial_state,
                             install_flags, install_parameter,
                             ruleset_install_prefs, extension_dict.get());

  FinishExtensionInfoPrefs(extension->id(), install_time,
                           extension->RequiresSortOrdinal(), page_ordinal,
                           extension_dict.get());
}

void ExtensionPrefs::OnExtensionUninstalled(const std::string& extension_id,
                                            const ManifestLocation location,
                                            bool external_uninstall) {
  app_sorting()->ClearOrdinals(extension_id);

  // For external extensions, we save a preference reminding ourself not to try
  // and install the extension anymore (except when |external_uninstall| is
  // true, which signifies that the registry key was deleted or the pref file
  // no longer lists the extension).
  if (!external_uninstall && Manifest::IsExternalLocation(location)) {
    ListPrefUpdate update(prefs_, kExternalUninstalls);
    update->GetList().Append(extension_id);
  }

  DeleteExtensionPrefs(extension_id);
}

void ExtensionPrefs::SetExtensionEnabled(const std::string& extension_id) {
  UpdateExtensionPref(extension_id, kPrefState,
                      std::make_unique<base::Value>(Extension::ENABLED));
  extension_pref_value_map_->SetExtensionState(extension_id, true);
  UpdateExtensionPref(extension_id, kPrefDisableReasons, nullptr);
  for (auto& observer : observer_list_)
    observer.OnExtensionStateChanged(extension_id, true);
}

void ExtensionPrefs::SetExtensionDisabled(const std::string& extension_id,
                                          int disable_reasons) {
  UpdateExtensionPref(extension_id, kPrefState,
                      std::make_unique<base::Value>(Extension::DISABLED));
  extension_pref_value_map_->SetExtensionState(extension_id, false);
  UpdateExtensionPref(extension_id, kPrefDisableReasons,
                      std::make_unique<base::Value>(disable_reasons));
  for (auto& observer : observer_list_)
    observer.OnExtensionStateChanged(extension_id, false);
}

std::string ExtensionPrefs::GetVersionString(
    const std::string& extension_id) const {
  const base::DictionaryValue* extension = GetExtensionPref(extension_id);
  if (!extension)
    return std::string();

  std::string version;
  extension->GetString(kPrefManifestVersion, &version);

  return version;
}

void ExtensionPrefs::UpdateManifest(const Extension* extension) {
  if (!Manifest::IsUnpackedLocation(extension->location())) {
    const base::DictionaryValue* extension_dict =
        GetExtensionPref(extension->id());
    if (!extension_dict)
      return;
    const base::Value* old_manifest =
        extension_dict->FindDictKey(kPrefManifest);
    bool update_required =
        !old_manifest || *extension->manifest()->value() != *old_manifest;
    if (update_required) {
      UpdateExtensionPref(
          extension->id(), kPrefManifest,
          base::DictionaryValue::From(base::Value::ToUniquePtrValue(
              extension->manifest()->value()->Clone())));
    }
  }
}

void ExtensionPrefs::SetInstallLocation(const std::string& extension_id,
                                        ManifestLocation location) {
  UpdateExtensionPref(
      extension_id, kPrefLocation,
      std::make_unique<base::Value>(static_cast<int>(location)));
}

std::unique_ptr<ExtensionInfo> ExtensionPrefs::GetInstalledInfoHelper(
    const std::string& extension_id,
    const base::Value::Dict& extension,
    bool include_component_extensions) const {
  absl::optional<int> location_value = extension.FindInt(kPrefLocation);
  if (!location_value)
    return nullptr;

  ManifestLocation location = static_cast<ManifestLocation>(*location_value);
  if (location == ManifestLocation::kComponent &&
      !include_component_extensions) {
    // Component extensions are ignored by default. Component extensions may
    // have data saved in preferences, but they are already loaded at this point
    // (by ComponentLoader) and shouldn't be populated into the result of
    // GetInstalledExtensionsInfo, otherwise InstalledLoader would also want to
    // load them.
    return nullptr;
  }

  // Only the following extension types have data saved in the preferences.
  if (location != ManifestLocation::kInternal &&
      location != ManifestLocation::kComponent &&
      !Manifest::IsUnpackedLocation(location) &&
      !Manifest::IsExternalLocation(location)) {
    NOTREACHED();
    return nullptr;
  }

  const base::Value* manifest = extension.Find(kPrefManifest);
  if (!Manifest::IsUnpackedLocation(location) &&
      !(manifest && manifest->is_dict())) {
    LOG(WARNING) << "Missing manifest for extension " << extension_id;
    // Just a warning for now.
  }

  const std::string* path = extension.FindString(kPrefPath);
  if (!path)
    return nullptr;
  base::FilePath file_path = base::FilePath::FromUTF8Unsafe(*path);

  // Make path absolute. Most (but not all) extension types have relative paths.
  if (!file_path.IsAbsolute())
    file_path = install_directory_.Append(file_path);
  const base::DictionaryValue* manifest_dict =
      (manifest && manifest->is_dict())
          ? &base::Value::AsDictionaryValue(*manifest)
          : nullptr;
  return std::make_unique<ExtensionInfo>(manifest_dict, extension_id, file_path,
                                         location);
}

std::unique_ptr<ExtensionInfo> ExtensionPrefs::GetInstalledExtensionInfo(
    const std::string& extension_id,
    bool include_component_extensions) const {
  const base::Value::Dict& extensions =
      prefs_->GetDict(pref_names::kExtensions);
  const base::Value::Dict* ext = extensions.FindDict(extension_id);
  if (!ext)
    return nullptr;

  absl::optional<int> state_value = ext->FindInt(kPrefState);
  // TODO(devlin): Remove this once all clients are updated with
  // MigrateToNewExternalUninstallPref().
  if (state_value == Extension::DEPRECATED_EXTERNAL_EXTENSION_UNINSTALLED)
    return nullptr;

  return GetInstalledInfoHelper(extension_id, *ext,
                                include_component_extensions);
}

std::unique_ptr<ExtensionPrefs::ExtensionsInfo>
ExtensionPrefs::GetInstalledExtensionsInfo(
    bool include_component_extensions) const {
  std::unique_ptr<ExtensionsInfo> extensions_info(new ExtensionsInfo);

  const base::Value::Dict& extensions =
      prefs_->GetDict(pref_names::kExtensions);
  for (const auto extension_id : extensions) {
    if (!crx_file::id_util::IdIsValid(extension_id.first))
      continue;

    std::unique_ptr<ExtensionInfo> info = GetInstalledExtensionInfo(
        extension_id.first, include_component_extensions);
    if (info)
      extensions_info->push_back(std::move(info));
  }

  return extensions_info;
}

void ExtensionPrefs::SetDelayedInstallInfo(
    const Extension* extension,
    Extension::State initial_state,
    int install_flags,
    DelayReason delay_reason,
    const syncer::StringOrdinal& page_ordinal,
    const std::string& install_parameter,
    const declarative_net_request::RulesetInstallPrefs& ruleset_install_prefs) {
  ScopedDictionaryUpdate update(this, extension->id(), kDelayedInstallInfo);
  auto extension_dict = update.Create();
  PopulateExtensionInfoPrefs(extension, clock_->Now(), initial_state,
                             install_flags, install_parameter,
                             ruleset_install_prefs, extension_dict.get());

  // Add transient data that is needed by FinishDelayedInstallInfo(), but
  // should not be in the final extension prefs. All entries here should have
  // a corresponding Remove() call in FinishDelayedInstallInfo().
  if (extension->RequiresSortOrdinal()) {
    extension_dict->SetString(
        kPrefSuggestedPageOrdinal,
        page_ordinal.IsValid() ? page_ordinal.ToInternalValue()
                               : std::string());
  }
  extension_dict->SetInteger(kDelayedInstallReason,
                             static_cast<int>(delay_reason));
}

bool ExtensionPrefs::RemoveDelayedInstallInfo(
    const std::string& extension_id) {
  if (!GetExtensionPref(extension_id))
    return false;
  ScopedExtensionPrefUpdate update(prefs_, extension_id);
  bool result = update->Remove(kDelayedInstallInfo);
  return result;
}

bool ExtensionPrefs::FinishDelayedInstallInfo(
    const std::string& extension_id) {
  CHECK(crx_file::id_util::IdIsValid(extension_id));
  ScopedExtensionPrefUpdate update(prefs_, extension_id);
  auto extension_dict = update.Get();
  std::unique_ptr<prefs::DictionaryValueUpdate> pending_install_dict;
  if (!extension_dict->GetDictionary(kDelayedInstallInfo,
                                     &pending_install_dict)) {
    return false;
  }

  // Retrieve and clear transient values populated by SetDelayedInstallInfo().
  // Also do any other data cleanup that makes sense.
  std::string serialized_ordinal;
  syncer::StringOrdinal suggested_page_ordinal;
  bool needs_sort_ordinal = false;
  if (pending_install_dict->GetString(kPrefSuggestedPageOrdinal,
                                      &serialized_ordinal)) {
    suggested_page_ordinal = syncer::StringOrdinal(serialized_ordinal);
    needs_sort_ordinal = true;
    pending_install_dict->Remove(kPrefSuggestedPageOrdinal);
  }
  pending_install_dict->Remove(kDelayedInstallReason);

  const base::Time install_time = clock_->Now();
  pending_install_dict->SetString(
      kPrefInstallTime, base::NumberToString(install_time.ToInternalValue()));

  // Commit the delayed install data.
  for (const auto [key, value] : *pending_install_dict->AsConstDict()) {
    extension_dict->Set(key, std::make_unique<base::Value>(value.Clone()));
  }
  FinishExtensionInfoPrefs(extension_id, install_time, needs_sort_ordinal,
                           suggested_page_ordinal, extension_dict.get());
  return true;
}

std::unique_ptr<ExtensionInfo> ExtensionPrefs::GetDelayedInstallInfo(
    const std::string& extension_id) const {
  const base::DictionaryValue* extension_prefs =
      GetExtensionPref(extension_id);
  if (!extension_prefs)
    return nullptr;

  const base::Value* ext = extension_prefs->FindDictKey(kDelayedInstallInfo);
  if (!ext)
    return nullptr;

  return GetInstalledInfoHelper(extension_id, ext->GetDict(),
                                /*include_component_extensions = */ false);
}

ExtensionPrefs::DelayReason ExtensionPrefs::GetDelayedInstallReason(
    const std::string& extension_id) const {
  const base::DictionaryValue* extension_prefs =
      GetExtensionPref(extension_id);
  if (!extension_prefs)
    return DELAY_REASON_NONE;

  const base::Value* ext = extension_prefs->FindDictKey(kDelayedInstallInfo);
  if (!ext)
    return DELAY_REASON_NONE;

  absl::optional<int> delay_reason = ext->FindIntKey(kDelayedInstallReason);
  if (!delay_reason)
    return DELAY_REASON_NONE;

  return static_cast<DelayReason>(*delay_reason);
}

std::unique_ptr<ExtensionPrefs::ExtensionsInfo>
ExtensionPrefs::GetAllDelayedInstallInfo() const {
  std::unique_ptr<ExtensionsInfo> extensions_info(new ExtensionsInfo);

  const base::Value::Dict& extensions =
      prefs_->GetDict(pref_names::kExtensions);
  for (const auto [extension_id, _] : extensions) {
    if (!crx_file::id_util::IdIsValid(extension_id))
      continue;

    std::unique_ptr<ExtensionInfo> info = GetDelayedInstallInfo(extension_id);
    if (info)
      extensions_info->push_back(std::move(info));
  }

  return extensions_info;
}

bool ExtensionPrefs::IsFromWebStore(
    const std::string& extension_id) const {
  const base::DictionaryValue* dictionary = GetExtensionPref(extension_id);
  if (dictionary)
    return dictionary->FindBoolKey(kPrefFromWebStore).value_or(false);
  return false;
}

int ExtensionPrefs::GetCreationFlags(const std::string& extension_id) const {
  int creation_flags = Extension::NO_FLAGS;
  if (!ReadPrefAsInteger(extension_id, kPrefCreationFlags, &creation_flags)) {
    // Since kPrefCreationFlags was added later, it will be missing for
    // previously installed extensions.
    if (IsFromWebStore(extension_id))
      creation_flags |= Extension::FROM_WEBSTORE;
    if (WasInstalledByDefault(extension_id))
      creation_flags |= Extension::WAS_INSTALLED_BY_DEFAULT;
    if (WasInstalledByOem(extension_id))
      creation_flags |= Extension::WAS_INSTALLED_BY_OEM;
  }
  return creation_flags;
}

int ExtensionPrefs::GetDelayedInstallCreationFlags(
    const std::string& extension_id) const {
  int creation_flags = Extension::NO_FLAGS;
  const base::DictionaryValue* delayed_info = nullptr;
  if (ReadPrefAsDictionary(extension_id, kDelayedInstallInfo, &delayed_info)) {
    if (absl::optional<int> flags =
            delayed_info->FindIntKey(kPrefCreationFlags)) {
      creation_flags = *flags;
    }
  }
  return creation_flags;
}

bool ExtensionPrefs::WasInstalledByDefault(
    const std::string& extension_id) const {
  const base::DictionaryValue* dictionary = GetExtensionPref(extension_id);
  if (!dictionary)
    return false;
  return dictionary->FindBoolKey(kPrefWasInstalledByDefault).value_or(false);
}

bool ExtensionPrefs::WasInstalledByOem(const std::string& extension_id) const {
  const base::DictionaryValue* dictionary = GetExtensionPref(extension_id);
  if (dictionary)
    return dictionary->FindBoolKey(kPrefWasInstalledByOem).value_or(false);
  return false;
}

base::Time ExtensionPrefs::GetInstallTime(
    const std::string& extension_id) const {
  const base::DictionaryValue* extension = GetExtensionPref(extension_id);
  if (!extension) {
    return base::Time();
  }
  std::string install_time_str;
  if (!extension->GetString(kPrefInstallTime, &install_time_str))
    return base::Time();
  int64_t install_time_i64 = 0;
  if (!base::StringToInt64(install_time_str, &install_time_i64))
    return base::Time();
  return base::Time::FromInternalValue(install_time_i64);
}

bool ExtensionPrefs::DoNotSync(const std::string& extension_id) const {
  bool do_not_sync;
  if (!ReadPrefAsBoolean(extension_id, kPrefDoNotSync, &do_not_sync))
    return false;

  return do_not_sync;
}

base::Time ExtensionPrefs::GetLastLaunchTime(
    const std::string& extension_id) const {
  const base::DictionaryValue* extension = GetExtensionPref(extension_id);
  if (!extension)
    return base::Time();

  std::string launch_time_str;
  if (!extension->GetString(kPrefLastLaunchTime, &launch_time_str))
    return base::Time();
  int64_t launch_time_i64 = 0;
  if (!base::StringToInt64(launch_time_str, &launch_time_i64))
    return base::Time();
  return base::Time::FromInternalValue(launch_time_i64);
}

void ExtensionPrefs::SetLastLaunchTime(const std::string& extension_id,
                                       const base::Time& time) {
  DCHECK(crx_file::id_util::IdIsValid(extension_id));
  {
    ScopedExtensionPrefUpdate update(prefs_, extension_id);
    SaveTime(update.Get().get(), kPrefLastLaunchTime, time);
  }
  for (auto& observer : observer_list_)
    observer.OnExtensionLastLaunchTimeChanged(extension_id, time);
}

void ExtensionPrefs::ClearLastLaunchTimes() {
  const base::Value::Dict& dict = prefs_->GetDict(pref_names::kExtensions);
  if (dict.empty())
    return;

  // Collect all the keys to remove the last launched preference from.
  prefs::ScopedDictionaryPrefUpdate update(prefs_, pref_names::kExtensions);
  auto update_dict = update.Get();
  for (const auto [key, value] : *update_dict->AsConstDict()) {
    std::unique_ptr<prefs::DictionaryValueUpdate> extension_dict;
    if (!update_dict->GetDictionary(key, &extension_dict))
      continue;

    if (extension_dict->HasKey(kPrefLastLaunchTime))
      extension_dict->Remove(kPrefLastLaunchTime);
  }
}

void ExtensionPrefs::SetPref(const PrefMap& pref,
                             std::unique_ptr<base::Value> value) {
  DCHECK_EQ(PrefScope::kProfile, pref.scope);
  DCHECK(CheckPrefType(pref.type, value.get()))
      << "The value passed in does not match the expected PrefType for "
      << pref.name;
  prefs_->Set(pref.name, std::move(*value));
}

void ExtensionPrefs::SetIntegerPref(const PrefMap& pref, int value) {
  DCHECK_EQ(PrefScope::kProfile, pref.scope);
  DCHECK_EQ(PrefType::kInteger, pref.type);
  prefs_->SetInteger(pref.name, value);
}

void ExtensionPrefs::SetBooleanPref(const PrefMap& pref, bool value) {
  DCHECK_EQ(PrefScope::kProfile, pref.scope);
  DCHECK_EQ(PrefType::kBool, pref.type);
  prefs_->SetBoolean(pref.name, value);
}

void ExtensionPrefs::SetStringPref(const PrefMap& pref,
                                   const std::string& value) {
  DCHECK_EQ(PrefScope::kProfile, pref.scope);
  DCHECK_EQ(PrefType::kString, pref.type);
  prefs_->SetString(pref.name, value);
}

void ExtensionPrefs::SetTimePref(const PrefMap& pref, base::Time value) {
  DCHECK_EQ(PrefScope::kProfile, pref.scope);
  DCHECK_EQ(PrefType::kTime, pref.type);
  prefs_->SetTime(pref.name, value);
}

void ExtensionPrefs::SetGURLPref(const PrefMap& pref, const GURL& value) {
  DCHECK_EQ(PrefScope::kProfile, pref.scope);
  DCHECK_EQ(PrefType::kGURL, pref.type);
  DCHECK(value.is_valid())
      << "Invalid GURL was passed in. The pref will not be updated.";
  prefs_->SetString(pref.name, value.spec());
}

void ExtensionPrefs::SetDictionaryPref(
    const PrefMap& pref,
    std::unique_ptr<base::DictionaryValue> value) {
  DCHECK_EQ(PrefScope::kProfile, pref.scope);
  DCHECK_EQ(PrefType::kDictionary, pref.type);
  SetPref(pref, std::move(value));
}

int ExtensionPrefs::GetPrefAsInteger(const PrefMap& pref) const {
  DCHECK_EQ(PrefScope::kProfile, pref.scope);
  DCHECK_EQ(PrefType::kInteger, pref.type);
  return prefs_->GetInteger(pref.name);
}

bool ExtensionPrefs::GetPrefAsBoolean(const PrefMap& pref) const {
  DCHECK_EQ(PrefScope::kProfile, pref.scope);
  DCHECK_EQ(PrefType::kBool, pref.type);
  return prefs_->GetBoolean(pref.name);
}

std::string ExtensionPrefs::GetPrefAsString(const PrefMap& pref) const {
  DCHECK_EQ(PrefScope::kProfile, pref.scope);
  DCHECK_EQ(PrefType::kString, pref.type);
  return prefs_->GetString(pref.name);
}

base::Time ExtensionPrefs::GetPrefAsTime(const PrefMap& pref) const {
  DCHECK_EQ(PrefScope::kProfile, pref.scope);
  DCHECK_EQ(PrefType::kTime, pref.type);
  return prefs_->GetTime(pref.name);
}

GURL ExtensionPrefs::GetPrefAsGURL(const PrefMap& pref) const {
  DCHECK_EQ(PrefScope::kProfile, pref.scope);
  DCHECK_EQ(PrefType::kGURL, pref.type);
  return GURL(prefs_->GetString(pref.name));
}

const base::DictionaryValue* ExtensionPrefs::GetPrefAsDictionary(
    const PrefMap& pref) const {
  DCHECK_EQ(PrefScope::kProfile, pref.scope);
  DCHECK_EQ(PrefType::kDictionary, pref.type);
  // TODO (https://crbug.com/1342019) This should call
  // `PrefService::GetDict`, which will in turn require the return type to
  // be `base::Value::Dict`.
  return &base::Value::AsDictionaryValue(prefs_->GetValue(pref.name));
}

std::unique_ptr<prefs::ScopedDictionaryPrefUpdate>
ExtensionPrefs::CreatePrefUpdate(const PrefMap& pref) {
  DCHECK_EQ(PrefScope::kProfile, pref.scope);
  DCHECK_EQ(PrefType::kDictionary, pref.type);
  return std::make_unique<prefs::ScopedDictionaryPrefUpdate>(prefs_, pref.name);
}

void ExtensionPrefs::IncrementPref(const PrefMap& pref) {
  int count = GetPrefAsInteger(pref);
  SetIntegerPref(pref, count + 1);
}

void ExtensionPrefs::DecrementPref(const PrefMap& pref) {
  int count = GetPrefAsInteger(pref);
  SetIntegerPref(pref, count - 1);
}

void ExtensionPrefs::GetExtensions(ExtensionIdList* out) const {
  CHECK(out);

  std::unique_ptr<ExtensionsInfo> extensions_info(GetInstalledExtensionsInfo());

  for (size_t i = 0; i < extensions_info->size(); ++i) {
    ExtensionInfo* info = extensions_info->at(i).get();
    out->push_back(info->extension_id);
  }
}

void ExtensionPrefs::AddObserver(ExtensionPrefsObserver* observer) {
  observer_list_.AddObserver(observer);
}

void ExtensionPrefs::RemoveObserver(ExtensionPrefsObserver* observer) {
  observer_list_.RemoveObserver(observer);
}

void ExtensionPrefs::InitPrefStore() {
  TRACE_EVENT0("browser,startup", "ExtensionPrefs::InitPrefStore");

  // When this is called, the PrefService is initialized and provides access
  // to the user preferences stored in a JSON file.
  std::unique_ptr<ExtensionsInfo> extensions_info;
  {
    SCOPED_UMA_HISTOGRAM_TIMER("Extensions.InitPrefGetExtensionsTime");
    extensions_info =
        GetInstalledExtensionsInfo(/*include_component_extensions = */ true);
  }

  if (extensions_disabled_) {
    // Normally, if extensions are disabled, we don't want to load the
    // controlled prefs from that extension. However, some extensions are
    // *always* loaded, even with e.g. --disable-extensions. For these, we
    // need to load the extension-controlled preferences.
    // See https://crbug.com/828295.
    auto predicate = [](const auto& info) {
      // HACK(devlin): Unpacked extensions stored in preferences do not have a
      // manifest, only a path (from which the manifest is later loaded). This
      // means that we don't know what type the extension is just from the
      // preferences (and, indeed, it may change types, if the file on disk has
      // changed).
      // Because of this, we may be passing |is_theme| incorrectly for unpacked
      // extensions below. This is okay in this instance, since if the extension
      // is a theme, initializing the controlled prefs shouldn't matter.
      // However, this is a pretty hacky solution. It would likely be better if
      // we could instead initialize the controlled preferences when the
      // extension is more finalized, but this also needs to happen sufficiently
      // before other subsystems are notified about the extension being loaded.
      Manifest::Type type =
          info->extension_manifest
              ? Manifest::GetTypeFromManifestValue(*info->extension_manifest)
              : Manifest::TYPE_UNKNOWN;
      bool is_theme = type == Manifest::TYPE_THEME;
      // Erase the entry if the extension won't be loaded.
      return !Manifest::ShouldAlwaysLoadExtension(info->extension_location,
                                                  is_theme);
    };
    base::EraseIf(*extensions_info, predicate);
  }

  InitExtensionControlledPrefs(*extensions_info);

  extension_pref_value_map_->NotifyInitializationCompleted();
}

bool ExtensionPrefs::HasIncognitoPrefValue(const std::string& pref_key) const {
  bool has_incognito_pref_value = false;
  extension_pref_value_map_->GetEffectivePrefValue(pref_key,
                                                   true,
                                                   &has_incognito_pref_value);
  return has_incognito_pref_value;
}

const base::DictionaryValue* ExtensionPrefs::GetGeometryCache(
    const std::string& extension_id) const {
  const base::DictionaryValue* extension_prefs = GetExtensionPref(extension_id);
  if (!extension_prefs)
    return nullptr;

  const base::Value* ext = extension_prefs->FindDictKey(kPrefGeometryCache);
  if (!ext)
    return nullptr;

  return &base::Value::AsDictionaryValue(*ext);
}

void ExtensionPrefs::SetGeometryCache(
    const std::string& extension_id,
    std::unique_ptr<base::DictionaryValue> cache) {
  UpdateExtensionPref(extension_id, kPrefGeometryCache, std::move(cache));
}

const base::DictionaryValue* ExtensionPrefs::GetInstallSignature() const {
  // TODO (https://crbug.com/1342019) This should call
  // `PrefService::GetDict`, which will in turn require the return type to
  // be `base::Value::Dict`.
  return &base::Value::AsDictionaryValue(prefs_->GetValue(kInstallSignature));
}

void ExtensionPrefs::SetInstallSignature(
    const base::DictionaryValue* signature) {
  if (signature) {
    prefs_->Set(kInstallSignature, *signature);
    DVLOG(1) << "SetInstallSignature - saving";
  } else {
    DVLOG(1) << "SetInstallSignature - clearing";
    prefs_->ClearPref(kInstallSignature);
  }
}

std::string ExtensionPrefs::GetInstallParam(
    const std::string& extension_id) const {
  const base::DictionaryValue* extension = GetExtensionPref(extension_id);
  if (!extension)  // Expected during unit testing.
    return std::string();
  std::string install_parameter;
  if (!extension->GetString(kPrefInstallParam, &install_parameter))
    return std::string();
  return install_parameter;
}

void ExtensionPrefs::SetInstallParam(const std::string& extension_id,
                                     const std::string& install_parameter) {
  UpdateExtensionPref(extension_id, kPrefInstallParam,
                      std::make_unique<base::Value>(install_parameter));
}

bool ExtensionPrefs::NeedsSync(const std::string& extension_id) const {
  return ReadPrefAsBooleanAndReturn(extension_id, kPrefNeedsSync);
}

void ExtensionPrefs::SetNeedsSync(const std::string& extension_id,
                                  bool needs_sync) {
  UpdateExtensionPref(
      extension_id, kPrefNeedsSync,
      needs_sync ? std::make_unique<base::Value>(true) : nullptr);
}

bool ExtensionPrefs::GetDNRStaticRulesetChecksum(
    const ExtensionId& extension_id,
    declarative_net_request::RulesetID ruleset_id,
    int* checksum) const {
  std::string pref =
      JoinPrefs({kDNRStaticRulesetPref,
                 base::NumberToString(ruleset_id.value()), kDNRChecksumKey});
  return ReadPrefAsInteger(extension_id, pref, checksum);
}

void ExtensionPrefs::SetDNRStaticRulesetChecksum(
    const ExtensionId& extension_id,
    declarative_net_request::RulesetID ruleset_id,
    int checksum) {
  std::string pref =
      JoinPrefs({kDNRStaticRulesetPref,
                 base::NumberToString(ruleset_id.value()), kDNRChecksumKey});
  UpdateExtensionPref(extension_id, pref,
                      std::make_unique<base::Value>(checksum));
}

bool ExtensionPrefs::GetDNRDynamicRulesetChecksum(
    const ExtensionId& extension_id,
    int* checksum) const {
  std::string pref = JoinPrefs({kDNRDynamicRulesetPref, kDNRChecksumKey});
  return ReadPrefAsInteger(extension_id, pref, checksum);
}

void ExtensionPrefs::SetDNRDynamicRulesetChecksum(
    const ExtensionId& extension_id,
    int checksum) {
  std::string pref = JoinPrefs({kDNRDynamicRulesetPref, kDNRChecksumKey});
  UpdateExtensionPref(extension_id, pref,
                      std::make_unique<base::Value>(checksum));
}

absl::optional<std::set<declarative_net_request::RulesetID>>
ExtensionPrefs::GetDNREnabledStaticRulesets(
    const ExtensionId& extension_id) const {
  std::set<declarative_net_request::RulesetID> ids;
  const base::ListValue* ids_value = nullptr;
  if (!ReadPrefAsList(extension_id, kDNREnabledStaticRulesetIDs, &ids_value))
    return absl::nullopt;

  DCHECK(ids_value);
  for (const base::Value& id_value : ids_value->GetList()) {
    if (!id_value.is_int())
      return absl::nullopt;

    ids.insert(declarative_net_request::RulesetID(id_value.GetInt()));
  }

  return ids;
}

void ExtensionPrefs::SetDNREnabledStaticRulesets(
    const ExtensionId& extension_id,
    const std::set<declarative_net_request::RulesetID>& ids) {
  base::Value::List ids_list;
  for (const auto& id : ids)
    ids_list.Append(id.value());

  UpdateExtensionPref(extension_id, kDNREnabledStaticRulesetIDs,
                      std::make_unique<base::Value>(std::move(ids_list)));
}

bool ExtensionPrefs::GetDNRUseActionCountAsBadgeText(
    const ExtensionId& extension_id) const {
  return ReadPrefAsBooleanAndReturn(extension_id,
                                    kPrefDNRUseActionCountAsBadgeText);
}

void ExtensionPrefs::SetDNRUseActionCountAsBadgeText(
    const ExtensionId& extension_id,
    bool use_action_count_as_badge_text) {
  UpdateExtensionPref(
      extension_id, kPrefDNRUseActionCountAsBadgeText,
      std::make_unique<base::Value>(use_action_count_as_badge_text));
}

bool ExtensionPrefs::ShouldIgnoreDNRRuleset(
    const ExtensionId& extension_id,
    declarative_net_request::RulesetID ruleset_id) const {
  std::string pref = JoinPrefs({kDNRStaticRulesetPref,
                                base::NumberToString(ruleset_id.value()),
                                kDNRIgnoreRulesetKey});
  return ReadPrefAsBooleanAndReturn(extension_id, pref);
}

bool ExtensionPrefs::GetDNRAllocatedGlobalRuleCount(
    const ExtensionId& extension_id,
    size_t* rule_count) const {
  int rule_count_value = -1;
  if (!ReadPrefAsInteger(extension_id, kDNRExtensionRulesAllocated,
                         &rule_count_value)) {
    return false;
  }

  DCHECK_GT(rule_count_value, 0);
  *rule_count = static_cast<size_t>(rule_count_value);
  return true;
}

void ExtensionPrefs::SetDNRAllocatedGlobalRuleCount(
    const ExtensionId& extension_id,
    size_t rule_count) {
  DCHECK_LE(
      rule_count,
      static_cast<size_t>(declarative_net_request::GetGlobalStaticRuleLimit()));

  // Clear the pref entry if the extension has a global allocation of 0.
  std::unique_ptr<base::Value> pref_value =
      rule_count > 0
          ? std::make_unique<base::Value>(static_cast<int>(rule_count))
          : nullptr;
  UpdateExtensionPref(extension_id, kDNRExtensionRulesAllocated,
                      std::move(pref_value));
}

bool ExtensionPrefs::GetDNRKeepExcessAllocation(
    const ExtensionId& extension_id) const {
  return ReadPrefAsBooleanAndReturn(extension_id, kPrefDNRKeepExcessAllocation);
}

void ExtensionPrefs::SetDNRKeepExcessAllocation(const ExtensionId& extension_id,
                                                bool keep_excess_allocation) {
  // Clear the pref entry if the extension will not keep its excess global rules
  // allocation.
  std::unique_ptr<base::Value> pref_value =
      keep_excess_allocation ? std::make_unique<base::Value>(true) : nullptr;

  UpdateExtensionPref(extension_id, kPrefDNRKeepExcessAllocation,
                      std::move(pref_value));
}

// static
void ExtensionPrefs::SetRunAlertsInFirstRunForTest() {
  g_run_alerts_in_first_run_for_testing = true;
}

void ExtensionPrefs::ClearExternalUninstallForTesting(const ExtensionId& id) {
  ClearExternalUninstallBit(id);
}

const char ExtensionPrefs::kFakeObsoletePrefForTesting[] =
    "__fake_obsolete_pref_for_testing";

ExtensionPrefs::ExtensionPrefs(
    content::BrowserContext* browser_context,
    PrefService* prefs,
    const base::FilePath& root_dir,
    ExtensionPrefValueMap* extension_pref_value_map,
    base::Clock* clock,
    bool extensions_disabled,
    const std::vector<EarlyExtensionPrefsObserver*>& early_observers)
    : browser_context_(browser_context),
      prefs_(prefs),
      install_directory_(root_dir),
      extension_pref_value_map_(extension_pref_value_map),
      clock_(clock),
      extensions_disabled_(extensions_disabled) {
  MakePathsRelative();

  // Ensure that any early observers are watching before prefs are initialized.
  for (auto iter = early_observers.cbegin(); iter != early_observers.cend();
       ++iter) {
    (*iter)->OnExtensionPrefsAvailable(this);
  }

  InitPrefStore();

  MigrateToNewWithholdingPref();

  MigrateToNewExternalUninstallPref();

  MigrateYoutubeOffBookmarkApps();

  MigrateDeprecatedDisableReasons();
}

AppSorting* ExtensionPrefs::app_sorting() const {
  return ExtensionSystem::Get(browser_context_)->app_sorting();
}

void ExtensionPrefs::SetNeedsStorageGarbageCollection(bool value) {
  prefs_->SetBoolean(pref_names::kStorageGarbageCollect, value);
}

bool ExtensionPrefs::NeedsStorageGarbageCollection() const {
  return prefs_->GetBoolean(pref_names::kStorageGarbageCollect);
}

// static
void ExtensionPrefs::RegisterProfilePrefs(
    user_prefs::PrefRegistrySyncable* registry) {
  registry->RegisterDictionaryPref(pref_names::kExtensions);
  registry->RegisterListPref(pref_names::kPinnedExtensions,
                             user_prefs::PrefRegistrySyncable::SYNCABLE_PREF);
  registry->RegisterListPref(pref_names::kDeletedComponentExtensions);
  registry->RegisterDictionaryPref(kExtensionsBlocklistUpdate);
  registry->RegisterListPref(pref_names::kInstallAllowList);
  registry->RegisterListPref(pref_names::kInstallDenyList);
  registry->RegisterDictionaryPref(pref_names::kInstallForceList);
  registry->RegisterListPref(pref_names::kAllowedTypes);
  registry->RegisterBooleanPref(pref_names::kStorageGarbageCollect, false);
  registry->RegisterListPref(pref_names::kAllowedInstallSites);
  registry->RegisterStringPref(pref_names::kLastChromeVersion, std::string());
  registry->RegisterDictionaryPref(kInstallSignature);
  registry->RegisterListPref(kExternalUninstalls);
#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX) || \
    BUILDFLAG(IS_FUCHSIA)
  registry->RegisterBooleanPref(pref_names::kChromeAppsEnabled, false);
#endif
  registry->RegisterBooleanPref(pref_names::kU2fSecurityKeyApiEnabled, false);
  registry->RegisterBooleanPref(pref_names::kLoadCryptoTokenExtension, false);

  registry->RegisterListPref(pref_names::kNativeMessagingBlocklist);
  registry->RegisterListPref(pref_names::kNativeMessagingAllowlist);
  registry->RegisterBooleanPref(pref_names::kNativeMessagingUserLevelHosts,
                                true);
  // TODO(archanasimha): move pref registration to where the variable is
  // defined.
  registry->RegisterIntegerPref(kCorruptedDisableCount.name, 0);

#if !BUILDFLAG(IS_MAC)
  registry->RegisterBooleanPref(pref_names::kAppFullscreenAllowed, true);
#endif

  registry->RegisterBooleanPref(pref_names::kBlockExternalExtensions, false);
}

template <class ExtensionIdContainer>
bool ExtensionPrefs::GetUserExtensionPrefIntoContainer(
    const char* pref,
    ExtensionIdContainer* id_container_out) const {
  DCHECK(id_container_out->empty());

  const base::Value* user_pref_value = prefs_->GetUserPrefValue(pref);
  if (!user_pref_value || !user_pref_value->is_list())
    return false;

  std::insert_iterator<ExtensionIdContainer> insert_iterator(
      *id_container_out, id_container_out->end());
  for (const auto& entry : user_pref_value->GetList()) {
    if (!entry.is_string()) {
      NOTREACHED();
      continue;
    }
    insert_iterator = entry.GetString();
  }
  return true;
}

template <class ExtensionIdContainer>
void ExtensionPrefs::SetExtensionPrefFromContainer(
    const char* pref,
    const ExtensionIdContainer& strings) {
  ListPrefUpdate update(prefs_, pref);
  base::Value::List& list_of_values = update->GetList();
  list_of_values.clear();
  for (auto iter = strings.cbegin(); iter != strings.cend(); ++iter) {
    list_of_values.Append(*iter);
  }
}

void ExtensionPrefs::PopulateExtensionInfoPrefs(
    const Extension* extension,
    const base::Time install_time,
    Extension::State initial_state,
    int install_flags,
    const std::string& install_parameter,
    const declarative_net_request::RulesetInstallPrefs& ruleset_install_prefs,
    prefs::DictionaryValueUpdate* extension_dict) {
  extension_dict->SetInteger(kPrefState, initial_state);
  extension_dict->SetInteger(kPrefLocation,
                             static_cast<int>(extension->location()));
  extension_dict->SetInteger(kPrefCreationFlags, extension->creation_flags());
  extension_dict->SetBoolean(kPrefFromWebStore, extension->from_webstore());
  extension_dict->SetBoolean(kPrefWasInstalledByDefault,
                             extension->was_installed_by_default());
  extension_dict->SetBoolean(kPrefWasInstalledByOem,
                             extension->was_installed_by_oem());
  extension_dict->SetString(
      kPrefInstallTime, base::NumberToString(install_time.ToInternalValue()));
  if (install_flags & kInstallFlagIsBlocklistedForMalware) {
    // Don't reset the acknowledged state during an update, because we wouldn't
    // want to reset the acknowledged state if the extension was already on the
    // blocklist.
    blocklist_prefs::SetSafeBrowsingExtensionBlocklistState(
        extension->id(), BitMapBlocklistState::BLOCKLISTED_MALWARE, this);
  }

  // If |ruleset_install_prefs| is empty, explicitly remove
  // the |kDNRStaticRulesetPref| entry to ensure any remaining old entries from
  // the previous install are cleared up in case of an update. Else just set the
  // entry (which will overwrite any existing value).
  if (ruleset_install_prefs.empty()) {
    extension_dict->Remove(kDNRStaticRulesetPref);
  } else {
    auto ruleset_prefs = std::make_unique<base::DictionaryValue>();
    for (const declarative_net_request::RulesetInstallPref& install_pref :
         ruleset_install_prefs) {
      std::string id_key =
          base::NumberToString(install_pref.ruleset_id.value());
      DCHECK(!ruleset_prefs->FindKey(id_key));
      auto* ruleset_dict = ruleset_prefs->SetKey(
          id_key, base::Value(base::Value::Type::DICTIONARY));
      if (install_pref.checksum)
        ruleset_dict->SetIntKey(kDNRChecksumKey, *install_pref.checksum);

      ruleset_dict->SetBoolKey(kDNRIgnoreRulesetKey, install_pref.ignored);
    }

    extension_dict->SetDictionary(kDNRStaticRulesetPref,
                                  std::move(ruleset_prefs));
  }

  // Clear the list of enabled static rulesets for the extension since it
  // shouldn't persist across extension updates.
  extension_dict->Remove(kDNREnabledStaticRulesetIDs);

  if (util::CanWithholdPermissionsFromExtension(*extension)) {
    // If the withhold permission creation flag is present it takes precedence
    // over any previous stored value.
    if (extension->creation_flags() & Extension::WITHHOLD_PERMISSIONS) {
      extension_dict->SetBoolean(kPrefWithholdingPermissions, true);
    } else if (!HasWithholdingPermissionsSetting(extension->id())) {
      // If no withholding creation flag was specified and there is no value
      // stored already, we set the default value.
      extension_dict->SetBoolean(kPrefWithholdingPermissions,
                                 kDefaultWithholdingBehavior);
    }
  }

  std::string path = MakePathRelative(install_directory_, extension->path());
  extension_dict->SetString(kPrefPath, path);
  if (!install_parameter.empty()) {
    extension_dict->SetString(kPrefInstallParam, install_parameter);
  }
  // We store prefs about LOAD extensions, but don't cache their manifest
  // since it may change on disk.
  if (!Manifest::IsUnpackedLocation(extension->location())) {
    extension_dict->SetKey(kPrefManifest,
                           extension->manifest()->value()->Clone());
  }

  // Only writes kPrefDoNotSync when it is not the default.
  if (install_flags & kInstallFlagDoNotSync)
    extension_dict->SetBoolean(kPrefDoNotSync, true);
  else
    extension_dict->Remove(kPrefDoNotSync);
}

void ExtensionPrefs::InitExtensionControlledPrefs(
    const ExtensionsInfo& extensions_info) {
  TRACE_EVENT0("browser,startup",
               "ExtensionPrefs::InitExtensionControlledPrefs");

  for (const auto& info : extensions_info) {
    const ExtensionId& extension_id = info->extension_id;

    base::Time install_time = GetInstallTime(extension_id);
    bool is_enabled = !IsExtensionDisabled(extension_id);
    bool is_incognito_enabled = IsIncognitoEnabled(extension_id);
    extension_pref_value_map_->RegisterExtension(
        extension_id, install_time, is_enabled, is_incognito_enabled);

    for (auto& observer : observer_list_)
      observer.OnExtensionRegistered(extension_id, install_time, is_enabled);

    // Set regular extension controlled prefs.
    LoadExtensionControlledPrefs(extension_id, kExtensionPrefsScopeRegular);
    // Set incognito extension controlled prefs.
    LoadExtensionControlledPrefs(extension_id,
                                 kExtensionPrefsScopeIncognitoPersistent);
    // Set regular-only extension controlled prefs.
    LoadExtensionControlledPrefs(extension_id, kExtensionPrefsScopeRegularOnly);

    for (auto& observer : observer_list_)
      observer.OnExtensionPrefsLoaded(extension_id, this);
  }
}

void ExtensionPrefs::LoadExtensionControlledPrefs(
    const ExtensionId& extension_id,
    ExtensionPrefsScope scope) {
  std::string scope_string;
  if (!pref_names::ScopeToPrefName(scope, &scope_string))
    return;
  std::string key = extension_id + "." + scope_string;

  const base::Value::Dict& source_dict =
      pref_service()->GetDict(pref_names::kExtensions);

  const base::Value::Dict* preferences = source_dict.FindDictByDottedPath(key);
  if (!preferences)
    return;

  for (auto pair : *preferences) {
    extension_pref_value_map_->SetExtensionPref(extension_id, pair.first, scope,
                                                pair.second.Clone());
  }
}

void ExtensionPrefs::FinishExtensionInfoPrefs(
    const std::string& extension_id,
    const base::Time install_time,
    bool needs_sort_ordinal,
    const syncer::StringOrdinal& suggested_page_ordinal,
    prefs::DictionaryValueUpdate* extension_dict) {
  // Reinitializes various preferences with empty dictionaries.
  if (!extension_dict->HasKey(pref_names::kPrefPreferences)) {
    extension_dict->Set(pref_names::kPrefPreferences,
                        std::make_unique<base::DictionaryValue>());
  }

  if (!extension_dict->HasKey(pref_names::kPrefIncognitoPreferences)) {
    extension_dict->Set(pref_names::kPrefIncognitoPreferences,
                        std::make_unique<base::DictionaryValue>());
  }

  if (!extension_dict->HasKey(pref_names::kPrefRegularOnlyPreferences)) {
    extension_dict->Set(pref_names::kPrefRegularOnlyPreferences,
                        std::make_unique<base::DictionaryValue>());
  }

  if (!extension_dict->HasKey(pref_names::kPrefContentSettings))
    extension_dict->Set(pref_names::kPrefContentSettings,
                        std::make_unique<base::ListValue>());

  if (!extension_dict->HasKey(pref_names::kPrefIncognitoContentSettings)) {
    extension_dict->Set(pref_names::kPrefIncognitoContentSettings,
                        std::make_unique<base::ListValue>());
  }

  // If this point has been reached, any pending installs should be considered
  // out of date.
  extension_dict->Remove(kDelayedInstallInfo);

  // Clear state that may be registered from a previous install.
  extension_dict->Remove(EventRouter::kRegisteredLazyEvents);
  extension_dict->Remove(EventRouter::kRegisteredServiceWorkerEvents);

  // FYI, all code below here races on sudden shutdown because |extension_dict|,
  // |app_sorting|, |extension_pref_value_map_|, and (potentially) observers
  // are updated non-transactionally. This is probably not fixable without
  // nested transactional updates to pref dictionaries.
  if (needs_sort_ordinal)
    app_sorting()->EnsureValidOrdinals(extension_id, suggested_page_ordinal);

  bool is_enabled = false;
  int initial_state;
  if (extension_dict->GetInteger(kPrefState, &initial_state)) {
    is_enabled = initial_state == Extension::ENABLED;
  }
  bool is_incognito_enabled = IsIncognitoEnabled(extension_id);

  extension_pref_value_map_->RegisterExtension(
      extension_id, install_time, is_enabled, is_incognito_enabled);

  for (auto& observer : observer_list_)
    observer.OnExtensionRegistered(extension_id, install_time, is_enabled);
}

void ExtensionPrefs::MigrateDeprecatedDisableReasons() {
  std::unique_ptr<ExtensionsInfo> extensions_info(GetInstalledExtensionsInfo());

  for (const auto& info : *extensions_info) {
    const ExtensionId& extension_id = info->extension_id;
    int disable_reasons = GetDisableReasons(extension_id);
    if ((disable_reasons &
         disable_reason::DEPRECATED_DISABLE_UNKNOWN_FROM_SYNC) == 0)
      continue;
    disable_reasons &= ~disable_reason::DEPRECATED_DISABLE_UNKNOWN_FROM_SYNC;
    if (disable_reasons == 0) {
      // We don't know exactly why the extension was disabled, but we don't
      // want to just suddenly re-enable it. Default to disabling it by the
      // user (which was most likely for coming in from sync, and is
      // reversible).
      disable_reasons = disable_reason::DISABLE_USER_ACTION;
    }
    ReplaceDisableReasons(extension_id, disable_reasons);
  }
}

void ExtensionPrefs::MigrateYoutubeOffBookmarkApps() {
  const base::Value::Dict& extensions_dictionary =
      prefs_->GetDict(pref_names::kExtensions);
  const base::Value::Dict* youtube_dictionary =
      extensions_dictionary.FindDict(extension_misc::kYoutubeAppId);
  if (!youtube_dictionary) {
    return;
  }
  int creation_flags =
      youtube_dictionary->FindInt(kPrefCreationFlags).value_or(0);
  if ((creation_flags & Extension::FROM_BOOKMARK) == 0)
    return;
  ScopedExtensionPrefUpdate update(prefs_, extension_misc::kYoutubeAppId);
  creation_flags &= ~Extension::FROM_BOOKMARK;
  update->SetInteger(kPrefCreationFlags, creation_flags);
}

void ExtensionPrefs::MigrateObsoleteExtensionPrefs() {
  const base::Value::Dict& extensions_dictionary =
      prefs_->GetDict(pref_names::kExtensions);

  // Please clean this list up periodically, removing any entries added more
  // than a year ago (with the exception of the testing key).
  constexpr const char* kObsoleteKeys[] = {
      // Permanent testing-only key.
      kFakeObsoletePrefForTesting,

      // TODO(crbug.com/1015619): Remove 2023-05. Incorrect spelling from 2013.
      "id_mapping_dictioanry"};

  for (auto key_value : extensions_dictionary) {
    if (!crx_file::id_util::IdIsValid(key_value.first))
      continue;
    ScopedExtensionPrefUpdate update(prefs_, key_value.first);
    std::unique_ptr<prefs::DictionaryValueUpdate> inner_update = update.Get();

    for (const char* key : kObsoleteKeys)
      inner_update->Remove(key);
  }
}

void ExtensionPrefs::MigrateToNewWithholdingPref() {
  std::unique_ptr<ExtensionsInfo> extensions_info(GetInstalledExtensionsInfo());

  for (const auto& info : *extensions_info) {
    const ExtensionId& extension_id = info->extension_id;
    // The manifest may be null in some cases, such as unpacked extensions
    // retrieved from the Preference file.
    if (!info->extension_manifest)
      continue;

    // If the new key is present in the prefs already, we don't need to check
    // further.
    bool value = false;
    if (ReadPrefAsBoolean(extension_id, kPrefWithholdingPermissions, &value)) {
      continue;
    }

    // We only want to migrate extensions we can actually withhold permissions
    // from.
    Manifest::Type type =
        Manifest::GetTypeFromManifestValue(*info->extension_manifest);
    ManifestLocation location = info->extension_location;
    if (!util::CanWithholdPermissionsFromExtension(extension_id, type,
                                                   location))
      continue;

    bool old_pref_value = false;
    // If there was an old preference set, use the same (conceptual) value.
    // Otherwise, use the default setting.
    bool new_pref_value = kDefaultWithholdingBehavior;
    if (ReadPrefAsBoolean(extension_id, kGrantExtensionAllHostPermissions,
                          &old_pref_value)) {
      // We invert the value as the previous pref stored if the extension was
      // granted all the requested permissions, whereas the new pref stores if
      // requested permissions are currently being withheld.
      new_pref_value = !old_pref_value;
    }

    UpdateExtensionPref(extension_id, kPrefWithholdingPermissions,
                        std::make_unique<base::Value>(new_pref_value));
  }
}

void ExtensionPrefs::MigrateToNewExternalUninstallPref() {
  const base::Value::Dict& extensions =
      prefs_->GetDict(pref_names::kExtensions);

  std::vector<std::string> uninstalled_ids;
  for (auto item : extensions) {
    if (!crx_file::id_util::IdIsValid(item.first) || !item.second.is_dict()) {
      continue;
    }

    absl::optional<int> state_value = item.second.FindIntKey(kPrefState);
    if (!state_value ||
        *state_value != Extension::DEPRECATED_EXTERNAL_EXTENSION_UNINSTALLED) {
      continue;
    }
    uninstalled_ids.push_back(item.first);
  }

  if (uninstalled_ids.empty())
    return;

  ListPrefUpdate update(prefs_, kExternalUninstalls);
  base::Value::List& current_ids = update->GetList();
  for (const auto& id : uninstalled_ids) {
    auto existing_entry =
        std::find_if(current_ids.begin(), current_ids.end(),
                     [&id](const base::Value& value) {
                       return value.is_string() && value.GetString() == id;
                     });
    if (existing_entry == current_ids.end())
      current_ids.Append(id);

    DeleteExtensionPrefs(id);
  }
}

bool ExtensionPrefs::ShouldInstallObsoleteComponentExtension(
    const std::string& extension_id) {
  ListPrefUpdate update(prefs_, pref_names::kDeletedComponentExtensions);
  base::Value::List& current_ids = update->GetList();
  auto existing_entry = std::find_if(current_ids.begin(), current_ids.end(),
                                     [&extension_id](const base::Value& value) {
                                       return value.is_string() &&
                                              value.GetString() == extension_id;
                                     });
  return (existing_entry == current_ids.end());
}

void ExtensionPrefs::MarkObsoleteComponentExtensionAsRemoved(
    const std::string& extension_id,
    const ManifestLocation location) {
  ListPrefUpdate update(prefs_, pref_names::kDeletedComponentExtensions);
  base::Value::List& current_ids = update->GetList();
  auto existing_entry = std::find_if(current_ids.begin(), current_ids.end(),
                                     [&extension_id](const base::Value& value) {
                                       return value.is_string() &&
                                              value.GetString() == extension_id;
                                     });
  // This should only be called once per extension.
  DCHECK(existing_entry == current_ids.end());
  current_ids.Append(extension_id);
  OnExtensionUninstalled(extension_id, location, false);
}

void ExtensionPrefs::ClearExternalUninstallBit(const ExtensionId& id) {
  ListPrefUpdate update(prefs_, kExternalUninstalls);
  base::Value::List& current_ids = update->GetList();
  current_ids.EraseIf([&id](const base::Value& value) {
    return value.is_string() && value.GetString() == id;
  });
}

}  // namespace extensions
