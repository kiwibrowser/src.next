// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.tasks.tab_management;

import android.app.Activity;
import android.graphics.Rect;
import android.util.Size;
import android.view.LayoutInflater;
import android.view.ViewGroup;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;

import org.chromium.base.TraceEvent;
import org.chromium.base.supplier.ObservableSupplier;
import org.chromium.base.supplier.ObservableSupplierImpl;
import org.chromium.base.supplier.Supplier;
import org.chromium.chrome.browser.browser_controls.BrowserControlsStateProvider;
import org.chromium.chrome.browser.compositor.layouts.content.TabContentManager;
import org.chromium.chrome.browser.tab.Tab;
import org.chromium.chrome.browser.tabmodel.TabCreatorManager;
import org.chromium.chrome.browser.tabmodel.TabModel;
import org.chromium.chrome.browser.tabmodel.TabModelFilter;
import org.chromium.chrome.browser.tasks.tab_management.TabListEditorCoordinator.TabListEditorController;
import org.chromium.chrome.browser.ui.messages.snackbar.SnackbarManager;
import org.chromium.chrome.tab_ui.R;
import org.chromium.components.browser_ui.widget.scrim.ScrimCoordinator;
import org.chromium.ui.modelutil.PropertyModel;
import org.chromium.ui.modelutil.PropertyModelChangeProcessor;

import java.util.List;

/**
 * A coordinator for TabGridDialog component. Manages the communication with
 * {@link TabListCoordinator} as well as the life-cycle of shared component
 * objects.
 */
public class TabGridDialogCoordinator implements TabGridDialogMediator.DialogController {
    private final String mComponentName;
    private final TabListCoordinator mTabListCoordinator;
    private final TabGridDialogMediator mMediator;
    private final PropertyModel mModel;
    private final PropertyModelChangeProcessor mModelChangeProcessor;
    private final ViewGroup mRootView;
    private final ObservableSupplierImpl<Boolean> mBackPressChangedSupplier =
            new ObservableSupplierImpl<>();
    private final Activity mActivity;
    private final ObservableSupplier<TabModelFilter> mCurrentTabModelFilterSupplier;
    private final Supplier<TabModel> mRegularTabModelSupplier;
    private final BrowserControlsStateProvider mBrowserControlsStateProvider;
    private TabContentManager mTabContentManager;
    private TabListEditorCoordinator mTabListEditorCoordinator;
    private TabGridDialogView mDialogView;
    private SnackbarManager mSnackbarManager;

    TabGridDialogCoordinator(
            Activity activity,
            BrowserControlsStateProvider browserControlsStateProvider,
            @NonNull ObservableSupplier<TabModelFilter> currentTabModelFilterSupplier,
            @NonNull Supplier<TabModel> regularTabModelSupplier,
            TabContentManager tabContentManager,
            TabCreatorManager tabCreatorManager,
            ViewGroup containerView,
            TabSwitcherResetHandler resetHandler,
            TabListMediator.GridCardOnClickListenerProvider gridCardOnClickListenerProvider,
            TabGridDialogMediator.AnimationSourceViewProvider animationSourceViewProvider,
            ScrimCoordinator scrimCoordinator,
            TabGroupTitleEditor tabGroupTitleEditor,
            ViewGroup rootView) {
        try (TraceEvent e = TraceEvent.scoped("TabGridDialogCoordinator.constructor")) {
            mActivity = activity;
            mComponentName =
                    animationSourceViewProvider == null
                            ? "TabGridDialogFromStrip"
                            : "TabGridDialogInSwitcher";
            mBrowserControlsStateProvider = browserControlsStateProvider;
            mCurrentTabModelFilterSupplier = currentTabModelFilterSupplier;
            mRegularTabModelSupplier = regularTabModelSupplier;
            mTabContentManager = tabContentManager;

            mModel =
                    new PropertyModel.Builder(TabGridPanelProperties.ALL_KEYS)
                            .with(
                                    TabGridPanelProperties.BROWSER_CONTROLS_STATE_PROVIDER,
                                    mBrowserControlsStateProvider)
                            .build();
            mRootView = rootView;

            mDialogView = containerView.findViewById(R.id.dialog_parent_view);
            if (mDialogView == null) {
                LayoutInflater.from(activity)
                        .inflate(R.layout.tab_grid_dialog_layout, containerView, true);
                mDialogView = containerView.findViewById(R.id.dialog_parent_view);
                mDialogView.setupScrimCoordinator(scrimCoordinator);
            }
            mSnackbarManager =
                    new SnackbarManager(activity, mDialogView.getSnackBarContainer(), null);

            mMediator =
                    new TabGridDialogMediator(
                            activity,
                            this,
                            mModel,
                            currentTabModelFilterSupplier,
                            tabCreatorManager,
                            resetHandler,
                            this::getRecyclerViewPosition,
                            animationSourceViewProvider,
                            mSnackbarManager,
                            mComponentName);

            // TODO(crbug.com/1031349) : Remove the inline mode logic here, make the constructor to
            // take in a mode parameter instead.
            mTabListCoordinator =
                    new TabListCoordinator(
                            TabUiFeatureUtilities.shouldUseListMode(mActivity)
                                    ? TabListCoordinator.TabListMode.LIST
                                    : TabListCoordinator.TabListMode.GRID,
                            activity,
                            mBrowserControlsStateProvider,
                            currentTabModelFilterSupplier,
                            regularTabModelSupplier,
                            (tabId,
                                    thumbnailSize,
                                    callback,
                                    forceUpdate,
                                    writeBack,
                                    isSelected) -> {
                                tabContentManager.getTabThumbnailWithCallback(
                                        tabId, thumbnailSize, callback, forceUpdate, writeBack);
                            },
                            null,
                            false,
                            gridCardOnClickListenerProvider,
                            mMediator.getTabGridDialogHandler(),
                            TabProperties.UiType.CLOSABLE,
                            null,
                            null,
                            containerView,
                            false,
                            mComponentName,
                            rootView,
                            null);
            mTabListCoordinator.setOnLongPressTabItemEventListener(mMediator);
            TabListRecyclerView recyclerView = mTabListCoordinator.getContainerView();

            TabGroupUiToolbarView toolbarView =
                    (TabGroupUiToolbarView)
                            LayoutInflater.from(activity)
                                    .inflate(R.layout.bottom_tab_grid_toolbar, recyclerView, false);
            toolbarView.setupDialogToolbarLayout();
            mModelChangeProcessor =
                    PropertyModelChangeProcessor.create(
                            mModel,
                            new TabGridPanelViewBinder.ViewHolder(
                                    toolbarView, recyclerView, mDialogView),
                            TabGridPanelViewBinder::bind);
            mBackPressChangedSupplier.set(isVisible());
            mModel.addObserver((source, key) -> mBackPressChangedSupplier.set(isVisible()));

            // This is always created post-native so calling these immediately is safe.
            // TODO(crbug/1418690): Consider inlining these behaviors in their respective
            // constructors if possible.
            mMediator.initWithNative(this::getTabListEditorController, tabGroupTitleEditor);
            mTabListCoordinator.initWithNative(mRegularTabModelSupplier.get().getProfile(), null);
        }
    }

