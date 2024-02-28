// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.tasks.tab_management;

import android.content.Context;
import android.content.res.Resources;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Matrix;
import android.graphics.Paint;
import android.graphics.PorterDuff;
import android.graphics.PorterDuffXfermode;
import android.graphics.Rect;
import android.graphics.RectF;
import android.graphics.drawable.Drawable;
import android.util.Size;

import androidx.annotation.NonNull;

import org.chromium.base.Callback;
import org.chromium.base.supplier.ObservableSupplier;
import org.chromium.base.task.PostTask;
import org.chromium.base.task.TaskTraits;
import org.chromium.chrome.browser.browser_controls.BrowserControlsStateProvider;
import org.chromium.chrome.browser.compositor.layouts.content.TabContentManager;
import org.chromium.chrome.browser.profiles.Profile;
import org.chromium.chrome.browser.tab.Tab;
import org.chromium.chrome.browser.tab.TabUtils;
import org.chromium.chrome.browser.tabmodel.TabModelFilter;
import org.chromium.chrome.browser.tabmodel.TabModelUtils;
import org.chromium.chrome.browser.tasks.pseudotab.PseudoTab;
import org.chromium.chrome.tab_ui.R;
import org.chromium.components.browser_ui.styles.SemanticColorUtils;
import org.chromium.url.GURL;

import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReference;

/**
 * A {@link ThumbnailProvider} that will create a single Bitmap Thumbnail for all
 * the related tabs for the given tabs.
 */
public class MultiThumbnailCardProvider implements ThumbnailProvider {
    private final TabContentManager mTabContentManager;
    private final ObservableSupplier<TabModelFilter> mCurrentTabModelFilterSupplier;
    private final Callback<TabModelFilter> mOnTabModelFilterChanged = this::onTabModelFilterChanged;

    private final float mRadius;
    private final float mFaviconFrameCornerRadius;
    private final Paint mEmptyThumbnailPaint;
    private final Paint mThumbnailFramePaint;
    private final Paint mThumbnailBasePaint;
    private final Paint mTextPaint;
    private final Paint mFaviconBackgroundPaint;
    private final Paint mSelectedEmptyThumbnailPaint;
    private final Paint mSelectedTextPaint;
    private final int mFaviconBackgroundPaintColor;
    private TabListFaviconProvider mTabListFaviconProvider;
    private Context mContext;
    private final BrowserControlsStateProvider mBrowserControlsStateProvider;

    private class MultiThumbnailFetcher {
        private final PseudoTab mInitialTab;
        private final Callback<Bitmap> mFinalCallback;
        private final boolean mForceUpdate;
        private final boolean mWriteToCache;
        private final boolean mIsTabSelected;
        private final List<PseudoTab> mTabs = new ArrayList<>(4);
        private final AtomicInteger mThumbnailsToFetch = new AtomicInteger();

        private Canvas mCanvas;
        private Bitmap mMultiThumbnailBitmap;
        private String mText;

        private final List<Rect> mFaviconRects = new ArrayList<>(4);
        private final List<RectF> mThumbnailRects = new ArrayList<>(4);
        private final List<RectF> mFaviconBackgroundRects = new ArrayList<>(4);
        private final int mThumbnailWidth;
        private final int mThumbnailHeight;

