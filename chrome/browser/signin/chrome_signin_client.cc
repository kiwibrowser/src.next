// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/signin/chrome_signin_client.h"

#include <stddef.h>

#include <memory>
#include <string>
#include <utility>

#include "base/bind.h"
#include "base/strings/utf_string_conversions.h"
#include "build/build_config.h"
#include "build/buildflag.h"
#include "build/chromeos_buildflags.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/content_settings/cookie_settings_factory.h"
#include "chrome/browser/content_settings/host_content_settings_map_factory.h"
#include "chrome/browser/enterprise/util/managed_browser_utils.h"
#include "chrome/browser/profiles/profile_attributes_entry.h"
#include "chrome/browser/profiles/profile_attributes_storage.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/profiles/profile_metrics.h"
#include "chrome/browser/signin/account_consistency_mode_manager.h"
#include "chrome/browser/signin/chrome_device_id_helper.h"
#include "chrome/browser/signin/force_signin_verifier.h"
#include "chrome/browser/signin/identity_manager_factory.h"
#include "chrome/browser/signin/signin_features.h"
#include "chrome/browser/signin/signin_util.h"
#include "chrome/common/buildflags.h"
#include "chrome/common/channel_info.h"
#include "chrome/common/pref_names.h"
#include "components/content_settings/core/browser/cookie_settings.h"
#include "components/metrics/metrics_service.h"
#include "components/policy/core/browser/browser_policy_connector.h"
#include "components/prefs/pref_service.h"
#include "components/signin/core/browser/cookie_settings_util.h"
#include "components/signin/public/base/signin_buildflags.h"
#include "components/signin/public/base/signin_pref_names.h"
#include "components/signin/public/identity_manager/access_token_info.h"
#include "components/signin/public/identity_manager/identity_manager.h"
#include "components/signin/public/identity_manager/scope_set.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/network_service_instance.h"
#include "content/public/browser/storage_partition.h"
#include "google_apis/gaia/gaia_constants.h"
#include "google_apis/gaia/gaia_urls.h"
#include "url/gurl.h"

#if BUILDFLAG(ENABLE_SUPERVISED_USERS)
#include "chrome/browser/supervised_user/supervised_user_constants.h"
#endif

#if BUILDFLAG(IS_CHROMEOS_ASH)
#include "chrome/browser/ash/net/delay_network_call.h"
#include "chromeos/ash/components/network/network_handler.h"
#endif

#if BUILDFLAG(IS_CHROMEOS_LACROS)
#include "chrome/browser/lacros/account_manager/account_manager_util.h"
#include "chromeos/crosapi/mojom/account_manager.mojom.h"
#include "chromeos/startup/browser_params_proxy.h"
#include "components/account_manager_core/account.h"
#include "components/account_manager_core/account_manager_util.h"
#include "third_party/abseil-cpp/absl/types/optional.h"
#endif

#if !BUILDFLAG(IS_ANDROID)
#include "chrome/browser/profiles/profile_window.h"
#endif

#if !BUILDFLAG(IS_ANDROID) && !BUILDFLAG(IS_CHROMEOS_ASH)
#include "chrome/browser/ui/browser_list.h"
#include "chrome/browser/ui/profile_picker.h"
#endif

namespace {

// List of sources for which sign out is always allowed.
// TODO(crbug.com/1161966): core product logic should not rely on metric
// sources/callsites.  Consider removing such logic, potentially as part of
// introducing a cross-platform SigninManager.
signin_metrics::ProfileSignout kAlwaysAllowedSignoutSources[] = {
    // Allowed, because data has not been synced yet.
    signin_metrics::ProfileSignout::ABORT_SIGNIN,
    // Allowed, because only used on Android and the primary account must be
    // cleared when the account is removed from device
    signin_metrics::ProfileSignout::ACCOUNT_REMOVED_FROM_DEVICE,
    // Allowed to force finish the account id migration.
    signin_metrics::ACCOUNT_ID_MIGRATION,
    // Allowed, for tests.
    signin_metrics::ProfileSignout::FORCE_SIGNOUT_ALWAYS_ALLOWED_FOR_TEST,
    // Allowed, because access to this entry point is controlled to only be
    // enabled if the user may turn off sync.
    signin_metrics::ProfileSignout::USER_CLICKED_REVOKE_SYNC_CONSENT_SETTINGS,
    // Allowed, because the dialog offers the option to the user to sign out.
    // Note that the dialog is only shown on iOS and isn't planned to be shown
    // on the other platforms since they already support user policies (no need
    // for a notification in that case). Still, the metric is added to the
    // kAlwaysAllowedSignoutSources for coherence.
    signin_metrics::ProfileSignout::
        USER_CLICKED_SIGNOUT_FROM_USER_POLICY_NOTIFICATION_DIALOG,
};

SigninClient::SignoutDecision IsSignoutAllowed(
    Profile* profile,
    const signin_metrics::ProfileSignout signout_source) {
  if (signin_util::IsUserSignoutAllowedForProfile(profile))
    return SigninClient::SignoutDecision::ALLOW_SIGNOUT;

  auto* identity_manager =
      IdentityManagerFactory::GetForProfileIfExists(profile);
  if (identity_manager &&
      !identity_manager->HasPrimaryAccount(signin::ConsentLevel::kSync)) {
    return SigninClient::SignoutDecision::ALLOW_SIGNOUT;
  }

  for (const auto& always_allowed_source : kAlwaysAllowedSignoutSources) {
    if (signout_source == always_allowed_source)
      return SigninClient::SignoutDecision::ALLOW_SIGNOUT;
  }

  return SigninClient::SignoutDecision::DISALLOW_SIGNOUT;
}

}  // namespace

