// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.omnibox;

import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Context;
import android.graphics.Typeface;
import android.text.Spanned;
import android.text.TextUtils;

import androidx.annotation.ColorInt;
import androidx.annotation.NonNull;
import androidx.annotation.VisibleForTesting;

import org.chromium.base.Callback;
import org.chromium.base.ContextUtils;
import org.chromium.chrome.browser.omnibox.UrlBar.ScrollType;
import org.chromium.chrome.browser.omnibox.UrlBar.UrlTextChangeListener;
import org.chromium.chrome.browser.omnibox.UrlBarCoordinator.SelectionState;
import org.chromium.chrome.browser.omnibox.UrlBarProperties.AutocompleteText;
import org.chromium.chrome.browser.omnibox.UrlBarProperties.UrlBarTextState;
import org.chromium.chrome.browser.omnibox.styles.OmniboxResourceProvider;
import org.chromium.chrome.browser.ui.theme.BrandedColorScheme;
import org.chromium.components.browser_ui.styles.SemanticColorUtils;
import org.chromium.components.omnibox.OmniboxUrlEmphasizer.UrlEmphasisSpan;
import org.chromium.ui.modelutil.PropertyModel;

import java.util.ArrayList;
import java.util.List;

/** Handles collecting and pushing state information to the UrlBar model. */
class UrlBarMediator implements UrlBar.UrlBarTextContextMenuDelegate, UrlBar.UrlTextChangeListener {
    private final Context mContext;
    private final PropertyModel mModel;

    private Callback<Boolean> mOnFocusChangeCallback;
    private boolean mHasFocus;

    private UrlBarData mUrlBarData;
    private @ScrollType int mScrollType = UrlBar.ScrollType.NO_SCROLL;
    private @SelectionState int mSelectionState = UrlBarCoordinator.SelectionState.SELECT_ALL;

    private final List<UrlTextChangeListener> mUrlTextChangeListeners = new ArrayList<>();
    private int mPreviousBrandedColorScheme;
    // For both Start Surface and NTP, when the surface polish flag is enabled, the search text hint
    // color is fixed for the real search box and we couldn't change it by the branded color scheme.
    private boolean mIsHintTextFixedForStartOrNtp;

    /**
     * Creates a URLBarMediator.
     *
     * @param context The current Android's context.
     * @param model MVC property model to write changes to.
     * @param focusChangeCallback The callback that will be notified when focus changes on the
     *     UrlBar.
     */
    public UrlBarMediator(
            Context context,
            @NonNull PropertyModel model,
            @NonNull Callback<Boolean> focusChangeCallback) {
        mContext = context;
        mModel = model;
        mOnFocusChangeCallback = focusChangeCallback;

        mModel.set(UrlBarProperties.FOCUS_CHANGE_CALLBACK, this::onUrlFocusChange);
        mModel.set(UrlBarProperties.SHOW_CURSOR, false);
        mModel.set(UrlBarProperties.TEXT_CONTEXT_MENU_DELEGATE, this);
        mModel.set(UrlBarProperties.URL_TEXT_CHANGE_LISTENER, this);
        mModel.set(UrlBarProperties.HAS_URL_SUGGESTIONS, false);
        setBrandedColorScheme(BrandedColorScheme.APP_DEFAULT);
    }

    public void destroy() {
        mUrlTextChangeListeners.clear();
        mOnFocusChangeCallback = (unused) -> {};
    }

    /** Adds a listener for url text changes. */
    public void addUrlTextChangeListener(UrlTextChangeListener listener) {
        mUrlTextChangeListeners.add(listener);
    }

