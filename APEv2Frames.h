///////////////////////////////////////////////////////////////////////////////
//
//  APEv2Frames.h
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
//  APEv2Header* pApeHdr = reinterpret_cast<APEv2Header*>( pFrame->GetData() );
// 
///////////////////////////////////////////////////////////////////////////////

#pragma once
#include <cassert>
#include <cstdint>
#include <span>
#include <string>

namespace // anonymous
{

static constexpr uint32_t kFlagIsReadOnly = ( 1u << 0 );
static constexpr uint32_t kFlagIsBinary   = ( 1u << 1 );
static constexpr uint32_t kFlagIsHeader   = ( 1u << 29 );
static constexpr uint32_t kFlagHasFooter  = ( 1u << 30 );
static constexpr uint32_t kFlagHasHeader  = ( 1u << 31 );

} // anonymous

namespace PKIsensee
{

///////////////////////////////////////////////////////////////////////////////
//
// APE tag header
//
// See: https://mutagen-specs.readthedocs.io/en/latest/apev2/apev2.html#

class APEv2TagHeader
{
public:

  static constexpr uint32_t kApeIDSize = 8;

private:

#pragma pack(push,1) // Essential for strict binary layout of the APE file format
  char     apeID_[ kApeIDSize ] = {};   // 'APETAGEX'
  uint32_t version_ = 2u;      // e.g. 1 or 2
  uint32_t tagSize_ = 0u;      // in bytes, including footer and all tag items; excluding header
  uint32_t itemCount_ = 0u;    // number of items in the tag
  uint32_t flags_ = 0u;        // see kFlag list above
  uint64_t reserved_ = 0uL;    // must be zero
#pragma pack(pop)

public:

  APEv2TagHeader() = default;
  APEv2TagHeader( const APEv2TagHeader& ) = default;
  APEv2TagHeader& operator=( const APEv2TagHeader& ) = delete;
  APEv2TagHeader( APEv2TagHeader&& ) = delete;
  APEv2TagHeader& operator=( APEv2TagHeader&& ) = delete;

  std::string GetHeaderID() const
  {
    return std::string{ apeID_[ 0 ], apeID_[ 1 ], apeID_[ 2 ], apeID_[ 3 ],
                        apeID_[ 4 ], apeID_[ 5 ], apeID_[ 6 ], apeID_[ 7 ] };
  }
  
  uint32_t GetVersion() const
  {
    return version_;
  }

  uint32_t GetTagSize() const
  {
    return tagSize_;
  }

  uint32_t GetItemCount() const
  {
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

  bool IsReadOnly() const
  {
    return !!( flags_ & kFlagIsReadOnly );
  }

};

///////////////////////////////////////////////////////////////////////////////
//
// APE tag item
//
// See: https://mutagen-specs.readthedocs.io/en/latest/apev2/apev2.html#item

class APEv2TagItem
{
private:

#pragma pack(push,1)   // Essential for strict binary layout of the APE file format
  uint32_t valueSize_; // size of value_ in bytes
  uint32_t flags_;     // see kFlag list above
  char key_[ 1 ];      // ASCII string key; null terminated
  // uint8_t value_[]; // size_ bytes long; may be a UTF8 string or binary blob
#pragma pack(pop)

  static constexpr uint32_t kMaxKeySize = 255;

public:

  APEv2TagItem() = default;
  APEv2TagItem( const APEv2TagItem& ) = default;
  APEv2TagItem& operator=( const APEv2TagItem& ) = delete;
  APEv2TagItem( APEv2TagItem&& ) = delete;
  APEv2TagItem& operator=( APEv2TagItem&& ) = delete;

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

  std::string GetKey() const
  {
    uint32_t charCount = 0u;
    std::string value;
    const char* s = key_;
    for( ; *s != '\0'; ++s, ++charCount )
    {
      // Safety check
      if( charCount == kMaxKeySize )
      {
        // Failure indicates malformed frame
        assert( charCount < kMaxKeySize );
        break;
      }
      value.push_back( *s );
    }
    return value;
  }

  std::span<const uint8_t> GetData() const
  {
    size_t blobBytes = static_cast<size_t>( GetValueSize() );
    std::string value = GetKey();
    size_t valueBytes = value.size() + sizeof( '\0' );
    const uint8_t* blobStart = reinterpret_cast<const uint8_t*>( key_ ) + valueBytes;
    return std::span{ blobStart, blobBytes };
  }

  std::string GetText() const
  {
    auto data = GetData();
    std::string value{ data.begin(), data.end() };
    return value;
  }

};

} // namespace PKIsensee

///////////////////////////////////////////////////////////////////////////////
