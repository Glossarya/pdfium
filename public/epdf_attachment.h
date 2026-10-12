// Copyright 2026 CloudPDF LTD
// SPDX-License-Identifier: Apache-2.0

#ifndef PUBLIC_EPDF_ATTACHMENT_H_
#define PUBLIC_EPDF_ATTACHMENT_H_

#include <stdint.h>

// NOLINTNEXTLINE(build/include)
#include "fpdf_attachment.h"
// NOLINTNEXTLINE(build/include)
#include "fpdf_save.h"

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

// EmbedPDF's additions to public/fpdf_attachment.h: attachment keys,
// extraction, and the attachment setters and getters.

// Experimental EmbedPDF API.
// Get the |document|'s EmbeddedFiles name-tree KEY at |index|, encoded in
// UTF-16LE. The key is the tree's unique identifier for the entry; it is
// usually — but not necessarily — equal to the filespec's /UF file name
// returned by FPDFAttachment_GetName() (foreign PDFs may diverge, and /UF
// values may collide while keys cannot). |buffer| is only modified if
// |buflen| is longer than the length of the key. On errors, |buffer| is
// unmodified and the returned length is 0.
//
//   document - handle to a document.
//   index    - the index of the embedded file.
//   buffer   - buffer for holding the key, encoded in UTF-16LE.
//   buflen   - length of the buffer in bytes.
//
// Returns the length of the key in bytes, or 0 on error.
FPDF_EXPORT unsigned long FPDF_CALLCONV
EPDFDoc_GetAttachmentKey(FPDF_DOCUMENT document,
                         int index,
                         FPDF_WCHAR* buffer,
                         unsigned long buflen);

// Experimental EmbedPDF API.
// Find the current index of the embedded file whose EmbeddedFiles
// name-tree key equals |key|. Keys are unique within the tree, so at most
// one entry matches. Note that indices shift when attachments are added
// (the tree is name-sorted) or deleted — the returned index is only valid
// until the next mutation.
//
//   document - handle to a document.
//   key      - the name-tree key to look for, encoded in UTF-16LE.
//
// Returns the index of the matching embedded file, or -1 if there is none.
FPDF_EXPORT int FPDF_CALLCONV
EPDFDoc_GetAttachmentIndexByKey(FPDF_DOCUMENT document, FPDF_WIDESTRING key);

// Detailed outcome of the EPDFAttachment_ExtractFile* APIs.
typedef enum {
  EPDFAttachmentExtractStatus_kSuccess = 0,
  // The attachment has no embedded file stream (no /EF entry).
  EPDFAttachmentExtractStatus_kNoFileStream = 1,
  // The file stream could not be decoded.
  EPDFAttachmentExtractStatus_kDecodeFailed = 2,
  // The decoded file exceeds |max_decoded_bytes| (or 2^32 - 1 bytes, the
  // largest size these APIs can report).
  EPDFAttachmentExtractStatus_kSizeLimitExceeded = 3,
  // Writing to the destination failed (invalid FPDF_FILEWRITE, a
  // WriteBlock() failure, or an invalid output argument). The destination
  // may contain a partial file.
  EPDFAttachmentExtractStatus_kWriteFailed = 4,
} EPDFAttachmentExtractStatus;

// Experimental EmbedPDF API.
// Decode the embedded file of |attachment| ONCE and write it to |file_write|.
// Unfiltered and Flate-compressed file streams (the overwhelmingly common
// cases) are written through |file_write| in bounded chunks without
// materializing the whole decoded file; other filter chains are decoded in
// memory first, then written out. Unlike the two-call
// FPDFAttachment_GetFile() pattern, the stream is never decoded twice.
//
//   attachment        - handle to an attachment.
//   file_write        - the destination; |version| must be 1 and
//                       |WriteBlock| non-null.
//   max_decoded_bytes - fail with kSizeLimitExceeded once the decoded file
//                       would exceed this many bytes (decompression-bomb
//                       guard). 0 means unlimited.
//   out_size          - optional; receives the decoded file size in bytes.
//   out_status        - optional; receives the detailed status.
//
// Returns true on success. On failure the destination may contain a
// partial file. Output parameters are zeroed before any work is done.
FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFAttachment_ExtractFile(FPDF_ATTACHMENT attachment,
                           FPDF_FILEWRITE* file_write,
                           uint64_t max_decoded_bytes,
                           uint32_t* out_size,
                           EPDFAttachmentExtractStatus* out_status);

