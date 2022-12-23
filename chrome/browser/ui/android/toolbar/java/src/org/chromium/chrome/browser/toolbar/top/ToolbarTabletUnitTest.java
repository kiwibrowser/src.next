// Copyright 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
package org.chromium.chrome.browser.toolbar.top;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.anyInt;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import android.animation.ObjectAnimator;
import android.app.Activity;
import android.graphics.drawable.Drawable;
import android.os.Looper;
import android.view.View;
import android.widget.ImageButton;

import org.junit.After;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TestRule;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.MockitoAnnotations;
import org.robolectric.Robolectric;
import org.robolectric.Shadows;
import org.robolectric.annotation.LooperMode;
import org.robolectric.shadows.ShadowToast;

import org.chromium.base.FeatureList;
import org.chromium.base.test.BaseRobolectricTestRunner;
import org.chromium.chrome.R;
import org.chromium.chrome.browser.flags.ChromeFeatureList;
import org.chromium.chrome.browser.omnibox.LocationBarCoordinator;
import org.chromium.chrome.browser.omnibox.LocationBarCoordinatorTablet;
import org.chromium.chrome.browser.omnibox.LocationBarLayout;
import org.chromium.chrome.browser.omnibox.status.StatusCoordinator;
import org.chromium.chrome.browser.toolbar.menu_button.MenuButtonCoordinator;
import org.chromium.chrome.test.util.browser.Features;
import org.chromium.chrome.test.util.browser.Features.EnableFeatures;

import java.util.ArrayList;

/**
 * Unit tests for @{@link ToolbarTablet}
 */
@LooperMode(LooperMode.Mode.PAUSED)
@RunWith(BaseRobolectricTestRunner.class)
public final class ToolbarTabletUnitTest {
    @Rule
    public TestRule mFeaturesProcessorRule = new Features.JUnitProcessor();
    @Mock
    private LocationBarCoordinator mLocationBar;
    @Mock
    private Drawable mDrawable;
    @Mock
    private LocationBarCoordinatorTablet mLocationBarTablet;
    @Mock
    private StatusCoordinator mStatusCoordinator;
    @Mock
    private MenuButtonCoordinator mMenuButtonCoordinator;
    @Mock
    private View mContainerView;
    private Activity mActivity;
    private ToolbarTablet mToolbarTablet;

    @Before
    public void setUp() {
        MockitoAnnotations.initMocks(this);
        mActivity = Robolectric.buildActivity(Activity.class).setup().get();
        mActivity.setTheme(org.chromium.chrome.tab_ui.R.style.Theme_BrowserUI_DayNight);
        mToolbarTablet = (ToolbarTablet) mActivity.getLayoutInflater().inflate(
                org.chromium.chrome.R.layout.toolbar_tablet, null);
        when(mLocationBar.getTabletCoordinator()).thenReturn(mLocationBarTablet);
        when(mLocationBarTablet.getBackground()).thenReturn(mDrawable);
        mToolbarTablet.setLocationBarCoordinator(mLocationBar);
        LocationBarLayout locationBarLayout = mToolbarTablet.findViewById(R.id.location_bar);
        locationBarLayout.setStatusCoordinatorForTesting(mStatusCoordinator);
        mToolbarTablet.setMenuButtonCoordinatorForTesting(mMenuButtonCoordinator);
    }

    @After
    public void tearDown() {
        disableGridTabSwitcher();
    }

    @Test
    public void onMeasureShortWidth_hidesToolbarButtons() {
        mToolbarTablet.measure(300, 300);

        ImageButton[] btns = mToolbarTablet.getToolbarButtons();
        for (ImageButton btn : btns) {
            assertEquals(
                    "Toolbar button visibility is not as expected", View.GONE, btn.getVisibility());
        }
    }

    @Test
    public void onMeasureLargeWidth_showsToolbarButtons() {
        mToolbarTablet.measure(700, 300);

        ImageButton[] btns = mToolbarTablet.getToolbarButtons();
        for (ImageButton btn : btns) {
            assertEquals("Toolbar button visibility is not as expected", View.VISIBLE,
                    btn.getVisibility());
        }
    }

    @Test
    public void onMeasureSmallWidthWithAnimation_hidesToolbarButtons() {
        for (ImageButton btn : mToolbarTablet.getToolbarButtons()) {
            when(mLocationBar.createHideButtonAnimatorForTablet(btn))
                    .thenReturn(ObjectAnimator.ofFloat(btn, View.ALPHA, 0.f));
        }
        when(mLocationBar.getHideButtonsWhenUnfocusedAnimatorsForTablet(anyInt()))
                .thenReturn(new ArrayList<>());

        mToolbarTablet.enableButtonVisibilityChangeAnimationForTesting();
        // Call
        mToolbarTablet.measure(300, 300);
        Shadows.shadowOf(Looper.getMainLooper()).idle();
        // Verify
        ImageButton[] btns = mToolbarTablet.getToolbarButtons();
        for (ImageButton btn : btns) {
            assertEquals(
                    "Toolbar button visibility is not as expected", View.GONE, btn.getVisibility());
        }
    }