    /**
     * Updates the text content of the UrlBar.
     *
     * @param data The new data to be displayed.
     * @param scrollType The scroll type that should be applied to the data.
     * @param selectionState Specifies how the text should be selected when focused.
     * @return Whether this data differs from the previously passed in values.
     */
    public boolean setUrlBarData(
            UrlBarData data, @ScrollType int scrollType, @SelectionState int selectionState) {
        if (data.originEndIndex == data.originStartIndex) {
            scrollType = UrlBar.ScrollType.SCROLL_TO_BEGINNING;
        }

        // Do not scroll to the end of the host for URLs such as data:, javascript:, etc...
        if (data.url != null
                && data.displayText != null
                && data.originEndIndex == data.displayText.length()) {
            String scheme = data.url.getScheme();
            if (!TextUtils.isEmpty(scheme) && !UrlBarData.SCHEMES_TO_SPLIT.contains(scheme)) {
                scrollType = UrlBar.ScrollType.SCROLL_TO_BEGINNING;
            }
        }

        if (!mHasFocus
                && isNewTextEquivalentToExistingText(mUrlBarData, data)
                && mScrollType == scrollType) {
            return false;
        }
        mUrlBarData = data;
        mScrollType = scrollType;
        mSelectionState = selectionState;

        pushTextToModel();
        return true;
    }

    UrlBarData getUrlBarData() {
        return mUrlBarData;
    }

    private void pushTextToModel() {
        CharSequence text =
                !mHasFocus ? mUrlBarData.displayText : mUrlBarData.getEditingOrDisplayText();
        CharSequence textForAutofillServices = text;

        if (!(mHasFocus || TextUtils.isEmpty(text) || mUrlBarData.url == null)) {
            textForAutofillServices = mUrlBarData.url.getSpec();
        }

        @ScrollType int scrollType = mHasFocus ? UrlBar.ScrollType.NO_SCROLL : mScrollType;
        if (text == null) text = "";

        UrlBarTextState state =
                new UrlBarTextState(
                        text,
                        textForAutofillServices,
                        scrollType,
                        mUrlBarData.originEndIndex,
                        mSelectionState);
        mModel.set(UrlBarProperties.TEXT_STATE, state);
    }

    @VisibleForTesting
    protected static boolean isNewTextEquivalentToExistingText(
            UrlBarData existingUrlData, UrlBarData newUrlData) {
        if (existingUrlData == null) return newUrlData == null;
        if (newUrlData == null) return false;

        if (!TextUtils.equals(existingUrlData.editingText, newUrlData.editingText)) return false;

        CharSequence existingCharSequence = existingUrlData.displayText;
        CharSequence newCharSequence = newUrlData.displayText;
        if (existingCharSequence == null) return newCharSequence == null;

        // Regardless of focus state, ensure the text content is the same.
        if (!TextUtils.equals(existingCharSequence, newCharSequence)) return false;

        // If both existing and new text is empty, then treat them equal regardless of their
        // spanned state.
        if (TextUtils.isEmpty(newCharSequence)) return true;

        // When not focused, compare the emphasis spans applied to the text to determine
        // equality.  Internally, TextView applies many additional spans that need to be
        // ignored for this comparison to be useful, so this is scoped to only the span types
        // applied by our UI.
        if (!(newCharSequence instanceof Spanned) || !(existingCharSequence instanceof Spanned)) {
            return false;
        }

        Spanned currentText = (Spanned) existingCharSequence;
        Spanned newText = (Spanned) newCharSequence;
        UrlEmphasisSpan[] currentSpans =
                currentText.getSpans(0, currentText.length(), UrlEmphasisSpan.class);
        UrlEmphasisSpan[] newSpans = newText.getSpans(0, newText.length(), UrlEmphasisSpan.class);
        if (currentSpans.length != newSpans.length) return false;
        for (int i = 0; i < currentSpans.length; i++) {
            UrlEmphasisSpan currentSpan = currentSpans[i];
            UrlEmphasisSpan newSpan = newSpans[i];
            if (!currentSpan.equals(newSpan)
                    || currentText.getSpanStart(currentSpan) != newText.getSpanStart(newSpan)
                    || currentText.getSpanEnd(currentSpan) != newText.getSpanEnd(newSpan)
                    || currentText.getSpanFlags(currentSpan) != newText.getSpanFlags(newSpan)) {
                return false;
            }
        }
        return true;
    }

    /**
     * Sets the autocomplete text to be shown.
     *
     * @param userText The existing user text.
     * @param autocompleteText The text to be appended to the user text.
     */
    public void setAutocompleteText(String userText, String autocompleteText) {
        if (!mHasFocus) {
            assert false : "Should not update autocomplete text when not focused";
            return;
        }
        mModel.set(
                UrlBarProperties.AUTOCOMPLETE_TEXT,
                new AutocompleteText(userText, autocompleteText));
    }

