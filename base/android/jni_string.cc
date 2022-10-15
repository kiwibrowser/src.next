// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/android/jni_string.h"

#include "base/android/jni_android.h"
#include "base/logging.h"
#include "base/numerics/safe_conversions.h"
#include "base/strings/utf_string_conversions.h"

namespace {

// Internal version that does not use a scoped local pointer.
jstring ConvertUTF16ToJavaStringImpl(JNIEnv* env,
                                     const base::StringPiece16& str) {
  jstring result = env->NewString(reinterpret_cast<const jchar*>(str.data()),
                                  base::checked_cast<jsize>(str.length()));
  base::android::CheckException(env);
  return result;
}

}  // namespace

namespace base {
namespace android {

void ConvertJavaStringToUTF8(JNIEnv* env, jstring str, std::string* result) {
  DCHECK(str);
  if (!str) {
    LOG(WARNING) << "ConvertJavaStringToUTF8 called with null string.";
    result->clear();
    return;
  }
  const jsize length = env->GetStringLength(str);
  if (length <= 0) {
    result->clear();
    CheckException(env);
    return;
  }
  // JNI's GetStringUTFChars() returns strings in Java "modified" UTF8, so
  // instead get the String in UTF16 and convert using chromium's conversion
  // function that yields plain (non Java-modified) UTF8.
  const jchar* chars = env->GetStringChars(str, NULL);
  DCHECK(chars);
  UTF16ToUTF8(reinterpret_cast<const char16_t*>(chars),
              static_cast<size_t>(length), result);
  env->ReleaseStringChars(str, chars);
  CheckException(env);
}

std::string ConvertJavaStringToUTF8(JNIEnv* env, jstring str) {
  std::string result;
  ConvertJavaStringToUTF8(env, str, &result);
  return result;
}

std::string ConvertJavaStringToUTF8(const JavaRef<jstring>& str) {
  return ConvertJavaStringToUTF8(AttachCurrentThread(), str.obj());
}

std::string ConvertJavaStringToUTF8(JNIEnv* env, const JavaRef<jstring>& str) {
  return ConvertJavaStringToUTF8(env, str.obj());
}

ScopedJavaLocalRef<jstring> ConvertUTF8ToJavaString(JNIEnv* env,
                                                    const StringPiece& str) {
  // JNI's NewStringUTF expects "modified" UTF8 so instead create the string
  // via our own UTF16 conversion utility.
  // Further, Dalvik requires the string passed into NewStringUTF() to come from
  // a trusted source. We can't guarantee that all UTF8 will be sanitized before
  // it gets here, so constructing via UTF16 side-steps this issue.
  // (Dalvik stores strings internally as UTF16 anyway, so there shouldn't be
  // a significant performance hit by doing it this way).
  return ScopedJavaLocalRef<jstring>(env, ConvertUTF16ToJavaStringImpl(
      env, UTF8ToUTF16(str)));
}

void ConvertJavaStringToUTF16(JNIEnv* env,
                              jstring str,
                              std::u16string* result) {
  DCHECK(str);
  if (!str) {
    LOG(WARNING) << "ConvertJavaStringToUTF16 called with null string.";
    result->clear();
    return;
  }
  const jsize length = env->GetStringLength(str);
  if (length <= 0) {
    result->clear();
    CheckException(env);
    return;
  }
  const jchar* chars = env->GetStringChars(str, NULL);
  DCHECK(chars);
  // GetStringChars isn't required to NULL-terminate the strings
  // it returns, so the length must be explicitly checked.
  result->assign(reinterpret_cast<const char16_t*>(chars),
                 static_cast<size_t>(length));
  env->ReleaseStringChars(str, chars);
  CheckException(env);
}

std::u16string ConvertJavaStringToUTF16(JNIEnv* env, jstring str) {
  std::u16string result;
  ConvertJavaStringToUTF16(env, str, &result);
  return result;
}

std::u16string ConvertJavaStringToUTF16(const JavaRef<jstring>& str) {
  return ConvertJavaStringToUTF16(AttachCurrentThread(), str.obj());
}

std::u16string ConvertJavaStringToUTF16(JNIEnv* env,
                                        const JavaRef<jstring>& str) {
  return ConvertJavaStringToUTF16(env, str.obj());
}

ScopedJavaLocalRef<jstring> ConvertUTF16ToJavaString(JNIEnv* env,
                                                     const StringPiece16& str) {
  return ScopedJavaLocalRef<jstring>(env,
                                     ConvertUTF16ToJavaStringImpl(env, str));
}

}  // namespace android
}  // namespace base
