// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/css/property_registry.h"
#include "third_party/blink/renderer/core/css/css_test_helpers.h"
#include "third_party/blink/renderer/core/dom/document.h"
#include "third_party/blink/renderer/core/dom/element.h"
#include "third_party/blink/renderer/core/testing/page_test_base.h"
#include "third_party/blink/renderer/platform/wtf/vector.h"

namespace blink {

class PropertyRegistryTest : public PageTestBase {
 public:
  PropertyRegistry& Registry() {
    return GetDocument().EnsurePropertyRegistry();
  }

  const PropertyRegistration* Registration(AtomicString name) {
    return Registry().Registration(name);
  }

  const PropertyRegistration* RegisterProperty(AtomicString name) {
    auto* registration = css_test_helpers::CreatePropertyRegistration(name);
    Registry().RegisterProperty(name, *registration);
    return registration;
  }

  const PropertyRegistration* DeclareProperty(AtomicString name) {
    auto* registration = css_test_helpers::CreatePropertyRegistration(name);
    Registry().DeclareProperty(name, *registration);
    return registration;
  }

  HeapVector<Member<const PropertyRegistration>> AllRegistrations() {
    HeapVector<Member<const PropertyRegistration>> vector;
    for (auto entry : Registry())
      vector.push_back(entry.value);
    return vector;
  }
};

TEST_F(PropertyRegistryTest, EnsurePropertyRegistry) {
  EXPECT_FALSE(GetDocument().GetPropertyRegistry());
  PropertyRegistry* registry = &GetDocument().EnsurePropertyRegistry();
  EXPECT_EQ(registry, GetDocument().GetPropertyRegistry());
}

TEST_F(PropertyRegistryTest, RegisterProperty) {
  EXPECT_FALSE(Registration("--x"));

  auto* registered = RegisterProperty("--x");
  EXPECT_EQ(registered, Registration("--x"));
}

TEST_F(PropertyRegistryTest, DeclareProperty) {
  EXPECT_FALSE(Registration("--x"));

  auto* declared = DeclareProperty("--x");
  EXPECT_EQ(declared, Registration("--x"));
}

TEST_F(PropertyRegistryTest, DeclareThenRegisterProperty) {
  auto* declared = DeclareProperty("--x");
  EXPECT_EQ(declared, Registration("--x"));

  auto* registered = RegisterProperty("--x");
  EXPECT_EQ(registered, Registration("--x"));
}

TEST_F(PropertyRegistryTest, RegisterThenDeclareProperty) {
  auto* registered = RegisterProperty("--x");
  EXPECT_EQ(registered, Registration("--x"));

  DeclareProperty("--x");
  EXPECT_EQ(registered, Registration("--x"));
}

TEST_F(PropertyRegistryTest, RegisterAndDeclarePropertyNonOverlapping) {
  auto* registered = RegisterProperty("--x");
  EXPECT_EQ(registered, Registration("--x"));

  auto* declared = DeclareProperty("--y");
  EXPECT_EQ(declared, Registration("--y"));
  EXPECT_EQ(registered, Registration("--x"));
}

TEST_F(PropertyRegistryTest, DeclareTwice) {
  auto* declared1 = DeclareProperty("--x");
  EXPECT_EQ(declared1, Registration("--x"));

  auto* declared2 = DeclareProperty("--x");
  EXPECT_EQ(declared2, Registration("--x"));
}

TEST_F(PropertyRegistryTest, IsInRegisteredPropertySet) {
  EXPECT_FALSE(Registry().IsInRegisteredPropertySet("--x"));

  RegisterProperty("--x");
  EXPECT_TRUE(Registry().IsInRegisteredPropertySet("--x"));
  EXPECT_FALSE(Registry().IsInRegisteredPropertySet("--y"));

  DeclareProperty("--y");
  EXPECT_TRUE(Registry().IsInRegisteredPropertySet("--x"));
  EXPECT_FALSE(Registry().IsInRegisteredPropertySet("--y"));

  RegisterProperty("--y");
  EXPECT_TRUE(Registry().IsInRegisteredPropertySet("--y"));
  EXPECT_TRUE(Registry().IsInRegisteredPropertySet("--y"));
}

TEST_F(PropertyRegistryTest, EmptyIterator) {
  EXPECT_EQ(0u, AllRegistrations().size());
}

TEST_F(PropertyRegistryTest, IterateSingleRegistration) {
  auto* reg1 = RegisterProperty("--x");
  auto registrations = AllRegistrations();
  EXPECT_EQ(1u, registrations.size());
  EXPECT_TRUE(registrations.Contains(reg1));
}

TEST_F(PropertyRegistryTest, IterateDoubleRegistration) {
  auto* reg1 = RegisterProperty("--x");
  auto* reg2 = RegisterProperty("--y");

  auto registrations = AllRegistrations();
  EXPECT_EQ(2u, registrations.size());
  EXPECT_TRUE(registrations.Contains(reg1));
  EXPECT_TRUE(registrations.Contains(reg2));
}

TEST_F(PropertyRegistryTest, IterateSingleDeclaration) {
  auto* reg1 = DeclareProperty("--x");
  auto registrations = AllRegistrations();
  EXPECT_EQ(1u, registrations.size());
  EXPECT_TRUE(registrations.Contains(reg1));
}

TEST_F(PropertyRegistryTest, IterateDoubleDeclaration) {
  auto* reg1 = DeclareProperty("--x");
  auto* reg2 = DeclareProperty("--y");

  auto registrations = AllRegistrations();
  EXPECT_EQ(2u, registrations.size());
  EXPECT_TRUE(registrations.Contains(reg1));
  EXPECT_TRUE(registrations.Contains(reg2));
}

TEST_F(PropertyRegistryTest, IterateRegistrationAndDeclaration) {
  auto* reg1 = RegisterProperty("--x");
  auto* reg2 = DeclareProperty("--y");

  auto registrations = AllRegistrations();
  EXPECT_EQ(2u, registrations.size());
  EXPECT_TRUE(registrations.Contains(reg1));
  EXPECT_TRUE(registrations.Contains(reg2));
}

TEST_F(PropertyRegistryTest, IterateRegistrationAndDeclarationConflict) {
  auto* reg1 = RegisterProperty("--x");
  auto* reg2 = RegisterProperty("--y");
  auto* reg3 = DeclareProperty("--y");
  auto* reg4 = DeclareProperty("--z");

  auto registrations = AllRegistrations();
  EXPECT_EQ(3u, registrations.size());
  EXPECT_TRUE(registrations.Contains(reg1));
  EXPECT_TRUE(registrations.Contains(reg2));
  EXPECT_FALSE(registrations.Contains(reg3));
  EXPECT_TRUE(registrations.Contains(reg4));
}

TEST_F(PropertyRegistryTest, IterateFullOverlapSingle) {
  auto* reg1 = DeclareProperty("--x");
  auto* reg2 = RegisterProperty("--x");

  auto registrations = AllRegistrations();
  EXPECT_EQ(1u, registrations.size());
  EXPECT_FALSE(registrations.Contains(reg1));
  EXPECT_TRUE(registrations.Contains(reg2));
}

TEST_F(PropertyRegistryTest, IterateFullOverlapMulti) {
  auto* reg1 = DeclareProperty("--x");
  auto* reg2 = DeclareProperty("--y");
  auto* reg3 = RegisterProperty("--x");
  auto* reg4 = RegisterProperty("--y");

  auto registrations = AllRegistrations();
  EXPECT_EQ(2u, registrations.size());
  EXPECT_FALSE(registrations.Contains(reg1));
  EXPECT_FALSE(registrations.Contains(reg2));
  EXPECT_TRUE(registrations.Contains(reg3));
  EXPECT_TRUE(registrations.Contains(reg4));
}

TEST_F(PropertyRegistryTest, IsEmptyUntilRegisterProperty) {
  EXPECT_TRUE(Registry().IsEmpty());
  RegisterProperty("--x");
  EXPECT_FALSE(Registry().IsEmpty());
}

TEST_F(PropertyRegistryTest, IsEmptyUntilDeclareProperty) {
  EXPECT_TRUE(Registry().IsEmpty());
  DeclareProperty("--x");
  EXPECT_FALSE(Registry().IsEmpty());
}

TEST_F(PropertyRegistryTest, Version) {
  EXPECT_EQ(0u, Registry().Version());

  RegisterProperty("--a");
  EXPECT_EQ(1u, Registry().Version());

  RegisterProperty("--b");
  EXPECT_EQ(2u, Registry().Version());

  DeclareProperty("--c");
  EXPECT_EQ(3u, Registry().Version());

  DeclareProperty("--c");
  EXPECT_EQ(4u, Registry().Version());

  DeclareProperty("--d");
  EXPECT_EQ(5u, Registry().Version());

  Registry().RemoveDeclaredProperties();
  EXPECT_EQ(6u, Registry().Version());

  Registry().RemoveDeclaredProperties();
  EXPECT_EQ(6u, Registry().Version());
}

TEST_F(PropertyRegistryTest, RemoveDeclaredProperties) {
  DeclareProperty("--a");
  DeclareProperty("--b");
  RegisterProperty("--c");
  RegisterProperty("--d");

  EXPECT_TRUE(Registration("--a"));
  EXPECT_TRUE(Registration("--b"));
  EXPECT_TRUE(Registration("--c"));
  EXPECT_TRUE(Registration("--d"));

  Registry().RemoveDeclaredProperties();

  EXPECT_FALSE(Registration("--a"));
  EXPECT_FALSE(Registration("--b"));
  EXPECT_TRUE(Registration("--c"));
  EXPECT_TRUE(Registration("--d"));
}

TEST_F(PropertyRegistryTest, MarkReferencedRegisterProperty) {
  css_test_helpers::RegisterProperty(GetDocument(), "--x", "<length>", "0px",
                                     false);

  UpdateAllLifecyclePhasesForTest();
  EXPECT_FALSE(Registry().WasReferenced("--x"));

  GetDocument().documentElement()->setInnerHTML(R"HTML(
    <style>
      :root {
        --x: 10px;
      }
      div {
        width: var(--x);
      }
    </style>
    <div id="div">Test</div>
  )HTML");

  UpdateAllLifecyclePhasesForTest();
  EXPECT_TRUE(Registry().WasReferenced("--x"));
}

TEST_F(PropertyRegistryTest, MarkReferencedAtProperty) {
  UpdateAllLifecyclePhasesForTest();
  EXPECT_FALSE(Registry().WasReferenced("--x"));

  GetDocument().documentElement()->setInnerHTML(R"HTML(
    <style>
      @property --x {
        syntax: "<length>";
        inherits: false;
        initial-value: 0px;
      }
      :root {
        --x: 10px;
      }
      div {
        width: var(--x);
      }
    </style>
    <div id="div">Test</div>
  )HTML");

  UpdateAllLifecyclePhasesForTest();
  EXPECT_TRUE(Registry().WasReferenced("--x"));

  css_test_helpers::RegisterProperty(GetDocument(), "--x", "<length>", "1px",
                                     false);

  // Check that the registration was successful, and did overwrite the
  // declaration.
  ASSERT_TRUE(Registration("--x"));
  ASSERT_TRUE(Registration("--x")->Initial());
  EXPECT_EQ("1px", Registration("--x")->Initial()->CssText());

  // --x should still be marked as referenced, even though RegisterProperty
  // now takes precedence over @property.
  UpdateAllLifecyclePhasesForTest();
  EXPECT_TRUE(Registry().WasReferenced("--x"));
}

}  // namespace blink
