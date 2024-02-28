// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.content.browser.selection;

import android.content.Context;
import android.graphics.Rect;
import android.view.ActionMode;
import android.view.Menu;
import android.view.MenuItem;
import android.view.View;

import androidx.annotation.Nullable;

import org.chromium.content.R;
import org.chromium.content.browser.selection.SelectActionMenuHelper.SelectActionMenuDelegate;
import org.chromium.content_public.browser.SelectionMenuGroup;
import org.chromium.content_public.browser.selection.SelectionActionMenuDelegate;
import org.chromium.ui.base.DeviceFormFactor;

import java.util.HashMap;
import java.util.Map;
import java.util.SortedSet;

/** Paste popup implementation based on floating ActionModes. */
// TODO(crbug.com/1468921): Merge this class with SelectionPopupControllerImpl and remove.
public class FloatingPastePopupMenu implements PastePopupMenu {
    private final View mParent;
    private final PastePopupMenuDelegate mDelegate;
    private final Context mContext;

    private ActionMode mActionMode;
    private Rect mSelectionRect;
    private final @Nullable SelectionActionMenuDelegate mSelectionActionMenuDelegate;
    private final Map<MenuItem, View.OnClickListener> mCustomMenuItemClickListeners;

    public FloatingPastePopupMenu(
            Context context,
            View parent,
            PastePopupMenuDelegate delegate,
            @Nullable SelectionActionMenuDelegate selectionActionMenuDelegate) {
        mParent = parent;
        mDelegate = delegate;
        mContext = context;
        mSelectionActionMenuDelegate = selectionActionMenuDelegate;
        mCustomMenuItemClickListeners = new HashMap<>();
    }

    @Override
    public void show(Rect selectionRect) {
        mSelectionRect = selectionRect;
        if (mActionMode != null) {
            mActionMode.invalidateContentRect();
            return;
        }

        ensureActionMode();
    }

    @Override
    public void hide() {
        if (mActionMode != null) {
            mActionMode.finish();
            mActionMode = null;
        }
    }

    private void ensureActionMode() {
        if (mActionMode != null) return;

        ActionMode actionMode =
                mParent.startActionMode(new ActionModeCallback(), ActionMode.TYPE_FLOATING);
        if (actionMode != null) {
            // crbug.com/651706
            LGEmailActionModeWorkaroundImpl.runIfNecessary(mContext, actionMode);

            assert actionMode.getType() == ActionMode.TYPE_FLOATING;
            mActionMode = actionMode;
        }
    }

    private class ActionModeCallback extends ActionMode.Callback2 {
        @Override
        public boolean onCreateActionMode(ActionMode mode, Menu menu) {
            createPasteMenu(mode, menu);
            return true;
        }

        private void createPasteMenu(ActionMode mode, Menu menu) {
            mode.setTitle(
                    DeviceFormFactor.isNonMultiDisplayContextOnTablet(mContext)
                            ? mContext.getString(R.string.actionbar_textselection_title)
                            : null);
            mode.setSubtitle(null);
            SelectActionMenuDelegate actionMenuDelegate =
                    new SelectActionMenuDelegate() {
                        @Override
                        public boolean canCut() {
                            return false;
                        }

                        @Override
                        public boolean canCopy() {
                            return false;
                        }

                        @Override
                        public boolean canPaste() {
                            return mDelegate.canPaste();
                        }

                        @Override
                        public boolean canShare() {
                            return false;
                        }

                        @Override
                        public boolean canSelectAll() {
                            return mDelegate.canSelectAll();
                        }

                        @Override
                        public boolean canWebSearch() {
                            return false;
                        }

                        @Override
                        public boolean canPasteAsPlainText() {
                            return mDelegate.canPasteAsPlainText();
                        }
                    };
            SortedSet<SelectionMenuGroup> nonSelectionMenuItems =
                    SelectActionMenuHelper.getNonSelectionMenuItems(
                            mContext, actionMenuDelegate, mSelectionActionMenuDelegate);
            SelectionPopupControllerImpl.initializeActionMenu(
                    mContext, nonSelectionMenuItems, menu, mCustomMenuItemClickListeners, null);
        }

        @Override
        public boolean onPrepareActionMode(ActionMode mode, Menu menu) {
            mCustomMenuItemClickListeners.clear();
            return false;
        }

        @Override
        public boolean onActionItemClicked(ActionMode mode, MenuItem item) {
            View.OnClickListener customMenuItemClickListener =
                    mCustomMenuItemClickListeners.get(item);
            if (customMenuItemClickListener != null) {
                customMenuItemClickListener.onClick(mParent);
            } else {
                int id = item.getItemId();
                if (id == R.id.select_action_menu_paste) {
                    mDelegate.paste();
                    mode.finish();
                } else if (id == R.id.select_action_menu_paste_as_plain_text) {
                    mDelegate.pasteAsPlainText();
                    mode.finish();
                } else if (id == R.id.select_action_menu_select_all) {
                    mDelegate.selectAll();
                    mode.finish();
                }
            }
            return true;
        }

        @Override
        public void onDestroyActionMode(ActionMode mode) {
            mActionMode = null;
        }

        @Override
        public void onGetContentRect(ActionMode mode, View view, Rect outRect) {
            outRect.set(mSelectionRect);
        }
    }
    ;
}
