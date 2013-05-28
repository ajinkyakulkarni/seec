//===- lib/Clang/GraphExpansion.hpp ---------------------------------------===//
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

#ifndef SEEC_LIB_CLANG_GRAPHEXPANSION_HPP
#define SEEC_LIB_CLANG_GRAPHEXPANSION_HPP

#include "seec/Clang/MappedValue.hpp"

#include <memory>
#include <vector>


namespace seec {

namespace cm {

class ProcessState;

namespace graph {


class Dereference {
  std::shared_ptr<ValueOfPointer const> Pointer;
  
  unsigned Index;
  
public:
  Dereference(std::shared_ptr<ValueOfPointer const> OfPointer,
              unsigned WithIndex)
  : Pointer(std::move(OfPointer)),
    Index(WithIndex)
  {}
};


class ExpansionImpl;


/// \brief Stores information about a state that has been expanded for graphing.
///
class Expansion final {
  std::unique_ptr<ExpansionImpl const> Impl;
  
  Expansion();
  
public:
  Expansion(Expansion &&) = default;
  
  ~Expansion();
  
  static Expansion from(seec::cm::ProcessState const &State);
  
  std::size_t
  countReferencesOf(std::shared_ptr<Value const> const &Value) const;
  
  std::vector<Dereference>
  getReferencesOf(std::shared_ptr<Value const> const &Value) const;
};


} // namespace graph (in cm in seec)

} // namespace cm (in seec)

} // namespace seec


#endif // SEEC_LIB_CLANG_GRAPHEXPANSION_HPP
