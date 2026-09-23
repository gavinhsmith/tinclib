/*
 * GETs a page and prints the status, the content type and the start of the
 * body on the home screen. Sends the user to TINCLIBC when Wi-Fi isn't set up.
 */

#include <stdio.h>
#include <ti/getcsc.h>
#include <ti/screen.h>

#include "tinclib.h"

#define URL "http://example.com/"

static void wait_key(void) {
    while (!os_GetCSC()) {
    }
}

int main(void) {
    static const tinc_config_t cfg = { "TINCHELO", 0, TINC_CF_ASCII };
    tinc_request_t req = { TINC_GET, URL, NULL, NULL, 0 };
    tinc_err_t err;
    tinc_state_t st;
    char buf[64];
    uint16_t shown = 0;

    os_ClrHome();
    err = tinc_init(&cfg);
    if (err == TINC_ERR_SETUP_CANCELLED) {
        /* The user already said no; don't send them straight back. */
        puts("Setup cancelled.");
        goto out;
    }
    if (err != TINC_OK && err != TINC_ERR_SETUP_FAILED) {
        printf("%s\n", tinc_errString(err));
        goto out;
    }
    if (!tinc_isActive(TINC_WIFI)) {
        /* Nothing to save here; a real app saves its state first. */
        err = tinc_openConfig("hello needs Wi-Fi");
        printf("%s\n", tinc_errString(err));
        goto out;
    }

    err = tinc_request(&req);
    if (err != TINC_OK) {
        printf("%s\n", tinc_errString(err));
        goto out;
    }
    while ((st = tinc_poll()) != TINC_DONE && st != TINC_ERROR) {
        int16_t n = tinc_read(buf, sizeof buf - 1);

        if (n && !shown)
            printf("%u %s\n", tinc_httpStatus(), tinc_contentType());
        if (n && shown < 120) {
            buf[n] = '\0';
            fputs(buf, stdout);
            shown += n;
        }
        if (os_GetCSC()) {
            tinc_abort();
            break;
        }
    }
    if (st == TINC_ERROR)
        printf("\n%s\n", tinc_errString(tinc_error()));

out:
    tinc_shutdown();
    wait_key();
    return 0;
}
