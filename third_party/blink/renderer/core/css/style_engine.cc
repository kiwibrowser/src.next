/*
 * Copyright (C) 1999 Lars Knoll (knoll@kde.org)
 *           (C) 1999 Antti Koivisto (koivisto@kde.org)
 *           (C) 2001 Dirk Mueller (mueller@kde.org)
 *           (C) 2006 Alexey Proskuryakov (ap@webkit.org)
 * Copyright (C) 2004, 2005, 2006, 2007, 2008, 2009, 2011, 2012 Apple Inc. All
 * rights reserved.
 * Copyright (C) 2008, 2009 Torch Mobile Inc. All rights reserved.
 * (http://www.torchmobile.com/)
 * Copyright (C) 2008, 2009, 2011, 2012 Google Inc. All rights reserved.
 * Copyright (C) 2010 Nokia Corporation and/or its subsidiary(-ies)
 * Copyright (C) Research In Motion Limited 2010-2011. All rights reserved.
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

#include "third_party/blink/renderer/core/css/style_engine.h"

#include "third_party/blink/public/mojom/frame/color_scheme.mojom-blink.h"
#include "third_party/blink/public/platform/platform.h"
#include "third_party/blink/public/platform/web_theme_engine.h"
#include "third_party/blink/renderer/core/css/container_query_evaluator.h"
#include "third_party/blink/renderer/core/css/counter_style_map.h"
#include "third_party/blink/renderer/core/css/css_default_style_sheets.h"
#include "third_party/blink/renderer/core/css/css_font_family_value.h"
#include "third_party/blink/renderer/core/css/css_font_selector.h"
#include "third_party/blink/renderer/core/css/css_identifier_value.h"
#include "third_party/blink/renderer/core/css/css_style_sheet.h"
#include "third_party/blink/renderer/core/css/css_uri_value.h"
#include "third_party/blink/renderer/core/css/css_value_list.h"
#include "third_party/blink/renderer/core/css/document_style_environment_variables.h"
#include "third_party/blink/renderer/core/css/document_style_sheet_collector.h"
#include "third_party/blink/renderer/core/css/font_face_cache.h"
#include "third_party/blink/renderer/core/css/has_matched_cache_scope.h"
#include "third_party/blink/renderer/core/css/invalidation/invalidation_set.h"
#include "third_party/blink/renderer/core/css/media_feature_overrides.h"
#include "third_party/blink/renderer/core/css/media_values.h"
#include "third_party/blink/renderer/core/css/property_registration.h"
#include "third_party/blink/renderer/core/css/property_registry.h"
#include "third_party/blink/renderer/core/css/resolver/scoped_style_resolver.h"
#include "third_party/blink/renderer/core/css/resolver/selector_filter_parent_scope.h"
#include "third_party/blink/renderer/core/css/resolver/style_rule_usage_tracker.h"
#include "third_party/blink/renderer/core/css/resolver/viewport_style_resolver.h"
#include "third_party/blink/renderer/core/css/shadow_tree_style_sheet_collection.h"
#include "third_party/blink/renderer/core/css/style_change_reason.h"
#include "third_party/blink/renderer/core/css/style_environment_variables.h"
#include "third_party/blink/renderer/core/css/style_sheet_contents.h"
#include "third_party/blink/renderer/core/css/vision_deficiency.h"
#include "third_party/blink/renderer/core/display_lock/display_lock_utilities.h"
#include "third_party/blink/renderer/core/dom/document_lifecycle.h"
#include "third_party/blink/renderer/core/dom/element.h"
#include "third_party/blink/renderer/core/dom/element_traversal.h"
#include "third_party/blink/renderer/core/dom/flat_tree_traversal.h"
#include "third_party/blink/renderer/core/dom/layout_tree_builder_traversal.h"
#include "third_party/blink/renderer/core/dom/node_computed_style.h"
#include "third_party/blink/renderer/core/dom/nth_index_cache.h"
#include "third_party/blink/renderer/core/dom/processing_instruction.h"
#include "third_party/blink/renderer/core/dom/shadow_root.h"
#include "third_party/blink/renderer/core/dom/text.h"
#include "third_party/blink/renderer/core/frame/settings.h"
#include "third_party/blink/renderer/core/html/forms/html_select_element.h"
#include "third_party/blink/renderer/core/html/html_body_element.h"
#include "third_party/blink/renderer/core/html/html_html_element.h"
#include "third_party/blink/renderer/core/html/html_iframe_element.h"
#include "third_party/blink/renderer/core/html/html_link_element.h"
#include "third_party/blink/renderer/core/html/html_slot_element.h"
#include "third_party/blink/renderer/core/html_names.h"
#include "third_party/blink/renderer/core/layout/adjust_for_absolute_zoom.h"
#include "third_party/blink/renderer/core/layout/geometry/logical_size.h"
#include "third_party/blink/renderer/core/layout/geometry/physical_size.h"
#include "third_party/blink/renderer/core/layout/layout_object.h"
#include "third_party/blink/renderer/core/layout/layout_theme.h"
#include "third_party/blink/renderer/core/layout/layout_view.h"
#include "third_party/blink/renderer/core/page/page.h"
#include "third_party/blink/renderer/core/page/page_popup_controller.h"
#include "third_party/blink/renderer/core/paint/paint_layer.h"
#include "third_party/blink/renderer/core/probe/core_probes.h"
#include "third_party/blink/renderer/core/style/computed_style.h"
#include "third_party/blink/renderer/core/style/filter_operations.h"
#include "third_party/blink/renderer/core/style/style_initial_data.h"
#include "third_party/blink/renderer/core/svg/svg_resource.h"
#include "third_party/blink/renderer/core/svg/svg_style_element.h"
#include "third_party/blink/renderer/platform/fonts/font_cache.h"
#include "third_party/blink/renderer/platform/fonts/font_selector.h"
#include "third_party/blink/renderer/platform/heap/heap.h"
#include "third_party/blink/renderer/platform/instrumentation/histogram.h"
#include "third_party/blink/renderer/platform/instrumentation/tracing/trace_event.h"
#include "third_party/blink/renderer/platform/wtf/vector.h"

namespace blink {

namespace {

CSSFontSelector* CreateCSSFontSelectorFor(Document& document) {
  DCHECK(document.GetFrame());
  if (UNLIKELY(document.GetFrame()->PagePopupOwner()))
    return PagePopupController::CreateCSSFontSelector(document);
  return MakeGarbageCollected<CSSFontSelector>(document);
}

}  // namespace

StyleEngine::StyleEngine(Document& document)
    : document_(&document),
      document_style_sheet_collection_(
          MakeGarbageCollected<DocumentStyleSheetCollection>(document)),
      resolver_(MakeGarbageCollected<StyleResolver>(document)),
      owner_color_scheme_(mojom::blink::ColorScheme::kLight) {
  if (document.GetFrame()) {
    global_rule_set_ = MakeGarbageCollected<CSSGlobalRuleSet>();
    font_selector_ = CreateCSSFontSelectorFor(document);
    font_selector_->RegisterForInvalidationCallbacks(this);
    if (const auto* owner = document.GetFrame()->Owner())
      owner_color_scheme_ = owner->GetColorScheme();
  }
  if (document.IsInMainFrame())
    viewport_resolver_ = MakeGarbageCollected<ViewportStyleResolver>(document);
  if (auto* settings = GetDocument().GetSettings()) {
    if (!settings->GetForceDarkModeEnabled())
      preferred_color_scheme_ = settings->GetPreferredColorScheme();
    UpdateColorSchemeMetrics();
  }
  if (Platform::Current() && Platform::Current()->ThemeEngine())
    forced_colors_ = Platform::Current()->ThemeEngine()->GetForcedColors();
  UpdateForcedBackgroundColor();
}

StyleEngine::~StyleEngine() = default;

TreeScopeStyleSheetCollection& StyleEngine::EnsureStyleSheetCollectionFor(
    TreeScope& tree_scope) {
  if (tree_scope == document_)
    return GetDocumentStyleSheetCollection();

  StyleSheetCollectionMap::AddResult result =
      style_sheet_collection_map_.insert(&tree_scope, nullptr);
  if (result.is_new_entry) {
    result.stored_value->value =
        MakeGarbageCollected<ShadowTreeStyleSheetCollection>(
            To<ShadowRoot>(tree_scope));
  }
  return *result.stored_value->value.Get();
}

TreeScopeStyleSheetCollection* StyleEngine::StyleSheetCollectionFor(
    TreeScope& tree_scope) {
  if (tree_scope == document_)
    return &GetDocumentStyleSheetCollection();

  StyleSheetCollectionMap::iterator it =
      style_sheet_collection_map_.find(&tree_scope);
  if (it == style_sheet_collection_map_.end())
    return nullptr;
  return it->value.Get();
}

const HeapVector<Member<StyleSheet>>& StyleEngine::StyleSheetsForStyleSheetList(
    TreeScope& tree_scope) {
  DCHECK(document_);
  TreeScopeStyleSheetCollection& collection =
      EnsureStyleSheetCollectionFor(tree_scope);
  if (document_->IsActive())
    collection.UpdateStyleSheetList();
  return collection.StyleSheetsForStyleSheetList();
}

void StyleEngine::InjectSheet(const StyleSheetKey& key,
                              StyleSheetContents* sheet,
                              WebDocument::CSSOrigin origin) {
  HeapVector<std::pair<StyleSheetKey, Member<CSSStyleSheet>>>&
      injected_style_sheets =
          origin == WebDocument::kUserOrigin ? injected_user_style_sheets_
                                             : injected_author_style_sheets_;
  injected_style_sheets.push_back(std::make_pair(
      key, MakeGarbageCollected<CSSStyleSheet>(sheet, *document_)));
  if (origin == WebDocument::kUserOrigin)
    MarkUserStyleDirty();
  else
    MarkDocumentDirty();
}

void StyleEngine::RemoveInjectedSheet(const StyleSheetKey& key,
                                      WebDocument::CSSOrigin origin) {
  HeapVector<std::pair<StyleSheetKey, Member<CSSStyleSheet>>>&
      injected_style_sheets =
          origin == WebDocument::kUserOrigin ? injected_user_style_sheets_
                                             : injected_author_style_sheets_;
  // Remove the last sheet that matches.
  const auto& it = std::find_if(injected_style_sheets.rbegin(),
                                injected_style_sheets.rend(),
                                [&key](const auto& item) {
                                  return item.first == key;
                                });
  if (it != injected_style_sheets.rend()) {
    injected_style_sheets.erase(std::next(it).base());
    if (origin == WebDocument::kUserOrigin)
      MarkUserStyleDirty();
    else
      MarkDocumentDirty();
  }
}

CSSStyleSheet& StyleEngine::EnsureInspectorStyleSheet() {
  if (inspector_style_sheet_)
    return *inspector_style_sheet_;

  auto* contents = MakeGarbageCollected<StyleSheetContents>(
      MakeGarbageCollected<CSSParserContext>(*document_));
  inspector_style_sheet_ =
      MakeGarbageCollected<CSSStyleSheet>(contents, *document_);
  MarkDocumentDirty();
  // TODO(futhark@chromium.org): Making the active stylesheets up-to-date here
  // is required by some inspector tests, at least. I theory this should not be
  // necessary. Need to investigate to figure out if/why.
  UpdateActiveStyle();
  return *inspector_style_sheet_;
}

void StyleEngine::AddPendingSheet(StyleEngineContext& context) {
  pending_script_blocking_stylesheets_++;

  context.AddingPendingSheet(GetDocument());

  if (context.AddedPendingSheetBeforeBody()) {
    pending_render_blocking_stylesheets_++;
  } else {
    pending_parser_blocking_stylesheets_++;
    GetDocument().DidAddPendingParserBlockingStylesheet();
  }
}

// This method is called whenever a top-level stylesheet has finished loading.
void StyleEngine::RemovePendingSheet(Node& style_sheet_candidate_node,
                                     const StyleEngineContext& context) {
  if (style_sheet_candidate_node.isConnected())
    SetNeedsActiveStyleUpdate(style_sheet_candidate_node.GetTreeScope());

  if (context.AddedPendingSheetBeforeBody()) {
    DCHECK_GT(pending_render_blocking_stylesheets_, 0);
    pending_render_blocking_stylesheets_--;
  } else {
    DCHECK_GT(pending_parser_blocking_stylesheets_, 0);
    pending_parser_blocking_stylesheets_--;
    if (!pending_parser_blocking_stylesheets_)
      GetDocument().DidLoadAllPendingParserBlockingStylesheets();
  }

  // Make sure we knew this sheet was pending, and that our count isn't out of
  // sync.
  DCHECK_GT(pending_script_blocking_stylesheets_, 0);

  pending_script_blocking_stylesheets_--;
  if (pending_script_blocking_stylesheets_)
    return;

  GetDocument().DidRemoveAllPendingStylesheets();
}

void StyleEngine::SetNeedsActiveStyleUpdate(TreeScope& tree_scope) {
  DCHECK(tree_scope.RootNode().isConnected());
  if (GetDocument().IsActive())
    MarkTreeScopeDirty(tree_scope);
}

void StyleEngine::AddStyleSheetCandidateNode(Node& node) {
  if (!node.isConnected() || GetDocument().IsDetached())
    return;

  DCHECK(!IsXSLStyleSheet(node));
  TreeScope& tree_scope = node.GetTreeScope();
  EnsureStyleSheetCollectionFor(tree_scope).AddStyleSheetCandidateNode(node);

  SetNeedsActiveStyleUpdate(tree_scope);
  if (tree_scope != document_)
    active_tree_scopes_.insert(&tree_scope);
}

void StyleEngine::RemoveStyleSheetCandidateNode(
    Node& node,
    ContainerNode& insertion_point) {
  DCHECK(!IsXSLStyleSheet(node));
  DCHECK(insertion_point.isConnected());

  ShadowRoot* shadow_root = node.ContainingShadowRoot();
  if (!shadow_root)
    shadow_root = insertion_point.ContainingShadowRoot();

  static_assert(std::is_base_of<TreeScope, ShadowRoot>::value,
                "The ShadowRoot must be subclass of TreeScope.");
  TreeScope& tree_scope =
      shadow_root ? static_cast<TreeScope&>(*shadow_root) : GetDocument();
  TreeScopeStyleSheetCollection* collection =
      StyleSheetCollectionFor(tree_scope);
  // After detaching document, collection could be null. In the case,
  // we should not update anything. Instead, just return.
  if (!collection)
    return;
  collection->RemoveStyleSheetCandidateNode(node);

  SetNeedsActiveStyleUpdate(tree_scope);
}

void StyleEngine::ModifiedStyleSheetCandidateNode(Node& node) {
  if (node.isConnected())
    SetNeedsActiveStyleUpdate(node.GetTreeScope());
}

void StyleEngine::AdoptedStyleSheetsWillChange(
    TreeScope& tree_scope,
    const HeapVector<Member<CSSStyleSheet>>& old_sheets,
    const HeapVector<Member<CSSStyleSheet>>& new_sheets) {
  if (GetDocument().IsDetached())
    return;

  unsigned old_sheets_count = old_sheets.size();
  unsigned new_sheets_count = new_sheets.size();

  unsigned min_count = std::min(old_sheets_count, new_sheets_count);
  unsigned index = 0;
  while (index < min_count && old_sheets[index] == new_sheets[index]) {
    index++;
  }

  if (old_sheets_count == new_sheets_count && index == old_sheets_count)
    return;

  for (unsigned i = index; i < old_sheets_count; ++i) {
    old_sheets[i]->RemovedAdoptedFromTreeScope(tree_scope);
  }
  for (unsigned i = index; i < new_sheets_count; ++i) {
    new_sheets[i]->AddedAdoptedToTreeScope(tree_scope);
  }

  if (!tree_scope.RootNode().isConnected())
    return;

  if (new_sheets_count) {
    EnsureStyleSheetCollectionFor(tree_scope);
    if (tree_scope != document_)
      active_tree_scopes_.insert(&tree_scope);
  } else if (!StyleSheetCollectionFor(tree_scope)) {
    return;
  }
  SetNeedsActiveStyleUpdate(tree_scope);
}

void StyleEngine::AddedCustomElementDefaultStyles(
    const HeapVector<Member<CSSStyleSheet>>& default_styles) {
  if (!RuntimeEnabledFeatures::CustomElementDefaultStyleEnabled() ||
      GetDocument().IsDetached())
    return;
  for (CSSStyleSheet* sheet : default_styles)
    custom_element_default_style_sheets_.insert(sheet);
  global_rule_set_->MarkDirty();
}

void StyleEngine::MediaQueryAffectingValueChanged(TreeScope& tree_scope,
                                                  MediaValueChange change) {
  auto* collection = StyleSheetCollectionFor(tree_scope);
  DCHECK(collection);
  if (AffectedByMediaValueChange(collection->ActiveStyleSheets(), change))
    SetNeedsActiveStyleUpdate(tree_scope);
}

void StyleEngine::WatchedSelectorsChanged() {
  DCHECK(global_rule_set_);
  global_rule_set_->InitWatchedSelectorsRuleSet(GetDocument());
  // TODO(futhark@chromium.org): Should be able to use RuleSetInvalidation here.
  MarkAllElementsForStyleRecalc(StyleChangeReasonForTracing::Create(
      style_change_reason::kDeclarativeContent));
}

bool StyleEngine::ShouldUpdateDocumentStyleSheetCollection() const {
  return document_scope_dirty_;
}

bool StyleEngine::ShouldUpdateShadowTreeStyleSheetCollection() const {
  return !dirty_tree_scopes_.IsEmpty();
}

void StyleEngine::MediaQueryAffectingValueChanged(
    UnorderedTreeScopeSet& tree_scopes,
    MediaValueChange change) {
  for (TreeScope* tree_scope : tree_scopes) {
    DCHECK(tree_scope != document_);
    MediaQueryAffectingValueChanged(*tree_scope, change);
  }
}

void StyleEngine::AddTextTrack(TextTrack* text_track) {
  text_tracks_.insert(text_track);
}

void StyleEngine::RemoveTextTrack(TextTrack* text_track) {
  text_tracks_.erase(text_track);
}

Element* StyleEngine::EnsureVTTOriginatingElement() {
  if (!vtt_originating_element_) {
    vtt_originating_element_ = MakeGarbageCollected<Element>(
        QualifiedName(g_null_atom, g_empty_atom, g_empty_atom), document_);
  }
  return vtt_originating_element_;
}

void StyleEngine::MediaQueryAffectingValueChanged(
    HeapHashSet<Member<TextTrack>>& text_tracks,
    MediaValueChange change) {
  if (text_tracks.IsEmpty())
    return;

  for (auto text_track : text_tracks) {
    bool style_needs_recalc = false;
    auto style_sheets = text_track->GetCSSStyleSheets();
    for (const auto& sheet : style_sheets) {
      StyleSheetContents* contents = sheet->Contents();
      if (contents->HasMediaQueries()) {
        style_needs_recalc = true;
        contents->ClearRuleSet();
      }
    }

    if (style_needs_recalc && text_track->Owner()) {
      // Use kSubtreeTreeStyleChange instead of RuleSet style invalidation
      // because it won't be expensive for tracks and we won't have dynamic
      // changes.
      text_track->Owner()->SetNeedsStyleRecalc(
          kSubtreeStyleChange,
          StyleChangeReasonForTracing::Create(style_change_reason::kShadow));
    }
  }
}

void StyleEngine::MediaQueryAffectingValueChanged(MediaValueChange change) {
  if (AffectedByMediaValueChange(active_user_style_sheets_, change))
    MarkUserStyleDirty();
  MediaQueryAffectingValueChanged(GetDocument(), change);
  MediaQueryAffectingValueChanged(active_tree_scopes_, change);
  MediaQueryAffectingValueChanged(text_tracks_, change);
  if (resolver_)
    resolver_->UpdateMediaType();
}

void StyleEngine::UpdateActiveStyleSheetsInShadow(
    TreeScope* tree_scope,
    UnorderedTreeScopeSet& tree_scopes_removed) {
  DCHECK_NE(tree_scope, document_);
  auto* collection =
      To<ShadowTreeStyleSheetCollection>(StyleSheetCollectionFor(*tree_scope));
  DCHECK(collection);
  collection->UpdateActiveStyleSheets(*this);
  if (!collection->HasStyleSheetCandidateNodes() &&
      !tree_scope->HasAdoptedStyleSheets()) {
    tree_scopes_removed.insert(tree_scope);
    // When removing TreeScope from ActiveTreeScopes,
    // its resolver should be destroyed by invoking resetAuthorStyle.
    DCHECK(!tree_scope->GetScopedStyleResolver());
  }
}

void StyleEngine::UpdateActiveUserStyleSheets() {
  DCHECK(user_style_dirty_);

  ActiveStyleSheetVector new_active_sheets;
  for (auto& sheet : injected_user_style_sheets_) {
    if (RuleSet* rule_set = RuleSetForSheet(*sheet.second))
      new_active_sheets.push_back(std::make_pair(sheet.second, rule_set));
  }

  ApplyUserRuleSetChanges(active_user_style_sheets_, new_active_sheets);
  new_active_sheets.swap(active_user_style_sheets_);
}

void StyleEngine::UpdateActiveStyleSheets() {
  if (!NeedsActiveStyleSheetUpdate())
    return;

  DCHECK(!GetDocument().InStyleRecalc());
  DCHECK(GetDocument().IsActive());

  TRACE_EVENT0("blink,blink_style", "StyleEngine::updateActiveStyleSheets");

  if (user_style_dirty_)
    UpdateActiveUserStyleSheets();

  if (ShouldUpdateDocumentStyleSheetCollection())
    GetDocumentStyleSheetCollection().UpdateActiveStyleSheets(*this);

  if (ShouldUpdateShadowTreeStyleSheetCollection()) {
    UnorderedTreeScopeSet tree_scopes_removed;
    for (TreeScope* tree_scope : dirty_tree_scopes_)
      UpdateActiveStyleSheetsInShadow(tree_scope, tree_scopes_removed);
    for (TreeScope* tree_scope : tree_scopes_removed)
      active_tree_scopes_.erase(tree_scope);
  }

  probe::ActiveStyleSheetsUpdated(document_);

  dirty_tree_scopes_.clear();
  document_scope_dirty_ = false;
  tree_scopes_removed_ = false;
  user_style_dirty_ = false;
}

void StyleEngine::UpdateCounterStyles() {
  if (!counter_styles_need_update_)
    return;
  CounterStyleMap::MarkAllDirtyCounterStyles(GetDocument(),
                                             active_tree_scopes_);
  CounterStyleMap::ResolveAllReferences(GetDocument(), active_tree_scopes_);
  counter_styles_need_update_ = false;
}

void StyleEngine::UpdateViewport() {
  if (viewport_resolver_)
    viewport_resolver_->UpdateViewport(GetDocumentStyleSheetCollection());
}

bool StyleEngine::NeedsActiveStyleUpdate() const {
  return (viewport_resolver_ && viewport_resolver_->NeedsUpdate()) ||
         NeedsActiveStyleSheetUpdate() ||
         (global_rule_set_ && global_rule_set_->IsDirty());
}

void StyleEngine::UpdateActiveStyle() {
  DCHECK(GetDocument().IsActive());
  DCHECK(IsMainThread());
  TRACE_EVENT0("blink", "Document::updateActiveStyle");
  UpdateViewport();
  UpdateActiveStyleSheets();
  UpdateGlobalRuleSet();
  UpdateTimelines();
}

const ActiveStyleSheetVector StyleEngine::ActiveStyleSheetsForInspector() {
  if (GetDocument().IsActive())
    UpdateActiveStyle();

  if (active_tree_scopes_.IsEmpty())
    return GetDocumentStyleSheetCollection().ActiveStyleSheets();

  ActiveStyleSheetVector active_style_sheets;

  active_style_sheets.AppendVector(
      GetDocumentStyleSheetCollection().ActiveStyleSheets());
  for (TreeScope* tree_scope : active_tree_scopes_) {
    if (TreeScopeStyleSheetCollection* collection =
            style_sheet_collection_map_.at(tree_scope))
      active_style_sheets.AppendVector(collection->ActiveStyleSheets());
  }

  // FIXME: Inspector needs a vector which has all active stylesheets.
  // However, creating such a large vector might cause performance regression.
  // Need to implement some smarter solution.
  return active_style_sheets;
}

void StyleEngine::ShadowRootInsertedToDocument(ShadowRoot& shadow_root) {
  DCHECK(shadow_root.isConnected());
  if (GetDocument().IsDetached() || !shadow_root.HasAdoptedStyleSheets())
    return;
  EnsureStyleSheetCollectionFor(shadow_root);
  SetNeedsActiveStyleUpdate(shadow_root);
  active_tree_scopes_.insert(&shadow_root);
}

void StyleEngine::ShadowRootRemovedFromDocument(ShadowRoot* shadow_root) {
  style_sheet_collection_map_.erase(shadow_root);
  active_tree_scopes_.erase(shadow_root);
  dirty_tree_scopes_.erase(shadow_root);
  tree_scopes_removed_ = true;
  ResetAuthorStyle(*shadow_root);
}

void StyleEngine::ResetAuthorStyle(TreeScope& tree_scope) {
  ScopedStyleResolver* scoped_resolver = tree_scope.GetScopedStyleResolver();
  if (!scoped_resolver)
    return;

  if (global_rule_set_)
    global_rule_set_->MarkDirty();
  if (tree_scope.RootNode().IsDocumentNode()) {
    scoped_resolver->ResetStyle();
    return;
  }

  tree_scope.ClearScopedStyleResolver();
}

void StyleEngine::SetRuleUsageTracker(StyleRuleUsageTracker* tracker) {
  tracker_ = tracker;

  if (resolver_)
    resolver_->SetRuleUsageTracker(tracker_);
}

RuleSet* StyleEngine::RuleSetForSheet(CSSStyleSheet& sheet) {
  if (!sheet.MatchesMediaQueries(EnsureMediaQueryEvaluator()))
    return nullptr;

  AddRuleFlags add_rule_flags = kRuleHasNoSpecialState;
  if (document_->GetExecutionContext()->GetSecurityOrigin()->CanRequest(
          sheet.BaseURL())) {
    add_rule_flags = kRuleHasDocumentSecurityOrigin;
  }
  return &sheet.Contents()->EnsureRuleSet(*media_query_evaluator_,
                                          add_rule_flags);
}

void StyleEngine::ClearResolvers() {
  DCHECK(!GetDocument().InStyleRecalc());

  GetDocument().ClearScopedStyleResolver();
  for (TreeScope* tree_scope : active_tree_scopes_)
    tree_scope->ClearScopedStyleResolver();

  if (resolver_) {
    TRACE_EVENT1("blink", "StyleEngine::clearResolver", "frame",
                 ToTraceValue(GetDocument().GetFrame()));
    resolver_->Dispose();
    resolver_.Clear();
  }
}

void StyleEngine::DidDetach() {
  ClearResolvers();
  if (global_rule_set_)
    global_rule_set_->Dispose();
  global_rule_set_ = nullptr;
  dirty_tree_scopes_.clear();
  active_tree_scopes_.clear();
  viewport_resolver_ = nullptr;
  media_query_evaluator_ = nullptr;
  style_invalidation_root_.Clear();
  style_recalc_root_.Clear();
  layout_tree_rebuild_root_.Clear();
  if (font_selector_)
    font_selector_->GetFontFaceCache()->ClearAll();
  font_selector_ = nullptr;
  if (environment_variables_)
    environment_variables_->DetachFromParent();
  environment_variables_ = nullptr;
}

bool StyleEngine::ClearFontFaceCacheAndAddUserFonts(
    const ActiveStyleSheetVector& user_sheets) {
  bool fonts_changed = false;

  if (font_selector_ &&
      font_selector_->GetFontFaceCache()->ClearCSSConnected()) {
    fonts_changed = true;
    if (resolver_)
      resolver_->InvalidateMatchedPropertiesCache();
  }

  // Rebuild the font cache with @font-face rules from user style sheets.
  for (unsigned i = 0; i < user_sheets.size(); ++i) {
    DCHECK(user_sheets[i].second);
    if (AddUserFontFaceRules(*user_sheets[i].second))
      fonts_changed = true;
  }

  return fonts_changed;
}

void StyleEngine::UpdateGenericFontFamilySettings() {
  // FIXME: we should not update generic font family settings when
  // document is inactive.
  DCHECK(GetDocument().IsActive());

  if (!font_selector_)
    return;

  font_selector_->UpdateGenericFontFamilySettings(*document_);
  if (resolver_)
    resolver_->InvalidateMatchedPropertiesCache();
  FontCache::GetFontCache()->InvalidateShapeCache();
}

void StyleEngine::RemoveFontFaceRules(
    const HeapVector<Member<const StyleRuleFontFace>>& font_face_rules) {
  if (!font_selector_)
    return;

  FontFaceCache* cache = font_selector_->GetFontFaceCache();
  for (const auto& rule : font_face_rules)
    cache->Remove(rule);
  if (resolver_)
    resolver_->InvalidateMatchedPropertiesCache();
}

void StyleEngine::MarkTreeScopeDirty(TreeScope& scope) {
  if (scope == document_) {
    MarkDocumentDirty();
    return;
  }

  TreeScopeStyleSheetCollection* collection = StyleSheetCollectionFor(scope);
  DCHECK(collection);
  collection->MarkSheetListDirty();
  dirty_tree_scopes_.insert(&scope);
  GetDocument().ScheduleLayoutTreeUpdateIfNeeded();
}

void StyleEngine::MarkDocumentDirty() {
  document_scope_dirty_ = true;
  document_style_sheet_collection_->MarkSheetListDirty();
  GetDocument().ScheduleLayoutTreeUpdateIfNeeded();
}

void StyleEngine::MarkUserStyleDirty() {
  user_style_dirty_ = true;
  GetDocument().ScheduleLayoutTreeUpdateIfNeeded();
}

void StyleEngine::MarkViewportStyleDirty() {
  viewport_style_dirty_ = true;
  GetDocument().ScheduleLayoutTreeUpdateIfNeeded();
}

CSSStyleSheet* StyleEngine::CreateSheet(Element& element,
                                        const String& text,
                                        TextPosition start_position,
                                        StyleEngineContext& context) {
  DCHECK(element.GetDocument() == GetDocument());
  CSSStyleSheet* style_sheet = nullptr;

  AddPendingSheet(context);

  AtomicString text_content(text);

  auto result = text_to_sheet_cache_.insert(text_content, nullptr);
  StyleSheetContents* contents = result.stored_value->value;
  if (result.is_new_entry || !contents ||
      !contents->IsCacheableForStyleElement()) {
    result.stored_value->value = nullptr;
    style_sheet = ParseSheet(element, text, start_position);
    if (style_sheet->Contents()->IsCacheableForStyleElement()) {
      result.stored_value->value = style_sheet->Contents();
      sheet_to_text_cache_.insert(style_sheet->Contents(), text_content);
    }
  } else {
    DCHECK(contents);
    DCHECK(contents->IsCacheableForStyleElement());
    DCHECK(contents->HasSingleOwnerDocument());
    contents->SetIsUsedFromTextCache();
    style_sheet =
        CSSStyleSheet::CreateInline(contents, element, start_position);
  }

  DCHECK(style_sheet);
  if (!element.IsInShadowTree()) {
    String title = element.title();
    if (!title.IsEmpty()) {
      style_sheet->SetTitle(title);
      SetPreferredStylesheetSetNameIfNotSet(title);
    }
  }
  return style_sheet;
}

CSSStyleSheet* StyleEngine::ParseSheet(Element& element,
                                       const String& text,
                                       TextPosition start_position) {
  CSSStyleSheet* style_sheet = nullptr;
  style_sheet = CSSStyleSheet::CreateInline(element, NullURL(), start_position,
                                            GetDocument().Encoding());
  style_sheet->Contents()->ParseString(text);
  return style_sheet;
}

void StyleEngine::CollectUserStyleFeaturesTo(RuleFeatureSet& features) const {
  for (unsigned i = 0; i < active_user_style_sheets_.size(); ++i) {
    CSSStyleSheet* sheet = active_user_style_sheets_[i].first;
    features.ViewportDependentMediaQueryResults().AppendVector(
        sheet->ViewportDependentMediaQueryResults());
    features.DeviceDependentMediaQueryResults().AppendVector(
        sheet->DeviceDependentMediaQueryResults());
    DCHECK(sheet->Contents()->HasRuleSet());
    features.Add(sheet->Contents()->GetRuleSet().Features());
  }
}

void StyleEngine::CollectScopedStyleFeaturesTo(RuleFeatureSet& features) const {
  HeapHashSet<Member<const StyleSheetContents>>
      visited_shared_style_sheet_contents;
  if (GetDocument().GetScopedStyleResolver()) {
    GetDocument().GetScopedStyleResolver()->CollectFeaturesTo(
        features, visited_shared_style_sheet_contents);
  }
  for (TreeScope* tree_scope : active_tree_scopes_) {
    if (ScopedStyleResolver* resolver = tree_scope->GetScopedStyleResolver()) {
      resolver->CollectFeaturesTo(features,
                                  visited_shared_style_sheet_contents);
    }
  }
}

void StyleEngine::InvalidateStyleAndLayoutForFontUpdates() {
  if (!fonts_need_update_)
    return;

  TRACE_EVENT0("blink", "StyleEngine::InvalidateStyleAndLayoutForFontUpdates");

  fonts_need_update_ = false;

  if (Element* root = GetDocument().documentElement()) {
    TRACE_EVENT0("blink", "Node::MarkSubtreeNeedsStyleRecalcForFontUpdates");
    root->MarkSubtreeNeedsStyleRecalcForFontUpdates();
  }

  // TODO(xiaochengh): Move layout invalidation after style update.
  if (LayoutView* layout_view = GetDocument().GetLayoutView()) {
    TRACE_EVENT0("blink", "LayoutObject::InvalidateSubtreeForFontUpdates");
    layout_view->InvalidateSubtreeLayoutForFontUpdates();
  }
}

void StyleEngine::MarkFontsNeedUpdate() {
  fonts_need_update_ = true;
  GetDocument().ScheduleLayoutTreeUpdateIfNeeded();
}

void StyleEngine::MarkCounterStylesNeedUpdate() {
  counter_styles_need_update_ = true;
  if (LayoutView* layout_view = GetDocument().GetLayoutView())
    layout_view->SetNeedsMarkerOrCounterUpdate();
  GetDocument().ScheduleLayoutTreeUpdateIfNeeded();
}

void StyleEngine::FontsNeedUpdate(FontSelector*, FontInvalidationReason) {
  if (!GetDocument().IsActive())
    return;

  if (resolver_)
    resolver_->InvalidateMatchedPropertiesCache();
  MarkViewportStyleDirty();
  MarkFontsNeedUpdate();

  probe::FontsUpdated(document_->GetExecutionContext(), nullptr, String(),
                      nullptr);
}

void StyleEngine::PlatformColorsChanged() {
  UpdateForcedBackgroundColor();
  UpdateColorSchemeBackground(/* color_scheme_changed */ true);
  if (resolver_)
    resolver_->InvalidateMatchedPropertiesCache();
  MarkAllElementsForStyleRecalc(StyleChangeReasonForTracing::Create(
      style_change_reason::kPlatformColorChange));
}

