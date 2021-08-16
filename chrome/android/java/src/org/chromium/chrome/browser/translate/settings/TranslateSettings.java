// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.translate.settings;

import android.os.Bundle;
import android.text.TextUtils;

import androidx.annotation.VisibleForTesting;
import androidx.preference.Preference;
import androidx.preference.PreferenceFragmentCompat;
import android.content.SharedPreferences;

import org.chromium.base.ContextUtils;
import org.chromium.base.metrics.RecordUserAction;
import org.chromium.chrome.R;
import org.chromium.chrome.browser.translate.settings.RadioButtonGroupTranslatePreference.PreferenceValues;
import org.chromium.chrome.browser.settings.ChromeManagedPreferenceDelegate;
import org.chromium.components.browser_ui.settings.ChromeSwitchPreference;
import org.chromium.components.browser_ui.settings.SettingsUtils;
import org.chromium.components.embedder_support.util.UrlUtilities;
import org.chromium.components.url_formatter.UrlFormatter;

/**
 * Fragment that allows the user to configure translate related preferences.
 */
public class TranslateSettings extends PreferenceFragmentCompat {
    @VisibleForTesting
    public static final String PREF_TRANSLATE_SWITCH = "translate_switch";
    @VisibleForTesting
    public static final String PREF_TRANSLATE_RADIO_GROUP = "translate_radio_group";

    private RadioButtonGroupTranslatePreference mRadioButtons;

    @Override
    public void onCreatePreferences(Bundle savedInstanceState, String rootKey) {
        getActivity().setTitle(R.string.main_menu_translate);
        SettingsUtils.addPreferencesFromResource(this, R.xml.translate_preferences);

        mRadioButtons =
                (RadioButtonGroupTranslatePreference) findPreference(PREF_TRANSLATE_RADIO_GROUP);

        RecordUserAction.record("Settings.Translate.Opened");
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
        String checkedOption = ContextUtils.getAppSharedPreferences().getString("active_translator", "");
        return new PreferenceValues(checkedOption);
    }
}
