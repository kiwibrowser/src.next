// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.download.settings;

import android.os.Bundle;

import androidx.annotation.Nullable;
import androidx.annotation.VisibleForTesting;
import androidx.preference.Preference;
import androidx.preference.PreferenceFragmentCompat;

import org.chromium.chrome.browser.download.DownloadDialogBridge;
import org.chromium.chrome.browser.download.DownloadLaterPromptStatus;
import org.chromium.chrome.browser.download.DownloadPromptStatus;
import org.chromium.chrome.browser.download.R;
import org.chromium.chrome.browser.flags.ChromeFeatureList;
import org.chromium.chrome.browser.offlinepages.prefetch.PrefetchConfiguration;
import org.chromium.chrome.browser.preferences.Pref;
import org.chromium.chrome.browser.profiles.Profile;
import org.chromium.chrome.browser.profiles.ProfileKey;
import org.chromium.chrome.browser.settings.ChromeManagedPreferenceDelegate;
import org.chromium.components.browser_ui.settings.ChromeSwitchPreference;
import org.chromium.components.browser_ui.settings.ManagedPreferenceDelegate;
import org.chromium.components.browser_ui.settings.SettingsUtils;
import org.chromium.components.prefs.PrefService;
import org.chromium.components.user_prefs.UserPrefs;

import android.content.ComponentName;
import android.app.Activity;
import android.content.SharedPreferences;
import android.os.Bundle;
import org.chromium.base.Log;
import android.view.View;
import org.chromium.base.ContextUtils;
import android.graphics.Color;
import android.graphics.drawable.ColorDrawable;
import android.widget.ListView;
import android.content.Context;
import android.widget.ListView;
import android.content.pm.PackageManager;
import android.content.Intent;
import android.content.Context;
import android.content.pm.ResolveInfo;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import android.net.Uri;
import android.text.TextUtils;
import org.chromium.components.browser_ui.settings.ChromeBaseCheckBoxPreference;

/**
 * Fragment containing Download settings.
 */
