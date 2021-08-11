// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.toolbar.top;

import android.graphics.Rect;
import android.graphics.drawable.Drawable;
import android.view.View;
import android.view.View.OnClickListener;
import android.view.View.OnLongClickListener;

import androidx.annotation.Nullable;
import androidx.annotation.VisibleForTesting;

import org.chromium.base.Callback;
import org.chromium.base.supplier.BooleanSupplier;
import org.chromium.base.supplier.ObservableSupplier;
import org.chromium.base.supplier.OneShotCallback;
import org.chromium.base.supplier.OneshotSupplier;
import org.chromium.base.supplier.Supplier;
import org.chromium.chrome.browser.browser_controls.BrowserControlsStateProvider;
import org.chromium.chrome.browser.device.DeviceClassManager;
import org.chromium.chrome.browser.layouts.LayoutManager;
import org.chromium.chrome.browser.layouts.LayoutStateProvider;
import org.chromium.chrome.browser.layouts.LayoutType;
import org.chromium.chrome.browser.omnibox.LocationBar;
import org.chromium.chrome.browser.tab.Tab;
import org.chromium.chrome.browser.tabmodel.IncognitoStateProvider;
import org.chromium.chrome.browser.tabmodel.TabModelSelector;
import org.chromium.chrome.browser.theme.ThemeColorProvider;
import org.chromium.chrome.browser.theme.TopUiThemeColorProvider;
import org.chromium.chrome.browser.toolbar.ButtonData;
import org.chromium.chrome.browser.toolbar.ButtonDataProvider;
import org.chromium.chrome.browser.toolbar.R;
import org.chromium.chrome.browser.toolbar.TabCountProvider;
import org.chromium.chrome.browser.toolbar.ToolbarDataProvider;
import org.chromium.chrome.browser.toolbar.ToolbarProgressBar;
import org.chromium.chrome.browser.toolbar.ToolbarTabController;
import org.chromium.chrome.browser.toolbar.menu_button.MenuButton;
import org.chromium.chrome.browser.toolbar.menu_button.MenuButtonCoordinator;
import org.chromium.chrome.browser.toolbar.top.NavigationPopup.HistoryDelegate;
import org.chromium.chrome.browser.toolbar.top.ToolbarTablet.OfflineDownloader;
import org.chromium.chrome.browser.ui.appmenu.AppMenuButtonHelper;
import org.chromium.chrome.browser.user_education.UserEducationHelper;
import org.chromium.chrome.features.start_surface.StartSurfaceState;
import org.chromium.ui.resources.ResourceManager;

import java.util.List;

/**
 * A coordinator for the top toolbar component.
 */
public class TopToolbarCoordinator implements Toolbar {
    /**
     * Observes toolbar URL expansion progress change.
     */
    public interface UrlExpansionObserver {
        /**
         * Notified when toolbar URL expansion progress fraction changes.
         *
         * @param fraction The toolbar expansion progress. 0 indicates that the URL bar is not
         *                   expanded. 1 indicates that the URL bar is expanded to the maximum
         *                   width.
         */
        void onUrlExpansionProgressChanged(float fraction);
    }

    public static final int TAB_SWITCHER_MODE_NORMAL_ANIMATION_DURATION_MS = 200;
    public static final int TAB_SWITCHER_MODE_GTS_ANIMATION_DURATION_MS = 150;

    private final ToolbarLayout mToolbarLayout;

    private final boolean mIsGridTabSwitcherEnabled;

    /**
     * The coordinator for the tab switcher mode toolbar (phones only). This will be lazily created
     * after ToolbarLayout is inflated.
     */
    private @Nullable TabSwitcherModeTTCoordinatorPhone mTabSwitcherModeCoordinatorPhone;
    /**
     * The coordinator for the start surface mode toolbar (phones only) if the StartSurface is
     * enabled. This will be lazily created after ToolbarLayout is inflated.
     */
    private @Nullable StartSurfaceToolbarCoordinator mStartSurfaceToolbarCoordinator;

