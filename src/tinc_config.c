/* Handoff to TINCLIBC (the config app) through the TINCHND appvar.
 *
 * TINCLIBC is started with os_RunPrgm() and a return callback, and simply
 * exits when done; the callback then runs the app's main() again from the
 * top. TINCLIBC must NOT relaunch the app itself: chaining app -> TINCLIBC
 * -> app with os_RunPrgm() and no callback crashes the calculator (RAM
 * reset), even with no tinclib code involved (tests/hw/handoff, OS 5.3).
 * return_to is still filled in, for TINCLIBC to show who asked.
 *
 * This file defines the TINCHND layout; tinclib-config must match it byte
 * for byte (a mismatch fails silently). All multi-byte fields little-endian.
 *
 *   0   magic        u8   'H'
 *   1   version      u8   1
 *   2   nonce        u32  chosen by tinclib
 *   6   return_to    9B   program to relaunch, NUL-padded
 *   15  action       u8   TINC_HND_SETUP_WIFI | TINC_HND_TEST_CONN
 *   16  requirements u8   TINC_HND_NEEDS_WIFI | TINC_HND_NEEDS_TIME
 *   17  hint_len     u8   <= TINC_HND_HINT_MAX
 *   18  hint         hint_len bytes, no NUL
 *   --- written by TINCLIBC, right after the hint ---
 *   +0  result       u8   TINC_HND_NONE (as tinclib writes it) | OK | CANCELLED | FAILED
 *   +1  nonce echo   u32  copy of the nonce above
 *   +5  detail_len   u8
 *   +6  detail       detail_len bytes (e.g. an error code)
 */

#include <stdio.h>
#include <string.h>
#include <time.h>

#include <fileioc.h>
#include <ti/vars.h>

#include "tinc_internal.h"

#define TINC_HND_NAME       "TINCHND"
#define TINC_HND_MAGIC      'H'
#define TINC_HND_VERSION    1u
#define TINC_HND_RETURN_TO  6
#define TINC_HND_HINT_LEN   17
#define TINC_HND_HINT       18
#define TINC_HND_HINT_MAX   63u

#define TINC_HND_SETUP_WIFI 1u
#define TINC_HND_NEEDS_WIFI 0x01u

enum { TINC_HND_NONE, TINC_HND_OK, TINC_HND_CANCELLED, TINC_HND_FAILED };

#define CONFIG_APP "TINCLIBC"

/* The app's own entry point: what runs again once TINCLIBC exits. */
extern int main(void);

static int back_from_config(void *data, int retval)
{
    (void)data;
    (void)retval;
    return main();
}

tinc_err_t tinc_openConfig(const char *hint)
{
    uint8_t hdr[TINC_HND_HINT], tail[6] = { TINC_HND_NONE };
    size_t n = hint ? strlen(hint) : 0;
    const char *app = tinc_g.cfg.appName;
    uint8_t h;

    if (!app || !*app || strlen(app) > 8)
        return TINC_ERR_BAD_ARG;
    if (n > TINC_HND_HINT_MAX)
        n = TINC_HND_HINT_MAX;

    memset(hdr, 0, sizeof hdr);
    hdr[0] = TINC_HND_MAGIC;
    hdr[1] = TINC_HND_VERSION;
    tinc_put_u32(hdr + 2, (uint32_t)clock());
    memcpy(hdr + TINC_HND_RETURN_TO, app, strlen(app));
    hdr[15] = TINC_HND_SETUP_WIFI;
    hdr[16] = TINC_HND_NEEDS_WIFI;
    hdr[TINC_HND_HINT_LEN] = (uint8_t)n;

    h = ti_Open(TINC_HND_NAME, "w");
    if (!h)
        return TINC_ERR_NO_MEM;
    if (ti_Write(hdr, sizeof hdr, 1, h) != 1 || (n && ti_Write(hint, n, 1, h) != 1) ||
        ti_Write(tail, sizeof tail, 1, h) != 1) {
        ti_Close(h);
        ti_Delete(TINC_HND_NAME);
        return TINC_ERR_NO_MEM;
    }
    ti_Close(h);

    /* The link must be closed before this program goes away. */
    tinc_shutdown();
    os_RunPrgm(CONFIG_APP, NULL, 0, back_from_config);

    /* Only reached when TINCLIBC couldn't be started. */
    ti_Delete(TINC_HND_NAME);
    return TINC_ERR_NO_CONFIG_APP;
}

tinc_err_t tinc_takeSetupResult(void)
{
    uint8_t hdr[TINC_HND_HINT], tail[5];
    const char *app = tinc_g.cfg.appName;
    tinc_err_t err = TINC_OK;
    uint8_t h;

    if (!app || !*app)
        return TINC_OK;
    memset(hdr, 0, sizeof hdr);
    h = ti_Open(TINC_HND_NAME, "r");
    if (!h)
        return TINC_OK;

    if (ti_Read(hdr, sizeof hdr, 1, h) == 1 && hdr[0] == TINC_HND_MAGIC &&
        hdr[1] == TINC_HND_VERSION &&
        strncmp((const char *)hdr + TINC_HND_RETURN_TO, app, 9) == 0 &&
        ti_Seek(hdr[TINC_HND_HINT_LEN], SEEK_CUR, h) != EOF &&
        ti_Read(tail, sizeof tail, 1, h) == 1 &&
        tinc_get_u32(tail + 1) == tinc_get_u32(hdr + 2)) {
        if (tail[0] == TINC_HND_CANCELLED)
            err = TINC_ERR_SETUP_CANCELLED;
        else if (tail[0] == TINC_HND_FAILED)
            err = TINC_ERR_SETUP_FAILED;
    }
    ti_Close(h);

    /* Only the program it was meant for consumes it. A result that never got
     * filled in (TINCLIBC was abandoned) is dropped too. */
    if (strncmp((const char *)hdr + TINC_HND_RETURN_TO, app, 9) == 0)
        ti_Delete(TINC_HND_NAME);
    return err;
}
