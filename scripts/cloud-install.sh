#!/usr/bin/env bash
# Cursor Cloud install / update script — keep idempotent and non-interactive.
# Avoids the FetchContent+Ninja "build.ninja still dirty" failure on warm VMs
# by clearing broken Vulkan-Headers subbuilds before reconfigure.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

echo ">>> [cloud-install] GENESIS cloud install"

# Stale ExternalProject/Ninja populate dirs are the usual startup failure mode.
if [[ -d build/_deps ]]; then
  find build/_deps -maxdepth 1 -type d -name '*-subbuild' -exec rm -rf {} +
  echo ">>> [cloud-install] cleared build/_deps/*-subbuild"
fi

cmake -B build -G Ninja \
  -DCMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE:-Debug}" \
  -DCMAKE_C_COMPILER="${CMAKE_C_COMPILER:-gcc}" \
  -DCMAKE_CXX_COMPILER="${CMAKE_CXX_COMPILER:-g++}"

echo "<<< [cloud-install] configure complete"
