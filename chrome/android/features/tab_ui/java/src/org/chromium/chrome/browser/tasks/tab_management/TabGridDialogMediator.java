// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.tasks.tab_management;

import android.app.Activity;
import android.content.Context;
import android.content.res.ColorStateList;
import android.text.Editable;
import android.text.TextWatcher;
import android.view.View;

import androidx.annotation.Nullable;
import androidx.appcompat.content.res.AppCompatResources;

import org.chromium.base.Callback;
import org.chromium.base.ResettersForTesting;
import org.chromium.base.ValueChangedCallback;
import org.chromium.base.metrics.RecordUserAction;
import org.chromium.base.supplier.ObservableSupplier;
import org.chromium.base.supplier.Supplier;
import org.chromium.base.task.PostTask;
import org.chromium.base.task.TaskTraits;
import org.chromium.chrome.browser.tab.Tab;
import org.chromium.chrome.browser.tab.TabCreationState;
import org.chromium.chrome.browser.tab.TabLaunchType;
import org.chromium.chrome.browser.tab.TabSelectionType;
import org.chromium.chrome.browser.tabmodel.TabCreatorManager;
import org.chromium.chrome.browser.tabmodel.TabModel;
import org.chromium.chrome.browser.tabmodel.TabModelFilter;
import org.chromium.chrome.browser.tabmodel.TabModelObserver;
import org.chromium.chrome.browser.tabmodel.TabModelUtils;
import org.chromium.chrome.browser.tasks.tab_groups.TabGroupModelFilter;
import org.chromium.chrome.browser.tasks.tab_management.TabListEditorAction.ButtonType;
import org.chromium.chrome.browser.tasks.tab_management.TabListEditorAction.IconPosition;
import org.chromium.chrome.browser.tasks.tab_management.TabListEditorAction.ShowMode;
import org.chromium.chrome.browser.tasks.tab_management.TabListEditorCoordinator.TabListEditorController;
import org.chromium.chrome.browser.tasks.tab_management.TabUiMetricsHelper.TabListEditorOpenMetricGroups;
import org.chromium.chrome.browser.ui.messages.snackbar.Snackbar;
import org.chromium.chrome.browser.ui.messages.snackbar.SnackbarManager;
import org.chromium.chrome.tab_ui.R;
import org.chromium.components.browser_ui.widget.gesture.BackPressHandler;
import org.chromium.components.embedder_support.util.UrlConstants;
import org.chromium.content_public.browser.LoadUrlParams;
import org.chromium.ui.KeyboardVisibilityDelegate;
import org.chromium.ui.base.DeviceFormFactor;
import org.chromium.ui.modelutil.PropertyModel;
import org.chromium.ui.text.EmptyTextWatcher;

import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

/**
 * A mediator for the TabGridDialog component, responsible for communicating
 * with the components' coordinator as well as managing the business logic
 * for dialog show/hide.
 */
