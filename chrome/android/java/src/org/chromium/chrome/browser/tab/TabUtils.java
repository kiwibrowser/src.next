// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.tab;

import android.app.Activity;
import android.content.Context;
import android.content.res.Configuration;
import android.content.res.Resources;
import android.graphics.Bitmap;
import android.graphics.Matrix;
import android.graphics.Point;
import android.graphics.Rect;
import android.util.Size;
import android.view.Display;
import android.widget.ImageView;
import android.widget.ImageView.ScaleType;

import androidx.annotation.IntDef;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.annotation.VisibleForTesting;

import org.chromium.base.BuildInfo;
import org.chromium.chrome.R;
import org.chromium.chrome.browser.browser_controls.BrowserControlsStateProvider;
import org.chromium.chrome.browser.profiles.Profile;
import org.chromium.chrome.browser.tasks.tab_management.TabUiThemeProvider;
import org.chromium.components.browser_ui.site_settings.WebsitePreferenceBridge;
import org.chromium.components.browser_ui.util.AutomotiveUtils;
import org.chromium.components.content_settings.ContentSettingValues;
import org.chromium.components.content_settings.ContentSettingsType;
import org.chromium.content_public.browser.WebContents;
import org.chromium.ui.base.WindowAndroid;
import org.chromium.ui.display.DisplayAndroidManager;
import org.chromium.ui.display.DisplayUtil;
import org.chromium.url.GURL;

import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;

/** Collection of utility methods that operates on Tab. */
public class TabUtils {
    @VisibleForTesting(otherwise = VisibleForTesting.PRIVATE)
    public static final float PORTRAIT_THUMBNAIL_ASPECT_RATIO = 0.85f;

    /** Define the callers of NavigationControllerImpl#setUseDesktopUserAgent. */
    @IntDef({
        UseDesktopUserAgentCaller.ON_MENU_OR_KEYBOARD_ACTION,
        UseDesktopUserAgentCaller.LOAD_IF_NEEDED,
        UseDesktopUserAgentCaller.RELOAD,
        UseDesktopUserAgentCaller.RELOAD_IGNORING_CACHE,
        UseDesktopUserAgentCaller.OTHER
    })
    @Retention(RetentionPolicy.SOURCE)
    public @interface UseDesktopUserAgentCaller {
        int ON_MENU_OR_KEYBOARD_ACTION = 0;
        int LOAD_IF_NEEDED = 100;
        int RELOAD = 200;
        int RELOAD_IGNORING_CACHE = 300;
        int OTHER = 400;
    }

    // Do not instantiate this class.
    private TabUtils() {}

    /**
     * @return {@link Activity} associated with the given tab.
     */
    public static @Nullable Activity getActivity(Tab tab) {
        WebContents webContents = tab != null ? tab.getWebContents() : null;
        if (webContents == null || webContents.isDestroyed()) return null;
        WindowAndroid window = webContents.getTopLevelNativeWindow();
        return window != null ? window.getActivity().get() : null;
    }

    /**
     * Provides an estimate of the contents size.
     *
     * The estimate is likely to be incorrect. This is not a problem, as the aim
     * is to avoid getting a different layout and resources than needed at
     * render time.
     * @param context The application context.
     * @return The estimated prerender size in pixels.
     */
    // status_bar_height is not a public framework resource, so we have to getIdentifier()
    @SuppressWarnings("DiscouragedApi")
    public static Rect estimateContentSize(Context context) {
        // The size is estimated as:
        // X = screenSizeX
        // Y = screenSizeY - top bar - bottom bar - custom tabs bar
        // The bounds rectangle includes the bottom bar and the custom tabs bar as well.
        Rect screenBounds = new Rect();
        Point screenSize = new Point();
        Display display = DisplayAndroidManager.getDefaultDisplayForContext(context);
        display.getSize(screenSize);
        Resources resources = context.getResources();
        int statusBarId = resources.getIdentifier("status_bar_height", "dimen", "android");
        try {
            screenSize.y -= resources.getDimensionPixelSize(statusBarId);
        } catch (Resources.NotFoundException e) {
            // Nothing, this is just a best effort estimate.
        }
        screenBounds.set(
                0,
                resources.getDimensionPixelSize(R.dimen.custom_tabs_control_container_height),
                screenSize.x,
                screenSize.y);
        return screenBounds;
    }

    public static Tab fromWebContents(WebContents webContents) {
        return TabImplJni.get().fromWebContents(webContents);
    }

