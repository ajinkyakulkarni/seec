//===- tools/seec-trace-view/ActionRecordSettings.cpp ---------------------===//
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

#include "seec/wxWidgets/StringConversion.hpp"
#include "seec/Util/Error.hpp"

#include <wx/config.h>
#include <wx/dialog.h>
#include <wx/dir.h>
#include <wx/filename.h>
#include <wx/stattext.h>
#include <wx/stdpaths.h>

#include <curl/curl.h>

#include "ActionRecordSettings.hpp"
#include "LocaleSettings.hpp"
#include "TraceViewerApp.hpp"

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>


// These constants control the size of submitted recordings, in MiB.
constexpr std::size_t cRecordSizeMinimum = 1;
constexpr std::size_t cRecordSizeMaximum = 1000;
constexpr std::size_t cRecordSizeDefault = 100;

// These constants control the size of locally stored recordings, in MiB.
constexpr std::size_t cRecordStoreMinimum = 1;
constexpr std::size_t cRecordStoreMaximum = 10000;
constexpr std::size_t cRecordStoreDefault = 1000;

char const * const cConfigKeyForToken = "/UserActionRecording/Token";
char const * const cConfigKeyForSizeLimit = "/UserActionRecording/SizeLimit";
char const * const cConfigKeyForStoreLimit = "/UserActionRecording/StoreLimit";


#define	MODKEY		10430227

