// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.tab;

import org.chromium.chrome.browser.SwipeRefreshHandler;
import org.chromium.chrome.browser.autofill_assistant.AutofillAssistantTabHelper;
import org.chromium.chrome.browser.complex_tasks.TaskTabHelper;
import org.chromium.chrome.browser.contextualsearch.ContextualSearchTabHelper;
import org.chromium.chrome.browser.crypto.CipherFactory;
import org.chromium.chrome.browser.dom_distiller.ReaderModeManager;
import org.chromium.chrome.browser.dom_distiller.TabDistillabilityProvider;
import org.chromium.chrome.browser.infobar.InfoBarContainer;
import org.chromium.chrome.browser.media.ui.MediaSessionTabHelper;
import org.chromium.chrome.browser.password_check.PasswordCheckUkmRecorder;
import org.chromium.chrome.browser.price_tracking.PriceTrackingFeatures;
import org.chromium.chrome.browser.tab.state.ShoppingPersistedTabData;

/**
 * Helper class that initializes various tab UserData objects.
 */
public final class TabHelpers {
    private TabHelpers() {}

    /**
     * Creates Tab helper objects upon Tab creation.
     * @param tab {@link Tab} to create helpers for.
     * @param parentTab {@link Tab} parent tab
     */
    static void initTabHelpers(Tab tab, Tab parentTab) {
        TabUma.createForTab(tab);
        TabDistillabilityProvider.createForTab(tab);
        InterceptNavigationDelegateTabHelper.createForTab(tab);
        ContextualSearchTabHelper.createForTab(tab);
        MediaSessionTabHelper.createForTab(tab);
        TaskTabHelper.createForTab(tab, parentTab);
        TabBrowserControlsConstraintsHelper.createForTab(tab);
        if (ReaderModeManager.isEnabled()) ReaderModeManager.createForTab(tab);
        AutofillAssistantTabHelper.createForTab(tab);
        PasswordCheckUkmRecorder.createForTab(tab);

        // The following will start prefetching data for the price drops feature, so
        // we should only do it if the user is eligible for the feature (e.g. has sync enabled).
        if (!tab.isIncognito() && !((TabImpl) tab).isCustomTab()
                && PriceTrackingFeatures.isPriceTrackingEligible()
                && ShoppingPersistedTabData.isPriceTrackingWithOptimizationGuideEnabled()) {
            ShoppingPersistedTabData.initialize(tab);
        }

        // TODO(jinsukkim): Do this by having something observe new tab creation.
        if (tab.isIncognito()) CipherFactory.getInstance().triggerKeyGeneration();
    }

    /**
     * Initializes {@link TabWebContentsUserData} and WebContents-related objects
     * when a new WebContents is set to the tab.
     * @param tab {@link Tab} to create helpers for.
     */
    static void initWebContentsHelpers(Tab tab) {
        // The InfoBarContainer needs to be created after the ContentView has been natively
        // initialized. In the case where restoring a Tab or showing a prerendered one we already
        // have a valid infobar container, no need to recreate one.
        InfoBarContainer.from(tab);

        TabWebContentsObserver.from(tab);
        SwipeRefreshHandler.from(tab);
        TabFavicon.from(tab);
        TrustedCdn.from(tab);
        TabAssociatedApp.from(tab);
        TabGestureStateListener.from(tab);
    }
}
