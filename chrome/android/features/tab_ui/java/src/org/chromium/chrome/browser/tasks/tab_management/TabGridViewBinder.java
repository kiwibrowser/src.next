// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.tasks.tab_management;

import static org.chromium.chrome.browser.tasks.tab_management.TabListModel.CardProperties.CARD_ALPHA;

import android.content.res.ColorStateList;
import android.graphics.Bitmap;
import android.graphics.Matrix;
import android.text.TextUtils;
import android.util.Size;
import android.view.View;
import android.view.ViewGroup;
import android.view.ViewGroup.LayoutParams;
import android.widget.ImageView;
import android.widget.ImageView.ScaleType;
import android.widget.TextView;

import androidx.annotation.ColorInt;
import androidx.annotation.Nullable;
import androidx.annotation.VisibleForTesting;
import androidx.core.graphics.drawable.DrawableCompat;
import androidx.core.view.ViewCompat;
import androidx.vectordrawable.graphics.drawable.AnimatedVectorDrawableCompat;

import org.chromium.base.ApiCompatibilityUtils;
import org.chromium.base.Callback;
import org.chromium.chrome.browser.tab.TabUtils;
import org.chromium.chrome.browser.tab.state.ShoppingPersistedTabData;
import org.chromium.chrome.tab_ui.R;
import org.chromium.components.browser_ui.widget.chips.ChipView;
import org.chromium.ui.modelutil.PropertyKey;
import org.chromium.ui.modelutil.PropertyModel;
import org.chromium.ui.widget.ButtonCompat;
import org.chromium.ui.widget.ChromeImageView;
import org.chromium.ui.widget.ViewLookupCachingFrameLayout;

/**
 * {@link org.chromium.ui.modelutil.SimpleRecyclerViewMcp.ViewBinder} for tab grid.
 * This class supports both full and partial updates to the {@link TabGridViewHolder}.
 */
class TabGridViewBinder {
    private static TabListMediator.ThumbnailFetcher sThumbnailFetcherForTesting;
    private static final String SHOPPING_METRICS_IDENTIFIER = "EnterTabSwitcher";
    /**
     * Bind a closable tab to a view.
     * @param model The model to bind.
     * @param view The view to bind to.
     * @param propertyKey The property that changed.
     */
    public static void bindClosableTab(
            PropertyModel model, ViewGroup view, @Nullable PropertyKey propertyKey) {
        assert view instanceof ViewLookupCachingFrameLayout;
        if (propertyKey == null) {
            onBindAll((ViewLookupCachingFrameLayout) view, model, TabProperties.UiType.CLOSABLE);
            return;
        }
        bindCommonProperties(model, (ViewLookupCachingFrameLayout) view, propertyKey);
        bindClosableTabProperties(model, (ViewLookupCachingFrameLayout) view, propertyKey);
    }

    /**
     * Bind a selectable tab to a view.
     * @param model The model to bind.
     * @param view The view to bind to.
     * @param propertyKey The property that changed.
     */
    public static void bindSelectableTab(
            PropertyModel model, ViewGroup view, @Nullable PropertyKey propertyKey) {
        assert view instanceof ViewLookupCachingFrameLayout;
        if (propertyKey == null) {
            onBindAll((ViewLookupCachingFrameLayout) view, model, TabProperties.UiType.SELECTABLE);
            return;
        }
        bindCommonProperties(model, (ViewLookupCachingFrameLayout) view, propertyKey);
        bindSelectableTabProperties(model, (ViewLookupCachingFrameLayout) view, propertyKey);
    }

    /**
     * Rebind all properties on a model to the view.
     * @param view The view to bind to.
     * @param model The model to bind.
     * @param viewType The view type to bind.
     */
    private static void onBindAll(ViewLookupCachingFrameLayout view, PropertyModel model,
            @TabProperties.UiType int viewType) {
        for (PropertyKey propertyKey : TabProperties.ALL_KEYS_TAB_GRID) {
            bindCommonProperties(model, view, propertyKey);
            switch (viewType) {
                case TabProperties.UiType.SELECTABLE:
                    bindSelectableTabProperties(model, view, propertyKey);
                    break;
                case TabProperties.UiType.CLOSABLE:
                    bindClosableTabProperties(model, view, propertyKey);
                    break;
                default:
                    assert false;
            }
        }
    }

