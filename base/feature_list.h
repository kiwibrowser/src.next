// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_FEATURE_LIST_H_
#define BASE_FEATURE_LIST_H_

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/base_export.h"
#include "base/containers/flat_map.h"
#include "base/dcheck_is_on.h"
#include "base/feature_list_buildflags.h"
#include "base/gtest_prod_util.h"
#include "base/logging.h"
#include "base/memory/raw_ptr.h"
#include "base/strings/string_piece.h"
#include "base/synchronization/lock.h"
#include "third_party/abseil-cpp/absl/types/optional.h"

namespace base {

class FieldTrial;
class FieldTrialList;
class PersistentMemoryAllocator;

// Specifies whether a given feature is enabled or disabled by default.
// NOTE: The actual runtime state may be different, due to a field trial or a
// command line switch.
enum FeatureState {
  FEATURE_DISABLED_BY_DEFAULT,
  FEATURE_ENABLED_BY_DEFAULT,
};

// The Feature struct is used to define the default state for a feature. See
// comment below for more details. There must only ever be one struct instance
// for a given feature name - generally defined as a constant global variable or
// file static. It should never be used as a constexpr as it breaks
// pointer-based identity lookup.
// Note: New code should use CONSTINIT on the base::Feature declaration.
struct BASE_EXPORT Feature {
  constexpr Feature(const char* name, FeatureState default_state)
      : name(name), default_state(default_state) {
#if BUILDFLAG(ENABLE_BANNED_BASE_FEATURE_PREFIX)
    if (StringPiece(name).find(BUILDFLAG(BANNED_BASE_FEATURE_PREFIX)) == 0) {
      LOG(FATAL) << "Invalid feature name " << name << " starts with "
                 << BUILDFLAG(BANNED_BASE_FEATURE_PREFIX);
    }
#endif  // BUILDFLAG(ENABLE_BANNED_BASE_FEATURE_PREFIX)
  }
  // The name of the feature. This should be unique to each feature and is used
  // for enabling/disabling features via command line flags and experiments.
  // It is strongly recommended to use CamelCase style for feature names, e.g.
  // "MyGreatFeature".
  const char* const name;

  // The default state (i.e. enabled or disabled) for this feature.
  // NOTE: The actual runtime state may be different, due to a field trial or a
  // command line switch.
  const FeatureState default_state;
};

#if defined(DCHECK_IS_CONFIGURABLE)
// DCHECKs have been built-in, and are configurable at run-time to be fatal, or
// not, via a DcheckIsFatal feature. We define the Feature here since it is
// checked in FeatureList::SetInstance(). See https://crbug.com/596231.
extern BASE_EXPORT const Feature kDCheckIsFatalFeature;
#endif  // defined(DCHECK_IS_CONFIGURABLE)

// The FeatureList class is used to determine whether a given feature is on or
// off. It provides an authoritative answer, taking into account command-line
// overrides and experimental control.
//
// The basic use case is for any feature that can be toggled (e.g. through
// command-line or an experiment) to have a defined Feature struct, e.g.:
//
//   const base::Feature kMyGreatFeature {
//     "MyGreatFeature", base::FEATURE_ENABLED_BY_DEFAULT
//   };
//
// Then, client code that wishes to query the state of the feature would check:
//
//   if (base::FeatureList::IsEnabled(kMyGreatFeature)) {
//     // Feature code goes here.
//   }
//
// Behind the scenes, the above call would take into account any command-line
// flags to enable or disable the feature, any experiments that may control it
// and finally its default state (in that order of priority), to determine
// whether the feature is on.
//
// Features can be explicitly forced on or off by specifying a list of comma-
// separated feature names via the following command-line flags:
//
//   --enable-features=Feature5,Feature7
//   --disable-features=Feature1,Feature2,Feature3
//
// To enable/disable features in a test, do NOT append --enable-features or
// --disable-features to the command-line directly. Instead, use
// ScopedFeatureList. See base/test/scoped_feature_list.h for details.
//
// After initialization (which should be done single-threaded), the FeatureList
// API is thread safe.
//
// Note: This class is a singleton, but does not use base/memory/singleton.h in
// order to have control over its initialization sequence. Specifically, the
// intended use is to create an instance of this class and fully initialize it,
// before setting it as the singleton for a process, via SetInstance().
class BASE_EXPORT FeatureList {
 public:
  FeatureList();
  FeatureList(const FeatureList&) = delete;
  FeatureList& operator=(const FeatureList&) = delete;
  ~FeatureList();

