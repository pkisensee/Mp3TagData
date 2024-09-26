///////////////////////////////////////////////////////////////////////////////
//
//  APEv2Tags.cpp
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
///////////////////////////////////////////////////////////////////////////////

#include <cassert>

#include "APEv2Tags.h"
#include "Util.h"

using namespace PKIsensee;

static constexpr uint32_t kMaxApeAllTagsSize = 1024 * 1024;
static constexpr uint32_t kMinTagItemSize = ( sizeof( uint32_t ) * 2 ) + 
                                              sizeof( char ) + 
                                              sizeof( '\n' );;

// See https://mutagen-specs.readthedocs.io/en/latest/apev2/apev2.html#
static constexpr const char* kApeTag = "APETAGEX";
static constexpr uint32_t kMinApeVersion = 1 * 1000; // 1000 = v1, 2000 = v2
static constexpr uint32_t kMaxApeVersion = 10 * kMinApeVersion;
static constexpr uint32_t kMinKeySize = 2;
static constexpr uint32_t kMaxKeySize = 255;
static constexpr char kMinKeyCharVal = ' ';
static constexpr char kMaxKeyCharVal = '~';

///////////////////////////////////////////////////////////////////////////////
//
// APEv2TagHeaders and APEv2TagItems are typically read from an untrusted file, 
// so verification is critical

bool APEv2TagHeader::IsValid() const
{
  static_assert( std::is_standard_layout_v<APEv2TagHeader> );

  if( version_ == kMinApeVersion ) // v1 had no flags defined
  {
    if( !PK_VALID( flags_ == 0u ) )
      return false;
  }

  return PK_VALID( GetHeaderID() == kApeTag ) &&
         PK_VALID( version_ >= kMinApeVersion ) &&
         PK_VALID( version_ <= kMaxApeVersion ) &&
         PK_VALID( tagBlockSize_ >= sizeof( *this ) ) && // always includes footer
         PK_VALID( tagBlockSize_ < kMaxApeAllTagsSize ) &&
         PK_VALID( itemCount_ > 0 ) &&
         PK_VALID( itemCount_ < ( kMaxApeAllTagsSize / kMinTagItemSize ) ) &&
         PK_VALID( ( flags_ & ~kFlagsUsed ) == 0 ) &&
         PK_VALID( reserved_ == 0u );
}

///////////////////////////////////////////////////////////////////////////////
//
// Standard APE tag

std::string_view APEv2TagHeader::GetStdApeTag() // static
{
  return std::string_view( kApeTag, kApeIDSize );
}

///////////////////////////////////////////////////////////////////////////////
//
// Extract tag key

std::string_view APEv2TagItem::GetKey() const
{
  if( !IsValid() ) // Validation check ensures key size <= 255
    return {};

  uint32_t charCount = 0u;
  for( const auto* s = key_; *s != '\0'; ++s )
    ++charCount;
  return std::string_view{ key_, charCount };
}

///////////////////////////////////////////////////////////////////////////////
//
// Extract tag data as blob; data block begins after key trailing null char

std::span<const uint8_t> APEv2TagItem::GetData() const
{
  std::string_view key = GetKey(); // Validation check in GetKey()
  if( key.empty() )
    return {};
  auto valueBytes = GetValueSize();
  auto keyBytes = key.size() + sizeof( '\0' );
  const uint8_t* valueStart = reinterpret_cast<const uint8_t*>( key_ ) + keyBytes;
  return std::span{ valueStart, valueBytes };
}

///////////////////////////////////////////////////////////////////////////////
//
// Extract data as text

std::string_view APEv2TagItem::GetText() const
{
  assert( IsText() );
  auto blob = GetData(); // Validation check here
  auto textStart = reinterpret_cast<const char*>( blob.data() );
  return std::string_view{ textStart, blob.size() };
}

bool APEv2TagItem::IsValid() const
{
  static_assert( std::is_standard_layout_v<APEv2TagItem> );

  if( !PK_VALID( (flags_ & ~kFlagsUsed) == 0 ) )
    return false;

  // key must be len 2-255, chars: ' ' through '~'
  uint32_t charCount = 0u;
  for( const auto* s = key_; *s != '\0'; ++s, ++charCount )
  {
    if( !PK_VALID( charCount < kMaxKeySize ) )
      return false;
    if( !PK_VALID( *s >= kMinKeyCharVal ) )
      return false;
    if( !PK_VALID( *s <= kMaxKeyCharVal ) )
      return false;
  }
  return PK_VALID( charCount > 1 );
}

///////////////////////////////////////////////////////////////////////////////
