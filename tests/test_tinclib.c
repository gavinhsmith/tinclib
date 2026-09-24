/* Host unit tests: the library against a fake board (stubs/stubs.c). */

#include <setjmp.h>
#include <stdio.h>
#include <string.h>

#include "fake_esp.h"
#include "tinc_frame.h"
#include "tinc_internal.h"   /* tinc_g.seq, to line up with golden frames */
#include "tinclib.h"
#include "vectors.h"

static int failures;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            printf("%s:%d: %s: CHECK(%s) failed\n", __FILE__, __LINE__,      \
                   __func__, #cond);                                         \
            failures++;                                                      \
        }                                                                    \
    } while (0)

static const tinc_config_t app_cfg = { "MYAPP", 0, 0 };

static void setup(void)
{
    tinc_shutdown();
    fake_reset();
    fake_appvar_exists = false;
    fake_run_prgm = NULL;
}

/* Runs a request to the end, collecting the body. */
static tinc_state_t run(char *body, uint16_t cap, uint16_t read_cap)
{
    tinc_state_t st;
    uint16_t len = 0;
    int guard = 0;

    while ((st = tinc_poll()) != TINC_DONE && st != TINC_ERROR && ++guard < 1000) {
        int16_t n = tinc_read(body + len, (uint16_t)(read_cap < cap - 1 - len ? read_cap : cap - 1 - len));
        len = (uint16_t)(len + n);
    }
    body[len] = '\0';
    return st;
}

static tinc_request_t get(const char *url)
{
    tinc_request_t r = { TINC_GET, url, NULL, NULL, 0 };
    return r;
}

/* ---- tests ------------------------------------------------------------ */

/* The CE side of a whole conversation must match tinclib-protocol's golden
 * frames byte for byte, and the golden replies must be understood. */
static void test_golden_vectors(void)
{
    static const tinc_config_t cfg = { NULL, 0, TINC_CF_ASCII };
    static const fake_step_t script[] = {
        { tv_hello_req, sizeof tv_hello_req, tv_hello_resp, sizeof tv_hello_resp },
        { tv_status_req, sizeof tv_status_req, tv_status_resp, sizeof tv_status_resp },
        { tv_req_begin_req, sizeof tv_req_begin_req, tv_req_begin_resp, sizeof tv_req_begin_resp },
        { tv_req_status_req, sizeof tv_req_status_req, tv_req_status_resp, sizeof tv_req_status_resp },
        /* our BODY_READ asks for more than the golden one (bigger buffer) */
        { NULL, 0, tv_body_read_resp, sizeof tv_body_read_resp },
        { NULL, 0, tv_body_read_resp_eof, sizeof tv_body_read_resp_eof },
    };
    tinc_request_t req = { TINC_GET, "http://example.com/api?q=1", "Accept: application/json\r\n", NULL, 0 };
    char body[64];

    setup();
    fake.script = script;
    fake.script_len = sizeof script / sizeof script[0];

    CHECK(tinc_init(&cfg) == TINC_OK);
    CHECK(tinc_isActive(TINC_WIFI));
    CHECK(tinc_request(&req) == TINC_OK);
    CHECK(run(body, sizeof body, 64) == TINC_DONE);
    CHECK(strcmp(body, "{\"ok\":true,\"n\":1}") == 0);
    CHECK(tinc_httpStatus() == 200);
    CHECK(strcmp(tinc_contentType(), "application/json") == 0);
    CHECK(!fake.script_mismatch);
    CHECK(fake.script_pos == fake.script_len);
}

/* 0.2: a STATUS with the Wi-Fi lock flag set still means online. */
static void test_golden_locked_status(void)
{
    static const fake_step_t script[] = {
        { tv_hello_req, sizeof tv_hello_req, tv_hello_resp, sizeof tv_hello_resp },
        { tv_status_req, sizeof tv_status_req, tv_status_resp_locked, sizeof tv_status_resp_locked },
    };

    setup();
    fake.script = script;
    fake.script_len = sizeof script / sizeof script[0];
    CHECK(tinc_init(NULL) == TINC_OK);
    CHECK(tinc_isActive(TINC_WIFI));
    CHECK(!fake.script_mismatch);
    CHECK(fake.script_pos == fake.script_len);
}

