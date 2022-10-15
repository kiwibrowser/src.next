// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/css/media_query_evaluator.h"

#include <memory>
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/blink/public/common/css/forced_colors.h"
#include "third_party/blink/public/common/privacy_budget/identifiability_sample_test_utils.h"
#include "third_party/blink/public/common/privacy_budget/identifiability_study_settings.h"
#include "third_party/blink/public/common/privacy_budget/identifiable_surface.h"
#include "third_party/blink/public/common/privacy_budget/identifiable_token.h"
#include "third_party/blink/public/common/privacy_budget/scoped_identifiability_test_sample_collector.h"
#include "third_party/blink/renderer/core/css/media_list.h"
#include "third_party/blink/renderer/core/css/media_values.h"
#include "third_party/blink/renderer/core/css/media_values_cached.h"
#include "third_party/blink/renderer/core/css/parser/css_tokenizer.h"
#include "third_party/blink/renderer/core/css/parser/media_query_parser.h"
#include "third_party/blink/renderer/core/css/resolver/media_query_result.h"
#include "third_party/blink/renderer/core/dom/document.h"
#include "third_party/blink/renderer/core/frame/local_frame.h"
#include "third_party/blink/renderer/core/frame/local_frame_view.h"
#include "third_party/blink/renderer/core/frame/settings.h"
#include "third_party/blink/renderer/core/html/html_element.h"
#include "third_party/blink/renderer/core/media_type_names.h"
#include "third_party/blink/renderer/core/testing/dummy_page_holder.h"
#include "third_party/blink/renderer/core/testing/page_test_base.h"
#include "third_party/blink/renderer/platform/testing/runtime_enabled_features_test_helpers.h"
#include "third_party/blink/renderer/platform/wtf/text/string_builder.h"
#include "ui/gfx/display_color_spaces.h"

