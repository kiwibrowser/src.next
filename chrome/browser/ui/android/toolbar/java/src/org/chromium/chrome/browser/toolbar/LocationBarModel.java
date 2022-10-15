// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.toolbar;

import android.content.Context;
import android.text.SpannableStringBuilder;
import android.text.TextUtils;
import android.util.LruCache;

import androidx.annotation.ColorInt;
import androidx.annotation.ColorRes;
import androidx.annotation.DrawableRes;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.annotation.StringRes;
import androidx.annotation.VisibleForTesting;

import org.chromium.base.FeatureList;
import org.chromium.base.ObserverList;
import org.chromium.base.TraceEvent;
import org.chromium.base.annotations.CalledByNative;
import org.chromium.base.annotations.NativeMethods;
import org.chromium.chrome.browser.flags.ChromeFeatureList;
import org.chromium.chrome.browser.layouts.LayoutStateProvider;
import org.chromium.chrome.browser.omnibox.ChromeAutocompleteSchemeClassifier;
import org.chromium.chrome.browser.omnibox.LocationBarDataProvider;
import org.chromium.chrome.browser.omnibox.NewTabPageDelegate;
import org.chromium.chrome.browser.omnibox.SearchEngineLogoUtils;
import org.chromium.chrome.browser.omnibox.UrlBarData;
import org.chromium.chrome.browser.omnibox.styles.OmniboxResourceProvider;
import org.chromium.chrome.browser.paint_preview.TabbedPaintPreview;
import org.chromium.chrome.browser.preferences.ChromePreferenceKeys;
import org.chromium.chrome.browser.profiles.Profile;
import org.chromium.chrome.browser.tab.Tab;
import org.chromium.chrome.browser.tab.TrustedCdn;
import org.chromium.chrome.browser.theme.ThemeUtils;
import org.chromium.chrome.browser.ui.theme.BrandedColorScheme;
import org.chromium.components.browser_ui.styles.ChromeColors;
import org.chromium.components.dom_distiller.core.DomDistillerUrlUtils;
import org.chromium.components.embedder_support.util.UrlConstants;
import org.chromium.components.embedder_support.util.UrlUtilities;
import org.chromium.components.metrics.OmniboxEventProtos.OmniboxEventProto.PageClassification;
import org.chromium.components.omnibox.AutocompleteSchemeClassifier;
import org.chromium.components.omnibox.OmniboxUrlEmphasizer;
import org.chromium.components.omnibox.SecurityStatusIcon;
import org.chromium.components.prefs.PrefService;
import org.chromium.components.security_state.ConnectionSecurityLevel;
import org.chromium.components.security_state.SecurityStateModel;
import org.chromium.components.user_prefs.UserPrefs;
import org.chromium.content_public.browser.WebContents;
import org.chromium.ui.base.WindowAndroid;
import org.chromium.url.GURL;
import org.chromium.url.URI;

import java.net.URISyntaxException;
import java.util.Objects;

/**
 * Provides a way of accessing toolbar data and state.
 */
public class LocationBarModel implements ToolbarDataProvider, LocationBarDataProvider {
    private static final int LRU_CACHE_SIZE = 10;
    static class SpannableDisplayTextCacheKey {
        @NonNull
        private final String mUrl;
        @NonNull
        private final String mDisplayText;
        private final int mSecurityLevel;
        private final int mNonEmphasizedColor;
        private final int mEmphasizedColor;
        private final int mDangerColor;
        private final int mSecureColor;

        private SpannableDisplayTextCacheKey(@NonNull String url, @NonNull String displayText,
                int securityLevel, int nonEmphasizedColor, int emphasizedColor, int dangerColor,
                int secureColor) {
            mUrl = url;
            mDisplayText = displayText;
            mSecurityLevel = securityLevel;
            mNonEmphasizedColor = nonEmphasizedColor;
            mEmphasizedColor = emphasizedColor;
            mDangerColor = dangerColor;
            mSecureColor = secureColor;
        }

