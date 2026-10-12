// Copyright 2026 CloudPDF LTD
// SPDX-License-Identifier: Apache-2.0

#ifndef CORE_FPDFAPI_EDIT_CPDF_REDACTION_COPIES_H_
#define CORE_FPDFAPI_EDIT_CPDF_REDACTION_COPIES_H_

#include <optional>

#include "core/fxcrt/bytestring.h"
#include "core/fxcrt/retain_ptr.h"

class CPDF_Dictionary;
class CPDF_Document;
class CPDF_Page;
class CPDF_Stream;

// Other copies of what a redaction removes. Next to its content, a page or a
// form can carry a thumbnail picture of the page (/Thumb, ISO 32000-1
// §12.3.4), application data that may hold the original artwork (/PieceInfo,
// §14.5), XMP metadata that may hold a preview (/Metadata, §14.3.2), and a
// link to a full-resolution original (/OPI, §14.11.7). The document's own XMP
// metadata can hold a preview of a page as well (xmp:Thumbnails).

// Whether `dict` (a page or a form) holds any of those entries.
bool HasContentCopies(const CPDF_Dictionary* dict);

// Removes those entries from `dict`.
void RemoveContentCopies(CPDF_Dictionary* dict);

// A copy of `stream` (a form, a soft mask's group, a Type3 glyph) for
// redaction to write into: the same data and dictionary, without those
// entries. Every stream redaction copies is copied this way.
RetainPtr<CPDF_Stream> CopyWithoutContentCopies(const CPDF_Stream* stream);

// Removes them from `page`, taking its dictionary for writing only when it
// holds one.
void RemovePageContentCopies(CPDF_Page* page);

// `packet` without its elements named Thumbnails, in any namespace. Nullopt
// when the packet can't be edited as UTF-8 text: it holds NUL bytes (UTF-16
// or UTF-32), or a Thumbnails element has no end.
std::optional<ByteString> RemoveXmpThumbnails(ByteStringView packet);

// Removes the previews from the document's XMP metadata (the catalog's
// /Metadata). Metadata that mentions thumbnails but can't be edited as text
// is removed whole.
void RemoveDocumentXmpThumbnails(CPDF_Document* doc);

#endif  // CORE_FPDFAPI_EDIT_CPDF_REDACTION_COPIES_H_
