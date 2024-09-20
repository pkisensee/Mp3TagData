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
#include <span>
#include <string>

#include "StrUtil.h"
#include "Util.h"

namespace // anonymous
{

static constexpr const char* kID3String = "ID3";
static constexpr const char* kEnglishLanguage = "eng";
static constexpr size_t      kFrameIDCharCount = 4;
static constexpr uint16_t    kByteOrderMark = 0xFEFF;
static constexpr uint8_t     kByteOrderMark0 = 0xFE;
static constexpr uint8_t     kByteOrderMark1 = 0xFF;

// syncSafeSize: V3: plain old big endian value, V4+: syncSafe integer
static constexpr uint8_t     kMajorVersionWith8BitSize = 3;

///////////////////////////////////////////////////////////////////////////////
//
// Converts a source integer from an ID3 file format big endian integer into a 
// useful native value. Automatically handles endian conversion. For syncSafe
// integers, kBitsPerByte is 7.

template <uint8_t kBitsPerByte>
uint32_t ReadID3Int( uint32_t sourceInt )
{
  using namespace PKIsensee;
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
  using namespace PKIsensee;
  auto result = Util::UnpackBits<kBits>( nativeInt );
  return Util::ToBigEndian( result );
}

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
// MP3 file header
//
// See: https://mutagen-specs.readthedocs.io/en/latest/id3/id3v2.4.0-structure.html#id3v2-header

class ID3v2FileHeader
{
private:

#pragma pack(push,1) // Essential for strict binary layout of the ID3 file format
  // Order and size must not be modified
  char     id3_[ 3 ] = {};    // 'ID3'
  uint8_t  majorVersion_ = 0; // e.g. 2; never 0xFF
  uint8_t  minorVersion_ = 0; // e.g. 3; never 0xFF
  uint8_t  flags_ = 0;        // values in Mp3TagData.cpp
  uint32_t syncSafeSize_ = 0; // see id3 6.2, https://en.wikipedia.org/wiki/Synchsafe
#pragma pack(pop)

public:

  [[maybe_unused]] static constexpr uint8_t kFlagUnsynchronized = ( 1 << 7 );
  [[maybe_unused]] static constexpr uint8_t kFlagExtended       = ( 1 << 6 );
  [[maybe_unused]] static constexpr uint8_t kFlagExperimental   = ( 1 << 5 );
  [[maybe_unused]] static constexpr uint8_t kFlagFooterPresent  = ( 1 << 4 );
  [[maybe_unused]] static constexpr uint8_t kFlagsRemaining = ( 0b00001111 ); // must be cleared

public:

  ID3v2FileHeader() = default;
  ID3v2FileHeader( const ID3v2FileHeader& ) = default;
  ID3v2FileHeader& operator=( const ID3v2FileHeader& ) = delete;
  ID3v2FileHeader( ID3v2FileHeader&& ) = delete;
  ID3v2FileHeader& operator=( ID3v2FileHeader&& ) = delete;

  std::string GetHeaderID() const
  {
    return std::string{ id3_[ 0 ], id3_[ 1 ], id3_[ 2 ] };
  }

  uint8_t GetMajorVersion() const
  {
    return majorVersion_;
  }

  uint8_t GetMinorVersion() const
  {
    return minorVersion_;
  }

  uint8_t GetFlags() const
  {
    return flags_;
  }

  uint32_t GetSize() const
  {
    return ReadID3Int<7>( syncSafeSize_ );
  }

  void SetSize( uint32_t newSize )
  {
    syncSafeSize_ = WriteID3Int<7>( newSize );
  }

};

///////////////////////////////////////////////////////////////////////////////
//
// MP3 string header
// 
// See https://mutagen-specs.readthedocs.io/en/latest/id3/id3v2.3.0.html
//
// Strings can be encoded in different fashions; this class disambiguates
// https://mutagen-specs.readthedocs.io/en/latest/id3/id3v2.2.html#id3v2-frames-overview

class ID3v2String
{
private:

#pragma pack(push,1) // Essential for strict binary layout of the ID3 file format
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
#pragma pack(pop)

  friend class ID3v2TextFrame;
  friend class ID3v2CommentFrame;

public:

  ID3v2String() = delete; // only used as a casted-to object

  const uint8_t* GetTextStart( bool isWideString ) const
  {
    auto wideStrStart   = reinterpret_cast<const uint8_t*>( unicode_.utf16_ );
    auto narrowStrStart = reinterpret_cast<const uint8_t*>( utf8_ );
    auto textStart = isWideString ? wideStrStart : narrowStrStart;
    return textStart;
  }

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
// MP3 frame header
// 
// See https://mutagen-specs.readthedocs.io/en/latest/id3/id3v2.3.0.html

class ID3v2FrameHdr
{
private:

#pragma pack(push,1) // Essential for strict binary layout of the ID3 file format
  // Order and size must not be modified
  char     frameID_[ kFrameIDCharCount ]; // e.g. "TALB"
  uint32_t syncSafeSize_;      // V3 sizes are regular ints; V4+ sizes are syncSafe ints
  uint8_t  statusMessages_;    // Whether the frame can be altered
  uint8_t  formatDescription_; // Compression, encryption and grouping
#pragma pack(pop)

  constexpr static uint8_t kStatusReadOnly = ( 1 << 5 );

public:

  std::string GetFrameID() const
  {
    return std::string{ frameID_[ 0 ], frameID_[ 1 ], frameID_[ 2 ], frameID_[ 3 ] };
  }

