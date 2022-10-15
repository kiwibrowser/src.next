// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include "net/http/http_auth_challenge_tokenizer.h"
#include "net/http/http_util.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  std::string input(reinterpret_cast<const char*>(data), size);
  net::HttpAuthChallengeTokenizer tokenizer(input.begin(), input.end());
  net::HttpUtil::NameValuePairsIterator parameters = tokenizer.param_pairs();
  while (parameters.GetNext()) {
  }
  tokenizer.base64_param();
  return 0;
}