bool StyleEngine::ShouldSkipInvalidationFor(const Element& element) const {
  DCHECK(element.GetDocument() == &GetDocument())
      << "Only schedule invalidations using the StyleEngine of the Document "
         "which owns the element.";
  if (!element.InActiveDocument())
    return true;
  if (!global_rule_set_) {
    // TODO(crbug.com/1175902): This is a speculative fix for a crash.
    NOTREACHED()
        << "global_rule_set_ should only be null for inactive documents.";
    return true;
  }
  if (GetDocument().InStyleRecalc()) {
#if DCHECK_IS_ON()
    // TODO(futhark): The InStyleRecalc() if-guard above should have been a
    // DCHECK(!InStyleRecalc()), but there are a couple of cases where we try to
    // invalidate style from style recalc:
    //
    // 1. We may animate the class attribute of an SVG element and change it
    //    during style recalc when applying the animation effect.
    // 2. We may call SetInlineStyle on elements in a UA shadow tree as part of
    //    style recalc. For instance from HTMLImageFallbackHelper.
    //
    // If there are more cases, we need to adjust the DCHECKs below, but ideally
    // The origin of these invalidations should be fixed.
    if (!element.IsSVGElement()) {
      DCHECK(element.ContainingShadowRoot());
      DCHECK(element.ContainingShadowRoot()->IsUserAgent());
    }
#endif  // DCHECK_IS_ON()
    return true;
  }
  if (GetDocument().GetStyleChangeType() == kSubtreeStyleChange)
    return true;
  Element* root = GetDocument().documentElement();
  if (!root || root->GetStyleChangeType() == kSubtreeStyleChange)
    return true;
  if (!element.parentNode())
    return true;
  return element.parentNode()->GetStyleChangeType() == kSubtreeStyleChange;
}