    private void onUrlFocusChange(boolean focus) {
        mHasFocus = focus;

        if (mModel.get(UrlBarProperties.ALLOW_FOCUS)) {
            mModel.set(UrlBarProperties.SHOW_CURSOR, mHasFocus);
        }

        UrlBarTextState preCallbackState = mModel.get(UrlBarProperties.TEXT_STATE);
        mOnFocusChangeCallback.onResult(focus);
        boolean textChangedInFocusCallback =
                mModel.get(UrlBarProperties.TEXT_STATE) != preCallbackState;
        if (mUrlBarData != null && !textChangedInFocusCallback) {
            pushTextToModel();
        }
    }

    /**
     * Sets the color scheme.
     *
     * @param brandedColorScheme The {@link @BrandedColorScheme}.
     * @return Whether this resulted in a change from the previous value.
     */
    public boolean setBrandedColorScheme(@BrandedColorScheme int brandedColorScheme) {
        // TODO(bauerb): Make clients observe the property instead of checking the return value.
        final @ColorInt int textColor =
                OmniboxResourceProvider.getUrlBarPrimaryTextColor(mContext, brandedColorScheme);
        final @ColorInt int hintTextColor =
                OmniboxResourceProvider.getUrlBarHintTextColor(mContext, brandedColorScheme);

        mModel.set(UrlBarProperties.TEXT_COLOR, textColor);
        if (!mIsHintTextFixedForStartOrNtp) {
            mModel.set(UrlBarProperties.HINT_TEXT_COLOR, hintTextColor);
        }

        boolean isBrandedColorSchemeChanged = mPreviousBrandedColorScheme != brandedColorScheme;
        mPreviousBrandedColorScheme = brandedColorScheme;
        return isBrandedColorSchemeChanged;
    }

    /**
     * Sets whether to use incognito colors.
     *
     * @param incognitoColorsEnabled Whether to use incognito colors.
     */
    public void setIncognitoColorsEnabled(boolean incognitoColorsEnabled) {
        mModel.set(UrlBarProperties.INCOGNITO_COLORS_ENABLED, incognitoColorsEnabled);
    }

    /** Sets whether the view allows user focus. */
    public void setAllowFocus(boolean allowFocus) {
        mModel.set(UrlBarProperties.ALLOW_FOCUS, allowFocus);
        if (allowFocus) {
            mModel.set(UrlBarProperties.SHOW_CURSOR, mHasFocus);
        }
    }

    /** Set the listener to be notified for URL direction changes. */
    public void setUrlDirectionListener(Callback<Integer> listener) {
        mModel.set(UrlBarProperties.URL_DIRECTION_LISTENER, listener);
    }

    @Override
    public String getReplacementCutCopyText(
            String currentText, int selectionStart, int selectionEnd) {
        if (mUrlBarData == null || mUrlBarData.url == null) return null;

        // Replace the cut/copy text only applies if the user selected from the beginning of the
        // display text.
        if (selectionStart != 0) return null;

        // Trim to just the currently selected text as that is the only text we are replacing.
        currentText = currentText.substring(selectionStart, selectionEnd);

        String formattedUrlLocation;
        String originalUrlLocation;

        formattedUrlLocation =
                getUrlContentsPrePath(
                        mUrlBarData.getEditingOrDisplayText().toString(),
                        mUrlBarData.url.getHost());
        originalUrlLocation =
                getUrlContentsPrePath(mUrlBarData.url.getSpec(), mUrlBarData.url.getHost());

        // If we are copying/cutting the full previously formatted URL, reset the URL
        // text before initiating the TextViews handling of the context menu.
        //
        // Example:
        //    Original display text: www.example.com
        //    Original URL:          http://www.example.com
        //
        // Editing State:
        //    www.example.com/blah/foo
        //    |<--- Selection --->|
        //
        // Resulting clipboard text should be:
        //    http://www.example.com/blah/
        //
        // As long as the full original text was selected, it will replace that with the original
        // URL and keep any further modifications by the user.
        if (!currentText.startsWith(formattedUrlLocation)
                || selectionEnd < formattedUrlLocation.length()) {
            return null;
        }

        return originalUrlLocation + currentText.substring(formattedUrlLocation.length());
    }