        /**
         * Fetcher that get the thumbnail drawable depending on if the tab is selected.
         * @see TabContentManager#getTabThumbnailWithCallback
         * @param initialTab Thumbnail is generated for tabs related to initialTab.
         * @param thumbnailSize Desired size of multi-thumbnail.
         * @param finalCallback Callback which receives generated bitmap.
         * @param forceUpdate, writeToCache Required for bitmap generator.
         * @param isTabSelected Whether the thumbnail is for a currently selected tab.
         */
        MultiThumbnailFetcher(
                PseudoTab initialTab,
                Size thumbnailSize,
                Callback<Bitmap> finalCallback,
                boolean forceUpdate,
                boolean writeToCache,
                boolean isTabSelected) {
            mFinalCallback = finalCallback;
            mInitialTab = initialTab;
            mForceUpdate = forceUpdate;
            mWriteToCache = writeToCache;
            mIsTabSelected = isTabSelected;

            if (thumbnailSize == null
                    || thumbnailSize.getHeight() <= 0
                    || thumbnailSize.getWidth() <= 0) {
                float expectedThumbnailAspectRatio =
                        TabUtils.getTabThumbnailAspectRatio(
                                mContext, mBrowserControlsStateProvider);
                mThumbnailWidth =
                        (int)
                                mContext.getResources()
                                        .getDimension(R.dimen.tab_grid_thumbnail_card_default_size);
                mThumbnailHeight = (int) (mThumbnailWidth / expectedThumbnailAspectRatio);
            } else {
                mThumbnailWidth = thumbnailSize.getWidth();
                mThumbnailHeight = thumbnailSize.getHeight();
            }
        }

        /** Initialize rects used for thumbnails. */
        private void initializeRects(Context context) {
            float thumbnailHorizontalPadding =
                    TabUiThemeProvider.getTabMiniThumbnailPaddingDimension(context);
            float thumbnailVerticalPadding = thumbnailHorizontalPadding;

            float centerX = mThumbnailWidth * 0.5f;
            float centerY = mThumbnailHeight * 0.5f;
            float halfThumbnailHorizontalPadding = thumbnailHorizontalPadding / 2;
            float halfThumbnailVerticalPadding = thumbnailVerticalPadding / 2;

            mThumbnailRects.add(
                    new RectF(
                            0,
                            0,
                            centerX - halfThumbnailHorizontalPadding,
                            centerY - halfThumbnailVerticalPadding));
            mThumbnailRects.add(
                    new RectF(
                            centerX + halfThumbnailHorizontalPadding,
                            0,
                            mThumbnailWidth,
                            centerY - halfThumbnailVerticalPadding));
            mThumbnailRects.add(
                    new RectF(
                            0,
                            centerY + halfThumbnailVerticalPadding,
                            centerX - halfThumbnailHorizontalPadding,
                            mThumbnailHeight));
            mThumbnailRects.add(
                    new RectF(
                            centerX + halfThumbnailHorizontalPadding,
                            centerY + halfThumbnailVerticalPadding,
                            mThumbnailWidth,
                            mThumbnailHeight));

            // Initialize Rects for favicons and favicon frame.
            final float halfFaviconFrameSize =
                    mContext.getResources()
                                    .getDimension(R.dimen.tab_grid_thumbnail_favicon_frame_size)
                            / 2f;
            float thumbnailFaviconPaddingFromBackground =
                    mContext.getResources()
                            .getDimension(R.dimen.tab_grid_thumbnail_favicon_padding_from_frame);
            for (int i = 0; i < 4; i++) {
                RectF thumbnailRect = mThumbnailRects.get(i);

                float thumbnailRectCenterX = thumbnailRect.centerX();
                float thumbnailRectCenterY = thumbnailRect.centerY();
                RectF faviconBackgroundRect =
                        new RectF(
                                thumbnailRectCenterX,
                                thumbnailRectCenterY,
                                thumbnailRectCenterX,
                                thumbnailRectCenterY);
                faviconBackgroundRect.inset(-halfFaviconFrameSize, -halfFaviconFrameSize);
                mFaviconBackgroundRects.add(faviconBackgroundRect);

                RectF faviconRectF = new RectF(faviconBackgroundRect);
                faviconRectF.inset(
                        thumbnailFaviconPaddingFromBackground,
                        thumbnailFaviconPaddingFromBackground);
                Rect faviconRect = new Rect();
                faviconRectF.roundOut(faviconRect);
                mFaviconRects.add(faviconRect);
            }
        }

