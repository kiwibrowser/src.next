// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef EXTENSIONS_BROWSER_EXTENSION_PREFS_H_
#define EXTENSIONS_BROWSER_EXTENSION_PREFS_H_

#include <memory>
#include <set>
#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/observer_list.h"
#include "base/strings/string_piece_forward.h"
#include "base/time/time.h"
#include "base/values.h"
#include "components/keyed_service/core/keyed_service.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "components/sync/model/string_ordinal.h"
#include "extensions/browser/api/declarative_net_request/ruleset_install_pref.h"
#include "extensions/browser/blocklist_state.h"
#include "extensions/browser/disable_reason.h"
#include "extensions/browser/extension_prefs_scope.h"
#include "extensions/browser/install_flag.h"
#include "extensions/browser/pref_types.h"
#include "extensions/common/api/declarative_net_request/constants.h"
#include "extensions/common/constants.h"
#include "extensions/common/extension.h"
#include "extensions/common/extension_id.h"
#include "extensions/common/mojom/manifest.mojom-shared.h"
#include "extensions/common/url_pattern_set.h"
#include "services/preferences/public/cpp/dictionary_value_update.h"
#include "services/preferences/public/cpp/scoped_pref_update.h"
#include "third_party/abseil-cpp/absl/types/optional.h"

class ExtensionPrefValueMap;
class PrefService;

namespace base {
class Clock;
}

namespace content {
class BrowserContext;
}

namespace prefs {
class DictionaryValueUpdate;
}

namespace user_prefs {
class PrefRegistrySyncable;
}

namespace extensions {

class AppSorting;
class EarlyExtensionPrefsObserver;
class ExtensionPrefsObserver;
class PermissionSet;
class URLPatternSet;

// Class for managing global and per-extension preferences.
//
// This class distinguishes the following kinds of preferences:
// - global preferences:
//       internal state for the extension system in general, not associated
//       with an individual extension, such as lastUpdateTime.
// - per-extension preferences:
//       meta-preferences describing properties of the extension like
//       installation time, whether the extension is enabled, etc.
// - extension controlled preferences:
//       browser preferences that an extension controls. For example, an
//       extension could use the proxy API to specify the browser's proxy
//       preference. Extension-controlled preferences are stored in
//       PrefValueStore::extension_prefs(), which this class populates and
//       maintains as the underlying extensions change.
class ExtensionPrefs : public KeyedService {
 public:
  using ExtensionsInfo = std::vector<std::unique_ptr<ExtensionInfo>>;

  // Vector containing identifiers for preferences.
  typedef std::set<std::string> PrefKeySet;

  // This enum is used to store the reason an extension's install has been
  // delayed.  Do not remove items or re-order this enum as it is used in
  // preferences.
  enum DelayReason {
    DELAY_REASON_NONE = 0,
    DELAY_REASON_GC = 1,
    DELAY_REASON_WAIT_FOR_IDLE = 2,
    DELAY_REASON_WAIT_FOR_IMPORTS = 3,
    DELAY_REASON_WAIT_FOR_OS_UPDATE = 4,
  };

  // This enum is used to specify the operation for bit map prefs.
  enum BitMapPrefOperation {
    BIT_MAP_PREF_ADD,
    BIT_MAP_PREF_REMOVE,
    BIT_MAP_PREF_REPLACE,
    BIT_MAP_PREF_CLEAR
  };

  // Wrappers around a prefs::ScopedDictionaryPrefUpdate, which allow us to
  // access the entry of a particular key for an extension. Use these if you
  // need a mutable record of a dictionary or list in the current settings.
  // Otherwise, prefer ReadPrefAsT() and UpdateExtensionPref() methods.
  class ScopedDictionaryUpdate {
   public:
    ScopedDictionaryUpdate(ExtensionPrefs* prefs,
                           const std::string& extension_id,
                           const std::string& key);

    ScopedDictionaryUpdate(const ScopedDictionaryUpdate&) = delete;
    ScopedDictionaryUpdate& operator=(const ScopedDictionaryUpdate&) = delete;

    ~ScopedDictionaryUpdate();

    // Returns a mutable value for the key, if one exists. Otherwise, returns
    // NULL.
    std::unique_ptr<prefs::DictionaryValueUpdate> Get();

    // Creates and returns a mutable value for the key, if one does not already
    // exist. Otherwise, returns the current value.
    std::unique_ptr<prefs::DictionaryValueUpdate> Create();

   private:
    std::unique_ptr<prefs::ScopedDictionaryPrefUpdate> update_;
    const std::string key_;
  };

  class ScopedListUpdate {
   public:
    ScopedListUpdate(ExtensionPrefs* prefs,
                     const std::string& extension_id,
                     const std::string& key);

    ScopedListUpdate(const ScopedListUpdate&) = delete;
    ScopedListUpdate& operator=(const ScopedListUpdate&) = delete;

    ~ScopedListUpdate();

