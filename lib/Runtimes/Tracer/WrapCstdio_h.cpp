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

#include "SimpleWrapper.hpp"
#include "Tracer.hpp"

#include "seec/RuntimeErrors/FormatSelects.hpp"
#include "seec/Runtimes/MangleFunction.h"
#include "seec/Trace/DetectCalls.hpp"
#include "seec/Trace/ScanFormatSpecifiers.hpp"
#include "seec/Trace/TraceThreadListener.hpp"
#include "seec/Trace/TraceThreadMemCheck.hpp"
#include "seec/Util/Fallthrough.hpp"
#include "seec/Util/ScopeExit.hpp"

#include "llvm/Support/CallSite.h"

#include <cctype>
#include <cinttypes>
#include <cstdio>


//===----------------------------------------------------------------------===//
// scanf, fscanf, sscanf common methods
//===----------------------------------------------------------------------===//

/// \brief Attempt to match a sequence of literal characters.
///
bool matchNonConversionCharacters(int &CharactersRead,
                                  FILE *Stream,
                                  char const *Start,
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
        
        ++CharactersRead;
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
      
      ++CharactersRead;
    }
    
    ++Start;
  }
  
  return true;
}

/// \brief Attempt to match an integer from a stream.
///
bool parseInt(int &CharactersRead,
              FILE *Stream,
              seec::trace::ScanConversionSpecifier const &Conversion,
              std::uintmax_t &Output)
{
  using namespace seec::trace;
  
  char Buffer[64];
  auto const BufferSize = sizeof(Buffer);
  
  auto Width = Conversion.Width;
  if (Width == 0 || Width > BufferSize - 1)
    Width = BufferSize - 1;
  
  bool Unsigned     = false;
  bool HexPrefixOK  = false;
  
  int Base = 0;
  
  switch (Conversion.Conversion) {
    case ScanConversionSpecifier::Specifier::d:
      Base = 10;
      break;
    case ScanConversionSpecifier::Specifier::i:
      break;
    case ScanConversionSpecifier::Specifier::o:
      Base = 8;
      Unsigned = true;
      break;
    case ScanConversionSpecifier::Specifier::u:
      Base = 10;
      Unsigned = true;
      break;
    case ScanConversionSpecifier::Specifier::x:
      Base = 16;
      Unsigned = true;
      break;
    default:
      llvm_unreachable("invalid conversion specifier for parseInt()");
      return false;
  }
  
  char *BufferPtr = Buffer;
  
  for (; Width != 0; --Width) {
    int ReadChar = std::fgetc(Stream);
    if (ReadChar == EOF) {
      if (std::ferror(Stream))
        return false;
      break;
    }
    
    bool ReadOK = true;
    
    switch (ReadChar) {
      // OK always. For %i, this sets the base to octal, unless it is followed
      // by 'x' or 'X', which will set the base to hexadecimal (we take care
      // of that when encountering 'x' or 'X').
      case '0':
        if (Base == 0) {
          Base = 8;
          HexPrefixOK = true;
        }
        break;
      
      // OK always.
      case '1': SEEC_FALLTHROUGH;
      case '2': SEEC_FALLTHROUGH;
      case '3': SEEC_FALLTHROUGH;
      case '4': SEEC_FALLTHROUGH;
      case '5': SEEC_FALLTHROUGH;
      case '6': SEEC_FALLTHROUGH;
      case '7':
        if (Base == 0)
          Base = 10;
        break;
      
      // OK iff decimal or hexadecimal.
      case '8': SEEC_FALLTHROUGH;
      case '9':
        if (Base == 0)
          Base = 10;
        else if (Base < 10)
          ReadOK = false;
        break;
      
      // OK iff hexadecimal.
      case 'A': SEEC_FALLTHROUGH;
      case 'a': SEEC_FALLTHROUGH;
      case 'B': SEEC_FALLTHROUGH;
      case 'b': SEEC_FALLTHROUGH;
      case 'C': SEEC_FALLTHROUGH;
      case 'c': SEEC_FALLTHROUGH;
      case 'D': SEEC_FALLTHROUGH;
      case 'd': SEEC_FALLTHROUGH;
      case 'E': SEEC_FALLTHROUGH;
      case 'e': SEEC_FALLTHROUGH;
      case 'F': SEEC_FALLTHROUGH;
      case 'f':
        if (Base < 16)
          ReadOK = false;
        break;
      
      // OK as first character.
      case '+': SEEC_FALLTHROUGH;
      case '-':
        if (BufferPtr != Buffer)
          ReadOK = false;
        break;
      
      // OK if part of the prefix "0x" or "0X".
      case 'X': SEEC_FALLTHROUGH;
      case 'x':
        if (HexPrefixOK) {
          Base = 16;
          HexPrefixOK = false;
        }
        else {
          ReadOK = false;
        }
        break;
      
      // Any other character is always invalid.
      default:
        ReadOK = false;
    }
    
    if (ReadOK) {
      *BufferPtr++ = static_cast<char>(ReadChar);
      ++CharactersRead;
    }
    else {
      // Push the character back into the stream.
      std::ungetc(ReadChar, Stream);
      break;
    }
  }
  
  // Short-circuit failure.
  if (BufferPtr == Buffer)
    return false;
  
  *BufferPtr = '\0';
  
  // Read the integer.
  char *ParseEnd = nullptr;
  
  if (Unsigned)
    Output = std::strtoumax(Buffer, &ParseEnd, Base);
  else
    Output = std::strtoimax(Buffer, &ParseEnd, Base);
  
  // Push unused characters back into the stream.
  if (ParseEnd != BufferPtr) {
    // Teach Clang's static analyzer that the value of ParseEnd following the
    // call to strtoumax or strtoimax will always be within the buffer range.
    assert(ParseEnd >= Buffer && ParseEnd < BufferPtr
           && "ParseEnd is not in buffer!");
    
    for (--BufferPtr; BufferPtr > ParseEnd; --BufferPtr) {
      std::ungetc(*BufferPtr, Stream);
    }
  }
  
  return (ParseEnd != Buffer);
}

