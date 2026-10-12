#!/usr/bin/env bash
# Copyright 2026 CloudPDF LTD
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

# EmbedPDF: build and run the thread-confined runtime soak harness
# (testing/tools:epdf_thread_soak). This is the gate for the thread_local
# globals work: run it (ideally under TSAN) before lifting the server pool cap.
#
# Usage:
#   thread-soak-target.sh <target> <pdf>... [-- <soak args...>]
#
# Env:
#   EMBEDPDF_TLS_GLOBALS  default "true". Set "false" to build the baseline
#                         (process-global) variant for an A/B comparison.
#   EMBEDPDF_TSAN         default "0". Set "1" to build with is_tsan=true; the
#                         run then stops at the first report (TSAN_OPTIONS).

SOURCE_DIR="${PDF_RUNTIME_SOURCE_DIR:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}"
TARGET="${1:-}"
EMBEDPDF_TLS_GLOBALS="${EMBEDPDF_TLS_GLOBALS:-true}"
EMBEDPDF_TSAN="${EMBEDPDF_TSAN:-0}"

shift || true
PDFS=()
while [[ $# -gt 0 && "$1" != "--" ]]; do
  PDFS+=("$1")
  shift
done
if [[ "${1:-}" == "--" ]]; then
  shift
fi
if [[ -z "$TARGET" || ${#PDFS[@]} -eq 0 ]]; then
  echo "usage: $0 <target> <pdf>... [-- <soak args...>]" >&2
  exit 1
fi

case "$TARGET" in
  darwin-arm64)
    GN_TARGET_OS="mac"
    GN_TARGET_CPU="arm64"
    ;;
  darwin-x64)
    GN_TARGET_OS="mac"
    GN_TARGET_CPU="x64"
    ;;
  linux-x64)
    GN_TARGET_OS="linux"
    GN_TARGET_CPU="x64"
    ;;
  linux-arm64)
    GN_TARGET_OS="linux"
    GN_TARGET_CPU="arm64"
    EXTRA_ARGS=$'\narm_control_flow_integrity="none"'
    ;;
  *)
    echo "thread-soak-target.sh only supports host-native targets: darwin-arm64, darwin-x64, linux-x64, linux-arm64" >&2
    exit 1
    ;;
esac

TSAN_ARG=""
if [[ "$EMBEDPDF_TSAN" == "1" ]]; then
  TSAN_ARG=$'\nis_tsan=true'
fi

PDF_RUNTIME_TARGET_OS_LIST="${PDF_RUNTIME_TARGET_OS_LIST:-$GN_TARGET_OS}" \
  "$SOURCE_DIR/scripts/embedpdf-runtime/ensure-deps.sh"

"$SOURCE_DIR/scripts/embedpdf-runtime/apply-patches.sh" "$TARGET"

if [[ "$TARGET" == linux-* ]]; then
  (
    cd "$SOURCE_DIR"
    # EmbedPDF: as in test-target.sh, skip the apt install when the image
    # already holds this install-build-deps.py's packages.
    if [[ -n "${EMBEDPDF_BUILD_DEPS_INSTALLED_FROM:-}" ]] &&
      cmp -s "$EMBEDPDF_BUILD_DEPS_INSTALLED_FROM" build/install-build-deps.py; then
      echo "Linux build dependencies preinstalled from this install-build-deps.py"
    elif build/install-build-deps.sh --quick-check >/dev/null 2>&1; then
      echo "Linux build dependencies already installed"
    else
      build/install-build-deps.sh --no-prompt
    fi
    build/linux/sysroot_scripts/install-sysroot.py "--arch=$GN_TARGET_CPU"
  )
fi

OUT="$SOURCE_DIR/out/embedpdf-runtime-thread-soak-$TARGET"
mkdir -p "$OUT"

cat > "$OUT/args.gn" <<EOF
is_debug=true
treat_warnings_as_errors=false
pdf_use_skia=false
pdf_enable_xfa=false
pdf_enable_v8=false
is_component_build=false
clang_use_chrome_plugins=false
pdf_is_standalone=true
use_debug_fission=false
pdf_is_complete_lib=false
pdf_use_partition_alloc=false
embedpdf_thread_local_globals=$EMBEDPDF_TLS_GLOBALS
symbol_level=1
target_os="$GN_TARGET_OS"
target_cpu="$GN_TARGET_CPU"${TSAN_ARG}${EXTRA_ARGS:-}
EOF

(
  cd "$SOURCE_DIR"
  gn gen "$OUT"
  ninja -C "$OUT" testing/tools:epdf_thread_soak
)

if [[ "$EMBEDPDF_TSAN" == "1" ]]; then
  export TSAN_OPTIONS="${TSAN_OPTIONS:-halt_on_error=1 second_deadlock_stack=1}"
fi
echo "=== running epdf_thread_soak (tls=$EMBEDPDF_TLS_GLOBALS tsan=$EMBEDPDF_TSAN) ==="
"$OUT/epdf_thread_soak" "${PDFS[@]}" "$@"