    private static void bindCommonProperties(PropertyModel model, ViewLookupCachingFrameLayout view,
            @Nullable PropertyKey propertyKey) {
        if (TabProperties.TITLE == propertyKey) {
            String title = model.get(TabProperties.TITLE);
            TextView tabTitleView = (TextView) view.fastFindViewById(R.id.tab_title);
            tabTitleView.setText(title);
            tabTitleView.setContentDescription(
                    view.getResources().getString(R.string.accessibility_tabstrip_tab, title));
        } else if (TabProperties.IS_SELECTED == propertyKey) {
            updateColor(view, model.get(TabProperties.IS_INCOGNITO),
                    model.get(TabProperties.IS_SELECTED));
            updateFavicon(view, model);
            if (TabUiFeatureUtilities.ENABLE_SEARCH_CHIP.getValue()) {
                ChipView pageInfoButton = (ChipView) view.fastFindViewById(R.id.page_info_button);
                pageInfoButton.getPrimaryTextView().setTextAlignment(
                        View.TEXT_ALIGNMENT_VIEW_START);
                pageInfoButton.getPrimaryTextView().setEllipsize(TextUtils.TruncateAt.END);
                // TODO(crbug.com/1048255): The selected state of ChipView doesn't look elevated.
                //  Fix the elevation in style instead.
                pageInfoButton.setSelected(false);
            }
        } else if (TabProperties.FAVICON == propertyKey) {
            updateFavicon(view, model);
        } else if (TabProperties.THUMBNAIL_FETCHER == propertyKey) {
            updateThumbnail(view, model);
        } else if (TabProperties.CONTENT_DESCRIPTION_STRING == propertyKey) {
            view.setContentDescription(model.get(TabProperties.CONTENT_DESCRIPTION_STRING));
        } else if (TabProperties.GRID_CARD_SIZE == propertyKey) {
            final Size cardSize = model.get(TabProperties.GRID_CARD_SIZE);
            view.setMinimumHeight(cardSize.getHeight());
            view.setMinimumWidth(cardSize.getWidth());
            view.getLayoutParams().height = cardSize.getHeight();
            view.getLayoutParams().width = cardSize.getWidth();
            view.setLayoutParams(view.getLayoutParams());
            TabGridThumbnailView thumbnail =
                    (TabGridThumbnailView) view.fastFindViewById(R.id.tab_thumbnail);
            thumbnail.getLayoutParams().height = LayoutParams.MATCH_PARENT;
            updateThumbnail(view, model);
        }
    }