  // Used by common test fixture classes to prevent abuse of ScopedFeatureList
  // after multiple threads have started.
  class BASE_EXPORT ScopedDisallowOverrides {
   public:
    explicit ScopedDisallowOverrides(const char* reason);
    ScopedDisallowOverrides(const ScopedDisallowOverrides&) = delete;
    ScopedDisallowOverrides& operator=(const ScopedDisallowOverrides&) = delete;
    ~ScopedDisallowOverrides();

   private:
#if DCHECK_IS_ON()
    const char* const previous_reason_;
#endif
  };

  // Specifies whether a feature override enables or disables the feature.
  enum OverrideState {
    OVERRIDE_USE_DEFAULT,
    OVERRIDE_DISABLE_FEATURE,
    OVERRIDE_ENABLE_FEATURE,
  };

  // Accessor class, used to look up features by _name_ rather than by Feature
  // object.
  // Should only be used in limited cases. See ConstructAccessor() for details.
  class BASE_EXPORT Accessor {
   public:
    Accessor(const Accessor&) = delete;
    Accessor& operator=(const Accessor&) = delete;

    // Looks up the feature, returning only its override state, rather than
    // falling back on a default value (since there is no default value given).
    // Callers of this MUST ensure that there is a consistent, compile-time
    // default value associated.
    FeatureList::OverrideState GetOverrideStateByFeatureName(
        StringPiece feature_name);

    // Look up the feature, and, if present, populate |params|.
    // See GetFieldTrialParams in field_trial_params.h for more documentation.
    bool GetParamsByFeatureName(StringPiece feature_name,
                                std::map<std::string, std::string>* params);

   private:
    // Allow FeatureList to construct this class.
    friend class FeatureList;

    explicit Accessor(FeatureList* feature_list);

    // Unowned pointer to the FeatureList object we use to look up feature
    // enablement.
    raw_ptr<FeatureList> feature_list_;
  };

  // Describes a feature override. The first member is a Feature that will be
  // overridden with the state given by the second member.
  using FeatureOverrideInfo =
      std::pair<const std::reference_wrapper<const Feature>, OverrideState>;

  // Initializes feature overrides via command-line flags `--enable-features=`
  // and `--disable-features=`, each of which is a comma-separated list of
  // features to enable or disable, respectively. This function also allows
  // users to set a feature's field trial params via `--enable-features=`. Must
  // only be invoked during the initialization phase (before
  // FinalizeInitialization() has been called).
  //
  // If a feature appears on both lists, then it will be disabled. If
  // a list entry has the format "FeatureName<TrialName" then this
  // initialization will also associate the feature state override with the
  // named field trial, if it exists. If a list entry has the format
  // "FeatureName:k1/v1/k2/v2", "FeatureName<TrialName:k1/v1/k2/v2" or
  // "FeatureName<TrialName.GroupName:k1/v1/k2/v2" then this initialization will
  // also associate the feature state override with the named field trial and
  // its params. If the feature params part is provided but trial and/or group
  // isn't, this initialization will also create a synthetic trial, named
  // "Study" followed by the feature name, i.e. "StudyFeature", and group, named
  // "Group" followed by the feature name, i.e. "GroupFeature", for the params.
  // If a feature name is prefixed with the '*' character, it will be created
  // with OVERRIDE_USE_DEFAULT - which is useful for associating with a trial
  // while using the default state.
  void InitializeFromCommandLine(const std::string& enable_features,
                                 const std::string& disable_features);

  // Initializes feature overrides through the field trial allocator, which
  // we're using to store the feature names, their override state, and the name
  // of the associated field trial.
  void InitializeFromSharedMemory(PersistentMemoryAllocator* allocator);

  // Returns true if the state of |feature_name| has been overridden (regardless
  // of whether the overridden value is the same as the default value) for any
  // reason (e.g. command line or field trial).
  bool IsFeatureOverridden(const std::string& feature_name) const;

