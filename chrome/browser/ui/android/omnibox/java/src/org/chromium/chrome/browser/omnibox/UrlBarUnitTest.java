// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.omnibox;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;
import static org.mockito.AdditionalMatchers.not;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyBoolean;
import static org.mockito.ArgumentMatchers.anyFloat;
import static org.mockito.ArgumentMatchers.anyInt;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.anyInt;
import static org.mockito.Mockito.clearInvocations;
import static org.mockito.Mockito.doReturn;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.spy;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import android.text.InputType;
import android.text.Layout;
import android.text.SpannableStringBuilder;
import android.text.TextPaint;
import android.text.TextUtils;
import android.view.ContextThemeWrapper;
import android.view.MotionEvent;
import android.view.View;
import android.view.View.MeasureSpec;
import android.view.ViewGroup.LayoutParams;
import android.view.ViewStructure;
import android.view.inputmethod.EditorInfo;

import androidx.core.view.inputmethod.EditorInfoCompat;

import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TestRule;
import org.junit.runner.RunWith;
import org.mockito.ArgumentCaptor;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;
import org.robolectric.annotation.Config;

import org.chromium.base.ContextUtils;
import org.chromium.base.test.BaseRobolectricTestRunner;
import org.chromium.base.test.util.Features.DisableFeatures;
import org.chromium.base.test.util.Features.EnableFeatures;
import org.chromium.base.test.util.Features.JUnitProcessor;
import org.chromium.base.test.util.HistogramWatcher;
import org.chromium.chrome.browser.flags.ChromeFeatureList;
import org.chromium.chrome.browser.omnibox.UrlBar.UrlBarDelegate;
import org.chromium.chrome.browser.omnibox.test.R;

import java.util.Collections;

/** Unit tests for the URL bar UI component. */
@RunWith(BaseRobolectricTestRunner.class)
@Config(qualifiers = "w100dp-h50dp")
public class UrlBarUnitTest {
    // UrlBar has 4 px of padding on the left and right. Set this to urlbar width + padding so
    // getVisibleMeasuredViewportWidth() returns 100. This ensures NUMBER_OF_VISIBLE_CHARACTERS
    // is accurate.
    private static final int URL_BAR_WIDTH = 100 + 8;
    private static final int URL_BAR_HEIGHT = 10;

    // Screen width is set to 100px, with a default density of 1px per dp, and we estimate 5dp per
    // char, so there will be 20 visible characters.
    private static final int NUMBER_OF_VISIBLE_CHARACTERS = 20;

    private UrlBar mUrlBar;
    public @Rule TestRule mFeaturesProcessorRule = new JUnitProcessor();
    public @Rule MockitoRule mockitoRule = MockitoJUnit.rule();
    private @Mock UrlBarDelegate mUrlBarDelegate;
    private @Mock ViewStructure mViewStructure;
    private @Mock Layout mLayout;
    private @Mock TextPaint mPaint;

    private final String mShortPath = "/aaaa";
    private final String mLongPath =
            "/" + TextUtils.join("", Collections.nCopies(UrlBar.MIN_LENGTH_FOR_TRUNCATION_V2, "a"));
    private final String mShortDomain = "www.a.com";
    private final String mLongDomain =
            "www."
                    + TextUtils.join(
                            "", Collections.nCopies(UrlBar.MIN_LENGTH_FOR_TRUNCATION_V2, "a"))
                    + ".com";

    @Before
    public void setUp() {
        var ctx =
                new ContextThemeWrapper(
                        ContextUtils.getApplicationContext(), R.style.Theme_AppCompat);
        mUrlBar = spy(new UrlBarApi26(ctx, null));
        mUrlBar.setDelegate(mUrlBarDelegate);

        doReturn(1).when(mLayout).getLineCount();
        when(mLayout.getPrimaryHorizontal(anyInt()))
                .thenAnswer(
                        invocation -> {
                            return (float) ((int) invocation.getArguments()[0]) * 5;
                        });
        when(mPaint.getOffsetForAdvance(
                        any(CharSequence.class),
                        anyInt(),
                        anyInt(),
                        anyInt(),
                        anyInt(),
                        anyBoolean(),
                        anyFloat()))
                .thenAnswer(
                        invocation -> {
                            return (int) ((float) invocation.getArguments()[6] / 5);
                        });
    }

    /** Force reset text layout. */
    private void resetTextLayout() {
        mUrlBar.nullLayouts();
        assertNull(mUrlBar.getLayout());
    }

