// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.tabbed_mode;

import android.os.Bundle;
import android.view.MotionEvent;
import android.view.ViewGroup;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.appcompat.app.AppCompatActivity;

import org.chromium.base.Callback;
import org.chromium.base.CallbackController;
import org.chromium.base.CommandLine;
import org.chromium.base.ResettersForTesting;
import org.chromium.base.TraceEvent;
import org.chromium.base.metrics.RecordHistogram;
import org.chromium.base.shared_preferences.SharedPreferencesManager;
import org.chromium.base.supplier.ObservableSupplier;
import org.chromium.base.supplier.ObservableSupplierImpl;
import org.chromium.base.supplier.OneShotCallback;
import org.chromium.base.supplier.OneshotSupplier;
import org.chromium.base.supplier.Supplier;
import org.chromium.base.supplier.SupplierUtils;
import org.chromium.base.version_info.VersionInfo;
import org.chromium.chrome.R;
import org.chromium.chrome.browser.ActivityTabProvider;
import org.chromium.chrome.browser.ActivityTabProvider.ActivityTabTabObserver;
import org.chromium.chrome.browser.ApplicationLifetime;
import org.chromium.chrome.browser.SwipeRefreshHandler;
import org.chromium.chrome.browser.accessibility.PageZoomIPHController;
import org.chromium.chrome.browser.app.tab_activity_glue.TabReparentingController;
import org.chromium.chrome.browser.back_press.BackPressManager;
import org.chromium.chrome.browser.banners.AppBannerInProductHelpController;
import org.chromium.chrome.browser.banners.AppBannerInProductHelpControllerFactory;
import org.chromium.chrome.browser.bookmarks.BookmarkModel;
import org.chromium.chrome.browser.bookmarks.TabBookmarker;
import org.chromium.chrome.browser.browser_controls.BrowserControlsSizer;
import org.chromium.chrome.browser.compositor.CompositorViewHolder;
import org.chromium.chrome.browser.compositor.bottombar.ephemeraltab.EphemeralTabCoordinator;
import org.chromium.chrome.browser.compositor.layouts.LayoutManagerImpl;
import org.chromium.chrome.browser.compositor.layouts.content.TabContentManager;
import org.chromium.chrome.browser.contextualsearch.ContextualSearchManager;
import org.chromium.chrome.browser.crash.ChromePureJavaExceptionReporter;
import org.chromium.chrome.browser.desktop_site.DesktopSiteSettingsIPHController;
import org.chromium.chrome.browser.dragdrop.ChromeTabbedOnDragListener;
import org.chromium.chrome.browser.feature_guide.notifications.FeatureNotificationUtils;
import org.chromium.chrome.browser.feature_guide.notifications.FeatureType;
import org.chromium.chrome.browser.feed.webfeed.WebFeedFollowIntroController;
import org.chromium.chrome.browser.firstrun.FirstRunStatus;
import org.chromium.chrome.browser.flags.ActivityType;
import org.chromium.chrome.browser.flags.ChromeFeatureList;
import org.chromium.chrome.browser.flags.ChromeSwitches;
import org.chromium.chrome.browser.fullscreen.BrowserControlsManager;
import org.chromium.chrome.browser.fullscreen.FullscreenManager;
import org.chromium.chrome.browser.gesturenav.BackActionDelegate;
import org.chromium.chrome.browser.gesturenav.HistoryNavigationCoordinator;
import org.chromium.chrome.browser.gesturenav.NavigationSheet;
import org.chromium.chrome.browser.gesturenav.TabbedSheetDelegate;
import org.chromium.chrome.browser.history.HistoryManagerUtils;
import org.chromium.chrome.browser.hub.HubFieldTrial;
import org.chromium.chrome.browser.hub.HubManager;
import org.chromium.chrome.browser.incognito.reauth.IncognitoReauthCoordinatorFactory;
import org.chromium.chrome.browser.incognito.reauth.IncognitoReauthManager;
import org.chromium.chrome.browser.incognito.reauth.IncognitoReauthTopToolbarDelegate;
import org.chromium.chrome.browser.language.AppLanguagePromoDialog;
import org.chromium.chrome.browser.layouts.LayoutManager;
import org.chromium.chrome.browser.layouts.LayoutStateProvider;
import org.chromium.chrome.browser.layouts.LayoutType;
import org.chromium.chrome.browser.lifecycle.ActivityLifecycleDispatcher;
import org.chromium.chrome.browser.locale.LocaleManager;
import org.chromium.chrome.browser.multiwindow.MultiInstanceIphController;
import org.chromium.chrome.browser.multiwindow.MultiInstanceManager;
import org.chromium.chrome.browser.multiwindow.MultiWindowUtils;
import org.chromium.chrome.browser.night_mode.WebContentsDarkModeMessageController;
import org.chromium.chrome.browser.notifications.permissions.NotificationPermissionController;
import org.chromium.chrome.browser.notifications.permissions.NotificationPermissionController.RationaleDelegate;
import org.chromium.chrome.browser.notifications.permissions.NotificationPermissionRationaleBottomSheet;
import org.chromium.chrome.browser.notifications.permissions.NotificationPermissionRationaleDialogController;
import org.chromium.chrome.browser.ntp.NewTabPageLaunchOrigin;
import org.chromium.chrome.browser.ntp.NewTabPageUtils;
import org.chromium.chrome.browser.offlinepages.indicator.OfflineIndicatorControllerV2;
import org.chromium.chrome.browser.offlinepages.indicator.OfflineIndicatorInProductHelpController;
import org.chromium.chrome.browser.omnibox.UrlFocusChangeListener;
import org.chromium.chrome.browser.preferences.ChromePreferenceKeys;
import org.chromium.chrome.browser.preferences.ChromeSharedPreferences;
import org.chromium.chrome.browser.privacy_sandbox.PrivacySandboxDialogController;
import org.chromium.chrome.browser.privacy_sandbox.TrackingProtectionNoticeController;
import org.chromium.chrome.browser.profiles.Profile;
import org.chromium.chrome.browser.read_later.ReadLaterIPHController;
import org.chromium.chrome.browser.settings.SettingsLauncherImpl;
import org.chromium.chrome.browser.share.ShareDelegate;
import org.chromium.chrome.browser.share.link_to_text.LinkToTextIPHController;
import org.chromium.chrome.browser.signin.SyncConsentActivityLauncherImpl;
import org.chromium.chrome.browser.status_indicator.StatusIndicatorCoordinator;
import org.chromium.chrome.browser.subscriptions.CommerceSubscriptionsService;
import org.chromium.chrome.browser.subscriptions.CommerceSubscriptionsServiceFactory;
import org.chromium.chrome.browser.tab.RequestDesktopUtils;
import org.chromium.chrome.browser.tab.Tab;
import org.chromium.chrome.browser.tab.TabAssociatedApp;
import org.chromium.chrome.browser.tab.TabLaunchType;
import org.chromium.chrome.browser.tabmodel.TabCreator;
import org.chromium.chrome.browser.tabmodel.TabCreatorManager;
import org.chromium.chrome.browser.tabmodel.TabModelSelector;
import org.chromium.chrome.browser.tasks.tab_management.TabSwitcher;
import org.chromium.chrome.browser.tasks.tab_management.TabUiFeatureUtilities;
import org.chromium.chrome.browser.tasks.tab_management.UndoGroupSnackbarController;
import org.chromium.chrome.browser.toolbar.ToolbarButtonInProductHelpController;
import org.chromium.chrome.browser.toolbar.ToolbarIntentMetadata;
import org.chromium.chrome.browser.ui.RootUiCoordinator;
import org.chromium.chrome.browser.ui.appmenu.AppMenuBlocker;
import org.chromium.chrome.browser.ui.appmenu.AppMenuDelegate;
import org.chromium.chrome.browser.ui.default_browser_promo.DefaultBrowserPromoUtils;
import org.chromium.chrome.browser.ui.edge_to_edge.EdgeToEdgeController;
import org.chromium.chrome.browser.ui.edge_to_edge.EdgeToEdgeControllerFactory;
import org.chromium.chrome.browser.ui.messages.snackbar.SnackbarManager;
import org.chromium.chrome.browser.ui.signin.FullScreenSyncPromoUtil;
import org.chromium.chrome.browser.ui.system.StatusBarColorController.StatusBarColorProvider;
import org.chromium.chrome.browser.webapps.AddToHomescreenIPHController;
import org.chromium.chrome.browser.webapps.AddToHomescreenMostVisitedTileClickObserver;
import org.chromium.chrome.browser.webapps.PwaRestorePromoUtils;
import org.chromium.chrome.features.start_surface.StartSurface;
import org.chromium.chrome.features.start_surface.StartSurfaceUserData;
import org.chromium.components.browser_ui.accessibility.PageZoomCoordinator;
import org.chromium.components.browser_ui.bottomsheet.EmptyBottomSheetObserver;
import org.chromium.components.browser_ui.widget.InsetObserver;
import org.chromium.components.browser_ui.widget.MenuOrKeyboardActionController;
import org.chromium.components.browser_ui.widget.TouchEventObserver;
import org.chromium.components.browser_ui.widget.scrim.ScrimCoordinator;
import org.chromium.components.messages.MessageDispatcherProvider;
import org.chromium.components.webapps.bottomsheet.PwaBottomSheetController;
import org.chromium.components.webapps.bottomsheet.PwaBottomSheetControllerFactory;
import org.chromium.ui.base.ActivityWindowAndroid;
import org.chromium.ui.base.DeviceFormFactor;
import org.chromium.ui.base.IntentRequestTracker;
import org.chromium.ui.dragdrop.DragDropGlobalState;
import org.chromium.ui.modaldialog.ModalDialogManager;

