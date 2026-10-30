#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Yoann
# ZINZIN: the firmware from a fresh Linux x86-64 (WSL on Windows, Debian or Ubuntu): what the build needs,
# fm1-base, JieLi's toolchain and the AC79 SDK's three package files, then a release build and its web installer
# (with m-bootloader), served on http://localhost:8000/install/ for Chrome or Edge. Under WSL the package is also
# copied to Windows' Downloads\zinzin.
#   sh tools/build_wsl.sh [VERSION]       (default 0.1)
set -e
cd "$(dirname "$0")/.."
VERSION="${1:-0.1}"
need=""
for p in git curl xz python3; do command -v "$p" >/dev/null 2>&1 || need=1; done
python3 -c "import PIL" 2>/dev/null || need=1
if [ -n "$need" ]; then
    echo "== the tools (git, curl, xz, python3 with Pillow)"
    sudo apt-get update
    sudo apt-get install -y git curl xz-utils python3 python3-pil ca-certificates
fi
echo "== fm1-base"
sh tools/get_base.sh
echo "== JieLi's toolchain"
[ -x "$HOME/.jieli/toolchain/pi32v2/bin/clang" ] || sh base/tools/get_toolchain.sh "$HOME/.jieli"
export JIELI_TOOLCHAIN="$HOME/.jieli/toolchain"
echo "== the AC79 SDK (cpu/wl82/tools only)"
SDK="${AC79_SDK:-$HOME/fw-AC79_AIoT_SDK}"
if [ ! -f "$SDK/cpu/wl82/tools/uboot.boot" ]; then
    rm -rf "$SDK"
    if git clone --depth 1 --filter=blob:none --sparse https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK "$SDK"; then
        git -C "$SDK" sparse-checkout set cpu/wl82/tools
    else                                          # (no partial clone: the whole SDK, once)
        rm -rf "$SDK"
        git clone --depth 1 https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK "$SDK"
    fi
fi
export AC79_SDK="$SDK"
echo "== ZINZIN $VERSION"
(cd base && python3 tools/build.py ../zinzin --release "$VERSION")
F=$(ls zinzin/build/*-"$VERSION".fwsc | head -1)
echo "firmware: $F"
python3 tools/make_installer.py "$F" "$VERSION" zinzin/build/installer
if command -v wslpath >/dev/null 2>&1 && command -v cmd.exe >/dev/null 2>&1; then
    W="$(wslpath "$(cmd.exe /c 'echo %USERPROFILE%' 2>/dev/null | tr -d '\r')")/Downloads/zinzin"
    mkdir -p "$W"
    cp "$F" base/tools/fm1_install.py "$W/"
    echo "Windows: Downloads\\zinzin\\ ($(basename "$F"), fm1_install.py)"
fi
echo
echo "== The installer: open  http://localhost:8000/install/  in Edge or Chrome (Ctrl+C here to stop it)"
python3 -m http.server 8000 --directory zinzin/build/installer
