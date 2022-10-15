// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
package org.chromium.chrome.browser.tab;

import android.content.Context;
import android.content.res.Resources;
import android.text.TextUtils;

import androidx.annotation.IntDef;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.annotation.VisibleForTesting;

import org.chromium.base.SysUtils;
import org.chromium.base.metrics.RecordHistogram;
import org.chromium.base.metrics.RecordUserAction;
import org.chromium.chrome.R;
import org.chromium.chrome.browser.feature_engagement.TrackerFactory;
import org.chromium.chrome.browser.flags.ChromeFeatureList;
import org.chromium.chrome.browser.metrics.UmaSessionStats;
import org.chromium.chrome.browser.page_info.SiteSettingsHelper;
import org.chromium.chrome.browser.preferences.ChromePreferenceKeys;
import org.chromium.chrome.browser.preferences.SharedPreferencesManager;
import org.chromium.chrome.browser.profiles.Profile;
import org.chromium.chrome.browser.tab.TabUtils.LoadIfNeededCaller;
import org.chromium.chrome.browser.tab.state.CriticalPersistedTabData;
import org.chromium.chrome.browser.tabmodel.TabModel;
import org.chromium.chrome.browser.tabmodel.TabModelSelector;
import org.chromium.components.browser_ui.site_settings.SingleCategorySettings;
import org.chromium.components.browser_ui.site_settings.SingleCategorySettingsConstants;
import org.chromium.components.browser_ui.site_settings.SiteSettingsCategory;
import org.chromium.components.browser_ui.site_settings.SiteSettingsFeatureList;
import org.chromium.components.browser_ui.site_settings.WebsitePreferenceBridge;
import org.chromium.components.content_settings.ContentSettingValues;
import org.chromium.components.content_settings.ContentSettingsType;
import org.chromium.components.embedder_support.util.UrlUtilities;
import org.chromium.components.feature_engagement.FeatureConstants;
import org.chromium.components.feature_engagement.Tracker;
import org.chromium.components.messages.MessageBannerProperties;
import org.chromium.components.messages.MessageDispatcher;
import org.chromium.components.messages.MessageIdentifier;
import org.chromium.components.messages.PrimaryActionClickBehavior;
import org.chromium.components.profile_metrics.BrowserProfileType;
import org.chromium.components.ukm.UkmRecorder;
import org.chromium.content_public.browser.ContentFeatureList;
import org.chromium.ui.modaldialog.DialogDismissalCause;
import org.chromium.ui.modaldialog.ModalDialogManager;
import org.chromium.ui.modaldialog.ModalDialogManager.ModalDialogType;
import org.chromium.ui.modaldialog.ModalDialogProperties;
import org.chromium.ui.modaldialog.ModalDialogProperties.ButtonType;
import org.chromium.ui.modaldialog.ModalDialogProperties.Controller;
import org.chromium.ui.modelutil.PropertyModel;
import org.chromium.url.GURL;

import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.util.HashSet;

/**
 * Utilities for requesting desktop sites support.
 */
public class RequestDesktopUtils {
    private static final String ANY_SUBDOMAIN_PATTERN = "[*.]";
    private static final String SITE_WILDCARD = "*";

    static final String PARAM_GLOBAL_SETTING_DEFAULT_ON_DISPLAY_SIZE_THRESHOLD_INCHES =
            "default_on_display_size_threshold_inches";
    static final double DEFAULT_GLOBAL_SETTING_DEFAULT_ON_DISPLAY_SIZE_THRESHOLD_INCHES = 12.0;
    static final String PARAM_GLOBAL_SETTING_DEFAULT_ON_ON_LOW_END_DEVICES =
            "default_on_on_low_end_devices";
    static final String PARAM_SHOW_MESSAGE_ON_GLOBAL_SETTING_DEFAULT_ON =
            "show_message_on_default_on";

    static final String PARAM_GLOBAL_SETTING_OPT_IN_ENABLED = "show_opt_in_message";
    static final String PARAM_GLOBAL_SETTING_OPT_IN_DISPLAY_SIZE_MIN_THRESHOLD_INCHES =
            "opt_in_display_size_min_threshold_inches";
    static final double DEFAULT_GLOBAL_SETTING_OPT_IN_DISPLAY_SIZE_MIN_THRESHOLD_INCHES = 10.0;
    static final String PARAM_GLOBAL_SETTING_OPT_IN_DISPLAY_SIZE_MAX_THRESHOLD_INCHES =
            "opt_in_display_size_max_threshold_inches";
    static final double DEFAULT_GLOBAL_SETTING_OPT_IN_DISPLAY_SIZE_MAX_THRESHOLD_INCHES =
            Double.MAX_VALUE;

    // Global defaults experiment constants.
    static final String ENABLED_GROUP_SUFFIX = "_Enabled";
    static final String CONTROL_GROUP_SUFFIX = "_Control";
    static final String DEFAULT_ON_GROUP_NAME_PREFIX = "DefaultOn_";
    static final String OPT_IN_GROUP_NAME_PREFIX = "OptIn_";