/* 0.4: an https request that fails the certificate check. */
static void test_golden_https_cert(void)
{
    static const fake_step_t script[] = {
        { tv_hello_req, sizeof tv_hello_req, tv_hello_resp, sizeof tv_hello_resp },
        { tv_status_req, sizeof tv_status_req, tv_status_resp, sizeof tv_status_resp },
        { tv_req_begin_req_https, sizeof tv_req_begin_req_https, tv_req_begin_resp, sizeof tv_req_begin_resp },
        { tv_req_status_req, sizeof tv_req_status_req, tv_req_status_resp_cert, sizeof tv_req_status_resp_cert },
    };
    tinc_request_t req = get("https://example.com/api?q=1");

    setup();
    fake.script = script;
    fake.script_len = sizeof script / sizeof script[0];
    CHECK(tinc_init(NULL) == TINC_OK);
    CHECK(tinc_isActive(TINC_WIFI));   /* STATUS with TIME_VALID set */
    CHECK(tinc_request(&req) == TINC_OK);
    CHECK(tinc_poll() == TINC_ERROR);
    CHECK(tinc_error() == TINC_ERR_CERT);
    CHECK(tinc_errDetail() == TINC_TLSR_HOSTNAME);
    CHECK(!fake.script_mismatch);
    CHECK(fake.script_pos == fake.script_len);
}

/* 0.5: a POST whose body the board takes in two writes. */
static void test_golden_post(void)
{
    static const fake_step_t script[] = {
        { tv_hello_req, sizeof tv_hello_req, tv_hello_resp, sizeof tv_hello_resp },
        { tv_status_req, sizeof tv_status_req, tv_status_resp, sizeof tv_status_resp },
        { tv_req_begin_req_post, sizeof tv_req_begin_req_post, tv_req_begin_resp, sizeof tv_req_begin_resp },
        { tv_body_write_req, sizeof tv_body_write_req, tv_body_write_resp_partial, sizeof tv_body_write_resp_partial },
        { tv_body_write_req_rest, sizeof tv_body_write_req_rest, tv_body_write_resp_done, sizeof tv_body_write_resp_done },
    };
    static const char body[] = "{\"name\":\"calc\",\"n\":84}";
    tinc_request_t req = { TINC_POST, "https://example.com/api?q=1",
                           "Content-Type: application/json\r\n", body, sizeof body - 1 };

    setup();
    fake.script = script;
    fake.script_len = sizeof script / sizeof script[0];
    CHECK(tinc_init(NULL) == TINC_OK);
    CHECK(tinc_isActive(TINC_WIFI));
    CHECK(tinc_request(&req) == TINC_OK);
    tinc_g.seq = 0x14;                 /* the golden writes are SEQ 0x15, 0x16 */
    CHECK(tinc_poll() == TINC_SENDING);
    CHECK(tinc_poll() == TINC_WAITING);
    CHECK(!fake.script_mismatch);
    CHECK(fake.script_pos == fake.script_len);
}

/* An upload the board takes a bit at a time, nothing while connecting. */
static void test_post_upload(void)
{
    static char up[600];
    char body[32];
    tinc_request_t req = { TINC_POST, "http://x/", NULL, up, sizeof up };
    uint16_t i;

    for (i = 0; i < sizeof up; i++)
        up[i] = (char)('a' + i % 26);
    setup();
    fake.req_polls = 2;
    fake.write_max = 100;
    fake.http_status = 201;
    fake.body = "created";
    CHECK(tinc_init(NULL) == TINC_OK);
    CHECK(tinc_request(&req) == TINC_OK);
    CHECK(fake.method == TINC_METHOD_POST && fake.content_len == sizeof up);
    CHECK(tinc_poll() == TINC_CONNECTING);
    CHECK(tinc_poll() == TINC_CONNECTING);
    CHECK(tinc_poll() == TINC_SENDING);
    CHECK(run(body, sizeof body, 32) == TINC_DONE);
    CHECK(strcmp(body, "created") == 0);
    CHECK(tinc_httpStatus() == 201);
    CHECK(fake.upload_len == sizeof up && memcmp(fake.upload, up, sizeof up) == 0);
    CHECK(fake.executed[TINC_T_BODY_WRITE] == 2 + 6);
}