namespace blink {

namespace {

MediaQueryExpValue PxValue(double value) {
  return MediaQueryExpValue(value, CSSPrimitiveValue::UnitType::kPixels);
}

MediaQueryExpValue RatioValue(unsigned numerator, unsigned denominator) {
  return MediaQueryExpValue(numerator, denominator);
}

}  // namespace

struct MediaQueryEvaluatorTestCase {
  const char* input;
  const bool output;
};

MediaQueryEvaluatorTestCase g_screen_test_cases[] = {
    {"", true},
    {" ", true},
    {"screen", true},
    {"screen and (color)", true},
    {"not screen and (color)", false},
    {"screen and (device-aspect-ratio: 16/9)", false},
    {"screen and (device-aspect-ratio: 1/1)", true},
    {"all and (min-color: 2)", true},
    {"all and (min-color: 32)", false},
    {"all and (min-color-index: 0)", true},
    {"all and (min-color-index: 1)", false},
    {"all and (monochrome)", false},
    {"all and (min-monochrome: 0)", true},
    {"all and (grid: 0)", true},
    {"(resolution: 2dppx)", true},
    {"(resolution: 1dppx)", false},
    {"(orientation: portrait)", true},
    {"(orientation: landscape)", false},
    {"(orientation: url(portrait))", false},
    {"(orientation: #portrait)", false},
    {"(orientation: @portrait)", false},
    {"(orientation: 'portrait')", false},
    {"(orientation: @junk portrait)", false},
    {"screen and (orientation: @portrait) and (max-width: 1000px)", false},
    {"screen and (orientation: @portrait), (max-width: 1000px)", true},
    {"tv and (scan: progressive)", false},
    {"(pointer: coarse)", false},
    {"(pointer: fine)", true},
    {"(hover: hover)", true},
    {"(hover: on-demand)", false},
    {"(hover: none)", false},
    {"(display-mode)", true},
    {"(display-mode: fullscreen)", false},
    {"(display-mode: standalone)", false},
    {"(display-mode: minimal-ui)", false},
    {"(display-mode: window-controls-overlay)", false},
    {"(display-mode: borderless)", false},
    {"(display-mode: browser)", true},
    {"(display-mode: min-browser)", false},
    {"(display-mode: url(browser))", false},
    {"(display-mode: #browser)", false},
    {"(display-mode: @browser)", false},
    {"(display-mode: 'browser')", false},
    {"(display-mode: @junk browser)", false},
    {"(max-device-aspect-ratio: 4294967295/1)", true},
    {"(min-device-aspect-ratio: 1/4294967296)", true},
    {nullptr, false}  // Do not remove the terminator line.
};

MediaQueryEvaluatorTestCase g_monochrome_test_cases[] = {
    {"(color)", false},
    {"(monochrome)", true},
    {nullptr, false}  // Do not remove the terminator line.
};

MediaQueryEvaluatorTestCase g_viewport_test_cases[] = {
    {"all and (min-width: 500px)", true},
    {"(min-width: 500px)", true},
    {"(min-width: 501px)", false},
    {"(max-width: 500px)", true},
    {"(max-width: 499px)", false},
    {"(width: 500px)", true},
    {"(width: 501px)", false},
    {"(min-height: 500px)", true},
    {"(min-height: 501px)", false},
    {"(min-height: 500.02px)", false},
    {"(max-height: 500px)", true},
    {"(max-height: calc(500px))", true},
    {"(max-height: 499.98px)", false},
    {"(max-height: 499px)", false},
    {"(height: 500px)", true},
    {"(height: calc(500px))", true},
    {"(height: 500.001px)", true},
    {"(height: 499.999px)", true},
    {"(height: 500.02px)", false},
    {"(height: 499.98px)", false},
    {"(height: 501px)", false},
    {"(height)", true},
    {"(width)", true},
    {"(width: whatisthis)", false},
    {"screen and (min-width: 400px) and (max-width: 700px)", true},
    {"(max-aspect-ratio: 4294967296/1)", true},
    {"(max-aspect-ratio: calc(4294967296) / calc(1)", true},
    {"(min-aspect-ratio: 1/4294967295)", true},
    {nullptr, false}  // Do not remove the terminator line.
};

MediaQueryEvaluatorTestCase g_float_viewport_test_cases[] = {
    {"all and (min-width: 600.5px)", true},
    {"(min-width: 600px)", true},
    {"(min-width: 600.5px)", true},
    {"(min-width: 601px)", false},
    {"(max-width: 600px)", false},
    {"(max-width: 600.5px)", true},
    {"(max-width: 601px)", true},
    {"(width: 600.5px)", true},
    {"(width: 601px)", false},
    {"(min-height: 700px)", true},
    {"(min-height: 700.125px)", true},
    {"(min-height: 701px)", false},
    {"(min-height: 700.141px)", false},
    {"(max-height: 701px)", true},
    {"(max-height: 700.125px)", true},
    {"(max-height: 700px)", false},
    {"(height: 700.125px)", true},
    {"(height: 700.141px)", false},
    {"(height: 700.109px)", false},
    {"(height: 701px)", false},
    {nullptr, false}  // Do not remove the terminator line.
};

MediaQueryEvaluatorTestCase g_float_non_friendly_viewport_test_cases[] = {
    {"(min-width: 821px)", true},
    {"(max-width: 821px)", true},
    {"(width: 821px)", true},
    {"(min-height: 821px)", true},
    {"(max-height: 821px)", true},
    {"(height: 821px)", true},
    {"(width: 100vw)", true},
    {"(height: 100vh)", true},
    {nullptr, false}  // Do not remove the terminator line.
};

MediaQueryEvaluatorTestCase g_print_test_cases[] = {
    {"print and (min-resolution: 1dppx)", true},
    {"print and (min-resolution: 118dpcm)", true},
    {"print and (min-resolution: 119dpcm)", false},
    {nullptr, false}  // Do not remove the terminator line.
};

MediaQueryEvaluatorTestCase g_non_immersive_test_cases[] = {
    {"(immersive: 1)", false},
    {"(immersive: 0)", true},
    {nullptr, false}  // Do not remove the terminator line.
};

MediaQueryEvaluatorTestCase g_immersive_test_cases[] = {
    {"(immersive: 1)", true},
    {"(immersive: 0)", false},
    {nullptr, false}  // Do not remove the terminator line.
};

MediaQueryEvaluatorTestCase g_non_ua_sheet_immersive_test_cases[] = {
    {"(immersive: 1)", false},
    {"(immersive: 0)", false},
    {nullptr, false}  // Do not remove the terminator line.
};

MediaQueryEvaluatorTestCase g_forcedcolors_active_cases[] = {
    {"(forced-colors: active)", true},
    {"(forced-colors: none)", false},
    {nullptr, false}  // Do not remove the terminator line.
};

MediaQueryEvaluatorTestCase g_forcedcolors_none_cases[] = {
    {"(forced-colors: active)", false},
    {"(forced-colors: none)", true},
    {nullptr, false}  // Do not remove the terminator line.
};

MediaQueryEvaluatorTestCase g_preferscontrast_nopreference_cases[] = {
    {"(prefers-contrast)", false},
    {"(prefers-contrast: more)", false},
    {"(prefers-contrast: less)", false},
    {"(prefers-contrast: no-preference)", true},
    {"(prefers-contrast: custom)", false},
    {nullptr, false}  // Do not remove the terminator line.
};

MediaQueryEvaluatorTestCase g_preferscontrast_more_cases[] = {
    {"(prefers-contrast)", true},
    {"(prefers-contrast: more)", true},
    {"(prefers-contrast: less)", false},
    {"(prefers-contrast: no-preference)", false},
    {"(prefers-contrast: custom)", false},
    {nullptr, false}  // Do not remove the terminator line.
};

MediaQueryEvaluatorTestCase g_preferscontrast_less_cases[] = {
    {"(prefers-contrast)", true},
    {"(prefers-contrast: more)", false},
    {"(prefers-contrast: less)", true},
    {"(prefers-contrast: no-preference)", false},
    {"(prefers-contrast: custom)", false},
    {nullptr, false}  // Do not remove the terminator line.
};

MediaQueryEvaluatorTestCase g_preferscontrast_custom_cases[] = {
    {"(prefers-contrast)", true},
    {"(prefers-contrast: more)", false},
    {"(prefers-contrast: less)", false},
    {"(prefers-contrast: no-preference)", false},
    {"(prefers-contrast: custom)", true},
    {nullptr, false}  // Do not remove the terminator line.
};

MediaQueryEvaluatorTestCase g_navigationcontrols_back_button_cases[] = {
    {"(navigation-controls: back-button)", true},
    {"(navigation-controls: none)", false},
    {nullptr, false}  // Do not remove the terminator line.
};

MediaQueryEvaluatorTestCase g_navigationcontrols_none_cases[] = {
    {"(navigation-controls: back-button)", false},
    {"(navigation-controls: none)", true},
    {nullptr, false}  // Do not remove the terminator line.
};

MediaQueryEvaluatorTestCase g_single_horizontal_viewport_segment_cases[] = {
    {"(horizontal-viewport-segments)", true},
    {"(horizontal-viewport-segments: 1)", true},
    {"(horizontal-viewport-segments > 1)", false},
    {"(horizontal-viewport-segments: 2)", false},
    {"(horizontal-viewport-segments: none)", false},
    {"(horizontal-viewport-segments: 1px)", false},
    {"(horizontal-viewport-segments: 16/9)", false},
    {nullptr, false}  // Do not remove the terminator line.
};

MediaQueryEvaluatorTestCase g_double_horizontal_viewport_segment_cases[] = {
    {"(horizontal-viewport-segments)", true},
    {"(horizontal-viewport-segments: 1)", false},
    {"(horizontal-viewport-segments: 2)", true},
    {"(horizontal-viewport-segments: 3)", false},
    {nullptr, false}  // Do not remove the terminator line.
};

MediaQueryEvaluatorTestCase g_single_vertical_viewport_segment_cases[] = {
    {"(vertical-viewport-segments)", true},
    {"(vertical-viewport-segments: 1)", true},
    {"(vertical-viewport-segments: 2)", false},
    {"(vertical-viewport-segments: none)", false},
    {"(vertical-viewport-segments: 1px)", false},
    {"(vertical-viewport-segments: 16/9)", false},
    {nullptr, false}  // Do not remove the terminator line.
};

MediaQueryEvaluatorTestCase g_double_vertical_viewport_segment_cases[] = {
    {"(vertical-viewport-segments)", true},
    {"(vertical-viewport-segments: 1)", false},
    {"(vertical-viewport-segments: 2)", true},
    {"(vertical-viewport-segments: 3)", false},
    {nullptr, false}  // Do not remove the terminator line.
};

MediaQueryEvaluatorTestCase g_device_posture_none_cases[] = {
    {"(device-posture)", true},
    {"(device-posture: continuous)", true},
    {"(device-posture: folded)", false},
    {"(device-posture: folded-over)", false},
    {"(device-posture: 15)", false},
    {"(device-posture: 2px)", false},
    {"(device-posture: 16/9)", false},
    {nullptr, false}  // Do not remove the terminator line.
};

MediaQueryEvaluatorTestCase g_device_posture_folded_cases[] = {
    {"(device-posture)", true},
    {"(device-posture: continuous)", false},
    {"(device-posture: folded)", true},
    {"(device-posture: folded-over)", false},
    {nullptr, false}  // Do not remove the terminator line.
};

MediaQueryEvaluatorTestCase g_device_posture_folded_over_cases[] = {
    {"(device-posture)", true},
    {"(device-posture: continuous)", false},
    {"(device-posture: folded)", false},
    {"(device-posture: folded-over)", true},
    {nullptr, false}  // Do not remove the terminator line.
};

MediaQueryEvaluatorTestCase g_dynamic_range_standard_cases[] = {
    {"(dynamic-range: standard)", true},
    {"(dynamic-range: high)", false},
    {"(dynamic-range: invalid)", false},
    {nullptr, false}  // Do not remove the terminator line.
};

MediaQueryEvaluatorTestCase g_dynamic_range_high_cases[] = {
    {"(dynamic-range: standard)", true},
    {"(dynamic-range: high)", true},
    {"(dynamic-range: invalid)", false},
    {nullptr, false}  // Do not remove the terminator line.
};

MediaQueryEvaluatorTestCase g_dynamic_range_feature_disabled_cases[] = {
    {"(dynamic-range: standard)", false},
    {"(dynamic-range: high)", false},
    {"(dynamic-range: invalid)", false},
    {nullptr, false}  // Do not remove the terminator line.
};

MediaQueryEvaluatorTestCase g_video_dynamic_range_standard_cases[] = {
    {"(video-dynamic-range: standard)", true},
    {"(video-dynamic-range: high)", false},
    {"(video-dynamic-range: invalid)", false},
    {nullptr, false}  // Do not remove the terminator line.
};

MediaQueryEvaluatorTestCase g_video_dynamic_range_high_cases[] = {
    {"(video-dynamic-range: standard)", true},
    {"(video-dynamic-range: high)", true},
    {"(video-dynamic-range: invalid)", false},
    {nullptr, false}  // Do not remove the terminator line.
};

MediaQueryEvaluatorTestCase g_video_dynamic_range_feature_disabled_cases[] = {
    {"(video-dynamic-range: standard)", false},
    {"(video-dynamic-range: high)", false},
    {"(video-dynamic-range: invalid)", false},
    {nullptr, false}  // Do not remove the terminator line.
};

void TestMQEvaluator(MediaQueryEvaluatorTestCase* test_cases,
                     const MediaQueryEvaluator& media_query_evaluator,
                     CSSParserMode mode) {
  MediaQuerySet* query_set = nullptr;
  for (unsigned i = 0; test_cases[i].input; ++i) {
    if (String(test_cases[i].input).IsEmpty()) {
      query_set = MediaQuerySet::Create();
    } else {
      query_set = MediaQueryParser::ParseMediaQuerySetInMode(
          CSSParserTokenRange(
              CSSTokenizer(test_cases[i].input).TokenizeToEOF()),
          mode, nullptr);
    }
    EXPECT_EQ(test_cases[i].output, media_query_evaluator.Eval(*query_set))
        << "Query: " << test_cases[i].input;
  }
}

void TestMQEvaluator(MediaQueryEvaluatorTestCase* test_cases,
                     const MediaQueryEvaluator& media_query_evaluator) {
  TestMQEvaluator(test_cases, media_query_evaluator, kHTMLStandardMode);
}

TEST(MediaQueryEvaluatorTest, Cached) {
  MediaValuesCached::MediaValuesCachedData data;
  data.viewport_width = 500;
  data.viewport_height = 500;
  data.device_width = 500;
  data.device_height = 500;
  data.device_pixel_ratio = 2.0;
  data.color_bits_per_component = 24;
  data.monochrome_bits_per_component = 0;
  data.primary_pointer_type = mojom::blink::PointerType::kPointerFineType;
  data.primary_hover_type = mojom::blink::HoverType::kHoverHoverType;
  data.three_d_enabled = true;
  data.media_type = media_type_names::kScreen;
  data.strict_mode = true;
  data.display_mode = blink::mojom::DisplayMode::kBrowser;
  data.immersive_mode = false;

  // Default values.
  {
    auto* media_values = MakeGarbageCollected<MediaValuesCached>(data);
    MediaQueryEvaluator media_query_evaluator(media_values);
    TestMQEvaluator(g_screen_test_cases, media_query_evaluator);
    TestMQEvaluator(g_viewport_test_cases, media_query_evaluator);
    TestMQEvaluator(g_non_immersive_test_cases, media_query_evaluator,
                    kUASheetMode);
    TestMQEvaluator(g_non_ua_sheet_immersive_test_cases, media_query_evaluator);
  }

  // Print values.
  {
    data.media_type = media_type_names::kPrint;
    auto* media_values = MakeGarbageCollected<MediaValuesCached>(data);
    MediaQueryEvaluator media_query_evaluator(media_values);
    TestMQEvaluator(g_print_test_cases, media_query_evaluator);
    data.media_type = media_type_names::kScreen;
  }

  // Monochrome values.
  {
    data.color_bits_per_component = 0;
    data.monochrome_bits_per_component = 8;
    auto* media_values = MakeGarbageCollected<MediaValuesCached>(data);
    MediaQueryEvaluator media_query_evaluator(media_values);
    TestMQEvaluator(g_monochrome_test_cases, media_query_evaluator);
    data.color_bits_per_component = 24;
    data.monochrome_bits_per_component = 0;
  }

  // Immersive values.
  {
    data.immersive_mode = true;
    auto* media_values = MakeGarbageCollected<MediaValuesCached>(data);
    MediaQueryEvaluator media_query_evaluator(media_values);
    TestMQEvaluator(g_immersive_test_cases, media_query_evaluator,
                    kUASheetMode);
    TestMQEvaluator(g_non_ua_sheet_immersive_test_cases, media_query_evaluator);
    data.immersive_mode = false;
  }
}

TEST(MediaQueryEvaluatorTest, Dynamic) {
  auto page_holder = std::make_unique<DummyPageHolder>(gfx::Size(500, 500));
  page_holder->GetFrameView().SetMediaType(media_type_names::kScreen);

  MediaQueryEvaluator media_query_evaluator(&page_holder->GetFrame());
  TestMQEvaluator(g_viewport_test_cases, media_query_evaluator);
  page_holder->GetFrameView().SetMediaType(media_type_names::kPrint);
  TestMQEvaluator(g_print_test_cases, media_query_evaluator);
}

TEST(MediaQueryEvaluatorTest, DynamicNoView) {
  auto page_holder = std::make_unique<DummyPageHolder>(gfx::Size(500, 500));
  LocalFrame* frame = &page_holder->GetFrame();
  page_holder.reset();
  ASSERT_EQ(nullptr, frame->View());
  MediaQueryEvaluator media_query_evaluator(frame);
  MediaQuerySet* query_set = MediaQuerySet::Create("foobar", nullptr);
  EXPECT_FALSE(media_query_evaluator.Eval(*query_set));
}

TEST(MediaQueryEvaluatorTest, CachedFloatViewport) {
  MediaValuesCached::MediaValuesCachedData data;
  data.viewport_width = 600.5;
  data.viewport_height = 700.125;
  auto* media_values = MakeGarbageCollected<MediaValuesCached>(data);

  MediaQueryEvaluator media_query_evaluator(media_values);
  TestMQEvaluator(g_float_viewport_test_cases, media_query_evaluator);
}

TEST(MediaQueryEvaluatorTest, CachedFloatViewportNonFloatFriendly) {
  MediaValuesCached::MediaValuesCachedData data;
  data.viewport_width = 821;
  data.viewport_height = 821;
  auto* media_values = MakeGarbageCollected<MediaValuesCached>(data);

  MediaQueryEvaluator media_query_evaluator(media_values);
  TestMQEvaluator(g_float_non_friendly_viewport_test_cases,
                  media_query_evaluator);
}

TEST(MediaQueryEvaluatorTest, DynamicImmersive) {
  auto page_holder = std::make_unique<DummyPageHolder>(gfx::Size(500, 500));
  page_holder->GetFrameView().SetMediaType(media_type_names::kScreen);

  MediaQueryEvaluator media_query_evaluator(&page_holder->GetFrame());
  page_holder->GetDocument().GetSettings()->SetImmersiveModeEnabled(false);

  TestMQEvaluator(g_non_immersive_test_cases, media_query_evaluator,
                  kUASheetMode);
  page_holder->GetDocument().GetSettings()->SetImmersiveModeEnabled(true);
  TestMQEvaluator(g_immersive_test_cases, media_query_evaluator, kUASheetMode);
}

TEST(MediaQueryEvaluatorTest, CachedForcedColors) {
  ScopedForcedColorsForTest scoped_feature(true);

  MediaValuesCached::MediaValuesCachedData data;

  // Forced colors - none.
  {
    data.forced_colors = ForcedColors::kNone;
    MediaValues* media_values = MakeGarbageCollected<MediaValuesCached>(data);
    MediaQueryEvaluator media_query_evaluator(media_values);
    TestMQEvaluator(g_forcedcolors_none_cases, media_query_evaluator);
  }

  // Forced colors - active.
  {
    data.forced_colors = ForcedColors::kActive;
    MediaValues* media_values = MakeGarbageCollected<MediaValuesCached>(data);
    MediaQueryEvaluator media_query_evaluator(media_values);
    TestMQEvaluator(g_forcedcolors_active_cases, media_query_evaluator);
  }
}

TEST(MediaQueryEvaluatorTest, CachedPrefersContrast) {
  ScopedForcedColorsForTest forced_scoped_feature(true);

  MediaValuesCached::MediaValuesCachedData data;
  data.forced_colors = ForcedColors::kNone;

  // Prefers-contrast - no-preference.
  {
    data.preferred_contrast = mojom::blink::PreferredContrast::kNoPreference;
    MediaValues* media_values = MakeGarbageCollected<MediaValuesCached>(data);
    MediaQueryEvaluator media_query_evaluator(media_values);
    TestMQEvaluator(g_preferscontrast_nopreference_cases,
                    media_query_evaluator);
  }

  // Prefers-contrast - more.
  {
    data.preferred_contrast = mojom::blink::PreferredContrast::kMore;
    MediaValues* media_values = MakeGarbageCollected<MediaValuesCached>(data);
    MediaQueryEvaluator media_query_evaluator(media_values);
    TestMQEvaluator(g_preferscontrast_more_cases, media_query_evaluator);
  }

  // Prefers-contrast - less.
  {
    data.preferred_contrast = mojom::blink::PreferredContrast::kLess;
    MediaValues* media_values = MakeGarbageCollected<MediaValuesCached>(data);
    MediaQueryEvaluator media_query_evaluator(media_values);
    TestMQEvaluator(g_preferscontrast_less_cases, media_query_evaluator);
  }

  // Prefers-contrast - custom.
  {
    data.preferred_contrast = mojom::blink::PreferredContrast::kCustom;
    MediaValues* media_values = MakeGarbageCollected<MediaValuesCached>(data);
    MediaQueryEvaluator media_query_evaluator(media_values);
    TestMQEvaluator(g_preferscontrast_custom_cases, media_query_evaluator);
  }
}

TEST(MediaQueryEvaluatorTest, CachedViewportSegments) {
  ScopedCSSFoldablesForTest scoped_feature(true);

  MediaValuesCached::MediaValuesCachedData data;
  {
    data.horizontal_viewport_segments = 1;
    MediaValues* media_values = MakeGarbageCollected<MediaValuesCached>(data);

    MediaQueryEvaluator media_query_evaluator(media_values);
    TestMQEvaluator(g_single_horizontal_viewport_segment_cases,
                    media_query_evaluator);
  }

  {
    data.horizontal_viewport_segments = 2;
    MediaValues* media_values = MakeGarbageCollected<MediaValuesCached>(data);
    MediaQueryEvaluator media_query_evaluator(media_values);
    TestMQEvaluator(g_double_horizontal_viewport_segment_cases,
                    media_query_evaluator);
  }

  {
    data.vertical_viewport_segments = 1;
    MediaValues* media_values = MakeGarbageCollected<MediaValuesCached>(data);

    MediaQueryEvaluator media_query_evaluator(media_values);
    TestMQEvaluator(g_single_vertical_viewport_segment_cases,
                    media_query_evaluator);
  }

  {
    data.vertical_viewport_segments = 2;
    MediaValues* media_values = MakeGarbageCollected<MediaValuesCached>(data);
    MediaQueryEvaluator media_query_evaluator(media_values);
    TestMQEvaluator(g_double_vertical_viewport_segment_cases,
                    media_query_evaluator);
  }
}

TEST(MediaQueryEvaluatorTest, CachedDevicePosture) {
  ScopedDevicePostureForTest scoped_feature(true);

  MediaValuesCached::MediaValuesCachedData data;
  {
    data.device_posture = device::mojom::blink::DevicePostureType::kContinuous;
    MediaValues* media_values = MakeGarbageCollected<MediaValuesCached>(data);
    MediaQueryEvaluator media_query_evaluator(media_values);
    TestMQEvaluator(g_device_posture_none_cases, media_query_evaluator);
  }

  {
    data.device_posture = device::mojom::blink::DevicePostureType::kFolded;
    MediaValues* media_values = MakeGarbageCollected<MediaValuesCached>(data);
    MediaQueryEvaluator media_query_evaluator(media_values);
    TestMQEvaluator(g_device_posture_folded_cases, media_query_evaluator);
  }

  {
    data.device_posture = device::mojom::blink::DevicePostureType::kFoldedOver;
    MediaValues* media_values = MakeGarbageCollected<MediaValuesCached>(data);
    MediaQueryEvaluator media_query_evaluator(media_values);
    TestMQEvaluator(g_device_posture_folded_over_cases, media_query_evaluator);
  }
}

TEST(MediaQueryEvaluatorTest, CachedDynamicRange) {
  MediaValuesCached::MediaValuesCachedData data;

  // Test with color spaces supporting standard dynamic range
  {
    data.device_supports_hdr = gfx::DisplayColorSpaces().SupportsHDR();
    MediaValues* media_values = MakeGarbageCollected<MediaValuesCached>(data);
    MediaQueryEvaluator media_query_evaluator(media_values);
    TestMQEvaluator(g_dynamic_range_standard_cases, media_query_evaluator);
    TestMQEvaluator(g_video_dynamic_range_standard_cases,
                    media_query_evaluator);

    // Test again with the feature disabled
    ScopedCSSVideoDynamicRangeMediaQueriesForTest const disable_video_feature{
        false};
    TestMQEvaluator(g_video_dynamic_range_feature_disabled_cases,
                    media_query_evaluator);
  }
  {
    data.device_supports_hdr =
        gfx::DisplayColorSpaces(gfx::ColorSpace::CreateDisplayP3D65())
            .SupportsHDR();
    MediaValues* media_values = MakeGarbageCollected<MediaValuesCached>(data);
    MediaQueryEvaluator media_query_evaluator(media_values);
    TestMQEvaluator(g_dynamic_range_standard_cases, media_query_evaluator);
    TestMQEvaluator(g_video_dynamic_range_standard_cases,
                    media_query_evaluator);

    // Test again with the feature disabled
    ScopedCSSVideoDynamicRangeMediaQueriesForTest const disable_video_feature{
        false};
    TestMQEvaluator(g_video_dynamic_range_feature_disabled_cases,
                    media_query_evaluator);
  }

  // Test with color spaces supporting high dynamic range
  {
    data.device_supports_hdr =
        gfx::DisplayColorSpaces(gfx::ColorSpace::CreateExtendedSRGB())
            .SupportsHDR();
    MediaValues* media_values = MakeGarbageCollected<MediaValuesCached>(data);
    MediaQueryEvaluator media_query_evaluator(media_values);
    TestMQEvaluator(g_dynamic_range_high_cases, media_query_evaluator);
    TestMQEvaluator(g_video_dynamic_range_high_cases, media_query_evaluator);

    // Test again with the feature disabled
    ScopedCSSVideoDynamicRangeMediaQueriesForTest const disable_video_feature{
        false};
    TestMQEvaluator(g_video_dynamic_range_feature_disabled_cases,
                    media_query_evaluator);
  }
  {
    data.device_supports_hdr =
        gfx::DisplayColorSpaces(gfx::ColorSpace::CreateSRGBLinear())
            .SupportsHDR();
    MediaValues* media_values = MakeGarbageCollected<MediaValuesCached>(data);
    MediaQueryEvaluator media_query_evaluator(media_values);
    TestMQEvaluator(g_dynamic_range_high_cases, media_query_evaluator);
    TestMQEvaluator(g_video_dynamic_range_high_cases, media_query_evaluator);

    // Test again with the feature disabled
    ScopedCSSVideoDynamicRangeMediaQueriesForTest const disable_video_feature{
        false};
    TestMQEvaluator(g_video_dynamic_range_feature_disabled_cases,
                    media_query_evaluator);
  }
  {
    data.device_supports_hdr =
        gfx::DisplayColorSpaces(gfx::ColorSpace::CreateHDR10()).SupportsHDR();
    MediaValues* media_values = MakeGarbageCollected<MediaValuesCached>(data);
    MediaQueryEvaluator media_query_evaluator(media_values);
    TestMQEvaluator(g_dynamic_range_high_cases, media_query_evaluator);
    TestMQEvaluator(g_video_dynamic_range_high_cases, media_query_evaluator);

    // Test again with the feature disabled
    ScopedCSSVideoDynamicRangeMediaQueriesForTest const disable_video_feature{
        false};
    TestMQEvaluator(g_video_dynamic_range_feature_disabled_cases,
                    media_query_evaluator);
  }
  {
    data.device_supports_hdr =
        gfx::DisplayColorSpaces(gfx::ColorSpace::CreateHLG()).SupportsHDR();
    MediaValues* media_values = MakeGarbageCollected<MediaValuesCached>(data);
    MediaQueryEvaluator media_query_evaluator(media_values);
    TestMQEvaluator(g_dynamic_range_high_cases, media_query_evaluator);
    TestMQEvaluator(g_video_dynamic_range_high_cases, media_query_evaluator);

    // Test again with the feature disabled
    ScopedCSSVideoDynamicRangeMediaQueriesForTest const disable_video_feature{
        false};
    TestMQEvaluator(g_video_dynamic_range_feature_disabled_cases,
                    media_query_evaluator);
  }
}

TEST(MediaQueryEvaluatorTest, RangedValues) {
  MediaValuesCached::MediaValuesCachedData data;
  data.viewport_width = 500;
  data.viewport_height = 250;

  auto* media_values = MakeGarbageCollected<MediaValuesCached>(data);
  MediaQueryEvaluator media_query_evaluator(media_values);

  auto eval = [&media_query_evaluator](MediaQueryExp exp) {
    const auto* feature = MakeGarbageCollected<MediaQueryFeatureExpNode>(exp);
    return media_query_evaluator.Eval(*feature) == KleeneValue::kTrue;
  };

  // (width < 600px)
  EXPECT_TRUE(eval(MediaQueryExp::Create(
      "width", MediaQueryExpBounds(MediaQueryExpComparison(
                   PxValue(600), MediaQueryOperator::kLt)))));

  // (width < 501px)
  EXPECT_TRUE(eval(MediaQueryExp::Create(
      "width", MediaQueryExpBounds(MediaQueryExpComparison(
                   PxValue(501), MediaQueryOperator::kLt)))));

  // (width < 500px)
  EXPECT_FALSE(eval(MediaQueryExp::Create(
      "width", MediaQueryExpBounds(MediaQueryExpComparison(
                   PxValue(500), MediaQueryOperator::kLt)))));

  // (width > 500px)
  EXPECT_FALSE(eval(MediaQueryExp::Create(
      "width", MediaQueryExpBounds(MediaQueryExpComparison(
                   PxValue(500), MediaQueryOperator::kGt)))));

  // (width < 501px)
  EXPECT_TRUE(eval(MediaQueryExp::Create(
      "width", MediaQueryExpBounds(MediaQueryExpComparison(
                   PxValue(501), MediaQueryOperator::kLt)))));

  // (width <= 500px)
  EXPECT_TRUE(eval(MediaQueryExp::Create(
      "width", MediaQueryExpBounds(MediaQueryExpComparison(
                   PxValue(500), MediaQueryOperator::kLe)))));

  // (400px < width)
  EXPECT_TRUE(eval(MediaQueryExp::Create(
      "width", MediaQueryExpBounds(MediaQueryExpComparison(
                                       PxValue(400), MediaQueryOperator::kLt),
                                   MediaQueryExpComparison()))));

  // (600px < width)
  EXPECT_FALSE(eval(MediaQueryExp::Create(
      "width", MediaQueryExpBounds(MediaQueryExpComparison(
                                       PxValue(600), MediaQueryOperator::kLt),
                                   MediaQueryExpComparison()))));

  // (400px > width)
  EXPECT_FALSE(eval(MediaQueryExp::Create(
      "width", MediaQueryExpBounds(MediaQueryExpComparison(
                                       PxValue(400), MediaQueryOperator::kGt),
                                   MediaQueryExpComparison()))));

  // (600px > width)
  EXPECT_TRUE(eval(MediaQueryExp::Create(
      "width", MediaQueryExpBounds(MediaQueryExpComparison(
                                       PxValue(600), MediaQueryOperator::kGt),
                                   MediaQueryExpComparison()))));

  // (400px <= width)
  EXPECT_TRUE(eval(MediaQueryExp::Create(
      "width", MediaQueryExpBounds(MediaQueryExpComparison(
                                       PxValue(400), MediaQueryOperator::kLe),
                                   MediaQueryExpComparison()))));

  // (600px <= width)
  EXPECT_FALSE(eval(MediaQueryExp::Create(
      "width", MediaQueryExpBounds(MediaQueryExpComparison(
                                       PxValue(600), MediaQueryOperator::kLe),
                                   MediaQueryExpComparison()))));

  // (400px >= width)
  EXPECT_FALSE(eval(MediaQueryExp::Create(
      "width", MediaQueryExpBounds(MediaQueryExpComparison(
                                       PxValue(400), MediaQueryOperator::kGe),
                                   MediaQueryExpComparison()))));

  // (600px >= width)
  EXPECT_TRUE(eval(MediaQueryExp::Create(
      "width", MediaQueryExpBounds(MediaQueryExpComparison(
                                       PxValue(600), MediaQueryOperator::kGe),
                                   MediaQueryExpComparison()))));

  // (width = 500px)
  EXPECT_TRUE(eval(MediaQueryExp::Create(
      "width", MediaQueryExpBounds(MediaQueryExpComparison(
                   PxValue(500), MediaQueryOperator::kEq)))));

  // (width = 400px)
  EXPECT_FALSE(eval(MediaQueryExp::Create(
      "width", MediaQueryExpBounds(MediaQueryExpComparison(
                   PxValue(400), MediaQueryOperator::kEq)))));

  // (500px = width)
  EXPECT_TRUE(eval(MediaQueryExp::Create(
      "width", MediaQueryExpBounds(MediaQueryExpComparison(
                                       PxValue(500), MediaQueryOperator::kEq),
                                   MediaQueryExpComparison()))));

  // (400px = width)
  EXPECT_FALSE(eval(MediaQueryExp::Create(
      "width", MediaQueryExpBounds(MediaQueryExpComparison(
                                       PxValue(400), MediaQueryOperator::kEq),
                                   MediaQueryExpComparison()))));

  // (400px < width < 600px)
  EXPECT_TRUE(eval(MediaQueryExp::Create(
      "width",
      MediaQueryExpBounds(
          MediaQueryExpComparison(PxValue(400), MediaQueryOperator::kLt),
          MediaQueryExpComparison(PxValue(600), MediaQueryOperator::kLt)))));

  // (550px < width < 600px)
  EXPECT_FALSE(eval(MediaQueryExp::Create(
      "width",
      MediaQueryExpBounds(
          MediaQueryExpComparison(PxValue(550), MediaQueryOperator::kLt),
          MediaQueryExpComparison(PxValue(600), MediaQueryOperator::kLt)))));

  // (400px < width < 450px)
  EXPECT_FALSE(eval(MediaQueryExp::Create(
      "width",
      MediaQueryExpBounds(
          MediaQueryExpComparison(PxValue(400), MediaQueryOperator::kLt),
          MediaQueryExpComparison(PxValue(450), MediaQueryOperator::kLt)))));

  // (aspect-ratio = 2/1)
  EXPECT_TRUE(eval(MediaQueryExp::Create(
      "aspect-ratio", MediaQueryExpBounds(MediaQueryExpComparison(
                          RatioValue(2, 1), MediaQueryOperator::kEq)))));

  // (aspect-ratio = 3/1)
  EXPECT_FALSE(eval(MediaQueryExp::Create(
      "aspect-ratio", MediaQueryExpBounds(MediaQueryExpComparison(
                          RatioValue(3, 1), MediaQueryOperator::kEq)))));

  // (aspect-ratio < 1/1)
  EXPECT_FALSE(eval(MediaQueryExp::Create(
      "aspect-ratio", MediaQueryExpBounds(MediaQueryExpComparison(
                          RatioValue(1, 1), MediaQueryOperator::kLt)))));

  // (aspect-ratio < 3/1)
  EXPECT_TRUE(eval(MediaQueryExp::Create(
      "aspect-ratio", MediaQueryExpBounds(MediaQueryExpComparison(
                          RatioValue(3, 1), MediaQueryOperator::kLt)))));

  // (aspect-ratio > 1/1)
  EXPECT_TRUE(eval(MediaQueryExp::Create(
      "aspect-ratio", MediaQueryExpBounds(MediaQueryExpComparison(
                          RatioValue(1, 1), MediaQueryOperator::kGt)))));

  // (aspect-ratio > 3/1)
  EXPECT_FALSE(eval(MediaQueryExp::Create(
      "aspect-ratio", MediaQueryExpBounds(MediaQueryExpComparison(
                          RatioValue(3, 1), MediaQueryOperator::kGt)))));
}

TEST(MediaQueryEvaluatorTest, ExpNode) {
  MediaValuesCached::MediaValuesCachedData data;
  data.viewport_width = 500;

  auto* media_values = MakeGarbageCollected<MediaValuesCached>(data);
  MediaQueryEvaluator media_query_evaluator(media_values);

  auto* width_lt_400 =
      MakeGarbageCollected<MediaQueryFeatureExpNode>(MediaQueryExp::Create(
          "width", MediaQueryExpBounds(MediaQueryExpComparison(
                       PxValue(400), MediaQueryOperator::kLt))));
  auto* width_lt_600 =
      MakeGarbageCollected<MediaQueryFeatureExpNode>(MediaQueryExp::Create(
          "width", MediaQueryExpBounds(MediaQueryExpComparison(
                       PxValue(600), MediaQueryOperator::kLt))));
  auto* width_lt_800 =
      MakeGarbageCollected<MediaQueryFeatureExpNode>(MediaQueryExp::Create(
          "width", MediaQueryExpBounds(MediaQueryExpComparison(
                       PxValue(800), MediaQueryOperator::kLt))));

  EXPECT_EQ(KleeneValue::kTrue, media_query_evaluator.Eval(*width_lt_600));
  EXPECT_EQ(KleeneValue::kFalse, media_query_evaluator.Eval(*width_lt_400));

  EXPECT_EQ(KleeneValue::kTrue,
            media_query_evaluator.Eval(
                *MakeGarbageCollected<MediaQueryNestedExpNode>(width_lt_600)));
  EXPECT_EQ(KleeneValue::kFalse,
            media_query_evaluator.Eval(
                *MakeGarbageCollected<MediaQueryNestedExpNode>(width_lt_400)));

  EXPECT_EQ(KleeneValue::kFalse,
            media_query_evaluator.Eval(
                *MakeGarbageCollected<MediaQueryNotExpNode>(width_lt_600)));
  EXPECT_EQ(KleeneValue::kTrue,
            media_query_evaluator.Eval(
                *MakeGarbageCollected<MediaQueryNotExpNode>(width_lt_400)));

  EXPECT_EQ(KleeneValue::kTrue, media_query_evaluator.Eval(
                                    *MakeGarbageCollected<MediaQueryAndExpNode>(
                                        width_lt_600, width_lt_800)));
  EXPECT_EQ(
      KleeneValue::kFalse,
      media_query_evaluator.Eval(*MakeGarbageCollected<MediaQueryAndExpNode>(
          width_lt_600, width_lt_400)));

  EXPECT_EQ(KleeneValue::kTrue, media_query_evaluator.Eval(
                                    *MakeGarbageCollected<MediaQueryOrExpNode>(
                                        width_lt_600, width_lt_400)));
  EXPECT_EQ(
      KleeneValue::kFalse,
      media_query_evaluator.Eval(*MakeGarbageCollected<MediaQueryOrExpNode>(
          width_lt_400,
          MakeGarbageCollected<MediaQueryNotExpNode>(width_lt_800))));
}

TEST(MediaQueryEvaluatorTest, DependentResults) {
  MediaValuesCached::MediaValuesCachedData data;
  data.viewport_width = 300;
  data.device_width = 400;

  auto* media_values = MakeGarbageCollected<MediaValuesCached>(data);
  MediaQueryEvaluator media_query_evaluator(media_values);

  // Viewport-dependent:
  auto* width_lt_400 =
      MakeGarbageCollected<MediaQueryFeatureExpNode>(MediaQueryExp::Create(
          "width", MediaQueryExpBounds(MediaQueryExpComparison(
                       PxValue(400), MediaQueryOperator::kLt))));

  // Device-dependent:
  auto* device_width_lt_600 =
      MakeGarbageCollected<MediaQueryFeatureExpNode>(MediaQueryExp::Create(
          "device-width", MediaQueryExpBounds(MediaQueryExpComparison(
                              PxValue(600), MediaQueryOperator::kLt))));

  // Neither viewport- nor device-dependent:
  auto* color =
      MakeGarbageCollected<MediaQueryFeatureExpNode>(MediaQueryExp::Create(
          "color",
          MediaQueryExpBounds(MediaQueryExpComparison(MediaQueryExpValue()))));

  // "(color)" should not be dependent on anything.
  {
    MediaQueryResultFlags result_flags;

    media_query_evaluator.Eval(*color, &result_flags);

    EXPECT_FALSE(result_flags.is_viewport_dependent);
    EXPECT_FALSE(result_flags.is_device_dependent);
  }

  // "(width < 400px)" should be viewport-dependent.
  {
    MediaQueryResultFlags result_flags;

    media_query_evaluator.Eval(*width_lt_400, &result_flags);

    EXPECT_TRUE(result_flags.is_viewport_dependent);
    EXPECT_FALSE(result_flags.is_device_dependent);
  }

  // "(device-width < 600px)" should be device-dependent.
  {
    MediaQueryResultFlags result_flags;

    media_query_evaluator.Eval(*device_width_lt_600, &result_flags);

    EXPECT_TRUE(result_flags.is_device_dependent);
    EXPECT_FALSE(result_flags.is_viewport_dependent);
  }

  // "((device-width < 600px))" should be device-dependent.
  {
    MediaQueryResultFlags result_flags;

    media_query_evaluator.Eval(
        *MakeGarbageCollected<MediaQueryNestedExpNode>(device_width_lt_600),
        &result_flags);

    EXPECT_FALSE(result_flags.is_viewport_dependent);
    EXPECT_TRUE(result_flags.is_device_dependent);
  }

  // "not (device-width < 600px)" should be device-dependent.
  {
    MediaQueryResultFlags result_flags;

    media_query_evaluator.Eval(
        *MakeGarbageCollected<MediaQueryNotExpNode>(device_width_lt_600),
        &result_flags);

    EXPECT_FALSE(result_flags.is_viewport_dependent);
    EXPECT_TRUE(result_flags.is_device_dependent);
  }

  // "(width < 400px) and (device-width < 600px)" should be both viewport- and
  // device-dependent.
  {
    MediaQueryResultFlags result_flags;

    media_query_evaluator.Eval(*MakeGarbageCollected<MediaQueryAndExpNode>(
                                   width_lt_400, device_width_lt_600),
                               &result_flags);

    EXPECT_TRUE(result_flags.is_viewport_dependent);
    EXPECT_TRUE(result_flags.is_device_dependent);
  }

  // "not (width < 400px) and (device-width < 600px)" should be
  // viewport-dependent only.
  //
  // Note that the evaluation short-circuits on the first condition, making the
  // the second condition irrelevant.
  {
    MediaQueryResultFlags result_flags;

    media_query_evaluator.Eval(
        *MakeGarbageCollected<MediaQueryAndExpNode>(
            MakeGarbageCollected<MediaQueryNotExpNode>(width_lt_400),
            device_width_lt_600),
        &result_flags);

    EXPECT_TRUE(result_flags.is_viewport_dependent);
    EXPECT_FALSE(result_flags.is_device_dependent);
  }

  // "(width < 400px) or (device-width < 600px)" should be viewport-dependent
  // only.
  //
  // Note that the evaluation short-circuits on the first condition, making the
  // the second condition irrelevant.
  {
    MediaQueryResultFlags result_flags;

    media_query_evaluator.Eval(*MakeGarbageCollected<MediaQueryOrExpNode>(
                                   width_lt_400, device_width_lt_600),
                               &result_flags);

    EXPECT_TRUE(result_flags.is_viewport_dependent);
    EXPECT_FALSE(result_flags.is_device_dependent);
  }

  // "not (width < 400px) or (device-width < 600px)" should be both viewport-
  //  and device-dependent.
  {
    MediaQueryResultFlags result_flags;

    media_query_evaluator.Eval(
        *MakeGarbageCollected<MediaQueryOrExpNode>(
            MakeGarbageCollected<MediaQueryNotExpNode>(width_lt_400),
            device_width_lt_600),
        &result_flags);

    EXPECT_TRUE(result_flags.is_viewport_dependent);
    EXPECT_TRUE(result_flags.is_device_dependent);
  }
}

TEST(MediaQueryEvaluatorTest, CSSMediaQueries4) {
  ScopedCSSMediaQueries4ForTest media_queries_4_flag(true);

  MediaValuesCached::MediaValuesCachedData data;
  data.viewport_width = 500;
  data.viewport_height = 500;
  auto* media_values = MakeGarbageCollected<MediaValuesCached>(data);
  MediaQueryEvaluator media_query_evaluator(media_values);

  MediaQueryEvaluatorTestCase test_cases[] = {
      {"(width: 1px) or (width: 2px)", false},
      {"(width: 1px) or (width: 2px) or (width: 3px)", false},
      {"(width: 500px) or (width: 2px) or (width: 3px)", true},
      {"(width: 1px) or (width: 500px) or (width: 3px)", true},
      {"(width: 1px) or (width: 2px) or (width: 500px)", true},
      {"((width: 1px))", false},
      {"((width: 500px))", true},
      {"(((width: 500px)))", true},
      {"((width: 1px) or (width: 2px)) or (width: 3px)", false},
      {"(width: 1px) or ((width: 2px) or (width: 500px))", true},
      {"(width = 500px)", true},
      {"(width >= 500px)", true},
      {"(width <= 500px)", true},
      {"(width < 500px)", false},
      {"(500px = width)", true},
      {"(500px >= width)", true},
      {"(500px <= width)", true},
      {"(499px < width)", true},
      {"(499px > width)", false},
      {"(499px < width < 501px)", true},
      {"(499px < width <= 500px)", true},
      {"(499px < width < 500px)", false},
      {"(500px < width < 501px)", false},
      {"(501px > width > 499px)", true},
      {"(500px >= width > 499px)", true},
      {"(501px > width >= 500px)", true},
      {"(502px > width >= 501px)", false},
      {"not (499px > width)", true},
      {"(not (499px > width))", true},
      {"(width >= 500px) and (not (499px > width))", true},
      {"(width >= 500px) and ((499px > width) or (not (width = 500px)))",
       false},
      {nullptr, false}  // Do not remove the terminator line.
  };

  TestMQEvaluator(test_cases, media_query_evaluator);
}

TEST(MediaQueryEvaluatorTest, GeneralEnclosed) {
  MediaValuesCached::MediaValuesCachedData data;
  data.viewport_width = 500;
  data.viewport_height = 500;

  auto* media_values = MakeGarbageCollected<MediaValuesCached>(data);
  MediaQueryEvaluator media_query_evaluator(media_values);

  MediaQueryEvaluatorTestCase tests[] = {
      {"(unknown)", false},
      {"((unknown: 1px))", false},
      {"not (unknown: 1px)", false},
      {"(width) or (unknown: 1px)", true},
      {"(unknown: 1px) or (width)", true},
      {"not ((width) or (unknown: 1px))", false},
      {"not ((unknown: 1px) or (width))", false},
      {"(width) and (unknown: 1px)", false},
      {"(unknown: 1px) and (width)", false},
      {"not ((width) and (unknown: 1px))", false},
      {"not ((unknown: 1px) and (width))", false},
  };

  // Run the same tests twice (CSSMediaQueries4 on/off).
  Vector<bool> flag_values = {true, false};
  for (bool flag : flag_values) {
    ScopedCSSMediaQueries4ForTest media_queries_4_flag(flag);

    for (const MediaQueryEvaluatorTestCase& test : tests) {
      SCOPED_TRACE(String(test.input));
      String input(test.input);
      MediaQuerySet* query_set =
          MediaQueryParser::ParseMediaQuerySet(input, nullptr);
      ASSERT_TRUE(query_set);
      // Always expect `false` with CSSMediaQueries4 disabled, otherwise
      // expect `test.output`.
      EXPECT_EQ(flag && test.output, media_query_evaluator.Eval(*query_set));
    }
  }
}

class MediaQueryEvaluatorIdentifiabilityTest : public PageTestBase {
 public:
  MediaQueryEvaluatorIdentifiabilityTest() {
    CallCounts counts{.response_for_is_active = true,
                      .response_for_is_anything_blocked = false,
                      .response_for_is_allowed = true};

    IdentifiabilityStudySettings::SetGlobalProvider(
        std::make_unique<CountingSettingsProvider>(&counts));
  }
  ~MediaQueryEvaluatorIdentifiabilityTest() override {
    IdentifiabilityStudySettings::ResetStateForTesting();
  }