    // Note: these values must match the UserAgentRequestType enum in enums.xml.
    @IntDef({UserAgentRequestType.REQUEST_DESKTOP, UserAgentRequestType.REQUEST_MOBILE})
    @Retention(RetentionPolicy.SOURCE)
    private @interface UserAgentRequestType {
        int REQUEST_DESKTOP = 0;
        int REQUEST_MOBILE = 1;
    }

    // Note: these values must match the DeviceOrientation2 enum in enums.xml.
    @IntDef({DeviceOrientation2.LANDSCAPE, DeviceOrientation2.PORTRAIT})
    @Retention(RetentionPolicy.SOURCE)
    private @interface DeviceOrientation2 {
        int LANDSCAPE = 0;
        int PORTRAIT = 1;
    }

    /**
     * Records the metrics associated with changing the user agent by user agent.
     * @param isDesktop True if the user agent is the desktop.
     * @param tab The current activity {@link Tab}.
     */
    public static void recordUserChangeUserAgent(boolean isDesktop, @Nullable Tab tab) {
        if (ChromeFeatureList.sAppMenuMobileSiteOption.isEnabled() && !isDesktop) {
            RecordUserAction.record("MobileMenuRequestMobileSite");
        } else {
            RecordUserAction.record("MobileMenuRequestDesktopSite");
        }

        RecordHistogram.recordBooleanHistogram(
                "Android.RequestDesktopSite.UserSwitchToDesktop", isDesktop);

        if (tab == null || tab.isIncognito() || tab.getWebContents() == null) return;

        new UkmRecorder.Bridge().recordEventWithIntegerMetric(tab.getWebContents(),
                "Android.UserRequestedUserAgentChange", "UserAgentType",
                isDesktop ? UserAgentRequestType.REQUEST_DESKTOP
                          : UserAgentRequestType.REQUEST_MOBILE);
    }

    /**
     * Records the ukms associated with changing screen orientation.
     * @param isLandscape True if the orientation is landscape.
     * @param tab The current activity {@link Tab}.
     */
    public static void recordScreenOrientationChangedUkm(boolean isLandscape, @Nullable Tab tab) {
        if (tab == null || tab.isIncognito() || tab.getWebContents() == null) return;

        new UkmRecorder.Bridge().recordEventWithIntegerMetric(tab.getWebContents(),
                "Android.ScreenRotation", "TargetDeviceOrientation",
                isLandscape ? DeviceOrientation2.LANDSCAPE : DeviceOrientation2.PORTRAIT);
    }

    /**
     * Set or remove a domain level exception with URL for {@link
     * ContentSettingsType.REQUEST_DESKTOP_SITE}. Clear the subdomain level exception if any.
     * @param profile Target profile whose content settings needs to be updated.
     * @param url  {@link GURL} for the site that changes in desktop user agent.
     * @param useDesktopUserAgent True if the input |url| needs to use desktop user agent.
     */
    public static void setRequestDesktopSiteContentSettingsForUrl(
            Profile profile, GURL url, boolean useDesktopUserAgent) {
        boolean isIncognito =
                Profile.getBrowserProfileTypeFromProfile(profile) == BrowserProfileType.INCOGNITO;
        String domainAndRegistry =
                UrlUtilities.getDomainAndRegistry(url.getSpec(), /*includePrivateRegistries*/ true);
        // Use host only (no scheme/port/path) for ContentSettings to ensure consistency.
        String hostPattern;
        if (TextUtils.isEmpty(domainAndRegistry)) {
            // Use host directly if fails to extract domain from url (e.g. ip address).
            hostPattern = url.getHost();
        } else {
            hostPattern = ANY_SUBDOMAIN_PATTERN + domainAndRegistry;
            // Clear subdomain level exception if any.
            WebsitePreferenceBridge.setContentSettingCustomScope(profile,
                    ContentSettingsType.REQUEST_DESKTOP_SITE, url.getHost(),
                    /*secondaryPattern*/ SITE_WILDCARD, ContentSettingValues.DEFAULT);
        }
        @ContentSettingValues
        int defaultValue = WebsitePreferenceBridge.getDefaultContentSetting(
                profile, ContentSettingsType.REQUEST_DESKTOP_SITE);
        assert defaultValue == ContentSettingValues.ALLOW
                || defaultValue == ContentSettingValues.BLOCK;
        boolean rdsGlobalSetting = defaultValue == ContentSettingValues.ALLOW;
        @ContentSettingValues
        int contentSettingValue =
                useDesktopUserAgent ? ContentSettingValues.ALLOW : ContentSettingValues.BLOCK;
        // For normal profile, remove domain level setting if it matches the global setting.
        // For incognito profile, keep the domain level setting to override the settings from normal
        // profile.
        if (!isIncognito && useDesktopUserAgent == rdsGlobalSetting) {
            contentSettingValue = ContentSettingValues.DEFAULT;
        }

        // Set or remove a domain level exception.
        WebsitePreferenceBridge.setContentSettingCustomScope(profile,
                ContentSettingsType.REQUEST_DESKTOP_SITE, hostPattern,
                /*secondaryPattern*/ SITE_WILDCARD, contentSettingValue);
    }