    @Test
    public void onMeasureLargeWidthWithAnimation_showsToolbarButtons() {
        mToolbarTablet.setToolbarButtonsVisibleForTesting(false);
        mToolbarTablet.enableButtonVisibilityChangeAnimationForTesting();
        for (ImageButton btn : mToolbarTablet.getToolbarButtons()) {
            when(mLocationBar.createShowButtonAnimatorForTablet(btn))
                    .thenReturn(ObjectAnimator.ofFloat(btn, View.ALPHA, 1.f));
        }
        when(mLocationBar.getShowButtonsWhenUnfocusedAnimatorsForTablet(anyInt()))
                .thenReturn(new ArrayList<>());
        // Call
        mToolbarTablet.measure(700, 300);
        Shadows.shadowOf(Looper.getMainLooper()).idle();
        // Verify
        ImageButton[] btns = mToolbarTablet.getToolbarButtons();
        for (ImageButton btn : btns) {
            assertEquals("Toolbar button visibility is not as expected", View.VISIBLE,
                    btn.getVisibility());
        }
    }

    @EnableFeatures(ChromeFeatureList.GRID_TAB_SWITCHER_FOR_TABLETS)
    @Test
    public void testSetTabSwitcherModeOn_hidesToolbar() {
        assertEquals("Initial Toolbar visibility is not as expected", View.VISIBLE,
                mToolbarTablet.getVisibility());
        // Call
        mToolbarTablet.setTabSwitcherMode(true, false, false, mMenuButtonCoordinator);
        mToolbarTablet.getTabSwitcherModeAnimation().end();
        assertEquals(
                "Toolbar visibility is not as expected", View.GONE, mToolbarTablet.getVisibility());
        verify(mLocationBar).setUrlBarFocusable(false);
    }

    @EnableFeatures(ChromeFeatureList.GRID_TAB_SWITCHER_FOR_TABLETS)
    @Test
    public void testSetTabSwitcherModeOff_showsToolbar() {
        // Hide toolbar as initial state.
        mToolbarTablet.setVisibility(View.GONE);
        // Call
        mToolbarTablet.setTabSwitcherMode(false, false, false, mMenuButtonCoordinator);
        mToolbarTablet.getTabSwitcherModeAnimation().end();
        assertEquals("Toolbar visibility is not as expected", View.VISIBLE,
                mToolbarTablet.getVisibility());
        verify(mLocationBar).setUrlBarFocusable(true);
    }

    @EnableFeatures(ChromeFeatureList.GRID_TAB_SWITCHER_FOR_TABLETS)
    @Test
    public void testSetTabSwitcherPolishModeOff_toolbarStillVisible() {
        enableGridTabSwitcher(true);
        assertEquals("Initial Toolbar visibility is not as expected", View.VISIBLE,
                mToolbarTablet.getVisibility());
        // Call
        mToolbarTablet.setTabSwitcherMode(false, false, false, mMenuButtonCoordinator);
        assertEquals("Toolbar visibility is not as expected", View.VISIBLE,
                mToolbarTablet.getVisibility());
        verify(mLocationBar).setUrlBarFocusable(true);
    }

    @EnableFeatures(ChromeFeatureList.GRID_TAB_SWITCHER_FOR_TABLETS)
    @Test
    public void testSetTabSwitcherPolishModeOn_toolbarStillVisible() {
        enableGridTabSwitcher(true);
        assertEquals("Initial Toolbar visibility is not as expected", View.VISIBLE,
                mToolbarTablet.getVisibility());
        // Call
        mToolbarTablet.setTabSwitcherMode(true, false, false, mMenuButtonCoordinator);
        assertEquals("Toolbar visibility is not as expected", View.VISIBLE,
                mToolbarTablet.getVisibility());
        verify(mLocationBar).setUrlBarFocusable(false);
    }

