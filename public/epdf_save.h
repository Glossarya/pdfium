// Copyright 2026 CloudPDF LTD
// SPDX-License-Identifier: Apache-2.0

#ifndef PUBLIC_EPDF_SAVE_H_
#define PUBLIC_EPDF_SAVE_H_

// NOLINTNEXTLINE(build/include)
#include "fpdf_save.h"

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

// EmbedPDF's additions to public/fpdf_save.h: saves to buffers the caller
// frees, saves with options, and layer saves.

// Function: EPDF_FreeBuffer
//          Releases a buffer returned by an EPDF_*ToOwnedBuffer() API.
// Parameters:
//          buffer      -   Buffer returned by an EPDF owned-buffer API, or
//                          NULL.
FPDF_EXPORT void FPDF_CALLCONV EPDF_FreeBuffer(void* buffer);

// Function: EPDF_SaveDocumentToOwnedBuffer
//          Saves the copy of specified document into an owned memory buffer.
//          The caller must release the returned buffer with EPDF_FreeBuffer().
// Parameters:
//          document    -   Handle to document.
//          flags       -   Flags above that affect how the PDF gets saved.
//          out_size    -   Receives the size of the returned buffer.
// Return value:
//          Owned buffer if successful, NULL if failed.
FPDF_EXPORT void* FPDF_CALLCONV
EPDF_SaveDocumentToOwnedBuffer(FPDF_DOCUMENT document,
                               FPDF_DWORD flags,
                               unsigned long* out_size);

// Function: EPDF_SaveDocumentToOwnedBufferWithVersion
//          Same as EPDF_SaveDocumentToOwnedBuffer(), except the file version of
//          the saved document can be specified by the caller.
FPDF_EXPORT void* FPDF_CALLCONV
EPDF_SaveDocumentToOwnedBufferWithVersion(FPDF_DOCUMENT document,
                                          FPDF_DWORD flags,
                                          unsigned long* out_size,
                                          int file_version);

// EmbedPDF(layer-save): what a save found. kUnchangedSinceLoad means nothing
// was written because no reachable object differs from the document the
// session was opened with: the loaded bytes ARE the document
// (EPDFDoc_ReadLoadedBytes).
typedef enum {
  EPDFSaveStatus_kFailed = 0,
  EPDFSaveStatus_kWritten = 1,
  EPDFSaveStatus_kUnchangedSinceLoad = 2,
} EPDFSaveStatus;

// Function: EPDF_SaveDocumentToOwnedBufferEx
//          EPDF_SaveDocumentToOwnedBufferWithVersion, reporting whether the
//          save wrote anything. For a layer document saved incrementally,
//          overlay objects equal to their base twin are never written, and
//          when no reachable object differs from the document the layer was
//          opened with the result is NULL, |out_size| is 0 and |out_status|
//          is EPDFSaveStatus_kUnchangedSinceLoad: read the loaded bytes
//          instead. |file_version| 0 keeps the document's version.
FPDF_EXPORT void* FPDF_CALLCONV
EPDF_SaveDocumentToOwnedBufferEx(FPDF_DOCUMENT document,
                                 FPDF_DWORD flags,
                                 int file_version,
                                 unsigned long* out_size,
                                 EPDFSaveStatus* out_status);

// Function: EPDF_SaveAsCopyEx
//          FPDF_SaveAsCopy, reporting the same. On kUnchangedSinceLoad no
//          byte reaches |file_write|.
FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDF_SaveAsCopyEx(FPDF_DOCUMENT document,
                  FPDF_FILEWRITE* file_write,
                  FPDF_DWORD flags,
                  EPDFSaveStatus* out_status);

// Runtime-side status for saving a layer delta.
typedef enum {
  EPDFLayerSaveStatus_kSuccess = 0,
  EPDFLayerSaveStatus_kAppendOnlyOffsetTooLarge = 1,
  EPDFLayerSaveStatus_kSaveFailed = 2,
} EPDFLayerSaveStatus;

// Function: EPDFLayer_SaveDelta
//          Saves only a layer document's current overlay delta. The caller can
//          materialize the layer as base bytes followed by the written delta.
// Parameters:
//          layer      -   A layer document returned by EPDFLayer_OpenLayer().
//          file_write -   A pointer to a custom file write structure.
//          out_status -   Optional detailed save status.
// Return value:
//          TRUE if succeed, FALSE if failed.
FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFLayer_SaveDelta(FPDF_DOCUMENT layer,
                    FPDF_FILEWRITE* file_write,
                    EPDFLayerSaveStatus* out_status);

