// Copyright 2026 CloudPDF LTD
// SPDX-License-Identifier: Apache-2.0

#include "core/fpdfapi/page/cpdf_contentnames.h"

#include <vector>

#include "core/fpdfapi/page/cpdf_form.h"
#include "core/fpdfapi/page/cpdf_streamcontentparser.h"
#include "core/fpdfapi/page/cpdf_streamparser.h"
#include "core/fpdfapi/parser/cpdf_dictionary.h"
#include "core/fpdfapi/parser/cpdf_document.h"
#include "core/fpdfapi/parser/cpdf_stream.h"
#include "core/fpdfapi/parser/fpdf_parser_utility.h"

namespace {

// The type of resource `op` looks a name operand up in; null for an
// operator that looks none up (or one, like BDC, of a type no page object
// pruning covers).
const char* ResourceTypeOf(ByteStringView op) {
  if (op == "Tf") {
    return "Font";
  }
  if (op == "Do") {
    return "XObject";
  }
  if (op == "gs") {
    return "ExtGState";
  }
  if (op == "cs" || op == "CS") {
    return "ColorSpace";
  }
  if (op == "scn" || op == "SCN") {
    return "Pattern";
  }
  if (op == "sh") {
    return "Shading";
  }
  return nullptr;
}

}  // namespace

ResourceNamesByType GetResourceNamesInContent(
    CPDF_Document* document,
    pdfium::span<const uint8_t> content,
    const CPDF_Dictionary* resources,
    const CPDF_Dictionary* page_resources) {
  ResourceNamesByType names;
  // An inline image's colour space is looked up as the parser looks it up:
  // the image's size depends on it.
  auto find_color_space = [&](const ByteString& name) {
    names["ColorSpace"].insert(name);
    RetainPtr<const CPDF_Dictionary> color_spaces =
        CPDF_Form::ChooseResourcesOfType(resources, page_resources,
                                         "ColorSpace");
    return color_spaces ? color_spaces->GetDirectObjectFor(name.AsStringView())
                        : nullptr;
  };

  CPDF_StreamParser syntax(content, document->GetByteStringPool());
  // The name operands since the last operator.
  std::vector<ByteString> operands;
  while (true) {
    switch (syntax.ParseNextElement()) {
      case CPDF_StreamParser::ElementType::kEndOfData:
        return names;
      case CPDF_StreamParser::ElementType::kName: {
        const ByteStringView word = syntax.GetWord();
        operands.push_back(PDF_NameDecode(word.Last(word.GetLength() - 1)));
        break;
      }
      case CPDF_StreamParser::ElementType::kKeyword: {
        const ByteStringView op = syntax.GetWord();
        if (op == "BI") {
          CPDF_StreamContentParser::ReadInlineImage(&syntax, document,
                                                    find_color_space);
        } else if (const char* type = ResourceTypeOf(op);
                   type && !operands.empty()) {
          // Tf names the font before its size; scn names the pattern after
          // the colour of an uncoloured one.
          names[type].insert(op == "scn" || op == "SCN" ? operands.back()
                                                        : operands.front());
        }
        operands.clear();
        break;
      }
      default:
        break;
    }
  }
}
