/* Host stand-ins for the CE libraries tinclib uses: usbdrvce and srldrvce
 * (wired to a fake board), fileioc (one in-memory appvar), os_RunPrgm and
 * clock(). The build defines clock as tinc_fake_clock so time only moves when
 * the library looks at it: timeouts run instantly and deterministically. */

#include <string.h>
#include <time.h>

#include <fileioc.h>
#include <srldrvce.h>
#include <ti/vars.h>
#include <usbdrvce.h>

#include "fake_esp.h"
#include "tinc_frame.h"

fake_esp_t fake;
unsigned long fake_now;   /* microseconds */

/* 10 us per call: fast enough that timeouts take no real time, slow enough
 * that a whole frame arrives well within a reply timeout. */
clock_t clock(void)
{
    fake_now += 10;
    return (clock_t)(fake_now / 1000 * CLOCKS_PER_SEC / 1000);
}

/* ---- The fake board --------------------------------------------------- */

static struct {
    tinc_parser parser;
    uint8_t in[TINC_FRAME_BUF(TINC_PAYLOAD_LIMIT)];
    uint8_t rxq[8192];                 /* board -> CE bytes */
    uint16_t rxq_len, rxq_pos;
    uint8_t cache[TINC_FRAME_BUF(TINC_PAYLOAD_LIMIT)];
    uint16_t cache_len;
    uint8_t cache_seq;
    bool cache_valid, hello_done;
    uint16_t ce_max;                   /* the CE's receive limit, from HELLO */
    /* the request */
    bool active;
    uint8_t polls;
    uint32_t last_off;
    uint16_t last_len;
    bool have_last, eof;
    bool responded;                    /* answered before the upload finished */
} esp;

static void push(const uint8_t *p, uint16_t len)
{
    if (fake.drop_replies) {
        fake.drop_replies--;
        return;
    }
    if (esp.rxq_pos == esp.rxq_len)
        esp.rxq_pos = esp.rxq_len = 0;
    memcpy(esp.rxq + esp.rxq_len, p, len);
    esp.rxq_len = (uint16_t)(esp.rxq_len + len);
}

static void reply(uint8_t flags, uint8_t type, uint8_t seq, const uint8_t *payload, uint16_t len)
{
    esp.cache_len = tinc_frame_encode(esp.cache, (uint8_t)(TINC_FLAG_RESP | flags), type, seq,
                                      payload, len);
    esp.cache_seq = seq;
    esp.cache_valid = true;
    push(esp.cache, esp.cache_len);
}

static void reply_err(uint8_t type, uint8_t seq, uint8_t err)
{
    reply(TINC_FLAG_ERR, type, seq, &err, 1);
}

static uint8_t req_state(void)
{
    if (!esp.active)
        return TINC_RS_IDLE;
    if (esp.polls < fake.req_polls)
        return strncmp(fake.url, "https:", 6) == 0 ? TINC_RS_TLS : TINC_RS_CONNECTING;
    if (fake.req_err)
        return TINC_RS_ERROR;
    if (fake.upload_len < fake.content_len && !esp.responded)
        return TINC_RS_SENDING;
    if (esp.polls < 2 * fake.req_polls)
        return TINC_RS_WAIT_HEADERS;
    return esp.eof ? TINC_RS_DONE : TINC_RS_BODY;
}

static void body_read(uint8_t seq, const uint8_t *q, uint16_t qlen)
{
    uint8_t out[TINC_PAYLOAD_LIMIT];
    uint32_t off, total = fake.body ? (uint32_t)strlen(fake.body) : 0;
    uint16_t max, len;
    uint8_t st = req_state();

    if (qlen < TINC_READ_REQ_LEN)
        return reply_err(TINC_T_BODY_READ, seq, TINC_ERR_BAD_LEN);
    if (st == TINC_RS_ERROR)
        return reply_err(TINC_T_BODY_READ, seq, fake.req_err);
    if (st != TINC_RS_BODY && st != TINC_RS_DONE)
        return reply_err(TINC_T_BODY_READ, seq, TINC_ERR_BAD_STATE);

    off = tinc_get_u32(q + TINC_READ_OFFSET);
    max = tinc_get_u16(q + TINC_READ_MAX_LEN);
    if (max > fake.max_payload - TINC_READ_DATA)
        max = (uint16_t)(fake.max_payload - TINC_READ_DATA);

    if (esp.have_last && esp.last_len && off == esp.last_off) {
        len = esp.last_len;                         /* re-deliver */
    } else if (esp.have_last ? off == esp.last_off + esp.last_len : off == 0) {
        len = (uint16_t)(total - off < max ? total - off : max);
        if (fake.chunk_max && len > fake.chunk_max)
            len = fake.chunk_max;
        if (fake.empty_reads) {           /* never cached, like the real one */
            fake.empty_reads--;
            len = 0;
        }
    } else {
        return reply_err(TINC_T_BODY_READ, seq, TINC_ERR_BAD_OFFSET);
    }

    esp.have_last = true;
    esp.last_off = off;
    esp.last_len = len;
    esp.eof = off + len == total;
    tinc_put_u32(out + TINC_READ_OFFSET, off);
    out[TINC_READ_FLAGS] = esp.eof ? TINC_READF_EOF : 0;
    if (len)
        memcpy(out + TINC_READ_DATA, fake.body + off, len);
    reply(0, TINC_T_BODY_READ, seq, out, (uint16_t)(TINC_READ_DATA + len));
}

