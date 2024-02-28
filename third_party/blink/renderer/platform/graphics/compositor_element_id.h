// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_PLATFORM_GRAPHICS_COMPOSITOR_ELEMENT_ID_H_
#define THIRD_PARTY_BLINK_RENDERER_PLATFORM_GRAPHICS_COMPOSITOR_ELEMENT_ID_H_

#include <type_traits>

#include "cc/paint/element_id.h"
#include "third_party/blink/renderer/platform/graphics/dom_node_id.h"
#include "third_party/blink/renderer/platform/platform_export.h"

namespace blink {

const int kCompositorNamespaceBitCount = 5;

// The functions in this header requires cc::ElementId::InternalValue to be
// uint64_t.
static_assert(std::is_same_v<cc::ElementId::InternalValue, uint64_t>);

enum class CompositorElementIdNamespace {
  kPrimary,
  kUniqueObjectId,
  kScroll,
  kStickyTranslation,
  kAnchorPositionScrollTranslation,
  kPrimaryEffect,
  kPrimaryTransform,
  kEffectFilter,
  kEffectMask,
  kEffectClipPath,
  kScaleTransform,
  kRotateTransform,
  kTranslateTransform,
  kVerticalScrollbar,
  kHorizontalScrollbar,
  kScrollCorner,
  kViewTransitionElement,
  kElementCapture,
  kDOMNodeId,
  // The following values are for internal usage only.
  kMax = kDOMNodeId,
  // A sentinel to indicate the maximum representable namespace id
  // (the maximum is one less than this value).
  kMaxRepresentable = 1 << kCompositorNamespaceBitCount
};

static_assert(CompositorElementIdNamespace::kMax <
              CompositorElementIdNamespace::kMaxRepresentable);

using CompositorElementId = cc::ElementId;
using ScrollbarId = uint64_t;
using UniqueObjectId = uint64_t;
using SyntheticEffectId = uint64_t;

// Call this to get a globally unique object id for a newly allocated object.
UniqueObjectId PLATFORM_EXPORT NewUniqueObjectId();

// Call this with an appropriate namespace if more than one CompositorElementId
// is required for the given UniqueObjectId.
CompositorElementId PLATFORM_EXPORT
    CompositorElementIdFromUniqueObjectId(UniqueObjectId,
                                          CompositorElementIdNamespace);
// ...and otherwise call this method if there is only one CompositorElementId
// required for the given UniqueObjectId.
CompositorElementId PLATFORM_EXPORT
    CompositorElementIdFromUniqueObjectId(UniqueObjectId);

// Returns a CompositorElementId with namespace of `element_id` replaced with
// `namespace_id`.
CompositorElementId PLATFORM_EXPORT
CompositorElementIdWithNamespace(CompositorElementId element_id,
                                 CompositorElementIdNamespace namespace_id);

// TODO(chrishtr): refactor ScrollState to remove this dependency.
CompositorElementId PLATFORM_EXPORT CompositorElementIdFromDOMNodeId(DOMNodeId);

CompositorElementIdNamespace PLATFORM_EXPORT
    NamespaceFromCompositorElementId(CompositorElementId);

// Maps a CompositorElementId in the kDOMNodeId namespace back to a DOMNodeId.
DOMNodeId PLATFORM_EXPORT DOMNodeIdFromCompositorElementId(CompositorElementId);

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_PLATFORM_GRAPHICS_COMPOSITOR_ELEMENT_ID_H_
