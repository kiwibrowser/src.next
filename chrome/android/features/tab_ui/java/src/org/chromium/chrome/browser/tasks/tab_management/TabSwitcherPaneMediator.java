// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.tasks.tab_management;

import static org.chromium.chrome.browser.tasks.tab_management.TabListContainerProperties.BLOCK_TOUCH_INPUT;
import static org.chromium.chrome.browser.tasks.tab_management.TabListContainerProperties.FOCUS_TAB_INDEX_FOR_ACCESSIBILITY;
import static org.chromium.chrome.browser.tasks.tab_management.TabListContainerProperties.INITIAL_SCROLL_INDEX;
import static org.chromium.chrome.browser.tasks.tab_management.TabListContainerProperties.IS_INCOGNITO;
import static org.chromium.chrome.browser.tasks.tab_management.TabListContainerProperties.MODE;

import android.view.View;
import android.view.ViewGroup;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;

import org.chromium.base.Callback;
import org.chromium.base.ValueChangedCallback;
import org.chromium.base.metrics.RecordUserAction;
import org.chromium.base.supplier.LazyOneshotSupplier;
import org.chromium.base.supplier.ObservableSupplier;
import org.chromium.base.supplier.ObservableSupplierImpl;
import org.chromium.base.supplier.TransitiveObservableSupplier;
import org.chromium.chrome.browser.back_press.BackPressManager;
import org.chromium.chrome.browser.tab.Tab;
import org.chromium.chrome.browser.tabmodel.TabModel;
import org.chromium.chrome.browser.tabmodel.TabModelFilter;
import org.chromium.chrome.browser.tabmodel.TabModelObserver;
import org.chromium.chrome.browser.tabmodel.TabModelUtils;
import org.chromium.chrome.browser.tasks.tab_management.PriceMessageService.PriceWelcomeMessageReviewActionProvider;
import org.chromium.chrome.browser.tasks.tab_management.TabGridDialogMediator.DialogController;
import org.chromium.chrome.browser.tasks.tab_management.TabListCoordinator.TabListMode;
import org.chromium.chrome.browser.tasks.tab_management.TabListEditorCoordinator.TabListEditorController;
import org.chromium.chrome.browser.tasks.tab_management.TabListMediator.GridCardOnClickListenerProvider;
import org.chromium.chrome.browser.tasks.tab_management.TabListMediator.TabActionListener;
import org.chromium.chrome.features.start_surface.StartSurfaceUserData;
import org.chromium.components.browser_ui.widget.gesture.BackPressHandler;
import org.chromium.ui.modelutil.PropertyModel;

import java.util.List;