        @Override
        public boolean equals(Object o) {
            if (this == o) {
                return true;
            }
            if (o == null || getClass() != o.getClass()) {
                return false;
            }
            SpannableDisplayTextCacheKey that = (SpannableDisplayTextCacheKey) o;
            return mSecurityLevel == that.mSecurityLevel
                    && mNonEmphasizedColor == that.mNonEmphasizedColor
                    && mEmphasizedColor == that.mEmphasizedColor
                    && mDangerColor == that.mDangerColor && mSecureColor == that.mSecureColor
                    && mUrl.equals(that.mUrl) && mDisplayText.equals(that.mDisplayText);
        }

        @Override
        public int hashCode() {
            return Objects.hash(mUrl, mDisplayText, mSecurityLevel, mNonEmphasizedColor,
                    mEmphasizedColor, mDangerColor, mSecureColor);
        }
    }

    /**
     * Formats the given URL to the original one of a distillation.
     */
    @FunctionalInterface
    public interface UrlFormatter {
        String format(GURL url);
    }

    /**
     * Provides non-primary incognito profile.
     */
    @FunctionalInterface
    public interface ProfileProvider {
        Profile getNonPrimaryOtrProfile(WindowAndroid window);
    }

    /**
     * Offline-related status of a given content.
     */
    public interface OfflineStatus {
        /**
         * Returns whether the WebContents is showing trusted offline page.
         */
        default boolean isShowingTrustedOfflinePage(WebContents webContents) {
            return false;
        }

        /**
         * Checks if an offline page is shown for the tab.
         */
        default boolean isOfflinePage(Tab tab) {
            return false;
        }
    }

    private final Context mContext;
    private final NewTabPageDelegate mNtpDelegate;
    private final @NonNull UrlFormatter mUrlFormatter;
    private final @NonNull ProfileProvider mProfileProvider;
    private final @NonNull OfflineStatus mOfflineStatus;
    private final SearchEngineLogoUtils mSearchEngineLogoUtils;
    // Always null if optimizations are disabled. Otherwise, non-null and unchanging following
    // native init. Always tied to the mLastUsedNonOTRProfile which is safe because no underlying
    // services have an incognito-specific instance.
    @Nullable
    private AutocompleteSchemeClassifier mChromeAutocompleteSchemeClassifier;
    // Non-null and unchanging following native init. The last used non-OTR (or regular) profile
    // can't change after this point because we don't support multi-profile on Android.
    @Nullable
    private Profile mLastUsedNonOTRProfile;
    @Nullable
    private LruCache<SpannableDisplayTextCacheKey, SpannableStringBuilder>
            mSpannableDisplayTextCache;
    private boolean mOptimizationsEnabled;

    private Tab mTab;
    private int mPrimaryColor;
    private int mDropdownStandardBgColor;
    private int mDropdownIncognitoBgColor;
    private int mSuggestionStandardBgColor;
    private int mSuggestionIncognitoBgColor;
    private LayoutStateProvider mLayoutStateProvider;

    private boolean mIsIncognito;
    private boolean mIsUsingBrandColor;
    private boolean mShouldShowOmniboxInOverviewMode;
    private boolean mIsShowingTabSwitcher;

    private long mNativeLocationBarModelAndroid;
    private ObserverList<LocationBarDataProvider.Observer> mLocationBarDataObservers =
            new ObserverList<>();

