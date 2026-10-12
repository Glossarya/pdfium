// Copyright 2026 CloudPDF LTD
// SPDX-License-Identifier: Apache-2.0

#include "public/epdf_redact.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "constants/annotation_common.h"
#include "core/fpdfapi/edit/cpdf_text_redactor.h"
#include "core/fpdfapi/page/cpdf_annotcontext.h"
#include "core/fpdfapi/page/cpdf_page.h"
#include "core/fpdfapi/parser/cpdf_array.h"
#include "core/fpdfapi/parser/cpdf_dictionary.h"
#include "core/fpdfapi/parser/cpdf_document.h"
#include "core/fpdfapi/parser/cpdf_reference.h"
#include "core/fpdfapi/parser/cpdf_stream.h"
#include "core/fpdfapi/parser/fpdf_parser_utility.h"
#include "core/fpdfdoc/cpdf_annot.h"
#include "core/fpdfdoc/cpdf_appearancegenerator.h"
#include "core/fpdfdoc/cpdf_interactiveform.h"
#include "core/fxcrt/bytestring.h"
#include "core/fxcrt/compiler_specific.h"
#include "core/fxcrt/containers/contains.h"
#include "core/fxcrt/numerics/safe_conversions.h"
#include "core/fxcrt/span.h"
#include "fpdfsdk/cpdfsdk_helpers.h"
#include "fpdfsdk/epdf_object_helpers.h"
#include "fpdfsdk/epdf_page_content_helpers.h"

