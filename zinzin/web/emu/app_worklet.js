// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Yoann
// ZINZIN's part of fm1-base's AudioWorklet (base/web/emu/worklet.js loads this first): about 15 times a second,
// where the knobs are, the track picked, the mood and the tempo, for the page (zinzin/web.c's zz_web_state); and
// the page's MIDI in (a computer's MIDI keyboard), as USB-MIDI packets.
globalThis.fm1App = {
  n: 0,
  message(fm1, m) {
    if (m.midi !== undefined && fm1.ex.web_midi_in) fm1.ex.web_midi_in(0, m.midi >>> 0);
  },
  process(fm1) {
    const ex = fm1.ex;
    if (!ex.zz_web_state || this.n++ % 24) return;          // 24 blocks of 128 frames: every 70 ms
    const st = new Float32Array(fm1.mem.buffer, ex.zz_web_state(), ex.zz_web_state_n()).slice();
    fm1.port.postMessage({ type: "app", st }, [st.buffer]);
  },
};