import java.util.function.BooleanSupplier;
import java.util.function.Function;

/** A {@link RootUiCoordinator} variant that controls tabbed-mode specific UI. */
public class TabbedRootUiCoordinator extends RootUiCoordinator {
    private static boolean sDisableTopControlsAnimationForTesting;
    private final RootUiTabObserver mRootUiTabObserver;
    private TabbedSystemUiCoordinator mSystemUiCoordinator;

    private StatusIndicatorCoordinator mStatusIndicatorCoordinator;
    private StatusIndicatorCoordinator.StatusIndicatorObserver mStatusIndicatorObserver;
    private OfflineIndicatorControllerV2 mOfflineIndicatorController;
    private OfflineIndicatorInProductHelpController mOfflineIndicatorInProductHelpController;
    private ReadLaterIPHController mReadLaterIPHController;
    private DesktopSiteSettingsIPHController mDesktopSiteSettingsIPHController;
    private WebFeedFollowIntroController mWebFeedFollowIntroController;
    private UrlFocusChangeListener mUrlFocusChangeListener;
    private @Nullable ToolbarButtonInProductHelpController mToolbarButtonInProductHelpController;
    private AddToHomescreenIPHController mAddToHomescreenIPHController;
    private LinkToTextIPHController mLinkToTextIPHController;
    private AddToHomescreenMostVisitedTileClickObserver mAddToHomescreenMostVisitedTileObserver;
    private AppBannerInProductHelpController mAppBannerInProductHelpController;
    private PwaBottomSheetController mPwaBottomSheetController;
    private NotificationPermissionController mNotificationPermissionController;
    private HistoryNavigationCoordinator mHistoryNavigationCoordinator;
    private NavigationSheet mNavigationSheet;
    private LayoutManagerImpl mLayoutManager;
    private CommerceSubscriptionsService mCommerceSubscriptionsService;
    private UndoGroupSnackbarController mUndoGroupSnackbarController;
    private final int mControlContainerHeightResource;
    private final Supplier<InsetObserver> mInsetObserverViewSupplier;
    private final Function<Tab, Boolean> mBackButtonShouldCloseTabFn;
    private LayoutStateProvider.LayoutStateObserver mGestureNavLayoutObserver;
    private final ObservableSupplierImpl<EphemeralTabCoordinator> mEphemeralTabCoordinatorSupplier;
    private Callback<Integer> mOnTabStripHeightChangedCallback;
    private MultiInstanceManager mMultiInstanceManager;
    private int mStatusIndicatorHeight;
    private TouchEventObserver mDragDropTouchObserver;
    private final OneshotSupplier<HubManager> mHubManagerSupplier;

    /**
     * A common {@link CallbackController} used for being notified when {@link TabSwitcher} or
     * {@link StartSurface} is available.
     */
    private CallbackController mTabSwitcherCustomViewManagerCallbackController;

    // Activity tab observer that updates the current tab used by various UI components.
    private class RootUiTabObserver extends ActivityTabTabObserver {
        private Tab mTab;

        private RootUiTabObserver(ActivityTabProvider activityTabProvider) {
            super(activityTabProvider);
        }

        @Override
        public void onObservingDifferentTab(Tab tab, boolean hint) {
            swapToTab(tab);
        }

        private void swapToTab(Tab tab) {
            if (mTab != null && !mTab.isDestroyed()) {
                SwipeRefreshHandler.from(mTab).setNavigationCoordinator(null);
            }
            mTab = tab;

            if (tab != null) {
                SwipeRefreshHandler.from(tab)
                        .setNavigationCoordinator(mHistoryNavigationCoordinator);
            }
        }

        @Override
        public void destroy() {
            if (mLayoutStateProvider != null && mGestureNavLayoutObserver != null) {
                mLayoutStateProvider.removeObserver(mGestureNavLayoutObserver);
            }
            super.destroy();
            swapToTab(null);
        }
    }

