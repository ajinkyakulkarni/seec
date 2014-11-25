//===- lib/wxWidgets/AugmentResources.cpp ---------------------------------===//
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

#include "seec/wxWidgets/AugmentationCollectionDataViewModel.hpp"
#include "seec/wxWidgets/AugmentResources.hpp"
#include "seec/wxWidgets/StringConversion.hpp"
#include "seec/wxWidgets/XmlNodeIterator.hpp"
#include "seec/Util/MakeUnique.hpp"
#include "seec/Util/Range.hpp"

#include <wx/dataview.h>
#include <wx/dir.h>
#include <wx/filename.h>
#include <wx/log.h>
#include <wx/stdpaths.h>
#include <wx/xml/xml.h>

#include <algorithm>

using namespace std;

namespace seec {

bool isAugmentation(wxXmlDocument const &Doc)
{
  if (!Doc.IsOk())
    return false;

  auto const RootNode = Doc.GetRoot();
  if (!RootNode || RootNode->GetName() != "package")
    return false;

  if (!RootNode->HasAttribute("name") || !RootNode->HasAttribute("id"))
    return false;

  return true;
}


//------------------------------------------------------------------------------
// Augmentation
//------------------------------------------------------------------------------

Augmentation::Augmentation(std::unique_ptr<wxXmlDocument> Doc,
                           EKind const Kind,
                           std::string Path)
: m_XmlDocument(std::move(Doc)),
  m_Kind(Kind),
  m_Path(std::move(Path))
{}

Augmentation::~Augmentation() = default;

Maybe<Augmentation> Augmentation::fromDoc(std::unique_ptr<wxXmlDocument> Doc,
                                          EKind const Kind,
                                          std::string Path)
{
  if (!isAugmentation(*Doc))
    return Maybe<Augmentation>();

  return Augmentation(std::move(Doc), Kind, std::move(Path));
}

wxString Augmentation::getName() const
{
  return m_XmlDocument->GetRoot()->GetAttribute("name");
}

wxString Augmentation::getID() const
{
  return m_XmlDocument->GetRoot()->GetAttribute("id");
}

wxString Augmentation::getSource() const
{
  return m_XmlDocument->GetRoot()->GetAttribute("source");
}

wxDateTime Augmentation::getDownloaded() const
{
  auto const Root = m_XmlDocument->GetRoot();
  wxDateTime Ret;

  if (Root->HasAttribute("downloaded")) {
    Ret.ParseISOCombined(Root->GetAttribute("downloaded"));
  }

  return Ret;
}

unsigned Augmentation::getVersion() const
{
  auto const Root = m_XmlDocument->GetRoot();
  unsigned long Value = 0;

  if (Root->HasAttribute("version")) {
    if (!Root->GetAttribute("version").ToULong(&Value))
      Value = 0;
  }

  if (Value >= std::numeric_limits<unsigned>::max())
    Value = std::numeric_limits<unsigned>::max();

  return static_cast<unsigned>(Value);
}


//------------------------------------------------------------------------------
// AugmentationCollection
//------------------------------------------------------------------------------

AugmentationCollection::AugmentationCollection() = default;

AugmentationCollection::~AugmentationCollection() = default;

bool AugmentationCollection::loadFromDoc(std::unique_ptr<wxXmlDocument> Doc,
                                         Augmentation::EKind const Kind,
                                         std::string Path)
{
  auto MaybeAug = Augmentation::fromDoc(std::move(Doc), Kind, std::move(Path));
  if (!MaybeAug.assigned())
    return false;

  m_Augmentations.push_back(MaybeAug.move<Augmentation>());

  for (auto const L : m_Listeners)
    L->DocAppended(*this);

  // Check if this document aliases an existing document.
  auto const &NewAug = m_Augmentations.back();
  auto const It =
    find_if(m_ActiveAugmentations.begin(), m_ActiveAugmentations.end(),
      [this, &NewAug] (unsigned const Index) {
        return m_Augmentations[Index].getID() == NewAug.getID();
      });

  if (It == m_ActiveAugmentations.end()) {
    // This is a new document, make it active.
    m_ActiveAugmentations.push_back(m_Augmentations.size() - 1);
  }
  else if (NewAug.getVersion() > m_Augmentations[*It].getVersion()) {
    // This document aliases an existing one and is newer.
    *It = m_Augmentations.size() - 1;
  }

  return true;
}

void AugmentationCollection::loadFromFile(wxString const &Path,
                                          Augmentation::EKind const Kind)
{
  auto Doc = seec::makeUnique<wxXmlDocument>(Path);
  if (!Doc || !Doc->IsOk())
    return;

  loadFromDoc(std::move(Doc), Kind, Path.ToStdString());
}

void AugmentationCollection::loadFromDirectory(wxString const &DirPath,
                                               Augmentation::EKind const Kind)
{
  wxDir Dir(DirPath);
  if (!Dir.IsOpened())
    return;

  auto Path = wxFileName::DirName(DirPath);
  wxString File;
  bool GotFile = Dir.GetFirst(&File, "*.xml");

  while (GotFile) {
    Path.SetName(File);
    loadFromFile(Path.GetFullPath(), Kind);
    GotFile = Dir.GetNext(&File);
  }
}

void AugmentationCollection::loadFromResources(std::string const &ResourcePath)
{
  auto Path = wxFileName::DirName(ResourcePath);
  Path.AppendDir("augment");
  loadFromDirectory(Path.GetFullPath(), Augmentation::EKind::Resource);
}

wxString AugmentationCollection::getUserLocalDataDirForAugmentations()
{
  auto Path = wxFileName::DirName(wxStandardPaths::Get().GetUserLocalDataDir());
  Path.AppendDir("augment");
  return Path.GetFullPath();
}

void AugmentationCollection::loadFromUserLocalDataDir()
{
  loadFromDirectory(getUserLocalDataDirForAugmentations(),
                    Augmentation::EKind::UserLocal);
}

bool AugmentationCollection::deleteUserLocalAugmentation(unsigned const Index)
{
  if (Index >= m_Augmentations.size())
    return false;

  if (m_Augmentations[Index].getKind() != Augmentation::EKind::UserLocal)
    return false;

  if (!wxRemoveFile(m_Augmentations[Index].getPath()))
    return false;

  // Store the ID so we can activate an aliasing augmentation if this
  // augmentation was active prior to removal.
  auto const ID = m_Augmentations[Index].getID();

  m_Augmentations.erase(m_Augmentations.begin() + Index);

  for (auto const L : m_Listeners)
    L->DocDeleted(*this, Index);

  // Find the removed augmentation in the active augmentations.
  auto const It =
    find(m_ActiveAugmentations.begin(), m_ActiveAugmentations.end(), Index);

  // Update all active augmentation indices that were higher than the removed.
  for (auto &I : m_ActiveAugmentations)
    if (I > Index)
      --I;

  // Update the active index of the removed augmentation (or erase it if there
  // is no replacement augmentation).
  if (It != m_ActiveAugmentations.end()) {
    // Find the "max" augmentation, where an augmentation matching our removed
    // augmentation's ID is higher than any that does not match, and
    // augmentations that both match are ordered by their versions.
    auto const ReplacementIt =
      max_element(m_Augmentations.begin(), m_Augmentations.end(),
                  [&ID] (Augmentation const &A, Augmentation const &B) {
                    if (A.getID() != ID) return B.getID() == ID;
                    if (B.getID() != ID) return false;
                    return A.getVersion() < B.getVersion();
                  });

    // If we found a replacement augmentation then update the index, otherwise
    // simply remove it.
    if (ReplacementIt != m_Augmentations.end() && ReplacementIt->getID() == ID)
    {
      *It = std::distance(m_Augmentations.begin(), ReplacementIt);
    }
    else {
      m_ActiveAugmentations.erase(It);
    }
  }

  return true;
}

bool AugmentationCollection::isActive(unsigned const Index) const
{
  return find(m_ActiveAugmentations.begin(), m_ActiveAugmentations.end(), Index)
          != m_ActiveAugmentations.end();
}

bool getStringsForAugFromPackageForLocale(wxXmlNode *Augmentations,
                                          wxString const &Name,
                                          wxString const &ID,
                                          icu::Locale const &Loc,
                                          std::vector<wxString> &Out)
{
  auto const Key = (Loc == icu::Locale::getRoot()) ? "root" : Loc.getBaseName();

  auto const It = std::find_if(seec::wxXmlNodeIterator(Augmentations),
                               seec::wxXmlNodeIterator(nullptr),
                                [&] (wxXmlNode const &Node) {
                                  return Node.GetName() == "augmentations"
                                      && Node.GetAttribute("locale") == Key;
                                });

  if (It == seec::wxXmlNodeIterator(nullptr))
    return false;

  for (wxXmlNode &Entry : *It) {
    if (Entry.GetName() == Name && Entry.GetAttribute("id") == ID) {
      Out.push_back(Entry.GetNodeContent());
      return true;
    }
  }

  return false;
}

void getStringsForAugFromDoc(wxXmlDocument const &Doc,
                             wxString const &Name,
                             wxString const &ID,
                             icu::Locale const &Loc,
                             std::vector<wxString> &Out)
{
  auto const RootNode = Doc.GetRoot();
  auto const Augs = RootNode->GetChildren();

  if (getStringsForAugFromPackageForLocale(Augs, Name, ID, Loc, Out))
    return;

  if (Loc.getVariant()) {
    auto Fallback = icu::Locale(Loc.getLanguage(), Loc.getCountry());
    if (getStringsForAugFromPackageForLocale(Augs, Name, ID, Fallback, Out))
      return;
  }

  // Try without country (language only).
  if (Loc.getCountry()) {
    auto Fallback = icu::Locale(Loc.getLanguage());
    if (getStringsForAugFromPackageForLocale(Augs, Name, ID, Fallback, Out))
      return;
  }

  // Try the root locale.
  auto Fallback = icu::Locale::getRoot();
  getStringsForAugFromPackageForLocale(Augs, Name, ID, Fallback, Out);
}

wxString AugmentationCollection::getAugmentationFor(wxString const &Type,
                                                    wxString const &Identifier,
                                                    ::icu::Locale const &Loc)
const
{
  std::vector<wxString> Strings;
  for (auto const Index : m_ActiveAugmentations) {
    getStringsForAugFromDoc(m_Augmentations[Index].getXmlDocument(),
                            Type, Identifier, Loc, Strings);
  }

  wxString Combined;
  for (auto &String : Strings)
    Combined += String + "\n";
  return Combined;
}

UnicodeString
AugmentationCollection::getAugmentationFor(UnicodeString const &Type,
                                           UnicodeString const &Identifier)
const
{
  return toUnicodeString(getAugmentationFor(towxString(Type),
                                            towxString(Identifier),
                                            icu::Locale::getDefault()));
}

void AugmentationCollection::addListener(Listener * const TheListener)
{
  m_Listeners.push_back(TheListener);
}

void AugmentationCollection::removeListener(Listener * const TheListener)
{
  auto const It = std::find(m_Listeners.begin(), m_Listeners.end(),
                            TheListener);

  if (It != m_Listeners.end())
    m_Listeners.erase(It);
}

} // namespace seec
