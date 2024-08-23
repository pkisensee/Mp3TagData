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
//  ID3 frame structures
// 
///////////////////////////////////////////////////////////////////////////////

#pragma once
#include <cstdint>
#include <string>

#include "Util.h"

namespace // anonymous
{

static constexpr const char* kID3String = "ID3";
static constexpr const char* kEnglishLanguage = "eng";
static constexpr size_t      kFrameIDCharCount = 4;

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

    // Version 3: big endian value. Other versions are syncSafe.
    syncSafeSize_ = ( majorVersion == 3 ) ? WriteID3Int<8>( newFrameSize ) :
                                            WriteID3Int<7>( newFrameSize );

    // not used by this implementation
    statusMessages_ = 0;
    formatDescription_ = 0;
  }

  bool IsReadOnly() const
  {
    return statusMessages_ & kStatusReadOnly;
  }

  // None of these currently used, so unimplemented
  // See https://mutagen-specs.readthedocs.io/en/latest/id3/id3v2.3.0.html
  //
  // bool PreserveFrameOnTagAlter() const
  // bool PreserveFrameOnFileAlter() const
  // bool IsReadOnly() const
  // bool IsCompressed() const
  // bool IsEncrypted() const
  // bool IsGrouped() const
  // uint_8 GetGroupID() const
  // uint_8 GetEncryptionMethod() const

};

// Strings can be encoded in different fashions; this struct disambiguates
// https://mutagen-specs.readthedocs.io/en/latest/id3/id3v2.2.html#id3v2-frames-overview

class ID3v2String // TODO member functions
{
public: // TODO private

  union {
    // textEncoding == ANSI OR UTF8
    char utf8[ 1 ];        // string start

    // textEncoding == UTF16 OR UTF16BE
    struct Unicode
    {
      uint8_t  bom[ 2 ] = { 0xFE, 0xFF };
      wchar_t  utf16[ 1 ]; // string start
    } unicode;
  };

public:

  ID3v2String() = delete; // only used as a casted-to object

  void SetText( const std::string& newText )
  {
    // assumes sufficient memory is allocated for the ID3V2String buffer;
    // not null terminated
    memcpy( utf8, newText.c_str(), newText.size() );
  }

  void SetText( const std::wstring& newText )
  {
    // assumes sufficient memory is allocated for the ID3V2String buffer
    unicode.bom[ 0 ] = 0xFE;
    unicode.bom[ 1 ] = 0xFF;
    // not null terminated
    memcpy( unicode.utf16, newText.c_str(), newText.size() * sizeof( wchar_t ) );
  }
};

// https://mutagen-specs.readthedocs.io/en/latest/id3/id3v2.2.html#text-information-frames
class ID3v2TextFrame : public ID3v2FrameHdr
{
private:

  uint8_t       textEncoding_; // see TextEncoding IDs above
public: // TODO remove
  ID3v2String   str_;

public:

  ID3v2TextFrame() = delete; // only used as a casted-to object

  ID3TextEncoding GetTextEncoding() const
  {
    assert( textEncoding_ >= 0 );
    assert( textEncoding_ <= static_cast<uint8_t>( ID3TextEncoding::Max ) );
    return static_cast<ID3TextEncoding>( textEncoding_ );
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
    memcpy( str_.utf8, newText.c_str(), newText.size() ); // TODO add to ID3v2String
  }
};

// https://mutagen-specs.readthedocs.io/en/latest/id3/id3v2.2.html#comments
struct ID3v2CommentFrame : public ID3v2FrameHdr // 'COMM' header
{
private:
  uint8_t       textEncoding_; // see TextEncoding IDs above
public:
  char          language[ 3 ]; // e.g. "eng"
  ID3v2String   str;

public:

  ID3v2CommentFrame() = delete; // only used as a casted-to object

  ID3TextEncoding GetTextEncoding() const
  {
    assert( textEncoding_ >= 0 );
    assert( textEncoding_ <= static_cast<uint8_t>( ID3TextEncoding::Max ) );
    return static_cast< ID3TextEncoding >( textEncoding_ );
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
    memcpy( language, kEnglishLanguage, 3 );
    *str.utf8 = '\0'; // empty description; add new param if needed
    memcpy( str.utf8 + sizeof( '\0' ), newText.c_str(), newText.size() );
  }

};
#pragma pack(pop) // end (push,1)

} // namespace PKIsensee

///////////////////////////////////////////////////////////////////////////////
