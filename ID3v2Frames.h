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
#include <string_view>

#include "StrUtil.h"
#include "Util.h"

namespace // anonymous
{

static constexpr const char* kID3String = "ID3";
static constexpr const char* kEnglishLanguage = "eng";
static constexpr size_t      kID3CharCount = 3;
static constexpr size_t      kLanguageCharCount = 3;
static constexpr size_t      kFrameIDCharCount = 4;
static constexpr uint8_t     kByteOrderMark0 = 0xFF; // UTF16; swapped for UTF16BE
static constexpr uint8_t     kByteOrderMark1 = 0xFE;
static constexpr uint8_t     kMajorVersionMin = 3;
static constexpr uint8_t     kMajorVersionWith8BitEncoding = 3;
static constexpr uint8_t     kMajorVersionMax = 7; // v4 circa 2024

///////////////////////////////////////////////////////////////////////////////
//
// In v3, sizes are typically encoded as normal big endian values.
// Starting in v4+, sizes are encoded using 7-bit sections called sync safe values.
// See id3 6.2, https://en.wikipedia.org/wiki/Synchsafe

bool UseSyncSafeSize( uint8_t majorVersion )
{
  return majorVersion > kMajorVersionWith8BitEncoding;
}

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
// ID3 (MP3) file header
//
// See: https://mutagen-specs.readthedocs.io/en/latest/id3/id3v2.4.0-structure.html#id3v2-header

class PK_PACKED_STRUCT ID3v2FileHeader // packing essential to match ID3 file format
{
private:

PK_START_PACK
  // Order and sizes must not be modified
  char     id3_[kID3CharCount] = {}; // 'ID3'
  uint8_t  majorVersion_ = 0;        // e.g. 2; never 0xFF
  uint8_t  minorVersion_ = 0;        // e.g. 3; never 0xFF
  uint8_t  flags_ = 0;               // values in Mp3TagData.cpp
  uint32_t syncSafeSize_ = 0;        // see id3 6.2, https://en.wikipedia.org/wiki/Synchsafe
PK_END_PACK

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

  std::string_view GetHeaderID() const
  {
    return std::string_view{ id3_, kID3CharCount };
  }

  uint8_t GetMajorVersion() const
  {
    return IsValid() ? majorVersion_ : kMajorVersionMin;
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

  bool IsValid() const;

}; // class ID3v2FileHeader

///////////////////////////////////////////////////////////////////////////////
//
// ID3 string header
// 
// See https://mutagen-specs.readthedocs.io/en/latest/id3/id3v2.3.0.html
//
// ID3 strings can be encoded in 8 and 16-bit formats
// https://mutagen-specs.readthedocs.io/en/latest/id3/id3v2.2.html#id3v2-frames-overview

class PK_PACKED_STRUCT ID3v2String // packing essential to match ID3 file format
{
private:

PK_START_PACK
  // Order and size must not be modified
  union {
    char utf8_[ 1 ];        // string start for textEncoding == ANSI or UTF8

    struct Unicode          // textEncoding == UTF16 or UTF16BE
    {
      // UTF16 specified; reverse for UTF16BE
      uint8_t  bom_[ 2 ] = { kByteOrderMark0, kByteOrderMark1 };
      wchar_t  utf16_[ 1 ]; // string start for UTF16 or UTF16BE
    } unicode_;
  };
PK_END_PACK

  friend class ID3v2TextFrame;
  friend class ID3v2CommentFrame;

public:

  ID3v2String() = delete; // only used as a casted-to object

  const uint8_t* GetTextStart( bool isWideString ) const // TODO private?
  {
    auto wideStrStart   = reinterpret_cast<const uint8_t*>( unicode_.utf16_ );
    auto narrowStrStart = reinterpret_cast<const uint8_t*>( utf8_ );
    auto textStart = isWideString ? wideStrStart : narrowStrStart;
    return textStart;
  }

