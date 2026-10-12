// Copyright 2026 CloudPDF LTD
// SPDX-License-Identifier: Apache-2.0

#ifndef FPDFSDK_EPDF_C_API_TEST_H_
#define FPDFSDK_EPDF_C_API_TEST_H_

#ifdef __cplusplus
extern "C" {
#endif

// Function to call from gtest harness to ensure linker resolution.
int CheckEmbedPDFCApi(void);

#ifdef __cplusplus
}
#endif

#endif  // FPDFSDK_EPDF_C_API_TEST_H_
