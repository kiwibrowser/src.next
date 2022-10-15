// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.toolbar.top;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.PorterDuff;
import android.graphics.Rect;
import android.graphics.Region;
import android.graphics.drawable.Drawable;
import android.os.Build;
import android.util.AttributeSet;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewStub;

import androidx.annotation.Nullable;
import androidx.annotation.VisibleForTesting;
import androidx.appcompat.content.res.AppCompatResources;

import org.chromium.base.FeatureList;
import org.chromium.base.TraceEvent;
import org.chromium.base.metrics.RecordHistogram;
import org.chromium.base.supplier.ObservableSupplier;
import org.chromium.base.supplier.Supplier;
import org.chromium.chrome.browser.flags.ChromeFeatureList;
import org.chromium.chrome.browser.tab.Tab;
import org.chromium.chrome.browser.toolbar.ConstraintsChecker;
import org.chromium.chrome.browser.toolbar.ControlContainer;
import org.chromium.chrome.browser.toolbar.R;
import org.chromium.chrome.browser.toolbar.ToolbarCaptureType;
import org.chromium.chrome.browser.toolbar.ToolbarProgressBar;
import org.chromium.chrome.browser.toolbar.top.CaptureReadinessResult.TopToolbarBlockCaptureReason;
import org.chromium.components.browser_ui.styles.ChromeColors;
import org.chromium.components.browser_ui.widget.ClipDrawableProgressBar.DrawingInfo;
import org.chromium.components.browser_ui.widget.ViewResourceFrameLayout;
import org.chromium.components.browser_ui.widget.gesture.SwipeGestureListener;
import org.chromium.components.browser_ui.widget.gesture.SwipeGestureListener.SwipeHandler;
import org.chromium.ui.KeyboardVisibilityDelegate;
import org.chromium.ui.base.ViewUtils;
import org.chromium.ui.resources.dynamics.ViewResourceAdapter;
import org.chromium.ui.widget.OptimizedFrameLayout;

/**
 * Layout for the browser controls (omnibox, menu, tab strip, etc..).
 */
public class ToolbarControlContainer extends OptimizedFrameLayout implements ControlContainer {
    private final float mTabStripHeight;

    private Toolbar mToolbar;
    private ToolbarViewResourceFrameLayout mToolbarContainer;

    private SwipeGestureListener mSwipeGestureListener;

    /**
     * Constructs a new control container.
     * <p>
     * This constructor is used when inflating from XML.
     *
     * @param context The context used to build this view.
     * @param attrs The attributes used to determine how to construct this view.
     */
    public ToolbarControlContainer(Context context, AttributeSet attrs) {
        super(context, attrs);
        mTabStripHeight = context.getResources().getDimension(R.dimen.tab_strip_height);
    }

    @Override
    public ViewResourceAdapter getToolbarResourceAdapter() {
        return mToolbarContainer.getResourceAdapter();
    }

    @Override
    public View getView() {
        return this;
    }

    @Override
    public void getProgressBarDrawingInfo(DrawingInfo drawingInfoOut) {
        if (mToolbar == null) return;
        // TODO(yusufo): Avoid casting to the layout without making the interface bigger.
        ToolbarProgressBar progressBar = mToolbar.getProgressBar();
        if (progressBar != null) progressBar.getDrawingInfo(drawingInfoOut);
    }

    @Override
    public int getToolbarBackgroundColor() {
        if (mToolbar == null) return 0;
        return mToolbar.getPrimaryColor();
    }

    @Override
    public void setSwipeHandler(SwipeHandler handler) {
        mSwipeGestureListener = new SwipeGestureListenerImpl(getContext(), handler);
    }

    @Override
    public void initWithToolbar(int toolbarLayoutId) {
        try (TraceEvent te = TraceEvent.scoped("ToolbarControlContainer.initWithToolbar")) {
            mToolbarContainer =
                    (ToolbarViewResourceFrameLayout) findViewById(R.id.toolbar_container);
            ViewStub toolbarStub = findViewById(R.id.toolbar_stub);
            toolbarStub.setLayoutResource(toolbarLayoutId);
            toolbarStub.inflate();
        }
    }

    /**
     * @param toolbar The toolbar contained inside this control container. Should be called
     *                after inflation is complete.
     * @param isIncognito Whether the toolbar should be initialized with incognito colors.
     * @param constraintsSupplier Used to access current constraints of the browser controls.
     * @param tabSupplier Used to access the current tab state.
     */
    public void setPostInitializationDependencies(Toolbar toolbar, boolean isIncognito,
            ObservableSupplier<Integer> constraintsSupplier, Supplier<Tab> tabSupplier) {
        mToolbar = toolbar;
        mToolbarContainer.setPostInitializationDependencies(
                mToolbar, constraintsSupplier, tabSupplier);

        View toolbarView = findViewById(R.id.toolbar);
        assert toolbarView != null;

        if (toolbarView instanceof ToolbarTablet) {
            // On tablet, draw a fake tab strip and toolbar until the compositor is
            // ready to draw the real tab strip. (On phone, the toolbar is made entirely
            // of Android views, which are already initialized.)
            final Drawable backgroundDrawable =
                    AppCompatResources.getDrawable(getContext(), R.drawable.toolbar_background)
                            .mutate();
            backgroundDrawable.setTint(
                    ChromeColors.getDefaultThemeColor(getContext(), isIncognito));
            backgroundDrawable.setTintMode(PorterDuff.Mode.MULTIPLY);
            setBackground(backgroundDrawable);
        }
    }