    // Returns a mutable value for the key (ownership remains with the prefs),
    // if one exists. Otherwise, returns NULL.
    base::Value::List* Get();

    // Creates and returns a mutable value for the key (the prefs own the new
    // value), if one does not already exist. Otherwise, returns the current
    // value.
    base::Value::List* Ensure();

   private:
    std::unique_ptr<prefs::ScopedDictionaryPrefUpdate> update_;
    const std::string key_;
  };

  // Creates an ExtensionPrefs object.
  // Does not take ownership of |prefs| or |extension_pref_value_map|.
  // If |extensions_disabled| is true, extension controlled preferences and
  // content settings do not become effective. EarlyExtensionPrefsObservers
  // should be included in |early_observers| if they need to observe events
  // which occur during initialization of the ExtensionPrefs object.
  static ExtensionPrefs* Create(
      content::BrowserContext* browser_context,
      PrefService* prefs,
      const base::FilePath& root_dir,
      ExtensionPrefValueMap* extension_pref_value_map,
      bool extensions_disabled,
      const std::vector<EarlyExtensionPrefsObserver*>& early_observers);

  // A version of Create which allows injection of a custom base::Time provider.
  // Use this as needed for testing.
  static ExtensionPrefs* Create(
      content::BrowserContext* browser_context,
      PrefService* prefs,
      const base::FilePath& root_dir,
      ExtensionPrefValueMap* extension_pref_value_map,
      bool extensions_disabled,
      const std::vector<EarlyExtensionPrefsObserver*>& early_observers,
      base::Clock* clock);

  ExtensionPrefs(const ExtensionPrefs&) = delete;
  ExtensionPrefs& operator=(const ExtensionPrefs&) = delete;

  ~ExtensionPrefs() override;

  // Convenience function to get the ExtensionPrefs for a BrowserContext.
  static ExtensionPrefs* Get(content::BrowserContext* context);

  // Add or remove an observer from the ExtensionPrefs.
  void AddObserver(ExtensionPrefsObserver* observer);
  void RemoveObserver(ExtensionPrefsObserver* observer);

  // Returns true if the specified external extension was uninstalled by the
  // user.
  bool IsExternalExtensionUninstalled(const std::string& id) const;

  // Checks whether |extension_id| is disabled. If there's no state pref for
  // the extension, this will return false. Generally you should use
  // ExtensionService::IsExtensionEnabled instead.
  // Note that blocklisted extensions are NOT marked as disabled!
  bool IsExtensionDisabled(const std::string& id) const;

  // Get/Set the set of extensions that are pinned to the toolbar. Only used
  // when the experiment ExtensionsMenu is active."
  // TODO(crbug.com/943702): Remove reference to experiment when it launches or
  // remove code if it does not.
  ExtensionIdList GetPinnedExtensions() const;
  void SetPinnedExtensions(const ExtensionIdList& extension_ids);

  // Called when an extension is installed, so that prefs get created.
  // If |page_ordinal| is invalid then a page will be found for the App.
  // |install_flags| are a bitmask of extension::InstallFlags.
  // |ruleset_install_prefs| contains install prefs needed for the Declarative
  // Net Request API.
  void OnExtensionInstalled(const Extension* extension,
                            Extension::State initial_state,
                            const syncer::StringOrdinal& page_ordinal,
                            int install_flags,
                            const std::string& install_parameter,
                            const declarative_net_request::RulesetInstallPrefs&
                                ruleset_install_prefs);
  // OnExtensionInstalled with no install flags and |ruleset_install_prefs|.
  void OnExtensionInstalled(const Extension* extension,
                            Extension::State initial_state,
                            const syncer::StringOrdinal& page_ordinal,
                            const std::string& install_parameter) {
    OnExtensionInstalled(extension, initial_state, page_ordinal,
                         kInstallFlagNone, install_parameter, {});
  }

  // Called when an extension is uninstalled, so that prefs get cleaned up.
  void OnExtensionUninstalled(const std::string& extension_id,
                              const mojom::ManifestLocation location,
                              bool external_uninstall);

  // Sets the extension's state to enabled and clears disable reasons.
  void SetExtensionEnabled(const std::string& extension_id);

  // Sets the extension's state to disabled and sets the disable reasons.
  // However, if the current state is EXTERNAL_EXTENSION_UNINSTALLED then that
  // is preserved (but the disable reasons are still set).
  void SetExtensionDisabled(const std::string& extension_id,
                            int disable_reasons);

  // Gets the value of a bit map pref. Gets the value of
  // |extension_id| from |pref_key|. If the value is not found or invalid,
  // return the |default_bit|.
  int GetBitMapPrefBits(const std::string& extension_id,
                        base::StringPiece pref_key,
                        int default_bit) const;
  // Modifies the extensions bit map pref |pref_key| to add a new bit value,
  // remove an existing bit value, or clear all bits. If |operation| is
  // BIT_MAP_PREF_CLEAR, then |pending_bits| are ignored. If the updated pref
  // value is the same as the |default_bit|, the pref value will be set to null.
  void ModifyBitMapPrefBits(const std::string& extension_id,
                            int pending_bits,
                            BitMapPrefOperation operation,
                            base::StringPiece pref_key,
                            int default_bit);

