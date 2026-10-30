/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 Jeff Croft */
/* Loading the sample bank (wind_sample.h) from a computer, over USB-MIDI SysEx: the web page and
 * tools/fm1_samples.py send it. Part of the unity build, after app.h (and the host simulator includes it).
 *
 * A message is F0 7D 57 53 01 CMD DATA F7: 7D the non-commercial ID, "WS", the protocol's version 1,
 * then the command and its body with a check byte after it (the bitwise not of the body's sum), the two
 * 8-to-7 packed, least significant bit first. A reply is the same with CMD | 0x40, and starts with rc
 * (SMP_OK and the rest). One request at a time: send the next after the reply.
 *
 *   01 INFO               rc, u32 room, u32 the bank's size now (0 none), u16 slots, u16 the largest piece
 *   02 READ  off n        rc, u32 off, n bytes of the bank (n <= SMP_PIECE): to read it back
 *   03 BEGIN size         rc. The bank is gone from here (its first sector erased) until END
 *   04 WRITE off bytes    rc, u32 off. In order from WS_HDR (the header comes with END), each piece
 *                         where the last ended (one already written, sent again, is taken if it matches);
 *                         a piece reaching a new sector erases it first
 *   05 END   header       rc. All of it written: the header's CRC is checked against the flash, and the
 *                         header written last, so a bank cut off part-way reads as none
 *
 * The audio stops for each sector's erase, so loading while playing clicks. */
#define SMP_PIECE 256u
enum { SMP_INFO = 1, SMP_READ, SMP_BEGIN, SMP_WRITE, SMP_END };
enum { SMP_OK, SMP_EARG, SMP_ESTATE, SMP_EFLASH, SMP_ECHECK };

static struct {
    uint32_t size, next, erased;       /* the bank being loaded: its size, where the next piece goes, erased up to */
    uint8_t on;                        /* loading */
} smp;
static uint8_t smp_buf[8 + SMP_PIECE + 2];   /* a message's body, unpacked */
static uint8_t smp_wire[5 + 2 + (sizeof smp_buf * 8u + 6u) / 7u + 1u];

static uint32_t smp_rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static void smp_wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

/* the reply to cmd: body (n bytes, rc first), checked and packed */
static void smp_reply(uint32_t cmd, uint8_t *body, uint32_t n)
{
    uint32_t i, o = 6, acc = 0, nb = 0;
    uint8_t s = 0;
    for (i = 0; i < n; i++)
        s = (uint8_t)(s + body[i]);
    body[n++] = (uint8_t)~s;
    smp_wire[0] = 0xF0;
    smp_wire[1] = 0x7D;
    smp_wire[2] = 0x57;
    smp_wire[3] = 0x53;
    smp_wire[4] = 0x01;
    smp_wire[5] = (uint8_t)(cmd | 0x40u);
    for (i = 0; i < n; i++) {
        acc |= (uint32_t)body[i] << nb;
        nb += 8u;
        while (nb >= 7u) {
            smp_wire[o++] = (uint8_t)(acc & 0x7Fu);
            acc >>= 7;
            nb -= 7u;
        }
    }
    if (nb)
        smp_wire[o++] = (uint8_t)(acc & 0x7Fu);
    smp_wire[o++] = 0xF7;
    plat_sysex_out(smp_wire, o);
}

static void smp_rc(uint32_t cmd, uint32_t rc)
{
    smp_buf[0] = (uint8_t)rc;
    smp_reply(cmd, smp_buf, 1);
}

/* the bank's sectors from `erased` up to past end, erased */
static int smp_erase_to(uint32_t end)
{
    while (smp.erased < end) {
        if (plat_samples_erase(smp.erased))
            return -1;
        smp.erased += 0x1000u;
    }
    return 0;
}

static void smp_attach(void)           /* the bank in flash, to the player (none if it isn't a good one) */
{
    uint32_t cap;
    const uint8_t *b = plat_samples(&cap);
    ws_bank(b, cap);
}

