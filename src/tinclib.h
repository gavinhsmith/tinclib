/**
 * @file tinclib.h
 * @brief Wi-Fi / HTTP for TI-84 Plus CE programs, through an ESP8266 board
 *        on the USB port.
 *
 * One request at a time: the library owns the single in-flight request, and
 * tinc_poll() is the only thing that moves it forward.
 *
 * @code
 * tinc_config_t cfg = { "MYAPP", 0, 0 };
 * if (tinc_init(&cfg) == TINC_OK && tinc_isActive(TINC_WIFI)) {
 *     tinc_request_t req = { TINC_GET, "http://example.com/", NULL, NULL, 0 };
 *     tinc_state_t st;
 *     tinc_request(&req);
 *     while ((st = tinc_poll()) != TINC_DONE && st != TINC_ERROR) {
 *         char buf[64];
 *         int16_t n = tinc_read(buf, sizeof buf);
 *         // use n bytes of buf
 *     }
 * }
 * tinc_shutdown();
 * @endcode
 *
 * Needs the CE C libraries usbdrvce, srldrvce and fileioc on the calculator.
 */
#ifndef TINCLIB_H
#define TINCLIB_H

#include <stdbool.h>
#include <stdint.h>

#include "protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TINC_VERSION "0.2.0"

/**
 * Largest frame payload this side receives, and so the largest body chunk
 * one tinc_poll() pre-fetches. Costs about this many bytes of RAM. Define it
 * before building the library to tune it; 64..1024.
 */
#ifndef TINC_RX_BUF_SIZE
#define TINC_RX_BUF_SIZE 256
#endif

/** How long tinc_init() waits for the ESP board to show up on USB. */
#ifndef TINC_DEVICE_WAIT_MS
#define TINC_DEVICE_WAIT_MS 2000u
#endif

/** How long tinc_isActive() waits while Wi-Fi is still connecting. */
#ifndef TINC_WIFI_WAIT_MS
#define TINC_WIFI_WAIT_MS 8000u
#endif

/**
 * Error codes. The wire codes from protocol.h (TINC_OK, TINC_ERR_DNS,
 * TINC_ERR_TIMEOUT, ...) are used as-is; the library adds its own from 0x80.
 */
typedef uint8_t tinc_err_t;

enum {
    TINC_ERR_NO_DEVICE          = 0x80, /**< no ESP board on the USB port */
    TINC_ERR_NO_REPLY           = 0x81, /**< the board stopped answering */
    TINC_ERR_ESP_RESET          = 0x82, /**< the board reset mid-request; not resent */
    TINC_ERR_NOT_INIT           = 0x83, /**< tinc_init() hasn't succeeded */
    TINC_ERR_UNSUPPORTED_METHOD = 0x84, /**< POST needs a newer protocol */
    TINC_ERR_SETUP_CANCELLED    = 0x85, /**< tinc_init(): the user backed out of TINCLIBC */
    TINC_ERR_SETUP_FAILED       = 0x86, /**< tinc_init(): TINCLIBC couldn't set up Wi-Fi */
    TINC_ERR_NO_CONFIG_APP      = 0x87  /**< tinc_openConfig(): TINCLIBC isn't installed */
};

/** tinc_config_t.flags: ask the board to transcode text bodies to ASCII. */
#define TINC_CF_ASCII 0x01u

typedef struct {
    const char *appName;   /**< this program's name, e.g. "MINIBRWS"; needed by tinc_openConfig() */
    uint8_t     timeoutS;  /**< per-phase request timeout in seconds, 0 = board default (10) */
    uint8_t     flags;     /**< TINC_CF_* */
} tinc_config_t;

typedef enum {
    TINC_WIFI = 0          /**< joined a network */
} tinc_mode_t;

typedef enum {
    TINC_GET = 1,
    TINC_POST = 2          /**< not supported yet: TINC_ERR_UNSUPPORTED_METHOD */
} tinc_method_t;

typedef enum {
    TINC_IDLE = 0,         /**< no request */
    TINC_CONNECTING,
    TINC_SECURING,         /**< TLS handshake (not before HTTPS support) */
    TINC_WAITING,          /**< sent, waiting for the response headers */
    TINC_BODY,             /**< tinc_read() the body; status and type are valid */
    TINC_DONE,             /**< whole body read; the request is released */
    TINC_ERROR             /**< see tinc_error(); the request is released */
} tinc_state_t;

/**
 * Opens the link to the board. cfg may be NULL for defaults and is copied.
 *
 * Also picks up the outcome of an earlier tinc_openConfig(): when this program
 * restarts after TINCLIBC it returns TINC_ERR_SETUP_CANCELLED or
 * TINC_ERR_SETUP_FAILED once. Don't answer CANCELLED with another
 * tinc_openConfig(), or the user can never leave.
 */
tinc_err_t tinc_init(const tinc_config_t *cfg);

/**
 * True when the board is usable in that mode. While Wi-Fi is still
 * connecting (e.g. right after power-up) it waits up to TINC_WIFI_WAIT_MS;
 * otherwise it answers straight away.
 */
bool tinc_isActive(tinc_mode_t mode);

/** Aborts any request and closes the link. Call before the program exits. */
void tinc_shutdown(void);

typedef struct {
    tinc_method_t method;
    const char   *url;       /**< "http://..." (no https yet) */
    const char   *headers;   /**< raw "Name: value\r\n..." or NULL */
    /**
     * Request body. NOT COPIED: it is streamed out from this pointer during
     * tinc_poll(), so it must stay valid and unchanged until the state
     * reaches TINC_BODY (or the request ends).
     */
    const void   *body;
    uint16_t      bodyLen;
} tinc_request_t;

/**
 * Starts a request and returns immediately; drive it with tinc_poll().
 * Replaces a finished one; fails with TINC_ERR_BUSY while one is running
 * (tinc_abort() it first). req is not kept, but see body above.
 */
tinc_err_t tinc_request(const tinc_request_t *req);

/** Moves the request along. Call it in your main loop. */
tinc_state_t tinc_poll(void);

/**
 * Copies up to cap bytes of body that tinc_poll() fetched. Returns the count,
 * 0 when nothing is waiting yet. Never talks to the board.
 */
int16_t tinc_read(void *buf, uint16_t cap);

/** HTTP status code, once the state reached TINC_BODY; 0 before. */
uint16_t tinc_httpStatus(void);

/** Content-Type, once the state reached TINC_BODY; "" before. */
const char *tinc_contentType(void);

/** Why the last request ended in TINC_ERROR. */
tinc_err_t tinc_error(void);

/** Short English description of an error code. */
const char *tinc_errString(tinc_err_t e);

/** Cancels the request, if any. */
void tinc_abort(void);

/**
 * Hands the user over to TINCLIBC to set up Wi-Fi, showing them hint.
 *
 * DOES NOT RETURN when it works: this program is unloaded while TINCLIBC
 * runs, and when the user is done it starts over from main(). EVERYTHING IN
 * RAM IS LOST. Save what you need (to your own appvar) before calling, and
 * restore it on the next start; tinc_init() then reports how setup went.
 * tinc_config_t.appName (this program's name) is still required.
 *
 * Returns only on failure: TINC_ERR_NO_CONFIG_APP, or TINC_ERR_BAD_ARG when
 * no appName was configured.
 */
tinc_err_t tinc_openConfig(const char *hint);

#ifdef __cplusplus
}
#endif

#endif /* TINCLIB_H */
