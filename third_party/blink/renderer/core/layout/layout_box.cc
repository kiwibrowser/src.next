/*
 * Copyright (C) 1999 Lars Knoll (knoll@kde.org)
 *           (C) 1999 Antti Koivisto (koivisto@kde.org)
 *           (C) 2005 Allan Sandfeld Jensen (kde@carewolf.com)
 *           (C) 2005, 2006 Samuel Weinig (sam.weinig@gmail.com)
 * Copyright (C) 2005, 2006, 2007, 2008, 2009, 2010 Apple Inc.
 *               All rights reserved.
 * Copyright (C) 2013 Adobe Systems Incorporated. All rights reserved.
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

#include "third_party/blink/renderer/core/layout/layout_box.h"

#include <math.h>
#include <algorithm>
#include <utility>

#include "cc/input/scroll_snap_data.h"
#include "third_party/blink/public/mojom/scroll/scroll_into_view_params.mojom-blink.h"
#include "third_party/blink/public/platform/web_theme_engine.h"
#include "third_party/blink/public/strings/grit/blink_strings.h"
#include "third_party/blink/renderer/core/display_lock/display_lock_utilities.h"
#include "third_party/blink/renderer/core/dom/document.h"
#include "third_party/blink/renderer/core/dom/node_computed_style.h"
#include "third_party/blink/renderer/core/editing/editing_utilities.h"
#include "third_party/blink/renderer/core/editing/ime/input_method_controller.h"
#include "third_party/blink/renderer/core/editing/position_with_affinity.h"
#include "third_party/blink/renderer/core/frame/local_dom_window.h"
#include "third_party/blink/renderer/core/frame/local_frame.h"
#include "third_party/blink/renderer/core/frame/local_frame_client.h"
#include "third_party/blink/renderer/core/frame/local_frame_view.h"
#include "third_party/blink/renderer/core/frame/settings.h"
#include "third_party/blink/renderer/core/html/forms/html_input_element.h"
#include "third_party/blink/renderer/core/html/forms/html_opt_group_element.h"
#include "third_party/blink/renderer/core/html/forms/html_select_element.h"
#include "third_party/blink/renderer/core/html/forms/html_text_area_element.h"
#include "third_party/blink/renderer/core/html/html_div_element.h"
#include "third_party/blink/renderer/core/html/html_element.h"
#include "third_party/blink/renderer/core/html/html_frame_element_base.h"
#include "third_party/blink/renderer/core/html/shadow/shadow_element_names.h"
#include "third_party/blink/renderer/core/html/shadow/shadow_element_utils.h"
#include "third_party/blink/renderer/core/input/event_handler.h"
#include "third_party/blink/renderer/core/input_type_names.h"
#include "third_party/blink/renderer/core/layout/api/line_layout_block_flow.h"
#include "third_party/blink/renderer/core/layout/api/line_layout_box.h"
#include "third_party/blink/renderer/core/layout/box_layout_extra_input.h"
#include "third_party/blink/renderer/core/layout/custom_scrollbar.h"
#include "third_party/blink/renderer/core/layout/geometry/physical_rect.h"
#include "third_party/blink/renderer/core/layout/hit_test_result.h"
#include "third_party/blink/renderer/core/layout/layout_deprecated_flexible_box.h"
#include "third_party/blink/renderer/core/layout/layout_embedded_content.h"
#include "third_party/blink/renderer/core/layout/layout_fieldset.h"
#include "third_party/blink/renderer/core/layout/layout_file_upload_control.h"
#include "third_party/blink/renderer/core/layout/layout_flexible_box.h"
#include "third_party/blink/renderer/core/layout/layout_grid.h"
#include "third_party/blink/renderer/core/layout/layout_inline.h"
#include "third_party/blink/renderer/core/layout/layout_list_marker.h"
#include "third_party/blink/renderer/core/layout/layout_multi_column_flow_thread.h"
#include "third_party/blink/renderer/core/layout/layout_multi_column_spanner_placeholder.h"
#include "third_party/blink/renderer/core/layout/layout_object.h"
#include "third_party/blink/renderer/core/layout/layout_table_cell.h"
#include "third_party/blink/renderer/core/layout/layout_text_control.h"
#include "third_party/blink/renderer/core/layout/layout_view.h"
#include "third_party/blink/renderer/core/layout/ng/custom/custom_layout_child.h"
#include "third_party/blink/renderer/core/layout/ng/custom/layout_ng_custom.h"
#include "third_party/blink/renderer/core/layout/ng/custom/layout_worklet.h"
#include "third_party/blink/renderer/core/layout/ng/custom/layout_worklet_global_scope_proxy.h"
#include "third_party/blink/renderer/core/layout/ng/geometry/ng_box_strut.h"
#include "third_party/blink/renderer/core/layout/ng/inline/ng_inline_cursor.h"
#include "third_party/blink/renderer/core/layout/ng/layout_ng_fieldset.h"
#include "third_party/blink/renderer/core/layout/ng/legacy_layout_tree_walking.h"
#include "third_party/blink/renderer/core/layout/ng/ng_box_fragment.h"
#include "third_party/blink/renderer/core/layout/ng/ng_box_fragment_builder.h"
#include "third_party/blink/renderer/core/layout/ng/ng_constraint_space.h"
#include "third_party/blink/renderer/core/layout/ng/ng_disable_side_effects_scope.h"
#include "third_party/blink/renderer/core/layout/ng/ng_fragmentation_utils.h"
#include "third_party/blink/renderer/core/layout/ng/ng_layout_result.h"
#include "third_party/blink/renderer/core/layout/ng/ng_layout_utils.h"
#include "third_party/blink/renderer/core/layout/ng/ng_length_utils.h"
#include "third_party/blink/renderer/core/layout/ng/table/layout_ng_table_cell.h"
#include "third_party/blink/renderer/core/layout/shapes/shape_outside_info.h"
#include "third_party/blink/renderer/core/loader/resource/image_resource_content.h"
#include "third_party/blink/renderer/core/page/autoscroll_controller.h"
#include "third_party/blink/renderer/core/page/chrome_client.h"
#include "third_party/blink/renderer/core/page/page.h"
#include "third_party/blink/renderer/core/page/scrolling/snap_coordinator.h"
#include "third_party/blink/renderer/core/paint/box_paint_invalidator.h"
#include "third_party/blink/renderer/core/paint/box_painter.h"
#include "third_party/blink/renderer/core/paint/object_paint_invalidator.h"
#include "third_party/blink/renderer/core/paint/outline_painter.h"
#include "third_party/blink/renderer/core/paint/paint_layer.h"
#include "third_party/blink/renderer/core/paint/paint_layer_scrollable_area.h"
#include "third_party/blink/renderer/core/paint/rounded_border_geometry.h"
#include "third_party/blink/renderer/core/resize_observer/resize_observer_size.h"
#include "third_party/blink/renderer/core/scroll/scroll_into_view_util.h"
#include "third_party/blink/renderer/core/style/computed_style_base_constants.h"
#include "third_party/blink/renderer/core/style/shadow_list.h"
#include "third_party/blink/renderer/core/style/style_overflow_clip_margin.h"
#include "third_party/blink/renderer/platform/geometry/float_rounded_rect.h"
#include "third_party/blink/renderer/platform/geometry/layout_rect.h"
#include "third_party/blink/renderer/platform/geometry/layout_rect_outsets.h"
#include "third_party/blink/renderer/platform/geometry/length_functions.h"
#include "third_party/blink/renderer/platform/runtime_enabled_features.h"
#include "third_party/blink/renderer/platform/text/platform_locale.h"
#include "third_party/blink/renderer/platform/theme/web_theme_engine_helper.h"
#include "third_party/blink/renderer/platform/wtf/size_assertions.h"
#include "ui/gfx/geometry/quad_f.h"

namespace blink {

// Used by flexible boxes when flexing this element and by table cells.
typedef WTF::HashMap<const LayoutBox*, LayoutUnit> OverrideSizeMap;

// Size of border belt for autoscroll. When mouse pointer in border belt,
// autoscroll is started.
static const int kAutoscrollBeltSize = 20;
static const unsigned kBackgroundObscurationTestMaxDepth = 4;

struct SameSizeAsLayoutBox : public LayoutBoxModelObject {
  LayoutRect frame_rect;
  LayoutSize previous_size;
  LayoutUnit intrinsic_content_logical_height;
  LayoutRectOutsets margin_box_outsets;
  MinMaxSizes intrinsic_logical_widths;
  LayoutUnit intrinsic_logical_widths_initial_block_size;
  Member<void*> result;
  HeapVector<Member<const NGLayoutResult>, 1> layout_results;
  void* pointers[2];
  Member<void*> inline_box_wrapper;
  wtf_size_t first_fragment_item_index_;
  Member<void*> rare_data;
};

ASSERT_SIZE(LayoutBox, SameSizeAsLayoutBox);

namespace {

LayoutUnit TextAreaIntrinsicInlineSize(const HTMLTextAreaElement& textarea,
                                       const LayoutBox& box) {
  // <textarea>'s intrinsic inline-size always contains the scrollbar thickness
  // regardless of actual existence of a scrollbar.
  //
  // See |NGBlockLayoutAlgorithm::ComputeMinMaxSizes()| and |LayoutBlock::
  // ComputeIntrinsicLogicalWidths()|.
  return LayoutUnit(ceilf(LayoutTextControl::GetAvgCharWidth(box.StyleRef()) *
                          textarea.cols())) +
         LayoutTextControl::ScrollbarThickness(box);
}

LayoutUnit TextFieldIntrinsicInlineSize(const HTMLInputElement& input,
                                        const LayoutBox& box) {
  int factor;
  const bool includes_decoration = input.SizeShouldIncludeDecoration(factor);
  if (factor <= 0)
    factor = 20;

  const float char_width = LayoutTextControl::GetAvgCharWidth(box.StyleRef());
  float float_result = char_width * factor;

  float max_char_width = 0.f;
  const Font& font = box.StyleRef().GetFont();
  if (LayoutTextControl::HasValidAvgCharWidth(font))
    max_char_width = font.PrimaryFont()->MaxCharWidth();

  // For text inputs, IE adds some extra width.
  if (max_char_width > char_width)
    float_result += max_char_width - char_width;

  LayoutUnit result(ceilf(float_result));
  if (includes_decoration) {
    const auto* spin_button =
        To<HTMLElement>(input.UserAgentShadowRoot()->getElementById(
            shadow_element_names::kIdSpinButton));
    if (LayoutBox* spin_box =
            spin_button ? spin_button->GetLayoutBox() : nullptr) {
      const Length& logical_width = spin_box->StyleRef().LogicalWidth();
      result += spin_box->BorderAndPaddingLogicalWidth();
      // Since the width of spin_box is not calculated yet,
      // spin_box->LogicalWidth() returns 0. Use the computed logical
      // width instead.
      if (logical_width.IsPercent()) {
        if (logical_width.Value() != 100.f) {
          result +=
              result * logical_width.Value() / (100 - logical_width.Value());
        }
      } else {
        result += logical_width.Value();
      }
    }
  }
  return result;
}

LayoutUnit TextAreaIntrinsicBlockSize(const HTMLTextAreaElement& textarea,
                                      const LayoutBox& box) {
  const auto* inner_editor = textarea.InnerEditorElement();
  if (!inner_editor || !inner_editor->GetLayoutBox()) {
    const LayoutUnit line_height = box.LineHeight(
        true,
        box.StyleRef().IsHorizontalWritingMode() ? kHorizontalLine
                                                 : kVerticalLine,
        kPositionOfInteriorLineBoxes);

    return line_height * textarea.rows();
  }
  const LayoutBox& inner_box = *inner_editor->GetLayoutBox();
  const ComputedStyle& inner_style = inner_box.StyleRef();
  // We are able to have a horizontal scrollbar if the overflow style is
  // scroll, or if its auto and there's no word wrap.
  int scrollbar_thickness = 0;
  if (box.StyleRef().OverflowInlineDirection() == EOverflow::kScroll ||
      (box.StyleRef().OverflowInlineDirection() == EOverflow::kAuto &&
       inner_style.OverflowWrap() == EOverflowWrap::kNormal))
    scrollbar_thickness = LayoutTextControl::ScrollbarThickness(box);
  return inner_box.LineHeight(true,
                              inner_style.IsHorizontalWritingMode()
                                  ? kHorizontalLine
                                  : kVerticalLine,
                              kPositionOfInteriorLineBoxes) *
             textarea.rows() +
         scrollbar_thickness;
}

LayoutUnit TextFieldIntrinsicBlockSize(const HTMLInputElement& input,
                                       const LayoutBox& box) {
  const auto* inner_editor = input.InnerEditorElement();
  // inner_editor's LayoutBox can be nullptr because web authors can set
  // display:none to ::-webkit-textfield-decoration-container element.
  const LayoutBox& target_box = (inner_editor && inner_editor->GetLayoutBox())
                                    ? *inner_editor->GetLayoutBox()
                                    : box;
  return target_box.LineHeight(true,
                               target_box.StyleRef().IsHorizontalWritingMode()
                                   ? kHorizontalLine
                                   : kVerticalLine,
                               kPositionOfInteriorLineBoxes);
}

LayoutUnit FileUploadControlIntrinsicInlineSize(const HTMLInputElement& input,
                                                const LayoutBox& box) {
  // Figure out how big the filename space needs to be for a given number of
  // characters (using "0" as the nominal character).
  constexpr int kDefaultWidthNumChars = 34;
  constexpr UChar kCharacter = '0';
  const String character_as_string = String(&kCharacter, 1u);
  const Font& font = box.StyleRef().GetFont();
  const float min_default_label_width =
      kDefaultWidthNumChars *
      font.Width(ConstructTextRun(font, character_as_string, box.StyleRef(),
                                  TextRun::kAllowTrailingExpansion));

  const String label =
      input.GetLocale().QueryString(IDS_FORM_FILE_NO_FILE_LABEL);
  float default_label_width = font.Width(ConstructTextRun(
      font, label, box.StyleRef(), TextRun::kAllowTrailingExpansion));
  if (HTMLInputElement* button = input.UploadButton()) {
    if (LayoutObject* button_layout_object = button->GetLayoutObject()) {
      default_label_width +=
          button_layout_object->PreferredLogicalWidths().max_size +
          LayoutFileUploadControl::kAfterButtonSpacing;
    }
  }
  return LayoutUnit(
      ceilf(std::max(min_default_label_width, default_label_width)));
}

LayoutUnit SliderIntrinsicInlineSize(const LayoutBox& box) {
  constexpr int kDefaultTrackLength = 129;
  return LayoutUnit(kDefaultTrackLength * box.StyleRef().EffectiveZoom());
}

LogicalSize ThemePartIntrinsicSize(const LayoutBox& box,
                                   WebThemeEngine::Part part) {
  const auto& style = box.StyleRef();
  PhysicalSize size(
      WebThemeEngineHelper::GetNativeThemeEngine()->GetSize(part));
  size.Scale(style.EffectiveZoom());
  return size.ConvertToLogical(style.GetWritingMode());
}

LayoutUnit ListBoxDefaultItemHeight(const LayoutBox& box) {
  constexpr int kDefaultPaddingBottom = 1;

  const SimpleFontData* font_data = box.StyleRef().GetFont().PrimaryFont();
  if (!font_data)
    return LayoutUnit();
  return LayoutUnit(font_data->GetFontMetrics().Height() +
                    kDefaultPaddingBottom);
}

// TODO(crbug.com/1040826): This function is written in LayoutObject API
// so that this works in both of the legacy layout and LayoutNG. We
// should have LayoutNG-specific code.
LayoutUnit ListBoxItemHeight(const HTMLSelectElement& select,
                             const LayoutBox& box) {
  const auto& items = select.GetListItems();
  if (items.IsEmpty() || box.ShouldApplySizeContainment())
    return ListBoxDefaultItemHeight(box);

  LayoutUnit max_height;
  for (Element* element : items) {
    if (auto* optgroup = DynamicTo<HTMLOptGroupElement>(element))
      element = &optgroup->OptGroupLabelElement();
    LayoutUnit item_height;
    if (auto* layout_box = element->GetLayoutBox())
      item_height = layout_box->Size().Height();
    else
      item_height = ListBoxDefaultItemHeight(box);
    max_height = std::max(max_height, item_height);
  }
  return max_height;
}

LayoutUnit MenuListIntrinsicInlineSize(const HTMLSelectElement& select,
                                       const LayoutBox& box) {
  const ComputedStyle& style = box.StyleRef();
  float max_option_width = 0;
  if (!box.ShouldApplySizeContainment()) {
    for (auto* const option : select.GetOptionList()) {
      String text = option->TextIndentedToRespectGroupLabel();
      style.ApplyTextTransform(&text);
      // We apply SELECT's style, not OPTION's style because max_option_width is
      // used to determine intrinsic width of the menulist box.
      TextRun text_run = ConstructTextRun(style.GetFont(), text, style);
      max_option_width =
          std::max(max_option_width, style.GetFont().Width(text_run));
    }
  }

  LayoutTheme& theme = LayoutTheme::GetTheme();
  int paddings = theme.PopupInternalPaddingStart(style) +
                 theme.PopupInternalPaddingEnd(box.GetFrame(), style);
  return LayoutUnit(ceilf(max_option_width)) + LayoutUnit(paddings);
}

LayoutUnit MenuListIntrinsicBlockSize(const HTMLSelectElement& select,
                                      const LayoutBox& box) {
  if (!box.StyleRef().HasEffectiveAppearance())
    return kIndefiniteSize;
  const SimpleFontData* font_data = box.StyleRef().GetFont().PrimaryFont();
  DCHECK(font_data);
  const LayoutBox* inner_box = select.InnerElement().GetLayoutBox();
  return (font_data ? font_data->GetFontMetrics().Height() : 0) +
         (inner_box ? inner_box->BorderAndPaddingLogicalHeight()
                    : LayoutUnit());
}

#if DCHECK_IS_ON()
void CheckDidAddFragment(const LayoutBox& box,
                         const NGPhysicalBoxFragment& new_fragment,
                         wtf_size_t new_fragment_index = kNotFound) {
  // If |HasFragmentItems|, |ChildrenInline()| should be true.
  // |HasFragmentItems| uses this condition to optimize .
  if (new_fragment.HasItems())
    DCHECK(box.ChildrenInline());

  wtf_size_t index = 0;
  for (const NGPhysicalBoxFragment& fragment : box.PhysicalFragments()) {
    DCHECK_EQ(fragment.IsFirstForNode(), index == 0);
    if (const NGFragmentItems* fragment_items = fragment.Items())
      fragment_items->CheckAllItemsAreValid();
    // Don't check past the fragment just added. Those entries may be invalid at
    // this point.
    if (index == new_fragment_index)
      break;
    ++index;
  }
}
#else
inline void CheckDidAddFragment(const LayoutBox& box,
                                const NGPhysicalBoxFragment& fragment,
                                wtf_size_t new_fragment_index = kNotFound) {}
#endif

// Applies the overflow clip to |result|. For any axis that is clipped, |result|
// is reset to |no_overflow_rect|. If neither axis is clipped, nothing is
// changed.
void ApplyOverflowClip(OverflowClipAxes overflow_clip_axes,
                       const LayoutRect& no_overflow_rect,
                       LayoutRect& result) {
  if (overflow_clip_axes & kOverflowClipX) {
    result.SetX(no_overflow_rect.X());
    result.SetWidth(no_overflow_rect.Width());
  }
  if (overflow_clip_axes & kOverflowClipY) {
    result.SetY(no_overflow_rect.Y());
    result.SetHeight(no_overflow_rect.Height());
  }
}

int HypotheticalScrollbarThickness(const LayoutBox& box,
                                   ScrollbarOrientation scrollbar_orientation,
                                   bool should_include_overlay_thickness) {
  box.CheckIsNotDestroyed();

  if (PaintLayerScrollableArea* scrollable_area = box.GetScrollableArea()) {
    return scrollable_area->HypotheticalScrollbarThickness(
        scrollbar_orientation, should_include_overlay_thickness);
  } else {
    Page* page = box.GetFrame()->GetPage();
    ScrollbarTheme& theme = page->GetScrollbarTheme();

    if (theme.UsesOverlayScrollbars() && !should_include_overlay_thickness) {
      return 0;
    } else {
      ChromeClient& chrome_client = page->GetChromeClient();
      Document& document = box.GetDocument();
      float scale_from_dip =
          chrome_client.WindowToViewportScalar(document.GetFrame(), 1.0f);
      return theme.ScrollbarThickness(scale_from_dip,
                                      box.StyleRef().ScrollbarWidth());
    }
  }
}

}  // namespace

BoxLayoutExtraInput::BoxLayoutExtraInput(LayoutBox& layout_box)
    : box(&layout_box) {
  box->SetBoxLayoutExtraInput(this);
}

BoxLayoutExtraInput::~BoxLayoutExtraInput() {
  box->SetBoxLayoutExtraInput(nullptr);
}

void BoxLayoutExtraInput::Trace(Visitor* visitor) const {
  visitor->Trace(box);
}

LayoutBoxRareData::LayoutBoxRareData()
    : spanner_placeholder_(nullptr),
      override_logical_width_(-1),
      override_logical_height_(-1),
      // TODO(rego): We should store these based on physical direction.
      has_override_containing_block_content_logical_width_(false),
      has_override_containing_block_content_logical_height_(false),
      has_override_percentage_resolution_block_size_(false),
      has_previous_content_box_rect_(false),
      percent_height_container_(nullptr),
      snap_container_(nullptr) {}

void LayoutBoxRareData::Trace(Visitor* visitor) const {
  visitor->Trace(spanner_placeholder_);
  visitor->Trace(percent_height_container_);
  visitor->Trace(snap_container_);
  visitor->Trace(snap_areas_);
  visitor->Trace(layout_child_);
}

LayoutBox::LayoutBox(ContainerNode* node)
    : LayoutBoxModelObject(node),
      intrinsic_content_logical_height_(-1),
      intrinsic_logical_widths_initial_block_size_(LayoutUnit::Min()),
      inline_box_wrapper_(nullptr) {
  SetIsBox();
  if (blink::IsA<HTMLLegendElement>(node))
    SetIsHTMLLegendElement();
}

void LayoutBox::Trace(Visitor* visitor) const {
  visitor->Trace(measure_result_);
  visitor->Trace(layout_results_);
  visitor->Trace(inline_box_wrapper_);
  visitor->Trace(rare_data_);
  LayoutBoxModelObject::Trace(visitor);
}

LayoutBox::~LayoutBox() = default;

PaintLayerType LayoutBox::LayerTypeRequired() const {
  NOT_DESTROYED();
  if (IsStacked() || HasHiddenBackface() ||
      (StyleRef().SpecifiesColumns() && !IsLayoutNGObject()))
    return kNormalPaintLayer;

  if (HasNonVisibleOverflow())
    return kOverflowClipPaintLayer;

  return kNoPaintLayer;
}

void LayoutBox::WillBeDestroyed() {
  NOT_DESTROYED();
  ClearOverrideSize();
  ClearOverrideContainingBlockContentSize();
  ClearOverridePercentageResolutionBlockSize();

  if (IsOutOfFlowPositioned())
    LayoutBlock::RemovePositionedObject(this);

  RemoveFromPercentHeightContainer();
  if (IsOrthogonalWritingModeRoot() && !DocumentBeingDestroyed())
    UnmarkOrthogonalWritingModeRoot();

  ShapeOutsideInfo::RemoveInfo(*this);

  if (!DocumentBeingDestroyed()) {
    DisassociatePhysicalFragments();
    GetDocument()
        .GetFrame()
        ->GetInputMethodController()
        .LayoutObjectWillBeDestroyed(*this);
    if (IsFixedPositioned())
      GetFrameView()->RemoveFixedPositionObject(*this);
  }

  SetSnapContainer(nullptr);
  LayoutBoxModelObject::WillBeDestroyed();
}

void LayoutBox::DisassociatePhysicalFragments() {
  if (FirstInlineFragmentItemIndex()) {
    NGFragmentItems::LayoutObjectWillBeDestroyed(*this);
    ClearFirstInlineFragmentItemIndex();
  }
  if (measure_result_)
    measure_result_->PhysicalFragment().LayoutObjectWillBeDestroyed();
  for (auto result : layout_results_)
    result->PhysicalFragment().LayoutObjectWillBeDestroyed();
}

void LayoutBox::InsertedIntoTree() {
  NOT_DESTROYED();
  LayoutBoxModelObject::InsertedIntoTree();
  AddScrollSnapMapping();
  AddCustomLayoutChildIfNeeded();

  if (IsOrthogonalWritingModeRoot())
    MarkOrthogonalWritingModeRoot();
}

void LayoutBox::WillBeRemovedFromTree() {
  NOT_DESTROYED();
  if (!DocumentBeingDestroyed() && IsOrthogonalWritingModeRoot())
    UnmarkOrthogonalWritingModeRoot();

  ClearCustomLayoutChild();
  ClearScrollSnapMapping();
  LayoutBoxModelObject::WillBeRemovedFromTree();
}

void LayoutBox::RemoveFloatingOrPositionedChildFromBlockLists() {
  NOT_DESTROYED();
  DCHECK(IsFloatingOrOutOfFlowPositioned());

  if (DocumentBeingDestroyed())
    return;

  if (IsFloating()) {
    LayoutBlockFlow* parent_block_flow = nullptr;
    for (LayoutObject* curr = Parent(); curr; curr = curr->Parent()) {
      auto* curr_block_flow = DynamicTo<LayoutBlockFlow>(curr);
      if (curr_block_flow) {
        if (!parent_block_flow || curr_block_flow->ContainsFloat(this))
          parent_block_flow = curr_block_flow;
      }
    }

    if (parent_block_flow) {
      parent_block_flow->MarkSiblingsWithFloatsForLayout(this);
      parent_block_flow->MarkAllDescendantsWithFloatsForLayout(this, false);
    }
  }

  if (IsOutOfFlowPositioned())
    LayoutBlock::RemovePositionedObject(this);
}

void LayoutBox::StyleWillChange(StyleDifference diff,
                                const ComputedStyle& new_style) {
  NOT_DESTROYED();
  const ComputedStyle* old_style = Style();
  if (old_style) {
    LayoutFlowThread* flow_thread = FlowThreadContainingBlock();
    if (flow_thread && flow_thread != this)
      flow_thread->FlowThreadDescendantStyleWillChange(this, diff, new_style);

    // The background of the root element or the body element could propagate up
    // to the canvas. Just dirty the entire canvas when our style changes
    // substantially.
    if ((diff.NeedsPaintInvalidation() || diff.NeedsLayout()) && GetNode() &&
        (IsDocumentElement() || IsA<HTMLBodyElement>(*GetNode()))) {
      View()->SetShouldDoFullPaintInvalidation();
    }

    // When a layout hint happens and an object's position style changes, we
    // have to do a layout to dirty the layout tree using the old position
    // value now.
    if (diff.NeedsFullLayout() && Parent()) {
      bool will_move_out_of_ifc = false;
      if (old_style->GetPosition() != new_style.GetPosition()) {
        if (!old_style->HasOutOfFlowPosition() &&
            new_style.HasOutOfFlowPosition()) {
          // We're about to go out of flow. Before that takes place, we need to
          // mark the current containing block chain for preferred widths
          // recalculation.
          SetNeedsLayoutAndIntrinsicWidthsRecalc(
              layout_invalidation_reason::kStyleChange);

          // Grid placement is different for out-of-flow elements, so if the
          // containing block is a grid, dirty the grid's placement. The
          // converse (going from out of flow to in flow) is handled in
          // LayoutBox::UpdateGridPositionAfterStyleChange.
          LayoutBlock* containing_block = ContainingBlock();
          if (containing_block && containing_block->IsLayoutNGGrid())
            containing_block->SetGridPlacementDirty(true);

          // Out of flow are not part of |NGFragmentItems|, and that further
          // changes including destruction cannot be tracked. We need to mark it
          // is moved out from this IFC.
          will_move_out_of_ifc = true;
        } else {
          MarkContainerChainForLayout();
        }

        if (old_style->GetPosition() == EPosition::kStatic)
          SetShouldDoFullPaintInvalidation();
        else if (new_style.HasOutOfFlowPosition())
          Parent()->SetChildNeedsLayout();
        if (IsFloating() && !IsOutOfFlowPositioned() &&
            new_style.HasOutOfFlowPosition())
          RemoveFloatingOrPositionedChildFromBlockLists();
      }

      bool will_become_inflow = false;
      if ((old_style->IsFloating() || old_style->HasOutOfFlowPosition()) &&
          !new_style.IsFloating() && !new_style.HasOutOfFlowPosition()) {
        // As a float or OOF, this object may have been part of an inline
        // formatting context, but that's definitely no longer the case.
        will_become_inflow = true;
        will_move_out_of_ifc = true;
      }

      if (will_move_out_of_ifc && FirstInlineFragmentItemIndex()) {
        NGFragmentItems::LayoutObjectWillBeMoved(*this);
        ClearFirstInlineFragmentItemIndex();
      }
      if (will_become_inflow)
        SetIsInLayoutNGInlineFormattingContext(false);
    }
    // FIXME: This branch runs when !oldStyle, which means that layout was never
    // called so what's the point in invalidating the whole view that we never
    // painted?
  } else if (IsBody()) {
    View()->SetShouldDoFullPaintInvalidation();
  }

  LayoutBoxModelObject::StyleWillChange(diff, new_style);
}

void LayoutBox::StyleDidChange(StyleDifference diff,
                               const ComputedStyle* old_style) {
  NOT_DESTROYED();
  // Horizontal writing mode definition is updated in LayoutBoxModelObject::
  // updateFromStyle, (as part of the LayoutBoxModelObject::styleDidChange call
  // below). So, we can safely cache the horizontal writing mode value before
  // style change here.
  bool old_horizontal_writing_mode = IsHorizontalWritingMode();

  LayoutBoxModelObject::StyleDidChange(diff, old_style);

  // Reflection works through PaintLayer. Some child classes e.g. LayoutSVGBlock
  // don't create layers and ignore reflections.
  if (HasReflection() && !HasLayer())
    SetHasReflection(false);

  auto* parent_flow_block = DynamicTo<LayoutBlockFlow>(Parent());
  if (IsFloatingOrOutOfFlowPositioned() && old_style &&
      !old_style->IsFloating() && !old_style->HasOutOfFlowPosition() &&
      parent_flow_block)
    parent_flow_block->ChildBecameFloatingOrOutOfFlow(this);

  const ComputedStyle& new_style = StyleRef();
  if (NeedsLayout() && old_style)
    RemoveFromPercentHeightContainer();

  if (old_horizontal_writing_mode != IsHorizontalWritingMode()) {
    if (old_style) {
      if (IsOrthogonalWritingModeRoot())
        MarkOrthogonalWritingModeRoot();
      else
        UnmarkOrthogonalWritingModeRoot();
    }

    ClearPercentHeightDescendants();
  }

  SetOverflowClipAxes(ComputeOverflowClipAxes());

  // If our zoom factor changes and we have a defined scrollLeft/Top, we need to
  // adjust that value into the new zoomed coordinate space.  Note that the new
  // scroll offset may be outside the normal min/max range of the scrollable
  // area, which is weird but OK, because the scrollable area will update its
  // min/max in updateAfterLayout().
  if (IsScrollContainer() && old_style &&
      old_style->EffectiveZoom() != new_style.EffectiveZoom()) {
    PaintLayerScrollableArea* scrollable_area = GetScrollableArea();
    DCHECK(scrollable_area);
    // We use GetScrollOffset() rather than ScrollPosition(), because scroll
    // offset is the distance from the beginning of flow for the box, which is
    // the dimension we want to preserve.
    ScrollOffset offset = scrollable_area->GetScrollOffset();
    if (!offset.IsZero()) {
      offset.Scale(new_style.EffectiveZoom() / old_style->EffectiveZoom());
      scrollable_area->SetScrollOffsetUnconditionally(offset);
    }
  }

  if (old_style && old_style->IsScrollContainer() != IsScrollContainer()) {
    if (auto* layer = EnclosingLayer())
      layer->ScrollContainerStatusChanged();
  }

  UpdateShapeOutsideInfoAfterStyleChange(*Style(), old_style);
  UpdateGridPositionAfterStyleChange(old_style);

  // When we're no longer a flex item because we're now absolutely positioned,
  // we need to clear the override size so we're not affected by it anymore.
  // This technically covers too many cases (even when out-of-flow did not
  // change) but that should be harmless.
  if (IsOutOfFlowPositioned() && Parent() &&
      Parent()->StyleRef().IsDisplayFlexibleOrGridBox())
    ClearOverrideSize();

  UpdateBackgroundAttachmentFixedStatusAfterStyleChange();

  if (old_style) {
    // Regular column content (i.e. non-spanners) have a hook into the flow
    // thread machinery before (StyleWillChange()) and after (here in
    // StyleDidChange()) the style has changed. Column spanners, on the other
    // hand, only have a hook here. The LayoutMultiColumnSpannerPlaceholder code
    // will do all the necessary things, including removing it as a spanner, if
    // it should no longer be one. Therefore, make sure that we skip
    // FlowThreadDescendantStyleDidChange() in such cases, as that might trigger
    // a duplicate flow thread insertion notification, if the spanner no longer
    // is a spanner.
    if (LayoutMultiColumnSpannerPlaceholder* placeholder =
            SpannerPlaceholder()) {
      placeholder->LayoutObjectInFlowThreadStyleDidChange(old_style);
    } else if (LayoutFlowThread* flow_thread = FlowThreadContainingBlock()) {
      if (flow_thread != this)
        flow_thread->FlowThreadDescendantStyleDidChange(this, diff, *old_style);
    }

    UpdateScrollSnapMappingAfterStyleChange(*old_style);

    if (ShouldClipOverflowAlongEitherAxis()) {
      // The overflow clip paint property depends on border sizes through
      // overflowClipRect(), and border radii, so we update properties on
      // border size or radii change.
      //
      // For some controls, it depends on paddings.
      if (!old_style->BorderSizeEquals(new_style) ||
          !old_style->RadiiEqual(new_style) ||
          (HasControlClip() && !old_style->PaddingEqual(new_style))) {
        SetNeedsPaintPropertyUpdate();
      }
    }

    if (old_style->OverscrollBehaviorX() != new_style.OverscrollBehaviorX() ||
        old_style->OverscrollBehaviorY() != new_style.OverscrollBehaviorY()) {
      SetNeedsPaintPropertyUpdate();
    }

    if (old_style->OverflowClipMargin() != new_style.OverflowClipMargin())
      SetNeedsPaintPropertyUpdate();

    if (IsInLayoutNGInlineFormattingContext() && IsAtomicInlineLevel() &&
        old_style->Direction() != new_style.Direction()) {
      SetNeedsCollectInlines();
    }
  }

  if (LocalFrameView* frame_view = View()->GetFrameView()) {
    bool new_style_is_fixed_position =
        StyleRef().GetPosition() == EPosition::kFixed;
    bool old_style_is_fixed_position =
        old_style && old_style->GetPosition() == EPosition::kFixed;
    if (new_style_is_fixed_position != old_style_is_fixed_position) {
      if (new_style_is_fixed_position && Layer())
        frame_view->AddFixedPositionObject(*this);
      else
        frame_view->RemoveFixedPositionObject(*this);
    }
  }

  // Update the script style map, from the new computed style.
  if (IsCustomItem())
    GetCustomLayoutChild()->styleMap()->UpdateStyle(GetDocument(), StyleRef());

  if (diff.NeedsPaintInvalidation()) {
    if (const AtomicString& old_anchor_scroll =
            old_style ? old_style->AnchorScroll() : g_null_atom;
        StyleRef().AnchorScroll() != old_anchor_scroll) {
      SetNeedsPaintPropertyUpdate();
    }
  }

  // Non-atomic inlines should be LayoutInline or LayoutText, not LayoutBox.
  DCHECK(!IsInline() || IsAtomicInlineLevel());
}

void LayoutBox::UpdateBackgroundAttachmentFixedStatusAfterStyleChange() {
  NOT_DESTROYED();
  if (!GetFrameView())
    return;

  SetIsBackgroundAttachmentFixedObject(
      !BackgroundTransfersToView() &&
      StyleRef().HasFixedAttachmentBackgroundImage());
}

void LayoutBox::UpdateShapeOutsideInfoAfterStyleChange(
    const ComputedStyle& style,
    const ComputedStyle* old_style) {
  NOT_DESTROYED();
  const ShapeValue* shape_outside = style.ShapeOutside();
  const ShapeValue* old_shape_outside =
      old_style ? old_style->ShapeOutside()
                : ComputedStyleInitialValues::InitialShapeOutside();

  const Length& shape_margin = style.ShapeMargin();
  Length old_shape_margin =
      old_style ? old_style->ShapeMargin()
                : ComputedStyleInitialValues::InitialShapeMargin();

  float shape_image_threshold = style.ShapeImageThreshold();
  float old_shape_image_threshold =
      old_style ? old_style->ShapeImageThreshold()
                : ComputedStyleInitialValues::InitialShapeImageThreshold();

  // FIXME: A future optimization would do a deep comparison for equality. (bug
  // 100811)
  if (shape_outside == old_shape_outside && shape_margin == old_shape_margin &&
      shape_image_threshold == old_shape_image_threshold)
    return;

  if (!shape_outside)
    ShapeOutsideInfo::RemoveInfo(*this);
  else
    ShapeOutsideInfo::EnsureInfo(*this).MarkShapeAsDirty();

  if (shape_outside || shape_outside != old_shape_outside)
    MarkShapeOutsideDependentsForLayout();
}

namespace {

bool GridStyleChanged(const ComputedStyle* old_style,
                      const ComputedStyle& current_style) {
  return old_style->GridColumnStart() != current_style.GridColumnStart() ||
         old_style->GridColumnEnd() != current_style.GridColumnEnd() ||
         old_style->GridRowStart() != current_style.GridRowStart() ||
         old_style->GridRowEnd() != current_style.GridRowEnd() ||
         old_style->Order() != current_style.Order() ||
         old_style->HasOutOfFlowPosition() !=
             current_style.HasOutOfFlowPosition();
}

bool AlignmentChanged(const ComputedStyle* old_style,
                      const ComputedStyle& current_style) {
  return old_style->AlignSelfPosition() != current_style.AlignSelfPosition() ||
         old_style->JustifySelfPosition() !=
             current_style.JustifySelfPosition();
}

}  // namespace

void LayoutBox::UpdateGridPositionAfterStyleChange(
    const ComputedStyle* old_style) {
  NOT_DESTROYED();

  if (!old_style)
    return;

  LayoutObject* parent = Parent();
  const bool was_out_of_flow = old_style->HasOutOfFlowPosition();
  const bool is_out_of_flow = StyleRef().HasOutOfFlowPosition();
  if (parent && parent->IsLayoutGrid() &&
      GridStyleChanged(old_style, StyleRef())) {
    // Positioned items don't participate on the layout of the grid,
    // so we don't need to mark the grid as dirty if they change positions.
    if (was_out_of_flow && is_out_of_flow)
      return;

    // It should be possible to not dirty the grid in some cases (like moving an
    // explicitly placed grid item).
    // For now, it's more simple to just always recompute the grid.
    To<LayoutGrid>(Parent())->DirtyGrid();
    return;
  }

  LayoutBlock* containing_block = ContainingBlock();
  if ((containing_block && containing_block->IsLayoutNGGrid()) &&
      GridStyleChanged(old_style, StyleRef())) {
    // Out-of-flow items do not impact grid placement.
    // TODO(kschmi): Scope this so that it only dirties the grid when track
    // sizing depends on grid item sizes.
    if (!was_out_of_flow || !is_out_of_flow)
      containing_block->SetGridPlacementDirty(true);

    // For out-of-flow elements with grid container as containing block, we need
    // to run the entire algorithm to place and size them correctly. As a
    // result, we trigger a full layout for GridNG.
    if (is_out_of_flow) {
      containing_block->SetNeedsLayout(layout_invalidation_reason::kGridChanged,
                                       kMarkContainerChain);
    }
  }

  // GridNG computes static positions for out-of-flow elements at layout time,
  // with alignment offsets baked in. So if alignment changes, we need to
  // schedule a layout.
  if (is_out_of_flow && AlignmentChanged(old_style, StyleRef())) {
    LayoutObject* grid_ng_ancestor = nullptr;
    if (containing_block && containing_block->IsLayoutNGGrid())
      grid_ng_ancestor = containing_block;
    else if (parent && parent->IsLayoutNGGrid())
      grid_ng_ancestor = parent;

    if (grid_ng_ancestor) {
      grid_ng_ancestor->SetNeedsLayout(layout_invalidation_reason::kGridChanged,
                                       kMarkContainerChain);
    }
  }
}

void LayoutBox::UpdateScrollSnapMappingAfterStyleChange(
    const ComputedStyle& old_style) {
  NOT_DESTROYED();
  DCHECK(Style());
  SnapCoordinator& snap_coordinator = GetDocument().GetSnapCoordinator();
  // scroll-snap-type and scroll-padding invalidate the snap container.
  if (old_style.GetScrollSnapType() != StyleRef().GetScrollSnapType() ||
      old_style.ScrollPaddingBottom() != StyleRef().ScrollPaddingBottom() ||
      old_style.ScrollPaddingLeft() != StyleRef().ScrollPaddingLeft() ||
      old_style.ScrollPaddingTop() != StyleRef().ScrollPaddingTop() ||
      old_style.ScrollPaddingRight() != StyleRef().ScrollPaddingRight()) {
    snap_coordinator.SnapContainerDidChange(*this);
  }

  // scroll-snap-align, scroll-snap-stop and scroll-margin invalidate the snap
  // area.
  if (old_style.GetScrollSnapAlign() != StyleRef().GetScrollSnapAlign() ||
      old_style.ScrollSnapStop() != StyleRef().ScrollSnapStop() ||
      old_style.ScrollMarginBottom() != StyleRef().ScrollMarginBottom() ||
      old_style.ScrollMarginLeft() != StyleRef().ScrollMarginLeft() ||
      old_style.ScrollMarginTop() != StyleRef().ScrollMarginTop() ||
      old_style.ScrollMarginRight() != StyleRef().ScrollMarginRight())
    snap_coordinator.SnapAreaDidChange(*this, StyleRef().GetScrollSnapAlign());

  // Transform invalidates the snap area.
  if (old_style.Transform() != StyleRef().Transform())
    snap_coordinator.SnapAreaDidChange(*this, StyleRef().GetScrollSnapAlign());
}

void LayoutBox::AddScrollSnapMapping() {
  NOT_DESTROYED();
  SnapCoordinator& snap_coordinator = GetDocument().GetSnapCoordinator();
  snap_coordinator.SnapAreaDidChange(*this, Style()->GetScrollSnapAlign());
}

void LayoutBox::ClearScrollSnapMapping() {
  NOT_DESTROYED();
  SnapCoordinator& snap_coordinator = GetDocument().GetSnapCoordinator();
  snap_coordinator.SnapAreaDidChange(*this, cc::ScrollSnapAlign());
}

void LayoutBox::UpdateFromStyle() {
  NOT_DESTROYED();
  LayoutBoxModelObject::UpdateFromStyle();

  const ComputedStyle& style_to_use = StyleRef();
  SetFloating(style_to_use.IsFloating() && !IsOutOfFlowPositioned() &&
              !style_to_use.IsFlexOrGridItem());
  SetHasTransformRelatedProperty(
      IsSVGChild() ? style_to_use.HasTransformRelatedPropertyForSVG()
                   : style_to_use.HasTransformRelatedProperty());
  SetHasReflection(style_to_use.BoxReflect());
  // LayoutTable and LayoutTableCell will overwrite this flag if needed.
  SetHasNonCollapsedBorderDecoration(style_to_use.HasBorderDecoration());

  bool should_clip_overflow = (!StyleRef().IsOverflowVisibleAlongBothAxes() ||
                               ShouldApplyPaintContainment()) &&
                              RespectsCSSOverflow();
  if (should_clip_overflow != HasNonVisibleOverflow()) {
    // The overflow clip paint property depends on whether overflow clip is
    // present so we need to update paint properties if this changes.
    SetNeedsPaintPropertyUpdate();
    if (Layer())
      Layer()->SetNeedsCompositingInputsUpdate();
  }
  SetHasNonVisibleOverflow(should_clip_overflow);
}

void LayoutBox::LayoutSubtreeRoot() {
  NOT_DESTROYED();
  if (RuntimeEnabledFeatures::LayoutNGEnabled() && !IsLayoutNGObject() &&
      GetCachedLayoutResult()) {
    // If this object is laid out by the legacy engine, while its containing
    // block is laid out by NG, it means that we normally (when laying out
    // starting at the real root, i.e. LayoutView) enter layout of this object
    // from NG code. This takes care of setting up a BoxLayoutExtraInput
    // structure, which makes legacy layout behave when managed by NG. Make a
    // short detour via NG just to set things up to re-enter legacy layout
    // correctly.
    DCHECK_EQ(PhysicalFragmentCount(), 1u);
    LayoutPoint old_location = Location();

    // Make a copy of the cached constraint space, since we'll overwrite the
    // layout result object as part of performing layout.
    auto constraint_space =
        GetCachedLayoutResult()->GetConstraintSpaceForCaching();

    NGBlockNode(this).Layout(constraint_space);

    // Restore the old location. While it's usually the job of the containing
    // block to position its children, out-of-flow positioned objects set
    // their own position, which could be wrong in this case.
    SetLocation(old_location);
  } else {
    UpdateLayout();
  }

  GetDocument().GetFrame()->GetInputMethodController().DidLayoutSubtree(*this);
}

void LayoutBox::UpdateLayout() {
  NOT_DESTROYED();
  DCHECK(NeedsLayout());

  if (ChildLayoutBlockedByDisplayLock())
    return;

  LayoutObject* child = SlowFirstChild();
  if (!child) {
    ClearNeedsLayout();
    return;
  }

  LayoutState state(*this);
  while (child) {
    child->LayoutIfNeeded();
    DCHECK(!child->NeedsLayout());
    child = child->NextSibling();
  }
  UpdateAfterLayout();
  ClearNeedsLayout();
  NotifyDisplayLockDidLayoutChildren();
}

// ClientWidth and ClientHeight represent the interior of an object excluding
// border and scrollbar.
DISABLE_CFI_PERF
LayoutUnit LayoutBox::ClientWidth() const {
  NOT_DESTROYED();
  // We need to clamp negative values. This function may be called during layout
  // before frame_rect_ gets the final proper value. Another reason: While
  // border side values are currently limited to 2^20px (a recent change in the
  // code), if this limit is raised again in the future, we'd have ill effects
  // of saturated arithmetic otherwise.
  if (CanSkipComputeScrollbars()) {
    return (frame_rect_.Width() - BorderLeft() - BorderRight())
        .ClampNegativeToZero();
  } else {
    return (frame_rect_.Width() - BorderLeft() - BorderRight() -
            ComputeScrollbarsInternal(kClampToContentBox).HorizontalSum())
        .ClampNegativeToZero();
  }
}

DISABLE_CFI_PERF
LayoutUnit LayoutBox::ClientHeight() const {
  NOT_DESTROYED();
  // We need to clamp negative values. This function can be called during layout
  // before frame_rect_ gets the final proper value. The scrollbar may be wider
  // than the padding box. Another reason: While border side values are
  // currently limited to 2^20px (a recent change in the code), if this limit is
  // raised again in the future, we'd have ill effects of saturated arithmetic
  // otherwise.
  if (CanSkipComputeScrollbars()) {
    return (frame_rect_.Height() - BorderTop() - BorderBottom())
        .ClampNegativeToZero();
  } else {
    return (frame_rect_.Height() - BorderTop() - BorderBottom() -
            ComputeScrollbarsInternal(kClampToContentBox).VerticalSum())
        .ClampNegativeToZero();
  }
}

int LayoutBox::PixelSnappedClientWidth() const {
  NOT_DESTROYED();
  return SnapSizeToPixel(ClientWidth(), Location().X() + ClientLeft());
}

DISABLE_CFI_PERF
int LayoutBox::PixelSnappedClientHeight() const {
  NOT_DESTROYED();
  return SnapSizeToPixel(ClientHeight(), Location().Y() + ClientTop());
}

LayoutUnit LayoutBox::ClientWidthWithTableSpecialBehavior() const {
  NOT_DESTROYED();
  // clientWidth/Height is the visual portion of the box content, not including
  // borders or scroll bars, but includes padding. And per
  // https://www.w3.org/TR/CSS2/tables.html#model,
  // table wrapper box is a principal block box that contains the table box
  // itself and any caption boxes, and table grid box is a block-level box that
  // contains the table's internal table boxes. When table's border is specified
  // in CSS, the border is added to table grid box, not table wrapper box.
  // Currently, Blink doesn't have table wrapper box, and we are supposed to
  // retrieve clientWidth/Height from table wrapper box, not table grid box. So
  // when we retrieve clientWidth/Height, it includes table's border size.
  if (IsTable())
    return ClientWidth() + BorderLeft() + BorderRight();
  return ClientWidth();
}

LayoutUnit LayoutBox::ClientHeightWithTableSpecialBehavior() const {
  NOT_DESTROYED();
  // clientWidth/Height is the visual portion of the box content, not including
  // borders or scroll bars, but includes padding. And per
  // https://www.w3.org/TR/CSS2/tables.html#model,
  // table wrapper box is a principal block box that contains the table box
  // itself and any caption boxes, and table grid box is a block-level box that
  // contains the table's internal table boxes. When table's border is specified
  // in CSS, the border is added to table grid box, not table wrapper box.
  // Currently, Blink doesn't have table wrapper box, and we are supposed to
  // retrieve clientWidth/Height from table wrapper box, not table grid box. So
  // when we retrieve clientWidth/Height, it includes table's border size.
  if (IsTable())
    return ClientHeight() + BorderTop() + BorderBottom();
  return ClientHeight();
}

int LayoutBox::PixelSnappedOffsetWidth(const Element*) const {
  NOT_DESTROYED();
  return SnapSizeToPixel(OffsetWidth(), Location().X() + ClientLeft());
}

int LayoutBox::PixelSnappedOffsetHeight(const Element*) const {
  NOT_DESTROYED();
  return SnapSizeToPixel(OffsetHeight(), Location().Y() + ClientTop());
}

bool LayoutBox::UsesOverlayScrollbars() const {
  NOT_DESTROYED();
  if (StyleRef().HasCustomScrollbarStyle())
    return false;
  if (GetFrame()->GetPage()->GetScrollbarTheme().UsesOverlayScrollbars())
    return true;
  return false;
}

LayoutUnit LayoutBox::ScrollWidth() const {
  NOT_DESTROYED();
  if (IsScrollContainer())
    return GetScrollableArea()->ScrollWidth();
  if (StyleRef().IsScrollbarGutterStable() &&
      StyleRef().OverflowBlockDirection() == EOverflow::kHidden) {
    if (auto* scrollable_area = GetScrollableArea())
      return scrollable_area->ScrollWidth();
    else
      return PhysicalLayoutOverflowRect().Width();
  }
  // For objects with visible overflow, this matches IE.
  // FIXME: Need to work right with writing modes.
  if (StyleRef().IsLeftToRightDirection())
    return std::max(ClientWidth(), LayoutOverflowRect().MaxX() - BorderLeft());
  return ClientWidth() -
         std::min(LayoutUnit(), LayoutOverflowRect().X() - BorderLeft());
}

LayoutUnit LayoutBox::ScrollHeight() const {
  NOT_DESTROYED();
  if (IsScrollContainer())
    return GetScrollableArea()->ScrollHeight();
  if (StyleRef().IsScrollbarGutterStable() &&
      StyleRef().OverflowBlockDirection() == EOverflow::kHidden) {
    if (auto* scrollable_area = GetScrollableArea())
      return scrollable_area->ScrollHeight();
    else
      return PhysicalLayoutOverflowRect().Height();
  }
  // For objects with visible overflow, this matches IE.
  // FIXME: Need to work right with writing modes.
  return std::max(ClientHeight(), LayoutOverflowRect().MaxY() - BorderTop());
}

int LayoutBox::PixelSnappedScrollWidth() const {
  NOT_DESTROYED();
  return SnapSizeToPixel(ScrollWidth(), Location().X() + ClientLeft());
}

int LayoutBox::PixelSnappedScrollHeight() const {
  NOT_DESTROYED();
  if (IsScrollContainer())
    return SnapSizeToPixel(GetScrollableArea()->ScrollHeight(),
                           Location().Y() + ClientTop());
  // For objects with visible overflow, this matches IE.
  // FIXME: Need to work right with writing modes.
  return SnapSizeToPixel(ScrollHeight(), Location().Y() + ClientTop());
}

void LayoutBox::SetMargin(const NGPhysicalBoxStrut& box) {
  NOT_DESTROYED();
  margin_box_outsets_.SetTop(box.top);
  margin_box_outsets_.SetRight(box.right);
  margin_box_outsets_.SetBottom(box.bottom);
  margin_box_outsets_.SetLeft(box.left);
}

void LayoutBox::AbsoluteQuads(Vector<gfx::QuadF>& quads,
                              MapCoordinatesFlags mode) const {
  NOT_DESTROYED();
  if (LayoutFlowThread* flow_thread = FlowThreadContainingBlock()) {
    flow_thread->AbsoluteQuadsForDescendant(*this, quads, mode);
    return;
  }
  quads.push_back(LocalRectToAbsoluteQuad(PhysicalBorderBoxRect(), mode));
}

gfx::RectF LayoutBox::LocalBoundingBoxRectForAccessibility() const {
  NOT_DESTROYED();
  return gfx::RectF(0, 0, frame_rect_.Width().ToFloat(),
                    frame_rect_.Height().ToFloat());
}

void LayoutBox::UpdateAfterLayout() {
  NOT_DESTROYED();
  // Transform-origin depends on box size, so we need to update the layer
  // transform after layout.
  if (HasLayer()) {
    Layer()->UpdateTransformationMatrix();
    Layer()->UpdateSizeAndScrollingAfterLayout();
  }

  // When we've finished layout, if we aren't a LayoutNG object, we need to
  // reset our cached layout result. LayoutNG inside of
  // |NGBlockNode::RunOldLayout| will call |LayoutBox::SetCachedLayoutResult|
  // with a new synthesized layout result, if we are still being laid out by an
  // NG container.
  //
  // We also want to make sure that if our entrance point into layout changes,
  // e.g. an OOF-positioned object is laid out by an NG containing block, then
  // Legacy, then NG again, NG won't use a stale layout result.
  if (!IsLayoutNGObject() &&
      // When side effects are disabled, it's not possible to disable side
      // effects completely for |RunLegacyLayout|, but at least keep the
      // fragment tree unaffected.
      !NGDisableSideEffectsScope::IsDisabled())
    ClearLayoutResults();

  Document& document = GetDocument();
  document.IncLayoutCallsCounter();
  GetFrame()->GetInputMethodController().DidUpdateLayout(*this);
  if (IsPositioned())
    GetFrame()->GetInputMethodController().DidLayoutSubtree(*this);
  if (IsLayoutNGObject())
    document.IncLayoutCallsCounterNG();
}

bool LayoutBox::ShouldUseAutoIntrinsicSize() const {
  DisplayLockContext* context = GetDisplayLockContext();
  return context && context->IsLocked();
}

bool LayoutBox::HasOverrideIntrinsicContentWidth() const {
  NOT_DESTROYED();
  if (!ShouldApplyWidthContainment())
    return false;

  return StyleRef().ContainIntrinsicWidth().has_value();
}

bool LayoutBox::HasOverrideIntrinsicContentHeight() const {
  NOT_DESTROYED();
  if (!ShouldApplyHeightContainment())
    return false;

  return StyleRef().ContainIntrinsicHeight().has_value();
}

LayoutUnit LayoutBox::OverrideIntrinsicContentWidth() const {
  NOT_DESTROYED();
  DCHECK(HasOverrideIntrinsicContentWidth());
  const auto& style = StyleRef();
  const absl::optional<StyleIntrinsicLength>& intrinsic_length =
      style.ContainIntrinsicWidth();
  DCHECK(intrinsic_length);
  if (intrinsic_length->HasAuto() && ShouldUseAutoIntrinsicSize()) {
    const Element* elem = DynamicTo<Element>(GetNode());
    const ResizeObserverSize* size = elem ? elem->LastIntrinsicSize() : nullptr;
    if (size) {
      // ResizeObserverSize is adjusted to be in CSS space, we need to adjust it
      // back to Layout space by applying the effective zoom.
      return LayoutUnit::FromFloatRound(
          ToPhysicalSize(size->size(), StyleRef().GetWritingMode()).width *
          style.EffectiveZoom());
    }
  }
  DCHECK(intrinsic_length->GetLength().IsFixed());
  DCHECK_GE(intrinsic_length->GetLength().Value(), 0.f);
  return LayoutUnit(intrinsic_length->GetLength().Value());
}

LayoutUnit LayoutBox::OverrideIntrinsicContentHeight() const {
  NOT_DESTROYED();
  DCHECK(HasOverrideIntrinsicContentHeight());
  const auto& style = StyleRef();
  const absl::optional<StyleIntrinsicLength>& intrinsic_length =
      style.ContainIntrinsicHeight();
  DCHECK(intrinsic_length);
  if (intrinsic_length->HasAuto() && ShouldUseAutoIntrinsicSize()) {
    const Element* elem = DynamicTo<Element>(GetNode());
    const ResizeObserverSize* size = elem ? elem->LastIntrinsicSize() : nullptr;
    if (size) {
      // ResizeObserverSize is adjusted to be in CSS space, we need to adjust it
      // back to Layout space by applying the effective zoom.
      return LayoutUnit::FromFloatRound(
          ToPhysicalSize(size->size(), StyleRef().GetWritingMode()).height *
          style.EffectiveZoom());
    }
  }
  DCHECK(intrinsic_length->GetLength().IsFixed());
  DCHECK_GE(intrinsic_length->GetLength().Value(), 0.f);
  return LayoutUnit(intrinsic_length->GetLength().Value());
}

LayoutUnit LayoutBox::DefaultIntrinsicContentInlineSize() const {
  NOT_DESTROYED();
  // If the intrinsic-inline-size is specified, then we shouldn't ever need to
  // get here.
  DCHECK(!HasOverrideIntrinsicContentLogicalWidth());

  if (!IsA<Element>(GetNode()))
    return kIndefiniteSize;
  const Element& element = *To<Element>(GetNode());

  const auto* select = DynamicTo<HTMLSelectElement>(element);
  if (UNLIKELY(select && select->UsesMenuList())) {
    return MenuListIntrinsicInlineSize(*select, *this);
  }
  const auto* input = DynamicTo<HTMLInputElement>(element);
  if (UNLIKELY(input)) {
    if (input->IsTextField())
      return TextFieldIntrinsicInlineSize(*input, *this);
    const AtomicString& type = input->type();
    if (type == input_type_names::kFile)
      return FileUploadControlIntrinsicInlineSize(*input, *this);
    if (type == input_type_names::kRange)
      return SliderIntrinsicInlineSize(*this);
    auto effective_appearance = StyleRef().EffectiveAppearance();
    if (effective_appearance == kCheckboxPart) {
      return ThemePartIntrinsicSize(*this, WebThemeEngine::kPartCheckbox)
          .inline_size;
    }
    if (effective_appearance == kRadioPart) {
      return ThemePartIntrinsicSize(*this, WebThemeEngine::kPartRadio)
          .inline_size;
    }
    return kIndefiniteSize;
  }
  const auto* textarea = DynamicTo<HTMLTextAreaElement>(element);
  if (UNLIKELY(textarea))
    return TextAreaIntrinsicInlineSize(*textarea, *this);
  if (IsSliderContainer(element))
    return SliderIntrinsicInlineSize(*this);

  return kIndefiniteSize;
}

LayoutUnit LayoutBox::DefaultIntrinsicContentBlockSize() const {
  NOT_DESTROYED();
  // If the intrinsic-block-size is specified, then we shouldn't ever need to
  // get here.
  DCHECK(!HasOverrideIntrinsicContentLogicalHeight());

  if (const auto* select = DynamicTo<HTMLSelectElement>(GetNode())) {
    if (select->UsesMenuList())
      return MenuListIntrinsicBlockSize(*select, *this);
    return ListBoxItemHeight(*select, *this) * select->ListBoxSize() -
           ComputeLogicalScrollbars().BlockSum();
  }
  if (IsTextFieldIncludingNG())
    return TextFieldIntrinsicBlockSize(*To<HTMLInputElement>(GetNode()), *this);
  if (IsTextAreaIncludingNG()) {
    return TextAreaIntrinsicBlockSize(*To<HTMLTextAreaElement>(GetNode()),
                                      *this);
  }

  auto effective_appearance = StyleRef().EffectiveAppearance();
  if (effective_appearance == kCheckboxPart) {
    return ThemePartIntrinsicSize(*this, WebThemeEngine::kPartCheckbox)
        .block_size;
  }
  if (effective_appearance == kRadioPart) {
    return ThemePartIntrinsicSize(*this, WebThemeEngine::kPartRadio).block_size;
  }

  return kIndefiniteSize;
}

LayoutUnit LayoutBox::LogicalHeightWithVisibleOverflow() const {
  NOT_DESTROYED();
  if (!LayoutOverflowIsSet() || IsScrollContainer() ||
      StyleRef().OverflowY() == EOverflow::kClip)
    return LogicalHeight();
  LayoutRect overflow = LayoutOverflowRect();
  if (StyleRef().IsHorizontalWritingMode())
    return overflow.MaxY();
  return overflow.MaxX();
}

LayoutUnit LayoutBox::ConstrainLogicalWidthByMinMax(
    LayoutUnit logical_width,
    LayoutUnit available_width,
    const LayoutBlock* cb,
    bool allow_intrinsic) const {
  NOT_DESTROYED();
  const ComputedStyle& style_to_use = StyleRef();

  // This implements the transferred min/max sizes per
  // https://drafts.csswg.org/css-sizing-4/#aspect-ratio
  if (ShouldComputeLogicalHeightFromAspectRatio()) {
    MinMaxSizes transferred_min_max =
        ComputeMinMaxLogicalWidthFromAspectRatio();
    logical_width = transferred_min_max.ClampSizeToMinAndMax(logical_width);
  }

  if (!style_to_use.LogicalMaxWidth().IsNone() &&
      (allow_intrinsic ||
       !style_to_use.LogicalMaxWidth().IsContentOrIntrinsic())) {
    logical_width = std::min(
        logical_width,
        ComputeLogicalWidthUsing(kMaxSize, style_to_use.LogicalMaxWidth(),
                                 available_width, cb));
  }

  // If we have an aspect-ratio, check if we need to apply min-width: auto.
  Length min_length = style_to_use.LogicalMinWidth();
  if (!style_to_use.AspectRatio().IsAuto() && min_length.IsAuto() &&
      (style_to_use.LogicalWidth().IsAuto() ||
       style_to_use.LogicalWidth().IsMinContent() ||
       style_to_use.LogicalWidth().IsMaxContent()) &&
      style_to_use.OverflowInlineDirection() == EOverflow::kVisible) {
    // Make sure we actually used the aspect ratio.
    if (ShouldComputeLogicalWidthFromAspectRatio())
      min_length = Length::MinIntrinsic();
  }
  if (!allow_intrinsic && style_to_use.LogicalMinWidth().IsContentOrIntrinsic())
    return logical_width;
  return std::max(logical_width, ComputeLogicalWidthUsing(kMinSize, min_length,
                                                          available_width, cb));
}

LayoutUnit LayoutBox::ConstrainLogicalHeightByMinMax(
    LayoutUnit logical_height,
    LayoutUnit intrinsic_content_height) const {
  NOT_DESTROYED();
  // Note that the values 'min-content', 'max-content' and 'fit-content' should
  // behave as the initial value if specified in the block direction.
  const Length& logical_max_height = StyleRef().LogicalMaxHeight();
  if (!logical_max_height.IsNone() && !logical_max_height.IsMinContent() &&
      !logical_max_height.IsMaxContent() &&
      !logical_max_height.IsMinIntrinsic() &&
      !logical_max_height.IsFitContent()) {
    LayoutUnit max_h = ComputeLogicalHeightUsing(kMaxSize, logical_max_height,
                                                 intrinsic_content_height);
    if (max_h != -1)
      logical_height = std::min(logical_height, max_h);
  }
  Length logical_min_height = StyleRef().LogicalMinHeight();
  if (logical_min_height.IsAuto() &&
      ShouldComputeLogicalHeightFromAspectRatio() &&
      intrinsic_content_height != kIndefiniteSize &&
      intrinsic_content_height != LayoutUnit::Max() &&
      StyleRef().OverflowBlockDirection() == EOverflow::kVisible) {
    logical_min_height = Length::Fixed(intrinsic_content_height);
  }
  if (logical_min_height.IsMinContent() || logical_min_height.IsMaxContent() ||
      logical_min_height.IsMinIntrinsic() || logical_min_height.IsFitContent())
    logical_min_height = Length::Auto();
  return std::max(logical_height,
                  ComputeLogicalHeightUsing(kMinSize, logical_min_height,
                                            intrinsic_content_height));
}

LayoutUnit LayoutBox::ConstrainContentBoxLogicalHeightByMinMax(
    LayoutUnit logical_height,
    LayoutUnit intrinsic_content_height) const {
  NOT_DESTROYED();
  // If the min/max height and logical height are both percentages we take
  // advantage of already knowing the current resolved percentage height
  // to avoid recursing up through our containing blocks again to determine it.
  const ComputedStyle& style_to_use = StyleRef();
  if (!style_to_use.LogicalMaxHeight().IsNone()) {
    if (style_to_use.LogicalMaxHeight().IsPercent() &&
        style_to_use.LogicalHeight().IsPercent()) {
      LayoutUnit available_logical_height(
          logical_height / style_to_use.LogicalHeight().Value() * 100);
      logical_height = std::min(logical_height,
                                ValueForLength(style_to_use.LogicalMaxHeight(),
                                               available_logical_height));
    } else {
      LayoutUnit max_height(ComputeContentLogicalHeight(
          kMaxSize, style_to_use.LogicalMaxHeight(), intrinsic_content_height));
      if (max_height != -1)
        logical_height = std::min(logical_height, max_height);
    }
  }

  if (style_to_use.LogicalMinHeight().IsPercent() &&
      style_to_use.LogicalHeight().IsPercent()) {
    LayoutUnit available_logical_height(
        logical_height / style_to_use.LogicalHeight().Value() * 100);
    logical_height =
        std::max(logical_height, ValueForLength(style_to_use.LogicalMinHeight(),
                                                available_logical_height));
  } else {
    logical_height = std::max(
        logical_height,
        ComputeContentLogicalHeight(kMinSize, style_to_use.LogicalMinHeight(),
                                    intrinsic_content_height));
  }

  return logical_height;
}

void LayoutBox::SetLocationAndUpdateOverflowControlsIfNeeded(
    const LayoutPoint& location) {
  NOT_DESTROYED();
  if (!HasLayer()) {
    SetLocation(location);
    return;
  }
  // The Layer does not yet have the up to date subpixel accumulation
  // so we base the size strictly on the frame rect's location.
  gfx::Size old_pixel_snapped_border_rect_size =
      PixelSnappedBorderBoxRect().size();
  SetLocation(location);
  // TODO(crbug.com/1020913): This is problematic because this function may be
  // called after layout of this LayoutBox. Changing scroll container size here
  // will cause inconsistent layout. Also we should be careful not to set
  // this LayoutBox NeedsLayout. This will be unnecessary when we support
  // subpixel layout of scrollable area and overflow controls.
  if (PixelSnappedBorderBoxRect().size() !=
      old_pixel_snapped_border_rect_size) {
    bool needed_layout = NeedsLayout();
    PaintLayerScrollableArea::FreezeScrollbarsScope freeze_scrollbar;
    Layer()->UpdateSizeAndScrollingAfterLayout();
    // The above call should not schedule new NeedsLayout.
    DCHECK(needed_layout || !NeedsLayout());
  }
}

gfx::QuadF LayoutBox::AbsoluteContentQuad(MapCoordinatesFlags flags) const {
  NOT_DESTROYED();
  PhysicalRect rect = PhysicalContentBoxRect();
  return LocalRectToAbsoluteQuad(rect, flags);
}

PhysicalRect LayoutBox::PhysicalBackgroundRect(
    BackgroundRectType rect_type) const {
  NOT_DESTROYED();
  // If the background transfers to view, the used background of this object
  // is transparent.
  if (rect_type == kBackgroundKnownOpaqueRect && BackgroundTransfersToView())
    return PhysicalRect();

  absl::optional<EFillBox> background_box;
  Color background_color = ResolveColor(GetCSSPropertyBackgroundColor());
  // Find the largest background rect of the given opaqueness.
  for (const FillLayer* cur = &(StyleRef().BackgroundLayers()); cur;
       cur = cur->Next()) {
    EFillBox current_clip = cur->Clip();
    if (rect_type == kBackgroundKnownOpaqueRect) {
      if (current_clip == EFillBox::kText)
        continue;

      if (cur->GetBlendMode() != BlendMode::kNormal ||
          cur->Composite() != kCompositeSourceOver)
        continue;

      bool layer_known_opaque = false;
      // Check if the image is opaque and fills the clip.
      if (const StyleImage* image = cur->GetImage()) {
        if ((cur->RepeatX() == EFillRepeat::kRepeatFill ||
             cur->RepeatX() == EFillRepeat::kRoundFill) &&
            (cur->RepeatY() == EFillRepeat::kRepeatFill ||
             cur->RepeatY() == EFillRepeat::kRoundFill) &&
            image->KnownToBeOpaque(GetDocument(), StyleRef())) {
          layer_known_opaque = true;
        }
      }

      // The background color is painted into the last layer.
      if (!cur->Next() && !background_color.HasAlpha())
        layer_known_opaque = true;

      // If neither the image nor the color are opaque then skip this layer.
      if (!layer_known_opaque)
        continue;
    } else {
      // Ignore invisible background layers for kBackgroundPaintedExtent.
      DCHECK_EQ(rect_type, kBackgroundPaintedExtent);
      if (!cur->GetImage() && (cur->Next() || background_color.Alpha() == 0))
        continue;
      // A content-box clipped fill layer can be scrolled into the padding box
      // of the overflow container.
      if (current_clip == EFillBox::kContent &&
          cur->Attachment() == EFillAttachment::kLocal) {
        current_clip = EFillBox::kPadding;
      }
    }

    // Restrict clip if attachment is local.
    if (current_clip == EFillBox::kBorder &&
        cur->Attachment() == EFillAttachment::kLocal)
      current_clip = EFillBox::kPadding;

    background_box = background_box
                         ? EnclosingFillBox(*background_box, current_clip)
                         : current_clip;
  }

  if (!background_box)
    return PhysicalRect();

  if (*background_box == EFillBox::kText) {
    DCHECK_NE(rect_type, kBackgroundKnownOpaqueRect);
    *background_box = EFillBox::kBorder;
  }

  if (rect_type == kBackgroundPaintedExtent &&
      *background_box == EFillBox::kBorder &&
      BackgroundClipBorderBoxIsEquivalentToPaddingBox()) {
    *background_box = EFillBox::kPadding;
  }

  switch (*background_box) {
    case EFillBox::kBorder:
      return PhysicalBorderBoxRect();
    case EFillBox::kPadding:
      return PhysicalPaddingBoxRect();
    case EFillBox::kContent:
      return PhysicalContentBoxRect();
    default:
      NOTREACHED();
  }
  return PhysicalRect();
}

void LayoutBox::AddOutlineRects(Vector<PhysicalRect>& rects,
                                OutlineInfo* info,
                                const PhysicalOffset& additional_offset,
                                NGOutlineType) const {
  NOT_DESTROYED();
  rects.emplace_back(additional_offset, Size());
  if (info)
    *info = OutlineInfo::GetFromStyle(StyleRef());
}

bool LayoutBox::CanResize() const {
  NOT_DESTROYED();
  // We need a special case for <iframe> because they never have
  // hasOverflowClip(). However, they do "implicitly" clip their contents, so
  // we want to allow resizing them also.
  return (IsScrollContainer() || IsLayoutIFrame()) && StyleRef().HasResize();
}

MinMaxSizes LayoutBox::ComputeMinMaxLogicalWidthFromAspectRatio() const {
  NOT_DESTROYED();
  DCHECK_NE(StyleRef().AspectRatio().GetType(), EAspectRatioType::kAuto);

  // The spec requires us to clamp these by the specified size (it calls it the
  // preferred size). However, we actually don't need to worry about that,
  // because we only use this if the width is indefinite.

  // We do not need to compute the min/max inline sizes; as long as we always
  // apply the transferred min/max size before the explicit min/max size, the
  // result will be identical.

  LogicalSize ratio = StyleRef().LogicalAspectRatio();
  MinMaxSizes block_min_max{
      ConstrainLogicalHeightByMinMax(LayoutUnit(), kIndefiniteSize),
      ConstrainLogicalHeightByMinMax(LayoutUnit::Max(), kIndefiniteSize)};
  if (block_min_max.max_size == kIndefiniteSize)
    block_min_max.max_size = LayoutUnit::Max();

  NGBoxStrut border_padding(BorderStart() + ComputedCSSPaddingStart(),
                            BorderEnd() + ComputedCSSPaddingEnd(),
                            BorderBefore() + ComputedCSSPaddingBefore(),
                            BorderAfter() + ComputedCSSPaddingAfter());

  MinMaxSizes transferred_min_max = {LayoutUnit(), LayoutUnit::Max()};
  if (block_min_max.min_size > LayoutUnit()) {
    transferred_min_max.min_size = InlineSizeFromAspectRatio(
        border_padding, ratio, StyleRef().BoxSizingForAspectRatio(),
        block_min_max.min_size);
  }
  if (block_min_max.max_size != LayoutUnit::Max()) {
    transferred_min_max.max_size = InlineSizeFromAspectRatio(
        border_padding, ratio, StyleRef().BoxSizingForAspectRatio(),
        block_min_max.max_size);
  }
  // Minimum size wins over maximum size.
  transferred_min_max.max_size =
      std::max(transferred_min_max.max_size, transferred_min_max.min_size);
  return transferred_min_max;
}

bool LayoutBox::HasScrollbarGutters(ScrollbarOrientation orientation) const {
  NOT_DESTROYED();
  if (StyleRef().IsScrollbarGutterAuto())
    return false;

  DCHECK(StyleRef().IsScrollbarGutterStable());

  // Scrollbar-gutter propagates to the viewport
  // (see:|StyleResolver::PropagateStyleToViewport|).
  if (orientation == kVerticalScrollbar) {
    EOverflow overflow = StyleRef().OverflowY();
    return StyleRef().IsHorizontalWritingMode() &&
           (overflow == EOverflow::kAuto || overflow == EOverflow::kScroll ||
            overflow == EOverflow::kHidden) &&
           !UsesOverlayScrollbars() &&
           GetNode() != GetDocument().ViewportDefiningElement();
  } else {
    EOverflow overflow = StyleRef().OverflowX();
    return !StyleRef().IsHorizontalWritingMode() &&
           (overflow == EOverflow::kAuto || overflow == EOverflow::kScroll ||
            overflow == EOverflow::kHidden) &&
           !UsesOverlayScrollbars() &&
           GetNode() != GetDocument().ViewportDefiningElement();
  }
}

NGPhysicalBoxStrut LayoutBox::ComputeScrollbarsInternal(
    ShouldClampToContentBox clamp_to_content_box,
    OverlayScrollbarClipBehavior overlay_scrollbar_clip_behavior,
    ShouldIncludeScrollbarGutter include_scrollbar_gutter) const {
  NOT_DESTROYED();
  NGPhysicalBoxStrut scrollbars;
  PaintLayerScrollableArea* scrollable_area = GetScrollableArea();

  if (include_scrollbar_gutter == kIncludeScrollbarGutter &&
      HasScrollbarGutters(kVerticalScrollbar)) {
    LayoutUnit gutter_size = LayoutUnit(HypotheticalScrollbarThickness(
        *this, kVerticalScrollbar, /* include_overlay_thickness */ true));
    if (ShouldPlaceVerticalScrollbarOnLeft()) {
      scrollbars.left = gutter_size;
      if (StyleRef().IsScrollbarGutterBothEdges())
        scrollbars.right = gutter_size;
    } else {
      scrollbars.right = gutter_size;
      if (StyleRef().IsScrollbarGutterBothEdges())
        scrollbars.left = gutter_size;
    }
  } else if (scrollable_area) {
    if (ShouldPlaceVerticalScrollbarOnLeft()) {
      scrollbars.left = LayoutUnit(scrollable_area->VerticalScrollbarWidth(
          overlay_scrollbar_clip_behavior));
    } else {
      scrollbars.right = LayoutUnit(scrollable_area->VerticalScrollbarWidth(
          overlay_scrollbar_clip_behavior));
    }
  }

  if (include_scrollbar_gutter == kIncludeScrollbarGutter &&
      HasScrollbarGutters(kHorizontalScrollbar)) {
    LayoutUnit gutter_size = LayoutUnit(
        HypotheticalScrollbarThickness(*this, kHorizontalScrollbar,
                                       /* include_overlay_thickness */ true));
    scrollbars.bottom = gutter_size;
    if (StyleRef().IsScrollbarGutterBothEdges())
      scrollbars.top = gutter_size;
  } else if (scrollable_area) {
    scrollbars.bottom = LayoutUnit(scrollable_area->HorizontalScrollbarHeight(
        overlay_scrollbar_clip_behavior));
  }

  // Use the width of the vertical scrollbar, unless it's larger than the
  // logical width of the content box, in which case we'll use that instead.
  // Scrollbar handling is quite bad in such situations, and this code here
  // is just to make sure that left-hand scrollbars don't mess up
  // scrollWidth. For the full story, visit http://crbug.com/724255.
  if (scrollbars.left > 0 && clamp_to_content_box == kClampToContentBox) {
    LayoutUnit max_width = frame_rect_.Width() - BorderAndPaddingWidth();
    scrollbars.left =
        std::min(scrollbars.left, max_width.ClampNegativeToZero());
  }

  return scrollbars;
}

bool LayoutBox::CanBeScrolledAndHasScrollableArea() const {
  NOT_DESTROYED();
  return CanBeProgrammaticallyScrolled() &&
         (PixelSnappedScrollHeight() != PixelSnappedClientHeight() ||
          PixelSnappedScrollWidth() != PixelSnappedClientWidth());
}

void LayoutBox::Autoscroll(const PhysicalOffset& position_in_root_frame) {
  NOT_DESTROYED();
  LocalFrame* frame = GetFrame();
  if (!frame)
    return;

  LocalFrameView* frame_view = frame->View();
  if (!frame_view)
    return;

  PhysicalOffset absolute_position =
      frame_view->ConvertFromRootFrame(position_in_root_frame);
  mojom::blink::ScrollIntoViewParamsPtr params =
      ScrollAlignment::CreateScrollIntoViewParams(
          ScrollAlignment::ToEdgeIfNeeded(), ScrollAlignment::ToEdgeIfNeeded(),
          mojom::blink::ScrollType::kUser);
  scroll_into_view_util::ScrollRectToVisible(
      *this,
      PhysicalRect(absolute_position,
                   PhysicalSize(LayoutUnit(1), LayoutUnit(1))),
      std::move(params));
}

// If specified point is outside the border-belt-excluded box (the border box
// inset by the autoscroll activation threshold), returned offset denotes
// direction of scrolling.
PhysicalOffset LayoutBox::CalculateAutoscrollDirection(
    const gfx::PointF& point_in_root_frame) const {
  NOT_DESTROYED();
  if (!GetFrame())
    return PhysicalOffset();

  LocalFrameView* frame_view = GetFrame()->View();
  if (!frame_view)
    return PhysicalOffset();

  PhysicalRect absolute_scrolling_box(AbsoluteBoundingBoxRect());

  // Exclude scrollbars so the border belt (activation area) starts from the
  // scrollbar-content edge rather than the window edge.
  ExcludeScrollbars(absolute_scrolling_box,
                    kExcludeOverlayScrollbarSizeForHitTesting);

  PhysicalRect belt_box =
      View()->GetFrameView()->ConvertToRootFrame(absolute_scrolling_box);
  belt_box.Inflate(LayoutUnit(-kAutoscrollBeltSize));
  gfx::PointF point = point_in_root_frame;

  if (point.x() < belt_box.X())
    point.Offset(-kAutoscrollBeltSize, 0);
  else if (point.x() > belt_box.Right())
    point.Offset(kAutoscrollBeltSize, 0);

  if (point.y() < belt_box.Y())
    point.Offset(0, -kAutoscrollBeltSize);
  else if (point.y() > belt_box.Bottom())
    point.Offset(0, kAutoscrollBeltSize);

  return PhysicalOffset::FromVector2dFRound(point - point_in_root_frame);
}

LayoutBox* LayoutBox::FindAutoscrollable(LayoutObject* layout_object,
                                         bool is_middle_click_autoscroll) {
  while (layout_object &&
         !(layout_object->IsBox() &&
           To<LayoutBox>(layout_object)->CanBeScrolledAndHasScrollableArea())) {
    // Do not start selection-based autoscroll when the node is inside a
    // fixed-position element.
    if (!is_middle_click_autoscroll && layout_object->IsBox() &&
        To<LayoutBox>(layout_object)->IsFixedToView()) {
      return nullptr;
    }

    if (!layout_object->Parent() &&
        layout_object->GetNode() == layout_object->GetDocument() &&
        layout_object->GetDocument().LocalOwner()) {
      layout_object =
          layout_object->GetDocument().LocalOwner()->GetLayoutObject();
    } else {
      layout_object = layout_object->Parent();
    }
  }

  return DynamicTo<LayoutBox>(layout_object);
}

bool LayoutBox::HasHorizontallyScrollableAncestor(LayoutObject* layout_object) {
  while (layout_object) {
    if (layout_object->IsBox() &&
        To<LayoutBox>(layout_object)->HasScrollableOverflowX())
      return true;

    // Scroll is not propagating.
    if (layout_object->StyleRef().OverscrollBehaviorX() !=
        EOverscrollBehavior::kAuto)
      break;

    if (!layout_object->Parent() &&
        layout_object->GetNode() == layout_object->GetDocument() &&
        layout_object->GetDocument().LocalOwner()) {
      layout_object =
          layout_object->GetDocument().LocalOwner()->GetLayoutObject();
    } else {
      layout_object = layout_object->Parent();
    }
  }

  return false;
}

bool LayoutBox::NeedsPreferredWidthsRecalculation() const {
  NOT_DESTROYED();
  return StyleRef().PaddingStart().IsPercentOrCalc() ||
         StyleRef().PaddingEnd().IsPercentOrCalc();
}

gfx::Vector2d LayoutBox::OriginAdjustmentForScrollbars() const {
  NOT_DESTROYED();
  if (CanSkipComputeScrollbars())
    return gfx::Vector2d();

  NGPhysicalBoxStrut scrollbars = ComputeScrollbarsInternal(kClampToContentBox);
  return gfx::Vector2d(scrollbars.left.ToInt(), scrollbars.top.ToInt());
}

gfx::Point LayoutBox::ScrollOrigin() const {
  NOT_DESTROYED();
  return GetScrollableArea() ? GetScrollableArea()->ScrollOrigin()
                             : gfx::Point();
}

PhysicalOffset LayoutBox::ScrolledContentOffset() const {
  NOT_DESTROYED();
  DCHECK(IsScrollContainer());
  DCHECK(GetScrollableArea());
  return PhysicalOffset::FromVector2dFFloor(
      GetScrollableArea()->GetScrollOffset());
}

gfx::Vector2d LayoutBox::PixelSnappedScrolledContentOffset() const {
  NOT_DESTROYED();
  DCHECK(IsScrollContainer());
  DCHECK(GetScrollableArea());
  return GetScrollableArea()->ScrollOffsetInt();
}

PhysicalRect LayoutBox::ClippingRect(const PhysicalOffset& location) const {
  NOT_DESTROYED();
  PhysicalRect result(PhysicalRect::InfiniteIntRect());
  if (ShouldClipOverflowAlongEitherAxis())
    result = OverflowClipRect(location);

  if (HasClip())
    result.Intersect(ClipRect(location));

  return result;
}

gfx::PointF LayoutBox::PerspectiveOrigin(const PhysicalSize* size) const {
  if (!HasTransformRelatedProperty())
    return gfx::PointF();

  // Use the |size| parameter instead of |Size()| if present.
  gfx::SizeF float_size = size ? gfx::SizeF(*size) : gfx::SizeF(Size());

  return PointForLengthPoint(StyleRef().PerspectiveOrigin(), float_size);
}

bool LayoutBox::MapVisualRectToContainer(
    const LayoutObject* container_object,
    const PhysicalOffset& container_offset,
    const LayoutObject* ancestor,
    VisualRectFlags visual_rect_flags,
    TransformState& transform_state) const {
  NOT_DESTROYED();
  bool container_preserve_3d = container_object->StyleRef().Preserves3D() &&
                               container_object == NearestAncestorForElement();

  TransformState::TransformAccumulation accumulation =
      container_preserve_3d ? TransformState::kAccumulateTransform
                            : TransformState::kFlattenTransform;

  // If there is no transform on this box, adjust for container offset and
  // container scrolling, then apply container clip.
  if (!ShouldUseTransformFromContainer(container_object)) {
    transform_state.Move(container_offset, accumulation);
    if (container_object->IsBox() && container_object != ancestor &&
        !To<LayoutBox>(container_object)
             ->MapContentsRectToBoxSpace(transform_state, accumulation, *this,
                                         visual_rect_flags)) {
      return false;
    }
    return true;
  }

  // Otherwise, do the following:
  // 1. Expand for pixel snapping.
  // 2. Generate transformation matrix combining, in this order
  //    a) transform,
  //    b) container offset,
  //    c) container scroll offset,
  //    d) perspective applied by container.
  // 3. Apply transform Transform+flattening.
  // 4. Apply container clip.

  // 1. Expand for pixel snapping.
  // Use EnclosingBoundingBox because we cannot properly compute pixel
  // snapping for painted elements within the transform since we don't know
  // the desired subpixel accumulation at this point, and the transform may
  // include a scale. This only makes sense for non-preserve3D.
  //
  // TODO(dbaron): Does the flattening here need to be done for the
  // early return case above as well?
  // (Why is this flattening needed in addition to the flattening done by
  // using TransformState::kAccumulateTransform?)
  if (!StyleRef().Preserves3D()) {
    transform_state.Flatten();
    transform_state.SetQuad(gfx::QuadF(gfx::RectF(
        gfx::ToEnclosingRect(transform_state.LastPlanarQuad().BoundingBox()))));
  }

  // 2. Generate transformation matrix.
  // a) Transform.
  TransformationMatrix transform;
  if (Layer() && Layer()->Transform())
    transform.Multiply(Layer()->CurrentTransform());

  // b) Container offset.
  transform.PostTranslate(container_offset.left.ToFloat(),
                          container_offset.top.ToFloat());

  // c) Container scroll offset.
  if (container_object->IsBox() && container_object != ancestor &&
      To<LayoutBox>(container_object)->ContainedContentsScroll(*this)) {
    PhysicalOffset offset(
        -To<LayoutBox>(container_object)->ScrolledContentOffset());
    transform.PostTranslate(offset.left, offset.top);
  }

  bool has_perspective = container_object && container_object->HasLayer() &&
                         container_object->StyleRef().HasPerspective();
  if (has_perspective && container_object != NearestAncestorForElement()) {
    has_perspective = false;

    if (StyleRef().Preserves3D() || transform.Creates3D()) {
      UseCounter::Count(GetDocument(),
                        WebFeature::kDifferentPerspectiveCBOrParent);
    }
  }

  // d) Perspective applied by container.
  if (has_perspective) {
    // Perspective on the container affects us, so we have to factor it in here.
    DCHECK(container_object->HasLayer());
    gfx::PointF perspective_origin;
    if (const auto* container_box = DynamicTo<LayoutBox>(container_object))
      perspective_origin = container_box->PerspectiveOrigin();

    TransformationMatrix perspective_matrix;
    perspective_matrix.ApplyPerspective(
        container_object->StyleRef().UsedPerspective());
    perspective_matrix.ApplyTransformOrigin(perspective_origin.x(),
                                            perspective_origin.y(), 0);

    transform = perspective_matrix * transform;
  }

  // 3. Apply transform and flatten.
  transform_state.ApplyTransform(transform, accumulation);
  if (!container_preserve_3d)
    transform_state.Flatten();

  // 4. Apply container clip.
  if (container_object->IsBox() && container_object != ancestor &&
      container_object->HasClipRelatedProperty()) {
    return To<LayoutBox>(container_object)
        ->ApplyBoxClips(transform_state, accumulation, visual_rect_flags);
  }

  return true;
}

bool LayoutBox::MapContentsRectToBoxSpace(
    TransformState& transform_state,
    TransformState::TransformAccumulation accumulation,
    const LayoutObject& contents,
    VisualRectFlags visual_rect_flags) const {
  NOT_DESTROYED();
  if (!HasClipRelatedProperty())
    return true;

  if (ContainedContentsScroll(contents))
    transform_state.Move(-ScrolledContentOffset());

  return ApplyBoxClips(transform_state, accumulation, visual_rect_flags);
}

bool LayoutBox::ContainedContentsScroll(const LayoutObject& contents) const {
  NOT_DESTROYED();
  if (IsA<LayoutView>(this) &&
      contents.StyleRef().GetPosition() == EPosition::kFixed) {
    return false;
  }
  return IsScrollContainer();
}

bool LayoutBox::ApplyBoxClips(
    TransformState& transform_state,
    TransformState::TransformAccumulation accumulation,
    VisualRectFlags visual_rect_flags) const {
  NOT_DESTROYED();
  // This won't work fully correctly for fixed-position elements, who should
  // receive CSS clip but for whom the current object is not in the containing
  // block chain.
  PhysicalRect clip_rect = ClippingRect(PhysicalOffset());

  transform_state.Flatten();
  PhysicalRect rect(
      gfx::ToEnclosingRect(transform_state.LastPlanarQuad().BoundingBox()));
  bool does_intersect;
  if (visual_rect_flags & kEdgeInclusive) {
    does_intersect = rect.InclusiveIntersect(clip_rect);
  } else {
    rect.Intersect(clip_rect);
    does_intersect = !rect.IsEmpty();
  }
  transform_state.SetQuad(gfx::QuadF(gfx::RectF(rect)));

  return does_intersect;
}

MinMaxSizes LayoutBox::PreferredLogicalWidths() const {
  NOT_DESTROYED();
  NOTREACHED();
  return MinMaxSizes();
}

MinMaxSizes LayoutBox::IntrinsicLogicalWidths(MinMaxSizesType type) const {
  NOT_DESTROYED();
  if (!ShouldComputeSizeAsReplaced() && type == MinMaxSizesType::kContent &&
      !StyleRef().AspectRatio().IsAuto()) {
    MinMaxSizes sizes;
    if (ComputeLogicalWidthFromAspectRatio(&sizes.min_size)) {
      sizes.max_size = sizes.min_size;
      return sizes;
    }
  }
  const_cast<LayoutBox*>(this)->UpdateCachedIntrinsicLogicalWidthsIfNeeded();
  return intrinsic_logical_widths_;
}

void LayoutBox::UpdateCachedIntrinsicLogicalWidthsIfNeeded() {
  NOT_DESTROYED();
  if (!IntrinsicLogicalWidthsDirty())
    return;

#if DCHECK_IS_ON()
  SetLayoutNeededForbiddenScope layout_forbidden_scope(*this);
#endif

  intrinsic_logical_widths_ = ComputeIntrinsicLogicalWidths();
  intrinsic_logical_widths_initial_block_size_ = LayoutUnit::Min();
  ClearIntrinsicLogicalWidthsDirty();
}

LayoutUnit LayoutBox::OverrideLogicalWidth() const {
  NOT_DESTROYED();
  DCHECK(HasOverrideLogicalWidth());
  if (extra_input_ && extra_input_->override_inline_size)
    return *extra_input_->override_inline_size;
  return rare_data_->override_logical_width_;
}

LayoutUnit LayoutBox::OverrideLogicalHeight() const {
  NOT_DESTROYED();
  DCHECK(HasOverrideLogicalHeight());
  if (extra_input_ && extra_input_->override_block_size)
    return *extra_input_->override_block_size;
  return rare_data_->override_logical_height_;
}

bool LayoutBox::IsOverrideLogicalHeightDefinite() const {
  NOT_DESTROYED();
  return extra_input_ && extra_input_->is_override_block_size_definite;
}

bool LayoutBox::StretchInlineSizeIfAuto() const {
  NOT_DESTROYED();
  return extra_input_ && extra_input_->stretch_inline_size_if_auto;
}

bool LayoutBox::StretchBlockSizeIfAuto() const {
  NOT_DESTROYED();
  return extra_input_ && extra_input_->stretch_block_size_if_auto;
}

bool LayoutBox::HasOverrideLogicalHeight() const {
  NOT_DESTROYED();
  if (extra_input_ && extra_input_->override_block_size)
    return true;
  return rare_data_ && rare_data_->override_logical_height_ != -1;
}

bool LayoutBox::HasOverrideLogicalWidth() const {
  NOT_DESTROYED();
  if (extra_input_ && extra_input_->override_inline_size)
    return true;
  return rare_data_ && rare_data_->override_logical_width_ != -1;
}

void LayoutBox::SetOverrideLogicalHeight(LayoutUnit height) {
  NOT_DESTROYED();
  DCHECK(!extra_input_);
  DCHECK_GE(height, 0);
  EnsureRareData().override_logical_height_ = height;
}

void LayoutBox::SetOverrideLogicalWidth(LayoutUnit width) {
  NOT_DESTROYED();
  DCHECK(!extra_input_);
  DCHECK_GE(width, 0);
  EnsureRareData().override_logical_width_ = width;
}

void LayoutBox::ClearOverrideLogicalHeight() {
  NOT_DESTROYED();
  DCHECK(!extra_input_);
  if (rare_data_)
    rare_data_->override_logical_height_ = LayoutUnit(-1);
}

void LayoutBox::ClearOverrideLogicalWidth() {
  NOT_DESTROYED();
  DCHECK(!extra_input_);
  if (rare_data_)
    rare_data_->override_logical_width_ = LayoutUnit(-1);
}

void LayoutBox::ClearOverrideSize() {
  NOT_DESTROYED();
  ClearOverrideLogicalHeight();
  ClearOverrideLogicalWidth();
}

LayoutUnit LayoutBox::OverrideContentLogicalWidth() const {
  NOT_DESTROYED();
  return (OverrideLogicalWidth() - BorderAndPaddingLogicalWidth() -
          ComputeLogicalScrollbars().InlineSum())
      .ClampNegativeToZero();
}

LayoutUnit LayoutBox::OverrideContentLogicalHeight() const {
  NOT_DESTROYED();
  return (OverrideLogicalHeight() - BorderAndPaddingLogicalHeight() -
          ComputeLogicalScrollbars().BlockSum())
      .ClampNegativeToZero();
}

LayoutUnit LayoutBox::OverrideContainingBlockContentWidth() const {
  NOT_DESTROYED();
  DCHECK(HasOverrideContainingBlockContentWidth());
  return ContainingBlock()->StyleRef().IsHorizontalWritingMode()
             ? OverrideContainingBlockContentLogicalWidth()
             : OverrideContainingBlockContentLogicalHeight();
}

LayoutUnit LayoutBox::OverrideContainingBlockContentHeight() const {
  NOT_DESTROYED();
  DCHECK(HasOverrideContainingBlockContentHeight());
  return ContainingBlock()->StyleRef().IsHorizontalWritingMode()
             ? OverrideContainingBlockContentLogicalHeight()
             : OverrideContainingBlockContentLogicalWidth();
}

bool LayoutBox::HasOverrideContainingBlockContentWidth() const {
  NOT_DESTROYED();
  if (!ContainingBlock())
    return false;

  return ContainingBlock()->StyleRef().IsHorizontalWritingMode()
             ? HasOverrideContainingBlockContentLogicalWidth()
             : HasOverrideContainingBlockContentLogicalHeight();
}

bool LayoutBox::HasOverrideContainingBlockContentHeight() const {
  NOT_DESTROYED();
  if (!ContainingBlock())
    return false;

  return ContainingBlock()->StyleRef().IsHorizontalWritingMode()
             ? HasOverrideContainingBlockContentLogicalHeight()
             : HasOverrideContainingBlockContentLogicalWidth();
}

// TODO (lajava) Shouldn't we implement these functions based on physical
// direction ?.
LayoutUnit LayoutBox::OverrideContainingBlockContentLogicalWidth() const {
  NOT_DESTROYED();
  DCHECK(HasOverrideContainingBlockContentLogicalWidth());
  if (extra_input_)
    return extra_input_->containing_block_content_inline_size;
  return rare_data_->override_containing_block_content_logical_width_;
}

// TODO (lajava) Shouldn't we implement these functions based on physical
// direction ?.
LayoutUnit LayoutBox::OverrideContainingBlockContentLogicalHeight() const {
  NOT_DESTROYED();
  DCHECK(HasOverrideContainingBlockContentLogicalHeight());
  if (extra_input_)
    return extra_input_->containing_block_content_block_size;
  return rare_data_->override_containing_block_content_logical_height_;
}

// TODO (lajava) Shouldn't we implement these functions based on physical
// direction ?.
bool LayoutBox::HasOverrideContainingBlockContentLogicalWidth() const {
  NOT_DESTROYED();
  if (extra_input_)
    return true;
  return rare_data_ &&
         rare_data_->has_override_containing_block_content_logical_width_;
}

// TODO (lajava) Shouldn't we implement these functions based on physical
// direction ?.
bool LayoutBox::HasOverrideContainingBlockContentLogicalHeight() const {
  NOT_DESTROYED();
  if (extra_input_)
    return true;
  return rare_data_ &&
         rare_data_->has_override_containing_block_content_logical_height_;
}

// TODO (lajava) Shouldn't we implement these functions based on physical
// direction ?.
void LayoutBox::SetOverrideContainingBlockContentLogicalWidth(
    LayoutUnit logical_width) {
  NOT_DESTROYED();
  DCHECK(!extra_input_);
  DCHECK_GE(logical_width, LayoutUnit(-1));
  EnsureRareData().override_containing_block_content_logical_width_ =
      logical_width;
  EnsureRareData().has_override_containing_block_content_logical_width_ = true;
}

// TODO (lajava) Shouldn't we implement these functions based on physical
// direction ?.
void LayoutBox::SetOverrideContainingBlockContentLogicalHeight(
    LayoutUnit logical_height) {
  NOT_DESTROYED();
  DCHECK(!extra_input_);
  DCHECK_GE(logical_height, LayoutUnit(-1));
  EnsureRareData().override_containing_block_content_logical_height_ =
      logical_height;
  EnsureRareData().has_override_containing_block_content_logical_height_ = true;
}

// TODO (lajava) Shouldn't we implement these functions based on physical
// direction ?.
void LayoutBox::ClearOverrideContainingBlockContentSize() {
  NOT_DESTROYED();
  DCHECK(!extra_input_);
  if (!rare_data_)
    return;
  EnsureRareData().has_override_containing_block_content_logical_width_ = false;
  EnsureRareData().has_override_containing_block_content_logical_height_ =
      false;
}

LayoutUnit LayoutBox::OverridePercentageResolutionBlockSize() const {
  NOT_DESTROYED();
  DCHECK(HasOverridePercentageResolutionBlockSize());
  return rare_data_->override_percentage_resolution_block_size_;
}

bool LayoutBox::HasOverridePercentageResolutionBlockSize() const {
  NOT_DESTROYED();
  return rare_data_ &&
         rare_data_->has_override_percentage_resolution_block_size_;
}

void LayoutBox::SetOverridePercentageResolutionBlockSize(
    LayoutUnit logical_height) {
  NOT_DESTROYED();
  DCHECK_GE(logical_height, LayoutUnit(-1));
  auto& rare_data = EnsureRareData();
  rare_data.override_percentage_resolution_block_size_ = logical_height;
  rare_data.has_override_percentage_resolution_block_size_ = true;
}

void LayoutBox::ClearOverridePercentageResolutionBlockSize() {
  NOT_DESTROYED();
  if (!rare_data_)
    return;
  EnsureRareData().has_override_percentage_resolution_block_size_ = false;
}

LayoutUnit LayoutBox::OverrideAvailableInlineSize() const {
  NOT_DESTROYED();
  DCHECK(HasOverrideAvailableInlineSize());
  if (extra_input_)
    return extra_input_->available_inline_size;
  return LayoutUnit();
}

LayoutUnit LayoutBox::AdjustBorderBoxLogicalWidthForBoxSizing(
    float width) const {
  NOT_DESTROYED();
  LayoutUnit borders_plus_padding = CollapsedBorderAndCSSPaddingLogicalWidth();
  LayoutUnit result(width);
  if (StyleRef().BoxSizing() == EBoxSizing::kContentBox)
    return result + borders_plus_padding;
  return std::max(result, borders_plus_padding);
}

LayoutUnit LayoutBox::AdjustBorderBoxLogicalHeightForBoxSizing(
    float height) const {
  NOT_DESTROYED();
  LayoutUnit borders_plus_padding = CollapsedBorderAndCSSPaddingLogicalHeight();
  LayoutUnit result(height);
  if (StyleRef().BoxSizing() == EBoxSizing::kContentBox)
    return result + borders_plus_padding;
  return std::max(result, borders_plus_padding);
}

LayoutUnit LayoutBox::AdjustContentBoxLogicalWidthForBoxSizing(
    float width) const {
  NOT_DESTROYED();
  LayoutUnit result(width);
  if (StyleRef().BoxSizing() == EBoxSizing::kBorderBox)
    result -= CollapsedBorderAndCSSPaddingLogicalWidth();
  return std::max(LayoutUnit(), result);
}

LayoutUnit LayoutBox::AdjustContentBoxLogicalHeightForBoxSizing(
    float height) const {
  NOT_DESTROYED();
  LayoutUnit result(height);
  if (StyleRef().BoxSizing() == EBoxSizing::kBorderBox)
    result -= CollapsedBorderAndCSSPaddingLogicalHeight();
  return std::max(LayoutUnit(), result);
}

bool LayoutBox::HitTestAllPhases(HitTestResult& result,
                                 const HitTestLocation& hit_test_location,
                                 const PhysicalOffset& accumulated_offset) {
  NOT_DESTROYED();
  if (!MayIntersect(result, hit_test_location, accumulated_offset))
    return false;
  return LayoutObject::HitTestAllPhases(result, hit_test_location,
                                        accumulated_offset);
}

bool LayoutBox::HitTestOverflowControl(
    HitTestResult& result,
    const HitTestLocation& hit_test_location,
    const PhysicalOffset& adjusted_location) const {
  NOT_DESTROYED();

  auto* scrollable_area = GetScrollableArea();
  if (!scrollable_area)
    return false;

  if (!VisibleToHitTestRequest(result.GetHitTestRequest()))
    return false;

  PhysicalOffset local_point = hit_test_location.Point() - adjusted_location;
  if (!scrollable_area->HitTestOverflowControls(result,
                                                ToRoundedPoint(local_point)))
    return false;

  UpdateHitTestResult(result, local_point);
  return result.AddNodeToListBasedTestResult(
             NodeForHitTest(), hit_test_location) == kStopHitTesting;
}

bool LayoutBox::NodeAtPoint(HitTestResult& result,
                            const HitTestLocation& hit_test_location,
                            const PhysicalOffset& accumulated_offset,
                            HitTestPhase phase) {
  NOT_DESTROYED();
  if (!MayIntersect(result, hit_test_location, accumulated_offset))
    return false;

  if (phase == HitTestPhase::kForeground && !HasSelfPaintingLayer() &&
      HitTestOverflowControl(result, hit_test_location, accumulated_offset))
    return true;

  bool skip_children = (result.GetHitTestRequest().GetStopNode() == this) ||
                       ChildPaintBlockedByDisplayLock();
  if (!skip_children && ShouldClipOverflowAlongEitherAxis()) {
    // PaintLayer::HitTestFragmentsWithPhase() checked the fragments'
    // foreground rect for intersection if a layer is self painting,
    // so only do the overflow clip check here for non-self-painting layers.
    if (!HasSelfPaintingLayer() &&
        !hit_test_location.Intersects(OverflowClipRect(
            accumulated_offset, kExcludeOverlayScrollbarSizeForHitTesting))) {
      skip_children = true;
    }
    if (!skip_children && StyleRef().HasBorderRadius()) {
      PhysicalRect bounds_rect(accumulated_offset, Size());
      skip_children = !hit_test_location.Intersects(
          RoundedBorderGeometry::PixelSnappedRoundedInnerBorder(StyleRef(),
                                                                bounds_rect));
    }
  }

  if (!skip_children &&
      HitTestChildren(result, hit_test_location, accumulated_offset, phase)) {
    return true;
  }

  if (StyleRef().HasBorderRadius() &&
      HitTestClippedOutByBorder(hit_test_location, accumulated_offset))
    return false;

  // Now hit test ourselves.
  if (IsInSelfHitTestingPhase(phase) &&
      VisibleToHitTestRequest(result.GetHitTestRequest())) {
    PhysicalRect bounds_rect;
    if (UNLIKELY(result.GetHitTestRequest().IsHitTestVisualOverflow())) {
      bounds_rect = PhysicalVisualOverflowRectIncludingFilters();
    } else {
      bounds_rect = PhysicalBorderBoxRect();
    }
    bounds_rect.Move(accumulated_offset);
    if (hit_test_location.Intersects(bounds_rect)) {
      UpdateHitTestResult(result,
                          hit_test_location.Point() - accumulated_offset);
      if (result.AddNodeToListBasedTestResult(NodeForHitTest(),
                                              hit_test_location,
                                              bounds_rect) == kStopHitTesting)
        return true;
    }
  }

  return false;
}

bool LayoutBox::HitTestChildren(HitTestResult& result,
                                const HitTestLocation& hit_test_location,
                                const PhysicalOffset& accumulated_offset,
                                HitTestPhase phase) {
  NOT_DESTROYED();
  for (LayoutObject* child = SlowLastChild(); child;
       child = child->PreviousSibling()) {
    if (child->HasLayer() &&
        To<LayoutBoxModelObject>(child)->Layer()->IsSelfPaintingLayer())
      continue;

    PhysicalOffset child_accumulated_offset = accumulated_offset;
    if (auto* box = DynamicTo<LayoutBox>(child))
      child_accumulated_offset += box->PhysicalLocation(this);

    if (child->NodeAtPoint(result, hit_test_location, child_accumulated_offset,
                           phase))
      return true;
  }

  return false;
}

bool LayoutBox::HitTestClippedOutByBorder(
    const HitTestLocation& hit_test_location,
    const PhysicalOffset& border_box_location) const {
  NOT_DESTROYED();
  PhysicalRect border_rect = PhysicalBorderBoxRect();
  border_rect.Move(border_box_location);
  return !hit_test_location.Intersects(
      RoundedBorderGeometry::PixelSnappedRoundedBorder(StyleRef(),
                                                       border_rect));
}

void LayoutBox::Paint(const PaintInfo& paint_info) const {
  NOT_DESTROYED();
  BoxPainter(*this).Paint(paint_info);
}

void LayoutBox::PaintBoxDecorationBackground(
    const PaintInfo& paint_info,
    const PhysicalOffset& paint_offset) const {
  NOT_DESTROYED();
  BoxPainter(*this).PaintBoxDecorationBackground(paint_info, paint_offset);
}

PhysicalRect LayoutBox::BackgroundPaintedExtent() const {
  NOT_DESTROYED();
  return PhysicalBackgroundRect(kBackgroundPaintedExtent);
}

bool LayoutBox::BackgroundIsKnownToBeOpaqueInRect(
    const PhysicalRect& local_rect) const {
  NOT_DESTROYED();
  // If the element has appearance, it might be painted by theme.
  // We cannot be sure if theme paints the background opaque.
  // In this case it is safe to not assume opaqueness.
  // FIXME: May be ask theme if it paints opaque.
  if (StyleRef().HasEffectiveAppearance())
    return false;
  // FIXME: Check the opaqueness of background images.

  // FIXME: Use rounded rect if border radius is present.
  if (StyleRef().HasBorderRadius())
    return false;
  if (HasClipPath())
    return false;
  if (StyleRef().HasBlendMode())
    return false;
  return PhysicalBackgroundRect(kBackgroundKnownOpaqueRect)
      .Contains(local_rect);
}

// TODO(wangxianzhu): The current rules are very basic. May use more complex
// rules if they can improve LCD text.
bool LayoutBox::TextIsKnownToBeOnOpaqueBackground() const {
  NOT_DESTROYED();
  // Text may overflow the background area.
  if (!ShouldClipOverflowAlongEitherAxis())
    return false;
  // Same as BackgroundIsKnownToBeOpaqueInRect() about appearance.
  if (StyleRef().HasEffectiveAppearance())
    return false;

  PhysicalRect rect = OverflowClipRect(PhysicalOffset());
  return PhysicalBackgroundRect(kBackgroundKnownOpaqueRect).Contains(rect);
}

static bool IsCandidateForOpaquenessTest(const LayoutBox& child_box) {
  // Skip all layers to simplify ForegroundIsKnownToBeOpaqueInRect(). This
  // covers cases of clipped, transformed, translucent, composited, etc.
  if (child_box.HasLayer())
    return false;
  const ComputedStyle& child_style = child_box.StyleRef();
  if (child_style.Visibility() != EVisibility::kVisible ||
      child_style.ShapeOutside())
    return false;
  if (child_box.Size().IsZero())
    return false;
  // A replaced element with border-radius always clips the content.
  if (child_box.IsLayoutReplaced() && child_style.HasBorderRadius())
    return false;
  return true;
}

bool LayoutBox::ForegroundIsKnownToBeOpaqueInRect(
    const PhysicalRect& local_rect,
    unsigned max_depth_to_test) const {
  NOT_DESTROYED();
  if (!max_depth_to_test)
    return false;
  for (LayoutObject* child = SlowFirstChild(); child;
       child = child->NextSibling()) {
    // We do not bother checking descendants of |LayoutInline|, including
    // block-in-inline, because the cost of checking them overweights the
    // benefits.
    if (!child->IsBox())
      continue;
    auto* child_box = To<LayoutBox>(child);
    if (!IsCandidateForOpaquenessTest(*child_box))
      continue;
    DCHECK(!child_box->IsPositioned());
    PhysicalRect child_local_rect = local_rect;
    child_local_rect.Move(-child_box->PhysicalLocation());
    if (child_local_rect.Y() < 0 || child_local_rect.X() < 0) {
      // If there is unobscured area above/left of a static positioned box then
      // the rect is probably not covered. This can cause false-negative in
      // non-horizontal-tb writing mode but is allowed.
      return false;
    }
    if (child_local_rect.Bottom() > child_box->Size().Height() ||
        child_local_rect.Right() > child_box->Size().Width())
      continue;
    if (RuntimeEnabledFeatures::CompositeBGColorAnimationEnabled() &&
        child->Style()->HasCurrentBackgroundColorAnimation()) {
      return false;
    }
    if (child_box->BackgroundIsKnownToBeOpaqueInRect(child_local_rect))
      return true;
    if (child_box->ForegroundIsKnownToBeOpaqueInRect(child_local_rect,
                                                     max_depth_to_test - 1))
      return true;
  }
  return false;
}

DISABLE_CFI_PERF
bool LayoutBox::ComputeBackgroundIsKnownToBeObscured() const {
  NOT_DESTROYED();
  if (ScrollsOverflow())
    return false;
  // Test to see if the children trivially obscure the background.
  if (!StyleRef().HasBackground())
    return false;
  // Root background painting is special.
  if (IsA<LayoutView>(this))
    return false;
  if (StyleRef().BoxShadow())
    return false;
  return ForegroundIsKnownToBeOpaqueInRect(BackgroundPaintedExtent(),
                                           kBackgroundObscurationTestMaxDepth);
}

void LayoutBox::PaintMask(const PaintInfo& paint_info,
                          const PhysicalOffset& paint_offset) const {
  NOT_DESTROYED();
  BoxPainter(*this).PaintMask(paint_info, paint_offset);
}

void LayoutBox::ImageChanged(WrappedImagePtr image,
                             CanDeferInvalidation defer) {
  NOT_DESTROYED();
  bool is_box_reflect_image =
      (StyleRef().BoxReflect() && StyleRef().BoxReflect()->Mask().GetImage() &&
       StyleRef().BoxReflect()->Mask().GetImage()->Data() == image);

  if (is_box_reflect_image && HasLayer()) {
    Layer()->SetFilterOnEffectNodeDirty();
    SetNeedsPaintPropertyUpdate();
  }

  // TODO(chrishtr): support delayed paint invalidation for animated border
  // images.
  if ((StyleRef().BorderImage().GetImage() &&
       StyleRef().BorderImage().GetImage()->Data() == image) ||
      (StyleRef().MaskBoxImage().GetImage() &&
       StyleRef().MaskBoxImage().GetImage()->Data() == image) ||
      is_box_reflect_image) {
    SetShouldDoFullPaintInvalidationWithoutGeometryChange(
        PaintInvalidationReason::kImage);
  } else {
    for (const FillLayer* layer = &StyleRef().MaskLayers(); layer;
         layer = layer->Next()) {
      if (layer->GetImage() && image == layer->GetImage()->Data()) {
        SetShouldDoFullPaintInvalidationWithoutGeometryChange(
            PaintInvalidationReason::kImage);
        break;
      }
    }
  }

  if (!BackgroundTransfersToView()) {
    for (const FillLayer* layer = &StyleRef().BackgroundLayers(); layer;
         layer = layer->Next()) {
      if (layer->GetImage() && image == layer->GetImage()->Data()) {
        bool maybe_animated =
            layer->GetImage()->CachedImage() &&
            layer->GetImage()->CachedImage()->GetImage() &&
            layer->GetImage()->CachedImage()->GetImage()->MaybeAnimated();
        if (defer == CanDeferInvalidation::kYes && maybe_animated)
          SetMayNeedPaintInvalidationAnimatedBackgroundImage();
        else
          SetBackgroundNeedsFullPaintInvalidation();
        break;
      }
    }
  }

  ShapeValue* shape_outside_value = StyleRef().ShapeOutside();
  if (!GetFrameView()->IsInPerformLayout() && IsFloating() &&
      shape_outside_value && shape_outside_value->GetImage() &&
      shape_outside_value->GetImage()->Data() == image) {
    ShapeOutsideInfo& info = ShapeOutsideInfo::EnsureInfo(*this);
    if (!info.IsComputingShape()) {
      info.MarkShapeAsDirty();
      MarkShapeOutsideDependentsForLayout();
    }
  }
}

ResourcePriority LayoutBox::ComputeResourcePriority() const {
  NOT_DESTROYED();
  PhysicalRect view_bounds = ViewRect();
  PhysicalRect object_bounds = PhysicalContentBoxRect();
  // TODO(japhet): Is this IgnoreTransforms correct? Would it be better to use
  // the visual rect (which has ancestor clips and transforms applied)? Should
  // we map to the top-level viewport instead of the current (sub) frame?
  object_bounds.Move(LocalToAbsolutePoint(PhysicalOffset(), kIgnoreTransforms));

  // The object bounds might be empty right now, so intersects will fail since
  // it doesn't deal with empty rects. Use LayoutRect::contains in that case.
  bool is_visible;
  if (!object_bounds.IsEmpty())
    is_visible = view_bounds.Intersects(object_bounds);
  else
    is_visible = view_bounds.Contains(object_bounds);

  PhysicalRect screen_rect;
  if (!object_bounds.IsEmpty()) {
    screen_rect = view_bounds;
    screen_rect.Intersect(object_bounds);
  }

  int screen_area = 0;
  if (!screen_rect.IsEmpty() && is_visible)
    screen_area = (screen_rect.Width() * screen_rect.Height()).ToInt();
  return ResourcePriority(
      is_visible ? ResourcePriority::kVisible : ResourcePriority::kNotVisible,
      screen_area);
}

void LayoutBox::LocationChanged() {
  NOT_DESTROYED();
  // The location may change because of layout of other objects. Should check
  // this object for paint invalidation.
  if (!NeedsLayout())
    SetShouldCheckForPaintInvalidation();
}

void LayoutBox::SizeChanged() {
  NOT_DESTROYED();
  // The size may change because of layout of other objects. Should check this
  // object for paint invalidation.
  if (!NeedsLayout())
    SetShouldCheckForPaintInvalidation();
  // In flipped blocks writing mode, our children can change physical location,
  // but their flipped location remains the same.
  if (HasFlippedBlocksWritingMode()) {
    if (ChildrenInline())
      SetSubtreeShouldDoFullPaintInvalidation();
    else
      SetSubtreeShouldCheckForPaintInvalidation();
  }
}

bool LayoutBox::IntersectsVisibleViewport() const {
  NOT_DESTROYED();
  PhysicalRect rect = PhysicalVisualOverflowRect();
  LayoutView* layout_view = View();
  while (layout_view->GetFrame()->OwnerLayoutObject())
    layout_view = layout_view->GetFrame()->OwnerLayoutObject()->View();
  MapToVisualRectInAncestorSpace(layout_view, rect);
  return rect.Intersects(PhysicalRect(
      layout_view->GetFrameView()->GetScrollableArea()->VisibleContentRect()));
}

void LayoutBox::EnsureIsReadyForPaintInvalidation() {
  NOT_DESTROYED();
  LayoutBoxModelObject::EnsureIsReadyForPaintInvalidation();

  bool new_obscured = ComputeBackgroundIsKnownToBeObscured();
  if (BackgroundIsKnownToBeObscured() != new_obscured) {
    SetBackgroundIsKnownToBeObscured(new_obscured);
    SetBackgroundNeedsFullPaintInvalidation();
  }

  if (MayNeedPaintInvalidationAnimatedBackgroundImage() &&
      !BackgroundIsKnownToBeObscured()) {
    SetBackgroundNeedsFullPaintInvalidation();
    SetShouldDelayFullPaintInvalidation();
  }

  if (ShouldDelayFullPaintInvalidation() && IntersectsVisibleViewport()) {
    // Do regular full paint invalidation if the object with delayed paint
    // invalidation is on screen.
    ClearShouldDelayFullPaintInvalidation();
    DCHECK(ShouldDoFullPaintInvalidation());
  }
}

void LayoutBox::InvalidatePaint(const PaintInvalidatorContext& context) const {
  NOT_DESTROYED();
  BoxPaintInvalidator(*this, context).InvalidatePaint();
}

void LayoutBox::ClearPaintFlags() {
  NOT_DESTROYED();
  LayoutObject::ClearPaintFlags();

  if (auto* scrollable_area = GetScrollableArea()) {
    if (auto* scrollbar =
            DynamicTo<CustomScrollbar>(scrollable_area->HorizontalScrollbar()))
      scrollbar->ClearPaintFlags();
    if (auto* scrollbar =
            DynamicTo<CustomScrollbar>(scrollable_area->VerticalScrollbar()))
      scrollbar->ClearPaintFlags();
  }
}

PhysicalRect LayoutBox::OverflowClipRect(
    const PhysicalOffset& location,
    OverlayScrollbarClipBehavior overlay_scrollbar_clip_behavior) const {
  NOT_DESTROYED();
  PhysicalRect clip_rect;

  if (IsEffectiveRootScroller()) {
    // If this box is the effective root scroller, use the viewport clipping
    // rect since it will account for the URL bar correctly which the border
    // box does not. We can do this because the effective root scroller is
    // restricted such that it exactly fills the viewport. See
    // RootScrollerController::IsValidRootScroller()
    clip_rect = PhysicalRect(location, View()->ViewRect().size);
  } else {
    clip_rect = PhysicalBorderBoxRect();
    clip_rect.Contract(BorderBoxOutsets());
    clip_rect.Move(location);

    // Videos need to be pre-snapped so that they line up with the
    // display_rect and can enable hardware overlays.
    // Embedded objects are always sized to fit the content rect, but they
    // could overflow by 1px due to pre-snapping. Adjust clip rect to
    // match pre-snapped box as a special case.
    if (IsVideo() || IsLayoutEmbeddedContent())
      clip_rect = LayoutReplaced::PreSnappedRectForPersistentSizing(clip_rect);

    if (HasNonVisibleOverflow()) {
      const auto overflow_clip = GetOverflowClipAxes();
      if (overflow_clip != kOverflowClipBothAxis) {
        ApplyVisibleOverflowToClipRect(overflow_clip, clip_rect);
      } else if (ShouldApplyOverflowClipMargin()) {
        switch (StyleRef().OverflowClipMargin()->GetReferenceBox()) {
          case StyleOverflowClipMargin::ReferenceBox::kBorderBox:
            clip_rect.Expand(BorderBoxOutsets());
            break;
          case StyleOverflowClipMargin::ReferenceBox::kPaddingBox:
            break;
          case StyleOverflowClipMargin::ReferenceBox::kContentBox:
            clip_rect.Contract(PaddingOutsets());
            break;
        }
        clip_rect.Inflate(StyleRef().OverflowClipMargin()->GetMargin());
      }
    }
  }

  if (IsScrollContainer()) {
    // The additional gutters created by scrollbar-gutter don't occlude the
    // content underneath, so they should not be clipped out here.
    // See https://crbug.com/710214
    ExcludeScrollbars(clip_rect, overlay_scrollbar_clip_behavior,
                      kExcludeScrollbarGutter);
  }

  auto* input = DynamicTo<HTMLInputElement>(GetNode());
  if (UNLIKELY(input)) {
    // As for LayoutButton, ControlClip is to for not BUTTONs but INPUT
    // buttons for IE/Firefox compatibility.
    if (IsTextFieldIncludingNG() || IsButtonIncludingNG()) {
      DCHECK(HasControlClip());
      PhysicalRect control_clip = PhysicalPaddingBoxRect();
      control_clip.Move(location);
      clip_rect.Intersect(control_clip);
    }
  } else if (UNLIKELY(IsMenuList(this))) {
    DCHECK(HasControlClip());
    PhysicalRect control_clip = PhysicalContentBoxRect();
    control_clip.Move(location);
    clip_rect.Intersect(control_clip);
  } else {
    DCHECK(!HasControlClip());
  }

  return clip_rect;
}

bool LayoutBox::HasControlClip() const {
  NOT_DESTROYED();
  return UNLIKELY(IsTextFieldIncludingNG() || IsFileUploadControl() ||
                  IsMenuList(this) ||
                  (IsButtonIncludingNG() && IsA<HTMLInputElement>(GetNode())));
}

void LayoutBox::ExcludeScrollbars(
    PhysicalRect& rect,
    OverlayScrollbarClipBehavior overlay_scrollbar_clip_behavior,
    ShouldIncludeScrollbarGutter include_scrollbar_gutter) const {
  NOT_DESTROYED();
  if (CanSkipComputeScrollbars())
    return;

  NGPhysicalBoxStrut scrollbars = ComputeScrollbarsInternal(
      kDoNotClampToContentBox, overlay_scrollbar_clip_behavior,
      include_scrollbar_gutter);
  rect.offset.top += scrollbars.top;
  rect.offset.left += scrollbars.left;
  rect.size.width -= scrollbars.HorizontalSum();
  rect.size.height -= scrollbars.VerticalSum();
  rect.size.ClampNegativeToZero();
}

PhysicalRect LayoutBox::ClipRect(const PhysicalOffset& location) const {
  NOT_DESTROYED();
  PhysicalRect clip_rect(location, Size());
  LayoutUnit width = Size().Width();
  LayoutUnit height = Size().Height();

  if (!StyleRef().ClipLeft().IsAuto()) {
    LayoutUnit c = ValueForLength(StyleRef().ClipLeft(), width);
    clip_rect.offset.left += c;
    clip_rect.size.width -= c;
  }

  if (!StyleRef().ClipRight().IsAuto()) {
    clip_rect.size.width -=
        width - ValueForLength(StyleRef().ClipRight(), width);
  }

  if (!StyleRef().ClipTop().IsAuto()) {
    LayoutUnit c = ValueForLength(StyleRef().ClipTop(), height);
    clip_rect.offset.top += c;
    clip_rect.size.height -= c;
  }

  if (!StyleRef().ClipBottom().IsAuto()) {
    clip_rect.size.height -=
        height - ValueForLength(StyleRef().ClipBottom(), height);
  }

  return clip_rect;
}

static LayoutUnit PortionOfMarginNotConsumedByFloat(LayoutUnit child_margin,
                                                    LayoutUnit content_side,
                                                    LayoutUnit offset) {
  if (child_margin <= 0)
    return LayoutUnit();
  LayoutUnit content_side_with_margin = content_side + child_margin;
  if (offset > content_side_with_margin)
    return child_margin;
  return offset - content_side;
}

LayoutUnit LayoutBox::ShrinkLogicalWidthToAvoidFloats(
    LayoutUnit child_margin_start,
    LayoutUnit child_margin_end,
    const LayoutBlockFlow* cb) const {
  NOT_DESTROYED();
  LayoutUnit logical_top_position = LogicalTop();
  LayoutUnit start_offset_for_content = cb->StartOffsetForContent();
  LayoutUnit end_offset_for_content = cb->EndOffsetForContent();

  // NOTE: This call to LogicalHeightForChild is bad, as it may contain data
  // from a previous layout.
  LayoutUnit logical_height = cb->LogicalHeightForChild(*this);
  LayoutUnit start_offset_for_avoiding_floats =
      cb->StartOffsetForAvoidingFloats(logical_top_position, logical_height);
  LayoutUnit end_offset_for_avoiding_floats =
      cb->EndOffsetForAvoidingFloats(logical_top_position, logical_height);

  // If there aren't any floats constraining us then allow the margins to
  // shrink/expand the width as much as they want.
  if (start_offset_for_content == start_offset_for_avoiding_floats &&
      end_offset_for_content == end_offset_for_avoiding_floats)
    return cb->AvailableLogicalWidthForAvoidingFloats(logical_top_position,
                                                      logical_height) -
           child_margin_start - child_margin_end;

  LayoutUnit width = cb->AvailableLogicalWidthForAvoidingFloats(
      logical_top_position, logical_height);
  width -= std::max(LayoutUnit(), child_margin_start);
  width -= std::max(LayoutUnit(), child_margin_end);

  // We need to see if margins on either the start side or the end side can
  // contain the floats in question. If they can, then just using the line width
  // is inaccurate. In the case where a float completely fits, we don't need to
  // use the line offset at all, but can instead push all the way to the content
  // edge of the containing block. In the case where the float doesn't fit, we
  // can use the line offset, but we need to grow it by the margin to reflect
  // the fact that the margin was "consumed" by the float. Negative margins
  // aren't consumed by the float, and so we ignore them.
  width += PortionOfMarginNotConsumedByFloat(child_margin_start,
                                             start_offset_for_content,
                                             start_offset_for_avoiding_floats);
  width += PortionOfMarginNotConsumedByFloat(
      child_margin_end, end_offset_for_content, end_offset_for_avoiding_floats);
  return width;
}

LayoutUnit LayoutBox::ContainingBlockLogicalHeightForGetComputedStyle() const {
  NOT_DESTROYED();
  if (HasOverrideContainingBlockContentLogicalHeight())
    return OverrideContainingBlockContentLogicalHeight();

  if (!IsPositioned())
    return ContainingBlockLogicalHeightForContent(kExcludeMarginBorderPadding);

  auto* cb = To<LayoutBoxModelObject>(Container());
  LayoutUnit height = ContainingBlockLogicalHeightForPositioned(
      cb, /* check_for_perpendicular_writing_mode */ false);
  if (IsInFlowPositioned())
    height -= cb->PaddingLogicalHeight();
  return height;
}

LayoutUnit LayoutBox::ContainingBlockLogicalWidthForContent() const {
  NOT_DESTROYED();
  if (HasOverrideContainingBlockContentLogicalWidth())
    return OverrideContainingBlockContentLogicalWidth();

  LayoutBlock* cb = ContainingBlock();
  if (IsOutOfFlowPositioned())
    return cb->ClientLogicalWidth();
  return cb->AvailableLogicalWidth();
}

LayoutUnit LayoutBox::ContainingBlockLogicalHeightForContent(
    AvailableLogicalHeightType height_type) const {
  NOT_DESTROYED();
  if (HasOverrideContainingBlockContentLogicalHeight())
    return OverrideContainingBlockContentLogicalHeight();

  LayoutBlock* cb = ContainingBlock();
  return cb->AvailableLogicalHeight(height_type);
}

LayoutUnit LayoutBox::ContainingBlockAvailableLineWidth() const {
  NOT_DESTROYED();
  LayoutBlock* cb = ContainingBlock();
  auto* child_block_flow = DynamicTo<LayoutBlockFlow>(cb);
  if (child_block_flow) {
    return child_block_flow->AvailableLogicalWidthForAvoidingFloats(
        LogicalTop(), AvailableLogicalHeight(kIncludeMarginBorderPadding));
  }
  return LayoutUnit();
}

LayoutUnit LayoutBox::PerpendicularContainingBlockLogicalHeight() const {
  NOT_DESTROYED();
  if (HasOverrideContainingBlockContentLogicalHeight())
    return OverrideContainingBlockContentLogicalHeight();

  LayoutBlock* cb = ContainingBlock();
  if (cb->HasOverrideLogicalHeight())
    return cb->OverrideContentLogicalHeight();

  const ComputedStyle& containing_block_style = cb->StyleRef();
  const Length& logical_height_length = containing_block_style.LogicalHeight();

  // FIXME: For now just support fixed heights.  Eventually should support
  // percentage heights as well.
  if (!logical_height_length.IsFixed()) {
    LayoutUnit fill_fallback_extent =
        LayoutUnit(containing_block_style.IsHorizontalWritingMode()
                       ? View()->GetFrameView()->Size().height()
                       : View()->GetFrameView()->Size().width());
    LayoutUnit fill_available_extent =
        ContainingBlock()->AvailableLogicalHeight(kExcludeMarginBorderPadding);
    if (fill_available_extent == -1)
      return fill_fallback_extent;
    return std::min(fill_available_extent, fill_fallback_extent);
  }

  // Use the content box logical height as specified by the style.
  return cb->AdjustContentBoxLogicalHeightForBoxSizing(
      LayoutUnit(logical_height_length.Value()));
}

PhysicalOffset LayoutBox::OffsetFromContainerInternal(
    const LayoutObject* o,
    bool ignore_scroll_offset) const {
  NOT_DESTROYED();
  DCHECK_EQ(o, Container());

  PhysicalOffset offset;
  if (IsInFlowPositioned())
    offset += OffsetForInFlowPosition();

  offset += PhysicalLocation();

  if (o->IsScrollContainer())
    offset += OffsetFromScrollableContainer(o, ignore_scroll_offset);

  if (IsOutOfFlowPositioned() && o->IsLayoutInline() &&
      o->CanContainOutOfFlowPositionedElement(StyleRef().GetPosition())) {
    offset += To<LayoutInline>(o)->OffsetForInFlowPositionedInline(*this);
  }

  if (AnchorScrollObject())
    offset += ComputeAnchorScrollOffset();

  return offset;
}

InlineBox* LayoutBox::CreateInlineBox() {
  NOT_DESTROYED();
  return MakeGarbageCollected<InlineBox>(LineLayoutItem(this));
}

void LayoutBox::DirtyLineBoxes(bool full_layout) {
  NOT_DESTROYED();
  if (!IsInLayoutNGInlineFormattingContext() && inline_box_wrapper_) {
    if (full_layout) {
      inline_box_wrapper_->Destroy();
      inline_box_wrapper_ = nullptr;
    } else {
      inline_box_wrapper_->DirtyLineBoxes();
    }
  }
}

bool LayoutBox::HasInlineFragments() const {
  NOT_DESTROYED();
  if (!IsInLayoutNGInlineFormattingContext())
    return inline_box_wrapper_;
  return first_fragment_item_index_;
}

void LayoutBox::ClearFirstInlineFragmentItemIndex() {
  NOT_DESTROYED();
  CHECK(IsInLayoutNGInlineFormattingContext()) << *this;
  first_fragment_item_index_ = 0u;
}

void LayoutBox::SetFirstInlineFragmentItemIndex(wtf_size_t index) {
  NOT_DESTROYED();
  CHECK(IsInLayoutNGInlineFormattingContext()) << *this;
  DCHECK_NE(index, 0u);
  first_fragment_item_index_ = index;
}

void LayoutBox::InLayoutNGInlineFormattingContextWillChange(bool new_value) {
  NOT_DESTROYED();
  if (IsInLayoutNGInlineFormattingContext())
    ClearFirstInlineFragmentItemIndex();
  else
    DeleteLineBoxWrapper();

  // Because |first_fragment_item_index_| and |inline_box_wrapper_| are union,
  // when one is deleted, the other should be initialized to nullptr.
  DCHECK(new_value ? !first_fragment_item_index_ : !inline_box_wrapper_);
}

bool LayoutBox::NGPhysicalFragmentList::HasFragmentItems() const {
  for (const NGPhysicalBoxFragment& fragment : *this) {
    if (fragment.HasItems())
      return true;
  }
  return false;
}

wtf_size_t LayoutBox::NGPhysicalFragmentList::IndexOf(
    const NGPhysicalBoxFragment& fragment) const {
  wtf_size_t index = 0;
  for (const auto& result : layout_results_) {
    if (&result->PhysicalFragment() == &fragment)
      return index;
    ++index;
  }
  return kNotFound;
}

bool LayoutBox::NGPhysicalFragmentList::Contains(
    const NGPhysicalBoxFragment& fragment) const {
  return IndexOf(fragment) != kNotFound;
}

void LayoutBox::SetCachedLayoutResult(const NGLayoutResult* result) {
  NOT_DESTROYED();
  DCHECK(!result->PhysicalFragment().BreakToken());
  DCHECK(To<NGPhysicalBoxFragment>(result->PhysicalFragment()).IsOnlyForNode());

  if (result->GetConstraintSpaceForCaching().CacheSlot() ==
      NGCacheSlot::kMeasure) {
    // We don't early return here, when setting the "measure" result we also
    // set the "layout" result.
    if (measure_result_)
      InvalidateItems(*measure_result_);
    if (IsTableCell() && !IsTableCellLegacy())
      To<LayoutNGTableCell>(this)->InvalidateLayoutResultCacheAfterMeasure();
    measure_result_ = result;
  } else {
    // We have a "layout" result, and we may need to clear the old "measure"
    // result if we needed non-simplified layout.
    if (measure_result_ && NeedsLayout() && !NeedsSimplifiedLayoutOnly()) {
      InvalidateItems(*measure_result_);
      measure_result_ = nullptr;
    }
  }

  // If we're about to cache a layout result that is different than the measure
  // result, mark the measure result's fragment as no longer having valid
  // children. It can still be used to query information about this box's
  // fragment from the measure pass, but children might be out of sync with the
  // latest version of the tree.
  if (measure_result_ && measure_result_ != result) {
    measure_result_->GetMutableForLayoutBoxCachedResults()
        .SetFragmentChildrenInvalid();
  }

  SetLayoutResult(std::move(result), 0);
}

void LayoutBox::SetLayoutResult(const NGLayoutResult* result,
                                wtf_size_t index) {
  NOT_DESTROYED();
  DCHECK_EQ(result->Status(), NGLayoutResult::kSuccess);
  const auto& box_fragment =
      To<NGPhysicalBoxFragment>(result->PhysicalFragment());

  if (index != WTF::kNotFound && layout_results_.size() > index) {
    if (layout_results_.size() > index + 1) {
      // If we have reached the end, remove surplus results from previous
      // layout.
      //
      // Note: When an OOF is fragmented, we wait to lay it out at the
      // fragmentation context root. If the OOF lives above a column spanner,
      // though, we may lay it out early to make sure the OOF contributes to the
      // correct column block-size. Thus, if an item broke as a result of a
      // spanner, remove subsequent sibling items so that OOFs don't try to
      // access old fragments.
      //
      // Additionally, if an outer multicol has a spanner break, we may try
      // to access old fragments of the inner multicol if it hasn't completed
      // layout yet. Remove subsequent multicol fragments to avoid OOFs from
      // trying to access old fragments.
      //
      // TODO(layout-dev): Other solutions to handling interactions between OOFs
      // and spanner breaks may need to be considered.
      if (!box_fragment.BreakToken() ||
          box_fragment.BreakToken()->IsCausedByColumnSpanner() ||
          box_fragment.IsFragmentationContextRoot()) {
        // Before forgetting any old fragments and their items, we need to clear
        // associations.
        if (box_fragment.IsInlineFormattingContext())
          NGFragmentItems::ClearAssociatedFragments(this);
        ShrinkLayoutResults(index + 1);
      }
    }
    ReplaceLayoutResult(std::move(result), index);
    return;
  }

  DCHECK(index == layout_results_.size() || index == kNotFound);
  AppendLayoutResult(result);

  if (!box_fragment.BreakToken())
    FinalizeLayoutResults();
}

void LayoutBox::AppendLayoutResult(const NGLayoutResult* result) {
  const auto& fragment = To<NGPhysicalBoxFragment>(result->PhysicalFragment());
  // |layout_results_| is particularly critical when side effects are disabled.
  DCHECK(!NGDisableSideEffectsScope::IsDisabled());
  layout_results_.push_back(std::move(result));
  CheckDidAddFragment(*this, fragment);

  if (layout_results_.size() > 1)
    FragmentCountOrSizeDidChange();
}

void LayoutBox::ReplaceLayoutResult(const NGLayoutResult* result,
                                    wtf_size_t index) {
  NOT_DESTROYED();
  DCHECK_LE(index, layout_results_.size());
  const NGLayoutResult* old_result = layout_results_[index];
  if (old_result == result)
    return;
  const auto& fragment = To<NGPhysicalBoxFragment>(result->PhysicalFragment());
  const auto& old_fragment = old_result->PhysicalFragment();
  bool got_new_fragment = &old_fragment != &fragment;
  if (got_new_fragment) {
    if (HasFragmentItems()) {
      if (!index)
        InvalidateItems(*old_result);
      NGFragmentItems::ClearAssociatedFragments(this);
    }
    if (layout_results_.size() > 1) {
      if (fragment.Size() != old_fragment.Size())
        FragmentCountOrSizeDidChange();
    }
  }
  // |layout_results_| is particularly critical when side effects are disabled.
  DCHECK(!NGDisableSideEffectsScope::IsDisabled());
  layout_results_[index] = std::move(result);
  CheckDidAddFragment(*this, fragment, index);

  if (got_new_fragment && !fragment.BreakToken()) {
    // If this is the last result, the results vector better agree on that.
    DCHECK_EQ(index, layout_results_.size() - 1);

    FinalizeLayoutResults();
  }
}

void LayoutBox::RestoreLegacyLayoutResults(
    const NGLayoutResult* measure_result,
    const NGLayoutResult* layout_result) {
  NOT_DESTROYED();
  DCHECK(!IsLayoutNGObject());
  measure_result_ = measure_result;
  if (layout_result)
    SetLayoutResult(layout_result, 0);
  else
    DCHECK(layout_results_.IsEmpty());
}

void LayoutBox::FinalizeLayoutResults() {
  DCHECK(!layout_results_.IsEmpty());
  DCHECK(!layout_results_.back()->PhysicalFragment().BreakToken());
  // If we've added all the results we were going to, and the node establishes
  // an inline formatting context, we have some finalization to do.
  if (HasFragmentItems())
    NGFragmentItems::FinalizeAfterLayout(layout_results_);
}

void LayoutBox::ClearLayoutResults() {
  NOT_DESTROYED();
  if (measure_result_)
    InvalidateItems(*measure_result_);
  measure_result_ = nullptr;

  if (HasFragmentItems())
    NGFragmentItems::ClearAssociatedFragments(this);

  ShrinkLayoutResults(0);
}

void LayoutBox::ShrinkLayoutResults(wtf_size_t results_to_keep) {
  NOT_DESTROYED();
  DCHECK_GE(layout_results_.size(), results_to_keep);
  // Invalidate if inline |DisplayItemClient|s will be destroyed.
  for (wtf_size_t i = results_to_keep; i < layout_results_.size(); i++)
    InvalidateItems(*layout_results_[i]);
  // |layout_results_| is particularly critical when side effects are disabled.
  DCHECK(!NGDisableSideEffectsScope::IsDisabled());
  if (layout_results_.size() > 1)
    FragmentCountOrSizeDidChange();
  layout_results_.Shrink(results_to_keep);
}

void LayoutBox::InvalidateItems(const NGLayoutResult& result) {
  NOT_DESTROYED();
  // Invalidate if inline |DisplayItemClient|s will be destroyed.
  const auto& box_fragment =
      To<NGPhysicalBoxFragment>(result.PhysicalFragment());
  if (!box_fragment.HasItems())
    return;
#if DCHECK_IS_ON()
  // Column fragments are not really associated with a layout object.
  if (IsLayoutFlowThread())
    DCHECK(box_fragment.IsColumnBox());
  else if (!IsShapingDeferred())
    DCHECK_EQ(this, box_fragment.GetLayoutObject());
#endif
  ObjectPaintInvalidator(*this).SlowSetPaintingLayerNeedsRepaint();
}

const NGLayoutResult* LayoutBox::GetCachedLayoutResult() const {
  NOT_DESTROYED();
  if (layout_results_.IsEmpty())
    return nullptr;
  // Only return re-usable results.
  const NGLayoutResult* result = layout_results_[0];
  if (!To<NGPhysicalBoxFragment>(result->PhysicalFragment()).IsOnlyForNode())
    return nullptr;
  DCHECK(!result->PhysicalFragment().IsLayoutObjectDestroyedOrMoved() ||
         BeingDestroyed());
  DCHECK_EQ(layout_results_.size(), 1u);
  return result;
}

const NGLayoutResult* LayoutBox::GetCachedMeasureResult() const {
  NOT_DESTROYED();
  if (!measure_result_)
    return nullptr;

  if (!To<NGPhysicalBoxFragment>(measure_result_->PhysicalFragment())
           .IsOnlyForNode())
    return nullptr;

  return measure_result_;
}

const NGLayoutResult* LayoutBox::GetLayoutResult(wtf_size_t i) const {
  NOT_DESTROYED();
  return layout_results_[i];
}

const NGPhysicalBoxFragment&
LayoutBox::NGPhysicalFragmentList::Iterator::operator*() const {
  return To<NGPhysicalBoxFragment>((*iterator_)->PhysicalFragment());
}

const NGPhysicalBoxFragment& LayoutBox::NGPhysicalFragmentList::front() const {
  return To<NGPhysicalBoxFragment>(layout_results_.front()->PhysicalFragment());
}

const NGPhysicalBoxFragment& LayoutBox::NGPhysicalFragmentList::back() const {
  return To<NGPhysicalBoxFragment>(layout_results_.back()->PhysicalFragment());
}

const FragmentData* LayoutBox::FragmentDataFromPhysicalFragment(
    const NGPhysicalBoxFragment& physical_fragment) const {
  NOT_DESTROYED();
  const FragmentData* fragment_data = &FirstFragment();
  for (const auto& result : layout_results_) {
    if (&result->PhysicalFragment() == &physical_fragment)
      return fragment_data;
    DCHECK(fragment_data->NextFragment());
    fragment_data = fragment_data->NextFragment();
  }
  NOTREACHED();
  return fragment_data;
}

void LayoutBox::PositionLineBox(InlineBox* box) {
  NOT_DESTROYED();
  if (IsOutOfFlowPositioned()) {
    // Cache the x position only if we were an INLINE type originally.
    bool originally_inline = StyleRef().IsOriginalDisplayInlineType();
    if (originally_inline) {
      // The value is cached in the xPos of the box.  We only need this value if
      // our object was inline originally, since otherwise it would have ended
      // up underneath the inlines.
      RootInlineBox& root = box->Root();
      root.Block().SetStaticInlinePositionForChild(LineLayoutBox(this),
                                                   box->LogicalLeft());
    } else {
      // Our object was a block originally, so we make our normal flow position
      // be just below the line box (as though all the inlines that came before
      // us got wrapped in an anonymous block, which is what would have happened
      // had we been in flow). This value was cached in the y() of the box.
      Layer()->SetStaticBlockPosition(box->LogicalTop());
    }

    if (Container()->IsLayoutInline())
      MoveWithEdgeOfInlineContainerIfNecessary(box->IsHorizontal());

    // Nuke the box.
    box->Remove(kDontMarkLineBoxes);
    box->Destroy();
  } else if (IsAtomicInlineLevel()) {
    SetLocationAndUpdateOverflowControlsIfNeeded(box->Location());
    SetInlineBoxWrapper(box);
  }
}

void LayoutBox::MoveWithEdgeOfInlineContainerIfNecessary(bool is_horizontal) {
  NOT_DESTROYED();
  DCHECK(IsOutOfFlowPositioned());
  DCHECK(Container()->IsLayoutInline());
  DCHECK(Container()->CanContainOutOfFlowPositionedElement(
      StyleRef().GetPosition()));
  // If this object is inside a relative positioned inline and its inline
  // position is an explicit offset from the edge of its container then it will
  // need to move if its inline container has changed width. We do not track if
  // the width has changed but if we are here then we are laying out lines
  // inside it, so it probably has - mark our object for layout so that it can
  // move to the new offset created by the new width.
  if (!NormalChildNeedsLayout() &&
      !StyleRef().HasStaticInlinePosition(is_horizontal))
    SetChildNeedsLayout(kMarkOnlyThis);
}

void LayoutBox::DeleteLineBoxWrapper() {
  NOT_DESTROYED();
  if (!IsInLayoutNGInlineFormattingContext() && inline_box_wrapper_) {
    if (!DocumentBeingDestroyed())
      inline_box_wrapper_->Remove();
    inline_box_wrapper_->Destroy();
    inline_box_wrapper_ = nullptr;
  }
}

void LayoutBox::SetSpannerPlaceholder(
    LayoutMultiColumnSpannerPlaceholder& placeholder) {
  NOT_DESTROYED();
  // Not expected to change directly from one spanner to another.
  CHECK(!rare_data_ || !rare_data_->spanner_placeholder_);
  EnsureRareData().spanner_placeholder_ = &placeholder;
}

void LayoutBox::ClearSpannerPlaceholder() {
  NOT_DESTROYED();
  if (!rare_data_)
    return;
  rare_data_->spanner_placeholder_ = nullptr;
}

void LayoutBox::SetPaginationStrut(LayoutUnit strut) {
  NOT_DESTROYED();
  if (!strut && !rare_data_)
    return;
  EnsureRareData().pagination_strut_ = strut;
}

bool LayoutBox::IsBreakBetweenControllable(EBreakBetween break_value) const {
  NOT_DESTROYED();
  if (break_value == EBreakBetween::kAuto)
    return true;
  // We currently only support non-auto break-before and break-after values on
  // in-flow block level elements, which is the minimum requirement according to
  // the spec.
  if (IsInline() || IsFloatingOrOutOfFlowPositioned())
    return false;
  const LayoutBlock* curr = ContainingBlock();
  if (!curr || !curr->IsLayoutBlockFlow())
    return false;
  const LayoutView* layout_view = View();
  bool view_is_paginated = layout_view->FragmentationContext();
  if (!view_is_paginated && !FlowThreadContainingBlock())
    return false;
  while (curr) {
    if (curr == layout_view) {
      return view_is_paginated && break_value != EBreakBetween::kColumn &&
             break_value != EBreakBetween::kAvoidColumn;
    }
    if (curr->IsLayoutFlowThread()) {
      if (break_value ==
          EBreakBetween::kAvoid)  // Valid in any kind of fragmentation context.
        return true;
      bool is_multicol_value = break_value == EBreakBetween::kColumn ||
                               break_value == EBreakBetween::kAvoidColumn;
      if (is_multicol_value)
        return true;
      // If this is a flow thread for a multicol container, and we have a break
      // value for paged, we need to keep looking.
    }
    if (curr->IsOutOfFlowPositioned())
      return false;
    curr = curr->ContainingBlock();
  }
  NOTREACHED();
  return false;
}

bool LayoutBox::IsBreakInsideControllable(EBreakInside break_value) const {
  NOT_DESTROYED();
  if (break_value == EBreakInside::kAuto)
    return true;
  // First check multicol.
  const LayoutFlowThread* flow_thread = FlowThreadContainingBlock();
  // 'avoid-column' is only valid in a multicol context.
  if (break_value == EBreakInside::kAvoidColumn)
    return flow_thread;
  // 'avoid' is valid in any kind of fragmentation context.
  if (break_value == EBreakInside::kAvoid && flow_thread)
    return true;
  DCHECK(break_value == EBreakInside::kAvoidPage ||
         break_value == EBreakInside::kAvoid);
  if (View()->FragmentationContext())
    return true;  // The view is paginated, probably because we're printing.
  if (!flow_thread)
    return false;  // We're not inside any pagination context
  return false;
}

EBreakBetween LayoutBox::BreakAfter() const {
  NOT_DESTROYED();
  EBreakBetween break_value = StyleRef().BreakAfter();
  if (break_value == EBreakBetween::kAuto ||
      IsBreakBetweenControllable(break_value))
    return break_value;
  return EBreakBetween::kAuto;
}

EBreakBetween LayoutBox::BreakBefore() const {
  NOT_DESTROYED();
  EBreakBetween break_value = StyleRef().BreakBefore();
  if (break_value == EBreakBetween::kAuto ||
      IsBreakBetweenControllable(break_value))
    return break_value;
  return EBreakBetween::kAuto;
}

EBreakInside LayoutBox::BreakInside() const {
  NOT_DESTROYED();
  EBreakInside break_value = StyleRef().BreakInside();
  if (break_value == EBreakInside::kAuto ||
      IsBreakInsideControllable(break_value))
    return break_value;
  return EBreakInside::kAuto;
}

EBreakBetween LayoutBox::ClassABreakPointValue(
    EBreakBetween previous_break_after_value) const {
  NOT_DESTROYED();
  // First assert that we're at a class A break point.
  DCHECK(IsBreakBetweenControllable(previous_break_after_value));

  return JoinFragmentainerBreakValues(previous_break_after_value,
                                      BreakBefore());
}

bool LayoutBox::NeedsForcedBreakBefore(
    EBreakBetween previous_break_after_value) const {
  NOT_DESTROYED();
  // Forced break values are only honored when specified on in-flow objects, but
  // floats and out-of-flow positioned objects may be affected by a break-after
  // value of the previous in-flow object, even though we're not at a class A
  // break point.
  EBreakBetween break_value =
      IsFloatingOrOutOfFlowPositioned()
          ? previous_break_after_value
          : ClassABreakPointValue(previous_break_after_value);
  return IsForcedFragmentainerBreakValue(break_value);
}

const AtomicString LayoutBox::StartPageName() const {
  NOT_DESTROYED();
  return StyleRef().Page();
}

const AtomicString LayoutBox::EndPageName() const {
  NOT_DESTROYED();
  return StyleRef().Page();
}

PhysicalRect LayoutBox::LocalVisualRectIgnoringVisibility() const {
  NOT_DESTROYED();
  return PhysicalSelfVisualOverflowRect();
}

void LayoutBox::InflateVisualRectForFilterUnderContainer(
    TransformState& transform_state,
    const LayoutObject& container,
    const LayoutBoxModelObject* ancestor_to_stop_at) const {
  NOT_DESTROYED();
  transform_state.Flatten();
  // Apply visual overflow caused by reflections and filters defined on objects
  // between this object and container (not included) or ancestorToStopAt
  // (included).
  PhysicalOffset offset_from_container = OffsetFromContainer(&container);
  transform_state.Move(offset_from_container);
  for (LayoutObject* parent = Parent(); parent && parent != container;
       parent = parent->Parent()) {
    if (parent->IsBox()) {
      // Convert rect into coordinate space of parent to apply parent's
      // reflection and filter.
      PhysicalOffset parent_offset = parent->OffsetFromAncestor(&container);
      transform_state.Move(-parent_offset);
      To<LayoutBox>(parent)->InflateVisualRectForFilter(transform_state);
      transform_state.Move(parent_offset);
    }
    if (parent == ancestor_to_stop_at)
      break;
  }
  transform_state.Move(-offset_from_container);
}

bool LayoutBox::MapToVisualRectInAncestorSpaceInternal(
    const LayoutBoxModelObject* ancestor,
    TransformState& transform_state,
    VisualRectFlags visual_rect_flags) const {
  NOT_DESTROYED();
  InflateVisualRectForFilter(transform_state);

  if (ancestor == this)
    return true;

  AncestorSkipInfo skip_info(ancestor, true);
  LayoutObject* container = Container(&skip_info);
  LayoutBox* table_row_container = nullptr;
  // Skip table row because cells and rows are in the same coordinate space (see
  // below, however for more comments about when |ancestor| is the table row).
  if ((IsTableCell() && !IsLayoutNGObject()) || IsTableCellLegacy()) {
    DCHECK(container->IsTableRow());
    DCHECK_EQ(ParentBox(), container);
    if (container != ancestor)
      container = container->Parent();
    else
      table_row_container = To<LayoutBox>(container);
  }
  if (!container)
    return true;

  PhysicalOffset container_offset;
  if (auto* box = DynamicTo<LayoutBox>(container)) {
    container_offset += PhysicalLocation(box);

    // If the row is the ancestor, however, add its offset back in. In effect,
    // this passes from the joint <td> / <tr> coordinate space to the parent
    // space, then back to <tr> / <td>.
    if (table_row_container)
      container_offset -= table_row_container->PhysicalLocation(box);
  } else {
    container_offset += PhysicalLocation();
  }

  const ComputedStyle& style_to_use = StyleRef();
  EPosition position = style_to_use.GetPosition();
  if (IsOutOfFlowPositioned() && container->IsLayoutInline() &&
      container->CanContainOutOfFlowPositionedElement(position)) {
    container_offset +=
        To<LayoutInline>(container)->OffsetForInFlowPositionedInline(*this);
  } else if (style_to_use.HasInFlowPosition() && Layer()) {
    // Apply the relative position offset when invalidating a rectangle. The
    // layer is translated, but the layout box isn't, so we need to do this to
    // get the right dirty rect.  Since this is called from
    // LayoutObject::setStyle, the relative position flag on the LayoutObject
    // has been cleared, so use the one on the style().
    container_offset += OffsetForInFlowPosition();
  } else if (UNLIKELY(AnchorScrollObject())) {
    container_offset += ComputeAnchorScrollOffset();
  }

  if (skip_info.FilterSkipped()) {
    InflateVisualRectForFilterUnderContainer(transform_state, *container,
                                             ancestor);
  }

  if (!MapVisualRectToContainer(container, container_offset, ancestor,
                                visual_rect_flags, transform_state))
    return false;

  if (skip_info.AncestorSkipped()) {
    bool preserve3D = container->StyleRef().Preserves3D();
    TransformState::TransformAccumulation accumulation =
        preserve3D ? TransformState::kAccumulateTransform
                   : TransformState::kFlattenTransform;

    // If the ancestor is below the container, then we need to map the rect into
    // ancestor's coordinates.
    PhysicalOffset ancestor_container_offset =
        ancestor->OffsetFromAncestor(container);
    transform_state.Move(-ancestor_container_offset, accumulation);
    return true;
  }

  if (IsFixedPositioned() && container == ancestor && container->IsLayoutView())
    transform_state.Move(To<LayoutView>(container)->OffsetForFixedPosition());

  return container->MapToVisualRectInAncestorSpaceInternal(
      ancestor, transform_state, visual_rect_flags);
}

void LayoutBox::InflateVisualRectForFilter(
    TransformState& transform_state) const {
  NOT_DESTROYED();
  if (!Layer() || !Layer()->PaintsWithFilters())
    return;

  transform_state.Flatten();
  PhysicalRect rect = PhysicalRect::EnclosingRect(
      transform_state.LastPlanarQuad().BoundingBox());
  transform_state.SetQuad(
      gfx::QuadF(gfx::RectF(Layer()->MapRectForFilter(rect))));
}

static bool ShouldRecalculateMinMaxWidthsAffectedByAncestor(
    const LayoutBox* box) {
  if (box->IntrinsicLogicalWidthsDirty()) {
    // If the preferred widths are already dirty at this point (during layout),
    // it actually means that we never need to calculate them, since that should
    // have been carried out by an ancestor that's sized based on preferred
    // widths (a shrink-to-fit container, for instance). In such cases the
    // object will be left as dirty indefinitely, and it would just be a waste
    // of time to calculate the preferred withs when nobody needs them.
    return false;
  }
  if (const LayoutBox* containing_block = box->ContainingBlock()) {
    if (containing_block->NeedsPreferredWidthsRecalculation() &&
        !containing_block->IntrinsicLogicalWidthsDirty()) {
      // If our containing block also has min/max widths that are affected by
      // the ancestry, we have already dealt with this object as well. Avoid
      // unnecessary work and O(n^2) time complexity.
      return false;
    }
  }
  return true;
}

void LayoutBox::UpdateLogicalWidth() {
  NOT_DESTROYED();
  if (NeedsPreferredWidthsRecalculation()) {
    if (ShouldRecalculateMinMaxWidthsAffectedByAncestor(this)) {
      // Laying out this object means that its containing block is also being
      // laid out. This object is special, in that its min/max widths depend on
      // the ancestry (min/max width calculation should ideally be strictly
      // bottom-up, but that's not always the case), so since the containing
      // block size may have changed, we need to recalculate the min/max widths
      // of this object, and every child that has the same issue, recursively.
      SetIntrinsicLogicalWidthsDirty(kMarkOnlyThis);

      // Since all this takes place during actual layout, instead of being part
      // of min/max the width calculation machinery, we need to enter said
      // machinery here, to make sure that what was dirtied is actually
      // recalculated. Leaving things dirty would mean that any subsequent
      // dirtying of descendants would fail.
      UpdateCachedIntrinsicLogicalWidthsIfNeeded();
    }
  }

  LogicalExtentComputedValues computed_values;
  ComputeLogicalWidth(computed_values);

  SetLogicalWidth(computed_values.extent_);
  SetLogicalLeft(computed_values.position_);
  SetMarginStart(computed_values.margins_.start_);
  SetMarginEnd(computed_values.margins_.end_);
}

static float GetMaxWidthListMarker(const LayoutBox* layout_object) {
#if DCHECK_IS_ON()
  DCHECK(layout_object);
  Node* parent_node = layout_object->GeneratingNode();
  DCHECK(parent_node);
  DCHECK(IsA<HTMLOListElement>(parent_node) ||
         IsA<HTMLUListElement>(parent_node));
  DCHECK_NE(layout_object->StyleRef().TextAutosizingMultiplier(), 1);
#endif
  float max_width = 0;
  for (LayoutObject* child = layout_object->SlowFirstChild(); child;
       child = child->NextSibling()) {
    if (!child->IsListItem())
      continue;

    auto* list_item = To<LayoutBox>(child);
    for (LayoutObject* item_child = list_item->SlowFirstChild(); item_child;
         item_child = item_child->NextSibling()) {
      if (!item_child->IsListMarkerForNormalContent())
        continue;
      auto* item_marker = To<LayoutBox>(item_child);
      // Make sure to compute the autosized width.
      if (item_marker->NeedsLayout())
        item_marker->UpdateLayout();
      max_width = std::max<float>(
          max_width,
          To<LayoutListMarker>(item_marker)->LogicalWidth().ToFloat());
      break;
    }
  }
  return max_width;
}

LayoutUnit LayoutBox::ContainerWidthInInlineDirection() const {
  NOT_DESTROYED();
  LayoutBlock* cb = ContainingBlock();

  if (IsParallelWritingMode(cb->StyleRef().GetWritingMode(),
                            StyleRef().GetWritingMode())) {
    return std::max(LayoutUnit(), ContainingBlockLogicalWidthForContent());
  }

  // PerpendicularContainingBlockLogicalHeight() can return -1 in some
  // situations but we cannot have a negative width, that's why we clamp it to
  // zero.
  return PerpendicularContainingBlockLogicalHeight().ClampNegativeToZero();
}

bool LayoutBox::ShouldComputeLogicalWidthFromAspectRatio(
    LayoutUnit* out_logical_height) const {
  NOT_DESTROYED();
  if (StyleRef().AspectRatio().IsAuto())
    return false;

  if (IsGridItem() && HasStretchedLogicalWidth(StretchingMode::kExplicit))
    return false;

  if (!HasOverrideLogicalHeight() &&
      !ShouldComputeLogicalWidthFromAspectRatioAndInsets() &&
      !StyleRef().LogicalHeight().IsFixed() &&
      !StyleRef().LogicalHeight().IsPercentOrCalc()) {
    return false;
  }

  LogicalExtentComputedValues values;
  values.extent_ = kIndefiniteSize;
  ComputeLogicalHeight(values);
  if (values.extent_ == kIndefiniteSize)
    return false;

  if (out_logical_height)
    *out_logical_height = values.extent_;
  return true;
}

bool LayoutBox::ComputeLogicalWidthFromAspectRatio(
    LayoutUnit* out_logical_width) const {
  NOT_DESTROYED();
  LayoutUnit logical_height_for_ar;
  if (!ShouldComputeLogicalWidthFromAspectRatio(&logical_height_for_ar))
    return false;

  LayoutUnit container_width_in_inline_direction =
      ContainerWidthInInlineDirection();

  NGBoxStrut border_padding(BorderStart() + ComputedCSSPaddingStart(),
                            BorderEnd() + ComputedCSSPaddingEnd(),
                            BorderBefore() + ComputedCSSPaddingBefore(),
                            BorderAfter() + ComputedCSSPaddingAfter());
  LayoutUnit logical_width = InlineSizeFromAspectRatio(
      border_padding, StyleRef().LogicalAspectRatio(),
      StyleRef().BoxSizingForAspectRatio(), logical_height_for_ar);
  *out_logical_width = ConstrainLogicalWidthByMinMax(
      logical_width, container_width_in_inline_direction, ContainingBlock(),
      /* allow_intrinsic */ false);
  return true;
}

DISABLE_CFI_PERF
void LayoutBox::ComputeLogicalWidth(
    LogicalExtentComputedValues& computed_values) const {
  NOT_DESTROYED();
  computed_values.position_ = LogicalLeft();
  computed_values.margins_.start_ = MarginStart();
  computed_values.margins_.end_ = MarginEnd();

  // The parent box is flexing us, so it has increased or decreased our
  // width.  Use the width from the style context.
  if (HasOverrideLogicalWidth()) {
    computed_values.extent_ = OverrideLogicalWidth();
    return;
  }

  if (IsOutOfFlowPositioned()) {
    ComputePositionedLogicalWidth(computed_values);
    return;
  }

  // FIXME: Account for writing-mode in flexible boxes.
  // https://bugs.webkit.org/show_bug.cgi?id=46418
  bool in_vertical_box =
      Parent()->IsDeprecatedFlexibleBox() &&
      (Parent()->StyleRef().BoxOrient() == EBoxOrient::kVertical);
  bool stretching =
      (Parent()->StyleRef().BoxAlign() == EBoxAlignment::kStretch);
  // TODO (lajava): Stretching is the only reason why we don't want the box to
  // be treated as a replaced element, so we could perhaps refactor all this
  // logic, not only for flex and grid since alignment is intended to be applied
  // to any block.
  bool treat_as_replaced = ShouldComputeSizeAsReplaced() &&
                           (!in_vertical_box || !stretching) &&
                           (!IsGridItem() || !HasStretchedLogicalWidth());
  const ComputedStyle& style_to_use = StyleRef();
  LayoutUnit container_logical_width =
      std::max(LayoutUnit(), ContainingBlockLogicalWidthForContent());

  if (IsInline() && !IsInlineBlockOrInlineTable()) {
    // just calculate margins
    computed_values.margins_.start_ = MinimumValueForLength(
        style_to_use.MarginStart(), container_logical_width);
    computed_values.margins_.end_ = MinimumValueForLength(
        style_to_use.MarginEnd(), container_logical_width);
    if (treat_as_replaced) {
      computed_values.extent_ = std::max(
          ComputeReplacedLogicalWidth() + BorderAndPaddingLogicalWidth(),
          PreferredLogicalWidths().min_size);
    }
    return;
  }

  LayoutUnit container_width_in_inline_direction =
      ContainerWidthInInlineDirection();
  LayoutBlock* cb = ContainingBlock();

  if (treat_as_replaced) {
    computed_values.extent_ =
        ComputeReplacedLogicalWidth() + BorderAndPaddingLogicalWidth();
  } else if (StyleRef().LogicalWidth().IsAuto() &&
             (!IsGridItem() || !ShouldComputeSizeAsReplaced() ||
              !HasStretchedLogicalWidth() || !HasStretchedLogicalHeight()) &&
             ComputeLogicalWidthFromAspectRatio(&computed_values.extent_)) {
    /* we're good */
  } else {
    LayoutUnit preferred_width = ComputeLogicalWidthUsing(
        kMainOrPreferredSize, style_to_use.LogicalWidth(),
        container_width_in_inline_direction, cb);
    computed_values.extent_ = ConstrainLogicalWidthByMinMax(
        preferred_width, container_width_in_inline_direction, cb);
  }

  // Margin calculations.
  ComputeMarginsForDirection(
      kInlineDirection, cb, container_logical_width, computed_values.extent_,
      computed_values.margins_.start_, computed_values.margins_.end_,
      StyleRef().MarginStart(), StyleRef().MarginEnd());

  bool has_perpendicular_containing_block =
      cb->IsHorizontalWritingMode() != IsHorizontalWritingMode();
  if (!has_perpendicular_containing_block && container_logical_width &&
      container_logical_width !=
          (computed_values.extent_ + computed_values.margins_.start_ +
           computed_values.margins_.end_) &&
      !IsFloating() && !IsInline() &&
      !cb->IsFlexibleBoxIncludingDeprecatedAndNG() &&
      !cb->IsLayoutGridIncludingNG()) {
    LayoutUnit new_margin_total =
        container_logical_width - computed_values.extent_;
    bool has_inverted_direction = cb->StyleRef().IsLeftToRightDirection() !=
                                  StyleRef().IsLeftToRightDirection();
    if (has_inverted_direction) {
      computed_values.margins_.start_ =
          new_margin_total - computed_values.margins_.end_;
    } else {
      computed_values.margins_.end_ =
          new_margin_total - computed_values.margins_.start_;
    }
  }

  if (style_to_use.TextAutosizingMultiplier() != 1 &&
      style_to_use.MarginStart().IsFixed()) {
    Node* parent_node = GeneratingNode();
    if (parent_node && (IsA<HTMLOListElement>(*parent_node) ||
                        IsA<HTMLUListElement>(*parent_node))) {
      // Make sure the markers in a list are properly positioned (i.e. not
      // chopped off) when autosized.
      const float adjusted_margin =
          (1 - 1.0 / style_to_use.TextAutosizingMultiplier()) *
          GetMaxWidthListMarker(this);
      bool has_inverted_direction = cb->StyleRef().IsLeftToRightDirection() !=
                                    StyleRef().IsLeftToRightDirection();
      if (has_inverted_direction)
        computed_values.margins_.end_ += adjusted_margin;
      else
        computed_values.margins_.start_ += adjusted_margin;
    }
  }
}

LayoutUnit LayoutBox::FillAvailableMeasure(
    LayoutUnit available_logical_width) const {
  NOT_DESTROYED();
  LayoutUnit margin_start;
  LayoutUnit margin_end;
  return FillAvailableMeasure(available_logical_width, margin_start,
                              margin_end);
}

LayoutUnit LayoutBox::FillAvailableMeasure(LayoutUnit available_logical_width,
                                           LayoutUnit& margin_start,
                                           LayoutUnit& margin_end) const {
  NOT_DESTROYED();
  DCHECK_GE(available_logical_width, 0);

  bool isOrthogonalElement =
      IsHorizontalWritingMode() != ContainingBlock()->IsHorizontalWritingMode();
  LayoutUnit available_size_for_resolving_margin =
      isOrthogonalElement ? ContainingBlockLogicalWidthForContent()
                          : available_logical_width;
  margin_start = MinimumValueForLength(StyleRef().MarginStart(),
                                       available_size_for_resolving_margin);
  margin_end = MinimumValueForLength(StyleRef().MarginEnd(),
                                     available_size_for_resolving_margin);

  if (HasOverrideAvailableInlineSize())
    available_logical_width = OverrideAvailableInlineSize();

  LayoutUnit available = available_logical_width - margin_start - margin_end;
  available = std::max(available, LayoutUnit());
  return available;
}

DISABLE_CFI_PERF
LayoutUnit LayoutBox::ComputeIntrinsicLogicalWidthUsing(
    const Length& logical_width_length,
    LayoutUnit available_logical_width) const {
  NOT_DESTROYED();
  if (logical_width_length.IsFillAvailable()) {
    if (!IsA<HTMLMarqueeElement>(GetNode())) {
      UseCounter::Count(GetDocument(),
                        WebFeature::kCSSFillAvailableLogicalWidth);
    }
    return std::max(BorderAndPaddingLogicalWidth(),
                    FillAvailableMeasure(available_logical_width));
  }

  MinMaxSizesType type = MinMaxSizesType::kContent;
  if (logical_width_length.IsMinIntrinsic())
    type = MinMaxSizesType::kIntrinsic;
  MinMaxSizes sizes = IntrinsicLogicalWidths(type);

  if (logical_width_length.IsMinContent() ||
      logical_width_length.IsMinIntrinsic())
    return sizes.min_size;

  if (logical_width_length.IsMaxContent())
    return sizes.max_size;

  if (logical_width_length.IsFitContent()) {
    return sizes.ClampSizeToMinAndMax(
        FillAvailableMeasure(available_logical_width));
  }

  NOTREACHED();
  return LayoutUnit();
}

DISABLE_CFI_PERF
LayoutUnit LayoutBox::ComputeLogicalWidthUsing(
    SizeType width_type,
    const Length& logical_width,
    LayoutUnit available_logical_width,
    const LayoutBlock* cb) const {
  NOT_DESTROYED();
  DCHECK(width_type == kMinSize || width_type == kMainOrPreferredSize ||
         !logical_width.IsAuto());
  if (width_type == kMinSize && logical_width.IsAuto())
    return AdjustBorderBoxLogicalWidthForBoxSizing(0);

  if (logical_width.IsSpecified()) {
    // FIXME: If the containing block flow is perpendicular to our direction we
    // need to use the available logical height instead.
    return AdjustBorderBoxLogicalWidthForBoxSizing(
        ValueForLength(logical_width, available_logical_width));
  }

  if (logical_width.IsContentOrIntrinsicOrFillAvailable()) {
    return ComputeIntrinsicLogicalWidthUsing(logical_width,
                                             available_logical_width);
  }

  LayoutUnit margin_start;
  LayoutUnit margin_end;
  LayoutUnit logical_width_result =
      FillAvailableMeasure(available_logical_width, margin_start, margin_end);

  auto* child_block_flow = DynamicTo<LayoutBlockFlow>(cb);
  if (ShrinkToAvoidFloats() && child_block_flow &&
      child_block_flow->ContainsFloats()) {
    logical_width_result = std::min(
        logical_width_result, ShrinkLogicalWidthToAvoidFloats(
                                  margin_start, margin_end, child_block_flow));
  }

  if (width_type == kMainOrPreferredSize &&
      SizesLogicalWidthToFitContent(logical_width)) {
    // Reset width so that any percent margins on inline children do not
    // use it when calculating min/max preferred width.
    // TODO(crbug.com/710026): Remove const_cast
    LayoutUnit w = LogicalWidth();
    const_cast<LayoutBox*>(this)->SetLogicalWidth(LayoutUnit());
    MinMaxSizes preferred_logical_widths = PreferredLogicalWidths();
    LayoutUnit result =
        preferred_logical_widths.ClampSizeToMinAndMax(logical_width_result);
    const_cast<LayoutBox*>(this)->SetLogicalWidth(w);
    return result;
  }
  return logical_width_result;
}

bool LayoutBox::ColumnFlexItemHasStretchAlignment() const {
  NOT_DESTROYED();
  // auto margins mean we don't stretch. Note that this function will only be
  // used for widths, so we don't have to check marginBefore/marginAfter.
  const auto& parent_style = Parent()->StyleRef();
  DCHECK(parent_style.ResolvedIsColumnFlexDirection());
  if (StyleRef().MarginStart().IsAuto() || StyleRef().MarginEnd().IsAuto())
    return false;
  return StyleRef()
             .ResolvedAlignSelf(
                 ContainingBlock()->SelfAlignmentNormalBehavior(),
                 &parent_style)
             .GetPosition() == ItemPosition::kStretch;
}

bool LayoutBox::IsStretchingColumnFlexItem() const {
  NOT_DESTROYED();
  LayoutObject* parent = Parent();
  if (parent->StyleRef().IsDeprecatedWebkitBox() &&
      parent->StyleRef().BoxOrient() == EBoxOrient::kVertical &&
      parent->StyleRef().BoxAlign() == EBoxAlignment::kStretch)
    return true;

  // We don't stretch multiline flexboxes because they need to apply line
  // spacing (align-content) first.
  if (parent->IsFlexibleBoxIncludingNG() &&
      parent->StyleRef().FlexWrap() == EFlexWrap::kNowrap &&
      parent->StyleRef().ResolvedIsColumnFlexDirection() &&
      ColumnFlexItemHasStretchAlignment())
    return true;
  return false;
}

// TODO (lajava) Can/Should we move this inside specific layout classes (flex.
// grid)? Can we refactor columnFlexItemHasStretchAlignment logic?
bool LayoutBox::HasStretchedLogicalWidth(StretchingMode stretchingMode) const {
  NOT_DESTROYED();
  const ComputedStyle& style = StyleRef();
  if (!style.LogicalWidth().IsAuto() || style.MarginStart().IsAuto() ||
      style.MarginEnd().IsAuto())
    return false;
  LayoutBlock* cb = ContainingBlock();
  if (!cb) {
    // We are evaluating align-self/justify-self, which default to 'normal' for
    // the root element. The 'normal' value behaves like 'start' except for
    // Flexbox Items, which obviously should have a container.
    return false;
  }
  auto defaultItemPosition = stretchingMode == StretchingMode::kAny
                                 ? cb->SelfAlignmentNormalBehavior(this)
                                 : ItemPosition::kNormal;
  if (cb->IsHorizontalWritingMode() != IsHorizontalWritingMode()) {
    return style.ResolvedAlignSelf(defaultItemPosition, cb->Style())
               .GetPosition() == ItemPosition::kStretch;
  }
  return style.ResolvedJustifySelf(defaultItemPosition, cb->Style())
             .GetPosition() == ItemPosition::kStretch;
}

// TODO (lajava) Can/Should we move this inside specific layout classes (flex.
// grid)? Can we refactor columnFlexItemHasStretchAlignment logic?
bool LayoutBox::HasStretchedLogicalHeight() const {
  NOT_DESTROYED();
  const ComputedStyle& style = StyleRef();
  if (!style.LogicalHeight().IsAuto() || style.MarginBefore().IsAuto() ||
      style.MarginAfter().IsAuto())
    return false;
  LayoutBlock* cb = ContainingBlock();
  if (!cb) {
    // We are evaluating align-self/justify-self, which default to 'normal' for
    // the root element. The 'normal' value behaves like 'start' except for
    // Flexbox Items, which obviously should have a container.
    return false;
  }
  if (cb->IsHorizontalWritingMode() != IsHorizontalWritingMode()) {
    return style
               .ResolvedJustifySelf(cb->SelfAlignmentNormalBehavior(this),
                                    cb->Style())
               .GetPosition() == ItemPosition::kStretch;
  }
  return style
             .ResolvedAlignSelf(cb->SelfAlignmentNormalBehavior(this),
                                cb->Style())
             .GetPosition() == ItemPosition::kStretch;
}

bool LayoutBox::SizesLogicalWidthToFitContent(
    const Length& logical_width) const {
  NOT_DESTROYED();
  if (IsFloating() || IsInlineBlockOrInlineTable() ||
      StyleRef().HasOutOfFlowPosition())
    return true;

  if (IsGridItem())
    return !HasStretchedLogicalWidth();

  // Flexible box items should shrink wrap, so we lay them out at their
  // intrinsic widths. In the case of columns that have a stretch alignment, we
  // go ahead and layout at the stretched size to avoid an extra layout when
  // applying alignment.
  if (Parent()->IsFlexibleBoxIncludingNG()) {
    // For multiline columns, we need to apply align-content first, so we can't
    // stretch now.
    if (!Parent()->StyleRef().ResolvedIsColumnFlexDirection() ||
        Parent()->StyleRef().FlexWrap() != EFlexWrap::kNowrap)
      return true;
    if (!ColumnFlexItemHasStretchAlignment())
      return true;
  }

  // Flexible horizontal boxes lay out children at their intrinsic widths. Also
  // vertical boxes that don't stretch their kids lay out their children at
  // their intrinsic widths.
  // FIXME: Think about writing-mode here.
  // https://bugs.webkit.org/show_bug.cgi?id=46473
  if ((Parent()->IsDeprecatedFlexibleBox() ||
       (Parent()->StyleRef().IsDeprecatedWebkitBox() &&
        Parent()->IsFlexibleBox())) &&
      (Parent()->StyleRef().BoxOrient() == EBoxOrient::kHorizontal ||
       Parent()->StyleRef().BoxAlign() != EBoxAlignment::kStretch))
    return true;

  // Button, input, select, textarea, and legend treat width value of 'auto' as
  // 'intrinsic' unless it's in a stretching column flexbox.
  // FIXME: Think about writing-mode here.
  // https://bugs.webkit.org/show_bug.cgi?id=46473
  if (logical_width.IsAuto() && !IsStretchingColumnFlexItem() &&
      AutoWidthShouldFitContent())
    return true;

  if (IsHorizontalWritingMode() != ContainingBlock()->IsHorizontalWritingMode())
    return true;

  if (IsCustomItem())
    return IsCustomItemShrinkToFit();

  return false;
}

bool LayoutBox::AutoWidthShouldFitContent() const {
  NOT_DESTROYED();
  return GetNode() &&
         (IsA<HTMLInputElement>(*GetNode()) ||
          IsA<HTMLSelectElement>(*GetNode()) ||
          IsA<HTMLButtonElement>(*GetNode()) ||
          IsA<HTMLTextAreaElement>(*GetNode()) || IsRenderedLegend());
}

void LayoutBox::ComputeMarginsForDirection(MarginDirection flow_direction,
                                           const LayoutBlock* containing_block,
                                           LayoutUnit container_width,
                                           LayoutUnit child_width,
                                           LayoutUnit& margin_start,
                                           LayoutUnit& margin_end,
                                           Length margin_start_length,
                                           Length margin_end_length) const {
  NOT_DESTROYED();
  // First assert that we're not calling this method on box types that don't
  // support margins.
  DCHECK(!IsTableCell());
  DCHECK(!IsTableRow());
  DCHECK(!IsTableSection());
  DCHECK(!IsLayoutTableCol());
  if (flow_direction == kBlockDirection || IsFloating() || IsInline()) {
    // Margins are calculated with respect to the logical width of
    // the containing block (8.3)
    // Inline blocks/tables and floats don't have their margins increased.
    margin_start = MinimumValueForLength(margin_start_length, container_width);
    margin_end = MinimumValueForLength(margin_end_length, container_width);
    return;
  }

  if (containing_block->IsFlexibleBoxIncludingNG()) {
    // We need to let flexbox handle the margin adjustment - otherwise, flexbox
    // will think we're wider than we actually are and calculate line sizes
    // wrong. See also https://drafts.csswg.org/css-flexbox/#auto-margins
    if (margin_start_length.IsAuto())
      margin_start_length = Length::Fixed(0);
    if (margin_end_length.IsAuto())
      margin_end_length = Length::Fixed(0);
  }

  LayoutUnit margin_start_width =
      MinimumValueForLength(margin_start_length, container_width);
  LayoutUnit margin_end_width =
      MinimumValueForLength(margin_end_length, container_width);

  LayoutUnit available_width = container_width;
  auto* containing_block_flow = DynamicTo<LayoutBlockFlow>(containing_block);
  if (CreatesNewFormattingContext() && containing_block_flow &&
      containing_block_flow->ContainsFloats()) {
    available_width = ContainingBlockAvailableLineWidth();
    if (ShrinkToAvoidFloats() && available_width < container_width) {
      margin_start = std::max(LayoutUnit(), margin_start_width);
      margin_end = std::max(LayoutUnit(), margin_end_width);
    }
  }

  // CSS 2.1 (10.3.3): "If 'width' is not 'auto' and 'border-left-width' +
  // 'padding-left' + 'width' + 'padding-right' + 'border-right-width' (plus any
  // of 'margin-left' or 'margin-right' that are not 'auto') is larger than the
  // width of the containing block, then any 'auto' values for 'margin-left' or
  // 'margin-right' are, for the following rules, treated as zero.
  LayoutUnit margin_box_width =
      child_width + (!StyleRef().Width().IsAuto()
                         ? margin_start_width + margin_end_width
                         : LayoutUnit());

  if (margin_box_width < available_width) {
    // CSS 2.1: "If both 'margin-left' and 'margin-right' are 'auto', their used
    // values are equal. This horizontally centers the element with respect to
    // the edges of the containing block."
    const ComputedStyle& containing_block_style = containing_block->StyleRef();
    if ((margin_start_length.IsAuto() && margin_end_length.IsAuto()) ||
        (!margin_start_length.IsAuto() && !margin_end_length.IsAuto() &&
         containing_block_style.GetTextAlign() == ETextAlign::kWebkitCenter)) {
      // Other browsers center the margin box for align=center elements so we
      // match them here.
      LayoutUnit centered_margin_box_start =
          std::max(LayoutUnit(), (available_width - child_width -
                                  margin_start_width - margin_end_width) /
                                     2);
      margin_start = centered_margin_box_start + margin_start_width;
      margin_end =
          available_width - child_width - margin_start + margin_end_width;
      return;
    }

    // Adjust margins for the align attribute
    if ((!containing_block_style.IsLeftToRightDirection() &&
         containing_block_style.GetTextAlign() == ETextAlign::kWebkitLeft) ||
        (containing_block_style.IsLeftToRightDirection() &&
         containing_block_style.GetTextAlign() == ETextAlign::kWebkitRight)) {
      if (containing_block_style.IsLeftToRightDirection() !=
          StyleRef().IsLeftToRightDirection()) {
        if (!margin_start_length.IsAuto())
          margin_end_length = Length::Auto();
      } else {
        if (!margin_end_length.IsAuto())
          margin_start_length = Length::Auto();
      }
    }

    // CSS 2.1: "If there is exactly one value specified as 'auto', its used
    // value follows from the equality."
    if (margin_end_length.IsAuto()) {
      margin_start = margin_start_width;
      margin_end = available_width - child_width - margin_start;
      return;
    }

    if (margin_start_length.IsAuto()) {
      margin_end = margin_end_width;
      margin_start = available_width - child_width - margin_end;
      return;
    }
  }

  // Either no auto margins, or our margin box width is >= the container width,
  // auto margins will just turn into 0.
  margin_start = margin_start_width;
  margin_end = margin_end_width;
}

DISABLE_CFI_PERF
void LayoutBox::UpdateLogicalHeight() {
  NOT_DESTROYED();
  if (!HasOverrideLogicalHeight()) {
    // If we have an override height, our children will have sized themselves
    // relative to our override height, which would make our intrinsic size
    // incorrect (too big).
    intrinsic_content_logical_height_ = ContentLogicalHeight();
  }

  LogicalExtentComputedValues computed_values;
  ComputeLogicalHeight(computed_values);

  SetLogicalHeight(computed_values.extent_);
  SetLogicalTop(computed_values.position_);
  SetMarginBefore(computed_values.margins_.before_);
  SetMarginAfter(computed_values.margins_.after_);
}

static inline const Length& HeightForDocumentElement(const Document& document) {
  return document.documentElement()
      ->GetLayoutObject()
      ->StyleRef()
      .LogicalHeight();
}

void LayoutBox::ComputeLogicalHeight(
    LogicalExtentComputedValues& computed_values) const {
  NOT_DESTROYED();
  LayoutUnit height;
  if (HasOverrideIntrinsicContentLogicalHeight()) {
    height = OverrideIntrinsicContentLogicalHeight() +
             BorderAndPaddingLogicalHeight() +
             ComputeLogicalScrollbars().BlockSum();
  } else {
    LayoutUnit default_height = DefaultIntrinsicContentBlockSize();
    if (default_height != kIndefiniteSize) {
      height = default_height + BorderAndPaddingLogicalHeight();
      // <textarea>'s intrinsic size should ignore scrollbar existence.
      if (!IsTextAreaIncludingNG())
        height += ComputeLogicalScrollbars().BlockSum();
      // FIXME: The logical height of the inner editor box should have been
      // added before calling ComputeLogicalHeight to avoid this hack.
      if (IsTextControlIncludingNG())
        SetIntrinsicContentLogicalHeight(default_height);
    } else if (ShouldApplySizeContainment() && !IsLayoutGrid()) {
      height = BorderAndPaddingLogicalHeight() +
               ComputeLogicalScrollbars().BlockSum();
    } else {
      height = LogicalHeight();
    }
  }
  ComputeLogicalHeight(height, LogicalTop(), computed_values);
}

void LayoutBox::ComputeLogicalHeight(
    LayoutUnit logical_height,
    LayoutUnit logical_top,
    LogicalExtentComputedValues& computed_values) const {
  NOT_DESTROYED();
  computed_values.extent_ = logical_height;
  computed_values.position_ = logical_top;

  // Cell height is managed by the table.
  if (IsTableCell())
    return;

  Length h;
  if (IsManagedByLayoutNG(*this) && HasOverrideLogicalHeight()) {
    computed_values.extent_ = OverrideLogicalHeight();
  } else if (IsOutOfFlowPositioned()) {
    ComputePositionedLogicalHeight(computed_values);
    if (HasOverrideLogicalHeight())
      computed_values.extent_ = OverrideLogicalHeight();
  } else {
    LayoutBlock* cb = ContainingBlock();

    // If we are perpendicular to our containing block then we need to resolve
    // our block-start and block-end margins so that if they are 'auto' we are
    // centred or aligned within the inline flow containing block: this is done
    // by computing the margins as though they are inline.
    // Note that as this is the 'sizing phase' we are using our own writing mode
    // rather than the containing block's. We use the containing block's writing
    // mode when figuring out the block-direction margins for positioning in
    // |computeAndSetBlockDirectionMargins| (i.e. margin collapsing etc.).
    // http://www.w3.org/TR/2014/CR-css-writing-modes-3-20140320/#orthogonal-flows
    MarginDirection flow_direction =
        IsHorizontalWritingMode() != cb->IsHorizontalWritingMode()
            ? kInlineDirection
            : kBlockDirection;

    // For tables, calculate margins only.
    if (IsTable()) {
      ComputeMarginsForDirection(
          flow_direction, cb, ContainingBlockLogicalWidthForContent(),
          computed_values.extent_, computed_values.margins_.before_,
          computed_values.margins_.after_, StyleRef().MarginBefore(),
          StyleRef().MarginAfter());
      return;
    }

    bool check_min_max_height = false;

    // The parent box is flexing us, so it has increased or decreased our
    // height. We have to grab our cached flexible height.
    if (HasOverrideLogicalHeight()) {
      h = Length::Fixed(OverrideLogicalHeight());
    } else if (ShouldComputeSizeAsReplaced()) {
      h = Length::Fixed(ComputeReplacedLogicalHeight() +
                        BorderAndPaddingLogicalHeight());
    } else {
      h = StyleRef().LogicalHeight();
      check_min_max_height = true;
    }

    LayoutUnit height_result;
    if (check_min_max_height) {
      if (ShouldComputeLogicalHeightFromAspectRatio()) {
        NGBoxStrut border_padding(BorderStart() + ComputedCSSPaddingStart(),
                                  BorderEnd() + ComputedCSSPaddingEnd(),
                                  BorderBefore() + ComputedCSSPaddingBefore(),
                                  BorderAfter() + ComputedCSSPaddingAfter());
        height_result = BlockSizeFromAspectRatio(
            border_padding, StyleRef().LogicalAspectRatio(),
            StyleRef().BoxSizingForAspectRatio(), LogicalWidth());
      } else {
        height_result = ComputeLogicalHeightUsing(
            kMainOrPreferredSize, h,
            computed_values.extent_ - BorderAndPaddingLogicalHeight());
      }
      if (height_result == -1)
        height_result = computed_values.extent_;
      height_result = ConstrainLogicalHeightByMinMax(
          height_result,
          computed_values.extent_ - BorderAndPaddingLogicalHeight());
    } else {
      DCHECK(h.IsFixed());
      height_result = LayoutUnit(h.Value());
    }

    computed_values.extent_ = height_result;
    ComputeMarginsForDirection(
        flow_direction, cb, ContainingBlockLogicalWidthForContent(),
        computed_values.extent_, computed_values.margins_.before_,
        computed_values.margins_.after_, StyleRef().MarginBefore(),
        StyleRef().MarginAfter());
  }

  // WinIE quirk: The <html> block always fills the entire canvas in quirks
  // mode. The <body> always fills the <html> block in quirks mode. Only apply
  // this quirk if the block is normal flow and no height is specified. When
  // we're printing, we also need this quirk if the body or root has a
  // percentage height since we don't set a height in LayoutView when we're
  // printing. So without this quirk, the height has nothing to be a percentage
  // of, and it ends up being 0. That is bad.
  bool paginated_content_needs_base_height =
      GetDocument().Printing() && h.IsPercentOrCalc() &&
      (IsDocumentElement() ||
       (IsBody() &&
        HeightForDocumentElement(GetDocument()).IsPercentOrCalc())) &&
      !IsInline();
  if (StretchesToViewport() || paginated_content_needs_base_height) {
    LayoutUnit margins = CollapsedMarginBefore() + CollapsedMarginAfter();
    LayoutUnit visible_height = View()->ViewLogicalHeightForPercentages();
    if (IsDocumentElement()) {
      computed_values.extent_ =
          std::max(computed_values.extent_, visible_height - margins);
    } else {
      LayoutUnit margins_borders_padding =
          margins + ParentBox()->MarginBefore() + ParentBox()->MarginAfter() +
          ParentBox()->BorderAndPaddingLogicalHeight();
      computed_values.extent_ = std::max(
          computed_values.extent_, visible_height - margins_borders_padding);
    }
  }
}

LayoutUnit LayoutBox::ComputeLogicalHeightWithoutLayout() const {
  NOT_DESTROYED();
  LogicalExtentComputedValues computed_values;

  if (!SelfNeedsLayout() && HasOverrideIntrinsicContentLogicalHeight()) {
    ComputeLogicalHeight(OverrideIntrinsicContentLogicalHeight() +
                             BorderAndPaddingLogicalHeight(),
                         LayoutUnit(), computed_values);
  } else {
    // TODO(cbiesinger): We should probably return something other than just
    // border + padding, but for now we have no good way to do anything else
    // without layout, so we just use that.
    ComputeLogicalHeight(BorderAndPaddingLogicalHeight(), LayoutUnit(),
                         computed_values);
  }
  return computed_values.extent_;
}

LayoutUnit LayoutBox::ComputeLogicalHeightUsing(
    SizeType height_type,
    const Length& height,
    LayoutUnit intrinsic_content_height) const {
  NOT_DESTROYED();
  LayoutUnit logical_height = ComputeContentAndScrollbarLogicalHeightUsing(
      height_type, height, intrinsic_content_height);
  if (logical_height != -1) {
    if (height.IsSpecified())
      logical_height = AdjustBorderBoxLogicalHeightForBoxSizing(logical_height);
    else
      logical_height += BorderAndPaddingLogicalHeight();
  }
  return logical_height;
}

LayoutUnit LayoutBox::ComputeContentLogicalHeight(
    SizeType height_type,
    const Length& height,
    LayoutUnit intrinsic_content_height) const {
  NOT_DESTROYED();
  LayoutUnit height_including_scrollbar =
      ComputeContentAndScrollbarLogicalHeightUsing(height_type, height,
                                                   intrinsic_content_height);
  if (height_including_scrollbar == -1)
    return LayoutUnit(-1);
  LayoutUnit adjusted = height_including_scrollbar;
  if (height.IsSpecified()) {
    // Keywords don't get adjusted for box-sizing
    adjusted =
        AdjustContentBoxLogicalHeightForBoxSizing(height_including_scrollbar);
  }
  return std::max(LayoutUnit(),
                  adjusted - ComputeLogicalScrollbars().BlockSum());
}

LayoutUnit LayoutBox::ComputeIntrinsicLogicalContentHeightUsing(
    SizeType height_type,
    const Length& logical_height_length,
    LayoutUnit intrinsic_content_height,
    LayoutUnit border_and_padding) const {
  NOT_DESTROYED();
  // FIXME(cbiesinger): The css-sizing spec is considering changing what
  // min-content/max-content should resolve to.
  // If that happens, this code will have to change.
  if (logical_height_length.IsMinContent() ||
      logical_height_length.IsMaxContent() ||
      logical_height_length.IsMinIntrinsic() ||
      logical_height_length.IsFitContent()) {
    if (IsAtomicInlineLevel() && !IsFlexibleBoxIncludingNG() &&
        !IsLayoutGridIncludingNG())
      return IntrinsicSize().Height();
    return intrinsic_content_height;
  }
  if (logical_height_length.IsFillAvailable()) {
    if (!IsA<HTMLMarqueeElement>(GetNode())) {
      UseCounter::Count(GetDocument(),
                        WebFeature::kCSSFillAvailableLogicalHeight);
    }
    const LayoutUnit available_logical_height =
        LayoutBoxUtils::AvailableLogicalHeight(*this, ContainingBlock());
    // If the available logical-height is indefinite fallback to the "default"
    // depending on the |SizeType|.
    if (available_logical_height == -1) {
      if (height_type == kMinSize)
        return LayoutUnit();
      if (height_type == kMainOrPreferredSize)
        return intrinsic_content_height;
      return LayoutUnit::Max();
    }
    return available_logical_height - border_and_padding;
  }
  NOTREACHED();
  return LayoutUnit();
}

LayoutUnit LayoutBox::ComputeContentAndScrollbarLogicalHeightUsing(
    SizeType height_type,
    const Length& height,
    LayoutUnit intrinsic_content_height) const {
  NOT_DESTROYED();
  if (height.IsAuto())
    return height_type == kMinSize ? LayoutUnit() : LayoutUnit(-1);
  // FIXME(cbiesinger): The css-sizing spec is considering changing what
  // min-content/max-content should resolve to.
  // If that happens, this code will have to change.
  if (height.IsContentOrIntrinsicOrFillAvailable()) {
    if (intrinsic_content_height == -1)
      return LayoutUnit(-1);  // Intrinsic height isn't available.
    return ComputeIntrinsicLogicalContentHeightUsing(
               height_type, height, intrinsic_content_height,
               BorderAndPaddingLogicalHeight()) +
           ComputeLogicalScrollbars().BlockSum();
  }
  if (height.IsFixed())
    return LayoutUnit(height.Value());
  if (height.IsPercentOrCalc())
    return ComputePercentageLogicalHeight(height);
  return LayoutUnit(-1);
}

bool LayoutBox::StretchesToViewportInQuirksMode() const {
  NOT_DESTROYED();
  if (!IsDocumentElement() && !IsBody())
    return false;
  return StyleRef().LogicalHeight().IsAuto() &&
         !IsFloatingOrOutOfFlowPositioned() && !IsInline() &&
         !ShouldComputeLogicalHeightFromAspectRatio() &&
         !FlowThreadContainingBlock();
}

bool LayoutBox::SkipContainingBlockForPercentHeightCalculation(
    const LayoutBox* containing_block) {
  const bool in_quirks_mode = containing_block->GetDocument().InQuirksMode();
  // Anonymous blocks should not impede percentage resolution on a child.
  // Examples of such anonymous blocks are blocks wrapped around inlines that
  // have block siblings (from the CSS spec) and multicol flow threads (an
  // implementation detail). Another implementation detail, ruby runs, create
  // anonymous inline-blocks, so skip those too. All other types of anonymous
  // objects, such as table-cells, will be treated just as if they were
  // non-anonymous.
  if (containing_block->IsAnonymous()) {
    if (!in_quirks_mode && containing_block->Parent() &&
        containing_block->Parent()->IsLayoutNGFieldset())
      return false;
    EDisplay display = containing_block->StyleRef().Display();
    return display == EDisplay::kBlock || display == EDisplay::kInlineBlock ||
           display == EDisplay::kFlowRoot;
  }

  // For quirks mode, we skip most auto-height containing blocks when computing
  // percentages.
  if (!in_quirks_mode || !containing_block->StyleRef().LogicalHeight().IsAuto())
    return false;

  const Node* node = containing_block->GetNode();
  if (UNLIKELY(node->IsInUserAgentShadowRoot())) {
    const Element* host = node->OwnerShadowHost();
    if (const auto* input = DynamicTo<HTMLInputElement>(host)) {
      // In web_tests/fast/forms/range/range-thumb-height-percentage.html, a
      // percent height for the slider thumb element should refer to the height
      // of the INPUT box.
      if (input->type() == input_type_names::kRange)
        return true;
    }
  }

  return !containing_block->IsTableCell() &&
         !containing_block->IsOutOfFlowPositioned() &&
         !containing_block->HasOverridePercentageResolutionBlockSize() &&
         !containing_block->IsLayoutGridIncludingNG() &&
         !containing_block->IsFlexibleBoxIncludingDeprecatedAndNG() &&
         !containing_block->IsLayoutNGCustom();
}

LayoutUnit LayoutBox::ContainingBlockLogicalHeightForPercentageResolution(
    LayoutBlock** out_cb,
    bool* out_skipped_auto_height_containing_block) const {
  NOT_DESTROYED();
  LayoutBlock* cb = ContainingBlock();
  const LayoutBlock* const real_cb = cb;
  const LayoutBox* containing_block_child = this;
  bool skipped_auto_height_containing_block = false;
  LayoutUnit root_margin_border_padding_height;
  while (!IsA<LayoutView>(cb) &&
         (IsHorizontalWritingMode() == cb->IsHorizontalWritingMode() &&
          SkipContainingBlockForPercentHeightCalculation(cb))) {
    if ((cb->IsBody() || cb->IsDocumentElement()) &&
        !HasOverrideContainingBlockContentLogicalHeight())
      root_margin_border_padding_height += cb->MarginBefore() +
                                           cb->MarginAfter() +
                                           cb->BorderAndPaddingLogicalHeight();
    skipped_auto_height_containing_block = true;
    containing_block_child = cb;
    cb = cb->ContainingBlock();
  }

  if (out_cb)
    *out_cb = cb;

  if (out_skipped_auto_height_containing_block) {
    *out_skipped_auto_height_containing_block =
        skipped_auto_height_containing_block;
  }

  LayoutUnit available_height(-1);
  if (containing_block_child->HasOverridePercentageResolutionBlockSize()) {
    available_height =
        containing_block_child->OverridePercentageResolutionBlockSize();
  } else if (cb->HasOverridePercentageResolutionBlockSize()) {
    available_height = cb->OverridePercentageResolutionBlockSize();
  } else if (HasOverrideContainingBlockContentLogicalWidth() &&
             IsHorizontalWritingMode() != real_cb->IsHorizontalWritingMode()) {
    available_height = OverrideContainingBlockContentLogicalWidth();
  } else if (HasOverrideContainingBlockContentLogicalHeight() &&
             IsHorizontalWritingMode() == real_cb->IsHorizontalWritingMode()) {
    available_height = OverrideContainingBlockContentLogicalHeight();
  } else if (IsHorizontalWritingMode() != cb->IsHorizontalWritingMode()) {
    available_height =
        containing_block_child->ContainingBlockLogicalWidthForContent();
  } else if (cb->IsTableCell()) {
    if (!skipped_auto_height_containing_block) {
      // Table cells violate what the CSS spec says to do with heights.
      // Basically we don't care if the cell specified a height or not. We just
      // always make ourselves be a percentage of the cell's current content
      // height.
      if (!cb->HasOverrideLogicalHeight()) {
        // https://drafts.csswg.org/css-tables-3/#row-layout:
        // For the purpose of calculating [the minimum height of a row],
        // descendants of table cells whose height depends on percentages
        // of their parent cell's height are considered to have an auto
        // height if they have overflow set to visible or hidden or if
        // they are replaced elements, and a 0px height if they have not.
        const LayoutNGTableCellInterface* cell =
            ToInterface<LayoutNGTableCellInterface>(cb);
        if (StyleRef().OverflowY() != EOverflow::kVisible &&
            StyleRef().OverflowY() != EOverflow::kHidden &&
            !ShouldBeConsideredAsReplaced() &&
            (!cb->StyleRef().LogicalHeight().IsAuto() || !cell->TableInterface()
                                                              ->ToLayoutObject()
                                                              ->StyleRef()
                                                              .LogicalHeight()
                                                              .IsAuto()))
          return LayoutUnit();
        return LayoutUnit(-1);
      }
      available_height = cb->OverrideLogicalHeight() -
                         cb->CollapsedBorderAndCSSPaddingLogicalHeight() -
                         cb->ComputeLogicalScrollbars().BlockSum();
    }
  } else {
    available_height = cb->AvailableLogicalHeightForPercentageComputation();
  }

  if (available_height == -1)
    return available_height;

  available_height = std::max(
      available_height - root_margin_border_padding_height, LayoutUnit());

  // LayoutNG already includes padding in
  // OverrideContainingBlockContentLogicalHeight so we only need to add it here
  // for legacy containing blocks.
  if (IsTable() && IsOutOfFlowPositioned() && !cb->IsLayoutNGObject())
    available_height += cb->PaddingLogicalHeight();

  return available_height;
}

LayoutUnit LayoutBox::ComputePercentageLogicalHeight(
    const Length& height) const {
  NOT_DESTROYED();
  bool skipped_auto_height_containing_block = false;
  LayoutBlock* cb = nullptr;
  LayoutUnit available_height =
      ContainingBlockLogicalHeightForPercentageResolution(
          &cb, &skipped_auto_height_containing_block);

  DCHECK(cb);
  cb->AddPercentHeightDescendant(const_cast<LayoutBox*>(this));

  if (available_height == -1)
    return available_height;

  LayoutUnit result = ValueForLength(height, available_height);

  // |OverrideLogicalHeight| is the maximum height made available by the
  // cell to its percent height children when we decide they can determine the
  // height of the cell. If the percent height child is box-sizing:content-box
  // then we must subtract the border and padding from the cell's
  // |available_height| (given by |OverrideLogicalHeight|) to arrive
  // at the child's computed height.
  bool subtract_border_and_padding =
      IsTable() ||
      (!RuntimeEnabledFeatures::LayoutNGEnabled() && cb->IsTableCell() &&
       !skipped_auto_height_containing_block &&
       cb->HasOverrideLogicalHeight() &&
       StyleRef().BoxSizing() == EBoxSizing::kContentBox);
  if (subtract_border_and_padding) {
    result -= BorderAndPaddingLogicalHeight();
    return std::max(LayoutUnit(), result);
  }
  return result;
}

LayoutUnit LayoutBox::ComputeReplacedLogicalWidth(
    ShouldComputePreferred should_compute_preferred) const {
  NOT_DESTROYED();
  return ComputeReplacedLogicalWidthRespectingMinMaxWidth(
      ComputeReplacedLogicalWidthUsing(kMainOrPreferredSize,
                                       StyleRef().LogicalWidth()),
      should_compute_preferred);
}

LayoutUnit LayoutBox::ComputeReplacedLogicalWidthRespectingMinMaxWidth(
    LayoutUnit logical_width,
    ShouldComputePreferred should_compute_preferred) const {
  NOT_DESTROYED();
  LayoutUnit min_logical_width =
      (should_compute_preferred == kComputePreferred &&
       StyleRef().LogicalMinWidth().IsPercentOrCalc())
          ? logical_width
          : ComputeReplacedLogicalWidthUsing(kMinSize,
                                             StyleRef().LogicalMinWidth());
  LayoutUnit max_logical_width =
      (should_compute_preferred == kComputePreferred &&
       StyleRef().LogicalMaxWidth().IsPercentOrCalc()) ||
              StyleRef().LogicalMaxWidth().IsNone()
          ? logical_width
          : ComputeReplacedLogicalWidthUsing(kMaxSize,
                                             StyleRef().LogicalMaxWidth());
  return std::max(min_logical_width,
                  std::min(logical_width, max_logical_width));
}

LayoutUnit LayoutBox::ComputeReplacedLogicalWidthUsing(
    SizeType size_type,
    Length logical_width) const {
  NOT_DESTROYED();
  DCHECK(size_type == kMinSize || size_type == kMainOrPreferredSize ||
         !logical_width.IsAuto());
  if (size_type == kMinSize && logical_width.IsAuto())
    return AdjustContentBoxLogicalWidthForBoxSizing(LayoutUnit());
  if (size_type == kMainOrPreferredSize && logical_width.IsAuto() &&
      StretchInlineSizeIfAuto())
    logical_width = Length::FillAvailable();

  switch (logical_width.GetType()) {
    case Length::kFixed:
      return AdjustContentBoxLogicalWidthForBoxSizing(logical_width.Value());
    case Length::kMinContent:
    case Length::kMaxContent:
    case Length::kMinIntrinsic: {
      // MinContent/MaxContent don't need the availableLogicalWidth argument.
      LayoutUnit available_logical_width;
      return ComputeIntrinsicLogicalWidthUsing(logical_width,
                                               available_logical_width) -
             BorderAndPaddingLogicalWidth();
    }
    case Length::kFitContent:
    case Length::kFillAvailable:
    case Length::kPercent:
    case Length::kCalculated: {
      LayoutUnit cw;
      if (IsOutOfFlowPositioned()) {
        cw = ContainingBlockLogicalWidthForPositioned(
            To<LayoutBoxModelObject>(Container()));
      } else {
        cw = IsHorizontalWritingMode() ==
                     ContainingBlock()->IsHorizontalWritingMode()
                 ? ContainingBlockLogicalWidthForContent()
                 : PerpendicularContainingBlockLogicalHeight();
      }
      const Length& container_logical_width =
          ContainingBlock()->StyleRef().LogicalWidth();
      // FIXME: Handle cases when containing block width is calculated or
      // viewport percent. https://bugs.webkit.org/show_bug.cgi?id=91071
      if (logical_width.IsContentOrIntrinsicOrFillAvailable())
        return ComputeIntrinsicLogicalWidthUsing(logical_width, cw) -
               BorderAndPaddingLogicalWidth();
      if (cw > 0 || (!cw && (container_logical_width.IsFixed() ||
                             container_logical_width.IsPercentOrCalc())))
        return AdjustContentBoxLogicalWidthForBoxSizing(
            MinimumValueForLength(logical_width, cw));
      return LayoutUnit();
    }
    case Length::kAuto:
    case Length::kNone:
      return IntrinsicLogicalWidth();
    case Length::kExtendToZoom:
    case Length::kDeviceWidth:
    case Length::kDeviceHeight:
    case Length::kContent:
      break;
  }

  NOTREACHED();
  return LayoutUnit();
}

LayoutUnit LayoutBox::ComputeReplacedLogicalHeight(LayoutUnit) const {
  NOT_DESTROYED();
  return ComputeReplacedLogicalHeightRespectingMinMaxHeight(
      ComputeReplacedLogicalHeightUsing(kMainOrPreferredSize,
                                        StyleRef().LogicalHeight()));
}

bool LayoutBox::LogicalHeightComputesAsNone(SizeType size_type) const {
  NOT_DESTROYED();
  DCHECK(size_type == kMinSize || size_type == kMaxSize);
  const Length& logical_height = size_type == kMinSize
                                     ? StyleRef().LogicalMinHeight()
                                     : StyleRef().LogicalMaxHeight();

  // Note that the values 'min-content', 'max-content' and 'fit-content' should
  // behave as the initial value if specified in the block direction.
  if (logical_height.IsMinContent() || logical_height.IsMaxContent() ||
      logical_height.IsMinIntrinsic() || logical_height.IsFitContent())
    return true;

  Length initial_logical_height =
      size_type == kMinSize ? ComputedStyleInitialValues::InitialMinHeight()
                            : ComputedStyleInitialValues::InitialMaxHeight();

  if (logical_height == initial_logical_height)
    return true;

  if (logical_height.IsPercentOrCalc() &&
      HasOverrideContainingBlockContentLogicalHeight()) {
    if (OverrideContainingBlockContentLogicalHeight() == kIndefiniteSize)
      return true;
    else if (!GetDocument().InQuirksMode())
      return false;
  }

  // CustomLayout items can resolve their percentages against an available or
  // percentage size override.
  if (IsCustomItem() && (HasOverrideContainingBlockContentLogicalHeight() ||
                         HasOverridePercentageResolutionBlockSize()))
    return false;

  if (LayoutBlock* cb = ContainingBlockForAutoHeightDetection(logical_height))
    return cb->HasAutoHeightOrContainingBlockWithAutoHeight();
  return false;
}

LayoutUnit LayoutBox::ComputeReplacedLogicalHeightRespectingMinMaxHeight(
    LayoutUnit logical_height) const {
  NOT_DESTROYED();
  // If the height of the containing block is not specified explicitly (i.e., it
  // depends on content height), and this element is not absolutely positioned,
  // the percentage value is treated as '0' (for 'min-height') or 'none' (for
  // 'max-height').
  LayoutUnit min_logical_height;
  if (!LogicalHeightComputesAsNone(kMinSize)) {
    min_logical_height = ComputeReplacedLogicalHeightUsing(
        kMinSize, StyleRef().LogicalMinHeight());
  }
  LayoutUnit max_logical_height = logical_height;
  if (!LogicalHeightComputesAsNone(kMaxSize)) {
    max_logical_height = ComputeReplacedLogicalHeightUsing(
        kMaxSize, StyleRef().LogicalMaxHeight());
  }
  return std::max(min_logical_height,
                  std::min(logical_height, max_logical_height));
}

LayoutUnit LayoutBox::ComputeReplacedLogicalHeightUsing(
    SizeType size_type,
    Length logical_height) const {
  NOT_DESTROYED();
  DCHECK(size_type == kMinSize || size_type == kMainOrPreferredSize ||
         !logical_height.IsAuto());
  if (size_type == kMinSize && logical_height.IsAuto())
    return AdjustContentBoxLogicalHeightForBoxSizing(LayoutUnit());
  if (size_type == kMainOrPreferredSize && logical_height.IsAuto() &&
      StretchBlockSizeIfAuto())
    logical_height = Length::FillAvailable();

  switch (logical_height.GetType()) {
    case Length::kFixed:
      return AdjustContentBoxLogicalHeightForBoxSizing(logical_height.Value());
    case Length::kPercent:
    case Length::kCalculated: {
      // TODO(rego): Check if we can somehow reuse
      // LayoutBox::computePercentageLogicalHeight() and/or
      // LayoutBlock::availableLogicalHeightForPercentageComputation() (see
      // http://crbug.com/635655).
      LayoutObject* cb =
          IsOutOfFlowPositioned() ? Container() : ContainingBlock();
      while (cb->IsAnonymous())
        cb = cb->ContainingBlock();
      bool has_perpendicular_containing_block =
          cb->IsHorizontalWritingMode() != IsHorizontalWritingMode();
      LayoutUnit stretched_height(-1);
      auto* block = DynamicTo<LayoutBlock>(cb);
      if (block) {
        block->AddPercentHeightDescendant(const_cast<LayoutBox*>(this));
        if (block->IsFlexItem()) {
          const auto* flex_box = To<LayoutFlexibleBox>(block->Parent());
          if (flex_box->UseOverrideLogicalHeightForPerentageResolution(*block))
            stretched_height = block->OverrideContentLogicalHeight();
        } else if (block->IsGridItem() && block->HasOverrideLogicalHeight() &&
                   !has_perpendicular_containing_block) {
          stretched_height = block->OverrideContentLogicalHeight();
        }
      }

      LayoutUnit available_height;
      if (IsOutOfFlowPositioned()) {
        available_height = ContainingBlockLogicalHeightForPositioned(
            To<LayoutBoxModelObject>(cb));
      } else if (stretched_height != -1) {
        available_height = stretched_height;
      } else if (HasOverridePercentageResolutionBlockSize()) {
        available_height = OverridePercentageResolutionBlockSize();
      } else {
        available_height = has_perpendicular_containing_block
                               ? ContainingBlockLogicalWidthForContent()
                               : ContainingBlockLogicalHeightForContent(
                                     kIncludeMarginBorderPadding);

        // It is necessary to use the border-box to match WinIE's broken
        // box model.  This is essential for sizing inside
        // table cells using percentage heights.
        // FIXME: This needs to be made writing-mode-aware. If the cell and
        // image are perpendicular writing-modes, this isn't right.
        // https://bugs.webkit.org/show_bug.cgi?id=46997
        while (!IsA<LayoutView>(cb) &&
               (cb->StyleRef().LogicalHeight().IsAuto() ||
                cb->StyleRef().LogicalHeight().IsPercentOrCalc())) {
          if (!RuntimeEnabledFeatures::LayoutNGEnabled() && cb->IsTableCell()) {
            // Don't let table cells squeeze percent-height replaced elements
            // <http://bugs.webkit.org/show_bug.cgi?id=15359>
            available_height =
                std::max(available_height, IntrinsicLogicalHeight());
            return ValueForLength(
                logical_height,
                available_height - BorderAndPaddingLogicalHeight());
          }
          To<LayoutBlock>(cb)->AddPercentHeightDescendant(
              const_cast<LayoutBox*>(this));
          cb = cb->ContainingBlock();
        }
      }

      return AdjustContentBoxLogicalHeightForBoxSizing(
          (RuntimeEnabledFeatures::LayoutNGEnabled() &&
           available_height == kIndefiniteSize)
              ? IntrinsicLogicalHeight()
              : ValueForLength(logical_height, available_height));
    }
    case Length::kMinContent:
    case Length::kMaxContent:
    case Length::kFitContent:
    case Length::kFillAvailable:
      return AdjustContentBoxLogicalHeightForBoxSizing(
          ComputeIntrinsicLogicalContentHeightUsing(size_type, logical_height,
                                                    IntrinsicLogicalHeight(),
                                                    BorderAndPaddingHeight()));
    default:
      return IntrinsicLogicalHeight();
  }
}

LayoutUnit LayoutBox::AvailableLogicalHeight(
    AvailableLogicalHeightType height_type) const {
  NOT_DESTROYED();
  if (RuntimeEnabledFeatures::LayoutNGEnabled()) {
    // LayoutNG code is correct, Legacy code incorrectly ConstrainsMinMax
    // when height is -1, and returns 0, not -1.
    // The reason this code is NG-only is that this code causes performance
    // regression for nested-percent-height-tables test case.
    // This code gets executed 740 times in the test case.
    // https://chromium-review.googlesource.com/c/chromium/src/+/1103289
    LayoutUnit height =
        AvailableLogicalHeightUsing(StyleRef().LogicalHeight(), height_type);
    if (UNLIKELY(height == -1))
      return height;
    return ConstrainContentBoxLogicalHeightByMinMax(height, LayoutUnit(-1));
  }
  // http://www.w3.org/TR/CSS2/visudet.html#propdef-height - We are interested
  // in the content height.
  // FIXME: Should we pass intrinsicContentLogicalHeight() instead of -1 here?
  return ConstrainContentBoxLogicalHeightByMinMax(
      AvailableLogicalHeightUsing(StyleRef().LogicalHeight(), height_type),
      LayoutUnit(-1));
}

LayoutUnit LayoutBox::AvailableLogicalHeightUsing(
    const Length& h,
    AvailableLogicalHeightType height_type) const {
  NOT_DESTROYED();
  if (auto* layout_view = DynamicTo<LayoutView>(this)) {
    return LayoutUnit(IsHorizontalWritingMode()
                          ? layout_view->GetFrameView()->Size().height()
                          : layout_view->GetFrameView()->Size().width());
  }

  // We need to stop here, since we don't want to increase the height of the
  // table artificially.  We're going to rely on this cell getting expanded to
  // some new height, and then when we lay out again we'll use the calculation
  // below.
  if (IsTableCell() && (h.IsAuto() || h.IsPercentOrCalc())) {
    if (HasOverrideLogicalHeight()) {
      return OverrideLogicalHeight() -
             CollapsedBorderAndCSSPaddingLogicalHeight() -
             ComputeLogicalScrollbars().BlockSum();
    }
    return LogicalHeight() - BorderAndPaddingLogicalHeight();
  }

  if (IsFlexItemIncludingNG()) {
    if (IsFlexItem()) {
      const auto& flex_box = To<LayoutFlexibleBox>(*Parent());
      if (flex_box.UseOverrideLogicalHeightForPerentageResolution(*this))
        return OverrideContentLogicalHeight();
    } else if (HasOverrideContainingBlockContentLogicalWidth() &&
               IsOrthogonalWritingModeRoot()) {
      return OverrideContainingBlockContentLogicalWidth();
    } else if (HasOverrideLogicalHeight() &&
               IsOverrideLogicalHeightDefinite()) {
      return OverrideContentLogicalHeight();
    } else if (!GetBoxLayoutExtraInput()) {
      // TODO(ikilpatrick): Remove this post M86.
      if (const auto* previous_result = GetCachedLayoutResult()) {
        const NGConstraintSpace& space =
            previous_result->GetConstraintSpaceForCaching();
        if (space.IsFixedBlockSize() && !space.IsInitialBlockSizeIndefinite())
          return space.AvailableSize().block_size;
      }
    }
  }
  if (ShouldComputeLogicalHeightFromAspectRatio()) {
    NGBoxStrut border_padding(BorderStart() + ComputedCSSPaddingStart(),
                              BorderEnd() + ComputedCSSPaddingEnd(),
                              BorderBefore() + ComputedCSSPaddingBefore(),
                              BorderAfter() + ComputedCSSPaddingAfter());
    return BlockSizeFromAspectRatio(
        border_padding, StyleRef().LogicalAspectRatio(),
        StyleRef().BoxSizingForAspectRatio(), LogicalWidth());
  }

  if (h.IsPercentOrCalc() && IsOutOfFlowPositioned()) {
    // FIXME: This is wrong if the containingBlock has a perpendicular writing
    // mode.
    LayoutUnit available_height =
        ContainingBlockLogicalHeightForPositioned(ContainingBlock());
    return AdjustContentBoxLogicalHeightForBoxSizing(
        ValueForLength(h, available_height));
  }

  // FIXME: Should we pass intrinsicContentLogicalHeight() instead of -1 here?
  LayoutUnit height_including_scrollbar =
      ComputeContentAndScrollbarLogicalHeightUsing(kMainOrPreferredSize, h,
                                                   LayoutUnit(-1));
  if (height_including_scrollbar != -1) {
    return std::max(LayoutUnit(), AdjustContentBoxLogicalHeightForBoxSizing(
                                      height_including_scrollbar) -
                                      ComputeLogicalScrollbars().BlockSum());
  }

  // FIXME: Check logicalTop/logicalBottom here to correctly handle vertical
  // writing-mode.
  // https://bugs.webkit.org/show_bug.cgi?id=46500
  auto* curr_layout_block = DynamicTo<LayoutBlock>(this);
  if (curr_layout_block && IsOutOfFlowPositioned() &&
      StyleRef().Height().IsAuto() &&
      !(StyleRef().Top().IsAuto() || StyleRef().Bottom().IsAuto())) {
    LayoutBlock* block = const_cast<LayoutBlock*>(curr_layout_block);
    LogicalExtentComputedValues computed_values;
    block->ComputeLogicalHeight(block->LogicalHeight(), LayoutUnit(),
                                computed_values);
    return computed_values.extent_ - block->BorderAndPaddingLogicalHeight() -
           block->ComputeLogicalScrollbars().BlockSum();
  }

  // FIXME: This is wrong if the containingBlock has a perpendicular writing
  // mode.
  LayoutUnit available_height =
      ContainingBlockLogicalHeightForContent(height_type);
  // FIXME: This is incorrect if available_height == -1 || 0
  if (height_type == kExcludeMarginBorderPadding) {
    // FIXME: Margin collapsing hasn't happened yet, so this incorrectly removes
    // collapsed margins.
    available_height -=
        MarginBefore() + MarginAfter() + BorderAndPaddingLogicalHeight();
  }
  return available_height;
}

void LayoutBox::ComputeAndSetBlockDirectionMargins(
    const LayoutBlock* containing_block) {
  NOT_DESTROYED();
  LayoutUnit margin_before;
  LayoutUnit margin_after;
  DCHECK(containing_block);
  ComputeMarginsForDirection(
      kBlockDirection, containing_block,
      ContainingBlockLogicalWidthForContent(), LogicalHeight(), margin_before,
      margin_after, StyleRef().MarginBeforeUsing(containing_block->StyleRef()),
      StyleRef().MarginAfterUsing(containing_block->StyleRef()));
  // Note that in this 'positioning phase' of the layout we are using the
  // containing block's writing mode rather than our own when calculating
  // margins.
  // http://www.w3.org/TR/2014/CR-css-writing-modes-3-20140320/#orthogonal-flows
  containing_block->SetMarginBeforeForChild(*this, margin_before);
  containing_block->SetMarginAfterForChild(*this, margin_after);
}

LayoutUnit LayoutBox::ContainingBlockLogicalWidthForPositioned(
    const LayoutBoxModelObject* containing_block,
    bool check_for_perpendicular_writing_mode) const {
  NOT_DESTROYED();
  if (check_for_perpendicular_writing_mode &&
      containing_block->IsHorizontalWritingMode() != IsHorizontalWritingMode())
    return ContainingBlockLogicalHeightForPositioned(containing_block, false);

  // Use viewport as container for top-level fixed-position elements.
  const auto* view = DynamicTo<LayoutView>(containing_block);
  if (StyleRef().GetPosition() == EPosition::kFixed && view &&
      !GetDocument().Printing()) {
    if (LocalFrameView* frame_view = view->GetFrameView()) {
      // Don't use visibleContentRect since the PaintLayer's size has not been
      // set yet.
      LayoutSize viewport_size(
          frame_view->LayoutViewport()->ExcludeScrollbars(frame_view->Size()));
      return LayoutUnit(containing_block->IsHorizontalWritingMode()
                            ? viewport_size.Width()
                            : viewport_size.Height());
    }
  }

  if (HasOverrideContainingBlockContentLogicalWidth())
    return OverrideContainingBlockContentLogicalWidth();

  if (containing_block->IsAnonymousBlock() &&
      containing_block->IsRelPositioned()) {
    // Ensure we compute our width based on the width of our rel-pos inline
    // container rather than any anonymous block created to manage a block-flow
    // ancestor of ours in the rel-pos inline's inline flow.
    containing_block = To<LayoutBox>(containing_block)->Continuation();
    // There may be nested parallel inline continuations. We have now found the
    // innermost inline (which may not be relatively positioned). Locate the
    // inline that serves as the containing block of this box.
    while (!containing_block->CanContainOutOfFlowPositionedElement(
        StyleRef().GetPosition())) {
      containing_block =
          To<LayoutBoxModelObject>(containing_block->Container());
      DCHECK(containing_block->IsLayoutInline());
    }
  } else if (containing_block->IsBox()) {
    return std::max(LayoutUnit(),
                    To<LayoutBox>(containing_block)->ClientLogicalWidth());
  }

  DCHECK(containing_block->IsLayoutInline());
  DCHECK(containing_block->CanContainOutOfFlowPositionedElement(
      StyleRef().GetPosition()));

  const auto* flow = To<LayoutInline>(containing_block);
  InlineFlowBox* first = flow->FirstLineBox();
  InlineFlowBox* last = flow->LastLineBox();

  // If the containing block is empty, return a width of 0.
  if (!first || !last)
    return LayoutUnit();

  LayoutUnit from_left;
  LayoutUnit from_right;
  if (containing_block->StyleRef().IsLeftToRightDirection()) {
    from_left = first->LogicalLeft() + first->BorderLogicalLeft();
    from_right =
        last->LogicalLeft() + last->LogicalWidth() - last->BorderLogicalRight();
  } else {
    from_right = first->LogicalLeft() + first->LogicalWidth() -
                 first->BorderLogicalRight();
    from_left = last->LogicalLeft() + last->BorderLogicalLeft();
  }

  return std::max(LayoutUnit(), from_right - from_left);
}

LayoutUnit LayoutBox::ContainingBlockLogicalHeightForPositioned(
    const LayoutBoxModelObject* containing_block,
    bool check_for_perpendicular_writing_mode) const {
  NOT_DESTROYED();
  if (check_for_perpendicular_writing_mode &&
      containing_block->IsHorizontalWritingMode() != IsHorizontalWritingMode())
    return ContainingBlockLogicalWidthForPositioned(containing_block, false);

  // Use viewport as container for top-level fixed-position elements.
  const auto* view = DynamicTo<LayoutView>(containing_block);
  if (StyleRef().GetPosition() == EPosition::kFixed && view &&
      !GetDocument().Printing()) {
    if (LocalFrameView* frame_view = view->GetFrameView()) {
      // Don't use visibleContentRect since the PaintLayer's size has not been
      // set yet.
      LayoutSize viewport_size(
          frame_view->LayoutViewport()->ExcludeScrollbars(frame_view->Size()));
      return containing_block->IsHorizontalWritingMode()
                 ? viewport_size.Height()
                 : viewport_size.Width();
    }
  }

  if (HasOverrideContainingBlockContentLogicalHeight())
    return OverrideContainingBlockContentLogicalHeight();

  if (containing_block->IsBox())
    return To<LayoutBox>(containing_block)->ClientLogicalHeight();

  DCHECK(containing_block->IsLayoutInline());
  DCHECK(containing_block->CanContainOutOfFlowPositionedElement(
      StyleRef().GetPosition()));

  const auto* flow = To<LayoutInline>(containing_block);
  // If the containing block is empty, return a height of 0.
  if (!flow->HasInlineFragments())
    return LayoutUnit();

  LayoutUnit height_result;
  auto bounding_box_size = flow->PhysicalLinesBoundingBox().size;
  if (containing_block->IsHorizontalWritingMode())
    height_result = bounding_box_size.height;
  else
    height_result = bounding_box_size.width;
  height_result -=
      (containing_block->BorderBefore() + containing_block->BorderAfter());
  return height_result;
}

static LayoutUnit AccumulateStaticOffsetForFlowThread(
    LayoutBox& layout_box,
    LayoutUnit inline_position,
    LayoutUnit& block_position) {
  if (layout_box.IsLegacyTableRow())
    return LayoutUnit();
  block_position += layout_box.LogicalTop();
  if (!layout_box.IsLayoutFlowThread())
    return LayoutUnit();
  LayoutUnit previous_inline_position = inline_position;
  // We're walking out of a flowthread here. This flow thread is not in the
  // containing block chain, so we need to convert the position from the
  // coordinate space of this flowthread to the containing coordinate space.
  To<LayoutFlowThread>(layout_box)
      .FlowThreadToContainingCoordinateSpace(block_position, inline_position);
  return inline_position - previous_inline_position;
}

void LayoutBox::ComputeInlineStaticDistance(
    Length& logical_left,
    Length& logical_right,
    const LayoutBox* child,
    const LayoutBoxModelObject* container_block,
    LayoutUnit container_logical_width,
    const NGBoxFragmentBuilder* fragment_builder) {
  if (!logical_left.IsAuto() || !logical_right.IsAuto())
    return;

  LayoutObject* parent = child->Parent();
  TextDirection parent_direction = parent->StyleRef().Direction();

  // This method is using EnclosingBox() which is wrong for absolutely
  // positioned grid items, as they rely on the grid area. So for grid items if
  // both "left" and "right" properties are "auto", we can consider that one of
  // them (depending on the direction) is simply "0".
  if (parent->IsLayoutGrid() && parent == child->ContainingBlock()) {
    if (parent_direction == TextDirection::kLtr)
      logical_left = Length::Fixed(0);
    else
      logical_right = Length::Fixed(0);
    return;
  }

  // For multicol we also need to keep track of the block position, since that
  // determines which column we're in and thus affects the inline position.
  LayoutUnit static_block_position = child->Layer()->StaticBlockPosition();

  // FIXME: The static distance computation has not been patched for mixed
  // writing modes yet.
  if (parent_direction == TextDirection::kLtr) {
    LayoutUnit static_position = child->Layer()->StaticInlinePosition() -
                                 container_block->BorderLogicalLeft();
    for (LayoutObject* curr = child->Parent(); curr && curr != container_block;
         curr = curr->Container()) {
      if (auto* box = DynamicTo<LayoutBox>(curr)) {
        static_position +=
            (fragment_builder &&
             fragment_builder->GetLayoutObject() == curr->Parent())
                ? fragment_builder->GetChildOffset(curr).inline_offset
                : box->LogicalLeft();
        if (box->IsInFlowPositioned())
          static_position += box->OffsetForInFlowPosition().left;
        if (curr->IsInsideFlowThread()) {
          static_position += AccumulateStaticOffsetForFlowThread(
              *box, static_position, static_block_position);
        }
      } else if (curr->IsInline() && curr->IsInFlowPositioned()) {
        if (!curr->IsInLayoutNGInlineFormattingContext()) {
          if (!curr->StyleRef().LogicalLeft().IsAuto())
            static_position +=
                ValueForLength(curr->StyleRef().LogicalLeft(),
                               curr->ContainingBlock()->AvailableWidth());
          else
            static_position -=
                ValueForLength(curr->StyleRef().LogicalRight(),
                               curr->ContainingBlock()->AvailableWidth());
        }
      }
    }
    logical_left = Length::Fixed(static_position);
  } else {
    LayoutBox* enclosing_box = child->Parent()->EnclosingBox();
    LayoutUnit static_position = child->Layer()->StaticInlinePosition() +
                                 container_logical_width +
                                 container_block->BorderLogicalLeft();
    if (container_block->IsBox()) {
      static_position +=
          To<LayoutBox>(container_block)->LogicalLeftScrollbarWidth();
    }
    for (LayoutObject* curr = child->Parent(); curr; curr = curr->Container()) {
      if (auto* box = DynamicTo<LayoutBox>(curr)) {
        if (curr == enclosing_box)
          static_position -= enclosing_box->LogicalWidth();
        if (curr != container_block) {
          static_position -=
              (fragment_builder &&
               fragment_builder->GetLayoutObject() == curr->Parent())
                  ? fragment_builder->GetChildOffset(curr).inline_offset
                  : box->LogicalLeft();
          if (box->IsInFlowPositioned()) {
            static_position -= box->OffsetForInFlowPosition().left;
          }
          if (curr->IsInsideFlowThread()) {
            static_position -= AccumulateStaticOffsetForFlowThread(
                *box, static_position, static_block_position);
          }
        }
      } else if (curr->IsInline() && curr->IsInFlowPositioned()) {
        if (!curr->IsInLayoutNGInlineFormattingContext()) {
          if (!curr->StyleRef().LogicalLeft().IsAuto())
            static_position -=
                ValueForLength(curr->StyleRef().LogicalLeft(),
                               curr->ContainingBlock()->AvailableWidth());
          else
            static_position +=
                ValueForLength(curr->StyleRef().LogicalRight(),
                               curr->ContainingBlock()->AvailableWidth());
        }
      }
      if (curr == container_block)
        break;
    }
    logical_right = Length::Fixed(static_position);
  }
}

void LayoutBox::ComputePositionedLogicalWidth(
    LogicalExtentComputedValues& computed_values) const {
  NOT_DESTROYED();
  // QUESTIONS
  // FIXME 1: Should we still deal with these the cases of 'left' or 'right'
  // having the type 'static' in determining whether to calculate the static
  // distance?
  // NOTE: 'static' is not a legal value for 'left' or 'right' as of CSS 2.1.

  // FIXME 2: Can perhaps optimize out cases when max-width/min-width are
  // greater than or less than the computed width(). Be careful of box-sizing
  // and percentage issues.

  // The following is based off of the W3C Working Draft from April 11, 2006 of
  // CSS 2.1: Section 10.3.7 "Absolutely positioned, non-replaced elements"
  // <http://www.w3.org/TR/CSS21/visudet.html#abs-non-replaced-width>
  // (block-style-comments in this function and in
  // computePositionedLogicalWidthUsing() correspond to text from the spec)

  // We don't use containingBlock(), since we may be positioned by an enclosing
  // relative positioned inline.
  const auto* container_block = To<LayoutBoxModelObject>(Container());

  const LayoutUnit container_logical_width =
      ContainingBlockLogicalWidthForPositioned(container_block);

  // Use the container block's direction except when calculating the static
  // distance. This conforms with the reference results for
  // abspos-replaced-width-margin-000.htm of the CSS 2.1 test suite.
  TextDirection container_direction = container_block->StyleRef().Direction();

  bool is_horizontal = IsHorizontalWritingMode();
  const LayoutUnit borders_plus_padding = BorderAndPaddingLogicalWidth();
  const Length& margin_logical_left =
      is_horizontal ? StyleRef().MarginLeft() : StyleRef().MarginTop();
  const Length& margin_logical_right =
      is_horizontal ? StyleRef().MarginRight() : StyleRef().MarginBottom();

  Length logical_left_length = StyleRef().LogicalLeft();
  Length logical_right_length = StyleRef().LogicalRight();
  // ---------------------------------------------------------------------------
  //  For the purposes of this section and the next, the term "static position"
  //  (of an element) refers, roughly, to the position an element would have had
  //  in the normal flow. More precisely:
  //
  //  * The static position for 'left' is the distance from the left edge of the
  //    containing block to the left margin edge of a hypothetical box that
  //    would have been the first box of the element if its 'position' property
  //    had been 'static' and 'float' had been 'none'. The value is negative if
  //    the hypothetical box is to the left of the containing block.
  //  * The static position for 'right' is the distance from the right edge of
  //    the containing block to the right margin edge of the same hypothetical
  //    box as above. The value is positive if the hypothetical box is to the
  //    left of the containing block's edge.
  //
  //  But rather than actually calculating the dimensions of that hypothetical
  //  box, user agents are free to make a guess at its probable position.
  //
  //  For the purposes of calculating the static position, the containing block
  //  of fixed positioned elements is the initial containing block instead of
  //  the viewport, and all scrollable boxes should be assumed to be scrolled to
  //  their origin.
  // ---------------------------------------------------------------------------
  // see FIXME 1
  // Calculate the static distance if needed.
  ComputeInlineStaticDistance(logical_left_length, logical_right_length, this,
                              container_block, container_logical_width);

  // Calculate constraint equation values for 'width' case.
  ComputePositionedLogicalWidthUsing(
      kMainOrPreferredSize, StyleRef().LogicalWidth(), container_block,
      container_direction, container_logical_width, borders_plus_padding,
      logical_left_length, logical_right_length, margin_logical_left,
      margin_logical_right, computed_values);

  MinMaxSizes transferred_min_max{LayoutUnit(), LayoutUnit::Max()};
  if (ShouldComputeLogicalHeightFromAspectRatio())
    transferred_min_max = ComputeMinMaxLogicalWidthFromAspectRatio();

  // Calculate constraint equation values for 'max-width' case.
  LogicalExtentComputedValues max_values;
  max_values.extent_ = LayoutUnit::Max();
  if (!StyleRef().LogicalMaxWidth().IsNone()) {
    ComputePositionedLogicalWidthUsing(
        kMaxSize, StyleRef().LogicalMaxWidth(), container_block,
        container_direction, container_logical_width, borders_plus_padding,
        logical_left_length, logical_right_length, margin_logical_left,
        margin_logical_right, max_values);
  }
  if (transferred_min_max.max_size < max_values.extent_) {
    ComputePositionedLogicalWidthUsing(
        kMaxSize, Length::Fixed(transferred_min_max.max_size), container_block,
        container_direction, container_logical_width, borders_plus_padding,
        logical_left_length, logical_right_length, margin_logical_left,
        margin_logical_right, max_values);
  }

  if (computed_values.extent_ > max_values.extent_)
    max_values.CopyExceptBlockMargins(&computed_values);

  LogicalExtentComputedValues min_values;
  // Calculate constraint equation values for 'min-width' case.
  if (!StyleRef().LogicalMinWidth().IsZero() ||
      StyleRef().LogicalMinWidth().IsContentOrIntrinsicOrFillAvailable()) {
    ComputePositionedLogicalWidthUsing(
        kMinSize, StyleRef().LogicalMinWidth(), container_block,
        container_direction, container_logical_width, borders_plus_padding,
        logical_left_length, logical_right_length, margin_logical_left,
        margin_logical_right, min_values);
  }
  if (transferred_min_max.min_size > min_values.extent_) {
    ComputePositionedLogicalWidthUsing(
        kMinSize, Length::Fixed(transferred_min_max.min_size), container_block,
        container_direction, container_logical_width, borders_plus_padding,
        logical_left_length, logical_right_length, margin_logical_left,
        margin_logical_right, min_values);
  }
  if (computed_values.extent_ < min_values.extent_)
    min_values.CopyExceptBlockMargins(&computed_values);

  computed_values.extent_ += borders_plus_padding;
}

void LayoutBox::ComputeLogicalLeftPositionedOffset(
    LayoutUnit& logical_left_pos,
    const LayoutBox* child,
    LayoutUnit logical_width_value,
    const LayoutBoxModelObject* container_block,
    LayoutUnit container_logical_width) {
  if (child->IsHorizontalWritingMode()) {
    if (container_block->HasFlippedBlocksWritingMode()) {
      // Deal with differing writing modes here. Our offset needs to be in the
      // containing block's coordinate space. If the containing block is flipped
      // along this axis, then we need to flip the coordinate. This can only
      // happen if the containing block has flipped mode and is perpendicular
      // to us.
      logical_left_pos =
          container_logical_width - logical_width_value - logical_left_pos;
      logical_left_pos += container_block->BorderRight();
      if (container_block->IsBox() &&
          !To<LayoutBox>(container_block)->CanSkipComputeScrollbars()) {
        logical_left_pos += To<LayoutBox>(container_block)
                                ->ComputeScrollbarsInternal(kClampToContentBox)
                                .right;
      }
    } else {
      logical_left_pos += container_block->BorderLeft();
      if (container_block->IsBox() &&
          !To<LayoutBox>(container_block)->CanSkipComputeScrollbars()) {
        logical_left_pos += To<LayoutBox>(container_block)
                                ->ComputeScrollbarsInternal(kClampToContentBox)
                                .left;
      }
    }
  } else {
    logical_left_pos += container_block->BorderTop();
    if (container_block->IsBox() &&
        !To<LayoutBox>(container_block)->CanSkipComputeScrollbars()) {
      logical_left_pos += To<LayoutBox>(container_block)
                              ->ComputeScrollbarsInternal(kClampToContentBox)
                              .top;
    }
  }
}

LayoutUnit LayoutBox::ShrinkToFitLogicalWidth(
    LayoutUnit available_logical_width,
    LayoutUnit borders_plus_padding) const {
  NOT_DESTROYED();
  MinMaxSizes sizes = PreferredLogicalWidths();
  sizes -= borders_plus_padding;
  return sizes.ShrinkToFit(available_logical_width);
}

void LayoutBox::ComputePositionedLogicalWidthUsing(
    SizeType width_size_type,
    const Length& logical_width,
    const LayoutBoxModelObject* container_block,
    TextDirection container_direction,
    LayoutUnit container_logical_width,
    LayoutUnit borders_plus_padding,
    const Length& logical_left,
    const Length& logical_right,
    const Length& margin_logical_left,
    const Length& margin_logical_right,
    LogicalExtentComputedValues& computed_values) const {
  NOT_DESTROYED();
  LayoutUnit logical_width_value;

  DCHECK(width_size_type == kMinSize ||
         width_size_type == kMainOrPreferredSize || !logical_width.IsAuto());
  if (width_size_type == kMinSize && logical_width.IsAuto()) {
    if (ShouldComputeLogicalWidthFromAspectRatio()) {
      logical_width_value =
          IntrinsicLogicalWidths(MinMaxSizesType::kIntrinsic).min_size;
    } else {
      logical_width_value = LayoutUnit();
    }
  } else if (width_size_type == kMainOrPreferredSize &&
             logical_width.IsAuto() &&
             ComputeLogicalWidthFromAspectRatio(&logical_width_value)) {
    // We're good.
  } else if (logical_width.IsContentOrIntrinsicOrFillAvailable()) {
    logical_width_value = ComputeIntrinsicLogicalWidthUsing(
                              logical_width, container_logical_width) -
                          borders_plus_padding;
  } else {
    logical_width_value = AdjustContentBoxLogicalWidthForBoxSizing(
        ValueForLength(logical_width, container_logical_width));
  }

  // 'left' and 'right' cannot both be 'auto' because one would of been
  // converted to the static position already
  DCHECK(!(logical_left.IsAuto() && logical_right.IsAuto()));

  // minimumValueForLength will convert 'auto' to 0 so that it doesn't impact
  // the available space computation below.
  LayoutUnit logical_left_value =
      MinimumValueForLength(logical_left, container_logical_width);
  LayoutUnit logical_right_value =
      MinimumValueForLength(logical_right, container_logical_width);

  const LayoutUnit container_relative_logical_width =
      ContainingBlockLogicalWidthForPositioned(container_block, false);

  // If we are using aspect-ratio, the width is effectively not auto.
  bool logical_width_is_auto =
      logical_width.IsAuto() && !ShouldComputeLogicalWidthFromAspectRatio();
  bool logical_left_is_auto = logical_left.IsAuto();
  bool logical_right_is_auto = logical_right.IsAuto();
  LayoutUnit& margin_logical_left_value = StyleRef().IsLeftToRightDirection()
                                              ? computed_values.margins_.start_
                                              : computed_values.margins_.end_;
  LayoutUnit& margin_logical_right_value =
      StyleRef().IsLeftToRightDirection() ? computed_values.margins_.end_
                                          : computed_values.margins_.start_;
  if (!logical_left_is_auto && !logical_width_is_auto &&
      !logical_right_is_auto) {
    // -------------------------------------------------------------------------
    // If none of the three is 'auto': If both 'margin-left' and 'margin-
    // right' are 'auto', solve the equation under the extra constraint that
    // the two margins get equal values, unless this would make them negative,
    // in which case when direction of the containing block is 'ltr' ('rtl'),
    // set 'margin-left' ('margin-right') to zero and solve for 'margin-right'
    // ('margin-left'). If one of 'margin-left' or 'margin-right' is 'auto',
    // solve the equation for that value. If the values are over-constrained,
    // ignore the value for 'left' (in case the 'direction' property of the
    // containing block is 'rtl') or 'right' (in case 'direction' is 'ltr')
    // and solve for that value.
    // -------------------------------------------------------------------------
    // NOTE:  It is not necessary to solve for 'right' in the over constrained
    // case because the value is not used for any further calculations.

    computed_values.extent_ = logical_width_value;

    const LayoutUnit available_space =
        container_logical_width -
        (logical_left_value + computed_values.extent_ + logical_right_value +
         borders_plus_padding);

    // Margins are now the only unknown
    if (margin_logical_left.IsAuto() && margin_logical_right.IsAuto()) {
      // Both margins auto, solve for equality
      if (available_space >= 0) {
        margin_logical_left_value =
            available_space / 2;  // split the difference
        margin_logical_right_value =
            available_space -
            margin_logical_left_value;  // account for odd valued differences
      } else {
        // Use the containing block's direction rather than the parent block's
        // per CSS 2.1 reference test abspos-non-replaced-width-margin-000.
        if (container_direction == TextDirection::kLtr) {
          margin_logical_left_value = LayoutUnit();
          margin_logical_right_value = available_space;  // will be negative
        } else {
          margin_logical_left_value = available_space;  // will be negative
          margin_logical_right_value = LayoutUnit();
        }
      }
    } else if (margin_logical_left.IsAuto()) {
      // Solve for left margin
      margin_logical_right_value = ValueForLength(
          margin_logical_right, container_relative_logical_width);
      margin_logical_left_value = available_space - margin_logical_right_value;
    } else if (margin_logical_right.IsAuto()) {
      // Solve for right margin
      margin_logical_left_value =
          ValueForLength(margin_logical_left, container_relative_logical_width);
      margin_logical_right_value = available_space - margin_logical_left_value;
    } else {
      // Over-constrained, solve for left if direction is RTL
      margin_logical_left_value =
          ValueForLength(margin_logical_left, container_relative_logical_width);
      margin_logical_right_value = ValueForLength(
          margin_logical_right, container_relative_logical_width);

      // Use the containing block's direction rather than the parent block's
      // per CSS 2.1 reference test abspos-non-replaced-width-margin-000.
      if (container_direction == TextDirection::kRtl)
        logical_left_value = (available_space + logical_left_value) -
                             margin_logical_left_value -
                             margin_logical_right_value;
    }
  } else {
    // -------------------------------------------------------------------------
    // Otherwise, set 'auto' values for 'margin-left' and 'margin-right'
    // to 0, and pick the one of the following six rules that applies.
    //
    // 1. 'left' and 'width' are 'auto' and 'right' is not 'auto', then the
    //    width is shrink-to-fit. Then solve for 'left'
    //
    //              OMIT RULE 2 AS IT SHOULD NEVER BE HIT
    // ------------------------------------------------------------------
    // 2. 'left' and 'right' are 'auto' and 'width' is not 'auto', then if
    //    the 'direction' property of the containing block is 'ltr' set
    //    'left' to the static position, otherwise set 'right' to the
    //    static position. Then solve for 'left' (if 'direction is 'rtl')
    //    or 'right' (if 'direction' is 'ltr').
    // ------------------------------------------------------------------
    //
    // 3. 'width' and 'right' are 'auto' and 'left' is not 'auto', then the
    //    width is shrink-to-fit . Then solve for 'right'
    // 4. 'left' is 'auto', 'width' and 'right' are not 'auto', then solve
    //    for 'left'
    // 5. 'width' is 'auto', 'left' and 'right' are not 'auto', then solve
    //    for 'width'
    // 6. 'right' is 'auto', 'left' and 'width' are not 'auto', then solve
    //    for 'right'
    //
    // Calculation of the shrink-to-fit width is similar to calculating the
    // width of a table cell using the automatic table layout algorithm.
    // Roughly: calculate the preferred width by formatting the content without
    // breaking lines other than where explicit line breaks occur, and also
    // calculate the preferred minimum width, e.g., by trying all possible line
    // breaks. CSS 2.1 does not define the exact algorithm.
    // Thirdly, calculate the available width: this is found by solving for
    // 'width' after setting 'left' (in case 1) or 'right' (in case 3) to 0.
    //
    // Then the shrink-to-fit width is:
    // min(max(preferred minimum width, available width), preferred width).
    // -------------------------------------------------------------------------
    // NOTE: For rules 3 and 6 it is not necessary to solve for 'right'
    // because the value is not used for any further calculations.

    // Calculate margins, 'auto' margins are ignored.
    margin_logical_left_value = MinimumValueForLength(
        margin_logical_left, container_relative_logical_width);
    margin_logical_right_value = MinimumValueForLength(
        margin_logical_right, container_relative_logical_width);

    const LayoutUnit available_space =
        container_logical_width -
        (margin_logical_left_value + margin_logical_right_value +
         logical_left_value + logical_right_value + borders_plus_padding);

    // FIXME: Is there a faster way to find the correct case?
    // Use rule/case that applies.
    if (logical_left_is_auto && logical_width_is_auto &&
        !logical_right_is_auto) {
      // RULE 1: (use shrink-to-fit for width, and solve of left)
      computed_values.extent_ =
          ShrinkToFitLogicalWidth(available_space, borders_plus_padding);
      logical_left_value = available_space - computed_values.extent_;
    } else if (!logical_left_is_auto && logical_width_is_auto &&
               logical_right_is_auto) {
      // RULE 3: (use shrink-to-fit for width, and no need solve of right)
      computed_values.extent_ =
          ShrinkToFitLogicalWidth(available_space, borders_plus_padding);
    } else if (logical_left_is_auto && !logical_width_is_auto &&
               !logical_right_is_auto) {
      // RULE 4: (solve for left)
      computed_values.extent_ = logical_width_value;
      logical_left_value = available_space - computed_values.extent_;
    } else if (!logical_left_is_auto && logical_width_is_auto &&
               !logical_right_is_auto) {
      // RULE 5: (solve for width)
      if (AutoWidthShouldFitContent())
        computed_values.extent_ =
            ShrinkToFitLogicalWidth(available_space, borders_plus_padding);
      else
        computed_values.extent_ = std::max(LayoutUnit(), available_space);
    } else if (!logical_left_is_auto && !logical_width_is_auto &&
               logical_right_is_auto) {
      // RULE 6: (no need solve for right)
      computed_values.extent_ = logical_width_value;
    }
  }

  // Use computed values to calculate the horizontal position.

  // FIXME: This hack is needed to calculate the  logical left position for a
  // 'rtl' relatively positioned, inline because right now, it is using the
  // logical left position of the first line box when really it should use the
  // last line box. When this is fixed elsewhere, this block should be removed.
  if (container_block->IsLayoutInline() &&
      !container_block->StyleRef().IsLeftToRightDirection()) {
    const auto* flow = To<LayoutInline>(container_block);
    InlineFlowBox* first_line = flow->FirstLineBox();
    InlineFlowBox* last_line = flow->LastLineBox();
    if (first_line && last_line && first_line != last_line) {
      computed_values.position_ =
          logical_left_value + margin_logical_left_value +
          last_line->BorderLogicalLeft() +
          (last_line->LogicalLeft() - first_line->LogicalLeft());
      return;
    }
  }

  computed_values.position_ = logical_left_value + margin_logical_left_value;
  ComputeLogicalLeftPositionedOffset(computed_values.position_, this,
                                     computed_values.extent_, container_block,
                                     container_logical_width);
}

void LayoutBox::ComputeBlockStaticDistance(
    Length& logical_top,
    Length& logical_bottom,
    const LayoutBox* child,
    const LayoutBoxModelObject* container_block,
    const NGBoxFragmentBuilder* fragment_builder) {
  if (!logical_top.IsAuto() || !logical_bottom.IsAuto())
    return;

  // FIXME: The static distance computation has not been patched for mixed
  // writing modes.
  LayoutUnit static_logical_top = child->Layer()->StaticBlockPosition();
  for (LayoutObject* curr = child->Parent(); curr && curr != container_block;
       curr = curr->Container()) {
    if (!curr->IsBox() || curr->IsLegacyTableRow())
      continue;
    const auto& box = *To<LayoutBox>(curr);
    static_logical_top +=
        (fragment_builder &&
         fragment_builder->GetLayoutObject() == box.Parent())
            ? fragment_builder->GetChildOffset(&box).block_offset
            : box.LogicalTop();
    if (box.IsInFlowPositioned())
      static_logical_top += box.OffsetForInFlowPosition().top;
    if (!box.IsLayoutFlowThread())
      continue;
    // We're walking out of a flowthread here. This flow thread is not in the
    // containing block chain, so we need to convert the position from the
    // coordinate space of this flowthread to the containing coordinate space.
    // The inline position cannot affect the block position, so we don't bother
    // calculating it.
    LayoutUnit dummy_inline_position;
    To<LayoutFlowThread>(box).FlowThreadToContainingCoordinateSpace(
        static_logical_top, dummy_inline_position);
  }

  // Now static_logical_top is relative to container_block's logical top.
  // Convert it to be relative to containing_block's logical client top.
  static_logical_top -= container_block->BorderBefore();
  if (auto* box = DynamicTo<LayoutBox>(container_block))
    static_logical_top -= box->LogicalTopScrollbarHeight();
  logical_top = Length::Fixed(static_logical_top);
}

void LayoutBox::ComputePositionedLogicalHeight(
    LogicalExtentComputedValues& computed_values) const {
  NOT_DESTROYED();
  // The following is based off of the W3C Working Draft from April 11, 2006 of
  // CSS 2.1: Section 10.6.4 "Absolutely positioned, non-replaced elements"
  // <http://www.w3.org/TR/2005/WD-CSS21-20050613/visudet.html#abs-non-replaced-height>
  // (block-style-comments in this function and in
  // computePositionedLogicalHeightUsing()
  // correspond to text from the spec)

  // We don't use containingBlock(), since we may be positioned by an enclosing
  // relpositioned inline.
  const auto* container_block = To<LayoutBoxModelObject>(Container());

  const LayoutUnit container_logical_height =
      ContainingBlockLogicalHeightForPositioned(container_block);

  const ComputedStyle& style_to_use = StyleRef();
  const LayoutUnit borders_plus_padding = BorderAndPaddingLogicalHeight();
  const Length& margin_before = style_to_use.MarginBefore();
  const Length& margin_after = style_to_use.MarginAfter();
  Length logical_top_length = style_to_use.LogicalTop();
  Length logical_bottom_length = style_to_use.LogicalBottom();

  // ---------------------------------------------------------------------------
  // For the purposes of this section and the next, the term "static position"
  // (of an element) refers, roughly, to the position an element would have had
  // in the normal flow. More precisely, the static position for 'top' is the
  // distance from the top edge of the containing block to the top margin edge
  // of a hypothetical box that would have been the first box of the element if
  // its 'position' property had been 'static' and 'float' had been 'none'. The
  // value is negative if the hypothetical box is above the containing block.
  //
  // But rather than actually calculating the dimensions of that hypothetical
  // box, user agents are free to make a guess at its probable position.
  //
  // For the purposes of calculating the static position, the containing block
  // of fixed positioned elements is the initial containing block instead of
  // the viewport.
  // ---------------------------------------------------------------------------
  // see FIXME 1
  // Calculate the static distance if needed.
  ComputeBlockStaticDistance(logical_top_length, logical_bottom_length, this,
                             container_block);

  // Calculate constraint equation values for 'height' case.
  LayoutUnit logical_height = computed_values.extent_;
  ComputePositionedLogicalHeightUsing(
      kMainOrPreferredSize, style_to_use.LogicalHeight(), container_block,
      container_logical_height, borders_plus_padding, logical_height,
      logical_top_length, logical_bottom_length, margin_before, margin_after,
      computed_values);

  // Avoid doing any work in the common case (where the values of min-height and
  // max-height are their defaults).
  // see FIXME 2

  // Calculate constraint equation values for 'max-height' case.
  const Length& logical_max_height = style_to_use.LogicalMaxHeight();
  if (!logical_max_height.IsNone() && !logical_max_height.IsMinContent() &&
      !logical_max_height.IsMaxContent() &&
      !logical_max_height.IsMinIntrinsic() &&
      !logical_max_height.IsFitContent()) {
    LogicalExtentComputedValues max_values;

    ComputePositionedLogicalHeightUsing(
        kMaxSize, logical_max_height, container_block, container_logical_height,
        borders_plus_padding, logical_height, logical_top_length,
        logical_bottom_length, margin_before, margin_after, max_values);

    if (computed_values.extent_ > max_values.extent_) {
      computed_values.extent_ = max_values.extent_;
      computed_values.position_ = max_values.position_;
      computed_values.margins_.before_ = max_values.margins_.before_;
      computed_values.margins_.after_ = max_values.margins_.after_;
    }
  }

  // Calculate constraint equation values for 'min-height' case.
  Length logical_min_height = style_to_use.LogicalMinHeight();
  if (logical_min_height.IsMinContent() || logical_min_height.IsMaxContent() ||
      logical_min_height.IsMinIntrinsic() || logical_min_height.IsFitContent())
    logical_min_height = Length::Auto();
  // auto is considered to be zero, so we need to check for it explicitly.
  if (logical_min_height.IsAuto() || !logical_min_height.IsZero() ||
      logical_min_height.IsFillAvailable()) {
    LogicalExtentComputedValues min_values;

    ComputePositionedLogicalHeightUsing(
        kMinSize, logical_min_height, container_block, container_logical_height,
        borders_plus_padding, logical_height, logical_top_length,
        logical_bottom_length, margin_before, margin_after, min_values);

    if (computed_values.extent_ < min_values.extent_) {
      computed_values.extent_ = min_values.extent_;
      computed_values.position_ = min_values.position_;
      computed_values.margins_.before_ = min_values.margins_.before_;
      computed_values.margins_.after_ = min_values.margins_.after_;
    }
  }

  // Set final height value.
  computed_values.extent_ += borders_plus_padding;
}

void LayoutBox::ComputeLogicalTopPositionedOffset(
    LayoutUnit& logical_top_pos,
    const LayoutBox* child,
    LayoutUnit logical_height_value,
    const LayoutBoxModelObject* container_block,
    LayoutUnit container_logical_height) {
  // Deal with differing writing modes here. Our offset needs to be in the
  // containing block's coordinate space. If the containing block is flipped
  // along this axis, then we need to flip the coordinate.  This can only happen
  // if the containing block is both a flipped mode and perpendicular to us.
  if ((child->StyleRef().IsFlippedBlocksWritingMode() &&
       child->IsHorizontalWritingMode() !=
           container_block->IsHorizontalWritingMode()) ||
      (child->StyleRef().IsFlippedBlocksWritingMode() !=
           container_block->StyleRef().IsFlippedBlocksWritingMode() &&
       child->IsHorizontalWritingMode() ==
           container_block->IsHorizontalWritingMode())) {
    logical_top_pos =
        container_logical_height - logical_height_value - logical_top_pos;
  }

  // Convert logical_top_pos from container's client space to container's border
  // box space.
  if (child->IsHorizontalWritingMode()) {
    logical_top_pos += container_block->BorderTop();
    if (container_block->IsBox() &&
        !To<LayoutBox>(container_block)->CanSkipComputeScrollbars()) {
      logical_top_pos += To<LayoutBox>(container_block)
                             ->ComputeScrollbarsInternal(kClampToContentBox)
                             .top;
    }
  } else if (container_block->HasFlippedBlocksWritingMode()) {
    logical_top_pos += container_block->BorderRight();
    if (container_block->IsBox() &&
        !To<LayoutBox>(container_block)->CanSkipComputeScrollbars()) {
      logical_top_pos += To<LayoutBox>(container_block)
                             ->ComputeScrollbarsInternal(kClampToContentBox)
                             .right;
    }
  } else {
    logical_top_pos += container_block->BorderLeft();
    if (container_block->IsBox() &&
        !To<LayoutBox>(container_block)->CanSkipComputeScrollbars()) {
      logical_top_pos += To<LayoutBox>(container_block)
                             ->ComputeScrollbarsInternal(kClampToContentBox)
                             .left;
    }
  }
}

void LayoutBox::ComputePositionedLogicalHeightUsing(
    SizeType height_size_type,
    Length logical_height_length,
    const LayoutBoxModelObject* container_block,
    LayoutUnit container_logical_height,
    LayoutUnit borders_plus_padding,
    LayoutUnit logical_height,
    const Length& logical_top,
    const Length& logical_bottom,
    const Length& margin_before,
    const Length& margin_after,
    LogicalExtentComputedValues& computed_values) const {
  NOT_DESTROYED();
  DCHECK(height_size_type == kMinSize ||
         height_size_type == kMainOrPreferredSize ||
         !logical_height_length.IsAuto());
  if (height_size_type == kMinSize && logical_height_length.IsAuto()) {
    if (ShouldComputeLogicalHeightFromAspectRatio())
      logical_height_length = Length::Fixed(logical_height);
    else
      logical_height_length = Length::Fixed(0);
  }

  // 'top' and 'bottom' cannot both be 'auto' because 'top would of been
  // converted to the static position in computePositionedLogicalHeight()
  DCHECK(!(logical_top.IsAuto() && logical_bottom.IsAuto()));

  LayoutUnit logical_height_value;
  LayoutUnit content_logical_height = logical_height - borders_plus_padding;

  const LayoutUnit container_relative_logical_width =
      ContainingBlockLogicalWidthForPositioned(container_block, false);

  LayoutUnit logical_top_value;

  bool from_aspect_ratio = height_size_type == kMainOrPreferredSize &&
                           ShouldComputeLogicalHeightFromAspectRatio();
  bool logical_height_is_auto =
      logical_height_length.IsAuto() && !from_aspect_ratio;
  bool logical_top_is_auto = logical_top.IsAuto();
  bool logical_bottom_is_auto = logical_bottom.IsAuto();

  LayoutUnit resolved_logical_height;
  // Height is never unsolved for tables.
  if (IsTable()) {
    resolved_logical_height = content_logical_height;
    logical_height_is_auto = false;
  } else {
    if (logical_height_length.IsContentOrIntrinsicOrFillAvailable()) {
      resolved_logical_height = ComputeIntrinsicLogicalContentHeightUsing(
          height_size_type, logical_height_length, content_logical_height,
          borders_plus_padding);
    } else if (from_aspect_ratio) {
      NGBoxStrut border_padding(BorderStart() + ComputedCSSPaddingStart(),
                                BorderEnd() + ComputedCSSPaddingEnd(),
                                BorderBefore() + ComputedCSSPaddingBefore(),
                                BorderAfter() + ComputedCSSPaddingAfter());
      resolved_logical_height = BlockSizeFromAspectRatio(
          border_padding, StyleRef().LogicalAspectRatio(),
          StyleRef().BoxSizingForAspectRatio(), LogicalWidth());
      resolved_logical_height = std::max(
          LayoutUnit(), resolved_logical_height - borders_plus_padding);
    } else {
      resolved_logical_height = AdjustContentBoxLogicalHeightForBoxSizing(
          ValueForLength(logical_height_length, container_logical_height));
    }
  }

  if (!logical_top_is_auto && !logical_height_is_auto &&
      !logical_bottom_is_auto) {
    // -------------------------------------------------------------------------
    // If none of the three are 'auto': If both 'margin-top' and 'margin-bottom'
    // are 'auto', solve the equation under the extra constraint that the two
    // margins get equal values. If one of 'margin-top' or 'margin- bottom' is
    // 'auto', solve the equation for that value. If the values are over-
    // constrained, ignore the value for 'bottom' and solve for that value.
    // -------------------------------------------------------------------------
    // NOTE:  It is not necessary to solve for 'bottom' in the over constrained
    // case because the value is not used for any further calculations.

    logical_height_value = resolved_logical_height;
    logical_top_value = ValueForLength(logical_top, container_logical_height);

    const LayoutUnit available_space =
        container_logical_height -
        (logical_top_value + logical_height_value +
         ValueForLength(logical_bottom, container_logical_height) +
         borders_plus_padding);

    // Margins are now the only unknown
    if (margin_before.IsAuto() && margin_after.IsAuto()) {
      // Both margins auto, solve for equality
      // NOTE: This may result in negative values.
      computed_values.margins_.before_ =
          available_space / 2;  // split the difference
      computed_values.margins_.after_ =
          available_space - computed_values.margins_
                                .before_;  // account for odd valued differences
    } else if (margin_before.IsAuto()) {
      // Solve for top margin
      computed_values.margins_.after_ =
          ValueForLength(margin_after, container_relative_logical_width);
      computed_values.margins_.before_ =
          available_space - computed_values.margins_.after_;
    } else if (margin_after.IsAuto()) {
      // Solve for bottom margin
      computed_values.margins_.before_ =
          ValueForLength(margin_before, container_relative_logical_width);
      computed_values.margins_.after_ =
          available_space - computed_values.margins_.before_;
    } else {
      // Over-constrained, (no need solve for bottom)
      computed_values.margins_.before_ =
          ValueForLength(margin_before, container_relative_logical_width);
      computed_values.margins_.after_ =
          ValueForLength(margin_after, container_relative_logical_width);
    }
  } else {
    // -------------------------------------------------------------------------
    // Otherwise, set 'auto' values for 'margin-top' and 'margin-bottom'
    // to 0, and pick the one of the following six rules that applies.
    //
    // 1. 'top' and 'height' are 'auto' and 'bottom' is not 'auto', then
    //    the height is based on the content, and solve for 'top'.
    //
    //              OMIT RULE 2 AS IT SHOULD NEVER BE HIT
    // ------------------------------------------------------------------
    // 2. 'top' and 'bottom' are 'auto' and 'height' is not 'auto', then
    //    set 'top' to the static position, and solve for 'bottom'.
    // ------------------------------------------------------------------
    //
    // 3. 'height' and 'bottom' are 'auto' and 'top' is not 'auto', then
    //    the height is based on the content, and solve for 'bottom'.
    // 4. 'top' is 'auto', 'height' and 'bottom' are not 'auto', and
    //    solve for 'top'.
    // 5. 'height' is 'auto', 'top' and 'bottom' are not 'auto', and
    //    solve for 'height'.
    // 6. 'bottom' is 'auto', 'top' and 'height' are not 'auto', and
    //    solve for 'bottom'.
    // -------------------------------------------------------------------------
    // NOTE: For rules 3 and 6 it is not necessary to solve for 'bottom'
    // because the value is not used for any further calculations.

    // Calculate margins, 'auto' margins are ignored.
    computed_values.margins_.before_ =
        MinimumValueForLength(margin_before, container_relative_logical_width);
    computed_values.margins_.after_ =
        MinimumValueForLength(margin_after, container_relative_logical_width);

    const LayoutUnit available_space =
        container_logical_height -
        (computed_values.margins_.before_ + computed_values.margins_.after_ +
         borders_plus_padding);

    // Use rule/case that applies.
    if (logical_top_is_auto && logical_height_is_auto &&
        !logical_bottom_is_auto) {
      // RULE 1: (height is content based, solve of top)
      logical_height_value = content_logical_height;
      logical_top_value =
          available_space -
          (logical_height_value +
           ValueForLength(logical_bottom, container_logical_height));
    } else if (!logical_top_is_auto && logical_height_is_auto &&
               logical_bottom_is_auto) {
      // RULE 3: (height is content based, no need solve of bottom)
      logical_top_value = ValueForLength(logical_top, container_logical_height);
      logical_height_value = content_logical_height;
    } else if (logical_top_is_auto && !logical_height_is_auto &&
               !logical_bottom_is_auto) {
      // RULE 4: (solve of top)
      logical_height_value = resolved_logical_height;
      logical_top_value =
          available_space -
          (logical_height_value +
           ValueForLength(logical_bottom, container_logical_height));
    } else if (!logical_top_is_auto && logical_height_is_auto &&
               !logical_bottom_is_auto) {
      // RULE 5: (solve of height)
      logical_top_value = ValueForLength(logical_top, container_logical_height);
      logical_height_value = std::max(
          LayoutUnit(),
          available_space -
              (logical_top_value +
               ValueForLength(logical_bottom, container_logical_height)));
    } else if (!logical_top_is_auto && !logical_height_is_auto &&
               logical_bottom_is_auto) {
      // RULE 6: (no need solve of bottom)
      logical_height_value = resolved_logical_height;
      logical_top_value = ValueForLength(logical_top, container_logical_height);
    }
  }
  computed_values.extent_ = logical_height_value;

  // Use computed values to calculate the vertical position.
  computed_values.position_ =
      logical_top_value + computed_values.margins_.before_;
  ComputeLogicalTopPositionedOffset(computed_values.position_, this,
                                    logical_height_value, container_block,
                                    container_logical_height);
}

LayoutRect LayoutBox::LocalCaretRect(
    const InlineBox* box,
    int caret_offset,
    LayoutUnit* extra_width_to_end_of_line) const {
  NOT_DESTROYED();
  // VisiblePositions at offsets inside containers either a) refer to the
  // positions before/after those containers (tables and select elements) or
  // b) refer to the position inside an empty block.
  // They never refer to children.
  // FIXME: Paint the carets inside empty blocks differently than the carets
  // before/after elements.
  LayoutUnit caret_width = GetFrameView()->CaretWidth();
  LayoutRect rect(Location(), LayoutSize(caret_width, Size().Height()));
  bool ltr =
      box ? box->IsLeftToRightDirection() : StyleRef().IsLeftToRightDirection();

  if ((!caret_offset) ^ ltr)
    rect.Move(LayoutSize(Size().Width() - caret_width, LayoutUnit()));

  if (box) {
    const RootInlineBox& root_box = box->Root();
    LayoutUnit top = root_box.LineTop();
    rect.SetY(top);
    rect.SetHeight(root_box.LineBottom() - top);
  }

  // If height of box is smaller than font height, use the latter one,
  // otherwise the caret might become invisible.
  //
  // Also, if the box is not an atomic inline-level element, always use the font
  // height. This prevents the "big caret" bug described in:
  // <rdar://problem/3777804> Deleting all content in a document can result in
  // giant tall-as-window insertion point
  //
  // FIXME: ignoring :first-line, missing good reason to take care of
  const SimpleFontData* font_data = StyleRef().GetFont().PrimaryFont();
  LayoutUnit font_height =
      LayoutUnit(font_data ? font_data->GetFontMetrics().Height() : 0);
  if (font_height > rect.Height() || (!IsAtomicInlineLevel() && !IsTable()))
    rect.SetHeight(font_height);

  if (extra_width_to_end_of_line)
    *extra_width_to_end_of_line = Location().X() + Size().Width() - rect.MaxX();

  // Move to local coords
  rect.MoveBy(-Location());

  // FIXME: Border/padding should be added for all elements but this workaround
  // is needed because we use offsets inside an "atomic" element to represent
  // positions before and after the element in deprecated editing offsets.
  if (GetNode() &&
      !(EditingIgnoresContent(*GetNode()) || IsDisplayInsideTable(GetNode()))) {
    rect.SetX(rect.X() + BorderLeft() + PaddingLeft());
    rect.SetY(rect.Y() + PaddingTop() + BorderTop());
  }

  if (!IsHorizontalWritingMode())
    return rect.TransposedRect();

  return rect;
}

PositionWithAffinity LayoutBox::PositionForPoint(
    const PhysicalOffset& point) const {
  NOT_DESTROYED();
  // NG codepath requires |kPrePaintClean|.
  // |SelectionModifier| calls this only in legacy codepath.
  DCHECK(!IsLayoutNGObject() || GetDocument().Lifecycle().GetState() >=
                                    DocumentLifecycle::kPrePaintClean);

  // no children...return this layout object's element, if there is one, and
  // offset 0
  LayoutObject* first_child = SlowFirstChild();
  if (!first_child)
    return FirstPositionInOrBeforeThis();

  if (IsTable() && NonPseudoNode()) {
    LayoutUnit x_in_block_direction = FlipForWritingMode(point.left);
    if (x_in_block_direction < 0 || x_in_block_direction > Size().Width() ||
        point.top < 0 || point.top > Size().Height()) {
      if (x_in_block_direction <= Size().Width() / 2) {
        return FirstPositionInOrBeforeThis();
      }
      return LastPositionInOrAfterThis();
    }
  }

  // Pass off to the closest child.
  LayoutUnit min_dist = LayoutUnit::Max();
  LayoutBox* closest_layout_object = nullptr;

  for (LayoutObject* layout_object = first_child; layout_object;
       layout_object = layout_object->NextSibling()) {
    if ((!layout_object->SlowFirstChild() && !layout_object->IsInline() &&
         !layout_object->IsLayoutBlockFlow()) ||
        layout_object->StyleRef().Visibility() != EVisibility::kVisible)
      continue;

    if (!layout_object->IsBox())
      continue;

    auto* layout_box = To<LayoutBox>(layout_object);

    LayoutUnit top = layout_box->BorderTop() + layout_box->PaddingTop() +
                     layout_box->Location().Y();
    LayoutUnit bottom = top + layout_box->ContentHeight();
    LayoutUnit left = layout_box->BorderLeft() + layout_box->PaddingLeft() +
                      layout_box->PhysicalLocation().left;
    LayoutUnit right = left + layout_box->ContentWidth();

    if (point.left <= right && point.left >= left && point.top <= top &&
        point.top >= bottom) {
      return layout_box->PositionForPoint(point -
                                          layout_box->PhysicalLocation());
    }

    // Find the distance from (x, y) to the box.  Split the space around the box
    // into 8 pieces and use a different compare depending on which piece (x, y)
    // is in.
    PhysicalOffset cmp;
    if (point.left > right) {
      if (point.top < top)
        cmp = PhysicalOffset(right, top);
      else if (point.top > bottom)
        cmp = PhysicalOffset(right, bottom);
      else
        cmp = PhysicalOffset(right, point.top);
    } else if (point.left < left) {
      if (point.top < top)
        cmp = PhysicalOffset(left, top);
      else if (point.top > bottom)
        cmp = PhysicalOffset(left, bottom);
      else
        cmp = PhysicalOffset(left, point.top);
    } else {
      if (point.top < top)
        cmp = PhysicalOffset(point.left, top);
      else
        cmp = PhysicalOffset(point.left, bottom);
    }

    PhysicalOffset difference = cmp - point;

    LayoutUnit dist =
        difference.left * difference.left + difference.top * difference.top;
    if (dist < min_dist) {
      closest_layout_object = layout_box;
      min_dist = dist;
    }
  }

  if (closest_layout_object) {
    return closest_layout_object->PositionForPoint(
        point - closest_layout_object->PhysicalLocation());
  }
  return FirstPositionInOrBeforeThis();
}

PositionWithAffinity LayoutBox::PositionForPointInFragments(
    const PhysicalOffset& target) const {
  NOT_DESTROYED();
  DCHECK_GE(GetDocument().Lifecycle().GetState(),
            DocumentLifecycle::kPrePaintClean);
  DCHECK_GT(PhysicalFragmentCount(), 0u);

  if (PhysicalFragmentCount() == 1) {
    const NGPhysicalBoxFragment* fragment = GetPhysicalFragment(0);
    return fragment->PositionForPoint(target);
  }

  // When |this| is block fragmented, find the closest fragment.
  const NGPhysicalBoxFragment* closest_fragment = nullptr;
  PhysicalOffset closest_fragment_offset;
  LayoutUnit shortest_square_distance = LayoutUnit::Max();
  for (const NGPhysicalBoxFragment& fragment : PhysicalFragments()) {
    // If |fragment| contains |target|, call its |PositionForPoint|.
    const PhysicalOffset fragment_offset = fragment.OffsetFromOwnerLayoutBox();
    const PhysicalSize distance =
        PhysicalRect(fragment_offset, fragment.Size()).DistanceAsSize(target);
    if (distance.IsZero())
      return fragment.PositionForPoint(target - fragment_offset);

    // Otherwise find the closest fragment.
    const LayoutUnit square_distance =
        distance.width * distance.width + distance.height * distance.height;
    if (square_distance < shortest_square_distance || !closest_fragment) {
      shortest_square_distance = square_distance;
      closest_fragment = &fragment;
      closest_fragment_offset = fragment_offset;
    }
  }
  DCHECK(closest_fragment);
  return closest_fragment->PositionForPoint(target - closest_fragment_offset);
}

DISABLE_CFI_PERF
bool LayoutBox::ShouldBeConsideredAsReplaced() const {
  NOT_DESTROYED();
  if (IsAtomicInlineLevel())
    return true;
  // We need to detect all types of objects that should be treated as replaced.
  // Callers of this method will use the result for various things, such as
  // determining how to size the object, or whether it needs to avoid adjacent
  // floats, just like objects that establish a new formatting context.
  // IsAtomicInlineLevel() will not catch all the cases. Objects may be
  // block-level and still replaced, and we cannot deduce this from the
  // LayoutObject type. Checkboxes and radio buttons are such examples. We need
  // to check the Element type. This also applies to images, since we may have
  // created a block-flow LayoutObject for the ALT text (which still counts as
  // replaced).
  auto* element = DynamicTo<Element>(GetNode());
  if (!element)
    return false;
  if (element->IsFormControlElement()) {
    // Form control elements are generally replaced objects. Fieldsets are not,
    // though. A fieldset is (almost) a regular block container, and should be
    // treated as such.
    return !IsA<HTMLFieldSetElement>(element);
  }
  return IsA<HTMLImageElement>(element);
}

void LayoutBox::UpdateFragmentationInfoForChild(LayoutBox& child) {
  NOT_DESTROYED();
  LayoutState* layout_state = View()->GetLayoutState();
  DCHECK(layout_state->IsPaginated());
  child.SetOffsetToNextPage(LayoutUnit());
  if (!IsPageLogicalHeightKnown())
    return;

  LayoutUnit logical_top = child.LogicalTop();
  LayoutUnit logical_height = child.LogicalHeightWithVisibleOverflow();
  LayoutUnit space_left = PageRemainingLogicalHeightForOffset(
      logical_top, kAssociateWithLatterPage);
  if (space_left < logical_height)
    child.SetOffsetToNextPage(space_left);
}

bool LayoutBox::ChildNeedsRelayoutForPagination(const LayoutBox& child) const {
  NOT_DESTROYED();
  if (child.IsFloating())
    return true;
  const LayoutFlowThread* flow_thread = child.FlowThreadContainingBlock();
  // Figure out if we really need to force re-layout of the child. We only need
  // to do this if there's a chance that we need to recalculate pagination
  // struts inside.
  if (IsPageLogicalHeightKnown()) {
    LayoutUnit logical_top = child.LogicalTop();
    LayoutUnit logical_height = child.LogicalHeightWithVisibleOverflow();
    LayoutUnit remaining_space = PageRemainingLogicalHeightForOffset(
        logical_top, kAssociateWithLatterPage);
    if (child.OffsetToNextPage()) {
      // We need to relayout unless we're going to break at the exact same
      // location as before.
      if (child.OffsetToNextPage() != remaining_space)
        return true;
      // If column height isn't guaranteed to be uniform, we have no way of
      // telling what has happened after the first break.
      if (flow_thread && flow_thread->MayHaveNonUniformPageLogicalHeight())
        return true;
    } else if (logical_height > remaining_space) {
      // Last time we laid out this child, we didn't need to break, but now we
      // have to. So we need to relayout.
      return true;
    }
  } else if (child.OffsetToNextPage()) {
    // This child did previously break, but it won't anymore, because we no
    // longer have a known fragmentainer height.
    return true;
  }

  // It seems that we can skip layout of this child, but we need to ask the flow
  // thread for permission first. We currently cannot skip over objects
  // containing column spanners.
  return flow_thread && !flow_thread->CanSkipLayout(child);
}

void LayoutBox::MarkChildForPaginationRelayoutIfNeeded(
    LayoutBox& child,
    SubtreeLayoutScope& layout_scope) {
  NOT_DESTROYED();
  DCHECK(!child.NeedsLayout() || child.ChildLayoutBlockedByDisplayLock());
  LayoutState* layout_state = View()->GetLayoutState();

  if (layout_state->PaginationStateChanged() ||
      (layout_state->IsPaginated() && ChildNeedsRelayoutForPagination(child)))
    layout_scope.SetChildNeedsLayout(&child);
}

void LayoutBox::MarkOrthogonalWritingModeRoot() {
  NOT_DESTROYED();
  DCHECK(GetFrameView());
  GetFrameView()->AddOrthogonalWritingModeRoot(*this);
}

void LayoutBox::UnmarkOrthogonalWritingModeRoot() {
  NOT_DESTROYED();
  DCHECK(GetFrameView());
  GetFrameView()->RemoveOrthogonalWritingModeRoot(*this);
}

// Children of LayoutCustom object's are only considered "items" when it has a
// loaded algorithm.
bool LayoutBox::IsCustomItem() const {
  NOT_DESTROYED();
  auto* parent_layout_box = DynamicTo<LayoutNGCustom>(Parent());
  return parent_layout_box && parent_layout_box->IsLoaded();
}

// LayoutCustom items are only shrink-to-fit during the web-developer defined
// layout phase (not during fallback).
bool LayoutBox::IsCustomItemShrinkToFit() const {
  NOT_DESTROYED();
  DCHECK(IsCustomItem());
  return To<LayoutNGCustom>(Parent())->IsLoaded();
}

void LayoutBox::AddVisualEffectOverflow() {
  NOT_DESTROYED();
  if (!StyleRef().HasVisualOverflowingEffect())
    return;

  // Add in the final overflow with shadows, outsets and outline combined.
  PhysicalRect visual_effect_overflow = PhysicalBorderBoxRect();
  LayoutRectOutsets outsets = ComputeVisualEffectOverflowOutsets();
  visual_effect_overflow.Expand(outsets);
  AddSelfVisualOverflow(visual_effect_overflow);
  if (VisualOverflowIsSet())
    UpdateHasSubpixelVisualEffectOutsets(outsets);
}

LayoutRectOutsets LayoutBox::ComputeVisualEffectOverflowOutsets() {
  NOT_DESTROYED();
  const ComputedStyle& style = StyleRef();
  DCHECK(style.HasVisualOverflowingEffect());

  LayoutRectOutsets outsets = style.BoxDecorationOutsets();

  if (style.HasOutline()) {
    OutlineInfo info;
    Vector<PhysicalRect> outline_rects =
        OutlineRects(&info, PhysicalOffset(),
                     style.OutlineRectsShouldIncludeBlockVisualOverflow());
    PhysicalRect rect = UnionRect(outline_rects);
    bool outline_affected = rect.size != PhysicalSizeToBeNoop(Size());
    SetOutlineMayBeAffectedByDescendants(outline_affected);
    rect.Inflate(LayoutUnit(OutlinePainter::OutlineOutsetExtent(style, info)));
    outsets.Unite(LayoutRectOutsets(-rect.Y(), rect.Right() - Size().Width(),
                                    rect.Bottom() - Size().Height(),
                                    -rect.X()));
  }

  return outsets;
}

void LayoutBox::AddVisualOverflowFromChild(const LayoutBox& child,
                                           const LayoutSize& delta) {
  NOT_DESTROYED();
  // Never allow flow threads to propagate overflow up to a parent.
  if (child.IsLayoutFlowThread())
    return;

  // Add in visual overflow from the child.  Even if the child clips its
  // overflow, it may still have visual overflow of its own set from box shadows
  // or reflections. It is unnecessary to propagate this overflow if we are
  // clipping our own overflow.
  if (child.HasSelfPaintingLayer())
    return;
  LayoutRect child_visual_overflow_rect =
      child.VisualOverflowRectForPropagation();
  child_visual_overflow_rect.Move(delta);
  AddContentsVisualOverflow(child_visual_overflow_rect);
}

DISABLE_CFI_PERF
void LayoutBox::AddLayoutOverflowFromChild(const LayoutBox& child,
                                           const LayoutSize& delta) {
  NOT_DESTROYED();
  DCHECK(!ChildLayoutBlockedByDisplayLock());

  // Never allow flow threads to propagate overflow up to a parent.
  if (child.IsLayoutFlowThread())
    return;

  // Only propagate layout overflow from the child if the child isn't clipping
  // its overflow.  If it is, then its overflow is internal to it, and we don't
  // care about it. LayoutOverflowRectForPropagation takes care of this and just
  // propagates the border box rect instead.
  LayoutRect child_layout_overflow_rect =
      child.LayoutOverflowRectForPropagation(this);
  child_layout_overflow_rect.Move(delta);
  AddLayoutOverflow(child_layout_overflow_rect);
}

void LayoutBox::SetLayoutClientAfterEdge(LayoutUnit client_after_edge) {
  NOT_DESTROYED();
  if (LayoutOverflowIsSet())
    overflow_->layout_overflow->SetLayoutClientAfterEdge(client_after_edge);
}

LayoutUnit LayoutBox::LayoutClientAfterEdge() const {
  NOT_DESTROYED();
  return LayoutOverflowIsSet()
             ? overflow_->layout_overflow->LayoutClientAfterEdge()
             : ClientLogicalBottom();
}

PhysicalRect LayoutBox::PhysicalVisualOverflowRectIncludingFilters() const {
  NOT_DESTROYED();
  PhysicalRect bounds_rect = PhysicalVisualOverflowRect();
  if (!StyleRef().HasFilter())
    return bounds_rect;
  gfx::RectF float_rect(bounds_rect);
  gfx::RectF filter_reference_box = Layer()->FilterReferenceBox();
  if (!filter_reference_box.size().IsZero())
    float_rect.UnionEvenIfEmpty(filter_reference_box);
  float_rect = Layer()->MapRectForFilter(float_rect);
  return PhysicalRect::EnclosingRect(float_rect);
}

bool LayoutBox::HasTopOverflow() const {
  NOT_DESTROYED();
  return !StyleRef().IsLeftToRightDirection() && !IsHorizontalWritingMode();
}

bool LayoutBox::HasLeftOverflow() const {
  NOT_DESTROYED();
  if (IsHorizontalWritingMode())
    return !StyleRef().IsLeftToRightDirection();
  return StyleRef().GetWritingMode() == WritingMode::kVerticalRl;
}

void LayoutBox::SetLayoutOverflowFromLayoutResults() {
  NOT_DESTROYED();
  ClearSelfNeedsLayoutOverflowRecalc();
  ClearChildNeedsLayoutOverflowRecalc();
  ClearLayoutOverflow();

  const WritingMode writing_mode = StyleRef().GetWritingMode();
  absl::optional<PhysicalRect> layout_overflow;
  LayoutUnit consumed_block_size;

  // Iterate over all the fragments and unite their individual layout-overflow
  // to determine the final layout-overflow.
  for (const auto& layout_result : layout_results_) {
    const auto& fragment =
        To<NGPhysicalBoxFragment>(layout_result->PhysicalFragment());

    // In order to correctly unite the overflow, we need to shift an individual
    // fragment's layout-overflow by previously consumed block-size so far.
    PhysicalOffset offset_adjust;
    switch (writing_mode) {
      case WritingMode::kHorizontalTb:
        offset_adjust = {LayoutUnit(), consumed_block_size};
        break;
      case WritingMode::kVerticalRl:
      case WritingMode::kSidewaysRl:
        offset_adjust = {-fragment.Size().width - consumed_block_size,
                         LayoutUnit()};
        break;
      case WritingMode::kVerticalLr:
      case WritingMode::kSidewaysLr:
        offset_adjust = {consumed_block_size, LayoutUnit()};
        break;
      default:
        NOTREACHED();
        break;
    }

    PhysicalRect fragment_layout_overflow = fragment.LayoutOverflow();
    fragment_layout_overflow.offset += offset_adjust;

    // If we are the first fragment just set the layout-overflow.
    if (!layout_overflow)
      layout_overflow = fragment_layout_overflow;
    else
      layout_overflow->UniteEvenIfEmpty(fragment_layout_overflow);

    if (const auto* break_token = fragment.BreakToken()) {
      // The legacy engine doesn't understand our concept of repeated
      // fragments. Stop now. The overflow rectangle will represent the
      // fragment(s) generated under the first repeated root.
      if (break_token->IsRepeated())
        break;
      consumed_block_size = break_token->ConsumedBlockSize();
    }
  }

  if (!layout_overflow)
    return;

  // layout-overflow is stored respecting flipped-blocks.
  if (IsFlippedBlocksWritingMode(writing_mode)) {
    layout_overflow->offset.left =
        -layout_overflow->offset.left - layout_overflow->size.width;
  }

  if (layout_overflow->IsEmpty() ||
      PhysicalPaddingBoxRect().Contains(*layout_overflow))
    return;

  DCHECK(!LayoutOverflowIsSet());
  if (!overflow_)
    overflow_ = std::make_unique<BoxOverflowModel>();
  overflow_->layout_overflow.emplace(layout_overflow->ToLayoutRect());
}

DISABLE_CFI_PERF
void LayoutBox::AddLayoutOverflow(const LayoutRect& rect) {
  NOT_DESTROYED();
  if (rect.IsEmpty())
    return;

  LayoutRect client_box = NoOverflowRect();
  if (client_box.Contains(rect))
    return;

  // For overflow clip objects, we don't want to propagate overflow into
  // unreachable areas.
  LayoutRect overflow_rect(rect);
  if (IsScrollContainer() || IsA<LayoutView>(this)) {
    // Overflow is in the block's coordinate space and thus is flipped for
    // vertical-rl writing
    // mode.  At this stage that is actually a simplification, since we can
    // treat vertical-lr/rl
    // as the same.
    if (HasTopOverflow()) {
      overflow_rect.ShiftMaxYEdgeTo(
          std::min(overflow_rect.MaxY(), client_box.MaxY()));
    } else {
      overflow_rect.ShiftYEdgeTo(std::max(overflow_rect.Y(), client_box.Y()));
    }
    if (HasLeftOverflow() !=
        IsFlippedBlocksWritingMode(StyleRef().GetWritingMode())) {
      overflow_rect.ShiftMaxXEdgeTo(
          std::min(overflow_rect.MaxX(), client_box.MaxX()));
    } else {
      overflow_rect.ShiftXEdgeTo(std::max(overflow_rect.X(), client_box.X()));
    }

    // Now re-test with the adjusted rectangle and see if it has become
    // unreachable or fully
    // contained.
    if (client_box.Contains(overflow_rect) || overflow_rect.IsEmpty())
      return;
  }

  if (!LayoutOverflowIsSet()) {
    if (!overflow_)
      overflow_ = std::make_unique<BoxOverflowModel>();
    overflow_->layout_overflow.emplace(client_box);
  }

  overflow_->layout_overflow->AddLayoutOverflow(overflow_rect);
}

void LayoutBox::AddSelfVisualOverflow(const LayoutRect& rect) {
  NOT_DESTROYED();
  if (rect.IsEmpty())
    return;

  LayoutRect border_box = BorderBoxRect();
  if (border_box.Contains(rect))
    return;

  if (!VisualOverflowIsSet()) {
    if (!overflow_)
      overflow_ = std::make_unique<BoxOverflowModel>();

    overflow_->visual_overflow.emplace(border_box);
  }

  overflow_->visual_overflow->AddSelfVisualOverflow(rect);
}

void LayoutBox::AddContentsVisualOverflow(const LayoutRect& rect) {
  NOT_DESTROYED();
  if (rect.IsEmpty())
    return;

  // If hasOverflowClip() we always save contents visual overflow because we
  // need it
  // e.g. to determine whether to apply rounded corner clip on contents.
  // Otherwise we save contents visual overflow only if it overflows the border
  // box.
  LayoutRect border_box = BorderBoxRect();
  if (!HasNonVisibleOverflow() && border_box.Contains(rect))
    return;

  if (!VisualOverflowIsSet()) {
    if (!overflow_)
      overflow_ = std::make_unique<BoxOverflowModel>();

    overflow_->visual_overflow.emplace(border_box);
  }
  overflow_->visual_overflow->AddContentsVisualOverflow(rect);
}

void LayoutBox::UpdateHasSubpixelVisualEffectOutsets(
    const LayoutRectOutsets& outsets) {
  DCHECK(VisualOverflowIsSet());
  overflow_->visual_overflow->SetHasSubpixelVisualEffectOutsets(
      !IsIntegerValue(outsets.Top()) || !IsIntegerValue(outsets.Right()) ||
      !IsIntegerValue(outsets.Bottom()) || !IsIntegerValue(outsets.Left()));
}

void LayoutBox::SetVisualOverflow(const PhysicalRect& self,
                                  const PhysicalRect& contents) {
  ClearVisualOverflow();
  AddSelfVisualOverflow(self);
  AddContentsVisualOverflow(contents);
  if (!VisualOverflowIsSet())
    return;

  const LayoutRectOutsets outsets =
      overflow_->visual_overflow->SelfVisualOverflowRect().ToOutsets(Size());
  UpdateHasSubpixelVisualEffectOutsets(outsets);

  // |OutlineMayBeAffectedByDescendants| is set whenever outline style
  // changes. Update to the actual value here.
  const ComputedStyle& style = StyleRef();
  if (style.HasOutline()) {
    const LayoutUnit outline_extent(OutlinePainter::OutlineOutsetExtent(
        style, OutlineInfo::GetFromStyle(style)));
    SetOutlineMayBeAffectedByDescendants(
        outsets.Top() != outline_extent || outsets.Right() != outline_extent ||
        outsets.Bottom() != outline_extent || outsets.Left() != outline_extent);
  }
}

void LayoutBox::ClearLayoutOverflow() {
  NOT_DESTROYED();
  if (overflow_)
    overflow_->layout_overflow.reset();
  // overflow_ will be reset by MutableForPainting::ClearPreviousOverflowData()
  // if we don't need it to store previous overflow data.
}

void LayoutBox::ClearVisualOverflow() {
  NOT_DESTROYED();
  if (overflow_)
    overflow_->visual_overflow.reset();
  // overflow_ will be reset by MutableForPainting::ClearPreviousOverflowData()
  // if we don't need it to store previous overflow data.
}

bool LayoutBox::CanUseFragmentsForVisualOverflow() const {
  NOT_DESTROYED();
  // TODO(crbug.com/1144203): Legacy, or no-fragments-objects such as
  // table-column. What to do with them is TBD.
  if (!PhysicalFragmentCount())
    return false;
  const NGPhysicalBoxFragment& fragment = *GetPhysicalFragment(0);
  if (!fragment.CanUseFragmentsForInkOverflow())
    return false;
  return true;
}

void LayoutBox::RecalcFragmentsVisualOverflow() {
  NOT_DESTROYED();
  DCHECK(CanUseFragmentsForVisualOverflow());
  DCHECK_GT(PhysicalFragmentCount(), 0u);
  DCHECK(!DisplayLockUtilities::LockedAncestorPreventingPrePaint(*this));
  for (const NGPhysicalBoxFragment& fragment : PhysicalFragments()) {
    DCHECK(fragment.CanUseFragmentsForInkOverflow());
    fragment.GetMutableForPainting().RecalcInkOverflow();
  }
  // |NGPhysicalBoxFragment::RecalcInkOverflow| should have copied the computed
  // values back to |this| and its descendant fragments.
  //
  // We can't check descendants of |this| here, because the descendant fragments
  // may be different from descendant |LayoutObject|s, but the descendant
  // fragments should match what |PrePaintTreeWalk| traverses. If there were
  // mismatches, |PrePaintTreeWalk| should hit the DCHECKs.
  CheckIsVisualOverflowComputed();
}

// Copy visual overflow from |PhysicalFragments()|.
void LayoutBox::CopyVisualOverflowFromFragments() {
  NOT_DESTROYED();
  DCHECK(CanUseFragmentsForVisualOverflow());
  const LayoutRect previous_visual_overflow = VisualOverflowRectAllowingUnset();
  CopyVisualOverflowFromFragmentsWithoutInvalidations();
  const LayoutRect visual_overflow = VisualOverflowRect();
  if (visual_overflow == previous_visual_overflow)
    return;
  InvalidateIntersectionObserverCachedRects();
  SetShouldCheckForPaintInvalidation();
  GetFrameView()->SetIntersectionObservationState(LocalFrameView::kDesired);
}

void LayoutBox::CopyVisualOverflowFromFragmentsWithoutInvalidations() {
  NOT_DESTROYED();
  DCHECK(CanUseFragmentsForVisualOverflow());
  if (UNLIKELY(!PhysicalFragmentCount())) {
    DCHECK(IsLayoutTableCol());
    ClearVisualOverflow();
    return;
  }

  if (PhysicalFragmentCount() == 1) {
    const NGPhysicalBoxFragment& fragment = *GetPhysicalFragment(0);
    DCHECK(fragment.CanUseFragmentsForInkOverflow());
    if (!fragment.HasInkOverflow()) {
      ClearVisualOverflow();
      return;
    }
    SetVisualOverflow(fragment.SelfInkOverflow(),
                      fragment.ContentsInkOverflow());
    return;
  }

  // When block-fragmented, stitch visual overflows from all fragments.
  const LayoutBlock* cb = ContainingBlock();
  DCHECK(cb);
  const WritingMode writing_mode = cb->StyleRef().GetWritingMode();
  bool has_overflow = false;
  PhysicalRect self_rect;
  PhysicalRect contents_rect;
  const NGPhysicalBoxFragment* last_fragment = nullptr;
  for (const NGPhysicalBoxFragment& fragment : PhysicalFragments()) {
    DCHECK(fragment.CanUseFragmentsForInkOverflow());
    if (!fragment.HasInkOverflow()) {
      last_fragment = &fragment;
      continue;
    }
    has_overflow = true;

    PhysicalRect fragment_self_rect = fragment.SelfInkOverflow();
    PhysicalRect fragment_contents_rect = fragment.ContentsInkOverflow();

    // Stitch this fragment to the bottom of the last one in horizontal
    // writing mode, or to the right in vertical. Flipped blocks is handled
    // later, after the loop.
    if (last_fragment) {
      const NGBlockBreakToken* break_token = last_fragment->BreakToken();
      DCHECK(break_token);
      const LayoutUnit block_offset = break_token->ConsumedBlockSize();
      if (blink::IsHorizontalWritingMode(writing_mode)) {
        fragment_self_rect.offset.top += block_offset;
        fragment_contents_rect.offset.top += block_offset;
      } else {
        fragment_self_rect.offset.left += block_offset;
        fragment_contents_rect.offset.left += block_offset;
      }
    }
    last_fragment = &fragment;

    self_rect.Unite(fragment_self_rect);
    contents_rect.Unite(fragment_contents_rect);

    // The legacy engine doesn't understand our concept of repeated
    // fragments. Stop now. The overflow rectangle will represent the
    // fragment(s) generated under the first repeated root.
    if (fragment.BreakToken() && fragment.BreakToken()->IsRepeated())
      break;
  }

  if (!has_overflow) {
    ClearVisualOverflow();
    return;
  }
  if (UNLIKELY(IsFlippedBlocksWritingMode(writing_mode))) {
    DCHECK(!blink::IsHorizontalWritingMode(writing_mode));
    const LayoutUnit flip_offset = cb->Size().Width() - Size().Width();
    self_rect.offset.left += flip_offset;
    contents_rect.offset.left += flip_offset;
  }
  SetVisualOverflow(self_rect, contents_rect);
}

bool LayoutBox::PercentageLogicalHeightIsResolvable() const {
  NOT_DESTROYED();
  Length fake_length = Length::Percent(100);
  return ComputePercentageLogicalHeight(fake_length) != -1;
}

DISABLE_CFI_PERF
bool LayoutBox::HasUnsplittableScrollingOverflow(
    FragmentationEngine engine) const {
  NOT_DESTROYED();
  // Fragmenting scrollbars is only problematic in interactive media, e.g.
  // multicol on a screen. If we're printing, which is non-interactive media, we
  // should allow objects with non-visible overflow to be paginated as normally.
  if (GetDocument().Printing())
    return false;

  // In LayoutNG, treat any scrollable container as monolithic.
  if (engine == kNGFragmentationEngine && StyleRef().IsScrollContainer())
    return true;

  // We will paginate as long as we don't scroll overflow in the pagination
  // direction.
  bool is_horizontal = IsHorizontalWritingMode();
  if ((is_horizontal && !ScrollsOverflowY()) ||
      (!is_horizontal && !ScrollsOverflowX()))
    return false;

  // We do have overflow. We'll still be willing to paginate as long as the
  // block has auto logical height, auto or undefined max-logical-height and a
  // zero or auto min-logical-height.
  // Note this is just a heuristic, and it's still possible to have overflow
  // under these conditions, but it should work out to be good enough for common
  // cases. Paginating overflow with scrollbars present is not the end of the
  // world and is what we used to do in the old model anyway.
  return StyleRef().LogicalHeight().IsSpecified() ||
         (StyleRef().LogicalMaxHeight().IsSpecified() &&
          (!StyleRef().LogicalMaxHeight().IsPercentOrCalc() ||
           PercentageLogicalHeightIsResolvable())) ||
         (StyleRef().LogicalMinHeight().IsSpecified() &&
          (!StyleRef().LogicalMinHeight().IsPercentOrCalc() ||
           PercentageLogicalHeightIsResolvable()));
}

LayoutBox::PaginationBreakability LayoutBox::GetPaginationBreakability(
    FragmentationEngine engine) const {
  NOT_DESTROYED();
  // TODO(almaher): Don't consider a writing mode root monolitic if
  // IsLayoutNGFlexibleBox(). The breakability should be handled at the item
  // level. (Likely same for Table and Grid).
  if (ShouldBeConsideredAsReplaced() ||
      HasUnsplittableScrollingOverflow(engine) ||
      (Parent() && IsWritingModeRoot()) ||
      (IsFixedPositioned() && GetDocument().Printing() &&
       IsA<LayoutView>(Container())) ||
      ShouldApplySizeContainment() || IsFrameSetIncludingNG())
    return kForbidBreaks;

  if (engine != kUnknownFragmentationEngine) {
    // If the object isn't using the same engine as the fragmentation context,
    // it must be treated as monolithic.
    if (IsLayoutNGObject() != (engine == kNGFragmentationEngine))
      return kForbidBreaks;
  }

  EBreakInside break_value = BreakInside();
  if (break_value == EBreakInside::kAvoid ||
      break_value == EBreakInside::kAvoidPage ||
      break_value == EBreakInside::kAvoidColumn)
    return kAvoidBreaks;
  return kAllowAnyBreaks;
}

LayoutUnit LayoutBox::LineHeight(bool /*firstLine*/,
                                 LineDirectionMode direction,
                                 LinePositionMode /*linePositionMode*/) const {
  if (IsAtomicInlineLevel()) {
    return direction == kHorizontalLine ? MarginHeight() + Size().Height()
                                        : MarginWidth() + Size().Width();
  }
  return LayoutUnit();
}

DISABLE_CFI_PERF
LayoutUnit LayoutBox::BaselinePosition(
    FontBaseline baseline_type,
    bool /*firstLine*/,
    LineDirectionMode direction,
    LinePositionMode line_position_mode) const {
  DCHECK_EQ(line_position_mode, kPositionOnContainingLine);
  if (IsAtomicInlineLevel()) {
    LayoutUnit result = direction == kHorizontalLine
                            ? MarginHeight() + Size().Height()
                            : MarginWidth() + Size().Width();
    if (baseline_type == kAlphabeticBaseline)
      return result;
    return result - result / 2;
  }
  return LayoutUnit();
}

PaintLayer* LayoutBox::EnclosingFloatPaintingLayer() const {
  NOT_DESTROYED();
  const LayoutObject* curr = this;
  while (curr) {
    PaintLayer* layer = curr->HasLayer() && curr->IsBox()
                            ? To<LayoutBox>(curr)->Layer()
                            : nullptr;
    if (layer && layer->IsSelfPaintingLayer())
      return layer;
    curr = curr->Parent();
  }
  return nullptr;
}

LayoutRect LayoutBox::LogicalVisualOverflowRectForPropagation() const {
  NOT_DESTROYED();
  LayoutRect rect = VisualOverflowRectForPropagation();
  if (!Parent()->StyleRef().IsHorizontalWritingMode())
    return rect.TransposedRect();
  return rect;
}

DISABLE_CFI_PERF
LayoutRect LayoutBox::RectForOverflowPropagation(const LayoutRect& rect) const {
  NOT_DESTROYED();
  // If the child and parent are in the same blocks direction, then we don't
  // have to do anything fancy. Just return the rect.
  if (Parent()->StyleRef().IsFlippedBlocksWritingMode() ==
      StyleRef().IsFlippedBlocksWritingMode())
    return rect;

  // Convert the rect into parent's blocks direction by flipping along the y
  // axis.
  LayoutRect result = rect;
  result.SetX(Size().Width() - rect.MaxX());
  return result;
}

DISABLE_CFI_PERF
LayoutRect LayoutBox::LogicalLayoutOverflowRectForPropagation(
    LayoutObject* container) const {
  NOT_DESTROYED();
  LayoutRect rect = LayoutOverflowRectForPropagation(container);
  if (!Parent()->StyleRef().IsHorizontalWritingMode())
    return rect.TransposedRect();
  return rect;
}

LayoutRectOutsets LayoutBox::BorderBoxOutsetsForClipping() const {
  auto padding_box = -BorderBoxOutsets();
  if (!ShouldApplyOverflowClipMargin())
    return padding_box;

  LayoutRectOutsets overflow_clip_margin;
  switch (StyleRef().OverflowClipMargin()->GetReferenceBox()) {
    case StyleOverflowClipMargin::ReferenceBox::kBorderBox:
      break;
    case StyleOverflowClipMargin::ReferenceBox::kPaddingBox:
      overflow_clip_margin = padding_box;
      break;
    case StyleOverflowClipMargin::ReferenceBox::kContentBox:
      overflow_clip_margin = padding_box - PaddingOutsets();
      break;
  }

  return overflow_clip_margin + StyleRef().OverflowClipMargin()->GetMargin();
}

DISABLE_CFI_PERF
LayoutRect LayoutBox::LayoutOverflowRectForPropagation(
    LayoutObject* container) const {
  NOT_DESTROYED();
  // Only propagate interior layout overflow if we don't clip it.
  LayoutRect rect = BorderBoxRect();

  if (!ShouldApplyLayoutContainment() &&
      (!ShouldClipOverflowAlongBothAxis() || ShouldApplyOverflowClipMargin())) {
    LayoutRect overflow = LayoutOverflowRect();
    if (HasNonVisibleOverflow()) {
      const OverflowClipAxes overflow_clip_axes = GetOverflowClipAxes();
      LayoutRect clip_rect = rect;
      if (ShouldApplyOverflowClipMargin()) {
        // We should apply overflow clip margin only if we clip overflow on both
        // axes.
        DCHECK_EQ(overflow_clip_axes, kOverflowClipBothAxis);
        clip_rect.Expand(BorderBoxOutsetsForClipping());
        overflow.Intersect(clip_rect);
      } else {
        ApplyOverflowClip(overflow_clip_axes, clip_rect, overflow);
      }
    }
    rect.Unite(overflow);
  }

  bool has_transform = HasLayer() && Layer()->Transform();
  if (IsInFlowPositioned() || has_transform) {
    // If we are relatively positioned or if we have a transform, then we have
    // to convert this rectangle into physical coordinates, apply relative
    // positioning and transforms to it, and then convert it back.
    DeprecatedFlipForWritingMode(rect);

    PhysicalOffset container_offset;

    if (IsRelPositioned())
      container_offset = RelativePositionOffset();

    if (ShouldUseTransformFromContainer(container)) {
      TransformationMatrix t;
      GetTransformFromContainer(container ? container : Container(),
                                container_offset, t);
      rect = t.MapRect(rect);
    } else {
      rect.Move(container_offset.ToLayoutSize());
    }

    // Now we need to flip back.
    DeprecatedFlipForWritingMode(rect);
  }

  return RectForOverflowPropagation(rect);
}

DISABLE_CFI_PERF
LayoutRect LayoutBox::NoOverflowRect() const {
  NOT_DESTROYED();
  return FlipForWritingMode(PhysicalPaddingBoxRect());
}

LayoutRect LayoutBox::VisualOverflowRect() const {
  NOT_DESTROYED();
  if (!VisualOverflowIsSet())
    return BorderBoxRect();

  const LayoutRect& self_visual_overflow_rect =
      overflow_->visual_overflow->SelfVisualOverflowRect();
  if (HasMask()) {
    return self_visual_overflow_rect;
  }

  const OverflowClipAxes overflow_clip_axes = GetOverflowClipAxes();
  if (ShouldApplyOverflowClipMargin()) {
    // We should apply overflow clip margin only if we clip overflow on both
    // axis.
    DCHECK_EQ(overflow_clip_axes, kOverflowClipBothAxis);
    const LayoutRect& contents_visual_overflow_rect =
        overflow_->visual_overflow->ContentsVisualOverflowRect();
    if (!contents_visual_overflow_rect.IsEmpty()) {
      LayoutRect result = BorderBoxRect();
      result.Expand(BorderBoxOutsetsForClipping());
      result.Intersect(contents_visual_overflow_rect);
      result.Unite(self_visual_overflow_rect);
      return result;
    }
  }

  if (overflow_clip_axes == kOverflowClipBothAxis)
    return self_visual_overflow_rect;

  LayoutRect result = overflow_->visual_overflow->ContentsVisualOverflowRect();
  result.Unite(self_visual_overflow_rect);
  ApplyOverflowClip(overflow_clip_axes, self_visual_overflow_rect, result);
  return result;
}

#if DCHECK_IS_ON()
LayoutRect LayoutBox::VisualOverflowRectAllowingUnset() const {
  NOT_DESTROYED();
  NGInkOverflow::ReadUnsetAsNoneScope read_unset_as_none;
  return VisualOverflowRect();
}

PhysicalRect LayoutBox::PhysicalVisualOverflowRectAllowingUnset() const {
  NOT_DESTROYED();
  NGInkOverflow::ReadUnsetAsNoneScope read_unset_as_none;
  return PhysicalVisualOverflowRect();
}

void LayoutBox::CheckIsVisualOverflowComputed() const {
  // TODO(crbug.com/1205708): There are still too many failures. Disable the
  // the check for now. Need to investigate the reason.
  return;
  /*
  if (NGInkOverflow::ReadUnsetAsNoneScope::IsActive())
    return;
  if (!CanUseFragmentsForVisualOverflow())
    return;
  // TODO(crbug.com/1203402): MathML needs some more work.
  if (IsMathML())
    return;
  for (const NGPhysicalBoxFragment& fragment : PhysicalFragments())
    DCHECK(fragment.IsInkOverflowComputed());
  */
}
#endif

PhysicalOffset LayoutBox::OffsetPoint(const Element* parent) const {
  NOT_DESTROYED();
  return AdjustedPositionRelativeTo(PhysicalLocation(), parent);
}

LayoutUnit LayoutBox::OffsetLeft(const Element* parent) const {
  NOT_DESTROYED();
  return OffsetPoint(parent).left;
}

LayoutUnit LayoutBox::OffsetTop(const Element* parent) const {
  NOT_DESTROYED();
  return OffsetPoint(parent).top;
}

LayoutBox* LayoutBox::LocationContainer() const {
  NOT_DESTROYED();
  // Location of a non-root SVG object derived from LayoutBox should not be
  // affected by writing-mode of the containing box (SVGRoot).
  if (IsSVGChild())
    return nullptr;

  // Normally the box's location is relative to its containing box.
  LayoutObject* container = Container();
  while (container && !container->IsBox())
    container = container->Container();
  return To<LayoutBox>(container);
}

bool LayoutBox::HasRelativeLogicalWidth() const {
  NOT_DESTROYED();
  return StyleRef().LogicalWidth().IsPercentOrCalc() ||
         StyleRef().LogicalMinWidth().IsPercentOrCalc() ||
         StyleRef().LogicalMaxWidth().IsPercentOrCalc();
}

bool LayoutBox::HasRelativeLogicalHeight() const {
  NOT_DESTROYED();
  return StyleRef().LogicalHeight().IsPercentOrCalc() ||
         StyleRef().LogicalMinHeight().IsPercentOrCalc() ||
         StyleRef().LogicalMaxHeight().IsPercentOrCalc();
}

LayoutUnit LayoutBox::OffsetFromLogicalTopOfFirstPage() const {
  NOT_DESTROYED();
  LayoutState* layout_state = View()->GetLayoutState();
  if (!layout_state || !layout_state->IsPaginated())
    return LayoutUnit();

  if (layout_state->GetLayoutObject() == this) {
    LayoutSize offset = layout_state->PaginationOffset();
    return IsHorizontalWritingMode() ? offset.Height() : offset.Width();
  }

  // A LayoutBlock always establishes a layout state, and this method is only
  // meant to be called on the object currently being laid out.
  DCHECK(!IsLayoutBlock());

  // In case this box doesn't establish a layout state, try the containing
  // block.
  LayoutBlock* container_block = ContainingBlock();
  DCHECK(layout_state->GetLayoutObject() == container_block);
  return container_block->OffsetFromLogicalTopOfFirstPage() + LogicalTop();
}

void LayoutBox::SetOffsetToNextPage(LayoutUnit offset) {
  NOT_DESTROYED();
  if (!rare_data_ && !offset)
    return;
  EnsureRareData().offset_to_next_page_ = offset;
}

void LayoutBox::LogicalExtentAfterUpdatingLogicalWidth(
    const LayoutUnit& new_logical_top,
    LayoutBox::LogicalExtentComputedValues& computed_values) {
  NOT_DESTROYED();
  // FIXME: None of this is right for perpendicular writing-mode children.
  LayoutUnit old_logical_width = LogicalWidth();
  LayoutUnit old_logical_left = LogicalLeft();
  LayoutUnit old_margin_left = MarginLeft();
  LayoutUnit old_margin_right = MarginRight();
  LayoutUnit old_logical_top = LogicalTop();

  SetLogicalTop(new_logical_top);
  UpdateLogicalWidth();

  computed_values.extent_ = LogicalWidth();
  computed_values.position_ = LogicalLeft();
  computed_values.margins_.start_ = MarginStart();
  computed_values.margins_.end_ = MarginEnd();

  SetLogicalTop(old_logical_top);
  SetLogicalWidth(old_logical_width);
  SetLogicalLeft(old_logical_left);
  SetMarginLeft(old_margin_left);
  SetMarginRight(old_margin_right);
}

ShapeOutsideInfo* LayoutBox::GetShapeOutsideInfo() const {
  NOT_DESTROYED();
  return ShapeOutsideInfo::Info(*this);
}

void LayoutBox::SetPercentHeightContainer(LayoutBlock* container) {
  NOT_DESTROYED();
  DCHECK(!container || !PercentHeightContainer());
  if (!container && !rare_data_)
    return;
  EnsureRareData().percent_height_container_ = container;
}

void LayoutBox::RemoveFromPercentHeightContainer() {
  NOT_DESTROYED();
  if (!PercentHeightContainer())
    return;

  DCHECK(PercentHeightContainer()->HasPercentHeightDescendant(this));
  PercentHeightContainer()->RemovePercentHeightDescendant(this);
  // The above call should call this object's
  // setPercentHeightContainer(nullptr).
  DCHECK(!PercentHeightContainer());
}

void LayoutBox::ClearPercentHeightDescendants() {
  NOT_DESTROYED();
  for (LayoutObject* curr = SlowFirstChild(); curr;
       curr = curr->NextInPreOrder(this)) {
    if (curr->IsBox())
      To<LayoutBox>(curr)->RemoveFromPercentHeightContainer();
  }
}

LayoutUnit LayoutBox::PageLogicalHeightForOffset(LayoutUnit offset) const {
  NOT_DESTROYED();
  // We need to have calculated some fragmentainer logical height (even a
  // tentative one will do, though) in order to tell how tall one fragmentainer
  // is.
  DCHECK(IsPageLogicalHeightKnown());

  LayoutView* layout_view = View();
  LayoutFlowThread* flow_thread = FlowThreadContainingBlock();
  LayoutUnit page_logical_height;
  if (!flow_thread) {
    page_logical_height = layout_view->PageLogicalHeight();
  } else {
    page_logical_height = flow_thread->PageLogicalHeightForOffset(
        offset + OffsetFromLogicalTopOfFirstPage());
  }
  DCHECK_GT(page_logical_height, LayoutUnit());
  return page_logical_height;
}

bool LayoutBox::IsPageLogicalHeightKnown() const {
  NOT_DESTROYED();
  if (const LayoutFlowThread* flow_thread = FlowThreadContainingBlock())
    return flow_thread->IsPageLogicalHeightKnown();
  return View()->PageLogicalHeight();
}

LayoutUnit LayoutBox::PageRemainingLogicalHeightForOffset(
    LayoutUnit offset,
    PageBoundaryRule page_boundary_rule) const {
  NOT_DESTROYED();
  DCHECK(IsPageLogicalHeightKnown());
  LayoutView* layout_view = View();
  offset += OffsetFromLogicalTopOfFirstPage();

  LayoutUnit footer_height =
      View()->GetLayoutState()->HeightOffsetForTableFooters();
  LayoutFlowThread* flow_thread = FlowThreadContainingBlock();
  LayoutUnit remaining_height;
  if (!flow_thread) {
    LayoutUnit page_logical_height = layout_view->PageLogicalHeight();
    remaining_height =
        page_logical_height - IntMod(offset, page_logical_height);
    if (page_boundary_rule == kAssociateWithFormerPage) {
      // An offset exactly at a page boundary will act as being part of the
      // former page in question (i.e. no remaining space), rather than being
      // part of the latter (i.e. one whole page length of remaining space).
      remaining_height = IntMod(remaining_height, page_logical_height);
    }
  } else {
    remaining_height = flow_thread->PageRemainingLogicalHeightForOffset(
        offset, page_boundary_rule);
  }
  return remaining_height - footer_height;
}

int LayoutBox::CurrentPageNumber(LayoutUnit child_logical_top) const {
  NOT_DESTROYED();
  LayoutUnit offset = OffsetFromLogicalTopOfFirstPage() + child_logical_top;
  return (offset / View()->PageLogicalHeight()).Floor();
}

bool LayoutBox::CrossesPageBoundary(LayoutUnit offset,
                                    LayoutUnit logical_height) const {
  NOT_DESTROYED();
  if (!IsPageLogicalHeightKnown())
    return false;
  return PageRemainingLogicalHeightForOffset(offset, kAssociateWithLatterPage) <
         logical_height;
}

LayoutUnit LayoutBox::CalculatePaginationStrutToFitContent(
    LayoutUnit offset,
    LayoutUnit content_logical_height) const {
  NOT_DESTROYED();
  LayoutUnit strut_to_next_page =
      PageRemainingLogicalHeightForOffset(offset, kAssociateWithLatterPage);

  LayoutState* layout_state = View()->GetLayoutState();
  strut_to_next_page += layout_state->HeightOffsetForTableFooters();
  // If we're inside a cell in a row that straddles a page then avoid the
  // repeating header group if necessary. If we're a table section we're
  // already accounting for it.
  if (!IsTableSection()) {
    strut_to_next_page += layout_state->HeightOffsetForTableHeaders();
  }

  LayoutUnit next_page_logical_top = offset + strut_to_next_page;
  if (PageLogicalHeightForOffset(next_page_logical_top) >=
      content_logical_height)
    return strut_to_next_page;  // Content fits just fine in the next page or
                                // column.

  // Moving to the top of the next page or column doesn't result in enough space
  // for the content that we're trying to fit. If we're in a nested
  // fragmentation context, we may find enough space if we move to a column
  // further ahead, by effectively breaking to the next outer fragmentainer.
  LayoutFlowThread* flow_thread = FlowThreadContainingBlock();
  if (!flow_thread) {
    // If there's no flow thread, we're not nested. All pages have the same
    // height. Give up.
    return strut_to_next_page;
  }
  // Start searching for a suitable offset at the top of the next page or
  // column.
  LayoutUnit flow_thread_offset =
      OffsetFromLogicalTopOfFirstPage() + next_page_logical_top;
  return strut_to_next_page +
         flow_thread->NextLogicalTopForUnbreakableContent(
             flow_thread_offset, content_logical_height) -
         flow_thread_offset;
}

LayoutBox* LayoutBox::SnapContainer() const {
  NOT_DESTROYED();
  return rare_data_ ? rare_data_->snap_container_ : nullptr;
}

void LayoutBox::ClearSnapAreas() {
  NOT_DESTROYED();
  if (SnapAreaSet* areas = SnapAreas()) {
    for (const auto& snap_area : *areas)
      snap_area->rare_data_->snap_container_ = nullptr;
    areas->clear();
  }
}

void LayoutBox::AddSnapArea(LayoutBox& snap_area) {
  NOT_DESTROYED();
  EnsureRareData().snap_areas_.insert(&snap_area);
}

void LayoutBox::RemoveSnapArea(const LayoutBox& snap_area) {
  NOT_DESTROYED();
  // const_cast is safe here because we only need to modify the type to match
  // the key type, and not actually mutate the object.
  if (rare_data_)
    rare_data_->snap_areas_.erase(const_cast<LayoutBox*>(&snap_area));
}

void LayoutBox::ReassignSnapAreas(LayoutBox& new_container) {
  NOT_DESTROYED();
  SnapAreaSet* areas = SnapAreas();
  if (!areas)
    return;
  for (const auto& snap_area : *areas) {
    snap_area->rare_data_->snap_container_ = &new_container;
    new_container.AddSnapArea(*snap_area);
  }
  areas->clear();
}

SnapAreaSet* LayoutBox::SnapAreas() const {
  NOT_DESTROYED();
  return rare_data_ ? &rare_data_->snap_areas_ : nullptr;
}

CustomLayoutChild* LayoutBox::GetCustomLayoutChild() const {
  NOT_DESTROYED();
  DCHECK(rare_data_);
  DCHECK(rare_data_->layout_child_);
  return rare_data_->layout_child_.Get();
}

void LayoutBox::AddCustomLayoutChildIfNeeded() {
  NOT_DESTROYED();
  if (!IsCustomItem())
    return;

  const AtomicString& name = Parent()->StyleRef().DisplayLayoutCustomName();
  LayoutWorklet* worklet = LayoutWorklet::From(*GetDocument().domWindow());
  const CSSLayoutDefinition* definition =
      worklet->Proxy()->FindDefinition(name);

  // If there isn't a definition yet, the web developer defined layout isn't
  // loaded yet (or is invalid). The layout tree will get re-attached when
  // loaded, so don't bother creating a script representation of this node yet.
  if (!definition)
    return;

  EnsureRareData().layout_child_ =
      MakeGarbageCollected<CustomLayoutChild>(*definition, NGBlockNode(this));
}

void LayoutBox::ClearCustomLayoutChild() {
  NOT_DESTROYED();
  if (!rare_data_)
    return;

  if (rare_data_->layout_child_)
    rare_data_->layout_child_->ClearLayoutNode();

  rare_data_->layout_child_ = nullptr;
}

PhysicalRect LayoutBox::DebugRect() const {
  NOT_DESTROYED();
  return PhysicalRect(PhysicalLocation(), Size());
}

OverflowClipAxes LayoutBox::ComputeOverflowClipAxes() const {
  NOT_DESTROYED();
  if (ShouldApplyPaintContainment() || HasControlClip())
    return kOverflowClipBothAxis;

  if (!RespectsCSSOverflow() || !HasNonVisibleOverflow())
    return kNoOverflowClip;

  if (IsScrollContainer())
    return kOverflowClipBothAxis;
  return (StyleRef().OverflowX() == EOverflow::kVisible ? kNoOverflowClip
                                                        : kOverflowClipX) |
         (StyleRef().OverflowY() == EOverflow::kVisible ? kNoOverflowClip
                                                        : kOverflowClipY);
}

void LayoutBox::MutableForPainting::SavePreviousOverflowData() {
  if (!GetLayoutBox().overflow_)
    GetLayoutBox().overflow_ = std::make_unique<BoxOverflowModel>();
  auto& previous_overflow = GetLayoutBox().overflow_->previous_overflow_data;
  if (!previous_overflow)
    previous_overflow.emplace();
  previous_overflow->previous_physical_layout_overflow_rect =
      GetLayoutBox().PhysicalLayoutOverflowRect();
  previous_overflow->previous_physical_visual_overflow_rect =
      GetLayoutBox().PhysicalVisualOverflowRect();
  previous_overflow->previous_physical_self_visual_overflow_rect =
      GetLayoutBox().PhysicalSelfVisualOverflowRect();
}

void LayoutBox::MutableForPainting::SetPreviousGeometryForLayoutShiftTracking(
    const PhysicalOffset& paint_offset,
    const LayoutSize& size,
    const PhysicalRect& visual_overflow_rect) {
  FirstFragment().SetPaintOffset(paint_offset);
  GetLayoutBox().previous_size_ = size;
  if (PhysicalRect(PhysicalOffset(), size).Contains(visual_overflow_rect))
    return;

  if (!GetLayoutBox().overflow_)
    GetLayoutBox().overflow_ = std::make_unique<BoxOverflowModel>();
  auto& previous_overflow = GetLayoutBox().overflow_->previous_overflow_data;
  if (!previous_overflow)
    previous_overflow.emplace();
  previous_overflow->previous_physical_visual_overflow_rect =
      visual_overflow_rect;
  // Other previous rects don't matter because they are used for paint
  // invalidation and we always do full paint invalidation on reattachment.
}

RasterEffectOutset LayoutBox::VisualRectOutsetForRasterEffects() const {
  NOT_DESTROYED();
  // If the box has subpixel visual effect outsets, as the visual effect may be
  // painted along the pixel-snapped border box, the pixels on the anti-aliased
  // edge of the effect may overflow the calculated visual rect. Expand visual
  // rect by one pixel in the case.
  return VisualOverflowIsSet() &&
                 overflow_->visual_overflow->HasSubpixelVisualEffectOutsets()
             ? RasterEffectOutset::kWholePixel
             : RasterEffectOutset::kNone;
}

TextDirection LayoutBox::ResolvedDirection() const {
  NOT_DESTROYED();
  if (IsInline() && IsAtomicInlineLevel()) {
    if (IsInLayoutNGInlineFormattingContext()) {
      NGInlineCursor cursor;
      cursor.MoveTo(*this);
      if (cursor)
        return cursor.Current().ResolvedDirection();
    }
    if (InlineBoxWrapper())
      return InlineBoxWrapper()->Direction();
  }
  return StyleRef().Direction();
}

bool LayoutBox::NeedsScrollNode(
    CompositingReasons direct_compositing_reasons) const {
  NOT_DESTROYED();
  if (!IsScrollContainer())
    return false;

  if (direct_compositing_reasons & CompositingReason::kRootScroller)
    return true;

  return GetScrollableArea()->ScrollsOverflow();
}

void LayoutBox::OverrideTickmarks(Vector<gfx::Rect> tickmarks) {
  NOT_DESTROYED();
  GetScrollableArea()->SetTickmarksOverride(std::move(tickmarks));
  InvalidatePaintForTickmarks();
}

void LayoutBox::InvalidatePaintForTickmarks() {
  NOT_DESTROYED();
  ScrollableArea* scrollable_area = GetScrollableArea();
  if (!scrollable_area)
    return;
  Scrollbar* scrollbar = scrollable_area->VerticalScrollbar();
  if (!scrollbar)
    return;
  scrollbar->SetNeedsPaintInvalidation(static_cast<ScrollbarPart>(~kThumbPart));
}

static bool HasInsetBoxShadow(const ComputedStyle& style) {
  if (!style.BoxShadow())
    return false;
  for (const ShadowData& shadow : style.BoxShadow()->Shadows()) {
    if (shadow.Style() == ShadowStyle::kInset)
      return true;
  }
  return false;
}

// If all borders and scrollbars are opaque, then background-clip: border-box
// is equivalent to background-clip: padding-box.
bool LayoutBox::BackgroundClipBorderBoxIsEquivalentToPaddingBox() const {
  // Custom scrollbars may be translucent.
  // TODO(flackr): Detect opaque custom scrollbars which would cover up a
  // border-box background.
  const auto* scrollable_area = GetScrollableArea();
  if (scrollable_area &&
      ((scrollable_area->HorizontalScrollbar() &&
        scrollable_area->HorizontalScrollbar()->IsCustomScrollbar()) ||
       (scrollable_area->VerticalScrollbar() &&
        scrollable_area->VerticalScrollbar()->IsCustomScrollbar()))) {
    return false;
  }

  if (StyleRef().BorderTopWidth() &&
      (ResolveColor(GetCSSPropertyBorderTopColor()).HasAlpha() ||
       StyleRef().BorderTopStyle() != EBorderStyle::kSolid))
    return false;
  if (StyleRef().BorderRightWidth() &&
      (ResolveColor(GetCSSPropertyBorderRightColor()).HasAlpha() ||
       StyleRef().BorderRightStyle() != EBorderStyle::kSolid))
    return false;
  if (StyleRef().BorderBottomWidth() &&
      (ResolveColor(GetCSSPropertyBorderBottomColor()).HasAlpha() ||
       StyleRef().BorderBottomStyle() != EBorderStyle::kSolid))
    return false;
  if (StyleRef().BorderLeftWidth() &&
      (ResolveColor(GetCSSPropertyBorderLeftColor()).HasAlpha() ||
       StyleRef().BorderLeftStyle() != EBorderStyle::kSolid))
    return false;

  return true;
}

BackgroundPaintLocation LayoutBox::ComputeBackgroundPaintLocationIfComposited()
    const {
  NOT_DESTROYED();
  bool may_have_scrolling_layers_without_scrolling = IsA<LayoutView>(this);
  const auto* scrollable_area = GetScrollableArea();
  bool scrolls_overflow = scrollable_area && scrollable_area->ScrollsOverflow();
  if (!scrolls_overflow && !may_have_scrolling_layers_without_scrolling)
    return kBackgroundPaintInBorderBoxSpace;

  // If we care about LCD text, paint root backgrounds into scrolling contents
  // layer even if style suggests otherwise. (For non-root scrollers, we just
  // avoid compositing - see PLSA::ComputeNeedsCompositedScrolling.)
  if (IsA<LayoutView>(this)) {
    if (!GetDocument().GetSettings()->GetPreferCompositingToLCDTextEnabled())
      return kBackgroundPaintInContentsSpace;
  }

  // Inset box shadow is painted in the scrolling area above the background, and
  // it doesn't scroll, so the background can only be painted in the main layer.
  if (HasInsetBoxShadow(StyleRef()))
    return kBackgroundPaintInBorderBoxSpace;

  // Assume optimistically that the background can be painted in the scrolling
  // contents until we find otherwise.
  BackgroundPaintLocation paint_location = kBackgroundPaintInContentsSpace;

  Color background_color = ResolveColor(GetCSSPropertyBackgroundColor());
  const FillLayer* layer = &(StyleRef().BackgroundLayers());
  for (; layer; layer = layer->Next()) {
    if (layer->Attachment() == EFillAttachment::kLocal)
      continue;

    // The background color is either the only background or it's the
    // bottommost value from the background property (see final-bg-layer in
    // https://drafts.csswg.org/css-backgrounds/#the-background).
    if (!layer->GetImage() && !layer->Next() && background_color.Alpha() > 0 &&
        StyleRef().IsScrollbarGutterAuto()) {
      // Solid color layers with an effective background clip of the padding box
      // can be treated as local.
      EFillBox clip = layer->Clip();
      if (clip == EFillBox::kPadding)
        continue;
      // A border box can be treated as a padding box if the border is opaque or
      // there is no border and we don't have custom scrollbars.
      if (clip == EFillBox::kBorder) {
        if (BackgroundClipBorderBoxIsEquivalentToPaddingBox())
          continue;
        // If we have an opaque background color, we can safely paint it into
        // both the scrolling contents layer and the graphics layer to preserve
        // LCD text. The background color is either the only background or
        // behind background-attachment:local images (ensured by previous
        // iterations of the loop). For the latter case, the first paint of the
        // images doesn't matter because it will be covered by the second paint
        // of the opaque color.
        if (!background_color.HasAlpha()) {
          paint_location = kBackgroundPaintInBothSpaces;
          continue;
        }
      } else if (clip == EFillBox::kContent &&
                 StyleRef().PaddingTop().IsZero() &&
                 StyleRef().PaddingLeft().IsZero() &&
                 StyleRef().PaddingRight().IsZero() &&
                 StyleRef().PaddingBottom().IsZero()) {
        // A content fill box can be treated as a padding fill box if there is
        // no padding.
        continue;
      }
    }
    return kBackgroundPaintInBorderBoxSpace;
  }

  // It can't paint in the scrolling contents because it has different 3d
  // context than the scrolling contents.
  if (!StyleRef().Preserves3D() && Parent() &&
      Parent()->StyleRef().Preserves3D()) {
    return kBackgroundPaintInBorderBoxSpace;
  }

  return paint_location;
}

bool LayoutBox::IsFixedToView(
    const LayoutObject* container_for_fixed_position) const {
  if (!IsFixedPositioned())
    return false;

  const auto* container = container_for_fixed_position;
  if (!container)
    container = Container();
  else
    DCHECK_EQ(container, Container());
  return container->IsLayoutView();
}

PhysicalRect LayoutBox::ComputeStickyConstrainingRect() const {
  NOT_DESTROYED();
  DCHECK(IsScrollContainer());
  PhysicalRect constraining_rect(OverflowClipRect(LayoutPoint()));
  constraining_rect.Move(PhysicalOffset(-BorderLeft() + PaddingLeft(),
                                        -BorderTop() + PaddingTop()));
  constraining_rect.ContractEdges(LayoutUnit(), PaddingLeft() + PaddingRight(),
                                  PaddingTop() + PaddingBottom(), LayoutUnit());
  return constraining_rect;
}

const LayoutObject* LayoutBox::AnchorScrollObject() const {
  if (!StyleRef().AnchorScroll())
    return nullptr;

  if (StyleRef().GetPosition() != EPosition::kAbsolute &&
      StyleRef().GetPosition() != EPosition::kFixed) {
    return nullptr;
  }

  NGPhysicalFragmentList containing_block_fragments =
      ContainingBlock()->PhysicalFragments();
  if (containing_block_fragments.IsEmpty())
    return nullptr;

  // TODO(crbug.com/1309178): Fix it when the containing block is fragmented.
  const NGPhysicalAnchorQuery* anchor_query =
      containing_block_fragments.front().AnchorQuery();
  if (!anchor_query)
    return nullptr;

  if (const NGPhysicalFragment* fragment =
          anchor_query->Fragment(StyleRef().AnchorScroll())) {
    return fragment->GetLayoutObject();
  }
  return nullptr;
}

const LayoutBox* LayoutBox::AnchorScrollContainer() const {
  if (const LayoutObject* object = AnchorScrollObject()) {
    const LayoutBox* scroller = object->ContainingScrollContainer();
    if (scroller != ContainingScrollContainer())
      return scroller;
  }
  return nullptr;
}

LayoutBox::AnchorScrollData LayoutBox::ComputeAnchorScrollData() const {
  if (!AnchorScrollContainer())
    return AnchorScrollData();

  const PaintLayer* inner_most_scroll_container_layer =
      AnchorScrollContainer()->Layer();
  const PaintLayer* outer_most_scroll_container_layer =
      inner_most_scroll_container_layer;
  gfx::Vector2dF accumulated_scroll_offset(0, 0);
  gfx::Vector2d accumulated_scroll_origin(0, 0);
  for (const PaintLayer* layer = inner_most_scroll_container_layer; layer;
       layer = layer->ContainingScrollContainerLayer()) {
    if (layer == Layer()->ContainingScrollContainerLayer())
      break;
    accumulated_scroll_offset += layer->GetScrollableArea()->GetScrollOffset();
    accumulated_scroll_origin +=
        layer->GetScrollableArea()->ScrollOrigin().OffsetFromOrigin();
    outer_most_scroll_container_layer = layer;
  }

  return {inner_most_scroll_container_layer, outer_most_scroll_container_layer,
          accumulated_scroll_offset, accumulated_scroll_origin};
}

PhysicalOffset LayoutBox::ComputeAnchorScrollOffset() const {
  return -PhysicalOffset::FromVector2dFFloor(
      ComputeAnchorScrollData().accumulated_scroll_offset);
}

}  // namespace blink
