// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_CSS_RESOLVER_STYLE_CASCADE_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_CSS_RESOLVER_STYLE_CASCADE_H_

#include "third_party/blink/renderer/core/animation/interpolation.h"
#include "third_party/blink/renderer/core/core_export.h"
#include "third_party/blink/renderer/core/css/css_property_name.h"
#include "third_party/blink/renderer/core/css/css_property_value.h"
#include "third_party/blink/renderer/core/css/parser/css_parser_token.h"
#include "third_party/blink/renderer/core/css/parser/css_parser_token_range.h"
#include "third_party/blink/renderer/core/css/properties/css_bitset.h"
#include "third_party/blink/renderer/core/css/properties/css_property.h"
#include "third_party/blink/renderer/core/css/resolver/cascade_filter.h"
#include "third_party/blink/renderer/core/css/resolver/cascade_interpolations.h"
#include "third_party/blink/renderer/core/css/resolver/cascade_map.h"
#include "third_party/blink/renderer/core/css/resolver/cascade_origin.h"
#include "third_party/blink/renderer/core/css/resolver/cascade_priority.h"
#include "third_party/blink/renderer/core/css/resolver/match_result.h"
#include "third_party/blink/renderer/core/frame/web_feature_forward.h"
#include "third_party/blink/renderer/platform/heap/collection_support/heap_hash_map.h"
#include "third_party/blink/renderer/platform/heap/member.h"
#include "third_party/blink/renderer/platform/wtf/allocator/allocator.h"
#include "third_party/blink/renderer/platform/wtf/text/text_encoding.h"
#include "third_party/blink/renderer/platform/wtf/vector.h"

namespace blink {

class CascadeInterpolations;
class CascadeResolver;
class CSSCustomPropertyDeclaration;
class CSSParserContext;
class CSSProperty;
class CSSValue;
class CSSVariableData;
class CSSVariableReferenceValue;
class CustomProperty;
class MatchResult;
class StyleResolverState;

namespace cssvalue {

class CSSPendingSubstitutionValue;

}  // namespace cssvalue

// StyleCascade analyzes declarations provided by CSS rules and animations,
// and figures out which declarations should be skipped, and which should be
// applied (and in which order).
//
// Usage:
//
//   StyleCascade cascade(state);
//   cascade.MutableMatchResult().AddMatchedProperties(...matched rule...);
//   cascade.MutableMatchResult().AddMatchedProperties(...another rule...);
//   cascade.AddInterpolation(...); // Optional
//   cascade.Apply();
//
// [1] https://drafts.csswg.org/css-cascade/#cascade
class CORE_EXPORT StyleCascade {
  STACK_ALLOCATED();

  using CSSPendingSubstitutionValue = cssvalue::CSSPendingSubstitutionValue;

 public:
  StyleCascade(StyleResolverState& state) : state_(state) {}
  StyleCascade(const StyleCascade&) = delete;
  StyleCascade& operator=(const StyleCascade&) = delete;

  const MatchResult& GetMatchResult() { return match_result_; }

  // Access the MatchResult in order to add declarations to it.
  // The modifications made will be taken into account during Apply().
  //
  // It is invalid to modify the MatchResult after Apply has been called
  // (unless Reset is called first).
  //
  // TODO(andruud): ElementRuleCollector could emit MatchedProperties
  // directly to the cascade.
  MatchResult& MutableMatchResult();

  // Add ActiveInterpolationsMap to the cascade. The interpolations present
  // in the map will be taken into account during the next call to Apply.
  //
  // It is valid to add interpolations to the StyleCascade even after Apply
  // has been called.
  //
  // Note that it's assumed that the incoming ActiveInterpolationsMap outlives
  // the StyleCascade object.
  void AddInterpolations(const ActiveInterpolationsMap*, CascadeOrigin);

  // Applies the current CSS declarations and animations to the
  // StyleResolverState.
  //
  // It is valid to call Apply multiple times (up to 15), and each call may
  // provide a different filter.
  void Apply(CascadeFilter = CascadeFilter());

  // Returns a CSSBitset containing the !important declarations (analyzing
  // if needed). If there are no !important declarations, returns nullptr.
  //
  // Note that this function does not return any set bits for -internal-visited-
  // properties. Instead, !important -internal-visited-* declarations cause
  // the corresponding unvisited properties to be set in the return value.
  std::unique_ptr<CSSBitset> GetImportantSet();

  bool InlineStyleLost() const { return map_.InlineStyleLost(); }

