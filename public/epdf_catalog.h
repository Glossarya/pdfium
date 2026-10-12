// Copyright 2026 CloudPDF LTD
// SPDX-License-Identifier: Apache-2.0

#ifndef PUBLIC_EPDF_CATALOG_H_
#define PUBLIC_EPDF_CATALOG_H_

// NOLINTNEXTLINE(build/include)
#include "fpdf_catalog.h"

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

// EmbedPDF's additions to public/fpdf_catalog.h: the catalog's language.

// Experimental EmbedPDF API.
// Gets the Catalog /Lang value as a UTF-16LE string with trailing NUL.
//
// If |buffer| is NULL or |length| is 0, the return value is the required length
// in 2-byte units (including the trailing NUL).
//
// If /Lang is missing, returns 0 to signal "not present".
// If /Lang exists but is empty, returns 2 (a single UTF-16 NUL).
//
// document - handle to a document.
// buffer   - destination buffer for UTF-16LE characters (can be NULL to probe).
// length   - size of |buffer| in 2-byte units.
//
// Returns the number of 2-byte units needed/written (including NUL),
// or 0 if /Lang is not present.
FPDF_EXPORT unsigned long FPDF_CALLCONV
EPDFCatalog_GetLanguage(FPDF_DOCUMENT document, void* buffer, unsigned long length);

#ifdef __cplusplus
}  // extern "C"
#endif  // __cplusplus

#endif  // PUBLIC_EPDF_CATALOG_H_
