// Copyright 2026 CloudPDF LTD
// SPDX-License-Identifier: Apache-2.0

// EmbedPDF: thread-confined runtime lifecycle.
//
// EPDF_InitThread / EPDF_ShutdownThread make the per-thread PDFium lifecycle
// explicit for callers that run PDFium on worker threads. With
// embedpdf_thread_local_globals enabled (see core/fxcrt/epdf_tls.h) each worker
// thread owns its own PDFium globals, so every such thread must initialize and
// tear down PDFium itself. With the flag disabled these are exact aliases of
// FPDF_InitLibrary / FPDF_DestroyLibrary.
//
// These are intentionally exported for ALL targets (including wasm, where they
// simply wrap the normal init/destroy) so shared runtime code can call a single
// lifecycle entry point regardless of platform.

#include "public/epdf_view.h"

#include "public/fpdfview.h"

FPDF_EXPORT void FPDF_CALLCONV EPDF_InitThread() {
  // Routes through FPDF_InitLibrary so the standard config/init path runs for
  // the calling thread.
  FPDF_InitLibrary();
}

FPDF_EXPORT void FPDF_CALLCONV EPDF_ShutdownThread() {
  // Lifecycle-strict: callers must have already closed every PDFium handle
  // created on this thread before invoking this.
  FPDF_DestroyLibrary();
}
