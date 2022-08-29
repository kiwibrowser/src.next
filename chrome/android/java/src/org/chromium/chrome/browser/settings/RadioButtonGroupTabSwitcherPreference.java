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

import org.chromium.chrome.browser.settings.ToolbarSettings;

import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;

/**
 * A radio button group Preference used for TabSwitcher Preference. It contains 2 options:
 * a {@link RadioButtonWithDescription} that represent Chrome NTP, and a
 * {@link RadioButtonWithEditText} that represents customized URL set by partner or user.
 */
public final class RadioButtonGroupTabSwitcherPreference
        extends Preference implements RadioGroup.OnCheckedChangeListener {
    /**
     * A data structure which holds the displayed value and the status for this preference.
     */
    static class PreferenceValues {
        /**
         * The option that is checked in {@link RadioButtonGroupTabSwitcherPreference}.
         */
        private String mCheckedOption;

        PreferenceValues(String checkedOption) {
            mCheckedOption = checkedOption;
        }

        /**
         * @return The option that is checked in {@link RadioButtonGroupTabSwitcherPreference}.
         */
        String getCheckedOption() {
            return mCheckedOption;
        }
    }

    private RadioButtonWithDescriptionLayout mGroup;

    private PreferenceValues mPreferenceValues;

    private PreferenceViewHolder mHolder;

    private Activity mActivity;

    public RadioButtonGroupTabSwitcherPreference(Context context, AttributeSet attrs) {
        super(context, attrs);

        // Inflating from XML.
        setLayoutResource(R.layout.radio_button_group_tabswitcher_preference);
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
        String checkedTabSwitcher = "";

        RadioButtonWithDescription mChoice_default = (RadioButtonWithDescription) mHolder.findViewById(R.id.radio_button_default);
        RadioButtonWithDescription mChoice_original = (RadioButtonWithDescription) mHolder.findViewById(R.id.radio_button_original);
        RadioButtonWithDescription mChoice_horizontal = (RadioButtonWithDescription) mHolder.findViewById(R.id.radio_button_horizontal);
        RadioButtonWithDescription mChoice_classic = (RadioButtonWithDescription) mHolder.findViewById(R.id.radio_button_classic);
        RadioButtonWithDescription mChoice_list = (RadioButtonWithDescription) mHolder.findViewById(R.id.radio_button_list);
        RadioButtonWithDescription mChoice_grid = (RadioButtonWithDescription) mHolder.findViewById(R.id.radio_button_grid);

        if (mChoice_list.isChecked()) {
          checkedTabSwitcher = "list";
        } else if (mChoice_original.isChecked()) {
          checkedTabSwitcher = "original";
        } else if (mChoice_horizontal.isChecked()) {
          checkedTabSwitcher = "horizontal";
        } else if (mChoice_grid.isChecked()) {
          checkedTabSwitcher = "grid";
        } else if (mChoice_classic.isChecked()) {
          checkedTabSwitcher = "classic";
        } else {
          checkedTabSwitcher = "default";
        }

        SharedPreferences.Editor sharedPreferencesEditor = ContextUtils.getAppSharedPreferences().edit();
        sharedPreferencesEditor.putString("active_tabswitcher", checkedTabSwitcher);
        sharedPreferencesEditor.apply();
        ContextUtils.getAppSharedPreferences().edit().putBoolean("accessibility_tab_switcher", false).apply();
        ToolbarSettings.AskForRelaunch(mActivity);
    }

    public void setActivity(Activity activity) {
        mActivity = activity;
    }

    @Override
    public void onBindViewHolder(PreferenceViewHolder holder) {
        super.onBindViewHolder(holder);

        RadioButtonWithDescription mChoice_default = (RadioButtonWithDescription) holder.findViewById(R.id.radio_button_default);
        RadioButtonWithDescription mChoice_original = (RadioButtonWithDescription) holder.findViewById(R.id.radio_button_original);
        RadioButtonWithDescription mChoice_horizontal = (RadioButtonWithDescription) holder.findViewById(R.id.radio_button_horizontal);
        RadioButtonWithDescription mChoice_classic = (RadioButtonWithDescription) holder.findViewById(R.id.radio_button_classic);
        RadioButtonWithDescription mChoice_list = (RadioButtonWithDescription) holder.findViewById(R.id.radio_button_list);
        RadioButtonWithDescription mChoice_grid = (RadioButtonWithDescription) holder.findViewById(R.id.radio_button_grid);

        mChoice_default.setPrimaryText("Default");
        if (ContextUtils.getAppSharedPreferences().getString("active_tabswitcher", "default").equals("default"))
          mChoice_default.setChecked(true);

        mChoice_original.setPrimaryText("Original (vertical, same as old Chromium)");
        if (ContextUtils.getAppSharedPreferences().getString("active_tabswitcher", "default").equals("original"))
          mChoice_original.setChecked(true);

        mChoice_horizontal.setPrimaryText("Horizontal (same as old Chromium)");
        if (ContextUtils.getAppSharedPreferences().getString("active_tabswitcher", "default").equals("horizontal"))
          mChoice_horizontal.setChecked(true);

        mChoice_classic.setPrimaryText("Vertical (supports tab group)");
        if (ContextUtils.getAppSharedPreferences().getString("active_tabswitcher", "default").equals("classic"))
          mChoice_classic.setChecked(true);

        mChoice_list.setPrimaryText("List");
        if (ContextUtils.getAppSharedPreferences().getString("active_tabswitcher", "default").equals("list"))
          mChoice_list.setChecked(true);

        mChoice_grid.setPrimaryText("Grid (supports tab group)");
        if (ContextUtils.getAppSharedPreferences().getString("active_tabswitcher", "default").equals("grid"))
          mChoice_grid.setChecked(true);

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