    private OptionalBrowsingModeButtonController mOptionalButtonController;

    private MenuButtonCoordinator mMenuButtonCoordinator;
    private ObservableSupplier<AppMenuButtonHelper> mAppMenuButtonHelperSupplier;
    private ObservableSupplier<TabModelSelector> mTabModelSelectorSupplier;

    private ToolbarControlContainer mControlContainer;
    private Supplier<ResourceManager> mResourceManagerSupplier;
    private TopToolbarOverlayCoordinator mOverlayCoordinator;

    /**
     * Creates a new {@link TopToolbarCoordinator}.
     * @param controlContainer The {@link ToolbarControlContainer} for the containing activity.
     * @param toolbarLayout The {@link ToolbarLayout}.
     * @param userEducationHelper Helper class for showing in-product help text bubbles.
     * @param buttonDataProviders List of classes that wish to display an optional button in the
     *         browsing mode toolbar.
     * @param layoutStateProviderSupplier Supplier of the {@link LayoutStateProvider}.
     * @param normalThemeColorProvider The {@link ThemeColorProvider} for normal mode.
     * @param overviewThemeColorProvider The {@link ThemeColorProvider} for overview mode.
     * @param tabModelSelectorSupplier Supplier of the {@link TabModelSelector}.
     * @param homepageEnabledSupplier Supplier of whether Home button is enabled.
     * @param startSurfaceAsHomepageSupplier Supplier of whether start surface should be shown as
     *         homepage.
     * @param homepageManagedByPolicySupplier Supplier of whether the homepage is managed by policy.
     * @param identityDiscStateSupplier Supplier of the state change of identity disc button.
     * @param invalidatorCallback Callback that will be invoked  when the toolbar attempts to
     *        invalidate the drawing surface.  This will give the object that registers as the host
     *        for the {@link Invalidator} a chance to defer the actual invalidate to sync drawing.
     * @param identityDiscButtonSupplier Supplier of Identity Disc button.
     * @param resourceManagerSupplier A supplier of a resource manager for native textures.
     * @param isProgressBarVisibleSupplier A supplier of whether the progress bar is visible.
     * @param isInconitoModeEnabledSupplier A supplier of the incognito mode being enabled or not.
     * @param isGridTabSwitcherEnabled Whether grid tab switcher is enabled via a feature flag.
     * @param isTabToGtsAnimationEnabled Whether Tab-to-GTS animation is enabled via a feature flag.
     * @param isStartSurfaceEnabled Whether start surface is enabled via a feature flag.
     * @param isTabGroupsAndroidContinuationEnabled Whether flag TabGroupsContinuationAndroid is
     *         enabled.
     */
    public TopToolbarCoordinator(ToolbarControlContainer controlContainer,
            ToolbarLayout toolbarLayout, ToolbarDataProvider toolbarDataProvider,
            ToolbarTabController tabController, UserEducationHelper userEducationHelper,
            List<ButtonDataProvider> buttonDataProviders,
            OneshotSupplier<LayoutStateProvider> layoutStateProviderSupplier,
            ThemeColorProvider normalThemeColorProvider,
            ThemeColorProvider overviewThemeColorProvider,
            MenuButtonCoordinator browsingModeMenuButtonCoordinator,
            MenuButtonCoordinator overviewModeMenuButtonCoordinator,
            ObservableSupplier<AppMenuButtonHelper> appMenuButtonHelperSupplier,
            ObservableSupplier<TabModelSelector> tabModelSelectorSupplier,
            ObservableSupplier<Boolean> homepageEnabledSupplier,
            ObservableSupplier<Boolean> startSurfaceAsHomepageSupplier,
            ObservableSupplier<Boolean> homepageManagedByPolicySupplier,
            ObservableSupplier<Boolean> identityDiscStateSupplier,
            Callback<Runnable> invalidatorCallback, Supplier<ButtonData> identityDiscButtonSupplier,
            Supplier<ResourceManager> resourceManagerSupplier,
            ObservableSupplier<Boolean> isProgressBarVisibleSupplier,
            BooleanSupplier isIncognitoModeEnabledSupplier, boolean isGridTabSwitcherEnabled,
            boolean isTabToGtsAnimationEnabled, boolean isStartSurfaceEnabled,
            boolean isTabGroupsAndroidContinuationEnabled, HistoryDelegate historyDelegate,
            BooleanSupplier partnerHomepageEnabledSupplier, OfflineDownloader offlineDownloader) {
        mControlContainer = controlContainer;
        mToolbarLayout = toolbarLayout;
        mMenuButtonCoordinator = browsingModeMenuButtonCoordinator;
        mOptionalButtonController = new OptionalBrowsingModeButtonController(buttonDataProviders,
                userEducationHelper, mToolbarLayout, () -> toolbarDataProvider.getTab());
        mResourceManagerSupplier = resourceManagerSupplier;

        mTabModelSelectorSupplier = tabModelSelectorSupplier;

        if (mToolbarLayout instanceof ToolbarPhone) {
            if (isStartSurfaceEnabled) {
                View.OnClickListener homeButtonOnClickListener = v -> {
                    if (tabController != null) {
                        tabController.openHomepage();
                    }
                };
                mStartSurfaceToolbarCoordinator = new StartSurfaceToolbarCoordinator(
                        controlContainer.getRootView().findViewById(R.id.tab_switcher_toolbar_stub),
                        userEducationHelper, layoutStateProviderSupplier, identityDiscStateSupplier,
                        overviewThemeColorProvider, overviewModeMenuButtonCoordinator,
                        identityDiscButtonSupplier, isGridTabSwitcherEnabled,
                        homepageEnabledSupplier, startSurfaceAsHomepageSupplier,
                        homepageManagedByPolicySupplier, homeButtonOnClickListener,
                        isTabGroupsAndroidContinuationEnabled, isIncognitoModeEnabledSupplier);
            } else {
                mTabSwitcherModeCoordinatorPhone = new TabSwitcherModeTTCoordinatorPhone(
                        controlContainer.getRootView().findViewById(R.id.tab_switcher_toolbar_stub),
                        overviewModeMenuButtonCoordinator, isGridTabSwitcherEnabled,
                        isTabToGtsAnimationEnabled, isStartSurfaceEnabled,
                        isIncognitoModeEnabledSupplier);
            }
        }
        mIsGridTabSwitcherEnabled = isGridTabSwitcherEnabled;
        controlContainer.setToolbar(this);
        mToolbarLayout.initialize(toolbarDataProvider, tabController, mMenuButtonCoordinator,
                isProgressBarVisibleSupplier, historyDelegate, partnerHomepageEnabledSupplier,
                offlineDownloader);
        mToolbarLayout.setThemeColorProvider(normalThemeColorProvider);
        mAppMenuButtonHelperSupplier = appMenuButtonHelperSupplier;
        new OneShotCallback<>(mAppMenuButtonHelperSupplier, this::setAppMenuButtonHelper);
        homepageEnabledSupplier.addObserver((show) -> mToolbarLayout.onHomeButtonUpdate(show));
        mToolbarLayout.setInvalidatorCallback(invalidatorCallback);
    }

