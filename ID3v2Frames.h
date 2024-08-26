///////////////////////////////////////////////////////////////////////////////
//
//  ID3v2Frames.h
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
//  ID3 frame structures. Intended to be used as "casted-to" objects, e.g.
//  ID3v2TextFrame* pTextFrame = reinterpret_cast<ID3v2TextFrame*>( pFrame->GetData() );
// 
///////////////////////////////////////////////////////////////////////////////

#pragma once
#include <cassert>
#include <cstdint>
#include <string>

#include "StrUtil.h"
#include "Util.h"

namespace // anonymous
{

static constexpr const char* kID3String = "ID3";
static constexpr const char* kEnglishLanguage = "eng";
static constexpr size_t      kFrameIDCharCount = 4;
static constexpr uint8_t     kByteOrderMark0 = 0xFE;
static constexpr uint8_t     kByteOrderMark1 = 0xFF;
static constexpr uint16_t    kByteOrderMark = 0xFEFF;

} // anonymous

namespace PKIsensee
{

enum class ID3TextEncoding
{
  ANSI = 0,
  UTF16 = 1,
  UTF16BE = 2,
  UTF8 = 3,
  Max
};

///////////////////////////////////////////////////////////////////////////////
//
// Converts a source integer from an ID3 file format big endian integer into a 
// useful native value. Automatically handles endian conversion. For syncSafe
// integers, kBitsPerByte is 7.

template <uint8_t kBitsPerByte>
uint32_t ReadID3Int( uint32_t sourceInt )
{
  auto result = Util::ToBigEndian( sourceInt );
  return Util::PackBits<kBitsPerByte>( result );
}

///////////////////////////////////////////////////////////////////////////////
//
// Converts a native integer from to an ID3 file format big endian integer.
// Automatically handles endian conversion. For syncSafe integers, 
// kBitsPerByte is 7.

template <uint8_t kBits>
uint32_t WriteID3Int( uint32_t nativeInt )
{
  auto result = Util::UnpackBits<kBits>( nativeInt );
  return Util::ToBigEndian( result );
}

#pragma pack(push,1) // Essential for strict binary layout of the ID3 file format

///////////////////////////////////////////////////////////////////////////////
//
// Standard MP3 ID3v2 frame header
// See https://mutagen-specs.readthedocs.io/en/latest/id3/id3v2.3.0.html

class ID3v2FrameHdr
{
private:

  // Order and size must not be modified
  char     frameID_[ kFrameIDCharCount ]; // e.g. "TALB"
  uint32_t syncSafeSize_;      // V3 sizes are regular ints; V4+ sizes are syncSafe ints
  uint8_t  statusMessages_;    // Whether the frame can be altered
  uint8_t  formatDescription_; // Compression, encryption and grouping

  constexpr static uint8_t kStatusReadOnly = ( 1 << 5 );

public:

  std::string GetFrameID() const
  {
    return std::string{ frameID_[ 0 ], frameID_[ 1 ], frameID_[ 2 ], frameID_[ 3 ] };
  }

  uint32_t GetSize( uint8_t majorVersion ) const
  {
    // Version 3: big endian value. Other versions are syncSafe.
    return ( majorVersion == 3 ) ? ReadID3Int<8>( syncSafeSize_ ) :
                                   ReadID3Int<7>( syncSafeSize_ );
  }

  void SetHeader( const std::string& frameID, uint32_t newFrameSize, uint8_t majorVersion )
  {
    assert( frameID.size() == kFrameIDCharCount );
    memcpy( frameID_, frameID.data(), kFrameIDCharCount );

    // Version 3: big endian value. Other versions are syncSafe values.
    syncSafeSize_ = ( majorVersion == 3 ) ? WriteID3Int<8>( newFrameSize ) :
                                            WriteID3Int<7>( newFrameSize );

    // Unused in current implementation
    statusMessages_ = 0;
    formatDescription_ = 0;
  }

  bool IsReadOnly() const
  {
    return statusMessages_ & kStatusReadOnly;
  }

  // None of this functionality currently needed, so unimplemented
  // See https://mutagen-specs.readthedocs.io/en/latest/id3/id3v2.3.0.html
  //
  // bool PreserveFrameOnTagAlter() const
  // bool PreserveFrameOnFileAlter() const
  // bool IsCompressed() const
  // bool IsEncrypted() const
  // bool IsGrouped() const
  // uint_8 GetGroupID() const
  // uint_8 GetEncryptionMethod() const

};

///////////////////////////////////////////////////////////////////////////////
//
// Standard MP3 string header
// See https://mutagen-specs.readthedocs.io/en/latest/id3/id3v2.3.0.html
//
// Strings can be encoded in different fashions; this class disambiguates
// https://mutagen-specs.readthedocs.io/en/latest/id3/id3v2.2.html#id3v2-frames-overview

class ID3v2String
{
public: // TODO private

