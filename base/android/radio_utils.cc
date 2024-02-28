// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/android/radio_utils.h"

#include "base/base_jni/RadioUtils_jni.h"
#include "third_party/abseil-cpp/absl/types/optional.h"

namespace base {
namespace android {

namespace {

RadioUtils::OverrideForTesting* g_overrider_for_tests = nullptr;

bool InitializeIsSupported() {
  JNIEnv* env = AttachCurrentThread();
  return Java_RadioUtils_isSupported(env);
}
}  // namespace

RadioUtils::OverrideForTesting::OverrideForTesting() {
  DCHECK(!g_overrider_for_tests);
  g_overrider_for_tests = this;
}

RadioUtils::OverrideForTesting::~OverrideForTesting() {
  DCHECK(g_overrider_for_tests);
  g_overrider_for_tests = nullptr;
}

bool RadioUtils::IsSupported() {
  static const bool kIsSupported = InitializeIsSupported();
  return kIsSupported;
}

RadioConnectionType RadioUtils::GetConnectionType() {
  if (g_overrider_for_tests) {
    // If GetConnectionType is being used in tests
    return g_overrider_for_tests->GetConnectionType();
  }
  if (!IsSupported())
    return RadioConnectionType::kUnknown;

  JNIEnv* env = AttachCurrentThread();
  if (Java_RadioUtils_isWifiConnected(env)) {
    return RadioConnectionType::kWifi;
  } else {
    return RadioConnectionType::kCell;
  }
}

absl::optional<RadioSignalLevel> RadioUtils::GetCellSignalLevel() {
  if (!IsSupported())
    return absl::nullopt;

  JNIEnv* env = AttachCurrentThread();
  int signal_level = Java_RadioUtils_getCellSignalLevel(env);
  if (signal_level < 0) {
    return absl::nullopt;
  } else {
    return static_cast<RadioSignalLevel>(signal_level);
  }
}

absl::optional<RadioDataActivity> RadioUtils::GetCellDataActivity() {
  if (!IsSupported())
    return absl::nullopt;

  JNIEnv* env = AttachCurrentThread();
  return static_cast<RadioDataActivity>(
      Java_RadioUtils_getCellDataActivity(env));
}

}  // namespace android
}  // namespace base
