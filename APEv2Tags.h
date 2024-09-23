///////////////////////////////////////////////////////////////////////////////
//
//  APEv2Tags.h
//
//  Copyright © Pete Isensee (PKIsensee@msn.com).
//  All rights reserved worldwide.
//
//  Permission to copy, modify, reproduce or redistribute this source code is
//  granted provided the above copyright notice is retained in the resulting 
//  source code.
// 
//  This software is provided "as is" and without any express or implied
//  warranties.
//
//  APE v2 frame structures. Intended to be used as "casted-to" objects, e.g.
//  APEv2Header* pApeHdr = reinterpret_cast<APEv2Header*>( pTag->GetData() );
// 
///////////////////////////////////////////////////////////////////////////////

#pragma once
#include <span>
#include <string>
#include <string_view>

#include "Util.h"

namespace // anonymous
{
static constexpr uint32_t kFlagIsReadOnly = ( 1u << 0 );
static constexpr uint32_t kFlagIsBinary   = ( 1u << 1 );
static constexpr uint32_t kFlagIsHeader   = ( 1u << 29 );
static constexpr uint32_t kFlagHasFooter  = ( 1u << 30 );
static constexpr uint32_t kFlagHasHeader  = ( 1u << 31 );
static constexpr uint32_t kFlagsUsed = kFlagIsReadOnly |
                                       kFlagIsBinary   |
                                       kFlagIsHeader   |
                                       kFlagHasFooter  |
                                       kFlagHasHeader;
} // anonymous

namespace PKIsensee
{

///////////////////////////////////////////////////////////////////////////////
//
// APE tag header
//
// See: https://mutagen-specs.readthedocs.io/en/latest/apev2/apev2.html#

class PK_PACKED_STRUCT APEv2TagHeader // packing essential to match APE file format
{
private:

  static constexpr uint32_t kApeIDSize = 8;

private:

PK_START_PACK
  char     apeID_[ kApeIDSize ] = {};   // 'APETAGEX'
  uint32_t version_ = 2u;      // e.g. 1 or 2
  uint32_t tagBlockSize_ = 0u; // in bytes, including footer and all tag items; excluding header
  uint32_t itemCount_ = 0u;    // number of items in the tag
  uint32_t flags_ = 0u;        // see kFlag list in implementation file
  uint64_t reserved_ = 0uL;    // must be zero
PK_END_PACK

public:

  // Intended to be used as casted-to object
  APEv2TagHeader() = delete;

  std::string_view GetHeaderID() const
  {
    return std::string_view{ apeID_, kApeIDSize };
  }
  
  uint32_t GetVersion() const
  {
    return version_;
  }

  uint32_t GetTagBlockSize() const
  {
    if( !IsValid() )
      return 0u;
    return tagBlockSize_;
  }

  uint32_t GetItemCount() const
  {
    if( !IsValid() )
      return 0u;
    return itemCount_;
  }

  bool ContainsHeader() const
  {
    return !!( flags_ & kFlagHasHeader );
  }

  bool ContainsFooter() const
  {
    return !!( flags_ & kFlagHasFooter );
  }

  bool IsHeader() const
  {
    return !!( flags_ & kFlagIsHeader );
  }

  bool IsValid() const;
  static std::string_view GetStdApeTag();

}; // class APEv2TagHeader

///////////////////////////////////////////////////////////////////////////////
//
// APE tag item
// 
// Consists of a key/value pair, where the key is a string from length 2-255
// and the value is either a string or binary blob of length GetValueSize().
//
// See: https://mutagen-specs.readthedocs.io/en/latest/apev2/apev2.html#item

class PK_PACKED_STRUCT APEv2TagItem // packing essential to match APE file format
{
private:

PK_START_PACK
  uint32_t valueSize_; // size of value_ in bytes
  uint32_t flags_;     // see kFlag list above
  char key_[ 1 ];      // ASCII string key; null terminated
  // uint8_t value_[]; // valueSize_ bytes long; may be a UTF8 string or binary blob
PK_END_PACK

public:

  // Intended to be used as casted-to object
  APEv2TagItem() = delete;

  uint32_t GetValueSize() const // bytes
  {
    return valueSize_;
  }

  uint32_t GetTagSize() const // bytes
  {
    return uint32_t( sizeof(*this) + GetKey().size() + valueSize_ );
  }

  bool IsText() const
  {
    return !IsBinary();
  }

  bool IsBinary() const
  {
    return !!( flags_ & kFlagIsBinary );
  }

  bool IsReadOnly() const
  {
    return !!( flags_ & kFlagIsReadOnly );
  }

  std::string_view GetKey() const;
  std::span<const uint8_t> GetData() const;
  std::string_view GetText() const;
  bool IsValid() const;

}; // class APEv2TagItem

} // namespace PKIsensee

///////////////////////////////////////////////////////////////////////////////
