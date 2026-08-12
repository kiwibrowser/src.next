// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.tasks.tab_management;

import android.view.View;

import org.chromium.build.annotations.NullMarked;
import org.chromium.ui.modelutil.PropertyKey;
import org.chromium.ui.modelutil.PropertyModel;

/** ViewBinder and ViewHolder for the Tab Search Overlay component. */
@NullMarked
public class TabSearchOverlayViewBinder {
    /** Helper class that holds references to the underlying Android views. */
    public static class ViewHolder {
        public final View panelContainer;
        public final View scrim;

        /**
         * Constructs a new ViewHolder holding the inflated views.
         *
         * @param panelContainer The root container layout for the search overlay.
         * @param scrim The background scrim view used to dismiss the overlay.
         */
        public ViewHolder(View panelContainer, View scrim) {
            this.panelContainer = panelContainer;
            this.scrim = scrim;
        }
    }

    /** Binds properties from the PropertyModel to the ViewHolder. */
    public static void bind(PropertyModel model, ViewHolder view, PropertyKey propertyKey) {
        if (TabSearchOverlayProperties.VISIBLE == propertyKey) {
            view.panelContainer.setVisibility(
                    model.get(TabSearchOverlayProperties.VISIBLE) ? View.VISIBLE : View.GONE);
        } else if (TabSearchOverlayProperties.ON_SCRIM_CLICK == propertyKey) {
            view.scrim.setOnClickListener(model.get(TabSearchOverlayProperties.ON_SCRIM_CLICK));
        }
    }
}