    @Override
    public String getTextToPaste() {
        Context context = ContextUtils.getApplicationContext();

        ClipboardManager clipboard =
                (ClipboardManager) context.getSystemService(Context.CLIPBOARD_SERVICE);
        ClipData clipData = clipboard.getPrimaryClip();
        if (clipData == null) return null;

        StringBuilder builder = new StringBuilder();
        for (int i = 0; i < clipData.getItemCount(); i++) {
            builder.append(clipData.getItemAt(i).coerceToText(context));
        }

        String stringToPaste = sanitizeTextForPaste(builder.toString());
        return stringToPaste;
    }

    /**
     * @param hasSuggestions Whether suggestions are showing in the URL bar.
     */
    public void onUrlBarSuggestionsChanged(boolean hasSuggestions) {
        mModel.set(UrlBarProperties.HAS_URL_SUGGESTIONS, hasSuggestions);
    }

    @VisibleForTesting
    protected String sanitizeTextForPaste(String text) {
        return OmniboxViewUtil.sanitizeTextForPaste(text);
    }

    /**
     * Returns the portion of the URL that precedes the path/query section of the URL.
     *
     * @param url The url to be used to find the preceding portion.
     * @param host The host to be located in the URL to determine the location of the path.
     * @return The URL contents that precede the path (or the passed in URL if the host is not
     *     found).
     */
    private static String getUrlContentsPrePath(String url, String host) {
        int hostIndex = url.indexOf(host);
        if (hostIndex == -1) return url;

        int pathIndex = url.indexOf('/', hostIndex);
        if (pathIndex <= 0) return url;

        return url.substring(0, pathIndex);
    }

    /**
     * @see UrlTextChangeListener
     */
    @Override
    public void onTextChanged(String textWithoutAutocomplete) {
        for (int i = 0; i < mUrlTextChangeListeners.size(); i++) {
            mUrlTextChangeListeners.get(i).onTextChanged(textWithoutAutocomplete);
        }
    }

    /**
     * Sets search box hint text color to brandedColorScheme.
     *
     * @param brandedColorScheme The {@link @BrandedColorScheme}.
     */
    void setUrlBarHintTextColorForDefault(@BrandedColorScheme int brandedColorScheme) {
        mIsHintTextFixedForStartOrNtp = false;
        setBrandedColorScheme(brandedColorScheme);
    }

    /**
     * Sets search box hint text color for Surface Polish. The color may be colorOnSurface or
     * colorOnPrimaryContainer, depending on useColorfulOmniboxType.
     *
     * @param useColorfulOmniboxType True if the surface polish flag and omnibox color variant are
     *     both enabled and we need to use the colorful type for the url bar hint color.
     */
    void setUrlBarHintTextColorForSurfacePolish(boolean useColorfulOmniboxType) {
        mIsHintTextFixedForStartOrNtp = true;
        final @ColorInt int hintTextColor =
                useColorfulOmniboxType
                        ? SemanticColorUtils.getDefaultTextColorOnAccent1Container(mContext)
                        : SemanticColorUtils.getDefaultTextColor(mContext);
        mModel.set(UrlBarProperties.HINT_TEXT_COLOR, hintTextColor);
    }

    /**
     * Updates the typeface and style of the search text in the search box.
     *
     * @param useDefaultUrlBarTypeface Whether to use the default typeface for the search text in
     *     the search box. If not we will use medium Google sans typeface for surface polish.
     */
    void updateUrlBarTypeface(boolean useDefaultUrlBarTypeface) {
        // TODO(crbug.com/1487760): Use TextAppearance style instead.
        Typeface typeface =
                useDefaultUrlBarTypeface
                        ? Typeface.defaultFromStyle(Typeface.NORMAL)
                        : Typeface.create("google-sans-medium", Typeface.NORMAL);
        mModel.set(UrlBarProperties.TYPEFACE, typeface);
    }
}
