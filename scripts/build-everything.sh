#!/usr/bin/env bash
# Build the compiler and the language server, for Linux and for Windows, in one go.
#
#   Linux   uses the MLIR at ~/llvm/mlir-all-targets (every backend, RTTI on)
#   Windows uses the MLIR at D:\llvm\mlir-22.1.5-install, through the NATIVE MSVC
#           build in D:\llvm\build-narval-lsp-windows.bat. The MinGW cross build
#           cannot drive that MLIR: its mlir-tblgen.exe is a Windows program and
#           the include paths handed to it would be Linux ones.
#
# Artifacts:
#   <ext>/bin/linux-x64/narval-lsp           language server, Linux
#   <build-lsp-alltargets>/narval            compiler, ELF, every backend
#   <ext>/bin/win32-x64/narval-lsp.exe       language server, Windows
#   D:\narval-src\build-win\Release\narval.exe   compiler, Windows
set -euo pipefail

NARVAL="/home/bacal/projects/cpp/narval"
EXT="/mnt/c/Users/user/Documents/projetos/narval grammar"
MLIR_LINUX="$HOME/llvm/mlir-all-targets"
BUILD_LINUX="$NARVAL/build-lsp-alltargets"
BAT_WIN='D:\llvm\build-narval-lsp-windows.bat'

if [ ! -f "$MLIR_LINUX/lib/cmake/mlir/MLIRConfig.cmake" ]; then
    echo "MLIR do Linux nao encontrado em $MLIR_LINUX" >&2
    echo "configure a fonte primeiro (configure-mlir-all-src.sh) e deixe o build terminar" >&2
    exit 1
fi

echo "=== Linux: narval (ELF) + narval-lsp, contra $MLIR_LINUX ==="
# -j1 e' a convencao deste projeto: -jN com N>1 derruba o build.
cmake -S "$NARVAL" -B "$BUILD_LINUX" -DNARVAL_BUILD_LSP=ON \
      -DMLIR_DIR="$MLIR_LINUX/lib/cmake/mlir" \
      -DLLVM_DIR="$MLIR_LINUX/lib/cmake/llvm"
cmake --build "$BUILD_LINUX" --target NarvalLsp Narval -j1

cp "$BUILD_LINUX/narval-lsp" "$EXT/bin/linux-x64/narval-lsp"
chmod 755 "$EXT/bin/linux-x64/narval-lsp"
echo "  -> $EXT/bin/linux-x64/narval-lsp"
echo "  -> $BUILD_LINUX/narval   (ELF, todos os backends)"

echo
echo "=== Windows: narval-lsp.exe + narval.exe, MSVC nativo ==="
# cmd.exe nao aceita um caminho WSL como pasta atual (avisa sobre caminho UNC e
# padroniza para uma pasta do Windows). Chamar de /mnt/d evita o aviso; o .bat usa
# caminhos absolutos, entao a pasta nao influencia o resultado.
cd /mnt/d/llvm
if cmd.exe /c "$BAT_WIN nopause" < /dev/null; then
    echo "  -> $EXT/bin/win32-x64/narval-lsp.exe"
    echo "  -> D:\\narval-src\\build-win\\Release\\narval.exe"
else
    echo "  (o .bat retornou erro; a saida acima diz qual alvo falhou)" >&2
fi

echo
echo "Tudo pronto."
