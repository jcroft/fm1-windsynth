// SPDX-License-Identifier: GPL-3.0-only
// The wind voice in the browser build: the fixed sweep (wind_sweep.h) rendered by the WebAssembly
// wind voice must match the host's render of it (wind_voice_test.c), sample for sample.
//   node tests/host/wind_wasm_test.mjs build/emu/kalimba.wasm build/host/wind/wind_sweep.f32
import fs from "fs";
const STEPS = 64, LEN = 960;
const wasm = fs.readFileSync(process.argv[2]);
const host = new Float32Array(fs.readFileSync(process.argv[3]).buffer.slice(0));
let mem;
const wasi = new Proxy({}, { get: () => () => 0 });
const { instance } = await WebAssembly.instantiate(wasm, { wasi_snapshot_preview1: wasi, env: new Proxy({}, { get: () => () => 0 }) });
const ex = instance.exports; mem = ex.memory;
if (ex._initialize) ex._initialize();
// sweep_params() in wind_sweep.h, in float32
const f = Math.fround;
ex.wv_init();
let worst = 0, at = -1;
for (let s = 0; s < STEPS; s++) {
  const note = f(24 + f(s * 1.125)), timbre = f((s % 16) / 15), morph = f(((s * 5) % 16) / 15), harm = f(((s * 11) % 16) / 15);
  ex.wv_render_raw(note, timbre, morph, harm, ex.web_out_l(), LEN);
  const out = new Float32Array(mem.buffer, ex.web_out_l(), LEN);
  for (let i = 0; i < LEN; i++) {
    const d = Math.abs(out[i] - host[s * LEN + i]);
    if (!(d <= worst)) { worst = d; at = s * LEN + i; }
  }
}
console.log("wasm sweep: worst difference from the host", worst, at >= 0 ? `at ${at}` : "");
process.exit(host.length === STEPS * LEN && worst === 0 ? 0 : 1);