  // Resets the cascade to its initial state. Note that this does not undo
  // any changes already applied to the StyleResolverState/ComputedStyle.
  void Reset();

  // Applying interpolations may involve resolving values, since we may be
  // applying a keyframe from e.g. "color: var(--x)" to "color: var(--y)".
  // Hence that code needs an entry point to the resolving process.
  //
  // This function handles IACVT [1] as follows:
  //
  //  - If a cycle was detected, returns nullptr.
  //  - If IACVT for other reasons, returns a 'CSSUnsetValue'.
  //
  // TODO(crbug.com/985023): This function has an associated const
  // violation, which isn't great. (This vilation was not introduced with
  // StyleCascade, however).
  //
  // See documentation the other Resolve* functions for what resolve means.
  //
  // [1] https://drafts.csswg.org/css-variables/#invalid-at-computed-value-time
  const CSSValue* Resolve(const CSSPropertyName&,
                          const CSSValue&,
                          CascadeOrigin,
                          CascadeResolver&);

  // Returns the cascaded values [1].
  //
  // This is intended for use by the Inspector Agent.
  //
  // Calling this requires a call to Apply to have taken place first. This is
  // because some of the cascaded values depend on computed value of other
  // properties (see ApplyCascadeAffecting).
  //
  // Note that this function currently returns cascaded values from
  // CascadeOrigin::kUserAgent, kUser and kAuthor only.
  //
  // [1] https://drafts.csswg.org/css-cascade/#cascaded
  HeapHashMap<CSSPropertyName, Member<const CSSValue>> GetCascadedValues()
      const;

  // The maximum number of tokens that may be produced by a var()
  // reference.
  //
  // https://drafts.csswg.org/css-variables/#long-variables
  static const size_t kMaxSubstitutionTokens = 65536;

 private:
  friend class TestCascade;

  // Before we can Apply the cascade, the MatchResult and CascadeInterpolations
  // must be Analyzed. This means going through all the declarations, and
  // adding them to the CascadeMap, which gives us a complete picture of which
  // declarations won the cascade.
  //
  // We analyze only if needed (i.e. if MatchResult or CascadeInterpolations)
  // has been mutated since the last call to AnalyzeIfNeeded.
  void AnalyzeIfNeeded();
  void AnalyzeMatchResult();
  void AnalyzeInterpolations();

  // Clears the CascadeMap and other state, and analyzes the MatchResult/
  // interpolations again.
  void Reanalyze();

  // Some properties are "cascade affecting", in the sense that their computed
  // value actually affects cascade behavior. For example, css-logical
  // properties change their cascade behavior depending on the computed value
  // of direction/writing-mode.
  void ApplyCascadeAffecting(CascadeResolver&);

  // Applies kHighPropertyPriority properties.
  //
  // In theory, it would be possible for each property/value that contains
  // em/ch/etc to dynamically apply font-size (and related properties), but
  // in practice, it is very inconvenient to detect these dependencies. Hence,
  // we apply font-affecting properties (among others) before all the others.
  void ApplyHighPriority(CascadeResolver&);

  // Applies -webkit-appearance, and excludes -internal-ua-* properties if
  // we don't have an appearance.
  void ApplyAppearance(CascadeResolver&);

  // Some legacy properties are "overlapping", in that they share parts of
  // a computed value with other properties.
  //
  // * -webkit-border-image (longhand) overlaps with border-image (shorthand).
  // * -webkit-perspective-origin-x/y overlaps with perspective-origin.
  // * -webkit-transform-origin-x/y/z overlaps with transform-origin.
  //
  // This overlap breaks the general rule that properties can be applied in
  // any order (they need to be applied in the order they are declared).
  //
  // This function applies the "widest" of those overlapping properties
  // (that is, properties which represent an entire computed-value),
  // and conditionally marks narrow ones with a lower priority as already done,
  // so that later apply steps do not apply them (ie., effectively causes them
  // to be skipped).
  void ApplyWideOverlapping(CascadeResolver&);

  void ApplyMatchResult(CascadeResolver&);
  void ApplyInterpolations(CascadeResolver&);
  void ApplyInterpolationMap(const ActiveInterpolationsMap&,
                             CascadeOrigin,
                             size_t index,
                             CascadeResolver&);
  void ApplyInterpolation(const CSSProperty&,
                          CascadePriority,
                          const ActiveInterpolations&,
                          CascadeResolver&);

