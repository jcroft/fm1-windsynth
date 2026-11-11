// SPDX-License-Identifier: GPL-3.0-only
// The page's sample banks (web/fm1samples.js): the same bytes and SysEx as tools/fm1_samples.py (its
// bank and messages from tests/samples_test.py), pitch found in pitched sounds and not in noise, loops
// that join without a click, mu-law, and a bank too damaged to take.
//   node tests/web/samples_test.mjs [DIR]     (DIR: tests/samples_test.py's, default build/samples)
import { readFileSync } from "node:fs";
import * as S from "../../web/fm1samples.js";

let failed = 0;
const ok = (cond, what) => { console.log(`${what.padEnd(78)} ${cond ? "ok" : "FAIL"}`); if (!cond) failed++; };
const dir = process.argv[2] || "build/samples";

// the Python tool's bank, read and built again: byte for byte; its SysEx, message for message
const py = new Uint8Array(readFileSync(`${dir}/bank.bin`));
const slots = S.parseBank(py);
ok(slots.filter(Boolean).length === 2 && slots[0].name === "A440" && slots[2].fmt === S.FMT_ULAW, "the Python bank read: slots 1 and 3");
const again = S.buildBank(slots.slice(0, 3));
ok(again.length === py.length && again.every((v, i) => v === py[i]), "built again: the same bytes");
const lines = readFileSync(`${dir}/load.txt`, "utf8").trim().split("\n");
const ms = S.frames(py);
ok(ms.length === lines.length && ms.every((m, i) => [...m].map((v) => v.toString(16).toUpperCase().padStart(2, "0")).join(" ") === lines[i]),
   `its SysEx: the same ${lines.length} messages`);
const bad = py.slice();
bad[bad.length - 1] ^= 1;
ok((() => { try { S.parseBank(bad); return false; } catch { return true; } })(), "a damaged bank is refused");
const reply = Uint8Array.from([0xF0, 0x7D, 0x57, 0x53, 0x01, 0x41, ...S.smpPack7([0, 1, 2, 3, 4, ~10 & 0xFF]), 0xF7]);
ok(S.parseReply(reply, 1)?.join() === "0,1,2,3,4" && S.parseReply(reply, 2) === null, "a reply parsed, and only for its request");
ok(S.crc32(new TextEncoder().encode("123456789")) === 0xCBF43926, "CRC-32 is zlib's");

// mu-law
let worst = 0;
for (let v = -1; v <= 1; v += 0.001) worst = Math.max(worst, Math.abs(S.ulawValue(S.ulawByte(v)) - v) / Math.max(Math.abs(v), 0.01));
ok(worst < 0.033, `mu-law round trip within 3.3% (${(worst * 100).toFixed(2)}%)`);

// pitch: a reed-like tone at several notes, a little sharp; none in noise
const rate = 22050;
const tone = (hz, n, h = 6) => Float32Array.from({ length: n }, (_, k) => {
  let s = 0;
  for (let j = 1; j <= h; j++) s += Math.sin(2 * Math.PI * hz * j * k / rate) / j;
  return 0.4 * s;
});
for (const note of [40.2, 57, 62.3, 69, 81.4, 93]) {
  const got = S.detectPitch(tone(440 * 2 ** ((note - 69) / 12), rate), rate);
  ok(got !== null && Math.abs(got - note) < 0.05, `pitch of a tone at ${note}: ${got?.toFixed(3)}`);
}
let seed = 1;
const noise = Float32Array.from({ length: rate }, () => ((seed = (seed * 1103515245 + 12345) & 0x7FFFFFFF) / 0x40000000) - 1);
ok(S.detectPitch(noise, rate) === null, "noise: no pitch");

// a slot from a tone: its root and tuning found, a loop whose seam steps as the wave does there (no click)
const [x, cut] = S.tidy(Float32Array.from([...new Float32Array(500), ...tone(293.66 * 2 ** (0.2 / 12), rate)]));
ok(cut >= 500 && cut < 510 && x.length < rate + 10 && Math.abs(Math.max(...x) - 0.89) < 0.01, "tidy: the leading silence gone, the peak at -1 dB");
const s = S.makeSlot(x, rate, { name: "reed tone.wav" });
ok(s.root === 62 && Math.abs(s.fine - 20) <= 3 && s.name === "REED TON", `slot: root ${s.root} ${s.fine}c, name ${s.name}`);
const [ls, le] = s.loop;
const seam = s.data[ls] - s.data[le - 1], own = s.data[le] - s.data[le - 1];
ok(le > ls + rate / 4 && le < s.data.length && Math.abs(seam - own) < 0.03 * 32767,
   `loop ${ls}..${le}: the seam steps ${seam}, the wave there ${own}`);
