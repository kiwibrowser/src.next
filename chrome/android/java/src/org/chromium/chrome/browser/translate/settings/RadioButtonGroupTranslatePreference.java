// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.translate.settings;

import android.content.Context;
import android.util.AttributeSet;
import android.view.View;
import android.widget.RadioGroup;
import android.widget.TextView;

import androidx.annotation.IntDef;
import androidx.annotation.NonNull;
import androidx.annotation.VisibleForTesting;
import androidx.preference.Preference;
import androidx.preference.PreferenceViewHolder;

import android.content.SharedPreferences;
import org.chromium.chrome.R;
import org.chromium.base.ContextUtils;
import org.chromium.components.browser_ui.widget.RadioButtonWithDescription;
import org.chromium.components.browser_ui.widget.RadioButtonWithDescriptionLayout;

import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;

/**
 * A radio button group Preference used for Translate Preference. It contains 2 options:
 * a {@link RadioButtonWithDescription} that represent Chrome NTP, and a
 * {@link RadioButtonWithEditText} that represents customized URL set by partner or user.
 */
public final class RadioButtonGroupTranslatePreference
        extends Preference implements RadioGroup.OnCheckedChangeListener {
    /**
     * A data structure which holds the displayed value and the status for this preference.
     */
    static class PreferenceValues {
        /**
         * The option that is checked in {@link RadioButtonGroupTranslatePreference}.
         */
        private String mCheckedOption;

        PreferenceValues(String checkedOption) {
            mCheckedOption = checkedOption;
        }

        /**
         * @return The option that is checked in {@link RadioButtonGroupTranslatePreference}.
         */
        String getCheckedOption() {
            return mCheckedOption;
        }
    }

    private RadioButtonWithDescriptionLayout mGroup;

    private PreferenceValues mPreferenceValues;

    private PreferenceViewHolder mHolder;

    public RadioButtonGroupTranslatePreference(Context context, AttributeSet attrs) {
        super(context, attrs);

        // Inflating from XML.
        setLayoutResource(R.layout.radio_button_group_translate_preference);
    }

    /**
     * Called when the checked radio button has changed. When the selection is cleared, checkedId is
     * -1.
     *
     * @param group The group in which the checked radio button has changed
     * @param checkedId The unique identifier of the newly checked radio button
     */
    @Override
    public void onCheckedChanged(RadioGroup group, int checkedId) {
        String checkedTranslator = "";

        RadioButtonWithDescription mChoice_google_translate = (RadioButtonWithDescription) mHolder.findViewById(R.id.radio_button_google_translate);
        RadioButtonWithDescription mChoice_microsoft_translator = (RadioButtonWithDescription) mHolder.findViewById(R.id.radio_button_microsoft_translator);
        RadioButtonWithDescription mChoice_yandex_translator = (RadioButtonWithDescription) mHolder.findViewById(R.id.radio_button_yandex_translator);
        RadioButtonWithDescription mChoice_baidu_fanyi = (RadioButtonWithDescription) mHolder.findViewById(R.id.radio_button_baidu_fanyi);

        if (mChoice_google_translate.isChecked()) {
          checkedTranslator = "Google";
        }
        else if (mChoice_microsoft_translator.isChecked()) {
          checkedTranslator = "Microsoft Translator";
        }
        else if (mChoice_yandex_translator.isChecked()) {
          checkedTranslator = "Yandex";
        }
        else if (mChoice_baidu_fanyi.isChecked()) {
          checkedTranslator = "Baidu";
        } else {
          checkedTranslator = "";
        }

        SharedPreferences.Editor sharedPreferencesEditor = ContextUtils.getAppSharedPreferences().edit();
        sharedPreferencesEditor.putString("active_translator", checkedTranslator);
        sharedPreferencesEditor.apply();
    }

    @Override
    public void onBindViewHolder(PreferenceViewHolder holder) {
        super.onBindViewHolder(holder);
        RadioButtonWithDescription mChoice_default = (RadioButtonWithDescription) holder.findViewById(R.id.radio_button_default);
        mChoice_default.setPrimaryText("Default (Google integrated)");
        if (ContextUtils.getAppSharedPreferences().getString("active_translator", "Default") == "" || ContextUtils.getAppSharedPreferences().getString("active_translator", "Default") == "Default")
           mChoice_default.setChecked(true);
        RadioButtonWithDescription mChoice_google_translate = (RadioButtonWithDescription) holder.findViewById(R.id.radio_button_google_translate);
        mChoice_google_translate.setPrimaryText("Google Translate (web)");
        if (ContextUtils.getAppSharedPreferences().getString("active_translator", "Default") == "Google")
          mChoice_google_translate.setChecked(true);
        RadioButtonWithDescription mChoice_microsoft_translator = (RadioButtonWithDescription) holder.findViewById(R.id.radio_button_microsoft_translator);
        mChoice_microsoft_translator.setPrimaryText("Microsoft Translator");
        if (ContextUtils.getAppSharedPreferences().getString("active_translator", "Default") == "Microsoft Translator")
          mChoice_microsoft_translator.setChecked(true);
        RadioButtonWithDescription mChoice_yandex_translator = (RadioButtonWithDescription) holder.findViewById(R.id.radio_button_yandex_translator);
        mChoice_yandex_translator.setPrimaryText("Yandex Translate");
        if (ContextUtils.getAppSharedPreferences().getString("active_translator", "Default") == "Yandex")
          mChoice_yandex_translator.setChecked(true);
        RadioButtonWithDescription mChoice_baidu_fanyi = (RadioButtonWithDescription) holder.findViewById(R.id.radio_button_baidu_fanyi);
        mChoice_baidu_fanyi.setPrimaryText("Baidu Fanyi");
        if (ContextUtils.getAppSharedPreferences().getString("active_translator", "Default") == "Baidu")
          mChoice_baidu_fanyi.setChecked(true);

        mGroup = (RadioButtonWithDescriptionLayout) holder.findViewById(R.id.radio_button_group);
        mGroup.setOnCheckedChangeListener(this);
        mHolder = holder;
    }

    /**
     * @return The current preference value stored in the preference.
     */
    PreferenceValues getPreferenceValue() {
        return mPreferenceValues;
    }
}
