/* The link: USB serial to the board, framing, stop-and-wait with retries,
 * the HELLO handshake, and reset detection. */

#include <string.h>
#include <time.h>

#include <srldrvce.h>
#include <usbdrvce.h>

#include "crc16.h"
#include "tinc_internal.h"

tinc_state_store_t tinc_g;

static srl_device_t srl;
static bool has_srl;
static bool usb_up;
static uint8_t srl_buf[TINC_SRL_BUF_SIZE];

/* The calculator is the USB host and the board's bridge chip the device.
 * A PC acting as the host (USB_HOST_CONFIGURE_EVENT) is accepted too, which
 * lets a PC-side fake board stand in for the ESP during development. */
static usb_error_t on_usb(usb_event_t event, void *data, usb_callback_data_t *cb)
{
    usb_error_t err = srl_UsbEventCallback(event, data, cb);
    usb_device_t dev = NULL;

    if (err != USB_SUCCESS)
        return err;

    if (event == USB_DEVICE_CONNECTED_EVENT && !(usb_GetRole() & USB_ROLE_DEVICE))
        usb_ResetDevice(data);
    else if (event == USB_DEVICE_ENABLED_EVENT && !(usb_GetRole() & USB_ROLE_DEVICE))
        dev = data;
    else if (event == USB_HOST_CONFIGURE_EVENT)
        dev = usb_FindDevice(NULL, NULL, USB_SKIP_HUBS);
    else if (event == USB_DEVICE_DISCONNECTED_EVENT && has_srl && data == srl.dev) {
        srl_Close(&srl);
        has_srl = false;
    }

    if (dev && !has_srl &&
        srl_Open(&srl, dev, srl_buf, sizeof srl_buf, SRL_INTERFACE_ANY,
                 TINC_BAUD_DEFAULT) == SRL_SUCCESS)
        has_srl = true;

    return USB_SUCCESS;
}

bool tinc_elapsed(clock_t start, uint16_t ms)
{
    return (unsigned long)(clock() - start) >= (unsigned long)ms * CLOCKS_PER_SEC / 1000u;
}

void tinc_sleep(uint16_t ms)
{
    clock_t start = clock();

    while (!tinc_elapsed(start, ms))
        usb_HandleEvents();
}

static bool send_all(const void *p, uint16_t len)
{
    const uint8_t *b = p;

    while (len) {
        int n;

        usb_HandleEvents();
        if (!has_srl)
            return false;
        n = srl_Write(&srl, b, len);
        if (n < 0)
            return false;
        b += n;
        len = (uint16_t)(len - n);
    }
    return true;
}

/* Streams a frame out piece by piece, so nothing is assembled in RAM. */
static bool send_frame(uint8_t type, const tinc_piece_t *pieces, uint8_t n)
{
    uint8_t hdr[TINC_HDR_LEN], crc_le[TINC_CRC_LEN];
    uint16_t len = 0, crc;
    uint8_t i;

    for (i = 0; i < n; i++)
        len = (uint16_t)(len + pieces[i].len);

    hdr[0] = TINC_SOF;
    hdr[1] = 0;
    hdr[2] = type;
    hdr[3] = tinc_g.seq;
    tinc_put_u16(hdr + 4, len);
    crc = tinc_crc16(hdr + 1, TINC_HDR_LEN - 1, TINC_CRC16_INIT);
    for (i = 0; i < n; i++)
        crc = tinc_crc16(pieces[i].p, pieces[i].len, crc);
    tinc_put_u16(crc_le, crc);

    if (!send_all(hdr, sizeof hdr))
        return false;
    for (i = 0; i < n; i++)
        if (!send_all(pieces[i].p, pieces[i].len))
            return false;
    return send_all(crc_le, sizeof crc_le);
}

/* Waits for the reply to (type, seq). 1 = got it, 0 = timed out,
 * -1 = the device went away. */
