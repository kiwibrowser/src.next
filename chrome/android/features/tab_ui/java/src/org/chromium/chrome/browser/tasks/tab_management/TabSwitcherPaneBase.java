// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.tasks.tab_management;

import static org.chromium.chrome.browser.hub.HubLayoutConstants.SHRINK_EXPAND_DURATION_MS;
import static org.chromium.chrome.browser.tasks.tab_management.TabSwitcherConstants.DESTROY_COORDINATOR_DELAY_MS;
import static org.chromium.chrome.browser.tasks.tab_management.TabSwitcherConstants.HARD_CLEANUP_DELAY_MS;
import static org.chromium.chrome.browser.tasks.tab_management.TabSwitcherConstants.SOFT_CLEANUP_DELAY_MS;

import android.content.Context;
import android.graphics.Rect;
import android.os.Handler;
import android.view.ViewGroup;
import android.widget.FrameLayout;

import androidx.annotation.ColorInt;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.annotation.VisibleForTesting;

import org.chromium.base.Log;
import org.chromium.base.metrics.RecordUserAction;
import org.chromium.base.supplier.ObservableSupplier;
import org.chromium.base.supplier.ObservableSupplierImpl;
import org.chromium.base.supplier.Supplier;
import org.chromium.base.supplier.SyncOneshotSupplier;
import org.chromium.base.supplier.SyncOneshotSupplierImpl;
import org.chromium.base.supplier.TransitiveObservableSupplier;
import org.chromium.chrome.browser.hub.DisplayButtonData;
import org.chromium.chrome.browser.hub.FadeHubLayoutAnimationFactory;
import org.chromium.chrome.browser.hub.FullButtonData;
import org.chromium.chrome.browser.hub.HubContainerView;
import org.chromium.chrome.browser.hub.HubLayoutAnimatorProvider;
import org.chromium.chrome.browser.hub.HubLayoutConstants;
import org.chromium.chrome.browser.hub.LoadHint;
import org.chromium.chrome.browser.hub.Pane;
import org.chromium.chrome.browser.hub.PaneHubController;
import org.chromium.chrome.browser.hub.ShrinkExpandAnimationData;
import org.chromium.chrome.browser.hub.ShrinkExpandHubLayoutAnimationFactory;
import org.chromium.chrome.browser.tab.Tab;
import org.chromium.chrome.browser.tasks.pseudotab.PseudoTab;
import org.chromium.chrome.browser.tasks.tab_management.TabListCoordinator.TabListMode;
import org.chromium.chrome.tab_ui.R;
import org.chromium.components.browser_ui.styles.ChromeColors;
import org.chromium.components.browser_ui.widget.MenuOrKeyboardActionController.MenuOrKeyboardActionHandler;
import org.chromium.ui.base.DeviceFormFactor;

import java.util.List;

/**
 * An abstract {@link Pane} representing a tab switcher for shared logic between the normal and
 * incognito modes.
 */
public abstract class TabSwitcherPaneBase implements Pane, TabSwitcherResetHandler {
    private static final String TAG = "TabSwitcherPaneBase";

    protected final ObservableSupplierImpl<DisplayButtonData> mReferenceButtonDataSupplier =
            new ObservableSupplierImpl<>();
    protected final ObservableSupplierImpl<FullButtonData> mNewTabButtonDataSupplier =
            new ObservableSupplierImpl<>();
    private final ObservableSupplierImpl<Boolean> mIsVisibleSupplier =
            new ObservableSupplierImpl<>();
    private final ObservableSupplierImpl<Boolean> mIsAnimatingSupplier =
            new ObservableSupplierImpl<>();
    private final Handler mHandler = new Handler();
    private final Runnable mSoftCleanupRunnable = this::softCleanupInternal;
    private final Runnable mHardCleanupRunnable = this::hardCleanupInternal;
    private final Runnable mDestroyCoordinatorRunnable = this::destroyTabSwitcherPaneCoordinator;
    private final TabSwitcherCustomViewManager mTabSwitcherCustomViewManager =
            new TabSwitcherCustomViewManager();

