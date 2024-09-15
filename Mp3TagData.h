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

  Mp3TagData() {}
  bool LoadTagData( const std::filesystem::path& );

  Mp3TagData( const Mp3TagData& ) = delete;
  Mp3TagData& operator=( const Mp3TagData& ) = delete;
  Mp3TagData( Mp3TagData&& ) = delete;
  Mp3TagData& operator=( Mp3TagData&& ) = delete;

  size_t GetFrameCount() const
  {
    return frames_.size();
  }

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
  bool IsDirty() const final
  {
    return isDirty_;
  }

private:

  bool IsValidFileHeader() const;
  bool ParseID3Frame( uint32_t& offset );
  void ParseID3Frames();
  bool ParseAPETag( uint32_t& offset );
  void ParseAPETags();
  static uint32_t GetFrameSize( const uint8_t* rawFrame, uint8_t version );
  static uint32_t GetFrameBytes( const uint8_t* rawFrame, uint8_t version );

  ///////////////////////////////////////////////////////////////////////////
  //
  // ID3 frame manager
  //
  // rawFrame is the frame from the MP3 file; nullptr indicates a new frame.
  // rawFrame points into the internal buffer managed by mFileHeader.
  // rawFrame is never written, only read.
  //
  // newFrame is a new or updated frame; it supercedes rawFrame when it has
  // size > 1; size == 1 (kFlaggedForDelete) means frame flagged for delete.
  //
  // Safe to cast rawFrame or newFrame.data() to ID3v2FrameHdr*

  struct ID3Frame
  {
  private:
    using RawFramePtr = const uint8_t*;
    using FrameBuf = std::vector<uint8_t>;

    RawFramePtr rawFrame = nullptr;
    FrameBuf    newFrame;

    static constexpr uint32_t kFlaggedForDelete = 1;
    static constexpr const char* kFlaggedForDeleteTag = "DEL ";
    static constexpr const char* kPrivateFrameID = "PRIV";

  public:
    ID3Frame() noexcept
    {
    }

    ID3Frame( RawFramePtr f ) noexcept
      : rawFrame( f )
    {
    }

    ID3Frame( const ID3Frame& ) = default; // may allocate; can't be noexcept
    ID3Frame& operator=( const ID3Frame& ) noexcept = delete;
    ID3Frame( ID3Frame&& ) noexcept = default;
    ID3Frame& operator=( ID3Frame&& ) noexcept = default;

    const uint8_t* GetData() const // select the most relevant data
    {
      switch( newFrame.size() )
      {
      case 0:                 return rawFrame;
      case kFlaggedForDelete: return rawFrame;
      default:                return newFrame.data();
      }
    }

    uint8_t* GetData() // can only modify newFrame
    {
      assert( newFrame.size() > 0 );
      assert( newFrame.size() != kFlaggedForDelete );
      return newFrame.data();
    }

    std::string GetFrameID() const
    {
      const char* str = nullptr;
      switch( newFrame.size() )
      {
      case 0:                 str = reinterpret_cast<const char*>( rawFrame ); break;
      case kFlaggedForDelete: str = kFlaggedForDeleteTag; break;
      default:                str = reinterpret_cast<const char*>( newFrame.data() ); break;
      }
      return std::string{ str[ 0 ], str[ 1 ], str[ 2 ], str[ 3 ] };
    }

    bool IsTextFrame() const // all ID3 text frames start w/ T
    {
      return ( *GetData() == 'T' );
    }

    bool IsFrameID( Mp3FrameType frameType ) const
    {
      return this->GetFrameID() == Mp3BaseTagData::GetFrameID( frameType );
    }

    bool IsCommentFrame() const
    {
      return IsFrameID( Mp3FrameType::Comment );
    }

    bool IsPrivateFrame() const
    {
      return this->GetFrameID() == kPrivateFrameID;
    }

    void Allocate( size_t size ) // prepare newFrame to receive data
    {
      newFrame.resize( size );
    }

    bool IsDirty() const // we should write this frame to storage
    {
      return( ( newFrame.size() > 0 ) && ( newFrame.size() != kFlaggedForDelete ) );
    }

    void FlagToDelete() // remove this frame from storage
    {
      newFrame.resize( kFlaggedForDelete );
    }

    uint32_t GetWriteBytes( uint8_t version ) const // # bytes to write
    {
      uint32_t newFrameSize = static_cast<uint32_t>( newFrame.size() );
      switch( newFrameSize )
      {
      case 0:                 return GetFrameBytes( rawFrame, version ); // orig frame
      case kFlaggedForDelete: return 0u;
      default:                return newFrameSize;
      }
    }
  }; // ID3Frame

private:

  uint64_t FindApeHeaderOffset( File& ) const;

  const ID3Frame* GetTextFrame( Mp3FrameType ) const;
  size_t GetTextFrameReferencePos( Mp3FrameType ) const;

  const ID3Frame* GetCommentFrame( size_t index ) const;
  size_t GetCommentFrameReferencePos( size_t index ) const;

  void DeleteTextFrame( Mp3FrameType );
  void DeleteCommentFrame( size_t index );

  friend std::ostream& operator<<( std::ostream&, const Mp3TagData& );

private:

  std::filesystem::path path_;
  ID3v2FileHeader       fileHeader_;
  uint32_t              audioBufferOffset_ = 0u;;
  std::vector<uint8_t>  id3FrameBuffer_; // raw buffer of all ID3 frames
  std::vector<uint8_t>  apeFrameBuffer_; // raw buffer of all APE frames
  std::vector<ID3Frame> frames_;         // list of all MP3 frames; typically <50

  using FramePos = size_t;               // index into mFrames
  std::vector<FramePos>  textFrames_;    // list of all text frames (subset of mFrames)
  std::vector<FramePos>  commentFrames_; // list of all comment frames (subset of mFrames)
  bool isDirty_ = false;

}; // end class Mp3TagData

} // end namespace PKIsensee

///////////////////////////////////////////////////////////////////////////////
