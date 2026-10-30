#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Yoann
# ZINZIN: the test bench's session as a video, the screen (x2, sharp pixels) and the sound together.
#   build/host build/host_out film && sh tools/host_film.sh build/host_out
set -e
D="${1:-build/host_out}"
ffmpeg -y -loglevel error -f rawvideo -pix_fmt rgb24 -s 240x240 -r 12.5 -i "$D/film.rgb" -i "$D/session.wav" \
    -vf "scale=480:480:flags=neighbor" -r 25 -c:v libx264 -preset veryfast -crf 18 -pix_fmt yuv420p \
    -c:a aac -b:a 160k -shortest "$D/zinzin_banc.mp4"
rm -f "$D/film.rgb"
echo "$D/zinzin_banc.mp4"
