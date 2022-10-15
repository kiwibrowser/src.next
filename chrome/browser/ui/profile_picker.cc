// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/profile_picker.h"

#include <algorithm>
#include <string>

#include "base/containers/flat_set.h"
#include "base/metrics/histogram_functions.h"
#include "base/time/time.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile_attributes_entry.h"
#include "chrome/browser/profiles/profile_attributes_storage.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/profiles/profiles_state.h"
#include "chrome/browser/signin/signin_util.h"
#include "chrome/browser/ui/ui_features.h"
#include "chrome/common/pref_names.h"
#include "chrome/common/webui_url_constants.h"
#include "components/prefs/pref_service.h"

namespace {

constexpr base::TimeDelta kActiveTimeThreshold = base::Days(28);

ProfilePicker::AvailabilityOnStartup GetAvailabilityOnStartup() {
  int availability_on_startup = g_browser_process->local_state()->GetInteger(
      prefs::kBrowserProfilePickerAvailabilityOnStartup);
  switch (availability_on_startup) {
    case 0:
      return ProfilePicker::AvailabilityOnStartup::kEnabled;
    case 1:
      return ProfilePicker::AvailabilityOnStartup::kDisabled;
    case 2:
      return ProfilePicker::AvailabilityOnStartup::kForced;
    default:
      NOTREACHED();
  }
  return ProfilePicker::AvailabilityOnStartup::kEnabled;
}

}  // namespace

const char ProfilePicker::kTaskManagerUrl[] =
    "chrome://profile-picker/task-manager";

ProfilePicker::Params::~Params() {
#if BUILDFLAG(IS_CHROMEOS_LACROS)
  NotifyAccountSelected(std::string());
  NotifyFirstRunExited(FirstRunExitStatus::kQuitEarly,
                       FirstRunExitSource::kParamDestructor,
                       base::OnceClosure());
#endif
}

ProfilePicker::Params::Params(ProfilePicker::Params&&) = default;

ProfilePicker::Params& ProfilePicker::Params::operator=(
    ProfilePicker::Params&&) = default;

// static
ProfilePicker::Params ProfilePicker::Params::FromEntryPoint(
    EntryPoint entry_point) {
  // Use specialized constructors when available.
  DCHECK_NE(entry_point, EntryPoint::kBackgroundModeManager);
  DCHECK_NE(entry_point, EntryPoint::kLacrosSelectAvailableAccount);
  DCHECK_NE(entry_point, EntryPoint::kLacrosPrimaryProfileFirstRun);
  return ProfilePicker::Params(entry_point, GetPickerProfilePath());
}

// static
ProfilePicker::Params ProfilePicker::Params::ForBackgroundManager(
    const GURL& on_select_profile_target_url) {
  Params params(EntryPoint::kBackgroundModeManager, GetPickerProfilePath());
  params.on_select_profile_target_url_ = on_select_profile_target_url;
  return params;
}

#if BUILDFLAG(IS_CHROMEOS_LACROS)
// static
ProfilePicker::Params ProfilePicker::Params::ForLacrosSelectAvailableAccount(
    const base::FilePath& profile_path,
    base::OnceCallback<void(const std::string&)> account_selected_callback) {
  Params params(EntryPoint::kLacrosSelectAvailableAccount,
                profile_path.empty() ? GetPickerProfilePath() : profile_path);
  params.account_selected_callback_ = std::move(account_selected_callback);
  return params;
}

// static
ProfilePicker::Params ProfilePicker::Params::ForLacrosPrimaryProfileFirstRun(
    FirstRunExitedCallback first_run_finished_callback) {
  Params params(EntryPoint::kLacrosPrimaryProfileFirstRun,
                ProfileManager::GetPrimaryUserProfilePath());
  params.first_run_exited_callback_ = std::move(first_run_finished_callback);
  return params;
}

void ProfilePicker::Params::NotifyAccountSelected(const std::string& gaia_id) {
  if (account_selected_callback_)
    std::move(account_selected_callback_).Run(gaia_id);
}

void ProfilePicker::Params::NotifyFirstRunExited(
    FirstRunExitStatus exit_status,
    FirstRunExitSource exit_source,
    base::OnceClosure maybe_callback) {
  if (!first_run_exited_callback_)
    return;

  LOG(ERROR) << "Notifying FirstRun exit with status="
             << static_cast<int>(exit_status)
             << " from source=" << static_cast<int>(exit_source);

  std::move(first_run_exited_callback_)
      .Run(exit_status, std::move(maybe_callback));
}
#endif