  // Order and size must not be modified
  union {
    // textEncoding == ANSI OR UTF8
    char utf8_[ 1 ];        // string start

    // textEncoding == UTF16 OR UTF16BE
    struct Unicode
    {
      uint8_t  bom_[ 2 ] = { kByteOrderMark0, kByteOrderMark1 };
      wchar_t  utf16_[ 1 ]; // string start
    } unicode_;
  };

  friend class ID3v2TextFrame;
  friend class ID3v2CommentFrame;

public:

  ID3v2String() = delete; // only used as a casted-to object

  void SetText( const std::string& newText )
  {
    // Assumes sufficient memory allocated for ID3V2String buffer;
    // ID3 strings are not null terminated, hence memcpy
    memcpy( utf8_, newText.c_str(), newText.size() );
  }

  void SetText( const std::wstring& newText )
  {
    // Assumes sufficient memory is allocated for the ID3V2String buffer
    unicode_.bom_[ 0 ] = kByteOrderMark0;
    unicode_.bom_[ 1 ] = kByteOrderMark1;

    // ID3 strings are not null terminated, hence memcpy
    memcpy( unicode_.utf16_, newText.c_str(), newText.size() * sizeof( wchar_t ) );
  }

  std::string GetText( size_t charCount ) const
  {
    return std::string( utf8_, charCount );
  }

  std::wstring GetTextWide( size_t charCount ) const
  {
    return std::wstring( unicode_.utf16_, charCount );
  }

};

///////////////////////////////////////////////////////////////////////////////
//
// Standard MP3 text frame header
// See https://mutagen-specs.readthedocs.io/en/latest/id3/id3v2.2.html#text-information-frames

class ID3v2TextFrame : public ID3v2FrameHdr
{
private:

  // Order and size must not be modified
  uint8_t       textEncoding_; // see TextEncoding IDs above
  ID3v2String   str_;

public:

  ID3v2TextFrame() = delete; // only used as a casted-to object

  ID3TextEncoding GetTextEncoding() const
  {
    assert( textEncoding_ >= 0 );
    assert( textEncoding_ <= static_cast<uint8_t>( ID3TextEncoding::Max ) );
    return static_cast<ID3TextEncoding>( textEncoding_ );
  }

  bool IsWideString() const
  {
    auto textEncoding = GetTextEncoding();
    return ( textEncoding == ID3TextEncoding::UTF16 ) || 
           ( textEncoding == ID3TextEncoding::UTF16BE );
  }