        private void initializeAndStartFetching(PseudoTab tab) {
            // Initialize mMultiThumbnailBitmap.
            mMultiThumbnailBitmap =
                    Bitmap.createBitmap(mThumbnailWidth, mThumbnailHeight, Bitmap.Config.ARGB_8888);
            mCanvas = new Canvas(mMultiThumbnailBitmap);
            mCanvas.drawColor(Color.TRANSPARENT);

            // Initialize Tabs.
            List<PseudoTab> relatedTabList =
                    PseudoTab.getRelatedTabs(mContext, tab, mCurrentTabModelFilterSupplier.get());
            if (relatedTabList.size() <= 4) {
                mThumbnailsToFetch.set(relatedTabList.size());

                mTabs.add(tab);
                relatedTabList.remove(tab);

                for (int i = 0; i < 3; i++) {
                    mTabs.add(i < relatedTabList.size() ? relatedTabList.get(i) : null);
                }
            } else {
                mText = "+" + (relatedTabList.size() - 3);
                mThumbnailsToFetch.set(3);

                mTabs.add(tab);
                relatedTabList.remove(tab);

                mTabs.add(relatedTabList.get(0));
                mTabs.add(relatedTabList.get(1));
                mTabs.add(null);
            }

            // Fetch and draw all.
            for (int i = 0; i < 4; i++) {
                if (mTabs.get(i) != null) {
                    final int index = i;
                    final GURL url = mTabs.get(i).getUrl();
                    final boolean isIncognito = mTabs.get(i).isIncognito();
                    final Size tabThumbnailSize =
                            new Size(
                                    (int) mThumbnailRects.get(i).width(),
                                    (int) mThumbnailRects.get(i).height());
                    // getTabThumbnailWithCallback() might call the callback up to twice,
                    // so use |lastFavicon| to avoid fetching the favicon the second time.
                    // Fetching the favicon after getting the live thumbnail would lead to
                    // visible flicker.
                    final AtomicReference<Drawable> lastFavicon = new AtomicReference<>();
                    mTabContentManager.getTabThumbnailWithCallback(
                            mTabs.get(i).getId(),
                            tabThumbnailSize,
                            thumbnail -> {
                                drawThumbnailBitmapOnCanvasWithFrame(thumbnail, index);
                                if (lastFavicon.get() != null) {
                                    drawFaviconThenMaybeSendBack(lastFavicon.get(), index);
                                } else {
                                    mTabListFaviconProvider.getFaviconDrawableForUrlAsync(
                                            url,
                                            isIncognito,
                                            (Drawable favicon) -> {
                                                lastFavicon.set(favicon);
                                                drawFaviconThenMaybeSendBack(favicon, index);
                                            });
                                }
                            },
                            mForceUpdate && i == 0,
                            mWriteToCache && i == 0);
                } else {
                    drawThumbnailBitmapOnCanvasWithFrame(null, i);
                    if (mText != null && i == 3) {
                        // Draw the text exactly centered on the thumbnail rect.
                        Paint textPaint = mIsTabSelected ? mSelectedTextPaint : mTextPaint;
                        mCanvas.drawText(
                                mText,
                                (mThumbnailRects.get(i).left + mThumbnailRects.get(i).right) / 2,
                                (mThumbnailRects.get(i).top + mThumbnailRects.get(i).bottom) / 2
                                        - ((mTextPaint.descent() + mTextPaint.ascent()) / 2),
                                textPaint);
                    }
                }
            }
        }

        private void drawThumbnailBitmapOnCanvasWithFrame(Bitmap thumbnail, int index) {
            final RectF rect = mThumbnailRects.get(index);
            if (thumbnail == null) {
                Paint emptyThumbnailPaint =
                        mIsTabSelected ? mSelectedEmptyThumbnailPaint : mEmptyThumbnailPaint;
                mCanvas.drawRoundRect(rect, mRadius, mRadius, emptyThumbnailPaint);
                return;
            }

            mCanvas.save();
            mCanvas.clipRect(rect);
            Matrix m = new Matrix();

            final float newWidth = rect.width();
            final float scale =
                    Math.max(
                            newWidth / thumbnail.getWidth(), rect.height() / thumbnail.getHeight());
            m.setScale(scale, scale);
            final float xOffset =
                    rect.left + (int) ((newWidth - (thumbnail.getWidth() * scale)) / 2);
            final float yOffset = rect.top;
            m.postTranslate(xOffset, yOffset);

            // Draw the base paint first and set the base for thumbnail to draw. Setting the xfer
            // mode as SRC_OVER so the thumbnail can be drawn on top of this paint. See
            // https://crbug.com/1227619.
            mThumbnailBasePaint.setXfermode(new PorterDuffXfermode(PorterDuff.Mode.SRC_OVER));
            mCanvas.drawRoundRect(rect, mRadius, mRadius, mThumbnailBasePaint);

            mThumbnailBasePaint.setXfermode(new PorterDuffXfermode(PorterDuff.Mode.SRC_IN));
            mCanvas.drawBitmap(thumbnail, m, mThumbnailBasePaint);
            mCanvas.restore();
            thumbnail.recycle();
        }

