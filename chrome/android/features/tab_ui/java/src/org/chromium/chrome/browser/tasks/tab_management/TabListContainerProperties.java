// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.tasks.tab_management;

import org.chromium.ui.modelutil.PropertyKey;
import org.chromium.ui.modelutil.PropertyModel;

class TabListContainerProperties {
    public static final PropertyModel.WritableBooleanPropertyKey IS_VISIBLE =
            new PropertyModel.WritableBooleanPropertyKey();

    public static final PropertyModel.WritableBooleanPropertyKey IS_INCOGNITO =
            new PropertyModel.WritableBooleanPropertyKey();

    public static final PropertyModel
            .WritableObjectPropertyKey<TabListRecyclerView.VisibilityListener> VISIBILITY_LISTENER =
            new PropertyModel.WritableObjectPropertyKey<>();

    /**
     * Integer, but not {@link PropertyModel.WritableIntPropertyKey} so that we can force update on
     * the same value.
     */
    public static final PropertyModel.WritableObjectPropertyKey<Integer> INITIAL_SCROLL_INDEX =
            new PropertyModel.WritableObjectPropertyKey<>(true);

    public static final PropertyModel.WritableBooleanPropertyKey ANIMATE_VISIBILITY_CHANGES =
            new PropertyModel.WritableBooleanPropertyKey();

    public static final PropertyModel.WritableIntPropertyKey TOP_MARGIN =
            new PropertyModel.WritableIntPropertyKey();

    public static final PropertyModel.WritableIntPropertyKey BOTTOM_CONTROLS_HEIGHT =
            new PropertyModel.WritableIntPropertyKey();

    public static final PropertyModel.WritableIntPropertyKey SHADOW_TOP_OFFSET =
            new PropertyModel.WritableIntPropertyKey();

    public static final PropertyModel.WritableIntPropertyKey BOTTOM_PADDING =
            new PropertyModel.WritableIntPropertyKey();

    /**
     * A property which controls whether to use the default animator specified in the underlying
     * {@link TabListRecyclerView} or use a null item animator.
     *
     * TODO(crbug.com/1227656): This property is used only by the Incognito re-auth client and once
     * the re-auth integration with tab-switcher design is further improved then remove this if no
     * other clients use this property.
     */
    public static final PropertyModel.WritableBooleanPropertyKey TAB_LIST_ITEM_ANIMATOR_ENABLED =
            new PropertyModel.WritableBooleanPropertyKey();

    public static final PropertyKey[] ALL_KEYS =
            new PropertyKey[] {IS_VISIBLE, IS_INCOGNITO, VISIBILITY_LISTENER, INITIAL_SCROLL_INDEX,
                    ANIMATE_VISIBILITY_CHANGES, TOP_MARGIN, BOTTOM_CONTROLS_HEIGHT,
                    SHADOW_TOP_OFFSET, BOTTOM_PADDING, TAB_LIST_ITEM_ANIMATOR_ENABLED};
}
