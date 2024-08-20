///////////////////////////////////////////////////////////////////////////////
//
//  Mp3TagData.cpp
//
//  Copyright � Pete Isensee (PKIsensee@msn.com).
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

#include <algorithm>
#include <future>
#include <limits>

#include "File.h"
#include "Log.h"
#include "Mp3TagData.h"
#include "Util.h"

using namespace PKIsensee;

#ifdef _DEBUG
#define verify(e) assert(e)
#else
#define verify(e) static_cast<void>(e)
#endif

namespace // anonymous
{

constexpr const char* kID3String = "ID3";
constexpr const char* kEnglishLanguage = "eng";

[[maybe_unused]] constexpr uint8_t kFlagUnsynchronized = ( 1 << 7 );
[[maybe_unused]] constexpr uint8_t kFlagExtended       = ( 1 << 6 );
[[maybe_unused]] constexpr uint8_t kFlagExperimental   = ( 1 << 5 );
[[maybe_unused]] constexpr uint8_t kFlagFooterPresent  = ( 1 << 4 );
[[maybe_unused]] constexpr uint8_t kFlagsRemaining = ( 0b00001111 ); // must be cleared

constexpr uint8_t kTextEncodingAnsi    = 0;
constexpr uint8_t kTextEncodingUTF16   = 1;
constexpr uint8_t kTextEncodingUTF16BE = 2;
constexpr uint8_t kTextEncodingUTF8    = 3;

constexpr size_t kInvalidFramePos = size_t( -1 );
constexpr size_t kPaddingBytes = 2048u; // commonly used in MP3 tagging software

#pragma pack(push,1)
///////////////////////////////////////////////////////////////////////////////
//
// ID3 frame structures

// Strings can be encoded in different fashions; this struct disambiguates
// https://mutagen-specs.readthedocs.io/en/latest/id3/id3v2.2.html#id3v2-frames-overview

struct ID3v2String
{
  union {
    // textEncoding == kTextEncodingAnsi OR kTextEncodingUTF8
    char utf8[ 1 ];        // string start

    // textEncoding == kTextEncodingUTF16 OR kTextEncodingUTF16BE
    struct Unicode
    {
      uint16_t bom;        // byte order mark
      wchar_t  utf16[ 1 ]; // string start
    } unicode;
  };
};

// https://mutagen-specs.readthedocs.io/en/latest/id3/id3v2.2.html#text-information-frames
struct ID3v2TextFrame
{
  ID3v2FrameHdr hdr;          // 'Txxx' header
  uint8_t       textEncoding; // see kTextEncoding IDs above
  ID3v2String   str;
};

// https://mutagen-specs.readthedocs.io/en/latest/id3/id3v2.2.html#comments
struct ID3v2CommentFrame
{
  ID3v2FrameHdr hdr;           // 'COMM' header
  uint8_t       textEncoding;  // see kTextEncoding IDs above
  char          language[ 3 ]; // e.g. "eng"
  ID3v2String   str;
};
#pragma pack(pop)

///////////////////////////////////////////////////////////////////////////////
//
// Converts a source integer from an ID3 file format big endian integer into a 
// useful native value. Automatically handles endian conversion. For synchSafe
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
// Automatically handles endian conversion. For synchSafe integers, 
// kBitsPerByte is 7.

template <uint8_t kBits>
uint32_t WriteID3Int( uint32_t nativeInt )
{
  auto result = Util::UnpackBits<kBits>( nativeInt );
  return Util::ToBigEndian( result );
}

} // end anonymous namespace

///////////////////////////////////////////////////////////////////////////////
//
// Read tags into memory