  test::ScopedIdentifiabilityTestSampleCollector* collector() {
    return &collector_;
  }

 protected:
  test::ScopedIdentifiabilityTestSampleCollector collector_;
  void UpdateAllLifecyclePhases() {
    GetDocument().View()->UpdateAllLifecyclePhasesForTest();
  }
};

TEST_F(MediaQueryEvaluatorIdentifiabilityTest,
       MediaFeatureIdentifiableSurfacePrefersReducedMotion) {
  GetDocument().body()->setInnerHTML(R"HTML(
    <style>
      @media (prefers-reduced-motion: reduce) {
        div { color: green }
      }
    </style>
    <div id="green"></div>
    <span></span>
  )HTML");

  UpdateAllLifecyclePhases();
  EXPECT_TRUE(GetDocument().WasMediaFeatureEvaluated(static_cast<int>(
      IdentifiableSurface::MediaFeatureName::kPrefersReducedMotion)));
  EXPECT_EQ(collector()->entries().size(), 1u);

  auto& entry = collector()->entries().front();
  EXPECT_EQ(entry.metrics.size(), 1u);
  EXPECT_EQ(
      entry.metrics.begin()->surface,
      IdentifiableSurface::FromTypeAndToken(
          IdentifiableSurface::Type::kMediaFeature,
          IdentifiableToken(
              IdentifiableSurface::MediaFeatureName::kPrefersReducedMotion)));
  EXPECT_EQ(entry.metrics.begin()->value, IdentifiableToken(false));
}

TEST_F(MediaQueryEvaluatorIdentifiabilityTest,
       MediaFeatureIdentifiableSurfaceOrientation) {
  GetDocument().body()->setInnerHTML(R"HTML(
    <style>
      @media (orientation: landscape) {
        div { color: green }
      }
    </style>
    <div id="green"></div>
    <span></span>
  )HTML");

  UpdateAllLifecyclePhases();
  EXPECT_TRUE(GetDocument().WasMediaFeatureEvaluated(
      static_cast<int>(IdentifiableSurface::MediaFeatureName::kOrientation)));
  EXPECT_EQ(collector()->entries().size(), 1u);

  auto& entry = collector()->entries().front();
  EXPECT_EQ(entry.metrics.size(), 1u);
  EXPECT_EQ(entry.metrics.begin()->surface,
            IdentifiableSurface::FromTypeAndToken(
                IdentifiableSurface::Type::kMediaFeature,
                IdentifiableToken(
                    IdentifiableSurface::MediaFeatureName::kOrientation)));
  EXPECT_EQ(entry.metrics.begin()->value,
            IdentifiableToken(CSSValueID::kLandscape));
}

TEST_F(MediaQueryEvaluatorIdentifiabilityTest,
       MediaFeatureIdentifiableSurfaceCollectOnce) {
  GetDocument().body()->setInnerHTML(R"HTML(
    <style>
      @media (orientation: landscape) {
        div { color: green }
      }
    </style>
    <div id="green"></div>
    <span></span>
  )HTML");

  // Recompute layout twice but expect only one sample.
  UpdateAllLifecyclePhases();
  UpdateAllLifecyclePhases();
  EXPECT_TRUE(GetDocument().WasMediaFeatureEvaluated(
      static_cast<int>(IdentifiableSurface::MediaFeatureName::kOrientation)));
  EXPECT_EQ(collector()->entries().size(), 1u);

  auto& entry = collector()->entries().front();
  EXPECT_EQ(entry.metrics.size(), 1u);
  EXPECT_EQ(entry.metrics.begin()->surface,
            IdentifiableSurface::FromTypeAndToken(
                IdentifiableSurface::Type::kMediaFeature,
                IdentifiableToken(
                    IdentifiableSurface::MediaFeatureName::kOrientation)));
  EXPECT_EQ(entry.metrics.begin()->value,
            IdentifiableToken(CSSValueID::kLandscape));
}

TEST_F(MediaQueryEvaluatorIdentifiabilityTest,
       MediaFeatureIdentifiableSurfaceDisplayMode) {
  GetDocument().body()->setInnerHTML(R"HTML(
    <style>
      @media all and (display-mode: browser) {
        div { color: green }
      }
    </style>
    <div id="green"></div>
    <span></span>
  )HTML");

  UpdateAllLifecyclePhases();
  EXPECT_TRUE(GetDocument().WasMediaFeatureEvaluated(
      static_cast<int>(IdentifiableSurface::MediaFeatureName::kDisplayMode)));
  EXPECT_EQ(collector()->entries().size(), 1u);

  auto& entry = collector()->entries().front();
  EXPECT_EQ(entry.metrics.size(), 1u);
  EXPECT_EQ(entry.metrics.begin()->surface,
            IdentifiableSurface::FromTypeAndToken(
                IdentifiableSurface::Type::kMediaFeature,
                IdentifiableToken(
                    IdentifiableSurface::MediaFeatureName::kDisplayMode)));
  EXPECT_EQ(entry.metrics.begin()->value,
            IdentifiableToken(blink::mojom::DisplayMode::kBrowser));
}

TEST_F(MediaQueryEvaluatorIdentifiabilityTest,
       MediaFeatureIdentifiableSurfaceForcedColorsHover) {
  GetDocument().body()->setInnerHTML(R"HTML(
    <style>
      @media all and (forced-colors: active) {
        div { color: green }
      }
    </style>
    <style>
      @media all and (hover: hover) {
        div { color: red }
      }
    </style>
    <div id="green"></div>
    <span></span>
  )HTML");

  UpdateAllLifecyclePhases();
  EXPECT_TRUE(GetDocument().WasMediaFeatureEvaluated(
      static_cast<int>(IdentifiableSurface::MediaFeatureName::kForcedColors)));
  EXPECT_TRUE(GetDocument().WasMediaFeatureEvaluated(
      static_cast<int>(IdentifiableSurface::MediaFeatureName::kHover)));
  EXPECT_EQ(collector()->entries().size(), 2u);

  auto& entry_forced_colors = collector()->entries().front();
  EXPECT_EQ(entry_forced_colors.metrics.size(), 1u);
  EXPECT_EQ(entry_forced_colors.metrics.begin()->surface,
            IdentifiableSurface::FromTypeAndToken(
                IdentifiableSurface::Type::kMediaFeature,
                IdentifiableToken(
                    IdentifiableSurface::MediaFeatureName::kForcedColors)));
  EXPECT_EQ(entry_forced_colors.metrics.begin()->value,
            IdentifiableToken(ForcedColors::kNone));

  auto& entry_hover = collector()->entries().back();
  EXPECT_EQ(entry_hover.metrics.size(), 1u);
  EXPECT_EQ(
      entry_hover.metrics.begin()->surface,
      IdentifiableSurface::FromTypeAndToken(
          IdentifiableSurface::Type::kMediaFeature,
          IdentifiableToken(IdentifiableSurface::MediaFeatureName::kHover)));
  EXPECT_EQ(entry_hover.metrics.begin()->value,
            IdentifiableToken(mojom::blink::HoverType::kHoverNone));
}

TEST_F(MediaQueryEvaluatorIdentifiabilityTest,
       MediaFeatureIdentifiableSurfaceAspectRatioNormalized) {
  GetDocument().body()->setInnerHTML(R"HTML(
    <style>
      @media all and (min-aspect-ratio: 8/5) {
        div { color: green }
      }
    </style>
    <div id="green"></div>
    <span></span>
  )HTML");

  UpdateAllLifecyclePhases();
  EXPECT_TRUE(GetDocument().WasMediaFeatureEvaluated(static_cast<int>(
      IdentifiableSurface::MediaFeatureName::kAspectRatioNormalized)));
  EXPECT_EQ(collector()->entries().size(), 1u);

  auto& entry = collector()->entries().front();
  EXPECT_EQ(entry.metrics.size(), 1u);
  EXPECT_EQ(
      entry.metrics.begin()->surface,
      IdentifiableSurface::FromTypeAndToken(
          IdentifiableSurface::Type::kMediaFeature,
          IdentifiableToken(
              IdentifiableSurface::MediaFeatureName::kAspectRatioNormalized)));
}

TEST_F(MediaQueryEvaluatorIdentifiabilityTest,
       MediaFeatureIdentifiableSurfaceResolution) {
  GetDocument().body()->setInnerHTML(R"HTML(
    <style>
      @media all and (min-resolution: 72dpi) {
        div { color: green }
      }
    </style>
    <div id="green"></div>
    <span></span>
  )HTML");

  UpdateAllLifecyclePhases();
  EXPECT_TRUE(GetDocument().WasMediaFeatureEvaluated(
      static_cast<int>(IdentifiableSurface::MediaFeatureName::kResolution)));
  EXPECT_EQ(collector()->entries().size(), 1u);

  auto& entry = collector()->entries().front();
  EXPECT_EQ(entry.metrics.size(), 1u);
  EXPECT_EQ(entry.metrics.begin()->surface,
            IdentifiableSurface::FromTypeAndToken(
                IdentifiableSurface::Type::kMediaFeature,
                IdentifiableToken(
                    IdentifiableSurface::MediaFeatureName::kResolution)));
}

}  // namespace blink
