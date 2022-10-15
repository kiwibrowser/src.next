// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_PICKLE_H_
#define BASE_PICKLE_H_

#include <stddef.h>
#include <stdint.h>

#include <string>

#include "base/base_export.h"
#include "base/check_op.h"
#include "base/containers/span.h"
#include "base/gtest_prod_util.h"
#include "base/memory/raw_ptr_exclusion.h"
#include "base/memory/ref_counted.h"
#include "base/strings/string_piece.h"

namespace base {

class Pickle;

// PickleIterator reads data from a Pickle. The Pickle object must remain valid
// while the PickleIterator object is in use.
class BASE_EXPORT PickleIterator {
 public:
  PickleIterator() : payload_(nullptr), read_index_(0), end_index_(0) {}
  explicit PickleIterator(const Pickle& pickle);

  // Methods for reading the payload of the Pickle. To read from the start of
  // the Pickle, create a PickleIterator from a Pickle. If successful, these
  // methods return true. Otherwise, false is returned to indicate that the
  // result could not be extracted. It is not possible to read from the iterator
  // after that.
  [[nodiscard]] bool ReadBool(bool* result);
  [[nodiscard]] bool ReadInt(int* result);
  [[nodiscard]] bool ReadLong(long* result);
  [[nodiscard]] bool ReadUInt16(uint16_t* result);
  [[nodiscard]] bool ReadUInt32(uint32_t* result);
  [[nodiscard]] bool ReadInt64(int64_t* result);
  [[nodiscard]] bool ReadUInt64(uint64_t* result);
  [[nodiscard]] bool ReadFloat(float* result);
  [[nodiscard]] bool ReadDouble(double* result);
  [[nodiscard]] bool ReadString(std::string* result);
  // The StringPiece data will only be valid for the lifetime of the message.
  [[nodiscard]] bool ReadStringPiece(StringPiece* result);
  [[nodiscard]] bool ReadString16(std::u16string* result);
  // The StringPiece16 data will only be valid for the lifetime of the message.
  [[nodiscard]] bool ReadStringPiece16(StringPiece16* result);

  // A pointer to the data will be placed in |*data|, and the length will be
  // placed in |*length|. The pointer placed into |*data| points into the
  // message's buffer so it will be scoped to the lifetime of the message (or
  // until the message data is mutated). Do not keep the pointer around!
  [[nodiscard]] bool ReadData(const char** data, size_t* length);

  // Similar, but using base::span for convenience.
  [[nodiscard]] bool ReadData(base::span<const uint8_t>* data);

  // A pointer to the data will be placed in |*data|. The caller specifies the
  // number of bytes to read, and ReadBytes will validate this length. The
  // pointer placed into |*data| points into the message's buffer so it will be
  // scoped to the lifetime of the message (or until the message data is
  // mutated). Do not keep the pointer around!
  [[nodiscard]] bool ReadBytes(const char** data, size_t length);

  // A version of ReadInt() that checks for the result not being negative. Use
  // it for reading the object sizes.
  [[nodiscard]] bool ReadLength(size_t* result) {
    int result_int;
    if (!ReadInt(&result_int) || result_int < 0)
      return false;
    *result = static_cast<size_t>(result_int);
    return true;
  }

  // Skips bytes in the read buffer and returns true if there are at least
  // num_bytes available. Otherwise, does nothing and returns false.
  [[nodiscard]] bool SkipBytes(size_t num_bytes) {
    return !!GetReadPointerAndAdvance(num_bytes);
  }

  bool ReachedEnd() const { return read_index_ == end_index_; }

 private:
  // Read Type from Pickle.
  template <typename Type>
  bool ReadBuiltinType(Type* result);

  // Advance read_index_ but do not allow it to exceed end_index_.
  // Keeps read_index_ aligned.
  void Advance(size_t size);

  // Get read pointer for Type and advance read pointer.
  template<typename Type>
  const char* GetReadPointerAndAdvance();

  // Get read pointer for |num_bytes| and advance read pointer. This method
  // checks num_bytes for wrapping.
  const char* GetReadPointerAndAdvance(size_t num_bytes);

  // Get read pointer for (num_elements * size_element) bytes and advance read
  // pointer. This method checks for overflow and wrapping.
  const char* GetReadPointerAndAdvance(size_t num_elements,
                                       size_t size_element);

  const char* payload_;  // Start of our pickle's payload.
  size_t read_index_;  // Offset of the next readable byte in payload.
  size_t end_index_;  // Payload size.

