// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.tasks.tab_management;

import android.animation.Animator;
import android.animation.AnimatorListenerAdapter;
import android.animation.AnimatorSet;
import android.animation.ObjectAnimator;
import android.content.Context;
import android.graphics.Rect;
import android.graphics.RectF;
import android.os.Handler;
import android.os.SystemClock;
import android.view.View;
import android.view.ViewGroup;

import androidx.annotation.Nullable;
import androidx.annotation.VisibleForTesting;

import org.chromium.base.Log;
import org.chromium.base.TraceEvent;
import org.chromium.base.jank_tracker.JankScenario;
import org.chromium.base.jank_tracker.JankTracker;
import org.chromium.base.metrics.RecordHistogram;
import org.chromium.base.metrics.RecordUserAction;
import org.chromium.base.supplier.Supplier;
import org.chromium.chrome.browser.browser_controls.BrowserControlsStateProvider;
import org.chromium.chrome.browser.compositor.layouts.Layout;
import org.chromium.chrome.browser.compositor.layouts.LayoutRenderHost;
import org.chromium.chrome.browser.compositor.layouts.LayoutUpdateHost;
import org.chromium.chrome.browser.compositor.layouts.components.LayoutTab;
import org.chromium.chrome.browser.compositor.layouts.content.TabContentManager;
import org.chromium.chrome.browser.compositor.scene_layer.TabListSceneLayer;
import org.chromium.chrome.browser.layouts.EventFilter;
import org.chromium.chrome.browser.layouts.LayoutType;
import org.chromium.chrome.browser.layouts.animation.CompositorAnimationHandler;
import org.chromium.chrome.browser.layouts.animation.CompositorAnimator;
import org.chromium.chrome.browser.layouts.scene_layer.SceneLayer;
import org.chromium.chrome.browser.tab.Tab;
import org.chromium.chrome.browser.tab.TabUtils;
import org.chromium.chrome.browser.tabmodel.TabModelSelector;
import org.chromium.chrome.browser.tasks.tab_management.TabSwitcher.TabListDelegate;
import org.chromium.chrome.browser.tasks.tab_management.TabSwitcher.TabSwitcherViewObserver;
import org.chromium.chrome.browser.util.ChromeAccessibilityUtil;
import org.chromium.components.browser_ui.widget.animation.Interpolators;
import org.chromium.components.browser_ui.widget.scrim.ScrimCoordinator;
import org.chromium.components.browser_ui.widget.scrim.ScrimProperties;
import org.chromium.components.version_info.VersionInfo;
import org.chromium.ui.base.DeviceFormFactor;
import org.chromium.ui.modelutil.PropertyModel;
import org.chromium.ui.resources.ResourceManager;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collection;
import java.util.LinkedList;
import java.util.List;
import java.util.Locale;

/**
 * A {@link Layout} that shows all tabs in one grid or list view.
 */
public class TabSwitcherLayout extends Layout {
    private static final String TAG = "TSLayout";

    // Duration of the transition animation
    public static final long ZOOMING_DURATION = 300;
    private static final int TRANSLATE_DURATION_MS = 450;
    private static final int BACKGROUND_FADING_DURATION_MS = 150;
    private static final int SCRIM_FADE_DURATION_MS = 450;

    private static final String TRACE_SHOW_TAB_SWITCHER = "TabSwitcherLayout.Show.TabSwitcher";
    private static final String TRACE_HIDE_TAB_SWITCHER = "TabSwitcherLayout.Hide.TabSwitcher";
    private static final String TRACE_DONE_SHOWING_TAB_SWITCHER = "TabSwitcherLayout.DoneShowing";
    private static final String TRACE_DONE_HIDING_TAB_SWITCHER = "TabSwitcherLayout.DoneHiding";

    // The transition animation from a tab to the tab switcher.
    private AnimatorSet mTabToSwitcherAnimation;
    private boolean mIsAnimatingHide;

    private TabListSceneLayer mSceneLayer;
    private final TabSwitcher mTabSwitcher;
    private final JankTracker mJankTracker;
    private final TabSwitcher.Controller mController;
    private final TabSwitcherViewObserver mTabSwitcherObserver;
    @Nullable
    private final ViewGroup mScrimAnchor;
    @Nullable
    private final ScrimCoordinator mScrimCoordinator;
    private final TabSwitcher.TabListDelegate mGridTabListDelegate;