bool Mp3TagData::LoadTagData( const std::filesystem::path& path )
{
  path_ = path;
  frameBuffer_.resize( 0 );
  frames_.resize( 0 );
  textFrames_.resize( 0 );
  commentFrames_.resize( 0 );
  isDirty_ = false;

  File mp3File( path_ );
  if( !mp3File.Open( FileFlags::Read | FileFlags::SharedRead | FileFlags::SequentialScan ) )
    return false;

  // Read id3v2 header
  if( !mp3File.Read( &fileHeader_, sizeof( fileHeader_ ) ) )
  {
    PKLOG_WARN( "Failed to read MP3 file header %S; ERR: %d\n", path_.c_str(), Util::GetLastError() );
    return false;
  };

  if( !IsValidFileHeader() )
    return false;

  auto frameSectionSize = ReadID3Int<7>( fileHeader_.synchSafeSize );
  assert( frameSectionSize < ( 1024 * 1024 ) ); // ensure reasonable
  audioBufferOffset_ = sizeof( fileHeader_ ) + frameSectionSize;

  // Read all frames into memory
  frameBuffer_.resize( frameSectionSize );
  size_t bytesRead;
  if( !mp3File.Read( frameBuffer_.data(), frameSectionSize, bytesRead ) )
  {
    PKLOG_WARN( "Failed to read MP3 frames from %S; ERR: %d\n", path_.c_str(), Util::GetLastError() );
    return false;
  }

  // Close the file asynchronously while we parse the frames from memory)
  std::future fileClose = std::async( std::launch::async, [&] { mp3File.Close(); } );
  if( bytesRead < frameSectionSize )
    frameBuffer_.resize( bytesRead );

  // Parse frames
  ParseFrames();
  fileClose.wait();
  return true;
};

///////////////////////////////////////////////////////////////////////////////
//
// Extract the MP3 tag string for the given text frame type

std::string Mp3TagData::GetText( Mp3FrameType frameType ) const
{
  assert( IsTextFrame( frameType ) );
  const Frame* pFrame = GetTextFrame(frameType);
  if( pFrame == nullptr )
    return std::string();

  const auto* rawFrame = pFrame->GetData();
  const auto* textFrame = reinterpret_cast<const ID3v2TextFrame*>( rawFrame );
  assert( textFrame->hdr.frameID[ 0 ] == 'T' );

  std::string value;
  switch( textFrame->textEncoding )
  {
  case kTextEncodingUTF8:
    // not officially handled; only part of 2.4 spec
    [[fallthrough]];

  case kTextEncodingAnsi:
  {
    auto textData = textFrame->str.utf8;
    auto charCount = GetTextBytes( textData, rawFrame );
    value.assign( textData, charCount );
    break;
  }
  case kTextEncodingUTF16:
  {
    auto textData = textFrame->str.unicode.utf16;
    auto charCount = GetTextBytes( textData, rawFrame ) / sizeof( wchar_t );
    std::wstring unicode( textData, charCount );
    value = StringUtil::GetUtf8( unicode );
    break;
  }
  case kTextEncodingUTF16BE:
  default:
    assert( false ); // not encountered to date, so left unwritten
    return std::string();
  }

  // In some buggy frames, trailing null bytes may be included in the frame,
  // so strip them out
  StrUtil::ToTrimmedTrailing( value, std::string( { '\0' } ));
  return value;
}

///////////////////////////////////////////////////////////////////////////////
//
// Number of comments in the MP3 file

size_t Mp3TagData::GetCommentCount() const
{
  return commentFrames_.size();
}

///////////////////////////////////////////////////////////////////////////////
//
// MP3 files can have multiple comments; returns the comment at the given position
// See https://mutagen-specs.readthedocs.io/en/latest/id3/id3v2.2.html#comments

std::string Mp3TagData::GetComment(size_t i) const
{
  assert( i < commentFrames_.size() );
  if( i >= commentFrames_.size() )
    return std::string();

  const auto* rawFrame = GetCommentFrame( i )->GetData();
  const auto* commentFrame = reinterpret_cast<const ID3v2CommentFrame*>( rawFrame );
  assert( commentFrame->hdr.frameID[0] == 'C' );

  std::string value;
  switch( commentFrame->textEncoding )
  {
  case kTextEncodingUTF8:
    // not officially handled; only part of 2.4
    [[fallthrough]];

  case kTextEncodingAnsi:
  {
    auto textData = commentFrame->str.utf8;

    // Skip comment description
    while( *textData++ )
      ;

    // Extract comment
    auto charCount = GetTextBytes( textData, rawFrame );
    value.assign( textData, charCount );
    break;
  }
  case kTextEncodingUTF16:
  {
    auto textData = commentFrame->str.unicode.utf16;

    // Skip comment description
    while( *textData++ )
      ;

    // The primary comment text starts immediately after the byte order mark (BOM)
    textData++;

    auto charCount = GetTextBytes( textData, rawFrame ) / sizeof( wchar_t );
    std::wstring unicode( textData, charCount );
    value = StringUtil::GetUtf8( unicode );
    break;
  }
  case kTextEncodingUTF16BE:
  default:
    assert( false ); // not encountered to date, so left unwritten
    break;
  }

  // In some buggy frames, trailing null bytes may be included in the frame size,
  // so strip them out
  StrUtil::ToTrimmedTrailing( value, std::string( { '\0' } ) );
  return value;
}

