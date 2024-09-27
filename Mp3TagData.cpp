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

#include <algorithm>
#include <future>
#include <limits>
#include <ranges>

#include "APEv2Tags.h"
#include "File.h"
#include "Log.h"
#include "Mp3TagData.h"
#include "Util.h"

using namespace PKIsensee;

namespace // anonymous
{

constexpr size_t   kInvalidFramePos = size_t( -1 );
constexpr size_t   kPaddingBytes = 2048u; // commonly used in MP3 tagging software
constexpr uint64_t kBacktrackBufferSize = 4096u;  // chunk size of APE header search
constexpr uint64_t kNoApeHeader = uint64_t( -1 );

} // end anonymous namespace

///////////////////////////////////////////////////////////////////////////////
//
// Read tags into memory

bool Mp3TagData::LoadTagData( const std::filesystem::path& path )
{
  path_ = path;
  id3FrameBuffer_.resize( 0 );
  apeFrameBuffer_.resize( 0 );
  id3Frames_.resize( 0 );
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

  auto frameSectionSize = fileHeader_.GetSize();
  assert( frameSectionSize < ( 1024 * 1024 ) ); // ensure reasonable
  audioBufferOffset_ = sizeof( fileHeader_ ) + frameSectionSize;

  // Read ID3 frames into memory buffer
  id3FrameBuffer_.resize( frameSectionSize );
  uint32_t bytesRead;
  if( !mp3File.Read( id3FrameBuffer_.data(), frameSectionSize, bytesRead ) )
  {
    PKLOG_WARN( "Failed to read ID3 frames from %S; ERR: %d\n", path_.c_str(), Util::GetLastError() );
    return false;
  }
  if( bytesRead < frameSectionSize )
    id3FrameBuffer_.resize( bytesRead );

  // Read APE tags into memory buffer
  uint64_t apeStart = FindApeHeaderOffset( mp3File );
  if( apeStart != kNoApeHeader )
  {
    uint32_t apeHeaderBytes = static_cast<uint32_t>( mp3File.GetLength() - apeStart );
    std::vector<uint8_t> apeBuffer_;
    apeFrameBuffer_.resize( apeHeaderBytes );
    if( !mp3File.SetPos( apeStart ) || 
        !mp3File.Read( apeFrameBuffer_.data(), apeHeaderBytes, bytesRead ) )
    {
      PKLOG_WARN( "Failed to read APE tags from %S; ERR: %d\n", path_.c_str(), Util::GetLastError() );
      return false;
    }
    if( bytesRead < apeHeaderBytes )
      apeFrameBuffer_.resize( bytesRead );
  }

  // Close the file asynchronously while we parse the frames from memory
  std::future fileClose = std::async( std::launch::async, [&] { mp3File.Close(); } );

  // Parse frames/tags
  ParseID3Frames();
  ParseAPETags();
  fileClose.wait();
  return true;
};

///////////////////////////////////////////////////////////////////////////////
//
// Extract the MP3 tag string for the given text frame type

std::string Mp3TagData::GetText( Mp3FrameType frameType ) const
{
  if( IsID3TextFrame( frameType ) )
  {
    const ID3Frame* pFrame = GetID3TextFrame( frameType );
    if( pFrame == nullptr )
      return {};

    const auto* rawFrame = pFrame->GetData();
    const auto* textFrame = reinterpret_cast<const ID3v2TextFrame*>( rawFrame );
    return textFrame->GetText( fileHeader_.GetMajorVersion() );
  }
  if( IsAPETextTag( frameType ) )
  {
    const APETag* pTag = GetAPETextTag( frameType );
    if( pTag == nullptr )
      return {};

    const auto* rawTag = pTag->GetData();
    const auto* textTag = reinterpret_cast<const APEv2TagItem*>( rawTag );
    return std::string( textTag->GetText() );
  }
  return {};
}

///////////////////////////////////////////////////////////////////////////////
//
// Number of comments in the MP3 file

size_t Mp3TagData::GetCommentCount() const
{
  auto commentCount = std::ranges::count_if( id3Frames_, [](const auto& frame)
    {
      return frame.IsFrameID( Mp3FrameType::ID3Comment );
    } );
  return size_t( commentCount );
}

///////////////////////////////////////////////////////////////////////////////
//
// MP3 files can have multiple comments; returns the comment at the given position
// See https://mutagen-specs.readthedocs.io/en/latest/id3/id3v2.2.html#comments

