/*
 * Copyright (C) 1999 Lars Knoll (knoll@kde.org)
 *           (C) 2004-2005 Allan Sandfeld Jensen (kde@carewolf.com)
 * Copyright (C) 2006, 2007 Nicholas Shanks (webkit@nickshanks.com)
 * Copyright (C) 2005, 2006, 2007, 2008, 2009, 2010, 2011, 2012, 2013 Apple Inc.
 * All rights reserved.
 * Copyright (C) 2007 Alexey Proskuryakov <ap@webkit.org>
 * Copyright (C) 2007, 2008 Eric Seidel <eric@webkit.org>
 * Copyright (C) 2008, 2009 Torch Mobile Inc. All rights reserved.
 * (http://www.torchmobile.com/)
 * Copyright (c) 2011, Code Aurora Forum. All rights reserved.
 * Copyright (C) Research In Motion Limited 2011. All rights reserved.
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
 */

#include "third_party/blink/renderer/core/css/resolver/matched_properties_cache.h"

#include "third_party/blink/renderer/core/css/css_property_value_set.h"
#include "third_party/blink/renderer/core/css/properties/css_property_ref.h"
#include "third_party/blink/renderer/core/css/resolver/style_resolver_state.h"
#include "third_party/blink/renderer/core/style/computed_style.h"
#include "third_party/blink/renderer/platform/heap/visitor.h"
#include "third_party/blink/renderer/platform/wtf/text/string_hasher.h"