    @Override
    // TODO(crbug.com/1231201): work out why this is causing a lint error
    @SuppressWarnings("Override")
    public boolean gatherTransparentRegion(Region region) {
        // Reset the translation on the control container before attempting to compute the
        // transparent region.
        float translateY = getTranslationY();
        setTranslationY(0);

        ViewUtils.gatherTransparentRegionsForOpaqueView(this, region);

        setTranslationY(translateY);

        return true;
    }

    /**
     * Invalidate the entire capturing bitmap region.
     */
    public void invalidateBitmap() {
        ((ToolbarViewResourceAdapter) getToolbarResourceAdapter()).forceInvalidate();
    }

    /**
     * Update whether the control container is ready to have the bitmap representation of
     * itself be captured.
     */
    public void setReadyForBitmapCapture(boolean ready) {
        mToolbarContainer.mReadyForBitmapCapture = ready;
    }

    /**
     * The layout that handles generating the toolbar view resource.
     */
    // Only publicly visible due to lint warnings.
    public static class ToolbarViewResourceFrameLayout extends ViewResourceFrameLayout {
        private boolean mReadyForBitmapCapture;

        public ToolbarViewResourceFrameLayout(Context context, AttributeSet attrs) {
            super(context, attrs);
        }

        @Override
        protected ViewResourceAdapter createResourceAdapter() {
            boolean useHardwareBitmapDraw = false;
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                useHardwareBitmapDraw = ChromeFeatureList.sToolbarUseHardwareBitmapDraw.isEnabled();
            }
            return new ToolbarViewResourceAdapter(this, useHardwareBitmapDraw);
        }

        public void setPostInitializationDependencies(Toolbar toolbar,
                ObservableSupplier<Integer> constraintsSupplier, Supplier<Tab> tabSupplier) {
            ToolbarViewResourceAdapter adapter =
                    ((ToolbarViewResourceAdapter) getResourceAdapter());
            adapter.setPostInitializationDependencies(toolbar, constraintsSupplier, tabSupplier);
        }

