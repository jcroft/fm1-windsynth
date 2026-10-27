// SPDX-License-Identifier: GPL-3.0-only
// The browser build in Node: boot it, play the panel's keys (key breath), expect sound and screen frames;
// then a MIDI note with breath: silent-ish with none, brighter and louder as breath (CC2) comes in, and
// the monitor's state (web_state) following, and the hint when breath comes on a CC that isn't Breath CC.
import fs from "fs";
const wasm = fs.readFileSync(process.argv[2]);
let mem;
const wasi = new Proxy({ clock_time_get: (id, p, out) => { new DataView(mem.buffer).setBigUint64(out, 0n, true); return 0; },
  fd_write: (fd, iov, n, out) => { new DataView(mem.buffer).setUint32(out, 0, true); return 0; } }, { get: (t, k) => k in t ? t[k] : () => 0 });
const { instance } = await WebAssembly.instantiate(wasm, { wasi_snapshot_preview1: wasi, env: new Proxy({}, { get: () => () => 0 }) });
const ex = instance.exports; mem = ex.memory;
if (ex._initialize) ex._initialize();
ex.web_master(2800); ex.web_boot();
const state = () => new Float32Array(mem.buffer, ex.web_state(), 13);
const render = (blocks) => {
  let peak = 0;
  for (let i = 0; i < blocks; i++) {
    ex.web_render(128);
    for (const v of new Float32Array(mem.buffer, ex.web_out_l(), 128)) peak = Math.max(peak, Math.abs(v));
  }
  return peak;
};
let failed = 0;
const ok = (cond, what) => { console.log(`${what.padEnd(56)} ${cond ? "ok" : "FAIL"}`); if (!cond) failed++; };

let keys = 0;
for (const k of [0, 4, 7, 12]) { ex.web_keys(1 << k); keys = Math.max(keys, render(40)); }
ex.web_keys(0);
render(400);                                             // released
ok(keys > 0.05 && keys < 1, `panel keys sound (peak ${keys.toFixed(3)})`);
ok(render(20) < 0.01, "quiet after release");

ex.web_midi(0x90, 72, 100);
const none = render(60);
ok(state()[9] === 72 && state()[1] > 0, "MIDI note 72 sounding, gate open");
const quarter = (ex.web_midi(0xB0, 2, 40), render(60)), c40 = state()[4];
const full = (ex.web_midi(0xB0, 2, 127), render(60)), c127 = state()[4];
ok(none < quarter && quarter < full, `louder with breath (${none.toFixed(3)} < ${quarter.toFixed(3)} < ${full.toFixed(3)})`);
ok(full > 0.05 && full < 1, "full breath in range");
ok(state()[2] > 0.99 && c127 > c40, `breath ${state()[2].toFixed(2)}, cutoff opens (${c40.toFixed(2)} -> ${c127.toFixed(2)})`);
ex.web_midi(0xE0, 0, 127);                               // bite: bend up
render(10);
ok(Math.abs(state()[0] - 74) < 0.05, `bend up 2 semitones (note ${state()[0].toFixed(2)})`);
ok(state()[10] >= 4, `MIDI counted (${state()[10]})`);
ex.web_midi(0x80, 72, 0);
render(200);
ok(state()[1] === 0, "note off closes the gate");
// breath on a CC that isn't Breath CC (CC74, Breath CC 2): the page's hint names it
ok(state()[11] === -1 && state()[12] === 2, `no breath hint while CC2 is the breath (${state()[11]})`);
render(800);                                             // (CC2's breath, above, a second old)
for (let v = 0; v < 100; v += 10) { ex.web_midi(0xB0, 74, v); render(4); }
ok(state()[11] === 74, `breath hint: CC74 (${state()[11]})`);
ok(ex.web_blits() > 10, `screen frames (${ex.web_blits()})`);
process.exit(failed ? 1 : 0);