    private final MenuOrKeyboardActionHandler mMenuOrKeyboardActionHandler =
            new MenuOrKeyboardActionHandler() {
                @Override
                public boolean handleMenuOrKeyboardAction(int id, boolean fromMenu) {
                    if (id == R.id.menu_select_tabs) {
                        @Nullable
                        TabSwitcherPaneCoordinator coordinator =
                                mTabSwitcherPaneCoordinatorSupplier.get();
                        if (coordinator == null) return false;

                        coordinator.showTabListEditor();
                        RecordUserAction.record("MobileMenuSelectTabs");
                        return true;
                    }
                    return false;
                }
            };
    private final ObservableSupplierImpl<TabSwitcherPaneCoordinator>
            mTabSwitcherPaneCoordinatorSupplier = new ObservableSupplierImpl<>();
    private final TransitiveObservableSupplier<TabSwitcherPaneCoordinator, Boolean>
            mHandleBackPressChangedSupplier =
                    new TransitiveObservableSupplier<>(
                            mTabSwitcherPaneCoordinatorSupplier,
                            pc -> pc.getHandleBackPressChangedSupplier());
    private final FrameLayout mRootView;
    private final TabSwitcherPaneCoordinatorFactory mFactory;
    private final boolean mIsIncognito;

    private boolean mNativeInitialized;
    private @Nullable PaneHubController mPaneHubController;

    /**
     * @param context The activity context.
     * @param factory The factory used to construct {@link TabSwitcherPaneCoordinator}s.
     * @param isIncognito Whether the pane is incognito.
     */
    TabSwitcherPaneBase(
            @NonNull Context context,
            @NonNull TabSwitcherPaneCoordinatorFactory factory,
            boolean isIncognito) {
        mFactory = factory;
        mIsIncognito = isIncognito;

        mRootView = new FrameLayout(context);
        mIsVisibleSupplier.set(false);
        mIsAnimatingSupplier.set(false);
    }

    @Override
    public void destroy() {
        destroyTabSwitcherPaneCoordinator();
    }

    @Override
    public @NonNull ViewGroup getRootView() {
        return mRootView;
    }

    @Override
    public @Nullable MenuOrKeyboardActionHandler getMenuOrKeyboardActionHandler() {
        return mMenuOrKeyboardActionHandler;
    }

    @Override
    public void setPaneHubController(@Nullable PaneHubController paneHubController) {
        mPaneHubController = paneHubController;
    }

    @Override
    public void notifyLoadHint(@LoadHint int loadHint) {
        boolean isVisible = loadHint == LoadHint.HOT;
        mIsVisibleSupplier.set(isVisible);

        removeDelayedCallbacks();

        if (isVisible) {
            createTabSwitcherPaneCoordinator();
            showAllTabs();
            setInitialScrollIndexOffset();
            // TODO(crbug/1502201): This should only happen when the Pane becomes user visible which
            // might only happen after the Hub animation finishes. Figure out how to handle that
            // since the load hint for hot will come before the animation is started. Panes likely
            // need to know an animation is going to play and when it is finished (possibly using
            // the isAnimatingSupplier?).
            requestAccessibilityFocusOnCurrentTab();
        }

        if (loadHint == LoadHint.WARM) {
            if (mTabSwitcherPaneCoordinatorSupplier.hasValue()) {
                mHandler.postDelayed(mSoftCleanupRunnable, SOFT_CLEANUP_DELAY_MS);
            } else {
                createTabSwitcherPaneCoordinator();
            }
        }

        if (loadHint == LoadHint.COLD) {
            if (mTabSwitcherPaneCoordinatorSupplier.hasValue()) {
                mHandler.postDelayed(mSoftCleanupRunnable, SOFT_CLEANUP_DELAY_MS);
                mHandler.postDelayed(mHardCleanupRunnable, HARD_CLEANUP_DELAY_MS);
                mHandler.postDelayed(mDestroyCoordinatorRunnable, DESTROY_COORDINATOR_DELAY_MS);
            }
        }
    }

