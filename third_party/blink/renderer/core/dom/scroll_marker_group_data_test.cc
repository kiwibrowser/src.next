// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/dom/scroll_marker_group_data.h"

#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/blink/renderer/core/dom/document.h"
#include "third_party/blink/renderer/core/dom/element.h"
#include "third_party/blink/renderer/core/dom/pseudo_element.h"
#include "third_party/blink/renderer/core/dom/scroll_marker_group_pseudo_element.h"
#include "third_party/blink/renderer/core/testing/core_unit_test_helper.h"

namespace blink {

class ScrollMarkerGroupDataTest : public RenderingTest {
 protected:
  void SetupSampleHTML(const char* main_html) {
    SetBodyInnerHTML(String::FromUtf8(main_html));
  }
};

TEST_F(ScrollMarkerGroupDataTest, SelectedMarkerReturnsNullIfDisconnected) {
  SetupSampleHTML(R"(
    <style>
      #container {
        overflow: scroll;
        scroll-marker-group: before;
      }
      #container::scroll-marker-group {
        display: flex;
      }
      #item::scroll-marker {
        content: ' ';
      }
    </style>
    <div id='container'>
      <div id='item'></div>
    </div>
  )");
  UpdateAllLifecyclePhasesForTest();

  Element* container = GetDocument().QuerySelector(AtomicString("#container"));
  Element* item = GetDocument().QuerySelector(AtomicString("#item"));
  PseudoElement* smg =
      container->GetPseudoElement(kPseudoIdScrollMarkerGroupBefore);
  PseudoElement* sm = item->GetPseudoElement(kPseudoIdScrollMarker);

  EXPECT_TRUE(smg);
  EXPECT_TRUE(sm);

  auto* smg_pseudo = To<ScrollMarkerGroupPseudoElement>(smg);
  smg_pseudo->SetSelected(*To<ScrollMarkerPseudoElement>(sm));

  EXPECT_EQ(sm, smg_pseudo->Selected());

  // Remove item from DOM to disconnect the marker.
  item->remove();

  // Now sm is disconnected. Selected() should return null.
  EXPECT_EQ(nullptr, smg_pseudo->Selected());
}

}  // namespace blink
