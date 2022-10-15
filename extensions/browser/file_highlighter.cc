// Copyright 2013 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "extensions/browser/file_highlighter.h"

#include "base/containers/stack.h"
#include "base/values.h"

namespace extensions {

namespace {

// Keys for a highlighted dictionary.
const char kBeforeHighlightKey[] = "beforeHighlight";
const char kHighlightKey[] = "highlight";
const char kAfterHighlightKey[] = "afterHighlight";

// Increment |index| to the position of the next quote ('"') in |str|, skipping
// over any escaped quotes. If no next quote is found, |index| is set to
// std::string::npos. Assumes |index| currently points to a quote.
void QuoteIncrement(const std::string& str, size_t* index) {
  size_t i = *index + 1;  // Skip over the first quote.
  bool found = false;
  while (!found && i < str.size()) {
    if (str[i] == '\\')
      i += 2;  // if we find an escaped character, skip it.
    else if (str[i] == '"')
      found = true;
    else
      ++i;
  }
  *index = found ? i : std::string::npos;
}

// Increment |index| by one if the next character is not a comment. Increment
// index until the end of the comment if it is a comment.
void CommentSafeIncrement(const std::string& str, size_t* index) {
  size_t i = *index;
  if (str[i] == '/' && i + 1 < str.size()) {
    // Eat a single-line comment.
    if (str[i + 1] == '/') {
      i += 2;  // Eat the '//'.
      while (i < str.size() && str[i] != '\n' && str[i] != '\r')
        ++i;
    } else if (str[i + 1] == '*') {  // Eat a multi-line comment.
      i += 3;  // Advance to the first possible comment end.
      while (i < str.size() && !(str[i - 1] == '*' && str[i] == '/'))
        ++i;
    }
  }
  *index = i + 1;
}

// Increment index until the end of the current "chunk"; a "chunk" is a JSON-
// style list, object, or string literal, without exceeding |end|. Assumes
// |index| currently points to a chunk's starting character ('{', '[', or '"').
void ChunkIncrement(const std::string& str, size_t* index, size_t end) {
  char c = str[*index];
  base::stack<char> stack;
  do {
    if (c == '"')
      QuoteIncrement(str, index);
    else if (c == '[')
      stack.push(']');
    else if (c == '{')
      stack.push('}');
    else if (!stack.empty() && c == stack.top())
      stack.pop();
    CommentSafeIncrement(str, index);
    c = str[*index];
  } while (!stack.empty() && *index < end);
}

}  // namespace

FileHighlighter::FileHighlighter(const std::string& contents)
    : contents_(contents), start_(0u), end_(contents_.size()) {
}

FileHighlighter::~FileHighlighter() {
}

std::string FileHighlighter::GetBeforeFeature() const {
  return contents_.substr(0, start_);
}

std::string FileHighlighter::GetFeature() const {
  return contents_.substr(start_, end_ - start_);
}

std::string FileHighlighter::GetAfterFeature() const {
  return contents_.substr(end_);
}

void FileHighlighter::SetHighlightedRegions(base::DictionaryValue* dict) const {
  std::string before_feature = GetBeforeFeature();
  if (!before_feature.empty())
    dict->SetStringKey(kBeforeHighlightKey, before_feature);

  std::string feature = GetFeature();
  if (!feature.empty())
    dict->SetStringKey(kHighlightKey, feature);

  std::string after_feature = GetAfterFeature();
  if (!after_feature.empty())
    dict->SetStringKey(kAfterHighlightKey, after_feature);
}

ManifestHighlighter::ManifestHighlighter(const std::string& manifest,
                                         const std::string& key,
                                         const std::string& specific)
    : FileHighlighter(manifest) {
  start_ = contents_.find('{');
  start_ = start_ == std::string::npos ? contents_.size() : start_ + 1;
  end_ = contents_.rfind('}');
  end_ = end_ == std::string::npos ? contents_.size() : end_;
  Parse(key, specific);
}

ManifestHighlighter::~ManifestHighlighter() {
}


void ManifestHighlighter::Parse(const std::string& key,
                                const std::string& specific) {
  // First, try to find the bounds of the full key.
  if (FindBounds(key, true) /* enforce at top level */) {
    // If we succeed, and we have a specific location, find the bounds of the
    // specific.
    if (!specific.empty())
      FindBounds(specific, false /* don't enforce at top level */);

    // We may have found trailing whitespace. Don't use base::TrimWhitespace,
    // because we want to keep any whitespace we find - just not highlight it.
    size_t trim = contents_.find_last_not_of(" \t\n\r", end_ - 1);
    if (trim < end_ && trim > start_)
      end_ = trim + 1;
  } else {
    // If we fail, then we set start to end so that the highlighted portion is
    // empty.
    start_ = end_;
  }
}

bool ManifestHighlighter::FindBounds(const std::string& feature,
                                     bool enforce_at_top_level) {
  char c = '\0';
  while (start_ < end_) {
    c = contents_[start_];
    if (c == '"') {
      // The feature may be quoted.
      size_t quote_end = start_;
      QuoteIncrement(contents_, &quote_end);
      if (contents_.substr(start_ + 1, quote_end - 1 - start_) == feature) {
        FindBoundsEnd(feature, quote_end + 1);
        return true;
      } else {
        // If it's not the feature, then we can skip the quoted section.
        start_ = quote_end + 1;
      }
    } else if (contents_.substr(start_, feature.size()) == feature) {
        FindBoundsEnd(feature, start_ + feature.size() + 1);
        return true;
    } else if (enforce_at_top_level && (c == '{' || c == '[')) {
      // If we don't have to be at the top level, then we can skip any chunks
      // we find.
      ChunkIncrement(contents_, &start_, end_);
    } else {
      CommentSafeIncrement(contents_, &start_);
    }
  }
  return false;
}

void ManifestHighlighter::FindBoundsEnd(const std::string& feature,
                                        size_t local_start) {
  char c = '\0';
  while (local_start < end_) {
    c = contents_[local_start];
    // We're done when we find a terminating character (i.e., either a comma or
    // an ending bracket.
    if (c == ',' || c == '}' || c == ']') {
      end_ = local_start;
      return;
    }
    // We can skip any chunks we find, since we are looking for the end of the
    // current feature, and don't want to go any deeper.
    if (c == '"' || c == '{' || c == '[')
      ChunkIncrement(contents_, &local_start, end_);
    else
      CommentSafeIncrement(contents_, &local_start);
  }
}

SourceHighlighter::SourceHighlighter(const std::string& contents,
                                     size_t line_number)
    : FileHighlighter(contents) {
  Parse(line_number);
}

SourceHighlighter::~SourceHighlighter() {
}

void SourceHighlighter::Parse(size_t line_number) {
  // If line 0 is requested, highlight nothing.
  if (line_number == 0) {
    start_ = contents_.size();
    return;
  }

  for (size_t i = 1; i < line_number; ++i) {
    start_ = contents_.find('\n', start_);
    if (start_ == std::string::npos)
      break;
    start_ += 1;
  }

  end_ = contents_.find('\n', start_);

  // If we went off the end of the string (i.e., the line number was invalid),
  // then move start and end to the end of the string, so that the highlighted
  // portion is empty.
  start_ = start_ == std::string::npos ? contents_.size() : start_;
  end_ = end_ == std::string::npos ? contents_.size() : end_;
}

}  // namespace extensions
