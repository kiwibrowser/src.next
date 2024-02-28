// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser;

import static org.chromium.chrome.browser.ui.IncognitoRestoreAppLaunchDrawBlocker.IS_INCOGNITO_SELECTED;

import android.content.Intent;
import android.content.pm.ShortcutManager;
import android.content.res.Configuration;
import android.os.Build;
import android.os.Bundle;
import android.os.SystemClock;
import android.provider.Browser;
import android.text.TextUtils;
import android.text.format.DateUtils;
import android.util.Pair;
import android.view.KeyEvent;
import android.view.KeyboardShortcutGroup;
import android.view.Menu;
import android.view.View;
import android.view.View.OnClickListener;
import android.view.ViewConfiguration;
import android.view.ViewGroup;
import android.view.ViewStub;
import android.view.Window;
import android.view.WindowManager;

import androidx.annotation.IntDef;
import androidx.annotation.Nullable;
import androidx.annotation.RequiresApi;
import androidx.annotation.VisibleForTesting;
import androidx.lifecycle.Lifecycle;
import androidx.lifecycle.LifecycleObserver;
import androidx.lifecycle.LifecycleRegistry;

import org.chromium.base.ApplicationStatus;
import org.chromium.base.BuildInfo;
import org.chromium.base.Callback;
import org.chromium.base.CallbackController;
import org.chromium.base.CommandLine;
import org.chromium.base.IntentUtils;
import org.chromium.base.Log;
import org.chromium.base.MemoryPressureListener;
import org.chromium.base.ThreadUtils;
import org.chromium.base.TraceEvent;
import org.chromium.base.jank_tracker.JankTracker;
import org.chromium.base.jank_tracker.JankTrackerImpl;
import org.chromium.base.jank_tracker.PlaceholderJankTracker;
import org.chromium.base.library_loader.LibraryLoader;
import org.chromium.base.metrics.RecordHistogram;
import org.chromium.base.metrics.RecordUserAction;
import org.chromium.base.supplier.LazyOneshotSupplier;
import org.chromium.base.supplier.ObservableSupplierImpl;
import org.chromium.base.supplier.OneShotCallback;
import org.chromium.base.supplier.OneshotSupplier;
import org.chromium.base.supplier.OneshotSupplierImpl;
import org.chromium.base.supplier.Supplier;
import org.chromium.base.supplier.UnownedUserDataSupplier;
import org.chromium.base.task.PostTask;
import org.chromium.base.task.TaskTraits;
import org.chromium.build.annotations.UsedByReflection;
import org.chromium.cc.input.BrowserControlsState;
import org.chromium.chrome.R;
import org.chromium.chrome.browser.IntentHandler.TabOpenType;
import org.chromium.chrome.browser.app.ChromeActivity;
import org.chromium.chrome.browser.app.metrics.LaunchCauseMetrics;
import org.chromium.chrome.browser.app.metrics.TabbedActivityLaunchCauseMetrics;
import org.chromium.chrome.browser.app.tabmodel.AsyncTabParamsManagerSingleton;
import org.chromium.chrome.browser.app.tabmodel.ChromeNextTabPolicySupplier;
import org.chromium.chrome.browser.app.tabmodel.TabModelOrchestrator;
import org.chromium.chrome.browser.app.tabmodel.TabWindowManagerSingleton;
import org.chromium.chrome.browser.app.tabmodel.TabbedModeTabModelOrchestrator;
import org.chromium.chrome.browser.auxiliary_search.AuxiliarySearchController;
import org.chromium.chrome.browser.auxiliary_search.AuxiliarySearchControllerFactory;
import org.chromium.chrome.browser.back_press.BackPressManager;
import org.chromium.chrome.browser.back_press.MinimizeAppAndCloseTabBackPressHandler;
import org.chromium.chrome.browser.back_press.MinimizeAppAndCloseTabBackPressHandler.MinimizeAppAndCloseTabType;
import org.chromium.chrome.browser.base.ColdStartTracker;
import org.chromium.chrome.browser.bookmarks.BookmarkUtils;
import org.chromium.chrome.browser.browserservices.intents.WebappConstants;
import org.chromium.chrome.browser.compositor.CompositorViewHolder;
import org.chromium.chrome.browser.compositor.layouts.Layout;
import org.chromium.chrome.browser.compositor.layouts.LayoutManagerChrome;
import org.chromium.chrome.browser.compositor.layouts.LayoutManagerChromePhone;
import org.chromium.chrome.browser.compositor.layouts.LayoutManagerChromeTablet;
import org.chromium.chrome.browser.compositor.overlays.strip.StripLayoutHelperManager.TabModelStartupInfo;
import org.chromium.chrome.browser.cookies.CookiesFetcher;
import org.chromium.chrome.browser.crash.ChromePureJavaExceptionReporter;
import org.chromium.chrome.browser.crypto.CipherFactory;
import org.chromium.chrome.browser.dependency_injection.ChromeActivityComponent;
import org.chromium.chrome.browser.device.DeviceClassManager;
import org.chromium.chrome.browser.dom_distiller.ReaderModeManager;
import org.chromium.chrome.browser.download.DownloadNotificationService;
import org.chromium.chrome.browser.download.DownloadOpenSource;
import org.chromium.chrome.browser.download.DownloadUtils;
import org.chromium.chrome.browser.dragdrop.ChromeDragAndDropBrowserDelegate;
import org.chromium.chrome.browser.feature_engagement.TrackerFactory;
import org.chromium.chrome.browser.feature_guide.notifications.FeatureNotificationUtils;
import org.chromium.chrome.browser.feed.FeedSurfaceTracker;
import org.chromium.chrome.browser.firstrun.FirstRunSignInProcessor;
import org.chromium.chrome.browser.flags.ActivityType;
import org.chromium.chrome.browser.flags.ChromeFeatureList;
import org.chromium.chrome.browser.flags.ChromeSwitches;
import org.chromium.chrome.browser.fonts.FontPreloader;
import org.chromium.chrome.browser.gesturenav.NavigationSheet;
import org.chromium.chrome.browser.homepage.HomepageManager;
import org.chromium.chrome.browser.hub.DefaultPaneOrderController;
import org.chromium.chrome.browser.hub.HubFieldTrial;
import org.chromium.chrome.browser.hub.HubLayoutDependencyHolder;
import org.chromium.chrome.browser.hub.HubManager;
import org.chromium.chrome.browser.hub.HubProvider;
import org.chromium.chrome.browser.hub.Pane;
import org.chromium.chrome.browser.hub.PaneId;
import org.chromium.chrome.browser.incognito.IncognitoNotificationManager;
import org.chromium.chrome.browser.incognito.IncognitoNotificationPresenceController;
import org.chromium.chrome.browser.incognito.IncognitoProfileDestroyer;
import org.chromium.chrome.browser.incognito.IncognitoStartup;
import org.chromium.chrome.browser.incognito.IncognitoTabLauncher;
import org.chromium.chrome.browser.incognito.IncognitoTabbedSnapshotController;
import org.chromium.chrome.browser.incognito.IncognitoUtils;
import org.chromium.chrome.browser.init.ActivityProfileProvider;
import org.chromium.chrome.browser.layouts.LayoutStateProvider;
import org.chromium.chrome.browser.layouts.LayoutType;
import org.chromium.chrome.browser.lifecycle.ActivityLifecycleDispatcher;
import org.chromium.chrome.browser.locale.LocaleManager;
import org.chromium.chrome.browser.magic_stack.ModuleDelegate.ModuleType;
import org.chromium.chrome.browser.magic_stack.ModuleRegistry;
import org.chromium.chrome.browser.metrics.AndroidSessionDurationsServiceState;
import org.chromium.chrome.browser.metrics.ExperimentalStartupMetricsTracker;
import org.chromium.chrome.browser.metrics.LaunchMetrics;
import org.chromium.chrome.browser.metrics.MainIntentBehaviorMetrics;
import org.chromium.chrome.browser.metrics.SimpleStartupForegroundSessionDetector;
import org.chromium.chrome.browser.modaldialog.ChromeTabModalPresenter;
import org.chromium.chrome.browser.multiwindow.MultiInstanceChromeTabbedActivity;
import org.chromium.chrome.browser.multiwindow.MultiInstanceManager;
import org.chromium.chrome.browser.multiwindow.MultiWindowUtils;
import org.chromium.chrome.browser.multiwindow.MultiWindowUtils.InstanceAllocationType;
import org.chromium.chrome.browser.native_page.NativePageAssassin;
import org.chromium.chrome.browser.navigation_predictor.NavigationPredictorBridge;
import org.chromium.chrome.browser.new_tab_url.DseNewTabUrlManager;
import org.chromium.chrome.browser.ntp.NewTabPageLaunchOrigin;
import org.chromium.chrome.browser.ntp.NewTabPageUma;
import org.chromium.chrome.browser.ntp.NewTabPageUtils;
import org.chromium.chrome.browser.offlinepages.OfflinePageUtils;
import org.chromium.chrome.browser.omnibox.OmniboxFocusReason;
import org.chromium.chrome.browser.paint_preview.StartupPaintPreviewHelper;
import org.chromium.chrome.browser.paint_preview.StartupPaintPreviewHelperSupplier;
import org.chromium.chrome.browser.partnercustomizations.PartnerBrowserCustomizations;
import org.chromium.chrome.browser.preferences.ChromePreferenceKeys;
import org.chromium.chrome.browser.preferences.ChromeSharedPreferences;
import org.chromium.chrome.browser.price_change.PriceChangeModuleBuilder;
import org.chromium.chrome.browser.profiles.OTRProfileID;
import org.chromium.chrome.browser.profiles.Profile;
import org.chromium.chrome.browser.profiles.ProfileManager;
import org.chromium.chrome.browser.profiles.ProfileProvider;
import org.chromium.chrome.browser.quick_delete.QuickDeleteController;
import org.chromium.chrome.browser.quick_delete.QuickDeleteDelegateImpl;
import org.chromium.chrome.browser.quick_delete.QuickDeleteMetricsDelegate;
import org.chromium.chrome.browser.read_later.ReadingListBackPressHandler;
import org.chromium.chrome.browser.read_later.ReadingListUtils;
import org.chromium.chrome.browser.reengagement.ReengagementNotificationController;
import org.chromium.chrome.browser.search_engines.SearchEngineChoiceNotification;
import org.chromium.chrome.browser.settings.SettingsLauncherImpl;
import org.chromium.chrome.browser.share.ShareHelper;
import org.chromium.chrome.browser.share.send_tab_to_self.SendTabToSelfAndroidBridge;
import org.chromium.chrome.browser.signin.services.IdentityServicesProvider;
import org.chromium.chrome.browser.suggestions.SuggestionsMetrics;
import org.chromium.chrome.browser.survey.ChromeSurveyController;
import org.chromium.chrome.browser.sync.SyncServiceFactory;
import org.chromium.chrome.browser.sync.ui.SyncErrorMessage;
import org.chromium.chrome.browser.tab.EmptyTabObserver;
import org.chromium.chrome.browser.tab.RedirectHandlerTabHelper;
import org.chromium.chrome.browser.tab.Tab;
import org.chromium.chrome.browser.tab.TabAssociatedApp;
import org.chromium.chrome.browser.tab.TabCreationState;
import org.chromium.chrome.browser.tab.TabDelegateFactory;
import org.chromium.chrome.browser.tab.TabLaunchType;
import org.chromium.chrome.browser.tab.TabObscuringHandler;
import org.chromium.chrome.browser.tab.TabSelectionType;
import org.chromium.chrome.browser.tab.tab_restore.HistoricalTabModelObserver;
import org.chromium.chrome.browser.tabbed_mode.TabbedAppMenuPropertiesDelegate;
import org.chromium.chrome.browser.tabbed_mode.TabbedRootUiCoordinator;
import org.chromium.chrome.browser.tabmodel.ChromeTabCreator;
import org.chromium.chrome.browser.tabmodel.ChromeTabCreator.OverviewNtpCreator;
import org.chromium.chrome.browser.tabmodel.IncognitoTabHost;
import org.chromium.chrome.browser.tabmodel.IncognitoTabHostRegistry;
import org.chromium.chrome.browser.tabmodel.IncognitoTabHostUtils;
import org.chromium.chrome.browser.tabmodel.NextTabPolicy.NextTabPolicySupplier;
import org.chromium.chrome.browser.tabmodel.TabModel;
import org.chromium.chrome.browser.tabmodel.TabModelSelector;
import org.chromium.chrome.browser.tabmodel.TabModelSelectorBase;
import org.chromium.chrome.browser.tabmodel.TabModelSelectorObserver;
import org.chromium.chrome.browser.tabmodel.TabModelSelectorTabModelObserver;
import org.chromium.chrome.browser.tabmodel.TabModelSelectorTabObserver;
import org.chromium.chrome.browser.tabmodel.TabModelUtils;
import org.chromium.chrome.browser.tabmodel.TabWindowManager;
import org.chromium.chrome.browser.tasks.EngagementTimeUtil;
import org.chromium.chrome.browser.tasks.HomeSurfaceTracker;
import org.chromium.chrome.browser.tasks.JourneyManager;
import org.chromium.chrome.browser.tasks.ReturnToChromeUtil;
import org.chromium.chrome.browser.tasks.ReturnToChromeUtil.ReturnToChromeBackPressHandler;
import org.chromium.chrome.browser.tasks.TasksUma;
import org.chromium.chrome.browser.tasks.tab_management.CloseAllTabsDialog;
import org.chromium.chrome.browser.tasks.tab_management.TabGroupUi;
import org.chromium.chrome.browser.tasks.tab_management.TabManagementDelegateProvider;
import org.chromium.chrome.browser.tasks.tab_management.TabSwitcher;
import org.chromium.chrome.browser.toolbar.ToolbarIntentMetadata;
import org.chromium.chrome.browser.toolbar.ToolbarManager;
import org.chromium.chrome.browser.toolbar.top.ToolbarControlContainer;
import org.chromium.chrome.browser.ui.AppLaunchDrawBlocker;
import org.chromium.chrome.browser.ui.IncognitoRestoreAppLaunchDrawBlockerFactory;
import org.chromium.chrome.browser.ui.RootUiCoordinator;
import org.chromium.chrome.browser.ui.appmenu.AppMenuPropertiesDelegate;
import org.chromium.chrome.browser.ui.native_page.NativePage;
import org.chromium.chrome.browser.undo_tab_close_snackbar.UndoBarController;
import org.chromium.chrome.browser.usage_stats.UsageStatsService;
import org.chromium.chrome.browser.util.ChromeAccessibilityUtil;
import org.chromium.chrome.features.start_surface.StartSurface;
import org.chromium.chrome.features.start_surface.StartSurfaceConfiguration;
import org.chromium.chrome.features.start_surface.StartSurfaceDelegate;
import org.chromium.chrome.features.start_surface.StartSurfaceState;
import org.chromium.chrome.features.start_surface.StartSurfaceUserData;
import org.chromium.chrome.features.tasks.SingleTabModuleBuilder;
import org.chromium.components.browser_ui.settings.SettingsLauncher;
import org.chromium.components.browser_ui.util.BrowserControlsVisibilityDelegate;
import org.chromium.components.browser_ui.util.ComposedBrowserControlsVisibilityDelegate;
import org.chromium.components.browser_ui.util.FirstDrawDetector;
import org.chromium.components.browser_ui.widget.gesture.BackPressHandler;
import org.chromium.components.embedder_support.util.UrlConstants;
import org.chromium.components.embedder_support.util.UrlUtilities;
import org.chromium.components.embedder_support.util.UrlUtilitiesJni;
import org.chromium.components.external_intents.ExternalNavigationHandler;
import org.chromium.components.feature_engagement.EventConstants;
import org.chromium.components.feature_engagement.Tracker;
import org.chromium.components.messages.MessageDispatcherProvider;
import org.chromium.components.profile_metrics.BrowserProfileType;
import org.chromium.components.user_prefs.UserPrefs;
import org.chromium.components.webapps.ShortcutSource;
import org.chromium.content_public.browser.LoadUrlParams;
import org.chromium.content_public.browser.NavigationHandle;
import org.chromium.content_public.browser.RenderFrameHost;
import org.chromium.content_public.browser.WebContents;
import org.chromium.content_public.common.ContentSwitches;
import org.chromium.ui.base.DeviceFormFactor;
import org.chromium.ui.base.PageTransition;
import org.chromium.ui.dragdrop.DragAndDropDelegate;
import org.chromium.ui.dragdrop.DragAndDropDelegateImpl;
import org.chromium.ui.widget.Toast;
import org.chromium.url.GURL;

import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.atomic.AtomicBoolean;

/**
 * This is the main activity for ChromeMobile when not running in document mode.  All the tabs
 * are accessible via a chrome specific tab switching UI.
 */
public class ChromeTabbedActivity extends ChromeActivity<ChromeActivityComponent> {
    private static final String TAG = "ChromeTabbedActivity";

    protected static final String WINDOW_INDEX = "window_index";

    private static final int INVALID_WINDOW_ID = TabWindowManager.INVALID_WINDOW_INDEX;

    // How long to delay closing the current tab when our app is minimized.  Have to delay this
    // so that we don't show the contents of the next tab while minimizing.
    private static final long CLOSE_TAB_ON_MINIMIZE_DELAY_MS = 500;

    // Maximum delay for initial tab creation. This is for homepage and NTP, not previous tabs
    // restore. This is needed because we do not know when reading PartnerBrowserCustomizations
    // provider will be finished.
    private static final int INITIAL_TAB_CREATION_TIMEOUT_MS = 500;

    /**
     * Sending an intent with this action to Chrome will cause it to close all tabs
     * (iff the --enable-test-intents command line flag is set). If a URL is supplied in the
     * intent data, this will be loaded and unaffected by the close all action.
     */
    private static final String ACTION_CLOSE_TABS =
            "com.google.android.apps.chrome.ACTION_CLOSE_TABS";

    // Name of the ChromeTabbedActivity alias that handles MAIN intents.
    public static final String MAIN_LAUNCHER_ACTIVITY_NAME = "com.google.android.apps.chrome.Main";

    public static final SettingsLauncher SETTINGS_LAUNCHER = new SettingsLauncherImpl();

    public static final Set<String> TABBED_MODE_COMPONENT_NAMES =
            Set.of(
                    ChromeTabbedActivity.class.getName(),
                    MultiInstanceChromeTabbedActivity.class.getName(),
                    ChromeTabbedActivity2.class.getName(),
                    MAIN_LAUNCHER_ACTIVITY_NAME);

    static final String HISTOGRAM_EXPLICIT_VIEW_INTENT_FINISHED_NEW_ACTIVITY =
            "Android.ExplicitViewIntentFinishedNewTabbedActivity";

    private static final String TAG_MULTI_INSTANCE = "MultiInstance";
    private static final String SOURCE_ACTIVITY_REFERRER_OS = "android-app://android";

    /**
     * Identifies a histogram to use in {@link #maybeDispatchExplicitMainViewIntent(Intent, int)}.
     */
    @IntDef({DispatchedBy.ON_CREATE, DispatchedBy.ON_NEW_INTENT})
    @Retention(RetentionPolicy.SOURCE)
    private @interface DispatchedBy {
        int ON_CREATE = 1;
        int ON_NEW_INTENT = 2;
    }

    // Time histogram used to track time to inflate tab switcher views.
    private static final String TAB_SWITCHER_CREATION_TIME = "Android.TabSwitcher.CreationTime";

    private final MainIntentBehaviorMetrics mMainIntentMetrics;
    private @Nullable MultiInstanceManager mMultiInstanceManager;

    private UndoBarController mUndoBarPopupController;

    private LayoutManagerChrome mLayoutManager;

    private ViewGroup mContentContainer;

    private ToolbarControlContainer mControlContainer;

    private TabbedModeTabModelOrchestrator mTabModelOrchestrator;
    private TabModelSelectorBase mTabModelSelector;
    private TabModelSelectorObserver mTabModelSelectorObserver;
    private TabModelSelectorTabObserver mTabModelSelectorTabObserver;
    private TabModelSelectorTabModelObserver mTabModelObserver;
    private HistoricalTabModelObserver mHistoricalTabModelObserver;

    private BrowserControlsVisibilityDelegate mVrBrowserControlsVisibilityDelegate;

    private boolean mUIWithNativeInitialized;

    private LocaleManager mLocaleManager;

    private AppIndexingUtil mAppIndexingUtil;

    private Runnable mShowHistoryRunnable;

    private CompositorViewHolder mCompositorViewHolder;

    /** Keeps track of whether or not a specific tab was created based on the startup intent. */
    private boolean mCreatedTabOnStartup;

    // Whether or not the initial tab is being created.
    private boolean mPendingInitialTabCreation;

    // Whether {@link setInitialOverviewState()} has been called within the current onStart/onStop
    // session.
    private boolean mHasDeterminedOverviewStateForCurrentSession;

