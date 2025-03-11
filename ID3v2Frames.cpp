///////////////////////////////////////////////////////////////////////////////
//
//  ID3v2Frames.cpp
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

#include "ID3v2Frames.h"

using namespace PKIsensee;

static constexpr char kTextFrameIDStart = 'T';
static constexpr const char* kCommentFrameID = "COMM";

///////////////////////////////////////////////////////////////////////////////
//
// Validates that ID3 header is reasonable

bool ID3v2FileHeader::IsValid() const
{
  static_assert( std::is_standard_layout_v<ID3v2FileHeader> );

  return PK_VALID( GetHeaderID() == kID3String ) &&
         PK_VALID( majorVersion_ >= kMajorVersionMin ) &&
         PK_VALID( majorVersion_ <= kMajorVersionMax ) &&
         PK_VALID( minorVersion_ != 0xFF ) &&
         PK_VALID( ( flags_ & kFlagsRemaining ) == 0x0 );
}

///////////////////////////////////////////////////////////////////////////////
//
// Set ID3 string to contain wide text. Only use when underlying memory is
// sufficiently sized to hold new text.

void ID3v2String::SetText( std::wstring_view newText, ID3TextEncoding textEncoding )
{
  switch( textEncoding )
  {
  case ID3TextEncoding::UTF16:
    unicode_.bom_[0] = kByteOrderMark0;
    unicode_.bom_[1] = kByteOrderMark1;
    break;
  case ID3TextEncoding::UTF16BE:
    unicode_.bom_[0] = kByteOrderMark1;
    unicode_.bom_[1] = kByteOrderMark0;
    break;
  default:
    break;
  }

  // ID3 strings are not null terminated, hence memcpy
  auto strBytes = newText.size() * sizeof( wchar_t );
  memcpy( unicode_.utf16_, newText.data(), strBytes );
}

///////////////////////////////////////////////////////////////////////////////
//
// Validate fields of ID3 string

bool ID3v2String::IsValid( ID3TextEncoding textEncoding ) const
{
  static_assert( std::is_standard_layout_v<ID3v2String> );

  if( !IsValidTextEncoding( static_cast<uint8_t>( textEncoding ) ) )
    return false;

  switch( textEncoding )
  {
  case ID3TextEncoding::ANSI:
  case ID3TextEncoding::UTF8:
    return true;
  case ID3TextEncoding::UTF16:
    return PK_VALID( unicode_.bom_[0] == kByteOrderMark0 ) &&
           PK_VALID( unicode_.bom_[1] == kByteOrderMark1 );
  case ID3TextEncoding::UTF16BE:
    return PK_VALID( unicode_.bom_[0] == kByteOrderMark1 ) &&
           PK_VALID( unicode_.bom_[1] == kByteOrderMark0 );
  default:
    return false;
  }
}

///////////////////////////////////////////////////////////////////////////////
//
// Validates that a text encoding value is reasonable

bool ID3v2String::IsValidTextEncoding( uint8_t textEncoding ) // static
{
  return PK_VALID( textEncoding >= 0 ) &&
         PK_VALID( textEncoding <= uint8_t( ID3TextEncoding::Max ) );
}

///////////////////////////////////////////////////////////////////////////////
//
// Establish valid frame header values

void ID3v2FrameHdr::SetHeader( std::string_view frameID, uint32_t newFrameSize, uint8_t majorVersion )
{
  assert( majorVersion >= kMajorVersionMin && majorVersion <= kMajorVersionMax );
  assert( frameID.size() == kFrameIDCharCount );
  memcpy( frameID_, frameID.data(), kFrameIDCharCount );

  // Version 3: big endian value. Other versions are syncSafe values.
  syncSafeSize_ = UseSyncSafeSize( majorVersion ) ? WriteID3Int<7>( newFrameSize ) :
                                                    WriteID3Int<8>( newFrameSize );

  // Unused in current implementation
  statusMessages_ = 0;
  formatDescription_ = 0;
}