static bool validate(const char key[])
{
//  cf:  www.opensource.apple.com/source/xnu/xnu-1456.1.26/bsd/libkern/crc32.c
    static uint32_t	table_crc32[256] = {
	0x00000000, 0x77073096, 0xee0e612c, 0x990951ba, 0x076dc419, 0x706af48f,
	0xe963a535, 0x9e6495a3,	0x0edb8832, 0x79dcb8a4, 0xe0d5e91e, 0x97d2d988,
	0x09b64c2b, 0x7eb17cbd, 0xe7b82d07, 0x90bf1d91, 0x1db71064, 0x6ab020f2,
	0xf3b97148, 0x84be41de,	0x1adad47d, 0x6ddde4eb, 0xf4d4b551, 0x83d385c7,
	0x136c9856, 0x646ba8c0, 0xfd62f97a, 0x8a65c9ec,	0x14015c4f, 0x63066cd9,
	0xfa0f3d63, 0x8d080df5,	0x3b6e20c8, 0x4c69105e, 0xd56041e4, 0xa2677172,
	0x3c03e4d1, 0x4b04d447, 0xd20d85fd, 0xa50ab56b,	0x35b5a8fa, 0x42b2986c,
	0xdbbbc9d6, 0xacbcf940,	0x32d86ce3, 0x45df5c75, 0xdcd60dcf, 0xabd13d59,
	0x26d930ac, 0x51de003a, 0xc8d75180, 0xbfd06116, 0x21b4f4b5, 0x56b3c423,
	0xcfba9599, 0xb8bda50f, 0x2802b89e, 0x5f058808, 0xc60cd9b2, 0xb10be924,
	0x2f6f7c87, 0x58684c11, 0xc1611dab, 0xb6662d3d,	0x76dc4190, 0x01db7106,
	0x98d220bc, 0xefd5102a, 0x71b18589, 0x06b6b51f, 0x9fbfe4a5, 0xe8b8d433,
	0x7807c9a2, 0x0f00f934, 0x9609a88e, 0xe10e9818, 0x7f6a0dbb, 0x086d3d2d,
	0x91646c97, 0xe6635c01, 0x6b6b51f4, 0x1c6c6162, 0x856530d8, 0xf262004e,
	0x6c0695ed, 0x1b01a57b, 0x8208f4c1, 0xf50fc457, 0x65b0d9c6, 0x12b7e950,
	0x8bbeb8ea, 0xfcb9887c, 0x62dd1ddf, 0x15da2d49, 0x8cd37cf3, 0xfbd44c65,
	0x4db26158, 0x3ab551ce, 0xa3bc0074, 0xd4bb30e2, 0x4adfa541, 0x3dd895d7,
	0xa4d1c46d, 0xd3d6f4fb, 0x4369e96a, 0x346ed9fc, 0xad678846, 0xda60b8d0,
	0x44042d73, 0x33031de5, 0xaa0a4c5f, 0xdd0d7cc9, 0x5005713c, 0x270241aa,
	0xbe0b1010, 0xc90c2086, 0x5768b525, 0x206f85b3, 0xb966d409, 0xce61e49f,
	0x5edef90e, 0x29d9c998, 0xb0d09822, 0xc7d7a8b4, 0x59b33d17, 0x2eb40d81,
	0xb7bd5c3b, 0xc0ba6cad, 0xedb88320, 0x9abfb3b6, 0x03b6e20c, 0x74b1d29a,
	0xead54739, 0x9dd277af, 0x04db2615, 0x73dc1683, 0xe3630b12, 0x94643b84,
	0x0d6d6a3e, 0x7a6a5aa8, 0xe40ecf0b, 0x9309ff9d, 0x0a00ae27, 0x7d079eb1,
	0xf00f9344, 0x8708a3d2, 0x1e01f268, 0x6906c2fe, 0xf762575d, 0x806567cb,
	0x196c3671, 0x6e6b06e7, 0xfed41b76, 0x89d32be0, 0x10da7a5a, 0x67dd4acc,
	0xf9b9df6f, 0x8ebeeff9, 0x17b7be43, 0x60b08ed5, 0xd6d6a3e8, 0xa1d1937e,
	0x38d8c2c4, 0x4fdff252, 0xd1bb67f1, 0xa6bc5767, 0x3fb506dd, 0x48b2364b,
	0xd80d2bda, 0xaf0a1b4c, 0x36034af6, 0x41047a60, 0xdf60efc3, 0xa867df55,
	0x316e8eef, 0x4669be79, 0xcb61b38c, 0xbc66831a, 0x256fd2a0, 0x5268e236,
	0xcc0c7795, 0xbb0b4703, 0x220216b9, 0x5505262f, 0xc5ba3bbe, 0xb2bd0b28,
	0x2bb45a92, 0x5cb36a04, 0xc2d7ffa7, 0xb5d0cf31, 0x2cd99e8b, 0x5bdeae1d,
	0x9b64c2b0, 0xec63f226, 0x756aa39c, 0x026d930a, 0x9c0906a9, 0xeb0e363f,
	0x72076785, 0x05005713, 0x95bf4a82, 0xe2b87a14, 0x7bb12bae, 0x0cb61b38,
	0x92d28e9b, 0xe5d5be0d, 0x7cdcefb7, 0x0bdbdf21, 0x86d3d2d4, 0xf1d4e242,
	0x68ddb3f8, 0x1fda836e, 0x81be16cd, 0xf6b9265b, 0x6fb077e1, 0x18b74777,
	0x88085ae6, 0xff0f6a70, 0x66063bca, 0x11010b5c, 0x8f659eff, 0xf862ae69,
	0x616bffd3, 0x166ccf45, 0xa00ae278, 0xd70dd2ee, 0x4e048354, 0x3903b3c2,
	0xa7672661, 0xd06016f7, 0x4969474d, 0x3e6e77db, 0xaed16a4a, 0xd9d65adc,
	0x40df0b66, 0x37d83bf0, 0xa9bcae53, 0xdebb9ec5, 0x47b2cf7f, 0x30b5ffe9,
	0xbdbdf21c, 0xcabac28a, 0x53b39330, 0x24b4a3a6, 0xbad03605, 0xcdd70693,
	0x54de5729, 0x23d967bf, 0xb3667a2e, 0xc4614ab8, 0x5d681b02, 0x2a6f2b94,
	0xb40bbe37, 0xc30c8ea1, 0x5a05df1b, 0x2d02ef8d
    };

#define	INIT_CRC32	0xFFFFFFFF
    uint32_t	crc32 = INIT_CRC32;
#undef	INIT_CRC32

    while(*key) {
	crc32	= (crc32 >> 8) ^ table_crc32[ (crc32 ^ *key) & 0xFF ];
	++key;
    }
    crc32	= crc32^0xFFFFFFFF;

    return ((crc32 & MODKEY) == 0);
}
#undef	MODKEY


/// \brief Shows settings for user action recording.
///
class ActionRecordSettingsDlg : public wxDialog
{
  /// User's token.
  wxTextCtrl *TokenText;
  
  /// Size limit of recordings to submit over the network.
  wxSlider *SizeSlider;
  
  /// Size limit of recordings stored locally (awaiting submission).
  wxSlider *StoreSlider;
  
public:
  /// \brief Constructor (without creation).
  ///
  ActionRecordSettingsDlg();
  
  /// \brief Constructor (with creation).
  ///
  ActionRecordSettingsDlg(wxWindow *Parent);
  
  /// \brief Destructor.
  ///
  virtual ~ActionRecordSettingsDlg();
  
  /// \brief Create the frame.
  ///
  bool Create(wxWindow *Parent);
  
