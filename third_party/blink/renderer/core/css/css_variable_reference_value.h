// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_CSS_CSS_VARIABLE_REFERENCE_VALUE_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_CSS_CSS_VARIABLE_REFERENCE_VALUE_H_

#include "base/memory/scoped_refptr.h"
#include "third_party/blink/renderer/core/css/css_value.h"
#include "third_party/blink/renderer/core/css/css_variable_data.h"
#include "third_party/blink/renderer/core/css/parser/css_parser_context.h"
#include "third_party/blink/renderer/platform/wtf/casting.h"

namespace blink {

class CSSVariableReferenceValue final : public CSSValue {
 public:
  explicit CSSVariableReferenceValue(scoped_refptr<CSSVariableData> data)
      : CSSValue(kVariableReferenceClass), data_(std::move(data)) {}

  CSSVariableReferenceValue(scoped_refptr<CSSVariableData> data,
                            const CSSParserContext& context)
      : CSSValue(kVariableReferenceClass),
        parser_context_(context),
        data_(std::move(data)) {}

  CSSVariableData* VariableDataValue() const { return data_.get(); }
  const CSSParserContext* ParserContext() const {
    // TODO(crbug.com/985028): CSSVariableReferenceValue should always have
    // a CSSParserContext.
    return parser_context_.Get();
  }

  bool Equals(const CSSVariableReferenceValue& other) const {
    return data_ == other.data_;
  }
  String CustomCSSText() const;

  void TraceAfterDispatch(blink::Visitor*) const;

 private:
  const Member<const CSSParserContext> parser_context_;
  scoped_refptr<CSSVariableData> data_;
};

template <>
struct DowncastTraits<CSSVariableReferenceValue> {
  static bool AllowFrom(const CSSValue& value) {
    return value.IsVariableReferenceValue();
  }
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_CORE_CSS_CSS_VARIABLE_REFERENCE_VALUE_H_
