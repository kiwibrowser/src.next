// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.tasks.tab_management;

import static org.chromium.chrome.browser.tasks.tab_management.MessageCardViewProperties.MESSAGE_TYPE;
import static org.chromium.chrome.browser.tasks.tab_management.TabListModel.CardProperties.CARD_ALPHA;
import static org.chromium.chrome.browser.tasks.tab_management.TabListModel.CardProperties.CARD_TYPE;
import static org.chromium.chrome.browser.tasks.tab_management.TabListModel.CardProperties.ModelType.TAB;
import static org.chromium.chrome.browser.tasks.tab_management.TabProperties.CLOSE_BUTTON_DESCRIPTION_STRING;
import static org.chromium.chrome.browser.tasks.tab_management.TabProperties.TAB_ID;

import android.content.ComponentCallbacks;
import android.content.Context;
import android.content.res.ColorStateList;
import android.content.res.Configuration;
import android.graphics.Bitmap;
import android.os.Bundle;
import android.os.Handler;
import android.text.TextUtils;
import android.util.Pair;
import android.util.Size;
import android.view.View;
import android.view.accessibility.AccessibilityNodeInfo;
import android.view.accessibility.AccessibilityNodeInfo.AccessibilityAction;

import androidx.annotation.IntDef;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.annotation.VisibleForTesting;
import androidx.appcompat.content.res.AppCompatResources;
import androidx.recyclerview.widget.GridLayoutManager;
import androidx.recyclerview.widget.ItemTouchHelper;
import androidx.recyclerview.widget.RecyclerView;
import androidx.recyclerview.widget.RecyclerView.OnScrollListener;

import org.chromium.base.Callback;
import org.chromium.base.Log;
import org.chromium.base.ResettersForTesting;
import org.chromium.base.ValueChangedCallback;
import org.chromium.base.metrics.RecordHistogram;
import org.chromium.base.metrics.RecordUserAction;
import org.chromium.base.shared_preferences.SharedPreferencesManager;
import org.chromium.base.supplier.ObservableSupplier;
import org.chromium.base.supplier.Supplier;
import org.chromium.base.task.PostTask;
import org.chromium.base.task.TaskTraits;
import org.chromium.chrome.browser.preferences.ChromePreferenceKeys;
import org.chromium.chrome.browser.preferences.ChromeSharedPreferences;
import org.chromium.chrome.browser.price_tracking.PriceTrackingFeatures;
import org.chromium.chrome.browser.price_tracking.PriceTrackingUtilities;
import org.chromium.chrome.browser.profiles.Profile;
import org.chromium.chrome.browser.tab.EmptyTabObserver;
import org.chromium.chrome.browser.tab.Tab;
import org.chromium.chrome.browser.tab.TabCreationState;
import org.chromium.chrome.browser.tab.TabLaunchType;
import org.chromium.chrome.browser.tab.TabObserver;
import org.chromium.chrome.browser.tab.TabSelectionType;
import org.chromium.chrome.browser.tab.state.ShoppingPersistedTabData;
import org.chromium.chrome.browser.tabmodel.TabList;
import org.chromium.chrome.browser.tabmodel.TabModel;
import org.chromium.chrome.browser.tabmodel.TabModelFilter;
import org.chromium.chrome.browser.tabmodel.TabModelObserver;
import org.chromium.chrome.browser.tabmodel.TabModelSelector;
import org.chromium.chrome.browser.tabmodel.TabModelUtils;
import org.chromium.chrome.browser.tasks.pseudotab.PseudoTab;
import org.chromium.chrome.browser.tasks.tab_groups.EmptyTabGroupModelFilterObserver;
import org.chromium.chrome.browser.tasks.tab_groups.TabGroupModelFilter;
import org.chromium.chrome.browser.tasks.tab_groups.TabGroupTitleUtils;
import org.chromium.chrome.browser.tasks.tab_groups.TabGroupUtils;
import org.chromium.chrome.browser.tasks.tab_management.PriceMessageService.PriceTabData;
import org.chromium.chrome.browser.tasks.tab_management.TabListCoordinator.TabListMode;
import org.chromium.chrome.browser.tasks.tab_management.TabListFaviconProvider.TabFaviconFetcher;
import org.chromium.chrome.browser.tasks.tab_management.TabProperties.UiType;
import org.chromium.chrome.browser.tasks.tab_management.TabUiMetricsHelper.TabListEditorActionMetricGroups;
import org.chromium.chrome.tab_ui.R;
import org.chromium.components.browser_ui.styles.SemanticColorUtils;
import org.chromium.components.browser_ui.widget.selectable_list.SelectionDelegate;
import org.chromium.components.embedder_support.util.UrlUtilities;
import org.chromium.components.feature_engagement.FeatureConstants;
import org.chromium.content_public.browser.NavigationHandle;
import org.chromium.ui.base.DeviceFormFactor;
import org.chromium.ui.modelutil.ListObservable;
import org.chromium.ui.modelutil.ListObservable.ListObserver;
import org.chromium.ui.modelutil.PropertyModel;
import org.chromium.ui.modelutil.SimpleRecyclerViewAdapter;
import org.chromium.url.GURL;

import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;

/**
 * Mediator for business logic for the tab grid. This class should be initialized with a list of
 * tabs and a TabModel to observe for changes and should not have any logic around what the list
 * signifies.
 * TODO(yusufo): Move some of the logic here to a parent component to make the above true.
 */
class TabListMediator {
    // The |mVisible| relies on whether the tab list is null when the last time
    // resetWithListOfTabs() was called, but not whether the RecyclerView is actually showing on the
    // screen.
    private boolean mVisible;
    private boolean mShownIPH;
    private Tab mTabToAddDelayed;

    /** An interface to handle requests about updating TabGridDialog. */
    public interface TabGridDialogHandler {
        /**
         * This method updates the status of the ungroup bar in TabGridDialog.
         *
         * @param status The status in {@link TabGridDialogView.UngroupBarStatus} that the ungroup
         *         bar should be updated to.
         */
        void updateUngroupBarStatus(@TabGridDialogView.UngroupBarStatus int status);

        /**
         * This method updates the content of the TabGridDialog.
         *
         * @param tabId The id of the {@link Tab} that is used to update TabGridDialog.
         */
        void updateDialogContent(int tabId);
    }

    /**
     * An interface to expose functionality needed to support reordering in grid layouts in
     * accessibility mode.
     */
    public interface TabGridAccessibilityHelper {
        /**
         * This method gets the possible actions for reordering a tab in grid layout.
         *
         * @param view The host view that triggers the accessibility action.
         * @return The list of possible {@link AccessibilityAction}s for host view.
         */
        List<AccessibilityAction> getPotentialActionsForView(View view);

        /**
         * This method gives the previous and target position of current reordering based on the
         * host view and current action.
         *
         * @param view   The host view that triggers the accessibility action.
         * @param action The id of the action.
         * @return {@link Pair} that contains previous and target position of this action.
         */
        Pair<Integer, Integer> getPositionsOfReorderAction(View view, int action);

        /**
         * This method returns whether the given action is a type of the reordering actions.
         *
         * @param action The accessibility action.
         * @return Whether the given action is a reordering action.
         */
        boolean isReorderAction(int action);
    }

    /** Provides capability to asynchronously acquire {@link ShoppingPersistedTabData} */
    static class ShoppingPersistedTabDataFetcher {
        protected final Tab mTab;
        protected final Supplier<PriceWelcomeMessageController>
                mPriceWelcomeMessageControllerSupplier;

        /**
         * @param tab {@link Tab} {@link ShoppingPersistedTabData} will be acquired for.
         * @param priceWelcomeMessageControllerSupplier to show the price welcome message.
         */
        ShoppingPersistedTabDataFetcher(
                Tab tab,
                @NonNull
                        Supplier<PriceWelcomeMessageController>
                                priceWelcomeMessageControllerSupplier) {
            mTab = tab;
            mPriceWelcomeMessageControllerSupplier = priceWelcomeMessageControllerSupplier;
        }

        /**
         * Asynchronously acquire {@link ShoppingPersistedTabData}
         * @param callback {@link Callback} to pass {@link ShoppingPersistedTabData} back in
         */
        public void fetch(Callback<ShoppingPersistedTabData> callback) {
            ShoppingPersistedTabData.from(
                    mTab,
                    (res) -> {
                        callback.onResult(res);
                        maybeShowPriceWelcomeMessage(res);
                    });
        }

        @VisibleForTesting
        void maybeShowPriceWelcomeMessage(
                @Nullable ShoppingPersistedTabData shoppingPersistedTabData) {
            // Avoid inserting message while RecyclerView is computing a layout.
            new Handler()
                    .post(
                            () -> {
                                if (!PriceTrackingUtilities.isPriceWelcomeMessageCardEnabled(
                                                mTab.getProfile())
                                        || (mPriceWelcomeMessageControllerSupplier == null)
                                        || (mPriceWelcomeMessageControllerSupplier.get() == null)
                                        || (shoppingPersistedTabData == null)
                                        || (shoppingPersistedTabData.getPriceDrop() == null)) {
                                    return;
                                }
                                mPriceWelcomeMessageControllerSupplier
                                        .get()
                                        .showPriceWelcomeMessage(
                                                new PriceTabData(
                                                        mTab.getId(),
                                                        shoppingPersistedTabData.getPriceDrop()));
                            });
        }
    }

    /**
     * The object to set to {@link TabProperties#THUMBNAIL_FETCHER} for the TabGridViewBinder to
     * obtain the thumbnail asynchronously.
     */
    static class ThumbnailFetcher {
        static Callback<Bitmap> sBitmapCallbackForTesting;
        static int sFetchCountForTesting;
        private ThumbnailProvider mThumbnailProvider;
        private int mId;
        private boolean mForceUpdate;
        private boolean mWriteToCache;

        ThumbnailFetcher(
                ThumbnailProvider provider, int id, boolean forceUpdate, boolean writeToCache) {
            mThumbnailProvider = provider;
            mId = id;
            mForceUpdate = forceUpdate;
            mWriteToCache = writeToCache;
        }

        void fetch(Callback<Bitmap> callback, Size thumbnailSize, boolean isSelected) {
            Callback<Bitmap> forking =
                    (bitmap) -> {
                        if (sBitmapCallbackForTesting != null) {
                            sBitmapCallbackForTesting.onResult(bitmap);
                        }
                        callback.onResult(bitmap);
                    };
            sFetchCountForTesting++;
            mThumbnailProvider.getTabThumbnailWithCallback(
                    mId, thumbnailSize, forking, mForceUpdate, mWriteToCache, isSelected);
        }
    }

    /** An interface to show IPH for a tab. */
    public interface IphProvider {
        void showIPH(View anchor);
    }

    private final IphProvider mIphProvider =
            new IphProvider() {
                private static final int IPH_DELAY_MS = 1000;

                @Override
                public void showIPH(View anchor) {
                    if (mShownIPH) return;
                    mShownIPH = true;

                    PostTask.postDelayedTask(
                            TaskTraits.UI_DEFAULT,
                            () -> {
                                TabGroupUtils.maybeShowIPH(
                                        FeatureConstants.TAB_GROUPS_YOUR_TABS_ARE_TOGETHER_FEATURE,
                                        anchor,
                                        null);
                            },
                            IPH_DELAY_MS);
                }
            };

