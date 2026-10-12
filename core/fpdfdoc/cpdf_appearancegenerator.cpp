// Copyright 2016 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Original code copyright 2014 Foxit Software Inc. http://www.foxitsoftware.com

// EmbedPDF: our appearance generator, moved out of upstream's
// core/fpdfdoc/cpdf_generateap.{h,cpp}, which stay exactly as upstream
// wrote them and unused (docs/embedpdf/fork-ledger.md, rule
// own-appearance-generator). EmbedPDF's changes are Copyright 2026
// CloudPDF LTD.
//
// This file holds the entry points and picks the family that draws each
// annotation. Each family has its own cpdf_appearancegenerator_<family>.cpp;
// what they share is declared in cpdf_appearancegenerator_internal.h.

#include "core/fpdfdoc/cpdf_appearancegenerator.h"

#include <optional>
#include <sstream>
#include <utility>

#include "core/fpdfapi/parser/cpdf_dictionary.h"
#include "core/fpdfapi/parser/cpdf_document.h"
#include "core/fpdfdoc/cpdf_annot.h"
#include "core/fpdfdoc/cpdf_appearancegenerator_internal.h"
#include "core/fxcrt/fx_string_wrappers.h"

using appearance_generator::APGenerationTarget;
using appearance_generator::BlendModeToPDFName;
using appearance_generator::DefaultBlendModeFor;
using appearance_generator::GenerateAndSetAPDict;
using appearance_generator::GenerateCaretAP;
using appearance_generator::GenerateCircleAP;
using appearance_generator::GenerateExtGStateDict;
using appearance_generator::GenerateFileAttachmentAP;
using appearance_generator::GenerateFreeTextAP;
using appearance_generator::GenerateHighlightAP;
using appearance_generator::GenerateInkAP;
using appearance_generator::GenerateLineAP;
using appearance_generator::GenerateLinkAP;
using appearance_generator::GeneratePolygonAP;
using appearance_generator::GeneratePolyLineAP;
using appearance_generator::GeneratePopupAP;
using appearance_generator::GenerateRedactAP;
using appearance_generator::GenerateResourcesDict;
using appearance_generator::GenerateSquareAP;
using appearance_generator::GenerateSquigglyAP;
using appearance_generator::GenerateStrikeOutAP;
using appearance_generator::GenerateTextAP;
using appearance_generator::GenerateUnderlineAP;
using appearance_generator::GetWidgetFormType;

namespace {

bool SupportsEphemeralAnnotAP(CPDF_Annot::Subtype subtype) {
  switch (subtype) {
    case CPDF_Annot::Subtype::CARET:
    case CPDF_Annot::Subtype::CIRCLE:
    case CPDF_Annot::Subtype::FILEATTACHMENT:
    case CPDF_Annot::Subtype::FREETEXT:
    case CPDF_Annot::Subtype::HIGHLIGHT:
    case CPDF_Annot::Subtype::INK:
    case CPDF_Annot::Subtype::LINE:
    case CPDF_Annot::Subtype::LINK:
    case CPDF_Annot::Subtype::POLYGON:
    case CPDF_Annot::Subtype::POLYLINE:
    case CPDF_Annot::Subtype::REDACT:
    case CPDF_Annot::Subtype::SQUARE:
    case CPDF_Annot::Subtype::SQUIGGLY:
    case CPDF_Annot::Subtype::STRIKEOUT:
    case CPDF_Annot::Subtype::TEXT:
    case CPDF_Annot::Subtype::UNDERLINE:
    case CPDF_Annot::Subtype::WIDGET:
      return true;
    default:
      return false;
  }
}

bool GenerateAnnotAPToTarget(APGenerationTarget* target,
                             CPDF_Dictionary* annot_dict,
                             CPDF_Annot::Subtype subtype,
                             BlendMode blend_mode) {
  ByteString blend_name = BlendModeToPDFName(blend_mode);
  switch (subtype) {
    case CPDF_Annot::Subtype::CIRCLE:
      return GenerateCircleAP(target, annot_dict, blend_name);
    case CPDF_Annot::Subtype::HIGHLIGHT:
      return GenerateHighlightAP(target, annot_dict, blend_name);
    case CPDF_Annot::Subtype::INK:
      return GenerateInkAP(target, annot_dict, blend_name);
    case CPDF_Annot::Subtype::LINE:
      return GenerateLineAP(target, annot_dict, blend_name);
    case CPDF_Annot::Subtype::POLYGON:
      return GeneratePolygonAP(target, annot_dict, blend_name);
    case CPDF_Annot::Subtype::POLYLINE:
      return GeneratePolyLineAP(target, annot_dict, blend_name);
    case CPDF_Annot::Subtype::SQUARE:
      return GenerateSquareAP(target, annot_dict, blend_name);
    case CPDF_Annot::Subtype::SQUIGGLY:
      return GenerateSquigglyAP(target, annot_dict, blend_name);
    case CPDF_Annot::Subtype::STRIKEOUT:
      return GenerateStrikeOutAP(target, annot_dict, blend_name);
    case CPDF_Annot::Subtype::UNDERLINE:
      return GenerateUnderlineAP(target, annot_dict, blend_name);
    case CPDF_Annot::Subtype::TEXT:
      return GenerateTextAP(target, annot_dict, blend_name);
    case CPDF_Annot::Subtype::FILEATTACHMENT:
      return GenerateFileAttachmentAP(target, annot_dict, blend_name);
    case CPDF_Annot::Subtype::LINK:
      return GenerateLinkAP(target, annot_dict, blend_name);
    case CPDF_Annot::Subtype::CARET:
      return GenerateCaretAP(target, annot_dict, blend_name);
    case CPDF_Annot::Subtype::REDACT:
      return GenerateRedactAP(target, annot_dict, blend_name);
    default:
      return false;
  }
}

}  // namespace

