// Copyright 2016 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Original code copyright 2014 Foxit Software Inc. http://www.foxitsoftware.com

#ifndef CORE_FPDFAPI_PAGE_CPDF_FORM_H_
#define CORE_FPDFAPI_PAGE_CPDF_FORM_H_

#include <stdint.h>

#include <set>
#include <utility>

#include "core/fpdfapi/font/cpdf_font.h"
#include "core/fpdfapi/page/cpdf_pageobjectholder.h"
#include "core/fxcrt/retain_ptr.h"
#include "core/fxcrt/unowned_ptr.h"

class CFX_Matrix;
class CPDF_AllStates;
// EmbedPDF(page-residency): see RecursionState.
class CPDF_ContentVersions;
class CPDF_Dictionary;
class CPDF_Document;
class CPDF_Stream;
class CPDF_Type3Char;
struct CPDF_ParsedSize;

class CPDF_Form final : public CPDF_PageObjectHolder,
                        public CPDF_Font::FormIface {
 public:
  struct RecursionState {
    RecursionState();
    ~RecursionState();

    std::set<const uint8_t*> parsed_set;
    // EmbedPDF(page-residency): set when the parse fills a page, and shared
    // by the parses of the forms on it: they count what they add and record
    // the versions of the streams they read
    // (CPDF_Page::StartContentRecords()).
    UnownedPtr<CPDF_ParsedSize> parsed_size;
    UnownedPtr<CPDF_ContentVersions> content_versions;
  };

  // EmbedPDF(reads-dont-write): const, so choosing resources never calls a
  // mutable getter.
  // Helper method to choose the first non-null resources dictionary.
  static const CPDF_Dictionary* ChooseResourcesDict(
      const CPDF_Dictionary* pResources,
      const CPDF_Dictionary* pParentResources,
      const CPDF_Dictionary* pPageResources);

  // EmbedPDF(faithful-content-rewrite): the dictionary of `type` (/Font,
  // /ColorSpace, ...) content looks names up in: that of `resources`, the
  // ones ChooseResourcesDict() chose, or when they have none of that type,
  // the page's.
  static RetainPtr<const CPDF_Dictionary> ChooseResourcesOfType(
      const CPDF_Dictionary* resources,
      const CPDF_Dictionary* page_resources,
      ByteStringView type);

  CPDF_Form(CPDF_Document* document,
            RetainPtr<CPDF_Dictionary> pPageResources,
            RetainPtr<CPDF_Stream> pFormStream);
  CPDF_Form(CPDF_Document* document,
            RetainPtr<CPDF_Dictionary> pPageResources,
            RetainPtr<CPDF_Stream> pFormStream,
            CPDF_Dictionary* pParentResources);
  ~CPDF_Form() override;

  // CPDF_Font::FormIface:
  void ParseContentForType3Char(CPDF_Type3Char* pType3Char) override;
  bool HasPageObjects() const override;
  CFX_FloatRect CalcBoundingBox() const override;
  std::optional<std::pair<RetainPtr<CFX_DIBitmap>, CFX_Matrix>>
  GetBitmapAndMatrixFromSoleImageOfForm() const override;

  // EmbedPDF(faithful-content-rewrite, layer-door): content regeneration
  // writes a form into its own stream: the writable version the door gives.
  // CPDF_PageObjectHolder:
  RetainPtr<CPDF_Stream> GetMutableFormStream() override;

  void ParseContent();
  void ParseContent(const CPDF_AllStates* pGraphicStates,
                    const CFX_Matrix* pParentMatrix,
                    RecursionState* recursion_state);

  // EmbedPDF(page-residency): starts the parse ParseContent() makes and
  // leaves the rest to ContinueParse(), for a form parsed after the content
  // that placed it (CPDF_StreamContentParser::ParseDeferredForms()).
  void StartParseContent(const CPDF_AllStates* pGraphicStates,
                         RecursionState* recursion_state);

  // Never returns nullptr.
  RetainPtr<const CPDF_Stream> GetStream() const;

  // EmbedPDF(page-residency): the stream version the form's objects were
  // parsed from or last written to, without looking for a newer one:
  // GetStream() does, and drops the parsed objects when it finds one.
  RetainPtr<const CPDF_Stream> GetParsedStream() const;

  // EmbedPDF(redaction, faithful-content-rewrite): a form placed more than
  // once is copied before its content is regenerated.
  // Rebinds this parsed form to a private clone of its backing stream. Call
  // before serializing per-placement edits so another use of the same Form
  // XObject cannot be changed or overwrite this form's sanitized content.
  // A form already drawing its clone keeps it.
  bool CloneBackingStreamForWrite();

 private:
  // EmbedPDF(layer-door): open the form's dictionary and resources for
  // writing on their first actual write.
  // CPDF_PageObjectHolder:
  void EnsureMutableBackingObjectForDict() override;
  void EnsureMutableBackingObjectForResources() override;

  // EmbedPDF(overlay-epoch): point the form at another version of its stream.
  void RebindFormStream(RetainPtr<CPDF_Stream> stream,
                        bool reset_parsed_content);
  void ParseContentInternal(const CPDF_AllStates* pGraphicStates,
                            const CFX_Matrix* pParentMatrix,
                            CPDF_Type3Char* pType3Char,
                            RecursionState* recursion_state);
  // EmbedPDF(page-residency): shared by ParseContent() and
  // StartParseContent().
  void StartParseContentInternal(RetainPtr<const CPDF_Stream> stream,
                                 const CPDF_AllStates* pGraphicStates,
                                 const CFX_Matrix* pParentMatrix,
                                 CPDF_Type3Char* pType3Char,
                                 RecursionState* recursion_state);

  RecursionState recursion_state_;
  // EmbedPDF(overlay-epoch): the stream and its resources are chosen again
  // when the overlay epoch moves; a stream without /Resources uses
  // `fallback_resources_`.
  RetainPtr<CPDF_Dictionary> const fallback_resources_;
  mutable RetainPtr<CPDF_Stream> form_stream_;
  mutable uint64_t form_stream_epoch_ = 0;
  // EmbedPDF(redaction, faithful-content-rewrite): the object number of the
  // clone CloneBackingStreamForWrite() made, if any.
  uint32_t private_stream_objnum_ = 0;
};

#endif  // CORE_FPDFAPI_PAGE_CPDF_FORM_H_