  void SetText( std::string_view newText )
  {
    // Requires ANSI or UTF8 text encoding
    // Assumes sufficient memory allocated for ID3V2String buffer;
    // ID3 strings are not null terminated, hence memcpy
    memcpy( utf8_, newText.data(), newText.size() );
  }

  std::string_view GetText( size_t charCount ) const
  {
    // Valid if textEncoding == ANSI or UTF8
    return std::string_view( utf8_, charCount );
  }

  std::wstring_view GetTextWide( size_t charCount ) const
  {
    // Valid if textEncoding == UTF16 or UTF16BE
    return std::wstring_view( unicode_.utf16_, charCount );
  }

  void SetText( std::wstring_view newText, ID3TextEncoding textEncoding = ID3TextEncoding::UTF16 );
  bool IsValid( ID3TextEncoding textEncoding ) const;
  static bool IsValidTextEncoding( uint8_t );

}; // class ID3v2String

///////////////////////////////////////////////////////////////////////////////
//
// ID3 frame header
// 
// See https://mutagen-specs.readthedocs.io/en/latest/id3/id3v2.3.0.html

class PK_PACKED_STRUCT ID3v2FrameHdr // packing essential to match ID3 file format
{
private:

PK_START_PACK
  // Order and size must not be modified
  char     frameID_[kFrameIDCharCount] = {}; // e.g. "TALB"
  uint32_t syncSafeSize_ = 0;      // V3 sizes are regular ints; V4+ sizes are syncSafe ints
  uint8_t  statusMessages_ = 0;    // Whether the frame can be altered
  uint8_t  formatDescription_ = 0; // Compression, encryption and grouping
  // additional data follows here
PK_END_PACK

  constexpr static uint8_t kStatusReadOnly = ( 1 << 5 );

public:

  std::string_view GetFrameID() const
  {
    return std::string_view{ frameID_, kFrameIDCharCount };
  }

  uint32_t GetSize( uint8_t majorVersion ) const
  {
    // Version 3: big endian value. Other versions are syncSafe.
    assert( majorVersion >= kMajorVersionMin && majorVersion <= kMajorVersionMax );
    return UseSyncSafeSize( majorVersion ) ? ReadID3Int<7>( syncSafeSize_ ) :
                                             ReadID3Int<8>( syncSafeSize_ );
  }

  bool IsReadOnly() const
  {
    return statusMessages_ & kStatusReadOnly;
  }

  void SetHeader( std::string_view frameID, uint32_t newFrameSize, uint8_t majorVersion );

  // Determine text size when "this" is ID3v2TextFrame with embedded ID3v2String
  uint32_t GetTextBytes( const ID3v2String&, uint8_t majorVersion, bool isWideString ) const;

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

}; // class ID3v2FrameHdr

///////////////////////////////////////////////////////////////////////////////
//
// MP3 text frame header
// 
// See https://mutagen-specs.readthedocs.io/en/latest/id3/id3v2.2.html#text-information-frames

class PK_PACKED_STRUCT ID3v2TextFrame : public ID3v2FrameHdr // packing essential to match ID3 file format
{
private:

PK_START_PACK
  // Order and size must not be modified
  uint8_t       textEncoding_; // see TextEncoding IDs above
  ID3v2String   str_;          // see ID3v2String above
PK_END_PACK

public:

  ID3v2TextFrame() = delete; // only used as a casted-to object

  ID3TextEncoding GetTextEncoding() const
  {
    return IsValid() ? ID3TextEncoding(textEncoding_) : ID3TextEncoding::ANSI;
  }

  bool IsWideString() const
  {
    auto textEncoding = GetTextEncoding();
    return ( textEncoding == ID3TextEncoding::UTF16 ) || 
           ( textEncoding == ID3TextEncoding::UTF16BE );
  }

  void SetText( std::string_view newText )
  {
    textEncoding_ = uint8_t( ID3TextEncoding::ANSI );
    str_.SetText( newText );
  }

