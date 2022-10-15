// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.feed;

import static org.chromium.components.browser_ui.widget.listmenu.BasicListMenu.buildMenuListItem;

import android.content.Context;
import android.content.Intent;
import android.content.res.Resources;
import android.os.Handler;
import android.os.SystemClock;
import android.view.View;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.annotation.VisibleForTesting;
import androidx.recyclerview.widget.LinearLayoutManager;
import androidx.recyclerview.widget.RecyclerView;

import org.chromium.base.Callback;
import org.chromium.base.MemoryPressureListener;
import org.chromium.base.ObserverList;
import org.chromium.base.memory.MemoryPressureCallback;
import org.chromium.chrome.R;
import org.chromium.chrome.browser.app.feed.feedmanagement.FeedManagementActivity;
import org.chromium.chrome.browser.feed.Stream.ContentChangedListener;
import org.chromium.chrome.browser.feed.sections.OnSectionHeaderSelectedListener;
import org.chromium.chrome.browser.feed.sections.SectionHeaderListProperties;
import org.chromium.chrome.browser.feed.sections.SectionHeaderProperties;
import org.chromium.chrome.browser.feed.sections.ViewVisibility;
import org.chromium.chrome.browser.feed.sort_ui.FeedOptionsCoordinator;
import org.chromium.chrome.browser.feed.v2.FeedUserActionType;
import org.chromium.chrome.browser.flags.ChromeFeatureList;
import org.chromium.chrome.browser.ntp.NewTabPageLaunchOrigin;
import org.chromium.chrome.browser.ntp.cards.SignInPromo;
import org.chromium.chrome.browser.preferences.Pref;
import org.chromium.chrome.browser.preferences.PrefChangeRegistrar;
import org.chromium.chrome.browser.profiles.Profile;
import org.chromium.chrome.browser.search_engines.TemplateUrlServiceFactory;
import org.chromium.chrome.browser.signin.services.IdentityServicesProvider;
import org.chromium.chrome.browser.signin.services.SigninManager;
import org.chromium.chrome.browser.suggestions.SuggestionsMetrics;
import org.chromium.chrome.browser.ui.native_page.TouchEnabledDelegate;
import org.chromium.chrome.browser.ui.signin.PersonalizedSigninPromoView;
import org.chromium.chrome.browser.ui.signin.SyncPromoController;
import org.chromium.chrome.browser.xsurface.FeedLaunchReliabilityLogger;
import org.chromium.chrome.browser.xsurface.FeedLaunchReliabilityLogger.StreamType;
import org.chromium.components.browser_ui.widget.listmenu.ListMenu;
import org.chromium.components.browser_ui.widget.listmenu.ListMenuItemProperties;
import org.chromium.components.feed.proto.wire.ReliabilityLoggingEnums.DiscoverLaunchResult;
import org.chromium.components.prefs.PrefService;
import org.chromium.components.search_engines.TemplateUrlService.TemplateUrlServiceObserver;
import org.chromium.components.signin.identitymanager.IdentityManager;
import org.chromium.components.signin.identitymanager.PrimaryAccountChangeEvent;
import org.chromium.components.signin.metrics.SigninAccessPoint;
import org.chromium.components.user_prefs.UserPrefs;
import org.chromium.content_public.browser.LoadUrlParams;
import org.chromium.ui.modelutil.MVCListAdapter;
import org.chromium.ui.modelutil.MVCListAdapter.ModelList;
import org.chromium.ui.modelutil.PropertyKey;
import org.chromium.ui.modelutil.PropertyListModel;
import org.chromium.ui.modelutil.PropertyModel;
import org.chromium.ui.mojom.WindowOpenDisposition;

import java.util.HashMap;
import java.util.Locale;

/**
 * A mediator for the {@link FeedSurfaceCoordinator} responsible for interacting with the
 * native library and handling business logic.
 */
