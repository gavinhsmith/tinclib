/*
 * Hardware test: handoff, the os_RunPrgm spike. Checks on the real OS that
 * app -> TINCLIBC -> app works:
 *
 *   1st run: tinc_openConfig() must not return; it starts TINCLIBC (here the
 *            stub in tests/hw/stub_tinclibc), which answers "cancelled" in
 *            TINCHND and exits.
 *   2nd run: main() again, through os_RunPrgm's return callback, where
 *            tinc_init() must report TINC_ERR_SETUP_CANCELLED, exactly once.
 *
 * Green screen = the whole round trip worked. (Having TINCLIBC relaunch the
 * app with os_RunPrgm instead crashes the calculator; see src/tinc_config.c.)
 */

#include "tinclib.h"
#include "../verdict.h"

int main(void) {
    static const tinc_config_t cfg = { "TAHANDOF", 0, 0 };
    tinc_err_t err = tinc_init(&cfg);

    if (err != TINC_ERR_SETUP_CANCELLED) {
        /* First run (no board in the emulator, so NO_DEVICE). */
        CHECK(err == TINC_ERR_NO_DEVICE);
        tinc_openConfig("hw test");
        CHECK(0);   /* openConfig came back: TINCLIBC didn't start */
    } else {
        CHECK(tinc_init(&cfg) == TINC_ERR_NO_DEVICE);   /* result consumed */
    }
    tinc_shutdown();

    verdict();
    return 0;
}
