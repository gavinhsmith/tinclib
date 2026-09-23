/*
 * Hardware test: nodevice. The emulator has no ESP board, so this checks the
 * no-board paths on the real CE: tinc_init gives up after TINC_DEVICE_WAIT_MS
 * (which also shows clock() runs), and nothing else hangs or pretends to work.
 */

#include <time.h>

#include "tinclib.h"
#include "../verdict.h"

static unsigned long ms_since(clock_t start) {
    return (unsigned long)(clock() - start) * 1000 / CLOCKS_PER_SEC;
}

int main(void) {
    static const tinc_request_t req = { TINC_GET, "http://example.com/", NULL, NULL, 0 };
    char buf[8];
    clock_t start;

    start = clock();
    CHECK(tinc_init(NULL) == TINC_ERR_NO_DEVICE);
    CHECK(ms_since(start) >= TINC_DEVICE_WAIT_MS - 100);
    CHECK(ms_since(start) < TINC_DEVICE_WAIT_MS + 1000);

    start = clock();
    CHECK(!tinc_isActive(TINC_WIFI));
    CHECK(ms_since(start) < 100);

    CHECK(tinc_request(&req) == TINC_ERR_NOT_INIT);
    CHECK(tinc_poll() == TINC_IDLE);
    CHECK(tinc_read(buf, sizeof buf) == 0);
    CHECK(tinc_openConfig("x") == TINC_ERR_BAD_ARG);   /* no appName */
    tinc_abort();
    tinc_shutdown();
    tinc_shutdown();                                     /* twice is fine */

    verdict();
    return 0;
}
