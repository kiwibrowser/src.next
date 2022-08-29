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

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_CSS_RESOLVER_FONT_BUILDER_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_CSS_RESOLVER_FONT_BUILDER_H_

#include "base/memory/scoped_refptr.h"
#include "third_party/blink/renderer/core/core_export.h"
#include "third_party/blink/renderer/core/css/font_size_functions.h"
#include "third_party/blink/renderer/core/css_value_keywords.h"
#include "third_party/blink/renderer/platform/fonts/font_description.h"
#include "third_party/blink/renderer/platform/fonts/font_palette.h"
#include "third_party/blink/renderer/platform/fonts/font_variant_numeric.h"
#include "third_party/blink/renderer/platform/wtf/allocator/allocator.h"

namespace blink {

class ComputedStyle;
class FontSelector;
class TreeScope;

class CORE_EXPORT FontBuilder {
  STACK_ALLOCATED();

 public:
  explicit FontBuilder(Document*);
  FontBuilder(const FontBuilder&) = delete;
  FontBuilder& operator=(const FontBuilder&) = delete;

  void SetInitial(float effective_zoom);

  void DidChangeEffectiveZoom();
  void DidChangeTextOrientation();
  void DidChangeWritingMode();

  FontFamily StandardFontFamily() const;
  AtomicString StandardFontFamilyName() const;
  AtomicString GenericFontFamilyName(FontDescription::GenericFamilyType) const;

  float FontSizeForKeyword(unsigned keyword, bool is_monospace) const;

  void SetSize(const FontDescription::Size&);
  void SetSizeAdjust(const float aspect_value);

  void SetStretch(FontSelectionValue);
  void SetStyle(FontSelectionValue);
  void SetWeight(FontSelectionValue);

  void SetFamilyDescription(const FontDescription::FamilyDescription&);
  // font-family is a tree-scoped reference.
  void SetFamilyTreeScope(const TreeScope*);
  void SetFeatureSettings(scoped_refptr<FontFeatureSettings>);
  void SetLocale(scoped_refptr<const LayoutLocale>);
  void SetVariantCaps(FontDescription::FontVariantCaps);
  void SetVariantEastAsian(const FontVariantEastAsian);
  void SetVariantLigatures(const FontDescription::VariantLigatures&);
  void SetVariantNumeric(const FontVariantNumeric&);
  void SetFontSynthesisWeight(FontDescription::FontSynthesisWeight);
  void SetFontSynthesisStyle(FontDescription::FontSynthesisStyle);
  void SetFontSynthesisSmallCaps(FontDescription::FontSynthesisSmallCaps);
  void SetTextRendering(TextRenderingMode);
  void SetKerning(FontDescription::Kerning);
  void SetFontPalette(scoped_refptr<FontPalette>);
  void SetFontOpticalSizing(OpticalSizing);
  void SetFontSmoothing(FontSmoothingMode);
  void SetVariationSettings(scoped_refptr<FontVariationSettings>);

  // FIXME: These need to just vend a Font object eventually.
  void UpdateFontDescription(FontDescription&,
                             FontOrientation = FontOrientation::kHorizontal);
  void CreateFont(ComputedStyle&, const ComputedStyle* parent_style);
  void CreateInitialFont(ComputedStyle&);

  bool FontDirty() const { return flags_; }

  static FontDescription::FamilyDescription InitialFamilyDescription() {
    return FontDescription::FamilyDescription(InitialGenericFamily());
  }
  static FontFeatureSettings* InitialFeatureSettings() { return nullptr; }
  static FontVariationSettings* InitialVariationSettings() { return nullptr; }
  static FontPalette* InitialFontPalette() { return nullptr; }
  static FontDescription::GenericFamilyType InitialGenericFamily() {
    return FontDescription::kStandardFamily;
  }
  static FontDescription::Size InitialSize() {
    return FontDescription::Size(FontSizeFunctions::InitialKeywordSize(), 0.0f,
                                 false);
  }
  static float InitialSizeAdjust() { return kFontSizeAdjustNone; }
  static TextRenderingMode InitialTextRendering() { return kAutoTextRendering; }
  static FontDescription::FontVariantCaps InitialVariantCaps() {
    return FontDescription::kCapsNormal;
  }
  static FontVariantEastAsian InitialVariantEastAsian() {
    return FontVariantEastAsian();
  }
  static FontDescription::VariantLigatures InitialVariantLigatures() {
    return FontDescription::VariantLigatures();
  }
  static FontVariantNumeric InitialVariantNumeric() {
    return FontVariantNumeric();
  }
  static LayoutLocale* InitialLocale() { return nullptr; }
  static FontDescription::Kerning InitialKerning() {
    return FontDescription::kAutoKerning;
  }
  static OpticalSizing InitialFontOpticalSizing() { return kAutoOpticalSizing; }
  static FontSmoothingMode InitialFontSmoothing() { return kAutoSmoothing; }

  static FontSelectionValue InitialStretch() { return NormalWidthValue(); }
  static FontSelectionValue InitialStyle() { return NormalSlopeValue(); }
  static FontSelectionValue InitialWeight() { return NormalWeightValue(); }
  static FontDescription::FontSynthesisWeight InitialFontSynthesisWeight() {
    return FontDescription::kAutoFontSynthesisWeight;
  }
  static FontDescription::FontSynthesisStyle InitialFontSynthesisStyle() {
    return FontDescription::kAutoFontSynthesisStyle;
  }
  static FontDescription::FontSynthesisSmallCaps
  InitialFontSynthesisSmallCaps() {
    return FontDescription::kAutoFontSynthesisSmallCaps;
  }

 private:
  void SetFamilyDescription(FontDescription&,
                            const FontDescription::FamilyDescription&);
  void SetSize(FontDescription&, const FontDescription::Size&);
  // This function fixes up the default font size if it detects that the current
  // generic font family has changed. -dwh
  void CheckForGenericFamilyChange(const FontDescription&, FontDescription&);
  void UpdateSpecifiedSize(FontDescription&,
                           const ComputedStyle&,
                           const ComputedStyle* parent_style);
  void UpdateComputedSize(FontDescription&, const ComputedStyle&);
  void UpdateAdjustedSize(FontDescription&,
                          const ComputedStyle&,
                          FontSelector*);

  float GetComputedSizeFromSpecifiedSize(FontDescription&,
                                         float effective_zoom,
                                         float specified_size);

  FontSelector* FontSelectorFromTreeScope(const TreeScope* tree_scope);
  FontSelector* ComputeFontSelector(const ComputedStyle& style);

  Document* document_{nullptr};
  const TreeScope* family_tree_scope_{nullptr};
  FontDescription font_description_;

  enum class PropertySetFlag {
    kWeight,
    kSize,
    kStretch,
    kFamily,
    kFeatureSettings,
    kLocale,
    kStyle,
    kSizeAdjust,
    kVariantCaps,
    kVariantEastAsian,
    kVariantLigatures,
    kVariantNumeric,
    kVariationSettings,
    kTextRendering,
    kKerning,
    kFontOpticalSizing,
    kFontPalette,
    kFontSmoothing,
    kFontSynthesisWeight,
    kFontSynthesisStyle,
    kFontSynthesisSmallCaps,

    kEffectiveZoom,
    kTextOrientation,
    kWritingMode
  };

  void Set(PropertySetFlag flag) { flags_ |= (1 << unsigned(flag)); }
  bool IsSet(PropertySetFlag flag) const {
    return flags_ & (1 << unsigned(flag));
  }

  unsigned flags_{0};
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_CORE_CSS_RESOLVER_FONT_BUILDER_H_