    // Force the toolbar to finish its animation when this Layout finished hiding by setting
    // setShowToolbar(true) on this dummy tab.
    private final LayoutTab mDummyLayoutTab;
    private boolean mIsInitialized;

    private float mBackgroundAlpha;

    private int mFrameCount;
    private long mStartTime;
    private long mLastFrameTime;
    private long mMaxFrameInterval;
    private int mStartFrame;

    private boolean mAndroidViewFinishedShowing;

    interface PerfListener {
        void onAnimationDone(
                int frameRendered, long elapsedMs, long maxFrameInterval, int dirtySpan);
    }

    private PerfListener mPerfListenerForTesting;

    public TabSwitcherLayout(Context context, LayoutUpdateHost updateHost,
            LayoutRenderHost renderHost, TabSwitcher tabSwitcher, JankTracker jankTracker,
            @Nullable ViewGroup tabSwitcherScrimAnchor,
            @Nullable ScrimCoordinator scrimCoordinator) {
        super(context, updateHost, renderHost);
        mDummyLayoutTab = createLayoutTab(Tab.INVALID_TAB_ID, false);
        mDummyLayoutTab.setShowToolbar(true);
        mTabSwitcher = tabSwitcher;
        mController = mTabSwitcher.getController();
        mTabSwitcher.setOnTabSelectingListener(this::onTabSelecting);
        mGridTabListDelegate = mTabSwitcher.getTabListDelegate();
        mJankTracker = jankTracker;
        mScrimAnchor = tabSwitcherScrimAnchor;
        mScrimCoordinator = scrimCoordinator;

        mTabSwitcherObserver = new TabSwitcherViewObserver() {
            @Override
            public void startedShowing() {
                mAndroidViewFinishedShowing = false;
            }

            @Override
            public void finishedShowing() {
                mAndroidViewFinishedShowing = true;
                if (!TabUiFeatureUtilities.isTabletGridTabSwitcherPolishEnabled(context)) {
                    doneShowing();
                }
                // When Tab-to-GTS animation is done, it's time to renew the thumbnail without
                // causing janky frames. When animation is off or not used, the thumbnail is already
                // updated when showing the GTS. Tab-to-GTS animation is not invoked for tablet tab
                // switcher polish.
                if (isTabGtsAnimationEnabled()) {
                    // Delay thumbnail taking a bit more to make it less likely to happen before the
                    // thumbnail taking triggered by ThumbnailFetcher. See crbug.com/996385 for
                    // details.
                    new Handler().postDelayed(() -> {
                        Tab currentTab = mTabModelSelector.getCurrentTab();
                        if (currentTab != null) mTabContentManager.cacheTabThumbnail(currentTab);
                        mLayoutTabs = null;
                    }, ZOOMING_DURATION);
                } else {
                    // crbug.com/1176548, mLayoutTabs is used to capture thumbnail, null it in a
                    // post delay handler to avoid creating a new pending surface in native, which
                    // will hold the thumbnail capturing task.
                    new Handler().postDelayed(() -> { mLayoutTabs = null; }, ZOOMING_DURATION);
                }
            }

            @Override
            public void startedHiding() {}

            @Override
            public void finishedHiding() {
                // The Android View version of GTS overview is hidden.
                // If not doing GTS-to-Tab transition animation, we show the fade-out instead, which
                // was already done.
                if (!isTabGtsAnimationEnabled()) {
                    postHiding();
                    return;
                }
                // If we are doing GTS-to-Tab transition animation, we start showing the Bitmap
                // version of the GTS overview in the background while expanding the thumbnail to
                // the viewport.
                expandTab(getThumbnailLocationOfCurrentTab());
            }
        };

        mController.addTabSwitcherViewObserver(mTabSwitcherObserver);
    }

    @Override
    public void onFinishNativeInitialization() {
        if (mIsInitialized) return;

        mIsInitialized = true;
        mTabSwitcher.initWithNative();
        ensureSceneLayerCreated();
        mSceneLayer.setTabModelSelector(mTabModelSelector);
    }

    // Layout implementation.
    @Override
    public void setTabModelSelector(TabModelSelector modelSelector, TabContentManager manager) {
        super.setTabModelSelector(modelSelector, manager);
        if (mSceneLayer != null) {
            mSceneLayer.setTabModelSelector(modelSelector);
        }
    }

