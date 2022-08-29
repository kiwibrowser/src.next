// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_CSS_CSS_PATH_VALUE_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_CSS_CSS_PATH_VALUE_H_

#include <memory>
#include "base/memory/scoped_refptr.h"
#include "third_party/blink/renderer/core/css/css_value.h"
#include "third_party/blink/renderer/core/style/style_path.h"
#include "third_party/blink/renderer/core/svg/svg_path_byte_stream.h"
#include "third_party/blink/renderer/core/svg/svg_path_utilities.h"
#include "third_party/blink/renderer/platform/wtf/casting.h"

namespace blink {

class StylePath;

namespace cssvalue {

class CSSPathValue : public CSSValue {
 public:
  static const CSSPathValue& EmptyPathValue();

  explicit CSSPathValue(scoped_refptr<StylePath>,
                        PathSerializationFormat = kNoTransformation);
  explicit CSSPathValue(std::unique_ptr<SVGPathByteStream>,
                        WindRule wind_rule = RULE_NONZERO,
                        PathSerializationFormat = kNoTransformation);

  StylePath* GetStylePath() const { return style_path_.get(); }
  String CustomCSSText() const;

  bool Equals(const CSSPathValue&) const;

  void TraceAfterDispatch(blink::Visitor*) const;

  const SVGPathByteStream& ByteStream() const {
    return style_path_->ByteStream();
  }

 private:
  scoped_refptr<StylePath> style_path_;
  const PathSerializationFormat serialization_format_;
};

}  // namespace cssvalue

template <>
struct DowncastTraits<cssvalue::CSSPathValue> {
  static bool AllowFrom(const CSSValue& value) { return value.IsPathValue(); }
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_CORE_CSS_CSS_PATH_VALUE_H_