    /**
     * @param appMenuButtonHelper The helper for managing menu button interactions.
     */
    public void setAppMenuButtonHelper(AppMenuButtonHelper appMenuButtonHelper) {
        mToolbarLayout.setAppMenuButtonHelper(appMenuButtonHelper);
    }

    /**
     * Initialize the coordinator with the components that have native initialization dependencies.
     * <p>
     * Calling this must occur after the native library have completely loaded.
     *
     * @param layoutUpdater A {@link Runnable} used to request layout update upon scene change.
     * @param tabSwitcherClickHandler The click handler for the tab switcher button.
     * @param tabSwitcherLongClickHandler The long click handler for the tab switcher button.
     * @param newTabClickHandler The click handler for the new tab button.
     * @param bookmarkClickHandler The click handler for the bookmarks button.
     * @param customTabsBackClickHandler The click handler for the custom tabs back button.
     * @param layoutManager A {@link LayoutManager} used to watch for scene changes.
     * @param tabSupplier Supplier of the activity tab.
     * @param browserControlsStateProvider {@link BrowserControlsStateProvider} to access browser
     *                                     controls offsets.
     * @param topUiThemeColorProvider {@link ThemeColorProvider} for top UI.
     */
    public void initializeWithNative(Runnable layoutUpdater,
            OnClickListener tabSwitcherClickHandler,
            OnLongClickListener tabSwitcherLongClickHandler, OnClickListener newTabClickHandler,
            OnClickListener bookmarkClickHandler, OnClickListener customTabsBackClickHandler,
            LayoutManager layoutManager, ObservableSupplier<Tab> tabSupplier,
            BrowserControlsStateProvider browserControlsStateProvider,
            TopUiThemeColorProvider topUiThemeColorProvider) {
        assert mTabModelSelectorSupplier.get() != null;
        if (mTabSwitcherModeCoordinatorPhone != null) {
            mTabSwitcherModeCoordinatorPhone.setOnTabSwitcherClickHandler(tabSwitcherClickHandler);
            mTabSwitcherModeCoordinatorPhone.setOnNewTabClickHandler(newTabClickHandler);
            mTabSwitcherModeCoordinatorPhone.setTabModelSelector(mTabModelSelectorSupplier.get());
        } else if (mStartSurfaceToolbarCoordinator != null) {
            mStartSurfaceToolbarCoordinator.setOnNewTabClickHandler(newTabClickHandler);
            mStartSurfaceToolbarCoordinator.setTabModelSelector(mTabModelSelectorSupplier.get());
            mStartSurfaceToolbarCoordinator.setTabSwitcherListener(tabSwitcherClickHandler);
            mStartSurfaceToolbarCoordinator.setOnTabSwitcherLongClickHandler(
                    tabSwitcherLongClickHandler);
            mStartSurfaceToolbarCoordinator.onNativeLibraryReady();
        }

        mToolbarLayout.setTabModelSelector(mTabModelSelectorSupplier.get());
        getLocationBar().updateVisualsForState();
        mToolbarLayout.setOnTabSwitcherClickHandler(tabSwitcherClickHandler);
        mToolbarLayout.setOnTabSwitcherLongClickHandler(tabSwitcherLongClickHandler);
        mToolbarLayout.setBookmarkClickHandler(bookmarkClickHandler);
        mToolbarLayout.setCustomTabCloseClickHandler(customTabsBackClickHandler);
        mToolbarLayout.setLayoutUpdater(layoutUpdater);

        mToolbarLayout.onNativeLibraryReady();

        // If fullscreen is disabled, don't bother creating this overlay; only the android view will
        // ever be shown.
        if (DeviceClassManager.enableFullscreen()) {
            mOverlayCoordinator = new TopToolbarOverlayCoordinator(mToolbarLayout.getContext(),
                    layoutManager, mControlContainer::getProgressBarDrawingInfo, tabSupplier,
                    browserControlsStateProvider, mResourceManagerSupplier, topUiThemeColorProvider,
                    LayoutType.BROWSING | LayoutType.SIMPLE_ANIMATION | LayoutType.TAB_SWITCHER,
                    false);
            layoutManager.addSceneOverlay(mOverlayCoordinator);
            mToolbarLayout.setOverlayCoordinator(mOverlayCoordinator);
        }
    }

