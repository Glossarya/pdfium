// Copyright 2026 CloudPDF LTD
// SPDX-License-Identifier: Apache-2.0

#ifndef PUBLIC_EPDF_EDIT_H_
#define PUBLIC_EPDF_EDIT_H_

// NOLINTNEXTLINE(build/include)
#include "fpdf_edit.h"

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

// EmbedPDF's additions to public/fpdf_edit.h: blank pages without loading the
// page, PNG and JPEG image objects, and text redaction.

// Experimental EmbedPDF Extension API.
// Insert a blank page without loading it: FPDFPage_New() for a caller that
// only writes the page into the document.
//
//   document   - handle to document.
//   page_index - the 0-based index the new page gets, from 0 to the page
//                count (append). Out of range is refused, never clamped.
//   width      - the page width in points.
//   height     - the page height in points.
//   objnum     - the object number the page gets, or 0 for the next free one.
//                A number other than 0 needs a layer transaction and must be
//                free (EPDFLayer_RaiseLastObjectNumber()).
//
// Returns TRUE when the page was inserted; FALSE otherwise, and then nothing
// changed.
FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFPage_InsertBlankRaw(FPDF_DOCUMENT document,
                        int page_index,
                        double width,
                        double height,
                        unsigned long objnum);

// Set a PNG image on |image_object|. Decodes the PNG buffer, applies
// FlateDecode with PNG prediction filters for optimal compression, and
// handles alpha transparency via an SMask stream.
//
//   pages        - pointer to the start of all loaded pages, may be NULL.
//   count        - number of |pages|, may be 0.
//   image_object - handle to an image object.
//   png_data     - pointer to the raw PNG file bytes.
//   png_size     - size in bytes of the PNG data.
//
// Returns TRUE on success.
FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFImageObj_SetPng(FPDF_PAGE* pages,
                    int count,
                    FPDF_PAGEOBJECT image_object,
                    const uint8_t* png_data,
                    size_t png_size);

// Set a JPEG image on |image_object| from a raw buffer. Embeds the JPEG
// data inline (copied into the document) using DCTDecode -- no re-encoding.
//
//   pages        - pointer to the start of all loaded pages, may be NULL.
//   count        - number of |pages|, may be 0.
//   image_object - handle to an image object.
//   jpeg_data    - pointer to the raw JPEG file bytes.
//   jpeg_size    - size in bytes of the JPEG data.
//
// Returns TRUE on success.
FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFImageObj_SetJpeg(FPDF_PAGE* pages,
                     int count,
                     FPDF_PAGEOBJECT image_object,
                     const uint8_t* jpeg_data,
                     size_t jpeg_size);

// Experimental API.
//
// Redact text in a given rectangle on a page.
//
//   page          - handle to a page.
//   rect          - handle to a rectangle.
//   recurse_forms - whether to recurse into form objects.
//
// Returns TRUE on success.
FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFText_RedactInRect(FPDF_PAGE page,
                      const FS_RECTF* rect,
                      FPDF_BOOL recurse_forms,
                      FPDF_BOOL draw_black_boxes);

// Experimental API.
//
// Redact text in a given quads on a page.
//
//   page          - handle to a page.
//   quads         - handle to a quads.
//   count         - the number of quads.
//   recurse_forms - whether to recurse into form objects.
//
FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFText_RedactInQuads(FPDF_PAGE page,
                       const FS_QUADPOINTSF* quads,
                       size_t count,
                       FPDF_BOOL recurse_forms,
                       FPDF_BOOL draw_black_boxes);

#ifdef __cplusplus
}  // extern "C"
#endif  // __cplusplus

#endif  // PUBLIC_EPDF_EDIT_H_
