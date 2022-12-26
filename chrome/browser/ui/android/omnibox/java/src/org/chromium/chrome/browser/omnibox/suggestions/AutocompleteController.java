// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.omnibox.suggestions;

import android.text.TextUtils;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.annotation.VisibleForTesting;

import org.chromium.base.annotations.CalledByNative;
import org.chromium.base.annotations.NativeMethods;
import org.chromium.chrome.browser.omnibox.voice.VoiceRecognitionHandler.VoiceResult;
import org.chromium.chrome.browser.profiles.Profile;
import org.chromium.chrome.browser.tab.Tab;
import org.chromium.components.omnibox.AutocompleteMatch;
import org.chromium.components.omnibox.AutocompleteResult;
import org.chromium.components.omnibox.AutocompleteResult.VerificationPoint;
import org.chromium.content_public.browser.WebContents;
import org.chromium.url.GURL;

import java.util.HashSet;
import java.util.List;
import java.util.Set;

/**
 * Bridge to the native AutocompleteControllerAndroid.
 *
 * The bridge is created and maintained by the AutocompleteControllerAndroid native class.
 * The Native class is created on request for supplied profiles and remains available until the
 * Profile gets destroyed, making this instance follow the same life cycle.
 *
 * Instances of this class should not be acquired directly; instead, when a profile-specific
 * AutocompleteController is required, please acquire one using the AutocompleteControllerFactory.
 *
 * When User Profile gets destroyed, native class gets destroyed as well, and during the
 * destruction calls the #notifyNativeDestroyed() method, which signals the Java
 * AutocompleteController is no longer valid, and removes it from the AutocompleteControllerFactory
 * cache.
 */
public class AutocompleteController {
    // Maximum number of voice suggestions to show.
    private static final int MAX_VOICE_SUGGESTION_COUNT = 3;

    private final @NonNull Profile mProfile;
    private final @NonNull Set<OnSuggestionsReceivedListener> mListeners = new HashSet<>();
    private long mNativeController;
    private @NonNull AutocompleteResult mAutocompleteResult = AutocompleteResult.EMPTY_RESULT;

    /**
     * Listener for receiving OmniboxSuggestions.
     */
    public interface OnSuggestionsReceivedListener {
        /**
         * Receive autocomplete matches for currently executing query.
         *
         * @param autocompleteResult The current set of autocomplete matches for previously supplied
         *         query.
         * @param inlineAutocompleteText The text to offer as an inline autocompletion.
         * @param isFinal Whether this result is transitory (false) or final (true). Final result
         *         always comes in last, even if the query is canceled.
         */
        void onSuggestionsReceived(AutocompleteResult autocompleteResult,
                String inlineAutocompleteText, boolean isFinal);
    }

    @CalledByNative
    private AutocompleteController(@NonNull Profile profile, long nativeController) {
        mProfile = profile;
        mNativeController = nativeController;
    }

    /** @param listener The listener to be notified when new suggestions are available. */
    public void addOnSuggestionsReceivedListener(@NonNull OnSuggestionsReceivedListener listener) {
        mListeners.add(listener);
    }

    /** @param listener A previously registered new suggestions listener to be removed. */
    public void removeOnSuggestionsReceivedListener(
            @NonNull OnSuggestionsReceivedListener listener) {
        mListeners.remove(listener);
    }

    /**
     * Starts querying for omnibox suggestions for a given text.
     *
     * @param url The URL of the current tab, used to suggest query refinements.
     * @param pageClassification The page classification of the current tab.
     * @param text The text to query autocomplete suggestions for.
     * @param cursorPosition The position of the cursor within the text.  Set to -1 if the cursor is
     *                       not focused on the text.
     * @param preventInlineAutocomplete Whether autocomplete suggestions should be prevented.
     */
    void start(@NonNull String url, int pageClassification, @NonNull String text,
            int cursorPosition, boolean preventInlineAutocomplete) {
        if (mNativeController == 0) return;
        assert !TextUtils.isEmpty(url);
        if (TextUtils.isEmpty(url)) return;

        AutocompleteControllerJni.get().start(mNativeController, text, cursorPosition, null, url,
                pageClassification, preventInlineAutocomplete, false, false, true);
    }

