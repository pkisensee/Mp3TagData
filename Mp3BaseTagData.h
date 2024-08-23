///////////////////////////////////////////////////////////////////////////////
//
//  Mp3BaseTagData.h
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

#pragma once
#include <string>

#include "..\frozen\unordered_map.h"
#include "Id3v2Frames.h"
#include "StrUtil.h"

namespace PKIsensee
{

enum class Mp3FrameType
{
  None = 0,
  First = 1,

  // Text frames
  Title = First,   // TIT2
  Subtitle,        // TIT3
  Genre,           // TCON
  Artist,          // TPE1
  Album,           // TALB
  Composer,        // TCOM
  Orchestra,       // TPE2
  OrigArtist,      // TOPE
  Year,            // TYER
  OrigYear,        // TORY
  TrackNum,        // TRCK
  BeatsPerMinute,  // TBPM
  Duration,        // TLEN
  Key,             // TKEY
  Conductor,       // TPE3
  Language,        // TLAN
  Mood,            // TMOO
  // Add new text frame entries here and to match below

  // Other frames
  Comment,         // COMM
  // Add new non-text frame entries here and to match below

  Max
};

constexpr size_t kMaxFrameTypes = static_cast<size_t>( Mp3FrameType::Max );
constexpr frozen::unordered_map< Mp3FrameType, const char*, kMaxFrameTypes >
kMp3FrameID =
{
  // https://mutagen-specs.readthedocs.io/en/latest/id3/id3v2.3.0.html#4.2.1
  // This is not an inclusive list, only the most useful frame info
  { Mp3FrameType::None,           ""     },
  { Mp3FrameType::Title,          "TIT2" },
  { Mp3FrameType::Subtitle,       "TIT3" }, // Rare, e.g. "Op. 6"
  { Mp3FrameType::Genre,          "TCON" }, // May be numeric; may have parens, e.g. "(21)"
  { Mp3FrameType::Artist,         "TPE1" }, // May have forward slashes as separators
  { Mp3FrameType::Album,          "TALB" },
  { Mp3FrameType::Composer,       "TCOM" }, // May have forward slashes as separators
  { Mp3FrameType::Orchestra,      "TPE2" }, // Often called "Album Artist"
  { Mp3FrameType::OrigArtist,     "TOPE" }, // Rare
  { Mp3FrameType::Year,           "TYER" }, // YYYY
  { Mp3FrameType::OrigYear,       "TORY" }, // Rare; YYYY
  { Mp3FrameType::TrackNum,       "TRCK" }, // e.g. "5" or "5/12"
  { Mp3FrameType::BeatsPerMinute, "TBPM" }, // e.g. "100"
  { Mp3FrameType::Duration,       "TLEN" }, // Milliseconds; often incorrect for VBR
  { Mp3FrameType::Key,            "TKEY" }, // e.g. "C#m"
  { Mp3FrameType::Conductor,      "TPE3" },
  { Mp3FrameType::Language,       "TLAN" }, // Rare; ISO-639-2 3-char codes
  { Mp3FrameType::Mood,           "TMOO" }, // v2.4; rare
  { Mp3FrameType::Comment,        "COMM" }  // Multiple allowed
};

inline Mp3FrameType& operator++( Mp3FrameType& frameType )
{
  return frameType = static_cast<Mp3FrameType>( static_cast<int>( frameType ) + 1 );
}

///////////////////////////////////////////////////////////////////////////////
//
// See Mp3GenreList.cpp for full list

constexpr size_t kMaxGenre = 125;
extern const char* kStaticGenreList[ kMaxGenre + 1 ];

///////////////////////////////////////////////////////////////////////////////
//
// Abstract base class interface

class Mp3BaseTagData
{
public:

  virtual ~Mp3BaseTagData() = default;
  Mp3BaseTagData() = default;

  Mp3BaseTagData( const Mp3BaseTagData& ) = delete;
  Mp3BaseTagData& operator=( const Mp3BaseTagData& ) = delete;
  Mp3BaseTagData( Mp3BaseTagData&& ) = delete;
  Mp3BaseTagData& operator=( Mp3BaseTagData&& ) = delete;

  // Extract string from text frame
  virtual std::string GetText( Mp3FrameType ) const = 0;

