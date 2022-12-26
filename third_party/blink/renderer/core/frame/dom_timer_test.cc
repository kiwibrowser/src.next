// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/frame/dom_timer.h"

#include "base/test/scoped_command_line.h"
#include "base/test/scoped_feature_list.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/blink/public/common/features.h"
#include "third_party/blink/public/common/switches.h"
#include "third_party/blink/renderer/bindings/core/v8/idl_types.h"
#include "third_party/blink/renderer/bindings/core/v8/native_value_traits_impl.h"
#include "third_party/blink/renderer/bindings/core/v8/script_evaluation_result.h"
#include "third_party/blink/renderer/bindings/core/v8/v8_binding_for_core.h"
#include "third_party/blink/renderer/core/dom/document.h"
#include "third_party/blink/renderer/core/script/classic_script.h"
#include "third_party/blink/renderer/core/testing/core_unit_test_helper.h"
#include "third_party/blink/renderer/core/timing/dom_window_performance.h"

using testing::DoubleNear;
using testing::ElementsAreArray;
using testing::Matcher;

namespace blink {

namespace {

// The resolution of performance.now is 5us, so the threshold for time
// comparison is 6us to account for rounding errors.
const double kThreshold = 0.006;

class DOMTimerTest : public RenderingTest {
 public:
  // Expected time between each iterator for setInterval(..., 1) or nested
  // setTimeout(..., 1) are 1, 1, 1, 1, 4, 4, ... as a minimum clamp of 4ms
  // is applied from the 5th iteration onwards.
  const Vector<Matcher<double>> kExpectedTimings = {
      DoubleNear(1., kThreshold), DoubleNear(1., kThreshold),
      DoubleNear(1., kThreshold), DoubleNear(1., kThreshold),
      DoubleNear(4., kThreshold), DoubleNear(4., kThreshold),
  };

  void SetUp() override {
    EnablePlatform();
    platform()->SetAutoAdvanceNowToPendingTasks(true);
    // Advance timer manually as RenderingTest expects the time to be non-zero.
    platform()->AdvanceClockSeconds(1.);
    RenderingTest::SetUp();
    auto* window_performance =
        DOMWindowPerformance::performance(*GetDocument().domWindow());
    auto test_task_runner = platform()->test_task_runner();
    auto* mock_clock = test_task_runner->GetMockClock();
    auto* mock_tick_clock = test_task_runner->GetMockTickClock();
    auto now_ticks = test_task_runner->NowTicks();
    window_performance->SetTickClockForTesting(mock_tick_clock);
    window_performance->ResetTimeOriginForTesting(now_ticks);
    GetDocument().GetSettings()->SetScriptEnabled(true);
    auto* loader = GetDocument().Loader();
    loader->GetTiming().SetNavigationStart(now_ticks);
    loader->GetTiming().SetClockForTesting(mock_clock);
    loader->GetTiming().SetTickClockForTesting(mock_tick_clock);
  }

  v8::Local<v8::Value> EvalExpression(const char* expr) {
    return ClassicScript::CreateUnspecifiedScript(expr)
        ->RunScriptAndReturnValue(GetDocument().domWindow())
        .GetSuccessValueOrEmpty();
  }

  Vector<double> ToDoubleArray(v8::Local<v8::Value> value,
                               v8::HandleScope& scope) {
    NonThrowableExceptionState exception_state;
    return NativeValueTraits<IDLSequence<IDLDouble>>::NativeValue(
        scope.GetIsolate(), value, exception_state);
  }

  double ToDoubleValue(v8::Local<v8::Value> value, v8::HandleScope& scope) {
    NonThrowableExceptionState exceptionState;
    return ToDouble(scope.GetIsolate(), value, exceptionState);
  }