    /**
     * Default constructor for this class.
     * @param context The Context used for styling the toolbar visuals.
     * @param newTabPageDelegate Delegate used to access NTP.
     * @param urlFormatter Formatter returning the formatted version of the original version
     *        of URL of a distillation.
     * @param profileProvider Interface returning non-primary OTR profile.
     * @param offlineStatus Offline-related status provider.
     * @param searchEngineLogoUtils Utils to query the state of the search engine logos feature.
     */
    public LocationBarModel(Context context, NewTabPageDelegate newTabPageDelegate,
            @NonNull UrlFormatter urlFormatter, @NonNull ProfileProvider profileProvider,
            @NonNull OfflineStatus offlineStatus,
            @NonNull SearchEngineLogoUtils searchEngineLogoUtils) {
        mContext = context;
        mNtpDelegate = newTabPageDelegate;
        mUrlFormatter = urlFormatter;
        mProfileProvider = profileProvider;
        mOfflineStatus = offlineStatus;
        mPrimaryColor = ChromeColors.getDefaultThemeColor(context, false);
        mSearchEngineLogoUtils = searchEngineLogoUtils;
        mDropdownStandardBgColor = ChromeColors.getSurfaceColor(
                mContext, R.dimen.omnibox_suggestion_dropdown_bg_elevation);
        mDropdownIncognitoBgColor = mContext.getColor(R.color.omnibox_dropdown_bg_incognito);
        mSuggestionStandardBgColor =
                ChromeColors.getSurfaceColor(context, R.dimen.omnibox_suggestion_bg_elevation);
        mSuggestionIncognitoBgColor = context.getColor(R.color.omnibox_suggestion_bg_incognito);
    }

    /**
     * Handle any initialization that must occur after native has been initialized.
     */
    public void initializeWithNative() {
        mOptimizationsEnabled =
                ChromeFeatureList.isEnabled(ChromeFeatureList.ANDROID_SCROLL_OPTIMIZATIONS);
        mLastUsedNonOTRProfile = Profile.getLastUsedRegularProfile();
        if (mOptimizationsEnabled) {
            mSpannableDisplayTextCache = new LruCache<>(LRU_CACHE_SIZE);
            mChromeAutocompleteSchemeClassifier =
                    new ChromeAutocompleteSchemeClassifier(getProfile());
        }

        mNativeLocationBarModelAndroid = LocationBarModelJni.get().init(LocationBarModel.this);
    }

    /**
     * Destroys the native LocationBarModel.
     */
    public void destroy() {
        if (mChromeAutocompleteSchemeClassifier != null) {
            mChromeAutocompleteSchemeClassifier.destroy();
            mChromeAutocompleteSchemeClassifier = null;
        }
        if (mNativeLocationBarModelAndroid == 0) return;
        LocationBarModelJni.get().destroy(mNativeLocationBarModelAndroid, LocationBarModel.this);
        mNativeLocationBarModelAndroid = 0;
    }

    /**
     * @return The currently active WebContents being used by the Toolbar.
     */
    @CalledByNative
    private WebContents getActiveWebContents() {
        if (!hasTab()) return null;
        return mTab.getWebContents();
    }

    /**
     * Sets the tab that contains the information to be displayed in the toolbar.
     *
     * @param tab The tab associated currently with the toolbar.
     * @param isIncognito Whether the incognito model is currently selected, which must match the
     *                    passed in tab if non-null.
     */
    public void setTab(Tab tab, boolean isIncognito) {
        assert tab == null || tab.isIncognito() == isIncognito;
        mTab = tab;
        if (mIsIncognito != isIncognito) {
            mIsIncognito = isIncognito;
            notifyIncognitoStateChanged();
        }
        updateUsingBrandColor();
        notifyTitleChanged();
        notifyUrlChanged();
        notifyPrimaryColorChanged();
        notifySecurityStateChanged();
    }

    @Override
    public Tab getTab() {
        return hasTab() ? mTab : null;
    }

    @Override
    public boolean hasTab() {
        // TODO(https://crbug.com/1147131): Remove the isInitialized() and isDestroyed checks when
        // we no longer wait for TAB_CLOSED events to remove this tab.  Otherwise there is a chance
        // we use this tab after {@link Tab#destroy()} is called.
        return mTab != null && mTab.isInitialized() && !mTab.isDestroyed();
    }

    @Override
    public void addObserver(LocationBarDataProvider.Observer observer) {
        mLocationBarDataObservers.addObserver(observer);
    }

    @Override
    public void removeObserver(LocationBarDataProvider.Observer observer) {
        mLocationBarDataObservers.removeObserver(observer);
    }