///////////////////////////////////////////////////////////////////////////////
//
// Update existing text frame, create new frame if one doesn't exist, or
// delete frame if newStr is empty

void Mp3TagData::SetText( Mp3FrameType frameType, const std::string& newStr )
{
  assert( IsTextFrame( frameType ) );
  if( newStr.empty() )
  {
    DeleteTextFrame( frameType );
    return;
  }

  size_t framePos = GetTextFrameReferencePos( frameType );
  if (framePos == kInvalidFramePos )
  {
    // Frame type isn't in MP3 file; create new frame and add to right lists 
    frames_.emplace_back( Frame() );
    framePos = frames_.size() - 1;
    textFrames_.emplace_back( framePos );
  }
  Mp3TagData::Frame* pFrame = &( frames_[ framePos ] );

  // Always create UTF8 text frames to avoid complications dealing with textEncoding byte,
  // UTF16 BOM and so forth
  auto sizeAlloc = sizeof( ID3v2FrameHdr ) + sizeof( ID3v2TextFrame::textEncoding ) + newStr.size();
  pFrame->Allocate( sizeAlloc );
  ID3v2TextFrame* pTextFrame = reinterpret_cast<ID3v2TextFrame*>( pFrame->GetData() );

  // Set the header fields
  auto frameSize = sizeAlloc - sizeof( ID3v2FrameHdr );
  SetFrameID( pTextFrame->hdr.frameID, frameType );
  pTextFrame->hdr.size = WriteFrameSize( static_cast<uint32_t>(frameSize) );
  pTextFrame->hdr.statusMessages = 0;
  pTextFrame->hdr.formatDescription = 0;

  // Set the text fields; null byte is not written
  pTextFrame->textEncoding = kTextEncodingAnsi;
  memcpy( pTextFrame->str.utf8, newStr.c_str(), newStr.size() );
  isDirty_ = true;
}

///////////////////////////////////////////////////////////////////////////////
//
// Set the comment at the given position; use position GetCommentCount() 
// to add a new comment; deletes comment frame if newComment is empty

void Mp3TagData::SetComment( size_t i, const std::string& newComment )
{
  if( newComment.empty() )
  {
    DeleteCommentFrame( i );
    return;
  }

  assert( i <= commentFrames_.size() );
  if( i == commentFrames_.size() )
  {
    // Comment at index i isn't in file yet; create new frame and add to right lists 
    frames_.emplace_back( Frame() );
    commentFrames_.emplace_back( frames_.size() - 1 );
  }

  FramePos framePos = commentFrames_[ i ];
  Mp3TagData::Frame* pFrame = &( frames_[framePos] );

  // Create UTF8 comment frames for simplicity
  auto sizeAlloc = sizeof( ID3v2FrameHdr ) + sizeof( ID3v2CommentFrame::textEncoding ) +
                   sizeof( ID3v2CommentFrame::language ) + 
                   sizeof( '\0' ) + // empty comment description
                   newComment.size();
        
  pFrame->Allocate( sizeAlloc );
  ID3v2CommentFrame* pCommentFrame = reinterpret_cast<ID3v2CommentFrame*>( pFrame->GetData() );

  // Set the header fields
  auto frameSize = sizeAlloc - sizeof( ID3v2FrameHdr );
  SetFrameID( pCommentFrame->hdr.frameID, Mp3FrameType::Comment );
  pCommentFrame->hdr.size = WriteFrameSize( static_cast<uint32_t>(frameSize) );
  pCommentFrame->hdr.statusMessages = 0;
  pCommentFrame->hdr.formatDescription = 0;

  // Set the text fields; comment  description is empty
  pCommentFrame->textEncoding = kTextEncodingAnsi;
  memcpy( pCommentFrame->language, kEnglishLanguage, 3 );
  *pCommentFrame->str.utf8 = '\0'; // empty comment description
  memcpy( pCommentFrame->str.utf8 + sizeof('\0'), newComment.c_str(), newComment.size() );
  isDirty_ = true;
}

