// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_BASE_PARSE_NUMBER_H_
#define NET_BASE_PARSE_NUMBER_H_

#include "base/strings/string_piece.h"
#include "net/base/net_export.h"

// This file contains utility functions for parsing numbers, in the context of
// network protocols.
//
// Q: Doesn't //base already provide these in string_number_conversions.h, with
//    functions like base::StringToInt()?
//
// A: Yes, and those functions are used under the hood by these implementations.
//
//    However using the base::StringTo*() has historically led to subtle bugs
//    in the context of parsing network protocols:
//
//      * Permitting a leading '+'
//      * Incorrectly classifying overflow/underflow from a parsing failure
//      * Allowing negative numbers for non-negative fields
//
//   This API tries to avoid these problems by picking sensible defaults for
//   //net code. For more details see crbug.com/596523.

namespace net {

// Format to use when parsing integers.
enum class ParseIntFormat {
  // Accepts non-negative base 10 integers of the form:
  //
  //    1*DIGIT
  //
  // This construction is used in a variety of IETF standards, such as RFC 7230
  // (HTTP).
  //
  // When attempting to parse a negative number using this format, the failure
  // will be FAILED_PARSE since it violated the expected format (and not
  // FAILED_UNDERFLOW).
  //
  // Also note that inputs need not be in minimal encoding: "0003" is valid and
  // equivalent to "3".
  NON_NEGATIVE,

  // Accept optionally negative base 10 integers of the form:
  //
  //    ["-"] 1*DIGIT
  //
  // In other words, this accepts the same things as NON_NEGATIVE, and
  // additionally recognizes those numbers prefixed with a '-'.
  //
  // Note that by this defintion "-0" IS a valid input.
  OPTIONALLY_NEGATIVE
};

// The specific reason why a ParseInt*() function failed.
enum class ParseIntError {
  // The parsed number couldn't fit into the provided output type because it was
  // too high.
  FAILED_OVERFLOW,

  // The parsed number couldn't fit into the provided output type because it was
  // too low.
  FAILED_UNDERFLOW,

  // The number failed to be parsed because it wasn't a valid decimal number (as
  // determined by the policy).
  FAILED_PARSE,
};

// The ParseInt*() functions parse a string representing a number.
//
// The format of the strings that are accepted is controlled by the |format|
// parameter. This allows rejecting negative numbers.
//
// These functions return true on success, and fill |*output| with the result.
//
// On failure, it is guaranteed that |*output| was not modified. If
// |optional_error| was non-null, then it is filled with the reason for the
// failure.
[[nodiscard]] NET_EXPORT bool ParseInt32(
    const base::StringPiece& input,
    ParseIntFormat format,
    int32_t* output,
    ParseIntError* optional_error = nullptr);

[[nodiscard]] NET_EXPORT bool ParseInt64(
    const base::StringPiece& input,
    ParseIntFormat format,
    int64_t* output,
    ParseIntError* optional_error = nullptr);

// The ParseUint*() functions parse a string representing a number.
//
// These are equivalent to calling ParseInt*() with a format string of
// ParseIntFormat::NON_NEGATIVE and unsigned output types.
[[nodiscard]] NET_EXPORT bool ParseUint32(
    const base::StringPiece& input,
    uint32_t* output,
    ParseIntError* optional_error = nullptr);

[[nodiscard]] NET_EXPORT bool ParseUint64(
    const base::StringPiece& input,
    uint64_t* output,
    ParseIntError* optional_error = nullptr);

}  // namespace net

#endif  // NET_BASE_PARSE_NUMBER_H_
