// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.omnibox;

import android.content.res.ColorStateList;

import androidx.annotation.ColorRes;
import androidx.annotation.DrawableRes;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.annotation.StringRes;

import org.chromium.chrome.browser.tab.Tab;
import org.chromium.components.security_state.ConnectionSecurityLevel;

/**
 * Interface defining a provider for data needed by the {@link LocationBar}.
 */
// TODO(crbug.com/1142887): Refine split between LocationBar properties and sub-component
// properties, e.g. security state, which is only used by the status icon.
public interface LocationBarDataProvider {
    /**
     * Observer interface for consumers who wish to subscribe to updates of LocationBarData.
     * Since LocationBarDataProvider data is typically calculated lazily, individual observer
     * methods don't directly supply the updated value. Instead, the expectation is that the
     * consumer will query the data it cares about.
     */
    interface Observer {
        default void onIncognitoStateChanged(){};
        default void onNtpStartedLoading(){};

        /**
         * Notifies about a possible change of the value of {@link #getPrimaryColor()}, or {@link
         * #isUsingBrandColor()}.
         */
        default void onPrimaryColorChanged(){};

        /** Notifies about possible changes to values affecting the status icon. */
        default void onSecurityStateChanged(){};

        default void onTitleChanged(){};
        default void onUrlChanged(){};

        default void hintZeroSuggestRefresh(){};
    }

    /** Adds an observer of changes to LocationBarDataProvider's data. */
    void addObserver(Observer observer);

    /** Removes an observer of changes to LocationBarDataProvider's data. */
    void removeObserver(Observer observer);

    /** Returns The url for the currently active page.*/
    @NonNull
    String getCurrentUrl();

    /** Returns the delegate for the NewTabPage shown for the current tab. */
    @NonNull
    NewTabPageDelegate getNewTabPageDelegate();

    /** Returns whether the currently active page is loading. */
    default boolean isLoading() {
        if (isInOverviewAndShowingOmnibox()) return false;
        Tab tab = getTab();
        return tab != null && tab.isLoading();
    }

    /** Returns whether the current page is in an incognito browser context. */
    boolean isIncognito();

    /** Returns the currently active tab, if there is one. */
    @Nullable
    Tab getTab();

    /** Returns whether the LocationBarDataProvider currently has an active tab. */
    boolean hasTab();

    /**
     * Returns whether the LocationBar's embedder is currently being displayed in overview mode and
     * showing the omnibox.
     */
    boolean isInOverviewAndShowingOmnibox();

    /** Returns the contents of the {@link UrlBar}. */
    UrlBarData getUrlBarData();

    /** Returns the title of the current page, or the empty string if there is currently no tab. */
    @NonNull
    String getTitle();

    /** Returns the primary color to use for the background.*/
    int getPrimaryColor();

    /** Returns whether the current primary color is a brand color. */
    boolean isUsingBrandColor();

    /** Returns whether the page currently shown is an offline page. */
    boolean isOfflinePage();

    /** Returns whether the page currently shown is a paint preview. */
    default boolean isPaintPreview() {
        return false;
    }

    /** Returns the current {@link ConnectionSecurityLevel}. */
    @ConnectionSecurityLevel
    int getSecurityLevel();

    /**
     * Returns the current page classification.
     *
     * @param isFocusedFromFakebox If the omnibox focus originated from the fakebox.
     * @return Integer value representing the {@code OmniboxEventProto.PageClassification}.
     */
    int getPageClassification(boolean isFocusedFromFakebox);

    /**
     * Returns the resource ID of the icon that should be displayed or 0 if no icon should be shown.
     *
     * @param isTablet Whether or not the display context of the icon is a tablet.
     */
    @DrawableRes
    int getSecurityIconResource(boolean isTablet);

    /** Returns The {@link ColorStateList} to use to tint the security state icon. */
    @ColorRes
    int getSecurityIconColorStateList();

    /** Returns the resource ID of the content description for the security icon. */
    @StringRes
    int getSecurityIconContentDescriptionResourceId();
}
