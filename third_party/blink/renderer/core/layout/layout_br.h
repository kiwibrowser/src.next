/*
 * Copyright (C) 2000 Lars Knoll (knoll@kde.org)
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

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_LAYOUT_LAYOUT_BR_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_LAYOUT_LAYOUT_BR_H_

#include "third_party/blink/renderer/core/layout/layout_text.h"

// The whole class here is a hack to get <br> working, as long as we don't have
// support for CSS2 :before and :after pseudo elements.
namespace blink {

class LayoutBR : public LayoutText {
 public:
  explicit LayoutBR(Node*);
  ~LayoutBR() override;

  const char* GetName() const override {
    NOT_DESTROYED();
    return "LayoutBR";
  }

  // Although line breaks contain no actual text, if we're selected we need
  // to return a rect that includes space to illustrate a newline.
  using LayoutText::LocalSelectionVisualRect;

  float Width(unsigned /* from */,
              unsigned /* len */,
              const Font&,
              LayoutUnit /* xpos */,
              TextDirection,
              HashSet<const SimpleFontData*>* = nullptr /* fallbackFonts */,
              gfx::RectF* /* glyphBounds */ = nullptr,
              float /* expansion */ = false) const override {
    NOT_DESTROYED();
    return 0;
  }
  float Width(unsigned /* from */,
              unsigned /* len */,
              LayoutUnit /* xpos */,
              TextDirection,
              bool = false /* firstLine */,
              HashSet<const SimpleFontData*>* = nullptr /* fallbackFonts */,
              gfx::RectF* /* glyphBounds */ = nullptr,
              float /* expansion */ = false) const override {
    NOT_DESTROYED();
    return 0;
  }

  int LineHeight(bool first_line) const;

  bool IsOfType(LayoutObjectType type) const override {
    NOT_DESTROYED();
    return type == kLayoutObjectBr || LayoutText::IsOfType(type);
  }

  int CaretMinOffset() const override;
  int CaretMaxOffset() const override;

  PositionWithAffinity PositionForPoint(const PhysicalOffset&) const final;

  Position PositionForCaretOffset(unsigned) const final;
  absl::optional<unsigned> CaretOffsetForPosition(const Position&) const final;

 protected:
  void StyleDidChange(StyleDifference, const ComputedStyle* old_style) override;
};

template <>
struct DowncastTraits<LayoutBR> {
  static bool AllowFrom(const LayoutObject& object) { return object.IsBR(); }
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_CORE_LAYOUT_LAYOUT_BR_H_
