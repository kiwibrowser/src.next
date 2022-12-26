/*
 * Copyright (C) 1999 Lars Knoll (knoll@kde.org)
 *           (C) 1999 Antti Koivisto (koivisto@kde.org)
 *           (C) 2001 Peter Kelly (pmk@post.com)
 *           (C) 2001 Dirk Mueller (mueller@kde.org)
 *           (C) 2007 David Smith (catfish.man@gmail.com)
 * Copyright (C) 2004, 2005, 2006, 2007, 2008, 2009, 2010, 2012, 2013 Apple Inc.
 * All rights reserved.
 *           (C) 2007 Eric Seidel (eric@webkit.org)
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

#include "third_party/blink/renderer/core/dom/element.h"

#include <algorithm>
#include <bitset>
#include <limits>
#include <memory>
#include <utility>

#include "base/feature_list.h"
#include "cc/input/snap_selection_strategy.h"
#include "third_party/blink/public/common/features.h"
#include "third_party/blink/public/common/privacy_budget/identifiability_metric_builder.h"
#include "third_party/blink/public/common/privacy_budget/identifiability_study_settings.h"
#include "third_party/blink/public/mojom/scroll/scroll_into_view_params.mojom-blink.h"
#include "third_party/blink/public/web/web_autofill_state.h"
#include "third_party/blink/renderer/bindings/core/v8/dictionary.h"
#include "third_party/blink/renderer/bindings/core/v8/script_promise_resolver.h"
#include "third_party/blink/renderer/bindings/core/v8/v8_check_visibility_options.h"
#include "third_party/blink/renderer/bindings/core/v8/v8_get_inner_html_options.h"
#include "third_party/blink/renderer/bindings/core/v8/v8_pointer_lock_options.h"
#include "third_party/blink/renderer/bindings/core/v8/v8_scroll_into_view_options.h"
#include "third_party/blink/renderer/bindings/core/v8/v8_scroll_to_options.h"
#include "third_party/blink/renderer/bindings/core/v8/v8_shadow_root_init.h"
#include "third_party/blink/renderer/bindings/core/v8/v8_union_boolean_scrollintoviewoptions.h"
#include "third_party/blink/renderer/core/accessibility/ax_context.h"
#include "third_party/blink/renderer/core/accessibility/ax_object_cache.h"
#include "third_party/blink/renderer/core/animation/css/css_animations.h"
#include "third_party/blink/renderer/core/aom/computed_accessible_node.h"
#include "third_party/blink/renderer/core/css/container_query_data.h"
#include "third_party/blink/renderer/core/css/container_query_evaluator.h"
#include "third_party/blink/renderer/core/css/css_identifier_value.h"
#include "third_party/blink/renderer/core/css/css_numeric_literal_value.h"
#include "third_party/blink/renderer/core/css/css_primitive_value.h"
#include "third_party/blink/renderer/core/css/css_property_value_set.h"
#include "third_party/blink/renderer/core/css/css_selector_watch.h"
#include "third_party/blink/renderer/core/css/css_style_sheet.h"
#include "third_party/blink/renderer/core/css/css_value.h"
#include "third_party/blink/renderer/core/css/parser/css_parser.h"
#include "third_party/blink/renderer/core/css/parser/css_selector_parser.h"
#include "third_party/blink/renderer/core/css/property_set_css_style_declaration.h"
#include "third_party/blink/renderer/core/css/resolver/selector_filter_parent_scope.h"
#include "third_party/blink/renderer/core/css/resolver/style_adjuster.h"
#include "third_party/blink/renderer/core/css/resolver/style_resolver.h"
#include "third_party/blink/renderer/core/css/resolver/style_resolver_stats.h"
#include "third_party/blink/renderer/core/css/selector_query.h"
#include "third_party/blink/renderer/core/css/style_change_reason.h"
#include "third_party/blink/renderer/core/css/style_engine.h"
#include "third_party/blink/renderer/core/css_value_keywords.h"
#include "third_party/blink/renderer/core/display_lock/display_lock_context.h"
#include "third_party/blink/renderer/core/display_lock/display_lock_document_state.h"
#include "third_party/blink/renderer/core/display_lock/display_lock_utilities.h"
#include "third_party/blink/renderer/core/document_transition/document_transition_supplement.h"
#include "third_party/blink/renderer/core/document_transition/document_transition_utils.h"
#include "third_party/blink/renderer/core/dom/attr.h"
#include "third_party/blink/renderer/core/dom/container_node.h"
#include "third_party/blink/renderer/core/dom/dataset_dom_string_map.h"
#include "third_party/blink/renderer/core/dom/document.h"
#include "third_party/blink/renderer/core/dom/dom_token_list.h"
#include "third_party/blink/renderer/core/dom/element_data_cache.h"
#include "third_party/blink/renderer/core/dom/element_rare_data.h"
#include "third_party/blink/renderer/core/dom/element_traversal.h"
#include "third_party/blink/renderer/core/dom/events/event_dispatch_forbidden_scope.h"
#include "third_party/blink/renderer/core/dom/events/event_dispatcher.h"
#include "third_party/blink/renderer/core/dom/events/event_path.h"
#include "third_party/blink/renderer/core/dom/first_letter_pseudo_element.h"
#include "third_party/blink/renderer/core/dom/focus_params.h"
#include "third_party/blink/renderer/core/dom/layout_tree_builder.h"
#include "third_party/blink/renderer/core/dom/mutation_observer_interest_group.h"
#include "third_party/blink/renderer/core/dom/mutation_record.h"
#include "third_party/blink/renderer/core/dom/named_node_map.h"
#include "third_party/blink/renderer/core/dom/node_computed_style.h"
#include "third_party/blink/renderer/core/dom/popup_data.h"
#include "third_party/blink/renderer/core/dom/presentation_attribute_style.h"
#include "third_party/blink/renderer/core/dom/pseudo_element.h"
#include "third_party/blink/renderer/core/dom/scriptable_document_parser.h"
#include "third_party/blink/renderer/core/dom/shadow_root.h"
#include "third_party/blink/renderer/core/dom/slot_assignment.h"
#include "third_party/blink/renderer/core/dom/space_split_string.h"
#include "third_party/blink/renderer/core/dom/text.h"
#include "third_party/blink/renderer/core/dom/whitespace_attacher.h"
#include "third_party/blink/renderer/core/editing/commands/undo_stack.h"
#include "third_party/blink/renderer/core/editing/editing_utilities.h"
#include "third_party/blink/renderer/core/editing/editor.h"
#include "third_party/blink/renderer/core/editing/ephemeral_range.h"
#include "third_party/blink/renderer/core/editing/frame_selection.h"
#include "third_party/blink/renderer/core/editing/ime/edit_context.h"
#include "third_party/blink/renderer/core/editing/ime/input_method_controller.h"
#include "third_party/blink/renderer/core/editing/selection_template.h"
#include "third_party/blink/renderer/core/editing/serializers/serialization.h"
#include "third_party/blink/renderer/core/editing/set_selection_options.h"
#include "third_party/blink/renderer/core/editing/visible_selection.h"
#include "third_party/blink/renderer/core/event_type_names.h"
#include "third_party/blink/renderer/core/events/focus_event.h"
#include "third_party/blink/renderer/core/events/keyboard_event.h"
#include "third_party/blink/renderer/core/execution_context/security_context.h"
#include "third_party/blink/renderer/core/frame/csp/content_security_policy.h"
#include "third_party/blink/renderer/core/frame/local_dom_window.h"
#include "third_party/blink/renderer/core/frame/local_frame.h"
#include "third_party/blink/renderer/core/frame/local_frame_view.h"
#include "third_party/blink/renderer/core/frame/settings.h"
#include "third_party/blink/renderer/core/frame/visual_viewport.h"
#include "third_party/blink/renderer/core/fullscreen/fullscreen.h"
#include "third_party/blink/renderer/core/geometry/dom_rect.h"
#include "third_party/blink/renderer/core/geometry/dom_rect_list.h"
#include "third_party/blink/renderer/core/html/canvas/html_canvas_element.h"
#include "third_party/blink/renderer/core/html/custom/custom_element.h"
#include "third_party/blink/renderer/core/html/custom/custom_element_registry.h"
#include "third_party/blink/renderer/core/html/forms/html_button_element.h"
#include "third_party/blink/renderer/core/html/forms/html_field_set_element.h"
#include "third_party/blink/renderer/core/html/forms/html_form_control_element.h"
#include "third_party/blink/renderer/core/html/forms/html_form_controls_collection.h"
#include "third_party/blink/renderer/core/html/forms/html_options_collection.h"
#include "third_party/blink/renderer/core/html/forms/html_select_element.h"
#include "third_party/blink/renderer/core/html/html_body_element.h"
#include "third_party/blink/renderer/core/html/html_collection.h"
#include "third_party/blink/renderer/core/html/html_document.h"
#include "third_party/blink/renderer/core/html/html_element.h"
#include "third_party/blink/renderer/core/html/html_frame_element_base.h"
#include "third_party/blink/renderer/core/html/html_frame_owner_element.h"
#include "third_party/blink/renderer/core/html/html_html_element.h"
#include "third_party/blink/renderer/core/html/html_plugin_element.h"
#include "third_party/blink/renderer/core/html/html_slot_element.h"
#include "third_party/blink/renderer/core/html/html_table_rows_collection.h"
#include "third_party/blink/renderer/core/html/html_template_element.h"
#include "third_party/blink/renderer/core/html/nesting_level_incrementer.h"
#include "third_party/blink/renderer/core/html/parser/html_parser_idioms.h"
#include "third_party/blink/renderer/core/html_element_type_helpers.h"
#include "third_party/blink/renderer/core/html_names.h"
#include "third_party/blink/renderer/core/input/event_handler.h"
#include "third_party/blink/renderer/core/inspector/console_message.h"
#include "third_party/blink/renderer/core/intersection_observer/element_intersection_observer_data.h"
#include "third_party/blink/renderer/core/intersection_observer/intersection_observer_controller.h"
#include "third_party/blink/renderer/core/layout/adjust_for_absolute_zoom.h"
#include "third_party/blink/renderer/core/layout/layout_text_fragment.h"
#include "third_party/blink/renderer/core/layout/layout_view.h"
#include "third_party/blink/renderer/core/layout/ng/inline/layout_ng_text_combine.h"
#include "third_party/blink/renderer/core/layout/ng/ng_block_node.h"
#include "third_party/blink/renderer/core/page/chrome_client.h"
#include "third_party/blink/renderer/core/page/focus_controller.h"
#include "third_party/blink/renderer/core/page/page.h"
#include "third_party/blink/renderer/core/page/page_animator.h"
#include "third_party/blink/renderer/core/page/pointer_lock_controller.h"
#include "third_party/blink/renderer/core/page/scrolling/root_scroller_controller.h"
#include "third_party/blink/renderer/core/page/spatial_navigation.h"
#include "third_party/blink/renderer/core/paint/paint_layer.h"
#include "third_party/blink/renderer/core/paint/paint_layer_scrollable_area.h"
#include "third_party/blink/renderer/core/probe/core_probes.h"
#include "third_party/blink/renderer/core/resize_observer/resize_observation.h"
#include "third_party/blink/renderer/core/scroll/scroll_into_view_util.h"
#include "third_party/blink/renderer/core/scroll/scrollable_area.h"
#include "third_party/blink/renderer/core/scroll/scrollbar_theme.h"
#include "third_party/blink/renderer/core/scroll/smooth_scroll_sequencer.h"
#include "third_party/blink/renderer/core/style/computed_style_constants.h"
#include "third_party/blink/renderer/core/svg/svg_a_element.h"
#include "third_party/blink/renderer/core/svg/svg_animated_href.h"
#include "third_party/blink/renderer/core/svg/svg_element.h"
#include "third_party/blink/renderer/core/svg/svg_svg_element.h"
#include "third_party/blink/renderer/core/svg_names.h"
#include "third_party/blink/renderer/core/trustedtypes/trusted_types_util.h"
#include "third_party/blink/renderer/core/xml_names.h"
#include "third_party/blink/renderer/platform/bindings/dom_data_store.h"
#include "third_party/blink/renderer/platform/bindings/exception_state.h"
#include "third_party/blink/renderer/platform/bindings/v8_dom_activity_logger.h"
#include "third_party/blink/renderer/platform/bindings/v8_dom_wrapper.h"
#include "third_party/blink/renderer/platform/bindings/v8_per_context_data.h"
#include "third_party/blink/renderer/platform/heap/garbage_collected.h"
#include "third_party/blink/renderer/platform/heap/thread_state.h"
#include "third_party/blink/renderer/platform/instrumentation/tracing/trace_event.h"
#include "third_party/blink/renderer/platform/instrumentation/use_counter.h"
#include "third_party/blink/renderer/platform/region_capture_crop_id.h"
#include "third_party/blink/renderer/platform/runtime_enabled_features.h"
#include "third_party/blink/renderer/platform/wtf/casting.h"
#include "third_party/blink/renderer/platform/wtf/hash_functions.h"
#include "third_party/blink/renderer/platform/wtf/text/atomic_string.h"
#include "third_party/blink/renderer/platform/wtf/text/string_builder.h"
#include "third_party/blink/renderer/platform/wtf/text/text_position.h"
#include "ui/accessibility/ax_mode.h"
#include "ui/gfx/geometry/rect_conversions.h"

namespace blink {

enum class ClassStringContent { kEmpty, kWhiteSpaceOnly, kHasClasses };

namespace {

bool EarlyExitOnNoopClassOrStyleChange() {
  static const bool is_enabled = base::FeatureList::IsEnabled(
      features::kEarlyExitOnNoopClassOrStyleChange);
  DCHECK_EQ(is_enabled, base::FeatureList::IsEnabled(
                            features::kEarlyExitOnNoopClassOrStyleChange));
  return is_enabled;
}

class DisplayLockStyleScope {
  STACK_ALLOCATED();

 public:
  explicit DisplayLockStyleScope(Element* element) : element_(element) {
    // Note that we don't store context as a member of this scope, since it may
    // get created as part of element self style recalc.
  }

  ~DisplayLockStyleScope() {
    if (auto* context = element_->GetDisplayLockContext()) {
      if (did_update_children_)
        context->DidStyleChildren();
    }
  }

  bool ShouldUpdateChildStyle() const {
    // We can't calculate this on construction time, because the element's lock
    // state may changes after self-style calculation ShouldStyle(children).
    auto* context = element_->GetDisplayLockContext();
    return !context || context->ShouldStyleChildren();
  }
  void DidUpdateChildStyle() { did_update_children_ = true; }
  // Returns true if the element was force unlocked due to missing requirements.
  StyleRecalcChange DidUpdateSelfStyle(StyleRecalcChange change) {
    if (auto* context = element_->GetDisplayLockContext()) {
      context->DidStyleSelf();
      // After we notified context that we styled self, it may cause an unlock /
      // modification to the blocked style change, so accumulate the change here
      // again. Note that if the context is locked we will restore it as the
      // blocked style change in RecalcStyle.
      return change.Combine(context->TakeBlockedStyleRecalcChange());
    }
    return change;
  }

  void NotifyChildStyleRecalcWasBlocked(const StyleRecalcChange& change) {
    DCHECK(!ShouldUpdateChildStyle());
    // The only way to be blocked here is if we have a display lock context.
    DCHECK(element_->GetDisplayLockContext());

    element_->GetDisplayLockContext()->NotifyChildStyleRecalcWasBlocked(change);
  }

 private:
  Element* element_;
  bool did_update_children_ = false;
};

bool IsRootEditableElementWithCounting(const Element& element) {
  bool is_editable = IsRootEditableElement(element);
  Document& doc = element.GetDocument();
  if (!doc.IsActive())
    return is_editable;
  // -webkit-user-modify doesn't affect text control elements.
  if (element.IsTextControl())
    return is_editable;
  const auto* style = element.GetComputedStyle();
  if (!style)
    return is_editable;
  auto user_modify = style->UsedUserModify();
  const AtomicString& ce_value =
      element.FastGetAttribute(html_names::kContenteditableAttr);
  if (ce_value.IsNull() || EqualIgnoringASCIICase(ce_value, "false")) {
    if (user_modify == EUserModify::kReadWritePlaintextOnly) {
      UseCounter::Count(doc, WebFeature::kPlainTextEditingEffective);
      UseCounter::Count(doc, WebFeature::kWebKitUserModifyPlainTextEffective);
      UseCounter::Count(doc, WebFeature::kWebKitUserModifyEffective);
    } else if (user_modify == EUserModify::kReadWrite) {
      UseCounter::Count(doc, WebFeature::kWebKitUserModifyReadWriteEffective);
      UseCounter::Count(doc, WebFeature::kWebKitUserModifyEffective);
    }
  } else if (ce_value.IsEmpty() || EqualIgnoringASCIICase(ce_value, "true")) {
    if (user_modify == EUserModify::kReadWritePlaintextOnly) {
      UseCounter::Count(doc, WebFeature::kPlainTextEditingEffective);
      UseCounter::Count(doc, WebFeature::kWebKitUserModifyPlainTextEffective);
      UseCounter::Count(doc, WebFeature::kWebKitUserModifyEffective);
    } else if (user_modify == EUserModify::kReadOnly) {
      UseCounter::Count(doc, WebFeature::kWebKitUserModifyReadOnlyEffective);
      UseCounter::Count(doc, WebFeature::kWebKitUserModifyEffective);
    }
  } else if (EqualIgnoringASCIICase(ce_value, "plaintext-only")) {
    UseCounter::Count(doc, WebFeature::kPlainTextEditingEffective);
    if (user_modify == EUserModify::kReadWrite) {
      UseCounter::Count(doc, WebFeature::kWebKitUserModifyReadWriteEffective);
      UseCounter::Count(doc, WebFeature::kWebKitUserModifyEffective);
    } else if (user_modify == EUserModify::kReadOnly) {
      UseCounter::Count(doc, WebFeature::kWebKitUserModifyReadOnlyEffective);
      UseCounter::Count(doc, WebFeature::kWebKitUserModifyEffective);
    }
  }
  return is_editable;
}

// Return true if we're absolutely sure that this node is going to establish a
// new formatting context that can serve as a layout engine boundary (NG
// vs. legacy). Whether or not it establishes a new formatting context cannot be
// accurately determined until we have actually created the object (see
// LayoutBlockFlow::CreatesNewFormattingContext()), so this function may (and is
// allowed to) return false negatives, but NEVER false positives.
bool DefinitelyNewFormattingContext(const Node& node,
                                    const ComputedStyle& style) {
  auto display = style.Display();
  if (display == EDisplay::kInline || display == EDisplay::kContents ||
      display == EDisplay::kTableRowGroup ||
      display == EDisplay::kTableHeaderGroup ||
      display == EDisplay::kTableFooterGroup ||
      display == EDisplay::kTableRow || display == EDisplay::kTableCaption ||
      display == EDisplay::kTableCell ||
      display == EDisplay::kTableColumnGroup ||
      display == EDisplay::kTableColumn)
    return false;

  // ::marker may establish a formatting context but still have some dependency
  // on the originating list item, so return false.
  if (node.IsMarkerPseudoElement())
    return false;
  // The only block-container display types that potentially don't establish a
  // new formatting context, are 'block' and 'list-item'.
  if (display != EDisplay::kBlock && display != EDisplay::kListItem) {
    // DETAILS and SUMMARY elements partially or completely ignore the display
    // type, though, and may end up disregarding the display type and just
    // create block containers. And those don't necessarily create a formatting
    // context.
    if (!IsA<HTMLDetailsElement>(node) && !IsA<HTMLSummaryElement>(node))
      return true;
  }
  if (style.IsScrollContainer())
    return node.GetDocument().ViewportDefiningElement() != &node;
  if (style.HasOutOfFlowPosition() ||
      (style.IsFloating() && !style.IsFlexOrGridItem()) ||
      style.ContainsPaint() || style.ContainsLayout() ||
      style.SpecifiesColumns())
    return true;
  if (node.GetDocument().documentElement() == &node)
    return true;
  if (const Element* element = DynamicTo<Element>(&node)) {
    // Replaced elements are considered to create a new formatting context, in
    // the sense that they can't possibly have children that participate in the
    // same formatting context as their parent.
    if (IsA<HTMLObjectElement>(element)) {
      // OBJECT elements are special, though. If they use fallback content, they
      // act as regular elements, and we can't claim that they establish a
      // formatting context, just based on element type, since children may very
      // well participate in the same formatting context as the parent of the
      // OBJECT.
      if (!element->ChildrenCanHaveStyle())
        return true;
    } else if (IsA<HTMLImageElement>(element) ||
               element->IsFormControlElement() || element->IsMediaElement() ||
               element->IsFrameOwnerElement()) {
      return true;
    }

    // foreignObject is absolutely-positioned for the purposes of CSS layout and
    // so always establishes a new formatting context.
    // https://svgwg.org/svg2-draft/embedded.html#Placement
    if (IsA<SVGForeignObjectElement>(element))
      return true;
  }
  // An item inside a flex or grid container always establishes a new formatting
  // context. Same for a child of a MathML or custom layout container.
  if (const Node* parent = LayoutTreeBuilderTraversal::LayoutParent(node))
    return parent->ComputedStyleRef().BlockifiesChildren();
  return false;
}

inline bool NeedsLegacyBlockFragmentation(const Element& element,
                                          const ComputedStyle& style) {
  if (!style.InsideFragmentationContextWithNondeterministicEngine())
    return false;

  // If we're inside an NG block fragmentation context, all fragmentable boxes
  // must be laid out by NG natively. We only allow legacy layout objects if
  // they are monolithic (e.g. replaced content, inline-table, and so
  // on).

  // Inline display types end up on a line, and are therefore monolithic, so we
  // can allow those.
  if (style.IsDisplayInlineType())
    return false;

  if (style.IsDisplayTableType() &&
      !RuntimeEnabledFeatures::LayoutNGTableFragmentationEnabled())
    return true;

  if (style.IsDisplayGridBox() &&
      !RuntimeEnabledFeatures::LayoutNGGridFragmentationEnabled())
    return true;

  // display:flex (and variants) require legacy fallback if NG flex
  // fragmentation isn't enabled. The same applies to button elements, as they
  // use flex layout (albeit with some exceptions, but we'll ignore those here).
  if ((style.IsDisplayFlexibleBox() ||
       style.IsDeprecatedFlexboxUsingFlexLayout() ||
       IsA<HTMLButtonElement>(element)) &&
      !RuntimeEnabledFeatures::LayoutNGFlexFragmentationEnabled())
    return true;

  return false;
}

bool NeedsLegacyLayoutForEntireDocument(Document& document) {
  // Fall back to legacy layout for frameset documents. The frameset itself (and
  // the frames) can only create legacy layout objects anyway (no NG counterpart
  // for them yet). However, the layout object for the HTML root element would
  // be an NG one. If we'd then print the document, we'd fall back to legacy
  // layout (because of the above check), which would re-attach all layout
  // objects, which would cause the frameset to lose state of some sort, leaving
  // everything blank when printed.
  if (document.IsFrameSet()) {
    UseCounter::Count(document, WebFeature::kLegacyLayoutByFrameSet);
    return true;
  }

  return false;
}

bool CalculateStyleShouldForceLegacyLayout(const Element& element,
                                           const ComputedStyle& style) {
  Document& document = element.GetDocument();

  if (style.DisplayTypeRequiresLayoutNG())
    return false;

  if (!RuntimeEnabledFeatures::LayoutNGBlockFragmentationEnabled()) {
    // Disable NG for the entire subtree if we're establishing a multicol
    // container.
    if (style.SpecifiesColumns()) {
      UseCounter::Count(document, WebFeature::kLegacyLayoutByMultiCol);
      return true;
    }
  }

  if (document.Printing() && element == document.documentElement() &&
      !RuntimeEnabledFeatures::LayoutNGPrintingEnabled()) {
    UseCounter::Count(document, WebFeature::kLegacyLayoutByPrinting);
    return true;
  }

  if (NeedsLegacyLayoutForEntireDocument(document))
    return true;

  if (NeedsLegacyBlockFragmentation(element, style)) {
    UseCounter::Count(
        document,
        WebFeature::kLegacyLayoutByTableFlexGridBlockInNGFragmentationContext);
    return true;
  }

  return false;
}

bool HasLeftwardDirection(const Element& element) {
  auto* style = element.GetComputedStyle();
  if (!style)
    return false;

  WritingMode writing_mode = style->GetWritingMode();
  bool is_rtl = !style->IsLeftToRightDirection();
  return (writing_mode == WritingMode::kHorizontalTb && is_rtl) ||
         writing_mode == WritingMode::kVerticalRl ||
         writing_mode == WritingMode::kSidewaysRl;
}

bool HasUpwardDirection(const Element& element) {
  auto* style = element.GetComputedStyle();
  if (!style)
    return false;

  WritingMode writing_mode = style->GetWritingMode();
  bool is_rtl = !style->IsLeftToRightDirection();
  return (is_rtl && (writing_mode == WritingMode::kVerticalRl ||
                     writing_mode == WritingMode::kVerticalLr ||
                     writing_mode == WritingMode::kSidewaysRl)) ||
         (!is_rtl && writing_mode == WritingMode::kSidewaysLr);
}

// TODO(meredithl): Automatically generate this method once the IDL compiler has
// been refactored. See http://crbug.com/839389 for details.
bool IsElementReflectionAttribute(const QualifiedName& name) {
  if (name == html_names::kAriaActivedescendantAttr)
    return true;
  if (name == html_names::kAriaControlsAttr)
    return true;
  if (name == html_names::kAriaDescribedbyAttr)
    return true;
  if (name == html_names::kAriaDetailsAttr)
    return true;
  if (name == html_names::kAriaErrormessageAttr)
    return true;
  if (name == html_names::kAriaFlowtoAttr)
    return true;
  if (name == html_names::kAriaLabeledbyAttr)
    return true;
  if (name == html_names::kAriaLabelledbyAttr)
    return true;
  if (name == html_names::kAriaOwnsAttr)
    return true;
  return false;
}

HeapLinkedHashSet<WeakMember<Element>>* GetExplicitlySetElementsForAttr(
    Element* element,
    const QualifiedName& name) {
  ExplicitlySetAttrElementsMap* element_attribute_map =
      element->GetDocument().GetExplicitlySetAttrElementsMap(element);
  auto it = element_attribute_map->find(name);
  if (it == element_attribute_map->end())
    return nullptr;
  const auto& elements = it->value;
  return elements->size() ? elements : nullptr;
}

// Checks that the given element |candidate| is a descendant of
// |attribute_element|'s  shadow including ancestors.
bool ElementIsDescendantOfShadowIncludingAncestor(
    const Element& attribute_element,
    const Element& candidate) {
  // TODO(meredithl): Update this to allow setting relationships for elements
  // outside of the DOM once the spec is finalized. For consistency and
  // simplicity, for now it is disallowed.
  if (!attribute_element.IsInTreeScope() || !candidate.IsInTreeScope())
    return false;
  ShadowRoot* nearest_root = attribute_element.ContainingShadowRoot();
  const Element* shadow_host = &attribute_element;
  while (nearest_root) {
    shadow_host = &nearest_root->host();
    if (candidate.IsDescendantOf(nearest_root))
      return true;
    nearest_root = shadow_host->ContainingShadowRoot();
  }

  Element* document_element = shadow_host->GetDocument().documentElement();
  return candidate.IsDescendantOf(document_element);
}

// The first algorithm in
// https://html.spec.whatwg.org/C/#the-autofocus-attribute
void EnqueueAutofocus(Element& element) {
  // When an element with the autofocus attribute specified is inserted into a
  // document, run the following steps:
  DCHECK(element.isConnected());
  if (!element.IsAutofocusable())
    return;

  // 1. If the user has indicated (for example, by starting to type in a form
  // control) that they do not wish focus to be changed, then optionally return.

  // We don't implement this optional step. If other browsers have such
  // behavior, we should follow it or standardize it.

  // 2. Let target be the element's node document.
  Document& doc = element.GetDocument();
  LocalDOMWindow* window = doc.domWindow();

  // 3. If target's browsing context is null, then return.
  if (!window)
    return;

  // 4. If target's active sandboxing flag set has the sandboxed automatic
  // features browsing context flag, then return.
  if (window->IsSandboxed(
          network::mojom::blink::WebSandboxFlags::kAutomaticFeatures)) {
    window->AddConsoleMessage(MakeGarbageCollected<ConsoleMessage>(
        mojom::ConsoleMessageSource::kSecurity,
        mojom::ConsoleMessageLevel::kError,
        String::Format(
            "Blocked autofocusing on a <%s> element because the element's "
            "frame "
            "is sandboxed and the 'allow-scripts' permission is not set.",
            element.TagQName().ToString().Ascii().c_str())));
    return;
  }

  // 5. For each ancestorBC of target's browsing context's ancestor browsing
  // contexts: if ancestorBC's active document's origin is not same origin with
  // target's origin, then return.
  for (Frame* frame = doc.GetFrame(); frame; frame = frame->Parent()) {
    if (!frame->IsCrossOriginToOutermostMainFrame())
      continue;
    window->AddConsoleMessage(MakeGarbageCollected<ConsoleMessage>(
        mojom::ConsoleMessageSource::kSecurity,
        mojom::ConsoleMessageLevel::kError,
        String::Format("Blocked autofocusing on a <%s> element in a "
                       "cross-origin subframe.",
                       element.TagQName().ToString().Ascii().c_str())));
    return;
  }

  // 6. Let topDocument be the active document of target's browsing context's
  // top-level browsing context.
  Document& top_document = element.GetDocument().TopDocument();

  top_document.EnqueueAutofocusCandidate(element);
}

// For container query containers, we may skip the style recalc of the
// container's descendants during regular style recalc, with the expectation
// that we will recalc the style of those elements during `NGBlockNode::Layout`.
// If a given LayoutObject isn't guaranteed to actually enter
// `NGBlockNode::Layout`, then we recalc the skipped descendants during
// layout-tree building instead.
bool IsGuaranteedToEnterNGBlockNodeLayout(const LayoutObject& layout_object) {
  if (!RuntimeEnabledFeatures::LayoutNGEnabled())
    return false;
  auto* box = DynamicTo<LayoutBox>(layout_object);
  if (!box)
    return false;
  if (!NGBlockNode::CanUseNewLayout(*box))
    return false;
  // Out-of-flow positioned replaced elements take the legacy path for layout
  // if the container for positioning is a legacy object. That is the case for
  // LayoutView, which is a legacy object but does not otherwise force
  // legacy layout objects.
  if (!RuntimeEnabledFeatures::LayoutNGViewEnabled() &&
      layout_object.IsOutOfFlowPositioned() &&
      layout_object.IsLayoutReplaced()) {
    return false;
  }
  return true;
}

}  // namespace

Element::Element(const QualifiedName& tag_name,
                 Document* document,
                 ConstructionType type)
    : ContainerNode(document, type), tag_name_(tag_name) {}

Element* Element::GetAnimationTarget() {
  return this;
}

bool Element::HasElementFlagInternal(ElementFlags mask) const {
  return GetElementRareData()->HasElementFlag(mask);
}

void Element::SetElementFlag(ElementFlags mask, bool value) {
  if (!HasRareData() && !value)
    return;
  EnsureElementRareData().SetElementFlag(mask, value);
}

void Element::ClearElementFlag(ElementFlags mask) {
  if (!HasRareData())
    return;
  GetElementRareData()->ClearElementFlag(mask);
}

void Element::ClearTabIndexExplicitlyIfNeeded() {
  if (HasRareData())
    GetElementRareData()->ClearTabIndexExplicitly();
}

void Element::SetTabIndexExplicitly() {
  EnsureElementRareData().SetTabIndexExplicitly();
}

void Element::setTabIndex(int value) {
  SetIntegralAttribute(html_names::kTabindexAttr, value);
}

int Element::tabIndex() const {
  // https://html.spec.whatwg.org/C/#dom-tabindex
  // The tabIndex IDL attribute must reflect the value of the tabindex content
  // attribute. The default value is 0 if the element is an a, area, button,
  // frame, iframe, input, object, select, textarea, or SVG a element, or is a
  // summary element that is a summary for its parent details. The default value
  // is −1 otherwise.
  return GetIntegralAttribute(html_names::kTabindexAttr, DefaultTabIndex());
}

int Element::DefaultTabIndex() const {
  return -1;
}

bool Element::IsFocusableStyle() const {
  // TODO(vmpstr): Note that this may be called by accessibility during layout
  // tree attachment, at which point we might not have cleared all of the dirty
  // bits to ensure that the layout tree doesn't need an update. This should be
  // fixable by deferring AX tree updates as a separate phase after layout tree
  // attachment has happened. At that point `InStyleRecalc()` portion of the
  // following DCHECK can be removed.
  DCHECK(
      !GetDocument().IsActive() || GetDocument().InStyleRecalc() ||
      !GetDocument().NeedsLayoutTreeUpdateForNodeIncludingDisplayLocked(*this));
  return IsBaseElementFocusableStyle(GetLayoutObject());
}

bool Element::IsBaseElementFocusableStyle(
    const LayoutObject* layout_object) const {
  if (LayoutObject* layout_object = GetLayoutObject())
    return layout_object->StyleRef().IsFocusable();

  // If a canvas represents embedded content, its descendants are not rendered.
  // But they are still allowed to be focusable as long as their style allows
  // focus, their canvas is rendered, and its style allows focus.
  if (IsInCanvasSubtree()) {
    const ComputedStyle* style = GetComputedStyle();
    if (!style || !style->IsFocusable())
      return false;

    const HTMLCanvasElement* canvas =
        Traversal<HTMLCanvasElement>::FirstAncestorOrSelf(*this);
    DCHECK(canvas);
    if (LayoutObject* layout_object = canvas->GetLayoutObject()) {
      return layout_object->IsCanvas() &&
             layout_object->StyleRef().IsFocusable();
    }
  }

  return false;
}

Node* Element::Clone(Document& factory, CloneChildrenFlag flag) const {
  if (flag == CloneChildrenFlag::kSkip)
    return &CloneWithoutChildren(&factory);
  Element* copy = &CloneWithChildren(flag, &factory);
  // 7. If node is a shadow host and the clone shadows flag is set, run these
  // steps:
  if (flag == CloneChildrenFlag::kCloneWithShadows) {
    auto* shadow_root = GetShadowRoot();
    if (shadow_root && (shadow_root->GetType() == ShadowRootType::kOpen ||
                        shadow_root->GetType() == ShadowRootType::kClosed)) {
      // 7.1 Run attach a shadow root with shadow host equal to copy, mode equal
      // to node’s shadow root’s mode, and delegates focus equal to node’s
      // shadow root’s delegates focus.
      ShadowRoot& cloned_shadow_root = copy->AttachShadowRootInternal(
          shadow_root->GetType(),
          shadow_root->delegatesFocus() ? FocusDelegation::kDelegateFocus
                                        : FocusDelegation::kNone,
          shadow_root->GetSlotAssignmentMode());
      // 7.2 If node’s shadow root’s "is declarative shadow root" is true, then
      // set copy’s shadow root’s "is declarative shadow root" property to true.
      cloned_shadow_root.SetIsDeclarativeShadowRoot(
          shadow_root->IsDeclarativeShadowRoot());

      // 7.NEW If node’s shadow root’s "is available to element internals" is
      // true, then set copy’s shadow root’s "is available to element internals"
      // property to true.
      cloned_shadow_root.SetAvailableToElementInternals(
          shadow_root->IsAvailableToElementInternals());

      // 7.3 If the clone children flag is set, clone all the children of node’s
      // shadow root and append them to copy’s shadow root, with document as
      // specified, the clone children flag being set, and the clone shadows
      // flag being set.
      cloned_shadow_root.CloneChildNodesFrom(*shadow_root, flag);
    }
  }
  return copy;
}

Element& Element::CloneWithChildren(CloneChildrenFlag flag,
                                    Document* nullable_factory) const {
  Element& clone = CloneWithoutAttributesAndChildren(
      nullable_factory ? *nullable_factory : GetDocument());
  // This will catch HTML elements in the wrong namespace that are not correctly
  // copied.  This is a sanity check as HTML overloads some of the DOM methods.
  DCHECK_EQ(IsHTMLElement(), clone.IsHTMLElement());

  clone.CloneAttributesFrom(*this);
  clone.CloneNonAttributePropertiesFrom(*this, flag);
  clone.CloneChildNodesFrom(*this, flag);
  return clone;
}

Element& Element::CloneWithoutChildren(Document* nullable_factory) const {
  Element& clone = CloneWithoutAttributesAndChildren(
      nullable_factory ? *nullable_factory : GetDocument());
  // This will catch HTML elements in the wrong namespace that are not correctly
  // copied.  This is a sanity check as HTML overloads some of the DOM methods.
  DCHECK_EQ(IsHTMLElement(), clone.IsHTMLElement());

  clone.CloneAttributesFrom(*this);
  clone.CloneNonAttributePropertiesFrom(*this, CloneChildrenFlag::kSkip);
  return clone;
}

Element& Element::CloneWithoutAttributesAndChildren(Document& factory) const {
  return *factory.CreateElement(TagQName(), CreateElementFlags::ByCloneNode(),
                                IsValue());
}

Attr* Element::DetachAttribute(wtf_size_t index) {
  DCHECK(GetElementData());
  const Attribute& attribute = GetElementData()->Attributes().at(index);
  Attr* attr_node = AttrIfExists(attribute.GetName());
  if (attr_node) {
    DetachAttrNodeAtIndex(attr_node, index);
  } else {
    attr_node = MakeGarbageCollected<Attr>(GetDocument(), attribute.GetName(),
                                           attribute.Value());
    RemoveAttributeInternal(index, AttributeModificationReason::kDirectly);
  }
  return attr_node;
}

void Element::DetachAttrNodeAtIndex(Attr* attr, wtf_size_t index) {
  DCHECK(attr);
  DCHECK(GetElementData());

  const Attribute& attribute = GetElementData()->Attributes().at(index);
  DCHECK(attribute.GetName() == attr->GetQualifiedName());
  DetachAttrNodeFromElementWithValue(attr, attribute.Value());
  RemoveAttributeInternal(index, AttributeModificationReason::kDirectly);
}

void Element::removeAttribute(const QualifiedName& name) {
  wtf_size_t index = FindAttributeIndex(name);
  if (index == kNotFound)
    return;

  RemoveAttributeInternal(index, AttributeModificationReason::kDirectly);
}

void Element::SetBooleanAttribute(const QualifiedName& name, bool value) {
  if (value)
    setAttribute(name, g_empty_atom);
  else
    removeAttribute(name);
}

bool Element::HasExplicitlySetAttrAssociatedElements(
    const QualifiedName& name) {
  return GetExplicitlySetElementsForAttr(this, name);
}

void Element::SynchronizeContentAttributeAndElementReference(
    const QualifiedName& name) {
  ExplicitlySetAttrElementsMap* element_attribute_map =
      GetDocument().GetExplicitlySetAttrElementsMap(this);
  element_attribute_map->erase(name);
}

void Element::SetElementAttribute(const QualifiedName& name, Element* element) {
  ExplicitlySetAttrElementsMap* explicitly_set_attr_elements_map =
      GetDocument().GetExplicitlySetAttrElementsMap(this);

  // If the reflected element is explicitly null then we remove the content
  // attribute and the explicitly set attr-element.
  if (!element) {
    explicitly_set_attr_elements_map->erase(name);
    removeAttribute(name);
    return;
  }

  const AtomicString id = element->GetIdAttribute();

  // In order to sprout a non-empty content attribute from an explicitly set
  // attr-element, |element| must:
  //  1) have a valid ID attribute, and
  //  2) be the first element in tree order with this ID.
  // Otherwise the content attribute will reflect the empty string.
  //
  // Note that the explicitly set attr-element is still set. See the spec for
  // more details:
  // https://whatpr.org/html/3917/common-dom-interfaces.html#reflecting-content-attributes-in-idl-attributes
  if (id.IsNull() || GetTreeScope() != element->GetTreeScope() ||
      GetTreeScope().getElementById(id) != element)
    setAttribute(name, g_empty_atom);
  else
    setAttribute(name, id);

  auto result = explicitly_set_attr_elements_map->insert(name, nullptr);
  if (result.is_new_entry) {
    result.stored_value->value =
        MakeGarbageCollected<HeapLinkedHashSet<WeakMember<Element>>>();
  } else {
    result.stored_value->value->clear();
  }
  result.stored_value->value->insert(element);

  if (isConnected()) {
    if (AXObjectCache* cache = GetDocument().ExistingAXObjectCache())
      cache->HandleAttributeChanged(name, this);
  }
}

Element* Element::GetElementAttribute(const QualifiedName& name) {
  HeapLinkedHashSet<WeakMember<Element>>* element_attribute_vector =
      GetExplicitlySetElementsForAttr(this, name);
  if (element_attribute_vector) {
    DCHECK_EQ(element_attribute_vector->size(), 1u);
    Element* explicitly_set_element = *(element_attribute_vector->begin());
    DCHECK_NE(explicitly_set_element, nullptr);

    // Only return the explicit element if it still exists within a valid scope.
    if (!ElementIsDescendantOfShadowIncludingAncestor(*this,
                                                      *explicitly_set_element))
      return nullptr;

    return explicitly_set_element;
  }

  // Compute the attr-associated element from the content attribute if present,
  // id can be null.
  AtomicString id = getAttribute(name);
  if (id.IsNull())
    return nullptr;

  // Will return null if the id is empty.
  return GetTreeScope().getElementById(id);
}

void Element::SetElementArrayAttribute(
    const QualifiedName& name,
    const HeapVector<Member<Element>>* given_elements) {
  ExplicitlySetAttrElementsMap* element_attribute_map =
      GetDocument().GetExplicitlySetAttrElementsMap(this);

  if (!given_elements) {
    element_attribute_map->erase(name);
    removeAttribute(name);
    return;
  }

  // Get or create element array, and remove any pre-existing elements.
  //
  // Note that this code intentionally performs two look ups on |name| within
  // the map (one here, and one below with a call to |Set|).
  // This is needed as modifying the content attribute (|setAttribute|) will
  // run the synchronization steps which modify the map invalidating any
  // outstanding iterators.
  auto it = element_attribute_map->find(name);
  HeapLinkedHashSet<WeakMember<Element>>* stored_elements =
      it != element_attribute_map->end() ? it->value : nullptr;
  if (!stored_elements) {
    stored_elements =
        MakeGarbageCollected<HeapLinkedHashSet<WeakMember<Element>>>();
  } else {
    stored_elements->clear();
  }
  SpaceSplitString value;

  for (auto element : *given_elements) {
    // If |value| is null and |stored_elements| is non-empty, then a previous
    // element must have been invalid wrt. the content attribute string rules,
    // and therefore the content attribute string should reflect the empty
    // string. This means we can stop trying to compute the content attribute
    // string.
    if (value.IsNull() && !stored_elements->IsEmpty()) {
      stored_elements->insert(element);
      continue;
    }

    stored_elements->insert(element);
    const AtomicString given_element_id = element->GetIdAttribute();

    // We compute the content attribute string as a space separated string of
    // the given |element| ids. Every |element| in |given_elements| must have an
    // id, must be in the same tree scope and must be the first id in tree order
    // with that id, otherwise the content attribute should reflect the empty
    // string.
    if (given_element_id.IsNull() ||
        GetTreeScope() != element->GetTreeScope() ||
        GetTreeScope().getElementById(given_element_id) != element) {
      value.Clear();
      continue;
    }

    // Whitespace between elements is added when the string is serialized.
    value.Add(given_element_id);
  }

  setAttribute(name, value.SerializeToString());
  if (isConnected()) {
    if (AXObjectCache* cache = GetDocument().ExistingAXObjectCache())
      cache->HandleAttributeChanged(name, this);
  }

  // This |Set| call must occur after our call to |setAttribute| above.
  //
  // |setAttribute| will call through to |AttributeChanged| which calls
  // |SynchronizeContentAttributeAndElementReference| erasing the entry for
  // |name| from the map.
  element_attribute_map->Set(name, stored_elements);
}

HeapVector<Member<Element>>* Element::GetElementArrayAttribute(
    const QualifiedName& name) {
  HeapVector<Member<Element>>* result_elements =
      MakeGarbageCollected<HeapVector<Member<Element>>>();
  // TODO(chrishall): this will fail to preserve `e1.ariaFoo === e1.ariaFoo`,
  // need additional cache to preserve this invariant, add tests covering this
  // case.
  HeapLinkedHashSet<WeakMember<Element>>* explicitly_set_elements =
      GetExplicitlySetElementsForAttr(this, name);

  if (explicitly_set_elements) {
    for (auto attrElement : *explicitly_set_elements) {
      if (ElementIsDescendantOfShadowIncludingAncestor(*this, *attrElement))
        result_elements->push_back(attrElement);
    }
    return result_elements;
  }

  QualifiedName attr = name;

  // Account for labelled vs labeled spelling
  if (attr == html_names::kAriaLabelledbyAttr) {
    attr = hasAttribute(html_names::kAriaLabeledbyAttr) &&
                   !hasAttribute(html_names::kAriaLabelledbyAttr)
               ? html_names::kAriaLabeledbyAttr
               : html_names::kAriaLabelledbyAttr;
  }

  if (!hasAttribute(attr))
    return nullptr;

  String attribute_value = getAttribute(attr).GetString();
  Vector<String> tokens;
  attribute_value = attribute_value.SimplifyWhiteSpace();
  attribute_value.Split(' ', tokens);

  // Lookup each id within the same root.
  // Since this is based on ID we know it cannot cross shadow boundaries, so we
  // don't need to include additional logic to check that.
  for (auto id : tokens) {
    Element* candidate = GetTreeScope().getElementById(AtomicString(id));
    if (candidate)
      result_elements->push_back(candidate);
  }

  return result_elements;
}

NamedNodeMap* Element::attributesForBindings() const {
  ElementRareData& rare_data =
      const_cast<Element*>(this)->EnsureElementRareData();
  if (NamedNodeMap* attribute_map = rare_data.AttributeMap())
    return attribute_map;

  rare_data.SetAttributeMap(
      MakeGarbageCollected<NamedNodeMap>(const_cast<Element*>(this)));
  return rare_data.AttributeMap();
}

Vector<AtomicString> Element::getAttributeNames() const {
  Vector<AtomicString> attributesVector;
  if (!hasAttributes())
    return attributesVector;

  AttributeCollection attributes = element_data_->Attributes();
  attributesVector.ReserveInitialCapacity(attributes.size());
  for (const Attribute& attr : attributes)
    attributesVector.UncheckedAppend(attr.GetName().ToString());
  return attributesVector;
}

inline ElementRareData* Element::GetElementRareData() const {
  DCHECK(HasRareData());
  return static_cast<ElementRareData*>(RareData());
}

inline ElementRareData& Element::EnsureElementRareData() {
  return static_cast<ElementRareData&>(EnsureRareData());
}

inline void Element::SynchronizeAttribute(const QualifiedName& name) const {
  if (!GetElementData())
    return;
  if (UNLIKELY(name == html_names::kStyleAttr &&
               GetElementData()->style_attribute_is_dirty())) {
    DCHECK(IsStyledElement());
    SynchronizeStyleAttributeInternal();
    return;
  }
  if (UNLIKELY(GetElementData()->svg_attributes_are_dirty())) {
    // See comment in the AtomicString version of SynchronizeAttribute()
    // also.
    To<SVGElement>(this)->SynchronizeSVGAttribute(name);
  }
}

ElementAnimations* Element::GetElementAnimations() const {
  if (HasRareData())
    return GetElementRareData()->GetElementAnimations();
  return nullptr;
}

ElementAnimations& Element::EnsureElementAnimations() {
  ElementRareData& rare_data = EnsureElementRareData();
  if (!rare_data.GetElementAnimations())
    rare_data.SetElementAnimations(MakeGarbageCollected<ElementAnimations>());
  return *rare_data.GetElementAnimations();
}

bool Element::HasAnimations() const {
  if (!HasRareData())
    return false;

  ElementAnimations* element_animations =
      GetElementRareData()->GetElementAnimations();
  return element_animations && !element_animations->IsEmpty();
}

Node::NodeType Element::getNodeType() const {
  return kElementNode;
}

bool Element::hasAttribute(const QualifiedName& name) const {
  return hasAttributeNS(name.NamespaceURI(), name.LocalName());
}

bool Element::HasAttributeIgnoringNamespace(
    const AtomicString& local_name) const {
  if (!GetElementData())
    return false;
  WTF::AtomicStringTable::WeakResult hint =
      WeakLowercaseIfNecessary(local_name);
  SynchronizeAttributeHinted(local_name, hint);
  if (hint.IsNull()) {
    return false;
  }
  for (const Attribute& attribute : GetElementData()->Attributes()) {
    if (hint == attribute.LocalName())
      return true;
  }
  return false;
}

void Element::SynchronizeAllAttributes() const {
  if (!GetElementData())
    return;
  // NOTE: AnyAttributeMatches in selector_checker.cc currently assumes that all
  // lazy attributes have a null namespace.  If that ever changes we'll need to
  // fix that code.
  if (GetElementData()->style_attribute_is_dirty()) {
    DCHECK(IsStyledElement());
    SynchronizeStyleAttributeInternal();
  }
  SynchronizeAllAttributesExceptStyle();
}

void Element::SynchronizeAllAttributesExceptStyle() const {
  if (!GetElementData())
    return;
  if (GetElementData()->svg_attributes_are_dirty())
    To<SVGElement>(this)->SynchronizeSVGAttribute(AnyQName());
}

const AtomicString& Element::getAttribute(const QualifiedName& name) const {
  if (!GetElementData())
    return g_null_atom;
  SynchronizeAttribute(name);
  if (const Attribute* attribute = GetElementData()->Attributes().Find(name))
    return attribute->Value();
  return g_null_atom;
}

AtomicString Element::LowercaseIfNecessary(AtomicString name) const {
  return IsHTMLElement() && IsA<HTMLDocument>(GetDocument())
             ? AtomicString::LowerASCII(std::move(name))
             : std::move(name);
}

const AtomicString& Element::nonce() const {
  return HasRareData() ? GetElementRareData()->GetNonce() : g_null_atom;
}

void Element::setNonce(const AtomicString& nonce) {
  EnsureElementRareData().SetNonce(nonce);
}

void Element::scrollIntoView(const V8UnionBooleanOrScrollIntoViewOptions* arg) {
  ScrollIntoViewOptions* options = nullptr;
  switch (arg->GetContentType()) {
    case V8UnionBooleanOrScrollIntoViewOptions::ContentType::kBoolean:
      options = ScrollIntoViewOptions::Create();
      options->setBlock(arg->GetAsBoolean() ? "start" : "end");
      options->setInlinePosition("nearest");
      break;
    case V8UnionBooleanOrScrollIntoViewOptions::ContentType::
        kScrollIntoViewOptions:
      options = arg->GetAsScrollIntoViewOptions();
      break;
  }
  DCHECK(options);
  scrollIntoViewWithOptions(options);
}

void Element::scrollIntoView(bool align_to_top) {
  auto* arg =
      MakeGarbageCollected<V8UnionBooleanOrScrollIntoViewOptions>(align_to_top);
  scrollIntoView(arg);
}

void Element::scrollIntoViewWithOptions(const ScrollIntoViewOptions* options) {
  ActivateDisplayLockIfNeeded(DisplayLockActivationReason::kScrollIntoView);
  GetDocument().EnsurePaintLocationDataValidForNode(
      this, DocumentUpdateReason::kJavaScript);

  if (!GetLayoutObject() || !GetDocument().GetPage())
    return;

  mojom::blink::ScrollIntoViewParamsPtr params =
      ScrollAlignment::CreateScrollIntoViewParams(*options,
                                                  *GetComputedStyle());

  ScrollIntoViewNoVisualUpdate(std::move(params));
}

void Element::ScrollIntoViewNoVisualUpdate(
    mojom::blink::ScrollIntoViewParamsPtr params) {
  if (!GetLayoutObject() || !GetDocument().GetPage())
    return;

  if (DisplayLockUtilities::ShouldIgnoreNodeDueToDisplayLock(
          *this, DisplayLockActivationReason::kScrollIntoView)) {
    return;
  }

  PhysicalRect bounds = BoundingBoxForScrollIntoView();
  scroll_into_view_util::ScrollRectToVisible(*GetLayoutObject(), bounds,
                                             std::move(params));

  GetDocument().SetSequentialFocusNavigationStartingPoint(this);
}

void Element::scrollIntoViewIfNeeded(bool center_if_needed) {
  GetDocument().EnsurePaintLocationDataValidForNode(
      this, DocumentUpdateReason::kJavaScript);

  if (!GetLayoutObject())
    return;

  PhysicalRect bounds = BoundingBoxForScrollIntoView();
  if (center_if_needed) {
    scroll_into_view_util::ScrollRectToVisible(
        *GetLayoutObject(), bounds,
        ScrollAlignment::CreateScrollIntoViewParams(
            ScrollAlignment::CenterIfNeeded(),
            ScrollAlignment::CenterIfNeeded()));
  } else {
    scroll_into_view_util::ScrollRectToVisible(
        *GetLayoutObject(), bounds,
        ScrollAlignment::CreateScrollIntoViewParams(
            ScrollAlignment::ToEdgeIfNeeded(),
            ScrollAlignment::ToEdgeIfNeeded()));
  }
}

int Element::OffsetLeft() {
  GetDocument().EnsurePaintLocationDataValidForNode(
      this, DocumentUpdateReason::kJavaScript);
  if (const auto* layout_object = GetLayoutBoxModelObject()) {
    return AdjustForAbsoluteZoom::AdjustLayoutUnit(
               layout_object->OffsetLeft(OffsetParent()),
               layout_object->StyleRef())
        .Round();
  }
  return 0;
}

int Element::OffsetTop() {
  GetDocument().EnsurePaintLocationDataValidForNode(
      this, DocumentUpdateReason::kJavaScript);
  if (const auto* layout_object = GetLayoutBoxModelObject()) {
    return AdjustForAbsoluteZoom::AdjustLayoutUnit(
               layout_object->OffsetTop(OffsetParent()),
               layout_object->StyleRef())
        .Round();
  }
  return 0;
}

int Element::OffsetWidth() {
  GetDocument().EnsurePaintLocationDataValidForNode(
      this, DocumentUpdateReason::kJavaScript);
  if (const auto* layout_object = GetLayoutBoxModelObject()) {
    return AdjustForAbsoluteZoom::AdjustLayoutUnit(layout_object->OffsetWidth(),
                                                   layout_object->StyleRef())
        .Round();
  }
  return 0;
}

int Element::OffsetHeight() {
  GetDocument().EnsurePaintLocationDataValidForNode(
      this, DocumentUpdateReason::kJavaScript);
  if (const auto* layout_object = GetLayoutBoxModelObject()) {
    return AdjustForAbsoluteZoom::AdjustLayoutUnit(
               layout_object->OffsetHeight(), layout_object->StyleRef())
        .Round();
  }
  return 0;
}

Element* Element::OffsetParent() {
  GetDocument().UpdateStyleAndLayoutForNode(this,
                                            DocumentUpdateReason::kJavaScript);

  LayoutObject* layout_object = GetLayoutObject();
  return layout_object ? layout_object->OffsetParent() : nullptr;
}

int Element::clientLeft() {
  GetDocument().UpdateStyleAndLayoutForNode(this,
                                            DocumentUpdateReason::kJavaScript);

  if (const auto* layout_object = GetLayoutBox()) {
    return AdjustForAbsoluteZoom::AdjustLayoutUnit(layout_object->ClientLeft(),
                                                   layout_object->StyleRef())
        .Round();
  }
  return 0;
}

int Element::clientTop() {
  GetDocument().UpdateStyleAndLayoutForNode(this,
                                            DocumentUpdateReason::kJavaScript);

  if (const auto* layout_object = GetLayoutBox()) {
    return AdjustForAbsoluteZoom::AdjustLayoutUnit(layout_object->ClientTop(),
                                                   layout_object->StyleRef())
        .Round();
  }
  return 0;
}

void Element::SaveIntrinsicSize(ResizeObserverSize* size) {
  EnsureElementRareData().SaveLastIntrinsicSize(size);
}

const ResizeObserverSize* Element::LastIntrinsicSize() const {
  if (!HasRareData())
    return nullptr;
  // If rare data exists, we are guaranteed that it's ElementRareData.
  ElementRareData* data = GetElementRareData();
  DCHECK(data);
  return data->LastIntrinsicSize();
}

bool Element::IsViewportScrollElement() {
  auto& document = GetDocument();
  bool quirks_mode = document.InQuirksMode();
  return (!quirks_mode && document.documentElement() == this) ||
         (quirks_mode && IsHTMLElement() && document.body() == this);
}

void Element::RecordScrollbarSizeForStudy(int measurement,
                                          bool is_width,
                                          bool is_offset) {
  if (!IdentifiabilityStudySettings::Get()->ShouldSampleType(
          IdentifiableSurface::Type::kScrollbarSize) ||
      (!is_offset && !IsViewportScrollElement()))
    return;

  // Check for presence of a scrollbar.
  PaintLayerScrollableArea* area;
  if (IsViewportScrollElement()) {
    auto* view = GetDocument().View();
    if (!view)
      return;
    area = view->LayoutViewport();
  } else {
    auto* layout = GetLayoutBox();
    if (!layout)
      return;
    area = layout->GetScrollableArea();
  }
  if (!area || area->HasOverlayOverflowControls())
    return;

  Scrollbar* scrollbar =
      is_width ? area->VerticalScrollbar() : area->HorizontalScrollbar();
  // We intentionally exclude platform overlay scrollbars since their size
  // cannot be detected in JavaScript using the methods below.
  if (!scrollbar)
    return;

  IdentifiableSurface::ScrollbarSurface surface;
  int scrollbar_size;

  // There are two common ways to detect the size of a scrollbar in a DOM
  // window. They are:
  // 1. Compute the difference of the window.inner[Width|Height] and the
  //    corresponding document.scrollingElement.offset[Width|Height].
  // 2. Any HTML element that insets the layout to fit a scrollbar, so it is
  //    measurable by a JavaScript program on a site.
  if (IsViewportScrollElement()) {
    LocalDOMWindow* dom_window = GetDocument().domWindow();
    scrollbar_size =
        (is_width ? dom_window->innerWidth() : dom_window->innerHeight()) -
        measurement;
    surface =
        is_width
            ? IdentifiableSurface::ScrollbarSurface::kScrollingElementWidth
            : IdentifiableSurface::ScrollbarSurface::kScrollingElementHeight;
  } else {
    scrollbar_size = measurement - (is_width ? clientWidth() : clientHeight());
    surface = is_width
                  ? IdentifiableSurface::ScrollbarSurface::kElemScrollbarWidth
                  : IdentifiableSurface::ScrollbarSurface::kElemScrollbarHeight;
  }

  blink::IdentifiabilityMetricBuilder(GetDocument().UkmSourceID())
      .Add(blink::IdentifiableSurface::FromTypeAndToken(
               blink::IdentifiableSurface::Type::kScrollbarSize, surface),
           scrollbar_size)
      .Record(GetDocument().UkmRecorder());
}

int Element::clientWidth() {
  // When in strict mode, clientWidth for the document element should return the
  // width of the containing frame.
  // When in quirks mode, clientWidth for the body element should return the
  // width of the containing frame.
  if (IsViewportScrollElement()) {
    auto* layout_view = GetDocument().GetLayoutView();
    if (layout_view) {
      // TODO(crbug.com/740879): Use per-page overlay scrollbar settings.
      if (!ScrollbarThemeSettings::OverlayScrollbarsEnabled() ||
          !GetDocument().GetFrame()->IsLocalRoot()) {
        GetDocument().UpdateStyleAndLayoutForNode(
            this, DocumentUpdateReason::kJavaScript);
      }
      if (GetDocument().GetPage()->GetSettings().GetForceZeroLayoutHeight()) {
        // OverflowClipRect() may return infinite along a particular axis if
        // |layout_view| is not a scroll-container.
        DCHECK(layout_view->IsScrollContainer());
        int result =
            AdjustForAbsoluteZoom::AdjustLayoutUnit(
                layout_view->OverflowClipRect(PhysicalOffset()).Width(),
                layout_view->StyleRef())
                .Round();
        RecordScrollbarSizeForStudy(result, /* is_width= */ true,
                                    /* is_offset= */ false);
        return result;
      }
      int result = AdjustForAbsoluteZoom::AdjustInt(
          layout_view->GetLayoutSize().width(), layout_view->StyleRef());
      RecordScrollbarSizeForStudy(result, /* is_width= */ true,
                                  /* is_offset= */ false);
      return result;
    }
  }

  GetDocument().UpdateStyleAndLayoutForNode(this,
                                            DocumentUpdateReason::kJavaScript);

  int result = 0;
  if (const auto* layout_object = GetLayoutBox()) {
    result = AdjustForAbsoluteZoom::AdjustLayoutUnit(
                 layout_object->ClientWidthWithTableSpecialBehavior(),
                 layout_object->StyleRef())
                 .Round();
    RecordScrollbarSizeForStudy(result, /* is_width= */ true,
                                /* is_offset= */ false);
  }
  return result;
}

int Element::clientHeight() {
  // When in strict mode, clientHeight for the document element should return
  // the height of the containing frame.
  // When in quirks mode, clientHeight for the body element should return the
  // height of the containing frame.
  if (IsViewportScrollElement()) {
    auto* layout_view = GetDocument().GetLayoutView();
    if (layout_view) {
      // TODO(crbug.com/740879): Use per-page overlay scrollbar settings.
      if (!ScrollbarThemeSettings::OverlayScrollbarsEnabled() ||
          !GetDocument().GetFrame()->IsLocalRoot()) {
        GetDocument().UpdateStyleAndLayoutForNode(
            this, DocumentUpdateReason::kJavaScript);
      }
      if (GetDocument().GetPage()->GetSettings().GetForceZeroLayoutHeight()) {
        // OverflowClipRect() may return infinite along a particular axis if
        // |layout_view| is not a scroll-container.
        DCHECK(layout_view->IsScrollContainer());
        int result =
            AdjustForAbsoluteZoom::AdjustLayoutUnit(
                layout_view->OverflowClipRect(PhysicalOffset()).Height(),
                layout_view->StyleRef())
                .Round();
        RecordScrollbarSizeForStudy(result, /* is_width= */ false,
                                    /* is_offset= */ false);
        return result;
      }
      int result = AdjustForAbsoluteZoom::AdjustInt(
          layout_view->GetLayoutSize().height(), layout_view->StyleRef());
      RecordScrollbarSizeForStudy(result, /* is_width= */ false,
                                  /* is_offset= */ false);
      return result;
    }
  }

  GetDocument().UpdateStyleAndLayoutForNode(this,
                                            DocumentUpdateReason::kJavaScript);

  int result = 0;
  if (const auto* layout_object = GetLayoutBox()) {
    result = AdjustForAbsoluteZoom::AdjustLayoutUnit(
                 layout_object->ClientHeightWithTableSpecialBehavior(),
                 layout_object->StyleRef())
                 .Round();
    RecordScrollbarSizeForStudy(result, /* is_width= */ false,
                                /* is_offset= */ false);
  }
  return result;
}

LayoutBox* Element::GetLayoutBoxForScrolling() const {
  LayoutBox* box = GetLayoutBox();
  if (!box || !box->IsScrollContainer()) {
    return nullptr;
  }
  return box;
}

double Element::scrollLeft() {
  if (!InActiveDocument())
    return 0;

  GetDocument().UpdateStyleAndLayoutForNode(this,
                                            DocumentUpdateReason::kJavaScript);

  if (GetDocument().ScrollingElementNoLayout() == this) {
    if (GetDocument().domWindow())
      return GetDocument().domWindow()->scrollX();
    return 0;
  }

  LayoutBox* box = GetLayoutBoxForScrolling();
  if (!box)
    return 0;
  if (PaintLayerScrollableArea* scrollable_area = box->GetScrollableArea()) {
    DCHECK(GetLayoutBox());

    if (HasLeftwardDirection(*this)) {
      UseCounter::Count(
          GetDocument(),
          WebFeature::
              kElementWithLeftwardOrUpwardOverflowDirection_ScrollLeftOrTop);
    }

    return AdjustForAbsoluteZoom::AdjustScroll(
        scrollable_area->GetScrollOffset().x(), *GetLayoutBox());
  }

  return 0;
}

double Element::scrollTop() {
  if (!InActiveDocument())
    return 0;

  GetDocument().UpdateStyleAndLayoutForNode(this,
                                            DocumentUpdateReason::kJavaScript);

  if (GetDocument().ScrollingElementNoLayout() == this) {
    if (GetDocument().domWindow())
      return GetDocument().domWindow()->scrollY();
    return 0;
  }

  // Don't disclose scroll position in preview state. See crbug.com/1261689.
  auto* select_element = DynamicTo<HTMLSelectElement>(this);
  if (select_element && !select_element->UsesMenuList() &&
      select_element->GetAutofillState() == WebAutofillState::kPreviewed) {
    return 0;
  }

  LayoutBox* box = GetLayoutBoxForScrolling();
  if (!box)
    return 0;
  if (PaintLayerScrollableArea* scrollable_area = box->GetScrollableArea()) {
    DCHECK(GetLayoutBox());

    if (HasUpwardDirection(*this)) {
      UseCounter::Count(
          GetDocument(),
          WebFeature::
              kElementWithLeftwardOrUpwardOverflowDirection_ScrollLeftOrTop);
    }

    return AdjustForAbsoluteZoom::AdjustScroll(
        scrollable_area->GetScrollOffset().y(), *GetLayoutBox());
  }

  return 0;
}

void Element::setScrollLeft(double new_left) {
  if (!InActiveDocument())
    return;

  GetDocument().UpdateStyleAndLayoutForNode(this,
                                            DocumentUpdateReason::kJavaScript);

  new_left = ScrollableArea::NormalizeNonFiniteScroll(new_left);

  if (GetDocument().ScrollingElementNoLayout() == this) {
    if (LocalDOMWindow* window = GetDocument().domWindow()) {
      ScrollToOptions* options = ScrollToOptions::Create();
      options->setLeft(new_left);
      window->scrollTo(options);
    }
    return;
  }

  LayoutBox* box = GetLayoutBoxForScrolling();
  if (!box)
    return;
  if (PaintLayerScrollableArea* scrollable_area = box->GetScrollableArea()) {
    if (HasLeftwardDirection(*this)) {
      UseCounter::Count(
          GetDocument(),
          WebFeature::
              kElementWithLeftwardOrUpwardOverflowDirection_ScrollLeftOrTop);
      if (new_left > 0) {
        UseCounter::Count(
            GetDocument(),
            WebFeature::
                kElementWithLeftwardOrUpwardOverflowDirection_ScrollLeftOrTopSetPositive);
      }
    }

    ScrollOffset end_offset(new_left * box->Style()->EffectiveZoom(),
                            scrollable_area->GetScrollOffset().y());
    std::unique_ptr<cc::SnapSelectionStrategy> strategy =
        cc::SnapSelectionStrategy::CreateForEndPosition(
            scrollable_area->ScrollOffsetToPosition(end_offset), true, false);
    absl::optional<gfx::PointF> snap_point =
        scrollable_area->GetSnapPositionAndSetTarget(*strategy);
    if (snap_point.has_value()) {
      end_offset = scrollable_area->ScrollPositionToOffset(snap_point.value());
    }
    scrollable_area->SetScrollOffset(end_offset,
                                     mojom::blink::ScrollType::kProgrammatic,
                                     mojom::blink::ScrollBehavior::kAuto);
  }
}

void Element::setScrollTop(double new_top) {
  if (!InActiveDocument())
    return;

  GetDocument().UpdateStyleAndLayoutForNode(this,
                                            DocumentUpdateReason::kJavaScript);

  new_top = ScrollableArea::NormalizeNonFiniteScroll(new_top);

  if (GetDocument().ScrollingElementNoLayout() == this) {
    if (LocalDOMWindow* window = GetDocument().domWindow()) {
      ScrollToOptions* options = ScrollToOptions::Create();
      options->setTop(new_top);
      window->scrollTo(options);
    }
    return;
  }

  LayoutBox* box = GetLayoutBoxForScrolling();
  if (!box)
    return;
  if (PaintLayerScrollableArea* scrollable_area = box->GetScrollableArea()) {
    if (HasUpwardDirection(*this)) {
      UseCounter::Count(
          GetDocument(),
          WebFeature::
              kElementWithLeftwardOrUpwardOverflowDirection_ScrollLeftOrTop);
      if (new_top > 0) {
        UseCounter::Count(
            GetDocument(),
            WebFeature::
                kElementWithLeftwardOrUpwardOverflowDirection_ScrollLeftOrTopSetPositive);
      }
    }

    ScrollOffset end_offset(scrollable_area->GetScrollOffset().x(),
                            new_top * box->Style()->EffectiveZoom());
    std::unique_ptr<cc::SnapSelectionStrategy> strategy =
        cc::SnapSelectionStrategy::CreateForEndPosition(
            scrollable_area->ScrollOffsetToPosition(end_offset), false, true);
    absl::optional<gfx::PointF> snap_point =
        scrollable_area->GetSnapPositionAndSetTarget(*strategy);
    if (snap_point.has_value()) {
      end_offset = scrollable_area->ScrollPositionToOffset(snap_point.value());
    }

    scrollable_area->SetScrollOffset(end_offset,
                                     mojom::blink::ScrollType::kProgrammatic,
                                     mojom::blink::ScrollBehavior::kAuto);
  }
}

int Element::scrollWidth() {
  if (!InActiveDocument())
    return 0;

  GetDocument().UpdateStyleAndLayoutForNode(this,
                                            DocumentUpdateReason::kJavaScript);

  if (GetDocument().ScrollingElementNoLayout() == this) {
    if (GetDocument().View()) {
      return AdjustForAbsoluteZoom::AdjustInt(
          GetDocument().View()->LayoutViewport()->ContentsSize().width(),
          GetDocument().GetFrame()->PageZoomFactor());
    }
    return 0;
  }

  if (LayoutBox* box = GetLayoutBox()) {
    return AdjustForAbsoluteZoom::AdjustLayoutUnit(box->ScrollWidth(), *box)
        .Round();
  }
  return 0;
}

int Element::scrollHeight() {
  if (!InActiveDocument())
    return 0;

  GetDocument().UpdateStyleAndLayoutForNode(this,
                                            DocumentUpdateReason::kJavaScript);

  if (GetDocument().ScrollingElementNoLayout() == this) {
    if (GetDocument().View()) {
      return AdjustForAbsoluteZoom::AdjustInt(
          GetDocument().View()->LayoutViewport()->ContentsSize().height(),
          GetDocument().GetFrame()->PageZoomFactor());
    }
    return 0;
  }

  if (LayoutBox* box = GetLayoutBox()) {
    return AdjustForAbsoluteZoom::AdjustLayoutUnit(box->ScrollHeight(), *box)
        .Round();
  }
  return 0;
}

void Element::scrollBy(double x, double y) {
  ScrollToOptions* scroll_to_options = ScrollToOptions::Create();
  scroll_to_options->setLeft(x);
  scroll_to_options->setTop(y);
  scrollBy(scroll_to_options);
}

void Element::scrollBy(const ScrollToOptions* scroll_to_options) {
  if (!InActiveDocument())
    return;

  // FIXME: This should be removed once scroll updates are processed only after
  // the compositing update. See http://crbug.com/420741.
  GetDocument().UpdateStyleAndLayoutForNode(this,
                                            DocumentUpdateReason::kJavaScript);

  if (GetDocument().ScrollingElementNoLayout() == this) {
    ScrollFrameBy(scroll_to_options);
  } else {
    ScrollLayoutBoxBy(scroll_to_options);
  }
}

void Element::scrollTo(double x, double y) {
  ScrollToOptions* scroll_to_options = ScrollToOptions::Create();
  scroll_to_options->setLeft(x);
  scroll_to_options->setTop(y);
  scrollTo(scroll_to_options);
}

void Element::scrollTo(const ScrollToOptions* scroll_to_options) {
  if (!InActiveDocument())
    return;

  // FIXME: This should be removed once scroll updates are processed only after
  // the compositing update. See http://crbug.com/420741.
  GetDocument().UpdateStyleAndLayoutForNode(this,
                                            DocumentUpdateReason::kJavaScript);

  if (GetDocument().ScrollingElementNoLayout() == this) {
    ScrollFrameTo(scroll_to_options);
  } else {
    ScrollLayoutBoxTo(scroll_to_options);
  }
}

void Element::ScrollLayoutBoxBy(const ScrollToOptions* scroll_to_options) {
  gfx::Vector2dF displacement;
  if (scroll_to_options->hasLeft()) {
    displacement.set_x(
        ScrollableArea::NormalizeNonFiniteScroll(scroll_to_options->left()));
  }
  if (scroll_to_options->hasTop()) {
    displacement.set_y(
        ScrollableArea::NormalizeNonFiniteScroll(scroll_to_options->top()));
  }

  mojom::blink::ScrollBehavior scroll_behavior =
      mojom::blink::ScrollBehavior::kAuto;
  ScrollableArea::ScrollBehaviorFromString(scroll_to_options->behavior(),
                                           scroll_behavior);
  LayoutBox* box = GetLayoutBoxForScrolling();
  if (!box)
    return;
  if (PaintLayerScrollableArea* scrollable_area = box->GetScrollableArea()) {
    DCHECK(box);
    gfx::PointF current_position(scrollable_area->ScrollPosition().x(),
                                 scrollable_area->ScrollPosition().y());
    displacement.Scale(box->Style()->EffectiveZoom());
    gfx::PointF new_position = current_position + displacement;

    std::unique_ptr<cc::SnapSelectionStrategy> strategy =
        cc::SnapSelectionStrategy::CreateForEndAndDirection(
            current_position, displacement,
            RuntimeEnabledFeatures::FractionalScrollOffsetsEnabled());
    new_position =
        scrollable_area->GetSnapPositionAndSetTarget(*strategy).value_or(
            new_position);
    scrollable_area->ScrollToAbsolutePosition(new_position, scroll_behavior);
  }
}

void Element::ScrollLayoutBoxTo(const ScrollToOptions* scroll_to_options) {
  mojom::blink::ScrollBehavior scroll_behavior =
      mojom::blink::ScrollBehavior::kAuto;
  ScrollableArea::ScrollBehaviorFromString(scroll_to_options->behavior(),
                                           scroll_behavior);

  LayoutBox* box = GetLayoutBoxForScrolling();
  if (!box)
    return;
  if (PaintLayerScrollableArea* scrollable_area = box->GetScrollableArea()) {
    if (scroll_to_options->hasLeft() && HasLeftwardDirection(*this)) {
      UseCounter::Count(
          GetDocument(),
          WebFeature::
              kElementWithLeftwardOrUpwardOverflowDirection_ScrollLeftOrTop);
      if (scroll_to_options->left() > 0) {
        UseCounter::Count(
            GetDocument(),
            WebFeature::
                kElementWithLeftwardOrUpwardOverflowDirection_ScrollLeftOrTopSetPositive);
      }
    }
    if (scroll_to_options->hasTop() && HasUpwardDirection(*this)) {
      UseCounter::Count(
          GetDocument(),
          WebFeature::
              kElementWithLeftwardOrUpwardOverflowDirection_ScrollLeftOrTop);
      if (scroll_to_options->top() > 0) {
        UseCounter::Count(
            GetDocument(),
            WebFeature::
                kElementWithLeftwardOrUpwardOverflowDirection_ScrollLeftOrTopSetPositive);
      }
    }

    ScrollOffset new_offset = scrollable_area->GetScrollOffset();
    if (scroll_to_options->hasLeft()) {
      new_offset.set_x(
          ScrollableArea::NormalizeNonFiniteScroll(scroll_to_options->left()) *
          box->Style()->EffectiveZoom());
    }
    if (scroll_to_options->hasTop()) {
      new_offset.set_y(
          ScrollableArea::NormalizeNonFiniteScroll(scroll_to_options->top()) *
          box->Style()->EffectiveZoom());
    }

    std::unique_ptr<cc::SnapSelectionStrategy> strategy =
        cc::SnapSelectionStrategy::CreateForEndPosition(
            scrollable_area->ScrollOffsetToPosition(new_offset),
            scroll_to_options->hasLeft(), scroll_to_options->hasTop());
    absl::optional<gfx::PointF> snap_point =
        scrollable_area->GetSnapPositionAndSetTarget(*strategy);
    if (snap_point.has_value()) {
      new_offset = scrollable_area->ScrollPositionToOffset(snap_point.value());
    }

    scrollable_area->SetScrollOffset(
        new_offset, mojom::blink::ScrollType::kProgrammatic, scroll_behavior);
  }
}

void Element::ScrollFrameBy(const ScrollToOptions* scroll_to_options) {
  gfx::Vector2dF displacement;
  if (scroll_to_options->hasLeft()) {
    displacement.set_x(
        ScrollableArea::NormalizeNonFiniteScroll(scroll_to_options->left()));
  }
  if (scroll_to_options->hasTop()) {
    displacement.set_y(
        ScrollableArea::NormalizeNonFiniteScroll(scroll_to_options->top()));
  }

  mojom::blink::ScrollBehavior scroll_behavior =
      mojom::blink::ScrollBehavior::kAuto;
  ScrollableArea::ScrollBehaviorFromString(scroll_to_options->behavior(),
                                           scroll_behavior);
  LocalFrame* frame = GetDocument().GetFrame();
  if (!frame || !frame->View() || !GetDocument().GetPage())
    return;

  ScrollableArea* viewport = frame->View()->LayoutViewport();
  if (!viewport)
    return;

  displacement.Scale(frame->PageZoomFactor());
  gfx::PointF new_position = viewport->ScrollPosition() + displacement;
  gfx::PointF current_position = viewport->ScrollPosition();
  std::unique_ptr<cc::SnapSelectionStrategy> strategy =
      cc::SnapSelectionStrategy::CreateForEndAndDirection(
          current_position, displacement,
          RuntimeEnabledFeatures::FractionalScrollOffsetsEnabled());
  new_position =
      viewport->GetSnapPositionAndSetTarget(*strategy).value_or(new_position);
  viewport->SetScrollOffset(viewport->ScrollPositionToOffset(new_position),
                            mojom::blink::ScrollType::kProgrammatic,
                            scroll_behavior);
}

void Element::ScrollFrameTo(const ScrollToOptions* scroll_to_options) {
  mojom::blink::ScrollBehavior scroll_behavior =
      mojom::blink::ScrollBehavior::kAuto;
  ScrollableArea::ScrollBehaviorFromString(scroll_to_options->behavior(),
                                           scroll_behavior);
  LocalFrame* frame = GetDocument().GetFrame();
  if (!frame || !frame->View() || !GetDocument().GetPage())
    return;

  ScrollableArea* viewport = frame->View()->LayoutViewport();
  if (!viewport)
    return;

  ScrollOffset new_offset = viewport->GetScrollOffset();
  if (scroll_to_options->hasLeft()) {
    new_offset.set_x(
        ScrollableArea::NormalizeNonFiniteScroll(scroll_to_options->left()) *
        frame->PageZoomFactor());
  }
  if (scroll_to_options->hasTop()) {
    new_offset.set_y(
        ScrollableArea::NormalizeNonFiniteScroll(scroll_to_options->top()) *
        frame->PageZoomFactor());
  }

  gfx::PointF new_position = viewport->ScrollOffsetToPosition(new_offset);
  std::unique_ptr<cc::SnapSelectionStrategy> strategy =
      cc::SnapSelectionStrategy::CreateForEndPosition(
          new_position, scroll_to_options->hasLeft(),
          scroll_to_options->hasTop());
  new_position =
      viewport->GetSnapPositionAndSetTarget(*strategy).value_or(new_position);
  new_offset = viewport->ScrollPositionToOffset(new_position);
  viewport->SetScrollOffset(new_offset, mojom::blink::ScrollType::kProgrammatic,
                            scroll_behavior);
}

gfx::Rect Element::BoundsInViewport() const {
  GetDocument().EnsurePaintLocationDataValidForNode(
      this, DocumentUpdateReason::kUnknown);

  LocalFrameView* view = GetDocument().View();
  if (!view)
    return gfx::Rect();

  Vector<gfx::QuadF> quads;

  // TODO(pdr): Unify the quad/bounds code with Element::ClientQuads.

  // Foreign objects need to convert between SVG and HTML coordinate spaces and
  // cannot use LocalToAbsoluteQuad directly with ObjectBoundingBox which is
  // SVG coordinates and not HTML coordinates. Instead, use the AbsoluteQuads
  // codepath below.
  auto* svg_element = DynamicTo<SVGElement>(this);
  if (svg_element && GetLayoutObject() &&
      !GetLayoutObject()->IsSVGForeignObjectIncludingNG()) {
    // Get the bounding rectangle from the SVG model.
    // TODO(pdr): This should include stroke.
    if (IsA<SVGGraphicsElement>(svg_element)) {
      quads.push_back(GetLayoutObject()->LocalToAbsoluteQuad(
          gfx::QuadF(GetLayoutObject()->ObjectBoundingBox())));
    }
  } else {
    // Get the bounding rectangle from the box model.
    if (GetLayoutBoxModelObject())
      GetLayoutBoxModelObject()->AbsoluteQuads(quads);
  }

  if (quads.IsEmpty())
    return gfx::Rect();

  gfx::RectF result;
  for (auto& quad : quads)
    result.Union(quad.BoundingBox());

  return view->FrameToViewport(gfx::ToEnclosingRect(result));
}

Vector<gfx::Rect> Element::OutlineRectsInVisualViewport(
    DocumentUpdateReason reason) const {
  Vector<gfx::Rect> rects;

  LocalFrameView* view = GetDocument().View();
  if (!view)
    return rects;

  GetDocument().EnsurePaintLocationDataValidForNode(this, reason);

  LayoutBoxModelObject* layout_object = GetLayoutBoxModelObject();
  if (!layout_object)
    return rects;

  Vector<PhysicalRect> outline_rects = layout_object->OutlineRects(
      nullptr, PhysicalOffset(),
      layout_object->StyleRef().OutlineRectsShouldIncludeBlockVisualOverflow());
  for (auto& r : outline_rects) {
    PhysicalRect physical_rect = layout_object->LocalToAbsoluteRect(r);
    gfx::Rect absolute_rect =
        view->FrameToViewport(ToPixelSnappedRect(physical_rect));
    rects.push_back(absolute_rect);
  }

  return rects;
}

gfx::Rect Element::VisibleBoundsInVisualViewport() const {
  if (!GetLayoutObject() || !GetDocument().GetPage() ||
      !GetDocument().GetFrame())
    return gfx::Rect();

  // We don't use absoluteBoundingBoxRect() because it can return an gfx::Rect
  // larger the actual size by 1px. crbug.com/470503
  PhysicalRect rect(
      gfx::ToRoundedRect(GetLayoutObject()->AbsoluteBoundingBoxRectF()));
  PhysicalRect frame_clip_rect =
      GetDocument().View()->GetLayoutView()->ClippingRect(PhysicalOffset());
  rect.Intersect(frame_clip_rect);

  // MapToVisualRectInAncestorSpace, called with a null ancestor argument,
  // returns the viewport-visible rect in the root frame's coordinate space.
  // MapToVisualRectInAncestorSpace applies ancestors' frame's clipping but does
  // not apply (overflow) element clipping.
  GetDocument().View()->GetLayoutView()->MapToVisualRectInAncestorSpace(nullptr,
                                                                        rect);

  // TODO(layout-dev): Callers of this method don't expect the offset of the
  // local frame root from a remote top-level frame to be applied here. They
  // expect the result to be in the coordinate system of the local root frame.
  // Either the method should be renamed to something which communicates that,
  // or callers should be updated to expect actual top-level frame coordinates.
  rect = GetDocument()
             .GetFrame()
             ->LocalFrameRoot()
             .ContentLayoutObject()
             ->AbsoluteToLocalRect(rect, kTraverseDocumentBoundaries |
                                             kApplyRemoteMainFrameTransform);

  gfx::Rect visible_rect = ToPixelSnappedRect(rect);
  // If the rect is in the coordinates of the main frame, then it should
  // also be clipped to the viewport to account for page scale. For OOPIFs,
  // local frame root -> viewport coordinate conversion is done in the
  // browser process.
  if (GetDocument().GetFrame()->LocalFrameRoot().IsMainFrame()) {
    gfx::Size viewport_size =
        GetDocument().GetPage()->GetVisualViewport().Size();
    visible_rect =
        GetDocument().GetPage()->GetVisualViewport().RootFrameToViewport(
            visible_rect);
    visible_rect.Intersect(gfx::Rect(gfx::Point(), viewport_size));
  }
  return visible_rect;
}

void Element::ClientQuads(Vector<gfx::QuadF>& quads) const {
  LayoutObject* element_layout_object = GetLayoutObject();
  if (!element_layout_object)
    return;

  // Foreign objects need to convert between SVG and HTML coordinate spaces and
  // cannot use LocalToAbsoluteQuad directly with ObjectBoundingBox which is
  // SVG coordinates and not HTML coordinates. Instead, use the AbsoluteQuads
  // codepath below.
  const auto* svg_element = DynamicTo<SVGElement>(this);
  if (svg_element && !element_layout_object->IsSVGRoot() &&
      !element_layout_object->IsSVGForeignObjectIncludingNG()) {
    // Get the bounding rectangle from the SVG model.
    // TODO(pdr): ObjectBoundingBox does not include stroke and the spec is not
    // clear (see: https://github.com/w3c/svgwg/issues/339, crbug.com/529734).
    // If stroke is desired, we can update this to use AbsoluteQuads, below.
    if (IsA<SVGGraphicsElement>(svg_element)) {
      quads.push_back(element_layout_object->LocalToAbsoluteQuad(
          gfx::QuadF(element_layout_object->ObjectBoundingBox())));
    }
    return;
  }

  // FIXME: Handle table/inline-table with a caption.
  if (element_layout_object->IsBoxModelObject() ||
      element_layout_object->IsBR())
    element_layout_object->AbsoluteQuads(quads);
}

DOMRectList* Element::getClientRects() {
  GetDocument().EnsurePaintLocationDataValidForNode(
      this, DocumentUpdateReason::kJavaScript);
  Vector<gfx::QuadF> quads;
  ClientQuads(quads);
  if (quads.IsEmpty())
    return MakeGarbageCollected<DOMRectList>();

  LayoutObject* element_layout_object = GetLayoutObject();
  DCHECK(element_layout_object);
  GetDocument().AdjustQuadsForScrollAndAbsoluteZoom(quads,
                                                    *element_layout_object);
  return MakeGarbageCollected<DOMRectList>(quads);
}

gfx::RectF Element::GetBoundingClientRectNoLifecycleUpdate() const {
  Vector<gfx::QuadF> quads;
  ClientQuads(quads);
  if (quads.IsEmpty())
    return gfx::RectF();

  gfx::RectF result;
  for (auto& quad : quads)
    result.Union(quad.BoundingBox());

  LayoutObject* element_layout_object = GetLayoutObject();
  DCHECK(element_layout_object);
  GetDocument().AdjustRectForScrollAndAbsoluteZoom(result,
                                                   *element_layout_object);
  return result;
}

DOMRect* Element::getBoundingClientRect() {
  GetDocument().EnsurePaintLocationDataValidForNode(
      this, DocumentUpdateReason::kJavaScript);
  return DOMRect::FromRectF(GetBoundingClientRectNoLifecycleUpdate());
}

const AtomicString& Element::computedRole() {
  Document& document = GetDocument();
  if (!document.IsActive() || !document.View())
    return g_null_atom;
  if (document.NeedsLayoutTreeUpdate() || document.View()->NeedsLayout() ||
      document.Lifecycle().GetState() < DocumentLifecycle::kPrePaintClean) {
    document.View()->UpdateAllLifecyclePhasesExceptPaint(
        DocumentUpdateReason::kJavaScript);
  }
  AXContext ax_context(document, ui::kAXModeBasic);
  return ax_context.GetAXObjectCache().ComputedRoleForNode(this);
}

String Element::computedName() {
  Document& document = GetDocument();
  if (!document.IsActive() || !document.View())
    return String();
  if (document.NeedsLayoutTreeUpdate() || document.View()->NeedsLayout() ||
      document.Lifecycle().GetState() < DocumentLifecycle::kPrePaintClean) {
    document.View()->UpdateAllLifecyclePhasesExceptPaint(
        DocumentUpdateReason::kJavaScript);
  }
  AXContext ax_context(document, ui::kAXModeBasic);
  return ax_context.GetAXObjectCache().ComputedNameForNode(this);
}

AccessibleNode* Element::ExistingAccessibleNode() const {
  if (!RuntimeEnabledFeatures::AccessibilityObjectModelEnabled())
    return nullptr;

  if (!HasRareData())
    return nullptr;

  return GetElementRareData()->GetAccessibleNode();
}

AccessibleNode* Element::accessibleNode() {
  if (!RuntimeEnabledFeatures::AccessibilityObjectModelEnabled())
    return nullptr;

  ElementRareData& rare_data = EnsureElementRareData();
  return rare_data.EnsureAccessibleNode(this);
}

bool Element::toggleAttribute(const AtomicString& qualified_name,
                              ExceptionState& exception_state) {
  // https://dom.spec.whatwg.org/#dom-element-toggleattribute
  // 1. If qualifiedName does not match the Name production in XML, then throw
  // an "InvalidCharacterError" DOMException.
  if (!Document::IsValidName(qualified_name)) {
    exception_state.ThrowDOMException(
        DOMExceptionCode::kInvalidCharacterError,
        "'" + qualified_name + "' is not a valid attribute name.");
    return false;
  }
  // 2. If the context object is in the HTML namespace and its node document is
  // an HTML document, then set qualifiedName to qualifiedName in ASCII
  // lowercase.
  AtomicString lowercase_name = LowercaseIfNecessary(qualified_name);
  WTF::AtomicStringTable::WeakResult hint(lowercase_name.Impl());
  // 3. Let attribute be the first attribute in the context object’s attribute
  // list whose qualified name is qualifiedName, and null otherwise.
  // 4. If attribute is null, then
  if (!GetAttributeHinted(lowercase_name, hint)) {
    // 4. 1. If force is not given or is true, create an attribute whose local
    // name is qualified_name, value is the empty string, and node document is
    // the context object’s node document, then append this attribute to the
    // context object, and then return true.
    SetAttributeHinted(lowercase_name, hint, g_empty_atom);
    return true;
  }
  // 5. Otherwise, if force is not given or is false, remove an attribute given
  // qualifiedName and the context object, and then return false.
  RemoveAttributeHinted(lowercase_name, hint);
  return false;
}

bool Element::toggleAttribute(const AtomicString& qualified_name,
                              bool force,
                              ExceptionState& exception_state) {
  // https://dom.spec.whatwg.org/#dom-element-toggleattribute
  // 1. If qualifiedName does not match the Name production in XML, then throw
  // an "InvalidCharacterError" DOMException.
  if (!Document::IsValidName(qualified_name)) {
    exception_state.ThrowDOMException(
        DOMExceptionCode::kInvalidCharacterError,
        "'" + qualified_name + "' is not a valid attribute name.");
    return false;
  }
  // 2. If the context object is in the HTML namespace and its node document is
  // an HTML document, then set qualifiedName to qualifiedName in ASCII
  // lowercase.
  AtomicString lowercase_name = LowercaseIfNecessary(qualified_name);
  WTF::AtomicStringTable::WeakResult hint(lowercase_name.Impl());
  // 3. Let attribute be the first attribute in the context object’s attribute
  // list whose qualified name is qualifiedName, and null otherwise.
  // 4. If attribute is null, then
  if (!GetAttributeHinted(lowercase_name, hint)) {
    // 4. 1. If force is not given or is true, create an attribute whose local
    // name is qualified_name, value is the empty string, and node document is
    // the context object’s node document, then append this attribute to the
    // context object, and then return true.
    if (force) {
      SetAttributeHinted(lowercase_name, hint, g_empty_atom);
      return true;
    }
    // 4. 2. Return false.
    return false;
  }
  // 5. Otherwise, if force is not given or is false, remove an attribute given
  // qualifiedName and the context object, and then return false.
  if (!force) {
    RemoveAttributeHinted(lowercase_name, hint);
    return false;
  }
  // 6. Return true.
  return true;
}

const AtomicString& Element::getAttributeNS(
    const AtomicString& namespace_uri,
    const AtomicString& local_name) const {
  return getAttribute(QualifiedName(g_null_atom, local_name, namespace_uri));
}

const AttrNameToTrustedType& Element::GetCheckedAttributeTypes() const {
  DEFINE_STATIC_LOCAL(AttrNameToTrustedType, attribute_map, ({}));
  return attribute_map;
}

SpecificTrustedType Element::ExpectedTrustedTypeForAttribute(
    const QualifiedName& q_name) const {
  // There are only a handful of namespaced attributes we care about
  // (xlink:href), and all of those have identical Trusted Types
  // properties to their namespace-less counterpart. So we check whether this
  // is one of SVG's 'known' attributes, and if so just check the local
  // name part as usual.
  if (!q_name.NamespaceURI().IsNull() &&
      !SVGAnimatedHref::IsKnownAttribute(q_name)) {
    return SpecificTrustedType::kNone;
  }

  const AttrNameToTrustedType* attribute_types = &GetCheckedAttributeTypes();
  AttrNameToTrustedType::const_iterator iter =
      attribute_types->find(q_name.LocalName());
  if (iter != attribute_types->end())
    return iter->value;

  // Since event handlers can be defined on nearly all elements, we will
  // consider them independently of the specific element they're attached to.
  //
  // Note: Element::IsEventHandlerAttribute is different and over-approximates
  // event-handler-ness, since it is expected to work only for builtin
  // attributes (like "onclick"), while Trusted Types needs to deal with
  // whatever users pass into setAttribute (for example "one"). Also, it
  // requires the actual Attribute rather than the QName, which means
  // Element::IsEventHandlerAttribute can only be called after an attribute has
  // been constructed.
  if (IsTrustedTypesEventHandlerAttribute(q_name))
    return SpecificTrustedType::kScript;

  return SpecificTrustedType::kNone;
}

void Element::setAttribute(const QualifiedName& name,
                           const String& string,
                           ExceptionState& exception_state) {
  // TODO(lyf): Removes |exception_state| because this function never throws.
  setAttribute(name, AtomicString(string));
}

DISABLE_CFI_PERF
void Element::AttributeChanged(const AttributeModificationParams& params) {
  const QualifiedName& name = params.name;
  if (name == html_names::kSlotAttr && params.old_value != params.new_value) {
    if (ShadowRoot* root = ShadowRootOfParent())
      root->DidChangeHostChildSlotName(params.old_value, params.new_value);
  }

  ParseAttribute(params);

  GetDocument().IncDOMTreeVersion();
  GetDocument().NotifyAttributeChanged(*this, params.name, params.old_value,
                                       params.new_value);

  if (name == html_names::kIdAttr) {
    AtomicString lowercase_id;
    if (GetDocument().InQuirksMode() && !params.new_value.IsLowerASCII())
      lowercase_id = params.new_value.LowerASCII();
    const AtomicString& new_id = lowercase_id ? lowercase_id : params.new_value;
    if (new_id != GetElementData()->IdForStyleResolution()) {
      AtomicString old_id = GetElementData()->SetIdForStyleResolution(new_id);
      GetDocument().GetStyleEngine().IdChangedForElement(old_id, new_id, *this);
    }
  } else if (name == html_names::kClassAttr) {
    if (params.old_value == params.new_value &&
        params.reason != AttributeModificationReason::kByMoveToNewDocument &&
        EarlyExitOnNoopClassOrStyleChange()) {
      return;
    }
    ClassAttributeChanged(params.new_value);
    UpdateClassList(params.old_value, params.new_value);
  } else if (name == html_names::kNameAttr) {
    SetHasName(!params.new_value.IsNull());
  } else if (name == html_names::kPartAttr) {
    part().DidUpdateAttributeValue(params.old_value, params.new_value);
    GetDocument().GetStyleEngine().PartChangedForElement(*this);
  } else if (name == html_names::kExportpartsAttr) {
    EnsureElementRareData().SetPartNamesMap(params.new_value);
    GetDocument().GetStyleEngine().ExportpartsChangedForElement(*this);
  } else if (IsElementReflectionAttribute(name)) {
    SynchronizeContentAttributeAndElementReference(name);
  } else if (IsStyledElement()) {
    if (name == html_names::kStyleAttr) {
      if (params.old_value == params.new_value &&
          EarlyExitOnNoopClassOrStyleChange()) {
        return;
      }
      StyleAttributeChanged(params.new_value, params.reason);
    } else if (IsPresentationAttribute(name)) {
      GetElementData()->SetPresentationAttributeStyleIsDirty(true);
      SetNeedsStyleRecalc(kLocalStyleChange,
                          StyleChangeReasonForTracing::FromAttribute(name));
    }
  }

  InvalidateNodeListCachesInAncestors(&name, this, nullptr);

  if (isConnected()) {
    if (AXObjectCache* cache = GetDocument().ExistingAXObjectCache()) {
      if (params.old_value != params.new_value)
        cache->HandleAttributeChanged(name, this);
    }
  }

  if (params.reason == AttributeModificationReason::kByParser &&
      name == html_names::kDefaultopenAttr && HasValidPopupAttribute()) {
    DCHECK(RuntimeEnabledFeatures::HTMLPopupAttributeEnabled());
    DCHECK(!isConnected());
    GetPopupData()->setHadDefaultOpenWhenParsed(true);
  }

  if (params.reason == AttributeModificationReason::kDirectly &&
      name == html_names::kTabindexAttr &&
      AdjustedFocusedElementInTreeScope() == this) {
    // The attribute change may cause supportsFocus() to return false
    // for the element which had focus.
    //
    // TODO(tkent): We should avoid updating style.  We'd like to check only
    // DOM-level focusability here.
    GetDocument().UpdateStyleAndLayoutTreeForNode(this);
    if (!SupportsFocus())
      blur();
  }
}

void Element::UpdatePopupAttribute(String value) {
  if (!RuntimeEnabledFeatures::HTMLPopupAttributeEnabled()) {
    // If the feature flag isn't enabled, give a console warning about this
    // usage of the 'popup' attribute, which is likely to cause breakage when
    // the feature ships.
    auto& document = GetDocument();
    auto* console_message = MakeGarbageCollected<ConsoleMessage>(
        mojom::blink::ConsoleMessageSource::kOther,
        mojom::blink::ConsoleMessageLevel::kError,
        "Found a 'popup' attribute. If you are testing the popup API, you must "
        "enable Experimental Web Platform Features. If not, note that custom "
        "attributes must start with 'data-': "
        "https://html.spec.whatwg.org/multipage/"
        "dom.html#custom-data-attribute. This usage will *likely cause site "
        "breakage* when the popup API ships: "
        "https://chromestatus.com/feature/5463833265045504.");
    console_message->SetNodes(document.GetFrame(),
                              {DOMNodeIds::IdForNode(this)});
    document.AddConsoleMessage(console_message);
    return;
  }
  PopupValueType type = PopupValueType::kNone;
  if (EqualIgnoringASCIICase(value, kPopupTypeValueAuto) ||
      (!value.IsNull() && value.IsEmpty())) {
    type = PopupValueType::kAuto;
  } else if (EqualIgnoringASCIICase(value, kPopupTypeValueHint)) {
    type = PopupValueType::kHint;
  } else if (EqualIgnoringASCIICase(value, kPopupTypeValueManual)) {
    type = PopupValueType::kManual;
  } else {
    type = PopupValueType::kNone;
  }
  if (HasValidPopupAttribute()) {
    if (PopupType() == type)
      return;
    // If the popup type is changing, hide it.
    if (popupOpen()) {
      HidePopUpInternal(HidePopupFocusBehavior::kFocusPreviousElement,
                        HidePopupForcingLevel::kHideAfterAnimations);
    }
  }
  if (type == PopupValueType::kNone) {
    if (HasValidPopupAttribute()) {
      // If the popup is changing from valid to invalid, remove the PopupData.
      GetElementRareData()->RemovePopupData();
    }
    // TODO(masonf) This console message might be too much log spam. Though
    // in case there's a namespace collision with something the developer is
    // doing with e.g. a function called 'popup', this will be helpful to
    // troubleshoot that.
    GetDocument().AddConsoleMessage(MakeGarbageCollected<ConsoleMessage>(
        mojom::blink::ConsoleMessageSource::kOther,
        mojom::blink::ConsoleMessageLevel::kInfo,
        "Found a 'popup' attribute with an invalid value."));
    return;
  }
  UseCounter::Count(GetDocument(), WebFeature::kValidPopupAttribute);
  EnsureElementRareData().EnsurePopupData().setType(type);
}

bool Element::HasValidPopupAttribute() const {
  return GetPopupData();
}

PopupData* Element::GetPopupData() const {
  return HasRareData() ? GetElementRareData()->GetPopupData() : nullptr;
}
PopupValueType Element::PopupType() const {
  return GetPopupData() ? GetPopupData()->type() : PopupValueType::kNone;
}

// This should be true when :top-layer should match.
bool Element::popupOpen() const {
  DCHECK(RuntimeEnabledFeatures::HTMLPopupAttributeEnabled());
  if (auto* popup_data = GetPopupData())
    return popup_data->visibilityState() == PopupVisibilityState::kShowing;
  return false;
}

// Showing a pop-up happens in phases, to facilitate animations and
// transitions:
// 1. Move the pop-up to the top layer, and remove the UA display:none
//     style.
// 2. Update style. (Transition initial style can be specified in this
//    state.)
// 3. Set the :top-layer pseudo class.
// 4. Update style. (Animations/transitions happen here.)
void Element::showPopUp(ExceptionState& exception_state) {
  DCHECK(RuntimeEnabledFeatures::HTMLPopupAttributeEnabled());
  if (!HasValidPopupAttribute()) {
    return exception_state.ThrowDOMException(
        DOMExceptionCode::kNotSupportedError,
        "Not supported on elements that do not have a valid value for the "
        "'popup' attribute");
  } else if (popupOpen() || !isConnected()) {
    return exception_state.ThrowDOMException(
        DOMExceptionCode::kInvalidStateError,
        "Invalid on already-showing or disconnected popup elements");
  }

  bool should_restore_focus = false;
  auto& document = GetDocument();
  if (PopupType() == PopupValueType::kAuto ||
      PopupType() == PopupValueType::kHint) {
    if (PopupType() == PopupValueType::kHint) {
      // If the new pop-up is popup=hint, hide other hints first.
      if (document.PopupHintShowing()) {
        document.PopupHintShowing()->HidePopUpInternal(
            HidePopupFocusBehavior::kNone,
            HidePopupForcingLevel::kHideAfterAnimations);
      }
      // Then hide open pop-ups that aren't ancestors of this hint.
      if (const Element* hint_ancestor = NearestOpenAncestralPopup(this)) {
        HideAllPopupsUntil(hint_ancestor, document,
                           HidePopupFocusBehavior::kNone,
                           HidePopupForcingLevel::kHideAfterAnimations,
                           HidePopupIndependence::kHideUnrelated);
      }
    } else {
      // If the new pop-up is a popup=auto, hide any pop-up above this in the
      // stack, and hide any hint pop-ups. Because this pop-up isn't yet in the
      // stack, we call NearestOpenAncestralPopup to find this pop-up's
      // ancestor, if any.
      const Element* auto_ancestor = NearestOpenAncestralPopup(this);
      HideAllPopupsUntil(auto_ancestor, document, HidePopupFocusBehavior::kNone,
                         HidePopupForcingLevel::kHideAfterAnimations,
                         HidePopupIndependence::kHideUnrelated);
    }

    // The 'hide' event handlers could have changed this popup, e.g. by changing
    // its type, removing it from the document, or calling showPopUp().
    if (!HasValidPopupAttribute() || !isConnected() || popupOpen())
      return;

    // We only restore focus for popup/hint, and only for the first popup in
    // the stack. If there's nothing showing, restore focus.
    should_restore_focus = !document.TopmostPopupAutoOrHint();
    if (PopupType() == PopupValueType::kAuto) {
      // Add this popup to the popup stack.
      auto& stack = document.PopupStack();
      DCHECK(!stack.Contains(this));
      stack.push_back(this);
    } else {
      document.SetPopupHintShowing(this);
    }
  }

  // Fire the show event (bubbles, not cancelable).
  Event* event = Event::CreateBubble(event_type_names::kShow);
  event->SetTarget(this);
  auto result = DispatchEvent(*event);
  DCHECK_EQ(result, DispatchEventResult::kNotCanceled);

  // The 'show' event handler could have changed this pop-up, e.g. by changing
  // its type, removing it from the document, or calling showPopUp().
  if (!HasValidPopupAttribute() || !isConnected() || popupOpen())
    return;

  GetPopupData()->setAnimationFinishedListener(nullptr);
  GetPopupData()->setPreviouslyFocusedElement(
      should_restore_focus ? document.FocusedElement() : nullptr);
  document.AddToTopLayer(this);
  // Remove display:none styling:
  GetPopupData()->setVisibilityState(PopupVisibilityState::kTransitioning);
  PseudoStateChanged(CSSSelector::kPseudoPopupHidden);

  // Force a style update. This ensures that base property values are set prior
  // to `:top-layer` matching, so that transitions can start on the change to
  // top layer.
  document.UpdateStyleAndLayoutTreeForNode(this);
  EnsureComputedStyle();

  // Make the popup match :top-layer:
  GetPopupData()->setVisibilityState(PopupVisibilityState::kShowing);
  PseudoStateChanged(CSSSelector::kPseudoTopLayer);

  SetPopupFocusOnShow();
}

// static
// All pop-ups up to, but not including, |endpoint|, will be hidden. If there
// are "unrelated" pop-ups open, such as a stack of popup=auto pop-ups and
// |endpoint| is a popup=hint, then the popup_independence argument controls
// whether those unrelated popup=auto pop-ups are hidden.
void Element::HideAllPopupsUntil(const Element* endpoint,
                                 Document& document,
                                 HidePopupFocusBehavior focus_behavior,
                                 HidePopupForcingLevel forcing_level,
                                 HidePopupIndependence popup_independence) {
  DCHECK(RuntimeEnabledFeatures::HTMLPopupAttributeEnabled());
  DCHECK(!endpoint || endpoint->HasValidPopupAttribute());

  // If we're forcing a popup to hide immediately, first hide any other popups
  // that have already started the hide process.
  if (forcing_level == HidePopupForcingLevel::kHideImmediately) {
    auto popups_to_hide = document.PopupsWaitingToHide();
    for (auto popup : popups_to_hide)
      popup->PopupHideFinishIfNeeded();
    DCHECK(document.PopupsWaitingToHide().IsEmpty());
  }

  if (endpoint && endpoint->PopupType() == PopupValueType::kHint) {
    if (popup_independence == HidePopupIndependence::kHideUnrelated) {
      if (document.PopupHintShowing() != endpoint) {
        document.PopupHintShowing()->HidePopUpInternal(focus_behavior,
                                                       forcing_level);
      }
      while (!document.PopupStack().IsEmpty() &&
             document.PopupStack().back() != endpoint) {
        document.PopupStack().back()->HidePopUpInternal(focus_behavior,
                                                        forcing_level);
      }
    }
  } else {
    DCHECK(!endpoint || endpoint->PopupType() == PopupValueType::kAuto);
    const Element* hint_ancestor = nullptr;
    if (document.PopupHintShowing()) {
      // If there is a hint showing that is a descendent of something on the
      // stack, then the hint should be hidden before that ancestor is hidden,
      // regardless of popup_independence.
      hint_ancestor = NearestOpenAncestralPopup(document.PopupHintShowing());
      if (!hint_ancestor &&
          popup_independence == HidePopupIndependence::kHideUnrelated) {
        document.PopupHintShowing()->HidePopUpInternal(focus_behavior,
                                                       forcing_level);
      }
    }
    // Then hide everything in the popup=auto stack up to the specified
    // endpoint.
    while (!document.PopupStack().IsEmpty()) {
      if (document.PopupStack().back() == hint_ancestor) {
        document.PopupHintShowing()->HidePopUpInternal(focus_behavior,
                                                       forcing_level);
      }
      if (document.PopupStack().back() == endpoint)
        break;
      document.PopupStack().back()->HidePopUpInternal(focus_behavior,
                                                      forcing_level);
    }
  }
}

void Element::hidePopUp(ExceptionState& exception_state) {
  DCHECK(RuntimeEnabledFeatures::HTMLPopupAttributeEnabled());
  if (!HasValidPopupAttribute()) {
    return exception_state.ThrowDOMException(
        DOMExceptionCode::kNotSupportedError,
        "Not supported on elements that do not have a valid value for the "
        "'popup' attribute");
  } else if (GetPopupData()->visibilityState() !=
             PopupVisibilityState::kShowing) {
    // Important to check that visibility is not kShowing (rather than
    // popupOpen()), because a hide transition might have been started on this
    // pop-up already, and we don't want to allow a double-hide.
    return exception_state.ThrowDOMException(
        DOMExceptionCode::kInvalidStateError,
        "Invalid on popup elements that aren't already showing");
  }
  HidePopUpInternal(HidePopupFocusBehavior::kFocusPreviousElement,
                    HidePopupForcingLevel::kHideAfterAnimations);
}

// Hiding a pop-up happens in phases, to facilitate animations and
// transitions:
// 1. Capture any already-running animations via getAnimations(), including
//    animations on descendent elements.
// 2. Remove the :top-layer pseudo class.
// 3. Fire the 'hide' event.
// 4. If the hidePopup() call is *not* the result of the pop-up being "forced
//    out" of the top layer, e.g. by a modal dialog or fullscreen element:
//   a. Restore focus to the previously-focused element.
//   b. Update style. (Animations/transitions start here.)
//   c. Call getAnimations() again, remove any from step #1, and then wait
//      until all of them finish or are cancelled.
// 5. Remove the pop-up from the top layer, and add the UA display:none style.
// 6. Update style.
void Element::HidePopUpInternal(HidePopupFocusBehavior focus_behavior,
                                HidePopupForcingLevel forcing_level) {
  DCHECK(RuntimeEnabledFeatures::HTMLPopupAttributeEnabled());
  DCHECK(HasValidPopupAttribute());
  auto& document = GetDocument();
  if (PopupType() == PopupValueType::kAuto ||
      PopupType() == PopupValueType::kHint) {
    // Hide any popups/hints above us in the stack.
    HideAllPopupsUntil(this, document, focus_behavior, forcing_level,
                       HidePopupIndependence::kLeaveUnrelated);

    // The 'hide' event handlers could have changed this popup, e.g. by changing
    // its type, removing it from the document, or calling hidePopUp().
    if (!HasValidPopupAttribute() || !isConnected() ||
        GetPopupData()->visibilityState() != PopupVisibilityState::kShowing) {
      DCHECK(!GetDocument().PopupStack().Contains(this));
      return;
    }

    // Then remove this popup/hint from the stack, if present. If the popup
    // is already hidden, it won't be in the stack.
    if (PopupType() == PopupValueType::kAuto) {
      auto& stack = document.PopupStack();
      DCHECK(!stack.IsEmpty());
      DCHECK_EQ(stack.back(), this);
      stack.pop_back();
    } else {
      DCHECK_EQ(document.TopmostPopupAutoOrHint(), this);
      document.SetPopupHintShowing(nullptr);
    }
  }
  document.PopupsWaitingToHide().insert(this);

  bool force_hide = forcing_level == HidePopupForcingLevel::kHideImmediately;
  HeapVector<Member<Animation>> previous_animations;
  if (!force_hide) {
    previous_animations = GetAnimationsInternal(
        GetAnimationsOptionsResolved{.use_subtree = true});
  }

  GetPopupData()->setInvoker(nullptr);
  GetPopupData()->setNeedsRepositioningForSelectMenu(false);
  GetPopupData()->setFocusBehavior(focus_behavior);
  // Stop matching :top-layer:
  GetPopupData()->setVisibilityState(PopupVisibilityState::kTransitioning);
  PseudoStateChanged(CSSSelector::kPseudoTopLayer);

  // Fire the hide event (bubbles, not cancelable).
  Event* event = Event::CreateBubble(event_type_names::kHide);
  event->SetTarget(this);
  if (force_hide) {
    // We will be force-hidden when the pop-up element is being removed from
    // the document, during which event dispatch is prohibited.
    GetDocument().EnqueueAnimationFrameEvent(event);
    // Immediately finish the hide process.
    return PopupHideFinishIfNeeded();
  }
  auto result = DispatchEvent(*event);
  DCHECK_EQ(result, DispatchEventResult::kNotCanceled);

  // The 'hide' event handler could have changed this popup, e.g. by changing
  // its type, removing it from the document, or calling showPopUp().
  if (!isConnected() || !HasValidPopupAttribute() ||
      GetPopupData()->visibilityState() !=
          PopupVisibilityState::kTransitioning) {
    return;
  }

  // Grab all animations, so that we can "finish" the hide operation once
  // they complete. This will *also* force a style update, ensuring property
  // values are set after `:top-layer` stops matching, so that transitions
  // can start.
  HeapHashSet<Member<EventTarget>> animations;
  for (const auto& animation : GetAnimationsInternal(
           GetAnimationsOptionsResolved{.use_subtree = true})) {
    animations.insert(animation);
  }
  animations.RemoveAll(previous_animations);
  if (animations.IsEmpty()) {
    // No animations to wait for: just finish immediately.
    PopupHideFinishIfNeeded();
  } else {
    GetPopupData()->setAnimationFinishedListener(
        MakeGarbageCollected<PopupAnimationFinishedEventListener>(
            this, std::move(animations)));
  }

  Element* previously_focused_element =
      GetPopupData()->previouslyFocusedElement();
  if (previously_focused_element) {
    GetPopupData()->setPreviouslyFocusedElement(nullptr);
    if (GetPopupData()->focusBehavior() ==
        HidePopupFocusBehavior::kFocusPreviousElement) {
      FocusOptions* focus_options = FocusOptions::Create();
      focus_options->setPreventScroll(true);
      previously_focused_element->Focus(focus_options);
    }
  }
}

void Element::PopupHideFinishIfNeeded() {
  DCHECK(RuntimeEnabledFeatures::HTMLPopupAttributeEnabled());
  GetDocument().PopupsWaitingToHide().erase(this);
  GetDocument().RemoveFromTopLayer(this);
  // Re-apply display:none.
  if (GetPopupData()) {
    GetPopupData()->setVisibilityState(PopupVisibilityState::kHidden);
    GetPopupData()->setAnimationFinishedListener(nullptr);
    PseudoStateChanged(CSSSelector::kPseudoPopupHidden);
  }
}

void Element::SetPopupFocusOnShow() {
  DCHECK(RuntimeEnabledFeatures::HTMLPopupAttributeEnabled());
  // The layout must be updated here because we call Element::isFocusable,
  // which requires an up-to-date layout.
  GetDocument().UpdateStyleAndLayoutTreeForNode(this);

  Element* control = nullptr;
  if (IsAutofocusable() || hasAttribute(html_names::kDelegatesfocusAttr)) {
    // If the popup has autofocus or delegatesfocus, focus it.
    control = this;
  } else {
    // Otherwise, look for a child control that has the autofocus attribute.
    control = GetPopupFocusableArea(/*autofocus_only=*/true);
  }

  // If the popup does not use autofocus or delegatesfocus, then the focus
  // should remain on the currently active element.
  // https://open-ui.org/components/popup.research.explainer#autofocus-logic
  if (!control)
    return;

  // 3. Run the focusing steps for control.
  control->Focus();

  // 4. Let topDocument be the active document of control's node document's
  // browsing context's top-level browsing context.
  // 5. If control's node document's origin is not the same as the origin of
  // topDocument, then return.
  Document& doc = control->GetDocument();
  if (!doc.IsActive())
    return;
  if (!doc.IsInMainFrame() &&
      !doc.TopFrameOrigin()->CanAccess(
          doc.GetExecutionContext()->GetSecurityOrigin())) {
    return;
  }

  // 6. Empty topDocument's autofocus candidates.
  // 7. Set topDocument's autofocus processed flag to true.
  doc.TopDocument().FinalizeAutofocus();
}

// TODO(masonf) This should really be combined with Element::GetFocusableArea(),
// and can possibly be merged with the similar logic for <dialog>. The spec for
// https://html.spec.whatwg.org/multipage/interaction.html#get-the-focusable-area
// does not include dialogs or popups yet.
Element* Element::GetPopupFocusableArea(bool autofocus_only) const {
  DCHECK(RuntimeEnabledFeatures::HTMLPopupAttributeEnabled());
  Node* next = nullptr;
  for (Node* node = FlatTreeTraversal::FirstChild(*this); node; node = next) {
    next = FlatTreeTraversal::Next(*node, this);
    auto* element = DynamicTo<Element>(node);
    if (!element)
      continue;
    if (element->HasValidPopupAttribute() || IsA<HTMLDialogElement>(*element)) {
      next = FlatTreeTraversal::NextSkippingChildren(*element, this);
      continue;
    }
    if (element->IsFocusable() &&
        (!autofocus_only || element->IsAutofocusable())) {
      return element;
    }
  }
  return nullptr;
}

namespace {
const Element* NearestOpenAncestralPopupRecursive(
    const Node* node,
    const HeapHashMap<Member<const Element>, int>& popup_positions,
    const HeapHashMap<Member<const Element>, Member<const Element>>&
        anchors_to_popups,
    int upper_bound,
    HashSet<Member<const Node>>& seen) {
  if (!node || seen.Contains(node))
    return nullptr;
  seen.insert(node);

  const Element* ancestor = nullptr;
  int position = -1;
  auto update = [&ancestor, &position, &popup_positions,
                 upper_bound](const Element* popup) {
    if (popup && popup->popupOpen()) {
      DCHECK(popup_positions.Contains(popup));
      int new_position = popup_positions.at(popup);
      if (new_position > position && new_position < upper_bound) {
        ancestor = popup;
        position = new_position;
      }
    }
  };
  auto recurse_and_update = [&update, &popup_positions, upper_bound,
                             &anchors_to_popups, &seen](const Node* node) {
    update(NearestOpenAncestralPopupRecursive(
        node, popup_positions, anchors_to_popups, upper_bound, seen));
  };

  if (auto* element = DynamicTo<Element>(node)) {
    // Update for this element.
    update(element);
    // Recursively look up the tree from this element's anchors and invokers.
    if (popup_positions.Contains(element)) {
      recurse_and_update(element->anchorElement());
      recurse_and_update(element->GetPopupData()->invoker());
    }
    // Include invokers that weren't used to invoke the popup. This is necessary
    // to catch invoking elements that should not light dismiss a pop-up, even
    // if they weren't used to show it.
    if (auto* form_control = DynamicTo<HTMLFormControlElement>(element)) {
      recurse_and_update(form_control->popupTargetElement().element);
    }
    // Include the anchor elements for all showing pop-ups.
    if (anchors_to_popups.Contains(element)) {
      recurse_and_update(anchors_to_popups.at(element));
    }
  }
  // Also walk up the flat tree from this node.
  recurse_and_update(FlatTreeTraversal::Parent(*node));

  return ancestor;
}
}  // namespace

// static
// This function will return the pop-up that is highest in the pop-up stack that
// is an ancestral pop-up of the provided node. Pop-up ancestors are created by
// DOM flat tree parents, or through either anchor or invoker relationships.
// Anchor relationships are formed by the anchor attribute on a pop-up, pointing
// to another node in the tree. Invoker relationships are formed by invoking
// elements, which are HTMLFormControlElements having popuptoggletarget,
// popupshowtarget, or popuphidetarget attributes pointing to a pop-up element.
// There can be multiple pop-ups that point to a single anchor element, and
// there can be multiple invoking elements for a single pop-up. Additionally, an
// anchor for one pop-up can be an invoker for a different pop-up. For these
// reasons, this function needs to do a recursive tree walk up from the provided
// node, plus all associated anchors and invokers, returning the highest (on the
// stack) pop-up that is found. If the inclusive parameter is true, the highest
// pop-up found during the tree-walk is included in the search. If it is false,
// the |node| parameter must be a pop-up, and the highest pop-up *below* that
// starting pop- up will be returned.
const Element* Element::NearestOpenAncestralPopup(const Node* node,
                                                  bool inclusive) {
  DCHECK(node);
  // popup_positions is a map from all showing (or about-to-show) pop-ups to
  // their position in the pop-up stack.
  HeapHashMap<Member<const Element>, int> popup_positions;
  // anchors_to_popups is a map from the anchor elements of all showing pop-ups
  // back to the pop-up itself.
  HeapHashMap<Member<const Element>, Member<const Element>> anchors_to_popups;
  int indx = 0;
  for (auto popup : node->GetDocument().PopupStack()) {
    popup_positions.Set(popup, indx++);
    if (popup->anchorElement())
      anchors_to_popups.Set(popup->anchorElement(), popup);
  }
  auto* hint_showing = node->GetDocument().PopupHintShowing();
  if (hint_showing) {
    popup_positions.Set(hint_showing, indx++);
    if (hint_showing->anchorElement()) {
      anchors_to_popups.Set(hint_showing->anchorElement(), hint_showing);
    }
  }
  auto* element = DynamicTo<Element>(node);
  bool new_element =
      element && element->HasValidPopupAttribute() && !element->popupOpen();
  if (new_element) {
    DCHECK(!inclusive);
    popup_positions.Set(element, indx++);
  }
  // upper_bound is one above the maximum pop-up stack height to accept. It is
  // typically the position of the provided element.
  int upper_bound =
      popup_positions.Contains(element) ? popup_positions.at(element) : INT_MAX;
  if (hint_showing && new_element) {
    upper_bound = popup_positions.at(hint_showing);  // Do not include the hint
  }
  if (inclusive) {
    // For inclusive mode, we need to walk up the tree until we find an open
    // pop-up, and then modify the upper bound to include that pop-up, if found.
    for (const Node* current_node = node; current_node;
         current_node = FlatTreeTraversal::Parent(*current_node)) {
      if (auto* current_element = DynamicTo<Element>(current_node);
          current_element && current_element->HasValidPopupAttribute() &&
          current_element->popupOpen()) {
        upper_bound = popup_positions.at(current_element) + 1;  // Include it.
        break;
      }
    }
  }
  HashSet<Member<const Node>> seen;
  return NearestOpenAncestralPopupRecursive(
      node, popup_positions, anchors_to_popups, upper_bound, seen);
}

// static
void Element::HandlePopupLightDismiss(const Event& event) {
  DCHECK(RuntimeEnabledFeatures::HTMLPopupAttributeEnabled());
  if (event.GetEventPath().IsEmpty())
    return;
  DCHECK_NE(Event::PhaseType::kNone, event.eventPhase());
  if (event.eventPhase() == Event::PhaseType::kBubblingPhase)
    return;
  // Ensure that shadow DOM event retargeting is considered when computing
  // the event target node.
  auto* target_node = event.GetEventPath()[0].Target()->ToNode();
  if (!target_node)
    return;
  auto& document = target_node->GetDocument();
  DCHECK(document.TopmostPopupAutoOrHint());
  const AtomicString& event_type = event.type();
  if (event_type == event_type_names::kMousedown) {
    // - Hide everything up to the clicked element. We do this on mousedown,
    //   rather than mouseup/click, for two reasons:
    //    1. This mirrors typical platform popups, which dismiss on mousedown.
    //    2. This allows a mouse-drag that starts on a popup and finishes off
    //       the popup, without light-dismissing the popup.

    // For a clicked node, hide all pop-ups outside the clicked pop-up tree,
    // including unrelated pop-ups.
    HideAllPopupsUntil(
        NearestOpenAncestralPopup(target_node, /*inclusive*/ true), document,
        HidePopupFocusBehavior::kNone,
        HidePopupForcingLevel::kHideAfterAnimations,
        HidePopupIndependence::kHideUnrelated);
  } else if (event_type == event_type_names::kKeydown) {
    const KeyboardEvent* key_event = DynamicTo<KeyboardEvent>(event);
    if (key_event && key_event->key() == "Escape") {
      // Escape key just pops the topmost popup or hint off the stack.
      document.TopmostPopupAutoOrHint()->HidePopUpInternal(
          HidePopupFocusBehavior::kFocusPreviousElement,
          HidePopupForcingLevel::kHideAfterAnimations);
    }
  } else if (event_type == event_type_names::kFocusin) {
    // If we focus an element, hide all pop-ups outside the that element's
    // pop-up tree, including unrelated pop-ups.
    HideAllPopupsUntil(
        NearestOpenAncestralPopup(target_node, /*inclusive*/ true), document,
        HidePopupFocusBehavior::kNone,
        HidePopupForcingLevel::kHideAfterAnimations,
        HidePopupIndependence::kHideUnrelated);
  }
}

void Element::InvokePopup(Element* invoker) {
  DCHECK(invoker);
  DCHECK(RuntimeEnabledFeatures::HTMLPopupAttributeEnabled());
  DCHECK(HasValidPopupAttribute());
  GetPopupData()->setInvoker(invoker);
  showPopUp(ASSERT_NO_EXCEPTION);
}

Element* Element::anchorElement() const {
  if (!RuntimeEnabledFeatures::HTMLPopupAttributeEnabled())
    return nullptr;
  if (!HasValidPopupAttribute())
    return nullptr;
  const AtomicString& anchor_id = FastGetAttribute(html_names::kAnchorAttr);
  if (anchor_id.IsNull())
    return nullptr;
  if (!IsInTreeScope())
    return nullptr;
  return GetTreeScope().getElementById(anchor_id);  // may be null
}

void Element::MaybeTriggerHoverPopup(Element* popup_element) {
  DCHECK(RuntimeEnabledFeatures::HTMLPopupAttributeEnabled());
  if (!popup_element || !popup_element->HasValidPopupAttribute())
    return;
  // Remove this element from hoverPopupTasks always.
  popup_element->GetPopupData()->hoverPopupTasks().erase(this);
  // Only trigger the pop-up if the hoverpopup attribute still points to the
  // same pop-up, and the pop-up is in the tree and still not showing.
  if (popup_element->IsInTreeScope() && !popup_element->popupOpen() &&
      popup_element == GetTreeScope().getElementById(
                           FastGetAttribute(html_names::kHoverpopupAttr))) {
    popup_element->showPopUp(ASSERT_NO_EXCEPTION);
  }
}

void Element::HandlePopupHovered(bool hovered) {
  if (!RuntimeEnabledFeatures::HTMLPopupAttributeEnabled())
    return;
  if (!FastHasAttribute(html_names::kHoverpopupAttr) || !IsInTreeScope())
    return;
  Element* popup_element = GetTreeScope().getElementById(
      FastGetAttribute(html_names::kHoverpopupAttr));
  if (!popup_element || !popup_element->HasValidPopupAttribute())
    return;
  if (hovered) {
    auto& hover_tasks = popup_element->GetPopupData()->hoverPopupTasks();
    DCHECK(!hover_tasks.Contains(this));

    // When we enter an element, we'll post a delayed task for the pop-up we're
    // targeting. It's possible that multiple nested elements have hoverpopup
    // attributes pointing to the same pop-up, and in that case, we want to
    // trigger on the first of them that reaches its timeout threshold.
    hover_tasks.insert(
        this,
        PostDelayedCancellableTask(
            *GetExecutionContext()->GetTaskRunner(TaskType::kInternalDefault),
            FROM_HERE,
            WTF::Bind(&Element::MaybeTriggerHoverPopup,
                      WrapWeakPersistent(this),
                      WrapWeakPersistent(popup_element)),
            base::Seconds(GetComputedStyle()->HoverPopUpDelay())));
  } else {
    // If we have a task still waiting, cancel it.
    popup_element->GetPopupData()->hoverPopupTasks().Take(this).Cancel();
    // TODO(masonf): Still need to implement the code to hide this pop-up after
    // a configurable delay. That needs to work even if the pop-up wasn't
    // triggered by a hoverpopup attribute. E.g. a regular pop-up that gets
    // hidden after it has not been hovered for n seconds. This should connect
    // to the HoverPopUpHideDelay() computed style value.
  }
}

void Element::SetNeedsRepositioningForSelectMenu(bool flag) {
  DCHECK(RuntimeEnabledFeatures::HTMLSelectMenuElementEnabled());
  DCHECK(RuntimeEnabledFeatures::HTMLPopupAttributeEnabled());
  DCHECK(HasValidPopupAttribute());
  auto& popup_data = EnsureElementRareData().EnsurePopupData();
  if (popup_data.needsRepositioningForSelectMenu() == flag)
    return;
  popup_data.setNeedsRepositioningForSelectMenu(flag);
  if (flag) {
    SetHasCustomStyleCallbacks();
    SetNeedsStyleRecalc(kLocalStyleChange,
                        StyleChangeReasonForTracing::Create(
                            style_change_reason::kPopupVisibilityChange));
  }
}

void Element::SetOwnerSelectMenuElement(HTMLSelectMenuElement* element) {
  DCHECK(RuntimeEnabledFeatures::HTMLSelectMenuElementEnabled());
  DCHECK(RuntimeEnabledFeatures::HTMLPopupAttributeEnabled());
  DCHECK(HasValidPopupAttribute());
  EnsureElementRareData().EnsurePopupData().setOwnerSelectMenuElement(element);
}

// TODO(crbug.com/1197720): The popup position should be provided by the new
// anchored positioning scheme.
void Element::AdjustPopupPositionForSelectMenu(ComputedStyle& style) {
  DCHECK(RuntimeEnabledFeatures::HTMLSelectMenuElementEnabled());
  DCHECK(HasValidPopupAttribute());
  DCHECK(GetPopupData()->needsRepositioningForSelectMenu());
  auto* owner_select = GetPopupData()->ownerSelectMenuElement();
  DCHECK(owner_select);

  LocalDOMWindow* window = GetDocument().domWindow();
  if (!window)
    return;

  gfx::RectF anchor_rect_in_screen =
      owner_select->GetBoundingClientRectNoLifecycleUpdate();
  const float anchor_zoom =
      owner_select->GetLayoutObject()
          ? owner_select->GetLayoutObject()->StyleRef().EffectiveZoom()
          : 1;
  anchor_rect_in_screen.Scale(anchor_zoom);
  // Don't use the LocalDOMWindow innerHeight/innerWidth getters, as those can
  // trigger a re-entrant style and layout update.
  int avail_width = GetDocument().View()->Size().width();
  int avail_height = GetDocument().View()->Size().height();
  gfx::Rect avail_rect = gfx::Rect(0, 0, avail_width, avail_height);

  // Remove any margins on the listbox part, so we can position it correctly.
  style.SetMarginTop(Length::Fixed(0));
  style.SetMarginLeft(Length::Fixed(0));
  style.SetMarginRight(Length::Fixed(0));
  style.SetMarginBottom(Length::Fixed(0));

  // Position the listbox part where is more space available.
  const float available_space_above =
      anchor_rect_in_screen.y() - avail_rect.y();
  const float available_space_below =
      avail_rect.bottom() - anchor_rect_in_screen.bottom();
  if (available_space_below < available_space_above) {
    style.SetMaxHeight(Length::Fixed(available_space_above));
    style.SetBottom(
        Length::Fixed(avail_rect.bottom() - anchor_rect_in_screen.y()));
    style.SetTop(Length::Auto());
  } else {
    style.SetMaxHeight(Length::Fixed(available_space_below));
    style.SetTop(Length::Fixed(anchor_rect_in_screen.bottom()));
  }

  const float available_space_if_left_anchored =
      avail_rect.right() - anchor_rect_in_screen.x();
  const float available_space_if_right_anchored =
      anchor_rect_in_screen.right() - avail_rect.x();
  style.SetMinWidth(Length::Fixed(anchor_rect_in_screen.width()));
  if (available_space_if_left_anchored > anchor_rect_in_screen.width() ||
      available_space_if_left_anchored > available_space_if_right_anchored) {
    style.SetLeft(Length::Fixed(anchor_rect_in_screen.x()));
    style.SetMaxWidth(Length::Fixed(available_space_if_left_anchored));
  } else {
    style.SetRight(
        Length::Fixed(avail_rect.right() - anchor_rect_in_screen.right()));
    style.SetLeft(Length::Auto());
    style.SetMaxWidth(Length::Fixed(available_space_if_right_anchored));
  }
}

bool Element::HasLegalLinkAttribute(const QualifiedName&) const {
  return false;
}

const QualifiedName& Element::SubResourceAttributeName() const {
  return QualifiedName::Null();
}

template <typename CharacterType>
static inline ClassStringContent ClassStringHasClassName(
    const CharacterType* characters,
    unsigned length) {
  DCHECK_GT(length, 0u);

  unsigned i = 0;
  do {
    if (IsNotHTMLSpace<CharacterType>(characters[i]))
      break;
    ++i;
  } while (i < length);

  if (i == length && length >= 1)
    return ClassStringContent::kWhiteSpaceOnly;

  return ClassStringContent::kHasClasses;
}

static inline ClassStringContent ClassStringHasClassName(
    const AtomicString& new_class_string) {
  unsigned length = new_class_string.length();

  if (!length)
    return ClassStringContent::kEmpty;

  if (new_class_string.Is8Bit())
    return ClassStringHasClassName(new_class_string.Characters8(), length);
  return ClassStringHasClassName(new_class_string.Characters16(), length);
}

void Element::ClassAttributeChanged(const AtomicString& new_class_string) {
  DCHECK(GetElementData());
  ClassStringContent class_string_content_type =
      ClassStringHasClassName(new_class_string);
  const bool should_fold_case = GetDocument().InQuirksMode();
  if (class_string_content_type == ClassStringContent::kHasClasses) {
    const SpaceSplitString old_classes = GetElementData()->ClassNames();
    GetElementData()->SetClass(new_class_string, should_fold_case);
    const SpaceSplitString& new_classes = GetElementData()->ClassNames();
    GetDocument().GetStyleEngine().ClassChangedForElement(old_classes,
                                                          new_classes, *this);
  } else {
    const SpaceSplitString& old_classes = GetElementData()->ClassNames();
    GetDocument().GetStyleEngine().ClassChangedForElement(old_classes, *this);
    if (class_string_content_type == ClassStringContent::kWhiteSpaceOnly)
      GetElementData()->SetClass(new_class_string, should_fold_case);
    else
      GetElementData()->ClearClass();
  }
}

void Element::UpdateClassList(const AtomicString& old_class_string,
                              const AtomicString& new_class_string) {
  if (!HasRareData())
    return;
  if (DOMTokenList* class_list = GetElementRareData()->GetClassList())
    class_list->DidUpdateAttributeValue(old_class_string, new_class_string);
}

// Returns true if the given attribute is an event handler.
// We consider an event handler any attribute that begins with "on".
// It is a simple solution that has the advantage of not requiring any
// code or configuration change if a new event handler is defined.

static inline bool IsEventHandlerAttribute(const Attribute& attribute) {
  return attribute.GetName().NamespaceURI().IsNull() &&
         attribute.GetName().LocalName().StartsWith("on");
}

bool Element::AttributeValueIsJavaScriptURL(const Attribute& attribute) {
  return ProtocolIsJavaScript(
      StripLeadingAndTrailingHTMLSpaces(attribute.Value()));
}

bool Element::IsJavaScriptURLAttribute(const Attribute& attribute) const {
  return IsURLAttribute(attribute) && AttributeValueIsJavaScriptURL(attribute);
}

bool Element::IsScriptingAttribute(const Attribute& attribute) const {
  return IsEventHandlerAttribute(attribute) ||
         IsJavaScriptURLAttribute(attribute) ||
         IsHTMLContentAttribute(attribute) ||
         IsSVGAnimationAttributeSettingJavaScriptURL(attribute);
}

void Element::StripScriptingAttributes(
    Vector<Attribute, kAttributePrealloc>& attribute_vector) const {
  wtf_size_t destination = 0;
  for (wtf_size_t source = 0; source < attribute_vector.size(); ++source) {
    if (IsScriptingAttribute(attribute_vector[source]))
      continue;

    if (source != destination)
      attribute_vector[destination] = attribute_vector[source];

    ++destination;
  }
  attribute_vector.Shrink(destination);
}

void Element::ParserSetAttributes(
    const Vector<Attribute, kAttributePrealloc>& attribute_vector) {
  DCHECK(!isConnected());
  DCHECK(!parentNode());
  DCHECK(!element_data_);

  if (!attribute_vector.IsEmpty()) {
    if (GetDocument().GetElementDataCache())
      element_data_ =
          GetDocument()
              .GetElementDataCache()
              ->CachedShareableElementDataWithAttributes(attribute_vector);
    else
      element_data_ =
          ShareableElementData::CreateWithAttributes(attribute_vector);
  }

  ParserDidSetAttributes();

  // Use attribute_vector instead of element_data_ because AttributeChanged
  // might modify element_data_.
  for (const auto& attribute : attribute_vector) {
    AttributeChanged(AttributeModificationParams(
        attribute.GetName(), g_null_atom, attribute.Value(),
        AttributeModificationReason::kByParser));
  }
}

bool Element::HasEquivalentAttributes(const Element& other) const {
  SynchronizeAllAttributes();
  other.SynchronizeAllAttributes();
  if (GetElementData() == other.GetElementData())
    return true;
  if (GetElementData())
    return GetElementData()->IsEquivalent(other.GetElementData());
  if (other.GetElementData())
    return other.GetElementData()->IsEquivalent(GetElementData());
  return true;
}

String Element::nodeName() const {
  return tag_name_.ToString();
}

AtomicString Element::LocalNameForSelectorMatching() const {
  if (IsHTMLElement() || !IsA<HTMLDocument>(GetDocument()))
    return localName();
  return localName().LowerASCII();
}

bool Element::IsHTMLWithTagName(const String& tag_name) const {
  return html_names::xhtmlNamespaceURI == namespaceURI() &&
         localName() == String(tag_name).LowerASCII();
}

const AtomicString& Element::LocateNamespacePrefix(
    const AtomicString& namespace_to_locate) const {
  if (!prefix().IsNull() && namespaceURI() == namespace_to_locate)
    return prefix();

  AttributeCollection attributes = Attributes();
  for (const Attribute& attr : attributes) {
    if (attr.Prefix() == g_xmlns_atom && attr.Value() == namespace_to_locate)
      return attr.LocalName();
  }

  if (Element* parent = parentElement())
    return parent->LocateNamespacePrefix(namespace_to_locate);

  return g_null_atom;
}

const AtomicString Element::ImageSourceURL() const {
  return FastGetAttribute(html_names::kSrcAttr);
}

bool Element::LayoutObjectIsNeeded(const ComputedStyle& style) const {
  return style.Display() != EDisplay::kNone &&
         style.Display() != EDisplay::kContents;
}

LayoutObject* Element::CreateLayoutObject(const ComputedStyle& style,
                                          LegacyLayout legacy) {
  return LayoutObject::CreateObject(this, style, legacy);
}

Node::InsertionNotificationRequest Element::InsertedInto(
    ContainerNode& insertion_point) {
  // need to do superclass processing first so isConnected() is true
  // by the time we reach updateId
  ContainerNode::InsertedInto(insertion_point);

  DCHECK(!HasRareData() || !GetElementRareData()->HasPseudoElements());

  if (!insertion_point.IsInTreeScope())
    return kInsertionDone;

  if (isConnected() && HasRareData()) {
    ElementRareData* rare_data = GetElementRareData();
    if (ElementIntersectionObserverData* observer_data =
            rare_data->IntersectionObserverData()) {
      observer_data->TrackWithController(
          GetDocument().EnsureIntersectionObserverController());
      if (!observer_data->IsEmpty()) {
        if (LocalFrameView* frame_view = GetDocument().View()) {
          frame_view->SetIntersectionObservationState(
              LocalFrameView::kRequired);
        }
      }
    }

    if (auto* context = rare_data->GetDisplayLockContext())
      context->ElementConnected();
  }

  if (isConnected()) {
    EnqueueAutofocus(*this);

    if (GetCustomElementState() == CustomElementState::kCustom)
      CustomElement::EnqueueConnectedCallback(*this);
    else if (GetCustomElementState() == CustomElementState::kUndefined)
      CustomElement::TryToUpgrade(*this);
  }

  if (GetPopupData() && GetPopupData()->hadDefaultOpenWhenParsed()) {
    // If a Popup element has the `defaultopen` attribute upon page
    // load, and it is the *first* such popup, show it.
    DCHECK(RuntimeEnabledFeatures::HTMLPopupAttributeEnabled());
    DCHECK(isConnected());
    GetPopupData()->setHadDefaultOpenWhenParsed(false);
    auto maybe_show_popup = [](Element* popup) {
      // The `defaultopen` attribute can only be used on popup=manual and
      // popup=auto pop-ups.
      if (popup && popup->isConnected() &&
          (popup->PopupType() == PopupValueType::kManual ||
           (popup->PopupType() == PopupValueType::kAuto &&
            !popup->GetDocument().PopupAutoShowing()))) {
        popup->showPopUp(ASSERT_NO_EXCEPTION);
      }
    };
    GetDocument()
        .GetTaskRunner(TaskType::kDOMManipulation)
        ->PostTask(FROM_HERE,
                   WTF::Bind(maybe_show_popup, WrapWeakPersistent(this)));
  }

  TreeScope& scope = insertion_point.GetTreeScope();
  if (scope != GetTreeScope())
    return kInsertionDone;

  const AtomicString& id_value = GetIdAttribute();
  if (!id_value.IsNull())
    UpdateId(scope, g_null_atom, id_value);

  const AtomicString& name_value = GetNameAttribute();
  if (!name_value.IsNull())
    UpdateName(g_null_atom, name_value);

  ExecutionContext* context = GetExecutionContext();
  if (RuntimeEnabledFeatures::FocusgroupEnabled(context)) {
    const AtomicString& focusgroup_value =
        FastGetAttribute(html_names::kFocusgroupAttr);
    if (!focusgroup_value.IsNull())
      UpdateFocusgroup(focusgroup_value);

    // We parse the focusgroup attribute for the ShadowDOM elements before we
    // parse it for any of its root's ancestors, which might lead to an
    // incorrect focusgroup value. Re-run the algorithm for the ShadowDOM
    // elements when the ShadowRoot's parent gets inserted in the tree.
    if (GetShadowRoot())
      UpdateFocusgroupInShadowRootIfNeeded();
  }

  if (parentElement() && parentElement()->IsInCanvasSubtree())
    SetIsInCanvasSubtree(true);

  return kInsertionDone;
}

void Element::RemovedFrom(ContainerNode& insertion_point) {
  bool was_in_document = insertion_point.isConnected();

  SetComputedStyle(nullptr);
  SetStyleShouldForceLegacyLayout(false);
  SetShouldForceLegacyLayoutForChild(false);

  if (Fullscreen::IsFullscreenElement(*this)) {
    SetContainsFullScreenElementOnAncestorsCrossingFrameBoundaries(false);
    if (auto* insertion_point_element = DynamicTo<Element>(insertion_point)) {
      insertion_point_element->SetContainsFullScreenElement(false);
      insertion_point_element
          ->SetContainsFullScreenElementOnAncestorsCrossingFrameBoundaries(
              false);
    }
  }

  // If a popup is removed from the document, make sure it gets
  // removed from the popup element stack and the top layer.
  if (was_in_document && HasValidPopupAttribute()) {
    // We can't run focus event handlers while removing elements.
    HidePopUpInternal(HidePopupFocusBehavior::kNone,
                      HidePopupForcingLevel::kHideImmediately);
  }

  if (GetDocument().GetPage())
    GetDocument().GetPage()->GetPointerLockController().ElementRemoved(this);

  GetDocument().UnobserveForIntrinsicSize(this);

  SetSavedLayerScrollOffset(ScrollOffset());

  if (insertion_point.IsInTreeScope() && GetTreeScope() == GetDocument()) {
    const AtomicString& id_value = GetIdAttribute();
    if (!id_value.IsNull())
      UpdateId(insertion_point.GetTreeScope(), id_value, g_null_atom);

    const AtomicString& name_value = GetNameAttribute();
    if (!name_value.IsNull())
      UpdateName(name_value, g_null_atom);
  }

  if (AccessibleNode* accessible_node = ExistingAccessibleNode())
    accessible_node->DetachedFromDocument();

  ContainerNode::RemovedFrom(insertion_point);

  if (was_in_document) {
    if (this == GetDocument().CssTarget())
      GetDocument().SetCSSTarget(nullptr);

    if (GetCustomElementState() == CustomElementState::kCustom)
      CustomElement::EnqueueDisconnectedCallback(*this);
  }

  GetDocument().GetRootScrollerController().ElementRemoved(*this);

  if (IsInTopLayer()) {
    Fullscreen::ElementRemoved(*this);
    GetDocument().RemoveFromTopLayer(this);
  }

  ClearElementFlag(ElementFlags::kIsInCanvasSubtree);

  if (HasRareData()) {
    ElementRareData* data = GetElementRareData();

    data->ClearFocusgroupFlags();
    data->ClearRestyleFlags();

    if (ElementAnimations* element_animations = data->GetElementAnimations())
      element_animations->CssAnimations().Cancel();

    if (was_in_document && data->IntersectionObserverData()) {
      data->IntersectionObserverData()->ComputeIntersectionsForTarget(
          IntersectionObservation::kExplicitRootObserversNeedUpdate |
          IntersectionObservation::kImplicitRootObserversNeedUpdate |
          IntersectionObservation::kIgnoreDelay);
      data->IntersectionObserverData()->StopTrackingWithController(
          GetDocument().EnsureIntersectionObserverController());
    }

    if (auto* context = data->GetDisplayLockContext())
      context->ElementDisconnected();

    DCHECK(!data->HasPseudoElements());
  }

  if (auto* const frame = GetDocument().GetFrame()) {
    if (UNLIKELY(HasUndoStack()))
      frame->GetEditor().GetUndoStack().ElementRemoved(this);
    frame->GetEventHandler().ElementRemoved(this);
  }
}

void Element::AttachLayoutTree(AttachContext& context) {
  DCHECK(GetDocument().InStyleRecalc());

  StyleEngine& style_engine = GetDocument().GetStyleEngine();

  const ComputedStyle* style = GetComputedStyle();
  bool being_rendered =
      context.parent && style && !style->IsEnsuredInDisplayNone();

  if (!being_rendered && !ChildNeedsReattachLayoutTree()) {
    // We may have skipped recalc for this Element if it's a container query
    // container. This recalc must be resumed now, since we're not going to
    // create a LayoutObject for the Element after all.
    style_engine.RecalcStyleForNonLayoutNGContainerDescendants(*this);
    // The above recalc may have marked some descendant for reattach, which
    // would set the child-needs flag.
    if (!ChildNeedsReattachLayoutTree()) {
      Node::AttachLayoutTree(context);
      return;
    }
  }

  AttachContext children_context(context);
  LayoutObject* layout_object = nullptr;
  if (being_rendered) {
    AdjustForceLegacyLayout(style, &children_context.force_legacy_layout);

    LegacyLayout legacy = children_context.force_legacy_layout
                              ? LegacyLayout::kForce
                              : LegacyLayout::kAuto;

    if (legacy == LegacyLayout::kForce &&
        style->IsContainerForSizeContainerQueries()) {
      style_engine.ReportUseOfLegacyLayoutWithContainerQueries();
    }

    LayoutTreeBuilderForElement builder(*this, context, style, legacy);
    builder.CreateLayoutObject();

    layout_object = GetLayoutObject();
    if (layout_object) {
      children_context.previous_in_flow = nullptr;
      children_context.parent = layout_object;
      children_context.next_sibling = nullptr;
      children_context.next_sibling_valid = true;
    } else if (style->Display() != EDisplay::kContents) {
      // The layout object creation was suppressed for other reasons than
      // being display:none or display:contents (E.g.
      // LayoutObject::CanHaveChildren() returning false). Make sure we don't
      // attempt to create LayoutObjects further down the subtree.
      children_context.parent = nullptr;
    }
    // For display:contents elements, we keep the previous_in_flow,
    // next_sibling, and parent, in the context for attaching children.
  } else {
    // We are a display:none element. Set the parent to nullptr to make sure
    // we never create any child layout boxes.
    children_context.parent = nullptr;
  }
  children_context.use_previous_in_flow = true;

  if (children_context.force_legacy_layout ||
      (being_rendered && !children_context.parent) ||
      (layout_object &&
       !IsGuaranteedToEnterNGBlockNodeLayout(*layout_object))) {
    // If the created LayoutObject is forced into a legacy object, or if a
    // LayoutObject was not created, even if we thought it should have been, for
    // instance because the parent LayoutObject returns false for
    // IsChildAllowed, we need to complete the skipped style recalc for size
    // query containers as we would not have an NGBlockNode to resume from.
    style_engine.RecalcStyleForNonLayoutNGContainerDescendants(*this);
  }

  bool skip_container_descendants = SkippedContainerStyleRecalc();
  bool skip_lock_descendants = ChildStyleRecalcBlockedByDisplayLock();
  if (skip_container_descendants || skip_lock_descendants) {
    // Since we block style recalc on descendants of this node due to display
    // locking or container queries, none of its descendants should have the
    // NeedsReattachLayoutTree bit set.
    DCHECK(!ChildNeedsReattachLayoutTree());

    if (skip_lock_descendants) {
      // If an element is locked we shouldn't attach the layout tree for its
      // descendants. We should notify that we blocked a reattach so that we
      // will correctly attach the descendants when allowed.
      GetDisplayLockContext()->NotifyReattachLayoutTreeWasBlocked();
    }
    Node::AttachLayoutTree(context);
    if (layout_object && layout_object->AffectsWhitespaceSiblings())
      context.previous_in_flow = layout_object;
    return;
  }

  AttachPrecedingPseudoElements(children_context);

  if (ShadowRoot* shadow_root = GetShadowRoot()) {
    // When a shadow root exists, it does the work of attaching the children.
    shadow_root->AttachLayoutTree(children_context);
    Node::AttachLayoutTree(context);
    ClearChildNeedsReattachLayoutTree();
  } else {
    ContainerNode::AttachLayoutTree(children_context);
  }

  AttachSucceedingPseudoElements(children_context);

  if (layout_object) {
    if (layout_object->AffectsWhitespaceSiblings())
      context.previous_in_flow = layout_object;
    layout_object->HandleSubtreeModifications();
  } else {
    context.previous_in_flow = children_context.previous_in_flow;
  }
}

void Element::DetachLayoutTree(bool performing_reattach) {
  HTMLFrameOwnerElement::PluginDisposeSuspendScope suspend_plugin_dispose;
  if (HasRareData()) {
    ElementRareData* data = GetElementRareData();
    if (!performing_reattach) {
      data->ClearPseudoElements();
      data->ClearContainerQueryData();
    }

    if (ElementAnimations* element_animations = data->GetElementAnimations()) {
      if (!performing_reattach) {
        DocumentLifecycle::DetachScope will_detach(GetDocument().Lifecycle());
        element_animations->CssAnimations().Cancel();
        element_animations->SetAnimationStyleChange(false);
      }
      element_animations->RestartAnimationOnCompositor();
    }
  }

  DetachPrecedingPseudoElements(performing_reattach);

  auto* context = GetDisplayLockContext();
  bool was_shaping_deferred =
      context && GetLayoutObject() && GetLayoutObject()->IsShapingDeferred();

  // TODO(futhark): We need to traverse into IsUserActionElement() subtrees,
  // even if they are already display:none because we do not clear the
  // hovered/active bits as part of style recalc, but wait until the next time
  // we do a hit test. That means we could be doing a forced layout tree update
  // making a hovered subtree display:none and immediately remove the subtree
  // leaving stale hovered/active state on ancestors. See relevant issues:
  // https://crbug.com/967548
  // https://crbug.com/939769
  if (ChildNeedsReattachLayoutTree() || GetComputedStyle() ||
      (!performing_reattach && IsUserActionElement())) {
    if (ShadowRoot* shadow_root = GetShadowRoot()) {
      shadow_root->DetachLayoutTree(performing_reattach);
      Node::DetachLayoutTree(performing_reattach);
    } else {
      ContainerNode::DetachLayoutTree(performing_reattach);
    }
  } else {
    Node::DetachLayoutTree(performing_reattach);
  }

  DetachSucceedingPseudoElements(performing_reattach);

  if (!performing_reattach) {
    UpdateCallbackSelectors(GetComputedStyle(), nullptr);
    SetComputedStyle(nullptr);
  }

  if (!performing_reattach && IsUserActionElement()) {
    if (IsHovered())
      GetDocument().HoveredElementDetached(*this);
    if (InActiveChain())
      GetDocument().ActiveChainNodeDetached(*this);
    GetDocument().UserActionElements().DidDetach(*this);
  }

  if (context) {
    context->DetachLayoutTree();
    if (was_shaping_deferred)
      context->SetRequestedState(EContentVisibility::kVisible);
  }
}

void Element::ReattachLayoutTreeChildren(base::PassKey<StyleEngine>) {
  DCHECK(NeedsReattachLayoutTree());
  DCHECK(ChildNeedsReattachLayoutTree());
  DCHECK(GetLayoutObject());

  constexpr bool performing_reattach = true;

  DetachPrecedingPseudoElements(performing_reattach);

  ShadowRoot* shadow_root = GetShadowRoot();

  if (shadow_root) {
    shadow_root->DetachLayoutTree(performing_reattach);
  } else {
    // Can not use ContainerNode::DetachLayoutTree() because that would also
    // call Node::DetachLayoutTree for this element.
    for (Node* child = firstChild(); child; child = child->nextSibling())
      child->DetachLayoutTree(performing_reattach);
  }

  DetachSucceedingPseudoElements(performing_reattach);

  AttachContext context;
  context.parent = GetLayoutObject();
  context.performing_reattach = performing_reattach;
  context.use_previous_in_flow = true;
  context.next_sibling_valid = true;
  AdjustForceLegacyLayout(GetComputedStyle(), &context.force_legacy_layout);

  AttachPrecedingPseudoElements(context);

  if (shadow_root) {
    shadow_root->AttachLayoutTree(context);
  } else {
    // Can not use ContainerNode::DetachLayoutTree() because that would also
    // call Node::AttachLayoutTree for this element.
    for (Node* child = firstChild(); child; child = child->nextSibling())
      child->AttachLayoutTree(context);
  }

  AttachSucceedingPseudoElements(context);

  ClearChildNeedsReattachLayoutTree();
  ClearNeedsReattachLayoutTree();
}

scoped_refptr<ComputedStyle> Element::StyleForLayoutObject(
    const StyleRecalcContext& style_recalc_context) {
  DCHECK(GetDocument().InStyleRecalc());

  // FIXME: Instead of clearing updates that may have been added from calls to
  // ResolveStyle outside RecalcStyle, we should just never set them if we're
  // not inside RecalcStyle.
  if (ElementAnimations* element_animations = GetElementAnimations())
    element_animations->CssAnimations().ClearPendingUpdate();

  scoped_refptr<ComputedStyle> style =
      HasCustomStyleCallbacks()
          ? CustomStyleForLayoutObject(style_recalc_context)
          : OriginalStyleForLayoutObject(style_recalc_context);
  if (!style) {
    DCHECK(IsPseudoElement());
    return nullptr;
  }

  style->UpdateIsStackingContextWithoutContainment(
      this == GetDocument().documentElement(), IsInTopLayer(),
      IsA<SVGForeignObjectElement>(*this));

  auto* context = GetDisplayLockContext();
  // The common case for most elements is that we don't have a context and have
  // the default (visible) content-visibility value.
  if (UNLIKELY(context || !style->IsContentVisibilityVisible())) {
    if (!context)
      context = &EnsureDisplayLockContext();
    bool is_shaping_deferred =
        GetLayoutObject() && GetLayoutObject()->IsShapingDeferred();
    // If shaping is deferred and |content-visibility| is |visible|, do nothing
    // in order to keep the "deferred" state.
    if (!is_shaping_deferred || !style->IsContentVisibilityVisible()) {
      // If shaping is deferred and |content-visibility| is not |visible|,
      // leave the "deferred" state.
      if (is_shaping_deferred)
        To<LayoutBlockFlow>(GetLayoutObject())->StopDeferringShaping();
      context->SetRequestedState(style->ContentVisibility());
      context->AdjustElementStyle(style.get());
    }
  }

  if (style->DependsOnSizeContainerQueries())
    GetDocument().GetStyleEngine().SetStyleAffectedByLayout();

  return style;
}

scoped_refptr<ComputedStyle> Element::OriginalStyleForLayoutObject(
    const StyleRecalcContext& style_recalc_context) {
  return GetDocument().GetStyleResolver().ResolveStyle(this,
                                                       style_recalc_context);
}

void Element::RecalcStyleForTraversalRootAncestor() {
  if (!ChildNeedsReattachLayoutTree())
    UpdateFirstLetterPseudoElement(StyleUpdatePhase::kRecalc);
  if (HasCustomStyleCallbacks())
    DidRecalcStyle({});
}

bool Element::SkipStyleRecalcForContainer(
    const ComputedStyle& style,
    const StyleRecalcChange& child_change) {
  DCHECK(RuntimeEnabledFeatures::CSSContainerSkipStyleRecalcEnabled());
  if (!child_change.TraversePseudoElements(*this)) {
    // If none of the children or pseudo elements need to be traversed for style
    // recalc, there is no point in marking the subtree as skipped.
    DCHECK(!child_change.TraverseChildren(*this));
    return false;
  }
  if (child_change.ReattachLayoutTree()) {
    if (!LayoutObjectIsNeeded(style) || style.Display() == EDisplay::kInline ||
        style.IsDisplayTableType()) {
      return false;
    }
  } else {
    LayoutObject* layout_object = GetLayoutObject();
    if (!layout_object || !layout_object->SelfNeedsLayout() ||
        !layout_object->IsEligibleForSizeContainment() ||
        !IsGuaranteedToEnterNGBlockNodeLayout(*layout_object)) {
      return false;
    }
  }

  // Don't skip style recalc for form controls. The reason for skipping is a
  // baseline inconsistency issue laying out an input element with a placeholder
  // when interleaving layout and style recalc. This bigger cannon is to avoid
  // potential issues with other peculiarities inside form controls.
  if (IsFormControlElement())
    return false;

  // If we are moving the ::backdrop element to the top layer while laying out
  // its originating element, it means we will add a layout-dirty box as a
  // preceding sibling of the originating element's box which means we will not
  // reach the box for ::backdrop during layout. Don't skip style recalc for
  // children of containers in the top layer for this reason.
  if (IsInTopLayer())
    return false;

  // Store the child_change so that we can continue interleaved style layout
  // from where we left off.
  EnsureElementRareData().EnsureContainerQueryData().SkipStyleRecalc(
      child_change.ForceMarkReattachLayoutTree());

  GetDocument().GetStyleEngine().IncrementSkippedContainerRecalc();

  if (HasCustomStyleCallbacks())
    DidRecalcStyle(child_change);

  // This needs to be cleared to satisty the DCHECKed invariants in
  // Element::RebuildLayoutTree(). ChildNeedsStyleRecalc() is flipped back on
  // before resuming the style recalc when the container is laid out. The stored
  // child_change contains the correct flags to resume recalc of child nodes.
  ClearChildNeedsStyleRecalc();
  return true;
}

void Element::MarkNonSlottedHostChildrenForStyleRecalc() {
  // Mark non-slotted children of shadow hosts for style recalc for forced
  // subtree recalcs when they have ensured computed style outside the flat
  // tree. Elements outside the flat tree are not recomputed during the style
  // recalc step, but we need to make sure the ensured styles are dirtied so
  // that we know to clear out old styles from
  // StyleEngine::ClearEnsuredDescendantStyles() the next time we call
  // getComputedStyle() on any of the descendant elements.
  for (Node* child = firstChild(); child; child = child->nextSibling()) {
    if (child->NeedsStyleRecalc())
      continue;
    if (!child->IsElementNode())
      continue;
    if (auto* style = child->GetComputedStyle()) {
      if (style->IsEnsuredOutsideFlatTree())
        child->SetStyleChangeForNonSlotted();
    }
  }
}

const ComputedStyle* Element::ParentComputedStyle() const {
  Element* parent = LayoutTreeBuilderTraversal::ParentElement(*this);
  if (parent && parent->ChildrenCanHaveStyle()) {
    const ComputedStyle* parent_style = parent->GetComputedStyle();
    if (parent_style && !parent_style->IsEnsuredInDisplayNone())
      return parent_style;
  }
  return nullptr;
}

// Recalculate the style for this element, and if that element notes
// that children must also be recalculated, call ourself recursively
// on any children (via RecalcDescendantStyles()), and/or update
// pseudo-elements.
void Element::RecalcStyle(const StyleRecalcChange change,
                          const StyleRecalcContext& style_recalc_context) {
  DCHECK(InActiveDocument());
  DCHECK(GetDocument().InStyleRecalc());
  DCHECK(!GetDocument().Lifecycle().InDetach());
  DCHECK(!GetForceReattachLayoutTree() || GetComputedStyle())
      << "No need to force a layout tree reattach if we had no computed style";

  DisplayLockStyleScope display_lock_style_scope(this);
  if (HasCustomStyleCallbacks())
    WillRecalcStyle(change);

  StyleRecalcChange child_change = change.ForChildren(*this);
  if (change.ShouldRecalcStyleFor(*this)) {
    child_change = RecalcOwnStyle(change, style_recalc_context);
    if (GetStyleChangeType() == kSubtreeStyleChange)
      child_change = child_change.ForceRecalcDescendants();
    ClearNeedsStyleRecalc();
  } else if (GetForceReattachLayoutTree() ||
             (change.MarkReattachLayoutTree() && GetComputedStyle())) {
    SetNeedsReattachLayoutTree();
    child_change = child_change.ForceReattachLayoutTree();
    ClearNeedsStyleRecalc();
  }

  // We may need to update the internal CSSContainerValues of the
  // ContainerQueryEvaluator if e.g. the value of the 'rem' unit or container-
  // relative units changed. It are not guaranteed to reach RecalcOwnStyle for
  // the container, so this update happens here instead.
  if (ContainerQueryEvaluator* evaluator = GetContainerQueryEvaluator())
    evaluator->UpdateValuesIfNeeded(GetDocument(), *this, child_change);

  // We're done with self style, notify the display lock.
  child_change = display_lock_style_scope.DidUpdateSelfStyle(child_change);
  if (!display_lock_style_scope.ShouldUpdateChildStyle()) {
    display_lock_style_scope.NotifyChildStyleRecalcWasBlocked(child_change);
    if (HasCustomStyleCallbacks())
      DidRecalcStyle(child_change);
    return;
  }

  if (LayoutObject* layout_object = GetLayoutObject()) {
    // If a layout subtree was synchronously detached on DOM or flat tree
    // changes, we need to revisit the element during layout tree rebuild for
    // two reasons:
    //
    // 1. SubtreeDidChange() needs to be called on list-item layout objects
    //    ancestors for markers (see SubtreeDidChange() implementation on list
    //    item layout objects).
    // 2. Whitespace siblings of removed subtrees may change to have their
    //    layout object added or removed as the need for rendering the
    //    whitespace may have changed.
    bool mark_ancestors = layout_object->WasNotifiedOfSubtreeChange();
    if (layout_object->WhitespaceChildrenMayChange()) {
      if (LayoutTreeBuilderTraversal::FirstChild(*this))
        mark_ancestors = true;
      else
        layout_object->SetWhitespaceChildrenMayChange(false);
    }
    if (mark_ancestors)
      MarkAncestorsWithChildNeedsReattachLayoutTree();
  }

  StyleRecalcContext child_recalc_context = style_recalc_context;

  if (RuntimeEnabledFeatures::CSSContainerQueriesEnabled()) {
    if (const ComputedStyle* style = GetComputedStyle()) {
      if (style->CanMatchSizeContainerQueries(*this)) {
        if (RuntimeEnabledFeatures::CSSContainerSkipStyleRecalcEnabled()) {
          if (change.IsSuppressed()) {
            // IsSuppressed() means we are at the root of a container subtree
            // called from UpdateStyleAndLayoutTreeForContainer(). If we skipped
            // the subtree during style recalc, retrieve the StyleRecalcChange
            // which was the current change for the skipped subtree and combine
            // it with any current container flags.
            auto* cq_data = GetContainerQueryData();
            // Should be guaranteed to have ContainerQueryData here since we at
            // least have a ContainerQueryEvaluator at this point.
            DCHECK(cq_data);
            if (cq_data->SkippedStyleRecalc()) {
              child_change =
                  cq_data->ClearAndReturnRecalcChangeForChildren().Combine(
                      child_change);
            }
          } else if (SkipStyleRecalcForContainer(*style, child_change)) {
            return;
          }
        }
      }
      if (style->IsContainerForSizeContainerQueries())
        child_recalc_context.container = this;
    }
  }

  if (child_change.TraversePseudoElements(*this)) {
    UpdatePseudoElement(kPseudoIdBackdrop, child_change, child_recalc_context);
    UpdatePseudoElement(kPseudoIdMarker, child_change, child_recalc_context);
    UpdatePseudoElement(kPseudoIdBefore, child_change, child_recalc_context);
  }

  if (child_change.TraverseChildren(*this)) {
    SelectorFilterParentScope filter_scope(*this);
    if (ShadowRoot* root = GetShadowRoot()) {
      root->RecalcDescendantStyles(child_change, child_recalc_context);
      if (child_change.RecalcDescendants())
        MarkNonSlottedHostChildrenForStyleRecalc();
    } else if (auto* slot = ToHTMLSlotElementIfSupportsAssignmentOrNull(this)) {
      slot->RecalcStyleForSlotChildren(
          child_change, child_recalc_context.ForSlotChildren(*slot));
    } else {
      RecalcDescendantStyles(child_change, child_recalc_context);
    }
  }

  if (child_change.TraversePseudoElements(*this)) {
    UpdatePseudoElement(kPseudoIdAfter, child_change, child_recalc_context);

    // If we are re-attaching us or any of our descendants, we need to attach
    // the descendants before we know if this element generates a ::first-letter
    // and which element the ::first-letter inherits style from.
    if (!child_change.ReattachLayoutTree() && !ChildNeedsReattachLayoutTree()) {
      UpdateFirstLetterPseudoElement(StyleUpdatePhase::kRecalc,
                                     child_recalc_context);
    }
  }

  ClearChildNeedsStyleRecalc();
  // We've updated all the children that needs an update (might be 0).
  display_lock_style_scope.DidUpdateChildStyle();

  if (HasCustomStyleCallbacks())
    DidRecalcStyle(child_change);
}

scoped_refptr<ComputedStyle> Element::PropagateInheritedProperties() {
  if (IsPseudoElement())
    return nullptr;
  if (NeedsStyleRecalc())
    return nullptr;
  if (HasAnimations())
    return nullptr;
  if (HasCustomStyleCallbacks())
    return nullptr;
  const ComputedStyle* parent_style = ParentComputedStyle();
  DCHECK(parent_style);
  const ComputedStyle* style = GetComputedStyle();
  if (!style || style->Animations() || style->Transitions() ||
      style->HasVariableReference() || style->HasVariableDeclaration())
    return nullptr;
  if (style->InsideLink() != EInsideLink::kNotInsideLink) {
    // We cannot do the inherited propagation optimization within links,
    // since -internal-visited-color is handled in CascadeExpansion
    // (which we do not run in that path), and we also have no tracking
    // of whether the property was inherited or not.
    return nullptr;
  }
  if (style->TextDecorationsInEffect() != TextDecorationLine::kNone) {
    // If we have text decorations, they can depend on currentColor,
    // and are normally updated by the StyleAdjuster. We can, however,
    // reach this path when color is modified, leading to the decoration
    // being the wrong color (see crbug.com/1330953). We could rerun
    // the right part of the StyleAdjuster here, but it's simpler just to
    // disable the optimization in such cases (especially as we have already
    // disabled it for links, which are the main causes of text decorations),
    // so we do that.
    return nullptr;
  }
  scoped_refptr<ComputedStyle> new_style = ComputedStyle::Clone(*style);
  new_style->PropagateIndependentInheritedProperties(*parent_style);
  INCREMENT_STYLE_STATS_COUNTER(GetDocument().GetStyleEngine(),
                                independent_inherited_styles_propagated, 1);
  return new_style;
}

static ContainerQueryEvaluator* ComputeContainerQueryEvaluator(
    Element& element,
    const ComputedStyle* old_style,
    const ComputedStyle& new_style) {
  if (!new_style.IsContainerForSizeContainerQueries())
    return nullptr;
  if (LayoutObject* layout_object = element.GetLayoutObject()) {
    if (layout_object->ForceLegacyLayout()) {
      element.GetDocument()
          .GetStyleEngine()
          .ReportUseOfLegacyLayoutWithContainerQueries();
    }
  }
  if (!RuntimeEnabledFeatures::LayoutNGPrintingEnabled() &&
      element.GetDocument().Printing()) {
    return nullptr;
  }
  // If we're switching to display:contents, any existing results cached on
  // ContainerQueryEvaluator are no longer valid, since any style recalc
  // based on that information would *not* be corrected by a subsequent
  // interleaved style recalc, since the element has no layout object.
  if (old_style && !element.LayoutObjectIsNeeded(new_style) &&
      element.LayoutObjectIsNeeded(*old_style)) {
    return MakeGarbageCollected<ContainerQueryEvaluator>();
  }
  // Otherwise, the existing ContainerQueryEvaluator can be used, if any.
  if (auto* evaluator = element.GetContainerQueryEvaluator())
    return evaluator;
  return MakeGarbageCollected<ContainerQueryEvaluator>();
}

static const StyleRecalcChange ApplyComputedStyleDiff(
    const StyleRecalcChange change,
    ComputedStyle::Difference diff) {
  if (change.RecalcDescendants() ||
      diff < ComputedStyle::Difference::kPseudoElementStyle)
    return change;
  if (diff == ComputedStyle::Difference::kDescendantAffecting)
    return change.ForceRecalcDescendants();
  if (diff == ComputedStyle::Difference::kInherited)
    return change.EnsureAtLeast(StyleRecalcChange::kRecalcChildren);
  if (diff == ComputedStyle::Difference::kIndependentInherited)
    return change.EnsureAtLeast(StyleRecalcChange::kIndependentInherit);
  DCHECK(diff == ComputedStyle::Difference::kPseudoElementStyle);
  return change.EnsureAtLeast(StyleRecalcChange::kUpdatePseudoElements);
}

static bool LayoutViewCanHaveChildren(Element& element) {
  if (LayoutObject* view = element.GetDocument().GetLayoutView())
    return view->CanHaveChildren();
  return false;
}

// LayoutTable[Row,Section,Cell] all amend their ComputedStyle in response
// to StyleDidChange. Whenever we recalc the style of an element and find
// no difference, we still need to do ApplyStyleChanges::kYes to perform the
// amendment.
static bool ForceApplyForLegacyLayout(const ComputedStyle& new_style,
                                      const LayoutObject& layout_object) {
  // See LayoutTable[Section, Row]::StyleDidChange.
  if (layout_object.IsLegacyTableRow() ||
      layout_object.IsLegacyTableSection()) {
    if (new_style.HasInFlowPosition())
      return true;
  }
  // See LayoutTableCell::StyleDidChange.
  if (layout_object.IsTableCellLegacy()) {
    if (layout_object.Parent() &&
        new_style.GetWritingMode() !=
            layout_object.Parent()->StyleRef().GetWritingMode()) {
      return true;
    }
  }
  return false;
}

// This function performs two important tasks:
//
//  1. It computes the correct style for the element itself.
//  2. It figures out to what degree we need to propagate changes
//     to child elements (and returns that).
//
// #1 can happen in one out of two ways. The normal way is that ask the
// style resolver to compute the style from scratch (modulo some caching).
// The other one is an optimization for “independent inherited properties”;
// if this recalc is because the parent has changed only properties marked
// as “independent” (i.e., they do not affect other properties; “visibility”
// is an example of such a property), we can reuse our existing style and just
// re-propagate those properties.
//
// #2 happens by diffing the old and new styles. In the extreme example,
// if the two are identical, we don't need to invalidate child elements
// at all. But if they are different, they will usually be different to
// differing degrees; e.g. as noted above, if only independent properties
// changed, we can inform children of that for less work down the tree.
// Our own diff gets combined with the input StyleRecalcChange to produce a
// child recalc policy that's roughly the strictest of the two.
StyleRecalcChange Element::RecalcOwnStyle(
    const StyleRecalcChange change,
    const StyleRecalcContext& style_recalc_context) {
  DCHECK(GetDocument().InStyleRecalc());

  StyleRecalcContext new_style_recalc_context = style_recalc_context;
  if (change.RecalcChildren() && NeedsStyleRecalc()) {
    if (HasRareData()) {
      // This element needs recalc because its parent changed inherited
      // properties or there was some style change in the ancestry which needed
      // a full subtree recalc. In that case we cannot use the BaseComputedStyle
      // optimization.
      if (ElementAnimations* element_animations =
              GetElementRareData()->GetElementAnimations())
        element_animations->SetAnimationStyleChange(false);
    }
    new_style_recalc_context.parent_forces_recalc = true;
  }

  scoped_refptr<ComputedStyle> new_style;
  scoped_refptr<const ComputedStyle> old_style = GetComputedStyle();

  StyleRecalcChange child_change = change.ForChildren(*this);

  const ComputedStyle* parent_style = ParentComputedStyle();
  if (parent_style && old_style && change.IndependentInherit(*old_style)) {
    // When propagating inherited changes, we don't need to do a full style
    // recalc if the only changed properties are independent. In this case, we
    // can simply clone the old ComputedStyle and set these directly.
    new_style = PropagateInheritedProperties();
    if (new_style) {
      // If the child style is copied from the old one, we'll never
      // reach StyleBuilder::ApplyProperty(), hence we'll
      // never set the flag on the parent. this is completely analogous
      // to the code in StyleResolver::ApplyMatchedCache().
      if (new_style->HasExplicitInheritance())
        parent_style->SetChildHasExplicitInheritance();
    }
  }
  if (!new_style && (parent_style || (GetDocument().documentElement() == this &&
                                      LayoutViewCanHaveChildren(*this)))) {
    // This is the normal flow through the function; calculates
    // the element's style more or less from scratch (typically
    // ending up calling StyleResolver::ResolveStyle()).
    new_style = StyleForLayoutObject(new_style_recalc_context);
  }
  if (new_style && !ShouldStoreComputedStyle(*new_style))
    new_style = nullptr;

  if (new_style && !CanSkipRecalcForHighlightPseudos(*new_style)) {
    const StyleHighlightData* parent_highlights =
        parent_style ? parent_style->HighlightData().get() : nullptr;

    if (RuntimeEnabledFeatures::HighlightInheritanceEnabled()) {
      if (new_style->HasPseudoElementStyle(kPseudoIdSelection)) {
        StyleHighlightData& highlights = new_style->MutableHighlightData();
        const ComputedStyle* highlight_parent =
            parent_highlights ? parent_highlights->Selection() : nullptr;
        StyleRequest style_request{kPseudoIdSelection, highlight_parent};
        style_request.originating_element_style = new_style.get();
        highlights.SetSelection(
            StyleForPseudoElement(new_style_recalc_context, style_request));
      }

      if (new_style->HasPseudoElementStyle(kPseudoIdTargetText)) {
        StyleHighlightData& highlights = new_style->MutableHighlightData();
        const ComputedStyle* highlight_parent =
            parent_highlights ? parent_highlights->TargetText() : nullptr;
        StyleRequest style_request{kPseudoIdTargetText, highlight_parent};
        style_request.originating_element_style = new_style.get();
        highlights.SetTargetText(
            StyleForPseudoElement(new_style_recalc_context, style_request));
      }

      if (new_style->HasPseudoElementStyle(kPseudoIdSpellingError)) {
        StyleHighlightData& highlights = new_style->MutableHighlightData();
        const ComputedStyle* highlight_parent =
            parent_highlights ? parent_highlights->SpellingError() : nullptr;
        StyleRequest style_request{kPseudoIdSpellingError, highlight_parent};
        style_request.originating_element_style = new_style.get();
        highlights.SetSpellingError(
            StyleForPseudoElement(new_style_recalc_context, style_request));
      }

      if (new_style->HasPseudoElementStyle(kPseudoIdGrammarError)) {
        StyleHighlightData& highlights = new_style->MutableHighlightData();
        const ComputedStyle* highlight_parent =
            parent_highlights ? parent_highlights->GrammarError() : nullptr;
        StyleRequest style_request{kPseudoIdGrammarError, highlight_parent};
        style_request.originating_element_style = new_style.get();
        highlights.SetGrammarError(
            StyleForPseudoElement(new_style_recalc_context, style_request));
      }
    }
    // Use new inheritance model for custom highlights even if it is not enabled
    // for other types of highlights.
    if (new_style->HasPseudoElementStyle(kPseudoIdHighlight)) {
      const HashSet<AtomicString>* custom_highlight_names =
          new_style->CustomHighlightNames();
      if (custom_highlight_names) {
        for (const AtomicString& custom_highlight_name :
             *custom_highlight_names) {
          StyleHighlightData& highlights = new_style->MutableHighlightData();
          const ComputedStyle* highlight_parent =
              parent_highlights
                  ? parent_highlights->CustomHighlight(custom_highlight_name)
                  : nullptr;
          StyleRequest style_request{kPseudoIdHighlight, highlight_parent,
                                     custom_highlight_name};
          style_request.originating_element_style = new_style.get();
          highlights.SetCustomHighlight(
              custom_highlight_name,
              StyleForPseudoElement(new_style_recalc_context, style_request));
        }
      }
    }
  }

  ComputedStyle::Difference diff =
      ComputedStyle::ComputeDifference(old_style.get(), new_style.get());

  if (old_style && old_style->IsEnsuredInDisplayNone()) {
    // Make sure we traverse children for clearing ensured computed styles
    // further down the tree.
    child_change =
        child_change.EnsureAtLeast(StyleRecalcChange::kRecalcChildren);
    // If the existing style was ensured in a display:none subtree, set it to
    // null to make sure we don't mark for re-attachment if the new style is
    // null.
    old_style = nullptr;
  }

  if (!new_style && HasRareData()) {
    ElementRareData* rare_data = GetElementRareData();
    if (ElementAnimations* element_animations =
            rare_data->GetElementAnimations()) {
      element_animations->CssAnimations().Cancel();
    }
    rare_data->SetContainerQueryEvaluator(nullptr);
    rare_data->ClearPseudoElements();
  }
  SetComputedStyle(new_style);

  if (new_style && !new_style->ContainsSize() &&
      ((new_style->ContainIntrinsicWidth() &&
        new_style->ContainIntrinsicWidth()->HasAuto()) ||
       (new_style->ContainIntrinsicHeight() &&
        new_style->ContainIntrinsicHeight()->HasAuto()))) {
    GetDocument().ObserveForIntrinsicSize(this);
  } else {
    GetDocument().UnobserveForIntrinsicSize(this);
  }

  if (!child_change.ReattachLayoutTree() &&
      (GetForceReattachLayoutTree() || NeedsReattachLayoutTree() ||
       ComputedStyle::NeedsReattachLayoutTree(*this, old_style.get(),
                                              new_style.get()))) {
    child_change = child_change.ForceReattachLayoutTree();
  }

  if (diff == ComputedStyle::Difference::kEqual) {
    INCREMENT_STYLE_STATS_COUNTER(GetDocument().GetStyleEngine(),
                                  styles_unchanged, 1);
    if (!new_style) {
      DCHECK(!old_style);
      return {};
    }
  } else {
    INCREMENT_STYLE_STATS_COUNTER(GetDocument().GetStyleEngine(),
                                  styles_changed, 1);
    probe::DidUpdateComputedStyle(this, old_style.get(), new_style.get());
    if (this == GetDocument().documentElement()) {
      if (GetDocument().GetStyleEngine().UpdateRemUnits(old_style.get(),
                                                        new_style.get())) {
        // Trigger a full document recalc on rem unit changes. We could keep
        // track of which elements depend on rem units like we do for viewport
        // styles, but we assume root font size changes are rare and just
        // recalculate everything.
        child_change = child_change.ForceRecalcDescendants();
      }
    }
    child_change = ApplyComputedStyleDiff(child_change, diff);
    UpdateCallbackSelectors(old_style.get(), new_style.get());
  }

  if (auto* context = GetDisplayLockContext()) {
    // Combine the change from the display lock context. If the context is
    // locked and is preventing child update, we'll store this style recalc
    // change again from Element::RecalcStyle.
    child_change =
        child_change.Combine(context->TakeBlockedStyleRecalcChange());
  }

  if (new_style) {
    if (old_style && !child_change.RecalcChildren() &&
        old_style->HasChildDependentFlags())
      new_style->CopyChildDependentFlagsFrom(*old_style);
    if (RuntimeEnabledFeatures::LayoutNGEnabled()) {
      if (UpdateForceLegacyLayout(*new_style, old_style.get()))
        child_change = child_change.ForceReattachLayoutTree();
    }
    if (RuntimeEnabledFeatures::CSSContainerQueriesEnabled()) {
      auto* evaluator =
          ComputeContainerQueryEvaluator(*this, old_style.get(), *new_style);
      if (evaluator != GetContainerQueryEvaluator()) {
        EnsureElementRareData()
            .EnsureContainerQueryData()
            .SetContainerQueryEvaluator(evaluator);
      } else if (evaluator) {
        DCHECK(old_style);
        evaluator->MarkFontDirtyIfNeeded(*old_style, *new_style);
      }
    }
  }

  if (child_change.ReattachLayoutTree()) {
    if (new_style || old_style)
      SetNeedsReattachLayoutTree();
    return child_change;
  }

  DCHECK(!NeedsReattachLayoutTree())
      << "If we need to reattach the layout tree we should have returned "
         "above. Updating and diffing the style of a LayoutObject which is "
         "about to be deleted is a waste.";

  if (LayoutObject* layout_object = GetLayoutObject()) {
    DCHECK(new_style);
    if (UNLIKELY(layout_object->IsText()) &&
        UNLIKELY(IsA<LayoutNGTextCombine>(layout_object->Parent()))) {
      // Adjust style for <br> and <wbr> in combined text.
      // See http://crbug.com/1228058
      StyleAdjuster::AdjustStyleForCombinedText(*new_style);
    }
    // kEqual means that the computed style didn't change, but there are
    // additional flags in ComputedStyle which may have changed. For instance,
    // the AffectedBy* flags. We don't need to go through the visual
    // invalidation diffing in that case, but we replace the old ComputedStyle
    // object with the new one to ensure the mentioned flags are up to date.
    LayoutObject::ApplyStyleChanges apply_changes =
        diff == ComputedStyle::Difference::kEqual
            ? LayoutObject::ApplyStyleChanges::kNo
            : LayoutObject::ApplyStyleChanges::kYes;

    scoped_refptr<const ComputedStyle> layout_style(std::move(new_style));
    if (auto* pseudo_element = DynamicTo<PseudoElement>(this)) {
      if (layout_style->Display() == EDisplay::kContents) {
        layout_style =
            pseudo_element->LayoutStyleForDisplayContents(*layout_style);
      }
    } else if (auto* html_element = DynamicTo<HTMLHtmlElement>(this)) {
      if (this == GetDocument().documentElement()) {
        layout_style = html_element->LayoutStyleForElement(layout_style);
        // Always apply changes for html root, even if the ComputedStyle may be
        // the same, propagation changes picked up from body style, or
        // previously propagated styles from a removed body element, may still
        // change the LayoutObject's style.
        apply_changes = LayoutObject::ApplyStyleChanges::kYes;
      }
    }
    // TODO(crbug.com/1246826): Remove CompositablePaintAnimationChanged.
    if (RuntimeEnabledFeatures::CompositeBGColorAnimationEnabled()) {
      if (layout_style->CompositablePaintAnimationChanged())
        apply_changes = LayoutObject::ApplyStyleChanges::kYes;
    }
    if (ForceApplyForLegacyLayout(*layout_style, *layout_object))
      apply_changes = LayoutObject::ApplyStyleChanges::kYes;
    layout_object->SetStyle(layout_style.get(), apply_changes);
  }
  return child_change;
}

void Element::RebuildLayoutTree(WhitespaceAttacher& whitespace_attacher) {
  DCHECK(InActiveDocument());
  DCHECK(parentNode());

  if (NeedsReattachLayoutTree()) {
    AttachContext reattach_context;
    reattach_context.parent =
        LayoutTreeBuilderTraversal::ParentLayoutObject(*this);
    if (reattach_context.parent &&
        reattach_context.parent->ForceLegacyLayoutForChildren()) {
      reattach_context.force_legacy_layout = true;
    }
    ReattachLayoutTree(reattach_context);
    whitespace_attacher.DidReattachElement(this,
                                           reattach_context.previous_in_flow);
  } else if (NeedsRebuildChildLayoutTrees(whitespace_attacher) &&
             !ChildStyleRecalcBlockedByDisplayLock() &&
             !SkippedContainerStyleRecalc()) {
    // TODO(crbug.com/972752): Make the condition above a DCHECK instead when
    // style recalc and dirty bit propagation uses flat-tree traversal.
    // We create a local WhitespaceAttacher when rebuilding children of an
    // element with a LayoutObject since whitespace nodes do not rely on layout
    // objects further up the tree. Also, if this Element's layout object is an
    // out-of-flow box, in-flow children should not affect whitespace siblings
    // of the out-of-flow box. However, if this element is a display:contents
    // element. Continue using the passed in attacher as display:contents
    // children may affect whitespace nodes further up the tree as they may be
    // layout tree siblings.
    WhitespaceAttacher local_attacher;
    WhitespaceAttacher* child_attacher;
    LayoutObject* layout_object = GetLayoutObject();
    if (layout_object || !HasDisplayContentsStyle()) {
      whitespace_attacher.DidVisitElement(this);
      if (layout_object && layout_object->WhitespaceChildrenMayChange()) {
        layout_object->SetWhitespaceChildrenMayChange(false);
        local_attacher.SetReattachAllWhitespaceNodes();
      }
      child_attacher = &local_attacher;
    } else {
      child_attacher = &whitespace_attacher;
    }
    RebuildPseudoElementLayoutTree(kPseudoIdAfter, *child_attacher);
    if (GetShadowRoot())
      RebuildShadowRootLayoutTree(*child_attacher);
    else
      RebuildChildrenLayoutTrees(*child_attacher);
    RebuildPseudoElementLayoutTree(kPseudoIdBefore, *child_attacher);
    RebuildMarkerLayoutTree(*child_attacher);
    RebuildPseudoElementLayoutTree(kPseudoIdBackdrop, *child_attacher);
    RebuildFirstLetterLayoutTree();
    ClearChildNeedsReattachLayoutTree();
  }
  DCHECK(!NeedsStyleRecalc());
  DCHECK(!ChildNeedsStyleRecalc() || ChildStyleRecalcBlockedByDisplayLock());
  DCHECK(!NeedsReattachLayoutTree());
  DCHECK(!ChildNeedsReattachLayoutTree() ||
         ChildStyleRecalcBlockedByDisplayLock());
  HandleSubtreeModifications();
}

void Element::RebuildShadowRootLayoutTree(
    WhitespaceAttacher& whitespace_attacher) {
  DCHECK(IsShadowHost(this));
  ShadowRoot* root = GetShadowRoot();
  root->RebuildLayoutTree(whitespace_attacher);
}

void Element::RebuildPseudoElementLayoutTree(
    PseudoId pseudo_id,
    WhitespaceAttacher& whitespace_attacher) {
  if (PseudoElement* element = GetPseudoElement(pseudo_id)) {
    if (element->NeedsRebuildLayoutTree(whitespace_attacher))
      element->RebuildLayoutTree(whitespace_attacher);
  }
}

void Element::RebuildFirstLetterLayoutTree() {
  // Need to create a ::first-letter element here for the following case:
  //
  // <style>#outer::first-letter {...}</style>
  // <div id=outer><div id=inner style="display:none">Text</div></div>
  // <script> outer.offsetTop; inner.style.display = "block" </script>
  //
  // The creation of FirstLetterPseudoElement relies on the layout tree of the
  // block contents. In this case, the ::first-letter element is not created
  // initially since the #inner div is not displayed. On RecalcStyle it's not
  // created since the layout tree is still not built, and AttachLayoutTree
  // for #inner will not update the ::first-letter of outer. However, we end
  // up here for #outer after AttachLayoutTree is called on #inner at which
  // point the layout sub-tree is available for deciding on creating the
  // ::first-letter.
  StyleEngine::AllowMarkForReattachFromRebuildLayoutTreeScope scope(
      GetDocument().GetStyleEngine());

  UpdateFirstLetterPseudoElement(StyleUpdatePhase::kRebuildLayoutTree);
  if (PseudoElement* element = GetPseudoElement(kPseudoIdFirstLetter)) {
    WhitespaceAttacher whitespace_attacher;
    if (element->NeedsRebuildLayoutTree(whitespace_attacher))
      element->RebuildLayoutTree(whitespace_attacher);
  }
}

void Element::RebuildMarkerLayoutTree(WhitespaceAttacher& whitespace_attacher) {
  if (PseudoElement* marker = GetPseudoElement(kPseudoIdMarker)) {
    // In legacy layout, we need to reattach a marker in this case:
    //
    // <ol><li id="outer"><div id="inner">0</div></li></ol>
    // <script>outer.offsetTop; inner.style.display = "inline";</script>
    //
    // An outside marker must be aligned with the 1st line box in the
    // list item, so legacy layout will insert it inside #inner.
    // But when #inner becomes inline, the LayoutBlockFlow is destroyed,
    // so we need to reinsert it.
    //
    // TODO: SetNeedsReattachLayoutTree() should not be called at this point.
    // The layout tree rebuilding for markers should be done similarly to how
    // it is done for ::first-letter.
    if (LayoutObject* layout_object = GetLayoutObject()) {
      if (layout_object->IsListItem() && !marker->GetLayoutObject()) {
        StyleEngine::AllowMarkForReattachFromRebuildLayoutTreeScope scope(
            GetDocument().GetStyleEngine());
        marker->SetNeedsReattachLayoutTree();
      }
    }

    if (marker->NeedsRebuildLayoutTree(whitespace_attacher))
      marker->RebuildLayoutTree(whitespace_attacher);
  }
}

void Element::HandleSubtreeModifications() {
  if (auto* layout_object = GetLayoutObject())
    layout_object->HandleSubtreeModifications();
}

void Element::UpdateCallbackSelectors(const ComputedStyle* old_style,
                                      const ComputedStyle* new_style) {
  Vector<String> empty_vector;
  const Vector<String>& old_callback_selectors =
      old_style ? old_style->CallbackSelectors() : empty_vector;
  const Vector<String>& new_callback_selectors =
      new_style ? new_style->CallbackSelectors() : empty_vector;
  if (old_callback_selectors.IsEmpty() && new_callback_selectors.IsEmpty())
    return;
  if (old_callback_selectors != new_callback_selectors)
    CSSSelectorWatch::From(GetDocument())
        .UpdateSelectorMatches(old_callback_selectors, new_callback_selectors);
}

ShadowRoot& Element::CreateAndAttachShadowRoot(ShadowRootType type) {
#if DCHECK_IS_ON()
  NestingLevelIncrementer slot_assignment_recalc_forbidden_scope(
      GetDocument().SlotAssignmentRecalcForbiddenRecursionDepth());
#endif
  HTMLFrameOwnerElement::PluginDisposeSuspendScope suspend_plugin_dispose;
  EventDispatchForbiddenScope assert_no_event_dispatch;
  ScriptForbiddenScope forbid_script;

  DCHECK(!GetShadowRoot());

  auto* shadow_root = MakeGarbageCollected<ShadowRoot>(GetDocument(), type);

  if (InActiveDocument()) {
    // We need to call child.RemovedFromFlatTree() before setting a shadow
    // root to the element because detach must use the original flat tree
    // structure before attachShadow happens. We cannot use
    // FlatTreeParentChanged() because we don't know at this point whether a
    // slot will be added and the child assigned to a slot on the next slot
    // assignment update.
    for (Node& child : NodeTraversal::ChildrenOf(*this))
      child.RemovedFromFlatTree();
  }
  EnsureElementRareData().SetShadowRoot(*shadow_root);
  shadow_root->SetParentOrShadowHostNode(this);
  shadow_root->SetParentTreeScope(GetTreeScope());
  shadow_root->InsertedInto(*this);

  probe::DidPushShadowRoot(this, shadow_root);

  return *shadow_root;
}

ShadowRoot* Element::GetShadowRoot() const {
  return HasRareData() ? GetElementRareData()->GetShadowRoot() : nullptr;
}

EditContext* Element::editContext() const {
  return HasRareData() ? GetElementRareData()->GetEditContext() : nullptr;
}

void Element::setEditContext(EditContext* edit_context) {
  // If an element is in focus when being attached to a new EditContext,
  // its old EditContext, if it has any, will get blurred,
  // and the new EditContext will automatically get focused.
  if (edit_context && IsFocusedElementInDocument()) {
    if (auto* old_edit_context = editContext())
      old_edit_context->Blur();

    edit_context->Focus();
  }

  if (auto* old_edit_context = editContext())
    old_edit_context->DetachElement(this);

  if (edit_context)
    edit_context->AttachElement(this);

  EnsureElementRareData().SetEditContext(edit_context);
}

struct Element::AffectedByPseudoStateChange {
  bool children_or_siblings{true};
  bool ancestors_or_siblings{true};

  AffectedByPseudoStateChange(CSSSelector::PseudoType pseudo_type,
                              Element& element) {
    switch (pseudo_type) {
      case CSSSelector::kPseudoFocus:
        children_or_siblings = element.ChildrenOrSiblingsAffectedByFocus();
        ancestors_or_siblings =
            element.AncestorsOrSiblingsAffectedByFocusInHas();
        break;
      case CSSSelector::kPseudoFocusVisible:
        children_or_siblings =
            element.ChildrenOrSiblingsAffectedByFocusVisible();
        ancestors_or_siblings =
            element.AncestorsOrSiblingsAffectedByFocusVisibleInHas();
        break;
      case CSSSelector::kPseudoFocusWithin:
        children_or_siblings =
            element.ChildrenOrSiblingsAffectedByFocusWithin();
        ancestors_or_siblings =
            element.AncestorsOrSiblingsAffectedByFocusInHas();
        break;
      case CSSSelector::kPseudoHover:
        children_or_siblings = element.ChildrenOrSiblingsAffectedByHover();
        ancestors_or_siblings =
            element.AncestorsOrSiblingsAffectedByHoverInHas();
        break;
      case CSSSelector::kPseudoActive:
        children_or_siblings = element.ChildrenOrSiblingsAffectedByActive();
        ancestors_or_siblings =
            element.AncestorsOrSiblingsAffectedByActiveInHas();
        break;
      default:
        // Activate :has() invalidation for all allowed pseudo classes.
        //
        // IsPseudoClassValidWithinHasArgument() in css_selector_parser.cc
        // maintains the disallowed pseudo classes inside :has().
        // If a :has() argument contains any of the disallowed pseudo,
        // CSSSelectorParser will drop the argument. If the argument is
        // dropped, RuleFeatureSet will not maintain the pseudo type for
        // :has() invalidation. So, StyleEngine will not do :has()
        // invalidation for the disallowed pseudo type changes even if
        // the Element::PseudoStateChanged() was called with the disallowed
        // pseudo type.
        break;
    }
  }

  AffectedByPseudoStateChange() : ancestors_or_siblings(true) {}  // For testing
};

void Element::PseudoStateChanged(CSSSelector::PseudoType pseudo) {
  PseudoStateChanged(pseudo, AffectedByPseudoStateChange(pseudo, *this));
}

void Element::PseudoStateChangedForTesting(CSSSelector::PseudoType pseudo) {
  PseudoStateChanged(pseudo, AffectedByPseudoStateChange());
}

void Element::PseudoStateChanged(
    CSSSelector::PseudoType pseudo,
    AffectedByPseudoStateChange&& affected_by_pseudo) {
  // We can't schedule invaliation sets from inside style recalc otherwise
  // we'd never process them.
  // TODO(esprehn): Make this an ASSERT and fix places that call into this
  // like HTMLSelectElement.
  if (GetDocument().InStyleRecalc())
    return;
  GetDocument().GetStyleEngine().PseudoStateChangedForElement(
      pseudo, *this, affected_by_pseudo.children_or_siblings,
      affected_by_pseudo.ancestors_or_siblings);
}

bool Element::CanSkipRecalcForHighlightPseudos(
    const ComputedStyle& new_style) const {
  // If we are a root element (our parent is a Document or ShadowRoot), we need
  // to recalc iff there are any highlight rules for the pseudo in question,
  // regardless of whether or not they are non-universal.
  if (parentNode() == ContainingTreeScope().RootNode())
    return false;

  // If the parent matched any non-universal highlight rules, then we need
  // to recalc, in case there are universal highlight rules.
  bool parent_non_universal =
      ParentComputedStyle() != nullptr &&
      ParentComputedStyle()->HasNonUniversalHighlightPseudoStyles();

  // If we matched any non-universal highlight rules, then we need to recalc
  // and our children also need to recalc (see above).
  bool self_non_universal = new_style.HasNonUniversalHighlightPseudoStyles();

  return !parent_non_universal && !self_non_universal;
}

void Element::SetAnimationStyleChange(bool animation_style_change) {
  if (animation_style_change && GetDocument().InStyleRecalc())
    return;
  if (!HasRareData())
    return;
  if (ElementAnimations* element_animations =
          GetElementRareData()->GetElementAnimations())
    element_animations->SetAnimationStyleChange(animation_style_change);
}

void Element::SetNeedsAnimationStyleRecalc() {
  if (GetDocument().InStyleRecalc())
    return;
  if (GetDocument().GetStyleEngine().InApplyAnimationUpdate())
    return;
  if (GetStyleChangeType() != kNoStyleChange)
    return;

  SetNeedsStyleRecalc(kLocalStyleChange, StyleChangeReasonForTracing::Create(
                                             style_change_reason::kAnimation));

  // Setting this flag to 'true' only makes sense if there's an existing style,
  // otherwise there is no previous style to use as the basis for the new one.
  if (NeedsStyleRecalc() && GetComputedStyle() &&
      !GetComputedStyle()->IsEnsuredInDisplayNone()) {
    SetAnimationStyleChange(true);
  }
}

void Element::SetNeedsCompositingUpdate() {
  if (!GetDocument().IsActive())
    return;
  LayoutBoxModelObject* layout_object = GetLayoutBoxModelObject();
  if (!layout_object)
    return;

  auto* painting_layer = layout_object->PaintingLayer();
  // Repaint because the foreign layer may have changed.
  painting_layer->SetNeedsRepaint();

  // Changes to AdditionalCompositingReasons can change direct compositing
  // reasons which affect paint properties.
  if (layout_object->CanHaveAdditionalCompositingReasons())
    layout_object->SetNeedsPaintPropertyUpdate();
}

void Element::SetRegionCaptureCropId(
    std::unique_ptr<RegionCaptureCropId> crop_id) {
  ElementRareData& rare_data = EnsureElementRareData();

  CHECK(!rare_data.GetRegionCaptureCropId());

  // Propagate efficient form through the rendering pipeline.
  rare_data.SetRegionCaptureCropId(std::move(crop_id));

  // The crop ID needs to be propagated to the paint system by the time that
  // capture begins. The API requires the implementation to propagate the
  // token right away, so we force invalidate here.
  if (GetLayoutObject()) {
    GetLayoutObject()->SetShouldDoFullPaintInvalidation();
  }
}

const RegionCaptureCropId* Element::GetRegionCaptureCropId() const {
  return HasRareData() ? GetElementRareData()->GetRegionCaptureCropId()
                       : nullptr;
}

void Element::ResetForceLegacyLayoutForPrinting() {
  SetShouldForceLegacyLayoutForChild(
      NeedsLegacyLayoutForEntireDocument(GetDocument()));
}

void Element::SetCustomElementDefinition(CustomElementDefinition* definition) {
  DCHECK(definition);
  DCHECK(!GetCustomElementDefinition());
  EnsureElementRareData().SetCustomElementDefinition(definition);
  SetCustomElementState(CustomElementState::kCustom);
}

CustomElementDefinition* Element::GetCustomElementDefinition() const {
  if (HasRareData())
    return GetElementRareData()->GetCustomElementDefinition();
  return nullptr;
}

void Element::SetIsValue(const AtomicString& is_value) {
  DCHECK(IsValue().IsNull()) << "SetIsValue() should be called at most once.";
  EnsureElementRareData().SetIsValue(is_value);
}

const AtomicString& Element::IsValue() const {
  if (HasRareData())
    return GetElementRareData()->IsValue();
  return g_null_atom;
}

void Element::SetDidAttachInternals() {
  EnsureElementRareData().SetDidAttachInternals();
}

bool Element::DidAttachInternals() const {
  return HasRareData() && GetElementRareData()->DidAttachInternals();
}

ElementInternals& Element::EnsureElementInternals() {
  return EnsureElementRareData().EnsureElementInternals(To<HTMLElement>(*this));
}

const ElementInternals* Element::GetElementInternals() const {
  return HasRareData() ? GetElementRareData()->GetElementInternals() : nullptr;
}

bool Element::CanAttachShadowRoot() const {
  const AtomicString& tag_name = localName();
  // Checking IsCustomElement() here is just an optimization
  // because IsValidName is not cheap.
  return (IsCustomElement() && CustomElement::IsValidName(tag_name)) ||
         tag_name == html_names::kArticleTag ||
         tag_name == html_names::kAsideTag ||
         tag_name == html_names::kBlockquoteTag ||
         tag_name == html_names::kBodyTag || tag_name == html_names::kDivTag ||
         tag_name == html_names::kFooterTag || tag_name == html_names::kH1Tag ||
         tag_name == html_names::kH2Tag || tag_name == html_names::kH3Tag ||
         tag_name == html_names::kH4Tag || tag_name == html_names::kH5Tag ||
         tag_name == html_names::kH6Tag || tag_name == html_names::kHeaderTag ||
         tag_name == html_names::kNavTag || tag_name == html_names::kMainTag ||
         tag_name == html_names::kPTag || tag_name == html_names::kSectionTag ||
         tag_name == html_names::kSelectmenuTag ||
         tag_name == html_names::kSpanTag;
}

const char* Element::ErrorMessageForAttachShadow() const {
  // https://dom.spec.whatwg.org/#concept-attach-a-shadow-root
  // 1. If shadow host’s namespace is not the HTML namespace, then throw a
  // "NotSupportedError" DOMException.
  // 2. If shadow host’s local name is not a valid custom element name,
  // "article", "aside", "blockquote", "body", "div", "footer", "h1", "h2",
  // "h3", "h4", "h5", "h6", "header", "main", "nav", "p", "section", or "span",
  // then throw a "NotSupportedError" DOMException.
  if (!CanAttachShadowRoot()) {
    return "This element does not support attachShadow";
  }

  // 3. If shadow host’s local name is a valid custom element name, or shadow
  // host’s is value is not null, then:
  // 3.1 Let definition be the result of looking up a custom element
  // definition given shadow host’s node document, its namespace, its local
  // name, and its is value.
  // 3.2 If definition is not null and definition’s
  // disable shadow is true, then throw a "NotSupportedError" DOMException.
  // Note: Checking IsCustomElement() is just an optimization because
  // IsValidName() is not cheap.
  if (IsCustomElement() &&
      (CustomElement::IsValidName(localName()) || !IsValue().IsNull())) {
    auto* registry = CustomElement::Registry(*this);
    auto* definition =
        registry ? registry->DefinitionForName(IsValue().IsNull() ? localName()
                                                                  : IsValue())
                 : nullptr;
    if (definition && definition->DisableShadow()) {
      return "attachShadow() is disabled by disabledFeatures static field.";
    }
  }

  // 4. If shadow host has a non-null shadow root whose "is declarative shadow
  // root" property is false, then throw an "NotSupportedError" DOMException.
  if (GetShadowRoot() && !GetShadowRoot()->IsDeclarativeShadowRoot()) {
    return "Shadow root cannot be created on a host "
           "which already hosts a shadow tree.";
  }
  return nullptr;
}

ShadowRoot* Element::attachShadow(const ShadowRootInit* shadow_root_init_dict,
                                  ExceptionState& exception_state) {
  DCHECK(shadow_root_init_dict->hasMode());
  ShadowRootType type = shadow_root_init_dict->mode() == "open"
                            ? ShadowRootType::kOpen
                            : ShadowRootType::kClosed;
  if (type == ShadowRootType::kOpen)
    UseCounter::Count(GetDocument(), WebFeature::kElementAttachShadowOpen);
  else
    UseCounter::Count(GetDocument(), WebFeature::kElementAttachShadowClosed);

  auto focus_delegation = (shadow_root_init_dict->hasDelegatesFocus() &&
                           shadow_root_init_dict->delegatesFocus())
                              ? FocusDelegation::kDelegateFocus
                              : FocusDelegation::kNone;
  auto slot_assignment = (shadow_root_init_dict->hasSlotAssignment() &&
                          shadow_root_init_dict->slotAssignment() == "manual")
                             ? SlotAssignmentMode::kManual
                             : SlotAssignmentMode::kNamed;
  if (const char* error_message = ErrorMessageForAttachShadow()) {
    exception_state.ThrowDOMException(DOMExceptionCode::kNotSupportedError,
                                      error_message);
    return nullptr;
  }

  ShadowRoot& shadow_root =
      AttachShadowRootInternal(type, focus_delegation, slot_assignment);

  // Ensure that the returned shadow root is not marked as declarative so that
  // attachShadow() calls after the first one do not succeed for a shadow host
  // with a declarative shadow root.
  shadow_root.SetIsDeclarativeShadowRoot(false);
  return &shadow_root;
}

void Element::AttachDeclarativeShadowRoot(HTMLTemplateElement* template_element,
                                          ShadowRootType type,
                                          FocusDelegation focus_delegation,
                                          SlotAssignmentMode slot_assignment) {
  DCHECK(template_element);
  DCHECK(type == ShadowRootType::kOpen || type == ShadowRootType::kClosed);
  UseCounter::Count(GetDocument(), WebFeature::kDeclarativeShadowRoot);

  // 12. Run attach a shadow root with shadow host equal to declarative shadow
  // host element, mode equal to declarative shadow mode, and delegates focus
  // equal to declarative shadow delegates focus. If an exception was thrown by
  // attach a shadow root, catch it, and ignore the exception.
  if (const char* error_message = ErrorMessageForAttachShadow()) {
    template_element->SetDeclarativeShadowRootType(
        DeclarativeShadowRootType::kNone);
    GetDocument().AddConsoleMessage(MakeGarbageCollected<ConsoleMessage>(
        mojom::blink::ConsoleMessageSource::kOther,
        mojom::blink::ConsoleMessageLevel::kError, error_message));
    return;
  }
  ShadowRoot& shadow_root =
      AttachShadowRootInternal(type, focus_delegation, slot_assignment);
  // 13.1. Set declarative shadow host element's shadow host's "is declarative
  // shadow root" property to true.
  shadow_root.SetIsDeclarativeShadowRoot(true);
  // 13.NEW. Set declarative shadow host element's shadow host's "available
  // to element internals" to true.
  shadow_root.SetAvailableToElementInternals(true);

  // 13.2. Append the declarative template element's DocumentFragment to the
  // newly-created shadow root.
  shadow_root.ParserTakeAllChildrenFrom(
      *template_element->DeclarativeShadowContent());
  // 13.3. Remove the declarative template element from the document.
  if (template_element->parentNode())
    template_element->parentNode()->ParserRemoveChild(*template_element);
}

ShadowRoot& Element::CreateUserAgentShadowRoot() {
  DCHECK(!GetShadowRoot());
  GetDocument().SetContainsShadowRoot();
  return CreateAndAttachShadowRoot(ShadowRootType::kUserAgent);
}

ShadowRoot& Element::AttachShadowRootInternal(
    ShadowRootType type,
    FocusDelegation focus_delegation,
    SlotAssignmentMode slot_assignment_mode) {
  // SVG <use> is a special case for using this API to create a closed shadow
  // root.
  DCHECK(CanAttachShadowRoot() || IsA<SVGUseElement>(*this));
  DCHECK(type == ShadowRootType::kOpen || type == ShadowRootType::kClosed)
      << type;
  DCHECK(!AlwaysCreateUserAgentShadowRoot());

  GetDocument().SetContainsShadowRoot();

  if (auto* shadow_root = GetShadowRoot()) {
    // NEW. If shadow host has a non-null shadow root whose "is declarative
    // shadow root property is true, then remove all of shadow root’s children,
    // in tree order. Return shadow host’s shadow root.
    DCHECK(shadow_root->IsDeclarativeShadowRoot());
    shadow_root->RemoveChildren();
    return *shadow_root;
  }

  // 5. Let shadow be a new shadow root whose node document is this’s node
  // document, host is this, and mode is init’s mode.
  ShadowRoot& shadow_root = CreateAndAttachShadowRoot(type);
  // 6. Set shadow’s delegates focus to init’s delegatesFocus.
  shadow_root.SetDelegatesFocus(focus_delegation ==
                                FocusDelegation::kDelegateFocus);
  // NEW. Set shadow’s "is declarative shadow root" property to false.
  shadow_root.SetIsDeclarativeShadowRoot(false);

  // 7. If this’s custom element state is "precustomized" or "custom", then set
  // shadow’s available to element internals to true.
  shadow_root.SetAvailableToElementInternals(
      !(IsCustomElement() &&
        GetCustomElementState() != CustomElementState::kCustom &&
        GetCustomElementState() != CustomElementState::kPreCustomized));

  shadow_root.SetSlotAssignmentMode(slot_assignment_mode);
  // 8. Set this’s shadow root to shadow.
  return shadow_root;
}

ShadowRoot* Element::OpenShadowRoot() const {
  ShadowRoot* root = GetShadowRoot();
  return root && root->GetType() == ShadowRootType::kOpen ? root : nullptr;
}

ShadowRoot* Element::ClosedShadowRoot() const {
  ShadowRoot* root = GetShadowRoot();
  if (!root)
    return nullptr;
  return root->GetType() == ShadowRootType::kClosed ? root : nullptr;
}

ShadowRoot* Element::AuthorShadowRoot() const {
  ShadowRoot* root = GetShadowRoot();
  if (!root)
    return nullptr;
  return !root->IsUserAgent() ? root : nullptr;
}

ShadowRoot* Element::UserAgentShadowRoot() const {
  ShadowRoot* root = GetShadowRoot();
  DCHECK(!root || root->IsUserAgent());
  return root;
}

ShadowRoot& Element::EnsureUserAgentShadowRoot() {
  if (ShadowRoot* shadow_root = UserAgentShadowRoot()) {
    DCHECK(shadow_root->GetType() == ShadowRootType::kUserAgent);
    return *shadow_root;
  }
  ShadowRoot& shadow_root = CreateUserAgentShadowRoot();
  DidAddUserAgentShadowRoot(shadow_root);
  return shadow_root;
}

bool Element::ChildTypeAllowed(NodeType type) const {
  switch (type) {
    case kElementNode:
    case kTextNode:
    case kCommentNode:
    case kProcessingInstructionNode:
    case kCdataSectionNode:
      return true;
    default:
      break;
  }
  return false;
}

namespace {

bool HasSiblingsForNonEmpty(const Node* sibling,
                            Node* (*next_func)(const Node&)) {
  for (; sibling; sibling = next_func(*sibling)) {
    if (sibling->IsElementNode())
      return true;
    auto* text_node = DynamicTo<Text>(sibling);
    if (text_node && !text_node->data().IsEmpty())
      return true;
  }
  return false;
}

}  // namespace

void Element::CheckForEmptyStyleChange(const Node* node_before_change,
                                       const Node* node_after_change) {
  if (!InActiveDocument())
    return;
  if (!StyleAffectedByEmpty())
    return;
  if (HasSiblingsForNonEmpty(node_before_change,
                             NodeTraversal::PreviousSibling) ||
      HasSiblingsForNonEmpty(node_after_change, NodeTraversal::NextSibling)) {
    return;
  }
  PseudoStateChanged(CSSSelector::kPseudoEmpty);
}

void Element::ChildrenChanged(const ChildrenChange& change) {
  ContainerNode::ChildrenChanged(change);

  CheckForEmptyStyleChange(change.sibling_before_change,
                           change.sibling_after_change);

  if (!change.ByParser() && change.IsChildElementChange()) {
    Element* changed_element = To<Element>(change.sibling_changed);
    bool removed = change.type == ChildrenChangeType::kElementRemoved;
    CheckForSiblingStyleChanges(
        removed ? kSiblingElementRemoved : kSiblingElementInserted,
        changed_element, change.sibling_before_change,
        change.sibling_after_change);
    if (removed) {
      GetDocument()
          .GetStyleEngine()
          .ScheduleInvalidationsForHasPseudoAffectedByRemoval(
              this, change.sibling_before_change, *changed_element);
    } else {
      GetDocument()
          .GetStyleEngine()
          .ScheduleInvalidationsForHasPseudoAffectedByInsertion(
              this, change.sibling_before_change, *changed_element);
    }
  }

  if (ShadowRoot* shadow_root = GetShadowRoot())
    shadow_root->SetNeedsAssignmentRecalc();
}

void Element::FinishParsingChildren() {
  SetIsFinishedParsingChildren(true);
  CheckForEmptyStyleChange(this, this);
  CheckForSiblingStyleChanges(kFinishedParsingChildren, nullptr, lastChild(),
                              nullptr);
  GetDocument()
      .GetStyleEngine()
      .ScheduleInvalidationsForHasPseudoAffectedByInsertion(parentElement(),
                                                            lastChild(), *this);
}

AttrNodeList* Element::GetAttrNodeList() {
  return HasRareData() ? GetElementRareData()->GetAttrNodeList() : nullptr;
}

void Element::RemoveAttrNodeList() {
  DCHECK(GetAttrNodeList());
  if (HasRareData())
    GetElementRareData()->RemoveAttrNodeList();
}

Attr* Element::setAttributeNodeNS(Attr* attr, ExceptionState& exception_state) {
  return setAttributeNode(attr, exception_state);
}

Attr* Element::removeAttributeNode(Attr* attr,
                                   ExceptionState& exception_state) {
  if (attr->ownerElement() != this) {
    exception_state.ThrowDOMException(
        DOMExceptionCode::kNotFoundError,
        "The node provided is owned by another element.");
    return nullptr;
  }

  DCHECK_EQ(GetDocument(), attr->GetDocument());

  SynchronizeAttribute(attr->GetQualifiedName());

  wtf_size_t index =
      GetElementData()->Attributes().FindIndex(attr->GetQualifiedName());
  if (index == kNotFound) {
    exception_state.ThrowDOMException(
        DOMExceptionCode::kNotFoundError,
        "The attribute was not found on this element.");
    return nullptr;
  }

  DetachAttrNodeAtIndex(attr, index);
  return attr;
}

void Element::ParseAttribute(const AttributeModificationParams& params) {
  if (params.name == html_names::kTabindexAttr) {
    int tabindex = 0;
    if (params.new_value.IsEmpty() ||
        !ParseHTMLInteger(params.new_value, tabindex)) {
      ClearTabIndexExplicitlyIfNeeded();
    } else {
      // We only set when value is in integer range.
      SetTabIndexExplicitly();
    }
  } else if (params.name == html_names::kFocusgroupAttr) {
    // Only update the focusgroup flags when the node has been added to the
    // tree. This is because the computed focusgroup value will depend on the
    // focusgroup value of its closest ancestor node that is a focusgroup, if
    // any.
    if (parentNode()) {
      UpdateFocusgroup(params.new_value);
    }
  } else if (params.name == xml_names::kLangAttr) {
    PseudoStateChanged(CSSSelector::kPseudoLang);
  } else if (params.name == html_names::kPopupAttr) {
    UpdatePopupAttribute(params.new_value);
  }
}

bool Element::ParseAttributeName(QualifiedName& out,
                                 const AtomicString& namespace_uri,
                                 const AtomicString& qualified_name,
                                 ExceptionState& exception_state) {
  AtomicString prefix, local_name;
  if (!Document::ParseQualifiedName(qualified_name, prefix, local_name,
                                    exception_state))
    return false;
  DCHECK(!exception_state.HadException());

  QualifiedName q_name(prefix, local_name, namespace_uri);

  if (!Document::HasValidNamespaceForAttributes(q_name)) {
    exception_state.ThrowDOMException(
        DOMExceptionCode::kNamespaceError,
        "'" + namespace_uri + "' is an invalid namespace for attributes.");
    return false;
  }

  out = q_name;
  return true;
}

void Element::setAttributeNS(const AtomicString& namespace_uri,
                             const AtomicString& qualified_name,
                             String value,
                             ExceptionState& exception_state) {
  QualifiedName parsed_name = g_any_name;
  if (!ParseAttributeName(parsed_name, namespace_uri, qualified_name,
                          exception_state))
    return;

  AtomicString trusted_value(TrustedTypesCheckFor(
      ExpectedTrustedTypeForAttribute(parsed_name), std::move(value),
      GetExecutionContext(), exception_state));
  if (exception_state.HadException())
    return;

  setAttribute(parsed_name, trusted_value);
}

void Element::setAttributeNS(const AtomicString& namespace_uri,
                             const AtomicString& qualified_name,
                             const V8TrustedType* trusted_string,
                             ExceptionState& exception_state) {
  QualifiedName parsed_name = g_any_name;
  if (!ParseAttributeName(parsed_name, namespace_uri, qualified_name,
                          exception_state))
    return;

  AtomicString value(TrustedTypesCheckFor(
      ExpectedTrustedTypeForAttribute(parsed_name), trusted_string,
      GetExecutionContext(), exception_state));
  if (exception_state.HadException())
    return;

  setAttribute(parsed_name, value);
}

void Element::RemoveAttributeInternal(wtf_size_t index,
                                      AttributeModificationReason reason) {
  MutableAttributeCollection attributes =
      EnsureUniqueElementData().Attributes();
  SECURITY_DCHECK(index < attributes.size());

  QualifiedName name = attributes[index].GetName();
  AtomicString value_being_removed = attributes[index].Value();

  if (reason !=
      AttributeModificationReason::kBySynchronizationOfLazyAttribute) {
    if (!value_being_removed.IsNull()) {
      WillModifyAttribute(name, value_being_removed, g_null_atom);
    } else if (GetCustomElementState() == CustomElementState::kCustom) {
      // This would otherwise be enqueued by willModifyAttribute.
      CustomElement::EnqueueAttributeChangedCallback(
          *this, name, value_being_removed, g_null_atom);
    }
  }

  if (Attr* attr_node = AttrIfExists(name))
    DetachAttrNodeFromElementWithValue(attr_node, attributes[index].Value());

  attributes.Remove(index);

  if (reason != AttributeModificationReason::kBySynchronizationOfLazyAttribute)
    DidRemoveAttribute(name, value_being_removed);
}

void Element::AppendAttributeInternal(const QualifiedName& name,
                                      const AtomicString& value,
                                      AttributeModificationReason reason) {
  if (reason != AttributeModificationReason::kBySynchronizationOfLazyAttribute)
    WillModifyAttribute(name, g_null_atom, value);
  EnsureUniqueElementData().Attributes().Append(name, value);
  if (reason != AttributeModificationReason::kBySynchronizationOfLazyAttribute)
    DidAddAttribute(name, value);
}

void Element::removeAttributeNS(const AtomicString& namespace_uri,
                                const AtomicString& local_name) {
  removeAttribute(QualifiedName(g_null_atom, local_name, namespace_uri));
}

Attr* Element::getAttributeNode(const AtomicString& local_name) {
  if (!GetElementData())
    return nullptr;
  WTF::AtomicStringTable::WeakResult hint =
      WeakLowercaseIfNecessary(local_name);
  SynchronizeAttributeHinted(local_name, hint);
  const Attribute* attribute =
      GetElementData()->Attributes().FindHinted(local_name, hint);
  if (!attribute)
    return nullptr;
  return EnsureAttr(attribute->GetName());
}

Attr* Element::getAttributeNodeNS(const AtomicString& namespace_uri,
                                  const AtomicString& local_name) {
  if (!GetElementData())
    return nullptr;
  QualifiedName q_name(g_null_atom, local_name, namespace_uri);
  SynchronizeAttribute(q_name);
  const Attribute* attribute = GetElementData()->Attributes().Find(q_name);
  if (!attribute)
    return nullptr;
  return EnsureAttr(attribute->GetName());
}

bool Element::hasAttribute(const AtomicString& local_name) const {
  if (!GetElementData())
    return false;
  WTF::AtomicStringTable::WeakResult hint =
      WeakLowercaseIfNecessary(local_name);
  SynchronizeAttributeHinted(local_name, hint);
  return GetElementData()->Attributes().FindIndexHinted(local_name, hint) !=
         kNotFound;
}

bool Element::hasAttributeNS(const AtomicString& namespace_uri,
                             const AtomicString& local_name) const {
  if (!GetElementData())
    return false;
  QualifiedName q_name(g_null_atom, local_name, namespace_uri);
  SynchronizeAttribute(q_name);
  return GetElementData()->Attributes().Find(q_name);
}

bool Element::DelegatesFocus() const {
  return AuthorShadowRoot() && AuthorShadowRoot()->delegatesFocus();
}

// https://html.spec.whatwg.org/C/#get-the-focusable-area
Element* Element::GetFocusableArea() const {
  DCHECK(!IsFocusable());
  // TODO(crbug.com/1018619): Support AREA -> IMG delegation.
  if (!DelegatesFocus())
    return nullptr;
  Document& doc = GetDocument();
  UseCounter::Count(doc, WebFeature::kDelegateFocus);

  // TODO(https://github.com/w3c/webcomponents/issues/840): We'd like to
  // standardize this behavior.
  Element* focused_element = doc.FocusedElement();
  if (focused_element && IsShadowIncludingInclusiveAncestorOf(*focused_element))
    return focused_element;

  // Slide the focus to its inner node.
  return FocusController::FindFocusableElementInShadowHost(*this);
}

// https://html.spec.whatwg.org/C/#autofocus-delegate
// TODO(https://crbug.com/383230): use this more broadly, including in
// FocusController::FindFocusableElementInShadowHost() which will at that time
// probably be renamed to "focus delegate".
Element* Element::GetAutofocusDelegate() const {
  for (Node& node : NodeTraversal::DescendantsOf(*this)) {
    auto* element = DynamicTo<Element>(node);
    if (!element)
      continue;

    if (!element->IsAutofocusable())
      continue;

    Element* focusable_area =
        element->IsFocusable() ? element : element->GetFocusableArea();
    if (!focusable_area)
      continue;

    // Step checking click-focusability and focus trigger omitted for now; it
    // may be needed as part of https://crbug.com/383230.

    return focusable_area;
  }

  return nullptr;
}

void Element::focusForBindings() {
  Focus(FocusParams(SelectionBehaviorOnFocus::kRestore,
                    mojom::blink::FocusType::kScript, nullptr));
}

void Element::focusForBindings(const FocusOptions* options) {
  Focus(FocusParams(SelectionBehaviorOnFocus::kRestore,
                    mojom::blink::FocusType::kScript, nullptr, options));
}

void Element::Focus() {
  Focus(FocusParams());
}

void Element::Focus(const FocusOptions* options) {
  Focus(FocusParams(SelectionBehaviorOnFocus::kRestore,
                    mojom::blink::FocusType::kNone, nullptr, options));
}

void Element::Focus(const FocusParams& params) {
  if (!isConnected())
    return;

  if (!GetDocument().IsFocusAllowed())
    return;

  if (GetDocument().FocusedElement() == this)
    return;

  if (!GetDocument().IsActive())
    return;

  auto* frame_owner_element = DynamicTo<HTMLFrameOwnerElement>(this);
  if (frame_owner_element && frame_owner_element->contentDocument() &&
      frame_owner_element->contentDocument()->UnloadStarted())
    return;

  if (HasValidPopupAttribute() &&
      hasAttribute(html_names::kDelegatesfocusAttr)) {
    DCHECK(RuntimeEnabledFeatures::HTMLPopupAttributeEnabled());
    if (auto* node_to_focus = GetPopupFocusableArea(/*autofocus_only=*/false)) {
      node_to_focus->Focus(params);
    }
    return;
  }

  // Ensure we have clean style (including forced display locks).
  GetDocument().UpdateStyleAndLayoutTreeForNode(this);

  // https://html.spec.whatwg.org/C/#focusing-steps
  //
  // 1. If new focus target is not a focusable area, ...
  if (!IsFocusable()) {
    if (Element* new_focus_target = GetFocusableArea()) {
      // Unlike the specification, we re-run focus() for new_focus_target
      // because we can't change |this| in a member function.
      new_focus_target->Focus(FocusParams(SelectionBehaviorOnFocus::kReset,
                                          mojom::blink::FocusType::kForward,
                                          nullptr, params.options));
    }
    // 2. If new focus target is null, then:
    //  2.1. If no fallback target was specified, then return.
    return;
  }
  // If a script called focus(), then the type would be kScript. This means
  // we are activating because of a script action (kScriptFocus). Otherwise,
  // this is a user activation (kUserFocus).
  ActivateDisplayLockIfNeeded(params.type == mojom::blink::FocusType::kScript
                                  ? DisplayLockActivationReason::kScriptFocus
                                  : DisplayLockActivationReason::kUserFocus);

  if (!GetDocument().GetPage()->GetFocusController().SetFocusedElement(
          this, GetDocument().GetFrame(), params))
    return;

  if (GetDocument().FocusedElement() == this) {
    ChromeClient& chrome_client = GetDocument().GetPage()->GetChromeClient();
    if (GetDocument().GetFrame()->HasStickyUserActivation()) {
      // Bring up the keyboard in the context of anything triggered by a user
      // gesture. Since tracking that across arbitrary boundaries (eg.
      // animations) is difficult, for now we match IE's heuristic and bring
      // up the keyboard if there's been any gesture since load.
      chrome_client.ShowVirtualKeyboardOnElementFocus(
          *GetDocument().GetFrame());
    }

    // TODO(bebeaudr): We might want to move the following code into the
    // HasStickyUserActivation condition above once https://crbug.com/1208874 is
    // fixed.
    //
    // Trigger a tooltip to show for the newly focused element only when the
    // focus was set resulting from a keyboard action.
    //
    // TODO(bebeaudr): To also trigger a tooltip when the |params.type| is
    // kSpatialNavigation, we'll first have to ensure that the fake mouse move
    // event fired by `SpatialNavigationController::DispatchMouseMoveEvent` does
    // not lead to a cursor triggered tooltip update. The only tooltip update
    // that there should be in that case is the one triggered from the spatial
    // navigation keypress. This issue is tracked in https://crbug.com/1206446.
    bool is_focused_from_keypress = false;
    switch (params.type) {
      case mojom::blink::FocusType::kScript:
        if (GetDocument()
                .GetFrame()
                ->LocalFrameRoot()
                .GetEventHandler()
                .IsHandlingKeyEvent()) {
          is_focused_from_keypress = true;
        }
        break;
      case mojom::blink::FocusType::kForward:
      case mojom::blink::FocusType::kBackward:
      case mojom::blink::FocusType::kAccessKey:
        is_focused_from_keypress = true;
        break;
      default:
        break;
    }

    if (is_focused_from_keypress) {
      chrome_client.ElementFocusedFromKeypress(*GetDocument().GetFrame(), this);
    } else {
      chrome_client.ClearKeyboardTriggeredTooltip(*GetDocument().GetFrame());
    }
  }
}

void Element::UpdateSelectionOnFocus(
    SelectionBehaviorOnFocus selection_behavior) {
  UpdateSelectionOnFocus(selection_behavior, FocusOptions::Create());
}

void Element::UpdateSelectionOnFocus(
    SelectionBehaviorOnFocus selection_behavior,
    const FocusOptions* options) {
  if (selection_behavior == SelectionBehaviorOnFocus::kNone)
    return;
  if (IsRootEditableElement(*this)) {
    LocalFrame* frame = GetDocument().GetFrame();
    if (!frame)
      return;

    // When focusing an editable element in an iframe, don't reset the selection
    // if it already contains a selection.
    if (this == frame->Selection()
                    .ComputeVisibleSelectionInDOMTreeDeprecated()
                    .RootEditableElement())
      return;

    // FIXME: We should restore the previous selection if there is one.
    // Passing DoNotSetFocus as this function is called after
    // FocusController::setFocusedElement() and we don't want to change the
    // focus to a new Element.
    frame->Selection().SetSelection(
        SelectionInDOMTree::Builder()
            .Collapse(FirstPositionInOrBeforeNode(*this))
            .Build(),
        SetSelectionOptions::Builder()
            .SetShouldCloseTyping(true)
            .SetShouldClearTypingStyle(true)
            .SetDoNotSetFocus(true)
            .Build());
    if (!options->preventScroll())
      frame->Selection().RevealSelection();
  } else if (GetLayoutObject() &&
             !GetLayoutObject()->IsLayoutEmbeddedContent()) {
    if (!options->preventScroll()) {
      auto params = ScrollAlignment::CreateScrollIntoViewParams();

      // It's common to have menus and list controls that have items slightly
      // overflowing horizontally but the control isn't horizontally
      // scrollable. Navigating through such a list should make sure items are
      // vertically fully visible but avoid horizontal changes. This mostly
      // matches behavior in WebKit and Gecko (though, the latter has the
      // same behavior vertically) and there's some UA-defined wiggle room in
      // the spec for the scrollIntoViewOptions from focus:
      // https://html.spec.whatwg.org/#dom-focus.
      params->align_x->rect_partial =
          mojom::blink::ScrollAlignment::Behavior::kNoScroll;

      scroll_into_view_util::ScrollRectToVisible(*GetLayoutObject(),
                                                 BoundingBoxForScrollIntoView(),
                                                 std::move(params));
    }
  }
}

void Element::blur() {
  CancelSelectionAfterLayout();
  if (AdjustedFocusedElementInTreeScope() == this) {
    Document& doc = GetDocument();
    if (doc.GetPage()) {
      doc.GetPage()->GetFocusController().SetFocusedElement(nullptr,
                                                            doc.GetFrame());
      if (doc.GetFrame()) {
        doc.GetPage()->GetChromeClient().ClearKeyboardTriggeredTooltip(
            *doc.GetFrame());
      }
    } else {
      doc.ClearFocusedElement();
    }
  }
}

bool Element::SupportsFocus() const {
  // FIXME: supportsFocus() can be called when layout is not up to date.
  // Logic that deals with the layoutObject should be moved to
  // layoutObjectIsFocusable().
  // But supportsFocus must return true when the element is editable, or else
  // it won't be focusable. Furthermore, supportsFocus cannot just return true
  // always or else tabIndex() will change for all HTML elements.
  if (DelegatesFocus())
    return false;
  return HasElementFlag(ElementFlags::kTabIndexWasSetExplicitly) ||
         IsRootEditableElementWithCounting(*this) ||
         SupportsSpatialNavigationFocus();
}

bool Element::SupportsSpatialNavigationFocus() const {
  // This function checks whether the element satisfies the extended criteria
  // for the element to be focusable, introduced by spatial navigation feature,
  // i.e. checks if click or keyboard event handler is specified.
  // This is the way to make it possible to navigate to (focus) elements
  // which web designer meant for being active (made them respond to click
  // events).
  if (!IsSpatialNavigationEnabled(GetDocument().GetFrame()))
    return false;

  if (!GetLayoutObject())
    return false;

  if (HasJSBasedEventListeners(event_type_names::kClick) ||
      HasJSBasedEventListeners(event_type_names::kKeydown) ||
      HasJSBasedEventListeners(event_type_names::kKeypress) ||
      HasJSBasedEventListeners(event_type_names::kKeyup) ||
      HasJSBasedEventListeners(event_type_names::kMouseover) ||
      HasJSBasedEventListeners(event_type_names::kMouseenter))
    return true;

  // Some web apps use click-handlers to react on clicks within rects that are
  // styled with {cursor: pointer}. Such rects *look* clickable so they probably
  // are. Here we make Hand-trees' tip, the first (biggest) node with {cursor:
  // pointer}, navigable because users shouldn't need to navigate through every
  // sub element that inherit this CSS.
  if (GetComputedStyle()->Cursor() == ECursor::kPointer &&
      (!ParentComputedStyle() ||
       (ParentComputedStyle()->Cursor() != ECursor::kPointer))) {
    return true;
  }

  if (!IsSVGElement())
    return false;
  return (HasEventListeners(event_type_names::kFocus) ||
          HasEventListeners(event_type_names::kBlur) ||
          HasEventListeners(event_type_names::kFocusin) ||
          HasEventListeners(event_type_names::kFocusout));
}

bool Element::IsBaseElementFocusable() const {
  return Element::IsMouseFocusable() || Element::IsKeyboardFocusable();
}

bool Element::IsFocusable() const {
  return IsMouseFocusable() || IsKeyboardFocusable();
}

bool Element::IsFocusableStyleAfterUpdate() const {
  // In order to check focusable style, we use the existence of LayoutObjects
  // as a proxy for determining whether the element would have a display mode
  // that restricts visibility (such as display: none). However, with
  // display-locking, it is possible that we deferred such LayoutObject
  // creation. We need to ensure to update style and layout tree to have
  // up-to-date information.
  //
  // Note also that there may be situations where focus / keyboard navigation
  // causes us to have dirty style, so we update StyleAndLayoutTreeForNode here.
  // If the style and layout tree are clean, then this should be a quick
  // operation. See crbug.com/1079385 for details.
  //
  // Note that this isn't a part of `IsFocusableStyle()` because there are
  // callers of that function which cannot do a layout tree update (e.g.
  // accessibility).
  //
  // Also note that if this node is ignored due to a display lock for focus
  // activation reason, we simply return false to avoid updating style & layout
  // tree for this node.
  if (DisplayLockUtilities::ShouldIgnoreNodeDueToDisplayLock(
          *this, DisplayLockActivationReason::kUserFocus)) {
    return false;
  }
  GetDocument().UpdateStyleAndLayoutTreeForNode(this);
  return IsFocusableStyle();
}

bool Element::IsKeyboardFocusable() const {
  return isConnected() && IsFocusableStyleAfterUpdate() &&
         ((SupportsFocus() &&
           GetIntegralAttribute(html_names::kTabindexAttr, 0) >= 0) ||
          (RuntimeEnabledFeatures::KeyboardFocusableScrollersEnabled() &&
           IsScrollableNode(this)));
}

bool Element::IsMouseFocusable() const {
  return isConnected() && IsFocusableStyleAfterUpdate() && SupportsFocus();
}

bool Element::IsAutofocusable() const {
  // https://html.spec.whatwg.org/C/#global-attributes
  // https://svgwg.org/svg2-draft/struct.html#autofocusattribute
  return (IsHTMLElement() || IsSVGElement()) &&
         FastHasAttribute(html_names::kAutofocusAttr);
}

bool Element::ActivateDisplayLockIfNeeded(DisplayLockActivationReason reason) {
  auto& state = GetDocument().GetDisplayLockDocumentState();
  state.UnlockShapingDeferredElements(*this);
  if (!state.HasActivatableLocks())
    return false;

  HeapVector<Member<Element>> activatable_targets;
  for (Node& ancestor : FlatTreeTraversal::InclusiveAncestorsOf(*this)) {
    auto* ancestor_element = DynamicTo<Element>(ancestor);
    if (!ancestor_element)
      continue;
    if (auto* context = ancestor_element->GetDisplayLockContext()) {
      // If any of the ancestors is not activatable for the given reason, we
      // can't activate.
      if (context->IsLocked() && !context->IsActivatable(reason))
        return false;
      activatable_targets.push_back(ancestor_element);
    }
  }

  bool activated = false;
  for (const auto& target : activatable_targets) {
    if (auto* context = target->GetDisplayLockContext()) {
      if (context->ShouldCommitForActivation(reason)) {
        activated = true;
        context->CommitForActivation(reason);
      }
    }
  }
  return activated;
}

bool Element::StyleShouldForceLegacyLayoutInternal() const {
  return GetElementRareData()->StyleShouldForceLegacyLayout();
}

void Element::SetStyleShouldForceLegacyLayoutInternal(bool force) {
  EnsureElementRareData().SetStyleShouldForceLegacyLayout(force);
}

bool Element::ShouldForceLegacyLayoutForChildInternal() const {
  return GetElementRareData()->ShouldForceLegacyLayoutForChild();
}

void Element::SetShouldForceLegacyLayoutForChildInternal(bool force) {
  EnsureElementRareData().SetShouldForceLegacyLayoutForChild(force);
}

bool Element::HasUndoStack() const {
  return HasRareData() && GetElementRareData()->HasUndoStack();
}

void Element::SetHasUndoStack(bool value) {
  EnsureElementRareData().SetHasUndoStack(value);
}

void Element::SetScrollbarPseudoElementStylesDependOnFontMetrics(bool value) {
  EnsureElementRareData().SetScrollbarPseudoElementStylesDependOnFontMetrics(
      value);
}

bool Element::AffectedByNonSubjectHas() const {
  if (HasRareData())
    return GetElementRareData()->AffectedByNonSubjectHas();
  return false;
}

void Element::SetAffectedByNonSubjectHas() {
  EnsureElementRareData().SetAffectedByNonSubjectHas();
}

bool Element::AncestorsOrAncestorSiblingsAffectedByHas() const {
  if (HasRareData())
    return GetElementRareData()->AncestorsOrAncestorSiblingsAffectedByHas();
  return false;
}

void Element::SetAncestorsOrAncestorSiblingsAffectedByHas() {
  EnsureElementRareData().SetAncestorsOrAncestorSiblingsAffectedByHas();
}

unsigned Element::GetSiblingsAffectedByHasFlags() const {
  if (HasRareData())
    return GetElementRareData()->GetSiblingsAffectedByHasFlags();
  return false;
}

bool Element::HasSiblingsAffectedByHasFlags(unsigned flags) const {
  if (HasRareData())
    return GetElementRareData()->HasSiblingsAffectedByHasFlags(flags);
  return false;
}

void Element::SetSiblingsAffectedByHasFlags(unsigned flags) {
  EnsureElementRareData().SetSiblingsAffectedByHasFlags(flags);
}

bool Element::AffectedByPseudoInHas() const {
  return HasRareData() ? GetElementRareData()->AffectedByPseudoInHas() : false;
}

void Element::SetAffectedByPseudoInHas() {
  EnsureElementRareData().SetAffectedByPseudoInHas();
}

bool Element::AncestorsOrSiblingsAffectedByHoverInHas() const {
  return HasRareData()
             ? GetElementRareData()->AncestorsOrSiblingsAffectedByHoverInHas()
             : false;
}

void Element::SetAncestorsOrSiblingsAffectedByHoverInHas() {
  EnsureElementRareData().SetAncestorsOrSiblingsAffectedByHoverInHas();
}

bool Element::AncestorsOrSiblingsAffectedByActiveInHas() const {
  return HasRareData()
             ? GetElementRareData()->AncestorsOrSiblingsAffectedByActiveInHas()
             : false;
}

void Element::SetAncestorsOrSiblingsAffectedByActiveInHas() {
  EnsureElementRareData().SetAncestorsOrSiblingsAffectedByActiveInHas();
}

bool Element::AncestorsOrSiblingsAffectedByFocusInHas() const {
  return HasRareData()
             ? GetElementRareData()->AncestorsOrSiblingsAffectedByFocusInHas()
             : false;
}

void Element::SetAncestorsOrSiblingsAffectedByFocusInHas() {
  EnsureElementRareData().SetAncestorsOrSiblingsAffectedByFocusInHas();
}

bool Element::AncestorsOrSiblingsAffectedByFocusVisibleInHas() const {
  return HasRareData() ? GetElementRareData()
                             ->AncestorsOrSiblingsAffectedByFocusVisibleInHas()
                       : false;
}

void Element::SetAncestorsOrSiblingsAffectedByFocusVisibleInHas() {
  EnsureElementRareData().SetAncestorsOrSiblingsAffectedByFocusVisibleInHas();
}

bool Element::AffectedByLogicalCombinationsInHas() const {
  return HasRareData()
             ? GetElementRareData()->AffectedByLogicalCombinationsInHas()
             : false;
}

void Element::SetAffectedByLogicalCombinationsInHas() {
  EnsureElementRareData().SetAffectedByLogicalCombinationsInHas();
}

bool Element::UpdateForceLegacyLayout(const ComputedStyle& new_style,
                                      const ComputedStyle* old_style) {
  // ::first-letter may cause structure discrepancies between DOM and layout
  //  tree (in layout the layout object will be wrapped around the actual text
  //  layout object, which may be deep down in the tree somewhere, while in DOM,
  //  the pseudo element will be a direct child of the node that matched the
  //  ::first-letter selector). Because of that, it's going to be tricky to
  //  determine whether we need to force legacy layout or not. Luckily, the
  //  ::first-letter pseudo element cannot introduce the need for legacy layout
  //  on its own, so just bail. We'll do whatever the parent layout object does.
  if (IsFirstLetterPseudoElement())
    return false;
  bool needs_reattach = false;
  bool old_force = old_style && ShouldForceLegacyLayout();
  SetStyleShouldForceLegacyLayout(
      CalculateStyleShouldForceLegacyLayout(*this, new_style));
  if (ShouldForceLegacyLayout()) {
    if (!old_force) {
      if (const LayoutObject* layout_object = GetLayoutObject()) {
        // Forced legacy layout is inherited down the layout tree, so even if we
        // just decided here on the DOM side that we need forced legacy layout,
        // check with the LayoutObject whether this is news and that it really
        // needs to be reattached.
        if (!layout_object->ForceLegacyLayout())
          needs_reattach = true;
      }
    }
    // If we're inside an NG fragmentation context, we need the entire
    // fragmentation context to fall back to legacy layout. Note that once this
    // has happened, the fragmentation context will be locked to legacy layout,
    // even if all the reasons for requiring it in the first place disappear
    // (e.g. if the only reason was a table, and that table is removed, we'll
    // still be using legacy layout).
    if (new_style.InsideFragmentationContextWithNondeterministicEngine()) {
      if (ForceLegacyLayoutInFragmentationContext(new_style))
        needs_reattach = true;
    } else {
      // Note that even if we also previously forced legacy layout, we may need
      // to introduce forced legacy layout in the ancestry, e.g. if this element
      // no longer establishes a new formatting context.
      if (ForceLegacyLayoutInFormattingContext(new_style))
        needs_reattach = true;
    }
  } else if (old_force) {
    // TODO(mstensho): If we have ancestors that got legacy layout just because
    // of this child, we should clean it up, and switch the subtree back to NG,
    // rather than being stuck with legacy forever. Also make sure to reattach
    // the Document, if we want to switch from LayoutView to LayoutNGView (may
    // happen after printing).
    needs_reattach = true;
  }
  return needs_reattach;
}

bool Element::ForceLegacyLayoutInFormattingContext(
    const ComputedStyle& new_style) {
  bool found_fc = DefinitelyNewFormattingContext(*this, new_style);
  bool needs_reattach = false;

  Element* container_recalc_root =
      GetDocument().GetStyleEngine().GetContainerForContainerStyleRecalc();

  // TODO(mstensho): Missing call to SetNeedsReattachLayoutTree() on Document
  // here. We may have to re-attach it if we want to change from LayoutNGView to
  // LayoutView.
  for (Element* ancestor = this; !found_fc;) {
    ancestor =
        DynamicTo<Element>(LayoutTreeBuilderTraversal::Parent(*ancestor));
    if (!ancestor || ancestor->ShouldForceLegacyLayoutForChild())
      break;
    const ComputedStyle* style = ancestor->GetComputedStyle();

    if (style->Display() == EDisplay::kNone)
      break;

    found_fc = DefinitelyNewFormattingContext(*ancestor, *style);
    ancestor->SetShouldForceLegacyLayoutForChild(true);
    ancestor->SetNeedsReattachLayoutTree();
    needs_reattach = true;

    if (container_recalc_root == ancestor) {
      DCHECK(found_fc)
          << "A size query container is always a formatting context";
      GetDocument()
          .GetStyleEngine()
          .ReportUseOfLegacyLayoutWithContainerQueries();
      break;
    }
  }
  return needs_reattach;
}

bool Element::ForceLegacyLayoutInFragmentationContext(
    const ComputedStyle& new_style) {
  DCHECK(new_style.InsideFragmentationContextWithNondeterministicEngine());

  // This element cannot be laid out natively by LayoutNG. We now need to switch
  // all enclosing block fragmentation contexts over to using legacy
  // layout. Find the element that establishes the fragmentation context, and
  // switch it over to legacy layout. Note that we walk the parent chain here,
  // and not the containing block chain. This means that we may get false
  // positives; e.g. if there's an absolutely positioned table, whose containing
  // block of the table is on the outside of the fragmentation context, we're
  // still going to fall back to legacy.

  Element* container_recalc_root =
      GetDocument().GetStyleEngine().GetContainerForContainerStyleRecalc();

  Element* parent;
  Element* legacy_root;
  for (legacy_root = this; legacy_root != container_recalc_root;
       legacy_root = parent) {
    parent =
        DynamicTo<Element>(LayoutTreeBuilderTraversal::Parent(*legacy_root));

    // Note that even if we also previously forced legacy layout, we may need to
    // introduce forced legacy layout in the ancestry, e.g. if legacy_root no
    // longer establishes a new formatting context. It is therefore important
    // that we first check if we reached the root, and potentially continue the
    // journey in search of a formatting context root.
    if (!parent ||
        !parent->GetComputedStyle()
             ->InsideFragmentationContextWithNondeterministicEngine())
      break;
  }

  // Only mark for reattachment if needed. Unnecessary reattachments may lead to
  // over-invalidation and also printing problems; if we re-attach a frameset
  // when printing, the frames will show up blank.
  bool needs_reattach = false;
  if (!legacy_root->ShouldForceLegacyLayoutForChild()) {
    legacy_root->SetShouldForceLegacyLayoutForChild(true);
    legacy_root->SetNeedsReattachLayoutTree();
    needs_reattach = true;
  }

  // When we have found the outermost fragmentation context candidate, we need
  // to make sure to mark for legacy all the way up to the element that we can
  // tell for sure will establish a new formatting context.
  //
  // E.g. <span style="columns:1;"> will trigger legacy layout fallback (false
  // positive). When this happens, we need to walk all the way up to the
  // ancestor that establishes a formatting context, and this is the subtree
  // that will force legacy layout.
  if (legacy_root->ForceLegacyLayoutInFormattingContext(
          *legacy_root->GetComputedStyle()))
    needs_reattach = true;

  if (legacy_root == container_recalc_root) {
    GetDocument()
        .GetStyleEngine()
        .ReportUseOfLegacyLayoutWithContainerQueries();
  }

  return needs_reattach;
}

bool Element::IsFocusedElementInDocument() const {
  return this == GetDocument().FocusedElement();
}

Element* Element::AdjustedFocusedElementInTreeScope() const {
  return IsInTreeScope() ? ContainingTreeScope().AdjustedFocusedElement()
                         : nullptr;
}

void Element::DispatchFocusEvent(Element* old_focused_element,
                                 mojom::blink::FocusType type,
                                 InputDeviceCapabilities* source_capabilities) {
  DispatchEvent(*FocusEvent::Create(
      event_type_names::kFocus, Event::Bubbles::kNo, GetDocument().domWindow(),
      0, old_focused_element, source_capabilities));
}

void Element::DispatchBlurEvent(Element* new_focused_element,
                                mojom::blink::FocusType type,
                                InputDeviceCapabilities* source_capabilities) {
  DispatchEvent(*FocusEvent::Create(
      event_type_names::kBlur, Event::Bubbles::kNo, GetDocument().domWindow(),
      0, new_focused_element, source_capabilities));
}

void Element::DispatchFocusInEvent(
    const AtomicString& event_type,
    Element* old_focused_element,
    mojom::blink::FocusType,
    InputDeviceCapabilities* source_capabilities) {
#if DCHECK_IS_ON()
  DCHECK(!EventDispatchForbiddenScope::IsEventDispatchForbidden());
#endif
  DCHECK(event_type == event_type_names::kFocusin ||
         event_type == event_type_names::kDOMFocusIn);
  DispatchScopedEvent(*FocusEvent::Create(
      event_type, Event::Bubbles::kYes, GetDocument().domWindow(), 0,
      old_focused_element, source_capabilities));
}

void Element::DispatchFocusOutEvent(
    const AtomicString& event_type,
    Element* new_focused_element,
    InputDeviceCapabilities* source_capabilities) {
#if DCHECK_IS_ON()
  DCHECK(!EventDispatchForbiddenScope::IsEventDispatchForbidden());
#endif
  DCHECK(event_type == event_type_names::kFocusout ||
         event_type == event_type_names::kDOMFocusOut);
  DispatchScopedEvent(*FocusEvent::Create(
      event_type, Event::Bubbles::kYes, GetDocument().domWindow(), 0,
      new_focused_element, source_capabilities));
}

String Element::innerHTML() const {
  return CreateMarkup(this, kChildrenOnly);
}

String Element::outerHTML() const {
  return CreateMarkup(this);
}

void Element::SetInnerHTMLInternal(const String& html,
                                   bool include_shadow_roots,
                                   ExceptionState& exception_state) {
  if (html.IsEmpty() && !HasNonInBodyInsertionMode()) {
    setTextContent(html);
  } else {
    if (DocumentFragment* fragment = CreateFragmentForInnerOuterHTML(
            html, this, kAllowScriptingContent, "innerHTML",
            include_shadow_roots, exception_state)) {
      ContainerNode* container = this;
      if (auto* template_element = DynamicTo<HTMLTemplateElement>(*this)) {
        // Allow replacing innerHTML on declarative shadow templates, prior to
        // their closing tag being parsed.
        container = template_element->IsDeclarativeShadowRoot()
                        ? template_element->DeclarativeShadowContent()
                        : template_element->content();
      }
      ReplaceChildrenWithFragment(container, fragment, exception_state);
    }
  }
}

void Element::setInnerHTML(const String& html,
                           ExceptionState& exception_state) {
  probe::BreakableLocation(GetExecutionContext(), "Element.setInnerHTML");
  SetInnerHTMLInternal(html, /*include_shadow_roots=*/false, exception_state);
}

void Element::setInnerHTMLWithDeclarativeShadowDOMForTesting(
    const String& html) {
  SetInnerHTMLInternal(html, /*include_shadow_roots=*/true,
                       ASSERT_NO_EXCEPTION);
}

String Element::getInnerHTML(const GetInnerHTMLOptions* options) const {
  ClosedRootsSet include_closed_roots;
  if (options->hasClosedRoots()) {
    for (auto& shadow_root : options->closedRoots()) {
      include_closed_roots.insert(shadow_root);
    }
  }
  return CreateMarkup(
      this, kChildrenOnly, kDoNotResolveURLs,
      options->includeShadowRoots() ? kIncludeShadowRoots : kNoShadowRoots,
      include_closed_roots);
}

void Element::setOuterHTML(const String& html,
                           ExceptionState& exception_state) {
  Node* p = parentNode();
  if (!p) {
    exception_state.ThrowDOMException(
        DOMExceptionCode::kNoModificationAllowedError,
        "This element has no parent node.");
    return;
  }

  auto* parent = DynamicTo<Element>(p);
  if (!parent) {
    exception_state.ThrowDOMException(
        DOMExceptionCode::kNoModificationAllowedError,
        "This element's parent is of type '" + p->nodeName() +
            "', which is not an element node.");
    return;
  }

  Node* prev = previousSibling();
  Node* next = nextSibling();

  DocumentFragment* fragment = CreateFragmentForInnerOuterHTML(
      html, parent, kAllowScriptingContent, "outerHTML",
      /*include_shadow_roots=*/false, exception_state);
  if (exception_state.HadException())
    return;

  parent->ReplaceChild(fragment, this, exception_state);
  if (exception_state.HadException())
    return;

  Node* node = next ? next->previousSibling() : nullptr;
  if (auto* text = DynamicTo<Text>(node)) {
    MergeWithNextTextNode(text, exception_state);
    if (exception_state.HadException())
      return;
  }

  if (auto* prev_text = DynamicTo<Text>(prev)) {
    MergeWithNextTextNode(prev_text, exception_state);
    if (exception_state.HadException())
      return;
  }
}

// Step 4 of http://domparsing.spec.whatwg.org/#insertadjacenthtml()
Node* Element::InsertAdjacent(const String& where,
                              Node* new_child,
                              ExceptionState& exception_state) {
  if (EqualIgnoringASCIICase(where, "beforeBegin")) {
    if (ContainerNode* parent = parentNode()) {
      parent->InsertBefore(new_child, this, exception_state);
      if (!exception_state.HadException())
        return new_child;
    }
    return nullptr;
  }

  if (EqualIgnoringASCIICase(where, "afterBegin")) {
    InsertBefore(new_child, firstChild(), exception_state);
    return exception_state.HadException() ? nullptr : new_child;
  }

  if (EqualIgnoringASCIICase(where, "beforeEnd")) {
    AppendChild(new_child, exception_state);
    return exception_state.HadException() ? nullptr : new_child;
  }

  if (EqualIgnoringASCIICase(where, "afterEnd")) {
    if (ContainerNode* parent = parentNode()) {
      parent->InsertBefore(new_child, nextSibling(), exception_state);
      if (!exception_state.HadException())
        return new_child;
    }
    return nullptr;
  }

  exception_state.ThrowDOMException(
      DOMExceptionCode::kSyntaxError,
      "The value provided ('" + where +
          "') is not one of 'beforeBegin', 'afterBegin', "
          "'beforeEnd', or 'afterEnd'.");
  return nullptr;
}

void Element::HideNonce() {
  const AtomicString& nonce_value = FastGetAttribute(html_names::kNonceAttr);
  if (nonce_value.IsEmpty())
    return;
  if (!InActiveDocument())
    return;
  if (GetExecutionContext()
          ->GetContentSecurityPolicy()
          ->HasHeaderDeliveredPolicy()) {
    setAttribute(html_names::kNonceAttr, g_empty_atom);
  }
}

void Element::AdjustForceLegacyLayout(const ComputedStyle* style,
                                      bool* should_force_legacy_layout) {
  // If an element requires forced legacy layout, all descendants need it too
  // (but see below):
  if (ShouldForceLegacyLayout())
    *should_force_legacy_layout = true;
}

ElementIntersectionObserverData* Element::IntersectionObserverData() const {
  if (HasRareData())
    return GetElementRareData()->IntersectionObserverData();
  return nullptr;
}

ElementIntersectionObserverData& Element::EnsureIntersectionObserverData() {
  return EnsureElementRareData().EnsureIntersectionObserverData();
}

HeapHashMap<Member<ResizeObserver>, Member<ResizeObservation>>*
Element::ResizeObserverData() const {
  if (HasRareData())
    return GetElementRareData()->ResizeObserverData();
  return nullptr;
}

HeapHashMap<Member<ResizeObserver>, Member<ResizeObservation>>&
Element::EnsureResizeObserverData() {
  return EnsureElementRareData().EnsureResizeObserverData();
}

DisplayLockContext* Element::GetDisplayLockContextFromRareData() const {
  DCHECK(HasDisplayLockContext());
  DCHECK(HasRareData());
  return GetElementRareData()->GetDisplayLockContext();
}

DisplayLockContext& Element::EnsureDisplayLockContext() {
  SetHasDisplayLockContext();
  return *EnsureElementRareData().EnsureDisplayLockContext(this);
}

ContainerQueryData* Element::GetContainerQueryData() const {
  if (!HasRareData())
    return nullptr;
  return GetElementRareData()->GetContainerQueryData();
}

ContainerQueryEvaluator* Element::GetContainerQueryEvaluator() const {
  if (const auto* cq_data = GetContainerQueryData())
    return cq_data->GetContainerQueryEvaluator();
  return nullptr;
}

void Element::SetContainerQueryEvaluator(ContainerQueryEvaluator* evaluator) {
  EnsureElementRareData().SetContainerQueryEvaluator(evaluator);
}

bool Element::SkippedContainerStyleRecalc() const {
  if (!RuntimeEnabledFeatures::CSSContainerSkipStyleRecalcEnabled())
    return false;
  if (const auto* cq_data = GetContainerQueryData())
    return cq_data->SkippedStyleRecalc();
  return false;
}

// Step 1 of http://domparsing.spec.whatwg.org/#insertadjacenthtml()
static Node* ContextNodeForInsertion(const String& where,
                                     Element* element,
                                     ExceptionState& exception_state) {
  if (EqualIgnoringASCIICase(where, "beforeBegin") ||
      EqualIgnoringASCIICase(where, "afterEnd")) {
    Node* parent = element->parentNode();
    if (!parent || IsA<Document>(parent)) {
      exception_state.ThrowDOMException(
          DOMExceptionCode::kNoModificationAllowedError,
          "The element has no parent.");
      return nullptr;
    }
    return parent;
  }
  if (EqualIgnoringASCIICase(where, "afterBegin") ||
      EqualIgnoringASCIICase(where, "beforeEnd"))
    return element;
  exception_state.ThrowDOMException(
      DOMExceptionCode::kSyntaxError,
      "The value provided ('" + where +
          "') is not one of 'beforeBegin', 'afterBegin', "
          "'beforeEnd', or 'afterEnd'.");
  return nullptr;
}

Element* Element::insertAdjacentElement(const String& where,
                                        Element* new_child,
                                        ExceptionState& exception_state) {
  Node* return_value = InsertAdjacent(where, new_child, exception_state);
  return To<Element>(return_value);
}

void Element::insertAdjacentText(const String& where,
                                 const String& text,
                                 ExceptionState& exception_state) {
  InsertAdjacent(where, GetDocument().createTextNode(text), exception_state);
}

void Element::insertAdjacentHTML(const String& where,
                                 const String& markup,
                                 ExceptionState& exception_state) {
  Node* context_node = ContextNodeForInsertion(where, this, exception_state);
  if (!context_node)
    return;

  // Step 2 of http://domparsing.spec.whatwg.org/#insertadjacenthtml()
  Element* context_element;
  if (!IsA<Element>(context_node) ||
      (IsA<HTMLDocument>(context_node->GetDocument()) &&
       IsA<HTMLHtmlElement>(context_node))) {
    context_element =
        MakeGarbageCollected<HTMLBodyElement>(context_node->GetDocument());
  } else {
    context_element = To<Element>(context_node);
  }

  // Step 3 of http://domparsing.spec.whatwg.org/#insertadjacenthtml()
  DocumentFragment* fragment = CreateFragmentForInnerOuterHTML(
      markup, context_element, kAllowScriptingContent, "insertAdjacentHTML",
      /*include_shadow_roots=*/false, exception_state);
  if (!fragment)
    return;
  InsertAdjacent(where, fragment, exception_state);
}

void Element::setPointerCapture(PointerId pointer_id,
                                ExceptionState& exception_state) {
  if (GetDocument().GetFrame()) {
    if (!GetDocument().GetFrame()->GetEventHandler().IsPointerEventActive(
            pointer_id)) {
      exception_state.ThrowDOMException(
          DOMExceptionCode::kNotFoundError,
          "No active pointer with the given id is found.");
    } else if (!isConnected() ||
               (GetDocument().GetPage() && GetDocument()
                                               .GetPage()
                                               ->GetPointerLockController()
                                               .GetElement())) {
      exception_state.ThrowDOMException(DOMExceptionCode::kInvalidStateError,
                                        "InvalidStateError");
    } else {
      GetDocument().GetFrame()->GetEventHandler().SetPointerCapture(
          pointer_id, this, /* explicit_capture */ true);
    }
  }
}

void Element::releasePointerCapture(PointerId pointer_id,
                                    ExceptionState& exception_state) {
  if (GetDocument().GetFrame()) {
    if (!GetDocument().GetFrame()->GetEventHandler().IsPointerEventActive(
            pointer_id)) {
      exception_state.ThrowDOMException(
          DOMExceptionCode::kNotFoundError,
          "No active pointer with the given id is found.");
    } else {
      GetDocument().GetFrame()->GetEventHandler().ReleasePointerCapture(
          pointer_id, this);
    }
  }
}

bool Element::hasPointerCapture(PointerId pointer_id) const {
  return GetDocument().GetFrame() &&
         GetDocument().GetFrame()->GetEventHandler().HasPointerCapture(
             pointer_id, this);
}

String Element::outerText() {
  // Getting outerText is the same as getting innerText, only
  // setting is different. You would think this should get the plain
  // text for the outer range, but this is wrong, <br> for instance
  // would return different values for inner and outer text by such
  // a rule, but it doesn't in WinIE, and we want to match that.
  return innerText();
}

String Element::TextFromChildren() {
  Text* first_text_node = nullptr;
  bool found_multiple_text_nodes = false;
  unsigned total_length = 0;

  for (Node* child = firstChild(); child; child = child->nextSibling()) {
    auto* child_text_node = DynamicTo<Text>(child);
    if (!child_text_node)
      continue;
    if (!first_text_node)
      first_text_node = child_text_node;
    else
      found_multiple_text_nodes = true;
    unsigned length = child_text_node->data().length();
    if (length > std::numeric_limits<unsigned>::max() - total_length)
      return g_empty_string;
    total_length += length;
  }

  if (!first_text_node)
    return g_empty_string;

  if (first_text_node && !found_multiple_text_nodes) {
    first_text_node->MakeParkable();
    return first_text_node->data();
  }

  StringBuilder content;
  content.ReserveCapacity(total_length);
  for (Node* child = first_text_node; child; child = child->nextSibling()) {
    auto* child_text_node = DynamicTo<Text>(child);
    if (!child_text_node)
      continue;
    content.Append(child_text_node->data());
  }

  DCHECK_EQ(content.length(), total_length);
  return content.ReleaseString();
}

const AtomicString& Element::ShadowPseudoId() const {
  if (ShadowRoot* root = ContainingShadowRoot()) {
    if (root->IsUserAgent())
      return FastGetAttribute(html_names::kPseudoAttr);
  }
  return g_null_atom;
}

void Element::SetShadowPseudoId(const AtomicString& id) {
  DCHECK(CSSSelectorParser::ParsePseudoType(id, false) ==
             CSSSelector::kPseudoWebKitCustomElement ||
         CSSSelectorParser::ParsePseudoType(id, false) ==
             CSSSelector::kPseudoBlinkInternalElement);
  setAttribute(html_names::kPseudoAttr, id);
}

bool Element::IsInDescendantTreeOf(const Element* shadow_host) const {
  DCHECK(shadow_host);
  DCHECK(IsShadowHost(shadow_host));

  for (const Element* ancestor_shadow_host = OwnerShadowHost();
       ancestor_shadow_host;
       ancestor_shadow_host = ancestor_shadow_host->OwnerShadowHost()) {
    if (ancestor_shadow_host == shadow_host)
      return true;
  }
  return false;
}

namespace {

bool NeedsEnsureComputedStyle(Element& element) {
  const ComputedStyle* style = element.GetComputedStyle();
  return !style || style->IsEnsuredOutsideFlatTree();
}

HeapVector<Member<Element>> CollectAncestorsToEnsure(Element& element) {
  HeapVector<Member<Element>> ancestors;

  Element* ancestor = &element;
  while ((ancestor = DynamicTo<Element>(
              LayoutTreeBuilderTraversal::Parent(*ancestor)))) {
    if (!NeedsEnsureComputedStyle(*ancestor))
      break;
    ancestors.push_back(ancestor);
  }

  return ancestors;
}

}  // namespace

const ComputedStyle* Element::EnsureComputedStyle(
    PseudoId pseudo_element_specifier,
    const AtomicString& pseudo_argument) {
  // Style computation should not be triggered when in a NoAllocationScope
  // because there is always a possibility that it could allocate something on
  // the V8 heap.
  DCHECK(ThreadState::Current()->IsAllocationAllowed());

  StyleEngine::InEnsureComputedStyleScope ensure_scope(
      GetDocument().GetStyleEngine());

  if (PseudoElement* element =
          GetNestedPseudoElement(pseudo_element_specifier, pseudo_argument)) {
    return element->EnsureComputedStyle();
  }

  if (!InActiveDocument())
    return nullptr;

  // EnsureComputedStyle is expected to be called to forcibly compute style for
  // elements in display:none subtrees on otherwise style-clean documents. If
  // you hit this DCHECK, consider if you really need ComputedStyle for
  // display:none elements. If not, use GetComputedStyle() instead.
  // Regardless, you need to UpdateStyleAndLayoutTree() before calling
  // EnsureComputedStyle. In some cases you might be fine using GetComputedStyle
  // without updating the style, but in most cases you want a clean tree for
  // that as well.
  //
  // Adjacent styling bits may be set and affect NeedsLayoutTreeUpdateForNode as
  // part of EnsureComputedStyle in an ancestor chain.
  // (see CSSComputedStyleDeclarationTest::NeedsAdjacentStyleRecalc). It is OK
  // that it happens, but we need to ignore the effect on
  // NeedsLayoutTreeUpdateForNodeIncludingDisplayLocked here.
  DCHECK(!GetDocument().NeedsLayoutTreeUpdateForNodeIncludingDisplayLocked(
      *this, true /* ignore_adjacent_style */));

  // Retrieve a list of (non-inclusive) ancestors that we need to ensure the
  // ComputedStyle for *before* we can ensure the ComputedStyle for this
  // element. Note that the list of ancestors can be empty if |this| is the
  // root of the display:none subtree.
  //
  // The front() element is the LayoutTreeBuilderTraversal::Parent of |this|,
  // and the back() element is the "top-most" ancestor in the chain.
  HeapVector<Member<Element>> ancestors = CollectAncestorsToEnsure(*this);

  Element* top = ancestors.IsEmpty() ? this : ancestors.back().Get();
  auto style_recalc_context = StyleRecalcContext::FromAncestors(*top);

  while (!ancestors.IsEmpty()) {
    Element* ancestor = ancestors.back();
    ancestors.pop_back();
    const ComputedStyle* style =
        ancestor->EnsureOwnComputedStyle(style_recalc_context, kPseudoIdNone);
    if (style->IsContainerForSizeContainerQueries())
      style_recalc_context.container = ancestor;
  }

  return EnsureOwnComputedStyle(style_recalc_context, pseudo_element_specifier,
                                pseudo_argument);
}

const ComputedStyle* Element::EnsureOwnComputedStyle(
    const StyleRecalcContext& style_recalc_context,
    PseudoId pseudo_element_specifier,
    const AtomicString& pseudo_argument) {
  // FIXME: Find and use the layoutObject from the pseudo element instead of the
  // actual element so that the 'length' properties, which are only known by the
  // layoutObject because it did the layout, will be correct and so that the
  // values returned for the ":selection" pseudo-element will be correct.
  const ComputedStyle* element_style = GetComputedStyle();
  if (NeedsEnsureComputedStyle(*this)) {
    if (element_style && NeedsStyleRecalc()) {
      // RecalcStyle() will not traverse into connected elements outside the
      // flat tree and we may have a dirty element or ancestors if this
      // element is not in the flat tree. If we don't need a style recalc,
      // we can just re-use the ComputedStyle from the last
      // getComputedStyle(). Otherwise, we need to clear the ensured styles
      // for the uppermost dirty ancestor and all of its descendants. If
      // this element was not the uppermost dirty element, we would not end
      // up here because a dirty ancestor would have cleared the
      // ComputedStyle via EnsureComputedStyle and element_style would
      // have been null.
      GetDocument().GetStyleEngine().ClearEnsuredDescendantStyles(*this);
      element_style = nullptr;
    }
    if (!element_style) {
      scoped_refptr<ComputedStyle> new_style = nullptr;
      // TODO(crbug.com/953707): Avoid setting inline style during
      // HTMLImageElement::CustomStyleForLayoutObject.
      if (HasCustomStyleCallbacks() && !IsA<HTMLImageElement>(*this))
        new_style = CustomStyleForLayoutObject(style_recalc_context);
      else
        new_style = OriginalStyleForLayoutObject(style_recalc_context);
      element_style = new_style.get();
      new_style->SetIsEnsuredInDisplayNone();
      SetComputedStyle(std::move(new_style));
    }
  }

  if (!pseudo_element_specifier)
    return element_style;

  if (const ComputedStyle* pseudo_element_style =
          element_style->GetCachedPseudoElementStyle(pseudo_element_specifier,
                                                     pseudo_argument))
    return pseudo_element_style;

  const ComputedStyle* layout_parent_style = element_style;
  if (HasDisplayContentsStyle()) {
    LayoutObject* parent_layout_object =
        LayoutTreeBuilderTraversal::ParentLayoutObject(*this);
    if (parent_layout_object)
      layout_parent_style = parent_layout_object->Style();
  }

  StyleRequest style_request;
  style_request.pseudo_id = pseudo_element_specifier;
  style_request.type = StyleRequest::kForComputedStyle;
  if (UsesHighlightPseudoInheritance(pseudo_element_specifier)) {
    const ComputedStyle* highlight_element_style = nullptr;
    ContainerNode* parent = LayoutTreeBuilderTraversal::Parent(*this);
    if (parent && parent->GetComputedStyle()->HighlightData()) {
      highlight_element_style =
          parent->GetComputedStyle()->HighlightData()->Style(
              pseudo_element_specifier, pseudo_argument);
    }
    style_request.parent_override = highlight_element_style;
    // All properties that apply to highlight pseudos are treated as inherited,
    // so we don't need to do anything special regarding display contents (see
    // https://drafts.csswg.org/css-pseudo/#highlight-cascade).
    style_request.layout_parent_override = highlight_element_style;
    style_request.originating_element_style = element_style;
  } else {
    style_request.parent_override = element_style;
    style_request.layout_parent_override = layout_parent_style;
  }
  style_request.pseudo_argument = pseudo_argument;

  StyleRecalcContext child_recalc_context = style_recalc_context;
  if (RuntimeEnabledFeatures::CSSContainerQueriesEnabled() &&
      element_style->IsContainerForSizeContainerQueries()) {
    child_recalc_context.container = this;
  }

  scoped_refptr<ComputedStyle> result =
      GetDocument().GetStyleResolver().ResolveStyle(this, child_recalc_context,
                                                    style_request);
  DCHECK(result);
  result->SetIsEnsuredInDisplayNone();
  return element_style->AddCachedPseudoElementStyle(
      std::move(result), pseudo_element_specifier, pseudo_argument);
}

bool Element::HasDisplayContentsStyle() const {
  if (const ComputedStyle* style = GetComputedStyle())
    return style->Display() == EDisplay::kContents;
  return false;
}

bool Element::ShouldStoreComputedStyle(const ComputedStyle& style) const {
  // If we're in a locked subtree and we're a top layer element, it means that
  // we shouldn't be creating a layout object. This path can happen if we're
  // force-updating style on the locked subtree and reach this node. Note that
  // we already detached layout when this element was added to top-layer, so we
  // simply maintain the fact that it doesn't have a layout object/subtree.
  if (IsInTopLayer() &&
      DisplayLockUtilities::LockedAncestorPreventingPaint(*this)) {
    return false;
  }

  if (LayoutObjectIsNeeded(style))
    return true;
  if (auto* svg_element = DynamicTo<SVGElement>(this)) {
    if (!svg_element->HasSVGParent())
      return false;
    if (IsA<SVGStopElement>(*this))
      return true;
  }
  return style.Display() == EDisplay::kContents;
}

AtomicString Element::ComputeInheritedLanguage() const {
  const Node* n = this;
  AtomicString value;
  // The language property is inherited, so we iterate over the parents to find
  // the first language.
  do {
    if (n->IsElementNode()) {
      if (const auto* element_data = To<Element>(n)->GetElementData()) {
        AttributeCollection attributes = element_data->Attributes();
        // Spec: xml:lang takes precedence -- http://www.w3.org/TR/xhtml1/#C_7
        if (const Attribute* attribute =
                attributes.Find(xml_names::kLangAttr)) {
          value = attribute->Value();
        } else {
          attribute = attributes.Find(html_names::kLangAttr);
          if (attribute)
            value = attribute->Value();
        }
      }
    } else if (auto* document = DynamicTo<Document>(n)) {
      // checking the MIME content-language
      value = document->ContentLanguage();
    }

    n = n->ParentOrShadowHostNode();
  } while (n && value.IsNull());

  return value;
}

Locale& Element::GetLocale() const {
  return GetDocument().GetCachedLocale(ComputeInheritedLanguage());
}

void Element::CancelSelectionAfterLayout() {
  if (GetDocument().FocusedElement() == this)
    GetDocument().SetShouldUpdateSelectionAfterLayout(false);
}

void Element::UpdateFirstLetterPseudoElement(StyleUpdatePhase phase) {
  if (CanGeneratePseudoElement(kPseudoIdFirstLetter) ||
      GetPseudoElement(kPseudoIdFirstLetter)) {
    UpdateFirstLetterPseudoElement(
        phase, StyleRecalcContext::FromInclusiveAncestors(*this));
  }
}

void Element::UpdateFirstLetterPseudoElement(
    StyleUpdatePhase phase,
    const StyleRecalcContext& style_recalc_context) {
  // Update the ::first-letter pseudo elements presence and its style. This
  // method may be called from style recalc or layout tree rebuilding/
  // reattachment. In order to know if an element generates a ::first-letter
  // element, we need to know if:
  //
  // * The element generates a block level box to which ::first-letter applies.
  // * The element's layout subtree generates any first letter text.
  // * None of the descendant blocks generate a ::first-letter element.
  //   (This is not correct according to spec as all block containers should be
  //   able to generate ::first-letter elements around the first letter of the
  //   first formatted text, but Blink is only supporting a single
  //   ::first-letter element which is the innermost block generating a
  //   ::first-letter).
  //
  // We do not always do this at style recalc time as that would have required
  // us to collect the information about how the layout tree will look like
  // after the layout tree is attached. So, instead we will wait until we have
  // an up-to-date layout sub-tree for the element we are considering for
  // ::first-letter.
  //
  // The StyleUpdatePhase tells where we are in the process of updating style
  // and layout tree.

  PseudoElement* element = GetPseudoElement(kPseudoIdFirstLetter);
  if (!element) {
    element =
        CreatePseudoElementIfNeeded(kPseudoIdFirstLetter, style_recalc_context);
    // If we are in Element::AttachLayoutTree, don't mess up the ancestor flags
    // for layout tree attachment/rebuilding. We will unconditionally call
    // AttachLayoutTree for the created pseudo element immediately after this
    // call.
    if (element && phase != StyleUpdatePhase::kAttachLayoutTree)
      element->SetNeedsReattachLayoutTree();
    return;
  }

  if (!CanGeneratePseudoElement(kPseudoIdFirstLetter)) {
    GetElementRareData()->SetPseudoElement(kPseudoIdFirstLetter, nullptr);
    return;
  }

  LayoutObject* remaining_text_layout_object =
      FirstLetterPseudoElement::FirstLetterTextLayoutObject(*element);

  if (!remaining_text_layout_object) {
    GetElementRareData()->SetPseudoElement(kPseudoIdFirstLetter, nullptr);
    return;
  }

  if (phase == StyleUpdatePhase::kRebuildLayoutTree &&
      element->NeedsReattachLayoutTree()) {
    // We were already updated in RecalcStyle and ready for reattach.
    DCHECK(element->GetComputedStyle());
    return;
  }

  bool text_node_changed =
      remaining_text_layout_object !=
      To<FirstLetterPseudoElement>(element)->RemainingTextLayoutObject();

  if (phase == StyleUpdatePhase::kAttachLayoutTree) {
    // RemainingTextLayoutObject should have been cleared from DetachLayoutTree.
    DCHECK(!To<FirstLetterPseudoElement>(element)->RemainingTextLayoutObject());
    DCHECK(text_node_changed);
    scoped_refptr<ComputedStyle> pseudo_style =
        element->StyleForLayoutObject(style_recalc_context);
    if (PseudoElementLayoutObjectIsNeeded(pseudo_style.get(), this))
      element->SetComputedStyle(std::move(pseudo_style));
    else
      GetElementRareData()->SetPseudoElement(kPseudoIdFirstLetter, nullptr);
    element->ClearNeedsStyleRecalc();
    return;
  }

  StyleRecalcChange change(StyleRecalcChange::kRecalcDescendants);
  // Remaining text part should be next to first-letter pseudo element.
  // See http://crbug.com/984389 for details.
  if (text_node_changed || remaining_text_layout_object->PreviousSibling() !=
                               element->GetLayoutObject())
    change = change.ForceReattachLayoutTree();

  element->RecalcStyle(change, style_recalc_context);

  if (element->NeedsReattachLayoutTree() &&
      !PseudoElementLayoutObjectIsNeeded(element->GetComputedStyle(), this)) {
    GetElementRareData()->SetPseudoElement(kPseudoIdFirstLetter, nullptr);
    GetDocument().GetStyleEngine().PseudoElementRemoved(*this);
  }
}

PseudoElement* Element::UpdatePseudoElement(
    PseudoId pseudo_id,
    const StyleRecalcChange change,
    const StyleRecalcContext& style_recalc_context,
    const AtomicString& document_transition_tag) {
  PseudoElement* element = GetPseudoElement(pseudo_id, document_transition_tag);
  if (!element) {
    if ((element = CreatePseudoElementIfNeeded(pseudo_id, style_recalc_context,
                                               document_transition_tag))) {
      // ::before and ::after can have a nested ::marker
      element->CreatePseudoElementIfNeeded(kPseudoIdMarker,
                                           style_recalc_context);
      element->SetNeedsReattachLayoutTree();
    }
    return element;
  }

  if (change.ShouldUpdatePseudoElement(*element)) {
    bool generate_pseudo = CanGeneratePseudoElement(pseudo_id);
    if (generate_pseudo) {
      element->RecalcStyle(change.ForPseudoElement(), style_recalc_context);
      if (element->NeedsReattachLayoutTree() &&
          !PseudoElementLayoutObjectIsNeeded(element->GetComputedStyle(),
                                             this)) {
        generate_pseudo = false;
      }
    }
    if (!generate_pseudo) {
      GetElementRareData()->SetPseudoElement(pseudo_id, nullptr,
                                             document_transition_tag);
      GetDocument().GetStyleEngine().PseudoElementRemoved(*this);
      element = nullptr;
    }
  }

  return element;
}

PseudoElement* Element::CreatePseudoElementIfNeeded(
    PseudoId pseudo_id,
    const StyleRecalcContext& style_recalc_context,
    const AtomicString& document_transition_tag) {
  if (!CanGeneratePseudoElement(pseudo_id))
    return nullptr;
  if (pseudo_id == kPseudoIdFirstLetter) {
    if (!FirstLetterPseudoElement::FirstLetterTextLayoutObject(*this))
      return nullptr;
  }

  PseudoElement* pseudo_element =
      PseudoElement::Create(this, pseudo_id, document_transition_tag);
  EnsureElementRareData().SetPseudoElement(pseudo_id, pseudo_element,
                                           document_transition_tag);
  pseudo_element->InsertedInto(*this);

  scoped_refptr<ComputedStyle> pseudo_style =
      pseudo_element->StyleForLayoutObject(style_recalc_context);
  if (!PseudoElementLayoutObjectIsNeeded(pseudo_style.get(), this)) {
    GetElementRareData()->SetPseudoElement(pseudo_id, nullptr,
                                           document_transition_tag);
    return nullptr;
  }

  if (pseudo_id == kPseudoIdBackdrop)
    GetDocument().AddToTopLayer(pseudo_element, this);
  else if (pseudo_id == kPseudoIdPageTransition)
    GetDocument().AddToTopLayer(pseudo_element);

  pseudo_element->SetComputedStyle(pseudo_style);

  // Most pseudo elements get their style calculated upon insertion, which means
  // that we don't get to RecalcOwnStyle() (regular DOM nodes do get there,
  // since their style isn't calculated directly upon insertion). Need to check
  // now if the element requires legacy layout.
  if (RuntimeEnabledFeatures::LayoutNGEnabled())
    pseudo_element->UpdateForceLegacyLayout(*pseudo_style, nullptr);

  probe::PseudoElementCreated(pseudo_element);

  return pseudo_element;
}

void Element::AttachPseudoElement(PseudoId pseudo_id, AttachContext& context) {
  if (PseudoElement* pseudo_element = GetPseudoElement(pseudo_id))
    pseudo_element->AttachLayoutTree(context);
}

void Element::DetachPseudoElement(PseudoId pseudo_id,
                                  bool performing_reattach) {
  if (PseudoElement* pseudo_element = GetPseudoElement(pseudo_id))
    pseudo_element->DetachLayoutTree(performing_reattach);
}

PseudoElement* Element::GetPseudoElement(
    PseudoId pseudo_id,
    const AtomicString& document_transition_tag) const {
  return HasRareData() ? GetElementRareData()->GetPseudoElement(
                             pseudo_id, document_transition_tag)
                       : nullptr;
}

PseudoElement* Element::GetNestedPseudoElement(
    PseudoId pseudo_id,
    const AtomicString& document_transition_tag) const {
  if (!IsTransitionPseudoElement(pseudo_id))
    return GetPseudoElement(pseudo_id, document_transition_tag);

  // This traverses the pseudo element hierarchy generated in
  // RecalcTransitionPseudoTreeStyle to query nested ::page-transition-container
  // ::page-transition-image-wrapper and
  // ::page-transition-{incoming,outgoing}-image pseudo elements.
  DCHECK_EQ(this, GetDocument().documentElement());
  auto* transition_pseudo = GetPseudoElement(kPseudoIdPageTransition);
  if (!transition_pseudo || pseudo_id == kPseudoIdPageTransition)
    return transition_pseudo;

  auto* container_pseudo = transition_pseudo->GetPseudoElement(
      kPseudoIdPageTransitionContainer, document_transition_tag);
  if (!container_pseudo || pseudo_id == kPseudoIdPageTransitionContainer)
    return container_pseudo;

  auto* wrapper_pseudo = container_pseudo->GetPseudoElement(
      kPseudoIdPageTransitionImageWrapper, document_transition_tag);
  if (!wrapper_pseudo || pseudo_id == kPseudoIdPageTransitionImageWrapper)
    return wrapper_pseudo;

  return wrapper_pseudo->GetPseudoElement(pseudo_id, document_transition_tag);
}

LayoutObject* Element::PseudoElementLayoutObject(PseudoId pseudo_id) const {
  if (PseudoElement* element = GetPseudoElement(pseudo_id))
    return element->GetLayoutObject();
  return nullptr;
}

bool Element::PseudoElementStylesDependOnFontMetrics() const {
  const ComputedStyle* style = GetComputedStyle();
  if (!style)
    return false;
  if (style->CachedPseudoElementStylesDependOnFontMetrics())
    return true;

  // If we don't generate a PseudoElement, its style must have been cached on
  // the originating element's ComputedStyle. Hence, it remains to check styles
  // on the generated PseudoElements.
  if (!HasRareData())
    return false;

  if (GetElementRareData()->ScrollbarPseudoElementStylesDependOnFontMetrics())
    return true;

  // Note that |HasAnyPseudoElementStyles()| counts public pseudo elements only.
  // ::-webkit-scrollbar-*  are internal, and hence are not counted. So we must
  // perform this check after checking scrollbar pseudo element styles.
  if (!style->HasAnyPseudoElementStyles())
    return false;

  for (PseudoElement* pseudo_element :
       GetElementRareData()->GetPseudoElements()) {
    if (pseudo_element->GetComputedStyle()->DependsOnFontMetrics())
      return true;
  }

  return false;
}

const ComputedStyle* Element::CachedStyleForPseudoElement(
    PseudoId pseudo_id,
    const AtomicString& pseudo_argument) {
  // Highlight pseudos are resolved into StyleHighlightData during originating
  // style recalc, and should never be stored in StyleCachedData.
  DCHECK(!UsesHighlightPseudoInheritance(pseudo_id));

  const ComputedStyle* style = GetComputedStyle();

  if (!style || (pseudo_id <= kLastTrackedPublicPseudoId &&
                 !style->HasPseudoElementStyle(pseudo_id))) {
    return nullptr;
  }

  if (const ComputedStyle* cached =
          style->GetCachedPseudoElementStyle(pseudo_id, pseudo_argument))
    return cached;

  scoped_refptr<ComputedStyle> result = UncachedStyleForPseudoElement(
      StyleRequest(pseudo_id, style, pseudo_argument));
  if (result) {
    return style->AddCachedPseudoElementStyle(std::move(result), pseudo_id,
                                              pseudo_argument);
  }
  return nullptr;
}

scoped_refptr<ComputedStyle> Element::UncachedStyleForPseudoElement(
    const StyleRequest& request) {
  // Highlight pseudos are resolved into StyleHighlightData during originating
  // style recalc, where we have the actual StyleRecalcContext.
  DCHECK(!UsesHighlightPseudoInheritance(request.pseudo_id));

  return StyleForPseudoElement(
      StyleRecalcContext::FromInclusiveAncestors(*this), request);
}

scoped_refptr<ComputedStyle> Element::StyleForPseudoElement(
    const StyleRecalcContext& style_recalc_context,
    const StyleRequest& request) {
  const bool is_before_or_after = request.pseudo_id == kPseudoIdBefore ||
                                  request.pseudo_id == kPseudoIdAfter;

  if (is_before_or_after) {
    DCHECK(request.parent_override);
    DCHECK(request.layout_parent_override);

    const ComputedStyle* layout_parent_style = request.parent_override;
    if (layout_parent_style->Display() == EDisplay::kContents) {
      // TODO(futhark@chromium.org): Calling getComputedStyle for elements
      // outside the flat tree should return empty styles, but currently we do
      // not. See issue https://crbug.com/831568. We can replace the if-test
      // with DCHECK(layout_parent) when that issue is fixed.
      if (Node* layout_parent =
              LayoutTreeBuilderTraversal::LayoutParent(*this)) {
        layout_parent_style = layout_parent->GetComputedStyle();
      }
    }
    StyleRequest before_after_request = request;
    before_after_request.layout_parent_override = layout_parent_style;
    return GetDocument().GetStyleResolver().ResolveStyle(
        this, style_recalc_context, before_after_request);
  }

  if (request.pseudo_id == kPseudoIdFirstLineInherited) {
    StyleRequest first_line_inherited_request = request;
    first_line_inherited_request.pseudo_id =
        IsPseudoElement() ? To<PseudoElement>(this)->GetPseudoId()
                          : kPseudoIdNone;
    Element* target = IsPseudoElement() ? parentElement() : this;
    scoped_refptr<ComputedStyle> result =
        GetDocument().GetStyleResolver().ResolveStyle(
            target, style_recalc_context, first_line_inherited_request);
    if (result)
      result->SetStyleType(kPseudoIdFirstLineInherited);
    return result;
  }

  // Use the originating DOM element when resolving style for ::transition*
  // pseudo elements instead of the element's direct ancestor (which could
  // itself be a pseudo element).
  auto* originating_element = IsTransitionPseudoElement(GetPseudoId())
                                  ? GetDocument().documentElement()
                                  : this;
  return GetDocument().GetStyleResolver().ResolveStyle(
      originating_element, style_recalc_context, request);
}

bool Element::CanGeneratePseudoElement(PseudoId pseudo_id) const {
  if (pseudo_id == kPseudoIdPageTransition) {
    DCHECK_EQ(this, GetDocument().documentElement());
    return !GetDocument().GetStyleEngine().DocumentTransitionTags().IsEmpty();
  }
  if (pseudo_id == kPseudoIdBackdrop && !IsInTopLayer())
    return false;
  if (pseudo_id == kPseudoIdFirstLetter && IsSVGElement())
    return false;
  if (const ComputedStyle* style = GetComputedStyle())
    return style->CanGeneratePseudoElement(pseudo_id);
  return false;
}

bool Element::MayTriggerVirtualKeyboard() const {
  return IsEditable(*this);
}

bool Element::matches(const AtomicString& selectors,
                      ExceptionState& exception_state) {
  SelectorQuery* selector_query = GetDocument().GetSelectorQueryCache().Add(
      selectors, GetDocument(), exception_state);
  if (!selector_query)
    return false;
  return selector_query->Matches(*this);
}

bool Element::matches(const AtomicString& selectors) {
  return matches(selectors, ASSERT_NO_EXCEPTION);
}

Element* Element::closest(const AtomicString& selectors,
                          ExceptionState& exception_state) {
  SelectorQuery* selector_query = GetDocument().GetSelectorQueryCache().Add(
      selectors, GetDocument(), exception_state);
  if (!selector_query)
    return nullptr;
  return selector_query->Closest(*this);
}

Element* Element::closest(const AtomicString& selectors) {
  return closest(selectors, ASSERT_NO_EXCEPTION);
}

DOMTokenList& Element::classList() {
  ElementRareData& rare_data = EnsureElementRareData();
  if (!rare_data.GetClassList()) {
    auto* class_list =
        MakeGarbageCollected<DOMTokenList>(*this, html_names::kClassAttr);
    class_list->DidUpdateAttributeValue(g_null_atom,
                                        getAttribute(html_names::kClassAttr));
    rare_data.SetClassList(class_list);
  }
  return *rare_data.GetClassList();
}

DOMStringMap& Element::dataset() {
  ElementRareData& rare_data = EnsureElementRareData();
  if (!rare_data.Dataset())
    rare_data.SetDataset(MakeGarbageCollected<DatasetDOMStringMap>(this));
  return *rare_data.Dataset();
}

KURL Element::HrefURL() const {
  // FIXME: These all have href() or url(), but no common super class. Why
  // doesn't <link> implement URLUtils?
  if (IsA<HTMLAnchorElement>(*this) || IsA<HTMLAreaElement>(*this) ||
      IsA<HTMLLinkElement>(*this))
    return GetURLAttribute(html_names::kHrefAttr);
  if (auto* svg_a = DynamicTo<SVGAElement>(*this))
    return svg_a->LegacyHrefURL(GetDocument());
  return KURL();
}

KURL Element::GetURLAttribute(const QualifiedName& name) const {
#if DCHECK_IS_ON()
  if (GetElementData()) {
    if (const Attribute* attribute = Attributes().Find(name))
      DCHECK(IsURLAttribute(*attribute));
  }
#endif
  return GetDocument().CompleteURL(
      StripLeadingAndTrailingHTMLSpaces(getAttribute(name)));
}

KURL Element::GetNonEmptyURLAttribute(const QualifiedName& name) const {
#if DCHECK_IS_ON()
  if (GetElementData()) {
    if (const Attribute* attribute = Attributes().Find(name))
      DCHECK(IsURLAttribute(*attribute));
  }
#endif
  String value = StripLeadingAndTrailingHTMLSpaces(getAttribute(name));
  if (value.IsEmpty())
    return KURL();
  return GetDocument().CompleteURL(value);
}

int Element::GetIntegralAttribute(const QualifiedName& attribute_name) const {
  return GetIntegralAttribute(attribute_name, 0);
}

int Element::GetIntegralAttribute(const QualifiedName& attribute_name,
                                  int default_value) const {
  int integral_value = default_value;
  ParseHTMLInteger(getAttribute(attribute_name), integral_value);
  return integral_value;
}

unsigned int Element::GetUnsignedIntegralAttribute(
    const QualifiedName& attribute_name) const {
  return static_cast<unsigned int>(
      std::max(0, GetIntegralAttribute(attribute_name)));
}

void Element::SetIntegralAttribute(const QualifiedName& attribute_name,
                                   int value) {
  setAttribute(attribute_name, AtomicString::Number(value));
}

void Element::SetUnsignedIntegralAttribute(const QualifiedName& attribute_name,
                                           unsigned value,
                                           unsigned default_value) {
  // Range restrictions are enforced for unsigned IDL attributes that
  // reflect content attributes,
  //   http://www.whatwg.org/specs/web-apps/current-work/multipage/common-dom-interfaces.html#reflecting-content-attributes-in-idl-attributes
  if (value > 0x7fffffffu)
    value = default_value;
  setAttribute(attribute_name, AtomicString::Number(value));
}

double Element::GetFloatingPointAttribute(const QualifiedName& attribute_name,
                                          double fallback_value) const {
  return ParseToDoubleForNumberType(getAttribute(attribute_name),
                                    fallback_value);
}

void Element::SetFloatingPointAttribute(const QualifiedName& attribute_name,
                                        double value) {
  String serialized_value = SerializeForNumberType(value);
  setAttribute(attribute_name, AtomicString(serialized_value));
}

void Element::SetContainsFullScreenElement(bool flag) {
  SetElementFlag(ElementFlags::kContainsFullScreenElement, flag);
  // When exiting fullscreen, the element's document may not be active.
  if (flag) {
    DCHECK(GetDocument().IsActive());
    GetDocument().GetStyleEngine().EnsureUAStyleForFullscreen();
  }
  PseudoStateChanged(CSSSelector::kPseudoFullScreenAncestor);
}

// Unlike Node::parentOrShadowHostElement, this can cross frame boundaries.
static Element* NextAncestorElement(Element* element) {
  DCHECK(element);
  if (element->ParentOrShadowHostElement())
    return element->ParentOrShadowHostElement();

  Frame* frame = element->GetDocument().GetFrame();
  if (!frame || !frame->Owner())
    return nullptr;

  // Find the next LocalFrame on the ancestor chain, and return the
  // corresponding <iframe> element for the remote child if it exists.
  while (frame->Tree().Parent() && frame->Tree().Parent()->IsRemoteFrame())
    frame = frame->Tree().Parent();

  if (auto* frame_owner_element =
          DynamicTo<HTMLFrameOwnerElement>(frame->Owner()))
    return frame_owner_element;

  return nullptr;
}

void Element::SetContainsFullScreenElementOnAncestorsCrossingFrameBoundaries(
    bool flag) {
  for (Element* element = NextAncestorElement(this); element;
       element = NextAncestorElement(element))
    element->SetContainsFullScreenElement(flag);
}

void Element::SetContainsPersistentVideo(bool value) {
  SetElementFlag(ElementFlags::kContainsPersistentVideo, value);
  PseudoStateChanged(CSSSelector::kPseudoVideoPersistentAncestor);

  // In some rare situations, when the persistent video has been removed from
  // the tree, part of the tree might still carry the flag.
  if (!value && Fullscreen::IsFullscreenElement(*this)) {
    for (Node* node = firstChild(); node;) {
      auto* element = DynamicTo<Element>(node);
      if (!element || !element->ContainsPersistentVideo()) {
        node = node->nextSibling();
        break;
      }

      element->SetContainsPersistentVideo(false);
      node = node->firstChild();
    }
  }
}

void Element::SetIsInTopLayer(bool in_top_layer) {
  if (IsInTopLayer() == in_top_layer)
    return;
  SetElementFlag(ElementFlags::kIsInTopLayer, in_top_layer);
  if (!isConnected())
    return;
  if (!GetDocument().InStyleRecalc())
    SetForceReattachLayoutTree();
}

ScriptValue Element::requestPointerLock(ScriptState* script_state,
                                        const PointerLockOptions* options,
                                        ExceptionState& exception_state) {
  auto* resolver = MakeGarbageCollected<ScriptPromiseResolver>(script_state);
  ScriptPromise promise;
  if (GetDocument().GetPage()) {
    promise =
        GetDocument().GetPage()->GetPointerLockController().RequestPointerLock(
            resolver, this, exception_state, options);
  } else {
    promise = resolver->Promise();
    exception_state.ThrowDOMException(
        DOMExceptionCode::kWrongDocumentError,
        "PointerLock cannot be request when there "
        "is no frame or that frame has no page.");
  }

  if (RuntimeEnabledFeatures::PointerLockOptionsEnabled(
          GetExecutionContext())) {
    if (exception_state.HadException())
      resolver->Reject(exception_state);
    return promise.AsScriptValue();
  }

  // The current spec for PointerLock does not have any language about throwing
  // exceptions. Therefore to be spec compliant we must clear all exceptions.
  // When behind our experimental flag however, we will throw exceptions which
  // should be caught as a promise rejection.
  exception_state.ClearException();

  // Detach the resolver, since we are not using it, to prepare it for garbage
  // collection.
  resolver->Detach();
  return ScriptValue();
}

SpellcheckAttributeState Element::GetSpellcheckAttributeState() const {
  const AtomicString& value = FastGetAttribute(html_names::kSpellcheckAttr);
  if (value == g_null_atom)
    return kSpellcheckAttributeDefault;
  if (EqualIgnoringASCIICase(value, "true") ||
      EqualIgnoringASCIICase(value, ""))
    return kSpellcheckAttributeTrue;
  if (EqualIgnoringASCIICase(value, "false"))
    return kSpellcheckAttributeFalse;

  return kSpellcheckAttributeDefault;
}

bool Element::IsSpellCheckingEnabled() const {
  for (const Element* element = this; element;
       element = element->ParentOrShadowHostElement()) {
    switch (element->GetSpellcheckAttributeState()) {
      case kSpellcheckAttributeTrue:
        return true;
      case kSpellcheckAttributeFalse:
        return false;
      case kSpellcheckAttributeDefault:
        break;
    }
  }

  if (!GetDocument().GetPage())
    return true;

  return GetDocument().GetPage()->GetSettings().GetSpellCheckEnabledByDefault();
}

#if DCHECK_IS_ON()
bool Element::FastAttributeLookupAllowed(const QualifiedName& name) const {
  if (name == html_names::kStyleAttr)
    return false;

  if (auto* svg_element = DynamicTo<SVGElement>(this))
    return !svg_element->IsAnimatableAttribute(name);

  return true;
}
#endif

#if DUMP_NODE_STATISTICS
bool Element::HasNamedNodeMap() const {
  return HasRareData() && GetElementRareData()->AttributeMap();
}
#endif

inline void Element::UpdateName(const AtomicString& old_name,
                                const AtomicString& new_name) {
  if (!IsInDocumentTree())
    return;

  if (old_name == new_name)
    return;

  NamedItemType type = GetNamedItemType();
  if (type != NamedItemType::kNone)
    UpdateNamedItemRegistration(type, old_name, new_name);
}

inline void Element::UpdateId(const AtomicString& old_id,
                              const AtomicString& new_id) {
  if (!IsInTreeScope())
    return;

  if (old_id == new_id)
    return;

  UpdateId(ContainingTreeScope(), old_id, new_id);
}

inline void Element::UpdateId(TreeScope& scope,
                              const AtomicString& old_id,
                              const AtomicString& new_id) {
  DCHECK(IsInTreeScope());
  DCHECK_NE(old_id, new_id);

  if (!old_id.IsEmpty())
    scope.RemoveElementById(old_id, *this);
  if (!new_id.IsEmpty())
    scope.AddElementById(new_id, *this);

  NamedItemType type = GetNamedItemType();
  if (type == NamedItemType::kNameOrId ||
      type == NamedItemType::kNameOrIdWithName)
    UpdateIdNamedItemRegistration(type, old_id, new_id);
}

inline void Element::UpdateFocusgroup(const AtomicString& input) {
  ExecutionContext* context = GetExecutionContext();
  if (!RuntimeEnabledFeatures::FocusgroupEnabled(context))
    return;

  if (ShadowRoot* shadow_root = ContainingShadowRoot())
    shadow_root->SetHasFocusgroupAttributeOnDescendant(true);

  EnsureElementRareData().SetFocusgroupFlags(
      focusgroup::ParseFocusgroup(this, input));
}

void Element::UpdateFocusgroupInShadowRootIfNeeded() {
  ShadowRoot* shadow_root = GetShadowRoot();
  DCHECK(shadow_root);

  // There's no need to re-run the focusgroup parser on the nodes of the shadow
  // tree if none of them had the focusgroup attribute set.
  if (!shadow_root->HasFocusgroupAttributeOnDescendant())
    return;

  Element* ancestor = this;
  bool has_focusgroup_ancestor = false;
  while (ancestor) {
    if (ancestor->GetFocusgroupFlags() != FocusgroupFlags::kNone) {
      has_focusgroup_ancestor = true;
      break;
    }
    ancestor = ancestor->parentElement();
  }

  // We don't need to update the focusgroup value for the ShadowDOM elements if
  // there is no ancestor with a focusgroup value, since the parsing would be
  // exactly the same as the one that happened when we first built the
  // ShadowDOM.
  if (!has_focusgroup_ancestor)
    return;

  // In theory, we should only reach this point when at least one node within
  // the shadow tree has the focusgroup attribute. However, it's possible to get
  // here if a node initially had the focusgroup attribute but then lost it
  // since we don't reset the `ShadowRoot::HasFocusgroupAttributeOnDescendant`
  // upon removing the attribute.
  //
  // Setting this value back to false before iterating over the nodes of the
  // shadow tree allow us to reset the bit in case an update to the shadow tree
  // removed all focusgroup attributes from the shadow tree. If there's still
  // a focusgroup attribute, then the call to `UpdateFocusgroup` below will
  // make sure that the bit is set to true for the containing shadow root.
  shadow_root->SetHasFocusgroupAttributeOnDescendant(false);

  Node* next = FlatTreeTraversal::Next(*this, this);
  while (next) {
    bool skip_subtree = false;
    if (Element* next_element = DynamicTo<Element>(next)) {
      const AtomicString& focusgroup_value =
          next_element->FastGetAttribute(html_names::kFocusgroupAttr);
      if (!focusgroup_value.IsNull())
        next_element->UpdateFocusgroup(focusgroup_value);

      if (auto* next_shadow_root = next_element->GetShadowRoot())
        skip_subtree = !next_shadow_root->HasFocusgroupAttributeOnDescendant();
    }

    if (skip_subtree)
      next = FlatTreeTraversal::NextSkippingChildren(*next, this);
    else
      next = FlatTreeTraversal::Next(*next, this);
  }
}

void Element::WillModifyAttribute(const QualifiedName& name,
                                  const AtomicString& old_value,
                                  const AtomicString& new_value) {
  if (name == html_names::kNameAttr) {
    UpdateName(old_value, new_value);
  }

  if (GetCustomElementState() == CustomElementState::kCustom) {
    CustomElement::EnqueueAttributeChangedCallback(*this, name, old_value,
                                                   new_value);
  }

  if (old_value != new_value) {
    GetDocument().GetStyleEngine().AttributeChangedForElement(name, *this);
  }

  if (MutationObserverInterestGroup* recipients =
          MutationObserverInterestGroup::CreateForAttributesMutation(*this,
                                                                     name)) {
    recipients->EnqueueMutationRecord(
        MutationRecord::CreateAttributes(this, name, old_value));
  }
  probe::WillModifyDOMAttr(this, old_value, new_value);
}

DISABLE_CFI_PERF
void Element::DidAddAttribute(const QualifiedName& name,
                              const AtomicString& value) {
  AttributeChanged(AttributeModificationParams(
      name, g_null_atom, value, AttributeModificationReason::kDirectly));
  if (name == html_names::kIdAttr)
    UpdateId(g_null_atom, value);
  probe::DidModifyDOMAttr(this, name, value);
  DispatchSubtreeModifiedEvent();
}

void Element::DidModifyAttribute(const QualifiedName& name,
                                 const AtomicString& old_value,
                                 const AtomicString& new_value,
                                 AttributeModificationReason reason) {
  if (name == html_names::kIdAttr)
    UpdateId(old_value, new_value);
  AttributeChanged(
      AttributeModificationParams(name, old_value, new_value, reason));
  probe::DidModifyDOMAttr(this, name, new_value);
  // Do not dispatch a DOMSubtreeModified event here; see bug 81141.
}

void Element::DidRemoveAttribute(const QualifiedName& name,
                                 const AtomicString& old_value) {
  if (name == html_names::kIdAttr)
    UpdateId(old_value, g_null_atom);
  AttributeChanged(AttributeModificationParams(
      name, old_value, g_null_atom, AttributeModificationReason::kDirectly));
  probe::DidRemoveDOMAttr(this, name);
  DispatchSubtreeModifiedEvent();
}

static bool NeedsURLResolutionForInlineStyle(const Element& element,
                                             const Document& old_document,
                                             const Document& new_document) {
  if (old_document == new_document)
    return false;
  if (old_document.BaseURL() == new_document.BaseURL())
    return false;
  const CSSPropertyValueSet* style = element.InlineStyle();
  if (!style)
    return false;
  for (unsigned i = 0; i < style->PropertyCount(); ++i) {
    if (style->PropertyAt(i).Value().MayContainUrl())
      return true;
  }
  return false;
}

static void ReResolveURLsInInlineStyle(const Document& document,
                                       MutableCSSPropertyValueSet& style) {
  for (unsigned i = 0; i < style.PropertyCount(); ++i) {
    const CSSValue& value = style.PropertyAt(i).Value();
    if (value.MayContainUrl())
      value.ReResolveUrl(document);
  }
}

void Element::DidMoveToNewDocument(Document& old_document) {
  Node::DidMoveToNewDocument(old_document);

  // If the documents differ by quirks mode then they differ by case sensitivity
  // for class and id names so we need to go through the attribute change logic
  // to pick up the new casing in the ElementData.
  if (old_document.InQuirksMode() != GetDocument().InQuirksMode()) {
    // TODO(tkent): If new owner Document has a ShareableElementData matching to
    // this element's attributes, we shouldn't make UniqueElementData, and this
    // element should point to the shareable one.
    EnsureUniqueElementData();

    if (const AtomicString& idAttr = GetIdAttribute())
      SetIdAttribute(idAttr);
    if (const AtomicString& classAttr = GetClassAttribute()) {
      // Going through setAttribute() to synchronize the attribute is only
      // required when setting the "style" attribute (this sets the "class"
      // attribute) or for an SVG element (in which case `GetClassAttribute`
      // above would already have synchronized).
      SetAttributeInternal(FindAttributeIndex(html_names::kClassAttr),
                           html_names::kClassAttr, classAttr,
                           AttributeModificationReason::kByMoveToNewDocument);
    }
  }
  // TODO(tkent): Even if Documents' modes are same, keeping
  // ShareableElementData owned by old_document isn't right.

  if (NeedsURLResolutionForInlineStyle(*this, old_document, GetDocument()))
    ReResolveURLsInInlineStyle(GetDocument(), EnsureMutableInlineStyle());

  if (auto* context = GetDisplayLockContext())
    context->DidMoveToNewDocument(old_document);
}

void Element::UpdateNamedItemRegistration(NamedItemType type,
                                          const AtomicString& old_name,
                                          const AtomicString& new_name) {
  auto* doc = DynamicTo<HTMLDocument>(GetDocument());
  if (!doc)
    return;

  if (!old_name.IsEmpty())
    doc->RemoveNamedItem(old_name);

  if (!new_name.IsEmpty())
    doc->AddNamedItem(new_name);

  if (type == NamedItemType::kNameOrIdWithName) {
    const AtomicString id = GetIdAttribute();
    if (!id.IsEmpty()) {
      if (!old_name.IsEmpty() && new_name.IsEmpty())
        doc->RemoveNamedItem(id);
      else if (old_name.IsEmpty() && !new_name.IsEmpty())
        doc->AddNamedItem(id);
    }
  }
}

void Element::UpdateIdNamedItemRegistration(NamedItemType type,
                                            const AtomicString& old_id,
                                            const AtomicString& new_id) {
  auto* doc = DynamicTo<HTMLDocument>(GetDocument());
  if (!doc)
    return;

  if (type == NamedItemType::kNameOrIdWithName && GetNameAttribute().IsEmpty())
    return;

  if (!old_id.IsEmpty())
    doc->RemoveNamedItem(old_id);

  if (!new_id.IsEmpty())
    doc->AddNamedItem(new_id);
}

ScrollOffset Element::SavedLayerScrollOffset() const {
  return HasRareData() ? GetElementRareData()->SavedLayerScrollOffset()
                       : ScrollOffset();
}

void Element::SetSavedLayerScrollOffset(const ScrollOffset& size) {
  if (size.IsZero() && !HasRareData())
    return;
  EnsureElementRareData().SetSavedLayerScrollOffset(size);
}

Attr* Element::AttrIfExists(const QualifiedName& name) {
  if (AttrNodeList* attr_node_list = GetAttrNodeList()) {
    for (const auto& attr : *attr_node_list) {
      if (attr->GetQualifiedName().Matches(name))
        return attr.Get();
    }
  }
  return nullptr;
}

Attr* Element::EnsureAttr(const QualifiedName& name) {
  Attr* attr_node = AttrIfExists(name);
  if (!attr_node) {
    attr_node = MakeGarbageCollected<Attr>(*this, name);
    GetTreeScope().AdoptIfNeeded(*attr_node);
    EnsureElementRareData().AddAttr(attr_node);
  }
  return attr_node;
}

void Element::DetachAttrNodeFromElementWithValue(Attr* attr_node,
                                                 const AtomicString& value) {
  DCHECK(GetAttrNodeList());
  attr_node->DetachFromElementWithValue(value);

  AttrNodeList* list = GetAttrNodeList();
  wtf_size_t index = list->Find(attr_node);
  DCHECK_NE(index, kNotFound);
  list->EraseAt(index);
  if (list->IsEmpty())
    RemoveAttrNodeList();
}

void Element::DetachAllAttrNodesFromElement() {
  AttrNodeList* list = GetAttrNodeList();
  if (!list)
    return;

  AttributeCollection attributes = GetElementData()->Attributes();
  for (const Attribute& attr : attributes) {
    if (Attr* attr_node = AttrIfExists(attr.GetName()))
      attr_node->DetachFromElementWithValue(attr.Value());
  }

  RemoveAttrNodeList();
}

void Element::WillRecalcStyle(const StyleRecalcChange) {
  DCHECK(HasCustomStyleCallbacks());
}

void Element::DidRecalcStyle(const StyleRecalcChange) {
  DCHECK(HasCustomStyleCallbacks());
}

scoped_refptr<ComputedStyle> Element::CustomStyleForLayoutObject(
    const StyleRecalcContext& style_recalc_context) {
  DCHECK(HasCustomStyleCallbacks());
  scoped_refptr<ComputedStyle> style =
      OriginalStyleForLayoutObject(style_recalc_context);
  // TODO(crbug.com/1197720): This logic is for positioning the selectmenu
  // popup. This should be replaced by the new anchored positioning scheme.
  if (HasValidPopupAttribute() &&
      GetPopupData()->needsRepositioningForSelectMenu()) {
    DCHECK(RuntimeEnabledFeatures::HTMLSelectMenuElementEnabled());
    DCHECK(RuntimeEnabledFeatures::HTMLPopupAttributeEnabled());
    AdjustPopupPositionForSelectMenu(*style);
  }
  return style;
}

void Element::CloneAttributesFrom(const Element& other) {
  if (HasRareData())
    DetachAllAttrNodesFromElement();

  other.SynchronizeAllAttributes();
  if (!other.element_data_) {
    element_data_.Clear();
    return;
  }

  const AtomicString& old_id = GetIdAttribute();
  const AtomicString& new_id = other.GetIdAttribute();

  if (!old_id.IsNull() || !new_id.IsNull())
    UpdateId(old_id, new_id);

  const AtomicString& old_name = GetNameAttribute();
  const AtomicString& new_name = other.GetNameAttribute();

  if (!old_name.IsNull() || !new_name.IsNull())
    UpdateName(old_name, new_name);

  // Quirks mode makes class and id not case sensitive. We can't share the
  // ElementData if the idForStyleResolution and the className need different
  // casing.
  bool owner_documents_have_different_case_sensitivity = false;
  if (other.HasClass() || other.HasID())
    owner_documents_have_different_case_sensitivity =
        other.GetDocument().InQuirksMode() != GetDocument().InQuirksMode();

  // If 'other' has a mutable ElementData, convert it to an immutable one so we
  // can share it between both elements.
  // We can only do this if there are no presentation attributes and sharing the
  // data won't result in different case sensitivity of class or id.
  auto* unique_element_data =
      DynamicTo<UniqueElementData>(other.element_data_.Get());
  if (unique_element_data && !owner_documents_have_different_case_sensitivity &&
      !other.element_data_->PresentationAttributeStyle()) {
    const_cast<Element&>(other).element_data_ =
        unique_element_data->MakeShareableCopy();
  }

  if (!other.element_data_->IsUnique() &&
      !owner_documents_have_different_case_sensitivity &&
      !NeedsURLResolutionForInlineStyle(other, other.GetDocument(),
                                        GetDocument()))
    element_data_ = other.element_data_;
  else
    element_data_ = other.element_data_->MakeUniqueCopy();

  for (const Attribute& attr : element_data_->Attributes()) {
    AttributeChanged(
        AttributeModificationParams(attr.GetName(), g_null_atom, attr.Value(),
                                    AttributeModificationReason::kByCloning));
  }

  if (other.nonce() != g_null_atom)
    setNonce(other.nonce());
}

void Element::CreateUniqueElementData() {
  if (!element_data_) {
    element_data_ = MakeGarbageCollected<UniqueElementData>();
  } else {
    DCHECK(!IsA<UniqueElementData>(element_data_.Get()));
    element_data_ =
        To<ShareableElementData>(element_data_.Get())->MakeUniqueCopy();
  }
}

void Element::SynchronizeStyleAttributeInternal() const {
  DCHECK(IsStyledElement());
  DCHECK(GetElementData());
  DCHECK(GetElementData()->style_attribute_is_dirty());
  GetElementData()->SetStyleAttributeIsDirty(false);
  const CSSPropertyValueSet* inline_style = InlineStyle();
  const_cast<Element*>(this)->SetSynchronizedLazyAttribute(
      html_names::kStyleAttr,
      inline_style ? AtomicString(inline_style->AsText()) : g_empty_atom);
}

CSSStyleDeclaration* Element::style() {
  if (!IsStyledElement())
    return nullptr;
  return &EnsureElementRareData().EnsureInlineCSSStyleDeclaration(this);
}

StylePropertyMap* Element::attributeStyleMap() {
  if (!IsStyledElement())
    return nullptr;
  return &EnsureElementRareData().EnsureInlineStylePropertyMap(this);
}

StylePropertyMapReadOnly* Element::ComputedStyleMap() {
  return GetDocument().ComputedStyleMap(this);
}

MutableCSSPropertyValueSet& Element::EnsureMutableInlineStyle() {
  DCHECK(IsStyledElement());
  Member<CSSPropertyValueSet>& inline_style =
      EnsureUniqueElementData().inline_style_;
  if (!inline_style) {
    CSSParserMode mode = (!IsHTMLElement() || GetDocument().InQuirksMode())
                             ? kHTMLQuirksMode
                             : kHTMLStandardMode;
    inline_style = MakeGarbageCollected<MutableCSSPropertyValueSet>(mode);
  } else if (!inline_style->IsMutable()) {
    inline_style = inline_style->MutableCopy();
  }
  return *To<MutableCSSPropertyValueSet>(inline_style.Get());
}

void Element::ClearMutableInlineStyleIfEmpty() {
  if (EnsureMutableInlineStyle().IsEmpty()) {
    EnsureUniqueElementData().inline_style_.Clear();
  }
}

void Element::NotifyInlineStyleMutation() {
  if (GetLayoutObject() && GetLayoutObject()->PreviousVisibilityVisible() &&
      GetDocument().GetPage()) {
    GetDocument().GetPage()->Animator().SetHasInlineStyleMutation();
  }
}

inline void Element::SetInlineStyleFromString(
    const AtomicString& new_style_string) {
  DCHECK(IsStyledElement());
  Member<CSSPropertyValueSet>& inline_style = GetElementData()->inline_style_;

  // Avoid redundant work if we're using shared attribute data with already
  // parsed inline style.
  if (inline_style && !GetElementData()->IsUnique())
    return;

  // We reconstruct the property set instead of mutating if there is no CSSOM
  // wrapper.  This makes wrapperless property sets immutable and so cacheable.
  if (inline_style && !inline_style->IsMutable())
    inline_style.Clear();

  if (!inline_style) {
    inline_style =
        CSSParser::ParseInlineStyleDeclaration(new_style_string, this);
  } else {
    DCHECK(inline_style->IsMutable());
    static_cast<MutableCSSPropertyValueSet*>(inline_style.Get())
        ->ParseDeclarationList(
            new_style_string,
            GetExecutionContext()
                ? GetExecutionContext()->GetSecureContextMode()
                : SecureContextMode::kInsecureContext,
            GetDocument().ElementSheet().Contents());
  }
}

void Element::StyleAttributeChanged(
    const AtomicString& new_style_string,
    AttributeModificationReason modification_reason) {
  DCHECK(IsStyledElement());
  WTF::OrdinalNumber start_line_number = WTF::OrdinalNumber::BeforeFirst();
  if (GetDocument().GetScriptableDocumentParser() &&
      !GetDocument().IsInDocumentWrite())
    start_line_number =
        GetDocument().GetScriptableDocumentParser()->LineNumber();

  if (new_style_string.IsNull()) {
    EnsureUniqueElementData().inline_style_.Clear();
  } else if (modification_reason == AttributeModificationReason::kByCloning ||
             (ContainingShadowRoot() &&
              ContainingShadowRoot()->IsUserAgent()) ||
             (GetExecutionContext() &&
              GetExecutionContext()
                  ->GetContentSecurityPolicyForCurrentWorld()
                  ->AllowInline(
                      ContentSecurityPolicy::InlineType::kStyleAttribute, this,
                      new_style_string, String() /* nonce */,
                      GetDocument().Url(), start_line_number))) {
    SetInlineStyleFromString(new_style_string);
  }

  GetElementData()->SetStyleAttributeIsDirty(false);

  SetNeedsStyleRecalc(kLocalStyleChange,
                      StyleChangeReasonForTracing::Create(
                          style_change_reason::kStyleSheetChange));
  probe::DidInvalidateStyleAttr(this);
}

void Element::InlineStyleChanged() {
  // NOTE: This is conservative; we can be more precise it in the future
  // if need be.
  const bool only_changed_independent_properties = false;

  DCHECK(IsStyledElement());
  InvalidateStyleAttribute(only_changed_independent_properties);
  probe::DidInvalidateStyleAttr(this);

  if (MutationObserverInterestGroup* recipients =
          MutationObserverInterestGroup::CreateForAttributesMutation(
              *this, html_names::kStyleAttr)) {
    // We don't use getAttribute() here to get a style attribute value
    // before the change.
    AtomicString old_value;
    if (const Attribute* attribute =
            GetElementData()->Attributes().Find(html_names::kStyleAttr))
      old_value = attribute->Value();
    recipients->EnqueueMutationRecord(MutationRecord::CreateAttributes(
        this, html_names::kStyleAttr, old_value));
    // Need to synchronize every time so that following MutationRecords will
    // have correct oldValues.
    SynchronizeAttribute(html_names::kStyleAttr);
  }
}

void Element::SetInlineStyleProperty(CSSPropertyID property_id,
                                     CSSValueID identifier,
                                     bool important) {
  DCHECK_NE(property_id, CSSPropertyID::kVariable);
  SetInlineStyleProperty(property_id, *CSSIdentifierValue::Create(identifier),
                         important);
}

void Element::SetInlineStyleProperty(CSSPropertyID property_id,
                                     double value,
                                     CSSPrimitiveValue::UnitType unit,
                                     bool important) {
  DCHECK_NE(property_id, CSSPropertyID::kVariable);
  SetInlineStyleProperty(
      property_id, *CSSNumericLiteralValue::Create(value, unit), important);
}

void Element::SetInlineStyleProperty(CSSPropertyID property_id,
                                     const CSSValue& value,
                                     bool important) {
  DCHECK_NE(property_id, CSSPropertyID::kVariable);
  DCHECK(IsStyledElement());
  EnsureMutableInlineStyle().SetProperty(property_id, value, important);
  InlineStyleChanged();
}

bool Element::SetInlineStyleProperty(CSSPropertyID property_id,
                                     const String& value,
                                     bool important) {
  DCHECK_NE(property_id, CSSPropertyID::kVariable);
  DCHECK(IsStyledElement());
  bool did_change =
      EnsureMutableInlineStyle().SetProperty(
          property_id, value, important,
          GetExecutionContext() ? GetExecutionContext()->GetSecureContextMode()
                                : SecureContextMode::kInsecureContext,
          GetDocument().ElementSheet().Contents()) >=
      MutableCSSPropertyValueSet::kModifiedExisting;
  if (did_change)
    InlineStyleChanged();
  return did_change;
}

void Element::SetInlineStyleProperty(const CSSPropertyName& name,
                                     const CSSValue& value,
                                     bool important) {
  DCHECK(IsStyledElement());
  EnsureMutableInlineStyle().SetProperty(name, value, important);
  InlineStyleChanged();
}

bool Element::RemoveInlineStyleProperty(CSSPropertyID property_id) {
  DCHECK(IsStyledElement());
  if (!InlineStyle())
    return false;
  bool did_change = EnsureMutableInlineStyle().RemoveProperty(property_id);
  if (did_change)
    InlineStyleChanged();
  return did_change;
}

bool Element::RemoveInlineStyleProperty(const AtomicString& property_name) {
  DCHECK(IsStyledElement());
  if (!InlineStyle())
    return false;
  bool did_change = EnsureMutableInlineStyle().RemoveProperty(property_name);
  if (did_change)
    InlineStyleChanged();
  return did_change;
}

void Element::RemoveAllInlineStyleProperties() {
  DCHECK(IsStyledElement());
  if (!InlineStyle())
    return;
  EnsureMutableInlineStyle().Clear();
  InlineStyleChanged();
}

void Element::UpdatePresentationAttributeStyle() {
  SynchronizeAllAttributes();
  // ShareableElementData doesn't store presentation attribute style, so make
  // sure we have a UniqueElementData.
  UniqueElementData& element_data = EnsureUniqueElementData();
  element_data.SetPresentationAttributeStyleIsDirty(false);
  element_data.presentation_attribute_style_ =
      ComputePresentationAttributeStyle(*this);

  if (RuntimeEnabledFeatures::BeforeMatchEventEnabled(GetExecutionContext())) {
    // We could do this in CreatePresentationAttributeStyle or
    // HTMLElement::CollectStyleForPresentationAttribute when we actually
    // iterate over attributes, but the presentational style gets cached so
    // those functions aren't necessarily called every time. This function
    // actually gets called every time, so we must do this check here.
    AttributeCollection attributes = AttributesWithoutUpdate();
    auto* hidden_attr = attributes.Find("hidden");
    if (hidden_attr && hidden_attr->Value() == "until-found") {
      EnsureDisplayLockContext().SetIsHiddenUntilFoundElement(true);
    } else if (DisplayLockContext* context = GetDisplayLockContext()) {
      context->SetIsHiddenUntilFoundElement(false);
    }
  }
}

CSSPropertyValueSet* Element::CreatePresentationAttributeStyle() {
  auto* style = MakeGarbageCollected<MutableCSSPropertyValueSet>(
      IsSVGElement() ? kSVGAttributeMode : kHTMLStandardMode);
  AttributeCollection attributes = AttributesWithoutUpdate();
  for (const Attribute& attr : attributes)
    CollectStyleForPresentationAttribute(attr.GetName(), attr.Value(), style);
  CollectExtraStyleForPresentationAttribute(style);
  return style;
}

void Element::AddPropertyToPresentationAttributeStyle(
    MutableCSSPropertyValueSet* style,
    CSSPropertyID property_id,
    CSSValueID identifier) {
  DCHECK(IsStyledElement());
  style->SetProperty(property_id, *CSSIdentifierValue::Create(identifier));
}

void Element::AddPropertyToPresentationAttributeStyle(
    MutableCSSPropertyValueSet* style,
    CSSPropertyID property_id,
    double value,
    CSSPrimitiveValue::UnitType unit) {
  DCHECK(IsStyledElement());
  style->SetProperty(property_id, *CSSNumericLiteralValue::Create(value, unit));
}

void Element::AddPropertyToPresentationAttributeStyle(
    MutableCSSPropertyValueSet* style,
    CSSPropertyID property_id,
    const String& value) {
  DCHECK(IsStyledElement());
  style->SetProperty(property_id, value, false,
                     GetExecutionContext()
                         ? GetExecutionContext()->GetSecureContextMode()
                         : SecureContextMode::kInsecureContext,
                     GetDocument().ElementSheet().Contents());
}

void Element::AddPropertyToPresentationAttributeStyle(
    MutableCSSPropertyValueSet* style,
    CSSPropertyID property_id,
    const CSSValue& value) {
  DCHECK(IsStyledElement());
  style->SetProperty(property_id, value);
}

void Element::LogAddElementIfIsolatedWorldAndInDocument(
    const char element[],
    const QualifiedName& attr1) {
  if (!isConnected())
    return;
  V8DOMActivityLogger* activity_logger =
      V8DOMActivityLogger::CurrentActivityLoggerIfIsolatedWorldForMainThread();
  if (!activity_logger)
    return;
  Vector<String, 2> argv;
  argv.push_back(element);
  argv.push_back(FastGetAttribute(attr1));
  activity_logger->LogEvent("blinkAddElement", argv.size(), argv.data());
}

void Element::LogAddElementIfIsolatedWorldAndInDocument(
    const char element[],
    const QualifiedName& attr1,
    const QualifiedName& attr2) {
  if (!isConnected())
    return;
  V8DOMActivityLogger* activity_logger =
      V8DOMActivityLogger::CurrentActivityLoggerIfIsolatedWorldForMainThread();
  if (!activity_logger)
    return;
  Vector<String, 3> argv;
  argv.push_back(element);
  argv.push_back(FastGetAttribute(attr1));
  argv.push_back(FastGetAttribute(attr2));
  activity_logger->LogEvent("blinkAddElement", argv.size(), argv.data());
}

void Element::LogAddElementIfIsolatedWorldAndInDocument(
    const char element[],
    const QualifiedName& attr1,
    const QualifiedName& attr2,
    const QualifiedName& attr3) {
  if (!isConnected())
    return;
  V8DOMActivityLogger* activity_logger =
      V8DOMActivityLogger::CurrentActivityLoggerIfIsolatedWorldForMainThread();
  if (!activity_logger)
    return;
  Vector<String, 4> argv;
  argv.push_back(element);
  argv.push_back(FastGetAttribute(attr1));
  argv.push_back(FastGetAttribute(attr2));
  argv.push_back(FastGetAttribute(attr3));
  activity_logger->LogEvent("blinkAddElement", argv.size(), argv.data());
}

void Element::LogUpdateAttributeIfIsolatedWorldAndInDocument(
    const char element[],
    const AttributeModificationParams& params) {
  if (!isConnected())
    return;
  V8DOMActivityLogger* activity_logger =
      V8DOMActivityLogger::CurrentActivityLoggerIfIsolatedWorldForMainThread();
  if (!activity_logger)
    return;
  Vector<String, 4> argv;
  argv.push_back(element);
  argv.push_back(params.name.ToString());
  argv.push_back(params.old_value);
  argv.push_back(params.new_value);
  activity_logger->LogEvent("blinkSetAttribute", argv.size(), argv.data());
}

void Element::Trace(Visitor* visitor) const {
  visitor->Trace(element_data_);
  ContainerNode::Trace(visitor);
}

bool Element::HasPart() const {
  if (HasRareData()) {
    if (auto* part = GetElementRareData()->GetPart()) {
      return part->length() > 0;
    }
  }
  return false;
}

DOMTokenList* Element::GetPart() const {
  return HasRareData() ? GetElementRareData()->GetPart() : nullptr;
}

DOMTokenList& Element::part() {
  ElementRareData& rare_data = EnsureElementRareData();
  DOMTokenList* part = rare_data.GetPart();
  if (!part) {
    part = MakeGarbageCollected<DOMTokenList>(*this, html_names::kPartAttr);
    rare_data.SetPart(part);
  }
  return *part;
}

bool Element::HasPartNamesMap() const {
  const NamesMap* names_map = PartNamesMap();
  return names_map && names_map->size() > 0;
}

const NamesMap* Element::PartNamesMap() const {
  return HasRareData() ? GetElementRareData()->PartNamesMap() : nullptr;
}

bool Element::ChildStyleRecalcBlockedByDisplayLock() const {
  auto* context = GetDisplayLockContext();
  return context && !context->ShouldStyleChildren();
}

void Element::SetHovered(bool hovered) {
  if (hovered == IsHovered())
    return;

  GetDocument().UserActionElements().SetHovered(this, hovered);
  HandlePopupHovered(hovered);

  const ComputedStyle* style = GetComputedStyle();
  if (!style || style->AffectedByHover()) {
    StyleChangeType change_type = kLocalStyleChange;
    if (style && style->HasPseudoElementStyle(kPseudoIdFirstLetter))
      change_type = kSubtreeStyleChange;
    SetNeedsStyleRecalc(change_type,
                        StyleChangeReasonForTracing::CreateWithExtraData(
                            style_change_reason::kPseudoClass,
                            style_change_extra_data::g_hover));
  }
  PseudoStateChanged(CSSSelector::kPseudoHover);

  InvalidateIfHasEffectiveAppearance();
}

void Element::SetActive(bool active) {
  if (active == IsActive())
    return;

  GetDocument().UserActionElements().SetActive(this, active);

  if (!GetLayoutObject()) {
    if (!ChildrenOrSiblingsAffectedByActive()) {
      SetNeedsStyleRecalc(kLocalStyleChange,
                          StyleChangeReasonForTracing::CreateWithExtraData(
                              style_change_reason::kPseudoClass,
                              style_change_extra_data::g_active));
    }
    PseudoStateChanged(CSSSelector::kPseudoActive);
    return;
  }

  if (GetComputedStyle()->AffectedByActive()) {
    StyleChangeType change_type =
        GetComputedStyle()->HasPseudoElementStyle(kPseudoIdFirstLetter)
            ? kSubtreeStyleChange
            : kLocalStyleChange;
    SetNeedsStyleRecalc(change_type,
                        StyleChangeReasonForTracing::CreateWithExtraData(
                            style_change_reason::kPseudoClass,
                            style_change_extra_data::g_active));
  }
  PseudoStateChanged(CSSSelector::kPseudoActive);

  if (!IsDisabledFormControl())
    InvalidateIfHasEffectiveAppearance();
}

void Element::InvalidateStyleAttribute(
    bool only_changed_independent_properties) {
  DCHECK(GetElementData());
  GetElementData()->SetStyleAttributeIsDirty(true);
  SetNeedsStyleRecalc(only_changed_independent_properties
                          ? kInlineIndependentStyleChange
                          : kLocalStyleChange,
                      StyleChangeReasonForTracing::Create(
                          style_change_reason::kInlineCSSStyleMutated));
  GetDocument().GetStyleEngine().AttributeChangedForElement(
      html_names::kStyleAttr, *this);
}

void Element::RecalcTransitionPseudoTreeStyle(
    const Vector<AtomicString>& document_transition_tags) {
  DCHECK_EQ(this, GetDocument().documentElement());

  auto* old_transition_pseudo = GetPseudoElement(kPseudoIdPageTransition);
  if (document_transition_tags.IsEmpty() && !old_transition_pseudo)
    return;

  const StyleRecalcChange style_recalc_change;
  const StyleRecalcContext style_recalc_context =
      StyleRecalcContext::FromInclusiveAncestors(
          *GetDocument().documentElement());

  auto* transition_pseudo =
      UpdatePseudoElement(kPseudoIdPageTransition, style_recalc_change,
                          style_recalc_context, g_null_atom);
  if (!transition_pseudo)
    return;

  for (const auto& document_transition_tag : document_transition_tags) {
    auto* container_pseudo = transition_pseudo->UpdatePseudoElement(
        kPseudoIdPageTransitionContainer, style_recalc_change,
        style_recalc_context, document_transition_tag);
    if (!container_pseudo)
      continue;

    auto* wrapper_pseudo = container_pseudo->UpdatePseudoElement(
        kPseudoIdPageTransitionImageWrapper, style_recalc_change,
        style_recalc_context, document_transition_tag);
    if (!wrapper_pseudo)
      continue;

    wrapper_pseudo->UpdatePseudoElement(
        kPseudoIdPageTransitionOutgoingImage, style_recalc_change,
        style_recalc_context, document_transition_tag);
    wrapper_pseudo->UpdatePseudoElement(
        kPseudoIdPageTransitionIncomingImage, style_recalc_change,
        style_recalc_context, document_transition_tag);

    container_pseudo->ClearChildNeedsStyleRecalc();
    wrapper_pseudo->ClearChildNeedsStyleRecalc();
  }

  // Regular pseudo update doesn't clear child style, since there are
  // (typically) no children / dirty child style. However, here we do need to
  // clear the child dirty bit.
  transition_pseudo->ClearChildNeedsStyleRecalc();
}

void Element::RebuildTransitionPseudoLayoutTree(
    const Vector<AtomicString>& document_transition_tags) {
  DCHECK_EQ(this, GetDocument().documentElement());

  if (document_transition_tags.IsEmpty()) {
    DCHECK(!GetPseudoElement(kPseudoIdPageTransition));
    return;
  }

  WhitespaceAttacher whitespace_attacher;
  auto rebuild_pseudo_tree =
      [&whitespace_attacher](PseudoElement* pseudo_element) {
        pseudo_element->RebuildLayoutTree(whitespace_attacher);
      };
  DocumentTransitionUtils::ForEachTransitionPseudo(GetDocument(),
                                                   rebuild_pseudo_tree);
}

bool Element::IsInertRoot() {
  return RuntimeEnabledFeatures::InertAttributeEnabled() &&
         FastHasAttribute(html_names::kInertAttr) && IsHTMLElement();
}

FocusgroupFlags Element::GetFocusgroupFlags() const {
  ExecutionContext* context = GetExecutionContext();
  if (!RuntimeEnabledFeatures::FocusgroupEnabled(context) || !HasRareData())
    return FocusgroupFlags::kNone;
  return GetElementRareData()->GetFocusgroupFlags();
}

bool Element::checkVisibility(CheckVisibilityOptions* options) const {
  // Unlock ancestor content-visibility:auto elements. If this element is
  // offscreen and locked due to content-visibility:auto, this method should not
  // count that as invisible.
  DisplayLockUtilities::ScopedForcedUpdate force_locks(
      this, DisplayLockContext::ForcedPhase::kStyleAndLayoutTree,
      /*include_self=*/false, /*only_cv_auto=*/true,
      /*emit_warnings=*/false);
  GetDocument().UpdateStyleAndLayoutTree();

  if (!GetLayoutObject())
    return false;

  auto* style = GetComputedStyle();
  if (!style)
    return false;

  DCHECK(options);
  if (options->checkVisibilityCSS() &&
      style->Visibility() != EVisibility::kVisible) {
    return false;
  }

  for (Node& ancestor : FlatTreeTraversal::InclusiveAncestorsOf(*this)) {
    // Check for content-visibility:hidden
    if (&ancestor != this) {
      if (Element* ancestor_element = DynamicTo<Element>(ancestor)) {
        if (auto* lock = ancestor_element->GetDisplayLockContext()) {
          if (lock->IsLocked() &&
              !lock->IsActivatable(DisplayLockActivationReason::kViewport)) {
            return false;
          }
        }
      }
    }

    // Check for opacity:0
    if (options->checkOpacity()) {
      if (auto* style = ancestor.GetComputedStyle()) {
        if (style->Opacity() == 0.f) {
          return false;
        }
      }
    }
  }

  return true;
}

WTF::AtomicStringTable::WeakResult Element::WeakLowercaseIfNecessary(
    const AtomicString& name) const {
  if (LIKELY(IsHTMLElement() && IsA<HTMLDocument>(GetDocument()))) {
    if (name.IsEmpty() || name.IsLowerASCII())
      return WTF::AtomicStringTable::WeakResult(name);
    return WTF::AtomicStringTable::Instance().WeakFindLowercase(name);
  }

  return WTF::AtomicStringTable::WeakResult(name);
}

// Note, SynchronizeAttributeHinted is safe to call between a WeakFind() and
// a check on the AttributeCollection for the element even though it may
// modify the AttributeCollection to insert a "style" attribute. The reason
// is because html_names::kStyleAttr.LocalName() is an AtomicString
// representing "style". This means the AtomicStringTable will always have
// an entry for "style" and a `hint` that corresponds to
// html_names::kStyleAttr.LocalName() will never refer to a deleted object
// thus it is safe to insert html_names::kStyleAttr.LocalName() into the
// AttributeCollection collection after the WeakFind() when `hint` is
// referring to "style". A subsequent lookup will match itself correctly
// without worry for UaF or false positives.
void Element::SynchronizeAttributeHinted(
    const AtomicString& local_name,
    WTF::AtomicStringTable::WeakResult hint) const {
  // This version of SynchronizeAttribute() is streamlined for the case where
  // you don't have a full QualifiedName, e.g when called from DOM API.
  if (!GetElementData())
    return;
  // TODO(ajwong): Does this unnecessarily synchronize style attributes on
  // SVGElements?
  if (GetElementData()->style_attribute_is_dirty() &&
      hint == html_names::kStyleAttr.LocalName()) {
    DCHECK(IsStyledElement());
    SynchronizeStyleAttributeInternal();
    return;
  }
  if (GetElementData()->svg_attributes_are_dirty()) {
    // We're passing a null namespace argument. svg_names::k*Attr are defined in
    // the null namespace, but for attributes that are not (like 'href' in the
    // XLink NS), this will not do the right thing.

    // TODO(fs): svg_attributes_are_dirty_ stays dirty unless
    // SynchronizeSVGAttribute is called with AnyQName(). This means that even
    // if Element::SynchronizeAttribute() is called on all attributes,
    // svg_attributes_are_dirty_ remains true. This information is available in
    // the attribute->property map in SVGElement.
    To<SVGElement>(this)->SynchronizeSVGAttribute(
        QualifiedName(g_null_atom, local_name, g_null_atom));
  }
}

const AtomicString& Element::GetAttributeHinted(
    const AtomicString& name,
    WTF::AtomicStringTable::WeakResult hint) const {
  if (!GetElementData())
    return g_null_atom;
  SynchronizeAttributeHinted(name, hint);
  if (const Attribute* attribute =
          GetElementData()->Attributes().FindHinted(name, hint))
    return attribute->Value();
  return g_null_atom;
}

std::pair<wtf_size_t, const QualifiedName> Element::LookupAttributeQNameHinted(
    AtomicString name,
    WTF::AtomicStringTable::WeakResult hint) const {
  if (!GetElementData()) {
    return std::make_pair(
        kNotFound,
        QualifiedName(g_null_atom, LowercaseIfNecessary(std::move(name)),
                      g_null_atom));
  }

  AttributeCollection attributes = GetElementData()->Attributes();
  wtf_size_t index = attributes.FindIndexHinted(name, hint);
  return std::make_pair(
      index,
      index != kNotFound
          ? attributes[index].GetName()
          : QualifiedName(g_null_atom, LowercaseIfNecessary(std::move(name)),
                          g_null_atom));
}

void Element::setAttribute(const QualifiedName& name,
                           const AtomicString& value) {
  SynchronizeAttribute(name);
  SetAttributeInternal(FindAttributeIndex(name), name, value,
                       AttributeModificationReason::kDirectly);
}

void Element::setAttribute(const QualifiedName& name,
                           const AtomicString& value,
                           ExceptionState& exception_state) {
  SynchronizeAttribute(name);

  AtomicString trusted_value(
      TrustedTypesCheckFor(ExpectedTrustedTypeForAttribute(name), value,
                           GetExecutionContext(), exception_state));
  if (exception_state.HadException())
    return;

  SetAttributeInternal(FindAttributeIndex(name), name, trusted_value,
                       AttributeModificationReason::kDirectly);
}

void Element::SetSynchronizedLazyAttribute(const QualifiedName& name,
                                           const AtomicString& value) {
  SetAttributeInternal(
      FindAttributeIndex(name), name, value,
      AttributeModificationReason::kBySynchronizationOfLazyAttribute);
}

void Element::SetAttributeHinted(AtomicString local_name,
                                 WTF::AtomicStringTable::WeakResult hint,
                                 String value,
                                 ExceptionState& exception_state) {
  if (!Document::IsValidName(local_name)) {
    exception_state.ThrowDOMException(
        DOMExceptionCode::kInvalidCharacterError,
        "'" + local_name + "' is not a valid attribute name.");
    return;
  }

  SynchronizeAttributeHinted(local_name, hint);
  wtf_size_t index;
  QualifiedName q_name = QualifiedName::Null();
  std::tie(index, q_name) =
      LookupAttributeQNameHinted(std::move(local_name), hint);

  AtomicString trusted_value(TrustedTypesCheckFor(
      ExpectedTrustedTypeForAttribute(q_name), std::move(value),
      GetExecutionContext(), exception_state));
  if (exception_state.HadException())
    return;

  SetAttributeInternal(index, q_name, trusted_value,
                       AttributeModificationReason::kDirectly);
}

void Element::SetAttributeHinted(AtomicString local_name,
                                 WTF::AtomicStringTable::WeakResult hint,
                                 const V8TrustedType* trusted_string,
                                 ExceptionState& exception_state) {
  if (!Document::IsValidName(local_name)) {
    exception_state.ThrowDOMException(
        DOMExceptionCode::kInvalidCharacterError,
        "'" + local_name + "' is not a valid attribute name.");
    return;
  }

  SynchronizeAttributeHinted(local_name, hint);
  wtf_size_t index;
  QualifiedName q_name = QualifiedName::Null();
  std::tie(index, q_name) =
      LookupAttributeQNameHinted(std::move(local_name), hint);
  AtomicString value(TrustedTypesCheckFor(
      ExpectedTrustedTypeForAttribute(q_name), trusted_string,
      GetExecutionContext(), exception_state));
  if (exception_state.HadException())
    return;
  SetAttributeInternal(index, q_name, value,
                       AttributeModificationReason::kDirectly);
}

wtf_size_t Element::FindAttributeIndex(const QualifiedName& name) {
  if (GetElementData())
    return GetElementData()->Attributes().FindIndex(name);
  return kNotFound;
}

ALWAYS_INLINE void Element::SetAttributeInternal(
    wtf_size_t index,
    const QualifiedName& name,
    const AtomicString& new_value,
    AttributeModificationReason reason) {
  if (new_value.IsNull()) {
    if (index != kNotFound)
      RemoveAttributeInternal(index, reason);
    return;
  }

  if (index == kNotFound) {
    AppendAttributeInternal(name, new_value, reason);
    return;
  }

  const Attribute& existing_attribute =
      GetElementData()->Attributes().at(index);
  AtomicString existing_attribute_value = existing_attribute.Value();
  QualifiedName existing_attribute_name = existing_attribute.GetName();

  if (reason !=
      AttributeModificationReason::kBySynchronizationOfLazyAttribute) {
    WillModifyAttribute(existing_attribute_name, existing_attribute_value,
                        new_value);
  }
  if (new_value != existing_attribute_value)
    EnsureUniqueElementData().Attributes().at(index).SetValue(new_value);
  if (reason !=
      AttributeModificationReason::kBySynchronizationOfLazyAttribute) {
    DidModifyAttribute(existing_attribute_name, existing_attribute_value,
                       new_value, reason);
  }
}

Attr* Element::setAttributeNode(Attr* attr_node,
                                ExceptionState& exception_state) {
  Attr* old_attr_node = AttrIfExists(attr_node->GetQualifiedName());
  if (old_attr_node == attr_node)
    return attr_node;  // This Attr is already attached to the element.

  // InUseAttributeError: Raised if node is an Attr that is already an attribute
  // of another Element object.  The DOM user must explicitly clone Attr nodes
  // to re-use them in other elements.
  if (attr_node->ownerElement()) {
    exception_state.ThrowDOMException(
        DOMExceptionCode::kInUseAttributeError,
        "The node provided is an attribute node that is already an attribute "
        "of another Element; attribute nodes must be explicitly cloned.");
    return nullptr;
  }

  if (!IsHTMLElement() && IsA<HTMLDocument>(attr_node->GetDocument()) &&
      attr_node->name() != attr_node->name().LowerASCII()) {
    UseCounter::Count(
        GetDocument(),
        WebFeature::
            kNonHTMLElementSetAttributeNodeFromHTMLDocumentNameNotLowercase);
  }

  SynchronizeAllAttributes();
  const UniqueElementData& element_data = EnsureUniqueElementData();

  AtomicString value(TrustedTypesCheckFor(
      ExpectedTrustedTypeForAttribute(attr_node->GetQualifiedName()),
      attr_node->value(), GetExecutionContext(), exception_state));
  if (exception_state.HadException())
    return nullptr;

  AttributeCollection attributes = element_data.Attributes();
  wtf_size_t index = attributes.FindIndex(attr_node->GetQualifiedName());
  AtomicString local_name;
  if (index != kNotFound) {
    const Attribute& attr = attributes[index];

    // If the name of the ElementData attribute doesn't
    // (case-sensitively) match that of the Attr node, record it
    // on the Attr so that it can correctly resolve the value on
    // the Element.
    if (!attr.GetName().Matches(attr_node->GetQualifiedName()))
      local_name = attr.LocalName();

    if (old_attr_node) {
      DetachAttrNodeFromElementWithValue(old_attr_node, attr.Value());
    } else {
      // FIXME: using attrNode's name rather than the
      // Attribute's for the replaced Attr is compatible with
      // all but Gecko (and, arguably, the DOM Level1 spec text.)
      // Consider switching.
      old_attr_node = MakeGarbageCollected<Attr>(
          GetDocument(), attr_node->GetQualifiedName(), attr.Value());
    }
  }

  SetAttributeInternal(index, attr_node->GetQualifiedName(), value,
                       AttributeModificationReason::kDirectly);

  attr_node->AttachToElement(this, local_name);
  GetTreeScope().AdoptIfNeeded(*attr_node);
  EnsureElementRareData().AddAttr(attr_node);

  return old_attr_node;
}

void Element::RemoveAttributeHinted(const AtomicString& name,
                                    WTF::AtomicStringTable::WeakResult hint) {
  if (!GetElementData())
    return;

  wtf_size_t index = GetElementData()->Attributes().FindIndexHinted(name, hint);
  if (index == kNotFound) {
    if (UNLIKELY(hint == html_names::kStyleAttr.LocalName()) &&
        GetElementData()->style_attribute_is_dirty() && IsStyledElement())
      RemoveAllInlineStyleProperties();
    return;
  }

  RemoveAttributeInternal(index, AttributeModificationReason::kDirectly);
}

bool Element::IsDocumentElement() const {
  return this == GetDocument().documentElement();
}

bool Element::IsReplacedElementRespectingCSSOverflow() const {
  // See https://github.com/w3c/csswg-drafts/issues/7144 for details on enabling
  // ink overflow for replaced elements.
  if (GetPseudoId() == kPseudoIdPageTransitionIncomingImage ||
      GetPseudoId() == kPseudoIdPageTransitionOutgoingImage)
    return true;

  if (!RuntimeEnabledFeatures::CSSOverflowForReplacedElementsEnabled())
    return false;

  return IsA<HTMLVideoElement>(this) || IsA<HTMLCanvasElement>(this) ||
         IsA<HTMLImageElement>(this) ||
         (IsA<SVGSVGElement>(this) &&
          To<SVGSVGElement>(this)->IsOutermostSVGSVGElement() &&
          !IsDocumentElement()) ||
         IsA<HTMLFrameOwnerElement>(this);
}

const ComputedStyle* Element::StyleForPositionFallback(unsigned index) {
  // @position-fallback style must be computed out of the main style recalc,
  // after the base style has been computed.
  DCHECK_GE(GetDocument().Lifecycle().GetState(),
            DocumentLifecycle::kStyleClean);
  const ComputedStyle* base_style = GetComputedStyle();
  if (!base_style)
    return nullptr;
  if (const ComputedStyle* cached_style =
          base_style->GetCachedPositionFallbackStyle(index))
    return cached_style;

  scoped_refptr<const ComputedStyle> style =
      GetDocument().GetStyleResolver().ResolvePositionFallbackStyle(*this,
                                                                    index);
  if (!style)
    return nullptr;
  return base_style->AddCachedPositionFallbackStyle(std::move(style), index);
}

}  // namespace blink
