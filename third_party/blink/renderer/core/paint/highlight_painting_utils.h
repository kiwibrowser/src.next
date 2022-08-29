// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_PAINT_HIGHLIGHT_PAINTING_UTILS_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_PAINT_HIGHLIGHT_PAINTING_UTILS_H_

#include "base/memory/scoped_refptr.h"
#include "third_party/abseil-cpp/absl/types/optional.h"
#include "third_party/blink/renderer/core/core_export.h"
#include "third_party/blink/renderer/core/paint/paint_flags.h"
#include "third_party/blink/renderer/core/style/applied_text_decoration.h"
#include "third_party/blink/renderer/platform/wtf/allocator/allocator.h"
#include "third_party/blink/renderer/platform/wtf/text/atomic_string.h"

namespace blink {

class Color;
class Document;
class ComputedStyle;
class Node;
struct TextPaintStyle;
struct PaintInfo;

class CORE_EXPORT HighlightPaintingUtils {
  STATIC_ONLY(HighlightPaintingUtils);

 public:
  static absl::optional<AppliedTextDecoration> HighlightTextDecoration(
      const ComputedStyle& style,
      const ComputedStyle& pseudo_style);
  static Color HighlightBackgroundColor(
      const Document&,
      const ComputedStyle&,
      Node*,
      absl::optional<Color> previous_layer_color,
      PseudoId,
      const AtomicString& pseudo_argument = g_null_atom);
  static Color HighlightForegroundColor(
      const Document&,
      const ComputedStyle&,
      Node*,
      Color previous_layer_color,
      PseudoId,
      PaintFlags,
      const AtomicString& pseudo_argument = g_null_atom);
  static Color HighlightEmphasisMarkColor(
      const Document&,
      const ComputedStyle&,
      Node*,
      Color previous_layer_color,
      PseudoId,
      PaintFlags,
      const AtomicString& pseudo_argument = g_null_atom);
  static TextPaintStyle HighlightPaintingStyle(
      const Document&,
      const ComputedStyle&,
      Node*,
      PseudoId,
      const TextPaintStyle& text_style,
      const PaintInfo&,
      const AtomicString& pseudo_argument = g_null_atom);
  static absl::optional<Color>
  HighlightTextDecorationColor(const ComputedStyle&, Node*, PseudoId);

  static scoped_refptr<const ComputedStyle> HighlightPseudoStyle(
      Node* node,
      const ComputedStyle& style,
      PseudoId pseudo,
      const AtomicString& pseudo_argument = g_null_atom);
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_CORE_PAINT_HIGHLIGHT_PAINTING_UTILS_H_
