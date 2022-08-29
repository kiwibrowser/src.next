/*
 * Copyright (C) 1999 Lars Knoll (knoll@kde.org)
 * Copyright (C) 2003, 2004, 2005, 2006, 2007, 2008, 2009, 2010, 2011 Apple Inc.
 * All rights reserved.
 * Copyright (C) 2013 Google Inc. All rights reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 */

#include "third_party/blink/renderer/core/css/resolver/element_style_resources.h"

#include "third_party/blink/renderer/core/css/css_crossfade_value.h"
#include "third_party/blink/renderer/core/css/css_gradient_value.h"
#include "third_party/blink/renderer/core/css/css_image_set_value.h"
#include "third_party/blink/renderer/core/css/css_image_value.h"
#include "third_party/blink/renderer/core/css/css_paint_value.h"
#include "third_party/blink/renderer/core/css/css_property_names.h"
#include "third_party/blink/renderer/core/css/css_to_length_conversion_data.h"
#include "third_party/blink/renderer/core/css/css_uri_value.h"
#include "third_party/blink/renderer/core/dom/document.h"
#include "third_party/blink/renderer/core/dom/element.h"
#include "third_party/blink/renderer/core/dom/pseudo_element.h"
#include "third_party/blink/renderer/core/dom/tree_scope.h"
#include "third_party/blink/renderer/core/frame/local_frame.h"
#include "third_party/blink/renderer/core/frame/settings.h"
#include "third_party/blink/renderer/core/loader/lazy_image_helper.h"
#include "third_party/blink/renderer/core/style/computed_style.h"
#include "third_party/blink/renderer/core/style/content_data.h"
#include "third_party/blink/renderer/core/style/cursor_data.h"
#include "third_party/blink/renderer/core/style/fill_layer.h"
#include "third_party/blink/renderer/core/style/filter_operation.h"
#include "third_party/blink/renderer/core/style/style_crossfade_image.h"
#include "third_party/blink/renderer/core/style/style_fetched_image.h"
#include "third_party/blink/renderer/core/style/style_fetched_image_set.h"
#include "third_party/blink/renderer/core/style/style_generated_image.h"
#include "third_party/blink/renderer/core/style/style_image.h"
#include "third_party/blink/renderer/core/style/style_pending_image.h"
#include "third_party/blink/renderer/core/svg/svg_resource.h"
#include "third_party/blink/renderer/core/svg/svg_tree_scope_resources.h"
#include "third_party/blink/renderer/platform/geometry/length.h"
#include "third_party/blink/renderer/platform/loader/fetch/cross_origin_attribute_value.h"
#include "third_party/blink/renderer/platform/loader/fetch/fetch_parameters.h"
#include "third_party/blink/renderer/platform/loader/fetch/resource_fetcher.h"
#include "third_party/blink/renderer/platform/runtime_enabled_features.h"

namespace blink {

namespace {

bool IsUsingContainerRelativeUnits(const CSSValue& value) {
  const auto* image_generator_value = DynamicTo<CSSImageGeneratorValue>(value);
  return image_generator_value &&
         image_generator_value->IsUsingContainerRelativeUnits();
}

class StyleImageLoader {
  STACK_ALLOCATED();

 public:
  using ContainerSizes = CSSToLengthConversionData::ContainerSizes;

  StyleImageLoader(Document& document,
                   ComputedStyle& style,
                   const PreCachedContainerSizes& pre_cached_container_sizes,
                   float device_scale_factor)
      : document_(document),
        style_(style),
        pre_cached_container_sizes_(pre_cached_container_sizes),
        device_scale_factor_(device_scale_factor) {}

  StyleImage* Load(CSSValue&,
                   FetchParameters::ImageRequestBehavior =
                       FetchParameters::ImageRequestBehavior::kNone,
                   CrossOriginAttributeValue = kCrossOriginAttributeNotSet);

 private:
  StyleImage* CrossfadeArgument(CSSValue&, CrossOriginAttributeValue);