    /**
     * Issue a prefetch request for zero prefix suggestions.
     *
     * Prefetch is a fire-and-forget operation that yields no results.
     */
    void startPrefetch() {
        if (mNativeController == 0) return;
        AutocompleteControllerJni.get().startPrefetch(mNativeController);
    }

    /**
     * Given some string |text| that the user wants to use for navigation, determines how it should
     * be interpreted. This is a fallback in case the user didn't select a visible suggestion (e.g.
     * the user pressed enter before omnibox suggestions had been shown).
     *
     * Note: this updates the internal state of the autocomplete controller just as start() does.
     * Future calls that reference autocomplete results by index, e.g. onSuggestionSelected(),
     * should reference the returned suggestion by index 0.
     *
     * @param text The user's input text to classify (i.e. what they typed in the omnibox)
     * @param focusedFromFakebox Whether the user entered the omnibox by tapping the fakebox on the
     *                           native NTP. This should be false on all other pages.
     * @return The AutocompleteMatch specifying where to navigate, the transition type, etc. May
     *         be null if the input is invalid.
     */
    @VisibleForTesting(otherwise = VisibleForTesting.PACKAGE_PRIVATE)
    public AutocompleteMatch classify(@NonNull String text, boolean focusedFromFakebox) {
        if (mNativeController == 0) return null;
        return AutocompleteControllerJni.get().classify(
                mNativeController, text, focusedFromFakebox);
    }

    /**
     * Starts a query for suggestions before any input is available from the user.
     *
     * @param omniboxText The text displayed in the omnibox.
     * @param url The url of the currently loaded web page.
     * @param pageClassification The page classification of the current tab.
     * @param title The title of the currently loaded web page.
     */
    public void startZeroSuggest(@NonNull String omniboxText, @NonNull String url,
            int pageClassification, @NonNull String title) {
        if (mNativeController == 0) return;
        assert !TextUtils.isEmpty(url);
        if (TextUtils.isEmpty(url)) return;

        AutocompleteControllerJni.get().onOmniboxFocused(
                mNativeController, omniboxText, url, pageClassification, title);
    }

    /**
     * Stops generating autocomplete suggestions for the currently specified text from
     * {@link #start(Profile,String, String, boolean)}.
     *
     * @param clear Whether to clear the most recent autocomplete results. When true, the
     *         {@link #onSuggestionsReceived(AutocompleteResult, String)} will be called with an
     *         empty result set.
     */
    void stop(boolean clear) {
        if (mNativeController == 0) return;
        AutocompleteControllerJni.get().stop(mNativeController, clear);
    }

    /**
     * Resets session for autocomplete controller. This happens every time we start typing
     * new input into the omnibox.
     */
    void resetSession() {
        if (mNativeController == 0) return;
        AutocompleteControllerJni.get().resetSession(mNativeController);
    }

    /**
     * Partially deletes an omnibox suggestion.
     * This call should be used by compound suggestion types (such as carousel) that host multiple
     * components inside (eg. MostVisitedTiles).
     * @param matchIndex The position at which the match is located.
     * @param elementIndex The element within the match that needs to be deleted.
     */
    @VisibleForTesting(otherwise = VisibleForTesting.PACKAGE_PRIVATE)
    public void deleteMatchElement(int matchIndex, int elementIndex) {
        if (mNativeController == 0) return;
        if (!mAutocompleteResult.verifyCoherency(matchIndex, VerificationPoint.DELETE_MATCH)) {
            return;
        }
        AutocompleteControllerJni.get().deleteMatchElement(
                mNativeController, matchIndex, elementIndex);
    }

    /**
     * Deletes an omnibox suggestion, if possible.
     * @param matchIndex The position at which the match is located.
     */
    @VisibleForTesting(otherwise = VisibleForTesting.PACKAGE_PRIVATE)
    public void deleteMatch(int matchIndex) {
        if (mNativeController == 0) return;
        if (!mAutocompleteResult.verifyCoherency(matchIndex, VerificationPoint.DELETE_MATCH)) {
            return;
        }
        AutocompleteControllerJni.get().deleteMatch(mNativeController, matchIndex);
    }

