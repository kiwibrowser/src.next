// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.tasks.tab_management;

import android.app.Activity;
import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.Rect;
import android.os.SystemClock;
import android.view.View;
import android.view.ViewGroup;
import android.view.accessibility.AccessibilityEvent;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.annotation.VisibleForTesting;
import androidx.recyclerview.widget.RecyclerView.ViewHolder;

import org.chromium.base.Callback;
import org.chromium.base.TraceEvent;
import org.chromium.base.metrics.RecordHistogram;
import org.chromium.base.metrics.RecordUserAction;
import org.chromium.base.supplier.Supplier;
import org.chromium.chrome.browser.browser_controls.BrowserControlsStateProvider;
import org.chromium.chrome.browser.compositor.layouts.content.TabContentManager;
import org.chromium.chrome.browser.flags.ChromeFeatureList;
import org.chromium.chrome.browser.incognito.reauth.IncognitoReauthManager;
import org.chromium.chrome.browser.lifecycle.ActivityLifecycleDispatcher;
import org.chromium.chrome.browser.lifecycle.DestroyObserver;
import org.chromium.chrome.browser.multiwindow.MultiWindowModeStateDispatcher;
import org.chromium.chrome.browser.preferences.SharedPreferencesManager;
import org.chromium.chrome.browser.price_tracking.PriceDropNotificationManager;
import org.chromium.chrome.browser.price_tracking.PriceDropNotificationManagerFactory;
import org.chromium.chrome.browser.price_tracking.PriceTrackingFeatures;
import org.chromium.chrome.browser.price_tracking.PriceTrackingUtilities;
import org.chromium.chrome.browser.profiles.Profile;
import org.chromium.chrome.browser.share.ShareDelegate;
import org.chromium.chrome.browser.tabmodel.TabCreatorManager;
import org.chromium.chrome.browser.tabmodel.TabList;
import org.chromium.chrome.browser.tabmodel.TabModelSelector;
import org.chromium.chrome.browser.tasks.pseudotab.PseudoTab;
import org.chromium.chrome.browser.tasks.pseudotab.TabAttributeCache;
import org.chromium.chrome.browser.tasks.tab_management.PriceMessageService.PriceMessageType;
import org.chromium.chrome.browser.tasks.tab_management.TabListCoordinator.TabListMode;
import org.chromium.chrome.browser.tasks.tab_management.TabSelectionEditorCoordinator.TabSelectionEditorController;
import org.chromium.chrome.browser.tasks.tab_management.TabSelectionEditorCoordinator.TabSelectionEditorNavigationProvider;
import org.chromium.chrome.browser.tasks.tab_management.suggestions.TabSuggestionsOrchestrator;
import org.chromium.chrome.browser.ui.messages.snackbar.SnackbarManager;
import org.chromium.chrome.features.start_surface.StartSurfaceConfiguration;
import org.chromium.chrome.tab_ui.R;
import org.chromium.components.browser_ui.widget.MenuOrKeyboardActionController;
import org.chromium.components.browser_ui.widget.scrim.ScrimCoordinator;
import org.chromium.components.browser_ui.widget.scrim.ScrimCoordinator.SystemUiScrimDelegate;
import org.chromium.ui.modaldialog.ModalDialogManager;
import org.chromium.ui.modelutil.LayoutViewBuilder;
import org.chromium.ui.modelutil.PropertyModel;
import org.chromium.ui.modelutil.PropertyModelChangeProcessor;
import org.chromium.ui.resources.dynamics.DynamicResourceLoader;

import java.util.List;

/**
 * Parent coordinator that is responsible for showing a grid or carousel of tabs for the main
 * TabSwitcher UI.
 */