// static
void CPDF_AppearanceGenerator::GenerateEmptyAP(CPDF_Document* doc,
                                               CPDF_Dictionary* annot_dict) {
  auto gs_dict = GenerateExtGStateDict(*annot_dict, "Normal");
  auto resources_dict = GenerateResourcesDict(doc, std::move(gs_dict), nullptr);

  fxcrt::ostringstream stream;
  GenerateAndSetAPDict(doc, annot_dict, &stream, std::move(resources_dict),
                       false);
}

// static
bool CPDF_AppearanceGenerator::GenerateAnnotAP(CPDF_Document* doc,
                                               CPDF_Dictionary* annot_dict,
                                               CPDF_Annot::Subtype subtype) {
  return GenerateAnnotAP(doc, annot_dict, subtype,
                         DefaultBlendModeFor(subtype));
}

// static
bool CPDF_AppearanceGenerator::GenerateAnnotAP(CPDF_Document* doc,
                                               CPDF_Dictionary* annot_dict,
                                               CPDF_Annot::Subtype subtype,
                                               BlendMode blend_mode) {
  APGenerationTarget target{doc, annot_dict};
  if (GenerateAnnotAPToTarget(&target, annot_dict, subtype, blend_mode)) {
    return true;
  }

  ByteString blend_name = BlendModeToPDFName(blend_mode);
  switch (subtype) {
    case CPDF_Annot::Subtype::FREETEXT:
      return GenerateFreeTextAP(&target, annot_dict, blend_name);
    case CPDF_Annot::Subtype::POPUP:
      return GeneratePopupAP(doc, annot_dict, blend_name);
    default:
      return false;
  }
}

// static
std::optional<CPDF_AppearanceGenerator::GeneratedAP>
CPDF_AppearanceGenerator::GenerateEphemeralAnnotAP(
    CPDF_Document* doc,
    const CPDF_Dictionary* annot_dict,
    CPDF_Annot::Subtype subtype) {
  return GenerateEphemeralAnnotAP(doc, annot_dict, subtype,
                                  DefaultBlendModeFor(subtype));
}

// static
std::optional<CPDF_AppearanceGenerator::GeneratedAP>
CPDF_AppearanceGenerator::GenerateEphemeralAnnotAP(
    CPDF_Document* doc,
    const CPDF_Dictionary* annot_dict,
    CPDF_Annot::Subtype subtype,
    BlendMode blend_mode) {
  if (!SupportsEphemeralAnnotAP(subtype)) {
    return std::nullopt;
  }

  if (subtype == CPDF_Annot::Subtype::WIDGET) {
    std::optional<FormType> type = GetWidgetFormType(annot_dict);
    return type.has_value()
               ? GenerateEphemeralFormAP(doc, annot_dict, type.value())
               : std::nullopt;
  }

  APGenerationTarget target{doc, nullptr};
  CPDF_Dictionary* mutable_annot_dict =
      const_cast<CPDF_Dictionary*>(annot_dict);
  if (subtype == CPDF_Annot::Subtype::FREETEXT) {
    if (!GenerateFreeTextAP(&target, mutable_annot_dict,
                            BlendModeToPDFName(blend_mode))) {
      return std::nullopt;
    }
    return GeneratedAP{std::move(target.ephemeral_fonts),
                       std::move(target.normal_stream)};
  }

  if (!GenerateAnnotAPToTarget(&target, mutable_annot_dict, subtype,
                               blend_mode)) {
    return std::nullopt;
  }

  return GeneratedAP{std::move(target.ephemeral_fonts),
                     std::move(target.normal_stream)};
}

// static
bool CPDF_AppearanceGenerator::CanGenerateEphemeralAnnotAP(
    CPDF_Annot::Subtype subtype) {
  return SupportsEphemeralAnnotAP(subtype);
}