    private static void bindClosableTabProperties(
            PropertyModel model, ViewLookupCachingFrameLayout view, PropertyKey propertyKey) {
        if (TabProperties.TAB_CLOSED_LISTENER == propertyKey) {
            if (model.get(TabProperties.TAB_CLOSED_LISTENER) == null) {
                view.fastFindViewById(R.id.action_button).setOnClickListener(null);
            } else {
                view.fastFindViewById(R.id.action_button).setOnClickListener(v -> {
                    int tabId = model.get(TabProperties.TAB_ID);
                    model.get(TabProperties.TAB_CLOSED_LISTENER).run(tabId);
                });
            }
        } else if (TabProperties.TAB_SELECTED_LISTENER == propertyKey) {
            if (model.get(TabProperties.TAB_SELECTED_LISTENER) == null) {
                view.setOnClickListener(null);
            } else {
                view.setOnClickListener(v -> {
                    int tabId = model.get(TabProperties.TAB_ID);
                    model.get(TabProperties.TAB_SELECTED_LISTENER).run(tabId);
                });
            }
        } else if (TabProperties.CREATE_GROUP_LISTENER == propertyKey) {
            TabListMediator.TabActionListener listener =
                    model.get(TabProperties.CREATE_GROUP_LISTENER);
            ButtonCompat createGroupButton =
                    (ButtonCompat) view.fastFindViewById(R.id.create_group_button);
            if (listener == null) {
                createGroupButton.setVisibility(View.GONE);
                createGroupButton.setOnClickListener(null);
                return;
            }
            createGroupButton.setVisibility(View.VISIBLE);
            createGroupButton.setOnClickListener(v -> {
                int tabId = model.get(TabProperties.TAB_ID);
                listener.run(tabId);
            });
        } else if (CARD_ALPHA == propertyKey) {
            view.setAlpha(model.get(CARD_ALPHA));
        } else if (TabProperties.TITLE == propertyKey) {
            if (TabUiFeatureUtilities.isLaunchPolishEnabled()) return;
            String title = model.get(TabProperties.TITLE);
            view.fastFindViewById(R.id.action_button)
                    .setContentDescription(view.getResources().getString(
                            R.string.accessibility_tabstrip_btn_close_tab, title));
        } else if (TabProperties.IPH_PROVIDER == propertyKey) {
            TabListMediator.IphProvider provider = model.get(TabProperties.IPH_PROVIDER);
            if (provider != null) provider.showIPH(view.fastFindViewById(R.id.tab_thumbnail));
        } else if (TabProperties.CARD_ANIMATION_STATUS == propertyKey) {
            ((ClosableTabGridView) view)
                    .scaleTabGridCardView(model.get(TabProperties.CARD_ANIMATION_STATUS));
        } else if (TabProperties.IS_INCOGNITO == propertyKey) {
            updateColor(view, model.get(TabProperties.IS_INCOGNITO),
                    model.get(TabProperties.IS_SELECTED));
            updateColorForActionButton(view, model.get(TabProperties.IS_INCOGNITO),
                    model.get(TabProperties.IS_SELECTED));
        } else if (TabProperties.ACCESSIBILITY_DELEGATE == propertyKey) {
            view.setAccessibilityDelegate(model.get(TabProperties.ACCESSIBILITY_DELEGATE));
        } else if (TabProperties.SEARCH_QUERY == propertyKey) {
            String query = model.get(TabProperties.SEARCH_QUERY);
            ChipView pageInfoButton = (ChipView) view.fastFindViewById(R.id.page_info_button);
            if (TextUtils.isEmpty(query)) {
                pageInfoButton.setVisibility(View.GONE);
            } else {
                // Search query and price string are mutually exclusive
                assert model.get(TabProperties.SHOPPING_PERSISTED_TAB_DATA_FETCHER) == null;
                pageInfoButton.setVisibility(View.VISIBLE);
                pageInfoButton.getPrimaryTextView().setText(query);
            }
        } else if (TabProperties.SHOPPING_PERSISTED_TAB_DATA_FETCHER == propertyKey) {
            fetchPriceDrop(model, (priceDrop) -> {
                PriceCardView priceCardView =
                        (PriceCardView) view.fastFindViewById(R.id.price_info_box_outer);
                if (priceDrop == null) {
                    priceCardView.setVisibility(View.GONE);
                    return;
                }
                priceCardView.setPriceStrings(priceDrop.price, priceDrop.previousPrice);
                priceCardView.setVisibility(View.VISIBLE);
                priceCardView.setContentDescription(
                        view.getResources().getString(R.string.accessibility_tab_price_card,
                                priceDrop.previousPrice, priceDrop.price));
            }, true);
        } else if (TabProperties.COUPON_PERSISTED_TAB_DATA_FETCHER == propertyKey) {
            CouponCardView couponCardView =
                    (CouponCardView) view.fastFindViewById(R.id.coupon_info_box_outer);
            if (model.get(TabProperties.COUPON_PERSISTED_TAB_DATA_FETCHER) == null) {
                couponCardView.setVisibility(View.GONE);
                return;
            }
            fetchPriceDrop(model, (priceDrop) -> {
                if (priceDrop != null) {
                    couponCardView.setVisibility(View.GONE);
                    return;
                }
                model.get(TabProperties.COUPON_PERSISTED_TAB_DATA_FETCHER)
                        .fetch((couponPersistedTabData) -> {
                            // TODO(crbug.com/1337117): add logging for when
                            // couponPersistedTabData is not null
                            if (couponPersistedTabData == null
                                    || couponPersistedTabData.getCouponAnnotationText() == null) {
                                couponCardView.setVisibility(View.GONE);
                            } else {
                                couponCardView.setCouponString(
                                        couponPersistedTabData.getCouponAnnotationText());
                                couponCardView.setVisibility(View.VISIBLE);
                            }
                        });
            }, false);
        } else if (TabProperties.STORE_PERSISTED_TAB_DATA_FETCHER == propertyKey) {
            StoreHoursCardView storeHoursCardView =
                    (StoreHoursCardView) view.fastFindViewById(R.id.store_hours_box_outer);
            if (model.get(TabProperties.STORE_PERSISTED_TAB_DATA_FETCHER) != null) {
                model.get(TabProperties.STORE_PERSISTED_TAB_DATA_FETCHER)
                        .fetch((storePersistedTabData) -> {
                            if (storePersistedTabData == null
                                    || TextUtils.isEmpty(
                                            storePersistedTabData.getStoreHoursString())) {
                                storeHoursCardView.setVisibility(View.GONE);
                            } else {
                                storeHoursCardView.setStoreHours(
                                        storePersistedTabData.getStoreHoursString());
                                storeHoursCardView.setVisibility(View.VISIBLE);
                            }
                        });
            } else {
                storeHoursCardView.setVisibility(View.GONE);
            }
        } else if (TabProperties.SHOULD_SHOW_PRICE_DROP_TOOLTIP == propertyKey) {
            if (model.get(TabProperties.SHOULD_SHOW_PRICE_DROP_TOOLTIP)) {
                PriceCardView priceCardView =
                        (PriceCardView) view.fastFindViewById(R.id.price_info_box_outer);
                assert priceCardView.getVisibility() == View.VISIBLE;
                LargeMessageCardView.showPriceDropTooltip(
                        priceCardView.findViewById(R.id.current_price));
            }
        } else if (TabProperties.PAGE_INFO_LISTENER == propertyKey) {
            TabListMediator.TabActionListener listener =
                    model.get(TabProperties.PAGE_INFO_LISTENER);
            ChipView pageInfoButton = (ChipView) view.fastFindViewById(R.id.page_info_button);
            if (listener == null) {
                pageInfoButton.setOnClickListener(null);
                return;
            }
            pageInfoButton.setOnClickListener(v -> {
                int tabId = model.get(TabProperties.TAB_ID);
                listener.run(tabId);
            });
        } else if (TabProperties.PAGE_INFO_ICON_DRAWABLE_ID == propertyKey) {
            ChipView pageInfoButton = (ChipView) view.fastFindViewById(R.id.page_info_button);
            int iconDrawableId = model.get(TabProperties.PAGE_INFO_ICON_DRAWABLE_ID);
            boolean shouldTint = iconDrawableId != R.drawable.ic_logo_googleg_24dp;
            pageInfoButton.setIcon(iconDrawableId, shouldTint);
        } else if (TabProperties.IS_SELECTED == propertyKey) {
            view.setSelected(model.get(TabProperties.IS_SELECTED));
            updateColorForActionButton(view, model.get(TabProperties.IS_INCOGNITO),
                    model.get(TabProperties.IS_SELECTED));
        } else if (TabUiFeatureUtilities.isLaunchPolishEnabled()
                && TabProperties.CLOSE_BUTTON_DESCRIPTION_STRING == propertyKey) {
            view.fastFindViewById(R.id.action_button)
                    .setContentDescription(
                            model.get(TabProperties.CLOSE_BUTTON_DESCRIPTION_STRING));
        }
    }

