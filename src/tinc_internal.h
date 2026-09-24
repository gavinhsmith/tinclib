/* Library-internal state and link helpers. Not part of the API. */
#ifndef TINC_INTERNAL_H
#define TINC_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include "tinclib.h"
#include "tinc_frame.h"

#if TINC_RX_BUF_SIZE < 64 || TINC_RX_BUF_SIZE > 1024
#error "TINC_RX_BUF_SIZE must be 64..1024"
#endif

/* srldrvce's own ring buffers: at least 128, even. */
#ifndef TINC_SRL_BUF_SIZE
#define TINC_SRL_BUF_SIZE 256
#endif

/* BODY_READ long-poll: bounds how long one tinc_poll() can block. */
#define TINC_BODY_WAIT_MS 50u

/* One piece of an outgoing payload; xfer() sends up to three back to back. */
typedef struct {
    const void *p;
    uint16_t len;
} tinc_piece_t;

typedef struct {
    bool ready;              /* tinc_init() got through HELLO */
    tinc_config_t cfg;
    uint8_t seq;
    uint16_t peer_max;       /* the board's receive limit, from HELLO */

    tinc_parser parser;
    clock_t last_byte;       /* clock() of the last received byte */
    /* ponytail: one buffer for frames and the pre-fetched body chunk; fine
     * while nothing else is exchanged with a chunk pending (tinc_isActive
     * skips its STATUS then). Split it if POST streaming needs traffic
     * alongside a pending chunk. */
    uint8_t frame[TINC_FRAME_BUF(TINC_RX_BUF_SIZE)];

    /* the one request */
    bool active;             /* the board holds it; tinc_poll() drives it */
    uint8_t state;           /* tinc_state_t */
    tinc_err_t err;
    uint8_t err_detail;      /* TINC_TLSR_* for ERR_TLS / ERR_CERT, else 0 */
    uint16_t http_status;
    char ctype[TINC_CTYPE_MAX + 1];
    uint32_t offset;         /* next BODY_READ offset */
    const uint8_t *chunk;    /* pre-fetched body, inside frame[] */
    uint16_t chunk_len, chunk_pos;
    bool eof;
} tinc_state_store_t;

extern tinc_state_store_t tinc_g;

/* Sends one frame and waits for its reply, retrying with the same SEQ.
 * On TINC_OK the reply payload is in tinc_g.parser.payload / .len. An error
 * reply returns its code. The board resetting mid-request fails the request
 * with TINC_ERR_ESP_RESET instead of resending anything. */
tinc_err_t tinc_xfer(uint8_t type, const tinc_piece_t *pieces, uint8_t n,
                     uint16_t wait_ms);

/* True once ms have passed since start (a clock() value). */
bool tinc_elapsed(clock_t start, uint16_t ms);

/* Busy-waits ms while keeping USB serviced. */
void tinc_sleep(uint16_t ms);

/* Ends the request (releases it) with the given state and error. */
void tinc_endRequest(uint8_t state, tinc_err_t err);

/* tinc_config.c: consumes a TINCLIBC result meant for us, else TINC_OK. */
tinc_err_t tinc_takeSetupResult(void);

#endif /* TINC_INTERNAL_H */