  // Gets or sets profile wide ExtensionPrefs.
  void SetIntegerPref(const PrefMap& pref, int value);
  void SetBooleanPref(const PrefMap& pref, bool value);
  void SetStringPref(const PrefMap& pref, const std::string& value);
  void SetTimePref(const PrefMap& pref, base::Time value);
  void SetGURLPref(const PrefMap& pref, const GURL& value);
  void SetDictionaryPref(const PrefMap& pref,
                         std::unique_ptr<base::DictionaryValue> value);

  int GetPrefAsInteger(const PrefMap& pref) const;
  bool GetPrefAsBoolean(const PrefMap& pref) const;
  std::string GetPrefAsString(const PrefMap& pref) const;
  base::Time GetPrefAsTime(const PrefMap& pref) const;
  GURL GetPrefAsGURL(const PrefMap& pref) const;
  const base::DictionaryValue* GetPrefAsDictionary(const PrefMap& pref) const;

  // Returns a wrapper that allows to update an ExtensionPref with a
  // PrefType::kDictionary.
  std::unique_ptr<prefs::ScopedDictionaryPrefUpdate> CreatePrefUpdate(
      const PrefMap& pref);

  // Increments/decrements an ExtensionPref with a PrefType::kInteger.
  void IncrementPref(const PrefMap& pref);
  void DecrementPref(const PrefMap& pref);

  // Populates |out| with the ids of all installed extensions.
  void GetExtensions(ExtensionIdList* out) const;

  void SetIntegerPref(const std::string& id, const PrefMap& pref, int value);
  void SetBooleanPref(const std::string& id, const PrefMap& pref, bool value);
  void SetStringPref(const std::string& id,
                     const PrefMap& pref,
                     const std::string value);
  void SetListPref(const std::string& id,
                   const PrefMap& pref,
                   base::Value value);
  void SetDictionaryPref(const std::string& id,
                         const PrefMap& pref,
                         std::unique_ptr<base::DictionaryValue> value);
  void SetTimePref(const std::string& id,
                   const PrefMap& pref,
                   const base::Time value);

  void UpdateExtensionPref(const std::string& id,
                           base::StringPiece key,
                           std::unique_ptr<base::Value> value);

  void DeleteExtensionPrefs(const std::string& id);

  void DeleteExtensionPrefsIfPrefEmpty(const std::string& id);

  bool ReadPrefAsBoolean(const std::string& extension_id,
                         const PrefMap& pref,
                         bool* out_value) const;

  bool ReadPrefAsInteger(const std::string& extension_id,
                         const PrefMap& pref,
                         int* out_value) const;

  bool ReadPrefAsString(const std::string& extension_id,
                        const PrefMap& pref,
                        std::string* out_value) const;

  bool ReadPrefAsList(const std::string& extension_id,
                      const PrefMap& pref,
                      const base::ListValue** out_value) const;

  bool ReadPrefAsDictionary(const std::string& extension_id,
                            const PrefMap& pref,
                            const base::DictionaryValue** out_value) const;

  base::Time ReadPrefAsTime(const std::string& extension_id,
                            const PrefMap& pref) const;

  bool ReadPrefAsBoolean(const std::string& extension_id,
                         base::StringPiece pref_key,
                         bool* out_value) const;

  bool ReadPrefAsInteger(const std::string& extension_id,
                         base::StringPiece pref_key,
                         int* out_value) const;

  bool ReadPrefAsString(const std::string& extension_id,
                        base::StringPiece pref_key,
                        std::string* out_value) const;

  bool ReadPrefAsList(const std::string& extension_id,
                      base::StringPiece pref_key,
                      const base::ListValue** out_value) const;

  // DEPRECATED: prefer ReadPrefAsDict() instead.
  bool ReadPrefAsDictionary(const std::string& extension_id,
                            base::StringPiece pref_key,
                            const base::DictionaryValue** out_value) const;

  const base::Value::Dict* ReadPrefAsDict(const std::string& extension_id,
                                          base::StringPiece pref_key) const;

  // Interprets the list pref, |pref_key| in |extension_id|'s preferences, as a
  // URLPatternSet. The |valid_schemes| specify how to parse the URLPatterns.
  bool ReadPrefAsURLPatternSet(const std::string& extension_id,
                               base::StringPiece pref_key,
                               URLPatternSet* result,
                               int valid_schemes) const;

  // Converts |set| to a list of strings and sets the |pref_key| pref belonging
  // to |extension_id|. If |set| is empty, the preference for |pref_key| is
  // cleared.
  void SetExtensionPrefURLPatternSet(const std::string& extension_id,
                                     base::StringPiece pref_key,
                                     const URLPatternSet& set);

