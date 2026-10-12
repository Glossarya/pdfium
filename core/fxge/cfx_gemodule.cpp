// Copyright 2016 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Original code copyright 2014 Foxit Software Inc. http://www.foxitsoftware.com

#include "core/fxge/cfx_gemodule.h"

#include "core/fxcrt/check.h"
#include "core/fxcrt/epdf_tls.h"
#include "core/fxge/cfx_folderfontinfo.h"
#include "core/fxge/cfx_fontmgr.h"

namespace {

// EmbedPDF(thread-confined-globals): each worker thread owns its own
// CFX_GEModule (FreeType FT_Library + font/glyph caches), created and
// destroyed on that thread.
EPDF_TLS CFX_GEModule* g_GEModule = nullptr;

}  // namespace

CFX_GEModule::CFX_GEModule(const char** pUserFontPaths)
    : platform_(PlatformIface::Create()),
      font_mgr_(std::make_unique<CFX_FontMgr>()),
      user_font_paths_(pUserFontPaths) {}

CFX_GEModule::~CFX_GEModule() = default;

// static
void CFX_GEModule::Create(const char** pUserFontPaths) {
  DCHECK(!g_GEModule);
  g_GEModule = new CFX_GEModule(pUserFontPaths);
  g_GEModule->platform_->Init();
  g_GEModule->font_mgr_->GetBuiltinMapper()->SetSystemFontInfo(
      g_GEModule->platform_->CreateDefaultSystemFontInfo());
}

// static
void CFX_GEModule::Destroy() {
  DCHECK(g_GEModule);
  g_GEModule->platform_->Terminate();
  delete g_GEModule;
  g_GEModule = nullptr;
}

// static
CFX_GEModule* CFX_GEModule::Get() {
  DCHECK(g_GEModule);
  return g_GEModule;
}