  // Looks up a value with random access, and applies it.
  void LookupAndApply(const CSSPropertyName&, CascadeResolver&);
  void LookupAndApply(const CSSProperty&, CascadeResolver&);
  void LookupAndApplyValue(const CSSProperty&,
                           CascadePriority*,
                           CascadeResolver&);
  void LookupAndApplyDeclaration(const CSSProperty&,
                                 CascadePriority*,
                                 CascadeResolver&);
  void LookupAndApplyInterpolation(const CSSProperty&,
                                   CascadePriority*,
                                   CascadeResolver&);

  // Whether or not we are calculating the style for the root element.
  // We need to know this to detect cycles with 'rem' units.
  // https://drafts.css-houdini.org/css-properties-values-api-1/#dependency-cycles
  bool IsRootElement() const;

  // The TokenSequence class acts as a builder for CSSVariableData.
  //
  // However, actually building a CSSVariableData is optional; you can also
  // get a CSSParserTokenRange directly, which is useful when resolving a
  // CSSVariableData which won't ultimately end up in a CSSVariableData
  // (i.e. CSSVariableReferenceValue or CSSPendingSubstitutionValue).
  class TokenSequence {
    STACK_ALLOCATED();

   public:
    TokenSequence() = default;
    // Initialize a TokenSequence from a CSSVariableData, preparing the
    // TokenSequence for var() resolution.
    //
    // This copies everything except the tokens.
    explicit TokenSequence(const CSSVariableData*);

    bool IsAnimationTainted() const { return is_animation_tainted_; }
    CSSParserTokenRange TokenRange() const { return tokens_; }

    bool Append(const TokenSequence&, wtf_size_t);
    bool Append(CSSVariableData* data,
                wtf_size_t limit = std::numeric_limits<wtf_size_t>::max());
    void Append(const CSSParserToken&);

    scoped_refptr<CSSVariableData> BuildVariableData();

   private:
    bool AppendTokens(const Vector<CSSParserToken>&, wtf_size_t);

    Vector<CSSParserToken> tokens_;
    Vector<scoped_refptr<const CSSVariableData>> variable_data_;
    // https://drafts.csswg.org/css-variables/#animation-tainted
    bool is_animation_tainted_ = false;
    // https://drafts.css-houdini.org/css-properties-values-api-1/#dependency-cycles
    bool has_font_units_ = false;
    bool has_root_font_units_ = false;

    // The base URL and charset are currently needed to calculate the computed
    // value of <url>-registered custom properties correctly.
    //
    // TODO(crbug.com/985013): Store CSSParserContext on
    // CSSCustomPropertyDeclaration and avoid this.
    //
    // https://drafts.css-houdini.org/css-properties-values-api-1/#relative-urls
    String base_url_;
    WTF::TextEncoding charset_;
  };

  // Resolving Values
  //
  // *Resolving* a value, means looking at the dependencies for a given
  // CSSValue, and ensuring that these dependencies are satisfied. The result
  // of a Resolve call is either the same CSSValue (e.g. if there were no
  // dependencies), or a new CSSValue with the dependencies resolved.
  //
  // For example, consider the following properties:
  //
  //  --x: 10px;
  //  --y: var(--x);
  //  width: var(--y);
  //
  // Here, to resolve 'width', the computed value of --y must be known. In
  // other words, we must first Apply '--y'. Hence, resolving 'width' will
  // Apply '--y' as a side-effect. (This process would then continue to '--x').

  const CSSValue* Resolve(const CSSProperty&,
                          const CSSValue&,
                          CascadePriority,
                          CascadeOrigin&,
                          CascadeResolver&);
  const CSSValue* ResolveCustomProperty(const CSSProperty&,
                                        const CSSCustomPropertyDeclaration&,
                                        CascadeResolver&);
  const CSSValue* ResolveVariableReference(const CSSProperty&,
                                           const CSSVariableReferenceValue&,
                                           CascadeResolver&);
  const CSSValue* ResolvePendingSubstitution(const CSSProperty&,
                                             const CSSPendingSubstitutionValue&,
                                             CascadeResolver&);
  const CSSValue* ResolveRevert(const CSSProperty&,
                                const CSSValue&,
                                CascadeOrigin&,
                                CascadeResolver&);
  const CSSValue* ResolveRevertLayer(const CSSProperty&,
                                     const CSSValue&,
                                     CascadePriority,
                                     CascadeOrigin&,
                                     CascadeResolver&);

  scoped_refptr<CSSVariableData> ResolveVariableData(CSSVariableData*,
                                                     CascadeResolver&);

