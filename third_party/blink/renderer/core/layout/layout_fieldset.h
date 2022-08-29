/*
 * Copyright (C) 1999 Lars Knoll (knoll@kde.org)
 *           (C) 1999 Antti Koivisto (koivisto@kde.org)
 *           (C) 2000 Dirk Mueller (mueller@kde.org)
 * Copyright (C) 2004, 2006, 2009 Apple Inc. All rights reserved.
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

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_LAYOUT_LAYOUT_FIELDSET_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_LAYOUT_LAYOUT_FIELDSET_H_

#include "third_party/blink/renderer/core/layout/layout_block_flow.h"

namespace blink {

class LayoutFieldset final : public LayoutBlockFlow {
 public:
  explicit LayoutFieldset(Element*);

  static LayoutBox* FindInFlowLegend(const LayoutBlock& fieldset);
  LayoutBox* FindInFlowLegend() const {
    NOT_DESTROYED();
    return FindInFlowLegend(*this);
  }

  const char* GetName() const override {
    NOT_DESTROYED();
    return "LayoutFieldset";
  }

  bool CreatesNewFormattingContext() const final {
    NOT_DESTROYED();
    return true;
  }

  bool BackgroundIsKnownToBeOpaqueInRect(const PhysicalRect&) const override;

 private:
  bool IsOfType(LayoutObjectType type) const override {
    NOT_DESTROYED();
    return type == kLayoutObjectFieldset || LayoutBlockFlow::IsOfType(type);
  }

  LayoutObject* LayoutSpecialExcludedChild(bool relayout_children,
                                           SubtreeLayoutScope&) override;

  MinMaxSizes PreferredLogicalWidths() const override;

  void PaintBoxDecorationBackground(
      const PaintInfo&,
      const PhysicalOffset& paint_offset) const override;
  void PaintMask(const PaintInfo&,
                 const PhysicalOffset& paint_offset) const override;
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_CORE_LAYOUT_LAYOUT_FIELDSET_H_