    @Override
    public @NonNull ObservableSupplier<FullButtonData> getActionButtonDataSupplier() {
        return mNewTabButtonDataSupplier;
    }

    @Override
    public @NonNull ObservableSupplier<DisplayButtonData> getReferenceButtonDataSupplier() {
        return mReferenceButtonDataSupplier;
    }

    @Override
    public @NonNull HubLayoutAnimatorProvider createShowHubLayoutAnimatorProvider(
            @NonNull HubContainerView hubContainerView) {
        assert !DeviceFormFactor.isNonMultiDisplayContextOnTablet(hubContainerView.getContext());
        int tabId = getCurrentTabId();
        if (getTabListMode() == TabListMode.LIST || tabId == Tab.INVALID_TAB_ID) {
            return FadeHubLayoutAnimationFactory.createFadeInAnimatorProvider(
                    hubContainerView, HubLayoutConstants.FADE_DURATION_MS);
        }

        @ColorInt int backgroundColor = getAnimationBackgroundColor();
        SyncOneshotSupplier<ShrinkExpandAnimationData> animationDataSupplier =
                requestAnimationData(hubContainerView, /* isShrink= */ true, tabId);
        return ShrinkExpandHubLayoutAnimationFactory.createShrinkTabAnimatorProvider(
                hubContainerView,
                animationDataSupplier,
                backgroundColor,
                SHRINK_EXPAND_DURATION_MS);
    }

    @Override
    public @NonNull HubLayoutAnimatorProvider createHideHubLayoutAnimatorProvider(
            @NonNull HubContainerView hubContainerView) {
        assert !DeviceFormFactor.isNonMultiDisplayContextOnTablet(hubContainerView.getContext());
        int tabId = getCurrentTabId();
        if (getTabListMode() == TabListMode.LIST || tabId == Tab.INVALID_TAB_ID) {
            return FadeHubLayoutAnimationFactory.createFadeOutAnimatorProvider(
                    hubContainerView, HubLayoutConstants.FADE_DURATION_MS);
        }

        @ColorInt int backgroundColor = getAnimationBackgroundColor();
        SyncOneshotSupplier<ShrinkExpandAnimationData> animationDataSupplier =
                requestAnimationData(hubContainerView, /* isShrink= */ false, tabId);
        return ShrinkExpandHubLayoutAnimationFactory.createExpandTabAnimatorProvider(
                hubContainerView,
                animationDataSupplier,
                backgroundColor,
                SHRINK_EXPAND_DURATION_MS);
    }

    private @ColorInt int getAnimationBackgroundColor() {
        if (mIsIncognito) {
            return ChromeColors.getPrimaryBackgroundColor(mRootView.getContext(), mIsIncognito);
        } else {
            // TODO(crbug/1507839): Consider not getting the color from home surface.
            return ChromeColors.getSurfaceColor(
                    mRootView.getContext(), R.dimen.home_surface_background_color_elevation);
        }
    }

    private SyncOneshotSupplier<ShrinkExpandAnimationData> requestAnimationData(
            @NonNull HubContainerView hubContainerView, boolean isShrink, int tabId) {
        SyncOneshotSupplierImpl<ShrinkExpandAnimationData> animationDataSupplier =
                new SyncOneshotSupplierImpl<>();
        @Nullable TabSwitcherPaneCoordinator coordinator = getTabSwitcherPaneCoordinator();
        assert coordinator != null;
        Runnable provideAnimationData =
                () -> {
                    Rect hubRect = new Rect();
                    hubContainerView.getGlobalVisibleRect(hubRect);
                    Rect initialRect;
                    Rect finalRect;
                    int leftOffset = 0;
                    if (isShrink) {
                        initialRect = coordinator.getRecyclerViewRect();
                        finalRect = coordinator.getTabThumbnailRect(tabId);
                        leftOffset = initialRect.left;
                    } else {
                        initialRect = coordinator.getTabThumbnailRect(tabId);
                        finalRect = coordinator.getRecyclerViewRect();
                        leftOffset = finalRect.left;
                    }
                    boolean useFallbackAnimation = false;
                    if (initialRect.isEmpty() || finalRect.isEmpty()) {
                        Log.d(TAG, "Geometry not ready using fallback animation.");
                        useFallbackAnimation = true;
                    }
                    // Ignore left offset and just ensure the width is correct. See crbug/1502437.
                    initialRect.offset(-leftOffset, -hubRect.top);
                    finalRect.offset(-leftOffset, -hubRect.top);
                    animationDataSupplier.set(
                            new ShrinkExpandAnimationData(
                                    initialRect,
                                    finalRect,
                                    coordinator.getThumbnailSize(),
                                    useFallbackAnimation));
                };
        coordinator.waitForLayoutWithTab(tabId, provideAnimationData);
        return animationDataSupplier;
    }