    /**
     * An interface to get a SelectionDelegate that contains the selected items for a selectable
     * tab list.
     */
    public interface SelectionDelegateProvider {
        SelectionDelegate getSelectionDelegate();
    }

    /** An interface to get the onClickListener when clicking on a grid card. */
    interface GridCardOnClickListenerProvider {
        /**
         * @return {@link TabActionListener} to open Tab Grid dialog.
         * If the given {@link Tab} is not able to create group, return null;
         */
        @Nullable
        TabActionListener openTabGridDialog(@NonNull Tab tab);

        /**
         * Run additional actions on tab selection.
         * @param tabId The ID of selected {@link Tab}.
         * @param fromActionButton Whether it is called from the Action button on the card.
         */
        void onTabSelecting(int tabId, boolean fromActionButton);
    }

    @IntDef({
        TabClosedFrom.TAB_STRIP,
        TabClosedFrom.GRID_TAB_SWITCHER,
        TabClosedFrom.GRID_TAB_SWITCHER_GROUP
    })
    @Retention(RetentionPolicy.SOURCE)
    private @interface TabClosedFrom {
        int TAB_STRIP = 0;
        // int TAB_GRID_SHEET = 1;  // Obsolete
        int GRID_TAB_SWITCHER = 2;
        int GRID_TAB_SWITCHER_GROUP = 3;
        int NUM_ENTRIES = 4;
    }

    private static final String TAG = "TabListMediator";
    private static Map<Integer, Integer> sTabClosedFromMapTabClosedFromMap = new HashMap<>();
    private static Set<Integer> sViewedTabIds = new HashSet<>();

    private final Context mContext;
    private final TabListModel mModel;
    private final @TabListMode int mMode;
    private final ObservableSupplier<TabModelFilter> mCurrentTabModelFilterSupplier;
    // TODO(crbug/1505772): Refactor price drops so we don't need this.
    private final Supplier<TabModel> mRegularTabModelSupplier;
    private final ValueChangedCallback<TabModelFilter> mOnTabModelFilterChanged =
            new ValueChangedCallback<>(this::onTabModelFilterChanged);
    private final TabActionListener mTabClosedListener;
    private final PseudoTab.TitleProvider mTitleProvider;
    private final SelectionDelegateProvider mSelectionDelegateProvider;
    private final GridCardOnClickListenerProvider mGridCardOnClickListenerProvider;
    private final TabGridDialogHandler mTabGridDialogHandler;
    private final TabListFaviconProvider mTabListFaviconProvider;
    private final Supplier<PriceWelcomeMessageController> mPriceWelcomeMessageControllerSupplier;

    private @Nullable Profile mProfile;
    private Size mDefaultGridCardSize;
    private String mComponentName;
    private ThumbnailProvider mThumbnailProvider;
    private boolean mActionsOnAllRelatedTabs;
    private ComponentCallbacks mComponentCallbacks;
    private TabGridItemTouchHelperCallback mTabGridItemTouchHelperCallback;
    private int mNextTabId = Tab.INVALID_TAB_ID;
    private @UiType int mUiType;
    private GridLayoutManager mGridLayoutManager;
    // mRecyclerView and mOnScrollListener are null, unless the the price drop IPH or badge is
    // enabled.
    private @Nullable RecyclerView mRecyclerView;
    private @Nullable OnScrollListener mOnScrollListener;

    private final TabActionListener mTabSelectedListener =
            new TabActionListener() {
                @Override
                public void run(int tabId) {
                    if (mModel.indexFromId(tabId) == TabModel.INVALID_TAB_INDEX) return;

                    mNextTabId = tabId;

                    TabModel tabModel = mCurrentTabModelFilterSupplier.get().getTabModel();
                    if (!mActionsOnAllRelatedTabs) {
                        Tab currentTab = TabModelUtils.getCurrentTab(tabModel);
                        Tab newlySelectedTab = TabModelUtils.getTabById(tabModel, tabId);

                        // We filtered the tab switching related metric for components that takes
                        // actions on all related tabs (e.g. GTS) because that component can
                        // switch to different TabModel before switching tabs, while this class
                        // only contains information for all tabs that are in the same TabModel,
                        // more specifically:
                        //   * For Tabs.TabOffsetOfSwitch, we do not want to log anything if the
                        // user
                        //     switched from normal to incognito or vice-versa.
                        //   * For MobileTabSwitched, as compared to the VTS, we need to account for
                        //     MobileTabReturnedToCurrentTab action. This action is defined as
                        // return to the
                        //     same tab as before entering the component, and we don't have this
                        // information
                        //     here.
                        recordUserSwitchedTab(currentTab, newlySelectedTab);
                    }
                    if (mGridCardOnClickListenerProvider != null) {
                        mGridCardOnClickListenerProvider.onTabSelecting(
                                tabId, /* fromActionButton= */ true);
                    } else {
                        tabModel.setIndex(
                                TabModelUtils.getTabIndexById(tabModel, tabId),
                                TabSelectionType.FROM_USER,
                                false);
                    }
                }

                /**
                 * Records MobileTabSwitched for the component. Also, records Tabs.TabOffsetOfSwitch
                 * but only when fromTab and toTab are within the same group. This method only
                 * records UMA for components other than TabSwitcher.
                 *
                 * @param fromTab The previous selected tab.
                 * @param toTab The new selected tab.
                 */
                private void recordUserSwitchedTab(Tab fromTab, Tab toTab) {
                    TabModelFilter filter = mCurrentTabModelFilterSupplier.get();
                    int fromFilterIndex = filter.indexOf(fromTab);
                    int toFilterIndex = filter.indexOf(toTab);

                    RecordUserAction.record("MobileTabSwitched." + mComponentName);

                    if (fromFilterIndex != toFilterIndex) return;

                    TabModel tabModel = filter.getTabModel();
                    int fromIndex = TabModelUtils.getTabIndexById(tabModel, fromTab.getId());
                    int toIndex = TabModelUtils.getTabIndexById(tabModel, toTab.getId());

                    RecordHistogram.recordSparseHistogram(
                            "Tabs.TabOffsetOfSwitch." + mComponentName, fromIndex - toIndex);
                }
            };

    private final TabActionListener mSelectableTabOnClickListener =
            new TabActionListener() {
                @Override
                public void run(int tabId) {
                    int index = mModel.indexFromId(tabId);
                    if (index == TabModel.INVALID_TAB_INDEX) return;
                    boolean selected = mModel.get(index).model.get(TabProperties.IS_SELECTED);
                    if (selected) {
                        TabUiMetricsHelper.recordSelectionEditorActionMetrics(
                                TabListEditorActionMetricGroups.UNSELECTED);
                    } else {
                        TabUiMetricsHelper.recordSelectionEditorActionMetrics(
                                TabListEditorActionMetricGroups.SELECTED);
                    }
                    mModel.get(index).model.set(TabProperties.IS_SELECTED, !selected);
                    // Reset thumbnail to ensure the color of the blank tab slots is correct.
                    TabModelFilter filter = mCurrentTabModelFilterSupplier.get();
                    Tab tab = TabModelUtils.getTabById(filter.getTabModel(), tabId);
                    if (tab != null && filter.hasOtherRelatedTabs(tab)) {
                        mModel.get(index)
                                .model
                                .set(
                                        TabProperties.THUMBNAIL_FETCHER,
                                        new ThumbnailFetcher(
                                                mThumbnailProvider, tabId, false, false));
                    }
                }
            };

    private final TabObserver mTabObserver =
            new EmptyTabObserver() {
                @Override
                public void onDidStartNavigationInPrimaryMainFrame(
                        Tab tab, NavigationHandle navigationHandle) {
                    // The URL of the tab and the navigation handle can match without it being a
                    // same document navigation if the tab had no renderer and needed to start a
                    // new one.
                    // See https://crbug.com/1359002.
                    if (navigationHandle.isSameDocument()
                            || UrlUtilities.isNtpUrl(tab.getUrl())
                            || tab.getUrl().equals(navigationHandle.getUrl())) {
                        return;
                    }
                    if (mModel.indexFromId(tab.getId()) == TabModel.INVALID_TAB_INDEX) return;

                    mModel.get(mModel.indexFromId(tab.getId()))
                            .model
                            .set(
                                    TabProperties.FAVICON_FETCHER,
                                    mTabListFaviconProvider.getDefaultFaviconFetcher(
                                            tab.isIncognito()));
                }

                @Override
                public void onTitleUpdated(Tab updatedTab) {
                    int index = mModel.indexFromId(updatedTab.getId());
                    // TODO(crbug.com/1098100) The null check for tab here should be redundant once
                    // we have resolved the bug.
                    if (index == TabModel.INVALID_TAB_INDEX
                            || TabModelUtils.getTabById(
                                            mCurrentTabModelFilterSupplier.get().getTabModel(),
                                            updatedTab.getId())
                                    == null) {
                        return;
                    }
                    mModel.get(index)
                            .model
                            .set(
                                    TabProperties.TITLE,
                                    getLatestTitleForTab(PseudoTab.fromTab(updatedTab)));
                }

                @Override
                public void onFaviconUpdated(Tab updatedTab, Bitmap icon, GURL iconUrl) {
                    updateFaviconForTab(PseudoTab.fromTab(updatedTab), icon, iconUrl);
                }

                @Override
                public void onUrlUpdated(Tab tab) {
                    int index = mModel.indexFromId(tab.getId());

                    if (index == TabModel.INVALID_TAB_INDEX && mActionsOnAllRelatedTabs) {
                        Tab currentGroupSelectedTab =
                                TabGroupUtils.getSelectedTabInGroupForTab(
                                        (TabGroupModelFilter) mCurrentTabModelFilterSupplier.get(),
                                        tab);
                        if (currentGroupSelectedTab == null) return;
                        index = mModel.indexFromId(currentGroupSelectedTab.getId());
                    }

                    if (index == TabModel.INVALID_TAB_INDEX) return;
                    mModel.get(index).model.set(TabProperties.URL_DOMAIN, getDomainForTab(tab));
                }
            };

    /** Interface for toggling whether item animations will run on the recycler view. */
    interface RecyclerViewItemAnimationToggle {
        void setDisableItemAnimations(boolean state);
    }

    private RecyclerViewItemAnimationToggle mRecyclerViewItemAnimationToggle;

    private final TabModelObserver mTabModelObserver;

    private ListObserver<Void> mListObserver;

    private TabGroupTitleEditor mTabGroupTitleEditor;

    private View.AccessibilityDelegate mAccessibilityDelegate;

    private int mLastSelectedTabListModelIndex = TabList.INVALID_TAB_INDEX;

