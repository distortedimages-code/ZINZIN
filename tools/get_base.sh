#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Yoann
# ZINZIN: fetch fm1-base (and felucca-bsp inside it) into base/, at the commit ZINZIN was written against.
# (A git clone --recursive of this repository does the same.)
set -e
cd "$(dirname "$0")/.."
REV=d8ff8f41aa511e03d70a7c565c9fa0f1dd18ae2f
if [ ! -e base/tools/build.py ]; then
    rm -rf base
    git clone https://github.com/charlesvestal/fm1-base base
fi
git -C base checkout -q "$REV"
git -C base submodule update --init --recursive
echo "base: fm1-base $(git -C base log -1 --format=%h)"