std::string Mp3TagData::GetComment( size_t i ) const
{
  if( !PK_VALID( i < GetCommentCount() ) )
    return {};

  const ID3Frame* pFrame = GetID3CommentFrame( i );
  if( pFrame == nullptr )
    return {};

  const auto* rawFrame = pFrame->GetData();
  const auto* commentFrame = reinterpret_cast<const ID3v2CommentFrame*>( rawFrame );
  return commentFrame->GetText( fileHeader_.GetMajorVersion() );
}

///////////////////////////////////////////////////////////////////////////////
//
// Update existing text frame, create new frame if one doesn't exist, or
// delete frame if newStr is empty

void Mp3TagData::SetText( Mp3FrameType frameType, std::string_view newStr )
{
  // Modifying APE tags not currently supported
  if( !PK_VALID( frameType < Mp3FrameType::ID3Max ) )
    return;

  if( newStr.empty() )
  {
    DeleteTextFrame( frameType );
    return;
  }

  // Locate the text frame
  ID3Frame* pFrame = FindID3TextFrame( frameType );

  // If frame type doesn't exist yet, create new frame
  if( pFrame == nullptr )
  {
    id3Frames_.emplace_back( ID3Frame{} );
    pFrame = &id3Frames_.back();
  }

  // Size the frame appropriately
  auto sizeAlloc = ID3v2TextFrame::GetFrameSize( newStr );
  pFrame->Allocate( sizeAlloc );

  // Set the frame fields
  std::string_view frameID = GetID3FrameID( frameType );
  uint32_t frameSize = static_cast<uint32_t>( sizeAlloc - sizeof( ID3v2FrameHdr ) );
  ID3v2TextFrame* pTextFrame = reinterpret_cast<ID3v2TextFrame*>( pFrame->GetData() );
  pTextFrame->SetHeader( frameID, frameSize, fileHeader_.GetMajorVersion() );
  pTextFrame->SetText( newStr );
  isDirty_ = true;
}

///////////////////////////////////////////////////////////////////////////////
//
// Set the comment at the given position; use position GetCommentCount() 
// to add a new comment; deletes comment frame if newComment is empty