  /// \brief Save the current values into the config.
  ///
  bool SaveValues();
};

ActionRecordSettingsDlg::ActionRecordSettingsDlg()
: TokenText(nullptr),
  SizeSlider(nullptr),
  StoreSlider(nullptr)
{}

ActionRecordSettingsDlg::ActionRecordSettingsDlg(wxWindow *Parent)
: ActionRecordSettingsDlg()
{
  Create(Parent);
}

ActionRecordSettingsDlg::~ActionRecordSettingsDlg()
{
  // Notify the TraceViewerApp that we no longer exist.
  auto &App = wxGetApp();
  App.removeTopLevelWindow(this);
}

bool ActionRecordSettingsDlg::Create(wxWindow *Parent)
{
  UErrorCode Status = U_ZERO_ERROR;
  auto const TextTable =
    seec::getResource("TraceViewer", getLocale(), Status,
                      "GUIText", "RecordingSettingsDialog");
  if (U_FAILURE(Status))
    return false;
  
  // Get the title.
  auto const Title = seec::getwxStringExOrEmpty(TextTable, "Title");
  
  if (!wxDialog::Create(Parent,
                        wxID_ANY,
                        Title,
                        wxDefaultPosition,
                        wxDefaultSize))
    return false;
  
  // Notify the TraceViewerApp that we exist.
  auto &App = wxGetApp();
  App.addTopLevelWindow(this);
  
  // Create the token input.
  TokenText = new wxTextCtrl(this,
                             wxID_ANY,
                             getActionRecordToken(),
                             wxDefaultPosition,
                             wxDefaultSize,
                             0 /* style */,
                             wxDefaultValidator);
  
  auto const TokenStr = seec::getwxStringExOrEmpty(TextTable, "Token");
  auto const TokenLabel = new wxStaticText(this, wxID_ANY, TokenStr);
  
  // Create the size limit slider.
  SizeSlider = new wxSlider(this,
                            wxID_ANY,
                            getActionRecordSizeLimit(),
                            cRecordSizeMinimum,
                            cRecordSizeMaximum,
                            wxDefaultPosition,
                            wxDefaultSize,
                            wxSL_HORIZONTAL | wxSL_LABELS);
  
  auto const SizeStr = seec::getwxStringExOrEmpty(TextTable, "MaximumSize");
  auto const SizeLabel = new wxStaticText(this, wxID_ANY, SizeStr);
  
  // Create the size limit slider.
  StoreSlider = new wxSlider(this,
                             wxID_ANY,
                             getActionRecordStoreLimit(),
                             cRecordStoreMinimum,
                             cRecordStoreMaximum,
                             wxDefaultPosition,
                             wxDefaultSize,
                             wxSL_HORIZONTAL | wxSL_LABELS);
  
  auto const StoreStr = seec::getwxStringExOrEmpty(TextTable, "MaximumStore");
  auto const StoreLabel = new wxStaticText(this, wxID_ANY, StoreStr);
  
  // Create accept/cancel buttons.
  auto const Buttons = wxDialog::CreateStdDialogButtonSizer(wxOK | wxCANCEL);
  
  // Vertical sizer to hold each row of input.
  auto const ParentSizer = new wxBoxSizer(wxVERTICAL);
  
  int const BorderDir = wxLEFT | wxRIGHT;
  int const BorderSize = 5;
  int const InterSettingSpace = 10;
  
  ParentSizer->Add(TokenLabel, wxSizerFlags().Border(BorderDir | wxTOP,
                                                     BorderSize));
  ParentSizer->Add(TokenText, wxSizerFlags().Expand()
                                            .Border(BorderDir, BorderSize));
  
  ParentSizer->AddSpacer(InterSettingSpace);

  ParentSizer->Add(SizeLabel, wxSizerFlags().Border(BorderDir, BorderSize));
  ParentSizer->Add(SizeSlider, wxSizerFlags().Expand()
                                             .Border(BorderDir, BorderSize));
  
  ParentSizer->AddSpacer(InterSettingSpace);
  
  ParentSizer->Add(StoreLabel, wxSizerFlags().Border(BorderDir, BorderSize));
  ParentSizer->Add(StoreSlider, wxSizerFlags().Expand()
                                              .Border(BorderDir, BorderSize));
  
  ParentSizer->AddSpacer(InterSettingSpace);
  
  ParentSizer->Add(Buttons, wxSizerFlags().Expand()
                                          .Border(BorderDir | wxBOTTOM,
                                                  BorderSize));
  
  SetSizerAndFit(ParentSizer);
  
  return true;
}

