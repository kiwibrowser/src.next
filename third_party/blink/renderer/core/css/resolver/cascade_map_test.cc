// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/css/resolver/cascade_map.h"
#include <gtest/gtest.h>
#include "third_party/blink/renderer/core/css/css_property_name.h"
#include "third_party/blink/renderer/core/css/css_property_names.h"
#include "third_party/blink/renderer/core/css/properties/css_property.h"
#include "third_party/blink/renderer/core/css/resolver/cascade_priority.h"

namespace blink {

namespace {
CascadePriority UaPriority(wtf_size_t position) {
  return CascadePriority(CascadeOrigin::kUserAgent, false, 0, false, 0,
                         position);
}
CascadePriority UserPriority(wtf_size_t position) {
  return CascadePriority(CascadeOrigin::kUser, false, 0, false, 0, position);
}
CascadePriority AuthorPriority(wtf_size_t position) {
  return CascadePriority(CascadeOrigin::kAuthor, false, 0, false, 0, position);
}

bool AddTo(CascadeMap& map,
           const CSSPropertyName& name,
           CascadePriority priority) {
  CascadePriority before = map.At(name);
  map.Add(name, priority);
  CascadePriority after = map.At(name);
  return before != after;
}

}  // namespace

TEST(CascadeMapTest, Empty) {
  CascadeMap map;
  EXPECT_FALSE(map.Find(CSSPropertyName(AtomicString("--x"))));
  EXPECT_FALSE(map.Find(CSSPropertyName(AtomicString("--y"))));
  EXPECT_FALSE(map.Find(CSSPropertyName(CSSPropertyID::kColor)));
  EXPECT_FALSE(map.Find(CSSPropertyName(CSSPropertyID::kDisplay)));
}

TEST(CascadeMapTest, AddCustom) {
  CascadeMap map;
  CascadePriority user(CascadeOrigin::kUser);
  CascadePriority author(CascadeOrigin::kAuthor);
  CSSPropertyName x(AtomicString("--x"));
  CSSPropertyName y(AtomicString("--y"));

  EXPECT_TRUE(AddTo(map, x, user));
  EXPECT_TRUE(AddTo(map, x, author));
  EXPECT_FALSE(AddTo(map, x, author));
  ASSERT_TRUE(map.Find(x));
  EXPECT_EQ(author, *map.Find(x));

  EXPECT_FALSE(map.Find(y));
  EXPECT_TRUE(AddTo(map, y, user));

  // --x should be unchanged.
  ASSERT_TRUE(map.Find(x));
  EXPECT_EQ(author, *map.Find(x));

  // --y should exist too.
  ASSERT_TRUE(map.Find(y));
  EXPECT_EQ(user, *map.Find(y));
}

TEST(CascadeMapTest, AddNative) {
  CascadeMap map;
  CascadePriority user(CascadeOrigin::kUser);
  CascadePriority author(CascadeOrigin::kAuthor);
  CSSPropertyName color(CSSPropertyID::kColor);
  CSSPropertyName display(CSSPropertyID::kDisplay);

  EXPECT_TRUE(AddTo(map, color, user));
  EXPECT_TRUE(AddTo(map, color, author));
  EXPECT_FALSE(AddTo(map, color, author));
  ASSERT_TRUE(map.Find(color));
  EXPECT_EQ(author, *map.Find(color));

  EXPECT_FALSE(map.Find(display));
  EXPECT_TRUE(AddTo(map, display, user));

  // color should be unchanged.
  ASSERT_TRUE(map.Find(color));
  EXPECT_EQ(author, *map.Find(color));

  // display should exist too.
  ASSERT_TRUE(map.Find(display));
  EXPECT_EQ(user, *map.Find(display));
}

TEST(CascadeMapTest, FindAndMutateCustom) {
  CascadeMap map;
  CascadePriority user(CascadeOrigin::kUser);
  CascadePriority author(CascadeOrigin::kAuthor);
  CSSPropertyName x(AtomicString("--x"));

  EXPECT_TRUE(AddTo(map, x, user));

  CascadePriority* p = map.Find(x);
  ASSERT_TRUE(p);
  EXPECT_EQ(user, *p);

  *p = author;

  EXPECT_FALSE(AddTo(map, x, author));
  ASSERT_TRUE(map.Find(x));
  EXPECT_EQ(author, *map.Find(x));
}

TEST(CascadeMapTest, FindAndMutateNative) {
  CascadeMap map;
  CascadePriority user(CascadeOrigin::kUser);
  CascadePriority author(CascadeOrigin::kAuthor);
  CSSPropertyName color(CSSPropertyID::kColor);

  EXPECT_TRUE(AddTo(map, color, user));

  CascadePriority* p = map.Find(color);
  ASSERT_TRUE(p);
  EXPECT_EQ(user, *p);

  *p = author;

  EXPECT_FALSE(AddTo(map, color, author));
  ASSERT_TRUE(map.Find(color));
  EXPECT_EQ(author, *map.Find(color));
}

TEST(CascadeMapTest, AtCustom) {
  CascadeMap map;
  CascadePriority user(CascadeOrigin::kUser);
  CascadePriority author(CascadeOrigin::kAuthor);
  CSSPropertyName x(AtomicString("--x"));

  EXPECT_EQ(CascadePriority(), map.At(x));

  EXPECT_TRUE(AddTo(map, x, user));
  EXPECT_EQ(user, map.At(x));

  EXPECT_TRUE(AddTo(map, x, author));
  EXPECT_EQ(author, map.At(x));
}

TEST(CascadeMapTest, AtNative) {
  CascadeMap map;
  CascadePriority user(CascadeOrigin::kUser);
  CascadePriority author(CascadeOrigin::kAuthor);
  CSSPropertyName color(CSSPropertyID::kColor);

  EXPECT_EQ(CascadePriority(), map.At(color));

  EXPECT_TRUE(AddTo(map, color, user));
  EXPECT_EQ(user, map.At(color));

  EXPECT_TRUE(AddTo(map, color, author));
  EXPECT_EQ(author, map.At(color));
}

TEST(CascadeMapTest, HighPriorityBits) {
  CascadeMap map;

  EXPECT_FALSE(map.HighPriorityBits());

  map.Add(CSSPropertyName(CSSPropertyID::kFontSize), CascadeOrigin::kAuthor);
  EXPECT_EQ(map.HighPriorityBits(),
            1ull << static_cast<uint64_t>(CSSPropertyID::kFontSize));

  map.Add(CSSPropertyName(CSSPropertyID::kColor), CascadeOrigin::kAuthor);
  map.Add(CSSPropertyName(CSSPropertyID::kFontSize), CascadeOrigin::kAuthor);
  EXPECT_EQ(map.HighPriorityBits(),
            (1ull << static_cast<uint64_t>(CSSPropertyID::kFontSize)) |
                (1ull << static_cast<uint64_t>(CSSPropertyID::kColor)));
}

TEST(CascadeMapTest, AllHighPriorityBits) {
  CascadeMap map;

  EXPECT_FALSE(map.HighPriorityBits());

  uint64_t expected = 0;
  for (CSSPropertyID id : CSSPropertyIDList()) {
    if (IsHighPriority(id)) {
      if (CSSProperty::Get(id).IsSurrogate())
        continue;
      map.Add(CSSPropertyName(id), CascadeOrigin::kAuthor);
      expected |= (1ull << static_cast<uint64_t>(id));
    }
  }

  EXPECT_EQ(expected, map.HighPriorityBits());
}

TEST(CascadeMapTest, LastHighPrio) {
  CascadeMap map;

  EXPECT_FALSE(map.HighPriorityBits());

  CSSPropertyID last = kLastHighPriorityCSSProperty;

  map.Add(CSSPropertyName(last), CascadeOrigin::kAuthor);
  EXPECT_EQ(map.HighPriorityBits(), 1ull << static_cast<uint64_t>(last));
}

TEST(CascadeMapTest, Reset) {
  CascadeMap map;

  CascadePriority author(CascadeOrigin::kAuthor);

  CSSPropertyName color(CSSPropertyID::kColor);
  CSSPropertyName x(AtomicString("--x"));

  EXPECT_FALSE(map.Find(color));
  EXPECT_FALSE(map.Find(x));

  map.Add(color, author);
  map.Add(x, author);

  EXPECT_EQ(author, map.At(color));
  EXPECT_EQ(author, map.At(x));

  map.Reset();

  EXPECT_FALSE(map.Find(color));
  EXPECT_FALSE(map.Find(x));
}

TEST(CascadeMapTest, ResetHighPrio) {
  CascadeMap map;
  EXPECT_FALSE(map.HighPriorityBits());
  map.Add(CSSPropertyName(CSSPropertyID::kFontSize),
          CascadePriority(CascadeOrigin::kAuthor));
  EXPECT_TRUE(map.HighPriorityBits());
  map.Reset();
  EXPECT_FALSE(map.HighPriorityBits());
}

TEST(CascadeMapTest, FindOrigin) {
  CascadeMap map;

  CSSPropertyName color(CSSPropertyID::kColor);
  CSSPropertyName display(CSSPropertyID::kDisplay);
  CSSPropertyName top(CSSPropertyID::kTop);
  CSSPropertyName left(CSSPropertyID::kLeft);
  CSSPropertyName right(CSSPropertyID::kRight);
  CSSPropertyName bottom(CSSPropertyID::kBottom);

  map.Add(color, UaPriority(1));
  map.Add(display, UaPriority(2));
  map.Add(top, UaPriority(3));
  map.Add(left, UaPriority(4));
  map.Add(right, UaPriority(5));

  map.Add(display, UserPriority(10));
  map.Add(right, UserPriority(11));

  map.Add(color, AuthorPriority(20));
  map.Add(display, AuthorPriority(21));
  map.Add(top, AuthorPriority(22));
  map.Add(bottom, AuthorPriority(23));

  // Final result of the cascade:
  EXPECT_EQ(AuthorPriority(20), *map.Find(color));
  EXPECT_EQ(AuthorPriority(21), *map.Find(display));
  EXPECT_EQ(AuthorPriority(22), *map.Find(top));
  EXPECT_EQ(UaPriority(4), *map.Find(left));
  EXPECT_EQ(UserPriority(11), *map.Find(right));
  EXPECT_EQ(AuthorPriority(23), *map.Find(bottom));

  // Final result up to and including kUser:
  EXPECT_EQ(UaPriority(1), *map.Find(color, CascadeOrigin::kUser));
  EXPECT_EQ(UserPriority(10), *map.Find(display, CascadeOrigin::kUser));
  EXPECT_EQ(UaPriority(3), *map.Find(top, CascadeOrigin::kUser));
  EXPECT_EQ(UaPriority(4), *map.Find(left, CascadeOrigin::kUser));
  EXPECT_EQ(UserPriority(11), *map.Find(right, CascadeOrigin::kUser));
  EXPECT_FALSE(map.Find(bottom, CascadeOrigin::kUser));

  // Final result up to and including kUserAgent:
  EXPECT_EQ(UaPriority(1), *map.Find(color, CascadeOrigin::kUserAgent));
  EXPECT_EQ(UaPriority(2), *map.Find(display, CascadeOrigin::kUserAgent));
  EXPECT_EQ(UaPriority(3), *map.Find(top, CascadeOrigin::kUserAgent));
  EXPECT_EQ(UaPriority(4), *map.Find(left, CascadeOrigin::kUserAgent));
  EXPECT_EQ(UaPriority(5), *map.Find(right, CascadeOrigin::kUserAgent));
  EXPECT_FALSE(map.Find(bottom, CascadeOrigin::kUserAgent));
}

}  // namespace blink