    /**
     * @param urlExpansionObserver The observer that observes URL expansion progress change.
     */
    public void addUrlExpansionObserver(UrlExpansionObserver urlExpansionObserver) {
        mToolbarLayout.addUrlExpansionObserver(urlExpansionObserver);
    }

    /**
     * @param urlExpansionObserver The observer that observes URL expansion progress change.
     */
    public void removeUrlExpansionObserver(UrlExpansionObserver urlExpansionObserver) {
        mToolbarLayout.removeUrlExpansionObserver(urlExpansionObserver);
    }

    /**
     * @see View#addOnAttachStateChangeListener(View.OnAttachStateChangeListener)
     */
    public void addOnAttachStateChangeListener(View.OnAttachStateChangeListener listener) {
        mToolbarLayout.addOnAttachStateChangeListener(listener);
    }

    /**
     * Cleans up any code as necessary.
     */
    public void destroy() {
        if (mOverlayCoordinator != null) {
            mOverlayCoordinator.destroy();
            mOverlayCoordinator = null;
        }
        mToolbarLayout.destroy();
        if (mTabSwitcherModeCoordinatorPhone != null) {
            mTabSwitcherModeCoordinatorPhone.destroy();
        } else if (mStartSurfaceToolbarCoordinator != null) {
            mStartSurfaceToolbarCoordinator.destroy();
        }

        if (mOptionalButtonController != null) {
            mOptionalButtonController.destroy();
            mOptionalButtonController = null;
        }

        if (mAppMenuButtonHelperSupplier != null) {
            mAppMenuButtonHelperSupplier = null;
        }
        if (mTabModelSelectorSupplier != null) {
            mTabModelSelectorSupplier = null;
        }
        if (mControlContainer != null) {
            mControlContainer = null;
        }
    }

