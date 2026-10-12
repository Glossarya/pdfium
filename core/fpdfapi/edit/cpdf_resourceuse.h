// Copyright 2026 CloudPDF LTD
// SPDX-License-Identifier: Apache-2.0

#ifndef CORE_FPDFAPI_EDIT_CPDF_RESOURCEUSE_H_
#define CORE_FPDFAPI_EDIT_CPDF_RESOURCEUSE_H_

#include <map>
#include <set>
#include <tuple>
#include <utility>

#include "core/fxcrt/bytestring.h"
#include "core/fxcrt/retain_ptr.h"
#include "core/fxcrt/unowned_ptr.h"

class CPDF_Dictionary;
class CPDF_Document;
class CPDF_Object;
class CPDF_Stream;

// Which names of a resource dictionary content uses without naming the
// dictionary itself: a form drawn with no /Resources of its own, the glyphs
// of a Type3 font, a soft mask's group, an annotation's appearance - and what
// those draw in turn. Where content looks a name up is the page layer's rule,
// as the parser and renderer follow it: CPDF_Form::ChooseResourcesDict() and
// ChooseResourcesOfType(), CPDF_Type3Font::GlyphsUseContentResources(), a
// soft mask's group reading the page's resources without its own
// (CPDF_RenderStatus::LoadSMask()) and a tiling pattern only its own
// (CPDF_TilingPattern::Load()).
class CPDF_ResourceUse {
 public:
  // The resource types whose names are tracked.
  static constexpr const char* kTypes[] = {"ExtGState",  "Font",    "XObject",
                                           "ColorSpace", "Pattern", "Shading"};

  using NamesByType = std::map<ByteString, std::set<ByteString>>;

  CPDF_ResourceUse(CPDF_Document* document,
                   RetainPtr<const CPDF_Dictionary> resources);
  ~CPDF_ResourceUse();

  // Walks `content`, which looks names up in `content_resources` and, for a
  // type they lack, in `fallback`: its names of each type that lands in our
  // dictionary of it are used, and what it draws - forms, soft masks' groups,
  // tiling patterns and the glyphs of the Type3 fonts it selects - is walked
  // in turn. One stream can be read with different resources in different
  // places, so it is walked once for each.
  void Walk(const CPDF_Stream* content,
            const CPDF_Dictionary* content_resources,
            const CPDF_Dictionary* fallback);

  // Walks the glyphs of the Type3 fonts `content_resources` (else
  // `fallback`) name `font_names`: a glyph procedure is a form whose page
  // resources are the font's, or without them the resources of the content
  // selecting the font (CPDF_Type3Font::LoadChar(), SetPageResources()).
  void WalkType3Glyphs(const std::set<ByteString>& font_names,
                       const CPDF_Dictionary* content_resources,
                       const CPDF_Dictionary* fallback);

  // The page's resources, which a soft mask's group without its own reads.
  void SetPageResources(RetainPtr<const CPDF_Dictionary> page_resources) {
    page_resources_ = std::move(page_resources);
  }

  // The group of the soft mask graphics state `state` sets, if any.
  static RetainPtr<const CPDF_Stream> SoftMaskGroup(
      RetainPtr<const CPDF_Dictionary> state);
  // `pattern`'s stream when it is a tiling pattern: content of its own.
  static RetainPtr<const CPDF_Stream> TilingPatternStream(
      RetainPtr<const CPDF_Object> pattern);

  // The names of our resources used so far, by type.
  const NamesByType& used() const { return used_; }

 private:
  void WalkSoftMasks(const std::set<ByteString>& state_names,
                     const CPDF_Dictionary* content_resources,
                     const CPDF_Dictionary* fallback);
  void WalkTilingPatterns(const std::set<ByteString>& pattern_names,
                          const CPDF_Dictionary* content_resources,
                          const CPDF_Dictionary* fallback);

  UnownedPtr<CPDF_Document> const document_;
  RetainPtr<const CPDF_Dictionary> const resources_;
  RetainPtr<const CPDF_Dictionary> page_resources_;
  NamesByType used_;
  std::set<std::tuple<const CPDF_Stream*,
                      const CPDF_Dictionary*,
                      const CPDF_Dictionary*>>
      walked_;
};

#endif  // CORE_FPDFAPI_EDIT_CPDF_RESOURCEUSE_H_
