// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/layout/layout_font_accessor_win.h"

#include "third_party/blink/renderer/core/frame/local_frame.h"
#include "third_party/blink/renderer/core/layout/layout_block_flow.h"
#include "third_party/blink/renderer/core/layout/layout_box.h"
#include "third_party/blink/renderer/core/layout/layout_object.h"
#include "third_party/blink/renderer/core/layout/layout_view.h"
#include "third_party/blink/renderer/core/layout/ng/inline/ng_fragment_item.h"
#include "third_party/blink/renderer/core/layout/ng/inline/ng_inline_cursor.h"
#include "third_party/blink/renderer/core/layout/ng/ng_link.h"
#include "third_party/blink/renderer/core/layout/ng/ng_physical_box_fragment.h"
#include "third_party/blink/renderer/platform/fonts/font_platform_data.h"
#include "third_party/blink/renderer/platform/fonts/shaping/shape_result_view.h"
#include "third_party/blink/renderer/platform/fonts/simple_font_data.h"

namespace blink {

namespace {

void GetFontsUsedByLayoutObject(const LayoutObject& layout_object,
                                FontFamilyNames& result);

void GetFontsUsedByFragment(const NGPhysicalBoxFragment& fragment,
                            FontFamilyNames& result) {
  for (NGInlineCursor cursor(fragment); cursor; cursor.MoveToNext()) {
    const NGFragmentItem& item = *cursor.Current().Item();
    if (item.IsText()) {
      const ShapeResultView* shape_result_view = item.TextShapeResult();
      if (shape_result_view) {
        const String font_family =
            shape_result_view->PrimaryFont()->PlatformData().FontFamilyName();
        if (!font_family.IsEmpty())
          result.primary_fonts.insert(font_family);
        HashSet<const SimpleFontData*> fallback_font_data;
        shape_result_view->FallbackFonts(&fallback_font_data);
        for (const SimpleFontData* font_data : fallback_font_data) {
          result.fallback_fonts.insert(
              font_data->PlatformData().FontFamilyName());
        }
      }
      continue;
    }

    // If this is a nested BFC (e.g., inline block, floats), compute its area.
    if (item.Type() == NGFragmentItem::kBox) {
      if (const auto* layout_box = DynamicTo<LayoutBox>(item.GetLayoutObject()))
        GetFontsUsedByLayoutObject(*layout_box, result);
    }
  }

  // Traverse out-of-flow children. They are not in |NGFragmentItems|.
  for (const NGLink& child : fragment.Children()) {
    if (const auto* child_layout_box =
            DynamicTo<LayoutBox>(child->GetLayoutObject()))
      GetFontsUsedByLayoutObject(*child_layout_box, result);
  }
}

void GetFontsUsedByLayoutObject(const LayoutObject& layout_object,
                                FontFamilyNames& result) {
  const LayoutObject* target = &layout_object;
  while (target) {
    // Use |NGInlineCursor| to traverse if |target| is an IFC.
    if (const auto* block_flow = DynamicTo<LayoutBlockFlow>(target)) {
      if (block_flow->HasFragmentItems()) {
        for (const NGPhysicalBoxFragment& fragment :
             block_flow->PhysicalFragments())
          GetFontsUsedByFragment(fragment, result);
        target = target->NextInPreOrderAfterChildren(&layout_object);
        continue;
      }
    }
    target = target->NextInPreOrder(&layout_object);
  }
}

}  // namespace

void GetFontsUsedByFrame(const LocalFrame& frame, FontFamilyNames& result) {
  GetFontsUsedByLayoutObject(frame.ContentLayoutObject()->RootBox(), result);
}

}  // namespace blink