    /**
     * Create a SharedPreferences string set of tab IDs of all tabs in the tab model to update the
     * tab user agent once when desktop site domain level settings are downgraded to tab level
     * settings.
     * @param tabModelSelector The {@link TabModelSelector} that will provide information about
     *         {@link Tab}s in all {@link TabModel}s.
     */
    public static void maybeDowngradeSiteSettings(TabModelSelector tabModelSelector) {
        SharedPreferencesManager sharedPreferencesManager = SharedPreferencesManager.getInstance();
        if (ContentFeatureList.isEnabled(ContentFeatureList.REQUEST_DESKTOP_SITE_EXCEPTIONS)) {
            // Remove the SharedPreferences keys if they exist when desktop site exceptions are
            // re-enabled.
            SharedPreferencesManager.getInstance().removeKey(
                    ChromePreferenceKeys.DESKTOP_SITE_EXCEPTIONS_DOWNGRADE_TAB_SETTING_SET);
            SharedPreferencesManager.getInstance().removeKey(
                    ChromePreferenceKeys.DESKTOP_SITE_EXCEPTIONS_DOWNGRADE_GLOBAL_SETTING_ENABLED);
            return;
        }
        if (!SiteSettingsFeatureList.isEnabled(
                    SiteSettingsFeatureList.REQUEST_DESKTOP_SITE_EXCEPTIONS_DOWNGRADE)) {
            return;
        }
        // Restore tab level settings exactly once after downgrade.
        if (sharedPreferencesManager.contains(
                    ChromePreferenceKeys.DESKTOP_SITE_EXCEPTIONS_DOWNGRADE_TAB_SETTING_SET)) {
            return;
        }

        var tabIds = new HashSet<String>();
        for (var tabModel : tabModelSelector.getModels()) {
            for (int index = 0; index < tabModel.getCount(); index++) {
                var tab = tabModel.getTabAt(index);
                tabIds.add(String.valueOf(tab.getId()));
            }
        }

        sharedPreferencesManager.writeStringSet(
                ChromePreferenceKeys.DESKTOP_SITE_EXCEPTIONS_DOWNGRADE_TAB_SETTING_SET, tabIds);
        // Preserve the global setting value from prior to downgrade to eventually update the
        // TabUserAgent for all current tabs with respect to this value.
        sharedPreferencesManager.writeBoolean(
                ChromePreferenceKeys.DESKTOP_SITE_EXCEPTIONS_DOWNGRADE_GLOBAL_SETTING_ENABLED,
                WebsitePreferenceBridge.isCategoryEnabled(
                        tabModelSelector.getCurrentModel().getProfile(),
                        ContentSettingsType.REQUEST_DESKTOP_SITE));
    }

    /**
     * Restore the tab level setting for a tab that was in use before desktop site domain level
     * settings were downgraded. This method specifically updates the CriticalPersistedTabData user
     * agent, but does not actually apply/change the user agent for the current web contents; it is
     * expected of the caller to do so.
     * @param tab The {@link Tab} whose tab level setting may be restored.
     */
    public static void maybeRestoreUserAgentOnSiteSettingsDowngrade(@NonNull Tab tab) {
        if (!isDesktopSiteExceptionsDowngradeEnabledForTab(tab.getId())
                || tab.getWebContents() == null) {
            return;
        }

        SharedPreferencesManager sharedPreferencesManager = SharedPreferencesManager.getInstance();

        // Get the user agent used by the tab from the last committed entry and update the tab level
        // setting.
        boolean usingDesktopUserAgent =
                tab.getWebContents().getNavigationController().getUseDesktopUserAgent();
        @TabUserAgent
        int tabUserAgent = usingDesktopUserAgent ? TabUserAgent.DESKTOP : TabUserAgent.MOBILE;

        // Retrieve the global setting from prior to downgrade, or use the current global setting.
        boolean globalEnabled =
                sharedPreferencesManager.contains(
                        ChromePreferenceKeys
                                .DESKTOP_SITE_EXCEPTIONS_DOWNGRADE_GLOBAL_SETTING_ENABLED)
                ? sharedPreferencesManager.readBoolean(
                        ChromePreferenceKeys
                                .DESKTOP_SITE_EXCEPTIONS_DOWNGRADE_GLOBAL_SETTING_ENABLED,
                        false)
                : TabUtils.isDesktopSiteGlobalEnabled(
                        Profile.fromWebContents(tab.getWebContents()));

        if (globalEnabled != usingDesktopUserAgent) {
            // Update the persisted TabUserAgent for a tab from DEFAULT if the user agent it last
            // used is different from the global setting.
            CriticalPersistedTabData.from(tab).setUserAgent(tabUserAgent);
        }

        // Remove the ID of the processed tab from the SharedPreferences string set so that its tab
        // level setting is not processed again.
        sharedPreferencesManager.removeFromStringSet(
                ChromePreferenceKeys.DESKTOP_SITE_EXCEPTIONS_DOWNGRADE_TAB_SETTING_SET,
                String.valueOf(tab.getId()));
    }

