// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.night_mode.settings;

import static org.chromium.chrome.browser.preferences.ChromePreferenceKeys.UI_THEME_SETTING;

import android.os.Build;
import android.os.Bundle;

import androidx.annotation.Nullable;
import androidx.preference.PreferenceFragmentCompat;

import org.chromium.chrome.browser.flags.ChromeFeatureList;
import org.chromium.chrome.browser.night_mode.NightModeMetrics;
import org.chromium.chrome.browser.night_mode.NightModeUtils;
import org.chromium.chrome.browser.night_mode.R;
import org.chromium.chrome.browser.night_mode.WebContentsDarkModeController;
import org.chromium.chrome.browser.night_mode.WebContentsDarkModeMessageController;
import org.chromium.chrome.browser.preferences.SharedPreferencesManager;
import org.chromium.chrome.browser.profiles.Profile;
import org.chromium.components.browser_ui.settings.SettingsUtils;
import org.chromium.ui.UiUtils;

/**
 * Fragment to manage the theme user settings.
 */
public class ThemeSettingsFragment extends PreferenceFragmentCompat {
    static final String PREF_UI_THEME_PREF = "ui_theme_pref";

    public static final String KEY_THEME_SETTINGS_ENTRY = "theme_settings_entry";

    private boolean mWebContentsDarkModeEnabled;

    @Override
    public void onCreatePreferences(@Nullable Bundle savedInstanceState, String rootKey) {
        SettingsUtils.addPreferencesFromResource(this, R.xml.theme_preferences);
        getActivity().setTitle(R.string.theme_settings);

        SharedPreferencesManager sharedPreferencesManager = SharedPreferencesManager.getInstance();
        RadioButtonGroupThemePreference radioButtonGroupThemePreference =
                (RadioButtonGroupThemePreference) findPreference(PREF_UI_THEME_PREF);
        mWebContentsDarkModeEnabled = WebContentsDarkModeController.isGlobalUserSettingsEnabled(
                Profile.getLastUsedRegularProfile());
        radioButtonGroupThemePreference.initialize(
                NightModeUtils.getThemeSetting(), mWebContentsDarkModeEnabled);

        radioButtonGroupThemePreference.setOnPreferenceChangeListener((preference, newValue) -> {
            if (ChromeFeatureList.isEnabled(
                        ChromeFeatureList.DARKEN_WEBSITES_CHECKBOX_IN_THEMES_SETTING)) {
                if (radioButtonGroupThemePreference.isDarkenWebsitesEnabled()
                        != mWebContentsDarkModeEnabled) {
                    mWebContentsDarkModeEnabled =
                            radioButtonGroupThemePreference.isDarkenWebsitesEnabled();
                    WebContentsDarkModeController.setGlobalUserSettings(
                            Profile.getLastUsedRegularProfile(), mWebContentsDarkModeEnabled);
                }
            }
            int theme = (int) newValue;
            sharedPreferencesManager.writeInt(UI_THEME_SETTING, theme);
            return true;
        });

        // TODO(crbug.com/1252868): Notify feature engagement system that settings were opened.
        // Record entry point metrics if this fragment is freshly created.
        if (savedInstanceState == null) {
            assert getArguments() != null
                    && getArguments().containsKey(KEY_THEME_SETTINGS_ENTRY)
                : "<theme_settings_entry> is missing in args.";
            NightModeMetrics.recordThemeSettingsEntry(
                    getArguments().getInt(KEY_THEME_SETTINGS_ENTRY));
        }

        if (ChromeFeatureList.isEnabled(
                    ChromeFeatureList.DARKEN_WEBSITES_CHECKBOX_IN_THEMES_SETTING)) {
            WebContentsDarkModeMessageController.notifyEventSettingsOpened(
                    Profile.getLastUsedRegularProfile());
        }
    }

    @Override
    public void onActivityCreated(Bundle savedInstanceState) {
        super.onActivityCreated(savedInstanceState);

        // On O_MR1, the flag View.SYSTEM_UI_FLAG_LIGHT_NAVIGATION_BAR in this fragment is not
        // updated to the attribute android:windowLightNavigationBar set in preference theme, so
        // we set the flag explicitly to workaround the issue. See https://crbug.com/942551.
        if (Build.VERSION.SDK_INT == Build.VERSION_CODES.O_MR1) {
            UiUtils.setNavigationBarIconColor(getActivity().getWindow().getDecorView(),
                    getResources().getBoolean(R.bool.window_light_navigation_bar));
        }

        setDivider(null);
    }
}
