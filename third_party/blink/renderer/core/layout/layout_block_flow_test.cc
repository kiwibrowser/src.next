// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/layout/ng/ng_layout_test.h"

namespace blink {

class LayoutBlockFlowTest : public NGLayoutTest {};

// crbug.com/1253159.  We had a bug that a legacy IFC LayoutBlockFlow didn't
// call RecalcVisualOverflow() for children.
TEST_F(LayoutBlockFlowTest, RecalcInlineChildrenLayoutOverflow) {
  SetBodyInnerHTML(R"HTML(
<style>
kbd { float: right; }
var { column-count: 17179869184; }
</style>
<kbd id="kbd">
<var>
<svg>
<text id="text">B B
)HTML");
  LayoutBlockFlow* kbd = To<LayoutBlockFlow>(GetLayoutObjectByElementId("kbd"));
  // The parent should be NG.
  ASSERT_TRUE(kbd->Parent()->IsLayoutNGBlockFlow());
  ASSERT_TRUE(kbd->CreatesNewFormattingContext());
  UpdateAllLifecyclePhasesForTest();
  GetElementById("text")->setAttribute("font-size", "100");
  UpdateAllLifecyclePhasesForTest();
  // The test passes if no DCHECK failure in ng_ink_overflow.cc.
}

}  // namespace blink