    /** Keeps track of the pref for the last time since this activity was stopped. */
    private ChromeInactivityTracker mInactivityTracker;

    /** The controller for the auxiliary search. */
    private @Nullable AuxiliarySearchController mAuxiliarySearchController;

    // This is the cached value of IntentHandler#shouldIgnoreIntent and shouldn't be read directly.
    // Use #shouldIgnoreIntent instead.
    private Boolean mShouldIgnoreIntent;

    // Listens to FrameMetrics and records janks.
    private JankTracker mJankTracker;

    // Supplier for a dependency to inform about the type of intent used to launch Chrome.
    private OneshotSupplierImpl<ToolbarIntentMetadata> mIntentMetadataOneshotSupplier =
            new OneshotSupplierImpl<>();

    /** Whether the StartSurface is shown when Chrome is launched. */
    private boolean mOverviewShownOnStart;

    // Whether the activity is staring from a resumption. False if the activity is starting from
    // onCreate(), a cold startup.
    private boolean mFromResumption;

    private NextTabPolicySupplier mNextTabPolicySupplier;

    private final UnownedUserDataSupplier<StartupPaintPreviewHelper>
            mStartupPaintPreviewHelperSupplier = new StartupPaintPreviewHelperSupplier();

    private final OneshotSupplierImpl<LayoutStateProvider> mLayoutStateProviderSupplier =
            new OneshotSupplierImpl<>();

    private final OneshotSupplierImpl<StartSurface> mStartSurfaceSupplier =
            new OneshotSupplierImpl<>();
    private final OneshotSupplierImpl<TabSwitcher> mTabSwitcherSupplier =
            new OneshotSupplierImpl<>();
    private final OneshotSupplierImpl<TabSwitcher> mIncognitoTabSwitcherSupplier =
            new OneshotSupplierImpl<>();
    private ObservableSupplierImpl<Tab> mStartSurfaceParentTabSupplier =
            new ObservableSupplierImpl<>();
    private HubProvider mHubProvider;
    private OneshotSupplierImpl<HubManager> mHubManagerSupplier = new OneshotSupplierImpl<>();
    private ObservableSupplierImpl<TabModelStartupInfo> mTabModelStartupInfoSupplier;
    // Calls isStartSurfaceRefactorEnabled() instead of using this variable directly.
    private Boolean mIsStartSurfaceRefactorEnabled;

    private CallbackController mCallbackController = new CallbackController();
    private TabbedModeTabDelegateFactory mTabDelegateFactory;

    private final AppLaunchDrawBlocker mAppLaunchDrawBlocker;

    private ReturnToChromeBackPressHandler mReturnToChromeBackPressHandler;

    // Only true if both Start surface and Start surface refactor are enabled.
    private Boolean mIsHandleTabSwitcherShownEnabled;
    private ReadingListBackPressHandler mReadingListBackPressHandler;
    private MinimizeAppAndCloseTabBackPressHandler mMinimizeAppAndCloseTabBackPressHandler;

    private HomeSurfaceTracker mHomeSurfaceTracker;

    // ID assigned to each ChromeTabbedActivity instance in Android S+ where multi-instance feature
    // is supported. This can be explicitly set in the incoming Intent or internally assigned.
    private int mWindowId;

    // The URL of the last active Tab read from the Tab metadata file during cold startup.
    private String mLastActiveTabUrl;

    private DseNewTabUrlManager mDseNewTabUrlManager;

    // Time at which an intent was received and handled.
    private long mIntentHandlingTimeMs;

    // Delegate to handle drag and drop features for tablets.
    private DragAndDropDelegate mDragDropDelegate;

    private ExperimentalStartupMetricsTracker mExperimentalStartupMetricsTracker;

    private final IncognitoTabHost mIncognitoTabHost =
            new IncognitoTabHost() {
                @Override
                public boolean hasIncognitoTabs() {
                    return getTabModelSelector().getModel(true).getCount() > 0;
                }

                @Override
                public void closeAllIncognitoTabs() {
                    if (isActivityFinishingOrDestroyed()) return;

                    // If the tabbed activity has not yet initialized, then finish the activity to
                    // avoid timing issues with clearing the incognito tab state in the
                    // background.
                    if (!areTabModelsInitialized() || !didFinishNativeInitialization()) {
                        finish();
                        return;
                    }

                    getTabModelSelector().getModel(true).closeAllTabs();
                }

                @Override
                public boolean isActiveModel() {
                    return getTabModelSelector().getModel(true).isActiveModel();
                }
            };

    private final OnClickListener mNewTabButtonClickListener =
            view -> {
                getTabModelSelector().getModel(false).commitAllTabClosures();
                // This assumes that the keyboard can not be seen at the same time as the
                // new tab button on the toolbar.
                int tabLaunchType =
                        (getLayoutManager().getActiveLayoutType() == LayoutType.TAB_SWITCHER)
                                ? TabLaunchType.FROM_TAB_SWITCHER_UI
                                : TabLaunchType.FROM_CHROME_UI;
                getCurrentTabCreator().launchNtp(tabLaunchType);
                mLocaleManager.showSearchEnginePromoIfNeeded(ChromeTabbedActivity.this, null);
                if (getTabModelSelector().isIncognitoSelected()) {
                    RecordUserAction.record("MobileToolbarStackViewNewIncognitoTab");
                } else {
                    RecordUserAction.record("MobileToolbarStackViewNewTab");
                }
                RecordUserAction.record("MobileTopToolbarNewTabButton");

                RecordUserAction.record("MobileNewTabOpened");
            };

    /**
     * This class is used to warm up the chrome split ClassLoader. See SplitChromeApplication for
     * more info
     */
    @UsedByReflection("SplitChromeApplication.java")
    public static class Preload extends ChromeTabbedActivity {
        private LifecycleRegistry mLifecycleRegistry;

        @UsedByReflection("SplitChromeApplication.java")
        public Preload() {}

        @Override
        public Lifecycle getLifecycle() {
            if (mLifecycleRegistry == null) {
                // LifecycleRegistry normally enforces it is called on the main thread, but this
                // class will be preloaded in a background thread. The only method that gets called
                // in the activity constructor is addObserver(), so just override that.
                mLifecycleRegistry =
                        new LifecycleRegistry(this) {
                            @Override
                            public void addObserver(LifecycleObserver observer) {}
                        };
            }
            return mLifecycleRegistry;
        }
    }

    /** Constructs a ChromeTabbedActivity. */
    public ChromeTabbedActivity() {
        mIntentHandlingTimeMs = SystemClock.uptimeMillis();
        mMainIntentMetrics = new MainIntentBehaviorMetrics();

        // AppLaunchDrawBlocker may block drawing the Activity content until the initial tab is
        // available.
        mAppLaunchDrawBlocker =
                new AppLaunchDrawBlocker(
                        getLifecycleDispatcher(),
                        () -> findViewById(android.R.id.content),
                        this::getIntent,
                        this::shouldIgnoreIntent,
                        this::isTablet,
                        this::shouldShowOverviewPageOnStart,
                        this::isInstantStartEnabled,
                        mTabModelProfileSupplier,
                        new IncognitoRestoreAppLaunchDrawBlockerFactory(
                                this::getSavedInstanceState, getTabModelSelectorSupplier()));
    }

    @Override
    protected void onPreCreate() {
        super.onPreCreate();
        mExperimentalStartupMetricsTracker =
                new ExperimentalStartupMetricsTracker(getTabModelSelectorSupplier());
        mMultiInstanceManager =
                MultiInstanceManager.create(
                        this,
                        getTabModelOrchestratorSupplier(),
                        getMultiWindowModeStateDispatcher(),
                        getLifecycleDispatcher(),
                        getModalDialogManagerSupplier(),
                        this);
        StartSurfaceUserData.reset();
        mBackPressManager.setFallbackOnBackPressed(
                () -> {
                    if (BackPressManager.correctTabNavigationOnFallback()) {
                        if (getToolbarManager() != null && getToolbarManager().back()) {
                            return;
                        }
                    }
                    minimizeAppAndCloseTabOnBackPress(getActivityTab());
                });
    }

    @Override
    protected @LaunchIntentDispatcher.Action int maybeDispatchLaunchIntent(
            Intent intent, Bundle savedInstanceState) {
        // Detect if incoming intent is a result of Chrome recreating itself. For now, restrict this
        // path to reparenting to ensure the launching logic isn't disrupted.
        // TODO(crbug.com/1065491): Unlock this codepath for all incoming intents once it's
        // confirmed working and stable.
        if (savedInstanceState != null
                && AsyncTabParamsManagerSingleton.getInstance().hasParamsWithTabToReparent()) {
            return LaunchIntentDispatcher.Action.CONTINUE;
        }

        if (getClass().equals(ChromeTabbedActivity.class)
                && Intent.ACTION_MAIN.equals(intent.getAction())) {
            // Call dispatchToTabbedActivity() for MAIN intents to activate proper multi-window
            // TabbedActivity (i.e. if CTA2 is currently running and Chrome is started, CTA2
            // should be brought to front). Don't call dispatchToTabbedActivity() for non-MAIN
            // intents to avoid breaking cases where CTA is started explicitly (e.g. to handle
            // 'Move to other window' command from CTA2).
            return LaunchIntentDispatcher.dispatchToTabbedActivity(this, intent);
        }
        @LaunchIntentDispatcher.Action
        int action = maybeDispatchExplicitMainViewIntent(intent, DispatchedBy.ON_CREATE);
        if (action != LaunchIntentDispatcher.Action.CONTINUE) {
            Log.i(TAG_MULTI_INSTANCE, "Dispatched explicit .Main (CTA) VIEW intent to CCT.");
            return action;
        }
        return super.maybeDispatchLaunchIntent(intent, savedInstanceState);
    }

    // We know of at least one app that explicitly specifies .Main activity in custom tab
    // intents. The app shouldn't be doing that, but until it's updated, we need to support
    // such use case.
    //
    // This method attempts to treat VIEW intents explicitly sent to .Main as custom tab
    // intents, and dispatch them accordingly. If the intent was not dispatched, the method
    // returns Action.CONTINUE.
    //
    // The method also updates the supplied boolean histogram with the dispatching result,
    // but only if the intent is a VIEW intent sent explicitly to .Main activity.
    private @LaunchIntentDispatcher.Action int maybeDispatchExplicitMainViewIntent(
            Intent intent, @DispatchedBy int dispatchedBy) {
        // The first check ensures that this is .Main activity alias (we can't check exactly, but
        // this gets us sufficiently close).
        if (getClass().equals(ChromeTabbedActivity.class)
                && Intent.ACTION_VIEW.equals(intent.getAction())
                && intent.getComponent() != null
                && MAIN_LAUNCHER_ACTIVITY_NAME.equals(intent.getComponent().getClassName())) {
            @LaunchIntentDispatcher.Action
            int action = LaunchIntentDispatcher.dispatchToCustomTabActivity(this, intent);
            switch (dispatchedBy) {
                case DispatchedBy.ON_CREATE:
                case DispatchedBy.ON_NEW_INTENT:
                    break;
                default:
                    assert false : "Unknown dispatchedBy value " + dispatchedBy;
            }
            if (action == LaunchIntentDispatcher.Action.CONTINUE) {
                // Intent was not dispatched, record its source.
                @IntentHandler.ExternalAppId
                int externalId = IntentHandler.determineExternalIntentSource(intent);

                // Crash if intent came from us, but only in debug builds and only if we weren't
                // explicitly told not to. Hopefully we'll get enough reports to find where
                // these intents come from.
                if (externalId == IntentHandler.ExternalAppId.CHROME
                        && BuildInfo.isDebugApp()
                        && !CommandLine.getInstance()
                                .hasSwitch(ChromeSwitches.DONT_CRASH_ON_VIEW_MAIN_INTENTS)) {
                    String intentInfo = intent.toString();
                    Bundle extras = intent.getExtras();
                    if (extras != null) {
                        intentInfo +=
                                ", extras.keySet = [" + TextUtils.join(", ", extras.keySet()) + "]";
                    }
                    String message =
                            String.format(
                                    """
                                    VIEW intent sent to .Main activity alias was not dispatched. \
                                    PLEASE report the following info to crbug.com/789732: \
                                    "%s". Use --%s flag to disable this check.""",
                                    intentInfo, ChromeSwitches.DONT_CRASH_ON_VIEW_MAIN_INTENTS);
                    throw new IllegalStateException(message);
                }
            }
            return action;
        }
        return LaunchIntentDispatcher.Action.CONTINUE;
    }

    @Override
    public void initializeCompositor() {
        try {
            TraceEvent.begin("ChromeTabbedActivity.initializeCompositor");
            super.initializeCompositor();

            // LocaleManager can only function after the native library is loaded.
            mLocaleManager = LocaleManager.getInstance();
            mLocaleManager.setSettingsLauncher(SETTINGS_LAUNCHER);
            mLocaleManager.showSearchEnginePromoIfNeeded(this, null);

            mTabModelOrchestrator.onNativeLibraryReady(getTabContentManager());

            // For saving non-incognito tab closures for Recent Tabs.
            mHistoricalTabModelObserver =
                    new HistoricalTabModelObserver(mTabModelSelector.getModel(false));

            // Defer initialization of this helper so it triggers after TabModelFilter
            // observers.
            UndoRefocusHelper.initialize(
                    this, mTabModelSelector, getLayoutManagerSupplier(), isTablet());

            mTabModelObserver =
                    new TabModelSelectorTabModelObserver(mTabModelSelector) {
                        @Override
                        public void onFinishingTabClosure(Tab tab) {
                            closeIfNoTabsAndHomepageEnabled(false);
                        }

                        @Override
                        public void tabPendingClosure(Tab tab) {
                            closeIfNoTabsAndHomepageEnabled(true);
                        }

                        @Override
                        public void multipleTabsPendingClosure(List<Tab> tabs, boolean isAllTabs) {
                            closeIfNoTabsAndHomepageEnabled(true);
                        }

                        @Override
                        public void tabRemoved(Tab tab) {
                            closeIfNoTabsAndHomepageEnabled(false);
                        }

                        private void closeIfNoTabsAndHomepageEnabled(boolean isPendingClosure) {
                            if (getTabModelSelector().getTotalTabCount() == 0) {
                                // If the last tab is closed, and homepage is enabled, then exit
                                // Chrome.
                                if (HomepageManager.shouldCloseAppWithZeroTabs()) {
                                    finish();
                                } else if (isPendingClosure) {
                                    NewTabPageUma.recordNtpImpression(
                                            NewTabPageUma.NTP_IMPRESSION_POTENTIAL_NO_TAB);
                                }
                            }
                        }

                        @Override
                        public void didAddTab(
                                Tab tab,
                                @TabLaunchType int type,
                                @TabCreationState int creationState,
                                boolean markedForSelection) {
                            if (type == TabLaunchType.FROM_LONGPRESS_BACKGROUND
                                    || type == TabLaunchType.FROM_LONGPRESS_BACKGROUND_IN_GROUP
                                    || type == TabLaunchType.FROM_RECENT_TABS
                                            && !DeviceClassManager.enableAnimations()) {
                                Toast.makeText(
                                                ChromeTabbedActivity.this,
                                                R.string.open_in_new_tab_toast,
                                                Toast.LENGTH_SHORT)
                                        .show();
                            }
                        }
                    };
        } finally {
            TraceEvent.end("ChromeTabbedActivity.initializeCompositor");
        }
    }

    private void refreshSignIn() {
        try (TraceEvent e = TraceEvent.scoped("ChromeTabbedActivity.refreshSignIn")) {
            FirstRunSignInProcessor.openSyncSettingsIfScheduled(this);
            BackupSigninProcessor.start(this);
        }
    }

    private HubLayoutDependencyHolder createHubLayoutDependencyHolder() {
        if (!HubFieldTrial.isHubEnabled()) return null;

        // The tab_switcher_view_holder can be used on both tablet and phone because Hub's
        // animations don't depend on the compositor. This differs from the current tab switcher
        // behavior on phones.
        LazyOneshotSupplier<ViewGroup> rootViewSupplier =
                LazyOneshotSupplier.fromSupplier(
                        () -> {
                            ViewStub stub =
                                    (ViewStub) findViewById(R.id.tab_switcher_view_holder_stub);
                            return (ViewGroup) stub.inflate();
                        });

        return new HubLayoutDependencyHolder(
                mHubProvider.getHubManagerSupplier(),
                rootViewSupplier,
                mRootUiCoordinator.getScrimCoordinator(),
                rootViewSupplier::get,
                mTabModelSelector::isIncognitoSelected);
    }

    private void setupCompositorContentPreNativeForPhone() {
        if (isTablet()) return;

        try (TraceEvent e =
                TraceEvent.scoped("ChromeTabbedActivity.setupCompositorContentPreNativeForPhone")) {
            CompositorViewHolder compositorViewHolder = getCompositorViewHolderSupplier().get();

            // TODO(crbug/1484592): Post Start Surface Refactor this now only creates Start Surface.
            // We should inline createStartSurface() (if enabled) once the refactor is launched.
            createTabSwitcherOrStartSurface(compositorViewHolder, compositorViewHolder);

            mLayoutManager =
                    new LayoutManagerChromePhone(
                            compositorViewHolder,
                            mContentContainer,
                            mStartSurfaceSupplier,
                            mTabSwitcherSupplier,
                            getBrowserControlsManager(),
                            getTabContentManagerSupplier(),
                            mRootUiCoordinator::getTopUiThemeColorProvider,
                            () -> {
                                createGridTabSwitcher(compositorViewHolder, compositorViewHolder);
                                return compositorViewHolder;
                            },
                            createHubLayoutDependencyHolder());
            mLayoutStateProviderSupplier.set(mLayoutManager);
        }
    }

    private void setupCompositorContentPreNativeForTablet() {
        if (!isTablet()) return;

        try (TraceEvent e =
                TraceEvent.scoped(
                        "ChromeTabbedActivity.setupCompositorContentPreNativeForTablet")) {
            CompositorViewHolder compositorViewHolder = getCompositorViewHolderSupplier().get();

            ViewGroup tabSwitcherViewHolder = findViewById(R.id.tab_switcher_view_holder);
            ViewStub tabHoverCardViewStub =
                    (ViewStub) findViewById(R.id.tab_hover_card_holder_stub);
            View toolbarContainerView = findViewById(R.id.toolbar_container);
            mDragDropDelegate = new DragAndDropDelegateImpl();
            mDragDropDelegate.setDragAndDropBrowserDelegate(
                    new ChromeDragAndDropBrowserDelegate(this));

            assert getToolbarManager() != null;
            mLayoutManager =
                    new LayoutManagerChromeTablet(
                            compositorViewHolder,
                            mContentContainer,
                            mStartSurfaceSupplier,
                            mTabSwitcherSupplier,
                            getBrowserControlsManager(),
                            getTabContentManagerSupplier(),
                            mRootUiCoordinator::getTopUiThemeColorProvider,
                            mTabModelStartupInfoSupplier,
                            tabSwitcherViewHolder,
                            mRootUiCoordinator.getScrimCoordinator(),
                            getLifecycleDispatcher(),
                            () -> createAndSetStartSurfaceForTablet(),
                            createHubLayoutDependencyHolder(),
                            mMultiInstanceManager,
                            mDragDropDelegate,
                            toolbarContainerView,
                            tabHoverCardViewStub,
                            getWindowAndroid(),
                            getToolbarManager());
            mLayoutStateProviderSupplier.set(mLayoutManager);
        }
    }

    private void createTabSwitcherOrStartSurface(
            CompositorViewHolder compositorViewHolder, ViewGroup tabSwitcherContainer) {
        // If the refactor is enabled, we create grid tab switcher directly instead of via start
        // surface.
        if (isStartSurfaceRefactorEnabled()) {
            // If Hub is enabled we don't create a GTS here because the TabSwitcherPane is created
            // when loading the Hub.
            if (!HubFieldTrial.isHubEnabled()) {
                // Tablets currently launch the GTS through this method, but tablets already have
                // deferred tab switcher creation semantics in LayoutManagerChrome so don't need to
                // check sDeferTabSwitcherLayoutCreation.
                if (!ChromeFeatureList.sDeferTabSwitcherLayoutCreation.isEnabled() || isTablet()) {
                    createGridTabSwitcher(compositorViewHolder, tabSwitcherContainer);
                }
            }
            if (ReturnToChromeUtil.isStartSurfaceEnabled(this)) {
                createStartSurface(compositorViewHolder, tabSwitcherContainer);
            }
        } else {
            createStartSurface(compositorViewHolder, tabSwitcherContainer);
        }
    }

