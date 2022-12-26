// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.app;

import android.annotation.SuppressLint;
import android.app.Activity;
import android.app.Fragment;
import android.app.SearchManager;
import android.app.assist.AssistContent;
import android.content.ActivityNotFoundException;
import android.content.Context;
import android.content.Intent;
import android.content.res.Configuration;
import android.graphics.drawable.ColorDrawable;
import android.graphics.drawable.Drawable;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.CancellationSignal;
import android.os.Handler;
import android.os.SystemClock;
import android.util.Pair;
import android.util.TypedValue;
import android.view.Display.Mode;
import android.view.MenuItem;
import android.view.View;
import android.view.ViewGroup;
import android.view.ViewStub;

import androidx.activity.OnBackPressedCallback;
import androidx.annotation.CallSuper;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.annotation.RequiresApi;
import androidx.annotation.VisibleForTesting;

import org.chromium.base.ActivityState;
import org.chromium.base.ApplicationStatus;
import org.chromium.base.BundleUtils;
import org.chromium.base.Callback;
import org.chromium.base.CommandLine;
import org.chromium.base.ContextUtils;
import org.chromium.base.Log;
import org.chromium.base.PowerMonitor;
import org.chromium.base.StrictModeContext;
import org.chromium.base.SysUtils;
import org.chromium.base.TraceEvent;
import org.chromium.base.annotations.UsedByReflection;
import org.chromium.base.jank_tracker.DummyJankTracker;
import org.chromium.base.metrics.RecordHistogram;
import org.chromium.base.metrics.RecordUserAction;
import org.chromium.base.supplier.ObservableSupplier;
import org.chromium.base.supplier.ObservableSupplierImpl;
import org.chromium.base.supplier.OneshotSupplier;
import org.chromium.base.supplier.OneshotSupplierImpl;
import org.chromium.base.supplier.Supplier;
import org.chromium.base.supplier.UnownedUserDataSupplier;
import org.chromium.chrome.R;
import org.chromium.chrome.browser.ActivityTabProvider;
import org.chromium.chrome.browser.ActivityUtils;
import org.chromium.chrome.browser.AppHooks;
import org.chromium.chrome.browser.ChromeActivitySessionTracker;
import org.chromium.chrome.browser.ChromeApplicationImpl;
import org.chromium.chrome.browser.ChromeKeyboardVisibilityDelegate;
import org.chromium.chrome.browser.ChromeWindow;
import org.chromium.chrome.browser.DeferredStartupHandler;
import org.chromium.chrome.browser.IntentHandler;
import org.chromium.chrome.browser.IntentHandler.IntentHandlerDelegate;
import org.chromium.chrome.browser.IntentHandler.TabOpenType;
import org.chromium.chrome.browser.PlayServicesVersionInfo;
import org.chromium.chrome.browser.WarmupManager;
import org.chromium.chrome.browser.app.appmenu.AppMenuPropertiesDelegateImpl;
import org.chromium.chrome.browser.app.download.DownloadMessageUiDelegate;
import org.chromium.chrome.browser.app.flags.ChromeCachedFlags;
import org.chromium.chrome.browser.app.metrics.LaunchCauseMetrics;
import org.chromium.chrome.browser.app.tab_activity_glue.ReparentingDelegateFactory;
import org.chromium.chrome.browser.app.tab_activity_glue.TabReparentingController;
import org.chromium.chrome.browser.app.tabmodel.AsyncTabParamsManagerSingleton;
import org.chromium.chrome.browser.app.tabmodel.TabModelOrchestrator;
import org.chromium.chrome.browser.back_press.BackPressManager;
import org.chromium.chrome.browser.bookmarks.BookmarkBridge;
import org.chromium.chrome.browser.bookmarks.BookmarkModel;
import org.chromium.chrome.browser.bookmarks.PowerBookmarkUtils;
import org.chromium.chrome.browser.bookmarks.TabBookmarker;
import org.chromium.chrome.browser.commerce.shopping_list.ShoppingFeatures;
import org.chromium.chrome.browser.compositor.CompositorViewHolder;
import org.chromium.chrome.browser.compositor.layouts.Layout;
import org.chromium.chrome.browser.compositor.layouts.LayoutManagerImpl;
import org.chromium.chrome.browser.compositor.layouts.SceneChangeObserver;
import org.chromium.chrome.browser.compositor.layouts.content.ContentOffsetProvider;
import org.chromium.chrome.browser.compositor.layouts.content.TabContentManager;
import org.chromium.chrome.browser.compositor.layouts.content.TabContentManagerHandler;
import org.chromium.chrome.browser.contextualsearch.ContextualSearchFieldTrial;
import org.chromium.chrome.browser.contextualsearch.ContextualSearchManager;
import org.chromium.chrome.browser.contextualsearch.ContextualSearchManager.ContextualSearchTabPromotionDelegate;
import org.chromium.chrome.browser.dependency_injection.ChromeActivityCommonsModule;
import org.chromium.chrome.browser.dependency_injection.ChromeActivityComponent;
import org.chromium.chrome.browser.dependency_injection.ModuleFactoryOverrides;
import org.chromium.chrome.browser.device.DeviceClassManager;
import org.chromium.chrome.browser.dom_distiller.DomDistillerUIUtils;
import org.chromium.chrome.browser.download.DownloadManagerService;
import org.chromium.chrome.browser.download.DownloadUtils;
import org.chromium.chrome.browser.download.items.OfflineContentAggregatorNotificationBridgeUiFactory;
import org.chromium.chrome.browser.feature_engagement.TrackerFactory;
import org.chromium.chrome.browser.feedback.HelpAndFeedbackLauncherImpl;
import org.chromium.chrome.browser.firstrun.ForcedSigninProcessor;
import org.chromium.chrome.browser.flags.ActivityType;
import org.chromium.chrome.browser.flags.CachedFeatureFlags;
import org.chromium.chrome.browser.flags.ChromeFeatureList;
import org.chromium.chrome.browser.flags.ChromeSessionState;
import org.chromium.chrome.browser.flags.ChromeSwitches;
import org.chromium.chrome.browser.fullscreen.BrowserControlsManager;
import org.chromium.chrome.browser.fullscreen.BrowserControlsManagerSupplier;
import org.chromium.chrome.browser.fullscreen.FullscreenBackPressHandler;
import org.chromium.chrome.browser.fullscreen.FullscreenManager;
import org.chromium.chrome.browser.gsa.ContextReporter;
import org.chromium.chrome.browser.gsa.GSAAccountChangeListener;
import org.chromium.chrome.browser.gsa.GSAContextDisplaySelection;
import org.chromium.chrome.browser.gsa.GSAState;
import org.chromium.chrome.browser.history.HistoryManagerUtils;
import org.chromium.chrome.browser.init.AsyncInitializationActivity;
import org.chromium.chrome.browser.init.ProcessInitializationHandler;
import org.chromium.chrome.browser.keyboard_accessory.ManualFillingComponent;
import org.chromium.chrome.browser.keyboard_accessory.ManualFillingComponentFactory;
import org.chromium.chrome.browser.keyboard_accessory.ManualFillingComponentSupplier;
import org.chromium.chrome.browser.layouts.LayoutManagerAppUtils;
import org.chromium.chrome.browser.locale.LocaleManager;
import org.chromium.chrome.browser.media.FullscreenVideoPictureInPictureController;
import org.chromium.chrome.browser.metrics.ActivityTabStartupMetricsTracker;
import org.chromium.chrome.browser.metrics.LaunchMetrics;
import org.chromium.chrome.browser.metrics.UmaSessionStats;
import org.chromium.chrome.browser.multiwindow.MultiWindowUtils;
import org.chromium.chrome.browser.night_mode.SystemNightModeMonitor;
import org.chromium.chrome.browser.night_mode.WebContentsDarkModeController;
import org.chromium.chrome.browser.night_mode.WebContentsDarkModeMessageController;
import org.chromium.chrome.browser.ntp.NewTabPageUma;
import org.chromium.chrome.browser.offlinepages.OfflinePageUtils;
import org.chromium.chrome.browser.offlinepages.indicator.OfflineIndicatorController;
import org.chromium.chrome.browser.omaha.UpdateMenuItemHelper;
import org.chromium.chrome.browser.page_info.ChromePageInfo;
import org.chromium.chrome.browser.page_info.ChromePageInfoHighlight;
import org.chromium.chrome.browser.partnercustomizations.PartnerBrowserCustomizations;
import org.chromium.chrome.browser.preferences.Pref;
import org.chromium.chrome.browser.printing.TabPrinter;
import org.chromium.chrome.browser.profiles.Profile;
import org.chromium.chrome.browser.read_later.ReadingListUtils;
import org.chromium.chrome.browser.selection.SelectionPopupBackPressHandler;
import org.chromium.chrome.browser.settings.SettingsLauncherImpl;
import org.chromium.chrome.browser.share.ShareDelegate;
import org.chromium.chrome.browser.share.ShareDelegateImpl;
import org.chromium.chrome.browser.share.ShareDelegateSupplier;
import org.chromium.chrome.browser.stylus_handwriting.StylusWritingCoordinator;
import org.chromium.chrome.browser.subscriptions.CommerceSubscriptionsServiceFactory;
import org.chromium.chrome.browser.subscriptions.SubscriptionsManager;
import org.chromium.chrome.browser.sync.SyncService;
import org.chromium.chrome.browser.tab.AccessibilityVisibilityHandler;
import org.chromium.chrome.browser.tab.RequestDesktopUtils;
import org.chromium.chrome.browser.tab.Tab;
import org.chromium.chrome.browser.tab.TabHidingType;
import org.chromium.chrome.browser.tab.TabLaunchType;
import org.chromium.chrome.browser.tab.TabSelectionType;
import org.chromium.chrome.browser.tab.TabState;
import org.chromium.chrome.browser.tab.TabUtils;
import org.chromium.chrome.browser.tab.TabUtils.UseDesktopUserAgentCaller;
import org.chromium.chrome.browser.tabmodel.EmptyTabModel;
import org.chromium.chrome.browser.tabmodel.TabCreator;
import org.chromium.chrome.browser.tabmodel.TabCreatorManager;
import org.chromium.chrome.browser.tabmodel.TabCreatorManagerSupplier;
import org.chromium.chrome.browser.tabmodel.TabModel;
import org.chromium.chrome.browser.tabmodel.TabModelInitializer;
import org.chromium.chrome.browser.tabmodel.TabModelSelector;
import org.chromium.chrome.browser.tabmodel.TabModelSelectorProfileSupplier;
import org.chromium.chrome.browser.tabmodel.TabModelSelectorSupplier;
import org.chromium.chrome.browser.tabmodel.TabModelSelectorTabObserver;
import org.chromium.chrome.browser.tabmodel.TabModelUtils;
import org.chromium.chrome.browser.tasks.ReturnToChromeUtil;
import org.chromium.chrome.browser.toolbar.ControlContainer;
import org.chromium.chrome.browser.toolbar.ToolbarManager;
import org.chromium.chrome.browser.translate.TranslateAssistContent;
import org.chromium.chrome.browser.translate.TranslateBridge;
import org.chromium.chrome.browser.ui.BottomContainer;
import org.chromium.chrome.browser.ui.RootUiCoordinator;
import org.chromium.chrome.browser.ui.TabObscuringHandler;
import org.chromium.chrome.browser.ui.appmenu.AppMenuBlocker;
import org.chromium.chrome.browser.ui.appmenu.AppMenuDelegate;
import org.chromium.chrome.browser.ui.appmenu.AppMenuPropertiesDelegate;
import org.chromium.chrome.browser.ui.messages.snackbar.Snackbar;
import org.chromium.chrome.browser.ui.messages.snackbar.SnackbarManager;
import org.chromium.chrome.browser.ui.messages.snackbar.SnackbarManager.SnackbarManageable;
import org.chromium.chrome.browser.ui.messages.snackbar.SnackbarManagerProvider;
import org.chromium.chrome.browser.ui.system.StatusBarColorController;
import org.chromium.chrome.browser.vr.ArDelegateProvider;
import org.chromium.chrome.browser.vr.VrModuleProvider;
import org.chromium.components.bookmarks.BookmarkId;
import org.chromium.components.browser_ui.accessibility.FontSizePrefs;
import org.chromium.components.browser_ui.bottomsheet.BottomSheetController;
import org.chromium.components.browser_ui.modaldialog.AppModalPresenter;
import org.chromium.components.browser_ui.notifications.NotificationManagerProxy;
import org.chromium.components.browser_ui.notifications.NotificationManagerProxyImpl;
import org.chromium.components.browser_ui.settings.SettingsLauncher;
import org.chromium.components.browser_ui.widget.InsetObserverView;
import org.chromium.components.browser_ui.widget.InsetObserverViewSupplier;
import org.chromium.components.browser_ui.widget.MenuOrKeyboardActionController;
import org.chromium.components.browser_ui.widget.gesture.BackPressHandler;
import org.chromium.components.browser_ui.widget.gesture.BackPressHandler.Type;
import org.chromium.components.browser_ui.widget.gesture.SwipeGestureListener.SwipeHandler;
import org.chromium.components.browser_ui.widget.textbubble.TextBubble;
import org.chromium.components.browser_ui.widget.textbubble.TextBubbleBackPressHandler;
import org.chromium.components.embedder_support.util.UrlConstants;
import org.chromium.components.embedder_support.util.UrlUtilities;
import org.chromium.components.feature_engagement.EventConstants;
import org.chromium.components.feature_engagement.Tracker;
import org.chromium.components.page_info.PageInfoController.OpenedFromSource;
import org.chromium.components.policy.CombinedPolicyProvider;
import org.chromium.components.policy.CombinedPolicyProvider.PolicyChangeListener;
import org.chromium.components.profile_metrics.BrowserProfileType;
import org.chromium.components.sync.ModelType;
import org.chromium.components.sync.PassphraseType;
import org.chromium.components.user_prefs.UserPrefs;
import org.chromium.components.webapk.lib.client.WebApkValidator;
import org.chromium.components.webapps.AddToHomescreenCoordinator;
import org.chromium.components.webapps.InstallTrigger;
import org.chromium.components.webapps.bottomsheet.PwaBottomSheetController;
import org.chromium.components.webapps.bottomsheet.PwaBottomSheetControllerProvider;
import org.chromium.components.webxr.ArDelegate;
import org.chromium.content_public.browser.ContentFeatureList;
import org.chromium.content_public.browser.LoadUrlParams;
import org.chromium.content_public.browser.ScreenOrientationProvider;
import org.chromium.content_public.browser.SelectionPopupController;
import org.chromium.content_public.browser.WebContents;
import org.chromium.content_public.common.ContentSwitches;
import org.chromium.printing.PrintManagerDelegateImpl;
import org.chromium.printing.PrintingController;
import org.chromium.printing.PrintingControllerImpl;
import org.chromium.ui.UiUtils;
import org.chromium.ui.base.ActivityWindowAndroid;
import org.chromium.ui.base.Clipboard;
import org.chromium.ui.base.DeviceFormFactor;
import org.chromium.ui.base.PageTransition;
import org.chromium.ui.base.WindowAndroid;
import org.chromium.ui.display.DisplayAndroid;
import org.chromium.ui.display.DisplayAndroid.DisplayAndroidObserver;
import org.chromium.ui.display.DisplayUtil;
import org.chromium.ui.modaldialog.ModalDialogManager;
import org.chromium.ui.widget.Toast;
import org.chromium.url.GURL;
import org.chromium.webapk.lib.client.WebApkNavigationClient;