///////////////////////////////////////////////////////////////////////////////
//
// Location in file where to start looking for MPEG audio data

uint32_t Mp3TagData::GetAudioBufferOffset() const
{
  return audioBufferOffset_;
}

///////////////////////////////////////////////////////////////////////////////
//
// Write modified or deleted frames to the given file, making sure that audio
// data remains intact

bool Mp3TagData::Write()
{
  if( !IsDirty() )
    return false;

  // same as std::accumulate
  size_t frameSectionSize = 
    std::ranges::fold_left( frames_, size_t{}, [ fh = fileHeader_ ]( size_t sum, const Frame& frame )
    {
      return sum + frame.GetWriteBytes( fh.majorVersion );
    } );

  File mp3File( path_ );
  if( !mp3File.Open( FileFlags::Read | FileFlags::Write | FileFlags::SharedRead | FileFlags::SharedWrite ) )
  {
    PKLOG_WARN( "Failed to write MP3 data to %S; ERR: %d\n", path_.c_str(), Util::GetLastError() );

    // Try one more time; useful in debugging scenarios
    if( !mp3File.Open( FileFlags::Read | FileFlags::Write | FileFlags::SharedRead | FileFlags::SharedWrite ) )
      return false;
  }

  // Padding bytes depends on whether new frames will fit within existing space
  size_t padBytes = ( frameSectionSize > frameBuffer_.size() ) ? 
                      kPaddingBytes : ( frameBuffer_.size() - frameSectionSize );

  // Write new id3v2 header size
  fileHeader_.synchSafeSize = WriteID3Int<7>( static_cast<uint32_t>(frameSectionSize + padBytes) );
  if( !mp3File.Write( &fileHeader_, sizeof( fileHeader_ ) ) )
    return false;

  // Read existing audio data if we're going to overwrite it
  std::vector<uint8_t> audioData;
  if( frameSectionSize > frameBuffer_.size() )
  {
    assert( mp3File.GetLength() <= std::numeric_limits<uint32_t>::max() );
    size_t audioDataSize = size_t(mp3File.GetLength()) - frameBuffer_.size() - sizeof( fileHeader_ );
    audioData.resize( audioDataSize );
    uint64_t pos = static_cast<uint64_t>( sizeof( fileHeader_ ) ) + frameBuffer_.size();
    if( mp3File.SetPos( pos ) )
    {
      mp3File.Read( audioData.data(), audioDataSize );
      mp3File.SetPos( sizeof( fileHeader_ ) );
    }
  }

  // Write all frames except deleted ones
  for( const auto& frame : frames_ )
  {
    if( frame.GetWriteBytes( fileHeader_.majorVersion ) )
      verify( mp3File.Write( frame.GetData(), frame.GetWriteBytes( fileHeader_.majorVersion ) ) );
  }

  // Pad with zeros
  if( padBytes )
  {
    // It's possible to have 2K stack buffer rather than a heap allocation, but this is simpler
    // and dominated by the file write time anyway
    std::vector<uint8_t> zeros( padBytes, 0 );
    verify( mp3File.Write( zeros.data(), zeros.size() ) );
  }

  // Append audio data if it was overwritten
  if( !audioData.empty() )
    verify( mp3File.Write( audioData.data(), audioData.size() ) );

  // Update all fields with correct new data
  mp3File.Close();
  return LoadTagData( path_ );
}

///////////////////////////////////////////////////////////////////////////////
//
// Determine if header looks reasonable

