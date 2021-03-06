//===- Util/TemplateSequence.hpp ------------------------------------ C++ -===//
//
//                                    SeeC
//
// This file is distributed under The MIT License (MIT). See LICENSE.TXT for
// details.
//
//===----------------------------------------------------------------------===//
///
/// \file
///
//===----------------------------------------------------------------------===//

#ifndef SEEC_UTIL_TEMPLATESEQUENCE_HPP
#define SEEC_UTIL_TEMPLATESEQUENCE_HPP

#include <type_traits>

namespace seec {

/// Compile-time utilities.
namespace ct {

/// \brief A compile-time sequence of ints.
template<int ...>
struct sequence_int {};

/// \brief Build a compile-time sequence of ints to cover a range.
/// Builds a sequence [Start, End) by recursively instantiating itself and
/// accumulating in the Sequence parameter pack.
template<int Start, int End, int ...Sequence>
struct generate_sequence_int
: public generate_sequence_int<Start, End - 1, End - 1, Sequence...> {};

/// \brief Build a compile-time sequence of ints to cover a range.
/// Base case of the recursion, create a sequence using all values that have
/// accumulated in the Sequence parameter pack.
template<int StartEnd, int ...Sequence>
struct generate_sequence_int<StartEnd, StartEnd, Sequence...> {
  typedef sequence_int<Sequence...> type;
};

/// \brief Check whether a compile-time sequence of bools are all true.
/// From: http://stackoverflow.com/questions/13562823/parameter-pack-aware-stdis-base-of
template<bool... B> struct static_all_of;

template<bool... Tail>
struct static_all_of<true, Tail...> : static_all_of<Tail...> {};

template<bool... Tail>
struct static_all_of<false, Tail...> : std::false_type {};

template<>
struct static_all_of<> : std::true_type {};

/// \brief Check whether at least one of a compile time sequence of bools is
///        true.
template<bool... B> struct static_any_of;

template<bool... Tail>
struct static_any_of<true, Tail...> : std::true_type {};

template<bool... Tail>
struct static_any_of<false, Tail...> : static_any_of<Tail...> {};

template<>
struct static_any_of<> : std::false_type {};

} // namespace ct

} // namespace seec

#endif // SEEC_UTIL_TEMPLATESEQUENCE_HPP