/* The server answers mid-upload (413): stop sending, read the answer. */
static void test_post_responded(void)
{
    static char up[100];
    char body[32];
    tinc_request_t req = { TINC_PUT, "http://x/", NULL, up, sizeof up };

    setup();
    fake.write_max = 10;
    fake.respond_early = true;
    fake.http_status = 413;
    fake.body = "too big";
    CHECK(tinc_init(NULL) == TINC_OK);
    CHECK(tinc_request(&req) == TINC_OK);
    CHECK(tinc_poll() == TINC_WAITING);
    CHECK(run(body, sizeof body, 32) == TINC_DONE);
    CHECK(tinc_httpStatus() == 413);
    CHECK(strcmp(body, "too big") == 0);
    CHECK(fake.upload_len == 10);
    CHECK(fake.executed[TINC_T_BODY_WRITE] == 1);
}

static void test_methods(void)
{
    static const tinc_method_t methods[] = { TINC_PUT, TINC_PATCH, TINC_DELETE, TINC_HEAD };
    char body[8];
    uint8_t i;

    setup();
    CHECK(tinc_init(NULL) == TINC_OK);
    for (i = 0; i < 4; i++) {
        tinc_request_t req = { methods[i], "http://x/", NULL, NULL, 0 };

        fake.executed[TINC_T_BODY_WRITE] = 0;
        CHECK(tinc_request(&req) == TINC_OK);
        CHECK(fake.method == (uint8_t)methods[i] && fake.content_len == 0);
        CHECK(run(body, sizeof body, 8) == TINC_DONE);   /* no body at all */
        CHECK(body[0] == '\0');
        CHECK(tinc_httpStatus() == 200);
        CHECK(fake.executed[TINC_T_BODY_WRITE] == 0);
    }
}

/* HDR_GET pages through a long value, and a body chunk that was already
 * fetched survives it. */
static void test_header(void)
{
    static char loc[301], big[500];
    char out[400], small[8], body[600];
    uint16_t len;
    tinc_request_t req = get("http://x/");

    memset(loc, 'L', sizeof loc - 1);
    memset(big, 'b', sizeof big - 1);
    big[0] = 'B';
    setup();
    fake.hdr_name = "Location";
    fake.hdr_value = loc;
    fake.body = big;
    fake.chunk_max = 100;
    fake.req_polls = 1;
    CHECK(tinc_init(NULL) == TINC_OK);
    CHECK(tinc_request(&req) == TINC_OK);
    CHECK(tinc_header("Location", out, sizeof out) == -1);   /* not yet */
    while (tinc_poll() != TINC_BODY)
        ;
    len = (uint16_t)tinc_read(body, 10);                    /* 90 left pending */

    CHECK(tinc_header("Location", out, sizeof out) == 300);
    CHECK(strcmp(out, loc) == 0);
    CHECK(fake.executed[TINC_T_HDR_GET] == 2);               /* two pages */
    CHECK(tinc_header("Location", small, sizeof small) == 300);
    CHECK(strcmp(small, "LLLLLLL") == 0);
    CHECK(tinc_header("X-Missing", out, sizeof out) == -1);
    CHECK(out[0] == '\0');
    CHECK(tinc_header("Location", NULL, 0) == 300);

    CHECK(run(body + len, (uint16_t)(sizeof body - len), 64) == TINC_DONE);
    CHECK(strcmp(body, big) == 0);
    CHECK(tinc_header("Location", out, sizeof out) == 300);  /* DONE still has it */
}