@VisibleForTesting(otherwise = VisibleForTesting.PACKAGE_PRIVATE)
public class FeedSurfaceMediator
        implements FeedSurfaceScrollDelegate, TouchEnabledDelegate, TemplateUrlServiceObserver,
                   ListMenu.Delegate, IdentityManager.Observer {
    private static final String TAG = "FeedSurfaceMediator";
    private static final int INTEREST_FEED_HEADER_POSITION = 0;

    private class FeedSurfaceHeaderSelectedCallback implements OnSectionHeaderSelectedListener {
        @Override
        public void onSectionHeaderSelected(int index) {
            PropertyListModel<PropertyModel, PropertyKey> headerList =
                    mSectionHeaderModel.get(SectionHeaderListProperties.SECTION_HEADERS_KEY);
            mSectionHeaderModel.set(SectionHeaderListProperties.CURRENT_TAB_INDEX_KEY, index);

            // Proactively disable the unread content. Waiting for observers is too slow.
            headerList.get(index).set(SectionHeaderProperties.UNREAD_CONTENT_KEY, false);

            FeedFeatures.setLastSeenFeedTabId(index);

            Stream newStream = mTabToStreamMap.get(index);
            if (newStream.supportsOptions()) {
                headerList.get(index).set(SectionHeaderProperties.OPTIONS_INDICATOR_VISIBILITY_KEY,
                        ViewVisibility.VISIBLE);
            }
            if (!mSettingUpStreams) {
                logSwitchedFeeds(newStream);
                bindStream(newStream, /*shouldScrollToTop=*/true);
            }
        }

        @Override
        public void onSectionHeaderUnselected(int index) {
            PropertyListModel<PropertyModel, PropertyKey> headerList =
                    mSectionHeaderModel.get(SectionHeaderListProperties.SECTION_HEADERS_KEY);
            PropertyModel headerModel = headerList.get(index);
            if (mTabToStreamMap.get(index).supportsOptions()) {
                headerModel.set(SectionHeaderProperties.OPTIONS_INDICATOR_VISIBILITY_KEY,
                        ViewVisibility.INVISIBLE);
                headerModel.set(SectionHeaderProperties.OPTIONS_INDICATOR_IS_OPEN_KEY, false);
            }
            mOptionsCoordinator.ensureGone();
        }

        @Override
        public void onSectionHeaderReselected(int index) {
            Stream stream = mTabToStreamMap.get(index);
            if (!stream.supportsOptions()) return;

            PropertyListModel<PropertyModel, PropertyKey> headerList =
                    mSectionHeaderModel.get(SectionHeaderListProperties.SECTION_HEADERS_KEY);
            PropertyModel headerModel = headerList.get(index);
            headerModel.set(SectionHeaderProperties.OPTIONS_INDICATOR_IS_OPEN_KEY,
                    !headerModel.get(SectionHeaderProperties.OPTIONS_INDICATOR_IS_OPEN_KEY));
            // Reselected toggles the visibility of the options view.
            mOptionsCoordinator.toggleVisibility();
        }
    }

    /**
     * The {@link SignInPromo} for the Feed.
     * TODO(huayinz): Update content and visibility through a ModelChangeProcessor.
     */
    private class FeedSignInPromo extends SignInPromo {
        FeedSignInPromo(SigninManager signinManager) {
            super(signinManager);
            maybeUpdateSignInPromo();
        }

        @Override
        protected void setVisibilityInternal(boolean visible) {
            if (isVisible() == visible) return;

            super.setVisibilityInternal(visible);
            mCoordinator.updateHeaderViews(visible);
            maybeUpdateSignInPromo();
        }

        @Override
        protected void notifyDataChanged() {
            maybeUpdateSignInPromo();
        }

        /** Update the content displayed in {@link PersonalizedSigninPromoView}. */
        private void maybeUpdateSignInPromo() {
            // Only call #setupPromoViewFromCache() if SignInPromo is visible to avoid potentially
            // blocking the UI thread for several seconds if the accounts cache is not populated
            // yet.
            if (isVisible()) {
                mSyncPromoController.setUpSyncPromoView(mProfileDataCache,
                        mCoordinator.getSigninPromoView().findViewById(
                                R.id.signin_promo_view_container),
                        this::onDismissPromo);
            }
        }

        @Override
        public void onDismissPromo() {
            super.onDismissPromo();
            mCoordinator.updateHeaderViews(false);
        }
    }

    @VisibleForTesting(otherwise = VisibleForTesting.PACKAGE_PRIVATE)
    public static void setPrefForTest(
            PrefChangeRegistrar prefChangeRegistrar, PrefService prefService) {
        sTestPrefChangeRegistar = prefChangeRegistrar;
        sPrefServiceForTest = prefService;
    }

    private static PrefChangeRegistrar sTestPrefChangeRegistar;
    private static PrefService sPrefServiceForTest;

    private final FeedSurfaceCoordinator mCoordinator;
    private final Context mContext;
    private final @Nullable SnapScrollHelper mSnapScrollHelper;
    private final PrefChangeRegistrar mPrefChangeRegistrar;
    private final SigninManager mSigninManager;
    private final PropertyModel mSectionHeaderModel;
    private final FeedActionDelegate mActionDelegate;
    private final FeedOptionsCoordinator mOptionsCoordinator;

    private @Nullable RecyclerView.OnScrollListener mStreamScrollListener;
    private final ObserverList<ScrollListener> mScrollListeners = new ObserverList<>();
    private HasContentListener mHasContentListener;
    private ContentChangedListener mStreamContentChangedListener;
    private MemoryPressureCallback mMemoryPressureCallback;
    private @Nullable SignInPromo mSignInPromo;
    private RecyclerViewAnimationFinishDetector mRecyclerViewAnimationFinishDetector =
            new RecyclerViewAnimationFinishDetector();

    private boolean mFeedEnabled;
    private boolean mTouchEnabled = true;
    private boolean mStreamContentChanged;
    private int mThumbnailWidth;
    private int mThumbnailHeight;
    private int mThumbnailScrollY;
    private int mRestoreTabId;
    private int mHeaderCount;

    /** The model representing feed-related cog menu items. */
    private ModelList mFeedMenuModel;

    /** Whether the Feed content is loading. */
    private boolean mIsLoadingFeed;
    private FeedScrollState mRestoreScrollState;

    private final HashMap<Integer, Stream> mTabToStreamMap = new HashMap<>();
    private Stream mCurrentStream;
    // Whether we're currently adding the streams. If this is true, streams should not be bound yet.
    // This avoids automatically binding the first stream when it's added.
    private boolean mSettingUpStreams;

    /**
     * @param coordinator The {@link FeedSurfaceCoordinator} that interacts with this class.
     * @param context The current context.
     * @param snapScrollHelper The {@link SnapScrollHelper} that handles snap scrolling.
     * @param headerModel The {@link PropertyModel} that contains this mediator should work with.
     * @param openingTabId The {@link FeedSurfaceCoordinator.StreamTabId} the feed should open to.
     * @param optionsCoordinator The {@link FeedOptionsCoordinator} for the feed.
     */
    FeedSurfaceMediator(FeedSurfaceCoordinator coordinator, Context context,
            @Nullable SnapScrollHelper snapScrollHelper, PropertyModel headerModel,
            @FeedSurfaceCoordinator.StreamTabId int openingTabId, FeedActionDelegate actionDelegate,
            FeedOptionsCoordinator optionsCoordinator) {
        mCoordinator = coordinator;
        mHasContentListener = coordinator;
        mContext = context;
        mSnapScrollHelper = snapScrollHelper;
        mSigninManager = IdentityServicesProvider.get().getSigninManager(
                Profile.getLastUsedRegularProfile());
        mActionDelegate = actionDelegate;
        mOptionsCoordinator = optionsCoordinator;

        if (sTestPrefChangeRegistar != null) {
            mPrefChangeRegistrar = sTestPrefChangeRegistar;
        } else {
            mPrefChangeRegistrar = new PrefChangeRegistrar();
        }
        mPrefChangeRegistrar.addObserver(Pref.ENABLE_SNIPPETS, this::updateContent);

        if (openingTabId == FeedSurfaceCoordinator.StreamTabId.DEFAULT) {
            mRestoreTabId = FeedFeatures.getFeedTabIdToRestore();
        } else {
            mRestoreTabId = openingTabId;
        }

        mSectionHeaderModel = headerModel;
        // This works around the bug that the out-of-screen toolbar is not brought back together
        // with the new tab page view when it slides down. This is because the RecyclerView
        // animation may not finish when content changed event is triggered and thus the new tab
        // page layout view may still be partially off screen.
        mStreamContentChangedListener = contents
                -> mRecyclerViewAnimationFinishDetector.runWhenAnimationComplete(
                        this::onContentsChanged);

        initialize();
    }

    /** Clears any dependencies. */
    void destroy() {
        destroyPropertiesForStream();
        mPrefChangeRegistrar.destroy();
        TemplateUrlServiceFactory.get().removeObserver(this);
    }

    @VisibleForTesting
    public void destroyForTesting() {
        destroy();
    }

    private void initialize() {
        if (mSnapScrollHelper == null) return;

        // Listen for layout changes on the NewTabPageView itself to catch changes in scroll
        // position that are due to layout changes after e.g. device rotation. This contrasts with
        // regular scrolling, which is observed through an OnScrollListener.
        mCoordinator.getView().addOnLayoutChangeListener(
                (v, left, top, right, bottom, oldLeft, oldTop, oldRight, oldBottom) -> {
                    mSnapScrollHelper.handleScroll();
                });
    }

    /** Update the content based on supervised user or enterprise policy. */
    void updateContent() {
        mFeedEnabled = FeedFeatures.isFeedEnabled();
        if (mFeedEnabled && !mTabToStreamMap.isEmpty()) {
            return;
        }

        RecyclerView recyclerView = mCoordinator.getRecyclerView();
        if (mSnapScrollHelper != null && recyclerView != null) {
            mSnapScrollHelper.setView(recyclerView);
        }

        if (mFeedEnabled) {
            mIsLoadingFeed = true;
            mCoordinator.setupHeaders(/* feedEnabled= */ true);

            // Only set up stream if recycler view initiation did not fail.
            if (recyclerView != null) {
                initializePropertiesForStream();
            }
        } else {
            mCoordinator.setupHeaders(/* feedEnabled= */ false);
            destroyPropertiesForStream();
        }
    }

    /** Gets the current state, for restoring later. */
    String getSavedInstanceString() {
        FeedScrollState state = new FeedScrollState();
        int tabId = mSectionHeaderModel.get(SectionHeaderListProperties.CURRENT_TAB_INDEX_KEY);
        state.tabId = tabId;
        LinearLayoutManager layoutManager = null;
        if (mCoordinator.getRecyclerView() != null) {
            layoutManager = (LinearLayoutManager) mCoordinator.getRecyclerView().getLayoutManager();
        }
        if (layoutManager != null) {
            state.position = layoutManager.findFirstVisibleItemPosition();
            state.lastPosition = layoutManager.findLastVisibleItemPosition();
            if (state.position != RecyclerView.NO_POSITION) {
                View firstVisibleView = layoutManager.findViewByPosition(state.position);
                if (firstVisibleView != null) {
                    state.offset = firstVisibleView.getTop();
                }
            }
            if (mCurrentStream != null) {
                state.feedContentState = mCurrentStream.getContentState();
            }
        }
        return state.toJson();
    }

    /** Restores a previously saved state. */
    void restoreSavedInstanceState(String json) {
        FeedScrollState state = FeedScrollState.fromJson(json);
        if (state == null) return;
        mRestoreTabId = state.tabId;
        if (mSectionHeaderModel != null) {
            mSectionHeaderModel.set(SectionHeaderListProperties.CURRENT_TAB_INDEX_KEY, state.tabId);
        }
        if (mCurrentStream == null) {
            mRestoreScrollState = state;
        } else {
            mCurrentStream.restoreSavedInstanceState(state);
        }
    }

    /**
     * Sets the current tab to {@code tabId}.
     *
     * <p>Called when the the mediator is already initialized in Start Surface, but the feed is
     * being shown again with a different {@link NewTabPageLaunchOrigin}.
     */
    void setTabId(@FeedSurfaceCoordinator.StreamTabId int tabId) {
        if (tabId == FeedSurfaceCoordinator.StreamTabId.DEFAULT) {
            tabId = FeedFeatures.getFeedTabIdToRestore();
        }
        if (mTabToStreamMap.size() <= tabId) tabId = 0;
        mSectionHeaderModel.set(SectionHeaderListProperties.CURRENT_TAB_INDEX_KEY, tabId);
    }

    /**
     * Initialize properties for UI components in the {@link NewTabPage}.
     * TODO(huayinz): Introduce a Model for these properties.
     */
    private void initializePropertiesForStream() {
        assert !mSettingUpStreams;
        mSettingUpStreams = true;
        mSectionHeaderModel.set(SectionHeaderListProperties.ON_TAB_SELECTED_CALLBACK_KEY,
                new FeedSurfaceHeaderSelectedCallback());

        mPrefChangeRegistrar.addObserver(Pref.ARTICLES_LIST_VISIBLE, this::updateSectionHeader);
        TemplateUrlServiceFactory.get().addObserver(this);

        boolean suggestionsVisible = isSuggestionsVisible();

        addHeaderAndStream(getInterestFeedHeaderText(suggestionsVisible),
                mCoordinator.createFeedStream(StreamKind.FOR_YOU));
        setHeaderIndicatorState(suggestionsVisible);

        // Build menu after section enabled key is set.
        mFeedMenuModel = buildMenuItems();

        mCoordinator.initializeBubbleTriggering();
        mSigninManager.getIdentityManager().addObserver(this);

        mSectionHeaderModel.set(SectionHeaderListProperties.MENU_MODEL_LIST_KEY, mFeedMenuModel);
        mSectionHeaderModel.set(
                SectionHeaderListProperties.MENU_DELEGATE_KEY, this::onItemSelected);

        setUpWebFeedTab();

        // Set the current tab index to what restoreSavedInstanceState had.
        if (mTabToStreamMap.size() <= mRestoreTabId) mRestoreTabId = 0;
        mSectionHeaderModel.set(SectionHeaderListProperties.CURRENT_TAB_INDEX_KEY, mRestoreTabId);
        mSettingUpStreams = false;

        if (mSectionHeaderModel.get(SectionHeaderListProperties.IS_SECTION_ENABLED_KEY)) {
            bindStream(mTabToStreamMap.get(mSectionHeaderModel.get(
                               SectionHeaderListProperties.CURRENT_TAB_INDEX_KEY)),
                    /*shouldScrollToTop=*/false);
        } else {
            unbindStream();
        }

        mStreamScrollListener = new RecyclerView.OnScrollListener() {
            @Override
            public void onScrollStateChanged(@NonNull RecyclerView recyclerView, int newState) {
                for (ScrollListener listener : mScrollListeners) {
                    listener.onScrollStateChanged(newState);
                }
            }

            @Override
            public void onScrolled(RecyclerView v, int dx, int dy) {
                if (mSnapScrollHelper != null) {
                    mSnapScrollHelper.handleScroll();
                }
                for (ScrollListener listener : mScrollListeners) {
                    listener.onScrolled(dx, dy);
                }
            }
        };
        mCoordinator.getRecyclerView().addOnScrollListener(mStreamScrollListener);

        initStreamHeaderViews();

        mMemoryPressureCallback =
                pressure -> mCoordinator.getRecyclerView().getRecycledViewPool().clear();
        MemoryPressureListener.addCallback(mMemoryPressureCallback);
    }

    void addScrollListener(ScrollListener listener) {
        mScrollListeners.addObserver(listener);
    }

    void removeScrollListener(ScrollListener listener) {
        mScrollListeners.removeObserver(listener);
    }

    private void addHeaderAndStream(String headerText, Stream stream) {
        int tabId = mSectionHeaderModel.get(SectionHeaderListProperties.SECTION_HEADERS_KEY).size();
        mTabToStreamMap.put(tabId, stream);

        PropertyModel headerModel = SectionHeaderProperties.createSectionHeader(headerText);
        ViewVisibility indicatorVisibility;
        // Keeping the indicator in place for the "Following" header, so it allows a fixed width of
        // the "Following" header.
        if (stream.supportsOptions() || stream.getStreamKind() == StreamKind.FOLLOWING) {
            indicatorVisibility = ViewVisibility.INVISIBLE;
        } else {
            indicatorVisibility = ViewVisibility.GONE;
        }
        headerModel.set(
                SectionHeaderProperties.OPTIONS_INDICATOR_VISIBILITY_KEY, indicatorVisibility);
        headerModel.set(SectionHeaderProperties.OPTIONS_INDICATOR_IS_OPEN_KEY, false);
        mSectionHeaderModel.get(SectionHeaderListProperties.SECTION_HEADERS_KEY).add(headerModel);

        // Update UNREAD_CONTENT_KEY and HEADER_ACCESSIBILITY_TEXT_KEY now, and any time
        // hasUnreadContent() changes.
        Callback<Boolean> callback = hasUnreadContent -> {
            headerModel.set(SectionHeaderProperties.UNREAD_CONTENT_KEY, hasUnreadContent);
            mHasContentListener.hasContentChanged(stream.getStreamKind(), hasUnreadContent);
        };
        callback.onResult(stream.hasUnreadContent().addObserver(callback));
    }

    private int getTabIdForSection(@StreamKind int streamKind) {
        for (int tabId : mTabToStreamMap.keySet()) {
            if (mTabToStreamMap.get(tabId).getStreamKind() == streamKind) {
                return tabId;
            }
        }
        return -1;
    }

    /** Adds WebFeed tab if we need it. */
    private void setUpWebFeedTab() {
        // Skip if the for-you tab hasn't been added yet.
        if (getTabIdForSection(StreamKind.FOR_YOU) == -1) {
            return;
        }
        int tabId = getTabIdForSection(StreamKind.FOLLOWING);
        boolean hasWebFeedTab = tabId != -1;
        boolean shouldHaveWebFeedTab = FeedFeatures.isWebFeedUIEnabled();
        if (hasWebFeedTab == shouldHaveWebFeedTab) return;
        if (shouldHaveWebFeedTab) {
            addHeaderAndStream(mContext.getResources().getString(R.string.ntp_following),
                    mCoordinator.createFeedStream(StreamKind.FOLLOWING));
        }
    }

    /**
     * Binds a stream to the {@link NtpListContentManager}. Unbinds currently active stream if
     * different from new stream. Once bound, the stream can add/remove contents.
     */
    @VisibleForTesting
    void bindStream(Stream stream, boolean shouldScrollToTop) {
        if (mCurrentStream == stream) return;
        if (mCurrentStream != null) {
            unbindStream(/* shouldPlaceSpacer = */ true);
        }
        // Don't bind before the coordinator is active, or when the feed should not show.
        if (!mCoordinator.isActive()
                || !mSectionHeaderModel.get(SectionHeaderListProperties.IS_SECTION_ENABLED_KEY)) {
            return;
        }
        mCurrentStream = stream;
        mCurrentStream.addOnContentChangedListener(mStreamContentChangedListener);

        if (FeedFeatures.isAutoScrollToTopEnabled() && mRestoreScrollState == null) {
            mRestoreScrollState = getScrollStateForAutoScrollToTop();
        }

        FeedReliabilityLogger reliabilityLogger = mCoordinator.getReliabilityLogger();
        mCurrentStream.bind(mCoordinator.getRecyclerView(), mCoordinator.getContentManager(),
                mRestoreScrollState, mCoordinator.getSurfaceScope(),
                mCoordinator.getHybridListRenderer(),
                reliabilityLogger != null ? reliabilityLogger.getLaunchLogger()
                                          : new FeedLaunchReliabilityLogger() {},
                mHeaderCount, shouldScrollToTop);
        mRestoreScrollState = null;
        mCoordinator.getHybridListRenderer().onSurfaceOpened();
    }

    void onContentsChanged() {
        if (mSnapScrollHelper != null) mSnapScrollHelper.resetSearchBoxOnScroll(true);

        mActionDelegate.onContentsChanged();

        mIsLoadingFeed = false;
        mStreamContentChanged = true;
    }

    public boolean isLoadingFeed() {
        return mIsLoadingFeed;
    }

    /** Unbinds the stream and clear all the stream's contents. */
    private void unbindStream() {
        unbindStream(false);
    }

    /** Unbinds the stream with option for stream to put a placeholder for its contents. */
    private void unbindStream(boolean shouldPlaceSpacer) {
        if (mCurrentStream == null) return;
        mCoordinator.getHybridListRenderer().onSurfaceClosed();
        mCurrentStream.unbind(shouldPlaceSpacer);
        mCurrentStream.removeOnContentChangedListener(mStreamContentChangedListener);
        mCurrentStream = null;

        // This is the catch-all feed launch end event to ensure a complete flow is logged
        // even if we don't know a more specific reason for the stream unbinding.
        FeedReliabilityLogger reliabilityLogger = mCoordinator.getReliabilityLogger();
        if (reliabilityLogger != null) {
            reliabilityLogger.logLaunchFinishedIfInProgress(
                    DiscoverLaunchResult.FRAGMENT_STOPPED, /*userMightComeBack=*/false);
        }
    }

    void onSurfaceOpened() {
        rebindStream();
    }

    void onSurfaceClosed() {
        unbindStream();
    }

    /** @return The stream that represents the 1st tab. */
    boolean hasStreams() {
        return !mTabToStreamMap.isEmpty();
    }

    boolean isActivityLoggingEnabledForCurrentStream() {
        if (mCurrentStream == null) return false;
        return mCurrentStream.isActivityLoggingEnabled();
    }

    long getLastFetchTimeMsForCurrentStream() {
        if (mCurrentStream == null) return 0;
        return mCurrentStream.getLastFetchTimeMs();
    }

    boolean isPlaceholderShown() {
        return mCurrentStream == null ? false : mCurrentStream.isPlaceholderShown();
    }

    @VisibleForTesting
    Stream getCurrentStreamForTesting() {
        return mCurrentStream;
    }

    private void rebindStream() {
        // If a stream is already bound, then do nothing.
        if (mCurrentStream != null) return;
        // Find the stream that should be bound and bind it. If no stream matches, then we haven't
        // fully set up yet. This will be taken care of by setup.
        Stream stream = mTabToStreamMap.get(
                mSectionHeaderModel.get(SectionHeaderListProperties.CURRENT_TAB_INDEX_KEY));
        if (stream != null) {
            bindStream(stream, /*shouldScrollToTop=*/false);
        }
    }

    /**
     * Notifies a bound stream of new header count number.
     * @param newHeaderCount Number of headers in the {@link RecyclerView}.
     */
    void notifyHeadersChanged(int newHeaderCount) {
        mHeaderCount = newHeaderCount;
        if (mCurrentStream != null) {
            mCurrentStream.notifyNewHeaderCount(newHeaderCount);
        }
    }

    private void initStreamHeaderViews() {
        boolean signInPromoVisible = shouldShowSigninPromo();
        mCoordinator.updateHeaderViews(signInPromoVisible);
    }

    /**
     * Determines whether a signin promo should be shown.
     * @return Whether the SignPromo should be visible.
     */
    private boolean shouldShowSigninPromo() {
        SyncPromoController.resetNTPSyncPromoLimitsIfHiddenForTooLong();
        if (!SignInPromo.shouldCreatePromo()
                || !SyncPromoController.canShowSyncPromo(
                        SigninAccessPoint.NTP_CONTENT_SUGGESTIONS)) {
            return false;
        }
        if (mSignInPromo == null) {
            mSignInPromo = new FeedSignInPromo(mSigninManager);
            mSignInPromo.setCanShowPersonalizedSuggestions(isSuggestionsVisible());
        }
        return mSignInPromo.isVisible();
    }

    /** Clear any dependencies related to the {@link Stream}. */
    private void destroyPropertiesForStream() {
        if (mTabToStreamMap.isEmpty()) return;

        if (mStreamScrollListener != null) {
            mCoordinator.getRecyclerView().removeOnScrollListener(mStreamScrollListener);
            mStreamScrollListener = null;
        }

        MemoryPressureListener.removeCallback(mMemoryPressureCallback);
        mMemoryPressureCallback = null;

        if (mSignInPromo != null) {
            mSignInPromo.destroy();
            mSignInPromo = null;
        }

        unbindStream();
        for (Stream s : mTabToStreamMap.values()) {
            s.removeOnContentChangedListener(mStreamContentChangedListener);
            s.destroy();
        }
        mTabToStreamMap.clear();
        mStreamContentChangedListener = null;

        mPrefChangeRegistrar.removeObserver(Pref.ARTICLES_LIST_VISIBLE);
        TemplateUrlServiceFactory.get().removeObserver(this);
        mSigninManager.getIdentityManager().removeObserver(this);

        mSectionHeaderModel.get(SectionHeaderListProperties.SECTION_HEADERS_KEY).clear();

        if (mCoordinator.getSurfaceScope() != null) {
            mCoordinator.getSurfaceScope().getFeedLaunchReliabilityLogger().cancelPendingEvents();
        }
    }

    private void setHeaderIndicatorState(boolean suggestionsVisible) {
        boolean isSignedIn = FeedServiceBridge.isSignedIn();
        boolean isTabMode = isSignedIn && FeedFeatures.isWebFeedUIEnabled() && suggestionsVisible;
        // If we're in tab mode now, make sure webfeed tab is set up.
        if (isTabMode) {
            setUpWebFeedTab();
        }
        mSectionHeaderModel.set(SectionHeaderListProperties.IS_TAB_MODE_KEY, isTabMode);

        // If not in tab mode, make sure we are on the for-you feed.
        if (!isTabMode) {
            mSectionHeaderModel.set(SectionHeaderListProperties.CURRENT_TAB_INDEX_KEY,
                    getTabIdForSection(StreamKind.FOR_YOU));
        }

        boolean isGoogleSearchEngine =
                TemplateUrlServiceFactory.get().isDefaultSearchEngineGoogle();
        // When Google is not the default search engine, we need to show the Logo.
        mSectionHeaderModel.set(SectionHeaderListProperties.IS_LOGO_KEY,
                !isGoogleSearchEngine && isSignedIn && suggestionsVisible);
        ViewVisibility indicatorState;
        if (!isSignedIn || !suggestionsVisible) {
            // Gone when not signed in or feed off to align text to far left.
            indicatorState = ViewVisibility.GONE;
        } else if (!isGoogleSearchEngine) {
            // Visible when Google is not the search engine (show logo).
            indicatorState = ViewVisibility.VISIBLE;
        } else {
            // Invisible when we have centered text (signed in and not shown). This
            // counterbalances the gear icon so text is properly centered.
            indicatorState = ViewVisibility.INVISIBLE;
        }
        mSectionHeaderModel.set(
                SectionHeaderListProperties.INDICATOR_VIEW_VISIBILITY_KEY, indicatorState);

        // Make sure to collapse option panel if not shown.
        if (!suggestionsVisible) {
            mOptionsCoordinator.ensureGone();
        }

        // Set enabled last because it makes the animation smoother.
        mSectionHeaderModel.set(
                SectionHeaderListProperties.IS_SECTION_ENABLED_KEY, suggestionsVisible);
    }

    /**
     * Update whether the section header should be expanded.
     *
     * Called when a settings change or update to this/another NTP caused the feed to show/hide.
     */
    void updateSectionHeader() {
        boolean suggestionsVisible = isSuggestionsVisible();
        mSectionHeaderModel.get(SectionHeaderListProperties.SECTION_HEADERS_KEY)
                .get(INTEREST_FEED_HEADER_POSITION)
                .set(SectionHeaderProperties.HEADER_TEXT_KEY,
                        getInterestFeedHeaderText(suggestionsVisible));

        setHeaderIndicatorState(suggestionsVisible);

        // Update toggleswitch item, which is last item in list.
        mSectionHeaderModel.set(SectionHeaderListProperties.MENU_MODEL_LIST_KEY, buildMenuItems());

        if (mSignInPromo != null) {
            mSignInPromo.setCanShowPersonalizedSuggestions(suggestionsVisible);
        }
        if (suggestionsVisible) mCoordinator.getSurfaceLifecycleManager().show();
        mStreamContentChanged = true;

        PropertyModel currentStreamHeaderModel =
                mSectionHeaderModel.get(SectionHeaderListProperties.SECTION_HEADERS_KEY)
                        .get(mSectionHeaderModel.get(
                                SectionHeaderListProperties.CURRENT_TAB_INDEX_KEY));
        Stream currentStream = mTabToStreamMap.get(
                mSectionHeaderModel.get(SectionHeaderListProperties.CURRENT_TAB_INDEX_KEY));

        // If feed turned on, we bind the last stream that was visible. Else unbind it.
        if (suggestionsVisible) {
            if (currentStream.supportsOptions()) {
                currentStreamHeaderModel.set(
                        SectionHeaderProperties.OPTIONS_INDICATOR_VISIBILITY_KEY,
                        ViewVisibility.VISIBLE);
            }
            rebindStream();
        } else {
            if (currentStream.supportsOptions()) {
                currentStreamHeaderModel.set(
                        SectionHeaderProperties.OPTIONS_INDICATOR_VISIBILITY_KEY,
                        ViewVisibility.INVISIBLE);
                currentStreamHeaderModel.set(
                        SectionHeaderProperties.OPTIONS_INDICATOR_IS_OPEN_KEY, false);
            }
            unbindStream();
        }
    }

    /**
     * Callback on section header toggled. This will update the visibility of the Feed and the
     * expand icon on the section header view.
     */
    private void onSectionHeaderToggled() {
        boolean isExpanded =
                !mSectionHeaderModel.get(SectionHeaderListProperties.IS_SECTION_ENABLED_KEY);

        // Record in prefs and UMA.
        // Model and stream visibility set in {@link #updateSectionHeader}
        // which is called by the prefService observer.
        getPrefService().setBoolean(Pref.ARTICLES_LIST_VISIBLE, isExpanded);
        FeedUma.recordFeedControlsAction(FeedUma.CONTROLS_ACTION_TOGGLED_FEED);
        SuggestionsMetrics.recordArticlesListVisible();

        int streamType = mTabToStreamMap
                                 .get(mSectionHeaderModel.get(
                                         SectionHeaderListProperties.CURRENT_TAB_INDEX_KEY))
                                 .getStreamKind();
        FeedServiceBridge.reportOtherUserAction(streamType,
                isExpanded ? FeedUserActionType.TAPPED_TURN_ON
                           : FeedUserActionType.TAPPED_TURN_OFF);
    }

    /** Returns the interest feed header text based on the selected default search engine */
    private String getInterestFeedHeaderText(boolean isExpanded) {
        Resources res = mContext.getResources();
        final boolean isDefaultSearchEngineGoogle =
                TemplateUrlServiceFactory.get().isDefaultSearchEngineGoogle();
        final int sectionHeaderStringId;

        if (ChromeFeatureList.isEnabled(ChromeFeatureList.WEB_FEED)
                && FeedServiceBridge.isSignedIn() && isExpanded) {
            sectionHeaderStringId = R.string.ntp_discover_on;
        } else if (isDefaultSearchEngineGoogle) {
            sectionHeaderStringId =
                    isExpanded ? R.string.ntp_discover_on : R.string.ntp_discover_off;
        } else {
            sectionHeaderStringId = isExpanded ? R.string.ntp_discover_on_branded
                                               : R.string.ntp_discover_off_branded;
        }

        return res.getString(sectionHeaderStringId);
    }

    private ModelList buildMenuItems() {
        ModelList itemList = new ModelList();
        int iconId = 0;
        if (FeedServiceBridge.isSignedIn()) {
            if (ChromeFeatureList.isEnabled(ChromeFeatureList.WEB_FEED)) {
                itemList.add(buildMenuListItem(
                        R.string.ntp_manage_feed, R.id.ntp_feed_header_menu_item_manage, iconId));
            } else {
                itemList.add(buildMenuListItem(R.string.ntp_manage_my_activity,
                        R.id.ntp_feed_header_menu_item_activity, iconId));
                itemList.add(buildMenuListItem(R.string.ntp_manage_interests,
                        R.id.ntp_feed_header_menu_item_interest, iconId));
                if (FeedServiceBridge.isAutoplayEnabled()) {
                    itemList.add(buildMenuListItem(R.string.ntp_manage_autoplay,
                            R.id.ntp_feed_header_menu_item_autoplay, iconId));
                }
                if (ChromeFeatureList.isEnabled(ChromeFeatureList.INTEREST_FEED_V2_HEARTS)) {
                    itemList.add(buildMenuListItem(R.string.ntp_manage_reactions,
                            R.id.ntp_feed_header_menu_item_reactions, iconId));
                }
            }
        } else if (FeedServiceBridge.isAutoplayEnabled()) {
            // Show manage autoplay if not signed in.
            itemList.add(buildMenuListItem(
                    R.string.ntp_manage_autoplay, R.id.ntp_feed_header_menu_item_autoplay, iconId));
        }
        itemList.add(buildMenuListItem(
                R.string.learn_more, R.id.ntp_feed_header_menu_item_learn, iconId));
        itemList.add(getMenuToggleSwitch(
                mSectionHeaderModel.get(SectionHeaderListProperties.IS_SECTION_ENABLED_KEY),
                iconId));
        return itemList;
    }

    /**
     * Returns the menu list item that represents turning the feed on/off.
     *
     * @param isEnabled Whether the feed section is currently enabled.
     * @param iconId IconId for the list item if any.
     */
    private MVCListAdapter.ListItem getMenuToggleSwitch(boolean isEnabled, int iconId) {
        if (isEnabled) {
            return buildMenuListItem(R.string.ntp_turn_off_feed,
                    R.id.ntp_feed_header_menu_item_toggle_switch, iconId);
        }
        return buildMenuListItem(
                R.string.ntp_turn_on_feed, R.id.ntp_feed_header_menu_item_toggle_switch, iconId);
    }

    /** Whether a new thumbnail should be captured. */
    boolean shouldCaptureThumbnail() {
        return mStreamContentChanged || mCoordinator.getView().getWidth() != mThumbnailWidth
                || mCoordinator.getView().getHeight() != mThumbnailHeight
                || getVerticalScrollOffset() != mThumbnailScrollY;
    }

    /** Reset all the properties for thumbnail capturing after a new thumbnail is captured. */
    void onThumbnailCaptured() {
        mThumbnailWidth = mCoordinator.getView().getWidth();
        mThumbnailHeight = mCoordinator.getView().getHeight();
        mThumbnailScrollY = getVerticalScrollOffset();
        mStreamContentChanged = false;
    }

    /**
     * @return Whether the touch events are enabled.
     * TODO(huayinz): Move this method to a Model once a Model is introduced.
     */
    boolean getTouchEnabled() {
        return mTouchEnabled;
    }

    // TODO(carlosk): replace with FeedFeatures.getPrefService().
    private PrefService getPrefService() {
        if (sPrefServiceForTest != null) return sPrefServiceForTest;
        return UserPrefs.get(Profile.getLastUsedRegularProfile());
    }

    // TouchEnabledDelegate interface.
    @Override
    public void setTouchEnabled(boolean enabled) {
        mTouchEnabled = enabled;
    }

    // ScrollDelegate interface.
    @Override
    public boolean isScrollViewInitialized() {
        RecyclerView recyclerView = mCoordinator.getRecyclerView();
        return recyclerView != null && recyclerView.getHeight() > 0;
    }

    @Override
    public int getVerticalScrollOffset() {
        // This method returns a valid vertical scroll offset only when the first header view in the
        // Stream is visible.
        if (!isScrollViewInitialized()) return 0;

        if (!isChildVisibleAtPosition(0)) {
            return Integer.MIN_VALUE;
        }

        LinearLayoutManager layoutManager =
                (LinearLayoutManager) mCoordinator.getRecyclerView().getLayoutManager();
        if (layoutManager == null) {
            return Integer.MIN_VALUE;
        }

        View view = layoutManager.findViewByPosition(0);
        if (view == null) {
            return Integer.MIN_VALUE;
        }

        return -view.getTop();
    }

    @Override
    public boolean isChildVisibleAtPosition(int position) {
        if (!isScrollViewInitialized()) return false;

        LinearLayoutManager layoutManager =
                (LinearLayoutManager) mCoordinator.getRecyclerView().getLayoutManager();
        if (layoutManager == null) {
            return false;
        }

        int firstItemPosition = layoutManager.findFirstVisibleItemPosition();
        int lastItemPosition = layoutManager.findLastVisibleItemPosition();
        if (firstItemPosition == RecyclerView.NO_POSITION
                || lastItemPosition == RecyclerView.NO_POSITION) {
            return false;
        }

        return firstItemPosition <= position && position <= lastItemPosition;
    }

    @Override
    public void snapScroll() {
        if (mSnapScrollHelper == null) return;
        if (!isScrollViewInitialized()) return;

        int initialScroll = getVerticalScrollOffset();
        int scrollTo = mSnapScrollHelper.calculateSnapPosition(initialScroll);

        // Calculating the snap position should be idempotent.
        assert scrollTo == mSnapScrollHelper.calculateSnapPosition(scrollTo);

        mCoordinator.getRecyclerView().smoothScrollBy(0, scrollTo - initialScroll);
    }

    /**
     * Scrolls the page to show the view at the given {@code viewPosition} if not already visible.
     * @param viewPosition The position of the view that should be visible or scrolled to.
     */
    void scrollToViewIfNecessary(int viewPosition) {
        if (!isScrollViewInitialized()) return;
        if (!isChildVisibleAtPosition(viewPosition)) {
            mCoordinator.getRecyclerView().scrollToPosition(viewPosition);
        }
    }

    @Override
    public void onTemplateURLServiceChanged() {
        updateSectionHeader();
    }

    @Override
    public void onItemSelected(PropertyModel item) {
        int itemId = item.get(ListMenuItemProperties.MENU_ITEM_ID);
        int feedType = mTabToStreamMap
                               .get(mSectionHeaderModel.get(
                                       SectionHeaderListProperties.CURRENT_TAB_INDEX_KEY))
                               .getStreamKind();
        if (itemId == R.id.ntp_feed_header_menu_item_manage) {
            Intent intent = new Intent(mContext, FeedManagementActivity.class);
            intent.putExtra(FeedManagementActivity.INITIATING_STREAM_TYPE_EXTRA, feedType);
            FeedServiceBridge.reportOtherUserAction(feedType, FeedUserActionType.TAPPED_MANAGE);
            FeedUma.recordFeedControlsAction(FeedUma.CONTROLS_ACTION_CLICKED_MANAGE);
            mContext.startActivity(intent);
        } else if (itemId == R.id.ntp_feed_header_menu_item_activity) {
            mActionDelegate.openUrl(WindowOpenDisposition.CURRENT_TAB,
                    new LoadUrlParams("https://myactivity.google.com/myactivity?product=50"));
            FeedServiceBridge.reportOtherUserAction(
                    feedType, FeedUserActionType.TAPPED_MANAGE_ACTIVITY);
            FeedUma.recordFeedControlsAction(FeedUma.CONTROLS_ACTION_CLICKED_MY_ACTIVITY);
        } else if (itemId == R.id.ntp_feed_header_menu_item_interest) {
            mActionDelegate.openUrl(WindowOpenDisposition.CURRENT_TAB,
                    new LoadUrlParams("https://www.google.com/preferences/interests"));
            FeedServiceBridge.reportOtherUserAction(
                    feedType, FeedUserActionType.TAPPED_MANAGE_INTERESTS);
            FeedUma.recordFeedControlsAction(FeedUma.CONTROLS_ACTION_CLICKED_MANAGE_INTERESTS);
        } else if (itemId == R.id.ntp_feed_header_menu_item_reactions) {
            mActionDelegate.openUrl(WindowOpenDisposition.CURRENT_TAB,
                    new LoadUrlParams("https://www.google.com/search/contributions/reactions"));
            FeedServiceBridge.reportOtherUserAction(
                    feedType, FeedUserActionType.TAPPED_MANAGE_REACTIONS);
            FeedUma.recordFeedControlsAction(FeedUma.CONTROLS_ACTION_CLICKED_MANAGE_INTERESTS);
        } else if (itemId == R.id.ntp_feed_header_menu_item_autoplay) {
            mCoordinator.launchAutoplaySettings();
            FeedUma.recordFeedControlsAction(FeedUma.CONTROLS_ACTION_CLICKED_MANAGE_AUTOPLAY);
        } else if (itemId == R.id.ntp_feed_header_menu_item_learn) {
            mActionDelegate.openHelpPage();
            FeedServiceBridge.reportOtherUserAction(feedType, FeedUserActionType.TAPPED_LEARN_MORE);
            FeedUma.recordFeedControlsAction(FeedUma.CONTROLS_ACTION_CLICKED_LEARN_MORE);
        } else if (itemId == R.id.ntp_feed_header_menu_item_toggle_switch) {
            onSectionHeaderToggled();
        } else {
            assert false : String.format(Locale.ENGLISH,
                                   "Cannot handle action for item in the menu with id %d", itemId);
        }
    }

    // IdentityManager.Observer interface.

    @Override
    public void onPrimaryAccountChanged(PrimaryAccountChangeEvent eventDetails) {
        updateSectionHeader();
    }

    @VisibleForTesting
    public SignInPromo getSignInPromoForTesting() {
        return mSignInPromo;
    }

    public void manualRefresh(Callback<Boolean> callback) {
        if (mCurrentStream != null) {
            mCurrentStream.triggerRefresh(callback);
        } else {
            callback.onResult(false);
        }
    }

    private FeedScrollState getScrollStateForAutoScrollToTop() {
        FeedScrollState state = new FeedScrollState();
        state.position = 1;
        state.lastPosition = 5;
        return state;
    }

    // Detects animation finishes in RecyclerView.
    // https://stackoverflow.com/questions/33710605/detect-animation-finish-in-androids-recyclerview
    private class RecyclerViewAnimationFinishDetector
            implements RecyclerView.ItemAnimator.ItemAnimatorFinishedListener {
        private Runnable mFinishedCallback;

        /**
         * Asynchronously waits for the animation to finish. If there's already a callback waiting,
         * this replaces the existing callback.
         *
         * @param finishedCallback Callback to invoke when the animation finishes.
         */
        public void runWhenAnimationComplete(Runnable finishedCallback) {
            if (mCoordinator.getRecyclerView() == null) {
                return;
            }
            mFinishedCallback = finishedCallback;

            // The RecyclerView has not started animating yet, so post a message to the
            // message queue that will be run after the RecyclerView has started animating.
            new Handler().post(() -> { checkFinish(); });
        }

        private void checkFinish() {
            RecyclerView recyclerView = mCoordinator.getRecyclerView();

            if (recyclerView != null && recyclerView.isAnimating()) {
                // The RecyclerView is still animating, try again when the animation has finished.
                recyclerView.getItemAnimator().isRunning(this);
                return;
            }

            // The RecyclerView has animated all it's views.
            onFinished();
        }

        private void onFinished() {
            if (mFinishedCallback != null) {
                mFinishedCallback.run();
                mFinishedCallback = null;
            }
        }

        @Override
        public void onAnimationsFinished() {
            // There might still be more items that will be animated after this one.
            new Handler().post(() -> { checkFinish(); });
        }
    }

    private @StreamType int getStreamType(Stream stream) {
        switch (stream.getStreamKind()) {
            case StreamKind.FOR_YOU:
                return StreamType.FOR_YOU;
            case StreamKind.FOLLOWING:
                return StreamType.WEB_FEED;
            default:
                return StreamType.UNSPECIFIED;
        }
    }

    private void logSwitchedFeeds(Stream switchedToStream) {
        // Log the end of an ongoing launch and the beginning of a new one.
        FeedReliabilityLogger reliabilityLogger = mCoordinator.getReliabilityLogger();
        if (reliabilityLogger == null) {
            return;
        }
        reliabilityLogger.logLaunchFinishedIfInProgress(
                DiscoverLaunchResult.SWITCHED_FEED_TABS, /*userMightComeBack=*/false);
        reliabilityLogger.getLaunchLogger().logSwitchedFeeds(
                getStreamType(switchedToStream), SystemClock.elapsedRealtimeNanos());
    }

    private boolean isSuggestionsVisible() {
        return getPrefService().getBoolean(Pref.ARTICLES_LIST_VISIBLE);
    }

    @VisibleForTesting
    OnSectionHeaderSelectedListener getOrCreateSectionHeaderListenerForTesting() {
        OnSectionHeaderSelectedListener listener =
                mSectionHeaderModel.get(SectionHeaderListProperties.ON_TAB_SELECTED_CALLBACK_KEY);
        if (listener == null) {
            listener = new FeedSurfaceHeaderSelectedCallback();
        }
        return listener;
    }

    @VisibleForTesting
    void setStreamForTesting(int key, Stream stream) {
        mTabToStreamMap.put(key, stream);
    }

    @VisibleForTesting
    int getTabToStreamSizeForTesting() {
        return mTabToStreamMap.size();
    }
}
