// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/values.h"

// values.h is a widely included header and its size has significant impact on
// build time. Try not to raise this limit unless absolutely necessary. See
// https://chromium.googlesource.com/chromium/src/+/HEAD/docs/wmax_tokens.md
#ifndef NACL_TC_REV
#pragma clang max_tokens_here 580000
#endif

#include <algorithm>
#include <cmath>
#include <ostream>
#include <tuple>
#include <utility>

#include "base/as_const.h"
#include "base/bit_cast.h"
#include "base/check_op.h"
#include "base/containers/checked_iterators.h"
#include "base/containers/cxx20_erase_vector.h"
#include "base/cxx17_backports.h"
#include "base/cxx20_to_address.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/memory/ptr_util.h"
#include "base/notreached.h"
#include "base/ranges/algorithm.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/trace_event/base_tracing.h"
#include "base/tracing_buildflags.h"
#include "third_party/abseil-cpp/absl/types/optional.h"
#include "third_party/abseil-cpp/absl/types/variant.h"

#if BUILDFLAG(ENABLE_BASE_TRACING)
#include "base/trace_event/memory_usage_estimator.h"  // no-presubmit-check
#endif  // BUILDFLAG(ENABLE_BASE_TRACING)

namespace base {

namespace {

const char* const kTypeNames[] = {"null",   "boolean", "integer",    "double",
                                  "string", "binary",  "dictionary", "list"};
static_assert(std::size(kTypeNames) ==
                  static_cast<size_t>(Value::Type::LIST) + 1,
              "kTypeNames Has Wrong Size");

std::unique_ptr<Value> CopyWithoutEmptyChildren(const Value& node);

// Make a deep copy of |node|, but don't include empty lists or dictionaries
// in the copy. It's possible for this function to return NULL and it
// expects |node| to always be non-NULL.
std::unique_ptr<Value> CopyListWithoutEmptyChildren(const Value& list) {
  Value copy(Value::Type::LIST);
  for (const auto& entry : list.GetListDeprecated()) {
    std::unique_ptr<Value> child_copy = CopyWithoutEmptyChildren(entry);
    if (child_copy)
      copy.Append(std::move(*child_copy));
  }
  return copy.GetListDeprecated().empty()
             ? nullptr
             : std::make_unique<Value>(std::move(copy));
}

std::unique_ptr<DictionaryValue> CopyDictionaryWithoutEmptyChildren(
    const DictionaryValue& dict) {
  std::unique_ptr<DictionaryValue> copy;
  for (auto it : dict.DictItems()) {
    std::unique_ptr<Value> child_copy = CopyWithoutEmptyChildren(it.second);
    if (child_copy) {
      if (!copy)
        copy = std::make_unique<DictionaryValue>();
      copy->SetKey(it.first, std::move(*child_copy));
    }
  }
  return copy;
}

std::unique_ptr<Value> CopyWithoutEmptyChildren(const Value& node) {
  switch (node.type()) {
    case Value::Type::LIST:
      return CopyListWithoutEmptyChildren(static_cast<const ListValue&>(node));

    case Value::Type::DICTIONARY:
      return CopyDictionaryWithoutEmptyChildren(
          static_cast<const DictionaryValue&>(node));

    default:
      return std::make_unique<Value>(node.Clone());
  }
}

// Helper class to enumerate the path components from a StringPiece
// without performing heap allocations. Components are simply separated
// by single dots (e.g. "foo.bar.baz"  -> ["foo", "bar", "baz"]).
//
// Usage example:
//    PathSplitter splitter(some_path);
//    while (splitter.HasNext()) {
//       StringPiece component = splitter.Next();
//       ...
//    }
//
class PathSplitter {
 public:
  explicit PathSplitter(StringPiece path) : path_(path) {}

  bool HasNext() const { return pos_ < path_.size(); }

  StringPiece Next() {
    DCHECK(HasNext());
    size_t start = pos_;
    size_t pos = path_.find('.', start);
    size_t end;
    if (pos == path_.npos) {
      end = path_.size();
      pos_ = end;
    } else {
      end = pos;
      pos_ = pos + 1;
    }
    return path_.substr(start, end - start);
  }