    /**
     * Construct a new TabbedRootUiCoordinator.
     *
     * @param activity The activity whose UI the coordinator is responsible for.
     * @param onOmniboxFocusChangedListener callback to invoke when Omnibox focus changes.
     * @param shareDelegateSupplier Supplies the {@link ShareDelegate}.
     * @param tabProvider The {@link ActivityTabProvider} to get current tab of the activity.
     * @param profileSupplier Supplier of the currently applicable profile.
     * @param bookmarkModelSupplier Supplier of the bookmark bridge for the current profile.
     * @param contextualSearchManagerSupplier Supplier of the {@link ContextualSearchManager}.
     * @param tabModelSelectorSupplier Supplies the {@link TabModelSelector}.
     * @param startSurfaceSupplier Supplier of the {@link StartSurface}.
     * @param tabSwitcherSupplier Supplier of the {@link TabSwitcher}.
     * @param hubManagerSupplier Supplier for the {@link HubManager}.
     * @param incognitoTabSwitcherSupplier Supplier of the incognito {@link TabSwitcher}.
     * @param intentMetadataOneshotSupplier Supplier with information about the launching intent.
     * @param layoutStateProviderOneshotSupplier Supplier of the {@link LayoutStateProvider}.
     * @param startSurfaceParentTabSupplier Supplies the parent tab for the StartSurface.
     * @param browserControlsManager Manages the browser controls.
     * @param windowAndroid The current {@link WindowAndroid}.
     * @param activityLifecycleDispatcher Allows observation of the activity lifecycle.
     * @param layoutManagerSupplier Supplies the {@link LayoutManager}.
     * @param menuOrKeyboardActionController Controls the menu or keyboard action controller.
     * @param activityThemeColorSupplier Supplies the activity color theme.
     * @param modalDialogManagerSupplier Supplies the {@link ModalDialogManager}.
     * @param appMenuBlocker Controls the app menu blocking.
     * @param supportsAppMenuSupplier Supplies the support state for the app menu.
     * @param supportsFindInPage Supplies the support state for find in page.
     * @param tabCreatorManagerSupplier Supplies the {@link TabCreatorManager}.
     * @param fullscreenManager Manages the fullscreen state.
     * @param compositorViewHolderSupplier Supplies the {@link CompositorViewHolder}.
     * @param tabContentManagerSupplier Supplies the {@link TabContentManager}.
     * @param snackbarManagerSupplier Supplies the {@link SnackbarManager}.
     * @param activityType The {@link ActivityType} for the activity.
     * @param isInOverviewModeSupplier Supplies whether the app is in overview mode.
     * @param isWarmOnResumeSupplier Supplies whether the app was warm on resume.
     * @param appMenuDelegate The app menu delegate.
     * @param statusBarColorProvider Provides the status bar color.
     * @param ephemeralTabCoordinatorSupplier Supplies the {@link EphemeralTabCoordinator}.
     * @param intentRequestTracker Tracks intent requests.
     * @param controlContainerHeightResource The resource for the control container.
     * @param insetObserverViewSupplier Supplier for the {@link InsetObserver}.
     * @param backButtonShouldCloseTabFn Function which supplies whether or not the back button
     *     should close the tab.
     * @param tabReparentingControllerSupplier Supplier for the {@link TabReparentingController}.
     * @param initializeUiWithIncognitoColors Whether to initialize the UI with incognito colors.
     * @param backPressManager The {@link BackPressManager} handling back press.
     * @param savedInstanceState The saved bundle for the last recorded state.
     * @param multiInstanceManager Manages multi-instance mode.
     */
    public TabbedRootUiCoordinator(
            @NonNull AppCompatActivity activity,
            @Nullable Callback<Boolean> onOmniboxFocusChangedListener,
            @NonNull ObservableSupplier<ShareDelegate> shareDelegateSupplier,
            @NonNull ActivityTabProvider tabProvider,
            @NonNull ObservableSupplier<Profile> profileSupplier,
            @NonNull ObservableSupplier<BookmarkModel> bookmarkModelSupplier,
            @NonNull ObservableSupplier<TabBookmarker> tabBookmarkerSupplier,
            @NonNull Supplier<ContextualSearchManager> contextualSearchManagerSupplier,
            @NonNull ObservableSupplier<TabModelSelector> tabModelSelectorSupplier,
            @NonNull OneshotSupplier<StartSurface> startSurfaceSupplier,
            @NonNull OneshotSupplier<TabSwitcher> tabSwitcherSupplier,
            @NonNull OneshotSupplier<TabSwitcher> incognitoTabSwitcherSupplier,
            @NonNull OneshotSupplier<HubManager> hubManagerSupplier,
            @NonNull OneshotSupplier<ToolbarIntentMetadata> intentMetadataOneshotSupplier,
            @NonNull OneshotSupplier<LayoutStateProvider> layoutStateProviderOneshotSupplier,
            @NonNull Supplier<Tab> startSurfaceParentTabSupplier,
            @NonNull BrowserControlsManager browserControlsManager,
            @NonNull ActivityWindowAndroid windowAndroid,
            @NonNull ActivityLifecycleDispatcher activityLifecycleDispatcher,
            @NonNull ObservableSupplier<LayoutManagerImpl> layoutManagerSupplier,
            @NonNull MenuOrKeyboardActionController menuOrKeyboardActionController,
            @NonNull Supplier<Integer> activityThemeColorSupplier,
            @NonNull ObservableSupplier<ModalDialogManager> modalDialogManagerSupplier,
            @NonNull AppMenuBlocker appMenuBlocker,
            @NonNull BooleanSupplier supportsAppMenuSupplier,
            @NonNull BooleanSupplier supportsFindInPage,
            @NonNull Supplier<TabCreatorManager> tabCreatorManagerSupplier,
            @NonNull FullscreenManager fullscreenManager,
            @NonNull Supplier<CompositorViewHolder> compositorViewHolderSupplier,
            @NonNull Supplier<TabContentManager> tabContentManagerSupplier,
            @NonNull Supplier<SnackbarManager> snackbarManagerSupplier,
            @NonNull ObservableSupplierImpl<EdgeToEdgeController> edgeToEdgeSupplier,
            @ActivityType int activityType,
            @NonNull Supplier<Boolean> isInOverviewModeSupplier,
            @NonNull Supplier<Boolean> isWarmOnResumeSupplier,
            @NonNull AppMenuDelegate appMenuDelegate,
            @NonNull StatusBarColorProvider statusBarColorProvider,
            @NonNull
                    ObservableSupplierImpl<EphemeralTabCoordinator> ephemeralTabCoordinatorSupplier,
            @NonNull IntentRequestTracker intentRequestTracker,
            int controlContainerHeightResource,
            @NonNull Supplier<InsetObserver> insetObserverViewSupplier,
            @NonNull Function<Tab, Boolean> backButtonShouldCloseTabFn,
            OneshotSupplier<TabReparentingController> tabReparentingControllerSupplier,
            boolean initializeUiWithIncognitoColors,
            @NonNull BackPressManager backPressManager,
            @Nullable Bundle savedInstanceState,
            @Nullable MultiInstanceManager multiInstanceManager) {
        super(
                activity,
                onOmniboxFocusChangedListener,
                shareDelegateSupplier,
                tabProvider,
                profileSupplier,
                bookmarkModelSupplier,
                tabBookmarkerSupplier,
                contextualSearchManagerSupplier,
                tabModelSelectorSupplier,
                startSurfaceSupplier,
                tabSwitcherSupplier,
                incognitoTabSwitcherSupplier,
                intentMetadataOneshotSupplier,
                layoutStateProviderOneshotSupplier,
                startSurfaceParentTabSupplier,
                browserControlsManager,
                windowAndroid,
                activityLifecycleDispatcher,
                layoutManagerSupplier,
                menuOrKeyboardActionController,
                activityThemeColorSupplier,
                modalDialogManagerSupplier,
                appMenuBlocker,
                supportsAppMenuSupplier,
                supportsFindInPage,
                tabCreatorManagerSupplier,
                fullscreenManager,
                compositorViewHolderSupplier,
                tabContentManagerSupplier,
                snackbarManagerSupplier,
                edgeToEdgeSupplier,
                activityType,
                isInOverviewModeSupplier,
                isWarmOnResumeSupplier,
                appMenuDelegate,
                statusBarColorProvider,
                intentRequestTracker,
                tabReparentingControllerSupplier,
                ephemeralTabCoordinatorSupplier,
                initializeUiWithIncognitoColors,
                backPressManager,
                savedInstanceState);
        mControlContainerHeightResource = controlContainerHeightResource;
        mInsetObserverViewSupplier = insetObserverViewSupplier;
        mBackButtonShouldCloseTabFn = backButtonShouldCloseTabFn;
        mEphemeralTabCoordinatorSupplier = ephemeralTabCoordinatorSupplier;
        mCanAnimateBrowserControls =
                () -> {
                    // These null checks prevent any exceptions that may be caused by callbacks
                    // after destruction.
                    if (mActivity == null || mActivityTabProvider == null) return false;
                    final Tab tab = mActivityTabProvider.get();
                    return tab != null && tab.isUserInteractable() && !tab.isNativePage();
                };

        getAppBrowserControlsVisibilityDelegate()
                .addDelegate(browserControlsManager.getBrowserVisibilityDelegate());
        mRootUiTabObserver = new RootUiTabObserver(tabProvider);
        mGestureNavLayoutObserver =
                new LayoutStateProvider.LayoutStateObserver() {
                    @Override
                    public void onStartedShowing(int layoutType) {
                        if (layoutType == LayoutType.TAB_SWITCHER) {
                            mHistoryNavigationCoordinator.reset();
                        }
                    }
                };
        mMultiInstanceManager = multiInstanceManager;
        mHubManagerSupplier = hubManagerSupplier;
    }