static int8_t wait_reply(uint8_t type, uint16_t timeout_ms)
{
    clock_t start = clock();
    tinc_parser *p = &tinc_g.parser;

    while (!tinc_elapsed(start, timeout_ms)) {
        uint8_t b;
        int n;

        usb_HandleEvents();
        if (!has_srl)
            return -1;
        n = srl_Read(&srl, &b, 1);
        if (n < 0)
            return -1;
        if (n == 0) {
            if (p->pos && tinc_elapsed(tinc_g.last_byte, TINC_INTERBYTE_RESET_MS))
                tinc_parser_reset(p);
            continue;
        }
        tinc_g.last_byte = clock();
        /* Stale replays of earlier frames and events are skipped. */
        if (tinc_parser_feed(p, b) == TINC_PARSE_FRAME &&
            (p->flags & TINC_FLAG_RESP) && p->type == type && p->seq == tinc_g.seq)
            return 1;
    }
    return 0;
}

static tinc_err_t xfer_once(uint8_t type, const tinc_piece_t *pieces, uint8_t n,
                            uint16_t wait_ms)
{
    uint8_t tries;

    tinc_g.seq++;
    for (tries = 0; tries < TINC_RETRY_MAX; tries++) {
        int8_t r;

        tinc_parser_reset(&tinc_g.parser);
        if (!send_frame(type, pieces, n))
            return TINC_ERR_NO_DEVICE;
        r = wait_reply(type, (uint16_t)(TINC_REPLY_TIMEOUT_MS + wait_ms));
        if (r < 0)
            return TINC_ERR_NO_DEVICE;
        if (r > 0) {
            tinc_parser *p = &tinc_g.parser;

            if (!(p->flags & TINC_FLAG_ERR))
                return TINC_OK;
            return p->len ? p->payload[0] : TINC_ERR_BAD_LEN;
        }
    }
    return TINC_ERR_NO_REPLY;
}

static tinc_err_t hello(void)
{
    uint8_t req[TINC_HELLO_REQ_LEN];
    tinc_piece_t piece = { req, sizeof req };
    const uint8_t *r;
    tinc_err_t err;

    req[TINC_HELLO_MAJOR] = TINC_PROTO_MAJOR;
    req[TINC_HELLO_MINOR] = TINC_PROTO_MINOR;
    tinc_put_u16(req + TINC_HELLO_CAPS, 0);
    tinc_put_u16(req + TINC_HELLO_MAX_PAYLOAD, TINC_RX_BUF_SIZE);

    tinc_g.peer_max = TINC_PAYLOAD_MIN;
    err = xfer_once(TINC_T_HELLO, &piece, 1, 0);
    if (err != TINC_OK)
        return err;
    r = tinc_g.parser.payload;
    if (tinc_g.parser.len < TINC_HELLO_REQ_LEN)
        return TINC_ERR_BAD_LEN;
    /* Pre-1.0 both sides need the exact same MAJOR.MINOR. */
    if (r[TINC_HELLO_MAJOR] != TINC_PROTO_MAJOR || r[TINC_HELLO_MINOR] != TINC_PROTO_MINOR)
        return TINC_ERR_VERSION;
    tinc_g.peer_max = tinc_get_u16(r + TINC_HELLO_MAX_PAYLOAD);
    if (tinc_g.peer_max < TINC_PAYLOAD_MIN)
        tinc_g.peer_max = TINC_PAYLOAD_MIN;
    return TINC_OK;
}

tinc_err_t tinc_xfer(uint8_t type, const tinc_piece_t *pieces, uint8_t n,
                     uint16_t wait_ms)
{
    tinc_err_t err = xfer_once(type, pieces, n, wait_ms);

    if (err != TINC_ERR_NO_HELLO)
        return err;

    /* The board reset since our HELLO. Handshake again; resend only if no
     * request was in flight, since a reset request (a POST!) must never be
     * silently resent. */
    err = hello();
    if (err != TINC_OK)
        return err;
    if (tinc_g.active) {
        tinc_endRequest(TINC_ERROR, TINC_ERR_ESP_RESET);
        return TINC_ERR_ESP_RESET;
    }
    return xfer_once(type, pieces, n, wait_ms);
}