    @Override
    public void disableMenuButton() {
        mMenuButtonCoordinator.disableMenuButton();
        mToolbarLayout.onMenuButtonDisabled();
    }

    /**
     * @return The wrapper for the browsing mode toolbar's menu button.
     */
    public MenuButton getMenuButtonWrapper() {
        return mMenuButtonCoordinator.getMenuButton();
    }

    @Nullable
    @Override
    public ToolbarProgressBar getProgressBar() {
        return mToolbarLayout.getProgressBar();
    }

    @Override
    public int getPrimaryColor() {
        return mToolbarLayout.getToolbarDataProvider().getPrimaryColor();
    }

    @Override
    public void getPositionRelativeToContainer(View containerView, int[] position) {
        mToolbarLayout.getPositionRelativeToContainer(containerView, position);
    }

    /**
     * Sets the {@link Invalidator} that will be called when the toolbar attempts to invalidate the
     * drawing surface.  This will give the object that registers as the host for the
     * {@link Invalidator} a chance to defer the actual invalidate to sync drawing.
     * @param invalidator An {@link Invalidator} instance.
     */
    public void setInvalidatorCallback(Callback<Runnable> callback) {
        mToolbarLayout.setInvalidatorCallback(callback);
    }

    /**
     * Gives inheriting classes the chance to respond to
     * {@link FindToolbar} state changes.
     * @param showing Whether or not the {@code FindToolbar} will be showing.
     */
    public void handleFindLocationBarStateChange(boolean showing) {
        mToolbarLayout.handleFindLocationBarStateChange(showing);
    }

    /**
     * Sets whether the urlbar should be hidden on first page load.
     */
    public void setUrlBarHidden(boolean hidden) {
        mToolbarLayout.setUrlBarHidden(hidden);
    }

    /**
     * @return The name of the publisher of the content if it can be reliably extracted, or null
     *         otherwise.
     */
    public String getContentPublisher() {
        return mToolbarLayout.getContentPublisher();
    }

    /**
     * Tells the Toolbar to update what buttons it is currently displaying.
     */
    public void updateButtonVisibility() {
        mToolbarLayout.updateButtonVisibility();
        mOptionalButtonController.updateButtonVisibility();
    }

    /**
     * Gives inheriting classes the chance to update the visibility of the
     * back button.
     * @param canGoBack Whether or not the current tab has any history to go back to.
     */
    public void updateBackButtonVisibility(boolean canGoBack) {
        mToolbarLayout.updateBackButtonVisibility(canGoBack);
    }

