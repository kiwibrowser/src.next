// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_CSS_NAVIGATION_QUERY_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_CSS_NAVIGATION_QUERY_H_

#include "third_party/blink/renderer/core/css/conditional_exp_node.h"
#include "third_party/blink/renderer/core/route_matching/navigation_phase.h"
#include "third_party/blink/renderer/core/route_matching/navigation_preposition.h"
#include "third_party/blink/renderer/platform/heap/garbage_collected.h"
#include "third_party/blink/renderer/platform/heap/member.h"
#include "third_party/blink/renderer/platform/wtf/casting.h"
#include "third_party/blink/renderer/platform/wtf/text/atomic_string.h"

namespace blink {

class Document;
class Element;
class Route;

// <route-location>
//
// https://drafts.csswg.org/css-navigation-1/#typedef-route-location
class RouteLocation : public GarbageCollected<RouteLocation> {
 public:
  enum Type {
    kRouteName,
    kUrlPattern,
    kUrl,
  };

  RouteLocation(Type type, const AtomicString& value)
      : type_(type), value_(value) {}

  void Trace(Visitor*) const {}

  Type GetType() const { return type_; }
  const AtomicString& GetValue() const { return value_; }

  // Look for a `Route` entry in the route map. Additionally, if this
  // <route-location> is a URLPattern, an entry will be inserted if it's
  // missing.
  const Route* FindOrCreateRoute(Document&) const;

  bool CheckSelectorMatch(
      const Element&,
      std::optional<NavigationPreposition> = std::nullopt) const;
  void SerializeTo(StringBuilder&) const;

 private:
  Type type_;
  AtomicString value_;
};

// <navigation-test>
//
// https://drafts.csswg.org/css-navigation-1/#typedef-navigation-test
class NavigationTestExpression
    : public GarbageCollected<NavigationTestExpression> {
 public:
  virtual void Trace(Visitor*) const {}

  // TODO(crbug.com/436805487): Do we need this? Only used by unit tests.
  virtual bool IsNavigationLocationTestExpression() const { return false; }

  virtual bool Matches(Document&) const = 0;
  virtual void SerializeTo(StringBuilder&) const = 0;
};

// <navigation-location-test>
//
// https://drafts.csswg.org/css-navigation-1/#typedef-navigation-location-test
class NavigationLocationTestExpression : public NavigationTestExpression {
 public:
  NavigationLocationTestExpression(RouteLocation& location,
                                   NavigationPreposition preposition)
      : route_location_(&location), preposition_(preposition) {}

  void Trace(Visitor* visitor) const override;

  bool IsNavigationLocationTestExpression() const override { return true; }

  RouteLocation& GetLocation() const { return *route_location_; }
  NavigationPreposition GetPreposition() const { return preposition_; }

  bool Matches(Document&) const override;
  void SerializeTo(StringBuilder&) const override;

  static void SerializePrepositionTo(NavigationPreposition, StringBuilder&);

 private:
  Member<RouteLocation> route_location_;
  NavigationPreposition preposition_;
};

// TODO(crbug.com/436805487): Do we need to keep this? Only used by unit tests.
template <>
struct DowncastTraits<NavigationLocationTestExpression> {
  static bool AllowFrom(const NavigationTestExpression& exp) {
    return exp.IsNavigationLocationTestExpression();
  }
};

// <navigation-location-between-test>
//
// https://drafts.csswg.org/css-navigation-1/#typedef-navigation-location-between-test
class NavigationLocationBetweenTestExpression
    : public NavigationTestExpression {
 public:
  NavigationLocationBetweenTestExpression(RouteLocation& location1,
                                          RouteLocation& location2)
      : route_location1_(&location1), route_location2_(location2) {}

  void Trace(Visitor* visitor) const override;

  bool Matches(Document&) const override;
  void SerializeTo(StringBuilder&) const override;

 private:
  Member<RouteLocation> route_location1_;
  Member<RouteLocation> route_location2_;
};

// <navigation-phase-test>
//
// https://drafts.csswg.org/css-navigation-1/#typedef-navigation-phase-test
class NavigationPhaseTestExpression : public NavigationTestExpression {
 public:
  explicit NavigationPhaseTestExpression(NavigationPhase phase)
      : phase_(phase) {}

  bool Matches(Document&) const override;
  void SerializeTo(StringBuilder&) const override;

 private:
  NavigationPhase phase_;
};

// <navigation-type-test>
//
// https://drafts.csswg.org/css-navigation-1/#typedef-navigation-type-test
class NavigationTypeTestExpression : public NavigationTestExpression {
 public:
  // TODO(crbug.com/436805487): Support "reload".
  enum Type { kTraverse, kBack, kForward };

  explicit NavigationTypeTestExpression(Type type) : type_(type) {}

  bool Matches(Document&) const override;
  void SerializeTo(StringBuilder&) const override;

 private:
  Type type_;
};

class NavigationPreviewTestExpression : public NavigationTestExpression {
 public:
  NavigationPreviewTestExpression() = default;

  bool Matches(Document&) const override;
  void SerializeTo(StringBuilder&) const override;
};

class NavigationExpNode : public ConditionalExpNode {
 public:
  explicit NavigationExpNode(NavigationTestExpression& test)
      : navigation_test_(&test) {}

  void Trace(Visitor*) const override;

  const NavigationTestExpression& NavigationTest() const {
    return *navigation_test_;
  }

  KleeneValue Evaluate(ConditionalExpNodeVisitor&) const override;
  void SerializeTo(StringBuilder&) const override;

 private:
  Member<NavigationTestExpression> navigation_test_;
};

class NavigationQuery : public GarbageCollected<NavigationQuery> {
 public:
  explicit NavigationQuery(const ConditionalExpNode& root_exp)
      : root_exp_(&root_exp) {}

  void Trace(Visitor*) const;

  const ConditionalExpNode* GetRootExp() const { return root_exp_; }
  bool Evaluate(Document*) const;

 private:
  Member<const ConditionalExpNode> root_exp_;
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_CORE_CSS_NAVIGATION_QUERY_H_