    /**
     * Simulate measure() and layout() pass on the view. Ensures view size and text layout are
     * resolved.
     */
    private void measureAndLayoutUrlBarForSize(int width, int height) {
        // Measure and layout the Url bar.
        mUrlBar.setLayoutParams(new LayoutParams(width, height));
        mUrlBar.measure(
                MeasureSpec.makeMeasureSpec(width, MeasureSpec.EXACTLY),
                MeasureSpec.makeMeasureSpec(height, MeasureSpec.EXACTLY));
        mUrlBar.layout(0, 0, width, height);

        // Sanity check: new layout should be available.
        assertNotNull(mUrlBar.getLayout());
        assertFalse(mUrlBar.isLayoutRequested());
    }

    /** Resize the UrlBar to its default size for testing. */
    private void measureAndLayoutUrlBar() {
        measureAndLayoutUrlBarForSize(URL_BAR_WIDTH, URL_BAR_HEIGHT);
    }

    @Test
    public void testAutofillStructureReceivesFullURL() {
        mUrlBar.setTextForAutofillServices("https://www.google.com");
        mUrlBar.setText("www.google.com");
        mUrlBar.onProvideAutofillStructure(mViewStructure, 0);

        ArgumentCaptor<SpannableStringBuilder> haveUrl =
                ArgumentCaptor.forClass(SpannableStringBuilder.class);
        verify(mViewStructure).setText(haveUrl.capture());
        assertEquals("https://www.google.com", haveUrl.getValue().toString());
    }

    @Test
    public void onCreateInputConnection_ensureNoAutocorrect() {
        var info = new EditorInfo();
        mUrlBar.onCreateInputConnection(info);
        assertEquals(
                EditorInfo.TYPE_TEXT_VARIATION_URI,
                info.inputType & EditorInfo.TYPE_TEXT_VARIATION_URI);
        assertEquals(0, info.inputType & EditorInfo.TYPE_TEXT_FLAG_AUTO_CORRECT);
    }

    @Test
    public void onCreateInputConnection_disallowKeyboardLearningPassedToIme() {
        doReturn(true).when(mUrlBarDelegate).allowKeyboardLearning();

        var info = new EditorInfo();
        mUrlBar.onCreateInputConnection(info);
        assertEquals(0, info.imeOptions & EditorInfoCompat.IME_FLAG_NO_PERSONALIZED_LEARNING);
    }

    @Test
    public void onCreateInputConnection_allowKeyboardLearningPassedToIme() {
        doReturn(false).when(mUrlBarDelegate).allowKeyboardLearning();

        var info = new EditorInfo();
        mUrlBar.onCreateInputConnection(info);
        assertEquals(
                EditorInfoCompat.IME_FLAG_NO_PERSONALIZED_LEARNING,
                info.imeOptions & EditorInfoCompat.IME_FLAG_NO_PERSONALIZED_LEARNING);
    }

    @Test
    public void onCreateInputConnection_setDefaultsWhenDelegateNotPresent() {
        mUrlBar.setDelegate(null);

        var info = new EditorInfo();
        mUrlBar.onCreateInputConnection(info);

        assertEquals(
                EditorInfoCompat.IME_FLAG_NO_PERSONALIZED_LEARNING,
                info.imeOptions & EditorInfoCompat.IME_FLAG_NO_PERSONALIZED_LEARNING);
        assertEquals(
                EditorInfo.TYPE_TEXT_VARIATION_URI,
                info.inputType & EditorInfo.TYPE_TEXT_VARIATION_URI);
        assertEquals(0, info.inputType & EditorInfo.TYPE_TEXT_FLAG_AUTO_CORRECT);
    }

    @Test
    public void urlBar_editorSetupPermitsWordSelection() {
        // This test verifies whether the Omnibox is set up so that it permits word selection. See:
        // https://cs.android.com/search?q=function:Editor.needsToSelectAllToSelectWordOrParagraph

        int klass = mUrlBar.getInputType() & InputType.TYPE_MASK_CLASS;
        int variation = mUrlBar.getInputType() & InputType.TYPE_MASK_VARIATION;
        int flags = mUrlBar.getInputType() & InputType.TYPE_MASK_FLAGS;
        assertEquals(InputType.TYPE_CLASS_TEXT, klass);
        assertEquals(InputType.TYPE_TEXT_VARIATION_NORMAL, variation);
        assertEquals(InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS, flags);
    }