static void body_write(uint8_t seq, const uint8_t *q, uint16_t qlen)
{
    uint8_t out[TINC_WRITE_RESP_LEN];
    uint8_t st = req_state();
    uint32_t off;
    uint16_t n;

    if (qlen < TINC_WRITE_DATA)
        return reply_err(TINC_T_BODY_WRITE, seq, TINC_ERR_BAD_LEN);
    if (st == TINC_RS_ERROR)
        return reply_err(TINC_T_BODY_WRITE, seq, fake.req_err);
    off = tinc_get_u32(q + TINC_WRITE_OFFSET);
    n = (uint16_t)(qlen - TINC_WRITE_DATA);
    if (st == TINC_RS_SENDING) {
        if (off != fake.upload_len) {
            out[0] = TINC_ERR_BAD_OFFSET;
            tinc_put_u32(out + 1, fake.upload_len);
            return reply(TINC_FLAG_ERR, TINC_T_BODY_WRITE, seq, out, 5);
        }
        if (off + n > fake.content_len || off + n > sizeof fake.upload)
            return reply_err(TINC_T_BODY_WRITE, seq, TINC_ERR_BAD_ARG);
        if (fake.write_max && n > fake.write_max)
            n = fake.write_max;
        memcpy(fake.upload + off, q + TINC_WRITE_DATA, n);
        fake.upload_len = (uint16_t)(fake.upload_len + n);
        if (fake.respond_early && n)
            esp.responded = true;
    } else if (st != TINC_RS_CONNECTING && st != TINC_RS_TLS && !esp.responded) {
        return reply_err(TINC_T_BODY_WRITE, seq, TINC_ERR_BAD_STATE);
    }
    esp.polls++;
    tinc_put_u32(out + TINC_WRITE_NEXT_OFFSET, fake.upload_len);
    out[TINC_WRITE_FLAGS] = esp.responded ? TINC_WRITEF_RESPONDED : 0;
    reply(0, TINC_T_BODY_WRITE, seq, out, TINC_WRITE_RESP_LEN);
}

static void hdr_get(uint8_t seq, const uint8_t *q, uint16_t qlen)
{
    uint8_t out[TINC_PAYLOAD_LIMIT];
    uint8_t st = req_state();
    uint16_t off, total = 0, n;
    bool found;

    if (qlen < TINC_HGET_NAME || qlen != TINC_HGET_NAME + q[TINC_HGET_NAME_LEN])
        return reply_err(TINC_T_HDR_GET, seq, TINC_ERR_BAD_LEN);
    if (st == TINC_RS_ERROR)
        return reply_err(TINC_T_HDR_GET, seq, fake.req_err);
    if (st != TINC_RS_BODY && st != TINC_RS_DONE)
        return reply_err(TINC_T_HDR_GET, seq, TINC_ERR_BAD_STATE);
    if (!q[TINC_HGET_NAME_LEN])
        return reply_err(TINC_T_HDR_GET, seq, TINC_ERR_BAD_ARG);

    found = fake.hdr_name && strlen(fake.hdr_name) == q[TINC_HGET_NAME_LEN] &&
            memcmp(fake.hdr_name, q + TINC_HGET_NAME, q[TINC_HGET_NAME_LEN]) == 0;
    if (found)
        total = (uint16_t)strlen(fake.hdr_value);
    off = tinc_get_u16(q + TINC_HGET_OFFSET);
    if (off > total)
        return reply_err(TINC_T_HDR_GET, seq, TINC_ERR_BAD_OFFSET);
    n = (uint16_t)(total - off);
    if (n > esp.ce_max - TINC_HGET_DATA)
        n = (uint16_t)(esp.ce_max - TINC_HGET_DATA);
    out[TINC_HGET_FLAGS] = found ? TINC_HGETF_FOUND : 0;
    tinc_put_u16(out + TINC_HGET_TOTAL_LEN, total);
    if (n)
        memcpy(out + TINC_HGET_DATA, fake.hdr_value + off, n);
    reply(0, TINC_T_HDR_GET, seq, out, (uint16_t)(TINC_HGET_DATA + n));
}