/** Mediator for {@link TabSwitcherPaneCoordinator}. */
public class TabSwitcherPaneMediator
        implements GridCardOnClickListenerProvider,
                PriceWelcomeMessageReviewActionProvider,
                TabSwitcherCustomViewManager.Delegate,
                BackPressHandler {
    private final ObservableSupplierImpl<Boolean> mBackPressChangedSupplier =
            new ObservableSupplierImpl<>();
    private final ObservableSupplierImpl<Boolean> mIsDialogVisibleSupplier =
            new ObservableSupplierImpl<>();
    private final TabActionListener mTabGridDialogOpener = this::onTabGroupClicked;
    private final ValueChangedCallback<TabModelFilter> mOnTabModelFilterChanged =
            new ValueChangedCallback<>(this::onTabModelFilterChanged);
    // TODO(crbug/1505772): this might not be required if we leverage the back press handling at the
    // Hub level. Need to check with UX/PM.
    private final TabModelObserver mTabModelObserver =
            new TabModelObserver() {
                @Override
                public void tabClosureUndone(Tab tab) {
                    notifyBackPressStateChangedInternal();
                }

                @Override
                public void tabPendingClosure(Tab tab) {
                    notifyBackPressStateChangedInternal();
                }

                @Override
                public void onFinishingTabClosure(Tab tab) {
                    // If tab is closed by the site itself rather than user's input,
                    // tabPendingClosure & tabClosureCommitted won't be called.
                    notifyBackPressStateChangedInternal();
                }

                @Override
                public void tabRemoved(Tab tab) {
                    notifyBackPressStateChangedInternal();
                }

                @Override
                public void multipleTabsPendingClosure(List<Tab> tabs, boolean isAllTabs) {
                    notifyBackPressStateChangedInternal();
                }

                @Override
                public void restoreCompleted() {
                    if (Boolean.TRUE.equals(mIsVisibleSupplier.get())) {
                        mResetHandler.resetWithTabList(mTabModelFilterSupplier.get(), false);
                        setInitialScrollIndexOffset();
                    }
                }
            };
    private final Callback<Boolean> mOnAnimatingChanged = this::onAnimatingChanged;
    private final Callback<Boolean> mOnVisibilityChanged = this::onVisibilityChanged;
    private final Callback<Boolean> mNotifyBackPressedCallback =
            ignored -> {
                notifyBackPressStateChangedInternal();
            };

    private final TabSwitcherResetHandler mResetHandler;
    private final ObservableSupplier<TabModelFilter> mTabModelFilterSupplier;
    private final LazyOneshotSupplier<DialogController> mTabGridDialogControllerSupplier;
    private final PropertyModel mContainerViewModel;
    private final ViewGroup mContainerView;
    private final ObservableSupplier<Boolean> mIsVisibleSupplier;
    private final ObservableSupplier<Boolean> mIsAnimatingSupplier;
    private final Runnable mOnTabSwitcherShown;
    private final Callback<Integer> mOnTabClickCallback;

    private @Nullable ObservableSupplier<TabListEditorController> mTabListEditorControllerSupplier;
    private @Nullable TransitiveObservableSupplier<TabListEditorController, Boolean>
            mCurrentTabListEditorControllerBackSupplier;
    private @Nullable View mCustomView;
    private @Nullable Runnable mCustomViewBackPressRunnable;

    /**
     * @param resetHandler The reset handler for updating the {@link TabListCoordinator}.
     * @param tabModelFilterSupplier The supplier of the {@link TabModelFilter}. This should usually
     *     only ever be set once.
     * @param tabGridDialogControllerSupplier The supplier of the {@link DialogController}.
     * @param containerViewModel The {@link PropertyModel} for the {@link TabListRecyclerView}.
     * @param containerView The view that hosts the {@link TabListRecyclerView}.
     * @param onTabSwitcherShown Runnable executed once the view becomes visible.
     * @param isVisibleSupplier Supplier for visibility of the pane.
     * @param isAnimatingSupplier Supplier for when the pane is animating in or out of visibility.
     * @param onTabClickCallback Callback to invoke when a tab is clicked.
     */
    public TabSwitcherPaneMediator(
            @NonNull TabSwitcherResetHandler resetHandler,
            @NonNull ObservableSupplier<TabModelFilter> tabModelFilterSupplier,
            @NonNull LazyOneshotSupplier<DialogController> tabGridDialogControllerSupplier,
            @NonNull PropertyModel containerViewModel,
            @NonNull ViewGroup containerView,
            @NonNull Runnable onTabSwitcherShown,
            @NonNull ObservableSupplier<Boolean> isVisibleSupplier,
            @NonNull ObservableSupplier<Boolean> isAnimatingSupplier,
            @NonNull Callback<Integer> onTabClickCallback) {
        mResetHandler = resetHandler;
        mOnTabClickCallback = onTabClickCallback;
        mTabModelFilterSupplier = tabModelFilterSupplier;
        mTabModelFilterSupplier.addObserver(mOnTabModelFilterChanged);

        mTabGridDialogControllerSupplier = tabGridDialogControllerSupplier;
        tabGridDialogControllerSupplier.onAvailable(
                tabGridDialogController -> {
                    tabGridDialogController
                            .getHandleBackPressChangedSupplier()
                            .addObserver(mNotifyBackPressedCallback);
                });

        mContainerViewModel = containerViewModel;
        // TODO(crbug/1505772): Remove the containerView dependency. It is only used for adding and
        // removing custom views for incognito reauth and it breaks the intended encapsulation of
        // views not being accessible to the mediator.
        mContainerView = containerView;
        mOnTabSwitcherShown = onTabSwitcherShown;

        mIsVisibleSupplier = isVisibleSupplier;
        isVisibleSupplier.addObserver(mOnVisibilityChanged);
        mIsAnimatingSupplier = isAnimatingSupplier;
        isAnimatingSupplier.addObserver(mOnAnimatingChanged);

        notifyBackPressStateChangedInternal();
    }

    /** Destroys the mediator unregistering all its observers. */
    public void destroy() {
        mTabModelFilterSupplier.removeObserver(mOnTabModelFilterChanged);
        removeTabModelObserver(mTabModelFilterSupplier.get());

        mIsVisibleSupplier.removeObserver(mOnVisibilityChanged);
        mIsAnimatingSupplier.removeObserver(mOnAnimatingChanged);
        DialogController controller = getTabGridDialogController();
        if (controller != null) {
            controller
                    .getHandleBackPressChangedSupplier()
                    .removeObserver(mNotifyBackPressedCallback);
        }
        if (mCurrentTabListEditorControllerBackSupplier != null) {
            mCurrentTabListEditorControllerBackSupplier.removeObserver(mNotifyBackPressedCallback);
        }
    }

    /** Returns a supplier that indicates whether any dialogs are visible. */
    public ObservableSupplier<Boolean> getIsDialogVisibleSupplier() {
        return mIsDialogVisibleSupplier;
    }

    /** Requests accessibility focus on the currently selected tab. */
    public void requestAccessibilityFocusOnCurrentTab() {
        mContainerViewModel.set(
                FOCUS_TAB_INDEX_FOR_ACCESSIBILITY, mTabModelFilterSupplier.get().index());
    }

    /** Scrolls to the currently selected tab. */
    public void setInitialScrollIndexOffset() {
        scrollToTab(mTabModelFilterSupplier.get().index());
    }

    @Override
    public @BackPressResult int handleBackPress() {
        TabListEditorController editorController = getTabListEditorController();
        if (editorController != null && editorController.handleBackPressed()) {
            return BackPressResult.SUCCESS;
        }
        if (mCustomViewBackPressRunnable != null) {
            mCustomViewBackPressRunnable.run();
            return BackPressResult.SUCCESS;
        }

        if (Boolean.TRUE.equals(mIsAnimatingSupplier.get())) {
            // crbug.com/1420410: intentionally do nothing to wait for tab-to-GTS transition to be
            // finished. Note this has to be before following if-branch since during transition, the
            // container is still invisible. On tablet, the translation transition replaces the
            // tab-to-GTS (expand/shrink) animation, which does not suffer from the same issue.
            return BackPressResult.SUCCESS;
        }

        if (Boolean.FALSE.equals(mIsVisibleSupplier.get())) {
            assert !BackPressManager.isEnabled()
                    : "Invisible container backpress should be handled.";
            return BackPressResult.FAILURE;
        }

        DialogController controller = getTabGridDialogController();
        if (controller != null && controller.handleBackPressed()) {
            return BackPressResult.SUCCESS;
        }

        // The signal to select a tab and exit is handled at the pane level.

        return BackPressResult.FAILURE;
    }

    @Override
    public ObservableSupplier<Boolean> getHandleBackPressChangedSupplier() {
        return mBackPressChangedSupplier;
    }

    @Override
    public @Nullable TabActionListener openTabGridDialog(Tab tab) {
        if (!ableToOpenDialog(tab)) return null;
        return mTabGridDialogOpener;
    }

    @Override
    public void onTabSelecting(int tabId, boolean fromActionButton) {
        if (fromActionButton && getMode() == TabListMode.GRID) {
            TabModel model = mTabModelFilterSupplier.get().getTabModel();
            Tab newlySelectedTab = TabModelUtils.getTabById(model, tabId);
            StartSurfaceUserData.setKeepTab(newlySelectedTab, true);
        }
        mOnTabClickCallback.onResult(tabId);
    }

    @Override
    public void scrollToTab(int tabIndex) {
        // TODO(crbug/1505772): This doesn't account for non-tab message cards, it probably should.
        mContainerViewModel.set(INITIAL_SCROLL_INDEX, tabIndex);
    }

    @Override
    public void addCustomView(
            @NonNull View customView, @Nullable Runnable backPressRunnable, boolean clearTabList) {
        assert mCustomView == null : "Only one custom view may be showing at a time.";

        hideDialogs();

        if (clearTabList) {
            mResetHandler.resetWithTabList(null, false);
        }

        mContainerView.addView(customView);
        mCustomView = customView;
        mCustomViewBackPressRunnable = backPressRunnable;
        notifyBackPressStateChangedInternal();
    }

    @Override
    public void removeCustomView(@NonNull View customView) {
        assert mCustomView != null : "No custom view client has added a view.";
        mContainerView.removeView(customView);
        mCustomView = null;
        mCustomViewBackPressRunnable = null;
        notifyBackPressStateChangedInternal();
    }

    void setTabListEditorControllerSupplier(
            @NonNull ObservableSupplier<TabListEditorController> tabListEditorControllerSupplier) {
        assert mTabListEditorControllerSupplier == null
                : "setTabListEditorControllerSupplier should be called only once.";
        mTabListEditorControllerSupplier = tabListEditorControllerSupplier;
        mCurrentTabListEditorControllerBackSupplier =
                new TransitiveObservableSupplier<>(
                        tabListEditorControllerSupplier,
                        tabListEditorController -> {
                            return tabListEditorController.getHandleBackPressChangedSupplier();
                        });
        mCurrentTabListEditorControllerBackSupplier.addObserver(mNotifyBackPressedCallback);
    }

    void hideDialogs() {
        DialogController controller = getTabGridDialogController();
        if (controller != null) {
            controller.hideDialog(false);
        }
        TabListEditorController editorController = getTabListEditorController();
        if (editorController != null && editorController.isVisible()) {
            editorController.hide();
        }
    }

    private void recordUserSwitchedTab(Tab tab, int lastId) {
        // TODO(crbug/1505772): Implement this.
    }

    private boolean ableToOpenDialog(Tab tab) {
        return mTabModelFilterSupplier.get().isIncognito() == tab.isIncognito()
                && getRelatedTabs(tab.getId()).size() != 1;
    }

    private List<Tab> getRelatedTabs(int tabId) {
        return mTabModelFilterSupplier.get().getRelatedTabList(tabId);
    }

    private void onTabGroupClicked(int tabId) {
        List<Tab> relatedTabs = getRelatedTabs(tabId);
        if (relatedTabs.size() == 0) {
            relatedTabs = null;
        }
        mTabGridDialogControllerSupplier.get().resetWithListOfTabs(relatedTabs);
        RecordUserAction.record("TabGridDialog.ExpandedFromSwitcher");
    }

    private void notifyBackPressStateChangedInternal() {
        if (Boolean.FALSE.equals(mIsVisibleSupplier.get())) return;

        mIsDialogVisibleSupplier.set(isDialogVisible());
        mBackPressChangedSupplier.set(shouldInterceptBackPress());
    }

    private boolean isDialogVisible() {
        TabListEditorController editorController = getTabListEditorController();
        if (editorController != null && editorController.isVisible()) {
            return true;
        }
        DialogController dialogController = getTabGridDialogController();
        if (dialogController != null && dialogController.isVisible()) {
            return true;
        }
        return false;
    }

    private boolean shouldInterceptBackPress() {
        if (isDialogVisible()) return true;
        if (mCustomViewBackPressRunnable != null) return true;

        // TODO(crbug/1505772) consider restricting to grid + phone only.
        if (Boolean.TRUE.equals(mIsAnimatingSupplier.get())) return true;

        // TODO(crbug/1505772): Figure out whether we care about tab selection/start surface here.
        return false;
    }

    private @TabListMode int getMode() {
        return mContainerViewModel.get(MODE);
    }

    private TabListEditorController getTabListEditorController() {
        return mTabListEditorControllerSupplier == null
                ? null
                : mTabListEditorControllerSupplier.get();
    }

    private @Nullable DialogController getTabGridDialogController() {
        var supplier = mTabGridDialogControllerSupplier;
        return !supplier.hasValue() ? null : supplier.get();
    }

    private void removeTabModelObserver(@Nullable TabModelFilter filter) {
        if (filter == null) return;

        filter.removeObserver(mTabModelObserver);
    }

    private void onTabModelFilterChanged(
            @Nullable TabModelFilter newFilter, @Nullable TabModelFilter oldFilter) {
        removeTabModelObserver(oldFilter);

        if (newFilter != null) {
            mContainerViewModel.set(IS_INCOGNITO, newFilter.isIncognito());
            newFilter.addObserver(mTabModelObserver);
        }
    }

    private void onAnimatingChanged(boolean animating) {
        mContainerViewModel.set(BLOCK_TOUCH_INPUT, animating);
        notifyBackPressStateChangedInternal();
    }

    private void onVisibilityChanged(boolean visible) {
        if (visible) mOnTabSwitcherShown.run();
        notifyBackPressStateChangedInternal();
    }
}