bool Mp3TagData::IsValidFileHeader() const
{
  // Validate header
  std::string id3 = { fileHeader_.id3[ 0 ], 
                      fileHeader_.id3[ 1 ], 
                      fileHeader_.id3[ 2 ] };
  if( id3 != kID3String )
  {
    PKLOG_WARN( "\nInvalid MP3 ID3v2 file %S; bad header\n", path_.c_str() );
    return false;
  }
  if( fileHeader_.majorVersion < 3 || 
      fileHeader_.majorVersion == 0xFF ||
      fileHeader_.minorVersion == 0xFF )
  {
    PKLOG_WARN( "\nSong %S has obsolete v2 or v1 header; resave\n", path_.c_str() );
    return false;
  }

  // Validate flags
  if( ( fileHeader_.flags & kFlagExtended ) ||
      ( fileHeader_.flags & kFlagExperimental ) ||
      ( fileHeader_.flags & kFlagsRemaining ) )
  {
    PKLOG_WARN( "\nSong %S has invalid header flags; resave\n", path_.c_str() );
    return false;
  }

  return true;
}

///////////////////////////////////////////////////////////////////////////////
//
// True if frame found and processed; false when there are no more frames left

bool Mp3TagData::ParseFrame( uint32_t& offset )
{
  // If we've reached end of the tag section, we're done
  if( offset >= frameBuffer_.size() )
    return false;

  const auto* rawFrame = frameBuffer_.data() + offset;

  // If we've hit a null byte or header is whacked, 
  // we're into padding territory and there are no more tags
  if( !Mp3BaseTagData::IsValidFrame( rawFrame ) )
    return false;

  Frame frame( rawFrame );
  frames_.emplace_back( frame );

  offset += static_cast<uint32_t>(GetFrameBytes( rawFrame, fileHeader_.majorVersion ));
  return true;
}

///////////////////////////////////////////////////////////////////////////////
//
// Process all the frames 

void Mp3TagData::ParseFrames()
{
  // Build frame list
  auto offset = 0u;
  auto framesRemain = true;
  while( framesRemain )
    framesRemain = ParseFrame( offset );

  // Create sublists for common frame types
  for( size_t i = 0u; i < frames_.size(); ++i )
  {
    if( frames_[i].IsTextFrame() )
      textFrames_.emplace_back( i );
    else if( frames_[i].IsCommentFrame() )
      commentFrames_.emplace_back( i );
  }

  // Check for duplicate text frames, which should never exist
  for( auto frameType = Mp3FrameType::First; frameType != Mp3FrameType::Comment; ++frameType )
  {
    [[maybe_unused]] size_t count = 0;
    for( auto i : textFrames_ )
      if( frames_[ i ].IsFrameID( frameType ) )
        ++count;
    if( count > 1 )
      PKLOG_WARN( "\nDuplicate frame %s in %S\n", GetFrameID(frameType).c_str(), path_.c_str());
  }
}

///////////////////////////////////////////////////////////////////////////////
//
// Converts the frameSize value to the proper ID3 file format. Version 3
// is just a big endian value. Other versions are synchSafe.

uint32_t Mp3TagData::WriteFrameSize( uint32_t frameSize ) const
{
  return (fileHeader_.majorVersion == 3) ? WriteID3Int<8>( frameSize ) : 
                                           WriteID3Int<7>( frameSize );
}

///////////////////////////////////////////////////////////////////////////////
//
// Extracts the frame size from the given frame

uint32_t Mp3TagData::GetFrameSize( const uint8_t* rawFrame, uint8_t version ) // static
{
  assert( rawFrame != nullptr );
  const auto* frameHeader = reinterpret_cast<const ID3v2FrameHdr*>( rawFrame );

  // Version 3 frame sizes are stored in big endian
  // Other version frame sizes are stored in synchSafe format
  auto frameSize = ( version == 3 ) ? ReadID3Int<8>( frameHeader->size ) :
                                      ReadID3Int<7>( frameHeader->size );
  return frameSize;
}

///////////////////////////////////////////////////////////////////////////////
//
// Extract the number of bytes represented by this frame 

size_t Mp3TagData::GetFrameBytes( const uint8_t* rawFrame, uint8_t version ) // static
{
  assert( rawFrame != nullptr );
  return sizeof( ID3v2FrameHdr ) + GetFrameSize( rawFrame, version );
}