    /**
     * Upgrade a non-default tab level RDS setting to a domain level setting when RDS exceptions is
     * supported. This method is expected to be invoked only once after support is added for domain
     * level exceptions.
     * @param tab The {@link Tab} for which the RDS setting will be upgraded.
     * @param profile The {@link Profile} used to upgrade the RDS setting.
     * @param tabUserAgent The current {@link TabUserAgent} set for the tab.
     * @param url The {@link GURL} for which a domain level exception will be added.
     */
    public static void maybeUpgradeTabLevelDesktopSiteSetting(
            Tab tab, Profile profile, @TabUserAgent int tabUserAgent, @Nullable GURL url) {
        if (!ContentFeatureList.isEnabled(ContentFeatureList.REQUEST_DESKTOP_SITE_EXCEPTIONS)
                || url == null) {
            return;
        }

        // If the tab UA is UNSET, it represents a state before tab level settings were applied for
        // the tab, so the domain level setting cannot be upgraded to at this time.
        if (tabUserAgent == TabUserAgent.UNSET) {
            return;
        }

        RequestDesktopUtils.setRequestDesktopSiteContentSettingsForUrl(
                profile, url, tabUserAgent == TabUserAgent.DESKTOP);
        // Reset the tab level setting after upgrade.
        CriticalPersistedTabData.from(tab).setUserAgent(TabUserAgent.DEFAULT);
    }

    /**
     * Determines whether the desktop site global setting should be enabled by default.
     * Also contains logic to support extra GWS visibility for the Finch experiment; see
     * crbug.com/1362914 for details.
     * @param displaySizeInInches The device primary display size, in inches.
     * @return Whether the desktop site global setting should be default-enabled.
     */
    static boolean shouldDefaultEnableGlobalSetting(double displaySizeInInches) {
        if (!ChromeFeatureList.isEnabled(ChromeFeatureList.REQUEST_DESKTOP_SITE_DEFAULTS)
                && !ChromeFeatureList.isEnabled(
                        ChromeFeatureList.REQUEST_DESKTOP_SITE_DEFAULTS_CONTROL)) {
            return false;
        }

        // Ascertain if the device is assigned to the control group in the Finch experiment based on
        // the status of the Finch flag.
        boolean isControlGroup = ChromeFeatureList.isEnabled(
                ChromeFeatureList.REQUEST_DESKTOP_SITE_DEFAULTS_CONTROL);

        String feature = isControlGroup ? ChromeFeatureList.REQUEST_DESKTOP_SITE_DEFAULTS_CONTROL
                                        : ChromeFeatureList.REQUEST_DESKTOP_SITE_DEFAULTS;

        // If the device is part of an opt-in experiment arm, avoid default-enabling the setting.
        if (ChromeFeatureList.getFieldTrialParamByFeatureAsBoolean(
                    feature, PARAM_GLOBAL_SETTING_OPT_IN_ENABLED, false)) {
            return false;
        }

        // Check whether default-on for low end devices is disabled.
        if (!ChromeFeatureList.getFieldTrialParamByFeatureAsBoolean(
                    feature, PARAM_GLOBAL_SETTING_DEFAULT_ON_ON_LOW_END_DEVICES, true)
                && SysUtils.isLowEndDevice()) {
            return false;
        }

        SharedPreferencesManager sharedPreferencesManager = SharedPreferencesManager.getInstance();

        boolean previouslyDefaultEnabled = sharedPreferencesManager.readBoolean(
                ChromePreferenceKeys.DEFAULT_ENABLED_DESKTOP_SITE_GLOBAL_SETTING, false);
        boolean previouslyUpdatedByUser = sharedPreferencesManager.contains(
                SingleCategorySettingsConstants
                        .USER_ENABLED_DESKTOP_SITE_GLOBAL_SETTING_PREFERENCE_KEY);

        double screenSizeThreshold = ChromeFeatureList.getFieldTrialParamByFeatureAsDouble(feature,
                PARAM_GLOBAL_SETTING_DEFAULT_ON_DISPLAY_SIZE_THRESHOLD_INCHES,
                DEFAULT_GLOBAL_SETTING_DEFAULT_ON_DISPLAY_SIZE_THRESHOLD_INCHES);

        boolean inCohort = !previouslyUpdatedByUser && displaySizeInInches >= screenSizeThreshold;
        boolean wouldEnable = !previouslyDefaultEnabled && inCohort;
        if (wouldEnable) {
            // Store a SharedPreferences key to tag the device as qualified for the feature
            // experiment for ongoing tracking in both enabled and control groups.
            sharedPreferencesManager.writeBoolean(
                    ChromePreferenceKeys.DEFAULT_ENABLE_DESKTOP_SITE_GLOBAL_SETTING_COHORT, true);
        }

        if (inCohort
                || sharedPreferencesManager.contains(
                        ChromePreferenceKeys.DEFAULT_ENABLE_DESKTOP_SITE_GLOBAL_SETTING_COHORT)) {
            maybeRegisterSyntheticFieldTrials(
                    isControlGroup, screenSizeThreshold, /*isOptInArm*/ false);
        }

        // Should enable the setting only in the enabled (not control) experiment group.
        return !isControlGroup && wouldEnable;
    }

