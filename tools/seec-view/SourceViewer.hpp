//===- tools/seec-trace-view/SourceViewer.hpp -----------------------------===//
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

#ifndef SEEC_TRACE_VIEW_SOURCEVIEWER_HPP
#define SEEC_TRACE_VIEW_SOURCEVIEWER_HPP

#include "seec/Util/Observer.hpp"

#include "llvm/ADT/StringRef.h"

#include <wx/wx.h>
#include <wx/panel.h>
#include <wx/aui/aui.h>
#include <wx/aui/auibook.h>

#include <map>
#include <memory>


// Forward declarations.

class ActionRecord;
class ActionReplayFrame;
class ColourSchemeSettings;
class ContextNotifier;
class OpenTrace;
class SourceFilePanel;
class StateAccessToken;

namespace seec {
  namespace cm {
    class FunctionState;
    class ProcessState;
    class RuntimeErrorState;
    class ThreadState;
  }
  namespace seec_clang {
    class MappedAST;
  }
}

namespace clang {
  class Decl;
  class FileEntry;
  class Stmt;
}


/// \brief SourceViewerPanel.
///
class SourceViewerPanel : public wxPanel
{
  /// Notebook that holds all of the source windows.
  wxAuiNotebook *Notebook;
  
  /// The currently associated trace information.
  OpenTrace *Trace;
  
  /// The central handler for context notifications.
  ContextNotifier *Notifier;
  
  /// Registration to ColourSchemeSettings changes.
  seec::observer::registration m_ColourSchemeSettingsRegistration;

  /// Used to record user interactions.
  ActionRecord *Recording;
  
  /// Lookup from file path to source window.
  std::map<clang::FileEntry const *, SourceFilePanel *> Pages;
  
  /// Token for accessing the current state.
  std::shared_ptr<StateAccessToken> CurrentAccess;
  
  /// \name Event handlers.
  /// @{

  void OnPageChanged(wxAuiNotebookEvent &Ev);

  /// @} (Event handlers.)


  /// \name Replay for \c SourceFilePanel events.
  /// @{

  void ReplayPageChanged(std::string &File);

  void ReplayMouseEnter(std::string &File);

  void ReplayMouseLeave(std::string &File);

  void ReplayMouseOverDecl(clang::Decl const *TheDecl);

  void ReplayMouseOverStmt(clang::Stmt const *TheStmt);

  /// @}

public:
  /// \name Recording for SourceFilePanel events.
  /// @{

  void OnMouseEnter(SourceFilePanel &Page);

  void OnMouseLeave(SourceFilePanel &Page);

  void OnMouseOver(SourceFilePanel &Page, clang::Decl const *Decl);

  void OnMouseOver(SourceFilePanel &Page, clang::Stmt const *Stmt);

  void OnRightClick(SourceFilePanel &Page, clang::Decl const *Decl);

  void OnRightClick(SourceFilePanel &Page, clang::Stmt const *Stmt);

  /// @} (Recording for SourceFilePanel events.)


  /// \brief Construct without creating.
  ///
  SourceViewerPanel();

  /// \brief Construct and create.
  ///
  SourceViewerPanel(wxWindow *Parent,
                    OpenTrace &TheTrace,
                    ContextNotifier &WithNotifier,
                    ActionRecord &WithRecording,
                    ActionReplayFrame &WithReplay,
                    wxWindowID ID = wxID_ANY,
                    wxPoint const &Position = wxDefaultPosition,
                    wxSize const &Size = wxDefaultSize);

  /// \brief Destructor.
  ///
  virtual ~SourceViewerPanel();

  /// \brief Create the panel.
  ///
  bool Create(wxWindow *Parent,
              OpenTrace &TheTrace,
              ContextNotifier &WithNotifier,
              ActionRecord &WithRecording,
              ActionReplayFrame &WithReplay,
              wxWindowID ID = wxID_ANY,
              wxPoint const &Position = wxDefaultPosition,
              wxSize const &Size = wxDefaultSize);
  
  
  /// \brief Get the currently associated trace.
  ///
  OpenTrace *getTrace() { return Trace; }


  /// \name Mutators.
  /// @{
  
  /// \brief Remove all files from the viewer.
  ///
  void clear();
  
  /// \brief Update this panel to reflect the given state.
  ///
  void show(std::shared_ptr<StateAccessToken> Access,
            seec::cm::ProcessState const &Process,
            seec::cm::ThreadState const &Thread);

  /// \brief Update the \c ColourSchemeSettings.
  ///
  void OnColourSchemeSettingsChanged(ColourSchemeSettings const &);

  /// @} (Mutators).

private:
  /// \name State display.
  /// @{

  /// \brief Show the given runtime error in the source code view.
  ///
  void showRuntimeError(seec::cm::RuntimeErrorState const &Error,
                        seec::cm::FunctionState const &InFunction);
  
  /// \brief Show the given Stmt in the source code view.
  ///
  void showActiveStmt(::clang::Stmt const *Statement,
                      ::seec::cm::FunctionState const &InFunction);
  
  /// \brief Show the given Decl in the source code view.
  ///
  void showActiveDecl(::clang::Decl const *Declaration,
                      ::seec::cm::FunctionState const &InFunction);
  
  /// @} (State display.)
  
  
  /// \name File management.
  /// @{

  /// \brief Show the given source file.
  ///
  SourceFilePanel *loadAndShowFile(clang::FileEntry const *File,
                                   seec::seec_clang::MappedAST const &MAST);
  
  /// @} (File management.)
  

  /// \name Highlighting.
  /// @{

  /// Set highlighting for a Decl.
  void highlightOn(::clang::Decl const *Decl);
  
  /// Set highlighting for a Stmt.
  void highlightOn(::clang::Stmt const *Stmt);
  
  /// Clear all highlighting.
  void highlightOff();
  
  /// @}  
};

#endif // SEEC_TRACE_VIEW_SOURCEVIEWER_HPP
