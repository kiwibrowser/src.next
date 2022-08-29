/*
 * Copyright (C) 1999 Lars Knoll (knoll@kde.org)
 *           (C) 1999 Antti Koivisto (koivisto@kde.org)
 *           (C) 2001 Dirk Mueller (mueller@kde.org)
 *           (C) 2006 Alexey Proskuryakov (ap@webkit.org)
 * Copyright (C) 2004, 2005, 2006, 2007, 2008, 2009, 2010, 2012 Apple Inc. All
 * rights reserved.
 * Copyright (C) 2008, 2009 Torch Mobile Inc. All rights reserved.
 * (http://www.torchmobile.com/)
 * Copyright (C) 2010 Nokia Corporation and/or its subsidiary(-ies)
 * Copyright (C) 2013 Google Inc. All rights reserved.
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

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_CSS_SHADOW_TREE_STYLE_SHEET_COLLECTION_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_CSS_SHADOW_TREE_STYLE_SHEET_COLLECTION_H_

#include "third_party/blink/renderer/core/css/tree_scope_style_sheet_collection.h"
#include "third_party/blink/renderer/platform/wtf/casting.h"

namespace blink {

class ShadowRoot;
class StyleEngine;
class StyleSheetCollection;

class ShadowTreeStyleSheetCollection final
    : public TreeScopeStyleSheetCollection {
 public:
  explicit ShadowTreeStyleSheetCollection(ShadowRoot&);
  ShadowTreeStyleSheetCollection(const ShadowTreeStyleSheetCollection&) =
      delete;
  ShadowTreeStyleSheetCollection& operator=(
      const ShadowTreeStyleSheetCollection&) = delete;

  void UpdateActiveStyleSheets(StyleEngine&);
  bool IsShadowTreeStyleSheetCollection() const final { return true; }

  void Trace(Visitor* visitor) const override {
    TreeScopeStyleSheetCollection::Trace(visitor);
  }

 private:
  void CollectStyleSheets(StyleEngine&, StyleSheetCollection&);
};

template <>
struct DowncastTraits<ShadowTreeStyleSheetCollection> {
  static bool AllowFrom(const TreeScopeStyleSheetCollection& value) {
    return value.IsShadowTreeStyleSheetCollection();
  }
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_CORE_CSS_SHADOW_TREE_STYLE_SHEET_COLLECTION_H_
