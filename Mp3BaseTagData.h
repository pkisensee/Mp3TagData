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
  ID3First = First,

  // Text frames
  ID3Title = First,  // TIT2
  ID3Subtitle,       // TIT3
  ID3Genre,          // TCON
  ID3Artist,         // TPE1
  ID3Album,          // TALB
  ID3Composer,       // TCOM
  ID3Orchestra,      // TPE2
  ID3OrigArtist,     // TOPE
  ID3Year,           // TYER
  ID3OrigYear,       // TORY
  ID3TrackNum,       // TRCK
  ID3BeatsPerMinute, // TBPM
  ID3Duration,       // TLEN
  ID3Key,            // TKEY
  ID3Conductor,      // TPE3
  ID3Language,       // TLAN
  ID3Mood,           // TMOO
  // Add new ID3 text frame entries here and to match kMp3FrameID below

  // Non-text frames
  ID3TextMax,
  ID3Comment = ID3TextMax, // COMM
  // Add new non-text frame entries here and to match kMp3FrameID below

  // APE frames
  ID3Max,
  APEFirst,
  APETrackGain = APEFirst, // REPLAYGAIN_TRACK_GAIN
  APETrackPeak,            // REPLAYGAIN_TRACK_PEAK
  // Add new APE frame entries here and to match kMp3FrameID below

  APEMax,
  Max = APEMax
};

constexpr size_t kMaxFrameTypes = static_cast<size_t>( Mp3FrameType::Max );
constexpr frozen::unordered_map< Mp3FrameType, const char*, kMaxFrameTypes >
kMp3FrameID =
{
  // https://mutagen-specs.readthedocs.io/en/latest/id3/id3v2.3.0.html#4.2.1
  // This is not an inclusive list, only the most useful frame info
  { Mp3FrameType::None,              ""     },
  { Mp3FrameType::ID3Title,          "TIT2" },
  { Mp3FrameType::ID3Subtitle,       "TIT3" }, // Rare, e.g. "Op. 6"
  { Mp3FrameType::ID3Genre,          "TCON" }, // May be numeric; may have parens, e.g. "(21)"
  { Mp3FrameType::ID3Artist,         "TPE1" }, // May have forward slashes as separators
  { Mp3FrameType::ID3Album,          "TALB" },
  { Mp3FrameType::ID3Composer,       "TCOM" }, // May have forward slashes as separators
  { Mp3FrameType::ID3Orchestra,      "TPE2" }, // Often called "Album Artist"
  { Mp3FrameType::ID3OrigArtist,     "TOPE" }, // Rare
  { Mp3FrameType::ID3Year,           "TYER" }, // YYYY
  { Mp3FrameType::ID3OrigYear,       "TORY" }, // Rare; YYYY
  { Mp3FrameType::ID3TrackNum,       "TRCK" }, // e.g. "5" or "5/12"
  { Mp3FrameType::ID3BeatsPerMinute, "TBPM" }, // e.g. "100"
  { Mp3FrameType::ID3Duration,       "TLEN" }, // Milliseconds; often incorrect for VBR
  { Mp3FrameType::ID3Key,            "TKEY" }, // e.g. "C#m"
  { Mp3FrameType::ID3Conductor,      "TPE3" },
  { Mp3FrameType::ID3Language,       "TLAN" }, // Rare; ISO-639-2 3-char codes
  { Mp3FrameType::ID3Mood,           "TMOO" }, // v2.4; rare
  { Mp3FrameType::ID3Comment,        "COMM" }, // Multiple allowed
  { Mp3FrameType::ID3Max,            "" },
  { Mp3FrameType::APETrackGain,      "REPLAYGAIN_TRACK_GAIN" }, // dB
  { Mp3FrameType::APETrackPeak,      "REPLAYGAIN_TRACK_PEAK" }, // dB
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

  // Set text frame string; an empty string removes the frame
  virtual void SetText( Mp3FrameType, std::string_view ) = 0;

  // Extract comment at given position
  virtual size_t GetCommentCount() const = 0;
  virtual std::string GetComment( size_t index ) const = 0;

  // Set comment frame string; an empty string removes the frame
  // A string at position GetCommentCount() adds a new comment
  virtual void SetComment( size_t index, std::string_view ) = 0;

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
  // True if incoming buffer looks like a typical ID3 frame

  static bool IsValidID3Frame( const uint8_t* rawFrame )
  {
    if( rawFrame == nullptr )
      return false;

    // This case could happen if we're into ID3v2 padding territory
    if( *rawFrame == 0 )
      return false;

    return Mp3BaseTagData::IsValidID3FrameID( GetID3FrameID(rawFrame) );
  }

  ///////////////////////////////////////////////////////////////////////////////
  //
  // ID3 FrameID must be capital letters A-Z or 0-9

  static bool IsValidID3FrameID( std::string_view frameID )
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
  // True if the indicated frame represents an ID3 text frame, e.g. "Txxx"

  static bool IsID3TextFrame( Mp3FrameType frameType )
  {
    return ( frameType >= Mp3FrameType::ID3First ) &&
           ( frameType <  Mp3FrameType::ID3TextMax );
  }

  ///////////////////////////////////////////////////////////////////////////////
  //
  // True if the indicated frame represents an APE text frame

  static bool IsAPETextFrame( Mp3FrameType frameType )
  {
    return ( frameType >= Mp3FrameType::APEFirst ) &&
           ( frameType <  Mp3FrameType::APEMax );
  }

  ///////////////////////////////////////////////////////////////////////////////
  //
  // Extract frameID from raw ID3v2 frame

  static std::string_view GetID3FrameID( const uint8_t* rawFrame )
  {
    assert( rawFrame != nullptr );
    const auto* frameHeader = reinterpret_cast<const ID3v2FrameHdr*>( rawFrame );
    return frameHeader->GetFrameID();
  }

  ///////////////////////////////////////////////////////////////////////////////
  //
  // Convert frame type to frameID string

  static std::string GetID3FrameID( Mp3FrameType frameType )
  {
    assert( frameType < Mp3FrameType::Max );
    return kMp3FrameID.at( frameType );
  }
  
}; // class Mp3BaseTagData

} // namespace PKIsensee

///////////////////////////////////////////////////////////////////////////////