/// \brief Implement checked scanf and fscanf.
///
static int
checkStreamScan(seec::runtime_errors::format_selects::CStdFunction FSFunction,
                unsigned VarArgsStartIndex,
                FILE *Stream,
                char const *Format)
{
  using namespace seec::trace;
  using namespace seec::runtime_errors;
  
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
  if (!FormatSize) {
    return 0;
  }
  
  int NumConversions = 0;
  int NumAssignments = 0;
  int NumCharsRead = 0;
  unsigned NextArg = 0;
  char const *NextChar = Format;
  bool InputFailure = false;
  
  while (true) {
    auto Conversion = ScanConversionSpecifier::readNextFrom(NextChar);
    if (!Conversion.Start) {
      // Attempt to match and consume remaining characters.
      matchNonConversionCharacters(NumCharsRead, Stream, NextChar);
      break;
    }
    
    // Attempt to match and consume [NextChar, Conversion.Start).
    if (!matchNonConversionCharacters(NumCharsRead, Stream, NextChar,
                                      Conversion.Start))
      break;
    
    auto const StartIndex = Conversion.Start - Format;
    
    // Ensure that the conversion specifier was parsed correctly.
    if (!Conversion.End) {
      Listener.handleRunError(
        *createRunError<RunErrorType::FormatSpecifierParse>
                       (FSFunction, VarArgsStartIndex - 1, StartIndex),
        RunErrorSeverity::Fatal,
        InstructionIndex);
      return NumAssignments;
    }
    
    auto const EndIndex = Conversion.End - Format;
    
    // If assignment was suppressed, ensure that suppressing assignment is OK.
    if (Conversion.SuppressAssignment) {
      if (!Conversion.allowedSuppressAssignment()) {
        Listener.handleRunError(
          *createRunError<RunErrorType::FormatSpecifierSuppressionDenied>
                         (FSFunction,
                          VarArgsStartIndex - 1,
                          StartIndex,
                          EndIndex),
          RunErrorSeverity::Fatal,
          InstructionIndex);
        return NumAssignments;
      }
    }
    else {
      // Check that the argument type matches the expected type. Don't check
      // that the argument exists here, because some conversion specifiers don't
      // require an argument (i.e. %%), so we check if it exists when needed, in
      // the isArgumentTypeOK() implementation.
      if (!Conversion.isArgumentTypeOK(VarArgs, NextArg)) {
        Listener.handleRunError(
          *createRunError<RunErrorType::FormatSpecifierArgType>
                         (FSFunction,
                          VarArgsStartIndex - 1,
                          StartIndex,
                          EndIndex,
                          asCFormatLengthModifier(Conversion.Length),
                          VarArgs.offset() + NextArg),
          RunErrorSeverity::Fatal,
          InstructionIndex);
        return NumAssignments;
      }

      // If the argument type is a pointer, check that the destination is
      // writable. The conversion for strings (and sets) is a special case.
      if (Conversion.Conversion == ScanConversionSpecifier::Specifier::s
          || Conversion.Conversion == ScanConversionSpecifier::Specifier::set) {
        if (NextArg < VarArgs.size() && Conversion.WidthSpecified) {
          // Check that the destination is writable and has sufficient space
          // for the field width specified by the programmer.
          auto MaybeArea = Conversion.getArgumentPointee(VarArgs, NextArg);
          auto const Size = (Conversion.Length == LengthModifier::l)
                          ? (Conversion.Width + 1) * sizeof(wchar_t)
                          : (Conversion.Width + 1) * sizeof(char);
          
          if (!Checker.checkMemoryExistsAndAccessibleForParameter(
                  VarArgs.offset() + NextArg,
                  MaybeArea.get<0>().address(),
                  Size,
                  seec::runtime_errors::format_selects::MemoryAccess::Write))
            return NumAssignments;
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
    }
    
    // Consume leading whitespace (if this conversion allows it).
    if (Conversion.consumesWhitespace()) {
      int ReadChar = 0;
      
      while ((ReadChar = std::fgetc(Stream)) != EOF) {
        if (!std::isspace(ReadChar)) {
          std::ungetc(ReadChar, Stream);
          break;
        }
      }
      
      if (ReadChar == EOF) {
        InputFailure = true;
        break;
      }
    }
    
    // Attempt the conversion.
    bool ConversionSuccessful = true;
    
    switch (Conversion.Conversion) {
      case ScanConversionSpecifier::Specifier::none:
        llvm_unreachable("encountered scan conversion specifier \"none\"");
        break;
      
      case ScanConversionSpecifier::Specifier::percent:
        if (!matchNonConversionCharacters(NumCharsRead,
                                          Stream,
                                          Conversion.End - 1,
                                          Conversion.End)) {
          if (std::feof(Stream) || std::ferror(Stream))
            InputFailure = true;
          else
            ConversionSuccessful = false;
        }
        break;
      
      case ScanConversionSpecifier::Specifier::c:
        // Read a single char.
        if (Conversion.Length == LengthModifier::none) {
          if (Conversion.SuppressAssignment || NextArg >= VarArgs.size()) {
            if (std::fscanf(Stream, "%*c") != EOF)
              ++NumConversions;
            else
              InputFailure = true;
          }
          else {
            auto Ptr = VarArgs.getAs<char *>(NextArg).get<0>();
            auto Result = std::fscanf(Stream, "%c", Ptr);
            
            switch (Result) {
              case 1:
                ++NumConversions;
                ++NumAssignments;
                Listener.recordUntypedState(reinterpret_cast<char const *>(Ptr),
                                            sizeof(*Ptr));
                break;
              case 0:
                ConversionSuccessful = false;
                break;
              case EOF:
                InputFailure = true;
                break;
              default:
                llvm_unreachable("unexpected result from std::fscanf.");
                break;
            }
          }
        }
        else if (Conversion.Length == LengthModifier::l) {
          if (Conversion.SuppressAssignment || NextArg >= VarArgs.size()) {
            if (std::fscanf(Stream, "&*lc") != EOF)
              ++NumConversions;
            else
              InputFailure = true;
          }
          else {
            auto Ptr = VarArgs.getAs<wchar_t *>(NextArg).get<0>();
            auto Result = std::fscanf(Stream, "%lc", Ptr);
            
            switch (Result) {
              case 1:
                ++NumConversions;
                ++NumAssignments;
                Listener.recordUntypedState(reinterpret_cast<char const *>(Ptr),
                                            sizeof(*Ptr));
                break;
              case 0:
                ConversionSuccessful = false;
                break;
              case EOF:
                InputFailure = true;
                break;
              default:
                llvm_unreachable("unexpected result from std::fscanf.");
                break;
            }
          }
        }
        else {
          llvm_unreachable("unsupported length for 'c' conversion.");
          ConversionSuccessful = false;
        }
        break;
        
      case ScanConversionSpecifier::Specifier::s:
        // Read string.
        {
          auto Width = Conversion.Width;
          if (Width == 0)
            Width = std::numeric_limits<decltype(Width)>::max();
          
          bool InsufficientMemory = false;
          
          if (Conversion.Length == LengthModifier::none) {
            auto const Dest = NextArg < VarArgs.size()
                            ? VarArgs.getAs<char *>(NextArg).get<0>()
                            : nullptr;
            
            auto const DestAddr = reinterpret_cast<uintptr_t>(Dest);
            
            auto const Writable
                          = DestAddr
                          ? Checker.getSizeOfWritableAreaStartingAt(DestAddr)
                          : 0;
            
            int MatchedChars = 0;
            int WrittenChars = 0;
            int ReadChar;
            
            for (; Width != 0; --Width) {
              if ((ReadChar = std::fgetc(Stream)) == EOF) {
                if (std::ferror(Stream) || (std::feof(Stream) && !WrittenChars))
                  InputFailure = true;
                break;
              }
              
              if (std::isspace(ReadChar)) {
                std::ungetc(ReadChar, Stream);
                break;
              }
              
              ++MatchedChars;
              ++NumCharsRead;
              
              if (!Conversion.SuppressAssignment) {
                // Write character.
                if (WrittenChars < Writable)
                  Dest[WrittenChars++] = static_cast<char>(ReadChar);
                else
                  InsufficientMemory = true;
              }
            }
            
            if (!InputFailure) {
              if (MatchedChars == 0) {
                ConversionSuccessful = false;
              }
              else {
                ++NumConversions;
                  
                if (!Conversion.SuppressAssignment) {
                  // Attempt to nul-terminate the string. If this succeeds,
                  // record the strings new state to the trace.
                  if (WrittenChars < Writable) {
                    Dest[WrittenChars++] = '\0';
                    Listener.recordUntypedState(Dest, WrittenChars);
                    ++NumAssignments;
                  }
                  else
                    InsufficientMemory = true;
                }
              }
            }
            
            if (InsufficientMemory) {
              // Raise error for insufficient memory in destination buffer.
              Listener.handleRunError(
                *createRunError<RunErrorType::ScanFormattedStringOverflow>
                               (FSFunction,
                                VarArgsStartIndex - 1,
                                StartIndex,
                                EndIndex,
                                asCFormatLengthModifier(Conversion.Length),
                                VarArgs.offset() + NextArg,
                                Writable,
                                MatchedChars),
                seec::trace::RunErrorSeverity::Fatal,
                InstructionIndex);
              
              return NumAssignments;
            }
          }
          else if (Conversion.Length == LengthModifier::l) {
            llvm_unreachable("%ls not yet supported.");
          }
          else {
            llvm_unreachable("unsupported length for 's' conversion.");
          }
        }
        break;
      
      case ScanConversionSpecifier::Specifier::set:
        // Read set.
        {
          auto Width = Conversion.Width;
          if (Width == 0)
            Width = std::numeric_limits<decltype(Width)>::max();
          
          bool InsufficientMemory = false;
          
          if (Conversion.Length == LengthModifier::none){
            auto const Dest = NextArg < VarArgs.size()
                            ? VarArgs.getAs<char *>(NextArg).get<0>()
                            : nullptr;
            
            auto const DestAddr = reinterpret_cast<uintptr_t>(Dest);
            
            auto const Writable
                          = DestAddr
                          ? Checker.getSizeOfWritableAreaStartingAt(DestAddr)
                          : 0;
            
            int MatchedChars = 0;
            int WrittenChars = 0;
            int ReadChar;
            
            for (; Width != 0; --Width) {
              if ((ReadChar = std::fgetc(Stream)) == EOF) {
                if (std::ferror(Stream) || (std::feof(Stream) && !WrittenChars))
                  InputFailure = true;
                break;
              }
              
              if (!Conversion.hasSetCharacter(static_cast<char>(ReadChar))) {
                std::ungetc(ReadChar, Stream);
                break;
              }
              
              ++MatchedChars;
              ++NumCharsRead;
              
              if (!Conversion.SuppressAssignment) {
                // Write character.
                if (WrittenChars < Writable)
                  Dest[WrittenChars++] = static_cast<char>(ReadChar);
                else
                  InsufficientMemory = true;
              }
            }
            
            if (!InputFailure) {
              if (MatchedChars == 0) {
                ConversionSuccessful = false;
              }
              else {
                ++NumConversions;
                  
                if (!Conversion.SuppressAssignment) {
                  // Attempt to nul-terminate the string. If this succeeds,
                  // record the strings new state to the trace.
                  if (WrittenChars < Writable) {
                    Dest[WrittenChars++] = '\0';
                    Listener.recordUntypedState(Dest, WrittenChars);
                    ++NumAssignments;
                  }
                  else
                    InsufficientMemory = true;
                }
              }
            }
            
            if (InsufficientMemory) {
              // Raise error for insufficient memory in destination buffer.
              Listener.handleRunError(
                *createRunError<RunErrorType::ScanFormattedStringOverflow>
                               (FSFunction,
                                VarArgsStartIndex - 1,
                                StartIndex,
                                EndIndex,
                                asCFormatLengthModifier(Conversion.Length),
                                VarArgs.offset() + NextArg,
                                Writable,
                                MatchedChars),
                seec::trace::RunErrorSeverity::Fatal,
                InstructionIndex);
              
              return NumAssignments;
            }
          }
          else if (Conversion.Length == LengthModifier::l) {
            llvm_unreachable("%l[ not yet supported.");
          }
          else {
            llvm_unreachable("unexpected length for set conversion.");
          }
        }
        break;
      
      case ScanConversionSpecifier::Specifier::u: SEEC_FALLTHROUGH;
      case ScanConversionSpecifier::Specifier::d: SEEC_FALLTHROUGH;
      case ScanConversionSpecifier::Specifier::i: SEEC_FALLTHROUGH;
      case ScanConversionSpecifier::Specifier::o: SEEC_FALLTHROUGH;
      case ScanConversionSpecifier::Specifier::x:
        // Read integer.
        {
          std::uintmax_t ReadInt;
          if (!parseInt(NumCharsRead, Stream, Conversion, ReadInt)) {
            ConversionSuccessful = false;
            break;
          }
          
          ++NumConversions;
          
          if (!Conversion.SuppressAssignment && NextArg < VarArgs.size()) {
            ConversionSuccessful
              = Conversion.assignPointee(Listener, VarArgs, NextArg, ReadInt);
            if (ConversionSuccessful)
              ++NumAssignments;
          }
        }
        break;
      
      case ScanConversionSpecifier::Specifier::n:
        ++NumConversions;
        
        if (!Conversion.SuppressAssignment) {
          ConversionSuccessful = Conversion.assignPointee(Listener,
                                                          VarArgs,
                                                          NextArg,
                                                          NumCharsRead);
          if (ConversionSuccessful)
            ++NumAssignments;
        }
        
        break;
      
      case ScanConversionSpecifier::Specifier::a:
      case ScanConversionSpecifier::Specifier::A:
      case ScanConversionSpecifier::Specifier::e:
      case ScanConversionSpecifier::Specifier::E:
      case ScanConversionSpecifier::Specifier::f:
      case ScanConversionSpecifier::Specifier::F:
      case ScanConversionSpecifier::Specifier::g:
      case ScanConversionSpecifier::Specifier::G:
        // Read float.
        {
          char Buffer[128];
          std::size_t BufferIdx = 0;
          int ReadChar;
          
          while ((ReadChar = std::fgetc(Stream)) != EOF) {
            if (std::isspace(ReadChar)) {
              std::ungetc(ReadChar, Stream);
              break;
            }
            
            Buffer[BufferIdx++] = static_cast<char>(ReadChar);
            
            if (BufferIdx >= sizeof(Buffer))
              break;
          }
          
          if (BufferIdx == 0) {
            if (ReadChar == EOF)
              InputFailure = true;
            else
              ConversionSuccessful = false;
            break;
          }
          
          Buffer[BufferIdx] = '\0';
          
          char *ParseEnd = nullptr;
          
          switch (Conversion.Length) {
            case LengthModifier::none:
              {
                float Value = std::strtof(Buffer, &ParseEnd);
                if (ParseEnd == Buffer)
                  ConversionSuccessful = false;
                else {
                  ++NumConversions;
                  if (!Conversion.SuppressAssignment) {
                    ConversionSuccessful = Conversion.assignPointee(Listener,
                                                                    VarArgs,
                                                                    NextArg,
                                                                    Value);
                    if (ConversionSuccessful)
                      ++NumAssignments;
                  }
                }
              }
              break;
              
            case LengthModifier::l:
              {
                double Value = std::strtod(Buffer, &ParseEnd);
                if (ParseEnd == Buffer)
                  ConversionSuccessful = false;
                else {
                  ++NumConversions;
                  if (!Conversion.SuppressAssignment) {
                    ConversionSuccessful = Conversion.assignPointee(Listener,
                                                                    VarArgs,
                                                                    NextArg,
                                                                    Value);
                    if (ConversionSuccessful)
                      ++NumAssignments;
                  }
                }
              }
              break;
              
            case LengthModifier::L:
              {
                long double Value = std::strtold(Buffer, &ParseEnd);
                if (ParseEnd == Buffer)
                  ConversionSuccessful = false;
                else {
                  ++NumConversions;
                  if (!Conversion.SuppressAssignment) {
                    ConversionSuccessful = Conversion.assignPointee(Listener,
                                                                    VarArgs,
                                                                    NextArg,
                                                                    Value);
                    if (ConversionSuccessful)
                      ++NumAssignments;
                  }
                }
              }
              break;
            
            default:
              llvm_unreachable("unexpected length for float conversion.");
              break;
          }
          
          for (auto Push = &Buffer[BufferIdx - 1]; Push >= ParseEnd; --Push) {
            std::ungetc(*Push, Stream);
          }
        }
        break;
      
      case ScanConversionSpecifier::Specifier::p:
        // Read pointer.
        if (Conversion.SuppressAssignment || NextArg >= VarArgs.size()) {
          if (std::fscanf(Stream, "%*p") != EOF)
            ++NumConversions;
          else
            InputFailure = true;
        }
        else {
          auto Ptr = VarArgs.getAs<void **>(NextArg).get<0>();
          auto Result = std::fscanf(Stream, "%p", Ptr);
          
          switch (Result) {
            case 1:
              ++NumConversions;
              ++NumAssignments;
              Listener.recordUntypedState(reinterpret_cast<char const *>(Ptr),
                                          sizeof(*Ptr));
              break;
            case 0:
              ConversionSuccessful = false;
              break;
            case EOF:
              InputFailure = true;
              break;
            default:
              llvm_unreachable("unexpected result from std::fscanf.");
              break;
          }
        }
        break;
    }
    
    if (!ConversionSuccessful || InputFailure) {
      break;
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
  
  // Ensure that we got a sufficient number of arguments.
  if (NextArg > VarArgs.size()) {
    Listener.handleRunError(*createRunError<RunErrorType::VarArgsInsufficient>
                                           (FSFunction,
                                            NextArg,
                                            VarArgs.size()),
                            RunErrorSeverity::Fatal,
                            InstructionIndex);
  }
  
  // Record the produced value.
  Listener.notifyValue(InstructionIndex, Instruction, unsigned(NumAssignments));
  
  if (InputFailure && NumConversions == 0)
    return EOF;
  
  return NumAssignments;
}


extern "C" {


//===----------------------------------------------------------------------===//
// fread
//===----------------------------------------------------------------------===//

size_t
SEEC_MANGLE_FUNCTION(fread)
(void *buffer, size_t size, size_t count, FILE *stream)
{
  // Use the SimpleWrapper mechanism.
  return
    seec::SimpleWrapper
      <seec::SimpleWrapperSetting::AcquireGlobalMemoryWriteLock>
      {seec::runtime_errors::format_selects::CStdFunction::fread}
      (fread,
       [](size_t Result){ return Result != 0; },
       seec::ResultStateRecorderForNoOp(),
       seec::wrapOutputPointer(buffer).setSize(size * count),
       size,
       count,
       seec::wrapInputFILE(stream));
}

//===----------------------------------------------------------------------===//
// fwrite
//===----------------------------------------------------------------------===//

size_t
SEEC_MANGLE_FUNCTION(fwrite)
(void const *buffer, size_t size, size_t count, FILE *stream)
{
  // Use the SimpleWrapper mechanism.
  return
    seec::SimpleWrapper
      <seec::SimpleWrapperSetting::AcquireGlobalMemoryReadLock>
      {seec::runtime_errors::format_selects::CStdFunction::fwrite}
      (fwrite,
       [](size_t Result){ return Result != 0; },
       seec::ResultStateRecorderForNoOp(),
       seec::wrapInputPointer(buffer).setSize(size * count),
       size,
       count,
       seec::wrapInputFILE(stream));
}

//===----------------------------------------------------------------------===//
// scanf
//===----------------------------------------------------------------------===//

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

//===----------------------------------------------------------------------===//
// fscanf
//===----------------------------------------------------------------------===//

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

//===----------------------------------------------------------------------===//
// sscanf
//===----------------------------------------------------------------------===//

int
SEEC_MANGLE_FUNCTION(sscanf)
(char const *Buffer, char const *Format, ...)
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
    VarArgs{Listener, Call, 2};

  // Lock global memory.
  Listener.acquireGlobalMemoryWriteLock();
  
  // Use a CIOChecker to help check memory.
  auto FSFunction = seec::runtime_errors::format_selects::CStdFunction::sscanf;
  CStdLibChecker Checker{Listener, InstructionIndex, FSFunction};
  
  // Check that the buffer is valid.
  auto BufferSize = Checker.checkCStringRead(0, Buffer);
  if (!BufferSize) {
    return 0;
  }
  
  // Check and perform the (f)scanf.
  auto FormatSize = Checker.checkCStringRead(1, Format);
  if (!FormatSize) {
    return 0;
  }
  
  int NumConversions = 0;
  unsigned NextArg = 0;
  char const *NextFormatChar = Format;
  char const *NextBufferChar = Buffer;
  
  while (true) {
    auto Conversion = ScanConversionSpecifier::readNextFrom(NextFormatChar);
    if (!Conversion.Start) {
      // We don't need to match and consume remaining characters, because it
      // would make no difference to the program's behaviour.
      break;
    }
    
    bool ConversionSuccessful = true;
    
    // Attempt to match and consume [NextFormatChar, Conversion.Start).
    while (NextFormatChar < Conversion.Start) {
      if (std::isspace(*NextFormatChar)) {
        // Consume any amount of whitespace.
        while (*NextBufferChar && std::isspace(*NextBufferChar))
          ++NextBufferChar;
        ++NextFormatChar;
      }
      else if (*NextFormatChar == *NextBufferChar){
        // Literal match.
        ++NextFormatChar;
        ++NextBufferChar;
      }
      else {
        // Match failure.
        ConversionSuccessful = false;
        break;
      }
    }
    
    if (!ConversionSuccessful)
      break;
    
    auto const StartIndex = Conversion.Start - Format;
    
    // Ensure that the conversion specifier was parsed correctly.
    if (!Conversion.End) {
      Listener.handleRunError(
        *createRunError<RunErrorType::FormatSpecifierParse>
                       (FSFunction, 1, StartIndex),
        RunErrorSeverity::Fatal,
        InstructionIndex);
      
      return NumConversions;
    }
    
    auto const EndIndex = Conversion.End - Format;
    
    // If assignment was suppressed, ensure that suppressing assignment is OK.
    if (Conversion.SuppressAssignment) {
      if (!Conversion.allowedSuppressAssignment()) {
        Listener.handleRunError(
          *createRunError<RunErrorType::FormatSpecifierSuppressionDenied>
                         (FSFunction, 1, StartIndex, EndIndex),
          RunErrorSeverity::Fatal,
          InstructionIndex);
        
        return NumConversions;
      }
    }
    else {
      // Check that the argument type matches the expected type. Don't check
      // that the argument exists here, because some conversion specifiers don't
      // require an argument (i.e. %%), so we check if it exists when needed, in
      // the isArgumentTypeOK() implementation.
      if (!Conversion.isArgumentTypeOK(VarArgs, NextArg)) {
        Listener.handleRunError(
          *createRunError<RunErrorType::FormatSpecifierArgType>
                         (FSFunction,
                          1,
                          StartIndex,
                          EndIndex,
                          asCFormatLengthModifier(Conversion.Length),
                          VarArgs.offset() + NextArg),
          RunErrorSeverity::Fatal,
          InstructionIndex);
        
        return NumConversions;
      }

      // If the argument type is a pointer, check that the destination is
      // writable. The conversion for strings (and sets) is a special case.
      if (Conversion.Conversion == ScanConversionSpecifier::Specifier::s
          || Conversion.Conversion == ScanConversionSpecifier::Specifier::set) {
        if (NextArg < VarArgs.size() && Conversion.WidthSpecified) {
          // Check that the destination is writable and has sufficient space
          // for the field width specified by the programmer.
          auto MaybeArea = Conversion.getArgumentPointee(VarArgs, NextArg);
          auto const Size = (Conversion.Length == LengthModifier::l)
                          ? (Conversion.Width + 1) * sizeof(wchar_t)
                          : (Conversion.Width + 1) * sizeof(char);
          
          if (!Checker.checkMemoryExistsAndAccessibleForParameter(
                  VarArgs.offset() + NextArg,
                  MaybeArea.get<0>().address(),
                  Size,
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
    }
    
    // Consume leading whitespace (if this conversion allows it).
    if (Conversion.consumesWhitespace()) {
      while (std::isspace(*NextBufferChar))
        ++NextBufferChar;
    }
    
    // Perform the conversion.
    bool IntConversion = false;
    bool IntConversionUnsigned = false;
    int IntConversionBase = 0;
    
    switch (Conversion.Conversion) {
      case ScanConversionSpecifier::Specifier::none:
        llvm_unreachable("encountered scan conversion specifier \"none\"");
        break;
      
      case ScanConversionSpecifier::Specifier::percent:
        if (*NextBufferChar == '%')
          ++NextBufferChar;
        else
          ConversionSuccessful = false;
        break;
      
      case ScanConversionSpecifier::Specifier::c:
        // Read a single char.
        if (Conversion.Length == LengthModifier::none) {
          if (*NextBufferChar) {
            if (!Conversion.SuppressAssignment && NextArg < VarArgs.size()) {
              ConversionSuccessful = Conversion.assignPointee(Listener,
                                                              VarArgs,
                                                              NextArg,
                                                              *NextBufferChar);
              if (ConversionSuccessful)
                ++NumConversions;
            }
            
            ++NextBufferChar;
          }
          else {
            ConversionSuccessful = false;
          }
        }
        else if (Conversion.Length == LengthModifier::l) {
          llvm_unreachable("%lc not supported yet.");
        }
        else {
          llvm_unreachable("unexpected length for c conversion.");
        }
        break;
        
      case ScanConversionSpecifier::Specifier::s:
        // Read string.
        {
          auto Width = Conversion.Width;
          if (Width == 0)
            Width = std::numeric_limits<decltype(Width)>::max();
          
          bool InsufficientMemory = false;
          
          if (Conversion.Length == LengthModifier::none) {
            auto const Dest = NextArg < VarArgs.size()
                            ? VarArgs.getAs<char *>(NextArg).get<0>()
                            : nullptr;
            
            auto const DestAddr = reinterpret_cast<uintptr_t>(Dest);
            
            auto const Writable
                          = DestAddr
                          ? Checker.getSizeOfWritableAreaStartingAt(DestAddr)
                          : 0;
            
            int MatchedChars = 0;
            int WrittenChars = 0;
            
            for (; *NextBufferChar && Width != 0; --Width) {
              if (std::isspace(*NextBufferChar))
                break;
              
              if (!Conversion.SuppressAssignment) {
                if (WrittenChars < Writable)
                  Dest[WrittenChars++] = *NextBufferChar;
                else
                  InsufficientMemory = true;
              }
              
              ++MatchedChars;
              ++NextBufferChar;
            }
            
            if (MatchedChars == 0)
              ConversionSuccessful = false;
            else {
              if (!Conversion.SuppressAssignment) {
                if (WrittenChars < Writable) {
                  Dest[WrittenChars++] = '\0';
                  Listener.recordUntypedState(Dest, WrittenChars);
                  ++NumConversions;
                }
                else
                  InsufficientMemory = true;
              }
              
              if (InsufficientMemory) {
                // Raise error for insufficient memory in destination buffer.
                Listener.handleRunError(
                  *createRunError<RunErrorType::ScanFormattedStringOverflow>
                                 (FSFunction,
                                  1, // Index of "Format" argument.
                                  StartIndex,
                                  EndIndex,
                                  asCFormatLengthModifier(Conversion.Length),
                                  VarArgs.offset() + NextArg,
                                  Writable,
                                  MatchedChars + 1),
                  seec::trace::RunErrorSeverity::Fatal,
                  InstructionIndex);
                
                return NumConversions;
              }
            }
          }
          else if (Conversion.Length == LengthModifier::l) {
            llvm_unreachable("%ls not supported yet.");
          }
          else {
            llvm_unreachable("unexpected length for s conversion.");
          }
        }
        break;
      
      case ScanConversionSpecifier::Specifier::set:
        // Read set.
        {
          auto Width = Conversion.Width;
          if (Width == 0)
            Width = std::numeric_limits<decltype(Width)>::max();
          
          bool InsufficientMemory = false;
          
          if (Conversion.Length == LengthModifier::none) {
            auto const Dest = NextArg < VarArgs.size()
                            ? VarArgs.getAs<char *>(NextArg).get<0>()
                            : nullptr;
            
            auto const DestAddr = reinterpret_cast<uintptr_t>(Dest);
            
            auto const Writable
                          = DestAddr
                          ? Checker.getSizeOfWritableAreaStartingAt(DestAddr)
                          : 0;
            
            int MatchedChars = 0;
            int WrittenChars = 0;
            
            for (; *NextBufferChar && Width != 0; --Width) {
              if (!Conversion.hasSetCharacter(*NextBufferChar))
                break;
              
              if (!Conversion.SuppressAssignment) {
                if (WrittenChars < Writable)
                  Dest[WrittenChars++] = *NextBufferChar;
                else
                  InsufficientMemory = true;
              }
              
              ++MatchedChars;
              ++NextBufferChar;
            }
            
            if (MatchedChars == 0)
              ConversionSuccessful = false;
            else {
              if (!Conversion.SuppressAssignment) {
                if (WrittenChars < Writable) {
                  Dest[WrittenChars++] = '\0';
                  Listener.recordUntypedState(Dest, WrittenChars);
                  ++NumConversions;
                }
                else
                  InsufficientMemory = true;
              }
              
              if (InsufficientMemory) {
                // Raise error for insufficient memory in destination buffer.
                Listener.handleRunError(
                  *createRunError<RunErrorType::ScanFormattedStringOverflow>
                                 (FSFunction,
                                  1, // Index of "Format" argument.
                                  StartIndex,
                                  EndIndex,
                                  asCFormatLengthModifier(Conversion.Length),
                                  VarArgs.offset() + NextArg,
                                  Writable,
                                  MatchedChars + 1),
                  seec::trace::RunErrorSeverity::Fatal,
                  InstructionIndex);
                
                return NumConversions;
              }
            }
          }
          else if (Conversion.Length == LengthModifier::l) {
            llvm_unreachable("%l[ not supported yet.");
          }
          else {
            llvm_unreachable("unexpected length for set conversion.");
          }
        }
        break;
      
      case ScanConversionSpecifier::Specifier::u:
        IntConversion = true;
        IntConversionUnsigned = true;
        break;
      
      case ScanConversionSpecifier::Specifier::d:
        IntConversion = true;
        IntConversionBase = 10;
        break;
      
      case ScanConversionSpecifier::Specifier::i:
        IntConversion = true;
        break;
      
      case ScanConversionSpecifier::Specifier::o:
        IntConversion = true;
        IntConversionBase = 8;
        break;
      
      case ScanConversionSpecifier::Specifier::x:
        IntConversion = true;
        IntConversionBase = 16;
        break;
      
      case ScanConversionSpecifier::Specifier::n:
        if (!Conversion.SuppressAssignment) {
          auto NumCharsRead = NextBufferChar - Buffer;
          ConversionSuccessful = Conversion.assignPointee(Listener,
                                                          VarArgs,
                                                          NextArg,
                                                          NumCharsRead);
        }
        break;
      
      case ScanConversionSpecifier::Specifier::a:
      case ScanConversionSpecifier::Specifier::A:
      case ScanConversionSpecifier::Specifier::e:
      case ScanConversionSpecifier::Specifier::E:
      case ScanConversionSpecifier::Specifier::f:
      case ScanConversionSpecifier::Specifier::F:
      case ScanConversionSpecifier::Specifier::g:
      case ScanConversionSpecifier::Specifier::G:
        // Read float.
        {
          if (Conversion.Length == LengthModifier::none) {
            float Value = 0;
            char *ParseEnd = nullptr;
            Value = std::strtof(NextBufferChar, &ParseEnd);
            if (ParseEnd != NextBufferChar) {
              NextBufferChar = ParseEnd;
              if (!Conversion.SuppressAssignment) {
                ConversionSuccessful
                  = Conversion.assignPointee(Listener, VarArgs, NextArg, Value);
                if (ConversionSuccessful)
                  ++NumConversions;
              }
            }
            else {
              ConversionSuccessful = false;
            }
          }
          else if (Conversion.Length == LengthModifier::l) {
            double Value = 0;
            char *ParseEnd = nullptr;
            Value = std::strtod(NextBufferChar, &ParseEnd);
            if (ParseEnd != NextBufferChar) {
              NextBufferChar = ParseEnd;
              if (!Conversion.SuppressAssignment) {
                ConversionSuccessful
                  = Conversion.assignPointee(Listener, VarArgs, NextArg, Value);
                if (ConversionSuccessful)
                  ++NumConversions;
              }
            }
            else {
              ConversionSuccessful = false;
            }
          }
          else if (Conversion.Length == LengthModifier::L) {
            long double Value = 0;
            char *ParseEnd = nullptr;
            Value = std::strtold(NextBufferChar, &ParseEnd);
            if (ParseEnd != NextBufferChar) {
              NextBufferChar = ParseEnd;
              if (!Conversion.SuppressAssignment) {
                ConversionSuccessful
                  = Conversion.assignPointee(Listener, VarArgs, NextArg, Value);
                if (ConversionSuccessful)
                  ++NumConversions;
              }
            }
            else {
              ConversionSuccessful = false;
            }
          }
          else {
            llvm_unreachable("unexpected length for f conversion.");
          }
        }
        break;
      
      case ScanConversionSpecifier::Specifier::p:
        // TODO: Read pointer.
        llvm_unreachable("%p not yet implemented");
        break;
    }
    
    if (IntConversion) {
      unsigned long Value = 0;
      char *ParseEnd = nullptr;
      
      if (IntConversionUnsigned)
        Value = std::strtoul(NextBufferChar, &ParseEnd, IntConversionBase);
      else
        Value = std::strtol(NextBufferChar, &ParseEnd, IntConversionBase);
      
      if (ParseEnd != NextBufferChar) {
        NextBufferChar = ParseEnd;
        
        if (!Conversion.SuppressAssignment) {
          ConversionSuccessful
            = Conversion.assignPointee(Listener, VarArgs, NextArg, Value);
          if (ConversionSuccessful)
            ++NumConversions;
        }
      }
      else {
        ConversionSuccessful = false;
      }
    }
    
    if (!ConversionSuccessful)
      break;
    
    // Move to the next argument (unless this conversion specifier doesn't
    // consume an argument).
    if (Conversion.Conversion != ScanConversionSpecifier::Specifier::percent
        && Conversion.SuppressAssignment == false) {
      ++NextArg;
    }
    
    // The next position to search from should be the first character following
    // this conversion specifier.
    NextFormatChar = Conversion.End;
  }
  
  // Ensure that we got a sufficient number of arguments.
  if (NextArg > VarArgs.size()) {
    Listener.handleRunError(*createRunError<RunErrorType::VarArgsInsufficient>
                                           (FSFunction,
                                            NextArg,
                                            VarArgs.size()),
                            RunErrorSeverity::Fatal,
                            InstructionIndex);
  }
  
  // Record the produced value.
  Listener.notifyValue(InstructionIndex, Instruction, unsigned(NumConversions));
  
  return NumConversions;
}


//===----------------------------------------------------------------------===//
// tmpnam
//===----------------------------------------------------------------------===//

char *
SEEC_MANGLE_FUNCTION(tmpnam)
(char *Buffer)
{
  using namespace seec::trace;
  
  auto &ThreadEnv = getThreadEnvironment();
  auto &Listener = ThreadEnv.getThreadListener();
  auto Instruction = ThreadEnv.getInstruction();
  auto InstructionIndex = ThreadEnv.getInstructionIndex();
  
  // Interact with the thread listener's notification system.
  Listener.enterNotification();
  auto DoExit = seec::scopeExit([&](){ Listener.exitPostNotification(); });
  
  // Lock global memory.
  Listener.acquireGlobalMemoryWriteLock();
  
  // Use a CIOChecker to help check memory.
  auto FSFunction = seec::runtime_errors::format_selects::CStdFunction::wait;
  CStdLibChecker Checker{Listener, InstructionIndex, FSFunction};
  
  // Ensure that writing to Buffer will be OK.
  if (Buffer)
    Checker.checkMemoryExistsAndAccessibleForParameter
              (0,
               reinterpret_cast<uintptr_t>(Buffer),
               L_tmpnam,
               seec::runtime_errors::format_selects::MemoryAccess::Write);
  
  auto Result = tmpnam(Buffer);
  auto Length = std::strlen(Result) + 1;
  
  // Record the result.
  Listener.notifyValue(InstructionIndex,
                       Instruction,
                       Result);
  
  if (Buffer) {
    // Record the write to Buffer.
    Listener.recordUntypedState(Buffer, Length);
  }
  else {
    // Record tmpnam's internal static array.
    auto Address = reinterpret_cast<uintptr_t>(Result);
    
    // Remove knowledge of the existing getenv string at this position (if any).
    Listener.removeKnownMemoryRegion(Address);
  
    // TODO: Delete any existing memory states at this address.
  
    // Set knowledge of the new string area.
    Listener.addKnownMemoryRegion(Address,
                                  Length,
                                  seec::MemoryPermission::ReadOnly);
    
    // Record the write to the new string area.
    Listener.recordUntypedState(Result, Length);
  }
  
  return Result;
}

//===----------------------------------------------------------------------===//
// fdopen
//===----------------------------------------------------------------------===//

FILE *
SEEC_MANGLE_FUNCTION(fdopen)
(int FileDescriptor, char const *Mode)
{
  using namespace seec::trace;
  
  auto &ThreadEnv = getThreadEnvironment();
  auto &Listener = ThreadEnv.getThreadListener();
  auto &ProcessListener = getProcessEnvironment().getProcessListener();
  auto Instruction = ThreadEnv.getInstruction();
  auto InstructionIndex = ThreadEnv.getInstructionIndex();
  
  // Interact with the thread listener's notification system.
  Listener.enterNotification();
  auto DoExit = seec::scopeExit([&](){ Listener.exitPostNotification(); });
  
  // Lock global memory.
  Listener.acquireGlobalMemoryWriteLock();
  auto Streams = ProcessListener.getStreamsAccessor();
  
  // Use a CIOChecker to help check memory.
  auto FSFunction = seec::runtime_errors::format_selects::CStdFunction::wait;
  CStdLibChecker Checker{Listener, InstructionIndex, FSFunction};
  
  // Ensure that Mode is accessible.
  Checker.checkCStringRead(1, Mode);
  
  auto Result = fdopen(FileDescriptor, Mode);
  
  // Record the result.
  Listener.notifyValue(InstructionIndex,
                       Instruction,
                       Result);
  
  if (Result) {
    Streams->streamOpened(Result);
  }
  else{
    Listener.recordUntypedState(reinterpret_cast<char const *>(&errno),
                                sizeof(errno));
  }
  
  return Result;
}

//===----------------------------------------------------------------------===//
// ftell
//===----------------------------------------------------------------------===//

long
SEEC_MANGLE_FUNCTION(ftell)
(FILE *stream)
{
  // Use the SimpleWrapper mechanism.
  return
    seec::SimpleWrapper
      <>
      {seec::runtime_errors::format_selects::CStdFunction::ftell}
      (ftell,
       [](long Result){ return Result != EOF; },
       seec::ResultStateRecorderForNoOp(),
       seec::wrapInputFILE(stream));
}

//===----------------------------------------------------------------------===//
// fgetpos
//===----------------------------------------------------------------------===//

int
SEEC_MANGLE_FUNCTION(fgetpos)
(FILE *stream, fpos_t *pos)
{
  // Use the SimpleWrapper mechanism.
  return
    seec::SimpleWrapper
      <seec::SimpleWrapperSetting::AcquireGlobalMemoryWriteLock>
      {seec::runtime_errors::format_selects::CStdFunction::fgetpos}
      (fgetpos,
       [](int Result){ return Result == 0; },
       seec::ResultStateRecorderForNoOp(),
       seec::wrapInputFILE(stream),
       seec::wrapOutputPointer(pos));
}

//===----------------------------------------------------------------------===//
// fseek
//===----------------------------------------------------------------------===//

int
SEEC_MANGLE_FUNCTION(fseek)
(FILE *stream, long offset, int origin)
{
  // Use the SimpleWrapper mechanism.
  // TODO: ensure that origin is SEEK_SET, SEEK_CUR, or SEEK_END
  return
    seec::SimpleWrapper
      <>
      {seec::runtime_errors::format_selects::CStdFunction::fseek}
      (fseek,
       [](int Result){ return Result == 0; },
       seec::ResultStateRecorderForNoOp(),
       seec::wrapInputFILE(stream),
       offset,
       origin);
}

//===----------------------------------------------------------------------===//
// fsetpos
//===----------------------------------------------------------------------===//

int
SEEC_MANGLE_FUNCTION(fsetpos)
(FILE *stream, fpos_t const *pos)
{
  // Use the SimpleWrapper mechanism.
  return
    seec::SimpleWrapper
      <seec::SimpleWrapperSetting::AcquireGlobalMemoryReadLock>
      {seec::runtime_errors::format_selects::CStdFunction::fsetpos}
      (fsetpos,
       [](int Result){ return Result == 0; },
       seec::ResultStateRecorderForNoOp(),
       seec::wrapInputFILE(stream),
       seec::wrapInputPointer(pos));
}

//===----------------------------------------------------------------------===//
// rewind
//===----------------------------------------------------------------------===//

void
SEEC_MANGLE_FUNCTION(rewind)
(FILE *stream)
{
  // Use the SimpleWrapper mechanism.
  return
    seec::SimpleWrapper
      <>
      {seec::runtime_errors::format_selects::CStdFunction::rewind}
      (rewind,
       [](){ return true; },
       seec::ResultStateRecorderForNoOp(),
       seec::wrapInputFILE(stream));
}


} // extern "C"
