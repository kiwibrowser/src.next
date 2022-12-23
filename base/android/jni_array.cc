// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/android/jni_array.h"

#include <ostream>

#include "base/android/jni_android.h"
#include "base/android/jni_string.h"
#include "base/check_op.h"
#include "base/numerics/safe_conversions.h"

namespace base {
namespace android {
namespace {

// As |GetArrayLength| makes no guarantees about the returned value (e.g., it
// may be -1 if |array| is not a valid Java array), provide a safe wrapper
// that always returns a valid, non-negative size.
template <typename JavaArrayType>
size_t SafeGetArrayLength(JNIEnv* env, const JavaRef<JavaArrayType>& jarray) {
  DCHECK(jarray);
  jsize length = env->GetArrayLength(jarray.obj());
  DCHECK_GE(length, 0) << "Invalid array length: " << length;
  return static_cast<size_t>(std::max(0, length));
}

}  // namespace

ScopedJavaLocalRef<jbyteArray> ToJavaByteArray(JNIEnv* env,
                                               const uint8_t* bytes,
                                               size_t len) {
  const jsize len_jsize = checked_cast<jsize>(len);
  jbyteArray byte_array = env->NewByteArray(len_jsize);
  CheckException(env);
  DCHECK(byte_array);

  env->SetByteArrayRegion(byte_array, 0, len_jsize,
                          reinterpret_cast<const jbyte*>(bytes));
  CheckException(env);

  return ScopedJavaLocalRef<jbyteArray>(env, byte_array);
}

ScopedJavaLocalRef<jbyteArray> ToJavaByteArray(
    JNIEnv* env,
    base::span<const uint8_t> bytes) {
  return ToJavaByteArray(env, bytes.data(), bytes.size());
}

ScopedJavaLocalRef<jbyteArray> ToJavaByteArray(JNIEnv* env,
                                               const std::string& str) {
  return ToJavaByteArray(env, reinterpret_cast<const uint8_t*>(str.data()),
                         str.size());
}

ScopedJavaLocalRef<jbooleanArray> ToJavaBooleanArray(JNIEnv* env,
                                                     const bool* bools,
                                                     size_t len) {
  const jsize len_jsize = checked_cast<jsize>(len);
  jbooleanArray boolean_array = env->NewBooleanArray(len_jsize);
  CheckException(env);
  DCHECK(boolean_array);

  env->SetBooleanArrayRegion(boolean_array, 0, len_jsize,
                             reinterpret_cast<const jboolean*>(bools));
  CheckException(env);

  return ScopedJavaLocalRef<jbooleanArray>(env, boolean_array);
}

ScopedJavaLocalRef<jintArray> ToJavaIntArray(JNIEnv* env,
                                             const int* ints,
                                             size_t len) {
  const jsize len_jsize = checked_cast<jsize>(len);
  jintArray int_array = env->NewIntArray(len_jsize);
  CheckException(env);
  DCHECK(int_array);

  env->SetIntArrayRegion(int_array, 0, len_jsize,
                         reinterpret_cast<const jint*>(ints));
  CheckException(env);

  return ScopedJavaLocalRef<jintArray>(env, int_array);
}

ScopedJavaLocalRef<jintArray> ToJavaIntArray(JNIEnv* env,
                                             base::span<const int> ints) {
  return ToJavaIntArray(env, ints.data(), ints.size());
}

ScopedJavaLocalRef<jlongArray> ToJavaLongArray(JNIEnv* env,
                                               const int64_t* longs,
                                               size_t len) {
  const jsize len_jsize = checked_cast<jsize>(len);
  jlongArray long_array = env->NewLongArray(len_jsize);
  CheckException(env);
  DCHECK(long_array);

  env->SetLongArrayRegion(long_array, 0, len_jsize,
                          reinterpret_cast<const jlong*>(longs));
  CheckException(env);

  return ScopedJavaLocalRef<jlongArray>(env, long_array);
}

// Returns a new Java long array converted from the given int64_t array.
BASE_EXPORT ScopedJavaLocalRef<jlongArray> ToJavaLongArray(
    JNIEnv* env,
    base::span<const int64_t> longs) {
  return ToJavaLongArray(env, longs.data(), longs.size());
}

// Returns a new Java float array converted from the given C++ float array.
BASE_EXPORT ScopedJavaLocalRef<jfloatArray>
ToJavaFloatArray(JNIEnv* env, const float* floats, size_t len) {
  const jsize len_jsize = checked_cast<jsize>(len);
  jfloatArray float_array = env->NewFloatArray(len_jsize);
  CheckException(env);
  DCHECK(float_array);

  env->SetFloatArrayRegion(float_array, 0, len_jsize,
                           reinterpret_cast<const jfloat*>(floats));
  CheckException(env);

  return ScopedJavaLocalRef<jfloatArray>(env, float_array);
}

BASE_EXPORT ScopedJavaLocalRef<jfloatArray> ToJavaFloatArray(
    JNIEnv* env,
    base::span<const float> floats) {
  return ToJavaFloatArray(env, floats.data(), floats.size());
}

BASE_EXPORT ScopedJavaLocalRef<jdoubleArray>
ToJavaDoubleArray(JNIEnv* env, const double* doubles, size_t len) {
  const jsize len_jsize = checked_cast<jsize>(len);
  jdoubleArray double_array = env->NewDoubleArray(len_jsize);
  CheckException(env);
  DCHECK(double_array);

  env->SetDoubleArrayRegion(double_array, 0, len_jsize,
                            reinterpret_cast<const jdouble*>(doubles));
  CheckException(env);

  return ScopedJavaLocalRef<jdoubleArray>(env, double_array);
}

BASE_EXPORT ScopedJavaLocalRef<jdoubleArray> ToJavaDoubleArray(
    JNIEnv* env,
    base::span<const double> doubles) {
  return ToJavaDoubleArray(env, doubles.data(), doubles.size());
}

BASE_EXPORT ScopedJavaLocalRef<jobjectArray> ToJavaArrayOfObjects(
    JNIEnv* env,
    ScopedJavaLocalRef<jclass> clazz,
    base::span<const ScopedJavaLocalRef<jobject>> v) {
  jobjectArray joa =
      env->NewObjectArray(checked_cast<jsize>(v.size()), clazz.obj(), nullptr);
  CheckException(env);

  for (size_t i = 0; i < v.size(); ++i) {
    env->SetObjectArrayElement(joa, static_cast<jsize>(i), v[i].obj());
  }
  return ScopedJavaLocalRef<jobjectArray>(env, joa);
}

BASE_EXPORT ScopedJavaLocalRef<jobjectArray> ToJavaArrayOfObjects(
    JNIEnv* env,
    base::span<const ScopedJavaLocalRef<jobject>> v) {
  return ToJavaArrayOfObjects(env, GetClass(env, "java/lang/Object"), v);
}

BASE_EXPORT ScopedJavaLocalRef<jobjectArray> ToJavaArrayOfObjects(
    JNIEnv* env,
    base::span<const ScopedJavaGlobalRef<jobject>> v) {
  ScopedJavaLocalRef<jclass> object_array_clazz =
      GetClass(env, "java/lang/Object");
  jobjectArray joa = env->NewObjectArray(checked_cast<jsize>(v.size()),
                                         object_array_clazz.obj(), nullptr);
  CheckException(env);

  for (size_t i = 0; i < v.size(); ++i) {
    env->SetObjectArrayElement(joa, static_cast<jsize>(i), v[i].obj());
  }
  return ScopedJavaLocalRef<jobjectArray>(env, joa);
}

BASE_EXPORT ScopedJavaLocalRef<jobjectArray> ToTypedJavaArrayOfObjects(
    JNIEnv* env,
    base::span<const ScopedJavaLocalRef<jobject>> v,
    ScopedJavaLocalRef<jclass> type) {
  jobjectArray joa =
      env->NewObjectArray(checked_cast<jsize>(v.size()), type.obj(), nullptr);
  CheckException(env);

  for (size_t i = 0; i < v.size(); ++i) {
    env->SetObjectArrayElement(joa, static_cast<jsize>(i), v[i].obj());
  }
  return ScopedJavaLocalRef<jobjectArray>(env, joa);
}

BASE_EXPORT ScopedJavaLocalRef<jobjectArray> ToTypedJavaArrayOfObjects(
    JNIEnv* env,
    base::span<const ScopedJavaGlobalRef<jobject>> v,
    ScopedJavaLocalRef<jclass> type) {
  jobjectArray joa =
      env->NewObjectArray(checked_cast<jsize>(v.size()), type.obj(), nullptr);
  CheckException(env);

  for (size_t i = 0; i < v.size(); ++i) {
    env->SetObjectArrayElement(joa, static_cast<jsize>(i), v[i].obj());
  }
  return ScopedJavaLocalRef<jobjectArray>(env, joa);
}

ScopedJavaLocalRef<jobjectArray> ToJavaArrayOfByteArray(
    JNIEnv* env,
    base::span<const std::string> v) {
  ScopedJavaLocalRef<jclass> byte_array_clazz = GetClass(env, "[B");
  jobjectArray joa = env->NewObjectArray(checked_cast<jsize>(v.size()),
                                         byte_array_clazz.obj(), nullptr);
  CheckException(env);

  for (size_t i = 0; i < v.size(); ++i) {
    ScopedJavaLocalRef<jbyteArray> byte_array = ToJavaByteArray(
        env, reinterpret_cast<const uint8_t*>(v[i].data()), v[i].length());
    env->SetObjectArrayElement(joa, static_cast<jsize>(i), byte_array.obj());
  }
  return ScopedJavaLocalRef<jobjectArray>(env, joa);
}

ScopedJavaLocalRef<jobjectArray> ToJavaArrayOfByteArray(
    JNIEnv* env,
    base::span<const std::vector<uint8_t>> v) {
  ScopedJavaLocalRef<jclass> byte_array_clazz = GetClass(env, "[B");
  jobjectArray joa = env->NewObjectArray(checked_cast<jsize>(v.size()),
                                         byte_array_clazz.obj(), nullptr);
  CheckException(env);

  for (size_t i = 0; i < v.size(); ++i) {
    ScopedJavaLocalRef<jbyteArray> byte_array =
        ToJavaByteArray(env, v[i].data(), v[i].size());
    env->SetObjectArrayElement(joa, static_cast<jsize>(i), byte_array.obj());
  }
  return ScopedJavaLocalRef<jobjectArray>(env, joa);
}

ScopedJavaLocalRef<jobjectArray> ToJavaArrayOfStrings(
    JNIEnv* env,
    base::span<const std::string> v) {
  ScopedJavaLocalRef<jclass> string_clazz = GetClass(env, "java/lang/String");
  jobjectArray joa = env->NewObjectArray(checked_cast<jsize>(v.size()),
                                         string_clazz.obj(), nullptr);
  CheckException(env);

  for (size_t i = 0; i < v.size(); ++i) {
    ScopedJavaLocalRef<jstring> item = ConvertUTF8ToJavaString(env, v[i]);
    env->SetObjectArrayElement(joa, static_cast<jsize>(i), item.obj());
  }
  return ScopedJavaLocalRef<jobjectArray>(env, joa);
}

ScopedJavaLocalRef<jobjectArray> ToJavaArrayOfStringArray(
    JNIEnv* env,
    base::span<const std::vector<std::string>> vec_outer) {
  ScopedJavaLocalRef<jclass> string_array_clazz =
      GetClass(env, "[Ljava/lang/String;");

  jobjectArray joa = env->NewObjectArray(checked_cast<jsize>(vec_outer.size()),
                                         string_array_clazz.obj(), nullptr);
  CheckException(env);

  for (size_t i = 0; i < vec_outer.size(); ++i) {
    ScopedJavaLocalRef<jobjectArray> inner =
        ToJavaArrayOfStrings(env, vec_outer[i]);
    env->SetObjectArrayElement(joa, static_cast<jsize>(i), inner.obj());
  }

  return ScopedJavaLocalRef<jobjectArray>(env, joa);
}

ScopedJavaLocalRef<jobjectArray> ToJavaArrayOfStringArray(
    JNIEnv* env,
    base::span<const std::vector<std::u16string>> vec_outer) {
  ScopedJavaLocalRef<jclass> string_array_clazz =
      GetClass(env, "[Ljava/lang/String;");

  jobjectArray joa = env->NewObjectArray(checked_cast<jsize>(vec_outer.size()),
                                         string_array_clazz.obj(), nullptr);
  CheckException(env);

  for (size_t i = 0; i < vec_outer.size(); ++i) {
    ScopedJavaLocalRef<jobjectArray> inner =
        ToJavaArrayOfStrings(env, vec_outer[i]);
    env->SetObjectArrayElement(joa, static_cast<jsize>(i), inner.obj());
  }

  return ScopedJavaLocalRef<jobjectArray>(env, joa);
}

ScopedJavaLocalRef<jobjectArray> ToJavaArrayOfStrings(
    JNIEnv* env,
    base::span<const std::u16string> v) {
  ScopedJavaLocalRef<jclass> string_clazz = GetClass(env, "java/lang/String");
  jobjectArray joa = env->NewObjectArray(checked_cast<jsize>(v.size()),
                                         string_clazz.obj(), nullptr);
  CheckException(env);

  for (size_t i = 0; i < v.size(); ++i) {
    ScopedJavaLocalRef<jstring> item = ConvertUTF16ToJavaString(env, v[i]);
    env->SetObjectArrayElement(joa, static_cast<jsize>(i), item.obj());
  }
  return ScopedJavaLocalRef<jobjectArray>(env, joa);
}

void AppendJavaStringArrayToStringVector(JNIEnv* env,
                                         const JavaRef<jobjectArray>& array,
                                         std::vector<std::u16string>* out) {
  DCHECK(out);
  if (!array)
    return;
  size_t len = SafeGetArrayLength(env, array);
  size_t back = out->size();
  out->resize(back + len);
  for (size_t i = 0; i < len; ++i) {
    ScopedJavaLocalRef<jstring> str(
        env, static_cast<jstring>(env->GetObjectArrayElement(
                 array.obj(), static_cast<jsize>(i))));
    ConvertJavaStringToUTF16(env, str.obj(), out->data() + back + i);
  }
}

void AppendJavaStringArrayToStringVector(JNIEnv* env,
                                         const JavaRef<jobjectArray>& array,
                                         std::vector<std::string>* out) {
  DCHECK(out);
  if (!array)
    return;
  size_t len = SafeGetArrayLength(env, array);
  size_t back = out->size();
  out->resize(back + len);
  for (size_t i = 0; i < len; ++i) {
    ScopedJavaLocalRef<jstring> str(
        env, static_cast<jstring>(env->GetObjectArrayElement(
                 array.obj(), static_cast<jsize>(i))));
    ConvertJavaStringToUTF8(env, str.obj(), out->data() + back + i);
  }
}

void AppendJavaByteArrayToByteVector(JNIEnv* env,
                                     const JavaRef<jbyteArray>& byte_array,
                                     std::vector<uint8_t>* out) {
  DCHECK(out);
  if (!byte_array)
    return;
  size_t len = SafeGetArrayLength(env, byte_array);
  if (!len)
    return;
  size_t back = out->size();
  out->resize(back + len);
  env->GetByteArrayRegion(byte_array.obj(), 0, static_cast<jsize>(len),
                          reinterpret_cast<int8_t*>(out->data() + back));
}

void JavaByteArrayToByteVector(JNIEnv* env,
                               const JavaRef<jbyteArray>& byte_array,
                               std::vector<uint8_t>* out) {
  DCHECK(out);
  DCHECK(byte_array);
  out->clear();
  AppendJavaByteArrayToByteVector(env, byte_array, out);
}

void JavaByteArrayToString(JNIEnv* env,
                           const JavaRef<jbyteArray>& byte_array,
                           std::string* out) {
  DCHECK(out);
  DCHECK(byte_array);

  std::vector<uint8_t> byte_vector;
  JavaByteArrayToByteVector(env, byte_array, &byte_vector);
  out->assign(byte_vector.begin(), byte_vector.end());
}

void JavaBooleanArrayToBoolVector(JNIEnv* env,
                                  const JavaRef<jbooleanArray>& boolean_array,
                                  std::vector<bool>* out) {
  DCHECK(out);
  if (!boolean_array)
    return;
  size_t len = SafeGetArrayLength(env, boolean_array);
  if (!len)
    return;
  out->resize(len);
  // It is not possible to get bool* out of vector<bool>.
  jboolean* values = env->GetBooleanArrayElements(boolean_array.obj(), nullptr);
  for (size_t i = 0; i < len; ++i) {
    out->at(i) = static_cast<bool>(values[i]);
  }
  env->ReleaseBooleanArrayElements(boolean_array.obj(), values, JNI_ABORT);
}

void JavaIntArrayToIntVector(JNIEnv* env,
                             const JavaRef<jintArray>& int_array,
                             std::vector<int>* out) {
  DCHECK(out);
  size_t len = SafeGetArrayLength(env, int_array);
  out->resize(len);
  if (!len)
    return;
  env->GetIntArrayRegion(int_array.obj(), 0, static_cast<jsize>(len),
                         out->data());
}

void JavaLongArrayToInt64Vector(JNIEnv* env,
                                const JavaRef<jlongArray>& long_array,
                                std::vector<int64_t>* out) {
  DCHECK(out);
  std::vector<jlong> temp;
  JavaLongArrayToLongVector(env, long_array, &temp);
  out->resize(0);
  out->insert(out->begin(), temp.begin(), temp.end());
}

void JavaLongArrayToLongVector(JNIEnv* env,
                               const JavaRef<jlongArray>& long_array,
                               std::vector<jlong>* out) {
  DCHECK(out);
  size_t len = SafeGetArrayLength(env, long_array);
  out->resize(len);
  if (!len)
    return;
  env->GetLongArrayRegion(long_array.obj(), 0, static_cast<jsize>(len),
                          out->data());
}

void JavaFloatArrayToFloatVector(JNIEnv* env,
                                 const JavaRef<jfloatArray>& float_array,
                                 std::vector<float>* out) {
  DCHECK(out);
  size_t len = SafeGetArrayLength(env, float_array);
  out->resize(len);
  if (!len)
    return;
  env->GetFloatArrayRegion(float_array.obj(), 0, static_cast<jsize>(len),
                           out->data());
}

void JavaDoubleArrayToDoubleVector(JNIEnv* env,
                                   const JavaRef<jdoubleArray>& double_array,
                                   std::vector<double>* out) {
  DCHECK(out);
  size_t len = SafeGetArrayLength(env, double_array);
  out->resize(len);
  if (!len)
    return;
  env->GetDoubleArrayRegion(double_array.obj(), 0, static_cast<jsize>(len),
                            out->data());
}

void JavaArrayOfByteArrayToStringVector(JNIEnv* env,
                                        const JavaRef<jobjectArray>& array,
                                        std::vector<std::string>* out) {
  DCHECK(out);
  size_t len = SafeGetArrayLength(env, array);
  out->resize(len);
  for (size_t i = 0; i < len; ++i) {
    ScopedJavaLocalRef<jbyteArray> bytes_array(
        env, static_cast<jbyteArray>(env->GetObjectArrayElement(
                 array.obj(), static_cast<jsize>(i))));
    size_t bytes_len = SafeGetArrayLength(env, bytes_array);
    jbyte* bytes = env->GetByteArrayElements(bytes_array.obj(), nullptr);
    (*out)[i].assign(reinterpret_cast<const char*>(bytes), bytes_len);
    env->ReleaseByteArrayElements(bytes_array.obj(), bytes, JNI_ABORT);
  }
}

void JavaArrayOfByteArrayToBytesVector(JNIEnv* env,
                                       const JavaRef<jobjectArray>& array,
                                       std::vector<std::vector<uint8_t>>* out) {
  DCHECK(out);
  const size_t len = SafeGetArrayLength(env, array);
  out->resize(len);
  for (size_t i = 0; i < len; ++i) {
    ScopedJavaLocalRef<jbyteArray> bytes_array(
        env, static_cast<jbyteArray>(env->GetObjectArrayElement(
                 array.obj(), static_cast<jsize>(i))));
    JavaByteArrayToByteVector(env, bytes_array, &(*out)[i]);
  }
}

void Java2dStringArrayTo2dStringVector(
    JNIEnv* env,
    const JavaRef<jobjectArray>& array,
    std::vector<std::vector<std::u16string>>* out) {
  DCHECK(out);
  size_t len = SafeGetArrayLength(env, array);
  out->resize(len);
  for (size_t i = 0; i < len; ++i) {
    ScopedJavaLocalRef<jobjectArray> strings_array(
        env, static_cast<jobjectArray>(env->GetObjectArrayElement(
                 array.obj(), static_cast<jsize>(i))));

    out->at(i).clear();
    AppendJavaStringArrayToStringVector(env, strings_array, &out->at(i));
  }
}

void Java2dStringArrayTo2dStringVector(
    JNIEnv* env,
    const JavaRef<jobjectArray>& array,
    std::vector<std::vector<std::string>>* out) {
  DCHECK(out);
  size_t len = SafeGetArrayLength(env, array);
  out->resize(len);
  for (size_t i = 0; i < len; ++i) {
    ScopedJavaLocalRef<jobjectArray> strings_array(
        env, static_cast<jobjectArray>(env->GetObjectArrayElement(
                 array.obj(), static_cast<jsize>(i))));

    out->at(i).clear();
    AppendJavaStringArrayToStringVector(env, strings_array, &out->at(i));
  }
}

void JavaArrayOfIntArrayToIntVector(JNIEnv* env,
                                    const JavaRef<jobjectArray>& array,
                                    std::vector<std::vector<int>>* out) {
  DCHECK(out);
  size_t len = SafeGetArrayLength(env, array);
  out->resize(len);
  for (size_t i = 0; i < len; ++i) {
    ScopedJavaLocalRef<jintArray> int_array(
        env, static_cast<jintArray>(env->GetObjectArrayElement(
                 array.obj(), static_cast<jsize>(i))));
    JavaIntArrayToIntVector(env, int_array, &out->at(i));
  }
}

}  // namespace android
}  // namespace base
