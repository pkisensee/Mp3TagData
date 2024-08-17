///////////////////////////////////////////////////////////////////////////////
//
//  Mp3TagData.cpp
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

// Strings can be encoded in different fashions. This struct disambiguates.

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

struct ID3v2TextFrame
{
  ID3v2FrameHdr hdr;          // 'Txxx' header
  uint8_t       textEncoding; // see kTextEncoding IDs above
  ID3v2String   str;
};

struct ID3v2CommentFrame
{
  ID3v2FrameHdr hdr;           // 'COMM', etc.
  uint8_t       textEncoding;  // see kTextEncoding IDs above
  char          language[ 3 ]; // e.g. "eng"
  ID3v2String   str;
};
#pragma pack(pop)

///////////////////////////////////////////////////////////////////////////////
//
// Converts a source integer from an ID3 file format big endian integer into a 
// useful native value. Automatically handles endian conversion. For synchSafe
// integers, kBitsPerByte should be 7.

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
// kBitsPerByte should be 7.

template <uint8_t kBits>
uint32_t WriteID3Int( uint32_t nativeInt )
{
  auto result = Util::UnpackBits<kBits>( nativeInt );
  return Util::ToBigEndian( result );
}

} // end anonymous namespace

///////////////////////////////////////////////////////////////////////////////
//
// Ctor

Mp3TagData::Mp3TagData( const std::filesystem::path& path )
  : mPath( path )
{
  ReadTagData();
}

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
    auto charCount = GetTextSize( textData, rawFrame );
    value.assign( textData, charCount );
    break;
  }
  case kTextEncodingUTF16:
  {
    auto textData = textFrame->str.unicode.utf16;
    auto charCount = GetTextSize( textData, rawFrame ) / sizeof( wchar_t );
    std::wstring unicode( textData, charCount );
    value = StringUtil::GetUtf8( unicode );
    break;
  }
  case kTextEncodingUTF16BE:
  default:
    assert( false ); // not encountered to date, so no need to write this code yet
    return std::string();
  }

  // In some buggy frames, trailing null bytes may be included in the frame size
  StrUtil::ToTrimmedTrailing( value, std::string( { '\0' } ));
  return value;
}

///////////////////////////////////////////////////////////////////////////////
//
// Number of comments in the MP3 file

size_t Mp3TagData::GetCommentCount() const
{
  return mCommentFrames.size();
}

///////////////////////////////////////////////////////////////////////////////
//
// MP3 files can have multiple comments; returns the comment at the given position

std::string Mp3TagData::GetComment(size_t i) const
{
  assert( i < mCommentFrames.size() );
  if( i >= mCommentFrames.size() )
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
    auto charCount = GetTextSize( textData, rawFrame );
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

    auto charCount = GetTextSize( textData, rawFrame ) / sizeof( wchar_t );
    std::wstring unicode( textData, charCount );
    value = StringUtil::GetUtf8( unicode );
    break;
  }
  case kTextEncodingUTF16BE:
  default:
    assert( false ); // not encountered to date, so no need to write this code yet
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
    mFrames.emplace_back( Frame() );
    framePos = mFrames.size() - 1;
    mTextFrames.emplace_back( framePos );
  }
  Mp3TagData::Frame* pFrame = &( mFrames[ framePos ] );

  // Always create UTF8 text frames, so we only need to account for the textEncoding byte
  // rather than the UTF16 BOM and so forth
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
  isDirty = true;
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

  assert( i <= mCommentFrames.size() );
  if( i == mCommentFrames.size() )
  {
    // Comment at index i isn't in file yet; create new frame and add to right lists 
    mFrames.emplace_back( Frame( nullptr ) );
    mCommentFrames.emplace_back( mFrames.size() - 1 );
  }

  FramePos framePos = mCommentFrames[ i ];
  Mp3TagData::Frame* pFrame = &( mFrames[framePos] );

  // Always create UTF8 comment frames for simplicity
  auto sizeAlloc = sizeof( ID3v2FrameHdr ) + sizeof( ID3v2CommentFrame::textEncoding ) +
                   sizeof( ID3v2CommentFrame::language ) + 
                   sizeof( '\0' ) + // empty description
                   newComment.size();
        
  pFrame->Allocate( sizeAlloc );
  ID3v2CommentFrame* pCommentFrame = reinterpret_cast<ID3v2CommentFrame*>( pFrame->GetData() );

  // Set the header fields
  auto frameSize = sizeAlloc - sizeof( ID3v2FrameHdr );
  SetFrameID( pCommentFrame->hdr.frameID, Mp3FrameType::Comment );
  pCommentFrame->hdr.size = WriteFrameSize( static_cast<uint32_t>(frameSize) );
  pCommentFrame->hdr.statusMessages = 0;
  pCommentFrame->hdr.formatDescription = 0;

  // Set the text fields; the description is empty
  pCommentFrame->textEncoding = kTextEncodingAnsi;
  memcpy( pCommentFrame->language, kEnglishLanguage, 3 );
  *pCommentFrame->str.utf8 = '\0'; // empty description
  memcpy( pCommentFrame->str.utf8 + sizeof('\0'), newComment.c_str(), newComment.size() );
  isDirty = true;
}

