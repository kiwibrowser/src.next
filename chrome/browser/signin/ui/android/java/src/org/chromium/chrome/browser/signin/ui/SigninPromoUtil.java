// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.signin.ui;

import android.accounts.Account;
import android.content.Context;
import android.text.TextUtils;

import org.chromium.chrome.browser.flags.ChromeFeatureList;
import org.chromium.chrome.browser.preferences.Pref;
import org.chromium.chrome.browser.profiles.Profile;
import org.chromium.chrome.browser.signin.services.IdentityServicesProvider;
import org.chromium.chrome.browser.signin.services.SigninPreferencesManager;
import org.chromium.components.signin.AccountManagerFacade;
import org.chromium.components.signin.AccountManagerFacadeProvider;
import org.chromium.components.signin.AccountUtils;
import org.chromium.components.signin.identitymanager.ConsentLevel;
import org.chromium.components.signin.metrics.SigninAccessPoint;
import org.chromium.components.user_prefs.UserPrefs;

import java.util.HashSet;
import java.util.List;
import java.util.Set;

/**
 * Helper functions for promoting sign in.
 */
public final class SigninPromoUtil {
    private SigninPromoUtil() {}

    /**
     * Launches the {@link SyncConsentActivity} if it needs to be displayed.
     * @param context The {@link Context} to launch the {@link SyncConsentActivity}.
     * @param syncConsentActivityLauncher launcher used to launch the {@link SyncConsentActivity}.
     * @param currentMajorVersion The current major version of Chrome.
     * @return Whether the signin promo is shown.
     */
    public static boolean launchSigninPromoIfNeeded(Context context,
            SyncConsentActivityLauncher syncConsentActivityLauncher,
            final int currentMajorVersion) {
        if (true)
          return false;
        final SigninPreferencesManager prefManager = SigninPreferencesManager.getInstance();
        if (shouldLaunchSigninPromo(prefManager, currentMajorVersion)) {
            syncConsentActivityLauncher.launchActivityIfAllowed(
                    context, SigninAccessPoint.SIGNIN_PROMO);
            prefManager.setSigninPromoLastShownVersion(currentMajorVersion);
            final List<Account> accounts = AccountUtils.getAccountsIfFulfilledOrEmpty(
                    AccountManagerFacadeProvider.getInstance().getAccounts());
            prefManager.setSigninPromoLastAccountNames(
                    new HashSet<>(AccountUtils.toAccountNames(accounts)));
            return true;
        }
        return false;
    }

    private static boolean shouldLaunchSigninPromo(
            SigninPreferencesManager prefManager, final int currentMajorVersion) {
        if (ChromeFeatureList.isEnabled(ChromeFeatureList.FORCE_STARTUP_SIGNIN_PROMO)) {
            return true;
        }
        final int lastPromoMajorVersion = prefManager.getSigninPromoLastShownVersion();
        if (lastPromoMajorVersion == 0) {
            prefManager.setSigninPromoLastShownVersion(currentMajorVersion);
            return false;
        }

        if (currentMajorVersion < lastPromoMajorVersion + 2) {
            // Promo can be shown at most once every 2 Chrome major versions.
            return false;
        }

        final Profile profile = Profile.getLastUsedRegularProfile();
        if (IdentityServicesProvider.get().getIdentityManager(profile).getPrimaryAccountInfo(
                    ConsentLevel.SYNC)
                != null) {
            // Don't show if user is signed in.
            return false;
        }

        if (TextUtils.isEmpty(
                    UserPrefs.get(profile).getString(Pref.GOOGLE_SERVICES_LAST_USERNAME))) {
            // Don't show if user has manually signed out.
            return false;
        }

        final AccountManagerFacade accountManagerFacade =
                AccountManagerFacadeProvider.getInstance();
        final List<Account> accounts =
                AccountUtils.getAccountsIfFulfilledOrEmpty(accountManagerFacade.getAccounts());
        if (accounts.isEmpty()) {
            // Don't show if the account list isn't available yet or there are no accounts in it.
            return false;
        }

        final boolean canDefaultAccountOfferExtendedSyncPromos =
                accountManagerFacade.canOfferExtendedSyncPromos(accounts.get(0)).or(false);
        if (ChromeFeatureList.isEnabled(ChromeFeatureList.MINOR_MODE_SUPPORT)
                && !canDefaultAccountOfferExtendedSyncPromos) {
            return false;
        }
        if (ChromeFeatureList.isEnabled(ChromeFeatureList.FORCE_DISABLE_EXTENDED_SYNC_PROMOS)) {
            return false;
        }

        final List<String> currentAccountNames = AccountUtils.toAccountNames(accounts);
        final Set<String> previousAccountNames = prefManager.getSigninPromoLastAccountNames();
        // Don't show if no new accounts have been added after the last time promo was shown.
        return previousAccountNames == null
                || !previousAccountNames.containsAll(currentAccountNames);
    }
}