void StyleEngine::ClassChangedForElement(
    const SpaceSplitString& changed_classes,
    Element& element) {
  if (ShouldSkipInvalidationFor(element))
    return;
  InvalidationLists invalidation_lists;
  unsigned changed_size = changed_classes.size();
  const RuleFeatureSet& features = GetRuleFeatureSet();
  for (unsigned i = 0; i < changed_size; ++i) {
    features.CollectInvalidationSetsForClass(invalidation_lists, element,
                                             changed_classes[i]);
  }
  pending_invalidations_.ScheduleInvalidationSetsForNode(invalidation_lists,
                                                         element);
}

void StyleEngine::ClassChangedForElement(const SpaceSplitString& old_classes,
                                         const SpaceSplitString& new_classes,
                                         Element& element) {
  if (ShouldSkipInvalidationFor(element))
    return;

  if (!old_classes.size()) {
    ClassChangedForElement(new_classes, element);
    return;
  }

  // Class vectors tend to be very short. This is faster than using a hash
  // table.
  WTF::Vector<bool> remaining_class_bits(old_classes.size());

  InvalidationLists invalidation_lists;
  const RuleFeatureSet& features = GetRuleFeatureSet();

  for (unsigned i = 0; i < new_classes.size(); ++i) {
    bool found = false;
    for (unsigned j = 0; j < old_classes.size(); ++j) {
      if (new_classes[i] == old_classes[j]) {
        // Mark each class that is still in the newClasses so we can skip doing
        // an n^2 search below when looking for removals. We can't break from
        // this loop early since a class can appear more than once.
        remaining_class_bits[j] = true;
        found = true;
      }
    }
    // Class was added.
    if (!found) {
      features.CollectInvalidationSetsForClass(invalidation_lists, element,
                                               new_classes[i]);
    }
  }

  for (unsigned i = 0; i < old_classes.size(); ++i) {
    if (remaining_class_bits[i])
      continue;
    // Class was removed.
    features.CollectInvalidationSetsForClass(invalidation_lists, element,
                                             old_classes[i]);
  }
  pending_invalidations_.ScheduleInvalidationSetsForNode(invalidation_lists,
                                                         element);
}