    @Override
    public @BackPressResult int handleBackPress() {
        @Nullable
        TabSwitcherPaneCoordinator coordinator = mTabSwitcherPaneCoordinatorSupplier.get();
        if (coordinator == null) return BackPressResult.FAILURE;
        return coordinator.handleBackPress();
    }

    @Override
    public ObservableSupplier<Boolean> getHandleBackPressChangedSupplier() {
        return mHandleBackPressChangedSupplier;
    }

    @Override
    public boolean resetWithTabs(@Nullable List<PseudoTab> tabs, boolean quickMode) {
        assert false : "Not reached.";
        return true;
    }

    @Override
    public void softCleanup() {
        assert false : "Not reached.";
    }

    @Override
    public void hardCleanup() {
        assert false : "Not reached.";
    }

    public void initWithNative() {
        if (mNativeInitialized) return;

        mNativeInitialized = true;
        @Nullable
        TabSwitcherPaneCoordinator coordinator = mTabSwitcherPaneCoordinatorSupplier.get();
        if (coordinator != null) {
            coordinator.initWithNative();
        }
    }

    /** Returns a {@link Supplier} that provides dialog visibility information. */
    public @Nullable Supplier<Boolean> getTabGridDialogVisibilitySupplier() {
        @Nullable
        TabSwitcherPaneCoordinator coordinator = mTabSwitcherPaneCoordinatorSupplier.get();
        if (coordinator == null) return null;
        return coordinator.getTabGridDialogVisibilitySupplier();
    }

    /** Returns a {@link TabSwitcherCustomViewManager} for supplying custom views. */
    public @Nullable TabSwitcherCustomViewManager getTabSwitcherCustomViewManager() {
        return mTabSwitcherCustomViewManager;
    }

    /** Returns the number of elements in the tab switcher's tab list model. */
    public int getTabSwitcherTabListModelSize() {
        @Nullable
        TabSwitcherPaneCoordinator coordinator = mTabSwitcherPaneCoordinatorSupplier.get();
        if (coordinator == null) return 0;
        return coordinator.getTabSwitcherTabListModelSize();
    }

    /** Set the tab switcher's RecyclerViewPosition. */
    public void setTabSwitcherRecyclerViewPosition(RecyclerViewPosition position) {
        @Nullable
        TabSwitcherPaneCoordinator coordinator = mTabSwitcherPaneCoordinatorSupplier.get();
        if (coordinator == null) return;
        coordinator.setTabSwitcherRecyclerViewPosition(position);
    }

    /**
     * Request to show all the tabs in the pane. Subclasses should override this method to invoke
     * {@link TabSwitcherResetHandler#resetWithTabList} with their available tabs.
     */
    protected abstract void showAllTabs();

    /** Returns the current selected tab ID. */
    protected abstract int getCurrentTabId();

    /** Requests accessibility focus on the currently selected tab in the tab switcher. */
    protected void requestAccessibilityFocusOnCurrentTab() {
        TabSwitcherPaneCoordinator coordinator = mTabSwitcherPaneCoordinatorSupplier.get();
        if (coordinator == null) return;
        coordinator.requestAccessibilityFocusOnCurrentTab();
    }