import java.util.ArrayList;
import java.util.List;
import java.util.function.Consumer;

/**
 * A {@link AsyncInitializationActivity} that builds and manages a {@link CompositorViewHolder}
 * and associated classes.
 * @param <C> - type of associated Dagger component.
 */
public abstract class ChromeActivity<C extends ChromeActivityComponent>
        extends AsyncInitializationActivity
        implements TabCreatorManager, PolicyChangeListener, ContextualSearchTabPromotionDelegate,
                   SnackbarManageable, SceneChangeObserver,
                   StatusBarColorController.StatusBarColorProvider, AppMenuDelegate, AppMenuBlocker,
                   MenuOrKeyboardActionController, CompositorViewHolder.Initializer,
                   TabModelInitializer {
    private static final String TAG = "ChromeActivity";
    private C mComponent;

    /** Used to access the {@link ShareDelegate} from {@link WindowAndroid}. */
    private final UnownedUserDataSupplier<ShareDelegate> mShareDelegateSupplier =
            new ShareDelegateSupplier();
    private final ObservableSupplierImpl<TabModelOrchestrator> mTabModelOrchestratorSupplier =
            new ObservableSupplierImpl<>();
    /** Used to access the {@link TabModelSelector} from {@link WindowAndroid}. */
    private final UnownedUserDataSupplier<TabModelSelector> mTabModelSelectorSupplier =
            new TabModelSelectorSupplier();
    /** Used to access the {@link TabCreatorManager} from {@link WindowAndroid}. */
    private final UnownedUserDataSupplier<TabCreatorManager> mTabCreatorManagerSupplier =
            new TabCreatorManagerSupplier();
    private final UnownedUserDataSupplier<ManualFillingComponent> mManualFillingComponentSupplier =
            new ManualFillingComponentSupplier();
    // TODO(crbug.com/1209864): Move ownership to RootUiCoordinator.
    private final UnownedUserDataSupplier<BrowserControlsManager> mBrowserControlsManagerSupplier =
            new BrowserControlsManagerSupplier();

    protected TabModelSelectorProfileSupplier mTabModelProfileSupplier =
            new TabModelSelectorProfileSupplier(mTabModelSelectorSupplier);
    protected ObservableSupplierImpl<BookmarkBridge> mBookmarkBridgeSupplier =
            new ObservableSupplierImpl<>();
    protected ObservableSupplierImpl<TabBookmarker> mTabBookmarkerSupplier =
            new ObservableSupplierImpl<>();
    private TabModelOrchestrator mTabModelOrchestrator;
    private TabModelSelectorTabObserver mTabModelSelectorTabObserver;

    private ObservableSupplierImpl<TabContentManager> mTabContentManagerSupplier =
            new ObservableSupplierImpl<>();
    private TabContentManager mTabContentManager;

    private UmaSessionStats mUmaSessionStats;
    private ContextReporter mContextReporter;

    private boolean mPartnerBrowserRefreshNeeded;

    protected final IntentHandler mIntentHandler;

    /** Set if {@link #postDeferredStartupIfNeeded()} is called before native has loaded. */
    private boolean mDeferredStartupQueued;

    /** Whether or not {@link #postDeferredStartupIfNeeded()} has already successfully run. */
    private boolean mDeferredStartupPosted;

    private boolean mNativeInitialized;
    private boolean mRemoveWindowBackgroundDone;
    protected AccessibilityVisibilityHandler mAccessibilityVisibilityHandler;

    // Observes when sync becomes ready to create the mContextReporter.
    private SyncService.SyncStateChangedListener mSyncStateChangedListener;

    // The FullscreenVideoPictureInPictureController is initialized lazily https://crbug.com/729738.
    private FullscreenVideoPictureInPictureController mFullscreenVideoPictureInPictureController;

    private ObservableSupplierImpl<CompositorViewHolder> mCompositorViewHolderSupplier =
            new ObservableSupplierImpl<>();
    private ObservableSupplierImpl<LayoutManagerImpl> mLayoutManagerSupplier =
            new ObservableSupplierImpl<>();
    protected final UnownedUserDataSupplier<InsetObserverView> mInsetObserverViewSupplier =
            new InsetObserverViewSupplier();
    private final ObservableSupplierImpl<ContextualSearchManager> mContextualSearchManagerSupplier =
            new ObservableSupplierImpl<>();

    private SnackbarManager mSnackbarManager;

    // Timestamp in ms when initial layout inflation begins
    private long mInflateInitialLayoutBeginMs;
    // Timestamp in ms when initial layout inflation ends
    private long mInflateInitialLayoutEndMs;

    /** Whether or not a PolicyChangeListener was added. */
    private boolean mDidAddPolicyChangeListener;

    private ActivityTabStartupMetricsTracker mActivityTabStartupMetricsTracker;

    /** A means of providing the foreground tab of the activity to different features. */
    private ActivityTabProvider mActivityTabProvider = new ActivityTabProvider();

    /** Whether or not the activity is in started state. */
    private boolean mStarted;

    /** The data associated with the most recently selected menu item. */
    @Nullable
    private Bundle mMenuItemData;

    /**
     * The current configuration, used to for diffing when the configuration is changed.
     */
    private Configuration mConfig;

    /**
     * Supplier of the instance to control the tab-reparenting tasks.
     */
    private OneshotSupplierImpl<TabReparentingController> mTabReparentingControllerSupplier =
            new OneshotSupplierImpl<>();

    /**
     * Track whether {@link #mTabReparentingController} has prepared tab reparenting.
     */
    private boolean mIsTabReparentingPrepared;

    /**
     * Listen to display change and start tab-reparenting if necessary.
     */
    private DisplayAndroidObserver mDisplayAndroidObserver;

    /**
     * The RootUiCoordinator associated with the activity. This variable is held to facilitate
     * testing.
     * TODO(pnoland, https://crbug.com/865801): make this private again.
     */
    protected RootUiCoordinator mRootUiCoordinator;

    @Nullable
    private BottomContainer mBottomContainer;

    private LaunchCauseMetrics mLaunchCauseMetrics;

    private GSAAccountChangeListener mGSAAccountChangeListener;

    // TODO(972867): Pull MenuOrKeyboardActionController out of ChromeActivity.
    private List<MenuOrKeyboardActionController.MenuOrKeyboardActionHandler> mMenuActionHandlers =
            new ArrayList<>();

    // Whether this Activity is in Picture in Picture mode, based on the most recent call to
    // {@link onPictureInPictureModeChanged} from the platform.  This might disagree with the value
    // returned by {@link isInPictureInPictureMode}.
    private boolean mLastPictureInPictureModeForTesting;

    protected BackPressManager mBackPressManager = new BackPressManager();
    private TextBubbleBackPressHandler mTextBubbleBackPressHandler;
    private SelectionPopupBackPressHandler mSelectionPopupBackPressHandler;
    private Callback<TabModelSelector> mSelectionPopupBackPressInitCallback;
    private StylusWritingCoordinator mStylusWritingCoordinator;

    protected ChromeActivity() {
        mIntentHandler = new IntentHandler(this, createIntentHandlerDelegate());
        mManualFillingComponentSupplier.set(ManualFillingComponentFactory.createComponent());
    }

    @Override
    protected void onPreCreate() {
        CachedFeatureFlags.onStartOrResumeCheckpoint();
        super.onPreCreate();
        initializeBackPressHandling();
    }

    @Override
    protected void onAbortCreate() {
        super.onAbortCreate();
        CachedFeatureFlags.onPauseCheckpoint();
    }

    @Override
    protected ActivityWindowAndroid createWindowAndroid() {
        return new ChromeWindow(/* activity= */ this, mActivityTabProvider,
                mCompositorViewHolderSupplier, getModalDialogManagerSupplier(),
                mManualFillingComponentSupplier, getIntentRequestTracker());
    }

    @Override
    public boolean onIntentCallbackNotFoundError(String error) {
        createWindowErrorSnackbar(error, mSnackbarManager);
        return true;
    }

    @VisibleForTesting
    public static void createWindowErrorSnackbar(String error, SnackbarManager snackbarManager) {
        if (snackbarManager != null) {
            Snackbar snackbar = Snackbar.make(
                    error, null, Snackbar.TYPE_NOTIFICATION, Snackbar.UMA_WINDOW_ERROR);
            snackbar.setSingleLine(false);
            snackbar.setDuration(SnackbarManager.DEFAULT_SNACKBAR_DURATION_LONG_MS);
            snackbarManager.showSnackbar(snackbar);
        }
    }

    @Override
    public void performPreInflationStartup() {
        setupUnownedUserDataSuppliers();

        // Ensure that mConfig is initialized before tablet mode changes.
        mConfig = getResources().getConfiguration();

        // Make sure the root coordinator is created prior to calling super to ensure all
        // the activity lifecycle events are called.
        mRootUiCoordinator = createRootUiCoordinator();

        mStylusWritingCoordinator = new StylusWritingCoordinator(
                this, getLifecycleDispatcher(), getActivityTabProvider());

        // Create component before calling super to give its members a chance to catch
        // onPreInflationStartup event.
        mComponent = createComponent();

        // Create the orchestrator that manages Tab models and persistence
        mTabModelOrchestrator = createTabModelOrchestrator();
        mTabModelOrchestratorSupplier.set(mTabModelOrchestrator);

        // There's no corresponding call to removeObserver() for this addObserver() because
        // mTabModelProfileSupplier has the same lifecycle as this activity.
        mTabModelProfileSupplier.addObserver((profile) -> {
            BookmarkBridge oldBridge = mBookmarkBridgeSupplier.get();
            if (oldBridge != null) oldBridge.destroy();
            mBookmarkBridgeSupplier.set(profile == null ? null : new BookmarkBridge(profile));
        });

        super.performPreInflationStartup();

        VrModuleProvider.getDelegate().doPreInflationStartup(this, getSavedInstanceState());

        // Force a partner customizations refresh if it has yet to be initialized.  This can happen
        // if Chrome is killed and you refocus a previous activity from Android recents, which does
        // not go through ChromeLauncherActivity that would have normally triggered this.
        mPartnerBrowserRefreshNeeded = !PartnerBrowserCustomizations.getInstance().isInitialized();

        CommandLine commandLine = CommandLine.getInstance();
        if (!commandLine.hasSwitch(ChromeSwitches.DISABLE_FULLSCREEN)) {
            TypedValue threshold = new TypedValue();
            getResources().getValue(R.dimen.top_controls_show_threshold, threshold, true);
            commandLine.appendSwitchWithValue(ContentSwitches.TOP_CONTROLS_SHOW_THRESHOLD,
                    threshold.coerceToString().toString());
            getResources().getValue(R.dimen.top_controls_hide_threshold, threshold, true);
            commandLine.appendSwitchWithValue(ContentSwitches.TOP_CONTROLS_HIDE_THRESHOLD,
                    threshold.coerceToString().toString());
        }

        getWindow().setBackgroundDrawable(getBackgroundDrawable());
    }

    private void setupUnownedUserDataSuppliers() {
        mShareDelegateSupplier.attach(getWindowAndroid().getUnownedUserDataHost());
        mTabModelSelectorSupplier.attach(getWindowAndroid().getUnownedUserDataHost());
        mTabCreatorManagerSupplier.attach(getWindowAndroid().getUnownedUserDataHost());
        mManualFillingComponentSupplier.attach(getWindowAndroid().getUnownedUserDataHost());
        mInsetObserverViewSupplier.attach(getWindowAndroid().getUnownedUserDataHost());
        mBrowserControlsManagerSupplier.attach(getWindowAndroid().getUnownedUserDataHost());
        // BrowserControlsManager is ready immediately.
        mBrowserControlsManagerSupplier.set(
                new BrowserControlsManager(this, BrowserControlsManager.ControlsPosition.TOP));
    }

    protected RootUiCoordinator createRootUiCoordinator() {
        // TODO(https://crbug.com/931496): Remove dependency on ChromeActivity in favor of passing
        // in direct dependencies on needed classes. While migrating code from Chrome*Activity
        // to the RootUiCoordinator, passing the activity is an easy way to get access to a
        // number of objects that will ultimately be owned by the RootUiCoordinator. This is not
        // a recommended pattern.
        // clang-format off
        return new RootUiCoordinator(this, null, getShareDelegateSupplier(),
                getActivityTabProvider(), mTabModelProfileSupplier, mBookmarkBridgeSupplier,
                mTabBookmarkerSupplier, getContextualSearchManagerSupplier(),
                getTabModelSelectorSupplier(), new OneshotSupplierImpl<>(),
                new OneshotSupplierImpl<>(), new OneshotSupplierImpl<>(),
                new OneshotSupplierImpl<>(), () -> null, mBrowserControlsManagerSupplier.get(),
                getWindowAndroid(), new DummyJankTracker(), getLifecycleDispatcher(),
                getLayoutManagerSupplier(), /* menuOrKeyboardActionController= */ this,
                this::getActivityThemeColor, getModalDialogManagerSupplier(),
                /* appMenuBlocker= */ this, this::supportsAppMenu, this::supportsFindInPage,
                mTabCreatorManagerSupplier, getFullscreenManager(), mCompositorViewHolderSupplier,
                getTabContentManagerSupplier(), this::getSnackbarManager, getActivityType(),
                this::isInOverviewMode, this::isWarmOnResume, /* appMenuDelegate= */ this,
                /* statusBarColorProvider= */ this, getIntentRequestTracker(),
                mTabReparentingControllerSupplier,
                /*ephemeralTabCoordinatorSupplier=*/new ObservableSupplierImpl<>(),
                false, mBackPressManager);
        // clang-format on
    }

    private NotificationManagerProxy getNotificationManagerProxy() {
        return new NotificationManagerProxyImpl(getApplicationContext());
    }

    private C createComponent() {
        ChromeActivityCommonsModule.Factory overridenCommonsFactory =
                ModuleFactoryOverrides.getOverrideFor(ChromeActivityCommonsModule.Factory.class);

        ChromeActivityCommonsModule commonsModule = overridenCommonsFactory == null
                ? new ChromeActivityCommonsModule(this,
                        mRootUiCoordinator::getBottomSheetController, getTabModelSelectorSupplier(),
                        getBrowserControlsManager(), getBrowserControlsManager(),
                        getBrowserControlsManager(), getFullscreenManager(),
                        getLayoutManagerSupplier(), getLifecycleDispatcher(),
                        this::getSnackbarManager, mActivityTabProvider, getTabContentManager(),
                        getWindowAndroid(), mCompositorViewHolderSupplier, this,
                        this::getCurrentTabCreator, this::isCustomTab,
                        mRootUiCoordinator.getStatusBarColorController(),
                        ScreenOrientationProvider.getInstance(), this::getNotificationManagerProxy,
                        getTabContentManagerSupplier(), this::getActivityTabStartupMetricsTracker,
                        /* CompositorViewHolder.Initializer */ this,
                        /* ChromeActivityNativeDelegate */ this, getModalDialogManagerSupplier(),
                        getBrowserControlsManager(), this::getSavedInstanceState,
                        mManualFillingComponentSupplier.get().getBottomInsetSupplier(),
                        getShareDelegateSupplier(), /* tabModelInitializer= */ this,
                        getActivityType())
                : overridenCommonsFactory.create(this, mRootUiCoordinator::getBottomSheetController,
                        getTabModelSelectorSupplier(), getBrowserControlsManager(),
                        getBrowserControlsManager(), getBrowserControlsManager(),
                        getFullscreenManager(), getLayoutManagerSupplier(),
                        getLifecycleDispatcher(), this::getSnackbarManager, mActivityTabProvider,
                        getTabContentManager(), getWindowAndroid(), mCompositorViewHolderSupplier,
                        this, this::getCurrentTabCreator, this::isCustomTab,
                        mRootUiCoordinator.getStatusBarColorController(),
                        ScreenOrientationProvider.getInstance(), this::getNotificationManagerProxy,
                        getTabContentManagerSupplier(), this::getActivityTabStartupMetricsTracker,
                        /* CompositorViewHolder.Initializer */ this,
                        /* ChromeActivityNativeDelegate */ this, getModalDialogManagerSupplier(),
                        getBrowserControlsManager(), this::getSavedInstanceState,
                        mManualFillingComponentSupplier.get().getBottomInsetSupplier(),
                        getShareDelegateSupplier(), /* tabModelInitializer= */ this,
                        getActivityType());

        return createComponent(commonsModule);
    }

    /**
     * Override this to create a component that represents a richer dependency graph for a
     * particular subclass of ChromeActivity. The specialized component should be activity-scoped
     * and include all modules for ChromeActivityComponent, such as
     * {@link ChromeActivityCommonsModule}, along with any additional modules.
     *
     * You may immediately resolve some of the classes belonging to the component in this method.
     */
    @SuppressWarnings("unchecked")
    protected C createComponent(ChromeActivityCommonsModule commonsModule) {
        return (C) ChromeApplicationImpl.getComponent().createChromeActivityComponent(
                commonsModule);
    }

    /**
     * @return the activity-scoped component associated with this instance of activity.
     */
    public final C getComponent() {
        return mComponent;
    }

    @SuppressLint("NewApi")
    @Override
    public void performPostInflationStartup() {
        try (TraceEvent te = TraceEvent.scoped("ChromeActivity.performPostInflationStartup")) {
            super.performPostInflationStartup();

            Intent intent = getIntent();
            if (getSavedInstanceState() == null) {
                VrModuleProvider.getDelegate().maybeHandleVrIntentPreNative(this, intent);
            }
            if (0 != (intent.getFlags() & Intent.FLAG_ACTIVITY_LAUNCHED_FROM_HISTORY)) {
                getLaunchCauseMetrics().onLaunchFromRecents();
            } else {
                getLaunchCauseMetrics().onReceivedIntent();
            }

            mBottomContainer = (BottomContainer) findViewById(R.id.bottom_container);

            // TODO(crbug.com/1199776): Move this to the RootUiCoordinator.
            mSnackbarManager = new SnackbarManager(this, mBottomContainer, getWindowAndroid());
            SnackbarManagerProvider.attach(getWindowAndroid(), mSnackbarManager);

            // Make the activity listen to policy change events
            CombinedPolicyProvider.get().addPolicyChangeListener(this);
            mDidAddPolicyChangeListener = true;

            // Set up the animation placeholder to be the SurfaceView. This disables the
            // SurfaceView's 'hole' clipping during animations that are notified to the window.
            getWindowAndroid().setAnimationPlaceholderView(
                    mCompositorViewHolderSupplier.get().getCompositorView());

            initializeTabModels();
            TabModelSelector tabModelSelector = mTabModelOrchestrator.getTabModelSelector();
            setTabContentManager(new TabContentManager(this, getContentOffsetProvider(),
                    !SysUtils.isLowEndDevice(),
                    tabModelSelector != null ? tabModelSelector::getTabById : null));

            if (!isFinishing()) {
                getBrowserControlsManager().initialize(
                        (ControlContainer) findViewById(R.id.control_container),
                        getActivityTabProvider(), getTabModelSelector(),
                        getControlContainerHeightResource());
            }

            mBottomContainer.initialize(getBrowserControlsManager(),
                    getWindowAndroid().getApplicationBottomInsetProvider(),
                    mManualFillingComponentSupplier.get().getBottomInsetSupplier());

            ShareDelegate shareDelegate =
                    new ShareDelegateImpl(mRootUiCoordinator.getBottomSheetController(),
                            getLifecycleDispatcher(), getActivityTabProvider(),
                            getTabModelSelectorSupplier(), mTabModelProfileSupplier,
                            new ShareDelegateImpl.ShareSheetDelegate(), isCustomTab());
            mShareDelegateSupplier.set(shareDelegate);
            TabBookmarker tabBookmarker = new TabBookmarker(this, mBookmarkBridgeSupplier,
                    mRootUiCoordinator::getBottomSheetController, this::getSnackbarManager,
                    isCustomTab());
            mTabBookmarkerSupplier.set(tabBookmarker);

            // If onStart was called before postLayoutInflation (because inflation was done in a
            // background thread) then make sure to call the relevant methods belatedly.
            if (mStarted) {
                mCompositorViewHolderSupplier.get().onStart();
            }
        }
    }

    @Override
    protected void initializeStartupMetrics() {
        // Initialize the activity session tracker as early as possible so that
        // it can start background tasks.
        ChromeActivitySessionTracker chromeActivitySessionTracker =
                ChromeActivitySessionTracker.getInstance();
        chromeActivitySessionTracker.registerTabModelSelectorSupplier(
                this, mTabModelSelectorSupplier);
        mActivityTabStartupMetricsTracker =
                new ActivityTabStartupMetricsTracker(mTabModelSelectorSupplier);
    }

    public ActivityTabStartupMetricsTracker getActivityTabStartupMetricsTracker() {
        return mActivityTabStartupMetricsTracker;
    }

    @Override
    protected View getViewToBeDrawnBeforeInitializingNative() {
        View controlContainer = findViewById(R.id.control_container);
        return controlContainer != null ? controlContainer
                                        : super.getViewToBeDrawnBeforeInitializingNative();
    }

    /**
     * This function triggers the layout inflation. If subclasses override {@link
     * #doLayoutInflation}, no calls to {@link #getCompositorViewHolderSupplier().get()} can be done
     * until inflation is complete and {@link #onInitialLayoutInflationComplete()} is called. If the
     * subclass does not override {@link #doLayoutInflation}, then {@link
     * #getCompositorViewHolderSupplier().get()} is safe to be called after calling super.
     */
    @Override
    protected final void triggerLayoutInflation() {
        mInflateInitialLayoutBeginMs = SystemClock.elapsedRealtime();
        try (TraceEvent te = TraceEvent.scoped("ChromeActivity.triggerLayoutInflation")) {
            SelectionPopupController.setShouldGetReadbackViewFromWindowAndroid();

            enableHardwareAcceleration();
            setLowEndTheme();

            WarmupManager warmupManager = WarmupManager.getInstance();
            if (warmupManager.hasViewHierarchyWithToolbar(getControlContainerLayoutId())) {
                View placeHolderView = new View(this);
                setContentView(placeHolderView);
                ViewGroup contentParent = (ViewGroup) placeHolderView.getParent();
                warmupManager.transferViewHierarchyTo(contentParent);
                contentParent.removeView(placeHolderView);
                onInitialLayoutInflationComplete();
            } else {
                warmupManager.clearViewHierarchy();
                doLayoutInflation();
            }
        }
    }

    /**
     * This function implements the actual layout inflation, Subclassing Activities that override
     * this method without calling super need to call {@link #onInitialLayoutInflationComplete()}.
     */
    // TODO(crbug.com/1336778): Remove the @SuppressLint.
    @SuppressLint("MissingInflatedId")
    protected void doLayoutInflation() {
        try (TraceEvent te = TraceEvent.scoped("ChromeActivity.doLayoutInflation")) {
            // Allow disk access for the content view and toolbar container setup.
            // On certain android devices this setup sequence results in disk writes outside
            // of our control, so we have to disable StrictMode to work. See
            // https://crbug.com/639352.
            try (StrictModeContext ignored = StrictModeContext.allowDiskWrites()) {
                TraceEvent.begin("setContentView(R.layout.main)");
                setContentView(R.layout.main);
                TraceEvent.end("setContentView(R.layout.main)");
                if (getControlContainerLayoutId() != ActivityUtils.NO_RESOURCE_ID) {
                    ViewStub toolbarContainerStub =
                            ((ViewStub) findViewById(R.id.control_container_stub));

                    toolbarContainerStub.setLayoutResource(getControlContainerLayoutId());
                    TraceEvent.begin("toolbarContainerStub.inflate");
                    toolbarContainerStub.inflate();
                    TraceEvent.end("toolbarContainerStub.inflate");
                }

                // It cannot be assumed that the result of toolbarContainerStub.inflate() will
                // be the control container since it may be wrapped in another view.
                ControlContainer controlContainer =
                        (ControlContainer) findViewById(R.id.control_container);

                if (controlContainer == null) {
                    // omnibox_results_container_stub anchors off of control_container, and will
                    // crash during layout if control_container doesn't exist.
                    UiUtils.removeViewFromParent(findViewById(R.id.omnibox_results_container_stub));
                }

                // Inflate the correct toolbar layout for the device.
                int toolbarLayoutId = getToolbarLayoutId();
                if (toolbarLayoutId != ActivityUtils.NO_RESOURCE_ID && controlContainer != null) {
                    controlContainer.initWithToolbar(toolbarLayoutId);
                }
            }
            onInitialLayoutInflationComplete();
        }
    }

    @Override
    protected void onInitialLayoutInflationComplete() {
        mInflateInitialLayoutEndMs = SystemClock.elapsedRealtime();

        mRootUiCoordinator.getStatusBarColorController().updateStatusBarColor();

        ViewGroup rootView = (ViewGroup) getWindow().getDecorView().getRootView();
        mCompositorViewHolderSupplier.set(
                (CompositorViewHolder) findViewById(R.id.compositor_view_holder));

        // If the UI was inflated on a background thread, then the CompositorView may not have been
        // fully initialized yet as that may require the creation of a handler which is not allowed
        // outside the UI thread. This call should fully initialize the CompositorView if it hasn't
        // been yet.
        mCompositorViewHolderSupplier.get().setRootView(rootView);

        // Setting fitsSystemWindows to false ensures that the root view doesn't consume the
        // insets.
        rootView.setFitsSystemWindows(false);

        // Add a custom view right after the root view that stores the insets to access later.
        // WebContents needs the insets to determine the portion of the screen obscured by
        // non-content displaying things such as the OSK.
        mInsetObserverViewSupplier.set(InsetObserverView.create(this));
        rootView.addView(mInsetObserverViewSupplier.get(), 0);

        super.onInitialLayoutInflationComplete();
    }

    @Override
    public boolean shouldStartGpuProcess() {
        return true;
    }

    @Override
    public final void initializeTabModels() {
        if (mTabModelOrchestrator.areTabModelsInitialized()) return;

        createTabModels();
        TabModelSelector tabModelSelector = mTabModelOrchestrator.getTabModelSelector();

        if (tabModelSelector == null) {
            assert isFinishing();
            return;
        }

        mTabModelSelectorSupplier.set(tabModelSelector);
        mActivityTabProvider.setTabModelSelector(tabModelSelector);
        mRootUiCoordinator.getStatusBarColorController().setTabModelSelector(tabModelSelector);

        Pair<? extends TabCreator, ? extends TabCreator> tabCreators = createTabCreators();
        mTabCreatorManagerSupplier.set(
                incognito -> incognito ? tabCreators.second : tabCreators.first);

        OfflinePageUtils.observeTabModelSelector(this, tabModelSelector);
        if (mTabModelSelectorTabObserver != null) mTabModelSelectorTabObserver.destroy();

        mTabModelSelectorTabObserver = new TabModelSelectorTabObserver(tabModelSelector) {
            @Override
            public void onLoadStopped(Tab tab, boolean toDifferentDocument) {
                postDeferredStartupIfNeeded();
            }

            @Override
            public void onPageLoadFinished(Tab tab, GURL url) {
                postDeferredStartupIfNeeded();
                OfflinePageUtils.showOfflineSnackbarIfNecessary(tab);
            }

            @Override
            public void onCrash(Tab tab) {
                postDeferredStartupIfNeeded();
            }
        };
    }

    /**
     * @return The {@link TabModelOrchestrator} owned by this {@link ChromeActivity}.
     */
    protected abstract TabModelOrchestrator createTabModelOrchestrator();

    /**
     * Call the {@link TabModelOrchestrator} to initialize its members.
     */
    protected abstract void createTabModels();

    /**
     * Call the {@link TabModelOrchestrator} to destroy its members.
     */
    protected abstract void destroyTabModels();

    /**
     * @return The {@link TabCreator}s owned
     *         by this {@link ChromeActivity}.  The first item in the Pair is the normal model tab
     *         creator, and the second is the tab creator for incognito tabs.
     */
    protected abstract Pair<? extends TabCreator, ? extends TabCreator> createTabCreators();

    /**
     * @return {@link ToolbarManager} that belongs to this activity or null if the current activity
     *         does not support a toolbar.
     * TODO(pnoland, https://crbug.com/865801): remove this in favor of having RootUICoordinator
     *         inject ToolbarManager directly to sub-components.
     */
    @Nullable
    public ToolbarManager getToolbarManager() {
        return mRootUiCoordinator.getToolbarManager();
    }

    /**
     * @return The {@link ManualFillingComponent} that belongs to this activity.
     */
    public ManualFillingComponent getManualFillingComponent() {
        return mManualFillingComponentSupplier.get();
    }

    /**
     * @return The {@link LaunchCauseMetrics} to be owned by this {@link ChromeActivity}.
     */
    protected abstract LaunchCauseMetrics createLaunchCauseMetrics();

    private LaunchCauseMetrics getLaunchCauseMetrics() {
        if (mLaunchCauseMetrics == null) {
            mLaunchCauseMetrics = createLaunchCauseMetrics();
        }
        return mLaunchCauseMetrics;
    }

    @Override
    public AppMenuPropertiesDelegate createAppMenuPropertiesDelegate() {
        return new AppMenuPropertiesDelegateImpl(this, getActivityTabProvider(),
                getMultiWindowModeStateDispatcher(), getTabModelSelector(), getToolbarManager(),
                getWindow().getDecorView(), null, null, mBookmarkBridgeSupplier,
                /*incognitoReauthControllerOneshotSupplier=*/null);
    }

    /**
     * @return The resource id for the layout to use for {@link ControlContainer}. 0 by default.
     */
    protected int getControlContainerLayoutId() {
        return ActivityUtils.NO_RESOURCE_ID;
    }

    /**
     * @return The resource id that contains how large the browser controls are.
     */
    public int getControlContainerHeightResource() {
        return ActivityUtils.NO_RESOURCE_ID;
    }

    /**
     * @return The layout ID for the toolbar to use.
     */
    protected int getToolbarLayoutId() {
        return ActivityUtils.NO_RESOURCE_ID;
    }

    @Override
    public void initializeState() {
        super.initializeState();

        IntentHandler.setTestIntentsEnabled(
                CommandLine.getInstance().hasSwitch(ContentSwitches.ENABLE_TEST_INTENTS));
    }

    @Override
    public void initializeCompositor() {
        TraceEvent.begin("ChromeActivity:CompositorInitialization");
        super.initializeCompositor();

        getTabContentManager().initWithNative();
        mCompositorViewHolderSupplier.get().onNativeLibraryReady(
                getWindowAndroid(), getTabContentManager());

        // TODO(1107916): Move contextual search initialization to the RootUiCoordinator.
        if (ContextualSearchFieldTrial.isEnabled()) {
            mContextualSearchManagerSupplier.set(new ContextualSearchManager(this, this,
                    mRootUiCoordinator.getScrimCoordinator(), getActivityTabProvider(),
                    getFullscreenManager(), getBrowserControlsManager(), getWindowAndroid(),
                    getTabModelSelectorSupplier().get(), () -> getLastUserInteractionTime()));
        }

        TraceEvent.end("ChromeActivity:CompositorInitialization");
    }

    @Override
    public void onStartWithNative() {
        assert mNativeInitialized : "onStartWithNative was called before native was initialized.";
        super.onStartWithNative();
        UpdateMenuItemHelper.getInstance().onStart();
        ChromeActivitySessionTracker.getInstance().onStartWithNative();
        ChromeCachedFlags.getInstance().cacheNativeFlags();
        OfflineIndicatorController.initialize();

        // postDeferredStartupIfNeeded() is called in TabModelSelectorTabObsever#onLoadStopped(),
        // #onPageLoadFinished() and #onCrash(). If we are not actively loading a tab (e.g.
        // in Android N multi-instance, which is created by re-parenting an existing tab),
        // ensure onDeferredStartup() gets called by calling postDeferredStartupIfNeeded() here.
        if (mDeferredStartupQueued || shouldPostDeferredStartupForReparentedTab()) {
            postDeferredStartupIfNeeded();
        }
    }

    /**
     * Returns whether deferred startup should be run if we are not actively loading a tab (e.g.
     * in Android N multi-instance, which is created by re-parenting an existing tab).
     */
    public boolean shouldPostDeferredStartupForReparentedTab() {
        return getActivityTab() == null || !getActivityTab().isLoading();
    }

    private void onActivityShown() {
        maybeRemoveWindowBackground();

        Tab tab = getActivityTab();
        if (tab != null) {
            if (tab.isHidden()) {
                tab.show(TabSelectionType.FROM_USER);
            } else {
                // The visible Tab's renderer process may have died after the activity was
                // paused. Ensure that it's restored appropriately.
                tab.loadIfNeeded();
            }
        }
        VrModuleProvider.getDelegate().onActivityShown(this);

        MultiWindowUtils.getInstance().recordMultiWindowStateUkm(this, tab);
    }

    private void onActivityHidden() {
        VrModuleProvider.getDelegate().onActivityHidden(this);

        Tab tab = getActivityTab();
        TabModelSelector tabModelSelector = mTabModelOrchestrator.getTabModelSelector();
        if (tabModelSelector != null && !tabModelSelector.isReparentingInProgress()
                && tab != null) {
            tab.hide(TabHidingType.ACTIVITY_HIDDEN);
        }
    }

    private boolean useWindowFocusForVisibility() {
        return Build.VERSION.SDK_INT < Build.VERSION_CODES.Q;
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);

        if (useWindowFocusForVisibility()) {
            if (hasFocus) {
                onActivityShown();
            } else {
                if (ApplicationStatus.getStateForActivity(this) == ActivityState.STOPPED) {
                    onActivityHidden();
                }
            }
        }

        Clipboard.getInstance().onWindowFocusChanged(hasFocus);
    }

    /**
     * Returns theme color which should be used when:
     * - Web page does not provide a custom theme color.
     * AND
     * - Browser is in a state where it can be themed (no  intersitial showing etc.)
     * {@link TabState#UNSPECIFIED_THEME_COLOR} should be returned if the activity should use the
     * default color in this scenario.
     */
    public int getActivityThemeColor() {
        return TabState.UNSPECIFIED_THEME_COLOR;
    }

    @Override
    public int getBaseStatusBarColor(Tab tab) {
        return StatusBarColorController.UNDEFINED_STATUS_BAR_COLOR;
    }

    private void createContextReporterIfNeeded() {
        if (!mStarted) return; // Sync state reporting should work only in started state.
        if (mContextReporter != null || getActivityTab() == null) return;

        final SyncService syncService = SyncService.get();

        if (syncService != null && syncService.isSyncingUrlsWithKeystorePassphrase()) {
            ContextReporter.SelectionReporter controller =
                    getContextualSearchManagerSupplier().hasValue() ? new ContextReporter.SelectionReporter() {
                        @Override
                        public void enable(Callback<GSAContextDisplaySelection> callback) {
                            getContextualSearchManagerSupplier().get().enableContextReporting(
                                    callback);
                        }

                        @Override
                        public void disable() {
                            getContextualSearchManagerSupplier().get().disableContextReporting();
                        }
                    } : null;
            mContextReporter = AppHooks.get().createGsaHelper().getContextReporter(
                    getActivityTabProvider(), mTabModelSelectorSupplier, controller);

            if (mSyncStateChangedListener != null) {
                syncService.removeSyncStateChangedListener(mSyncStateChangedListener);
                mSyncStateChangedListener = null;
            }

            return;
        } else {
            reportSyncStatus(syncService);
        }

        if (mSyncStateChangedListener == null && syncService != null) {
            mSyncStateChangedListener = () -> createContextReporterIfNeeded();
            syncService.addSyncStateChangedListener(mSyncStateChangedListener);
        }
    }

    /**
     * Records an appropriate status via UMA given the current sync status.
     */
    private static void reportSyncStatus(@Nullable SyncService syncService) {
        if (syncService == null || !syncService.isEngineInitialized()) {
            ContextReporter.reportStatus(ContextReporter.STATUS_SYNC_NOT_INITIALIZED);
        } else if (!syncService.getActiveDataTypes().contains(ModelType.TYPED_URLS)) {
            ContextReporter.reportStatus(ContextReporter.STATUS_SYNC_NOT_SYNCING_URLS);
        } else if (syncService.getPassphraseType() != PassphraseType.KEYSTORE_PASSPHRASE
                && syncService.getPassphraseType() != PassphraseType.TRUSTED_VAULT_PASSPHRASE) {
            ContextReporter.reportStatus(ContextReporter.STATUS_SYNC_NOT_KEYSTORE_PASSPHRASE);
        } else {
            ContextReporter.reportStatus(ContextReporter.STATUS_SYNC_OTHER);
        }
    }

    @Override
    public void onResumeWithNative() {
        // TODO(b/182286787): Clean-up once fixed session resume order is verified.
        final boolean useFixedUmaSessionResumeOrder =
                ChromeFeatureList.isEnabled(ChromeFeatureList.FIXED_UMA_SESSION_RESUME_ORDER);
        if (useFixedUmaSessionResumeOrder) {
            // First, update the activity type in order to have it properly captured in
            // markSessionResume; stage the activity type value such that it can be picked up when
            // the new UMA record is opened as a part of the subsequent session resume.
            //
            // TODO(b/182286787): Move this inside markSessionResume, plumb through to UMA session
            // resumption.
            ChromeSessionState.setActivityType(getActivityType());

            // Close the current UMA record and start a new UMA one.
            markSessionResume();

            // Inform the actity lifecycle observers. Among other things, the observers record
            // metrics pertaining to the "resumed" activity. This needs to happens after
            // markSessionResume has closed the old UMA record, pertaining to the previous
            // (backgrounded) activity, and opened a new one pertaining to the "resumed" activity.
            super.onResumeWithNative();
        } else {
            super.onResumeWithNative();
            markSessionResume();
        }

        // Resume the ChromeActivity...

        RecordUserAction.record("MobileComeToForeground");
        getLaunchCauseMetrics().recordLaunchCause();

        Tab tab = getActivityTab();
        if (tab != null) {
            WebContents webContents = tab.getWebContents();
            LaunchMetrics.commitLaunchMetrics(webContents);

            // For picture-in-picture mode / auto-darken web contents.
            if (webContents != null) webContents.notifyRendererPreferenceUpdate();
        }

        // TODO(b/182286787): Remove once fixed session resume order is verified.
        if (!useFixedUmaSessionResumeOrder) {
            ChromeSessionState.setActivityType(getActivityType());
        }

        ChromeSessionState.setIsInMultiWindowMode(
                MultiWindowUtils.getInstance().isInMultiWindowMode(this));

        boolean appIsInNightMode = getNightModeStateProvider().isInNightMode();
        boolean systemIsInNightMode = SystemNightModeMonitor.getInstance().isSystemNightModeOn();
        ChromeSessionState.setDarkModeState(appIsInNightMode, systemIsInNightMode);

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            ensureFullscreenVideoPictureInPictureController();
        }
        if (mFullscreenVideoPictureInPictureController != null) {
            mFullscreenVideoPictureInPictureController.onFrameworkExitedPictureInPicture();
        }
        VrModuleProvider.getDelegate().maybeRegisterVrEntryHook(this);

        getManualFillingComponent().onResume();
    }

    private void ensureFullscreenVideoPictureInPictureController() {
        if (mFullscreenVideoPictureInPictureController == null) {
            mFullscreenVideoPictureInPictureController =
                    new FullscreenVideoPictureInPictureController(
                            this, getActivityTabProvider(), getFullscreenManager());
        }
    }

    @Override
    protected void onUserLeaveHint() {
        super.onUserLeaveHint();

        getLaunchCauseMetrics().onUserLeaveHint();

        // Can be in finishing state. No need to attempt PIP.
        if (isActivityFinishingOrDestroyed()) return;

        ensureFullscreenVideoPictureInPictureController();
        mFullscreenVideoPictureInPictureController.attemptPictureInPicture();
        // The attempt might not be successful.  If it is, then `onPictureInPictureModeChanged` will
        // let us know later.  Note that the activity might report that it is in PictureInPicture
        // mode at any point after this, which might be before we finish setup after receiving
        // notification from mOnPictureInPictureModeChanged.
    }

    /**
     * When we're notified that Picture-in-Picture mode has changed, make sure that the controller
     * is kept up-to-date.
     */
    @Override
    @RequiresApi(api = Build.VERSION_CODES.O)
    public void onPictureInPictureModeChanged(boolean inPicture, Configuration newConfig) {
        super.onPictureInPictureModeChanged(inPicture, newConfig);
        if (inPicture) {
            ensureFullscreenVideoPictureInPictureController();
            mFullscreenVideoPictureInPictureController.onEnteredPictureInPictureMode();
            mLastPictureInPictureModeForTesting = true;
        } else if (mFullscreenVideoPictureInPictureController != null) {
            mLastPictureInPictureModeForTesting = false;
            mFullscreenVideoPictureInPictureController.onFrameworkExitedPictureInPicture();
        }
    }

    /**
     * Return the status of a Picture-in-Picture transition.  This is separate from
     * {@link isInPictureInPictureMode}, because this will trigger only after we have received and
     * processed an Activity.onPictureInPictureModeChanged call.
     */
    public boolean getLastPictureInPictureModeForTesting() {
        return mLastPictureInPictureModeForTesting;
    }

    @Override
    public void onPauseWithNative() {
        RecordUserAction.record("MobileGoToBackground");
        Tab tab = getActivityTab();
        if (tab != null) getTabContentManager().cacheTabThumbnail(tab);
        getManualFillingComponent().onPause();

        VrModuleProvider.getDelegate().maybeUnregisterVrEntryHook();
        markSessionEnd();

        super.onPauseWithNative();
    }

    @Override
    public void onStopWithNative() {
        if (GSAState.getInstance(this).isGsaAvailable() && !SysUtils.isLowEndDevice()) {
            if (mGSAAccountChangeListener != null) mGSAAccountChangeListener.disconnect();
        }
        if (mSyncStateChangedListener != null) {
            SyncService syncService = SyncService.get();
            if (syncService != null) {
                syncService.removeSyncStateChangedListener(mSyncStateChangedListener);
            }
            mSyncStateChangedListener = null;
        }
        if (mContextReporter != null) mContextReporter.disable();

        super.onStopWithNative();
    }

    @Override
    public void onNewIntent(Intent intent) {
        // This should be called before the call to super so that the needed VR flags are set as
        // soon as the VR intent is received.
        VrModuleProvider.getDelegate().maybeHandleVrIntentPreNative(this, intent);
        super.onNewIntent(intent);
    }

    @Override
    public void onNewIntentWithNative(Intent intent) {
        if (mFullscreenVideoPictureInPictureController != null) {
            mFullscreenVideoPictureInPictureController.onFrameworkExitedPictureInPicture();
        }

        super.onNewIntentWithNative(intent);
        getLaunchCauseMetrics().onReceivedIntent();
        if (mIntentHandler.shouldIgnoreIntent(intent, /*startedActivity=*/false)) return;

        // We send this intent so that we can enter WebVr presentation mode if needed. This
        // call doesn't consume the intent because it also has the url that we need to load.
        VrModuleProvider.getDelegate().onNewIntentWithNative(this, intent);
        mIntentHandler.onNewIntent(intent);
    }

    /**
     * @return The type for this activity.
     */
    @ActivityType
    public abstract int getActivityType();

    /**
     * @return Whether the given activity contains a CustomTab.
     */
    public boolean isCustomTab() {
        return getActivityType() == ActivityType.CUSTOM_TAB
                || getActivityType() == ActivityType.TRUSTED_WEB_ACTIVITY;
    }

    /**
     * Actions that may be run at some point after startup. Place tasks that are not critical to the
     * startup path here.  This method will be called automatically.
     */
    private void onDeferredStartup() {
        initDeferredStartupForActivity();
        ProcessInitializationHandler.getInstance().initializeDeferredStartupTasks();
        DeferredStartupHandler.getInstance().queueDeferredTasksOnIdleHandler();
    }

    /**
     * All deferred startup tasks that require the activity rather than the app should go here.
     *
     * Overriding methods should queue tasks on the DeferredStartupHandler before or after calling
     * super depending on whether the tasks should run before or after these ones.
     */
    @CallSuper
    protected void initDeferredStartupForActivity() {
        final String simpleName = getClass().getSimpleName();
        DeferredStartupHandler.getInstance().addDeferredTask(() -> {
            if (isActivityFinishingOrDestroyed()) return;
            if (getToolbarManager() != null) {
                RecordHistogram.recordTimesHistogram(
                        "MobileStartup.ToolbarInflationTime." + simpleName,
                        mInflateInitialLayoutEndMs - mInflateInitialLayoutBeginMs);
                getToolbarManager().onDeferredStartup(getOnCreateTimestampMs(), simpleName);
            }

            if (MultiWindowUtils.getInstance().isInMultiWindowMode(ChromeActivity.this)) {
                onDeferredStartupForMultiWindowMode();
            }

            long intentTimestamp = IntentHandler.getTimestampFromIntent(getIntent());
            if (intentTimestamp != -1) {
                recordIntentToCreationTime(getOnCreateTimestampMs() - intentTimestamp);
            }

            recordDisplayDimensions();
            int playServicesVersion = PlayServicesVersionInfo.getApkVersionNumber(this);
            RecordHistogram.recordBooleanHistogram(
                    "Android.PlayServices.Installed", playServicesVersion > 0);
            RecordHistogram.recordSparseHistogram(
                    "Android.PlayServices.Version", playServicesVersion);

            FontSizePrefs.getInstance(Profile.getLastUsedRegularProfile())
                    .recordUserFontPrefOnStartup();
        });

        DeferredStartupHandler.getInstance().addDeferredTask(() -> {
            if (isActivityFinishingOrDestroyed()) return;
            ForcedSigninProcessor.checkCanSignIn(ChromeActivity.this);
        });

        // GSA connection is not needed on low-end devices because Icing is disabled.
        if (!SysUtils.isLowEndDevice()) {
            if (isActivityFinishingOrDestroyed()) return;
            DeferredStartupHandler.getInstance().addDeferredTask(() -> {
                if (!GSAState.getInstance(this).isGsaAvailable()) {
                    ContextReporter.reportStatus(ContextReporter.STATUS_GSA_NOT_AVAILABLE);
                    return;
                }

                if (mGSAAccountChangeListener == null) {
                    mGSAAccountChangeListener =
                            GSAAccountChangeListener.create(AppHooks.get().createGsaHelper());
                }
                mGSAAccountChangeListener.connect();
                createContextReporterIfNeeded();
            });
        }
    }

    /**
     * Actions that may be run at some point after startup for Android N multi-window mode. Should
     * be called from #onDeferredStartup() if the activity is in multi-window mode.
     */
    private void onDeferredStartupForMultiWindowMode() {
        // If the Activity was launched in multi-window mode, record a user action.
        recordMultiWindowModeChanged(
                /* isInMultiWindowMode= */ true, /* isDeferredStartup= */ true);
    }

    /**
     * Records the time it takes from creating an intent for {@link ChromeActivity} to activity
     * creation, including time spent in the framework.
     * @param timeMs The time from creating an intent to activity creation.
     */
    @CallSuper
    protected void recordIntentToCreationTime(long timeMs) {
        RecordHistogram.recordTimesHistogram("MobileStartup.IntentToCreationTime", timeMs);
    }

    @Override
    public void onStart() {
        // Sometimes mCompositorViewHolder is null, see crbug.com/1057613.
        if (AsyncTabParamsManagerSingleton.getInstance().hasParamsWithTabToReparent()) {
            // TODO(https://crbug.com/1252526): Remove logging once root cause of bug is identified
            //  & fixed.
            Log.i(TAG,
                    "#onStart, num async tabs: "
                            + AsyncTabParamsManagerSingleton.getInstance()
                                      .getAsyncTabParams()
                                      .size());

            if (mCompositorViewHolderSupplier.hasValue()) {
                mCompositorViewHolderSupplier.get().prepareForTabReparenting();
            }
        }
        super.onStart();

        if (!useWindowFocusForVisibility()) {
            onActivityShown();
        }

        if (mPartnerBrowserRefreshNeeded) {
            mPartnerBrowserRefreshNeeded = false;
            PartnerBrowserCustomizations.getInstance().initializeAsync(getApplicationContext());
            PartnerBrowserCustomizations.getInstance().setOnInitializeAsyncFinished(() -> {
                if (PartnerBrowserCustomizations.isIncognitoDisabled()) {
                    terminateIncognitoSession();
                }
            });
        }
        if (mCompositorViewHolderSupplier.hasValue()) mCompositorViewHolderSupplier.get().onStart();

        mStarted = true;
    }

    /**
     * WARNING: DO NOT USE THIS METHOD. PASS TabObscuringHandler TO THE OBJECT CONSTRUCTOR INSTEAD.
     * @return {@link TabObscuringHandler} object.
     */
    public TabObscuringHandler getTabObscuringHandler() {
        if (mRootUiCoordinator == null) return null;
        return mRootUiCoordinator.getTabObscuringHandler();
    }

    @Override
    public void onStop() {
        super.onStop();

        if (useWindowFocusForVisibility()) {
            if (!hasWindowFocus()) onActivityHidden();
        } else {
            onActivityHidden();
        }

        // We want to refresh partner browser provider every onStart().
        mPartnerBrowserRefreshNeeded = true;
        if (mCompositorViewHolderSupplier.hasValue()) mCompositorViewHolderSupplier.get().onStop();

        // If postInflationStartup hasn't been called yet (because inflation was done asynchronously
        // and has not yet completed), it no longer needs to do the belated onStart code since we
        // were stopped in the mean time.
        mStarted = false;
    }

    @Override
    @RequiresApi(Build.VERSION_CODES.M)
    public void onProvideAssistContent(AssistContent outContent) {
        Tab tab = getActivityTab();
        boolean inOverviewMode = isInOverviewMode();

        // Attempt to fetch translate data here so we can record UMA even if it won't be attached.
        @Nullable
        String structuredData = TranslateAssistContent.getTranslateDataForTab(tab, inOverviewMode);

        // No information is provided in incognito mode and overview mode.
        if (tab != null && !tab.isIncognito() && !inOverviewMode) {
            outContent.setWebUri(Uri.parse(tab.getUrl().getSpec()));
            if (structuredData != null) {
                outContent.setStructuredData(structuredData);
            }
        }
    }

    // TODO(crbug.com/973781): Once Chromium is built against Android Q SDK, replace
    // @SuppressWarnings with @Override
    @SuppressWarnings("MissingOverride")
    @RequiresApi(29)
    @UsedByReflection("Called from Android Q")
    public void onPerformDirectAction(String actionId, Bundle arguments,
            CancellationSignal cancellationSignal, Consumer<Bundle> callback) {
        mRootUiCoordinator.onPerformDirectAction(actionId, arguments, cancellationSignal, callback);
    }

    // TODO(crbug.com/973781): Once Chromium is built against Android Q SDK:
    //  - replace @SuppressWarnings with @Override
    //  - replace Consumer with Consumer<List<DirectAction>>
    @SuppressWarnings("MissingOverride")
    @RequiresApi(29)
    @UsedByReflection("Called from Android Q")
    public void onGetDirectActions(CancellationSignal cancellationSignal, Consumer callback) {
        mRootUiCoordinator.onGetDirectActions(cancellationSignal, callback);
    }

    @Override
    public long getOnCreateTimestampMs() {
        return super.getOnCreateTimestampMs();
    }

    @Override
    protected void onSaveInstanceState(Bundle outState) {
        super.onSaveInstanceState(outState);
        VrModuleProvider.getDelegate().onSaveInstanceState(outState);
    }

    /**
     * This cannot be overridden in order to preserve destruction order.  Override
     * {@link #onDestroyInternal()} instead to perform clean up tasks.
     */
    @SuppressLint("NewApi")
    @Override
    protected final void onDestroy() {
        if (mContextualSearchManagerSupplier.hasValue()) {
            mContextualSearchManagerSupplier.get().destroy();
            mContextualSearchManagerSupplier.set(null);
        }

        if (mSnackbarManager != null) {
            SnackbarManagerProvider.detach(mSnackbarManager);
        }

        if (mTabModelSelectorTabObserver != null) {
            mTabModelSelectorTabObserver.destroy();
            mTabModelSelectorTabObserver = null;
        }

        // TODO(1168131): Destruction and detaching of the LayoutManager should be moved to the
        //                RootUiCoordinator.
        if (mLayoutManagerSupplier.get() != null) {
            LayoutManagerAppUtils.detach(mLayoutManagerSupplier.get());
        }

        if (mCompositorViewHolderSupplier.hasValue()) {
            CompositorViewHolder compositorViewHolder = mCompositorViewHolderSupplier.get();
            if (compositorViewHolder.getLayoutManager() != null) {
                compositorViewHolder.getLayoutManager().removeSceneChangeObserver(this);
            }
            compositorViewHolder.shutDown();
            mCompositorViewHolderSupplier.set(null);
        }

        onDestroyInternal();

        if (mDidAddPolicyChangeListener) {
            CombinedPolicyProvider.get().removePolicyChangeListener(this);
            mDidAddPolicyChangeListener = false;
        }

        if (mTabContentManager != null) {
            mTabContentManager.destroy();
            mTabContentManager = null;
        }

        if (mTabContentManagerSupplier != null) {
            mTabContentManagerSupplier = null;
        }

        if (mManualFillingComponentSupplier.hasValue()) {
            mManualFillingComponentSupplier.get().destroy();
        }
        mManualFillingComponentSupplier.destroy();

        if (mBrowserControlsManagerSupplier.hasValue()) {
            mBrowserControlsManagerSupplier.get().destroy();
        }
        mBrowserControlsManagerSupplier.destroy();

        if (mActivityTabStartupMetricsTracker != null) {
            mActivityTabStartupMetricsTracker.destroy();
            mActivityTabStartupMetricsTracker = null;
        }

        destroyTabModels();

        if (mBookmarkBridgeSupplier != null) {
            BookmarkBridge bookmarkBridge = mBookmarkBridgeSupplier.get();
            if (bookmarkBridge != null) bookmarkBridge.destroy();
            mBookmarkBridgeSupplier = null;
        }

        if (mShareDelegateSupplier != null) {
            mShareDelegateSupplier.destroy();
        }

        if (mTabModelSelectorSupplier != null) {
            mTabModelSelectorSupplier.destroy();
        }

        if (mBottomContainer != null) {
            mBottomContainer.destroy();
            mBottomContainer = null;
        }

        if (mDisplayAndroidObserver != null) {
            getWindowAndroid().getDisplay().removeObserver(mDisplayAndroidObserver);
            mDisplayAndroidObserver = null;
        }

        if (mTextBubbleBackPressHandler != null) {
            mTextBubbleBackPressHandler.destroy();
            mTextBubbleBackPressHandler = null;
        }

        if (mSelectionPopupBackPressHandler != null) {
            mSelectionPopupBackPressHandler.destroy();
            mSelectionPopupBackPressHandler = null;
        }

        if (mStylusWritingCoordinator != null) {
            mStylusWritingCoordinator.destroy();
            mStylusWritingCoordinator = null;
        }

        mActivityTabProvider.destroy();
        ChromeActivitySessionTracker.getInstance().unregisterTabModelSelectorSupplier(this);

        mComponent = null;

        super.onDestroy();
    }

    /**
     * Override this to perform destruction tasks.  Note that by the time this is called, the
     * {@link CompositorViewHolder} will be destroyed, but the {@link WindowAndroid} and
     * {@link TabModelSelector} will not.
     * <p>
     * After returning from this, the {@link TabModelSelector} will be destroyed followed
     * by the {@link WindowAndroid}.
     */
    protected void onDestroyInternal() {}

    /**
     * @return The unified manager for all snackbar related operations.
     */
    @Override
    public SnackbarManager getSnackbarManager() {
        BottomSheetController controller =
                mRootUiCoordinator == null ? null : mRootUiCoordinator.getBottomSheetController();
        if (mRootUiCoordinator != null && controller != null && controller.isSheetOpen()
                && !controller.isSheetHiding()) {
            return mRootUiCoordinator.getBottomSheetSnackbarManager();
        }
        return mSnackbarManager;
    }

    @Override
    protected ModalDialogManager createModalDialogManager() {
        return new ModalDialogManager(
                new AppModalPresenter(this), ModalDialogManager.ModalDialogType.APP);
    }

    protected Drawable getBackgroundDrawable() {
        return new ColorDrawable(getColor(R.color.window_background_color));
    }

    /**
     * Change the Window background color that will be used as the resizing background color on
     * Android N+ multi-window mode. Note that subclasses can override this behavior accordingly in
     * case there is already a Window background Drawable and don't want it to be replaced with the
     * ColorDrawable.
     */
    protected void changeBackgroundColorForResizing() {
        getWindow().setBackgroundDrawable(
                new ColorDrawable(getColor(R.color.window_background_color)));
    }

    private void maybeRemoveWindowBackground() {
        // Only need to do this logic once.
        if (mRemoveWindowBackgroundDone) return;

        // Remove the window background only after native init and window getting focus. It's done
        // after native init because before native init, a fake background gets shown. The window
        // focus dependency is because doing it earlier can cause drawing bugs, e.g. crbug/673831.
        if (!mNativeInitialized || !hasWindowFocus()) return;

        // The window background color is used as the resizing background color in Android N+
        // multi-window mode. See crbug.com/602366.
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.N) {
            changeBackgroundColorForResizing();
        } else {
            // Post the removeWindowBackground() call as a separate task, as doing it synchronously
            // here can cause redrawing glitches. See crbug.com/686662 for an example problem.
            Handler handler = new Handler();
            handler.post(() -> removeWindowBackground());
        }

        mRemoveWindowBackgroundDone = true;
    }

    @Override
    public void finishNativeInitialization() {
        mNativeInitialized = true;
        OfflineContentAggregatorNotificationBridgeUiFactory.instance();
        maybeRemoveWindowBackground();
        DownloadManagerService.getDownloadManagerService().onActivityLaunched(
                new DownloadMessageUiDelegate());
        VrModuleProvider.maybeInit();
        VrModuleProvider.getDelegate().onNativeLibraryAvailable();

        PowerMonitor.create();

        if (getSavedInstanceState() == null && getIntent() != null) {
            VrModuleProvider.getDelegate().onNewIntentWithNative(this, getIntent());
        }

        // The first launch of the screenshot feature benefits from this DFM being installed
        // proactively. However without the isolated split feature there are performance regressions
        // as a result of adding this extra code.
        if (BundleUtils.isolatedSplitsEnabled() && Build.VERSION.SDK_INT >= Build.VERSION_CODES.O
                && AppHooks.get().getImageEditorModuleProvider() != null) {
            AppHooks.get().getImageEditorModuleProvider().maybeInstallModuleDeferred();
        }

        super.finishNativeInitialization();

        mManualFillingComponentSupplier.get().initialize(getWindowAndroid(),
                mRootUiCoordinator.getBottomSheetController(),
                (ChromeKeyboardVisibilityDelegate) getWindowAndroid().getKeyboardDelegate(),
                mBackPressManager, findViewById(R.id.keyboard_accessory_sheet_stub),
                findViewById(R.id.keyboard_accessory_stub));

        mTabReparentingControllerSupplier.set(new TabReparentingController(
                ReparentingDelegateFactory.createReparentingControllerDelegate(
                        getTabModelSelector()),
                AsyncTabParamsManagerSingleton.getInstance()));

        // This must be initialized after initialization of tab reparenting controller.
        DisplayAndroid display = getWindowAndroid().getDisplay();
        mDisplayAndroidObserver = new DisplayAndroidObserver() {
            @Override
            public void onDisplayModesChanged(List<Mode> supportedModes) {
                maybeOnScreenSizeChange();
            }

            @Override
            public void onCurrentModeChanged(Mode currentMode) {
                maybeOnScreenSizeChange();
            }

            private void maybeOnScreenSizeChange() {
                if (didChangeTabletMode()) {
                    onScreenLayoutSizeChange();
                }
            }
        };
        display.addObserver(mDisplayAndroidObserver);
    }

    /**
     * @return Whether native initialization has been completed for this activity.
     */
    public boolean didFinishNativeInitialization() {
        return mNativeInitialized;
    }

    @Override
    public boolean onOptionsItemSelected(int itemId, @Nullable Bundle menuItemData) {
        mMenuItemData = menuItemData;
        if (mManualFillingComponentSupplier.hasValue()) {
            mManualFillingComponentSupplier.get().dismiss();
        }
        return onMenuOrKeyboardAction(itemId, true);
    }

    @Override
    public boolean onOptionsItemSelected(MenuItem item) {
        if (item != null) {
            if (onOptionsItemSelected(item.getItemId(), null)) return true;
        }
        return super.onOptionsItemSelected(item);
    }

    /**
     * @return Whether the activity is in overview mode.
     */
    public boolean isInOverviewMode() {
        return false;
    }

    /**
     * Returns whether grid Tab switcher or the Start surface should be shown at startup.
     */
    public boolean shouldShowOverviewPageOnStart() {
        return false;
    }

    @CallSuper
    @Override
    public boolean canShowAppMenu() {
        if (isActivityFinishingOrDestroyed()) return false;

        @ActivityState
        int state = ApplicationStatus.getStateForActivity(this);
        boolean inMultiWindow = MultiWindowUtils.getInstance().isInMultiWindowMode(this);
        if (state != ActivityState.RESUMED && (!inMultiWindow || state != ActivityState.PAUSED)) {
            return false;
        }

        return true;
    }

    protected IntentHandlerDelegate createIntentHandlerDelegate() {
        return new IntentHandlerDelegate() {
            @Override
            public void processWebSearchIntent(String query) {
                final Intent searchIntent = new Intent(Intent.ACTION_WEB_SEARCH);
                searchIntent.putExtra(SearchManager.QUERY, query);
                Callback<Boolean> callback = result -> {
                    if (result != null && result) startActivity(searchIntent);
                };
                LocaleManager.getInstance().showSearchEnginePromoIfNeeded(
                        ChromeActivity.this, callback);
            }

            @Override
            public long getIntentHandlingTimeMs() {
                return 0;
            }

            @Override
            public void processTranslateTabIntent(
                    @Nullable String targetLanguageCode, @Nullable String expectedUrl) {}

            @Override
            public void processUrlViewIntent(LoadUrlParams loadUrlParams,
                    @TabOpenType int tabOpenType, String externalAppId, int tabIdToBringToFront,
                    Intent intent) {}
        };
    }

    /**
     * @return Whether the tab models have been fully initialized.
     */
    public boolean areTabModelsInitialized() {
        return mTabModelOrchestrator.areTabModelsInitialized();
    }

    /**
     * {@link TabModelSelector} no longer implements TabModel.  Use getTabModelSelector() or
     * getCurrentTabModel() depending on your needs.
     * @return The {@link TabModelSelector}, possibly null.
     * @deprecated in favor of getTabModelSelectorSupplier.
     */
    @Deprecated
    public TabModelSelector getTabModelSelector() {
        if (!mTabModelOrchestrator.areTabModelsInitialized()) {
            throw new IllegalStateException(
                    "Attempting to access TabModelSelector before initialization");
        }
        return mTabModelOrchestrator.getTabModelSelector();
    }

    /**
     * Returns an {@link ObservableSupplier} for {@link TabModelOrchestrator}.
     */
    public final ObservableSupplier<TabModelOrchestrator> getTabModelOrchestratorSupplier() {
        return mTabModelOrchestratorSupplier;
    }

    /**
     * Returns an {@link ObservableSupplier} for {@link TabModelSelector}. Prefer this method over
     * using {@link #getTabModelSelector()} directly.
     */
    public final ObservableSupplier<TabModelSelector> getTabModelSelectorSupplier() {
        return mTabModelSelectorSupplier;
    }

    /**
     * @return The provider of the visible tab in the current activity.
     */
    public ActivityTabProvider getActivityTabProvider() {
        return mActivityTabProvider;
    }

    /**
     * @return The provider of the instance of {@link TabReparentingController}.
     */
    protected OneshotSupplier<TabReparentingController> getTabReparentingControllerSupplier() {
        return mTabReparentingControllerSupplier;
    }

    /**
     * Gets the supplier of the {@link TabCreatorManager} instance.
     */
    public ObservableSupplier<TabCreatorManager> getTabCreatorManagerSupplier() {
        return mTabCreatorManagerSupplier;
    }

    @Override
    public TabCreator getTabCreator(boolean incognito) {
        if (!mTabModelOrchestrator.areTabModelsInitialized()) {
            throw new IllegalStateException(
                    "Attempting to access TabCreator before initialization");
        }
        return mTabCreatorManagerSupplier.get().getTabCreator(incognito);
    }

    /**
     * Convenience method that returns a tab creator for the currently selected {@link TabModel}.
     * @return A tab creator for the currently selected {@link TabModel}.
     */
    public TabCreator getCurrentTabCreator() {
        return getTabCreator(getTabModelSelector().isIncognitoSelected());
    }

    /**
     * Gets the {@link TabContentManager} instance which holds snapshots of the tabs in this model.
     * @return The thumbnail cache, possibly null.
     * @Deprecated in favor of getTabContentManagerSupplier().
     */
    @Deprecated
    public TabContentManager getTabContentManager() {
        return mTabContentManager;
    }

    /**
     * Sets the {@link TabContentManager} owned by this {@link ChromeActivity}.
     * @param tabContentManager A {@link TabContentManager} instance.
     */
    private void setTabContentManager(TabContentManager tabContentManager) {
        mTabContentManager = tabContentManager;
        TabContentManagerHandler.create(
                tabContentManager, getFullscreenManager(), getTabModelSelector());
        mTabContentManagerSupplier.set(tabContentManager);
    }

    /**
     * Gets the supplier of the {@link TabContentManager} instance.
     */
    public ObservableSupplier<TabContentManager> getTabContentManagerSupplier() {
        return mTabContentManagerSupplier;
    }

    /**
     * Gets the current (inner) TabModel.  This is a convenience function for
     * getModelSelector().getCurrentModel().  It is *not* equivalent to the former getModel()
     * @return Never null, if modelSelector or its field is uninstantiated returns a
     *         {@link EmptyTabModel} singleton
     */
    public TabModel getCurrentTabModel() {
        TabModelSelector modelSelector = getTabModelSelector();
        if (modelSelector == null) return EmptyTabModel.getInstance();
        return modelSelector.getCurrentModel();
    }

    /**
     * DEPRECATED: Instead, use/hold a reference to {@link #mActivityTabProvider}. See
     *             https://crbug.com/871279 for more details. Note that there are important
     *             functional differences between {@link ActivityTabProvider} and this function
     *             when transitioning to/from the tab switcher. For a drop-in replacement, use
     *             {@link TabModelSelector#getCurrentTab} instead.
     *
     * Returns the tab being displayed by this ChromeActivity instance. This allows differentiation
     * between ChromeActivity subclasses that swap between multiple tabs (e.g. ChromeTabbedActivity)
     * and subclasses that only display one Tab (e.g. DocumentActivity).
     *
     * The default implementation grabs the tab currently selected by the TabModel, which may be
     * null if the Tab does not exist or the system is not initialized.
     */
    public Tab getActivityTab() {
        if (!mTabModelOrchestrator.areTabModelsInitialized()) {
            return null;
        }
        return TabModelUtils.getCurrentTab(getCurrentTabModel());
    }

    /**
     * @return The current WebContents, or null if the tab does not exist or is not showing a
     *         WebContents.
     */
    public WebContents getCurrentWebContents() {
        if (!mTabModelOrchestrator.areTabModelsInitialized()) {
            return null;
        }
        return TabModelUtils.getCurrentWebContents(getCurrentTabModel());
    }

    /**
     * Gets the browser controls manager, creates it unless already created.
     * @deprecated Instead, inject this directly to your constructor. If that's not possible, then
     *         use {@link BrowserControlsManagerSupplier}.
     */
    @NonNull
    @Deprecated
    public BrowserControlsManager getBrowserControlsManager() {
        if (!mBrowserControlsManagerSupplier.hasValue() && isActivityFinishingOrDestroyed()) {
            // BrowserControlsManagerSupplier should always have a value unless it's in the process
            // of destruction (and in that case, nothing should be called this method).
            throw new IllegalStateException();
        }
        assert mBrowserControlsManagerSupplier.hasValue();
        return mBrowserControlsManagerSupplier.get();
    }

    /**
     * @return Fullscreen manager object.
     */
    @NonNull
    public FullscreenManager getFullscreenManager() {
        return getBrowserControlsManager().getFullscreenManager();
    }

    /**
     * @return The content offset provider, may be null.
     */
    public ContentOffsetProvider getContentOffsetProvider() {
        return mCompositorViewHolderSupplier.get();
    }

    /**
     * @return The {@code ContextualSearchManager} or {@code null} if none;
     */
    public ObservableSupplier<ContextualSearchManager> getContextualSearchManagerSupplier() {
        return mContextualSearchManagerSupplier;
    }

    /**
     * Exits the fullscreen mode, if any. Does nothing if no fullscreen is present.
     * @return Whether the fullscreen mode is currently showing.
     */
    public boolean exitFullscreenIfShowing() {
        FullscreenManager fullscreenManager = getFullscreenManager();
        if (fullscreenManager.getPersistentFullscreenMode()) {
            fullscreenManager.exitPersistentFullscreenMode();
            return true;
        }
        return false;
    }

    @Override
    public void initializeCompositorContent(LayoutManagerImpl layoutManager, View urlBar,
            ViewGroup contentContainer, ControlContainer controlContainer) {
        // TODO(1168131): The responsibility of managing the availability of the LayoutManager
        //                should be moved to the RootUiCoordinator.
        LayoutManagerAppUtils.attach(getWindowAndroid(), layoutManager);
        mLayoutManagerSupplier.set(layoutManager);

        layoutManager.addSceneChangeObserver(this);
        CompositorViewHolder compositorViewHolder = mCompositorViewHolderSupplier.get();
        compositorViewHolder.setLayoutManager(layoutManager);
        compositorViewHolder.setFocusable(false);
        compositorViewHolder.setControlContainer(controlContainer);
        compositorViewHolder.setBrowserControlsManager(mBrowserControlsManagerSupplier.get());
        compositorViewHolder.setUrlBar(urlBar);
        compositorViewHolder.setInsetObserverView(mInsetObserverViewSupplier.get());
        compositorViewHolder.setAutofillUiBottomInsetSupplier(
                mManualFillingComponentSupplier.get().getBottomInsetSupplier());
        compositorViewHolder.setTopUiThemeColorProvider(
                mRootUiCoordinator.getTopUiThemeColorProvider());
        compositorViewHolder.onFinishNativeInitialization(getTabModelSelector(), this);

        SwipeHandler swipeHandler = layoutManager.getToolbarSwipeHandler();
        if (controlContainer != null && DeviceClassManager.enableToolbarSwipe()
                && swipeHandler != null) {
            controlContainer.setSwipeHandler(swipeHandler);
        }

        mActivityTabProvider.setLayoutStateProvider(layoutManager);

        if (mContextualSearchManagerSupplier.hasValue()) {
            mContextualSearchManagerSupplier.get().initialize(contentContainer, layoutManager,
                    mRootUiCoordinator.getBottomSheetController(), compositorViewHolder,
                    getControlContainerHeightResource() == ActivityUtils.NO_RESOURCE_ID
                            ? 0f
                            : getResources().getDimension(getControlContainerHeightResource()),
                    getToolbarManager(), getActivityType(), getIntentRequestTracker());
        }
    }

    /**
     * @return An {@link ObservableSupplier} that will supply the {@link LayoutManagerImpl} when it
     *         is ready.
     */
    public ObservableSupplier<LayoutManagerImpl> getLayoutManagerSupplier() {
        return mLayoutManagerSupplier;
    }

    /**
     * @return An {@link ObservableSupplier} that will supply the {@link ShareDelegate} when
     *         it is ready.
     */
    public ObservableSupplier<ShareDelegate> getShareDelegateSupplier() {
        return mShareDelegateSupplier;
    }

    /**
     * @return An {@link ObservableSupplier} that will supply the {@link CompositorViewHolder} when
     *         it is ready.
     */
    public ObservableSupplier<CompositorViewHolder> getCompositorViewHolderSupplier() {
        return mCompositorViewHolderSupplier;
    }

    /**
     * Called when the back button is pressed.
     * @return Whether or not the back button was handled.
     */
    protected abstract boolean handleBackPressed();

    /**
     * @return If no higher priority back actions occur, whether pressing the back button
     *         would result in closing the tab. A true return value does not guarantee that
     *         a subsequent call to {@link #handleBackPressed()} will close the tab.
     */
    public boolean backShouldCloseTab(Tab tab) {
        return false;
    }

    @Override
    public void performOnConfigurationChanged(Configuration newConfig) {
        super.performOnConfigurationChanged(newConfig);
        if (mConfig != null) {
            if (mTabReparentingControllerSupplier.get() != null && didChangeTabletMode()) {
                onScreenLayoutSizeChange();
            }
            // We only handle VR UI mode and UI mode night changes. Any other changes should follow
            // the default behavior of recreating the activity. Note that if UI mode night changes,
            // with or without other changes, we will still recreate() until we get a callback from
            // the ChromeBaseAppCompatActivity#onNightModeStateChanged or the overridden method in
            // sub-classes if necessary.
            if (didChangeNonVrUiMode(mConfig.uiMode, newConfig.uiMode)
                    && !didChangeUiModeNight(mConfig.uiMode, newConfig.uiMode)) {
                recreate();
                return;
            }

            if (newConfig.densityDpi != mConfig.densityDpi) {
                if (!VrModuleProvider.getDelegate().onDensityChanged(
                            mConfig.densityDpi, newConfig.densityDpi)) {
                    recreate();
                    return;
                }
            }

            if (newConfig.orientation != mConfig.orientation) {
                RequestDesktopUtils.recordScreenOrientationChangedUkm(
                        newConfig.orientation == Configuration.ORIENTATION_LANDSCAPE,
                        getActivityTab());
            }
        }

        mConfig = newConfig;
    }

    private static boolean didChangeNonVrUiMode(int oldMode, int newMode) {
        if (oldMode == newMode) return false;
        return isInVrUiMode(oldMode) == isInVrUiMode(newMode);
    }

    private static boolean isInVrUiMode(int uiMode) {
        return (uiMode & Configuration.UI_MODE_TYPE_MASK) == Configuration.UI_MODE_TYPE_VR_HEADSET;
    }

    private static boolean didChangeUiModeNight(int oldMode, int newMode) {
        return (oldMode & Configuration.UI_MODE_NIGHT_MASK)
                != (newMode & Configuration.UI_MODE_NIGHT_MASK);
    }

    /**
     * Called by the system when the activity changes from fullscreen mode to multi-window mode
     * and visa-versa.
     * @param isInMultiWindowMode True if the activity is in multi-window mode.
     */
    @Override
    public void onMultiWindowModeChanged(boolean isInMultiWindowMode) {
        // If native is not initialized, the multi-window user action will be recorded in
        // #onDeferredStartupForMultiWindowMode() and CachedFeatureFlags#setIsInMultiWindowMode()
        // will be called in #onResumeWithNative(). Both of these methods require native to be
        // initialized, so do not call here to avoid crashing. See https://crbug.com/797921.
        if (mNativeInitialized) {
            recordMultiWindowModeChanged(isInMultiWindowMode, /* isDeferredStartup= */ false);

            if (!isInMultiWindowMode
                    && ApplicationStatus.getStateForActivity(this) == ActivityState.RESUMED) {
                // Start a new UMA session when exiting multi-window mode if the activity is
                // currently resumed. When entering multi-window Android recents gains focus, so
                // ChromeActivity will get a call to onPauseWithNative(), ending the current UMA
                // session. When exiting multi-window, however, if ChromeActivity is resumed it
                // stays in that state.
                markSessionEnd();
                markSessionResume();
                ChromeSessionState.setIsInMultiWindowMode(
                        MultiWindowUtils.getInstance().isInMultiWindowMode(this));
            }
        }

        VrModuleProvider.getDelegate().onMultiWindowModeChanged(isInMultiWindowMode);

        super.onMultiWindowModeChanged(isInMultiWindowMode);
    }

    /**
     * Records user actions and ukms associated with entering and exiting Android N multi-window
     * mode.
     * @param isInMultiWindowMode True if the activity is in multi-window mode.
     * @param isDeferredStartup True if the activity is deferred startup.
     */
    private void recordMultiWindowModeChanged(
            boolean isInMultiWindowMode, boolean isDeferredStartup) {
        MultiWindowUtils.getInstance().recordMultiWindowModeChanged(
                isInMultiWindowMode, isDeferredStartup, isFirstActivity(), getActivityTab());
    }

    /**
     * This method serves to distinguish windows in multi-window mode.
     * @return True if this activity is the first created activity.
     */
    protected boolean isFirstActivity() {
        return true;
    }

    /** Handles back press events for Chrome in various states. */
    protected final void handleOnBackPressed() {
        assert !BackPressManager.isEnabled()
            : "Back press should be handled by implementors of BackPressHandler if enabled";
        if (mNativeInitialized) RecordUserAction.record("SystemBack");

        if (TextBubble.getCountSupplier().get() != null
                && TextBubble.getCountSupplier().get() > 0) {
            // TODO(crbug.com/1279941): should this stop propagating the event?
            TextBubble.dismissBubbles();
            BackPressManager.record(Type.TEXT_BUBBLE);
        }

        if (VrModuleProvider.getDelegate().onBackPressed()) {
            BackPressManager.record(Type.VR_DELEGATE);
            return;
        }

        ArDelegate arDelegate = ArDelegateProvider.getDelegate();
        if (arDelegate != null && arDelegate.onBackPressed()) {
            BackPressManager.record(Type.AR_DELEGATE);
            return;
        }

        if (mCompositorViewHolderSupplier.hasValue()) {
            LayoutManagerImpl layoutManager =
                    mCompositorViewHolderSupplier.get().getLayoutManager();
            if (layoutManager != null && layoutManager.onBackPressed()) {
                // Back press metrics recording is handled by LayoutManagerImpl internally.
                return;
            }
        }

        SelectionPopupController controller = getSelectionPopupController();
        if (controller != null && controller.isSelectActionBarShowing()) {
            controller.clearSelection();
            BackPressManager.record(Type.SELECTION_POPUP);
            return;
        }

        if (getManualFillingComponent().onBackPressed()) {
            BackPressManager.record(Type.MANUAL_FILLING);
            return;
        }

        if (exitFullscreenIfShowing()) {
            BackPressManager.record(Type.FULLSCREEN);
            return;
        }

        if (mRootUiCoordinator.getBottomSheetController() != null
                && mRootUiCoordinator.getBottomSheetController().handleBackPress()) {
            BackPressManager.record(BackPressHandler.Type.BOTTOM_SHEET);
            return;
        }

        handleBackPressed();
    }

    private void initializeBackPressHandling() {
        if (BackPressManager.isEnabled()) {
            getOnBackPressedDispatcher().addCallback(this, mBackPressManager.getCallback());
            // TODO(crbug.com/1279941): consider move to RootUiCoordinator.
            mTextBubbleBackPressHandler = new TextBubbleBackPressHandler();
            mBackPressManager.addHandler(mTextBubbleBackPressHandler, Type.TEXT_BUBBLE);
            mBackPressManager.addHandler(VrModuleProvider.getDelegate(), Type.VR_DELEGATE);

            if (ArDelegateProvider.getDelegate() != null) {
                mBackPressManager.addHandler(ArDelegateProvider.getDelegate(), Type.AR_DELEGATE);
            }

            mLayoutManagerSupplier.addObserver((layoutManager) -> {
                assert !mBackPressManager.has(Type.SCENE_OVERLAY)
                    : "LayoutManager should be only set at most once";
                mBackPressManager.addHandler(layoutManager, Type.SCENE_OVERLAY);
            });

            mSelectionPopupBackPressInitCallback = (tabModelSelector) -> {
                assert !mBackPressManager.has(Type.SELECTION_POPUP)
                    : "Tab Model Selector should be set at most once";
                mSelectionPopupBackPressHandler =
                        new SelectionPopupBackPressHandler(tabModelSelector);
                mBackPressManager.addHandler(mSelectionPopupBackPressHandler, Type.SELECTION_POPUP);
                getTabModelSelectorSupplier().removeObserver(mSelectionPopupBackPressInitCallback);
            };
            getTabModelSelectorSupplier().addObserver(mSelectionPopupBackPressInitCallback);

            mBrowserControlsManagerSupplier.addObserver((controlManager) -> {
                assert !mBackPressManager.has(Type.FULLSCREEN)
                    : "BrowserControlManager should be set at most once";
                mBackPressManager.addHandler(
                        new FullscreenBackPressHandler(controlManager.getFullscreenManager()),
                        BackPressHandler.Type.FULLSCREEN);
            });
        } else {
            OnBackPressedCallback callback = new OnBackPressedCallback(true) {
                @Override
                public void handleOnBackPressed() {
                    ChromeActivity.this.handleOnBackPressed();
                }
            };
            getOnBackPressedDispatcher().addCallback(this, callback);
        }
    }

    @Override
    public void onTrimMemory(int level) {
        super.onTrimMemory(level);
        if (ChromeApplicationImpl.isSevereMemorySignal(level)) {
            clearToolbarResourceCache();
        }
    }

    private SelectionPopupController getSelectionPopupController() {
        WebContents webContents = getCurrentWebContents();
        return webContents != null ? SelectionPopupController.fromWebContents(webContents) : null;
    }

    @Override
    public void createContextualSearchTab(String searchUrl) {
        Tab currentTab = getActivityTab();
        if (currentTab == null) return;

        TabCreator tabCreator = getTabCreator(currentTab.isIncognito());
        if (tabCreator == null) return;

        tabCreator.createNewTab(new LoadUrlParams(searchUrl, PageTransition.LINK),
                TabLaunchType.FROM_LINK, getActivityTab());
    }

    /** Opens the chrome://management page on a new tab. */
    private void openChromeManagementPage() {
        Tab currentTab = getActivityTab();
        TabCreator tabCreator = getTabCreator(currentTab != null && currentTab.isIncognito());
        if (tabCreator == null) return;

        tabCreator.createNewTab(
                new LoadUrlParams(UrlConstants.MANAGEMENT_URL, PageTransition.AUTO_TOPLEVEL),
                TabLaunchType.FROM_CHROME_UI, getActivityTab());
    }

    /**
     * @return The {@link MenuOrKeyboardActionController} for registering menu or keyboard action
     *     handler for this activity.
     */
    public MenuOrKeyboardActionController getMenuOrKeyboardActionController() {
        return this;
    }

    @Override
    public void registerMenuOrKeyboardActionHandler(MenuOrKeyboardActionHandler handler) {
        mMenuActionHandlers.add(handler);
    }

    @Override
    public void unregisterMenuOrKeyboardActionHandler(MenuOrKeyboardActionHandler handler) {
        mMenuActionHandlers.remove(handler);
    }

    /**
     * Handles menu item selection and keyboard shortcuts.
     *
     * @param id The ID of the selected menu item (defined in main_menu.xml) or keyboard shortcut
     *     (defined in values.xml).
     * @param fromMenu Whether this was triggered from the menu.
     * @return Whether the action was handled.
     */
    @Override
    public boolean onMenuOrKeyboardAction(int id, boolean fromMenu) {
        for (MenuOrKeyboardActionController.MenuOrKeyboardActionHandler handler :
                mMenuActionHandlers) {
            if (handler.handleMenuOrKeyboardAction(id, fromMenu)) return true;
        }

        @BrowserProfileType
        int type = Profile.getBrowserProfileTypeFromProfile(getCurrentTabModel().getProfile());

        if (id == R.id.preferences_id) {
            SettingsLauncher settingsLauncher = new SettingsLauncherImpl();
            settingsLauncher.launchSettingsActivity(this);
            RecordUserAction.record("MobileMenuSettings");
            RecordHistogram.recordEnumeratedHistogram(
                    "Settings.OpenSettingsFromMenu.PerProfileType", type,
                    BrowserProfileType.MAX_VALUE + 1);
            return true;
        }

        if (id == R.id.update_menu_id) {
            UpdateMenuItemHelper.getInstance().onMenuItemClicked(this);
            return true;
        }

        final Tab currentTab = getActivityTab();

        if (id == R.id.help_id) {
            String url = currentTab != null ? currentTab.getUrl().getSpec() : "";
            Profile profile = getTabModelSelector().isIncognitoSelected()
                    ? Profile.getLastUsedRegularProfile().getPrimaryOTRProfile(
                            /*createIfNeeded=*/true)
                    : Profile.getLastUsedRegularProfile();
            startHelpAndFeedback(url, "MobileMenuFeedback", profile);
            return true;
        }

        if (id == R.id.open_history_menu_id) {
            // 'currentTab' could only be null when opening history from start surface, which is
            // not available on tablet.
            assert (isTablet() && currentTab != null) || !isTablet();
            if (currentTab != null && UrlUtilities.isNTPUrl(currentTab.getUrl())) {
                NewTabPageUma.recordAction(NewTabPageUma.ACTION_OPENED_HISTORY_MANAGER);
            }
            RecordUserAction.record("MobileMenuHistory");
            ReturnToChromeUtil.onHistoryOpened();
            HistoryManagerUtils.showHistoryManager(
                    this, currentTab, getTabModelSelector().isIncognitoSelected());
            RecordHistogram.recordEnumeratedHistogram("Android.OpenHistoryFromMenu.PerProfileType",
                    type, BrowserProfileType.MAX_VALUE + 1);
            return true;
        }

        // All the code below assumes currentTab is not null, so return early if it is null.
        if (currentTab == null) {
            return false;
        }

        if (id == R.id.forward_menu_id) {
            if (currentTab.canGoForward()) {
                currentTab.goForward();
                RecordUserAction.record("MobileMenuForward");
                return true;
            }
            return false;
        }

        if (id == R.id.bookmark_this_page_id || id == R.id.add_bookmark_menu_id
                || id == R.id.edit_bookmark_menu_id) {
            mTabBookmarkerSupplier.get().addOrEditBookmark(currentTab);
            TrackerFactory.getTrackerForProfile(Profile.getLastUsedRegularProfile())
                    .notifyEvent(EventConstants.APP_MENU_BOOKMARK_STAR_ICON_PRESSED);
            RecordUserAction.record("MobileMenuAddToBookmarks");
            return true;
        }

        if (id == R.id.add_to_reading_list_menu_id) {
            mTabBookmarkerSupplier.get().addToReadingList(currentTab);
            RecordUserAction.record("MobileMenuAddToReadingList");
            return true;
        }

        if (id == R.id.delete_from_reading_list_menu_id) {
            ReadingListUtils.deleteFromReadingList(
                    new BookmarkModel(), mSnackbarManager, /*activity=*/this, currentTab);
            RecordUserAction.record("MobileMenuDeleteFromReadingList");
            return true;
        }

        if (id == R.id.enable_price_tracking_menu_id) {
            mTabBookmarkerSupplier.get().startOrModifyPriceTracking(currentTab);
            RecordUserAction.record("MobileMenuEnablePriceTracking");
            TrackerFactory.getTrackerForProfile(Profile.getLastUsedRegularProfile())
                    .notifyEvent(EventConstants.SHOPPING_LIST_PRICE_TRACK_FROM_MENU);
            return true;
        }

        if (id == R.id.disable_price_tracking_menu_id) {
            // TODO(crbug.com/1268976): Extract this code into a one-liner.
            List<BookmarkId> bookmarkIds =
                    PowerBookmarkUtils.getBookmarkIdsWithSharedClusterIdForTab(
                            currentTab, mBookmarkBridgeSupplier.get());
            SubscriptionsManager subscriptionsManager = null;
            if (ShoppingFeatures.isShoppingListEnabled()) {
                subscriptionsManager = new CommerceSubscriptionsServiceFactory()
                                               .getForLastUsedProfile()
                                               .getSubscriptionsManager();
            }
            PowerBookmarkUtils.setPriceTrackingEnabledWithSnackbars(subscriptionsManager,
                    mBookmarkBridgeSupplier.get(), bookmarkIds,
                    /*enabled=*/false, mSnackbarManager, getResources());
            RecordUserAction.record("MobileMenuDisablePriceTracking");
            return true;
        }

        if (id == R.id.offline_page_id) {
            DownloadUtils.downloadOfflinePage(this, currentTab);
            RecordUserAction.record("MobileMenuDownloadPage");
            return true;
        }

        if (id == R.id.reload_menu_id) {
            if (currentTab.isLoading()) {
                currentTab.stopLoading();
                RecordUserAction.record("MobileMenuStop");
            } else {
                currentTab.reload();
                RecordUserAction.record("MobileMenuReload");
            }
            return true;
        }

        if (id == R.id.info_menu_id) {
            ChromePageInfo pageInfo =
                    new ChromePageInfo(getModalDialogManagerSupplier(), null, OpenedFromSource.MENU,
                            mRootUiCoordinator.getMerchantTrustSignalsCoordinatorSupplier()::get,
                            mRootUiCoordinator.getEphemeralTabCoordinatorSupplier());
            pageInfo.show(currentTab, ChromePageInfoHighlight.noHighlight());
            return true;
        }

        if (id == R.id.translate_id) {
            RecordUserAction.record("MobileMenuTranslate");
            Tracker tracker = TrackerFactory.getTrackerForProfile(
                    Profile.fromWebContents(currentTab.getWebContents()));
            tracker.notifyEvent(EventConstants.TRANSLATE_MENU_BUTTON_CLICKED);
            TranslateBridge.translateTabWhenReady(currentTab);
            return true;
        }

        if (id == R.id.print_id) {
            RecordUserAction.record("MobileMenuPrint");
            return doPrintShare(this, mActivityTabProvider);
        }

        if (id == R.id.add_to_homescreen_id) {
            RecordUserAction.record("MobileMenuAddToHomescreen");
            PwaBottomSheetController controller =
                    PwaBottomSheetControllerProvider.from(getWindowAndroid());
            if (controller != null
                    && controller.requestOrExpandBottomSheetInstaller(
                            currentTab.getWebContents(), InstallTrigger.MENU)) {
                return true;
            }
            AddToHomescreenCoordinator.showForAppMenu(this, getWindowAndroid(),
                    getModalDialogManager(), currentTab.getWebContents(), mMenuItemData);
            if (ChromeFeatureList.isEnabled(ChromeFeatureList.ADD_TO_HOMESCREEN_IPH)) {
                Tracker tracker = TrackerFactory.getTrackerForProfile(
                        Profile.fromWebContents(currentTab.getWebContents()));
                tracker.notifyEvent(EventConstants.ADD_TO_HOMESCREEN_DIALOG_SHOWN);
            }
            return true;
        }

        if (id == R.id.open_webapk_id) {
            Context context = ContextUtils.getApplicationContext();
            String packageName =
                    WebApkValidator.queryFirstWebApkPackage(context, currentTab.getUrl().getSpec());
            Intent launchIntent = WebApkNavigationClient.createLaunchWebApkIntent(
                    packageName, currentTab.getUrl().getSpec(), false);
            try {
                context.startActivity(launchIntent);
                RecordUserAction.record("MobileMenuOpenWebApk");
            } catch (ActivityNotFoundException e) {
                Toast.makeText(context, R.string.open_webapk_failed, Toast.LENGTH_SHORT).show();
            }
            return true;
        }

        if (id == R.id.request_desktop_site_id || id == R.id.request_desktop_site_check_id) {
            boolean usingDesktopUserAgent =
                    currentTab.getWebContents().getNavigationController().getUseDesktopUserAgent();
            usingDesktopUserAgent = !usingDesktopUserAgent;
            if (ContentFeatureList.isEnabled(ContentFeatureList.REQUEST_DESKTOP_SITE_EXCEPTIONS)) {
                Profile profile = getCurrentTabModel().getProfile();
                RequestDesktopUtils.setRequestDesktopSiteContentSettingsForUrl(
                        profile, currentTab.getUrl(), usingDesktopUserAgent);
                currentTab.reload();
            } else {
                TabUtils.switchUserAgent(currentTab, usingDesktopUserAgent, /* forcedByUser */ true,
                        UseDesktopUserAgentCaller.ON_MENU_OR_KEYBOARD_ACTION);
            }
            RequestDesktopUtils.recordUserChangeUserAgent(usingDesktopUserAgent, getActivityTab());
            return true;
        }

        if (id == R.id.auto_dark_web_contents_id || id == R.id.auto_dark_web_contents_check_id) {
            // Get values needed to check/enable auto dark for the current site.
            Profile profile = getCurrentTabModel().getProfile();
            GURL url = currentTab.getUrl();

            // Flip auto dark state.
            boolean isEnabled = WebContentsDarkModeController.isEnabledForUrl(profile, url);
            WebContentsDarkModeController.setEnabledForUrl(profile, url, !isEnabled);
            currentTab.getWebContents().notifyRendererPreferenceUpdate();

            WebContentsDarkModeController.recordAutoDarkUkm(
                    currentTab.getWebContents(), !isEnabled);

            // Show dialog informing user how to disable the feature globally and give feedback if
            // disabling through the app menu for the nth time (determined by feature engagement).
            if (isEnabled) {
                WebContentsDarkModeMessageController.attemptToShowDialog(this, profile,
                        url.getSpec(), getModalDialogManager(), new SettingsLauncherImpl(),
                        HelpAndFeedbackLauncherImpl.getInstance());
            }

            return true;
        }

        if (id == R.id.reader_mode_prefs_id) {
            DomDistillerUIUtils.openSettings(currentTab.getWebContents());
            return true;
        }

        if (id == R.id.managed_by_menu_id) {
            openChromeManagementPage();
            return true;
        }

        return false;
    }

    /**
     * Shows Help and Feedback and records the user action as well.
     * @param url The URL of the tab the user is currently on.
     * @param recordAction The user action to record.
     * @param profile The current {@link Profile}.
     */
    public void startHelpAndFeedback(String url, String recordAction, Profile profile) {
        // Since reading back the compositor is asynchronous, we need to do the readback
        // before starting the GoogleHelp.
        String helpContextId = HelpAndFeedbackLauncherImpl.getHelpContextIdFromUrl(
                this, url, getCurrentTabModel().isIncognito());
        HelpAndFeedbackLauncherImpl.getInstance().show(this, helpContextId, profile, url);
        RecordUserAction.record(recordAction);
    }

    private void markSessionResume() {
        // Start new session for UMA.
        if (mUmaSessionStats == null) {
            mUmaSessionStats = new UmaSessionStats(this);
        }

        UmaSessionStats.updateMetricsServiceState();
        mUmaSessionStats.startNewSession(getTabModelSelector(), getWindowAndroid());
    }

    /**
     * Mark that the UMA session has ended.
     */
    private void markSessionEnd() {
        if (mUmaSessionStats == null) {
            // If you hit this assert, please update crbug.com/172653 on how you got there.
            assert false;
            return;
        }
        // Record session metrics.
        mUmaSessionStats.logAndEndSession();
    }

    public final void postDeferredStartupIfNeeded() {
        if (!mNativeInitialized) {
            // Native hasn't loaded yet.  Queue it up for later.
            mDeferredStartupQueued = true;
            return;
        }
        mDeferredStartupQueued = false;

        if (!mDeferredStartupPosted) {
            mDeferredStartupPosted = true;
            onDeferredStartup();
        }
    }

    @Override
    public void terminateIncognitoSession() {}

    @Override
    public void onTabSelectionHinted(int tabId) {}

    @Override
    public void onSceneChange(Layout layout) {}

    @Override
    public void onAttachFragment(Fragment fragment) {
        if (mRootUiCoordinator == null) return;
        mRootUiCoordinator.onAttachFragment(fragment);
    }

    /**
     * Looks up the Chrome activity of the given web contents. This can be null. Should never be
     * cached, because web contents can change activities, e.g., when user selects "Open in Chrome"
     * menu item.
     *
     * @param webContents The web contents for which to lookup the Chrome activity.
     * @return Possibly null Chrome activity that should never be cached.
     * @deprecated Use {@link ActivityUtils#getActivityFromWebContents(WebContents)} instead.
     */
    @Nullable
    @Deprecated
    public static ChromeActivity fromWebContents(@Nullable WebContents webContents) {
        Activity activity = ActivityUtils.getActivityFromWebContents(webContents);
        if (!(activity instanceof ChromeActivity)) return null;

        return (ChromeActivity) activity;
    }

    private void setLowEndTheme() {
        if (ActivityUtils.getThemeId() == R.style.Theme_Chromium_WithWindowAnimation_LowEnd) {
            setTheme(R.style.Theme_Chromium_WithWindowAnimation_LowEnd);
        }
    }

    /**
     * Records histograms related to display dimensions.
     */
    private void recordDisplayDimensions() {
        DisplayAndroid display = DisplayAndroid.getNonMultiDisplay(this);
        int displayWidth = DisplayUtil.pxToDp(display, display.getDisplayWidth());
        int displayHeight = DisplayUtil.pxToDp(display, display.getDisplayHeight());
        int smallestDisplaySize = Math.min(displayWidth, displayHeight);
        int largestDisplaySize = Math.max(displayWidth, displayHeight);

        // 10dp granularity.
        RecordHistogram.recordLinearCountHistogram(
                "Android.DeviceSize.SmallestDisplaySize2", smallestDisplaySize, 100, 1000, 92);
        // 20dp granularity.
        RecordHistogram.recordLinearCountHistogram(
                "Android.DeviceSize.LargestDisplaySize2", largestDisplaySize, 200, 2000, 92);
    }

    @Override
    public boolean onActivityResultWithNative(int requestCode, int resultCode, Intent intent) {
        if (super.onActivityResultWithNative(requestCode, resultCode, intent)) return true;
        if (VrModuleProvider.getDelegate().onActivityResultWithNative(requestCode, resultCode)) {
            return true;
        }
        return false;
    }

    /**
     * Called when VR mode is entered using this activity. 2D UI components that steal focus or
     * draw over VR contents should be hidden in this call.
     */
    public void onEnterVr() {}

    /**
     * Called when VR mode using this activity is exited. Any state set for VR should be restored
     * in this call, including showing 2D UI that was hidden.
     */
    public void onExitVr() {}

    private void clearToolbarResourceCache() {
        View v = findViewById(R.id.control_container);
        try {
            ControlContainer controlContainer = (ControlContainer) v;
            if (controlContainer != null) {
                controlContainer.getToolbarResourceAdapter().dropCachedBitmap();
            }
        } catch (ClassCastException e) {
            // This is a workaround for crbug.com/1236981. Doing nothing here is better than
            // crashing. We assert, which will be stripped in builds that get shipped to users.
            Log.e(TAG, "crbug.com/1236981", e);
            String extraInfo = "";
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                extraInfo = " inflated from layout ID #" + v.getSourceLayoutResId();
            }
            assert false : "View " + v.toString() + extraInfo + " was not a ControlContainer. "
                           + " If you can reproduce, post in crbug.com/1236981";
        }
    }

    @Override
    public void startActivity(Intent intent) {
        startActivity(intent, null);
    }

    @Override
    public void startActivity(Intent intent, Bundle options) {
        if (VrModuleProvider.getDelegate().canLaunch2DIntents()
                || VrModuleProvider.getIntentDelegate().isVrIntent(intent)) {
            if (VrModuleProvider.getDelegate().isInVr()) {
                VrModuleProvider.getIntentDelegate().setupVrIntent(intent);
            }
            super.startActivity(intent, options);
            return;
        }
        VrModuleProvider.getDelegate().requestToExitVrAndRunOnSuccess(() -> {
            if (!VrModuleProvider.getDelegate().canLaunch2DIntents()) {
                throw new IllegalStateException("Still in VR after having exited VR.");
            }
            super.startActivity(intent, options);
        });
    }

    @Override
    public void startActivityForResult(Intent intent, int requestCode) {
        startActivityForResult(intent, requestCode, null);
    }

    @Override
    public void startActivityForResult(Intent intent, int requestCode, Bundle options) {
        if (VrModuleProvider.getDelegate().canLaunch2DIntents()
                || VrModuleProvider.getIntentDelegate().isVrIntent(intent)) {
            super.startActivityForResult(intent, requestCode, options);
            return;
        }
        VrModuleProvider.getDelegate().requestToExitVrAndRunOnSuccess(() -> {
            if (!VrModuleProvider.getDelegate().canLaunch2DIntents()) {
                throw new IllegalStateException("Still in VR after having exited VR.");
            }
            super.startActivityForResult(intent, requestCode, options);
        });
    }

    @Override
    public boolean startActivityIfNeeded(Intent intent, int requestCode) {
        return startActivityIfNeeded(intent, requestCode, null);
    }

    @Override
    public boolean startActivityIfNeeded(Intent intent, int requestCode, Bundle options) {
        // Avoid starting Activities when possible while in VR.
        if (VrModuleProvider.getDelegate().isInVr()
                && !VrModuleProvider.getIntentDelegate().isVrIntent(intent)) {
            return false;
        }
        return super.startActivityIfNeeded(intent, requestCode, options);
    }

    /**
     * TODO(https://crbug.com/931496): Revisit this as part of the broader discussion around
     * activity-specific UI customizations.
     * @return Whether this Activity supports the App Menu.
     */
    public boolean supportsAppMenu() {
        // Derived classes that disable the toolbar should also have the Menu disabled without
        // having to explicitly disable the Menu as well.
        return getToolbarLayoutId() != ActivityUtils.NO_RESOURCE_ID;
    }

    /**
     * @return  Whether this activity supports the find in page feature.
     */
    public boolean supportsFindInPage() {
        return true;
    }

    @VisibleForTesting
    public RootUiCoordinator getRootUiCoordinatorForTesting() {
        return mRootUiCoordinator;
    }

    // NightModeStateProvider.Observer implementation.
    @Override
    public void onNightModeStateChanged() {
        // Note: order matters here because the call to super will recreate the activity.
        // Note: it's possible for this method to be called before mNightModeReparentingController
        // is constructed.
        if (mTabReparentingControllerSupplier.get() != null) {
            mTabReparentingControllerSupplier.get().prepareTabsForReparenting();
        }
        super.onNightModeStateChanged();
    }

    @VisibleForTesting
    public boolean didChangeTabletMode() {
        assert mConfig
                != null : "Can not determine the tablet mode when mConfig is not initialized";
        int smallestWidth = getCurrentSmallestScreenWidth(this);
        boolean isTablet = smallestWidth >= DeviceFormFactor.MINIMUM_TABLET_WIDTH_DP;
        boolean wasTablet =
                mConfig.smallestScreenWidthDp >= DeviceFormFactor.MINIMUM_TABLET_WIDTH_DP;
        return wasTablet != isTablet;
    }

    /**
     * Switch between phone and tablet mode and do the tab re-parenting in the meantime.
     */
    private void onScreenLayoutSizeChange() {
        if (mTabReparentingControllerSupplier.get() != null && !mIsTabReparentingPrepared) {
            mTabReparentingControllerSupplier.get().prepareTabsForReparenting();
            mIsTabReparentingPrepared = true;
            if (!isFinishing()) recreate();
        }
    }

    @VisibleForTesting
    @Nullable
    public BookmarkBridge getBookmarkBridgeForTesting() {
        return mBookmarkBridgeSupplier.get();
    }

    @VisibleForTesting
    public Configuration getSavedConfigurationForTesting() {
        return mConfig;
    }

    @VisibleForTesting
    public boolean deferredStartupPostedForTesting() {
        return mDeferredStartupPosted;
    }

    @VisibleForTesting
    public DisplayAndroidObserver getDisplayAndroidObserverForTesting() {
        return mDisplayAndroidObserver;
    }

    @VisibleForTesting
    public BackPressManager getBackPressManagerForTesting() {
        return mBackPressManager;
    }

    /** Returns whether the print action was successfully started. */
    private boolean doPrintShare(Activity activity, Supplier<Tab> currentTabSupplier) {
        PrintingController printingController = PrintingControllerImpl.getInstance();

        if (!currentTabSupplier.hasValue()) return false;
        if (printingController == null || printingController.isBusy()) return false;
        if (!UserPrefs.get(Profile.getLastUsedRegularProfile()).getBoolean(Pref.PRINTING_ENABLED)) {
            return false;
        }
        printingController.startPrint(
                new TabPrinter(currentTabSupplier.get()), new PrintManagerDelegateImpl(activity));
        return true;
    }

    /**
     * Returns a {@link CompositorViewHolder} instance for testing.
     */
    public CompositorViewHolder getCompositorViewHolderForTesting() {
        return mCompositorViewHolderSupplier.get();
    }
}