  uint32_t GetSize( uint8_t majorVersion ) const
  {
    // Version 3: big endian value. Other versions are syncSafe.
    assert( majorVersion >= kMajorVersionWith8BitSize );
    return ( majorVersion == kMajorVersionWith8BitSize ) ? ReadID3Int<8>( syncSafeSize_ ) :
                                                           ReadID3Int<7>( syncSafeSize_ );
  }

  void SetHeader( const std::string& frameID, uint32_t newFrameSize, uint8_t majorVersion )
  {
    assert( majorVersion >= kMajorVersionWith8BitSize );
    assert( frameID.size() == kFrameIDCharCount );
    memcpy( frameID_, frameID.data(), kFrameIDCharCount );

    // Version 3: big endian value. Other versions are syncSafe values.
    syncSafeSize_ = ( majorVersion == kMajorVersionWith8BitSize ) ? WriteID3Int<8>( newFrameSize ) :
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

  // Determine text size when "this" is ID3v2TextFrame with embedded ID3v2String
  uint32_t GetTextBytes( const ID3v2String& str, uint8_t majorVersion, bool isWideString ) const
  {
    //  rawFrame                   textStart
    //  |                          |
    //  v                          v
    // |<------------------------>|<-------------->|
    // |                                           |
    // |<--ID3v2FrameHdr-->|<-----frameSize------->|
    // |                                           |
    // |<----------offset-------->|<--textBytes--->|

    assert( majorVersion >= kMajorVersionWith8BitSize );
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

};

///////////////////////////////////////////////////////////////////////////////
//
// MP3 text frame header
// 
// See https://mutagen-specs.readthedocs.io/en/latest/id3/id3v2.2.html#text-information-frames

class ID3v2TextFrame : public ID3v2FrameHdr
{
private:

#pragma pack(push,1) // Essential for strict binary layout of the ID3 file format
  // Order and size must not be modified
  uint8_t       textEncoding_; // see TextEncoding IDs above
  ID3v2String   str_;
#pragma pack(pop)

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
    assert( majorVersion >= kMajorVersionWith8BitSize );
    bool isWideString = IsWideString();

    // Determine size of string
    auto byteCount = GetTextBytes( str_, majorVersion, isWideString );

    // Read data; current implementation always returns std::string for simplicity
    std::string value;
    if( isWideString )
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

  void SetText( const std::string& newText )
  {
    textEncoding_ = static_cast<uint8_t>( ID3TextEncoding::ANSI );
    str_.SetText( newText );
  }

  static uint32_t GetFrameSize( const std::string& newText )
  {
    auto size = sizeof( ID3v2TextFrame );
    size -= sizeof( ID3v2String ); // don't include faux string disambiguator

    // Create ANSI text frames for simplicity
    // If UTF16 needed, add wstring method that multiplies this value by sizeof(wchar_t)
    size += newText.size();
    return static_cast<uint32_t>( size );
  }

};

///////////////////////////////////////////////////////////////////////////////
//
// MP3 comment frame header
// 
// See https://mutagen-specs.readthedocs.io/en/latest/id3/id3v2.2.html#comments

class ID3v2CommentFrame : public ID3v2FrameHdr // 'COMM' header
{
private:

#pragma pack(push,1) // Essential for strict binary layout of the ID3 file format
  // Order and size must not be modified
  uint8_t       textEncoding_;  // see TextEncoding IDs above
  char          language_[ 3 ]; // e.g. "eng"
  ID3v2String   str_;           // contains both description and comment
#pragma pack(pop)

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
    assert( majorVersion >= kMajorVersionWith8BitSize );
    bool isWideString = IsWideString();

    // Determine size of string
    auto byteCount = GetTextBytes( str_, majorVersion, isWideString );

    // Read data; current implementation always returns std::string for simplicity.
    // Comment is made up of description text, then comment text, separated by a null byte.
    std::string value;
    if( isWideString )
    {
      assert( byteCount % 2 == 0 );
      auto charCount = byteCount / sizeof( wchar_t );
      std::wstring descriptionAndComment = str_.GetTextWide( charCount );

      // Skip comment description
      auto start = std::begin( descriptionAndComment );
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

  static uint32_t GetFrameSize( const std::string& newComment )
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
};

///////////////////////////////////////////////////////////////////////////////
//
// MP3 private frame header
// 
// See https://mutagen-specs.readthedocs.io/en/latest/id3/id3v2.3.0.html

class ID3v2PrivateFrame : public ID3v2FrameHdr
{
private:

#pragma pack(push,1) // Essential for strict binary layout of the ID3 file format
  // Order and size must not be modified
  char str_[ 1 ];     // null terminated
  // Followed by a binary blob
#pragma pack(pop)

public:

  ID3v2PrivateFrame() = delete; // only used as a casted-to object

  std::string GetText() const
  {
    uint32_t maxFrameSize = GetSize( kMajorVersionWith8BitSize );
    uint32_t charCount = 0u;
    std::string value;
    const char* s = str_;
    for( ; *s != '\0'; ++s, ++charCount )
    {
      // Safety check
      if( charCount == maxFrameSize )
      {
        // Failure indicates malformed frame
        assert( charCount < maxFrameSize );
        break;
      }
      value.push_back( *s );
    }
    return value;
  }

  std::span<const uint8_t> GetData( uint8_t majorVersion ) const
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

    assert( majorVersion >= kMajorVersionWith8BitSize );
    uint32_t frameSize = GetSize( majorVersion );
    std::string str = GetText();
    size_t strBytes = str.size() + sizeof( '\0' );
    size_t blobBytes = static_cast<size_t>( frameSize - strBytes );
    const uint8_t* blobStart = reinterpret_cast<const uint8_t*>( str_ ) + strBytes;
    return std::span{ blobStart, blobBytes };
  }

};

} // namespace PKIsensee

///////////////////////////////////////////////////////////////////////////////