/* one SysEx message's bytes between F0 and F7: 1 if it was one of ours (answered), 0 if not */
int smp_sysex(const uint8_t *in, uint32_t n)
{
    uint32_t i, d = 0, acc = 0, nb = 0, cap, cmd;
    const uint8_t *bank = plat_samples(&cap);
    uint8_t s = 0;
    if (n < 6u || in[0] != 0x7D || in[1] != 0x57 || in[2] != 0x53)
        return 0;
    cmd = in[4];
    if (in[3] != 0x01) {                           /* (another version of the protocol: say so) */
        smp_rc(cmd, SMP_EARG);
        return 1;
    }
    for (i = 5; i < n; i++) {                      /* unpack: a byte too many, or a 7-bit one that isn't, is bad */
        if (in[i] & 0x80u) {
            smp_rc(cmd, SMP_EARG);
            return 1;
        }
        acc |= (uint32_t)in[i] << nb;
        nb += 7u;
        if (nb >= 8u) {
            if (d >= sizeof smp_buf) {
                smp_rc(cmd, SMP_EARG);
                return 1;
            }
            smp_buf[d++] = (uint8_t)acc;
            acc >>= 8;
            nb -= 8u;
        }
    }
    for (i = 0; i + 1u < d; i++)
        s = (uint8_t)(s + smp_buf[i]);
    if (!d || smp_buf[d - 1u] != (uint8_t)~s) {
        smp_rc(cmd, SMP_EARG);
        return 1;
    }
    d--;                                           /* (the check byte) */
    switch (cmd) {
    case SMP_INFO: {
        int32_t have = ws_check(bank, cap);
        smp_buf[0] = SMP_OK;
        smp_wr32(smp_buf + 1, cap);
        smp_wr32(smp_buf + 5, have > 0 && !smp.on ? (uint32_t)have : 0u);
        smp_buf[9] = (uint8_t)WS_NSLOT;
        smp_buf[10] = 0;
        smp_buf[11] = (uint8_t)SMP_PIECE;
        smp_buf[12] = (uint8_t)(SMP_PIECE >> 8);
        smp_reply(cmd, smp_buf, 13);
        return 1;
    }
    case SMP_READ: {
        uint32_t off, len;
        if (d != 6u) {
            smp_rc(cmd, SMP_EARG);
            return 1;
        }
        off = smp_rd32(smp_buf);
        len = (uint32_t)smp_buf[4] | (uint32_t)smp_buf[5] << 8;
        if (!bank || len > SMP_PIECE || off > cap || len > cap - off) {
            smp_rc(cmd, SMP_EARG);
            return 1;
        }
        smp_buf[0] = SMP_OK;
        smp_wr32(smp_buf + 1, off);
        for (i = 0; i < len; i++)
            smp_buf[5 + i] = bank[off + i];
        smp_reply(cmd, smp_buf, 5 + len);
        return 1;
    }
    case SMP_BEGIN: {
        uint32_t size;
        if (d != 4u || (size = smp_rd32(smp_buf)) < WS_DATA || size > cap) {
            smp_rc(cmd, SMP_EARG);
            return 1;
        }
        if (!bank) {
            smp_rc(cmd, SMP_EFLASH);
            return 1;
        }
        ws_bank(0, 0);                             /* (nothing plays from it while it's rewritten) */
        smp.on = 1;
        smp.size = size;
        smp.next = WS_HDR;
        smp.erased = 0;
        if (smp_erase_to(0x1000u)) {
            smp.on = 0;
            smp_rc(cmd, SMP_EFLASH);
            return 1;
        }
        ui_say("LOADING", "SAMPLES");
        smp_rc(cmd, SMP_OK);
        return 1;
    }
    case SMP_WRITE: {
        uint32_t off, len = d - 4u, rc = SMP_OK;
        if (d < 5u || d > 4u + SMP_PIECE) {
            smp_rc(cmd, SMP_EARG);
            return 1;
        }
        off = smp_rd32(smp_buf);
        if (!smp.on || off < WS_HDR || off > smp.size || len > smp.size - off || off > smp.next)
            rc = SMP_ESTATE;
        else if (off + len <= smp.next) {          /* sent again (its reply lost): taken if it's what's there */
            for (i = 0; i < len && rc == SMP_OK; i++)
                if (bank[off + i] != smp_buf[4 + i])
                    rc = SMP_ESTATE;
        } else if (off != smp.next)                /* (overlapping what's written: not a piece we sent) */
            rc = SMP_ESTATE;
        else if (smp_erase_to(off + len) || plat_samples_write(off, smp_buf + 4, len))
            rc = SMP_EFLASH;
        else
            smp.next = off + len;
        smp_buf[0] = (uint8_t)rc;
        smp_wr32(smp_buf + 1, off);
        smp_reply(cmd, smp_buf, 5);
        return 1;
    }
    case SMP_END: {
        uint32_t rc = SMP_OK;
        const uint8_t *h = smp_buf;
        if (d != WS_HDR)
            rc = SMP_EARG;
        else if (!smp.on || smp.next != smp.size || smp_rd32(h + 8) != smp.size)
            rc = SMP_ESTATE;
        else if (smp_rd32(h) != WS_MAGIC || ws_crc32(bank + WS_HDR, smp.size - WS_HDR, 0) != smp_rd32(h + 12))
            rc = SMP_ECHECK;                       /* (what's in the flash isn't what was meant) */
        else if (plat_samples_write(0, h, WS_HDR))
            rc = SMP_EFLASH;
        else if (ws_check(bank, cap) != (int32_t)smp.size)
            rc = SMP_ECHECK;
        smp.on = 0;
        smp_attach();
        ui_say("SAMPLES", rc == SMP_OK ? "LOADED" : "NOT LOADED");
        smp_rc(cmd, rc);
        return 1;
    }
    default:
        smp_rc(cmd, SMP_EARG);
        return 1;
    }
}