const one = S.parseBank(S.buildBank([null, s]))[1];
ok(one.root === 62 && one.loop.join() === s.loop.join() && one.data.length === s.data.length && one.data[100] === s.data[100],
   "the slot through a bank and back");
ok((() => { try { S.buildBank(Array(33).fill(null)); return false; } catch { return true; } })(), "33 slots refused");

// the link: a bank sent to a pretend FM-1 (it keeps what's written, as samples.c does) and read back;
// a refusal comes back as an error, and no answer as another
class FakeFM1 {
  constructor({ refuse = 0, mute = false } = {}) {
    this.flash = new Uint8Array(S.SMP.CAP).fill(0xFF);
    this.size = 0;
    this.msgs = 0;
    const listeners = [];
    const input = { name: "Windsynth FM-1", addEventListener: (t, f) => listeners.push(f), removeEventListener: () => {} };
    const reply = (cmd, body) => {
      let sum = 0;
      for (const x of body) sum += x;
      const p = Uint8Array.from([0xF0, 0x7D, 0x57, 0x53, 0x01, cmd | 0x40, ...S.smpPack7([...body, ~sum & 0xFF]), 0xF7]);
      setTimeout(() => listeners.forEach((f) => f({ data: p })), 1);
    };
    const output = { name: "Windsynth FM-1", send: (p) => {
      this.msgs++;
      if (mute) return;
      const cmd = p[5], d = S.smpUnpack7(p.subarray(6, p.length - 1)), b = d.subarray(0, d.length - 1);
      const dv = new DataView(b.buffer, b.byteOffset, b.byteLength), le = (v) => [v & 255, v >> 8 & 255, v >> 16 & 255, v >>> 24];
      if (refuse && cmd === refuse) return reply(cmd, [4]);
      if (cmd === 1) return reply(cmd, [0, ...le(S.SMP.CAP), ...le(this.size), 32, 0, 0, 1]);
      if (cmd === 3) { this.size = 0; this.want = dv.getUint32(0, true); return reply(cmd, [0]); }
      if (cmd === 4) { const off = dv.getUint32(0, true); this.flash.set(b.subarray(4), off); return reply(cmd, [0, ...le(off)]); }
      if (cmd === 5) { this.flash.set(b, 0); this.size = this.want; return reply(cmd, [0]); }
      if (cmd === 2) { const off = dv.getUint32(0, true), n = dv.getUint16(4, true); return reply(cmd, [0, ...le(off), ...this.flash.subarray(off, off + n)]); }
    } };
    this.access = { inputs: new Map([["i", input]]), outputs: new Map([["o", output]]) };
  }
}
{
  const dev = new FakeFM1();
  const link = new S.SampleLink(dev.access, 200);
  let last = 0;
  await link.send(py, (f) => { last = f; });
  const sent = dev.msgs;
  const back = await link.read();
  ok(last === 1 && sent === lines.length + 1 && back && back.length === py.length && back.every((v, i) => v === py[i]),
     `the link: sent in ${sent} messages, read back the same`);
  const e1 = await new S.SampleLink(new FakeFM1({ refuse: 5 }).access, 200).send(py).then(() => null, (e) => e);
  ok(e1 && /refused/.test(e1.message), "the link: a refusal is an error");
  const e2 = await new S.SampleLink(new FakeFM1({ mute: true }).access, 50).info().then(() => null, (e) => e);
  ok(e2 && /No answer/.test(e2.message), "the link: no answer is an error");
  ok((() => { try { new S.SampleLink({ inputs: new Map(), outputs: new Map() }); return false; } catch (e) { return /not found/.test(e.message); } })(),
     "the link: no FM-1 connected");
}