  bool HasPrefForExtension(const std::string& extension_id) const;

  // Did the extension ask to escalate its permission during an upgrade?
  bool DidExtensionEscalatePermissions(const std::string& id) const;

  // Getters and setters for disabled reason.
  // Note that you should rarely need to modify disable reasons directly -
  // pass the proper value to SetExtensionState instead when you enable/disable
  // an extension. In particular, AddDisableReason(s) is only legal when the
  // extension is not enabled.
  int GetDisableReasons(const std::string& extension_id) const;
  bool HasDisableReason(const std::string& extension_id,
                        disable_reason::DisableReason disable_reason) const;
  void AddDisableReason(const std::string& extension_id,
                        disable_reason::DisableReason disable_reason);
  void AddDisableReasons(const std::string& extension_id, int disable_reasons);
  void RemoveDisableReason(const std::string& extension_id,
                           disable_reason::DisableReason disable_reason);
  void ReplaceDisableReasons(const std::string& extension_id,
                             int disable_reasons);
  void ClearDisableReasons(const std::string& extension_id);

  // Clears disable reasons that do not apply to component extensions.
  void ClearInapplicableDisableReasonsForComponentExtension(
      const std::string& component_extension_id);

  // Returns the version string for the currently installed extension, or
  // the empty string if not found.
  std::string GetVersionString(const std::string& extension_id) const;

  // Re-writes the extension manifest into the prefs.
  // Called to change the extension's manifest when it's re-localized.
  void UpdateManifest(const Extension* extension);

  // Returns base extensions install directory.
  const base::FilePath& install_directory() const { return install_directory_; }

  // For updating the prefs when the install location is changed for the
  // extension.
  void SetInstallLocation(const std::string& extension_id,
                          mojom::ManifestLocation location);

  // Increment the count of how many times we prompted the user to acknowledge
  // the given extension, and return the new count.
  int IncrementAcknowledgePromptCount(const std::string& extension_id);

  // Whether the user has acknowledged an external extension.
  bool IsExternalExtensionAcknowledged(const std::string& extension_id) const;
  void AcknowledgeExternalExtension(const std::string& extension_id);

  // Whether the user has acknowledged a blocklisted extension.
  bool IsBlocklistedExtensionAcknowledged(
      const std::string& extension_id) const;
  void AcknowledgeBlocklistedExtension(const std::string& extension_id);

  // Whether the external extension was installed during the first run
  // of this profile.
  bool IsExternalInstallFirstRun(const std::string& extension_id) const;
  void SetExternalInstallFirstRun(const std::string& extension_id);

  // Returns true if the extension notification code has already run for the
  // first time for this profile. Currently we use this flag to mean that any
  // extensions that would trigger notifications should get silently
  // acknowledged. This is a fuse. Calling it the first time returns false.
  // Subsequent calls return true. It's not possible through an API to ever
  // reset it. Don't call it unless you mean it!
  bool SetAlertSystemFirstRun();

  // Returns the last value set via SetLastPingDay. If there isn't such a
  // pref, the returned Time will return true for is_null().
  base::Time LastPingDay(const std::string& extension_id) const;

  // The time stored is based on the server's perspective of day start time, not
  // the client's.
  void SetLastPingDay(const std::string& extension_id, const base::Time& time);

  // Similar to the 2 above, but for the extensions blocklist.
  base::Time BlocklistLastPingDay() const;
  void SetBlocklistLastPingDay(const base::Time& time);

  // Similar to LastPingDay/SetLastPingDay, but for sending "days since active"
  // ping.
  base::Time LastActivePingDay(const std::string& extension_id) const;
  void SetLastActivePingDay(const std::string& extension_id,
                            const base::Time& time);

  // A bit we use for determining if we should send the "days since active"
  // ping. A value of true means the item has been active (launched) since the
  // last update check.
  bool GetActiveBit(const std::string& extension_id) const;
  void SetActiveBit(const std::string& extension_id, bool active);

  // Returns the granted permission set for the extension with |extension_id|,
  // and NULL if no preferences were found for |extension_id|.
  // This passes ownership of the returned set to the caller.
  std::unique_ptr<PermissionSet> GetGrantedPermissions(
      const std::string& extension_id) const;

  // Adds |permissions| to the granted permissions set for the extension with
  // |extension_id|. The new granted permissions set will be the union of
  // |permissions| and the already granted permissions.
  void AddGrantedPermissions(const std::string& extension_id,
                             const PermissionSet& permissions);

  // As above, but subtracts the given |permissions| from the granted set.
  void RemoveGrantedPermissions(const std::string& extension_id,
                                const PermissionSet& permissions);

