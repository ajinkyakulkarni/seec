//===- tools/seec-trace-view/ActionReplay.cpp -----------------------------===//
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
#include "seec/Util/Parsing.hpp"
#include "seec/wxWidgets/StringConversion.hpp"
#include "seec/wxWidgets/XmlNodeIterator.hpp"

#include "ActionReplay.hpp"
#include "LocaleSettings.hpp"

#include "llvm/ADT/STLExtras.h"

#include <wx/gauge.h>
#include <wx/listctrl.h>

#include <iterator>
#include <string>


//------------------------------------------------------------------------------
// IEventHandler
//------------------------------------------------------------------------------

IEventHandler::~IEventHandler()
{}

seec::Error IEventHandler::error_attribute(std::string const &Name) const
{
  return seec::Error{
    seec::LazyMessageByRef::create("TraceViewer",
                                   {"ActionRecording", "ErrorAttribute"},
                                   std::make_pair("name", Name.c_str()))
  };
}


//------------------------------------------------------------------------------
// ActionEventListCtrl
//------------------------------------------------------------------------------

class ActionEventListCtrl : public wxListCtrl
{
  seec::wxXmlNodeIterator m_FirstEvent;
  
  long m_CurrentEvent;

public:
  ActionEventListCtrl()
  : m_FirstEvent(),
    m_CurrentEvent(-1)
  {}
  
  ActionEventListCtrl(wxWindow *Parent,
                      wxWindowID ID = wxID_ANY,
                      wxPoint const &Pos = wxDefaultPosition,
                      wxSize const &Size = wxDefaultSize)
  : ActionEventListCtrl()
  {
    Create(Parent, ID, Pos, Size);
  }
  
  bool Create(wxWindow *Parent,
              wxWindowID ID = wxID_ANY,
              wxPoint const &Pos = wxDefaultPosition,
              wxSize const &Size = wxDefaultSize)
  {
    if (!wxListCtrl::Create(Parent, ID, Pos, Size,
                            wxLC_REPORT | wxLC_VIRTUAL | wxLC_SINGLE_SEL))
      return false;
    
    AppendColumn("Time",    wxLIST_FORMAT_LEFT, wxLIST_AUTOSIZE_USEHEADER);
    AppendColumn("Handler", wxLIST_FORMAT_LEFT, wxLIST_AUTOSIZE_USEHEADER);
    AppendColumn("Attributes", wxLIST_FORMAT_LEFT, wxLIST_AUTOSIZE_USEHEADER);
    
    return true;
  }
  
  virtual wxString OnGetItemText(long Item, long Column) const override {
    // Find the event.
    auto EventIt = m_FirstEvent;
    std::advance(EventIt, Item);
    if (EventIt == seec::wxXmlNodeIterator())
      return wxEmptyString;
    
    switch (Column) {
      case 0:  return EventIt->GetAttribute("time");
      case 1:  return EventIt->GetAttribute("handler");
      case 2:
      {
        wxString RetVal;
        for (auto Att = EventIt->GetAttributes(); Att; Att = Att->GetNext()) {
          auto const &Name = Att->GetName();
          if (Name != "time" && Name != "handler")
            RetVal << Name << "=" << Att->GetValue() << ", ";
        }
        return RetVal;
      }
      default: return wxEmptyString;
    }
  }
  
  void SetFirstEvent(wxXmlNode *FirstEvent)
  {
    m_FirstEvent = seec::wxXmlNodeIterator(FirstEvent);
    auto const EventCount = std::distance(m_FirstEvent,
                                          seec::wxXmlNodeIterator());
    
    m_CurrentEvent = -1;
    
    SetItemCount(EventCount);
    if (EventCount > 0)
      RefreshItems(0, EventCount - 1);
  }
  