        private void drawFaviconDrawableOnCanvasWithFrame(Drawable favicon, int index) {
            mCanvas.drawRoundRect(
                    mFaviconBackgroundRects.get(index),
                    mFaviconFrameCornerRadius,
                    mFaviconFrameCornerRadius,
                    mFaviconBackgroundPaint);
            Rect oldBounds = new Rect(favicon.getBounds());
            favicon.setBounds(mFaviconRects.get(index));
            favicon.draw(mCanvas);
            // Restore the bounds since this may be a shared drawable.
            favicon.setBounds(oldBounds);
        }

        private void drawFaviconThenMaybeSendBack(Drawable favicon, int index) {
            drawFaviconDrawableOnCanvasWithFrame(favicon, index);
            if (mThumbnailsToFetch.decrementAndGet() == 0) {
                PostTask.postTask(
                        TaskTraits.UI_USER_VISIBLE, mFinalCallback.bind(mMultiThumbnailBitmap));
            }
        }

        private void fetch() {
            initializeRects(mContext);
            initializeAndStartFetching(mInitialTab);
        }
    }

    MultiThumbnailCardProvider(
            @NonNull Context context,
            @NonNull BrowserControlsStateProvider browserControlsStateProvider,
            @NonNull TabContentManager tabContentManager,
            @NonNull ObservableSupplier<TabModelFilter> currentTabModelFilterSupplier) {
        mContext = context;
        mBrowserControlsStateProvider = browserControlsStateProvider;
        Resources resources = context.getResources();

        mTabContentManager = tabContentManager;
        mCurrentTabModelFilterSupplier = currentTabModelFilterSupplier;
        mRadius = resources.getDimension(R.dimen.tab_list_mini_card_radius);
        mFaviconFrameCornerRadius =
                resources.getDimension(R.dimen.tab_grid_thumbnail_favicon_frame_corner_radius);

        mTabListFaviconProvider =
                new TabListFaviconProvider(context, false, R.dimen.default_favicon_corner_radius);

        // Initialize Paints to use.
        mEmptyThumbnailPaint = new Paint();
        mEmptyThumbnailPaint.setStyle(Paint.Style.FILL);
        mEmptyThumbnailPaint.setAntiAlias(true);
        mEmptyThumbnailPaint.setColor(
                TabUiThemeProvider.getMiniThumbnailPlaceholderColor(context, false, false));

        mSelectedEmptyThumbnailPaint = new Paint(mEmptyThumbnailPaint);
        mSelectedEmptyThumbnailPaint.setColor(
                TabUiThemeProvider.getMiniThumbnailPlaceholderColor(context, false, true));

        // Paint used to set base for thumbnails, in case mEmptyThumbnailPaint has transparency.
        mThumbnailBasePaint = new Paint(mEmptyThumbnailPaint);
        mThumbnailBasePaint.setColor(Color.BLACK);
        mThumbnailBasePaint.setXfermode(new PorterDuffXfermode(PorterDuff.Mode.SRC_IN));

        mThumbnailFramePaint = new Paint();
        mThumbnailFramePaint.setStyle(Paint.Style.STROKE);
        mThumbnailFramePaint.setStrokeWidth(
                resources.getDimension(R.dimen.tab_list_mini_card_frame_size));
        mThumbnailFramePaint.setColor(SemanticColorUtils.getDividerLineBgColor(context));
        mThumbnailFramePaint.setAntiAlias(true);

        // TODO(996048): Use pre-defined styles to avoid style out of sync if any text/color styles
        // changes.
        mTextPaint = new Paint();
        mTextPaint.setTextSize(resources.getDimension(R.dimen.compositor_tab_title_text_size));
        mTextPaint.setFakeBoldText(true);
        mTextPaint.setAntiAlias(true);
        mTextPaint.setTextAlign(Paint.Align.CENTER);
        mTextPaint.setColor(TabUiThemeProvider.getTabGroupNumberTextColor(context, false, false));

        mSelectedTextPaint = new Paint(mTextPaint);
        mSelectedTextPaint.setColor(
                TabUiThemeProvider.getTabGroupNumberTextColor(context, false, true));

        mFaviconBackgroundPaintColor = context.getColor(R.color.favicon_background_color);
        mFaviconBackgroundPaint = new Paint();
        mFaviconBackgroundPaint.setAntiAlias(true);
        mFaviconBackgroundPaint.setColor(mFaviconBackgroundPaintColor);
        mFaviconBackgroundPaint.setStyle(Paint.Style.FILL);
        mFaviconBackgroundPaint.setShadowLayer(
                resources.getDimension(R.dimen.tab_grid_thumbnail_favicon_background_radius),
                0,
                resources.getDimension(R.dimen.tab_grid_thumbnail_favicon_background_down_shift),
                context.getColor(R.color.modern_grey_800_alpha_38));

        mCurrentTabModelFilterSupplier.addObserver(mOnTabModelFilterChanged);
    }