ChromeSigninClient::ChromeSigninClient(Profile* profile) : profile_(profile) {
#if !BUILDFLAG(IS_CHROMEOS_ASH)
  content::GetNetworkConnectionTracker()->AddNetworkConnectionObserver(this);
#endif
}

ChromeSigninClient::~ChromeSigninClient() {
#if !BUILDFLAG(IS_CHROMEOS_ASH)
  content::GetNetworkConnectionTracker()->RemoveNetworkConnectionObserver(this);
#endif
}

void ChromeSigninClient::DoFinalInit() {
  VerifySyncToken();
}

// static
bool ChromeSigninClient::ProfileAllowsSigninCookies(Profile* profile) {
  content_settings::CookieSettings* cookie_settings =
      CookieSettingsFactory::GetForProfile(profile).get();
  return signin::SettingsAllowSigninCookies(cookie_settings);
}

PrefService* ChromeSigninClient::GetPrefs() { return profile_->GetPrefs(); }

scoped_refptr<network::SharedURLLoaderFactory>
ChromeSigninClient::GetURLLoaderFactory() {
  if (url_loader_factory_for_testing_)
    return url_loader_factory_for_testing_;

  return profile_->GetDefaultStoragePartition()
      ->GetURLLoaderFactoryForBrowserProcess();
}

network::mojom::CookieManager* ChromeSigninClient::GetCookieManager() {
  return profile_->GetDefaultStoragePartition()
      ->GetCookieManagerForBrowserProcess();
}

bool ChromeSigninClient::AreSigninCookiesAllowed() {
  return ProfileAllowsSigninCookies(profile_);
}

bool ChromeSigninClient::AreSigninCookiesDeletedOnExit() {
  content_settings::CookieSettings* cookie_settings =
      CookieSettingsFactory::GetForProfile(profile_).get();
  return signin::SettingsDeleteSigninCookiesOnExit(cookie_settings);
}

void ChromeSigninClient::AddContentSettingsObserver(
    content_settings::Observer* observer) {
  HostContentSettingsMapFactory::GetForProfile(profile_)
      ->AddObserver(observer);
}

void ChromeSigninClient::RemoveContentSettingsObserver(
    content_settings::Observer* observer) {
  HostContentSettingsMapFactory::GetForProfile(profile_)
      ->RemoveObserver(observer);
}