    /**
     * Default-enables the desktop site global setting if {@code shouldDefaultEnableGlobalSetting}
     * returns true.
     * @param displaySizeInInches The device primary display size, in inches.
     * @param profile The current {@link Profile}.
     * @return Whether the desktop site global setting was default-enabled.
     */
    public static boolean maybeDefaultEnableGlobalSetting(
            double displaySizeInInches, Profile profile) {
        if (!shouldDefaultEnableGlobalSetting(displaySizeInInches)) {
            return false;
        }

        WebsitePreferenceBridge.setCategoryEnabled(
                profile, ContentSettingsType.REQUEST_DESKTOP_SITE, true);
        SharedPreferencesManager.getInstance().writeBoolean(
                ChromePreferenceKeys.DEFAULT_ENABLED_DESKTOP_SITE_GLOBAL_SETTING, true);
        // This key will be added only once, since this method will be invoked only once on a
        // device. Once the corresponding message is shown, the key will be removed since the
        // message will also be shown at most once.
        SharedPreferencesManager.getInstance().writeBoolean(
                ChromePreferenceKeys.DEFAULT_ENABLED_DESKTOP_SITE_GLOBAL_SETTING_SHOW_MESSAGE,
                true);
        return true;
    }

    /**
     * Disables the desktop site global setting when REQUEST_DESKTOP_SITE_DEFAULTS is disabled based
     * on the following conditions:
     * 1. The setting was previously default-enabled.
     * 2. The setting has not been previously updated by the user.
     * These changes are guarded behind the REQUEST_DESKTOP_SITE_DEFAULTS_DOWNGRADE flag.
     * @param profile The current {@link Profile}.
     * @return Whether the desktop site global setting was disabled.
     */
    public static boolean maybeDisableGlobalSetting(Profile profile) {
        if (ChromeFeatureList.isEnabled(ChromeFeatureList.REQUEST_DESKTOP_SITE_DEFAULTS)
                || !ChromeFeatureList.isEnabled(
                        ChromeFeatureList.REQUEST_DESKTOP_SITE_DEFAULTS_DOWNGRADE)) {
            return false;
        }

        SharedPreferencesManager sharedPreferencesManager = SharedPreferencesManager.getInstance();
        // Do not downgrade if the global setting was not default-enabled.
        if (!sharedPreferencesManager.readBoolean(
                    ChromePreferenceKeys.DEFAULT_ENABLED_DESKTOP_SITE_GLOBAL_SETTING, false)) {
            return false;
        }

        // Remove SharedPreferences keys that were added when the feature was supported.
        sharedPreferencesManager.removeKey(
                ChromePreferenceKeys.DEFAULT_ENABLED_DESKTOP_SITE_GLOBAL_SETTING);
        sharedPreferencesManager.removeKey(
                ChromePreferenceKeys.DEFAULT_ENABLED_DESKTOP_SITE_GLOBAL_SETTING_SHOW_MESSAGE);
        sharedPreferencesManager.removeKey(
                ChromePreferenceKeys.DEFAULT_ENABLE_DESKTOP_SITE_GLOBAL_SETTING_COHORT);

        // Do not disable the global setting if it was previously updated by the user.
        if (sharedPreferencesManager.contains(
                    SingleCategorySettingsConstants
                            .USER_ENABLED_DESKTOP_SITE_GLOBAL_SETTING_PREFERENCE_KEY)) {
            return false;
        }
        WebsitePreferenceBridge.setCategoryEnabled(
                profile, ContentSettingsType.REQUEST_DESKTOP_SITE, false);
        return true;
    }