  // Gets the set of permissions that the extension would like to be active.
  // This should always include at least the required permissions from the
  // manifest and can include a subset of optional permissions, if the extension
  // requested and was granted them.
  // This differs from the set of permissions *actually* active on the extension
  // because the user may have withheld certain permissions, as well as because
  // of possible enterprise policy settings. Use `PermissionsData` to determine
  // the current effective permissions of an extension.
  std::unique_ptr<PermissionSet> GetDesiredActivePermissions(
      const std::string& extension_id) const;

  // Sets the desired active permissions for the given `extension_id` to
  // `permissions`.
  void SetDesiredActivePermissions(const std::string& extension_id,
                                   const PermissionSet& permissions);

  // Adds `permissions` to the set of permissions the extension desires to be
  // active.
  void AddDesiredActivePermissions(const ExtensionId& extension_id,
                                   const PermissionSet& permissions);

  // Removes `permissions` to the set of permissions the extension desires to be
  // active.
  void RemoveDesiredActivePermissions(const ExtensionId& extension_id,
                                      const PermissionSet& permissions);

  // Sets/Gets the value indicating if an extension should be granted all the
  // requested host permissions without requiring explicit runtime-granted
  // permissions from the user.
  void SetWithholdingPermissions(const ExtensionId& extension_id,
                                 bool should_withhold);
  bool GetWithholdingPermissions(const ExtensionId& extension_id) const;

  // Returns the set of runtime-granted permissions. These are permissions that
  // the user explicitly approved at runtime, rather than install time (such
  // as those granted through the permissions API or the runtime host
  // permissions feature). Note that, similar to granted permissions, this can
  // include permissions granted to the extension, even if they are not active.
  std::unique_ptr<PermissionSet> GetRuntimeGrantedPermissions(
      const ExtensionId& extension_id) const;

  // Adds to the set of runtime-granted permissions.
  void AddRuntimeGrantedPermissions(const ExtensionId& extension_id,
                                    const PermissionSet& permissions);

  // Removes from the set of runtime-granted permissions.
  void RemoveRuntimeGrantedPermissions(const ExtensionId& extension_id,
                                       const PermissionSet& permissions);

  // Records whether or not this extension is currently running.
  void SetExtensionRunning(const std::string& extension_id, bool is_running);

  // Returns whether or not this extension is marked as running. This is used to
  // restart apps across browser restarts.
  bool IsExtensionRunning(const std::string& extension_id) const;

  // Set/Get whether or not the app is active. Used to force a launch of apps
  // that don't handle onRestarted() on a restart. We can only safely do that if
  // the app was active when it was last running.
  void SetIsActive(const std::string& extension_id, bool is_active);
  bool IsActive(const std::string& extension_id) const;

  // Returns true if the user enabled this extension to be loaded in incognito
  // mode.
  //
  // IMPORTANT: you probably want to use extensions::util::IsIncognitoEnabled
  // instead of this method.
  bool IsIncognitoEnabled(const std::string& extension_id) const;
  void SetIsIncognitoEnabled(const std::string& extension_id, bool enabled);

  // Returns true if the user has chosen to allow this extension to inject
  // scripts into pages with file URLs.
  //
  // IMPORTANT: you probably want to use extensions::util::AllowFileAccess
  // instead of this method.
  bool AllowFileAccess(const std::string& extension_id) const;
  void SetAllowFileAccess(const std::string& extension_id, bool allow);
  bool HasAllowFileAccessSetting(const std::string& extension_id) const;

  // Saves ExtensionInfo for each installed extension with the path to the
  // version directory and the location. Blocklisted extensions won't be saved
  // and neither will external extensions the user has explicitly uninstalled.
  // Caller takes ownership of returned structure.
  std::unique_ptr<ExtensionsInfo> GetInstalledExtensionsInfo(
      bool include_component_extensions = false) const;

  // Returns the ExtensionInfo from the prefs for the given extension. If the
  // extension is not present, NULL is returned.
  std::unique_ptr<ExtensionInfo> GetInstalledExtensionInfo(
      const std::string& extension_id,
      bool include_component_extensions = false) const;

  // We've downloaded an updated .crx file for the extension, but are waiting
  // to install it.
  //
  // |install_flags| are a bitmask of extension::InstallFlags.
  void SetDelayedInstallInfo(const Extension* extension,
                             Extension::State initial_state,
                             int install_flags,
                             DelayReason delay_reason,
                             const syncer::StringOrdinal& page_ordinal,
                             const std::string& install_parameter,
                             const declarative_net_request::RulesetInstallPrefs&
                                 ruleset_install_prefs = {});

  // Removes any delayed install information we have for the given
  // |extension_id|. Returns true if there was info to remove; false otherwise.
  bool RemoveDelayedInstallInfo(const std::string& extension_id);

  // Update the prefs to finish the update for an extension.
  bool FinishDelayedInstallInfo(const std::string& extension_id);

  // Returns the ExtensionInfo from the prefs for delayed install information
  // for |extension_id|, if we have any. Otherwise returns NULL.
  std::unique_ptr<ExtensionInfo> GetDelayedInstallInfo(
      const std::string& extension_id) const;