    @Test
    @EnableFeatures(ChromeFeatureList.ANDROID_VISIBLE_URL_TRUNCATION_V2)
    public void testTruncation_LongUrl() {
        doReturn(mLayout).when(mUrlBar).getLayout();
        measureAndLayoutUrlBar();
        String url = mShortDomain + mLongPath;
        mUrlBar.setTextWithTruncation(url, UrlBar.ScrollType.SCROLL_TO_TLD, mShortDomain.length());
        String text = mUrlBar.getText().toString();
        assertEquals(url.substring(0, NUMBER_OF_VISIBLE_CHARACTERS), text);
    }

    @Test
    @EnableFeatures(ChromeFeatureList.ANDROID_VISIBLE_URL_TRUNCATION_V2)
    public void testTruncation_ShortUrl() {
        String url = mShortDomain + mShortPath;
        mUrlBar.setTextWithTruncation(url, UrlBar.ScrollType.SCROLL_TO_TLD, mShortDomain.length());
        String text = mUrlBar.getText().toString();
        assertEquals(url, text);
    }

    @Test
    @EnableFeatures(ChromeFeatureList.ANDROID_VISIBLE_URL_TRUNCATION_V2)
    public void testTruncation_LongTld_ScrollToTld() {
        doReturn(mLayout).when(mUrlBar).getLayout();
        measureAndLayoutUrlBar();
        String url = mLongDomain + mShortPath;
        mUrlBar.setTextWithTruncation(url, UrlBar.ScrollType.SCROLL_TO_TLD, mLongDomain.length());
        String text = mUrlBar.getText().toString();
        assertEquals(mLongDomain, text);
    }

    @Test
    @EnableFeatures(ChromeFeatureList.ANDROID_VISIBLE_URL_TRUNCATION_V2)
    public void testTruncation_LongTld_ScrollToBeginning() {
        doReturn(mLayout).when(mUrlBar).getLayout();
        measureAndLayoutUrlBar();
        String url = mShortDomain + mLongPath;
        mUrlBar.setTextWithTruncation(url, UrlBar.ScrollType.SCROLL_TO_BEGINNING, 0);
        String text = mUrlBar.getText().toString();
        assertEquals(url.substring(0, NUMBER_OF_VISIBLE_CHARACTERS), text);
    }

    @Test
    @EnableFeatures(ChromeFeatureList.ANDROID_VISIBLE_URL_TRUNCATION_V2)
    public void testTruncation_NoTruncationForWrapContent() {
        measureAndLayoutUrlBar();
        LayoutParams previousLayoutParams = mUrlBar.getLayoutParams();
        LayoutParams params =
                new LayoutParams(LayoutParams.WRAP_CONTENT, LayoutParams.MATCH_PARENT);
        mUrlBar.setLayoutParams(params);

        mUrlBar.setTextWithTruncation(mLongDomain, UrlBar.ScrollType.SCROLL_TO_BEGINNING, 0);
        String text = mUrlBar.getText().toString();
        assertEquals(mLongDomain, text);

        mUrlBar.setLayoutParams(previousLayoutParams);
    }

    @Test
    @EnableFeatures(ChromeFeatureList.ANDROID_VISIBLE_URL_TRUNCATION_V2)
    public void testTruncation_NoTruncationWhileFocused() {
        mUrlBar.onFocusChanged(true, 0, null);

        mUrlBar.setTextWithTruncation(mLongDomain, UrlBar.ScrollType.SCROLL_TO_BEGINNING, 0);
        String text = mUrlBar.getText().toString();
        assertEquals(mLongDomain, text);

        mUrlBar.onFocusChanged(false, 0, null);
    }

    @Test
    public void testOnTouchEvent_handleTouchAfterFocus() {
        mUrlBar.onFocusChanged(true, View.FOCUS_DOWN, null);
        mUrlBar.onTouchEvent(MotionEvent.obtain(0, 0, MotionEvent.ACTION_UP, 0, 0, 0));
        verify(mUrlBarDelegate).onTouchAfterFocus();
    }

    @Test
    @EnableFeatures(ChromeFeatureList.ANDROID_NO_VISIBLE_HINT_FOR_TABLETS)
    @Config(qualifiers = "sw600dp")
    public void testNoVisibleHintCalculationForTablets_noHistogramRecords() {
        measureAndLayoutUrlBar();
        mUrlBar.setText(mShortDomain + mLongPath);

        HistogramWatcher histogramWatcher =
                HistogramWatcher.newBuilder()
                        .expectNoRecords("Omnibox.CalculateVisibleHint.Duration")
                        .expectNoRecords("Omnibox.NumberOfVisibleCharacters")
                        .build();
        mUrlBar.setScrollState(UrlBar.ScrollType.SCROLL_TO_TLD, mShortDomain.length());
        histogramWatcher.assertExpected();
    }