    @Override
    public LayoutTab getLayoutTab(int id) {
        return mDummyLayoutTab;
    }

    @Override
    public void destroy() {
        mController.removeTabSwitcherViewObserver(mTabSwitcherObserver);
    }

    @Override
    public void show(long time, boolean animate) {
        try (TraceEvent e = TraceEvent.scoped(TRACE_SHOW_TAB_SWITCHER)) {
            super.show(time, animate);

            mJankTracker.startTrackingScenario(JankScenario.TAB_SWITCHER);

            // Keep the current tab in mLayoutTabs even if we are not going to show the shrinking
            // animation so that thumbnail taking is not blocked.
            LayoutTab sourceLayoutTab = createLayoutTab(
                    mTabModelSelector.getCurrentTabId(), mTabModelSelector.isIncognitoSelected());
            sourceLayoutTab.setDecorationAlpha(0);

            mLayoutTabs = new LayoutTab[] {sourceLayoutTab};

            boolean quick = mGridTabListDelegate.prepareTabSwitcherView();

            // Skip animation when there is no tab in current tab model, we don't show the shrink
            // tab animatio.
            boolean isCurrentTabModelEmpty = mTabModelSelector.getCurrentModel().getCount() == 0;
            animate = animate && !isCurrentTabModelEmpty;

            if (TabUiFeatureUtilities.isTabletGridTabSwitcherPolishEnabled(getContext())) {
                showOverviewWithTranslateUp(animate);
            } else {
                showOverviewWithTabShrink(animate,
                        () -> mGridTabListDelegate.getThumbnailLocationOfCurrentTab(false), quick);
            }
        }
    }

    private void showBrowserScrim() {
        if (mScrimCoordinator == null) return;
        PropertyModel scrimProp = new PropertyModel.Builder(ScrimProperties.REQUIRED_KEYS)
                                          .with(ScrimProperties.ANCHOR_VIEW, mScrimAnchor)
                                          .with(ScrimProperties.SHOW_IN_FRONT_OF_ANCHOR_VIEW, false)
                                          .build();
        mScrimCoordinator.showScrim(scrimProp);
    }

    private void hideBrowserScrim() {
        if (mScrimCoordinator == null || !mScrimCoordinator.isShowingScrim()) return;
        mScrimCoordinator.hideScrim(true, SCRIM_FADE_DURATION_MS);
    }

    @Override
    protected void updateLayout(long time, long dt) {
        ensureSceneLayerCreated();
        super.updateLayout(time, dt);
        if (mLayoutTabs == null) return;

        assert mLayoutTabs.length >= 1;
        boolean needUpdate = updateSnap(dt, mLayoutTabs[0]);
        if (needUpdate) requestUpdate();
    }

    @Override
    public void startHiding(int nextId, boolean hintAtTabSelection) {
        try (TraceEvent e = TraceEvent.scoped(TRACE_HIDE_TAB_SWITCHER)) {
            super.startHiding(nextId, hintAtTabSelection);

            int sourceTabId = nextId;
            if (sourceTabId == Tab.INVALID_TAB_ID) {
                sourceTabId = mTabModelSelector.getCurrentTabId();
            }

            LayoutTab sourceLayoutTab =
                    createLayoutTab(sourceTabId, mTabModelSelector.isIncognitoSelected());
            sourceLayoutTab.setDecorationAlpha(0);

            List<LayoutTab> layoutTabs = new ArrayList<>();
            layoutTabs.add(sourceLayoutTab);

            if (sourceTabId != mTabModelSelector.getCurrentTabId()) {
                // Keep the original tab in mLayoutTabs to unblock thumbnail taking at the end of
                // the animation.
                LayoutTab originalTab = createLayoutTab(mTabModelSelector.getCurrentTabId(),
                        mTabModelSelector.isIncognitoSelected());
                originalTab.setScale(0);
                originalTab.setDecorationAlpha(0);
                layoutTabs.add(originalTab);
            }
            mLayoutTabs = layoutTabs.toArray(new LayoutTab[0]);

            updateCacheVisibleIds(new LinkedList<>(Arrays.asList(sourceTabId)));

            mIsAnimatingHide = true;
            if (TabUiFeatureUtilities.isTabletGridTabSwitcherPolishEnabled(getContext())) {
                translateDown();
            } else {
                mController.hideTabSwitcherView(!isTabGtsAnimationEnabled());
            }
        }
    }

