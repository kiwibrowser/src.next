/*
 * Copyright (C) 1999 Lars Knoll (knoll@kde.org)
 * Copyright (C) 2003, 2004, 2005, 2006, 2007, 2008, 2009, 2010, 2011 Apple Inc.
 * All rights reserved.
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

#include "third_party/blink/renderer/core/css/resolver/style_resolver_state.h"

#include "third_party/blink/public/mojom/use_counter/metrics/web_feature.mojom-blink.h"
#include "third_party/blink/renderer/core/animation/css/css_animations.h"
#include "third_party/blink/renderer/core/css/css_light_dark_value_pair.h"
#include "third_party/blink/renderer/core/css/css_property_value_set.h"
#include "third_party/blink/renderer/core/dom/node.h"
#include "third_party/blink/renderer/core/dom/node_computed_style.h"
#include "third_party/blink/renderer/core/dom/pseudo_element.h"
#include "third_party/blink/renderer/core/style/computed_style.h"

namespace blink {

namespace {

bool CanCacheBaseStyle(const StyleRequest& style_request) {
  return style_request.IsPseudoStyleRequest() ||
         (!style_request.parent_override &&
          !style_request.layout_parent_override &&
          style_request.matching_behavior == kMatchAllRules);
}

}  // namespace

StyleResolverState::StyleResolverState(
    Document& document,
    Element& element,
    const StyleRecalcContext* style_recalc_context,
    const StyleRequest& style_request)
    : element_context_(element),
      document_(&document),
      parent_style_(style_request.parent_override),
      layout_parent_style_(style_request.layout_parent_override),
      pseudo_request_type_(style_request.type),
      font_builder_(&document),
      pseudo_element_(
          element.GetNestedPseudoElement(style_request.pseudo_id,
                                         style_request.pseudo_argument)),
      element_style_resources_(GetElement(),
                               document.DevicePixelRatio(),
                               pseudo_element_),
      element_type_(style_request.IsPseudoStyleRequest()
                        ? ElementType::kPseudoElement
                        : ElementType::kElement),
      container_unit_context_(style_recalc_context
                                  ? style_recalc_context->container
                                  : element.ParentOrShadowHostElement()),
      originating_element_style_(style_request.originating_element_style),
      is_for_highlight_(IsHighlightPseudoElement(style_request.pseudo_id)),
      uses_highlight_pseudo_inheritance_(
          ::blink::UsesHighlightPseudoInheritance(style_request.pseudo_id)),
      can_cache_base_style_(blink::CanCacheBaseStyle(style_request)) {
  DCHECK(!!parent_style_ == !!layout_parent_style_);

  if (UsesHighlightPseudoInheritance()) {
    DCHECK(originating_element_style_);
  } else {
    if (!parent_style_)
      parent_style_ = element_context_.ParentStyle();
    if (!layout_parent_style_)
      layout_parent_style_ = element_context_.LayoutParentStyle();
  }

  if (!layout_parent_style_)
    layout_parent_style_ = parent_style_;

  DCHECK(document.IsActive());
}

StyleResolverState::~StyleResolverState() {
  // For performance reasons, explicitly clear HeapVectors and
  // HeapHashMaps to avoid giving a pressure on Oilpan's GC.
  animation_update_.Clear();
}

bool StyleResolverState::IsInheritedForUnset(
    const CSSProperty& property) const {
  return property.IsInherited() || UsesHighlightPseudoInheritance();
}

void StyleResolverState::SetStyle(scoped_refptr<ComputedStyle> style) {
  // FIXME: Improve RAII of StyleResolverState to remove this function.
  style_ = std::move(style);
  UpdateLengthConversionData();
}

scoped_refptr<ComputedStyle> StyleResolverState::TakeStyle() {
  if (had_no_matched_properties_ &&
      pseudo_request_type_ == StyleRequest::kForRenderer) {
    return nullptr;
  }
  return std::move(style_);
}

void StyleResolverState::UpdateLengthConversionData() {
  css_to_length_conversion_data_ = CSSToLengthConversionData(
      Style(), RootElementStyle(), GetDocument().GetLayoutView(),
      CSSToLengthConversionData::ContainerSizes(container_unit_context_),
      Style()->EffectiveZoom());
  element_style_resources_.UpdateLengthConversionData(
      &css_to_length_conversion_data_);
}

CSSToLengthConversionData StyleResolverState::UnzoomedLengthConversionData(
    const ComputedStyle* font_style) const {
  float em = font_style->SpecifiedFontSize();
  float rem = RootElementStyle() ? RootElementStyle()->SpecifiedFontSize() : 1;
  CSSToLengthConversionData::FontSizes font_sizes(
      em, rem, &font_style->GetFont(), font_style->EffectiveZoom());
  CSSToLengthConversionData::ViewportSize viewport_size(
      GetDocument().GetLayoutView());
  CSSToLengthConversionData::ContainerSizes container_sizes(
      container_unit_context_);

  return CSSToLengthConversionData(Style(), Style()->GetWritingMode(),
                                   font_sizes, viewport_size, container_sizes,
                                   1);
}

CSSToLengthConversionData StyleResolverState::FontSizeConversionData() const {
  return UnzoomedLengthConversionData(ParentStyle());
}

CSSToLengthConversionData StyleResolverState::UnzoomedLengthConversionData()
    const {
  return UnzoomedLengthConversionData(Style());
}

void StyleResolverState::SetParentStyle(
    scoped_refptr<const ComputedStyle> parent_style) {
  parent_style_ = std::move(parent_style);
}

void StyleResolverState::SetLayoutParentStyle(
    scoped_refptr<const ComputedStyle> parent_style) {
  layout_parent_style_ = std::move(parent_style);
}

void StyleResolverState::LoadPendingResources() {
  if (pseudo_request_type_ == StyleRequest::kForComputedStyle ||
      (ParentStyle() && ParentStyle()->IsEnsuredInDisplayNone()) ||
      (StyleRef().Display() == EDisplay::kNone &&
       !GetElement().LayoutObjectIsNeeded(StyleRef())) ||
      StyleRef().IsEnsuredOutsideFlatTree()) {
    return;
  }

  if (StyleRef().StyleType() == kPseudoIdTargetText) {
    // Do not load any resources for ::target-text since that could leak text
    // content to external stylesheets.
    return;
  }

  element_style_resources_.LoadPendingResources(StyleRef());
}

const FontDescription& StyleResolverState::ParentFontDescription() const {
  return parent_style_->GetFontDescription();
}

void StyleResolverState::SetZoom(float f) {
  float parent_effective_zoom = ParentStyle()
                                    ? ParentStyle()->EffectiveZoom()
                                    : ComputedStyleInitialValues::InitialZoom();

  style_->SetZoom(f);

  if (f != 1.f)
    GetDocument().CountUse(WebFeature::kCascadedCSSZoomNotEqualToOne);

  if (style_->SetEffectiveZoom(parent_effective_zoom * f))
    font_builder_.DidChangeEffectiveZoom();
}

void StyleResolverState::SetEffectiveZoom(float f) {
  if (style_->SetEffectiveZoom(f))
    font_builder_.DidChangeEffectiveZoom();
}

void StyleResolverState::SetWritingMode(WritingMode new_writing_mode) {
  if (style_->GetWritingMode() == new_writing_mode) {
    return;
  }
  style_->SetWritingMode(new_writing_mode);
  UpdateLengthConversionData();
  font_builder_.DidChangeWritingMode();
}

void StyleResolverState::SetTextOrientation(ETextOrientation text_orientation) {
  if (style_->GetTextOrientation() != text_orientation) {
    style_->SetTextOrientation(text_orientation);
    font_builder_.DidChangeTextOrientation();
  }
}

CSSParserMode StyleResolverState::GetParserMode() const {
  return GetDocument().InQuirksMode() ? kHTMLQuirksMode : kHTMLStandardMode;
}

Element* StyleResolverState::GetAnimatingElement() const {
  if (element_type_ == ElementType::kElement)
    return &GetElement();
  DCHECK_EQ(ElementType::kPseudoElement, element_type_);
  return pseudo_element_;
}

PseudoElement* StyleResolverState::GetPseudoElement() const {
  return element_type_ == ElementType::kPseudoElement ? pseudo_element_
                                                      : nullptr;
}

const CSSValue& StyleResolverState::ResolveLightDarkPair(
    const CSSValue& value) {
  if (const auto* pair = DynamicTo<CSSLightDarkValuePair>(value)) {
    if (Style()->UsedColorScheme() == mojom::blink::ColorScheme::kLight)
      return pair->First();
    return pair->Second();
  }
  return value;
}

void StyleResolverState::UpdateFont() {
  GetFontBuilder().CreateFont(StyleRef(), ParentStyle());
  SetConversionFontSizes(
      CSSToLengthConversionData::FontSizes(Style(), RootElementStyle()));
  SetConversionZoom(Style()->EffectiveZoom());
}

}  // namespace blink
