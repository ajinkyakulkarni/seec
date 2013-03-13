//===- include/seec/Trace/TraceThreadListener.hpp ------------------- C++ -===//
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

#ifndef SEEC_TRACE_TRACETHREADLISTENER_HPP
#define SEEC_TRACE_TRACETHREADLISTENER_HPP

#include "seec/DSA/MemoryBlock.hpp"
#include "seec/RuntimeErrors/RuntimeErrors.hpp"
#include "seec/Trace/DetectCalls.hpp"
#include "seec/Trace/RuntimeValue.hpp"
#include "seec/Trace/TracedFunction.hpp"
#include "seec/Trace/TraceEventWriter.hpp"
#include "seec/Trace/TraceFormat.hpp"
#include "seec/Trace/TraceProcessListener.hpp"
#include "seec/Trace/TraceStorage.hpp"
#include "seec/Util/Maybe.hpp"
#include "seec/Util/ModuleIndex.hpp"
#include "seec/Util/Serialization.hpp"
#include "seec/Util/SynchronizedExit.hpp"

#include "llvm/ADT/ArrayRef.h"

// #include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <thread>
#include <memory>
#include <vector>


namespace llvm {
  class AllocaInst;
  class BinaryOperator;
  class Function;
  class CallInst;
  class LoadInst;
  class StoreInst;
  class Instruction;
  struct GenericValue;
} // namespace llvm