namespace {

bool HasAttributeDependentGeneratedContent(const Element& element) {
  if (PseudoElement* before = element.GetPseudoElement(kPseudoIdBefore)) {
    const ComputedStyle* style = before->GetComputedStyle();
    if (style && style->HasAttrContent())
      return true;
  }
  if (PseudoElement* after = element.GetPseudoElement(kPseudoIdAfter)) {
    const ComputedStyle* style = after->GetComputedStyle();
    if (style && style->HasAttrContent())
      return true;
  }
  return false;
}

}  // namespace

void StyleEngine::AttributeChangedForElement(
    const QualifiedName& attribute_name,
    Element& element) {
  if (ShouldSkipInvalidationFor(element))
    return;

  InvalidationLists invalidation_lists;
  GetRuleFeatureSet().CollectInvalidationSetsForAttribute(
      invalidation_lists, element, attribute_name);
  pending_invalidations_.ScheduleInvalidationSetsForNode(invalidation_lists,
                                                         element);

  if (!element.NeedsStyleRecalc() &&
      HasAttributeDependentGeneratedContent(element)) {
    element.SetNeedsStyleRecalc(
        kLocalStyleChange,
        StyleChangeReasonForTracing::FromAttribute(attribute_name));
  }
}

void StyleEngine::IdChangedForElement(const AtomicString& old_id,
                                      const AtomicString& new_id,
                                      Element& element) {
  if (ShouldSkipInvalidationFor(element))
    return;

  InvalidationLists invalidation_lists;
  const RuleFeatureSet& features = GetRuleFeatureSet();
  if (!old_id.IsEmpty())
    features.CollectInvalidationSetsForId(invalidation_lists, element, old_id);
  if (!new_id.IsEmpty())
    features.CollectInvalidationSetsForId(invalidation_lists, element, new_id);
  pending_invalidations_.ScheduleInvalidationSetsForNode(invalidation_lists,
                                                         element);
}

void StyleEngine::PseudoStateChangedForElement(
    CSSSelector::PseudoType pseudo_type,
    Element& element) {
  if (ShouldSkipInvalidationFor(element))
    return;

  InvalidationLists invalidation_lists;
  GetRuleFeatureSet().CollectInvalidationSetsForPseudoClass(
      invalidation_lists, element, pseudo_type);
  pending_invalidations_.ScheduleInvalidationSetsForNode(invalidation_lists,
                                                         element);
}

void StyleEngine::PartChangedForElement(Element& element) {
  if (ShouldSkipInvalidationFor(element))
    return;
  if (element.GetTreeScope() == document_)
    return;
  if (!GetRuleFeatureSet().InvalidatesParts())
    return;
  element.SetNeedsStyleRecalc(
      kLocalStyleChange,
      StyleChangeReasonForTracing::FromAttribute(html_names::kPartAttr));
}

void StyleEngine::ExportpartsChangedForElement(Element& element) {
  if (ShouldSkipInvalidationFor(element))
    return;
  if (!element.GetShadowRoot())
    return;

  InvalidationLists invalidation_lists;
  GetRuleFeatureSet().CollectPartInvalidationSet(invalidation_lists);
  pending_invalidations_.ScheduleInvalidationSetsForNode(invalidation_lists,
                                                         element);
}

void StyleEngine::ScheduleSiblingInvalidationsForElement(
    Element& element,
    ContainerNode& scheduling_parent,
    unsigned min_direct_adjacent) {
  DCHECK(min_direct_adjacent);

  InvalidationLists invalidation_lists;

  const RuleFeatureSet& features = GetRuleFeatureSet();

  if (element.HasID()) {
    features.CollectSiblingInvalidationSetForId(invalidation_lists, element,
                                                element.IdForStyleResolution(),
                                                min_direct_adjacent);
  }

  if (element.HasClass()) {
    const SpaceSplitString& class_names = element.ClassNames();
    for (wtf_size_t i = 0; i < class_names.size(); i++) {
      features.CollectSiblingInvalidationSetForClass(
          invalidation_lists, element, class_names[i], min_direct_adjacent);
    }
  }

  for (const Attribute& attribute : element.Attributes()) {
    features.CollectSiblingInvalidationSetForAttribute(
        invalidation_lists, element, attribute.GetName(), min_direct_adjacent);
  }

  features.CollectUniversalSiblingInvalidationSet(invalidation_lists,
                                                  min_direct_adjacent);

  pending_invalidations_.ScheduleSiblingInvalidationsAsDescendants(
      invalidation_lists, scheduling_parent);
}

void StyleEngine::ScheduleInvalidationsForInsertedSibling(
    Element* before_element,
    Element& inserted_element) {
  unsigned affected_siblings =
      inserted_element.parentNode()->ChildrenAffectedByIndirectAdjacentRules()
          ? SiblingInvalidationSet::kDirectAdjacentMax
          : MaxDirectAdjacentSelectors();

  ContainerNode* scheduling_parent =
      inserted_element.ParentElementOrShadowRoot();
  if (!scheduling_parent)
    return;

  ScheduleSiblingInvalidationsForElement(inserted_element, *scheduling_parent,
                                         1);

  for (unsigned i = 1; before_element && i <= affected_siblings;
       i++, before_element =
                ElementTraversal::PreviousSibling(*before_element)) {
    ScheduleSiblingInvalidationsForElement(*before_element, *scheduling_parent,
                                           i);
  }
}

void StyleEngine::ScheduleInvalidationsForRemovedSibling(
    Element* before_element,
    Element& removed_element,
    Element& after_element) {
  unsigned affected_siblings =
      after_element.parentNode()->ChildrenAffectedByIndirectAdjacentRules()
          ? SiblingInvalidationSet::kDirectAdjacentMax
          : MaxDirectAdjacentSelectors();

  ContainerNode* scheduling_parent = after_element.ParentElementOrShadowRoot();
  if (!scheduling_parent)
    return;

  ScheduleSiblingInvalidationsForElement(removed_element, *scheduling_parent,
                                         1);

  for (unsigned i = 1; before_element && i <= affected_siblings;
       i++, before_element =
                ElementTraversal::PreviousSibling(*before_element)) {
    ScheduleSiblingInvalidationsForElement(*before_element, *scheduling_parent,
                                           i);
  }
}

void StyleEngine::ScheduleNthPseudoInvalidations(ContainerNode& nth_parent) {
  InvalidationLists invalidation_lists;
  GetRuleFeatureSet().CollectNthInvalidationSet(invalidation_lists);
  pending_invalidations_.ScheduleInvalidationSetsForNode(invalidation_lists,
                                                         nth_parent);
}

void StyleEngine::ScheduleRuleSetInvalidationsForElement(
    Element& element,
    const HeapHashSet<Member<RuleSet>>& rule_sets) {
  AtomicString id;
  const SpaceSplitString* class_names = nullptr;

  if (element.HasID())
    id = element.IdForStyleResolution();
  if (element.HasClass())
    class_names = &element.ClassNames();

  InvalidationLists invalidation_lists;
  for (const auto& rule_set : rule_sets) {
    if (!id.IsNull()) {
      rule_set->Features().CollectInvalidationSetsForId(invalidation_lists,
                                                        element, id);
    }
    if (class_names) {
      wtf_size_t class_name_count = class_names->size();
      for (wtf_size_t i = 0; i < class_name_count; i++) {
        rule_set->Features().CollectInvalidationSetsForClass(
            invalidation_lists, element, (*class_names)[i]);
      }
    }
    for (const Attribute& attribute : element.Attributes()) {
      rule_set->Features().CollectInvalidationSetsForAttribute(
          invalidation_lists, element, attribute.GetName());
    }
  }
  pending_invalidations_.ScheduleInvalidationSetsForNode(invalidation_lists,
                                                         element);
}

void StyleEngine::ScheduleTypeRuleSetInvalidations(
    ContainerNode& node,
    const HeapHashSet<Member<RuleSet>>& rule_sets) {
  InvalidationLists invalidation_lists;
  for (const auto& rule_set : rule_sets) {
    rule_set->Features().CollectTypeRuleInvalidationSet(invalidation_lists,
                                                        node);
  }
  DCHECK(invalidation_lists.siblings.IsEmpty());
  pending_invalidations_.ScheduleInvalidationSetsForNode(invalidation_lists,
                                                         node);

  auto* shadow_root = DynamicTo<ShadowRoot>(node);
  if (!shadow_root)
    return;

  Element& host = shadow_root->host();
  if (host.NeedsStyleRecalc())
    return;

  for (auto& invalidation_set : invalidation_lists.descendants) {
    if (invalidation_set->InvalidatesTagName(host)) {
      host.SetNeedsStyleRecalc(kLocalStyleChange,
                               StyleChangeReasonForTracing::Create(
                                   style_change_reason::kStyleSheetChange));
      return;
    }
  }
}

void StyleEngine::ScheduleCustomElementInvalidations(
    HashSet<AtomicString> tag_names) {
  scoped_refptr<DescendantInvalidationSet> invalidation_set =
      DescendantInvalidationSet::Create();
  for (auto& tag_name : tag_names) {
    invalidation_set->AddTagName(tag_name);
  }
  invalidation_set->SetTreeBoundaryCrossing();
  InvalidationLists invalidation_lists;
  invalidation_lists.descendants.push_back(invalidation_set);
  pending_invalidations_.ScheduleInvalidationSetsForNode(invalidation_lists,
                                                         *document_);
}

void StyleEngine::InvalidateStyle() {
  StyleInvalidator style_invalidator(
      pending_invalidations_.GetPendingInvalidationMap());
  style_invalidator.Invalidate(GetDocument(),
                               style_invalidation_root_.RootElement());
  style_invalidation_root_.Clear();
}

void StyleEngine::InvalidateSlottedElements(HTMLSlotElement& slot) {
  for (auto& node : slot.FlattenedAssignedNodes()) {
    if (node->IsElementNode()) {
      node->SetNeedsStyleRecalc(kLocalStyleChange,
                                StyleChangeReasonForTracing::Create(
                                    style_change_reason::kStyleSheetChange));
    }
  }
}

void StyleEngine::ScheduleInvalidationsForRuleSets(
    TreeScope& tree_scope,
    const HeapHashSet<Member<RuleSet>>& rule_sets,
    InvalidationScope invalidation_scope) {
#if DCHECK_IS_ON()
  // Full scope recalcs should be handled while collecting the rule sets before
  // calling this method.
  for (auto rule_set : rule_sets)
    DCHECK(!rule_set->Features().NeedsFullRecalcForRuleSetInvalidation());
#endif  // DCHECK_IS_ON()

  TRACE_EVENT0("blink,blink_style",
               "StyleEngine::scheduleInvalidationsForRuleSets");

  ScheduleTypeRuleSetInvalidations(tree_scope.RootNode(), rule_sets);

  bool invalidate_slotted = false;
  if (auto* shadow_root = DynamicTo<ShadowRoot>(&tree_scope.RootNode())) {
    Element& host = shadow_root->host();
    ScheduleRuleSetInvalidationsForElement(host, rule_sets);
    if (host.GetStyleChangeType() == kSubtreeStyleChange)
      return;
    for (auto rule_set : rule_sets) {
      if (rule_set->HasSlottedRules()) {
        invalidate_slotted = true;
        break;
      }
    }
  }

  Node* stay_within = &tree_scope.RootNode();
  Element* element = ElementTraversal::FirstChild(*stay_within);
  while (element) {
    ScheduleRuleSetInvalidationsForElement(*element, rule_sets);
    auto* html_slot_element = DynamicTo<HTMLSlotElement>(element);
    if (html_slot_element && invalidate_slotted)
      InvalidateSlottedElements(*html_slot_element);

    if (invalidation_scope == kInvalidateAllScopes) {
      if (ShadowRoot* shadow_root = element->GetShadowRoot()) {
        ScheduleInvalidationsForRuleSets(*shadow_root, rule_sets,
                                         kInvalidateAllScopes);
      }
    }

    if (element->GetStyleChangeType() < kSubtreeStyleChange &&
        element->GetComputedStyle()) {
      element = ElementTraversal::Next(*element, stay_within);
    } else {
      element = ElementTraversal::NextSkippingChildren(*element, stay_within);
    }
  }
}

void StyleEngine::SetStatsEnabled(bool enabled) {
  if (!enabled) {
    style_resolver_stats_ = nullptr;
    return;
  }
  if (!style_resolver_stats_)
    style_resolver_stats_ = std::make_unique<StyleResolverStats>();
  else
    style_resolver_stats_->Reset();
}