  void ExecuteScriptAndWaitUntilIdle(const char* script_text) {
    ClassicScript::CreateUnspecifiedScript(String(script_text))
        ->RunScript(GetDocument().domWindow());
    platform()->RunUntilIdle();
  }
};

class DOMTimerTestWithSetTimeoutWithout1MsClampPolicyOverride
    : public DOMTimerTest {
 public:
  DOMTimerTestWithSetTimeoutWithout1MsClampPolicyOverride() = default;

  void SetUp() override {
    DOMTimerTest::SetUp();
    features::ClearSetTimeoutWithout1MsClampPolicyOverrideCacheForTesting();
  }

  void TearDown() override {
    features::ClearSetTimeoutWithout1MsClampPolicyOverrideCacheForTesting();
    DOMTimerTest::TearDown();
  }

  // This should only be called once per test, and prior to the
  // DomTimer logic actually parsing the policy switch.
  void SetPolicyOverride(bool enabled) {
    DCHECK(!scoped_command_line_.GetProcessCommandLine()->HasSwitch(
        switches::kSetTimeoutWithout1MsClampPolicy));
    scoped_command_line_.GetProcessCommandLine()->AppendSwitchASCII(
        switches::kSetTimeoutWithout1MsClampPolicy,
        enabled ? switches::kSetTimeoutWithout1MsClampPolicy_ForceEnable
                : switches::kSetTimeoutWithout1MsClampPolicy_ForceDisable);
  }

 private:
  base::test::ScopedCommandLine scoped_command_line_;
};

TEST_F(DOMTimerTestWithSetTimeoutWithout1MsClampPolicyOverride,
       PolicyForceEnable) {
  SetPolicyOverride(/* enabled = */ true);
  EXPECT_TRUE(blink::features::IsSetTimeoutWithoutClampEnabled());
}

TEST_F(DOMTimerTestWithSetTimeoutWithout1MsClampPolicyOverride,
       PolicyForceDisable) {
  SetPolicyOverride(/* enabled = */ false);
  EXPECT_FALSE(blink::features::IsSetTimeoutWithoutClampEnabled());
}

class DOMTimerTestWithMaxUnthrottledTimeoutNestingLevelPolicyOverride
    : public DOMTimerTest {
 public:
  DOMTimerTestWithMaxUnthrottledTimeoutNestingLevelPolicyOverride() = default;

  void SetUp() override {
    DOMTimerTest::SetUp();
    features::ClearUnthrottledNestedTimeoutOverrideCacheForTesting();
  }

  void TearDown() override {
    features::ClearUnthrottledNestedTimeoutOverrideCacheForTesting();
    DOMTimerTest::TearDown();
  }

  // This should only be called once per test, and prior to the
  // DomTimer logic actually parsing the policy switch.
  void SetPolicyOverride(bool enabled) {
    DCHECK(!scoped_command_line_.GetProcessCommandLine()->HasSwitch(
        switches::kUnthrottledNestedTimeoutPolicy));
    scoped_command_line_.GetProcessCommandLine()->AppendSwitchASCII(
        switches::kUnthrottledNestedTimeoutPolicy,
        enabled ? switches::kUnthrottledNestedTimeoutPolicy_ForceEnable
                : switches::kUnthrottledNestedTimeoutPolicy_ForceDisable);
  }

 private:
  base::test::ScopedCommandLine scoped_command_line_;
};

TEST_F(DOMTimerTestWithMaxUnthrottledTimeoutNestingLevelPolicyOverride,
       PolicyForceEnable) {
  SetPolicyOverride(/* enabled = */ true);
  EXPECT_TRUE(blink::features::IsMaxUnthrottledTimeoutNestingLevelEnabled());
}

TEST_F(DOMTimerTestWithMaxUnthrottledTimeoutNestingLevelPolicyOverride,
       PolicyForceDisable) {
  SetPolicyOverride(/* enabled = */ false);
  EXPECT_FALSE(blink::features::IsMaxUnthrottledTimeoutNestingLevelEnabled());
}

const char* const kSetTimeout0ScriptText =
    "var last = performance.now();"
    "var elapsed;"
    "function setTimeoutCallback() {"
    "  var current = performance.now();"
    "  elapsed = current - last;"
    "}"
    "setTimeout(setTimeoutCallback, 0);";

TEST_F(DOMTimerTest, setTimeout_ZeroIsNotClampedToOne) {
  v8::HandleScope scope(v8::Isolate::GetCurrent());

  base::test::ScopedFeatureList feature_list;
  feature_list.InitAndEnableFeature(features::kSetTimeoutWithoutClamp);

  ExecuteScriptAndWaitUntilIdle(kSetTimeout0ScriptText);

  double time = ToDoubleValue(EvalExpression("elapsed"), scope);

  EXPECT_THAT(time, DoubleNear(0., kThreshold));
}

TEST_F(DOMTimerTest, setTimeout_ZeroIsClampedToOne) {
  v8::HandleScope scope(v8::Isolate::GetCurrent());

  base::test::ScopedFeatureList feature_list;
  feature_list.InitAndDisableFeature(features::kSetTimeoutWithoutClamp);

  ExecuteScriptAndWaitUntilIdle(kSetTimeout0ScriptText);

  double time = ToDoubleValue(EvalExpression("elapsed"), scope);

  EXPECT_THAT(time, DoubleNear(1., kThreshold));
}

const char* const kSetTimeoutNestedScriptText =
    "var last = performance.now();"
    "var times = [];"
    "function nestSetTimeouts() {"
    "  var current = performance.now();"
    "  var elapsed = current - last;"
    "  last = current;"
    "  times.push(elapsed);"
    "  if (times.length < 6) {"
    "    setTimeout(nestSetTimeouts, 1);"
    "  }"
    "}"
    "setTimeout(nestSetTimeouts, 1);";

TEST_F(DOMTimerTest, setTimeout_ClampsAfter4Nestings) {
  v8::HandleScope scope(v8::Isolate::GetCurrent());

  base::test::ScopedFeatureList feature_list;
  feature_list.InitAndDisableFeature(
      features::kMaxUnthrottledTimeoutNestingLevel);

  ExecuteScriptAndWaitUntilIdle(kSetTimeoutNestedScriptText);

  auto times(ToDoubleArray(EvalExpression("times"), scope));

  EXPECT_THAT(times, ElementsAreArray(kExpectedTimings));
}

TEST_F(DOMTimerTest, setTimeout_ClampsAfter5Nestings) {
  v8::HandleScope scope(v8::Isolate::GetCurrent());

  base::test::ScopedFeatureList feature_list;
  feature_list.InitAndEnableFeatureWithParameters(
      features::kMaxUnthrottledTimeoutNestingLevel, {{"nesting", "6"}});

  ExecuteScriptAndWaitUntilIdle(kSetTimeoutNestedScriptText);

  auto times(ToDoubleArray(EvalExpression("times"), scope));

  EXPECT_THAT(times, ElementsAreArray({
                         DoubleNear(1., kThreshold),
                         DoubleNear(1., kThreshold),
                         DoubleNear(1., kThreshold),
                         DoubleNear(1., kThreshold),
                         DoubleNear(1., kThreshold),
                         DoubleNear(4., kThreshold),
                     }));
}

const char* const kSetIntervalScriptText =
    "var last = performance.now();"
    "var times = [];"
    "var id = setInterval(function() {"
    "  var current = performance.now();"
    "  var elapsed = current - last;"
    "  last = current;"
    "  times.push(elapsed);"
    "  if (times.length > 5) {"
    "    clearInterval(id);"
    "  }"
    "}, 1);";

TEST_F(DOMTimerTest, setInterval_ClampsAfter4Iterations) {
  v8::HandleScope scope(v8::Isolate::GetCurrent());

  base::test::ScopedFeatureList feature_list;
  feature_list.InitAndDisableFeature(
      features::kMaxUnthrottledTimeoutNestingLevel);

  ExecuteScriptAndWaitUntilIdle(kSetIntervalScriptText);

  auto times(ToDoubleArray(EvalExpression("times"), scope));

  EXPECT_THAT(times, ElementsAreArray(kExpectedTimings));
}

TEST_F(DOMTimerTest, setInterval_ClampsAfter5Iterations) {
  v8::HandleScope scope(v8::Isolate::GetCurrent());

  base::test::ScopedFeatureList feature_list;
  feature_list.InitAndEnableFeatureWithParameters(
      features::kMaxUnthrottledTimeoutNestingLevel, {{"nesting", "6"}});

  ExecuteScriptAndWaitUntilIdle(kSetIntervalScriptText);

  auto times(ToDoubleArray(EvalExpression("times"), scope));

  EXPECT_THAT(times, ElementsAreArray({
                         DoubleNear(1., kThreshold),
                         DoubleNear(1., kThreshold),
                         DoubleNear(1., kThreshold),
                         DoubleNear(1., kThreshold),
                         DoubleNear(1., kThreshold),
                         DoubleNear(4., kThreshold),
                     }));
}

TEST_F(DOMTimerTest, setInterval_NestingResetsForLaterCalls) {
  v8::HandleScope scope(v8::Isolate::GetCurrent());

  base::test::ScopedFeatureList feature_list;
  feature_list.InitAndDisableFeature(
      features::kMaxUnthrottledTimeoutNestingLevel);

  ExecuteScriptAndWaitUntilIdle(kSetIntervalScriptText);

  // Run the setIntervalScript again to verify that the clamp imposed for
  // nesting beyond 4 levels is reset when setInterval is called again in the
  // original scope but after the original setInterval has completed.
  ExecuteScriptAndWaitUntilIdle(kSetIntervalScriptText);

  auto times(ToDoubleArray(EvalExpression("times"), scope));

  EXPECT_THAT(times, ElementsAreArray(kExpectedTimings));
}

}  // namespace

}  // namespace blink