///////////////////////////////////////////////////////////////////////////////
//
// Location in file where to start looking for MPEG audio data

uint32_t Mp3TagData::GetAudioBufferOffset() const
{
  return mAudioBufferOffset;
}

///////////////////////////////////////////////////////////////////////////////
//
// Write modified or deleted frames to the given file, making sure that audio
// data remains intact

bool Mp3TagData::Write()
{
  if( !IsDirty() )
    return false;

  // Determine the size of the new frame section
  // TODO algorithm
  size_t frameSectionSize = {};
  for( const auto& frame : mFrames )
    frameSectionSize += frame.GetWriteBytes( mFileHeader.majorVersion );

  File mp3File( mPath );
  if( !mp3File.Open( FileFlags::Read | FileFlags::Write | FileFlags::SharedRead | FileFlags::SharedWrite ) )
  {
    PKLOG_WARN( "Failed to write MP3 data to %S; ERR: %d\n", mPath.c_str(), Util::GetLastError() );

    // Try one more time; useful in debugging scenarios
    if( !mp3File.Open( FileFlags::Read | FileFlags::Write | FileFlags::SharedRead | FileFlags::SharedWrite ) )
      return false;
  }

  // Padding bytes depends on whether new frames will fit within existing space
  size_t padBytes = ( frameSectionSize > mFrameBuffer.size() ) ? 
                      kPaddingBytes : ( mFrameBuffer.size() - frameSectionSize );

  // Write new id3v2 header size
  mFileHeader.synchSafeSize = WriteID3Int<7>( static_cast<uint32_t>(frameSectionSize + padBytes) );
  if( !mp3File.Write( &mFileHeader, sizeof( mFileHeader ) ) )
    return false;

  // Read existing audio data if we're going to overwrite it
  std::vector<uint8_t> audioData;
  if( frameSectionSize > mFrameBuffer.size() )
  {
    // TODO validate file size is less than 32-bit max
    size_t audioDataSize = size_t(mp3File.GetLength()) - mFrameBuffer.size() - sizeof( mFileHeader );
    audioData.resize( audioDataSize );
    if( mp3File.SetPos( sizeof( mFileHeader ) + mFrameBuffer.size() ) )
    {
      mp3File.Read( audioData.data(), audioDataSize );
      mp3File.SetPos( sizeof( mFileHeader ) );
    }
  }

  // Write all frames except deleted ones
  for( const auto& frame : mFrames )
  {
    if( frame.GetWriteBytes( mFileHeader.majorVersion ) )
      verify( mp3File.Write( frame.GetData(), frame.GetWriteBytes( mFileHeader.majorVersion ) ) );
  }

  // Pad with zeros
  if( padBytes )
  {
    std::vector<uint8_t> zeros( padBytes, 0 );
    verify( mp3File.Write( zeros.data(), zeros.size() ) );
  }

  // Append audio data if we overwrote it
  if( !audioData.empty() )
    verify( mp3File.Write( audioData.data(), audioData.size() ) );

  // Update all fields with correct new data
  mp3File.Close();
  ReadTagData();
  return true;
}

///////////////////////////////////////////////////////////////////////////////
//
// Determine if header looks reasonable

