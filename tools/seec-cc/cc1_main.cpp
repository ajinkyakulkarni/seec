//===- tools/seec-cc/cc1_main.cpp -----------------------------------------===//
//
//                                    SeeC
//
// This file is distributed under The MIT License (MIT). See LICENSE.TXT for
// details.
//
//===----------------------------------------------------------------------===//
///
/// \file This is primarily taken from Clang's tools/driver/cc1_main.cpp.
///
//===----------------------------------------------------------------------===//

#include "seec/Clang/Compile.hpp"
#include "seec/ICU/Output.hpp"

#include "llvm/Option/Arg.h"
#include "clang/Basic/DebugInfoOptions.h"
#include "clang/CodeGen/ObjectFilePCHContainerOperations.h"
#include "clang/Driver/DriverDiagnostic.h"
#include "clang/Driver/Options.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/CompilerInvocation.h"
#include "clang/Frontend/FrontendDiagnostic.h"
#include "clang/Frontend/TextDiagnosticBuffer.h"
#include "clang/Frontend/TextDiagnosticPrinter.h"
#include "clang/Frontend/Utils.h"
#include "clang/FrontendTool/Utils.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/LinkAllPasses.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Option/OptTable.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/Timer.h"
#include "llvm/Support/raw_ostream.h"

#include "SeeCExecuteCompilerInvocation.hpp"

#include <cstdio>
using namespace clang;
using namespace llvm::opt;

//===----------------------------------------------------------------------===//
// Main driver
//===----------------------------------------------------------------------===//

static void LLVMErrorHandler(void *UserData, const std::string &Message,
                             bool GenCrashDiag) {
  DiagnosticsEngine &Diags = *static_cast<DiagnosticsEngine*>(UserData);

  Diags.Report(diag::err_fe_error_backend) << Message;

  // Run the interrupt handlers to make sure any special cleanups get done, in
  // particular that we remove files registered with RemoveFileOnSignal.
  llvm::sys::RunInterruptHandlers();

  // We cannot recover from llvm errors.  When reporting a fatal error, exit
  // with status 70 to generate crash diagnostics.  For BSD systems this is
  // defined as an internal software error.  Otherwise, exit with status 1.
  exit(GenCrashDiag ? 70 : 1);
}

#ifdef LINK_POLLY_INTO_TOOLS
namespace polly {
void initializePollyPasses(llvm::PassRegistry &Registry);
}
#endif

int cc1_main(ArrayRef<const char *> Argv, const char *Argv0, void *MainAddr) {
  std::unique_ptr<CompilerInstance> Clang(new CompilerInstance());
  IntrusiveRefCntPtr<DiagnosticIDs> DiagID(new DiagnosticIDs());

  // Register the support for object-file-wrapped Clang modules.
  auto PCHOps = Clang->getPCHContainerOperations();
  PCHOps->registerWriter(llvm::make_unique<ObjectFilePCHContainerWriter>());
  PCHOps->registerReader(llvm::make_unique<ObjectFilePCHContainerReader>());

  // Initialize targets first, so that --version shows registered targets.
  llvm::InitializeAllTargets();
  llvm::InitializeAllTargetMCs();
  llvm::InitializeAllAsmPrinters();
  llvm::InitializeAllAsmParsers();

#ifdef LINK_POLLY_INTO_TOOLS
  llvm::PassRegistry &Registry = *llvm::PassRegistry::getPassRegistry();
  polly::initializePollyPasses(Registry);
#endif

  // Buffer diagnostics from argument parsing so that we can output them using a
  // well formed diagnostic object.
  IntrusiveRefCntPtr<DiagnosticOptions> DiagOpts = new DiagnosticOptions();
  TextDiagnosticBuffer *DiagsBuffer = new TextDiagnosticBuffer;
  DiagnosticsEngine Diags(DiagID, &*DiagOpts, DiagsBuffer);
  bool Success = CompilerInvocation::CreateFromArgs(
      Clang->getInvocation(), Argv.begin(), Argv.end(), Diags);

  // Emit debug information, as it assists SeeC.
  Clang->getCodeGenOpts()
    .setDebugInfo(codegenoptions::DebugInfoKind::FullDebugInfo);

  // Infer the builtin include path if unspecified.
  if (Clang->getHeaderSearchOpts().UseBuiltinIncludes &&
      Clang->getHeaderSearchOpts().ResourceDir.empty())
    Clang->getHeaderSearchOpts().ResourceDir =
      CompilerInvocation::GetResourcesPath(Argv0, MainAddr);

  // Create the actual diagnostics engine.
  Clang->createDiagnostics();
  if (!Clang->hasDiagnostics())
    return 1;

  // Set an error handler, so that any LLVM backend diagnostics go through our
  // error handler.
  llvm::install_fatal_error_handler(LLVMErrorHandler,
                                  static_cast<void*>(&Clang->getDiagnostics()));

  DiagsBuffer->FlushDiagnostics(Clang->getDiagnostics());
  if (!Success)
    return 1;

  // Execute the frontend actions.
  Success = DoCompilerInvocation(Clang.get(), Argv.begin(), Argv.end());

  // If any timers were active but haven't been destroyed yet, print their
  // results now.  This happens in -disable-free mode.
  llvm::TimerGroup::printAll(llvm::errs());

  // Our error handler depends on the Diagnostics object, which we're
  // potentially about to delete. Uninstall the handler now so that any
  // later errors use the default handling behavior instead.
  llvm::remove_fatal_error_handler();

  // When running with -disable-free, don't do any destruction or shutdown.
  if (Clang->getFrontendOpts().DisableFree) {
    BuryPointer(std::move(Clang));
    return !Success;
  }

  return !Success;
}