///////////////////////////////////////////////////////////////////////////////
//
// Given raw frame pointer and start of text, return text size in bytes

uint32_t Mp3TagData::GetTextBytes( const void* textStart, const uint8_t* rawFrame ) const
{
  //  rawFrame                   textStart (char* or wchar_t*)
  //  |                          |
  //  v                          v
  // |<------------------------>|<-------------->|
  // |                                           |
  // |<--ID3v2FrameHdr-->|<-----frameSize------->|
  // |                                           |
  // |<----------offset-------->|<---textSize--->|

  assert( rawFrame < textStart );
  ptrdiff_t offset = reinterpret_cast<const uint8_t*>( textStart ) - rawFrame;
  uint32_t offsetU32 = static_cast<uint32_t>( offset );

  uint32_t frameSize = GetFrameSize( rawFrame, fileHeader_.majorVersion );
  uint32_t textSize = sizeof( ID3v2FrameHdr ) + frameSize;
  assert( offsetU32 <= textSize );
  textSize -= offsetU32;
  return textSize;
}

///////////////////////////////////////////////////////////////////////////////
//
// Locate text frame
//
// There are on order of a couple dozen of frames in a typical MP3 file and rarely
// more than 100, so linear search is fine

const Mp3TagData::Frame* Mp3TagData::GetTextFrame( Mp3FrameType frameType ) const
{
  auto framePos = GetTextFrameReferencePos( frameType );
  if( framePos == kInvalidFramePos )
    return nullptr;
  return &( frames_[ framePos ] );
}

size_t Mp3TagData::GetTextFrameReferencePos( Mp3FrameType frameType ) const
{
  assert( IsTextFrame( frameType ) );
  auto it = std::ranges::find_if( textFrames_, [ &frames_ = frames_, frameType ]( size_t pos )
    {
      return frames_[ pos ].IsFrameID( frameType );
    } );
  if( it != std::end( textFrames_) )
    return *it; // position index

  return kInvalidFramePos;
}

///////////////////////////////////////////////////////////////////////////////
//
// Locate comment frame

const Mp3TagData::Frame* Mp3TagData::GetCommentFrame( size_t i ) const
{
  assert( i < commentFrames_.size() );
  auto framePos = GetCommentFrameReferencePos( i );
  if( framePos == kInvalidFramePos )
    return nullptr;
  return &( frames_[ framePos ] );
}

size_t Mp3TagData::GetCommentFrameReferencePos( size_t i ) const
{
  assert( i < commentFrames_.size() );
  if( i >= commentFrames_.size() )
    return kInvalidFramePos;
  return commentFrames_[ i ];
}

///////////////////////////////////////////////////////////////////////////////
//
// Flag the given frame for deletion. The frame remains in mFrames, so we know 
// to delete it during Write(), but the frame is removed from mTextFrames, 
// since it shouldn't be available for future GetText()s

void Mp3TagData::DeleteTextFrame( Mp3FrameType frameType )
{
  auto framePos = GetTextFrameReferencePos( frameType );
  if( framePos == kInvalidFramePos )
    return;

  frames_[ framePos ].FlagToDelete();
  auto pos = std::ranges::find( textFrames_, framePos );
  if( pos != textFrames_.end() )
    textFrames_.erase( pos );
  isDirty_ = true;
}

///////////////////////////////////////////////////////////////////////////////
//
// Flag the given comment for deletion. The frame remains in mFrames, so we know 
// to delete it during Write(), but the frame is removed from mCommentFrames, 
// since it shouldn't be available for future GetComment()s

void Mp3TagData::DeleteCommentFrame( size_t i )
{
  assert( i < commentFrames_.size() );
  if( i >= commentFrames_.size() )
    return;

  auto framePos = GetCommentFrameReferencePos( i );
  frames_[ framePos ].FlagToDelete();
  auto pos = std::ranges::find( commentFrames_, framePos );
  if ( pos != commentFrames_.end() )
    commentFrames_.erase( pos );
  isDirty_ = true;
}

///////////////////////////////////////////////////////////////////////////////