  // Returns true if the state of |feature_name| has been overridden via
  // |InitializeFromCommandLine()|. This includes features explicitly
  // disabled/enabled with --disable-features and --enable-features, as well as
  // any extra feature overrides that depend on command line switches.
  bool IsFeatureOverriddenFromCommandLine(
      const std::string& feature_name) const;

  // Returns true if the state |feature_name| has been overridden by
  // |InitializeFromCommandLine()| and the state matches |state|.
  bool IsFeatureOverriddenFromCommandLine(const std::string& feature_name,
                                          OverrideState state) const;

  // Associates a field trial for reporting purposes corresponding to the
  // command-line setting the feature state to |for_overridden_state|. The trial
  // will be activated when the state of the feature is first queried. This
  // should be called during registration, after InitializeFromCommandLine() has
  // been called but before the instance is registered via SetInstance().
  void AssociateReportingFieldTrial(const std::string& feature_name,
                                    OverrideState for_overridden_state,
                                    FieldTrial* field_trial);

  // Registers a field trial to override the enabled state of the specified
  // feature to |override_state|. Command-line overrides still take precedence
  // over field trials, so this will have no effect if the feature is being
  // overridden from the command-line. The associated field trial will be
  // activated when the feature state for this feature is queried. This should
  // be called during registration, after InitializeFromCommandLine() has been
  // called but before the instance is registered via SetInstance().
  void RegisterFieldTrialOverride(const std::string& feature_name,
                                  OverrideState override_state,
                                  FieldTrial* field_trial);

  // Adds extra overrides (not associated with a field trial). Should be called
  // before SetInstance().
  // The ordering of calls with respect to InitializeFromCommandLine(),
  // RegisterFieldTrialOverride(), etc. matters. The first call wins out,
  // because the |overrides_| map uses insert(), which retains the first
  // inserted entry and does not overwrite it on subsequent calls to insert().
  void RegisterExtraFeatureOverrides(
      const std::vector<FeatureOverrideInfo>& extra_overrides);

  // Loops through feature overrides and serializes them all into |allocator|.
  void AddFeaturesToAllocator(PersistentMemoryAllocator* allocator);

  // Returns comma-separated lists of feature names (in the same format that is
  // accepted by InitializeFromCommandLine()) corresponding to features that
  // have been overridden - either through command-line or via FieldTrials. For
  // those features that have an associated FieldTrial, the output entry will be
  // of the format "FeatureName<TrialName", where "TrialName" is the name of the
  // FieldTrial. Features that have overrides with OVERRIDE_USE_DEFAULT will be
  // added to |enable_overrides| with a '*' character prefix. Must be called
  // only after the instance has been initialized and registered.
  void GetFeatureOverrides(std::string* enable_overrides,
                           std::string* disable_overrides) const;

  // Like GetFeatureOverrides(), but only returns overrides that were specified
  // explicitly on the command-line, omitting the ones from field trials.
  void GetCommandLineFeatureOverrides(std::string* enable_overrides,
                                      std::string* disable_overrides) const;

  // Returns the field trial associated with the given feature |name|. Used for
  // getting the FieldTrial without requiring a struct Feature.
  base::FieldTrial* GetAssociatedFieldTrialByFeatureName(
      StringPiece name) const;

  // Get associated field trial for the given feature |name| only if override
  // enables it.
  FieldTrial* GetEnabledFieldTrialByFeatureName(StringPiece name) const;

  // Construct an accessor allowing access to GetOverrideStateByFeatureName().
  // This can only be called before the FeatureList is initialized, and is
  // intended for very narrow use.
  // If you're tempted to use it, do so only in consultation with feature_list
  // OWNERS.
  std::unique_ptr<Accessor> ConstructAccessor();

  // Returns whether the given |feature| is enabled. Must only be called after
  // the singleton instance has been registered via SetInstance(). Additionally,
  // a feature with a given name must only have a single corresponding Feature
  // struct, which is checked in builds with DCHECKs enabled.
  static bool IsEnabled(const Feature& feature);

