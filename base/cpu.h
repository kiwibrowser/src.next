// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_CPU_H_
#define BASE_CPU_H_

#include <string>
#include <vector>

#include "base/base_export.h"
#include "base/time/time.h"
#include "build/build_config.h"

namespace base {

#if defined(ARCH_CPU_X86_FAMILY)
namespace internal {

struct X86ModelInfo {
  int family;
  int model;
  int ext_family;
  int ext_model;
};

// Compute the CPU family and model based on the vendor and CPUID signature.
BASE_EXPORT X86ModelInfo ComputeX86FamilyAndModel(const std::string& vendor,
                                                  int signature);

}  // namespace internal
#endif  // defined(ARCH_CPU_X86_FAMILY)

// Query information about the processor.
class BASE_EXPORT CPU final {
 public:
  CPU();
  CPU(CPU&&);
  CPU(const CPU&) = delete;

  // Get a preallocated instance of CPU.
  // This can be used in very early application startup. The instance of CPU is
  // created without branding, see CPU(bool requires_branding) for details and
  // implications.
  static const CPU& GetInstanceNoAllocation();

  enum IntelMicroArchitecture {
    PENTIUM = 0,
    SSE = 1,
    SSE2 = 2,
    SSE3 = 3,
    SSSE3 = 4,
    SSE41 = 5,
    SSE42 = 6,
    AVX = 7,
    AVX2 = 8,
    FMA3 = 9,
    MAX_INTEL_MICRO_ARCHITECTURE = 10
  };

  // Accessors for CPU information.
  const std::string& vendor_name() const { return cpu_vendor_; }
  int signature() const { return signature_; }
  int stepping() const { return stepping_; }
  int model() const { return model_; }
  int family() const { return family_; }
  int type() const { return type_; }
  int extended_model() const { return ext_model_; }
  int extended_family() const { return ext_family_; }
  bool has_mmx() const { return has_mmx_; }
  bool has_sse() const { return has_sse_; }
  bool has_sse2() const { return has_sse2_; }
  bool has_sse3() const { return has_sse3_; }
  bool has_ssse3() const { return has_ssse3_; }
  bool has_sse41() const { return has_sse41_; }
  bool has_sse42() const { return has_sse42_; }
  bool has_popcnt() const { return has_popcnt_; }
  bool has_avx() const { return has_avx_; }
  bool has_fma3() const { return has_fma3_; }
  bool has_avx2() const { return has_avx2_; }
  bool has_aesni() const { return has_aesni_; }
  bool has_non_stop_time_stamp_counter() const {
    return has_non_stop_time_stamp_counter_;
  }
  bool is_running_in_vm() const { return is_running_in_vm_; }

#if defined(ARCH_CPU_ARM_FAMILY)
  // The cpuinfo values for ARM cores are from the MIDR_EL1 register, a
  // bitfield whose format is described in the core-specific manuals. E.g.,
  // ARM Cortex-A57:
  // https://developer.arm.com/documentation/ddi0488/h/system-control/aarch64-register-descriptions/main-id-register--el1.
  uint8_t implementer() const { return implementer_; }
  uint32_t part_number() const { return part_number_; }
#endif

  // Armv8.5-A extensions for control flow and memory safety.
#if defined(ARCH_CPU_ARM_FAMILY)
  bool has_mte() const { return has_mte_; }
  bool has_bti() const { return has_bti_; }
#else
  constexpr bool has_mte() const { return false; }
  constexpr bool has_bti() const { return false; }
#endif

#if defined(ARCH_CPU_X86_FAMILY)
  IntelMicroArchitecture GetIntelMicroArchitecture() const;
#endif
  const std::string& cpu_brand() const { return cpu_brand_; }

#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_ANDROID) || \
    BUILDFLAG(IS_AIX)
  enum class CoreType {
    kUnknown = 0,
    kOther,
    kSymmetric,
    kBigLittle_Little,
    kBigLittle_Big,
    kBigLittleBigger_Little,
    kBigLittleBigger_Big,
    kBigLittleBigger_Bigger,
    kMaxValue = kBigLittleBigger_Bigger
  };

  // Attempts to guess the core types of individual CPU cores based on frequency
  // information from /sys/devices/system/cpu/cpuN/cpufreq/cpuinfo_max_freq.
  // Beware that it is kernel/hardware dependent whether the information from
  // sys is accurate. Returns a reference to a static-storage vector (leaked on
  // shutdown) with the guessed type for core N at index N.
  static const std::vector<CoreType>& GetGuessedCoreTypes();

  struct TimeInStateEntry {
    CPU::CoreType core_type;      // type of the cores in this cluster.
    size_t cluster_core_index;    // index of the first core in the cluster.
    uint64_t core_frequency_khz;
    TimeDelta cumulative_time;
  };
  using TimeInState = std::vector<TimeInStateEntry>;

  // For each CPU core, emits the cumulative time spent in different frequency
  // states into the output parameter (replacing its current contents). One
  // entry in the output parameter is added for each cluster core index
  // + frequency state combination with a non-zero CPU time value. Returns false
  // on failure. We return the usage via an output parameter to allow reuse of
  // TimeInState's std::vector by the caller, e.g. to avoid allocations between
  // repeated calls to this method.
  //
  // NOTE: Currently only supported on Linux/Android, and only on kernels with
  // cpufreq-stats driver.
  static bool GetTimeInState(TimeInState&);

  // For each CPU core, emits the total cumulative wall time spent in any idle
  // state into the output parameter (replacing its current contents). Returns
  // false on failure. We return the usage via an output parameter to allow
  // reuse of TimeInState's std::vector by the caller, e.g. to avoid allocations
  // between repeated calls to this method.
  //
  // NOTE: Currently only supported on Linux/Android, and only on kernels with
  // cpuidle driver.
  using CoreIdleTimes = std::vector<TimeDelta>;
  static bool GetCumulativeCoreIdleTimes(CoreIdleTimes&);
#endif  // BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) ||
        // BUILDFLAG(IS_ANDROID) || BUILDFLAG(IS_AIX)

 private:
  // Query the processor for CPUID information.
  void Initialize(bool requires_branding);
  explicit CPU(bool requires_branding);

  int signature_ = 0;  // raw form of type, family, model, and stepping
  int type_ = 0;       // process type
  int family_ = 0;     // family of the processor
  int model_ = 0;      // model of processor
  int stepping_ = 0;   // processor revision number
  int ext_model_ = 0;
  int ext_family_ = 0;
#if defined(ARCH_CPU_ARM_FAMILY)
  uint32_t part_number_ = 0;  // ARM MIDR part number
  uint8_t implementer_ = 0;   // ARM MIDR implementer identifier
#endif
  bool has_mmx_ = false;
  bool has_sse_ = false;
  bool has_sse2_ = false;
  bool has_sse3_ = false;
  bool has_ssse3_ = false;
  bool has_sse41_ = false;
  bool has_sse42_ = false;
  bool has_popcnt_ = false;
  bool has_avx_ = false;
  bool has_fma3_ = false;
  bool has_avx2_ = false;
  bool has_aesni_ = false;
#if defined(ARCH_CPU_ARM_FAMILY)
  bool has_mte_ = false;  // Armv8.5-A MTE (Memory Taggging Extension)
  bool has_bti_ = false;  // Armv8.5-A BTI (Branch Target Identification)
#endif
  bool has_non_stop_time_stamp_counter_ = false;
  bool is_running_in_vm_ = false;
  std::string cpu_vendor_ = "unknown";
  std::string cpu_brand_;
};

}  // namespace base

#endif  // BASE_CPU_H_