    @Test
    @DisableFeatures(ChromeFeatureList.ANDROID_VISIBLE_URL_TRUNCATION_V2)
    public void testSetLengthHistogram_noTruncation() {
        measureAndLayoutUrlBar();
        String url = mShortDomain + mLongPath;

        HistogramWatcher histogramWatcher =
                HistogramWatcher.newSingleRecordWatcher("Omnibox.SetText.TextLength", url.length());
        mUrlBar.setText(mShortDomain + mLongPath);
        histogramWatcher.assertExpected();
    }

    @Test
    @EnableFeatures(ChromeFeatureList.ANDROID_VISIBLE_URL_TRUNCATION_V2)
    public void testSetLengtHistogram_withTruncation() {
        doReturn(mLayout).when(mUrlBar).getLayout();

        measureAndLayoutUrlBar();
        String url = mShortDomain + mLongPath;

        HistogramWatcher histogramWatcher =
                HistogramWatcher.newSingleRecordWatcher(
                        "Omnibox.SetText.TextLength", NUMBER_OF_VISIBLE_CHARACTERS);
        mUrlBar.setTextWithTruncation(url, UrlBar.ScrollType.SCROLL_TO_TLD, mShortDomain.length());
        histogramWatcher.assertExpected();
    }

    @Test
    public void
            scrollToBeginning_fallBackToDefaultWhenLayoutUnavailable_ltrLayout_noText_ltrHint() {
        // Explicitly invalidate text layouts. This could happen for a number of reasons.
        // This is also the implicit default value until text is measured, but don't rely on this.
        doReturn(View.LAYOUT_DIRECTION_LTR).when(mUrlBar).getLayoutDirection();
        mUrlBar.setHint("hint text");
        resetTextLayout();

        // As long as layouts are not available, no action should be taken.
        // This is typically the case when the text view or content is manipulated in some way and
        // has not yet completed the full measure/layout cycle.
        mUrlBar.scrollDisplayText(UrlBar.ScrollType.SCROLL_TO_BEGINNING);
        verify(mUrlBar, never()).scrollTo(anyInt(), anyInt());
        assertTrue(mUrlBar.hasPendingDisplayTextScrollForTesting());
        clearInvocations(mUrlBar);

        // LTR layout always scrolls to 0, because that's the natural origin of LTR text.
        measureAndLayoutUrlBar();
        verify(mUrlBar).scrollTo(0, 0);
        assertFalse(mUrlBar.hasPendingDisplayTextScrollForTesting());
        clearInvocations(mUrlBar);

        // Simulate request to update scroll type with no changes of scroll type, text, or view
        // size. This should avoid recalculations and simply re-set the scroll position.
        mUrlBar.scrollDisplayText(UrlBar.ScrollType.SCROLL_TO_BEGINNING);
        verify(mUrlBar, never()).scrollToTLD();
        verify(mUrlBar, never()).scrollToBeginning();
        verify(mUrlBar).scrollTo(0, 0);
    }

    @Test
    public void
            scrollToBeginning_fallBackToDefaultWhenLayoutUnavailable_rtlLayout_noText_ltrHint() {
        // Explicitly invalidate text layouts. This could happen for a number of reasons.
        // This is also the implicit default value until text is measured, but don't rely on this.
        doReturn(View.LAYOUT_DIRECTION_RTL).when(mUrlBar).getLayoutDirection();
        mUrlBar.setHint("hint text");
        resetTextLayout();

        // As long as layouts are not available, no action should be taken.
        // This is typically the case when the text view or content is manipulated in some way and
        // has not yet completed the full measure/layout cycle.
        mUrlBar.scrollDisplayText(UrlBar.ScrollType.SCROLL_TO_BEGINNING);
        verify(mUrlBar, never()).scrollTo(anyInt(), anyInt());
        assertTrue(mUrlBar.hasPendingDisplayTextScrollForTesting());
        clearInvocations(mUrlBar);

        // RTL layouts should scroll to 0 too, because that's the natural origin of LTR text.
        measureAndLayoutUrlBar();
        verify(mUrlBar).scrollTo(0, 0);
        assertFalse(mUrlBar.hasPendingDisplayTextScrollForTesting());
        clearInvocations(mUrlBar);

        // Simulate request to update scroll type with no changes of scroll type, text, or view
        // size. This should avoid recalculations and simply re-set the scroll position.
        mUrlBar.scrollDisplayText(UrlBar.ScrollType.SCROLL_TO_BEGINNING);
        verify(mUrlBar, never()).scrollToTLD();
        verify(mUrlBar, never()).scrollToBeginning();
        verify(mUrlBar).scrollTo(0, 0);
    }

