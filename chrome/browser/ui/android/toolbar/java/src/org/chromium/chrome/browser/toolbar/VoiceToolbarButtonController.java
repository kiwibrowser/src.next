// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.toolbar;

import android.content.Context;
import android.content.res.Configuration;
import android.graphics.drawable.Drawable;
import android.view.View.OnClickListener;

import androidx.annotation.StringRes;

import org.chromium.base.FeatureList;
import org.chromium.base.ObserverList;
import org.chromium.base.metrics.RecordUserAction;
import org.chromium.base.supplier.Supplier;
import org.chromium.chrome.browser.flags.ChromeFeatureList;
import org.chromium.chrome.browser.lifecycle.ActivityLifecycleDispatcher;
import org.chromium.chrome.browser.lifecycle.ConfigurationChangedObserver;
import org.chromium.chrome.browser.tab.Tab;
import org.chromium.chrome.browser.toolbar.adaptive.AdaptiveToolbarFeatures;
import org.chromium.chrome.browser.toolbar.adaptive.AdaptiveToolbarFeatures.AdaptiveToolbarButtonVariant;
import org.chromium.chrome.browser.user_education.IPHCommandBuilder;
import org.chromium.components.browser_ui.widget.highlight.ViewHighlighter.HighlightParams;
import org.chromium.components.browser_ui.widget.highlight.ViewHighlighter.HighlightShape;
import org.chromium.components.embedder_support.util.UrlUtilities;
import org.chromium.components.feature_engagement.EventConstants;
import org.chromium.components.feature_engagement.FeatureConstants;
import org.chromium.components.feature_engagement.Tracker;
import org.chromium.ui.modaldialog.ModalDialogManager;
import org.chromium.ui.modaldialog.ModalDialogManager.ModalDialogManagerObserver;
import org.chromium.ui.modelutil.PropertyModel;

/**
 * Handles displaying the voice search button on toolbar depending on several conditions (e.g.
 * device width, whether NTP is shown, whether voice is enabled).
 *
 * TODO(crbug.com/1144976): Move this to ../voice/ along with VoiceRecognitionHandler and the
 * assistant support.
 */
