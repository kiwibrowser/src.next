// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.settings;

import android.os.Bundle;
import android.text.TextUtils;

import androidx.annotation.VisibleForTesting;
import androidx.preference.Preference;
import androidx.preference.PreferenceFragmentCompat;
import android.content.SharedPreferences;

import org.chromium.base.ContextUtils;
import org.chromium.base.metrics.RecordUserAction;
import org.chromium.chrome.R;
import org.chromium.chrome.browser.settings.RadioButtonGroupTabSwitcherPreference.PreferenceValues;
import org.chromium.chrome.browser.settings.ChromeManagedPreferenceDelegate;
import org.chromium.components.browser_ui.settings.ChromeSwitchPreference;
import org.chromium.components.browser_ui.settings.SettingsUtils;
import org.chromium.components.embedder_support.util.UrlUtilities;
import org.chromium.components.url_formatter.UrlFormatter;

/**
 * Fragment that allows the user to configure tabswitcher related preferences.
 */
public class TabSwitcherSettings extends PreferenceFragmentCompat {
    @VisibleForTesting
    public static final String PREF_TABSWITCHER_SWITCH = "tabswitcher_switch";
    @VisibleForTesting
    public static final String PREF_TABSWITCHER_RADIO_GROUP = "tabswitcher_radio_group";

    private RadioButtonGroupTabSwitcherPreference mRadioButtons;

    @Override
    public void onCreatePreferences(Bundle savedInstanceState, String rootKey) {
        getActivity().setTitle(R.string.preferences_tabswitcher);
        SettingsUtils.addPreferencesFromResource(this, R.xml.tabswitcher_preferences);

        mRadioButtons =
                (RadioButtonGroupTabSwitcherPreference) findPreference(PREF_TABSWITCHER_RADIO_GROUP);
        mRadioButtons.setActivity(getActivity());

        RecordUserAction.record("Settings.TabSwitcher.Opened");
    }

    @Override
    public void onResume() {
        super.onResume();
    }

    @Override
    public void onStop() {
        super.onStop();
    }

    private PreferenceValues createPreferenceValuesForRadioGroup() {
        String checkedOption = ContextUtils.getAppSharedPreferences().getString("active_tabswitcher", "default");
        return new PreferenceValues(checkedOption);
    }
}
