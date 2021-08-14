// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.settings;

import android.app.Activity;

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

import org.chromium.chrome.browser.accessibility.settings.AccessibilitySettings;

import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;

import org.chromium.chrome.browser.night_mode.WebContentsDarkModeController;

/**
 * A radio button group Preference used for NightMode Preference. It contains 2 options:
 * a {@link RadioButtonWithDescription} that represent Chrome NTP, and a
 * {@link RadioButtonWithEditText} that represents customized URL set by partner or user.
 */
public final class RadioButtonGroupNightModePreference
        extends Preference implements RadioGroup.OnCheckedChangeListener {
    /**
     * A data structure which holds the displayed value and the status for this preference.
     */
    static class PreferenceValues {
        /**
         * The option that is checked in {@link RadioButtonGroupNightModePreference}.
         */
        private String mCheckedOption;

        PreferenceValues(String checkedOption) {
            mCheckedOption = checkedOption;
        }

        /**
         * @return The option that is checked in {@link RadioButtonGroupNightModePreference}.
         */
        String getCheckedOption() {
            return mCheckedOption;
        }
    }

    private RadioButtonWithDescriptionLayout mGroup;

    private PreferenceValues mPreferenceValues;

    private PreferenceViewHolder mHolder;

    private Activity mActivity;

    public RadioButtonGroupNightModePreference(Context context, AttributeSet attrs) {
        super(context, attrs);

        // Inflating from XML.
        setLayoutResource(R.layout.radio_button_group_nightmode_preference);
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
        String checkedNightMode = "";

        RadioButtonWithDescription mChoice_default = (RadioButtonWithDescription) mHolder.findViewById(R.id.radio_button_default);
        RadioButtonWithDescription mChoice_amoled = (RadioButtonWithDescription) mHolder.findViewById(R.id.radio_button_amoled);
        RadioButtonWithDescription mChoice_amoled_grayscale = (RadioButtonWithDescription) mHolder.findViewById(R.id.radio_button_amoled_grayscale);
        RadioButtonWithDescription mChoice_gray = (RadioButtonWithDescription) mHolder.findViewById(R.id.radio_button_gray);
        RadioButtonWithDescription mChoice_gray_grayscale = (RadioButtonWithDescription) mHolder.findViewById(R.id.radio_button_gray_grayscale);
        RadioButtonWithDescription mChoice_high_contrast = (RadioButtonWithDescription) mHolder.findViewById(R.id.radio_button_high_contrast);

        if (mChoice_gray_grayscale.isChecked()) {
          checkedNightMode = "gray_grayscale";
        } else if (mChoice_amoled.isChecked()) {
          checkedNightMode = "amoled";
        } else if (mChoice_amoled_grayscale.isChecked()) {
          checkedNightMode = "amoled_grayscale";
        } else if (mChoice_high_contrast.isChecked()) {
          checkedNightMode = "high_contrast";
        } else if (mChoice_gray.isChecked()) {
          checkedNightMode = "gray";
        } else {
          checkedNightMode = "default";
        }

        SharedPreferences.Editor sharedPreferencesEditor = ContextUtils.getAppSharedPreferences().edit();
        sharedPreferencesEditor.putString("active_nightmode", checkedNightMode);
        sharedPreferencesEditor.apply();
        WebContentsDarkModeController.updateDarkModeStringSettings();
        AccessibilitySettings.AskForRelaunch(mActivity);
    }

    public void setActivity(Activity activity) {
        mActivity = activity;
    }

    @Override
    public void onBindViewHolder(PreferenceViewHolder holder) {
        super.onBindViewHolder(holder);

        RadioButtonWithDescription mChoice_default = (RadioButtonWithDescription) holder.findViewById(R.id.radio_button_default);
        RadioButtonWithDescription mChoice_amoled = (RadioButtonWithDescription) holder.findViewById(R.id.radio_button_amoled);
        RadioButtonWithDescription mChoice_amoled_grayscale = (RadioButtonWithDescription) holder.findViewById(R.id.radio_button_amoled_grayscale);
        RadioButtonWithDescription mChoice_gray = (RadioButtonWithDescription) holder.findViewById(R.id.radio_button_gray);
        RadioButtonWithDescription mChoice_gray_grayscale = (RadioButtonWithDescription) holder.findViewById(R.id.radio_button_gray_grayscale);
        RadioButtonWithDescription mChoice_high_contrast = (RadioButtonWithDescription) holder.findViewById(R.id.radio_button_high_contrast);

        mChoice_default.setPrimaryText("Default");
        if (ContextUtils.getAppSharedPreferences().getString("active_nightmode", "default").equals("default"))
          mChoice_default.setChecked(true);

        mChoice_amoled.setPrimaryText("Optimized for AMOLED devices (black background, color images)");
        if (ContextUtils.getAppSharedPreferences().getString("active_nightmode", "default").equals("amoled"))
          mChoice_amoled.setChecked(true);

        mChoice_amoled_grayscale.setPrimaryText("Optimized for AMOLED devices (black background, some images in grayscale)");
        if (ContextUtils.getAppSharedPreferences().getString("active_nightmode", "default").equals("amoled_grayscale"))
          mChoice_amoled_grayscale.setChecked(true);

        mChoice_gray.setPrimaryText("Gray background (color images)");
        if (ContextUtils.getAppSharedPreferences().getString("active_nightmode", "default").equals("gray"))
          mChoice_gray.setChecked(true);

        mChoice_gray_grayscale.setPrimaryText("Gray background (some images in grayscale)");
        if (ContextUtils.getAppSharedPreferences().getString("active_nightmode", "default").equals("gray_grayscale"))
          mChoice_gray_grayscale.setChecked(true);

        mChoice_high_contrast.setPrimaryText("High-contrast");
        if (ContextUtils.getAppSharedPreferences().getString("active_nightmode", "default").equals("high_contrast"))
          mChoice_high_contrast.setChecked(true);

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
