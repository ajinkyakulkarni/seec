//===- include/seec/Clang/MappedFunctionState.hpp -------------------------===//
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

#ifndef SEEC_CLANG_MAPPEDFUNCTIONSTATE_HPP
#define SEEC_CLANG_MAPPEDFUNCTIONSTATE_HPP

#include "seec/Clang/MappedStateCommon.hpp"
#include "seec/Clang/MappedValue.hpp"
#include "seec/ICU/Augmenter.hpp"
#include "seec/Util/Range.hpp"

#include <string>


namespace clang {
  class Decl;
  class FunctionDecl;
  class Stmt;
  class VarDecl;
}

namespace llvm {
  class raw_ostream;
} // namespace llvm

namespace seec {

namespace seec_clang {
  class MappedAST;
  class MappedFunctionDecl;
} // namespace seec_clang (in seec)

namespace trace {
  class FunctionState;
} // namespace trace (in seec)

namespace util {
  class IndentationGuide;
} // namespace util (in seec)

// Documented in MappedProcessTrace.hpp
namespace cm {

class AllocaState;
class FunctionState;
class RuntimeErrorState;
class ThreadState;


/// \brief SeeC-Clang-mapped parameter state.
///
class ParamState {
  /// The function state that this parameter belongs to.
  FunctionState &Parent;
  
  /// The address of the parameter in memory.
  stateptr_ty Address;
  
  /// The mapped Decl.
  ::clang::VarDecl const *Decl;
  
public:
  /// \brief Constructor.
  ///
  ParamState(FunctionState &WithParent,
             stateptr_ty WithAddress,
             ::clang::VarDecl const *ForDecl)
  : Parent(WithParent),
    Address(WithAddress),
    Decl(ForDecl)
  {}
  
  // Allow moving.
  ParamState(ParamState &&) = default;
  ParamState &operator=(ParamState &&) = default;
  
  // No copying.
  ParamState(ParamState const &) = delete;
  ParamState &operator=(ParamState const &) = delete;
  
  
  /// \brief Print a textual description of the state.
  ///
  void print(llvm::raw_ostream &Out,
             seec::util::IndentationGuide &Indentation) const;
  
  
  /// \name Accessors.
  /// @{
  
  /// \brief Get the VarDecl for this alloca.
  ///
  ::clang::VarDecl const *getDecl() const { return Decl; }
  
  /// \brief Get the current mapped Value of this alloca.
  ///
  std::shared_ptr<Value const> getValue() const;
  
  /// @} (Accessors.)
};


/// \brief SeeC-Clang-mapped local state.
///
class LocalState {
  /// The function state that this local belongs to.
  FunctionState &Parent;
  
  /// The address of the variable in memory.
  stateptr_ty Address;
  
  /// The mapped Decl.
  ::clang::VarDecl const *Decl;
  
public:
  /// \brief Constructor.
  ///
  LocalState(FunctionState &WithParent,
             stateptr_ty WithAddress,
             ::clang::VarDecl const *ForDecl)
  : Parent(WithParent),
    Address(WithAddress),
    Decl(ForDecl)
  {}
  
  // Allow moving.
  LocalState(LocalState &&) = default;
  LocalState &operator=(LocalState &&) = default;
  
  // No copying.
  LocalState(LocalState const &) = delete;
  LocalState &operator=(LocalState const &) = delete;
  
  
  /// \brief Print a textual description of the state.
  ///
  void print(llvm::raw_ostream &Out,
             seec::util::IndentationGuide &Indentation) const;
  
  
  /// \name Accessors.
  /// @{
  
  /// \brief Get the VarDecl for this alloca.
  ///
  ::clang::VarDecl const *getDecl() const { return Decl; }
  
  /// \brief Get the current mapped Value of this alloca.
  ///
  std::shared_ptr<Value const> getValue() const;
  
  /// @} (Accessors.)
};


/// \brief SeeC-Clang-mapped function state.
///
class FunctionState {
  /// The thread that this function belongs to.
  ThreadState &Parent;
  
  /// The base (unmapped) state.
  seec::trace::FunctionState &UnmappedState;
  
