/*
 * Stand-in for TINCLIBC (tinclib-config) in the handoff test: answers every
 * handoff with "cancelled" and exits, without any UI. Exiting is what hands
 * control back to the app. The TINCHND layout is defined in src/tinc_config.c.
 */

#include <fileioc.h>
#include <stdint.h>
#include <string.h>

#include "../verdict.h"

#define RESULT_CANCELLED 2

int main(void) {
    uint8_t hdr[18], tail[6] = { RESULT_CANCELLED };
    uint8_t h = ti_Open("TINCHND", "r+");

    CHECK(h != 0);
    if (h) {
        CHECK(ti_Read(hdr, sizeof hdr, 1, h) == 1);
        memcpy(tail + 1, hdr + 2, 4);                  /* echo the nonce */
        CHECK(ti_Seek(sizeof hdr + hdr[17], SEEK_SET, h) != EOF);
        CHECK(ti_Write(tail, sizeof tail, 1, h) == 1);
        ti_Close(h);
    }
    if (hw_failed_line)
        verdict();
    return 0;
}
