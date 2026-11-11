/* SPDX-License-Identifier: GPL-3.0-only */
/* Which flash parts the FM-1's storage and updates may use (main_fm1.c's boot sets flash_ok).
 *
 * Early FM-1s carry a Puya P25Q80H (JEDEC 85 60 14, 1 MiB). Units sold since autumn 2026 (mostly
 * AliExpress) carry a UC25HQ80 (JEDEC B3 60 14, 1 MiB, the same plain SPI NOR commands) instead.
 * A firmware that knows only the Puya takes those units for unknown: saves stay in RAM and the
 * update path is refused. The same check as Felucca 1.8.2, X0X 1.2.1 and fm1-base (#35).
 * Any other answer, including a failed read (0): RAM only, nothing written. */
#ifndef FM1_FLASH_OK_H
#define FM1_FLASH_OK_H

#define FM1_FLASH_JEDEC_PUYA 0x856014u           /* Puya P25Q80H, the first units */
#define FM1_FLASH_JEDEC_UC   0xB36014u           /* UC25HQ80, the newer units */
#define FM1_FLASH_JEDEC_OK(id) ((id) == FM1_FLASH_JEDEC_PUYA || (id) == FM1_FLASH_JEDEC_UC)

#endif