    private static void bindSelectableTabProperties(
            PropertyModel model, ViewLookupCachingFrameLayout view, PropertyKey propertyKey) {
        final int tabId = model.get(TabProperties.TAB_ID);

        if (TabProperties.IS_SELECTED == propertyKey) {
            updateColorForSelectionToggleButton(view, model.get(TabProperties.IS_INCOGNITO),
                    model.get(TabProperties.IS_SELECTED));
        } else if (TabProperties.SELECTABLE_TAB_CLICKED_LISTENER == propertyKey) {
            view.setOnClickListener(v -> {
                model.get(TabProperties.SELECTABLE_TAB_CLICKED_LISTENER).run(tabId);
                ((SelectableTabGridView) view).onClick();
            });
            view.setOnLongClickListener(v -> {
                model.get(TabProperties.SELECTABLE_TAB_CLICKED_LISTENER).run(tabId);
                return ((SelectableTabGridView) view).onLongClick(view);
            });
        } else if (TabProperties.TAB_SELECTION_DELEGATE == propertyKey) {
            assert model.get(TabProperties.TAB_SELECTION_DELEGATE) != null;

            ((SelectableTabGridView) view)
                    .setSelectionDelegate(model.get(TabProperties.TAB_SELECTION_DELEGATE));
            ((SelectableTabGridView) view).setItem(tabId);
        } else if (TabProperties.IS_INCOGNITO == propertyKey) {
            updateColor(view, model.get(TabProperties.IS_INCOGNITO),
                    model.get(TabProperties.IS_SELECTED));
            updateColorForSelectionToggleButton(view, model.get(TabProperties.IS_INCOGNITO),
                    model.get(TabProperties.IS_SELECTED));
        }
    }

