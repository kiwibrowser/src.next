// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.omnibox;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Rect;
import android.util.AttributeSet;
import android.view.TouchDelegate;
import android.view.View;
import android.widget.FrameLayout;
import org.chromium.base.TraceEvent;

/**
 * A location bar implementation specific for smaller/phone screens.
 */
class LocationBarPhone extends LocationBarLayout {
    private static final int ACTION_BUTTON_TOUCH_OVERFLOW_LEFT = 15;

    private View mFirstVisibleFocusedView;
    private View mUrlBar;
    private View mStatusView;

    /**
     * Constructor used to inflate from XML.
     */
    public LocationBarPhone(Context context, AttributeSet attrs) {
        super(context, attrs);
    }

    @Override
    protected void onFinishInflate() {
        super.onFinishInflate();

        mUrlBar = findViewById(R.id.url_bar);
        mStatusView = findViewById(R.id.location_bar_status);

        Rect delegateArea = new Rect();
        mUrlActionContainer.getHitRect(delegateArea);
        delegateArea.left -= ACTION_BUTTON_TOUCH_OVERFLOW_LEFT;
        TouchDelegate touchDelegate = new TouchDelegate(delegateArea, mUrlActionContainer);
        assert mUrlActionContainer.getParent() == this;
        mCompositeTouchDelegate.addDelegateForDescendantView(touchDelegate);
    }

    @Override
    protected boolean drawChild(Canvas canvas, View child, long drawingTime) {
        boolean needsCanvasRestore = false;
        if (child == mUrlBar && mUrlActionContainer.getVisibility() == VISIBLE) {
            canvas.save();

            // Clip the URL bar contents to ensure they do not draw under the URL actions during
            // focus animations.  Based on the RTL state of the location bar, the url actions
            // container can be on the left or right side, so clip accordingly.
            if (mUrlBar.getLeft() < mUrlActionContainer.getLeft()) {
                canvas.clipRect(0, 0, (int) mUrlActionContainer.getX(), getBottom());
            } else {
                canvas.clipRect(mUrlActionContainer.getX() + mUrlActionContainer.getWidth(), 0,
                        getWidth(), getBottom());
            }
            needsCanvasRestore = true;
        }
        boolean retVal = super.drawChild(canvas, child, drawingTime);
        if (needsCanvasRestore) {
            canvas.restore();
        }
        return retVal;
    }

    @Override
    public void setShowIconsWhenUrlFocused(boolean showIcon) {
        super.setShowIconsWhenUrlFocused(showIcon);
        mFirstVisibleFocusedView = showIcon ? mStatusView : mUrlBar;
    }

    /* package */ void setFirstVisibleFocusedView(boolean toStatusView) {
        mFirstVisibleFocusedView = toStatusView ? mStatusView : mUrlBar;
        // It's possible that the fade animators hid the new first visible focused view if it was
        // to the start of the previous first visible focused view. This happens while
        // transitioning between incognito in some start surface scenarios.
        mFirstVisibleFocusedView.setAlpha(1f);
        mFirstVisibleFocusedView.setVisibility(View.VISIBLE);
    }

    @Override
    protected void onMeasure(int widthMeasureSpec, int heightMeasureSpec) {
        try (TraceEvent e = TraceEvent.scoped("LocationBarPhone.onMeasure")) {
            super.onMeasure(widthMeasureSpec, heightMeasureSpec);
        }
    }

    @Override
    protected void onLayout(boolean changed, int left, int top, int right, int bottom) {
        try (TraceEvent e = TraceEvent.scoped("LocationBarPhone.onLayout")) {
            super.onLayout(changed, left, top, right, bottom);
        }
    }

    /**
     * Returns the first child view that would be visible when location bar is focused. The first
     * visible, focused view should be either url bar or status icon.
     */
    // TODO(crbug.com/1194642): Remove the idea of firstVisibleFocusedView.
    /* package */ View getFirstVisibleFocusedView() {
        return mFirstVisibleFocusedView;
    }

    /**
     * Returns {@link FrameLayout.LayoutParams} of the LocationBar view.
     *
     * <p>TODO(1133482): Hide this View interaction if possible.
     *
     * @see View#getLayoutParams()
     */
    public FrameLayout.LayoutParams getFrameLayoutParams() {
        return (FrameLayout.LayoutParams) getLayoutParams();
    }
}
