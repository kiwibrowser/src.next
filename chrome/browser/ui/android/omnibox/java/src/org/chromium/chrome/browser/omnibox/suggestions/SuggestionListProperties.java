// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.omnibox.suggestions;

import org.chromium.ui.modelutil.MVCListAdapter.ModelList;
import org.chromium.ui.modelutil.PropertyKey;
import org.chromium.ui.modelutil.PropertyModel.WritableBooleanPropertyKey;
import org.chromium.ui.modelutil.PropertyModel.WritableIntPropertyKey;
import org.chromium.ui.modelutil.PropertyModel.WritableObjectPropertyKey;

/**
 * The properties controlling the state of the list of suggestion items.
 */
public class SuggestionListProperties {
    /** Whether the suggestion list is visible. */
    public static final WritableBooleanPropertyKey VISIBLE = new WritableBooleanPropertyKey();

    /** The embedder for the suggestion list. */
    public static final WritableObjectPropertyKey<OmniboxSuggestionsDropdownEmbedder> EMBEDDER =
            new WritableObjectPropertyKey<>();

    /**
     * The list of models controlling the state of the suggestion items. This should never be
     * bound to the same view more than once.
     */
    public static final WritableObjectPropertyKey<ModelList> SUGGESTION_MODELS =
            new WritableObjectPropertyKey<>(true);

    /**
     * Whether the list encompasses the final set of suggestions for the current user query.
     */
    public static final WritableBooleanPropertyKey LIST_IS_FINAL = new WritableBooleanPropertyKey();

    /**
     * Specifies the color scheme. It can be light or dark because of a publisher defined color,
     * incognito, or the default theme that follows dynamic colors.
     */
    public static final WritableIntPropertyKey COLOR_SCHEME = new WritableIntPropertyKey();

    /** Observer that will receive notifications and callbacks from Suggestion List. */
    public static final WritableObjectPropertyKey<OmniboxSuggestionsDropdown.Observer> OBSERVER =
            new WritableObjectPropertyKey<>();

    public static final PropertyKey[] ALL_KEYS = new PropertyKey[] {
            VISIBLE, EMBEDDER, SUGGESTION_MODELS, COLOR_SCHEME, OBSERVER, LIST_IS_FINAL};
}