    private final TabGroupModelFilter.Observer mTabGroupObserver =
            new EmptyTabGroupModelFilterObserver() {
                @Override
                public void didMoveWithinGroup(
                        Tab movedTab, int tabModelOldIndex, int tabModelNewIndex) {
                    if (!mVisible || tabModelNewIndex == tabModelOldIndex) return;

                    TabModelFilter filter = mCurrentTabModelFilterSupplier.get();
                    TabModel tabModel = filter.getTabModel();

                    // For the tab switcher update the tab card correctly.
                    if (mActionsOnAllRelatedTabs && mThumbnailProvider != null) {
                        int indexInModel = getIndexForTabWithRelatedTabs(movedTab);
                        if (indexInModel == TabModel.INVALID_TAB_INDEX) return;

                        Tab lastShownTab = filter.getTabAt(filter.indexOf(movedTab));
                        mModel.get(indexInModel)
                                .model
                                .set(
                                        TabProperties.THUMBNAIL_FETCHER,
                                        new ThumbnailFetcher(
                                                mThumbnailProvider,
                                                lastShownTab.getId(),
                                                true,
                                                false));
                        return;
                    }

                    // For the grid dialog maintain order.
                    int curPosition = mModel.indexFromId(movedTab.getId());

                    if (!isValidMovePosition(curPosition)) return;

                    Tab destinationTab =
                            tabModel.getTabAt(
                                    tabModelNewIndex > tabModelOldIndex
                                            ? tabModelNewIndex - 1
                                            : tabModelNewIndex + 1);

                    int newPosition = mModel.indexFromId(destinationTab.getId());

                    if (!isValidMovePosition(newPosition)) return;
                    mModel.move(curPosition, newPosition);
                }

                @Override
                public void didMoveTabOutOfGroup(Tab movedTab, int prevFilterIndex) {
                    if (!mVisible) return;
                    assert !(mActionsOnAllRelatedTabs && mTabGridDialogHandler != null);
                    TabModelFilter filter = mCurrentTabModelFilterSupplier.get();
                    Tab groupTab = filter.getTabAt(prevFilterIndex);
                    boolean isUngroupingLastTabInGroup = groupTab.getId() == movedTab.getId();
                    if (mActionsOnAllRelatedTabs) {
                        if (isUngroupingLastTabInGroup) return;

                        final int currentSelectedTabId =
                                TabModelUtils.getCurrentTabId(filter.getTabModel());
                        // Only add a tab to the model if it represents a new card (new group or new
                        // singular tab). However, always update the previous group to clean up old
                        // state. The addition of the new tab to an existing group is handled in
                        // didMergeTabToGroup().
                        if (!filter.hasOtherRelatedTabs(movedTab)) {
                            int filterIndex = filter.indexOf(movedTab);
                            addTabInfoToModel(
                                    PseudoTab.fromTab(movedTab),
                                    mModel.indexOfNthTabCard(filterIndex),
                                    currentSelectedTabId == movedTab.getId());
                        }
                        boolean isSelected = currentSelectedTabId == groupTab.getId();
                        updateTab(
                                mModel.indexOfNthTabCard(prevFilterIndex),
                                PseudoTab.fromTab(groupTab),
                                isSelected,
                                true,
                                false);
                    } else {
                        int curTabListModelIndex = mModel.indexFromId(movedTab.getId());
                        if (!isValidMovePosition(curTabListModelIndex)) return;
                        mModel.removeAt(curTabListModelIndex);
                        if (mTabGridDialogHandler != null) {
                            mTabGridDialogHandler.updateDialogContent(
                                    isUngroupingLastTabInGroup
                                            ? Tab.INVALID_TAB_ID
                                            : filter.getTabAt(prevFilterIndex).getId());
                        }
                    }
                }

                @Override
                public void didMergeTabToGroup(Tab movedTab, int selectedTabIdInGroup) {
                    if (!mVisible || !mActionsOnAllRelatedTabs) return;

                    // When merging Tab 1 to Tab 2 as a new group, or merging Tab 1 to an
                    // existing group 1, we can always find the current indexes of 1) Tab 1
                    // and 2) Tab 2 or group 1 in the model. The method
                    // getIndexesForMergeToGroup() returns these two ids by using Tab 1's
                    // related Tabs, which have been updated in
                    // TabModel.
                    TabModelFilter filter = mCurrentTabModelFilterSupplier.get();
                    TabModel tabModel = filter.getTabModel();
                    List<Tab> relatedTabs = getRelatedTabsForId(movedTab.getId());
                    Pair<Integer, Integer> positions =
                            mModel.getIndexesForMergeToGroup(tabModel, relatedTabs);
                    int srcIndex = positions.second;
                    int desIndex = positions.first;

                    // If only the desIndex is valid then the movedTab was already part of
                    // another group and is not present in the model. This happens only during
                    // an undo.
                    // Refresh just the desIndex tab card in the model. The removal of the
                    // movedTab from its previous group was already handled by
                    // didMoveTabOutOfGroup.
                    if (desIndex != TabModel.INVALID_TAB_INDEX
                            && srcIndex == TabModel.INVALID_TAB_INDEX) {
                        boolean isSelected = false;
                        for (Tab tab : relatedTabs) {
                            isSelected |= tab == TabModelUtils.getCurrentTab(tabModel);
                        }
                        Tab tab =
                                TabModelUtils.getTabById(
                                        tabModel,
                                        mModel.get(desIndex).model.get(TabProperties.TAB_ID));
                        updateTab(desIndex, PseudoTab.fromTab(tab), isSelected, false, false);
                        return;
                    }

                    if (!isValidMovePosition(srcIndex) || !isValidMovePosition(desIndex)) return;

                    Tab newSelectedTabInMergedGroup = null;
                    mModel.removeAt(srcIndex);
                    if (getRelatedTabsForId(movedTab.getId()).size() == 2) {
                        // When users use drop-to-merge to create a group.
                        RecordUserAction.record("TabGroup.Created.DropToMerge");
                    } else {
                        RecordUserAction.record("TabGrid.Drag.DropToMerge");
                    }
                    desIndex = srcIndex > desIndex ? desIndex : mModel.getTabIndexBefore(desIndex);
                    newSelectedTabInMergedGroup =
                            filter.getTabAt(mModel.getTabCardCountsBefore(desIndex));

                    boolean isSelected =
                            TabModelUtils.getCurrentTab(tabModel) == newSelectedTabInMergedGroup;
                    updateTab(
                            desIndex,
                            PseudoTab.fromTab(newSelectedTabInMergedGroup),
                            isSelected,
                            true,
                            false);
                }

                @Override
                public void didMoveTabGroup(
                        Tab movedTab, int tabModelOldIndex, int tabModelNewIndex) {
                    if (!mVisible
                            || !mActionsOnAllRelatedTabs
                            || tabModelNewIndex == tabModelOldIndex) {
                        return;
                    }
                    List<Tab> relatedTabs = getRelatedTabsForId(movedTab.getId());
                    TabModelFilter filter = mCurrentTabModelFilterSupplier.get();
                    Tab currentGroupSelectedTab =
                            TabGroupUtils.getSelectedTabInGroupForTab(
                                    (TabGroupModelFilter) filter, movedTab);
                    TabModel tabModel = filter.getTabModel();
                    int curPosition = mModel.indexFromId(currentGroupSelectedTab.getId());
                    if (curPosition == TabModel.INVALID_TAB_INDEX) {
                        // Sync TabListModel with updated TabGroupModelFilter.
                        int indexToUpdate =
                                mModel.indexOfNthTabCard(
                                        filter.indexOf(tabModel.getTabAt(tabModelOldIndex)));
                        mModel.updateTabListModelIdForGroup(currentGroupSelectedTab, indexToUpdate);
                        curPosition = mModel.indexFromId(currentGroupSelectedTab.getId());
                    }
                    if (!isValidMovePosition(curPosition)) return;

                    // Find the tab which was in the destination index before this move. Use
                    // that tab to figure out the new position.
                    int destinationTabIndex =
                            tabModelNewIndex > tabModelOldIndex
                                    ? tabModelNewIndex - relatedTabs.size()
                                    : tabModelNewIndex + 1;
                    Tab destinationTab = tabModel.getTabAt(destinationTabIndex);
                    Tab destinationGroupSelectedTab =
                            TabGroupUtils.getSelectedTabInGroupForTab(
                                    (TabGroupModelFilter) filter, destinationTab);
                    int newPosition = mModel.indexFromId(destinationGroupSelectedTab.getId());
                    if (newPosition == TabModel.INVALID_TAB_INDEX) {
                        int indexToUpdate =
                                mModel.indexOfNthTabCard(
                                        filter.indexOf(destinationTab)
                                                + (tabModelNewIndex > tabModelOldIndex ? 1 : -1));
                        mModel.updateTabListModelIdForGroup(
                                destinationGroupSelectedTab, indexToUpdate);
                        newPosition = mModel.indexFromId(destinationGroupSelectedTab.getId());
                    }
                    if (!isValidMovePosition(newPosition)) return;

                    mModel.move(curPosition, newPosition);
                }

                @Override
                public void didCreateGroup(
                        List<Tab> tabs,
                        List<Integer> tabOriginalIndex,
                        List<Integer> tabOriginalRootId,
                        String destinationGroupTitle) {}
            };

    /** Interface for implementing a {@link Runnable} that takes a tabId for a generic action. */
    public interface TabActionListener {
        void run(int tabId);
    }