  DelayReason GetDelayedInstallReason(const std::string& extension_id) const;

  // Returns information about all the extensions that have delayed install
  // information.
  std::unique_ptr<ExtensionsInfo> GetAllDelayedInstallInfo() const;

  // Returns true if there is an extension which controls the preference value
  //  for |pref_key| *and* it is specific to incognito mode.
  bool HasIncognitoPrefValue(const std::string& pref_key) const;

  // Returns the creation flags mask for the extension.
  int GetCreationFlags(const std::string& extension_id) const;

  // Returns the creation flags mask for a delayed install extension.
  int GetDelayedInstallCreationFlags(const std::string& extension_id) const;

  // Returns true if the extension was installed from the Chrome Web Store.
  bool IsFromWebStore(const std::string& extension_id) const;

  // Returns true if the extension was installed as a default app.
  bool WasInstalledByDefault(const std::string& extension_id) const;

  // Returns true if the extension was installed as an oem app.
  bool WasInstalledByOem(const std::string& extension_id) const;

  // Helper method to acquire the installation time of an extension.
  // Returns base::Time() if the installation time could not be parsed or
  // found.
  base::Time GetInstallTime(const std::string& extension_id) const;

  // Returns true if the extension should not be synced.
  bool DoNotSync(const std::string& extension_id) const;

  // Gets/sets the last launch time of an extension.
  base::Time GetLastLaunchTime(const std::string& extension_id) const;
  void SetLastLaunchTime(const std::string& extension_id,
                         const base::Time& time);

  // Clear any launch times. This is called by the browsing data remover when
  // history is cleared.
  void ClearLastLaunchTimes();

  static void RegisterProfilePrefs(user_prefs::PrefRegistrySyncable* registry);

  bool extensions_disabled() const { return extensions_disabled_; }

  // The underlying PrefService.
  PrefService* pref_service() const { return prefs_; }

  // The underlying AppSorting.
  AppSorting* app_sorting() const;

  // Schedules garbage collection of an extension's on-disk data on the next
  // start of this ExtensionService. Applies only to extensions with isolated
  // storage.
  void SetNeedsStorageGarbageCollection(bool value);
  bool NeedsStorageGarbageCollection() const;

  // Used by AppWindowGeometryCache to persist its cache. These methods
  // should not be called directly.
  const base::DictionaryValue* GetGeometryCache(
        const std::string& extension_id) const;
  void SetGeometryCache(const std::string& extension_id,
                        std::unique_ptr<base::DictionaryValue> cache);

  // Used for verification of installed extension ids. For the Set method, pass
  // null to remove the preference.
  const base::DictionaryValue* GetInstallSignature() const;
  void SetInstallSignature(const base::DictionaryValue* signature);

  // The installation parameter associated with the extension.
  std::string GetInstallParam(const std::string& extension_id) const;
  void SetInstallParam(const std::string& extension_id,
                       const std::string& install_parameter);

  // Whether the extension with the given |extension_id| needs to be synced.
  // This is set when the state (such as enabled/disabled or allowed in
  // incognito) is changed before Sync is ready.
  bool NeedsSync(const std::string& extension_id) const;
  void SetNeedsSync(const std::string& extension_id, bool needs_sync);

  // Returns false if there is no ruleset checksum corresponding to the given
  // |extension_id| and |ruleset_id|. On success, returns true and populates the
  // checksum.
  bool GetDNRStaticRulesetChecksum(
      const ExtensionId& extension_id,
      declarative_net_request::RulesetID ruleset_id,
      int* checksum) const;
  void SetDNRStaticRulesetChecksum(
      const ExtensionId& extension_id,
      declarative_net_request::RulesetID ruleset_id,
      int checksum);

  // Returns false if there is no dynamic ruleset corresponding to
  // |extension_id|. On success, returns true and populates the checksum.
  bool GetDNRDynamicRulesetChecksum(const ExtensionId& extension_id,
                                    int* checksum) const;
  void SetDNRDynamicRulesetChecksum(const ExtensionId& extension_id,
                                    int checksum);

  // Returns the set of enabled static ruleset IDs or absl::nullopt if the
  // extension hasn't updated the set of enabled static rulesets.
  absl::optional<std::set<declarative_net_request::RulesetID>>
  GetDNREnabledStaticRulesets(const ExtensionId& extension_id) const;
  // Updates the set of enabled static rulesets for the |extension_id|. This
  // preference gets cleared on extension update.
  void SetDNREnabledStaticRulesets(
      const ExtensionId& extension_id,
      const std::set<declarative_net_request::RulesetID>& ids);

  // Whether the extension with the given |extension_id| is using its ruleset's
  // matched action count for the badge text. This is set via the
  // setExtensionActionOptions API call.
  bool GetDNRUseActionCountAsBadgeText(const ExtensionId& extension_id) const;
  void SetDNRUseActionCountAsBadgeText(const ExtensionId& extension_id,
                                       bool use_action_count_as_badge_text);

