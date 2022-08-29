/*
 * Copyright (C) 1999 Lars Knoll (knoll@kde.org)
 *           (C) 2000 Simon Hausmann <hausmann@kde.org>
 * Copyright (C) 2006, 2008 Apple Inc. All rights reserved.
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

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_LAYOUT_LAYOUT_FRAME_SET_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_LAYOUT_LAYOUT_FRAME_SET_H_

#include "base/check_op.h"
#include "third_party/blink/renderer/core/layout/layout_box.h"

namespace blink {

class HTMLDimension;
class HTMLFrameSetElement;
class MouseEvent;

enum FrameEdge {
  kLeftFrameEdge,
  kRightFrameEdge,
  kTopFrameEdge,
  kBottomFrameEdge
};

struct FrameEdgeInfo {
  STACK_ALLOCATED();

 public:
  FrameEdgeInfo(bool prevent_resize = false, bool allow_border = true)
      : prevent_resize_(4), allow_border_(4) {
    prevent_resize_.Fill(prevent_resize);
    allow_border_.Fill(allow_border);
  }

  bool PreventResize(FrameEdge edge) const { return prevent_resize_[edge]; }
  bool AllowBorder(FrameEdge edge) const { return allow_border_[edge]; }

  void SetPreventResize(FrameEdge edge, bool prevent_resize) {
    prevent_resize_[edge] = prevent_resize;
  }
  void SetAllowBorder(FrameEdge edge, bool allow_border) {
    allow_border_[edge] = allow_border;
  }

 private:
  Vector<bool> prevent_resize_;
  Vector<bool> allow_border_;
};

class LayoutFrameSet final : public LayoutBox {
 public:
  LayoutFrameSet(HTMLFrameSetElement*);
  ~LayoutFrameSet() override;
  void Trace(Visitor*) const override;

  LayoutObject* FirstChild() const {
    NOT_DESTROYED();
    DCHECK_EQ(Children(), VirtualChildren());
    return Children()->FirstChild();
  }
  LayoutObject* LastChild() const {
    NOT_DESTROYED();
    DCHECK_EQ(Children(), VirtualChildren());
    return Children()->LastChild();
  }

  // If you have a LayoutFrameSet, use firstChild or lastChild instead.
  void SlowFirstChild() const = delete;
  void SlowLastChild() const = delete;

  const LayoutObjectChildList* Children() const {
    NOT_DESTROYED();
    return &children_;
  }
  LayoutObjectChildList* Children() {
    NOT_DESTROYED();
    return &children_;
  }

  FrameEdgeInfo EdgeInfo() const;

  bool UserResize(const MouseEvent&);

  bool CanResizeRow(const gfx::Point&) const;
  bool CanResizeColumn(const gfx::Point&) const;

  void NotifyFrameEdgeInfoChanged();
  HTMLFrameSetElement* FrameSet() const;

  class GridAxis {
    DISALLOW_NEW();

   public:
    GridAxis();
    GridAxis(const GridAxis&) = delete;
    GridAxis& operator=(const GridAxis&) = delete;
    void Resize(int);

    Vector<int> sizes_;
    Vector<int> deltas_;
    Vector<bool> prevent_resize_;
    Vector<bool> allow_border_;
    int split_being_resized_;
    int split_resize_offset_;
  };

  const GridAxis& Rows() const {
    NOT_DESTROYED();
    return rows_;
  }
  const GridAxis& Columns() const {
    NOT_DESTROYED();
    return cols_;
  }

  const char* GetName() const override {
    NOT_DESTROYED();
    return "LayoutFrameSet";
  }

 private:
  static const int kNoSplit = -1;

  LayoutObjectChildList* VirtualChildren() override {
    NOT_DESTROYED();
    return Children();
  }
  const LayoutObjectChildList* VirtualChildren() const override {
    NOT_DESTROYED();
    return Children();
  }

  bool IsOfType(LayoutObjectType type) const override {
    NOT_DESTROYED();
    return type == kLayoutObjectFrameSet || LayoutBox::IsOfType(type);
  }

  void UpdateLayout() override;
  void Paint(const PaintInfo&) const override;

  MinMaxSizes PreferredLogicalWidths() const override {
    NOT_DESTROYED();
    return MinMaxSizes();
  }
  MinMaxSizes ComputeIntrinsicLogicalWidths() const final {
    NOT_DESTROYED();
    MinMaxSizes sizes;
    LayoutUnit scrollbar_thickness = ComputeLogicalScrollbars().InlineSum();
    sizes += BorderAndPaddingLogicalWidth() + scrollbar_thickness;
    return sizes;
  }

  bool IsChildAllowed(LayoutObject*, const ComputedStyle&) const override;
  CursorDirective GetCursor(const PhysicalOffset&, ui::Cursor&) const override;

  void SetIsResizing(bool);

  void LayOutAxis(GridAxis&, const Vector<HTMLDimension>&, int available_space);
  void ComputeEdgeInfo();
  void FillFromEdgeInfo(const FrameEdgeInfo&, int r, int c);
  void PositionFrames();

  int SplitPosition(const GridAxis&, int split) const;
  int HitTestSplit(const GridAxis&, int position) const;

  void StartResizing(GridAxis&, int position);
  void ContinueResizing(GridAxis&, int position);

  LayoutObjectChildList children_;

  GridAxis rows_;
  GridAxis cols_;

  bool is_resizing_;
};

template <>
struct DowncastTraits<LayoutFrameSet> {
  static bool AllowFrom(const LayoutObject& object) {
    return object.IsFrameSet();
  }
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_CORE_LAYOUT_LAYOUT_FRAME_SET_H_