bool ActionRecordSettingsDlg::SaveValues()
{
  wxString const Token = TokenText->GetValue();
  if (!validate(Token.c_str())) {
    wxMessageDialog Dlg(this, "The user token is invalid.");
    Dlg.ShowModal();
    return false;
  }
  
  long const SizeLimit = SizeSlider->GetValue();
  long const StoreLimit = StoreSlider->GetValue();
  
  auto const Config = wxConfig::Get();
  Config->Write(cConfigKeyForToken, Token);
  Config->Write(cConfigKeyForSizeLimit, SizeLimit);
  Config->Write(cConfigKeyForStoreLimit, StoreLimit);
  Config->Flush();
  
  return true;
}

void showActionRecordSettings()
{
  ActionRecordSettingsDlg Dlg(nullptr);
  
  while (true) {
    auto const Result = Dlg.ShowModal();
    
    if (Result == wxID_OK)
      if (!Dlg.SaveValues())
        continue;
    
    break;
  }
}

wxString getActionRecordToken()
{
  auto const Config = wxConfig::Get();
  
  wxString CurrentToken;
  Config->Read(cConfigKeyForToken, &CurrentToken);
  
  if (!validate(CurrentToken.c_str()))
    CurrentToken.clear();
  
  return CurrentToken;
}

bool hasValidActionRecordToken()
{
  return !getActionRecordToken().empty();
}

long getActionRecordSizeLimit()
{
  auto const Config = wxConfig::Get();
  
  return Config->ReadLong(cConfigKeyForSizeLimit, cRecordSizeDefault);
}

long getActionRecordStoreLimit()
{
  auto const Config = wxConfig::Get();
  
  return Config->ReadLong(cConfigKeyForStoreLimit, cRecordStoreDefault);
}


//===----------------------------------------------------------------------===//
// ActionRecordingSubmitter
//===----------------------------------------------------------------------===//

//  ---------------------------------------------------------------------
// cf   http://curl.haxx.se/libcurl/c/httpput.html
// and  http://curl.haxx.se/libcurl/c/postit2.html

/// \brief Implementation of action recording submission handler.
///
class ActionRecordingSubmitterImpl final
{
  static char const * const getSubmissionURL() {
    return "https://secure.csse.uwa.edu.au/run/seeclogd";
  }

  std::thread WorkerThread;

  /// Used by GUI thread to notify worker that it should shutdown.
  bool Shutdown;

  /// Used by GUI thread to send new recording paths to worker thread.
  std::vector<wxString> NewRecordings;

  std::mutex NotifyMutex;

  std::condition_variable NotifyCV;

  static wxFileName GetRecordingDirFileName()
  {
    wxFileName RecordingDirPath;
    RecordingDirPath.AssignDir(wxStandardPaths::Get().GetUserLocalDataDir());
    RecordingDirPath.AppendDir("recordings");
    return RecordingDirPath;
  }

  static std::vector<wxString> WorkerGetAllRecordingFiles()
  {
    std::vector<wxString> RecordingFiles;

    wxDir RecordingDir(GetRecordingDirFileName().GetFullPath());
    if (!RecordingDir.IsOpened())
      return RecordingFiles;

    wxString RecordingPath;
    bool GotFile = RecordingDir.GetFirst(&RecordingPath, "*.seecrecord");

    while (GotFile) {
      RecordingFiles.emplace_back(RecordingPath);
      GotFile = RecordingDir.GetNext(&RecordingPath);
    }

    return RecordingFiles;
  }

  static int WorkerProgress(void *OwnerPtr,
                            double const dltotal,
                            double const dlnow,
                            double const ultotal,
                            double const ulnow)
  {
    assert(OwnerPtr);

    auto &Owner = *reinterpret_cast<ActionRecordingSubmitterImpl *>(OwnerPtr);

    std::lock_guard<std::mutex> Lock(Owner.NotifyMutex);
    if (Owner.Shutdown)
      return 1; // Abort the transfer.

    return 0;
  }

  enum class EWorkerSendResult {
    TerminatedByShutdown,
    Failure,
    Success
  };