    @Override
    public void onDestroy() {
        FeatureNotificationUtils.unregisterIPHCallback(FeatureType.DEFAULT_BROWSER);

        if (mSystemUiCoordinator != null) mSystemUiCoordinator.destroy();

        if (mOfflineIndicatorController != null) {
            mOfflineIndicatorController.destroy();
        }

        if (mToolbarManager != null) {
            mToolbarManager.getOmniboxStub().removeUrlFocusChangeListener(mUrlFocusChangeListener);
            if (mOnTabStripHeightChangedCallback != null) {
                mToolbarManager
                        .getTabStripHeightSupplier()
                        .removeObserver(mOnTabStripHeightChangedCallback);
                mOnTabStripHeightChangedCallback = null;
            }
        }

        if (mOfflineIndicatorInProductHelpController != null) {
            mOfflineIndicatorInProductHelpController.destroy();
        }
        if (mStatusIndicatorCoordinator != null) {
            mStatusIndicatorCoordinator.removeObserver(mStatusIndicatorObserver);
            mStatusIndicatorCoordinator.removeObserver(mStatusBarColorController);
            mStatusIndicatorCoordinator.destroy();
        }

        if (mToolbarButtonInProductHelpController != null) {
            mToolbarButtonInProductHelpController.destroy();
        }

        if (mWebFeedFollowIntroController != null) {
            mWebFeedFollowIntroController.destroy();
        }

        if (mRootUiTabObserver != null) mRootUiTabObserver.destroy();

        if (mAddToHomescreenIPHController != null) mAddToHomescreenIPHController.destroy();

        if (mAppBannerInProductHelpController != null) {
            AppBannerInProductHelpControllerFactory.detach(mAppBannerInProductHelpController);
        }

        if (mPwaBottomSheetController != null) {
            PwaBottomSheetControllerFactory.detach(mPwaBottomSheetController);
        }

        if (mHistoryNavigationCoordinator != null) {
            TouchEventObserver obs = mHistoryNavigationCoordinator.getTouchEventObserver();
            if (mCompositorViewHolderSupplier.hasValue() && obs != null) {
                mCompositorViewHolderSupplier.get().removeTouchEventObserver(obs);
            }
            mHistoryNavigationCoordinator.destroy();
            mHistoryNavigationCoordinator = null;
        }

        if (mUndoGroupSnackbarController != null) {
            mUndoGroupSnackbarController.destroy();
        }

        if (mCommerceSubscriptionsService != null) {
            mCommerceSubscriptionsService.destroy();
            mCommerceSubscriptionsService = null;
        }

        if (mNotificationPermissionController != null) {
            NotificationPermissionController.detach(mNotificationPermissionController);
            mNotificationPermissionController = null;
        }

        if (mTabSwitcherCustomViewManagerCallbackController != null) {
            mTabSwitcherCustomViewManagerCallbackController.destroy();
        }

        if (mDesktopSiteSettingsIPHController != null) {
            mDesktopSiteSettingsIPHController.destroy();
            mDesktopSiteSettingsIPHController = null;
        }

        if (mCompositorViewHolderSupplier.get() != null && mDragDropTouchObserver != null) {
            mCompositorViewHolderSupplier.get().removeTouchEventObserver(mDragDropTouchObserver);
            mDragDropTouchObserver = null;
        }

        super.onDestroy();
    }

    @Override
    public void onPostInflationStartup() {
        super.onPostInflationStartup();

        mSystemUiCoordinator =
                new TabbedSystemUiCoordinator(
                        mActivity.getWindow(),
                        mTabModelSelectorSupplier.get(),
                        mLayoutManagerSupplier,
                        mFullscreenManager);
    }

    @Override
    protected void onFindToolbarShown() {
        super.onFindToolbarShown();
        EphemeralTabCoordinator coordinator = mEphemeralTabCoordinatorSupplier.get();
        if (coordinator != null && coordinator.isOpened()) coordinator.close();
    }

    /** Show navigation history sheet. */
    public void showFullHistorySheet() {
        if (mActivity == null) return;
        Tab tab = mActivityTabProvider.get();
        if (tab == null || tab.getWebContents() == null || !tab.isUserInteractable()) return;
        Profile profile = tab.getProfile();
        mNavigationSheet =
                NavigationSheet.create(
                        mActivity.getWindow().getDecorView().findViewById(android.R.id.content),
                        mActivity,
                        this::getBottomSheetController,
                        profile);
        mNavigationSheet.setDelegate(
                new TabbedSheetDelegate(
                        tab,
                        aTab -> {
                            HistoryManagerUtils.showHistoryManager(
                                    mActivity,
                                    aTab,
                                    mTabModelSelectorSupplier.hasValue()
                                            && mTabModelSelectorSupplier
                                                    .get()
                                                    .isIncognitoSelected());
                        },
                        mActivity.getResources().getString(R.string.show_full_history)));
        if (!mNavigationSheet.startAndExpand(/* forward= */ false, /* animate= */ true)) {
            mNavigationSheet = null;
        } else {
            getBottomSheetController()
                    .addObserver(
                            new EmptyBottomSheetObserver() {
                                @Override
                                public void onSheetClosed(int reason) {
                                    getBottomSheetController().removeObserver(this);
                                    mNavigationSheet = null;
                                }
                            });
        }
    }

