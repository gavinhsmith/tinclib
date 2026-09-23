/*
 * GETs a page and prints the status, the content type and the body on the
 * home screen, a screenful at a time ([clear] stops). Sends the user to
 * TINCLIBC when Wi-Fi isn't set up.
 */

#include <stdio.h>
#include <ti/getcsc.h>
#include <ti/screen.h>

#include "tinclib.h"

#define URL "http://example.com/"

static uint8_t wait_key(void) {
    uint8_t k;

    while (!(k = os_GetCSC())) {
    }
    return k;
}

/* Prints the body, pausing when the 26x10 home screen is full. Returns
 * false if the user pressed [clear] at the pause. */
static bool show(const char *s, int16_t n) {
    unsigned int row, col;

    while (n--) {
        char c = *s++;

        os_GetCursorPos(&row, &col);
        if (row == 9 && (c == '\n' || col == 25)) {
            if (wait_key() == sk_Clear)
                return false;
            os_ClrHome();
            if (c == '\n')
                continue;
        }
        putchar(c);
    }
    return true;
}

int main(void) {
    static const tinc_config_t cfg = { "TINCHELO", 0, TINC_CF_ASCII };
    tinc_request_t req = { TINC_GET, URL, NULL, NULL, 0 };
    tinc_err_t err;
    tinc_state_t st;
    char buf[64];
    bool shown = false;

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
        int16_t n = tinc_read(buf, sizeof buf);

        if (n && !shown) {
            printf("%u %s\n", tinc_httpStatus(), tinc_contentType());
            shown = true;
        }
        if (!show(buf, n) || os_GetCSC() == sk_Clear) {
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