    /**
     * Gives inheriting classes the chance to update the visibility of the
     * forward button.
     * @param canGoForward Whether or not the current tab has any history to go forward to.
     */
    public void updateForwardButtonVisibility(boolean canGoForward) {
        mToolbarLayout.updateForwardButtonVisibility(canGoForward);
    }

    @Override
    public void updateReloadButtonVisibility(boolean isReloading) {
        mToolbarLayout.updateReloadButtonVisibility(isReloading);
    }

    /**
     * Gives inheriting classes the chance to update the visual status of the
     * bookmark button.
     * @param isBookmarked Whether or not the current tab is already bookmarked.
     * @param editingAllowed Whether or not bookmarks can be modified (added, edited, or removed).
     */
    public void updateBookmarkButton(boolean isBookmarked, boolean editingAllowed) {
        mToolbarLayout.updateBookmarkButton(isBookmarked, editingAllowed);
    }

    /**
     * Gives inheriting classes the chance to respond to accessibility state changes.
     * @param enabled Whether or not accessibility is enabled.
     */
    public void onAccessibilityStatusChanged(boolean enabled) {
        mToolbarLayout.onAccessibilityStatusChanged(enabled);
        if (mTabSwitcherModeCoordinatorPhone != null) {
            mTabSwitcherModeCoordinatorPhone.onAccessibilityStatusChanged(enabled);
        } else if (mStartSurfaceToolbarCoordinator != null) {
            mStartSurfaceToolbarCoordinator.onAccessibilityStatusChanged(enabled);
        }
    }

    /**
     * Gives inheriting classes the chance to do the necessary UI operations after Chrome is
     * restored to a previously saved state.
     */
    public void onStateRestored() {
        mToolbarLayout.onStateRestored();
    }

    /**
     * Triggered when the current tab or model has changed.
     * <p>
     * As there are cases where you can select a model with no tabs (i.e. having incognito
     * tabs but no normal tabs will still allow you to select the normal model), this should
     * not guarantee that the model's current tab is non-null.
     */
    public void onTabOrModelChanged() {
        mToolbarLayout.onTabOrModelChanged();
    }

    /**
     * For extending classes to override and carry out the changes related with the primary color
     * for the current tab changing.
     */
    public void onPrimaryColorChanged(boolean shouldAnimate) {
        mToolbarLayout.onPrimaryColorChanged(shouldAnimate);
    }

    /**
     * Sets whether a title should be shown within the Toolbar.
     * @param showTitle Whether a title should be shown.
     */
    public void setShowTitle(boolean showTitle) {
        getLocationBar().setShowTitle(showTitle);
    }

    /**
     * Sets the icon drawable that the close button in the toolbar (if any) should show, or hides
     * it if {@code drawable} is {@code null}.
     */
    public void setCloseButtonImageResource(@Nullable Drawable drawable) {
        mToolbarLayout.setCloseButtonImageResource(drawable);
    }

    /**
     * Adds a custom action button to the toolbar layout, if it is supported.
     * @param drawable The icon for the button.
     * @param description The content description for the button.
     * @param listener The {@link View.OnClickListener} to use for clicks to the button.
     */
    public void addCustomActionButton(
            Drawable drawable, String description, View.OnClickListener listener) {
        mToolbarLayout.addCustomActionButton(drawable, description, listener);
    }

    /**
     * Updates the visual appearance of a custom action button in the toolbar layout,
     * if it is supported.
     * @param index The index of the button.
     * @param drawable The icon for the button.
     * @param description The content description for the button.
     */
    public void updateCustomActionButton(int index, Drawable drawable, String description) {
        mToolbarLayout.updateCustomActionButton(index, drawable, description);
    }

    @Override
    public int getTabStripHeight() {
        return mToolbarLayout.getTabStripHeight();
    }

    /**
     * Triggered when the content view for the specified tab has changed.
     */
    public void onTabContentViewChanged() {
        mToolbarLayout.onTabContentViewChanged();
    }