    private static void fetchPriceDrop(PropertyModel model,
            Callback<ShoppingPersistedTabData.PriceDrop> callback, boolean shouldLog) {
        if (model.get(TabProperties.SHOPPING_PERSISTED_TAB_DATA_FETCHER) == null) {
            callback.onResult(null);
            return;
        }
        model.get(TabProperties.SHOPPING_PERSISTED_TAB_DATA_FETCHER)
                .fetch((shoppingPersistedTabData) -> {
                    if (shoppingPersistedTabData == null) {
                        callback.onResult(null);
                        return;
                    }
                    if (shouldLog) {
                        shoppingPersistedTabData.logPriceDropMetrics(SHOPPING_METRICS_IDENTIFIER);
                    }
                    callback.onResult(shoppingPersistedTabData.getPriceDrop());
                });
    }

    private static void updateThumbnail(ViewLookupCachingFrameLayout view, PropertyModel model) {
        TabListMediator.ThumbnailFetcher fetcher = model.get(TabProperties.THUMBNAIL_FETCHER);
        TabGridThumbnailView thumbnail =
                (TabGridThumbnailView) view.fastFindViewById(R.id.tab_thumbnail);
        thumbnail.maybeAdjustThumbnailHeight();
        if (fetcher == null) {
            thumbnail.setImageDrawable(null);
            return;
        }
        // Use placeholder drawable before the real thumbnail is available.
        thumbnail.setColorThumbnailPlaceHolder(
                model.get(TabProperties.IS_INCOGNITO), model.get(TabProperties.IS_SELECTED));

        final Size cardSize = model.get(TabProperties.GRID_CARD_SIZE);
        final Size thumbnailSize =
                TabUiFeatureUtilities.isTabletGridTabSwitcherPolishEnabled(view.getContext())
                        && cardSize != null
                ? TabUtils.deriveThumbnailSize(cardSize, view.getContext())
                : null;
        Callback<Bitmap> callback = result -> {
            if (result != null) {
                if (TabUiFeatureUtilities.isTabletGridTabSwitcherPolishEnabled(view.getContext())
                        && model.get(TabProperties.GRID_CARD_SIZE) != null) {
                    // Adjust bitmap to thumbnail.
                    Size destSize = TabUtils.deriveThumbnailSize(
                            model.get(TabProperties.GRID_CARD_SIZE), view.getContext());
                    updateThumbnailMatrix(thumbnail, result, destSize);
                } else {
                    thumbnail.setScaleType(ScaleType.FIT_CENTER);
                    thumbnail.setAdjustViewBounds(true);
                }
                thumbnail.setImageBitmap(result);
            }
        };
        if (TabUiFeatureUtilities.isLaunchPolishEnabled() && sThumbnailFetcherForTesting != null) {
            sThumbnailFetcherForTesting.fetch(callback, thumbnailSize);
        } else {
            fetcher.fetch(callback, thumbnailSize);
        }
    }

    /**
     * Update @{@link Matrix} of ImageView. Bitmap is scaled to larger of the two dimens, then
     * top-center aligned.
     * @param thumbnail Destination image view @{@link TabGridThumbnailView}.
     * @param source Image bitmap to resize.
     * @param destinationSize Desired width and height for source.
     */
    private static void updateThumbnailMatrix(
            TabGridThumbnailView thumbnail, Bitmap source, Size destinationSize) {
        int newWidth = destinationSize == null ? 0 : destinationSize.getWidth();
        int newHeight = destinationSize == null ? 0 : destinationSize.getHeight();
        if (newWidth <= 0 || newHeight <= 0
                || (newWidth == source.getWidth() && newHeight == source.getHeight())) {
            thumbnail.setScaleType(ScaleType.FIT_CENTER);
            return;
        }

        final Matrix m = new Matrix();

        // Scale image to larger of the two dimensions.
        final float scale = Math.max(
                (float) newWidth / source.getWidth(), (float) newHeight / source.getHeight()); //
        m.setScale(scale, scale);

        /**
         * Bitmap is top-left aligned by default. We want to translate the image to be horizontally
         * center-aligned. |destination width - scaled width| is the width that is out of view
         * bounds. We need to translate bitmap (to left) by half of this distance.
         */
        final int xOffset = (int) ((newWidth - (source.getWidth() * scale)) / 2);
        m.postTranslate(xOffset, 0);

        thumbnail.setScaleType(ScaleType.MATRIX);
        thumbnail.setImageMatrix(m);
    }

