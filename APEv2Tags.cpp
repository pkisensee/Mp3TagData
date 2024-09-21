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

namespace // anonymous
{

static constexpr const char* kApeTag = "APETAGEX";

static constexpr uint32_t kMinApeVersion = 1 * 1000;
static constexpr uint32_t kMaxApeVersion = 10 * 1000;
static constexpr uint32_t kMaxApeAllTagsSize = 1024 * 1024;
static constexpr uint32_t kMinTagItemSize = ( sizeof( uint32_t ) * 2 ) + sizeof( char ) + sizeof( '\n' );;
static constexpr uint32_t kMinKeySize = 2;
static constexpr uint32_t kMaxKeySize = 255;
static constexpr char kMinKeyCharVal = ' ';
static constexpr char kMaxKeyCharVal = '~';

} // anonymous

///////////////////////////////////////////////////////////////////////////////
//
// APEv2TagHeaders and Items are typically read from an untrusted file, so 
// verification is critical

bool APEv2TagHeader::IsValid() const
{
  if( !PK_VALID( GetHeaderID() == kApeTag ) )
    return false;

  if( !PK_VALID( version_ >= kMinApeVersion ) )
    return false;
  if( !PK_VALID( version_ <= kMaxApeVersion ) )
    return false;

  if( !PK_VALID( tagBlockSize_ >= sizeof( *this ) ) ) // always includes footer
    return false;
  if( !PK_VALID( tagBlockSize_ < kMaxApeAllTagsSize ) )
    return false;

  if( !PK_VALID( itemCount_ > 0 ) )
    return false;
  if( !PK_VALID( itemCount_ < ( kMaxApeAllTagsSize / kMinTagItemSize ) ) )
    return false;

  if( version_ == kMinApeVersion )
  {
    if( !PK_VALID( flags_ == 0u ) )
      return false;
  }
  else if( !PK_VALID( ( flags_ & ~kFlagsUsed ) == 0 ) )
    return false;

  if( !PK_VALID( reserved_ == 0u ) )
    return false;
  return true;
}

///////////////////////////////////////////////////////////////////////////////
//
// Extract key as string

std::string_view APEv2TagHeader::GetStdApeTag() // static
{
  return std::string_view( kApeTag );
}

///////////////////////////////////////////////////////////////////////////////
//
// Extract key as string

std::string_view APEv2TagItem::GetKey() const
{
  if( !IsValid() )
    return {};

  uint32_t charCount = 0u;
  for( const auto* s = key_; *s != '\0'; ++s, ++charCount )
    ;
  return std::string_view{ key_, charCount };
}

///////////////////////////////////////////////////////////////////////////////
//
// Extract data as blob; data block starts after key trailing null char

std::span<const uint8_t> APEv2TagItem::GetData() const
{
  std::string_view key = GetKey();
  if( key.empty() )
    return {};
  size_t valueBytes = static_cast<size_t>( GetValueSize() );
  size_t keyBytes = key.size() + sizeof( '\0' );
  const uint8_t* valueStart = reinterpret_cast<const uint8_t*>( key_ ) + keyBytes;
  return std::span{ valueStart, valueBytes };
}

///////////////////////////////////////////////////////////////////////////////
//
// Extract data as text

std::string_view APEv2TagItem::GetText() const
{
  if( !IsValid() )
    return {};
  assert( IsText() );
  auto blob = GetData();
  auto textStart = reinterpret_cast<const char*>( blob.data() );
  return std::string_view{ textStart, blob.size() };
}

bool APEv2TagItem::IsValid() const
{
  if( !PK_VALID( ( flags_ & ~kFlagsUsed ) == 0 ) )
    return false;

  // key must be len 2-255, space through ~
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
  if( !PK_VALID( charCount > 1 ) )
    return false;
  return true;
}





