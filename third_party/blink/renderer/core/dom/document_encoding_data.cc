/*
 * Copyright (C) 2013 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "third_party/blink/renderer/core/dom/document_encoding_data.h"

#include "third_party/blink/public/platform/web_encoding_data.h"
#include "third_party/blink/renderer/core/html/parser/text_resource_decoder.h"

namespace blink {

DocumentEncodingData::DocumentEncodingData()
    : encoding_(UTF8Encoding()),
      was_detected_heuristically_(false),
      saw_decoding_error_(false) {}

DocumentEncodingData::DocumentEncodingData(const TextResourceDecoder& decoder) {
  encoding_ = decoder.Encoding();
  was_detected_heuristically_ = decoder.EncodingWasDetectedHeuristically();
  saw_decoding_error_ = decoder.SawError();
}

DocumentEncodingData::DocumentEncodingData(const WebEncodingData& data)
    : encoding_(data.encoding),
      was_detected_heuristically_(data.was_detected_heuristically),
      saw_decoding_error_(data.saw_decoding_error) {}

void DocumentEncodingData::SetEncoding(const WTF::TextEncoding& encoding) {
  encoding_ = encoding;
}

}  // namespace blink