  void MoveToNextEvent()
  {
    // Deselect current event.
    if (m_CurrentEvent >= 0)
      SetItemState(m_CurrentEvent, 0, wxLIST_STATE_SELECTED);
    
    // Move to next event and select it.
    ++m_CurrentEvent;
    SetItemState(m_CurrentEvent, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
    EnsureVisible(m_CurrentEvent);
  }
};


//------------------------------------------------------------------------------
// ActionReplayFrame
//------------------------------------------------------------------------------

void ActionReplayFrame::ReplayEvent()
{
  assert(NextEvent);
  
  // Remember the time at which this event occurred.
  auto const TimeStr = NextEvent->GetAttribute("time").ToStdString();
  if (!seec::parseTo(TimeStr, LastEventTime)) {
    wxLogDebug("Couldn't get time for next event.");
  }
  
  auto const Handler = NextEvent->GetAttribute("handler").ToStdString();
  auto const HandlerIt = Handlers.find(Handler);
  if (HandlerIt == Handlers.end()) {
    wxLogDebug("Handler \"%s\" not found.", Handler);
    return;
  }
  
  // Dispatch to the handler.
  auto const MaybeError = HandlerIt->second->handle(*NextEvent);
  if (MaybeError.assigned<seec::Error>()) {
    wxLogDebug("Handler failed to replay event.");
    return;
  }
}

void ActionReplayFrame::MoveToNextEvent()
{
  if (!NextEvent)
    return;
  
  NextEvent = NextEvent->GetNext();
  
  GaugeEventProgress->SetValue(GaugeEventProgress->GetValue() + 1);
  EventList->MoveToNextEvent();
  
  if (!NextEvent) {
    ButtonPlay->Disable();
    ButtonPause->Disable();
    ButtonStep->Disable();
  }
}

void ActionReplayFrame::OnPlay(wxCommandEvent &)
{
  SetEventTimer();
}

void ActionReplayFrame::OnPause(wxCommandEvent &)
{
  if (EventTimer.IsRunning()) {
    EventTimer.Stop();
    ButtonPlay->Enable();
    ButtonPause->Disable();
  }
}

void ActionReplayFrame::OnStep(wxCommandEvent &)
{
  assert(NextEvent);
  
  ReplayEvent();
  MoveToNextEvent();
}

void ActionReplayFrame::SetEventTimer()
{
  assert(NextEvent && !EventTimer.IsRunning());
  
  ButtonPlay->Disable();
  ButtonPause->Enable();
  
  auto const NextTimeStr = NextEvent->GetAttribute("time").ToStdString();
  uint64_t NextTime = 1;
  if (!seec::parseTo(NextTimeStr, NextTime)) {
    wxLogDebug("Couldn't get time for next event.");
  }
  
  EventTimer.Start(NextTime - LastEventTime, wxTIMER_ONE_SHOT);
}

void ActionReplayFrame::OnEventTimer(wxTimerEvent &)
{
  ReplayEvent();
  MoveToNextEvent();
  
  if (NextEvent)
    SetEventTimer();
}

ActionReplayFrame::ActionReplayFrame()
: Trace(nullptr),
  ButtonPlay(nullptr),
  ButtonPause(nullptr),
  ButtonStep(nullptr),
  GaugeEventProgress(nullptr),
  EventList(nullptr),
  Handlers(),
  RecordDocument(llvm::make_unique<wxXmlDocument>()),
  NextEvent(nullptr),
  LastEventTime(0),
  EventTimer()
{}

ActionReplayFrame::ActionReplayFrame(wxWindow *Parent,
                                     seec::cm::ProcessTrace const &WithTrace)
: ActionReplayFrame()
{
  Create(Parent, WithTrace);
}

ActionReplayFrame::~ActionReplayFrame()
{}

bool ActionReplayFrame::Create(wxWindow *Parent,
                               seec::cm::ProcessTrace const &WithTrace)
{
  // Get the internationalized resources.
  UErrorCode Status = U_ZERO_ERROR;
  auto ICUTable = seec::getResource("TraceViewer",
                                    getLocale(),
                                    Status,
                                    "ActionRecording");
  if (U_FAILURE(Status))
    return false;
  
  // Create the underlying wxFrame.
  auto const Title = seec::getwxStringExOrEmpty(ICUTable, "ReplayFrameTitle");
  if (!wxFrame::Create(Parent, wxID_ANY, Title))
    return false;
  
  Trace = &WithTrace;
  
  // Top level sizer for this frame.
  auto const SizerTopLevel = new wxBoxSizer(wxVERTICAL);
  
  // Add buttons for play, pause, step.
  auto const SizerForButtons = new wxBoxSizer(wxHORIZONTAL);
  
  auto const PlayText  = seec::getwxStringExOrEmpty(ICUTable, "ButtonPlay");
  auto const PauseText = seec::getwxStringExOrEmpty(ICUTable, "ButtonPause");
  auto const StepText  = seec::getwxStringExOrEmpty(ICUTable, "ButtonStep");
  
  ButtonPlay  = new wxButton(this, wxID_ANY, PlayText);
  ButtonPause = new wxButton(this, wxID_ANY, PauseText);
  ButtonStep  = new wxButton(this, wxID_ANY, StepText);
  
  ButtonPlay->Bind(wxEVT_BUTTON, &ActionReplayFrame::OnPlay, this);
  ButtonPause->Bind(wxEVT_BUTTON, &ActionReplayFrame::OnPause, this);
  ButtonStep->Bind(wxEVT_BUTTON, &ActionReplayFrame::OnStep, this);
  
  SizerForButtons->Add(ButtonPlay,  wxSizerFlags());
  SizerForButtons->Add(ButtonPause, wxSizerFlags());
  SizerForButtons->Add(ButtonStep,  wxSizerFlags());
  
  SizerTopLevel->Add(SizerForButtons, wxSizerFlags());
  
  // Add the progress gauge.
  GaugeEventProgress = new wxGauge(this, wxID_ANY, 1);
  SizerTopLevel->Add(GaugeEventProgress, wxSizerFlags().Expand());
  
  // Add the event list.
  EventList = new ActionEventListCtrl(this);
  SizerTopLevel->Add(EventList, wxSizerFlags().Proportion(1).Expand());
  
  SetSizerAndFit(SizerTopLevel);
  
  // Bind the close event to hide the frame (only destroy it if the parent is
  // being closed).
  Bind(wxEVT_CLOSE_WINDOW,
      std::function<void (wxCloseEvent &)>{
        [this] (wxCloseEvent &Event) {
          if (Event.CanVeto()) {
            Event.Veto();
            Hide();
          }
          else {
            Event.Skip();
          }
        }});
  
  EventTimer.Bind(wxEVT_TIMER, &ActionReplayFrame::OnEventTimer, this);
  
  return true;
}

std::size_t countChildren(wxXmlNode *Node)
{
  if (!Node)
    return 0;
  
  return std::distance(begin(*Node), end(*Node));
}

bool ActionReplayFrame::LoadRecording(wxXmlDocument const &Recording)
{
  UErrorCode Status = U_ZERO_ERROR;
  auto TextTable = seec::getResource("TraceViewer",
                                     getLocale(),
                                     Status,
                                     "ActionRecording");
  assert(U_SUCCESS(Status));
  
  // Copy the recording.
  RecordDocument = llvm::make_unique<wxXmlDocument>(Recording);
  
  auto const Root = RecordDocument->GetRoot();
  if (!Root || Root->GetName() != "recording") {
    auto const ErrorMessage =
      seec::getwxStringExOrEmpty(TextTable, "ReplayFileInvalid");
    wxMessageDialog ErrorDialog{nullptr, ErrorMessage};
    ErrorDialog.ShowModal();
    return false;
  }
  
  GaugeEventProgress->SetRange(countChildren(Root));
  GaugeEventProgress->SetValue(0);
  NextEvent = Root->GetChildren();
  
  EventList->SetFirstEvent(NextEvent);
  
  Show();
  
  return true;
}