void StyleEngine::SetPreferredStylesheetSetNameIfNotSet(const String& name) {
  DCHECK(!name.IsEmpty());
  if (!preferred_stylesheet_set_name_.IsEmpty())
    return;
  preferred_stylesheet_set_name_ = name;
  MarkDocumentDirty();
}

void StyleEngine::SetHttpDefaultStyle(const String& content) {
  if (!content.IsEmpty())
    SetPreferredStylesheetSetNameIfNotSet(content);
}

void StyleEngine::EnsureUAStyleForXrOverlay() {
  DCHECK(global_rule_set_);
  if (CSSDefaultStyleSheets::Instance().EnsureDefaultStyleSheetForXrOverlay()) {
    global_rule_set_->MarkDirty();
    UpdateActiveStyle();
  }
}

void StyleEngine::EnsureUAStyleForFullscreen() {
  DCHECK(global_rule_set_);
  if (global_rule_set_->HasFullscreenUAStyle())
    return;
  CSSDefaultStyleSheets::Instance().EnsureDefaultStyleSheetForFullscreen();
  global_rule_set_->MarkDirty();
  UpdateActiveStyle();
}

void StyleEngine::EnsureUAStyleForElement(const Element& element) {
  DCHECK(global_rule_set_);
  if (CSSDefaultStyleSheets::Instance().EnsureDefaultStyleSheetsForElement(
          element)) {
    global_rule_set_->MarkDirty();
    UpdateActiveStyle();
  }
}

void StyleEngine::EnsureUAStyleForPseudoElement(PseudoId pseudo_id) {
  DCHECK(global_rule_set_);
  if (CSSDefaultStyleSheets::Instance()
          .EnsureDefaultStyleSheetsForPseudoElement(pseudo_id)) {
    global_rule_set_->MarkDirty();
    UpdateActiveStyle();
  }
}

void StyleEngine::EnsureUAStyleForForcedColors() {
  DCHECK(global_rule_set_);
  if (CSSDefaultStyleSheets::Instance()
          .EnsureDefaultStyleSheetForForcedColors()) {
    global_rule_set_->MarkDirty();
    if (GetDocument().IsActive())
      UpdateActiveStyle();
  }
}

bool StyleEngine::HasRulesForId(const AtomicString& id) const {
  DCHECK(global_rule_set_);
  return global_rule_set_->GetRuleFeatureSet().HasSelectorForId(id);
}

void StyleEngine::InitialStyleChanged() {
  if (viewport_resolver_)
    viewport_resolver_->InitialStyleChanged();

  // Media queries may rely on the initial font size relative lengths which may
  // have changed.
  MediaQueryAffectingValueChanged(MediaValueChange::kOther);
  MarkViewportStyleDirty();
  MarkAllElementsForStyleRecalc(
      StyleChangeReasonForTracing::Create(style_change_reason::kSettings));
}

void StyleEngine::InitialViewportChanged() {
  if (viewport_resolver_)
    viewport_resolver_->InitialViewportChanged();
}

void StyleEngine::ViewportRulesChanged() {
  if (viewport_resolver_)
    viewport_resolver_->SetNeedsCollectRules();

  // When we remove an import link and re-insert it into the document, the
  // import Document and CSSStyleSheet pointers are persisted. That means the
  // comparison of active stylesheets is not able to figure out that the order
  // of the stylesheets have changed after insertion.
  //
  // This is also the case when we import the same document twice where the
  // last inserted document is inserted before the first one in dom order where
  // the last would take precedence.
  //
  // Fall back to re-add all sheets to the scoped resolver and recalculate style
  // for the whole document when we remove or insert an import document.
  if (ScopedStyleResolver* resolver = GetDocument().GetScopedStyleResolver()) {
    MarkDocumentDirty();
    resolver->SetNeedsAppendAllSheets();
    MarkAllElementsForStyleRecalc(StyleChangeReasonForTracing::Create(
        style_change_reason::kActiveStylesheetsUpdate));
  }
}

namespace {

enum RuleSetFlags {
  kFontFaceRules = 1 << 0,
  kKeyframesRules = 1 << 1,
  kFullRecalcRules = 1 << 2,
  kPropertyRules = 1 << 3,
  kScrollTimelineRules = 1 << 4,
  kCounterStyleRules = 1 << 5,
};

unsigned GetRuleSetFlags(const HeapHashSet<Member<RuleSet>> rule_sets) {
  unsigned flags = 0;
  for (auto& rule_set : rule_sets) {
    rule_set->CompactRulesIfNeeded();
    if (!rule_set->KeyframesRules().IsEmpty())
      flags |= kKeyframesRules;
    if (!rule_set->FontFaceRules().IsEmpty())
      flags |= kFontFaceRules;
    if (rule_set->NeedsFullRecalcForRuleSetInvalidation())
      flags |= kFullRecalcRules;
    if (!rule_set->PropertyRules().IsEmpty())
      flags |= kPropertyRules;
    if (!rule_set->CounterStyleRules().IsEmpty())
      flags |= kCounterStyleRules;
    if (!rule_set->ScrollTimelineRules().IsEmpty())
      flags |= kScrollTimelineRules;
  }
  return flags;
}

}  // namespace

void StyleEngine::InvalidateForRuleSetChanges(
    TreeScope& tree_scope,
    const HeapHashSet<Member<RuleSet>>& changed_rule_sets,
    unsigned changed_rule_flags,
    InvalidationScope invalidation_scope) {
  if (tree_scope.GetDocument().HasPendingForcedStyleRecalc())
    return;
  if (!tree_scope.GetDocument().documentElement())
    return;
  if (changed_rule_sets.IsEmpty())
    return;

  Element& invalidation_root =
      ScopedStyleResolver::InvalidationRootForTreeScope(tree_scope);
  if (invalidation_root.GetStyleChangeType() == kSubtreeStyleChange)
    return;

  if (changed_rule_flags & kFullRecalcRules) {
    invalidation_root.SetNeedsStyleRecalc(
        kSubtreeStyleChange,
        StyleChangeReasonForTracing::Create(
            style_change_reason::kActiveStylesheetsUpdate));
    return;
  }

  if (changed_rule_sets.IsEmpty())
    return;
  ScheduleInvalidationsForRuleSets(tree_scope, changed_rule_sets,
                                   invalidation_scope);
}

void StyleEngine::InvalidateInitialData() {
  initial_data_ = nullptr;
}

void StyleEngine::ApplyUserRuleSetChanges(
    const ActiveStyleSheetVector& old_style_sheets,
    const ActiveStyleSheetVector& new_style_sheets) {
  DCHECK(global_rule_set_);
  HeapHashSet<Member<RuleSet>> changed_rule_sets;

  ActiveSheetsChange change = CompareActiveStyleSheets(
      old_style_sheets, new_style_sheets, changed_rule_sets);

  if (change == kNoActiveSheetsChanged)
    return;

  // With rules added or removed, we need to re-aggregate rule meta data.
  global_rule_set_->MarkDirty();

  unsigned changed_rule_flags = GetRuleSetFlags(changed_rule_sets);
  bool has_rebuilt_font_face_cache = false;
  if (changed_rule_flags & kFontFaceRules) {
    if (ScopedStyleResolver* scoped_resolver =
            GetDocument().GetScopedStyleResolver()) {
      // User style and document scope author style shares the font cache. If
      // @font-face rules are added/removed from user stylesheets, we need to
      // reconstruct the font cache because @font-face rules from author style
      // need to be added to the cache after user rules.
      scoped_resolver->SetNeedsAppendAllSheets();
      MarkDocumentDirty();
    } else {
      has_rebuilt_font_face_cache =
          ClearFontFaceCacheAndAddUserFonts(new_style_sheets);
    }
  }

  if (changed_rule_flags & kKeyframesRules) {
    if (change == kActiveSheetsChanged)
      ClearKeyframeRules();

    for (auto* it = new_style_sheets.begin(); it != new_style_sheets.end();
         it++) {
      DCHECK(it->second);
      AddUserKeyframeRules(*it->second);
    }
    ScopedStyleResolver::KeyframesRulesAdded(GetDocument());
  }

  if (changed_rule_flags & kCounterStyleRules) {
    if (change == kActiveSheetsChanged && user_counter_style_map_)
      user_counter_style_map_->Dispose();

    for (auto* it = new_style_sheets.begin(); it != new_style_sheets.end();
         it++) {
      DCHECK(it->second);
      if (!it->second->CounterStyleRules().IsEmpty())
        EnsureUserCounterStyleMap().AddCounterStyles(*it->second);
    }

    MarkCounterStylesNeedUpdate();
  }

  if (changed_rule_flags & (kPropertyRules | kScrollTimelineRules)) {
    if (changed_rule_flags & kPropertyRules) {
      ClearPropertyRules();
      AddPropertyRulesFromSheets(new_style_sheets);
    }
    if (changed_rule_flags & kScrollTimelineRules) {
      ClearScrollTimelineRules();
      AddScrollTimelineRulesFromSheets(new_style_sheets);
    }

    // We just cleared all the rules, which includes any author rules. They
    // must be forcibly re-added.
    if (ScopedStyleResolver* scoped_resolver =
            GetDocument().GetScopedStyleResolver()) {
      scoped_resolver->SetNeedsAppendAllSheets();
      MarkDocumentDirty();
    }
  }

  if ((changed_rule_flags & kFontFaceRules) || has_rebuilt_font_face_cache) {
    GetFontSelector()->FontFaceInvalidated(
        FontInvalidationReason::kGeneralInvalidation);
  }

  InvalidateForRuleSetChanges(GetDocument(), changed_rule_sets,
                              changed_rule_flags, kInvalidateAllScopes);
}

void StyleEngine::ApplyRuleSetChanges(
    TreeScope& tree_scope,
    const ActiveStyleSheetVector& old_style_sheets,
    const ActiveStyleSheetVector& new_style_sheets) {
  DCHECK(global_rule_set_);
  HeapHashSet<Member<RuleSet>> changed_rule_sets;

  ActiveSheetsChange change = CompareActiveStyleSheets(
      old_style_sheets, new_style_sheets, changed_rule_sets);

  unsigned changed_rule_flags = GetRuleSetFlags(changed_rule_sets);

  bool rebuild_font_face_cache = change == kActiveSheetsChanged &&
                                 (changed_rule_flags & kFontFaceRules) &&
                                 tree_scope.RootNode().IsDocumentNode();
  bool rebuild_at_property_registry = false;
  bool rebuild_at_scroll_timeline_map = false;
  ScopedStyleResolver* scoped_resolver = tree_scope.GetScopedStyleResolver();
  if (scoped_resolver && scoped_resolver->NeedsAppendAllSheets()) {
    rebuild_font_face_cache = true;
    rebuild_at_property_registry = true;
    rebuild_at_scroll_timeline_map = true;
    change = kActiveSheetsChanged;
  }

  if (change == kNoActiveSheetsChanged)
    return;

  // With rules added or removed, we need to re-aggregate rule meta data.
  global_rule_set_->MarkDirty();

  if (changed_rule_flags & kKeyframesRules)
    ScopedStyleResolver::KeyframesRulesAdded(tree_scope);

  if (changed_rule_flags & kCounterStyleRules)
    MarkCounterStylesNeedUpdate();

  if ((changed_rule_flags & kPropertyRules) || rebuild_at_property_registry) {
    // @property rules are (for now) ignored in shadow trees, per spec.
    // https://drafts.css-houdini.org/css-properties-values-api-1/#at-property-rule
    if (tree_scope.RootNode().IsDocumentNode()) {
      ClearPropertyRules();
      AddPropertyRulesFromSheets(active_user_style_sheets_);
      AddPropertyRulesFromSheets(new_style_sheets);
    }
  }

  if ((changed_rule_flags & kScrollTimelineRules) ||
      rebuild_at_scroll_timeline_map) {
    // @scroll-timeline rules are currently not allowed in shadow trees.
    // https://drafts.csswg.org/scroll-animations-1/#scroll-timeline-at-rule
    if (tree_scope.RootNode().IsDocumentNode()) {
      ClearScrollTimelineRules();
      AddScrollTimelineRulesFromSheets(active_user_style_sheets_);
      AddScrollTimelineRulesFromSheets(new_style_sheets);
    }
  }

  bool has_rebuilt_font_face_cache = false;
  if (rebuild_font_face_cache) {
    has_rebuilt_font_face_cache =
        ClearFontFaceCacheAndAddUserFonts(active_user_style_sheets_);
  }

  unsigned append_start_index = 0;
  if (scoped_resolver) {
    // - If all sheets were removed, we remove the ScopedStyleResolver.
    // - If new sheets were appended to existing ones, start appending after the
    //   common prefix.
    // - For other diffs, reset author style and re-add all sheets for the
    //   TreeScope.
    if (new_style_sheets.IsEmpty())
      ResetAuthorStyle(tree_scope);
    else if (change == kActiveSheetsAppended)
      append_start_index = old_style_sheets.size();
    else
      scoped_resolver->ResetStyle();
  }

  if (!new_style_sheets.IsEmpty()) {
    tree_scope.EnsureScopedStyleResolver().AppendActiveStyleSheets(
        append_start_index, new_style_sheets);
  }

  if (tree_scope.RootNode().IsDocumentNode()) {
    if ((changed_rule_flags & kFontFaceRules) || has_rebuilt_font_face_cache) {
      GetFontSelector()->FontFaceInvalidated(
          FontInvalidationReason::kGeneralInvalidation);
    }
  }

  InvalidateForRuleSetChanges(tree_scope, changed_rule_sets, changed_rule_flags,
                              kInvalidateCurrentScope);
}