    @Test
    public void
            scrollToBeginning_fallBackToDefaultWhenLayoutUnavailable_ltrLayout_noText_rtlHint() {
        // Explicitly invalidate text layouts. This could happen for a number of reasons.
        // This is also the implicit default value until text is measured, but don't rely on this.
        doReturn(View.LAYOUT_DIRECTION_LTR).when(mUrlBar).getLayoutDirection();
        mUrlBar.setHint("טקסט רמז");
        resetTextLayout();

        // As long as layouts are not available, no action should be taken.
        // This is typically the case when the text view or content is manipulated in some way and
        // has not yet completed the full measure/layout cycle.
        mUrlBar.scrollDisplayText(UrlBar.ScrollType.SCROLL_TO_BEGINNING);
        verify(mUrlBar, never()).scrollTo(anyInt(), anyInt());
        assertTrue(mUrlBar.hasPendingDisplayTextScrollForTesting());
        clearInvocations(mUrlBar);

        // LTR layout always scrolls to 0, even if the hint text is RTL. View hierarchy dictates the
        // layout direction.
        measureAndLayoutUrlBar();
        verify(mUrlBar).scrollTo(0, 0);
        assertFalse(mUrlBar.hasPendingDisplayTextScrollForTesting());
        clearInvocations(mUrlBar);

        // Simulate request to update scroll type with no changes of scroll type, text, or view
        // size. This should avoid recalculations and simply re-set the scroll position.
        mUrlBar.scrollDisplayText(UrlBar.ScrollType.SCROLL_TO_BEGINNING);
        verify(mUrlBar, never()).scrollToTLD();
        verify(mUrlBar, never()).scrollToBeginning();
        verify(mUrlBar).scrollTo(0, 0);
    }

    @Test
    public void
            scrollToBeginning_fallBackToDefaultWhenLayoutUnavailable_rtlLayout_noText_rtlHint() {
        // Explicitly invalidate text layouts. This could happen for a number of reasons.
        // This is also the implicit default value until text is measured, but don't rely on this.
        doReturn(View.LAYOUT_DIRECTION_RTL).when(mUrlBar).getLayoutDirection();
        mUrlBar.setHint("טקסט רמז");
        resetTextLayout();

        // As long as layouts are not available, no action should be taken.
        // This is typically the case when the text view or content is manipulated in some way and
        // has not yet completed the full measure/layout cycle.
        mUrlBar.scrollDisplayText(UrlBar.ScrollType.SCROLL_TO_BEGINNING);
        verify(mUrlBar, never()).scrollTo(anyInt(), anyInt());
        assertTrue(mUrlBar.hasPendingDisplayTextScrollForTesting());
        clearInvocations(mUrlBar);

        // RTL layout should position RTL text at an appropriate offset relative to view end.
        measureAndLayoutUrlBar();
        verify(mUrlBar).scrollTo(not(eq(0)), eq(0));
        assertFalse(mUrlBar.hasPendingDisplayTextScrollForTesting());
        clearInvocations(mUrlBar);

        // Simulate request to update scroll type with no changes of scroll type, text, or view
        // size. This should avoid recalculations and simply re-set the scroll position.
        mUrlBar.scrollDisplayText(UrlBar.ScrollType.SCROLL_TO_BEGINNING);
        verify(mUrlBar, never()).scrollToTLD();
        verify(mUrlBar, never()).scrollToBeginning();
        verify(mUrlBar).scrollTo(not(eq(0)), eq(0));
    }

    @Test
    public void layout_noScrollWithNoSizeChanges() {
        // Initialize the URL bar. Verify test conditions.
        mUrlBar.setText(mShortDomain);
        mUrlBar.scrollDisplayText(UrlBar.ScrollType.SCROLL_TO_BEGINNING);
        measureAndLayoutUrlBar();
        assertFalse(mUrlBar.hasPendingDisplayTextScrollForTesting());
        clearInvocations(mUrlBar);

        // Simulate layout re-entry.
        // We know the url bar has no pending scroll request, and we apply the same size.
        measureAndLayoutUrlBar();
        verify(mUrlBar, never()).scrollDisplayText(anyInt());
    }