    private void createStartSurface(
            CompositorViewHolder compositorViewHolder, ViewGroup tabSwitcherContainer) {
        StartSurfaceDelegate.createStartSurface(
                this,
                mRootUiCoordinator.getScrimCoordinator(),
                mRootUiCoordinator.getBottomSheetController(),
                mStartSurfaceSupplier,
                mStartSurfaceParentTabSupplier,
                hadWarmStart(),
                getWindowAndroid(),
                mJankTracker,
                tabSwitcherContainer,
                compositorViewHolder::getDynamicResourceLoader,
                getTabModelSelector(),
                getBrowserControlsManager(),
                getSnackbarManager(),
                getShareDelegateSupplier(),
                getToolbarManager()::getOmniboxStub,
                getTabContentManager(),
                getModalDialogManager(),
                /* chromeActivityNativeDelegate= */ this,
                getLifecycleDispatcher(),
                getTabCreatorManagerSupplier().get(),
                getMenuOrKeyboardActionController(),
                getMultiWindowModeStateDispatcher(),
                getToolbarManager()::getToolbar,
                mBackPressManager,
                mRootUiCoordinator.getIncognitoReauthControllerSupplier(),
                v -> onTabSwitcherClicked(),
                mTabModelProfileSupplier,
                getToolbarManager().getTabStripHeightSupplier());
    }

    private void initHub() {
        if (!HubFieldTrial.isHubEnabled()) return;

        mHubProvider =
                new HubProvider(
                        this,
                        new DefaultPaneOrderController(),
                        mBackPressManager,
                        getMenuOrKeyboardActionController(),
                        this::getSnackbarManager,
                        getTabModelSelectorSupplier(),
                        () -> getToolbarManager().getOverviewModeMenuButtonCoordinator());
        var builder = mHubProvider.getPaneListBuilder();
        builder.registerPane(
                PaneId.TAB_SWITCHER,
                LazyOneshotSupplier.fromSupplier(
                        () -> {
                            return createTabSwitcherPane(false);
                        }));
        builder.registerPane(
                PaneId.INCOGNITO_TAB_SWITCHER,
                LazyOneshotSupplier.fromSupplier(
                        () -> {
                            return createTabSwitcherPane(true);
                        }));
        mHubProvider
                .getHubManagerSupplier()
                .onAvailable(
                        manager -> {
                            mHubManagerSupplier.set(manager);
                        });
    }

    private Pane createTabSwitcherPane(boolean isIncognito) {
        Pair<TabSwitcher, Pane> result =
                TabManagementDelegateProvider.getDelegate()
                        .createTabSwitcherPane(
                                this,
                                getLifecycleDispatcher(),
                                getProfileProviderSupplier(),
                                getTabModelSelector(),
                                getTabContentManager(),
                                getTabCreatorManagerSupplier().get(),
                                getBrowserControlsManager(),
                                getMultiWindowModeStateDispatcher(),
                                mRootUiCoordinator.getScrimCoordinator(),
                                getSnackbarManager(),
                                getModalDialogManager(),
                                mRootUiCoordinator.getIncognitoReauthControllerSupplier(),
                                mNewTabButtonClickListener,
                                isIncognito);
        if (didFinishNativeInitialization()) {
            result.first.initWithNative();
        }
        if (isIncognito) {
            mIncognitoTabSwitcherSupplier.set(result.first);
        } else {
            mTabSwitcherSupplier.set(result.first);
        }
        return result.second;
    }

    private void createGridTabSwitcher(
            CompositorViewHolder compositorViewHolder, ViewGroup tabSwitcherContainer) {
        var tabSwitcher =
                TabManagementDelegateProvider.getDelegate()
                        .createGridTabSwitcher(
                                this,
                                getLifecycleDispatcher(),
                                getTabModelSelector(),
                                getTabContentManager(),
                                getBrowserControlsManager(),
                                getTabCreatorManagerSupplier().get(),
                                getMenuOrKeyboardActionController(),
                                tabSwitcherContainer,
                                getMultiWindowModeStateDispatcher(),
                                mRootUiCoordinator.getScrimCoordinator(),
                                /* rootView= */ tabSwitcherContainer,
                                compositorViewHolder::getDynamicResourceLoader,
                                getSnackbarManager(),
                                getModalDialogManager(),
                                mRootUiCoordinator.getIncognitoReauthControllerSupplier(),
                                mBackPressManager,
                                mLayoutStateProviderSupplier);
        mTabSwitcherSupplier.set(tabSwitcher);
        mIncognitoTabSwitcherSupplier.set(tabSwitcher);
    }

    private void setupCompositorContentPreNative() {
        try (TraceEvent e =
                TraceEvent.scoped("ChromeTabbedActivity.setupCompositorContentPostNative")) {
            if (!isLayoutManagerCreated()) {
                if (isTablet()) {
                    setupCompositorContentPreNativeForTablet();
                } else {
                    setupCompositorContentPreNativeForPhone();
                }
            }

            mLayoutManager.setEnableAnimations(DeviceClassManager.enableAnimations());
        }
    }

    private void initializeCompositorContent() {
        try (TraceEvent e = TraceEvent.scoped("ChromeTabbedActivity.initializeCompositorContent")) {
            // TODO(yusufo): get rid of findViewById(R.id.url_bar).
            initializeCompositorContent(
                    mLayoutManager,
                    findViewById(R.id.url_bar),
                    mContentContainer,
                    mControlContainer);
        }
    }

    private boolean isLayoutManagerCreated() {
        return mLayoutManager != null;
    }

    private void onTabSwitcherClicked() {
        Profile profile = mTabModelProfileSupplier.get();
        if (profile != null) {
            TrackerFactory.getTrackerForProfile(profile)
                    .notifyEvent(EventConstants.TAB_SWITCHER_BUTTON_CLICKED);
        }

        if (getFullscreenManager().getPersistentFullscreenMode()) {
            return;
        }

        ReturnToChromeUtil.recordClickTabSwitcher(
                isInOverviewMode(), getTabModelSelector().getCurrentTab());

        showOverview(StartSurfaceState.SHOWING_TABSWITCHER);
    }

    private void initializeToolbarManager() {
        try (TraceEvent e = TraceEvent.scoped("ChromeTabbedActivity.initializeToolbarManager")) {
            mUndoBarPopupController.initialize();

            OnClickListener bookmarkClickHandler =
                    v -> mTabBookmarkerSupplier.get().addOrEditBookmark(getActivityTab());

            Supplier<Boolean> showStartSurfaceSupplier =
                    () -> {
                        // If incognito is selected, tapping the home button stays on the current
                        // incognito tab.
                        if (ReturnToChromeUtil.shouldShowStartSurfaceAsTheHomePageOnPhone(
                                        this, isTablet())
                                && !mTabModelSelector.isIncognitoSelected()) {
                            StartSurfaceUserData.setKeepTab(getActivityTab(), true);
                            showOverview(StartSurfaceState.SHOWING_HOMEPAGE);
                            return true;
                        }
                        return false;
                    };

            getToolbarManager()
                    .initializeWithNative(
                            mLayoutManager,
                            mLayoutManager.getStripLayoutHelperManager(),
                            v -> onTabSwitcherClicked(),
                            mNewTabButtonClickListener,
                            bookmarkClickHandler,
                            null,
                            showStartSurfaceSupplier);

            if (!isInstantStartEnabled()) {
                // TODO(https://crbug.com/1306904): Fix this assert which is tripping on unrelated
                // tests.
                // assert !(mOverviewModeController != null
                //         && mOverviewModeController.overviewVisible());
            }
        }
    }

    private ViewGroup createAndSetStartSurfaceForTablet() {
        assert isTablet();
        final long startTimeMs = SystemClock.uptimeMillis();
        CompositorViewHolder compositorViewHolder = getCompositorViewHolderSupplier().get();
        ViewGroup containerView = compositorViewHolder;
        // Inflate view holder for polish GTS.
        containerView =
                (ViewGroup) ((ViewStub) findViewById(R.id.tab_switcher_view_holder_stub)).inflate();
        // Set view in toolbar manager to set toolbar stub.
        getToolbarManager().setTabSwitcherFullScreenView(containerView);

        // create start surface.
        createTabSwitcherOrStartSurface(compositorViewHolder, containerView);
        RecordHistogram.recordTimesHistogram(
                TAB_SWITCHER_CREATION_TIME, SystemClock.uptimeMillis() - startTimeMs);
        return containerView;
    }

    private void maybeCreateIncognitoTabSnapshotController() {
        try (TraceEvent e =
                TraceEvent.scoped(
                        "ChromeTabbedActivity.maybeCreateIncognitoTabSnapshotController")) {
            if (!CommandLine.getInstance()
                    .hasSwitch(ChromeSwitches.ENABLE_INCOGNITO_SNAPSHOTS_IN_ANDROID_RECENTS)) {
                IncognitoTabbedSnapshotController.createIncognitoTabSnapshotController(
                        getWindow(), mLayoutManager, mTabModelSelector, getLifecycleDispatcher());
            }

            mUIWithNativeInitialized = true;

            // The dataset has already been created, we need to initialize our state.
            mTabModelSelector.notifyChanged();

            // Check for incognito tabs to handle the case where Chrome was swiped away in the
            // background.
            if (!IncognitoTabHostUtils.doIncognitoTabsExist()) {
                IncognitoNotificationManager.dismissIncognitoNotification();
                DownloadNotificationService.getInstance().cancelOffTheRecordDownloads();
            }
        }
    }

    private void maybeGetFeedAppLifecycleAndMaybeCreatePageViewObserver() {
        try (TraceEvent e =
                TraceEvent.scoped(
                        "ChromeTabbedActivity."
                                + "maybeGetFeedAppLifecycleAndMaybeCreatePageViewObserver")) {
            FeedSurfaceTracker.getInstance().startup();

            UsageStatsService.createPageViewObserverIfEnabled(
                    this, getActivityTabProvider(), getTabContentManagerSupplier());
        }
    }

    private void initJourneyManager() {
        assert mLayoutManager != null;

        try (TraceEvent e = TraceEvent.scoped("ChromeTabbedActivity.initJourneyManager")) {
            if (ChromeFeatureList.isEnabled(ChromeFeatureList.TAB_ENGAGEMENT_REPORTING_ANDROID)) {
                // The lifecycle of this object is managed by the lifecycle dispatcher.
                new JourneyManager(
                        getTabModelSelector(),
                        getLifecycleDispatcher(),
                        mLayoutManager,
                        new EngagementTimeUtil());
            }
        }
    }

    @Override
    protected OneshotSupplier<ProfileProvider> createProfileProvider() {
        return new ActivityProfileProvider(getLifecycleDispatcher());
    }

    @Override
    public void startNativeInitialization() {
        try (TraceEvent e = TraceEvent.scoped("ChromeTabbedActivity.startNativeInitialization")) {
            // This is on the critical path so don't delay.
            setupCompositorContentPreNative();
            if (!DeviceFormFactor.isTablet()) {
                PostTask.postTask(
                        TaskTraits.UI_DEFAULT,
                        mCallbackController.makeCancelable(this::initializeCompositorContent));
            } else {
                // TODO(crbug.com/1344639): Enable split compositor task on tablets.
                initializeCompositorContent();
            }

            // All this initialization can be expensive so it's split into multiple tasks.
            PostTask.postTask(
                    TaskTraits.UI_DEFAULT, mCallbackController.makeCancelable(this::refreshSignIn));
            PostTask.postTask(
                    TaskTraits.UI_DEFAULT,
                    mCallbackController.makeCancelable(this::initializeToolbarManager));
            PostTask.postTask(
                    TaskTraits.UI_DEFAULT,
                    mCallbackController.makeCancelable(
                            this::maybeCreateIncognitoTabSnapshotController));
            if (BackPressManager.isEnabled()) {
                PostTask.postTask(TaskTraits.UI_DEFAULT, this::initializeBackPressHandlers);
            }

            PostTask.postTask(
                    TaskTraits.UI_DEFAULT,
                    mCallbackController.makeCancelable(
                            this::maybeGetFeedAppLifecycleAndMaybeCreatePageViewObserver));
            PostTask.postTask(
                    TaskTraits.UI_DEFAULT,
                    mCallbackController.makeCancelable(this::initJourneyManager));
            PostTask.postTask(
                    TaskTraits.UI_DEFAULT,
                    mCallbackController.makeCancelable(this::finishNativeInitialization));
        }
    }

    @Override
    public void finishNativeInitialization() {
        try (TraceEvent te = TraceEvent.scoped("ChromeTabbedActivity.finishNativeInitialization")) {
            super.finishNativeInitialization();

            // TODO(jinsukkim): Let these classes handle the registration by themselves.
            mCompositorViewHolder = getCompositorViewHolderSupplier().get();
            getTabObscuringHandler().addObserver(mCompositorViewHolder);

            ChromeAccessibilityUtil.get().addObserver(mLayoutManager);
            if (isTablet()) {
                ChromeAccessibilityUtil.get().addObserver(mCompositorViewHolder);
            }

            if (mIsHandleTabSwitcherShownEnabled == null) {
                mIsHandleTabSwitcherShownEnabled =
                        !isTablet()
                                && isStartSurfaceRefactorEnabled()
                                && ReturnToChromeUtil.isStartSurfaceEnabled(this);
            }
            if (HubFieldTrial.isHubEnabled()) {
                TabSwitcher switcher = mTabSwitcherSupplier.get();
                if (switcher != null) {
                    switcher.initWithNative();
                }
                TabSwitcher incognitoSwitcher = mIncognitoTabSwitcherSupplier.get();
                if (incognitoSwitcher != null) {
                    incognitoSwitcher.initWithNative();
                }
            }

            mInactivityTracker.setLastVisibleTimeMsAndRecord(System.currentTimeMillis());

            getSnackbarManager().setEdgeToEdgeSupplier(getEdgeToEdgeSupplier().get());
        }
    }

    @Override
    public void onResumeWithNative() {
        // On warm startup, call setInitialOverviewState in onResume() instead of onStart(). This is
        // because onResume() is guaranteed to called after onNewIntent() and thus have the updated
        // Intent which is used by shouldShowOverviewPageOnStart(). See https://crbug.com/1321607.
        if (mFromResumption) {
            setInitialOverviewState(shouldShowOverviewPageOnStart());
        } else {
            // Set mFromResumption to be true to skip the call of setInitialOverviewState() in
            // onStart() when the next time onStart() is called, since it is no longer a cold start.
            mFromResumption = true;
        }

        super.onResumeWithNative();

        IncognitoStartup.onResumeWithNative(
                getTabModelSelectorSupplier(), TABBED_MODE_COMPONENT_NAMES);

        mLocaleManager.setSnackbarManager(getSnackbarManager());
        mLocaleManager.startObservingPhoneChanges();

        if (isWarmOnResume()) {
            NavigationPredictorBridge.onActivityWarmResumed();
        } else {
            NavigationPredictorBridge.onColdStart();
        }

        // This call is not guarded by a feature flag.
        SearchEngineChoiceNotification.handleSearchEngineChoice(
                this, getSnackbarManager(), SETTINGS_LAUNCHER);

        if (!isWarmOnResume()) {
            SuggestionsMetrics.recordArticlesListVisible();
        } else {
            mInactivityTracker.setLastVisibleTimeMsAndRecord(System.currentTimeMillis());
        }

        FeatureNotificationUtils.handleIntentIfApplicable(getIntent());
    }

    @Override
    public void onPauseWithNative() {
        mTabModelSelector.commitAllTabClosures();
        CookiesFetcher.persistCookies();

        mLocaleManager.setSnackbarManager(null);
        mLocaleManager.stopObservingPhoneChanges();

        NavigationPredictorBridge.onPause();
        // Always track the last backgrounded time in case others are using the pref.
        mInactivityTracker.setLastBackgroundedTimeInPrefs(System.currentTimeMillis());

        super.onPauseWithNative();
    }

    @Override
    public void onStopWithNative() {
        super.onStopWithNative();

        saveState();
        mHasDeterminedOverviewStateForCurrentSession = false;
    }

    @Override
    public void onStartWithNative() {
        mMainIntentMetrics.logLaunchBehavior();
        super.onStartWithNative();

        // Don't call setInitialOverviewState if 1) we're waiting for the tab's creation or we risk
        // showing a glimpse of the tab selector during start up. 2) on warm startup from an
        // resumption. Defer it to onResumeWitheNative() since it needs to check the latest Intent
        // which is only guaranteed to be updated onResume() if onNewIntent() is called.
        if (!mPendingInitialTabCreation && !mFromResumption) {
            setInitialOverviewState(shouldShowOverviewPageOnStart());
        }

        Bundle savedInstanceState = getSavedInstanceState();
        if (savedInstanceState != null
                && savedInstanceState.getBoolean(IS_INCOGNITO_SELECTED, false)) {
            // This will be executed only once since SavedInstanceState will be reset a few lines
            // later.
            AndroidSessionDurationsServiceState.restoreNativeFromSerialized(
                    savedInstanceState,
                    getCurrentTabModel()
                            .getProfile()
                            .getPrimaryOTRProfile(/* createIfNeeded= */ true));
        }

        resetSavedInstanceState();
        ReturnToChromeUtil.addFeedVisibilityObserver();
        BookmarkUtils.maybeExpireLastBookmarkLocationForReadLater(
                mInactivityTracker.getTimeSinceLastBackgroundedMs());
    }

    @Override
    public void onNewIntent(Intent intent) {
        try (TraceEvent e = TraceEvent.scoped("ChromeTabbedActivity.onNewIntent")) {
            mIntentHandlingTimeMs = SystemClock.uptimeMillis();

            // Drop the cleaner intent since it's created in order to clear up the OS share sheet.
            if (ShareHelper.isCleanerIntent(intent)) {
                return;
            }

            // The intent to use in maybeDispatchExplicitMainViewIntent(). We're explicitly
            // adding NEW_TASK flag to make sure backing from CCT brings up the caller activity,
            // and not Chrome
            Intent intentForDispatching = new Intent(intent);
            intentForDispatching.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
            @LaunchIntentDispatcher.Action
            int action =
                    maybeDispatchExplicitMainViewIntent(
                            intentForDispatching, DispatchedBy.ON_NEW_INTENT);
            if (action != LaunchIntentDispatcher.Action.CONTINUE) {
                // Pressing back button in CCT should bring user to the caller activity.
                moveTaskToBack(true);
                // Intent was dispatched to CustomTabActivity, consume it.
                return;
            }

            super.onNewIntent(intent);

            boolean shouldShowRegularOverviewMode =
                    IntentUtils.safeGetBooleanExtra(
                            intent, IntentHandler.EXTRA_OPEN_REGULAR_OVERVIEW_MODE, false);
            if (shouldShowRegularOverviewMode && IntentHandler.wasIntentSenderChrome(intent)) {
                mTabModelSelector.selectModel(/* incognito= */ false);
                mLayoutManager.showLayout(LayoutType.TAB_SWITCHER, /* animate= */ false);
            }
        }
    }

    @Override
    public void onNewIntentWithNative(Intent intent) {
        try {
            TraceEvent.begin("ChromeTabbedActivity.onNewIntentWithNative");

            super.onNewIntentWithNative(intent);
            if (!IntentHandler.shouldIgnoreIntent(intent, /* isCustomTab= */ false)) {
                maybeHandleUrlIntent(intent);
            }

            if (IntentUtils.isMainIntentFromLauncher(intent)) {
                logMainIntentBehavior(intent);
            }

            if (CommandLine.getInstance().hasSwitch(ContentSwitches.ENABLE_TEST_INTENTS)) {
                handleDebugIntent(intent);
            }

        } finally {
            TraceEvent.end("ChromeTabbedActivity.onNewIntentWithNative");
        }
    }

    /**
     * @param intent The received intent.
     * @return Whether the Intent was successfully handled.
     */
    private boolean maybeHandleUrlIntent(Intent intent) {
        String url = IntentHandler.getUrlFromIntent(intent);
        @TabOpenType int tabOpenType = IntentHandler.getTabOpenType(intent);
        int tabIdToBringToFront = IntentHandler.getBringTabToFrontId(intent);
        if (url == null && tabIdToBringToFront == Tab.INVALID_TAB_ID) return false;

        LoadUrlParams loadUrlParams =
                IntentHandler.createLoadUrlParamsForIntent(url, intent, mIntentHandlingTimeMs);

        if (IntentHandler.isIntentForMhtmlFileOrContent(intent)
                && tabOpenType == TabOpenType.OPEN_NEW_TAB
                && loadUrlParams.getReferrer() == null
                && loadUrlParams.getVerbatimHeaders() == null) {
            handleMhtmlFileOrContentIntent(url, intent);
            return true;
        }
        processUrlViewIntent(
                loadUrlParams,
                tabOpenType,
                IntentUtils.safeGetStringExtra(intent, Browser.EXTRA_APPLICATION_ID),
                tabIdToBringToFront,
                intent);
        return true;
    }