  Document& document_;
  ComputedStyle& style_;
  const PreCachedContainerSizes& pre_cached_container_sizes_;
  const float device_scale_factor_;
};

StyleImage* StyleImageLoader::Load(
    CSSValue& value,
    FetchParameters::ImageRequestBehavior image_request_behavior,
    CrossOriginAttributeValue cross_origin) {
  const ContainerSizes& container_sizes =
      IsUsingContainerRelativeUnits(value) ? pre_cached_container_sizes_.Get()
                                           : ContainerSizes();

  if (auto* image_value = DynamicTo<CSSImageValue>(value)) {
    return image_value->CacheImage(document_, image_request_behavior,
                                   cross_origin);
  }

  if (auto* paint_value = DynamicTo<CSSPaintValue>(value)) {
    auto* image = MakeGarbageCollected<StyleGeneratedImage>(*paint_value,
                                                            container_sizes);
    style_.AddPaintImage(image);
    return image;
  }

  if (auto* crossfade_value = DynamicTo<cssvalue::CSSCrossfadeValue>(value)) {
    return MakeGarbageCollected<StyleCrossfadeImage>(
        *crossfade_value,
        CrossfadeArgument(crossfade_value->From(), cross_origin),
        CrossfadeArgument(crossfade_value->To(), cross_origin));
  }

  if (auto* image_gradient_value =
          DynamicTo<cssvalue::CSSGradientValue>(value)) {
    return MakeGarbageCollected<StyleGeneratedImage>(*image_gradient_value,
                                                     container_sizes);
  }

  if (auto* image_set_value = DynamicTo<CSSImageSetValue>(value)) {
    return image_set_value->CacheImage(document_, device_scale_factor_,
                                       image_request_behavior, cross_origin);
  }

  NOTREACHED();
  return nullptr;
}

StyleImage* StyleImageLoader::CrossfadeArgument(
    CSSValue& value,
    CrossOriginAttributeValue cross_origin) {
  // TODO(crbug.com/614906): For some reason we allow 'none' as an argument to
  // -webkit-cross-fade() - the unprefixed cross-fade() function does however
  // not accept 'none'. Map 'none' to a null StyleImage.
  if (auto* identifier_value = DynamicTo<CSSIdentifierValue>(value)) {
    DCHECK_EQ(identifier_value->GetValueID(), CSSValueID::kNone);
    return nullptr;
  }
  // Reject paint() functions. They make assumptions about the client (being
  // a LayoutObject) that we can't meet with the current implementation.
  if (IsA<CSSPaintValue>(value))
    return nullptr;
  return Load(value, FetchParameters::ImageRequestBehavior::kNone,
              cross_origin);
}

}  // namespace

const PreCachedContainerSizes::ContainerSizes& PreCachedContainerSizes::Get()
    const {
  DCHECK(RuntimeEnabledFeatures::CSSContainerRelativeUnitsEnabled());
  if (!cache_) {
    if (conversion_data_) {
      cache_ = conversion_data_->PreCachedContainerSizesCopy();
    } else {
      cache_ = ContainerSizes();
    }
  }
  return *cache_;
}

ElementStyleResources::ElementStyleResources(Element& element,
                                             float device_scale_factor,
                                             PseudoElement* pseudo_element)
    : element_(element),
      device_scale_factor_(device_scale_factor),
      pseudo_element_(pseudo_element) {}

bool ElementStyleResources::IsPending(const CSSValue& value) const {
  if (auto* img_value = DynamicTo<CSSImageValue>(value))
    return img_value->IsCachePending();

  // paint(...) is always treated as pending because it needs to call
  // AddPaintImage() on the ComputedStyle.
  if (IsA<CSSPaintValue>(value))
    return true;

  // cross-fade(...) is always treated as pending (to avoid adding more complex
  // recursion).
  if (IsA<cssvalue::CSSCrossfadeValue>(value))
    return true;

  // Gradient functions are never pending.
  if (IsA<cssvalue::CSSGradientValue>(value))
    return false;

  if (auto* img_set_value = DynamicTo<CSSImageSetValue>(value))
    return img_set_value->IsCachePending(device_scale_factor_);

  NOTREACHED();
  return false;
}

StyleImage* ElementStyleResources::CachedStyleImage(
    const CSSValue& value) const {
  DCHECK(!IsPending(value));
  if (auto* img_value = DynamicTo<CSSImageValue>(value)) {
    img_value->RestoreCachedResourceIfNeeded(element_.GetDocument());
    return img_value->CachedImage();
  }

  // Gradient functions are never pending (but don't cache StyleImages).
  if (auto* gradient_value = DynamicTo<cssvalue::CSSGradientValue>(value)) {
    using ContainerSizes = CSSToLengthConversionData::ContainerSizes;
    const ContainerSizes& container_sizes =
        IsUsingContainerRelativeUnits(value) ? pre_cached_container_sizes_.Get()
                                             : ContainerSizes();
    return MakeGarbageCollected<StyleGeneratedImage>(*gradient_value,
                                                     container_sizes);
  }

  if (auto* img_set_value = DynamicTo<CSSImageSetValue>(value))
    return img_set_value->CachedImage(device_scale_factor_);

  NOTREACHED();
  return nullptr;
}

StyleImage* ElementStyleResources::GetStyleImage(CSSPropertyID property,
                                                 const CSSValue& value) {
  if (auto* identifier_value = DynamicTo<CSSIdentifierValue>(value)) {
    DCHECK_EQ(identifier_value->GetValueID(), CSSValueID::kNone);
    return nullptr;
  }
  if (IsPending(value)) {
    pending_image_properties_.insert(property);
    return MakeGarbageCollected<StylePendingImage>(value);
  }
  return CachedStyleImage(value);
}

static bool AllowExternalResources(CSSPropertyID property) {
  return property == CSSPropertyID::kBackdropFilter ||
         property == CSSPropertyID::kFilter;
}

SVGResource* ElementStyleResources::GetSVGResourceFromValue(
    CSSPropertyID property,
    const cssvalue::CSSURIValue& value) {
  if (value.IsLocal(element_.GetDocument())) {
    SVGTreeScopeResources& tree_scope_resources =
        element_.OriginatingTreeScope().EnsureSVGTreeScopedResources();
    AtomicString decoded_fragment(DecodeURLEscapeSequences(
        value.FragmentIdentifier(), DecodeURLMode::kUTF8OrIsomorphic));
    return tree_scope_resources.ResourceForId(decoded_fragment);
  }
  if (AllowExternalResources(property)) {
    pending_svg_resource_properties_.insert(property);
    return value.EnsureResourceReference();
  }
  return nullptr;
}

static void LoadResourcesForFilter(
    FilterOperations::FilterOperationVector& filter_operations,
    Document& document) {
  for (const auto& filter_operation : filter_operations) {
    auto* reference_operation =
        DynamicTo<ReferenceFilterOperation>(filter_operation.Get());
    if (!reference_operation)
      continue;
    if (SVGResource* resource = reference_operation->Resource())
      resource->Load(document);
  }
}

void ElementStyleResources::LoadPendingSVGResources(ComputedStyle& style) {
  Document& document = element_.GetDocument();
  for (CSSPropertyID property : pending_svg_resource_properties_) {
    switch (property) {
      case CSSPropertyID::kBackdropFilter:
        LoadResourcesForFilter(style.MutableBackdropFilter().Operations(),
                               document);
        break;
      case CSSPropertyID::kFilter:
        LoadResourcesForFilter(style.MutableFilter().Operations(), document);
        break;
      default:
        NOTREACHED();
    }
  }
}

static CSSValue* PendingCssValue(StyleImage* style_image) {
  if (auto* pending_image = DynamicTo<StylePendingImage>(style_image))
    return pending_image->CssValue();
  return nullptr;
}

void ElementStyleResources::LoadPendingImages(ComputedStyle& style) {
  // We must loop over the properties and then look at the style to see if
  // a pending image exists, and only load that image. For example:
  //
  // <style>
  //    div { background-image: url(a.png); }
  //    div { background-image: url(b.png); }
  //    div { background-image: none; }
  // </style>
  // <div></div>
  //
  // We call styleImage() for both a.png and b.png adding the
  // CSSPropertyID::kBackgroundImage property to the pending_image_properties_
  // set, then we null out the background image because of the "none".
  //
  // If we eagerly loaded the images we'd fetch a.png, even though it's not
  // used. If we didn't null check below we'd crash since the none actually
  // removed all background images.
  StyleImageLoader loader(element_.GetDocument(), style,
                          pre_cached_container_sizes_, device_scale_factor_);
  for (CSSPropertyID property : pending_image_properties_) {
    switch (property) {
      case CSSPropertyID::kBackgroundImage: {
        for (FillLayer* background_layer = &style.AccessBackgroundLayers();
             background_layer; background_layer = background_layer->Next()) {
          if (auto* pending_value =
                  PendingCssValue(background_layer->GetImage())) {
            FetchParameters::ImageRequestBehavior image_request_behavior =
                FetchParameters::ImageRequestBehavior::kNone;
            StyleImage* new_image =
                loader.Load(*pending_value, image_request_behavior);
            if (new_image && new_image->IsLazyloadPossiblyDeferred()) {
              LazyImageHelper::StartMonitoring(pseudo_element_ ? pseudo_element_
                                                               : &element_);
            }
            background_layer->SetImage(new_image);
          }
        }
        break;
      }
      case CSSPropertyID::kContent: {
        for (ContentData* content_data =
                 const_cast<ContentData*>(style.GetContentData());
             content_data; content_data = content_data->Next()) {
          if (auto* image_content =
                  DynamicTo<ImageContentData>(*content_data)) {
            if (auto* pending_value =
                    PendingCssValue(image_content->GetImage())) {
              image_content->SetImage(loader.Load(*pending_value));
            }
          }
        }
        break;
      }
      case CSSPropertyID::kCursor: {
        if (CursorList* cursor_list = style.Cursors()) {
          for (CursorData& cursor : *cursor_list) {
            if (auto* pending_value = PendingCssValue(cursor.GetImage()))
              cursor.SetImage(loader.Load(*pending_value));
          }
        }
        break;
      }
      case CSSPropertyID::kListStyleImage: {
        if (auto* pending_value = PendingCssValue(style.ListStyleImage()))
          style.SetListStyleImage(loader.Load(*pending_value));
        break;
      }
      case CSSPropertyID::kBorderImageSource: {
        if (auto* pending_value = PendingCssValue(style.BorderImageSource()))
          style.SetBorderImageSource(loader.Load(*pending_value));
        break;
      }
      case CSSPropertyID::kWebkitBoxReflect: {
        if (StyleReflection* reflection = style.BoxReflect()) {
          const NinePieceImage& mask_image = reflection->Mask();
          if (auto* pending_value = PendingCssValue(mask_image.GetImage())) {
            StyleImage* loaded_image = loader.Load(*pending_value);
            reflection->SetMask(NinePieceImage(
                loaded_image, mask_image.ImageSlices(), mask_image.Fill(),
                mask_image.BorderSlices(), mask_image.Outset(),
                mask_image.HorizontalRule(), mask_image.VerticalRule()));
          }
        }
        break;
      }
      case CSSPropertyID::kWebkitMaskBoxImageSource: {
        if (auto* pending_value = PendingCssValue(style.MaskBoxImageSource()))
          style.SetMaskBoxImageSource(loader.Load(*pending_value));
        break;
      }
      case CSSPropertyID::kWebkitMaskImage: {
        for (FillLayer* mask_layer = &style.AccessMaskLayers(); mask_layer;
             mask_layer = mask_layer->Next()) {
          if (auto* pending_value = PendingCssValue(mask_layer->GetImage())) {
            mask_layer->SetImage(loader.Load(
                *pending_value, FetchParameters::ImageRequestBehavior::kNone,
                kCrossOriginAttributeAnonymous));
          }
        }
        break;
      }
      case CSSPropertyID::kShapeOutside:
        if (ShapeValue* shape_value = style.ShapeOutside()) {
          if (auto* pending_value = PendingCssValue(shape_value->GetImage())) {
            shape_value->SetImage(loader.Load(
                *pending_value, FetchParameters::ImageRequestBehavior::kNone,
                kCrossOriginAttributeAnonymous));
          }
        }
        break;
      default:
        NOTREACHED();
    }
  }
}

void ElementStyleResources::LoadPendingResources(
    ComputedStyle& computed_style) {
  LoadPendingImages(computed_style);
  LoadPendingSVGResources(computed_style);
}

void ElementStyleResources::UpdateLengthConversionData(
    const CSSToLengthConversionData* conversion_data) {
  pre_cached_container_sizes_ = PreCachedContainerSizes(conversion_data);
}

}  // namespace blink
