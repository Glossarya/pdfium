// Copyright 2016 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Original code copyright 2014 Foxit Software Inc. http://www.foxitsoftware.com

#include "core/fpdfdoc/cpvt_fontmap.h"

#include <utility>

#include "core/fpdfapi/font/cpdf_font.h"
#include "core/fpdfapi/parser/cpdf_dictionary.h"
#include "core/fpdfapi/parser/cpdf_document.h"
#include "core/fpdfapi/parser/cpdf_reference.h"
#include "core/fpdfapi/parser/fpdf_parser_utility.h"
#include "core/fpdfdoc/cpdf_interactiveform.h"
#include "core/fxcrt/check.h"
#include "core/fxcrt/fx_codepage.h"
#include "core/fxcrt/numerics/safe_conversions.h"

CPVT_FontMap::CPVT_FontMap(CPDF_Document* doc,
                           RetainPtr<CPDF_Dictionary> pResDict,
                           RetainPtr<CPDF_Font> pDefFont,
                           const ByteString& sDefFontAlias)
    : document_(doc),
      res_dict_(std::move(pResDict)),
      def_font_(std::move(pDefFont)),
      def_font_alias_(sDefFontAlias) {}

CPVT_FontMap::~CPVT_FontMap() = default;

void CPVT_FontMap::SetupAnnotSysPDFFont() {
  if (!document_ || !res_dict_) {
    return;
  }

  RetainPtr<CPDF_Font> pPDFFont =
      CPDF_InteractiveForm::AddNativeInteractiveFormFont(document_,
                                                         &sys_font_alias_);
  if (!pPDFFont) {
    return;
  }

  RetainPtr<CPDF_Dictionary> font_list = res_dict_->GetMutableDictFor("Font");
  if (ValidateFontResourceDict(font_list.Get()) &&
      !font_list->KeyExist(sys_font_alias_.AsStringView())) {
    font_list->SetNewFor<CPDF_Reference>(sys_font_alias_, document_,
                                         pPDFFont->GetFontDictObjNum());
  }
  sys_font_ = std::move(pPDFFont);
}

RetainPtr<CPDF_Font> CPVT_FontMap::GetPDFFont(int32_t nFontIndex) {
  switch (nFontIndex) {
    case 0:
      return def_font_;
    case 1:
      if (!sys_font_) {
        SetupAnnotSysPDFFont();
      }
      return sys_font_;
    default:
      return nullptr;
  }
}

ByteString CPVT_FontMap::GetPDFFontAlias(int32_t nFontIndex) {
  switch (nFontIndex) {
    case 0:
      return def_font_alias_;
    case 1:
      if (!sys_font_) {
        SetupAnnotSysPDFFont();
      }
      return sys_font_alias_;
    default:
      return ByteString();
  }
}

int32_t CPVT_FontMap::GetWordFontIndex(uint16_t word,
                                       FX_Charset charset,
                                       int32_t nFontIndex) {
  // EmbedPDF(fonts): preserve upstream shared AP behavior for the default
  // form/popup font map: choose the DA font first, then PDFium's native
  // annotation font, based only on CharCodeFromUnicode(). Stricter glyph
  // checks live in CPDF_AnnotFontMap, which is used only by registered
  // FreeText fonts.
  if (RetainPtr<CPDF_Font> pDefFont = GetPDFFont(0)) {
    if (pDefFont->CharCodeFromUnicode(word) != CPDF_Font::kInvalidCharCode) {
      return 0;
    }
  }
  if (RetainPtr<CPDF_Font> pSysFont = GetPDFFont(1)) {
    if (pSysFont->CharCodeFromUnicode(word) != CPDF_Font::kInvalidCharCode) {
      return 1;
    }
  }
  return -1;
}

int32_t CPVT_FontMap::CharCodeFromUnicode(int32_t nFontIndex, uint16_t word) {
  // EmbedPDF(fonts): default implementation is equivalent to the old shared AP
  // path's direct pdf_font->CharCodeFromUnicode() call. The hook exists so
  // specialized font maps can return registered-font subset glyph ids.
  RetainPtr<CPDF_Font> font = GetPDFFont(nFontIndex);
  if (!font) {
    return -1;
  }
  uint32_t charcode = font->CharCodeFromUnicode(word);
  if (charcode == CPDF_Font::kInvalidCharCode ||
      !pdfium::IsValueInRangeForNumericType<int32_t>(charcode)) {
    return -1;
  }
  return static_cast<int32_t>(charcode);
}

FX_Charset CPVT_FontMap::CharSetFromUnicode(uint16_t word,
                                            FX_Charset nOldCharset) {
  // EmbedPDF(fonts): provide a conservative default implementation for the
  // PVT provider hook; registered annotation maps may override as needed.
  if (word < 0x7F) {
    return FX_Charset::kANSI;
  }
  if (nOldCharset != FX_Charset::kDefault) {
    return nOldCharset;
  }
  return CFX_Font::GetCharSetFromUnicode(word);
}