void ChromeSigninClient::PreSignOut(
    base::OnceCallback<void(SignoutDecision)> on_signout_decision_reached,
    signin_metrics::ProfileSignout signout_source_metric) {
  DCHECK(on_signout_decision_reached);
  DCHECK(!on_signout_decision_reached_) << "SignOut already in-progress!";
  on_signout_decision_reached_ = std::move(on_signout_decision_reached);

#if !BUILDFLAG(IS_ANDROID) && !BUILDFLAG(IS_CHROMEOS_ASH)
  // `signout_source_metric` is `signin_metrics::ABORT_SIGNIN` if the user
  // declines sync in the signin process. In case the user accepts the managed
  // account but declines sync, we should keep the window open.
  bool user_declines_sync_after_consenting_to_management =
      signout_source_metric == signin_metrics::ABORT_SIGNIN &&
      chrome::enterprise_util::UserAcceptedAccountManagement(profile_);
  // These sign out won't remove the policy cache, keep the window opened.
  bool keep_window_opened =
      signout_source_metric ==
          signin_metrics::GOOGLE_SERVICE_NAME_PATTERN_CHANGED ||
      signout_source_metric == signin_metrics::SERVER_FORCED_DISABLE ||
      signout_source_metric == signin_metrics::SIGNOUT_PREF_CHANGED ||
      user_declines_sync_after_consenting_to_management;
  if (signin_util::IsForceSigninEnabled() && !profile_->IsSystemProfile() &&
      !profile_->IsGuestSession() && !profile_->IsChild() &&
      !keep_window_opened) {
    if (signout_source_metric ==
        signin_metrics::SIGNIN_PREF_CHANGED_DURING_SIGNIN) {
      // SIGNIN_PREF_CHANGED_DURING_SIGNIN will be triggered when
      // IdentityManager is initialized before window opening, there is no need
      // to close window. Call OnCloseBrowsersSuccess to continue sign out and
      // show UserManager afterwards.
      should_display_user_manager_ = false;  // Don't show UserManager twice.
      OnCloseBrowsersSuccess(signout_source_metric, profile_->GetPath());
    } else {
      BrowserList::CloseAllBrowsersWithProfile(
          profile_,
          base::BindRepeating(&ChromeSigninClient::OnCloseBrowsersSuccess,
                              base::Unretained(this), signout_source_metric),
          base::BindRepeating(&ChromeSigninClient::OnCloseBrowsersAborted,
                              base::Unretained(this)),
          signout_source_metric == signin_metrics::ABORT_SIGNIN ||
              signout_source_metric ==
                  signin_metrics::AUTHENTICATION_FAILED_WITH_FORCE_SIGNIN ||
              signout_source_metric == signin_metrics::TRANSFER_CREDENTIALS);
    }
  } else {
#else
  {
#endif
    std::move(on_signout_decision_reached_)
        .Run(IsSignoutAllowed(profile_, signout_source_metric));
  }
}

#if !BUILDFLAG(IS_CHROMEOS_ASH)
void ChromeSigninClient::OnConnectionChanged(
    network::mojom::ConnectionType type) {
  if (type == network::mojom::ConnectionType::CONNECTION_NONE)
    return;

  for (base::OnceClosure& callback : delayed_callbacks_)
    std::move(callback).Run();

  delayed_callbacks_.clear();
}
#endif

void ChromeSigninClient::DelayNetworkCall(base::OnceClosure callback) {
#if BUILDFLAG(IS_CHROMEOS_ASH)
  // Do not make network requests in unit tests. ash::NetworkHandler should
  // not be used and is not expected to have been initialized in unit tests.
  if (url_loader_factory_for_testing_ &&
      !ash::NetworkHandler::IsInitialized()) {
    std::move(callback).Run();
    return;
  }
  ash::DelayNetworkCall(base::Milliseconds(ash::kDefaultNetworkRetryDelayMS),
                        std::move(callback));
  return;
#else
  // Don't bother if we don't have any kind of network connection.
  network::mojom::ConnectionType type;
  bool sync = content::GetNetworkConnectionTracker()->GetConnectionType(
      &type, base::BindOnce(&ChromeSigninClient::OnConnectionChanged,
                            weak_ptr_factory_.GetWeakPtr()));
  if (!sync || type == network::mojom::ConnectionType::CONNECTION_NONE) {
    // Connection type cannot be retrieved synchronously so delay the callback.
    delayed_callbacks_.push_back(std::move(callback));
  } else {
    std::move(callback).Run();
  }
#endif
}

std::unique_ptr<GaiaAuthFetcher> ChromeSigninClient::CreateGaiaAuthFetcher(
    GaiaAuthConsumer* consumer,
    gaia::GaiaSource source) {
  return std::make_unique<GaiaAuthFetcher>(consumer, source,
                                           GetURLLoaderFactory());
}

void ChromeSigninClient::VerifySyncToken() {
#if !BUILDFLAG(IS_ANDROID) && !BUILDFLAG(IS_CHROMEOS_ASH)
  // We only verifiy the token once when Profile is just created.
  if (signin_util::IsForceSigninEnabled() && !force_signin_verifier_)
    force_signin_verifier_ = std::make_unique<ForceSigninVerifier>(
        profile_, IdentityManagerFactory::GetForProfile(profile_));
#endif
}

#if BUILDFLAG(IS_CHROMEOS_LACROS)
// Returns the account that must be auto-signed-in to the Main Profile in
// Lacros.
// This is, when available, the account used to sign into the Chrome OS
// session. This may be a Gaia account or a Microsoft Active Directory
// account. This field will be null for Guest sessions, Managed Guest
// sessions, Demo mode, and Kiosks. Note that this is different from the
// concept of a Primary Account in the browser. A user may not be signed into
// a Lacros browser Profile, or may be signed into a browser Profile with an
// account which is different from the account which they used to sign into
// the device - aka Device Account.
// Also note that this will be null for Secondary / non-Main Profiles in
// Lacros, because they do not start with the Chrome OS Device Account
// signed-in by default.
absl::optional<account_manager::Account>
ChromeSigninClient::GetInitialPrimaryAccount() {
  if (!profile_->IsMainProfile())
    return absl::nullopt;

  const crosapi::mojom::AccountPtr& device_account =
      chromeos::BrowserParamsProxy::Get()->DeviceAccount();
  if (!device_account)
    return absl::nullopt;

  return account_manager::FromMojoAccount(device_account);
}

// Returns whether the account that must be auto-signed-in to the main profile
// in Lacros is a child account.
// Returns false for guest session, public session, kiosk, demo mode and Active
// Directory account.
// Returns null for secondary / non-main profiles in LaCrOS.
absl::optional<bool> ChromeSigninClient::IsInitialPrimaryAccountChild() const {
  if (!profile_->IsMainProfile())
    return absl::nullopt;

  const bool is_child_session =
      chromeos::BrowserParamsProxy::Get()->SessionType() ==
      crosapi::mojom::SessionType::kChildSession;
  return is_child_session;
}

void ChromeSigninClient::RemoveAccount(
    const account_manager::AccountKey& account_key) {
  absl::optional<account_manager::Account> device_account =
      GetInitialPrimaryAccount();
  if (device_account.has_value() && device_account->key == account_key) {
    DLOG(ERROR)
        << "The primary account should not be removed from the main profile";
    return;
  }

  g_browser_process->profile_manager()
      ->GetAccountProfileMapper()
      ->RemoveAccount(profile_->GetPath(), account_key);
}

void ChromeSigninClient::RemoveAllAccounts() {
  if (GetInitialPrimaryAccount().has_value()) {
    DLOG(ERROR) << "It is not allowed to remove the initial primary account.";
    return;
  }

  DCHECK(!profile_->IsMainProfile());
  g_browser_process->profile_manager()
      ->GetAccountProfileMapper()
      ->RemoveAllAccounts(profile_->GetPath());
}
#endif  // BUILDFLAG(IS_CHROMEOS_LACROS)

void ChromeSigninClient::SetURLLoaderFactoryForTest(
    scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory) {
  url_loader_factory_for_testing_ = url_loader_factory;
}

void ChromeSigninClient::OnCloseBrowsersSuccess(
    const signin_metrics::ProfileSignout signout_source_metric,
    const base::FilePath& profile_path) {
#if !BUILDFLAG(IS_ANDROID) && !BUILDFLAG(IS_CHROMEOS_ASH)
  if (signin_util::IsForceSigninEnabled() && force_signin_verifier_.get()) {
    force_signin_verifier_->Cancel();
  }
#endif

  std::move(on_signout_decision_reached_)
      .Run(IsSignoutAllowed(profile_, signout_source_metric));

  LockForceSigninProfile(profile_path);
  // After sign out, lock the profile and show UserManager if necessary.
  if (should_display_user_manager_) {
    ShowUserManager(profile_path);
  } else {
    should_display_user_manager_ = true;
  }
}

void ChromeSigninClient::OnCloseBrowsersAborted(
    const base::FilePath& profile_path) {
  should_display_user_manager_ = true;

  // Disallow sign-out (aborted).
  std::move(on_signout_decision_reached_)
      .Run(SignoutDecision::DISALLOW_SIGNOUT);
}

void ChromeSigninClient::LockForceSigninProfile(
    const base::FilePath& profile_path) {
  ProfileAttributesEntry* entry =
      g_browser_process->profile_manager()
          ->GetProfileAttributesStorage()
          .GetProfileAttributesWithPath(profile_->GetPath());
  if (!entry)
    return;
  entry->LockForceSigninProfile(true);
}

void ChromeSigninClient::ShowUserManager(const base::FilePath& profile_path) {
#if !BUILDFLAG(IS_ANDROID) && !BUILDFLAG(IS_CHROMEOS_ASH)
  ProfilePicker::Show(ProfilePicker::Params::FromEntryPoint(
      ProfilePicker::EntryPoint::kProfileLocked));
#endif
}