    @Override
    public void onFinishNativeInitialization() {
        super.onFinishNativeInitialization();
        assert mLayoutManager != null;

        // final Function<Tab, Boolean> backButtonShouldCloseTabFn = mBackButtonShouldCloseTabFn;
        mHistoryNavigationCoordinator =
                HistoryNavigationCoordinator.create(
                        mWindowAndroid,
                        mActivityLifecycleDispatcher,
                        mCompositorViewHolderSupplier.get(),
                        mCallbackController.makeCancelable(
                                () -> mLayoutManager.getActiveLayout().requestUpdate()),
                        mActivityTabProvider,
                        mInsetObserverViewSupplier.get(),
                        mStartSurfaceSupplier,
                        new BackActionDelegate() {
                            @Override
                            public @ActionType int getBackActionType(Tab tab) {
                                if (isShowingStartSurfaceHomepage()) return ActionType.EXIT_APP;
                                if (tab.canGoBack()
                                        || StartSurfaceUserData.isOpenedFromStart(tab)
                                        || tab.getLaunchType()
                                                == TabLaunchType.FROM_START_SURFACE) {
                                    return ActionType.NAVIGATE_BACK;
                                }
                                if (TabAssociatedApp.isOpenedFromExternalApp(tab)) {
                                    return ActionType.EXIT_APP;
                                }
                                return mBackButtonShouldCloseTabFn.apply(tab)
                                        ? ActionType.CLOSE_TAB
                                        : ActionType.EXIT_APP;
                            }

                            @Override
                            public void onBackGesture() {
                                // Back navigation gesture performs what the back button would do.
                                mActivity.onBackPressed();
                            }

                            @Override
                            public boolean isNavigable() {
                                return isShowingStartSurfaceHomepage();
                            }
                        },
                        () -> mCompositorViewHolderSupplier.get(),
                        mLayoutManager);
        mRootUiTabObserver.swapToTab(mActivityTabProvider.get());

        // TODO(crbug.com/1505851): Consider register this drag listener to other views besides CVH.
        if (ChromeFeatureList.sTabLinkDragDropAndroid.isEnabled()
                && !TabUiFeatureUtilities.DISABLE_STRIP_TO_CONTENT_DD.getValue()) {
            ChromeTabbedOnDragListener chromeTabbedOnDragListener =
                    new ChromeTabbedOnDragListener(
                            mMultiInstanceManager,
                            mTabModelSelectorSupplier.get(),
                            mWindowAndroid,
                            mLayoutStateProviderOneShotSupplier);

            mCompositorViewHolderSupplier.get().setOnDragListener(chromeTabbedOnDragListener);

            // Disable touch event while drag is in progress.
            // TODO(crbug.com/1519687): Move onTouchEventObserver to CoordinatorLayoutForPointer.
            mDragDropTouchObserver =
                    new TouchEventObserver() {
                        @Override
                        public boolean onInterceptTouchEvent(MotionEvent e) {
                            return DragDropGlobalState.hasValue();
                        }
                    };
            mCompositorViewHolderSupplier.get().addTouchEventObserver(mDragDropTouchObserver);
        }

        if (!DeviceFormFactor.isNonMultiDisplayContextOnTablet(mActivity)) {
            getToolbarManager().enableBottomControls();
        }

        if (EphemeralTabCoordinator.isSupported()) {
            Supplier<TabCreator> tabCreator =
                    () ->
                            mTabCreatorManagerSupplier
                                    .get()
                                    .getTabCreator(
                                            mTabModelSelectorSupplier.get().isIncognitoSelected());
            mEphemeralTabCoordinatorSupplier.set(
                    new EphemeralTabCoordinator(
                            mActivity,
                            mWindowAndroid,
                            mActivity.getWindow().getDecorView(),
                            mActivityTabProvider,
                            tabCreator,
                            getBottomSheetController(),
                            true));
        }

        SupplierUtils.waitForAll(
                mCallbackController.makeCancelable(
                        () -> {
                            initializeIPH(
                                    mProfileSupplier.get().getOriginalProfile(),
                                    mIntentMetadataOneshotSupplier.get().getIsIntentWithEffect());
                        }),
                mIntentMetadataOneshotSupplier,
                mProfileSupplier);

        // TODO(https://crbug.com/1157955): Investigate switching to per-Activity coordinator that
        // uses signals from the current Tab to decide when to show the PWA install bottom sheet
        // rather than relying on unowned user data.
        mPwaBottomSheetController =
                PwaBottomSheetControllerFactory.createPwaBottomSheetController(mActivity);
        PwaBottomSheetControllerFactory.attach(mWindowAndroid, mPwaBottomSheetController);
        initCommerceSubscriptionsService();
        initUndoGroupSnackbarController();
        initTabStripTransitionCoordinator();
    }

    /** Creates an instance of {@link IncognitoReauthCoordinatorFactory} for tabbed activity. */
    @Override
    protected IncognitoReauthCoordinatorFactory getIncognitoReauthCoordinatorFactory() {
        // TODO(crbug.com/1324211, crbug.com/1227656) : Refactor below to remove
        // IncognitoReauthTopToolbarDelegate and pass TopToolbarInteractabilityManager.
        IncognitoReauthTopToolbarDelegate incognitoReauthTopToolbarDelegate =
                new IncognitoReauthTopToolbarDelegate() {
                    @Override
                    public int disableNewTabButton() {
                        // Hub handles this at the pane level.
                        if (HubFieldTrial.isHubEnabled()) return 0;
                        return mToolbarManager
                                .getTopToolbarInteractabilityManager()
                                .disableNewTabButton();
                    }

                    @Override
                    public void enableNewTabButton(int clientToken) {
                        // Hub handles this at the pane level.
                        if (HubFieldTrial.isHubEnabled()) return;
                        mToolbarManager
                                .getTopToolbarInteractabilityManager()
                                .enableNewTabButton(clientToken);
                    }
                };

        IncognitoReauthCoordinatorFactory incognitoReauthCoordinatorFactory =
                new IncognitoReauthCoordinatorFactory(
                        mActivity,
                        mTabModelSelectorSupplier.get(),
                        mModalDialogManagerSupplier.get(),
                        new IncognitoReauthManager(),
                        new SettingsLauncherImpl(),
                        incognitoReauthTopToolbarDelegate,
                        mLayoutManager,
                        mHubManagerSupplier,
                        /* showRegularOverviewIntent= */ null,
                        /* isTabbedActivity= */ true);

        mTabSwitcherCustomViewManagerCallbackController = new CallbackController();
        mStartSurfaceSupplier.onAvailable(
                mTabSwitcherCustomViewManagerCallbackController.makeCancelable(
                        (startSurface) -> {
                            new OneShotCallback<>(
                                    startSurface.getTabSwitcherCustomViewManagerSupplier(),
                                    (tabSwitcherCustomViewManager) -> {
                                        if (!incognitoReauthCoordinatorFactory
                                                        .getTabSwitcherCustomViewManagerSupplier()
                                                        .hasValue()
                                                && tabSwitcherCustomViewManager != null) {
                                            incognitoReauthCoordinatorFactory
                                                    .setTabSwitcherCustomViewManager(
                                                            tabSwitcherCustomViewManager);
                                        }
                                    });
                        }));

        mIncognitoTabSwitcherSupplier.onAvailable(
                mTabSwitcherCustomViewManagerCallbackController.makeCancelable(
                        (tabSwitcher) -> {
                            var tabSwitcherCustomViewManager =
                                    tabSwitcher.getTabSwitcherCustomViewManager();
                            if (!incognitoReauthCoordinatorFactory
                                            .getTabSwitcherCustomViewManagerSupplier()
                                            .hasValue()
                                    && tabSwitcherCustomViewManager != null) {
                                incognitoReauthCoordinatorFactory.setTabSwitcherCustomViewManager(
                                        tabSwitcherCustomViewManager);
                            }
                        }));

        return incognitoReauthCoordinatorFactory;
    }