  EWorkerSendResult WorkerSendRecording(wxString const &Filename)
  {
    CURL *curl = curl_easy_init();
    if (!curl)
      return EWorkerSendResult::Failure;

    auto const Token = getActionRecordToken().ToStdString();

    auto const Path = [&] () -> wxFileName {
      auto RecordingFileName = GetRecordingDirFileName();
      RecordingFileName.SetFullName(Filename);
      return RecordingFileName;
    } ();

    struct curl_httppost *post = nullptr;
    struct curl_httppost *last = nullptr;

    curl_formadd(&post, &last,
                 CURLFORM_COPYNAME,     "seecticket",
                 CURLFORM_COPYCONTENTS, Token.c_str(),
                 CURLFORM_END);

    curl_formadd(&post, &last,
                 CURLFORM_COPYNAME,     "seectime",
                 CURLFORM_COPYCONTENTS,
                 static_cast<char const *>(Path.GetName().utf8_str()),
                 CURLFORM_END);

    curl_formadd(&post, &last,
                 CURLFORM_COPYNAME,    Filename.ToStdString().c_str(),
                 CURLFORM_FILE,
                 static_cast<char const *>(Path.GetFullPath().utf8_str()),
                 CURLFORM_CONTENTTYPE, "application/octet-stream",
                 CURLFORM_END);

    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_URL,      getSubmissionURL());
    curl_easy_setopt(curl, CURLOPT_HTTPPOST, post);

    // Track progress so that we can display it to the user.
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS,       0);
    curl_easy_setopt(curl, CURLOPT_PROGRESSFUNCTION,
                     &ActionRecordingSubmitterImpl::WorkerProgress);
    curl_easy_setopt(curl, CURLOPT_PROGRESSDATA,     this);

    auto const Result = curl_easy_perform(curl);

    curl_easy_cleanup(curl);
    curl = nullptr;

    curl_formfree(post);

    switch (Result) {
      case CURLE_OK:
        return EWorkerSendResult::Success;

      case CURLE_ABORTED_BY_CALLBACK:
        return EWorkerSendResult::TerminatedByShutdown;

      default:
        wxLogDebug("CURL failure: %s", curl_easy_strerror(Result));
        return EWorkerSendResult::Failure;
    }
  }

  void WorkerImpl()
  {
    auto const &App = wxGetApp();
    if (App.checkCURL()) {
      auto AllRecordingFiles = WorkerGetAllRecordingFiles();

      while (true) {
        // Send all the recording files we know about.
        while (!AllRecordingFiles.empty()) {
          auto const RecordingFile = std::move(AllRecordingFiles.back());
          AllRecordingFiles.pop_back();

          auto const Result = WorkerSendRecording(RecordingFile);

          if (Result == EWorkerSendResult::TerminatedByShutdown) {
            break;
          }
          else if (Result == EWorkerSendResult::Failure) {
            // TODO?
          }
          else if (Result == EWorkerSendResult::Success) {
            auto Path = GetRecordingDirFileName();
            Path.SetFullName(RecordingFile);

            if (!wxRemoveFile(Path.GetFullPath())) {
              wxLogDebug("Couldn't remove file %s!", RecordingFile);
            }
          }
        }

        // Wait until either we get a new recording file, or we shutdown.
        std::unique_lock<std::mutex> Lock(NotifyMutex);
        while (!Shutdown && NewRecordings.empty())
          NotifyCV.wait(Lock);

        if (Shutdown)
          break;

        AllRecordingFiles.insert(AllRecordingFiles.end(),
                                 std::make_move_iterator(NewRecordings.begin()),
                                 std::make_move_iterator(NewRecordings.end()));
        NewRecordings.clear();
      }
    }
    else {
      wxLogDebug("cURL is not initialized.");
    }
  }

public:
  ActionRecordingSubmitterImpl()
  : WorkerThread(),
    Shutdown(false),
    NotifyMutex(),
    NotifyCV()
  {
    WorkerThread = std::thread([this] () { WorkerImpl(); });
  }

  ~ActionRecordingSubmitterImpl()
  {
    // Notify the worker that it should terminate, then wait.
    std::unique_lock<std::mutex> Lock(NotifyMutex);
    Shutdown = true;
    Lock.unlock();
    NotifyCV.notify_one();
    WorkerThread.join();
  }

  void notifyOfNewRecording(wxString const &FullPath)
  {
    // Just take the file name from the path (it *should* be in our recording
    // dir anyway).
    wxFileName RecordingPath = FullPath;
    std::unique_lock<std::mutex> Lock(NotifyMutex);
    NewRecordings.push_back(RecordingPath.GetFullName());
    Lock.unlock();
    NotifyCV.notify_one();
  }
};

ActionRecordingSubmitter::ActionRecordingSubmitter()
: pImpl(new ActionRecordingSubmitterImpl())
{}

ActionRecordingSubmitter::~ActionRecordingSubmitter() = default;

void ActionRecordingSubmitter::notifyOfNewRecording(wxString const &FullPath)
{
  pImpl->notifyOfNewRecording(FullPath);
}
