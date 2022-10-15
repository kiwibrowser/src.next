// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.tasks.tab_management;

import android.graphics.Bitmap;
import android.graphics.Rect;
import android.os.SystemClock;
import android.view.ViewGroup;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.annotation.VisibleForTesting;

import org.chromium.base.Callback;
import org.chromium.base.supplier.ObservableSupplier;
import org.chromium.base.supplier.Supplier;
import org.chromium.chrome.browser.compositor.layouts.Layout;
import org.chromium.chrome.browser.tab.Tab;
import org.chromium.chrome.browser.tasks.tab_management.TabManagementDelegate.TabSwitcherType;
import org.chromium.components.browser_ui.widget.gesture.BackPressHandler;
import org.chromium.ui.resources.dynamics.ViewResourceAdapter;

import java.util.List;

/**
 * Interface for the Tab Switcher.
 */
public interface TabSwitcher {
    /**
     * Defines an interface to pass out tab selecting event.
     */
    interface OnTabSelectingListener {
        /**
         * Called when a tab is getting selected. Typically when exiting the overview mode.
         * @param time  The current time of the app in ms.
         * @param tabId The ID of selected {@link Tab}.
         * @see Layout#onTabSelecting(long, int)
         */
        void onTabSelecting(long time, int tabId);
    }

    /**
     * Set the listener to get the {@link Layout#onTabSelecting} event from the Tab Switcher.
     * @param listener The {@link OnTabSelectingListener} to use.
     */
    void setOnTabSelectingListener(OnTabSelectingListener listener);

    /**
     * Called when native initialization is completed.
     */
    void initWithNative();

    // TODO(1322733): Remove the following interfaces when we find a better way to notify the layout
    // that the GTS animation is finished.
    /**
     * An observer that is notified when the TabSwitcher view state changes.
     */
    interface TabSwitcherViewObserver {
        /**
         * Called when tab switcher starts showing.
         */
        void startedShowing();

        /**
         * Called when tab switcher finishes showing.
         */
        void finishedShowing();

        /**
         * Called when tab switcher starts hiding.
         */
        void startedHiding();

        /**
         * Called when tab switcher finishes hiding.
         */
        void finishedHiding();
    }

    /**
     * Interface to control the TabSwitcher.
     */
    interface Controller extends BackPressHandler {
        /**
         * @return Whether or not the overview {@link Layout} is visible.
         */
        // TODO(crbug.com/1315676): Remove this method after removing the usage in
        // StartSurfaceMediator.
        @Deprecated // This method will be removed, please do not use.
        boolean overviewVisible();

        /**
         * @param listener Registers {@code listener} for tab switcher status changes.
         */
        void addTabSwitcherViewObserver(TabSwitcherViewObserver listener);

        /**
         * @param listener Unregisters {@code listener} for tab switcher status changes.
         */
        void removeTabSwitcherViewObserver(TabSwitcherViewObserver listener);

        /**
         * Before tab switcher starts hiding.
         */
        void prepareHideTabSwitcherView();

        /**
         * Hide the tab switcher view.
         * @param animate Whether we should animate while hiding.
         */
        void hideTabSwitcherView(boolean animate);

        /**
         * Show the tab switcher view.
         * @param animate Whether we should animate while showing.
         */
        void showTabSwitcherView(boolean animate);

        /**
         * Called by the StartSurfaceLayout when the system back button is pressed.
         * @return Whether or not the TabSwitcher consumed the event.
         * @param isOnHomepage Whether the Start surface is showing.
         */
        // TODO(crbug.com/1315676): Remove the parameter when tab switcher and start surface are
        // decoupled.
        boolean onBackPressed(boolean isOnHomepage);

        /**
         * Enable recording the first meaningful paint event of the Grid Tab Switcher.
         * @param activityCreateTimeMs {@link SystemClock#elapsedRealtime} at activity creation.
         */
        void enableRecordingFirstMeaningfulPaint(long activityCreateTimeMs);

        /**
         * Called after the Chrome activity is launched.
         * @param activityCreationTimeMs {@link SystemClock#elapsedRealtime} at activity creation.
         */
        // TODO(crbug.com/1315676): Remove this API when tab switcher and start surface are
        // decoupled.
        void onOverviewShownAtLaunch(long activityCreationTimeMs);