    @CalledByNative
    private void onSuggestionsReceived(@NonNull AutocompleteResult autocompleteResult,
            @NonNull String inlineAutocompleteText, boolean isFinal) {
        mAutocompleteResult = autocompleteResult;
        // Notify callbacks of suggestions.
        for (OnSuggestionsReceivedListener listener : mListeners) {
            listener.onSuggestionsReceived(autocompleteResult, inlineAutocompleteText, isFinal);
        }
    }

    @CalledByNative
    private void notifyNativeDestroyed() {
        mNativeController = 0;
    }

    /**
     * Called whenever a navigation happens from the omnibox to record metrics about the user's
     * interaction with the omnibox.
     *
     * @param matchIndex The index of the suggestion that was selected.
     * @param disposition The window open disposition.
     * @param type The type of the selected suggestion.
     * @param currentPageUrl The URL of the current page.
     * @param pageClassification The page classification of the current tab.
     * @param elapsedTimeSinceModified The number of ms that passed between the user first
     *                                 modifying text in the omnibox and selecting a suggestion.
     * @param completedLength The length of the default match's inline autocompletion if any.
     * @param webContents The web contents for the tab where the selected suggestion will be shown.
     */
    @VisibleForTesting(otherwise = VisibleForTesting.PACKAGE_PRIVATE)
    public void onSuggestionSelected(int matchIndex, int disposition, int type,
            @NonNull String currentPageUrl, int pageClassification, long elapsedTimeSinceModified,
            int completedLength, @Nullable WebContents webContents) {
        if (mNativeController == 0) return;
        if (!mAutocompleteResult.verifyCoherency(matchIndex, VerificationPoint.SELECT_MATCH)) {
            return;
        }
        AutocompleteControllerJni.get().onSuggestionSelected(mNativeController, matchIndex,
                disposition, currentPageUrl, pageClassification, elapsedTimeSinceModified,
                completedLength, webContents);
    }

    /**
     * Pass the voice provider a list representing the results of a voice recognition.
     * @param results A list containing the results of a voice recognition.
     */
    void onVoiceResults(@Nullable List<VoiceResult> results) {
        if (mNativeController == 0) return;
        if (results == null || results.size() == 0) return;
        final int count = Math.min(results.size(), MAX_VOICE_SUGGESTION_COUNT);
        String[] voiceMatches = new String[count];
        float[] confidenceScores = new float[count];
        for (int i = 0; i < count; i++) {
            voiceMatches[i] = results.get(i).getMatch();
            confidenceScores[i] = results.get(i).getConfidence();
        }
        AutocompleteControllerJni.get().setVoiceMatches(
                mNativeController, voiceMatches, confidenceScores);
    }

    /**
     * Updates aqs parameters on the selected match that we will navigate to and returns the
     * updated URL.
     *
     * @param matchIndex The index of the autocomplete entry selected.
     * @param elapsedTimeSinceInputChange The number of ms between the time the user started
     *         typing in the omnibox and the time the user has selected a suggestion.
     */
    @Nullable
    GURL updateMatchDestinationUrlWithQueryFormulationTime(
            int matchIndex, long elapsedTimeSinceInputChange) {
        return updateMatchDestinationUrlWithQueryFormulationTime(
                matchIndex, elapsedTimeSinceInputChange, null, null);
    }