///////////////////////////////////////////////////////////////////////////////
//
// Determine text size when "this" is ID3v2TextFrame with embedded ID3v2String

uint32_t ID3v2FrameHdr::GetTextBytes( const ID3v2String& str, uint8_t majorVersion, 
                                      bool isWideString ) const
{
  //  rawFrame                   textStart
  //  |                          |
  //  v                          v
  // |<------------------------>|<-------------->|
  // |                                           |
  // |<--ID3v2FrameHdr-->|<-----frameSize------->|
  // |                                           |
  // |<----------offset-------->|<--textBytes--->|

  assert( majorVersion >= kMajorVersionMin && majorVersion <= kMajorVersionMax );
  auto rawFrame = reinterpret_cast<const uint8_t*>( this );
  auto textStart = str.GetTextStart( isWideString );
  assert( rawFrame < textStart );
  ptrdiff_t offset = textStart - rawFrame;
  uint32_t offsetU32 = static_cast<uint32_t>( offset );

  uint32_t frameSize = GetSize( majorVersion );
  uint32_t textBytes = sizeof( ID3v2FrameHdr ) + frameSize;
  if( offsetU32 > textBytes )
    return 0u; // malformed frame; no text possible
  textBytes -= offsetU32;
  return textBytes;
}

///////////////////////////////////////////////////////////////////////////////
//
// Extract text from the text frame. Returns std::string for simplicity, even
// if underlying data is stored as Unicode.

std::string ID3v2TextFrame::GetText( uint8_t majorVersion ) const
{
  assert( majorVersion >= kMajorVersionMin && majorVersion <= kMajorVersionMax );
  if( !IsValid() )
    return {};

  // Determine size of string
  bool isWideString = IsWideString();
  auto byteCount = GetTextBytes( str_, majorVersion, isWideString );

  std::string value;
  if( isWideString )
  {
    assert( byteCount % 2 == 0 );
    auto charCount = byteCount / sizeof( wchar_t );
    std::wstring_view unicode = str_.GetTextWide( charCount );
    value = StringUtil::GetUtf8( unicode );
  }
  else
  {
    auto charCount = byteCount / sizeof( char );
    value = str_.GetText( charCount );
  }

  // In some buggy frames, trailing null bytes may be included, so strip them out
  StrUtil::ToTrimmedTrailing( value, std::string( { '\0' } ) );
  return value;
}

///////////////////////////////////////////////////////////////////////////////
//
// Validate ID3 text frame fields

bool ID3v2TextFrame::IsValid() const
{
  // static_assert( std::is_standard_layout_v<ID3v2TextFrame> );
  // Derived classes with data don't have standard layouts due to potential padding,
  // but as long as the sizes are correct, we can use casting properly
  static_assert( sizeof( *this ) == sizeof( ID3v2FrameHdr ) +
                 sizeof( textEncoding_ ) +
                 sizeof( str_ ) );

  // If header indicates text but size is zero, don't even access ID3v2TextFrame fields
  if( GetSize( kMajorVersionWith8BitEncoding ) == 0 )
    return false;

  return PK_VALID( GetFrameID()[0] == kTextFrameIDStart ) &&
         PK_VALID( str_.IsValid( ID3TextEncoding( textEncoding_ ) ) );
}

///////////////////////////////////////////////////////////////////////////////
//
// Extract comment from the comment frame. Returns std::string for simplicity,
// even if stored in Unicode. Skips and does not return the comment description.

