/* Link / Wi-Fi status. */

#include <time.h>

#include "tinc_internal.h"

/* Polls STATUS every so often while Wi-Fi is still connecting. */
#define WIFI_POLL_MS 250u

bool tinc_isActive(tinc_mode_t mode)
{
    clock_t start = clock();

    if (!tinc_g.ready || mode != TINC_WIFI)
        return false;
    /* A body chunk is streaming, so Wi-Fi is up; a STATUS reply would also
     * overwrite that chunk (see tinc_internal.h). */
    if (tinc_g.chunk_pos < tinc_g.chunk_len)
        return true;

    for (;;) {
        if (tinc_xfer(TINC_T_STATUS, NULL, 0, 0) != TINC_OK || tinc_g.parser.len < 1)
            return false;
        switch (tinc_g.parser.payload[TINC_STATUS_WIFI_STATE]) {
        case TINC_WIFI_CONNECTED:
            return true;
        case TINC_WIFI_CONNECTING:
            /* Bounded wait: right after power-up the board is still joining,
             * and a false here would send the app into a needless handoff. */
            if (tinc_elapsed(start, TINC_WIFI_WAIT_MS))
                return false;
            tinc_sleep(WIFI_POLL_MS);
            break;
        default:
            return false;
        }
    }
}