    /**
     * Call when tab need to switch user agent between desktop and mobile.
     * @param tab The tab to be switched the user agent.
     * @param switchToDesktop Whether switching the user agent to desktop.
     * @param forcedByUser Whether this was triggered by users action.
     * @param caller The caller of this method.
     */
    // TODO(crbug.com/1413060): Remove param forcedByUser from TabUtils#switchUserAgent.
    public static void switchUserAgent(
            Tab tab, boolean switchToDesktop, boolean forcedByUser, int caller) {
        final boolean reloadOnChange = !tab.isNativePage();
        tab.getWebContents()
                .getNavigationController()
                .setUseDesktopUserAgent(switchToDesktop, reloadOnChange, caller);
        if (forcedByUser) {
            @TabUserAgent
            int tabUserAgent = switchToDesktop ? TabUserAgent.DESKTOP : TabUserAgent.MOBILE;
            if (isDesktopSiteGlobalEnabled(tab.getProfile()) == switchToDesktop) {
                tabUserAgent = TabUserAgent.DEFAULT;
            }
            tab.setUserAgent(tabUserAgent);
        }
    }

    /**
     * Get UseDesktopUserAgent setting from webContents.
     * @param webContents The webContents used to retrieve UseDesktopUserAgent setting.
     * @return Whether the webContents is set to use desktop user agent.
     */
    public static boolean isUsingDesktopUserAgent(WebContents webContents) {
        return webContents != null
                && webContents.getNavigationController().getUseDesktopUserAgent();
    }

    /**
     * Get tabUserAgent from the tab, which represents the tab level RDS setting.
     * @param tab The tab used to retrieve tabUserAgent.
     * @return The tab level RDS setting.
     */
    public static @TabUserAgent int getTabUserAgent(Tab tab) {
        @TabUserAgent int tabUserAgent = tab.getUserAgent();
        WebContents webContents = tab.getWebContents();
        boolean currentRequestDesktopSite = isUsingDesktopUserAgent(webContents);
        // TabUserAgent.UNSET means this is a pre-existing tab from an earlier build. In this case
        // we set the TabUserAgent bit based on last committed entry's user agent. If webContents is
        // null, this method is triggered too early, and we cannot read the last committed entry's
        // user agent yet. We will skip for now and let the following call set the TabUserAgent bit.
        if (webContents != null && tabUserAgent == TabUserAgent.UNSET) {
            if (currentRequestDesktopSite) {
                tabUserAgent = TabUserAgent.DESKTOP;
            } else {
                tabUserAgent = TabUserAgent.DEFAULT;
            }
            tab.setUserAgent(tabUserAgent);
        }
        return tabUserAgent;
    }

    /**
     * Read Request Desktop Site ContentSettings.
     * @param profile The profile used to retrieve ContentSettings.
     * @param url The Url used to retrieve site level ContentSettings.
     * @return Whether Request Desktop Site is enabled in ContentSettings.
     */
    public static boolean readRequestDesktopSiteContentSettings(
            Profile profile, @Nullable GURL url) {
        return url != null && TabUtils.isDesktopSiteEnabled(profile, url);
    }

    /**
     * Check if Request Desktop Site ContentSettings is global setting.
     * @param profile The profile used to retrieve ContentSettings.
     * @param url The Url used to retrieve ContentSettings.
     * @return Whether Request Desktop Site ContentSettings is global setting.
     */
    public static boolean isRequestDesktopSiteContentSettingsGlobal(
            Profile profile, @Nullable GURL url) {
        if (url == null) {
            return true;
        }
        return WebsitePreferenceBridge.isContentSettingGlobal(
                profile, ContentSettingsType.REQUEST_DESKTOP_SITE, url, url);
    }

    /**
     * Check if Request Desktop Site global setting is enabled.
     * @param profile The profile of the tab.
     *        Content settings have separate storage for incognito profiles.
     *        For site-specific exceptions the actual profile is needed.
     * @return Whether the desktop site should be requested.
     */
    public static boolean isDesktopSiteGlobalEnabled(Profile profile) {
        return WebsitePreferenceBridge.isCategoryEnabled(
                profile, ContentSettingsType.REQUEST_DESKTOP_SITE);
    }

    /**
     * Check if Request Desktop Site global setting is enabled.
     * @param profile The profile of the tab.
     *        Content settings have separate storage for incognito profiles.
     *        For site-specific exceptions the actual profile is needed.
     * @param url The URL for the current web content.
     * @return Whether the desktop site should be requested.
     */
    public static boolean isDesktopSiteEnabled(Profile profile, GURL url) {
        return WebsitePreferenceBridge.getContentSetting(
                        profile, ContentSettingsType.REQUEST_DESKTOP_SITE, url, url)
                == ContentSettingValues.ALLOW;
    }