    private void onTabModelFilterChanged(TabModelFilter filter) {
        boolean isIncognito = filter.isIncognito();
        mEmptyThumbnailPaint.setColor(
                TabUiThemeProvider.getMiniThumbnailPlaceholderColor(mContext, isIncognito, false));
        mTextPaint.setColor(
                TabUiThemeProvider.getTabGroupNumberTextColor(mContext, isIncognito, false));
        mThumbnailFramePaint.setColor(
                TabUiThemeProvider.getMiniThumbnailFrameColor(mContext, isIncognito));
        mFaviconBackgroundPaint.setColor(
                TabUiThemeProvider.getFaviconBackgroundColor(mContext, isIncognito));

        mSelectedEmptyThumbnailPaint.setColor(
                TabUiThemeProvider.getMiniThumbnailPlaceholderColor(mContext, isIncognito, true));
        mSelectedTextPaint.setColor(
                TabUiThemeProvider.getTabGroupNumberTextColor(mContext, isIncognito, true));
    }

    /**
     * @param regularProfile The regular profile to use for favicons.
     */
    public void initWithNative(Profile regularProfile) {
        mTabListFaviconProvider.initWithNative(regularProfile);
    }

    /** Destroy any member that needs clean up. */
    public void destroy() {
        mCurrentTabModelFilterSupplier.removeObserver(mOnTabModelFilterChanged);
    }

    @Override
    public void getTabThumbnailWithCallback(
            int tabId,
            Size thumbnailSize,
            Callback<Bitmap> finalCallback,
            boolean forceUpdate,
            boolean writeToCache,
            boolean isSelected) {
        TabModelFilter filter = mCurrentTabModelFilterSupplier.get();
        Tab tab = TabModelUtils.getTabById(filter, tabId);
        PseudoTab pseudoTab = (tab != null) ? PseudoTab.fromTab(tab) : PseudoTab.fromTabId(tabId);
        if (pseudoTab == null
                || PseudoTab.getRelatedTabs(mContext, pseudoTab, filter).size() == 1) {
            mTabContentManager.getTabThumbnailWithCallback(
                    tabId, thumbnailSize, finalCallback, forceUpdate, writeToCache);
            return;
        }
        new MultiThumbnailFetcher(
                        pseudoTab,
                        thumbnailSize,
                        finalCallback,
                        forceUpdate,
                        writeToCache,
                        isSelected)
                .fetch();
    }
}
