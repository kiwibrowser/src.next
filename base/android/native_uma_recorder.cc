// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/android/callback_android.h"
#include "base/android/jni_android.h"
#include "base/android/jni_string.h"
#include "base/base_jni_headers/NativeUmaRecorder_jni.h"
#include "base/format_macros.h"
#include "base/lazy_instance.h"
#include "base/metrics/histogram.h"
#include "base/metrics/histogram_base.h"
#include "base/metrics/sparse_histogram.h"
#include "base/metrics/statistics_recorder.h"
#include "base/metrics/user_metrics.h"
#include "base/strings/stringprintf.h"
#include "base/synchronization/lock.h"
#include "base/time/time.h"

namespace base {
namespace android {

namespace {

using HistogramsSnapshot =
    std::map<std::string, std::unique_ptr<HistogramSamples>>;

// Simple thread-safe wrapper for caching histograms. This avoids
// relatively expensive JNI string translation for each recording.
class HistogramCache {
 public:
  HistogramCache() {}

  HistogramCache(const HistogramCache&) = delete;
  HistogramCache& operator=(const HistogramCache&) = delete;

  std::string HistogramConstructionParamsToString(HistogramBase* histogram) {
    std::string params_str = histogram->histogram_name();
    switch (histogram->GetHistogramType()) {
      case HISTOGRAM:
      case LINEAR_HISTOGRAM:
      case BOOLEAN_HISTOGRAM:
      case CUSTOM_HISTOGRAM: {
        Histogram* hist = static_cast<Histogram*>(histogram);
        params_str += StringPrintf("/%d/%d/%" PRIuS, hist->declared_min(),
                                   hist->declared_max(), hist->bucket_count());
        break;
      }
      case SPARSE_HISTOGRAM:
      case DUMMY_HISTOGRAM:
        break;
    }
    return params_str;
  }

  void CheckHistogramArgs(JNIEnv* env,
                          jstring j_histogram_name,
                          int32_t expected_min,
                          int32_t expected_max,
                          size_t expected_bucket_count,
                          HistogramBase* histogram) {
    std::string histogram_name = ConvertJavaStringToUTF8(env, j_histogram_name);
    bool valid_arguments = Histogram::InspectConstructionArguments(
        histogram_name, &expected_min, &expected_max, &expected_bucket_count);
    DCHECK(valid_arguments);
    DCHECK(histogram->HasConstructionArguments(expected_min, expected_max,
                                               expected_bucket_count))
        << histogram_name << "/" << expected_min << "/" << expected_max << "/"
        << expected_bucket_count << " vs. "
        << HistogramConstructionParamsToString(histogram);
  }

  HistogramBase* BooleanHistogram(JNIEnv* env,
                                  jstring j_histogram_name,
                                  jlong j_histogram_hint) {
    DCHECK(j_histogram_name);
    HistogramBase* histogram = HistogramFromHint(j_histogram_hint);
    if (histogram)
      return histogram;

    std::string histogram_name = ConvertJavaStringToUTF8(env, j_histogram_name);
    histogram = BooleanHistogram::FactoryGet(
        histogram_name, HistogramBase::kUmaTargetedHistogramFlag);
    return histogram;
  }

  HistogramBase* ExponentialHistogram(JNIEnv* env,
                                      jstring j_histogram_name,
                                      jlong j_histogram_hint,
                                      jint j_min,
                                      jint j_max,
                                      jint j_num_buckets) {
    DCHECK(j_histogram_name);
    int32_t min = static_cast<int32_t>(j_min);
    int32_t max = static_cast<int32_t>(j_max);
    size_t num_buckets = static_cast<size_t>(j_num_buckets);
    HistogramBase* histogram = HistogramFromHint(j_histogram_hint);
    if (histogram) {
      CheckHistogramArgs(env, j_histogram_name, min, max, num_buckets,
                         histogram);
      return histogram;
    }

    DCHECK_GE(min, 1) << "The min expected sample must be >= 1";

    std::string histogram_name = ConvertJavaStringToUTF8(env, j_histogram_name);
    histogram = Histogram::FactoryGet(histogram_name, min, max, num_buckets,
                                      HistogramBase::kUmaTargetedHistogramFlag);
    return histogram;
  }

