// Copyright 2026 CloudPDF LTD
// SPDX-License-Identifier: Apache-2.0

#ifndef PUBLIC_EPDF_LAYER_H_
#define PUBLIC_EPDF_LAYER_H_

// NOLINTNEXTLINE(build/include)
#include "fpdfview.h"

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

// EmbedPDF's base documents and layers: a frozen base document many layers
// share, the layers opened on it, their transactions and their saves.

// A base document: loaded once, frozen, and shared by the layers opened on it.
typedef struct fpdf_base_document_t__* EPDF_BASE_DOCUMENT;

// Function: EPDF_LoadBaseDocument
//          Load and freeze a shareable base PDF document from a custom access
//          descriptor. The returned handle is distinct from FPDF_DOCUMENT and
//          cannot be used with ordinary document APIs such as FPDF_LoadPage().
// Parameters:
//          pFileAccess -   A structure for accessing the file.
//          password    -   Optional password for decrypting the PDF file.
// Return value:
//          A handle to the loaded base document, or NULL on failure.
FPDF_EXPORT EPDF_BASE_DOCUMENT FPDF_CALLCONV
EPDF_LoadBaseDocument(FPDF_FILEACCESS* pFileAccess, FPDF_BYTESTRING password);

// Function: EPDF_LoadMemBaseDocument
//          Load and freeze a shareable base PDF document from memory. The
//          returned handle is distinct from FPDF_DOCUMENT and cannot be used
//          with ordinary document APIs such as FPDF_LoadPage().
// Parameters:
//          data_buf    -   Pointer to a buffer containing the PDF document.
//          size        -   Number of bytes in the PDF document.
//          password    -   Optional password for decrypting the PDF file.
// Return value:
//          A handle to the loaded base document, or NULL on failure.
// Comments:
//          The memory buffer must remain valid until the returned base document
//          is released with EPDF_ReleaseBaseDocument().
//
//          See the comments for FPDF_LoadDocument() regarding the encoding for
//          |password|.
FPDF_EXPORT EPDF_BASE_DOCUMENT FPDF_CALLCONV
EPDF_LoadMemBaseDocument(const void* data_buf,
                         int size,
                         FPDF_BYTESTRING password);

// Function: EPDF_LoadMemBaseDocument64
//          Load and freeze a shareable base PDF document from memory. The
//          returned handle is distinct from FPDF_DOCUMENT and cannot be used
//          with ordinary document APIs such as FPDF_LoadPage().
// Parameters:
//          data_buf    -   Pointer to a buffer containing the PDF document.
//          size        -   Number of bytes in the PDF document.
//          password    -   Optional password for decrypting the PDF file.
// Return value:
//          A handle to the loaded base document, or NULL on failure.
// Comments:
//          The memory buffer must remain valid until the returned base document
//          is released with EPDF_ReleaseBaseDocument().
//
//          See the comments for FPDF_LoadDocument() regarding the encoding for
//          |password|.
FPDF_EXPORT EPDF_BASE_DOCUMENT FPDF_CALLCONV
EPDF_LoadMemBaseDocument64(const void* data_buf,
                           size_t size,
                           FPDF_BYTESTRING password);

// Function: EPDF_ReleaseBaseDocument
//          Release a base document returned by EPDF_LoadBaseDocument() or
//          EPDF_LoadMemBaseDocument()/EPDF_LoadMemBaseDocument64().
FPDF_EXPORT void FPDF_CALLCONV
EPDF_ReleaseBaseDocument(EPDF_BASE_DOCUMENT base);

// Function: EPDF_SetBaseDocumentSha256
//          Supply the SHA-256 of the exact bytes |base| was loaded from, so
//          the runtime need not hash them again. Without it the runtime
//          hashes the file on first use (a layer artifact carries the hash;
//          a completed signature reports it as the version). Call it right
//          after loading, before any layer is opened on |base|.
//          |sha256| points at 32 bytes. This is an identity claim, not a
//          check: a wrong value only makes the caller's own layer artifacts
//          fail to open on the real bytes.
FPDF_EXPORT void FPDF_CALLCONV
EPDF_SetBaseDocumentSha256(EPDF_BASE_DOCUMENT base,
                           const unsigned char* sha256);

