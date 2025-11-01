///////////////////////////////////////////////////////////////////////////////
//
//  Mp3FieldIterator.h
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
// ----------------------------------------------------------------------------
// 
//  MP3 text frames commonly use semicolons to separate multiple entries.
//  This helper extracts semicolon-separated sub-fields from MP3 text frames.
// 
//  Examples:
//
//    Artist frame: "Kaskade; Haley Gibby" -> 2 artists
//    Composer frame: "Daryl Hall; John Oates" -> 2 composers 
//
//  Example usage:
//
//    std::string composers = Mp3TagData::GetText( Mp3FrameType::ID3Composer );
//    for ( auto i = BeginMp3Field(composers); i != EndMp3Field(composers); ++i )
//      std::cout << *i; // print each composer
// 
///////////////////////////////////////////////////////////////////////////////

#pragma once
#include <string>
#include <string_view>
#include <algorithm>

#include "CharUtil.h"

namespace PKIsensee
{

class Mp3FieldIterator
{
public:
  // Iterator traits
  using iterator_category = std::forward_iterator_tag;
  using value_type = std::string_view;
  using difference_type = std::ptrdiff_t;
  using pointer = const std::string_view*;
  using reference = const std::string_view&;

  Mp3FieldIterator() = default;

  Mp3FieldIterator( std::string_view text, size_t pos, bool isEnd = false ) : 
    text_( text ), 
    start_( pos ), 
    isEnd_( isEnd )
  {
    if ( !isEnd_ && !text_.empty() )
      findNextField();
    else
      end_ = pos;
  }

  std::string_view operator*() const
  {
    return currentField_;
  }

  // Pre-increment operator
  Mp3FieldIterator& operator++()
  {
    if ( isEnd_ )
      return *this;

    start_ = end_;
    if ( start_ < text_.size() )
    {
      if ( text_[ start_ ] == ';' ) // skip semicolon separator
        ++start_;
      findNextField();
    }
    else
      isEnd_ = true;

    return *this;
  }

  Mp3FieldIterator operator++( int )
  {
    Mp3FieldIterator tmp = *this;
    ++( *this );
    return tmp;
  }

  bool operator==( const Mp3FieldIterator& rhs ) const
  {
    if ( isEnd_ && rhs.isEnd_ )
      return true;
    if ( isEnd_ != rhs.isEnd_ )
      return false;
    return text_ == rhs.text_ && start_ == rhs.start_;
  }

  bool operator!=( const Mp3FieldIterator& rhs ) const
  {
    return !( *this == rhs );
  }

private:

  void findNextField()
  {
    if ( start_ >= text_.size() )
    {
      isEnd_ = true;
      return;
    }

    // Skip leading whitespace
    while ( start_ < text_.size() && CharUtil::IsWhitespace( text_[start_] ) )
      ++start_;

    if ( start_ >= text_.size() )
    {
      isEnd_ = true;
      return;
    }

    // End of field is next semicolon or end of string
    end_ = text_.find( ';', start_ );
    if ( end_ == std::string::npos )
      end_ = text_.size();

    // Trim trailing whitespace
    size_t fieldEnd = end_;
    while ( fieldEnd > start_ && CharUtil::IsWhitespace( text_[ fieldEnd - 1 ] ) )
      --fieldEnd;

    // Create the current field view
    currentField_ = std::string_view( text_.data() + start_, fieldEnd - start_ );
  }

private:
  std::string_view text_;
  std::string_view currentField_;
  size_t start_ = 0;
  size_t end_ = 0;
  bool isEnd_ = true;

}; // end class Mp3FieldIterator

// Factory functions; usage is like std::begin() and std::end()
inline Mp3FieldIterator BeginMp3Field( std::string_view text )
{
  return Mp3FieldIterator( text, 0, text.empty() );
}

inline Mp3FieldIterator EndMp3Field( std::string_view text )
{
  return Mp3FieldIterator( text, text.size(), true );
}

} // end namespace PKIsensee

///////////////////////////////////////////////////////////////////////////////
