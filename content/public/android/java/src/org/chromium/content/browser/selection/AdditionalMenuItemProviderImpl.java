// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.content.browser.selection;

import android.app.PendingIntent;
import android.app.RemoteAction;
import android.content.Context;
import android.graphics.drawable.Drawable;
import android.os.Build;
import android.text.TextUtils;
import android.view.Menu;
import android.view.MenuItem;
import android.view.View;
import android.view.View.OnClickListener;
import android.view.textclassifier.TextClassification;

import androidx.annotation.RequiresApi;

import org.chromium.base.Log;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

// TODO(ctzsm): Add unit tests for this class once this is upstreamed.
/**
 * Implements AdditionalMenuItemProvider interface.
 * We prevent inlinings since this uses a number of new Android APIs which would create verification
 * errors (on older Android versions) which would require a slow re-verification at runtime.
 */
@RequiresApi(Build.VERSION_CODES.P)
public class AdditionalMenuItemProviderImpl implements AdditionalMenuItemProvider {
    private static final String TAG = "MenuItemProvider";
    // We want the secondary assist actions to come after the default actions but before the text
    // processing actions. This constant needs to be greater than all of the default action orders
    // but small enough so that all of the secondary items have order less than
    // MENU_ITEM_ORDER_TEXT_PROCESS_START in SelectionPopupControllerImpl.
    private static final int MENU_ITEM_ORDER_SECONDARY_ASSIST_ACTIONS_START = 50;

    // Record MenuItem OnClickListener pair we added to menu.
    private final Map<MenuItem, OnClickListener> mAssistClickHandlers = new HashMap<>();

    @Override
    public void addMenuItems(
            Context context, Menu menu, TextClassification classification, List<Drawable> icons) {
        if (menu == null || classification == null) return;

        final int count = classification.getActions().size();

        assert icons == null
                || icons.size()
                        == count
            : "icons list should be either null or have the same length with actions.";

        // Fallback to new API to set icon on P.
        if (count > 0) {
            RemoteAction primaryAction = classification.getActions().get(0);

            MenuItem item = menu.findItem(android.R.id.textAssist);
            if (primaryAction.shouldShowIcon()) {
                item.setIcon(icons == null ? null : icons.get(0));
            } else {
                item.setIcon(null);
            }
        }

        // First action is reserved for primary action.
        for (int i = 1; i < count; ++i) {
            RemoteAction action = classification.getActions().get(i);
            final OnClickListener listener =
                    getSupportedOnClickListener(action.getTitle(), action.getActionIntent());
            if (listener == null) continue;

            // We have to use android.R.id.textAssist as group id to make framework show icons for
            // these menu items.
            MenuItem item = menu.add(android.R.id.textAssist, Menu.NONE,
                    MENU_ITEM_ORDER_SECONDARY_ASSIST_ACTIONS_START + i, action.getTitle());
            item.setContentDescription(action.getContentDescription());
            if (action.shouldShowIcon()) {
                item.setIcon(icons == null ? null : icons.get(i));
            }
            // Set this flag to SHOW_AS_ACTION_IF_ROOM to match text processing menu items. So
            // Android could put them to the same level and then consider their actual order.
            item.setShowAsAction(MenuItem.SHOW_AS_ACTION_IF_ROOM);
            mAssistClickHandlers.put(item, listener);
        }
    }

    @Override
    public void clearMenuItemListeners() {
        mAssistClickHandlers.clear();
    }

    @Override
    public void performAction(MenuItem item, View view) {
        OnClickListener listener = mAssistClickHandlers.get(item);
        if (listener == null) return;
        listener.onClick(view);
    }

    // Because Icon#loadDrawable() should not be called on UI thread, we pre-load the icons on
    // background thread right after we get the text classification result in
    // SmartSelectionProvider. TextClassification#getActions() is only available on P and above, so
    public static List<Drawable> loadIconDrawables(Context context, TextClassification tc) {
        if (context == null || tc == null) return null;

        ArrayList<Drawable> res = new ArrayList<>();
        for (RemoteAction action : tc.getActions()) {
            res.add(action.getIcon().loadDrawable(context));
        }
        return res;
    }

    private static OnClickListener getSupportedOnClickListener(
            CharSequence title, PendingIntent pendingIntent) {
        if (TextUtils.isEmpty(title) || pendingIntent == null) return null;

        return v -> {
            try {
                pendingIntent.send();
            } catch (PendingIntent.CanceledException e) {
                Log.e(TAG, "Error creating OnClickListener from PendingIntent", e);
            }
        };
    }
}