/* The TLS reason can also come in a BODY_READ error reply. */
static void test_err_detail_body_read(void)
{
    static const uint8_t err[] = { TINC_ERR_TLS, TINC_TLSR_ALERT };
    static uint8_t body_err[TINC_OVERHEAD + sizeof err];
    static fake_step_t script[] = {
        { tv_hello_req, sizeof tv_hello_req, tv_hello_resp, sizeof tv_hello_resp },
        { tv_status_req, sizeof tv_status_req, tv_status_resp, sizeof tv_status_resp },
        { tv_req_begin_req, sizeof tv_req_begin_req, tv_req_begin_resp, sizeof tv_req_begin_resp },
        { tv_req_status_req, sizeof tv_req_status_req, tv_req_status_resp, sizeof tv_req_status_resp },
        { NULL, 0, body_err, sizeof body_err },
    };
    static const tinc_config_t cfg = { NULL, 0, TINC_CF_ASCII };
    tinc_request_t req = { TINC_GET, "http://example.com/api?q=1", "Accept: application/json\r\n", NULL, 0 };

    tinc_frame_encode(body_err, TINC_FLAG_RESP | TINC_FLAG_ERR, TINC_T_BODY_READ, 5, err, sizeof err);
    setup();
    fake.script = script;
    fake.script_len = sizeof script / sizeof script[0];
    CHECK(tinc_init(&cfg) == TINC_OK);
    CHECK(tinc_isActive(TINC_WIFI));
    CHECK(tinc_request(&req) == TINC_OK);
    CHECK(tinc_poll() == TINC_ERROR);
    CHECK(tinc_error() == TINC_ERR_TLS);
    CHECK(tinc_errDetail() == TINC_TLSR_ALERT);
    CHECK(!fake.script_mismatch);

    /* Any other error clears it. */
    setup();
    fake.req_err = TINC_ERR_DNS;
    CHECK(tinc_init(NULL) == TINC_OK);
    CHECK(tinc_request(&req) == TINC_OK);
    CHECK(tinc_poll() == TINC_ERROR);
    CHECK(tinc_errDetail() == 0);
}

/* https goes through SECURING on its way to the body. */
static void test_https(void)
{
    tinc_request_t req = get("https://example.com/");
    char body[16];

    setup();
    fake.req_polls = 1;
    fake.body = "ok";
    CHECK(tinc_init(NULL) == TINC_OK);
    CHECK(tinc_request(&req) == TINC_OK);
    CHECK(tinc_poll() == TINC_SECURING);
    CHECK(tinc_poll() == TINC_WAITING);
    CHECK(run(body, sizeof body, 16) == TINC_DONE);
    CHECK(strcmp(body, "ok") == 0);
}

static void test_no_device(void)
{
    tinc_request_t req = get("http://x/");

    setup();
    fake.present = false;
    CHECK(tinc_init(NULL) == TINC_ERR_NO_DEVICE);
    CHECK(!tinc_isActive(TINC_WIFI));
    CHECK(tinc_request(&req) == TINC_ERR_NOT_INIT);
    CHECK(tinc_poll() == TINC_IDLE);
}

static void test_version_mismatch(void)
{
    setup();
    fake.minor = TINC_PROTO_MINOR + 1;
    CHECK(tinc_init(NULL) == TINC_ERR_VERSION);
    CHECK(!tinc_isActive(TINC_WIFI));
}

static void test_is_active(void)
{
    unsigned long start;

    setup();
    fake.wifi_connecting_polls = 3;
    CHECK(tinc_init(NULL) == TINC_OK);
    CHECK(tinc_isActive(TINC_WIFI));

    /* A locked board is still online (0.2 STATUS flags byte). */
    fake.wifi_locked = true;
    CHECK(tinc_isActive(TINC_WIFI));
    fake.wifi_locked = false;

    fake.wifi_state = TINC_WIFI_NO_CREDS;
    CHECK(!tinc_isActive(TINC_WIFI));
    CHECK(fake.executed[TINC_T_STATUS] == 6);  /* answered at once */

    /* Stuck connecting: gives up after about TINC_WIFI_WAIT_MS. */
    fake.wifi_connecting_polls = 255;
    start = fake_now;
    CHECK(!tinc_isActive(TINC_WIFI));
    CHECK((fake_now - start) / 1000 >= TINC_WIFI_WAIT_MS);
    CHECK((fake_now - start) / 1000 < TINC_WIFI_WAIT_MS + 1000);
}

