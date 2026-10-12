// Copyright 2016 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Original code copyright 2014 Foxit Software Inc. http://www.foxitsoftware.com

#ifndef CORE_FPDFDOC_CPDF_INTERACTIVEFORM_H_
#define CORE_FPDFDOC_CPDF_INTERACTIVEFORM_H_

#include <stddef.h>
#include <stdint.h>

#include <functional>
#include <map>
#include <memory>
#include <vector>

#include "core/fpdfapi/parser/cpdf_dictionary.h"
#include "core/fpdfapi/parser/fpdf_parser_decode.h"
#include "core/fpdfdoc/cpdf_defaultappearance.h"
#include "core/fpdfdoc/cpdf_formfield.h"
#include "core/fxcrt/fx_coordinates.h"
#include "core/fxcrt/fx_string.h"
#include "core/fxcrt/retain_ptr.h"
#include "core/fxcrt/span.h"
#include "core/fxcrt/unowned_ptr.h"

class CFieldTree;
class CFDF_Document;
class CPDF_Document;
class CPDF_Font;
class CPDF_FormControl;
class CPDF_Page;

class CPDF_InteractiveForm {
 public:
  class NotifierIface {
   public:
    virtual ~NotifierIface() = default;

    virtual bool BeforeValueChange(CPDF_FormField* field,
                                   const WideString& value) = 0;
    virtual void AfterValueChange(CPDF_FormField* field) = 0;
    virtual bool BeforeSelectionChange(CPDF_FormField* field,
                                       const WideString& value) = 0;
    virtual void AfterSelectionChange(CPDF_FormField* field) = 0;
    virtual void AfterCheckedStatusChange(CPDF_FormField* field) = 0;
    virtual void AfterFormReset(CPDF_InteractiveForm* form) = 0;
  };

  explicit CPDF_InteractiveForm(CPDF_Document* document);
  ~CPDF_InteractiveForm();

  static bool IsUpdateAPEnabled();
  static void SetUpdateAP(bool bUpdateAP);
  static RetainPtr<CPDF_Font> AddNativeInteractiveFormFont(
      CPDF_Document* document,
      ByteString* name_tag);
  // Adds a new /AcroForm dictionary to the root dictionary of `document`.
  // Returns the newly created dictionary.
  static RetainPtr<CPDF_Dictionary> InitAcroFormDict(CPDF_Document* document);

  size_t CountFields(const WideString& field_name) const;
  CPDF_FormField* GetField(size_t index, const WideString& field_name) const;
  CPDF_FormField* GetFieldByDict(const CPDF_Dictionary* field) const;

  const CPDF_FormControl* GetControlAtPoint(const CPDF_Page* page,
                                            const CFX_PointF& point,
                                            int* z_order) const;
  CPDF_FormControl* GetControlByDict(const CPDF_Dictionary* widget_dict) const;

  bool NeedConstructAP() const;
  int CountFieldsInCalculationOrder();
  CPDF_FormField* GetFieldInCalculationOrder(int index);
  int FindFieldInCalculationOrder(const CPDF_FormField* field);

  RetainPtr<CPDF_Font> GetFormFont(ByteString name_tag) const;
  RetainPtr<CPDF_Font> GetFontForElement(
      RetainPtr<CPDF_Dictionary> element) const;
  CPDF_DefaultAppearance GetDefaultAppearance() const;
  int GetFormAlignment() const;
  bool CheckRequiredFields(const std::vector<CPDF_FormField*>* fields,
                           bool bIncludeOrExclude) const;

  // EmbedPDF(forms-model): |skip_empty_required| preserves the historic
  // submission behavior of omitting required fields whose value is empty;
  // pass false for faithful interchange exports.
  std::unique_ptr<CFDF_Document> ExportToFDF(
      const WideString& pdf_path,
      bool skip_empty_required = true) const;
  std::unique_ptr<CFDF_Document> ExportToFDF(
      const WideString& pdf_path,
      const std::vector<CPDF_FormField*>& fields,
      bool bIncludeOrExclude,
      bool skip_empty_required = true) const;

  void ResetForm();
  void ResetForm(pdfium::span<CPDF_FormField*> fields, bool bIncludeOrExclude);

  void SetNotifierIface(NotifierIface* notify);
  void FixPageFields(CPDF_Page* page);

  // EmbedPDF(forms-model): load a widget annotation that is reachable from a
  // page's /Annots array but not from the /AcroForm /Fields tree. Climbs the
  // /Parent chain to the field root first, so sibling widgets of a
  // partially linked field reconcile onto a single logical field.
  // In-memory reconciliation only; never writes to the document.
  void ReconcileWidget(RetainPtr<const CPDF_Dictionary> widget_dict);

  // Wrap callbacks thru NotifierIface.
  bool NotifyBeforeValueChange(CPDF_FormField* field, const WideString& value);
  void NotifyAfterValueChange(CPDF_FormField* field);
  bool NotifyBeforeSelectionChange(CPDF_FormField* field,
                                   const WideString& value);
  void NotifyAfterSelectionChange(CPDF_FormField* field);
  void NotifyAfterCheckedStatusChange(CPDF_FormField* field);

  const std::vector<UnownedPtr<CPDF_FormControl>>& GetControlsForField(
      const CPDF_FormField* field);

  CPDF_Document* document() { return document_; }

 private:
  // EmbedPDF(forms-model): rebind an indirect dictionary to the document's
  // current view of that object number. On layer documents this returns the
  // promoted clone when one exists; references held by frozen base objects
  // resolve to stale frozen instances otherwise. Identity on plain documents.
  RetainPtr<const CPDF_Dictionary> ResolveCurrentDict(
      RetainPtr<const CPDF_Dictionary> dict) const;

  // EmbedPDF(reads-dont-write): loading the form walks const dictionaries.
  void LoadField(RetainPtr<const CPDF_Dictionary> field_dict, int nLevel);
  void AddTerminalField(RetainPtr<const CPDF_Dictionary> field_dict);
  CPDF_FormControl* AddControl(CPDF_FormField* field,
                               RetainPtr<CPDF_Dictionary> widget_dict);

  static bool s_bUpdateAP;

  ByteString encoding_;
  UnownedPtr<CPDF_Document> const document_;
  RetainPtr<CPDF_Dictionary> form_dict_;
  std::unique_ptr<CFieldTree> field_tree_;
  std::map<RetainPtr<const CPDF_Dictionary>,
           std::unique_ptr<CPDF_FormControl>,
           std::less<>>
      control_map_;
  // Points into |control_map_|.
  std::map<UnownedPtr<const CPDF_FormField>,
           std::vector<UnownedPtr<CPDF_FormControl>>,
           std::less<>>
      control_lists_;
  UnownedPtr<NotifierIface> form_notify_;
};

#endif  // CORE_FPDFDOC_CPDF_INTERACTIVEFORM_H_