    /**
     * Creates and shows a message to notify the user of a default update to the desktop site global
     * setting.
     * @param profile The current {@link Profile}.
     * @param messageDispatcher The {@link MessageDispatcher} to enqueue the message.
     * @param context The current context.
     * @return Whether the message was shown.
     */
    public static boolean maybeShowDefaultEnableGlobalSettingMessage(
            Profile profile, MessageDispatcher messageDispatcher, Context context) {
        if (messageDispatcher == null) return false;

        // Present the message only if the global setting has been default-enabled.
        if (!SharedPreferencesManager.getInstance().contains(
                    ChromePreferenceKeys
                            .DEFAULT_ENABLED_DESKTOP_SITE_GLOBAL_SETTING_SHOW_MESSAGE)) {
            return false;
        }

        // Since there might be a delay in triggering this message after the desktop site global
        // setting is default-enabled, it could be possible that the user subsequently disabled the
        // setting. Present the message only if the setting is enabled.
        if (!WebsitePreferenceBridge.isCategoryEnabled(
                    profile, ContentSettingsType.REQUEST_DESKTOP_SITE)) {
            SharedPreferencesManager.getInstance().removeKey(
                    ChromePreferenceKeys.DEFAULT_ENABLED_DESKTOP_SITE_GLOBAL_SETTING_SHOW_MESSAGE);
            return false;
        }

        // Do not show the message if disabled through Finch.
        if (!ChromeFeatureList.getFieldTrialParamByFeatureAsBoolean(
                    ChromeFeatureList.REQUEST_DESKTOP_SITE_DEFAULTS,
                    PARAM_SHOW_MESSAGE_ON_GLOBAL_SETTING_DEFAULT_ON, true)) {
            return false;
        }

        Resources resources = context.getResources();
        PropertyModel message =
                new PropertyModel.Builder(MessageBannerProperties.ALL_KEYS)
                        .with(MessageBannerProperties.MESSAGE_IDENTIFIER,
                                MessageIdentifier.DESKTOP_SITE_GLOBAL_DEFAULT_OPT_OUT)
                        .with(MessageBannerProperties.TITLE,
                                resources.getString(R.string.rds_global_default_on_message_title))
                        .with(MessageBannerProperties.ICON_RESOURCE_ID,
                                R.drawable.ic_desktop_windows)
                        .with(MessageBannerProperties.PRIMARY_BUTTON_TEXT,
                                resources.getString(R.string.rds_global_default_on_message_button))
                        .with(MessageBannerProperties.ON_PRIMARY_ACTION,
                                () -> {
                                    SiteSettingsHelper.showCategorySettings(context,
                                            SiteSettingsCategory.Type.REQUEST_DESKTOP_SITE);
                                    return PrimaryActionClickBehavior.DISMISS_IMMEDIATELY;
                                })
                        .build();

        messageDispatcher.enqueueWindowScopedMessage(message, false);
        SharedPreferencesManager.getInstance().removeKey(
                ChromePreferenceKeys.DEFAULT_ENABLED_DESKTOP_SITE_GLOBAL_SETTING_SHOW_MESSAGE);
        return true;
    }

    /**
     * Determines whether a message to opt-in to the desktop site global setting should be shown.
     * Also contains logic to support extra GWS visibility for the Finch experiment; see
     * crbug.com/1362914 for details.
     * @param displaySizeInInches The device primary display size, in inches.
     * @param profile The current {@link Profile}.
     * @return Whether the message to opt-in to the desktop site global setting should be shown.
     */
    static boolean shouldShowGlobalSettingOptInMessage(
            double displaySizeInInches, Profile profile) {
        if (!ChromeFeatureList.isEnabled(ChromeFeatureList.REQUEST_DESKTOP_SITE_DEFAULTS)
                && !ChromeFeatureList.isEnabled(
                        ChromeFeatureList.REQUEST_DESKTOP_SITE_DEFAULTS_CONTROL)) {
            return false;
        }

        // Ascertain if the device is assigned to the control group in the Finch experiment based on
        // the status of the Finch flag.
        boolean isControlGroup = ChromeFeatureList.isEnabled(
                ChromeFeatureList.REQUEST_DESKTOP_SITE_DEFAULTS_CONTROL);

        String feature = isControlGroup ? ChromeFeatureList.REQUEST_DESKTOP_SITE_DEFAULTS_CONTROL
                                        : ChromeFeatureList.REQUEST_DESKTOP_SITE_DEFAULTS;

        // Present the message only if opt-in is enabled.
        if (!ChromeFeatureList.getFieldTrialParamByFeatureAsBoolean(
                    feature, PARAM_GLOBAL_SETTING_OPT_IN_ENABLED, false)) {
            return false;
        }

        // Present the message only if the desktop site global setting is off.
        if (WebsitePreferenceBridge.isCategoryEnabled(
                    profile, ContentSettingsType.REQUEST_DESKTOP_SITE)) {
            return false;
        }

        SharedPreferencesManager sharedPreferencesManager = SharedPreferencesManager.getInstance();

        boolean previouslyShowedOptIn = sharedPreferencesManager.contains(
                ChromePreferenceKeys.DESKTOP_SITE_GLOBAL_SETTING_OPT_IN_MESSAGE_SHOWN);
        boolean previouslyUpdatedByUser = sharedPreferencesManager.contains(
                SingleCategorySettingsConstants
                        .USER_ENABLED_DESKTOP_SITE_GLOBAL_SETTING_PREFERENCE_KEY);

        double minScreenSizeThreshold = ChromeFeatureList.getFieldTrialParamByFeatureAsDouble(
                feature, PARAM_GLOBAL_SETTING_OPT_IN_DISPLAY_SIZE_MIN_THRESHOLD_INCHES,
                DEFAULT_GLOBAL_SETTING_OPT_IN_DISPLAY_SIZE_MIN_THRESHOLD_INCHES);
        double maxScreenSizeThreshold = ChromeFeatureList.getFieldTrialParamByFeatureAsDouble(
                feature, PARAM_GLOBAL_SETTING_OPT_IN_DISPLAY_SIZE_MAX_THRESHOLD_INCHES,
                DEFAULT_GLOBAL_SETTING_OPT_IN_DISPLAY_SIZE_MAX_THRESHOLD_INCHES);

        boolean inCohort = !previouslyUpdatedByUser && displaySizeInInches >= minScreenSizeThreshold
                && displaySizeInInches < maxScreenSizeThreshold;
        boolean wouldShowOptIn = !previouslyShowedOptIn && inCohort;
        if (wouldShowOptIn) {
            // Store a SharedPreferences key to tag the device as qualified for the feature
            // experiment for ongoing tracking in both enabled and control groups.
            sharedPreferencesManager.writeBoolean(
                    ChromePreferenceKeys.DESKTOP_SITE_GLOBAL_SETTING_OPT_IN_MESSAGE_COHORT, true);
        }

        if (inCohort
                || sharedPreferencesManager.contains(
                        ChromePreferenceKeys.DESKTOP_SITE_GLOBAL_SETTING_OPT_IN_MESSAGE_COHORT)) {
            maybeRegisterSyntheticFieldTrials(
                    isControlGroup, minScreenSizeThreshold, /*isOptInArm*/ true);
        }

        // Should show the opt-in message only in the enabled (not control) experiment group.
        return !isControlGroup && wouldShowOptIn;
    }

