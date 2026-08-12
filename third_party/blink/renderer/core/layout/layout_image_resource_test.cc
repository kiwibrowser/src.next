// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/layout/layout_image_resource.h"

#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/blink/renderer/core/layout/layout_image.h"
#include "third_party/blink/renderer/core/testing/core_unit_test_helper.h"
#include "third_party/blink/renderer/platform/graphics/test/stub_image.h"

namespace blink {

class LayoutImageResourceTest : public RenderingTest {
 public:
 protected:
};

TEST_F(LayoutImageResourceTest, BrokenImageHighRes) {
  EXPECT_NE(LayoutImageResource::BrokenImage(2.0),
            LayoutImageResource::BrokenImage(1.0));
}

TEST_F(LayoutImageResourceTest, InvalidatePaintRequiresCachedImage) {
  SetBodyInnerHTML("<img id='image' style='width: 100px; height: 100px;'>");
  UpdateAllLifecyclePhasesForTest();

  auto* layout_image = To<LayoutImage>(GetLayoutObjectByElementId("image"));
  LayoutImageResource* image_resource = layout_image->ImageResource();

  layout_image->ClearPaintInvalidationFlags();
  image_resource->InvalidatePaint();
  EXPECT_FALSE(layout_image->ShouldDoFullPaintInvalidation());

  ImageResourceContent* image =
      ImageResourceContent::CreateLoaded(base::AdoptRef(new StubImage()));
  image_resource->SetImageResource(image);

  layout_image->ClearPaintInvalidationFlags();
  image_resource->InvalidatePaint();
  EXPECT_TRUE(layout_image->ShouldDoFullPaintInvalidation());
}

}  // namespace blink
