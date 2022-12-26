// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.toolbar.top;

import android.os.Build;
import android.view.ActionMode;
import android.view.Menu;
import android.view.MenuItem;

/**
 * A custom ActionMode.Callback that handles copy, paste selection in omnibox and toolbar.
 */
public class ToolbarActionModeCallback implements ActionMode.Callback {
    private ActionModeController mActionModeController;

    private boolean mMovedToolbar;

    /**
     * Sets the {@link #mActionModeController}.
     */
    public void setActionModeController(ActionModeController actionModeController) {
        mActionModeController = actionModeController;
    }

    @Override
    public boolean onPrepareActionMode(ActionMode mode, Menu menu) {
        return true;
    }

    @Override
    public void onDestroyActionMode(ActionMode mode) {
        ensureValidToolbarVisibility(false);
    }

    @Override
    public boolean onCreateActionMode(ActionMode mode, Menu menu) {
        ensureValidToolbarVisibility(!isFloatingActionMode(mode));
        return true;
    }

    private void ensureValidToolbarVisibility(boolean shouldBeMoved) {
        if (shouldBeMoved == mMovedToolbar) return;

        if (shouldBeMoved) {
            mActionModeController.startShowAnimation();
        } else {
            mActionModeController.startHideAnimation();
        }
        mMovedToolbar = shouldBeMoved;
    }

    @Override
    public boolean onActionItemClicked(ActionMode mode, MenuItem item) {
        return false;
    }

    private static boolean isFloatingActionMode(ActionMode mode) {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.M) return false;

        return mode.getType() == ActionMode.TYPE_FLOATING;
    }
}
