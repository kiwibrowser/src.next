// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_FRAME_DACTYLOSCOPER_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_FRAME_DACTYLOSCOPER_H_

#include "third_party/abseil-cpp/absl/types/optional.h"
#include "third_party/blink/public/common/privacy_budget/identifiable_token.h"
#include "third_party/blink/public/common/privacy_budget/identifiable_token_builder.h"
#include "third_party/blink/renderer/core/core_export.h"
#include "third_party/blink/renderer/core/frame/web_feature_forward.h"
#include "third_party/blink/renderer/core/typed_arrays/array_buffer_view_helpers.h"
#include "third_party/blink/renderer/core/typed_arrays/dom_typed_array.h"
#include "third_party/blink/renderer/platform/wtf/allocator/allocator.h"
#include "third_party/blink/renderer/platform/wtf/forward.h"
#include "third_party/blink/renderer/platform/wtf/text/wtf_string.h"

namespace blink {

class ExecutionContext;
class SVGStringListTearOff;

class CORE_EXPORT Dactyloscoper {
  DISALLOW_NEW();

 public:
  Dactyloscoper();
  Dactyloscoper(const Dactyloscoper&) = delete;
  Dactyloscoper& operator=(const Dactyloscoper&) = delete;

  void Record(WebFeature);

  static void Record(ExecutionContext*, WebFeature);

  // These are helpers used by the generated bindings code when invoking IDL
  // methods with HighEntropy=Direct.
  static void RecordDirectSurface(ExecutionContext*,
                                  WebFeature,
                                  const IdentifiableToken&);
  static void RecordDirectSurface(ExecutionContext*, WebFeature, const String&);
  static void RecordDirectSurface(ExecutionContext*,
                                  WebFeature,
                                  const Vector<String>&);
  static void RecordDirectSurface(ExecutionContext*,
                                  WebFeature,
                                  const DOMArrayBufferView*);
  static void RecordDirectSurface(ExecutionContext*,
                                  WebFeature,
                                  SVGStringListTearOff*);
  static void RecordDirectSurface(
      ExecutionContext* context,
      WebFeature feature,
      const NotShared<DOMArrayBufferView>& not_shared) {
    Dactyloscoper::RecordDirectSurface(context, feature, not_shared.Get());
  }
  static void RecordDirectSurface(
      ExecutionContext* context,
      WebFeature feature,
      const MaybeShared<DOMArrayBufferView>& maybe_shared) {
    Dactyloscoper::RecordDirectSurface(context, feature, maybe_shared.Get());
  }

  template <typename T>
  static void RecordDirectSurface(ExecutionContext* context,
                                  WebFeature feature,
                                  const absl::optional<T>& value) {
    if (value.has_value()) {
      RecordDirectSurface(context, feature, value.value());
    } else {
      RecordDirectSurface(context, feature,
                          IdentifiableTokenBuilder().GetToken());
    }
  }
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_CORE_FRAME_DACTYLOSCOPER_H_