  // Whether the ruleset for the given |extension_id| and |ruleset_id| should be
  // ignored while loading the extension.
  bool ShouldIgnoreDNRRuleset(
      const ExtensionId& extension_id,
      declarative_net_request::RulesetID ruleset_id) const;

  // Returns the global rule allocation for the given |extension_id|. If no
  // rules are allocated to the extension, false is returned.
  bool GetDNRAllocatedGlobalRuleCount(const ExtensionId& extension_id,
                                      size_t* rule_count) const;
  void SetDNRAllocatedGlobalRuleCount(const ExtensionId& extension_id,
                                      size_t rule_count);

  // Whether the extension with the given |extension_id| should have its excess
  // global rules allocation kept during its next load.
  bool GetDNRKeepExcessAllocation(const ExtensionId& extension_id) const;
  void SetDNRKeepExcessAllocation(const ExtensionId& extension_id,
                                  bool keep_excess_allocation);

  // Migrates the disable reasons extension pref for extensions that were
  // disabled due to a deprecated reason.
  // TODO(archanasimha): Remove this around M89.
  void MigrateDeprecatedDisableReasons();

  // Looks to see if the Youtube extension is installed, and removes the
  // FROM_BOOKMARK flag from it's creation flags.
  // TODO(dmurph): Remove this in m90.
  void MigrateYoutubeOffBookmarkApps();

  // Iterates over the extension pref entries and removes any obsolete keys. We
  // need to do this here specially (rather than in
  // MigrateObsoleteProfilePrefs()) because these entries are subkeys of the
  // extension's dictionary, which is keyed on the extension ID.
  void MigrateObsoleteExtensionPrefs();

  // Updates an extension to use the new withholding pref key if it doesn't have
  // it yet, removing the old key in the process.
  // TODO(tjudkins): Remove this and the obsolete key in M83.
  void MigrateToNewWithholdingPref();

  // Migrates to the new way of recording explicit user uninstalls of external
  // extensions (by using a list of IDs rather than a bit set in each extension
  // dictionary).
  // TODO(devlin): Remove this once clients are migrated over, around M84.
  void MigrateToNewExternalUninstallPref();

  // Returns true if the given component extension should be installed, even
  // though it has been obsoleted. Installing it allows us to ensure it is
  // cleaned/deleted up properly. After that cleanup is done, this will return
  // false.
  bool ShouldInstallObsoleteComponentExtension(const std::string& extension_id);

  // Mark the given component extension as deleted. It should not be installed /
  // loaded again after this.
  void MarkObsoleteComponentExtensionAsRemoved(
      const std::string& extension_id,
      const mojom::ManifestLocation location);

  // When called before the ExtensionService is created, alerts that are
  // normally suppressed in first run will still trigger.
  static void SetRunAlertsInFirstRunForTest();

  void ClearExternalUninstallForTesting(const ExtensionId& id);

  static const char kFakeObsoletePrefForTesting[];

 private:
  friend class ExtensionPrefsBlocklistedExtensions;  // Unit test.
  friend class ExtensionPrefsComponentExtension;     // Unit test.
  friend class ExtensionPrefsUninstallExtension;     // Unit test.
  friend class
      ExtensionPrefsBitMapPrefValueClearedIfEqualsDefaultValue;  // Unit test.

  // See the Create methods.
  ExtensionPrefs(
      content::BrowserContext* browser_context,
      PrefService* prefs,
      const base::FilePath& root_dir,
      ExtensionPrefValueMap* extension_pref_value_map,
      base::Clock* clock,
      bool extensions_disabled,
      const std::vector<EarlyExtensionPrefsObserver*>& early_observers);

  // Sets profile wide ExtensionPrefs.
  void SetPref(const PrefMap& pref, std::unique_ptr<base::Value> value);

  // Updates ExtensionPrefs for a specific extension.
  void UpdateExtensionPref(const std::string& id,
                           const PrefMap& pref,
                           std::unique_ptr<base::Value> value);

  // Converts absolute paths in the pref to paths relative to the
  // install_directory_.
  void MakePathsRelative();

  // Converts internal relative paths to be absolute. Used for export to
  // consumers who expect full paths.
  void MakePathsAbsolute(base::DictionaryValue* dict);

  // Helper function used by GetInstalledExtensionInfo() and
  // GetDelayedInstallInfo() to construct an ExtensionInfo from the provided
  // |extension| dictionary.
  std::unique_ptr<ExtensionInfo> GetInstalledInfoHelper(
      const std::string& extension_id,
      const base::Value::Dict& extension,
      bool include_component_extensions) const;

  // Read the boolean preference entry and return true if the preference exists
  // and the preference's value is true; false otherwise.
  bool ReadPrefAsBooleanAndReturn(const std::string& extension_id,
                                  base::StringPiece pref_key) const;