    /**
     * Creates and shows a message to the user to opt-in to the desktop site global setting based on
     * device conditions.
     * @param displaySizeInInches The device primary display size, in inches.
     * @param profile The current {@link Profile}.
     * @param messageDispatcher The {@link MessageDispatcher} to enqueue the message.
     * @param context The current context.
     * @param tab The {@link Tab} where the message is shown.
     * @return Whether the opt-in message was shown.
     */
    public static boolean maybeShowGlobalSettingOptInMessage(double displaySizeInInches,
            Profile profile, MessageDispatcher messageDispatcher, Context context, Tab tab) {
        if (messageDispatcher == null) return false;

        if (!shouldShowGlobalSettingOptInMessage(displaySizeInInches, profile)) {
            return false;
        }

        Resources resources = context.getResources();
        PropertyModel message =
                new PropertyModel.Builder(MessageBannerProperties.ALL_KEYS)
                        .with(MessageBannerProperties.MESSAGE_IDENTIFIER,
                                MessageIdentifier.DESKTOP_SITE_GLOBAL_OPT_IN)
                        .with(MessageBannerProperties.TITLE,
                                resources.getString(R.string.rds_global_opt_in_message_title))
                        .with(MessageBannerProperties.ICON_RESOURCE_ID,
                                R.drawable.ic_desktop_windows)
                        .with(MessageBannerProperties.PRIMARY_BUTTON_TEXT,
                                resources.getString(R.string.yes))
                        .with(MessageBannerProperties.ON_PRIMARY_ACTION,
                                () -> {
                                    updateDesktopSiteGlobalSettingOnUserRequest(profile, true);
                                    // TODO(crbug.com/1350274): Remove this explicit load when this
                                    // bug is addressed.
                                    if (tab != null) {
                                        tab.loadIfNeeded(
                                                LoadIfNeededCaller
                                                        .MAYBE_SHOW_GLOBAL_SETTING_OPT_IN_MESSAGE);
                                    }
                                    return PrimaryActionClickBehavior.DISMISS_IMMEDIATELY;
                                })
                        .build();

        messageDispatcher.enqueueWindowScopedMessage(message, false);
        SharedPreferencesManager.getInstance().writeBoolean(
                ChromePreferenceKeys.DESKTOP_SITE_GLOBAL_SETTING_OPT_IN_MESSAGE_SHOWN, true);
        return true;
    }