void Mp3TagData::SetComment( size_t i, std::string_view newComment )
{
  if( newComment.empty() )
  {
    DeleteCommentFrame( i );
    return;
  }

  if( !PK_VALID( i <= GetCommentCount() ) )
    return;

  ID3Frame* pFrame = FindID3CommentFrame( i );
  if( pFrame == nullptr )
  {
    // Create new comment frame
    assert( i == GetCommentCount() );
    id3Frames_.emplace_back( ID3Frame{} );
    pFrame = &id3Frames_.back();
  }

  // Size the comment frame appropriately
  auto sizeAlloc = ID3v2CommentFrame::GetFrameSize( newComment );
  pFrame->Allocate( sizeAlloc );

  // Set the comment frame fields
  uint32_t frameSize = static_cast<uint32_t>( sizeAlloc - sizeof( ID3v2FrameHdr ) );
  std::string_view frameID = GetID3FrameID( Mp3FrameType::ID3Comment );
  ID3v2CommentFrame* pCommentFrame = reinterpret_cast<ID3v2CommentFrame*>( pFrame->GetData() );
  pCommentFrame->SetHeader( frameID, frameSize, fileHeader_.GetMajorVersion() );
  pCommentFrame->SetText( newComment );
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
// and APE data remains intact

bool Mp3TagData::Write()
{
  if( !IsDirty() )
    return false;

  // same as std::accumulate
  size_t frameSectionSize = 
    std::ranges::fold_left( id3Frames_, size_t{}, [ fh = fileHeader_ ]( size_t sum, const ID3Frame& frame )
    {
      return sum + frame.GetWriteBytes( fh.GetMajorVersion() );
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
  size_t padBytes = ( frameSectionSize > id3FrameBuffer_.size() ) ? 
                      kPaddingBytes : ( id3FrameBuffer_.size() - frameSectionSize );

  // Write new ID3v2 header size
  fileHeader_.SetSize( static_cast<uint32_t>( frameSectionSize + padBytes ) );
  if( !mp3File.Write( &fileHeader_, sizeof( fileHeader_ ) ) )
    return false;

  // Read existing audio and APE data if we're going to overwrite it
  std::vector<uint8_t> audioData;
  if( frameSectionSize > id3FrameBuffer_.size() )
  {
    uint64_t audioDataSize64 = mp3File.GetLength() - id3FrameBuffer_.size() - sizeof( fileHeader_ );
    assert( audioDataSize64 <= std::numeric_limits<uint32_t>::max() );
    uint32_t audioDataSize = static_cast<uint32_t>( audioDataSize64 );
    audioData.resize( audioDataSize );
    uint64_t pos = sizeof( fileHeader_ ) + id3FrameBuffer_.size();
    if( mp3File.SetPos( pos ) )
    {
      mp3File.Read( audioData.data(), audioDataSize );
      mp3File.SetPos( sizeof( fileHeader_ ) );
    }
  }

  // Write all frames except deleted ones
  for( const auto& frame : id3Frames_ )
  {
    auto bytesToWrite = frame.GetWriteBytes( fileHeader_.GetMajorVersion() );
    if( bytesToWrite )
      PK_VALID( mp3File.Write( frame.GetData(), bytesToWrite ) );
  }

  // Pad with zeros
  if( padBytes )
  {
    // It's possible to have 2K stack buffer rather than a heap allocation, but this is simpler
    // and dominated by the file write time anyway
    std::vector<uint8_t> zeros( padBytes, 0 );
    PK_VALID( mp3File.Write( zeros.data(), uint32_t( padBytes ) ) );
  }

  // Append audio and APE data if it was overwritten
  if( !audioData.empty() )
    PK_VALID( mp3File.Write( audioData.data(), uint32_t( audioData.size() ) ) );

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
  if( fileHeader_.GetHeaderID() != kID3String )
  {
    PKLOG_WARN( "\nInvalid MP3 ID3v2 file %S; bad header\n", path_.c_str() );
    return false;
  }
  if( fileHeader_.GetMajorVersion() < 3 ||
      fileHeader_.GetMajorVersion() == 0xFF ||
      fileHeader_.GetMinorVersion() == 0xFF )
  {
    PKLOG_WARN( "\nSong %S has obsolete v2 or v1 header; resave\n", path_.c_str() );
    return false;
  }

  // Validate flags
  auto flags = fileHeader_.GetFlags();
  if( ( flags & ID3v2FileHeader::kFlagExtended ) ||
      ( flags & ID3v2FileHeader::kFlagExperimental ) ||
      ( flags & ID3v2FileHeader::kFlagsRemaining ) )
  {
    PKLOG_WARN( "\nSong %S has invalid header flags; resave\n", path_.c_str() );
    return false;
  }

  return true;
}

///////////////////////////////////////////////////////////////////////////////
//
// True if ID3 frame found and processed; false when there are no more frames left

bool Mp3TagData::ParseID3Frame( uint32_t& offset )
{
  // If we've reached end of the tag section, we're done
  if( offset >= id3FrameBuffer_.size() )
    return false;

  const auto* rawFrame = id3FrameBuffer_.data() + offset;

  // If we've hit a null byte or header is whacked, 
  // we're into padding territory and there are no more tags
  if( !Mp3BaseTagData::IsValidID3Frame( rawFrame ) )
    return false;

  ID3Frame frame( rawFrame );
  id3Frames_.emplace_back( frame );

  offset += GetID3FrameBytes( rawFrame, fileHeader_.GetMajorVersion() );
  return true;
}

///////////////////////////////////////////////////////////////////////////////
//
// Process all the ID3 frames 

void Mp3TagData::ParseID3Frames()
{
  // Build frame list
  uint32_t offset = 0u;
  bool framesRemain = true;
  while( framesRemain )
    framesRemain = ParseID3Frame( offset );

  // Check for duplicate ID3 text frames, which should never exist
  for( auto frameType = Mp3FrameType::First; frameType < Mp3FrameType::ID3TextMax; ++frameType )
  {
    size_t count = 0u;
    for( const auto& frame : id3Frames_ )
      if( frame.IsFrameID( frameType ) )
        ++count;
    if( count > 1 )
    {
      [[maybe_unused]] std::string_view frameID = GetID3FrameID( frameType );
      PKLOG_WARN( "\nDuplicate frame %.*s in %S\n", frameID.size(), frameID.data(), path_.c_str() );
    }
  }
}

///////////////////////////////////////////////////////////////////////////////
//
// Read next APE tag

bool Mp3TagData::ParseAPETag( uint32_t& offset )
{
  // Safety check: if unexpected end of the tag section, something is wrong
  // so bail out
  if( offset >= apeFrameBuffer_.size() )
    return false;
  
  const auto* rawTag = apeFrameBuffer_.data() + offset;
  APETag tag( rawTag );
  apeTags_.emplace_back( tag );

  // Determine where to find the next tag
  const auto* apeTagItem = reinterpret_cast<const APEv2TagItem*>( rawTag );
  offset += apeTagItem->GetTagSize();
  return true;
}

///////////////////////////////////////////////////////////////////////////////
//
// Process all the APE tags
// See https://mutagen-specs.readthedocs.io/en/latest/apev2/apev2.html#

void Mp3TagData::ParseAPETags()
{
  if( apeFrameBuffer_.empty() )
    return;

  // Validate the header
  const auto* rawTag = apeFrameBuffer_.data();
  const auto* apeTagHeader = reinterpret_cast<const APEv2TagHeader*>( rawTag );
  assert(apeTagHeader->IsHeader());

  // Build tag item list
  uint32_t offset = sizeof(APEv2TagHeader);
  for( auto itemCount = apeTagHeader->GetItemCount(); itemCount; --itemCount )
    if( !ParseAPETag( offset ) )
      break;

  // Validate the footer
  assert( offset == apeTagHeader->GetTagBlockSize());
  rawTag = apeFrameBuffer_.data() + offset;
  [[maybe_unused]] const auto* apeTagFooter = reinterpret_cast<const APEv2TagHeader*>( rawTag );
  assert( !apeTagFooter->IsHeader() );
}

///////////////////////////////////////////////////////////////////////////////
//
// Extract the frame size from the given ID3 frame
// 
//  rawFrame
//  |
//  v
// |<--ID3v2FrameHdr-->|<-----frameSize------->|
//       ^
//       |
//       frameSize stored here

uint32_t Mp3TagData::GetID3FrameSize( const uint8_t* rawFrame, uint8_t majorVersion ) // static
{
  assert( rawFrame != nullptr );
  const auto* frameHeader = reinterpret_cast<const ID3v2FrameHdr*>( rawFrame );
  return frameHeader->GetSize( majorVersion );
}

///////////////////////////////////////////////////////////////////////////////
//
// Extract the number of bytes represented by this ID3 frame
// 
//  rawFrame
//  |
//  v
// |<--ID3v2FrameHdr-->|<-----frameSize------->|
// |                                           |
// |<---------------frameBytes---------------->|

uint32_t Mp3TagData::GetID3FrameBytes( const uint8_t* rawFrame, uint8_t version ) // static
{
  assert( rawFrame != nullptr );
  return sizeof( ID3v2FrameHdr ) + GetID3FrameSize( rawFrame, version );
}

///////////////////////////////////////////////////////////////////////////////
//
// Locate APE header in the MP3 file
//
// Typically found near the end of the file. Search in chunks to maximize
// speed. Returns the file offset of the APE header, or kNoApeHeader if not found.

uint64_t Mp3TagData::FindApeHeaderOffset( File& mp3File ) const
{
  auto fileSize = mp3File.GetLength();
  auto filePos = ( kBacktrackBufferSize > fileSize ) ? 0u : fileSize - kBacktrackBufferSize;
  std::string searchBuffer;
  searchBuffer.resize( kBacktrackBufferSize );
  uint32_t bytesRead = 0u;
  auto findPos = std::string::npos;
  auto tagLength = APEv2TagHeader::GetStdApeTag().size();
  uint32_t readLength = kBacktrackBufferSize;

  while( findPos == std::string::npos && filePos > 0 )
  {
    // Read the search buffer from the MP3 file
    mp3File.SetPos( filePos );
    if( !mp3File.Read( searchBuffer.data(), readLength, bytesRead ) )
    {
      PKLOG_WARN( "Failed to read MP3 APE frames from %S; ERR: %d\n", path_.c_str(), Util::GetLastError() );
      return kNoApeHeader;
    }

    findPos = searchBuffer.find( APEv2TagHeader::GetStdApeTag() );
    if( findPos != std::string::npos )
    {
      // Found the APE header
      uint64_t apeStart = filePos + findPos;
      return apeStart;
    }

    // Keep searching
    if( kBacktrackBufferSize > filePos )
      filePos = 0u;
    else
      filePos -= kBacktrackBufferSize;

    // For all searches after the first, include the tag length in the buffer
    // to detect scenarios where the tag is on the border of the two backtrackBuffers
    readLength = uint32_t( kBacktrackBufferSize + tagLength );
    searchBuffer.resize( readLength );
  }

  // Searched the entire file and no APE header
  return kNoApeHeader;
}

///////////////////////////////////////////////////////////////////////////////
//
// Locate ID3 text frame
//
// There are on order of a couple dozen of ID3 frames in a typical MP3 file and rarely
// more than 50, so linear search on contiguous data is fine

const Mp3TagData::ID3Frame* Mp3TagData::GetID3TextFrame( Mp3FrameType frameType ) const
{
  return FindID3TextFrame( frameType );
}

const Mp3TagData::ID3Frame* Mp3TagData::FindID3TextFrame( Mp3FrameType frameType ) const
{
  auto it = std::ranges::find_if( id3Frames_, [frameType]( const auto& frame )
    {
      return frame.IsFrameID( frameType );
    } );
  return ( it != std::end( id3Frames_ ) ) ? &( *it ) : nullptr;
}

Mp3TagData::ID3Frame* Mp3TagData::FindID3TextFrame( Mp3FrameType frameType )
{
  // casting cruft avoids code duplication
  auto frame = std::as_const( *this ).FindID3TextFrame( frameType ); // invoke fn above
  return const_cast<Mp3TagData::ID3Frame*>( frame );
}

///////////////////////////////////////////////////////////////////////////////
//
// Locate ID3 comment frame
//
// There are on order of a couple dozen of ID3 frames in a typical MP3 file and rarely
// more than 50, so linear search on contiguous data is fine

const Mp3TagData::ID3Frame* Mp3TagData::GetID3CommentFrame( size_t i ) const
{
  assert( i < GetCommentCount() );
  return FindID3CommentFrame( i );
}

const Mp3TagData::ID3Frame* Mp3TagData::FindID3CommentFrame( size_t i ) const
{
  size_t commentIndex = 0;
  auto it = std::ranges::find_if( id3Frames_, [i, &commentIndex]( const auto& frame )
    {
      return frame.IsFrameID( Mp3FrameType::ID3Comment ) && ( i == commentIndex++ );
    } );
  return ( it != std::end( id3Frames_ ) ) ? &( *it ) : nullptr;
}

Mp3TagData::ID3Frame* Mp3TagData::FindID3CommentFrame( size_t i )
{
  // casting cruft avoids code duplication
  auto frame = std::as_const( *this ).FindID3CommentFrame( i ); // invoke fn above
  return const_cast<Mp3TagData::ID3Frame*>( frame );
}

///////////////////////////////////////////////////////////////////////////////
//
// Locate APE text frame
//
// There are on order of a dozen of APE frames in a typical MP3 file and rarely
// more than 30, so linear search on contiguous data is fine

const Mp3TagData::APETag* Mp3TagData::GetAPETextTag( Mp3FrameType tagType ) const
{
  return FindAPETextTag( tagType );
}

const Mp3TagData::APETag* Mp3TagData::FindAPETextTag( Mp3FrameType tagType ) const
{
  auto it = std::ranges::find_if( apeTags_, [tagType]( const auto& tag )
    {
      return tag.IsTagID( tagType );
    } );
  return ( it != std::end( apeTags_ ) ) ? &( *it ) : nullptr;
}

Mp3TagData::APETag* Mp3TagData::FindAPETextTag( Mp3FrameType tagType )
{
  // casting cruft avoids code duplication
  auto tag = std::as_const( *this ).FindAPETextTag( tagType ); // invoke fn above
  return const_cast<Mp3TagData::APETag*>( tag );
}

///////////////////////////////////////////////////////////////////////////////
//
// Flag the given frame for deletion

void Mp3TagData::DeleteTextFrame( Mp3FrameType frameType )
{
  auto pFrame = FindID3TextFrame( frameType );
  if (pFrame != nullptr )
    pFrame->FlagToDelete();
  isDirty_ = true;
}

///////////////////////////////////////////////////////////////////////////////
//
// Flag the given comment for deletion

void Mp3TagData::DeleteCommentFrame( size_t i )
{
  if( !PK_VALID( i < GetCommentCount() ) )
    return;

  auto* pCommentFrame = FindID3CommentFrame( i );
  pCommentFrame->FlagToDelete();
  isDirty_ = true;
}

///////////////////////////////////////////////////////////////////////////////
//
// Stream helpers

namespace {

std::string PrintEncoding( ID3TextEncoding encoding )
{
  std::ostringstream oss;
  oss << "Enc:" << static_cast<int>( encoding ) << "<";
  switch( encoding )
  {
  case ID3TextEncoding::ANSI:    oss << "ANSI";    break;
  case ID3TextEncoding::UTF16:   oss << "UTF16";   break;
  case ID3TextEncoding::UTF16BE: oss << "UTF16BE"; break;
  case ID3TextEncoding::UTF8:    oss << "UTF8";    break;
  default: break;
  }
  oss << '>';
  return oss.str();
}

std::string PrintTextWithPrefix( std::string_view prefix, std::string_view text )
{
  std::ostringstream oss;
  oss << prefix << ":\"" << text << "\"[" << text.size() << ']';
  return oss.str();
}

std::string PrintText( std::string_view text )
{
  return PrintTextWithPrefix("Txt", text);
}

std::string PrintKey( std::string_view key )
{
  return PrintTextWithPrefix("Key", key);
}

std::string PrintBlob( std::span<const uint8_t> blob )
{
  std::ostringstream oss;
  oss << "Dta:";
  oss << std::hex;
  for( auto u : blob )
    oss << std::uppercase << std::setfill('0') << std::setw(2) << std::right << +u << ' ';
  oss << std::dec << "[" << blob.size() << "]";
  return oss.str();
}

} // anonymous

///////////////////////////////////////////////////////////////////////////////
//
// Stream frame/tag info as text

std::ostream& PKIsensee::operator<<( std::ostream& out, const Mp3TagData& tagData )
{
  out << "Path: " << tagData.path_ << '\n';

  const ID3v2FileHeader& hdr = tagData.fileHeader_;
  out << "ID3:" << hdr.GetHeaderID() << ' ';
  out << "Ver:" << std::dec << +hdr.GetMajorVersion() << '.' << +hdr.GetMinorVersion() << ' ';
  out << "Flg:0x" << std::hex << std::uppercase << +hdr.GetFlags() << std::dec << ' ';
  out << "Siz:" << hdr.GetSize() << " (" << std::hex << std::uppercase << hdr.GetSize() << std::dec << ")\n";
  out << "AudOffset:" << tagData.audioBufferOffset_ << '\n';

  for( const auto& f : tagData.id3Frames_ )
  {
    out << "ID3: " << f.GetID3FrameID();
    const auto* rawFrame = f.GetData();
    const auto* id3Frame = reinterpret_cast<const ID3v2FrameHdr*>( rawFrame );
    out << " Siz:" << id3Frame->GetSize( hdr.GetMajorVersion() ) << ' ';
    if( f.IsTextFrame() )
    {
      const auto* textFrame = reinterpret_cast<const ID3v2TextFrame*>( rawFrame );
      out << PrintText(textFrame->GetText(hdr.GetMajorVersion())) << ' ';
      out << PrintEncoding( textFrame->GetTextEncoding() ) << '\n';
    }
    else if( f.IsCommentFrame() )
    {
      const auto* commFrame = reinterpret_cast<const ID3v2CommentFrame*>( rawFrame );
      out << PrintText( commFrame->GetText( hdr.GetMajorVersion() ) ) << ' ';
      out << PrintEncoding(commFrame->GetTextEncoding()) << '\n';
    }
    else if ( f.IsPrivateFrame() )
    {
      const auto* privFrame = reinterpret_cast<const ID3v2PrivateFrame*>( rawFrame );
      out << PrintText( privFrame->GetText() ) << ' ';
      out << PrintBlob( privFrame->GetData( hdr.GetMajorVersion() ) ) << '\n';
    }
    else // some other frame type
    {
      out << '\n';
    }
  }

  // Output APE tags
  for( const auto& t : tagData.apeTags_ )
  {
    const auto* rawTag = t.GetData();
    const auto* apeTag = reinterpret_cast<const APEv2TagItem*>( rawTag );
    out << "APE: Siz:" << apeTag->GetTagSize() << ' ';
    out << PrintKey(apeTag->GetKey()) << ' ';
    out << ( apeTag->IsText() ? PrintText(apeTag->GetText()) :
                                PrintBlob(apeTag->GetData()) );
    if( apeTag->IsReadOnly() )
      out << "(RO)";
    out << '\n';
  }

  return out;
}

///////////////////////////////////////////////////////////////////////////////