    @Test
    public void testSetTabSwitcherOnGTSDisabled_hidesViews() {
        // Enable tab stack button when GTS is disabled
        mToolbarTablet.enableTabStackButton(true);
        when(mLocationBar.getContainerView()).thenReturn(mContainerView);
        assertEquals("Initial Toolbar visibility is not as expected", View.VISIBLE,
                mToolbarTablet.getVisibility());
        // Call
        mToolbarTablet.setTabSwitcherMode(true, false, false, mMenuButtonCoordinator);
        assertFalse("Button should not be enabled",
                mToolbarTablet.findViewById(R.id.back_button).isEnabled());
        assertFalse("Button should not be enabled",
                mToolbarTablet.findViewById(R.id.forward_button).isEnabled());
        assertFalse("Button should not be enabled",
                mToolbarTablet.findViewById(R.id.refresh_button).isEnabled());
        verify(mContainerView).setVisibility(View.INVISIBLE);
        verify(mMenuButtonCoordinator).setAppMenuUpdateBadgeSuppressed(true);
    }

    @Test
    public void testSetTabSwitcherOffGTSDisabled_showsViews() {
        // Enable tab stack button when GTS is disabled
        mToolbarTablet.enableTabStackButton(true);
        when(mLocationBar.getContainerView()).thenReturn(mContainerView);
        assertEquals("Initial Toolbar visibility is not as expected", View.VISIBLE,
                mToolbarTablet.getVisibility());
        // Call
        mToolbarTablet.setTabSwitcherMode(false, false, false, mMenuButtonCoordinator);
        verify(mContainerView).setVisibility(View.VISIBLE);
        verify(mMenuButtonCoordinator).setAppMenuUpdateBadgeSuppressed(false);
    }

    @Test
    public void testOnLongClick() {
        longClickAndVerifyToast(R.id.refresh_button, R.string.refresh);
        longClickAndVerifyToast(R.id.bookmark_button, R.string.menu_bookmark);
        longClickAndVerifyToast(R.id.save_offline_button, R.string.menu_download);
    }

    @Test
    public void testUpdateBackButtonVisibility() {
        ImageButton btn = mToolbarTablet.findViewById(R.id.back_button);
        mToolbarTablet.updateBackButtonVisibility(true);
        assertTrue("Button should be enabled", btn.isEnabled());
        assertTrue("Button should be focused", btn.isFocusable());
        mToolbarTablet.updateBackButtonVisibility(false);
        assertFalse("Button should not be enabled", btn.isEnabled());
        assertFalse("Button should not be focused", btn.isFocusable());
    }

    @Test
    public void testUpdateForwardButtonVisibility() {
        ImageButton btn = mToolbarTablet.findViewById(R.id.forward_button);
        mToolbarTablet.updateForwardButtonVisibility(true);
        assertTrue("Button should be enabled", btn.isEnabled());
        assertTrue("Button should be focused", btn.isFocusable());
        mToolbarTablet.updateForwardButtonVisibility(false);
        assertFalse("Button should not be enabled", btn.isEnabled());
        assertFalse("Button should not be focused", btn.isFocusable());
    }

    @Test
    public void testUpdateReloadButtonVisibility() {
        ImageButton btn = mToolbarTablet.findViewById(R.id.refresh_button);
        mToolbarTablet.updateReloadButtonVisibility(true);
        assertTrue("Button should be enabled", btn.isEnabled());
        assertEquals("Button drawable level is not as expected", 1, btn.getDrawable().getLevel());
        assertEquals("Button description is not as expected",
                mActivity.getResources().getString(R.string.accessibility_btn_stop_loading),
                btn.getContentDescription());
        mToolbarTablet.updateReloadButtonVisibility(false);
        assertEquals("Button drawable level is not as expected", 0, btn.getDrawable().getLevel());
        assertEquals("Button description is not as expected",
                mActivity.getResources().getString(R.string.accessibility_btn_refresh),
                btn.getContentDescription());
        assertTrue("Button should be enabled", btn.isEnabled());
    }

    private void longClickAndVerifyToast(int viewId, int stringId) {
        mToolbarTablet.onLongClick(mToolbarTablet.findViewById(viewId));
        assertTrue("Toast is not as expected",
                ShadowToast.showedCustomToast(
                        mActivity.getResources().getString(stringId), R.id.toast_text));
    }

    private void enableGridTabSwitcher(boolean enablePolish) {
        FeatureList.TestValues testValues = new FeatureList.TestValues();
        testValues.addFeatureFlagOverride(ChromeFeatureList.GRID_TAB_SWITCHER_FOR_TABLETS, true);
        testValues.addFieldTrialParamOverride(ChromeFeatureList.GRID_TAB_SWITCHER_FOR_TABLETS,
                "enable_launch_polish", String.valueOf(enablePolish));
        FeatureList.setTestValues(testValues);
    }

    private void disableGridTabSwitcher() {
        FeatureList.TestValues testValues = new FeatureList.TestValues();
        testValues.addFeatureFlagOverride(ChromeFeatureList.GRID_TAB_SWITCHER_FOR_TABLETS, false);
        FeatureList.setTestValues(testValues);
    }
}