    /**
     * Construct the Mediator with the given Models and observing hooks from the given
     * ChromeActivity.
     *
     * @param context The context used to get some configuration information.
     * @param model The Model to keep state about a list of {@link Tab}s.
     * @param mode The {@link TabListMode}
     * @param tabModelSelector {@link TabModelSelector} that will provide and receive signals about
     *     the tabs concerned.
     * @param regularTabModelSupplier The supplier of the regular {@link TabModel}.
     * @param thumbnailProvider {@link ThumbnailProvider} to provide screenshot related details.
     * @param titleProvider {@link PseudoTab.TitleProvider} for a given tab's title to show.
     * @param tabListFaviconProvider Provider for all favicon related drawables.
     * @param actionOnRelatedTabs Whether tab-related actions should be operated on all related
     *     tabs.
     * @param selectionDelegateProvider Provider for a {@link SelectionDelegate} that is used for a
     *     selectable list. It's null when selection is not possible.
     * @param gridCardOnClickListenerProvider Provides the onClickListener for opening dialog when
     *     click on a grid card.
     * @param dialogHandler A handler to handle requests about updating TabGridDialog.
     * @param priceWelcomeMessageControllerSupplier A supplier of a controller to show
     *     PriceWelcomeMessage.
     * @param componentName This is a unique string to identify different components.
     * @param uiType The type of UI this mediator should be building.
     */
    public TabListMediator(
            Context context,
            TabListModel model,
            @TabListMode int mode,
            @NonNull ObservableSupplier<TabModelFilter> tabModelFilterSupplier,
            @NonNull Supplier<TabModel> regularTabModelSupplier,
            @Nullable ThumbnailProvider thumbnailProvider,
            @Nullable PseudoTab.TitleProvider titleProvider,
            TabListFaviconProvider tabListFaviconProvider,
            boolean actionOnRelatedTabs,
            @Nullable SelectionDelegateProvider selectionDelegateProvider,
            @Nullable GridCardOnClickListenerProvider gridCardOnClickListenerProvider,
            @Nullable TabGridDialogHandler dialogHandler,
            @NonNull Supplier<PriceWelcomeMessageController> priceWelcomeMessageControllerSupplier,
            String componentName,
            @UiType int uiType) {
        mContext = context;
        mCurrentTabModelFilterSupplier = tabModelFilterSupplier;
        mRegularTabModelSupplier = regularTabModelSupplier;
        mThumbnailProvider = thumbnailProvider;
        mModel = model;
        mMode = mode;
        mTabListFaviconProvider = tabListFaviconProvider;
        mComponentName = componentName;
        mTitleProvider = titleProvider;
        mSelectionDelegateProvider = selectionDelegateProvider;
        mGridCardOnClickListenerProvider = gridCardOnClickListenerProvider;
        mTabGridDialogHandler = dialogHandler;
        mActionsOnAllRelatedTabs = actionOnRelatedTabs;
        mUiType = uiType;
        mPriceWelcomeMessageControllerSupplier = priceWelcomeMessageControllerSupplier;

        mTabModelObserver =
                new TabModelObserver() {
                    @Override
                    public void didSelectTab(Tab tab, int type, int lastId) {
                        mNextTabId = Tab.INVALID_TAB_ID;
                        if (tab.getId() == lastId) return;

                        int oldIndex = mModel.indexFromId(lastId);
                        int newIndex = mModel.indexFromId(tab.getId());
                        if (newIndex == TabModel.INVALID_TAB_INDEX
                                && mActionsOnAllRelatedTabs
                                && type == TabSelectionType.FROM_UNDO) {
                            // If a tab in tab group does not exist in model and needs to be
                            // selected from undo, identify the related TabIds and determine
                            // newIndex based on if any of the related ids are present in model.
                            newIndex = getIndexForTabWithRelatedTabs(tab);
                            if (newIndex != Tab.INVALID_TAB_ID) {
                                model.updateTabListModelIdForGroup(tab, newIndex);
                            }
                        }

                        mLastSelectedTabListModelIndex = oldIndex;
                        if (mTabToAddDelayed != null && mTabToAddDelayed == tab) {
                            // If tab is being added later, it will be selected later.
                            return;
                        }
                        selectTab(oldIndex, newIndex);
                    }

                    @Override
                    public void tabClosureUndone(Tab tab) {
                        onTabAdded(tab, !mActionsOnAllRelatedTabs);

                        if (sTabClosedFromMapTabClosedFromMap.containsKey(tab.getId())) {
                            @TabClosedFrom
                            int from = sTabClosedFromMapTabClosedFromMap.get(tab.getId());
                            switch (from) {
                                case TabClosedFrom.TAB_STRIP:
                                    RecordUserAction.record("TabStrip.UndoCloseTab");
                                    break;
                                case TabClosedFrom.GRID_TAB_SWITCHER:
                                    RecordUserAction.record("GridTabSwitch.UndoCloseTab");
                                    break;
                                case TabClosedFrom.GRID_TAB_SWITCHER_GROUP:
                                    RecordUserAction.record("GridTabSwitcher.UndoCloseTabGroup");
                                    break;
                                default:
                                    assert false
                                            : "tabClosureUndone for tab that closed from an unknown"
                                                    + " UI";
                            }
                            sTabClosedFromMapTabClosedFromMap.remove(tab.getId());
                        }
                        // TODO(yuezhanggg): clean up updateTab() calls in this class.
                        if (mActionsOnAllRelatedTabs) {
                            TabModelFilter filter = mCurrentTabModelFilterSupplier.get();
                            int filterIndex = filter.indexOf(tab);
                            if (filterIndex == TabList.INVALID_TAB_INDEX
                                    || getRelatedTabsForId(tab.getId()).size() == 1
                                    || filterIndex >= mModel.size()) {
                                return;
                            }
                            Tab currentGroupSelectedTab = filter.getTabAt(filterIndex);

                            int tabListModelIndex = mModel.indexOfNthTabCard(filterIndex);
                            assert mModel.indexFromId(currentGroupSelectedTab.getId())
                                    == tabListModelIndex;

                            updateTab(
                                    tabListModelIndex,
                                    PseudoTab.fromTab(currentGroupSelectedTab),
                                    mModel.get(tabListModelIndex)
                                            .model
                                            .get(TabProperties.IS_SELECTED),
                                    false,
                                    false);
                        }
                    }

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
                        // Check if we need to delay tab addition to model.
                        boolean delayAdd =
                                (type == TabLaunchType.FROM_TAB_SWITCHER_UI)
                                        && markedForSelection
                                        && TabSwitcherCoordinator.COMPONENT_NAME.equals(
                                                mComponentName);
                        if (delayAdd) {
                            mTabToAddDelayed = tab;
                            return;
                        }
                        onTabAdded(tab, !mActionsOnAllRelatedTabs);
                        if (type == TabLaunchType.FROM_RESTORE && mActionsOnAllRelatedTabs) {
                            // When tab is restored after restoring stage (e.g. exiting multi-window
                            // mode, switching between dark/light mode in incognito), we need to
                            // update related property models.
                            int filterIndex = filter.indexOf(tab);
                            if (filterIndex == TabList.INVALID_TAB_INDEX) return;
                            Tab currentGroupSelectedTab = filter.getTabAt(filterIndex);
                            // TabModel and TabListModel may be in the process of syncing up through
                            // restoring. Examples of this situation are switching between
                            // light/dark mode in incognito, exiting multi-window mode, etc.
                            int tabListModelIndex = mModel.indexOfNthTabCard(filterIndex);
                            if (mModel.indexFromId(currentGroupSelectedTab.getId())
                                    != tabListModelIndex) {
                                return;
                            }
                            updateTab(
                                    tabListModelIndex,
                                    PseudoTab.fromTab(currentGroupSelectedTab),
                                    mModel.get(tabListModelIndex)
                                            .model
                                            .get(TabProperties.IS_SELECTED),
                                    false,
                                    false);
                        }
                    }

                    @Override
                    public void willCloseTab(Tab tab, boolean animate, boolean didCloseAlone) {
                        if (mModel.indexFromId(tab.getId()) == TabModel.INVALID_TAB_INDEX) return;
                        tab.removeObserver(mTabObserver);
                        mModel.removeAt(mModel.indexFromId(tab.getId()));
                    }

                    @Override
                    public void tabRemoved(Tab tab) {
                        if (mModel.indexFromId(tab.getId()) == TabModel.INVALID_TAB_INDEX) return;
                        mModel.removeAt(mModel.indexFromId(tab.getId()));
                    }
                };

        // TODO(meiliang): follow up with unit tests to test the close signal is sent correctly with
        // the recommendedNextTab.
        mTabClosedListener =
                new TabActionListener() {
                    @Override
                    public void run(int tabId) {
                        // TODO(crbug.com/990698): Consider disabling all touch events during
                        // animation.
                        if (mModel.indexFromId(tabId) == TabModel.INVALID_TAB_INDEX) return;

                        TabModel tabModel = mCurrentTabModelFilterSupplier.get().getTabModel();
                        Tab closingTab = TabModelUtils.getTabById(tabModel, tabId);
                        if (closingTab == null) return;

                        RecordUserAction.record("MobileTabClosed." + mComponentName);

                        if (mActionsOnAllRelatedTabs) {
                            List<Tab> related = getRelatedTabsForId(tabId);
                            if (related.size() > 1) {
                                onGroupClosedFrom(tabId);
                                tabModel.closeMultipleTabs(related, true);
                                return;
                            }
                        }
                        onTabClosedFrom(tabId, mComponentName);

                        Tab currentTab = TabModelUtils.getCurrentTab(tabModel);
                        Tab nextTab = currentTab == closingTab ? getNextTab(tabId) : null;

                        tabModel.closeTab(closingTab, nextTab, false, false, true);
                    }

                    private Tab getNextTab(int closingTabId) {
                        int closingTabIndex = mModel.indexFromId(closingTabId);

                        if (closingTabIndex == TabModel.INVALID_TAB_INDEX) {
                            assert false;
                            return null;
                        }

                        int nextTabId = Tab.INVALID_TAB_ID;
                        if (mModel.size() > 1) {
                            int nextTabIndex =
                                    closingTabIndex == 0
                                            ? mModel.getTabIndexAfter(closingTabIndex)
                                            : mModel.getTabIndexBefore(closingTabIndex);
                            nextTabId =
                                    nextTabIndex == TabModel.INVALID_TAB_INDEX
                                            ? Tab.INVALID_TAB_ID
                                            : mModel.get(nextTabIndex)
                                                    .model
                                                    .get(TabProperties.TAB_ID);
                        }

                        return TabModelUtils.getTabById(
                                mCurrentTabModelFilterSupplier.get().getTabModel(), nextTabId);
                    }
                };

        TabActionListener swipeSafeTabActionListener =
                (id) -> {
                    // The DefaultItemAnimator is prone to crashing in combination with the swipe
                    // animation.
                    // Avoid this issue by disabling the default item animation for the duration of
                    // the tab removal. This is a framework issue. For more details see
                    // crbug/1319859.
                    mRecyclerViewItemAnimationToggle.setDisableItemAnimations(true);
                    mTabClosedListener.run(id);
                    // It is necessary to post the restoration as otherwise any animation triggered
                    // by removing the tab will still use the animator as they are also posted to
                    // the UI thread.
                    new Handler()
                            .post(
                                    () -> {
                                        mRecyclerViewItemAnimationToggle.setDisableItemAnimations(
                                                false);
                                    });
                };

