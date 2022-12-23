// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.omnibox.suggestions;

import android.content.Context;
import android.os.Handler;
import android.view.KeyEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.ViewStub;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.annotation.VisibleForTesting;
import androidx.core.view.ViewCompat;

import org.chromium.base.Callback;
import org.chromium.base.StrictModeContext;
import org.chromium.base.jank_tracker.JankTracker;
import org.chromium.base.supplier.ObservableSupplier;
import org.chromium.base.supplier.Supplier;
import org.chromium.chrome.browser.omnibox.LocationBarDataProvider;
import org.chromium.chrome.browser.omnibox.R;
import org.chromium.chrome.browser.omnibox.UrlBar.UrlTextChangeListener;
import org.chromium.chrome.browser.omnibox.UrlBarEditingTextStateProvider;
import org.chromium.chrome.browser.omnibox.UrlFocusChangeListener;
import org.chromium.chrome.browser.omnibox.suggestions.AutocompleteController.OnSuggestionsReceivedListener;
import org.chromium.chrome.browser.omnibox.suggestions.SuggestionListViewBinder.SuggestionListViewHolder;
import org.chromium.chrome.browser.omnibox.suggestions.answer.AnswerSuggestionViewBinder;
import org.chromium.chrome.browser.omnibox.suggestions.base.BaseSuggestionView;
import org.chromium.chrome.browser.omnibox.suggestions.base.BaseSuggestionViewBinder;
import org.chromium.chrome.browser.omnibox.suggestions.basic.BasicSuggestionProcessor.BookmarkState;
import org.chromium.chrome.browser.omnibox.suggestions.basic.SuggestionViewViewBinder;
import org.chromium.chrome.browser.omnibox.suggestions.carousel.BaseCarouselSuggestionItemViewBuilder;
import org.chromium.chrome.browser.omnibox.suggestions.carousel.BaseCarouselSuggestionViewBinder;
import org.chromium.chrome.browser.omnibox.suggestions.editurl.EditUrlSuggestionView;
import org.chromium.chrome.browser.omnibox.suggestions.editurl.EditUrlSuggestionViewBinder;
import org.chromium.chrome.browser.omnibox.suggestions.entity.EntitySuggestionViewBinder;
import org.chromium.chrome.browser.omnibox.suggestions.header.HeaderView;
import org.chromium.chrome.browser.omnibox.suggestions.header.HeaderViewBinder;
import org.chromium.chrome.browser.omnibox.suggestions.pedal.PedalSuggestionView;
import org.chromium.chrome.browser.omnibox.suggestions.pedal.PedalSuggestionViewBinder;
import org.chromium.chrome.browser.omnibox.suggestions.tail.TailSuggestionView;
import org.chromium.chrome.browser.omnibox.suggestions.tail.TailSuggestionViewBinder;
import org.chromium.chrome.browser.omnibox.voice.VoiceRecognitionHandler;
import org.chromium.chrome.browser.profiles.Profile;
import org.chromium.chrome.browser.share.ShareDelegate;
import org.chromium.chrome.browser.tab.Tab;
import org.chromium.chrome.browser.tabmodel.TabWindowManager;
import org.chromium.chrome.browser.ui.theme.BrandedColorScheme;
import org.chromium.chrome.browser.util.KeyNavigationUtil;
import org.chromium.components.omnibox.AutocompleteMatch;
import org.chromium.ui.ViewProvider;
import org.chromium.ui.modaldialog.ModalDialogManager;
import org.chromium.ui.modelutil.LazyConstructionPropertyMcp;
import org.chromium.ui.modelutil.MVCListAdapter;
import org.chromium.ui.modelutil.MVCListAdapter.ModelList;
import org.chromium.ui.modelutil.PropertyModel;

import java.util.ArrayList;
import java.util.List;

/**
 * Coordinator that handles the interactions with the autocomplete system.
 */
public class AutocompleteCoordinator implements UrlFocusChangeListener, UrlTextChangeListener {
    private final @NonNull ViewGroup mParent;
    private final @NonNull ObservableSupplier<Profile> mProfileSupplier;
    private final @NonNull Callback<Profile> mProfileChangeCallback;
    private final @NonNull AutocompleteMediator mMediator;
    private final @NonNull Supplier<ModalDialogManager> mModalDialogManagerSupplier;
    private @Nullable OmniboxSuggestionsDropdown mDropdown;