bool Mp3TagData::IsValidFileHeader() const
{
  // Validate header
  std::string id3 = { mFileHeader.id3[ 0 ], mFileHeader.id3[ 1 ], mFileHeader.id3[ 2 ] };
  if( id3 != kID3String )
  {
    PKLOG_WARN( "Invalid MP3 ID3v2 file %S; bad header\n", mPath.c_str() );
    return false;
  }
  assert( mFileHeader.majorVersion >= 3 );
  assert( mFileHeader.majorVersion != 0xFF );
  assert( mFileHeader.minorVersion != 0xFF );
  if( mFileHeader.majorVersion < 3 || 
      mFileHeader.majorVersion == 0xFF ||
      mFileHeader.minorVersion == 0xFF )
  {
    PKLOG_WARN( "Song %S has obsolete v2 or v1 header; resave\n", mPath.c_str() );
    return false;
  }

  // Validate flags
  assert( ( mFileHeader.flags & kFlagExtended ) == 0 );
  assert( ( mFileHeader.flags & kFlagExperimental ) == 0 );
  assert( ( mFileHeader.flags & kFlagsRemaining ) == 0 ); // must be cleared
  if( ( mFileHeader.minorVersion == 0xFF ) ||
    ( mFileHeader.flags & kFlagExtended ) ||
    ( mFileHeader.flags & kFlagExperimental ) ||
    ( mFileHeader.flags & kFlagsRemaining ) )
  {
    PKLOG_WARN( "Song %S has invalid header flags; resave\n", mPath.c_str() );
    return false;
  }

  return true;
}

///////////////////////////////////////////////////////////////////////////////
//
// Read tags into memory

void Mp3TagData::ReadTagData()
{
  mFrameBuffer.resize( 0 );
  mFrames.resize( 0 );
  mTextFrames.resize( 0 );
  mCommentFrames.resize( 0 );
  isDirty = false;

  File mp3File( mPath );
  if( !mp3File.Open( FileFlags::Read | FileFlags::SharedRead | FileFlags::SequentialScan ) )
    return;

  // Read id3v2 header
  if( !mp3File.Read( &mFileHeader, sizeof( mFileHeader ) ) )
  {
    PKLOG_WARN( "Failed to read MP3 file header %S; ERR: %d\n", mPath.c_str(), Util::GetLastError() );
    return;
  };

  if( !IsValidFileHeader() )
    return;

  auto frameSectionSize = ReadID3Int<7>( mFileHeader.synchSafeSize );
  assert( frameSectionSize < ( 1024 * 1024 ) ); // ensure reasonable
  mAudioBufferOffset = sizeof( mFileHeader ) + frameSectionSize;

  // Read all frames into memory
  mFrameBuffer.resize( frameSectionSize );
  size_t bytesRead;
  if( !mp3File.Read( mFrameBuffer.data(), frameSectionSize, bytesRead ) )
  {
    PKLOG_WARN( "Failed to read MP3 frames from %S; ERR: %d\n", mPath.c_str(), Util::GetLastError() );
    return;
  }
  // Close the file asynchronously while we parse the frames from memory)
  std::future fileClose = std::async( std::launch::async, [&] { mp3File.Close(); } );
  if( bytesRead < frameSectionSize )
    mFrameBuffer.resize( bytesRead );

  // Parse frames
  ParseFrames();
  fileClose.wait();
};

///////////////////////////////////////////////////////////////////////////////
//
// Returns true if we found and processed a frame; returns false if there are 
// no more frames to parse

bool Mp3TagData::ParseFrame( uint32_t& offset )
{
  // If we've reached end of the tag section, we're done
  if( offset >= mFrameBuffer.size() )
    return false;

  const auto* rawFrame = mFrameBuffer.data() + offset;

  // If we've hit a null byte or header is whacked, we're into padding territory and there are no more tags
  if( !Mp3BaseTagData::IsValidFrame( rawFrame ) )
    return false;

  Frame frame( rawFrame );
  mFrames.emplace_back( frame );

  offset += static_cast<uint32_t>(GetFrameBytes( rawFrame, mFileHeader.majorVersion ));
  return true;
}

///////////////////////////////////////////////////////////////////////////////
//
// Process all the frames 