    @Override
    // TODO(https://crbug.com/1305374): migrate to GURL.
    public String getCurrentUrl() {
        // Provide NTP url instead of most recent tab url for searches in overview mode (when Start
        // Surface is enabled).
        String url = getTab() != null && getTab().isInitialized()
                ? getTab().getUrl().getSpec().trim()
                : "";
        if (isInOverviewAndShowingOmnibox()) {
            return UrlConstants.NTP_URL;
        }

        return url;
    }

    public void notifyUrlChanged() {
        for (LocationBarDataProvider.Observer observer : mLocationBarDataObservers) {
            observer.onUrlChanged();
        }
    }

    public void notifyZeroSuggestRefresh() {
        for (LocationBarDataProvider.Observer observer : mLocationBarDataObservers) {
            observer.hintZeroSuggestRefresh();
        }
    }

    @Override
    public NewTabPageDelegate getNewTabPageDelegate() {
        return mNtpDelegate;
    }

    void notifyNtpStartedLoading() {
        for (Observer observer : mLocationBarDataObservers) {
            observer.onNtpStartedLoading();
        }
    }

    @Override
    public UrlBarData getUrlBarData() {
        // Part of scroll jank investigation http://crbug.com/905461. Will remove TraceEvent after
        // the investigation is complete.
        try (TraceEvent te = TraceEvent.scoped("LocationBarModel.getUrlBarData")) {
            String url = getCurrentUrl();
            if (!hasTab()) {
                return UrlBarData.EMPTY;
            }

            if (!UrlBarData.shouldShowUrl(url, isIncognito())) {
                return UrlBarData.EMPTY;
            }

            String formattedUrl = getFormattedFullUrl();
            if (mTab.isFrozen()) return buildUrlBarData(url, formattedUrl);

            if (DomDistillerUrlUtils.isDistilledPage(url)) {
                GURL originalUrl =
                        DomDistillerUrlUtils.getOriginalUrlFromDistillerUrl(new GURL(url));
                return buildUrlBarData(mUrlFormatter.format(originalUrl));
            }

            if (isOfflinePage()) {
                GURL originalUrl = mTab.getOriginalUrl();
                formattedUrl = UrlUtilities.stripScheme(mUrlFormatter.format(originalUrl));

                // Clear the editing text for untrusted offline pages.
                if (!mOfflineStatus.isShowingTrustedOfflinePage(mTab.getWebContents())) {
                    return buildUrlBarData(url, formattedUrl, "");
                }

                return buildUrlBarData(url, formattedUrl);
            }

            String urlForDisplay = getUrlForDisplay();
            if (!urlForDisplay.equals(formattedUrl)) {
                return buildUrlBarData(url, urlForDisplay, formattedUrl);
            }

            return buildUrlBarData(url, formattedUrl);
        }
    }

    private UrlBarData buildUrlBarData(String url) {
        return buildUrlBarData(url, url, url);
    }

    private UrlBarData buildUrlBarData(String url, String displayText) {
        return buildUrlBarData(url, displayText, displayText);
    }