void StyleEngine::LoadVisionDeficiencyFilter() {
  VisionDeficiency old_vision_deficiency = vision_deficiency_;
  vision_deficiency_ = GetDocument().GetPage()->GetVisionDeficiency();
  if (vision_deficiency_ == old_vision_deficiency)
    return;

  if (vision_deficiency_ == VisionDeficiency::kNoVisionDeficiency) {
    vision_deficiency_filter_ = nullptr;
  } else {
    AtomicString url = CreateVisionDeficiencyFilterUrl(vision_deficiency_);
    cssvalue::CSSURIValue css_uri_value(url);
    SVGResource* svg_resource = css_uri_value.EnsureResourceReference();
    // Note: The fact that we're using data: URLs here is an
    // implementation detail. Emulating vision deficiencies should still
    // work even if the Document's Content-Security-Policy disallows
    // data: URLs.
    svg_resource->LoadWithoutCSP(GetDocument());
    vision_deficiency_filter_ =
        MakeGarbageCollected<ReferenceFilterOperation>(url, svg_resource);
  }
}

void StyleEngine::VisionDeficiencyChanged() {
  MarkViewportStyleDirty();
}

void StyleEngine::ApplyVisionDeficiencyStyle(
    scoped_refptr<ComputedStyle> layout_view_style) {
  LoadVisionDeficiencyFilter();
  if (vision_deficiency_filter_) {
    FilterOperations ops;
    ops.Operations().push_back(vision_deficiency_filter_);
    layout_view_style->SetFilter(ops);
  }
}

const MediaQueryEvaluator& StyleEngine::EnsureMediaQueryEvaluator() {
  if (!media_query_evaluator_) {
    if (GetDocument().GetFrame()) {
      media_query_evaluator_ =
          MakeGarbageCollected<MediaQueryEvaluator>(GetDocument().GetFrame());
    } else {
      media_query_evaluator_ = MakeGarbageCollected<MediaQueryEvaluator>("all");
    }
  }
  return *media_query_evaluator_;
}

bool StyleEngine::MediaQueryAffectedByViewportChange() {
  DCHECK(global_rule_set_);
  return EnsureMediaQueryEvaluator().DidResultsChange(
      global_rule_set_->GetRuleFeatureSet()
          .ViewportDependentMediaQueryResults());
}

bool StyleEngine::MediaQueryAffectedByDeviceChange() {
  DCHECK(global_rule_set_);
  return EnsureMediaQueryEvaluator().DidResultsChange(
      global_rule_set_->GetRuleFeatureSet().DeviceDependentMediaQueryResults());
}

bool StyleEngine::UpdateRemUnits(const ComputedStyle* old_root_style,
                                 const ComputedStyle* new_root_style) {
  if (!new_root_style || !UsesRemUnits())
    return false;
  if (!old_root_style || old_root_style->SpecifiedFontSize() !=
                             new_root_style->SpecifiedFontSize()) {
    // Resolved rem units are stored in the matched properties cache so we need
    // to make sure to invalidate the cache if the documentElement font size
    // changes.
    GetStyleResolver().InvalidateMatchedPropertiesCache();
    return true;
  }
  return false;
}

void StyleEngine::PropertyRegistryChanged() {
  // TODO(timloh): Invalidate only elements with this custom property set
  MarkAllElementsForStyleRecalc(StyleChangeReasonForTracing::Create(
      style_change_reason::kPropertyRegistration));
  if (resolver_)
    resolver_->InvalidateMatchedPropertiesCache();
  InvalidateInitialData();
}

void StyleEngine::EnvironmentVariableChanged() {
  MarkAllElementsForStyleRecalc(StyleChangeReasonForTracing::Create(
      style_change_reason::kPropertyRegistration));
  if (resolver_)
    resolver_->InvalidateMatchedPropertiesCache();
}

void StyleEngine::ScrollTimelinesChanged() {
  MarkAllElementsForStyleRecalc(StyleChangeReasonForTracing::Create(
      style_change_reason::kScrollTimeline));
  // We currently rely on marking at least one element for recalc in order
  // to clean the timelines_need_update_ flag. (Otherwise the timelines
  // will just remain dirty). Hence, if we in the future remove the call
  // to mark elements for recalc, we would need to call
  // ScheduleLayoutTreeUpdateIfNeeded to ensure that we reach UpdateTimelines.
  timelines_need_update_ = true;
}

void StyleEngine::MarkForWhitespaceReattachment() {
  DCHECK(GetDocument().InStyleRecalc());
  for (auto element : whitespace_reattach_set_) {
    if (element->NeedsReattachLayoutTree() || !element->GetLayoutObject())
      continue;
      // This element might be located inside a display locked subtree, so we
      // might mark it for ReattachLayoutTree later on instead.
    if (Element* locked_ancestor =
            DisplayLockUtilities::NearestLockedInclusiveAncestor(*element)) {
      locked_ancestor->GetDisplayLockContext()->AddToWhitespaceReattachSet(
          *element);
      continue;
    }
    DCHECK(!element->NeedsStyleRecalc());
    DCHECK(!element->ChildNeedsStyleRecalc());
    if (Node* first_child = LayoutTreeBuilderTraversal::FirstChild(*element))
      first_child->MarkAncestorsWithChildNeedsReattachLayoutTree();
  }
}

void StyleEngine::NodeWillBeRemoved(Node& node) {
  if (auto* element = DynamicTo<Element>(node)) {
    pending_invalidations_.RescheduleSiblingInvalidationsAsDescendants(
        *element);
  }

  // Mark closest ancestor with with LayoutObject to have all whitespace
  // children being considered for re-attachment during the layout tree build.

  LayoutObject* layout_object = node.GetLayoutObject();
  // The removed node does not have a layout object. No sibling whitespace nodes
  // will change rendering.
  if (!layout_object)
    return;
  // Floating or out-of-flow elements do not affect whitespace siblings.
  if (!layout_object->AffectsWhitespaceSiblings())
    return;
  layout_object = layout_object->Parent();
  while (layout_object->IsAnonymous())
    layout_object = layout_object->Parent();
  DCHECK(layout_object);
  DCHECK(layout_object->GetNode());
  if (auto* layout_object_element =
          DynamicTo<Element>(layout_object->GetNode())) {
    whitespace_reattach_set_.insert(layout_object_element);
    GetDocument().ScheduleLayoutTreeUpdateIfNeeded();
  }
}

void StyleEngine::ChildrenRemoved(ContainerNode& parent) {
  if (!parent.isConnected())
    return;
  DCHECK(!layout_tree_rebuild_root_.GetRootNode());
  if (InDOMRemoval()) {
    // This is necessary for nested removals. There are elements which
    // removes parts of its UA shadow DOM as part of being removed which means
    // we do a removal from within another removal where isConnected() is not
    // completely up to date which would confuse this code. Also, the removal
    // doesn't have to be in the same subtree as the outer removal. For instance
    // for the ListAttributeTargetChanged mentioned below.
    //
    // Instead we fall back to use the document root as the traversal root for
    // all traversal roots.
    //
    // TODO(crbug.com/882869): MediaControlLoadingPanelElement
    // TODO(crbug.com/888448): TextFieldInputType::ListAttributeTargetChanged
    if (style_invalidation_root_.GetRootNode())
      UpdateStyleInvalidationRoot(nullptr, nullptr);
    if (style_recalc_root_.GetRootNode())
      UpdateStyleRecalcRoot(nullptr, nullptr);
    return;
  }
  style_invalidation_root_.SubtreeModified(parent);
  style_recalc_root_.SubtreeModified(parent);
}

void StyleEngine::CollectMatchingUserRules(
    ElementRuleCollector& collector) const {
  for (unsigned i = 0; i < active_user_style_sheets_.size(); ++i) {
    DCHECK(active_user_style_sheets_[i].second);
    collector.CollectMatchingRules(
        MatchRequest(active_user_style_sheets_[i].second, nullptr,
                     active_user_style_sheets_[i].first, i));
  }
}

void StyleEngine::ClearPropertyRules() {
  PropertyRegistration::RemoveDeclaredProperties(GetDocument());
}

void StyleEngine::ClearScrollTimelineRules() {
  scroll_timeline_rule_map_.clear();
  ScrollTimelinesChanged();
}

void StyleEngine::AddPropertyRulesFromSheets(
    const ActiveStyleSheetVector& sheets) {
  for (const ActiveStyleSheet& active_sheet : sheets) {
    if (RuleSet* rule_set = active_sheet.second)
      AddPropertyRules(*rule_set);
  }
}

void StyleEngine::AddScrollTimelineRulesFromSheets(
    const ActiveStyleSheetVector& sheets) {
  for (const ActiveStyleSheet& active_sheet : sheets) {
    if (RuleSet* rule_set = active_sheet.second)
      AddScrollTimelineRules(*rule_set);
  }
}

bool StyleEngine::AddUserFontFaceRules(const RuleSet& rule_set) {
  if (!font_selector_)
    return false;

  const HeapVector<Member<StyleRuleFontFace>> font_face_rules =
      rule_set.FontFaceRules();
  for (auto& font_face_rule : font_face_rules) {
    if (FontFace* font_face = FontFace::Create(document_, font_face_rule))
      font_selector_->GetFontFaceCache()->Add(font_face_rule, font_face);
  }
  if (resolver_ && font_face_rules.size())
    resolver_->InvalidateMatchedPropertiesCache();
  return font_face_rules.size();
}

void StyleEngine::AddUserKeyframeRules(const RuleSet& rule_set) {
  const HeapVector<Member<StyleRuleKeyframes>> keyframes_rules =
      rule_set.KeyframesRules();
  for (unsigned i = 0; i < keyframes_rules.size(); ++i)
    AddUserKeyframeStyle(keyframes_rules[i]);
}

void StyleEngine::AddUserKeyframeStyle(StyleRuleKeyframes* rule) {
  AtomicString animation_name(rule->GetName());

  if (rule->IsVendorPrefixed()) {
    KeyframesRuleMap::iterator it = keyframes_rule_map_.find(animation_name);
    if (it == keyframes_rule_map_.end())
      keyframes_rule_map_.Set(animation_name, rule);
    else if (it->value->IsVendorPrefixed())
      keyframes_rule_map_.Set(animation_name, rule);
  } else {
    keyframes_rule_map_.Set(animation_name, rule);
  }
}

void StyleEngine::AddPropertyRules(const RuleSet& rule_set) {
  const HeapVector<Member<StyleRuleProperty>> property_rules =
      rule_set.PropertyRules();
  for (unsigned i = 0; i < property_rules.size(); ++i) {
    StyleRuleProperty* rule = property_rules[i];
    AtomicString name(rule->GetName());
    PropertyRegistration::DeclareProperty(GetDocument(), name, *rule);
  }
}

void StyleEngine::AddScrollTimelineRules(const RuleSet& rule_set) {
  const HeapVector<Member<StyleRuleScrollTimeline>> scroll_timeline_rules =
      rule_set.ScrollTimelineRules();
  if (scroll_timeline_rules.IsEmpty())
    return;
  for (const auto& rule : scroll_timeline_rules)
    scroll_timeline_rule_map_.Set(rule->GetName(), rule);
  ScrollTimelinesChanged();
}

StyleRuleKeyframes* StyleEngine::KeyframeStylesForAnimation(
    const AtomicString& animation_name) {
  if (keyframes_rule_map_.IsEmpty())
    return nullptr;

  KeyframesRuleMap::iterator it = keyframes_rule_map_.find(animation_name);
  if (it == keyframes_rule_map_.end())
    return nullptr;

  return it->value.Get();
}

void StyleEngine::UpdateTimelines() {
  if (!timelines_need_update_)
    return;
  timelines_need_update_ = false;

  HeapHashMap<AtomicString, Member<CSSScrollTimeline>> timelines;

  for (const auto& it : scroll_timeline_rule_map_) {
    const AtomicString& name = it.key;

    CSSScrollTimeline::Options options(GetDocument(), *it.value);

    // Check if we can re-use existing timeline.
    CSSScrollTimeline* existing_timeline = FindScrollTimeline(name);
    if (existing_timeline && existing_timeline->Matches(options)) {
      timelines.Set(name, existing_timeline);
      continue;
    }

    // Create a new timeline.
    auto* timeline = MakeGarbageCollected<CSSScrollTimeline>(
        &GetDocument(), std::move(options));
    // It is not allowed for a style update to create timelines that
    // needs timing updates (i.e.
    // AnimationTimeline::NeedsAnimationTimingUpdate() must return false).
    // Servicing animations after creation preserves this invariant by ensuring
    // the last-update time of the timeline is equal to the current time.
    timeline->ServiceAnimations(kTimingUpdateOnDemand);
    timelines.Set(name, timeline);
  }

  std::swap(scroll_timeline_map_, timelines);
}

