// Copyright 2026 CloudPDF LTD
// SPDX-License-Identifier: Apache-2.0

#ifndef TESTING_DOCUMENT_SENTINELS_H_
#define TESTING_DOCUMENT_SENTINELS_H_

#include <string>

class CPDF_Document;

// Redaction tests' fixtures carry ASCII sentinels. These inspect the saved
// graph as well as text extraction: an unused resource or replacement-text
// property can retain the sentinel even when the rendered page and the text
// extractor are clean. Deliberately NOT a general-purpose PDF sanitization
// oracle.

// Expects no stream (decoded), string or name reachable from the document's
// root to carry `sentinel`.
void ExpectNoSentinel(CPDF_Document* doc, const std::string& sentinel);

// How many streams reachable from the document's root carry `sentinel`.
int CountSentinel(CPDF_Document* doc, const std::string& sentinel);

// The same, reachable from page `index`'s dictionary without going up the
// page tree (/Parent): what that page draws or holds, when another page may
// carry the sentinel legitimately.
void ExpectNoSentinelOnPage(CPDF_Document* doc,
                            int index,
                            const std::string& sentinel);
int CountSentinelOnPage(CPDF_Document* doc,
                        int index,
                        const std::string& sentinel);

#endif  // TESTING_DOCUMENT_SENTINELS_H_