static void test_get_chunked(void)
{
    static char big[1000];
    static char body[1100];
    tinc_config_t cfg = { NULL, 7, TINC_CF_ASCII };
    tinc_request_t req = get("http://example.com/big");
    int i;

    for (i = 0; i < 999; i++)
        big[i] = (char)('a' + i % 26);
    setup();
    fake.body = big;
    fake.ctype = "text/plain";
    fake.req_polls = 2;
    fake.empty_reads = 2;
    fake.chunk_max = 100;

    CHECK(tinc_init(&cfg) == TINC_OK);
    CHECK(tinc_request(&req) == TINC_OK);
    CHECK(strcmp(fake.url, "http://example.com/big") == 0);
    CHECK(fake.req_flags == TINC_REQF_TRANSCODE);
    CHECK(fake.req_timeout == 7);
    CHECK(tinc_httpStatus() == 0);

    CHECK(tinc_poll() == TINC_CONNECTING);
    CHECK(tinc_poll() == TINC_CONNECTING);
    CHECK(tinc_poll() == TINC_WAITING);
    CHECK(run(body, sizeof body, 33) == TINC_DONE);
    CHECK(strcmp(body, big) == 0);
    CHECK(tinc_httpStatus() == 200);
    CHECK(strcmp(tinc_contentType(), "text/plain") == 0);
    CHECK(tinc_poll() == TINC_DONE);
    CHECK(tinc_read(body, 10) == 0);

    /* A finished request is released: a new one can start. */
    CHECK(tinc_request(&req) == TINC_OK);
}

static void test_last_chunk_held_until_read(void)
{
    char buf[8];
    tinc_request_t req = get("http://x/");

    setup();
    fake.body = "hi";
    CHECK(tinc_init(NULL) == TINC_OK);
    CHECK(tinc_request(&req) == TINC_OK);
    CHECK(tinc_poll() == TINC_BODY);    /* EOF is in, "hi" not read yet */
    CHECK(tinc_poll() == TINC_BODY);
    CHECK(tinc_read(buf, sizeof buf) == 2);
    CHECK(tinc_poll() == TINC_DONE);
}

/* frame[] holds both the pending body chunk and every reply, so a
 * tinc_isActive() between reads must not exchange anything mid-chunk. */
static void test_is_active_mid_chunk(void)
{
    static char big[300];
    char body[310];
    tinc_request_t req = get("http://x/");
    tinc_state_t st;
    uint16_t len = 0;
    int i, guard = 0;

    for (i = 0; i < 299; i++)
        big[i] = (char)('A' + i % 26);
    setup();
    fake.body = big;
    fake.chunk_max = 100;
    CHECK(tinc_init(NULL) == TINC_OK);
    CHECK(tinc_request(&req) == TINC_OK);
    while ((st = tinc_poll()) != TINC_DONE && st != TINC_ERROR && ++guard < 1000) {
        uint16_t statuses = fake.executed[TINC_T_STATUS];
        int16_t n = tinc_read(body + len, 7);   /* leaves most of each chunk pending */

        len = (uint16_t)(len + n);
        CHECK(tinc_isActive(TINC_WIFI));
        if (n == 7)
            CHECK(fake.executed[TINC_T_STATUS] == statuses);
    }
    body[len] = '\0';
    CHECK(st == TINC_DONE);
    CHECK(strcmp(body, big) == 0);
    CHECK(fake.executed[TINC_T_STATUS] > 0);  /* between chunks it does ask */
}

static void test_lost_reply_is_not_rerun(void)
{
    char body[64];
    tinc_request_t req = get("http://x/");

    setup();
    fake.body = "0123456789";
    fake.chunk_max = 4;
    CHECK(tinc_init(NULL) == TINC_OK);
    fake.drop_replies = 1;           /* REQ_BEGIN's reply */
    CHECK(tinc_request(&req) == TINC_OK);
    CHECK(fake.executed[TINC_T_REQ_BEGIN] == 1);
    CHECK(fake.frames_in == 3);      /* HELLO, REQ_BEGIN twice */

    CHECK(tinc_poll() == TINC_BODY);
    fake.drop_replies = 2;           /* a BODY_READ reply, twice */
    CHECK(run(body, sizeof body, 64) == TINC_DONE);
    CHECK(strcmp(body, "0123456789") == 0);
}

