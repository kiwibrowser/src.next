// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This header defines cross-platform ByteSwap() implementations for 16, 32 and
// 64-bit values, and NetToHostXX() / HostToNextXX() functions equivalent to
// the traditional ntohX() and htonX() functions.
// Use the functions defined here rather than using the platform-specific
// functions directly.

#ifndef BASE_SYS_BYTEORDER_H_
#define BASE_SYS_BYTEORDER_H_

#include <stdint.h>

#include "build/build_config.h"

#if defined(COMPILER_MSVC)
#include <stdlib.h>
#endif

#if defined(COMPILER_MSVC) && !defined(__clang__)
// TODO(pkasting): See
// https://developercommunity.visualstudio.com/t/Mark-some-built-in-functions-as-constexp/362558
// https://developercommunity.visualstudio.com/t/constexpr-byte-swapping-optimization/983963
#define BASE_BYTESWAPS_CONSTEXPR
#else
#define BASE_BYTESWAPS_CONSTEXPR constexpr
#endif

namespace base {

// Returns a value with all bytes in |x| swapped, i.e. reverses the endianness.
// TODO(pkasting): Once C++23 is available, replace with std::byteswap.
inline BASE_BYTESWAPS_CONSTEXPR uint16_t ByteSwap(uint16_t x) {
#if defined(COMPILER_MSVC) && !defined(__clang__)
  return _byteswap_ushort(x);
#else
  return __builtin_bswap16(x);
#endif
}

inline BASE_BYTESWAPS_CONSTEXPR uint32_t ByteSwap(uint32_t x) {
#if defined(COMPILER_MSVC) && !defined(__clang__)
  return _byteswap_ulong(x);
#else
  return __builtin_bswap32(x);
#endif
}

inline BASE_BYTESWAPS_CONSTEXPR uint64_t ByteSwap(uint64_t x) {
  // Per build/build_config.h, clang masquerades as MSVC on Windows. If we are
  // actually using clang, we can rely on the builtin.
  //
  // This matters in practice, because on x86(_64), this is a single "bswap"
  // instruction. MSVC correctly replaces the call with an inlined bswap at /O2
  // as of 2021, but clang as we use it in Chromium doesn't, keeping a function
  // call for a single instruction.
#if defined(COMPILER_MSVC) && !defined(__clang__)
  return _byteswap_uint64(x);
#else
  return __builtin_bswap64(x);
#endif
}

inline BASE_BYTESWAPS_CONSTEXPR uintptr_t ByteSwapUintPtrT(uintptr_t x) {
  // We do it this way because some build configurations are ILP32 even when
  // defined(ARCH_CPU_64_BITS). Unfortunately, we can't use sizeof in #ifs. But,
  // because these conditionals are constexprs, the irrelevant branches will
  // likely be optimized away, so this construction should not result in code
  // bloat.
  static_assert(sizeof(uintptr_t) == 4 || sizeof(uintptr_t) == 8,
                "Unsupported uintptr_t size");
  if (sizeof(uintptr_t) == 4)
    return ByteSwap(static_cast<uint32_t>(x));
  return ByteSwap(static_cast<uint64_t>(x));
}

// Converts the bytes in |x| from host order (endianness) to little endian, and
// returns the result.
inline BASE_BYTESWAPS_CONSTEXPR uint16_t ByteSwapToLE16(uint16_t x) {
#if defined(ARCH_CPU_LITTLE_ENDIAN)
  return x;
#else
  return ByteSwap(x);
#endif
}
inline BASE_BYTESWAPS_CONSTEXPR uint32_t ByteSwapToLE32(uint32_t x) {
#if defined(ARCH_CPU_LITTLE_ENDIAN)
  return x;
#else
  return ByteSwap(x);
#endif
}
inline BASE_BYTESWAPS_CONSTEXPR uint64_t ByteSwapToLE64(uint64_t x) {
#if defined(ARCH_CPU_LITTLE_ENDIAN)
  return x;
#else
  return ByteSwap(x);
#endif
}

// Converts the bytes in |x| from network to host order (endianness), and
// returns the result.
inline BASE_BYTESWAPS_CONSTEXPR uint16_t NetToHost16(uint16_t x) {
#if defined(ARCH_CPU_LITTLE_ENDIAN)
  return ByteSwap(x);
#else
  return x;
#endif
}
inline BASE_BYTESWAPS_CONSTEXPR uint32_t NetToHost32(uint32_t x) {
#if defined(ARCH_CPU_LITTLE_ENDIAN)
  return ByteSwap(x);
#else
  return x;
#endif
}
inline BASE_BYTESWAPS_CONSTEXPR uint64_t NetToHost64(uint64_t x) {
#if defined(ARCH_CPU_LITTLE_ENDIAN)
  return ByteSwap(x);
#else
  return x;
#endif
}

// Converts the bytes in |x| from host to network order (endianness), and
// returns the result.
inline BASE_BYTESWAPS_CONSTEXPR uint16_t HostToNet16(uint16_t x) {
#if defined(ARCH_CPU_LITTLE_ENDIAN)
  return ByteSwap(x);
#else
  return x;
#endif
}
inline BASE_BYTESWAPS_CONSTEXPR uint32_t HostToNet32(uint32_t x) {
#if defined(ARCH_CPU_LITTLE_ENDIAN)
  return ByteSwap(x);
#else
  return x;
#endif
}
inline BASE_BYTESWAPS_CONSTEXPR uint64_t HostToNet64(uint64_t x) {
#if defined(ARCH_CPU_LITTLE_ENDIAN)
  return ByteSwap(x);
#else
  return x;
#endif
}

}  // namespace base

#undef BASE_BYTESWAPS_CONSTEXPR

#endif  // BASE_SYS_BYTEORDER_H_
