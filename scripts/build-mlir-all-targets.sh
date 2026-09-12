#!/usr/bin/env bash
# Build LLVM/MLIR with every backend.
#
# Narval registers whatever targets its LLVM was built with (main.cpp calls
# InitializeAllTargets), so which architectures the compiler can emit for is decided here, by
# LLVM_TARGETS_TO_BUILD. A tree built with X86 only answers "--build=aarch64-linux-gnu" with
# "unable to get target"; with `all` it emits for anything LLVM supports.
#
# RTTI and exceptions are not optional: narval compiles with -frtti -fexceptions.
#
# Usage: scripts/build-mlir-all-targets.sh <llvm-project source> [install prefix]
set -euo pipefail

SRC="${1:?usage: $0 <llvm-project source> [install prefix]}"
PREFIX="${2:-$HOME/llvm-all-targets}"
BUILD="$SRC/build-all-targets"

GEN_FLAGS=()
if command -v ninja >/dev/null 2>&1; then GEN_FLAGS=(-G Ninja); fi

cmake -S "$SRC/llvm" -B "$BUILD" "${GEN_FLAGS[@]}" \
  -DCMAKE_INSTALL_PREFIX="$PREFIX" \
  -DLLVM_ENABLE_PROJECTS=mlir \
  -DLLVM_TARGETS_TO_BUILD=all \
  -DCMAKE_BUILD_TYPE=Release \
  -DLLVM_ENABLE_RTTI=ON \
  -DLLVM_ENABLE_EH=ON \
  -DLLVM_ENABLE_ASSERTIONS=OFF \
  -DLLVM_INCLUDE_TESTS=OFF \
  -DLLVM_INCLUDE_BENCHMARKS=OFF \
  -DLLVM_INCLUDE_EXAMPLES=OFF \
  -DLLVM_INCLUDE_DOCS=OFF \
  -DMLIR_ENABLE_BINDINGS_PYTHON=OFF

cmake --build "$BUILD" --target install -j"$(nproc)"

cat <<EOF

MLIR with all targets installed at:
   $PREFIX

Configure narval against it with:
   cmake -S . -B build-all-targets -DNARVAL_BUILD_LSP=OFF \
         -DMLIR_DIR=$PREFIX/lib/cmake/mlir \
         -DLLVM_DIR=$PREFIX/lib/cmake/llvm

Check which targets answer, once built:
   for t in aarch64-linux-gnu riscv64-unknown-elf x86_64-pc-windows-msvc nvptx64-nvidia-cuda; do
       printf '%-28s ' "\$t"; echo 'write("x");' > /tmp/t.nv
       ./build-all-targets/narval --build="\$t" --emit-llvm /tmp/t.nv >/dev/null 2>&1 && echo ok || echo "sem backend"
   done
EOF
