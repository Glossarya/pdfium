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
EPDF_TLS CFX_GEModule* g_pGEModule = nullptr;

}  // namespace

CFX_GEModule::CFX_GEModule(const char** pUserFontPaths)
    : platform_(PlatformIface::Create()),
      font_mgr_(std::make_unique<CFX_FontMgr>()),
      user_font_paths_(pUserFontPaths) {}

CFX_GEModule::~CFX_GEModule() = default;

// static
void CFX_GEModule::Create(const char** pUserFontPaths) {
  DCHECK(!g_pGEModule);
  g_pGEModule = new CFX_GEModule(pUserFontPaths);
  g_pGEModule->platform_->Init();
  g_pGEModule->GetFontMgr()->GetBuiltinMapper()->SetSystemFontInfo(
      g_pGEModule->platform_->CreateDefaultSystemFontInfo());
}

// static
void CFX_GEModule::Destroy() {
  DCHECK(g_pGEModule);
  delete g_pGEModule;
  g_pGEModule = nullptr;
}

// static
CFX_GEModule* CFX_GEModule::Get() {
  DCHECK(g_pGEModule);
  return g_pGEModule;
}