/* A PC host answers only once its app has the port open: HELLO keeps
 * being retried until TINC_DEVICE_WAIT_MS, then gives up. */
static void test_hello_late_reply(void)
{
    unsigned long start, elapsed_ms;

    setup();
    fake.drop_replies = TINC_RETRY_MAX * 2;
    CHECK(tinc_init(NULL) == TINC_OK);
    CHECK(fake.frames_in == TINC_RETRY_MAX * 2 + 1);

    setup();
    fake.drop_replies = 255;
    start = fake_now;
    CHECK(tinc_init(NULL) == TINC_ERR_NO_REPLY);
    elapsed_ms = (fake_now - start) / 1000;
    CHECK(elapsed_ms >= TINC_DEVICE_WAIT_MS);
    CHECK(elapsed_ms < TINC_DEVICE_WAIT_MS + TINC_RETRY_MAX * TINC_REPLY_TIMEOUT_MS + 100);
}

static void test_board_gone_quiet(void)
{
    tinc_request_t req = get("http://x/");

    setup();
    CHECK(tinc_init(NULL) == TINC_OK);
    fake.drop_replies = 255;
    CHECK(tinc_request(&req) == TINC_ERR_NO_REPLY);
    CHECK(tinc_poll() == TINC_ERROR);
    CHECK(tinc_error() == TINC_ERR_NO_REPLY);
}

static void test_esp_reset_mid_request(void)
{
    char body[16];
    tinc_request_t req = get("http://x/");

    setup();
    fake.body = "ok";
    fake.req_polls = 1;
    CHECK(tinc_init(NULL) == TINC_OK);
    CHECK(tinc_request(&req) == TINC_OK);
    CHECK(tinc_poll() == TINC_CONNECTING);
    fake_reboot();
    CHECK(tinc_poll() == TINC_ERROR);
    CHECK(tinc_error() == TINC_ERR_ESP_RESET);
    CHECK(fake.executed[TINC_T_HELLO] == 2);      /* re-handshook */
    CHECK(fake.executed[TINC_T_REQ_BEGIN] == 1);  /* not resent */

    /* Commands outside a request just go through after the re-handshake. */
    fake_reboot();
    CHECK(tinc_isActive(TINC_WIFI));
    CHECK(tinc_request(&req) == TINC_OK);
    CHECK(run(body, sizeof body, 16) == TINC_DONE);
    CHECK(strcmp(body, "ok") == 0);
}

static void test_request_errors(void)
{
    static char long_url[1100];
    tinc_request_t req = get("http://nowhere/");
    tinc_request_t bad = { (tinc_method_t)7, "http://x/", NULL, NULL, 0 };
    tinc_request_t no_body = { TINC_POST, "http://x/", NULL, NULL, 3 };
    tinc_request_t get_body = { TINC_GET, "http://x/", NULL, "a=1", 3 };

    setup();
    fake.req_err = TINC_ERR_DNS;
    fake.req_polls = 1;
    CHECK(tinc_init(NULL) == TINC_OK);
    CHECK(tinc_request(&req) == TINC_OK);
    CHECK(tinc_poll() == TINC_CONNECTING);
    CHECK(tinc_poll() == TINC_ERROR);
    CHECK(tinc_error() == TINC_ERR_DNS);

    CHECK(tinc_request(&bad) == TINC_ERR_UNSUPPORTED_METHOD);
    CHECK(tinc_request(&no_body) == TINC_ERR_BAD_ARG);
    CHECK(tinc_request(&get_body) == TINC_ERR_BAD_ARG);   /* the board refuses it */
    CHECK(tinc_request(NULL) == TINC_ERR_BAD_ARG);
    memset(long_url, 'a', sizeof long_url - 1);
    req.url = long_url;
    CHECK(tinc_request(&req) == TINC_ERR_BAD_LEN);

    fake.wifi_state = TINC_WIFI_FAILED;
    req.url = "http://x/";
    CHECK(tinc_request(&req) == TINC_ERR_WIFI_DOWN);
    CHECK(tinc_poll() == TINC_ERROR);
    CHECK(tinc_error() == TINC_ERR_WIFI_DOWN);
}