    private UrlBarData buildUrlBarData(String url, String displayText, String editingText) {
        SpannableStringBuilder spannableDisplayText = new SpannableStringBuilder(displayText);
        if (mNativeLocationBarModelAndroid != 0 && spannableDisplayText.length() > 0
                && shouldEmphasizeUrl()) {
            boolean isInternalPage = false;
            try {
                isInternalPage = UrlUtilities.isInternalScheme(new URI(url));
            } catch (URISyntaxException e) {
                // Ignore as this only is for applying color
            }

            final @BrandedColorScheme int brandedColorScheme =
                    OmniboxResourceProvider.getBrandedColorScheme(
                            mContext, isIncognito(), getPrimaryColor());
            final @ColorInt int nonEmphasizedColor =
                    OmniboxResourceProvider.getUrlBarSecondaryTextColor(
                            mContext, brandedColorScheme);
            final @ColorInt int emphasizedColor =
                    OmniboxResourceProvider.getUrlBarPrimaryTextColor(mContext, brandedColorScheme);
            final @ColorInt int dangerColor =
                    OmniboxResourceProvider.getUrlBarDangerColor(mContext, brandedColorScheme);
            final @ColorInt int secureColor =
                    OmniboxResourceProvider.getUrlBarSecureColor(mContext, brandedColorScheme);

            AutocompleteSchemeClassifier autocompleteSchemeClassifier;
            SpannableDisplayTextCacheKey cacheKey =
                    new SpannableDisplayTextCacheKey(url, displayText, getSecurityLevel(),
                            nonEmphasizedColor, emphasizedColor, dangerColor, secureColor);
            SpannableStringBuilder cachedSpannableDisplayText = null;
            if (mOptimizationsEnabled) {
                autocompleteSchemeClassifier = mChromeAutocompleteSchemeClassifier;
                cachedSpannableDisplayText = mSpannableDisplayTextCache.get(cacheKey);
            } else {
                autocompleteSchemeClassifier = new ChromeAutocompleteSchemeClassifier(getProfile());
            }

            try {
                if (cachedSpannableDisplayText != null) {
                    return UrlBarData.forUrlAndText(url, cachedSpannableDisplayText, editingText);
                } else {
                    OmniboxUrlEmphasizer.emphasizeUrl(spannableDisplayText,
                            autocompleteSchemeClassifier, getSecurityLevel(), isInternalPage,
                            shouldEmphasizeHttpsScheme(), nonEmphasizedColor, emphasizedColor,
                            dangerColor, secureColor);
                    if (mOptimizationsEnabled) {
                        mSpannableDisplayTextCache.put(cacheKey, spannableDisplayText);
                    }
                }
            } finally {
                if (!mOptimizationsEnabled) autocompleteSchemeClassifier.destroy();
            }
        }
        return UrlBarData.forUrlAndText(url, spannableDisplayText, editingText);
    }

    /**
     * @return True if the displayed URL should be emphasized, false if the displayed text
     *         already has formatting for emphasis applied.
     */
    private boolean shouldEmphasizeUrl() {
        // If the toolbar shows the publisher URL, it applies its own formatting for emphasis.
        if (mTab == null) return true;

        return TrustedCdn.getPublisherUrl(mTab) == null;
    }

    @VisibleForTesting
    LruCache<SpannableDisplayTextCacheKey, SpannableStringBuilder> getCacheForTesting() {
        return mSpannableDisplayTextCache;
    }

    /**
     * @return Whether the light security theme should be used.
     */
    @VisibleForTesting
    public boolean shouldEmphasizeHttpsScheme() {
        return !isUsingBrandColor() && !isIncognito();
    }

    @Override
    public String getTitle() {
        if (!hasTab()) return "";

        String title = getTab().getTitle();
        return TextUtils.isEmpty(title) ? title : title.trim();
    }

    public void notifyTitleChanged() {
        for (LocationBarDataProvider.Observer observer : mLocationBarDataObservers) {
            observer.onTitleChanged();
        }
    }

    @Override
    public boolean isIncognito() {
        return mIsIncognito;
    }

    private void notifyIncognitoStateChanged() {
        for (LocationBarDataProvider.Observer observer : mLocationBarDataObservers) {
            observer.onIncognitoStateChanged();
        }
    }

    /**
     * @return Whether the location bar is showing in overview mode. If the location bar should not
     *  currently be showing in overview mode, returns false.
     */
    @Override
    public boolean isInOverviewAndShowingOmnibox() {
        if (!mShouldShowOmniboxInOverviewMode) return false;

        return mLayoutStateProvider != null && mIsShowingTabSwitcher;
    }

    /**
     * @return Whether the location bar should show when in overview mode.
     */
    @Override
    public boolean shouldShowLocationBarInOverviewMode() {
        return mShouldShowOmniboxInOverviewMode;
    }

