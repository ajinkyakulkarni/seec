//===- include/seec/DSA/MemoryArea.hpp ------------------------------ C++ -===//
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

#ifndef SEEC_DSA_MEMORYAREA_HPP
#define SEEC_DSA_MEMORYAREA_HPP

#include "seec/DSA/Interval.hpp"

#include <cstdint>
#include <cstring>

namespace seec {


enum class MemoryPermission : uint8_t {
  None,
  ReadOnly,
  WriteOnly,
  ReadWrite
};


/// \brief Represents a contiguous range of memory.
///
class MemoryArea : public Interval<uint64_t> {
  MemoryPermission Access;
  
public:
  /// \brief Default constructor.
  ///
  MemoryArea()
  noexcept
  : Interval(Interval<uint64_t>::withStartEnd(0, 0)),
    Access(MemoryPermission::ReadWrite)
  {}

  /// \brief Initializing constructor.
  ///
  MemoryArea(uint64_t Address, std::size_t Length)
  noexcept
  : Interval(Interval<uint64_t>::withStartLength(Address, Length)),
    Access(MemoryPermission::ReadWrite)
  {}
  
  /// \brief Initializing constructor.
  ///
  MemoryArea(uint64_t Address, std::size_t Length, MemoryPermission Access)
  noexcept
  : Interval(Interval<uint64_t>::withStartLength(Address, Length)),
    Access(Access)
  {}

  /// \brief Initializing constructor.
  ///
  MemoryArea(void const *Start, std::size_t Length)
  noexcept
  : Interval(
      Interval<uint64_t>::withStartLength(reinterpret_cast<uintptr_t>(Start),
                                          static_cast<uint64_t>(Length))),
    Access(MemoryPermission::ReadWrite)
  {}
  
  /// \brief Initializing constructor.
  ///
  MemoryArea(void const *Start, std::size_t Length, MemoryPermission Access)
  noexcept
  : Interval(
      Interval<uint64_t>::withStartLength(reinterpret_cast<uintptr_t>(Start),
                                          static_cast<uint64_t>(Length))),
    Access(Access)
  {}

  /// \brief Copy constructor.
  ///
  MemoryArea(MemoryArea const &Other) noexcept = default;
  
  /// \brief Move constructor.
  ///
  MemoryArea(MemoryArea &&Other) noexcept = default;

  /// \brief Copy assignment.
  ///
  MemoryArea &operator=(MemoryArea const &RHS) noexcept = default;
  
  /// \brief Move assignment.
  ///
  MemoryArea &operator=(MemoryArea &&RHS) noexcept = default;


  /// \name Accessors
  /// @{

  /// Get the address of the first byte in this area.
  uint64_t address() const { return start(); }

  /// Get the address of the last byte in this area.
  uint64_t lastAddress() const { return last(); }
  
  /// Get the access permissions for this memory area.
  MemoryPermission getAccess() const { return Access; }

  /// @}
  
  
  /// \name Queries
  /// @{
  
  bool operator==(MemoryArea const &RHS) const {
    return Interval<uint64_t>::operator==(RHS);
  }
  
  bool operator!=(MemoryArea const &RHS) const {
    return Interval<uint64_t>::operator==(RHS);
  }
  
  /// @}
  
  
  /// \name Mutators
  /// @{
  
  /// \brief Get a copy of this MemoryArea with a new Length.
  MemoryArea withLength(std::size_t Length) const {
    return MemoryArea(start(), Length, Access);
  }
  
  /// @}
};


} // namespace seec

#endif // SEEC_DSA_MEMORYAREA_HPP
