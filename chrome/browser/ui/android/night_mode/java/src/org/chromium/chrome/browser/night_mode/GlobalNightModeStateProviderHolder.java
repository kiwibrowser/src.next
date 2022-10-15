// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.night_mode;

import androidx.annotation.NonNull;
import androidx.annotation.VisibleForTesting;
import androidx.appcompat.app.AppCompatDelegate;

import org.chromium.base.CommandLine;
import org.chromium.chrome.browser.flags.ChromeSwitches;
import org.chromium.chrome.browser.preferences.SharedPreferencesManager;

/**
 * Holds an instance of {@link NightModeStateProvider} that provides night mode state for the entire
 * application.
 */
public class GlobalNightModeStateProviderHolder {
    private static NightModeStateProvider sInstance;

    /**
     * Created when night mode is not available or not supported.
     */
    private static class DummyNightModeStateProvider implements NightModeStateProvider {
        final boolean mIsNightModeForceEnabled;

        private DummyNightModeStateProvider() {
            mIsNightModeForceEnabled =
                    CommandLine.getInstance().hasSwitch(ChromeSwitches.FORCE_ENABLE_NIGHT_MODE);
            // Always stay in night mode if night mode is force enabled, and always stay in light
            // mode if night mode is not available.
            AppCompatDelegate.setDefaultNightMode(mIsNightModeForceEnabled
                            ? AppCompatDelegate.MODE_NIGHT_YES
                            : AppCompatDelegate.MODE_NIGHT_NO);
        }

        @Override
        public boolean isInNightMode() {
            return mIsNightModeForceEnabled;
        }

        @Override
        public void addObserver(@NonNull Observer observer) {}

        @Override
        public void removeObserver(@NonNull Observer observer) {}
    }

    /**
     * @return The {@link NightModeStateProvider} that maintains the night mode state for the entire
     *         application. Note that UI widgets should use ColorUtils#inNightMode(Context) using
     *         the activity context they are attached to, because the night mode state can be
     *         overridden at the activity level.
     */
    public static NightModeStateProvider getInstance() {
        if (sInstance == null) {
            if (CommandLine.getInstance().hasSwitch(ChromeSwitches.FORCE_ENABLE_NIGHT_MODE)
                    || !NightModeUtils.isNightModeSupported()) {
                sInstance = new DummyNightModeStateProvider();
            } else {
                sInstance = new GlobalNightModeStateController(SystemNightModeMonitor.getInstance(),
                        PowerSavingModeMonitor.getInstance(),
                        SharedPreferencesManager.getInstance());
            }
        }
        return sInstance;
    }

    @VisibleForTesting
    static void setInstanceForTesting(NightModeStateProvider instance) {
        sInstance = instance;
    }
}