// Function: EPDFLayer_GetBaseSha256
//          Copy the SHA-256 (32 bytes) of the base a layer document was
//          opened on into |out_sha256|. Returns FALSE when |layer| is not a
//          layer document or |out_sha256| is NULL.
FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFLayer_GetBaseSha256(FPDF_DOCUMENT layer, unsigned char* out_sha256);

// Runtime-side status for opening a layer on top of a base document.
typedef enum {
  EPDFLayerOpenStatus_kSuccess = 0,
  EPDFLayerOpenStatus_kPasswordRequired = 1,
  EPDFLayerOpenStatus_kMalformedDelta = 2,
  EPDFLayerOpenStatus_kBaseLayerMismatch = 3,
  EPDFLayerOpenStatus_kOpenFailed = 4,
} EPDFLayerOpenStatus;

// Function: EPDFLayer_OpenLayer
//          Open a layer view on top of a previously loaded base document.
//          The returned handle is an ordinary FPDF_DOCUMENT and must be closed
//          with FPDF_CloseDocument().
// Parameters:
//          base        -   A base document returned by EPDF_LoadBaseDocument().
//          pFileAccess -   Optional raw layer delta bytes as returned by
//                          EPDFLayer_SaveDelta(). NULL or zero-length opens a
//                          fresh empty layer.
//          password    -   Reserved for future delta-password handling.
//          out_status  -   Optional detailed open status.
// Return value:
//          A layer document handle, or NULL on failure.
FPDF_EXPORT FPDF_DOCUMENT FPDF_CALLCONV
EPDFLayer_OpenLayer(EPDF_BASE_DOCUMENT base,
                    FPDF_FILEACCESS* pFileAccess,
                    FPDF_BYTESTRING password,
                    EPDFLayerOpenStatus* out_status);

// Function: EPDFLayer_OpenLayerArtifact
//          Open a layer view from a layer artifact produced by
//          EPDFLayer_SaveLayerArtifactToOwnedBuffer(). Validates that the
//          artifact belongs to |base| before ingesting its raw delta.
FPDF_EXPORT FPDF_DOCUMENT FPDF_CALLCONV
EPDFLayer_OpenLayerArtifact(EPDF_BASE_DOCUMENT base,
                            FPDF_FILEACCESS* pFileAccess,
                            FPDF_BYTESTRING password,
                            EPDFLayerOpenStatus* out_status);

// Function: EPDFLayer_OpenLayerArtifactFromPath
//          EPDFLayer_OpenLayerArtifact over a file the runtime opens itself
//          and keeps open for the life of the layer: the delta is read in
//          place, never copied, and every stream the delta carries stays a
//          view into the file. The file must not change while the layer is
//          open. |path| is a UTF-8 file system path.
FPDF_EXPORT FPDF_DOCUMENT FPDF_CALLCONV
EPDFLayer_OpenLayerArtifactFromPath(EPDF_BASE_DOCUMENT base,
                                    FPDF_STRING path,
                                    FPDF_BYTESTRING password,
                                    EPDFLayerOpenStatus* out_status);

// Function: EPDFLayer_IsObjectPromoted
//          Return whether the object exists in the layer overlay.
FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFLayer_IsObjectPromoted(FPDF_DOCUMENT layer, unsigned long obj_num);

// Function: EPDFPage_IsValid
//          Return whether |page| still names a page of its document: its
//          object resolves and it is in the document's page list. A page
//          deleted, or created in an aborted layer transaction, is invalid;
//          an abort that brings a page back makes its handle valid again.
//          Other calls on an invalid page keep working on its last version;
//          load the page again after a transaction that may have removed it.
FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV EPDFPage_IsValid(FPDF_PAGE page);

// Function: EPDFLayer_IsPagePromoted
//          Return whether |layer| promoted the page with object number
//          |page_obj_num| (EPDFPage_PromoteInlineAnnotsRaw()): its births
//          are recorded, so its inline annotations from the base resolve
//          through them, not by position.
FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFLayer_IsPagePromoted(FPDF_DOCUMENT layer, unsigned long page_obj_num);