  // If the given |feature| is overridden, returns its enabled state; otherwise,
  // returns an empty optional. Must only be called after the singleton instance
  // has been registered via SetInstance(). Additionally, a feature with a given
  // name must only have a single corresponding Feature struct, which is checked
  // in builds with DCHECKs enabled.
  static absl::optional<bool> GetStateIfOverridden(const Feature& feature);

  // Returns the field trial associated with the given |feature|. Must only be
  // called after the singleton instance has been registered via SetInstance().
  static FieldTrial* GetFieldTrial(const Feature& feature);

  // Splits a comma-separated string containing feature names into a vector. The
  // resulting pieces point to parts of |input|.
  static std::vector<base::StringPiece> SplitFeatureListString(
      base::StringPiece input);

  // Initializes and sets an instance of FeatureList with feature overrides via
  // command-line flags |enable_features| and |disable_features| if one has not
  // already been set from command-line flags. Returns true if an instance did
  // not previously exist. See InitializeFromCommandLine() for more details
  // about |enable_features| and |disable_features| parameters.
  static bool InitializeInstance(const std::string& enable_features,
                                 const std::string& disable_features);

  // Like the above, but also adds extra overrides. If a feature appears in
  // |extra_overrides| and also |enable_features| or |disable_features|, the
  // disable/enable will supersede the extra overrides.
  static bool InitializeInstance(
      const std::string& enable_features,
      const std::string& disable_features,
      const std::vector<FeatureOverrideInfo>& extra_overrides);

  // Returns the singleton instance of FeatureList. Will return null until an
  // instance is registered via SetInstance().
  static FeatureList* GetInstance();

  // Registers the given |instance| to be the singleton feature list for this
  // process. This should only be called once and |instance| must not be null.
  // Note: If you are considering using this for the purposes of testing, take
  // a look at using base/test/scoped_feature_list.h instead.
  static void SetInstance(std::unique_ptr<FeatureList> instance);

  // Clears the previously-registered singleton instance for tests and returns
  // the old instance.
  // Note: Most tests should never call this directly. Instead consider using
  // base::test::ScopedFeatureList.
  static std::unique_ptr<FeatureList> ClearInstanceForTesting();

  // Sets a given (initialized) |instance| to be the singleton feature list,
  // for testing. Existing instance must be null. This is primarily intended
  // to support base::test::ScopedFeatureList helper class.
  static void RestoreInstanceForTesting(std::unique_ptr<FeatureList> instance);

  // On some platforms, the base::FeatureList singleton might be duplicated to
  // more than one module. If this function is called, then using base::Feature
  // API will result in DCHECK if accessed from the same module as the callee.
  // Has no effect if DCHECKs are not enabled.
  static void ForbidUseForCurrentModule();

 private:
  FRIEND_TEST_ALL_PREFIXES(FeatureListTest, CheckFeatureIdentity);
  FRIEND_TEST_ALL_PREFIXES(FeatureListTest,
                           StoreAndRetrieveFeaturesFromSharedMemory);
  FRIEND_TEST_ALL_PREFIXES(FeatureListTest,
                           StoreAndRetrieveAssociatedFeaturesFromSharedMemory);
  // Allow Accessor to access GetOverrideStateByFeatureName().
  friend class Accessor;

  struct OverrideEntry {
    // The overridden enable (on/off) state of the feature.
    OverrideState overridden_state;

    // An optional associated field trial, which will be activated when the
    // state of the feature is queried for the first time. Weak pointer to the
    // FieldTrial object that is owned by the FieldTrialList singleton.
    base::FieldTrial* field_trial;

    // Specifies whether the feature's state is overridden by |field_trial|.
    // If it's not, and |field_trial| is not null, it means it is simply an
    // associated field trial for reporting purposes (and |overridden_state|
    // came from the command-line).
    bool overridden_by_field_trial;

    // TODO(asvitkine): Expand this as more support is added.

    // Constructs an OverrideEntry for the given |overridden_state|. If
    // |field_trial| is not null, it implies that |overridden_state| comes from
    // the trial, so |overridden_by_field_trial| will be set to true.
    OverrideEntry(OverrideState overridden_state, FieldTrial* field_trial);
  };

  // Returns the override for the field trial associated with the given feature
  // |name| or null if the feature is not found.
  const base::FeatureList::OverrideEntry* GetOverrideEntryByFeatureName(
      StringPiece name) const;

