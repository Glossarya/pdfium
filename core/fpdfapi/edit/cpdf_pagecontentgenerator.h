// Copyright 2016 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Original code copyright 2014 Foxit Software Inc. http://www.foxitsoftware.com

#ifndef CORE_FPDFAPI_EDIT_CPDF_PAGECONTENTGENERATOR_H_
#define CORE_FPDFAPI_EDIT_CPDF_PAGECONTENTGENERATOR_H_

#include <stdint.h>

#include <map>
#include <set>
#include <vector>

#include "core/fpdfapi/page/cpdf_textstate.h"
#include "core/fxcrt/bytestring.h"
#include "core/fxcrt/fx_string_wrappers.h"
#include "core/fxcrt/retain_ptr.h"
#include "core/fxcrt/unowned_ptr.h"

// EmbedPDF(faithful-content-rewrite): CPDF_Dictionary, CPDF_Font,
// CPDF_ShadingObject and the colour classes are for the writers the fork
// added below.
class CPDF_ContentMarks;
class CPDF_Dictionary;
class CPDF_Document;
class CPDF_Font;
class CPDF_FormObject;
class CPDF_ImageObject;
class CPDF_Object;
class CPDF_PageObject;
class CPDF_PageObjectHolder;
class CPDF_Path;
class CPDF_PathObject;
class CPDF_ShadingObject;
class CPDF_TextObject;
class CPDF_Color; 
class CPDF_ColorSpace;
class CPDF_ColorState;

class CPDF_PageContentGenerator {
 public:
  explicit CPDF_PageContentGenerator(CPDF_PageObjectHolder* pObjHolder);
  ~CPDF_PageContentGenerator();

  void GenerateContent();
  bool ProcessPageObjects(fxcrt::ostringstream* buf);

 private:
  friend class CPDFPageContentGeneratorTest;

  void ProcessPageObject(fxcrt::ostringstream* buf, CPDF_PageObject* pPageObj);
  void ProcessPathPoints(fxcrt::ostringstream* buf, CPDF_Path* pPath);
  void ProcessPath(fxcrt::ostringstream* buf, CPDF_PathObject* pPathObj);
  void ProcessForm(fxcrt::ostringstream* buf, CPDF_FormObject* pFormObj);
  void ProcessImage(fxcrt::ostringstream* buf, CPDF_ImageObject* pImageObj);
  // EmbedPDF(faithful-content-rewrite): shading objects are written too.
  void ProcessShading(fxcrt::ostringstream* buf, CPDF_ShadingObject* pShadingObj);
  void ProcessGraphics(fxcrt::ostringstream* buf, CPDF_PageObject* pPageObj);
  // EmbedPDF(faithful-content-rewrite): the clip `pPageObj` is drawn in, its
  // paths and then the text that clips (render modes 4 to 7), written
  // invisible.
  void ProcessClip(fxcrt::ostringstream* buf, CPDF_PageObject* pPageObj);
  // EmbedPDF(faithful-content-rewrite): the graphics state resources the
  // content set for `pPageObj`, or one made for its alpha and blend mode.
  void ProcessGraphicsState(fxcrt::ostringstream* buf,
                            CPDF_PageObject* pPageObj);
  void ProcessDefaultGraphics(fxcrt::ostringstream* buf);
  void ProcessText(fxcrt::ostringstream* buf, CPDF_TextObject* pTextObj);
  // EmbedPDF(faithful-content-rewrite): names the font `text` draws with in
  // the resources, and sets `font` to it; empty when the font can be
  // described by neither its dictionary nor its type.
  ByteString RealizeFont(CPDF_TextObject* text, RetainPtr<CPDF_Font>* font);
  // EmbedPDF(faithful-content-rewrite): the same for `font` itself (a stock
  // Helvetica when null).
  ByteString RealizeFont(RetainPtr<CPDF_Font>* font);
  // EmbedPDF(faithful-content-rewrite): content showing text without setting
  // its font, spacing or rendering mode draws with those of the content
  // around it. Writes them as they were where `form_object` was read, for a
  // form that shows text.
  void WriteInheritedTextState(fxcrt::ostringstream& buf,
                               CPDF_FormObject* form_object);
  // EmbedPDF(faithful-content-rewrite): Tf, Tr, Tc, Tw and the operator that
  // shows `text`'s characters, drawn with `render_mode`.
  void WriteTextShowing(fxcrt::ostringstream& buf,
                        CPDF_TextObject* text,
                        CPDF_Font* font,
                        const ByteString& font_name,
                        TextRenderingMode render_mode);
  // EmbedPDF(faithful-content-rewrite): writes a colour as it was read.
  bool EmitColor(fxcrt::ostringstream& buf,
    const CPDF_Color* color,
    bool is_stroke,
    CPDF_PageObject* owner);
  ByteString GetOrCreateDefaultGraphics() const;
  // EmbedPDF(faithful-content-rewrite): names the array `cs` was loaded from
  // in the resources: the name they already give it when they have one.
  // Empty for a colour space with no array (the stock ones).
  ByteString RealizeColorSpaceObject(const CPDF_ColorSpace* cs);
  // EmbedPDF(redaction): points `resources`' entries at the objects the
  // holder's content is to draw instead
  // (CPDF_PageObjectHolder::ReplaceResource()).
  void ApplyResourceReplacements(CPDF_Dictionary* resources);
  // EmbedPDF(faithful-content-rewrite): adds to `seen_resources` the names
  // other content looks up in these resources (a form drawn here, a Type3
  // glyph, an appearance), so pruning keeps them.
  void AddNamesBorrowed(std::map<ByteString, std::set<ByteString>>&
                            seen_resources) const;
  ByteString RealizeResource(const CPDF_Object* pResource,
                             ByteStringView type) const;
  const CPDF_ContentMarks* ProcessContentMarks(fxcrt::ostringstream* buf,
                                               const CPDF_PageObject* pPageObj,
                                               const CPDF_ContentMarks* pPrev);
  void FinishMarks(fxcrt::ostringstream* buf,
                   const CPDF_ContentMarks* pContentMarks);

