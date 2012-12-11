//===- lib/Runtimes/Tracer/WrapCstdio_h.cpp -------------------------------===//
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

#include "Tracer.hpp"

#include "seec/RuntimeErrors/FormatSelects.hpp"
#include "seec/Runtimes/MangleFunction.h"
#include "seec/Trace/DetectCalls.hpp"
#include "seec/Trace/ScanFormatSpecifiers.hpp"
#include "seec/Trace/TraceThreadListener.hpp"
#include "seec/Trace/TraceThreadMemCheck.hpp"
#include "seec/Util/ScopeExit.hpp"

#include "llvm/Support/CallSite.h"

#include <cctype>
#include <cstdio>


extern "C" {

//===----------------------------------------------------------------------===//
// scanf, fscanf, sscanf
//===----------------------------------------------------------------------===//

//
bool matchNonConversionCharacters(FILE *Stream,
                                  char const *&Start,
                                  char const *End = nullptr)
{
  while(*Start && (End == nullptr || Start < End)) {
    if (std::isspace(*Start)) {
      // Match all whitespace characters.
      int ReadChar;
      
      while ((ReadChar = std::fgetc(Stream)) != EOF) {
        if (!std::isspace(ReadChar)) {
          std::ungetc(ReadChar, Stream);
          break;
        }
      }
    }
    else {
      // Match a single character.
      int ReadChar = std::fgetc(Stream);
      if (ReadChar == EOF)
        return false;
      
      if (ReadChar != *Start) {
        std::ungetc(ReadChar, Stream);
        return false;
      }
    }
    
    ++Start;
  }
  
  return true;
}

// ParseInt

// ParseFloat

// ParseString

// ParseCharacterSet

static int
checkStreamScan(seec::runtime_errors::format_selects::CStdFunction FSFunction,
                unsigned VarArgsStartIndex,
                FILE *Stream,
                char const *Format)
{
  using namespace seec::trace;
  
  auto &ThreadEnv = getThreadEnvironment();
  auto &Listener = ThreadEnv.getThreadListener();
  auto Instruction = ThreadEnv.getInstruction();
  auto InstructionIndex = ThreadEnv.getInstructionIndex();
  auto Call = llvm::CallSite(Instruction);
  assert(Call && "expected call or invoke instruction.");
    
  // Interace with the thread listener's notification system.
  Listener.enterNotification();
  auto DoExit = seec::scopeExit([&](){ Listener.exitPostNotification(); });
  
  // Use a VarArgList to access our arguments.
  detect_calls::VarArgList<TraceThreadListener>
    VarArgs{Listener, Call, VarArgsStartIndex};

  // Lock IO streams and global memory.
  Listener.acquireGlobalMemoryWriteLock();
  auto StreamsAccessor = Listener.getProcessListener().getStreamsAccessor();
  
  // Use a CIOChecker to help check memory.
  CIOChecker Checker{Listener,
                     InstructionIndex,
                     FSFunction,
                     StreamsAccessor.getObject()};
  
  // Check that the stream is valid.
  if (Stream == stdin || Stream == stdout || Stream == stderr) {
    Checker.checkStandardStreamIsValid(Stream);
  }
  else {
    Checker.checkStreamIsValid(0, Stream);
  }
  
  // Check and perform the (f)scanf.
  auto FormatSize = Checker.checkCStringRead(VarArgsStartIndex - 1, Format);
  if (!FormatSize)
    return 0;
  
  int Result = 0;
  unsigned NextArg = 0;
  char const *NextChar = Format;
  
  while (true) {
    auto Conversion = ScanConversionSpecifier::readNextFrom(NextChar);
    if (!Conversion.Start) {
      // Attempt to match and consume remaining characters.
      matchNonConversionCharacters(Stream, NextChar);
      break;
    }
    
    // Attempt to match and consume [NextChar, Conversion.Start).
    if (!matchNonConversionCharacters(Stream, NextChar, Conversion.Start))
      break;
    
    auto const StartIndex = Conversion.Start - Format;
    
    // Ensure that the conversion specifier was parsed correctly.
    if (!Conversion.End) {
      Listener.handleRunError(
        seec::runtime_errors::createRunError
          <seec::runtime_errors::RunErrorType::FormatSpecifierParse>
          (FSFunction, VarArgsStartIndex - 1, StartIndex),
        RunErrorSeverity::Fatal,
        InstructionIndex);
      return false;
    }
    
    auto const EndIndex = Conversion.End - Format;
    
    // If a precision was specified, ensure that precision is allowed.
    if (Conversion.SuppressAssignment
        && !Conversion.allowedSuppressAssignment()) {
      Listener.handleRunError(
        seec::runtime_errors::createRunError
          <seec::runtime_errors::RunErrorType::FormatSpecifierSuppressionDenied>
          (FSFunction, VarArgsStartIndex - 1, StartIndex, EndIndex),
        RunErrorSeverity::Fatal,
        InstructionIndex);
      return false;
    }
    
    // Check that the argument type matches the expected type. Don't check that
    // the argument exists here, because some conversion specifiers don't
    // require an argument (i.e. %%), so we check if it exists when needed, in
    // the isArgumentTypeOK() implementation.
    if (!Conversion.isArgumentTypeOK(VarArgs, NextArg)) {
      Listener.handleRunError(
        seec::runtime_errors::createRunError
          <seec::runtime_errors::RunErrorType::FormatSpecifierArgType>
          (FSFunction,
           VarArgsStartIndex - 1,
           StartIndex,
           EndIndex,
           asCFormatLengthModifier(Conversion.Length),
           VarArgs.offset() + NextArg),
        RunErrorSeverity::Fatal,
        InstructionIndex);
      return false;
    }
    
    // If the argument type is a pointer, check that the destination is
    // writable. The conversion for strings (and sets) is a special case.
    if (Conversion.Conversion == ScanConversionSpecifier::Specifier::s
        || Conversion.Conversion == ScanConversionSpecifier::Specifier::set) {
      if (NextArg < VarArgs.size()) {
        // Check that the field width is specified.
        if (!Conversion.WidthSpecified) {
          Listener.handleRunError(
            seec::runtime_errors::createRunError
              <seec::runtime_errors::RunErrorType::FormatSpecifierWidthMissing>
              (FSFunction,
               VarArgsStartIndex - 1,
               StartIndex,
               EndIndex),
            RunErrorSeverity::Fatal,
            InstructionIndex);
          return false;
        }
        
        // Check that the destination is writable and has sufficient space.
        auto MaybePointeeArea = Conversion.getArgumentPointee(VarArgs, NextArg);
        if (!MaybePointeeArea.assigned()) {
          llvm_unreachable("unassigned pointee for string conversion.");
          return false;
        }
        
        // TODO: This width may not be correct for wchar_t *.
        if (!Checker.checkMemoryExistsAndAccessibleForParameter(
                VarArgs.offset() + NextArg,
                MaybePointeeArea.get<0>().address(),
                Conversion.Width,
                seec::runtime_errors::format_selects::MemoryAccess::Write))
          return false;
      }
    }
    else {
      auto MaybePointeeArea = Conversion.getArgumentPointee(VarArgs, NextArg);
      if (MaybePointeeArea.assigned()) {
        auto Area = MaybePointeeArea.get<0>();
        Checker.checkMemoryExistsAndAccessibleForParameter(
          VarArgs.offset() + NextArg,
          Area.address(),
          Area.length(),
          seec::runtime_errors::format_selects::MemoryAccess::Write);
      }
    }
    
    // Move to the next argument (unless this conversion specifier doesn't
    // consume an argument).
    if (Conversion.Conversion != ScanConversionSpecifier::Specifier::percent
        && Conversion.SuppressAssignment == false) {
      ++NextArg;
    }
    
    // The next position to search from should be the first character following
    // this conversion specifier.
    NextChar = Conversion.End;
  }
  
  // Record the produced value.
  Listener.notifyValue(InstructionIndex, Instruction, unsigned(Result));
  
  if (NextChar == Format && (std::feof(Stream) || std::ferror(Stream)))
    return EOF;
  
  return Result;
}

int
SEEC_MANGLE_FUNCTION(scanf)
(char const *Format, ...)
{
  return checkStreamScan
            (seec::runtime_errors::format_selects::CStdFunction::scanf,
             1,
             stdin,
             Format);
}

int
SEEC_MANGLE_FUNCTION(fscanf)
(FILE *Stream, char const *Format, ...)
{
  return checkStreamScan
            (seec::runtime_errors::format_selects::CStdFunction::fscanf,
             2,
             Stream,
             Format);
}

int
SEEC_MANGLE_FUNCTION(sscanf)
(char const *Buffer, char const *Format, ...)
{
  auto &ThreadEnv = seec::trace::getThreadEnvironment();
  auto &Listener = ThreadEnv.getThreadListener();
  auto Call = llvm::CallSite(ThreadEnv.getInstruction());
  assert(Call && "expected call or invoke instruction.");
    
  // TODO: Check and do.
  
  // Do.
  llvm_unreachable("sscanf: not implemented.");
  
  // Record.
  
  // Return.
  return 0;
}

} // extern "C"