        @Override
        protected boolean isReadyForCapture() {
            return mReadyForBitmapCapture && getVisibility() == VISIBLE;
        }
    }

    @VisibleForTesting
    protected static class ToolbarViewResourceAdapter extends ViewResourceAdapter {
        private final int[] mTempPosition = new int[2];
        private final Rect mLocationBarRect = new Rect();
        private final Rect mToolbarRect = new Rect();
        private final View mToolbarContainer;

        @Nullable
        private Toolbar mToolbar;
        private int mTabStripHeightPx;
        @Nullable
        private ConstraintsChecker mConstraintsObserver;
        @Nullable
        private Supplier<Tab> mTabSupplier;

        /** Builds the resource adapter for the toolbar. */
        public ToolbarViewResourceAdapter(View toolbarContainer, boolean useHardwareBitmapDraw) {
            super(toolbarContainer, useHardwareBitmapDraw);
            mToolbarContainer = toolbarContainer;
        }

        /**
         * Set the toolbar after it has been dynamically inflated.
         * @param toolbar The browser's toolbar.
         * @param constraintsSupplier Used to access current constraints of the browser controls.
         * @param tabSupplier Used to access the current tab state.
         */
        public void setPostInitializationDependencies(Toolbar toolbar,
                @Nullable ObservableSupplier<Integer> constraintsSupplier,
                @Nullable Supplier<Tab> tabSupplier) {
            assert mToolbar == null;
            mToolbar = toolbar;
            mTabStripHeightPx = mToolbar.getTabStripHeight();

            assert mConstraintsObserver == null;
            if (constraintsSupplier != null) {
                mConstraintsObserver = new ConstraintsChecker(this, constraintsSupplier);
            }

            assert mTabSupplier == null;
            mTabSupplier = tabSupplier;
        }

        /**
         * Force this resource to be recaptured in full, ignoring the checks
         * {@link #invalidate(Rect)} does.
         */
        public void forceInvalidate() {
            super.invalidate(null);
        }

        @Override
        public boolean isDirty() {
            if (!super.isDirty()) {
                CaptureReadinessResult.logCaptureReasonFromResult(CaptureReadinessResult.notReady(
                        TopToolbarBlockCaptureReason.VIEW_NOT_DIRTY));
                return false;
            }

            if (FeatureList.isInitialized()
                    && ChromeFeatureList.isEnabled(ChromeFeatureList.SUPPRESS_TOOLBAR_CAPTURES)
                    && mConstraintsObserver != null && mTabSupplier != null) {
                Tab tab = mTabSupplier.get();

                // TODO(https://crbug.com/1355516): Understand and fix this for native pages. It
                // seems capturing is required for some part of theme observers to work correctly,
                // but it shouldn't be.
                boolean isNativePage = tab == null || tab.isNativePage();
                if (!isNativePage && mConstraintsObserver.areControlsLocked()) {
                    mConstraintsObserver.scheduleRequestResourceOnUnlock();
                    CaptureReadinessResult.logCaptureReasonFromResult(
                            CaptureReadinessResult.notReady(
                                    TopToolbarBlockCaptureReason.BROWSER_CONTROLS_LOCKED));
                    return false;
                }
            }

            CaptureReadinessResult isReadyResult =
                    mToolbar == null ? null : mToolbar.isReadyForTextureCapture();
            CaptureReadinessResult.logCaptureReasonFromResult(isReadyResult);
            return isReadyResult == null ? false : isReadyResult.isReady;
        }

        @Override
        public void onCaptureStart(Canvas canvas, Rect dirtyRect) {
            RecordHistogram.recordEnumeratedHistogram("Android.Toolbar.BitmapCapture",
                    ToolbarCaptureType.TOP, ToolbarCaptureType.NUM_ENTRIES);

            // Erase the canvas because assets drawn are not fully opaque and therefore painting
            // twice would be bad.
            canvas.save();
            canvas.clipRect(0, 0, mToolbarContainer.getWidth(), mToolbarContainer.getHeight());
            canvas.drawColor(0, PorterDuff.Mode.CLEAR);
            canvas.restore();
            dirtyRect.set(0, 0, mToolbarContainer.getWidth(), mToolbarContainer.getHeight());

            mToolbar.setTextureCaptureMode(true);

            super.onCaptureStart(canvas, dirtyRect);
        }

        @Override
        public void onCaptureEnd() {
            mToolbar.setTextureCaptureMode(false);
            // Forcing a texture capture should only be done for one draw. Turn off forced
            // texture capture.
            mToolbar.setForceTextureCapture(false);
        }

        @Override
        public long createNativeResource() {
            mToolbar.getPositionRelativeToContainer(mToolbarContainer, mTempPosition);
            mToolbarRect.set(mTempPosition[0], mTempPosition[1], mToolbarContainer.getWidth(),
                    mTempPosition[1] + mToolbar.getHeight());

            mToolbar.getLocationBarContentRect(mLocationBarRect);
            mLocationBarRect.offset(mTempPosition[0], mTempPosition[1]);

            int shadowHeight =
                    mToolbarContainer.getHeight() - mToolbar.getHeight() - mTabStripHeightPx;
            return ResourceFactory.createToolbarContainerResource(
                    mToolbarRect, mLocationBarRect, shadowHeight);
        }
    }

    @Override
    public boolean onTouchEvent(MotionEvent event) {
        // Don't eat the event if we don't have a handler.
        if (mSwipeGestureListener == null) return false;

        // Don't react on touch events if the toolbar container is not fully visible.
        if (!isToolbarContainerFullyVisible()) return true;

        // If we have ACTION_DOWN in this context, that means either no child consumed the event or
        // this class is the top UI at the event position. Then, we don't need to feed the event to
        // mGestureDetector here because the event is already once fed in onInterceptTouchEvent().
        // Moreover, we have to return true so that this class can continue to intercept all the
        // subsequent events.
        if (event.getActionMasked() == MotionEvent.ACTION_DOWN && !isOnTabStrip(event)) {
            return true;
        }

        return mSwipeGestureListener.onTouchEvent(event);
    }

    @Override
    public boolean onInterceptTouchEvent(MotionEvent event) {
        if (!isToolbarContainerFullyVisible()) return true;
        if (mSwipeGestureListener == null || isOnTabStrip(event)) return false;

        return mSwipeGestureListener.onTouchEvent(event);
    }

    private boolean isOnTabStrip(MotionEvent e) {
        return e.getY() <= mTabStripHeight;
    }

    /**
     * @return Whether or not the toolbar container is fully visible on screen.
     */
    private boolean isToolbarContainerFullyVisible() {
        return Float.compare(0f, getTranslationY()) == 0
                && mToolbarContainer.getVisibility() == VISIBLE;
    }

    private class SwipeGestureListenerImpl extends SwipeGestureListener {
        public SwipeGestureListenerImpl(Context context, SwipeHandler handler) {
            super(context, handler);
        }

        @Override
        public boolean shouldRecognizeSwipe(MotionEvent e1, MotionEvent e2) {
            if (isOnTabStrip(e1)) return false;
            if (mToolbar != null && mToolbar.shouldIgnoreSwipeGesture()) return false;
            if (KeyboardVisibilityDelegate.getInstance().isKeyboardShowing(
                        getContext(), ToolbarControlContainer.this)) {
                return false;
            }
            return true;
        }
    }
}