  // Extract comment at given position
  virtual size_t GetCommentCount() const = 0;
  virtual std::string GetComment( size_t index ) const = 0;

  // Set text frame string; an empty string removes the frame
  virtual void SetText( Mp3FrameType, const std::string& ) = 0;

  // Set comment frame string; an empty string removes the frame
  // A string at position GetCommentCount() adds a new comment
  virtual void SetComment( size_t index, const std::string& ) = 0;

  // Write frame data if there have been changes
  virtual bool Write() = 0;
  virtual bool IsDirty() const = 0;

  static constexpr size_t GetMaxGenre()
  {
    return kMaxGenre;
  }

  // Extract genre name from index
  static constexpr const char* GetGenre( size_t n )
  {
    assert( n < kMaxGenre );
    return kStaticGenreList[ n ];
  }

  ///////////////////////////////////////////////////////////////////////////////
  //
  // True if incoming buffer looks like a typical MP3 frame

  static bool IsValidFrame( const uint8_t* rawFrame )
  {
    if( rawFrame == nullptr )
      return false;

    // This case could happen if we're into ID3v2 padding territory
    if( *rawFrame == 0 )
      return false;

    return Mp3BaseTagData::IsValidFrameID( GetFrameID(rawFrame) );
  }

  ///////////////////////////////////////////////////////////////////////////////
  //
  // True if frameID is made out of capital letters A-Z and 0-9

  static bool IsValidFrameID( const std::string& frameID )
  {
    // Must be 4 characters, alphanumeric and uppercase
    if( frameID.size() != kFrameIDCharCount )
      return false;

    for( auto c : frameID )
    {
      // ASCII table excerpt: "0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ"
      if( c < '0' || c > 'Z' ) // outside of range
        return false;
      if( c > '9' && c < 'A' ) // inside ":;<=>?@"
        return false;
    }
    return true;
  }

  ///////////////////////////////////////////////////////////////////////////////
  //
  // True if the indicated frame represents a text frame, e.g. "Txxx"

  static bool IsTextFrame( Mp3FrameType frameType )
  {
    assert( frameType < Mp3FrameType::Max );
    return kMp3FrameID.at( frameType )[ 0 ] == 'T';
  }

  static bool IsTextFrame( const std::string& frameID )
  {
    return IsTextFrame( frameID.c_str() );
  }

  static bool IsTextFrame( const char* frameID )
  {
    assert( frameID != nullptr );
    return *frameID == 'T';
  }

  ///////////////////////////////////////////////////////////////////////////////
  //
  // Extract frameID from raw ID3v2 frame

  static std::string GetFrameID( const uint8_t* rawFrame )
  {
    assert( rawFrame != nullptr );
    const auto* frameHeader = reinterpret_cast<const ID3v2FrameHdr*>( rawFrame );
    return frameHeader->GetFrameID();
  }

  ///////////////////////////////////////////////////////////////////////////////
  //
  // Convert frame type to frameID string

  static std::string GetFrameID( Mp3FrameType frameType )
  {
    assert( frameType < Mp3FrameType::Max );
    return kMp3FrameID.at( frameType );
  }

  ///////////////////////////////////////////////////////////////////////////////
  //
  // Convert frameID string to frame type

  static Mp3FrameType GetFrameType( const std::string& frameID )
  {
    assert( frameID.size() == kFrameIDCharCount );
    return GetFrameType( frameID.c_str() );
  }

  static Mp3FrameType GetFrameType( const char* frameID )
  {
    // Note: frameID not necessarily null terminated
    assert( frameID != nullptr );
    Mp3FrameType frameType = Mp3FrameType::None; 
    for ( ++frameType; frameType != Mp3FrameType::Max; ++frameType )
    {
      if( memcmp( frameID, kMp3FrameID.at( frameType ), kFrameIDCharCount ) == 0 )
        return frameType;
    }
    return Mp3FrameType::None;
  }

  ///////////////////////////////////////////////////////////////////////////////
  //
  // Put the correct frameID at the given position

  static void SetFrameID( char* frameID, Mp3FrameType frameType )
  {
    // TODO need?
    assert( frameID != nullptr );
    memcpy( frameID, GetFrameID( frameType ).c_str(), kFrameIDCharCount );
  }

}; // class Mp3BaseTagData

} // namespace PKIsensee

///////////////////////////////////////////////////////////////////////////////