tinc_err_t tinc_init(const tinc_config_t *cfg)
{
    static const tinc_config_t defaults = { NULL, 0, 0 };
    tinc_err_t setup, err;
    clock_t start;

    if (tinc_g.ready)
        tinc_abort();
    memset(&tinc_g, 0, sizeof tinc_g);
    tinc_g.cfg = cfg ? *cfg : defaults;
    tinc_parser_init(&tinc_g.parser, tinc_g.frame, sizeof tinc_g.frame);

    setup = tinc_takeSetupResult();

    /* A retry keeps USB up: tearing it down makes a PC host re-enumerate
     * the calculator, and its COM port goes away with it. */
    if (!usb_up) {
        if (usb_Init(on_usb, NULL, srl_GetCDCStandardDescriptors(),
                     USB_DEFAULT_INIT_FLAGS) != USB_SUCCESS) {
            usb_Cleanup();
            return setup ? setup : TINC_ERR_NO_DEVICE;
        }
        usb_up = true;
    }

    /* A PC host is "connected" as soon as it configures us, but its app
     * can only open the port while we pump USB events, so keep saying
     * HELLO until the deadline. */
    start = clock();
    do {
        while (!has_srl && !tinc_elapsed(start, TINC_DEVICE_WAIT_MS))
            usb_HandleEvents();
        err = has_srl ? hello() : TINC_ERR_NO_DEVICE;
    } while (err == TINC_ERR_NO_REPLY && !tinc_elapsed(start, TINC_DEVICE_WAIT_MS));
    tinc_g.ready = err == TINC_OK;
    return setup ? setup : err;
}

void tinc_shutdown(void)
{
    if (tinc_g.ready)
        tinc_abort();
    tinc_g.ready = false;
    if (has_srl)
        srl_Close(&srl);
    has_srl = false;
    if (usb_up)
        usb_Cleanup();
    usb_up = false;
}

const char *tinc_errString(tinc_err_t e)
{
    switch (e) {
    case TINC_OK:                     return "OK";
    case TINC_ERR_UNSUPPORTED:        return "Not supported by board";
    case TINC_ERR_NO_HELLO:           return "Board not ready";
    case TINC_ERR_VERSION:            return "Board firmware version mismatch";
    case TINC_ERR_BAD_LEN:            return "Request too long";
    case TINC_ERR_BUSY:               return "A request is already running";
    case TINC_ERR_BAD_STATE:          return "Request lost";
    case TINC_ERR_BAD_OFFSET:         return "Body out of sync";
    case TINC_ERR_BAD_ARG:            return "Bad request";
    case TINC_ERR_UNSUPPORTED_SCHEME: return "Only http:// and https:// are supported";
    case TINC_ERR_LOCKED:             return "Wi-Fi settings are locked";
    case TINC_ERR_WIFI_DOWN:          return "Wi-Fi not connected";
    case TINC_ERR_DNS:                return "Host not found";
    case TINC_ERR_CONNECT:            return "Could not connect";
    case TINC_ERR_TIMEOUT:            return "Timed out";
    case TINC_ERR_HTTP_PROTO:         return "Bad HTTP response";
    case TINC_ERR_TOO_MANY_REDIRECTS: return "Too many redirects";
    case TINC_ERR_NO_MEM:             return "Board out of memory";
    case TINC_ERR_TLS:                return "Secure connection failed";
    case TINC_ERR_CERT:               return "Certificate rejected";
    case TINC_ERR_TIME:               return "Board clock not set";
    case TINC_ERR_REDIRECT_DOWNGRADE: return "Redirect from https to http";
    case TINC_ERR_NO_DEVICE:          return "No board connected";
    case TINC_ERR_NO_REPLY:           return "Board not responding";
    case TINC_ERR_ESP_RESET:          return "Board reset";
    case TINC_ERR_NOT_INIT:           return "tinc_init not called";
    case TINC_ERR_UNSUPPORTED_METHOD: return "Method not supported";
    case TINC_ERR_SETUP_CANCELLED:    return "Setup cancelled";
    case TINC_ERR_SETUP_FAILED:       return "Setup failed";
    case TINC_ERR_NO_CONFIG_APP:      return "TINCLIBC not installed";
    default:                          return "Unknown error";
    }
}