  // Returns a map from content stream index to new stream data. Unmodified
  // streams are not touched.
  std::map<int32_t, fxcrt::ostringstream> GenerateModifiedStreams();

  // For each entry in `new_stream_data`, adds the string buffer to the page's
  // content stream.
  void UpdateContentStreams(
      std::map<int32_t, fxcrt::ostringstream>&& new_stream_data);

  // Sets the stream index of all page objects with stream index ==
  // |CPDF_PageObject::kNoContentStream|. These are new objects that had not
  // been parsed from or written to any content stream yet.
  void UpdateStreamlessPageObjects(int new_content_stream_index);

  // Updates the resource dictionary for `obj_holder_` to account for all the
  // changes.
  // EmbedPDF(faithful-content-rewrite): `regenerated_all_streams` reports
  // whether THIS pass rewrote every existing content stream. Resource pruning
  // is only sound then — see the guard in the implementation.
  void UpdateResourcesDict(bool regenerated_all_streams);

  // EmbedPDF(faithful-content-rewrite): the holder's current content-stream
  // count (form = its single stream; page = resolved /Contents array size,
  // or 1 for a lone stream).
  int32_t CountExistingContentStreams();

  UnownedPtr<CPDF_PageObjectHolder> const obj_holder_;
  UnownedPtr<CPDF_Document> const document_;
  std::vector<UnownedPtr<CPDF_PageObject>> page_objects_;
  ByteString default_graphics_name_;
  // EmbedPDF(faithful-content-rewrite): the font names
  // WriteInheritedTextState() wrote.
  std::set<ByteString> inherited_font_names_;
  // EmbedPDF(faithful-content-rewrite): the names RealizeColorSpaceObject()
  // gave in this pass. The page objects keep the colour spaces alive.
  std::map<const CPDF_ColorSpace*, ByteString> color_space_names_;
};

#endif  // CORE_FPDFAPI_EDIT_CPDF_PAGECONTENTGENERATOR_H_