        /**
         * @return Whether any dialog is opened.
         */
        boolean isDialogVisible();

        /**
         * @return An {@link ObservableSupplier<Boolean>} which yields true if any dialog is opened.
         */
        ObservableSupplier<Boolean> isDialogVisibleSupplier();

        /**
         * Shows the TabSelectionEditor.
         */
        @VisibleForTesting
        default void showTabSelectionEditor(List<Tab> tabs) {}

        /**
         * Returns the tab switcher type.
         */
        @TabSwitcherType
        int getTabSwitcherType();

        /**
         * Called when start surface is showing or hiding.
         * @param isOnHomepage Whether the Start surface is showing.
         */
        // TODO(crbug.com/1315676): Remove this API when tab switcher and start surface are
        // decoupled.
        void onHomepageChanged(boolean isOnHomepage);

        /**
         * Sets the parent view for snackbars. If <code>null</code> is given, the original parent
         * view is restored.
         *
         * @param parentView The {@link ViewGroup} to attach snackbars to.
         */
        default void setSnackbarParentView(ViewGroup parentView){};

        /**
         * @return The Tab switcher container view.
         */
        default ViewGroup getTabSwitcherContainer() {
            return null;
        }
    }

    /**
     * @return Controller implementation that can be used for controlling
     *         visibility changes.
     */
    Controller getController();

    /**
     * Interface to access the Tab List.
     */
    interface TabListDelegate {
        /**
         * @return The dynamic resource ID of the TabSwitcher RecyclerView.
         */
        int getResourceId();

        /**
         * @return The timestamp of last dirty event of {@link ViewResourceAdapter} of
         * {@link TabListRecyclerView}.
         */
        long getLastDirtyTime();

        /**
         * Before calling {@link Controller#showTabSwitcherView} to start showing the
         * TabSwitcher {@link TabListRecyclerView}, call this to populate it without making it
         * visible.
         * @return Whether the {@link TabListRecyclerView} can be shown quickly.
         */
        boolean prepareTabSwitcherView();

        /**
         * This is called after the compositor animation is done, for potential clean-up work.
         * {@link TabSwitcherViewObserver#finishedHiding} happens after
         * the Android View animation, but before the compositor animation.
         */
        void postHiding();

        /**
         * @param forceUpdate Whether to measure the current location again. If not, return the last
         *                    location measured on last layout, which can be wrong after scrolling.
         * @return The {@link Rect} of the thumbnail of the current tab, relative to the
         *         TabSwitcher {@link TabListRecyclerView} coordinates.
         */
        @NonNull
        Rect getThumbnailLocationOfCurrentTab(boolean forceUpdate);

        /**
         * Set a hook to receive all the {@link Bitmap}s returned by
         * {@link TabListMediator.ThumbnailFetcher} for testing.
         * @param callback The callback to send bitmaps through.
         */
        @VisibleForTesting
        void setBitmapCallbackForTesting(Callback<Bitmap> callback);

        /**
         * @return The number of thumbnail fetching for testing.
         */
        @VisibleForTesting
        int getBitmapFetchCountForTesting();

        /**
         * @return The soft cleanup delay for testing.
         */
        @VisibleForTesting
        int getSoftCleanupDelayForTesting();

        /**
         * @return The cleanup delay for testing.
         */
        @VisibleForTesting
        int getCleanupDelayForTesting();

        /**
         * @return The top offset from top toolbar to tab list. Used to adjust the animations for
         *         tab switcher.
         */
        int getTabListTopOffset();

        /**
         * @return The mode of the list of Tabs.
         */
        @VisibleForTesting
        int getListModeForTesting();

        /**
         * Request accessibility focus for the currently selected tab.
         */
        default void requestFocusOnCurrentTab(){};
    }

    /**
     * @return The {@link TabListDelegate}.
     */
    TabListDelegate getTabListDelegate();

    /**
     * @return {@link Supplier} that provides dialog visibility.
     */
    Supplier<Boolean> getTabGridDialogVisibilitySupplier();

    /**
     *  @return {@link TabSwitcherCustomViewManager} that allows to pass custom views to {@link
     *         TabSwitcherCoordinator}.
     */
    @Nullable
    TabSwitcherCustomViewManager getTabSwitcherCustomViewManager();
}
