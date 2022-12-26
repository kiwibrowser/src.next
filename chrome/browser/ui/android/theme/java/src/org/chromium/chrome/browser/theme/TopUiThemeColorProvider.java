// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.theme;

import android.content.Context;
import android.content.res.ColorStateList;

import androidx.annotation.IntDef;
import androidx.annotation.NonNull;

import org.chromium.base.metrics.RecordHistogram;
import org.chromium.base.supplier.ObservableSupplier;
import org.chromium.base.supplier.Supplier;
import org.chromium.chrome.browser.tab.CurrentTabObserver;
import org.chromium.chrome.browser.tab.EmptyTabObserver;
import org.chromium.chrome.browser.tab.Tab;
import org.chromium.chrome.browser.tab.TabState;
import org.chromium.chrome.browser.ui.native_page.NativePage;
import org.chromium.chrome.browser.ui.theme.BrandedColorScheme;
import org.chromium.components.browser_ui.styles.ChromeColors;
import org.chromium.components.embedder_support.util.UrlUtilities;
import org.chromium.ui.util.ColorUtils;

import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;

/**
 * Manages the theme color used on the top part of the UI based on Tab's theme color and other
 * conditions such as dark mode settings, incognito mode, security state, etc.
 * <p>The theme color is only updated when the supplied tab is non-null.
 */
public class TopUiThemeColorProvider extends ThemeColorProvider {
    private static final float LOCATION_BAR_TRANSPARENT_BACKGROUND_ALPHA = 0.2f;

    @IntDef({ThemeColorUma.NO_COLOR_SET, ThemeColorUma.COLOR_SET_AND_APPLIED,
            ThemeColorUma.COLOR_SET_BUT_NOT_APPLIED, ThemeColorUma.NUM_ENTRIES})
    @Retention(RetentionPolicy.SOURCE)
    private @interface ThemeColorUma {
        int NO_COLOR_SET = 0;
        int COLOR_SET_AND_APPLIED = 1;
        int COLOR_SET_BUT_NOT_APPLIED = 2;
        int NUM_ENTRIES = 3;
    }

    private final CurrentTabObserver mTabObserver;

    private final Supplier<Integer> mActivityThemeColorSupplier;
    private final boolean mIsTablet;
    private final Context mContext;

    /** Whether the theme should apply while in dark mode. */
    private final boolean mAllowThemingInNightMode;

    /** Whether bright theme colors are allowed. */
    private final boolean mAllowBrightThemeColors;

    /** Whether or not the default color is used. */
    private boolean mIsDefaultColorUsed;

    /**
     * @param context {@link Context} to access the theme and the resources.
     * @param tabSupplier Supplier of the current tab.
     * @param activityThemeColorSupplier Supplier of activity theme color.
     * @param isTablet Whether the current activity is being run on a tablet.
     * @param allowThemingInNightMode Whether the tab theme should be used when the device is in
     *                                night mode.
     * @param allowBrightThemeColors Whether the tab allows bright theme colors.
     */
    public TopUiThemeColorProvider(Context context, ObservableSupplier<Tab> tabSupplier,
            Supplier<Integer> activityThemeColorSupplier, boolean isTablet,
            boolean allowThemingInNightMode, boolean allowBrightThemeColors) {
        super(context);
        mContext = context;
        mTabObserver = new CurrentTabObserver(tabSupplier,
                new EmptyTabObserver() {
                    @Override
                    public void onDidChangeThemeColor(Tab tab, int themeColor) {
                        updateColor(tab, themeColor, true);
                    }

                    @Override
                    public void didFirstVisuallyNonEmptyPaint(Tab tab) {
                        recordMetaThemeHistogramForTab(tab);
                    }
                },
                (tab) -> {
                    if (tab != null) updateColor(tab, tab.getThemeColor(), false);
                });
        mActivityThemeColorSupplier = activityThemeColorSupplier;
        mIsTablet = isTablet;
        mAllowThemingInNightMode = allowThemingInNightMode;
        mAllowBrightThemeColors = allowBrightThemeColors;
    }

    /**
     * @return Theme color or the given fallback color if the default color is
     *         used or there is no current tab.
     */
    public int getThemeColorOrFallback(Tab tab, int fallbackColor) {
        return (tab == null || mIsDefaultColorUsed) ? fallbackColor : getThemeColor();
    }

    private void updateColor(Tab tab, int themeColor, boolean shouldAnimate) {
        updatePrimaryColor(calculateColor(tab, themeColor), shouldAnimate);
        mIsDefaultColorUsed = isUsingDefaultColor(tab, themeColor);
        final @BrandedColorScheme int brandedColorScheme =
                calculateBrandedColorScheme(tab.isIncognito(), mIsDefaultColorUsed);
        final ColorStateList iconTint =
                ThemeUtils.getThemedToolbarIconTint(mContext, brandedColorScheme);
        updateTint(iconTint, brandedColorScheme);
    }

