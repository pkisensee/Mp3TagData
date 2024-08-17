///////////////////////////////////////////////////////////////////////////////
//
//  Mp3TagData.h
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
#include <filesystem>
#include <vector>

#include "Mp3BaseTagData.h"

namespace PKIsensee
{

class Mp3TagData : public Mp3BaseTagData
{
public:

  explicit Mp3TagData( const std::filesystem::path& );

  Mp3TagData() = delete;
  Mp3TagData( const Mp3TagData& ) = delete;
  Mp3TagData& operator=( const Mp3TagData& ) = delete;
  Mp3TagData( Mp3TagData&& ) = delete;
  Mp3TagData& operator=( Mp3TagData&& ) = delete;

  // Extract string from text frame
  std::string GetText( Mp3FrameType ) const final;

  // Extract comment at given position
  size_t GetCommentCount() const final;
  std::string GetComment( size_t index=0 ) const final;

  // Set text frame string; an empty string removes the frame
  void SetText( Mp3FrameType, const std::string& ) final;

  // Set comment frame string; an empty string removes the frame
  // A string at position GetCommentCount() adds a new comment
  void SetComment( size_t index, const std::string& ) final;

  // Location in file where to start looking for MPEG audio data
  uint32_t GetAudioBufferOffset() const;

  // Write frame data if there have been changes
  bool Write() final;
  bool IsDirty() const final {
    return isDirty;
  }

private:

  bool IsValidFileHeader() const;
  void ReadTagData();
  bool ParseFrame( uint32_t& offset );
  void ParseFrames();
  uint32_t WriteFrameSize( uint32_t frameSize ) const;
  static uint32_t GetFrameSize( const uint8_t* rawFrame, uint8_t version );
  static size_t GetFrameBytes( const uint8_t* rawFrame, uint8_t version );
  uint32_t GetTextSize( const void* textStart, const void* rawFrame ) const;

  ///////////////////////////////////////////////////////////////////////////
  //
  // ID3 frame manager
  //
  // rawFrame is the frame from the MP3 file; nullptr indicates a new frame.
  // rawFrame points into the internal buffer managed by mFileHeader.
  // rawFrame is never written, only read.
  //
  // newFrame is a new or updated frame; it supercedes rawFrame when it has
  // size > 1; size == 1 means frame flagged for delete. TODO better way?
  //
  // Safe to cast rawFrame or newFrame.data() to ID3v2FrameHdr*

  struct Frame
  {
  private:
    using RawFramePtr = const uint8_t*;
    using FrameVec = std::vector<uint8_t>; // TODO new name?

    RawFramePtr rawFrame = nullptr;
    FrameVec    newFrame;

  public:
    Frame() noexcept
      : rawFrame( nullptr ), newFrame()
    {
    }

    Frame( RawFramePtr f ) noexcept
      : rawFrame( f ), newFrame()
    {
    }

    Frame( const Frame& ) = default; // may allocate; can't be noexcept
    Frame& operator=( const Frame& ) noexcept = delete;
    Frame( Frame&& ) noexcept = default;
    Frame& operator=( Frame&& ) noexcept = default;

    const uint8_t* GetData() const // select the most relevant data
    {
      return ( newFrame.size() <= 1 ) ? rawFrame : newFrame.data();
    }

    uint8_t* GetData() // can only modify newFrame
    {
      assert( newFrame.size() > 1 );
      return newFrame.data();
    }

    bool IsTextFrame() const // all ID3 text frames start w/ T
    {
      return ( *GetData() == 'T' );
    }

    bool IsFrameID( Mp3FrameType frameType ) const
    {
      return ( memcmp( GetData(), GetFrameID(frameType).c_str(), kFrameIDCharCount ) == 0 );
    }

    bool IsCommentFrame() const
    {
      return IsFrameID( Mp3FrameType::Comment );
    }

    void Allocate( size_t size ) // prepare newFrame to receive data
    {
      newFrame.resize( size );
    }

    bool IsDirty() const // we should write this frame to storage
    {
      return ( newFrame.size() > 1 );
    }

    void FlagToDelete() // remove this frame from storage
    {
      newFrame.resize( 1 );
    }

    size_t GetWriteBytes( uint8_t version ) const // # bytes to write
    {
      if( newFrame.size() == 1 )
        return 0u; // deleted frame
      if( newFrame.size() > 1 )
        return newFrame.size(); // new frame
      return GetFrameBytes( rawFrame, version ); // orig frame
    }
  };

  const Frame* GetTextFrame( Mp3FrameType ) const;
  size_t GetTextFrameReferencePos( Mp3FrameType ) const;

  const Frame* GetCommentFrame( size_t index ) const;
  size_t GetCommentFrameReferencePos( size_t index ) const;

  void DeleteTextFrame( Mp3FrameType );
  void DeleteCommentFrame( size_t index );

private:

#pragma pack(push,1)
  struct ID3v2FileHeader // MP3 File header structure
  {
    // https://mutagen-specs.readthedocs.io/en/latest/id3/id3v2.4.0-structure.html#id3v2-header
    char     id3[ 3 ];      // 'ID3'
    uint8_t  majorVersion;  // e.g. 2; never 0xFF
    uint8_t  minorVersion;  // e.g. 3; never 0xFF
    uint8_t  flags;         // values in Mp3TagData.cpp
    uint32_t synchSafeSize; // see id3 6.2, https://en.wikipedia.org/wiki/Synchsafe
  };
#pragma pack(pop)

  std::filesystem::path mPath;
  ID3v2FileHeader       mFileHeader;
  uint32_t              mAudioBufferOffset = 0u;;
  std::vector<uint8_t>  mFrameBuffer; // raw buffer of all MP3 frames
  std::vector<Frame>    mFrames;      // list of all MP3 frames

  using FramePos = size_t;               // index into mFrames
  std::vector<FramePos>  mTextFrames;    // list of all text frames (subset of mFrames)
  std::vector<FramePos>  mCommentFrames; // list of all comment frames (subset of mFrames)
  bool isDirty = false;

}; // end class Mp3TagData

} // end namespace PKIsensee

///////////////////////////////////////////////////////////////////////////////