std::string ID3v2CommentFrame::GetText( uint8_t majorVersion ) const
{
  assert( majorVersion >= kMajorVersionMin && majorVersion <= kMajorVersionMax );
  if( !IsValid() )
    return {};
  bool isWideString = IsWideString();

  // Determine size of string
  auto byteCount = GetTextBytes( str_, majorVersion, isWideString );

  // Comment is description text, then comment text, separated by a null char
  std::string value;
  if( isWideString )
  {
    assert( byteCount % 2 == 0 );
    auto charCount = byteCount / sizeof( wchar_t );
    std::wstring_view descriptionAndComment = str_.GetTextWide( charCount );

    // Skip comment description
    auto start = std::begin( descriptionAndComment );
    auto end = start + static_cast<signed>( charCount );
    for( ; *start && start < end; ++start )
      ;

    ++start; // skip null char

    // Must always be embedded null character between desc & comment
    if( !PK_VALID( start != end ) )
      return {};

    ++start; // Skip BOM

    value = StringUtil::GetUtf8( std::wstring_view{ start, end } ); // comment text
  }
  else
  {
    auto charCount = byteCount / sizeof( char );
    std::string_view descPlusComment = str_.GetText( charCount );

    // Skip comment description
    auto start = std::begin( descPlusComment );
    auto end = start + static_cast<signed>( charCount );
    for( ; *start && start < end; ++start )
      ;

    ++start; // skip null char

    // Must always be embedded null character between desc & comment
    if( !PK_VALID( start != end ) )
      return {};

    value.assign( start, end ); // comment text
  }

  // In some buggy frames, trailing null bytes may be included, so strip them out
  StrUtil::ToTrimmedTrailing( value, std::string( { '\0' } ) );
  return value;
}

///////////////////////////////////////////////////////////////////////////////
//
// Validate ID3 comment fields

bool ID3v2CommentFrame::IsValid() const
{
  // static_assert( std::is_standard_layout_v<ID3v2CommentFrame> );
  // Derived classes with data don't have standard layouts due to potential padding,
  // but as long as the sizes are correct, we can use casting properly
  static_assert( sizeof( *this ) == sizeof( ID3v2FrameHdr ) +
                                    sizeof( textEncoding_ ) +
                                  ( sizeof( char) * kLanguageCharCount ) +
                                    sizeof(ID3v2String) );

  for( size_t i = 0; i < kLanguageCharCount; ++i )
  {
    if( !PK_VALID( language_[i] == '\0' || CharUtil::IsAlpha(language_[i])) )
      return false;
  }

  return PK_VALID( GetFrameID() == kCommentFrameID ) &&
         PK_VALID( str_.IsValid( ID3TextEncoding( textEncoding_ ) ) );
}

///////////////////////////////////////////////////////////////////////////////
//
// Get text value of private frame

std::string_view ID3v2PrivateFrame::GetText() const
{
  // For private frames, size isn't sync safe
  uint32_t maxFrameSize = GetSize( kMajorVersionWith8BitEncoding );
  uint32_t charCount = 0u;
  for( const char* s = str_; *s != '\0'; ++s, ++charCount )
  {
    // Safety check; failure indicates malformed frame
    if( !PK_VALID( charCount < maxFrameSize ) )
      return {};
  }
  return std::string_view{ str_, charCount };
}

///////////////////////////////////////////////////////////////////////////////
//
// Extract data from private frame

std::span<const uint8_t> ID3v2PrivateFrame::GetData( uint8_t majorVersion ) const
{
  //  rawFrame                           blobStart
  //  |                                  |
  //  v                                  v
  // |<-------------------------------->|<--------------->|
  // |                                                    |
  // |<--ID3v2FrameHdr-->|<---string--->|<-----blob------>|
  // |                                                    |
  // |                   |<----------frameSize----------->|
  // |                                                    |
  // |                   |<--strBytes-->|<---blobBytes--->|

  assert( majorVersion >= kMajorVersionMin && majorVersion <= kMajorVersionMax );
  uint32_t frameSize = GetSize( majorVersion );
  std::string_view str = GetText();
  size_t strBytes = str.size() + sizeof( '\0' );
  size_t blobBytes = static_cast<size_t>( frameSize - strBytes );
  const uint8_t* blobStart = reinterpret_cast<const uint8_t*>( str_ ) + strBytes;
  return std::span{ blobStart, blobBytes };
}

///////////////////////////////////////////////////////////////////////////////