namespace {

std::vector<RedactRegion> GetRedactRegionsFromAnnotDict(
    const CPDF_Dictionary* annot_dict) {
  std::vector<RedactRegion> regions;
  if (!annot_dict) {
    return regions;
  }

  RetainPtr<const CPDF_Array> quad_points_array =
      annot_dict->GetArrayFor("QuadPoints");
  if (quad_points_array && quad_points_array->size() >= 8) {
    bool invalid_quad = false;
    size_t quad_count = CPDF_Annot::QuadPointCount(quad_points_array.Get());
    for (size_t i = 0; i < quad_count; ++i) {
      const size_t offset = i * 8;
      std::array<CFX_PointF, 4> corners;
      for (size_t c = 0; c < 4; ++c) {
        corners[c] =
            CFX_PointF(quad_points_array->GetFloatAt(offset + c * 2),
                       quad_points_array->GetFloatAt(offset + c * 2 + 1));
      }
      // Oriented well-formed quads redact their exact cells. Finite malformed
      // entries degrade to their all-corner AABB; non-finite/degenerate data
      // invalidates the quad set so the trusted annotation /Rect is used.
      std::optional<RedactRegion> region =
          RedactRegionFromQuadCorners(corners);
      if (!region.has_value() || region->bbox.IsEmpty()) {
        invalid_quad = true;
        break;
      }
      regions.push_back(std::move(*region));
    }
    if (!invalid_quad && !regions.empty()) {
      return regions;
    }
    regions.clear();
  }

  CFX_FloatRect rect = annot_dict->GetRectFor(pdfium::annotation::kRect);
  rect.Normalize();
  if (!rect.IsEmpty()) {
    RedactRegion region;
    region.bbox = rect;
    regions.push_back(std::move(region));
  }

  return regions;
}

struct RemovedAnnotCandidate {
  size_t index = 0;
  uint32_t object_number = 0;
  // A const view: collecting candidates is a read (reads never promote); the
  // removal itself takes the mutable page, and a widget is taken mutable
  // only when it is detached from its field.
  RetainPtr<const CPDF_Dictionary> dict;
};

uint32_t GetAnnotObjectNumber(const CPDF_Object* entry,
                              const CPDF_Dictionary* dict) {
  if (entry && entry->IsReference()) {
    return entry->AsReference()->GetRefObjNum();
  }
  return dict ? dict->GetObjNum() : 0;
}

bool AnnotIntersectsAny(const CPDF_Dictionary* annot_dict,
                        pdfium::span<const RedactRegion> regions) {
  if (!annot_dict) {
    return false;
  }
  CFX_FloatRect annot_rect =
      annot_dict->GetRectFor(pdfium::annotation::kRect);
  annot_rect.Normalize();
  if (annot_rect.IsEmpty()) {
    return false;
  }
  // Quad-aware: an annotation touching only the EMPTY corner of a rotated
  // mark's bounding box is not collateral — removal is destructive.
  for (const RedactRegion& region : regions) {
    if (RedactRegionIntersectsRect(region, annot_rect)) {
      return true;
    }
  }
  return false;
}

bool CandidateExistsAtIndex(
    const std::vector<RemovedAnnotCandidate>& candidates,
    size_t index) {
  return pdfium::Contains(candidates, index, &RemovedAnnotCandidate::index);
}

bool AddRemovalCandidate(CPDF_Page* page,
                         size_t index,
                         std::vector<RemovedAnnotCandidate>* candidates) {
  if (!page || !candidates || CandidateExistsAtIndex(*candidates, index)) {
    return false;
  }
  RetainPtr<const CPDF_Array> annots = page->GetAnnotsArray();
  if (!annots || index >= annots->size()) {
    return false;
  }
  RetainPtr<const CPDF_Object> entry = annots->GetObjectAt(index);
  RetainPtr<const CPDF_Dictionary> dict =
      ToDictionary(annots->GetDirectObjectAt(index));
  if (!dict) {
    return false;
  }

  RemovedAnnotCandidate candidate;
  candidate.index = index;
  candidate.object_number = GetAnnotObjectNumber(entry.Get(), dict.Get());
  candidate.dict = std::move(dict);
  candidates->push_back(std::move(candidate));
  return true;
}

int FindAnnotIndexOnPageByObjNumOrDict(const CPDF_Page* page,
                                       const CPDF_Dictionary* annot_dict) {
  if (!page || !annot_dict) {
    return -1;
  }
  RetainPtr<const CPDF_Array> annots = page->GetAnnotsArray();
  if (!annots) {
    return -1;
  }
  const uint32_t target_objnum = annot_dict->GetObjNum();
  for (size_t i = 0; i < annots->size(); ++i) {
    RetainPtr<const CPDF_Dictionary> current = annots->GetDictAt(i);
    if (!current) {
      continue;
    }
    if (current.Get() == annot_dict ||
        (target_objnum != 0 && current->GetObjNum() == target_objnum)) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

void AddPopupCascade(CPDF_Page* page,
                     std::vector<RemovedAnnotCandidate>* candidates) {
  if (!page || !candidates) {
    return;
  }
  for (size_t i = 0; i < candidates->size(); ++i) {
    RetainPtr<const CPDF_Dictionary> popup =
        candidates->at(i).dict ? candidates->at(i).dict->GetDictFor("Popup")
                               : nullptr;
    if (!popup) {
      continue;
    }
    const int popup_index =
        FindAnnotIndexOnPageByObjNumOrDict(page, popup.Get());
    if (popup_index >= 0) {
      AddRemovalCandidate(page, static_cast<size_t>(popup_index), candidates);
    }
  }
}

bool RemoveDictFromArray(CPDF_Array* array, const CPDF_Dictionary* dict) {
  if (!array || !dict) {
    return false;
  }
  bool removed = false;
  const uint32_t objnum = dict->GetObjNum();
  for (size_t i = array->size(); i > 0; --i) {
    RetainPtr<const CPDF_Dictionary> item = array->GetDictAt(i - 1);
    if (item &&
        (item.Get() == dict || (objnum != 0 && item->GetObjNum() == objnum))) {
      array->RemoveAt(i - 1);
      removed = true;
    }
  }
  return removed;
}

RetainPtr<CPDF_Array> GetAcroFormFields(CPDF_Document* doc) {
  if (!doc) {
    return nullptr;
  }
  RetainPtr<CPDF_Dictionary> root = doc->GetMutableRoot();
  if (!root) {
    return nullptr;
  }
  RetainPtr<CPDF_Dictionary> acro_form = root->GetMutableDictFor("AcroForm");
  return acro_form ? acro_form->GetMutableArrayFor("Fields") : nullptr;
}

bool DetachWidgetFromAcroForm(CPDF_Document* doc,
                              CPDF_Dictionary* widget_dict) {
  if (!doc || !widget_dict ||
      widget_dict->GetNameFor(pdfium::annotation::kSubtype) != "Widget") {
    return false;
  }

  bool changed = false;
  RetainPtr<CPDF_Dictionary> parent = widget_dict->GetMutableDictFor("Parent");
  if (!parent) {
    RetainPtr<CPDF_Array> fields = GetAcroFormFields(doc);
    return fields ? RemoveDictFromArray(fields.Get(), widget_dict) : false;
  }

  RetainPtr<CPDF_Array> kids = parent->GetMutableArrayFor("Kids");
  if (kids) {
    changed |= RemoveDictFromArray(kids.Get(), widget_dict);
  }

  RetainPtr<CPDF_Dictionary> current = parent;
  while (current) {
    RetainPtr<CPDF_Array> current_kids = current->GetMutableArrayFor("Kids");
    if (current_kids && !current_kids->IsEmpty()) {
      break;
    }

    RetainPtr<CPDF_Dictionary> next_parent =
        current->GetMutableDictFor("Parent");
    if (next_parent) {
      RetainPtr<CPDF_Array> parent_kids =
          next_parent->GetMutableArrayFor("Kids");
      if (parent_kids) {
        changed |= RemoveDictFromArray(parent_kids.Get(), current.Get());
      }
      current = std::move(next_parent);
      continue;
    }

    RetainPtr<CPDF_Array> fields = GetAcroFormFields(doc);
    if (fields) {
      changed |= RemoveDictFromArray(fields.Get(), current.Get());
    }
    break;
  }

  return changed;
}

void DetachWidgetsFromAcroForm(
    CPDF_Page* page,
    const std::vector<RemovedAnnotCandidate>& candidates) {
  if (!page) {
    return;
  }
  CPDF_Document* doc = page->GetDocument();
  if (!doc) {
    return;
  }
  bool changed = false;
  for (const RemovedAnnotCandidate& candidate : candidates) {
    if (!candidate.dict ||
        candidate.dict->GetNameFor(pdfium::annotation::kSubtype) != "Widget") {
      continue;
    }
    // Detaching edits the field tree through the widget's /Parent. The
    // widget is about to be removed, so taking it mutable promotes nothing
    // that survives the removal.
    RetainPtr<CPDF_Dictionary> widget =
        candidate.object_number
            ? ToDictionary(
                  doc->GetMutableIndirectObject(candidate.object_number))
            : pdfium::WrapRetain(
                  const_cast<CPDF_Dictionary*>(candidate.dict.Get()));
    changed |= DetachWidgetFromAcroForm(doc, widget.Get());
  }
  if (changed) {
    CPDF_InteractiveForm form(doc);
    form.FixPageFields(page);
  }
}

// The count deliberately excludes REDACT annotations: they are the removal
// instructions (the applied one, and every sibling consumed by a page-wide
// apply), not collateral. Callers use this to warn that a redaction also
// destroyed annotations the user never explicitly marked.
uint32_t CountRemovedNonRedactAnnots(
    const std::vector<RemovedAnnotCandidate>& candidates) {
  uint32_t count = 0;
  for (const RemovedAnnotCandidate& candidate : candidates) {
    if (candidate.dict && candidate.dict->GetNameFor(
                              pdfium::annotation::kSubtype) != "Redact") {
      ++count;
    }
  }
  return count;
}

void RemoveCandidatesFromPage(
    CPDF_Page* page,
    const std::vector<RemovedAnnotCandidate>& candidates) {
  if (!page) {
    return;
  }
  RetainPtr<CPDF_Array> annots = page->GetMutableAnnotsArray();
  if (!annots) {
    return;
  }
  CPDF_Document* doc = page->GetDocument();
  for (auto it = candidates.rbegin(); it != candidates.rend(); ++it) {
    if (it->index >= annots->size()) {
      continue;
    }
    annots->RemoveAt(it->index);
    if (doc && it->object_number) {
      doc->DeleteIndirectObject(it->object_number);
    }
  }
}

void SortCandidatesByOriginalIndex(
    std::vector<RemovedAnnotCandidate>* candidates) {
  std::sort(candidates->begin(), candidates->end(),
            [](const RemovedAnnotCandidate& a,
               const RemovedAnnotCandidate& b) { return a.index < b.index; });
}

// Resolve the overlay to flatten for one redact annotation: a pre-baked /RO
// always wins (ISO 32000-2); without one (e.g. the file was marked by another
// processor) the overlay is synthesized from the declarative entries. Returns
// null when there is nothing to paint.
RetainPtr<const CPDF_Stream> ResolveRedactOverlay(
    CPDF_Document* doc,
    const CPDF_Dictionary* redact_dict) {
  RetainPtr<const CPDF_Stream> overlay = redact_dict->GetStreamFor("RO");
  if (overlay) {
    return overlay;
  }
  return CPDF_AppearanceGenerator::BuildRedactOverlayForm(doc, redact_dict);
}

// The indexes in the page's /Annots of the REDACT annotations to apply:
// `marks`, each a REDACT annotation of the page, or every one on the page
// when `marks` is empty. In /Annots order, each once. Nullopt when a mark
// isn't a REDACT annotation of the page (another page's, or another
// document's under the same object number).
std::optional<std::vector<size_t>> MarkIndexes(
    CPDF_Page* page,
    pdfium::span<const FPDF_ANNOTATION> marks) {
  std::vector<size_t> indexes;
  if (marks.empty()) {
    RetainPtr<const CPDF_Array> annots = page->GetAnnotsArray();
    for (size_t i = 0; annots && i < annots->size(); ++i) {
      RetainPtr<const CPDF_Dictionary> annot_dict =
          ToDictionary(annots->GetDirectObjectAt(i));
      if (annot_dict &&
          annot_dict->GetNameFor(pdfium::annotation::kSubtype) == "Redact") {
        indexes.push_back(i);
      }
    }
    return indexes;
  }
  for (FPDF_ANNOTATION mark : marks) {
    CPDF_AnnotContext* context = CPDFAnnotContextFromFPDFAnnotation(mark);
    if (!context || !epdf::IsAnnotationOfPage(page, context)) {
      return std::nullopt;
    }
    const CPDF_Dictionary* annot_dict = context->GetAnnotDict();
    if (!annot_dict ||
        annot_dict->GetNameFor(pdfium::annotation::kSubtype) != "Redact") {
      return std::nullopt;
    }
    const int index = FindAnnotIndexOnPageByObjNumOrDict(page, annot_dict);
    if (index < 0) {
      return std::nullopt;
    }
    indexes.push_back(static_cast<size_t>(index));
  }
  std::sort(indexes.begin(), indexes.end());
  indexes.erase(std::unique(indexes.begin(), indexes.end()), indexes.end());
  return indexes;
}

// Applies the REDACT annotations at `indexes` of the page's /Annots
// together: their regions are redacted in one pass, so each image under them
// is read and written once, and their overlays are drawn in /Annots order.
bool ApplyRedactionsCore(CPDF_Page* page,
                         pdfium::span<const size_t> indexes,
                         uint32_t* out_removed_annot_count) {
  RetainPtr<const CPDF_Array> annots = page->GetAnnotsArray();
  if (!annots || indexes.empty()) {
    return false;
  }

  std::vector<RedactRegion> all_regions;
  std::vector<std::pair<RetainPtr<const CPDF_Stream>, CFX_FloatRect>>
      overlays;
  std::vector<RemovedAnnotCandidate> removals;

  for (size_t i : indexes) {
    RetainPtr<const CPDF_Dictionary> annot_dict =
        ToDictionary(annots->GetDirectObjectAt(i));
    if (!annot_dict) {
      continue;
    }

    AddRemovalCandidate(page, i, &removals);

    std::vector<RedactRegion> regions =
        GetRedactRegionsFromAnnotDict(annot_dict.Get());
    for (RedactRegion& region : regions) {
      all_regions.push_back(std::move(region));
    }

    RetainPtr<const CPDF_Stream> overlay =
        ResolveRedactOverlay(page->GetDocument(), annot_dict.Get());
    if (overlay) {
      CFX_FloatRect annot_rect =
          annot_dict->GetRectFor(pdfium::annotation::kRect);
      annot_rect.Normalize();
      overlays.push_back({std::move(overlay), annot_rect});
    }
  }

  if (all_regions.empty()) {
    return false;
  }

  // Annotations the regions touch go too; other REDACT annotations stay.
  for (size_t i = 0; i < annots->size(); ++i) {
    RetainPtr<const CPDF_Dictionary> annot_dict =
        ToDictionary(annots->GetDirectObjectAt(i));
    if (!annot_dict) {
      continue;
    }
    if (annot_dict->GetNameFor(pdfium::annotation::kSubtype) == "Redact") {
      continue;
    }
    if (AnnotIntersectsAny(annot_dict.Get(), pdfium::span(all_regions))) {
      AddRemovalCandidate(page, i, &removals);
    }
  }

  AddPopupCascade(page, &removals);
  SortCandidatesByOriginalIndex(&removals);

  const RedactResult redact_result = RedactTextInRegions(
      page, pdfium::span(all_regions),
      /*recurse_forms=*/true,
      /*draw_black_boxes=*/false);
  if (!redact_result.succeeded) {
    return false;
  }

  for (const auto& [overlay, annot_rect] : overlays) {
    EpdfAppendFormXObjectToPage(page, overlay, annot_rect);
  }

  DetachWidgetsFromAcroForm(page, removals);
  if (out_removed_annot_count) {
    *out_removed_annot_count = CountRemovedNonRedactAnnots(removals);
  }
  RemoveCandidatesFromPage(page, removals);
  return true;
}

}  // namespace

FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFPage_ApplyRedactions(FPDF_PAGE page,
                         const FPDF_ANNOTATION* marks,
                         int count,
                         uint32_t* out_removed_annot_count) {
  if (out_removed_annot_count) {
    *out_removed_annot_count = 0;
  }

  CPDF_Page* pPage = CPDFPageFromFPDFPage(page);
  if (!pPage || count < 0 || (count > 0 && !marks) || (marks && count == 0)) {
    return false;
  }
  // SAFETY: the caller passes `count` annotation handles at `marks`.
  const pdfium::span<const FPDF_ANNOTATION> mark_span =
      marks ? UNSAFE_BUFFERS(pdfium::span(marks, static_cast<size_t>(count)))
            : pdfium::span<const FPDF_ANNOTATION>();

  // The caller may hand us a never-rendered page. Redaction MUST see the full
  // object model: an unparsed page would silently remove nothing (a security
  // failure, not a cosmetic one) and would let content regeneration reason
  // from an empty object list.
  pPage->ParseContent();

  std::optional<std::vector<size_t>> indexes = MarkIndexes(pPage, mark_span);
  if (!indexes.has_value()) {
    return false;
  }
  return ApplyRedactionsCore(pPage, indexes.value(), out_removed_annot_count);
}