  std::string GetText( uint8_t majorVersion ) const
  {
    // Determine size of string
    auto byteCount = GetTextBytes( majorVersion );

    // Read data; current implementation always returns std::string for simplicity
    std::string value;
    if( IsWideString() )
    {
      assert( byteCount % 2 == 0 );
      auto charCount = byteCount / sizeof( wchar_t );
      std::wstring unicode = str_.GetTextWide( charCount );
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

  static uint32_t ComputeFrameSize( const std::string& newText )
  {
    auto size = sizeof( ID3v2TextFrame );
    size -= sizeof( ID3v2String ); // don't include faux string disambiguator

    // Create ANSI text frames for simplicity
    // If UTF16 needed, add wstring method that multiplies this value by sizeof(wchar_t)
    size += newText.size();
    return static_cast<uint32_t>( size );
  }

  void SetText( const std::string& newText )
  {
    textEncoding_ = static_cast<uint8_t>( ID3TextEncoding::ANSI );
    str_.SetText( newText );
  }

private:

  uint32_t GetTextBytes( uint8_t majorVersion ) const // TODO merge with other GetTextBytes
  {
    //  rawFrame                   textStart
    //  |                          |
    //  v                          v
    // |<------------------------>|<-------------->|
    // |                                           |
    // |<--ID3v2FrameHdr-->|<-----frameSize------->|
    // |                                           |
    // |<----------offset-------->|<--textBytes--->|

    auto rawFrame = reinterpret_cast<const uint8_t*>( this );
    auto wideStrStart   = reinterpret_cast<const uint8_t*>( str_.unicode_.utf16_ );
    auto narrowStrStart = reinterpret_cast<const uint8_t*>( str_.utf8_ );
    auto textStart = IsWideString() ? wideStrStart : narrowStrStart;
    assert( rawFrame < textStart );
    ptrdiff_t offset = textStart - rawFrame;
    uint32_t offsetU32 = static_cast<uint32_t>( offset );

    uint32_t frameSize = GetSize( majorVersion );
    uint32_t textBytes = sizeof( ID3v2FrameHdr ) + frameSize;
    assert( offsetU32 <= textBytes );
    textBytes -= offsetU32;
    return textBytes;
  }

};

///////////////////////////////////////////////////////////////////////////////
//
// Standard MP3 comment frame header
// See https://mutagen-specs.readthedocs.io/en/latest/id3/id3v2.2.html#comments

class ID3v2CommentFrame : public ID3v2FrameHdr // 'COMM' header
{
private:
  // Order and size must not be modified
  uint8_t       textEncoding_; // see TextEncoding IDs above
public:
  char          language_[ 3 ]; // e.g. "eng"
  ID3v2String   str_;

public:

  ID3v2CommentFrame() = delete; // only used as a casted-to object

  ID3TextEncoding GetTextEncoding() const
  {
    assert( textEncoding_ >= 0 );
    assert( textEncoding_ <= static_cast<uint8_t>( ID3TextEncoding::Max ) );
    return static_cast< ID3TextEncoding >( textEncoding_ );
  }

  bool IsWideString() const
  {
    auto textEncoding = GetTextEncoding();
    return ( textEncoding == ID3TextEncoding::UTF16 ) ||
           ( textEncoding == ID3TextEncoding::UTF16BE );
  }

  std::string GetText( uint8_t majorVersion ) const
  {
    // Determine size of string
    auto byteCount = GetTextBytes( majorVersion );

    // Read data; current implementation always returns std::string for simplicity.
    // Comment is made up of description text, then comment text. The separator is a
    // null byte.
    std::string value;
    if( IsWideString() )
    {
      assert( byteCount % 2 == 0 );
      auto charCount = byteCount / sizeof( wchar_t );
      std::wstring descPlusComment = str_.GetTextWide( charCount );

      // Skip comment description
      auto start = std::begin( descPlusComment );
      auto end = start + static_cast<signed>( charCount );
      for( ; *start++; )
        ;

      // Skip BOM
      assert( *start == kByteOrderMark );
      start++;

      value = StringUtil::GetUtf8( std::wstring( start, end ) ); // comment text
    }
    else
    {
      auto charCount = byteCount / sizeof( char );
      std::string descPlusComment = str_.GetText( charCount );

      // Skip comment description
      auto start = std::begin( descPlusComment );
      auto end = start + static_cast<signed>( charCount );
      for( ; *start++; )
        ;

      value.assign( start, end ); // comment text
    }

    // In some buggy frames, trailing null bytes may be included, so strip them out
    StrUtil::ToTrimmedTrailing( value, std::string( { '\0' } ) );
    return value;
  }

  static uint32_t ComputeFrameSize( const std::string& newComment )
  {
    auto size = sizeof( ID3v2CommentFrame );
    size -= sizeof( ID3v2String ); // don't include faux string disambiguator

    // Create ANSI comment frames for simplicity
    // If UTF16 needed, add wstring method that multiplies these values by sizeof(wchar_t)
    size += sizeof( '\0' ); // empty description; add new param if needed
    size += newComment.size();
    return static_cast<uint32_t>( size );
  }

  void SetText( const std::string& newText )
  {
    textEncoding_ = static_cast<uint8_t>( ID3TextEncoding::ANSI );
    memcpy( language_, kEnglishLanguage, 3 );
    *str_.utf8_ = '\0'; // empty description; add new param if needed
    memcpy( str_.utf8_ + sizeof( '\0' ), newText.c_str(), newText.size() );
  }

private:

  uint32_t GetTextBytes( uint8_t majorVersion ) const // TODO merge with other GetTextBytes
  {
    //  rawFrame                   textStart
    //  |                          |
    //  v                          v
    // |<------------------------>|<-------------->|
    // |                                           |
    // |<--ID3v2FrameHdr-->|<-----frameSize------->|
    // |                                           |
    // |<----------offset-------->|<--textBytes--->|

    auto rawFrame = reinterpret_cast<const uint8_t*>( this );
    auto wideStrStart = reinterpret_cast<const uint8_t*>( str_.unicode_.utf16_ );
    auto narrowStrStart = reinterpret_cast<const uint8_t*>( str_.utf8_ );
    auto textStart = IsWideString() ? wideStrStart : narrowStrStart;
    assert( rawFrame < textStart );
    ptrdiff_t offset = textStart - rawFrame;
    uint32_t offsetU32 = static_cast<uint32_t>( offset );

    uint32_t frameSize = GetSize( majorVersion );
    uint32_t textBytes = sizeof( ID3v2FrameHdr ) + frameSize;
    assert( offsetU32 <= textBytes );
    textBytes -= offsetU32;
    return textBytes;
  }

};
#pragma pack(pop) // end (push,1)

} // namespace PKIsensee

///////////////////////////////////////////////////////////////////////////////