    /**
     * Update the favicon drawable to use from {@link TabListFaviconProvider.TabFavicon}, and the
     * padding around it. The color work is already handled when favicon is bind in {@link
     * #bindCommonProperties}.
     */
    private static void updateFavicon(ViewLookupCachingFrameLayout rootView, PropertyModel model) {
        TabListFaviconProvider.TabFavicon favicon = model.get(TabProperties.FAVICON);
        ImageView faviconView = (ImageView) rootView.fastFindViewById(R.id.tab_favicon);
        if (favicon == null) {
            faviconView.setImageDrawable(null);
            faviconView.setPadding(0, 0, 0, 0);
            return;
        }

        boolean isSelected = model.get(TabProperties.IS_SELECTED);
        faviconView.setImageDrawable(
                isSelected ? favicon.getSelectedDrawable() : favicon.getDefaultDrawable());
        int padding =
                (int) TabUiThemeProvider.getTabCardTopFaviconPadding(faviconView.getContext());
        faviconView.setPadding(padding, padding, padding, padding);
    }

    private static void updateColor(
            ViewLookupCachingFrameLayout rootView, boolean isIncognito, boolean isSelected) {
        View cardView = rootView.fastFindViewById(R.id.card_view);
        View dividerView = rootView.fastFindViewById(R.id.divider_view);
        TextView titleView = (TextView) rootView.fastFindViewById(R.id.tab_title);
        TabGridThumbnailView thumbnail =
                (TabGridThumbnailView) rootView.fastFindViewById(R.id.tab_thumbnail);
        ChromeImageView backgroundView =
                (ChromeImageView) rootView.fastFindViewById(R.id.background_view);

        cardView.getBackground().mutate();
        final @ColorInt int backgroundColor = TabUiThemeProvider.getCardViewBackgroundColor(
                cardView.getContext(), isIncognito, isSelected);
        ViewCompat.setBackgroundTintList(cardView, ColorStateList.valueOf(backgroundColor));

        dividerView.setBackgroundColor(
                TabUiThemeProvider.getDividerColor(dividerView.getContext(), isIncognito));

        titleView.setTextColor(TabUiThemeProvider.getTitleTextColor(
                titleView.getContext(), isIncognito, isSelected));

        if (thumbnail.isPlaceHolder()) {
            thumbnail.setColorThumbnailPlaceHolder(isIncognito, isSelected);
        }

        if (TabUiFeatureUtilities.isTabGroupsAndroidEnabled(rootView.getContext())) {
            ViewCompat.setBackgroundTintList(backgroundView,
                    TabUiThemeProvider.getHoveredCardBackgroundTintList(
                            backgroundView.getContext(), isIncognito, isSelected));
        }
    }

    private static void updateColorForActionButton(
            ViewLookupCachingFrameLayout rootView, boolean isIncognito, boolean isSelected) {
        ImageView actionButton = (ImageView) rootView.fastFindViewById(R.id.action_button);
        ApiCompatibilityUtils.setImageTintList(actionButton,
                TabUiThemeProvider.getActionButtonTintList(
                        actionButton.getContext(), isIncognito, isSelected));
    }

    private static void updateColorForSelectionToggleButton(
            ViewLookupCachingFrameLayout rootView, boolean isIncognito, boolean isSelected) {
        final int defaultLevel =
                rootView.getResources().getInteger(R.integer.list_item_level_default);
        final int selectedLevel =
                rootView.getResources().getInteger(R.integer.list_item_level_selected);

        ImageView actionButton = (ImageView) rootView.fastFindViewById(R.id.action_button);
        actionButton.getBackground().setLevel(isSelected ? selectedLevel : defaultLevel);
        DrawableCompat.setTintList(actionButton.getBackground().mutate(),
                TabUiThemeProvider.getToggleActionButtonBackgroundTintList(
                        rootView.getContext(), isIncognito, isSelected));

        // The check should be invisible if not selected.
        actionButton.getDrawable().setAlpha(isSelected ? 255 : 0);
        ApiCompatibilityUtils.setImageTintList(actionButton,
                isSelected ? TabUiThemeProvider.getToggleActionButtonCheckedDrawableTintList(
                        rootView.getContext(), isIncognito)
                           : null);

        if (isSelected) {
            ((AnimatedVectorDrawableCompat) actionButton.getDrawable()).start();
        }
    }

    @VisibleForTesting
    static void setThumbnailFeatureForTesting(TabListMediator.ThumbnailFetcher fetcher) {
        sThumbnailFetcherForTesting = fetcher;
    }
}