    public AutocompleteCoordinator(@NonNull ViewGroup parent,
            @NonNull AutocompleteDelegate delegate,
            @NonNull OmniboxSuggestionsDropdownEmbedder dropdownEmbedder,
            @NonNull UrlBarEditingTextStateProvider urlBarEditingTextProvider,
            @NonNull Supplier<ModalDialogManager> modalDialogManagerSupplier,
            @NonNull Supplier<Tab> activityTabSupplier,
            @Nullable Supplier<ShareDelegate> shareDelegateSupplier,
            @NonNull LocationBarDataProvider locationBarDataProvider,
            @NonNull ObservableSupplier<Profile> profileObservableSupplier,
            @NonNull Callback<Tab> bringToForegroundCallback,
            @NonNull Supplier<TabWindowManager> tabWindowManagerSupplier,
            @NonNull BookmarkState bookmarkState, @NonNull JankTracker jankTracker,
            @NonNull OmniboxPedalDelegate omniboxPedalDelegate) {
        mParent = parent;
        mModalDialogManagerSupplier = modalDialogManagerSupplier;
        Context context = parent.getContext();

        PropertyModel listModel = new PropertyModel(SuggestionListProperties.ALL_KEYS);
        ModelList listItems = new ModelList();

        listModel.set(SuggestionListProperties.EMBEDDER, dropdownEmbedder);
        listModel.set(SuggestionListProperties.VISIBLE, false);
        listModel.set(SuggestionListProperties.SUGGESTION_MODELS, listItems);

        mMediator = new AutocompleteMediator(context, delegate, urlBarEditingTextProvider,
                listModel, new Handler(), modalDialogManagerSupplier, activityTabSupplier,
                shareDelegateSupplier, locationBarDataProvider, bringToForegroundCallback,
                tabWindowManagerSupplier, bookmarkState, jankTracker, omniboxPedalDelegate);
        mMediator.initDefaultProcessors();

        listModel.set(SuggestionListProperties.OBSERVER, mMediator);

        ViewProvider<SuggestionListViewHolder> viewProvider =
                createViewProvider(context, listItems);
        viewProvider.whenLoaded((holder) -> { mDropdown = holder.dropdown; });
        LazyConstructionPropertyMcp.create(listModel, SuggestionListProperties.VISIBLE,
                viewProvider, SuggestionListViewBinder::bind);

        mProfileSupplier = profileObservableSupplier;
        mProfileChangeCallback = this::setAutocompleteProfile;
        mProfileSupplier.addObserver(mProfileChangeCallback);

        // https://crbug.com/966227 Set initial layout direction ahead of inflating the suggestions.
        updateSuggestionListLayoutDirection();
    }

    /**
     * Clean up resources used by this class.
     */
    public void destroy() {
        mProfileSupplier.removeObserver(mProfileChangeCallback);
        mMediator.destroy();
        if (mDropdown != null) {
            mDropdown.destroy();
            mDropdown = null;
        }
    }