  FRIEND_TEST_ALL_PREFIXES(PickleTest, GetReadPointerAndAdvance);
};

// This class provides facilities for basic binary value packing and unpacking.
//
// The Pickle class supports appending primitive values (ints, strings, etc.)
// to a pickle instance.  The Pickle instance grows its internal memory buffer
// dynamically to hold the sequence of primitive values.   The internal memory
// buffer is exposed as the "data" of the Pickle.  This "data" can be passed
// to a Pickle object to initialize it for reading.
//
// When reading from a Pickle object, it is important for the consumer to know
// what value types to read and in what order to read them as the Pickle does
// not keep track of the type of data written to it.
//
// The Pickle's data has a header which contains the size of the Pickle's
// payload.  It can optionally support additional space in the header.  That
// space is controlled by the header_size parameter passed to the Pickle
// constructor.
//
class BASE_EXPORT Pickle {
 public:
  // Auxiliary data attached to a Pickle. Pickle must be subclassed along with
  // this interface in order to provide a concrete implementation of support
  // for attachments. The base Pickle implementation does not accept
  // attachments.
  class BASE_EXPORT Attachment : public RefCountedThreadSafe<Attachment> {
   public:
    Attachment();
    Attachment(const Attachment&) = delete;
    Attachment& operator=(const Attachment&) = delete;

   protected:
    friend class RefCountedThreadSafe<Attachment>;
    virtual ~Attachment();
  };

  // Initialize a Pickle object using the default header size.
  Pickle();

  // Initialize a Pickle object with the specified header size in bytes, which
  // must be greater-than-or-equal-to sizeof(Pickle::Header).  The header size
  // will be rounded up to ensure that the header size is 32bit-aligned.
  explicit Pickle(size_t header_size);

  // Initializes a Pickle from a const block of data.  The data is not copied;
  // instead the data is merely referenced by this Pickle.  Only const methods
  // should be used on the Pickle when initialized this way.  The header
  // padding size is deduced from the data length.
  Pickle(const char* data, size_t data_len);

  // Initializes a Pickle as a deep copy of another Pickle.
  Pickle(const Pickle& other);

  // Note: There are no virtual methods in this class.  This destructor is
  // virtual as an element of defensive coding.  Other classes have derived from
  // this class, and there is a *chance* that they will cast into this base
  // class before destruction.  At least one such class does have a virtual
  // destructor, suggesting at least some need to call more derived destructors.
  virtual ~Pickle();

  // Performs a deep copy.
  Pickle& operator=(const Pickle& other);

  // Returns the number of bytes written in the Pickle, including the header.
  size_t size() const {
    return header_ ? header_size_ + header_->payload_size : 0;
  }

  // Returns the data for this Pickle.
  const void* data() const { return header_; }

  // Returns the effective memory capacity of this Pickle, that is, the total
  // number of bytes currently dynamically allocated or 0 in the case of a
  // read-only Pickle. This should be used only for diagnostic / profiling
  // purposes.
  size_t GetTotalAllocatedSize() const;

  // Methods for adding to the payload of the Pickle.  These values are
  // appended to the end of the Pickle's payload.  When reading values from a
  // Pickle, it is important to read them in the order in which they were added
  // to the Pickle.

  void WriteBool(bool value) { WriteInt(value ? 1 : 0); }
  void WriteInt(int value) { WritePOD(value); }
  void WriteLong(long value) {
    // Always write long as a 64-bit value to ensure compatibility between
    // 32-bit and 64-bit processes.
    WritePOD(static_cast<int64_t>(value));
  }
  void WriteUInt16(uint16_t value) { WritePOD(value); }
  void WriteUInt32(uint32_t value) { WritePOD(value); }
  void WriteInt64(int64_t value) { WritePOD(value); }
  void WriteUInt64(uint64_t value) { WritePOD(value); }
  void WriteFloat(float value) { WritePOD(value); }
  void WriteDouble(double value) { WritePOD(value); }
  void WriteString(const StringPiece& value);
  void WriteString16(const StringPiece16& value);
  // "Data" is a blob with a length. When you read it out you will be given the
  // length. See also WriteBytes.
  void WriteData(const char* data, size_t length);
  // "Bytes" is a blob with no length. The caller must specify the length both
  // when reading and writing. It is normally used to serialize PoD types of a
  // known size. See also WriteData.
  void WriteBytes(const void* data, size_t length);

  // WriteAttachment appends |attachment| to the pickle. It returns
  // false iff the set is full or if the Pickle implementation does not support
  // attachments.
  virtual bool WriteAttachment(scoped_refptr<Attachment> attachment);

  // ReadAttachment parses an attachment given the parsing state |iter| and
  // writes it to |*attachment|. It returns true on success.
  virtual bool ReadAttachment(base::PickleIterator* iter,
                              scoped_refptr<Attachment>* attachment) const;

