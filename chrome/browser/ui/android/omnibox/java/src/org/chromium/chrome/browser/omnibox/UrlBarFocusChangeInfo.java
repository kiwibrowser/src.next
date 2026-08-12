// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.omnibox;

import android.view.View;

import androidx.annotation.IntDef;

import org.chromium.build.annotations.NullMarked;

import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;

/** Encapsulates a UrlBar focus change, including the direction focus arrived from. */
@NullMarked
public class UrlBarFocusChangeInfo {
    @IntDef({
        View.FOCUS_BACKWARD,
        View.FOCUS_FORWARD,
        View.FOCUS_LEFT,
        View.FOCUS_UP,
        View.FOCUS_RIGHT,
        View.FOCUS_DOWN
    })
    @Retention(RetentionPolicy.SOURCE)
    public @interface FocusDirection {}

    public final boolean hasFocus;

    /** The focus traversal direction. Only meaningful when {@link #hasFocus} is true. */
    public final @FocusDirection int direction;

    public UrlBarFocusChangeInfo(boolean hasFocus, @FocusDirection int direction) {
        this.hasFocus = hasFocus;
        this.direction = direction;
    }
}