        mTabGridItemTouchHelperCallback =
                new TabGridItemTouchHelperCallback(
                        context,
                        mModel,
                        mCurrentTabModelFilterSupplier,
                        swipeSafeTabActionListener,
                        mTabGridDialogHandler,
                        mComponentName,
                        mActionsOnAllRelatedTabs,
                        mMode);
    }

    /**
     * @param onLongPressTabItemEventListener to handle long press events on tabs.
     */
    public void setOnLongPressTabItemEventListener(
            @Nullable
                    TabGridItemTouchHelperCallback.OnLongPressTabItemEventListener
                            onLongPressTabItemEventListener) {
        mTabGridItemTouchHelperCallback.setOnLongPressTabItemEventListener(
                onLongPressTabItemEventListener);
    }

    void setRecyclerViewItemAnimationToggle(
            RecyclerViewItemAnimationToggle recyclerViewItemAnimationToggle) {
        mRecyclerViewItemAnimationToggle = recyclerViewItemAnimationToggle;
    }

    /**
     * @param size The default size to use for any new Tab cards.
     */
    void setDefaultGridCardSize(Size size) {
        mDefaultGridCardSize = size;
    }

    /**
     * @return The default size to use for any tab cards.
     */
    Size getDefaultGridCardSize() {
        return mDefaultGridCardSize;
    }

    private void selectTab(int oldIndex, int newIndex) {
        if (oldIndex != TabModel.INVALID_TAB_INDEX) {
            int lastId = mModel.get(oldIndex).model.get(TAB_ID);
            mModel.get(oldIndex).model.set(TabProperties.IS_SELECTED, false);
            if (mActionsOnAllRelatedTabs && mThumbnailProvider != null && mVisible) {
                mModel.get(oldIndex)
                        .model
                        .set(
                                TabProperties.THUMBNAIL_FETCHER,
                                new ThumbnailFetcher(mThumbnailProvider, lastId, true, false));
            }
        }

        if (newIndex != TabModel.INVALID_TAB_INDEX) {
            int newId = mModel.get(newIndex).model.get(TAB_ID);
            mModel.get(newIndex).model.set(TabProperties.IS_SELECTED, true);
            if (mThumbnailProvider != null && mVisible) {
                mModel.get(newIndex)
                        .model
                        .set(
                                TabProperties.THUMBNAIL_FETCHER,
                                new ThumbnailFetcher(mThumbnailProvider, newId, true, false));
            }
        }
    }

    public void initWithNative(Profile profile) {
        assert !profile.isOffTheRecord() : "Expecting a non-incognito profile.";
        mProfile = profile;
        mTabListFaviconProvider.initWithNative(profile);

        mOnTabModelFilterChanged.onResult(
                mCurrentTabModelFilterSupplier.addObserver(mOnTabModelFilterChanged));

        mTabGroupTitleEditor =
                new TabGroupTitleEditor(mContext, mCurrentTabModelFilterSupplier) {
                    @Override
                    protected void updateTabGroupTitle(Tab tab, String title) {
                        // Only update title in PropertyModel for tab switcher.
                        if (!mActionsOnAllRelatedTabs) return;
                        Tab currentGroupSelectedTab =
                                TabGroupUtils.getSelectedTabInGroupForTab(
                                        (TabGroupModelFilter) mCurrentTabModelFilterSupplier.get(),
                                        tab);
                        int index = mModel.indexFromId(currentGroupSelectedTab.getId());
                        if (index == TabModel.INVALID_TAB_INDEX) return;
                        mModel.get(index).model.set(TabProperties.TITLE, title);
                        updateDescriptionString(PseudoTab.fromTab(tab), mModel.get(index).model);
                        updateCloseButtonDescriptionString(
                                PseudoTab.fromTab(tab), mModel.get(index).model);
                    }

                    @Override
                    protected void deleteTabGroupTitle(int tabRootId) {
                        TabGroupTitleUtils.deleteTabGroupTitle(tabRootId);
                    }

                    @Override
                    protected String getTabGroupTitle(int tabRootId) {
                        return TabGroupTitleUtils.getTabGroupTitle(tabRootId);
                    }

                    @Override
                    protected void storeTabGroupTitle(int tabRootId, String title) {
                        TabGroupTitleUtils.storeTabGroupTitle(tabRootId, title);
                    }
                };

        // Right now we need to update layout only if there is a price welcome message card in tab
        // switcher.
        if (mMode == TabListMode.GRID
                && mUiType != UiType.SELECTABLE
                && PriceTrackingFeatures.isPriceTrackingEnabled(profile)) {
            mListObserver =
                    new ListObserver<Void>() {
                        @Override
                        public void onItemRangeInserted(
                                ListObservable source, int index, int count) {
                            updateLayout();
                        }

                        @Override
                        public void onItemRangeRemoved(
                                ListObservable source, int index, int count) {
                            updateLayout();
                        }

                        @Override
                        public void onItemRangeChanged(
                                ListObservable<Void> source,
                                int index,
                                int count,
                                @Nullable Void payload) {
                            updateLayout();
                        }

                        @Override
                        public void onItemMoved(ListObservable source, int curIndex, int newIndex) {
                            updateLayout();
                        }
                    };
            mModel.addObserver(mListObserver);
        }
    }

    private void onTabClosedFrom(int tabId, String fromComponent) {
        @TabClosedFrom int from;
        if (fromComponent.equals(TabGroupUiCoordinator.COMPONENT_NAME)) {
            from = TabClosedFrom.TAB_STRIP;
        } else if (fromComponent.equals(TabSwitcherCoordinator.COMPONENT_NAME)) {
            from = TabClosedFrom.GRID_TAB_SWITCHER;
        } else {
            Log.w(TAG, "Attempting to close tab from Unknown UI");
            return;
        }
        sTabClosedFromMapTabClosedFromMap.put(tabId, from);
    }

    private void onGroupClosedFrom(int tabId) {
        sTabClosedFromMapTabClosedFromMap.put(tabId, TabClosedFrom.GRID_TAB_SWITCHER_GROUP);
    }

    void setActionOnAllRelatedTabsForTesting(boolean actionOnAllRelatedTabs) {
        var oldValue = mActionsOnAllRelatedTabs;
        mActionsOnAllRelatedTabs = actionOnAllRelatedTabs;
        ResettersForTesting.register(() -> mActionsOnAllRelatedTabs = oldValue);
    }

    private List<Tab> getRelatedTabsForId(int id) {
        TabModelFilter filter = mCurrentTabModelFilterSupplier.get();
        return filter == null ? new ArrayList<>() : filter.getRelatedTabList(id);
    }

    private List<Integer> getRelatedTabsIds(int id) {
        TabModelFilter filter = mCurrentTabModelFilterSupplier.get();
        return filter == null ? new ArrayList<>() : filter.getRelatedTabIds(id);
    }

    private int getInsertionIndexOfTab(Tab tab, boolean onlyShowRelatedTabs) {
        int index = TabList.INVALID_TAB_INDEX;
        if (tab == null) return index;
        if (onlyShowRelatedTabs) {
            if (mModel.size() == 0) return TabList.INVALID_TAB_INDEX;
            List<Tab> related = getRelatedTabsForId(mModel.get(0).model.get(TabProperties.TAB_ID));
            index = related.indexOf(tab);
            if (index == -1) return TabList.INVALID_TAB_INDEX;
        } else {
            index =
                    mModel.indexOfNthTabCard(
                            TabModelUtils.getTabIndexById(
                                    mCurrentTabModelFilterSupplier.get(), tab.getId()));
            // TODO(wychen): the title (tab count in the group) is wrong when it's not the last
            //  tab added in the group.
        }
        return index;
    }

    private int onTabAdded(Tab tab, boolean onlyShowRelatedTabs) {
        int existingIndex = mModel.indexFromId(tab.getId());
        if (existingIndex != TabModel.INVALID_TAB_INDEX) return existingIndex;

        int newIndex = getInsertionIndexOfTab(tab, onlyShowRelatedTabs);
        if (newIndex == TabList.INVALID_TAB_INDEX) return newIndex;

        Tab currentTab =
                TabModelUtils.getCurrentTab(mCurrentTabModelFilterSupplier.get().getTabModel());
        addTabInfoToModel(PseudoTab.fromTab(tab), newIndex, currentTab == tab);
        return newIndex;
    }

    private boolean isValidMovePosition(int position) {
        return position != TabModel.INVALID_TAB_INDEX && position < mModel.size();
    }

    private boolean areTabsUnchanged(@Nullable List<PseudoTab> tabs) {
        int tabsCount = 0;
        for (int i = 0; i < mModel.size(); i++) {
            if (mModel.get(i).model.get(CARD_TYPE) == TAB) {
                tabsCount += 1;
            }
        }
        if (tabs == null) {
            return tabsCount == 0;
        }
        if (tabs.size() != tabsCount) return false;
        int tabsIndex = 0;
        for (int i = 0; i < mModel.size(); i++) {
            if (mModel.get(i).model.get(CARD_TYPE) == TAB
                    && mModel.get(i).model.get(TabProperties.TAB_ID)
                            != tabs.get(tabsIndex++).getId()) {
                return false;
            }
        }
        return true;
    }

    /**
     * Initialize the component with a list of tabs to show in a grid.
     *
     * @param tabs The list of tabs to be shown.
     * @param quickMode Whether to skip capturing the selected live tab for the thumbnail.
     * @return Whether the {@link TabListRecyclerView} can be shown quickly.
     */
    boolean resetWithListOfTabs(@Nullable List<PseudoTab> tabs, boolean quickMode) {
        List<PseudoTab> tabsList = tabs;
        mVisible = tabsList != null;
        if (tabs != null) {
            recordPriceAnnotationsEnabledMetrics();
        }
        TabModelFilter filter = mCurrentTabModelFilterSupplier.get();
        if (areTabsUnchanged(tabsList)) {
            if (tabsList == null) return true;
            for (int i = 0; i < tabsList.size(); i++) {
                PseudoTab tab = tabsList.get(i);
                int currentTabId = TabModelUtils.getCurrentTabId(filter.getTabModel());
                boolean isSelected = isSelectedTab(tab, currentTabId);
                updateTab(mModel.indexOfNthTabCard(i), tab, isSelected, false, quickMode);
            }
            return true;
        }
        mModel.set(new ArrayList<>());
        mLastSelectedTabListModelIndex = TabList.INVALID_TAB_INDEX;

        if (tabsList == null) {
            return true;
        }
        int currentTabId = TabModelUtils.getCurrentTabId(filter.getTabModel());

        for (int i = 0; i < tabsList.size(); i++) {
            PseudoTab tab = tabsList.get(i);
            addTabInfoToModel(tab, i, isSelectedTab(tab, currentTabId));
        }

        return false;
    }

    /**
     * Add the tab id of a {@Tab} that has been viewed to the sViewedTabIds set.
     * @param tabIndex  The tab index of a {@Tab} the user has viewed.
     */
    private void addViewedTabId(int tabIndex) {
        TabModel tabModel = mCurrentTabModelFilterSupplier.get().getTabModel();
        assert !tabModel.isIncognito();
        int tabId = mModel.get(tabIndex).model.get(TabProperties.TAB_ID);
        assert TabModelUtils.getTabById(tabModel, tabId) != null;
        sViewedTabIds.add(tabId);
    }

    void postHiding() {
        mVisible = false;
        unregisterOnScrolledListener();
        // if tab was marked for add later, add to model and mark as selected.
        if (mTabToAddDelayed != null) {
            int index = onTabAdded(mTabToAddDelayed, !mActionsOnAllRelatedTabs);
            selectTab(mLastSelectedTabListModelIndex, index);
            mTabToAddDelayed = null;
        }
    }

    private boolean isSelectedTab(PseudoTab tab, int tabModelSelectedTabId) {
        SelectionDelegate<Integer> selectionDelegate = getTabSelectionDelegate();
        if (selectionDelegate == null) {
            return tab.getId() == tabModelSelectedTabId;
        } else {
            return selectionDelegate.isItemSelected(tab.getId());
        }
    }

    /**
     * @see TabSwitcherMediator.ResetHandler#softCleanup
     */
    void softCleanup() {
        assert !mVisible;
        for (int i = 0; i < mModel.size(); i++) {
            if (mModel.get(i).model.get(CARD_TYPE) == TAB) {
                mModel.get(i).model.set(TabProperties.THUMBNAIL_FETCHER, null);
                mModel.get(i).model.set(TabProperties.FAVICON_FETCHER, null);
            }
        }
    }

    void hardCleanup() {
        assert !mVisible;
        if (mProfile != null
                && PriceTrackingUtilities.isTrackPricesOnTabsEnabled(mProfile)
                && (PriceTrackingFeatures.isPriceDropIphEnabled(mProfile)
                        || PriceTrackingFeatures.isPriceDropBadgeEnabled(mProfile))) {
            saveSeenPriceDrops();
        }
        sViewedTabIds.clear();
    }

    /**
     * While leaving the tab switcher grid this update whether a tab's current price drop has or has
     * not been seen.
     */
    private void saveSeenPriceDrops() {
        for (Integer tabId : sViewedTabIds) {
            Tab tab = TabModelUtils.getTabById(mRegularTabModelSupplier.get(), tabId);
            if (tab != null && isUngroupedTab(tab.getId())) {
                ShoppingPersistedTabData.from(
                        tab,
                        (sptd) -> {
                            if (sptd != null && sptd.getPriceDrop() != null) {
                                sptd.setIsCurrentPriceDropSeen(true);
                            }
                        });
            }
        }
    }

    private void updateTab(
            int index,
            PseudoTab pseudoTab,
            boolean isSelected,
            boolean isUpdatingId,
            boolean quickMode) {
        if (index < 0 || index >= mModel.size()) return;
        if (isUpdatingId) {
            mModel.get(index).model.set(TabProperties.TAB_ID, pseudoTab.getId());
        } else {
            assert mModel.get(index).model.get(TabProperties.TAB_ID) == pseudoTab.getId();
        }

        // TODO(wychen): refactor this.
        boolean isRealTab = pseudoTab.hasRealTab();
        TabActionListener tabSelectedListener;
        if (!isRealTab) {
            tabSelectedListener = null;
        } else if (mGridCardOnClickListenerProvider == null
                || getRelatedTabsForId(pseudoTab.getId()).size() == 1
                || !mActionsOnAllRelatedTabs) {
            tabSelectedListener = mTabSelectedListener;
        } else {
            tabSelectedListener =
                    mGridCardOnClickListenerProvider.openTabGridDialog(pseudoTab.getTab());

            if (tabSelectedListener == null) {
                tabSelectedListener = mTabSelectedListener;
            }
        }

        mModel.get(index).model.set(TabProperties.TAB_SELECTED_LISTENER, tabSelectedListener);
        mModel.get(index).model.set(TabProperties.IS_SELECTED, isSelected);
        mModel.get(index).model.set(TabProperties.SHOULD_SHOW_PRICE_DROP_TOOLTIP, false);
        mModel.get(index).model.set(TabProperties.TITLE, getLatestTitleForTab(pseudoTab));
        mModel.get(index)
                .model
                .set(TabProperties.TAB_CLOSED_LISTENER, isRealTab ? mTabClosedListener : null);
        updateDescriptionString(pseudoTab, mModel.get(index).model);
        updateCloseButtonDescriptionString(pseudoTab, mModel.get(index).model);
        if (isRealTab) {
            mModel.get(index)
                    .model
                    .set(TabProperties.URL_DOMAIN, getDomainForTab(pseudoTab.getTab()));
        }

        setupPersistedTabDataFetcherForTab(pseudoTab, index);

        updateFaviconForTab(pseudoTab, null, null);
        boolean forceUpdate = isSelected && !quickMode;
        boolean forceUpdateLastSelected =
                mActionsOnAllRelatedTabs && index == mLastSelectedTabListModelIndex && !quickMode;
        // TODO(crbug.com/1457653): Fetching thumbnail for group is expansive, we should consider to
        // improve it.
        boolean forceUpdateGroupTab =
                PseudoTab.getRelatedTabs(mContext, pseudoTab, mCurrentTabModelFilterSupplier.get())
                                .size()
                        > 1;
        if (mThumbnailProvider != null
                && mVisible
                && (mModel.get(index).model.get(TabProperties.THUMBNAIL_FETCHER) == null
                        || forceUpdate
                        || isUpdatingId
                        || forceUpdateLastSelected
                        || forceUpdateGroupTab)) {
            boolean isSelectable = mUiType == UiType.SELECTABLE;
            ThumbnailFetcher callback =
                    new ThumbnailFetcher(
                            mThumbnailProvider,
                            pseudoTab.getId(),
                            (forceUpdate || forceUpdateLastSelected) && !isSelectable,
                            forceUpdate
                                    && !TabUiFeatureUtilities.isTabToGtsAnimationEnabled(mContext)
                                    && !isSelectable);
            mModel.get(index).model.set(TabProperties.THUMBNAIL_FETCHER, callback);
        }
    }

    @VisibleForTesting
    public boolean isUngroupedTab(int tabId) {
        return getRelatedTabsForId(tabId).size() == 1;
    }

    public Set<Integer> getViewedTabIdsForTesting() {
        return sViewedTabIds;
    }

    /**
     * @return The callback that hosts the logic for swipe and drag related actions.
     */
    ItemTouchHelper.SimpleCallback getItemTouchHelperCallback(
            final float swipeToDismissThreshold,
            final float mergeThreshold,
            final float ungroupThreshold) {
        mTabGridItemTouchHelperCallback.setupCallback(
                swipeToDismissThreshold, mergeThreshold, ungroupThreshold);
        return mTabGridItemTouchHelperCallback;
    }

    void registerOrientationListener(GridLayoutManager manager) {
        mComponentCallbacks =
                new ComponentCallbacks() {
                    @Override
                    public void onConfigurationChanged(Configuration newConfig) {
                        updateSpanCount(manager, newConfig.screenWidthDp);
                        if (mMode == TabListMode.GRID && mUiType != UiType.SELECTABLE) {
                            updateLayout();
                        }
                    }

                    @Override
                    public void onLowMemory() {}
                };
        mContext.registerComponentCallbacks(mComponentCallbacks);
        mGridLayoutManager = manager;
    }

    /**
     * Update the grid layout span count and span size lookup base on orientation.
     * @param manager     The {@link GridLayoutManager} used to update the span count.
     * @param screenWidthDp The screnWidth based on which we update the span count.
     * @return whether the span count changed.
     */
    boolean updateSpanCount(GridLayoutManager manager, int screenWidthDp) {
        final int oldSpanCount = manager.getSpanCount();
        final int newSpanCount = getSpanCount(screenWidthDp);
        manager.setSpanCount(newSpanCount);
        manager.setSpanSizeLookup(
                new GridLayoutManager.SpanSizeLookup() {
                    @Override
                    public int getSpanSize(int position) {
                        int itemType = mModel.get(position).type;

                        if (itemType == TabProperties.UiType.MESSAGE
                                || itemType == TabProperties.UiType.LARGE_MESSAGE
                                || itemType == UiType.DIVIDER
                                || itemType == TabProperties.UiType.CUSTOM_MESSAGE) {
                            return manager.getSpanCount();
                        }
                        return 1;
                    }
                });
        return oldSpanCount != newSpanCount;
    }

    /**
     * Adds an on scroll listener to {@link TabListRecyclerView} that determines whether a tab
     * thumbnail is within view after a scroll is completed.
     * @param recyclerView the {@link TabListRecyclerView} to add the listener too.
     */
    void registerOnScrolledListener(RecyclerView recyclerView) {
        // For InstantStart, this can be called before native is initialized, so ensure the Profile
        // is available before proceeding.
        if (mProfile == null) return;

        if (PriceTrackingUtilities.isTrackPricesOnTabsEnabled(mProfile)
                && (PriceTrackingFeatures.isPriceDropIphEnabled(mProfile)
                        || PriceTrackingFeatures.isPriceDropBadgeEnabled(mProfile))) {
            mRecyclerView = recyclerView;
            mOnScrollListener =
                    new OnScrollListener() {
                        @Override
                        public void onScrolled(@NonNull RecyclerView recyclerView, int dx, int dy) {
                            if (!mCurrentTabModelFilterSupplier.get().getTabModel().isIncognito()) {
                                for (int i = 0; i < mRecyclerView.getChildCount(); i++) {
                                    if (mRecyclerView
                                            .getLayoutManager()
                                            .isViewPartiallyVisible(
                                                    mRecyclerView.getChildAt(i), false, true)) {
                                        addViewedTabId(i);
                                    }
                                }
                            }
                        }
                    };
            mRecyclerView.addOnScrollListener(mOnScrollListener);
        }
    }

    private void unregisterOnScrolledListener() {
        if (mRecyclerView != null && mOnScrollListener != null) {
            mRecyclerView.removeOnScrollListener(mOnScrollListener);
            mOnScrollListener = null;
        }
    }

    /**
     * Span count is computed based on screen width for tablets and orientation for phones.
     * When in multi-window mode on phone, the span count is fixed to 2 to keep tab card size
     * reasonable.
     */
    private int getSpanCount(int screenWidthDp) {
        if (DeviceFormFactor.isNonMultiDisplayContextOnTablet(mContext)) {
            return screenWidthDp < TabListCoordinator.MAX_SCREEN_WIDTH_COMPACT_DP
                    ? TabListCoordinator.GRID_LAYOUT_SPAN_COUNT_COMPACT
                    : screenWidthDp < TabListCoordinator.MAX_SCREEN_WIDTH_MEDIUM_DP
                            ? TabListCoordinator.GRID_LAYOUT_SPAN_COUNT_MEDIUM
                            : TabListCoordinator.GRID_LAYOUT_SPAN_COUNT_LARGE;
        }
        return screenWidthDp < TabListCoordinator.MAX_SCREEN_WIDTH_COMPACT_DP
                ? TabListCoordinator.GRID_LAYOUT_SPAN_COUNT_COMPACT
                : TabListCoordinator.GRID_LAYOUT_SPAN_COUNT_MEDIUM;
    }

    /**
     * Setup the {@link View.AccessibilityDelegate} for grid layout.
     * @param helper The {@link TabGridAccessibilityHelper} used to setup accessibility support.
     */
    void setupAccessibilityDelegate(TabGridAccessibilityHelper helper) {
        mAccessibilityDelegate =
                new View.AccessibilityDelegate() {
                    @Override
                    public void onInitializeAccessibilityNodeInfo(
                            View host, AccessibilityNodeInfo info) {
                        super.onInitializeAccessibilityNodeInfo(host, info);
                        for (AccessibilityAction action : helper.getPotentialActionsForView(host)) {
                            info.addAction(action);
                        }
                    }

                    @Override
                    public boolean performAccessibilityAction(View host, int action, Bundle args) {
                        if (!helper.isReorderAction(action)) {
                            return super.performAccessibilityAction(host, action, args);
                        }

                        Pair<Integer, Integer> positions =
                                helper.getPositionsOfReorderAction(host, action);
                        int currentPosition = positions.first;
                        int targetPosition = positions.second;
                        if (!isValidMovePosition(currentPosition)
                                || !isValidMovePosition(targetPosition)) {
                            return false;
                        }
                        mModel.move(currentPosition, targetPosition);
                        RecordUserAction.record("TabGrid.AccessibilityDelegate.Reordered");
                        return true;
                    }
                };
    }

    /**
     * Exposes a {@link TabGroupTitleEditor} to modify the title of a tab group.
     * @return The {@link TabGroupTitleEditor} used to modify the title of a tab group.
     */
    @Nullable
    TabGroupTitleEditor getTabGroupTitleEditor() {
        return mTabGroupTitleEditor;
    }

    /** Destroy any members that needs clean up. */
    public void destroy() {
        if (mListObserver != null) {
            mModel.removeObserver(mListObserver);
        }
        removeTabModelFilterObservers(mCurrentTabModelFilterSupplier.get());
        mCurrentTabModelFilterSupplier.removeObserver(mOnTabModelFilterChanged);

        if (mComponentCallbacks != null) {
            mContext.unregisterComponentCallbacks(mComponentCallbacks);
        }
        if (mTabGroupTitleEditor != null) {
            mTabGroupTitleEditor.destroy();
        }
        unregisterOnScrolledListener();
    }

    private void addTabInfoToModel(final PseudoTab pseudoTab, int index, boolean isSelected) {
        assert index != TabModel.INVALID_TAB_INDEX;
        boolean showIPH = false;
        boolean isRealTab = pseudoTab.hasRealTab();
        if (mActionsOnAllRelatedTabs && !mShownIPH && isRealTab) {
            showIPH = getRelatedTabsForId(pseudoTab.getId()).size() > 1;
        }
        TabActionListener tabSelectedListener;
        if (!isRealTab) {
            tabSelectedListener = null;
        } else if (mGridCardOnClickListenerProvider == null
                || getRelatedTabsForId(pseudoTab.getId()).size() == 1
                || !mActionsOnAllRelatedTabs) {
            tabSelectedListener = mTabSelectedListener;
        } else {
            tabSelectedListener =
                    mGridCardOnClickListenerProvider.openTabGridDialog(pseudoTab.getTab());
            if (tabSelectedListener == null) {
                tabSelectedListener = mTabSelectedListener;
            }
        }

        int selectedTabBackgroundDrawableId =
                pseudoTab.isIncognito()
                        ? R.drawable.selected_tab_background_incognito
                        : R.drawable.selected_tab_background;

        int tabstripFaviconBackgroundDrawableId =
                pseudoTab.isIncognito()
                        ? R.color.favicon_background_color_incognito
                        : R.color.favicon_background_color;
        PropertyModel tabInfo =
                new PropertyModel.Builder(TabProperties.ALL_KEYS_TAB_GRID)
                        .with(TabProperties.TAB_ID, pseudoTab.getId())
                        .with(TabProperties.TITLE, getLatestTitleForTab(pseudoTab))
                        .with(
                                TabProperties.URL_DOMAIN,
                                isRealTab ? getDomainForTab(pseudoTab.getTab()) : null)
                        .with(TabProperties.FAVICON_FETCHER, null)
                        .with(TabProperties.FAVICON_FETCHED, false)
                        .with(TabProperties.IS_SELECTED, isSelected)
                        .with(TabProperties.IPH_PROVIDER, showIPH ? mIphProvider : null)
                        .with(CARD_ALPHA, 1f)
                        .with(
                                TabProperties.CARD_ANIMATION_STATUS,
                                ClosableTabGridView.AnimationStatus.CARD_RESTORE)
                        .with(
                                TabProperties.TAB_SELECTION_DELEGATE,
                                isRealTab ? getTabSelectionDelegate() : null)
                        .with(TabProperties.IS_INCOGNITO, pseudoTab.isIncognito())
                        .with(
                                TabProperties.SELECTED_TAB_BACKGROUND_DRAWABLE_ID,
                                selectedTabBackgroundDrawableId)
                        .with(
                                TabProperties.TABSTRIP_FAVICON_BACKGROUND_COLOR_ID,
                                tabstripFaviconBackgroundDrawableId)
                        .with(TabProperties.ACCESSIBILITY_DELEGATE, mAccessibilityDelegate)
                        .with(TabProperties.SHOULD_SHOW_PRICE_DROP_TOOLTIP, false)
                        .with(CARD_TYPE, TAB)
                        .build();

        tabInfo.set(
                TabProperties.FAVICON_FETCHER,
                mTabListFaviconProvider.getDefaultFaviconFetcher(pseudoTab.isIncognito()));

        if (mUiType == UiType.SELECTABLE) {
            // Incognito in both light/dark theme is the same as non-incognito mode in dark theme.
            // Non-incognito mode and incognito in both light/dark themes in dark theme all look
            // dark.
            ColorStateList checkedDrawableColorList =
                    ColorStateList.valueOf(
                            pseudoTab.isIncognito()
                                    ? mContext.getColor(R.color.default_icon_color_dark)
                                    : SemanticColorUtils.getDefaultIconColorInverse(mContext));
            ColorStateList actionButtonBackgroundColorList =
                    AppCompatResources.getColorStateList(
                            mContext,
                            pseudoTab.isIncognito()
                                    ? R.color.default_icon_color_light
                                    : R.color.default_icon_color_tint_list);
            // TODO(995876): Update color modern_blue_300 to active_color_dark when the associated
            // bug is landed.
            ColorStateList actionbuttonSelectedBackgroundColorList =
                    ColorStateList.valueOf(
                            pseudoTab.isIncognito()
                                    ? mContext.getColor(R.color.modern_blue_300)
                                    : SemanticColorUtils.getDefaultControlColorActive(mContext));

            tabInfo.set(TabProperties.CHECKED_DRAWABLE_STATE_LIST, checkedDrawableColorList);
            tabInfo.set(
                    TabProperties.SELECTABLE_TAB_ACTION_BUTTON_BACKGROUND,
                    actionButtonBackgroundColorList);
            tabInfo.set(
                    TabProperties.SELECTABLE_TAB_ACTION_BUTTON_SELECTED_BACKGROUND,
                    actionbuttonSelectedBackgroundColorList);
            tabInfo.set(
                    TabProperties.SELECTABLE_TAB_CLICKED_LISTENER, mSelectableTabOnClickListener);
        } else {
            tabInfo.set(TabProperties.TAB_SELECTED_LISTENER, tabSelectedListener);
            tabInfo.set(TabProperties.TAB_CLOSED_LISTENER, isRealTab ? mTabClosedListener : null);
            updateDescriptionString(pseudoTab, tabInfo);
            updateCloseButtonDescriptionString(pseudoTab, tabInfo);
        }

        if (index >= mModel.size()) {
            mModel.add(new SimpleRecyclerViewAdapter.ListItem(mUiType, tabInfo));
        } else {
            mModel.add(index, new SimpleRecyclerViewAdapter.ListItem(mUiType, tabInfo));
        }

        setupPersistedTabDataFetcherForTab(pseudoTab, index);

        updateFaviconForTab(pseudoTab, null, null);

        if (mThumbnailProvider != null && mDefaultGridCardSize != null) {
            if (!mDefaultGridCardSize.equals(tabInfo.get(TabProperties.GRID_CARD_SIZE))) {
                tabInfo.set(
                        TabProperties.GRID_CARD_SIZE,
                        new Size(
                                mDefaultGridCardSize.getWidth(), mDefaultGridCardSize.getHeight()));
            }
        }
        if (mThumbnailProvider != null && mVisible) {
            boolean isSelectable = mUiType == UiType.SELECTABLE;
            ThumbnailFetcher callback =
                    new ThumbnailFetcher(
                            mThumbnailProvider,
                            pseudoTab.getId(),
                            isSelected && !isSelectable,
                            isSelected
                                    && !TabUiFeatureUtilities.isTabToGtsAnimationEnabled(mContext)
                                    && !isSelectable);
            tabInfo.set(TabProperties.THUMBNAIL_FETCHER, callback);
        }
        if (pseudoTab.getTab() != null) pseudoTab.getTab().addObserver(mTabObserver);
    }

    // TODO(wychen): make this work with PseudoTab.
    private String getDomainForTab(Tab tab) {
        if (!mActionsOnAllRelatedTabs) return getDomain(tab);

        List<Tab> relatedTabs = getRelatedTabsForId(tab.getId());

        List<String> domainNames = new ArrayList<>();

        for (int i = 0; i < relatedTabs.size(); i++) {
            String domain = getDomain(relatedTabs.get(i));
            domainNames.add(domain);
        }
        // TODO(1024925): Address i18n issue for the list delimiter.
        return TextUtils.join(", ", domainNames);
    }

    private void updateDescriptionString(PseudoTab pseudoTab, PropertyModel model) {
        if (!mActionsOnAllRelatedTabs) return;
        int numOfRelatedTabs = getRelatedTabsForId(pseudoTab.getId()).size();
        if (numOfRelatedTabs > 1) {
            String title = getLatestTitleForTab(pseudoTab);
            title = title.equals(pseudoTab.getTitle(mContext, mTitleProvider)) ? "" : title;
            model.set(
                    TabProperties.CONTENT_DESCRIPTION_STRING,
                    title.isEmpty()
                            ? mContext.getString(
                                    R.string.accessibility_expand_tab_group,
                                    String.valueOf(numOfRelatedTabs))
                            : mContext.getString(
                                    R.string.accessibility_expand_tab_group_with_group_name,
                                    title,
                                    String.valueOf(numOfRelatedTabs)));
        } else {
            model.set(TabProperties.CONTENT_DESCRIPTION_STRING, null);
        }
    }

    private void updateCloseButtonDescriptionString(PseudoTab pseudoTab, PropertyModel model) {
        if (mActionsOnAllRelatedTabs) {
            int numOfRelatedTabs = getRelatedTabsForId(pseudoTab.getId()).size();
            if (numOfRelatedTabs > 1) {
                String title = getLatestTitleForTab(pseudoTab);
                title = title.equals(pseudoTab.getTitle(mContext, mTitleProvider)) ? "" : title;

                if (title.isEmpty()) {
                    model.set(
                            TabProperties.CLOSE_BUTTON_DESCRIPTION_STRING,
                            mContext.getString(
                                    R.string.accessibility_close_tab_group_button,
                                    String.valueOf(numOfRelatedTabs)));
                } else {
                    model.set(
                            TabProperties.CLOSE_BUTTON_DESCRIPTION_STRING,
                            mContext.getString(
                                    R.string.accessibility_close_tab_group_button_with_group_name,
                                    title,
                                    String.valueOf(numOfRelatedTabs)));
                }
                return;
            }
        }

        model.set(
                CLOSE_BUTTON_DESCRIPTION_STRING,
                mContext.getString(
                        R.string.accessibility_tabstrip_btn_close_tab, pseudoTab.getTitle()));
    }

    @VisibleForTesting
    protected static String getDomain(Tab tab) {
        // TODO(crbug.com/1116613) Investigate how uninitialized Tabs are appearing
        // here.
        assert tab.isInitialized();
        if (!tab.isInitialized()) {
            return "";
        }

        String spec = tab.getUrl().getSpec();
        if (spec == null) return "";

        // TODO(crbug/783819): convert UrlUtilities to GURL
        String domain = UrlUtilities.getDomainAndRegistry(spec, false);

        if (domain == null || domain.isEmpty()) return spec;
        return domain;
    }

    @Nullable
    private SelectionDelegate<Integer> getTabSelectionDelegate() {
        return mSelectionDelegateProvider == null
                ? null
                : mSelectionDelegateProvider.getSelectionDelegate();
    }

    @VisibleForTesting
    String getLatestTitleForTab(PseudoTab pseudoTab) {
        String originalTitle = pseudoTab.getTitle(mContext, mTitleProvider);
        if (!mActionsOnAllRelatedTabs || mTabGroupTitleEditor == null) return originalTitle;
        // If the group degrades to a single tab, delete the stored title.
        if (getRelatedTabsForId(pseudoTab.getId()).size() <= 1) {
            return originalTitle;
        }
        String storedTitle = mTabGroupTitleEditor.getTabGroupTitle(pseudoTab.getRootId());
        return storedTitle == null ? originalTitle : storedTitle;
    }

    int selectedTabId() {
        if (mNextTabId != Tab.INVALID_TAB_ID) {
            return mNextTabId;
        }

        return TabModelUtils.getCurrentTabId(mCurrentTabModelFilterSupplier.get().getTabModel());
    }

    private void setupPersistedTabDataFetcherForTab(PseudoTab pseudoTab, int index) {
        if (mMode == TabListMode.GRID && pseudoTab.hasRealTab() && !pseudoTab.isIncognito()) {
            assert mProfile != null;
            if (PriceTrackingUtilities.isTrackPricesOnTabsEnabled(mProfile)
                    && isUngroupedTab(pseudoTab.getId())) {
                mModel.get(index)
                        .model
                        .set(
                                TabProperties.SHOPPING_PERSISTED_TAB_DATA_FETCHER,
                                new ShoppingPersistedTabDataFetcher(
                                        pseudoTab.getTab(),
                                        mPriceWelcomeMessageControllerSupplier));
            } else {
                mModel.get(index)
                        .model
                        .set(TabProperties.SHOPPING_PERSISTED_TAB_DATA_FETCHER, null);
            }
        } else {
            mModel.get(index).model.set(TabProperties.SHOPPING_PERSISTED_TAB_DATA_FETCHER, null);
        }
    }

    @VisibleForTesting
    void updateFaviconForTab(PseudoTab pseudoTab, @Nullable Bitmap icon, @Nullable GURL iconUrl) {
        int modelIndex = mModel.indexFromId(pseudoTab.getId());
        if (modelIndex == Tab.INVALID_TAB_ID) return;
        List<Tab> relatedTabList = getRelatedTabsForId(pseudoTab.getId());

        if (mActionsOnAllRelatedTabs && relatedTabList.size() > 1) {
            if (mMode != TabListMode.LIST) {
                // For tab group card in grid tab switcher, the favicon is set to be null.
                mModel.get(modelIndex).model.set(TabProperties.FAVICON_FETCHER, null);
                return;
            }

            // The order of the url list matches the multi-thumbnail.
            List<GURL> urls = new ArrayList<>();
            urls.add(pseudoTab.getUrl());
            for (int i = 0; urls.size() < 4 && i < relatedTabList.size(); i++) {
                if (pseudoTab.getId() == relatedTabList.get(i).getId()) continue;
                urls.add(relatedTabList.get(i).getUrl());
            }

            // For tab group card in list tab switcher, the favicon is the composed favicon.
            mModel.get(modelIndex)
                    .model
                    .set(
                            TabProperties.FAVICON_FETCHER,
                            mTabListFaviconProvider.getComposedFaviconImageFetcher(
                                    urls, pseudoTab.isIncognito()));
            return;
        }
        if (!mTabListFaviconProvider.isInitialized()) {
            return;
        }

        // If there is an available icon, we fetch favicon synchronously; otherwise asynchronously.
        if (icon != null && iconUrl != null) {
            mModel.get(modelIndex)
                    .model
                    .set(
                            TabProperties.FAVICON_FETCHER,
                            mTabListFaviconProvider.getFaviconFromBitmapFetcher(icon, iconUrl));
            return;
        }

        TabFaviconFetcher fetcher =
                mTabListFaviconProvider.getFaviconForUrlFetcher(
                        pseudoTab.getUrl(), pseudoTab.isIncognito());
        mModel.get(modelIndex).model.set(TabProperties.FAVICON_FETCHER, fetcher);
    }

    /**
     * Inserts a special {@link org.chromium.ui.modelutil.MVCListAdapter.ListItem} at given index of
     * the current {@link TabListModel}.
     *
     * @param index The index of the {@link org.chromium.ui.modelutil.MVCListAdapter.ListItem} to be
     *              inserted.
     * @param uiType The view type the model will bind to.
     * @param model The model that will be bound to a view.
     */
    void addSpecialItemToModel(int index, @UiType int uiType, PropertyModel model) {
        mModel.add(index, new SimpleRecyclerViewAdapter.ListItem(uiType, model));
    }

    /**
     * Removes a special {@link org.chromium.ui.modelutil.MVCListAdapter.ListItem} that has the
     * given {@code uiType} and/or its {@link PropertyModel} has the given {@code itemIdentifier}
     * from the current {@link TabListModel}.
     *
     * @param uiType The uiType to match.
     * @param itemIdentifier The itemIdentifier to match. This can be obsoleted if the {@link
     *     org.chromium.ui.modelutil.MVCListAdapter.ListItem} does not need additional identifier.
     */
    void removeSpecialItemFromModel(
            @UiType int uiType, @MessageService.MessageType int itemIdentifier) {
        int index = TabModel.INVALID_TAB_INDEX;
        if (uiType == UiType.MESSAGE || uiType == UiType.LARGE_MESSAGE) {
            if (itemIdentifier == MessageService.MessageType.ALL) {
                while (mModel.lastIndexForMessageItem() != TabModel.INVALID_TAB_INDEX) {
                    index = mModel.lastIndexForMessageItem();
                    mModel.removeAt(index);
                }
                return;
            }
            index = mModel.lastIndexForMessageItemFromType(itemIdentifier);
        }

        if (index == TabModel.INVALID_TAB_INDEX) return;

        assert validateItemAt(index, uiType, itemIdentifier);
        mModel.removeAt(index);
    }

    private boolean validateItemAt(
            int index, @UiType int uiType, @MessageService.MessageType int itemIdentifier) {
        if (uiType == UiType.MESSAGE || uiType == UiType.LARGE_MESSAGE) {
            return mModel.get(index).type == uiType
                    && mModel.get(index).model.get(MESSAGE_TYPE) == itemIdentifier;
        }

        return false;
    }

    /**
     * The PriceWelcomeMessage should be in view when user enters the tab switcher, so we put it
     * exactly below the currently selected tab.
     *
     * @return Where the PriceWelcomeMessage should be inserted in the {@link TabListModel} when
     *         user enters the tab switcher.
     */
    int getPriceWelcomeMessageInsertionIndex() {
        assert mGridLayoutManager != null;
        int spanCount = mGridLayoutManager.getSpanCount();
        int selectedTabIndex =
                mModel.indexOfNthTabCard(mCurrentTabModelFilterSupplier.get().index());
        int indexBelowSelectedTab = (selectedTabIndex / spanCount + 1) * spanCount;
        int indexAfterLastTab = mModel.getTabIndexBefore(mModel.size()) + 1;
        return Math.min(indexBelowSelectedTab, indexAfterLastTab);
    }

    /**
     * Update the layout of tab switcher to make it compact. Because now we have messages within the
     * tabs like PriceMessage and these messages take up the entire row, some operations like
     * closing a tab above the message card will leave a blank grid, so we need to update the
     * layout.
     */
    @VisibleForTesting
    void updateLayout() {
        // Right now we need to update layout only if there is a price welcome message card in tab
        // switcher.
        if (mProfile == null
                || !PriceTrackingUtilities.isPriceWelcomeMessageCardEnabled(mProfile)) {
            return;
        }
        assert mGridLayoutManager != null;
        int spanCount = mGridLayoutManager.getSpanCount();
        GridLayoutManager.SpanSizeLookup spanSizeLookup = mGridLayoutManager.getSpanSizeLookup();
        int spanSizeSumForCurrentRow = 0;
        int index = 0;
        for (; index < mModel.size(); index++) {
            spanSizeSumForCurrentRow += spanSizeLookup.getSpanSize(index);
            if (spanSizeSumForCurrentRow == spanCount) {
                // This row is compact, we clear and recount the spanSize for next row.
                spanSizeSumForCurrentRow = 0;
            } else if (spanSizeSumForCurrentRow > spanCount) {
                // Find a blank grid and break.
                if (mModel.get(index).type == TabProperties.UiType.LARGE_MESSAGE) break;
                spanSizeSumForCurrentRow = 0;
            }
        }
        if (spanSizeSumForCurrentRow <= spanCount) return;
        int blankSize = spanCount - (spanSizeSumForCurrentRow - spanSizeLookup.getSpanSize(index));
        for (int i = index + 1; i < mModel.size(); i++) {
            if (spanSizeLookup.getSpanSize(i) > blankSize) continue;
            mModel.move(i, index);
            // We should return after one move because once item moved, updateLayout() will be
            // called again.
            return;
        }
    }

    View.AccessibilityDelegate getAccessibilityDelegateForTesting() {
        return mAccessibilityDelegate;
    }

    @VisibleForTesting
    void recordPriceAnnotationsEnabledMetrics() {
        if (mMode != TabListMode.GRID
                || !mActionsOnAllRelatedTabs
                || mProfile == null
                || !PriceTrackingFeatures.isPriceTrackingEligible(mProfile)) {
            return;
        }
        SharedPreferencesManager preferencesManager = ChromeSharedPreferences.getInstance();
        if (System.currentTimeMillis()
                        - preferencesManager.readLong(
                                ChromePreferenceKeys
                                        .PRICE_TRACKING_ANNOTATIONS_ENABLED_METRICS_TIMESTAMP,
                                -1)
                >= PriceTrackingFeatures.getAnnotationsEnabledMetricsWindowDurationMilliSeconds()) {
            RecordHistogram.recordBooleanHistogram(
                    "Commerce.PriceDrop.AnnotationsEnabled",
                    PriceTrackingUtilities.isTrackPricesOnTabsEnabled(mProfile));
            preferencesManager.writeLong(
                    ChromePreferenceKeys.PRICE_TRACKING_ANNOTATIONS_ENABLED_METRICS_TIMESTAMP,
                    System.currentTimeMillis());
        }
    }

    /**
     * @param tab the {@link Tab} to find the group index of.
     * @return the index for the tab group within {@link mModel}
     */
    int getIndexForTabWithRelatedTabs(Tab tab) {
        List<Integer> relatedTabIds = getRelatedTabsIds(tab.getId());
        if (!relatedTabIds.isEmpty()) {
            for (int i = 0; i < mModel.size(); i++) {
                int modelTabId = mModel.get(i).model.get(TAB_ID);
                if (relatedTabIds.contains(modelTabId)) {
                    return i;
                }
            }
        }
        return TabModel.INVALID_TAB_INDEX;
    }

    Tab getTabToAddDelayedForTesting() {
        return mTabToAddDelayed;
    }

    void setComponentNameForTesting(String name) {
        var oldValue = mComponentName;
        mComponentName = name;
        ResettersForTesting.register(() -> mComponentName = oldValue);
    }

    private void onTabModelFilterChanged(
            @Nullable TabModelFilter newFilter, @Nullable TabModelFilter oldFilter) {
        removeTabModelFilterObservers(oldFilter);

        if (newFilter != null) {
            newFilter.addObserver(mTabModelObserver);
            if (newFilter instanceof TabGroupModelFilter newGroupFilter) {
                newGroupFilter.addTabGroupObserver(mTabGroupObserver);
            }
        }
    }

    private void removeTabModelFilterObservers(@Nullable TabModelFilter filter) {
        if (filter == null) return;

        TabModel tabModel = filter.getTabModel();
        if (tabModel != null) {
            // Observers are added when tabs are shown via addTabInfoToModel(). When switching
            // filters the TabObservers should be removed from all the tabs in the previous model.
            // If no observer was added this will no-op. Previously this was only done in
            // destroy(), but that left observers behind on the inactive model.
            for (int i = 0; i < tabModel.getCount(); i++) {
                tabModel.getTabAt(i).removeObserver(mTabObserver);
            }
        }
        filter.removeObserver(mTabModelObserver);
        if (filter instanceof TabGroupModelFilter groupFilter) {
            groupFilter.removeTabGroupObserver(mTabGroupObserver);
        }
    }

    /**
     * @param itemIdentifier The itemIdentifier to match.
     * @return whether a special {@link org.chromium.ui.modelutil.MVCListAdapter.ListItem} with the
     *     given {@code itemIdentifier} for its {@link PropertyModel} exists in the current {@link
     *     TabListModel}.
     */
    boolean specialItemExistsInModel(@MessageService.MessageType int itemIdentifier) {
        if (itemIdentifier == MessageService.MessageType.ALL) {
            return mModel.lastIndexForMessageItem() != TabModel.INVALID_TAB_INDEX;
        }
        return mModel.lastIndexForMessageItemFromType(itemIdentifier) != TabModel.INVALID_TAB_INDEX;
    }
}