 private:
  StringPiece path_;
  size_t pos_ = 0;
};

std::string DebugStringImpl(ValueView value) {
  std::string json;
  JSONWriter::WriteWithOptions(value, JSONWriter::OPTIONS_PRETTY_PRINT, &json);
  return json;
}

}  // namespace

// static
Value Value::FromUniquePtrValue(std::unique_ptr<Value> val) {
  return std::move(*val);
}

// static
std::unique_ptr<Value> Value::ToUniquePtrValue(Value val) {
  return std::make_unique<Value>(std::move(val));
}

// static
const DictionaryValue& Value::AsDictionaryValue(const Value& val) {
  CHECK(val.is_dict());
  return static_cast<const DictionaryValue&>(val);
}

// static
const ListValue& Value::AsListValue(const Value& val) {
  CHECK(val.is_list());
  return static_cast<const ListValue&>(val);
}

Value::Value() noexcept = default;

Value::Value(Value&&) noexcept = default;

Value& Value::operator=(Value&&) noexcept = default;

Value::Value(Type type) {
  // Initialize with the default value.
  switch (type) {
    case Type::NONE:
      return;

    case Type::BOOLEAN:
      data_.emplace<bool>(false);
      return;
    case Type::INTEGER:
      data_.emplace<int>(0);
      return;
    case Type::DOUBLE:
      data_.emplace<DoubleStorage>(0.0);
      return;
    case Type::STRING:
      data_.emplace<std::string>();
      return;
    case Type::BINARY:
      data_.emplace<BlobStorage>();
      return;
    case Type::DICTIONARY:
      data_.emplace<Dict>();
      return;
    case Type::LIST:
      data_.emplace<List>();
      return;
  }

  CHECK(false);
}

Value::Value(bool value) : data_(value) {}

Value::Value(int value) : data_(value) {}

Value::Value(double value)
    : data_(absl::in_place_type_t<DoubleStorage>(), value) {}

Value::Value(StringPiece value) : Value(std::string(value)) {}

Value::Value(StringPiece16 value) : Value(UTF16ToUTF8(value)) {}

Value::Value(const char* value) : Value(std::string(value)) {}

Value::Value(const char16_t* value) : Value(UTF16ToUTF8(value)) {}

Value::Value(std::string&& value) noexcept : data_(std::move(value)) {
  DCHECK(IsStringUTF8AllowingNoncharacters(GetString()));
}

Value::Value(const std::vector<char>& value)
    : data_(absl::in_place_type_t<BlobStorage>(), value.begin(), value.end()) {}

Value::Value(base::span<const uint8_t> value)
    : data_(absl::in_place_type_t<BlobStorage>(), value.size()) {
  // This is 100x faster than using the "range" constructor for a 512k blob:
  // crbug.com/1343636
  std::copy(value.begin(), value.end(), absl::get<BlobStorage>(data_).data());
}

Value::Value(BlobStorage&& value) noexcept : data_(std::move(value)) {}

Value::Value(Dict&& value) noexcept : data_(std::move(value)) {}

Value::Value(List&& value) noexcept : data_(std::move(value)) {}

Value::Value(span<const Value> value) : data_(absl::in_place_type_t<List>()) {
  list().reserve(value.size());
  for (const auto& val : value)
    list().emplace_back(val.Clone());
}

Value::Value(ListStorage&& value) noexcept
    : data_(absl::in_place_type_t<List>()) {
  list() = std::move(value);
}

Value::Value(const LegacyDictStorage& storage)
    : data_(absl::in_place_type_t<Dict>()) {
  dict().reserve(storage.size());
  for (const auto& it : storage) {
    dict().try_emplace(dict().end(), it.first,
                       std::make_unique<Value>(it.second->Clone()));
  }
}

Value::Value(LegacyDictStorage&& storage) noexcept
    : data_(absl::in_place_type_t<Dict>()) {
  dict() = std::move(storage);
}

Value::Value(absl::monostate) {}

Value::Value(DoubleStorage storage) : data_(std::move(storage)) {}

Value::DoubleStorage::DoubleStorage(double v) : v_(bit_cast<decltype(v_)>(v)) {
  if (!std::isfinite(v)) {
    NOTREACHED() << "Non-finite (i.e. NaN or positive/negative infinity) "
                 << "values cannot be represented in JSON";
    v_ = bit_cast<decltype(v_)>(0.0);
  }
}

Value Value::Clone() const {
  return absl::visit(
      [](const auto& member) {
        using T = std::decay_t<decltype(member)>;
        if constexpr (std::is_same_v<T, Dict> || std::is_same_v<T, List>) {
          return Value(member.Clone());
        } else {
          return Value(member);
        }
      },
      data_);
}

Value::~Value() = default;

// static
const char* Value::GetTypeName(Value::Type type) {
  DCHECK_GE(static_cast<int>(type), 0);
  DCHECK_LT(static_cast<size_t>(type), std::size(kTypeNames));
  return kTypeNames[static_cast<size_t>(type)];
}

absl::optional<bool> Value::GetIfBool() const {
  return is_bool() ? absl::make_optional(GetBool()) : absl::nullopt;
}

absl::optional<int> Value::GetIfInt() const {
  return is_int() ? absl::make_optional(GetInt()) : absl::nullopt;
}

absl::optional<double> Value::GetIfDouble() const {
  return (is_int() || is_double()) ? absl::make_optional(GetDouble())
                                   : absl::nullopt;
}

const std::string* Value::GetIfString() const {
  return absl::get_if<std::string>(&data_);
}

std::string* Value::GetIfString() {
  return absl::get_if<std::string>(&data_);
}

const Value::BlobStorage* Value::GetIfBlob() const {
  return absl::get_if<BlobStorage>(&data_);
}

const Value::Dict* Value::GetIfDict() const {
  return absl::get_if<Dict>(&data_);
}

Value::Dict* Value::GetIfDict() {
  return absl::get_if<Dict>(&data_);
}

const Value::List* Value::GetIfList() const {
  return absl::get_if<List>(&data_);
}

Value::List* Value::GetIfList() {
  return absl::get_if<List>(&data_);
}

bool Value::GetBool() const {
  return absl::get<bool>(data_);
}

int Value::GetInt() const {
  return absl::get<int>(data_);
}

double Value::GetDouble() const {
  if (is_double())
    return absl::get<DoubleStorage>(data_);
  if (is_int())
    return GetInt();
  CHECK(false);
  return 0.0;
}

const std::string& Value::GetString() const {
  return absl::get<std::string>(data_);
}

std::string& Value::GetString() {
  return absl::get<std::string>(data_);
}

const Value::BlobStorage& Value::GetBlob() const {
  return absl::get<BlobStorage>(data_);
}

const Value::Dict& Value::GetDict() const {
  return absl::get<Dict>(data_);
}

Value::Dict& Value::GetDict() {
  return absl::get<Dict>(data_);
}

const Value::List& Value::GetList() const {
  return absl::get<List>(data_);
}

Value::List& Value::GetList() {
  return absl::get<List>(data_);
}

Value::Dict::Dict() = default;

Value::Dict::Dict(Dict&&) noexcept = default;

Value::Dict& Value::Dict::operator=(Dict&&) noexcept = default;

Value::Dict::~Dict() = default;

bool Value::Dict::empty() const {
  return storage_.empty();
}

size_t Value::Dict::size() const {
  return storage_.size();
}

Value::Dict::iterator Value::Dict::begin() {
  return iterator(storage_.begin());
}

Value::Dict::const_iterator Value::Dict::begin() const {
  return const_iterator(storage_.begin());
}

Value::Dict::const_iterator Value::Dict::cbegin() const {
  return const_iterator(storage_.cbegin());
}

Value::Dict::iterator Value::Dict::end() {
  return iterator(storage_.end());
}

Value::Dict::const_iterator Value::Dict::end() const {
  return const_iterator(storage_.end());
}

Value::Dict::const_iterator Value::Dict::cend() const {
  return const_iterator(storage_.cend());
}

bool Value::Dict::contains(base::StringPiece key) const {
  DCHECK(IsStringUTF8AllowingNoncharacters(key));

  return storage_.contains(key);
}

void Value::Dict::clear() {
  return storage_.clear();
}

Value::Dict::iterator Value::Dict::erase(iterator pos) {
  return iterator(storage_.erase(pos.GetUnderlyingIteratorDoNotUse()));
}

Value::Dict::iterator Value::Dict::erase(const_iterator pos) {
  return iterator(storage_.erase(pos.GetUnderlyingIteratorDoNotUse()));
}

Value::Dict Value::Dict::Clone() const {
  return Dict(storage_);
}

void Value::Dict::Merge(Dict dict) {
  for (const auto [key, value] : dict) {
    if (Dict* nested_dict = value.GetIfDict()) {
      if (Dict* current_dict = FindDict(key)) {
        // If `key` is a nested dictionary in this dictionary and the dictionary
        // being merged, recursively merge the two dictionaries.
        current_dict->Merge(std::move(*nested_dict));
        continue;
      }
    }

    // Otherwise, unconditionally set the value, overwriting any value that may
    // already be associated with the key.
    Set(key, std::move(value));
  }
}

const Value* Value::Dict::Find(StringPiece key) const {
  DCHECK(IsStringUTF8AllowingNoncharacters(key));

  auto it = storage_.find(key);
  return it != storage_.end() ? it->second.get() : nullptr;
}

Value* Value::Dict::Find(StringPiece key) {
  auto it = storage_.find(key);
  return it != storage_.end() ? it->second.get() : nullptr;
}

absl::optional<bool> Value::Dict::FindBool(StringPiece key) const {
  const Value* v = Find(key);
  return v ? v->GetIfBool() : absl::nullopt;
}

absl::optional<int> Value::Dict::FindInt(StringPiece key) const {
  const Value* v = Find(key);
  return v ? v->GetIfInt() : absl::nullopt;
}

absl::optional<double> Value::Dict::FindDouble(StringPiece key) const {
  const Value* v = Find(key);
  return v ? v->GetIfDouble() : absl::nullopt;
}

const std::string* Value::Dict::FindString(StringPiece key) const {
  const Value* v = Find(key);
  return v ? v->GetIfString() : nullptr;
}

std::string* Value::Dict::FindString(StringPiece key) {
  Value* v = Find(key);
  return v ? v->GetIfString() : nullptr;
}

const Value::BlobStorage* Value::Dict::FindBlob(StringPiece key) const {
  const Value* v = Find(key);
  return v ? v->GetIfBlob() : nullptr;
}

const Value::Dict* Value::Dict::FindDict(StringPiece key) const {
  const Value* v = Find(key);
  return v ? v->GetIfDict() : nullptr;
}

Value::Dict* Value::Dict::FindDict(StringPiece key) {
  Value* v = Find(key);
  return v ? v->GetIfDict() : nullptr;
}

const Value::List* Value::Dict::FindList(StringPiece key) const {
  const Value* v = Find(key);
  return v ? v->GetIfList() : nullptr;
}

Value::List* Value::Dict::FindList(StringPiece key) {
  Value* v = Find(key);
  return v ? v->GetIfList() : nullptr;
}

Value* Value::Dict::Set(StringPiece key, Value&& value) {
  DCHECK(IsStringUTF8AllowingNoncharacters(key));

  auto wrapped_value = std::make_unique<Value>(std::move(value));
  auto* raw_value = wrapped_value.get();
  storage_.insert_or_assign(key, std::move(wrapped_value));
  return raw_value;
}

Value* Value::Dict::Set(StringPiece key, bool value) {
  return Set(key, Value(value));
}

Value* Value::Dict::Set(StringPiece key, int value) {
  return Set(key, Value(value));
}

Value* Value::Dict::Set(StringPiece key, double value) {
  return Set(key, Value(value));
}

Value* Value::Dict::Set(StringPiece key, StringPiece value) {
  return Set(key, Value(value));
}

Value* Value::Dict::Set(StringPiece key, StringPiece16 value) {
  return Set(key, Value(value));
}

Value* Value::Dict::Set(StringPiece key, const char* value) {
  return Set(key, Value(value));
}

Value* Value::Dict::Set(StringPiece key, const char16_t* value) {
  return Set(key, Value(value));
}

Value* Value::Dict::Set(StringPiece key, std::string&& value) {
  return Set(key, Value(std::move(value)));
}

Value* Value::Dict::Set(StringPiece key, BlobStorage&& value) {
  return Set(key, Value(std::move(value)));
}

Value* Value::Dict::Set(StringPiece key, Dict&& value) {
  return Set(key, Value(std::move(value)));
}

Value* Value::Dict::Set(StringPiece key, List&& value) {
  return Set(key, Value(std::move(value)));
}

bool Value::Dict::Remove(StringPiece key) {
  DCHECK(IsStringUTF8AllowingNoncharacters(key));

  return storage_.erase(key) > 0;
}

absl::optional<Value> Value::Dict::Extract(StringPiece key) {
  DCHECK(IsStringUTF8AllowingNoncharacters(key));

  auto it = storage_.find(key);
  if (it == storage_.end())
    return absl::nullopt;
  Value v = std::move(*it->second);
  storage_.erase(it);
  return v;
}

const Value* Value::Dict::FindByDottedPath(StringPiece path) const {
  DCHECK(!path.empty());
  DCHECK(IsStringUTF8AllowingNoncharacters(path));

  const Dict* current_dict = this;
  const Value* current_value = nullptr;
  PathSplitter splitter(path);
  while (true) {
    current_value = current_dict->Find(splitter.Next());
    if (!splitter.HasNext()) {
      return current_value;
    }
    if (!current_value) {
      return nullptr;
    }
    current_dict = current_value->GetIfDict();
    if (!current_dict) {
      return nullptr;
    }
  }
}

Value* Value::Dict::FindByDottedPath(StringPiece path) {
  return const_cast<Value*>(as_const(*this).FindByDottedPath(path));
}

absl::optional<bool> Value::Dict::FindBoolByDottedPath(StringPiece path) const {
  const Value* v = FindByDottedPath(path);
  return v ? v->GetIfBool() : absl::nullopt;
}

absl::optional<int> Value::Dict::FindIntByDottedPath(StringPiece path) const {
  const Value* v = FindByDottedPath(path);
  return v ? v->GetIfInt() : absl::nullopt;
}

absl::optional<double> Value::Dict::FindDoubleByDottedPath(
    StringPiece path) const {
  const Value* v = FindByDottedPath(path);
  return v ? v->GetIfDouble() : absl::nullopt;
}

const std::string* Value::Dict::FindStringByDottedPath(StringPiece path) const {
  const Value* v = FindByDottedPath(path);
  return v ? v->GetIfString() : nullptr;
}

std::string* Value::Dict::FindStringByDottedPath(StringPiece path) {
  Value* v = FindByDottedPath(path);
  return v ? v->GetIfString() : nullptr;
}

const Value::BlobStorage* Value::Dict::FindBlobByDottedPath(
    StringPiece path) const {
  const Value* v = FindByDottedPath(path);
  return v ? v->GetIfBlob() : nullptr;
}

const Value::Dict* Value::Dict::FindDictByDottedPath(StringPiece path) const {
  const Value* v = FindByDottedPath(path);
  return v ? v->GetIfDict() : nullptr;
}

Value::Dict* Value::Dict::FindDictByDottedPath(StringPiece path) {
  Value* v = FindByDottedPath(path);
  return v ? v->GetIfDict() : nullptr;
}

const Value::List* Value::Dict::FindListByDottedPath(StringPiece path) const {
  const Value* v = FindByDottedPath(path);
  return v ? v->GetIfList() : nullptr;
}

Value::List* Value::Dict::FindListByDottedPath(StringPiece path) {
  Value* v = FindByDottedPath(path);
  return v ? v->GetIfList() : nullptr;
}

Value* Value::Dict::SetByDottedPath(StringPiece path, Value&& value) {
  DCHECK(!path.empty());
  DCHECK(IsStringUTF8AllowingNoncharacters(path));

  Dict* current_dict = this;
  Value* current_value = nullptr;
  PathSplitter splitter(path);
  while (true) {
    StringPiece next_key = splitter.Next();
    if (!splitter.HasNext()) {
      return current_dict->Set(next_key, std::move(value));
    }
    // This could be clever to avoid a double-lookup via use of lower_bound(),
    // but for now, just implement it the most straightforward way.
    current_value = current_dict->Find(next_key);
    if (current_value) {
      // Unlike the legacy DictionaryValue API, encountering an intermediate
      // node that is not a `Value::Type::DICT` is an error.
      current_dict = current_value->GetIfDict();
      if (!current_dict) {
        return nullptr;
      }
    } else {
      current_dict = &current_dict->Set(next_key, Dict())->GetDict();
    }
  }
}

Value* Value::Dict::SetByDottedPath(StringPiece path, bool value) {
  return SetByDottedPath(path, Value(value));
}

Value* Value::Dict::SetByDottedPath(StringPiece path, int value) {
  return SetByDottedPath(path, Value(value));
}

Value* Value::Dict::SetByDottedPath(StringPiece path, double value) {
  return SetByDottedPath(path, Value(value));
}

Value* Value::Dict::SetByDottedPath(StringPiece path, StringPiece value) {
  return SetByDottedPath(path, Value(value));
}

Value* Value::Dict::SetByDottedPath(StringPiece path, StringPiece16 value) {
  return SetByDottedPath(path, Value(value));
}

Value* Value::Dict::SetByDottedPath(StringPiece path, const char* value) {
  return SetByDottedPath(path, Value(value));
}

Value* Value::Dict::SetByDottedPath(StringPiece path, const char16_t* value) {
  return SetByDottedPath(path, Value(value));
}

Value* Value::Dict::SetByDottedPath(StringPiece path, std::string&& value) {
  return SetByDottedPath(path, Value(std::move(value)));
}

Value* Value::Dict::SetByDottedPath(StringPiece path, BlobStorage&& value) {
  return SetByDottedPath(path, Value(std::move(value)));
}

Value* Value::Dict::SetByDottedPath(StringPiece path, Dict&& value) {
  return SetByDottedPath(path, Value(std::move(value)));
}

Value* Value::Dict::SetByDottedPath(StringPiece path, List&& value) {
  return SetByDottedPath(path, Value(std::move(value)));
}

bool Value::Dict::RemoveByDottedPath(StringPiece path) {
  return ExtractByDottedPath(path).has_value();
}

absl::optional<Value> Value::Dict::ExtractByDottedPath(StringPiece path) {
  DCHECK(!path.empty());
  DCHECK(IsStringUTF8AllowingNoncharacters(path));

  // Use recursion instead of PathSplitter here, as it simplifies code for
  // removing dictionaries that become empty if a value matching `path` is
  // extracted.
  size_t dot_index = path.find('.');
  if (dot_index == StringPiece::npos) {
    return Extract(path);
  }
  // This could be clever to avoid a double-lookup by using storage_ directly,
  // but for now, just implement it in the most straightforward way.
  StringPiece next_key = path.substr(0, dot_index);
  auto* next_dict = FindDict(next_key);
  if (!next_dict) {
    return absl::nullopt;
  }
  absl::optional<Value> extracted =
      next_dict->ExtractByDottedPath(path.substr(dot_index + 1));
  if (extracted && next_dict->empty()) {
    Remove(next_key);
  }
  return extracted;
}

std::string Value::Dict::DebugString() const {
  return DebugStringImpl(*this);
}

#if BUILDFLAG(ENABLE_BASE_TRACING)
void Value::Dict::WriteIntoTrace(perfetto::TracedValue context) const {
  perfetto::TracedDictionary dict = std::move(context).WriteDictionary();
  for (auto kv : *this) {
    dict.Add(perfetto::DynamicString(kv.first), kv.second);
  }
}
#endif  // BUILDFLAG(ENABLE_BASE_TRACING)

Value::Dict::Dict(
    const flat_map<std::string, std::unique_ptr<Value>>& storage) {
  storage_.reserve(storage.size());
  for (const auto& [key, value] : storage) {
    Set(key, value->Clone());
  }
}

bool operator==(const Value::Dict& lhs, const Value::Dict& rhs) {
  auto deref_2nd = [](const auto& p) { return std::tie(p.first, *p.second); };
  return ranges::equal(lhs.storage_, rhs.storage_, {}, deref_2nd, deref_2nd);
}

bool operator!=(const Value::Dict& lhs, const Value::Dict& rhs) {
  return !(lhs == rhs);
}

bool operator<(const Value::Dict& lhs, const Value::Dict& rhs) {
  auto deref_2nd = [](const auto& p) { return std::tie(p.first, *p.second); };
  return ranges::lexicographical_compare(lhs.storage_, rhs.storage_, {},
                                         deref_2nd, deref_2nd);
}

bool operator>(const Value::Dict& lhs, const Value::Dict& rhs) {
  return rhs < lhs;
}

bool operator<=(const Value::Dict& lhs, const Value::Dict& rhs) {
  return !(rhs < lhs);
}

bool operator>=(const Value::Dict& lhs, const Value::Dict& rhs) {
  return !(lhs < rhs);
}

Value::List::List() = default;

Value::List::List(List&&) noexcept = default;

Value::List& Value::List::operator=(List&&) noexcept = default;

Value::List::~List() = default;

bool Value::List::empty() const {
  return storage_.empty();
}

size_t Value::List::size() const {
  return storage_.size();
}

Value::List::iterator Value::List::begin() {
  return iterator(base::to_address(storage_.begin()),
                  base::to_address(storage_.end()));
}

Value::List::const_iterator Value::List::begin() const {
  return const_iterator(base::to_address(storage_.begin()),
                        base::to_address(storage_.end()));
}

Value::List::const_iterator Value::List::cbegin() const {
  return const_iterator(base::to_address(storage_.cbegin()),
                        base::to_address(storage_.cend()));
}

Value::List::iterator Value::List::end() {
  return iterator(base::to_address(storage_.begin()),
                  base::to_address(storage_.end()),
                  base::to_address(storage_.end()));
}

Value::List::const_iterator Value::List::end() const {
  return const_iterator(base::to_address(storage_.begin()),
                        base::to_address(storage_.end()),
                        base::to_address(storage_.end()));
}

Value::List::const_iterator Value::List::cend() const {
  return const_iterator(base::to_address(storage_.cbegin()),
                        base::to_address(storage_.cend()),
                        base::to_address(storage_.cend()));
}

const Value& Value::List::front() const {
  CHECK(!storage_.empty());
  return storage_.front();
}

Value& Value::List::front() {
  CHECK(!storage_.empty());
  return storage_.front();
}

const Value& Value::List::back() const {
  CHECK(!storage_.empty());
  return storage_.back();
}

Value& Value::List::back() {
  CHECK(!storage_.empty());
  return storage_.back();
}

void Value::List::reserve(size_t capacity) {
  storage_.reserve(capacity);
}

const Value& Value::List::operator[](size_t index) const {
  CHECK_LT(index, storage_.size());
  return storage_[index];
}

Value& Value::List::operator[](size_t index) {
  CHECK_LT(index, storage_.size());
  return storage_[index];
}

void Value::List::clear() {
  storage_.clear();
}

Value::List::iterator Value::List::erase(iterator pos) {
  auto next_it = storage_.erase(storage_.begin() + (pos - begin()));
  return iterator(base::to_address(storage_.begin()), base::to_address(next_it),
                  base::to_address(storage_.end()));
}

Value::List::const_iterator Value::List::erase(const_iterator pos) {
  auto next_it = storage_.erase(storage_.begin() + (pos - begin()));
  return const_iterator(base::to_address(storage_.begin()),
                        base::to_address(next_it),
                        base::to_address(storage_.end()));
}

Value::List::iterator Value::List::erase(iterator first, iterator last) {
  auto next_it = storage_.erase(storage_.begin() + (first - begin()),
                                storage_.begin() + (last - begin()));
  return iterator(base::to_address(storage_.begin()), base::to_address(next_it),
                  base::to_address(storage_.end()));
}

Value::List::const_iterator Value::List::erase(const_iterator first,
                                               const_iterator last) {
  auto next_it = storage_.erase(storage_.begin() + (first - begin()),
                                storage_.begin() + (last - begin()));
  return const_iterator(base::to_address(storage_.begin()),
                        base::to_address(next_it),
                        base::to_address(storage_.end()));
}

Value::List Value::List::Clone() const {
  return List(storage_);
}

void Value::List::Append(Value&& value) {
  storage_.emplace_back(std::move(value));
}

void Value::List::Append(bool value) {
  storage_.emplace_back(value);
}

void Value::List::Append(int value) {
  storage_.emplace_back(value);
}

void Value::List::Append(double value) {
  storage_.emplace_back(value);
}

void Value::List::Append(StringPiece value) {
  Append(Value(value));
}

void Value::List::Append(StringPiece16 value) {
  storage_.emplace_back(value);
}

void Value::List::Append(const char* value) {
  storage_.emplace_back(value);
}

void Value::List::Append(const char16_t* value) {
  storage_.emplace_back(value);
}

void Value::List::Append(std::string&& value) {
  storage_.emplace_back(std::move(value));
}

void Value::List::Append(BlobStorage&& value) {
  storage_.emplace_back(std::move(value));
}

void Value::List::Append(Dict&& value) {
  storage_.emplace_back(std::move(value));
}

void Value::List::Append(List&& value) {
  storage_.emplace_back(std::move(value));
}

Value::List::iterator Value::List::Insert(const_iterator pos, Value&& value) {
  auto inserted_it =
      storage_.insert(storage_.begin() + (pos - begin()), std::move(value));
  return iterator(base::to_address(storage_.begin()),
                  base::to_address(inserted_it),
                  base::to_address(storage_.end()));
}

size_t Value::List::EraseValue(const Value& value) {
  return Erase(storage_, value);
}

std::string Value::List::DebugString() const {
  return DebugStringImpl(*this);
}

#if BUILDFLAG(ENABLE_BASE_TRACING)
void Value::List::WriteIntoTrace(perfetto::TracedValue context) const {
  perfetto::TracedArray array = std::move(context).WriteArray();
  for (const auto& item : *this) {
    array.Append(item);
  }
}
#endif  // BUILDFLAG(ENABLE_BASE_TRACING)

Value::List::List(const std::vector<Value>& storage) {
  storage_.reserve(storage.size());
  for (const auto& value : storage) {
    storage_.push_back(value.Clone());
  }
}

bool operator==(const Value::List& lhs, const Value::List& rhs) {
  return lhs.storage_ == rhs.storage_;
}

bool operator!=(const Value::List& lhs, const Value::List& rhs) {
  return !(lhs == rhs);
}

bool operator<(const Value::List& lhs, const Value::List& rhs) {
  return lhs.storage_ < rhs.storage_;
}

bool operator>(const Value::List& lhs, const Value::List& rhs) {
  return rhs < lhs;
}

bool operator<=(const Value::List& lhs, const Value::List& rhs) {
  return !(rhs < lhs);
}

bool operator>=(const Value::List& lhs, const Value::List& rhs) {
  return !(lhs < rhs);
}

Value::ListView Value::GetListDeprecated() {
  return list();
}

Value::ConstListView Value::GetListDeprecated() const {
  return list();
}

void Value::Append(bool value) {
  GetList().Append(value);
}

void Value::Append(int value) {
  GetList().Append(value);
}

void Value::Append(double value) {
  GetList().Append(value);
}

void Value::Append(const char* value) {
  GetList().Append(value);
}

void Value::Append(StringPiece value) {
  GetList().Append(value);
}

void Value::Append(std::string&& value) {
  GetList().Append(std::move(value));
}

void Value::Append(StringPiece16 value) {
  GetList().Append(value);
}

void Value::Append(Value&& value) {
  GetList().Append(std::move(value));
}

CheckedContiguousIterator<Value> Value::Insert(
    CheckedContiguousConstIterator<Value> pos,
    Value&& value) {
  return GetList().Insert(pos, std::move(value));
}

bool Value::EraseListIter(CheckedContiguousConstIterator<Value> iter) {
  const auto offset = iter - ListView(list()).begin();
  auto list_iter = list().begin() + offset;
  if (list_iter == list().end())
    return false;

  list().erase(list_iter);
  return true;
}

size_t Value::EraseListValue(const Value& val) {
  return GetList().EraseValue(val);
}

void Value::ClearList() {
  GetList().clear();
}

Value* Value::FindKey(StringPiece key) {
  return GetDict().Find(key);
}

const Value* Value::FindKey(StringPiece key) const {
  return GetDict().Find(key);
}

Value* Value::FindKeyOfType(StringPiece key, Type type) {
  return const_cast<Value*>(as_const(*this).FindKeyOfType(key, type));
}

const Value* Value::FindKeyOfType(StringPiece key, Type type) const {
  const Value* result = FindKey(key);
  if (!result || result->type() != type)
    return nullptr;
  return result;
}

absl::optional<bool> Value::FindBoolKey(StringPiece key) const {
  return GetDict().FindBool(key);
}

absl::optional<int> Value::FindIntKey(StringPiece key) const {
  return GetDict().FindInt(key);
}

absl::optional<double> Value::FindDoubleKey(StringPiece key) const {
  return GetDict().FindDouble(key);
}

const std::string* Value::FindStringKey(StringPiece key) const {
  return GetDict().FindString(key);
}

std::string* Value::FindStringKey(StringPiece key) {
  return GetDict().FindString(key);
}

const Value::BlobStorage* Value::FindBlobKey(StringPiece key) const {
  return GetDict().FindBlob(key);
}

const Value* Value::FindDictKey(StringPiece key) const {
  return FindKeyOfType(key, Type::DICTIONARY);
}

Value* Value::FindDictKey(StringPiece key) {
  return FindKeyOfType(key, Type::DICTIONARY);
}

const Value* Value::FindListKey(StringPiece key) const {
  return FindKeyOfType(key, Type::LIST);
}

Value* Value::FindListKey(StringPiece key) {
  return FindKeyOfType(key, Type::LIST);
}

Value* Value::SetKey(StringPiece key, Value&& value) {
  return GetDict().Set(key, std::move(value));
}

Value* Value::SetBoolKey(StringPiece key, bool value) {
  return GetDict().Set(key, value);
}

Value* Value::SetIntKey(StringPiece key, int value) {
  return GetDict().Set(key, value);
}

Value* Value::SetDoubleKey(StringPiece key, double value) {
  return GetDict().Set(key, value);
}

Value* Value::SetStringKey(StringPiece key, StringPiece value) {
  return GetDict().Set(key, value);
}

Value* Value::SetStringKey(StringPiece key, StringPiece16 value) {
  return GetDict().Set(key, value);
}

Value* Value::SetStringKey(StringPiece key, const char* value) {
  return GetDict().Set(key, value);
}

Value* Value::SetStringKey(StringPiece key, std::string&& value) {
  return GetDict().Set(key, std::move(value));
}

bool Value::RemoveKey(StringPiece key) {
  return GetDict().Remove(key);
}

absl::optional<Value> Value::ExtractKey(StringPiece key) {
  return GetDict().Extract(key);
}

Value* Value::FindPath(StringPiece path) {
  return GetDict().FindByDottedPath(path);
}

const Value* Value::FindPath(StringPiece path) const {
  return GetDict().FindByDottedPath(path);
}

Value* Value::FindPathOfType(StringPiece path, Type type) {
  return const_cast<Value*>(as_const(*this).FindPathOfType(path, type));
}

const Value* Value::FindPathOfType(StringPiece path, Type type) const {
  const Value* cur = FindPath(path);
  if (!cur || cur->type() != type)
    return nullptr;
  return cur;
}

absl::optional<bool> Value::FindBoolPath(StringPiece path) const {
  return GetDict().FindBoolByDottedPath(path);
}

absl::optional<int> Value::FindIntPath(StringPiece path) const {
  return GetDict().FindIntByDottedPath(path);
}

absl::optional<double> Value::FindDoublePath(StringPiece path) const {
  return GetDict().FindDoubleByDottedPath(path);
}

const std::string* Value::FindStringPath(StringPiece path) const {
  return GetDict().FindStringByDottedPath(path);
}

std::string* Value::FindStringPath(StringPiece path) {
  return GetDict().FindStringByDottedPath(path);
}

const Value* Value::FindDictPath(StringPiece path) const {
  return FindPathOfType(path, Type::DICTIONARY);
}

Value* Value::FindDictPath(StringPiece path) {
  return FindPathOfType(path, Type::DICTIONARY);
}

const Value* Value::FindListPath(StringPiece path) const {
  return FindPathOfType(path, Type::LIST);
}

Value* Value::FindListPath(StringPiece path) {
  return FindPathOfType(path, Type::LIST);
}

Value* Value::SetPath(StringPiece path, Value&& value) {
  return GetDict().SetByDottedPath(path, std::move(value));
}

Value* Value::SetBoolPath(StringPiece path, bool value) {
  return GetDict().SetByDottedPath(path, value);
}

Value* Value::SetIntPath(StringPiece path, int value) {
  return GetDict().SetByDottedPath(path, value);
}

Value* Value::SetDoublePath(StringPiece path, double value) {
  return GetDict().SetByDottedPath(path, value);
}

Value* Value::SetStringPath(StringPiece path, StringPiece value) {
  return GetDict().SetByDottedPath(path, value);
}

Value* Value::SetStringPath(StringPiece path, std::string&& value) {
  return GetDict().SetByDottedPath(path, std::move(value));
}

Value* Value::SetStringPath(StringPiece path, const char* value) {
  return GetDict().SetByDottedPath(path, value);
}

Value* Value::SetStringPath(StringPiece path, StringPiece16 value) {
  return GetDict().SetByDottedPath(path, value);
}

bool Value::RemovePath(StringPiece path) {
  return GetDict().RemoveByDottedPath(path);
}

absl::optional<Value> Value::ExtractPath(StringPiece path) {
  return GetDict().ExtractByDottedPath(path);
}

// DEPRECATED METHODS
Value* Value::FindPath(std::initializer_list<StringPiece> path) {
  return const_cast<Value*>(as_const(*this).FindPath(path));
}

Value* Value::FindPath(span<const StringPiece> path) {
  return const_cast<Value*>(as_const(*this).FindPath(path));
}

const Value* Value::FindPath(std::initializer_list<StringPiece> path) const {
  DCHECK_GE(path.size(), 2u) << "Use FindKey() for a path of length 1.";
  return FindPath(make_span(path.begin(), path.size()));
}

const Value* Value::FindPath(span<const StringPiece> path) const {
  const Value* cur = this;
  for (const StringPiece& component : path) {
    if (!cur->is_dict() || (cur = cur->FindKey(component)) == nullptr)
      return nullptr;
  }
  return cur;
}

Value* Value::FindPathOfType(std::initializer_list<StringPiece> path,
                             Type type) {
  return const_cast<Value*>(as_const(*this).FindPathOfType(path, type));
}

Value* Value::FindPathOfType(span<const StringPiece> path, Type type) {
  return const_cast<Value*>(as_const(*this).FindPathOfType(path, type));
}

const Value* Value::FindPathOfType(std::initializer_list<StringPiece> path,
                                   Type type) const {
  DCHECK_GE(path.size(), 2u) << "Use FindKeyOfType() for a path of length 1.";
  return FindPathOfType(make_span(path.begin(), path.size()), type);
}

const Value* Value::FindPathOfType(span<const StringPiece> path,
                                   Type type) const {
  const Value* result = FindPath(path);
  if (!result || result->type() != type)
    return nullptr;
  return result;
}

Value* Value::SetPath(std::initializer_list<StringPiece> path, Value&& value) {
  DCHECK_GE(path.size(), 2u) << "Use SetKey() for a path of length 1.";
  return SetPath(make_span(path.begin(), path.size()), std::move(value));
}

Value* Value::SetPath(span<const StringPiece> path, Value&& value) {
  DCHECK(path.begin() != path.end());  // Can't be empty path.

  // Walk/construct intermediate dictionaries. The last element requires
  // special handling so skip it in this loop.
  Value* cur = this;
  auto cur_path = path.begin();
  for (; (cur_path + 1) < path.end(); ++cur_path) {
    if (!cur->is_dict())
      return nullptr;

    // Use lower_bound to avoid doing the search twice for missing keys.
    const StringPiece path_component = *cur_path;
    auto found = cur->dict().lower_bound(path_component);
    if (found == cur->dict().end() || found->first != path_component) {
      // No key found, insert one.
      auto inserted = cur->dict().try_emplace(
          found, path_component, std::make_unique<Value>(Type::DICTIONARY));
      cur = inserted->second.get();
    } else {
      cur = found->second.get();
    }
  }

  // "cur" will now contain the last dictionary to insert or replace into.
  if (!cur->is_dict())
    return nullptr;
  return cur->SetKey(*cur_path, std::move(value));
}

Value::dict_iterator_proxy Value::DictItems() {
  return dict_iterator_proxy(&dict());
}

Value::const_dict_iterator_proxy Value::DictItems() const {
  return const_dict_iterator_proxy(&dict());
}

size_t Value::DictSize() const {
  return GetDict().size();
}

bool Value::DictEmpty() const {
  return GetDict().empty();
}

void Value::DictClear() {
  GetDict().clear();
}

void Value::MergeDictionary(const Value* dictionary) {
  return GetDict().Merge(dictionary->GetDict().Clone());
}

bool Value::GetAsDictionary(DictionaryValue** out_value) {
  if (out_value && is_dict()) {
    *out_value = static_cast<DictionaryValue*>(this);
    return true;
  }
  return is_dict();
}

bool Value::GetAsDictionary(const DictionaryValue** out_value) const {
  if (out_value && is_dict()) {
    *out_value = static_cast<const DictionaryValue*>(this);
    return true;
  }
  return is_dict();
}

std::unique_ptr<Value> Value::CreateDeepCopy() const {
  return std::make_unique<Value>(Clone());
}

bool operator==(const Value& lhs, const Value& rhs) {
  return lhs.data_ == rhs.data_;
}

bool operator!=(const Value& lhs, const Value& rhs) {
  return !(lhs == rhs);
}

bool operator<(const Value& lhs, const Value& rhs) {
  return lhs.data_ < rhs.data_;
}

bool operator>(const Value& lhs, const Value& rhs) {
  return rhs < lhs;
}

bool operator<=(const Value& lhs, const Value& rhs) {
  return !(rhs < lhs);
}

bool operator>=(const Value& lhs, const Value& rhs) {
  return !(lhs < rhs);
}

bool operator==(const Value& lhs, bool rhs) {
  return lhs.is_bool() && lhs.GetBool() == rhs;
}

bool operator==(const Value& lhs, int rhs) {
  return lhs.is_int() && lhs.GetInt() == rhs;
}

bool operator==(const Value& lhs, double rhs) {
  return lhs.is_double() && lhs.GetDouble() == rhs;
}

bool operator==(const Value& lhs, StringPiece rhs) {
  return lhs.is_string() && lhs.GetString() == rhs;
}

bool operator==(const Value& lhs, const Value::Dict& rhs) {
  return lhs.is_dict() && lhs.GetDict() == rhs;
}

bool operator==(const Value& lhs, const Value::List& rhs) {
  return lhs.is_list() && lhs.GetList() == rhs;
}

size_t Value::EstimateMemoryUsage() const {
  switch (type()) {
#if BUILDFLAG(ENABLE_BASE_TRACING)
    case Type::STRING:
      return base::trace_event::EstimateMemoryUsage(GetString());
    case Type::BINARY:
      return base::trace_event::EstimateMemoryUsage(GetBlob());
    case Type::DICTIONARY:
      return base::trace_event::EstimateMemoryUsage(dict());
    case Type::LIST:
      return base::trace_event::EstimateMemoryUsage(list());
#endif  // BUILDFLAG(ENABLE_BASE_TRACING)
    default:
      return 0;
  }
}

std::string Value::DebugString() const {
  return DebugStringImpl(*this);
}

#if BUILDFLAG(ENABLE_BASE_TRACING)
void Value::WriteIntoTrace(perfetto::TracedValue context) const {
  Visit([&](const auto& member) {
    using T = std::decay_t<decltype(member)>;
    if constexpr (std::is_same_v<T, absl::monostate>) {
      std::move(context).WriteString("<none>");
    } else if constexpr (std::is_same_v<T, bool>) {
      std::move(context).WriteBoolean(member);
    } else if constexpr (std::is_same_v<T, int>) {
      std::move(context).WriteInt64(member);
    } else if constexpr (std::is_same_v<T, DoubleStorage>) {
      std::move(context).WriteDouble(member);
    } else if constexpr (std::is_same_v<T, std::string>) {
      std::move(context).WriteString(member);
    } else if constexpr (std::is_same_v<T, BlobStorage>) {
      std::move(context).WriteString("<binary data not supported>");
    } else if constexpr (std::is_same_v<T, Dict>) {
      member.WriteIntoTrace(std::move(context));
    } else if constexpr (std::is_same_v<T, List>) {
      member.WriteIntoTrace(std::move(context));
    }
  });
}
#endif  // BUILDFLAG(ENABLE_BASE_TRACING)

///////////////////// DictionaryValue ////////////////////

// static
std::unique_ptr<DictionaryValue> DictionaryValue::From(
    std::unique_ptr<Value> value) {
  DictionaryValue* out;
  if (value && value->GetAsDictionary(&out)) {
    std::ignore = value.release();
    return WrapUnique(out);
  }
  return nullptr;
}

DictionaryValue::DictionaryValue() : Value(Type::DICTIONARY) {}

DictionaryValue::DictionaryValue(const LegacyDictStorage& storage)
    : Value(storage) {}

DictionaryValue::DictionaryValue(LegacyDictStorage&& storage) noexcept
    : Value(std::move(storage)) {}

Value* DictionaryValue::Set(StringPiece path, std::unique_ptr<Value> in_value) {
  DCHECK(IsStringUTF8AllowingNoncharacters(path));
  DCHECK(in_value);

  // IMPORTANT NOTE: Do not replace with GetDict.SetByDottedPath() yet, because
  // the latter fails when over-writing a non-dict intermediate node, while this
  // method just replaces it with one. This difference makes some tests actually
  // fail (http://crbug.com/949461).
  StringPiece current_path(path);
  Value* current_dictionary = this;
  for (size_t delimiter_position = current_path.find('.');
       delimiter_position != StringPiece::npos;
       delimiter_position = current_path.find('.')) {
    // Assume that we're indexing into a dictionary.
    StringPiece key = current_path.substr(0, delimiter_position);
    Value* child_dictionary =
        current_dictionary->FindKeyOfType(key, Type::DICTIONARY);
    if (!child_dictionary) {
      child_dictionary =
          current_dictionary->SetKey(key, Value(Type::DICTIONARY));
    }

    current_dictionary = child_dictionary;
    current_path = current_path.substr(delimiter_position + 1);
  }

  return static_cast<DictionaryValue*>(current_dictionary)
      ->SetWithoutPathExpansion(current_path, std::move(in_value));
}

Value* DictionaryValue::SetBoolean(StringPiece path, bool in_value) {
  return Set(path, std::make_unique<Value>(in_value));
}

Value* DictionaryValue::SetInteger(StringPiece path, int in_value) {
  return Set(path, std::make_unique<Value>(in_value));
}

Value* DictionaryValue::SetDouble(StringPiece path, double in_value) {
  return Set(path, std::make_unique<Value>(in_value));
}

Value* DictionaryValue::SetString(StringPiece path, StringPiece in_value) {
  return Set(path, std::make_unique<Value>(in_value));
}

Value* DictionaryValue::SetString(StringPiece path,
                                  const std::u16string& in_value) {
  return Set(path, std::make_unique<Value>(in_value));
}

ListValue* DictionaryValue::SetList(StringPiece path,
                                    std::unique_ptr<ListValue> in_value) {
  return static_cast<ListValue*>(Set(path, std::move(in_value)));
}

Value* DictionaryValue::SetWithoutPathExpansion(
    StringPiece key,
    std::unique_ptr<Value> in_value) {
  // NOTE: We can't use |insert_or_assign| here, as only |try_emplace| does
  // an explicit conversion from StringPiece to std::string if necessary.
  auto result = dict().try_emplace(key, std::move(in_value));
  if (!result.second) {
    // in_value is guaranteed to be still intact at this point.
    result.first->second = std::move(in_value);
  }
  return result.first->second.get();
}

bool DictionaryValue::Get(StringPiece path, const Value** out_value) const {
  DCHECK(IsStringUTF8AllowingNoncharacters(path));
  const Value* value = FindPath(path);
  if (!value)
    return false;
  if (out_value)
    *out_value = value;
  return true;
}

bool DictionaryValue::Get(StringPiece path, Value** out_value) {
  return as_const(*this).Get(path, const_cast<const Value**>(out_value));
}

bool DictionaryValue::GetInteger(StringPiece path, int* out_value) const {
  const Value* value;
  if (!Get(path, &value))
    return false;

  bool is_int = value->is_int();
  if (is_int && out_value)
    *out_value = value->GetInt();
  return is_int;
}

bool DictionaryValue::GetString(StringPiece path,
                                std::string* out_value) const {
  const Value* value;
  if (!Get(path, &value))
    return false;

  const bool is_string = value->is_string();
  if (is_string && out_value)
    *out_value = value->GetString();
  return is_string;
}

bool DictionaryValue::GetString(StringPiece path,
                                std::u16string* out_value) const {
  const Value* value;
  if (!Get(path, &value))
    return false;

  const bool is_string = value->is_string();
  if (is_string && out_value)
    *out_value = UTF8ToUTF16(value->GetString());
  return is_string;
}

bool DictionaryValue::GetDictionary(StringPiece path,
                                    const DictionaryValue** out_value) const {
  const Value* value;
  bool result = Get(path, &value);
  if (!result || !value->is_dict())
    return false;

  if (out_value)
    *out_value = static_cast<const DictionaryValue*>(value);

  return true;
}

bool DictionaryValue::GetDictionary(StringPiece path,
                                    DictionaryValue** out_value) {
  return as_const(*this).GetDictionary(
      path, const_cast<const DictionaryValue**>(out_value));
}

bool DictionaryValue::GetList(StringPiece path,
                              const ListValue** out_value) const {
  const Value* value;
  bool result = Get(path, &value);
  if (!result || !value->is_list())
    return false;

  if (out_value)
    *out_value = static_cast<const ListValue*>(value);

  return true;
}

bool DictionaryValue::GetList(StringPiece path, ListValue** out_value) {
  return as_const(*this).GetList(path,
                                 const_cast<const ListValue**>(out_value));
}

bool DictionaryValue::GetDictionaryWithoutPathExpansion(
    StringPiece key,
    const DictionaryValue** out_value) const {
  const Value* value = FindKey(key);
  if (!value || !value->is_dict())
    return false;

  if (out_value)
    *out_value = static_cast<const DictionaryValue*>(value);

  return true;
}

bool DictionaryValue::GetDictionaryWithoutPathExpansion(
    StringPiece key,
    DictionaryValue** out_value) {
  return as_const(*this).GetDictionaryWithoutPathExpansion(
      key, const_cast<const DictionaryValue**>(out_value));
}

bool DictionaryValue::GetListWithoutPathExpansion(
    StringPiece key,
    const ListValue** out_value) const {
  const Value* value = FindKey(key);
  if (!value || !value->is_list())
    return false;

  if (out_value)
    *out_value = static_cast<const ListValue*>(value);

  return true;
}

bool DictionaryValue::GetListWithoutPathExpansion(StringPiece key,
                                                  ListValue** out_value) {
  return as_const(*this).GetListWithoutPathExpansion(
      key, const_cast<const ListValue**>(out_value));
}

std::unique_ptr<DictionaryValue> DictionaryValue::DeepCopyWithoutEmptyChildren()
    const {
  std::unique_ptr<DictionaryValue> copy =
      CopyDictionaryWithoutEmptyChildren(*this);
  if (!copy)
    copy = std::make_unique<DictionaryValue>();
  return copy;
}

void DictionaryValue::Swap(DictionaryValue* other) {
  CHECK(other->is_dict());
  dict().swap(other->dict());
}

DictionaryValue::Iterator::Iterator(const DictionaryValue& target)
    : target_(target), it_(target.DictItems().begin()) {}

DictionaryValue::Iterator::Iterator(const Iterator& other) = default;

DictionaryValue::Iterator::~Iterator() = default;

DictionaryValue* DictionaryValue::DeepCopy() const {
  return new DictionaryValue(dict());
}

std::unique_ptr<DictionaryValue> DictionaryValue::CreateDeepCopy() const {
  return std::make_unique<DictionaryValue>(dict());
}

///////////////////// ListValue ////////////////////

// static
std::unique_ptr<ListValue> ListValue::From(std::unique_ptr<Value> value) {
  if (value && value->is_list())
    return WrapUnique(static_cast<ListValue*>(value.release()));

  return nullptr;
}

ListValue::ListValue() : Value(Type::LIST) {}

void ListValue::Append(base::Value::Dict in_dict) {
  list().emplace_back(std::move(in_dict));
}

void ListValue::Append(base::Value::List in_list) {
  list().emplace_back(std::move(in_list));
}

void ListValue::Swap(ListValue* other) {
  CHECK(other->is_list());
  list().swap(other->list());
}

ValueView::ValueView(const Value& value)
    : data_view_(
          value.Visit([](const auto& member) { return ViewType(member); })) {}

ValueSerializer::~ValueSerializer() = default;

ValueDeserializer::~ValueDeserializer() = default;

std::ostream& operator<<(std::ostream& out, const Value& value) {
  return out << value.DebugString();
}

std::ostream& operator<<(std::ostream& out, const Value::Dict& dict) {
  return out << dict.DebugString();
}

std::ostream& operator<<(std::ostream& out, const Value::List& list) {
  return out << list.DebugString();
}

std::ostream& operator<<(std::ostream& out, const Value::Type& type) {
  if (static_cast<int>(type) < 0 ||
      static_cast<size_t>(type) >= std::size(kTypeNames))
    return out << "Invalid Type (index = " << static_cast<int>(type) << ")";
  return out << Value::GetTypeName(type);
}

}  // namespace base