    /** Returns the {@link TabListMode} of the {@link TabListCoordinator} that the pane hosts. */
    protected @TabListMode int getTabListMode() {
        return mFactory.getTabListMode();
    }

    /** Returns whether the pane is visible onscreen. Note this is not the same as being focused. */
    protected boolean isVisible() {
        return mIsVisibleSupplier.get();
    }

    /** Returns whether the pane is focused. */
    protected boolean isFocused() {
        return mPaneHubController != null;
    }

    /** Returns the PaneHubController if one exists or null otherwise. */
    protected @Nullable PaneHubController getPaneHubController() {
        return mPaneHubController;
    }

    /** Returns the current {@link TabSwitcherPaneCoordinator} or null if one doesn't exist. */
    @VisibleForTesting(otherwise = VisibleForTesting.PROTECTED)
    @Nullable
    TabSwitcherPaneCoordinator getTabSwitcherPaneCoordinator() {
        return mTabSwitcherPaneCoordinatorSupplier.get();
    }

    /** Creates a {@link TabSwitcherCoordinator}. */
    @VisibleForTesting(otherwise = VisibleForTesting.PROTECTED)
    void createTabSwitcherPaneCoordinator() {
        if (mTabSwitcherPaneCoordinatorSupplier.hasValue()) return;

        @NonNull
        TabSwitcherPaneCoordinator coordinator =
                mFactory.create(
                        mRootView,
                        /* resetHandler= */ this,
                        mIsVisibleSupplier,
                        mIsAnimatingSupplier,
                        this::onTabClick,
                        mIsIncognito);
        mTabSwitcherPaneCoordinatorSupplier.set(coordinator);
        mTabSwitcherCustomViewManager.setDelegate(
                coordinator.getTabSwitcherCustomViewManagerDelegate());
        if (mNativeInitialized) {
            coordinator.initWithNative();
        }
    }

    /**
     * Destroys the current {@link TabSwitcherCoordinator}. It is safe to call this even if a
     * coordinator does not exist.
     */
    @VisibleForTesting(otherwise = VisibleForTesting.PROTECTED)
    void destroyTabSwitcherPaneCoordinator() {
        if (!mTabSwitcherPaneCoordinatorSupplier.hasValue()) return;

        @NonNull TabSwitcherPaneCoordinator coordinator = mTabSwitcherPaneCoordinatorSupplier.get();
        mTabSwitcherPaneCoordinatorSupplier.set(null);
        mRootView.removeAllViews();
        mTabSwitcherCustomViewManager.setDelegate(null);
        coordinator.destroy();
    }

    private void onTabClick(int tabId) {
        if (mPaneHubController == null) return;

        // TODO(crbug/1516949): Consider using INVALID_TAB_ID if already selected to prevent a
        // repeat selection. For now this is required to ensure the tab gets marked as shown when
        // exiting the Hub. See if this can be updated/changed.
        mPaneHubController.selectTabAndHideHub(tabId);
    }

    private void setInitialScrollIndexOffset() {
        TabSwitcherPaneCoordinator coordinator = mTabSwitcherPaneCoordinatorSupplier.get();
        if (coordinator == null) return;
        coordinator.setInitialScrollIndexOffset();
    }

    private void softCleanupInternal() {
        TabSwitcherPaneCoordinator coordinator = mTabSwitcherPaneCoordinatorSupplier.get();
        if (coordinator == null) return;
        coordinator.softCleanup();
    }

    private void hardCleanupInternal() {
        TabSwitcherPaneCoordinator coordinator = mTabSwitcherPaneCoordinatorSupplier.get();
        if (coordinator == null) return;
        coordinator.hardCleanup();
    }

    private void removeDelayedCallbacks() {
        mHandler.removeCallbacks(mSoftCleanupRunnable);
        mHandler.removeCallbacks(mHardCleanupRunnable);
        mHandler.removeCallbacks(mDestroyCoordinatorRunnable);
    }
}
