/* SPDX-License-Identifier: MPL-2.0 */
/* Copyright (C) 2026 Jeff Croft. This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file, You can obtain one at
 * https://mozilla.org/MPL/2.0/. */
/* The sample layer: one sample at a time from a bank of WS_NSLOT, played at the note's pitch (or its
 * own), from Start, looped between its loop points (or ping-ponged, or once). The voice mixes it in with
 * the noise layer, before our filter.
 *
 * The bank is read in place: on the FM-1 straight from flash, through the XIP window, so a sample
 * takes no RAM. It's built by the web page (or tools/fm1_samples.py); little-endian:
 *
 *   0     header, WS_HDR bytes: magic "WSMP", u16 version 1, u16 slots (WS_NSLOT), u32 size (the bank's
 *         bytes, header included), u32 CRC-32 (zlib's) of the bytes from WS_HDR to size; the rest 0
 *   64    WS_NSLOT entries of WS_ENT bytes: u32 offset (from the bank's start), u32 length (frames),
 *         u32 loop start, u32 loop end (frames; end 0: no loop points, the whole sample loops), u16
 *         rate (Hz), u8 format (WS_FMT_*: 0 an empty slot), u8 root (MIDI note), i8 fine tune (cents),
 *         3 bytes 0, 8 bytes of name (ASCII, 0-padded)
 *   1088  the samples: 16-bit signed, or 8-bit mu-law (G.711), mono
 *
 * Threads: ws_bank and the settings come from the UI (or the main loop), between audio blocks;
 * ws_render runs in the audio interrupt. */
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WS_MAGIC 0x504D5357u           /* "WSMP" */
#define WS_VERSION 1u
#define WS_NSLOT 32
#define WS_HDR 64u
#define WS_ENT 32u
#define WS_DATA (WS_HDR + WS_NSLOT * WS_ENT)   /* where the samples start: 1088 */
#define WS_NAME 8
enum { WS_FMT_NONE, WS_FMT_S16, WS_FMT_ULAW, WS_NFMT };
enum { WS_LOOP_OFF, WS_LOOP_ON, WS_LOOP_PINGPONG, WS_NLOOP };
enum { WS_TRIG_PHRASE, WS_TRIG_NOTE, WS_NTRIG };
/* ws_render's trig: a phrase starts (a note from silence), or a new note within one */
#define WS_START_PHRASE 1u
#define WS_START_NOTE 2u

typedef struct {
    uint32_t len, loop_start, loop_end;   /* frames (loop_end 0: none set) */
    uint32_t rate;                        /* Hz */
    uint8_t format, root;                 /* WS_FMT_*, MIDI note */
    int8_t fine;                          /* cents */
    char name[WS_NAME + 1];
} ws_info_t;

void ws_init(void);
/* the bank at p, cap bytes of room: its size if it's a good one, else -1 (and no bank). 0 / 0: none
 * (before its flash is rewritten). The audio interrupt reads it from the next block on. */
int32_t ws_bank(const uint8_t *p, uint32_t cap);
int32_t ws_check(const uint8_t *p, uint32_t cap);   /* the same check, changing nothing */
uint32_t ws_crc32(const uint8_t *p, uint32_t n, uint32_t crc);   /* zlib's CRC-32 (start at 0) */
int ws_slot_info(int slot, ws_info_t *out);   /* 1..WS_NSLOT; 1: it holds a sample */
int ws_used(void);                     /* the slots holding a sample */
uint32_t ws_gen(void);                 /* + 1 at each ws_bank: what was read of the bank before is stale */
/* the slot's sample in n columns: each one's lowest and highest value, -1..1 (the views'). 0: no sample */
int ws_peaks(int slot, int n, float *lo, float *hi);

/* the Sample pages: Slot (0 off, 1..WS_NSLOT), Tune (semitones), Start (0..1 of the sample), Loop
 * (WS_LOOP_*), Trigger (WS_TRIG_*), Track (1: the note's pitch, 0: the sample's own) */
void ws_settings(int slot, float tune, float start, int loop, int trig, int track);

/* n frames of the sample at `note` (MIDI semitones, bend and glide in it) into out; trig: WS_START_* (a
 * start on this block). 0: silent (no slot, no sample there, or a one-shot that has ended: out untouched) */
int ws_render(float note, uint32_t trig, float *out, uint32_t n);
float ws_where(void);                  /* where it plays now, 0..1 of the sample; -1 not playing */

#ifdef __cplusplus
}
#endif
