/*
 * Copyright (C) 2006, 2007, 2009, 2012 Apple Inc. All rights reserved.
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

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_LAYOUT_LAYOUT_FILE_UPLOAD_CONTROL_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_LAYOUT_LAYOUT_FILE_UPLOAD_CONTROL_H_

#include "third_party/blink/renderer/core/core_export.h"
#include "third_party/blink/renderer/core/layout/layout_block_flow.h"

namespace blink {

// Each LayoutFileUploadControl contains a LayoutButton (for opening the file
// chooser), and sufficient space to draw a file icon and filename. The
// LayoutButton has a shadow node associated with it to receive click/hover
// events.

class CORE_EXPORT LayoutFileUploadControl final : public LayoutBlockFlow {
 public:
  explicit LayoutFileUploadControl(Element*);
  ~LayoutFileUploadControl() override;

  bool IsOfType(LayoutObjectType type) const override {
    NOT_DESTROYED();
    return type == kLayoutObjectFileUploadControl ||
           LayoutBlockFlow::IsOfType(type);
  }

  String FileTextValue() const;

  HTMLInputElement* UploadButton() const;

  static const int kAfterButtonSpacing = 4;

  const char* GetName() const override {
    NOT_DESTROYED();
    return "LayoutFileUploadControl";
  }

 private:
  bool IsChildAllowed(LayoutObject* child,
                      const ComputedStyle& style) const override;
  void PaintObject(const PaintInfo&,
                   const PhysicalOffset& paint_offset) const override;

  int MaxFilenameWidth() const;
};

template <>
struct DowncastTraits<LayoutFileUploadControl> {
  static bool AllowFrom(const LayoutObject& object) {
    return object.IsFileUploadControl();
  }
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_CORE_LAYOUT_LAYOUT_FILE_UPLOAD_CONTROL_H_