    private int calculateBrandedColorScheme(boolean isIncognito, boolean isDefaultColor) {
        if (isIncognito) return BrandedColorScheme.INCOGNITO;
        if (isDefaultColor) return BrandedColorScheme.APP_DEFAULT;

        final boolean isDarkTheme =
                ColorUtils.shouldUseLightForegroundOnBackground(getThemeColor());
        return isDarkTheme ? BrandedColorScheme.DARK_BRANDED_THEME
                           : BrandedColorScheme.LIGHT_BRANDED_THEME;
    }

    /**
     * Calculate theme color to be used for a given tab.
     * @param tab Tab to get the theme color for.
     * @param themeColor Initial color to calculate the theme color with.
     * @return Final theme color for a given tab, with other signals taken into account.
     */
    public int calculateColor(Tab tab, int themeColor) {
        // This method is used not only for the current tab but also for
        // any given tab. Therefore it should not alter any class state.
        if (!isUsingTabThemeColor(tab, themeColor)) {
            themeColor = ChromeColors.getDefaultThemeColor(mContext, tab.isIncognito());
            if (isThemingAllowed(tab)) {
                int customThemeColor = mActivityThemeColorSupplier.get();
                if (customThemeColor != TabState.UNSPECIFIED_THEME_COLOR) {
                    themeColor = customThemeColor;
                }
            }
        }

        // Ensure there is no alpha component to the theme color as that is not supported in the
        // dependent UI.
        return themeColor | 0xFF000000;
    }

    private boolean isUsingDefaultColor(Tab tab, int themeColor) {
        // This method is used not only for the current tab but also for
        // any given tab. Therefore it should not alter any class state.
        return !(isUsingTabThemeColor(tab, themeColor)
                || (isThemingAllowed(tab)
                        && mActivityThemeColorSupplier.get() != TabState.UNSPECIFIED_THEME_COLOR));
    }

    /**
     * The default background color used for {@link Tab} if the associate web content doesn't
     * specify a background color.
     * @param tab {@link Tab} object to get the background color for.
     * @return The background color of {@link Tab}.
     */
    public int getBackgroundColor(Tab tab) {
        // This method makes it easy to mock, test-friendly.
        return ThemeUtils.getBackgroundColor(tab);
    }

    /**
     * @param tab Tab to get the theme color for.
     * @param themeColor Initial color to calculate the theme color with.
     * @return Whether the given tab is using the tab theme color.
     */
    private boolean isUsingTabThemeColor(Tab tab, int themeColor) {
        return isThemingAllowed(tab) && themeColor != TabState.UNSPECIFIED_THEME_COLOR
                && (mAllowBrightThemeColors || !ColorUtils.isThemeColorTooBright(themeColor));
    }

    /**
     * Returns whether theming the activity is allowed (either by the web contents or by the
     * activity).
     */
    private boolean isThemingAllowed(Tab tab) {
        boolean disallowDueToNightMode =
                !mAllowThemingInNightMode && ColorUtils.inNightMode(tab.getContext());

        return tab.isThemingAllowed() && !mIsTablet && !disallowDueToNightMode
                && !tab.isNativePage() && !tab.isIncognito();
    }

    /**
     * @param tab The {@link Tab} on which the toolbar scene layer color is used.
     * @return The toolbar (or browser controls) color used in the compositor scene layer. Note that
     *         this is primarily used for compositor animation, and doesn't affect the Android view.
     */
    public int getSceneLayerBackground(Tab tab) {
        NativePage nativePage = tab.getNativePage();
        int defaultColor = calculateColor(tab, tab.getThemeColor());
        return nativePage != null ? nativePage.getToolbarSceneLayerBackground(defaultColor)
                                  : defaultColor;
    }

    /**
     * @param tab The {@link Tab} on which the top toolbar is drawn.
     * @return Background alpha for the textbox given a Tab.
     */
    public float getTextBoxBackgroundAlpha(Tab tab) {
        float alpha = ColorUtils.shouldUseOpaqueTextboxBackground(
                              calculateColor(tab, tab.getThemeColor()))
                ? 1.f
                : LOCATION_BAR_TRANSPARENT_BACKGROUND_ALPHA;
        NativePage nativePage = tab.getNativePage();
        return nativePage != null ? nativePage.getToolbarTextBoxAlpha(alpha) : alpha;
    }

    private void recordMetaThemeHistogramForTab(@NonNull Tab tab) {
        // We're only interested in tabs where a meta theme tag is possible, i.e. websites.
        if (tab.isNativePage() || tab.getWebContents() == null
                || UrlUtilities.isInternalScheme(tab.getUrl())) {
            return;
        }

        final String histogramName = "Android.ThemeColor";
        final int themeColor = tab.getWebContents().getThemeColor();
        @ThemeColorUma
        int bucket;
        if (themeColor == TabState.UNSPECIFIED_THEME_COLOR) {
            bucket = ThemeColorUma.NO_COLOR_SET;
        } else if (!isUsingDefaultColor(tab, themeColor)) {
            bucket = ThemeColorUma.COLOR_SET_AND_APPLIED;
        } else {
            bucket = ThemeColorUma.COLOR_SET_BUT_NOT_APPLIED;
        }
        RecordHistogram.recordEnumeratedHistogram(histogramName, bucket, ThemeColorUma.NUM_ENTRIES);
    }

    @Override
    public void destroy() {
        super.destroy();
        mTabObserver.destroy();
    }
}
