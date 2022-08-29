// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser;

import androidx.annotation.VisibleForTesting;

import org.chromium.base.metrics.RecordHistogram;
import org.chromium.chrome.browser.lifecycle.ActivityLifecycleDispatcher;
import org.chromium.chrome.browser.lifecycle.DestroyObserver;
import org.chromium.chrome.browser.lifecycle.PauseResumeWithNativeObserver;
import org.chromium.chrome.browser.lifecycle.StartStopWithNativeObserver;
import org.chromium.chrome.browser.preferences.SharedPreferencesManager;

/**
 * Manages pref that can track the delay since the last stop of the tracked activity.
 * TODO(crbug.com/1081453): Split ChromeInactivityTracker out from ChromeTabbedActivity.
 */
public class ChromeInactivityTracker
        implements StartStopWithNativeObserver, PauseResumeWithNativeObserver, DestroyObserver {
    private static final String TAG = "InactivityTracker";
    private static final String UMA_DURATION_SINCE_LAST_BACKGROUND_TIME =
            "Startup.Android.DurationSinceLastBackgroundTime";

    private static final long UNKNOWN_LAST_BACKGROUNDED_TIME = -1;

    private final String mPrefName;
    private ActivityLifecycleDispatcher mLifecycleDispatcher;

    /**
     * Creates an inactivity tracker without a timeout callback. This is useful if clients only
     * want to query the inactivity state manually.
     * @param prefName the location in shared preferences that the timestamp is stored.
     */
    public ChromeInactivityTracker(String prefName) {
        mPrefName = prefName;
    }

    /**
     * Registers to the given lifecycle dispatcher.
     * @param lifecycleDispatcher tracks the lifecycle of the Activity of interest, and calls
     *     observer methods on ChromeInactivityTracker.
     */
    public void register(ActivityLifecycleDispatcher lifecycleDispatcher) {
        mLifecycleDispatcher = lifecycleDispatcher;
        mLifecycleDispatcher.register(this);
    }

    /**
     * Updates the shared preferences to contain the given time. Used internally and for tests.
     * @param timeInMillis the time to record.
     */
    @VisibleForTesting
    public void setLastBackgroundedTimeInPrefs(long timeInMillis) {
        SharedPreferencesManager.getInstance().writeLong(mPrefName, timeInMillis);
    }

    /**
     * @return The last backgrounded time in millis.
     */
    public long getLastBackgroundedTimeMs() {
        return SharedPreferencesManager.getInstance().readLong(
                mPrefName, UNKNOWN_LAST_BACKGROUNDED_TIME);
    }

    /**
     * @return the time interval in millis since the last backgrounded time.
     */
    public long getTimeSinceLastBackgroundedMs() {
        long lastBackgroundedTimeMs = getLastBackgroundedTimeMs();
        if (lastBackgroundedTimeMs == UNKNOWN_LAST_BACKGROUNDED_TIME) {
            return UNKNOWN_LAST_BACKGROUNDED_TIME;
        }
        return System.currentTimeMillis() - lastBackgroundedTimeMs;
    }

    @Override
    public void onStartWithNative() {}

    @Override
    public void onResumeWithNative() {
        // We clear the backgrounded time here, rather than in #onStartWithNative, to give
        // handlers the chance to respond to inactivity during any onStartWithNative handler
        // regardless of ordering. onResume is always called after onStart, and it should be fine to
        // consider Chrome active if it reaches onResume.
        long lastBackgroundTime = SharedPreferencesManager.getInstance().readLong(
                mPrefName, UNKNOWN_LAST_BACKGROUNDED_TIME);
        setLastBackgroundedTimeInPrefs(UNKNOWN_LAST_BACKGROUNDED_TIME);

        if (lastBackgroundTime != UNKNOWN_LAST_BACKGROUNDED_TIME) {
            RecordHistogram.recordLongTimesHistogram100(UMA_DURATION_SINCE_LAST_BACKGROUND_TIME,
                    System.currentTimeMillis() - lastBackgroundTime);
        }
    }

    @Override
    public void onPauseWithNative() {}

    @Override
    public void onStopWithNative() {
        // Always track the last backgrounded time in case others are using the pref.
        setLastBackgroundedTimeInPrefs(System.currentTimeMillis());
    }

    @Override
    public void onDestroy() {
        mLifecycleDispatcher.unregister(this);
    }
}