    /**
     * Updates destination url on the selected match that we will navigate to and returns the
     * updated URL.
     *
     * If |newQueryText| and |newQueryParams| are not empty, they will be used to replace the
     * existing query string and query params. For example, if:
     * - |elapsedTimeSinceInputChange| > 0,
     * - |newQyeryText| is "Politics news",
     * - existing destination URL is "www.google.com/search?q=News+&aqs=chrome.0.69i...l3",
     * the returned new URL will be of the format
     *   "www.google.com/search?q=Politics+news&aqs=chrome.0.69i...l3.1409j0j9"
     * where ".1409j0j9" is the encoded elapsed time.
     *
     * @param matchIndex The index of the autocomplete entry selected.
     * @param elapsedTimeSinceInputChange The number of ms between the time the user started
     *                                    typing in the omnibox and the time the user has selected
     *                                    a suggestion.
     * @param newQueryText The new query string that will replace the existing one.
     * @param newQueryParams A list of search params to be appended to the query.
     * @return The url to navigate to for this match with aqs parameter, query string and parameters
     *         updated, if we are making a Google search query.
     */
    @Nullable
    GURL updateMatchDestinationUrlWithQueryFormulationTime(int matchIndex,
            long elapsedTimeSinceInputChange, @Nullable String newQueryText,
            @Nullable List<String> newQueryParams) {
        if (mNativeController == 0) return null;
        if (!mAutocompleteResult.verifyCoherency(matchIndex, VerificationPoint.UPDATE_MATCH)) {
            return null;
        }
        return AutocompleteControllerJni.get()
                .updateMatchDestinationURLWithAdditionalAssistedQueryStats(mNativeController,
                        matchIndex, elapsedTimeSinceInputChange, newQueryText,
                        newQueryParams == null
                                ? null
                                : newQueryParams.toArray(new String[newQueryParams.size()]));
    }

    /**
     * Retrieves matching tab for suggestion at specific index.
     * TODO(crbug.com/1266558): move this to AutocompleteMatch object when Tab is no longer part
     * of the //chrome/browser directory.
     *
     * @param matchIndex Index of the suggestion to retrieve Tab info for.
     * @return Tab that hosts matching URL.
     */
    @Nullable
    Tab getMatchingTabForSuggestion(int matchIndex) {
        if (mNativeController == 0) return null;
        return AutocompleteControllerJni.get().getMatchingTabForSuggestion(
                mNativeController, matchIndex);
    }

    /**
     * Acquire an instance of AutocompleteController associated with the supplied Profile.
     *
     * @param profile The profile to get the AutocompleteController for.
     * @return An existing (if one is available) or new (otherwise) instance of the
     *         AutocompleteController associated with the supplied profile.
     */
    public static AutocompleteController getForProfile(Profile profile) {
        assert profile != null : "AutocompleteController cannot be created for null profile";
        if (profile == null) return null;
        return AutocompleteControllerJni.get().getForProfile(profile);
    }

    @VisibleForTesting(otherwise = VisibleForTesting.PACKAGE_PRIVATE)
    @NativeMethods
    public interface Natives {
        void start(long nativeAutocompleteControllerAndroid, String text, int cursorPosition,
                String desiredTld, String currentUrl, int pageClassification,
                boolean preventInlineAutocomplete, boolean preferKeyword,
                boolean allowExactKeywordMatch, boolean wantAsynchronousMatches);
        AutocompleteMatch classify(
                long nativeAutocompleteControllerAndroid, String text, boolean focusedFromFakebox);
        void stop(long nativeAutocompleteControllerAndroid, boolean clearResults);
        void resetSession(long nativeAutocompleteControllerAndroid);
        void onSuggestionSelected(long nativeAutocompleteControllerAndroid, int matchIndex,
                int disposition, String currentPageUrl, int pageClassification,
                long elapsedTimeSinceModified, int completedLength, WebContents webContents);
        void onOmniboxFocused(long nativeAutocompleteControllerAndroid, String omniboxText,
                String currentUrl, int pageClassification, String currentTitle);
        void deleteMatchElement(
                long nativeAutocompleteControllerAndroid, int matchIndex, int elementIndex);
        void deleteMatch(long nativeAutocompleteControllerAndroid, int matchIndex);
        GURL updateMatchDestinationURLWithAdditionalAssistedQueryStats(
                long nativeAutocompleteControllerAndroid, int matchIndex,
                long elapsedTimeSinceInputChange, String newQueryText, String[] newQueryParams);
        Tab getMatchingTabForSuggestion(long nativeAutocompleteControllerAndroid, int matchIndex);
        void setVoiceMatches(long nativeAutocompleteControllerAndroid, String[] matches,
                float[] confidenceScores);

        /**
         * Sends a zero suggest request to the server in order to pre-populate the result cache.
         */
        void startPrefetch(long nativeAutocompleteControllerAndroid);

        /**
         * Acquire an instance of AutocompleteController associated with the supplied profile.
         */
        AutocompleteController getForProfile(Profile profile);
    }
}