    @Override
    public void doneHiding() {
        try (TraceEvent e = TraceEvent.scoped(TRACE_DONE_HIDING_TAB_SWITCHER)) {
            super.doneHiding();
            RecordUserAction.record("MobileExitStackView");
            mJankTracker.finishTrackingScenario(JankScenario.TAB_SWITCHER);
        }
    }

    @Override
    public void doneShowing() {
        try (TraceEvent e = TraceEvent.scoped(TRACE_DONE_SHOWING_TAB_SWITCHER)) {
            if (!mAndroidViewFinishedShowing) return;
            super.doneShowing();
        }
    }

    @Override
    public boolean onBackPressed() {
        return mController.onBackPressed(false);
    }

    @Override
    protected EventFilter getEventFilter() {
        return null;
    }

    @Override
    protected SceneLayer getSceneLayer() {
        return mSceneLayer;
    }

    private void ensureSceneLayerCreated() {
        if (mSceneLayer != null) return;
        mSceneLayer = new TabListSceneLayer();
    }

    @Override
    public boolean handlesTabClosing() {
        return true;
    }

    @Override
    public boolean handlesTabCreating() {
        return true;
    }

    @Override
    protected void forceAnimationToFinish() {
        super.forceAnimationToFinish();
        if (mTabToSwitcherAnimation != null) {
            if (mTabToSwitcherAnimation.isRunning()) {
                mTabToSwitcherAnimation.end();
            }
        }
    }

    /**
     * Animate shrinking a tab to a target {@link Rect} area.
     * @param animate Whether to play an entry animation.
     * @param target The target {@link Rect} area.
     */
    private void showOverviewWithTabShrink(boolean animate, Supplier<Rect> target, boolean quick) {
        // Skip shrinking animation when there is no tab in current tab model.
        boolean isCurrentTabModelEmpty = mTabModelSelector.getCurrentModel().getCount() == 0;
        boolean showShrinkingAnimation =
                animate && isTabGtsAnimationEnabled() && !isCurrentTabModelEmpty;

        boolean skipSlowZooming = TabUiFeatureUtilities.SKIP_SLOW_ZOOMING.getValue();
        Log.d(TAG, "SkipSlowZooming = " + skipSlowZooming);
        if (skipSlowZooming) {
            showShrinkingAnimation &= quick;
        }
        if (TabUiFeatureUtilities.isLaunchPolishEnabled()) {
            // Intentionally disable the shrinking animation when accessibility is enabled.
            // During the shrinking animation, since the ComponsitorViewHolder is not focusable,
            // I think we are in a temporary no "valid" focus target state, so the focus shifts
            // to the omnibox and triggers an accessibility announcement of the URL and a
            // keyboard hiding event. Disable the animation to avoid this temporary state.
            showShrinkingAnimation &= !ChromeAccessibilityUtil.get().isAccessibilityEnabled();
        }

        if (!showShrinkingAnimation || target.get() == null) {
            mController.showTabSwitcherView(animate);
            return;
        }

        forceAnimationToFinish();
        LayoutTab sourceLayoutTab = mLayoutTabs[0];
        CompositorAnimationHandler handler = getAnimationHandler();
        Collection<Animator> animationList = new ArrayList<>(5);

        // Step 1: zoom out the source tab
        Supplier<Float> scaleStartValueSupplier = () -> 1.0f;
        Supplier<Float> scaleEndValueSupplier = () -> target.get().width() / (getWidth() * mDpToPx);

        Supplier<Float> xStartValueSupplier = () -> 0f;
        Supplier<Float> xEndValueSupplier = () -> target.get().left / mDpToPx;

        Supplier<Float> yStartValueSupplier = () -> 0f;
        Supplier<Float> yEndValueSupplier = () -> target.get().top / mDpToPx;

        animationList.add(CompositorAnimator.ofWritableFloatPropertyKey(handler, sourceLayoutTab,
                LayoutTab.SCALE, scaleStartValueSupplier, scaleEndValueSupplier, ZOOMING_DURATION,
                Interpolators.FAST_OUT_SLOW_IN_INTERPOLATOR));
        animationList.add(CompositorAnimator.ofWritableFloatPropertyKey(handler, sourceLayoutTab,
                LayoutTab.X, xStartValueSupplier, xEndValueSupplier, ZOOMING_DURATION,
                Interpolators.FAST_OUT_SLOW_IN_INTERPOLATOR));
        animationList.add(CompositorAnimator.ofWritableFloatPropertyKey(handler, sourceLayoutTab,
                LayoutTab.Y, yStartValueSupplier, yEndValueSupplier, ZOOMING_DURATION,
                Interpolators.FAST_OUT_SLOW_IN_INTERPOLATOR));
        // TODO(crbug.com/964406): when shrinking to the bottom row, bottom of the tab goes up and
        // down, making the "create group" visible for a while.
        animationList.add(CompositorAnimator.ofWritableFloatPropertyKey(handler, sourceLayoutTab,
                LayoutTab.MAX_CONTENT_HEIGHT, sourceLayoutTab.getUnclampedOriginalContentHeight(),
                TabUiFeatureUtilities.isTabThumbnailAspectRatioNotOne()
                        ? Math.min(getWidth() / TabUtils.getTabThumbnailAspectRatio(getContext()),
                                sourceLayoutTab.getUnclampedOriginalContentHeight())
                        : getWidth(),
                ZOOMING_DURATION, Interpolators.FAST_OUT_SLOW_IN_INTERPOLATOR));

        CompositorAnimator backgroundAlpha =
                CompositorAnimator.ofFloat(handler, 0f, 1f, BACKGROUND_FADING_DURATION_MS,
                        animator -> mBackgroundAlpha = animator.getAnimatedValue());
        backgroundAlpha.setInterpolator(Interpolators.FAST_OUT_LINEAR_IN_INTERPOLATOR);
        animationList.add(backgroundAlpha);

        mTabToSwitcherAnimation = new AnimatorSet();
        mTabToSwitcherAnimation.playTogether(animationList);
        mTabToSwitcherAnimation.addListener(new AnimatorListenerAdapter() {
            @Override
            public void onAnimationEnd(Animator animation) {
                mTabToSwitcherAnimation = null;
                // Step 2: fade in the real GTS RecyclerView.
                mController.showTabSwitcherView(true);

                reportAnimationPerf(true);
            }
        });
        mStartFrame = mFrameCount;
        mStartTime = SystemClock.elapsedRealtime();
        mLastFrameTime = SystemClock.elapsedRealtime();
        mMaxFrameInterval = 0;
        mTabToSwitcherAnimation.start();
    }

