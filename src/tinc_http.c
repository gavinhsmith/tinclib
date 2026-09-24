/* The one HTTP request: start, drive, read, abort. */

#include <string.h>

#include "tinc_internal.h"

void tinc_endRequest(uint8_t state, tinc_err_t err)
{
    tinc_g.active = false;
    tinc_g.state = state;
    tinc_g.err = err;
    tinc_g.err_detail = 0;
    tinc_g.chunk_len = tinc_g.chunk_pos = 0;
    tinc_g.body_len = 0;
}

/* Ends the request on an error reply, keeping a TLS failure's reason. */
static void fail(tinc_err_t err)
{
    tinc_endRequest(TINC_ERROR, err);
    /* Only these two come with a detail, so the payload is their error reply. */
    if ((err == TINC_ERR_TLS || err == TINC_ERR_CERT) && tinc_g.parser.len > 1)
        tinc_g.err_detail = tinc_g.parser.payload[1];
}

tinc_err_t tinc_request(const tinc_request_t *req)
{
    uint8_t fixed[TINC_BEGIN_URL];
    tinc_piece_t pieces[3];
    size_t url_len, hdr_len;
    tinc_err_t err;

    if (!tinc_g.ready)
        return TINC_ERR_NOT_INIT;
    if (tinc_g.active)
        return TINC_ERR_BUSY;
    if (!req || !req->url || (req->bodyLen && !req->body))
        return TINC_ERR_BAD_ARG;
    if (req->method < TINC_GET || req->method > TINC_HEAD)
        return TINC_ERR_UNSUPPORTED_METHOD;

    url_len = strlen(req->url);
    hdr_len = req->headers ? strlen(req->headers) : 0;
    if (TINC_BEGIN_URL + url_len + hdr_len > tinc_g.peer_max)
        return TINC_ERR_BAD_LEN;

    fixed[TINC_BEGIN_METHOD] = (uint8_t)req->method;   /* same values as TINC_METHOD_* */
    fixed[TINC_BEGIN_FLAGS] = (tinc_g.cfg.flags & TINC_CF_ASCII) ? TINC_REQF_TRANSCODE : 0;
    fixed[TINC_BEGIN_TIMEOUT_S] = tinc_g.cfg.timeoutS;
    tinc_put_u32(fixed + TINC_BEGIN_CONTENT_LEN, req->bodyLen);
    tinc_put_u16(fixed + TINC_BEGIN_URL_LEN, (uint16_t)url_len);
    tinc_put_u16(fixed + TINC_BEGIN_HDR_LEN, (uint16_t)hdr_len);
    pieces[0].p = fixed;
    pieces[0].len = sizeof fixed;
    pieces[1].p = req->url;
    pieces[1].len = (uint16_t)url_len;
    pieces[2].p = req->headers;
    pieces[2].len = (uint16_t)hdr_len;

    tinc_g.http_status = 0;
    tinc_g.ctype[0] = '\0';
    tinc_g.offset = 0;
    tinc_g.eof = false;

    err = tinc_xfer(TINC_T_REQ_BEGIN, pieces, 3, 0);
    if (err != TINC_OK) {
        tinc_endRequest(TINC_ERROR, err);
        return err;
    }
    tinc_g.active = true;
    tinc_g.state = TINC_CONNECTING;
    tinc_g.err = TINC_OK;
    tinc_g.body = req->body;
    tinc_g.body_len = req->bodyLen;
    tinc_g.body_sent = 0;
    return TINC_OK;
}

/* Offers the board the rest of the body. It takes what fits, nothing while
 * still connecting, so this is also the connect poll during an upload. */
