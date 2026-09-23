/*
 * Shared by the hardware tests: CHECK() notes the first failure, verdict()
 * paints the whole screen green (all passed) or red with the failing line,
 * then waits for [clear]. One color means one CRC for autotest.json.
 */
#ifndef TINC_HW_VERDICT_H
#define TINC_HW_VERDICT_H

#include <graphx.h>
#include <ti/getcsc.h>

#define HW_PASS 0x03 /* gfx_green */
#define HW_FAIL 0xE0 /* gfx_red */

static unsigned hw_failed_line;

#define CHECK(cond)                                  \
    do {                                             \
        if (!(cond) && !hw_failed_line)              \
            hw_failed_line = __LINE__;               \
    } while (0)

static void verdict(void) {
    gfx_Begin();
    gfx_FillScreen(hw_failed_line ? HW_FAIL : HW_PASS);
    if (hw_failed_line) {
        gfx_SetTextFGColor(0x00);
        gfx_SetTextBGColor(HW_FAIL);
        gfx_PrintStringXY("FAILED at line", 8, 8);
        gfx_SetTextXY(8, 20);
        gfx_PrintUInt(hw_failed_line, 1);
    }
    while (os_GetCSC() != sk_Clear) {
    }
    gfx_End();
}

#endif
