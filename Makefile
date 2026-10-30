# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Yoann
# ZINZIN: everything that runs on a computer. The firmware itself is built by fm1-base (base/, a submodule).
CC ?= cc
CFLAGS = -O2 -std=c99 -ffp-contract=off -Wall -Wextra -Wno-unused-parameter -Wno-unused-function -DZZ_LIBM
INC = -Ihostsim -Izinzin -Izinzin/gen -Izinzin/dsp
OUT = build

.PHONY: all gen test pics firmware host-base clean
all: test

# the generated tables (pictures, fonts, sine): checked in, made again from assets/
gen:
	python3 tools/make_assets.py
	python3 tools/make_font.py
	python3 tools/make_tables.py

$(OUT)/engine_test: tests/engine_test.c zinzin/dsp/*.c zinzin/dsp/*.h zinzin/gen/*.h
	@mkdir -p $(OUT)
	$(CC) $(CFLAGS) $(INC) tests/engine_test.c zinzin/dsp/zz_engine.c -lm -o $@

$(OUT)/host: hostsim/*.c hostsim/*.h zinzin/*.c zinzin/*.h zinzin/dsp/*.c zinzin/dsp/*.h zinzin/gen/*.h
	@mkdir -p $(OUT)
	$(CC) $(CFLAGS) $(INC) hostsim/host.c zinzin/zz_draw.c zinzin/dsp/zz_engine.c -lm -o $@

# the engine's checks (levels, NaN, every bêtise and mood: WAVs in build/wav), then the whole app on a script
test: $(OUT)/engine_test $(OUT)/host
	@mkdir -p $(OUT)/wav $(OUT)/host_out
	$(OUT)/engine_test $(OUT)/wav
	$(OUT)/host $(OUT)/host_out
	python3 tools/host_pics.py $(OUT)/host_out 2

# the firmware (JieLi toolchain and AC79 SDK: see base/README.md): zinzin/build/zinzin.fwsc
firmware:
	cd base && python3 tools/build.py ../zinzin

# fm1-base's own simulator and its browser emulator, with the real platform code
host-base:
	cd base && python3 tools/build.py ../zinzin --host

clean:
	rm -rf $(OUT) zinzin/build