  // Finalizes the initialization state of the FeatureList, so that no further
  // overrides can be registered. This is called by SetInstance() on the
  // singleton feature list that is being registered.
  void FinalizeInitialization();

  // Returns whether the given |feature| is enabled. This is invoked by the
  // public FeatureList::IsEnabled() static function on the global singleton.
  // Requires the FeatureList to have already been fully initialized.
  bool IsFeatureEnabled(const Feature& feature) const;

  // Returns whether the given |feature| is enabled. This is invoked by the
  // public FeatureList::GetStateIfOverridden() static function on the global
  // singleton. Requires the FeatureList to have already been fully initialized.
  absl::optional<bool> IsFeatureEnabledIfOverridden(
      const Feature& feature) const;

  // Returns the override state of a given |feature|. If the feature was not
  // overridden, returns OVERRIDE_USE_DEFAULT. Performs any necessary callbacks
  // for when the feature state has been observed, e.g. activating field trials.
  OverrideState GetOverrideState(const Feature& feature) const;

  // Same as GetOverrideState(), but without a default value.
  OverrideState GetOverrideStateByFeatureName(StringPiece feature_name) const;

  // Returns the field trial associated with the given |feature|. This is
  // invoked by the public FeatureList::GetFieldTrial() static function on the
  // global singleton. Requires the FeatureList to have already been fully
  // initialized.
  base::FieldTrial* GetAssociatedFieldTrial(const Feature& feature) const;

  // For each feature name in comma-separated list of strings |feature_list|,
  // registers an override with the specified |overridden_state|. Also, will
  // associate an optional named field trial if the entry is of the format
  // "FeatureName<TrialName".
  void RegisterOverridesFromCommandLine(const std::string& feature_list,
                                        OverrideState overridden_state);

  // Registers an override for feature |feature_name|. The override specifies
  // whether the feature should be on or off (via |overridden_state|), which
  // will take precedence over the feature's default state. If |field_trial| is
  // not null, registers the specified field trial object to be associated with
  // the feature, which will activate the field trial when the feature state is
  // queried. If an override is already registered for the given feature, it
  // will not be changed.
  void RegisterOverride(StringPiece feature_name,
                        OverrideState overridden_state,
                        FieldTrial* field_trial);

  // Implementation of GetFeatureOverrides() with a parameter that specifies
  // whether only command-line enabled overrides should be emitted. See that
  // function's comments for more details.
  void GetFeatureOverridesImpl(std::string* enable_overrides,
                               std::string* disable_overrides,
                               bool command_line_only) const;

  // Verifies that there's only a single definition of a Feature struct for a
  // given feature name. Keeps track of the first seen Feature struct for each
  // feature. Returns false when called on a Feature struct with a different
  // address than the first one it saw for that feature name. Used only from
  // DCHECKs and tests. This is const because it's called from const getters and
  // doesn't modify externally visible state.
  bool CheckFeatureIdentity(const Feature& feature) const;

  // Map from feature name to an OverrideEntry struct for the feature, if it
  // exists.
  base::flat_map<std::string, OverrideEntry> overrides_;

  // Locked map that keeps track of seen features, to ensure a single feature is
  // only defined once. This verification is only done in builds with DCHECKs
  // enabled. This is mutable as it's not externally visible and needs to be
  // usable from const getters.
  mutable Lock feature_identity_tracker_lock_;
  mutable std::map<std::string, const Feature*> feature_identity_tracker_
      GUARDED_BY(feature_identity_tracker_lock_);

  // Tracks the associated FieldTrialList for DCHECKs. This is used to catch
  // the scenario where multiple FieldTrialList are used with the same
  // FeatureList - which can lead to overrides pointing to invalid FieldTrial
  // objects.
  raw_ptr<base::FieldTrialList> field_trial_list_ = nullptr;

  // Whether this object has been fully initialized. This gets set to true as a
  // result of FinalizeInitialization().
  bool initialized_ = false;

  // Whether this object has been initialized from command line.
  bool initialized_from_command_line_ = false;
};

}  // namespace base

#endif  // BASE_FEATURE_LIST_H_