public class TabSwitcherCoordinator
        implements DestroyObserver, TabSwitcher, TabSwitcher.TabListDelegate,
                   TabSwitcherMediator.ResetHandler, TabSwitcherMediator.MessageItemsController,
                   TabSwitcherMediator.PriceWelcomeMessageController {
    /**
     * Interface to control the IPH dialog.
     */
    interface IphController {
        /**
         * Show the dialog with IPH.
         */
        void showIph();
    }

    private class TabGroupManualSelectionMode {
        public final String actionString;
        public final int actionButtonDescriptionResourceId;
        public final int enablingThreshold;
        public final TabSelectionEditorActionProvider actionProvider;
        public final TabSelectionEditorCoordinator
                .TabSelectionEditorNavigationProvider navigationProvider;

        TabGroupManualSelectionMode(String actionString, int actionButtonDescriptionResourceId,
                int enablingThreshold, TabSelectionEditorActionProvider actionProvider,
                TabSelectionEditorNavigationProvider navigationProvider) {
            this.actionString = actionString;
            this.actionButtonDescriptionResourceId = actionButtonDescriptionResourceId;
            this.enablingThreshold = enablingThreshold;
            this.actionProvider = actionProvider;
            this.navigationProvider = navigationProvider;
        }
    }

    // TODO(crbug.com/982018): Rename 'COMPONENT_NAME' so as to add different metrics for carousel
    // tab switcher.
    static final String COMPONENT_NAME = "GridTabSwitcher";
    private static boolean sAppendedMessagesForTesting;
    // TODO(crbug.com/1240249): We have to use a static variable because startedShowing() &
    // startedHiding() aren't always called for CAROUSEL tab switcher, thus we can't get its
    // visibility directly.
    private static boolean sIsGridTabSwitcherShowing;
    private final Activity mActivity;
    private final PropertyModelChangeProcessor mContainerViewChangeProcessor;
    private final ActivityLifecycleDispatcher mLifecycleDispatcher;
    private final MenuOrKeyboardActionController mMenuOrKeyboardActionController;
    private final TabListCoordinator mTabListCoordinator;
    private final TabSwitcherMediator mMediator;
    private final MultiThumbnailCardProvider mMultiThumbnailCardProvider;
    @Nullable
    private final TabGridDialogCoordinator mTabGridDialogCoordinator;
    private final TabModelSelector mTabModelSelector;
    private final @TabListCoordinator.TabListMode int mMode;
    private final MessageCardProviderCoordinator mMessageCardProviderCoordinator;
    private final MultiWindowModeStateDispatcher mMultiWindowModeStateDispatcher;
    private final Supplier<DynamicResourceLoader> mDynamicResourceLoaderSupplier;
    private final SnackbarManager mSnackbarManager;
    private final ModalDialogManager mModalDialogManager;
    @Nullable
    private TabSelectionEditorCoordinator mTabSelectionEditorCoordinator;
    @Nullable
    private TabGroupManualSelectionMode mTabGroupManualSelectionMode;
    private TabSuggestionsOrchestrator mTabSuggestionsOrchestrator;
    private TabAttributeCache mTabAttributeCache;
    private ViewGroup mContainer;
    private TabCreatorManager mTabCreatorManager;
    private boolean mIsInitialized;
    private PriceMessageService mPriceMessageService;
    private SharedPreferencesManager.Observer mPriceAnnotationsPrefObserver;
    private final ViewGroup mCoordinatorView;
    private final ViewGroup mRootView;
    private TabContentManager mTabContentManager;
    private IncognitoReauthPromoMessageService mIncognitoReauthPromoMessageService;
    /**
     * TODO(crbug.com/1227656): Refactor this to pass a supplier instead to ensure we re-use the
     * same instance of {@link IncognitoReauthManager} across the codebase.
     */
    private IncognitoReauthManager mIncognitoReauthManager;

    private final MenuOrKeyboardActionController
            .MenuOrKeyboardActionHandler mTabSwitcherMenuActionHandler =
            new MenuOrKeyboardActionController.MenuOrKeyboardActionHandler() {
                @Override
                public boolean handleMenuOrKeyboardAction(int id, boolean fromMenu) {
                    // Both GRID and CAROUSEL tab switchers register a MenuOrKeyboardActionHandler
                    // upon creation, but only the first registered handler will handle the menu
                    // actions. Checking the mode allows the handler created under GRID tab switcher
                    // to handle the menu actions when GRID tab switcher is showing; while CAROUSAL
                    // tab switcher handles the menu actions when Start Surface is showing.
                    if ((sIsGridTabSwitcherShowing && mMode == TabListMode.CAROUSEL)
                            || (!sIsGridTabSwitcherShowing && mMode == TabListMode.GRID)) {
                        return false;
                    }
                    if (id == R.id.menu_group_tabs && mTabSelectionEditorCoordinator != null) {
                        assert mTabGroupManualSelectionMode != null;

                        mTabSelectionEditorCoordinator.getController().configureToolbar(
                                mTabGroupManualSelectionMode.actionString,
                                mTabGroupManualSelectionMode.actionButtonDescriptionResourceId,
                                mTabGroupManualSelectionMode.actionProvider,
                                mTabGroupManualSelectionMode.enablingThreshold,
                                mTabGroupManualSelectionMode.navigationProvider);

                        mTabSelectionEditorCoordinator.getController().show(
                                mTabModelSelector.getTabModelFilterProvider()
                                        .getCurrentTabModelFilter()
                                        .getTabsWithNoOtherRelatedTabs());
                        RecordUserAction.record("MobileMenuGroupTabs");
                        return true;
                    } else if (id == R.id.track_prices_row_menu_id) {
                        assert mPriceTrackingDialogCoordinator != null;
                        mPriceTrackingDialogCoordinator.show();
                        return true;
                    }
                    return false;
                }
            };
    private TabGridIphDialogCoordinator mTabGridIphDialogCoordinator;
    private PriceTrackingDialogCoordinator mPriceTrackingDialogCoordinator;
    private TabSwitcherCustomViewManager mTabSwitcherCustomViewManager;

    /** {@see TabManagementDelegate#createCarouselTabSwitcher} */
    public TabSwitcherCoordinator(@NonNull Activity activity,
            @NonNull ActivityLifecycleDispatcher lifecycleDispatcher,
            @NonNull TabModelSelector tabModelSelector,
            @NonNull TabContentManager tabContentManager,
            @NonNull BrowserControlsStateProvider browserControls,
            @NonNull TabCreatorManager tabCreatorManager,
            @NonNull MenuOrKeyboardActionController menuOrKeyboardActionController,
            @NonNull ViewGroup container, @NonNull Supplier<ShareDelegate> shareDelegateSupplier,
            @NonNull MultiWindowModeStateDispatcher multiWindowModeStateDispatcher,
            @NonNull ScrimCoordinator scrimCoordinator, @TabListMode int mode,
            @NonNull ViewGroup rootView,
            @NonNull Supplier<DynamicResourceLoader> dynamicResourceLoaderSupplier,
            @NonNull SnackbarManager snackbarManager,
            @NonNull ModalDialogManager modalDialogManager) {
        try (TraceEvent e = TraceEvent.scoped("TabSwitcherCoordinator.constructor")) {
            mActivity = activity;
            mMode = mode;
            mTabModelSelector = tabModelSelector;
            mContainer = container;
            mCoordinatorView = activity.findViewById(R.id.coordinator);
            mTabCreatorManager = tabCreatorManager;
            mMultiWindowModeStateDispatcher = multiWindowModeStateDispatcher;
            mRootView = rootView;
            mTabContentManager = tabContentManager;
            mDynamicResourceLoaderSupplier = dynamicResourceLoaderSupplier;
            mSnackbarManager = snackbarManager;
            mModalDialogManager = modalDialogManager;

            PropertyModel containerViewModel =
                    new PropertyModel(TabListContainerProperties.ALL_KEYS);

            mMediator = new TabSwitcherMediator(activity, this, containerViewModel,
                    tabModelSelector, browserControls, container, tabContentManager, this, this,
                    multiWindowModeStateDispatcher, mode);

            mTabSwitcherCustomViewManager = new TabSwitcherCustomViewManager(mMediator);

            mMultiThumbnailCardProvider =
                    new MultiThumbnailCardProvider(activity, tabContentManager, tabModelSelector);

            PseudoTab.TitleProvider titleProvider = (context, tab) -> {
                int numRelatedTabs =
                        PseudoTab.getRelatedTabs(context, tab, tabModelSelector).size();
                if (numRelatedTabs == 1) return tab.getTitle();
                return activity.getResources().getQuantityString(
                        R.plurals.bottom_tab_grid_title_placeholder, numRelatedTabs,
                        numRelatedTabs);
            };

            long startTimeMs = SystemClock.uptimeMillis();
            mTabListCoordinator = new TabListCoordinator(mode, activity, tabModelSelector,
                    mMultiThumbnailCardProvider, titleProvider, true, mMediator, null,
                    TabProperties.UiType.CLOSABLE, null, this, container, true, COMPONENT_NAME,
                    mRootView, null);
            mContainerViewChangeProcessor = PropertyModelChangeProcessor.create(containerViewModel,
                    mTabListCoordinator.getContainerView(), TabListContainerViewBinder::bind);

            RecordHistogram.recordTimesHistogram("Android.TabSwitcher.SetupRecyclerView.Time",
                    SystemClock.uptimeMillis() - startTimeMs);

            mMediator.addTabSwitcherViewObserver(new TabSwitcherViewObserver() {
                @Override
                public void startedShowing() {
                    if (mMode == TabListMode.GRID) sIsGridTabSwitcherShowing = true;
                }

                @Override
                public void finishedShowing() {}

                @Override
                public void startedHiding() {
                    if (mMode == TabListMode.GRID) sIsGridTabSwitcherShowing = false;
                }

                @Override
                public void finishedHiding() {}
            });

            if (TabUiFeatureUtilities.isLaunchPolishEnabled()
                    && TabUiFeatureUtilities.isTabGroupsAndroidContinuationEnabled(activity)) {
                mMediator.addTabSwitcherViewObserver(new TabSwitcherViewObserver() {
                    @Override
                    public void startedShowing() {}

                    @Override
                    public void finishedShowing() {
                        if (!mTabModelSelector.isTabStateInitialized()) return;

                        int selectedIndex = mTabModelSelector.getTabModelFilterProvider()
                                                    .getCurrentTabModelFilter()
                                                    .index();
                        ViewHolder selectedViewHolder =
                                mTabListCoordinator.getContainerView()
                                        .findViewHolderForAdapterPosition(selectedIndex);

                        if (selectedViewHolder == null) return;

                        View focusView = selectedViewHolder.itemView;
                        focusView.requestFocus();
                        focusView.sendAccessibilityEvent(AccessibilityEvent.TYPE_VIEW_FOCUSED);
                    }

                    @Override
                    public void startedHiding() {}

                    @Override
                    public void finishedHiding() {}
                });
            }

            mMessageCardProviderCoordinator = new MessageCardProviderCoordinator(
                    activity, tabModelSelector::isIncognitoSelected, (identifier) -> {
                        if (identifier == MessageService.MessageType.PRICE_MESSAGE
                                || identifier
                                        == MessageService.MessageType
                                                   .INCOGNITO_REAUTH_PROMO_MESSAGE) {
                            mTabListCoordinator.removeSpecialListItem(
                                    TabProperties.UiType.LARGE_MESSAGE, identifier);
                        } else {
                            mTabListCoordinator.removeSpecialListItem(
                                    TabProperties.UiType.MESSAGE, identifier);
                            appendNextMessage(identifier);
                        }
                    });

            if (TabUiFeatureUtilities.isTabGroupsAndroidEnabled(activity)) {
                ScrimCoordinator gridDialogScrimCoordinator =
                        shouldUseNewScrim() ? createScrimCoordinator() : scrimCoordinator;
                mTabGridDialogCoordinator = new TabGridDialogCoordinator(activity, tabModelSelector,
                        tabContentManager, tabCreatorManager, mCoordinatorView, this, mMediator,
                        this::getTabGridDialogAnimationSourceView, shareDelegateSupplier,
                        gridDialogScrimCoordinator, rootView);
                mMediator.setTabGridDialogController(
                        mTabGridDialogCoordinator.getDialogController());
            } else {
                mTabGridDialogCoordinator = null;
            }

            if (mode == TabListCoordinator.TabListMode.GRID) {
                if (shouldRegisterMessageItemType()) {
                    mTabListCoordinator.registerItemType(TabProperties.UiType.MESSAGE,
                            new LayoutViewBuilder(R.layout.tab_grid_message_card_item),
                            MessageCardViewBinder::bind);
                }

                if (shouldRegisterLargeMessageItemType()) {
                    mTabListCoordinator.registerItemType(TabProperties.UiType.LARGE_MESSAGE,
                            new LayoutViewBuilder(R.layout.large_message_card_item),
                            LargeMessageCardViewBinder::bind);
                }

                if (PriceTrackingFeatures.isPriceTrackingEnabled()
                        && PriceTrackingFeatures.getPriceTrackingEnabled()) {
                    mPriceAnnotationsPrefObserver = key -> {
                        if (PriceTrackingUtilities.TRACK_PRICES_ON_TABS.equals(key)
                                && !mTabModelSelector.isIncognitoSelected()
                                && mTabModelSelector.isTabStateInitialized()) {
                            resetWithTabList(mTabModelSelector.getTabModelFilterProvider()
                                                     .getCurrentTabModelFilter(),
                                    false, isShowingTabsInMRUOrder(mMode));
                        }
                    };
                    SharedPreferencesManager.getInstance().addObserver(
                            mPriceAnnotationsPrefObserver);
                }
            }

            if (ChromeFeatureList.sInstantStart.isEnabled()
                    || TabUiFeatureUtilities.ENABLE_SEARCH_CHIP.getValue()
                            && mode != TabListCoordinator.TabListMode.CAROUSEL) {
                mTabAttributeCache = new TabAttributeCache(mTabModelSelector);
            }

            mMenuOrKeyboardActionController = menuOrKeyboardActionController;
            mMenuOrKeyboardActionController.registerMenuOrKeyboardActionHandler(
                    mTabSwitcherMenuActionHandler);

            mLifecycleDispatcher = lifecycleDispatcher;
            mLifecycleDispatcher.register(this);
        }
    }

    /**
     * Tablet Tab Switcher polish uses a scrim to show/hide tab switcher.
     * Create a new scrim via a new scrim coordinator for tab group dialog.
     * @return if tab switcher polish is enabled on tablets.
     */
    private boolean shouldUseNewScrim() {
        return TabUiFeatureUtilities.isTabletGridTabSwitcherPolishEnabled(mRootView.getContext());
    }

    private ScrimCoordinator createScrimCoordinator() {
        ViewGroup coordinator = mActivity.findViewById(org.chromium.chrome.R.id.coordinator);
        SystemUiScrimDelegate delegate = new SystemUiScrimDelegate() {
            @Override
            public void setStatusBarScrimFraction(float scrimFraction) {}

            @Override
            public void setNavigationBarScrimFraction(float scrimFraction) {}
        };
        return new ScrimCoordinator(mActivity, delegate, coordinator,
                coordinator.getContext().getColor(
                        org.chromium.chrome.R.color.omnibox_focused_fading_background_color));
    }

    @VisibleForTesting
    public static boolean hasAppendedMessagesForTesting() {
        return sAppendedMessagesForTesting;
    }

    @Override
    public void initWithNative() {
        if (mIsInitialized) return;
        try (TraceEvent e = TraceEvent.scoped("TabSwitcherCoordinator.initWithNative")) {
            mTabListCoordinator.initWithNative(mDynamicResourceLoaderSupplier.get());

            // Selector editor required for tab groups and close tab suggestions.
            if (TabUiFeatureUtilities.isTabGroupsAndroidEnabled(mActivity)
                    || ChromeFeatureList.sCloseTabSuggestions.isEnabled()) {
                setUpTabSelectionEditorCoordinator(mActivity, mTabContentManager);
            }
            if (TabUiFeatureUtilities.isTabGroupsAndroidEnabled(mActivity)) {
                setUpTabGroupManualSelectionMode(mActivity);
                if (mTabGridDialogCoordinator != null) {
                    mTabGridDialogCoordinator.initWithNative(mActivity, mTabModelSelector,
                            mTabContentManager, mTabListCoordinator.getTabGroupTitleEditor());
                }
            }

            final TabSelectionEditorController controller = mTabSelectionEditorCoordinator != null
                    ? mTabSelectionEditorCoordinator.getController()
                    : null;

            if (mMode == TabListCoordinator.TabListMode.GRID) {
                if (ChromeFeatureList.sCloseTabSuggestions.isEnabled()) {
                    mTabSuggestionsOrchestrator = new TabSuggestionsOrchestrator(
                            mActivity, mTabModelSelector, mLifecycleDispatcher);
                    TabSuggestionMessageService tabSuggestionMessageService =
                            new TabSuggestionMessageService(
                                    mActivity, mTabModelSelector, controller);
                    mTabSuggestionsOrchestrator.addObserver(tabSuggestionMessageService);
                    mMessageCardProviderCoordinator.subscribeMessageService(
                            tabSuggestionMessageService);
                }

                if (TabUiFeatureUtilities.isTabGroupsAndroidEnabled(mActivity)
                        && !TabSwitcherCoordinator.isShowingTabsInMRUOrder(mMode)) {
                    mTabGridIphDialogCoordinator = new TabGridIphDialogCoordinator(
                            mActivity, mContainer, mModalDialogManager);
                    IphMessageService iphMessageService =
                            new IphMessageService(mTabGridIphDialogCoordinator);
                    mMessageCardProviderCoordinator.subscribeMessageService(iphMessageService);
                }

                if (IncognitoReauthManager.isIncognitoReauthFeatureAvailable()
                        && mIncognitoReauthPromoMessageService == null) {
                    mIncognitoReauthManager = new IncognitoReauthManager();
                    mIncognitoReauthPromoMessageService = new IncognitoReauthPromoMessageService(
                            MessageService.MessageType.INCOGNITO_REAUTH_PROMO_MESSAGE,
                            Profile.getLastUsedRegularProfile(), mActivity,
                            SharedPreferencesManager.getInstance(), mIncognitoReauthManager,
                            mSnackbarManager, TabUiFeatureUtilities::isTabToGtsAnimationEnabled,
                            mLifecycleDispatcher);
                    mMessageCardProviderCoordinator.subscribeMessageService(
                            mIncognitoReauthPromoMessageService);
                }
            }

            mMultiThumbnailCardProvider.initWithNative();
            mMediator.initWithNative(controller, mSnackbarManager);
            // TODO(crbug.com/1222762): Only call setUpPriceTracking in GRID TabSwitcher.
            setUpPriceTracking(mActivity, mModalDialogManager);

            mIsInitialized = true;
        }
    }

    private void setUpTabGroupManualSelectionMode(Context context) {
        try (TraceEvent e = TraceEvent.scoped(
                     "TabSwitcherCoordintor.setUpTabGroupManualSelectionMode")) {
            mTabGroupManualSelectionMode = new TabGroupManualSelectionMode(
                    context.getString(R.string.tab_selection_editor_group),
                    R.plurals.accessibility_tab_selection_editor_group_button, 2,
                    new TabSelectionEditorActionProvider(
                            mTabSelectionEditorCoordinator.getController(),
                            TabSelectionEditorActionProvider.TabSelectionEditorAction.GROUP),
                    new TabSelectionEditorNavigationProvider(
                            mTabSelectionEditorCoordinator.getController()));
        }
    }

    private void setUpTabSelectionEditorCoordinator(
            Context context, TabContentManager tabContentManager) {
        // For tab switcher in carousel mode, the selection editor should still follow grid
        // style.
        int selectionEditorMode = mMode == TabListMode.CAROUSEL ? TabListMode.GRID : mMode;
        mTabSelectionEditorCoordinator =
                new TabSelectionEditorCoordinator(context, mCoordinatorView, mTabModelSelector,
                        tabContentManager, selectionEditorMode, mRootView, /*displayGroups=*/false);
    }

    private void setUpPriceTracking(Context context, ModalDialogManager modalDialogManager) {
        if (PriceTrackingFeatures.isPriceTrackingEnabled()) {
            PriceDropNotificationManager notificationManager =
                    PriceDropNotificationManagerFactory.create();
            mPriceTrackingDialogCoordinator = new PriceTrackingDialogCoordinator(context,
                    modalDialogManager, this, mTabModelSelector, notificationManager, mMode);
            if (mMode == TabListCoordinator.TabListMode.GRID) {
                mPriceMessageService = new PriceMessageService(
                        mTabListCoordinator, mMediator, notificationManager);
                mMessageCardProviderCoordinator.subscribeMessageService(mPriceMessageService);
                mMediator.setPriceMessageService(mPriceMessageService);
            }
        }
    }

    // TabSwitcher implementation.
    @Override
    public void setOnTabSelectingListener(OnTabSelectingListener listener) {
        mMediator.setOnTabSelectingListener(listener);
    }

    @Override
    public Controller getController() {
        return mMediator;
    }

    @Override
    public TabListDelegate getTabListDelegate() {
        return this;
    }

    @Override
    public Supplier<Boolean> getTabGridDialogVisibilitySupplier() {
        if (mTabGridDialogCoordinator != null) {
            return mTabGridDialogCoordinator::isVisible;
        }
        return () -> false;
    }

    @Override
    public TabSwitcherCustomViewManager getTabSwitcherCustomViewManager() {
        return mTabSwitcherCustomViewManager;
    }

    @Override
    public int getTabListTopOffset() {
        return mTabListCoordinator.getTabListTopOffset();
    }

    @Override
    public int getListModeForTesting() {
        return mMode;
    }

    @Override
    public void requestFocusOnCurrentTab() {
        if (!mTabModelSelector.isTabStateInitialized()) return;

        int selectedIndex =
                mTabModelSelector.getTabModelFilterProvider().getCurrentTabModelFilter().index();
        ViewHolder selectedViewHolder =
                mTabListCoordinator.getContainerView().findViewHolderForAdapterPosition(
                        selectedIndex);

        if (selectedViewHolder == null) return;

        View focusView = selectedViewHolder.itemView;
        focusView.requestFocus();
        focusView.sendAccessibilityEvent(AccessibilityEvent.TYPE_VIEW_FOCUSED);
    }

    @Override
    public boolean prepareTabSwitcherView() {
        boolean quick = mMediator.prepareTabSwitcherView();
        mTabListCoordinator.prepareTabSwitcherView();
        return quick;
    }

    @Override
    public void postHiding() {
        mTabListCoordinator.postHiding();
        mMediator.postHiding();
    }

    @Override
    @NonNull
    public Rect getThumbnailLocationOfCurrentTab(boolean forceUpdate) {
        if (mTabGridDialogCoordinator != null && mTabGridDialogCoordinator.isVisible()) {
            assert forceUpdate;
            Rect thumbnail = mTabGridDialogCoordinator.getGlobalLocationOfCurrentThumbnail();
            // Adjust to the relative coordinate.
            Rect root = mTabListCoordinator.getRecyclerViewLocation();
            thumbnail.offset(-root.left, -root.top);
            return thumbnail;
        }
        if (forceUpdate) mTabListCoordinator.updateThumbnailLocation();
        return mTabListCoordinator.getThumbnailLocationOfCurrentTab();
    }

    // TabListDelegate implementation.
    @Override
    public int getResourceId() {
        return mTabListCoordinator.getResourceId();
    }

    @Override
    public long getLastDirtyTime() {
        return mTabListCoordinator.getLastDirtyTime();
    }

    @Override
    @VisibleForTesting
    public void setBitmapCallbackForTesting(Callback<Bitmap> callback) {
        TabListMediator.ThumbnailFetcher.sBitmapCallbackForTesting = callback;
    }

    @Override
    @VisibleForTesting
    public int getBitmapFetchCountForTesting() {
        return TabListMediator.ThumbnailFetcher.sFetchCountForTesting;
    }

    @Override
    @VisibleForTesting
    public int getSoftCleanupDelayForTesting() {
        return mMediator.getSoftCleanupDelayForTesting();
    }

    @Override
    @VisibleForTesting
    public int getCleanupDelayForTesting() {
        return mMediator.getCleanupDelayForTesting();
    }

    // ResetHandler implementation.
    @Override
    public boolean resetWithTabList(@Nullable TabList tabList, boolean quickMode, boolean mruMode) {
        return resetWithTabs(PseudoTab.getListOfPseudoTab(tabList), quickMode, mruMode);
    }

    @Override
    public boolean resetWithTabs(
            @Nullable List<PseudoTab> tabs, boolean quickMode, boolean mruMode) {
        mMediator.registerFirstMeaningfulPaintRecorder();
        // Invalidate price welcome message for every reset so that the stale message won't be
        // restored by mistake (e.g. from tabClosureUndone in TabSwitcherMediator).
        if (mPriceMessageService != null) {
            mPriceMessageService.invalidateMessage();
        }
        boolean showQuickly = mTabListCoordinator.resetWithListOfTabs(tabs, quickMode, mruMode);
        if (showQuickly) {
            removeAllAppendedMessage();
        }
        if (tabs != null && tabs.size() > 0) {
            if (mPriceMessageService != null
                    && PriceTrackingUtilities.isPriceAlertsMessageCardEnabled()) {
                mPriceMessageService.preparePriceMessage(PriceMessageType.PRICE_ALERTS, null);
            }
            appendMessagesTo(tabs.size());
        }

        return showQuickly;
    }

    // MessageItemsController implementation.
    @Override
    public void removeAllAppendedMessage() {
        mTabListCoordinator.removeSpecialListItem(
                TabProperties.UiType.MESSAGE, MessageService.MessageType.ALL);
        mTabListCoordinator.removeSpecialListItem(TabProperties.UiType.LARGE_MESSAGE,
                MessageService.MessageType.INCOGNITO_REAUTH_PROMO_MESSAGE);
        sAppendedMessagesForTesting = false;
    }

    @Override
    public void restoreAllAppendedMessage() {
        sAppendedMessagesForTesting = false;
        List<MessageCardProviderMediator.Message> messages =
                mMessageCardProviderCoordinator.getMessageItems();
        for (int i = 0; i < messages.size(); i++) {
            if (!shouldAppendMessage(messages.get(i).model)) continue;
            // The restore of PRICE_MESSAGE is handled in the restorePriceWelcomeMessage() below.
            if (messages.get(i).type == MessageService.MessageType.PRICE_MESSAGE) {
                continue;
            } else if (messages.get(i).type
                    == MessageService.MessageType.INCOGNITO_REAUTH_PROMO_MESSAGE) {
                mTabListCoordinator.addSpecialListItemToEnd(
                        TabProperties.UiType.LARGE_MESSAGE, messages.get(i).model);
            } else {
                mTabListCoordinator.addSpecialListItemToEnd(
                        TabProperties.UiType.MESSAGE, messages.get(i).model);
            }
        }
        sAppendedMessagesForTesting = messages.size() > 0;
    }

    // PriceWelcomeMessageController implementation.
    @Override
    public void removePriceWelcomeMessage() {
        mTabListCoordinator.removeSpecialListItem(
                TabProperties.UiType.LARGE_MESSAGE, MessageService.MessageType.PRICE_MESSAGE);
    }

    @Override
    public void restorePriceWelcomeMessage() {
        appendNextMessage(MessageService.MessageType.PRICE_MESSAGE);
    }

    @Override
    public void showPriceWelcomeMessage(PriceMessageService.PriceTabData priceTabData) {
        if (mPriceMessageService == null
                || !PriceTrackingUtilities.isPriceWelcomeMessageCardEnabled()
                || mMessageCardProviderCoordinator.isMessageShown(
                        MessageService.MessageType.PRICE_MESSAGE, PriceMessageType.PRICE_WELCOME)) {
            return;
        }
        if (mPriceMessageService.preparePriceMessage(
                    PriceMessageType.PRICE_WELCOME, priceTabData)) {
            appendNextMessage(MessageService.MessageType.PRICE_MESSAGE);
            // To make the message card in view when user enters tab switcher, we should scroll to
            // current tab with 0 offset. See {@link
            // TabSwitcherMediator#setInitialScrollIndexOffset} for more details.
            mMediator.scrollToTab(mTabModelSelector.getTabModelFilterProvider()
                                          .getCurrentTabModelFilter()
                                          .index());
        }
    }

    private void appendMessagesTo(int index) {
        if (mMultiWindowModeStateDispatcher.isInMultiWindowMode()) return;
        sAppendedMessagesForTesting = false;
        List<MessageCardProviderMediator.Message> messages =
                mMessageCardProviderCoordinator.getMessageItems();
        for (int i = 0; i < messages.size(); i++) {
            if (!shouldAppendMessage(messages.get(i).model)) continue;
            if (messages.get(i).type == MessageService.MessageType.PRICE_MESSAGE) {
                mTabListCoordinator.addSpecialListItem(
                        index, TabProperties.UiType.LARGE_MESSAGE, messages.get(i).model);
            } else if (messages.get(i).type
                    == MessageService.MessageType.INCOGNITO_REAUTH_PROMO_MESSAGE) {
                mayAddIncognitoReauthPromoCard(messages.get(i).model);
            } else {
                mTabListCoordinator.addSpecialListItem(
                        index, TabProperties.UiType.MESSAGE, messages.get(i).model);
            }
            index++;
        }
        if (messages.size() > 0) sAppendedMessagesForTesting = true;
    }

    private void appendNextMessage(@MessageService.MessageType int messageType) {
        assert mMessageCardProviderCoordinator != null;

        MessageCardProviderMediator.Message nextMessage =
                mMessageCardProviderCoordinator.getNextMessageItemForType(messageType);
        if (nextMessage == null || !shouldAppendMessage(nextMessage.model)) return;
        if (messageType == MessageService.MessageType.PRICE_MESSAGE) {
            mTabListCoordinator.addSpecialListItem(
                    mTabListCoordinator.getPriceWelcomeMessageInsertionIndex(),
                    TabProperties.UiType.LARGE_MESSAGE, nextMessage.model);
        } else {
            mTabListCoordinator.addSpecialListItemToEnd(
                    TabProperties.UiType.MESSAGE, nextMessage.model);
        }
    }

    private void mayAddIncognitoReauthPromoCard(PropertyModel model) {
        if (mIncognitoReauthPromoMessageService.isIncognitoReauthPromoMessageEnabled(
                    Profile.getLastUsedRegularProfile())) {
            mTabListCoordinator.addSpecialListItemToEnd(TabProperties.UiType.LARGE_MESSAGE, model);
            mIncognitoReauthPromoMessageService.increasePromoShowCountAndMayDisableIfCountExceeds();
        }
    }

    private boolean shouldAppendMessage(PropertyModel messageModel) {
        Integer messageCardVisibilityControlValue = messageModel.get(
                MessageCardViewProperties
                        .MESSAGE_CARD_VISIBILITY_CONTROL_IN_REGULAR_AND_INCOGNITO_MODE);

        @MessageCardViewProperties.MessageCardScope
        int scope = (messageCardVisibilityControlValue != null)
                ? messageCardVisibilityControlValue
                : MessageCardViewProperties.MessageCardScope.REGULAR;

        if (scope == MessageCardViewProperties.MessageCardScope.BOTH) return true;
        return mTabModelSelector.isIncognitoSelected()
                ? scope == MessageCardViewProperties.MessageCardScope.INCOGNITO
                : scope == MessageCardViewProperties.MessageCardScope.REGULAR;
    }

    private View getTabGridDialogAnimationSourceView(int tabId) {
        int index = mTabListCoordinator.indexOfTab(tabId);
        // TODO(crbug.com/999372): This is band-aid fix that will show basic fade-in/fade-out
        // animation when we cannot find the animation source view holder. This is happening due to
        // current group id in TabGridDialog can not be indexed in TabListModel, which should never
        // happen. Remove this when figure out the actual cause.
        ViewHolder sourceViewHolder =
                mTabListCoordinator.getContainerView().findViewHolderForAdapterPosition(index);
        if (sourceViewHolder == null) return null;
        return sourceViewHolder.itemView;
    }

    private boolean shouldRegisterMessageItemType() {
        return ChromeFeatureList.sCloseTabSuggestions.isEnabled()
                || (TabUiFeatureUtilities.isTabGroupsAndroidEnabled(mRootView.getContext())
                        && !TabSwitcherCoordinator.isShowingTabsInMRUOrder(mMode));
    }

    private boolean shouldRegisterLargeMessageItemType() {
        return PriceTrackingFeatures.isPriceTrackingEnabled()
                || IncognitoReauthManager.isIncognitoReauthFeatureAvailable();
    }

    @Override
    public void softCleanup() {
        mTabListCoordinator.softCleanup();
    }

    @Override
    public void hardCleanup() {
        mTabListCoordinator.hardCleanup();
    }

    // ResetHandler implementation.
    @Override
    public void onDestroy() {
        mMenuOrKeyboardActionController.unregisterMenuOrKeyboardActionHandler(
                mTabSwitcherMenuActionHandler);
        mTabListCoordinator.onDestroy();
        mMessageCardProviderCoordinator.destroy();
        mContainerViewChangeProcessor.destroy();
        if (mTabGridDialogCoordinator != null) {
            mTabGridDialogCoordinator.destroy();
        }
        if (mTabGridIphDialogCoordinator != null) {
            mTabGridIphDialogCoordinator.destroy();
        }
        mMultiThumbnailCardProvider.destroy();
        if (mTabSelectionEditorCoordinator != null) {
            mTabSelectionEditorCoordinator.destroy();
        }
        mMediator.destroy();
        mLifecycleDispatcher.unregister(this);
        if (mTabAttributeCache != null) {
            mTabAttributeCache.destroy();
        }
        if (mPriceAnnotationsPrefObserver != null) {
            SharedPreferencesManager.getInstance().removeObserver(mPriceAnnotationsPrefObserver);
        }
    }

    /**
     * Returns whether tabs should be shown in MRU order in current start surface tab switcher.
     * @param mode The Tab switcher mode.
     */
    static boolean isShowingTabsInMRUOrder(@TabListMode int mode) {
        return StartSurfaceConfiguration.SHOW_TABS_IN_MRU_ORDER.getValue()
                && mode == TabListMode.CAROUSEL;
    }
}
