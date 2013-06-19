//===- tools/seec-trace-view/TraceViewerApp.cpp ---------------------------===//
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

#include "seec/ICU/Resources.hpp"
#include "seec/Util/ScopeExit.hpp"
#include "seec/wxWidgets/ICUBundleFSHandler.hpp"
#include "seec/wxWidgets/StringConversion.hpp"

#include "llvm/Support/raw_os_ostream.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/Path.h"

#include <unicode/resbund.h>

#include <wx/wx.h>
#include <wx/filesys.h>
#include <wx/stdpaths.h>
#include "seec/wxWidgets/CleanPreprocessor.h"

#include <array>
#include <memory>
#include <set>

#include "OpenTrace.hpp"
#include "TraceViewerApp.hpp"
#include "TraceViewerFrame.hpp"
#include "WelcomeFrame.hpp"

#define SEEC_SHOW_DEBUG 1


//------------------------------------------------------------------------------
// TraceViewerApp
//------------------------------------------------------------------------------

// Define the event table for TraceViewerApp.
BEGIN_EVENT_TABLE(TraceViewerApp, wxApp)
  EVT_MENU(wxID_OPEN, TraceViewerApp::OnCommandOpen)
  EVT_MENU(wxID_EXIT, TraceViewerApp::OnCommandExit)
END_EVENT_TABLE()



//------------------------------------------------------------------------------
// main
//------------------------------------------------------------------------------

IMPLEMENT_APP(TraceViewerApp)


//------------------------------------------------------------------------------
// TraceViewerApp
//------------------------------------------------------------------------------

void TraceViewerApp::OpenFile(wxString const &FileName) {
  wxLogDebug("Open file %s\n", FileName.c_str());

  // Attempt to read the trace, which should either return the newly read trace
  // (in Maybe slot 0), or an error message (in Maybe slot 1).
  auto NewTrace = OpenTrace::FromFilePath(FileName);
  assert(NewTrace.assigned());

  if (NewTrace.assigned<std::unique_ptr<OpenTrace>>()) {
    // The trace was read successfully, so create a new viewer to display it.
    auto TraceViewer =
      new TraceViewerFrame(nullptr,
                           NewTrace.move<std::unique_ptr<OpenTrace>>());
    
    TopLevelFrames.insert(TraceViewer);
    TraceViewer->Show(true);

    // Hide the Welcome frame (on Mac OS X), or destroy it (all others).
#ifdef __WXMAC__
    if (Welcome)
      Welcome->Show(false);
#else
    if (Welcome) {
      Welcome->Close(true);
      Welcome = nullptr;
    }
#endif
  }
  else if (NewTrace.assigned<seec::Error>()) {
    UErrorCode Status = U_ZERO_ERROR;
    auto const Message = NewTrace.get<seec::Error>
                                     ().getMessage(Status, Locale{});
    
    
    
    // Display the error that occured.
    auto ErrorDialog = new wxMessageDialog(nullptr, seec::towxString(Message));
    ErrorDialog->ShowModal();
    ErrorDialog->Destroy();
  }
}