    @Override
    public Profile getProfile() {
        if (mIsIncognito) {
            WindowAndroid windowAndroid = (mTab != null) ? mTab.getWindowAndroid() : null;
            // If the mTab belongs to a CustomTabActivity then we return the non-primary OTR profile
            // which is associated with it. For all other cases we return the primary OTR profile.
            Profile nonPrimaryOtrProfile = mProfileProvider.getNonPrimaryOtrProfile(windowAndroid);
            if (nonPrimaryOtrProfile != null) return nonPrimaryOtrProfile;

            // When in overview mode with no open tabs, there has not been created an
            // OTR profile yet.
            assert mLastUsedNonOTRProfile.hasPrimaryOTRProfile() || isInOverviewAndShowingOmnibox();
            // Return the primary OTR profile.
            return mLastUsedNonOTRProfile.getPrimaryOTRProfile(/*createIfNeeded=*/true);
        }
        return mLastUsedNonOTRProfile;
    }

    public void setLayoutStateProvider(LayoutStateProvider layoutStateProvider) {
        mLayoutStateProvider = layoutStateProvider;
    }

    public void setShouldShowOmniboxInOverviewMode(boolean shouldShowOmniboxInOverviewMode) {
        if (mShouldShowOmniboxInOverviewMode != shouldShowOmniboxInOverviewMode) {
            mShouldShowOmniboxInOverviewMode = shouldShowOmniboxInOverviewMode;
            notifyPrimaryColorChanged();
        }
    }

    /**
     * Sets the primary color and changes the state for isUsingBrandColor.
     * @param color The primary color for the current tab.
     */
    public void setPrimaryColor(int color) {
        mPrimaryColor = color;
        updateUsingBrandColor();
        notifyPrimaryColorChanged();
    }

    private void updateUsingBrandColor() {
        mIsUsingBrandColor = !isIncognito()
                && mPrimaryColor != ChromeColors.getDefaultThemeColor(mContext, isIncognito())
                && hasTab() && !mTab.isNativePage();
    }

    @Override
    public int getPrimaryColor() {
        return isInOverviewAndShowingOmnibox()
                ? ChromeColors.getDefaultThemeColor(mContext, isIncognito())
                : mPrimaryColor;
    }

    @Override
    public boolean isUsingBrandColor() {
        // If the overview is visible, force use of primary color, which is also overridden when the
        // overview is visible.
        return isInOverviewAndShowingOmnibox() || mIsUsingBrandColor;
    }

    public void notifyPrimaryColorChanged() {
        for (LocationBarDataProvider.Observer observer : mLocationBarDataObservers) {
            observer.onPrimaryColorChanged();
        }
    }

    @Override
    public boolean isOfflinePage() {
        // Start Surface homepage is not bond with a tab and mTab is kept as the previous tab if
        // homepage is shown. |!isInOverviewAndShowingOmnibox()| is added here to make sure Start
        // Surface homepage is not regarded as offline.
        return hasTab() && mOfflineStatus.isOfflinePage(mTab) && !isInOverviewAndShowingOmnibox();
    }

    @Override
    public boolean isPaintPreview() {
        // Start Surface homepage is not bound with a tab and mTab is kept as the previous tab if
        // the homepage is shown. This is added here to make sure Start Surface homepage is not
        // regarded as a paint preview.
        if (isInOverviewAndShowingOmnibox()) return false;
        return hasTab() && TabbedPaintPreview.get(mTab).isShowing();
    }

    @Override
    public int getSecurityLevel() {
        Tab tab = getTab();
        String publisherUrl = tab != null ? TrustedCdn.getPublisherUrl(tab) : null;
        return getSecurityLevel(tab, isOfflinePage(), publisherUrl);
    }

    @Override
    public int getPageClassification(boolean isFocusedFromFakebox) {
        if (mNativeLocationBarModelAndroid == 0) return PageClassification.INVALID_SPEC_VALUE;

        // Provide NTP as page class in overview mode (when Start Surface is enabled). No call
        // to the backend necessary or possible, since there is no tab or navigation entry.
        if (isInOverviewAndShowingOmnibox()) return PageClassification.NTP_VALUE;

        return LocationBarModelJni.get().getPageClassification(
                mNativeLocationBarModelAndroid, LocationBarModel.this, isFocusedFromFakebox);
    }