    @Override
    protected void setLayoutStateProvider(LayoutStateProvider layoutStateProvider) {
        super.setLayoutStateProvider(layoutStateProvider);
        if (mGestureNavLayoutObserver != null) {
            layoutStateProvider.addObserver(mGestureNavLayoutObserver);
        }
    }

    private boolean isShowingStartSurfaceHomepage() {
        return mStartSurfaceSupplier.get() != null && mStartSurfaceSupplier.get().isHomepageShown();
    }

    // Protected class methods

    @Override
    protected void onLayoutManagerAvailable(LayoutManagerImpl layoutManager) {
        super.onLayoutManagerAvailable(layoutManager);

        initStatusIndicatorCoordinator(layoutManager);
        mLayoutManager = layoutManager;
    }

    @Override
    protected boolean canShowMenuUpdateBadge() {
        return true;
    }

    @Override
    protected boolean shouldInitializeMerchantTrustSignals() {
        return true;
    }

    @Override
    protected ScrimCoordinator buildScrimWidget() {
        ViewGroup coordinator = mActivity.findViewById(R.id.coordinator);
        ScrimCoordinator.SystemUiScrimDelegate delegate =
                new ScrimCoordinator.SystemUiScrimDelegate() {
                    @Override
                    public void setScrimColor(int scrimColor) {
                        mStatusBarColorController.setScrimColor(scrimColor);
                    }

                    @Override
                    public void setStatusBarScrimFraction(float scrimFraction) {
                        mStatusBarColorController.setStatusBarScrimFraction(scrimFraction);
                    }

                    @Override
                    public void setNavigationBarScrimFraction(float scrimFraction) {
                        TabbedNavigationBarColorController controller =
                                mSystemUiCoordinator.getNavigationBarColorController();
                        if (controller == null) {
                            return;
                        }
                        controller.setNavigationBarScrimFraction(scrimFraction);
                    }
                };
        return new ScrimCoordinator(
                mActivity,
                delegate,
                coordinator,
                coordinator.getContext().getColor(R.color.omnibox_focused_fading_background_color));
    }

    // Private class methods

    private void initializeIPH(Profile profile, boolean intentWithEffect) {
        if (mActivity == null) return;
        mToolbarButtonInProductHelpController =
                new ToolbarButtonInProductHelpController(
                        mActivity,
                        mWindowAndroid,
                        mAppMenuCoordinator,
                        mActivityLifecycleDispatcher,
                        mProfileSupplier,
                        mActivityTabProvider,
                        mIsInOverviewModeSupplier,
                        mToolbarManager.getMenuButtonView(),
                        mToolbarManager.getSecurityIconView());
        mReadLaterIPHController =
                new ReadLaterIPHController(
                        mActivity,
                        getToolbarManager().getMenuButtonView(),
                        mAppMenuCoordinator.getAppMenuHandler());

        boolean didTriggerPromo = false;

        String histogramName =
                "Startup.Android.PrivacySandbox.DialogNotShownDueToTabLaunchedFromExternalApp";
        Tab tab = mActivityTabProvider.get();
        boolean isTabLaunchedFromExternalApp =
                tab != null && tab.getLaunchType() == TabLaunchType.FROM_EXTERNAL_APP;
        boolean shouldSuppressPSDialogForExternalAppLaunches =
                ChromeFeatureList.getFieldTrialParamByFeatureAsBoolean(
                        ChromeFeatureList.PRIVACY_SANDBOX_SETTINGS_4,
                        "suppress-dialog-for-external-app-launches",
                        true);
        boolean shouldSuppressPSDialog =
                isTabLaunchedFromExternalApp && shouldSuppressPSDialogForExternalAppLaunches;

        if (!shouldSuppressPSDialog) {
            didTriggerPromo =
                    PrivacySandboxDialogController.maybeLaunchPrivacySandboxDialog(
                            mActivity,
                            new SettingsLauncherImpl(),
                            mTabModelSelectorSupplier.get().isIncognitoSelected());
        }
        RecordHistogram.recordBooleanHistogram(histogramName, shouldSuppressPSDialog);

        if (!didTriggerPromo && TrackingProtectionNoticeController.shouldShowNotice()) {
            TrackingProtectionNoticeController.create(
                    mActivity,
                    mActivityTabProvider,
                    mMessageDispatcher,
                    new SettingsLauncherImpl());
            // Promo will be triggered eventually. We don't want for this promo to clash with other
            // promos in the same run.
            didTriggerPromo = true;
        }

        if (!didTriggerPromo) {
            Supplier<RationaleDelegate> rationaleUIDelegateSupplier;

            if (NotificationPermissionController.shouldUseBottomSheetRationaleUi()) {
                rationaleUIDelegateSupplier =
                        () ->
                                new NotificationPermissionRationaleBottomSheet(
                                        mActivity, getBottomSheetController());
            } else {
                rationaleUIDelegateSupplier =
                        () ->
                                new NotificationPermissionRationaleDialogController(
                                        mActivity, mModalDialogManagerSupplier.get());
            }

            mNotificationPermissionController =
                    new NotificationPermissionController(
                            mWindowAndroid, rationaleUIDelegateSupplier);

            NotificationPermissionController.attach(
                    mWindowAndroid, mNotificationPermissionController);

            didTriggerPromo =
                    mNotificationPermissionController.requestPermissionIfNeeded(
                            /* contextual= */ false);
        }

        if (!didTriggerPromo) {
            didTriggerPromo = FeatureNotificationUtils.willShowIPH(FeatureType.DEFAULT_BROWSER);
            FeatureNotificationUtils.registerIPHCallback(
                    FeatureType.DEFAULT_BROWSER,
                    () -> {
                        DefaultBrowserPromoUtils.prepareLaunchPromoIfNeeded(
                                mActivity, mWindowAndroid, /* ignoreMaxCount= */ true);
                    });
        }

        if (!didTriggerPromo) {
            didTriggerPromo = triggerPromo(profile, intentWithEffect);
        }

        if (!didTriggerPromo) {
            didTriggerPromo =
                    DeviceFormFactor.isWindowOnTablet(mWindowAndroid)
                            && RequestDesktopUtils.maybeShowGlobalSettingOptInMessage(
                                    getPrimaryDisplaySizeInInches(),
                                    profile,
                                    mMessageDispatcher,
                                    mActivity,
                                    mActivityTabProvider);
        }

        if (!didTriggerPromo) {
            didTriggerPromo =
                    RequestDesktopUtils.maybeShowDefaultEnableGlobalSettingMessage(
                            profile, mMessageDispatcher, mActivity);
        }

        if (!didTriggerPromo) {
            mToolbarButtonInProductHelpController.showColdStartIPH();
            mReadLaterIPHController.showColdStartIPH();
            if (MultiWindowUtils.instanceSwitcherEnabled()
                    && MultiWindowUtils.shouldShowManageWindowsMenu()) {
                MultiInstanceIphController.maybeShowInProductHelp(
                        mActivity,
                        getToolbarManager().getMenuButtonView(),
                        mAppMenuCoordinator.getAppMenuHandler(),
                        R.id.manage_all_windows_menu_id);
            }
            DesktopSiteSettingsIPHController.create(
                    mActivity,
                    mWindowAndroid,
                    mActivityTabProvider,
                    profile,
                    getToolbarManager().getMenuButtonView(),
                    mAppMenuCoordinator.getAppMenuHandler());
        }
        mPromoShownOneshotSupplier.set(didTriggerPromo);

        if (mOfflineIndicatorController != null) {
            // Initialize the OfflineIndicatorInProductHelpController if the
            // mOfflineIndicatorController is enabled and initialized. For example, it wouldn't be
            // initialized if the OfflineIndicatorV2 feature is disabled.
            assert mOfflineIndicatorInProductHelpController == null;
            mOfflineIndicatorInProductHelpController =
                    new OfflineIndicatorInProductHelpController(
                            mActivity,
                            mToolbarManager,
                            mAppMenuCoordinator.getAppMenuHandler(),
                            mStatusIndicatorCoordinator);
        }

        mAddToHomescreenIPHController =
                new AddToHomescreenIPHController(
                        mActivity,
                        mWindowAndroid,
                        mModalDialogManagerSupplier.get(),
                        MessageDispatcherProvider.from(mWindowAndroid));
        mLinkToTextIPHController =
                new LinkToTextIPHController(
                        mActivityTabProvider, mTabModelSelectorSupplier.get(), mProfileSupplier);
        mAddToHomescreenMostVisitedTileObserver =
                new AddToHomescreenMostVisitedTileClickObserver(
                        mActivityTabProvider, mAddToHomescreenIPHController);
        mAppBannerInProductHelpController =
                AppBannerInProductHelpControllerFactory.createAppBannerInProductHelpController(
                        mActivity,
                        mAppMenuCoordinator.getAppMenuHandler(),
                        () -> mToolbarManager.getMenuButtonView(),
                        R.id.add_to_homescreen_id);
        AppBannerInProductHelpControllerFactory.attach(
                mWindowAndroid, mAppBannerInProductHelpController);

        if (!didTriggerPromo
                && ChromeFeatureList.isEnabled(
                        ChromeFeatureList.DARKEN_WEBSITES_CHECKBOX_IN_THEMES_SETTING)) {
            // TODO(crbug.com/1252965): Investigate locking feature engagement system during
            // "second run promos" to avoid !didTriggerPromo check.
            WebContentsDarkModeMessageController.attemptToSendMessage(
                    mActivity,
                    tab != null ? tab.getProfile() : profile,
                    tab != null ? tab.getWebContents() : null,
                    new SettingsLauncherImpl(),
                    mMessageDispatcher);
        }

        if (ChromeFeatureList.isEnabled(ChromeFeatureList.WEB_FEED)) {
            mWebFeedFollowIntroController =
                    new WebFeedFollowIntroController(
                            mActivity,
                            mAppMenuCoordinator.getAppMenuHandler(),
                            mActivityTabProvider,
                            mToolbarManager.getMenuButtonView(),
                            () -> {
                                mTabCreatorManagerSupplier
                                        .get()
                                        .getTabCreator(/* incognito= */ false)
                                        .launchUrl(
                                                NewTabPageUtils.encodeNtpUrl(
                                                        NewTabPageLaunchOrigin.WEB_FEED),
                                                TabLaunchType.FROM_CHROME_UI);
                            },
                            mModalDialogManagerSupplier.get(),
                            mSnackbarManagerSupplier.get());
        }

        if (!didTriggerPromo && PageZoomCoordinator.shouldShowMenuItem()) {
            // Page Zoom IPH should only show if the menu item is visible, and not on NTP or CCT.
            if (tab != null && tab.getWebContents() != null && !tab.isNativePage()) {
                PageZoomIPHController mPageZoomIPHController =
                        new PageZoomIPHController(
                                mActivity,
                                mAppMenuCoordinator.getAppMenuHandler(),
                                mToolbarManager.getMenuButtonView());
                mPageZoomIPHController.showColdStartIPH();
            }
        }
    }