    /**
     * Show a prompt to educate the user about the update in the behavior of the desktop site app
     * menu setting from a tab-level setting to a site-level setting.
     * @param profile The current {@link Profile}.
     * @param context The current context.
     * @param modalDialogManager The {@link ModalDialogManager} that will manage the dialog.
     * @return Whether the prompt was shown.
     */
    public static boolean maybeShowUserEducationPromptForAppMenuSelection(
            Profile profile, Context context, ModalDialogManager modalDialogManager) {
        // Avoid presenting the prompt in case of an incognito profile.
        if (Profile.getBrowserProfileTypeFromProfile(profile) == BrowserProfileType.INCOGNITO) {
            return false;
        }

        Tracker tracker = TrackerFactory.getTrackerForProfile(profile);
        if (!tracker.shouldTriggerHelpUI(FeatureConstants.REQUEST_DESKTOP_SITE_APP_MENU_FEATURE)) {
            return false;
        }

        Resources resources = context.getResources();
        Controller modalDialogController = new ModalDialogProperties.Controller() {
            @Override
            public void onClick(PropertyModel model, int buttonType) {
                if (buttonType == ButtonType.POSITIVE) {
                    modalDialogManager.dismissDialog(
                            model, DialogDismissalCause.POSITIVE_BUTTON_CLICKED);
                }
            }

            @Override
            public void onDismiss(PropertyModel model, int dismissalCause) {
                tracker.dismissed(FeatureConstants.REQUEST_DESKTOP_SITE_APP_MENU_FEATURE);
            }
        };
        PropertyModel dialog =
                new PropertyModel.Builder(ModalDialogProperties.ALL_KEYS)
                        .with(ModalDialogProperties.CONTROLLER, modalDialogController)
                        .with(ModalDialogProperties.TITLE,
                                resources.getString(
                                        R.string.rds_app_menu_user_education_dialog_title))
                        .with(ModalDialogProperties.MESSAGE_PARAGRAPH_1,
                                resources.getString(
                                        R.string.rds_app_menu_user_education_dialog_message))
                        .with(ModalDialogProperties.POSITIVE_BUTTON_TEXT,
                                resources.getString(R.string.got_it))
                        .with(ModalDialogProperties.CANCEL_ON_TOUCH_OUTSIDE, true)
                        .build();
        modalDialogManager.showDialog(dialog, ModalDialogType.APP, true);
        return true;
    }

    /**
     * Updates the desktop site content setting on user request.
     * @param profile The current {@link Profile}.
     * @param requestDesktopSite Whether the user requested for desktop sites globally.
     */
    static void updateDesktopSiteGlobalSettingOnUserRequest(
            Profile profile, boolean requestDesktopSite) {
        WebsitePreferenceBridge.setCategoryEnabled(
                profile, ContentSettingsType.REQUEST_DESKTOP_SITE, requestDesktopSite);
        SingleCategorySettings.recordSiteLayoutChanged(requestDesktopSite);
    }

    @VisibleForTesting
    static void maybeRegisterSyntheticFieldTrials(
            boolean isControlGroup, double screenSizeThreshold, boolean isOptInArm) {
        if (!UmaSessionStats.isMetricsServiceAvailable()) {
            return;
        }

        String thresholdAsString = String.valueOf(screenSizeThreshold).replace('.', '_');
        String baseGroupName =
                (isOptInArm ? OPT_IN_GROUP_NAME_PREFIX : DEFAULT_ON_GROUP_NAME_PREFIX)
                + thresholdAsString;

        String syntheticFeatureName = isControlGroup
                ? ChromeFeatureList.REQUEST_DESKTOP_SITE_DEFAULTS_CONTROL_SYNTHETIC
                : ChromeFeatureList.REQUEST_DESKTOP_SITE_DEFAULTS_SYNTHETIC;
        if (isOptInArm) {
            syntheticFeatureName = isControlGroup
                    ? ChromeFeatureList.REQUEST_DESKTOP_SITE_OPT_IN_CONTROL_SYNTHETIC
                    : ChromeFeatureList.REQUEST_DESKTOP_SITE_OPT_IN_SYNTHETIC;
        }

        if (!isControlGroup && !ChromeFeatureList.isEnabled(syntheticFeatureName)) {
            UmaSessionStats.registerSyntheticFieldTrial(
                    syntheticFeatureName, baseGroupName + ENABLED_GROUP_SUFFIX);
        } else if (isControlGroup && !ChromeFeatureList.isEnabled(syntheticFeatureName)) {
            UmaSessionStats.registerSyntheticFieldTrial(
                    syntheticFeatureName, baseGroupName + CONTROL_GROUP_SUFFIX);
        }
    }

    private static boolean isDesktopSiteExceptionsDowngradeEnabledForTab(int tabId) {
        if (ContentFeatureList.isEnabled(ContentFeatureList.REQUEST_DESKTOP_SITE_EXCEPTIONS)
                || !SiteSettingsFeatureList.isEnabled(
                        SiteSettingsFeatureList.REQUEST_DESKTOP_SITE_EXCEPTIONS_DOWNGRADE)) {
            return false;
        }
        return SharedPreferencesManager.getInstance()
                .readStringSet(
                        ChromePreferenceKeys.DESKTOP_SITE_EXCEPTIONS_DOWNGRADE_TAB_SETTING_SET)
                .contains(String.valueOf(tabId));
    }
}