// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Jeff Croft
//
// Sample banks for the Windsynth FM-1's sample layer (firmware/src/wind/wind_sample.h), in the browser:
// built from sounds, read back, and loaded on the FM-1 over Web MIDI SysEx (firmware/src/app/samples.c).
// The same banks and messages as tools/fm1_samples.py (tests/web/samples_test.mjs checks they match).

export const SMP = { MAGIC: 0x504D5357, VERSION: 1, NSLOT: 32, HDR: 64, ENT: 32, NAME: 8, CAP: 0x3D000, PIECE: 256 };
SMP.DATA = SMP.HDR + SMP.NSLOT * SMP.ENT;
export const FMT_S16 = 1, FMT_ULAW = 2;
const INFO = 1, READ = 2, BEGIN = 3, WRITE = 4, END = 5;
const RC = ["OK", "a bad message", "out of order", "a flash error", "a check that failed"];

let crcT = null;
export function crc32(b) {                     // zlib's
  if (!crcT) {
    crcT = new Uint32Array(256);
    for (let n = 0; n < 256; n++) {
      let c = n;
      for (let k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320 ^ (c >>> 1) : c >>> 1;
      crcT[n] = c >>> 0;
    }
  }
  let c = 0xFFFFFFFF;
  for (let i = 0; i < b.length; i++) c = crcT[(c ^ b[i]) & 0xFF] ^ (c >>> 8);
  return (c ^ 0xFFFFFFFF) >>> 0;
}

export function ulawByte(v) {                  // G.711 mu-law of -1..1
  let s = Math.max(-32635, Math.min(32635, Math.round(v * 32768)));
  const sign = s < 0 ? 0x80 : 0;
  s = Math.abs(s) + 0x84;
  let e = 7;
  while (e > 0 && !(s & (0x4000 >> (7 - e)))) e--;
  return ~(sign | e << 4 | ((s >> (e + 3)) & 15)) & 0xFF;
}
export function ulawValue(u) {
  u = ~u & 0xFF;
  const e = (u >> 4) & 7, v = (((u & 15) << 3) + 0x84 << e) - 0x84;
  return (u & 0x80 ? -v : v) / 32768;
}

// ------------------------------------------------------------- a sound into a slot ---

// the root (MIDI note, fraction and all) of a pitched sound, from its middle (YIN); null if it has none
export function detectPitch(x, rate) {
  const n = Math.min(x.length, Math.round(rate * 0.25)), at = Math.max(0, Math.floor((x.length - n) / 2));
  const lo = Math.floor(rate / 2000), hi = Math.min(Math.floor(rate / 40), n >> 1);
  if (hi <= lo + 2) return null;
  const w = n - hi, d = new Float32Array(hi + 1);
  for (let tau = 1; tau <= hi; tau++) {
    let s = 0;
    for (let i = 0; i < w; i++) { const v = x[at + i] - x[at + i + tau]; s += v * v; }
    d[tau] = s;
  }
  let run = 0;
  const cm = new Float32Array(hi + 1);
  cm[0] = 1;
  for (let tau = 1; tau <= hi; tau++) { run += d[tau]; cm[tau] = run ? d[tau] * tau / run : 1; }
  let tau = -1;
  for (let t = Math.max(lo, 2); t < hi; t++)
    if (cm[t] < 0.15) { while (t + 1 < hi && cm[t + 1] < cm[t]) t++; tau = t; break; }
  if (tau < 0) return null;
  const a = cm[tau - 1], b = cm[tau], c = cm[tau + 1], den = a - 2 * b + c;
  const t = den > 0 ? tau + 0.5 * (a - c) / den : tau;
  return 69 + 12 * Math.log2(rate / t / 440);
}

// a loop for a sustained sound: about its last half, its ends where the wave matches best (one period
// apart at most from where they'd be), so it plays on without a click. [start, end) in frames
export function findLoop(x, rate, note) {
  const len = x.length;
  if (len < 64) return [0, 0];
  const period = note == null ? 256 : rate / (440 * Math.pow(2, (note - 69) / 12));
  const win = Math.max(16, Math.min(256, Math.round(period)));
  const match = (a, b) => {
    let s = 0;
    for (let i = -win; i < win; i++) { const v = (x[a + i] || 0) - (x[b + i] || 0); s += v * v; }
    return s;
  };
  let le = Math.min(len - win, Math.round(len * 0.92));
  while (le > win && !(x[le - 1] < 0 && x[le] >= 0)) le--;        // a rising zero crossing
  const want = Math.max(win * 2, Math.round(le - len * 0.45));
  let best = want, bestS = Infinity;
  const r = Math.ceil(period);
  for (let s = Math.max(win, want - r); s <= Math.min(le - 2 * win, want + r); s++) {
    const v = match(s, le);
    if (v < bestS) { bestS = v; best = s; }
  }
  return best + 2 <= le ? [best, le] : [0, 0];
}

// mono samples (any rate already resampled to `rate`): silence trimmed off both ends, the peak at -1 dB.
// [the samples, how many frames came off the start]
export function tidy(x) {
  let peak = 0;
  for (const v of x) peak = Math.max(peak, Math.abs(v));
  if (!peak) return [x, 0];
  const th = peak * 0.001;                     // -60 dB
  let a = 0, b = x.length;
  while (a < b && Math.abs(x[a]) < th) a++;
  while (b > a && Math.abs(x[b - 1]) < th) b--;
  const g = 0.89 / peak, out = new Float32Array(b - a);
  for (let i = a; i < b; i++) out[i - a] = x[i] * g;
  return [out, a];
}

// a WAV's own root, tuning and loop (its smpl chunk), and rate; {} if it has none (or isn't a WAV)
export function wavInfo(b) {
  const out = {};
  if (b.length < 12 || String.fromCharCode(...b.subarray(0, 4)) !== "RIFF") return out;
  const dv = new DataView(b.buffer, b.byteOffset, b.byteLength);
  for (let i = 12; i + 8 <= b.length;) {
    const id = String.fromCharCode(...b.subarray(i, i + 4)), n = dv.getUint32(i + 4, true);
    if (id === "fmt " && n >= 8) out.rate = dv.getUint32(i + 12, true);
    if (id === "smpl" && n >= 36) {
      const unity = dv.getUint32(i + 8 + 12, true), frac = dv.getUint32(i + 8 + 16, true), loops = dv.getUint32(i + 8 + 28, true);
      if (unity < 128) { out.root = unity; out.fine = Math.round(frac / 4294967296 * 100); }
      if (loops && n >= 60) {
        const s = dv.getUint32(i + 8 + 44, true), e = dv.getUint32(i + 8 + 48, true);
        if (s < e) out.loop = [s, e + 1];
      }
    }
    i += 8 + n + (n & 1);
  }
  return out;
}

// a slot from mono samples at `rate`: { name, rate, fmt, root, fine, loop: [start, end) or [0, 0], data }
export function makeSlot(x, rate, { name = "", fmt = FMT_S16, root, fine, loop } = {}) {
  const note = root == null ? detectPitch(x, rate) : root + (fine || 0) / 100;   // (null: unpitched)
  if (root == null) {
    root = note == null ? 60 : Math.round(note);
    fine = note == null ? 0 : Math.round((note - root) * 100);
  }
  if (!loop) loop = findLoop(x, rate, note);
  const data = fmt === FMT_ULAW ? Uint8Array.from(x, ulawByte)
                                : Int16Array.from(x, (v) => Math.max(-32768, Math.min(32767, Math.round(v * 32767))));
  return { name: cleanName(name), rate, fmt, root: Math.max(0, Math.min(127, root)), fine: fine || 0, loop, data };
}
export function cleanName(s) { return String(s).replace(/\.[^.]*$/, "").replace(/[^ -~]/g, "").toUpperCase().slice(0, SMP.NAME).trimEnd(); }
export function slotBytes(s) { return s ? s.data.length * (s.fmt === FMT_S16 ? 2 : 1) : 0; }

// --------------------------------------------------------------------------- banks ---

// the bank of slots (SMP.NSLOT at most, null an empty one)
export function buildBank(slots) {
  let size = SMP.DATA;
  for (const s of slots) if (s) size += (size & 1) + slotBytes(s);
  if (slots.length > SMP.NSLOT) throw new Error(`${slots.length} slots: a bank holds ${SMP.NSLOT}`);
  const b = new Uint8Array(size), dv = new DataView(b.buffer);
  let off = SMP.DATA;
  slots.forEach((s, i) => {
    if (!s) return;
    off += off & 1;
    const e = SMP.HDR + i * SMP.ENT, n = s.data.length;
    if (n < 4) throw new Error(`slot ${i + 1}: too short`);
    const [ls, le] = s.loop || [0, 0];
    if (le && (le > n || ls + 2 > le)) throw new Error(`slot ${i + 1}: its loop isn't inside it`);
    dv.setUint32(e, off, true);
    dv.setUint32(e + 4, n, true);
    dv.setUint32(e + 8, le ? ls : 0, true);
    dv.setUint32(e + 12, le, true);
    dv.setUint16(e + 16, s.rate, true);
    b[e + 18] = s.fmt;
    b[e + 19] = s.root;
    dv.setInt8(e + 20, s.fine);
    for (let k = 0; k < Math.min(SMP.NAME, s.name.length); k++) b[e + 24 + k] = s.name.charCodeAt(k);
    if (s.fmt === FMT_S16) for (let k = 0; k < n; k++) dv.setInt16(off + 2 * k, s.data[k], true);
    else b.set(s.data, off);
    off += slotBytes(s);
  });
  dv.setUint32(0, SMP.MAGIC, true);
  dv.setUint16(4, SMP.VERSION, true);
  dv.setUint16(6, SMP.NSLOT, true);
  dv.setUint32(8, size, true);
  dv.setUint32(12, crc32(b.subarray(SMP.HDR)), true);
  return b;
}

// a bank's slots (null the empty ones); throws if it isn't a good bank
export function parseBank(b) {
  if (b.length < SMP.DATA) throw new Error("not a sample bank (too short)");
  const dv = new DataView(b.buffer, b.byteOffset, b.byteLength), size = dv.getUint32(8, true);
  if (dv.getUint32(0, true) !== SMP.MAGIC || dv.getUint16(4, true) !== SMP.VERSION || dv.getUint16(6, true) !== SMP.NSLOT ||
      size < SMP.DATA || size > b.length) throw new Error("not a sample bank");
  if (crc32(b.subarray(SMP.HDR, size)) !== dv.getUint32(12, true)) throw new Error("the sample bank is damaged (its CRC)");
  const out = [];
  for (let i = 0; i < SMP.NSLOT; i++) {
    const e = SMP.HDR + i * SMP.ENT, fmt = b[e + 18];
    if (!fmt) { out.push(null); continue; }
    const off = dv.getUint32(e, true), n = dv.getUint32(e + 4, true);
    if (fmt > FMT_ULAW || off < SMP.DATA || off + n * (fmt === FMT_S16 ? 2 : 1) > size) throw new Error(`slot ${i + 1} is damaged`);
    const data = fmt === FMT_S16 ? Int16Array.from({ length: n }, (_, k) => dv.getInt16(off + 2 * k, true)) : b.slice(off, off + n);
    let name = "";
    for (let k = 0; k < SMP.NAME && b[e + 24 + k]; k++) name += String.fromCharCode(b[e + 24 + k]);
    out.push({ name: name.trimEnd(), rate: dv.getUint16(e + 16, true), fmt, root: b[e + 19], fine: dv.getInt8(e + 20),
               loop: [dv.getUint32(e + 8, true), dv.getUint32(e + 12, true)], data });
  }
  return out;
}

// --------------------------------------------------------------------------- SysEx ---

export function smpPack7(b) {   // (the update's pack7, named apart: the page has both)
  const out = [];
  let acc = 0, nb = 0;
  for (const x of b) {
    acc |= x << nb;
    nb += 8;
    while (nb >= 7) { out.push(acc & 0x7F); acc >>= 7; nb -= 7; }
  }
  if (nb) out.push(acc & 0x7F);
  return out;
}
export function smpUnpack7(s) {
  const out = [];
  let acc = 0, nb = 0;
  for (const x of s) {
    acc |= x << nb;
    nb += 7;
    if (nb >= 8) { out.push(acc & 0xFF); acc >>= 8; nb -= 8; }
  }
  return Uint8Array.from(out);
}
export function message(cmd, body = []) {
  let s = 0;
  for (const x of body) s += x;
  return Uint8Array.from([0xF0, 0x7D, 0x57, 0x53, 0x01, cmd, ...smpPack7([...body, ~s & 0xFF]), 0xF7]);
}
export function parseReply(p, cmd) {             // the reply's body (rc first) if p answers cmd, else null
  if (p.length < 8 || p[0] !== 0xF0 || p[1] !== 0x7D || p[2] !== 0x57 || p[3] !== 0x53 || p[4] !== 1 ||
      p[5] !== (cmd | 0x40) || p[p.length - 1] !== 0xF7) return null;
  const d = smpUnpack7(p.subarray(6, p.length - 1));
  let s = 0;
  for (let i = 0; i + 1 < d.length; i++) s += d[i];
  return d.length && d[d.length - 1] === (~s & 0xFF) ? d.subarray(0, d.length - 1) : null;
}
const u32 = (v) => [v & 0xFF, (v >>> 8) & 0xFF, (v >>> 16) & 0xFF, (v >>> 24) & 0xFF];

// the messages that load a bank: BEGIN, the WRITEs, END
export function frames(bank) {
  const out = [message(BEGIN, u32(bank.length))];
  for (let off = SMP.HDR; off < bank.length; off += SMP.PIECE)
    out.push(message(WRITE, [...u32(off), ...bank.subarray(off, off + SMP.PIECE)]));
  out.push(message(END, bank.subarray(0, SMP.HDR)));
  return out;
}

// the FM-1 over Web MIDI (an access with sysex), one request and its reply at a time
export class SampleLink {
  constructor(access, timeout = 2000) {
    const pick = (m) => [...m.values()].find((p) => /fm-1|windsynth/i.test(p.name || ""));
    this.inp = pick(access.inputs);
    this.out = pick(access.outputs);
    if (!this.inp || !this.out) throw new Error("FM-1 not found. Check the USB cable, and close other apps that use MIDI.");
    this.timeout = timeout;
    this.got = [];
    this.wake = null;
    this.on = (e) => { if (e.data && e.data[0] === 0xF0) { this.got.push(e.data); if (this.wake) this.wake(); } };
    this.inp.addEventListener("midimessage", this.on);
  }
  close() { this.inp.removeEventListener("midimessage", this.on); }
  async ask(pkt, tries = 3) {
    const cmd = pkt[5];
    for (let t = 0; t < tries; t++) {
      this.got = [];
      this.out.send(pkt);
      const end = Date.now() + this.timeout;
      while (Date.now() < end) {
        const p = this.got.shift();
        if (!p) { await new Promise((r) => { this.wake = r; setTimeout(r, end - Date.now()); }); this.wake = null; continue; }
        const r = parseReply(p, cmd);
        if (!r) continue;
        if (r[0]) throw new Error(`The FM-1 refused the samples: ${RC[r[0]] || r[0]}.`);
        return r.subarray(1);
      }
    }
    throw new Error("No answer from the FM-1. Is it running Windsynth with the sample layer (this version or newer)?");
  }
  async info() {                                  // { room, size (0: no bank) }
    const r = await this.ask(message(INFO));
    const dv = new DataView(r.buffer, r.byteOffset, r.byteLength);
    return { room: dv.getUint32(0, true), size: dv.getUint32(4, true) };
  }
  async send(bank, progress = () => {}) {
    const { room } = await this.info();
    if (bank.length > room) throw new Error(`The bank is ${bank.length} bytes: the FM-1 has room for ${room}.`);
    const ms = frames(bank);
    for (let k = 0; k < ms.length; k++) { await this.ask(ms[k]); progress((k + 1) / ms.length); }
  }
  async read(progress = () => {}) {               // the FM-1's bank, or null if it has none
    const { size } = await this.info();
    if (!size) return null;
    const b = new Uint8Array(size);
    for (let off = 0; off < size; off += SMP.PIECE) {
      const n = Math.min(SMP.PIECE, size - off);
      const r = await this.ask(message(READ, [...u32(off), n & 0xFF, n >> 8]));
      b.set(r.subarray(4, 4 + n), off);
      progress((off + n) / size);
    }
    return b;
  }
}