  HistogramBase* LinearHistogram(JNIEnv* env,
                                 jstring j_histogram_name,
                                 jlong j_histogram_hint,
                                 jint j_min,
                                 jint j_max,
                                 jint j_num_buckets) {
    DCHECK(j_histogram_name);
    int32_t min = static_cast<int32_t>(j_min);
    int32_t max = static_cast<int32_t>(j_max);
    size_t num_buckets = static_cast<size_t>(j_num_buckets);
    HistogramBase* histogram = HistogramFromHint(j_histogram_hint);
    if (histogram) {
      CheckHistogramArgs(env, j_histogram_name, min, max, num_buckets,
                         histogram);
      return histogram;
    }

    std::string histogram_name = ConvertJavaStringToUTF8(env, j_histogram_name);
    histogram =
        LinearHistogram::FactoryGet(histogram_name, min, max, num_buckets,
                                    HistogramBase::kUmaTargetedHistogramFlag);
    return histogram;
  }

  HistogramBase* SparseHistogram(JNIEnv* env,
                                 jstring j_histogram_name,
                                 jlong j_histogram_hint) {
    DCHECK(j_histogram_name);
    HistogramBase* histogram = HistogramFromHint(j_histogram_hint);
    if (histogram)
      return histogram;

    std::string histogram_name = ConvertJavaStringToUTF8(env, j_histogram_name);
    histogram = SparseHistogram::FactoryGet(
        histogram_name, HistogramBase::kUmaTargetedHistogramFlag);
    return histogram;
  }