GURL ProfilePicker::Params::GetInitialURL() const {
  GURL base_url = GURL(chrome::kChromeUIProfilePickerUrl);
  switch (entry_point_) {
    case ProfilePicker::EntryPoint::kOnStartup: {
      GURL::Replacements replacements;
      replacements.SetQueryStr(chrome::kChromeUIProfilePickerStartupQuery);
      return base_url.ReplaceComponents(replacements);
    }
    case ProfilePicker::EntryPoint::kProfileMenuManageProfiles:
    case ProfilePicker::EntryPoint::kOpenNewWindowAfterProfileDeletion:
    case ProfilePicker::EntryPoint::kNewSessionOnExistingProcess:
    case ProfilePicker::EntryPoint::kProfileLocked:
    case ProfilePicker::EntryPoint::kUnableToCreateBrowser:
    case ProfilePicker::EntryPoint::kBackgroundModeManager:
    case ProfilePicker::EntryPoint::kProfileIdle:
      return base_url;
    case ProfilePicker::EntryPoint::kProfileMenuAddNewProfile:
      return base_url.Resolve("new-profile");
    case ProfilePicker::EntryPoint::kLacrosSelectAvailableAccount:
      return base_url.Resolve("account-selection-lacros");
    case ProfilePicker::EntryPoint::kLacrosPrimaryProfileFirstRun:
      // No web UI should be displayed initially.
      NOTREACHED();
      return GURL();
  }
}

bool ProfilePicker::Params::CanReusePickerWindow(const Params& other) const {
#if BUILDFLAG(IS_CHROMEOS_LACROS)
  LOG(WARNING) << "Checking window reusability from entry point "
               << static_cast<int>(entry_point_) << " to "
               << static_cast<int>(other.entry_point());

  // Some entry points have specific UIs that cannot be reused for other entry
  // points.
  base::flat_set<EntryPoint> exclusive_entry_points = {
      EntryPoint::kLacrosPrimaryProfileFirstRun,
      EntryPoint::kLacrosSelectAvailableAccount};
  if (entry_point_ != other.entry_point_ &&
      (exclusive_entry_points.contains(entry_point_) ||
       exclusive_entry_points.contains(other.entry_point_))) {
    return false;
  }
  return profile_path_ == other.profile_path_;
#else
  DCHECK_EQ(profile_path_, other.profile_path_);
  return true;
#endif
}

ProfilePicker::Params::Params(EntryPoint entry_point,
                              const base::FilePath& profile_path)
    : entry_point_(entry_point), profile_path_(profile_path) {}

// static
bool ProfilePicker::Shown() {
  PrefService* prefs = g_browser_process->local_state();
  DCHECK(prefs);
  return prefs->GetBoolean(prefs::kBrowserProfilePickerShown);
}

// static
bool ProfilePicker::ShouldShowAtLaunch() {
  AvailabilityOnStartup availability_on_startup = GetAvailabilityOnStartup();

  if (availability_on_startup == AvailabilityOnStartup::kDisabled)
    return false;

  // TODO (crbug/1155158): Move this over the urls check (in
  // startup_browser_creator.cc) once the profile picker can forward urls
  // specified in command line.
  if (availability_on_startup == AvailabilityOnStartup::kForced)
    return true;

#if BUILDFLAG(IS_CHROMEOS_LACROS)
  // Don't show the profile picker if secondary profiles are not allowed.
  bool lacros_secondary_profiles_allowed =
      g_browser_process->local_state()->GetBoolean(
          prefs::kLacrosSecondaryProfilesAllowed);

  if (!lacros_secondary_profiles_allowed)
    return false;
#endif

  ProfileManager* profile_manager = g_browser_process->profile_manager();

  size_t number_of_profiles = profile_manager->GetNumberOfProfiles();
  // Need to consider 0 profiles as this is what happens in some browser-tests.
  if (number_of_profiles <= 1)
    return false;

  std::vector<ProfileAttributesEntry*> profile_attributes =
      profile_manager->GetProfileAttributesStorage().GetAllProfilesAttributes();
  int number_of_active_profiles =
      std::count_if(profile_attributes.begin(), profile_attributes.end(),
                    [](ProfileAttributesEntry* entry) {
                      return (base::Time::Now() - entry->GetActiveTime() <
                              kActiveTimeThreshold);
                    });
  // Don't show the profile picker at launch if the user has less than two
  // active profiles. However, if the user has already seen the profile picker
  // before, respect user's preference.
  if (number_of_active_profiles < 2 && !Shown())
    return false;

  bool pref_enabled = g_browser_process->local_state()->GetBoolean(
      prefs::kBrowserShowProfilePickerOnStartup);
  base::UmaHistogramBoolean("ProfilePicker.AskOnStartup", pref_enabled);
  return pref_enabled;
}
