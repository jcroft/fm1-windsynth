/* SPDX-License-Identifier: GPL-3.0-only */
/* The flash parts the boot accepts (fm1_flash_ok.h): both FM-1 parts, and nothing else. */
#include <stdint.h>
#include <stdio.h>
#include "fm1_flash_ok.h"

static int fails;
static void check(uint32_t id, int want)
{
    int got = FM1_FLASH_JEDEC_OK(id) ? 1 : 0;
    printf("  %06X %s%s\n", (unsigned)id, got ? "accepted" : "RAM only", got == want ? "" : "  FAIL");
    fails += got != want;
}

int main(void)
{
    check(0x856014u, 1);                         /* Puya P25Q80H, the first units */
    check(0xB36014u, 1);                         /* UC25HQ80, the newer units */
    check(0x000000u, 0);                         /* the read failed */
    check(0xFFFFFFu, 0);                         /* nothing answered */
    check(0x856015u, 0);                         /* a 2 MiB Puya: not the FM-1's map */
    check(0xB36013u, 0);
    check(0xEF4014u, 0);                         /* another 1 MiB part */
    if (fails)
        printf("FAIL: %d\n", fails);
    return fails != 0;
}