CSSScrollTimeline* StyleEngine::FindScrollTimeline(const AtomicString& name) {
  DCHECK(!timelines_need_update_);
  return scroll_timeline_map_.DeprecatedAtOrEmptyValue(name);
}

void StyleEngine::ScrollTimelineInvalidated(CSSScrollTimeline& timeline) {
  timelines_need_update_ = true;
  timeline.InvalidateEffectTargetStyle();
}

DocumentStyleEnvironmentVariables& StyleEngine::EnsureEnvironmentVariables() {
  if (!environment_variables_) {
    environment_variables_ = DocumentStyleEnvironmentVariables::Create(
        StyleEnvironmentVariables::GetRootInstance(), *document_);
  }
  return *environment_variables_.get();
}

scoped_refptr<StyleInitialData> StyleEngine::MaybeCreateAndGetInitialData() {
  if (initial_data_)
    return initial_data_;
  if (const PropertyRegistry* registry = document_->GetPropertyRegistry()) {
    if (!registry->IsEmpty())
      initial_data_ = StyleInitialData::Create(*registry);
  }
  return initial_data_;
}

void StyleEngine::UpdateStyleAndLayoutTreeForContainer(
    Element& container,
    const LogicalSize& logical_size,
    LogicalAxes contained_axes) {
  DCHECK(!style_recalc_root_.GetRootNode());
  DCHECK(!container.NeedsStyleRecalc());
  DCHECK(!in_container_query_style_recalc_);

  base::AutoReset<bool> cq_recalc(&in_container_query_style_recalc_, true);

  DCHECK(container.GetLayoutObject()) << "Containers must have a LayoutObject";
  const ComputedStyle& style = container.GetLayoutObject()->StyleRef();
  DCHECK(style.IsContainerForContainerQueries());
  WritingMode writing_mode = style.GetWritingMode();
  PhysicalSize physical_size = AdjustForAbsoluteZoom::AdjustPhysicalSize(
      ToPhysicalSize(logical_size, writing_mode), style);
  PhysicalAxes physical_axes = ToPhysicalAxes(contained_axes, writing_mode);

  StyleRecalcChange change;

  auto* evaluator = container.GetContainerQueryEvaluator();
  DCHECK(evaluator);

  switch (evaluator->ContainerChanged(physical_size, physical_axes)) {
    case ContainerQueryEvaluator::Change::kNone:
      return;
    case ContainerQueryEvaluator::Change::kNearestContainer:
      change = change.ForceRecalcContainer();
      break;
    case ContainerQueryEvaluator::Change::kDescendantContainers:
      change = change.ForceRecalcDescendantContainers();
      break;
  }

  // The container node must not need recalc at this point.
  DCHECK(!StyleRecalcChange().ShouldRecalcStyleFor(container));

  // If the container itself depends on an outer container, then its
  // DependsOnContainerQueries flag will be set, and we would recalc its
  // style (due to ForceRecalcContainer/ForceRecalcDescendantContainers).
  // This is not necessary, hence we suppress recalc for this element.
  change = change.SuppressRecalc();

  NthIndexCache nth_index_cache(GetDocument());
  style_recalc_root_.Update(nullptr, &container);

  // No need to initialize container for the StyleRecalcContext with
  // FromAncestors because style will not be recalculated for "container, and
  // Element::RecalcStyle for the "container" will initialize StyleRecalcContext
  // with itself for its children.
  RecalcStyle(change, StyleRecalcContext());

  // Nodes are marked for whitespace reattachment for DOM removal only. This set
  // should have been cleared before layout.
  DCHECK(!NeedsWhitespaceReattachment());

  if (container.ChildNeedsReattachLayoutTree()) {
    DCHECK(layout_tree_rebuild_root_.GetRootNode());
    if (layout_tree_rebuild_root_.GetRootNode()->IsDocumentNode()) {
      // Avoid traversing from outside the container root. We know none of the
      // elements outside the subtree should be marked dirty in this pass, but
      // we may have fallen back to the document root.
      layout_tree_rebuild_root_.Clear();
      layout_tree_rebuild_root_.Update(nullptr, &container);
    } else {
      DCHECK(FlatTreeTraversal::Contains(
          container, *layout_tree_rebuild_root_.GetRootNode()));
    }
    RebuildLayoutTree();
  }

  if (container == GetDocument().documentElement()) {
    // If the container is the root element, there may be body styles which have
    // changed as a result of the new container query evaluation, and if
    // properties propagated from body changed, we need to update the viewport
    // styles.
    GetStyleResolver().PropagateStyleToViewport();
  }
  GetDocument().GetLayoutView()->UpdateMarkersAndCountersAfterStyleChange(
      container.GetLayoutObject());
}

void StyleEngine::RecalcStyle(StyleRecalcChange change,
                              const StyleRecalcContext& style_recalc_context) {
  DCHECK(GetDocument().documentElement());
  HasMatchedCacheScope has_matched_cache_scope(&GetDocument());
  Element& root_element = style_recalc_root_.RootElement();
  Element* parent = FlatTreeTraversal::ParentElement(root_element);

  SelectorFilterRootScope filter_scope(parent);
  root_element.RecalcStyle(change, style_recalc_context);

  for (ContainerNode* ancestor = root_element.GetStyleRecalcParent(); ancestor;
       ancestor = ancestor->GetStyleRecalcParent()) {
    if (auto* ancestor_element = DynamicTo<Element>(ancestor))
      ancestor_element->RecalcStyleForTraversalRootAncestor();
    ancestor->ClearChildNeedsStyleRecalc();
  }
  style_recalc_root_.Clear();
  if (!parent || IsA<HTMLBodyElement>(root_element))
    PropagateWritingModeAndDirectionToHTMLRoot();
}

void StyleEngine::RecalcStyle() {
  Element& root_element = style_recalc_root_.RootElement();

  auto style_recalc_context =
      RuntimeEnabledFeatures::CSSContainerQueriesEnabled()
          ? StyleRecalcContext::FromAncestors(root_element)
          : StyleRecalcContext();

  RecalcStyle({}, style_recalc_context);
}

void StyleEngine::ClearEnsuredDescendantStyles(Element& root) {
  Node* current = &root;
  while (current) {
    if (auto* element = DynamicTo<Element>(current)) {
      if (const auto* style = element->GetComputedStyle()) {
        DCHECK(style->IsEnsuredOutsideFlatTree());
        element->SetComputedStyle(nullptr);
        element->ClearNeedsStyleRecalc();
        element->ClearChildNeedsStyleRecalc();
        current = FlatTreeTraversal::Next(*current, &root);
        continue;
      }
    }
    current = FlatTreeTraversal::NextSkippingChildren(*current, &root);
  }
}

void StyleEngine::RebuildLayoutTree() {
  DCHECK(GetDocument().documentElement());
  DCHECK(!InRebuildLayoutTree());
  base::AutoReset<bool> rebuild_scope(&in_layout_tree_rebuild_, true);

  // We need a root scope here in case we recalc style for ::first-letter
  // elements as part of UpdateFirstLetterPseudoElement.
  SelectorFilterRootScope filter_scope(nullptr);

  Element& root_element = layout_tree_rebuild_root_.RootElement();
  {
    WhitespaceAttacher whitespace_attacher;
    root_element.RebuildLayoutTree(whitespace_attacher);
  }

  for (ContainerNode* ancestor = root_element.GetReattachParent(); ancestor;
       ancestor = ancestor->GetReattachParent()) {
    if (auto* ancestor_element = DynamicTo<Element>(ancestor))
      ancestor_element->RebuildLayoutTreeForTraversalRootAncestor();
    ancestor->ClearChildNeedsStyleRecalc();
    ancestor->ClearChildNeedsReattachLayoutTree();
  }
  layout_tree_rebuild_root_.Clear();

  if (IsA<HTMLHtmlElement>(root_element) || IsA<HTMLBodyElement>(root_element))
    PropagateWritingModeAndDirectionToHTMLRoot();
}

void StyleEngine::UpdateStyleAndLayoutTree() {
  // All of layout tree dirtiness and rebuilding needs to happen on a stable
  // flat tree. We have an invariant that all of that happens in this method
  // as a result of style recalc and the following layout tree rebuild.
  //
  // NeedsReattachLayoutTree() marks dirty up the flat tree ancestors. Re-
  // slotting on a dirty tree could break ancestor chains and fail to update the
  // tree properly.
  DCHECK(!NeedsLayoutTreeRebuild());

  UpdateViewportStyle();

  if (Element* document_element = GetDocument().documentElement()) {
    NthIndexCache nth_index_cache(GetDocument());
    if (NeedsStyleRecalc()) {
      TRACE_EVENT0("blink,blink_style", "Document::recalcStyle");
      SCOPED_BLINK_UMA_HISTOGRAM_TIMER_HIGHRES("Style.RecalcTime");
      Element* viewport_defining = GetDocument().ViewportDefiningElement();
      RecalcStyle();
      if (viewport_defining != GetDocument().ViewportDefiningElement())
        ViewportDefiningElementDidChange();
    }
    MarkForWhitespaceReattachment();
    if (NeedsLayoutTreeRebuild()) {
      TRACE_EVENT0("blink,blink_style", "Document::rebuildLayoutTree");
      SCOPED_BLINK_UMA_HISTOGRAM_TIMER_HIGHRES("Style.RebuildLayoutTreeTime");
      RebuildLayoutTree();
    }
  } else {
    style_recalc_root_.Clear();
  }
  ClearWhitespaceReattachSet();
  UpdateColorSchemeBackground();
  GetStyleResolver().PropagateStyleToViewport();
}

void StyleEngine::ViewportDefiningElementDidChange() {
  // Guarded by if-test in UpdateStyleAndLayoutTree().
  DCHECK(GetDocument().documentElement());
  if (GetDocument().documentElement()->NeedsReattachLayoutTree())
    return;
  HTMLBodyElement* body = GetDocument().FirstBodyElement();
  if (!body || body->NeedsReattachLayoutTree())
    return;
  LayoutObject* layout_object = body->GetLayoutObject();
  if (layout_object && layout_object->IsLayoutBlock()) {
    // When the overflow style for documentElement changes to or from visible,
    // it changes whether the body element's box should have scrollable overflow
    // on its own box or propagated to the viewport. If the body style did not
    // need a recalc, this will not be updated as its done as part of setting
    // ComputedStyle on the LayoutObject. Force a SetStyle for body when the
    // ViewportDefiningElement changes in order to trigger an update of
    // IsScrollContainer() and the PaintLayer in StyleDidChange().
    layout_object->SetStyle(ComputedStyle::Clone(*layout_object->Style()));
  }
}

void StyleEngine::UpdateStyleInvalidationRoot(ContainerNode* ancestor,
                                              Node* dirty_node) {
  if (GetDocument().IsActive()) {
    if (InDOMRemoval()) {
      ancestor = nullptr;
      dirty_node = document_;
    }
    style_invalidation_root_.Update(ancestor, dirty_node);
  }
}

void StyleEngine::UpdateStyleRecalcRoot(ContainerNode* ancestor,
                                        Node* dirty_node) {
  if (!GetDocument().IsActive())
    return;
  // We have at least one instance where we mark style dirty from style recalc
  // (from LayoutTextControl::StyleDidChange()). That means we are in the
  // process of traversing down the tree from the recalc root. Any updates to
  // the style recalc root will be cleared after the style recalc traversal
  // finishes and updating it may just trigger sanity DCHECKs in
  // StyleTraversalRoot. Just return here instead.
  if (GetDocument().InStyleRecalc()) {
    DCHECK(allow_mark_style_dirty_from_recalc_);
    return;
  }
  DCHECK(!InRebuildLayoutTree());
  if (InDOMRemoval()) {
    ancestor = nullptr;
    dirty_node = document_;
  }
  style_recalc_root_.Update(ancestor, dirty_node);
}

void StyleEngine::UpdateLayoutTreeRebuildRoot(ContainerNode* ancestor,
                                              Node* dirty_node) {
  DCHECK(!InDOMRemoval());
  if (!GetDocument().IsActive())
    return;
  if (InRebuildLayoutTree()) {
    DCHECK(allow_mark_for_reattach_from_rebuild_layout_tree_);
    return;
  }
  DCHECK(GetDocument().InStyleRecalc());
  DCHECK(dirty_node);
  if (!ancestor && !dirty_node->NeedsReattachLayoutTree() &&
      !dirty_node->ChildNeedsReattachLayoutTree()) {
    // The StyleTraversalRoot requires the root node to be dirty or child-dirty.
    // When we mark for whitespace re-attachment, we only mark the ancestor
    // chain. Use the parent as the dirty node if the dirty_node is not dirty.
    dirty_node = dirty_node->GetReattachParent();
    DCHECK(dirty_node && dirty_node->ChildNeedsReattachLayoutTree());
  }
  layout_tree_rebuild_root_.Update(ancestor, dirty_node);
}