    @Override
    public boolean isReadyForTextureCapture() {
        return mToolbarLayout.isReadyForTextureCapture();
    }

    @Override
    public boolean setForceTextureCapture(boolean forceTextureCapture) {
        return mToolbarLayout.setForceTextureCapture(forceTextureCapture);
    }

    /**
     * @param attached Whether or not the web content is attached to the view heirarchy.
     */
    public void setContentAttached(boolean attached) {
        mToolbarLayout.setContentAttached(attached);
    }

    /**
     * Gives inheriting classes the chance to show or hide the TabSwitcher mode of this toolbar.
     * @param inTabSwitcherMode Whether or not TabSwitcher mode should be shown or hidden.
     * @param showToolbar    Whether or not to show the normal toolbar while animating.
     * @param delayAnimation Whether or not to delay the animation until after the transition has
     *                       finished (which can be detected by a call to
     *                       {@link #onTabSwitcherTransitionFinished()}).
     */
    public void setTabSwitcherMode(
            boolean inTabSwitcherMode, boolean showToolbar, boolean delayAnimation) {
        mToolbarLayout.setTabSwitcherMode(
                inTabSwitcherMode, showToolbar, delayAnimation, mMenuButtonCoordinator);
        if (mTabSwitcherModeCoordinatorPhone != null) {
            mTabSwitcherModeCoordinatorPhone.setTabSwitcherMode(inTabSwitcherMode);
        } else if (mStartSurfaceToolbarCoordinator != null) {
            mStartSurfaceToolbarCoordinator.setStartSurfaceMode(inTabSwitcherMode);
        }
    }

    /**
     * Gives inheriting classes the chance to update their state when the TabSwitcher transition has
     * finished.
     */
    public void onTabSwitcherTransitionFinished() {
        mToolbarLayout.onTabSwitcherTransitionFinished();
    }

    /**
     * Gives inheriting classes the chance to observe tab count changes.
     * @param tabCountProvider The {@link TabCountProvider} subclasses can observe.
     */
    public void setTabCountProvider(TabCountProvider tabCountProvider) {
        mToolbarLayout.setTabCountProvider(tabCountProvider);
        if (mTabSwitcherModeCoordinatorPhone != null) {
            mTabSwitcherModeCoordinatorPhone.setTabCountProvider(tabCountProvider);
        }
        if (mStartSurfaceToolbarCoordinator != null) {
            mStartSurfaceToolbarCoordinator.setTabCountProvider(tabCountProvider);
        }
    }

    /**
     * @param provider The provider used to determine incognito state.
     */
    public void setIncognitoStateProvider(IncognitoStateProvider provider) {
        if (mTabSwitcherModeCoordinatorPhone != null) {
            mTabSwitcherModeCoordinatorPhone.setIncognitoStateProvider(provider);
        } else if (mStartSurfaceToolbarCoordinator != null) {
            mStartSurfaceToolbarCoordinator.setIncognitoStateProvider(provider);
        }
    }

    /**
     * Gives inheriting classes the chance to update themselves based on default search engine
     * changes.
     */
    public void onDefaultSearchEngineChanged() {
        mToolbarLayout.onDefaultSearchEngineChanged();
    }

    @Override
    public void getLocationBarContentRect(Rect outRect) {
        mToolbarLayout.getLocationBarContentRect(outRect);
    }

    @Override
    public void setTextureCaptureMode(boolean textureMode) {
        mToolbarLayout.setTextureCaptureMode(textureMode);
    }

    @Override
    public boolean shouldIgnoreSwipeGesture() {
        return mToolbarLayout.shouldIgnoreSwipeGesture();
    }

    /**
     * Triggered when the URL input field has gained or lost focus.
     * @param hasFocus Whether the URL field has gained focus.
     */
    public void onUrlFocusChange(boolean hasFocus) {
        mToolbarLayout.onUrlFocusChange(hasFocus);
    }