    private void updateTopControlsHeight() {
        if (mToolbarManager == null) return;

        final boolean animate = !sDisableTopControlsAnimationForTesting;
        final BrowserControlsSizer browserControlsSizer = mBrowserControlsManager;

        int topControlsNewHeight;
        if (ChromeFeatureList.sDynamicTopChrome.isEnabled()) {
            // This method can be called when the toolbar didn't go through a layout pass (e.g. when
            // theme switches in settings, activity recreates), so getToolbar().getHeight() returns
            // 0.
            // TODO(crbug.com/1503029): Remove the reference to toolbar_height_no_shadow.
            final int toolbarHeight =
                    mActivity
                            .getResources()
                            .getDimensionPixelSize(R.dimen.toolbar_height_no_shadow);
            final int tabStripHeight = mToolbarManager.getTabStripHeightSupplier().get();
            topControlsNewHeight = toolbarHeight + tabStripHeight + mStatusIndicatorHeight;

            boolean isTablet = DeviceFormFactor.isNonMultiDisplayContextOnTablet(mActivity);
            if (tabStripHeight > 0 && !isTablet) {
                String msg =
                        "Non-zero tab strip height found on non-tablet form factor. tabStripHeight="
                                + " "
                                + tabStripHeight
                                + " toolbarHeight= "
                                + toolbarHeight
                                + " statusIndicatorHeight= "
                                + mStatusIndicatorHeight;
                ChromePureJavaExceptionReporter.reportJavaException(
                        new Throwable(msg), /* withLogWarning= */ true);
            }
        } else {
            topControlsNewHeight =
                    mActivity.getResources().getDimensionPixelSize(mControlContainerHeightResource)
                            + mStatusIndicatorHeight;
        }

        browserControlsSizer.setAnimateBrowserControlsHeightChanges(animate);
        browserControlsSizer.setTopControlsHeight(topControlsNewHeight, mStatusIndicatorHeight);
        if (animate) browserControlsSizer.setAnimateBrowserControlsHeightChanges(false);
    }

    private void initCommerceSubscriptionsService() {
        SupplierUtils.waitForAll(
                mCallbackController.makeCancelable(
                        () -> {
                            mCommerceSubscriptionsService =
                                    CommerceSubscriptionsServiceFactory.getInstance()
                                            .getForProfile(mProfileSupplier.get());
                            mCommerceSubscriptionsService.initDeferredStartupForActivity(
                                    mTabModelSelectorSupplier.get(), mActivityLifecycleDispatcher);
                        }),
                mTabModelSelectorSupplier,
                mProfileSupplier);
    }

    private void initUndoGroupSnackbarController() {
        mUndoGroupSnackbarController =
                new UndoGroupSnackbarController(
                        mActivity, mTabModelSelectorSupplier.get(), mSnackbarManagerSupplier.get());
    }