    @NonNull
    RecyclerViewPosition getRecyclerViewPosition() {
        return mTabListCoordinator.getRecyclerViewPosition();
    }

    private @Nullable TabListEditorController getTabListEditorController() {
        if (mTabListEditorCoordinator == null) {
            @TabListCoordinator.TabListMode
            int mode =
                    TabUiFeatureUtilities.shouldUseListMode(mActivity)
                            ? TabListCoordinator.TabListMode.LIST
                            : TabListCoordinator.TabListMode.GRID;
            mTabListEditorCoordinator =
                    new TabListEditorCoordinator(
                            mActivity,
                            mDialogView.findViewById(R.id.dialog_container_view),
                            mBrowserControlsStateProvider,
                            mCurrentTabModelFilterSupplier,
                            mRegularTabModelSupplier,
                            mTabContentManager,
                            mTabListCoordinator::setRecyclerViewPosition,
                            mode,
                            mRootView,
                            /* displayGroups= */ false,
                            mSnackbarManager,
                            TabProperties.UiType.SELECTABLE);
        }

        return mTabListEditorCoordinator.getController();
    }

    /** Destroy any members that needs clean up. */
    public void destroy() {
        mTabListCoordinator.onDestroy();
        mMediator.destroy();
        mModelChangeProcessor.destroy();
        if (mTabListEditorCoordinator != null) {
            mTabListEditorCoordinator.destroy();
        }
    }

    @Override
    public boolean isVisible() {
        return mMediator.isVisible();
    }

    /**
     * @param tabId The tab ID to get a rect for.
     * @return a {@link Rect} for the tab's thumbnail (may be an empty rect if the tab is not
     *     found).
     */
    @NonNull
    Rect getTabThumbnailRect(int tabId) {
        return mTabListCoordinator.getTabThumbnailRect(tabId);
    }

    @NonNull
    Size getThumbnailSize() {
        return mTabListCoordinator.getThumbnailSize();
    }

    void waitForLayoutWithTab(int tabId, Runnable r) {
        mTabListCoordinator.waitForLayoutWithTab(tabId, r);
    }

    @NonNull
    Rect getGlobalLocationOfCurrentThumbnail() {
        Rect thumbnail = mTabListCoordinator.getThumbnailLocationOfCurrentTab();
        Rect recyclerViewLocation = mTabListCoordinator.getRecyclerViewLocation();
        thumbnail.offset(recyclerViewLocation.left, recyclerViewLocation.top);
        return thumbnail;
    }

    TabGridDialogMediator.DialogController getDialogController() {
        return this;
    }

    @Override
    public void resetWithListOfTabs(@Nullable List<Tab> tabs) {
        mTabListCoordinator.resetWithListOfTabs(tabs);
        mMediator.onReset(tabs);
    }

    @Override
    public void hideDialog(boolean showAnimation) {
        mMediator.hideDialog(showAnimation);
    }

    @Override
    public void prepareDialog() {
        mTabListCoordinator.prepareTabGridView();
    }

    @Override
    public void postHiding() {
        mTabListCoordinator.postHiding();
        // TODO(crbug/1366128): This shouldn't be required if resetWithListOfTabs(null) is called.
        // Find out why this helps and fix upstream if possible.
        mTabListCoordinator.softCleanup();
    }

    @Override
    public boolean handleBackPressed() {
        if (!isVisible()) return false;
        handleBackPress();
        return true;
    }

    @Override
    public @BackPressResult int handleBackPress() {
        final boolean handled = mMediator.handleBackPress();
        return handled ? BackPressResult.SUCCESS : BackPressResult.FAILURE;
    }

    @Override
    public ObservableSupplier<Boolean> getHandleBackPressChangedSupplier() {
        return mBackPressChangedSupplier;
    }
}