    private ViewProvider<SuggestionListViewHolder> createViewProvider(
            Context context, MVCListAdapter.ModelList modelList) {
        return new ViewProvider<SuggestionListViewHolder>() {
            private List<Callback<SuggestionListViewHolder>> mCallbacks = new ArrayList<>();
            private SuggestionListViewHolder mHolder;

            @Override
            public void inflate() {
                OmniboxSuggestionsDropdown dropdown;
                try (StrictModeContext ignored = StrictModeContext.allowDiskReads()) {
                    dropdown = new OmniboxSuggestionsDropdown(context);
                }

                // Start with visibility GONE to ensure that show() is called.
                // http://crbug.com/517438
                dropdown.getViewGroup().setVisibility(View.GONE);
                dropdown.getViewGroup().setClipToPadding(false);

                OmniboxSuggestionsDropdownAdapter adapter =
                        new OmniboxSuggestionsDropdownAdapter(modelList);
                dropdown.setAdapter(adapter);

                // Note: clang-format does a bad job formatting lambdas so we turn it off here.
                // clang-format off
                // Register a view type for a default omnibox suggestion.
                adapter.registerType(
                        OmniboxSuggestionUiType.DEFAULT,
                        parent -> new BaseSuggestionView<View>(
                                parent.getContext(), R.layout.omnibox_basic_suggestion),
                        new BaseSuggestionViewBinder<View>(SuggestionViewViewBinder::bind));

                adapter.registerType(
                        OmniboxSuggestionUiType.EDIT_URL_SUGGESTION,
                        parent -> new EditUrlSuggestionView(parent.getContext()),
                        new EditUrlSuggestionViewBinder());

                adapter.registerType(
                        OmniboxSuggestionUiType.ANSWER_SUGGESTION,
                        parent -> new BaseSuggestionView<View>(
                                parent.getContext(), R.layout.omnibox_answer_suggestion),
                        new BaseSuggestionViewBinder<View>(AnswerSuggestionViewBinder::bind));

                adapter.registerType(
                        OmniboxSuggestionUiType.ENTITY_SUGGESTION,
                        parent -> new BaseSuggestionView<View>(
                                parent.getContext(), R.layout.omnibox_entity_suggestion),
                        new BaseSuggestionViewBinder<View>(EntitySuggestionViewBinder::bind));

                adapter.registerType(
                        OmniboxSuggestionUiType.TAIL_SUGGESTION,
                        parent -> new BaseSuggestionView<TailSuggestionView>(
                                new TailSuggestionView(parent.getContext())),
                        new BaseSuggestionViewBinder<TailSuggestionView>(
                                TailSuggestionViewBinder::bind));

                adapter.registerType(
                        OmniboxSuggestionUiType.CLIPBOARD_SUGGESTION,
                        parent -> new BaseSuggestionView<View>(
                                parent.getContext(), R.layout.omnibox_basic_suggestion),
                        new BaseSuggestionViewBinder<View>(SuggestionViewViewBinder::bind));

                adapter.registerType(
                        OmniboxSuggestionUiType.TILE_NAVSUGGEST,
                        BaseCarouselSuggestionItemViewBuilder::createView,
                        BaseCarouselSuggestionViewBinder::bind);

                adapter.registerType(
                        OmniboxSuggestionUiType.HEADER,
                        parent -> new HeaderView(parent.getContext()),
                        HeaderViewBinder::bind);

                adapter.registerType(
                        OmniboxSuggestionUiType.PEDAL_SUGGESTION,
                        parent -> new PedalSuggestionView<View>(
                                parent.getContext(), R.layout.omnibox_basic_suggestion),
                        new PedalSuggestionViewBinder<View>(SuggestionViewViewBinder::bind));
                // clang-format on

                ViewGroup container = (ViewGroup) ((ViewStub) mParent.getRootView().findViewById(
                                                           R.id.omnibox_results_container_stub))
                                              .inflate();

                mHolder = new SuggestionListViewHolder(container, dropdown);
                for (int i = 0; i < mCallbacks.size(); i++) {
                    mCallbacks.get(i).onResult(mHolder);
                }
                mCallbacks = null;
            }

            @Override
            public void whenLoaded(Callback<SuggestionListViewHolder> callback) {
                if (mHolder != null) {
                    callback.onResult(mHolder);
                    return;
                }
                mCallbacks.add(callback);
            }
        };
    }

    @Override
    public void onUrlFocusChange(boolean hasFocus) {
        mMediator.onUrlFocusChange(hasFocus);
    }

    @Override
    public void onUrlAnimationFinished(boolean hasFocus) {
        mMediator.onUrlAnimationFinished(hasFocus);
    }

    /**
     * Updates the profile used for generating autocomplete suggestions.
     * @param profile The profile to be used.
     */
    @VisibleForTesting(otherwise = VisibleForTesting.PRIVATE)
    public void setAutocompleteProfile(Profile profile) {
        mMediator.setAutocompleteProfile(profile);
    }

    /**
     * Whether omnibox autocomplete should currently be prevented from generating suggestions.
     */
    public void setShouldPreventOmniboxAutocomplete(boolean prevent) {
        mMediator.setShouldPreventOmniboxAutocomplete(prevent);
    }

    /**
     * @return The number of current autocomplete suggestions.
     */
    public int getSuggestionCount() {
        return mMediator.getSuggestionCount();
    }

    /**
     * Retrieve the omnibox suggestion at the specified index.  The index represents the ordering
     * in the underlying model.  The index does not represent visibility due to the current scroll
     * position of the list.
     *
     * @param index The index of the suggestion to fetch.
     * @return The suggestion at the given index.
     */
    public AutocompleteMatch getSuggestionAt(int index) {
        return mMediator.getSuggestionAt(index);
    }

    /**
     * Signals that native initialization has completed.
     */
    public void onNativeInitialized() {
        mMediator.onNativeInitialized();
    }

    /**
     * @see AutocompleteController#onVoiceResults(List)
     */
    public void onVoiceResults(@Nullable List<VoiceRecognitionHandler.VoiceResult> results) {
        mMediator.onVoiceResults(results);
    }

    /**
     * @return The current native pointer to the autocomplete results.
     * TODO(ender): Figure out how to remove this.
     */
    public long getCurrentNativeAutocompleteResult() {
        return mMediator.getCurrentNativeAutocompleteResult();
    }

    /**
     * Update the layout direction of the suggestion list based on the parent layout direction.
     */
    public void updateSuggestionListLayoutDirection() {
        mMediator.setLayoutDirection(ViewCompat.getLayoutDirection(mParent));
    }