bool StyleEngine::SupportsDarkColorScheme() {
  if (!meta_color_scheme_)
    return false;
  bool has_light = false;
  bool has_dark = false;
  if (const auto* scheme_list = DynamicTo<CSSValueList>(*meta_color_scheme_)) {
    for (auto& item : *scheme_list) {
      if (const auto* ident = DynamicTo<CSSIdentifierValue>(*item)) {
        if (ident->GetValueID() == CSSValueID::kDark)
          has_dark = true;
        else if (ident->GetValueID() == CSSValueID::kLight)
          has_light = true;
      }
    }
  }
  return has_dark &&
         (!has_light ||
          preferred_color_scheme_ == mojom::blink::PreferredColorScheme::kDark);
}

void StyleEngine::UpdateColorScheme() {
  auto* settings = GetDocument().GetSettings();
  auto* web_theme_engine =
      Platform::Current() ? Platform::Current()->ThemeEngine() : nullptr;
  if (!settings || !web_theme_engine)
    return;

  ForcedColors old_forced_colors = forced_colors_;
  forced_colors_ = web_theme_engine->GetForcedColors();

  mojom::blink::PreferredColorScheme old_preferred_color_scheme =
      preferred_color_scheme_;
  preferred_color_scheme_ = settings->GetPreferredColorScheme();
  if (const auto* overrides =
          GetDocument().GetPage()->GetMediaFeatureOverrides()) {
    MediaQueryExpValue value = overrides->GetOverride("prefers-color-scheme");
    if (value.IsValid())
      preferred_color_scheme_ = CSSValueIDToPreferredColorScheme(value.id);
  }
  if (!SupportsDarkColorScheme() && settings->GetForceDarkModeEnabled()) {
    // Make sure we don't match (prefers-color-scheme: dark) when forced
    // darkening is enabled.
    preferred_color_scheme_ = mojom::blink::PreferredColorScheme::kLight;
  }
  if (GetDocument().Printing())
    preferred_color_scheme_ = mojom::blink::PreferredColorScheme::kLight;

  if (forced_colors_ != old_forced_colors ||
      preferred_color_scheme_ != old_preferred_color_scheme) {
    PlatformColorsChanged();
  }

  UpdateColorSchemeMetrics();
}

void StyleEngine::UpdateColorSchemeMetrics() {
  auto* settings = GetDocument().GetSettings();
  if (settings->GetForceDarkModeEnabled())
    UseCounter::Count(GetDocument(), WebFeature::kForcedDarkMode);

  // True if the preferred color scheme will match dark.
  if (preferred_color_scheme_ == mojom::blink::PreferredColorScheme::kDark)
    UseCounter::Count(GetDocument(), WebFeature::kPreferredColorSchemeDark);

  // This is equal to kPreferredColorSchemeDark in most cases, but can differ
  // with forced dark mode. With the system in dark mode and forced dark mode
  // enabled, the preferred color scheme can be light while the setting is dark.
  if (settings->GetPreferredColorScheme() ==
      mojom::blink::PreferredColorScheme::kDark) {
    UseCounter::Count(GetDocument(),
                      WebFeature::kPreferredColorSchemeDarkSetting);
  }

  // Record kColorSchemeDarkSupportedOnRoot if the meta color-scheme contains
  // dark (though dark may not be used). This metric is also recorded in
  // longhands_custom.cc (see: ColorScheme::ApplyValue) if the root style
  // color-scheme contains dark.
  if (meta_color_scheme_) {
    const auto* scheme_list = DynamicTo<CSSValueList>(*meta_color_scheme_);
    if (scheme_list) {
      for (auto& item : *scheme_list) {
        const auto* ident = DynamicTo<CSSIdentifierValue>(*item);
        if (ident && ident->GetValueID() == CSSValueID::kDark) {
          UseCounter::Count(GetDocument(),
                            WebFeature::kColorSchemeDarkSupportedOnRoot);
        }
      }
    }
  }
}

void StyleEngine::ColorSchemeChanged() {
  UpdateColorScheme();
}

void StyleEngine::SetColorSchemeFromMeta(const CSSValue* color_scheme) {
  meta_color_scheme_ = color_scheme;
  DCHECK(GetDocument().documentElement());
  GetDocument().documentElement()->SetNeedsStyleRecalc(
      kLocalStyleChange, StyleChangeReasonForTracing::Create(
                             style_change_reason::kPlatformColorChange));
  UpdateColorScheme();
  UpdateColorSchemeBackground();
}

void StyleEngine::UpdateColorSchemeBackground(bool color_scheme_changed) {
  LocalFrameView* view = GetDocument().View();
  if (!view)
    return;

  LocalFrameView::UseColorAdjustBackground use_color_adjust_background =
      LocalFrameView::UseColorAdjustBackground::kNo;

  if (forced_colors_ != ForcedColors::kNone) {
    if (GetDocument().IsInMainFrame()) {
      use_color_adjust_background =
          LocalFrameView::UseColorAdjustBackground::kIfBaseNotTransparent;
    }
  } else {
    // Find out if we should use a canvas color that is different from the
    // view's base background color in order to match the root element color-
    // scheme. See spec:
    // https://drafts.csswg.org/css-color-adjust/#color-scheme-effect
    mojom::blink::ColorScheme root_color_scheme =
        mojom::blink::ColorScheme::kLight;
    if (auto* root_element = GetDocument().documentElement()) {
      if (const ComputedStyle* style = root_element->GetComputedStyle())
        root_color_scheme = style->UsedColorSchemeForInitialColors();
      else if (SupportsDarkColorScheme())
        root_color_scheme = mojom::blink::ColorScheme::kDark;
    }
    auto* settings = GetDocument().GetSettings();
    bool force_dark_enabled = settings && settings->GetForceDarkModeEnabled();
    color_scheme_background_ =
        root_color_scheme == mojom::blink::ColorScheme::kLight &&
                !force_dark_enabled
            ? Color::kWhite
            : Color(0x12, 0x12, 0x12);
    if (GetDocument().IsInMainFrame()) {
      if (root_color_scheme == mojom::blink::ColorScheme::kDark ||
          (root_color_scheme == mojom::blink::ColorScheme::kLight &&
           force_dark_enabled)) {
        use_color_adjust_background =
            LocalFrameView::UseColorAdjustBackground::kIfBaseNotTransparent;
      }
    } else if (root_color_scheme != owner_color_scheme_) {
      // Iframes should paint a solid background if the embedding iframe has a
      // used color-scheme different from the used color-scheme of the embedded
      // root element. Normally, iframes as transparent by default.
      use_color_adjust_background =
          LocalFrameView::UseColorAdjustBackground::kYes;
    }
  }

  view->SetUseColorAdjustBackground(use_color_adjust_background,
                                    color_scheme_changed);
}

void StyleEngine::SetOwnerColorScheme(mojom::blink::ColorScheme color_scheme) {
  DCHECK(!GetDocument().IsInMainFrame());
  if (owner_color_scheme_ == color_scheme)
    return;
  owner_color_scheme_ = color_scheme;
  UpdateColorSchemeBackground(true);
}

void StyleEngine::UpdateForcedBackgroundColor() {
  forced_background_color_ = LayoutTheme::GetTheme().SystemColor(
      CSSValueID::kCanvas, mojom::blink::ColorScheme::kLight);
}

Color StyleEngine::ColorAdjustBackgroundColor() const {
  if (forced_colors_ != ForcedColors::kNone)
    return ForcedBackgroundColor();
  return color_scheme_background_;
}

void StyleEngine::MarkAllElementsForStyleRecalc(
    const StyleChangeReasonForTracing& reason) {
  if (Element* root = GetDocument().documentElement())
    root->SetNeedsStyleRecalc(kSubtreeStyleChange, reason);
}

void StyleEngine::UpdateViewportStyle() {
  if (!viewport_style_dirty_)
    return;

  viewport_style_dirty_ = false;

  scoped_refptr<ComputedStyle> viewport_style = resolver_->StyleForViewport();
  if (ComputedStyle::ComputeDifference(
          viewport_style.get(), GetDocument().GetLayoutView()->Style()) !=
      ComputedStyle::Difference::kEqual) {
    GetDocument().GetLayoutView()->SetStyle(std::move(viewport_style));
  }
}

bool StyleEngine::NeedsFullStyleUpdate() const {
  return NeedsActiveStyleUpdate() || NeedsWhitespaceReattachment() ||
         IsViewportStyleDirty();
}

void StyleEngine::PropagateWritingModeAndDirectionToHTMLRoot() {
  if (HTMLHtmlElement* root_element =
          DynamicTo<HTMLHtmlElement>(GetDocument().documentElement()))
    root_element->PropagateWritingModeAndDirectionFromBody();
}

CounterStyleMap& StyleEngine::EnsureUserCounterStyleMap() {
  if (!user_counter_style_map_) {
    user_counter_style_map_ =
        CounterStyleMap::CreateUserCounterStyleMap(GetDocument());
  }
  return *user_counter_style_map_;
}

const CounterStyle& StyleEngine::FindCounterStyleAcrossScopes(
    const AtomicString& name,
    const TreeScope* scope) const {
  CounterStyleMap* target_map = nullptr;
  while (scope) {
    if (CounterStyleMap* map =
            CounterStyleMap::GetAuthorCounterStyleMap(*scope)) {
      target_map = map;
      break;
    }
    scope = scope->ParentTreeScope();
  }
  if (!target_map && user_counter_style_map_)
    target_map = user_counter_style_map_;
  if (!target_map)
    target_map = CounterStyleMap::GetUACounterStyleMap();
  if (CounterStyle* result = target_map->FindCounterStyleAcrossScopes(name))
    return *result;
  return CounterStyle::GetDecimal();
}

void StyleEngine::ChangeRenderingForHTMLSelect(HTMLSelectElement& select) {
  // TODO(crbug.com/1191353): SetForceReattachLayoutTree() should be the correct
  // way to create a new layout tree for a select element that changes rendering
  // and not style, but the code for updating the selected index relies on the
  // layout tree to be deleted. To work around that, we do a synchronous
  // DetachLayoutTree as if the subtree is taken out of the flat tree.
  // DetachLayoutTree will clear dirty bits which means we also need to simulate
  // that we are in a dom removal to make the style recalc root be updated
  // correctly.
  StyleEngine::DOMRemovalScope removal_scope(*this);
  To<Element>(select).DetachLayoutTree();
  // If the recalc root is in this subtree, DetachLayoutTree() above clears the
  // bits and we need to update the root. Otherwise the AssertRootNodeInvariants
  // will fail for SetNeedsStyleRecalc below.
  if (Element* parent = select.GetStyleRecalcParent()) {
    style_recalc_root_.SubtreeModified(*parent);
  } else if (GetDocument() == select.parentNode()) {
    // Style recalc parent being null either means the select element is not
    // part of the flat tree or the document root node. In the latter case all
    // dirty bits will be cleared by DetachLayoutTree() and we can clear the
    // recalc root.
    style_recalc_root_.Clear();
  }
  select.SetNeedsStyleRecalc(
      kLocalStyleChange,
      StyleChangeReasonForTracing::Create(style_change_reason::kControl));
}

void StyleEngine::Trace(Visitor* visitor) const {
  visitor->Trace(document_);
  visitor->Trace(injected_user_style_sheets_);
  visitor->Trace(injected_author_style_sheets_);
  visitor->Trace(active_user_style_sheets_);
  visitor->Trace(custom_element_default_style_sheets_);
  visitor->Trace(keyframes_rule_map_);
  visitor->Trace(user_counter_style_map_);
  visitor->Trace(scroll_timeline_rule_map_);
  visitor->Trace(scroll_timeline_map_);
  visitor->Trace(inspector_style_sheet_);
  visitor->Trace(document_style_sheet_collection_);
  visitor->Trace(style_sheet_collection_map_);
  visitor->Trace(dirty_tree_scopes_);
  visitor->Trace(active_tree_scopes_);
  visitor->Trace(resolver_);
  visitor->Trace(vision_deficiency_filter_);
  visitor->Trace(viewport_resolver_);
  visitor->Trace(media_query_evaluator_);
  visitor->Trace(global_rule_set_);
  visitor->Trace(pending_invalidations_);
  visitor->Trace(style_invalidation_root_);
  visitor->Trace(style_recalc_root_);
  visitor->Trace(layout_tree_rebuild_root_);
  visitor->Trace(whitespace_reattach_set_);
  visitor->Trace(font_selector_);
  visitor->Trace(text_to_sheet_cache_);
  visitor->Trace(sheet_to_text_cache_);
  visitor->Trace(tracker_);
  visitor->Trace(meta_color_scheme_);
  visitor->Trace(text_tracks_);
  visitor->Trace(vtt_originating_element_);
  FontSelectorClient::Trace(visitor);
}

}  // namespace blink