    private void handleMhtmlFileOrContentIntent(final String url, final Intent intent) {
        OfflinePageUtils.getLoadUrlParamsForOpeningMhtmlFileOrContent(
                url,
                (loadUrlParams) -> {
                    loadUrlParams.setVerbatimHeaders(
                            IntentHandler.maybeAddAdditionalContentHeaders(
                                    intent, url, loadUrlParams.getVerbatimHeaders()));
                    processUrlViewIntent(
                            loadUrlParams,
                            TabOpenType.OPEN_NEW_TAB,
                            null,
                            Tab.INVALID_TAB_ID,
                            intent);
                },
                Profile.getLastUsedRegularProfile());
    }

    @Override
    public @ActivityType int getActivityType() {
        return ActivityType.TABBED;
    }

    @Override
    public ChromeTabCreator getTabCreator(boolean incognito) {
        return (ChromeTabCreator) super.getTabCreator(incognito);
    }

    @Override
    public ChromeTabCreator getCurrentTabCreator() {
        return (ChromeTabCreator) super.getCurrentTabCreator();
    }

    private void handleDebugIntent(Intent intent) {
        if (ACTION_CLOSE_TABS.equals(intent.getAction())) {
            getTabModelSelector().closeAllTabs();
        } else if (MemoryPressureListener.handleDebugIntent(
                ChromeTabbedActivity.this, intent.getAction())) {
            // Handled.
        }
    }

    private void disablePaintPreviewOnRestore() {
        StartupPaintPreviewHelper.setShouldShowOnRestore(false);
    }

    private void setTrackColdStartupMetrics(boolean shouldTrackColdStartupMetrics) {
        assert getActivityTabStartupMetricsTracker() != null;

        if (shouldTrackColdStartupMetrics) {
            getActivityTabStartupMetricsTracker().setHistogramSuffix(ActivityType.TABBED);
        } else {
            getActivityTabStartupMetricsTracker().cancelTrackingStartupMetrics();
        }
    }

    private void setInitialOverviewState(boolean shouldShowOverviewPageOnStart) {
        if (StartSurfaceConfiguration.isNtpAsHomeSurfaceEnabled(isTablet())) {
            if (mFromResumption) {
                setInitialOverviewStateWithNtp();
            }
            return;
        }
        if (mHasDeterminedOverviewStateForCurrentSession) return;

        mHasDeterminedOverviewStateForCurrentSession = true;
        boolean isOverviewVisible = isInOverviewMode();

        if (shouldShowOverviewPageOnStart && !isOverviewVisible) {
            mOverviewShownOnStart = true;
            // Cancel recording cold startup metrics if an overview is shown as they expect a tab to
            // be the first thing shown after startup.
            setTrackColdStartupMetrics(false);
            mExperimentalStartupMetricsTracker.destroy();
            disablePaintPreviewOnRestore();
            showOverview(StartSurfaceState.SHOWING_START);
            mAppLaunchDrawBlocker.onOverviewPageAvailable(
                    mOverviewShownOnStart && !isInstantStartEnabled());
            return;
        }

        if (getActivityTab() == null
                && !isOverviewVisible
                && ReturnToChromeUtil.isStartSurfaceEnabled(this)) {
            mOverviewShownOnStart = true;
            // Cancel recording cold startup metrics if an overview is shown as they expect a tab to
            // be the first thing shown after startup.
            setTrackColdStartupMetrics(false);
            mExperimentalStartupMetricsTracker.destroy();
            disablePaintPreviewOnRestore();
            showOverview(StartSurfaceState.SHOWING_START);
        }

        if (IntentUtils.isMainIntentFromLauncher(getIntent()) && isInOverviewMode()) {
            RecordUserAction.record("MobileStartup.UserEnteredTabSwitcher");
        }
        mAppLaunchDrawBlocker.onOverviewPageAvailable(
                mOverviewShownOnStart && !isInstantStartEnabled());
    }

    /**
     * Called on warm startup to show a home surface NTP instead of the last active Tab if the user
     * has left Chrome for a while.
     */
    private void setInitialOverviewStateWithNtp() {
        boolean showedNtp =
                ReturnToChromeUtil.setInitialOverviewStateOnResumeWithNtp(
                        mTabModelSelector.isIncognitoSelected(),
                        shouldShowNtpHomeSurfaceOnStartup(),
                        getCurrentTabModel(),
                        getTabCreator(false),
                        mHomeSurfaceTracker);
        if (showedNtp
                && HubFieldTrial.isHubEnabled()
                && mLayoutManager.isLayoutVisible(LayoutType.TAB_SWITCHER)) {
            mLayoutManager.showLayout(LayoutType.BROWSING, /* animate= */ false);
        }
    }

    private void logMainIntentBehavior(Intent intent) {
        assert IntentUtils.isMainIntentFromLauncher(intent);
        // TODO(tedchoc): We should cache the last visible time and reuse it to avoid different
        //                values of this depending on when it is called after the activity was
        //                shown.

        // Temporary safety check to make sure none of this code runs if the feature is
        // disabled.
        if (ReengagementNotificationController.isEnabled()) {
            if (mCallbackController != null) {
                new OneShotCallback<>(
                        mTabModelProfileSupplier,
                        mCallbackController.makeCancelable(
                                profile -> {
                                    assert profile != null
                                            : "Unexpectedly null profile from TabModel.";
                                    if (profile == null) return;

                                    TrackerFactory.getTrackerForProfile(profile)
                                            .notifyEvent(EventConstants.STARTED_FROM_MAIN_INTENT);
                                }));
            }
        }

        mMainIntentMetrics.onMainIntentWithNative(
                mInactivityTracker.getTimeSinceLastBackgroundedMs());
    }

    /** Access the main intent metrics for test validation. */
    public MainIntentBehaviorMetrics getMainIntentBehaviorMetricsForTesting() {
        return mMainIntentMetrics;
    }

    public ChromeInactivityTracker getInactivityTrackerForTesting() {
        return mInactivityTracker;
    }

    @Override
    public void initializeState() {
        // This method goes through 3 steps:
        // 1. Load the saved tab state (but don't start restoring the tabs yet).
        // 2. Process the Intent that this activity received and if that should result in any
        //    new tabs, create them.  This is done after step 1 so that the new tab gets
        //    created after previous tab state was restored.
        // 3. If no tabs were created in any of the above steps, create an NTP, otherwise
        //    start asynchronous tab restore (loading the previously active tab synchronously
        //    if no new tabs created in step 2).

        // Only look at the original intent if this is not a "restoration" and we are allowed to
        // process intents. Any subsequent intents are carried through onNewIntent.
        try {
            TraceEvent.begin("ChromeTabbedActivity.initializeState");

            super.initializeState();
            Log.i(TAG, "#initializeState");
            Intent intent = getIntent();

            boolean hadCipherData =
                    CipherFactory.getInstance().restoreFromBundle(getSavedInstanceState());

            boolean noRestoreState =
                    CommandLine.getInstance().hasSwitch(ChromeSwitches.NO_RESTORE_STATE);
            boolean shouldShowNtpAsHomeSurfaceAtStartup = false;
            final AtomicBoolean isActiveUrlNtp = new AtomicBoolean(false);
            if (noRestoreState) {
                // Clear the state files because they are inconsistent and useless from now on.
                mTabModelOrchestrator.clearState();
            } else {
                // State should be clear when we start first run and hence we do not need to load
                // a previous state. This may change the current Model, watch out for initialization
                // based on the model.
                // Never attempt to restore incognito tabs when this activity was previously swiped
                // away in Recents. http://crbug.com/626629
                boolean ignoreIncognitoFiles = !hadCipherData;

                // If the Start surface should be shown on startup, check if the active tab restored
                // from disk is an NTP that can be reused for Start.
                Callback<String> onStandardActiveIndexRead = null;
                shouldShowNtpAsHomeSurfaceAtStartup = shouldShowNtpHomeSurfaceOnStartup();
                boolean skipSavingNonActiveNtps = skipSavingNonActiveNtps();
                if (skipSavingNonActiveNtps) {
                    mHomeSurfaceTracker = new HomeSurfaceTracker();
                }
                if (shouldShowNtpAsHomeSurfaceAtStartup) {
                    onStandardActiveIndexRead =
                            url -> {
                                mLastActiveTabUrl = url;
                                if (UrlUtilities.isNtpUrl(url)) {
                                    assert !mTabModelSelector.isIncognitoSelected();
                                    isActiveUrlNtp.set(true);
                                }
                            };
                }
                mTabModelOrchestrator.loadState(ignoreIncognitoFiles, onStandardActiveIndexRead);
                mTabModelOrchestrator.setSkipSavingNonActiveNtps(skipSavingNonActiveNtps);
            }

            mAuxiliarySearchController =
                    AuxiliarySearchControllerFactory.createAuxiliarySearchController(
                            Profile.getLastUsedRegularProfile(), mTabModelSelector);
            if (mAuxiliarySearchController != null) {
                mAuxiliarySearchController.register(this.getLifecycleDispatcher());
            }
            mInactivityTracker.register(this.getLifecycleDispatcher());
            boolean isIntentWithEffect = false;
            boolean isMainIntentFromLauncher = false;
            if (getSavedInstanceState() == null && intent != null) {
                if (!shouldIgnoreIntent()) isIntentWithEffect = maybeHandleUrlIntent(intent);

                if (IntentUtils.isMainIntentFromLauncher(intent)) {
                    isMainIntentFromLauncher = true;
                    logMainIntentBehavior(intent);
                }
            }

            mIntentMetadataOneshotSupplier.set(
                    new ToolbarIntentMetadata(isMainIntentFromLauncher, isIntentWithEffect));

            // If we have tabs to reparent and getSavedInstanceState() is non-null, then the tabs
            // are coming from night mode tab reparenting. In this case, reparenting happens
            // synchronously along with tab restoration so there are no tabs waiting for
            // reparenting like there are for other tab reparenting operations.
            boolean hasTabWaitingForReparenting =
                    AsyncTabParamsManagerSingleton.getInstance().hasParamsWithTabToReparent()
                            && getSavedInstanceState() == null;
            mCreatedTabOnStartup =
                    getCurrentTabModel().getCount() > 0
                            || mTabModelOrchestrator.getRestoredTabCount() > 0
                            || isIntentWithEffect
                            || hasTabWaitingForReparenting;

            // We always need to try to restore tabs. The set of tabs might be empty, but at least
            // it will trigger the notification that tab restore is complete which is needed by
            // other parts of Chrome such as sync.
            boolean activeTabBeingRestored =
                    !isIntentWithEffect
                            || (shouldShowOverviewPageOnStart()
                                    && !mTabModelSelector.isIncognitoSelected());

            if (shouldShowNtpAsHomeSurfaceAtStartup
                    && !isIntentWithEffect
                    && !hasTabWaitingForReparenting) {
                // If a home surface should be shown at startup on tablets and the last active Tab
                // is a NTP, we will reuse it to show the home surface UI. Otherwise, we'll create
                // one, and set it as the active Tab. |mLastActiveTabUrl| is null when there isn't
                // any Tab.
                if (!isActiveUrlNtp.get() && mLastActiveTabUrl != null) {
                    ReturnToChromeUtil.createNewTabAndShowHomeSurfaceUi(
                            getTabCreator(false),
                            mHomeSurfaceTracker,
                            mTabModelSelector,
                            mLastActiveTabUrl,
                            null);
                    activeTabBeingRestored = false;
                    mCreatedTabOnStartup = true;
                    mLastActiveTabUrl = null;
                }
                ReturnToChromeUtil.recordHomeSurfaceShownAtStartup();
                ReturnToChromeUtil.recordHomeSurfaceShown();
            }

            mTabModelOrchestrator.restoreTabs(activeTabBeingRestored);

            // Only create an initial tab if no tabs were restored and no intent was handled.
            // Also, check whether the active tab was supposed to be restored and that the total
            // tab count is now non zero.  If this is not the case, tab restore failed and we need
            // to create a new tab as well.
            if (!mCreatedTabOnStartup
                    || (!hasTabWaitingForReparenting
                            && activeTabBeingRestored
                            && getTabModelSelector().getTotalTabCount() == 0)) {
                // If homepage URI is not determined, due to PartnerBrowserCustomizations provider
                // async reading, then create a tab at the async reading finished. If it takes
                // too long, just create NTP.
                mPendingInitialTabCreation = true;
                PartnerBrowserCustomizations.getInstance()
                        .setOnInitializeAsyncFinished(
                                () -> {
                                    boolean isActivityFinishingOrDestroyed =
                                            isActivityFinishingOrDestroyed();
                                    PartnerBrowserCustomizations.logActivityFinishingOrDestroyed(
                                            isActivityFinishingOrDestroyed);
                                    if (!isActivityFinishingOrDestroyed) {
                                        createInitialTab();
                                    }
                                },
                                INITIAL_TAB_CREATION_TIMEOUT_MS);
            }

            // If initial tab creation is pending, this will instead be handled when we create the
            // initial tab in #createInitialTab.
            if (!mPendingInitialTabCreation) {
                Tab currentTab = getActivityTab();
                boolean isTabNtp = isTabRegularNtp(currentTab);
                if (isTabNtp && !currentTab.isNativePage()) {
                    // This will be a NTP, but the native page hasn't been created yet. Need to wait
                    // for this to be created before allowing the toolbar to draw.
                    currentTab.addObserver(
                            new EmptyTabObserver() {
                                @Override
                                public void onContentChanged(Tab tab) {
                                    tab.removeObserver(this);
                                    mAppLaunchDrawBlocker.onActiveTabAvailable(
                                            /* isTabNtp= */ true);
                                }
                            });
                } else {
                    mAppLaunchDrawBlocker.onActiveTabAvailable(isTabNtp);
                }
            }

            if (getSavedInstanceState() != null) {
                long unfoldLatencyBeginTime =
                        getSavedInstanceState()
                                .getLong(ChromeActivity.UNFOLD_LATENCY_BEGIN_TIMESTAMP);
                if (unfoldLatencyBeginTime != 0) {
                    getWindowAndroid().setUnfoldLatencyBeginTime(unfoldLatencyBeginTime);
                }
            }
        } finally {
            TraceEvent.end("ChromeTabbedActivity.initializeState");
        }
    }

    private boolean hasStartWithNativeBeenCalled() {
        int activity_state = getLifecycleDispatcher().getCurrentActivityState();
        return activity_state == ActivityLifecycleDispatcher.ActivityState.STARTED_WITH_NATIVE
                || activity_state == ActivityLifecycleDispatcher.ActivityState.RESUMED_WITH_NATIVE;
    }

    /** Create an initial tab for cold start without restored tabs. */
    private void createInitialTab() {
        Log.i(TAG, "#createInitialTab executed.");
        mPendingInitialTabCreation = false;

        // If the start surface or grid tab switcher will be shown on start, do not create a new
        // tab.
        String url = null;
        long createInitialTabStartTime = SystemClock.elapsedRealtime();
        boolean shouldShowOverviewPageOnStart = shouldShowOverviewPageOnStart();
        if (!shouldShowOverviewPageOnStart) {
            GURL homepageGurl = HomepageManager.getHomepageGurl();
            if (homepageGurl.isEmpty()) {
                url = UrlConstants.NTP_URL;
            } else {
                // Migrate legacy NTP URLs (chrome://newtab) to the newer format
                // (chrome-native://newtab)
                if (UrlUtilities.isNtpUrl(homepageGurl)) {
                    url = UrlConstants.NTP_URL;
                } else {
                    url = homepageGurl.getSpec();
                }
            }
            getTabCreator(false).launchUrl(url, TabLaunchType.FROM_STARTUP);
        }
        PartnerBrowserCustomizations.getInstance()
                .onCreateInitialTab(
                        url,
                        createInitialTabStartTime,
                        shouldShowOverviewPageOnStart,
                        getLifecycleDispatcher(),
                        HomepageManager::getHomepageCharacterizationHelper);

        // If we didn't call setInitialOverviewState() in onStartWithNative() because
        // mPendingInitialTabCreation was true then do so now.
        if (hasStartWithNativeBeenCalled()) {
            // We have to use the cached value of shouldShowOverviewPageOnStart because once a new
            // Tab is created above, the return value of shouldShowOverviewPageOnStart() may change
            // since the Tab count is increased from 0 to 1.
            setInitialOverviewState(shouldShowOverviewPageOnStart);
        }

        mAppLaunchDrawBlocker.onActiveTabAvailable(isTabRegularNtp(getActivityTab()));
    }

    private void recordExternalIntentSourceUMA(Intent intent) {
        @IntentHandler.ExternalAppId
        int externalId = IntentHandler.determineExternalIntentSource(intent);

        // Don't record external app page loads for intents we sent.
        if (externalId == IntentHandler.ExternalAppId.CHROME) return;
        RecordHistogram.recordEnumeratedHistogram(
                "MobileIntent.PageLoadDueToExternalApp",
                externalId,
                IntentHandler.ExternalAppId.NUM_ENTRIES);
    }

    /**
     * Records an action when a user chose to handle a URL in Chrome that could have been handled
     * by an application installed on the phone. Also records the name of that application.
     * This doesn't include generic URL handlers, such as browsers.
     */
    private static void recordAppHandlersForIntent(Intent intent) {
        List<String> packages =
                IntentUtils.safeGetStringArrayListExtra(
                        intent, ExternalNavigationHandler.EXTRA_EXTERNAL_NAV_PACKAGES);
        if (packages != null && packages.size() > 0) {
            RecordUserAction.record("MobileExternalNavigationReceived");
        }
    }