    /**
     * Update the visuals of the autocomplete UI.
     * @param brandedColorScheme The {@link @BrandedColorScheme}.
     */
    public void updateVisualsForState(@BrandedColorScheme int brandedColorScheme) {
        mMediator.updateVisualsForState(brandedColorScheme);
    }

    /**
     * Show cached zero suggest results.
     * Enables Autocomplete subsystem to offer most recently presented suggestions in the event
     * where Native counterpart is not yet initialized.
     */
    public void startCachedZeroSuggest() {
        mMediator.startCachedZeroSuggest();
    }

    /**
     * Handle the key events associated with the suggestion list.
     *
     * @param keyCode The keycode representing what key was interacted with.
     * @param event The key event containing all meta-data associated with the event.
     * @return Whether the key event was handled.
     */
    public boolean handleKeyEvent(int keyCode, KeyEvent event) {
        // Note: this method receives key events for key presses and key releases.
        // Make sure we focus only on key press events alone.
        if (!KeyNavigationUtil.isActionDown(event)) {
            return false;
        }

        boolean isShowingList = mDropdown != null && mDropdown.getViewGroup().isShown();
        boolean isAnyDirection = KeyNavigationUtil.isGoAnyDirection(event);

        if (isShowingList && mMediator.getSuggestionCount() > 0 && isAnyDirection) {
            mMediator.allowPendingItemSelection();
        }
        if (isShowingList && mDropdown.getViewGroup().onKeyDown(keyCode, event)) {
            return true;
        }
        if (KeyNavigationUtil.isEnter(event) && mParent.getVisibility() == View.VISIBLE) {
            mMediator.loadTypedOmniboxText(event.getEventTime());
            return true;
        }
        return false;
    }

    @Override
    public void onTextChanged(String textWithoutAutocomplete, String textWithAutocomplete) {
        mMediator.onTextChanged(textWithoutAutocomplete, textWithAutocomplete);
    }

    /**
     * Trigger autocomplete for the given query.
     */
    public void startAutocompleteForQuery(String query) {
        mMediator.startAutocompleteForQuery(query);
    }

    /**
     * Given a search query, this will attempt to see if the query appears to be portion of a
     * properly formed URL.  If it appears to be a URL, this will return the fully qualified
     * version (i.e. including the scheme, etc...).  If the query does not appear to be a URL,
     * this will return null.
     *
     * Note:
     * 1) This call is expensive. Use only when it is absolutely necessary to get the exact
     *    information about how a given query string will be interpreted. For less restrictive
     *    URL vs text matching, please defer to GURL.
     * 2) this updates the internal state of the autocomplete controller just as start() does.
     *    Future calls that reference autocomplete results by index, e.g. onSuggestionSelected(),
     *    should reference the returned suggestion by index 0.
     *
     * TODO(crbug.com/966424): Fix the dependency issue and remove this method.
     *                       Please don't use this in any new code.
     *
     * @param profile The profile to expand the query for.
     * @param query The query to be expanded into a fully qualified URL if appropriate.
     * @return The AutocompleteMatch for a default / top match. This may be either SEARCH
     *         match built with the user's default search engine, or a NAVIGATION match.
     */
    @Deprecated
    public static AutocompleteMatch classify(@NonNull Profile profile, @NonNull String query) {
        return AutocompleteController.getForProfile(profile).classify(query, false);
    }

    /**
     * Sends a zero suggest request to the server in order to pre-populate the result cache.
     */
    public void prefetchZeroSuggestResults() {
        mMediator.startPrefetch();
    }

    /** @return Suggestions Dropdown view, showing the list of suggestions. */
    @VisibleForTesting(otherwise = VisibleForTesting.PRIVATE)
    public OmniboxSuggestionsDropdown getSuggestionsDropdownForTest() {
        return mDropdown;
    }

    /** @return The current receiving OnSuggestionsReceived events. */
    @VisibleForTesting(otherwise = VisibleForTesting.PRIVATE)
    public OnSuggestionsReceivedListener getSuggestionsReceivedListenerForTest() {
        return mMediator;
    }

    /** @return The ModelList for the currently shown suggestions. */
    @VisibleForTesting(otherwise = VisibleForTesting.PRIVATE)
    public ModelList getSuggestionModelListForTest() {
        return mMediator.getSuggestionModelListForTest();
    }

    @VisibleForTesting
    public @NonNull ModalDialogManager getModalDialogManagerForTest() {
        assert mModalDialogManagerSupplier.hasValue();
        return mModalDialogManagerSupplier.get();
    }

    @VisibleForTesting
    public void stopAutocompleteForTest(boolean clearResults) {
        mMediator.stopAutocomplete(clearResults);
    }
}