  // The Resolve*Into functions either resolve dependencies, append to the
  // TokenSequence accordingly, and return true; or it returns false when
  // the TokenSequence is "invalid at computed-value time" [1]. This happens
  // when there was a reference to an invalid/missing custom property, or when a
  // cycle was detected.
  //
  // [1] https://drafts.csswg.org/css-variables/#invalid-at-computed-value-time

  bool ResolveTokensInto(CSSParserTokenRange, CascadeResolver&, TokenSequence&);
  bool ResolveVarInto(CSSParserTokenRange, CascadeResolver&, TokenSequence&);
  bool ResolveEnvInto(CSSParserTokenRange, CascadeResolver&, TokenSequence&);

  CSSVariableData* GetVariableData(const CustomProperty&) const;
  CSSVariableData* GetEnvironmentVariable(const AtomicString&,
                                          WTF::Vector<unsigned>) const;
  const CSSParserContext* GetParserContext(const CSSVariableReferenceValue&);

  // Detects if the given property/data depends on the font-size property
  // of the Element we're calculating the style for.
  //
  // https://drafts.css-houdini.org/css-properties-values-api-1/#dependency-cycles
  bool HasFontSizeDependency(const CustomProperty&, CSSVariableData*) const;
  // The fallback must match the syntax of the custom property, otherwise the
  // the declaration is "invalid at computed-value time".'
  //
  // https://drafts.css-houdini.org/css-properties-values-api-1/#fallbacks-in-var-references
  bool ValidateFallback(const CustomProperty&, CSSParserTokenRange) const;
  // Marks the CustomProperty as referenced by something. Needed to avoid
  // animating these custom properties on the compositor.
  void MarkIsReferenced(const CSSProperty& referencer,
                        const CustomProperty& referenced);
  // Marks a CSSProperty as having a reference to a custom property. Needed to
  // disable the matched property cache in some cases.
  void MarkHasVariableReference(const CSSProperty&);

  const Document& GetDocument() const;
  const CSSProperty& ResolveSurrogate(const CSSProperty& surrogate);

  void CountUse(WebFeature);
  void MaybeUseCountRevert(const CSSValue&);
  void MaybeUseCountSummaryDisplayBlock();

  StyleResolverState& state_;
  MatchResult match_result_;
  CascadeInterpolations interpolations_;
  CascadeMap map_;
  // Generational Apply
  //
  // Generation is a number that's incremented by one for each call to Apply
  // (the first call to Apply has generation 1). When a declaration is applied
  // to ComputedStyle, the current Apply-generation is stored in the CascadeMap.
  // In other words, the CascadeMap knows which declarations have already been
  // applied to ComputedStyle, which makes it possible to avoid applying the
  // same declaration twice during a single call to Apply:
  //
  // For example:
  //
  //   --x: red;
  //   background-color: var(--x);
  //
  // During Apply (generation=1), we linearly traverse the declarations above,
  // and first apply '--x' to the ComputedStyle. Then, we proceed to
  // 'background-color', which must first have its dependencies resolved before
  // we can apply it. This is where we check the current generation stored for
  // '--x'. If it's equal to the generation associated with the Apply call, we
  // know that we already applied it. Either something else referenced it before
  // we did, or it appeared before us in the MatchResult. Either way, we don't
  // have to apply '--x' again.
  //
  // Had the order been reversed, such that the '--x' declaration appeared after
  // the 'background-color' declaration, we would discover (during resolution of
  // var(--x), that the current generation of '--x' is _less_ than the
  // generation associated with the Apply call, hence we need to LookupAndApply
  // '--x' before applying 'background-color'.
  //
  // A secondary benefit to the generational apply mechanic, is that it's
  // possible to efficiently apply the StyleCascade more than once (perhaps with
  // a different CascadeFilter for each call), without rebuilding it. By
  // incrementing generation_, the existing record of what has been applied is
  // immediately invalidated, and everything will be applied again.
  //
  // Note: The maximum generation number is currently 15. This is more than
  //       enough for our needs.
  uint8_t generation_ = 0;

  bool needs_match_result_analyze_ = false;
  bool needs_interpolations_analyze_ = false;
  // A cascade-affecting property is for example 'direction', since the
  // computed value of the property affects how e.g. margin-inline-start
  // (and other css-logical properties) cascade.
  bool depends_on_cascade_affecting_property_ = false;
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_CORE_CSS_RESOLVER_STYLE_CASCADE_H_