    /**
     * Processes a url view intent.
     *
     * @param url The url from the intent.
     */
    private void processUrlViewIntent(
            LoadUrlParams loadUrlParams,
            @TabOpenType int tabOpenType,
            String externalAppId,
            int tabIdToBringToFront,
            Intent intent) {
        if (isActivityFinishingOrDestroyed()) {
            return;
        }
        if (isProbablyFromChrome(intent, externalAppId)) {
            RecordUserAction.record("MobileTabbedModeViewIntentFromChrome");
        } else {
            RecordUserAction.record("MobileTabbedModeViewIntentFromApp");
        }

        recordExternalIntentSourceUMA(intent);
        recordAppHandlersForIntent(intent);

        final String url = loadUrlParams.getUrl();
        boolean fromLauncherShortcut =
                IntentUtils.safeGetBooleanExtra(
                        intent, IntentHandler.EXTRA_INVOKED_FROM_SHORTCUT, false);
        boolean fromAppWidget =
                IntentUtils.safeGetBooleanExtra(
                        intent, IntentHandler.EXTRA_INVOKED_FROM_APP_WIDGET, false);
        boolean focus = false;

        TabModel tabModel = getCurrentTabModel();
        switch (tabOpenType) {
            case TabOpenType.REUSE_URL_MATCHING_TAB_ELSE_NEW_TAB:
                mTabModelOrchestrator.tryToRestoreTabStateForUrl(url);
                int tabToBeClobberedIndex = TabModelUtils.getTabIndexByUrl(tabModel, url);
                Tab tabToBeClobbered = tabModel.getTabAt(tabToBeClobberedIndex);
                if (tabToBeClobbered != null) {
                    TabModelUtils.setIndex(tabModel, tabToBeClobberedIndex, false);
                    tabToBeClobbered.reload();
                } else {
                    launchIntent(loadUrlParams, externalAppId, true, intent);
                }
                int shortcutSource =
                        intent.getIntExtra(WebappConstants.EXTRA_SOURCE, ShortcutSource.UNKNOWN);
                LaunchMetrics.recordHomeScreenLaunchIntoTab(url, shortcutSource);
                if (fromAppWidget && UrlConstants.CHROME_DINO_URL.equals(url)) {
                    RecordUserAction.record("QuickActionSearchWidget.StartDinoGame");
                }
                break;
            case TabOpenType.BRING_TAB_TO_FRONT:
                mTabModelOrchestrator.tryToRestoreTabStateForId(tabIdToBringToFront);

                int tabIndex = TabModelUtils.getTabIndexById(tabModel, tabIdToBringToFront);
                if (tabIndex == TabModel.INVALID_TAB_INDEX) {
                    TabModel otherModel = getTabModelSelector().getModel(!tabModel.isIncognito());
                    tabIndex = TabModelUtils.getTabIndexById(otherModel, tabIdToBringToFront);
                    if (tabIndex != TabModel.INVALID_TAB_INDEX) {
                        getTabModelSelector().selectModel(otherModel.isIncognito());
                        TabModelUtils.setIndex(otherModel, tabIndex, false);
                    } else {
                        Log.e(TAG, "Failed to bring tab to front because it doesn't exist.");
                        return;
                    }
                } else {
                    TabModelUtils.setIndex(tabModel, tabIndex, false);
                }
                break;
            case TabOpenType.CLOBBER_CURRENT_TAB:
                // The browser triggered the intent. This happens when clicking links which
                // can be handled by other applications (e.g. www.youtube.com links).
                Tab currentTab = getActivityTab();
                if (currentTab != null) {
                    RedirectHandlerTabHelper.updateIntentInTab(currentTab, intent);
                    currentTab.loadUrl(loadUrlParams);
                } else {
                    launchIntent(loadUrlParams, externalAppId, true, intent);
                }
                break;
            case TabOpenType.REUSE_APP_ID_MATCHING_TAB_ELSE_NEW_TAB:
                launchIntent(loadUrlParams, externalAppId, false, intent);
                break;
            case TabOpenType.REUSE_TAB_MATCHING_ID_ELSE_NEW_TAB:
                int tabId =
                        IntentUtils.safeGetIntExtra(
                                intent,
                                TabOpenType.REUSE_TAB_MATCHING_ID_STRING,
                                Tab.INVALID_TAB_ID);
                if (tabId != Tab.INVALID_TAB_ID) {
                    mTabModelOrchestrator.tryToRestoreTabStateForId(tabId);
                    int matchingTabIndex = TabModelUtils.getTabIndexById(tabModel, tabId);
                    boolean loaded = false;
                    if (matchingTabIndex != TabModel.INVALID_TAB_INDEX) {
                        Tab tab = tabModel.getTabAt(matchingTabIndex);
                        String spec = tab.getUrl().getSpec();
                        if (spec.equals(url)
                                || spec.equals(
                                        IntentUtils.safeGetStringExtra(
                                                intent,
                                                TabOpenType.REUSE_TAB_ORIGINAL_URL_STRING))) {
                            tabModel.setIndex(matchingTabIndex, TabSelectionType.FROM_USER, false);
                            tab.loadUrl(loadUrlParams);
                            loaded = true;
                        }
                    }
                    if (!loaded) {
                        launchIntent(loadUrlParams, externalAppId, false, intent);
                    }
                }
                break;
            case TabOpenType.OPEN_NEW_TAB:
                if (fromLauncherShortcut) {
                    recordLauncherShortcutAction(false);
                    reportNewTabShortcutUsed(false);
                }

                launchIntent(loadUrlParams, externalAppId, true, intent);
                break;
            case TabOpenType.OPEN_NEW_INCOGNITO_TAB:
                if (!TextUtils.equals(externalAppId, getPackageName())) {
                    assert false : "Only Chrome is allowed to open incognito tabs";
                    Log.e(TAG, "Only Chrome is allowed to open incognito tabs");
                    return;
                }

                if (!IncognitoUtils.isIncognitoModeEnabled()) {
                    // The incognito launcher shortcut is manipulated in #onDeferredStartup(),
                    // so it's possible for a user to invoke the shortcut before it's disabled.
                    // Quick actions search widget is installed on the home screen and may
                    // need to be updated before the incognito button is removed.
                    // Opening an incognito tab while incognito mode is disabled from somewhere
                    // besides the launcher shortcut of from quick action search widget is an
                    // error.
                    if (fromAppWidget || fromLauncherShortcut) {
                        // We are using the message introduced for quick action search widget
                        // for both the widget and the launcher shortcut here.
                        Toast.makeText(
                                        ChromeTabbedActivity.this,
                                        R.string.quick_action_search_widget_message_no_incognito,
                                        Toast.LENGTH_LONG)
                                .show();
                    } else {
                        assert false : "Tried to open incognito tab while incognito disabled";
                        Log.e(TAG, "Tried to open incognito tab while incognito disabled");
                    }

                    return;
                }

                if (url == null || url.equals(UrlConstants.NTP_URL)) {
                    if (fromLauncherShortcut) {
                        getTabCreator(true)
                                .launchUrl(
                                        UrlConstants.NTP_URL, TabLaunchType.FROM_LAUNCHER_SHORTCUT);
                        recordLauncherShortcutAction(true);
                        reportNewTabShortcutUsed(true);
                    } else if (fromAppWidget) {
                        RecordUserAction.record("QuickActionSearchWidget.StartIncognito");
                        getTabCreator(true)
                                .launchUrl(UrlConstants.NTP_URL, TabLaunchType.FROM_APP_WIDGET);
                    } else if (IncognitoTabLauncher.didCreateIntent(intent)) {
                        Tab tab =
                                getTabCreator(true)
                                        .launchUrl(
                                                UrlConstants.NTP_URL,
                                                TabLaunchType.FROM_LAUNCH_NEW_INCOGNITO_TAB);
                        if (IncognitoTabLauncher.shouldFocusOmnibox(intent)) {
                            // Since the Tab is created in the foreground, its View will gain
                            // focus, and since the Tab and the URL bar are not yet in the same
                            // View hierarchy, setting the URL bar's focus here won't clear the
                            // Tab's focus. When the Tab is added to the hierarchy, we want the
                            // URL bar to retain focus, so we clear the Tab's focus here.
                            tab.getView().clearFocus();
                            focus = true;
                        }

                        IncognitoTabLauncher.recordUse();
                    } else {
                        // Used by the Account management screen to open a new incognito tab.
                        // Account management screen collects its metrics separately.
                        getTabCreator(true)
                                .launchUrl(
                                        UrlConstants.NTP_URL,
                                        TabLaunchType.FROM_CHROME_UI,
                                        intent,
                                        mIntentHandlingTimeMs);
                    }
                } else {
                    launchIntent(loadUrlParams, externalAppId, true, intent);
                }
                break;
            default:
                assert false : "Unknown TabOpenType: " + tabOpenType;
                break;
        }

        getToolbarManager()
                .setUrlBarFocusOnceNativeInitialized(
                        focus,
                        focus
                                ? OmniboxFocusReason.LAUNCH_NEW_INCOGNITO_TAB
                                : OmniboxFocusReason.UNFOCUS);

        if (tabModel.getCount() > 0
                && isInOverviewMode()
                && !isTablet()
                && !shouldShowOverviewPageOnStart(intent)) {
            boolean wasStartSurfaceHomepageShowing =
                    mStartSurfaceSupplier.hasValue()
                            && mStartSurfaceSupplier.get().isHomepageShown();

            // Hides the overview page to ensure proper layout change signals are sent.
            hideOverview();

            // If the Start Surface was showing, the ContentView for the currently selected Tab
            // behind the Start Surface may not yet be attached and therefore the Tab may not
            // be interactive. Notify CompositorViewHolder that content has changed, which
            // will cause the Tab's ContentView to be attached. See https://crbug.com/1457603.
            if (wasStartSurfaceHomepageShowing) mCompositorViewHolder.onContentChanged();
        }
    }

    private boolean isProbablyFromChrome(Intent intent, String externalAppId) {
        // To determine if the processed intent is from Chrome, check for any of the following:
        // 1.) The authentication token that will be added to trusted intents.
        // 2.) The app ID matches Chrome.  This value can be spoofed by other applications, but
        //     in cases where we were not able to add the authentication token this is our only
        //     indication the intent was from Chrome.
        return IntentHandler.wasIntentSenderChrome(intent)
                || TextUtils.equals(externalAppId, getPackageName());
    }

    private boolean useNewColdStartHeuristic() {
        return ChromeFeatureList.sPaintPreviewNewColdStartHeuristic.isEnabled();
    }

    @Override
    public void performPreInflationStartup() {
        super.performPreInflationStartup();

        // Android FrameMetrics allow tracking of java views and their deadline misses (frame
        // drops/janks).
        if (ChromeFeatureList.sCollectAndroidFrameTimelineMetrics.isEnabled()) {
            mJankTracker = new JankTrackerImpl(this);
        } else {
            mJankTracker = new PlaceholderJankTracker();
        }

        // Decide whether to record startup UMA histograms. This is done early in the main
        // Activity.onCreate() to avoid recording navigation delays when they require user input to
        // proceed. Having an uninitialized native library has been taken as a sign of starting
        // Chrome with an immediate navigation without user input.
        // TODO(crbug.com/1471136): Native library initialization was moved to another thread, and
        //  it now proceeds faster, making the metrics think that the start is not cold enough.
        //  To cover more startup cases change the heuristic detecting cold startup that happens
        //  without user interaction.
        if (!LibraryLoader.getInstance().isInitialized()) {
            if (!useNewColdStartHeuristic()) {
                StartupPaintPreviewHelper.setShouldShowOnRestore(true);
            }
            setTrackColdStartupMetrics(true);
        }

        // Enable Paint Preview only on a cold start. This way the Paint preview is most useful by
        // being much faster than the real load of the page. Also cold start detection excludes user
        // interactions changing the course of restoring the page.
        if (useNewColdStartHeuristic()
                && ColdStartTracker.wasColdOnFirstActivityCreationOrNow()
                && !SimpleStartupForegroundSessionDetector.isSessionDiscarded()) {
            StartupPaintPreviewHelper.setShouldShowOnRestore(true);
        }

        supportRequestWindowFeature(Window.FEATURE_ACTION_MODE_OVERLAY);
        IncognitoTabHostRegistry.getInstance().register(mIncognitoTabHost);
        mStartupPaintPreviewHelperSupplier.attach(getWindowAndroid().getUnownedUserDataHost());
        if (DseNewTabUrlManager.isNewTabSearchEngineUrlAndroidEnabled()) {
            mDseNewTabUrlManager = new DseNewTabUrlManager(mTabModelProfileSupplier);
        }

        initHub();
    }

    @Override
    protected RootUiCoordinator createRootUiCoordinator() {
        return new TabbedRootUiCoordinator(
                this,
                this::onOmniboxFocusChanged,
                getShareDelegateSupplier(),
                getActivityTabProvider(),
                mTabModelProfileSupplier,
                mBookmarkModelSupplier,
                mTabBookmarkerSupplier,
                getContextualSearchManagerSupplier(),
                getTabModelSelectorSupplier(),
                mStartSurfaceSupplier,
                mTabSwitcherSupplier,
                mIncognitoTabSwitcherSupplier,
                mHubManagerSupplier,
                mIntentMetadataOneshotSupplier,
                mLayoutStateProviderSupplier,
                mStartSurfaceParentTabSupplier,
                getBrowserControlsManager(),
                getWindowAndroid(),
                getLifecycleDispatcher(),
                getLayoutManagerSupplier(),
                /* menuOrKeyboardActionController= */ this,
                this::getActivityThemeColor,
                getModalDialogManagerSupplier(),
                /* appMenuBlocker= */ this,
                this::supportsAppMenu,
                this::supportsFindInPage,
                getTabCreatorManagerSupplier(),
                getFullscreenManager(),
                getCompositorViewHolderSupplier(),
                getTabContentManagerSupplier(),
                this::getSnackbarManager,
                getEdgeToEdgeSupplier(),
                getActivityType(),
                this::isInOverviewMode,
                this::isWarmOnResume,
                /* appMenuDelegate= */ this,
                /* statusBarColorProvider= */ this,
                new ObservableSupplierImpl<>(),
                getIntentRequestTracker(),
                getControlContainerHeightResource(),
                mInsetObserverViewSupplier,
                this::backShouldCloseTab,
                getTabReparentingControllerSupplier(),
                // TODO(sinansahin): This currently only checks for incognito extras in the intent.
                // We should make it more robust by using more signals.
                IntentHandler.hasAnyIncognitoExtra(getIntent().getExtras()),
                mBackPressManager,
                getSavedInstanceState(),
                mMultiInstanceManager);
    }

    @Override
    protected int getControlContainerLayoutId() {
        return R.layout.control_container;
    }

    @Override
    public int getControlContainerHeightResource() {
        return R.dimen.control_container_height;
    }

    @Override
    protected int getToolbarLayoutId() {
        return isTablet() ? R.layout.toolbar_tablet : R.layout.toolbar_phone;
    }

    @Override
    public void performPostInflationStartup() {
        super.performPostInflationStartup();

        if (isFinishing()) return;

        FontPreloader.getInstance().onPostInflationStartupTabbedActivity();

        TabModelSelector tabModelSelector = getTabModelSelector();
        IncognitoProfileDestroyer.observeTabModelSelector(tabModelSelector);
        IncognitoNotificationPresenceController.observeTabModelSelector(tabModelSelector);

        // Don't show the keyboard until user clicks in.
        getWindow()
                .setSoftInputMode(
                        WindowManager.LayoutParams.SOFT_INPUT_STATE_ALWAYS_HIDDEN
                                | WindowManager.LayoutParams.SOFT_INPUT_ADJUST_RESIZE);

        mContentContainer = (ViewGroup) findViewById(android.R.id.content);
        mControlContainer = (ToolbarControlContainer) findViewById(R.id.control_container);

        // Instead of overriding AsyncInitializationActivity#onFirstDrawComplete like the other
        // activities, we're adding our own draw detector here because this activity's draw can be
        // blocked by AppLaunchDrawBlocker, and #onFirstDrawComplete doesn't account for that.
        FirstDrawDetector.waitForFirstDrawStrict(
                mContentContainer, () -> FontPreloader.getInstance().onFirstDrawTabbedActivity());

        Supplier<Boolean> dialogVisibilitySupplier = null;
        dialogVisibilitySupplier =
                () -> {
                    // Return true if dialog from either tab switcher or tab strip is visible.
                    ToolbarManager toolbarManager = getToolbarManager();
                    TabGroupUi tabGroupUi = toolbarManager.getTabGroupUi();
                    boolean isDialogVisible =
                            tabGroupUi != null && tabGroupUi.isTabGridDialogVisible();

                    if (isStartSurfaceRefactorEnabled() && !mTabSwitcherSupplier.hasValue()) {
                        // The grid tab switcher may be lazily initialized; early out if it isn't
                        // ready.
                        return isDialogVisible;
                    }

                    Supplier<Boolean> tabSwitcherDialogVisibilitySupplier =
                            isStartSurfaceRefactorEnabled()
                                    ? mTabSwitcherSupplier
                                            .get()
                                            .getTabGridDialogVisibilitySupplier()
                                    : (mStartSurfaceSupplier == null
                                                    || !mStartSurfaceSupplier.hasValue())
                                            ? null
                                            : mStartSurfaceSupplier
                                                    .get()
                                                    .getTabGridDialogVisibilitySupplier();

                    if (tabSwitcherDialogVisibilitySupplier != null) {
                        isDialogVisible |= tabSwitcherDialogVisibilitySupplier.get();
                    }
                    var incognitoTabSwitcher = mIncognitoTabSwitcherSupplier.get();
                    if (incognitoTabSwitcher != null) {
                        var incognitoDialogVisibilitySupplier =
                                incognitoTabSwitcher.getTabGridDialogVisibilitySupplier();
                        if (incognitoDialogVisibilitySupplier != null) {
                            isDialogVisible |= incognitoDialogVisibilitySupplier.get();
                        }
                    }
                    return isDialogVisible;
                };

        mUndoBarPopupController =
                new UndoBarController(
                        this,
                        mTabModelSelector,
                        this::getSnackbarManager,
                        dialogVisibilitySupplier);

        mInactivityTracker =
                new ChromeInactivityTracker(
                        ChromePreferenceKeys.TABBED_ACTIVITY_LAST_BACKGROUNDED_TIME_MS_PREF);
        TabUsageTracker.initialize(this.getLifecycleDispatcher(), tabModelSelector);

        assert getActivityTabStartupMetricsTracker() != null;
        boolean shouldShowOverviewPageOnStart = shouldShowOverviewPageOnStart();
        StartSurfaceUserData.getInstance()
                .setUnusedTabRestoredAtStartup(shouldShowOverviewPageOnStart);
        StartupPaintPreviewHelper paintPreviewHelper =
                new StartupPaintPreviewHelper(
                        getWindowAndroid(),
                        getOnCreateTimestampMs(),
                        getBrowserControlsManager(),
                        getTabModelSelector(),
                        shouldShowOverviewPageOnStart,
                        () -> {
                            return getToolbarManager() == null
                                    ? null
                                    : getToolbarManager().getProgressBarCoordinator();
                        });
        mStartupPaintPreviewHelperSupplier.set(paintPreviewHelper);
        getActivityTabStartupMetricsTracker().registerPaintPreviewObserver(paintPreviewHelper);

        maybeRegisterHomeModules();
    }

    private void maybeRegisterHomeModules() {
        if (!StartSurfaceConfiguration.useMagicStack()) return;

        ModuleRegistry moduleRegistry = ModuleRegistry.getInstance();
        SingleTabModuleBuilder singleTabModuleBuilder =
                new SingleTabModuleBuilder(
                        this, getTabModelSelectorSupplier(), getTabContentManagerSupplier());
        moduleRegistry.registerModule(ModuleType.SINGLE_TAB, singleTabModuleBuilder);

        if (ChromeFeatureList.sPriceChangeModule.isEnabled()) {
            PriceChangeModuleBuilder priceChangeModuleBuilder =
                    new PriceChangeModuleBuilder(this, mTabModelProfileSupplier, mTabModelSelector);
            moduleRegistry.registerModule(ModuleType.PRICE_CHANGE, priceChangeModuleBuilder);
        }
    }

    private boolean shouldIgnoreIntent() {
        if (mShouldIgnoreIntent == null) {
            // We call this only once to give a consistent view of whether the intent should be
            // ignored during startup as this function depends on transient state like whether the
            // screen is on.
            mShouldIgnoreIntent = IntentHandler.shouldIgnoreIntent(getIntent());
        }
        return mShouldIgnoreIntent;
    }

    @Override
    protected final void dispatchOnInflationComplete() {
        super.dispatchOnInflationComplete();

        // When the feature flag {@link ChromeFeatureList.INSTANT_START} turns on phones (not
        // tablet), a view-only start page created on Java will be shown before native is
        // initialized. The {@link prepareToShowStartPagePreNative()} is only called in a cold
        // start.
        if (ReturnToChromeUtil.isStartSurfaceEnabled(this)
                && isInstantStartEnabled()
                && !hadWarmStart()) {
            prepareToShowStartPagePreNative();
        }
    }

    /**
     * Prepares to show the start page before native is initialized. For example, create
     * an LayoutManagerChrome object, add overview mode observer and so on.
     */
    private void prepareToShowStartPagePreNative() {
        assert isInstantStartEnabled() && !hadWarmStart() && !mFromResumption;
        try (TraceEvent e =
                TraceEvent.scoped("ChromeTabbedActivity.prepareToShowStartPagePreNative")) {
            setupCompositorContentPreNativeForPhone();
            getCompositorViewHolderSupplier().get().setLayoutManager(mLayoutManager);

            if (shouldShowOverviewPageOnStart()) {
                mLayoutManager.setTabModelSelector(mTabModelSelector);
                assert !mHasDeterminedOverviewStateForCurrentSession;
                setInitialOverviewState(/* shouldShowOverviewPageOnStart= */ true);
            }
        }
    }

    @Override
    protected TabModelOrchestrator createTabModelOrchestrator() {
        boolean tabMergingEnabled =
                mMultiInstanceManager != null && mMultiInstanceManager.isTabModelMergingEnabled();
        mTabModelOrchestrator = new TabbedModeTabModelOrchestrator(tabMergingEnabled);
        if (ChromeFeatureList.sTabStripStartupRefactoring.isEnabled()) {
            mTabModelStartupInfoSupplier = new ObservableSupplierImpl<>();
            mTabModelOrchestrator.setStartupInfoObservableSupplier(mTabModelStartupInfoSupplier);
        }
        return mTabModelOrchestrator;
    }

    @Override
    protected void createTabModels() {
        assert mTabModelSelector == null;
        assert mWindowId != INVALID_WINDOW_ID;

        Bundle savedInstanceState = getSavedInstanceState();

        // We determine the model as soon as possible so every systems get initialized coherently.
        boolean startIncognito =
                savedInstanceState != null
                        && savedInstanceState.getBoolean(IS_INCOGNITO_SELECTED, false);

        mNextTabPolicySupplier = new ChromeNextTabPolicySupplier(mLayoutStateProviderSupplier);

        boolean tabModelWasCreated =
                mTabModelOrchestrator.createTabModels(
                        this,
                        getProfileProviderSupplier(),
                        this,
                        mNextTabPolicySupplier,
                        mWindowId);
        if (!tabModelWasCreated) {
            finishAndRemoveTask();
            return;
        }

        if (mMultiInstanceManager != null) {
            int assignedIndex = TabWindowManagerSingleton.getInstance().getIndexForWindow(this);
            // The given index and the one computed by TabWindowManager should be one and the same.
            int taskId = ApplicationStatus.getTaskId(this);
            Map<String, Integer> taskMap =
                    ChromeSharedPreferences.getInstance()
                            .readIntsWithPrefix(ChromePreferenceKeys.MULTI_INSTANCE_TASK_MAP);
            String message =
                    String.format(
                            Locale.getDefault(),
                            "Instance mismatch for assignedIndex: %d, mWindowId: %d with taskId:"
                                    + " %s and taskMap: %s",
                            assignedIndex,
                            mWindowId,
                            taskId,
                            taskMap);

            if (MultiWindowUtils.isMultiInstanceApi31Enabled()) {
                boolean indicesMatch = assignedIndex == mWindowId;
                assert indicesMatch : message;
                if (!indicesMatch) {
                    Log.i(TAG_MULTI_INSTANCE, message);
                }
            }

            mMultiInstanceManager.initialize(assignedIndex, taskId);
        }

        mTabModelSelector = mTabModelOrchestrator.getTabModelSelector();
        mTabModelSelectorObserver =
                new TabModelSelectorObserver() {
                    @Override
                    public void onTabStateInitialized() {
                        if (mMultiInstanceManager != null) {
                            mMultiInstanceManager.onTabStateInitialized();
                        }

                        if (!mCreatedTabOnStartup) return;

                        TabModel model = mTabModelSelector.getModel(false);
                        TasksUma.recordTasksUma(model);
                    }
                };
        mTabModelSelector.addObserver(mTabModelSelectorObserver);

        mTabModelSelectorTabObserver =
                new TabModelSelectorTabObserver(mTabModelSelector) {
                    @Override
                    public void onDidFinishNavigationInPrimaryMainFrame(
                            Tab tab, NavigationHandle navigation) {
                        if (!navigation.hasCommitted()) return;

                        // Show the sync error message even if the navigation happened on incognito.
                        Profile profile = mTabModelProfileSupplier.get().getOriginalProfile();

                        try (TraceEvent e =
                                TraceEvent.scoped("CheckSyncErrorOnDidFinishNavigation")) {
                            SyncErrorMessage.maybeShowMessageUi(
                                    getWindowAndroid(),
                                    IdentityServicesProvider.get().getIdentityManager(profile),
                                    SyncServiceFactory.getForProfile(profile),
                                    UserPrefs.get(profile));
                        }
                        try (TraceEvent te = TraceEvent.scoped("updateActiveWebContents")) {
                            SendTabToSelfAndroidBridge.updateActiveWebContents(
                                    tab.getWebContents());
                        }
                    }
                };
        mAppIndexingUtil = new AppIndexingUtil(mTabModelSelector);

        if (startIncognito) mTabModelSelector.selectModel(true);
    }

