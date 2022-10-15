// Copyright 2013 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chromoting;

import android.content.Context;
import android.view.MotionEvent;
import android.view.ViewConfiguration;

import androidx.annotation.IntDef;

import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;

/**
 * Helper class for disambiguating whether to treat a two-finger gesture as a swipe or a pinch.
 * Initially, the status will be unknown, until the fingers have moved sufficiently far to
 * determine the intent.
 */
public class SwipePinchDetector {
    /** Current state of the gesture. */
    @IntDef({State.UNKNOWN, State.SWIPE, State.PINCH})
    @Retention(RetentionPolicy.SOURCE)
    private @interface State {
        int UNKNOWN = 0;
        int SWIPE = 1;
        int PINCH = 2;
    }
    private @State int mState = State.UNKNOWN;

    /** Initial coordinates of the two pointers in the current gesture. */
    private float mFirstX0;
    private float mFirstY0;
    private float mFirstX1;
    private float mFirstY1;

    /**
     * The initial coordinates above are valid when this flag is set. Used to determine whether a
     * MotionEvent's pointer coordinates are the first ones of the gesture.
     */
    private boolean mInGesture;

    /**
     * Threshold squared-distance, in pixels, to use for motion-detection.
     */
    private int mTouchSlopSquare;

    private void reset() {
        mState = State.UNKNOWN;
        mInGesture = false;
    }

    /** Construct a new detector, using the context to determine movement thresholds. */
    public SwipePinchDetector(Context context) {
        ViewConfiguration config = ViewConfiguration.get(context);
        int touchSlop = config.getScaledTouchSlop();
        mTouchSlopSquare = touchSlop * touchSlop;
    }

    /** Returns whether a swipe is in progress. */
    public boolean isSwiping() {
        return mState == State.SWIPE;
    }

    /** Returns whether a pinch is in progress. */
    public boolean isPinching() {
        return mState == State.PINCH;
    }

    /**
     * Analyzes the touch event to determine whether the user is swiping or pinching. Only
     * motion events with 2 pointers are considered here. Once the gesture is determined to be a
     * swipe or a pinch, further 2-finger motion-events will be ignored. When a different event is
     * passed in (motion event with != 2 pointers, or some other event type), this object will
     * revert back to the original UNKNOWN state.
     */
    public void onTouchEvent(MotionEvent event) {
        if (event.getPointerCount() != 2) {
            reset();
            return;
        }

        // Only MOVE or DOWN events are considered - all other events should finish any current
        // gesture and reset the detector. In addition, a DOWN event should reset the detector,
        // since it signals the start of the gesture. If the events are consistent, a DOWN event
        // will occur at the start of the gesture, but this implementation tries to cope in case
        // the first event is MOVE rather than DOWN.
        int action = event.getActionMasked();
        if (action != MotionEvent.ACTION_MOVE) {
            reset();
            if (action != MotionEvent.ACTION_POINTER_DOWN) {
                return;
            }
        }

        // If the gesture is known, there is no need for further processing - the state should
        // remain the same until the gesture is complete, as tested above.
        if (mState != State.UNKNOWN) {
            return;
        }

        float currentX0 = event.getX(0);
        float currentY0 = event.getY(0);
        float currentX1 = event.getX(1);
        float currentY1 = event.getY(1);
        if (!mInGesture) {
            // This is the first event of the gesture, so store the pointer coordinates.
            mFirstX0 = currentX0;
            mFirstY0 = currentY0;
            mFirstX1 = currentX1;
            mFirstY1 = currentY1;
            mInGesture = true;
            return;
        }

        float deltaX0 = currentX0 - mFirstX0;
        float deltaY0 = currentY0 - mFirstY0;
        float deltaX1 = currentX1 - mFirstX1;
        float deltaY1 = currentY1 - mFirstY1;

        float squaredDistance0 = deltaX0 * deltaX0 + deltaY0 * deltaY0;
        float squaredDistance1 = deltaX1 * deltaX1 + deltaY1 * deltaY1;


        // If both fingers have moved beyond the touch-slop, it is safe to recognize the gesture.
        // However, one finger might be held stationary whilst the other finger is moved a long
        // distance. In this case, it is preferable to trigger a PINCH. This should be detected
        // soon enough to avoid triggering a sudden large change in the zoom level, but not so
        // soon that SWIPE never gets triggered.

        // Threshold level for triggering the PINCH gesture if one finger is stationary. This
        // cannot be equal to the touch-slop, because in that case, SWIPE would rarely be detected.
        // One finger would usually leave the touch-slop radius slightly before the other finger,
        // triggering a PINCH as described above. A larger radius gives an opportunity for
        // SWIPE to be detected. Doubling the radius is an arbitrary choice that works well.
        int pinchThresholdSquare = 4 * mTouchSlopSquare;

        boolean finger0Moved = squaredDistance0 > mTouchSlopSquare;
        boolean finger1Moved = squaredDistance1 > mTouchSlopSquare;

        if (!finger0Moved && !finger1Moved) {
            return;
        }

        if (finger0Moved && !finger1Moved) {
            if (squaredDistance0 > pinchThresholdSquare) {
                mState = State.PINCH;
            }
            return;
        }

        if (!finger0Moved && finger1Moved) {
            if (squaredDistance1 > pinchThresholdSquare) {
                mState = State.PINCH;
            }
            return;
        }

        // Both fingers have moved, so determine SWIPE/PINCH status. If the fingers have moved in
        // the same direction, this is a SWIPE, otherwise it's a PINCH. This can be measured by
        // taking the scalar product of the direction vectors. This product is positive if the
        // vectors are pointing in the same direction, and negative if they're in opposite
        // directions.
        float scalarProduct = deltaX0 * deltaX1 + deltaY0 * deltaY1;
        mState = (scalarProduct > 0) ? State.SWIPE : State.PINCH;
    }
}