  // Interprets |pref_key| in |extension_id|'s preferences as an
  // PermissionSet, and passes ownership of the set to the caller.
  std::unique_ptr<PermissionSet> ReadPrefAsPermissionSet(
      const std::string& extension_id,
      base::StringPiece pref_key) const;

  // Converts the |new_value| to its value and sets the |pref_key| pref
  // belonging to |extension_id|.
  void SetExtensionPrefPermissionSet(const std::string& extension_id,
                                     base::StringPiece pref_key,
                                     const PermissionSet& new_value);

  // Common implementation to add permissions to a stored permission set.
  void AddToPrefPermissionSet(const ExtensionId& extension_id,
                              const PermissionSet& permissions,
                              const char* pref_name);

  // Common implementation to remove permissions from a stored permission set.
  void RemoveFromPrefPermissionSet(const ExtensionId& extension_id,
                                   const PermissionSet& permissions,
                                   const char* pref_name);

  // Returns an immutable dictionary for extension |id|'s prefs, or NULL if it
  // doesn't exist.
  const base::DictionaryValue* GetExtensionPref(const std::string& id) const;

  // Returns an immutable base::Value for extension |id|'s prefs, or nullptr if
  // it doesn't exist.
  const base::Value* GetPrefAsValue(const std::string& extension_id,
                                    base::StringPiece pref_key) const;

  // Modifies the extensions disable reasons to add a new reason, remove an
  // existing reason, or clear all reasons. Notifies observers if the set of
  // DisableReasons has changed.
  // If |operation| is BIT_MAP_PREF_CLEAR, then |reasons| are ignored.
  void ModifyDisableReasons(const std::string& extension_id,
                            int reasons,
                            BitMapPrefOperation operation);

  // Installs the persistent extension preferences into |prefs_|'s extension
  // pref store. Does nothing if extensions_disabled_ is true.
  void InitPrefStore();

  // Checks whether there is a state pref for the extension and if so, whether
  // it matches |check_state|.
  bool DoesExtensionHaveState(const std::string& id,
                              Extension::State check_state) const;

  // Reads the list of strings for |pref| from user prefs into
  // |id_container_out|. Returns false if the pref wasn't found in the user
  // pref store.
  template <class ExtensionIdContainer>
  bool GetUserExtensionPrefIntoContainer(
      const char* pref,
      ExtensionIdContainer* id_container_out) const;

  // Writes the list of strings contained in |strings| to |pref| in prefs.
  template <class ExtensionIdContainer>
  void SetExtensionPrefFromContainer(const char* pref,
                                     const ExtensionIdContainer& strings);

  // Helper function to populate |extension_dict| with the values needed
  // by a newly installed extension. Work is broken up between this
  // function and FinishExtensionInfoPrefs() to accommodate delayed
  // installations.
  //
  // |install_flags| are a bitmask of extension::InstallFlags.
  void PopulateExtensionInfoPrefs(
      const Extension* extension,
      const base::Time install_time,
      Extension::State initial_state,
      int install_flags,
      const std::string& install_parameter,
      const declarative_net_request::RulesetInstallPrefs& ruleset_install_prefs,
      prefs::DictionaryValueUpdate* extension_dict);

  void InitExtensionControlledPrefs(const ExtensionsInfo& extensions_info);

  // Loads preferences for the given |extension_id| into the pref value map.
  void LoadExtensionControlledPrefs(const ExtensionId& extension_id,
                                    ExtensionPrefsScope scope);

  // Helper function to complete initialization of the values in
  // |extension_dict| for an extension install. Also see
  // PopulateExtensionInfoPrefs().
  void FinishExtensionInfoPrefs(
      const std::string& extension_id,
      const base::Time install_time,
      bool needs_sort_ordinal,
      const syncer::StringOrdinal& suggested_page_ordinal,
      prefs::DictionaryValueUpdate* extension_dict);

  // Returns true if the prefs have any permission withholding setting stored
  // for a given extension.
  bool HasWithholdingPermissionsSetting(const ExtensionId& extension_id) const;

  // Clears the bit indicating that an external extension was uninstalled.
  void ClearExternalUninstallBit(const ExtensionId& extension_id);

  raw_ptr<content::BrowserContext> browser_context_;

  // The pref service specific to this set of extension prefs. Owned by the
  // BrowserContext.
  raw_ptr<PrefService> prefs_;

  // Base extensions install directory.
  base::FilePath install_directory_;

  // Weak pointer, owned by BrowserContext.
  raw_ptr<ExtensionPrefValueMap> extension_pref_value_map_;

  raw_ptr<base::Clock> clock_;

  bool extensions_disabled_;

  base::ObserverList<ExtensionPrefsObserver>::Unchecked observer_list_;
};

}  // namespace extensions

#endif  // EXTENSIONS_BROWSER_EXTENSION_PREFS_H_