// AIFF and AIFF-C (browsers don't decode them): built here, read back by aiffDecode
{
  const be16 = (v) => [v >> 8 & 255, v & 255], be32 = (v) => [v >>> 24, v >> 16 & 255, v >> 8 & 255, v & 255];
  const ext = (r) => { const e = Math.floor(Math.log2(r)), m = r / 2 ** e * 2 ** 31; return [...be16(16383 + e), ...be32(m >>> 0), 0, 0, 0, 0]; };
  const chunk = (id, body) => [...new TextEncoder().encode(id), ...be32(body.length), ...body, ...(body.length & 1 ? [0] : [])];
  function aiff({ form = "AIFF", comp = null, bits = 16, ch = 2, rate = 44100, frames = 1000, inst = false }) {
    const w = Math.ceil(bits / 8), data = [];
    for (let k = 0; k < frames; k++)
      for (let c = 0; c < ch; c++) {
        const v = Math.round(Math.sin(2 * Math.PI * 440 * k / rate) * (c ? -0.5 : 0.5) * (2 ** (w * 8 - 1) - 1));
        const u = v < 0 ? v + 2 ** (w * 8) : v, bytes = [];
        for (let j = w - 1; j >= 0; j--) bytes.push(Math.floor(u / 2 ** (8 * j)) & 255);
        data.push(...(comp === "sowt" ? bytes.reverse() : bytes));
      }
    let comm = [...be16(ch), ...be32(frames), ...be16(bits), ...ext(rate)];
    if (comp) comm = [...comm, ...new TextEncoder().encode(comp), 0, 0];
    let body = [...new TextEncoder().encode(form), ...chunk("COMM", comm)];
    if (inst) {
      body = [...body, ...chunk("MARK", [0, 2, 0, 1, ...be32(100), 1, 0x41, 0, 2, ...be32(900), 2, 0x42, 0x43, 0])];
      body = [...body, ...chunk("INST", [62, 0xF6, 0, 127, 1, 127, 0, 0, 0, 1, 0, 1, 0, 2, 0, 0, 0, 0, 0, 0])];
    }
    body = [...body, ...chunk("SSND", [0, 0, 0, 0, 0, 0, 0, 0, ...data])];
    return Uint8Array.from([...new TextEncoder().encode("FORM"), ...be32(body.length), ...body]);
  }
  const near = (a, k, want, bits = 16) => Math.abs(a[k] - want) < 1e-3 + 2 / 2 ** (Math.ceil(bits / 8) * 8 - 1);
  const sine = (k, r) => 0.5 * Math.sin(2 * Math.PI * 440 * k / r);
  for (const [what, opt] of [["16-bit stereo", {}], ["24-bit mono", { bits: 24, ch: 1, rate: 48000 }],
                             ["8-bit", { bits: 8, ch: 1, rate: 22050 }], ["AIFF-C sowt", { form: "AIFC", comp: "sowt" }],
                             ["AIFF-C NONE", { form: "AIFC", comp: "NONE", bits: 32, ch: 1 }]]) {
    const r = S.aiffDecode(aiff(opt)), rate = opt.rate || 44100, ch = opt.ch || 2;
    ok(r && r.rate === rate && r.channels.length === ch && r.channels[0].length === 1000 &&
       near(r.channels[0], 37, sine(37, rate), opt.bits) && (ch < 2 || near(r.channels[1], 37, -sine(37, rate), opt.bits)) && near(r.channels[0], 999, sine(999, rate), opt.bits),
       `AIFF ${what}: read`);
  }
  const it = S.aiffDecode(aiff({ inst: true }));
  ok(it.root === 62 && it.fine === -10 && it.loop?.join() === "100,900", "AIFF: root, detune and the sustain loop from INST and MARK");
  ok(S.aiffDecode(new Uint8Array([82, 73, 70, 70, 0, 0, 0, 4, 87, 65, 86, 69])) === null, "a WAV isn't an AIFF");
  ok((() => { try { S.aiffDecode(aiff({ form: "AIFC", comp: "ima4" })); return false; } catch (e) { return /ima4/.test(e.message); } })(),
     "a compressed AIFF-C is refused, by name");
}

if (failed) { console.log(`${failed} failed`); process.exit(1); }
console.log("samples page ok");