static void write_body(void)
{
    uint8_t fixed[TINC_WRITE_DATA];
    tinc_piece_t pieces[2];
    uint16_t n = (uint16_t)(tinc_g.body_len - tinc_g.body_sent);
    const uint8_t *r;
    uint32_t next;
    tinc_err_t err;

    if (n > tinc_g.peer_max - TINC_WRITE_DATA)
        n = (uint16_t)(tinc_g.peer_max - TINC_WRITE_DATA);
    tinc_put_u32(fixed + TINC_WRITE_OFFSET, tinc_g.body_sent);
    fixed[TINC_WRITE_WAIT_MS] = TINC_UPLOAD_WAIT_MS;
    pieces[0].p = fixed;
    pieces[0].len = sizeof fixed;
    pieces[1].p = tinc_g.body + tinc_g.body_sent;
    pieces[1].len = n;

    err = tinc_xfer(TINC_T_BODY_WRITE, pieces, 2, TINC_UPLOAD_WAIT_MS);
    if (!tinc_g.active)
        return;
    r = tinc_g.parser.payload;
    next = tinc_get_u32(r + TINC_WRITE_NEXT_OFFSET);
    if (err == TINC_OK && (tinc_g.parser.len < TINC_WRITE_RESP_LEN ||
                           next < tinc_g.body_sent || next > tinc_g.body_len))
        err = TINC_ERR_BAD_OFFSET;
    if (err != TINC_OK) {
        fail(err);
        return;
    }

    tinc_g.body_sent = (uint16_t)next;
    /* RESPONDED: the server answered early (401, 413...); read that. */
    if (next == tinc_g.body_len || (r[TINC_WRITE_FLAGS] & TINC_WRITEF_RESPONDED)) {
        tinc_g.body_len = 0;
        tinc_g.state = TINC_WAITING;
    } else if (next) {
        tinc_g.state = TINC_SENDING;
    }
}

/* Asks for the request's state until the response headers are in. */
static void poll_status(void)
{
    const uint8_t *r;
    uint8_t n;
    tinc_err_t err = tinc_xfer(TINC_T_REQ_STATUS, NULL, 0, 0);

    if (!tinc_g.active)            /* ended by a board reset */
        return;
    if (err != TINC_OK || tinc_g.parser.len < TINC_RSTAT_CTYPE) {
        tinc_endRequest(TINC_ERROR, err != TINC_OK ? err : TINC_ERR_BAD_LEN);
        return;
    }

    r = tinc_g.parser.payload;
    switch (r[TINC_RSTAT_STATE]) {
    case TINC_RS_CONNECTING:
        tinc_g.state = TINC_CONNECTING;
        break;
    case TINC_RS_TLS:
        tinc_g.state = TINC_SECURING;
        break;
    case TINC_RS_SENDING:
    case TINC_RS_WAIT_HEADERS:
        tinc_g.state = TINC_WAITING;
        break;
    case TINC_RS_BODY:
    case TINC_RS_DONE:
        tinc_g.http_status = tinc_get_u16(r + TINC_RSTAT_HTTP_STATUS);
        n = r[TINC_RSTAT_CTYPE_LEN];
        if (n > TINC_CTYPE_MAX)
            n = TINC_CTYPE_MAX;
        if (n > tinc_g.parser.len - TINC_RSTAT_CTYPE)
            n = (uint8_t)(tinc_g.parser.len - TINC_RSTAT_CTYPE);
        memcpy(tinc_g.ctype, r + TINC_RSTAT_CTYPE, n);
        tinc_g.ctype[n] = '\0';
        tinc_g.state = TINC_BODY;
        break;
    case TINC_RS_ERROR:
        n = r[TINC_RSTAT_CTYPE_LEN];
        tinc_endRequest(TINC_ERROR, r[TINC_RSTAT_ERR] ? r[TINC_RSTAT_ERR] : TINC_ERR_BAD_STATE);
        if (tinc_g.parser.len > TINC_RSTAT_CTYPE + (uint16_t)n)
            tinc_g.err_detail = r[TINC_RSTAT_CTYPE + n];
        break;
    default:                       /* IDLE: the board dropped it */
        tinc_endRequest(TINC_ERROR, TINC_ERR_BAD_STATE);
        break;
    }
}

/* Pre-fetches the next body chunk into frame[]. */
static void read_body(void)
{
    uint8_t req[TINC_READ_REQ_LEN];
    tinc_piece_t piece = { req, sizeof req };
    const uint8_t *r;
    tinc_err_t err;

    tinc_put_u32(req + TINC_READ_OFFSET, tinc_g.offset);
    tinc_put_u16(req + TINC_READ_MAX_LEN, TINC_RX_BUF_SIZE - TINC_READ_DATA);
    req[TINC_READ_WAIT_MS] = TINC_BODY_WAIT_MS;

    err = tinc_xfer(TINC_T_BODY_READ, &piece, 1, TINC_BODY_WAIT_MS);
    if (!tinc_g.active)
        return;
    r = tinc_g.parser.payload;
    if (err == TINC_OK && (tinc_g.parser.len < TINC_READ_DATA ||
                           tinc_get_u32(r + TINC_READ_OFFSET) != tinc_g.offset))
        err = TINC_ERR_BAD_OFFSET;
    if (err != TINC_OK) {
        fail(err);
        return;
    }

    tinc_g.chunk = r + TINC_READ_DATA;
    tinc_g.chunk_len = (uint16_t)(tinc_g.parser.len - TINC_READ_DATA);
    tinc_g.chunk_pos = 0;
    tinc_g.offset += tinc_g.chunk_len;
    tinc_g.eof = (r[TINC_READ_FLAGS] & TINC_READF_EOF) != 0;
}

