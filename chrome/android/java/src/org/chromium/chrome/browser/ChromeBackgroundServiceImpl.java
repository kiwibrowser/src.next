// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser;

import android.content.Context;

import androidx.annotation.VisibleForTesting;

import com.google.android.gms.gcm.GcmNetworkManager;
import com.google.android.gms.gcm.TaskParams;

import org.chromium.base.Log;
import org.chromium.base.task.PostTask;
import org.chromium.chrome.browser.background_sync.BackgroundSyncBackgroundTaskScheduler;
import org.chromium.chrome.browser.init.ChromeBrowserInitializer;
import org.chromium.chrome.browser.init.MinimalBrowserStartupUtils;
import org.chromium.chrome.browser.offlinepages.BackgroundScheduler;
import org.chromium.chrome.browser.offlinepages.OfflinePageUtils;
import org.chromium.content_public.browser.UiThreadTaskTraits;

/**
 * {@link ChromeBackgroundService} is scheduled through the {@link GcmNetworkManager} when the
 * browser needs to be launched for scheduled tasks, or in response to changing network or power
 * conditions.
 */
public class ChromeBackgroundServiceImpl extends ChromeBackgroundService.Impl {
    private static final String TAG = "BackgroundService";

    @Override
    @VisibleForTesting
    public int onRunTask(final TaskParams params) {
        final String taskTag = params.getTag();
        final Context context = getService();
        PostTask.runOrPostTask(UiThreadTaskTraits.DEFAULT, () -> {
            switch (taskTag) {
                case BackgroundSyncBackgroundTaskScheduler.TASK_TAG:
                    // Background Sync tasks are now scheduled using BackgroundTaskScheduler.
                    // This should be rare, and we simply reschedule using BackgroundTaskScheduler.
                    rescheduleOneShotBackgroundSyncTasks();
                    break;

                case OfflinePageUtils.TASK_TAG:
                    // Offline pages are migrating to BackgroundTaskScheduler, therefore getting
                    // a task through ChromeBackgroundService should cause a rescheduling using
                    // the new component.
                    rescheduleOfflinePages();
                    break;

                // This is only for tests.
                case MinimalBrowserStartupUtils.TASK_TAG:
                    handleServicificationStartupTask(context, taskTag);
                    break;

                default:
                    Log.i(TAG, "Unknown task tag " + taskTag);
                    break;
            }
        });

        return GcmNetworkManager.RESULT_SUCCESS;
    }

    private void handleServicificationStartupTask(Context context, String tag) {
        launchBrowser(context, tag);
    }

    @VisibleForTesting
    protected void launchBrowser(Context context, String tag) {
        Log.i(TAG, "Launching browser");
        ChromeBrowserInitializer.getInstance().handleSynchronousStartup();
    }

    @VisibleForTesting
    protected void rescheduleBackgroundSyncTasksOnUpgrade() {
        rescheduleOneShotBackgroundSyncTasks();
    }

    /** Reschedules offline pages (using appropriate version of Background Task Scheduler). */
    private void rescheduleOfflinePages() {
        BackgroundScheduler.getInstance().reschedule();
    }

    private void rescheduleOneShotBackgroundSyncTasks() {
        BackgroundSyncBackgroundTaskScheduler.getInstance().reschedule(
                BackgroundSyncBackgroundTaskScheduler.BackgroundSyncTask
                        .ONE_SHOT_SYNC_CHROME_WAKE_UP);
    }

    @Override
    public void onInitializeTasks() {
        rescheduleBackgroundSyncTasksOnUpgrade();
    }
}
