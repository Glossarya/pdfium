// Copyright 2016 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Original code copyright 2014 Foxit Software Inc. http://www.foxitsoftware.com

#include "core/fpdfdoc/cpdf_linklist.h"

#include <utility>

#include "core/fpdfapi/page/cpdf_page.h"
#include "core/fpdfapi/parser/cpdf_array.h"
#include "core/fpdfapi/parser/cpdf_dictionary.h"
#include "core/fxcrt/numerics/safe_conversions.h"

CPDF_LinkList::CPDF_LinkList() = default;

CPDF_LinkList::~CPDF_LinkList() = default;

CPDF_Link CPDF_LinkList::GetLinkAtPoint(CPDF_Page* pPage,
                                        const CFX_PointF& point,
                                        int* z_order) {
  // EmbedPDF(reads-dont-write): links are kept as const dictionaries.
  const std::vector<RetainPtr<const CPDF_Dictionary>>* pPageLinkList =
      GetPageLinks(pPage);
  if (!pPageLinkList) {
    return CPDF_Link();
  }

  for (size_t i = pPageLinkList->size(); i > 0; --i) {
    size_t annot_index = i - 1;
    // EmbedPDF(reads-dont-write): links are kept as const dictionaries.
    RetainPtr<const CPDF_Dictionary> pAnnot = (*pPageLinkList)[annot_index];
    if (!pAnnot) {
      continue;
    }

    // EmbedPDF(reads-dont-write): CPDF_Link holds a mutable pointer; it gets
    // the dictionary read without the mutable getter.
    CPDF_Link link(
        pdfium::WrapRetain(const_cast<CPDF_Dictionary*>(pAnnot.Get())));
    if (!link.GetRect().Contains(point)) {
      continue;
    }

    if (z_order) {
      *z_order = pdfium::checked_cast<int32_t>(annot_index);
    }
    return link;
  }
  return CPDF_Link();
}

// EmbedPDF(reads-dont-write): links are kept as const dictionaries.
const std::vector<RetainPtr<const CPDF_Dictionary>>*
CPDF_LinkList::GetPageLinks(CPDF_Page* pPage) {
  // EmbedPDF(overlay-epoch): cached links are read again when the epoch moves.
  const uint64_t current_epoch = pPage->GetDocument()->GetOverlayEpoch();
  if (overlay_epoch_ != current_epoch) {
    page_map_.clear();
    overlay_epoch_ = current_epoch;
  }

  uint32_t objnum = pPage->GetDict()->GetObjNum();
  if (objnum == 0) {
    return nullptr;
  }

  auto it = page_map_.find(objnum);
  if (it != page_map_.end()) {
    return &it->second;
  }

  // std::map::operator[] forces the creation of a map entry.
  auto* page_link_list = &page_map_[objnum];
  // EmbedPDF(reads-dont-write): read without the mutable getter.
  RetainPtr<const CPDF_Array> pAnnotList = pPage->GetAnnotsArray();
  if (!pAnnotList) {
    return page_link_list;
  }

  for (size_t i = 0; i < pAnnotList->size(); ++i) {
    // EmbedPDF(reads-dont-write): read without the mutable getter.
    RetainPtr<const CPDF_Dictionary> pAnnot = pAnnotList->GetDictAt(i);
    bool add_link = (pAnnot && pAnnot->GetByteStringFor("Subtype") == "Link");
    // Add non-links as nullptrs to preserve z-order.
    page_link_list->emplace_back(add_link ? pAnnot : nullptr);
  }
  return page_link_list;
}