// Function: EPDFLayer_SaveLayerArtifact
//          Saves a server-facing layer artifact containing base identity
//          metadata and the raw layer delta. Unlike the owned-buffer variant,
//          this writes to the caller-supplied FPDF_FILEWRITE and is suitable
//          for native/server paths that should avoid materializing the whole
//          artifact in memory.
// Parameters:
//          layer      -   A layer document returned by EPDFLayer_OpenLayer().
//          file_write -   A pointer to a custom file write structure.
//          out_status -   Optional detailed save status.
// Return value:
//          TRUE if succeed, FALSE if failed.
FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFLayer_SaveLayerArtifact(FPDF_DOCUMENT layer,
                            FPDF_FILEWRITE* file_write,
                            EPDFLayerSaveStatus* out_status);

// Function: EPDFLayer_SaveDeltaToOwnedBuffer
//          Saves only a layer document's current overlay delta into an owned
//          memory buffer. The caller must release the returned buffer with
//          EPDF_FreeBuffer().
FPDF_EXPORT void* FPDF_CALLCONV
EPDFLayer_SaveDeltaToOwnedBuffer(FPDF_DOCUMENT layer,
                                 unsigned long* out_size,
                                 EPDFLayerSaveStatus* out_status);

// Function: EPDFLayer_SaveLayerArtifactToOwnedBuffer
//          Saves a server-facing layer artifact containing base identity
//          metadata and the raw layer delta. The caller must release the
//          returned buffer with EPDF_FreeBuffer().
FPDF_EXPORT void* FPDF_CALLCONV
EPDFLayer_SaveLayerArtifactToOwnedBuffer(FPDF_DOCUMENT layer,
                                         unsigned long* out_size,
                                         EPDFLayerSaveStatus* out_status);

// Function: EPDFLayer_SaveDeltaEx
//          EPDFLayer_SaveDelta, reporting whether anything reachable changed
//          since the layer was opened. When nothing did, nothing reaches
//          |file_write| and |out_changed_since_load| is false.
FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFLayer_SaveDeltaEx(FPDF_DOCUMENT layer,
                      FPDF_FILEWRITE* file_write,
                      EPDFLayerSaveStatus* out_status,
                      FPDF_BOOL* out_changed_since_load);

// Function: EPDFLayer_SaveLayerArtifactEx
//          EPDFLayer_SaveLayerArtifact with the same report; nothing reaches
//          |file_write| when nothing changed since load. The delta inside
//          the artifact is streamed through a temporary file, never held
//          whole in memory.
FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFLayer_SaveLayerArtifactEx(FPDF_DOCUMENT layer,
                              FPDF_FILEWRITE* file_write,
                              EPDFLayerSaveStatus* out_status,
                              FPDF_BOOL* out_changed_since_load);

// Function: EPDFLayer_SaveDeltaToOwnedBufferEx
//          EPDFLayer_SaveDeltaToOwnedBuffer, reporting whether anything
//          reachable changed since the layer was opened. When nothing did,
//          nothing is written (NULL, size 0, kSuccess) and
//          |out_changed_since_load| is false: the caller keeps the delta the
//          layer was opened with. The delta written otherwise is cumulative
//          against the base; it is empty (NULL, size 0, kSuccess, changed
//          true) when the layer came to equal its base.
FPDF_EXPORT void* FPDF_CALLCONV
EPDFLayer_SaveDeltaToOwnedBufferEx(FPDF_DOCUMENT layer,
                                   unsigned long* out_size,
                                   EPDFLayerSaveStatus* out_status,
                                   FPDF_BOOL* out_changed_since_load);

// Function: EPDFLayer_SaveLayerArtifactToOwnedBufferEx
//          EPDFLayer_SaveLayerArtifactToOwnedBuffer with the same report;
//          NULL and size 0 when nothing changed since load.
FPDF_EXPORT void* FPDF_CALLCONV
EPDFLayer_SaveLayerArtifactToOwnedBufferEx(FPDF_DOCUMENT layer,
                                           unsigned long* out_size,
                                           EPDFLayerSaveStatus* out_status,
                                           FPDF_BOOL* out_changed_since_load);

#ifdef __cplusplus
}  // extern "C"
#endif  // __cplusplus

#endif  // PUBLIC_EPDF_SAVE_H_