void Mp3TagData::ParseFrames()
{
  auto offset = 0u;
  auto framesRemain = true;
  while( framesRemain )
    framesRemain = ParseFrame( offset );

  // Create sublists for common frame types
  for( size_t i = 0u; i < mFrames.size(); ++i )
  {
    if( mFrames[i].IsTextFrame() )
      mTextFrames.emplace_back( i );
    else if( mFrames[i].IsCommentFrame() )
      mCommentFrames.emplace_back( i );
  }

  // Check for duplicate text frames, which should never exist
  for( auto frameType = Mp3FrameType::First; frameType != Mp3FrameType::Comment; ++frameType )
  {
    [[maybe_unused]] size_t count = 0;
    for( auto i : mTextFrames )
      if( mFrames[ i ].IsFrameID( frameType ) )
        ++count;
    if( count > 1 )
      PKLOG_WARN( "Duplicate frame %s in %S\n", GetFrameID(frameType).c_str(), mPath.c_str());
  }
}

///////////////////////////////////////////////////////////////////////////////
//
// Converts the frameSize value to the proper ID3 file format. Version 3
// is just a big endian value. Other versions are synchSafe.

uint32_t Mp3TagData::WriteFrameSize( uint32_t frameSize ) const
{
  return (mFileHeader.majorVersion == 3) ? WriteID3Int<8>( frameSize ) : 
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
// Given basic information about the frame and the position of the text in the 
// frame, returns the text size in bytes

uint32_t Mp3TagData::GetTextSize( const void* textStart, const void* rawFrame ) const
{
  // <---->|<----->| TODO

  assert( rawFrame < textStart );
  uint32_t frameSize = GetFrameSize( reinterpret_cast<const uint8_t*>(rawFrame), mFileHeader.majorVersion );
  uint32_t textSize = frameSize + sizeof( ID3v2FrameHdr );
  auto offset = reinterpret_cast<const uint8_t*>( textStart ) - 
                reinterpret_cast<const uint8_t*>( rawFrame );
  assert( offset <= std::numeric_limits<uint32_t>::max() );
  auto offset32u = static_cast<uint32_t>( offset );
  assert( offset32u <= textSize );
  textSize -= offset32u;
  return textSize;
}

///////////////////////////////////////////////////////////////////////////////
//
// Locate text frame

const Mp3TagData::Frame* Mp3TagData::GetTextFrame( Mp3FrameType frameType ) const
{
  auto framePos = GetTextFrameReferencePos( frameType );
  if( framePos == kInvalidFramePos )
    return nullptr;
  return &( mFrames[ framePos ] );
}

size_t Mp3TagData::GetTextFrameReferencePos( Mp3FrameType frameType ) const
{
  assert( IsTextFrame( frameType ) );
  for( size_t i=0; i < mFrames.size(); ++i )
  {
    if( mFrames[ i ].IsFrameID( frameType ) )
      return i;
  }
  return kInvalidFramePos;
}

///////////////////////////////////////////////////////////////////////////////
//
// Locate comment frame

const Mp3TagData::Frame* Mp3TagData::GetCommentFrame( size_t i ) const
{
  auto framePos = GetCommentFrameReferencePos( i );
  return &( mFrames[ framePos ] );
}

size_t Mp3TagData::GetCommentFrameReferencePos( size_t i ) const
{
  assert( i < mCommentFrames.size() );
  return mCommentFrames[ i ];
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

  mFrames[ framePos ].FlagToDelete();
  auto pos = std::find( mTextFrames.begin(), mTextFrames.end(), framePos );
  if( pos != mTextFrames.end() )
    mTextFrames.erase( pos );
  isDirty = true;
}

///////////////////////////////////////////////////////////////////////////////
//
// Flag the given comment for deletion. The frame remains in mFrames, so we know 
// to delete it during Write(), but the frame is removed from mCommentFrames, 
// since it shouldn't be available for future GetComment()s

void Mp3TagData::DeleteCommentFrame( size_t i )
{
  assert( i < mCommentFrames.size() );
  if( i >= mCommentFrames.size() )
    return;

  auto framePos = GetCommentFrameReferencePos( i );
  mFrames[ framePos ].FlagToDelete();
  auto pos = std::find( mCommentFrames.begin(), mCommentFrames.end(), framePos );
  if ( pos != mCommentFrames.end() )
    mCommentFrames.erase( pos );
  isDirty = true;
}

///////////////////////////////////////////////////////////////////////////////
