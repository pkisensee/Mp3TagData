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
//  Supports reading both ID3 and APE frames/tags from MP3 file data. Uses the
//  word "frames" to represent both types.
//
///////////////////////////////////////////////////////////////////////////////

#pragma once
#include <filesystem>
#include <vector>

#include "File.h"
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
    return id3Frames_.size() + apeTags_.size();
  }

  // Extract string from text frame/tag
  std::string GetText( Mp3FrameType ) const final;

  // Set text frame/tag string; an empty string removes the frame/tag
  void SetText( Mp3FrameType, std::string_view ) final;

  // Extract comment at given position
  size_t GetCommentCount() const final;
  std::string GetComment( size_t index=0 ) const final;

  // Set comment; an empty string removes the comment
  // A string at position GetCommentCount() adds a new comment
  void SetComment( size_t index, std::string_view ) final;

  // Location in file where to start looking for MPEG audio data
  uint32_t GetAudioBufferOffset() const;

  // Write frame/tag data if there have been changes
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
  static uint32_t GetID3FrameSize( const uint8_t* rawFrame, uint8_t version );
  static uint32_t GetID3FrameBytes( const uint8_t* rawFrame, uint8_t version );

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

  class ID3Frame
  {
  private:
    using RawFramePtr = const uint8_t*;
    using FrameBuf = std::vector<uint8_t>;

    RawFramePtr rawFrame_ = nullptr;
    FrameBuf    newFrame_;

    static constexpr uint32_t    kFlaggedForDelete = 1;
    static constexpr const char* kFlaggedForDeleteTag = "DEL ";
    static constexpr const char* kPrivateID3FrameID = "PRIV";

  public:
    ID3Frame() noexcept
    {
    }

    explicit ID3Frame( RawFramePtr f ) noexcept
      : rawFrame_( f )
    {
    }

    ID3Frame( const ID3Frame& ) = default; // may allocate; can't be noexcept
    ID3Frame& operator=( const ID3Frame& ) noexcept = delete;
    ID3Frame( ID3Frame&& ) noexcept = default;
    ID3Frame& operator=( ID3Frame&& ) noexcept = default;

    const uint8_t* GetData() const // select the most relevant data
    {
      switch( newFrame_.size() )
      {
      case 0:                 return rawFrame_;
      case kFlaggedForDelete: return rawFrame_;
      default:                return newFrame_.data();
      }
    }

    uint8_t* GetData() // can only modify newFrame
    {
      assert( newFrame_.size() > 0 );
      assert( newFrame_.size() != kFlaggedForDelete );
      return newFrame_.data();
    }

    std::string_view GetID3FrameID() const
    {
      const char* str = nullptr;
      switch( newFrame_.size() )
      {
      case 0:                 str = reinterpret_cast<const char*>( rawFrame_ ); break;
      case kFlaggedForDelete: str = kFlaggedForDeleteTag; break;
      default:                str = reinterpret_cast<const char*>( newFrame_.data() ); break;
      }
      return std::string_view{ str, kFrameIDCharCount };
    }

    bool IsTextFrame() const // all ID3 text frames start w/ T
    {
      return ( *GetData() == 'T' ); // TODO used?
    }

    bool IsFrameID( Mp3FrameType frameType ) const
    {
      return this->GetID3FrameID() == Mp3BaseTagData::GetID3FrameID( frameType );
    }

    bool IsCommentFrame() const
    {
      return IsFrameID( Mp3FrameType::ID3Comment );
    }

    bool IsPrivateFrame() const
    {
      return this->GetID3FrameID() == kPrivateID3FrameID;
    }

    void Allocate( size_t size ) // prepare newFrame to receive data
    {
      newFrame_.resize( size );
    }

    bool IsDirty() const // we should write this frame to storage
    {
      return( ( newFrame_.size() > 0 ) && ( newFrame_.size() != kFlaggedForDelete ) );
    }

    void FlagToDelete() // remove this frame from storage
    {
      newFrame_.resize( kFlaggedForDelete );
    }

    uint32_t GetWriteBytes( uint8_t version ) const // # bytes to write
    {
      uint32_t newFrameSize = static_cast<uint32_t>( newFrame_.size() );
      switch( newFrameSize )
      {
      case 0:                 return GetID3FrameBytes( rawFrame_, version ); // orig frame
      case kFlaggedForDelete: return 0u;
      default:                return newFrameSize;
      }
    }
  }; // ID3Frame

  ///////////////////////////////////////////////////////////////////////////
  //
  // APE tag manager
  // 
  // APE tags currently treated as read-only.
  // Safe to cast GetData() to const APEv2TagItem*

  class APETag
  {
  private:
    using RawTagPtr = const uint8_t*;
    RawTagPtr rawTag_ = nullptr;

  public:
    APETag() noexcept
    {
    }

    explicit APETag(RawTagPtr t) noexcept
      : rawTag_(t)
    {
    }

    APETag(const APETag&) noexcept = default;
    APETag& operator=(const APETag&) noexcept = delete;
    APETag(APETag&&) noexcept = default;
    APETag& operator=(APETag&&) noexcept = default;

    const uint8_t* GetData() const
    {
      // safe to cast to APEv2TagItem
      return rawTag_;
    }

    std::string_view GetAPETagID() const
    {
      if( GetData() == nullptr )
        return {};
      const auto* apeTag = reinterpret_cast<const APEv2TagItem*>( GetData() );
      return apeTag->GetKey();
    }

    bool IsTagID( Mp3FrameType tagType ) const
    {
      return this->GetAPETagID() == Mp3BaseTagData::GetAPETagID( tagType );
    }
  }; // APETag

private:

  uint64_t FindApeHeaderOffset( File& ) const;

  const ID3Frame* GetID3TextFrame( Mp3FrameType ) const;
  const Mp3TagData::ID3Frame* FindID3TextFrame( Mp3FrameType ) const;
  Mp3TagData::ID3Frame* FindID3TextFrame( Mp3FrameType );

  const ID3Frame* GetID3CommentFrame( size_t index ) const;
  const Mp3TagData::ID3Frame* FindID3CommentFrame( size_t index ) const;
  Mp3TagData::ID3Frame* FindID3CommentFrame( size_t index );

  const APETag* GetAPETextTag( Mp3FrameType ) const;
  const Mp3TagData::APETag* FindAPETextTag( Mp3FrameType ) const;
  Mp3TagData::APETag* FindAPETextTag( Mp3FrameType );

  void DeleteTextFrame( Mp3FrameType );
  void DeleteCommentFrame( size_t index );

  friend std::ostream& operator<<( std::ostream&, const Mp3TagData& );

private:

  std::filesystem::path path_;
  ID3v2FileHeader       fileHeader_;
  uint32_t              audioBufferOffset_ = 0u;
  std::vector<uint8_t>  id3FrameBuffer_; // raw buffer containing all ID3 frames
  std::vector<uint8_t>  apeFrameBuffer_; // raw buffer containing all APE frames
  std::vector<ID3Frame> id3Frames_;      // list of all ID3 frames; typically <50
  std::vector<APETag>   apeTags_;        // list of all APE tags; typically <20
  bool isDirty_ = false;

}; // Mp3TagData

} // end namespace PKIsensee

///////////////////////////////////////////////////////////////////////////////