    TabModelSelectorObserver getTabModelSelectorObserverForTesting() {
        return mTabModelSelectorObserver;
    }

    boolean getCreatedTabOnStartupForTesting() {
        return mCreatedTabOnStartup;
    }

    void setCreatedTabOnStartupForTesting(boolean createdTabOnStartup) {
        mCreatedTabOnStartup = createdTabOnStartup;
    }

    @Override
    protected LaunchCauseMetrics createLaunchCauseMetrics() {
        return new TabbedActivityLaunchCauseMetrics(this);
    }

    @Override
    public AppMenuPropertiesDelegate createAppMenuPropertiesDelegate() {
        return new TabbedAppMenuPropertiesDelegate(
                this,
                getActivityTabProvider(),
                getMultiWindowModeStateDispatcher(),
                getTabModelSelector(),
                getToolbarManager(),
                getWindow().getDecorView(),
                this,
                mLayoutStateProviderSupplier,
                ReturnToChromeUtil.isStartSurfaceEnabled(this) ? mStartSurfaceSupplier : null,
                mBookmarkModelSupplier,
                () ->
                        getTabCreator(/* incognito= */ false)
                                .launchUrl(
                                        NewTabPageUtils.encodeNtpUrl(
                                                NewTabPageLaunchOrigin.WEB_FEED),
                                        TabLaunchType.FROM_CHROME_UI),
                getModalDialogManager(),
                getSnackbarManager(),
                mRootUiCoordinator.getIncognitoReauthControllerSupplier(),
                mRootUiCoordinator.getReadAloudControllerSupplier());
    }

    private TabDelegateFactory getTabDelegateFactory() {
        if (mTabDelegateFactory == null) {
            mTabDelegateFactory =
                    new TabbedModeTabDelegateFactory(
                            this,
                            getAppBrowserControlsVisibilityDelegate(),
                            getShareDelegateSupplier(),
                            mRootUiCoordinator.getEphemeralTabCoordinatorSupplier(),
                            ((TabbedRootUiCoordinator) mRootUiCoordinator)::onContextMenuCopyLink,
                            mRootUiCoordinator.getBottomSheetController(),
                            /* chromeActivityNativeDelegate= */ this,
                            /* isCustomTab= */ false,
                            getBrowserControlsManager(),
                            getFullscreenManager(),
                            /* tabCreatorManager= */ this,
                            getTabModelSelectorSupplier(),
                            getCompositorViewHolderSupplier(),
                            getModalDialogManagerSupplier(),
                            this::getSnackbarManager,
                            getBrowserControlsManager(),
                            getActivityTabProvider(),
                            getLifecycleDispatcher(),
                            getWindowAndroid(),
                            mJankTracker,
                            getToolbarManager()::getToolbar,
                            mHomeSurfaceTracker,
                            getTabContentManagerSupplier(),
                            getToolbarManager().getTabStripHeightSupplier());
        }
        return mTabDelegateFactory;
    }

    @Override
    protected Pair<ChromeTabCreator, ChromeTabCreator> createTabCreators() {
        OverviewNtpCreator overviewNtpCreator = null;

        if (ReturnToChromeUtil.isStartSurfaceEnabled(this)) {
            overviewNtpCreator =
                    new OverviewNtpCreator() {
                        @Override
                        public boolean handleCreateNtpIfNeeded(
                                boolean isNtp,
                                boolean incognito,
                                Tab parentTab,
                                @NewTabPageLaunchOrigin int launchOrigin) {
                            boolean shouldShowStart =
                                    showStartSurfaceHomeForNtp(
                                            isNtp, incognito, parentTab, launchOrigin);
                            if (shouldShowStart) {
                                mStartSurfaceParentTabSupplier.set(parentTab);
                            }
                            return shouldShowStart;
                        }
                    };
        }
        return Pair.create(
                new ChromeTabCreator(
                        this,
                        getWindowAndroid(),
                        this::getTabDelegateFactory,
                        getProfileProviderSupplier(),
                        false,
                        overviewNtpCreator,
                        AsyncTabParamsManagerSingleton.getInstance(),
                        getTabModelSelectorSupplier(),
                        getCompositorViewHolderSupplier(),
                        mDseNewTabUrlManager),
                new ChromeTabCreator(
                        this,
                        getWindowAndroid(),
                        this::getTabDelegateFactory,
                        getProfileProviderSupplier(),
                        true,
                        overviewNtpCreator,
                        AsyncTabParamsManagerSingleton.getInstance(),
                        getTabModelSelectorSupplier(),
                        getCompositorViewHolderSupplier(),
                        null));
    }

    @Override
    protected void initDeferredStartupForActivity() {
        super.initDeferredStartupForActivity();
        DeferredStartupHandler.getInstance().addDeferredTask(this::onDeferredStartup);
    }

    private void onDeferredStartup() {
        if (isActivityFinishingOrDestroyed()) {
            return;
        }

        LauncherShortcutActivity.updateIncognitoShortcut(ChromeTabbedActivity.this);

        ChromeSurveyController.initialize(
                mTabModelSelector,
                getLifecycleDispatcher(),
                ChromeTabbedActivity.this,
                MessageDispatcherProvider.from(getWindowAndroid()),
                mTabModelProfileSupplier.get());

        if (mStartSurfaceSupplier.get() == null) {
            return;
        }
        // The start surface is not the layout shown on startup, so wait
        // until it is shown before notifying the start surface that is was.
        // TODO(1292661): We should allow the start surface to be the layout
        // that the browser starts on to avoid logic like this.
        // TODO(1315676): Clean up the check of LayoutType.TAB_SWITCHER once
        // the refactoring is done. This is because only Start surface is
        // allowed to shown on startup, not the Grid Tab switcher.
        boolean isStartSurfaceLayoutShown = false;
        var layoutManager = getLayoutManager();
        @LayoutType int activeType = layoutManager.getActiveLayoutType();
        @LayoutType int nextType = layoutManager.getNextLayoutType();
        if (isStartSurfaceRefactorEnabled()) {
            isStartSurfaceLayoutShown =
                    activeType == LayoutType.START_SURFACE || nextType != LayoutType.START_SURFACE;
        } else {
            isStartSurfaceLayoutShown =
                    activeType == LayoutType.TAB_SWITCHER || nextType != LayoutType.TAB_SWITCHER;
        }
        if (isStartSurfaceLayoutShown) {
            mStartSurfaceSupplier
                    .get()
                    .onOverviewShownAtLaunch(mOverviewShownOnStart, getOnCreateTimestampMs());
        } else if (nextType == LayoutType.TAB_SWITCHER || nextType == LayoutType.START_SURFACE) {
            layoutManager.addObserver(
                    new LayoutStateProvider.LayoutStateObserver() {
                        @Override
                        public void onStartedShowing(int layoutType) {
                            if (layoutType != LayoutType.TAB_SWITCHER
                                    && layoutType != LayoutType.START_SURFACE) {
                                return;
                            }

                            mStartSurfaceSupplier
                                    .get()
                                    .onOverviewShownAtLaunch(
                                            mOverviewShownOnStart, getOnCreateTimestampMs());

                            getLayoutManager().removeObserver(this);
                        }
                    });
        }
    }

    @Override
    protected void recordIntentToCreationTime(long timeMs) {
        super.recordIntentToCreationTime(timeMs);
        RecordHistogram.recordCustomTimesHistogram(
                "MobileStartup.IntentToCreationTime.TabbedMode",
                timeMs,
                1,
                DateUtils.SECOND_IN_MILLIS * 30,
                50);
    }

    @Override
    protected boolean isStartedUpCorrectly(Intent intent) {
        mWindowId = 0;
        Bundle savedInstanceState = getSavedInstanceState();
        int windowId = getExtraWindowIdFromIntent(intent);
        if (savedInstanceState != null && savedInstanceState.containsKey(WINDOW_INDEX)) {
            // Activity is recreated after destruction. |windowId| must not be valid in this case.
            assert windowId == INVALID_WINDOW_ID;
            Log.i(TAG_MULTI_INSTANCE, "Retrieved windowId from saved instance state.");
            mWindowId = savedInstanceState.getInt(WINDOW_INDEX, 0);
        } else if (mMultiInstanceManager != null) {
            // |allocInstanceId| doesn't do any disk I/O that would add a long-running task
            // to pre-inflation startup.
            boolean preferNew = getExtraPreferNewFromIntent(intent);
            Pair<Integer, Integer> instanceIdInfo =
                    mMultiInstanceManager.allocInstanceId(
                            windowId, ApplicationStatus.getTaskId(this), preferNew);
            mWindowId = instanceIdInfo.first;
            logIntentInfo(intent, instanceIdInfo);
            // If a new instance ID was allocated for the newly created activity, potentially
            // dispatch it to an existing activity under special circumstances. See
            // |#maybeDispatchIntentInExistingActivity(Intent)| for details.
            if (instanceIdInfo.second == InstanceAllocationType.NEW_INSTANCE_NEW_TASK
                    && maybeDispatchIntentInExistingActivity(intent)) {
                return false;
            }
        }
        if (mWindowId == INVALID_WINDOW_ID) {
            Log.i(TAG, "Window ID not allocated. Finishing the activity");
            Toast.makeText(this, R.string.max_number_of_windows, Toast.LENGTH_LONG).show();
            recordMaxWindowLimitExceededHistogram(/* limitExceeded= */ true);
            return false;
        } else {
            Map<String, Integer> taskMap =
                    ChromeSharedPreferences.getInstance()
                            .readIntsWithPrefix(ChromePreferenceKeys.MULTI_INSTANCE_TASK_MAP);
            Log.i(
                    TAG_MULTI_INSTANCE,
                    "Window ID allocated: " + mWindowId + ", instance-task map: " + taskMap);
        }

        if (mMultiInstanceManager != null
                && !mMultiInstanceManager.isStartedUpCorrectly(ApplicationStatus.getTaskId(this))) {
            return false;
        }
        recordMaxWindowLimitExceededHistogram(/* limitExceeded= */ false);

        return super.isStartedUpCorrectly(intent);
    }

    private void logIntentInfo(Intent intent, Pair<Integer, Integer> instanceIdInfo) {
        boolean isFromOs =
                getReferrer() != null
                        && getReferrer().toString().equals(SOURCE_ACTIVITY_REFERRER_OS);
        boolean isFromChrome = IntentHandler.wasIntentSenderChrome(intent);
        int windowId = instanceIdInfo.first;

        var logMessage =
                "Intent routed via ChromeLauncherActivity: "
                        + IntentUtils.safeGetBooleanExtra(
                                intent,
                                IntentHandler.EXTRA_LAUNCHED_VIA_CHROME_LAUNCHER_ACTIVITY,
                                false)
                        + "\nActivity referrer: "
                        + getReferrer()
                        + "\nIntent referrer extra: "
                        + IntentUtils.safeGetStringExtra(
                                intent, IntentHandler.EXTRA_ACTIVITY_REFERRER)
                        + "\nIntent contains LAUNCHER category: "
                        + intent.hasCategory(Intent.CATEGORY_LAUNCHER)
                        + "\nIntent contains FLAG_ACTIVITY_MULTIPLE_TASK: "
                        + ((intent.getFlags() & Intent.FLAG_ACTIVITY_MULTIPLE_TASK) != 0)
                        + "\nIntent contains FLAG_ACTIVITY_NEW_TASK: "
                        + ((intent.getFlags() & Intent.FLAG_ACTIVITY_NEW_TASK) != 0)
                        + "\nIntent component: "
                        + (intent.getComponent() == null
                                ? "N/A"
                                : intent.getComponent().getClassName())
                        + "\nIntent hash: "
                        + System.identityHashCode(intent);
        Log.i(TAG_MULTI_INSTANCE, logMessage);
        // Only crash-report if a valid window ID is allocated to launch the intent.
        if (windowId == INVALID_WINDOW_ID) return;

        // Report an exception iff all the following conditions are satisfied:
        // 1. At least one instance of Chrome already exists (that is, the newly created activity is
        // for an instance that is not the first).
        // 2. The intent will be launched in a new instance of Chrome.
        // 3. The device is a phone.
        // 4. The intent is a VIEW intent with a non-Chrome source, OR, a MAIN intent with a
        // non-Chrome / non-OS source.
        boolean isViewIntent = Intent.ACTION_VIEW.equals(intent.getAction()) && !isFromChrome;
        boolean isMainIntent =
                Intent.ACTION_MAIN.equals(intent.getAction()) && !isFromChrome && !isFromOs;

        if (MultiWindowUtils.getInstanceCount() >= 1
                && instanceIdInfo.second == InstanceAllocationType.NEW_INSTANCE_NEW_TASK
                && !DeviceFormFactor.isNonMultiDisplayContextOnTablet(this)) {
            if (isViewIntent) {
                logMessage =
                        "This is not a crash. Logging info for VIEW intent received in"
                                + " ChromeTabbedActivity dispatched via"
                                + " AsyncInitializationActivity#onCreate() that could potentially"
                                + " create a new Chrome instance.\n"
                                + logMessage;
                ChromePureJavaExceptionReporter.reportJavaException(
                        new Throwable(logMessage), /* withLogWarning= */ true);
            } else if (isMainIntent) {
                logMessage =
                        "This is not a crash. Logging info for MAIN intent received in"
                                + " ChromeTabbedActivity dispatched via"
                                + " AsyncInitializationActivity#onCreate() that could potentially"
                                + " create a new Chrome instance.\n"
                                + logMessage;
                ChromePureJavaExceptionReporter.reportJavaException(
                        new Throwable(logMessage), /* withLogWarning= */ true);
            }
        }
    }

    // It is possible that an undesired attempt is made to launch a VIEW intent in a new
    // ChromeTabbedActivity by an external app. When a new activity is created using such an intent,
    // we will try to launch it in an existing ChromeTabbedActivity instead, and finish the newly
    // created ChromeTabbedActivity.
    private boolean maybeDispatchIntentInExistingActivity(Intent intent) {
        if (!MultiWindowUtils.isMultiInstanceApi31Enabled()) {
            return false;
        }

        if (!ChromeFeatureList.sRedirectExplicitCTAIntentsToExistingActivity.isEnabled()) {
            return false;
        }

        // Check if the intent creating a new ChromeTabbedActivity was a VIEW intent launched via
        // ChromeLauncherActivity, in which case continue to launch in the current activity.
        boolean isExplicitViewIntent =
                Intent.ACTION_VIEW.equals(intent.getAction())
                        && !IntentUtils.safeGetBooleanExtra(
                                intent,
                                IntentHandler.EXTRA_LAUNCHED_VIA_CHROME_LAUNCHER_ACTIVITY,
                                false);
        if (!isExplicitViewIntent) return false;

        // If the intent sender is Chrome, continue to launch in the current activity.
        if (IntentHandler.wasIntentSenderChrome(intent)) {
            return false;
        }

        // Find an instance to launch the intent in.
        int instanceId = MultiWindowUtils.getInstanceIdForViewIntent(false);

        // If there is no running ChromeTabbedActivity, continue to launch in the current activity.
        if (instanceId == INVALID_WINDOW_ID) {
            RecordHistogram.recordBooleanHistogram(
                    HISTOGRAM_EXPLICIT_VIEW_INTENT_FINISHED_NEW_ACTIVITY, false);
            return false;
        }

        intent.putExtra(IntentHandler.EXTRA_WINDOW_ID, instanceId);
        MultiWindowUtils.launchIntentInInstance(intent, instanceId);
        RecordHistogram.recordBooleanHistogram(
                HISTOGRAM_EXPLICIT_VIEW_INTENT_FINISHED_NEW_ACTIVITY, true);
        return true;
    }

    private void recordMaxWindowLimitExceededHistogram(boolean limitExceeded) {
        RecordHistogram.recordBooleanHistogram(
                "Android.MultiInstance.MaxWindowLimitExceeded", limitExceeded);
    }

    private static int getExtraWindowIdFromIntent(Intent intent) {
        int windowId =
                IntentUtils.safeGetIntExtra(
                        intent, IntentHandler.EXTRA_WINDOW_ID, INVALID_WINDOW_ID);
        return IntentUtils.isTrustedIntentFromSelf(intent) ? windowId : INVALID_WINDOW_ID;
    }

    private static boolean getExtraPreferNewFromIntent(Intent intent) {
        return IntentUtils.safeGetBooleanExtra(intent, IntentHandler.EXTRA_PREFER_NEW, false);
    }

    @Override
    public void terminateIncognitoSession() {
        getTabModelSelector().getModel(true).closeAllTabs();
    }