  bool IsValid() const
  {
    // static_assert( std::is_standard_layout_v<ID3v2TextFrame> );
    // Derived classes with data don't have standard layouts due to potential padding,
    // but as long as the sizes are correct, we can use casting properly
    static_assert( sizeof( *this ) == sizeof( ID3v2FrameHdr ) + 
                                      sizeof( textEncoding_ ) + 
                                      sizeof(str_) );
    return str_.IsValid( ID3TextEncoding( textEncoding_ ) );
  }

  // Determine new frame size given text value
  static uint32_t GetFrameSize( std::string_view newText )
  {
    auto size = sizeof( ID3v2TextFrame );
    size -= sizeof( ID3v2String ); // don't include faux string disambiguator

    // Assume ANSI text frames for simplicity
    // If UTF16[BE] needed, add wstring method that multiplies this 
    // value by sizeof(wchar_t)
    size += newText.size();
    return static_cast<uint32_t>( size );
  }

  std::string GetText( uint8_t majorVersion ) const;

}; // class ID3v2TextFrame

///////////////////////////////////////////////////////////////////////////////
//
// MP3 comment frame header
// 
// See https://mutagen-specs.readthedocs.io/en/latest/id3/id3v2.2.html#comments

class PK_PACKED_STRUCT ID3v2CommentFrame : public ID3v2FrameHdr // 'COMM' header
{
private:

PK_START_PACK
  // Order and size must not be modified
  uint8_t     textEncoding_;  // see TextEncoding IDs above
  char        language_[kLanguageCharCount]; // e.g. "eng"
  ID3v2String str_;           // comment description, null char, then comment
PK_END_PACK

public:

  ID3v2CommentFrame() = delete; // only used as a casted-to object

  ID3TextEncoding GetTextEncoding() const
  {
    return IsValid() ? ID3TextEncoding( textEncoding_ ) : ID3TextEncoding::ANSI;
  }

  bool IsWideString() const
  {
    auto textEncoding = GetTextEncoding();
    return ( textEncoding == ID3TextEncoding::UTF16 ) ||
           ( textEncoding == ID3TextEncoding::UTF16BE );
  }

  // Determine new frame size given new comment
  static uint32_t GetFrameSize( std::string_view newComment )
  {
    auto size = sizeof( ID3v2CommentFrame );
    size -= sizeof( ID3v2String ); // don't include faux string disambiguator

    // Assume ANSI comment text for simplicity
    // If UTF16[BE] needed, add wstring method that multiplies this 
    // value by sizeof(wchar_t)
    size += sizeof( '\0' ); // empty description; add new param if needed
    size += newComment.size();
    return static_cast<uint32_t>( size );
  }

  void SetText( std::string_view newText )
  {
    // assume ANSI comment text, English language, no comment description
    textEncoding_ = static_cast<uint8_t>( ID3TextEncoding::ANSI );
    memcpy( language_, kEnglishLanguage, kLanguageCharCount );
    *str_.utf8_ = '\0'; // empty description; add new param if needed
    memcpy( str_.utf8_ + sizeof( '\0' ), newText.data(), newText.size() );
  }

  std::string GetText( uint8_t majorVersion ) const;
  bool IsValid() const;

}; // ID3v2CommentFrame

///////////////////////////////////////////////////////////////////////////////
//
// MP3 private frame header
// 
// See https://mutagen-specs.readthedocs.io/en/latest/id3/id3v2.3.0.html

class PK_PACKED_STRUCT ID3v2PrivateFrame : public ID3v2FrameHdr
{
private:

PK_START_PACK
  // Order and size must not be modified
  char str_[ 1 ];     // null terminated
  // Followed by a binary blob
PK_END_PACK

public:

  ID3v2PrivateFrame() = delete; // only used as a casted-to object

  std::string_view GetText() const;
  std::span<const uint8_t> GetData( uint8_t majorVersion ) const;

}; // class ID3v2PrivateFrame

} // namespace PKIsensee

///////////////////////////////////////////////////////////////////////////////