bool TraceViewerApp::OnInit() {
  // Find the path to the executable.
  wxStandardPaths StdPaths;
  llvm::sys::Path ExecutablePath(StdPaths.GetExecutablePath().ToStdString());
  
  // Initialize the wxImage image handlers.
  wxInitAllImageHandlers();

  // Load ICU resources for TraceViewer.
  ICUResources.reset(new seec::ResourceLoader(ExecutablePath));
  
  std::array<char const *, 4> ResourceList {
    {"SeeCClang", "ClangEPV", "TraceViewer", "RuntimeErrors"}
  };
  
  if (!ICUResources->loadResources(ResourceList))
    HandleFatalError("Couldn't load resources!");
  
  // Enable wxWidgets virtual file system access to the ICU bundles.
  wxFileSystem::AddHandler(new seec::ICUBundleFSHandler());
  
  // Get the GUIText from the TraceViewer ICU resources.
  UErrorCode Status = U_ZERO_ERROR;
  auto TextTable = seec::getResource("TraceViewer",
                                     Locale::getDefault(),
                                     Status,
                                     "GUIText");
  if (U_FAILURE(Status))
    HandleFatalError("Couldn't load resource bundle TraceViewer->GUIText!");

  // Setup OS-X behaviour.
#ifdef __WXMAC__
  wxApp::SetExitOnFrameDelete(false);

  // Setup common menus.
  auto menuFile = new wxMenu();
  menuFile->Append(wxID_OPEN);
  menuFile->AppendSeparator();
  menuFile->Append(wxID_EXIT);

  auto menuBar = new wxMenuBar();
  menuBar->Append(menuFile,
                  seec::getwxStringExOrEmpty(TextTable, "Menu_File"));

  wxMenuBar::MacSetCommonMenuBar(menuBar);
#endif

  // Setup the welcome frame.
  Welcome = new WelcomeFrame(nullptr,
                             wxID_ANY,
                             seec::getwxStringExOrEmpty(TextTable,
                                                        "Welcome_Title"),
                             wxDefaultPosition,
                             wxDefaultSize);
  Welcome->Show(true);

#ifdef SEEC_SHOW_DEBUG
  // Setup the debugging log window.
  LogWindow = new wxLogWindow(nullptr, "Log");
#endif

  return true;
}

void TraceViewerApp::MacNewFile() {
  // TODO
  wxLogDebug("NewFile");
}

void TraceViewerApp::MacOpenFiles(wxArrayString const &FileNames) {
  // TODO: In the future we could check if the files are source files, in which
  // case we might compile them for the user (and possibly automatically
  // generate a trace file).
  for (wxString const &FileName : FileNames) {
    OpenFile(FileName);
  }
}

void TraceViewerApp::MacOpenFile(wxString const &FileName) {
  OpenFile(FileName);
}

void TraceViewerApp::MacReopenApp() {  
  if (TopLevelFrames.size() == 0) {
    // Re-open welcome frame, if it exists.
    if (Welcome)
      Welcome->Show(true);
  }
}

void TraceViewerApp::OnCommandOpen(wxCommandEvent &WXUNUSED(Event)) {
  UErrorCode Status = U_ZERO_ERROR;
  auto TextTable = seec::getResource("TraceViewer",
                                     Locale::getDefault(),
                                     Status,
                                     "GUIText");
  assert(U_SUCCESS(Status));

  // Create the open file dialog.
  wxFileDialog *OpenDialog
    = new wxFileDialog(nullptr,
                       seec::getwxStringExOrDie(TextTable,
                                                "OpenTrace_Title"),
                       wxEmptyString,
                       wxEmptyString,
                       seec::getwxStringExOrDie(TextTable,
                                                "OpenTrace_FileType"),
                       wxFD_OPEN,
                       wxDefaultPosition);

  // Destroy the dialog when we leave this scope.
  auto DestroyDialog = seec::scopeExit([=](){OpenDialog->Destroy();});

  // Show the dialog and exit if the user didn't select a file.
  if (OpenDialog->ShowModal() != wxID_OK)
    return;

  OpenFile(OpenDialog->GetPath());
}

void TraceViewerApp::OnCommandExit(wxCommandEvent &WXUNUSED(Event)) {
#ifdef __WXMAC_
  wxApp::SetExitOnFrameDelete(true);
#endif

#ifdef SEEC_SHOW_DEBUG
  if (LogWindow) {
    if (auto Frame = LogWindow->GetFrame()) {
      Frame->Close(true);
    }
  }
#endif

  if (Welcome) {
    Welcome->Close(true);
    Welcome = nullptr;
  }

  for (auto Frame : TopLevelFrames) {
    Frame->Close(true);
  }
  TopLevelFrames.clear();
    
  std::exit(EXIT_SUCCESS);
}

void TraceViewerApp::HandleFatalError(wxString Description) {
  // Show an error dialog for the user.
  auto ErrorDialog = new wxMessageDialog(NULL,
                                         Description,
                                         "Fatal error!",
                                         wxOK,
                                         wxDefaultPosition);
  ErrorDialog->ShowModal();

  exit(EXIT_FAILURE);
}