    @Override
    public boolean onMenuOrKeyboardAction(final int id, boolean fromMenu) {
        final Tab currentTab = getActivityTab();
        boolean currentTabIsNtp = isTabNtp(currentTab);
        if (id == R.id.new_tab_menu_id) {
            if (!mTabModelSelector.isTabStateInitialized()) return false;

            getTabModelSelector().getModel(false).commitAllTabClosures();
            RecordUserAction.record("MobileMenuNewTab");
            RecordUserAction.record("MobileNewTabOpened");
            reportNewTabShortcutUsed(false);
            if (fromMenu) RecordUserAction.record("MobileMenuNewTab.AppMenu");

            getTabCreator(false).launchNtp();

            mLocaleManager.showSearchEnginePromoIfNeeded(this, null);
        } else if (id == R.id.new_incognito_tab_menu_id) {
            if (IncognitoUtils.isIncognitoModeEnabled()) {
                if (!mTabModelSelector.isTabStateInitialized()) return false;

                getTabModelSelector().getModel(false).commitAllTabClosures();
                // This action must be recorded before opening the incognito tab since UMA actions
                // are dropped when an incognito tab is open.
                RecordUserAction.record("MobileMenuNewIncognitoTab");
                RecordUserAction.record("MobileNewTabOpened");
                reportNewTabShortcutUsed(true);
                if (fromMenu) RecordUserAction.record("MobileMenuNewIncognitoTab.AppMenu");
                getTabCreator(true).launchNtp();
                Tracker tracker =
                        TrackerFactory.getTrackerForProfile(Profile.getLastUsedRegularProfile());
                tracker.notifyEvent(EventConstants.APP_MENU_NEW_INCOGNITO_TAB_CLICKED);
            }
        } else if (id == R.id.all_bookmarks_menu_id) {
            // Note that 'currentTab' could be null in overview mode when start surface is
            // enabled.
            getCompositorViewHolderSupplier()
                    .get()
                    .hideKeyboard(
                            () -> {
                                BookmarkUtils.showBookmarkManager(
                                        ChromeTabbedActivity.this,
                                        getCurrentTabModel().isIncognito());
                            });
            if (currentTabIsNtp) {
                NewTabPageUma.recordAction(NewTabPageUma.ACTION_OPENED_BOOKMARKS_MANAGER);
            }

            @BrowserProfileType
            int type =
                    getCurrentTabModel().isIncognito()
                            ? BrowserProfileType.INCOGNITO
                            : BrowserProfileType.REGULAR;
            RecordHistogram.recordEnumeratedHistogram(
                    "Bookmarks.OpenBookmarkManager.PerProfileType",
                    type,
                    BrowserProfileType.MAX_VALUE + 1);

            RecordUserAction.record("MobileMenuAllBookmarks");
        } else if (id == R.id.recent_tabs_menu_id) {
            LoadUrlParams params =
                    new LoadUrlParams(UrlConstants.RECENT_TABS_URL, PageTransition.AUTO_BOOKMARK);
            boolean isInOverviewMode = isInOverviewMode();
            if (isInOverviewMode && !isTablet() && ReturnToChromeUtil.isStartSurfaceEnabled(this)) {
                // When tapping the "Recent tabs" menu item from the overview page (Start surface or
                // GTS), we will create the tab with the launch type FROM_START_SURFACE. Thus, if
                // the back button is tapped on this "Recent tabs" page, it can go back to the
                // overview page.
                ReturnToChromeUtil.handleLoadUrlFromStartSurface(
                        params, getCurrentTabModel().isIncognito(), null);
            } else if (currentTab != null) {
                currentTab.loadUrl(params);
            } else {
                getTabCreator(getCurrentTabModel().isIncognito())
                        .createNewTab(params, TabLaunchType.FROM_CHROME_UI, null);
            }
            if (isInOverviewMode) {
                mLayoutManager.showLayout(LayoutType.BROWSING, true);
            }

            if (currentTabIsNtp) {
                NewTabPageUma.recordAction(NewTabPageUma.ACTION_OPENED_RECENT_TABS_MANAGER);
            }
            RecordUserAction.record("MobileMenuRecentTabs");
        } else if (id == R.id.close_tab) {
            getCurrentTabModel().closeTab(currentTab, true, false, true);
            RecordUserAction.record("MobileTabClosed");
        } else if (id == R.id.close_all_tabs_menu_id) {
            // Close both incognito and normal tabs.
            CloseAllTabsDialog.show(
                    this,
                    getModalDialogManagerSupplier(),
                    getTabModelSelectorSupplier().get(),
                    () -> getTabModelSelector().closeAllTabs());
            RecordUserAction.record("MobileMenuCloseAllTabs");
        } else if (id == R.id.close_all_incognito_tabs_menu_id) {
            // Close only incognito tabs
            CloseAllTabsDialog.show(
                    this,
                    getModalDialogManagerSupplier(),
                    getTabModelSelectorSupplier().get(),
                    () -> getTabModelSelector().getModel(true).closeAllTabs());
            RecordUserAction.record("MobileMenuCloseAllIncognitoTabs");
        } else if (id == R.id.focus_url_bar) {
            boolean isUrlBarVisible =
                    !isInOverviewMode() && (!isTablet() || getCurrentTabModel().getCount() != 0);
            if (isUrlBarVisible) {
                getToolbarManager()
                        .setUrlBarFocus(true, OmniboxFocusReason.MENU_OR_KEYBOARD_ACTION);
            }
        } else if (id == R.id.downloads_menu_id) {
            OTRProfileID otrProfileID = null;
            if (currentTab != null) {
                otrProfileID = currentTab.getProfile().getOTRProfileID();
            }
            DownloadUtils.showDownloadManager(
                    this, currentTab, otrProfileID, DownloadOpenSource.MENU);
            if (currentTabIsNtp) {
                NewTabPageUma.recordAction(NewTabPageUma.ACTION_OPENED_DOWNLOADS_MANAGER);
            }
            RecordUserAction.record("MobileMenuDownloadManager");
        } else if (id == R.id.open_recently_closed_tab) {
            TabModel currentModel = mTabModelSelector.getCurrentModel();
            if (!currentModel.isIncognito()) currentModel.openMostRecentlyClosedEntry();
            RecordUserAction.record("MobileTabClosedUndoShortCut");
        } else if (id == R.id.quick_delete_menu_id
                && QuickDeleteController.isQuickDeleteEnabled()) {
            assert !mTabModelSelector.getCurrentModel().isIncognito()
                    : "Quick delete is not supported in Incognito.";

            if (getLayoutManager().getActiveLayoutType() == LayoutType.TAB_SWITCHER) {
                QuickDeleteMetricsDelegate.recordHistogram(
                        QuickDeleteMetricsDelegate.QuickDeleteAction
                                .TAB_SWITCHER_MENU_ITEM_CLICKED);
            } else {
                QuickDeleteMetricsDelegate.recordHistogram(
                        QuickDeleteMetricsDelegate.QuickDeleteAction.MENU_ITEM_CLICKED);
            }

            new QuickDeleteController(
                    this,
                    new QuickDeleteDelegateImpl(),
                    getModalDialogManager(),
                    getSnackbarManager(),
                    getLayoutManager(),
                    mTabModelSelector);
        } else {
            return super.onMenuOrKeyboardAction(id, fromMenu);
        }
        return true;
    }

    private boolean isTabNtp(Tab tab) {
        return tab != null && UrlUtilities.isNtpUrl(tab.getUrl());
    }

    private boolean isTabRegularNtp(Tab tab) {
        return isTabNtp(tab) && !tab.isIncognito();
    }

    private void onOmniboxFocusChanged(boolean hasFocus) {
        getTabModalLifetimeHandler().onOmniboxFocusChanged(hasFocus);
    }

    private void recordLauncherShortcutAction(boolean isIncognito) {
        if (isIncognito) {
            RecordUserAction.record("Android.LauncherShortcut.NewIncognitoTab");
        } else {
            RecordUserAction.record("Android.LauncherShortcut.NewTab");
        }
    }

    @Override
    public boolean handleBackPressed() {
        // Back press event should be handled through the back press handler.
        assert !BackPressManager.isEnabled() : "Incorrect way of handling back press.";
        if (!mUIWithNativeInitialized) {
            RecordUserAction.record("SystemBackBeforeUINativeInitialized");
            return false;
        }

        if (getToolbarManager() != null && getToolbarManager().unfocusUrlBarOnBackPress()) {
            BackPressManager.record(BackPressHandler.Type.LOCATION_BAR);
            return true;
        }

        if (getTabModalLifetimeHandler().onBackPressed()) {
            BackPressManager.record(BackPressHandler.Type.TAB_MODAL_HANDLER);
            return true;
        }

        // crbug.com/1416719: back press on start surface should close the app.
        final boolean isStartSurfaceHomepageShowing =
                mStartSurfaceSupplier.hasValue() && mStartSurfaceSupplier.get().isHomepageShown();
        final Tab activityTab =
                BackPressManager.shouldUseActivityTabProvider()
                        ? getActivityTabProvider().get()
                        : getActivityTab();
        final Tab currentTab = isStartSurfaceHomepageShowing ? null : activityTab;
        // Handles the case of Start surface homepage is showing.
        if (currentTab == null) {
            minimizeAppAndCloseTabOnBackPress(null);
            return true;
        }

        final WebContents webContents = currentTab.getWebContents();
        if (webContents != null) {
            RenderFrameHost focusedFrame = webContents.getFocusedFrame();
            if (focusedFrame != null && focusedFrame.signalCloseWatcherIfActive()) {
                BackPressManager.record(BackPressHandler.Type.CLOSE_WATCHER);
                return true;
            }
        }

        if (getToolbarManager().back()) {
            BackPressManager.record(BackPressHandler.Type.TAB_HISTORY);
            return true;
        }

        // Handles the case of showing Tab switcher and will return to the Start surface.
        if (ReturnToChromeUtil.shouldHandleTabSwitcherShown(
                mIsHandleTabSwitcherShownEnabled, mLayoutStateProviderSupplier.get())) {
            // Passes true since Tab switcher is showing and we should handle this case to return to
            // the Start surface.
            returnToOverviewModeOnBackPressed(true);
            BackPressManager.record(BackPressHandler.Type.TAB_RETURN_TO_CHROME_START_SURFACE);
            return true;
        }

        // If we aren't in the overview mode, we handle the Tab that is opened from Start Surface.
        if (!isInOverviewMode()
                && !isTablet()
                && ReturnToChromeUtil.isTabFromStartSurface(currentTab)) {
            // Passes false since Tab switcher isn't showing.
            returnToOverviewModeOnBackPressed(false);
            BackPressManager.record(BackPressHandler.Type.TAB_RETURN_TO_CHROME_START_SURFACE);
            return true;
        }

        final @TabLaunchType int type = currentTab.getLaunchType();

        if (type == TabLaunchType.FROM_READING_LIST) {
            assert !isTablet() : "Not expecting to see FROM_READING_LIST on tablets";
            ReadingListUtils.showReadingList(currentTab.isIncognito());
            BackPressManager.record(BackPressHandler.Type.SHOW_READING_LIST);
            if (webContents != null) webContents.dispatchBeforeUnload(false);
            return true;
        }

        // At this point we know either the tab will close or the app will minimize.
        NativePage nativePage = currentTab.getNativePage();
        if (nativePage != null) {
            nativePage.notifyHidingWithBack();
        }

        if (minimizeAppAndCloseTabOnBackPress(currentTab)) return true;

        assert false : "The back button should have already been handled by this point";
        return false;
    }

    private boolean minimizeAppAndCloseTabOnBackPress(@Nullable Tab currentTab) {
        if (currentTab == null) {
            BackPressManager.record(BackPressHandler.Type.MINIMIZE_APP_AND_CLOSE_TAB);
            MinimizeAppAndCloseTabBackPressHandler.record(MinimizeAppAndCloseTabType.MINIMIZE_APP);
            assertOnLastBackPress();
            moveTaskToBack(true);
            return true;
        }
        // TAB history handler has a higher priority and should navigate page back before
        // minimizing app and closing tab.
        assert !currentTab.canGoBack()
                : "Tab should be navigated back before closing or exiting app";
        final boolean shouldCloseTab = backShouldCloseTab(currentTab);
        final WebContents webContents = currentTab.getWebContents();

        // Minimize the app if either:
        // - we decided not to close the tab
        // - we decided to close the tab, but it was opened by an external app, so we will go
        //   exit Chrome on top of closing the tab
        final boolean minimizeApp =
                !shouldCloseTab || TabAssociatedApp.isOpenedFromExternalApp(currentTab);

        BackPressManager.record(BackPressHandler.Type.MINIMIZE_APP_AND_CLOSE_TAB);
        assertOnLastBackPress();

        if (minimizeApp) {
            if (shouldCloseTab) {
                MinimizeAppAndCloseTabBackPressHandler.record(
                        MinimizeAppAndCloseTabType.MINIMIZE_APP_AND_CLOSE_TAB);
                sendToBackground(currentTab);
            } else {
                MinimizeAppAndCloseTabBackPressHandler.record(
                        MinimizeAppAndCloseTabType.MINIMIZE_APP);
                sendToBackground(null);
            }
            return true;
        } else if (shouldCloseTab) {
            MinimizeAppAndCloseTabBackPressHandler.record(MinimizeAppAndCloseTabType.CLOSE_TAB);
            if (webContents != null) webContents.dispatchBeforeUnload(false);
            return true;
        }
        return false;
    }

    private void assertOnLastBackPress() {
        final boolean isStartSurfaceHomepageShowing =
                mStartSurfaceSupplier.hasValue() && mStartSurfaceSupplier.get().isHomepageShown();
        final Tab currentTab = isStartSurfaceHomepageShowing ? null : getActivityTab();
        var activityTab = getActivityTabProvider().get();
        MinimizeAppAndCloseTabBackPressHandler.assertOnLastBackPress(
                currentTab,
                activityTab,
                this::backShouldCloseTab,
                mLayoutStateProviderSupplier,
                isActivityFinishingOrDestroyed());
    }

    private void initializeBackPressHandlers() {
        mBackPressManager.setHasSystemBackArm(true);
        if (mReturnToChromeBackPressHandler == null && !isTablet()) {
            mIsHandleTabSwitcherShownEnabled =
                    isStartSurfaceRefactorEnabled()
                            && ReturnToChromeUtil.isStartSurfaceEnabled(this);

            mReturnToChromeBackPressHandler =
                    new ReturnToChromeBackPressHandler(
                            getActivityTabProvider(),
                            this::returnToOverviewModeOnBackPressed,
                            this::getActivityTab,
                            mLayoutStateProviderSupplier,
                            mIsHandleTabSwitcherShownEnabled);
            mBackPressManager.addHandler(
                    mReturnToChromeBackPressHandler,
                    BackPressHandler.Type.TAB_RETURN_TO_CHROME_START_SURFACE);
        }
        if (mReadingListBackPressHandler == null && !isTablet()) {
            mReadingListBackPressHandler =
                    new ReadingListBackPressHandler(getActivityTabProvider());
            mBackPressManager.addHandler(
                    mReadingListBackPressHandler, BackPressHandler.Type.SHOW_READING_LIST);
        }
        if (mMinimizeAppAndCloseTabBackPressHandler == null) {
            mMinimizeAppAndCloseTabBackPressHandler =
                    new MinimizeAppAndCloseTabBackPressHandler(
                            getActivityTabProvider(),
                            this::backShouldCloseTab,
                            this::sendToBackground,
                            this::assertOnLastBackPress);
            mBackPressManager.addHandler(
                    mMinimizeAppAndCloseTabBackPressHandler,
                    BackPressHandler.Type.MINIMIZE_APP_AND_CLOSE_TAB);
        }
    }

    /**
     * [true]: Reached the bottom of the back stack on a tab the user did not explicitly
     * create (i.e. it was created by an external app or opening a link in background, etc).
     * [false]: Reached the bottom of the back stack on a tab that the user explicitly
     * created (e.g. selecting "new tab" from menu).
     *
     * @return Whether pressing the back button on the provided Tab should close the Tab.
     */
    @Override
    public boolean backShouldCloseTab(Tab tab) {
        if (!tab.isInitialized() || tab.isClosing() || tab.isDestroyed()) {
            return false;
        }
        @TabLaunchType int type = tab.getLaunchType();

        return type == TabLaunchType.FROM_LINK
                || type == TabLaunchType.FROM_EXTERNAL_APP
                || type == TabLaunchType.FROM_READING_LIST
                || type == TabLaunchType.FROM_LONGPRESS_FOREGROUND
                || type == TabLaunchType.FROM_LONGPRESS_INCOGNITO
                || type == TabLaunchType.FROM_LONGPRESS_BACKGROUND
                || type == TabLaunchType.FROM_LONGPRESS_BACKGROUND_IN_GROUP
                || type == TabLaunchType.FROM_RECENT_TABS
                || (type == TabLaunchType.FROM_RESTORE && tab.getParentId() != Tab.INVALID_TAB_ID);
    }

    /**
     * Sends this Activity to the background.
     *
     * @param tabToClose Tab that will be closed once the app is not visible.
     */
    private void sendToBackground(@Nullable final Tab tabToClose) {
        Log.i(TAG, "sendToBackground(): " + tabToClose);
        moveTaskToBack(true);
        if (tabToClose != null) {
            // In the case of closing a tab upon minimization, don't allow the close action to
            // happen until after our app is minimized to make sure we don't get a brief glimpse of
            // the newly active tab before we exit Chrome.
            //
            // If the runnable doesn't run before the Activity dies, Chrome won't crash but the tab
            // won't be closed (crbug.com/587565).
            mHandler.postDelayed(
                    () -> {
                        if (mTabModelSelector == null
                                || tabToClose.isClosing()
                                || tabToClose.isDestroyed()) {
                            return;
                        }

                        final TabModel currentModel = mTabModelSelector.getCurrentModel();
                        final TabModel tabToCloseModel =
                                mTabModelSelector.getModel(tabToClose.isIncognito());
                        if (currentModel != tabToCloseModel) {
                            // This seems improbable; however, crbug/1463397 suggests otherwise. If
                            // this happens, remain on the current tab and close the tab in the
                            // other model.
                            tabToCloseModel.closeTab(tabToClose, null, false, true, false);
                            return;
                        }

                        Tab nextTab =
                                currentModel.getNextTabIfClosed(
                                        tabToClose.getId(), /* uponExit= */ true);
                        currentModel.closeTab(tabToClose, nextTab, false, true, false);

                        // If there is no next tab to open, enter overview mode.
                        if (nextTab == null && !isActivityFinishingOrDestroyed()) {
                            showOverview(StartSurfaceState.SHOWING_START);
                        }
                    },
                    CLOSE_TAB_ON_MINIMIZE_DELAY_MS);
        }
    }

    @Override
    public boolean moveTaskToBack(boolean nonRoot) {
        try {
            return super.moveTaskToBack(nonRoot);
        } catch (NullPointerException e) {
            // Work around framework bug described in https://crbug.com/817567.
            finish();
            return true;
        }
    }

    /**
     * Launch a URL from an intent.
     * @param loadUrlParams Parameters specifying the url to load.
     * @param externalAppId External app id.
     * @param forceNewTab   Whether to force the URL to be launched in a new tab or to fall
     *                      back to the default behavior for making that determination.
     * @param intent        The original intent.
     */
    private Tab launchIntent(
            LoadUrlParams loadUrlParams, String externalAppId, boolean forceNewTab, Intent intent) {
        if (mUIWithNativeInitialized && !UrlUtilities.isNtpUrl(loadUrlParams.getUrl())) {
            getLayoutManager().showLayout(LayoutType.BROWSING, false);
            getToolbarManager().finishAnimations();
        }
        if (IntentHandler.wasIntentSenderChrome(intent)) {
            // If the intent was launched by chrome, open the new tab in the appropriate model.
            boolean isIncognito =
                    IntentUtils.safeGetBooleanExtra(
                            intent, IntentHandler.EXTRA_OPEN_NEW_INCOGNITO_TAB, false);
            @TabLaunchType Integer launchType = IntentHandler.getTabLaunchType(intent);
            if (launchType == null) {
                if (IntentUtils.safeGetBooleanExtra(
                        intent, IntentHandler.EXTRA_INVOKED_FROM_SHORTCUT, false)) {
                    launchType = TabLaunchType.FROM_LAUNCHER_SHORTCUT;
                } else if (IntentUtils.safeGetBooleanExtra(
                        intent, IntentHandler.EXTRA_INVOKED_FROM_APP_WIDGET, false)) {
                    launchType = TabLaunchType.FROM_APP_WIDGET;
                } else if (IncognitoTabLauncher.didCreateIntent(intent)) {
                    launchType = TabLaunchType.FROM_LAUNCH_NEW_INCOGNITO_TAB;
                } else {
                    // Using FROM_LINK ensures the tab is parented to the current tab, which allows
                    // the back button to close these tabs and restore selection to the previous
                    // tab.
                    launchType = TabLaunchType.FROM_LINK;
                }
            }

            ChromeTabCreator tabCreator = getTabCreator(isIncognito);
            Tab firstTab = tabCreator.createNewTab(loadUrlParams, launchType, null, intent);

            List<String> additionalUrls =
                    IntentUtils.safeGetSerializableExtra(
                            intent, IntentHandler.EXTRA_ADDITIONAL_URLS);
            boolean openAdditionalUrlsInTabGroup =
                    IntentUtils.safeGetBooleanExtra(
                            intent, IntentHandler.EXTRA_OPEN_ADDITIONAL_URLS_IN_TAB_GROUP, false);
            if (additionalUrls != null) {
                final Tab parent = openAdditionalUrlsInTabGroup ? firstTab : null;
                @TabLaunchType
                int additionalUrlLaunchType =
                        openAdditionalUrlsInTabGroup
                                ? TabLaunchType.FROM_LONGPRESS_BACKGROUND_IN_GROUP
                                : TabLaunchType.FROM_RESTORE;
                for (int i = 0; i < additionalUrls.size(); i++) {
                    String url = additionalUrls.get(i);
                    LoadUrlParams copy = LoadUrlParams.copy(loadUrlParams);
                    copy.setUrl(url);
                    tabCreator.createNewTab(copy, additionalUrlLaunchType, parent);
                }
            }
            return firstTab;
        }

        // Check if the tab is being created from a Reader Mode navigation.
        if (ReaderModeManager.isEnabled() && ReaderModeManager.isReaderModeCreatedIntent(intent)) {
            Bundle extras = intent.getExtras();
            int readerParentId =
                    IntentUtils.safeGetInt(
                            extras, ReaderModeManager.EXTRA_READER_MODE_PARENT, Tab.INVALID_TAB_ID);
            extras.remove(ReaderModeManager.EXTRA_READER_MODE_PARENT);
            // Set the parent tab to the tab that Reader Mode started from.
            if (readerParentId != Tab.INVALID_TAB_ID && mTabModelSelector != null) {
                return getCurrentTabCreator()
                        .createNewTab(
                                new LoadUrlParams(loadUrlParams.getUrl(), PageTransition.LINK),
                                TabLaunchType.FROM_LINK,
                                mTabModelSelector.getTabById(readerParentId));
            }
        }

        return getTabCreator(false)
                .launchUrlFromExternalApp(loadUrlParams, externalAppId, forceNewTab, intent);
    }

    // TODO(crbug.com/1315676): Once the refactor is finished, remove all StartSurfaceState codes.
    private void showOverview(@StartSurfaceState int state) {
        showOverview(state, NewTabPageLaunchOrigin.UNKNOWN);
    }