  /// The mapping information for this function.
  seec::seec_clang::MappedFunctionDecl const *Mapping;
  
  /// The mapped parameters.
  std::vector<ParamState> Parameters;
  
  /// The mapped local variables.
  std::vector<LocalState> Variables;
  
  /// The mapped runtime errors.
  std::vector<RuntimeErrorState> RuntimeErrors;
  
public:
  /// \brief Constructor.
  ///
  FunctionState(ThreadState &WithParent,
                seec::trace::FunctionState &ForUnmappedState);
  
  /// \brief Destructor.
  ///
  ~FunctionState();
  
  /// \brief Move constructor.
  ///
  FunctionState(FunctionState &&) = default;
  
  /// \brief Move assignment.
  ///
  FunctionState &operator=(FunctionState &&) = default;
  
  // No copying.
  FunctionState(FunctionState const &) = delete;
  FunctionState &operator=(FunctionState const &) = delete;
  
  
  /// \brief Print a textual description of the state.
  ///
  void print(llvm::raw_ostream &Out,
             seec::util::IndentationGuide &Indentation,
             AugmentationCallbackFn Augmenter) const;
  
  
  /// \name Access underlying information.
  /// @{
  
  /// \brief Get the underlying (unmapped) state.
  ///
  seec::trace::FunctionState &getUnmappedState() { return UnmappedState; }
  
  /// \brief Get the underlying (unmapped) state.
  ///
  seec::trace::FunctionState const &getUnmappedState() const {
    return UnmappedState;
  }
  
  /// @} (Access underlying information.)
  
  
  /// \name Accessors.
  /// @{
  
  /// \brief Get the ThreadState that this FunctionState belongs to.
  ///
  ThreadState &getParent() { return Parent; }
  
  /// \brief Get the ThreadState that this FunctionState belongs to.
  ///
  ThreadState const &getParent() const { return Parent; }
  
  /// \brief Get the FunctionDecl for this function.
  ///
  ::clang::FunctionDecl const *getFunctionDecl() const;
  
  /// \brief Get a name for this function.
  ///
  std::string getNameAsString() const;
  
  /// \brief Get the mapped AST that this function belongs to.
  ///
  seec::seec_clang::MappedAST const *getMappedAST() const;
  
  /// @} (Accessors.)


  /// \name Decl execution.
  /// @{

  /// \brief Get the active Decl (if any).
  ///
  ::clang::Decl const *getActiveDecl() const;

  /// @}
  
  
  /// \name Stmt evaluation.
  /// @{
  
  /// \brief Get the active Stmt (if any).
  ///
  /// This Stmt may have just finished evaluating, or may be partially
  /// evaluated.
  ///
  ::clang::Stmt const *getActiveStmt() const;
  
  /// \brief Get the Value of a Stmt.
  ///
  /// This gets the Value resulting from the last evaluation of the given Stmt,
  /// if it has been evaluated (and the value is mapped).
  ///
  std::shared_ptr<Value const> getStmtValue(::clang::Stmt const *S) const;
  
  /// @} (Stmt evaluation.)
  
  
  /// \name Local variables.
  /// @{
  
  /// \brief Get the mapped parameters.
  std::vector<ParamState> const &getParameters() const {
    return Parameters;
  }
  
  /// \brief Get the mapped local variables.
  std::vector<LocalState> const &getLocals() const {
    return Variables;
  }
  
  /// @} (Local variables.)
  
  
  /// \name Runtime Errors.
  /// @{
  
  /// \brief Get all runtime errors.
  ///
  decltype(RuntimeErrors) const &getRuntimeErrors() const {
    return RuntimeErrors;
  }
  
  /// \brief Get active runtime errors.
  ///
  seec::Range<decltype(RuntimeErrors)::const_iterator>
  getRuntimeErrorsActive() const;
  
  /// @} (Runtime Errors.)
};


/// Print a textual description of a ThreadState.
llvm::raw_ostream &operator<<(llvm::raw_ostream &Out,
                              FunctionState const &State);


} // namespace cm (in seec)

} // namespace seec


#endif // SEEC_CLANG_MAPPEDFUNCTIONSTATE_HPP