 private:
  // Convert a jlong |histogram_hint| from Java to a HistogramBase* via a cast.
  // The Java side caches these in a map (see NativeUmaRecorder.java), which is
  // safe to do since C++ Histogram objects are never freed.
  static HistogramBase* HistogramFromHint(jlong j_histogram_hint) {
    return reinterpret_cast<HistogramBase*>(j_histogram_hint);
  }
};

LazyInstance<HistogramCache>::Leaky g_histograms = LAZY_INSTANCE_INITIALIZER;

struct ActionCallbackWrapper {
  base::ActionCallback action_callback;
};

static void OnActionRecorded(const JavaRef<jobject>& callback,
                             const std::string& action,
                             TimeTicks action_time) {
  RunStringCallbackAndroid(callback, action);
}

}  // namespace

jlong JNI_NativeUmaRecorder_RecordBooleanHistogram(
    JNIEnv* env,
    const JavaParamRef<jstring>& j_histogram_name,
    jlong j_histogram_hint,
    jboolean j_sample) {
  bool sample = static_cast<bool>(j_sample);
  HistogramBase* histogram = g_histograms.Get().BooleanHistogram(
      env, j_histogram_name, j_histogram_hint);
  histogram->AddBoolean(sample);
  return reinterpret_cast<jlong>(histogram);
}

jlong JNI_NativeUmaRecorder_RecordExponentialHistogram(
    JNIEnv* env,
    const JavaParamRef<jstring>& j_histogram_name,
    jlong j_histogram_hint,
    jint j_sample,
    jint j_min,
    jint j_max,
    jint j_num_buckets) {
  int sample = static_cast<int>(j_sample);
  HistogramBase* histogram = g_histograms.Get().ExponentialHistogram(
      env, j_histogram_name, j_histogram_hint, j_min, j_max, j_num_buckets);
  histogram->Add(sample);
  return reinterpret_cast<jlong>(histogram);
}

jlong JNI_NativeUmaRecorder_RecordLinearHistogram(
    JNIEnv* env,
    const JavaParamRef<jstring>& j_histogram_name,
    jlong j_histogram_hint,
    jint j_sample,
    jint j_min,
    jint j_max,
    jint j_num_buckets) {
  int sample = static_cast<int>(j_sample);
  HistogramBase* histogram = g_histograms.Get().LinearHistogram(
      env, j_histogram_name, j_histogram_hint, j_min, j_max, j_num_buckets);
  histogram->Add(sample);
  return reinterpret_cast<jlong>(histogram);
}

jlong JNI_NativeUmaRecorder_RecordSparseHistogram(
    JNIEnv* env,
    const JavaParamRef<jstring>& j_histogram_name,
    jlong j_histogram_hint,
    jint j_sample) {
  int sample = static_cast<int>(j_sample);
  HistogramBase* histogram = g_histograms.Get().SparseHistogram(
      env, j_histogram_name, j_histogram_hint);
  histogram->Add(sample);
  return reinterpret_cast<jlong>(histogram);
}

void JNI_NativeUmaRecorder_RecordUserAction(
    JNIEnv* env,
    const JavaParamRef<jstring>& j_user_action_name,
    jlong j_millis_since_event) {
  // Time values coming from Java need to be synchronized with TimeTick clock.
  RecordComputedActionSince(ConvertJavaStringToUTF8(env, j_user_action_name),
                            Milliseconds(j_millis_since_event));
}

// This backs a Java test util for testing histograms -
// MetricsUtils.HistogramDelta. It should live in a test-specific file, but we
// currently can't have test-specific native code packaged in test-specific Java
// targets - see http://crbug.com/415945.
jint JNI_NativeUmaRecorder_GetHistogramValueCountForTesting(
    JNIEnv* env,
    const JavaParamRef<jstring>& histogram_name,
    jint sample,
    jlong snapshot_ptr) {
  std::string name = android::ConvertJavaStringToUTF8(env, histogram_name);
  HistogramBase* histogram = StatisticsRecorder::FindHistogram(name);
  if (histogram == nullptr) {
    // No samples have been recorded for this histogram (yet?).
    return 0;
  }

  int actual_count = histogram->SnapshotSamples()->GetCount(sample);
  if (snapshot_ptr) {
    auto* snapshot = reinterpret_cast<HistogramsSnapshot*>(snapshot_ptr);
    auto snapshot_data = snapshot->find(name);
    if (snapshot_data != snapshot->end())
      actual_count -= snapshot_data->second->GetCount(sample);
  }

  return actual_count;
}

jint JNI_NativeUmaRecorder_GetHistogramTotalCountForTesting(
    JNIEnv* env,
    const JavaParamRef<jstring>& histogram_name,
    jlong snapshot_ptr) {
  std::string name = android::ConvertJavaStringToUTF8(env, histogram_name);
  HistogramBase* histogram = StatisticsRecorder::FindHistogram(name);
  if (histogram == nullptr) {
    // No samples have been recorded for this histogram.
    return 0;
  }

  int actual_count = histogram->SnapshotSamples()->TotalCount();
  if (snapshot_ptr) {
    auto* snapshot = reinterpret_cast<HistogramsSnapshot*>(snapshot_ptr);
    auto snapshot_data = snapshot->find(name);
    if (snapshot_data != snapshot->end())
      actual_count -= snapshot_data->second->TotalCount();
  }
  return actual_count;
}

jlong JNI_NativeUmaRecorder_CreateHistogramSnapshotForTesting(JNIEnv* env) {
  HistogramsSnapshot* snapshot = new HistogramsSnapshot();
  for (const auto* const histogram : StatisticsRecorder::GetHistograms()) {
    (*snapshot)[histogram->histogram_name()] = histogram->SnapshotSamples();
  }
  return reinterpret_cast<intptr_t>(snapshot);
}

void JNI_NativeUmaRecorder_DestroyHistogramSnapshotForTesting(
    JNIEnv* env,
    jlong snapshot_ptr) {
  delete reinterpret_cast<HistogramsSnapshot*>(snapshot_ptr);
}

static jlong JNI_NativeUmaRecorder_AddActionCallbackForTesting(
    JNIEnv* env,
    const JavaParamRef<jobject>& callback) {
  // Create a wrapper for the ActionCallback, so it can life on the heap until
  // RemoveActionCallbackForTesting() is called.
  auto* wrapper = new ActionCallbackWrapper{base::BindRepeating(
      &OnActionRecorded, ScopedJavaGlobalRef<jobject>(env, callback))};
  base::AddActionCallback(wrapper->action_callback);
  return reinterpret_cast<intptr_t>(wrapper);
}

static void JNI_NativeUmaRecorder_RemoveActionCallbackForTesting(
    JNIEnv* env,
    jlong callback_id) {
  DCHECK(callback_id);
  auto* wrapper = reinterpret_cast<ActionCallbackWrapper*>(callback_id);
  base::RemoveActionCallback(wrapper->action_callback);
  delete wrapper;
}

}  // namespace android
}  // namespace base