static void copy_str(char *dst, const uint8_t *src, uint16_t n)
{
    if (n > 255)
        n = 255;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static void handle(const tinc_parser *p)
{
    const uint8_t *q = p->payload;
    uint8_t out[TINC_PAYLOAD_LIMIT];

    fake.frames_in++;

    if (fake.script) {
        const fake_step_t *s = &fake.script[fake.script_pos];

        if (fake.script_pos >= fake.script_len ||
            (s->expect && (s->expect_len != TINC_OVERHEAD + p->len ||
                           memcmp(s->expect, p->buf, s->expect_len) != 0))) {
            fake.script_mismatch = true;
            return;
        }
        fake.script_pos++;
        push(s->reply, s->reply_len);
        return;
    }

    /* A retry of the last frame gets the cached reply, never a re-run. */
    if (p->type != TINC_T_HELLO && esp.cache_valid && p->seq == esp.cache_seq) {
        push(esp.cache, esp.cache_len);
        return;
    }
    fake.executed[p->type]++;

    if (p->type == TINC_T_HELLO) {
        if (p->len < TINC_HELLO_REQ_LEN)
            return reply_err(p->type, p->seq, TINC_ERR_BAD_LEN);
        if (q[TINC_HELLO_MAJOR] != fake.major || q[TINC_HELLO_MINOR] != fake.minor) {
            out[0] = TINC_ERR_VERSION;
            out[1] = fake.major;
            out[2] = fake.minor;
            reply(TINC_FLAG_ERR, p->type, p->seq, out, 3);
            return;
        }
        esp.ce_max = tinc_get_u16(q + TINC_HELLO_MAX_PAYLOAD);
        if (esp.ce_max > TINC_PAYLOAD_LIMIT)
            esp.ce_max = TINC_PAYLOAD_LIMIT;
        out[TINC_HELLO_MAJOR] = fake.major;
        out[TINC_HELLO_MINOR] = fake.minor;
        tinc_put_u16(out + TINC_HELLO_CAPS, 0);
        tinc_put_u16(out + TINC_HELLO_MAX_PAYLOAD, fake.max_payload);
        tinc_put_u32(out + TINC_HELLO_FREE_HEAP, 20000);
        out[TINC_HELLO_WIFI_SLOTS] = 3;
        reply(0, p->type, p->seq, out, TINC_HELLO_RESP_LEN);
        esp.hello_done = true;
        esp.cache_valid = false;
        return;
    }
    if (!esp.hello_done)
        return reply_err(p->type, p->seq, TINC_ERR_NO_HELLO);

    switch (p->type) {
    case TINC_T_STATUS:
        memset(out, 0, TINC_STATUS_RESP_LEN);
        if (fake.wifi_connecting_polls) {
            fake.wifi_connecting_polls--;
            out[TINC_STATUS_WIFI_STATE] = TINC_WIFI_CONNECTING;
        } else {
            out[TINC_STATUS_WIFI_STATE] = fake.wifi_state;
        }
        out[TINC_STATUS_REQ_STATE] = req_state();
        out[TINC_STATUS_FLAGS] = fake.wifi_locked ? TINC_STATUSF_WIFI_LOCKED : 0;
        reply(0, p->type, p->seq, out, TINC_STATUS_RESP_LEN);
        break;

    case TINC_T_REQ_BEGIN: {
        uint16_t ul, hl;

        if (esp.active && req_state() != TINC_RS_DONE && req_state() != TINC_RS_ERROR)
            return reply_err(p->type, p->seq, TINC_ERR_BUSY);
        if (p->len < TINC_BEGIN_URL)
            return reply_err(p->type, p->seq, TINC_ERR_BAD_LEN);
        ul = tinc_get_u16(q + TINC_BEGIN_URL_LEN);
        hl = tinc_get_u16(q + TINC_BEGIN_HDR_LEN);
        if (TINC_BEGIN_URL + ul + hl != p->len)
            return reply_err(p->type, p->seq, TINC_ERR_BAD_LEN);
        if (q[TINC_BEGIN_METHOD] < TINC_METHOD_GET || q[TINC_BEGIN_METHOD] > TINC_METHOD_HEAD)
            return reply_err(p->type, p->seq, TINC_ERR_BAD_ARG);
        if ((q[TINC_BEGIN_METHOD] == TINC_METHOD_GET || q[TINC_BEGIN_METHOD] == TINC_METHOD_HEAD) &&
            tinc_get_u32(q + TINC_BEGIN_CONTENT_LEN))
            return reply_err(p->type, p->seq, TINC_ERR_BAD_ARG);
        if (fake.wifi_state != TINC_WIFI_CONNECTED)
            return reply_err(p->type, p->seq, TINC_ERR_WIFI_DOWN);
        copy_str(fake.url, q + TINC_BEGIN_URL, ul);
        copy_str(fake.headers, q + TINC_BEGIN_URL + ul, hl);
        fake.req_flags = q[TINC_BEGIN_FLAGS];
        fake.req_timeout = q[TINC_BEGIN_TIMEOUT_S];
        fake.method = q[TINC_BEGIN_METHOD];
        fake.content_len = tinc_get_u32(q + TINC_BEGIN_CONTENT_LEN);
        fake.upload_len = 0;
        esp.responded = false;
        esp.active = true;
        esp.polls = 0;
        esp.have_last = esp.eof = false;
        reply(0, p->type, p->seq, NULL, 0);
        break;
    }

    case TINC_T_REQ_STATUS: {
        const char *ct = fake.ctype ? fake.ctype : "";
        uint8_t st = req_state();

        memset(out, 0, TINC_RSTAT_CTYPE);
        out[TINC_RSTAT_STATE] = st;
        out[TINC_RSTAT_ERR] = st == TINC_RS_ERROR ? fake.req_err : 0;
        if (st == TINC_RS_BODY || st == TINC_RS_DONE) {
            tinc_put_u16(out + TINC_RSTAT_HTTP_STATUS, fake.http_status);
            tinc_put_u32(out + TINC_RSTAT_CONTENT_LEN, TINC_LEN_UNKNOWN);
            out[TINC_RSTAT_CTYPE_LEN] = (uint8_t)strlen(ct);
            memcpy(out + TINC_RSTAT_CTYPE, ct, strlen(ct));
        }
        out[TINC_RSTAT_CTYPE + out[TINC_RSTAT_CTYPE_LEN]] = 0;   /* err_detail */
        if (esp.active)
            esp.polls++;
        reply(0, p->type, p->seq, out, (uint16_t)(TINC_RSTAT_CTYPE + out[TINC_RSTAT_CTYPE_LEN] + 1));
        break;
    }

    case TINC_T_BODY_READ:
        body_read(p->seq, q, p->len);
        break;

    case TINC_T_BODY_WRITE:
        body_write(p->seq, q, p->len);
        break;

    case TINC_T_HDR_GET:
        hdr_get(p->seq, q, p->len);
        break;

    case TINC_T_REQ_ABORT:
        esp.active = false;
        reply(0, p->type, p->seq, NULL, 0);
        break;

    default:
        reply_err(p->type, p->seq, TINC_ERR_UNSUPPORTED);
        break;
    }
}

void fake_reboot(void)
{
    esp.hello_done = false;
    esp.cache_valid = false;
    esp.active = false;
}

void fake_reset(void)
{
    memset(&fake, 0, sizeof fake);
    memset(&esp, 0, sizeof esp);
    tinc_parser_init(&esp.parser, esp.in, sizeof esp.in);
    fake.present = true;
    fake.major = TINC_PROTO_MAJOR;
    fake.minor = TINC_PROTO_MINOR;
    fake.max_payload = TINC_PAYLOAD_LIMIT;
    fake.wifi_state = TINC_WIFI_CONNECTED;
    fake.http_status = 200;
}

/* ---- usbdrvce / srldrvce ---------------------------------------------- */

static usb_event_callback_t usb_handler;
static bool usb_connected;
static struct usb_device { int unused; } the_device;

usb_error_t usb_Init(usb_event_callback_t handler, usb_callback_data_t *data,
                     const usb_standard_descriptors_t *desc, unsigned flags)
{
    (void)data;
    (void)desc;
    (void)flags;
    usb_handler = handler;
    usb_connected = false;
    return USB_SUCCESS;
}

void usb_Cleanup(void)
{
    usb_handler = NULL;
    usb_connected = false;
}

usb_error_t usb_HandleEvents(void)
{
    if (!usb_handler)
        return USB_SUCCESS;
    if (fake.present && !usb_connected) {
        usb_connected = true;
        usb_handler(USB_DEVICE_CONNECTED_EVENT, &the_device, NULL);
        usb_handler(USB_DEVICE_ENABLED_EVENT, &the_device, NULL);
    } else if (!fake.present && usb_connected) {
        usb_connected = false;
        usb_handler(USB_DEVICE_DISCONNECTED_EVENT, &the_device, NULL);
    }
    return USB_SUCCESS;
}

usb_device_t usb_FindDevice(usb_device_t root, usb_device_t from, unsigned flags)
{
    (void)root;
    (void)from;
    (void)flags;
    return NULL;
}

usb_error_t usb_ResetDevice(usb_device_t device)
{
    (void)device;
    return USB_SUCCESS;
}

usb_role_t usb_GetRole(void)
{
    return 0;
}

srl_error_t srl_Open(srl_device_t *srl, usb_device_t dev, void *buffer, size_t size,
                     uint8_t interface, uint24_t rate)
{
    (void)buffer;
    (void)size;
    (void)interface;
    (void)rate;
    srl->dev = dev;
    return SRL_SUCCESS;
}

void srl_Close(srl_device_t *srl)
{
    srl->dev = NULL;
}

int srl_Read(srl_device_t *srl, void *data, size_t length)
{
    size_t n = (size_t)(esp.rxq_len - esp.rxq_pos);

    (void)srl;
    if (n > length)
        n = length;
    memcpy(data, esp.rxq + esp.rxq_pos, n);
    esp.rxq_pos = (uint16_t)(esp.rxq_pos + n);
    return (int)n;
}

int srl_Write(srl_device_t *srl, const void *data, size_t length)
{
    const uint8_t *b = data;
    size_t i;

    (void)srl;
    for (i = 0; i < length; i++)
        if (tinc_parser_feed(&esp.parser, b[i]) == TINC_PARSE_FRAME)
            handle(&esp.parser);
    return (int)length;
}

const usb_standard_descriptors_t *srl_GetCDCStandardDescriptors(void)
{
    return NULL;
}

usb_error_t srl_UsbEventCallback(usb_event_t event, void *event_data,
                                 usb_callback_data_t *callback_data)
{
    (void)event;
    (void)event_data;
    (void)callback_data;
    return USB_SUCCESS;
}

/* ---- fileioc: one appvar ---------------------------------------------- */

uint8_t fake_appvar[256];
uint16_t fake_appvar_len;
bool fake_appvar_exists;
static uint16_t appvar_pos;

uint8_t ti_Open(const char *name, const char *mode)
{
    (void)name;
    if (mode[0] == 'w') {
        fake_appvar_exists = true;
        fake_appvar_len = 0;
    } else if (!fake_appvar_exists) {
        return 0;
    }
    appvar_pos = 0;
    return 1;
}

int ti_Close(uint8_t handle)
{
    (void)handle;
    return 1;
}

size_t ti_Write(const void *data, size_t size, size_t count, uint8_t handle)
{
    size_t n = size * count;

    (void)handle;
    if (appvar_pos + n > sizeof fake_appvar)
        return 0;
    memcpy(fake_appvar + appvar_pos, data, n);
    appvar_pos = (uint16_t)(appvar_pos + n);
    if (appvar_pos > fake_appvar_len)
        fake_appvar_len = appvar_pos;
    return count;
}

size_t ti_Read(void *data, size_t size, size_t count, uint8_t handle)
{
    size_t got = 0;

    (void)handle;
    while (got < count && appvar_pos + size <= fake_appvar_len) {
        memcpy((uint8_t *)data + got * size, fake_appvar + appvar_pos, size);
        appvar_pos = (uint16_t)(appvar_pos + size);
        got++;
    }
    return got;
}

int ti_Seek(int offset, unsigned int origin, uint8_t handle)
{
    long to = (origin == SEEK_CUR ? appvar_pos : origin == SEEK_END ? fake_appvar_len : 0) + offset;

    (void)handle;
    if (to < 0 || to > fake_appvar_len)
        return EOF;
    appvar_pos = (uint16_t)to;
    return 0;
}

int ti_Delete(const char *name)
{
    (void)name;
    fake_appvar_exists = false;
    fake_appvar_len = 0;
    return 1;
}

/* ---- os_RunPrgm ------------------------------------------------------- */

void (*fake_run_prgm)(const char *name);
os_runprgm_callback_t fake_run_prgm_callback;

int os_RunPrgm(const char *prgm, void *data, size_t size, os_runprgm_callback_t callback)
{
    (void)data;
    (void)size;
    fake_run_prgm_callback = callback;
    if (fake_run_prgm)
        fake_run_prgm(prgm);   /* the test longjmps out: "doesn't return" */
    return -1;
}