    private void initStatusIndicatorCoordinator(LayoutManagerImpl layoutManager) {
        // TODO(crbug.com/1035584): Disable on tablets for now as we need to do one or two extra
        // things for tablets.
        if (DeviceFormFactor.isNonMultiDisplayContextOnTablet(mActivity)) {
            return;
        }

        final BrowserControlsSizer browserControlsSizer = mBrowserControlsManager;
        mStatusIndicatorCoordinator =
                new StatusIndicatorCoordinator(
                        mActivity,
                        mCompositorViewHolderSupplier.get().getResourceManager(),
                        browserControlsSizer,
                        mTabObscuringHandlerSupplier.get(),
                        mStatusBarColorController::getStatusBarColorWithoutStatusIndicator,
                        mCanAnimateBrowserControls,
                        layoutManager::requestUpdate);
        layoutManager.addSceneOverlay(mStatusIndicatorCoordinator.getSceneLayer());
        mStatusIndicatorObserver =
                new StatusIndicatorCoordinator.StatusIndicatorObserver() {
                    @Override
                    public void onStatusIndicatorHeightChanged(int indicatorHeight) {
                        mStatusIndicatorHeight = indicatorHeight;
                        updateTopControlsHeight();
                    }
                };
        mStatusIndicatorCoordinator.addObserver(mStatusIndicatorObserver);
        mStatusIndicatorCoordinator.addObserver(mStatusBarColorController);

        ObservableSupplierImpl<Boolean> isUrlBarFocusedSupplier = new ObservableSupplierImpl<>();
        isUrlBarFocusedSupplier.set(mToolbarManager.isUrlBarFocused());
        mUrlFocusChangeListener =
                new UrlFocusChangeListener() {
                    @Override
                    public void onUrlFocusChange(boolean hasFocus) {
                        // Offline indicator should assume the UrlBar is focused if it's focusing.
                        if (hasFocus) {
                            isUrlBarFocusedSupplier.set(true);
                        }
                    }

                    @Override
                    public void onUrlAnimationFinished(boolean hasFocus) {
                        // Wait for the animation to finish before notifying that UrlBar is
                        // unfocused.
                        if (!hasFocus) {
                            isUrlBarFocusedSupplier.set(false);
                        }
                    }
                };
        mOfflineIndicatorController =
                new OfflineIndicatorControllerV2(
                        mActivity,
                        mStatusIndicatorCoordinator,
                        isUrlBarFocusedSupplier,
                        mCanAnimateBrowserControls);
        if (mToolbarManager.getOmniboxStub() != null) {
            mToolbarManager.getOmniboxStub().addUrlFocusChangeListener(mUrlFocusChangeListener);
        }
    }

    private void initTabStripTransitionCoordinator() {
        // Tab strip transition is only supported for tablets.
        if (!DeviceFormFactor.isNonMultiDisplayContextOnTablet(mActivity)
                || !ChromeFeatureList.sDynamicTopChrome.isEnabled()) return;

        mOnTabStripHeightChangedCallback = (height) -> updateTopControlsHeight();
        mToolbarManager.getTabStripHeightSupplier().addObserver(mOnTabStripHeightChangedCallback);
    }

    @Override
    protected boolean supportsEdgeToEdge() {
        return EdgeToEdgeControllerFactory.isSupportedConfiguration(mActivity);
    }

    public StatusIndicatorCoordinator getStatusIndicatorCoordinatorForTesting() {
        return mStatusIndicatorCoordinator;
    }

    public HistoryNavigationCoordinator getHistoryNavigationCoordinatorForTesting() {
        return mHistoryNavigationCoordinator;
    }

    public NavigationSheet getNavigationSheetForTesting() {
        return mNavigationSheet;
    }

    /** Called when a link is copied through context menu. */
    public void onContextMenuCopyLink() {
        // TODO(crbug/1150090): Find a better way of passing event for IPH.
        mReadLaterIPHController.onCopyContextMenuItemClicked();
    }

    /**
     * Triggers the display of an appropriate promo, if any, returning true if a promo is actually
     * displayed.
     */
    private boolean triggerPromo(Profile profile, boolean intentWithEffect) {
        try (TraceEvent e = TraceEvent.scoped("TabbedRootUiCoordinator.triggerPromo")) {
            if (CommandLine.getInstance().hasSwitch(ChromeSwitches.DISABLE_STARTUP_PROMOS)) {
                return false;
            }

            SharedPreferencesManager preferenceManager = ChromeSharedPreferences.getInstance();
            // Promos can only be shown when we start with ACTION_MAIN intent and
            // after FRE is complete. Native initialization can finish before the FRE flow is
            // complete, and this will only show promos on the second opportunity. This is
            // because the FRE is shown on the first opportunity, and we don't want to show such
            // content back to back.
            //
            // TODO(https://crbug.com/865801, pnoland): Unify promo dialog logic and move into a
            // single PromoDialogCoordinator.
            boolean isShowingPromo =
                    LocaleManager.getInstance().hasShownSearchEnginePromoThisSession();
            if (!isShowingPromo
                    && !intentWithEffect
                    && FirstRunStatus.getFirstRunFlowComplete()
                    && preferenceManager.readBoolean(
                            ChromePreferenceKeys.PROMOS_SKIPPED_ON_FIRST_START, false)) {
                isShowingPromo = maybeShowPromo(profile);
            } else {
                preferenceManager.writeBoolean(
                        ChromePreferenceKeys.PROMOS_SKIPPED_ON_FIRST_START, true);
            }

            if (FirstRunStatus.isFirstRunTriggered()) {
                notifyPromosOfFirstRunTriggered();
            }

            return isShowingPromo;
        }
    }

    /** Notifies promos of the First Run Experience having triggered during this launch. */
    private void notifyPromosOfFirstRunTriggered() {
        PwaRestorePromoUtils.notifyFirstRunPromoTriggered();
    }

    private boolean maybeShowPromo(Profile profile) {
        // NOTE: Only one promo can be shown in one run to avoid nagging users too much.

        // The PWA Restore promotion runs when we've detected that a user has switched to a new
        // device but is leaving behind web apps on the old device. It promotes the idea that the
        // user can restore their web apps from their old device (if they have any), and as such it
        // is most effective when shown shortly after the first-run experience. It is therefore
        // at the front of the list of promotions.
        if (PwaRestorePromoUtils.launchPromoIfNeeded(
                profile, mWindowAndroid, R.drawable.ic_arrow_back_24dp)) {
            return true;
        }
        if (FullScreenSyncPromoUtil.launchPromoIfNeeded(
                mActivity,
                profile,
                SyncConsentActivityLauncherImpl.get(),
                VersionInfo.getProductMajorVersion())) {
            return true;
        }
        if (DefaultBrowserPromoUtils.prepareLaunchPromoIfNeeded(
                mActivity, mWindowAndroid, /* ignoreMaxCount= */ false)) {
            return true;
        }
        return AppLanguagePromoDialog.maybeShowPrompt(
                mActivity, mModalDialogManagerSupplier, () -> ApplicationLifetime.terminate(true));
    }

    public static void setDisableTopControlsAnimationsForTesting(boolean disable) {
        sDisableTopControlsAnimationForTesting = disable;
        ResettersForTesting.register(() -> sDisableTopControlsAnimationForTesting = false);
    }
}