public class TabGridDialogMediator
        implements SnackbarManager.SnackbarController,
                TabGridDialogView.VisibilityListener,
                TabGridItemTouchHelperCallback.OnLongPressTabItemEventListener {
    /** Defines an interface for a {@link TabGridDialogMediator} to control dialog. */
    interface DialogController extends BackPressHandler {
        /**
         * Handles a reset event originated from {@link TabGridDialogMediator} and {@link
         * TabSwitcherMediator}.
         *
         * @param tabs List of Tabs to reset.
         */
        void resetWithListOfTabs(@Nullable List<Tab> tabs);

        /**
         * Hide the TabGridDialog
         * @param showAnimation Whether to show an animation when hiding the dialog.
         */
        void hideDialog(boolean showAnimation);

        /** Prepare the TabGridDialog before show. */
        void prepareDialog();

        /** Cleanup post hiding dialog. */
        void postHiding();

        /**
         * @return Whether or not the TabGridDialog consumed the event.
         */
        boolean handleBackPressed();

        /**
         * @return Whether the TabGridDialog is visible.
         */
        boolean isVisible();
    }

    /**
     * Defines an interface for a {@link TabGridDialogMediator} to get the source {@link View}
     * in order to prepare show/hide animation.
     */
    interface AnimationSourceViewProvider {
        /**
         * Provide {@link View} of the source item to setup the animation.
         *
         * @param tabId The id of the tab whose position is requested.
         * @return The source {@link View} used to setup the animation.
         */
        View getAnimationSourceViewForTab(int tabId);
    }

    private final Context mContext;
    private final PropertyModel mModel;
    private final ObservableSupplier<TabModelFilter> mCurrentTabModelFilterSupplier;
    private final ValueChangedCallback<TabModelFilter> mOnTabModelFilterChanged =
            new ValueChangedCallback<>(this::onTabModelFilterChanged);
    private final TabModelObserver mTabModelObserver;
    private final TabCreatorManager mTabCreatorManager;
    private final DialogController mDialogController;
    private final TabSwitcherResetHandler mTabSwitcherResetHandler;
    private final Supplier<RecyclerViewPosition> mRecyclerViewPositionSupplier;
    private final AnimationSourceViewProvider mAnimationSourceViewProvider;
    private final DialogHandler mTabGridDialogHandler;
    private final Runnable mScrimClickRunnable;
    private final SnackbarManager mSnackbarManager;
    private final String mComponentName;

    private TabGroupTitleEditor mTabGroupTitleEditor;
    private Supplier<TabListEditorController> mTabListEditorControllerSupplier;
    private boolean mTabListEditorSetup;
    private KeyboardVisibilityDelegate.KeyboardVisibilityListener mKeyboardVisibilityListener;
    private int mCurrentTabId = Tab.INVALID_TAB_ID;
    private boolean mIsUpdatingTitle;
    private String mCurrentGroupModifiedTitle;
    private Callback<Integer> mToolbarMenuCallback;
    private Activity mActivity;

    TabGridDialogMediator(
            Activity activity,
            DialogController dialogController,
            PropertyModel model,
            ObservableSupplier<TabModelFilter> currentTabModelFilterSupplier,
            TabCreatorManager tabCreatorManager,
            TabSwitcherResetHandler tabSwitcherResetHandler,
            Supplier<RecyclerViewPosition> recyclerViewPositionSupplier,
            AnimationSourceViewProvider animationSourceViewProvider,
            SnackbarManager snackbarManager,
            String componentName) {
        mContext = activity;
        mModel = model;
        mCurrentTabModelFilterSupplier = currentTabModelFilterSupplier;
        mTabCreatorManager = tabCreatorManager;
        mDialogController = dialogController;
        mTabSwitcherResetHandler = tabSwitcherResetHandler;
        mRecyclerViewPositionSupplier = recyclerViewPositionSupplier;
        mAnimationSourceViewProvider = animationSourceViewProvider;
        mTabGridDialogHandler = new DialogHandler();
        mSnackbarManager = snackbarManager;
        mComponentName = componentName;
        mActivity = activity;

        // Register for tab model.
        mTabModelObserver =
                new TabModelObserver() {
                    @Override
                    public void didAddTab(
                            Tab tab,
                            @TabLaunchType int type,
                            @TabCreationState int creationState,
                            boolean markedForSelection) {
                        TabModelFilter filter = mCurrentTabModelFilterSupplier.get();
                        if (filter == null || !filter.isTabModelRestored()) {
                            return;
                        }
                        hideDialog(false);
                    }

                    @Override
                    public void tabClosureUndone(Tab tab) {
                        updateDialog();
                        updateGridTabSwitcher();
                        dismissSingleTabSnackbar(tab.getId());
                    }

                    @Override
                    public void didSelectTab(Tab tab, int type, int lastId) {
                        if (type == TabSelectionType.FROM_USER) {
                            // Cancel the zooming into tab grid card animation.
                            hideDialog(false);
                        } else if (getRelatedTabs(mCurrentTabId).contains(tab)) {
                            mCurrentTabId = tab.getId();
                        }
                    }

                    @Override
                    public void willCloseTab(Tab tab, boolean animate, boolean didCloseAlone) {
                        List<Tab> relatedTabs = getRelatedTabs(tab.getId());
                        // If the group is empty, update the animation and hide the dialog.
                        if (relatedTabs.size() == 0) {
                            hideDialog(false);
                            return;
                        }
                        // If current tab is closed and tab group is not empty, hand over ID of the
                        // next tab in the group to mCurrentTabId.
                        if (tab.getId() == mCurrentTabId) {
                            mCurrentTabId = relatedTabs.get(0).getId();
                        }
                        updateDialog();
                        updateGridTabSwitcher();
                    }

                    @Override
                    public void tabPendingClosure(Tab tab) {
                        if (!mModel.get(TabGridPanelProperties.IS_DIALOG_VISIBLE)) return;

                        showSingleTabClosureSnackbar(tab);
                    }

                    @Override
                    public void multipleTabsPendingClosure(
                            List<Tab> closedTabs, boolean isAllTabs) {
                        if (!mModel.get(TabGridPanelProperties.IS_DIALOG_VISIBLE)) return;

                        if (closedTabs.size() == 1) {
                            showSingleTabClosureSnackbar(closedTabs.get(0));
                            return;
                        }

                        assert !isAllTabs;
                        String content =
                                String.format(Locale.getDefault(), "%d", closedTabs.size());
                        snackbarManager.showSnackbar(
                                Snackbar.make(
                                                content,
                                                TabGridDialogMediator.this,
                                                Snackbar.TYPE_ACTION,
                                                Snackbar.UMA_TAB_CLOSE_MULTIPLE_UNDO)
                                        .setTemplateText(
                                                mContext.getString(
                                                        R.string.undo_bar_close_all_message))
                                        .setAction(mContext.getString(R.string.undo), closedTabs));
                    }

                    @Override
                    public void tabClosureCommitted(Tab tab) {
                        dismissSingleTabSnackbar(tab.getId());
                    }

                    @Override
                    public void onFinishingMultipleTabClosure(List<Tab> tabs) {
                        if (tabs.size() == 1) {
                            dismissSingleTabSnackbar(tabs.get(0).getId());
                            return;
                        }
                        dismissMultipleTabSnackbar(tabs);
                    }

                    @Override
                    public void allTabsClosureCommitted(boolean isIncognito) {
                        dismissAllSnackbars();
                    }

                    private void showSingleTabClosureSnackbar(Tab tab) {
                        snackbarManager.showSnackbar(
                                Snackbar.make(
                                                tab.getTitle(),
                                                TabGridDialogMediator.this,
                                                Snackbar.TYPE_ACTION,
                                                Snackbar.UMA_TAB_CLOSE_UNDO)
                                        .setTemplateText(
                                                mContext.getString(R.string.undo_bar_close_message))
                                        .setAction(mContext.getString(R.string.undo), tab.getId()));
                    }

                    private void dismissMultipleTabSnackbar(List<Tab> tabs) {
                        PostTask.postTask(
                                TaskTraits.UI_DEFAULT,
                                () -> {
                                    snackbarManager.dismissSnackbars(
                                            TabGridDialogMediator.this, tabs);
                                });
                    }

                    private void dismissSingleTabSnackbar(int tabId) {
                        PostTask.postTask(
                                TaskTraits.UI_DEFAULT,
                                () -> {
                                    snackbarManager.dismissSnackbars(
                                            TabGridDialogMediator.this, tabId);
                                });
                    }

                    private void dismissAllSnackbars() {
                        PostTask.postTask(
                                TaskTraits.UI_DEFAULT,
                                () -> {
                                    snackbarManager.dismissSnackbars(TabGridDialogMediator.this);
                                });
                    }
                };

        mOnTabModelFilterChanged.onResult(
                mCurrentTabModelFilterSupplier.addObserver(mOnTabModelFilterChanged));

        // Setup ScrimView click Runnable.
        mScrimClickRunnable =
                () -> {
                    hideDialog(true);
                    RecordUserAction.record("TabGridDialog.Exit");
                };
        mModel.set(TabGridPanelProperties.VISIBILITY_LISTENER, this);
        mModel.set(TabGridPanelProperties.IS_DIALOG_VISIBLE, false);
        mModel.set(
                TabGridPanelProperties.UNGROUP_BAR_STATUS, TabGridDialogView.UngroupBarStatus.HIDE);
    }

    public void initWithNative(
            Supplier<TabListEditorController> tabListEditorControllerSupplier,
            TabGroupTitleEditor tabGroupTitleEditor) {
        mTabListEditorControllerSupplier = tabListEditorControllerSupplier;
        mTabGroupTitleEditor = tabGroupTitleEditor;

        assert mCurrentTabModelFilterSupplier.get() instanceof TabGroupModelFilter;

        mToolbarMenuCallback =
                result -> {
                    if (result == R.id.ungroup_tab || result == R.id.select_tabs) {
                        mModel.set(TabGridPanelProperties.IS_TITLE_TEXT_FOCUSED, false);
                        if (setupAndShowTabListEditor(mCurrentTabId)) {
                            TabUiMetricsHelper.recordSelectionEditorOpenMetrics(
                                    TabListEditorOpenMetricGroups.OPEN_FROM_DIALOG, mContext);
                        }
                    }

                    if (result == R.id.edit_group_name) {
                        mModel.set(TabGridPanelProperties.IS_TITLE_TEXT_FOCUSED, true);
                    }
                };

        setupToolbarClickHandlers();
        setupToolbarEditText();

        mModel.set(TabGridPanelProperties.MENU_CLICK_LISTENER, getMenuButtonClickListener());
    }

    void hideDialog(boolean showAnimation) {
        if (!mModel.get(TabGridPanelProperties.IS_DIALOG_VISIBLE)) return;

        mSnackbarManager.dismissSnackbars(TabGridDialogMediator.this);

        // Save the title first so that the animation has the correct title.
        saveCurrentGroupModifiedTitle();
        mModel.set(TabGridPanelProperties.IS_TITLE_TEXT_FOCUSED, false);

        if (!showAnimation) {
            mModel.set(TabGridPanelProperties.ANIMATION_SOURCE_VIEW, null);
        } else {
            if (mAnimationSourceViewProvider != null && mCurrentTabId != Tab.INVALID_TAB_ID) {
                mModel.set(
                        TabGridPanelProperties.ANIMATION_SOURCE_VIEW,
                        mAnimationSourceViewProvider.getAnimationSourceViewForTab(mCurrentTabId));
            }
        }
        if (mTabListEditorControllerSupplier != null
                && mTabListEditorControllerSupplier.hasValue()) {
            mTabListEditorControllerSupplier.get().hide();
        }
        // Hide view first. Listener will reset tabs on #finishedHiding.
        mModel.set(TabGridPanelProperties.IS_DIALOG_VISIBLE, false);
    }

    /**
     * @return a boolean indicating if the result of handling the backpress was successful.
     */
    public boolean handleBackPress() {
        if (mTabListEditorControllerSupplier != null
                && mTabListEditorControllerSupplier.hasValue()
                && mTabListEditorControllerSupplier.get().isVisible()) {
            mTabListEditorControllerSupplier.get().hide();
            return !mTabListEditorControllerSupplier.get().isVisible();
        }
        hideDialog(true);
        RecordUserAction.record("TabGridDialog.Exit");
        return !isVisible();
    }

    // @TabGridDialogView.VisibilityListener
    @Override
    public void finishedHidingDialogView() {
        mDialogController.resetWithListOfTabs(null);
        mDialogController.postHiding();
        // Purge the bitmap reference in the animation.
        mModel.set(TabGridPanelProperties.ANIMATION_SOURCE_VIEW, null);
        mModel.set(TabGridPanelProperties.BINDING_TOKEN, null);
    }

    void onReset(@Nullable List<Tab> tabs) {
        TabModelFilter filter = mCurrentTabModelFilterSupplier.get();
        if (tabs == null) {
            mCurrentTabId = Tab.INVALID_TAB_ID;
        } else {
            mCurrentTabId = filter.getTabAt(filter.indexOf(tabs.get(0))).getId();
        }

        if (mCurrentTabId != Tab.INVALID_TAB_ID) {
            if (mAnimationSourceViewProvider != null) {
                mModel.set(
                        TabGridPanelProperties.ANIMATION_SOURCE_VIEW,
                        mAnimationSourceViewProvider.getAnimationSourceViewForTab(mCurrentTabId));
            } else {
                mModel.set(TabGridPanelProperties.ANIMATION_SOURCE_VIEW, null);
            }
            updateDialog();
            mModel.set(TabGridPanelProperties.SCRIMVIEW_CLICK_RUNNABLE, mScrimClickRunnable);
            updateDialogScrollPosition();

            // Do this after the dialog is updated so most attributes are not set with stale values
            // when the binding token is set.
            mModel.set(TabGridPanelProperties.BINDING_TOKEN, hashCode());

            mDialogController.prepareDialog();
            mModel.set(TabGridPanelProperties.IS_DIALOG_VISIBLE, true);
        } else if (mModel.get(TabGridPanelProperties.IS_DIALOG_VISIBLE)) {
            mModel.set(TabGridPanelProperties.IS_DIALOG_VISIBLE, false);
        }
    }

    /** Destroy any members that needs clean up. */
    public void destroy() {
        removeTabModelFilterObserver(mCurrentTabModelFilterSupplier.get());
        mCurrentTabModelFilterSupplier.removeObserver(mOnTabModelFilterChanged);
        KeyboardVisibilityDelegate.getInstance()
                .removeKeyboardVisibilityListener(mKeyboardVisibilityListener);
    }

    boolean isVisible() {
        return mModel.get(TabGridPanelProperties.IS_DIALOG_VISIBLE);
    }

    private void updateGridTabSwitcher() {
        if (!isVisible() || mTabSwitcherResetHandler == null) return;
        mTabSwitcherResetHandler.resetWithTabList(mCurrentTabModelFilterSupplier.get(), false);
    }

    private void updateDialog() {
        final int tabsCount = getRelatedTabs(mCurrentTabId).size();
        if (tabsCount == 0) {
            hideDialog(true);
            return;
        }
        if (mTabGroupTitleEditor != null) {
            Tab currentTab =
                    TabModelUtils.getTabById(
                            mCurrentTabModelFilterSupplier.get().getTabModel(), mCurrentTabId);
            String storedTitle = mTabGroupTitleEditor.getTabGroupTitle(getRootId(currentTab));
            if (storedTitle != null && tabsCount > 1) {
                mModel.set(
                        TabGridPanelProperties.COLLAPSE_BUTTON_CONTENT_DESCRIPTION,
                        mContext.getResources()
                                .getQuantityString(
                                        R.plurals.accessibility_dialog_back_button_with_group_name,
                                        tabsCount,
                                        storedTitle,
                                        tabsCount));
                mModel.set(TabGridPanelProperties.HEADER_TITLE, storedTitle);
                return;
            }
        }
        mModel.set(
                TabGridPanelProperties.COLLAPSE_BUTTON_CONTENT_DESCRIPTION,
                mContext.getResources()
                        .getQuantityString(
                                R.plurals.accessibility_dialog_back_button, tabsCount, tabsCount));
        mModel.set(
                TabGridPanelProperties.HEADER_TITLE,
                TabGroupTitleEditor.getDefaultTitle(mContext, tabsCount));
    }

    private void updateColorProperties(Context context, boolean isIncognito) {
        int dialogBackgroundColor =
                TabUiThemeProvider.getTabGridDialogBackgroundColor(context, isIncognito);
        ColorStateList tintList =
                isIncognito
                        ? AppCompatResources.getColorStateList(
                                mContext, R.color.default_icon_color_light_tint_list)
                        : AppCompatResources.getColorStateList(
                                mContext, R.color.default_icon_color_tint_list);
        int ungroupBarBackgroundColor =
                TabUiThemeProvider.getTabGridDialogUngroupBarBackgroundColor(context, isIncognito);
        int ungroupBarHoveredBackgroundColor =
                TabUiThemeProvider.getTabGridDialogUngroupBarHoveredBackgroundColor(
                        context, isIncognito);
        int ungroupBarTextColor =
                TabUiThemeProvider.getTabGridDialogUngroupBarTextColor(context, isIncognito);
        int ungroupBarHoveredTextColor =
                TabUiThemeProvider.getTabGridDialogUngroupBarHoveredTextColor(context, isIncognito);

        mModel.set(TabGridPanelProperties.DIALOG_BACKGROUND_COLOR, dialogBackgroundColor);
        mModel.set(TabGridPanelProperties.TINT, tintList);
        mModel.set(
                TabGridPanelProperties.DIALOG_UNGROUP_BAR_BACKGROUND_COLOR,
                ungroupBarBackgroundColor);
        mModel.set(
                TabGridPanelProperties.DIALOG_UNGROUP_BAR_HOVERED_BACKGROUND_COLOR,
                ungroupBarHoveredBackgroundColor);
        mModel.set(TabGridPanelProperties.DIALOG_UNGROUP_BAR_TEXT_COLOR, ungroupBarTextColor);
        mModel.set(
                TabGridPanelProperties.DIALOG_UNGROUP_BAR_HOVERED_TEXT_COLOR,
                ungroupBarHoveredTextColor);
    }

    private static int getRootId(Tab tab) {
        return tab.getRootId();
    }

    private int getIdForTab(@Nullable Tab tab) {
        return tab == null ? Tab.INVALID_TAB_ID : tab.getId();
    }

    private void updateDialogScrollPosition() {
        // If current selected tab is not within this dialog, always scroll to the top.
        Tab currentTab = TabModelUtils.getCurrentTab(mCurrentTabModelFilterSupplier.get());
        if (mCurrentTabId != getIdForTab(currentTab)) {
            mModel.set(TabGridPanelProperties.INITIAL_SCROLL_INDEX, 0);
            return;
        }
        List<Tab> relatedTabs = getRelatedTabs(mCurrentTabId);
        int initialPosition = relatedTabs.indexOf(currentTab);
        mModel.set(TabGridPanelProperties.INITIAL_SCROLL_INDEX, initialPosition);
    }

    private void setupToolbarClickHandlers() {
        mModel.set(
                TabGridPanelProperties.COLLAPSE_CLICK_LISTENER, getCollapseButtonClickListener());
        mModel.set(TabGridPanelProperties.ADD_CLICK_LISTENER, getAddButtonClickListener());
    }

    private void setupDialogSelectionEditor() {
        assert mTabListEditorControllerSupplier != null;

        if (!mTabListEditorControllerSupplier.hasValue() || mTabListEditorSetup) {
            return;
        }

        mTabListEditorSetup = true;

        List<TabListEditorAction> actions = new ArrayList<>();
        actions.add(
                TabListEditorSelectionAction.createAction(
                        mContext, ShowMode.MENU_ONLY, ButtonType.ICON_AND_TEXT, IconPosition.END));
        actions.add(
                TabListEditorCloseAction.createAction(
                        mContext,
                        ShowMode.MENU_ONLY,
                        ButtonType.ICON_AND_TEXT,
                        IconPosition.START));
        actions.add(
                TabListEditorUngroupAction.createAction(
                        mContext,
                        ShowMode.MENU_ONLY,
                        ButtonType.ICON_AND_TEXT,
                        IconPosition.START));
        actions.add(
                TabListEditorBookmarkAction.createAction(
                        mActivity,
                        ShowMode.MENU_ONLY,
                        ButtonType.ICON_AND_TEXT,
                        IconPosition.START));
        actions.add(
                TabListEditorShareAction.createAction(
                        mContext,
                        ShowMode.MENU_ONLY,
                        ButtonType.ICON_AND_TEXT,
                        IconPosition.START));
        mTabListEditorControllerSupplier.get().configureToolbarWithMenuItems(actions, null);
    }

    private void setupToolbarEditText() {
        mKeyboardVisibilityListener =
                isShowing -> {
                    mModel.set(TabGridPanelProperties.TITLE_CURSOR_VISIBILITY, isShowing);
                    if (!isShowing) {
                        mModel.set(TabGridPanelProperties.IS_TITLE_TEXT_FOCUSED, false);
                        saveCurrentGroupModifiedTitle();
                    }
                };
        KeyboardVisibilityDelegate.getInstance()
                .addKeyboardVisibilityListener(mKeyboardVisibilityListener);

        TextWatcher textWatcher =
                new EmptyTextWatcher() {
                    @Override
                    public void afterTextChanged(Editable s) {
                        if (!mIsUpdatingTitle) return;
                        mCurrentGroupModifiedTitle = s.toString();
                    }
                };
        mModel.set(TabGridPanelProperties.TITLE_TEXT_WATCHER, textWatcher);

        View.OnFocusChangeListener onFocusChangeListener =
                (v, hasFocus) -> {
                    mIsUpdatingTitle = hasFocus;
                    mModel.set(TabGridPanelProperties.IS_KEYBOARD_VISIBLE, hasFocus);
                    mModel.set(TabGridPanelProperties.IS_TITLE_TEXT_FOCUSED, hasFocus);
                };
        mModel.set(TabGridPanelProperties.TITLE_TEXT_ON_FOCUS_LISTENER, onFocusChangeListener);
    }

    private View.OnClickListener getCollapseButtonClickListener() {
        return view -> {
            hideDialog(true);
            RecordUserAction.record("TabGridDialog.Exit");
        };
    }

    private View.OnClickListener getAddButtonClickListener() {
        return view -> {
            // Get the current Tab first since hideDialog causes mCurrentTabId to be
            // Tab.INVALID_TAB_ID.
            TabModelFilter filter = mCurrentTabModelFilterSupplier.get();
            Tab currentTab = TabModelUtils.getTabById(filter.getTabModel(), mCurrentTabId);
            hideDialog(false);

            // Reset the list of tabs so the new tab doesn't appear on the dialog before the
            // animation.
            if (!DeviceFormFactor.isNonMultiDisplayContextOnTablet(mContext)) {
                mDialogController.resetWithListOfTabs(null);
            }

            if (currentTab == null) {
                mTabCreatorManager.getTabCreator(filter.isIncognito()).launchNtp();
                return;
            }
            List<Tab> relatedTabs = getRelatedTabs(currentTab.getId());

            assert relatedTabs.size() > 0;

            Tab parentTabToAttach = relatedTabs.get(relatedTabs.size() - 1);
            mTabCreatorManager
                    .getTabCreator(currentTab.isIncognito())
                    .createNewTab(
                            new LoadUrlParams(UrlConstants.NTP_URL),
                            TabLaunchType.FROM_TAB_GROUP_UI,
                            parentTabToAttach);
            RecordUserAction.record("MobileNewTabOpened." + mComponentName);
        };
    }

    private View.OnClickListener getMenuButtonClickListener() {
        assert mTabListEditorControllerSupplier != null;
        return TabGridDialogMenuCoordinator.getTabGridDialogMenuOnClickListener(
                mToolbarMenuCallback);
    }

    private List<Tab> getRelatedTabs(int tabId) {
        return mCurrentTabModelFilterSupplier.get().getRelatedTabList(tabId);
    }

    private void saveCurrentGroupModifiedTitle() {
        // When current group no longer exists, skip saving the title.
        if (getRelatedTabs(mCurrentTabId).size() < 2) {
            mCurrentGroupModifiedTitle = null;
        }

        if (mCurrentGroupModifiedTitle == null) {
            return;
        }
        assert mTabGroupTitleEditor != null;

        Tab currentTab =
                TabModelUtils.getTabById(
                        mCurrentTabModelFilterSupplier.get().getTabModel(), mCurrentTabId);
        int tabsCount = getRelatedTabs(mCurrentTabId).size();
        assert tabsCount >= 2;
        if (mCurrentGroupModifiedTitle.length() == 0
                || mTabGroupTitleEditor.isDefaultTitle(mCurrentGroupModifiedTitle, tabsCount)) {
            // When dialog title is empty or was unchanged, delete previously stored title and
            // restore default title.
            mTabGroupTitleEditor.deleteTabGroupTitle(getRootId(currentTab));

            String originalTitle = TabGroupTitleEditor.getDefaultTitle(mContext, tabsCount);
            mModel.set(
                    TabGridPanelProperties.COLLAPSE_BUTTON_CONTENT_DESCRIPTION,
                    mContext.getResources()
                            .getQuantityString(
                                    R.plurals.accessibility_dialog_back_button,
                                    tabsCount,
                                    tabsCount));
            mModel.set(TabGridPanelProperties.HEADER_TITLE, originalTitle);
            mTabGroupTitleEditor.updateTabGroupTitle(currentTab, originalTitle);
            mCurrentGroupModifiedTitle = null;
            return;
        }
        mTabGroupTitleEditor.storeTabGroupTitle(getRootId(currentTab), mCurrentGroupModifiedTitle);
        mTabGroupTitleEditor.updateTabGroupTitle(currentTab, mCurrentGroupModifiedTitle);
        int relatedTabsCount = getRelatedTabs(mCurrentTabId).size();
        mModel.set(
                TabGridPanelProperties.COLLAPSE_BUTTON_CONTENT_DESCRIPTION,
                mContext.getResources()
                        .getQuantityString(
                                R.plurals.accessibility_dialog_back_button_with_group_name,
                                relatedTabsCount,
                                mCurrentGroupModifiedTitle,
                                relatedTabsCount));
        mModel.set(TabGridPanelProperties.HEADER_TITLE, mCurrentGroupModifiedTitle);
        RecordUserAction.record("TabGridDialog.TabGroupNamedInDialog");
        mCurrentGroupModifiedTitle = null;
    }

    TabListMediator.TabGridDialogHandler getTabGridDialogHandler() {
        return mTabGridDialogHandler;
    }

    // SnackbarManager.SnackbarController implementation.
    @Override
    public void onAction(Object actionData) {
        if (actionData instanceof Integer) {
            int tabId = (Integer) actionData;
            TabModel model = mCurrentTabModelFilterSupplier.get().getTabModel();

            model.cancelTabClosure(tabId);
        } else {
            List<Tab> tabs = (List<Tab>) actionData;
            if (tabs.isEmpty()) return;
            TabModel model = mCurrentTabModelFilterSupplier.get().getTabModel();

            for (Tab tab : tabs) {
                model.cancelTabClosure(tab.getId());
            }
        }
    }

    @Override
    public void onDismissNoAction(Object actionData) {
        if (actionData instanceof Integer) {
            int tabId = (Integer) actionData;
            TabModel model = mCurrentTabModelFilterSupplier.get().getTabModel();

            model.commitTabClosure(tabId);
        } else {
            List<Tab> tabs = (List<Tab>) actionData;
            if (tabs.isEmpty()) return;

            TabModel model = mCurrentTabModelFilterSupplier.get().getTabModel();

            for (Tab tab : tabs) {
                model.commitTabClosure(tab.getId());
            }
        }
    }

    // OnLongPressTabItemEventListener implementation
    @Override
    public void onLongPressEvent(int tabId) {
        if (setupAndShowTabListEditor(tabId)) {
            RecordUserAction.record("TabMultiSelectV2.OpenLongPressInDialog");
        }
    }

    private boolean setupAndShowTabListEditor(int currentTabId) {
        if (mTabListEditorControllerSupplier == null) return false;

        List<Tab> tabs = getRelatedTabs(currentTabId);
        // Setup dialog selection editor.
        setupDialogSelectionEditor();
        mTabListEditorControllerSupplier
                .get()
                .show(tabs, /* preSelectedTabCount= */ 0, mRecyclerViewPositionSupplier.get());
        return true;
    }

    private void onTabModelFilterChanged(
            @Nullable TabModelFilter newFilter, @Nullable TabModelFilter oldFilter) {
        removeTabModelFilterObserver(oldFilter);

        if (newFilter != null) {
            updateColorProperties(mContext, newFilter.isIncognito());
            newFilter.addObserver(mTabModelObserver);
        }
    }

    private void removeTabModelFilterObserver(@Nullable TabModelFilter filter) {
        if (filter != null) {
            filter.removeObserver(mTabModelObserver);
        }
    }

    /**
     * A handler that handles TabGridDialog related changes originated from {@link TabListMediator}
     * and {@link TabGridItemTouchHelperCallback}.
     */
    class DialogHandler implements TabListMediator.TabGridDialogHandler {
        @Override
        public void updateUngroupBarStatus(@TabGridDialogView.UngroupBarStatus int status) {
            mModel.set(TabGridPanelProperties.UNGROUP_BAR_STATUS, status);
        }

        @Override
        public void updateDialogContent(int tabId) {
            mCurrentTabId = tabId;
            updateDialog();
        }
    }

    int getCurrentTabIdForTesting() {
        return mCurrentTabId;
    }

    void setCurrentTabIdForTesting(int tabId) {
        var oldValue = mCurrentTabId;
        mCurrentTabId = tabId;
        ResettersForTesting.register(() -> mCurrentTabId = oldValue);
    }

    KeyboardVisibilityDelegate.KeyboardVisibilityListener
            getKeyboardVisibilityListenerForTesting() {
        return mKeyboardVisibilityListener;
    }

    boolean getIsUpdatingTitleForTesting() {
        return mIsUpdatingTitle;
    }

    String getCurrentGroupModifiedTitleForTesting() {
        return mCurrentGroupModifiedTitle;
    }

    Callback<Integer> getToolbarMenuCallbackForTesting() {
        return mToolbarMenuCallback;
    }

    Runnable getScrimClickRunnableForTesting() {
        return mScrimClickRunnable;
    }
}