namespace blink {

static unsigned ComputeMatchedPropertiesHash(const MatchResult& result) {
  const MatchedPropertiesVector& vector = result.GetMatchedProperties();
  return StringHasher::HashMemory(vector.data(),
                                  sizeof(MatchedProperties) * vector.size());
}

void CachedMatchedProperties::Set(const ComputedStyle& style,
                                  const ComputedStyle& parent_style,
                                  const MatchedPropertiesVector& properties) {
  for (const auto& new_matched_properties : properties) {
    matched_properties.push_back(new_matched_properties.properties);
    matched_properties_types.push_back(new_matched_properties.types_);
  }

  // Note that we don't cache the original ComputedStyle instance. It may be
  // further modified.  The ComputedStyle in the cache is really just a holder
  // for the substructures and never used as-is.
  this->computed_style = ComputedStyle::Clone(style);
  this->parent_computed_style = ComputedStyle::Clone(parent_style);
}

void CachedMatchedProperties::Clear() {
  matched_properties.clear();
  matched_properties_types.clear();
  computed_style = nullptr;
  parent_computed_style = nullptr;
}

bool CachedMatchedProperties::DependenciesEqual(
    const StyleResolverState& state) {
  if (!state.ParentStyle())
    return false;
  if ((parent_computed_style->IsEnsuredInDisplayNone() ||
       computed_style->IsEnsuredOutsideFlatTree()) &&
      !state.ParentStyle()->IsEnsuredInDisplayNone() &&
      !state.Style()->IsEnsuredOutsideFlatTree()) {
    // If we cached a ComputedStyle in a display:none subtree, or outside the
    // flat tree,  we would not have triggered fetches for external resources
    // and have StylePendingImages in the ComputedStyle. Instead of having to
    // inspect the cached ComputedStyle for such resources, don't use a cached
    // ComputedStyle when it was cached in display:none but is now rendered.
    return false;
  }

  if (parent_computed_style->GetWritingMode() !=
      state.ParentStyle()->GetWritingMode()) {
    return false;
  }
  if (parent_computed_style->Direction() != state.ParentStyle()->Direction())
    return false;
  if (parent_computed_style->UsedColorScheme() !=
      state.ParentStyle()->UsedColorScheme()) {
    return false;
  }
  if (computed_style->HasVariableReferenceFromNonInheritedProperty()) {
    if (parent_computed_style->InheritedVariables() !=
        state.ParentStyle()->InheritedVariables()) {
      return false;
    }
  }

  return true;
}

MatchedPropertiesCache::MatchedPropertiesCache() = default;

MatchedPropertiesCache::Key::Key(const MatchResult& result)
    : Key(result,
          result.IsCacheable() ? ComputeMatchedPropertiesHash(result)
                               : HashTraits<unsigned>::EmptyValue()) {}

MatchedPropertiesCache::Key::Key(const MatchResult& result, unsigned hash)
    : result_(result), hash_(hash) {}

const CachedMatchedProperties* MatchedPropertiesCache::Find(
    const Key& key,
    const StyleResolverState& style_resolver_state) {
  DCHECK(key.IsValid());
  Cache::iterator it = cache_.find(key.hash_);
  if (it == cache_.end())
    return nullptr;
  CachedMatchedProperties* cache_item = it->value.Get();
  if (!cache_item)
    return nullptr;
  if (*cache_item != key.result_.GetMatchedProperties())
    return nullptr;
  if (cache_item->computed_style->InsideLink() !=
      style_resolver_state.Style()->InsideLink())
    return nullptr;
  if (!cache_item->DependenciesEqual(style_resolver_state))
    return nullptr;
  return cache_item;
}

bool CachedMatchedProperties::operator==(
    const MatchedPropertiesVector& properties) {
  if (properties.size() != matched_properties.size())
    return false;
  for (wtf_size_t i = 0; i < properties.size(); ++i) {
    if (properties[i].properties != matched_properties[i])
      return false;
    if (properties[i].types_.link_match_type !=
        matched_properties_types[i].link_match_type)
      return false;
    if (properties[i].types_.tree_order !=
        matched_properties_types[i].tree_order)
      return false;
    if (properties[i].types_.layer_order !=
        matched_properties_types[i].layer_order)
      return false;
    if (properties[i].types_.valid_property_filter !=
        matched_properties_types[i].valid_property_filter)
      return false;
    if (properties[i].types_.is_inline_style !=
        matched_properties_types[i].is_inline_style)
      return false;
  }
  return true;
}

bool CachedMatchedProperties::operator!=(
    const MatchedPropertiesVector& properties) {
  return !(*this == properties);
}

void MatchedPropertiesCache::Add(const Key& key,
                                 const ComputedStyle& style,
                                 const ComputedStyle& parent_style) {
  DCHECK(key.IsValid());

  Member<CachedMatchedProperties>& cache_item =
      cache_.insert(key.hash_, nullptr).stored_value->value;

  if (!cache_item)
    cache_item = MakeGarbageCollected<CachedMatchedProperties>();
  else
    cache_item->Clear();

  cache_item->Set(style, parent_style, key.result_.GetMatchedProperties());
}

void MatchedPropertiesCache::Clear() {
  // MatchedPropertiesCache must be cleared promptly because some
  // destructors in the properties (e.g., ~FontFallbackList) expect that
  // the destructors are called promptly without relying on a GC timing.
  for (auto& cache_entry : cache_) {
    if (cache_entry.value)
      cache_entry.value->Clear();
  }
  cache_.clear();
}

void MatchedPropertiesCache::ClearViewportDependent() {
  Vector<unsigned, 16> to_remove;
  for (const auto& cache_entry : cache_) {
    CachedMatchedProperties* cache_item = cache_entry.value.Get();
    if (cache_item && cache_item->computed_style->HasViewportUnits())
      to_remove.push_back(cache_entry.key);
  }
  cache_.RemoveAll(to_remove);
}

bool MatchedPropertiesCache::IsStyleCacheable(const ComputedStyle& style) {
  // Content property with attr() values depend on the attribute value of the
  // originating element, thus we cannot cache based on the matched properties
  // because the value of content is retrieved from the attribute at apply time.
  if (style.HasAttrContent())
    return false;
  if (style.Zoom() != ComputedStyleInitialValues::InitialZoom())
    return false;
  if (style.TextAutosizingMultiplier() != 1)
    return false;
  if (style.HasContainerRelativeUnits())
    return false;
  // Avoiding cache for ::highlight styles, and the originating styles they are
  // associated with, because the style depends on the highlight names involved
  // and they're not cached.
  if (style.HasPseudoElementStyle(kPseudoIdHighlight) ||
      style.StyleType() == kPseudoIdHighlight)
    return false;
  return true;
}

bool MatchedPropertiesCache::IsCacheable(const StyleResolverState& state) {
  const ComputedStyle& style = *state.Style();
  const ComputedStyle& parent_style = *state.ParentStyle();

  if (!IsStyleCacheable(style))
    return false;

  // The cache assumes static knowledge about which properties are inherited.
  // Without a flat tree parent, StyleBuilder::ApplyProperty will not
  // SetChildHasExplicitInheritance on the parent style.
  if (!state.ParentNode() || parent_style.ChildHasExplicitInheritance())
    return false;

  // Do not cache computed styles for shadow root children which have a
  // different UserModify value than its shadow host.
  //
  // UserModify is modified to not inherit from the shadow host for shadow root
  // children. That means that if we get a MatchedPropertiesCache match for a
  // style stored for a shadow root child against a non shadow root child, we
  // would end up with an incorrect match.
  if (IsAtShadowBoundary(&state.GetElement()) &&
      style.UserModify() != parent_style.UserModify()) {
    return false;
  }

  return true;
}

void MatchedPropertiesCache::Trace(Visitor* visitor) const {
  visitor->Trace(cache_);
  visitor->RegisterWeakCallbackMethod<
      MatchedPropertiesCache,
      &MatchedPropertiesCache::RemoveCachedMatchedPropertiesWithDeadEntries>(
      this);
}

void MatchedPropertiesCache::RemoveCachedMatchedPropertiesWithDeadEntries(
    const LivenessBroker& info) {
  Vector<unsigned> to_remove;
  for (const auto& entry_pair : cache_) {
    // A nullptr value indicates that the entry is currently being created; see
    // |MatchedPropertiesCache::Add|. Keep such entries.
    if (!entry_pair.value)
      continue;
    for (const auto& matched_properties :
         entry_pair.value->matched_properties) {
      if (!info.IsHeapObjectAlive(matched_properties)) {
        to_remove.push_back(entry_pair.key);
        break;
      }
    }
  }
  // Allocation is forbidden during executing weak callbacks, so the data
  // structure will not be rehashed here. The next insertion/deletion from
  // regular code will take care of shrinking accordingly.
  cache_.RemoveAll(to_remove);
}

}  // namespace blink
