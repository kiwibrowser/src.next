// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.tasks.tab_management;

import android.content.Context;
import android.util.AttributeSet;
import android.view.View;
import android.view.ViewGroup;
import android.widget.CompoundButton.OnCheckedChangeListener;
import android.widget.LinearLayout;

import androidx.appcompat.widget.SwitchCompat;

import org.chromium.chrome.browser.price_tracking.PriceTrackingFeatures;
import org.chromium.chrome.browser.price_tracking.PriceTrackingUtilities;
import org.chromium.chrome.tab_ui.R;
import org.chromium.ui.widget.ChromeImageButton;

/**
 * The view for PriceTrackingSettings dialog related UIs.
 */
public class PriceTrackingDialogView extends LinearLayout {
    private SwitchCompat mTrackPricesSwitch;
    private ChromeImageButton mPriceAlertsArrow;
    private ViewGroup mPriceAnnotationsRowMenu;
    private ViewGroup mPriceAlertsRowMenu;

    public PriceTrackingDialogView(Context context, AttributeSet attrs) {
        super(context, attrs);
    }

    @Override
    protected void onFinishInflate() {
        super.onFinishInflate();
        mTrackPricesSwitch = (SwitchCompat) findViewById(R.id.track_prices_switch);
        mPriceAlertsArrow = (ChromeImageButton) findViewById(R.id.price_alerts_arrow);
        mPriceAnnotationsRowMenu = (ViewGroup) findViewById(R.id.price_annotations_row_menu_id);
        mPriceAlertsRowMenu = (ViewGroup) findViewById(R.id.price_alerts_row_menu_id);
    }

    /**
     * Update the checked/unchecked status of the track prices switch. This is called every time
     * before the dialog shows.
     */
    void updateSwitch() {
        mTrackPricesSwitch.setChecked(PriceTrackingUtilities.isTrackPricesOnTabsEnabled());
    }

    /**
     * Set OnCheckedChangeListener of the track prices switch.
     */
    void setupTrackPricesSwitchOnCheckedChangeListener(
            OnCheckedChangeListener onCheckedChangeListener) {
        mTrackPricesSwitch.setOnCheckedChangeListener(onCheckedChangeListener);
    }

    /**
     * Set OnClickListener of the price alerts arrow button.
     */
    void setupPriceAlertsArrowOnClickListener(OnClickListener onClickListener) {
        mPriceAlertsArrow.setOnClickListener(onClickListener);
    }

    /**
     * Set visibility of each row menu.
     */
    void setupRowMenuVisibility() {
        mPriceAnnotationsRowMenu.setVisibility(
                PriceTrackingFeatures.allowUsersToDisablePriceAnnotations() ? View.VISIBLE
                                                                            : View.GONE);
        mPriceAlertsRowMenu.setVisibility(
                PriceTrackingFeatures.isPriceDropNotificationEligible() ? View.VISIBLE : View.GONE);
        // At least one row should be visible.
        assert mPriceAnnotationsRowMenu.getVisibility() == View.VISIBLE
                || mPriceAlertsRowMenu.getVisibility() == View.VISIBLE;
    }
}
