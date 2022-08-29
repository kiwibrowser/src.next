/*
 * (C) 1999-2003 Lars Knoll (knoll@kde.org)
 * Copyright (C) 2004, 2005, 2006, 2007, 2008 Apple Inc. All rights reserved.
 * Copyright (C) 2013 Intel Corporation. All rights reserved.
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
 */

#include "third_party/blink/renderer/core/style_property_shorthand.h"

namespace blink {

// The transition-property longhand appears last during parsing to prevent it
// from matching against transition-timing-function keywords. Ideally we would
// change the spec to use this order, see:
// https://github.com/w3c/csswg-drafts/issues/4223
const StylePropertyShorthand& transitionShorthandForParsing() {
  static const CSSProperty* kTransitionProperties[] = {
      &GetCSSPropertyTransitionDuration(),
      &GetCSSPropertyTransitionTimingFunction(),
      &GetCSSPropertyTransitionDelay(), &GetCSSPropertyTransitionProperty()};
  static StylePropertyShorthand transition_longhands(
      CSSPropertyID::kTransition, kTransitionProperties,
      std::size(kTransitionProperties));
  return transition_longhands;
}

unsigned indexOfShorthandForLonghand(
    CSSPropertyID shorthand_id,
    const Vector<StylePropertyShorthand, 4>& shorthands) {
  for (unsigned i = 0; i < shorthands.size(); ++i) {
    if (shorthands.at(i).id() == shorthand_id)
      return i;
  }
  NOTREACHED();
  return 0;
}

}  // namespace blink