// Function: EPDFLayer_GetBirthObjectNumber
//          The object an inline annotation became when |layer| promoted its
//          page. |page_obj_num| and |birth_index| are its birth name: the
//          page's object number and the annotation's position in the base
//          page's /Annots.
// Return value:
//          The object number, or 0 when the page isn't promoted (the
//          annotation is still inline at its birth index) or the base had no
//          inline annotation at |birth_index|.
FPDF_EXPORT unsigned long FPDF_CALLCONV
EPDFLayer_GetBirthObjectNumber(FPDF_DOCUMENT layer,
                               unsigned long page_obj_num,
                               unsigned long birth_index);

// Function: EPDFLayer_GetBirthName
//          The birth name of object |obj_num|, when it is an inline
//          annotation from the base that |layer| promoted.
// Parameters:
//          page_obj_num - receives the page's object number.
//          birth_index  - receives its position in the base page's /Annots.
// Return value:
//          TRUE when |obj_num| has a birth name.
FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFLayer_GetBirthName(FPDF_DOCUMENT layer,
                       unsigned long obj_num,
                       unsigned long* page_obj_num,
                       unsigned long* birth_index);

// Function: EPDFLayer_BeginTransaction
//          Start a transaction on |layer|. Until it is committed or aborted,
//          every write lands in an overlay above the layer's committed objects,
//          and every read sees it. One at a time, never nested; saving during
//          a transaction is refused.
// Return value:
//          TRUE if a transaction started; FALSE for a document that is not a
//          layer, or when one is already open.
FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFLayer_BeginTransaction(FPDF_DOCUMENT layer);

// Function: EPDFLayer_CommitTransaction
//          Make the open transaction's writes part of the layer.
// Return value:
//          TRUE on success; FALSE when no transaction is open.
FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFLayer_CommitTransaction(FPDF_DOCUMENT layer);

// Function: EPDFLayer_AbortTransaction
//          Drop the open transaction's writes. The layer's objects and page
//          list are exactly as at the transaction's start; object numbers it
//          handed out are not given back.
// Return value:
//          TRUE on success; FALSE when no transaction is open.
FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFLayer_AbortTransaction(FPDF_DOCUMENT layer);

// Function: EPDFLayer_IsInTransaction
//          Return whether |layer| has an open transaction.
FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFLayer_IsInTransaction(FPDF_DOCUMENT layer);

// Function: EPDFLayer_GetLastObjectNumber
//          The last object number |layer| has handed out: new objects get
//          numbers above it. Numbers a transaction handed out count even
//          after it aborted.
// Return value:
//          The number, or 0 for a document that is not a layer.
FPDF_EXPORT unsigned long FPDF_CALLCONV
EPDFLayer_GetLastObjectNumber(FPDF_DOCUMENT layer);

// Function: EPDFLayer_RaiseLastObjectNumber
//          Raise |layer|'s last object number to |obj_num| when it is lower,
//          so new objects get numbers above it. The numbers in between are
//          handed out: a caller can create objects at them (the |objnum|
//          parameter of EPDFPage_CreateAnnotRaw(), EPDFPage_InsertBlankRaw(),
//          EPDFForm_CreateField() and EPDFForm_AttachWidget()). Never lowers
//          the number.
// Return value:
//          TRUE on success; FALSE for a document that is not a layer, or a
//          number above the parser's limit.
FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFLayer_RaiseLastObjectNumber(FPDF_DOCUMENT layer, unsigned long obj_num);

// Function: EPDFLayer_GetPromotedObjectCount
//          Return the number of objects currently stored in the layer overlay.
//          Committed objects only: an open transaction's copies and new
//          objects count once it commits.
FPDF_EXPORT unsigned long FPDF_CALLCONV
EPDFLayer_GetPromotedObjectCount(FPDF_DOCUMENT layer);

// Function: EPDFLayer_GetBaseDocument
//          Return the layer's borrowed base document handle. The caller MUST
//          NOT release the returned handle.
FPDF_EXPORT EPDF_BASE_DOCUMENT FPDF_CALLCONV
EPDFLayer_GetBaseDocument(FPDF_DOCUMENT layer);

#ifdef __cplusplus
}  // extern "C"
#endif  // __cplusplus

#endif  // PUBLIC_EPDF_LAYER_H_
