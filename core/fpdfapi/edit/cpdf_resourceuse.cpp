// Copyright 2026 CloudPDF LTD
// SPDX-License-Identifier: Apache-2.0

#include "core/fpdfapi/edit/cpdf_resourceuse.h"

#include <utility>

#include "core/fpdfapi/font/cpdf_type3font.h"
#include "core/fpdfapi/page/cpdf_contentnames.h"
#include "core/fpdfapi/page/cpdf_form.h"
#include "core/fpdfapi/parser/cpdf_dictionary.h"
#include "core/fpdfapi/parser/cpdf_document.h"
#include "core/fpdfapi/parser/cpdf_stream.h"
#include "core/fpdfapi/parser/cpdf_stream_acc.h"

CPDF_ResourceUse::CPDF_ResourceUse(CPDF_Document* document,
                                   RetainPtr<const CPDF_Dictionary> resources)
    : document_(document), resources_(std::move(resources)) {}

CPDF_ResourceUse::~CPDF_ResourceUse() = default;

void CPDF_ResourceUse::Walk(const CPDF_Stream* content,
                            const CPDF_Dictionary* content_resources,
                            const CPDF_Dictionary* fallback) {
  // Only content looking up in ours, or falling back to them (a page's), or
  // drawing content that does, can reach them - and when ours are the page's,
  // any soft mask can.
  if (!resources_ ||
      (content_resources != resources_.Get() && fallback != resources_.Get() &&
       page_resources_ != resources_.Get())) {
    return;
  }
  if (!content ||
      !walked_.emplace(content, content_resources, fallback).second) {
    return;
  }
  auto acc = pdfium::MakeRetain<CPDF_StreamAcc>(pdfium::WrapRetain(content));
  acc->LoadAllDataFiltered();
  ResourceNamesByType names = GetResourceNamesInContent(
      document_, acc->GetSpan(), content_resources, fallback);
  for (const char* type : kTypes) {
    RetainPtr<const CPDF_Dictionary> ours = resources_->GetDictFor(type);
    if (ours && CPDF_Form::ChooseResourcesOfType(content_resources, fallback,
                                                 type) == ours) {
      used_[type].insert(names[type].begin(), names[type].end());
    }
  }

  RetainPtr<const CPDF_Dictionary> xobjects =
      CPDF_Form::ChooseResourcesOfType(content_resources, fallback, "XObject");
  for (const ByteString& name : names["XObject"]) {
    RetainPtr<const CPDF_Stream> form =
        xobjects ? ToStream(xobjects->GetDirectObjectFor(name.AsStringView()))
                 : nullptr;
    if (!form || form->GetDict()->GetNameFor("Subtype") != "Form") {
      continue;
    }
    RetainPtr<const CPDF_Dictionary> own =
        form->GetDict()->GetDictFor("Resources");
    Walk(form.Get(),
         CPDF_Form::ChooseResourcesDict(own.Get(), content_resources, fallback),
         fallback);
  }
  WalkSoftMasks(names["ExtGState"], content_resources, fallback);
  WalkTilingPatterns(names["Pattern"], content_resources, fallback);
  WalkType3Glyphs(names["Font"], content_resources, fallback);
}

void CPDF_ResourceUse::WalkSoftMasks(const std::set<ByteString>& state_names,
                                     const CPDF_Dictionary* content_resources,
                                     const CPDF_Dictionary* fallback) {
  RetainPtr<const CPDF_Dictionary> states = CPDF_Form::ChooseResourcesOfType(
      content_resources, fallback, "ExtGState");
  for (const ByteString& name : state_names) {
    RetainPtr<const CPDF_Stream> group = SoftMaskGroup(
        states ? states->GetDictFor(name.AsStringView()) : nullptr);
    if (!group) {
      continue;
    }
    RetainPtr<const CPDF_Dictionary> own =
        group->GetDict()->GetDictFor("Resources");
    Walk(group.Get(),
         CPDF_Form::ChooseResourcesDict(own.Get(), nullptr,
                                        page_resources_.Get()),
         page_resources_.Get());
  }
}

void CPDF_ResourceUse::WalkTilingPatterns(
    const std::set<ByteString>& pattern_names,
    const CPDF_Dictionary* content_resources,
    const CPDF_Dictionary* fallback) {
  RetainPtr<const CPDF_Dictionary> patterns =
      CPDF_Form::ChooseResourcesOfType(content_resources, fallback, "Pattern");
  for (const ByteString& name : pattern_names) {
    RetainPtr<const CPDF_Stream> pattern = TilingPatternStream(
        patterns ? patterns->GetDirectObjectFor(name.AsStringView()) : nullptr);
    RetainPtr<const CPDF_Dictionary> own =
        pattern ? pattern->GetDict()->GetDictFor("Resources") : nullptr;
    if (own) {
      Walk(pattern.Get(), own.Get(), nullptr);
    }
  }
}

// static
RetainPtr<const CPDF_Stream> CPDF_ResourceUse::SoftMaskGroup(
    RetainPtr<const CPDF_Dictionary> state) {
  RetainPtr<const CPDF_Dictionary> mask =
      state ? state->GetDictFor("SMask") : nullptr;
  return mask ? mask->GetStreamFor("G") : nullptr;
}

// static
RetainPtr<const CPDF_Stream> CPDF_ResourceUse::TilingPatternStream(
    RetainPtr<const CPDF_Object> pattern) {
  RetainPtr<const CPDF_Stream> stream = ToStream(std::move(pattern));
  return stream && stream->GetDict()->GetIntegerFor("PatternType") == 1
             ? stream
             : nullptr;
}

void CPDF_ResourceUse::WalkType3Glyphs(const std::set<ByteString>& font_names,
                                       const CPDF_Dictionary* content_resources,
                                       const CPDF_Dictionary* fallback) {
  RetainPtr<const CPDF_Dictionary> fonts =
      CPDF_Form::ChooseResourcesOfType(content_resources, fallback, "Font");
  if (!fonts) {
    return;
  }
  for (const ByteString& name : font_names) {
    RetainPtr<const CPDF_Dictionary> font =
        fonts->GetDictFor(name.AsStringView());
    RetainPtr<const CPDF_Dictionary> procs =
        font && font->GetNameFor("Subtype") == "Type3"
            ? font->GetDictFor("CharProcs")
            : nullptr;
    if (!procs) {
      continue;
    }
    RetainPtr<const CPDF_Dictionary> glyph_resources =
        CPDF_Type3Font::GlyphsUseContentResources(font.Get())
            ? pdfium::WrapRetain(content_resources)
            : font->GetDictFor("Resources");
    CPDF_DictionaryLocker proc_locker(procs);
    for (const auto& proc : proc_locker) {
      RetainPtr<const CPDF_Stream> stream = ToStream(proc.second->GetDirect());
      if (!stream) {
        continue;
      }
      RetainPtr<const CPDF_Dictionary> own =
          stream->GetDict()->GetDictFor("Resources");
      Walk(stream.Get(),
           CPDF_Form::ChooseResourcesDict(own.Get(), nullptr,
                                          glyph_resources.Get()),
           glyph_resources.Get());
    }
  }
}