    /**
     * Animate expanding a tab from a source {@link Rect} area.
     * @param source The source {@link Rect} area.
     */
    private void expandTab(Rect source) {
        LayoutTab sourceLayoutTab = mLayoutTabs[0];

        forceAnimationToFinish();
        CompositorAnimationHandler handler = getAnimationHandler();
        Collection<Animator> animationList = new ArrayList<>(5);

        // Zoom in the source tab
        animationList.add(CompositorAnimator.ofWritableFloatPropertyKey(handler, sourceLayoutTab,
                LayoutTab.SCALE, source.width() / (getWidth() * mDpToPx), 1, ZOOMING_DURATION,
                Interpolators.FAST_OUT_SLOW_IN_INTERPOLATOR));
        animationList.add(CompositorAnimator.ofWritableFloatPropertyKey(handler, sourceLayoutTab,
                LayoutTab.X, source.left / mDpToPx, 0f, ZOOMING_DURATION,
                Interpolators.FAST_OUT_SLOW_IN_INTERPOLATOR));
        animationList.add(CompositorAnimator.ofWritableFloatPropertyKey(handler, sourceLayoutTab,
                LayoutTab.Y, source.top / mDpToPx, 0f, ZOOMING_DURATION,
                Interpolators.FAST_OUT_SLOW_IN_INTERPOLATOR));
        // TODO(crbug.com/964406): when shrinking to the bottom row, bottom of the tab goes up and
        // down, making the "create group" visible for a while.
        animationList.add(CompositorAnimator.ofWritableFloatPropertyKey(handler, sourceLayoutTab,
                LayoutTab.MAX_CONTENT_HEIGHT,
                TabUiFeatureUtilities.isTabThumbnailAspectRatioNotOne()
                        ? Math.min(getWidth() / TabUtils.getTabThumbnailAspectRatio(getContext()),
                                sourceLayoutTab.getUnclampedOriginalContentHeight())
                        : getWidth(),
                sourceLayoutTab.getUnclampedOriginalContentHeight(), ZOOMING_DURATION,
                Interpolators.FAST_OUT_SLOW_IN_INTERPOLATOR));

        CompositorAnimator backgroundAlpha =
                CompositorAnimator.ofFloat(handler, 1f, 0f, BACKGROUND_FADING_DURATION_MS,
                        animator -> mBackgroundAlpha = animator.getAnimatedValue());
        backgroundAlpha.setInterpolator(Interpolators.FAST_OUT_LINEAR_IN_INTERPOLATOR);
        animationList.add(backgroundAlpha);

        mTabToSwitcherAnimation = new AnimatorSet();
        mTabToSwitcherAnimation.playTogether(animationList);
        mTabToSwitcherAnimation.addListener(new AnimatorListenerAdapter() {
            @Override
            public void onAnimationEnd(Animator animation) {
                mTabToSwitcherAnimation = null;
                postHiding();

                reportAnimationPerf(false);
            }
        });
        mStartFrame = mFrameCount;
        mStartTime = SystemClock.elapsedRealtime();
        mLastFrameTime = SystemClock.elapsedRealtime();
        mMaxFrameInterval = 0;
        mTabToSwitcherAnimation.start();
    }