tinc_state_t tinc_poll(void)
{
    if (tinc_g.active) {
        if (tinc_g.body_len)
            write_body();
        else if (tinc_g.state < TINC_BODY)
            poll_status();
        if (tinc_g.active && tinc_g.state == TINC_BODY && tinc_g.chunk_pos == tinc_g.chunk_len) {
            if (!tinc_g.eof)
                read_body();
            /* DONE only once EOF is in and the app has read everything. */
            if (tinc_g.active && tinc_g.eof && tinc_g.chunk_pos == tinc_g.chunk_len)
                tinc_endRequest(TINC_DONE, TINC_OK);
        }
    }
    return (tinc_state_t)tinc_g.state;
}

int16_t tinc_read(void *buf, uint16_t cap)
{
    uint16_t n = (uint16_t)(tinc_g.chunk_len - tinc_g.chunk_pos);

    if (n > cap)
        n = cap;
    if (n)
        memcpy(buf, tinc_g.chunk + tinc_g.chunk_pos, n);
    tinc_g.chunk_pos = (uint16_t)(tinc_g.chunk_pos + n);
    return (int16_t)n;
}

int16_t tinc_header(const char *name, char *out, uint16_t cap)
{
    uint8_t fixed[TINC_HGET_NAME];
    tinc_piece_t pieces[2];
    size_t name_len = name ? strlen(name) : 0;
    uint16_t room = cap ? (uint16_t)(cap - 1) : 0, got = 0, total = 0, n;
    uint16_t pending = (uint16_t)(tinc_g.chunk_len - tinc_g.chunk_pos);
    int16_t ret = -1;

    if (cap)
        out[0] = '\0';
    if (!tinc_g.ready || (tinc_g.state != TINC_BODY && tinc_g.state != TINC_DONE) ||
        !name_len || name_len > 255 || TINC_HGET_NAME + name_len > tinc_g.peer_max)
        return -1;

    fixed[TINC_HGET_INDEX] = 0;
    fixed[TINC_HGET_NAME_LEN] = (uint8_t)name_len;
    pieces[0].p = fixed;
    pieces[0].len = sizeof fixed;
    pieces[1].p = name;
    pieces[1].len = (uint16_t)name_len;
    do {
        const uint8_t *r = tinc_g.parser.payload;

        tinc_put_u16(fixed + TINC_HGET_OFFSET, got);
        if (tinc_xfer(TINC_T_HDR_GET, pieces, 2, 0) != TINC_OK ||
            tinc_g.parser.len < TINC_HGET_DATA || !(r[TINC_HGET_FLAGS] & TINC_HGETF_FOUND))
            break;
        total = tinc_get_u16(r + TINC_HGET_TOTAL_LEN);
        ret = total > 0x7FFF ? 0x7FFF : (int16_t)total;
        n = (uint16_t)(tinc_g.parser.len - TINC_HGET_DATA);
        if (n > room - got)
            n = (uint16_t)(room - got);
        if (n)
            memcpy(out + got, r + TINC_HGET_DATA, n);
        got = (uint16_t)(got + n);
    } while (n && got < room && got < total);
    if (cap)
        out[got] = '\0';

    /* The reply overwrote the pre-fetched chunk in frame[]; the board hands
     * the same chunk out again for the same offset. */
    if (pending && tinc_g.active) {
        uint16_t pos = tinc_g.chunk_pos;

        tinc_g.offset -= tinc_g.chunk_len;
        read_body();
        if (tinc_g.active)
            tinc_g.chunk_pos = pos;
    }
    return ret;
}

uint16_t tinc_httpStatus(void)
{
    return tinc_g.http_status;
}

const char *tinc_contentType(void)
{
    return tinc_g.ctype;
}

tinc_err_t tinc_error(void)
{
    return tinc_g.err;
}

uint8_t tinc_errDetail(void)
{
    return tinc_g.err_detail;
}

void tinc_abort(void)
{
    bool was_active = tinc_g.active;

    tinc_endRequest(TINC_IDLE, TINC_OK);
    if (was_active)
        tinc_xfer(TINC_T_REQ_ABORT, NULL, 0, 0);
}