static void test_busy_and_abort(void)
{
    tinc_request_t req = get("http://x/");

    setup();
    fake.req_polls = 5;
    CHECK(tinc_init(NULL) == TINC_OK);
    CHECK(tinc_request(&req) == TINC_OK);
    CHECK(tinc_request(&req) == TINC_ERR_BUSY);
    tinc_abort();
    CHECK(fake.executed[TINC_T_REQ_ABORT] == 1);
    CHECK(tinc_poll() == TINC_IDLE);
    CHECK(tinc_request(&req) == TINC_OK);
    tinc_shutdown();                 /* aborts too */
    CHECK(fake.executed[TINC_T_REQ_ABORT] == 2);
}

static void test_small_buffers(void)
{
    char body[600];
    static char big[500];
    tinc_request_t req = get("http://x/");

    /* The board must never send more than we advertised in HELLO. */
    memset(big, 'z', sizeof big - 1);
    setup();
    fake.body = big;
    CHECK(tinc_init(NULL) == TINC_OK);
    CHECK(tinc_request(&req) == TINC_OK);
    CHECK(run(body, sizeof body, 600) == TINC_DONE);
    CHECK(strcmp(body, big) == 0);
    CHECK(fake.executed[TINC_T_BODY_READ] == 2);  /* 251 + 248 */

    /* ...and we must never send more than it advertised. */
    fake.max_payload = TINC_PAYLOAD_MIN;
    CHECK(tinc_init(NULL) == TINC_OK);
    req.url = "http://example.com/a/path/that/is/long/enough/to/pass/64";
    CHECK(tinc_request(&req) == TINC_ERR_BAD_LEN);
}

/* ---- handoff ---------------------------------------------------------- */

static jmp_buf relaunch;
static uint8_t config_result;
static bool config_bad_nonce;
static char config_ran[16];

/* Plays TINCLIBC: fills in the result and exits, which restarts the app. */
static void fake_tinclibc(const char *name)
{
    uint16_t at;

    strcpy(config_ran, name);
    at = (uint16_t)(18 + fake_appvar[17]);
    fake_appvar[at] = config_result;
    memcpy(fake_appvar + at + 1, fake_appvar + 2, 4);
    if (config_bad_nonce)
        fake_appvar[at + 1] ^= 0xFF;
    fake_appvar[at + 5] = 0;
    fake_appvar_len = (uint16_t)(at + 6);
    longjmp(relaunch, 1);
}

/* openConfig -> TINCLIBC -> app again. Returns what the restarted app's
 * tinc_init says. */
static tinc_err_t handoff(uint8_t result)
{
    config_result = result;
    fake_run_prgm = fake_tinclibc;
    if (!setjmp(relaunch)) {
        tinc_openConfig("Needs Wi-Fi");
        return 0xFF;  /* openConfig returned: wrong */
    }
    return tinc_init(&app_cfg);
}