namespace seec {

namespace trace {


/// Describes the severity of a detected run-time error.
enum class RunErrorSeverity {
  Warning,
  Fatal
};


/// \brief Records thread-specific execution events.
///
///
class TraceThreadListener
: public seec::trace::CallDetector<TraceThreadListener>
{
  friend class CStdLibChecker;
  
  // Don't allow copying.
  TraceThreadListener(TraceThreadListener const &) = delete;
  TraceThreadListener &operator=(TraceThreadListener const &) = delete;

  /// \name Constant information.
  /// @{

  /// The process listener for this process.
  TraceProcessListener &ProcessListener;

  /// Support this thread's participation in synchronized exits.
  SupportSynchronizedExit const SupportSyncExit;

  /// The unique integer identifier for this thread.
  uint32_t const ThreadID;

  /// @} (Constant information)


  /// \name Outputs
  /// @{
  
  /// Allocates output streams.
  OutputStreamAllocator &StreamAllocator;
  
  /// Controls trace file output.
  bool OutputEnabled;
  
  /// Handles writing event data.
  EventWriter EventsOut;

  /// @} (Outputs)


  /// The synthetic ``thread time'' for this thread.
  uint64_t Time;

  /// This thread's view of the synthetic ``process time'' for this process.
  uint64_t ProcessTime;

  /// List of all traced Functions, in order.
  std::vector<std::unique_ptr<TracedFunction>> RecordedFunctions;

  /// Offset of all top-level traced Functions.
  std::vector<offset_uint> RecordedTopLevelFunctions;

  /// Stack of trace information for still-executing Functions.
  /// The back of the vector is the currently active Function.
  std::vector<TracedFunction *> FunctionStack;

  /// Controls access to FunctionStack.
  mutable std::mutex FunctionStackMutex;

  /// Pointer to the trace information for the currently active Function, or
  /// nullptr if no Function is currently active.
  // std::atomic<TracedFunction *> ActiveFunction;
  TracedFunction *ActiveFunction;

  /// Global memory lock owned by this thread.
  std::unique_lock<std::mutex> GlobalMemoryLock;

  /// Dynamic memory lock owned by this thread.
  std::unique_lock<std::mutex> DynamicMemoryLock;
  
  /// I/O streams lock owned by this thread.
  std::unique_lock<std::mutex> StreamsLock;


  /// \name Current instruction information.
  /// @{
  
  /// Holds the new ProcessTime generated by the current Instruction, if any.
  seec::util::Maybe<uint64_t> CIProcessTime;
  
  /// Get the new ProcessTime generated by the current Instruction.
  uint64_t getCIProcessTime() {
    if (CIProcessTime.assigned())
      return CIProcessTime.get<0>();
    
    CIProcessTime = ProcessListener.getNewTime();
    return CIProcessTime.get<0>();
  }
  
  /// Clear current Instruction information.
  void clearCI() {
    CIProcessTime.reset();
    
    if (ActiveFunction)
      ActiveFunction->clearActiveInstruction();
  }
  
  /// @} (Current instruction information)
  
  
  /// \name Helper methods
  /// @{

  /// \brief Get the offset that a new FunctionRecord would be placed at.
  offset_uint getNewFunctionRecordOffset() {
    constexpr size_t SizeOfFunctionRecord
      = sizeof(uint32_t)        // index
      + (2*sizeof(offset_uint)) // event start, end
      + (2*sizeof(uint64_t))    // thread entered, exited
      + (1*sizeof(offset_uint)) // child list
      ;

    return sizeof(offset_uint) // For top-level function list offset
      + (RecordedFunctions.size() * SizeOfFunctionRecord); // For functions
  }

  /// \brief Synchronize this thread's view of the synthetic process time.
  void synchronizeProcessTime();

public:  
  /// \brief Acquire the StreamsLock, if we don't have it already.
  void acquireStreamsLock() {
    if (!StreamsLock) {
      StreamsLock = ProcessListener.getStreamsLock();
    }
  }

  /// @} (Helper methods)


public:
  /// \name Dynamic memory
  /// @{
  
  /// \brief Acquire the DynamicMemoryLock, if we don't have it already.
  void acquireDynamicMemoryLock() {
    if (!DynamicMemoryLock) {
      DynamicMemoryLock = ProcessListener.lockDynamicMemory();
    }
  }

  /// \brief Write a malloc record and update the process' dynamic memory.
  void recordMalloc(uintptr_t Address, std::size_t Size);

  /// \brief Write a free record and update the process' dynamic memory.
  /// \return the DynamicAllocation that was freed.
  DynamicAllocation recordFree(uintptr_t Address);

  /// \brief Write a free record and update the process' dynamic memory. Clears
  ///        the freed area of memory.
  void recordFreeAndClear(uintptr_t Address);

  /// @} (Dynamic memory)


public:
  /// \name Memory states
  /// @{
  
  /// \brief Acquire the GlobalMemoryLock, if we don't have it already.
  /// At the moment, this is identical to acquireGlobalMemoryReadLock(), but we
  /// may change to a multiple readers / single writer design in the future.
  void acquireGlobalMemoryWriteLock() {
    if (!GlobalMemoryLock) {
      GlobalMemoryLock = ProcessListener.lockMemory();
    }
  }
  
  /// \brief Acquire the GlobalMemoryLock, if we don't have it already.
  /// At the moment, this is identical to acquireGlobalMemoryWriteLock(), but we
  /// may change to a multiple readers / single writer design in the future.
  void acquireGlobalMemoryReadLock() {
    if (!GlobalMemoryLock) {
      GlobalMemoryLock = ProcessListener.lockMemory();
    }
  }

  /// \brief Write a set of overwritten states.
  void writeStateOverwritten(OverwrittenMemoryInfo const &Info);

  /// \brief Record an untyped update to memory.
  void recordUntypedState(char const *Data, std::size_t Size);

  /// \brief Record a typed update to memory.
  void recordTypedState(void const *Data, std::size_t Size, offset_uint Value);

  /// \brief Record a clear to a memory region.
  void recordStateClear(uintptr_t Address, std::size_t Size);

  /// \brief Unimplemented.
  void recordMemset();

  /// \brief Record a memmove (or memcpy) update to memory.
  void recordMemmove(uintptr_t Source, uintptr_t Destination, std::size_t Size);

  /// \brief Add a region of known, but unowned, memory.
  void addKnownMemoryRegion(uintptr_t Address,
                            std::size_t Length,
                            MemoryPermission Access);
  
  /// \brief Remove the region of known memory starting at Address.
  bool removeKnownMemoryRegion(uintptr_t Address);

  /// @} (Memory states)


public:
  /// Constructor.
  TraceThreadListener(TraceProcessListener &ProcessListener,
                      OutputStreamAllocator &StreamAllocator)
  : seec::trace::CallDetector<TraceThreadListener>
                             (ProcessListener.getDetectCallsLookup()),
    ProcessListener(ProcessListener),
    SupportSyncExit(ProcessListener.syncExit()),
    ThreadID(ProcessListener.registerThreadListener(this)),
    StreamAllocator(StreamAllocator),
    OutputEnabled(false),
    EventsOut(),
    Time(0),
    ProcessTime(0),
    RecordedFunctions(),
    RecordedTopLevelFunctions(),
    FunctionStack(),
    FunctionStackMutex(),
    ActiveFunction(nullptr),
    GlobalMemoryLock(),
    DynamicMemoryLock(),
    StreamsLock()
  {
    traceOpen();
  }

  /// Destructor.
  ~TraceThreadListener() {
    traceWrite();
    traceFlush();
    traceClose();
    
    ProcessListener.deregisterThreadListener(ThreadID);
  }
  
  
  /// \name Trace writing control.
  /// @{
  
  /// \brief Check if tracing is enabled.
  ///
  bool traceEnabled() const { return OutputEnabled; }
  
  /// \brief Write out complete trace information.
  ///
  void traceWrite();
  
  /// \brief Flush all open trace streams.
  ///
  void traceFlush();
  
  /// \brief Close all open trace streams and disable future writes.
  ///
  void traceClose();
  
  /// \brief Open all used trace streams and enable future writes.
  ///
  void traceOpen();
  
  /// @} (Trace writing control.)


  /// \name Accessors
  /// @{

  /// \brief Get the TraceProcessListener for the process that this thread
  ///        belongs to.
  ///
  TraceProcessListener const &getProcessListener() const {
    return ProcessListener;
  }
  
  /// \brief Access the synchronized exit supporter for this thread.
  ///
  SupportSynchronizedExit const &getSupportSynchronizedExit() {
    return SupportSyncExit;
  }

  /// \brief Get the unique ThreadID for this thread.
  ///
  uint32_t getThreadID() const { return ThreadID; }
  
  /// \brief Get access to the event output.
  ///
  EventWriter &getEventsOut() { return EventsOut; }

  /// \brief Get the run-time address of a GlobalVariable.
  /// \param GV the GlobalVariable.
  /// \return the run-time address of GV, or 0 if it is not known.
  ///
  uintptr_t getRuntimeAddress(llvm::GlobalVariable const *GV) const {
    return ProcessListener.getRuntimeAddress(GV);
  }

  /// \brief Get the run-time address of a Function.
  /// \param F the Function.
  /// \return the run-time address of F, or 0 if it is not known.
  ///
  uintptr_t getRuntimeAddress(llvm::Function const *F) const {
    return ProcessListener.getRuntimeAddress(F);
  }

  /// \brief Get trace information about the currently active Function.
  /// \return a pointer to the current active TracedFunction, or nullptr
  ///         if no Function is currently active.
  ///
  TracedFunction *getActiveFunction() {
    // return ActiveFunction.load();
    return ActiveFunction;
  }

  /// \brief Get trace information about the currently active Function.
  /// \return a const pointer to the current active TracedFunction, or nullptr
  ///         if no Function is currently active.
  ///
  TracedFunction const *getActiveFunction() const {
    // return ActiveFunction.load();
    return ActiveFunction;
  }

  /// \brief Get the current RuntimeValue associated with an Instruction.
  ///
  RuntimeValue const *getCurrentRuntimeValue(llvm::Instruction const *I) const {
    // auto ActiveFunc = ActiveFunction.load();
    auto ActiveFunc = ActiveFunction;

    if (!ActiveFunc)
      return nullptr;

    auto &FIndex = ActiveFunc->getFunctionIndex();

    auto MaybeIndex = FIndex.getIndexOfInstruction(I);
    if (!MaybeIndex.assigned())
      return nullptr;

    return ActiveFunc->getCurrentRuntimeValue(MaybeIndex.get<0>());
  }

  /// \brief Find the allocated range that owns an address, if it belongs to this
  ///        thread. This method is thread safe.
  ///
  seec::util::Maybe<MemoryArea>
  getContainingMemoryArea(uintptr_t Address) const {
    std::lock_guard<std::mutex> Lock(FunctionStackMutex);

    seec::util::Maybe<MemoryArea> Area;

    for (auto TracedFunc : FunctionStack) {
      Area = TracedFunc->getContainingMemoryArea(Address);
      if (Area.assigned()) {
        return Area;
      }
    }

    return Area;
  }

  /// @} (Accessors)


  /// \name Mutators
  /// @{
  
  /// \brief Increment the thread time and write a NewThreadTime event.
  ///
  /// This will also reset the process time associated with the current
  /// instruction.
  ///
  uint64_t incrementThreadTime() {
    EventsOut.write<seec::trace::EventType::NewThreadTime>(++Time);
    
    CIProcessTime.reset();
    
    return Time;
  }

  /// \brief Handle a run-time error.
  /// At this time, a run-time error is handled by writing it to the trace, and
  /// then terminating execution with a synchronized exit.
  /// \param Error the run-time error.
  /// \param Severity the severity of the error.
  /// \param PreInstructionIndex the index of the Instruction that would cause
  ///                            this error.
  void handleRunError(seec::runtime_errors::RunError const &Error,
                      RunErrorSeverity Severity,
                      seec::util::Maybe<uint32_t> PreInstructionIndex
                        = seec::util::Maybe<uint32_t>());

  /// @} (Mutators)


  /// \name Thread Listener Notifications
  /// @{

  void enterNotification();

  void exitNotification();

  void exitPreNotification();

  void exitPostNotification();

  void notifyFunctionBegin(uint32_t Index, llvm::Function const *F);

  void notifyFunctionEnd(uint32_t Index, llvm::Function const *F);
  
  /// \brief Notify of a byval argument.
  void notifyArgumentByVal(uint32_t Index, llvm::Argument const *Arg,
                           void const *Address);
  
  /// \brief Receive the contents of argc and argv.
  void notifyArgs(uint64_t ArgC, char **ArgV);
  
  /// \brief Receive the contents of envp.
  void notifyEnv(char **EnvP);

  void notifyPreCall(uint32_t Index, llvm::CallInst const *Call,
                     void const *Address);

  void notifyPostCall(uint32_t Index, llvm::CallInst const *Call,
                      void const *Address);

  void notifyPreCallIntrinsic(uint32_t Index, llvm::CallInst const *Call);

  void notifyPostCallIntrinsic(uint32_t Index, llvm::CallInst const *Call);

  void notifyPreLoad(uint32_t Index,
                     llvm::LoadInst const *Load,
                     void const *Address,
                     std::size_t Size);

  void notifyPostLoad(uint32_t Index,
                      llvm::LoadInst const *Load,
                      void const *Address,
                      std::size_t Size);

  void notifyPreStore(uint32_t Index,
                      llvm::StoreInst const *Store,
                      void const *Address,
                      std::size_t Size);

  void notifyPostStore(uint32_t Index,
                       llvm::StoreInst const *Store,
                       void const *Address,
                       std::size_t Size);

  void notifyPreDivide(uint32_t Index,
                       llvm::BinaryOperator const *Instruction);

  void notifyValue(uint32_t Index,
                   llvm::Instruction const *Instruction,
                   void *Value);

  void notifyValue(uint32_t Index,
                   llvm::Instruction const *Instruction,
                   uint64_t Value);

  void notifyValue(uint32_t Index,
                   llvm::Instruction const *Instruction,
                   uint32_t Value);

  void notifyValue(uint32_t Index,
                   llvm::Instruction const *Instruction,
                   uint16_t Value);

  void notifyValue(uint32_t Index,
                   llvm::Instruction const *Instruction,
                   uint8_t Value);

  void notifyValue(uint32_t Index,
                   llvm::Instruction const *Instruction,
                   float Value);

  void notifyValue(uint32_t Index,
                   llvm::Instruction const *Instruction,
                   double Value);
  
  void notifyValue(uint32_t Index,
                   llvm::Instruction const *Instruction,
                   long double Value);

  /// @} (Thread Listener Notifications)


  /// \name Detect Calls - stdio.h file access
  /// @{
  
  // fopen
  void preCfopen(llvm::CallInst const *Call, uint32_t Index,
                 char const *Filename, char const *Mode);
  void postCfopen(llvm::CallInst const *Call, uint32_t Index,
                  char const *Filename, char const *Mode);
  
  // freopen
  void preCfreopen(llvm::CallInst const *Call, uint32_t Index,
                   char const *Filename, char const *Mode, FILE *Stream);
  void postCfreopen(llvm::CallInst const *Call, uint32_t Index,
                    char const *Filename, char const *Mode, FILE *Stream);
  
  // fclose
  void preCfclose(llvm::CallInst const *Call, uint32_t Index, FILE *Stream);
  void postCfclose(llvm::CallInst const *Call, uint32_t Index, FILE *Stream);
  
  // fflush
  void preCfflush(llvm::CallInst const *Call, uint32_t Index, FILE *Stream);
  
  // fwide
  void preCfwide(llvm::CallInst const *Call, uint32_t Index, FILE *Stream,
                 int Mode);
  
  /// @}
  
  
  /// \name Detect Calls - stdio.h direct input/output.
  /// @{
  
  /// @}
  
  
  /// \name Detect Calls - stdio.h unformatted input/output.
  /// @{
  
  // fgetc
  void preCfgetc(llvm::CallInst const *Call, uint32_t Index, FILE *Stream);
  
  // fgets
  void preCfgets(llvm::CallInst const *Call, uint32_t Index, char *Str,
                 int Count, FILE *Stream);
  void postCfgets(llvm::CallInst const *Call, uint32_t Index, char *Str,
                  int Count, FILE *Stream);
  
  // fputc
  void preCfputc(llvm::CallInst const *Call, uint32_t Index, int Ch,
                 FILE *Stream);
  
  // fputs
  void preCfputs(llvm::CallInst const *Call, uint32_t Index, char const *Str,
                 FILE *Stream);
  
  // getchar
  void preCgetchar(llvm::CallInst const *Call, uint32_t Index);
  
  // gets - should never be used.
  
  // putchar
  void preCputchar(llvm::CallInst const *Call, uint32_t Index, int Ch);
  
  // puts
  void preCputs(llvm::CallInst const *Call, uint32_t Index, char const *Str);
  
  // ungetc
  void preCungetc(llvm::CallInst const *Call, uint32_t Index, int Ch,
                  FILE *Stream);
  
  /// @}
  
  
  /// \name Detect Calls - stdio.h formatted input/output.
  /// @{
  
  // printf
  void preCprintf(llvm::CallInst const *Call, uint32_t Index, char const *Str,
                  detect_calls::VarArgList<TraceThreadListener> const &Args);
  
  // fprintf
  void preCfprintf(llvm::CallInst const *Call, uint32_t Index, FILE *Out,
                   char const *Str,
                   detect_calls::VarArgList<TraceThreadListener> const &Args);
  
  // sprintf
  void preCsprintf(llvm::CallInst const *Call, uint32_t Index, char *Buffer,
                   char const *Str,
                   detect_calls::VarArgList<TraceThreadListener> const &Args);
  void postCsprintf(llvm::CallInst const *Call, uint32_t Index, char *Buffer,
                    char const *Str,
                    detect_calls::VarArgList<TraceThreadListener> const &Args);
  
  // snprintf
  void preCsnprintf(llvm::CallInst const *Call, uint32_t Index, char *Buffer,
                    std::size_t BufSize, char const *Str,
                    detect_calls::VarArgList<TraceThreadListener> const &Args);
  void postCsnprintf(llvm::CallInst const *Call, uint32_t Index, char *Buffer,
                     std::size_t BufSize, char const *Str,
                     detect_calls::VarArgList<TraceThreadListener> const &Args);
  
  /// @}
  
  
  /// \name Detect Calls - stdlib.h string
  /// @{
  
  void preCatof(llvm::CallInst const *Call, uint32_t Index, char const *Str);
  
  void preCatoi(llvm::CallInst const *Call, uint32_t Index, char const *Str);
  void preCatol(llvm::CallInst const *Call, uint32_t Index, char const *Str);
  void preCatoll(llvm::CallInst const *Call, uint32_t Index, char const *Str);
  
  // strtol
  void preCstrtol(llvm::CallInst const *Call, uint32_t Index, char const *Str,
                  char **EndPtr, int Base);
  void postCstrtol(llvm::CallInst const *Call, uint32_t Index, char const *Str,
                   char **EndPtr, int Base);
  
  // strtoll
  void preCstrtoll(llvm::CallInst const *Call, uint32_t Index, char const *Str,
                   char **EndPtr, int Base);
  void postCstrtoll(llvm::CallInst const *Call, uint32_t Index, char const *Str,
                    char **EndPtr, int Base);

  // strtoul
  void preCstrtoul(llvm::CallInst const *Call, uint32_t Index, char const *Str,
                   char **EndPtr, int Base);
  void postCstrtoul(llvm::CallInst const *Call, uint32_t Index, char const *Str,
                    char **EndPtr, int Base);
  
  // strtoull
  void preCstrtoull(llvm::CallInst const *Call, uint32_t Index, char const *Str,
                    char **EndPtr, int Base);
  void postCstrtoull(llvm::CallInst const *Call, uint32_t Index, char const *Str,
                     char **EndPtr, int Base);

  // strtof
  void preCstrtof(llvm::CallInst const *Call, uint32_t Index, char const *Str,
                  char **EndPtr);
  void postCstrtof(llvm::CallInst const *Call, uint32_t Index, char const *Str,
                   char **EndPtr);
  
  // strtod
  void preCstrtod(llvm::CallInst const *Call, uint32_t Index, char const *Str,
                  char **EndPtr);
  void postCstrtod(llvm::CallInst const *Call, uint32_t Index, char const *Str,
                   char **EndPtr);
  
  // strtold
  void preCstrtold(llvm::CallInst const *Call, uint32_t Index, char const *Str,
                   char **EndPtr);
  void postCstrtold(llvm::CallInst const *Call, uint32_t Index, char const *Str,
                    char **EndPtr);

  // strtoimax
  void preCstrtoimax(llvm::CallInst const *Call, uint32_t Index,
                     char const *Str, char **EndPtr);
  void postCstrtoimax(llvm::CallInst const *Call, uint32_t Index,
                      char const *Str, char **EndPtr);
  
  // strtoumax
  void preCstrtoumax(llvm::CallInst const *Call, uint32_t Index,
                     char const *Str, char **EndPtr);
  void postCstrtoumax(llvm::CallInst const *Call, uint32_t Index,
                      char const *Str, char **EndPtr);

  /// @}
  
  
  /// \name Detect Calls - stdlib.h memory
  /// @{
  
  void preCcalloc(llvm::CallInst const *Call, uint32_t Index, size_t Num,
                  size_t Size);

  void postCcalloc(llvm::CallInst const *Call, uint32_t Index, size_t Num,
                   size_t Size);

  void preCfree(llvm::CallInst const *Call, uint32_t Index, void *Address);

  void postCfree(llvm::CallInst const *Call, uint32_t Index, void *Address);

  void preCmalloc(llvm::CallInst const *Call, uint32_t Index, size_t Size);

  void postCmalloc(llvm::CallInst const *Call, uint32_t Index, size_t Size);

  void preCrealloc(llvm::CallInst const *Call, uint32_t Index, void *Address,
                   size_t Size);

  void postCrealloc(llvm::CallInst const *Call, uint32_t Index, void *Address,
                    size_t Size);
  
  /// @}
  
  
  /// \name Detect Calls - stdlib.h environment
  /// @{
  
  void preCgetenv(llvm::CallInst const *Call, uint32_t Index,
                  char const *Name);
  
  void postCgetenv(llvm::CallInst const *Call, uint32_t Index,
                   char const *Name);
  
  void preCsystem(llvm::CallInst const *Call, uint32_t Index,
                  char const *Command);
  
  /// @}
  
  
  /// \name Detect Calls - string.h
  /// @{

  void preCmemchr(llvm::CallInst const *Call, uint32_t Index,
                  void const *Ptr, int Value, size_t Num);

  void preCmemcmp(llvm::CallInst const *Call, uint32_t Index,
                  void const *Address1, void const *Address2, size_t Size);

  void postCmemcmp(llvm::CallInst const *Call, uint32_t Index,
                   void const *Address1, void const *Address2, size_t Size);

  void preCmemcpy(llvm::CallInst const *Call, uint32_t Index,
                  void *Destination, void const *Source, size_t Size);

  void postCmemcpy(llvm::CallInst const *Call, uint32_t Index,
                   void *Destination, void const *Source, size_t Size);

  void preCmemmove(llvm::CallInst const *Call, uint32_t Index,
                   void *Destination, void const *Source, size_t Size);

  void postCmemmove(llvm::CallInst const *Call, uint32_t Index,
                    void *Destination, void const *Source, size_t Size);

  void preCmemset(llvm::CallInst const *Call, uint32_t Index,
                  void *Destination, int Value, size_t Size);

  void postCmemset(llvm::CallInst const *Call, uint32_t Index,
                   void *Destination, int Value, size_t Size);

  void preCstrcat(llvm::CallInst const *Call, uint32_t Index,
                  char *Destination, char const *Source);

  void postCstrcat(llvm::CallInst const *Call, uint32_t Index,
                   char *Destination, char const *Source);

  void preCstrchr(llvm::CallInst const *Call, uint32_t Index,
                  char const *Str, int Character);

  void preCstrcmp(llvm::CallInst const *Call, uint32_t Index,
                  char const *Str1, char const *Str2);

  void preCstrcoll(llvm::CallInst const *Call, uint32_t Index,
                   char const *Str1, char const *Str2);

  void preCstrcpy(llvm::CallInst const *Call, uint32_t Index,
                  char *Destination, char const *Source);

  void postCstrcpy(llvm::CallInst const *Call, uint32_t Index,
                   char *Destination, char const *Source);

  void preCstrcspn(llvm::CallInst const *Call, uint32_t Index,
                   char const *Str1, char const *Str2);

  void preCstrerror(llvm::CallInst const *Call, uint32_t Index, int Errnum);
  
  void postCstrerror(llvm::CallInst const *Call, uint32_t Index, int Errnum);

  void preCstrlen(llvm::CallInst const *Call, uint32_t Index,
                  char const *Str);

  void preCstrncat(llvm::CallInst const *Call, uint32_t Index,
                   char *Destination, char const *Source, size_t Size);

  void postCstrncat(llvm::CallInst const *Call, uint32_t Index,
                    char *Destination, char const *Source, size_t Size);

  void preCstrncmp(llvm::CallInst const *Call, uint32_t Index,
                   char const *Str1, char const *Str2, size_t Num);

  void preCstrncpy(llvm::CallInst const *Call, uint32_t Index,
                   char *Destination, char const *Source, size_t Size);

  void postCstrncpy(llvm::CallInst const *Call, uint32_t Index,
                    char *Destination, char const *Source, size_t Size);

  void preCstrpbrk(llvm::CallInst const *Call, uint32_t Index,
                   char const *Str1, char const *Str2);

  void preCstrrchr(llvm::CallInst const *Call, uint32_t Index,
                   char const *Str, int Character);

  void preCstrspn(llvm::CallInst const *Call, uint32_t Index,
                  char const *Str1, char const *Str2);

  void preCstrstr(llvm::CallInst const *Call, uint32_t Index,
                  char const *Str1, char const *Str2);

  void preCstrxfrm(llvm::CallInst const *Call, uint32_t Index,
                   char *Destination, char const *Source, size_t Num);
  
  void postCstrxfrm(llvm::CallInst const *Call, uint32_t Index,
                    char *Destination, char const *Source, size_t Num);
  
  /// @}
};


} // namespace trace (in seec)

} // namespace seec

#endif // SEEC_TRACE_TRACETHREADLISTENER_HPP