    @Override
    public @DrawableRes int getSecurityIconResource(boolean isTablet) {
        return getSecurityIconResource(
                getSecurityLevel(), !isTablet, isOfflinePage(), isPaintPreview());
    }

    @Override
    @StringRes
    public int getSecurityIconContentDescriptionResourceId() {
        return SecurityStatusIcon.getSecurityIconContentDescriptionResourceId(getSecurityLevel());
    }

    @Override
    public int getDropdownStandardBackgroundColor() {
        return mDropdownStandardBgColor;
    }

    @Override
    public int getDropdownIncognitoBackgroundColor() {
        return mDropdownIncognitoBgColor;
    }

    @Override
    public int getSuggestionStandardBackgroundColor() {
        return mSuggestionStandardBgColor;
    }

    @Override
    public int getSuggestionIncognitoBackgroundColor() {
        return mSuggestionIncognitoBgColor;
    }

    @VisibleForTesting
    @ConnectionSecurityLevel
    int getSecurityLevel(Tab tab, boolean isOfflinePage, @Nullable String publisherUrl) {
        if (tab == null || isOfflinePage || isInOverviewAndShowingOmnibox()) {
            return ConnectionSecurityLevel.NONE;
        }

        if (publisherUrl != null) {
            assert getSecurityLevelFromStateModel(tab.getWebContents())
                    != ConnectionSecurityLevel.DANGEROUS;
            return (URI.create(publisherUrl).getScheme().equals(UrlConstants.HTTPS_SCHEME))
                    ? ConnectionSecurityLevel.SECURE
                    : ConnectionSecurityLevel.WARNING;
        }
        return getSecurityLevelFromStateModel(tab.getWebContents());
    }

    @VisibleForTesting
    @ConnectionSecurityLevel
    int getSecurityLevelFromStateModel(WebContents webContents) {
        int securityLevel = SecurityStateModel.getSecurityLevelForWebContents(webContents);
        return securityLevel;
    }

    @VisibleForTesting
    PrefService getPrefService() {
        return UserPrefs.get(Profile.getLastUsedRegularProfile());
    }

    @VisibleForTesting
    @DrawableRes
    int getSecurityIconResource(int securityLevel, boolean isSmallDevice, boolean isOfflinePage,
            boolean isPaintPreview) {
        // Paint Preview appears on top of WebContents and shows a visual representation of the page
        // that has been previously stored locally.
        if (isPaintPreview) return R.drawable.omnibox_info;

        // Checking for a preview first because one possible preview type is showing an offline page
        // on a slow connection. In this case, the previews UI takes precedence.
        if (isOfflinePage) {
            return R.drawable.ic_offline_pin_24dp;
        }

        // Return early if native initialization hasn't been done yet.
        if ((securityLevel == ConnectionSecurityLevel.NONE
                    || securityLevel == ConnectionSecurityLevel.WARNING)
                && mNativeLocationBarModelAndroid == 0) {
            return R.drawable.omnibox_info;
        }

        boolean skipIconForNeutralState =
                !mSearchEngineLogoUtils.shouldShowSearchEngineLogo(isIncognito())
                || mNtpDelegate.isCurrentlyVisible() || isInOverviewAndShowingOmnibox();

        boolean useLockIconEnabled = false;
        if (mNativeLocationBarModelAndroid != 0) {
            PrefService prefService = getPrefService();
            if (prefService.isManagedPreference(
                        ChromePreferenceKeys.LOCK_ICON_IN_ADDRESS_BAR_ENABLED)) {
                useLockIconEnabled = prefService.getBoolean(
                        ChromePreferenceKeys.LOCK_ICON_IN_ADDRESS_BAR_ENABLED);
            }
        }

        boolean useUpdatedConnectionSecurityIndicators = FeatureList.isInitialized()
                && ChromeFeatureList.isEnabled(
                        ChromeFeatureList.OMNIBOX_UPDATED_CONNECTION_SECURITY_INDICATORS)
                && !useLockIconEnabled && !(hasTab() && mTab.isCustomTab());

        return SecurityStatusIcon.getSecurityIconResource(securityLevel, isSmallDevice,
                skipIconForNeutralState, useUpdatedConnectionSecurityIndicators);
    }