static void test_handoff(void)
{
    static const tinc_config_t other = { "OTHER", 0, 0 };

    setup();
    CHECK(tinc_init(NULL) == TINC_OK);
    CHECK(tinc_openConfig("x") == TINC_ERR_BAD_ARG);   /* no appName */

    CHECK(tinc_init(&app_cfg) == TINC_OK);
    CHECK(handoff(2) == TINC_ERR_SETUP_CANCELLED);
    CHECK(strcmp(config_ran, "TINCLIBC") == 0);
    CHECK(fake_run_prgm_callback != NULL);            /* comes back to us */
    CHECK(!fake_appvar_exists);                        /* consumed... */
    CHECK(tinc_init(&app_cfg) == TINC_OK);             /* ...once */
    CHECK(tinc_isActive(TINC_WIFI));                   /* and the link works */

    CHECK(handoff(3) == TINC_ERR_SETUP_FAILED);
    CHECK(handoff(1) == TINC_OK);

    /* The layout TINCLIBC reads. */
    CHECK(handoff(1) == TINC_OK);
    setup();
    tinc_init(&app_cfg);
    fake_run_prgm = fake_tinclibc;
    config_result = 2;
    if (!setjmp(relaunch))
        tinc_openConfig("Needs Wi-Fi");
    CHECK(fake_appvar[0] == 'H' && fake_appvar[1] == 1);
    CHECK(memcmp(fake_appvar + 6, "MYAPP\0\0\0\0", 9) == 0);
    CHECK(fake_appvar[15] == 1 && fake_appvar[16] == 1);
    CHECK(fake_appvar[17] == 11 && memcmp(fake_appvar + 18, "Needs Wi-Fi", 11) == 0);

    /* Meant for another program: left alone. */
    CHECK(tinc_init(&other) == TINC_OK);
    CHECK(fake_appvar_exists);
    CHECK(tinc_init(&app_cfg) == TINC_ERR_SETUP_CANCELLED);
    /* Nonce echo wrong: stale, ignored and dropped. */
    config_bad_nonce = true;
    CHECK(handoff(2) == TINC_OK);
    CHECK(!fake_appvar_exists);
    config_bad_nonce = false;

    /* TINCLIBC missing: openConfig comes back and cleans up. */
    fake_run_prgm = NULL;
    CHECK(tinc_init(&app_cfg) == TINC_OK);
    CHECK(tinc_openConfig("hint") == TINC_ERR_NO_CONFIG_APP);
    CHECK(!fake_appvar_exists);
}

static void test_err_strings(void)
{
    static const tinc_err_t codes[] = {
        TINC_OK, TINC_ERR_UNSUPPORTED, TINC_ERR_NO_HELLO, TINC_ERR_VERSION, TINC_ERR_BAD_LEN,
        TINC_ERR_BUSY, TINC_ERR_BAD_STATE, TINC_ERR_BAD_OFFSET, TINC_ERR_BAD_ARG,
        TINC_ERR_UNSUPPORTED_SCHEME, TINC_ERR_LOCKED, TINC_ERR_WIFI_DOWN, TINC_ERR_DNS, TINC_ERR_CONNECT,
        TINC_ERR_TIMEOUT, TINC_ERR_HTTP_PROTO, TINC_ERR_TOO_MANY_REDIRECTS, TINC_ERR_NO_MEM,
        TINC_ERR_TLS, TINC_ERR_CERT, TINC_ERR_TIME, TINC_ERR_REDIRECT_DOWNGRADE,
        TINC_ERR_NO_DEVICE, TINC_ERR_NO_REPLY, TINC_ERR_ESP_RESET, TINC_ERR_NOT_INIT,
        TINC_ERR_UNSUPPORTED_METHOD, TINC_ERR_SETUP_CANCELLED, TINC_ERR_SETUP_FAILED,
        TINC_ERR_NO_CONFIG_APP,
    };
    size_t i;

    for (i = 0; i < sizeof codes; i++)
        CHECK(strcmp(tinc_errString(codes[i]), "Unknown error") != 0);
    CHECK(strcmp(tinc_errString(0x7F), "Unknown error") == 0);
}

#define RUN(test)                \
    do {                         \
        printf("%s\n", #test);   \
        fflush(stdout);          \
        test();                  \
    } while (0)

int main(void)
{
    RUN(test_golden_vectors);
    RUN(test_golden_locked_status);
    RUN(test_golden_https_cert);
    RUN(test_err_detail_body_read);
    RUN(test_https);
    RUN(test_no_device);
    RUN(test_version_mismatch);
    RUN(test_is_active);
    RUN(test_get_chunked);
    RUN(test_last_chunk_held_until_read);
    RUN(test_is_active_mid_chunk);
    RUN(test_lost_reply_is_not_rerun);
    RUN(test_golden_post);
    RUN(test_post_upload);
    RUN(test_post_responded);
    RUN(test_methods);
    RUN(test_header);
    RUN(test_hello_late_reply);
    RUN(test_board_gone_quiet);
    RUN(test_esp_reset_mid_request);
    RUN(test_request_errors);
    RUN(test_busy_and_abort);
    RUN(test_small_buffers);
    RUN(test_handoff);
    RUN(test_err_strings);
    tinc_shutdown();

    if (failures) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("all tests passed\n");
    return 0;
}
