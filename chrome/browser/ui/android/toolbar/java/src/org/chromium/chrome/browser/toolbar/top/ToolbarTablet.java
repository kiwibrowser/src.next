// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.toolbar.top;

import android.animation.Animator;
import android.animation.AnimatorListenerAdapter;
import android.animation.AnimatorSet;
import android.animation.ObjectAnimator;
import android.annotation.SuppressLint;
import android.content.Context;
import android.content.res.ColorStateList;
import android.content.res.Resources;
import android.graphics.drawable.Drawable;
import android.graphics.drawable.LevelListDrawable;
import android.util.AttributeSet;
import android.view.View;
import android.view.View.OnClickListener;
import android.view.ViewStub;
import android.view.accessibility.AccessibilityEvent;
import android.widget.ImageButton;
import android.widget.ImageView;

import androidx.annotation.ColorRes;
import androidx.annotation.VisibleForTesting;
import androidx.appcompat.content.res.AppCompatResources;
import androidx.core.view.ViewCompat;

import org.chromium.base.ApiCompatibilityUtils;
import org.chromium.base.metrics.RecordUserAction;
import org.chromium.base.supplier.BooleanSupplier;
import org.chromium.base.supplier.ObservableSupplier;
import org.chromium.chrome.browser.flags.ChromeFeatureList;
import org.chromium.chrome.browser.omnibox.LocationBar;
import org.chromium.chrome.browser.omnibox.LocationBarCoordinator;
import org.chromium.chrome.browser.omnibox.NewTabPageDelegate;
import org.chromium.chrome.browser.preferences.ChromePreferenceKeys;
import org.chromium.chrome.browser.preferences.SharedPreferencesManager;
import org.chromium.chrome.browser.profiles.Profile;
import org.chromium.chrome.browser.tab.Tab;
import org.chromium.chrome.browser.theme.ThemeUtils;
import org.chromium.chrome.browser.toolbar.ButtonData;
import org.chromium.chrome.browser.toolbar.ButtonData.ButtonSpec;
import org.chromium.chrome.browser.toolbar.HomeButton;
import org.chromium.chrome.browser.toolbar.KeyboardNavigationListener;
import org.chromium.chrome.browser.toolbar.R;
import org.chromium.chrome.browser.toolbar.TabCountProvider;
import org.chromium.chrome.browser.toolbar.ToolbarDataProvider;
import org.chromium.chrome.browser.toolbar.ToolbarTabController;
import org.chromium.chrome.browser.toolbar.menu_button.MenuButtonCoordinator;
import org.chromium.chrome.browser.toolbar.top.NavigationPopup.HistoryDelegate;
import org.chromium.chrome.browser.ui.theme.BrandedColorScheme;
import org.chromium.chrome.browser.util.ChromeAccessibilityUtil;
import org.chromium.components.browser_ui.styles.ChromeColors;
import org.chromium.components.browser_ui.widget.animation.CancelAwareAnimatorListener;
import org.chromium.components.browser_ui.widget.animation.Interpolators;
import org.chromium.ui.UiUtils;
import org.chromium.ui.base.DeviceFormFactor;
import org.chromium.ui.widget.Toast;

import java.util.ArrayList;
import java.util.Collection;

/**
 * The Toolbar object for Tablet screens.
 */
