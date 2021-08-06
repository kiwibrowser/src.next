// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.night_mode;

import static org.chromium.chrome.browser.preferences.ChromePreferenceKeys.UI_THEME_DARKEN_WEBSITES_ENABLED;

import android.text.TextUtils;

import org.chromium.base.ApplicationState;
import org.chromium.base.ApplicationStatus;
import org.chromium.base.ApplicationStatus.ApplicationStateListener;
import org.chromium.base.ContextUtils;
import org.chromium.chrome.browser.preferences.Pref;
import org.chromium.chrome.browser.preferences.SharedPreferencesManager;
import org.chromium.chrome.browser.profiles.Profile;
import org.chromium.components.user_prefs.UserPrefs;

import android.content.SharedPreferences.Editor;
import org.chromium.base.Log;

import org.chromium.base.SysUtils;
import java.text.DecimalFormat;

/**
 * A controller class could enable or disable web content dark mode feature based on the night mode
 * and the user preference
 */
public class WebContentsDarkModeController implements ApplicationStateListener {
    private NightModeStateProvider.Observer mNightModeObserver;
    private SharedPreferencesManager.Observer mPreferenceObserver;

    private static WebContentsDarkModeController sController;

    private WebContentsDarkModeController() {
        enableWebContentsDarkMode(shouldEnableWebContentsDarkMode());
        final int applicationState = ApplicationStatus.getStateForApplication();
        if (applicationState == ApplicationState.HAS_RUNNING_ACTIVITIES
                || applicationState == ApplicationState.HAS_PAUSED_ACTIVITIES) {
            start();
        }
        ApplicationStatus.registerApplicationStateListener(this);
    }

    /**
     * @return The instance can enable or disable the feature. Call the start method to listen
     * the user setting and app night mode change so that the instance can automatically apply the
     * change. Call the stop method to stop the listening.
     */
    public static WebContentsDarkModeController createInstance() {
        if (sController == null) {
            sController = new WebContentsDarkModeController();
        }
        return sController;
    }

    // copy-paste of the setting in AccessibilityPreferences
    private static float getUserNightModeFactor() {
        float nightFactor = ContextUtils.getAppSharedPreferences().getFloat("user_night_mode_factor", 0.99f);
        return nightFactor;
    }


    public static void updateDarkModeStringSettings() {
        String nightModeSettings = "";
        Log.i("Kiwi", "SetContentCommandLineFlags - Setting new dark mode settings to [" + nightModeSettings + "]");

        if (ContextUtils.getAppSharedPreferences().getString("active_nightmode", "default").equals("default") || ContextUtils.getAppSharedPreferences().getString("active_nightmode", "default").equals("amoled")) {
          nightModeSettings = "ContrastPercent=0,"; // -1 to 1
        } else if (ContextUtils.getAppSharedPreferences().getString("active_nightmode", "default").equals("amoled_grayscale")) {
          nightModeSettings = "ContrastPercent=0,"; // -1 to 1
          nightModeSettings += "IsGrayScale=1,ImageGrayScalePercent=1.0,ImagePolicy=0,";
        } else if (ContextUtils.getAppSharedPreferences().getString("active_nightmode", "default").equals("gray")) {
          nightModeSettings = "ContrastPercent=0.15,"; // -1 to 1
        } else if (ContextUtils.getAppSharedPreferences().getString("active_nightmode", "default").equals("gray_grayscale")) {
          nightModeSettings = "ContrastPercent=0.15,"; // -1 to 1
          nightModeSettings += "IsGrayScale=1,ImageGrayScalePercent=1.0,ImagePolicy=0,";
        } else if (ContextUtils.getAppSharedPreferences().getString("active_nightmode", "default").equals("high_contrast")) {
          nightModeSettings = "ContrastPercent=-0.15,"; // -1 to 1
        }

        SharedPreferencesManager.getInstance().writeStringUnchecked("night_mode_settings", nightModeSettings);
    }

    /**
     * Enable or Disable web content dark mode
     * @param enabled the new state of the web content dark mode
     */
    private static void enableWebContentsDarkMode(boolean enabled) {
        UserPrefs.get(Profile.getLastUsedRegularProfile())
                .setBoolean(Pref.WEB_KIT_FORCE_DARK_MODE_ENABLED, enabled);
        updateDarkModeStringSettings();
    }

    private static boolean shouldEnableWebContentsDarkMode() {
        return GlobalNightModeStateProviderHolder.getInstance().isInNightMode()
                && SharedPreferencesManager.getInstance().readBoolean(
                        UI_THEME_DARKEN_WEBSITES_ENABLED, false);
    }

    /**
     * start listening to any event can enable or disable web content dark mode
     */
    private void start() {
        if (mNightModeObserver != null) return;
        mNightModeObserver = () -> enableWebContentsDarkMode(shouldEnableWebContentsDarkMode());
        mPreferenceObserver = (key) -> {
            if (TextUtils.equals(key, UI_THEME_DARKEN_WEBSITES_ENABLED)) {
                enableWebContentsDarkMode(shouldEnableWebContentsDarkMode());
            }
        };
        enableWebContentsDarkMode(shouldEnableWebContentsDarkMode());
        GlobalNightModeStateProviderHolder.getInstance().addObserver(mNightModeObserver);
        SharedPreferencesManager.getInstance().addObserver(mPreferenceObserver);
    }

    /**
     * stop listening to any event can enable or disable web content dark mode
     */
    private void stop() {
        if (mNightModeObserver == null) return;
        GlobalNightModeStateProviderHolder.getInstance().removeObserver(mNightModeObserver);
        SharedPreferencesManager.getInstance().removeObserver(mPreferenceObserver);
        mNightModeObserver = null;
        mPreferenceObserver = null;
    }

    @Override
    public void onApplicationStateChange(int newState) {
        if (newState == ApplicationState.HAS_RUNNING_ACTIVITIES) {
            start();
        } else if (newState == ApplicationState.HAS_STOPPED_ACTIVITIES) {
            stop();
        }
    }
}
