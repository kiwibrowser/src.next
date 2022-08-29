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

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_CSS_CSS_DEFAULT_STYLE_SHEETS_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_CSS_CSS_DEFAULT_STYLE_SHEETS_H_

#include "third_party/blink/renderer/core/core_export.h"
#include "third_party/blink/renderer/core/style/computed_style_constants.h"
#include "third_party/blink/renderer/platform/heap/garbage_collected.h"
#include "third_party/blink/renderer/platform/heap/member.h"
#include "third_party/blink/renderer/platform/wtf/allocator/allocator.h"
#include "third_party/blink/renderer/platform/wtf/text/wtf_string.h"

namespace blink {

class Document;
class Element;
class MediaQueryEvaluator;
class RuleFeatureSet;
class RuleSet;
class StyleSheetContents;

class CSSDefaultStyleSheets final
    : public GarbageCollected<CSSDefaultStyleSheets> {
 public:
  CORE_EXPORT static CSSDefaultStyleSheets& Instance();

  // Performs any initialization that should be done on renderer startup.
  static void Init();

  static StyleSheetContents* ParseUASheet(const String&);
  static const MediaQueryEvaluator& ScreenEval();

  CSSDefaultStyleSheets();
  CSSDefaultStyleSheets(const CSSDefaultStyleSheets&) = delete;
  CSSDefaultStyleSheets& operator=(const CSSDefaultStyleSheets&) = delete;

  bool EnsureDefaultStyleSheetsForElement(const Element&);
  bool EnsureDefaultStyleSheetsForPseudoElement(PseudoId);
  bool EnsureDefaultStyleSheetForXrOverlay();
  void EnsureDefaultStyleSheetForFullscreen();
  bool EnsureDefaultStyleSheetForForcedColors();

  RuleSet* DefaultHtmlStyle() { return default_html_style_.Get(); }
  RuleSet* DefaultMathMLStyle() { return default_mathml_style_.Get(); }
  RuleSet* DefaultSVGStyle() { return default_svg_style_.Get(); }
  RuleSet* DefaultHtmlQuirksStyle() { return default_html_quirks_style_.Get(); }
  RuleSet* DefaultPrintStyle() { return default_print_style_.Get(); }
  RuleSet* DefaultViewSourceStyle();
  RuleSet* DefaultForcedColorStyle() {
    return default_forced_color_style_.Get();
  }
  RuleSet* DefaultPseudoElementStyleOrNull() {
    return default_pseudo_element_style_.Get();
  }
  RuleSet* DefaultMediaControlsStyle() {
    return default_media_controls_style_.Get();
  }

  StyleSheetContents* EnsureMobileViewportStyleSheet();
  StyleSheetContents* EnsureTelevisionViewportStyleSheet();
  StyleSheetContents* EnsureXHTMLMobileProfileStyleSheet();

  StyleSheetContents* DefaultStyleSheet() { return default_style_sheet_.Get(); }
  StyleSheetContents* QuirksStyleSheet() { return quirks_style_sheet_.Get(); }
  StyleSheetContents* PopupStyleSheet() { return popup_style_sheet_.Get(); }
  StyleSheetContents* SelectMenuStyleSheet() {
    return selectmenu_style_sheet_.Get();
  }
  StyleSheetContents* SvgStyleSheet() { return svg_style_sheet_.Get(); }
  StyleSheetContents* MathmlStyleSheet() { return mathml_style_sheet_.Get(); }
  StyleSheetContents* MediaControlsStyleSheet() {
    return media_controls_style_sheet_.Get();
  }
  StyleSheetContents* FullscreenStyleSheet() {
    return fullscreen_style_sheet_.Get();
  }
  StyleSheetContents* MarkerStyleSheet() { return marker_style_sheet_.Get(); }
  StyleSheetContents* ForcedColorsStyleSheet() {
    return forced_colors_style_sheet_.Get();
  }

  CORE_EXPORT void PrepareForLeakDetection();

  // Media Controls UA stylesheet loading is handled by the media_controls
  // module.
  class CORE_EXPORT UAStyleSheetLoader {
   public:
    UAStyleSheetLoader() = default;
    UAStyleSheetLoader(const UAStyleSheetLoader&) = delete;
    UAStyleSheetLoader& operator=(const UAStyleSheetLoader&) = delete;
    virtual ~UAStyleSheetLoader() = default;
    virtual String GetUAStyleSheet() = 0;
  };
  CORE_EXPORT void SetMediaControlsStyleSheetLoader(
      std::unique_ptr<UAStyleSheetLoader>);
  CORE_EXPORT bool HasMediaControlsStyleSheetLoader() {
    return media_controls_style_sheet_loader_.get();
  }

  void CollectFeaturesTo(const Document&, RuleFeatureSet&);

  void Trace(Visitor*) const;

 private:
  void InitializeDefaultStyles();

  enum class NamespaceType {
    kHTML,
    kMathML,
    kSVG,
    kMediaControls,  // Not exactly a namespace
  };
  void AddRulesToDefaultStyleSheets(StyleSheetContents* rules,
                                    NamespaceType type);

  Member<RuleSet> default_html_style_;
  Member<RuleSet> default_mathml_style_;
  Member<RuleSet> default_svg_style_;
  Member<RuleSet> default_html_quirks_style_;
  Member<RuleSet> default_print_style_;
  Member<RuleSet> default_view_source_style_;
  Member<RuleSet> default_forced_color_style_;
  Member<RuleSet> default_pseudo_element_style_;
  Member<RuleSet> default_media_controls_style_;

  Member<StyleSheetContents> default_style_sheet_;
  Member<StyleSheetContents> mobile_viewport_style_sheet_;
  Member<StyleSheetContents> television_viewport_style_sheet_;
  Member<StyleSheetContents> xhtml_mobile_profile_style_sheet_;
  Member<StyleSheetContents> quirks_style_sheet_;
  Member<StyleSheetContents> svg_style_sheet_;
  Member<StyleSheetContents> mathml_style_sheet_;
  Member<StyleSheetContents> media_controls_style_sheet_;
  Member<StyleSheetContents> text_track_style_sheet_;
  Member<StyleSheetContents> fullscreen_style_sheet_;
  Member<StyleSheetContents> popup_style_sheet_;
  Member<StyleSheetContents> selectmenu_style_sheet_;
  Member<StyleSheetContents> webxr_overlay_style_sheet_;
  Member<StyleSheetContents> marker_style_sheet_;
  Member<StyleSheetContents> forced_colors_style_sheet_;

  std::unique_ptr<UAStyleSheetLoader> media_controls_style_sheet_loader_;
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_CORE_CSS_CSS_DEFAULT_STYLE_SHEETS_H_