@SuppressLint("Instantiatable")
public class ToolbarTablet
        extends ToolbarLayout implements OnClickListener, View.OnLongClickListener {
    private ObjectAnimator mTabSwitcherModeAnimation;

    /**
     * Downloads page for offline access.
     */
    public interface OfflineDownloader {
        /**
         * Trigger the download of a page.
         *
         * @param context Context to pull resources from.
         * @param tab Tab containing the page to download.
         */
        void downloadPage(Context context, Tab tab);
    }

    private HomeButton mHomeButton;
    private ImageButton mBackButton;
    private ImageButton mForwardButton;
    private ImageButton mReloadButton;
    private ImageButton mBookmarkButton;
    private ImageButton mSaveOfflineButton;
    private ToggleTabStackButton mAccessibilitySwitcherButton;

    private OnClickListener mBookmarkListener;

    private boolean mIsInTabSwitcherMode;

    private boolean mShowTabStack;
    private boolean mToolbarButtonsVisible;
    private ImageButton[] mToolbarButtons;
    private ImageButton mOptionalButton;
    private boolean mOptionalButtonUsesTint;
    private ImageView mToolbarShadow;

    private NavigationPopup mNavigationPopup;

    private Boolean mIsIncognito;
    private LocationBarCoordinator mLocationBar;

    private final int mStartPaddingWithButtons;
    private final int mStartPaddingWithoutButtons;
    private boolean mShouldAnimateButtonVisibilityChange;
    private AnimatorSet mButtonVisibilityAnimators;
    private HistoryDelegate mHistoryDelegate;
    private OfflineDownloader mOfflineDownloader;

    /**
     * Constructs a ToolbarTablet object.
     *
     * @param context The Context in which this View object is created.
     * @param attrs The AttributeSet that was specified with this View.
     */
    public ToolbarTablet(Context context, AttributeSet attrs) {
        super(context, attrs);
        mStartPaddingWithButtons =
                getResources().getDimensionPixelOffset(R.dimen.tablet_toolbar_start_padding);
        mStartPaddingWithoutButtons =
                getResources().getDimensionPixelOffset(R.dimen.toolbar_edge_padding);
    }

    @Override
    public void onFinishInflate() {
        super.onFinishInflate();
        mHomeButton = findViewById(R.id.home_button);
        mBackButton = findViewById(R.id.back_button);
        mForwardButton = findViewById(R.id.forward_button);
        mReloadButton = findViewById(R.id.refresh_button);
        // ImageView tinting doesn't work with LevelListDrawable, use Drawable tinting instead.
        // See https://crbug.com/891593 for details.
        // Also, using Drawable tinting doesn't work correctly with LevelListDrawable on Android L
        // and M. As a workaround, we are constructing the LevelListDrawable programmatically. See
        // https://crbug.com/958031 for details.
        final LevelListDrawable reloadIcon = new LevelListDrawable();
        final int reloadLevel = getResources().getInteger(R.integer.reload_button_level_reload);
        final int stopLevel = getResources().getInteger(R.integer.reload_button_level_stop);
        final Drawable reloadLevelDrawable = UiUtils.getTintedDrawable(
                getContext(), R.drawable.btn_toolbar_reload, R.color.default_icon_color_tint_list);
        reloadIcon.addLevel(reloadLevel, reloadLevel, reloadLevelDrawable);
        final Drawable stopLevelDrawable = UiUtils.getTintedDrawable(
                getContext(), R.drawable.btn_close, R.color.default_icon_color_tint_list);
        reloadIcon.addLevel(stopLevel, stopLevel, stopLevelDrawable);
        mReloadButton.setImageDrawable(reloadIcon);
        mShowTabStack = (ChromeAccessibilityUtil.get().isAccessibilityEnabled()
                                && isAccessibilityTabSwitcherPreferenceEnabled())
                || isGridTabSwitcherEnabled();

        mAccessibilitySwitcherButton = findViewById(R.id.tab_switcher_button);
        updateSwitcherButtonVisibility(mShowTabStack);

        mBookmarkButton = findViewById(R.id.bookmark_button);

        mSaveOfflineButton = findViewById(R.id.save_offline_button);

        // Initialize values needed for showing/hiding toolbar buttons when the activity size
        // changes.
        mShouldAnimateButtonVisibilityChange = false;
        mToolbarButtonsVisible = true;
        mToolbarButtons = new ImageButton[] {mBackButton, mForwardButton, mReloadButton};
    }

    @Override
    public void setLocationBarCoordinator(LocationBarCoordinator locationBarCoordinator) {
        mLocationBar = locationBarCoordinator;
        final int color = ChromeColors.getSurfaceColor(getContext(), R.dimen.default_elevation_2);
        mLocationBar.getTabletCoordinator().getBackground().setTint(color);
    }

    /**
     * Sets up key listeners after native initialization is complete, so that we can invoke native
     * functions.
     */
    @Override
    public void onNativeLibraryReady() {
        super.onNativeLibraryReady();
        mHomeButton.setOnClickListener(this);
        mHomeButton.setOnKeyListener(new KeyboardNavigationListener() {
            @Override
            public View getNextFocusForward() {
                if (mBackButton.isFocusable()) {
                    return findViewById(R.id.back_button);
                } else if (mForwardButton.isFocusable()) {
                    return findViewById(R.id.forward_button);
                } else {
                    return findViewById(R.id.refresh_button);
                }
            }

            @Override
            public View getNextFocusBackward() {
                return findViewById(R.id.menu_button);
            }
        });

        mBackButton.setOnClickListener(this);
        mBackButton.setLongClickable(true);
        mBackButton.setOnKeyListener(new KeyboardNavigationListener() {
            @Override
            public View getNextFocusForward() {
                if (mForwardButton.isFocusable()) {
                    return findViewById(R.id.forward_button);
                } else {
                    return findViewById(R.id.refresh_button);
                }
            }

            @Override
            public View getNextFocusBackward() {
                if (mHomeButton.getVisibility() == VISIBLE) {
                    return findViewById(R.id.home_button);
                } else {
                    return findViewById(R.id.menu_button);
                }
            }
        });

        mForwardButton.setOnClickListener(this);
        mForwardButton.setLongClickable(true);
        mForwardButton.setOnKeyListener(new KeyboardNavigationListener() {
            @Override
            public View getNextFocusForward() {
                return findViewById(R.id.refresh_button);
            }

            @Override
            public View getNextFocusBackward() {
                if (mBackButton.isFocusable()) {
                    return mBackButton;
                } else if (mHomeButton.getVisibility() == VISIBLE) {
                    return findViewById(R.id.home_button);
                } else {
                    return findViewById(R.id.menu_button);
                }
            }
        });

        mReloadButton.setOnClickListener(this);
        mReloadButton.setOnLongClickListener(this);
        mReloadButton.setOnKeyListener(new KeyboardNavigationListener() {
            @Override
            public View getNextFocusForward() {
                return findViewById(R.id.url_bar);
            }

            @Override
            public View getNextFocusBackward() {
                if (mForwardButton.isFocusable()) {
                    return mForwardButton;
                } else if (mBackButton.isFocusable()) {
                    return mBackButton;
                } else if (mHomeButton.getVisibility() == VISIBLE) {
                    return findViewById(R.id.home_button);
                } else {
                    return findViewById(R.id.menu_button);
                }
            }
        });

        mBookmarkButton.setOnClickListener(this);
        mBookmarkButton.setOnLongClickListener(this);

        getMenuButtonCoordinator().setOnKeyListener(new KeyboardNavigationListener() {
            @Override
            public View getNextFocusForward() {
                return getCurrentTabView();
            }

            @Override
            public View getNextFocusBackward() {
                return findViewById(R.id.url_bar);
            }

            @Override
            protected boolean handleEnterKeyPress() {
                return getMenuButtonCoordinator().onEnterKeyPress();
            }
        });

        mSaveOfflineButton.setOnClickListener(this);
        mSaveOfflineButton.setOnLongClickListener(this);
    }

    @Override
    public boolean showContextMenuForChild(View originalView) {
        if (mBackButton == originalView) {
            // Display backwards navigation popup.
            displayNavigationPopup(false, mBackButton);
            return true;
        } else if (mForwardButton == originalView) {
            // Display forwards navigation popup.
            displayNavigationPopup(true, mForwardButton);
            return true;
        }
        return super.showContextMenuForChild(originalView);
    }

    @Override
    protected void onAttachedToWindow() {
        super.onAttachedToWindow();

        mToolbarShadow = (ImageView) getRootView().findViewById(R.id.toolbar_hairline);
    }

    @Override
    public void onWindowFocusChanged(boolean hasWindowFocus) {
        // Ensure the the popup is not shown after resuming activity from background.
        if (hasWindowFocus && mNavigationPopup != null) {
            mNavigationPopup.dismiss();
            mNavigationPopup = null;
        }
        super.onWindowFocusChanged(hasWindowFocus);
    }

    private void displayNavigationPopup(boolean isForward, View anchorView) {
        Tab tab = getToolbarDataProvider().getTab();
        if (tab == null || tab.getWebContents() == null) return;
        mNavigationPopup = new NavigationPopup(Profile.fromWebContents(tab.getWebContents()),
                getContext(), tab.getWebContents().getNavigationController(),
                isForward ? NavigationPopup.Type.TABLET_FORWARD : NavigationPopup.Type.TABLET_BACK,
                getToolbarDataProvider()::getTab, mHistoryDelegate);
        mNavigationPopup.show(anchorView);
    }

    @Override
    public void onClick(View v) {
        if (mHomeButton == v) {
            openHomepage();
        } else if (mBackButton == v) {
            if (!back()) return;
            RecordUserAction.record("MobileToolbarBack");
        } else if (mForwardButton == v) {
            forward();
            RecordUserAction.record("MobileToolbarForward");
        } else if (mReloadButton == v) {
            stopOrReloadCurrentTab();
        } else if (mBookmarkButton == v) {
            if (mBookmarkListener != null) {
                mBookmarkListener.onClick(mBookmarkButton);
                RecordUserAction.record("MobileToolbarToggleBookmark");
            }
        } else if (mSaveOfflineButton == v) {
            mOfflineDownloader.downloadPage(getContext(), getToolbarDataProvider().getTab());
            RecordUserAction.record("MobileToolbarDownloadPage");
        }
    }

    @Override
    public boolean onLongClick(View v) {
        String description = null;
        Context context = getContext();
        Resources resources = context.getResources();

        if (v == mReloadButton) {
            description = (mReloadButton.getDrawable().getLevel()
                                  == resources.getInteger(R.integer.reload_button_level_reload))
                    ? resources.getString(R.string.refresh)
                    : resources.getString(R.string.menu_stop_refresh);
        } else if (v == mBookmarkButton) {
            description = resources.getString(R.string.menu_bookmark);
        } else if (v == mSaveOfflineButton) {
            description = resources.getString(R.string.menu_download);
        }
        return Toast.showAnchoredToast(context, v, description);
    }

    private void updateSwitcherButtonVisibility(boolean enabled) {
        mAccessibilitySwitcherButton.setVisibility(
                mShowTabStack || enabled ? View.VISIBLE : View.GONE);
    }

    /**
     * Update the visibility of the toolbar shadow.
     */
    private void updateShadowVisibility() {
        int shadowVisibility = mIsInTabSwitcherMode ? View.INVISIBLE : View.VISIBLE;

        if (mToolbarShadow != null && mToolbarShadow.getVisibility() != shadowVisibility) {
            mToolbarShadow.setVisibility(shadowVisibility);
        }
    }

    @Override
    CaptureReadinessResult isReadyForTextureCapture() {
        // Don't track tablet metrics yet for capturing, just return unknown for now.
        return CaptureReadinessResult.unknown(!urlHasFocus());
    }

    @Override
    void onTabOrModelChanged() {
        super.onTabOrModelChanged();
        final boolean incognito = isIncognito();
        if (mIsIncognito == null || mIsIncognito != incognito) {
            // TODO (amaralp): Have progress bar observe theme color and incognito changes directly.
            getProgressBar().setThemeColor(
                    ChromeColors.getDefaultThemeColor(getContext(), incognito), isIncognito());

            mIsIncognito = incognito;
        }

        updateNtp();
    }

    @Override
    public void onTintChanged(ColorStateList tint, @BrandedColorScheme int brandedColorScheme) {
        ApiCompatibilityUtils.setImageTintList(mHomeButton, tint);
        ApiCompatibilityUtils.setImageTintList(mBackButton, tint);
        ApiCompatibilityUtils.setImageTintList(mForwardButton, tint);
        ApiCompatibilityUtils.setImageTintList(mSaveOfflineButton, tint);
        ApiCompatibilityUtils.setImageTintList(mReloadButton, tint);
        mAccessibilitySwitcherButton.setBrandedColorScheme(brandedColorScheme);

        if (mOptionalButton != null && mOptionalButtonUsesTint) {
            ApiCompatibilityUtils.setImageTintList(mOptionalButton, tint);
        }
    }

    @Override
    public void onThemeColorChanged(int color, boolean shouldAnimate) {
        setBackgroundColor(color);
        final int textBoxColor = ThemeUtils.getTextBoxColorForToolbarBackgroundInNonNativePage(
                getContext(), color, isIncognito());
        mLocationBar.getTabletCoordinator().getBackground().setTint(textBoxColor);
        mLocationBar.updateVisualsForState();
        setToolbarHairlineColor(color);
    }

    /**
     * Called when the currently visible New Tab Page changes.
     */
    private void updateNtp() {
        NewTabPageDelegate ntpDelegate = getToolbarDataProvider().getNewTabPageDelegate();
        ntpDelegate.setSearchBoxScrollListener((scrollFraction) -> {
            // Fade the search box out in the first 40% of the scrolling transition.
            float alpha = Math.max(1f - scrollFraction * 2.5f, 0f);
            ntpDelegate.setSearchBoxAlpha(alpha);
            ntpDelegate.setSearchProviderLogoAlpha(alpha);
        });
    }

    @Override
    void onTabContentViewChanged() {
        super.onTabContentViewChanged();
        updateNtp();
    }

    @Override
    void updateButtonVisibility() {
        mLocationBar.updateButtonVisibility();
    }

    @Override
    void updateBackButtonVisibility(boolean canGoBack) {
        boolean enableButton = canGoBack && !mIsInTabSwitcherMode;
        mBackButton.setEnabled(enableButton);
        mBackButton.setFocusable(enableButton);
    }

    @Override
    void updateForwardButtonVisibility(boolean canGoForward) {
        boolean enableButton = canGoForward && !mIsInTabSwitcherMode;
        mForwardButton.setEnabled(enableButton);
        mForwardButton.setFocusable(enableButton);
    }

    @Override
    void updateReloadButtonVisibility(boolean isReloading) {
        if (isReloading) {
            mReloadButton.getDrawable().setLevel(
                    getResources().getInteger(R.integer.reload_button_level_stop));
            mReloadButton.setContentDescription(
                    getContext().getString(R.string.accessibility_btn_stop_loading));
        } else {
            mReloadButton.getDrawable().setLevel(
                    getResources().getInteger(R.integer.reload_button_level_reload));
            mReloadButton.setContentDescription(
                    getContext().getString(R.string.accessibility_btn_refresh));
        }
        mReloadButton.setEnabled(!mIsInTabSwitcherMode);
    }

    @Override
    void updateBookmarkButton(boolean isBookmarked, boolean editingAllowed) {
        if (isBookmarked) {
            mBookmarkButton.setImageResource(R.drawable.btn_star_filled);
            final @ColorRes int tint = isIncognito() ? R.color.default_icon_color_blue_light
                                                     : R.color.default_icon_color_accent1_tint_list;
            ApiCompatibilityUtils.setImageTintList(
                    mBookmarkButton, AppCompatResources.getColorStateList(getContext(), tint));
            mBookmarkButton.setContentDescription(getContext().getString(R.string.edit_bookmark));
        } else {
            mBookmarkButton.setImageResource(R.drawable.btn_star);
            ApiCompatibilityUtils.setImageTintList(mBookmarkButton, getTint());
            mBookmarkButton.setContentDescription(
                    getContext().getString(R.string.accessibility_menu_bookmark));
        }
        mBookmarkButton.setEnabled(editingAllowed);
    }

    @Override
    void setTabSwitcherMode(boolean inTabSwitcherMode, boolean showToolbar, boolean delayAnimation,
            MenuButtonCoordinator menuButtonCoordinator) {
        boolean isInTabSwitcherMode = mShowTabStack && inTabSwitcherMode;
        mIsInTabSwitcherMode = isInTabSwitcherMode;

        if (isTabletGridTabSwitcherPolishEnabled()) {
            int importantForAccessibility = inTabSwitcherMode
                    ? View.IMPORTANT_FOR_ACCESSIBILITY_NO_HIDE_DESCENDANTS
                    : View.IMPORTANT_FOR_ACCESSIBILITY_AUTO;

            mLocationBar.setUrlBarFocusable(!mIsInTabSwitcherMode);
            if (getImportantForAccessibility() != importantForAccessibility) {
                setImportantForAccessibility(importantForAccessibility);
                sendAccessibilityEvent(AccessibilityEvent.TYPE_WINDOW_CONTENT_CHANGED);
            }
        } else if (isGridTabSwitcherEnabled()) {
            setTabSwitcherModeWithAnimation(delayAnimation);
        } else {
            if (mIsInTabSwitcherMode) {
                mBackButton.setEnabled(false);
                mForwardButton.setEnabled(false);
                mReloadButton.setEnabled(false);
                mLocationBar.getContainerView().setVisibility(View.INVISIBLE);
                menuButtonCoordinator.setAppMenuUpdateBadgeSuppressed(true);
            } else {
                mLocationBar.getContainerView().setVisibility(View.VISIBLE);
                menuButtonCoordinator.setAppMenuUpdateBadgeSuppressed(false);
            }
        }
    }

    private void setTabSwitcherModeWithAnimation(boolean delayAnimation) {
        assert !delayAnimation : "Animation delay should be false when switcher is toggled.";

        float startAlpha = mIsInTabSwitcherMode ? 1f : 0f;
        float endAlpha = mIsInTabSwitcherMode ? 0f : 1f;

        if (mTabSwitcherModeAnimation != null) {
            mTabSwitcherModeAnimation.cancel();
        }

        if (mIsInTabSwitcherMode) {
            mLocationBar.setUrlBarFocusable(false);
            updateShadowVisibility();
        }
        setVisibility(View.VISIBLE);
        setAlpha(startAlpha);

        mTabSwitcherModeAnimation = ObjectAnimator.ofFloat(this, View.ALPHA, startAlpha, endAlpha);
        mTabSwitcherModeAnimation.setDuration(
                TopToolbarCoordinator.TAB_SWITCHER_MODE_NORMAL_ANIMATION_DURATION_MS);
        mTabSwitcherModeAnimation.setInterpolator(Interpolators.LINEAR_INTERPOLATOR);
        mTabSwitcherModeAnimation.addListener(new CancelAwareAnimatorListener() {
            @Override
            public void onEnd(Animator animator) {
                int endVisibility = mIsInTabSwitcherMode ? View.GONE : View.VISIBLE;
                setVisibility(endVisibility);
                if (!mIsInTabSwitcherMode) {
                    mLocationBar.setUrlBarFocusable(true);
                    updateShadowVisibility();
                }
            }
        });

        mTabSwitcherModeAnimation.start();
    }

    @Override
    protected void initialize(ToolbarDataProvider toolbarDataProvider,
            ToolbarTabController tabController, MenuButtonCoordinator menuButtonCoordinator,
            ObservableSupplier<Boolean> isProgressBarVisibleSupplier,
            HistoryDelegate historyDelegate, BooleanSupplier partnerHomepageEnabledSupplier,
            OfflineDownloader offlineDownloader) {
        super.initialize(toolbarDataProvider, tabController, menuButtonCoordinator,
                isProgressBarVisibleSupplier, historyDelegate, partnerHomepageEnabledSupplier,
                offlineDownloader);
        mHistoryDelegate = historyDelegate;
        mOfflineDownloader = offlineDownloader;
        menuButtonCoordinator.setVisibility(true);
    }

    @Override
    void destroy() {
        super.destroy();
        if (mButtonVisibilityAnimators != null) {
            mButtonVisibilityAnimators.removeAllListeners();
            mButtonVisibilityAnimators.cancel();
            mButtonVisibilityAnimators = null;
        }
    }

    @Override
    void setTabCountProvider(TabCountProvider tabCountProvider) {
        mAccessibilitySwitcherButton.setTabCountProvider(tabCountProvider);
    }

    @Override
    void onAccessibilityStatusChanged(boolean enabled) {
        enableTabStackButton(enabled);
    }

    @VisibleForTesting
    void enableTabStackButton(boolean enabled) {
        mShowTabStack = (enabled && isAccessibilityTabSwitcherPreferenceEnabled())
                || isGridTabSwitcherEnabled();
        updateSwitcherButtonVisibility(mShowTabStack);
    }

    @Override
    void setBookmarkClickHandler(OnClickListener listener) {
        mBookmarkListener = listener;
    }

    @Override
    void setOnTabSwitcherClickHandler(OnClickListener listener) {
        mAccessibilitySwitcherButton.setOnTabSwitcherClickHandler(listener);
    }

    @Override
    void onHomeButtonUpdate(boolean homeButtonEnabled) {
        mHomeButton.setVisibility(homeButtonEnabled ? VISIBLE : GONE);
    }

    @Override
    public LocationBar getLocationBar() {
        return mLocationBar;
    }

    @Override
    protected void onLayout(boolean changed, int left, int top, int right, int bottom) {
        // After the first layout, button visibility changes should be animated. On the first
        // layout, the button visibility shouldn't be animated because the visibility may be
        // changing solely because Chrome was launched into multi-window.
        mShouldAnimateButtonVisibilityChange = true;

        super.onLayout(changed, left, top, right, bottom);
    }

    @Override
    protected void onMeasure(int widthMeasureSpec, int heightMeasureSpec) {
        // Hide or show toolbar buttons if needed. With the introduction of multi-window on
        // Android N, the Activity can be < 600dp, in which case the toolbar buttons need to be
        // moved into the menu so that the location bar is usable. The buttons must be shown
        // in onMeasure() so that the location bar gets measured and laid out correctly.
        setToolbarButtonsVisible(MeasureSpec.getSize(widthMeasureSpec)
                >= DeviceFormFactor.getNonMultiDisplayMinimumTabletWidthPx(getContext()));

        super.onMeasure(widthMeasureSpec, heightMeasureSpec);
    }

    @Override
    void updateOptionalButton(ButtonData buttonData) {
        if (mOptionalButton == null) {
            ViewStub viewStub = findViewById(R.id.optional_button_stub);
            mOptionalButton = (ImageButton) viewStub.inflate();
        }

        ButtonSpec buttonSpec = buttonData.getButtonSpec();
        mOptionalButtonUsesTint = buttonSpec.getSupportsTinting();
        if (mOptionalButtonUsesTint) {
            ApiCompatibilityUtils.setImageTintList(mOptionalButton, getTint());
        } else {
            ApiCompatibilityUtils.setImageTintList(mOptionalButton, null);
        }

        if (buttonSpec.getIPHCommandBuilder() != null) {
            buttonSpec.getIPHCommandBuilder().setAnchorView(mOptionalButton);
        }
        mOptionalButton.setOnClickListener(buttonSpec.getOnClickListener());
        if (buttonSpec.getOnLongClickListener() == null) {
            mOptionalButton.setLongClickable(false);
        } else {
            mOptionalButton.setLongClickable(true);
            mOptionalButton.setOnLongClickListener(buttonSpec.getOnLongClickListener());
        }
        mOptionalButton.setImageDrawable(buttonSpec.getDrawable());
        mOptionalButton.setContentDescription(
                getContext().getResources().getString(buttonSpec.getContentDescriptionResId()));
        mOptionalButton.setVisibility(View.VISIBLE);
        mOptionalButton.setEnabled(buttonData.isEnabled());
    }

    @Override
    void hideOptionalButton() {
        if (mOptionalButton == null || mOptionalButton.getVisibility() == View.GONE) {
            return;
        }

        mOptionalButton.setVisibility(View.GONE);
    }

    @Override
    @VisibleForTesting
    public View getOptionalButtonViewForTesting() {
        return mOptionalButton;
    }

    @Override
    public HomeButton getHomeButton() {
        return mHomeButton;
    }

    private void setToolbarButtonsVisible(boolean visible) {
        if (mToolbarButtonsVisible == visible) return;

        mToolbarButtonsVisible = visible;

        if (mShouldAnimateButtonVisibilityChange) {
            runToolbarButtonsVisibilityAnimation(visible);
        } else {
            for (ImageButton button : mToolbarButtons) {
                button.setVisibility(visible ? View.VISIBLE : View.GONE);
            }
            mLocationBar.setShouldShowButtonsWhenUnfocusedForTablet(visible);
            setStartPaddingBasedOnButtonVisibility(visible);
        }
    }

    /**
     * Sets the toolbar start padding based on whether the buttons are visible.
     *
     * @param buttonsVisible Whether the toolbar buttons are visible.
     */
    private void setStartPaddingBasedOnButtonVisibility(boolean buttonsVisible) {
        buttonsVisible = buttonsVisible || mHomeButton.getVisibility() == View.VISIBLE;

        ViewCompat.setPaddingRelative(this,
                buttonsVisible ? mStartPaddingWithButtons : mStartPaddingWithoutButtons,
                getPaddingTop(), ViewCompat.getPaddingEnd(this), getPaddingBottom());
    }

    /**
     * @return The difference in start padding when the buttons are visible and when they are not
     * visible.
     */
    public int getStartPaddingDifferenceForButtonVisibilityAnimation() {
        // If the home button is visible then the padding doesn't change.
        return mHomeButton.getVisibility() == View.VISIBLE
                ? 0
                : mStartPaddingWithButtons - mStartPaddingWithoutButtons;
    }

    private void runToolbarButtonsVisibilityAnimation(boolean visible) {
        if (mButtonVisibilityAnimators != null) mButtonVisibilityAnimators.cancel();

        mButtonVisibilityAnimators =
                visible ? buildShowToolbarButtonsAnimation() : buildHideToolbarButtonsAnimation();
        mButtonVisibilityAnimators.start();
    }

    private AnimatorSet buildShowToolbarButtonsAnimation() {
        Collection<Animator> animators = new ArrayList<>();

        // Create animators for all of the toolbar buttons.
        for (ImageButton button : mToolbarButtons) {
            animators.add(mLocationBar.createShowButtonAnimatorForTablet(button));
        }

        // Add animators for location bar.
        animators.addAll(mLocationBar.getShowButtonsWhenUnfocusedAnimatorsForTablet(
                getStartPaddingDifferenceForButtonVisibilityAnimation()));

        AnimatorSet set = new AnimatorSet();
        set.playTogether(animators);

        set.addListener(new AnimatorListenerAdapter() {
            @Override
            public void onAnimationStart(Animator animation) {
                for (ImageButton button : mToolbarButtons) {
                    button.setVisibility(View.VISIBLE);
                }
                // Set the padding at the start of the animation so the toolbar buttons don't jump
                // when the animation ends.
                setStartPaddingBasedOnButtonVisibility(true);
            }

            @Override
            public void onAnimationEnd(Animator animation) {
                mButtonVisibilityAnimators = null;
            }
        });

        return set;
    }

    private AnimatorSet buildHideToolbarButtonsAnimation() {
        Collection<Animator> animators = new ArrayList<>();

        // Create animators for all of the toolbar buttons.
        for (ImageButton button : mToolbarButtons) {
            animators.add(mLocationBar.createHideButtonAnimatorForTablet(button));
        }

        // Add animators for location bar.
        animators.addAll(mLocationBar.getHideButtonsWhenUnfocusedAnimatorsForTablet(
                getStartPaddingDifferenceForButtonVisibilityAnimation()));

        AnimatorSet set = new AnimatorSet();
        set.playTogether(animators);

        set.addListener(new AnimatorListenerAdapter() {
            @Override
            public void onAnimationEnd(Animator animation) {
                // Only set end visibility and alpha if the animation is ending because it's
                // completely finished and not because it was canceled.
                if (mToolbarButtons[0].getAlpha() == 0.f) {
                    for (ImageButton button : mToolbarButtons) {
                        button.setVisibility(View.GONE);
                        button.setAlpha(1.f);
                    }
                    // Set the padding at the end of the animation so the toolbar buttons don't jump
                    // when the animation starts.
                    setStartPaddingBasedOnButtonVisibility(false);
                }

                mButtonVisibilityAnimators = null;
            }
        });

        return set;
    }

    private boolean isAccessibilityTabSwitcherPreferenceEnabled() {
        return SharedPreferencesManager.getInstance().readBoolean(
                ChromePreferenceKeys.ACCESSIBILITY_TAB_SWITCHER, true);
    }

    private boolean isGridTabSwitcherEnabled() {
        return ChromeFeatureList.sGridTabSwitcherForTablets.isEnabled();
    }

    private boolean isTabletGridTabSwitcherPolishEnabled() {
        return ChromeFeatureList.getFieldTrialParamByFeatureAsBoolean(
                ChromeFeatureList.GRID_TAB_SWITCHER_FOR_TABLETS, "enable_launch_polish", false);
    }

    @VisibleForTesting
    ImageButton[] getToolbarButtons() {
        return mToolbarButtons;
    }

    @VisibleForTesting
    ObjectAnimator getTabSwitcherModeAnimation() {
        return mTabSwitcherModeAnimation;
    }

    @VisibleForTesting
    void enableButtonVisibilityChangeAnimationForTesting() {
        mShouldAnimateButtonVisibilityChange = true;
    }

    @VisibleForTesting
    void setToolbarButtonsVisibleForTesting(boolean value) {
        mToolbarButtonsVisible = value;
    }
}