    @Override
    public @ColorRes int getSecurityIconColorStateList() {
        final @ColorInt int color = getPrimaryColor();
        final @BrandedColorScheme int brandedColorScheme =
                OmniboxResourceProvider.getBrandedColorScheme(mContext, isIncognito(), color);

        // Assign red color to security icon if the page shows security warning.
        return getSecurityIconColorWithSecurityLevel(
                getSecurityLevel(), brandedColorScheme, isIncognito());
    }

    /**
     * Get the color for the security icon for different security levels.
     * If we are using dark background (dark mode or incognito mode), we should return light red.
     * If we are using light background (light mode, but not LIGHT_BRANDED_THEME), we should return
     * dark red. The default brand color will be returned if no change is needed.
     *
     * @param connectionSecurityLevel The connection security level for the current website.
     * @param brandedColorScheme The branded color scheme for the omnibox.
     * @param isIncognito Whether the tab is in Incognito mode.
     * @return The color resource for the security icon, returns -1 if doe snot need to change
     *         color.
     */
    @VisibleForTesting
    protected @ColorRes int getSecurityIconColorWithSecurityLevel(
            @ConnectionSecurityLevel int connectionSecurityLevel,
            @BrandedColorScheme int brandedColorScheme, boolean isIncognito) {
        // Return regular color scheme if the website does not show warning.
        if (connectionSecurityLevel == ConnectionSecurityLevel.DANGEROUS) {
            // Assign red color only on light or dark background including Incognito mode.
            // We will not change the security icon to red when BrandedColorScheme is
            // LIGHT_BRANDED_THEME for the purpose of improving contrast.
            if (isIncognito) {
                // Use light red for Incognito mode.
                return R.color.baseline_error_200;
            } else if (brandedColorScheme == BrandedColorScheme.APP_DEFAULT) {
                // Use adaptive red for light and dark background.
                return R.color.default_red;
            }
        }
        return ThemeUtils.getThemedToolbarIconTintRes(brandedColorScheme);
    }

    public void notifySecurityStateChanged() {
        for (LocationBarDataProvider.Observer observer : mLocationBarDataObservers) {
            observer.onSecurityStateChanged();
        }
    }

    /** @return The formatted URL suitable for editing. */
    public String getFormattedFullUrl() {
        if (mNativeLocationBarModelAndroid == 0) return "";
        return LocationBarModelJni.get().getFormattedFullURL(
                mNativeLocationBarModelAndroid, LocationBarModel.this);
    }

    /** @return The formatted URL suitable for display only. */
    public String getUrlForDisplay() {
        if (mNativeLocationBarModelAndroid == 0) return "";
        return LocationBarModelJni.get().getURLForDisplay(
                mNativeLocationBarModelAndroid, LocationBarModel.this);
    }

    /**
     * Set whether tab switcher is showing or not and notify changes.
     * @param isShowingTabSwitcher Whether tab switcher is showing or not.
     */
    public void setIsShowingTabSwitcher(boolean isShowingTabSwitcher) {
        mIsShowingTabSwitcher = isShowingTabSwitcher;
        notifyTitleChanged();
        notifyUrlChanged();
        notifyPrimaryColorChanged();
        notifySecurityStateChanged();
    }

    @NativeMethods
    interface Natives {
        long init(LocationBarModel caller);
        void destroy(long nativeLocationBarModelAndroid, LocationBarModel caller);
        String getFormattedFullURL(long nativeLocationBarModelAndroid, LocationBarModel caller);
        String getURLForDisplay(long nativeLocationBarModelAndroid, LocationBarModel caller);
        int getPageClassification(long nativeLocationBarModelAndroid, LocationBarModel caller,
                boolean isFocusedFromFakebox);
    }
}