    // TODO(crbug.com/1115757): After crrev.com/c/2315823, Overview state and Startsurface state are
    // two different things, we actual can split this into two methods: showOverview() and
    // showStartSurface(state). Let's do some auditing and clean up before perform the actual split.
    private void showOverview(
            @StartSurfaceState int state, @NewTabPageLaunchOrigin int launchOrigin) {
        // TODO(crbug.com/1315676): Remove all StartSurfaceState relevant code.
        assert (state == StartSurfaceState.SHOWING_TABSWITCHER
                || state == StartSurfaceState.SHOWING_HOMEPAGE
                || state == StartSurfaceState.SHOWING_PREVIOUS
                || state == StartSurfaceState.SHOWING_START);
        if (mStartSurfaceSupplier.get() != null) {
            if (!isStartSurfaceRefactorEnabled()) {
                if (ReturnToChromeUtil.shouldHideStartSurfaceWithAccessibilityOn(this)
                        || !HomepageManager.isHomepageEnabled()) {
                    state = StartSurfaceState.SHOWING_TABSWITCHER;
                }
                mStartSurfaceSupplier.get().setStartSurfaceState(state, launchOrigin);
            } else {
                mStartSurfaceSupplier.get().setLaunchOrigin(launchOrigin);
            }
        }

        if (mLayoutManager == null) return;

        if (isInOverviewMode()) {
            if (didFinishNativeInitialization()) {
                getCompositorViewHolderSupplier().get().hideKeyboard(() -> {});
            }
            if (!isStartSurfaceRefactorEnabled()) return;
        }

        Tab currentTab = getActivityTab();
        @LayoutType int layoutTypeToShow = LayoutType.TAB_SWITCHER;
        if (isStartSurfaceRefactorEnabled()
                && ReturnToChromeUtil.isStartSurfaceEnabled(this)
                && state != StartSurfaceState.SHOWING_TABSWITCHER
                && !getTabModelSelector().isIncognitoSelected()) {
            // The NTP is attempting to show Start Surface after it was turned off previously due to
            // accessibility state change without a cold restart. If the supplier is null the
            // browser is in a weird state. Bail out to a normal NTP or last tab. This logic can be
            // removed once Start Surface with accessibility support is launched.
            // See crbug.com/1473947.
            if (mStartSurfaceSupplier.get() == null) {
                if (getTabModelSelector().getCurrentModel().getCount() == 0) {
                    getCurrentTabCreator().launchNtp();
                } else {
                    mLayoutManager.showLayout(LayoutType.BROWSING, true);
                }
                return;
            }

            layoutTypeToShow = LayoutType.START_SURFACE;
            if (state == StartSurfaceState.SHOWING_PREVIOUS) {
                ReturnToChromeUtil.recordBackNavigationToStart("FromTab");
            } else {
                // Resets the scroll position when Start is showing not via back operations.
                mStartSurfaceSupplier.get().resetScrollPosition();
            }
        }

        ReturnToChromeUtil.recordStartSurfaceState(state);

        // If we don't have a current tab, show the overview mode.
        if (currentTab == null) {
            mLayoutManager.showLayout(layoutTypeToShow, false);
        } else {
            final int layoutTypeToShowFinal = layoutTypeToShow;
            getCompositorViewHolderSupplier()
                    .get()
                    .hideKeyboard(() -> mLayoutManager.showLayout(layoutTypeToShowFinal, true));
        }
    }

    private void hideOverview() {
        assert (isInOverviewMode());
        if (getCurrentTabModel().getCount() != 0) {
            // Don't hide overview if current tab stack is empty()
            mLayoutManager.showLayout(LayoutType.BROWSING, false);
        }
    }

    /**
     * @return Whether opening a new tab is handled by the Start surface.
     */
    private boolean showStartSurfaceHomeForNtp(
            boolean isNtp,
            boolean incognito,
            Tab parentTab,
            @NewTabPageLaunchOrigin int launchOrigin) {
        if (!isNtp
                || !ReturnToChromeUtil.shouldShowStartSurfaceHomeAsNewTab(
                        this, incognito, isTablet())) {
            return false;
        }

        getTabModelSelector().selectModel(incognito);
        if (isInstantStartEnabled()
                || (getTabModelSelector().isTabStateInitialized() && isLayoutManagerCreated())) {
            showOverview(StartSurfaceState.SHOWING_HOMEPAGE, launchOrigin);
        }
        return true;
    }

    @Override
    public void onSaveInstanceState(Bundle outState) {
        try (TraceEvent e = TraceEvent.scoped("ChromeTabbedActivity.onSaveInstanceState")) {
            super.onSaveInstanceState(outState);
            CipherFactory.getInstance().saveToBundle(outState);
            outState.putInt(
                    WINDOW_INDEX, TabWindowManagerSingleton.getInstance().getIndexForWindow(this));
            Boolean is_incognito = getCurrentTabModel().isIncognito();
            outState.putBoolean(IS_INCOGNITO_SELECTED, is_incognito);
            // If it's Incognito and native is initialized and profile exists, serialize duration
            // service state.
            if (is_incognito && ProfileManager.isInitialized()) {
                AndroidSessionDurationsServiceState.serializeFromNative(
                        outState, getCurrentTabModel().getProfile());
            }
        }
    }

    @Override
    public void onDestroyInternal() {
        if (mReturnToChromeBackPressHandler != null) {
            mReturnToChromeBackPressHandler.destroy();
            mReturnToChromeBackPressHandler = null;
        }

        if (mReadingListBackPressHandler != null) {
            mReadingListBackPressHandler.destroy();
            mReadingListBackPressHandler = null;
        }
        if (mMinimizeAppAndCloseTabBackPressHandler != null) {
            mMinimizeAppAndCloseTabBackPressHandler.destroy();
            mMinimizeAppAndCloseTabBackPressHandler = null;
        }

        if (mCallbackController != null) {
            mCallbackController.destroy();
            mCallbackController = null;
        }

        if (mJankTracker != null) {
            mJankTracker.destroy();
            mJankTracker = null;
        }

        if (mTabModelSelectorTabObserver != null) {
            mTabModelSelectorTabObserver.destroy();
            mTabModelSelectorTabObserver = null;
        }

        if (mHistoricalTabModelObserver != null) mHistoricalTabModelObserver.destroy();

        if (mTabModelObserver != null) mTabModelObserver.destroy();

        if (mUndoBarPopupController != null) {
            mUndoBarPopupController.destroy();
            mUndoBarPopupController = null;
        }

        if (mAppIndexingUtil != null) {
            mAppIndexingUtil.destroy();
            mAppIndexingUtil = null;
        }

        if (mStartSurfaceSupplier.get() != null) {
            mStartSurfaceSupplier.get().destroy();
        }

        if (mStartupPaintPreviewHelperSupplier != null) {
            mStartupPaintPreviewHelperSupplier.destroy();
        }

        IncognitoTabHostRegistry.getInstance().unregister(mIncognitoTabHost);

        TabObscuringHandler tabObscuringHandler = getTabObscuringHandler();
        if (tabObscuringHandler != null) {
            getTabObscuringHandler().removeObserver(mCompositorViewHolder);
        }

        if (isTablet()) ChromeAccessibilityUtil.get().removeObserver(mCompositorViewHolder);
        ChromeAccessibilityUtil.get().removeObserver(mLayoutManager);

        if (mTabDelegateFactory != null) mTabDelegateFactory.destroy();

        mAppLaunchDrawBlocker.destroy();

        if (mAuxiliarySearchController != null) {
            mAuxiliarySearchController.destroy();
        }

        if (mDragDropDelegate != null) {
            mDragDropDelegate.destroy();
        }

        if (mExperimentalStartupMetricsTracker != null) {
            mExperimentalStartupMetricsTracker.destroy();
            mExperimentalStartupMetricsTracker = null;
        }

        if (mHubProvider != null) mHubProvider.destroy();

        super.onDestroyInternal();
    }

    @Override
    protected void destroyTabModels() {
        if (mTabModelOrchestrator != null) {
            mTabModelOrchestrator.destroy();
        }
    }

    @Override
    public void onTrimMemory(int level) {
        super.onTrimMemory(level);
        if (ChromeApplicationImpl.isSevereMemorySignal(level)) {
            NativePageAssassin.getInstance().freezeAllHiddenPages();
        }
    }

    @Override
    protected boolean supportsTabModalDialogs() {
        return true;
    }

    @Override
    public boolean dispatchKeyEvent(KeyEvent event) {
        Boolean result =
                KeyboardShortcuts.dispatchKeyEvent(
                        event,
                        mUIWithNativeInitialized,
                        getFullscreenManager(),
                        /* menuOrKeyboardActionController= */ this);
        return result != null ? result : super.dispatchKeyEvent(event);
    }

    @Override
    public boolean onKeyDown(int keyCode, KeyEvent event) {
        if (!mUIWithNativeInitialized) {
            return super.onKeyDown(keyCode, event);
        }
        // Detecting a long press of the back button via onLongPress is broken in Android N.
        // To work around this, use a postDelayed, which is supported in all versions.
        if (keyCode == KeyEvent.KEYCODE_BACK
                && !isTablet()
                && !getFullscreenManager().getPersistentFullscreenMode()) {
            if (mShowHistoryRunnable == null) mShowHistoryRunnable = this::showFullHistorySheet;
            mHandler.postDelayed(mShowHistoryRunnable, ViewConfiguration.getLongPressTimeout());
            return super.onKeyDown(keyCode, event);
        }
        boolean isCurrentTabVisible =
                !isInOverviewMode() && (!isTablet() || getCurrentTabModel().getCount() != 0);
        boolean keyboardShortcutHandled =
                KeyboardShortcuts.onKeyDown(
                        event,
                        isCurrentTabVisible,
                        true,
                        getTabModelSelector(),
                        /* menuOrKeyboardActionController= */ this,
                        getToolbarManager());
        if (keyboardShortcutHandled) {
            RecordUserAction.record("MobileKeyboardShortcutUsed");
        }
        return keyboardShortcutHandled || super.onKeyDown(keyCode, event);
    }

    private void showFullHistorySheet() {
        ((TabbedRootUiCoordinator) mRootUiCoordinator).showFullHistorySheet();
    }

    @Override
    public boolean onKeyUp(int keyCode, KeyEvent event) {
        if (keyCode == KeyEvent.KEYCODE_BACK && !isTablet()) {
            mHandler.removeCallbacks(mShowHistoryRunnable);
            mShowHistoryRunnable = null;
            if (event.getEventTime() - event.getDownTime()
                            >= ViewConfiguration.getLongPressTimeout()
                    && NavigationSheet.isInstanceShowing(
                            mRootUiCoordinator.getBottomSheetController())) {
                // If tab history popup is showing, do not process the keyUp event
                // which will dismiss it immediately.
                return true;
            }
        }
        return super.onKeyUp(keyCode, event);
    }

    public boolean hasPendingNavigationRunnableForTesting() {
        ThreadUtils.assertOnUiThread();
        return mShowHistoryRunnable != null;
    }

    @Override
    public void onProvideKeyboardShortcuts(
            List<KeyboardShortcutGroup> data, Menu menu, int deviceId) {
        data.addAll(KeyboardShortcuts.createShortcutGroup(this));
    }

    @VisibleForTesting
    public View getTabsView() {
        return getCompositorViewHolderSupplier().get();
    }

    @VisibleForTesting
    public LayoutManagerChrome getLayoutManager() {
        return (LayoutManagerChrome) getCompositorViewHolderSupplier().get().getLayoutManager();
    }

    @VisibleForTesting
    public OneshotSupplier<LayoutStateProvider> getLayoutStateProviderSupplier() {
        return mLayoutStateProviderSupplier;
    }

    @VisibleForTesting
    public StartSurface getStartSurface() {
        return mStartSurfaceSupplier.get();
    }

    public TabSwitcher getTabSwitcherForTesting() {
        return mTabSwitcherSupplier.get();
    }

    private ComposedBrowserControlsVisibilityDelegate getAppBrowserControlsVisibilityDelegate() {
        return mRootUiCoordinator.getAppBrowserControlsVisibilityDelegate();
    }

    // App Menu related code -----------------------------------------------------------------------

    @Override
    public boolean canShowAppMenu() {
        // The popup menu relies on the model created during the full UI initialization, so do not
        // attempt to show the menu until the UI creation has finished.
        if (!mUIWithNativeInitialized) return false;

        // If the current active tab is showing a tab modal dialog, an app menu shouldn't be shown
        // in any cases, e.g. when a hardware menu button is clicked.
        Tab tab = getActivityTab();
        if (tab != null && ChromeTabModalPresenter.isDialogShowing(tab)) return false;

        return super.canShowAppMenu();
    }

    @Override
    public boolean isInOverviewMode() {
        return mLayoutManager != null
                && (mLayoutManager.isLayoutVisible(LayoutType.TAB_SWITCHER)
                        || mLayoutManager.isLayoutVisible(LayoutType.START_SURFACE));
    }

    @Override
    public boolean shouldShowOverviewPageOnStart(Intent intent) {
        assert mInactivityTracker != null;
        assert getTabModelSelector() != null;
        return ReturnToChromeUtil.shouldShowOverviewPageOnStart(
                this, intent, getTabModelSelector(), mInactivityTracker, isTablet());
    }

    private boolean shouldShowOverviewPageOnStart() {
        return shouldShowOverviewPageOnStart(getIntent());
    }

    @Override
    public void onStart() {
        try (TraceEvent e = TraceEvent.scoped("ChromeTabbedActivity.onStart")) {
            super.onStart();
        }
    }

    @Override
    public void onStop() {
        try (TraceEvent e = TraceEvent.scoped("ChromeTabbedActivity.onStop")) {
            super.onStop();
        }
    }

    @Override
    public void onPause() {
        try (TraceEvent e = TraceEvent.scoped("ChromeTabbedActivity.onPause")) {
            super.onPause();
        }
    }

    @Override
    public void onResume() {
        try (TraceEvent e = TraceEvent.scoped("ChromeTabbedActivity.onResume")) {
            super.onResume();
        }
    }

    @Override
    public void onActivityResult(int requestCode, int resultCode, Intent data) {
        try (TraceEvent e = TraceEvent.scoped("ChromeTabbedActivity.onActivityResult")) {
            super.onActivityResult(requestCode, resultCode, data);
        }
    }

    @Override
    public void performOnConfigurationChanged(Configuration newConfig) {
        try (TraceEvent e =
                TraceEvent.scoped("ChromeTabbedActivity.performOnConfigurationChanged")) {
            super.performOnConfigurationChanged(newConfig);
        }
    }

    private void returnToOverviewModeOnBackPressed(boolean handleTabSwitcherShown) {
        // When TabSwitcherLayout is shown while TabSwitcherMediator doesn't handle the back
        // operation, it means it should return to the Start surface and is handled here.
        if (ReturnToChromeUtil.mayReturnToStartSurface(handleTabSwitcherShown, mLayoutManager)) {
            return;
        }

        Tab currentTab = getActivityTab();
        assert currentTab != null && !currentTab.canGoBack();

        // If current tab is an incognito one, we need to change tab model to non-incognito for
        // showing non-incognito start surface homepage.
        if (currentTab.isIncognito()) mTabModelSelector.selectModel(/* incognito= */ false);

        if (StartSurfaceUserData.getKeepTab(currentTab)
                || StartSurfaceUserData.isOpenedFromStart(currentTab)) {
            // If the current tab is created from the start surface with the keepTab property,
            // shows the Start surface non-incognito homepage to prevent a loop between the
            // current tab and previous overview mode. Once in the Start surface, it will close
            // Chrome if back button is tapped again.
            showOverview(StartSurfaceState.SHOWING_HOMEPAGE);
            ReturnToChromeUtil.recordBackNavigationToStart("FromTab");
        } else {
            // Otherwise, clicking the back button should go back to the previous overview mode.
            showOverview(StartSurfaceState.SHOWING_PREVIOUS);
        }

        if (currentTab.isClosing()) return;
        // If current tab is incognito, or it shouldn't be kept and it's not from restore, close
        // the tab.
        if (currentTab.isIncognito()
                || (!StartSurfaceUserData.getKeepTab(currentTab)
                        && currentTab.getLaunchType() != TabLaunchType.FROM_RESTORE)) {
            closeTabAfterStartSurfaceLayoutIsShown(currentTab);
        }
    }

    @Override
    public void onSceneChange(Layout layout) {
        super.onSceneChange(layout);
        if (!layout.shouldDisplayContentOverlay()) mTabModelSelector.onTabsViewShown();
    }

    /** Writes the tab state to disk. */
    @VisibleForTesting
    public void saveState() {
        mTabModelOrchestrator.saveState();

        // Save whether the current tab is a search result page into preferences.
        Tab currentStandardTab = TabModelUtils.getCurrentTab(mTabModelSelector.getModel(false));
        ChromeSharedPreferences.getInstance()
                .writeBoolean(
                        ChromePreferenceKeys.IS_LAST_VISITED_TAB_SRP,
                        currentStandardTab != null
                                && UrlUtilitiesJni.get()
                                        .isGoogleSearchUrl(currentStandardTab.getUrl().getSpec()));
    }

    @Override
    public void onEnterVr() {
        super.onEnterVr();
        mControlContainer.setVisibility(View.INVISIBLE);
        if (mVrBrowserControlsVisibilityDelegate == null) {
            mVrBrowserControlsVisibilityDelegate =
                    new BrowserControlsVisibilityDelegate(BrowserControlsState.BOTH);
            getAppBrowserControlsVisibilityDelegate()
                    .addDelegate(mVrBrowserControlsVisibilityDelegate);
        }
        mVrBrowserControlsVisibilityDelegate.set(BrowserControlsState.HIDDEN);
    }

    @Override
    public void onExitVr() {
        super.onExitVr();
        mControlContainer.setVisibility(View.VISIBLE);
        if (mVrBrowserControlsVisibilityDelegate != null) {
            mVrBrowserControlsVisibilityDelegate.set(BrowserControlsState.BOTH);
        }
    }

    /**
     * Reports that a new tab launcher shortcut was selected or an action equivalent to a shortcut
     * was performed.
     * @param isIncognito Whether the shortcut or action created a new incognito tab.
     */
    @RequiresApi(Build.VERSION_CODES.N_MR1)
    private void reportNewTabShortcutUsed(boolean isIncognito) {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.N_MR1) return;

        ShortcutManager shortcutManager = getSystemService(ShortcutManager.class);
        shortcutManager.reportShortcutUsed(
                isIncognito ? "new-incognito-tab-shortcut" : "new-tab-shortcut");
    }

    public MultiInstanceManager getMultiInstanceMangerForTesting() {
        return mMultiInstanceManager;
    }

    @VisibleForTesting
    public ChromeNextTabPolicySupplier getNextTabPolicySupplier() {
        return (ChromeNextTabPolicySupplier) mNextTabPolicySupplier;
    }

    private void closeTabAfterStartSurfaceLayoutIsShown(Tab currentTab) {
        // Closing tab should be called after StartSurfaceLayout finishes showing.
        getLayoutManager()
                .addObserver(
                        new LayoutStateProvider.LayoutStateObserver() {
                            @Override
                            public void onFinishedShowing(int layoutType) {
                                assert layoutType == LayoutType.TAB_SWITCHER
                                        || layoutType == LayoutType.START_SURFACE;
                                PostTask.postTask(
                                        TaskTraits.UI_DEFAULT,
                                        () -> {
                                            mTabModelSelector
                                                    .getModel(currentTab.isIncognito())
                                                    .closeTab(currentTab);
                                        });
                                getLayoutManager().removeObserver(this);
                            }
                        });
    }

    /** Returns whether the Start surface refactoring is enabled. */
    private boolean isStartSurfaceRefactorEnabled() {
        if (mIsStartSurfaceRefactorEnabled == null) {
            mIsStartSurfaceRefactorEnabled = ReturnToChromeUtil.isStartSurfaceRefactorEnabled(this);
        }
        return mIsStartSurfaceRefactorEnabled;
    }

    /** Returns whether to show a NTP as the home surface at startup on tablet in regular mode. */
    private boolean shouldShowNtpHomeSurfaceOnStartup() {
        if (mTabModelSelector.isIncognitoSelected()) return false;

        assert mInactivityTracker != null;
        return ReturnToChromeUtil.shouldShowNtpAsHomeSurfaceAtStartup(
                isTablet(),
                getIntent(),
                getSavedInstanceState(),
                mTabModelSelector,
                mInactivityTracker);
    }

    /**
     * Returns whether to skip saving non-active Ntps on tablet. Note: if a NTP is the last active
     * Tab, it won't be skipped.
     */
    private boolean skipSavingNonActiveNtps() {
        return StartSurfaceConfiguration.isNtpAsHomeSurfaceEnabled(isTablet());
    }

    public void showStartSurfaceForTesting() {
        showOverview(StartSurfaceState.SHOWING_START);
    }
}