// Experimental EmbedPDF API.
// Decode the embedded file of |attachment| ONCE into an owned memory buffer.
// The caller must release the returned buffer with EPDF_FreeBuffer(). A
// zero-byte embedded file is a valid success: true is returned with
// |*out_buffer| set to NULL and |*out_size| set to 0.
//
//   attachment        - handle to an attachment.
//   max_decoded_bytes - fail with kSizeLimitExceeded once the decoded file
//                       would exceed this many bytes (decompression-bomb
//                       guard). 0 means unlimited.
//   out_buffer        - receives the owned buffer holding the decoded file.
//   out_size          - receives the decoded file size in bytes.
//   out_status        - optional; receives the detailed status.
//
// Returns true on success. Output parameters are zeroed before any work is
// done.
FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFAttachment_ExtractFileToOwnedBuffer(FPDF_ATTACHMENT attachment,
                                        uint64_t max_decoded_bytes,
                                        void** out_buffer,
                                        uint32_t* out_size,
                                        EPDFAttachmentExtractStatus* out_status);

// Experimental EmbedPDF API.
// Set the MIME type (Subtype) of the embedded file |attachment|.
//
//   attachment - handle to an attachment.
//   subtype    - the MIME type to be set, encoded in UTF-8.
//
// Returns true if successful.
FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFAttachment_SetSubtype(FPDF_ATTACHMENT attachment, FPDF_BYTESTRING subtype);

// Experimental EmbedPDF API.
// Set the description of the embedded file |attachment|.
//
//   attachment - handle to an attachment.
//   desc       - the description to be set, encoded in UTF-16LE.
//
// Returns true if successful.
FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFAttachment_SetDescription(FPDF_ATTACHMENT attachment, FPDF_WIDESTRING desc);

// Experimental EmbedPDF API.
// Set the file name of the embedded file |attachment|: the /UF and /F
// entries of its file specification. Every other entry is kept.
//
//   attachment - handle to an attachment.
//   name       - the file name, encoded in UTF-16LE. Must not be empty.
//
// Returns true if successful.
FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFAttachment_SetName(FPDF_ATTACHMENT attachment, FPDF_WIDESTRING name);

// Experimental EmbedPDF API.
// Get the description of the embedded file |attachment|.
//
//   attachment - handle to an attachment.
//   buffer     - buffer for holding the description, encoded in UTF-16LE.
//   buflen     - length of the buffer in bytes.
//
// Returns the length of the description in bytes.
FPDF_EXPORT unsigned long FPDF_CALLCONV
EPDFAttachment_GetDescription(FPDF_ATTACHMENT attachment,
                              FPDF_WCHAR* buffer,
                              unsigned long buflen);

// Experimental EmbedPDF API.
// Get the integer value corresponding to |key| in the params dictionary of the
// embedded file |attachment|.

//   attachment - handle to an attachment.
//   key        - the key to the requested integer value, encoded in UTF-8.
//   out_value  - pointer to the variable that will receive the integer value.
//
// Returns true if successful.
FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFAttachment_GetIntegerValue(FPDF_ATTACHMENT attachment,
                              FPDF_BYTESTRING key,
                              int* out_value);

#ifdef __cplusplus
}  // extern "C"
#endif  // __cplusplus

#endif  // PUBLIC_EPDF_ATTACHMENT_H_