    /**
     * Returns the elapsed realtime in ms of the time at which first draw for the toolbar occurred.
     */
    public long getFirstDrawTime() {
        return mToolbarLayout.getFirstDrawTime();
    }

    /**
     * Notified when a navigation to a different page has occurred.
     */
    public void onNavigatedToDifferentPage() {
        mToolbarLayout.onNavigatedToDifferentPage();
    }

    /**
     * Force to hide toolbar shadow.
     * @param forceHideShadow Whether toolbar shadow should be hidden.
     *
     * TODO(crbug.com/1202994): change to token-based access
     */
    public void setForceHideShadow(boolean forceHideShadow) {
        mToolbarLayout.setForceHideShadow(forceHideShadow);
    }

    /**
     * Finish any toolbar animations.
     */
    public void finishAnimations() {
        mToolbarLayout.finishAnimations();
    }

    /**
     * @return {@link LocationBar} object this {@link ToolbarLayout} contains.
     */
    public LocationBar getLocationBar() {
        return mToolbarLayout.getLocationBar();
    }

    /**
     * Update the start surface toolbar state.
     * @param newState New Start Surface State.
     * @param requestToShow Whether or not request showing the start surface toolbar.
     * @param toolbarHeight The height of start surface toolbar.
     */
    public void updateStartSurfaceToolbarState(
            @StartSurfaceState int newState, boolean requestToShow, int toolbarHeight) {
        if (mStartSurfaceToolbarCoordinator == null
                || mToolbarLayout.getToolbarDataProvider() == null) {
            return;
        }
        mStartSurfaceToolbarCoordinator.onStartSurfaceStateChanged(newState, requestToShow);
        updateToolbarLayoutVisibility(toolbarHeight);
    }

    /**
     * Triggered when the offset of start surface header view is changed.
     * @param verticalOffset The start surface header view's offset.
     * @param toolbarHeight The height of start surface toolbar.
     */
    public void onStartSurfaceHeaderOffsetChanged(int verticalOffset, int toolbarHeight) {
        if (mStartSurfaceToolbarCoordinator != null) {
            mStartSurfaceToolbarCoordinator.onStartSurfaceHeaderOffsetChanged(verticalOffset);
            updateToolbarLayoutVisibility(toolbarHeight);
        }
    }

    private void updateToolbarLayoutVisibility(int toolbarHeight) {
        assert mStartSurfaceToolbarCoordinator != null;
        mToolbarLayout.onStartSurfaceStateChanged(
                mStartSurfaceToolbarCoordinator.shouldShowRealSearchBox(toolbarHeight),
                mStartSurfaceToolbarCoordinator.isOnHomepage());
    }

    @Override
    public int getHeight() {
        return mToolbarLayout.getHeight();
    }

    /**
     * Sets the highlight on the new tab button shown during overview mode.
     * @param highlight If the new tab button should be highlighted.
     */
    public void setNewTabButtonHighlight(boolean highlight) {
        if (mTabSwitcherModeCoordinatorPhone != null) {
            mTabSwitcherModeCoordinatorPhone.setNewTabButtonHighlight(highlight);
        } else if (mStartSurfaceToolbarCoordinator != null) {
            mStartSurfaceToolbarCoordinator.setNewTabButtonHighlight(highlight);
        }
    }

    /** Returns the {@link OptionalBrowsingModeButtonController}. */
    @VisibleForTesting
    public OptionalBrowsingModeButtonController getOptionalButtonControllerForTesting() {
        return mOptionalButtonController;
    }

    /** Returns the {@link ToolbarLayout} that constitutes the toolbar. */
    @VisibleForTesting
    public ToolbarLayout getToolbarLayoutForTesting() {
        return mToolbarLayout;
    }

    /** Returns the {@link StartSurfaceToolbarCoordinator}. */
    @VisibleForTesting
    public StartSurfaceToolbarCoordinator getStartSurfaceToolbarForTesting() {
        return mStartSurfaceToolbarCoordinator;
    }
}