public class VoiceToolbarButtonController
        implements ButtonDataProvider, ConfigurationChangedObserver {
    private static final String IPH_PROMO_PARAM = "generic_message";

    private final Supplier<Tab> mActiveTabSupplier;
    private final Supplier<Tracker> mTrackerSupplier;
    private final ActivityLifecycleDispatcher mActivityLifecycleDispatcher;

    private final ModalDialogManager mModalDialogManager;
    private final ModalDialogManagerObserver mModalDialogManagerObserver;

    private final VoiceSearchDelegate mVoiceSearchDelegate;

    private final ButtonDataImpl mButtonData;
    private final ObserverList<ButtonDataObserver> mObservers = new ObserverList<>();

    private int mScreenWidthDp;

    /**
     * Delegate interface for interacting with voice search.
     */
    public interface VoiceSearchDelegate {
        /**
         * @return True if voice search is enabled for the current session.
         */
        boolean isVoiceSearchEnabled();

        /**
         * Starts a voice search interaction.
         */
        void startVoiceRecognition();
    }

    /**
     * Creates a VoiceToolbarButtonController object.
     * @param context The Context for retrieving resources, etc.
     * @param buttonDrawable Drawable for the voice button.
     * @param activeTabSupplier Provides the currently displayed {@link Tab}.
     * @param trackerSupplier  Supplier for the current profile tracker.
     * @param activityLifecycleDispatcher Dispatcher for activity lifecycle events, e.g.
     *                                    configuration changes.
     * @param modalDialogManager Dispatcher for modal lifecycle events
     * @param voiceSearchDelegate Provides interaction with voice search.
     */
    public VoiceToolbarButtonController(Context context, Drawable buttonDrawable,
            Supplier<Tab> activeTabSupplier, Supplier<Tracker> trackerSupplier,
            ActivityLifecycleDispatcher activityLifecycleDispatcher,
            ModalDialogManager modalDialogManager, VoiceSearchDelegate voiceSearchDelegate) {
        mActiveTabSupplier = activeTabSupplier;
        mTrackerSupplier = trackerSupplier;

        // Register for onConfigurationChanged events, which notify on changes to screen width.
        mActivityLifecycleDispatcher = activityLifecycleDispatcher;
        mActivityLifecycleDispatcher.register(this);

        mModalDialogManagerObserver = new ModalDialogManagerObserver() {
            @Override
            public void onDialogAdded(PropertyModel model) {
                mButtonData.setEnabled(false);
                notifyObservers(mButtonData.canShow());
            }

            @Override
            public void onLastDialogDismissed() {
                mButtonData.setEnabled(true);
                notifyObservers(mButtonData.canShow());
            }
        };
        mModalDialogManager = modalDialogManager;
        mModalDialogManager.addObserver(mModalDialogManagerObserver);

        mVoiceSearchDelegate = voiceSearchDelegate;

        OnClickListener onClickListener = (view) -> {
            RecordUserAction.record("MobileTopToolbarVoiceButton");
            mVoiceSearchDelegate.startVoiceRecognition();

            if (mTrackerSupplier.hasValue()) {
                mTrackerSupplier.get().notifyEvent(
                        EventConstants.ADAPTIVE_TOOLBAR_CUSTOMIZATION_VOICE_SEARCH_OPENED);
            }
        };

        mButtonData = new ButtonDataImpl(/*canShow=*/false, buttonDrawable, onClickListener,
                R.string.accessibility_toolbar_btn_mic,
                /*supportsTinting=*/true, /*iphCommandBuilder=*/null, /*isEnabled=*/true,
                AdaptiveToolbarButtonVariant.VOICE);

        mScreenWidthDp = context.getResources().getConfiguration().screenWidthDp;
    }

    @Override
    public void onConfigurationChanged(Configuration configuration) {
        if (mScreenWidthDp == configuration.screenWidthDp) {
            return;
        }
        mScreenWidthDp = configuration.screenWidthDp;
        mButtonData.setCanShow(shouldShowVoiceButton(mActiveTabSupplier.get()));
        notifyObservers(mButtonData.canShow());
    }

    /** Triggers checking and possibly updating the mic visibility */
    public void updateMicButtonState() {
        mButtonData.setCanShow(shouldShowVoiceButton(mActiveTabSupplier.get()));
        notifyObservers(mButtonData.canShow());
    }

    @Override
    public void destroy() {
        mActivityLifecycleDispatcher.unregister(this);
        mModalDialogManager.removeObserver(mModalDialogManagerObserver);
        mObservers.clear();
    }

    @Override
    public void addObserver(ButtonDataObserver obs) {
        mObservers.addObserver(obs);
    }

    @Override
    public void removeObserver(ButtonDataObserver obs) {
        mObservers.removeObserver(obs);
    }

    @Override
    public ButtonData get(Tab tab) {
        mButtonData.setCanShow(shouldShowVoiceButton(tab));
        maybeSetIphCommandBuilder(tab);
        return mButtonData;
    }

    /**
     * Since Features are not yet initialized when ButtonData is created, use the
     * fist available opportunity to create and set IPHCommandBuilder. Once set it's
     * never updated.
     */
    private void maybeSetIphCommandBuilder(Tab tab) {
        if (mButtonData.getButtonSpec().getIPHCommandBuilder() != null || tab == null
                || !FeatureList.isInitialized()) {
            return;
        }

        IPHCommandBuilder iphCommandBuilder = null;
        if (AdaptiveToolbarFeatures.isCustomizationEnabled()) {
            iphCommandBuilder = createCustomizationIPHCommandBuilder(tab);
        } else {
            // No IPH features enabled.
            return;
        }

        mButtonData.updateIPHCommandBuilder(iphCommandBuilder);
    }

    private boolean shouldShowVoiceButton(Tab tab) {
        if (tab == null || tab.isIncognito() || !mVoiceSearchDelegate.isVoiceSearchEnabled()) {
            return false;
        }

        boolean isDeviceWideEnough =
                mScreenWidthDp >= AdaptiveToolbarFeatures.getDeviceMinimumWidthForShowingButton();
        if (!isDeviceWideEnough) return false;

        return UrlUtilities.isHttpOrHttps(tab.getUrl());
    }

    /** Returns whether the feature flags allow showing the mic icon in the toolbar. */
    public static boolean isToolbarMicEnabled() {
        if (!FeatureList.isInitialized()) return false;
        return AdaptiveToolbarFeatures.isCustomizationEnabled();
    }

    private void notifyObservers(boolean hint) {
        for (ButtonDataObserver observer : mObservers) {
            observer.buttonDataChanged(hint);
        }
    }

    private IPHCommandBuilder createVoiceButtonIPHCommandBuilder(Tab tab) {
        boolean useGenericMessage = ChromeFeatureList.getFieldTrialParamByFeatureAsBoolean(
                ChromeFeatureList.TOOLBAR_MIC_IPH_ANDROID, IPH_PROMO_PARAM, true);
        @StringRes
        int text = useGenericMessage ? R.string.iph_mic_toolbar_generic_message_text
                                     : R.string.iph_mic_toolbar_example_query_text;
        @StringRes
        int accessibilityText =
                useGenericMessage ? R.string.iph_mic_toolbar_generic_message_accessibility_text
                                  : R.string.iph_mic_toolbar_example_query_accessibility_text;

        HighlightParams params = new HighlightParams(HighlightShape.CIRCLE);
        params.setBoundsRespectPadding(true);
        IPHCommandBuilder iphCommandBuilder = new IPHCommandBuilder(tab.getContext().getResources(),
                FeatureConstants.IPH_MIC_TOOLBAR_FEATURE, text, accessibilityText)
                                                      .setHighlightParams(params);

        return iphCommandBuilder;
    }

    private IPHCommandBuilder createCustomizationIPHCommandBuilder(Tab tab) {
        HighlightParams params = new HighlightParams(HighlightShape.CIRCLE);
        params.setBoundsRespectPadding(true);
        IPHCommandBuilder iphCommandBuilder = new IPHCommandBuilder(tab.getContext().getResources(),
                FeatureConstants.ADAPTIVE_BUTTON_IN_TOP_TOOLBAR_CUSTOMIZATION_VOICE_SEARCH_FEATURE,
                /* stringId = */ R.string.adaptive_toolbar_button_voice_search_iph,
                /* accessibilityStringId = */ R.string.adaptive_toolbar_button_voice_search_iph)
                                                      .setHighlightParams(params);

        return iphCommandBuilder;
    }
}