    /**
     * Animate translating grid tab switcher and its toolbar up.
     */
    private void showOverviewWithTranslateUp(boolean animate) {
        forceAnimationToFinish();
        showBrowserScrim();

        Animator translateUp = ObjectAnimator.ofFloat(mController.getTabSwitcherContainer(),
                View.TRANSLATION_Y, mController.getTabSwitcherContainer().getHeight(), 0f);
        translateUp.setInterpolator(Interpolators.EMPHASIZED_DECELERATE);
        translateUp.setDuration(TRANSLATE_DURATION_MS);

        mTabToSwitcherAnimation = new AnimatorSet();
        mTabToSwitcherAnimation.play(translateUp);
        mTabToSwitcherAnimation.addListener(new AnimatorListenerAdapter() {
            @Override
            public void onAnimationStart(Animator animation) {
                // Skip fade-in for tab switcher view, since it will translate in instead.
                mController.getTabSwitcherContainer().setVisibility(View.VISIBLE);
                mController.showTabSwitcherView(animate);
                mController.setSnackbarParentView(mController.getTabSwitcherContainer());
            }

            @Override
            public void onAnimationEnd(Animator animation) {
                mTabToSwitcherAnimation = null;
                mController.getTabSwitcherContainer().setY(0);
                doneShowing();

                reportTabletAnimationPerf(true);
            }
        });
        mTabToSwitcherAnimation.start();
    }

    /**
     * Animate translating grid tab switcher and its toolbar down off-screen.
     */
    private void translateDown() {
        forceAnimationToFinish();
        hideBrowserScrim();

        Animator translateDown = ObjectAnimator.ofFloat(mController.getTabSwitcherContainer(),
                View.TRANSLATION_Y, 0f, mController.getTabSwitcherContainer().getHeight());
        translateDown.setInterpolator(Interpolators.EMPHASIZED_ACCELERATE);
        translateDown.setDuration(TRANSLATE_DURATION_MS);

        mTabToSwitcherAnimation = new AnimatorSet();
        mTabToSwitcherAnimation.play(translateDown);
        mTabToSwitcherAnimation.addListener(new AnimatorListenerAdapter() {
            @Override
            public void onAnimationStart(Animator animation) {
                mController.prepareHideTabSwitcherView();
                mController.setSnackbarParentView(null);
            }

            @Override
            public void onAnimationEnd(Animator animation) {
                mTabToSwitcherAnimation = null;

                // Skip fade-out  for tab switcher view, since it will translate out instead.
                mController.hideTabSwitcherView(false);
                mController.getTabSwitcherContainer().setVisibility(View.GONE);

                reportTabletAnimationPerf(false);
            }
        });
        mTabToSwitcherAnimation.start();
    }

    private Rect getThumbnailLocationOfCurrentTab() {
        return mGridTabListDelegate.getThumbnailLocationOfCurrentTab(true);
    }

    private TabListDelegate getGridTabListDelegate() {
        return mGridTabListDelegate;
    }

    private void postHiding() {
        mGridTabListDelegate.postHiding();
        mIsAnimatingHide = false;
        doneHiding();
    }

    @VisibleForTesting
    void setPerfListenerForTesting(PerfListener perfListener) {
        mPerfListenerForTesting = perfListener;
    }

