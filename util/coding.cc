// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "util/coding.h"

namespace leveldb {

// TED::如果系统本身就是little endian,直接用memcpy.
// TED:: memcpy本身实现依赖于系统字节序？？
// TED::否则手动little endian 赋值
// TED:: why not use loop??
void EncodeFixed32(char* buf, uint32_t value) {
  if (port::kLittleEndian) {
    memcpy(buf, &value, sizeof(value));
  } else {
    buf[0] = value & 0xff;
    buf[1] = (value >> 8) & 0xff;
    buf[2] = (value >> 16) & 0xff;
    buf[3] = (value >> 24) & 0xff;
  }
}

void EncodeFixed64(char* buf, uint64_t value) {
  if (port::kLittleEndian) {
    memcpy(buf, &value, sizeof(value));
  } else {
    buf[0] = value & 0xff;
    buf[1] = (value >> 8) & 0xff;
    buf[2] = (value >> 16) & 0xff;
    buf[3] = (value >> 24) & 0xff;
    buf[4] = (value >> 32) & 0xff;
    buf[5] = (value >> 40) & 0xff;
    buf[6] = (value >> 48) & 0xff;
    buf[7] = (value >> 56) & 0xff;
  }
}

void PutFixed32(std::string* dst, uint32_t value) {
  char buf[sizeof(value)];
  EncodeFixed32(buf, value);
  dst->append(buf, sizeof(buf));
}

void PutFixed64(std::string* dst, uint64_t value) {
  char buf[sizeof(value)];
  EncodeFixed64(buf, value);
  dst->append(buf, sizeof(buf));
}

// TED:: 将输入整形数据，按7位进行分割。7位一组。除了最后一组，前面的组最高位都设置为1.
// TED:: why not use loop to split?? performance??
char* EncodeVarint32(char* dst, uint32_t v) {
  // Operate on characters as unsigneds
  unsigned char* ptr = reinterpret_cast<unsigned char*>(dst);
  static const int B = 128;
  if (v < (1<<7)) {
    *(ptr++) = v;
  } else if (v < (1<<14)) {
    *(ptr++) = v | B;
    *(ptr++) = v>>7;
  } else if (v < (1<<21)) {
    *(ptr++) = v | B;
    *(ptr++) = (v>>7) | B;
    *(ptr++) = v>>14;
  } else if (v < (1<<28)) {
    *(ptr++) = v | B;
    *(ptr++) = (v>>7) | B;
    *(ptr++) = (v>>14) | B;
    *(ptr++) = v>>21;
  } else {
    *(ptr++) = v | B;
    *(ptr++) = (v>>7) | B;
    *(ptr++) = (v>>14) | B;
    *(ptr++) = (v>>21) | B;
    *(ptr++) = v>>28;
  }
  return reinterpret_cast<char*>(ptr);
}

void PutVarint32(std::string* dst, uint32_t v) {
  // TED:: 32bit/7bit ~= 4.57
  char buf[5];
  char* ptr = EncodeVarint32(buf, v);
  dst->append(buf, ptr - buf);
}


// TED:: 同EncodeVarint32，但是使用了循环，简洁。
// TED:: 注意reinterpret_cast和static_cast的使用
char* EncodeVarint64(char* dst, uint64_t v) {
  static const int B = 128;
  unsigned char* ptr = reinterpret_cast<unsigned char*>(dst);

  // TED:: 只要>=1000_0000(2^7=128)说明超过7位，可继续拆分。
  while (v >= B) {
    *(ptr++) = (v & (B-1)) | B;
    v >>= 7;
  }

  // TED:: 不要忘了还剩下一个7位
  *(ptr++) = static_cast<unsigned char>(v);
  return reinterpret_cast<char*>(ptr);
}

void PutVarint64(std::string* dst, uint64_t v) {
  // TED:: 64bit/7bit ~= 9.14
  char buf[10];
  char* ptr = EncodeVarint64(buf, v);
  dst->append(buf, ptr - buf);
}

// TED:: |--length (varint32 type)--|---actual value(string)|
void PutLengthPrefixedSlice(std::string* dst, const Slice& value) {
  PutVarint32(dst, value.size());
  dst->append(value.data(), value.size());
}


// TED:: 查看可以拆分成多少个7bit segment.
// TED:: 参数也可以传入uint32_t
int VarintLength(uint64_t v) {
  int len = 1;
  while (v >= 128) {
    v >>= 7;
    len++;
  }
  return len;
}

const char* GetVarint32PtrFallback(const char* p,
                                   const char* limit,
                                   uint32_t* value) {
  uint32_t result = 0;

  // TED:: 仅需注意是little endian字节序
  // TED:: 32bit/7bit ~= 4.57, 即编码字符串中存在5段分割
  // TED:: 7 * (5 - 1) = 28  因为从0计数，且全闭合。
  for (uint32_t shift = 0; shift <= 28 && p < limit; shift += 7) {
    uint32_t byte = *(reinterpret_cast<const unsigned char*>(p));
    p++;

    // TED:: 看最高位是否为1
    if (byte & 128) {
      // More bytes are present
      result |= ((byte & 127) << shift);
    } else {
      result |= (byte << shift);
      *value = result;
      return reinterpret_cast<const char*>(p);
    }
  }
  return NULL;
}

bool GetVarint32(Slice* input, uint32_t* value) {
  const char* p = input->data();
  const char* limit = p + input->size();
  const char* q = GetVarint32Ptr(p, limit, value);
  if (q == NULL) {
    return false;
  } else {
    // TED:: 此处即为coding.h中原注释写的advance the slice past the parsed value
    *input = Slice(q, limit - q);
    return true;
  }
}


// TED:: 为何GetVarint64Ptr不能像GetVarint32Ptr一样设为inline
const char* GetVarint64Ptr(const char* p, const char* limit, uint64_t* value) {
  uint64_t result = 0;

  // TED:: 64bit/7bit ~= 9.14, 即编码字符串中存在10段分割
  // TED:: 7 * (10 - 1) = 63  因为从0计数，且全闭合。
  for (uint32_t shift = 0; shift <= 63 && p < limit; shift += 7) {
    uint64_t byte = *(reinterpret_cast<const unsigned char*>(p));
    p++;
    if (byte & 128) {
      // More bytes are present
      result |= ((byte & 127) << shift);
    } else {
      result |= (byte << shift);
      *value = result;
      return reinterpret_cast<const char*>(p);
    }
  }
  return NULL;
}

bool GetVarint64(Slice* input, uint64_t* value) {
  const char* p = input->data();
  const char* limit = p + input->size();
  const char* q = GetVarint64Ptr(p, limit, value);
  if (q == NULL) {
    return false;
  } else {
    *input = Slice(q, limit - q);
    return true;
  }
}

// TED:: Get_LengthPrefixed_Slice
// TED:: 此方法未在头文件中暴露，在coding.cc中也未被引用。????
const char* GetLengthPrefixedSlice(const char* p, const char* limit,
                                   Slice* result) {
  uint32_t len;
  p = GetVarint32Ptr(p, limit, &len);
  if (p == NULL) return NULL;
  if (p + len > limit) return NULL;
  *result = Slice(p, len);
  return p + len;
}

bool GetLengthPrefixedSlice(Slice* input, Slice* result) {
  uint32_t len;
  if (GetVarint32(input, &len) &&
      input->size() >= len) {
    *result = Slice(input->data(), len);

    // TED:: advance the slice past the parsed value.
    input->remove_prefix(len);
    return true;
  } else {
    return false;
  }
}

}  // namespace leveldb