public class DownloadSettings
        extends PreferenceFragmentCompat implements Preference.OnPreferenceChangeListener {
    static final String PREF_LOCATION_CHANGE = "location_change";
    static final String PREF_DOWNLOAD_LATER_PROMPT_ENABLED = "download_later_prompt_enabled";
    static final String PREF_LOCATION_PROMPT_ENABLED = "location_prompt_enabled";
    static final String PREF_PREFETCHING_ENABLED = "prefetching_enabled";

    private PrefService mPrefService;
    private DownloadLocationPreference mLocationChangePref;
    private ChromeSwitchPreference mDownloadLaterPromptEnabledPref;
    private ChromeSwitchPreference mLocationPromptEnabledPref;
    private ManagedPreferenceDelegate mLocationPromptEnabledPrefDelegate;
    private ChromeSwitchPreference mPrefetchingEnabled;

    private ChromeBaseCheckBoxPreference mExternalDownloadManager;

    @Override
    public void onCreatePreferences(@Nullable Bundle savedInstanceState, String s) {
        getActivity().setTitle(R.string.menu_downloads);
        SettingsUtils.addPreferencesFromResource(this, R.xml.download_preferences);
        mPrefService = UserPrefs.get(Profile.getLastUsedRegularProfile());

        mDownloadLaterPromptEnabledPref =
                (ChromeSwitchPreference) findPreference(PREF_DOWNLOAD_LATER_PROMPT_ENABLED);
        mDownloadLaterPromptEnabledPref.setOnPreferenceChangeListener(this);

        boolean locationManaged = DownloadDialogBridge.isLocationDialogManaged();
        if (locationManaged || !ChromeFeatureList.isEnabled(ChromeFeatureList.DOWNLOAD_LATER)) {
            getPreferenceScreen().removePreference(
                    findPreference(PREF_DOWNLOAD_LATER_PROMPT_ENABLED));
        }

        mLocationPromptEnabledPref =
                (ChromeSwitchPreference) findPreference(PREF_LOCATION_PROMPT_ENABLED);
        mLocationPromptEnabledPref.setOnPreferenceChangeListener(this);
        mLocationPromptEnabledPrefDelegate = new ChromeManagedPreferenceDelegate() {
            @Override
            public boolean isPreferenceControlledByPolicy(Preference preference) {
                return DownloadDialogBridge.isLocationDialogManaged();
            }
        };
        mLocationPromptEnabledPref.setManagedPreferenceDelegate(mLocationPromptEnabledPrefDelegate);
        mLocationChangePref = (DownloadLocationPreference) findPreference(PREF_LOCATION_CHANGE);

        if (PrefetchConfiguration.isPrefetchingFlagEnabled()) {
            mPrefetchingEnabled = (ChromeSwitchPreference) findPreference(PREF_PREFETCHING_ENABLED);
            mPrefetchingEnabled.setOnPreferenceChangeListener(this);

            updatePrefetchSummary();
        } else {
            getPreferenceScreen().removePreference(findPreference(PREF_PREFETCHING_ENABLED));
        }

        mExternalDownloadManager = (ChromeBaseCheckBoxPreference) findPreference("enable_external_download_manager");
    }

    @Override
    public void onDisplayPreferenceDialog(Preference preference) {
        if (preference instanceof DownloadLocationPreference) {
            DownloadLocationPreferenceDialog dialogFragment =
                    DownloadLocationPreferenceDialog.newInstance(
                            (DownloadLocationPreference) preference);
            dialogFragment.setTargetFragment(this, 0);
            dialogFragment.show(getFragmentManager(), DownloadLocationPreferenceDialog.TAG);
        } else {
            super.onDisplayPreferenceDialog(preference);
        }
    }

    @Override
    public void onResume() {
        super.onResume();
        updateDownloadSettings();
    }

    private void updateDownloadSettings() {
        mLocationChangePref.updateSummary();

        if (ChromeFeatureList.isEnabled(ChromeFeatureList.DOWNLOAD_LATER)) {
            @DownloadLaterPromptStatus
            int downloadLaterPromptStatus =
                    mPrefService.getInteger(Pref.DOWNLOAD_LATER_PROMPT_STATUS);
            mDownloadLaterPromptEnabledPref.setChecked(
                    !(downloadLaterPromptStatus == DownloadLaterPromptStatus.DONT_SHOW));
        }

        if (DownloadDialogBridge.isLocationDialogManaged()) {
            // Location prompt can be controlled by the enterprise policy.
            mLocationPromptEnabledPref.setChecked(
                    DownloadDialogBridge.getPromptForDownloadPolicy());
        } else {
            // Location prompt is marked enabled if the prompt status is not DONT_SHOW.
            boolean isLocationPromptEnabled = DownloadDialogBridge.getPromptForDownloadAndroid()
                    != DownloadPromptStatus.DONT_SHOW;
            mLocationPromptEnabledPref.setChecked(isLocationPromptEnabled);
            mLocationPromptEnabledPref.setEnabled(true);
        }

        if (mPrefetchingEnabled != null) {
            mPrefetchingEnabled.setChecked(PrefetchConfiguration.isPrefetchingEnabledInSettings(
                    ProfileKey.getLastUsedRegularProfileKey()));
            updatePrefetchSummary();
        }

        if (mExternalDownloadManager != null) {
            mExternalDownloadManager.setOnPreferenceChangeListener(this);
            mExternalDownloadManager.setChecked(ContextUtils.getAppSharedPreferences().getBoolean("enable_external_download_manager", false));
            if (ContextUtils.getAppSharedPreferences().getBoolean("enable_external_download_manager", false)
                  && !TextUtils.isEmpty(ContextUtils.getAppSharedPreferences().getString("selected_external_download_manager_package_name", ""))) {
                mExternalDownloadManager.setSummary(ContextUtils.getAppSharedPreferences().getString("selected_external_download_manager_package_name", ""));
            }
        }
    }

    @Override
    public void onActivityResult(int requestCode, int resultCode, Intent data) {
            Log.i("Kiwi", "[DownloadPreferences] Received activity result, RQ: " + requestCode);
            if (requestCode == 4242 && resultCode == Activity.RESULT_OK && data != null) {
                 ComponentName componentName = data.getComponent();
                 final String packageName = componentName.getPackageName();
                 final String activityName = componentName.getClassName();
                 Log.i("Kiwi", "[DownloadPreferences] Received activity result, PN: " + packageName + " - AN: " + activityName);
                 SharedPreferences.Editor sharedPreferencesEditor = ContextUtils.getAppSharedPreferences().edit();
                 sharedPreferencesEditor.putString("selected_external_download_manager_package_name", packageName);
                 sharedPreferencesEditor.putString("selected_external_download_manager_activity_name", activityName);
                 sharedPreferencesEditor.apply();
                 updateDownloadSettings();
            }
    }

    private void updatePrefetchSummary() {
        // The summary text should remain empty if mPrefetchingEnabled is switched off so it is only
        // updated when the setting is on.
        ProfileKey profileKey = ProfileKey.getLastUsedRegularProfileKey();
        if (PrefetchConfiguration.isPrefetchingEnabled(profileKey)) {
            mPrefetchingEnabled.setSummaryOn("");
        } else if (PrefetchConfiguration.isPrefetchingEnabledInSettings(profileKey)) {
            // If prefetching is enabled by the user but isPrefetchingEnabled() returned false, we
            // know that prefetching is forbidden by the server.
            if (PrefetchConfiguration.isEnabledByServerUnknown(profileKey)) {
                mPrefetchingEnabled.setSummaryOn(
                        R.string.download_settings_prefetch_maybe_unavailable_description);
            } else {
                mPrefetchingEnabled.setSummaryOn(
                        R.string.download_settings_prefetch_unavailable_description);
            }
        }
    }

    // Preference.OnPreferenceChangeListener implementation.
    @Override
    public boolean onPreferenceChange(Preference preference, Object newValue) {
        if (PREF_DOWNLOAD_LATER_PROMPT_ENABLED.equals(preference.getKey())) {
            if (!ChromeFeatureList.isEnabled(ChromeFeatureList.DOWNLOAD_LATER)) return false;
            @DownloadLaterPromptStatus
            int downloadLaterPromptStatus =
                    mPrefService.getInteger(Pref.DOWNLOAD_LATER_PROMPT_STATUS);
            if (!(boolean) newValue) {
                mPrefService.setInteger(
                        Pref.DOWNLOAD_LATER_PROMPT_STATUS, DownloadLaterPromptStatus.DONT_SHOW);
                return true;
            }

            // Only update if the download later dialog has been shown before.
            if (downloadLaterPromptStatus != DownloadLaterPromptStatus.SHOW_INITIAL) {
                mPrefService.setInteger(Pref.DOWNLOAD_LATER_PROMPT_STATUS,
                        DownloadLaterPromptStatus.SHOW_PREFERENCE);
            }
        } else if (PREF_LOCATION_PROMPT_ENABLED.equals(preference.getKey())) {
            if ((boolean) newValue) {
                // Only update if the download location dialog has been shown before.
                if (DownloadDialogBridge.getPromptForDownloadAndroid()
                        != DownloadPromptStatus.SHOW_INITIAL) {
                    DownloadDialogBridge.setPromptForDownloadAndroid(
                            DownloadPromptStatus.SHOW_PREFERENCE);
                }
            } else {
                DownloadDialogBridge.setPromptForDownloadAndroid(DownloadPromptStatus.DONT_SHOW);
            }
        } else if (PREF_PREFETCHING_ENABLED.equals(preference.getKey())) {
            PrefetchConfiguration.setPrefetchingEnabledInSettings(
                    ProfileKey.getLastUsedRegularProfileKey(), (boolean) newValue);
            updatePrefetchSummary();
        }
        else if ("enable_external_download_manager".equals(preference.getKey())) {
            SharedPreferences.Editor sharedPreferencesEditor = ContextUtils.getAppSharedPreferences().edit();
            sharedPreferencesEditor.putBoolean("enable_external_download_manager", (boolean)newValue);
            sharedPreferencesEditor.apply();
            if ((boolean)newValue == true) {
                    List<Intent> targetedShareIntents = new ArrayList<Intent>();
                    Intent shareIntent = new Intent(android.content.Intent.ACTION_VIEW, Uri.parse("http://test.com/file.rar"));
                    // Set title and text to share when the user selects an option.
                    shareIntent.putExtra(Intent.EXTRA_TEXT, "http://test.com/file.rar");
                    List<ResolveInfo> resInfo = getActivity().getPackageManager().queryIntentActivities(shareIntent, 0);
                    if (!resInfo.isEmpty()) {
                        for (ResolveInfo info : resInfo) {
                            if (!"com.kiwibrowser.browser".equalsIgnoreCase(info.activityInfo.packageName)) {
                                Intent targetedShare = new Intent(android.content.Intent.ACTION_VIEW);
                                targetedShare.setPackage(info.activityInfo.packageName.toLowerCase(Locale.ROOT));
                                targetedShareIntents.add(targetedShare);
                            }
                        }
                        // Then show the ACTION_PICK_ACTIVITY to let the user select it
                        Intent intentPick = new Intent();
                        intentPick.setAction(Intent.ACTION_PICK_ACTIVITY);
                        // Set the title of the dialog
                        intentPick.putExtra(Intent.EXTRA_TITLE, "Download manager");
                        intentPick.putExtra(Intent.EXTRA_INTENT, shareIntent);
                        intentPick.putExtra(Intent.EXTRA_INITIAL_INTENTS, targetedShareIntents.toArray());
                        // Call StartActivityForResult so we can get the app name selected by the user
                        this.startActivityForResult(intentPick, /* REQUEST_CODE_MY_PICK */ 4242);
                    }
            }
        }
        return true;
    }

    @VisibleForTesting
    ManagedPreferenceDelegate getLocationPromptEnabledPrefDelegateForTesting() {
        return mLocationPromptEnabledPrefDelegate;
    }
}