    @VisibleForTesting
    public TabSwitcher getTabSwitcherForTesting() {
        return mTabSwitcher;
    }

    private void reportAnimationPerf(boolean isShrinking) {
        int frameRendered = mFrameCount - mStartFrame;
        long elapsedMs = SystemClock.elapsedRealtime() - mStartTime;
        long lastDirty = mGridTabListDelegate.getLastDirtyTime();
        int dirtySpan = (int) (lastDirty - mStartTime);
        float fps = 1000.f * frameRendered / elapsedMs;
        String message = String.format(Locale.US,
                "fps = %.2f (%d / %dms), maxFrameInterval = %d, dirtySpan = %d", fps, frameRendered,
                elapsedMs, mMaxFrameInterval, dirtySpan);

        // TODO(crbug.com/964406): stop logging it after this feature stabilizes.
        if (!VersionInfo.isStableBuild()) {
            Log.i(TAG, message);
        }

        String suffix = isShrinking ? ".Shrink" : ".Expand";

        // TODO(crbug.com/982018): Separate histograms for carousel tab switcher.
        RecordHistogram.recordCount100Histogram(
                "GridTabSwitcher.FramePerSecond" + suffix, (int) fps);
        RecordHistogram.recordTimesHistogram(
                "GridTabSwitcher.MaxFrameInterval" + suffix, mMaxFrameInterval);
        RecordHistogram.recordTimesHistogram("GridTabSwitcher.DirtySpan" + suffix, dirtySpan);

        if (mPerfListenerForTesting != null) {
            mPerfListenerForTesting.onAnimationDone(
                    frameRendered, elapsedMs, mMaxFrameInterval, dirtySpan);
        }
    }

    private void reportTabletAnimationPerf(boolean translatingUp) {
        // TODO(crbug.com/1304926): Record metrics for tablet animations.
    }

    @Override
    protected void updateSceneLayer(RectF viewport, RectF contentViewport,
            TabContentManager tabContentManager, ResourceManager resourceManager,
            BrowserControlsStateProvider browserControls) {
        ensureSceneLayerCreated();
        super.updateSceneLayer(
                viewport, contentViewport, tabContentManager, resourceManager, browserControls);
        assert mSceneLayer != null;

        // The content viewport is intentionally sent as both params below.
        mSceneLayer.pushLayers(getContext(), contentViewport, contentViewport, this,
                tabContentManager, resourceManager, browserControls,
                isTabGtsAnimationEnabled() ? mGridTabListDelegate.getResourceId() : 0,
                mBackgroundAlpha, mGridTabListDelegate.getTabListTopOffset());
        mFrameCount++;
        if (mLastFrameTime != 0) {
            long elapsed = SystemClock.elapsedRealtime() - mLastFrameTime;
            mMaxFrameInterval = Math.max(mMaxFrameInterval, elapsed);
        }
        mLastFrameTime = SystemClock.elapsedRealtime();
    }

    @Override
    public int getLayoutType() {
        return LayoutType.TAB_SWITCHER;
    }

    @Override
    public boolean onUpdateAnimation(long time, boolean jumpToEnd) {
        return mTabToSwitcherAnimation == null && !mIsAnimatingHide;
    }

    @Override
    public boolean canHostBeFocusable() {
        if (TabUiFeatureUtilities.isLaunchPolishEnabled()
                && ChromeAccessibilityUtil.get().isAccessibilityEnabled()
                && !DeviceFormFactor.isNonMultiDisplayContextOnTablet(getContext())) {
            // We don't allow this layout to gain focus when accessibility is enabled so that the
            // CompositorViewHolder doesn't steal focus when entering tab switcher.
            // (crbug.com/1125185).
            // We ignore this logic on tablets, since it would cause focus to briefly shift to the
            // omnibox while entering the tab switcher. This was most notable on the NTP, where the
            // virtual keyboard would quickly appear then disappear. (https://crbug.com/1320035).
            return false;
        }
        return super.canHostBeFocusable();
    }

    /**
     * Shrink/Expand animation is disabled for Tablet TabSwitcher launch polish.
     * @return Whether shrink/expand animation is enabled.
     */
    private boolean isTabGtsAnimationEnabled() {
        if (TabUiFeatureUtilities.isTabletGridTabSwitcherPolishEnabled(getContext())) return false;
        return TabUiFeatureUtilities.isTabToGtsAnimationEnabled();
    }
}
