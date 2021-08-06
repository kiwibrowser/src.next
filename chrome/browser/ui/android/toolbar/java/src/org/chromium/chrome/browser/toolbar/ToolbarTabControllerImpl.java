// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.toolbar;

import org.chromium.base.metrics.RecordHistogram;
import org.chromium.base.metrics.RecordUserAction;
import org.chromium.base.supplier.Supplier;
import org.chromium.chrome.browser.flags.ChromeFeatureList;
import org.chromium.chrome.browser.profiles.Profile;
import org.chromium.chrome.browser.tab.Tab;
import org.chromium.chrome.browser.toolbar.bottom.BottomControlsCoordinator;
import org.chromium.components.embedder_support.util.UrlConstants;
import org.chromium.components.embedder_support.util.UrlUtilities;
import org.chromium.components.feature_engagement.EventConstants;
import org.chromium.components.feature_engagement.Tracker;
import org.chromium.components.profile_metrics.BrowserProfileType;
import org.chromium.content_public.browser.LoadUrlParams;
import org.chromium.content_public.common.ContentUrlConstants;
import org.chromium.ui.base.PageTransition;

/**
 * Implementation of {@link ToolbarTabController}.
 */
public class ToolbarTabControllerImpl implements ToolbarTabController {
    private final Supplier<Tab> mTabSupplier;
    private final Supplier<Boolean> mOverrideHomePageSupplier;
    private final Supplier<Tracker> mTrackerSupplier;
    private final Supplier<BottomControlsCoordinator> mBottomControlsCoordinatorSupplier;
    private final Supplier<String> mHomepageUrlSupplier;
    private final Runnable mOnSuccessRunnable;

    /**
     *
     * @param tabSupplier Supplier for the currently active tab.
     * @param overrideHomePageSupplier Supplier that returns true if it overrides the default
     *         homepage behavior.
     * @param trackerSupplier Supplier for the current profile tracker.
     * @param homepageUrlSupplier Supplier for the homepage URL.
     * @param onSuccessRunnable Runnable that is invoked when the active tab is asked to perform the
     *         corresponding ToolbarTabController action; it is not invoked if the tab cannot
     *         perform the action or for openHompage.
     */
    public ToolbarTabControllerImpl(Supplier<Tab> tabSupplier,
            Supplier<Boolean> overrideHomePageSupplier, Supplier<Tracker> trackerSupplier,
            Supplier<BottomControlsCoordinator> bottomControlsCoordinatorSupplier,
            Supplier<String> homepageUrlSupplier, Runnable onSuccessRunnable) {
        mTabSupplier = tabSupplier;
        mOverrideHomePageSupplier = overrideHomePageSupplier;
        mTrackerSupplier = trackerSupplier;
        mBottomControlsCoordinatorSupplier = bottomControlsCoordinatorSupplier;
        mHomepageUrlSupplier = homepageUrlSupplier;
        mOnSuccessRunnable = onSuccessRunnable;
    }

    @Override
    public boolean back() {
        BottomControlsCoordinator controlsCoordinator = mBottomControlsCoordinatorSupplier.get();
        if (controlsCoordinator != null && controlsCoordinator.onBackPressed()) {
            return true;
        }

        Tab tab = mTabSupplier.get();
        if (tab != null && tab.canGoBack()) {
            tab.goBack();
            mOnSuccessRunnable.run();
            return true;
        }
        return false;
    }

    @Override
    public boolean forward() {
        Tab tab = mTabSupplier.get();
        if (tab != null && tab.canGoForward()) {
            tab.goForward();
            mOnSuccessRunnable.run();
            return true;
        }
        return false;
    }

    @Override
    public void stopOrReloadCurrentTab() {
        Tab currentTab = mTabSupplier.get();
        if (currentTab == null) return;

        if (currentTab.isLoading()) {
            currentTab.stopLoading();
            RecordUserAction.record("MobileToolbarStop");
        } else {
            currentTab.reload();
            RecordUserAction.record("MobileToolbarReload");
        }
        mOnSuccessRunnable.run();
    }

    @Override
    public void openHomepage() {
        RecordUserAction.record("Home");
        recordHomeButtonUserPerProfileType();
        if (mOverrideHomePageSupplier.get()) {
            if (ChromeFeatureList.isEnabled(ChromeFeatureList.TOOLBAR_IPH_ANDROID)) {
                // While some other element is handling the routing of this click event, something
                // still needs to notify the event. This approach allows consolidation of events for
                // the home button.
                Tracker tracker = mTrackerSupplier.get();
                if (tracker != null) tracker.notifyEvent(EventConstants.HOMEPAGE_BUTTON_CLICKED);
            }
            return;
        }
        Tab currentTab = mTabSupplier.get();
        if (currentTab == null) return;
        String homePageUrl = mHomepageUrlSupplier.get();
        boolean is_chrome_internal =
                homePageUrl.startsWith(ContentUrlConstants.ABOUT_URL_SHORT_PREFIX)
                || homePageUrl.startsWith(UrlConstants.CHROME_URL_SHORT_PREFIX)
                || homePageUrl.startsWith(UrlConstants.CHROME_NATIVE_URL_SHORT_PREFIX)
                || homePageUrl.startsWith("chrome-search://local-ntp/");
        RecordHistogram.recordBooleanHistogram(
                "Navigation.Home.IsChromeInternal", is_chrome_internal);
        // Log a user action for the !is_chrome_internal case. This value is used as part of a
        // high-level guiding metric, which is being migrated to user actions.
        if (!is_chrome_internal) {
            RecordUserAction.record("Navigation.Home.NotChromeInternal");
        }

        recordHomeButtonUseForIPH(homePageUrl);
        currentTab.loadUrl(new LoadUrlParams(homePageUrl, PageTransition.HOME_PAGE));
    }

    /** Record that homepage button was used for IPH reasons */
    private void recordHomeButtonUseForIPH(String homepageUrl) {
        Tab tab = mTabSupplier.get();
        Tracker tracker = mTrackerSupplier.get();
        if (tab == null || tracker == null) return;

        tracker.notifyEvent(EventConstants.HOMEPAGE_BUTTON_CLICKED);

        if (UrlUtilities.isNTPUrl(homepageUrl)) {
            tracker.notifyEvent(EventConstants.NTP_HOME_BUTTON_CLICKED);
        }
    }

    private void recordHomeButtonUserPerProfileType() {
        Tab tab = mTabSupplier.get();
        if (tab == null) return;
        Profile profile = Profile.fromWebContents(tab.getWebContents());
        if (profile == null) return;

        @BrowserProfileType
        int type = Profile.getBrowserProfileTypeFromProfile(profile);
        RecordHistogram.recordEnumeratedHistogram(
                "Android.HomeButton.PerProfileType", type, BrowserProfileType.MAX_VALUE + 1);
    }
}