    @Test
    public void layout_noScrollWhenHeightChanges() {
        // Initialize the URL bar. Verify test conditions.
        mUrlBar.setText(mShortDomain);
        mUrlBar.scrollDisplayText(UrlBar.ScrollType.SCROLL_TO_BEGINNING);
        measureAndLayoutUrlBar();
        assertFalse(mUrlBar.hasPendingDisplayTextScrollForTesting());
        clearInvocations(mUrlBar);

        // Simulate layout re-entry.
        // We change the height of the view which should not affect scroll position.
        measureAndLayoutUrlBarForSize(URL_BAR_WIDTH, URL_BAR_HEIGHT + 1);
        verify(mUrlBar, never()).scrollDisplayText(anyInt());
    }

    @Test
    public void layout_updateScrollWhenWidthChanges() {
        // Initialize the URL bar. Verify test conditions.
        mUrlBar.setText(mShortDomain);
        mUrlBar.scrollDisplayText(UrlBar.ScrollType.SCROLL_TO_BEGINNING);
        measureAndLayoutUrlBar();
        assertFalse(mUrlBar.hasPendingDisplayTextScrollForTesting());
        clearInvocations(mUrlBar);

        // Simulate layout re-entry.
        // We change the width, which may impact scroll position.
        measureAndLayoutUrlBarForSize(URL_BAR_WIDTH + 1, URL_BAR_HEIGHT);
        verify(mUrlBar).scrollDisplayText(anyInt());
    }

    @Test
    @EnableFeatures(ChromeFeatureList.ANDROID_NO_VISIBLE_HINT_FOR_DIFFERENT_TLD)
    public void scrollToTLD_sameTLD_calculateVisibleHint() {
        doReturn(mLayout).when(mUrlBar).getLayout();
        doReturn(mPaint).when(mLayout).getPaint();

        measureAndLayoutUrlBar();
        // Url needs to be long enough to fill the entire url bar.
        String url =
                mShortDomain
                        + "/"
                        + TextUtils.join(
                                "", Collections.nCopies(NUMBER_OF_VISIBLE_CHARACTERS, "a"));
        mUrlBar.setText(url);
        mUrlBar.setScrollState(UrlBar.ScrollType.SCROLL_TO_TLD, mShortDomain.length());
        verify(mUrlBar, times(0)).calculateVisibleHint();

        // Keep domain the same, but change the path.
        String url2 =
                mShortDomain
                        + "/"
                        + TextUtils.join(
                                "", Collections.nCopies(NUMBER_OF_VISIBLE_CHARACTERS, "b"));
        mUrlBar.setText(url2);
        mUrlBar.setScrollState(UrlBar.ScrollType.SCROLL_TO_TLD, mShortDomain.length());
        verify(mUrlBar, times(1)).calculateVisibleHint();
        String visibleHint = mUrlBar.getVisibleTextPrefixHint().toString();
        assertEquals(url2.substring(0, NUMBER_OF_VISIBLE_CHARACTERS + 1), visibleHint);
    }

    @Test
    @EnableFeatures(ChromeFeatureList.ANDROID_NO_VISIBLE_HINT_FOR_DIFFERENT_TLD)
    public void scrollToTLD_differentTLD_noVisibleHintCalculation() {
        doReturn(mLayout).when(mUrlBar).getLayout();
        doReturn(mPaint).when(mLayout).getPaint();

        measureAndLayoutUrlBar();
        // Url needs to be long enough to fill the entire url bar.
        String url =
                "www.a.com/"
                        + TextUtils.join(
                                "", Collections.nCopies(NUMBER_OF_VISIBLE_CHARACTERS, "a"));
        mUrlBar.setText(url);
        mUrlBar.setScrollState(UrlBar.ScrollType.SCROLL_TO_TLD, mShortDomain.length());
        verify(mUrlBar, times(0)).calculateVisibleHint();

        // Change the domain, but keep the path the same.
        String url2 =
                "www.b.com/"
                        + TextUtils.join(
                                "", Collections.nCopies(NUMBER_OF_VISIBLE_CHARACTERS, "a"));
        mUrlBar.setText(url2);
        mUrlBar.setScrollState(UrlBar.ScrollType.SCROLL_TO_TLD, mShortDomain.length());
        verify(mUrlBar, times(0)).calculateVisibleHint();
        assertNull(mUrlBar.getVisibleTextPrefixHint());
    }
}