  // Indicates whether the pickle has any attachments.
  virtual bool HasAttachments() const;

  // Reserves space for upcoming writes when multiple writes will be made and
  // their sizes are computed in advance. It can be significantly faster to call
  // Reserve() before calling WriteFoo() multiple times.
  void Reserve(size_t additional_capacity);

  // Payload follows after allocation of Header (header size is customizable).
  struct Header {
    uint32_t payload_size;  // Specifies the size of the payload.
  };

  // Returns the header, cast to a user-specified type T.  The type T must be a
  // subclass of Header and its size must correspond to the header_size passed
  // to the Pickle constructor.
  template <class T>
  T* headerT() {
    DCHECK_EQ(header_size_, sizeof(T));
    return static_cast<T*>(header_);
  }
  template <class T>
  const T* headerT() const {
    DCHECK_EQ(header_size_, sizeof(T));
    return static_cast<const T*>(header_);
  }

  // The payload is the pickle data immediately following the header.
  size_t payload_size() const {
    return header_ ? header_->payload_size : 0;
  }

  const char* payload() const {
    return reinterpret_cast<const char*>(header_) + header_size_;
  }

  // Returns the address of the byte immediately following the currently valid
  // header + payload.
  const char* end_of_payload() const {
    // This object may be invalid.
    return header_ ? payload() + payload_size() : NULL;
  }

 protected:
  // Returns size of the header, which can have default value, set by user or
  // calculated by passed raw data.
  size_t header_size() const { return header_size_; }

  char* mutable_payload() {
    return reinterpret_cast<char*>(header_) + header_size_;
  }

  size_t capacity_after_header() const {
    return capacity_after_header_;
  }

  // Resize the capacity, note that the input value should not include the size
  // of the header.
  void Resize(size_t new_capacity);

  // Claims |num_bytes| bytes of payload. This is similar to Reserve() in that
  // it may grow the capacity, but it also advances the write offset of the
  // pickle by |num_bytes|. Claimed memory, including padding, is zeroed.
  //
  // Returns the address of the first byte claimed.
  void* ClaimBytes(size_t num_bytes);

  // Find the end of the pickled data that starts at range_start.  Returns NULL
  // if the entire Pickle is not found in the given data range.
  static const char* FindNext(size_t header_size,
                              const char* range_start,
                              const char* range_end);

  // Parse pickle header and return total size of the pickle. Data range
  // doesn't need to contain entire pickle.
  // Returns true if pickle header was found and parsed. Callers must check
  // returned |pickle_size| for sanity (against maximum message size, etc).
  // NOTE: when function successfully parses a header, but encounters an
  // overflow during pickle size calculation, it sets |pickle_size| to the
  // maximum size_t value and returns true.
  static bool PeekNext(size_t header_size,
                       const char* range_start,
                       const char* range_end,
                       size_t* pickle_size);

  // The allocation granularity of the payload.
  static const size_t kPayloadUnit;

 private:
  friend class PickleIterator;

  // `header_` is not a raw_ptr<...> for performance reasons (based on analysis
  // of sampling profiler data).
  RAW_PTR_EXCLUSION Header* header_;
  size_t header_size_;  // Supports extra data between header and payload.
  // Allocation size of payload (or -1 if allocation is const). Note: this
  // doesn't count the header.
  size_t capacity_after_header_;
  // The offset at which we will write the next field. Note: this doesn't count
  // the header.
  size_t write_offset_;

  // Just like WriteBytes, but with a compile-time size, for performance.
  template<size_t length> void BASE_EXPORT WriteBytesStatic(const void* data);

  // Writes a POD by copying its bytes.
  template <typename T> bool WritePOD(const T& data) {
    WriteBytesStatic<sizeof(data)>(&data);
    return true;
  }

  inline void* ClaimUninitializedBytesInternal(size_t num_bytes);
  inline void WriteBytesCommon(const void* data, size_t length);

  FRIEND_TEST_ALL_PREFIXES(PickleTest, DeepCopyResize);
  FRIEND_TEST_ALL_PREFIXES(PickleTest, Resize);
  FRIEND_TEST_ALL_PREFIXES(PickleTest, PeekNext);
  FRIEND_TEST_ALL_PREFIXES(PickleTest, PeekNextOverflow);
  FRIEND_TEST_ALL_PREFIXES(PickleTest, FindNext);
  FRIEND_TEST_ALL_PREFIXES(PickleTest, FindNextWithIncompleteHeader);
  FRIEND_TEST_ALL_PREFIXES(PickleTest, FindNextOverflow);
};

}  // namespace base

#endif  // BASE_PICKLE_H_