    /**
     * Return aspect ratio for grid tab card based on form factor and orientation.
     * @param context - Context of the application.
     * @param browserControlsStateProvider - For getting browser controls height.
     * @return Aspect ratio for the grid tab card.
     */
    public static float getTabThumbnailAspectRatio(
            Context context, BrowserControlsStateProvider browserControlsStateProvider) {
        if (context.getResources().getConfiguration().orientation
                == Configuration.ORIENTATION_LANDSCAPE) {
            assert browserControlsStateProvider != null;
            int browserControlsHeightDp =
                    (browserControlsStateProvider == null)
                            ? 0
                            : Math.round(
                                    (float) browserControlsStateProvider.getTopControlsHeight()
                                            / context.getResources().getDisplayMetrics().density);
            int horizontalAutomotiveToolbarHeightDp =
                    AutomotiveUtils.getHorizontalAutomotiveToolbarHeightDp(context);
            int verticalAutomotiveToolbarWidthDp =
                    AutomotiveUtils.getVerticalAutomotiveToolbarWidthDp(context);
            // This should match the aspect ratio of a Tab's content area.
            return (context.getResources().getConfiguration().screenWidthDp * 1.f
                            - verticalAutomotiveToolbarWidthDp)
                    / (context.getResources().getConfiguration().screenHeightDp * 1.f
                            - browserControlsHeightDp
                            - horizontalAutomotiveToolbarHeightDp);
        }
        // This is an experimentally determined value.
        return PORTRAIT_THUMBNAIL_ASPECT_RATIO;
    }

    /**
     * Derive grid card height based on width, expected thumbnail aspect ratio and margins.
     * @param cardWidthPx width of the card
     * @param context to derive view margins
     * @param browserControlsStateProvider - For getting browser controls height.
     * @return computed card height.
     */
    public static int deriveGridCardHeight(
            int cardWidthPx,
            Context context,
            BrowserControlsStateProvider browserControlsStateProvider) {
        int tabThumbnailHeight =
                (int)
                        ((cardWidthPx - getThumbnailWidthDiff(context))
                                / getTabThumbnailAspectRatio(
                                        context, browserControlsStateProvider));
        int cardHeightPx = tabThumbnailHeight + getThumbnailHeightDiff(context);
        return cardHeightPx;
    }

    /**
     * Derive thumbnail size based on parent card size.
     * @param gridCardSize size of parent card.
     * @param context to derive view margins.
     * @return computed width and height of thumbnail.
     */
    public static Size deriveThumbnailSize(@NonNull Size gridCardSize, @NonNull Context context) {
        int thumbnailWidth = gridCardSize.getWidth() - getThumbnailWidthDiff(context);
        int thumbnailHeight = gridCardSize.getHeight() - getThumbnailHeightDiff(context);
        return new Size(thumbnailWidth, thumbnailHeight);
    }

    /**
     * Update the {@link Bitmap} and @{@link Matrix} of ImageView. The bitmap is scaled by a
     * matrix to be scaled to larger of the two dimensions of {@code destinationSize},
     * then top-center aligned.
     * @param view The {@link ImageView} to update.
     * @param bitmap The {@link Bitmap} to set in the view and scale.
     * @param destinationSize The desired {@link Size} of the bitmap.
     */
    public static void setBitmapAndUpdateImageMatrix(
            ImageView view, Bitmap bitmap, Size destinationSize) {
        if (BuildInfo.getInstance().isAutomotive) {
            bitmap.setDensity(DisplayUtil.getUiDensityForAutomotive(bitmap.getDensity()));
        }
        view.setImageBitmap(bitmap);
        int newWidth = destinationSize == null ? 0 : destinationSize.getWidth();
        int newHeight = destinationSize == null ? 0 : destinationSize.getHeight();
        if (newWidth <= 0
                || newHeight <= 0
                || (newWidth == bitmap.getWidth() && newHeight == bitmap.getHeight())) {
            view.setScaleType(ScaleType.FIT_CENTER);
            return;
        }

        final Matrix m = new Matrix();
        final float scale =
                Math.max(
                        (float) newWidth / bitmap.getWidth(),
                        (float) newHeight / bitmap.getHeight());
        m.setScale(scale, scale);

        /**
         * Bitmap is top-left aligned by default. We want to translate the image to be horizontally
         * center-aligned. |destination width - scaled width| is the width that is out of view
         * bounds. We need to translate bitmap (to left) by half of this distance.
         */
        final int xOffset = (int) ((newWidth - (bitmap.getWidth() * scale)) / 2);
        m.postTranslate(xOffset, 0);

        view.setScaleType(ScaleType.MATRIX);
        view.setImageMatrix(m);
    }

    private static int getThumbnailHeightDiff(Context context) {
        final int tabGridCardMargin = (int) TabUiThemeProvider.getTabGridCardMargin(context);
        final int thumbnailMargin =
                (int) context.getResources().getDimension(R.dimen.tab_grid_card_thumbnail_margin);
        int heightMargins = (2 * tabGridCardMargin) + thumbnailMargin;
        final int titleHeight =
                (int) context.getResources().getDimension(R.dimen.tab_grid_card_header_height);
        return titleHeight + heightMargins;
    }

    private static int getThumbnailWidthDiff(Context context) {
        final int tabGridCardMargin = (int) TabUiThemeProvider.getTabGridCardMargin(context);
        final int thumbnailMargin =
                (int) context.getResources().getDimension(R.dimen.tab_grid_card_thumbnail_margin);
        return 2 * (tabGridCardMargin + thumbnailMargin);
    }
}
