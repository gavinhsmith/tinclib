/* A fake ESP board on the far end of the stubbed serial link. It speaks the
 * protocol through tinclib-protocol's own frame parser/encoder, so the
 * library is checked against the reference implementation, not itself. */
#ifndef FAKE_ESP_H
#define FAKE_ESP_H

#include <stdbool.h>
#include <stdint.h>

#include "protocol.h"

typedef struct {
    const uint8_t *expect;   /* CE frame expected at this step, or NULL */
    uint16_t expect_len;
    const uint8_t *reply;    /* sent verbatim */
    uint16_t reply_len;
} fake_step_t;

typedef struct {
    /* setup, set by the test */
    bool present;            /* board plugged in */
    uint8_t major, minor;    /* version it answers HELLO with */
    uint16_t max_payload;
    uint8_t wifi_state;
    bool wifi_locked;        /* STATUS flags: TINC_STATUSF_WIFI_LOCKED */
    uint8_t wifi_connecting_polls; /* STATUS says CONNECTING this many times first */
    uint8_t req_polls;       /* REQ_STATUS says CONNECTING, then WAIT_HEADERS, this many times each */
    uint8_t req_err;         /* nonzero: request ends in ERROR with this */
    uint16_t http_status;
    const char *ctype;
    const char *body;
    uint16_t chunk_max;      /* 0 = as much as max_len allows */
    uint8_t empty_reads;     /* BODY_READs answered empty before the data */
    uint8_t drop_replies;    /* the next N replies are "lost" (not sent) */

    /* scripted mode: replay these steps instead of emulating */
    const fake_step_t *script;
    uint8_t script_len, script_pos;
    bool script_mismatch;

    /* observed */
    uint16_t executed[256];  /* commands run per TYPE (replays don't count) */
    uint16_t frames_in;      /* CE frames received, retries included */
    char url[256];
    char headers[256];
    uint8_t req_flags, req_timeout;
} fake_esp_t;

extern fake_esp_t fake;

/* Plugs in a fresh board: HELLO needed, Wi-Fi connected, no request. */
void fake_reset(void);

/* The board reboots (keeps the test setup, forgets the handshake). */
void fake_reboot(void);

/* The in-memory TINCHND appvar. */
extern uint8_t fake_appvar[256];
extern uint16_t fake_appvar_len;
extern bool fake_appvar_exists;

/* Called from os_RunPrgm(); NULL makes os_RunPrgm fail. The callback it
 * was given is kept (calling it would re-enter the test's main()). */
extern void (*fake_run_prgm)(const char *name);
extern int (*fake_run_prgm_callback)(void *data, int retval);

/* Microseconds, advanced by every clock() call the library makes. */
extern unsigned long fake_now;

#endif
